#!/usr/bin/env python3
"""Fit the unit trims to NAM captures of real RAT2s (the "Captured" calibration).

Stage 1 fits the global trims (op amp slew rate, slew asymmetry, bandwidth and output swing,
diode emission, pot laws, the JFET's bias) to the captures with known knob positions: by
default outmoded's 20 (+11.5 dBu, clock positions given) and stomptones' 4 (whose input level
is fitted too). The objective is the log-spectral distance plus a little ESR, plus the level
error on captures whose levels and VOLUME are known, plus the clipping asymmetry error
(positive over negative peak). Candidates that fail the long-run stability check
(specs/stability.json) are rejected.

--opamp op07 fits the OP07's trims instead of the LM308's (to captures of OP07 units).

Stage 2 holds the trims and checks them on a different pedal: romanrecords' 7 x 7 grid of a
1990s RAT (+3.98 dBu). Its knob positions are unknown, so the pot rotation at each index is
fitted; the resulting LSD is the out-of-sample number, and monotonic positions are a sanity
check of the tapers.

Usage: fit_nam.py [--opamp lm308|op07] [--captures SLUG,...] [--quick] [--maxiter N]
                  [--only NAME,...] [--start NAME=VALUE ...] [--no-stage2]
                  [--stage2-only --set ...]   (repo venv, ../../.venv)
Prints the C++ for capturedCalibration() and writes build/fit/result.json.
"""
import argparse
import concurrent.futures as cf
import json
import pathlib
import sys
import time

import numpy as np
from scipy.optimize import minimize

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import compare  # noqa: E402
import stability  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "build/fit"

# name, start, lower, upper, log-scaled
# Bounds are physical, from the bench measurements of RAT op amp stages at 9 V and the datasheets
# (private/reference/measurements; research.md sections 2 and 11):
# - LM308 with 30 pF: slew 0.11-0.24 V/us at 9 V, every chip rising slower than it falls
#   (rise/fall 0.71-0.86, so asymmetry -0.17..-0.08); the swing symmetric about the bias within
#   0.13 V (the positive side, if either, clipping first; the 1990 RAT2's duty says 0.2 V),
#   3.7-4.0 V each way at light load (the drop under the clipper's load is the output resistance).
#   The LM308's swing is fitted as a half-range and a shift so that holds.
# - OP07: slew 0.18-0.25 V/us, nearly symmetric on the bench; a hard negative limit 3.17 V
#   below the bias and the positive side beyond +3.95 V.
# - 2N5458: real units' sources sit at 2.6-2.9 V in this circuit (research.md section 11), so
#   Vto is held where the LTspice beta gives 2.5-3.0 V.
# - Pots: Alpha's laws 05A-30A, the track ending at 85-99 % of rotation.
VREF = 4.49  # the bias at the op amp's input and output (DC operating point)
PARAMS_LM308 = [
    ("slew", 0.2, 0.1, 0.3, True),
    ("slewasym", -0.12, -0.25, 0.0, False),
    ("gbw", 0.55e6, 0.35e6, 0.9e6, True),
    ("swing", 3.8, 3.5, 4.1, False),     # virtual: outlo/outhi = VREF -/+ swing + shift
    ("shift", -0.05, -0.12, 0.0, False),
    ("outohms", 60.0, 20.0, 300.0, True),
]
PARAMS_OP07 = [
    ("op07slew", 0.23, 0.12, 0.45, True),
    ("op07slewasym", -0.05, -0.4, 0.1, False),
    ("op07gbw", 0.6e6, 0.35e6, 1.2e6, True),
    ("op07outlo", 1.32, 1.15, 1.5, False),
    ("op07outhi", 8.5, 8.3, 8.8, False),
    ("op07outohms", 60.0, 20.0, 300.0, True),
]
PARAMS_COMMON = [
    ("dioden", 1.8, 1.3, 2.6, False),
    ("distmid", 0.15, 0.04, 0.3, True),
    ("filtermid", 0.15, 0.04, 0.3, True),
    ("distend", 0.94, 0.85, 0.99, False),
    ("filterend", 0.94, 0.85, 0.99, False),
    ("volumemid", 0.15, 0.05, 0.3, True),
    ("volumeend", 0.94, 0.85, 0.99, False),
    ("jfetvto", -3.5, -3.78, -3.22, False),
]


def calibration_sets(names, vals):
    """The trims as calibration fields (the LM308's swing and shift become outlo and outhi)."""
    d = dict(zip(names, vals))
    if "swing" in d:
        sw, sh = d.pop("swing"), d.pop("shift", 0.0)
        d["outlo"], d["outhi"] = VREF - sw + sh, VREF + sw + sh
    return list(d.items())


PARAMS = PARAMS_LM308 + PARAMS_COMMON
FIT_SIGNALS = ["pluck-mf", "pluck-soft", "sine-steps", "sweep", "tb303"]


def to_x(vals):
    return np.array([np.log(v) if lg else v for (n, s, lo, hi, lg), v in zip(PARAMS, vals)])


def from_x(x):
    out = []
    for (n, s, lo, hi, lg), xi in zip(PARAMS, x):
        v = np.exp(xi) if lg else xi
        out.append(float(np.clip(v, lo, hi)))
    return out


LEVEL_WEIGHT = 0.5  # per dB of level error, on captures with known levels and VOLUME
ASYM_WEIGHT = 10.0  # per unit of |ln(ours / ref)| of the positive over negative peak
LEVELLED = set()     # captures whose output level is meaningful (filled in main)


def objective_rows(rows):
    lsd = compare.summary(rows, "lsd_db")
    esr = np.mean([max(r["esr_db"], -40.0) for r in rows])
    lev = [abs(r["gain_db"]) for r in rows if r["capture"] in LEVELLED]
    level = float(np.mean(lev)) if lev else 0.0
    asym = float(np.mean([abs(np.log(r["asym_ours"] / r["asym_ref"])) for r in rows]))
    return lsd + 0.1 * esr + LEVEL_WEIGHT * level + ASYM_WEIGHT * asym, lsd, esr, level, asym


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--maxiter", type=int, default=160)
    ap.add_argument("--stage2-only", action="store_true")
    ap.add_argument("--set", action="append", default=[], help="NAME=VALUE held during the fit")
    ap.add_argument("--only", help="comma-separated trims to fit; the rest stay at their start values")
    ap.add_argument("--start", action="append", default=[], help="NAME=VALUE start (or fixed) value")
    ap.add_argument("--opamp", choices=["lm308", "op07"], default="lm308")
    ap.add_argument("--captures", default="outmoded-rat2-11dbu,stomptones-rat2",
                    help="comma-separated captures to fit (stomptones-rat2's input level is fitted)")
    ap.add_argument("--no-stage2", action="store_true")
    ap.add_argument("--parts", choices=["rat", "rat2", "china"], default="rat", help="part values (rat: the 1984 RAT; rat2: US RAT2s; china: current RAT2s)")
    ap.add_argument("--tag", default="", help="name for build/fit/<tag>.json and .log (default result.json)")
    a = ap.parse_args()
    global PARAMS
    PARAMS = (PARAMS_OP07 if a.opamp == "op07" else PARAMS_LM308) + PARAMS_COMMON
    starts = {kv.split("=")[0]: float(kv.split("=")[1]) for kv in a.start}
    PARAMS = [(n, starts.get(n, s0), lo, hi, lg) for (n, s0, lo, hi, lg) in PARAMS]
    fixed = []
    if a.only:
        keep = a.only.split(",")
        fixed = [(n, s0) for (n, s0, lo, hi, lg) in PARAMS if n not in keep]
        PARAMS = [p for p in PARAMS if p[0] in keep]
    OUT.mkdir(parents=True, exist_ok=True)
    suite = compare.load_suite()
    sigs = [s for s in suite["signals"] if s["name"] in (FIT_SIGNALS[:2] if a.quick else FIT_SIGNALS)]
    fitcaps = a.captures.split(",")
    known = [c for c in suite["captures"] if c["capture"] in fitcaps]
    LEVELLED.update(c["capture"] for c in known
                    if c.get("out_level_dbu") is not None and c.get("level_dbu") is not None and "volume" in c)
    print(f"fitting {len(known)} captures of {', '.join(fitcaps)}; levels from {', '.join(sorted(LEVELLED)) or 'none'}")
    roman = [c for c in suite["captures"] if c["capture"] == "romanrecords-rat-90s"]
    log = open(OUT / f"{a.tag or 'fit'}.log", "a")

    pool = cf.ProcessPoolExecutor()
    best = {"j": 1e9}

    st_level = [0.0]  # first fit: stomptones at about 0 dBu
    if a.stage2_only:
        sets = [(kv.split("=")[0], float(kv.split("=")[1])) for kv in a.set]
    else:
        def evaluate(x, st_level):
            vals = from_x(x)
            sets = calibration_sets([p[0] for p in PARAMS] + [n for n, _ in fixed], vals + [v for _, v in fixed])
            sets += [(kv.split("=")[0], float(kv.split("=")[1])) for kv in a.set]  # held values
            rows = compare.evaluate(known, sigs, "datasheet", sets, levels={"stomptones-rat2": st_level}, pool=pool,
                                    opamp=a.opamp, parts=a.parts)
            return objective_rows(rows), sets

        x0 = to_x([p[1] for p in PARAMS])

        def f(x):
            t0 = time.time()
            (j, lsd, esr, level, asym), sets = evaluate(x, st_level[0])
            msg = f"j={j:.4f} lsd={lsd:.3f} esr={esr:.2f} level={level:.2f} asym={asym:.3f} " + \
                  " ".join(f"{k}={v:.4g}" for k, v in sets) + \
                  f" stlevel={st_level[0]:.2f} ({time.time() - t0:.1f}s)"
            print(msg, flush=True)
            log.write(msg + "\n")
            log.flush()
            if j < best["j"]:
                best.update(j=j, x=x.copy(), sets=sets, lsd=lsd, esr=esr, level=level, asym=asym)
            return j

        # Alternate: trims (Nelder-Mead), then the stomptones input level (1-D scan), twice.
        def simplex(x):
            # Steps: 15 % for log-scaled trims, 6 % of the range for linear ones.
            pts = [np.array(x, dtype=float)]
            for k, (n, s0, lo, hi, lg) in enumerate(PARAMS):
                p = np.array(x, dtype=float)
                p[k] += np.log(1.15) if lg else 0.06 * (hi - lo)
                pts.append(p)
            return np.array(pts)

        has_st = any(c["capture"] == "stomptones-rat2" for c in known)
        for rnd in range(2):
            res = minimize(f, best.get("x", x0), method="Nelder-Mead",
                           options={"maxiter": a.maxiter // 2, "xatol": 1e-3, "fatol": 1e-3,
                                    "initial_simplex": simplex(best.get("x", x0))})
            sets = best["sets"]
            if not has_st:
                continue
            scan = []
            for L in np.arange(-6, 18.1, 2.0):
                rows = compare.evaluate([c for c in known if c["capture"] == "stomptones-rat2"], sigs, "datasheet",
                                        sets, levels={"stomptones-rat2": float(L)}, pool=pool, opamp=a.opamp,
                                        parts=a.parts)
                scan.append((objective_rows(rows)[0], float(L)))
            st_level[0] = min(scan)[1]
            print(f"round {rnd}: stomptones level {st_level[0]:+.1f} dBu", flush=True)
            best["j"] = 1e9  # re-evaluate with the new level
            f(best["x"])

        sets = best["sets"]
        ok, report = stability.check("datasheet", sets, a.opamp, a.parts)
        print("\n".join(report))
        if not ok:
            print("BEST CANDIDATE IS UNSTABLE: not writing a calibration")
            return 1

    result = {"opamp": a.opamp, "parts": a.parts, "captures": fitcaps, "sets": sets,
              "stage1": {k: best.get(k) for k in ("j", "lsd", "esr", "level", "asym")}, "stlevel": st_level[0]}
    if a.no_stage2:
        (OUT / f"{a.tag or 'result'}.json").write_text(json.dumps(result, indent=1, default=float))
        print("\nCalibration capturedCalibration():")
        for k, v in sets:
            print(f"\t{k} = {v:.6g};")
        return 0

    # Stage 2: romanrecords, positions fitted per index with the trims held.
    idx = {"dist": list(compare.DEFAULT_INDEX_POS), "filter": list(compare.DEFAULT_INDEX_POS)}

    def stage2_rows(idx):
        pos = {("dist", "romanrecords-rat-90s"): idx["dist"], ("filter", "romanrecords-rat-90s"): idx["filter"]}
        return compare.evaluate(roman, sigs[:3], "datasheet", sets, index_pos=pos, pool=pool, opamp=a.opamp, parts=a.parts)

    for sweep in range(2):
        for knob in ("dist", "filter"):
            for i in range(7):
                trial = []
                cur = idx[knob][i]
                for d in (-0.12, -0.06, -0.03, 0.0, 0.03, 0.06, 0.12):
                    v = float(np.clip(cur + d, 0, 1))
                    cand = {k: list(v2) for k, v2 in idx.items()}
                    cand[knob][i] = v
                    rows = [r for r in stage2_rows(cand)
                            if (r["dist"] == cand["dist"][i] if knob == "dist" else r["filter"] == cand["filter"][i])]
                    trial.append((compare.summary(rows, "lsd_db"), v))
                idx[knob][i] = min(trial)[1]
            print(f"stage 2 sweep {sweep} {knob}: " + " ".join(f"{v:.2f}" for v in idx[knob]), flush=True)
    rows = stage2_rows(idx)
    lsd2, esr2 = compare.summary(rows, "lsd_db"), compare.summary(rows, "esr_db")
    print(f"romanrecords (held out): LSD {lsd2:.2f} dB, ESR {esr2:.1f} dB with fitted positions "
          f"dist {idx['dist']} filter {idx['filter']}")

    result["stage2"] = {"lsd": lsd2, "esr": esr2, "positions": idx}
    (OUT / f"{a.tag or 'result'}.json").write_text(json.dumps(result, indent=1, default=float))
    print("\nCalibration capturedCalibration():")
    for k, v in sets:
        print(f"\t{k} = {v:.6g};")
    return 0


if __name__ == "__main__":
    sys.exit(main())
