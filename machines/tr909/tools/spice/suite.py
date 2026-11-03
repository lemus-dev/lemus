#!/usr/bin/env python3
"""Every TR-909 netlist against ngspice: runs compare_circuit.py on a set of cases (each voice
with a normal and an accented trigger, knob extremes; the shared blocks and the mixer; the cases
are in tools/spice/cases/) and summarises the difference per output, in % of the output's range
(docs/model.md, "SPICE").

    suite.py [--out DIR] [--rate HZ] [--only NAME,...] [--jobs N] [--no-substeps] [--extra "ARGS"]

The trigger edges get 8 substeps, the first 8 quarter steps by backward Euler, as the board does
(--no-substeps leaves them out).
Each case keeps its decks, data and a plot under DIR/<case>/ (default build/tr909/spice).
"""
import argparse
import concurrent.futures as cf
import json
import math
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "../../../.."))
NET = os.path.join(REPO, "machines/tr909/core/netlists")
CMP = os.path.join(HERE, "compare_circuit.py")
PY = sys.executable

E = 1e-7  # edge time: a trigger edge takes well under a microsecond (Circuit::jump)
T = 0.01  # trigger time


def pulse(v, t=T, w=1e-3, base=0.0):
    return [(0, base), (t, base), (t + E, v), (t + w, v), (t + w + E, base)]


def pulses(specs, base=0.0):
    pts = [(0, base)]
    for t, v in specs:
        pts += [(t, base), (t + E, v), (t + 1e-3, v), (t + 1e-3 + E, base)]
    return pts


def hold(v):
    return [(0, v)]


def sines(parts, seconds, step):
    n = int(round(seconds / step)) + 1
    t = np.arange(n) * step
    x = sum(a * np.sin(2 * math.pi * f * t + ph) for f, a, ph in parts)
    return list(zip(t, x))


def square(f, seconds, v=4.7, edge=5e-6):
    pts = [(0, 0.0)]
    per = 1 / f
    k = 1
    while k * per / 2 < seconds:
        t = k * per / 2
        lo, hi = (0.0, v) if k % 2 else (v, 0.0)
        pts += [(t, lo), (t + edge, hi)]
        k += 1
    return pts


def triangle(half_samples, amp, seconds, rate=96000):
    h = half_samples / rate
    n = int(seconds / h) + 2
    return [(0, 0.0)] + [((2 * k + 1) * h / 2, amp if k % 2 == 0 else -amp) for k in range(n)]


def fmt(pts):
    return ",".join(f"{t:.9g},{v:.6g}" for t, v in pts)


def cases():
    """Every case of every block: tools/spice/cases/<block>.py, each with cases(add, h), where
    add(name, files, seconds, inputs, controls=None, t0=0.0, ttrig=T) adds one (files: the netlist's
    name without .cir, or several joined with ","; models.cir comes first) and h is this module
    (pulse, pulses, hold, sines, square, triangle, E, T)."""
    import importlib.util
    C = []

    def add(name, files, seconds, inputs, controls=None, t0=0.0, ttrig=T):
        C.append(dict(name=name, files=files, seconds=seconds, inputs=inputs, controls=controls or {}, t0=t0, ttrig=ttrig))

    here = sys.modules[__name__]
    d = os.path.join(HERE, "cases")
    for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if f.endswith(".py") and not f.startswith("_"):
            spec = importlib.util.spec_from_file_location("cases_" + f[:-3], os.path.join(d, f))
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            mod.cases(add, here)
    return C


def load(work):
    d = np.genfromtxt(os.path.join(work, "ours.csv"), delimiter=",", names=True)
    names = d.dtype.names[1:]
    raw = np.loadtxt(os.path.join(work, "spice.txt"), skiprows=1)
    return d["time"], {n: d[n] for n in names}, raw[:, 0], {n: raw[:, k + 1] for k, n in enumerate(names)}


def analyse(work, t0, ttrig):
    t, y, ts, ys = load(work)
    res = {}
    w = t >= t0
    for n, v in y.items():
        s = np.interp(t, ts, ys[n])
        d = (v - s)[w]
        sw, vw, tw = s[w], v[w], t[w]
        span = max(np.ptp(sw), 1e-12)
        k = int(np.argmax(np.abs(d)))
        rms = float(np.sqrt(np.mean(d * d)))
        win = {}
        for lab, a, b in (("atk", ttrig, ttrig + 0.005), ("mid", ttrig + 0.005, ttrig + 0.05), ("tail", ttrig + 0.05, 1e9)):
            m = (tw >= a) & (tw < b)
            if m.any():
                win[lab] = float(np.sqrt(np.mean(d[m] ** 2)) / span)
        res[n] = dict(range=float(span), peak_ours=float(np.max(np.abs(vw))), peak_spice=float(np.max(np.abs(sw))),
                      maxd=float(np.max(np.abs(d))), t_maxd=float(tw[k]), rms=rms, rms_rel=rms / span,
                      max_rel=float(np.max(np.abs(d)) / span), sig_rms=float(np.sqrt(np.mean(sw * sw))), win=win)
    return res


def run_case(c, rate, out, plot=True, extra=()):
    work = os.path.join(out, c["name"])
    os.makedirs(work, exist_ok=True)
    args = [PY, CMP, ",".join(["models.cir"] + [f + ".cir" for f in c["files"].split(",")]), "--rate", str(rate), "--seconds", str(c["seconds"])]
    for k, v in c["inputs"].items():
        args += ["--input", f"{k}={fmt(v)}"]
    for k, v in c["controls"].items():
        args += ["--control", f"{k}={v}"]
    if any(f in ("hh", "cr", "rd") for f in c["files"].split(",")) and not os.environ.get("TR909_SUITE_POLE_BE"):
        args += ["--pole-trap"]  # as the voicing board runs the sample voices (Circuit::setPoleTrap)
    args += list(extra)
    args += ["--keep", work]
    if plot:
        args += ["--plot", os.path.join(work, "plot.png")]
    r = subprocess.run(args, cwd=NET, capture_output=True, text=True)
    with open(os.path.join(work, "compare.txt"), "w") as f:
        f.write(r.stdout + r.stderr)
    if not os.path.exists(os.path.join(work, "spice.txt")):
        return c["name"], dict(error=(r.stdout + r.stderr)[-2000:])
    out = dict(stdout=r.stdout, stderr=r.stderr, res=analyse(work, c["t0"], c["ttrig"]))
    with open(os.path.join(work, "result.json"), "w") as f:
        json.dump(out, f, indent=1)
    return c["name"], out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "build", "tr909", "spice"))
    ap.add_argument("--rate", type=float, default=96000)
    ap.add_argument("--only")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--no-substeps", action="store_true")
    ap.add_argument("--extra", default="")
    a = ap.parse_args()
    # the board's edges are its trigger and data lines', not the metal squares' or the audio's
    edges = ["--edge-substeps", "8", "--edge-be", "8", "--edge-inputs", "trig,acc,ohsel"]
    extra = ([] if a.no_substeps else edges) + a.extra.split()
    cs = cases()
    if a.only:
        keep = a.only.split(",")
        cs = [c for c in cs if any(c["name"] == k or c["name"].startswith(k + "_") or c["files"] == k for k in keep)]
    results = {}
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        futs = [ex.submit(run_case, c, a.rate, a.out, True, extra) for c in cs]
        for fu in cf.as_completed(futs):
            name, out = fu.result()
            results[name] = out
            if "error" in out:
                print(f"{name}: ERROR {out['error']}", flush=True)
    print(f"\n{'case':20s} {'out':5s} {'peak ours/spice':>16s} {'range':>7s} {'max %':>7s} {'rms %':>7s}")
    for c in cs:
        out = results.get(c["name"], {})
        for n, r in out.get("res", {}).items():
            if r["range"] < 1e-3:
                continue  # an output that isn't sounding in this case
            print(f"{c['name']:20s} {n:5s} {r['peak_ours']:7.3f}/{r['peak_spice']:7.3f} {r['range']:7.3f}"
                  f" {100 * r['max_rel']:7.2f} {100 * r['rms_rel']:7.3f}")


if __name__ == "__main__":
    main()
