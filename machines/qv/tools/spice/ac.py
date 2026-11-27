#!/usr/bin/env python3
"""Each linear analog stage of the model (core/analog.h, through qvanalog) against the same stage as
an ngspice deck from the service manual's sheet 1 (rev F), by AC analysis, 20 Hz to 20 kHz.

    ac.py [--qvanalog PATH] [--keep DIR]

The op-amps are ideal (a gain of 10^6): the TL084's 3 MHz leaves these stages' responses alone in
the audio band at their gains (at most 10). Prints, per case, the largest magnitude difference
(dB) and phase difference (degrees) in two bands, to 10 kHz and to 18 kHz, and fails a case past
0.05 dB / 1 degree to 10 kHz or 0.5 dB / 10 degrees to 18 kHz (the bilinear transform's warping
near the rate's Nyquist frequency).
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "../../../.."))

OPAMP = "E{n} {out} 0 {plus} {minus} 1e6"


def deck_input(t):
    # U1: the follower (ideal), C2 4.7u, R4 1K, the pot's track R_a to the wiper at the - input, R_b on
    ra, rb = max(10e3 * (1 - t), 1e-3), max(10e3 * t, 1e-3)
    return f"""
C2 in a 4.7u
R4 a b 1k
Ra b m {ra}
Rb m out {rb}
{OPAMP.format(n=1, out='out', plus=0, minus='m')}
"""


DECKS = {
    "preemphasis": lambda p: f"""
R23 out m 75k
R24 m n 20k
C43 n 0 330p
{OPAMP.format(n=1, out='out', plus='in', minus='m')}
""",
    "reconstruction": lambda p: f"""
R30 in a 1.1k
R29 a b 1.1k
C50 a out 0.01u
C49 b 0 3300p
{OPAMP.format(n=1, out='out', plus='b', minus='out')}
""",
    "summer": lambda p: f"""
R34 in m 20k
R7 m out 20k
C26 m out 150p
{OPAMP.format(n=1, out='out', plus=0, minus='m')}
""",
    "output": lambda p: f"""
C4 in p 4.7u
Rpot p 0 10k
R10 out m 2k
R11 m q 2k
C8 q 0 4.7u
{OPAMP.format(n=1, out='out', plus='p', minus='m')}
""",
    # the CEM3381 as a current mirror at gain G: R33's current into its virtual ground (Vs), copied
    # into U2's summer
    "dry": lambda g: f"""
C9 in a 4.7u
R33 a s 20k
Vs s 0 0
F1 0 m Vs {g}
R7 m out 20k
C26 m out 150p
{OPAMP.format(n=1, out='out', plus=0, minus='m')}
""",
    "input": deck_input,
}

CASES = [("input", 1.0), ("input", 0.55), ("input", 0.2), ("preemphasis", None), ("reconstruction", None),
         ("summer", None), ("output", None), ("dry", 1.0), ("dry", 0.25)]


def run_spice(block, param, work):
    body = DECKS[block](param)
    out = os.path.join(work, f"{block}-{param}.txt")
    deck = f"""* {block} {param}
Vin in 0 dc 0 ac 1
{body}
.control
ac dec 40 20 20000
wrdata {out} v(out)
.endc
.end
"""
    path = os.path.join(work, f"{block}-{param}.cir")
    with open(path, "w") as f:
        f.write(deck)
    r = subprocess.run(["ngspice", "-b", path], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        sys.exit(f"ngspice failed on {block}:\n{r.stdout}\n{r.stderr}")
    d = np.loadtxt(out)
    return d[:, 0], d[:, 1] + 1j * d[:, 2]


def run_model(qvanalog, block, param, freqs):
    args = [qvanalog, block] + ([str(param)] if param is not None else [])
    r = subprocess.run(args, input="\n".join(f"{f:.9g}" for f in freqs), capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"qvanalog failed on {block}: {r.stderr}")
    d = np.loadtxt(r.stdout.splitlines())
    return d[:, 1] + 1j * d[:, 2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qvanalog", default=os.path.join(REPO, "../build/qv/qvanalog"))
    ap.add_argument("--keep")
    args = ap.parse_args()
    work = args.keep or tempfile.mkdtemp(prefix="qvac")
    os.makedirs(work, exist_ok=True)
    failed = 0
    print(f"{'case':<22} {'to 10 kHz':>18} {'to 18 kHz':>18}")
    for block, param in CASES:
        f, spice = run_spice(block, param, work)
        ours = run_model(args.qvanalog, block, param, f)
        db = 20 * np.log10(np.abs(ours) / np.abs(spice))
        ph = np.degrees(np.angle(ours / spice))
        lo, hi = f <= 10e3, f <= 18e3
        m1, p1 = np.max(np.abs(db[lo])), np.max(np.abs(ph[lo]))
        m2, p2 = np.max(np.abs(db[hi])), np.max(np.abs(ph[hi]))
        ok = m1 <= 0.05 and p1 <= 1 and m2 <= 0.5 and p2 <= 10
        failed += not ok
        name = block + ("" if param is None else f" {param:g}")
        print(f"{name:<22} {m1:7.3f} dB {p1:5.2f} deg {m2:7.3f} dB {p2:5.2f} deg  {'ok' if ok else 'FAIL'}")
    print("\nall passed" if not failed else f"\n{failed} FAILED")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
