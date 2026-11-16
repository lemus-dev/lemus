#!/usr/bin/env python3
"""The preamp's circuits against ngspice on a workload's own signals: qvgtbench runs the workload
(the hiss off, 50 ms of silence first) and taps every circuit's inputs and outputs at each preamp
step (Preamp::Probe); each circuit's inputs from the first onset on go to ngspice as PWL sources,
with the controls the unit had, and its outputs are compared with what the unit made of them.

    tapcheck.py [WORKLOAD] [--ms 36] [--over 4] [--bench PATH] [--rom PATH] [--keep DIR]

Prints, per output, the RMS and largest difference, over the whole band and under 20 kHz (what the
decimator keeps), and the in-band difference in dB under the output's own RMS. Environment for
qvgtbench passes through (TR909_*, QVGT_*), so other solver settings can be checked against the
same reference; --over runs the preamp at that many steps a DSP sample (QVGT_PREAMP_OVER; the
unit's is core/preamp.h's kPreampOver), ngspice at a 64th of a DSP sample whatever it is. Needs
ngspice, numpy and scipy.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

import numpy as np
from scipy.signal import butter, sosfiltfilt

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "../../../.."))
NETS = os.path.abspath(os.path.join(HERE, "../../core/netlists"))
DSP_RATE = 24e6 / 576
PROBE = ["inL", "inR", "p8", "dryL", "dryR", "i1", "od8", "conout1", "pre", "i2", "out", "send"]
# circuit: its inputs (netlist name: probe), its outputs (netlist name: probe)
CIRCUITS = {
    "input": ({"left": "inL", "right": "inR"}, {"p8": "p8", "dryl": "dryL", "dryr": "dryR"}),
    "overdrive": ({"vca": "i1"}, {"od8": "od8"}),
    "shaper": ({"od": "od8", "conout": "conout1"}, {"pre": "pre", "dist": None}),
    "gateout": ({"vca": "i2"}, {"out": "out", "send": "send"}),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("workload", nargs="?", default="p00-pluck-48k")
    ap.add_argument("--ms", type=float, default=36)
    ap.add_argument("--bench", default=os.path.join(REPO, "build/qvgt/qvgtbench"))
    ap.add_argument("--rom", default=os.path.join(REPO, "../private/roms/qvgt/qvgtv103.bin"))
    ap.add_argument("--circuitrun", default=os.path.join(REPO, "build/qvgt/qvgtcircuitrun"))
    ap.add_argument("--over", type=int, default=4)
    ap.add_argument("--keep")
    args = ap.parse_args()
    RATE = DSP_RATE * args.over
    work = os.path.abspath(args.keep or tempfile.mkdtemp(prefix="qvgttap"))
    os.makedirs(work, exist_ok=True)

    env = dict(os.environ, QVGT_HISS="0", QVGT_PREROLL="0.05", QVGT_TAP=os.path.join(work, "tap.bin"), QVGT_STATS="1",
               QVGT_PREAMP_OVER=str(args.over))
    r = subprocess.run([args.bench, args.rom, "golden", work, args.workload], env=env, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(r.stderr)
    controls = {}
    for line in r.stderr.splitlines():
        m = re.match(r"(\w+) controls:(.*)", line)
        if m:
            controls[m.group(1)] = [kv.split("=") for kv in m.group(2).split()]
    tap = np.fromfile(os.path.join(work, "tap.bin")).reshape(-1, len(PROBE))
    col = {n: k for k, n in enumerate(PROBE)}
    moved = np.flatnonzero(np.abs(tap[:, col["inL"]] - tap[0, col["inL"]]) > 1e-6)
    if moved.size == 0:
        sys.exit("the input never moves")
    s0 = max(0, int(moved[0]) - 300)
    n = int(args.ms * 1e-3 * RATE)
    t = (np.arange(n) + 1) / RATE
    sos = butter(8, 20000, fs=RATE, output="sos")
    print(f"{args.workload}: {n} steps from {s0 / RATE * 1e3:.1f} ms (the onset at {moved[0] / RATE * 1e3:.1f} ms)")
    print(f"{'circuit':10s} {'output':6s} {'rms':>9s} {'max':>9s} {'<20k rms':>9s} {'<20k max':>9s} {'dB under':>9s}")
    for name, (ins, outs) in CIRCUITS.items():
        cmd = [args.circuitrun, f"{NETS}/models.cir,{NETS}/{name}.cir", "--rate", repr(RATE), "--seconds", repr(n / RATE)]
        for k, v in controls.get(name, []):
            cmd += ["--control", f"{k}={v}"]
        for net, probe in ins.items():
            v = tap[s0:s0 + n + 1, col[probe]]
            pts = ["0", f"{v[0]:.10g}"]
            for k in range(n):
                pts += [f"{(k + 1) / RATE:.10g}", f"{v[k + 1]:.10g}"]
            cmd += ["--input", f"{net}=" + ",".join(pts)]
        deck = os.path.join(work, f"{name}.cir")
        data = os.path.join(work, f"{name}.txt")
        cr = subprocess.run(cmd + ["--out", os.path.join(work, f"{name}.csv"), "--spice", deck, data], capture_output=True, text=True)
        if cr.returncode != 0:
            sys.exit(cr.stderr)
        text = open(deck).read()
        text = re.sub(r"^\.tran (\S+) (\S+) 0 (\S+)", lambda m: f".tran {m.group(1)} {m.group(2)} 0 {float(m.group(3)) * args.over / 64:.6g}", text, flags=re.M)
        open(deck, "w").write(text)
        sp = subprocess.run(["ngspice", "-b", deck], capture_output=True, text=True, cwd=work)
        if sp.returncode != 0 or not os.path.exists(data):
            sys.exit(sp.stdout[-2000:] + sp.stderr[-2000:])
        ref = np.loadtxt(data, skiprows=1)
        for j, (net, probe) in enumerate(outs.items()):
            if probe is None:
                continue
            want = np.interp(t, ref[:, 0], ref[:, j + 1])
            got = tap[s0 + 1:s0 + 1 + n, col[probe]]
            e = got - want
            eb = sosfiltfilt(sos, got) - sosfiltfilt(sos, want)
            rms = np.sqrt(np.mean(sosfiltfilt(sos, want) ** 2))
            erms = np.sqrt(np.mean(eb ** 2))
            under = 20 * np.log10(rms / erms) if erms > 0 else float("inf")
            print(f"{name:10s} {net:6s} {np.sqrt(np.mean(e * e)):9.3g} {np.max(np.abs(e)):9.3g} {erms:9.3g} {np.max(np.abs(eb)):9.3g} {under:9.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
