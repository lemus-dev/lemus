#!/usr/bin/env python3
"""The voice model's control circuits against a SPICE simulation of the schematic
(tools/spice/env.cir): gate conditioning, MEG, accent, accent sweep, VEG and the cutoff converter.

Usage (from the machine folder, after building tb303blocks):
    ../../.venv/bin/python tools/spice/compare_env.py [--models datasheet,ds-low,ds-high] [--scenarios a,b,...]

Both sides play the same notes with the same knobs (service-notes calibration's TM3). For each note
it prints, model / SPICE: the note-start delay (gate edge to I_ABC at half its peak), the release
(gate off to I_ABC under 10 %), the I_ABC peak, the MEG peak and its fall to 10 %, the sweep wiper's
rise and the ladder current's peak. Accented notes' I_ABC peak is also given against the plain
note's, which is the accent's level boost."""
import os, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(ROOT, "..", ".."))
BLOCKS = os.path.join(REPO, "build", "cmake", "tb303blocks")
MODELS = {"datasheet": os.path.join(HERE, "models-datasheet.lib"),
          "ds-low": os.path.join(HERE, "models-datasheet-low.lib"),
          "ds-high": os.path.join(HERE, "models-datasheet-high.lib")}

ACCENTS = [(0.05, 0.25, 0), (0.40, 0.55, 1), (0.65, 0.80, 1), (0.90, 1.05, 1), (1.30, 1.50, 0)]
SCENARIOS = {
    # name: (cutoff, resonance, envmod, decay, accent), notes, seconds
    "plain": ((0.5, 0.5, 0.5, 0.5, 0.5), [(0.05, 0.30, 0), (0.45, 0.55, 0), (0.70, 0.71, 0)], 1.0),
    "accent-half": ((0.5, 0.5, 0.5, 0.5, 0.5), ACCENTS, 2.0),
    "accent-full": ((0.5, 1.0, 0.5, 0.5, 1.0), ACCENTS, 2.0),
    "accent-full-res0": ((0.5, 0.0, 0.5, 0.5, 1.0), ACCENTS, 2.0),
    "decay-min": ((0.5, 0.5, 0.5, 0.0, 0.5), [(0.05, 2.5, 0)], 3.0),
    "decay-max": ((0.5, 0.5, 0.5, 1.0, 0.5), [(0.05, 2.5, 0)], 3.0),
    "envmod-full": ((0.0, 0.5, 1.0, 0.5, 0.5), [(0.05, 0.5, 0)], 1.0),
}


def ours(scn, tmp):
    knobs, notes, sec = SCENARIOS[scn]
    out = os.path.join(tmp, "ours.txt")
    r = subprocess.run([BLOCKS, "env", "service", ",".join(map(str, knobs)), ";".join(f"{a},{b},{c}" for a, b, c in notes), str(sec), out],
                       capture_output=True, text=True, check=True)
    params = dict(kv.split("=") for kv in r.stdout.split())
    return np.loadtxt(out), params


def pwl(points):
    return " ".join(f"{t:.6f} {v}" for t, v in points)


def spice(models, scn, params, tmp):
    knobs, notes, sec = SCENARIOS[scn]
    edge = 10e-6
    g, a = [(0, 0)], [(0, 0)]
    for i, (on, off, acc) in enumerate(notes):
        g += [(on, 0), (on + edge, 5.5), (off, 5.5), (off + edge, 0)]
        nxt = notes[i + 1][0] if i + 1 < len(notes) else sec
        lvl = 11.85 if acc else 0
        a += [(on - 0.001, a[-1][1]), (on - 0.001 + edge, lvl), (nxt - 0.001, lvl)]
    a = sorted(set(a))
    deck = os.path.join(tmp, "deck.cir")
    out = os.path.join(tmp, "spice.txt")
    p = " ".join(f"{k}={v}" for k, v in params.items())
    with open(deck, "w") as fh:
        fh.write(f"* compare_env\n.param {p}\n.include {models}\n.include {os.path.join(HERE, 'ba662.lib')}\n"
                 f".include {os.path.join(HERE, 'env.cir')}\n")
        fh.write(f"Vgate ngate 0 PWL({pwl(g)})\nVacc nacc 0 PWL({pwl(a)})\n")
        fh.write(f".control\nset wr_singlescale\noption numdgt=10\ntran 10u {sec} 0 10u\n"
                 f"wrdata {out} v(ngi) v(nmeg) i(vabc) v(nw4b) i(vlad)\nquit\n.endc\n.end\n")
    r = subprocess.run(["ngspice", "-b", deck], capture_output=True, text=True)
    if not os.path.exists(out):
        raise RuntimeError(r.stdout[-3000:])
    d = np.loadtxt(out)
    t = np.arange(0, sec, 1 / 44100)
    return np.column_stack([t] + [np.interp(t, d[:, 0], d[:, k]) for k in range(1, 6)])


def note_metrics(d, on, off, nxt):
    t, gi, meg, iabc, sw, ilad = (d[:, k] for k in range(6))
    w = (t >= on) & (t < nxt)
    tt, ia = t[w], iabc[w]
    pk = ia.max()
    start = tt[np.argmax(ia > 0.5 * pk)] - on if pk > 0 else np.nan
    after = (tt >= off)
    rel = tt[after][np.argmax(ia[after] < 0.1 * pk)] - off if pk > 0 and np.any(after & (ia < 0.1 * pk)) else np.nan
    m = meg[w]
    mpk = m.max()
    i10 = np.argmax((tt > tt[np.argmax(m)]) & (m < 0.1 * mpk))
    mdec = tt[i10] - tt[np.argmax(m)] if i10 > 0 else np.nan
    return {"start": start * 1e3, "release": rel * 1e3, "iabc": pk * 1e6, "meg": mpk, "megdec": mdec * 1e3,
            "sweep": (sw[w].max() - sw[w][0]) * 1e3, "ilad": ilad[w].max() * 1e6}


def main():
    args = sys.argv[1:]
    models = args[args.index("--models") + 1].split(",") if "--models" in args else ["datasheet"]
    scns = args[args.index("--scenarios") + 1].split(",") if "--scenarios" in args else list(SCENARIOS)
    for m in models:
        for scn in scns:
            knobs, notes, sec = SCENARIOS[scn]
            with tempfile.TemporaryDirectory() as tmp:
                o, params = ours(scn, tmp)
                s = spice(MODELS[m], scn, params, tmp)
            print(f"\n{scn} ({os.path.basename(MODELS[m])}): cutoff {knobs[0]} res {knobs[1]} envmod {knobs[2]} decay {knobs[3]} accent {knobs[4]}"
                  f"   [model / SPICE]")
            print(f"  {'note':10} {'start ms':>13} {'release ms':>13} {'I_ABC uA':>15} {'MEG V':>13} {'MEG to 10% ms':>15} "
                  f"{'sweep mV':>13} {'ladder uA':>13}")
            plain = None
            for i, (on, off, acc) in enumerate(notes):
                nxt = notes[i + 1][0] if i + 1 < len(notes) else sec
                a, b = note_metrics(o, on, off, nxt), note_metrics(s, on, off, nxt)
                if not acc and plain is None:
                    plain = (a["iabc"], b["iabc"])
                boost = ""
                if acc and plain:
                    boost = f"  boost {20 * np.log10(a['iabc'] / plain[0]):+5.1f} / {20 * np.log10(b['iabc'] / plain[1]):+5.1f} dB"
                f = lambda k, fmt: f"{format(a[k], fmt):>6}/{format(b[k], fmt):<6}"
                print(f"  {('accent' if acc else 'plain') + f' {on:.2f}':10} {f('start', '.1f')} {f('release', '.1f')} {f('iabc', '.1f'):>15} "
                      f"{f('meg', '.2f')} {f('megdec', '.0f'):>15} {f('sweep', '.0f')} {f('ilad', '.1f')}{boost}")


if __name__ == "__main__":
    main()
