#!/usr/bin/env python3
"""The IR3109 at transistor level (ir3109t.lib, from the published decodes) against the model's
IR3109 (Ir3109Vcf, through sh101filterblocks) in the SH-101's own VCF circuit (../vcf.cir), and
against the behavioural chip the other SPICE checks use (models.lib IR3109).

Usage (from machines/sh101, after building sh101filterblocks):
    ../../.venv/bin/python tools/spice/filter/ir3109/compare_ir3109.py [--only dc,limits,ac,osc,onset,saw,harm,expo] [--keep DIR]

dc:     operating point: OUT1-4 and CAP1-4 against the bus, the buffers' |V_GS|
limits: the buffers' tops and the OTA outputs' compliance, driven by a large DC input
ac:     |H| at OUT4 and the VCA input, model vs transistor level (as compare_vcf.py ac)
osc:    self-oscillation frequency and OUT4 peak, model vs transistor level
onset:  the lowest RES that self-oscillates, I_ABC 10 uA
saw:    a 0-10 V saw at 110 Hz through 470k: OUT4 waveform error / level
harm:   the same saw: the first 8 harmonics at OUT4, model vs transistor level (dB)
expo:   the expo converter alone: output current against pin 9, slope, I at V9 = 0, temperature
"""
import argparse, math, os, re, subprocess, sys, tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
FILTER = os.path.dirname(HERE)
sys.path.insert(0, FILTER)
import compare_vcf as V  # noqa: E402

CHIP = "IR3109T"


def netlist(iabc, res, source, analysis):
    """vcf.cir with the chip swapped for the transistor-level one (or kept, CHIP = "IR3109")."""
    s = V.__dict__["_netlist_orig"](iabc, res, source, analysis)
    # Both at 25 C, the model's V_T (SPICE's default is 27 C).
    s = s.replace(".include models.lib", ".include models.lib\n.options temp=25", 1)
    if CHIP != "IR3109":
        s = s.replace(".include models.lib", ".include models.lib\n.include ir3109/ir3109t.lib")
        s = s.replace("IR3109 PARAMS:", CHIP + " PARAMS:")
    return s


V._netlist_orig = V.netlist
V.netlist = netlist


def blocks_exe():
    # SH101_BUILD names another build folder (relative to the repo, or absolute), tried first.
    for d in ([os.environ["SH101_BUILD"]] if "SH101_BUILD" in os.environ else []) + ["build/sh101", "build/cmake"]:
        p = os.path.join(V.LEMUS, d, "sh101filterblocks")
        if os.path.exists(p):
            return p
    sys.exit("build sh101filterblocks first")


V.blocks_exe = blocks_exe
ISCALE = 1.0
_run_blocks = V.run_blocks


def run_blocks(*args, fs=176400):
    """sh101filterblocks, with the model's I_ABC scaled by --iscale for the vcf-* commands (to
    separate a uniform cutoff offset from differences of shape)."""
    if args and str(args[0]).startswith("vcf-") and ISCALE != 1.0:
        args = (args[0], float(args[1]) * ISCALE) + tuple(args[2:])
    return _run_blocks(*args, fs=fs)


V.run_blocks = run_blocks


def spice_op(iabc, res, source, nodes, keep, tag):
    ana = ".control\nop\nprint " + " ".join("v(%s)" % n for n in nodes) + " > op.txt\n.endc\n"
    d = keep or tempfile.mkdtemp(prefix="ir3109op-")
    cir = netlist(iabc, res, source, ana).replace("op.txt", os.path.join(d, tag + "-op.txt"))
    path = os.path.join(d, tag + ".cir")
    open(path, "w").write(cir)
    subprocess.run([V.NGSPICE, "-b", path], cwd=FILTER, capture_output=True, text=True)
    out = open(os.path.join(d, tag + "-op.txt")).read()
    vals = {}
    for m in re.finditer(r"v\(([^)]+)\)\s*=\s*([-+0-9.eE]+)", out):
        vals[m.group(1)] = float(m.group(2))
    return vals


def test_dc(keep):
    print("DC operating point (transistor level), volts against the bus")
    for iabc in (1e-6, 10e-6, 100e-6, 500e-6):
        v = spice_op(iabc, 0.0, "Vin src 0 DC 5", ["bus", "out1", "out2", "out3", "out4", "cap1", "cap2", "cap3", "cap4"],
                     keep, "dc-%g" % iabc)
        b = v["bus"]
        outs = [v["out%d" % i] - b for i in range(1, 5)]
        vgs = [v["out%d" % i] - v["cap%d" % i] for i in range(1, 5)]
        print("  I_ABC %5.0f uA  OUT %s mV   |V_GS| %s V" % (
            iabc * 1e6, " ".join("%+6.2f" % (1e3 * o) for o in outs), " ".join("%.3f" % g for g in vgs)))


def test_limits(keep):
    print("Limits: a large DC input at the mixer (through 470k and C45 shorted), RES 0, I_ABC 10 uA")
    print("  (each stage inverts, so +/- inputs drive the stages to alternate rails)")
    for vin in (-40.0, 40.0):
        src = "Vin src 0 DC %g" % (5 + vin)
        s = netlist(10e-6, 0.0, src, ".control\nop\nprint v(bus) v(out1) v(out2) v(out3) v(out4) v(cap1) v(cap2) v(cap3) v(cap4) > lim.txt\n.endc\n")
        s = s.replace("C45 mix in1 10u", "R45 mix in1 1")
        d = keep or tempfile.mkdtemp(prefix="ir3109lim-")
        tag = "lim%+g" % vin
        s = s.replace("lim.txt", os.path.join(d, tag + ".txt"))
        open(os.path.join(d, tag + ".cir"), "w").write(s)
        subprocess.run([V.NGSPICE, "-b", os.path.join(d, tag + ".cir")], cwd=FILTER, capture_output=True, text=True)
        txt = open(os.path.join(d, tag + ".txt")).read()
        v = {m.group(1): float(m.group(2)) for m in re.finditer(r"v\(([^)]+)\)\s*=\s*([-+0-9.eE]+)", txt)}
        b = v["bus"]
        print("  input %+5.0f V: OUT %s   CAP %s (V re bus)" % (
            vin, " ".join("%+6.3f" % (v["out%d" % i] - b) for i in range(1, 5)),
            " ".join("%+6.3f" % (v["cap%d" % i] - b) for i in range(1, 5))))


def test_harm(keep):
    print("Saw 0-10 V at 110 Hz through 470k: OUT4 harmonics H1..H8 (dB re model H1), model - transistor level")
    for iabc, res in ((5e-6, 0.0), (20e-6, 0.0), (20e-6, 0.5), (60e-6, 0.7)):
        per = 1 / 110.0
        src = saw_source(per)
        stop = STOP
        t, y4, _ = V.spice_tran(iabc, res, src, stop, 2e-6, keep, "harm-%g-%g" % (iabc, res))
        md = np.array(V.run_blocks("vcf-tran", iabc, res, "saw", 110, 5, stop, 1))
        tm, ym = md[:, 0], md[:, 5]
        # 20 whole periods at the end
        n0 = stop - 20 * per
        def harms(tt, yy):
            sel = tt >= n0
            tt, yy = tt[sel], yy[sel]
            out = []
            for k in range(1, 9):
                w = 2 * math.pi * 110 * k
                c, s_ = np.trapezoid(yy * np.cos(w * tt), tt), np.trapezoid(yy * np.sin(w * tt), tt)
                out.append(2 * math.hypot(c, s_) / (tt[-1] - tt[0]))
            return np.array(out)
        hs, hm = harms(t, y4), harms(tm, ym)
        ref = hm[0]
        print("  I_ABC %3.0f uA RES %.1f  model %s dB   diff %s dB" % (
            iabc * 1e6, res, " ".join("%6.1f" % (20 * math.log10(x / ref)) for x in hm),
            " ".join("%+5.2f" % (20 * math.log10(a / b)) for a, b in zip(hm, hs))))


def saw_source(per):
    """A 0-10 V saw that starts at its mean (5 V), so SPICE's operating point leaves C45 where the
    saw keeps it. Starting at 0 V (PULSE's V1) charges C45 5 V away from its mean, and its 1-8 s
    recovery is a slow DC transient into the filter that the model (which settles C45 at start-up)
    doesn't have."""
    h = per / 2
    return "Vin src 0 PWL(0 5 %g 10 %g 0 %g 5) r=0" % (h - 1e-6, h, per)


# Long enough that the model's start-up settling (0.2 s, Ir3109Vcf::kSettleSeconds) is well over
# before the window that is compared.
STOP = 0.6


def test_saw(keep):
    print("Saw 0-10 V at 110 Hz through 470k: OUT4 over the last 50 ms of %.1f s, model vs SPICE" % STOP)
    for iabc, res in ((5e-6, 0.0), (20e-6, 0.5), (20e-6, 0.9), (100e-6, 1.0)):
        per = 1 / 110.0
        src = saw_source(per)
        t, y4, _ = V.spice_tran(iabc, res, src, STOP, 2e-6, keep, "saw-%g-%g" % (iabc, res))
        md = np.array(V.run_blocks("vcf-tran", iabc, res, "saw", 110, 5, STOP, 1))
        tm, ym = md[:, 0], md[:, 5]
        sel = t > STOP - 0.05
        ys = y4[sel] - y4[sel].mean()
        # The two saws start at different points of their period: align the model by the lag
        # (within one period) that best matches SPICE.
        best = None
        for lag in np.arange(0, per, per / 400):
            yy = np.interp(t[sel] - lag, tm, ym)
            yy = yy - yy.mean()
            e = np.mean((yy - ys) ** 2)
            if best is None or e < best[0]:
                best = (e, yy)
        yi = best[1]
        rs, rm = np.sqrt(np.mean(ys ** 2)), np.sqrt(np.mean(yi ** 2))
        if res < 0.78:
            err = np.sqrt(np.mean((yi - ys) ** 2)) / rs
            what = "waveform error %.1f %% (%.1f dB)" % (100 * err, 20 * math.log10(err + 1e-12))
        else:
            what = "level %+.2f dB, peaks %+.0f/%+.0f mV (self-oscillating)" % (
                20 * math.log10(rm / rs), 1e3 * (yi.max() - ys.max()), 1e3 * (yi.min() - ys.min()))
        print("  I_ABC %4.0f uA RES %.1f   SPICE rms %.3f V pk %.3f/%.3f   model rms %.3f V pk %.3f/%.3f   %s" % (
            iabc * 1e6, res, rs, ys.max(), ys.min(), rm, yi.max(), yi.min(), what))


def test_expo(keep):
    print("Expo converter alone (IR3109EXPO): output current against pin 9 (re pin 1)")
    d = keep or tempfile.mkdtemp(prefix="ir3109expo-")
    for temp in (27, 45, 65):
        cir = """* expo
.include models.lib
.include ir3109/ir3109t.lib
.options temp=%g
Vcc vcc 0 9
Vbus bus 0 5
Vc vc bus 0
Vo out 0 5
X1 vc bus vcc 0 out IR3109EXPO PARAMS: iref=3u
.control
set wr_singlescale
dc Vc -0.2 0.2 0.002
wrdata %s i(Vo)
.endc
.end
""" % (temp, os.path.join(d, "expo-%g.txt" % temp))
        p = os.path.join(d, "expo-%g.cir" % temp)
        open(p, "w").write(cir)
        subprocess.run([V.NGSPICE, "-b", p], cwd=FILTER, capture_output=True, text=True)
        a = np.loadtxt(os.path.join(d, "expo-%g.txt" % temp))
        v9, i = a[:, 0], -a[:, 1]
        i0 = np.interp(0.0, v9, i)
        # slope from the log over the linear region (1 nA .. 5 uA)
        sel = (i > 1e-9) & (i < 5e-6)
        k = np.polyfit(v9[sel], np.log2(i[sel]), 1)[0]
        # linearity error against the low-current fit at larger currents
        fit = 2 ** np.polyval(np.polyfit(v9[sel], np.log2(i[sel]), 1), v9)
        errs = []
        for target in (12e-6, 100e-6, 300e-6, 500e-6, 700e-6):
            j = np.argmin(np.abs(i - target))
            errs.append("%g uA %+.1f %%" % (target * 1e6, 100 * (i[j] / fit[j] - 1)))
        print("  %2d C: I(V9 = 0) %.3f uA, slope %.2f mV/oct, max %.0f uA; linearity %s" % (
            temp, i0 * 1e6, -1e3 / k, i.max() * 1e6, ", ".join(errs)))


def main():
    global CHIP, ISCALE
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="dc,limits,ac,osc,onset,saw,harm,expo")
    ap.add_argument("--keep")
    ap.add_argument("--chip", default="IR3109T", help="IR3109T (transistor level) or IR3109 (behavioural)")
    ap.add_argument("--iscale", type=float, default=1.0, help="scale the model's I_ABC (vcf-* commands)")
    a = ap.parse_args()
    CHIP = a.chip
    ISCALE = a.iscale
    tests = {"dc": test_dc, "limits": test_limits, "ac": V.test_ac, "osc": V.test_osc, "onset": V.test_onset,
             "saw": test_saw, "harm": test_harm, "expo": test_expo}
    for name in a.only.split(","):
        tests[name](a.keep)


if __name__ == "__main__":
    main()
