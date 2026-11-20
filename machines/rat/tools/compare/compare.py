#!/usr/bin/env python3
"""Compare our model against the NAM captures of real RATs.

For every capture in tools/compare/specs/suite.json and every suite signal, renders the
signal through ratrender at the capture's knob positions and input level, aligns it to the
NAM render (sub-sample), matches gain and reports:
  - ESR: error-to-signal ratio after gain matching, in dB (NAM's own training metric);
  - LSD: log-spectral distance, dB (STFT, 40 Hz - 16 kHz), as for the TB-303;
  - centroid: spectral centroid difference, %;
  - gain: the gain applied to ours, dB (meaningful where the capture's output level is known);
  - pos/neg: positive over negative peak of each (99.9th percentiles), the clipping asymmetry.

Usage: compare.py [--cal datasheet|captured] [--opamp lm308|op07] [--parts rat|rat2|china] [--set NAME=VALUE ...] [--capture SLUG ...]
                  [--signal NAME ...] [--json OUT.json] [--quiet]
Run with the repo venv (../../.venv). Library use: evaluate(...) is what fit_nam.py calls.
"""
import argparse
import concurrent.futures as cf
import json
import os
import pathlib
import subprocess
import tempfile

import numpy as np
import soundfile as sf
from scipy import signal as sps

ROOT = pathlib.Path(__file__).resolve().parents[2]
RATRENDER = pathlib.Path(os.environ.get("RATRENDER", ROOT.parents[1] / "build/rat/ratrender"))  # RATRENDER: another build
NAMR = ROOT / "build/nam_renders"

# Knob rotation for the 1..7 index grids: the positions stomptones lists for its full grid
# (min, 9:00, 11:00, 12:00, 1:00, 3:00, max). fit_nam.py can replace these.
DEFAULT_INDEX_POS = [0.0, 0.2, 0.4, 0.5, 0.6, 0.8, 1.0]
# Input level for captures that do not state one.
DEFAULT_LEVEL_DBU = 4.0


def knobs_for(spec, index_pos=None):
    pos = index_pos or {}
    d = spec.get("dist")
    f = spec.get("filter")
    if d is None and "dist_index" in spec:
        d = pos.get(("dist", spec["capture"]), DEFAULT_INDEX_POS)[spec["dist_index"] - 1]
    if f is None and "filter_index" in spec:
        f = pos.get(("filter", spec["capture"]), DEFAULT_INDEX_POS)[spec["filter_index"] - 1]
    v = spec.get("volume", 0.5)
    return d, f, v


def level_for(spec, levels=None):
    if levels and spec["capture"] in levels:
        return levels[spec["capture"]]
    return spec["level_dbu"] if spec.get("level_dbu") is not None else DEFAULT_LEVEL_DBU


def ref_path(spec, sig):
    return NAMR / spec["capture"] / f"{pathlib.Path(spec['model']).stem}__{sig}.wav"


def render_ours(inp, out, dist, filt, vol, level, cal="datasheet", sets=(), opamp=None, parts=None):
    cmd = [str(RATRENDER), str(inp), str(out), "--dist", f"{dist:.6f}", "--filter", f"{filt:.6f}",
           "--volume", f"{vol:.6f}", "--level", f"{level:.4f}", "--cal", cal]
    if opamp:
        cmd += ["--opamp", opamp]
    if parts:
        cmd += ["--parts", parts]
    for k, v in sets:
        cmd += ["--set", f"{k}={v:.10g}"]
    subprocess.run(cmd, check=True)


def align(ref, ours, max_lag=12):
    """Shift ours (sub-sample) to line up with ref. Returns the shifted copy and the lag.

    The window is small on purpose: the NAM renders lead ours by 3-4 samples on broadband
    signals (their latency compensation; ours has a few samples of filter group delay), and a
    wide search locks onto the wrong period of the steady sines and squares."""
    n = min(len(ref), len(ours))
    r, o = ref[:n], ours[:n]
    c = sps.correlate(r, o, mode="full", method="fft")
    mid = n - 1
    lo, hi = mid - max_lag, mid + max_lag + 1
    k = lo + int(np.argmax(c[lo:hi]))
    lag = float(k - mid)
    if lo < k < hi - 1:
        y0, y1, y2 = c[k - 1], c[k], c[k + 1]
        den = y0 - 2 * y1 + y2
        if den != 0:
            lag += 0.5 * (y0 - y2) / den
    # Fractional shift in the frequency domain (signals have silence-ish edges).
    m = 1 << int(np.ceil(np.log2(n + 2 * max_lag)))
    spec = np.fft.rfft(o, m)
    w = np.fft.rfftfreq(m) * 2 * np.pi
    shifted = np.fft.irfft(spec * np.exp(-1j * w * lag), m)[:n]
    return shifted, lag


def lsd_db(ref, ours, rate):
    f, _, R = sps.stft(ref, rate, nperseg=2048, noverlap=1536)
    _, _, O = sps.stft(ours, rate, nperseg=2048, noverlap=1536)
    band = (f >= 40) & (f <= 16000)
    pr = np.abs(R[band]) ** 2
    po = np.abs(O[band]) ** 2
    top = pr.max()
    floor = top * 1e-8
    frames = pr.sum(axis=0) > top * 1e-6 * band.sum()
    if not frames.any():
        return float("nan")
    dr = 10 * np.log10(np.maximum(pr[:, frames], floor))
    do = 10 * np.log10(np.maximum(po[:, frames], floor))
    return float(np.mean(np.sqrt(np.mean((dr - do) ** 2, axis=0))))


def centroid(x, rate):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / rate)
    return float((f * X).sum() / (X.sum() + 1e-30))


def peak_ratio(x):
    """Positive over negative peak (99.9th percentiles): above 1 when positive peaks are larger."""
    return float(np.quantile(x, 0.999) / (-np.quantile(x, 0.001) + 1e-12))


def metrics(ref, ours, rate):
    ours, lag = align(ref, ours)
    n = min(len(ref), len(ours))
    ref, ours = ref[:n], ours[:n]
    g = float(np.dot(ref, ours) / (np.dot(ours, ours) + 1e-30))
    err = ref - g * ours
    esr = float(np.dot(err, err) / (np.dot(ref, ref) + 1e-30))
    return {
        "esr_db": 10 * np.log10(esr + 1e-12),
        "lsd_db": lsd_db(ref, g * ours, rate),
        "centroid_pct": 100 * (centroid(g * ours, rate) / (centroid(ref, rate) + 1e-9) - 1),
        "gain_db": 20 * np.log10(abs(g) + 1e-12),
        "lag": lag,
        "asym_ref": peak_ratio(ref),
        "asym_ours": peak_ratio(np.sign(g) * ours),
    }


def _job(args):
    spec, sig, cal, sets, index_pos, levels, opamp, parts = args
    ref, rate = sf.read(ref_path(spec, sig["name"]))
    d, f, v = knobs_for(spec, index_pos)
    with tempfile.TemporaryDirectory() as td:
        out = pathlib.Path(td) / "ours.wav"
        render_ours(ROOT / sig["file"], out, d, f, v, level_for(spec, levels), cal, sets, opamp, parts)
        ours, _ = sf.read(out)
    m = metrics(ref, ours, rate)
    m.update(capture=spec["capture"], model=spec["name"], signal=sig["name"], dist=d, filter=f, volume=v)
    return m


def evaluate(captures, signals, cal="datasheet", sets=(), index_pos=None, levels=None, pool=None, opamp=None,
             parts=None):
    jobs = [(c, s, cal, tuple(sets), index_pos, levels, opamp, parts) for c in captures for s in signals]
    if pool is None:
        with cf.ProcessPoolExecutor() as ex:
            return list(ex.map(_job, jobs, chunksize=2))
    return list(pool.map(_job, jobs, chunksize=2))


def load_suite():
    return json.loads((ROOT / "tools/compare/specs/suite.json").read_text())


def summary(rows, key):
    vals = [r[key] for r in rows if np.isfinite(r[key])]
    return float(np.mean(vals)) if vals else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cal", default="datasheet")
    ap.add_argument("--opamp", choices=["lm308", "op07"])
    ap.add_argument("--parts", choices=["rat", "rat2", "china"])
    ap.add_argument("--level", type=float, help="input level in dBu for every capture (uncalibrated ones)")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--capture", action="append", default=[])
    ap.add_argument("--signal", action="append", default=[])
    ap.add_argument("--json")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    suite = load_suite()
    # By default the stock pedals whose knob positions are stated (or a 1..7 index); a capture
    # named with --capture is run whatever its flags.
    caps = [c for c in suite["captures"] if c["capture"] in a.capture] if a.capture else \
        [c for c in suite["captures"] if c.get("variant") is None and not set(c.get("exclude") or []) - {"index-knobs"}]
    if a.level is not None:
        for c in caps:
            c["level_dbu"] = a.level
    sigs = [s for s in suite["signals"] if not a.signal or s["name"] in a.signal]
    sets = [(kv.split("=")[0], float(kv.split("=")[1])) for kv in a.set]
    rows = evaluate(caps, sigs, a.cal, sets, opamp=a.opamp, parts=a.parts)
    if not a.quiet:
        print(f"{'capture':24s} {'model':42s} {'signal':11s} {'ESR dB':>7s} {'LSD dB':>7s} {'cent %':>7s} {'gain dB':>8s}")
        for r in rows:
            print(f"{r['capture'][:24]:24s} {r['model'][:42]:42s} {r['signal']:11s} {r['esr_db']:7.1f} "
                  f"{r['lsd_db']:7.2f} {r['centroid_pct']:7.1f} {r['gain_db']:8.2f}")
    print()
    by = {}
    for r in rows:
        by.setdefault(r["capture"], []).append(r)
    for cap, rr in by.items():
        print(f"{cap:28s} n={len(rr):4d}  ESR {summary(rr, 'esr_db'):6.1f} dB  LSD {summary(rr, 'lsd_db'):5.2f} dB  "
              f"centroid {np.mean([abs(r['centroid_pct']) for r in rr]):5.1f} %  "
              f"pos/neg ref {np.median([r['asym_ref'] for r in rr]):.3f} ours {np.median([r['asym_ours'] for r in rr]):.3f}")
    print(f"{'ALL':28s} n={len(rows):4d}  ESR {summary(rows, 'esr_db'):6.1f} dB  LSD {summary(rows, 'lsd_db'):5.2f} dB")
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(rows, indent=1))


if __name__ == "__main__":
    main()
