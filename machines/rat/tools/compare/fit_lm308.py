#!/usr/bin/env python3
"""Fit the LM308's trims of the Captured calibration to LM308 pedals.

The reference packs that fit_nam.py uses clip like OP07 units (positive peaks 8-12 % larger:
the OP07's hard negative limit; an LM308's swing is symmetric within 0.13 V), so the shared
trims (pots, diodes, JFET) and the OP07's come from there, and this fits only what is the
LM308's own: slew rate and its asymmetry, bandwidth, swing (half-range and shift about the
bias), output resistance and knee. The targets:
  - the published scope and FFT measurements of a ~1990 RAT2 with a Motorola LM308N
    (compare_scope.py): duty, positive over negative peak and H2/H4 at DISTORTION 5-10, where
    they come from the op amp rather than the pot law;
  - the NAM captures of LM308 pedals with stated knobs (pong's 1996 USA RAT2, rbrt's ~1994
    RAT), by log-spectral distance, their unknown input levels scanned first.
The shared trims are held at --base (a fit_nam.py result.json).

Usage: fit_lm308.py --base build/fit/v2/op07.json [--maxiter N]   (repo venv, ../../.venv)
"""
import argparse
import concurrent.futures as cf
import json
import pathlib
import sys

import numpy as np
from scipy.optimize import minimize

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import compare  # noqa: E402
import compare_scope  # noqa: E402
import fit_nam  # noqa: E402
import stability  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "build/fit"
CAPTURES = ["pong-rat2-1996-usa", "rbrt-rat-1990s"]
SIGNALS = ["pluck-mf", "twotone", "tb303"]
PARAMS = fit_nam.PARAMS_LM308 + [("outknee", 0.1, 0.02, 0.4, True)]


def scope_error(rows):
    """Duty, peak ratio and even harmonics at DISTORTION 5 and up."""
    hi = [r for r in rows if r["dist"] >= 0.5]
    duty = np.mean([abs(r["ours_duty"] - r["duty"]) for r in hi])
    ratio = np.mean([abs(np.log(r["ours_ratio"] / r["ratio"])) for r in hi])
    harm = np.mean([abs(r["ours_harm"][h] - r["harm"][h]) for r in hi for h in ("H2", "H4")])
    return 100 * duty + 10 * ratio + 0.1 * harm, duty, ratio, harm


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--maxiter", type=int, default=120)
    a = ap.parse_args()
    base = [(k, v) for k, v in json.loads(pathlib.Path(a.base).read_text())["sets"]
            if not k.startswith("op07")]
    suite = compare.load_suite()
    caps = [c for c in suite["captures"] if c["capture"] in CAPTURES]
    sigs = [s for s in suite["signals"] if s["name"] in SIGNALS]
    pool = cf.ProcessPoolExecutor()
    names = [p[0] for p in PARAMS]

    def sets_for(vals):
        return [kv for kv in base if kv[0] not in ("outlo", "outhi")] + fit_nam.calibration_sets(names, vals)

    def from_x(x):
        return [float(np.clip(np.exp(xi) if lg else xi, lo, hi)) for (n, s, lo, hi, lg), xi in zip(PARAMS, x)]

    x0 = np.array([np.log(s) if lg else s for (n, s, lo, hi, lg) in PARAMS])

    # The captures' input levels: scanned once at the start values.
    levels = {}
    for cap in CAPTURES:
        cc = [c for c in caps if c["capture"] == cap]
        scan = []
        for L in np.arange(-8, 16.1, 2.0):
            rows = compare.evaluate(cc, sigs, "datasheet", sets_for(from_x(x0)), levels={cap: float(L)}, pool=pool,
                                    opamp="lm308")
            scan.append((compare.summary(rows, "lsd_db"), float(L)))
        levels[cap] = min(scan)[1]
        print(f"{cap}: input level {levels[cap]:+.0f} dBu (LSD {min(scan)[0]:.2f})", flush=True)

    log = open(OUT / "fit_lm308.log", "a")
    best = {"j": 1e9}

    def f(x):
        vals = from_x(x)
        sets = sets_for(vals)
        rows = compare.evaluate(caps, sigs, "datasheet", sets, levels=levels, pool=pool, opamp="lm308")
        lsd = compare.summary(rows, "lsd_db")
        srows = list(pool.map(compare_scope.render_job, [(s, "datasheet", "lm308", tuple(sets))
                                                         for s in compare_scope.shots()]))
        sj, duty, ratio, harm = scope_error(srows)
        j = lsd + sj
        msg = (f"j={j:.4f} lsd={lsd:.3f} scope={sj:.3f} duty={duty:.4f} ratio={ratio:.3f} harm={harm:.1f} "
               + " ".join(f"{n}={v:.4g}" for n, v in zip(names, vals)))
        print(msg, flush=True)
        log.write(msg + "\n")
        log.flush()
        if j < best["j"]:
            best.update(j=j, sets=sets, vals=vals, lsd=lsd, scope=sj, duty=duty, ratio=ratio, harm=harm)
        return j

    pts = [x0]
    for k, (n, s0, lo, hi, lg) in enumerate(PARAMS):
        p = x0.copy()
        p[k] += np.log(1.15) if lg else 0.08 * (hi - lo)
        pts.append(p)
    minimize(f, x0, method="Nelder-Mead",
             options={"maxiter": a.maxiter, "xatol": 1e-3, "fatol": 1e-3, "initial_simplex": np.array(pts)})
    ok, report = stability.check("datasheet", best["sets"], "lm308")
    print("\n".join(report))
    result = {"base": a.base, "levels": levels, "sets": best["sets"],
              "lm308": dict(zip(names, best["vals"])), "stable": ok,
              **{k: best[k] for k in ("j", "lsd", "scope", "duty", "ratio", "harm")}}
    (OUT / "result_lm308.json").write_text(json.dumps(result, indent=1, default=float))
    print("\nLM308 trims:")
    for k, v in fit_nam.calibration_sets(names, best["vals"]):
        print(f"\t{k} = {v:.6g};")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
