#!/usr/bin/env python3
"""First comparison of the MS-20 model against recordings of real units (docs/compare.md).

    first.py [--render PATH] [--out DIR] [--only vco1,vco2,noise,selfosc] [--scan]

Reads the YouTube clips from machines/ms20/private/reference/youtube/<id>/<id>.48k.wav (private,
never in the repo), renders the model with ms20render (core only, SIGNAL OUT and probed board
signals) into --out, and prints one table per question:

  vco1     VCO 1's saw, triangle and square (PW 0) at one LEVEL, t8t17yEHHUQ 12-38 s, and the PW
           sweep 38-57 s: H1 ratios against the model's at SIGNAL OUT, the swing they imply
  vco2     VCO 2's saw, square and narrow pulse, t8t17yEHHUQ 80-101 s, and both saws in unison
           107.5-108.8 s: the narrow pulse's duty from its spectral nulls, H1 ratios, the swing
  noise    PINK (2d8UXDNJvkI 9.1-12.9 s, patched to EXT SIGNAL IN) against VCO 1's WHITE
           (t8t17yEHHUQ 187.0-202.5 s, same unit and owner), and pink + white in one take
           (2d8UXDNJvkI 55.6-58.2 s): third-octave spectra, the pink/white slope; the model's noise
           block probed (ms20render can't patch PINK into EXT SIGNAL IN)
  selfosc  the Rev 1 self-oscillation events (YH3g01qU7O8 668-683 s at 386 Hz, 637.6-639.0 s at
           1042.6 Hz) against the model's LPF self-oscillation locus over PEAK; --scan also lists
           every near-pure-tone run in the Rev 1 clips (slow: about a minute per 15 minutes of audio)

Levels are compared only as ratios within one recording (a YouTube clip's gain is unknown); every
number says which clip and time range it comes from. The panel settings of the atoooy clips
(t8t17yEHHUQ, 2d8UXDNJvkI) were read from their videos, not from this script (docs/compare.md).
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np
from scipy.io import wavfile
from scipy.optimize import least_squares
from scipy.signal import welch

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.abspath(os.path.join(HERE, "../.."))
CLIPS = os.path.join(MACHINE, "private", "reference", "youtube")
DEFAULT_RENDER = os.path.abspath(os.path.join(MACHINE, "../../../build/ms20/ms20render"))

T8 = "t8t17yEHHUQ"  # atoooy part 1: labelled waveforms (vintage, filter revision not stated)
D2 = "2d8UXDNJvkI"  # atoooy part 3: pink vs white noise (same unit)
YH3 = "YH3g01qU7O8"  # Meuchine: vintage Rev 1, dry, 15 min
REV1 = ["96N82NCbKlQ", "YH3g01qU7O8", "1MXo9aPZETA", "tIb5X9X_tPk", "dxO5ywTETIY"]


def db(v):
    return 20 * np.log10(np.maximum(np.abs(v), 1e-30))


def pdb(v):
    return 10 * np.log10(np.maximum(v, 1e-30))


# ------------------------------------------------------------------ audio and analysis helpers

_cache = {}


def clip(vid):
    """A clip as mono float (the mean of both channels; every clip used here is mono in content)."""
    if vid not in _cache:
        sr, x = wavfile.read(os.path.join(CLIPS, vid, vid + ".48k.wav"))
        x = x.astype(np.float64) / 32768.0
        _cache[vid] = (sr, x.mean(axis=1) if x.ndim > 1 else x)
    return _cache[vid]


def seg(vid, t0, t1):
    sr, x = clip(vid)
    return sr, x[int(t0 * sr):int(t1 * sr)]


def f0_est(x, sr, fmin, fmax):
    """The strongest spectral peak between fmin and fmax, parabolic interpolation on a zero-padded
    Hann spectrum (for these clips the fundamental; pass a band that holds it)."""
    n = len(x)
    N = 1 << int(np.ceil(np.log2(n * 8)))
    X = np.abs(np.fft.rfft((x - x.mean()) * np.hanning(n), N))
    f = np.fft.rfftfreq(N, 1 / sr)
    k = int(np.argmax(X * ((f > fmin) & (f < fmax))))
    a, b, c = np.log(X[k - 1:k + 2] + 1e-30)
    return (k + 0.5 * (a - c) / (a - 2 * b + c)) * sr / N


def harmonics(x, sr, f0, nh):
    """Amplitudes and phases of nh harmonics of f0 by least squares (Hann-weighted, DC included):
    a window holds many periods, so this is a per-event average, not a single cycle."""
    t = np.arange(len(x)) / sr
    w = np.hanning(len(x))
    cols = [np.ones_like(t)]
    for h in range(1, nh + 1):
        cols += [np.cos(2 * np.pi * h * f0 * t), np.sin(2 * np.pi * h * f0 * t)]
    A = np.array(cols).T
    c, *_ = np.linalg.lstsq(A * w[:, None], x * w, rcond=None)
    return np.hypot(c[1::2], c[2::2]), np.arctan2(-c[2::2], c[1::2])


def event(vid, t0, t1, fmin, fmax, nh=12):
    sr, x = seg(vid, t0, t1)
    f0 = f0_est(x, sr, fmin, fmax)
    a, ph = harmonics(x, sr, f0, min(nh, int(20000 / f0)))
    return f0, a, ph


def two_series(x, sr, f1, f2, nh=6):
    """Two harmonic series at once (two free-running VCOs in one take): amplitudes of each, with
    f1 and f2 refined on a 0.05 Hz grid by the residual."""
    t = np.arange(len(x)) / sr
    w = np.hanning(len(x))

    def fit(g1, g2):
        cols = [np.ones_like(t)]
        for g in (g1, g2):
            for k in range(1, nh + 1):
                cols += [np.cos(2 * np.pi * k * g * t), np.sin(2 * np.pi * k * g * t)]
        A = np.array(cols).T
        c, *_ = np.linalg.lstsq(A * w[:, None], x * w, rcond=None)
        r = x * w - (A * w[:, None]) @ c
        a = np.hypot(c[1::2], c[2::2])
        return a[:nh], a[nh:], float(np.sum(r ** 2))

    best = min(((fit(f1 + d1, f2 + d2)[2], f1 + d1, f2 + d2)
                for d1 in np.linspace(-0.3, 0.3, 13) for d2 in np.linspace(-0.3, 0.3, 13)))
    a1, a2, e = fit(best[1], best[2])
    return best[1], best[2], a1, a2, 1 - e / np.sum((x * w) ** 2)


def duty_from_nulls(a, f0, fmax=14500):
    """A rectangular pulse of duty d has |sin(pi n d)|/n harmonics: nulls at n = 1/d, 2/d...
    Fits d with the chain's roll-off as a free quadratic (in dB, against frequency); also returns
    the harmonics that sit 8 dB or more under both neighbours (the nulls themselves)."""
    n = np.arange(1, len(a) + 1)
    keep = n * f0 < fmax
    n, y = n[keep], db(a[keep] / a[0])
    F = n * f0 / 1000.0
    A = np.vstack([F, F ** 2]).T
    best = None
    for d in np.arange(0.02, 0.25, 0.0005):
        s = np.abs(np.sinc(n * d))
        r = y - db(s / s[0] + 1e-6)
        c, *_ = np.linalg.lstsq(A, r, rcond=None)
        e = np.sum(np.minimum(np.abs(r - A @ c), 12) ** 2)
        if best is None or e < best[0]:
            best = (e, d)
    nulls = [int(n[k]) for k in range(1, len(n) - 1) if y[k] < y[k - 1] - 8 and y[k] < y[k + 1] - 8]
    return best[1], nulls


def thirds(x, sr, fmin=25, fmax=16000):
    """Third-octave band PSD (Welch, 8192-point Hann segments), V^2/Hz averaged over each band."""
    f, P = welch(x, sr, nperseg=8192)
    cs = 1000 * 2 ** (np.arange(-16, 15) / 3)
    cs = cs[(cs >= fmin) & (cs <= fmax)]
    return cs, np.array([P[(f >= c * 2 ** (-1 / 6)) & (f < c * 2 ** (1 / 6))].mean() for c in cs])


def rms_frames(vid, t0, t1, hop=0.05):
    sr, x = seg(vid, t0, t1)
    h = int(hop * sr)
    return np.array([t0 + i * hop for i in range(len(x) // h)]), \
        np.array([db(np.sqrt(np.mean(x[i * h:(i + 1) * h] ** 2))) for i in range(len(x) // h)])


def half_ratio(x):
    """Median positive half-period over median negative half-period, from interpolated zero
    crossings: 1 for a symmetric wave. A recording's polarity is unknown, so compare r with 1/r."""
    x = x - x.mean()
    z = np.where(np.diff(np.sign(x)) != 0)[0]
    tz = np.array([i + x[i] / (x[i] - x[i + 1]) for i in z])
    d = np.diff(tz)
    pos = [d[k] for k in range(len(d)) if x[z[k] + 1] > 0]
    neg = [d[k] for k in range(len(d)) if x[z[k] + 1] <= 0]
    return float(np.median(pos) / np.median(neg))


def h2_phase(a, ph):
    """H2's phase against twice H1's, degrees (inverting the signal moves it by 180)."""
    return float(np.degrees((ph[1] - 2 * ph[0] + np.pi) % (2 * np.pi) - np.pi))


# ------------------------------------------------------------------ the model

class Model:
    def __init__(self, render, out):
        self.render, self.out = render, out
        os.makedirs(out, exist_ok=True)

    def run(self, name, key, sets, seconds=1.2, on=0.1, probes=("mix", "sigout"), rate=48000):
        """One held key, the panel controls in sets (ms20render's parameter ids); returns the
        sample rate and a dict of SIGNAL OUT ('sigout' from the WAV) and the probed signals."""
        wav = os.path.join(self.out, name + ".wav")
        csv = os.path.join(self.out, name + ".csv")
        cmd = [self.render, wav, "--rate", str(rate), "--seconds", str(seconds),
               "--note", "%d,%g,%g" % (key, on, seconds - 0.02), "--probe", ",".join(probes), "--csv", csv]
        for k, v in sets.items():
            cmd += ["--set", "%s=%g" % (k, v)]
        subprocess.run(cmd, check=True, capture_output=True)
        d = np.genfromtxt(csv, delimiter=",", names=True)
        return rate, {p: d[p] for p in d.dtype.names}

    def h1(self, name, key, sets, probe="sigout", t0=0.5, t1=1.1, band=(80, 3000), nh=10):
        sr, d = self.run(name, key, sets)
        x = d[probe][int(t0 * sr):int(t1 * sr)]
        f0 = f0_est(x, sr, *band)
        a, ph = harmonics(x, sr, f0, nh)
        return f0, a, ph


OPEN = {"lpf_cutoff": 10, "hpf_cutoff": 0, "lpf_peak": 0, "hpf_peak": 0}


def table(head, rows):
    print("| " + " | ".join(head) + " |")
    print("|" + "---|" * len(head))
    for r in rows:
        print("| " + " | ".join(r) + " |")
    print()


# ------------------------------------------------------------------ question 1a: VCO 1

# steady parts of held notes (between retriggers), VCO 1 alone, t8t17yEHHUQ
VCO1_EVENTS = {
    ("saw", 520): [(12.8, 13.3), (14.2, 15.0), (16.4, 17.4)],
    ("saw", 1042): [(17.8, 18.7), (19.8, 20.4)],
    ("tri", 520): [(22.4, 23.4), (23.8, 24.5), (24.8, 25.6)],
    ("tri", 1042): [(26.2, 26.75), (27.4, 28.5)],
    ("sq", 520): [(32.6, 33.5), (34.0, 34.5), (35.8, 36.3)],
    ("sq", 1042): [(35.0, 35.5), (36.6, 37.7)],
}


def q_vco1(model):
    print("## VCO 1: saw, triangle, square at one LEVEL (t8t17yEHHUQ)\n")
    rec = {}
    rows = []
    for (wave, f), wins in VCO1_EVENTS.items():
        h1s = []
        for t0, t1 in wins:
            f0, a, _ = event(T8, t0, t1, f * 0.8, f * 1.2, 10)
            h1s.append(db(a[0]))
            rel = db(a / a[0])
            rows.append([wave, "%.1f-%.1f s" % (t0, t1), "%.2f" % f0, "%.2f" % db(a[0]),
                         " ".join("%.1f" % v for v in rel[1:7])])
        rec[(wave, f)] = (np.mean(h1s), np.std(h1s))
    table(["wave", "window", "f0 (Hz)", "H1 (dBFS)", "H2..H7 re H1 (dB)"], rows)

    # the model at SIGNAL OUT, LEVEL near the video's reading (10)
    mod = {}
    for lvl in (10, 9.5, 9, 8):
        for key, f in ((24, 520), (36, 1042)):
            for wave, w in (("tri", 0), ("saw", 1), ("sq", 2)):
                s = dict(OPEN, vco1_level=lvl, vco2_level=0, vco1_wave=w, vco1_pw=0)
                f0, a, _ = model.h1("v1_%s_%d_%g" % (wave, key, lvl), key, s, band=(300, 1500))
                mod[(lvl, wave, f)] = db(a[0])
    rows = []
    for f in (520, 1042):
        r = {w: rec[(w, f)][0] for w in ("saw", "tri", "sq")}
        rows.append(["recording, %d Hz" % f, "%+.2f" % (r["saw"] - r["sq"]), "%+.2f" % (r["tri"] - r["sq"]),
                     "%+.2f" % (r["saw"] - r["tri"])])
        for lvl in (10, 9.5, 9, 8):
            m = {w: mod[(lvl, w, f)] for w in ("saw", "tri", "sq")}
            rows.append(["model LEVEL %g, key %d" % (lvl, 24 if f == 520 else 36), "%+.2f" % (m["saw"] - m["sq"]),
                         "%+.2f" % (m["tri"] - m["sq"]), "%+.2f" % (m["saw"] - m["tri"])])
    print("H1 ratios at SIGNAL OUT (dB). Saw and triangle scale with the core's swing, the square "
          "does not; saw/triangle doesn't depend on the swing but on LEVEL's loading of the saw's 100k.\n")
    table(["", "saw/square", "triangle/square", "saw/triangle"], rows)

    # LEVEL from saw/triangle (swing-free), then the swing from triangle/square at that LEVEL
    for f in (520, 1042):
        r_st = rec[("saw", f)][0] - rec[("tri", f)][0]
        lv = np.array([8, 9, 9.5, 10])
        st = np.array([mod[(l, "saw", f)] - mod[(l, "tri", f)] for l in lv])
        tq = np.array([mod[(l, "tri", f)] - mod[(l, "sq", f)] for l in lv])
        L = float(np.interp(r_st, st[::-1], lv[::-1]))
        tq_m = float(np.interp(L, lv, tq))
        d = rec[("tri", f)][0] - rec[("sq", f)][0] - tq_m
        print("- %d Hz: saw/triangle %+.2f dB puts LEVEL at %.2f in the model; there triangle/square is "
              "%+.2f dB against the recording's %+.2f: the swing x %.3f (the model's 5.46 Vpp -> %.2f Vpp)"
              % (f, r_st, L, tq_m, rec[("tri", f)][0] - rec[("sq", f)][0], 10 ** (d / 20), 5.46 * 10 ** (d / 20)))
    print()

    # the PW sweep: where the pulse goes away
    t, r = rms_frames(T8, 38.0, 57.0)
    gone = t[r < -55]
    runs = []
    for v in gone:
        if runs and v - runs[-1][1] <= 0.051:
            runs[-1][1] = v
        else:
            runs.append([v, v])
    sq = np.median(r[(t > 46.0) & (t < 48.0)])
    # a key released drops straight from the square's level; the pulse narrowing away fades first
    faded, gaps = [], []
    for a, b in runs:
        before = r[(t >= a - 0.3) & (t < a)]
        (faded if len(before) and before.max() < sq - 10 else gaps).append("%.2f-%.2f s" % (a, b + 0.05))
    print("PW sweep 38-57 s (50 ms RMS): square %.1f dBFS; under -55 dBFS (the floor between notes is "
          "about -66) after fading 10 dB or more below the square: %s (the pulse narrowed away); "
          "straight from the square's level: %s (keys released). The model's pulse goes at PW 7.8 "
          "(5.46 Vpp against the 3.49 V top of PW's range); a saw over 6.98 Vpp would never lose it.\n"
          % (sq, ", ".join(faded), ", ".join(gaps)))


# ------------------------------------------------------------------ question 1b: VCO 2

VCO2_EVENTS = {
    ("saw", 1059): [(86.45, 86.75)],
    ("saw", 2121): [(86.9, 87.8)],
    ("saw", 986): [(80.1, 80.65)],
    ("sq", 1059): [(88.05, 88.4), (88.9, 89.05)],
    ("sq", 2121): [(88.55, 88.8), (90.8, 92.0)],
    ("narrow", 1059): [(92.6, 92.85), (93.4, 93.55), (93.95, 94.1), (96.0, 96.4)],
    ("narrow", 2121): [(93.05, 93.25), (93.65, 93.8), (94.7, 95.85)],
}
# (the notes alternate 1059 / 2121 Hz every 0.15-0.4 s from 88 s: each window is inside one note,
# from a 50 ms pitch track)
NARROW_NULL_WINDOWS = [(96.45, 96.9, 500, 560), (97.0, 98.0, 250, 280), (99.0, 100.0, 250, 280)]


def q_vco2(model):
    print("## VCO 2: saw, square, narrow pulse at one LEVEL (t8t17yEHHUQ)\n")
    rec = {}
    rows = []
    for (wave, f), wins in VCO2_EVENTS.items():
        h1s = []
        for t0, t1 in wins:
            f0, a, _ = event(T8, t0, t1, f * 0.9, f * 1.1, 10)
            h1s.append(db(a[0]))
            rows.append([wave, "%.2f-%.2f s" % (t0, t1), "%.2f" % f0, "%.2f" % db(a[0]),
                         " ".join("%.1f" % v for v in db(a / a[0])[1:7])])
        rec[(wave, f)] = np.mean(h1s)
    table(["wave", "window", "f0 (Hz)", "H1 (dBFS)", "H2..H7 re H1 (dB)"], rows)

    print("Narrow pulse duty from the nulls of its spectrum (|sin(pi n d)|/n; a chain roll-off fitted "
          "alongside). The windows from 97 s are VCO 2 at 16' (264.6 Hz, H2 is the strongest line).\n")
    rows = []
    for t0, t1, lo, hi in NARROW_NULL_WINDOWS:
        sr, x = seg(T8, t0, t1)
        f0 = f0_est(x, sr, lo, hi)
        a, _ = harmonics(x, sr, f0, int(15000 / f0))
        d, nulls = duty_from_nulls(a, f0)
        rows.append(["%.2f-%.2f s" % (t0, t1), "%.2f" % f0, ", ".join("H%d" % n for n in nulls),
                     "%.2f %%" % (100 * d), "%.2f Vpp" % (2.705 / (0.5 - d))])
    table(["window", "f0 (Hz)", "nulls", "duty", "node 11 swing for a 2.705 V threshold"], rows)
    for f in (1059, 2121):
        r = rec[("narrow", f)] - rec[("sq", f)]
        print("- narrow/square H1 at %d Hz: %+.2f dB; a pulse of duty d has sin(pi d) of the square's "
              "H1: d = %.1f %%" % (f, r, 100 * np.arcsin(10 ** (r / 20)) / np.pi))
    print()

    # the model
    rows = []
    for lvl in (10, 8, 6):
        for sc, f in ((1, 1059), (2, 2121)):
            m = {}
            for wave, w in (("saw", 0), ("sq", 1), ("narrow", 2)):
                s = dict(OPEN, vco1_level=0, vco2_level=lvl, vco2_wave=w, vco2_scale=sc)
                sr, d = model.run("v2_%s_%d_%g" % (wave, sc, lvl), 36, s)
                for p in ("mix", "sigout"):
                    x = d[p][int(0.5 * sr):int(1.1 * sr)]
                    f0 = f0_est(x, sr, 500, 3000)
                    m[(wave, p)] = db(harmonics(x, sr, f0, 6)[0][0])
            rows.append(["model LEVEL %g, key 36 %s" % (lvl, "8'" if sc == 1 else "4'"),
                         "%+.2f / %+.2f" % (m[("saw", "mix")] - m[("sq", "mix")], m[("saw", "sigout")] - m[("sq", "sigout")]),
                         "%+.1f / %+.1f" % (m[("narrow", "mix")] - m[("sq", "mix")], m[("narrow", "sigout")] - m[("sq", "sigout")])])
    for f in (1059, 2121):
        rows.insert(0, ["recording, %d Hz" % f, "%+.2f (SIGNAL OUT)" % (rec[("saw", f)] - rec[("sq", f)]),
                        "%+.1f (SIGNAL OUT)" % (rec[("narrow", f)] - rec[("sq", f)])])
    print("H1 ratios (dB), model at the mixer (node 13) / at SIGNAL OUT:\n")
    table(["", "saw/square", "narrow/square"], rows)

    # VCO 2's saw against VCO 1's saw, both at LEVEL 10
    print("VCO 2's saw against VCO 1's saw at the same LEVEL (the video shows both at full):\n")
    rows = []
    for t0, t1 in ((107.5, 108.5), (107.6, 108.8)):
        sr, x = seg(T8, t0, t1)
        f1, f2, a1, a2, r2 = two_series(x, sr, 259.9, 264.2)
        rows.append(["unison %.1f-%.1f s (VCO 1 %.2f Hz, VCO 2 %.2f Hz, %.0f %% of the power fitted)"
                     % (t0, t1, f1, f2, 100 * r2), "%+.2f" % db(a2[0] / a1[0]), "%+.2f" % db(a2[1] / a1[1])])
    v1 = np.mean([db(event(T8, a, b, 900, 1200)[1][0]) for a, b in VCO1_EVENTS[("saw", 1042)]])
    rows.append(["solo: VCO 2 86.45-86.75 s (1059 Hz) against VCO 1 17.8-20.4 s (1042 Hz)",
                 "%+.2f" % (rec[("saw", 1059)] - v1), ""])
    s1 = model.h1("u_v1saw", 36, dict(OPEN, vco1_level=10, vco2_level=0, vco1_wave=1), band=(800, 1500))
    s2 = model.h1("u_v2saw", 36, dict(OPEN, vco1_level=0, vco2_level=10, vco2_wave=0), band=(800, 1500))
    m1 = model.h1("u_v1saw", 36, dict(OPEN, vco1_level=10, vco2_level=0, vco1_wave=1), probe="mix", band=(800, 1500))
    m2 = model.h1("u_v2saw", 36, dict(OPEN, vco1_level=0, vco2_level=10, vco2_wave=0), probe="mix", band=(800, 1500))
    rows.append(["model, LEVEL 10 each, key 36: mixer / SIGNAL OUT", "%+.2f / %+.2f" % (db(m2[1][0] / m1[1][0]), db(s2[1][0] / s1[1][0])),
                 "%+.2f / %+.2f" % (db(m2[1][1] / m1[1][1]), db(s2[1][1] / s1[1][1]))])
    table(["", "VCO 2 / VCO 1, H1 (dB)", "H2 (dB)"], rows)
    print("Saw shapes at SIGNAL OUT, H2..H10 re H1 (dB): model VCO 1 %s; model VCO 2 %s (the limiters "
          "compress it); recording VCO 1 (17.8-18.7 s) %s; recording VCO 2 (86.45-86.75 s) %s\n"
          % tuple(" ".join("%.1f" % v for v in db(a / a[0])[1:10])
                  for a in (s1[1], s2[1], event(T8, 17.8, 18.7, 900, 1200)[1], event(T8, 86.45, 86.75, 900, 1200)[1])))


# ------------------------------------------------------------------ question 2: pink noise

def q_noise(model):
    print("## Pink against white noise\n")
    sr, xa = seg(D2, 9.1, 12.9)      # PINK -> EXT SIGNAL IN alone
    _, xb = seg(D2, 55.6, 58.2)      # PINK + VCO 1 WHITE
    _, xw = seg(T8, 187.0, 202.5)    # VCO 1 WHITE alone (part 1 of the same series)
    cs, A = thirds(xa, sr)
    _, B = thirds(xb, sr)
    _, W = thirds(xw, sr)
    m = (cs >= 40) & (cs <= 12000)
    res = least_squares(lambda p: pdb(10 ** p[0] * A[m] + 10 ** p[1] * W[m]) - pdb(B[m]), [0, 0])
    a, b = 10 ** res.x

    # the model's noise block (white = J25, pink = J19), and the white through the whole chain
    srm, d = model.run("noise", 24, dict(OPEN, vco1_level=10, vco2_level=0, vco1_wave=3), seconds=6.0, on=0.05,
                       probes=("white", "pink", "mix", "sigout"))
    _, Wm = thirds(d["white"][srm:], srm)
    _, Pm = thirds(d["pink"][srm:], srm)
    _, So = thirds(d["sigout"][srm:], srm)
    i500 = int(np.argmin(abs(cs - 500)))
    rows = []
    for k, c in enumerate(cs):
        circ = 20 * np.log10(32.35) - 10 * np.log10(1 + (c / 32.9) ** 2)
        rows.append(["%.0f" % c, "%.1f" % pdb(A[k]), "%.1f" % pdb(W[k]), "%.1f" % pdb(B[k]),
                     "%.1f" % pdb(a * A[k] + b * W[k]), "%.1f" % pdb(A[k] / W[k]), "%.1f" % pdb(Pm[k] / Wm[k]),
                     "%.1f" % circ, "%.1f" % pdb(W[k] / W[i500]), "%.1f" % pdb(So[k] / So[i500])])
    print("Third-octave PSD (dB re 1/Hz, full scale): A = PINK alone (%s 9.1-12.9 s), W = WHITE alone "
          "(%s 187.0-202.5 s), B = both (%s 55.6-58.2 s) and its fit a*A + b*W (a %+.2f dB, b %+.2f dB, "
          "residual %.2f dB RMS over 40 Hz-12 kHz); pink/white recorded (A/W), in the model's noise block, "
          "and the drawn circuit (32.35 / (1 + (f / 32.9 Hz)^2)^0.5); the white's shape through the whole "
          "chain re 500 Hz, recorded and modelled (SIGNAL OUT, VCO 1 LEVEL 10, filters open).\n"
          % (D2, T8, D2, pdb(a), pdb(b), np.sqrt(np.mean(res.fun ** 2))))
    table(["f (Hz)", "A pink", "W white", "B both", "fit", "A/W", "model P/W", "circuit", "W re 500", "model W re 500"], rows)
    mm = (cs >= 100) & (cs <= 2000)
    sl = lambda y: np.polyfit(np.log2(cs[mm]), pdb(y[mm]), 1)[0]
    print("Slopes 100 Hz-2 kHz (dB/octave): pink %.2f, white %.2f, pink/white %.2f; model pink/white %.2f; "
          "circuit %.2f; a -3 dB/octave pink would read -3.0."
          % (sl(A), sl(W), sl(A / W), sl(Pm / Wm),
             np.polyfit(np.log2(cs[mm]), -10 * np.log10(1 + (cs[mm] / 32.9) ** 2), 1)[0]))
    best = None
    for fc in np.arange(15, 60, 0.25):
        k = (cs >= 50) & (cs <= 5000)
        r = pdb(A[k] / W[k]) + 10 * np.log10(1 + (cs[k] / fc) ** 2)
        if best is None or np.std(r) < best[0]:
            best = (np.std(r), fc, np.mean(r))
    print("One pole fitted to A/W over 50 Hz-5 kHz: %.1f Hz, gain %.1f dB below it (residual %.2f dB); "
          "drawn: 32.9 Hz, 30.2 dB.\n" % (best[1], best[2], best[0]))


# ------------------------------------------------------------------ question 3: self-oscillation

# YH3g01qU7O8 (Rev 1): a held 386 Hz tone, odd-heavy (H3 -16..-20 dB, H2 -25..-33 dB), 668-683 s,
# and a 1042.6 Hz tone over a 110 Hz VCO, 637.6-639.0 s; the windows avoid the retriggers
SELFOSC_EVENTS = [(672.4, 673.15, 300, 500), (674.35, 674.9, 300, 500), (675.0, 675.85, 300, 500),
                  (676.5, 677.2, 300, 500), (677.6, 678.75, 300, 500), (682.5, 683.15, 300, 500),
                  (637.6, 639.0, 900, 1200)]


def wave_row(x, sr, f0):
    a, ph = harmonics(x, sr, f0, 5)
    r = db(a / a[0])
    return ["%.1f" % f0] + ["%.1f" % v for v in r[1:5]] + ["%.3f" % half_ratio(x), "%+.0f" % h2_phase(a, ph)]


def q_selfosc(model, scan):
    print("## Self-oscillation\n")
    rows = []
    for t0, t1, lo, hi in SELFOSC_EVENTS:
        sr, x = seg(YH3, t0, t1)
        x = x - x.mean()
        rows.append(["%s %.2f-%.2f s" % (YH3, t0, t1)] + wave_row(x, sr, f0_est(x, sr, lo, hi)))
    sr, x = seg(YH3, 637.6, 639.0)
    f0 = f0_est(x, sr, 900, 1200)
    a1 = harmonics(x, sr, f0, 1)[0][0]
    lines = [db(harmonics(x, sr, fq, 1)[0][0] / a1) for fq in (110.0, f0 - 110.0, f0 + 110.0)]
    print("Rev 1 recording. The 637.6-639.0 s tone (%.1f Hz) is no harmonic of the VCO also sounding at "
          "about 110 Hz (%.1f dB re the tone; sidebands at the tone -/+ 110 Hz %.1f / %.1f dB): the LPF "
          "self-oscillating with a little VCO in it. The 386 Hz tone has no other line within 30 dB "
          "(677.6-678.75 s). Half-period ratio: positive over negative half (a recording's polarity is "
          "unknown: compare r with 1/r); H2's phase against twice H1's (inverting moves it 180 deg).\n"
          % (f0, lines[0], lines[1], lines[2]))
    head = ["", "f0 (Hz)", "H2", "H3", "H4", "H5 (dB re H1)", "half-period ratio", "H2 phase (deg)"]
    for cut in (4.67, 6.0):
        for peak in (8.8, 9.0, 9.25, 9.5, 9.75, 10):
            srm, d = model.run("so_%g_%g" % (cut, peak), 24,
                               dict(OPEN, vco1_level=0, vco2_level=0, lpf_peak=peak, lpf_cutoff=cut),
                               seconds=1.5, on=0.02, probes=("lpf", "sigout"))
            for p, name in (("lpf", "LPF output"), ("sigout", "SIGNAL OUT")):
                y = d[p][int(1.0 * srm):int(1.45 * srm)]
                y = y - y.mean()
                rows.append(["model CUTOFF %g PEAK %g, %s (%.3f Vpp)" % (cut, peak, name, np.ptp(y))]
                            + wave_row(y, srm, f0_est(y, srm, 50, 20000)))
    print("The recording's events, then the model's LPF self-oscillating (both VCO LEVELs 0, HPF open) "
          "at the LPF's output (check point 16) and at SIGNAL OUT:\n")
    table(head, rows)
    if scan:
        print("Near-pure-tone runs (one line holding 85 %% of the power, 0.4 s or longer) in the Rev 1 "
              "clips (%s):\n" % ", ".join(REV1))
        for vid in REV1:
            for line in tonal_runs(vid):
                print("- %s %s" % (vid, line))
        print()


def tonal_runs(vid, win=0.1, hop=0.05, purity=0.85, mind=0.4):
    sr, x = clip(vid)
    n = int(win * sr)
    N = 8 * n
    w = np.hanning(n)
    f = np.fft.rfftfreq(N, 1 / sr)
    band = (f > 30) & (f < 16000)
    rows = []
    for t in np.arange(0, len(x) / sr - win, hop):
        s = x[int(t * sr):int(t * sr) + n]
        if np.std(s) < 10 ** (-45 / 20):
            continue
        P = np.abs(np.fft.rfft((s - s.mean()) * w, N)) ** 2
        k = int(np.argmax(P * band))
        E = lambda c, rel: P[(f > c * (1 - rel)) & (f < c * (1 + rel))].sum()
        if E(f[k], 0.03) / P[band].sum() < purity:
            continue
        rows.append((t, f[k], [pdb(E(h * f[k], 0.015 / h) / E(f[k], 0.015)) for h in (2, 3)]))
    out, run = [], []
    for r in rows + [(1e9, 0, [0, 0])]:
        if run and r[0] - run[-1][0] > hop + 1e-9:
            if run[-1][0] - run[0][0] + win >= mind:
                fs = [q[1] for q in run]
                out.append("%.2f-%.2f s: %.0f-%.0f Hz, H2 %.1f, H3 %.1f dB" % (
                    run[0][0], run[-1][0] + win, min(fs), max(fs),
                    np.median([q[2][0] for q in run]), np.median([q[2][1] for q in run])))
            run = []
        run.append(r)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--render", default=DEFAULT_RENDER)
    ap.add_argument("--out", default=os.path.join(tempfile.gettempdir(), "ms20compare"))
    ap.add_argument("--only", default="vco1,vco2,noise,selfosc")
    ap.add_argument("--scan", action="store_true")
    args = ap.parse_args()
    if not os.path.isdir(CLIPS):
        sys.exit("no clips at %s (private/ms20/reference/youtube)" % CLIPS)
    model = Model(args.render, args.out)
    only = args.only.split(",")
    if "vco1" in only:
        q_vco1(model)
    if "vco2" in only:
        q_vco2(model)
    if "noise" in only:
        q_noise(model)
    if "selfosc" in only:
        q_selfosc(model, args.scan)


if __name__ == "__main__":
    main()
