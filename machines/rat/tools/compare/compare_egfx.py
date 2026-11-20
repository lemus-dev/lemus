#!/usr/bin/env python3
"""Compare our model with EGFxSet's recordings of a real ProCo RAT2 (private/reference/egfx/,
from fetch_egfx.py).

Each pair is a clean Stratocaster note (Clean/<pickup>/<string>-<fret>.wav, 5 s) and the same
note re-amped through the dataset's RAT2 (a 2010 China-made unit, OP07 era) at DISTORTION 1.0,
FILTER 0.5, VOLUME 1.0. Both sets were normalized, so:
  - the input level (dBu at 0 dBFS of the clean file) is unknown. If the clean files were
    normalized before re-amping, it is one level for the whole set: --scan finds it (as
    compare_aalto.py --scan does). If they were normalized after, every file has its own level:
    --scan-each finds the best level per file and reports the spread, which tells the two apart;
  - the output level is matched per file (the gain reported is not meaningful).
The re-amped files are not sample-aligned with the clean ones: ours is first lined up with the
recording by cross-correlation over +-0.5 s (which also gives the chain's polarity), then
compare.py's metrics refine the alignment, match gain and give ESR, LSD and centroid.
The asymmetry is the positive over negative peak (99.9th percentiles) of the recording, in the
pedal's polarity (the chain's inversion, if any, undone; the RAT2 itself does not invert), and
of ours.

Usage: compare_egfx.py [--level DBU] [--scan | --scan-each] [--cal captured] [--opamp NAME] [--parts rat|rat2|china]
                       [--set NAME=VALUE ...] [--pickup NAME ...] [--every K] [--limit N]
                       [--json OUT]
  --every K / --limit N take every K-th pair / the first N (690 pairs; a scan over all of
  them renders each file about 22 times, so scan on --every 5 or so).
"""
import argparse
import concurrent.futures as cf
import json
import pathlib
import sys
import tempfile

import numpy as np
import soundfile as sf

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import compare  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
EGFX = ROOT / "private/reference/egfx"
COARSE_S = 0.5


def peak_ratio(x):
    """Positive over negative peak (99.9th percentiles), as compare.py's."""
    return float(np.quantile(x, 0.999) / (-np.quantile(x, 0.001) + 1e-12))


def mono(x):
    return x if x.ndim == 1 else x.mean(axis=1)


def coarse_align(ref, ours, max_lag):
    """Shift ours by whole samples to line up with ref (|lag| <= max_lag), by the largest
    |cross-correlation|. Returns ref and ours trimmed to one length, the lag (positive: the
    recording is late) and the sign of the correlation (the chain's polarity)."""
    n = len(ref) + len(ours) - 1
    nfft = 1 << int(np.ceil(np.log2(n)))
    c = np.fft.irfft(np.fft.rfft(ref, nfft) * np.conj(np.fft.rfft(ours, nfft)), nfft)
    lags = np.arange(-max_lag, max_lag + 1)
    vals = c[lags % nfft]
    i = int(np.argmax(np.abs(vals)))
    k, sign = int(lags[i]), float(np.sign(vals[i]) or 1.0)
    if k > 0:
        ours = np.concatenate([np.zeros(k), ours])
    elif k < 0:
        ours = ours[-k:]
    m = min(len(ref), len(ours))
    return ref[:m], ours[:m], k, sign


def render(dry, rate, level, knobs, cal, sets, opamp):
    with tempfile.TemporaryDirectory() as td:
        inp, out = pathlib.Path(td) / "in.wav", pathlib.Path(td) / "out.wav"
        sf.write(inp, dry.astype(np.float32), int(rate), subtype="FLOAT")
        op, parts = opamp  # (op amp, part values), either None
        compare.render_ours(inp, out, knobs["dist"], knobs["filter"], knobs["volume"], level, cal, sets, op, parts)
        ours, _ = sf.read(out)
    return mono(ours)


def job(args):
    p, level, knobs, cal, sets, opamp = args
    dry, rate = sf.read(ROOT / p["dry"])
    wet, rate_w = sf.read(ROOT / p["wet"])
    dry, wet = mono(dry), mono(wet)
    if rate_w != rate:
        raise RuntimeError(f"{p['wet']}: {rate_w} Hz, dry {rate} Hz")
    ours = render(dry, rate, level, knobs, cal, sets, opamp)
    ref, ours, lag, sign = coarse_align(wet, ours, int(COARSE_S * rate))
    m = compare.metrics(ref, ours, rate)
    m.update(pickup=p["pickup"], string=p["string"], fret=p["fret"], file=p["wet"], level=level,
             coarse_lag=lag, polarity=sign, asym_ref=peak_ratio(sign * ref), asym_ours=peak_ratio(ours),
             dry_peak=float(np.max(np.abs(dry))), wet_peak=float(np.max(np.abs(wet))))
    return m


def evaluate(pairs, level, knobs, cal, sets, opamp, pool):
    jobs = [(p, level, knobs, cal, tuple(sets), opamp) for p in pairs]
    return list(pool.map(job, jobs))


def scan_levels(pairs, knobs, cal, sets, opamp, pool, key="lsd_db"):
    """Coarse then fine scan of the input level; returns the best level and the coarse curve."""
    curve = []
    for L in np.arange(-8, 20.1, 2.0):
        rows = evaluate(pairs, float(L), knobs, cal, sets, opamp, pool)
        curve.append((compare.summary(rows, key), float(L)))
        print(f"level {L:+5.1f} dBu: LSD {curve[-1][0]:.2f} dB  ESR {compare.summary(rows, 'esr_db'):6.1f} dB",
              flush=True)
    best = min(curve)[1]
    fine = []
    for L in np.arange(best - 1.5, best + 1.51, 0.5):
        rows = evaluate(pairs, float(L), knobs, cal, sets, opamp, pool)
        fine.append((compare.summary(rows, key), float(L)))
    return min(fine)[1], curve


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--level", type=float, default=4.0)
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("--scan-each", action="store_true")
    ap.add_argument("--cal", default="captured")
    ap.add_argument("--opamp")
    ap.add_argument("--parts", choices=["rat", "rat2", "china"])
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--pickup", action="append", default=[])
    ap.add_argument("--every", type=int, default=1)
    ap.add_argument("--limit", type=int)
    ap.add_argument("--json")
    a = ap.parse_args()
    sets = [(kv.split("=")[0], float(kv.split("=")[1])) for kv in a.set]
    idx = json.loads((EGFX / "index.json").read_text())
    knobs = idx["knobs"]
    pairs = [p for p in idx["pairs"] if not a.pickup or p["pickup"] in a.pickup][:: a.every]
    if a.limit:
        pairs = pairs[: a.limit]
    print(f"{len(pairs)} pairs, knobs D {knobs['dist']} F {knobs['filter']} V {knobs['volume']}, cal {a.cal}"
          + (f", op amp {a.opamp}" if a.opamp else "") + (f", {a.parts} parts" if a.parts else ""))
    with cf.ProcessPoolExecutor() as pool:
        level = a.level
        if a.scan:
            level, _ = scan_levels(pairs, knobs, a.cal, sets, (a.opamp, a.parts), pool)
            print(f"best level {level:+.1f} dBu")
        if a.scan_each:
            # Every file on its own: the spread says whether one level fits the set.
            grid = np.arange(-8, 20.1, 2.0)
            per = {L: evaluate(pairs, float(L), knobs, a.cal, sets, (a.opamp, a.parts), pool) for L in grid}
            rows = []
            for i in range(len(pairs)):
                best = min(grid, key=lambda L: per[L][i]["lsd_db"] if np.isfinite(per[L][i]["lsd_db"]) else 1e9)
                rows.append(per[best][i])
            lv = np.array([r["level"] for r in rows])
            print(f"best level per file: median {np.median(lv):+.1f} dBu, IQR {np.percentile(lv, 25):+.1f} … "
                  f"{np.percentile(lv, 75):+.1f}, range {lv.min():+.1f} … {lv.max():+.1f}")
            pk = np.array([r["dry_peak"] for r in rows])
            print(f"clean-file peaks: {pk.min():.3f} … {pk.max():.3f} (equal peaks: normalized per file)")
        else:
            rows = evaluate(pairs, level, knobs, a.cal, sets, (a.opamp, a.parts), pool)
    s = compare.summary
    print(f"{len(rows)} pairs at {'per-file levels' if a.scan_each else f'{level:+.1f} dBu'}: "
          f"ESR {s(rows, 'esr_db'):.1f} dB, LSD {s(rows, 'lsd_db'):.2f} dB, "
          f"|centroid| {np.mean([abs(r['centroid_pct']) for r in rows]):.1f} %")
    ar = np.array([r["asym_ref"] for r in rows])
    ao = np.array([r["asym_ours"] for r in rows])
    pol = np.array([r["polarity"] for r in rows])
    print(f"pos/neg peak ratio: recording median {np.median(ar):.3f} (IQR {np.percentile(ar, 25):.3f}-"
          f"{np.percentile(ar, 75):.3f}), ours {np.median(ao):.3f} (IQR {np.percentile(ao, 25):.3f}-"
          f"{np.percentile(ao, 75):.3f})")
    print(f"chain polarity: {int((pol > 0).sum())} non-inverted, {int((pol < 0).sum())} inverted; "
          f"coarse lag median {np.median([r['coarse_lag'] for r in rows]):.0f} samples")
    by = {}
    for r in rows:
        by.setdefault(r["pickup"], []).append(r)
    for pu, rr in sorted(by.items()):
        print(f"  {pu:14s} n={len(rr):3d}  ESR {s(rr, 'esr_db'):6.1f} dB  LSD {s(rr, 'lsd_db'):5.2f} dB  "
              f"centroid {np.mean([r['centroid_pct'] for r in rr]):+6.1f} %  "
              f"asym {np.median([r['asym_ref'] for r in rr]):.3f} / {np.median([r['asym_ours'] for r in rr]):.3f}")
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(rows, indent=1, default=float))


if __name__ == "__main__":
    main()
