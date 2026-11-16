#!/usr/bin/env python3
"""Compare jack board A's mute (core/jack_board.*, run by sh101jackblocks) with ngspice (mute.cir).

Usage (from machines/sh101): ../../.venv/bin/python tools/spice/jack/compare.py [--only on,clamp,off]
on:    switched on with a 110 Hz sine playing: the OUTPUT jack's peaks over the first 3 s.
clamp: switched on long before: the peaks against the level, where TR7's base-collector path
       limits the negative half.
off:   switched off with a 1 V sine playing (poweroff.cir): the jack over the next 0.3 s.
"""
import argparse, os, subprocess, sys, tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../../../.."))
NGSPICE = "ngspice"
HZ = 110.0
LOAD = 47e3


def blocks_exe():
    # SH101_BUILD names another build folder (relative to the repo, or absolute), tried first.
    for d in ([os.environ["SH101_BUILD"]] if "SH101_BUILD" in os.environ else []) + ["build/sh101", "build/cmake"]:
        p = os.path.join(ROOT, d, "sh101jackblocks")
        if os.path.exists(p):
            return p
    sys.exit("build sh101jackblocks first")


def spice(amp, ton, tsig, tstop, step=10e-6):
    with open(os.path.join(HERE, "mute.cir")) as f:
        s = f.read()
    d = tempfile.mkdtemp(prefix="sh101jack-")
    out = os.path.join(d, "out.txt")
    s = (s.replace("{TON}", repr(ton)).replace("{TON2}", repr(ton + 10e-6)).replace("{TSIG}", repr(tsig))
          .replace("{AMP}", repr(amp)).replace("{HZ}", repr(HZ)).replace("{LOAD}", repr(LOAD))
          .replace("{STEP}", repr(step)).replace("{TSTOP}", repr(tstop)).replace("OUT.txt", out))
    p = os.path.join(d, "mute.cir")
    with open(p, "w") as f:
        f.write(s)
    r = subprocess.run([NGSPICE, "-b", p], cwd=HERE, capture_output=True, text=True)
    if not os.path.exists(out):
        print(r.stdout[-2000:], r.stderr[-2000:])
        sys.exit("ngspice failed")
    return np.loadtxt(out)


def peaks(data, t0):
    seg = data[(data[:, 0] >= t0) & (data[:, 0] < t0 + 1 / HZ)]
    return seg[:, 1].max(), seg[:, 1].min()


def model(mode, amp, times):
    out = subprocess.run([blocks_exe(), "--load", str(LOAD), mode, str(amp), str(HZ)] + [str(t) for t in times],
                         capture_output=True, text=True, check=True).stdout
    return [tuple(float(x) for x in line.split()[1:]) for line in out.strip().splitlines()]


def db(a, b):
    return 20 * np.log10(max(abs(a), 1e-9) / max(abs(b), 1e-9))


def test_on():
    print("Switched on with a 1 V peak sine from IC1a: the jack's peaks (V), model vs SPICE")
    times = [0.001, 0.1, 0.25, 0.5, 0.75, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.75, 2.0, 2.5, 3.0]
    ton = 1e-3
    sp = spice(1.0, ton, ton, ton + 3.0 + 2 / HZ)
    md = model("on", 1.0, times)
    worst = 0
    for t, (mh, ml) in zip(times, md):
        sh, sl = peaks(sp, ton + t)
        # Where the output is open, compare in dB; where shunted, in volts.
        eh, el = mh - sh, ml - sl
        worst = max(worst, abs(eh), abs(el))
        print(f"  t {t:4.2f} s   + {mh:+.4f} / {sh:+.4f}   - {ml:+.4f} / {sl:+.4f}   diff {eh:+.4f} {el:+.4f}")
    print(f"  largest difference {worst * 1e3:.1f} mV")
    return worst


def test_clamp():
    print("Switched on long before: the jack's peaks against the level from IC1a, model vs SPICE")
    worst = 0
    for amp in (0.5, 0.8, 1.0, 1.2, 1.5, 2.0, 3.0):
        ton, tsig = 1e-3, 12.0
        sp = spice(amp, ton, tsig, tsig + 0.2, step=20e-6)
        sh, sl = peaks(sp, tsig + 0.1)
        mh, ml = model("settled", amp, [0.1])[0]
        open_ = amp * LOAD / (2.2e3 + 1e3 + LOAD) # the jack's peak with TR7 off (C2 passes 110 Hz)
        worst = max(worst, abs(db(ml, sl)), abs(db(mh, sh)))
        print(f"  {amp:.1f} V   + {mh:+.4f} / {sh:+.4f}   - {ml:+.4f} / {sl:+.4f}"
              f"   negative half {db(ml, -open_):+.2f} dB (SPICE {db(sl, -open_):+.2f})")
    print(f"  largest difference {worst:.3f} dB")
    return worst


def test_off():
    print("Switched off with a 1 V peak sine from IC1a: the OUTPUT jack (dBFS, 0 dBFS = 2.2 V), model vs SPICE")
    toff, tstop, tau = 0.5, 0.82, 12e-3
    with open(os.path.join(HERE, "poweroff.cir")) as f:
        s = f.read()
    d = tempfile.mkdtemp(prefix="sh101jack-")
    out = os.path.join(d, "out.txt")
    s = (s.replace("{TOFF}", repr(toff)).replace("{TAU}", repr(tau)).replace("{VDIES}", "6.9")
          .replace("{AMP}", "1.0").replace("{HZ}", repr(HZ)).replace("{LOAD}", repr(LOAD))
          .replace("{STEP}", "5e-6").replace("{TSTOP}", repr(tstop)).replace("OUT.txt", out))
    p = os.path.join(d, "poweroff.cir")
    open(p, "w").write(s)
    subprocess.run([NGSPICE, "-b", p], cwd=HERE, capture_output=True, text=True)
    sp = np.loadtxt(out)
    md = np.array([[float(x) for x in line.split()] for line in subprocess.run(
        [blocks_exe(), "--load", str(LOAD), "off", "1.0", str(HZ), str(toff), str(tstop)],
        capture_output=True, text=True, check=True).stdout.strip().splitlines()])
    spj = np.interp(md[:, 0], sp[:, 0], sp[:, 1])
    worst = 0
    for a, b in ((0, 0.002), (0.002, 0.005), (0.005, 0.02), (0.02, 0.1), (0.1, 0.3)):
        m = (md[:, 0] >= toff + a) & (md[:, 0] < toff + b)
        lm = 20 * np.log10(np.abs(md[m, 1]).max() / 2.2)
        ls = 20 * np.log10(np.abs(spj[m]).max() / 2.2)
        err = np.abs(md[m, 1] - spj[m]).max()
        worst = max(worst, err)
        print(f"  {a * 1e3:5.0f}-{b * 1e3:3.0f} ms   peak {lm:6.1f} / {ls:6.1f} dBFS   largest difference {err * 1e3:6.2f} mV")
    print(f"  largest difference {worst * 1e3:.2f} mV")
    return worst


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="on,clamp,off")
    args = ap.parse_args()
    for name in args.only.split(","):
        globals()["test_" + name]()
