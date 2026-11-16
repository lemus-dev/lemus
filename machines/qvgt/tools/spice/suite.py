#!/usr/bin/env python3
"""The preamp's netlists against ngspice: each case through compare_circuit.py (the solver and
ngspice on the same deck), a table of the worst and RMS differences relative to each output's range.

    suite.py [--over 4] [--circuitrun PATH]

at the preamp's rate, --over times the DSP's (core/preamp.h, kPreampOver).

A case passes when its RMS difference is under 0.5 % of the range and its worst under 3 % (a
clipping stage's corners land a sample apart). Needs ngspice and numpy.
"""
import argparse
import math
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NETS = os.path.abspath(os.path.join(HERE, "../../core/netlists"))
DSP_RATE = 24e6 / 576


def sine(name, amp, freq, seconds, points=400):
    pts = []
    for i in range(points + 1):
        t = seconds * i / points
        pts += [f"{t:.7f}", f"{amp * math.sin(2 * math.pi * freq * t):.6g}"]
    return ["--input", f"{name}=" + ",".join(pts)]


def clipped(name, amp, clip, freq, seconds, points=800):
    pts = []
    for i in range(points + 1):
        t = seconds * i / points
        pts += [f"{t:.7f}", f"{max(-clip, min(clip, amp * math.sin(2 * math.pi * freq * t))):.6g}"]
    return ["--input", f"{name}=" + ",".join(pts)]


def ctl(**kw):
    out = []
    for k, v in kw.items():
        out += ["--control", f"{k}={v}"]
    return out


S = 0.02  # seconds per case
CASES = [
    ("input, pot full", "input", sine("left", 0.3, 220, S) + sine("right", 0.2, 330, S) + ctl(input=1)),
    ("input, pot half", "input", sine("left", 1.0, 220, S) + sine("right", 1.0, 1000, S) + ctl(input=0.5)),
    ("overdrive off", "overdrive", sine("vca", 150e-6, 220, S) + ctl(od=0)),
    ("overdrive on", "overdrive", sine("vca", 150e-6, 220, S) + ctl(od=1)),
    ("shaper clean", "shaper", sine("od", 2.0, 220, S) + ctl(dist=0, flat=1)),
    ("shaper clean, clipping in", "shaper", clipped("od", 13, 10.5, 220, S) + ctl(dist=0, flat=1)),
    ("shaper dist type 1", "shaper", sine("od", 2.0, 220, S) + ctl(dist=1, ch7=1)),
    ("shaper dist type 8", "shaper", sine("od", 1.0, 220, S) + ctl(dist=1, ch7=0, ch0=1)),
    ("shaper dist 8, Q1 on", "shaper", sine("od", 0.3, 220, S) + ctl(dist=1, ch7=0, ch0=1) + ["--input", "conout=0,-10"]),
    ("shaper presence", "shaper", sine("od", 1.0, 3000, S) + ctl(dist=0, flat=0, presence=1)),
    ("shaper bright", "shaper", sine("od", 1.0, 3000, S) + ctl(dist=0, flat=0, presence=0)),
    ("shaper cabinet 1", "shaper", sine("od", 1.0, 2000, S) + ctl(dist=0, cab=1, cab2=0)),
    ("shaper cabinet 2, bass", "shaper", sine("od", 1.0, 150, S) + ctl(dist=0, cab=1, cab2=1, bass=1)),
    ("gateout", "gateout", sine("vca", 200e-6, 440, S) + ctl(loop=0)),
    ("gateout, loop return", "gateout", sine("vca", 200e-6, 440, S) + sine("ret", 1.0, 220, S) + ctl(loop=1)),
]


# the circuits whose op-amps' poles the preamp integrates by the trapezoidal rule (core/preamp.cpp)
TRAP = {"shaper": ["--pole-trap"]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--over", type=int, default=4)
    ap.add_argument("--circuitrun")
    args = ap.parse_args()
    rate = DSP_RATE * args.over
    extra = ["--circuitrun", args.circuitrun] if args.circuitrun else []
    line = re.compile(r"^(\S+)\s+range\s+(\S+) V\s+max diff\s+(\S+) V \(\s*(\S+) %\)\s+rms\s+(\S+) V")
    failed = 0
    print(f"{'case':30s} {'output':8s} {'range V':>9s} {'max %':>8s} {'rms %':>8s}")
    for name, net, a in CASES:
        r = subprocess.run([sys.executable, os.path.join(HERE, "compare_circuit.py"), f"{NETS}/models.cir,{NETS}/{net}.cir",
                            "--rate", repr(rate), "--seconds", str(S), *a, *TRAP.get(net, []), *extra],
                           capture_output=True, text=True)
        rows = [m.groups() for m in map(line.match, r.stdout.splitlines()) if m]
        if not rows:
            print(f"{name:30s} FAILED TO RUN\n{r.stdout[-800:]}{r.stderr[-800:]}")
            failed += 1
            continue
        for out, rng, _, mx, rms in rows:
            rel = 100 * float(rms) / max(float(rng), 1e-12)
            ok = rel < 0.5 and float(mx) < 3
            failed += 0 if ok else 1
            print(f"{name:30s} {out:8s} {float(rng):9.4g} {float(mx):8.3f} {rel:8.4f}{'' if ok else '  FAIL'}")
            name = ""
    print("\nall passed" if not failed else f"\n{failed} FAILED")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
