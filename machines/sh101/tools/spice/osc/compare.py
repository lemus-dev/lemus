#!/usr/bin/env python3
"""The oscillator section's blocks against SPICE simulations of the transcribed schematic.

Usage (from the machine folder, after building sh101oscblocks):
    ../../.venv/bin/python tools/spice/osc/compare.py [lfo] [lforst] [porta] [grip] [pwm] [levels]
                                                     [noise] [pitch] [--build DIR] [--plots DIR]
With no block named, runs them all. Each prints the model's numbers next to SPICE's and the
difference; docs/model-osc.md, "Validation", records them.

  lfo     MODULATOR: triangle levels and frequency at five RATE positions, both S2 loadings
  lforst  CLK RST: CLK, the triangle's fall and the time to the next rising clock edge
  porta   portamento: glide time constant (ON) at three knob positions, AUTO's gate-edge short
  grip    the grip's LFO MOD: IC2a's sine against the triangle, the fade-in
  pwm     PULSE WIDTH network: pin 5 against the slider for MAN, LFO (both triangle ends), ENV
  levels  pulse and sub-oscillator levels at the S5 contacts / VR13, VR15
  noise   the noise amplifier's response (TR22, C31, IC12a) against the model's colour
  pitch   IC3a, IC12b and the pin-15 summing node: DC gains and the IC12b pole
"""
import math, os, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
REPO = os.path.abspath(os.path.join(ROOT, "..", ".."))
MODELS = os.path.join(HERE, "models.lib")
# The machine's build folder: SH101_BUILD (relative to the repo, or absolute), else build/sh101.
BUILD = os.path.join(REPO, os.environ.get("SH101_BUILD", "build/sh101"))
args = sys.argv[1:]
if "--build" in args:
    BUILD = os.path.abspath(args[args.index("--build") + 1])
    del args[args.index("--build"):args.index("--build") + 2]
PLOTS = None
if "--plots" in args:
    PLOTS = os.path.abspath(args[args.index("--plots") + 1])
    del args[args.index("--plots"):args.index("--plots") + 2]
    os.makedirs(PLOTS, exist_ok=True)
BLOCKS = os.path.join(BUILD, "sh101oscblocks")


def taper_a(t):
    s = 1 / 0.15 - 1
    b = s * s
    t = min(max(t, 0.0), 1.0)
    return (b ** t - 1) / (b - 1)


def spice(netlist, params, analysis, probes, tmp, extra=""):
    deck = os.path.join(tmp, "deck.cir")
    out = os.path.join(tmp, "spice.txt")
    with open(deck, "w") as fh:
        fh.write("* compare.py\n")
        for k, v in params.items():
            fh.write(f".param {k}={v}\n")
        fh.write(f".options temp=25\n.include {MODELS}\n.include {os.path.join(HERE, netlist)}\n{extra}\n")
        fh.write(".control\nset wr_singlescale\noption numdgt=10\n")
        fh.write(f"{analysis}\nwrdata {out} {' '.join(probes)}\nquit\n.endc\n.end\n")
    r = subprocess.run(["ngspice", "-b", deck], capture_output=True, text=True)
    if not os.path.exists(out):
        sys.exit(f"ngspice failed on {netlist}:\n{r.stdout[-3000:]}\n{r.stderr[-3000:]}")
    return np.loadtxt(out)


def ours(*a):
    return subprocess.run([BLOCKS, *map(str, a)], capture_output=True, text=True, check=True).stdout


def edges(t, x, thr):
    """Times where x rises through thr (linear interpolation)."""
    i = np.where((x[:-1] < thr) & (x[1:] >= thr))[0]
    return t[i] + (thr - x[i]) / (x[i + 1] - x[i]) * (t[i + 1] - t[i])


def tri_stats(t, v, t0):
    m = t >= t0
    t, v = t[m], v[m]
    mid = 0.5 * (v.max() + v.min())
    e = edges(t, v, mid)
    f = 1 / np.median(np.diff(e)) if len(e) > 2 else float("nan")
    return v.min(), v.max(), f


# --------------------------------------------------------------------------------------------
def block_lfo():
    print("\nMODULATOR (IC3a/IC3b): triangle low / high (V) and frequency (Hz), model vs SPICE")
    print(f"{'RATE':>5} {'S2':>6} | {'low':>6} {'high':>6} {'Hz':>8} | {'low':>6} {'high':>6} {'Hz':>8} | {'dHz %':>6}")
    for knob in (0.0, 0.25, 0.5, 0.75, 1.0):
        for load, wave in ((1, "tri"), (0, "noise")):
            a = taper_a(knob)
            rb, rt = a * 100e3, (1 - a) * 100e3
            hz_nom = float(ours("lfohz", knob, wave).split()[0])
            periods = 4
            tstop = periods / hz_nom + 0.6 / hz_nom
            t0 = 0.6 / hz_nom
            with tempfile.TemporaryDirectory() as tmp:
                rate = max(48000, 2000 * hz_nom)
                f = os.path.join(tmp, "o.txt")
                ours("lfo", knob, wave, tstop, rate, f)
                d = np.loadtxt(f)
                lo1, hi1, f1 = tri_stats(d[:, 0], d[:, 1], t0)
                step = min(1 / hz_nom / 4000, 20e-6)
                s = spice("lfo.cir", {"rt": max(rt, 1e-3), "rb": max(rb, 1e-3), "load": load, "ron": 1e3, "roff": 1e3 + 1},
                          f"tran {step} {tstop} 0 {step} uic", ["v(ntri)", "v(nsq)", "v(nclk)"], tmp,
                          ".ic v(ntri)=5")
                lo2, hi2, f2 = tri_stats(s[:, 0], s[:, 1], t0)
            print(f"{knob:5.2f} {wave:>6} | {lo1:6.3f} {hi1:6.3f} {f1:8.4f} | {lo2:6.3f} {hi2:6.3f} {f2:8.4f} | "
                  f"{100 * (f1 / f2 - 1):6.2f}")


def block_lforst():
    print("\nCLK RST at RATE 5 (4.5 Hz), triangle position: reset 1.5 ms long at several phases")
    print("(the two runs reach the reset at different points of the cycle: compare the end against the start)")
    print(f"{'phase':>6} | {'start':>6} {'end':>7} {'next rise ms':>12} | {'start':>6} {'end':>7} {'next rise ms':>12}")
    knob = 0.5
    a = taper_a(knob)
    rb, rt = a * 100e3, (1 - a) * 100e3
    hz = float(ours("lfohz", knob, "tri").split()[0])
    for ph in (0.1, 0.2, 0.35, 0.5, 0.6, 0.7, 0.85, 0.95):
        ron = 1.0 / hz + ph / hz + 0.3 / hz
        roff = ron + 1.5e-3
        tstop = roff + 1.2 / hz
        with tempfile.TemporaryDirectory() as tmp:
            f = os.path.join(tmp, "o.txt")
            ours("lfo", knob, "tri", tstop, 96000, f, ron, roff)
            d = np.loadtxt(f)
            s = spice("lfo.cir", {"rt": rt, "rb": rb, "load": 1, "ron": ron, "roff": roff},
                      f"tran 5u {tstop} 0 5u uic", ["v(ntri)", "v(nclk)"], tmp, ".ic v(ntri)=5")
        res = []
        for t, tri, clk in ((d[:, 0], d[:, 1], d[:, 2]), (s[:, 0], s[:, 1], (s[:, 2] > 4.5).astype(float))):
            vstart = np.interp(ron, t, tri)
            vend = np.interp(roff, t, tri)
            rises = edges(t, clk, 0.5)
            nxt = rises[rises > roff + 1e-4]
            res.append((vstart, vend, (nxt[0] - roff) * 1e3 if len(nxt) else float("nan")))
        print(f"{ph:6.2f} | {res[0][0]:6.2f} {res[0][1]:7.3f} {res[0][2]:12.2f} | {res[1][0]:6.2f} {res[1][1]:7.3f} {res[1][2]:12.2f}")


# --------------------------------------------------------------------------------------------
def porta_spice(tmp, mode, rpot, events, tstop):
    def pwl(idx, scale):
        pts = []
        for i, e in enumerate(events):
            v = e[idx] * scale
            if i:
                pts.append(f"{e[0]:.6f} {prev}")
            pts.append(f"{e[0] + 1e-6:.6f} {v}")
            prev = v
        pts.append(f"{tstop} {prev}")
        return "PWL(0 " + str(events[0][idx] * scale) + " " + " ".join(pts) + ")"
    extra = (f"Vkcv nporta 0 {pwl(1, 1)}\nVgate ngate 0 {pwl(2, 12)}\nVpo npo 0 {pwl(3, 5)}\n")
    return spice("porta.cir", {"rpot": rpot, "mode": {"on": 0, "auto": 1, "off": 2}[mode]},
                 f"tran 20u {tstop} 0 20u", ["v(ncv)", "v(nx)"], tmp, extra)


def block_porta():
    print("\nPortamento: glide time constant (ms), ON, 2 V step")
    print(f"{'knob':>5} | {'model':>8} {'SPICE':>8} {'diff %':>7}")
    tau_max = 0.66
    for knob in (0.25, 0.5, 1.0):
        rpot = 2e6 * (0.005 + 0.995 * taper_a(knob))
        tstop = max(6 * rpot * 0.33e-6, 0.05)
        events = [(0, 1.0, 1, 0), (0.01, 3.0, 1, 0)]
        ev = ";".join(f"{e[0]},{e[1]},{e[2]},{e[3]}" for e in events)
        with tempfile.TemporaryDirectory() as tmp:
            f = os.path.join(tmp, "o.txt")
            ours("porta", knob, "on", tau_max, 48000, tstop, ev, f)
            d = np.loadtxt(f)
            s = porta_spice(tmp, "on", rpot, events, tstop)
        taus = []
        for t, v in ((d[:, 0], d[:, 1]), (s[:, 0], s[:, 1])):
            m = t > 0.01
            tt, vv = t[m] - 0.01, v[m]
            target = 1 + 2 * (1 - math.exp(-1))
            taus.append(np.interp(target, vv, tt) * 1e3)
        print(f"{knob:5.2f} | {taus[0]:8.2f} {taus[1]:8.2f} {100 * (taus[0] / taus[1] - 1):7.2f}")
    print("AUTO, knob 1: legato step (no new gate) vs detached step (gate falls, then rises with the KCV)")
    events = [(0, 1.0, 1, 0), (0.05, 3.0, 1, 0), (1.5, 3.0, 0, 0), (1.6, 1.0, 1, 0), (2.4, 1.0, 1, 1), (2.5, 3.0, 1, 1)]
    tstop = 3.0
    ev = ";".join(f"{e[0]},{e[1]},{e[2]},{e[3]}" for e in events)
    rpot = 2e6
    with tempfile.TemporaryDirectory() as tmp:
        f = os.path.join(tmp, "o.txt")
        ours("porta", 1.0, "auto", tau_max, 48000, tstop, ev, f)
        d = np.loadtxt(f)
        s = porta_spice(tmp, "auto", rpot * 1.0, events, tstop)
    for name, t, v in (("model", d[:, 0], d[:, 1]), ("SPICE", s[:, 0], s[:, 1])):
        leg = np.interp(0.05 + 0.66, t, v)
        det = np.interp(1.6 + 0.005, t, v)
        po = np.interp(2.5 + 0.005, t, v)
        print(f"  {name}: legato after 0.66 s {leg:.3f} V (glides), detached 5 ms after the gate {det:.3f} V (jumps to 1.0),"
              f" PORTA OFF 5 ms after {po:.3f} V (jumps to 3.0)")
    if PLOTS:
        import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
        plt.figure(figsize=(8, 3)); plt.plot(d[:, 0], d[:, 1], label="model"); plt.plot(s[:, 0], s[:, 1], "--", label="SPICE")
        plt.legend(); plt.xlabel("s"); plt.ylabel("CV (V)"); plt.tight_layout(); plt.savefig(os.path.join(PLOTS, "porta-auto.png"))


# --------------------------------------------------------------------------------------------
def block_grip():
    print("\nGrip LFO MOD (TR2, IC2a, TR3/C9): IC2a's output, MOD pressed at 0.2 s, triangle 2 Hz")
    lo, hi = 0.1, float(ours("lfohz", 0.5, "tri").split()[2])
    hz = 2.0
    tstop = 3.2
    with tempfile.TemporaryDirectory() as tmp:
        f = os.path.join(tmp, "o.txt")
        ours("grip", hz, 1.0, 0.2, tstop, 48000, f)
        d = np.loadtxt(f)
        n = int(tstop * hz * 2) + 2
        pts = " ".join(f"{k / (2 * hz):.6f} {lo if k % 2 == 0 else hi}" for k in range(n))
        s = spice("grip.cir", {"pushon": 0.2}, "tran 50u 3.2 0 50u uic", ["v(nout)", "v(ntail)", "v(ntri)"], tmp,
                  f"Vtri ntri 0 PWL({pts})")
    for name, t, v in (("model", d[:, 0], d[:, 2]), ("SPICE", s[:, 0], s[:, 1])):
        res = []
        for t0 in (0.5, 0.9, 1.6, 2.6):
            m = (t >= t0) & (t < t0 + 1 / hz)
            res.append(f"{0.5 * (v[m].max() - v[m].min()):.3f}")
        m = t > 2.6
        print(f"  {name}: amplitude (V peak) at 0.5/0.9/1.6/2.6 s: {' '.join(res)}; mean at the end {v[m].mean():.3f} V")
    # Shape: the sine against the triangle, at the end (after normalising the amplitude).
    m1 = d[:, 0] > 2.6
    m2 = s[:, 0] > 2.6
    g = np.interp(d[m1, 0], s[m2, 0], s[m2, 1])
    a = d[m1, 2] - d[m1, 2].mean()
    b = g - g.mean()
    print(f"  shape: rms difference {np.sqrt(np.mean((a - b) ** 2)):.4f} V of {np.sqrt(np.mean(b ** 2)):.3f} V rms")


# --------------------------------------------------------------------------------------------
def block_pwm():
    print("\nPULSE WIDTH network: pin 5 (V) against the slider, model vs SPICE")
    vr4 = float(ours("pwm", "man", 0).split("\n")[0].split()[2])
    cases = [("man", 0, "MAN"), ("lfo", 0.1, "LFO, triangle low"), ("lfo", 9.03, "LFO, triangle high"),
             ("env", 5.0, "ENV at rest"), ("env", 10.42, "ENV at peak")]
    for src, v, label in cases:
        rows = [l.split() for l in ours("pwm", src, v).strip().split("\n")[1:]]
        worst = 0
        out = []
        for sl, p5, _ in rows:
            sl, p5 = float(sl), float(p5)
            if abs(sl * 4 - round(sl * 4)) > 1e-6:
                continue
            k = sl
            with tempfile.TemporaryDirectory() as tmp:
                s = spice("pwm.cir", {"rt": max((1 - k) * 100e3, 1e-3), "rb": max(k * 100e3, 1e-3), "x": 100e3 + vr4,
                                      "src": {"man": 0, "lfo": 1, "env": 2}[src], "vin": v},
                          "op", ["v(npin5)"], tmp)
            sp = float(np.atleast_2d(s)[0, -1])
            worst = max(worst, abs(sp - p5))
            out.append(f"{sl:.2f}:{p5:.3f}/{sp:.3f}")
        print(f"  {label:20s} {'  '.join(out)}   max |diff| {worst * 1e3:.1f} mV")


def block_levels():
    print("\nPulse and sub levels: the current each pushes into the mixer node (held at the bus) through its"
          " mixing resistor, sliders at 10 (µA), model vs SPICE")
    txt = ours("levels", 1, 1)
    model = {}
    for line in txt.strip().split("\n"):
        w = line.split()
        if w[0] == "pulse":
            v, r = float(w[9]), float(w[12])
            model[("pulse", w[1])] = (v - 5) / (680e3 + r) * 1e6
        else:
            v, r = float(w[13]), float(w[16])
            model[("sub", int(w[1]), int(w[3]), int(w[5]))] = (v - 5) / (820e3 + r) * 1e6
    with tempfile.TemporaryDirectory() as tmp:
        for state in (0, 1):
            s = spice("levels.cir", {"pon": 1 - state, "q1": 0, "q2": 0, "sel": 0}, "op", ["i(vmp)"], tmp)
            sp = float(np.atleast_2d(s)[0, -1]) * 1e6
            name = "high" if state else "low "
            print(f"  pulse {name}: model {model[('pulse', name.strip())]:8.3f}  SPICE {sp:8.3f}")
        for q1, q2 in ((0, 0), (1, 0), (0, 1), (1, 1)):
            sp = []
            for sel in (0, 1, 2):
                s = spice("levels.cir", {"pon": 0, "q1": q1, "q2": q2, "sel": sel}, "op", ["i(vms)"], tmp)
                sp.append(float(np.atleast_2d(s)[0, -1]) * 1e6)
            print(f"  sub q1={q1} q2={q2}: 1 OCT {model[('sub', 0, q1, q2)]:7.3f}/{sp[0]:7.3f}  2 OCT {model[('sub', 1, q1, q2)]:7.3f}/{sp[1]:7.3f}"
                  f"  25% pulse {model[('sub', 2, q1, q2)]:7.3f}/{sp[2]:7.3f}")


# --------------------------------------------------------------------------------------------
def block_noise():
    print("\nNoise amplifier: TR22 + C31 + IC12a response (dB re 200 Hz), SPICE for three junction resistances,"
          " against the model's colour (C31 pole at the fitted 1150 Hz, IC12a's shelf)")
    freqs = np.array([100, 200, 400, 800, 1600, 3200, 6400, 10000])
    f2 = 1 / (2 * math.pi * 220e3 * 240e-12)

    def model(f, f1=1150.0):
        s = 1j * f
        return 20 * np.log10(abs(1 / (1 + s / f1) * (1 + 22 / (1 + s / f2))))
    m = model(freqs)
    print(f"  {'Hz':>14} " + " ".join(f"{int(f):>6}" for f in freqs))
    print(f"  {'model':>14} " + " ".join(f"{x - m[1]:6.1f}" for x in m))
    for rd in (100, 1e3, 10e3, 100e3):
        with tempfile.TemporaryDirectory() as tmp:
            s = spice("noise.cir", {"rd": rd}, "ac dec 40 20 40k", ["vdb(nout)"], tmp)
        f, db = s[:, 0], s[:, 1]
        lv = np.interp(freqs, f, db)
        # the -3 dB corner of the part before IC12a: fit a one-pole + the shelf
        best = min(((np.mean(((lv - lv[1]) - (model(freqs, f1) - model(freqs, f1)[1])) ** 2), f1)
                    for f1 in np.arange(200, 5000, 10)))
        print(f"  {'r_d ' + (str(int(rd)) + ' ohm' if rd < 1e3 else str(int(rd / 1e3)) + 'k'):>14} " + " ".join(f"{x - lv[1]:6.1f}" for x in lv) +
              f"   (one-pole fit {best[1]:.0f} Hz)")


# --------------------------------------------------------------------------------------------
def block_pitch():
    print("\nIC3a / IC12b / pin 15: DC, model vs SPICE (MODV is IC18a's output)")
    print(f"{'MODV':>5} {'VCOMOD':>6} {'TUNE':>5} {'BEND':>5} | {'IC3a':>8} {'IC12b':>8} | {'IC3a':>8} {'IC12b':>8} {'I15 uA':>8}")
    for modv, vm, tune, bend in ((5.0, 0.0, 0.5, 0.0), (8.96, 1.0, 0.5, 0.0), (1.61, 1.0, 0.5, 0.0), (8.96, 0.5, 0.5, 0.0),
                                 (5.0, 0.0, 0.0, 0.0), (5.0, 0.0, 1.0, 0.0), (5.0, 0.0, 0.5, 1.0), (5.0, 0.0, 0.5, -1.0)):
        w = ours("pitch", 4.75, 0, modv, vm, tune, bend, 1.0).split()
        ic3a, ic12b = float(w[1]), float(w[3])
        k = taper_a(vm)
        with tempfile.TemporaryDirectory() as tmp:
            s = spice("pitch.cir", {"modv": modv, "k11t": max((1 - k) * 100e3, 1e-3), "k11b": max(k * 100e3, 1e-3),
                                    "k9t": max(tune * 100e3, 1e-3), "k9b": max((1 - tune) * 100e3, 1e-3),
                                    "lever": 5 + 1.48 * bend},
                      "op", ["v(n3a)", "v(n12b)", "i(vpin15)"], tmp)
        r = np.atleast_2d(s)[0]
        print(f"{modv:5.2f} {vm:6.2f} {tune:5.2f} {bend:5.1f} | {ic3a:8.4f} {ic12b:8.4f} | {r[1]:8.4f} {r[2]:8.4f} {r[3] * 1e6:8.3f}")
    # IC12b's pole from its feedback: R89 C35
    print(f"  IC12b feedback pole: {1 / (2 * math.pi * 100e3 * 680e-12):.0f} Hz (R89 C35); the model uses the same one-pole")


BLOCKS_ALL = {"lfo": block_lfo, "lforst": block_lforst, "porta": block_porta, "grip": block_grip,
              "pwm": block_pwm, "levels": block_levels, "noise": block_noise, "pitch": block_pitch}

if __name__ == "__main__":
    todo = [a for a in args if a in BLOCKS_ALL] or list(BLOCKS_ALL)
    for b in todo:
        BLOCKS_ALL[b]()
