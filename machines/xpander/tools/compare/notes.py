#!/usr/bin/env python3
"""Per-note comparison of the model against the freesound Xpander notes (Modular Samples, CC0).

    notes.py [--out FILE.md] [--work DIR] [--rerender]
    (run with the repo venv: ../../../../.venv/bin/python -I notes.py)

Real units: private/xpander/reference/freesound/<pack>/<id>.48k.wav, single notes MIDI 36-96 (Some
Bass 48-84) at velocity 127, four patches with unknown settings. The MIDI number comes from the
file name in the title (the titles' note names are wrong).

Model: build/xpander/xpanderrender from power-on: RESET MEMORY, the firmware's TUNE VCOS, then the
default patch (two sawtooths at the same pitch, 4-pole low pass) plays MIDI 36-96 at velocity
127, each note held 5 s with 1 s between, four times through: the patch as it is, VCO1 alone
(VCO2 VOL 0), VCO2 alone (VCO1 VOL 0), and VCO1 alone an octave down (FREQ 0), where the real
patches' lowest VCO plays. Voices rotate, so each pass also moves every note to the next voice. A "show" 0.6 s into each note records which voice plays.

Per note, in absolute units:
  pitch   the fundamental from 0.5 to 4.5 s after the onset (harmonics up to 5 kHz, the power
          centroid within 25 cents of each, weighted by power), in cents against equal temperament
          at A4 = 440 Hz. Each real patch has its own reference (its median), and the residual
          against that is what the unit's auto-tune left. The patch octave is found per patch.
  drift   max - min of the pitch in four 1 s windows (vibrato, beating of detuned stacks, drift).
  level   RMS of the same window, dB against the set's median (keyboard tracking of loudness).
  bright  spectral centroid of the same window in harmonics of the fundamental (patch-dependent).
Prints the markdown to stdout, or writes it to --out (keeping any text between <!-- findings -->
and <!-- /findings --> in that file). The render is cached in --work (build/xpander-compare).
"""
import csv
import os
import re
import subprocess
import sys

import numpy as np
import scipy.io.wavfile as wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "../../../.."))  # the lemus clone
ROOT = os.path.dirname(REPO)  # holds build/ and private/
PRIVATE = os.environ.get("LEMUS_PRIVATE_DIR", os.path.join(ROOT, "private"))
FREESOUND = os.path.join(PRIVATE, "xpander/reference/freesound")
ROMS = os.path.join(PRIVATE, "roms/xpander")
RENDER = os.path.join(ROOT, "build/xpander/xpanderrender")

PACKS = [("17452", "Big Stack"), ("17457", "Melodirt"), ("17460", "Nasty"), ("17463", "Some Bass")]
NOTES = list(range(36, 97))
HOLD, GAP, T0 = 5.0, 1.0, 37.0  # model: note length, gap, first note (s from power-on)
WIN = (0.5, 4.5)  # analysis window after the onset (s)
FMAX = 5000.0  # highest harmonic used for pitch


def et(midi):
    return 440.0 * 2 ** ((midi - 69) / 12)


def cents(f, ref):
    return 1200 * np.log2(f / ref)


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(float)
    if x.dtype.kind != "f" or np.abs(x).max() > 2:
        x = x / 32768
    if x.ndim > 1:
        x = x.mean(axis=1)
    return sr, x


def onset(x, sr, start=0, end=None):
    seg = np.abs(x[start:end])
    return start + int(np.argmax(seg > 0.05 * seg.max()))


class Spectrum:
    def __init__(self, x, sr):
        self.sr = sr
        n = 1 << int(np.ceil(np.log2(len(x) * 8)))  # zero-pad 8x
        self.p = np.abs(np.fft.rfft((x - x.mean()) * np.hanning(len(x)), n)) ** 2
        self.f = np.arange(len(self.p)) * sr / n

    def band(self, lo, hi):
        i, j = np.searchsorted(self.f, [lo, hi])
        return self.f[i:j], self.p[i:j]

    def centroid(self, fc, width_cents):
        r = 2 ** (width_cents / 1200)
        f, p = self.band(fc / r, fc * r)
        return (np.sum(f * p) / np.sum(p), np.sum(p)) if p.sum() > 0 else (fc, 0.0)


def odd_power(spec, c):
    """Power within 30 cents of the odd harmonics 1, 3, 5, 7 of c, and the total in the band."""
    odd = sum(spec.centroid(c * h, 30)[1] for h in (1, 3, 5, 7) if c * h < spec.sr / 2)
    f, p = spec.band(c * 0.9, min(c * 8, spec.sr / 2))
    return odd, p.sum()


def octave_of(x, sr, midi):
    """The lowest octave k (f0 = ET * 2^k) whose odd harmonics hold real power."""
    spec = Spectrum(x, sr)
    for k in (-2, -1, 0, 1, 2):
        c = et(midi) * 2.0 ** k
        if c < 15:
            continue
        odd, tot = odd_power(spec, c)
        if tot > 0 and odd / tot > 0.05:
            return k
    return 0


def pitch(x, sr, f_guess):
    """Fundamental: power-weighted mean of (harmonic centroid / h) over harmonics below FMAX."""
    spec = Spectrum(x, sr)
    f0 = f_guess
    for width in (60, 25):
        num = den = 0.0
        for h in range(1, 33):
            if h * f0 > min(FMAX, sr / 2 - 500) and h > 1:
                break
            fc, p = spec.centroid(h * f0, width)
            num += p * fc / h
            den += p
        if den > 0:
            f0 = num / den
    return f0


def analyse(x, sr, i0, midi, octave):
    a, b = i0 + int(WIN[0] * sr), i0 + int(WIN[1] * sr)
    seg = x[a:b]
    guess = et(midi) * 2.0 ** octave
    f0 = pitch(seg, sr, guess)
    sub = [pitch(seg[k * sr:(k + 1) * sr], sr, f0) for k in range(int((b - a) / sr))]
    drift = cents(max(sub), min(sub)) if sub else float("nan")
    level = 10 * np.log10(np.mean(seg ** 2) + 1e-20)
    spec = Spectrum(seg, sr)
    f, p = spec.band(20, 16000)
    bright = np.sum(f * p) / np.sum(p) / f0
    return dict(f0=f0, c=cents(f0, et(midi) * 2.0 ** octave), drift=drift, level=level, bright=bright)


def real_sets():
    sets = []
    for pack, name in PACKS:
        rows = list(csv.DictReader(open(os.path.join(FREESOUND, pack, "sounds.tsv")), delimiter="\t"))
        notes = {}
        for r in rows:
            m = re.search(r"-(\d+)-(\d+)(?:-\w+)?\.aif", r["title"])
            notes[int(m.group(1))] = os.path.join(FREESOUND, pack, r["sound_id"] + ".48k.wav")
        # patch octave: majority over the notes
        ks = []
        for midi, path in sorted(notes.items()):
            sr, x = load(path)
            i0 = onset(x, sr)
            ks.append(octave_of(x[i0 + int(WIN[0] * sr):i0 + int(WIN[1] * sr)], sr, midi))
        octave = max(set(ks), key=ks.count)
        res = {}
        for midi, path in sorted(notes.items()):
            sr, x = load(path)
            res[midi] = analyse(x, sr, onset(x, sr), midi, octave)
        sets.append(dict(name=f"{name} ({pack})", octave=octave, octave_votes=ks.count(octave), n=len(ks), notes=res))
    return sets


# Model passes: name, VCO1 (FREQ, VOL), VCO2 (FREQ, VOL), octave of the result. FREQ is in
# semitones (12 = as played), VOL 0-63; the default patch is 12/63 on both. The last pass puts VCO1
# an octave down, where the four real patches' lowest VCO plays (32.7-1047 Hz).
PASSES = [
    ("model: default patch", (12, 63), (12, 63), 0),
    ("model: VCO1 alone", (12, 63), (12, 0), 0),
    ("model: VCO2 alone", (12, 0), (12, 63), 0),
    ("model: VCO1 alone, FREQ 0", (0, 63), (12, 0), -1),
]
PAGE = {1: "tap:3,6", 2: "tap:3,7"}  # VCO1 / VCO2 page keys
ENC = {"freq": 0, "vol": 5}
DETENT = 0.04  # s between encoder detents; at 10 ms the firmware misses some


def edit(state, want):
    """Steps that take the VCOs' FREQ and VOL from state to want ({1: (f, v), 2: (f, v)}); '+T' waits."""
    steps = []
    for vco in (1, 2):
        if state[vco] == want[vco]:
            continue
        steps += [PAGE[vco], "+0.4"]
        for k, name in enumerate(("freq", "vol")):
            d = want[vco][k] - state[vco][k]
            steps += [f"enc:{ENC[name]},{1 if d > 0 else -1}", f"+{DETENT}"] * abs(d)
        steps += ["+0.3"]
    return steps + [PAGE[2], "+0.4", "show", PAGE[1], "+0.4", "show"]


def model_steps():
    """The render's steps, each note's start and each pass's check time (s from power-on)."""
    steps = ["@1.3", "tap:4,0", "@6", "tap:1,6", "tap:3,0", "@35", "tap:3,6", "@36", "rec"]
    t, starts, checks = T0, [], []
    state = {1: (12, 63), 2: (12, 63)}
    for _, v1, v2, _ in PASSES:
        want = {1: v1, 2: v2}
        ed = edit(state, want)
        state = want
        steps.append(f"@{t:.3f}")
        for st in ed:
            if st[0] == "+":
                t += float(st[1:])
                steps.append(f"@{t:.3f}")
            else:
                steps.append(st)
                t += 0.1 if st.startswith("tap") else 0.01 if st.startswith("enc") else 0
        checks.append(t)
        t += GAP
        for n in NOTES:
            starts.append(t)
            steps += [f"@{t:.3f}", f"on:{n},127", f"@{t + 0.6:.3f}", "show", f"@{t + HOLD:.3f}", f"off:{n}"]
            t += HOLD + GAP
    steps.append(f"@{t:.3f}")
    return steps, starts, checks


def model_set(work, rerender):
    wav, log = os.path.join(work, "model-notes.wav"), os.path.join(work, "model-notes.log")
    steps, starts, checks = model_steps()
    if rerender or not os.path.exists(wav):
        out = subprocess.run([RENDER, ROMS, wav] + steps, check=True, capture_output=True, text=True).stdout
        open(log, "w").write(out)
    shows = parse_shows(open(log).read())
    sr, x = load(wav)
    rec = 36.0  # the WAV starts at the "rec" step
    passes = []
    for p, (name, v1, v2, octave) in enumerate(PASSES):
        # the panel as the pass starts: the last two shows before it (VCO2 page, then VCO1 page)
        before = [s for s in shows if s["t"] <= checks[p] + 0.01][-2:]
        got = {int(s["page"]): s["values"] for s in before}
        if got.get(1, [None] * 4)[0::3] != list(v1) or got.get(2, [None] * 4)[0::3] != list(v2):
            sys.exit(f"{name}: panel reads VCO1 {got.get(1)} VCO2 {got.get(2)}, wanted FREQ/VOL {v1} {v2}")
        res = {}
        for i, n in enumerate(NOTES):
            k = p * len(NOTES) + i
            start = int((starts[k] - rec) * sr)
            i0 = onset(x, sr, start, start + int(HOLD * sr))
            r = analyse(x, sr, i0, n, octave)
            r["voice"] = min(shows, key=lambda s: abs(s["t"] - starts[k] - 0.6))["voice"]
            res[n] = r
        passes.append(dict(name=name, octave=octave, notes=res))
    return passes


def parse_shows(text):
    """Per show: time, the VCO page shown and its four values, the sounding voice (highest VCA pin)."""
    out = []
    for block in re.split(r"\n(?=\s*[\d.]+ VFD1)", text):
        m1 = re.search(r"([\d.]+) VFD1 \|.*VC0\s+(\d)\s*\|", block)
        m3 = re.search(r"VFD3 \|(.*)\|", block)
        if not m1:
            continue
        best = max(((int(m.group(1)), float(m.group(2))) for m in re.finditer(r"v(\d) cv:.*?vca pin\s+(-?[\d.]+)", block)),
                   key=lambda v: v[1], default=(0, 0))
        out.append(dict(t=float(m1.group(1)), page=m1.group(2), values=[int(v) for v in m3.group(1).split()],
                        voice=best[0]))
    return out


def stats(s):
    notes = s["notes"]
    m = np.array(sorted(notes))
    c = np.array([notes[k]["c"] for k in m])
    ref = np.median(c)
    r = c - ref
    slope, icpt = np.polyfit((m - 66) / 12, c, 1)
    fit = r - (np.polyval([slope, icpt], (m - 66) / 12) - ref)
    lv = np.array([notes[k]["level"] for k in m])
    lslope = np.polyfit((m - 66) / 12, lv, 1)[0]
    lo, hi = m < m.min() + 12, m > m.max() - 12
    s.update(ref=ref, resid=dict(zip(m, r)), rms=np.sqrt(np.mean(r ** 2)), min=r.min(), max=r.max(), slope=slope,
             fitrms=np.sqrt(np.mean(fit ** 2)), bottom=r[lo].mean(), top=r[hi].mean(),
             lmed=np.median(lv), lslope=lslope, drift=np.median([notes[k]["drift"] for k in m]),
             m=m, scatter=np.sqrt(np.mean(np.diff(r) ** 2) / 2))
    return s


def voice_share(s):
    """Share of the residual variance explained by note index mod 6 (one mean per group).

    If the sampler played the notes in order on a rotating voice assignment, per-voice tuning
    differences show up as a pattern with period 6. 5/(n-1) is what random grouping gives."""
    m = s["m"]
    r = np.array([s["resid"][k] for k in m])
    r = r - r.mean()
    g = (m - m.min()) % 6
    means = np.array([r[g == i].mean() for i in range(6)])
    return np.sum(means[g] ** 2) / np.sum(r ** 2), 5 / (len(m) - 1)


def short(s):
    return s["name"].split(" (")[0].replace("model: ", "model ")


def estimator_check():
    """Largest error (cents) of pitch() on synthetic sawtooths: one, and a 3-cent detuned pair."""
    sr, e1, e2 = 48000, 0.0, 0.0
    t = np.arange(int((WIN[1] - WIN[0]) * sr)) / sr
    rng = np.random.default_rng(1)

    def saw(f):
        ph = rng.uniform(0, 2 * np.pi)
        return sum(np.sin(2 * np.pi * h * f * t + h * ph) / h for h in range(1, int(16000 / f) + 1))

    for n in NOTES:
        f = et(n) * 2 ** (rng.uniform(-20, 20) / 1200)
        e1 = max(e1, abs(cents(pitch(saw(f), sr, et(n)), f)))
        f2 = f * 2 ** (3 / 1200)
        target = (f * 1.0 + f2 * 0.25) / 1.25
        e2 = max(e2, abs(cents(pitch(saw(f) + 0.5 * saw(f2), sr, et(n)), target)))
    return e1, e2


def fmt(v, f="{:+.1f}"):
    return "" if v is None or (isinstance(v, float) and np.isnan(v)) else f.format(v)


def report(real, model):
    L = []
    w = L.append
    sets = real + model
    w("# Xpander per-note comparison: freesound notes vs the model\n")
    w("Made by `machines/xpander/tools/compare/notes.py`. Real: Modular Samples' CC0 notes (four patches, "
      "settings unknown, MIDI 36-96 at velocity 127, Vorbis 192 kb/s previews). Model: power-on, RESET MEMORY, "
      "TUNE VCOS, the default patch (two sawtooths, 4-pole LP), the same MIDI notes at velocity 127, four "
      "times through: as it is, VCO1 alone, VCO2 alone, VCO1 alone an octave down (FREQ 0; each pass moves "
      "every note to the next voice). Window 0.5-4.5 s after the onset.\n")
    w("## Pitch\n")
    w("Cents against equal temperament at A4 = 440 Hz. *Ref* is each set's median (the recording's or the "
      "patch's own offset, including the patch octave and any detune); the residual is against it. "
      "*Stretch* is the fitted slope (cents per octave); *fit rms* what is left after it. *Bottom/top* are the "
      "mean residuals of the lowest and highest octave. *Scatter* is the note-to-note part, rms of the "
      "neighbour differences over the square root of 2 (a smooth curve gives about 0). *Drift* is the median over notes of max-min across "
      "four 1 s windows. *Voice share*: share of the residual variance a period-6 pattern explains "
      "(random grouping gives the value after the slash).\n")
    w("| Set | Octave | Ref (c) | Residual rms | min | max | Stretch (c/oct) | Fit rms | Bottom | Top | Scatter | Drift "
      "| Voice share |")
    w("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for s in sets:
        vs, rnd = voice_share(s)
        octv = f"{s['octave']:+d}" + (f" ({s['octave_votes']}/{s['n']})" if "octave_votes" in s else "")
        w(f"| {s['name']} | {octv} | {s['ref']:+.1f} | {s['rms']:.1f} | {s['min']:+.1f} | {s['max']:+.1f} | "
          f"{s['slope']:+.2f} | {s['fitrms']:.1f} | {s['bottom']:+.1f} | {s['top']:+.1f} | {s['scatter']:.1f} | "
          f"{s['drift']:.1f} | "
          f"{vs:.2f} / {rnd:.2f} |")
    e1, e2 = estimator_check()
    w(f"\nEstimator check on synthetic notes at the same MIDI numbers (window, Hann, 8x zero-pad): one sawtooth "
      f"max |error| {e1:.3f} c; two sawtooths 3 cents apart, at 0 dB and -6 dB (the power-weighted mean is the "
      f"target), max |error| {e2:.2f} c.\n")
    w("Correlation of the per-note residuals between sets (notes in common):\n")
    w("| | " + " | ".join(short(s) for s in sets) + " |")
    w("|" + "---|" * (len(sets) + 1))
    for a in sets:
        row = []
        for b in sets:
            k = sorted(set(a["resid"]) & set(b["resid"]))
            row.append(f"{np.corrcoef([a['resid'][n] for n in k], [b['resid'][n] for n in k])[0, 1]:+.2f}")
        w(f"| {short(a)} | " + " | ".join(row) + " |")
    w("")

    w("### Per note\n")
    w("Residual in cents (real: against the set's median; model: absolute against A4 = 440 Hz, since its "
      "reference is known). *(v)* is the model voice.\n")
    w("| MIDI | " + " | ".join(short(s) for s in sets) + " |")
    w("|" + "---|" * (len(sets) + 1))
    for n in NOTES:
        cells = [fmt(s["resid"].get(n)) for s in real]
        cells += [f"{m['notes'][n]['c']:+.1f} ({m['notes'][n]['voice']})" for m in model]
        w(f"| {n} | " + " | ".join(cells) + " |")

    w("\n## Level per note (patch-dependent, indicative)\n")
    w("RMS of the window, dB against the set's median. *Slope* is the fitted dB per octave; *median* the set's "
      "median in dBFS (recording gain unknown, so not comparable across sets).\n")
    w("| Set | Median (dBFS) | Slope (dB/oct) | min | max |")
    w("|---|---|---|---|---|")
    for s in sets:
        lv = np.array([s["notes"][k]["level"] for k in s["m"]]) - s["lmed"]
        w(f"| {s['name']} | {s['lmed']:.1f} | {s['lslope']:+.2f} | {lv.min():+.1f} | {lv.max():+.1f} |")

    w("\n## Brightness per note (patch-dependent)\n")
    w("Spectral centroid of the window (20 Hz-16 kHz) in harmonics of the fundamental. A fixed-cut-off filter "
      "gives a centroid in harmonics that falls with pitch; full keyboard tracking keeps it level.\n")
    w("| MIDI | " + " | ".join(short(s) + " dB / ctr" for s in sets) + " |")
    w("|" + "---|" * (len(sets) + 1))
    for n in NOTES[::3]:
        cells = []
        for s in sets:
            r = s["notes"].get(n)
            cells.append("" if r is None else f"{r['level'] - s['lmed']:+.1f} / {r['bright']:.1f}")
        w(f"| {n} | " + " | ".join(cells) + " |")
    return "\n".join(L) + "\n"


def main():
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else None
    work = sys.argv[sys.argv.index("--work") + 1] if "--work" in sys.argv else os.path.join(ROOT, "build/xpander-compare")
    os.makedirs(work, exist_ok=True)
    real = [stats(s) for s in real_sets()]
    model = [stats(s) for s in model_set(work, "--rerender" in sys.argv)]
    md = report(real, model)
    if out:
        # keep hand-written findings between the markers in an existing file
        old = open(out).read() if os.path.exists(out) else ""
        m = re.search(r"<!-- findings -->.*?<!-- /findings -->\n", old, re.S)
        if m:
            head, rest = md.split("\n## Pitch", 1)
            md = head + "\n" + m.group(0) + "\n## Pitch" + rest
        open(out, "w").write(md)
    else:
        sys.stdout.write(md)


if __name__ == "__main__":
    main()
