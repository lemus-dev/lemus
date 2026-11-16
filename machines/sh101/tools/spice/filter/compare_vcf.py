#!/usr/bin/env python3
"""Compare the VCF model (sh101filterblocks) with SPICE of the transcribed schematic (vcf.cir).

Usage (from machines/sh101, after building sh101filterblocks):
    ../../.venv/bin/python tools/spice/filter/compare_vcf.py [--only ac,osc,onset,saw] [--keep DIR]

ac:    small-signal |H| from the mixer input (470k) to OUT4 and to the BA662 input, at several
       expo currents and resonance settings.
osc:   steady self-oscillation: frequency and OUT4 peak.
onset: the lowest VR18 travel that self-oscillates.
saw:   a 0-10 V saw through R111 470k, waveform error at OUT4 (large signal, clamp active).
"""
import argparse, math, os, subprocess, sys, tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../../.."))
LEMUS = os.path.normpath(os.path.join(ROOT, "../.."))
NGSPICE = "/opt/homebrew/bin/ngspice"


def blocks_exe():
    # SH101_BUILD names another build folder (relative to the repo, or absolute), tried first.
    for d in ([os.environ["SH101_BUILD"]] if "SH101_BUILD" in os.environ else []) + ["build/sh101", "build/cmake"]:
        p = os.path.join(LEMUS, d, "sh101filterblocks")
        if os.path.exists(p):
            return p
    sys.exit("build sh101filterblocks first")


def run_blocks(*args, fs=176400):
    out = subprocess.run([blocks_exe(), "--fs", str(fs)] + [str(a) for a in args], check=True,
                         capture_output=True, text=True).stdout
    return [list(map(float, l.split())) for l in out.strip().splitlines() if l.strip()]


def netlist(iabc, res, source, analysis):
    with open(os.path.join(HERE, "vcf.cir")) as f:
        s = f.read()
    a = min(max(res, 1e-4), 1 - 1e-4)
    return (s.replace("{IABC}", repr(iabc)).replace("{RTOP}", "%gk" % (100 * (1 - a)))
             .replace("{RBOT}", "%gk" % (100 * a)).replace("{SOURCE}", source)
             .replace("{ANALYSIS}", analysis))


def run_spice(cir, out_names, keep=None, tag="vcf"):
    """Runs ngspice on cir in the netlist folder (for .include); each wrdata file name in
    out_names is made unique with the tag and moved into the keep (or a temporary) folder."""
    d = keep or tempfile.mkdtemp(prefix="sh101vcf-")
    os.makedirs(d, exist_ok=True)
    for name in out_names:
        cir = cir.replace(name, tag + "-" + name)
    path = os.path.join(d, tag + ".cir")
    with open(path, "w") as f:
        f.write(cir)
    r = subprocess.run([NGSPICE, "-b", path], cwd=HERE, capture_output=True, text=True)
    res = {}
    for name in out_names:
        src = os.path.join(HERE, tag + "-" + name)
        if not os.path.exists(src):
            print(r.stdout[-3000:], r.stderr[-3000:])
            sys.exit("ngspice failed: " + tag)
        dst = os.path.join(d, tag + "-" + name)
        os.replace(src, dst)
        res[name] = np.loadtxt(dst)
    return res


def test_ac(keep):
    print("AC: |H| from the mixer input (dB), model - SPICE, at OUT4 and the VCA input")
    worst = 0
    for iabc in (1e-6, 10e-6, 100e-6):
        for res in (0.0, 0.5, 0.75):
            ana = (".control\nset wr_singlescale\nac dec 200 10 100k\n"
                   "wrdata ac.txt mag(v(out4)) mag(v(p3))\n.endc\n")
            sp = run_spice(netlist(iabc, res, "Vin src 0 DC 5 AC 1", ana), ["ac.txt"], keep,
                           "ac-%g-%g" % (iabc, res))["ac.txt"]
            f, m4, m3 = sp[:, 0], 20 * np.log10(sp[:, 1]), 20 * np.log10(sp[:, 2])
            fc = 104.5 * iabc * 1e6
            lo, hi = max(10, fc / 30), min(20e3, fc * 4)  # the audio band only
            md = np.array(run_blocks("vcf-ac", iabc, res, lo, hi, 9))
            e4 = md[:, 2] - np.interp(np.log(md[:, 0]), np.log(f), m4)
            e3 = md[:, 1] - np.interp(np.log(md[:, 0]), np.log(f), m3)
            # Only where the response is within 40 dB of its peak (the model's sine fit floor).
            keep4 = md[:, 2] > md[:, 2].max() - 40
            w = max(np.abs(e4[keep4]).max(), np.abs(e3[keep4]).max())
            worst = max(worst, w)
            print("  I_ABC %5.0f uA  RES %.2f  peak %6.2f dB @ %7.1f Hz   max |err| %.3f dB" % (
                iabc * 1e6, res, m4.max(), f[m4.argmax()], w))
    print("  worst %.3f dB" % worst)


def spice_tran(iabc, res, source, stop, step, keep, tag):
    ana = (".options method=gear reltol=1e-5 abstol=1e-12 vntol=1e-7\n"
           ".control\nset wr_singlescale\ntran %g %g 0 %g uic\n"
           "wrdata tran.txt v(out4) v(bus) v(p3)\n.endc\n" % (step, stop, step))
    # uic is not used for the operating point: the .op is computed first by tran without uic
    ana = ana.replace(" uic", "")
    sp = run_spice(netlist(iabc, res, source, ana), ["tran.txt"], keep, tag)["tran.txt"]
    return sp[:, 0], sp[:, 1] - sp[:, 2], sp[:, 3]


def osc_stats(t, y):
    n = len(t)
    t, y = t[3 * n // 4:], y[3 * n // 4:]
    # SPICE's time points crowd where the wave moves fastest, so their plain mean is not the wave's:
    # at 50 uA it sat 41 mV off, and that went into the peak. The mean over time is.
    y = y - np.trapezoid(y, t) / (t[-1] - t[0])
    ups = np.where((y[:-1] < 0) & (y[1:] >= 0))[0]
    if len(ups) < 3:
        return 0.0, float(np.abs(y).max())
    tz = t[ups] - y[ups] * (t[ups + 1] - t[ups]) / (y[ups + 1] - y[ups])
    return (len(tz) - 1) / (tz[-1] - tz[0]), float(np.abs(y).max())


def test_osc(keep):
    print("Self-oscillation: frequency and OUT4 peak, model vs SPICE")
    for iabc in (2e-6, 10e-6, 50e-6):
        for res in (0.9, 1.0):
            stop = max(0.3, 300 / (104.5 * iabc * 1e6))
            kick = "Vin src 0 PWL(0 5 1u 5.05 50u 5.05 51u 5)"
            t, y4, _ = spice_tran(iabc, res, kick, stop, 2e-6, keep, "osc-%g-%g" % (iabc, res))
            fs, ps = osc_stats(t, y4)
            fm, pm, _ = run_blocks("vcf-osc", iabc, res, stop)[0]
            print("  I_ABC %4.0f uA RES %.1f  SPICE %8.2f Hz %.3f V   model %8.2f Hz %.3f V   "
                  "%+.1f cents %+.2f dB" % (iabc * 1e6, res, fs, ps, fm, pm,
                                           1200 * math.log2(fm / fs) if fs and fm else float('nan'),
                                           20 * math.log10(pm / ps) if ps and pm else float('nan')))


def test_onset(keep):
    print("Self-oscillation onset (VR18 travel), I_ABC 10 uA")
    def oscillates_spice(res):
        kick = "Vin src 0 PWL(0 5 1u 5.05 50u 5.05 51u 5)"
        t, y4, _ = spice_tran(10e-6, res, kick, 0.4, 4e-6, keep, "onset-%g" % res)
        n = len(t)
        return np.abs(y4[-n // 10:] - y4[-n // 10:].mean()).max() > 0.02
    def oscillates_model(res):
        return run_blocks("vcf-osc", 10e-6, res, 0.4)[0][1] > 0.02
    for name, fn in (("SPICE", oscillates_spice), ("model", oscillates_model)):
        lo, hi = 0.7, 0.95
        for _ in range(7):
            mid = (lo + hi) / 2
            if fn(mid):
                hi = mid
            else:
                lo = mid
        print("  %-5s onset at %.3f" % (name, (lo + hi) / 2))


def test_saw(keep):
    print("Saw 0-10 V at 110 Hz through 470k: OUT4 waveform, model vs SPICE (last 50 ms)")
    for iabc, res in ((5e-6, 0.0), (20e-6, 0.5), (20e-6, 0.9), (100e-6, 1.0)):
        per = 1 / 110.0
        src = "Vin src 0 PULSE(0 10 0 %g 1u 0 %g)" % (per - 1e-6, per)
        stop = 0.25
        t, y4, _ = spice_tran(iabc, res, src, stop, 2e-6, keep, "saw-%g-%g" % (iabc, res))
        md = np.array(run_blocks("vcf-tran", iabc, res, "saw", 110, 5, stop, 1))
        tm, ym = md[:, 0], md[:, 5]
        sel = t > stop - 0.05
        ys = y4[sel] - y4[sel].mean()
        yi = np.interp(t[sel], tm, ym)
        yi = yi - yi.mean()
        rs, rm = np.sqrt(np.mean(ys ** 2)), np.sqrt(np.mean(yi ** 2))
        if res < 0.78:
            err = np.sqrt(np.mean((yi - ys) ** 2)) / rs
            what = "waveform error %.1f %% (%.1f dB)" % (100 * err, 20 * math.log10(err + 1e-12))
        else:
            # Self-oscillating: not locked to the input, so compare levels, not waveforms.
            what = "level %+.2f dB, peaks %+.0f/%+.0f mV (self-oscillating)" % (
                20 * math.log10(rm / rs), 1e3 * (yi.max() - ys.max()), 1e3 * (yi.min() - ys.min()))
        print("  I_ABC %4.0f uA RES %.1f   SPICE rms %.3f V pk %.3f/%.3f   model rms %.3f V pk %.3f/%.3f   %s" % (
            iabc * 1e6, res, rs, ys.max(), ys.min(), rm, yi.max(), yi.min(), what))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="ac,osc,onset,saw")
    ap.add_argument("--keep")
    a = ap.parse_args()
    for name in a.only.split(","):
        {"ac": test_ac, "osc": test_osc, "onset": test_onset, "saw": test_saw}[name](a.keep)


if __name__ == "__main__":
    main()
