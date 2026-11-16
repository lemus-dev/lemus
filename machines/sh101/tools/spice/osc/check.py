#!/usr/bin/env python3
"""The oscillator section against the service notes' adjustments and the published specifications,
pass or fail, and against the real units where the shootout clips measure something.

Usage (from the machine folder, after building sh101oscblocks):
    ../../.venv/bin/python tools/spice/osc/check.py [--build DIR]
"""
import math, os, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
REPO = os.path.abspath(os.path.join(ROOT, "..", ".."))
# The machine's build folder: SH101_BUILD (relative to the repo, or absolute), else build/sh101.
BUILD = os.path.join(REPO, os.environ.get("SH101_BUILD", "build/sh101"))
if "--build" in sys.argv:
    BUILD = os.path.abspath(sys.argv[sys.argv.index("--build") + 1])
BLOCKS = os.path.join(BUILD, "sh101oscblocks")
SHOOTOUT = os.path.join(ROOT, "private", "reference", "shootout", "wav")
results = []


def ours(*a):
    return subprocess.run([BLOCKS, *map(str, a)], capture_output=True, text=True, check=True).stdout


def check(name, ok, detail):
    results.append(ok)
    print(f"  {'pass' if ok else 'FAIL'}  {name}: {detail}")


def hz(kcv, rng=0.0, modv=5.0, vcomod=0.0, tune=0.5, bend=0.0, bendvco=1.0):
    return float(ours("pitch", kcv, rng, modv, vcomod, tune, bend, bendvco).split()[5])


def cents(a, b):
    return 1200 * math.log2(a / b)


print("Service notes p.4, VCO adjustment (A = 442 Hz, VR6/VR7/VR5 trimmed)")
f = hz(4.75)
check("VCO TUNE: test mode U&D (KCV 4.75 V, RANGE 0) is 442 Hz", abs(cents(f, 442)) < 0.5, f"{f:.3f} Hz")
f2 = hz(0, 4.75)
check("RANGE WIDTH: UP (KCV 0, RANGE 4.75 V) matches U&D", abs(cents(f2, f)) < 0.5, f"{f2:.3f} Hz ({cents(f2, f):+.2f} cents)")
# KCV = (key + 5 + 12 x TRANSPOSE) / 12: F3 is key 0, F5 key 24; 8' is RANGE 2 V.
f3, f5 = hz(17 / 12, 2), hz(41 / 12, 2)
check("VCO WIDTH: F3 to F5 is two octaves", abs(cents(f5, f3) - 2400) < 1, f"{cents(f5, f3):.2f} cents")
a4 = hz(33 / 12, 2)  # 8', M: A4 is key 16
check("Fig. 1: 8', MIDDLE, A4 = 442 Hz", abs(cents(a4, 442)) < 1, f"{a4:.3f} Hz")

print("\nSpecifications and the pitch law")
worst = 0
rows = []
for v in np.arange(0.5, 9.51, 1.0):
    kcv = min(v, 5.25)
    rng = v - kcv
    fv = hz(kcv, rng)
    ideal = 442 * 2 ** (v - 4.75)
    worst = max(worst, abs(cents(fv, ideal)))
    rows.append(f"{v:.1f} V {fv:8.2f} Hz ({cents(fv, ideal):+.1f})")
print("   " + "; ".join(rows))
check("1 V/oct from 0.5 to 9.5 V of KCV + RANGE (16.4 Hz .. 8.4 kHz): HF track against the comparator delay",
      worst < 5, f"worst {worst:.2f} cents from 442 Hz x 2^(V - 4.75)")
lo, hi = hz(5 / 12, 1), hz(60 / 12, 4)
check("range 16' L lowest F (F1) .. 2' H highest C (C9)", abs(cents(lo, 43.85)) < 2 and abs(cents(hi, 8414)) < 5,
      f"{lo:.2f} Hz .. {hi:.0f} Hz")
t0, t1 = hz(4.75, tune=0.0), hz(4.75, tune=1.0)
check("TUNE: about +-50 cents (spec), the circuit gives R89 / R90 x 5 V", abs(cents(t1, t0) / 2 - 50) < 20,
      f"{cents(t0, f):+.1f} .. {cents(t1, f):+.1f} cents around the trimmed pitch")
m0 = hz(4.75, modv=7.742, vcomod=1.0)
check("VCO MOD at 10, triangle peak: IC18a 2.74 V above the bus x 2/3 (R3 / R1) at 1 V/oct",
      abs(cents(m0, f) - 1200 * 2.742 * 2 / 3) < 40,
      f"{cents(m0, f):+.0f} cents")
b9 = hz(4.75, bend=1.0, bendvco=0.9)
check("bender, VCO slider at 9, lever right: +14.7 semitones (a real unit, shootout)", abs(cents(b9, f) / 100 - 14.7) < 0.3,
      f"{cents(b9, f) / 100:+.2f} semitones")

print("\nMODULATOR")
rates = [float(ours("lfohz", k, "tri").split()[0]) for k in (0, 0.5, 1)]
check("LFO/CLK RATE 0.1 .. 30 Hz (spec)", 0.05 < rates[0] < 0.15 and 25 < rates[2] < 40,
      f"{rates[0]:.3f} .. {rates[2]:.1f} Hz (mid-travel {rates[1]:.2f} Hz)")
ratio = float(ours("lfohz", 1, "noise").split()[0]) / rates[2]
check("NOISE position: R31 unloaded, the triangle grows and the clock slows", 0.75 < ratio < 0.9,
      f"x{ratio:.3f}")

print("\nPortamento")
with tempfile.TemporaryDirectory() as tmp:
    out = os.path.join(tmp, "p.txt")
    ours("porta", 1.0, "on", 0.66, 4800, 8, "0,1.0,1,0;0.01,3.0,1,0", out)
    d = np.loadtxt(out)
    t = d[:, 0] - 0.01
    err = np.abs(1200 * (3.0 - d[:, 1]))
    t1c = t[np.argmax(err < 1.0)]
    check("PORTAMENTO 0 .. 5 s (spec): two octaves settle to 1 cent", 4.5 < t1c < 5.6, f"{t1c:.2f} s at 10")
    ours("porta", 0.0, "on", 0.66, 48000, 0.05, "0,1.0,1,0;0.01,3.0,1,0", out)
    d = np.loadtxt(out)
    t = d[:, 0] - 0.01
    err = np.abs(1200 * (3.0 - d[:, 1]))
    t0c = t[np.argmax(err < 1.0)]
    check("PORTAMENTO at 0 is about instant", t0c < 0.03, f"{t0c * 1e3:.1f} ms to 1 cent")

print("\nPULSE WIDTH (MAN)")
rows = [l.split() for l in ours("pwm", "man", 0).strip().split("\n")[1:]]
w0, w10 = float(rows[0][2]), float(rows[-1][2])
check("50 % at 0 .. under 5 % at 10 (spec 0 %; R177/R178 on +14 V leave about 3 %)", abs(w0 - 0.5) < 0.005 and w10 < 0.05, f"{100 * w0:.1f} % .. {100 * w10:.1f} %")

print("\nSub-oscillator and pulse waveforms (VCO at 440 Hz)")
with tempfile.TemporaryDirectory() as tmp:
    out = os.path.join(tmp, "v.txt")
    rate = 192000
    ours("vco", 440, 2.5, rate, 1.0, out)
    d = np.loadtxt(out)[rate // 10:]

    def f0_duty(x, expect):
        x = x - x.mean()
        spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))
        fr = np.fft.rfftfreq(len(x), 1 / rate)
        k = np.argmax(spec[(fr > 5)]) + np.argmax(fr > 5)
        duty = np.mean(x > 0)
        return fr[k], duty
    for col, name, ef, ed in ((2, "pulse (PW 0)", 440, 0.5), (3, "1 OCT DOWN square", 220, 0.5),
                              (4, "2 OCT DOWN square", 110, 0.5), (5, "2 OCT DOWN pulse", 110, 0.75)):
        fq, du = f0_duty(d[:, col], ef)
        check(f"{name}: {ef} Hz, high {100 * ed:.0f} %", abs(fq - ef) < 2 and abs(du - ed) < 0.01, f"{fq:.1f} Hz, high {100 * du:.1f} %")
    # The saw's partials and the aliasing at the top of the range.
    for top in (2000, 8000):
        ours("vco", top, 2.5, 176400, 1.0, out)
        x = np.loadtxt(out)[17640:, 1]
        x = x - x.mean()
        w = np.blackman(len(x))
        spec = np.abs(np.fft.rfft(x * w))
        fr = np.fft.rfftfreq(len(x), 1 / 176400)
        harm = np.zeros_like(spec, dtype=bool)
        for n in range(1, int(88200 / top) + 1):
            harm |= np.abs(fr - n * top) < 15
        band = fr < 20000
        a = 20 * np.log10(spec[band & ~harm].max() / spec[harm].max())
        h = [20 * np.log10(spec[np.abs(fr - n * top) < 15].max() / spec[np.abs(fr - top) < 15].max()) for n in (2, 4)]
        # The B-spline BLEP's kernel droops the edges by sinc^4: -0.35 dB on a 16 kHz partial at 176.4 kHz.
        ok = abs(h[0] + 6.02) < 0.5 and (4 * top > 20000 or abs(h[1] + 12.04) < 0.5) and a < -60
        check(f"saw at {top} Hz: 1/n partials below 20 kHz, aliases far down", ok,
              f"h2 {h[0]:.2f} dB, h4 {h[1]:.2f} dB, worst alias {a:.0f} dB re the fundamental")

print("\nNoise colour against real units (three clips with their own saw's tilt taken out, which removes the filter\n"
      "and the recording chain: -6.3 .. -6.9 dB/octave, centroid 1.04-1.15 kHz; tools/compare/units.py 'noise')")
with tempfile.TemporaryDirectory() as tmp:
    out = os.path.join(tmp, "n.txt")
    ours("noise", 48000, 20, out)
    x = np.loadtxt(out)[:, 1]
    import scipy.signal as ss
    fr, P = ss.welch(x - x.mean(), 48000, nperseg=8192)
    m = (fr > 200) & (fr < 8000)
    slope = np.polyfit(np.log2(fr[m]), 10 * np.log10(P[m]), 1)[0]
    mm = (fr > 50) & (fr < 16000)
    cen = (fr[mm] * P[mm]).sum() / P[mm].sum()
    check("slope 200 Hz .. 8 kHz", -7.4 < slope < -5.8, f"{slope:.2f} dB/octave")
    check("centroid", 850 < cen < 1250, f"{cen:.0f} Hz")
    check("level: IC12a's noise RMS as calibrated", abs(np.std(x) - 1.05) < 0.05, f"{np.std(x):.3f} V")

print(f"\n{sum(results)} of {len(results)} pass")
sys.exit(0 if all(results) else 1)
