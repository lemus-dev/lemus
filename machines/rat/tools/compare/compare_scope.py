#!/usr/bin/env python3
"""Compare the model with the published scope and FFT measurements of a real ~1990 RAT2.

The pedal (Motorola LM308N) was measured with a 1 kHz sine at -20 dBu at several DISTORTION
settings (private/reference/measurements/measurements.json, "rat2_1990_jazzcaster",
digitised from jazzcaster.com's screenshots). This renders the same test through ratrender and
compares what does not depend on the unstated output level: the positive over negative peak,
the fraction of each period above the mean (duty) and the harmonics re the fundamental.

The settings are the author's 0-10 knob readings. The shots labelled FILTER 10 are filed as
"F10" (10 %), and their near-ideal square waves confirm it, so FILTER is read as 0.1 there.

Usage: compare_scope.py [--cal datasheet|captured] [--opamp lm308|op07] [--set NAME=VALUE ...]
"""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile

import numpy as np
import soundfile as sf

ROOT = pathlib.Path(__file__).resolve().parents[2]
RATRENDER = pathlib.Path(os.environ.get("RATRENDER", ROOT.parents[1] / "build/rat/ratrender"))  # RATRENDER: another build
MEAS = ROOT / "private/reference/measurements/measurements.json"
RATE = 192000  # a 1 kHz period is 192 samples; duty comes from interpolated crossings
F0 = 1000
HARM = ["H2", "H3", "H4", "H5"]


def shots():
    d = json.loads(MEAS.read_text())["rat2_1990_jazzcaster"]
    ffts = {(f["settings"]["D"], f["settings"]["F"]): f["re_fund_dB"] for f in d["ffts"]}
    out = []
    for s in d["scope_shots"]:
        st = s["settings"]
        if st.get("D") is None:
            continue  # the bypass reference
        fft = ffts.get((st["D"], st["F"]), {})
        out.append({
            "label": f"D{st['D']:g} F{st['F']:g} V{st['V']:g}",
            "dist": st["D"] / 10, "filter": 0.1 if st["F"] == 10 else st["F"] / 10, "volume": st["V"] / 10,
            "ratio": s["pos_neg_peak_ratio"], "duty": s["duty_above_mean"],
            "harm": {h: fft.get(h, s["waveform_harmonics_dB_re_fund"][h]) for h in HARM},
        })
    return out


def analyse(y):
    """Steady state: peak ratio, duty above the mean, harmonics re the fundamental (dB)."""
    n = RATE // F0 * 200
    y = y[-n:] - np.mean(y[-n:])
    spec = np.abs(np.fft.rfft(y)) / n
    k = n * F0 // RATE
    fund = spec[k]
    harm = {f"H{m}": 20 * np.log10(spec[m * k] / fund + 1e-12) for m in range(2, 6)}
    # Peaks as a scope trace shows them: the 99th percentiles, so a sample or two of edge
    # overshoot does not count.
    return float(np.quantile(y, 0.99) / -np.quantile(y, 0.01)), duty(y), harm


def duty(y):
    """Fraction of the time above zero, from linearly interpolated zero crossings."""
    i = np.nonzero(np.signbit(y[:-1]) != np.signbit(y[1:]))[0]
    t = i + y[i] / (y[i] - y[i + 1])
    up = y[i] < 0
    k0 = int(np.argmax(up))  # start at the first rising crossing
    t, up = t[k0:], up[k0:]
    starts, ends = t[up], t[~up]
    n = min(len(starts), len(ends))
    if n < 2:
        return float(np.mean(y > 0))
    return float(np.sum(ends[:n - 1] - starts[:n - 1]) / (starts[n - 1] - starts[0]))


def render(shot, cal, opamp, sets):
    t = np.arange(int(1.2 * RATE)) / RATE
    x = 0.1 * np.sin(2 * np.pi * F0 * t)  # -20 dBu at --level 0 (0 dBFS = 0 dBu peak)
    with tempfile.TemporaryDirectory() as td:
        inp, out = pathlib.Path(td) / "in.wav", pathlib.Path(td) / "out.wav"
        sf.write(inp, x.astype(np.float32), RATE, subtype="FLOAT")
        cmd = [str(RATRENDER), str(inp), str(out), "--dist", str(shot["dist"]), "--filter", str(shot["filter"]),
               "--volume", str(shot["volume"]), "--level", "0", "--cal", cal]
        if opamp:
            cmd += ["--opamp", opamp]
        for k, v in sets:
            cmd += ["--set", f"{k}={v:.10g}"]
        subprocess.run(cmd, check=True, capture_output=True)
        y, _ = sf.read(out)
    return analyse(y)


def render_job(args):
    """One shot rendered and analysed: (shot, cal, opamp, sets) -> row (for process pools)."""
    shot, cal, opamp, sets = args
    ratio, duty, harm = render(shot, cal, opamp, sets)
    return {**shot, "ours_ratio": ratio, "ours_duty": duty, "ours_harm": harm}


def evaluate(cal="datasheet", opamp=None, sets=()):
    return [render_job((s, cal, opamp, tuple(sets))) for s in shots()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cal", default="datasheet")
    ap.add_argument("--opamp", choices=["lm308", "op07"])
    ap.add_argument("--set", action="append", default=[])
    a = ap.parse_args()
    sets = [(kv.split("=")[0], float(kv.split("=")[1])) for kv in a.set]
    rows = evaluate(a.cal, a.opamp, sets)
    print(f"{'setting':14s} {'pos/neg real':>12s} {'ours':>6s} {'duty real':>9s} {'ours':>6s}   "
          + "  ".join(f"{h} real/ours" for h in HARM))
    for r in rows:
        print(f"{r['label']:14s} {r['ratio']:12.3f} {r['ours_ratio']:6.3f} {r['duty']:9.3f} {r['ours_duty']:6.3f}   "
              + "  ".join(f"{r['harm'][h]:6.1f}/{r['ours_harm'][h]:6.1f}" for h in HARM))
    dr = np.mean([abs(r["ours_ratio"] - r["ratio"]) for r in rows])
    dh = np.mean([abs(r["ours_harm"][h] - r["harm"][h]) for r in rows for h in HARM if r["harm"][h] > -50])
    print(f"\nmean |pos/neg error| {dr:.3f}; mean |harmonic error| {dh:.1f} dB (harmonics above -50 dB)")


if __name__ == "__main__":
    main()
