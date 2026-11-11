#!/usr/bin/env python3
"""Runs a netlist through circuitrun (the voices' solver) and through ngspice, and compares them.

    compare_circuit.py NETLIST [circuitrun arguments...] [--circuitrun PATH] [--keep DIR] [--plot FILE.png]

The circuitrun arguments (--rate, --seconds, --control, --param, --input) go to both: circuitrun
writes the same netlist as an ngspice deck. Prints, per output, the largest difference and the RMS
difference, in volts and relative to the output's own range.
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "../../../.."))


def load_ours(path):
    data = np.genfromtxt(path, delimiter=",", names=True)
    names = data.dtype.names
    return data[names[0]], {n: data[n] for n in names[1:]}


def load_spice(path, names):
    raw = np.loadtxt(path, skiprows=1)
    t = raw[:, 0]
    return t, {n: raw[:, k + 1] for k, n in enumerate(names)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("netlist")
    ap.add_argument("--circuitrun", default=os.environ.get("CIRCUITRUN", os.path.join(REPO, "build/tr808/circuitrun")))
    ap.add_argument("--keep")
    ap.add_argument("--plot")
    args, rest = ap.parse_known_args()
    work = os.path.abspath(args.keep or tempfile.mkdtemp(prefix="tr808spice"))
    os.makedirs(work, exist_ok=True)
    ours = os.path.join(work, "ours.csv")
    deck = os.path.join(work, "deck.cir")
    data = os.path.join(work, "spice.txt")
    r = subprocess.run([args.circuitrun, args.netlist, *rest, "--out", ours, "--spice", deck, data],
                       capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if r.returncode != 0:
        sys.exit(r.returncode)
    s = subprocess.run(["ngspice", "-b", deck], capture_output=True, text=True, cwd=work)
    if s.returncode != 0 or not os.path.exists(data):
        sys.stderr.write(s.stdout[-3000:] + s.stderr[-3000:])
        sys.exit(1)
    t, y = load_ours(ours)
    ts, ys = load_spice(data, list(y.keys()))
    worst = 0.0
    for name, v in y.items():
        sv = np.interp(t, ts, ys[name])
        d = v - sv
        span = max(np.ptp(sv), 1e-12)
        worst = max(worst, np.max(np.abs(d)) / span)
        print(f"{name:12s} range {span:9.4g} V  max diff {np.max(np.abs(d)):9.3g} V ({100 * np.max(np.abs(d)) / span:6.3f} %)"
              f"  rms {np.sqrt(np.mean(d * d)):9.3g} V  start {v[0]:+.6f} vs {sv[0]:+.6f}")
    if args.plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, axes = plt.subplots(len(y), 1, figsize=(10, 3 * len(y)), squeeze=False)
        for ax, (name, v) in zip(axes[:, 0], y.items()):
            ax.plot(ts, ys[name], label="ngspice", lw=1)
            ax.plot(t, v, "--", label="circuit", lw=1)
            ax.set_title(name)
            ax.legend()
        fig.tight_layout()
        fig.savefig(args.plot, dpi=90)
    return 0 if worst < 0.01 else 1


if __name__ == "__main__":
    sys.exit(main())
