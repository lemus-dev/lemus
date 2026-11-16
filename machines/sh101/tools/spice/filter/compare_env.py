#!/usr/bin/env python3
"""Compare the ENV GEN model (EnvGen) with SPICE of the transcribed circuit (env.cir).

Usage (from machines/sh101): ../../.venv/bin/python tools/spice/filter/compare_env.py [--only a,b]
For each scenario: the ENV waveform error (rms and max, volts), the attack time (gate to peak),
the level at gate-off, the release time to 10 %, and the GATE level (C57's node) error.
"""
import argparse, math, os, subprocess, sys, tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from compare_vcf import blocks_exe, NGSPICE  # noqa: E402

POT_END, POT = 200.0, 1e6
GATE_ON = 0.01
TRIG_W = 0.54e-3   # the model's TRIG pulse for a semitone (EnvGen's detector)


def taper_a(x, mid=0.15):
    b = (1 / mid - 1) ** 2
    return (b ** x - 1) / (b - 1)


SCENARIOS = {
    # name: (A, D, S, R, mode, gate seconds, total seconds, extra)
    "mid": (0.3, 0.4, 0.5, 0.4, "gate", 1.0, 2.5, []),
    "fast": (0.0, 0.0, 0.0, 0.0, "gate", 0.05, 0.15, []),
    "slow": (1.0, 1.0, 0.7, 1.0, "gate", 6.0, 14.0, []),
    "sustain10": (0.2, 0.5, 1.0, 0.2, "gate", 0.6, 1.2, []),
    "sustain0": (0.1, 0.5, 0.0, 0.5, "gate", 1.5, 2.5, []),
    "trig": (0.3, 0.4, 0.5, 0.3, "gatetrig", 1.0, 1.6, [0.5, 0.8]),
    "lfo": (0.1, 0.3, 0.3, 0.2, "lfo", 1.5, 2.0, [4.0]),
}


def spice(sc, keep):
    a, d, s, r, mode, gate, total, extra = sc
    with open(os.path.join(HERE, "env.cir")) as f:
        net = f.read()
    gsrc = "Vgate gate 0 PWL(0 0 %.9g 0 %.9g 12 %.9g 12 %.9g 0)" % (GATE_ON, GATE_ON + 1e-6, GATE_ON + gate,
                                                           GATE_ON + gate + 1e-6)
    if mode == "gatetrig" and extra:
        pts = ["0 0"]
        for t in extra:
            pts += ["%.9g 0" % t, "%.9g 12" % (t + 1e-6), "%.9g 12" % (t + TRIG_W), "%.9g 0" % (t + TRIG_W + 1e-6)]
        p84 = "Vp84 pin84 0 PWL(%s)" % " ".join(pts)
    elif mode == "lfo":
        hz = extra[0]
        per = 1 / hz
        # The model attacks in the square's high half, which starts at the gate (CLK RST): pin 84
        # (TR32 on) is high in the low half.
        p84 = "Vp84 pin84 0 PULSE(0 12 %g 1u 1u %g %g)" % (GATE_ON + per / 2, per / 2 - 2e-6, per)
    else:
        p84 = "Vp84 pin84 0 0"
    sub = {"{GATE}": gsrc, "{PIN84}": p84, "{VCALOAD}": "",
           "{RATT}": repr(POT_END + POT * taper_a(a)), "{RDEC}": repr(POT_END + POT * taper_a(d)),
           "{RREL}": repr(POT_END + POT * taper_a(r)),
           "{RSTOP}": repr(max(100e3 * (1 - s), 1e-3)), "{RSBOT}": repr(max(100e3 * s, 1e-3))}
    step = min(20e-6, total / 20000)
    sub["{ANALYSIS}"] = (".options method=gear reltol=1e-4\n.control\nset wr_singlescale\n"
                         "tran %g %g\nwrdata env-out.txt v(env) v(n83)\n.endc\n" % (step, total))
    for k, v in sub.items():
        net = net.replace(k, v)
    dd = keep or tempfile.mkdtemp(prefix="sh101env-")
    os.makedirs(dd, exist_ok=True)
    tag = "env-%s-%s" % (mode, "-".join("%g" % x for x in (a, d, s, r)))
    net = net.replace("env-out.txt", tag + ".txt")
    p = os.path.join(dd, tag + ".cir")
    with open(p, "w") as f:
        f.write(net)
    res = subprocess.run([NGSPICE, "-b", p], cwd=HERE, capture_output=True, text=True)
    out = os.path.join(HERE, tag + ".txt")
    if not os.path.exists(out):
        print(res.stdout[-2000:], res.stderr[-2000:])
        sys.exit("ngspice failed")
    data = np.loadtxt(out)
    os.replace(out, os.path.join(dd, tag + ".txt"))
    return data[:, 0], data[:, 1], data[:, 2]


def model(sc):
    a, d, s, r, mode, gate, total, extra = sc
    out = subprocess.run([blocks_exe(), "env", a, d, s, r, mode, gate, total] + extra if False else
                         [blocks_exe(), "env"] + [str(x) for x in (a, d, s, r, mode, gate, total)] +
                         [str(x) for x in extra], capture_output=True, text=True, check=True).stdout
    m = np.array([list(map(float, l.split())) for l in out.strip().splitlines()])
    return m[:, 0], m[:, 1], m[:, 2]


def metrics(t, v, gate):
    on = (t >= GATE_ON) & (t < GATE_ON + gate)
    tp = t[on][np.argmax(v[on])] - GATE_ON
    voff = np.interp(GATE_ON + gate, t, v)
    after = t > GATE_ON + gate
    target = 5.0 + 0.1 * (voff - 5.0)
    below = np.where(after & (v <= target))[0]
    trel = t[below[0]] - (GATE_ON + gate) if len(below) else float("nan")
    return tp, voff, trel


def test_gatevca(keep):
    """The VCA's GATE mode: C57's node loaded by R133 into TR28 (pin 1 as a diode drop)."""
    with open(os.path.join(HERE, "env.cir")) as f:
        net = f.read()
    gate, total = 0.02, 0.04
    on = 0.001
    sub = {"{GATE}": "Vgate gate 0 PWL(0 0 %.9g 0 %.9g 12 %.9g 12 %.9g 0)" % (on, on + 1e-6, on + gate, on + gate + 1e-6),
           "{PIN84}": "Vp84 pin84 0 0", "{RATT}": "1e5", "{RDEC}": "1e5", "{RREL}": "1e5",
           "{RSTOP}": "5e4", "{RSBOT}": "5e4",
           "{VCALOAD}": "R133g n83 e28g 12k\nQ28g c28g bus e28g Q2SA1015\nVp1g c28g p1g 0\nDp1g p1g 0 D1S",
           "{ANALYSIS}": ".control\nset wr_singlescale\ntran 2u %g\nwrdata gatevca.txt i(Vp1g) v(n83)\n.endc\n" % total}
    for k, v in sub.items():
        net = net.replace(k, v)
    dd = keep or tempfile.mkdtemp(prefix="sh101env-")
    os.makedirs(dd, exist_ok=True)
    p = os.path.join(dd, "gatevca.cir")
    with open(p, "w") as f:
        f.write(net)
    subprocess.run([NGSPICE, "-b", p], cwd=HERE, capture_output=True, text=True)
    d = np.loadtxt(os.path.join(HERE, "gatevca.txt"))
    os.replace(os.path.join(HERE, "gatevca.txt"), os.path.join(dd, "gatevca.txt"))
    out = subprocess.run([blocks_exe(), "gate-vca", str(gate), str(total)], capture_output=True, text=True,
                         check=True).stdout
    m = np.array([list(map(float, l.split())) for l in out.strip().splitlines()])
    ts, isp = d[:, 0], np.abs(d[:, 1])
    im = np.interp(ts, m[:, 0], m[:, 1])
    def t_to(t, i, frac, after):
        target = frac * i[(t > after + 0.015) & (t < after + 0.019)].mean()
        idx = np.where((t > after) & (i >= target))[0]
        return t[idx[0]] - after if len(idx) else float("nan")
    def t_down(t, i, frac, after, top):
        idx = np.where((t > after) & (i <= frac * top))[0]
        return t[idx[0]] - after if len(idx) else float("nan")
    tops, topm = isp[(ts > on + 0.015) & (ts < on + 0.019)].mean(), im[(ts > on + 0.015) & (ts < on + 0.019)].mean()
    print("GATE mode VCA (I_ABC): steady %.1f / %.1f uA (%+.2f dB) | rise to 90%% %.3f / %.3f ms | "
          "fall to 10%% %.3f / %.3f ms | max err %.1f uA  (SPICE / model)" % (
              tops * 1e6, topm * 1e6, 20 * math.log10(topm / tops),
              1e3 * t_to(ts, isp, 0.9, on), 1e3 * t_to(ts, im, 0.9, on),
              1e3 * t_down(ts, isp, 0.1, on + gate, tops), 1e3 * t_down(ts, im, 0.1, on + gate, topm),
              1e6 * np.abs(im - isp).max()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=",".join(list(SCENARIOS) + ["gatevca"]))
    ap.add_argument("--keep")
    a = ap.parse_args()
    print("ENV: model vs SPICE (transistor-level env.cir)")
    for name in a.only.split(","):
        if name == "gatevca":
            test_gatevca(a.keep)
            continue
        sc = SCENARIOS[name]
        ts, vs, gs = spice(sc, a.keep)
        tm, vm, gm = model(sc)
        vi = np.interp(ts, tm, vm)
        gi = np.interp(ts, tm, gm)
        rms = np.sqrt(np.mean((vi - vs) ** 2))
        mx = np.abs(vi - vs).max()
        ms, mm = metrics(ts, vs, sc[5]), metrics(tm, vm, sc[5])
        print("  %-10s ENV err rms %6.1f mV max %6.0f mV | peak at %7.4f / %7.4f s | at gate-off "
              "%6.3f / %6.3f V | release to 10%% %7.4f / %7.4f s | GATE level err max %4.0f mV" % (
                  name, 1e3 * rms, 1e3 * mx, ms[0], mm[0], ms[1], mm[1], ms[2], mm[2],
                  1e3 * np.abs(gi - gs).max()))
    print("  (SPICE / model)")


if __name__ == "__main__":
    main()
