#!/usr/bin/env python3
"""units.py: Lemus's SH-101 against recordings of real SH-101 units, per unit and per event.

One row per event (a take, a note, a glide, a slider reading), never averaged across units, in
absolute units: real, ours, ours - real, a limit and a status, then a PASS/FAIL line. Lemus is
rendered fresh on every run (sh101render, about 1,550 short specs; Real unit calibration unless
--calibration says otherwise), so the table is the current model's. About 25 s on an M1 Max.

Usage (from anywhere, with the repo's venv):
    .venv/bin/python machines/sh101/tools/compare/units.py [--only GROUP,...] [--out DIR]
                     [--calibration real|service|roland] [--jobs N] [--render PATH]
Renders go to DIR/<calibration>/ (default build/sh101/work/units/renders). Exit status 0 when no
row is OVER, 1 otherwise. sh101render must be built (build/sh101); if it is missing the tool
waits up to 10 minutes for it (another session may be relinking it).

Groups:
  env     ENV to cutoff: Friendly Noise's RES 10 take (plateau, release-fit rest, sweep, release
          tau), loopop's two resonant decays (the sweep from a decay fit), Failed Muso's ping.
  levels  each source re the saw at 10 (Perfect Circuit, gstormelectro, AutomaticGainsay): per
          harmonic, real - ours, less the recording chain measured on the same unit's saw.
  res     AutomaticGainsay's peak over the fundamental at RES 2.3 / 4.7 / 7.1 (hand-swept cutoff
          against ours on a fine FREQ grid); loopop's peak frequency as RES rises.
  porta   each glide's time constant: Once Upon a Synth at maximum, AutomaticGainsay at mid-travel.
  lfo     Perfect Circuit's top LFO rate (sideband spacing).
  freq    FREQ's bottom: Friendly Noise's sub take, the cutoff at KCV 0, against ours at FREQ
          1.2 / 1.5 / 1.8.
  noise   the noise source's colour per clip, less the same unit's saw at FREQ 10 (its filter top
          and recording chain); a unit without a saw is info.
  top     the saw at FREQ 10 against 1/n per unit, 5-13 kHz: the filter's top, chain included.

Status: ok (within the limit), OVER (outside it), known (outside it, and a known gap listed in
docs/improvements.md, "Known inaccuracies"; the note says which), info (no pass/fail: the
setting is uncertain, so the note gives the setting at which ours matches instead). Every limit
states its basis in LIMITS below.

The clips are private/reference/shootout/wav/roland_sh-101_real__*.wav (lossy YouTube audio;
docs/improvements.md, "Reference data", lists what each unit shows); no full download is needed.
Every constant a check needs (clip windows, slider readings, notes) is in this file, with where it
was read. AutomaticGainsay's mixer sliders were read from its 720p video (2026-11-13, with
build/sh101/work/mixer/dl/readag.py); the rest are as docs/roland-measurements.md ("Real
recordings"), docs/online-references.md and private/reference/scenes/shootout-real/make_scenes.py
give them. The 2026-10-24/26 hand checks this replaces are in build/sh101/work/{env,mixer}.
"""
import argparse, json, math, os, subprocess, sys, time, warnings

import numpy as np
from scipy import optimize, signal
from scipy.signal import medfilt

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import analyse as A  # noqa: E402

warnings.filterwarnings("ignore", category=RuntimeWarning)   # fits trying wild values, empty slices

REPO = os.path.normpath(os.path.join(HERE, "../../../.."))
WAV = os.path.normpath(os.path.join(HERE, "../../private/reference/shootout/wav"))
# The machine's build folder: SH101_BUILD (relative to the repo, or absolute), else build/sh101.
RENDER = os.path.join(REPO, os.environ.get("SH101_BUILD", "build/sh101"), "sh101render_artefacts/Release/sh101render")
CLIP = "roland_sh-101_real__"

# A plain patch (private/reference/scenes/shootout-real/make_scenes.py): saw only, filter open,
# VCA GATE, no modulation. Sliders are travel 0-1; switches are item indices.
BASE = dict(lfoRate=0.5, lfoWave=0, vcoMod=0, range=1, pulseWidth=0, pwmSource=1, pulseLevel=0, sawLevel=1,
            subLevel=0, noiseLevel=0, subMode=0, vcfFreq=1, vcfRes=0, vcfEnv=0, vcfMod=0, vcfKybd=0, vcaMode=1,
            attack=0, decay=0.5, sustain=1, release=0, trigger=0, volume=0.5, tune=0.5, portamento=0, portaMode=2,
            bendVco=0, bendVcf=0, lfoModDepth=0, transpose=1)
NOTE_NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]

# Limits, and their basis. A limit is what a real unit may differ from ours by before the row is
# OVER: the documented unit-to-unit spread, or the reading's uncertainty, never widened to pass.
LIMITS = {
    # ENV, the Friendly Noise unit (the Real unit calibration is fitted to it): fitting the release
    # over other windows moves the rest 25.6-32.2 Hz and the depth 7.02-7.31 oct, tau 4.09-4.41 s
    # (online-references.md 1.1); the plateau line is read to under 1 %.
    "fn_plateau_oct": 0.10, "fn_rest_oct": 0.25, "fn_sweep_oct": 0.20, "fn_tau_s": 0.20,
    # ENV, loopop's unit: the 2026-10-24 fits put its weight at 0.72-0.79 of the schematic's,
    # +-5 % of a 7.2-octave sweep (model.md, "Calibrations").
    "lp_sweep_oct": 0.40,
    # Levels re the saw at 10: the parts' spread is +1.2 to +2.7 dB on the square and -1.0 to
    # +0.7 dB on the subs (improvements.md, "Mixer levels"), +-0.75 / +-0.85 about its middle,
    # plus about 0.2 dB for a slider read at 10 that may sit at 9.8.
    "lvl_square_db": 1.0, "lvl_sub_db": 1.0,
    # Noise level: not on the schematic; the three units span 3.2 dB (+0.7, +0.6, -2.5).
    "lvl_noise_db": 3.2,
    # Resonance: the RES readings are +-0.2 of a division (720p frames): the limit is ours' slope
    # of peak height against RES times 0.02 of travel, plus 1 dB for the lossy clip and the
    # harmonics sampling the peak.
    "res_reading_travel": 0.02, "res_meas_db": 1.0,
    # loopop's peak against RES: both ends are a peak on 65 Hz harmonics (parabola), about
    # +-0.04 oct each.
    "res_shift_oct": 0.10,
    # Portamento at maximum: the clip's two glides differ by 0.014 s; "near maximum" is read
    # off the knob as 10 (ours at 9.8 is given in the note).
    "porta_max_s": 0.06,
    "freqlaw_pct": 5.0,       # the reading's +-0.15 div systematic over the travel, about 3 %, and the fit
    # LFO top: the RATE slider is at its stop; the sideband spacing reads to +-0.2 Hz on each
    # side. The unit-to-unit spread at R31/R35 100k is not known (spread.md predates it).
    "lfo_top_hz": 0.5,
    # Noise colour: the four clips used for the noise corner span -6.8 to -7.9 dB/oct and
    # 0.95-1.1 kHz (model-osc.md, "Real units"): +-0.55 dB/oct and +-7 % about their middle;
    # doubled for what the saw on the same take doesn't take out (its harmonics are sparse above
    # 9 kHz, and a chain's own noise floor).
    "noise_slope_db_oct": 1.1, "noise_centroid_pct": 15.0,
    # The top's floor: the saw's level re 1/n reads to about +-0.3 dB per band (the three units'
    # notes agree that closely); 1 dB.
    "top_floor_db": 1.0,
    # FREQ 10's cutoff at KYBD 0: Starsky Carr's unit 13.93 kHz (RES 10), AutomaticGainsay's
    # 15.2 kHz (RES 7.1 peak): 0.13 octave apart.
    "top_oct": 0.15,
}
KNOWN = {
    "lfo": "known: improvements.md 'LFO's top rate' (unit spread: 33-42 Hz across the parts)",
    "freq": "known: improvements.md 'FREQ's bottom' (ours matches at FREQ 1.97 against the 1.5 +- 0.3 reading since the 22.8 mV/oct top fit)",
    "noise": "known: improvements.md 'Noise' (the three units span 2.7 dB re their saws)",
    "res": "known: improvements.md 'Resonance' (ours matches 0.4 division past this unit's reading; one unit)",
    "freqlaw": "known: improvements.md 'FREQ's law' (this unit 13-14 % steeper per division yet its top flattens; cause not settled)",
}


# --- rendering ----------------------------------------------------------------------------------

def note_name(midi):
    return f"{NOTE_NAMES[midi % 12]}{midi // 12 - 1}"


def spec(name, params, notes, tail=0.3):
    """notes: (note name, start s, length s). bpm 60 and 100 lines per beat: a line is 10 ms."""
    p = dict(BASE)
    p.update(params)
    ev = [{"line": round(t * 100, 3), "note": n, "len": round(d * 100, 3), "vel": 100} for n, t, d in notes]
    end = max(e["line"] + e["len"] for e in ev)
    return {"name": name, "bpm": 60, "lpb": 100, "lines": end, "tail": tail, "params": p, "notes": ev}


def render_all(specs, out, cal, jobs, binary):
    """Renders every spec (name -> spec) into out/NAME.wav with jobs sh101render processes."""
    os.makedirs(out, exist_ok=True)
    for _ in range(60):          # another session may be relinking the binary: wait for it
        if os.path.exists(binary) and os.access(binary, os.X_OK):
            break
        print("waiting for", binary, file=sys.stderr)
        time.sleep(10)
    items = sorted(specs.values(), key=lambda s: -s["lines"])
    chunks = [[] for _ in range(jobs)]
    load = [0.0] * jobs
    for s in items:                       # longest first onto the least loaded chunk
        k = int(np.argmin(load))
        chunks[k].append(s)
        load[k] += s["lines"] / 100 + s["tail"] + 0.3
    procs = []
    for k, c in enumerate(chunks):
        if not c:
            continue
        path = os.path.join(out, f"_specs{k}.json")
        json.dump(c, open(path, "w"))
        for attempt in range(3):
            try:
                procs.append(subprocess.Popen([binary, path, "--out", out, "--calibration", cal],
                                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE))
                break
            except OSError:
                time.sleep(10)
    for p in procs:
        _, err = p.communicate()
        if p.returncode != 0:
            raise SystemExit("sh101render failed: " + err.decode(errors="replace"))
    missing = [n for n in specs if not os.path.exists(os.path.join(out, n + ".wav"))]
    if missing:
        raise SystemExit("not rendered: " + ", ".join(missing[:5]))


class Ours:
    def __init__(self, out):
        self.out = out
        self.cache = {}

    def __call__(self, name):
        if name not in self.cache:
            self.cache[name] = A.load(os.path.join(self.out, name + ".wav"))
        return self.cache[name]


def real(name):
    return A.load(os.path.join(WAV, CLIP + name + ".wav"))


# --- rows ---------------------------------------------------------------------------------------

ROWS = []


def row(group, unit, event, qty, real_v, ours_v, diff, limit, fmt, dunit, status=None, note="", known=None):
    """fmt formats real/ours (with their unit); diff and limit are in dunit."""
    if status is None:
        if limit is None or not np.isfinite(diff):
            status = "info"
        elif abs(diff) <= limit:
            status = "ok"
        else:
            status = "known" if known else "OVER"
    if status == "known" and known:
        note = (note + "; " if note else "") + known
    f = lambda v: (fmt % v) if v is not None and np.isfinite(v) else "-"
    ROWS.append(dict(group=group, unit=unit, event=event, qty=qty, real=f(real_v), ours=f(ours_v),
                     diff=("%+.2f %s" % (diff, dunit)) if diff is not None and np.isfinite(diff) else "-",
                     limit=("%.2f %s" % (limit, dunit)) if limit is not None else "-", status=status, note=note))


def oct_(a, b):
    return math.log2(a / b) if a > 0 and b > 0 else float("nan")


# --- analysis helpers ---------------------------------------------------------------------------

def harm(x, sr, f, ks, pad=4):
    """Amplitude (dB) and refined frequency of harmonics k*f, k in ks, from one Hann FFT."""
    x = x - x.mean()
    n = 1 << int(math.ceil(math.log2(len(x) * pad)))
    w = np.hanning(len(x))
    X = np.abs(np.fft.rfft(x * w, n)) * 2 / w.sum()
    df = sr / n
    out = []
    for k in ks:
        c = k * f / df
        h = max(2, min(0.3 * f / df, 0.006 * k * f / df + 3 * pad))
        lo, hi = max(1, int(c - h)), min(len(X) - 2, int(c + h) + 1)
        j = lo + int(np.argmax(X[lo:hi]))
        a, b, cc = np.log(X[j - 1:j + 2] + 1e-30)
        den = a - 2 * b + cc
        p = 0.5 * (a - cc) / den if den != 0 else 0
        out.append((float((j + p) * df), float(20 * np.log10(X[j] + 1e-12))))
    return np.array(out)


def refine_f0(x, sr, guess, nh=8):
    """Fundamental from the first nh harmonics' peaks (a steady periodic segment)."""
    h = harm(x, sr, guess, range(1, nh + 1))
    ok = h[:, 1] > h[:, 1].max() - 40
    k = np.arange(1, nh + 1)
    return float(np.median(h[ok, 0] / k[ok]))


def steady(x, sr, drop_db=3):
    """The held stretch: where the 50 ms envelope is within drop_db of its median."""
    t, e = A.envelope(x, sr, 0.05, 0.01)
    med = np.median(e[e > e.max() * 0.1])
    ok = e > med * 10 ** (-drop_db / 20)
    i = np.argmax(ok)
    j = len(ok) - np.argmax(ok[::-1]) - 1
    return t[i] + 0.15, t[j] - 0.1


def segments(x, sr, fmin=18, fmax=1200, min_len=0.2, hop=0.025):
    """Stable-pitch stretches (start, end, Hz)."""
    t, f = A.pitch_track(x, sr, hop, 0.08, fmin, fmax)
    out, i = [], 0
    while i < len(f):
        if not np.isfinite(f[i]):
            i += 1
            continue
        j = i
        while j + 1 < len(f) and np.isfinite(f[j + 1]) and abs(np.log2(f[j + 1] / f[i])) < 0.04:
            j += 1
        if t[j] - t[i] >= min_len:
            out.append((t[i] + 0.06, t[j] - 0.03, float(np.median(f[i:j + 1]))))
        i = j + 1
    return out


def respeak(x, sr, t0, t1, f0, hop=0.01, win=0.02, fmax=16000):
    """Resonant-peak track (build/sh101/work/env/py/respeak.py): per frame the 1/f-undone,
    sixth-octave-smoothed spectrum's maximum above 1.5 f0. Rows (t, Hz, dB over the low bands)."""
    n = 1 << 15
    fr = np.fft.rfftfreq(n, 1 / sr)
    g = np.geomspace(1.5 * f0, fmax, 160)
    lo, hi = np.searchsorted(fr, g / 2 ** (1 / 12)), np.searchsorted(fr, g * 2 ** (1 / 12))
    w = int(win * sr)
    hw = np.hanning(w)
    wt = np.maximum(fr, 1) ** 2
    out = []
    for t in np.arange(t0, t1, hop):
        s = x[int(t * sr):int(t * sr) + w]
        if len(s) < w:
            break
        P = np.abs(np.fft.rfft(s * hw, n)) ** 2 * wt
        c = np.concatenate([[0], np.cumsum(P)])
        Ls = 10 * np.log10((c[hi] - c[lo]) / np.maximum(hi - lo, 1) + 1e-30)
        k = int(np.argmax(Ls))
        out.append((t, g[k], Ls[k] - np.median(Ls[:8]), 10 * np.log10(np.mean(s * s) + 1e-20)))
    return np.array(out)


def exp_decay_fit(t, y, p0):
    model = lambda p, t: p[0] + p[1] * np.exp(-t / p[2])
    p, _ = optimize.leastsq(lambda p: model(p, t) - y, p0)
    return p, float(np.sqrt(np.mean((model(p, t) - y) ** 2)))


# --- 1. ENV to cutoff ---------------------------------------------------------------------------

FN_SELFOSC = "friendly-noise_youtube_eStzYAZoOAI__selfosc_vcf_env_sweep_res_max_adsr_max"
FN_CUT = 17.0          # the video's edit cuts the release at 113.4 s (clip 17.0 s; online-references.md 1.1)
FN_SAW = 261.5         # the saw under it, C4 on this unit
FN_REL = 9.8           # seconds of release fitted, both sides (all the clip has)
LOOPOP = {  # take: (clip, onset search window s, FREQ, RES, DECAY, note length s); 1080p frames,
            # ENV 10, KYBD 0, A 0, S 0, VCA GATE (model.md, "A second unit")
    "A": ("loopop_youtube_3JBFoXgtHvE__vcf_env_decay_res_sweep_saw_c2_setA", (0.2, 0.6), 0.47, 0.65, 0.32, 3.4),
    "B": ("loopop_youtube_3JBFoXgtHvE__vcf_env_decay_res_sweep_saw_c2_setB", (0.1, 0.4), 0.39, 0.69, 0.48, 2.6),
}
FM_PING = "failed-muso_youtube_6NqaegXGbtE__vcf_res_full_env_half_cutoff_half"
ENV_WEIGHT = 0.90      # envCvScale, Real unit calibration (model.md, "Calibrations"); for the notes only
FM_ENVS = (0.4, 0.45, 0.5, 0.55, 0.6, 0.7)


def fn_patch():
    return dict(range=0, sawLevel=1, vcfFreq=0, vcfRes=1, vcfEnv=1, vcfKybd=1, vcaMode=0, attack=1, decay=1,
                sustain=1, release=1, trigger=0)


def topline(x, sr, hop=0.1, win=0.2, fmin=1000):
    n = 1 << 15
    fr = np.fft.rfftfreq(n, 1 / sr)
    m = fr > fmin
    ts, fs, ls = [], [], []
    for i in range(0, len(x) - int(win * sr), int(hop * sr)):
        s = x[i:i + int(win * sr)]
        sp = np.abs(np.fft.rfft(s * np.hanning(len(s)), n))[m]
        k = int(np.argmax(sp))
        ts.append((i + win * sr / 2) / sr)
        fs.append(fr[m][k])
        ls.append(20 * np.log10(sp[k] + 1e-12))
    return np.array(ts), np.array(fs), np.array(ls)


def release_track(x, sr, a, b, saw):
    """tools/compare/online_release_fit.py's tracker: the strongest peak off the saw's harmonics."""
    y = x[int(a * sr):int(b * sr)]
    f, t, S = signal.spectrogram(y, sr, nperseg=16384, noverlap=16384 - int(0.025 * sr))
    m = (f > 20) & (f < 9000)
    out = []
    for i in range(len(t)):
        L = 10 * np.log10(S[m, i] + 1e-20)
        pk, _ = signal.find_peaks(L, prominence=6)
        fr, lv = f[m][pk], L[pk]
        keep = [k for k in range(len(fr)) if fr[k] < 0.8 * saw or abs(fr[k] / saw - round(fr[k] / saw)) * saw > 0.03 * fr[k]]
        if keep:
            k = max(keep, key=lambda k: lv[k])
            out.append((a + t[i], fr[k], lv[k]))
    return np.array(out)


def fn_sweep(x, sr, saw):
    """Plateau (S 10: the ENV's peak), then log2 f = rest + D exp(-t/tau) fitted to FN_REL s of
    the release; the sweep is plateau over rest."""
    t, f, lv = topline(x, sr)
    lf = np.log2(f)
    loud = lv > lv.max() - 10
    ref = np.median(lf[loud & (lf > np.percentile(lf[loud], 50))])
    idx = np.where((np.abs(lf - ref) < 0.1) & loud)[0]
    run = max(np.split(idx, np.where(np.diff(idx) > 1)[0] + 1), key=len)
    plateau, t1 = 2 ** np.median(lf[run]), t[run[-1]]
    tr = release_track(x, sr, t1 - 0.2, min(len(x) / sr, t1 + FN_REL + 0.2), saw)
    sel = (tr[:, 0] >= t1 + 0.2) & (tr[:, 0] <= t1 + FN_REL) & (tr[:, 2] > tr[:, 2].max() - 15)
    p, rms = exp_decay_fit(tr[sel, 0] - t1, np.log2(tr[sel, 1]), [5.0, 6.0, 4.0])
    return dict(plateau=plateau, rest=2 ** p[0], tau=p[2], sweep=oct_(plateau, 2 ** p[0]), rms=rms)


def spec_onset(x, sr, a, b, f0=65.4, jump=4000):
    """The note's start in a resonant ENV pluck: the peak track's jump above jump Hz (2 ms hop)."""
    tr = respeak(x, sr, a, b, f0, hop=0.002, win=0.006)
    loud = tr[:, 3] > tr[:, 3].max() - 40
    i = int(np.argmax((tr[:, 1] > jump) & loud))
    return tr[i, 0] + 0.003


def pluck_fit(x, sr, ton, dur, f0=65.4, fmax_fit=8000):
    """loopop: the resonant peak's decay, log2 f = rest + D exp(-t/tau) from the note's start,
    fitted below fmax_fit (the top of the sweep is past what the clip carries)."""
    tr = respeak(x, sr, ton, ton + dur, f0)
    t, lf = tr[:, 0] - ton, medfilt(np.log2(tr[:, 1]), 5)
    sel = (lf < np.log2(fmax_fit)) & (t > 0.01)
    sel[:int(np.argmax(sel))] = False
    p, rms = exp_decay_fit(t[sel], lf[sel], [7.5, 6.0, 0.3])
    return dict(rest=2 ** p[0], sweep=p[1], tau=p[2], rms=rms)


def ping(x, sr, dur=2.4):
    """build/sh101/work/env/fm/ping.py: the self-oscillating ping's pitch, fitted with one
    exponential: peak and sustain."""
    w = int(0.002 * sr)
    e = np.sqrt(np.convolve(x * x, np.ones(w) / w, "same"))
    db = 20 * np.log10(e + 1e-12)
    k0 = int(np.argmax(db > db.max() - 20))
    seg = x[k0:k0 + int(dur * sr)]
    t, f = A.pitch_track(seg, sr, 0.005, 0.02, 30, 8000)
    lv = np.array([20 * np.log10(np.sqrt(np.mean(seg[int(tt * sr):int(tt * sr) + int(0.02 * sr)] ** 2)) + 1e-12) for tt in t])
    sel = np.isfinite(f) & (t > 0.008) & (lv > lv.max() - 15)
    tt, y = t[sel], np.log2(f[sel])
    p, _ = exp_decay_fit(tt, y, [y[-1], y[0] - y[-1], 0.3])
    return dict(first=2 ** y[0], sus=2 ** p[0], peak=2 ** (p[0] + p[1]), tau=p[2], sweep=p[1])


def env_specs():
    s = {"env-fn": spec("env-fn", fn_patch(), [("C-5", 0.5, 7.1)], tail=FN_REL + 0.7)}
    for k, (_, _, fq, rs, dc, ln) in LOOPOP.items():
        s[f"env-lp{k}"] = spec(f"env-lp{k}", dict(range=0, vcfFreq=fq, vcfRes=rs, vcfEnv=1, vcfKybd=0, vcaMode=1,
                                                 attack=0, decay=dc, sustain=0, release=0, trigger=0),
                               [("C-3", 0.5, ln)], tail=0.2)
    for e in FM_ENVS:   # make_scenes.py's failed-muso-ping: FREQ 5, RES 10, ENV 5 by ear; D, S inferred
        n = "env-fm-e%03d" % round(e * 1000)
        s[n] = spec(n, dict(sawLevel=0, vcfFreq=0.5, vcfRes=1, vcfEnv=e, vcfKybd=0, vcaMode=0, attack=0, decay=0.55,
                            sustain=0.5, release=0.1, trigger=1), [("C-3", 0.5, 3.6)], tail=0.3)
    return s


def env_rows(ours):
    L = LIMITS
    x, sr = real(FN_SELFOSC)
    r = fn_sweep(x[:int(FN_CUT * sr)], sr, FN_SAW)
    o = fn_sweep(*ours("env-fn"), 261.63)
    u, ev = "Friendly Noise", "RES 10 take: ENV 10, FREQ 0, KYBD 10, ADSR 10"
    row("env", u, ev, "plateau (ENV peak, S 10)", r["plateau"], o["plateau"], oct_(o["plateau"], r["plateau"]),
        L["fn_plateau_oct"], "%.0f Hz", "oct")
    row("env", u, ev, "rest (release fit)", r["rest"], o["rest"], oct_(o["rest"], r["rest"]), L["fn_rest_oct"],
        "%.1f Hz", "oct")
    row("env", u, ev, "sweep rest->plateau", r["sweep"], o["sweep"], o["sweep"] - r["sweep"], L["fn_sweep_oct"],
        "%.2f oct", "oct", note="ENV weight %.2f fitted here; this take implies %.3f" % (
            ENV_WEIGHT, ENV_WEIGHT * r["sweep"] / o["sweep"]))
    row("env", u, ev, "release tau (R 10)", r["tau"], o["tau"], o["tau"] - r["tau"], L["fn_tau_s"], "%.2f s", "s")
    for k, (clip, (a, b), fq, rs, dc, ln) in LOOPOP.items():
        x, sr = real(clip)
        ton = spec_onset(x, sr, a, b)
        r = pluck_fit(x, sr, ton, min(2.4, len(x) / sr - ton - 0.05))
        y, sr2 = ours(f"env-lp{k}")
        o = pluck_fit(y, sr2, spec_onset(y, sr2, 0.3, 0.8), min(2.4, ln))
        ev = f"set {k} (FREQ {fq*10:.1f}, RES {rs*10:.1f}, D {dc*10:.1f}, ENV 10)"
        row("env", "loopop", ev, "sweep (decay fit at onset)", r["sweep"], o["sweep"], o["sweep"] - r["sweep"],
            L["lp_sweep_oct"], "%.2f oct", "oct", note="implies ENV weight %.3f (ours %.2f)" % (
                ENV_WEIGHT * r["sweep"] / o["sweep"], ENV_WEIGHT))
        row("env", "loopop", ev, "rest (decay fit)", r["rest"], o["rest"], oct_(o["rest"], r["rest"]), None,
            "%.0f Hz", "oct", note="the unit's own cutoff trim (scenes README: ~0.4 oct off)")
        row("env", "loopop", ev, "decay tau", r["tau"], o["tau"], o["tau"] - r["tau"], None, "%.3f s", "s",
            note="DECAY read off video")
    x, sr = real(FM_PING)
    r = ping(x, sr)
    sw = {e: ping(*ours("env-fm-e%03d" % round(e * 1000))) for e in FM_ENVS}
    es = np.array(FM_ENVS)
    v = np.array([sw[e]["sweep"] for e in FM_ENVS])
    match = float(np.interp(r["sweep"], v, es)) if np.all(np.diff(v) > 0) else float("nan")
    ev = "ping: FREQ 5, RES 10, ENV 5 by ear (D, S guessed)"
    o = sw[0.5]
    row("env", "Failed Muso", ev, "ping peak->sustain", r["sweep"], o["sweep"], o["sweep"] - r["sweep"], None,
        "%.2f oct", "oct", note="ours matches at ENV %.2f (weight %.3f if ENV is at 5)" % (
            match * 10, ENV_WEIGHT * match / 0.5))
    row("env", "Failed Muso", ev, "ping sustain pitch", r["sus"], o["sus"], oct_(o["sus"], r["sus"]), None,
        "%.0f Hz", "oct", note="FREQ 5 by ear")


# --- 2. Source levels re the saw at 10 --------------------------------------------------------

PC = "perfect-circuit_youtube_biuWnvJ0f7I__"
GS = "gstormelectro_youtube_SsHcY4YQOfw__"
GS_CLONE = "gstormelectro_101-vco__gstormelectro_youtube_SsHcY4YQOfw__"   # left channel: the 101-VCO module
AG = "automaticgainsay_youtube_8jCX_EUNocI__"
SRC = {"saw": dict(sawLevel=1), "square": dict(sawLevel=0, pulseLevel=1), "sub 1 OCT": dict(sawLevel=0, subLevel=1, subMode=0),
       "sub 2 OCT": dict(sawLevel=0, subLevel=1, subMode=1), "25% sub": dict(sawLevel=0, subLevel=1, subMode=2),
       "noise": dict(sawLevel=0, noiseLevel=1)}
DIV = {"saw": 1, "square": 1, "sub 1 OCT": 2, "sub 2 OCT": 4, "25% sub": 4, "noise": 1}
# Takes: (unit, source, clip, window or None for the held stretch or "seg" for each note).
# Perfect Circuit: sliders at 10 (1080p), FREQ 10, F2; its sub_2oct clip switches the mode
# (windows as build/sh101/work/mixer/pc.py). gstormelectro: stills, sliders taken as 10, legato
# sequences. AutomaticGainsay (720p frames, readag.py): saw 10.1 at 93 s, sub 8.0 1 OCT with saw
# 10.2 from 102.5 s to 121.5 s, sub 10.1 2 OCT square at 228.5-231 s, sub 10.1 2 OCT pulse at
# 247.5-253.5 s, noise 9.9 at 284 s, FREQ 10.1 throughout.
LEVEL_SAW = {"Perfect Circuit": [(PC + "saw_f2", None)], "gstormelectro": [(GS + "saw_octave_seq_a2_a4", "seg")],
             "AutomaticGainsay": [(AG + "saw_only_ref_before_sub", "seg")]}
LEVEL_TAKES = [
    ("Perfect Circuit", "square", PC + "square_f2", None),
    ("Perfect Circuit", "sub 1 OCT", PC + "sub_1oct_square_only_f2", None),
    ("Perfect Circuit", "sub 1 OCT", PC + "sub_2oct_only_f2", (0.05, 0.7)),
    ("Perfect Circuit", "25% sub", PC + "sub_2oct_only_f2", (1.05, 1.9)),
    ("Perfect Circuit", "sub 2 OCT", PC + "sub_2oct_only_f2", (2.25, 2.95)),
    ("Perfect Circuit", "sub 1 OCT", PC + "sub_2oct_only_f2", (3.3, 4.4)),
    ("Perfect Circuit", "noise", PC + "noise_only", None),
    ("gstormelectro", "square", GS + "square_50pct_seq", "seg"),
    ("gstormelectro", "sub 1 OCT", GS + "sub_osc_1oct_square_only", "seg"),
    ("gstormelectro", "sub 2 OCT", GS + "sub_osc_2oct_square_only", "seg"),
    ("gstormelectro", "noise", GS + "noise_only", None),
    ("AutomaticGainsay", "sub 1 OCT", AG + "saw_plus_sub_1oct_level_rising", "mix"),
    ("AutomaticGainsay", "sub 1 OCT", AG + "saw_plus_sub_1oct_phrase", "mix"),
    ("AutomaticGainsay", "sub 2 OCT", AG + "sub_2oct_square_only_a", "seg"),
    ("AutomaticGainsay", "sub 2 OCT", AG + "sub_2oct_square_only_b", "seg"),
    ("AutomaticGainsay", "25% sub", AG + "sub_2oct_pulse25_only", "seg"),
    ("AutomaticGainsay", "noise", AG + "noise_only_held_vcf_85pct", None),
]
AG_MIX_FROM = {AG + "saw_plus_sub_1oct_level_rising": 2.35}   # the sub reaches 8.0 at 102.5 s (clip 2.3 s)
# The phrase clip starts at 122.0 s; the last overhead frame (saw 10.2, sub 8.0) is at 121.5 s and
# the camera then turns away: notes more than 2 s into the clip are info rows (reading stale).
AG_MIX_SEEN_UNTIL = {AG + "saw_plus_sub_1oct_phrase": 2.0, AG + "saw_plus_sub_1oct_level_rising": 9.9}
AG_SUB_MIX = 0.80          # the sub slider in the mix takes, read 8.0
LEVEL_NOTES = range(26, 98)  # MIDI notes ours may render (VCO), 8'


def vco_midi(f):
    return int(round(69 + 12 * math.log2(f / 440)))


def level_spec_name(src, midi, mix=False):
    return ("lvl-mix-%d" % midi) if mix else ("lvl-%s-%d" % (src.replace(" ", "").replace("%", "p"), midi))


def present(x, sr, f):
    """A harmonic at f stands 15 dB over the spectrum within +-40 % of it."""
    n = 1 << int(math.ceil(math.log2(len(x) * 4)))
    X = 20 * np.log10(np.abs(np.fft.rfft((x - x.mean()) * np.hanning(len(x)), n)) + 1e-12)
    fr = np.fft.rfftfreq(n, 1 / sr)
    near = np.abs(fr / f - 1) < 0.03
    around = (np.abs(fr / f - 1) < 0.4) & ~near
    return near.any() and X[near].max() - np.median(X[around]) > 15


def source_f0(s, sr, f, src):
    """The source's fundamental: YIN's pitch, or a sub-octave of it when the sub is there (YIN
    can lock onto the VCO or an octave of the sub)."""
    for j in {"sub 2 OCT": (2, 1), "25% sub": (2, 1), "sub 1 OCT": (1,), "mix": (1,)}.get(src, ()):
        g = f / 2 ** j
        if g > 15 and present(s, sr, g) and present(s, sr, 3 * g):
            f = g
            break
    return refine_f0(s, sr, f)


def note_segments(x, sr, src, div):
    """(start, end, source Hz, VCO Hz) of each held note."""
    out = []
    for a, b, f in segments(x, sr):
        if b - a < 0.15:
            continue
        s = x[int(a * sr):int(b * sr)]
        fs = source_f0(s, sr, f, src)
        out.append((a, b, fs, fs * div))
    return out


def level_jobs():
    """Every (unit, source, clip, window, src Hz, VCO Hz, mix) to measure, from the clips."""
    jobs = []
    for unit, takes in LEVEL_SAW.items():
        for clip, win in takes:
            jobs.append((unit, "saw", clip, win))
    jobs += LEVEL_TAKES
    out = []
    for unit, src, clip, win in jobs:
        x, sr = real(clip)
        if win is None:
            a, b = steady(x, sr)
            g = A.yin(x[int(a * sr):int(a * sr) + int(0.3 * sr)], sr, 18, 1200) if src != "noise" else 100.0
            fs = source_f0(x[int(a * sr):int(b * sr)], sr, g, src) if src != "noise" else 100.0
            out.append((unit, src, clip, (a, b), fs, fs * DIV[src], False))
        elif win == "seg" or win == "mix":
            t0 = AG_MIX_FROM.get(clip, 0.0)
            for a, b, fs, fv in note_segments(x[int(t0 * sr):], sr, "mix" if win == "mix" else src,
                                              2 if win == "mix" else DIV[src]):
                out.append((unit, src, clip, (a + t0, b + t0), fs, fv, win == "mix"))
        else:
            a, b = win
            s = x[int(a * sr):int(b * sr)]
            g = A.yin(s[:int(0.3 * sr)], sr, 18, 1200)
            fs = source_f0(s, sr, g, src)
            out.append((unit, src, clip, (a, b), fs, fs * DIV[src], False))
    return out


def level_specs(jobs):
    s = {}
    for unit, src, clip, win, fs, fv, mix in jobs:
        m = vco_midi(fv)
        for sname in (src, "saw"):
            n = level_spec_name(sname, m)
            s[n] = spec(n, SRC[sname], [(note_name(m), 0.3, 1.5)])
        if mix:
            n = level_spec_name(None, m, True)
            s[n] = spec(n, dict(sawLevel=1, subLevel=AG_SUB_MIX, subMode=0), [(note_name(m), 0.3, 1.5)])
            n = level_spec_name("sub 1 OCT", m)
            s[n] = spec(n, SRC["sub 1 OCT"], [(note_name(m), 0.3, 1.5)])
    for n in ("noise", "saw"):
        nm = level_spec_name(n, 57)
        s[nm] = spec(nm, SRC[n], [("A-3", 0.3, 3.0)])
    return s


def ours_seg(ours, name, t0=0.6, t1=1.7):
    y, sr = ours(name)
    return y[int(t0 * sr):int(t1 * sr)], sr


def rmsdb(x):
    x = x - x.mean()
    return 20 * np.log10(np.sqrt(np.mean(x * x)) + 1e-12)


def diff_curve(sr_x, sr_y, fx, fy, ks, fmax=3000, mindb=-35):
    """real - ours (dB) at harmonics ks of fx (real) and fy (ours), where ours is within mindb of
    its strongest and below fmax."""
    (x, sr), (y, sr2) = sr_x, sr_y
    hr, ho = harm(x, sr, fx, ks), harm(y, sr2, fy, ks)
    keep = (ho[:, 1] > ho[:, 1].max() + mindb) & (hr[:, 0] < fmax)
    return hr[keep, 0], hr[keep, 1] - ho[keep, 1]


class Chain:
    """The recording chain (and the take's gain): real - ours on the saw's harmonics, smoothed.
    A saw note whose median gain sits more than 1.5 dB off the others' is left out (said in the
    notes): the chain can't change within a phrase, so that note isn't the saw at 10."""
    def __init__(self):
        self.notes = []
        self.dropped = []

    def add(self, f, d, label):
        self.notes.append((np.asarray(f), np.asarray(d), label))

    def _kept(self):
        g = np.array([np.median(d) for _, d, _ in self.notes])
        mid = np.median(g)
        keep = [k for k in range(len(g)) if abs(g[k] - mid) <= 1.5]
        self.dropped = ["%s (%+.1f dB)" % (self.notes[k][2], g[k] - mid) for k in range(len(g)) if k not in keep]
        return keep

    def __call__(self, f):
        keep = self._kept()
        F = np.concatenate([self.notes[k][0] for k in keep])
        D = np.concatenate([self.notes[k][1] for k in keep])
        o = np.argsort(F)
        F, D = F[o], D[o]
        edges = np.geomspace(F.min() * 0.99, F.max() * 1.01, 24)
        cf, cd = [], []
        for a, b in zip(edges[:-1], edges[1:]):
            m = (F >= a) & (F < b)
            if m.sum():
                cf.append(np.exp(np.mean(np.log(F[m]))))
                cd.append(np.median(D[m]))
        return np.interp(np.log(f), np.log(cf), cd, left=np.nan, right=np.nan)


def band_db(x, sr, fc):
    # power in each half-octave band: density times the bin width, so 44.1 and 48 kHz agree
    f, p = signal.welch(x - x.mean(), sr, nperseg=4096)
    df = f[1] - f[0]
    return np.array([10 * np.log10(np.sum(p[(f >= c / 2 ** 0.25) & (f < c * 2 ** 0.25)]) * df + 1e-30) for c in fc])


def level_rows(ours, jobs):
    L = LIMITS
    chains = {u: Chain() for u in LEVEL_SAW}
    for unit, src, clip, (a, b), fs, fv, mix in jobs:
        if src != "saw":
            continue
        x, sr = real(clip)
        m = vco_midi(fv)
        y = ours_seg(ours, level_spec_name("saw", m))
        f, d = diff_curve((x[int(a * sr):int(b * sr)], sr), y, fs, 440 * 2 ** ((m - 69) / 12), range(1, int(3000 / fs) + 1))
        chains[unit].add(f, d, "saw %s at %.1f s" % (note_name(m).replace("-", ""), a))
    by_event, skipped = {}, {}
    for unit, src, clip, (a, b), fs, fv, mix in jobs:
        if src == "saw":
            continue
        x, sr = real(clip)
        seg = x[int(a * sr):int(b * sr)]
        m = vco_midi(fv)
        fo = 440 * 2 ** ((m - 69) / 12) / DIV[src]
        ch = chains[unit]
        if src == "noise":
            yo, sr2 = ours_seg(ours, level_spec_name("noise", 57), 0.5, 3.0)
            ys, _ = ours_seg(ours, level_spec_name("saw", 57), 0.5, 3.0)
            fc = 200 * 2 ** (np.arange(0, 8) / 2)          # 200 Hz - 2.26 kHz, half octaves
            d = band_db(seg, sr, fc) - band_db(yo, sr2, fc) - ch(fc)
            d = d[np.isfinite(d)]
            off, sd, n = float(np.mean(d)), float(np.std(d)), len(d)
            ours_lvl = rmsdb(yo) - rmsdb(ys)
            note, ev = "", "held key"
        elif mix:
            # AutomaticGainsay's sub with the saw at 10: the sub's odd harmonics (of the sub's
            # fundamental) against the saw's even ones in the same note, both against ours at
            # the same sliders; real at 10 = ours at 10 + that difference.
            ym = ours_seg(ours, level_spec_name(None, m, True))
            k = np.arange(1, int(3000 / fs) + 1)
            # is the sub sounding in this note? Odd against even harmonics of fs (k 3,5,7 against
            # 2,4,6): a saw alone on fs gives -2.3 dB, ours' mix at these sliders gives c_mix.
            hr = harm(seg, sr, fs, range(1, 8))[:, 1]
            hm = harm(ym[0], ym[1], fo, range(1, 8))[:, 1]
            c_real = np.mean(hr[[2, 4, 6]]) - np.mean(hr[[1, 3, 5]])
            c_mix = np.mean(hm[[2, 4, 6]]) - np.mean(hm[[1, 3, 5]])
            if abs(c_real - c_mix) > abs(c_real + 2.27):
                skipped.setdefault((unit, src, clip), []).append("%.1f s" % a)
                continue
            fr_, dr = diff_curve((seg, sr), ym, fs, fo, k, mindb=-60)
            odd = np.isin(np.round(fr_ / fs).astype(int) % 2, [1])
            if (~odd).sum() < 3 or odd.sum() < 3:
                continue
            saw_f, saw_d = fr_[~odd], dr[~odd]
            sel = odd & (fr_ >= 100) & (fr_ <= 2500)
            dd = dr[sel] - np.interp(np.log(fr_[sel]), np.log(saw_f), saw_d)
            off, sd, n = float(np.mean(dd)), float(np.std(dd)), int(sel.sum())
            ysub = ours_seg(ours, level_spec_name(src, m))
            ysaw = ours_seg(ours, level_spec_name("saw", m))
            ours_lvl = rmsdb(ysub[0]) - rmsdb(ysaw[0])
            note = "sub read 8.0 with the saw at 10, chain from the saw in the same note"
            ev = note_name(m).replace("-", "")
            if a > AG_MIX_SEEN_UNTIL.get(clip, 99):
                note += "; sliders last seen 121.5 s, %.1f s before this note" % (a + 0.5)
                ev += " [stale reading]"
        else:
            yo = ours_seg(ours, level_spec_name(src, m))
            ys = ours_seg(ours, level_spec_name("saw", m))
            f, d = diff_curve((seg, sr), yo, fs, fo, range(1, int(3000 / fs) + 1))
            sel = (f >= 100) & (f <= 2500)
            dd = d[sel] - ch(f[sel])
            dd = dd[np.isfinite(dd)]
            if len(dd) < 3:
                continue
            off, sd, n = float(np.mean(dd)), float(np.std(dd)), len(dd)
            ours_lvl = rmsdb(yo[0]) - rmsdb(ys[0])
            note = ""
            ev = note_name(m).replace("-", "")
        key = (unit, src, clip, ev)
        if ch.dropped and src != "noise" and not mix:
            note = (note + "; " if note else "") + "chain without " + ", ".join(ch.dropped)
        by_event.setdefault(key, []).append((ours_lvl + off, ours_lvl, off, sd, n, a, b, note, fv))
    for (unit, src, clip, ev), vals in by_event.items():
        rl = float(np.median([v[0] for v in vals]))
        ol = float(np.median([v[1] for v in vals]))
        sd = float(np.median([v[3] for v in vals]))
        n = int(sum(v[4] for v in vals))
        lim = L["lvl_noise_db"] if src == "noise" else L["lvl_square_db"] if src == "square" else L["lvl_sub_db"]
        when = ", ".join("%.1f-%.1f s" % (v[5], v[6]) for v in vals[:3]) + (" ..." if len(vals) > 3 else "")
        nt = vals[0][7]
        nt = (nt + "; " if nt else "") + "%s; %d harmonics/bands, sd %.1f dB; %s" % (
            clip.split("__")[1], n, sd, when if len(vals) == 1 else "%d notes, median: %s" % (len(vals), when))
        if (unit, src, clip) in skipped:
            nt += "; notes without the sub left out: " + ", ".join(skipped.pop((unit, src, clip)))
        stale = "[stale reading]" in ev
        ev = ev.replace(" [stale reading]", "")
        if ev != "held key":
            ev += " (VCO %.0f Hz)" % np.median([v[8] for v in vals])
        row("levels", unit, src + ", " + ev, "level re saw at 10", rl, ol, ol - rl, None if stale else lim,
            "%+.2f dB", "dB", note=nt, known=KNOWN["noise"] if src == "noise" else None)


# --- 3. Resonance -------------------------------------------------------------------------------

AG_RES = [  # AutomaticGainsay, saw on a held D3, cutoff swept by hand; RES read at 720p (+-0.2)
    ("automaticgainsay_youtube_VQXq7OmZCtY__vcf_sweep_res25_saw_d3_a", 0.23),
    ("automaticgainsay_youtube_VQXq7OmZCtY__vcf_sweep_res50_saw_d3", 0.47),
    ("automaticgainsay_youtube_VQXq7OmZCtY__vcf_sweep_res_edge_of_selfosc_saw_d3", 0.71),
]
RES_FREQS = np.round(np.arange(0.55, 0.8801, 0.005), 3)   # fine: the peak's height scallops between harmonics
RES_STEPS = (-0.10, -0.05, 0.0, 0.05, 0.10)
LP_RES = "loopop_youtube_3JBFoXgtHvE__vcf_resonance_raise_saw_c2"
LP_RISE = (1.5, 3.2)       # RES raised over 1.6 s (clip seconds); it is lowered again at 3.65-3.9 s,
                           # too fast to follow (five frames from +23 dB to nothing)
LP_H = (2.0, 18.0)                            # peak heights compared (dB over the fundamental)
LP_FREQS = np.round(np.arange(0.56, 0.8401, 0.02), 3)
LP_RESES = np.round(np.arange(0.0, 0.9501, 0.025), 3)


def res_peak(x, sr, f0):
    """A.resonant_peak on harmonics from harm(): peak Hz (parabola over k), its height over the
    fundamental with the saw's 1/n undone (dB), and the fundamental (dB)."""
    nh = int(min(60, 0.45 * sr / f0, 12000 / f0))
    h = harm(x, sr, f0, range(1, nh + 1), pad=2)
    w = 10 ** (h[:, 1] / 20) * np.arange(1, nh + 1)
    k = int(np.argmax(w[1:])) + 1
    kk = float(k)
    if 1 < k < nh - 1:
        y0, y1, y2 = 20 * np.log10(w[k - 1:k + 2])
        den = y0 - 2 * y1 + y2
        kk = k + (0.5 * (y0 - y2) / den if den != 0 else 0)
    return f0 * (kk + 1), float(20 * np.log10(w[k] / w[0])), float(h[0, 1])


def res_frames(x, sr, f0, win=0.1, hop=0.05, kmin=3, kmax=20):
    out = []
    lv = []
    for i in range(0, len(x) - int(win * sr), int(hop * sr)):
        lv.append(rmsdb(x[i:i + int(win * sr)]))
    top = max(lv)
    for j, i in enumerate(range(0, len(x) - int(win * sr), int(hop * sr))):
        if lv[j] < top - 25:
            continue
        s = x[i:i + int(win * sr)]
        g = A.yin(s, sr, f0 / 1.5, f0 * 1.5)
        if not np.isfinite(g) or abs(1200 * math.log2(g / f0)) > 40:
            continue
        pf, ph, _ = res_peak(s, sr, f0)
        if kmin * f0 <= pf <= kmax * f0:
            out.append((i / sr, pf, ph))
    return np.array(out)


def res_specs():
    s = {}
    for _, r in AG_RES:
        for dr in RES_STEPS:
            for fq in RES_FREQS:
                n = "res-ag-r%03d-f%03d" % (round((r + dr) * 1000), round(fq * 1000))
                s[n] = spec(n, dict(vcfFreq=fq, vcfRes=max(0.0, r + dr)), [("D-3", 0.2, 0.75)], tail=0.05)
    for fq in LP_FREQS:
        for r in LP_RESES:
            n = "res-lp-r%03d-f%03d" % (round(r * 1000), round(fq * 1000))
            s[n] = spec(n, dict(range=0, vcfFreq=fq, vcfRes=r), [("C-3", 0.2, 0.9)], tail=0.05)
    return s


def ours_curve(ours, prefix, r, freqs, f0):
    pts = []
    for fq in freqs:
        y, sr = ours("%s-r%03d-f%03d" % (prefix, round(r * 1000), round(fq * 1000)))
        pts.append(res_peak(y[int(0.55 * sr):int(0.95 * sr)], sr, f0)[:2])
    return np.array(pts)


def at_height(f, h, target):
    """log-interpolated frequency where a rising (h, f) track reaches target dB."""
    i = int(np.argmax(h >= target))
    if h[i] < target or i == 0:
        return float("nan")
    w = (target - h[i - 1]) / (h[i] - h[i - 1])
    return float(np.exp((1 - w) * np.log(f[i - 1]) + w * np.log(f[i])))


def res_rows(ours):
    L = LIMITS
    for clip, r in AG_RES:
        x, sr = real(clip)
        f0 = refine_f0(x[int(0.3 * sr):int(1.5 * sr)], sr, 146.8)
        fr = res_frames(x, sr, f0)
        fo = 146.83
        med = {}
        for dr in RES_STEPS:
            c = ours_curve(ours, "res-ag", r + dr, RES_FREQS, fo)
            cf = c[:, 0] * f0 / fo
            # ours at each real frame's peak: the median over one harmonic spacing either side,
            # which averages the scallop as the hand-swept real frames do
            oh = [np.median(c[np.abs(cf - f) <= f0, 1]) if np.any(np.abs(cf - f) <= f0) else np.nan for f in fr[:, 1]]
            med[dr] = float(np.nanmedian(oh))
        rh = float(np.median(fr[:, 2]))
        drs = np.array(RES_STEPS)
        hv = np.array([med[d] for d in drs])
        slope = float(np.polyfit(drs[1:4], hv[1:4], 1)[0])            # dB per unit travel at the reading
        lim = L["res_meas_db"] + abs(slope) * L["res_reading_travel"]
        match = float(np.interp(rh, hv, drs + r)) if np.all(np.diff(hv) > 0) and hv[0] <= rh <= hv[-1] else float("nan")
        row("res", "AutomaticGainsay", "D3, RES %.1f (cutoff swept)" % (r * 10), "peak over fundamental",
            rh, med[0.0], med[0.0] - rh, lim, "%+.1f dB", "dB", known=KNOWN["res"],
            note="%d frames, peak %.0f-%.0f Hz; limit 1 dB + %.0f dB/div x 0.2 div; ours matches at RES %s" % (
                len(fr), fr[:, 1].min(), fr[:, 1].max(), abs(slope) / 10,
                "%.2f" % (match * 10) if np.isfinite(match) else "outside %.1f-%.1f" % ((r - 0.1) * 10, (r + 0.1) * 10)))
    # loopop: the peak's frequency as RES rises at a fixed cutoff (C2), and as it falls again
    x, sr = real(LP_RES)
    f0 = refine_f0(x[int(0.2 * sr):int(1.5 * sr)], sr, 65.4)
    fr = res_frames(x, sr, f0, win=0.15, hop=0.05, kmin=2, kmax=40)
    fo = 65.41
    curves = {}
    for fq in LP_FREQS:
        curves[fq] = np.array([ours_curve(ours, "res-lp", r, [fq], fo)[0] for r in LP_RESES])
    o_lo = np.array([at_height(curves[fq][:, 0], curves[fq][:, 1], LP_H[0]) for fq in LP_FREQS]) * f0 / fo
    o_hi = np.array([at_height(curves[fq][:, 0], curves[fq][:, 1], LP_H[1]) for fq in LP_FREQS]) * f0 / fo
    for label, (t0, t1) in (("RES raised", LP_RISE),):
        sel = fr[(fr[:, 0] >= t0) & (fr[:, 0] <= t1)]
        r_lo, r_hi = at_height(sel[:, 1], sel[:, 2], LP_H[0]), at_height(sel[:, 1], sel[:, 2], LP_H[1])
        ok = np.isfinite(o_lo) & np.isfinite(o_hi)
        fq = float(np.interp(math.log(r_lo), np.log(o_lo[ok]), LP_FREQS[ok]))
        ohi = float(np.exp(np.interp(fq, LP_FREQS[ok], np.log(o_hi[ok]))))
        olo = float(np.exp(np.interp(fq, LP_FREQS[ok], np.log(o_lo[ok]))))
        rs, os_ = oct_(r_hi, r_lo), oct_(ohi, olo)
        row("res", "loopop", "C2, %s at a fixed cutoff (%.1f-%.1f s)" % (label, t0, t1),
            "peak shift %+.0f -> %+.0f dB" % LP_H, rs, os_, os_ - rs, L["res_shift_oct"], "%.2f oct", "oct",
            note="real %.0f -> %.0f Hz, ours %.0f -> %.0f Hz (ours' FREQ %.2f puts its +%.0f dB peak where the real one is)" % (
                r_lo, r_hi, olo, ohi, fq * 10, LP_H[0]))


# --- 4. Portamento ------------------------------------------------------------------------------

PORTA = [  # clip, unit, knob reading, glides kept (the longest, in time order), notes ours plays
    ("once-upon-a-synth_youtube_PFXO4ZRsVC4__portamento_on_max_time_glide_up_then_down", "Once Upon a Synth", 1.0),
    ("automaticgainsay_youtube_rA93zzTjk1A__portamento_on_glides_time_40_to_55pct", "AutomaticGainsay", None),
]
# The knob, from 720p frames (2026-11-13) and the video watched through (2026-11-15): PORTAMENTO
# ON at 611 s with the knob at 3, turned up to 10 at 621 s, before both long glides (620.96 and
# 624.22 s). The 360p reading recorded before (40-55 %) was wrong.
AG_KNOB = (1.0, 1.0)
# The short glides of the clip's first five seconds, with the knob at about 3 (clip seconds; the
# windows hold one note change each). The one at 5.0-5.2 s (616.75 s) is left out: it is at the
# edge of the knob's turn (617-621 s), and reads 3.4.
AG_SHORT = [(0.08, 0.42), (0.42, 0.75), (3.57, 3.87), (3.88, 4.35)]
AG_SHORT_KNOB = 0.30
AG_SHORT_READING = 0.03    # +- of travel
KNOBS = np.round(np.arange(0.30, 1.0001, 0.05), 3)


def clean_track(x, sr):
    t, f = A.pitch_track(x, sr, 0.01, 0.05, 30, 3000)
    ok = np.isfinite(f)
    idx = np.where(ok)[0]
    f = np.interp(np.arange(len(f)), idx, f[idx])         # gaps hold the neighbours' pitch
    c = 1200 * np.log2(f / 100.0)
    c = medfilt(c, 7)
    ref = medfilt(c, 21)
    bad = np.abs(c - ref) > 200
    c[bad] = np.interp(np.where(bad)[0], np.where(~bad)[0], c[~bad])
    return t, c, ok & ~bad


def glides(t, c, ok, n=2, min_cents=600):
    """The n longest glides that settle on their note: one-way stretches on voiced frames
    (wobbles under 3 cents a step ignored, no step over 150 cents: that is a new note), in time
    order."""
    d = np.diff(c)
    found, i = [], 0
    while i < len(d):
        sgn = np.sign(d[i]) if 3 < abs(d[i]) < 150 and ok[i] and ok[i + 1] else 0
        if sgn == 0:
            i += 1
            continue
        j = i
        while j + 1 < len(d) and ok[j + 2] and abs(d[j + 1]) < 150 and (np.sign(d[j + 1]) == sgn or abs(d[j + 1]) <= 3):
            j += 1
        k = max(i, j + 1 - int(round(0.2 / (t[1] - t[0]))))
        settled = abs(c[j + 1] - c[k]) < 0.05 * abs(c[j + 1] - c[i])   # near its note: the last 0.2 s moves < 5 %
        if abs(c[j + 1] - c[i]) >= min_cents and settled:
            found.append((t[j + 1] - t[i], i, j + 1))
        i = j + 1
    found = sorted(found, reverse=True)[:n]
    return sorted((i, j) for _, i, j in found)


def glide_tau(t, c, i, j, dur=None):
    if dur is not None:
        j = min(j, i + int(round(dur / (t[1] - t[0]))))
    tau, ci = A.exp_fit(t[i:j + 1], c[i:j + 1])
    return tau, 100.0 * 2 ** (c[i] / 1200), 100.0 * 2 ** (c[j] / 1200), t[j] - t[i]


def porta_real():
    out = []
    for clip, unit, knob in PORTA:
        x, sr = real(clip)
        t, c, ok = clean_track(x, sr)
        for k, (i, j) in enumerate(glides(t, c, ok)):
            tau, fa, fb, dur = glide_tau(t, c, i, j)
            kn = knob if knob is not None else AG_KNOB[k]
            out.append((unit, clip, k, kn, tau, fa, fb, dur, t[i]))
    return out


def porta_spec_name(unit, k, knob):
    return "porta-%s-%d-k%03d" % (unit.split()[0].lower(), k, round(knob * 1000))


def porta_specs(pr):
    s = {}
    for unit, clip, k, kn, tau, fa, fb, dur, t0 in pr:
        na, nb = note_name(vco_midi(fa)), note_name(vco_midi(fb))
        knobs = [1.0, 0.98, 0.95] if unit == "Once Upon a Synth" else sorted(set(KNOBS) | set(AG_KNOB))
        for kn2 in knobs:
            n = porta_spec_name(unit, k, kn2)
            s[n] = spec(n, dict(portamento=kn2, portaMode=0), [(na, 0.2, 3.5), (nb, 3.7, 5.0)], tail=0.2)
    for kn2 in short_knobs():
        n = "porta-agshort-k%03d" % round(kn2 * 1000)
        s[n] = spec(n, dict(portamento=kn2, portaMode=0), [("G-3", 0.2, 0.6), ("A-4", 0.8, 0.6)], tail=0.2)
    return s


def short_knobs():
    return [round(AG_SHORT_KNOB + d, 3) for d in (-AG_SHORT_READING, 0.0, AG_SHORT_READING)] + [0.36, 0.40]


def short_tau(t, f, a, b):
    """A short glide's time constant: the log-frequency distance to the settled pitch, fitted
    exponential between 80 % and 8 % of the step."""
    m = (t >= a) & (t <= b) & np.isfinite(f) & (f > 0)
    tt, ff = t[m], np.log2(f[m])
    if len(ff) < 12:
        return float("nan"), float("nan"), float("nan")
    tgt, st = np.median(ff[-8:]), ff[0]
    d, d0 = np.abs(ff - tgt), abs(st - tgt)
    sel = (d < 0.8 * d0) & (d > 0.08 * d0)
    if sel.sum() < 5:
        return float("nan"), 2 ** st, 2 ** tgt
    return float(-1 / np.polyfit(tt[sel], np.log(d[sel]), 1)[0]), 2 ** st, 2 ** tgt


def porta_rows(ours, pr):
    L = LIMITS
    for unit, clip, k, kn, tau, fa, fb, dur, t0 in pr:
        def ours_tau(knob):
            y, sr = ours(porta_spec_name(unit, k, knob))
            seg = y[int(3.6 * sr):int(8.5 * sr)]
            t, c, ok = clean_track(seg, sr)
            g = glides(t, c, ok, 1)
            if not g:
                return float("nan")
            return glide_tau(t, c, g[0][0], g[0][1], dur)[0]
        ev = "glide %d, %.0f -> %.0f Hz (%.1f s at %.1f s)" % (k + 1, fa, fb, dur, t0)
        if unit == "Once Upon a Synth":
            o = ours_tau(1.0)
            o98 = ours_tau(0.98)
            row("porta", unit, ev, "tau, knob at max", tau, o, o - tau, L["porta_max_s"], "%.3f s", "s",
                note="ours at 9.8: %.3f s" % o98)
        else:
            ks = sorted(set(KNOBS) | set(AG_KNOB))
            ts = np.array([ours_tau(q) for q in ks])
            ok = np.isfinite(ts)
            kk, tt = np.array(ks)[ok], ts[ok]
            match = float(np.interp(math.log(tau), np.log(tt), kk)) if np.all(np.diff(tt) > 0) else float("nan")
            o = ts[ks.index(kn)]
            row("porta", unit, ev, "tau, knob ~%d %%" % round(kn * 100), tau, o, o - tau, None, "%.3f s", "s",
                note="knob at 10 (the video); ours matches at knob %.2f (%.1f): the unit's own top, inside "
                     "the parts' spread (0.53-0.80 s at 10)" % (match, match * 10))
    # AutomaticGainsay's short glides at the knob's first position (about 3): the A taper's middle.
    x, sr = real(PORTA[1][0])
    t, f = A.pitch_track(x[:int(5.3 * sr)], sr, hop=0.002, win=0.025, fmin=40, fmax=1500)[:2]
    t, f = np.asarray(t), np.asarray(f)
    ks = short_knobs()
    ot = []
    for kn2 in ks:
        y, sr2 = ours("porta-agshort-k%03d" % round(kn2 * 1000))
        ty, fy = A.pitch_track(y, sr2, hop=0.002, win=0.025, fmin=40, fmax=1500)[:2]
        ot.append(short_tau(np.asarray(ty), np.asarray(fy), 0.75, 1.3)[0])
    ot = np.array(ot)
    lo, mid, hi = ot[0], ot[1], ot[2]
    for a, b in AG_SHORT:
        tau, fa, fb = short_tau(t, f, a, b)
        order = np.argsort(ks)
        match = float(np.interp(math.log(tau), np.log(ot[order]), np.array(ks)[order])) if np.isfinite(tau) else float("nan")
        lim = max(abs(hi - mid), abs(mid - lo))   # the knob reading's +- 0.3 division, in ours' tau
        row("porta", "AutomaticGainsay", "short glide %.0f -> %.0f Hz (%.2f-%.2f s)" % (fa, fb, a, b),
            "tau, knob ~3 (10 o'clock)", tau, mid, mid - tau, lim, "%.3f s", "s",
            note="ours %.3f-%.3f s across the reading; matches at knob %.2f" % (lo, hi, match * 10))


# --- 5. LFO top rate ----------------------------------------------------------------------------

LFO_CLIP = "perfect-circuit_youtube_biuWnvJ0f7I__lfo_rate_sweep"
LFO_TOP = (14.6, 17.0)   # the sweep has reached RATE 10 by 14.5 s (sidebands steady to the end)


def sideband_rate(x, sr, carrier):
    n = 1 << 19
    sp = np.abs(np.fft.rfft(x * np.hanning(len(x)), n))
    fr = np.fft.rfftfreq(n, 1 / sr)
    L = 20 * np.log10(sp + 1e-12)
    m = (fr > carrier - 260) & (fr < carrier + 260)
    pk, _ = signal.find_peaks(L[m], prominence=10)
    pk = pk[L[m][pk] > L[m][pk].max() - 25]
    f = np.sort(fr[m][pk])
    d = np.diff(f)
    d = d[d > 10]
    return float(np.median(d)), len(f)


def lfo_specs():
    return {"lfo-top": spec("lfo-top", dict(lfoRate=1.0, lfoWave=0, vcoMod=0.35), [("F-4", 0.2, 3.0)])}


def lfo_rows(ours):
    x, sr = real(LFO_CLIP)
    r, nr = sideband_rate(x[int(LFO_TOP[0] * sr):int(LFO_TOP[1] * sr)], sr, 350.0)
    y, sr2 = ours("lfo-top")
    o, no = sideband_rate(y[int(0.6 * sr2):int(3.0 * sr2)], sr2, 349.2)
    row("lfo", "Perfect Circuit", "RATE swept to its top (%.1f-%.1f s)" % LFO_TOP, "LFO rate at RATE 10", r, o, o - r,
        LIMITS["lfo_top_hz"], "%.2f Hz", "Hz", known=KNOWN["lfo"],
        note="sideband spacing round the 350 Hz note (%d / %d peaks); ours: VCO MOD on F4" % (nr, no))


# --- 6. FREQ's bottom ---------------------------------------------------------------------------

FN_SUB = "friendly-noise_youtube_eStzYAZoOAI__sub_only_res_max_seq"
FN_SUB_KCV = 1.90      # the lowest note's key CV on this unit (online-references.md 1.2)
FN_KYBD = 1.024        # the unit's tracking at KYBD 10, oct/oct (same)
FREQ_READ = (0.12, 0.15, 0.18)
FREQ_GRID = np.round(np.arange(0.10, 0.3001, 0.02), 3)
FREQ_NOTE = 59         # B-3 at 16': VCO 123.5 Hz, KCV (59 - 36)/12 = 1.917 V on ours


def note_rings(x, sr, a, b, sub=2, minlen=0.12):
    """tools/compare/online_notes.py: per note, the sub's fundamental and the self-oscillation
    (the strongest peak off the source's harmonics)."""
    y = x[int(a * sr):int(b * sr)]
    hop = int(0.005 * sr)
    env = np.array([np.sqrt(np.mean(y[i:i + hop] ** 2)) for i in range(0, len(y) - hop, hop)])
    db = 20 * np.log10(env + 1e-9)
    on = [i for i in range(3, len(db)) if db[i] - db[i - 3] > 6 and db[i] > db.max() - 40]
    notes = []
    for i in on:
        if notes and i - notes[-1] < int(minlen / 0.005):
            continue
        notes.append(i)
    notes.append(len(db))
    out = []
    for k in range(len(notes) - 1):
        s0 = (notes[k] + 8) * hop
        s1 = min(notes[k + 1] * hop, s0 + int(0.6 * sr))
        if s1 - s0 < int(0.06 * sr):
            continue
        w = y[s0:s1] * np.hanning(s1 - s0)
        n = 1 << 18
        sp = np.abs(np.fft.rfft(w, n))
        fr = np.fft.rfftfreq(n, 1 / sr)
        L = 20 * np.log10(sp + 1e-12)
        m = (fr > 20) & (fr < 8000)
        pk, _ = signal.find_peaks(L[m], prominence=12, distance=20)
        pf, pl = fr[m][pk], L[m][pk]
        if len(pf) == 0:
            continue
        f0 = pf[pl > pl.max() - 30].min()
        nonh = [(l, f) for f, l in zip(pf, pl) if abs(f / f0 - round(f / f0)) * f0 > 0.015 * f]
        if nonh:
            out.append((a + s0 / sr, f0, f0 * 2 ** sub, max(nonh)[1]))
    return out


def freq_specs():
    """Ours: the self-oscillation itself (sources off), FREQ x, RES 10, KYBD 10, at the key whose
    VCO is the real lowest note's: what the real sub take's ringing measures, without the sub's
    harmonics to tell apart. Held 3 s: at these cutoffs it takes a while to build up."""
    s = {}
    for fq in sorted(set(FREQ_GRID) | set(FREQ_READ)):
        n = "freq-f%03d" % round(fq * 1000)
        s[n] = spec(n, dict(range=0, sawLevel=0, vcfFreq=fq, vcfRes=1, vcfKybd=1), [(note_name(FREQ_NOTE), 0.2, 3.0)])
    return s


def freq_rows(ours):
    x, sr = real(FN_SUB)
    notes = [n for n in note_rings(x, sr, 0, len(x) / sr) if 2.2 < n[3] / n[1] < 3.3]   # ringing ~2.75 x the sub
    vco = np.array([n[2] for n in notes])
    low = [n for n in notes if n[2] < vco.min() * 1.02]
    ring = float(np.median([n[3] for n in low]))
    r0 = ring / 2 ** (FN_SUB_KCV * FN_KYBD)
    kcv = (FREQ_NOTE - 36) / 12

    def ours_at(fq):
        y, sr2 = ours("freq-f%03d" % round(fq * 1000))
        return A.f0_sine(y[int(2.2 * sr2):int(3.15 * sr2)], sr2, 5.0)

    grid = {fq: ours_at(fq) for fq in sorted(set(FREQ_GRID) | set(FREQ_READ))}
    g = np.array(sorted(grid))
    v = np.array([grid[q] for q in g]) / 2 ** kcv
    match = float(np.interp(math.log(r0), np.log(v), g)) if np.all(np.diff(v) > 0) else float("nan")
    o12, o15, o18 = (grid[q] / 2 ** kcv for q in FREQ_READ)
    span = abs(oct_(o18, o12)) / 2
    for fq, o in zip(FREQ_READ, (o12, o15, o18)):
        reading = fq == 0.15
        row("freq", "Friendly Noise", "sub take, lowest note x%d, FREQ %.1f%s" % (
            len(low), fq * 10, " (reading)" if reading else ""),
            "cutoff at KCV 0", r0, o, oct_(o, r0), span if reading else None, "%.1f Hz", "oct",
            known=KNOWN["freq"] if reading else None,
            note=("real: ringing %.1f Hz at KCV %.2f V, / 2^(KCV x %.3f); ours: self-oscillation at KCV %.2f V; "
                  "limit: half ours' span over FREQ 1.2-1.8; ours matches at FREQ %.2f"
                  % (ring, FN_SUB_KCV, FN_KYBD, kcv, match * 10)) if reading else "")


# --- 7. Noise colour, and the top of the filter -------------------------------------------------

# A saw at FREQ 10 on the same unit, same recording: its harmonics against the ideal 1/n give the
# unit's filter top and its recording chain together. Taken out of the noise clip's spectrum, what
# is left is the noise source's own colour; on its own, it is the top, chain included.
TOP_SAW = {"Perfect Circuit": (PC + "saw_f2", None, 87.3), "gstormelectro": (GS + "saw_octave_seq_a2_a4", "seg", None),
           "AutomaticGainsay": (AG + "saw_only_ref_before_sub", "seg", None)}
TOP_BANDS = [(5000, 7000), (7000, 9000), (9000, 11000), (11000, 13000)]
# Ours at each unit's settings, where the video shows them (others are info): Perfect Circuit's
# SH-101 at 1:48 has ENV 5.5 with S 10 (read 2026-11-16; "sliders at 10" before), AutomaticGainsay's
# FREQ 10.1 with ENV 0 (readag.py, 360p).
TOP_OURS = {"Perfect Circuit": "top-saw-F-2-env055", "AutomaticGainsay": "top-saw-F-2"}
TOP_EVENT = {"Perfect Circuit": "saw, FREQ 10, ENV 5.5 (S 10), RES 0", "AutomaticGainsay": "saw, FREQ 10, ENV 0, RES 0"}
NOISE_CLIPS = [  # unit, clip, window ("fm": as real.py, None: the held stretch), NOISE slider
    ("Failed Muso", "failed-muso_youtube_6NqaegXGbtE__noise_full_level_open_filter", "fm", 1.0),
    ("Failed Muso", "failed-muso_youtube_6NqaegXGbtE__noise_half_level_open_filter", "fm", 0.5),
    ("Perfect Circuit", PC + "noise_only", None, 1.0),
    ("gstormelectro", GS + "noise_only", None, 1.0),
    ("AutomaticGainsay", AG + "noise_only_held_vcf_85pct", None, 1.0),   # FREQ 10.1 in the 720p frames
]


def saw_tilt(x, sr, f0):
    """The saw's harmonics re the ideal 1/n (dB) up to 13 kHz, 0 dB at 200-800 Hz: (Hz, dB)."""
    f0 = refine_f0(x, sr, f0)
    ks = [k for k in range(1, 800) if k * f0 < 13000]
    h = harm(x, sr, f0, ks)
    lv = h[:, 1] + 20 * np.log10(np.array(ks))
    return h[:, 0], lv - np.median(lv[(h[:, 0] > 200) & (h[:, 0] < 800)])


def unit_tilt(unit):
    clip, win, f0 = TOP_SAW[unit]
    x, sr = real(clip)
    if win is None:
        a, b = steady(x, sr)
        return saw_tilt(x[int(a * sr):int(b * sr)], sr, f0)
    grid = np.linspace(100, 13000, 600)
    curves = [np.interp(grid, *saw_tilt(x[int(a * sr):int(b * sr)], sr, g)) for a, b, g in segments(x, sr)[:5]]
    return grid, np.median(curves, axis=0)


def ours_tilt(ours, name="top-saw-F-2"):
    y, sr = ours(name)
    return saw_tilt(y[int(0.6 * sr):int(3.0 * sr)], sr, 87.3)


def noise_colour(x, sr, tilt=None):
    """analyse.noise_spectrum's slope (200 Hz-8 kHz) and centroid (50 Hz-12 kHz), less a saw tilt."""
    f, p = signal.welch(x, sr, nperseg=8192)
    d = 10 * np.log10(p + 1e-30)
    if tilt is not None:
        d = d - np.interp(f, *tilt)
    band = (f > 200) & (f < 8000)
    slope = float(np.polyfit(np.log2(f[band]), d[band], 1)[0])
    m = (f > 50) & (f < 12000)
    q = 10 ** (d[m] / 10)
    return slope, float((q * f[m]).sum() / q.sum())


def noise_specs():
    s = {"noise-%03d" % round(v * 100): spec("noise-%03d" % round(v * 100), dict(sawLevel=0, noiseLevel=v),
                                             [("A-3", 0.2, 3.0)]) for v in (1.0, 0.5)}
    s["top-saw-F-2"] = spec("top-saw-F-2", dict(sawLevel=1), [("F-2", 0.2, 3.0)])
    # Perfect Circuit's take at 1:48 (1080p): FREQ 10, RES 0, ENV 5.5, KYBD 0, A 0, D 0, S 10, R 0.
    s["top-saw-F-2-env055"] = spec("top-saw-F-2-env055", dict(sawLevel=1, vcfEnv=0.55, attack=0, decay=0, sustain=1),
                                   [("F-2", 0.2, 3.0)])
    return s


def noise_rows(ours):
    L = LIMITS
    ot = ours_tilt(ours)
    for unit, clip, win, lvl in NOISE_CLIPS:
        x, sr = real(clip)
        a, b = (0.2, len(x) / sr - 0.1) if win == "fm" else steady(x, sr)
        ev = clip.split("__")[1] + " (%.1f-%.1f s)" % (a, b)
        y, sr2 = ours("noise-%03d" % round(lvl * 100))
        y = y[int(0.5 * sr2):int(3.0 * sr2)]
        if unit in TOP_SAW:
            rs, rc = noise_colour(x[int(a * sr):int(b * sr)], sr, unit_tilt(unit))
            os_, oc = noise_colour(y, sr2, ot)
            note = "less each side's own saw at FREQ 10 (the filter's top and the recording chain)"
            row("noise", unit, ev, "source slope 200 Hz-8 kHz", rs, os_, os_ - rs, L["noise_slope_db_oct"],
                "%.2f dB/oct", "dB/oct", note=note)
            row("noise", unit, ev, "source centroid", rc, oc, 100 * (oc / rc - 1), L["noise_centroid_pct"],
                "%.0f Hz", "%", note=note)
        else:
            rs, rc = noise_colour(x[int(a * sr):int(b * sr)], sr)
            os_, oc = noise_colour(y, sr2)
            note = ("no saw from this unit: its filter top and recording chain stay in (three units' tops "
                    "differ by 6 dB at 9-13 kHz, 'top' below)")
            row("noise", unit, ev, "slope 200 Hz-8 kHz", rs, os_, os_ - rs, None, "%.2f dB/oct", "dB/oct", note=note)
            row("noise", unit, ev, "centroid", rc, oc, 100 * (oc / rc - 1), None, "%.0f Hz", "%", note=note)


def top_rows(ours):
    """The saw at FREQ 10 re 1/n, per unit and band, against ours at that unit's settings. A lossy
    chain takes highs away and doesn't add them, so where the settings are seen the unit's recording
    is a floor: ours may sit above it, not more than the limit under it. Where they aren't, info."""
    for unit in TOP_SAW:
        rf, rl = unit_tilt(unit)
        r = [float(np.median(rl[(rf > lo) & (rf < hi)])) for lo, hi in TOP_BANDS]
        seen = unit in TOP_OURS
        of, ol = ours_tilt(ours, TOP_OURS.get(unit, "top-saw-F-2"))
        o = [float(np.median(ol[(of > lo) & (of < hi)])) for lo, hi in TOP_BANDS]
        for (lo, hi), rv, ov in zip(TOP_BANDS, r, o):
            d = ov - rv
            if seen:
                row("top", unit, TOP_EVENT[unit], "re 1/n at %d-%d kHz" % (lo // 1000, hi // 1000), rv, ov, d,
                    LIMITS["top_floor_db"], "%+.2f dB", "dB", status="ok" if d > -LIMITS["top_floor_db"] else "OVER",
                    note="a floor: the chain is in the real side, so ours may sit above, not under")
            else:
                row("top", unit, "saw, FREQ and ENV unseen", "re 1/n at %d-%d kHz" % (lo // 1000, hi // 1000), rv, ov,
                    d, None, "%+.2f dB", "dB", note="ours at FREQ 10, ENV 0; the take shows only stills")
        if unit == "gstormelectro":
            # The same take's other channel is GS_CLONE's VCO through the same chain: SH-101 less
            # clone leaves this unit's filter and output stage, if the module went out bare (a
            # 101-VCF is patched in the same case). Its KYBD is 0: the tilt is the same on A2, A3
            # and A4 within 0.4 dB. FREQ and ENV are unseen (the take shows only stills).
            cx, csr = A.load(os.path.join(WAV, GS_CLONE + "saw_octave_seq_a2_a4.wav"))
            grid = np.linspace(100, 13000, 600)
            cc = [np.interp(grid, *saw_tilt(cx[int(a * csr):int(b * csr)], csr, g)) for a, b, g in segments(cx, csr)[:5]]
            cl = np.median(cc, axis=0)
            c = [float(np.median(cl[(grid > lo) & (grid < hi)])) for lo, hi in TOP_BANDS]
            for (lo, hi), rv, cv, ov in zip(TOP_BANDS, r, c, o):
                row("top", unit, "saw less its 101-VCO, KYBD 0", "re 1/n at %d-%d kHz" % (lo // 1000, hi // 1000),
                    rv - cv, ov, ov - (rv - cv), None, "%+.2f dB", "dB",
                    note="FREQ and ENV unseen; chain out only if the module went out bare")


# --- 8. FREQ's law along its travel ------------------------------------------------------------

# AutomaticGainsay's unit (VQXq7OmZCtY): the filter self-oscillating (RES ~9.6, KYBD 0, a D3 saw
# on) while FREQ is swept by hand down and up. FREQ read per frame off the cap's index line at
# 720p (6.66 px/div; +-0.05 random, +-0.15 systematic between the printed ticks and the stops),
# the audio 0.115 s behind the video; 316 frame pairs, the down and up branches within 0.01 oct.
# Measured 2026-11-13 (build/sh101/work/sliders: freq_law_ag_table.csv); readings with 8 or more
# pairs. The absolute level is the unit's own (the expo's bias current spreads); the law's
# octaves per division is what is compared.
FL_AG = [(1.5, 10.86), (2.0, 16.81), (2.5, 26.26), (3.0, 40.05), (3.5, 62.40), (4.0, 98.31),
         (5.0, 234.17), (5.5, 339.58), (6.0, 534.51), (6.5, 814.14)]
FL_AG_HI = [(7.0, 1345.0), (8.0, 3644.0), (8.5, 5538.0)]   # from the study's summary table
# The same unit's top (VQXq7OmZCtY 199-218 s; private/sh101/reference/online/automaticgainsay_VQXq7OmZCtY):
# a saw on D3, RES 7.1, KYBD 0, FREQ pushed up by hand three times; the resonant peak stops at
# 15.08-15.23 kHz each time, its energy below the codec's 15.6 kHz band edge, not above it. FREQ's
# index line reads 9.8 just after the hand leaves (the top stop reads 10.0 at 96, 142 and 168 s).
AG_TOP_HZ, AG_TOP_READ, AG_TOP_RES = 15200.0, (9.8, 10.0), 0.71
AG_TOP_FREQS = np.round(np.arange(8.8, 10.0001, 0.05), 3)
# Starsky Carr's unit (q0yu1txLd8I 425.75-427.5 s, 1080p; private/sh101/reference/online/
# starsky-carr_q0yu1txLd8I): FREQ 10, RES 10, ENV 0, MOD 0, KYBD 0, sources off: 13.93 kHz, pure.
SC_TOP_HZ = 13930.0


def freqlaw_specs():
    s = {}
    for r, _ in FL_AG + FL_AG_HI:
        n = "freqlaw-f%03d" % round(r * 100)
        # sources off, RES 9.6, KYBD 0; the bender up for the first second so the oscillation is
        # running before the cutoff drops (at the bottom a held note alone builds too slowly)
        sp = spec(n, dict(sawLevel=0, pulseLevel=0, subLevel=0, noiseLevel=0, vcfRes=0.96, vcfFreq=r / 10,
                          vcfKybd=0, bendVcf=1.0), [("D-3", 0.2, 5.0)], tail=0.05)
        sp["bend"] = [{"line": 0, "value": 1.0}, {"line": 100, "value": 0.0}]
        s[n] = sp
    s["freqtop-sc"] = spec("freqtop-sc", dict(sawLevel=0, vcfFreq=1.0, vcfRes=1.0, vcfKybd=0), [("D-3", 0.2, 1.5)],
                           tail=0.05)
    for fq in AG_TOP_FREQS:
        n = "freqtop-f%04d" % round(fq * 100)
        s[n] = spec(n, dict(vcfFreq=fq / 10, vcfRes=AG_TOP_RES, vcfKybd=0), [("D-3", 0.2, 1.0)], tail=0.05)
    return s


def saw_peak_hz(y, sr, f0):
    """The resonant peak of a saw: the harmonic with the largest level x n, refined by a parabola."""
    nh = int(0.45 * sr / f0)
    h = harm(y, sr, f0, range(1, nh + 1), pad=2)
    w = 20 * np.log10(10 ** (h[:, 1] / 20) * np.arange(1, nh + 1) + 1e-12)
    k = int(np.argmax(w[2:])) + 2
    d = 0.0
    if k < nh - 1:
        den = w[k - 1] - 2 * w[k] + w[k + 1]
        d = 0.5 * (w[k - 1] - w[k + 1]) / den if den != 0 else 0.0
    return f0 * (k + 1 + d)


def freqlaw_rows(ours):
    pts = FL_AG + FL_AG_HI
    rr, rf, of = [], [], []
    for r, hz in pts:
        y, sr = ours("freqlaw-f%03d" % round(r * 100))
        o = A.f0_sine(y[int(4.0 * sr):int(5.0 * sr)], sr, fmin=4.0)
        rr.append(r), rf.append(hz), of.append(o)
        row("freqlaw", "AutomaticGainsay", "FREQ %.1f, self-oscillating, KYBD 0" % r, "cutoff", hz, o,
            oct_(o, hz), None, "%.1f Hz", "oct", note="absolute: the unit's own bias (spread)")
    rr, lr, lo = np.array(rr), np.log2(rf), np.log2(of)
    for name, a, b in (("1.5-3.5", 1.5, 3.5), ("3.5-6.5", 3.5, 6.5), ("6.0-8.5", 6.0, 8.5), ("1.5-8.5", 1.5, 8.5)):
        m = (rr >= a) & (rr <= b) & np.isfinite(lo)
        if m.sum() < 2:
            continue
        sr_, so_ = np.polyfit(rr[m], lr[m], 1)[0], np.polyfit(rr[m], lo[m], 1)[0]
        row("freqlaw", "AutomaticGainsay", "FREQ %s" % name, "oct per division", sr_, so_, 100 * (so_ / sr_ - 1),
            LIMITS["freqlaw_pct"], "%.3f", "%", known=KNOWN["freqlaw"])
    # The top, KYBD 0: two units, 0.13 octave apart (the limit is that spread, for each).
    y, sr = ours("freqtop-sc")
    o = A.f0_sine(y[int(1.0 * sr):int(1.6 * sr)], sr, fmin=1000.0)
    row("freqlaw", "Starsky Carr", "top: self-oscillating, FREQ 10, RES 10", "cutoff", SC_TOP_HZ, o, oct_(o, SC_TOP_HZ),
        LIMITS["top_oct"], "%.0f Hz", "oct", note="sliders read at 1080p; ENV, MOD, KYBD 0")
    pk = []
    for fq in AG_TOP_FREQS:
        y, sr = ours("freqtop-f%04d" % round(fq * 100))
        pk.append(saw_peak_hz(y[int(0.6 * sr):int(1.15 * sr)], sr, 146.83))
    pk = np.array(pk)
    at = float(np.interp(math.log2(AG_TOP_HZ), np.log2(pk), AG_TOP_FREQS)) if pk.max() > AG_TOP_HZ else math.nan
    o10 = float(pk[-1])
    row("freqlaw", "AutomaticGainsay", "top: saw D3, RES 7.1, FREQ %.1f-%.1f" % AG_TOP_READ, "resonant peak",
        AG_TOP_HZ, o10, oct_(o10, AG_TOP_HZ), LIMITS["top_oct"], "%.0f Hz", "oct",
        note="ours at FREQ 10" + ("; ours reaches it at FREQ %.2f" % at if np.isfinite(at) else "; ours stays under it"))


GROUPS = ["env", "levels", "res", "porta", "lfo", "freq", "noise", "top", "freqlaw"]


def print_table(rows, out):
    cols = [("group", "check"), ("unit", "unit"), ("event", "event"), ("qty", "quantity"), ("real", "real"),
            ("ours", "ours"), ("diff", "ours-real"), ("limit", "limit"), ("status", "status")]
    w = {k: max(len(h), *(len(r[k]) for r in rows)) for k, h in cols}
    w["event"] = min(w["event"], 48)
    lines = ["  ".join(h.ljust(w[k]) for k, h in cols) + "  note", "  ".join("-" * w[k] for k, _ in cols) + "  ----"]
    for r in rows:
        ev = r["event"] if len(r["event"]) <= 48 else r["event"][:47] + "~"
        cells = [r[k] if k != "event" else ev for k, _ in cols]
        lines.append("  ".join((c.rjust(w[k]) if k in ("real", "ours", "diff", "limit") else c.ljust(w[k]))
                               for c, (k, _) in zip(cells, cols)) + "  " + r["note"])
    print("\n".join(lines), file=out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=os.path.join(REPO, "build/sh101/work/units/renders"))
    ap.add_argument("--only", default=",".join(GROUPS))
    ap.add_argument("--calibration", default="real", choices=["real", "service", "roland"])
    ap.add_argument("--jobs", type=int, default=max(1, min(8, (os.cpu_count() or 4) - 2)))
    ap.add_argument("--render", default=os.environ.get("SH101_RENDER", RENDER))
    a = ap.parse_args()
    only = [g for g in a.only.split(",") if g]
    t0 = time.time()
    specs = {}
    lj = level_jobs() if "levels" in only else []
    pr = porta_real() if "porta" in only else []
    if "env" in only:
        specs.update(env_specs())
    if "levels" in only:
        specs.update(level_specs(lj))
    if "res" in only:
        specs.update(res_specs())
    if "porta" in only:
        specs.update(porta_specs(pr))
    if "lfo" in only:
        specs.update(lfo_specs())
    if "freq" in only:
        specs.update(freq_specs())
    if "noise" in only or "top" in only:
        specs.update(noise_specs())
    if "freqlaw" in only:
        specs.update(freqlaw_specs())
    out = os.path.join(a.out, a.calibration)
    render_all(specs, out, a.calibration, a.jobs, a.render)
    t_render = time.time() - t0
    ours = Ours(out)
    for g in GROUPS:
        if g not in only:
            continue
        if g == "env":
            env_rows(ours)
        elif g == "levels":
            level_rows(ours, lj)
        elif g == "res":
            res_rows(ours)
        elif g == "porta":
            porta_rows(ours, pr)
        elif g == "lfo":
            lfo_rows(ours)
        elif g == "freq":
            freq_rows(ours)
        elif g == "freqlaw":
            freqlaw_rows(ours)
        elif g == "noise":
            noise_rows(ours)
        elif g == "top":
            top_rows(ours)
    print("SH-101 against real units: Lemus (%s calibration, rendered now: %d specs in %.0f s) against %s"
          % (a.calibration, len(specs), t_render, os.path.relpath(WAV, REPO)))
    print_table(ROWS, sys.stdout)
    n = {s: sum(r["status"] == s for r in ROWS) for s in ("ok", "OVER", "known", "info")}
    verdict = "PASS" if n["OVER"] == 0 else "FAIL"
    print("\n%s: %d rows: %d ok, %d OVER, %d known, %d info (%.0f s)" % (
        verdict, len(ROWS), n["ok"], n["OVER"], n["known"], n["info"], time.time() - t0))
    return 0 if n["OVER"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
