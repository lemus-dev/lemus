#!/usr/bin/env python3
"""Compare the VCA and output stage (Ba662Vca) with SPICE (vca.cir, the BA662 at transistor
level from ba662.lib).

Usage (from machines/sh101): ../../.venv/bin/python tools/spice/filter/compare_vca.py [--only ctl,gain,thd,out]
ctl:  TR28's collector current (the BA662's I_ABC) against the ENV voltage, ENV mode.
gain: small-signal gain pin 3 -> pin 8 at several ENV voltages; pin 3 open-circuit (the R130/R132,
      C53 and R139 network's Thevenin, as the VCF hands it to the model), so the BA662's input
      resistance loading it is part of the gain.
thd:  a 30 mV input at the ENV's peak: output level and third harmonic.
out:  pin 8 -> OUTPUT jack (VOLUME, IC1a, C2, jack board, 47k load) at a few frequencies.
"""
import argparse, math, os, subprocess, sys, tempfile

import numpy as np

PEAK = 10.11  # the ENV's peak: R158/VR24's threshold on the +14 V rail (docs/circuit-filter.md 5.5)

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from compare_vcf import blocks_exe, NGSPICE  # noqa: E402


def taper_a(x, mid=0.15):
    b = (1 / mid - 1) ** 2
    return (b ** x - 1) / (b - 1)


def netlist(venv, vin, analysis, vol=1.0):
    with open(os.path.join(HERE, "vca.cir")) as f:
        s = f.read()
    k = taper_a(vol)
    return (s.replace("{VENV}", repr(venv)).replace("{VIN}", vin).replace("{BAPARAMS}", "")
             .replace("{RVT}", repr(max(100e3 * (1 - k), 1e-3))).replace("{RVB}", repr(max(100e3 * k, 1e-3)))
             .replace("{ANALYSIS}", analysis))


def spice(cir, outname):
    d = tempfile.mkdtemp(prefix="sh101vca-")
    p = os.path.join(d, "vca.cir")
    with open(p, "w") as f:
        f.write(cir.replace("OUT.txt", os.path.join(d, outname)))
    r = subprocess.run([NGSPICE, "-b", p], cwd=HERE, capture_output=True, text=True)
    path = os.path.join(d, outname)
    if not os.path.exists(path):
        print(r.stdout[-2500:], r.stderr[-2500:])
        sys.exit("ngspice failed")
    return np.loadtxt(path), r.stdout


def blocks(*args):
    out = subprocess.run([blocks_exe()] + [str(a) for a in args], capture_output=True, text=True,
                         check=True).stdout
    return [float(x) for x in out.split()]


def model_iabc(venv):
    # S6's ENV contact as a Thevenin source: R197 from the ENV, R196 from +14 V; then R133.
    g = 1 / 3.3e3 + 1 / 120e3
    return blocks("vca-ctl", (venv / 3.3e3 + 14.3 / 120e3) / g, 1 / g + 12e3)[0] * 300 / 301


def test_ctl():
    print("I_ABC (TR28's collector) against the ENV, ENV mode: model vs SPICE")
    data, _ = spice(netlist(5.0, "Vin vin bus 0", ".control\nset wr_singlescale\ndc Venv 5 10.6 0.2\n"
                            "wrdata OUT.txt i(Vpin1)\n.endc\n"), "ctl.txt")
    worst = 0
    for v, i in data:
        m = model_iabc(v)
        s = abs(i)  # into pin 1
        err = 20 * math.log10(m / s) if s > 1e-9 and m > 1e-9 else float("nan")
        if s > 1e-6:
            worst = max(worst, abs(err))
        if abs((v - 5) * 5 - round((v - 5) * 5)) < 1e-6 and round((v - 5) * 5) % 3 == 0:
            print("  ENV %5.2f V   SPICE %9.3f uA   model %9.3f uA   %+.2f dB" % (v, s * 1e6, m * 1e6, err))
    print("  worst above 1 uA: %.2f dB" % worst)


def amp(t, y, f):
    sel = t > t[-1] - 10 / f
    t, y = t[sel], y[sel]
    c = np.sum(y * np.cos(2 * np.pi * f * t)) * 2 / len(t)
    s = np.sum(y * np.sin(2 * np.pi * f * t)) * 2 / len(t)
    return math.hypot(c, s)


def tran(venv, vpk, f, stop=0.03, vol=1.0):
    vin = "Vin vin bus SIN(0 %g %g)" % (vpk * 76.0, f)  # p3 = vin x 1k/76k
    step = min(2e-6, 1 / f / 200)
    ana = (".options method=trap reltol=1e-5\n.control\nset wr_singlescale\ntran %g %g 0 %g\n"
           "wrdata OUT.txt v(p3) v(p8) v(bus) v(jack)\n.endc\n" % (step, stop, step))
    d, _ = spice(netlist(venv, vin, ana, vol), "tran.txt")
    t = d[:, 0]
    return t, d[:, 1] - d[:, 3], d[:, 2] - d[:, 3], d[:, 4]


def test_gain():
    print("Small-signal gain pin 3 (open circuit) -> pin 8 (1 kHz, 2 mV): model vs SPICE")
    for venv in (6.0, 7.0, 8.5, PEAK):
        t, p3, p8, _ = tran(venv, 2e-3, 1000)
        gs = amp(t, p8, 1000) / 2e-3
        gm = blocks("vca-gain", model_iabc(venv), 2.0)[0]
        print("  ENV %5.2f V   SPICE %8.3f   model %8.3f   %+.2f dB" % (venv, gs, gm, 20 * math.log10(gm / gs)))


def test_thd():
    print("30 mV at pin 3 (open circuit), ENV at its peak (%.2f V): level and 3rd harmonic" % PEAK)
    t, p3, p8, _ = tran(PEAK, 30e-3, 1000, 0.03)
    a1, a3 = amp(t, p8, 1000), amp(t, p8, 3000)
    print("  SPICE: fundamental %.3f V, H3 %.2f %%" % (a1, 100 * a3 / a1))
    print("  (model: run sh101filterblocks vca-gain with 30 mV; the ideal tanh gives H3 = u^2/12)")
    gm = blocks("vca-gain", model_iabc(PEAK), 30.0)[0]
    print("  model: fundamental %.3f V (%+.2f dB)" % (gm * 0.03, 20 * math.log10(gm * 0.03 / a1)))


def test_out():
    print("Pin 8 -> OUTPUT jack: model vs SPICE")
    for vol in (1.0, 0.5):
        for f in (2.0, 10.0, 1000.0, 20000.0):
            stop = max(0.03, 30 / f)
            t, _, p8, jack = tran(8.0, 2e-3, f, stop, vol)
            gs = amp(t, jack, f) / amp(t, p8, f)
            gm = blocks("vca-out", vol, f)[0]
            print("  VOLUME %.1f  %7.0f Hz   SPICE %.4f   model %.4f   %+.2f dB" % (
                vol, f, gs, gm, 20 * math.log10(gm / gs)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="ctl,gain,thd,out")
    a = ap.parse_args()
    for n in a.only.split(","):
        {"ctl": test_ctl, "gain": test_gain, "thd": test_thd, "out": test_out}[n]()


if __name__ == "__main__":
    main()
