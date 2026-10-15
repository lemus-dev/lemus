#!/usr/bin/env python3
"""The pitch DAC and the slide against a SPICE simulation of the schematic (tools/spice/slide.cir).

Usage (from the machine folder): ../../.venv/bin/python tools/spice/compare_slide.py

Prints the ladder's CV for every code against the model's (code + 1)/12 V (core/voice.cpp), and the
slide's time constant against the model's 22 ms."""
import os, subprocess, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
VREF = 5.333


def run(body, analysis, outputs, tmp):
    deck, out = os.path.join(tmp, "d.cir"), os.path.join(tmp, "o.txt")
    open(deck, "w").write(f"* slide\n.include {os.path.join(HERE, 'slide.cir')}\n{body}\n.control\nset wr_singlescale\n"
                          f"option numdgt=10\n{analysis}\nwrdata {out} {outputs}\nquit\n.endc\n.end\n")
    subprocess.run(["ngspice", "-b", deck], capture_output=True)
    return np.atleast_2d(np.loadtxt(out))


def bits(code, t=None):
    return "\n".join(f"Vb{i} b{i} 0 {VREF if code >> i & 1 else 0}" for i in range(6))


with tempfile.TemporaryDirectory() as tmp:
    worst = 0
    for code in range(64):
        d = run(bits(code) + "\nVsl nsl 0 0", "op", "v(tp4)", tmp)
        v = d[0, 1]
        worst = max(worst, abs(v - (code + 1) / 12 * (VREF * 12 / 64)))
    print(f"DAC: largest difference from V_ref (code + 1)/64 over all 64 codes: {worst * 1e6:.2f} uV "
          f"(1 semitone = {VREF / 64 * 1e3:.2f} mV)")
    # Slide: code 23 (C) to 35 (C') at 10 ms with SLIDE high from then on.
    src = []
    for i in range(6):
        a, b = 23 >> i & 1, 35 >> i & 1
        src.append(f"Vb{i} b{i} 0 PWL(0 {VREF * a} 0.01 {VREF * a} 0.01001 {VREF * b})")
    d = run("\n".join(src) + "\nVsl nsl 0 PWL(0 0 0.00999 0 0.01 11.85)", "tran 20u 0.2 0 20u", "v(tp4) v(nc35)", tmp)
    t, v = d[:, 0], d[:, 1]
    v0, v1 = 24 / 12 * VREF * 12 / 64, 36 / 12 * VREF * 12 / 64
    k = (t > 0.0101) & (t < 0.1)
    tau = -np.polyfit(t[k] - 0.01, np.log(np.abs(v1 - v[k])), 1)[0] ** -1
    print(f"Slide C to C': SPICE time constant {tau * 1e3:.2f} ms (model 22.00 ms); 10-90 % {2.197 * tau * 1e3:.1f} ms")
