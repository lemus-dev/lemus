#!/usr/bin/env python3
"""The whole SH-101 voice against SPICE of the whole transcribed circuit (voice.cir): the blocks
joined from the VCO's outputs to the OUTPUT jack, so they load and drive each other as on the
board, against the model's Voice through its public interface (sh101voiceblocks).

Usage (from machines/sh101, after building sh101voiceblocks):
    ../../.venv/bin/python tools/spice/voice/compare_voice.py [--only NAME,...] [--group G,...]
           [--keep DIR] [--jobs N]

Only the VCO core is not simulated. SPICE's saw, the CEM3340 pulse comparator's saw input and the
4013's outputs follow the model's own VCO (its frequency, sample by sample from its reset, with the
4013 toggling as the model's does), so both sides play the same phase; everything after them is
the circuit. Both run with the service-notes calibration (the schematic's values), SPICE with the
same parts: VR8 at the model's trim, the IR3109's expo at the junction temperature that gives
19 mV/oct and with its reference set for 2.4 uA at pin 9 = bus.

For each case it prints, model / SPICE: the level (peak of a 30 ms RMS envelope, dBV at the jack);
the envelope's mean difference while within 30 dB of the peak; the waveform error (the difference
after lining the two up to a fraction of a sample, against the signal: -20 dB is 10 %); the
harmonics 1-12 (RMS difference in dB of those within 50 dB of the strongest); for noise the
third-octave band levels instead; for the ENV cases the ENV itself (rms difference, as a share of
its swing) and the times to 90 % of the attack and back down.
--keep writes both outputs as WAV files and keeps the decks."""
import argparse, math, os, subprocess, sys, tempfile
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
REPO = os.path.abspath(os.path.join(ROOT, "..", ".."))
NGSPICE = "/opt/homebrew/bin/ngspice"
FS = 44100
OVER = 4                      # SPICE's output grid: FS x OVER
PRE = 0.3                     # the model's run before t = 0 (its start-up settling)

# The model's service-notes values that SPICE needs (core/filter.cpp, core/env_gen.cpp).
MV_PER_OCT, I0 = 19.0e-3, 2.4e-6
POT_END, POT = 200.0, 1e6
TJ = MV_PER_OCT / math.log(2) / 8.617333e-5 - 273.15    # junction temperature for that V_T


def vr8():
    """VR8 as the model trims it (core/filter.cpp, applyCalibration): F5's volt of KCV over F4's,
    through TR42 (R195 from +14 V) and R193 into R192 and the pin-9 divider, one octave of the expo,
    making up TR42's own gain."""
    v14, re195, r193, r192, vref = 14.3, 33e3, 150e3, 33e3, 5.0
    rpar = 1 / (1 / 5600 + 1 / 2800)
    div = 820 / (4700 + rpar + 820)
    r8 = MV_PER_OCT / (r192 / r193 * div)

    def emitter(kcv, r, veb=0.62):
        vth = kcv * 100e3 / (100e3 + r)
        rth = 1 / (1 / 100e3 + 1 / (r + 1e-3))
        for _ in range(12):
            ve = vth + veb
            ie = max((v14 - ve) / re195 - (ve - vref) / r193, 1e-6)
            veb = 0.025693 * math.log(ie / 3.3e-14)
            ve = vth + ie / 301 * rth + veb
        return ve

    def gain(r):
        return emitter(41 / 12, r) - emitter(29 / 12, r) - r8
    a = min(max(100e3 * (1 / r8 - 1), 1.0), 47e3)
    b = a * 0.98
    ga, gb = gain(a), gain(b)
    for _ in range(30):
        if abs(gb) < 1e-9 or gb == ga:
            break
        a, b, ga = b, min(max(b - gb * (b - a) / (gb - ga), 0.0), 47e3), gb
        gb = gain(b)
    return b


def taper_a(x, mid=0.15):
    b = (1 / mid - 1) ** 2
    return (b ** x - 1) / (b - 1)


def blocks_exe():
    # SH101_BUILD names another build folder (relative to the repo, or absolute), tried first.
    for d in ([os.environ["SH101_BUILD"]] if "SH101_BUILD" in os.environ else []) + ["build/sh101", "build/cmake"]:
        p = os.path.join(REPO, d, "sh101voiceblocks")
        if os.path.exists(p):
            return p
    sys.exit("build sh101voiceblocks first")


# Notes: KCV and RANGE codes (1/12 V each). 16' is RANGE 12, 8' 24, 4' 36, 2' 48 (with TRANSPOSE
# folded into KCV): low C2 at 16' (KCV 7), mid C4 at 8' (KCV 19), high C6 at 4'.
LOW, MID, HIGH = (7, 12), (19, 24), (31, 36)
OPEN = dict(vcfFreq=1.0, vcfRes=0.0, vcfEnv=0.0, vcfKybd=0.0)
SRC0 = dict(sawLevel=0.0, pulseLevel=0.0, subLevel=0.0, noiseLevel=0.0)
BASE = dict(vcaMode=1, volume=0.8, attack=0.0, decay=0.0, sustain=1.0, release=0.0, trigger=1,
            pulseWidth=0.0, pwmSource=1, subMode=0, portaMode=2, vcoMod=0.0, vcfMod=0.0,
            bendVco=0.0, bendVcf=0.0, lfoModDepth=0.0, tune=0.5, vcfFreq=1.0, vcfRes=0.0, vcfEnv=0.0,
            vcfKybd=0.0)


def case(group, note, sec=0.32, on=0.02, off=None, spre=0.0, pre=PRE, **c):
    """spre: how long SPICE runs before t = 0, with the model's VCO (the model always runs `pre`
    before t = 0). Cases whose filter rings slowly need it, or SPICE's start shows."""
    p = dict(BASE)
    p.update(SRC0)
    p.update(c)
    div = {0: 2, 1: 4, 2: 4}[int(p["subMode"])] if p["subLevel"] > 0 else 1
    return dict(group=group, ctl=p, note=note, on=on, off=off if off is not None else sec, sec=sec,
                spre=spre, pre=max(pre, spre), div=div, selfosc=p["vcfRes"] >= 0.9)


SCENARIOS = {}
for nm, n in (("low", LOW), ("mid", MID), ("high", HIGH)):
    SCENARIOS[f"saw-{nm}"] = case("osc", n, sawLevel=1.0, **OPEN)
    SCENARIOS[f"pulse-{nm}"] = case("osc", n, pulseLevel=1.0, **OPEN)
SCENARIOS["pulse7-mid"] = case("osc", MID, pulseLevel=1.0, pulseWidth=0.7, **OPEN)
SCENARIOS["pulse7-low"] = case("osc", LOW, pulseLevel=1.0, pulseWidth=0.7, **OPEN)
for m, nm in ((0, "1oct"), (1, "2oct"), (2, "2oct25")):
    SCENARIOS[f"sub-{nm}"] = case("osc", MID, subLevel=1.0, subMode=m, **OPEN)
SCENARIOS["noise"] = case("noise", MID, noiseLevel=1.0, **OPEN)
SCENARIOS["mix-10"] = case("osc", MID, sawLevel=1.0, pulseLevel=1.0, subLevel=1.0, subMode=0, **OPEN)
SCENARIOS["mix-5"] = case("osc", MID, sawLevel=0.5, pulseLevel=0.5, subLevel=0.5, subMode=1, **OPEN)
for f in (0.3, 0.5, 0.7):
    for r in (0.0, 0.5, 0.75, 0.95):
        for w, lv in (("saw", "sawLevel"), ("pulse", "pulseLevel")):
            # A resonant or low filter rings for a while: both sides settle first. At FREQ 3 (about
            # 16 Hz) near and past the onset that takes seconds: the ring decays, or self-oscillation
            # builds from the circuits' noise.
            spre = 3.0 if (f <= 0.3 and r >= 0.75) else 0.3 if (r >= 0.5 or f <= 0.3) else 0.0
            sec = 0.6 if (f <= 0.3 and r >= 0.75) else 0.32
            SCENARIOS[f"vcf-{w}-f{int(f * 10)}-r{int(r * 100):02d}"] = case(
                "vcf", MID, sec=sec, spre=spre, **{lv: 1.0}, vcfFreq=f, vcfRes=r, vcfEnv=0.0, vcfKybd=0.0)
SCENARIOS["vcf-saw5-f5-r50"] = case("vcf", MID, spre=0.3, sawLevel=0.5, vcfFreq=0.5, vcfRes=0.5)
SCENARIOS["vcf-pulse5-f5-r75"] = case("vcf", MID, spre=0.3, pulseLevel=0.5, vcfFreq=0.5, vcfRes=0.75)
SCENARIOS["vcf-saw-kybd"] = case("vcf", HIGH, spre=0.3, sawLevel=1.0, vcfFreq=0.3, vcfRes=0.5, vcfKybd=1.0)
for mode, nm in ((0, "vcaenv"), (1, "vcagate")):
    SCENARIOS[f"env-{nm}"] = case("env", MID, sec=1.3, on=0.02, off=0.75, sawLevel=1.0, vcfFreq=0.3,
                                  vcfRes=0.5, vcfEnv=0.6, vcaMode=mode, attack=0.25, decay=0.25,
                                  sustain=0.5, release=0.25)


# --- the model -------------------------------------------------------------------------------

def run_model(sc):
    c = sc["ctl"]
    pre = sc["pre"]
    args = [blocks_exe(), "--calibration", "service", str(pre), str(sc["sec"])]
    args += [f"{k}={v}" for k, v in c.items()]
    kcv, rng = sc["note"]
    args += [f"@{-pre}:{kcv},{rng},0", f"@{sc['on']}:{kcv},{rng},1", f"@{sc['off']}:{kcv},{rng},0"]
    r = subprocess.run(args, capture_output=True, text=True, check=True)
    lat = float(r.stderr.split("latency")[1].split()[0])
    d = np.array([list(map(float, l.split())) for l in r.stdout.strip().splitlines()])
    return d, lat


# --- SPICE -------------------------------------------------------------------------------------

def pwl(points):
    return " ".join(f"{t:.9g} {v:.6g}" for t, v in points)


def vco_sources(d, sec, spre=0.0):
    """The saw and the 4013's outputs as PWL sources from the model's VCO frequency: its phase
    integrated sample by sample from the reset, the wraps found to a fraction of a sample. SPICE's
    time is the model's plus spre (SPICE starts spre before t = 0)."""
    t, hz = d[:, 0] + spre, d[:, 2]
    sec = sec + spre
    phase = np.cumsum(hz / FS)   # after each sample
    i0 = int(np.searchsorted(t, 0.0))
    ph0 = phase[i0 - 1] if i0 > 0 else 0.0
    n0 = int(math.floor(ph0))
    q1 = n0 % 2
    q2 = (-(-n0 // 2)) % 2
    V4013 = 14.3                # the 4013's outputs swing to its supply, the +14 V rail
    edge = 50e-9                # the saw's reset takes 2 x edge
    qd, qe = 300e-9, 200e-9     # the 4013 switches after the saw's drop (TR41 lets go of its clock
                                # as the saw falls through 4.6 V) and takes 200 ns (CMOS at 15 V);
                                # edges that coincide with the saw's stall SPICE's time step
    saw = [(0.0, 10 * (ph0 - n0))]
    s1, s2 = [(0.0, V4013 * q1)], [(0.0, V4013 * q2)]
    k = n0 + 1
    tt = t[i0:] + 1 / FS        # phase[i] holds after sample i, i.e. at its end
    pp = phase[i0:]
    while True:
        j = int(np.searchsorted(pp, k))
        if j >= len(pp) or tt[j] > sec:
            break
        a = pp[j - 1] if j > 0 else ph0
        ta = tt[j - 1] if j > 0 else 0.0
        tw = ta + (k - a) / (pp[j] - a) * (tt[j] - ta)
        h = hz[i0 + j]
        saw += [(tw - edge, 10 * (1 - edge * h)), (tw + edge, 10 * edge * h)]
        q1 ^= 1
        s1 += [(tw + qd, V4013 * (1 - q1)), (tw + qd + qe, V4013 * q1)]
        if q1:
            q2 ^= 1
            s2 += [(tw + qd, V4013 * (1 - q2)), (tw + qd + qe, V4013 * q2)]
        k += 1
    endph = np.interp(sec, tt, pp)
    saw.append((sec, 10 * (endph - math.floor(endph))))
    return saw, s1, s2


def mix_mean(c, pw):
    """The mixer node's mean (C45 open), so the operating point starts C45 settled: each slider's
    wiper mean through its Thevenin and mixing resistor (docs/circuit-osc.md 4.4-4.7: R92 and the
    4013 on the 14.3 V rail)."""
    def wiper(top_mean, bottom, k, rs=0.0):
        return bottom + k * (top_mean - bottom), 100e3 * k * (1 - k) + rs * k * k
    br = []
    v, r = wiper(5.0, 5.0, c["sawLevel"])
    br.append((v, r + 470e3))
    high = 1 - pw
    v, r = wiper(high * 10.54 + (1 - high) * 0.03, 5.0, c["pulseLevel"], 68e3 * 100 / 168)
    br.append((v, r + 680e3))
    sub = {0: 9.73 / 2, 1: 9.73 / 2, 2: (0 + 9.40 + 9.40 + 11.21) / 4}[int(c["subMode"])]
    v, r = wiper(sub, 0.0, c["subLevel"], 47e3 * 100 / 147)
    br.append((v, r + 820e3))
    v, r = wiper(5.0, 5.0, c["noiseLevel"])
    br.append((v, r + 150e3))
    g = sum(1 / r for _, r in br)
    return sum(v / r for v, r in br) / g


def pots(c, kcv):
    def b(x, name):
        x = min(max(x, 0.0), 1.0)
        return {f"{name}t": max(100e3 * (1 - x), 1e-3), f"{name}b": max(100e3 * x, 1e-3)}
    p = {}
    p.update(b(c["sawLevel"], "r14"))
    p.update(b(c["pulseLevel"], "r13"))
    p.update(b(c["subLevel"], "r15"))
    p.update(b(c["noiseLevel"], "r16"))
    p.update(b(c["pulseWidth"], "r12"))
    p.update(b(c["vcfFreq"], "r17"))
    p.update(b(c["vcfRes"], "r18"))
    p.update(b(c["vcfEnv"], "r19"))
    p.update(b(c["vcfKybd"], "r21"))
    p.update(b(c["sustain"], "rs"))
    p["rstop"], p["rsbot"] = p.pop("rst"), p.pop("rsb")
    k = taper_a(c["volume"])
    p["rvt"], p["rvb"] = max(100e3 * (1 - k), 1e-3), max(100e3 * k, 1e-3)
    p["ratt"] = POT_END + POT * taper_a(c["attack"])
    p["rdec"] = POT_END + POT * taper_a(c["decay"])
    p["rrel"] = POT_END + POT * taper_a(c["release"])
    p["rvr8"] = vr8()
    p["vkcv"] = kcv / 12.0
    p["sel"] = int(c["subMode"])
    p["rd"] = 1.5e3  # TR23's junction: the model's 1150 Hz noise corner (model-osc.md)
    p["rbh"] = RBH
    p["iref"] = IREF
    p["tj"] = TJ
    return p


IREF = I0       # set by calibrate()
NOISE_A = 0.0   # set by calibrate()
RX = 128.4e3    # R94 + VR4, set by calibrate()
RBH = 10e6      # the IR3109 decode's tail-mirror helper bias (ir3109t.lib Rbh); --rbh to vary


def deck(sc, d, tag, out):
    c = sc["ctl"]
    kcv, _ = sc["note"]
    spre = sc["spre"]
    saw, s1, s2 = vco_sources(d, sc["sec"], spre)
    pw = (2.50 + 2.35 * c["pulseWidth"]) / 5.0            # pin 5 / 5 V (docs/circuit-osc.md 4.4; R178 on +14 V)
    src = [f"Vsaw nsaw 0 PWL({pwl(saw)})",
           # the comparator's 20 mV transition keeps SPICE's time step workable; TR24 itself
           # switches within a twentieth of it, a microsecond or two at these pitches
           "Bp4 np4 0 V = 6.75*(1+tanh((V(npin5)-V(nsaw)/2)/0.02))"]
    if c["subLevel"] > 0:
        src += [f"Vq1 nq1 0 PWL({pwl(s1)})", f"Vq2 nq2 0 PWL({pwl(s2)})"]
    else:
        # SUB at 0 grounds VR15's wiper, so the 4013 can't reach the mixer; held still, its
        # diodes don't swap over on their floating node, which stalls SPICE
        src += ["Vq1 nq1 0 0", "Vq2 nq2 0 0"]
    if c["noiseLevel"] > 0:
        src.append(f"Inz nsrc 0 dc 0 trnoise({NOISE_A:.6g} 5u 0 0)")
    if sc["selfosc"]:
        # The model starts self-oscillation from its circuit noise (20 uV RMS at the mixer node,
        # core/filter.cpp); SPICE gets one small kick into the first stage instead (a noise source's
        # breakpoints stall SPICE's time step over seconds). Steady self-oscillation doesn't depend
        # on how it started.
        src.append("Ikick in1 0 PULSE(0 10n 1m 10u 10u 5m 100)")
    on, off = sc["on"] + spre, sc["off"] + spre
    gate = f"Vgate gate 0 PWL(0 0 {on:.9g} 0 {on + 1e-6:.9g} 12 {off:.9g} 12 {off + 1e-6:.9g} 0)"
    vca = "R133 s6 e28 12k" if int(c["vcaMode"]) == 0 else "R133 n83 e28 12k"
    p = pots(c, kcv)
    p["rx"] = RX
    step = 1 / (FS * OVER)
    ana = (f".ic v(mix)={mix_mean(c, pw):.6f}\n"
           ".options method=gear reltol=2e-4 abstol=1e-12 vntol=2e-6 itl4=40\n"
           ".control\nset wr_singlescale\noption numdgt=10\n"
           f"tran {step:.6g} {sc['sec'] + spre:.6g} {spre:.6g} 2u\n"
           f"linearize v(jack) v(env) v(th) i(v.xic14.vsx)\nwrdata {out} v(jack) v(env) v(th) i(v.xic14.vsx)\nquit\n.endc\n")
    net = open(os.path.join(HERE, "voice.cir")).read()
    net = (net.replace("{PARAMS}", ".param " + " ".join(f"{k}={v:.9g}" for k, v in p.items()))
              .replace("{SOURCES}", "\n".join(src)).replace("{GATE}", gate).replace("{VCA}", vca)
              .replace("{ANALYSIS}", ana))
    return net


def run_spice(sc, d, workdir, tag):
    out = os.path.join(workdir, tag + ".txt")
    path = os.path.join(workdir, tag + ".cir")
    with open(path, "w") as fh:
        fh.write(deck(sc, d, tag, out))
    r = subprocess.run([NGSPICE, "-b", path], cwd=HERE, capture_output=True, text=True)
    if not os.path.exists(out) or "aborted" in r.stdout + r.stderr:
        raise RuntimeError(f"{tag}: ngspice failed\n{(r.stdout + r.stderr)[-1500:]}")
    s = np.loadtxt(out)
    return s


def calibrate(workdir):
    """The expo's reference for I0 at pin 9 = bus, and the noise current for the model's 1.05 V
    RMS at IC12a's output (core/osc.cpp noiseRms), from two short runs of the same deck."""
    global IREF, NOISE_A, RX
    sc = case("cal", MID, sec=0.05, noiseLevel=1.0, vcfFreq=0.5)
    d, _ = run_model(sc)
    IREF, NOISE_A = I0, 1e-6
    net = deck(sc, d, "cal", "x")
    # pin 9 at the bus: I_ABC = i(Vsx) inside the subcircuit
    net = net.replace("R125 pin9 bus 820", "R125 pin9 bus 820\nVpin9 pin9 bus 0")
    cut = net.index(".ic ")
    end = net.index(".end\n", cut)
    net = net[:cut] + (".control\nop\nprint v(ne25) i(v.xic14.vsx)\nquit\n.endc\n") + net[end:]
    path = os.path.join(workdir, "cal-op.cir")
    open(path, "w").write(net)
    r = subprocess.run([NGSPICE, "-b", path], cwd=HERE, capture_output=True, text=True)
    i = ve = None
    for line in r.stdout.splitlines():
        if "i(v.xic14.vsx)" in line and "=" in line:
            i = float(line.split("=")[1])
        if line.strip().startswith("v(ne25)") and "=" in line:
            ve = float(line.split("=")[1])
    if i is None or ve is None:
        raise RuntimeError("calibration failed\n" + r.stdout[-2000:] + r.stderr[-1000:])
    IREF = I0 * I0 / i
    # SN p.6 (D): VR4 trimmed for a square at PULSE WIDTH 0, i.e. pin 5 at 2.50 V (pwFullVolts / 2)
    RX = 2.5 * 100e3 / (ve - 2.5)
    # noise: a short transient with 1 uA RMS, scaled to the model's RMS
    net = deck(sc, d, "caln", os.path.join(workdir, "caln.txt"))
    net = net.replace("linearize v(jack) v(env) v(th)", "linearize v(nnoise)").replace(
        "wrdata " + os.path.join(workdir, "caln.txt") + " v(jack) v(env) v(th)",
        "wrdata " + os.path.join(workdir, "caln.txt") + " v(nnoise)")
    path = os.path.join(workdir, "caln.cir")
    open(path, "w").write(net)
    subprocess.run([NGSPICE, "-b", path], cwd=HERE, capture_output=True, text=True)
    v = np.loadtxt(os.path.join(workdir, "caln.txt"))[:, 1]
    v = v[len(v) // 4:]
    NOISE_A = 1e-6 * 1.05 / np.std(v)
    return IREF, NOISE_A, RX


# --- comparison ---------------------------------------------------------------------------------

def spice_to_fs(s, sec, spre=0.0):
    """SPICE's output (and the ENV, the threshold, I_ABC) on the model's grid from t = 0."""
    t, y = s[:, 0] - spre, s[:, 1]
    fs2 = 1 / np.median(np.diff(t))
    b, a = signal.butter(6, 18000 / (fs2 / 2))
    y = signal.filtfilt(b, a, y)
    grid = np.arange(int(sec * FS)) / FS
    return tuple(np.interp(grid, t, v) for v in (y, s[:, 2], s[:, 3], s[:, 4]))


def peak(x, fmin=5.0):
    """The strongest spectral line (Hz, parabolic) and its level (dB re 1 V RMS)."""
    n = len(x)
    w = np.hanning(n)
    X = np.abs(np.fft.rfft((x - x.mean()) * w)) / (w.sum() / 2) / math.sqrt(2)
    f = np.fft.rfftfreq(n, 1 / FS)
    k0 = int(np.searchsorted(f, fmin))
    k = k0 + int(np.argmax(X[k0:]))
    y0, y1, y2 = np.log(X[k - 1:k + 2] + 1e-20)
    off = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
    return (k + off) * FS / n, 20 * math.log10(X[k])


def frac_shift(x, lag):
    n = len(x)
    f = np.fft.rfftfreq(2 * n)
    X = np.fft.rfft(np.concatenate([x, np.zeros(n)]))
    return np.fft.irfft(X * np.exp(-2j * np.pi * f * lag), 2 * n)[:n]


def best_lag(a, b, maxlag):
    """The fractional lag of b against a (samples) by cross-correlation, refined parabolically."""
    n = len(a)
    c = signal.correlate(b, a, mode="full", method="fft")
    mid = n - 1
    w = c[mid - maxlag:mid + maxlag + 1]
    k = int(np.argmax(w))
    if 0 < k < len(w) - 1:
        y0, y1, y2 = w[k - 1], w[k], w[k + 1]
        den = y0 - 2 * y1 + y2
        off = 0.5 * (y0 - y2) / den if den != 0 else 0.0
    else:
        off = 0.0
    return k - maxlag + off


def env_db(y, win=0.03):
    n = int(win * FS)
    p = np.convolve(y * y, np.ones(n) / n, mode="same")
    return 10 * np.log10(p + 1e-20)


def harmonics_db(o, s, hz, nh=12):
    k = int(int(len(o) * hz / FS) * FS / hz)
    if k < int(2 * FS / hz):
        return float("nan"), None
    w = np.hanning(k)
    fo, fs_ = np.abs(np.fft.rfft(o[:k] * w)), np.abs(np.fft.rfft(s[:k] * w))
    harm = [h * hz for h in range(1, nh + 1) if h * hz < 15000]
    idx = [int(round(h * k / FS)) for h in harm]
    ao = np.array([fo[max(i - 1, 0):i + 2].max() for i in idx])
    as_ = np.array([fs_[max(i - 1, 0):i + 2].max() for i in idx])
    keep = as_ > as_.max() * 10 ** (-50 / 20)
    diff = 20 * np.log10(ao / as_)
    return float(np.sqrt(np.mean(diff[keep] ** 2))), diff


def bands_db(o, s):
    f, po = signal.welch(o, FS, nperseg=4096)
    _, ps = signal.welch(s, FS, nperseg=4096)
    edges = 20 * 2 ** (np.arange(0, 30) / 3)
    out = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        k = (f >= lo) & (f < hi)
        if k.sum() and hi < 16000:
            out.append(10 * np.log10(po[k].sum() / ps[k].sum()))
    out = np.array(out)
    return float(np.sqrt(np.mean(out ** 2))), out


def compare(name, keep=None):
    sc = SCENARIOS[name]
    d, lat = run_model(sc)
    workdir = keep or tempfile.mkdtemp(prefix="sh101voice-")
    os.makedirs(workdir, exist_ok=True)
    s = run_spice(sc, d, workdir, name)
    sec = sc["sec"]
    ys, envs, ths, iabcs = spice_to_fs(s, sec, sc["spre"])
    m = d[d[:, 0] >= -0.5 / FS]
    yo = m[:len(ys), 1]
    yo = frac_shift(yo, -lat)                     # undo the decimator's delay
    hz = float(np.median(m[:, 2])) / sc["div"]       # the waveform's own fundamental (the sub's)
    on, off = sc["on"], sc["off"]
    # The steady part: past the gate's DC step (the VCA's offset through C2, tau 73 ms), which the
    # dc column reports on its own.
    i0, i1 = int((on + 0.15) * FS), int(min(off, sec) * FS) - 1
    seg_o, seg_s = yo[i0:i1], ys[i0:i1]
    res = dict(name=name, group=sc["group"], hz=hz, selfosc=sc["selfosc"])
    if sc["group"] in ("vcf", "env"):
        # I_ABC, model (its probe) against SPICE's expo, over the steady part (cents)
        st = slice(int((on + 0.05) * FS), int(min(off, sec) * FS) - 1)
        io = np.median(m[:len(iabcs), 5][st])
        res["iabc"] = (io, float(np.median(iabcs[st])))
    eo, es = env_db(yo), env_db(ys)
    w = slice(int(on * FS), int(sec * FS))
    res["level"] = (10 * math.log10(np.mean(seg_o ** 2)), 10 * math.log10(np.mean(seg_s ** 2)))  # dBV RMS

    def dc_step(y):
        n = int(round(2 * FS / hz))
        i, j = int(on * FS), int((on + 0.005) * FS)
        return 1e3 * (np.mean(y[j:j + n]) - np.mean(y[max(i - n, 0):i]))
    res["dc"] = (dc_step(yo), dc_step(ys))
    live = es[w] > es[w].max() - 30
    res["envdiff"] = float(np.mean(np.abs((eo[w] - eo[w].max()) - (es[w] - es[w].max()))[live]))
    if sc["group"] == "noise":
        res["bands"], res["banddiff"] = bands_db(seg_o, seg_s)
        res["level"] = (10 * np.log10(np.mean(seg_o ** 2)), 10 * np.log10(np.mean(seg_s ** 2)))
    else:
        lag = best_lag(seg_o - seg_o.mean(), seg_s - seg_s.mean(), int(FS / hz / 2) + 2)
        al = frac_shift(seg_s - seg_s.mean(), -lag)
        ao = seg_o - seg_o.mean()
        cut = int(FS / hz) + 4
        e = np.sum((ao[cut:-cut] - al[cut:-cut]) ** 2) / np.sum(ao[cut:-cut] ** 2)
        res["wave"] = 10 * math.log10(e + 1e-30)
        res["lag"] = lag
        steady = slice(int((on + 0.05) * FS), int(min(off, sec) * FS) - 1) if sc["group"] != "env" else \
            slice(int((off - 0.2) * FS), int(off * FS))
        res["harm"], res["harmdiff"] = harmonics_db(yo[steady], ys[steady], hz)
        if sc["selfosc"]:
            last = slice(len(ys) - int(0.25 * FS), len(ys))
            res["osc"] = (peak(yo[last]), peak(ys[last]))
    if sc["group"] == "env":
        th = float(np.median(ths))
        ne = (envs - 5.0) / (th - 5.0)
        no = m[:len(ne), 3]
        res["envrms"] = float(np.sqrt(np.mean((no - ne) ** 2)))

        def t90(x):
            k = np.where(x >= 0.9 * x[int(on * FS):int(off * FS)].max())[0]
            k = k[k >= int(on * FS)]
            return k[0] / FS - on if len(k) else float("nan")

        def tdown(x):
            k = np.where((np.arange(len(x)) > int(off * FS)) & (x <= 0.1 * x[int(off * FS)]))[0]
            return k[0] / FS - off if len(k) else float("nan")

        res["att90"] = (t90(no), t90(ne))
        res["rel10"] = (tdown(no), tdown(ne))
    if keep:
        import soundfile as sf
        sf.write(os.path.join(workdir, f"{name}-model.wav"), (yo / 4).astype(np.float32), FS, subtype="FLOAT")
        sf.write(os.path.join(workdir, f"{name}-spice.wav"), (ys / 4).astype(np.float32), FS, subtype="FLOAT")
    return res


def _job(a):
    name, keep, cal, rbh = a
    global IREF, NOISE_A, RX, RBH
    IREF, NOISE_A, RX = cal
    RBH = rbh
    try:
        return compare(name, keep)
    except RuntimeError as e:
        return dict(name=name, group=SCENARIOS[name]["group"], failed=str(e).splitlines()[-3:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only")
    ap.add_argument("--group")
    ap.add_argument("--keep")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--rbh", type=float, default=10e6,
                    help="the decode's tail-mirror helper bias; its base current comes out of I_ABC")
    a = ap.parse_args()
    global RBH
    RBH = a.rbh
    names = list(SCENARIOS)
    if a.only:
        names = [n for n in names if any(x in n for x in a.only.split(","))]
    if a.group:
        names = [n for n in names if SCENARIOS[n]["group"] in a.group.split(",")]
    cal = calibrate(a.keep or tempfile.mkdtemp(prefix="sh101voicecal-"))
    print(f"SPICE expo reference {cal[0] * 1e6:.3f} uA (I0 {I0 * 1e6:.2f} uA at pin 9 = bus), "
          f"junction {TJ:.1f} C ({MV_PER_OCT * 1e3:.1f} mV/oct); noise current {cal[1] * 1e6:.3f} uA RMS; "
          f"VR8 {vr8():.0f} ohm; R94 + VR4 {cal[2]:.0f} ohm")
    with ProcessPoolExecutor(max_workers=a.jobs) as ex:
        results = list(ex.map(_job, [(n, a.keep, cal, RBH) for n in names]))
    print(f"\n{'case':22} {'Hz':>7} {'level dBV model/SPICE':>22} {'env diff':>9} {'wave err':>9} {'harm 1-12':>10}"
          f" {'DC step mV':>14}")
    failed = [r for r in results if "failed" in r]
    results = [r for r in results if "failed" not in r]
    for r in failed:
        print(f"{r['name']:22} SPICE FAILED: {' / '.join(r['failed'])}")
    for r in results:
        if r["group"] == "noise":
            print(f"{r['name']:22} {'':>7} {r['level'][0]:+8.2f} / {r['level'][1]:+8.2f}  {r['envdiff']:7.2f} dB"
                  f"   third-octave bands RMS {r['bands']:.2f} dB")
            continue
        line = (f"{r['name']:22} {r['hz']:7.1f} {r['level'][0]:+8.2f} / {r['level'][1]:+8.2f}  {r['envdiff']:7.2f} dB"
                f" {r['wave']:7.1f} dB {r['harm']:7.2f} dB  {r['dc'][0]:+6.1f}/{r['dc'][1]:+6.1f}")
        if "iabc" in r:
            io, isp = r["iabc"]
            line += f"   I_ABC {io * 1e6:.3g}/{isp * 1e6:.3g} uA ({1200 * math.log2(io / isp):+.0f} c)"
        if r.get("osc"):
            (fo, lo), (fs_, ls) = r["osc"]
            line += f"   self-osc {fo:.1f}/{fs_:.1f} Hz ({1200 * math.log2(fo / fs_):+.0f} c), {lo:+.1f}/{ls:+.1f} dBV"
        if r["group"] == "env":
            line += (f"   ENV rms {100 * r['envrms']:.1f} %, attack to 90 % {r['att90'][0] * 1e3:.0f}/{r['att90'][1] * 1e3:.0f} ms,"
                     f" release to 10 % {r['rel10'][0] * 1e3:.0f}/{r['rel10'][1] * 1e3:.0f} ms")
        print(line)
    groups = {}
    for r in results:
        groups.setdefault(r["group"], []).append(r)
    print()
    for g, rs in groups.items():
        osc = [r for r in rs if r.get("osc")]
        rs = [r for r in rs if not r.get("osc")]
        if rs:
            lv = [abs(r["level"][0] - r["level"][1]) for r in rs]
            txt = f"{g:6} {len(rs):3d} cases: level within {max(lv):.2f} dB (mean {np.mean(lv):.2f})"
            if g != "noise":
                txt += (f", waveform error {min(r['wave'] for r in rs):.1f} to {max(r['wave'] for r in rs):.1f} dB,"
                        f" harmonics {np.mean([r['harm'] for r in rs]):.2f} dB mean / {max(r['harm'] for r in rs):.2f} max")
            ia = [abs(1200 * math.log2(r["iabc"][0] / r["iabc"][1])) for r in rs if "iabc" in r]
            if ia:
                txt += f", I_ABC within {max(ia):.0f} cents"
            print(txt)
        if osc:
            dc = [1200 * math.log2(r["osc"][0][0] / r["osc"][1][0]) for r in osc]
            dl = [r["osc"][0][1] - r["osc"][1][1] for r in osc]
            print(f"{g:6} {len(osc):3d} self-oscillating: pitch {min(dc):+.0f} to {max(dc):+.0f} cents, "
                  f"level {min(dl):+.2f} to {max(dl):+.2f} dB")


if __name__ == "__main__":
    main()
