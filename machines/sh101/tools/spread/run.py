#!/usr/bin/env python3
"""The SH-101 spread study (docs/spread.md): draws units from the parts' tolerances (parts.py),
computes what each does with its trims redone (measures.py), and prints the nominal unit, the
median and the 5-95 % range of every measure, and where each real-unit observation falls.

Usage (from machines/sh101): ../../.venv/bin/python tools/spread/run.py [--n 20000] [--seed 101]
Writes private/sh101/reference/spread/spread.json (every draw's measures) and prints markdown.
"""
import argparse, json, os, sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import parts  # noqa: E402
import measures  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "private", "reference", "spread")

# Observations of real units, Roland's plug-in and the spec (docs/model.md, roland-measurements.md):
# (measure, value, source).
OBS = [
    ("ENV depth at ENV 10 (oct)", 6.6, "the clip as real.py measured it (51 Hz -> 4.86 kHz; tracker window 30 Hz-5 kHz)"),
    ("ENV depth at ENV 10 (oct)", 7.24, "the clip re-measured (clip_sweep.py: plateau 4.84 kHz over the release's 31.9 Hz rest)"),
    ("ENV depth at ENV 10 (oct)", 7.65, "Roland's plug-in"),
    ("Clip settings: start, self-osc (Hz)", 31.9, "the clip's rest, re-measured (FREQ 0, KYBD 10, C4; depends on I0)"),
    ("Clip settings: end, self-osc (kHz)", 4.84, "the clip's plateau (FREQ 0, KYBD 10, C4, ENV 10, S 10)"),
    ("ENV peak re bus (V)", 4.82, "the clip's attack curvature (k = 2.05; weakly determined, 1.8-2.35 fit within 10 cents)"),
    ("Release tau at 10 (s)", 4.15, "the clip's release (if R is at 10)"),
    ("Attack at 10 (s)", 3.24, "the clip's attack (if A is at 10)"),
    ("Attack at 10 (s)", 3.0, "Reid, Sound On Sound 2018 (one unit)"),
    ("Attack at 10 (s)", 4.0, "the spec (1.5 ms-4 s)"),
    ("Portamento tau at 10 (s)", 0.78, "a real unit (clip, glide at full)"),
    ("FREQ 0, KYBD 0: self-osc (Hz)", 10.0, "the spec's 10 Hz bottom"),
    ("FREQ 10, KYBD 0: self-osc (kHz)", 20.0, "the spec's 20 kHz top"),
    ("FREQ 0, KYBD 0: self-osc (Hz)", 3.4, "Roland's plug-in, FREQ 0"),
    ("FREQ 10, KYBD 0: self-osc (kHz)", 15.6, "Roland's plug-in, FREQ 10"),
    ("FREQ span 0-10 (oct)", 12.2, "Roland's plug-in"),
    ("FREQ span 0-10 (oct)", np.log2(20e3 / 10), "the spec (10 Hz-20 kHz)"),
    ("FREQ top/mid division", 1.0, "Roland's plug-in (a straight exponential law)"),
    ("LFO rate at 0 (Hz)", 0.1, "the spec's 0.1 Hz"),
    ("LFO rate at 0 (Hz)", 0.088, "Roland's plug-in"),
    ("LFO rate at 5 (Hz)", 5.89, "Roland's plug-in"),
    ("LFO rate at 10 (Hz)", 30.0, "the spec's 30 Hz"),
    ("LFO rate at 10 (Hz)", 39.1, "Roland's plug-in"),
    ("LFO rate at 10 (Hz)", 38.2, "a real unit (Perfect Circuit's RATE sweep)"),
    ("TUNE range (+- cents)", 50.0, "the spec (+-50 cent)"),
    ("VCO MOD at 10 (+- cents)", 2600.0, "Roland's plug-in (+-26 semitones)"),
    ("PW at 10, MAN (% high)", 3.9, "Roland's plug-in"),
    ("PW at 10, MAN (% high)", 0.0, "the spec (50 % ~ 0 %)"),
    ("Pulse re saw (dB)", 1.9, "Roland's plug-in"),
    ("Sub re saw (dB)", -1.6, "Roland's plug-in"),
    ("Self-osc onset (RES travel)", 0.82, "Roland's plug-in (about 8.2)"),
]


def pct(x, v):
    """Where v falls in the population x, as a percentile (0-100)."""
    return 100.0 * np.mean(x <= v)


def fmt(v):
    a = abs(v)
    if a >= 100:
        return f"{v:.0f}"
    if a >= 10:
        return f"{v:.1f}"
    if a >= 1:
        return f"{v:.2f}"
    return f"{v:.3f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=20000)
    ap.add_argument("--seed", type=int, default=101)
    ap.add_argument("--slope", help="LO,HI: the expo slope's range at 25 C, mV/oct (default 17.5,20.8)")
    ap.add_argument("--i0", help="LO,HI: I0's range, uA, log-uniform (default 1.2,4.8)")
    ap.add_argument("--swing", type=float, default=1.0, help="scale the ENV's peak (a scenario for R158/VR24)")
    ap.add_argument("--json", default="spread.json", help="file name under private/sh101/reference/spread")
    a = ap.parse_args()
    u = parts.draw(a.n, a.seed)
    rng = np.random.default_rng(a.seed + 1)
    if a.slope:
        lo, hi = map(float, a.slope.split(","))
        u["slope25"] = rng.uniform(lo, hi, a.n) * 1e-3
    if a.i0:
        lo, hi = map(float, a.i0.split(","))
        u["I0"] = np.exp(rng.uniform(np.log(lo), np.log(hi), a.n)) * 1e-6
    if a.swing != 1.0:
        u["swingScale"] = np.full(a.n, a.swing)
    m = measures.measures(u)
    nom = measures.measures(parts.draw(1, nominal=True))
    os.makedirs(OUT, exist_ok=True)
    json.dump({k: v.tolist() for k, v in m.items()}, open(os.path.join(OUT, a.json), "w"))
    print(f"{a.n} units, seed {a.seed}" + (f", slope {a.slope} mV/oct" if a.slope else "") +
          (f", I0 {a.i0} uA" if a.i0 else "") + (f", ENV peak x{a.swing}" if a.swing != 1.0 else "") + "\n")
    print("| Measure | Nominal | Median | 5-95 % |")
    print("|---|---|---|---|")
    for k, v in m.items():
        lo, med, hi = np.percentile(v, [5, 50, 95])
        print(f"| {k} | {fmt(nom[k][0])} | {fmt(med)} | {fmt(lo)} .. {fmt(hi)} |")
    print("\n| Observation | Value | Percentile in the spread | Inside 5-95 %? |")
    print("|---|---|---|---|")
    for k, v, src in OBS:
        p = pct(m[k], v)
        print(f"| {src}: {k} | {fmt(v)} | {p:.1f} | {'yes' if 5 <= p <= 95 else 'no'} |")


if __name__ == "__main__":
    main()
