#!/usr/bin/env python3
"""Compare our model with the Aalto recordings of a real ProCo RAT (private/reference/aalto/).

Each clip is 1 s of real guitar (channel 1) and the pedal's recorded output (channel 2) at the
knob positions in its name (DISTORTION, FILTER, VOLUME, 0-100; the order follows from the levels:
the third number tracks the output level, r = 0.90). The interface's level calibration is not
published, so the input level (dBu at 0 dBFS) is one unknown for the whole set: --scan finds it.
The gain applied per clip is reported against VOLUME, which shows the volume taper directly.

Usage: compare_aalto.py [--level DBU] [--scan] [--cal captured] [--opamp lm308|op07] [--set NAME=VALUE ...]
                        [--json OUT]
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


def job(args):
    e, level, cal, sets, opamp = args
    x, rate = sf.read(ROOT / e["file"])
    dry, wet = x[:, 0], x[:, 1]
    d, f, v = (k / 100.0 for k in e["knobs"])
    with tempfile.TemporaryDirectory() as td:
        inp, out = pathlib.Path(td) / "in.wav", pathlib.Path(td) / "out.wav"
        sf.write(inp, dry.astype(np.float32), int(rate), subtype="FLOAT")
        compare.render_ours(inp, out, d, f, v, level, cal, sets, opamp)
        ours, _ = sf.read(out)
    m = compare.metrics(wet, ours, rate)
    m.update(file=e["file"], dist=d, filter=f, volume=v,
             wet_db=float(20 * np.log10(np.sqrt(np.mean(wet ** 2)) + 1e-12)))
    return m


def evaluate(level, cal="captured", sets=(), pool=None, min_wet_db=-60, opamp=None):
    idx = json.loads((ROOT / "private/reference/aalto/index.json").read_text())
    jobs = [(e, level, cal, tuple(sets), opamp) for e in idx]
    rows = list(pool.map(job, jobs)) if pool else list(cf.ProcessPoolExecutor().map(job, jobs))
    # Clips with VOLUME near zero are below the recording's noise: leave them out.
    return [r for r in rows if r["wet_db"] > min_wet_db]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--level", type=float, default=4.0)
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("--cal", default="captured")
    ap.add_argument("--opamp", choices=["lm308", "op07"])
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--json")
    a = ap.parse_args()
    sets = [(kv.split("=")[0], float(kv.split("=")[1])) for kv in a.set]
    with cf.ProcessPoolExecutor() as pool:
        level = a.level
        if a.scan:
            scan = []
            for L in np.arange(-8, 20.1, 2.0):
                rows = evaluate(float(L), a.cal, sets, pool, opamp=a.opamp)
                scan.append((compare.summary(rows, "lsd_db"), float(L)))
                print(f"level {L:+5.1f} dBu: LSD {scan[-1][0]:.2f} dB", flush=True)
            level = min(scan)[1]
            fine = []
            for L in np.arange(level - 1.5, level + 1.51, 0.5):
                rows = evaluate(float(L), a.cal, sets, pool, opamp=a.opamp)
                fine.append((compare.summary(rows, "lsd_db"), float(L)))
            level = min(fine)[1]
            print(f"best level {level:+.1f} dBu")
        rows = evaluate(level, a.cal, sets, pool, opamp=a.opamp)
    print(f"{len(rows)} clips at {level:+.1f} dBu: ESR {compare.summary(rows, 'esr_db'):.1f} dB, "
          f"LSD {compare.summary(rows, 'lsd_db'):.2f} dB, |centroid| {np.mean([abs(r['centroid_pct']) for r in rows]):.1f} %, pos/neg real "
          f"{np.median([r['asym_ref'] for r in rows]):.3f} ours {np.median([r['asym_ours'] for r in rows]):.3f}")
    # Volume taper check: the gain we needed against VOLUME (a constant offset is the unknown
    # output calibration).
    vol = np.array([r["volume"] for r in rows])
    g = np.array([r["gain_db"] for r in rows])
    order = np.argsort(vol)
    print("gain needed vs VOLUME (should be flat if the taper is right):")
    for lo in np.arange(0, 1.0, 0.2):
        sel = (vol >= lo) & (vol < lo + 0.2)
        if sel.any():
            print(f"  VOLUME {lo:.1f}-{lo + 0.2:.1f}: {g[sel].mean():+6.2f} dB (n={sel.sum()})")
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(rows, indent=1, default=float))


if __name__ == "__main__":
    main()
