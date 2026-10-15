#!/usr/bin/env python3
"""Compare the voice with a real TB-303: Din Sync's reference recordings (Feb 2010), 400 single
low Cs at documented knob positions (private/reference/dinsync/index.md).

Each recording SET-<A-E><1-5>.wav sweeps one knob (1 cut off, 2 resonance, 3 env mod, 4 decay,
5 accent) through 25/50/75/100 % with the other four at 0 % (set A) or 25/50/75/100 % (B to E).
Each position has four notes, 3 s apart: plain saw, accented saw, plain square, accented square.
TUNING and VOLUME aren't stated: TUNING is left at the centre (the real unit plays C2 + 5 cents)
and levels are compared after one gain offset per render set, so VOLUME doesn't matter.

  dinsync.py specs              writes specs/dinsync.json: one spec per position and waveform (a
                                plain note, then an accented one, as recorded), duplicates removed
  dinsync.py real               cuts the recordings into private/reference/dinsync/real/<spec>.wav,
                                laid out as the specs (first take), for compare.py
  dinsync.py render [--roland] [--force]
                                renders the specs with tb303render, nominal and as the plugin's
                                unit (renders/service, renders/unit), and with --roland through
                                Roland's plugin (renders/roland; drives Onyx Tracker, ~10 min).
                                Sets already rendered are kept unless --force (rebuild first).
  dinsync.py compare [--out DIR]
                                every note of every take against each render set: a table per
                                knob sweep, sweeps-<metric>.png and summary.json

Per note, render against real: level (gate RMS, after the set's offset), brightness (spectral
centroid, 0-0.1 s and 0.6-1.2 s, cents), resonant peak (the harmonic the resonance lifts most,
whitened for the saw's 1/k, over the frames where both show one, cents), envelope shape (RMS
envelope after per-note gain, dB) and LSD (log-spectral distance, dB). "retake" is the real unit
against its own first take of the same setting: the floor for all of these.

compare.py works on the same files for one spec at a time:
  compare.py private/reference/dinsync/real private/reference/dinsync/renders/service \\
      --suite specs/dinsync.json --only ds-c050-r050-e050-d050-a050
Run with the venv python (numpy, scipy, soundfile, matplotlib).
"""
import argparse, json, os, subprocess, sys
import numpy as np
import soundfile as sf
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../../../.."))
DIN = os.path.normpath(os.path.join(HERE, "../../private/reference/dinsync"))
SPECS = os.path.join(HERE, "specs", "dinsync.json")
TB303RENDER = os.path.join(ROOT, "build/cmake/tb303render_artefacts/Release/tb303render")
SR = 44100

KNOBS = ["cutoff", "resonance", "envmod", "decay", "accent"]
SHORT = ["c", "r", "e", "d", "a"]
LEVELS = [0.25, 0.5, 0.75, 1.0]
BASES = {"A": 0.0, "B": 0.25, "C": 0.5, "D": 0.75, "E": 1.0}

# Recording layout (measured: note 1 at 1.140 s, gate 1.325 s, C2 + 5 cents).
REAL_FIRST, REAL_STEP = 1.14, 3.0
# Spec layout: 119 BPM at 8 lines per beat puts the gate at 21 lines (1.3235 s).
BPM, LPB = 119, 8
LINE = 60.0 / BPM / LPB
FIRST, SECOND, GATE, LINES = 4, 52, 21, 100

# Each render set and how it is made: the schematic's nominal parts with the service notes'
# trims, the unit Lemus plays (tb303::unitCalibration, fitted to these recordings) and Roland's plugin.
RENDERS = {
    "service": [],
    "unit": ["--unit"],
    "roland": None,  # Roland's plugin via roland_render.py
}


def knob_values(set_name, pos):
    """Pot positions (0-1) of the five knobs for set e.g. "B3", position 1-4."""
    group, knob = set_name[0], int(set_name[1]) - 1
    v = [BASES[group]] * 5
    v[knob] = LEVELS[pos - 1]
    return v


def spec_name(v, square):
    return "ds-" + "-".join(f"{s}{round(x * 100):03d}" for s, x in zip(SHORT, v)) + ("-sqr" if square else "-saw")


def takes():
    """Every recorded note: (spec name, accented, set, note index 0-15)."""
    out = []
    for group in "ABCDE":
        for k in range(1, 6):
            s = f"{group}{k}"
            for pos in range(1, 5):
                v = knob_values(s, pos)
                for w, square in enumerate((False, True)):
                    for acc in (False, True):
                        out.append((spec_name(v, square), acc, s, 4 * (pos - 1) + 2 * w + int(acc)))
    return out


def cmd_specs():
    specs, seen = [], set()
    for group in "ABCDE":
        for k in range(1, 6):
            for pos in range(1, 5):
                v = knob_values(f"{group}{k}", pos)
                for square in (False, True):
                    name = spec_name(v, square)
                    if name in seen:
                        continue
                    seen.add(name)
                    params = {"waveform": int(square), "tuning": 0.5, "volume": 0.6}
                    params.update(dict(zip(KNOBS, v)))
                    specs.append({"name": name, "bpm": BPM, "lpb": LPB, "params": params, "lines": LINES,
                                  "notes": [{"line": FIRST, "note": "C-2", "len": GATE},
                                            {"line": SECOND, "note": "C-2", "len": GATE, "accent": True}]})
    with open(SPECS, "w") as f:
        f.write("[\n" + ",\n".join(json.dumps(s) for s in specs) + "\n]\n")
    print(f"{SPECS}: {len(specs)} specs")


def load(path):
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    if sr != SR:
        x = signal.resample_poly(x, SR, sr)
    return x


def onset_near(x, t, before=0.05, after=0.3):
    """Onset of the note expected near t (s): where the 2 ms RMS first passes -30 dB of the
    note's peak."""
    a, b = max(0, int((t - before) * SR)), min(len(x), int((t + after + 0.5) * SR))
    env = np.sqrt(np.convolve(x[a:b] ** 2, np.ones(88) / 88, "same"))
    i = int(np.argmax(env > env.max() * 10 ** (-30 / 20)))
    return (a + i) / SR


_real_cache = {}


def real_set(s):
    if s not in _real_cache:
        x = load(os.path.join(DIN, "wav", f"SET-{s}.wav"))
        _real_cache[s] = (x, [onset_near(x, REAL_FIRST + REAL_STEP * k) for k in range(16)])
    return _real_cache[s]


def cmd_real():
    out = os.path.join(DIN, "real")
    os.makedirs(out, exist_ok=True)
    done = set()
    n = int((LINES * LINE + 2.5) * SR)
    for name, acc, s, k in takes():
        if acc or name in done:
            continue
        done.add(name)
        x, on = real_set(s)
        y = np.zeros(n)
        # The plain note at FIRST, the accented one (the next recorded note) SECOND - FIRST lines later.
        for j, at in ((k, FIRST * LINE), (k + 1, SECOND * LINE)):
            a = int((on[j] - 0.2) * SR)
            seg = x[a:a + int(3.0 * SR)]
            b = int((at - 0.2) * SR)
            y[b:b + len(seg)] = seg
        sf.write(os.path.join(out, name + ".wav"), y.astype(np.float32), SR, subtype="FLOAT")
    print(f"{out}: {len(done)} files")


def cmd_render(roland, force):
    for label, extra in RENDERS.items():
        out = os.path.join(DIN, "renders", label)
        os.makedirs(out, exist_ok=True)
        if extra is None and not roland:
            continue
        names = [s["name"] for s in json.load(open(SPECS))]
        if not force and all(os.path.exists(os.path.join(out, n + ".wav")) for n in names):
            print(f"{out}: up to date (--force renders again)")
            continue
        if extra is None:
            subprocess.run([sys.executable, os.path.join(HERE, "roland_render.py"), SPECS, "--out", out], check=True)
        else:
            subprocess.run([TB303RENDER, SPECS, "--out", out, *extra], check=True, stdout=subprocess.DEVNULL)
        print(f"{out}: done", flush=True)


# ---------------------------------------------------------------- per-note features

HOP, WIN, NFFT = 0.005, 0.03, 4096
BAND = (20, 20000)
NOTE_LEN = 2.6  # analysed from the onset; the next note starts 3 s on
GATE_S = GATE * LINE
PEAK_DB = 6  # a resonant peak counts when its harmonic stands this far above the median


def fundamental(seg):
    """f0 of a held low C from a long, zero-padded FFT of the gate."""
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 19))
    f = np.fft.rfftfreq(1 << 19, 1 / SR)
    m = (f > 55) & (f < 78)
    return float(f[m][np.argmax(sp[m])])


def resonant_peak(spec, f0):
    """Per frame: the harmonic the resonance lifts most (Hz) and how far it stands out (dB).
    Harmonic levels are whitened by the saw's 1/k fall-off; the peak is the strongest from the
    fourth harmonic up, among harmonics within 60 dB of the frame's strongest, and it stands out
    by its level over the median of the passband below it (harmonics 2 to half its number)."""
    df = SR / NFFT
    k = np.arange(1, int(BAND[1] / f0))
    centre = np.round(k * f0 / df).astype(int)
    idx = np.clip(centre[:, None] + np.arange(-2, 3)[None, :], 0, spec.shape[1] - 1)
    lev = db(spec[:, idx].max(axis=2))  # frames x harmonics
    w = lev + 20 * np.log10(k)[None, :]
    valid = lev > lev.max(axis=1, keepdims=True) - 60
    w4 = np.where(valid & (k >= 4)[None, :], w, -np.inf)
    best = np.argmax(w4, axis=1)  # index: harmonic best + 1
    stand = np.array([w4[i, b] - np.median(w[i, 1:max(2, (b + 1) // 2)]) for i, b in enumerate(best)])
    return (best + 1) * f0, stand


def note_features(x, t0):
    a = int((t0 - 0.005) * SR)
    seg = x[a:a + int(NOTE_LEN * SR)]
    if len(seg) < int(NOTE_LEN * SR):
        seg = np.concatenate([seg, np.zeros(int(NOTE_LEN * SR) - len(seg))])
    h, w = int(HOP * SR), int(WIN * SR)
    n = (len(seg) - w) // h
    idx = np.arange(w)[None, :] + h * np.arange(n)[:, None]
    fr = seg[idx] * np.hanning(w)[None, :]
    spec = np.abs(np.fft.rfft(fr, NFFT, axis=1))
    freqs = np.fft.rfftfreq(NFFT, 1 / SR)
    rms = np.sqrt(np.mean(fr ** 2, axis=1) * 8 / 3)
    p = spec ** 2
    band = (freqs > BAND[0]) & (freqs < BAND[1])
    cent = (p[:, band] * freqs[band]).sum(1) / np.maximum(p[:, band].sum(1), 1e-30)
    t = (np.arange(n) * h + w / 2) / SR - 0.005
    gate = (t >= 0) & (t < GATE_S)
    level = 10 * np.log10(np.mean(rms[gate] ** 2) + 1e-20)

    def cwin(t1, t2):
        m = (t >= t1) & (t < t2)
        return float(np.sum(cent[m] * rms[m] ** 2) / max(np.sum(rms[m] ** 2), 1e-30))

    f0 = fundamental(seg[int(0.005 * SR):int((0.005 + GATE_S) * SR)])
    peak, stand = resonant_peak(spec, f0)
    peak[(stand < PEAK_DB) | (t < 0) | (t > GATE_S)] = np.nan

    def pwin(t1, t2):
        m = (t >= t1) & (t < t2) & np.isfinite(peak)
        return float(np.exp(np.mean(np.log(peak[m])))) if m.sum() >= 5 else float("nan")

    return dict(t=t, rms=rms, cent=cent, spec=spec[:, band], level=level, f0=f0, peak=peak,
                early=cwin(0.0, 0.1), late=cwin(0.6, 1.2), peak_early=pwin(0.0, 0.1), peak_late=pwin(0.6, 1.2))


def db(v):
    return 20 * np.log10(np.maximum(v, 1e-10))


def pair_metrics(ref, test, gain):
    """test against ref, gain = the render set's level offset (dB) to remove."""
    g = 10 ** (-gain / 20)
    loud = (ref["t"] >= 0) & (ref["t"] < GATE_S + 0.3) & (ref["rms"] > ref["rms"].max() * 10 ** (-40 / 20))
    er, et = db(ref["rms"][loud]), db(test["rms"][loud] * g)
    shape = float(np.sqrt(np.mean((et - er - np.mean(et - er)) ** 2)))
    # Spectra gain-matched per note by RMS (a mean in dB would follow the renders' silent bins).
    lr = db(ref["spec"][loud]); ls = db(test["spec"][loud] * 10 ** (-(test["level"] - ref["level"]) / 20))
    floor = lr.max() - 80
    lsd = float(np.sqrt(np.mean((np.maximum(lr, floor) - np.maximum(ls, floor)) ** 2)))
    both = np.isfinite(ref["peak"]) & np.isfinite(test["peak"])
    peak = float(np.mean(1200 * np.log2(test["peak"][both] / ref["peak"][both]))) if both.sum() >= 5 else float("nan")
    return dict(dlevel=test["level"] - gain - ref["level"], shape=shape, lsd=lsd,
                early=1200 * np.log2(test["early"] / ref["early"]), late=1200 * np.log2(test["late"] / ref["late"]),
                peak=peak, f0=1200 * np.log2(test["f0"] / ref["f0"]))


def cmd_compare(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    specs = {s["name"]: s for s in json.load(open(SPECS))}
    all_takes = takes()

    # Real notes (every take).
    real = {}
    for name, acc, s, k in all_takes:
        x, on = real_set(s)
        real.setdefault((name, acc), []).append(dict(note_features(x, on[k]), set=s, k=k))

    # Rendered notes.
    rendered = {}
    for label in RENDERS:
        d = os.path.join(DIN, "renders", label)
        if not all(os.path.exists(os.path.join(d, n + ".wav")) for n in specs):
            print(f"skipping {label}: renders missing (dinsync.py render{' --roland' if label == 'roland' else ''})")
            continue
        r = {}
        for n in specs:
            x = load(os.path.join(d, n + ".wav"))
            t1 = onset_near(x, FIRST * LINE, before=0.05, after=0.2)
            t2 = onset_near(x, t1 + (SECOND - FIRST) * LINE, before=0.02, after=0.05)
            r[(n, False)], r[(n, True)] = note_features(x, t1), note_features(x, t2)
        rendered[label] = r
        print(f"loaded {label}", flush=True)

    # One level offset per render set: the median over all notes (VOLUME isn't stated).
    gains = {label: float(np.median([r[key]["level"] - real[key][0]["level"] for key in r])) for label, r in rendered.items()}
    rows = []
    for (name, acc), reals in real.items():
        for i, ref in enumerate(reals):
            row = dict(spec=name, accent=acc, set=ref["set"], note=ref["k"] + 1, level=ref["level"],
                       early_hz=ref["early"], late_hz=ref["late"])
            for label, r in rendered.items():
                m = pair_metrics(ref, r[(name, acc)], gains[label])
                row.update({f"{label}.{k}": v for k, v in m.items()})
            if i > 0:  # take-to-take: the real unit against its own first take of the same setting
                m = pair_metrics(reals[0], ref, 0.0)
                row.update({f"retake.{k}": v for k, v in m.items()})
            rows.append(row)

    labels = list(rendered)
    print(f"\nlevel offsets removed (render - real, dB): " + ", ".join(f"{l} {g:+.1f}" for l, g in gains.items()))
    report(rows, labels, real, rendered, gains)
    plots(rows, labels, real, rendered, gains, out_dir)
    json.dump(dict(gains=gains, notes=rows), open(os.path.join(out_dir, "summary.json"), "w"), indent=1)
    print(f"\n{out_dir}: sweeps-*.png, summary.json")


def sweep_rows(rows, s, wave, acc):
    out = []
    for pos in range(1, 5):
        name = spec_name(knob_values(s, pos), wave == "sqr")
        r = [x for x in rows if x["spec"] == name and x["accent"] == acc and x["set"] == s]
        out.append(r[0] if r else None)
    return out


def report(rows, labels, real, rendered, gains):
    metrics = [("dlevel", "level dB"), ("early", "brightness 0-0.1 s, c"), ("late", "brightness 0.6-1.2 s, c"),
               ("peak", "resonant peak, c"), ("lsd", "lsd dB")]
    for wave in ("saw", "sqr"):
        for acc in (False, True):
            print(f"\n=== {wave}, {'accented' if acc else 'plain'}: each knob swept 25/50/75/100 %; "
                  f"render minus real (level after the set's offset; brightness = spectral centroid, cents)")
            head = f"{'set':4s} {'knob':9s} {'others':6s}"
            for _, title in metrics:
                head += f" | {title:^27s}"
            for label in labels:
                print(f"--- {label}")
                print(head)
                for group in "ABCDE":
                    for k in range(1, 6):
                        s = f"{group}{k}"
                        line = f"{s:4s} {KNOBS[k - 1]:9s} {round(BASES[group] * 100):3d} % "
                        sw = sweep_rows(rows, s, wave, acc)
                        for key, _ in metrics:
                            vals = [r.get(f"{label}.{key}") if r else None for r in sw]
                            line += " | " + " ".join("   -  " if v is None or not np.isfinite(v) else
                                                     f"{v:+6.0f}" if key in ("early", "late", "peak") else
                                                     f"{v:+6.1f}" if key == "dlevel" else f"{v:6.1f}" for v in vals)
                        print(line)
    # Accent: the accented note's level over the plain one, per position.
    print("\n=== accent boost (accented minus plain, dB), saw: real / " + " / ".join(labels))
    for group in "ABCDE":
        for k in range(1, 6):
            s = f"{group}{k}"
            cells = []
            for pos in range(1, 5):
                name = spec_name(knob_values(s, pos), False)
                rp = [x for x in rows if x["spec"] == name and not x["accent"] and x["set"] == s][0]
                ra = [x for x in rows if x["spec"] == name and x["accent"] and x["set"] == s][0]
                c = [ra["level"] - rp["level"]]
                for label in labels:
                    c.append(rendered[label][(name, True)]["level"] - rendered[label][(name, False)]["level"])
                cells.append("/".join(f"{v:+.1f}" for v in c))
            print(f"{s:4s} {KNOBS[k - 1]:9s} {round(BASES[group] * 100):3d} %  " + "   ".join(f"{c:>22s}" for c in cells))
    # Overall.
    print("\n=== overall: mean |difference| over the notes (resonant peak: notes where both show one)")
    for label in labels + ["retake"]:
        for wave in ("saw", "sqr"):
            sel = [r for r in rows if r["spec"].endswith(wave) and f"{label}.lsd" in r]
            if not sel:
                continue
            m = lambda k: np.nanmean([abs(r[f"{label}.{k}"]) for r in sel])
            npk = sum(np.isfinite(r[f"{label}.peak"]) for r in sel)
            print(f"{label:11s} {wave}  level {m('dlevel'):4.1f} dB  brightness early {m('early'):4.0f} c, late {m('late'):4.0f} c  "
                  f"res peak {m('peak'):4.0f} c ({npk})  envelope shape {m('shape'):4.1f} dB  lsd {m('lsd'):4.1f} dB  "
                  f"f0 {np.nanmean([r[f'{label}.f0'] for r in sel]):+4.1f} c  ({len(sel)} notes)")


def plots(rows, labels, real, rendered, gains, out_dir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    colours = {"real": "k", "service": "tab:blue", "unit": "tab:green", "roland": "tab:red"}
    x = [25, 50, 75, 100]

    def value(src, name, acc, metric):
        if src == "real":
            vals = [n[metric] for n in real[(name, acc)]]
            return np.nanmean(vals) if np.isfinite(vals).any() else np.nan, (np.nanmin(vals), np.nanmax(vals)) if np.isfinite(vals).any() else None
        f = rendered[src][(name, acc)]
        v = f[metric] - (gains[src] if metric == "level" else 0)
        return v, None

    for metric, title, logy in (("level", "level (dB, gate RMS; renders offset to the real median)", False),
                                ("early", "brightness 0-100 ms (spectral centroid, Hz)", True),
                                ("late", "brightness 0.6-1.2 s (spectral centroid, Hz)", True),
                                ("peak_early", "resonant peak 0-0.1 s (Hz; where it stands out)", True),
                                ("peak_late", "resonant peak 0.6-1.2 s (Hz; where it stands out)", True),
                                ("accent", "accent boost (accented minus plain, dB)", False)):
        fig, axs = plt.subplots(5, 5, figsize=(22, 18), sharex=True)
        for gi, group in enumerate("ABCDE"):
            for k in range(1, 6):
                ax = axs[gi][k - 1]
                s = f"{group}{k}"
                for src in ["real"] + labels:
                    for wave, ls in (("saw", "-"), ("sqr", "--")):
                        ys, spread = [], []
                        for pos in range(1, 5):
                            name = spec_name(knob_values(s, pos), wave == "sqr")
                            if metric == "accent":
                                a, _ = value(src, name, True, "level"); p, _ = value(src, name, False, "level")
                                ys.append(a - p)
                            else:
                                v, sp = value(src, name, False, metric)
                                ys.append(v); spread.append(sp)
                        ax.plot(x, ys, ls, color=colours.get(src, "tab:gray"), marker="o", ms=3, lw=1.2,
                                label=f"{src} {wave}" if gi == 0 and k == 1 else None)
                        bars = [(xi, sp) for xi, sp in zip(x, spread) if sp and sp[0] != sp[1]]
                        if src == "real" and metric != "accent" and bars:
                            ax.vlines([b[0] for b in bars], [b[1][0] for b in bars], [b[1][1] for b in bars], color="k", lw=0.6)
                if logy:
                    ax.set_yscale("log")
                ax.set_title(f"{s}: {KNOBS[k - 1]} (others {round(BASES[group] * 100)} %)", fontsize=9)
                ax.grid(alpha=0.3)
        axs[0][0].legend(fontsize=7)
        fig.suptitle(f"Din Sync real TB-303 vs renders: {title}; x = swept knob %, solid saw, dashed square, plain notes")
        plt.tight_layout(rect=(0, 0, 1, 0.97))
        plt.savefig(os.path.join(out_dir, f"sweeps-{metric}.png"), dpi=55)
        plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["specs", "real", "render", "compare"])
    ap.add_argument("--roland", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--out", default=os.path.join(DIN, "compare"))
    a = ap.parse_args()
    if a.command == "specs":
        cmd_specs()
    elif a.command == "real":
        cmd_real()
    elif a.command == "render":
        cmd_render(a.roland, a.force)
    else:
        cmd_compare(a.out)


if __name__ == "__main__":
    main()
