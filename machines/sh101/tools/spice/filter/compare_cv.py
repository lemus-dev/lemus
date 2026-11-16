#!/usr/bin/env python3
"""Compare the cutoff CV summer and pin-9 divider (FilterSection) with SPICE (cv.cir).

Usage (from machines/sh101): ../../.venv/bin/python tools/spice/filter/compare_cv.py
Prints pin 9 against the bus for each case, model and SPICE, and the difference in cents of
cutoff at 19 mV/oct.
"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from compare_vcf import blocks_exe, NGSPICE  # noqa: E402

V15, VREF = 14.87, 5.0  # +15 V (IC1b): FREQ's top and R187; +14 V is in cv.cir
VR8 = 27.859e3  # the model's trim for the service-notes values (FilterSection, VR8 by the F4/F5 procedure)
TH1 = 1000
MV_PER_OCT = 19.0


def taper_a(x, mid=0.15):
    b = (1 / mid - 1) ** 2
    return (b ** x - 1) / (b - 1)


def halves(k, rpot=100e3):
    k = min(max(k, 0.0), 1.0)
    return max(rpot * (1 - k), 1e-3), max(rpot * k, 1e-3)


def spice(case, env_volts):
    freq, envpot, envpeak, mod, modv, kybd, kcv, bendpot, bend = case
    with open(os.path.join(HERE, "cv.cir")) as f:
        s = f.read()
    sub = {"V15": V15, "VENV": env_volts, "VMOD": modv, "VKCV": kcv, "RVR8": VR8, "TH1": TH1,
           "VBEND": 0.43 * bend}
    for name, k, ta in (("17", freq, False), ("19", envpot, False), ("20", mod, True),
                        ("21", kybd, False), ("5", bendpot, False)):
        t, b = halves(taper_a(k) if ta else k)
        sub["R%sT" % name], sub["R%sB" % name] = t, b
    for k, v in sub.items():
        s = s.replace("{%s}" % k, repr(float(v)))
    d = tempfile.mkdtemp(prefix="sh101cv-")
    p = os.path.join(d, "cv.cir")
    with open(p, "w") as f:
        f.write(s)
    out = subprocess.run([NGSPICE, "-b", p], cwd=HERE, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.strip().startswith("v(pin9)-v(bus)"):
            return float(line.split("=")[1])
    sys.exit(out[-2000:])


def model(case):
    out = subprocess.run([blocks_exe(), "cv"] + [str(x) for x in case], capture_output=True,
                         text=True, check=True).stdout.split()
    return float(out[0]), float(out[3])


CASES = [
    # FREQ ENVPOT ENVPEAK MOD MODV KYBD KCV BENDPOT BEND
    (0.0, 0, 0, 0, 0, 0, 0, 0, 0),
    (0.25, 0, 0, 0, 0, 0, 0, 0, 0),
    (0.5, 0, 0, 0, 0, 0, 0, 0, 0),
    (0.75, 0, 0, 0, 0, 0, 0, 0, 0),
    (1.0, 0, 0, 0, 0, 0, 0, 0, 0),
    (0.5, 0.5, 1, 0, 0, 0, 0, 0, 0),
    (0.5, 1.0, 1, 0, 0, 0, 0, 0, 0),
    (0.5, 0, 0, 1.0, 2.5, 0, 0, 0, 0),
    (0.5, 0, 0, 0.5, -2.5, 0, 0, 0, 0),
    (0.5, 0, 0, 0, 0, 1.0, 0.4167, 0, 0),
    (0.5, 0, 0, 0, 0, 1.0, 2.0, 0, 0),
    (0.5, 0, 0, 0, 0, 1.0, 5.0, 0, 0),
    (0.5, 0, 0, 0, 0, 0.5, 3.0, 0, 0),
    (0.5, 0, 0, 0, 0, 0, 0, 1.0, 1.0),
    (0.5, 0, 0, 0, 0, 0, 0, 0.5, -1.0),
    (1.0, 1.0, 1, 0, 0, 1.0, 5.0, 0, 0),
]


def main():
    print("Pin 9 against the bus (mV): model vs SPICE, difference in cents at %g mV/oct" % MV_PER_OCT)
    worst = 0
    for case in CASES:
        m, env = model(case)
        s = spice(case, env)
        c = (m - s) * 1e3 / MV_PER_OCT * 1200
        worst = max(worst, abs(c))
        print("  %-48s model %8.2f  SPICE %8.2f   %+6.1f cents" % (str(case), m * 1e3, s * 1e3, c))
    print("  worst %.1f cents" % worst)


if __name__ == "__main__":
    main()
