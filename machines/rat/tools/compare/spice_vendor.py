#!/usr/bin/env python3
"""Compare our model with private/reference/spice/rat2.cir: the same circuit with the vendors' device
models (LTC's LM308 macromodel, LTspice's 1N4148 and 2N5458). Unlike spice_check.py, which
checks the solver on identical equations, this measures what our compact device models
(op amp macromodel, diode pair, Shichman-Hodges JFET) get wrong against the vendor models.

Each case drives both with the same signal at 384 kHz and reports the error at the op amp
output and at the output jack, plus slew rate and clip levels.

Usage: spice_vendor.py [--opamp LM308_LT|LM308|OP07] [--cal datasheet] [--set NAME=VALUE ...]
"""
import argparse
import concurrent.futures as cf
import os
import pathlib
import re
import subprocess
import tempfile

import numpy as np
import soundfile as sf

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPICE = ROOT / "private/reference/spice"
RATRENDER = pathlib.Path(os.environ.get("RATRENDER", ROOT.parents[1] / "build/rat/ratrender"))  # RATRENDER: another build
RATE = 384000.0
SETTLE = 0.03

CASES = [
    # name, signal, amplitude V, dist, filter, volume, seconds
    ("small-d05", "sine1k", 0.02, 0.5, 0.5, 0.5, 0.04),
    ("mid-d05-f03", "sine220", 0.2, 0.5, 0.3, 0.7, 0.05),
    ("hot-d1-f0", "sine1k", 0.5, 1.0, 0.0, 0.5, 0.04),
    ("slew-d1-5k", "sine5k", 0.3, 1.0, 0.0, 0.5, 0.02),
    ("burst-d1", "burst", 0.5, 1.0, 0.2, 0.5, 0.06),
    ("sweep-d07", "sweep", 0.1, 0.7, 0.2, 0.5, 0.12),
]


def signal(kind, amp, dur):
    t = np.arange(int(dur * RATE)) / RATE
    if kind.startswith("sine"):
        f = float(kind[4:].replace("k", "000"))
        x = amp * np.sin(2 * np.pi * f * t)
    elif kind == "sweep":
        f0, f1 = 50.0, 12000.0
        k = np.log(f1 / f0) / dur
        x = amp * np.sin(2 * np.pi * f0 * (np.exp(k * t) - 1) / k)
    elif kind == "burst":
        x = amp * ((t % 0.02) < 0.01) * np.sin(2 * np.pi * 2000 * t)
    x = x * np.minimum(1, t / 0.002)
    return np.concatenate([np.zeros(int(SETTLE * RATE)), x])


def vendor_netlist(pwl, dist, filt, vol, opamp, out):
    net = (SPICE / "rat2.cir").read_text()
    net = re.sub(r"(?m)^VIN .*$", f"""A_in %vd([vsrc 0]) insrc
.model insrc filesource (file="{pwl}" amploffset=[0] amplscale=[1] timeoffset=0 timescale=1 timerelative=false amplstep=false)""", net)
    net = re.sub(r"(?m)^\.param dist=.*$", f".param dist={dist} filter={filt} vol={vol}", net)
    if opamp != "LM308_LT":
        line = "XU1 inp inn v9 0 opout cc1 cc8 LM308" if opamp == "LM308" else "XU1 inp inn v9 0 opout OP07"
        net = re.sub(r"(?m)^XU1 .*$", line, net)
    net = re.sub(r"(?m)^\.tran .*$", "", net)
    ctl = net.index(".control")
    end = net.index(".endc")
    step = 1 / RATE
    dur = (len(open(pwl).readlines())) / RATE
    net = net[:ctl] + f""".control
tran {step:.9e} {dur:.9e} 0 {step:.9e}
linearize v(out) v(opout) v(clip)
wrdata {out} v(out) v(opout) v(clip)
.endc
""" + net[end + len('.endc'):]
    return net


def run(case, opamp, cal, sets):
    name, kind, amp, dist, filt, vol, dur = case
    x = signal(kind, amp, dur)
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        sf.write(td / "in.wav", x.astype(np.float32), int(RATE), subtype="FLOAT")
        x32, _ = sf.read(td / "in.wav")
        pwl = td / "in.pwl"
        with open(pwl, "w") as f:
            for i, v in enumerate(x32):
                f.write(f"{i / RATE:.9e} {v:.9e}\n")
        res = {}
        knobs = ["--dist", str(dist), "--filter", str(filt), "--volume", str(vol), "--cal", cal,
                 "--opamp", "op07" if opamp == "OP07" else "lm308"]
        for k, v in sets:
            knobs += ["--set", f"{k}={v}"]
        for probe in ("", "opout", "clip"):
            o = td / f"ours{probe}.wav"
            cmd = [str(RATRENDER), str(td / "in.wav"), str(o), "--direct", *knobs]
            if probe:
                cmd += ["--probe", probe]
            subprocess.run(cmd, check=True)
            res[probe or "out"], _ = sf.read(o)
        (td / "v.cir").write_text(vendor_netlist(pwl, dist, filt, vol, opamp, td / "v.txt"))
        r = subprocess.run(["ngspice", "-b", str(td / "v.cir")], cwd=SPICE, capture_output=True, text=True)
        if not (td / "v.txt").exists():
            return name, None, r.stdout[-1500:]
        d = np.loadtxt(td / "v.txt")
    t = np.arange(len(res["out"])) / RATE
    vend = {k: np.interp(t, d[:, 0], d[:, c]) for k, c in (("out", 1), ("opout", 3), ("clip", 5))}
    a = int((SETTLE + 0.005) * RATE)
    rms = lambda v: float(np.sqrt(np.mean(v * v)) + 1e-30)
    out = {}
    for k in ("out", "opout"):
        ref = vend[k][a:] - vend[k][a:].mean()
        err = (res[k][a:] - res[k][a:].mean()) - ref
        out[k + "_err_db"] = 20 * np.log10(rms(err) / rms(ref))
    dt = 1 / RATE
    out["slew_ours"] = float(np.max(np.abs(np.diff(res["opout"][a:]))) / dt / 1e6)
    out["slew_vendor"] = float(np.max(np.abs(np.diff(vend["opout"][a:]))) / dt / 1e6)
    out["op_hi"], out["op_lo"] = float(res["opout"][a:].max()), float(res["opout"][a:].min())
    out["v_op_hi"], out["v_op_lo"] = float(vend["opout"][a:].max()), float(vend["opout"][a:].min())
    out["clip_ours"], out["clip_vendor"] = float(res["clip"][a:].max()), float(vend["clip"][a:].max())
    return name, out, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--opamp", default="LM308_LT")
    ap.add_argument("--cal", default="datasheet")
    ap.add_argument("--set", action="append", default=[])
    a = ap.parse_args()
    sets = [(kv.split("=")[0], kv.split("=")[1]) for kv in a.set]
    with cf.ProcessPoolExecutor() as ex:
        results = list(ex.map(run, CASES, [a.opamp] * len(CASES), [a.cal] * len(CASES), [sets] * len(CASES)))
    print(f"{'case':14s} {'out err':>8s} {'op err':>7s}  {'slew ours/vendor V/us':>21s}  {'op swing ours':>15s}  {'vendor':>13s}  {'clip ours/vendor':>16s}")
    for name, r, err in results:
        if r is None:
            print(name, "ngspice failed:", err)
            continue
        print(f"{name:14s} {r['out_err_db']:7.1f}  {r['opout_err_db']:6.1f}  {r['slew_ours']:9.3f} / {r['slew_vendor']:.3f}  "
              f"{r['op_lo']:6.2f}..{r['op_hi']:5.2f}  {r['v_op_lo']:5.2f}..{r['v_op_hi']:5.2f}  {r['clip_ours']:7.3f} / {r['clip_vendor']:.3f}")


if __name__ == "__main__":
    main()
