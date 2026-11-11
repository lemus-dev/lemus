#!/usr/bin/env python3
"""The voice against a third unit: acidvoice.com's RE-303 (a replica of the TB-303's boards),
32 recordings sent 2026-11-10 (private/reference/acidvoice-re303): one 16-step pattern at 130 BPM
at 16 knob settings, saw and square, 24-bit. The knobs are read off a drawing per setting (a
pointer's clock position on the pot's 300 degrees), good to about half a tick; TUNING and VOLUME
aren't shown. Each clip is rendered at his unit's pitch in it (`pitch`), and at 1.5 cents either
side, the three distances averaged (DETUNES).

Each clip's second bar (the first starts from rest) is cut into its 16 steps and each step into
three slices; a slice is its level in third-octave bands. A render through tb303render is cut the
same way, lined up on the clip by its envelope and level-matched over the bar.

  re303.py compare [--unit] [--top HZ] [--cal NAME=VALUE ...] [--tuning T (one knob for every clip)] [--knob SETTING.KNOB=V ...] [--steps N[,N]]
  re303.py fit NAME[,NAME...] [--unit] [--cal ...]     fit those calibration values (and TUNING)
  re303.py pitch                                        the recordings' own pitch, step by step

compare prints, per clip: the distance (RMS dB over bands, slices and steps), each step's level
and brightness (spectral centroid, cents) against the recording's, and where one stands out, the
resonant peak. --steps prints those clips step by step.
"""
import json, os, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../../../.."))
REAL = os.path.normpath(os.path.join(HERE, "../../private/reference/acidvoice-re303"))
TB303RENDER = os.environ.get("TB303RENDER", os.path.join(ROOT, "build/tb303/tb303render_artefacts/Release/tb303render"))
SR = 44100
BPM = 130
STEP = 60 / BPM / 4
KNOBS = ["cutoff", "resonance", "envmod", "decay", "accent"]

# step: (note, steps held, accent). Sounding pitches (his Cubase names are an octave above).
PATTERN = {0: ("A-2", 1, 1), 1: ("A-1", 2, 1), 3: ("A-2", 1, 0), 4: ("A-2", 2, 0), 6: ("D-2", 2, 1), 8: ("D-3", 2, 0),
           10: ("D-3", 1, 0), 11: ("D-3", 1, 0), 12: ("D-3", 1, 0), 13: ("C#3", 1, 0), 14: ("E-3", 1, 0), 15: ("E-3", 1, 0)}
MIDI = {"A-1": 33, "D-2": 38, "A-2": 45, "C#3": 49, "D-3": 50, "E-3": 52}
ACCENTED = (0, 1, 2, 6, 7)  # steps sounding an accented note (2 and 7 are its held half)
# The note sounding in each step.
STEP_NOTE = {}
for _s, (_n, _held, _a) in PATTERN.items():
    for _k in range(_held):
        STEP_NOTE[_s + _k] = _n

# Pointer angle in each screenshot (0 = 12 o'clock): CUT OFF, RESONANCE, ENV MOD, DECAY, ACCENT.
MIN, MAX = -150, 150
ANGLES = {
    1: (MIN, MAX, MIN, MAX, MIN), 2: (0, MAX, MIN, MAX, MIN), 3: (MAX, MAX, MIN, MAX, MIN),
    4: (-56, MAX, MIN, MAX, 0), 5: (90, MAX, MIN, MAX, 90), 6: (MAX, MAX, MIN, MAX, MAX),
    7: (0, 0, 0, 0, MIN), 8: (MIN, MIN, MIN, MIN, 90), 9: (-90, 0, MAX, MIN, 0),
    10: (0, 90, 90, -54, -90), 11: (MAX, MAX, MAX, MAX, MIN), 12: (-90, MAX, -27, MIN, MIN),
    13: (90, MAX, 90, MIN, 90), 14: (MIN, MIN, MIN, MIN, MIN), 15: (-90, 50, 0, 0, 0),
    16: (0, MAX, MAX, MIN, MAX),
}
SETTINGS = {n: dict(zip(KNOBS, (round((a + 150) / 300, 3) for a in ang))) for n, ang in ANGLES.items()}
CLIPS = [(n, w) for n in SETTINGS for w in ("saw", "square")]

# Analysis: three slices a step, third-octave bands.
SLICES = [(0.008, 0.043), (0.043, 0.078), (0.078, 0.113)]
NFFT = 8192
# Third-octave bands from 60 Hz to 14 kHz, none narrower than 120 Hz: a 35 ms slice smears each
# harmonic over ~60 Hz, so a narrower band's level follows the VCO's phase (a one-cent change of
# TUNING moved the score 0.3 dB with 56 Hz bands).
EDGES = [60.0]
while EDGES[-1] < 14000:
    EDGES.append(max(EDGES[-1] * 2 ** (1 / 3), EDGES[-1] + 120))
EDGES = np.array(EDGES)
FLOOR_DB = 55  # bands this far under the clip's loudest don't count


def name(n, w):
    return f"re303-{n:02d}-{w}"


def load(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    assert sr == SR, path
    return x


def real(n, w):
    return load(os.path.join(REAL, f"{n:02d}_{w.capitalize()}.wav"))


def specs(tuning=None, knobs=None, detune=0.0, suffix=""):
    """One spec per clip. TUNING plays each clip at the pitch his unit had in it (clip_cents),
    `detune` cents off it, unless `tuning` sets the knob for every clip."""
    out = []
    cents = clip_cents() if tuning is None else {}
    for n, w in CLIPS:
        p = dict(SETTINGS[n])
        for key, v in (knobs or {}).items():
            s, k = key.split(".")
            if s in ("*", f"{n:02d}", str(n)):
                p[k] = v
        t = tuning if tuning is not None else 0.5 + (cents[(n, w)] - OUR_A_CENTS + detune) / (2 * TUNING_RANGE_CENTS)
        notes = [{"line": bar * 16 + s, "note": note, "len": held - 0.5, "accent": bool(acc)}
                 for bar in range(2) for s, (note, held, acc) in PATTERN.items()]
        out.append({"name": name(n, w) + suffix, "bpm": BPM, "lpb": 4, "lines": 32,
                    "params": {"waveform": int(w == "square"), "tuning": t, "volume": 1.0, **p}, "notes": notes})
    return out


# Each clip is scored at his pitch and these cents either side, and the three averaged: at full
# RESONANCE a cent of pitch moves a clip's distance by up to 1.8 dB (where the ring falls among the
# harmonics), which would otherwise steer a fit.
DETUNES = (-1.5, 0.0, 1.5)
OUR_A_CENTS = 0.5           # the voice's A at TUNING centre: 110.03 Hz (tools/service/check.py)
TUNING_RANGE_CENTS = 700    # TUNING end to end / 2 (core/voice.h)


def render(args, tuning=None, knobs=None, out_dir=None, jobs=8, dither=True):
    """Render every clip; returns {(clip name, detune): samples}. args go to tb303render."""
    detunes = DETUNES if dither and tuning is None else (0.0,)
    all_specs = []
    for i, dt in enumerate(detunes):
        all_specs += specs(tuning, knobs, dt, f"~{i}")
    tmp = tempfile.TemporaryDirectory()
    out = out_dir or tmp.name
    os.makedirs(out, exist_ok=True)

    def run(chunk):
        path = os.path.join(tmp.name, f"specs-{chunk[0]['name']}.json")
        json.dump(chunk, open(path, "w"))
        subprocess.run([TB303RENDER, path, "--out", out, "--rate", str(SR), *args], check=True, stdout=subprocess.DEVNULL)

    chunks = [all_specs[i::jobs] for i in range(jobs)]
    with ThreadPoolExecutor(jobs) as ex:
        list(ex.map(run, chunks))
    res = {}
    for i, dt in enumerate(detunes):
        for n, w in CLIPS:
            res[(name(n, w), dt)] = load(os.path.join(out, f"{name(n, w)}~{i}.wav"))
    tmp.cleanup()
    return res


def envelope(x):
    n = len(x) // 44 * 44
    return np.abs(x[:n]).reshape(-1, 44).mean(axis=1)  # 1 ms


def onsets_ms(x):
    """Each new note's onset in the two bars (ms): the first ms its envelope passes a fifth of the
    step's peak, searched from 25 ms before the step's line."""
    e = envelope(x)
    out = []
    for bar in range(2):
        for s in PATTERN:
            t = int(round((bar * 16 + s) * STEP * 1000))
            a = max(0, t - 25)
            seg = e[a:t + 90]
            if len(seg) < 30:
                out.append(np.nan)
                continue
            out.append(a + int(np.argmax(seg > 0.2 * seg.max())))
    return np.array(out, float)


def lag_ms(ref, x):
    """How much later x plays than ref, ms: the median over the notes of the onset differences
    (an envelope cross-correlation jumped by up to 7 ms with the timbre)."""
    return int(round(np.nanmedian(onsets_ms(x) - onsets_ms(ref))))


_window = {}


def slice_spec(x, t0, t1):
    seg = x[int(round(t0 * SR)):int(round(t1 * SR))]
    w = _window.setdefault(len(seg), np.hanning(len(seg)))
    return np.abs(np.fft.rfft(seg * w, NFFT)) ** 2 / (w ** 2).sum()


FREQS = np.fft.rfftfreq(NFFT, 1 / SR)
BAND_OF = np.searchsorted(EDGES, FREQS) - 1
BAND_OF[(FREQS < EDGES[0]) | (FREQS >= EDGES[-1])] = -1
CENT = (FREQS > 40) & (FREQS < 12000)


def features(x, start):
    """Bar 2 of a take starting at `start` s: bands[step, slice, band] (power), centroid Hz,
    level (power), and the spectra for the resonant peak."""
    bands = np.zeros((16, len(SLICES), len(EDGES) - 1))
    cent = np.zeros((16, len(SLICES)))
    lev = np.zeros((16, len(SLICES)))
    spec = np.zeros((16, len(SLICES), len(FREQS)))
    for s in range(16):
        for k, (a, b) in enumerate(SLICES):
            t = start + (16 + s) * STEP
            p = slice_spec(x, t + a, t + b)
            spec[s, k] = p
            bands[s, k] = np.bincount(BAND_OF[BAND_OF >= 0], p[BAND_OF >= 0], len(EDGES) - 1)
            cent[s, k] = (p[CENT] * FREQS[CENT]).sum() / max(p[CENT].sum(), 1e-30)
            lev[s, k] = p.sum()
    return dict(bands=bands, cent=cent, lev=lev, spec=spec)


def resonant_peak(p, square):
    """The frequency the resonance lifts most in one slice's spectrum (Hz) and how far it stands
    over the octave under it (dB): the spectrum smoothed over a third of an octave and whitened
    by the wave's fall-off (6 dB per octave)."""
    m = (FREQS > 150) & (FREQS < 9000)
    f = FREQS[m]
    cs = np.concatenate([[0], np.cumsum(p)])
    lo = np.searchsorted(FREQS, f * 2 ** (-1 / 6)); hi = np.searchsorted(FREQS, f * 2 ** (1 / 6))
    sm = (cs[hi] - cs[lo]) / np.maximum(hi - lo, 1)
    w = 10 * np.log10(sm + 1e-30) + 20 * np.log10(f)
    i = int(np.argmax(w))
    under = (f > f[i] / 3) & (f < f[i] / 1.6)
    stand = w[i] - np.median(w[under]) if under.sum() > 3 else 0.0
    return float(f[i]), float(stand)


def db(v):
    return 10 * np.log10(np.maximum(v, 1e-30))


_real_cache = {}


def real_features(n, w):
    if (n, w) not in _real_cache:
        x = real(n, w)
        _real_cache[(n, w)] = (x, features(x, 0.0))
    return _real_cache[(n, w)]


def compare_clip(n, w, y):
    """One clip against its render y. Returns the distance and the per-step differences."""
    x, fr = real_features(n, w)
    lag = lag_ms(x, y)
    fo = features(y, lag / 1000)
    gain = db(fr["lev"].sum()) - db(fo["lev"].sum())  # level-matched over the bar
    br, bo = db(fr["bands"]), db(fo["bands"]) + gain
    floor = br.max() - FLOOR_DB
    d = np.maximum(bo, floor) - np.maximum(br, floor)
    count = (br > floor) | (bo > floor)
    dist = float(np.sqrt((d[count] ** 2).mean()))
    step_dist = np.array([np.sqrt((d[s][count[s]] ** 2).mean()) if count[s].any() else 0 for s in range(16)])
    dlev = db(fo["lev"].sum(axis=1)) + gain - db(fr["lev"].sum(axis=1))  # per step, ours - real
    # Brightness per slice, cents, ours - real; slices 40 dB under the bar's loudest left out.
    loud = fr["lev"] > fr["lev"].max() * 1e-4
    dcent = np.where(loud, 1200 * np.log2(fo["cent"] / fr["cent"]), np.nan)
    square = w == "square"
    pr = np.array([[resonant_peak(fr["spec"][s, k], square) for k in range(len(SLICES))] for s in range(16)])
    po = np.array([[resonant_peak(fo["spec"][s, k], square) for k in range(len(SLICES))] for s in range(16)])
    return dict(dist=dist, step_dist=step_dist, dlev=dlev, dcent=dcent, lag=lag, gain=gain, peak_real=pr, peak_ours=po,
                d=d, count=count)


def compare_all(renders):
    """Per clip: the comparison at his pitch, its distance the RMS over the detuned renders."""
    out = {}
    detunes = sorted({dt for _, dt in renders})
    for n, w in CLIPS:
        each = {dt: compare_clip(n, w, renders[(name(n, w), dt)]) for dt in detunes}
        r = dict(each[0.0])
        r["dist"] = float(np.sqrt(np.mean([e["dist"] ** 2 for e in each.values()])))
        r["dist_spread"] = float(np.ptp([e["dist"] for e in each.values()]))
        out[(n, w)] = r
    return out


def score(res):
    return float(np.sqrt(np.mean([r["dist"] ** 2 for r in res.values()])))


def nanmean(v):
    v = np.asarray(v, float)
    return float(np.nanmean(v)) if np.isfinite(v).any() else float("nan")


def report(res, steps=()):
    print(f"{'clip':16s} {'CUT RES ENV DEC ACC':20s} {'dist':>5s} {'+-':>4s} {'lag':>4s} | level, ours - his (dB)  | brightness, ours - his (cents)        | peak, his / ours (Hz), first slice")
    print(f"{'':16s} {'':20s} {'dB':>5s} {'':>4s} {'ms':>4s} | accents  plain  spread | plain  accent: 1st  2nd  3rd slice  | accent A2 (step 0)   plain D3 (step 10)")
    plain = [s for s in range(16) if s not in ACCENTED]
    for (n, w), r in res.items():
        k = SETTINGS[n]
        knobs = " ".join(f"{k[x] * 10:3.0f}" for x in KNOBS)
        la, lp = r["dlev"][list(ACCENTED)].mean(), r["dlev"][plain].mean()
        ca = [nanmean(r["dcent"][list(ACCENTED), i]) for i in range(3)]
        cp = nanmean(r["dcent"][plain])

        def pk(s):
            a, b = r["peak_real"][s, 0], r["peak_ours"][s, 0]
            if a[1] < 8 and b[1] < 8:
                return f"{'-':>17s}"
            return f"{a[0]:6.0f} / {b[0]:5.0f} {1200 * np.log2(b[0] / a[0]):+5.0f}c"[:20].rjust(20)
        print(f"{name(n, w):16s} {knobs:20s} {r['dist']:5.2f} {r['dist_spread']:4.1f} {r['lag']:4d} | {la:+6.1f} {lp:+6.1f} {np.ptp(r['dlev']):6.1f}  | {cp:+5.0f}         {ca[0]:+5.0f} {ca[1]:+5.0f} {ca[2]:+5.0f}       | {pk(0)}  {pk(10)}")
    print(f"score {score(res):.3f} dB (RMS over clips); saw {score({k: v for k, v in res.items() if k[1] == 'saw'}):.3f}, "
          f"square {score({k: v for k, v in res.items() if k[1] == 'square'}):.3f}")
    for n in steps:
        for w in ("saw", "square"):
            r = res[(n, w)]
            print(f"\n{name(n, w)}: step by step (ours - his)")
            print("step note acc  dist  level   bright 1st   2nd   3rd    peak his (Hz, dB out)         peak ours")
            for s in range(16):
                c = r["dcent"][s]
                pr, po = r["peak_real"][s], r["peak_ours"][s]
                print(f"{s:3d}  {STEP_NOTE.get(s, '   ')}  {'A' if s in ACCENTED else ' '}  {r['step_dist'][s]:5.1f} {r['dlev'][s]:+6.1f}   "
                      f"{c[0]:+6.0f} {c[1]:+6.0f} {c[2]:+6.0f}   "
                      + " ".join(f"{a:5.0f}({b:2.0f})" for a, b in pr) + "   " + " ".join(f"{a:5.0f}({b:2.0f})" for a, b in po))


def step_cents(x):
    """The pitch of each new note in bar 2 against equal temperament at A 440 (cents; NaN for
    steps without one): the strongest of its first six harmonics, from 30 to 105 ms in."""
    row = []
    for s in range(16):
        if s not in PATTERN:
            row.append(np.nan)
            continue
        t = (16 + s) * STEP
        seg = x[int((t + 0.03) * SR):int((t + 0.105) * SR)]
        f = 440 * 2 ** ((MIDI[PATTERN[s][0]] - 69) / 12)
        sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 18))
        fr = np.fft.rfftfreq(1 << 18, 1 / SR)
        best = None
        for h in range(1, 7):
            m = (fr > h * f * 2 ** (-1 / 12)) & (fr < h * f * 2 ** (1 / 12))
            i = np.argmax(sp[m])
            if best is None or sp[m][i] > best[0]:
                best = (sp[m][i], fr[m][i] / h)
        row.append(1200 * np.log2(best[1] / f))
    return np.array(row)


_clip_cents = {}


def clip_cents():
    """His unit's pitch in each clip: the median over its plain notes (the accented ones read low
    where the sweep pulls the strongest harmonic). It moves between clips, -10 to -34 cents."""
    if not _clip_cents:
        for n, w in CLIPS:
            c = step_cents(real(n, w))
            _clip_cents[(n, w)] = float(np.nanmedian([c[s] for s in PATTERN if not PATTERN[s][2]]))
    return _clip_cents


def cmd_pitch():
    print("clip             " + " ".join(f"{s:>5d}" for s in range(16)) + "   clip  (cents from the note; the held halves left out)")
    rows = []
    for n, w in CLIPS:
        row = step_cents(real(n, w))
        rows.append(row)
        print(f"{name(n, w):16s} " + " ".join("     " if np.isnan(c) else f"{c:+5.0f}" for c in row) + f"   {clip_cents()[(n, w)]:+5.1f}")
    rows = np.array(rows)
    print(f"{'median':16s} " + " ".join("     " if np.all(np.isnan(c)) else f"{np.nanmedian(c):+5.0f}" for c in rows.T))


def parse(argv):
    args, tuning, knobs, steps, rest = [], None, {}, [], []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--unit":
            args.append(a)
        elif a in ("--cal", "--top"):
            args += [a, argv[i + 1]]; i += 1
        elif a == "--tuning":
            tuning = float(argv[i + 1]); i += 1
        elif a == "--knob":
            k, v = argv[i + 1].split("="); knobs[k] = float(v); i += 1
        elif a == "--steps":
            steps = [int(v) for v in argv[i + 1].split(",")]; i += 1
        else:
            rest.append(a)
        i += 1
    return args, tuning, knobs, steps, rest


# Fit bounds: a part's tolerance, or a trimmer's travel.
BOUNDS = {
    "vcfRingHz": (150, 1700), "cutoffTaperMid": (0.03, 0.6), "taperAMid": (0.05, 0.5), "q21Beta": (200, 700),
    "megPeakV": (8.0, 11.0), "decayPotOhms": (700e3, 1300e3), "decayTaperMid": (0.05, 0.4), "megCapUF": (0.8, 1.5),
    "vr4bOhms": (35e3, 65e3), "taperAShape": (0.3, 2.5), "decayTaperShape": (0.3, 2.5), "c13UF": (0.8, 1.5), "vcaSwing": (0.1, 1.0), "q21TailmA": (0.4, 0.65), "pin1ShiftV": (-0.12, 0.04),
}


def cmd_fit(names, args, tuning, knobs):
    from scipy.optimize import minimize
    base = {}
    for i, a in enumerate(args):
        if a == "--cal":
            k, v = args[i + 1].split("="); base[k] = float(v)
    fixed = [a for a in args if a == "--unit"]
    lo = np.array([BOUNDS[k][0] for k in names]); hi = np.array([BOUNDS[k][1] for k in names])
    log = lo > 0

    def to_x(v):
        return (np.log(v / lo) / np.log(hi / lo))

    def from_x(x):
        return lo * (hi / lo) ** np.clip(x, 0, 1)

    seen = {}

    def f(x):
        v = from_x(x)
        key = tuple(np.round(v, 6))
        if key not in seen:
            cal = dict(base); cal.update(dict(zip(names, v)))
            a = list(fixed)
            for k, val in cal.items():
                a += ["--cal", f"{k}={val:.6g}"]
            seen[key] = score(compare_all(render(a, tuning, knobs)))
            print("  " + " ".join(f"{k}={val:.5g}" for k, val in zip(names, v)) + f"  -> {seen[key]:.4f}", flush=True)
        return seen[key] + 10 * float(np.sum(np.maximum(0, x - 1) ** 2 + np.maximum(0, -x) ** 2))

    start = np.array([base.get(k, np.sqrt(BOUNDS[k][0] * BOUNDS[k][1])) for k in names])
    x0 = to_x(np.clip(start, lo, hi))
    r = minimize(f, x0, method="Nelder-Mead", options={"xatol": 0.004, "fatol": 0.002, "maxfev": 60 * len(names),
                                                       "initial_simplex": np.vstack([x0] + [x0 + 0.08 * np.eye(len(names))[i] * (1 if x0[i] < 0.9 else -1) for i in range(len(names))])})
    v = from_x(r.x)
    print("fit: " + " ".join(f"--cal {k}={val:.5g}" for k, val in zip(names, v)) + f"   score {r.fun:.4f}")
    for k, val in zip(names, v):
        if val <= BOUNDS[k][0] * 1.01 or val >= BOUNDS[k][1] * 0.99:
            print(f"  {k} is at its bound ({BOUNDS[k][0]:g} to {BOUNDS[k][1]:g})")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    args, tuning, knobs, steps, rest = parse(sys.argv[2:])
    if cmd == "pitch":
        cmd_pitch()
    elif cmd == "compare":
        report(compare_all(render(args, tuning, knobs, out_dir=os.environ.get("RE303_OUT"))), steps)
    elif cmd == "fit":
        cmd_fit(rest[0].split(","), args, tuning, knobs)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
