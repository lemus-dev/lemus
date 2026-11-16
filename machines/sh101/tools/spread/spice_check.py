#!/usr/bin/env python3
"""Checks the spread study's closed forms (measures.py) against SPICE of the transcribed circuit, on
the same random units (parts.py):
  - the cutoff CV (tools/spice/filter/cv.cir): pin 9 at FREQ 0 and 10, the ENV-10 step, and the
    KYBD trim's slope (F4 to F5 at KYBD 10 with the unit's VR8);
  - the ENV (tools/spice/filter/env.cir, transistor level): the peak (the threshold) and the
    attack time at ATTACK 10.

Usage (from machines/sh101): ../../.venv/bin/python tools/spread/spice_check.py [--n 30]
"""
import argparse, os, re, subprocess, sys, tempfile
from concurrent.futures import ProcessPoolExecutor

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import parts  # noqa: E402
import measures as M  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SPICE = os.path.join(ROOT, "tools", "spice", "filter")
NGSPICE = "/opt/homebrew/bin/ngspice"


def one(u, i):
    return {k: float(v[i]) for k, v in u.items()}


def set_value(net, name, value):
    """Replaces the value of element `name` (its 4th field) in a netlist."""
    pat = re.compile(r"^(%s\s+\S+\s+\S+\s+)\S+" % re.escape(name), re.M)
    assert pat.search(net), name
    return pat.sub(lambda m: m.group(1) + repr(float(value)), net, count=1)


def cv_net(p, freq, envv, kybd, kcv, r8):
    with open(os.path.join(SPICE, "cv.cir")) as f:
        net = f.read()
    net = net.replace("models.lib", os.path.join(SPICE, "models.lib"))
    net = re.sub(r"^Vbus bus 0 \S+", "Vbus bus 0 %r" % p["Vbus"], net, flags=re.M)
    net = re.sub(r"^V15 v15 0 \S+", "V15 v15 0 %r" % p["V15"], net, flags=re.M)
    for r in ("R186", "R187", "R189", "R191", "R192", "R193", "R195", "R128", "R127", "R126", "R125"):
        net = set_value(net, r, p[r])
    vr8 = p["VR21"] * (1 / r8 - 1)
    sub = {"V14": p["V14"], "VENV": p["Vbus"] + envv, "VMOD": 0.0, "VKCV": kcv, "RVR8": max(vr8, 1e-3),
           "TH1": p["TH1_25"] * (1 + 3.5e-3 * (p["T"] - 25)), "VBEND": 0.0,
           "R17T": max(p["VR17"] * (1 - freq), 1e-3), "R17B": max(p["VR17"] * freq, 1e-3),
           "R19T": 1e-3, "R19B": p["VR19"], "R20T": p["VR20"], "R20B": 1e-3,
           "R21T": max(p["VR21"] * (1 - kybd), 1e-3), "R21B": max(p["VR21"] * kybd, 1e-3),
           "R5T": 50e3, "R5B": 50e3}
    for k, v in sub.items():
        net = net.replace("{%s}" % k, repr(float(v)))
    return net


def run_op(net):
    d = tempfile.mkdtemp(prefix="sh101spread-")
    path = os.path.join(d, "n.cir")
    with open(path, "w") as f:
        f.write(net)
    out = subprocess.run([NGSPICE, "-b", path], cwd=SPICE, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.strip().startswith("v(pin9)-v(bus)"):
            return float(line.split("=")[1])
    raise RuntimeError(out[-1500:])


def cv_case(args):
    p, r8, swing = args
    off = 0.0
    v0 = run_op(cv_net(p, 0.0, off, 0.0, 0.0, r8))
    v10 = run_op(cv_net(p, 1.0, off, 0.0, 0.0, r8))
    venv = run_op(cv_net(p, 0.0, swing, 0.0, 0.0, r8))
    f4 = run_op(cv_net(p, 0.5, off, 1.0, 2.417, r8))
    f5 = run_op(cv_net(p, 0.5, off, 1.0, 3.417, r8))
    return v0, v10, venv - v0, f4 - f5


def env_case(p):
    with open(os.path.join(SPICE, "env.cir")) as f:
        net = f.read()
    net = net.replace("models.lib", os.path.join(SPICE, "models.lib"))
    net = re.sub(r"^Vbus bus 0 \S+", "Vbus bus 0 %r" % p["Vbus"], net, flags=re.M)
    net = re.sub(r"^V15 v15 0 \S+", "V15 v15 0 %r" % p["V15"], net, flags=re.M)
    for r in ("R158", "R141"):
        net = set_value(net, r, p[r])
    net = set_value(net, "C59", p["C59"])
    on, total = 0.01, 7.0
    sub = {"{GATE}": "Vgate gate 0 PWL(0 0 %g 0 %g 12)" % (on, on + 1e-6), "{PIN84}": "Vp84 pin84 0 0",
           "{VCALOAD}": "", "{RATT}": repr(p["VR22"]), "{RDEC}": repr(p["VR23"]), "{RREL}": repr(p["VR25"]),
           "{RSTOP}": "0.001", "{RSBOT}": repr(p["VR24"]),
           "{ANALYSIS}": ".options method=gear reltol=1e-4\n.control\nset wr_singlescale\ntran 2m %g\n"
                         "wrdata out.txt v(env)\n.endc\n" % total}
    for k, v in sub.items():
        net = net.replace(k, v)
    d = tempfile.mkdtemp(prefix="sh101spread-env-")
    net = net.replace("out.txt", os.path.join(d, "out.txt"))
    path = os.path.join(d, "env.cir")
    with open(path, "w") as f:
        f.write(net)
    subprocess.run([NGSPICE, "-b", path], cwd=SPICE, capture_output=True, text=True)
    data = np.loadtxt(os.path.join(d, "out.txt"))
    t, e = data[:, 0], data[:, 1]
    i = int(np.argmax(e))
    return e[i] - p["Vbus"], t[i] - on


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=30)
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()
    u = parts.draw(a.n, a.seed)
    # SPICE runs at 25 C (the netlists' Vt), so the closed forms are evaluated there too.
    u["T"] = np.full(a.n, 25.0)
    m = M.measures(u)
    r8, _, _ = M.kybd_trim(u)
    swing = M.env_swing(u)
    units = [one(u, i) for i in range(a.n)]
    with ProcessPoolExecutor() as ex:
        cv = list(ex.map(cv_case, [(units[i], float(r8[i]), float(swing[i])) for i in range(a.n)]))
        env = list(ex.map(env_case, units))
    v0 = M.pin9(u, freq=0.0); v10 = M.pin9(u, freq=1.0)
    envd = -u["R192"] * swing / u["R189"] * M.divider(u)
    trk = M.pin9(u, freq=0.5, kybd=1.0, kcv=2.417, r8=r8) - M.pin9(u, freq=0.5, kybd=1.0, kcv=3.417, r8=r8)
    sl = M.slope(u)
    e = {"pin 9 at FREQ 0 (mV)": [], "pin 9 at FREQ 10 (mV)": [], "ENV-10 step (cents)": [], "KYBD F4-F5 (cents)": [],
         "ENV peak (mV)": [], "attack at 10 (%)": []}
    for i in range(a.n):
        e["pin 9 at FREQ 0 (mV)"].append(1e3 * (v0[i] - cv[i][0]))
        e["pin 9 at FREQ 10 (mV)"].append(1e3 * (v10[i] - cv[i][1]))
        e["ENV-10 step (cents)"].append(1200 * (envd[i] - cv[i][2]) / sl[i])
        e["KYBD F4-F5 (cents)"].append(1200 * (trk[i] - cv[i][3]) / sl[i])
        e["ENV peak (mV)"].append(1e3 * (swing[i] - env[i][0]))
        e["attack at 10 (%)"].append(100 * (m["Attack at 10 (s)"][i] / env[i][1] - 1))
    print(f"{a.n} units at 25 C: closed form minus SPICE")
    print("| Quantity | Mean | Max abs |")
    print("|---|---|---|")
    for k, v in e.items():
        v = np.array(v)
        print(f"| {k} | {v.mean():+.2f} | {np.abs(v).max():.2f} |")


if __name__ == "__main__":
    main()
