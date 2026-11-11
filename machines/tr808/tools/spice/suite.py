#!/usr/bin/env python3
"""Every TR-808 netlist against ngspice: runs compare_circuit.py on a set of cases (each voice
with a normal and an accented trigger, knob extremes, the conga/claves/maracas switches; the trig,
accent, noise, metal and mixer blocks) and summarises the difference per output, in % of the
output's range (docs/model.md, "SPICE").

    suite.py [--out DIR] [--rate HZ] [--only NAME,...] [--jobs N] [--no-substeps] [--extra "ARGS"]

The trigger edges get 8 substeps, the first 8 quarter steps by backward Euler, as the board does
(--no-substeps leaves them out).
Each case keeps its decks, data and a plot under DIR/<case>/ (default build/tr808/spice).
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
NET = os.path.join(REPO, "machines/tr808/core/netlists")
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


WN = [(317, 0.09, 0.0), (1123, 0.09, 1.0), (2203, 0.09, 2.0), (3571, 0.09, 3.0)]
PN = [(89, 0.03, 0.0), (197, 0.03, 1.0), (401, 0.03, 2.0)]
METAL = {"o1": 558, "o2": 824, "o3": 486, "o4": 288, "o5": 379, "o6": 199}


def cases():
    C = []

    def add(name, files, seconds, inputs, controls=None, t0=0.0, ttrig=T):
        C.append(dict(name=name, files=files, seconds=seconds, inputs=inputs, controls=controls or {}, t0=t0, ttrig=ttrig))

    N, A = 4.3, 14.2
    NN, NA = 6.8, 13.5
    # BD
    for tag, v in (("n", N), ("a", A)):
        add(f"bd_{tag}", "bd", 1.5, {"trig": pulse(v), "data": hold(5)})
    add("bd_decay1", "bd", 3.0, {"trig": pulse(N), "data": hold(5)}, {"decay": 1})
    add("bd_tone0", "bd", 1.5, {"trig": pulse(N), "data": hold(5)}, {"tone": 0})
    add("bd_tone1_decay0", "bd", 1.0, {"trig": pulse(A), "data": hold(5)}, {"tone": 1, "decay": 0})
    # SD
    sec = 0.5
    wn = sines(WN, sec, 3 / 96000)
    add("sd_n_wn0", "sd", sec, {"trig": pulse(N), "data": hold(5), "wn": hold(0)})
    for tag, v in (("n", N), ("a", A)):
        add(f"sd_{tag}", "sd", sec, {"trig": pulse(v), "data": hold(5), "wn": wn})
    add("sd_snappy1_tone1", "sd", sec, {"trig": pulse(N), "data": hold(5), "wn": wn}, {"snappy": 1, "tone": 1})
    add("sd_snappy0_tone0", "sd", sec, {"trig": pulse(N), "data": hold(5), "wn": wn}, {"snappy": 0, "tone": 0})
    # toms / congas
    for v_ in ("lt", "mt", "ht"):
        sec = 0.8
        pn = sines(PN, sec, 6 / 96000)
        add(f"{v_}_n_pn0", v_, sec, {"trig": pulse(N), "data": hold(5), "pn": hold(0)})
        for tag, v in (("n", N), ("a", A)):
            add(f"{v_}_{tag}", v_, sec, {"trig": pulse(v), "data": hold(5), "pn": pn})
        add(f"{v_}_conga_n", v_, 0.5, {"trig": pulse(N), "data": hold(5), "pn": pn[: len(pn) * 5 // 8 + 1]}, {"conga": 1})
        add(f"{v_}_conga_a", v_, 0.5, {"trig": pulse(A), "data": hold(5), "pn": pn[: len(pn) * 5 // 8 + 1]}, {"conga": 1})
        add(f"{v_}_tune0_a", v_, sec, {"trig": pulse(A), "data": hold(5), "pn": pn}, {"tuning": 0})
        add(f"{v_}_tune1_a", v_, sec, {"trig": pulse(A), "data": hold(5), "pn": pn}, {"tuning": 1})
    # RS / CL
    for tag, v in (("n", N), ("a", A)):
        add(f"rs_{tag}", "rs", 0.3, {"trig": pulse(v), "data": hold(5)})
        add(f"cl_{tag}", "rs", 0.3, {"trig": pulse(v), "data": hold(5)}, {"claves": 1})
    # CP / MA
    sec = 0.5
    wn = sines(WN, sec, 3 / 96000)
    add("cp_n_wn0", "cp", sec, {"trig": pulse(N), "data": hold(5), "wn": hold(0)})
    for tag, v in (("n", N), ("a", A)):
        add(f"cp_{tag}", "cp", sec, {"trig": pulse(v), "data": hold(5), "wn": wn})
    add("cp_off0_a", "cp", sec, {"trig": pulse(A), "data": hold(5), "wn": wn}, {"offset": 0})
    add("cp_off1_a", "cp", sec, {"trig": pulse(A), "data": hold(5), "wn": wn}, {"offset": 1})
    sec = 0.3
    wn3 = sines(WN, sec, 3 / 96000)
    for tag, v in (("n", N), ("a", A)):
        add(f"ma_{tag}", "ma", sec, {"trig": pulse(v), "data": hold(5), "wn": wn3})
    # CB: squares from 0, trigger at 0.6 s, compare from 0.59 s
    sec = 1.0
    sq = {k: square(METAL[k], sec) for k in ("o1", "o2")}
    for tag, v in (("n", NN), ("a", NA)):
        add(f"cb_{tag}", "cb", sec, {"ntrig": pulse(v, t=0.6), "data": hold(5), **sq}, t0=0.59, ttrig=0.6)
    add("cb_sq0_n", "cb", sec, {"ntrig": pulse(NN, t=0.6), "data": hold(5), "o1": hold(0), "o2": hold(0)}, t0=0.59, ttrig=0.6)
    # CY: hi/lo triangles locked to the 96 kHz grid
    for sec, tag, v, ctl in ((1.5, "n", NN, {}), (1.5, "a", NA, {}), (2.0, "decay1_tone0", NA, {"decay": 1, "tone": 0}),
                             (1.0, "decay0_tone1", NA, {"decay": 0, "tone": 1})):
        hi = triangle(7, 0.15, sec)
        lo = triangle(14, 0.15, sec)
        add(f"cy_{tag}", "cy", sec, {"ntrig": pulse(v), "data": hold(5), "hi": hi, "lo": lo}, ctl)
    add("cy_a_short", "cy", 0.3, {"ntrig": pulse(NA), "data": hold(5), "hi": triangle(7, 0.15, 0.3), "lo": triangle(14, 0.15, 0.3)})
    add("cy_n_metal0", "cy", 1.5, {"ntrig": pulse(NN), "data": hold(5), "hi": hold(0), "lo": hold(0)})
    # hats
    sec = 0.8
    hi = triangle(7, 0.15, sec)
    for tag, v in (("n", NN), ("a", NA)):
        add(f"oh_{tag}", "hats", sec, {"ntrig": pulse(v), "dataoh": hold(5), "datach": hold(0), "hi": hi})
        add(f"ch_{tag}", "hats", 0.4, {"ntrig": pulse(v), "dataoh": hold(0), "datach": hold(5), "hi": hi[: len(hi) // 2]})
    add("oh_decay1_a", "hats", 1.5, {"ntrig": pulse(NA), "dataoh": hold(5), "datach": hold(0), "hi": triangle(7, 0.15, 1.5)},
        {"decay": 1})
    add("oh_choke", "hats", sec, {"ntrig": pulses([(T, NN), (0.1, NN)]), "dataoh": [(0, 5), (0.05, 5), (0.05001, 0)],
                                  "datach": [(0, 0), (0.05, 0), (0.05001, 5)], "hi": hi})
    add("hats_n_hi0", "hats", sec, {"ntrig": pulse(NN), "dataoh": hold(5), "datach": hold(0), "hi": hold(0)})
    # accent, mtrig, metal, noise, mix
    for acc in (0, 5):
        for a in (0, 0.5, 1):
            add(f"accent_acc{acc}_{a}", "accent", 0.05, {"mono": pulse(0, base=15), "acc": hold(acc)}, {"accent": a})
    for tag, v in (("n", N), ("a", A)):
        add(f"mtrig_{tag}", "mtrig", 0.03, {"trig": pulse(v)})
    add("metal", "metal", 0.2, {k: square(f, 0.2) for k, f in METAL.items()})
    nz = sines([(53, 0.002, 0.0), (1777, 0.002, 1.0), (4111, 0.002, 2.0)], 0.3, 3 / 96000)
    add("noise", "noise", 0.3, {"nz": nz})
    add("noise_tm4_1", "noise", 0.3, {"nz": nz}, {"tm4": 1})
    mixin = sines([(55, 1.0, 0.0)], 0.3, 6 / 96000)
    mixin2 = sines([(1200, 0.5, 0.0), (3300, 0.3, 1.0)], 0.3, 3 / 96000)
    add("mix", "mix", 0.3, {"bd": mixin, "cy": mixin2, "mute": [(0, -15.0)]})  # Q13 off, as when settled
    add("mix_poweron", "mix", 0.3, {"bd": mixin, "cy": mixin2, "mute": [(0, 0.0), (0.15, 0.0), (0.15 + E, -15.0)]})
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


# The netlists whose op-amps' poles the voicing board integrates by the trapezoidal rule
# (VoiceBoard, Circuit::setPoleTrap).
POLE_TRAP = {"rs"}


def run_case(c, rate, out, plot=True, extra=()):
    work = os.path.join(out, c["name"])
    os.makedirs(work, exist_ok=True)
    args = [PY, CMP, f"models.cir,{c['files']}.cir", "--rate", str(rate), "--seconds", str(c["seconds"])]
    for k, v in c["inputs"].items():
        args += ["--input", f"{k}={fmt(v)}"]
    for k, v in c["controls"].items():
        args += ["--control", f"{k}={v}"]
    if c["files"] in POLE_TRAP:
        args += ["--pole-trap"]
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
    ap.add_argument("--out", default=os.path.join(REPO, "build", "tr808", "spice"))
    ap.add_argument("--rate", type=float, default=96000)
    ap.add_argument("--only")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--no-substeps", action="store_true")
    ap.add_argument("--extra", default="")
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)  # compare_circuit.py runs in the netlists' folder
    # the board's edges are its trigger and data lines', not the metal squares' or the audio's
    edges = ["--edge-substeps", "8", "--edge-be", "8", "--edge-inputs", "trig,ntrig,data,datach,dataoh,mono,acc"]
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
