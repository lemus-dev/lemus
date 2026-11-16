"""Signal analysis for the SH-101 renders: pitch, partials, envelopes, glides, modulation, pulse
width, sub-oscillator and noise. Used by measure.py (the laws), compare.py and the fit.

Every function takes a mono float array and its sample rate. Times are seconds from the start of
the render, which is line 0 of the spec in both renderers (sh101render drops its pre-roll; Onyx
starts at line 0).
"""
import json, math, os
import numpy as np
import soundfile as sf
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))


def load(path):
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    return x.mean(axis=1), sr


def line_seconds(spec):
    return 60.0 / spec.get("bpm", 120) / spec.get("lpb", 4)


def note_span(spec, i=0):
    """(start, end) in seconds of the spec's i-th note."""
    n = spec["notes"][i]
    ls = line_seconds(spec)
    return n["line"] * ls, (n["line"] + n.get("len", 1)) * ls


def seg(x, sr, t0, t1):
    return x[max(0, int(t0 * sr)):max(0, int(t1 * sr))]


def db(v):
    return 20 * np.log10(np.maximum(np.abs(v), 1e-12))


def rms(x):
    return float(np.sqrt(np.mean(np.square(x)))) if len(x) else 0.0


def midi_hz(note, a4=440.0):
    return a4 * 2 ** ((note - 69) / 12)


def parse_note(s):
    semi = "C D EF G A B".index(s[0].upper())
    pos = 1
    if s[pos] == "#":
        semi += 1; pos += 1
    elif s[pos] == "b":
        semi -= 1; pos += 1
    elif s[pos] == "-":
        pos += 1
    return 12 * (int(s[pos:]) + 1) + semi


# --- pitch -------------------------------------------------------------------------------------

def yin(x, sr, fmin=20.0, fmax=5000.0, thresh=0.15):
    """YIN on one segment: the fundamental in Hz, or nan."""
    w = min(len(x) // 2, int(sr / fmin) * 3)
    tau_max = min(int(sr / fmin) + 2, len(x) - w)
    if w < 32 or tau_max < 4 or rms(x) < 1e-6:
        return float("nan")
    a = x[:w]
    # difference function via FFT
    d = np.zeros(tau_max)
    xx = np.cumsum(np.concatenate([[0], x ** 2]))
    n = 1 << int(math.ceil(math.log2(len(x) + w)))
    corr = np.fft.irfft(np.fft.rfft(x, n) * np.conj(np.fft.rfft(a, n)), n)[:tau_max]
    for tau in range(tau_max):
        d[tau] = xx[w] - xx[0] + xx[tau + w] - xx[tau] - 2 * corr[tau]
    cm = np.ones(tau_max)
    cs = np.cumsum(d[1:])
    cm[1:] = d[1:] * np.arange(1, tau_max) / np.maximum(cs, 1e-20)
    tau_min = max(2, int(sr / fmax))
    cand = np.where(cm[tau_min:] < thresh)[0]
    if len(cand):
        tau = tau_min + cand[0]
        while tau + 1 < tau_max and cm[tau + 1] < cm[tau]:
            tau += 1
    else:
        tau = tau_min + int(np.argmin(cm[tau_min:]))
    if 1 <= tau < tau_max - 1:
        y0, y1, y2 = cm[tau - 1], cm[tau], cm[tau + 1]
        den = y0 - 2 * y1 + y2
        tau = tau + (0.5 * (y0 - y2) / den if den != 0 else 0)
    return sr / tau if tau > 1 else float("nan")


def spectrum(x, sr, n=1 << 20):
    w = np.hanning(len(x))
    sp = np.abs(np.fft.rfft(x * w, n)) * 2 / w.sum()
    return np.fft.rfftfreq(n, 1 / sr), sp


def peak_near(fr, sp, f, rel=0.03):
    """Frequency and amplitude of the largest spectral peak within f*(1 +- rel), refined by a
    parabola on the log magnitude."""
    band = np.where((fr > f * (1 - rel)) & (fr < f * (1 + rel)))[0]
    if len(band) < 3:
        return float("nan"), 0.0
    k = band[np.argmax(sp[band])]
    if 0 < k < len(sp) - 1:
        a, b, c = np.log(sp[k - 1:k + 2] + 1e-30)
        den = a - 2 * b + c
        p = 0.5 * (a - c) / den if den != 0 else 0
        return float(fr[k] + p * (fr[1] - fr[0])), float(np.exp(b - 0.25 * (a - c) * p))
    return float(fr[k]), float(sp[k])


def f0(x, sr, fmin=20.0, fmax=5000.0, guess=None):
    """Precise fundamental of a steady segment: YIN for the octave, the spectral peak for the
    decimals."""
    g = guess if guess else yin(x[: min(len(x), int(0.2 * sr))], sr, fmin, fmax)
    if not np.isfinite(g):
        return float("nan")
    fr, sp = spectrum(x, sr)
    f, _ = peak_near(fr, sp, g)
    return f if np.isfinite(f) else g


def f0_sine(x, sr, fmin=2.0):
    """Frequency of a near-sine (self-oscillation): the largest spectral peak above fmin, refined."""
    fr, sp = spectrum(x, sr)
    band = fr > fmin
    k = int(np.argmax(sp * band))
    return peak_near(fr, sp, fr[k], 0.02)[0] if fr[k] > 0 else float("nan")


def cents(f, ref):
    return 1200 * math.log2(f / ref) if f > 0 and ref > 0 else float("nan")


def period_track(x, sr, fmin=20.0, fmax=8000.0, hyst=0.1):
    """Instantaneous frequency from rising crossings with hysteresis (one per period): times and
    Hz. For vibrato and glides of waves with one rising edge per period (saw, sine, pulse)."""
    b, a = signal.butter(2, max(fmin * 0.5, 5) / (sr / 2), "high")
    y = signal.filtfilt(b, a, x)
    lp = min(fmax * 1.2, sr * 0.45)
    sos = signal.butter(4, lp / (sr / 2), "low", output="sos")
    y = signal.sosfiltfilt(sos, y)
    env = np.sqrt(signal.filtfilt(*signal.butter(1, fmin / 4 / (sr / 2)), y ** 2).clip(0)) * math.sqrt(2)
    hi, lo = hyst * env, -hyst * env
    armed = False
    times = []
    for i in range(1, len(y)):
        if y[i] < lo[i]:
            armed = True
        elif armed and y[i - 1] < 0 <= y[i]:
            frac = -y[i - 1] / (y[i] - y[i - 1])
            times.append((i - 1 + frac) / sr)
            armed = False
    times = np.array(times)
    if len(times) < 3:
        return np.array([]), np.array([])
    per = np.diff(times)
    t = times[:-1] + per / 2
    f = 1 / per
    ok = (f > fmin) & (f < fmax)
    return t[ok], f[ok]


def pitch_track(x, sr, hop=0.005, win=0.04, fmin=25, fmax=4000):
    """YIN per frame: times and Hz (nan where silent)."""
    w, h = int(win * sr), int(hop * sr)
    ts, fs = [], []
    for start in range(0, len(x) - 2 * w, h):
        s = x[start:start + 2 * w]
        fs.append(yin(s, sr, fmin, fmax) if rms(s) > 1e-4 else float("nan"))
        ts.append((start + w) / sr)
    return np.array(ts), np.array(fs)


# --- partials, filter ------------------------------------------------------------------------

def harmonics(x, sr, f, nh=60):
    fr, sp = spectrum(x, sr)
    return np.array([peak_near(fr, sp, f * k, 0.5 / k if k > 1 else 0.05)[1] for k in range(1, nh + 1)])


def resonant_peak(x, sr, f):
    """Harmonic amplitudes of a saw on f, weighted by n to undo the saw's 1/n: the resonant peak's
    frequency (parabola over the weighted harmonics) and its height over the fundamental (dB),
    and the fundamental's level (dBFS)."""
    nh = int(min(60, 0.45 * sr / f))
    a = harmonics(x, sr, f, nh)
    w = a * np.arange(1, nh + 1)
    k = int(np.argmax(w[1:])) + 1
    kk = float(k)
    if 1 < k < nh - 1:
        y0, y1, y2 = db(w[k - 1:k + 2])
        den = y0 - 2 * y1 + y2
        kk = k + (0.5 * (y0 - y2) / den if den != 0 else 0)
    return {"peak_hz": round(f * (kk + 1), 1), "peak_db": round(float(db(w[k] / w[0])), 2),
            "h1_dbfs": round(float(db(a[0])), 2)}


def harmonic_profile(x, sr, f, nh=12):
    a = harmonics(x, sr, f, nh)
    return [round(float(v), 1) for v in db(a / max(a[0], 1e-12))]


# --- envelopes ---------------------------------------------------------------------------------

def envelope(x, sr, win=0.005, hop=0.001):
    """RMS envelope (x sqrt 2: peak of a sine), times and linear level."""
    w, h = max(1, int(win * sr)), max(1, int(hop * sr))
    c = np.cumsum(np.concatenate([[0], x ** 2]))
    starts = np.arange(0, len(x) - w, h)
    e = np.sqrt(np.maximum(c[starts + w] - c[starts], 0) / w * 2)
    return (starts + w / 2) / sr, e


def rise(t, e, t0, frac=(0.1, 0.9), top=None):
    """Times (from t0) at which the envelope first reaches each fraction of its top."""
    m = t >= t0
    tt, ee = t[m], e[m]
    top = top if top is not None else ee.max()
    out = []
    for f in frac:
        i = np.where(ee >= f * top)[0]
        out.append(float(tt[i[0]] - t0) if len(i) else float("nan"))
    return out


def decay_tau(t, e, t0, t1, hi_db=-3.0, lo_db=-30.0, floor=None):
    """Time constant of an exponential decay of e between t0 and t1: a line through ln(e - floor)
    between hi_db and lo_db below its start. Returns tau (s) and the times to -20 / -40 dB."""
    m = (t >= t0) & (t <= t1)
    tt, ee = t[m] - t0, e[m]
    if len(ee) < 10:
        return float("nan"), float("nan"), float("nan")
    fl = floor if floor is not None else 0.0
    y = ee - fl
    top = max(y[: max(3, len(y) // 50)].max(), 1e-12)
    ydb = db(y / top)
    sel = (ydb <= hi_db) & (ydb >= lo_db)
    first = np.where(ydb <= lo_db)[0]
    if len(first):
        sel[first[0]:] = False
    tau = float("nan")
    if sel.sum() >= 5:
        p = np.polyfit(tt[sel], np.log(np.maximum(y[sel], 1e-12)), 1)
        tau = -1 / p[0] if p[0] < 0 else float("nan")
    def reach(lev):
        i = np.where(ydb <= lev)[0]
        return float(tt[i[0]]) if len(i) else float("nan")
    return tau, reach(-20), reach(-40)


def count_attacks(t, e, t0, t1, rise_db=6.0):
    """Number of re-attacks: rises of at least rise_db within 30 ms."""
    m = (t >= t0) & (t <= t1)
    ee = db(e[m])
    step = max(1, int(round(0.03 / max(t[1] - t[0], 1e-6))))
    jumps = ee[step:] - ee[:-step]
    above = jumps > rise_db
    return int(np.sum(above[1:] & ~above[:-1]) + (1 if above[:1].any() else 0))


# --- glides and modulation ---------------------------------------------------------------------

def glide(t, f, t0, f_to, f_from=None):
    """Glide to f_to starting at t0 (from the pitch just before t0, or f_from): the 10-90 % time
    (in cents) and the time constant of the approach (ln of the remaining interval, from 90 % to
    5 % remaining). An RC glide (the SH-101's) is exponential in cents: t10-90 = 2.2 tau."""
    if f_from is None:
        pre = (t > t0 - 0.06) & (t < t0 - 0.003) & np.isfinite(f)
        f_from = float(np.median(f[pre])) if pre.any() else float("nan")
    m = (t >= t0) & np.isfinite(f)
    tt, cc = t[m] - t0, np.array([cents(v, f_to) for v in f[m]])
    total = cents(f_from, f_to)
    if len(tt) < 5 or not np.isfinite(total) or abs(total) < 50:
        return {}
    rem = cc / total  # 1 at the start, 0 at the end
    def when(r):
        i = np.where(rem <= r)[0]
        return float(tt[i[0]]) if len(i) else float("nan")
    t10, t90 = when(0.9), when(0.1)
    sel = (rem < 0.9) & (rem > 0.05)
    stop = np.where(rem <= 0.05)[0]
    if len(stop):
        sel[stop[0]:] = False
    tau = float("nan")
    if sel.sum() >= 5:
        p = np.polyfit(tt[sel], np.log(rem[sel]), 1)
        tau = -1 / p[0] if p[0] < 0 else float("nan")
    return {"from_hz": round(f_from, 2), "t10_90": round(t90 - t10, 4), "tau": round(tau, 4)}


def exp_fit(t, c):
    """Least-squares fit of c(t) = c_inf + (c0 - c_inf) exp(-(t - t[0]) / tau): tau and c_inf."""
    from scipy.optimize import curve_fit
    t = t - t[0]
    f = lambda tt, ci, a, tau: ci + a * np.exp(-tt / tau)
    try:
        (ci, a, tau), _ = curve_fit(f, t, c, p0=(c[-1], c[0] - c[-1], max(t[-1] / 3, 1e-3)), maxfev=20000)
        return float(tau), float(ci)
    except Exception:
        return float("nan"), float("nan")


def modulation(t, v, sr_track=500.0):
    """A modulation trajectory v(t) (cents or Hz): rate (Hz), peak-to-peak depth and shape."""
    if len(t) < 20:
        return {}
    tu = np.arange(t[0], t[-1], 1 / sr_track)
    vu = np.interp(tu, t, v)
    vu = vu - np.median(vu)
    lo, hi = np.percentile(vu, [1, 99])
    depth = hi - lo
    if depth <= 1e-9:
        return {"rate_hz": 0.0, "depth": 0.0, "shape": "none"}
    n = 1 << int(math.ceil(math.log2(len(vu) * 8)))
    sp = np.abs(np.fft.rfft((vu - vu.mean()) * np.hanning(len(vu)), n))
    fr = np.fft.rfftfreq(n, 1 / sr_track)
    band = fr > 0.05
    k = np.argmax(sp * band)
    rate = float(fr[k])
    # shape: how much of the time is spent near the extremes, how flat the trajectory is between
    # jumps, and how tonal its spectrum is
    z = (vu - (hi + lo) / 2) / (depth / 2)
    extreme = float(np.mean(np.abs(z) > 0.85))
    dz = np.abs(np.diff(z))
    # Steps (square, sample-and-hold) move only in short jumps and sit still between them: their
    # median slope is a small part of their mean slope; a triangle's or sine's is not.
    flat = float(1 - np.median(dz) / max(dz.mean(), 1e-12))
    moving = dz > max(4 * np.median(dz), 0.02 * dz.max())
    runs = np.split(np.arange(len(dz)), np.where(np.diff(moving.astype(int)) != 0)[0] + 1)
    jumps = sum(1 for run in runs if moving[run[0]] and np.abs(np.diff(z)[run].sum()) > 0.1)
    jump_rate = jumps / max(tu[-1] - tu[0], 1e-9)
    tonal = float(sp[k] / max(np.sqrt(np.mean(sp[band] ** 2)), 1e-20))
    if flat > 0.8 and extreme > 0.7:
        shape = "square"
    elif flat > 0.8:
        shape, rate = "steps", jump_rate
    elif tonal < 4:
        shape = "noise"
    elif extreme > 0.25:
        shape = "sine"
    else:
        shape = "triangle"
    return {"rate_hz": round(rate, 3), "depth": round(float(depth), 2), "shape": shape,
            "extreme": round(extreme, 2), "flat": round(flat, 2)}


# --- pulse width, sub-oscillator, noise ----------------------------------------------------------

def duty_from_wave(x):
    """Fraction of the time a pulse is in its upper state (midpoint of the 2nd/98th percentiles)."""
    lo, hi = np.percentile(x, [2, 98])
    return float(np.mean(x > (lo + hi) / 2))


def duty_from_harmonics(x, sr, f):
    """Pulse duty d (<= 0.5) that best explains the first harmonics: a_n ~ |sin(pi n d)| / n."""
    a = harmonics(x, sr, f, 10)
    a = a / max(a[0], 1e-12)
    n = np.arange(1, 11)
    best, bd = 1e9, float("nan")
    for d in np.linspace(0.005, 0.5, 496):
        m = np.abs(np.sin(np.pi * n * d)) / n
        m = m / max(m[0], 1e-12)
        e = np.sum((db(a) - db(np.maximum(m, 1e-4))).clip(-40, 40) ** 2)
        if e < best:
            best, bd = e, d
    return float(bd)


def duty_track(x, sr, f, hop=0.02, periods=None):
    """Duty per frame of a pulse at about f, over whole periods."""
    periods = periods or max(4, int(math.ceil(0.02 * f)))
    w, h = int(round(periods * sr / f)), int(hop * sr)
    ts, ds = [], []
    for s in range(0, len(x) - w, h):
        fr = x[s:s + w]
        if rms(fr) < 1e-4:
            continue
        d = duty_from_wave(fr)
        ts.append((s + w / 2) / sr)
        ds.append(min(d, 1 - d))
    return np.array(ts), np.array(ds)


def noise_spectrum(x, sr):
    f, p = signal.welch(x, sr, nperseg=8192)
    band = (f > 200) & (f < 8000)
    slope = np.polyfit(np.log2(f[band]), 10 * np.log10(p[band] + 1e-30), 1)[0]
    cent = float((p * f).sum() / max(p.sum(), 1e-30))
    # -3 dB corner relative to the 200-500 Hz level
    ref = 10 * np.log10(np.mean(p[(f > 200) & (f < 500)]) + 1e-30)
    pd = 10 * np.log10(p + 1e-30)
    above = np.where((f > 500) & (pd < ref - 3))[0]
    corner = float(f[above[0]]) if len(above) else float("nan")
    return {"slope_db_oct": round(float(slope), 2), "centroid_hz": round(cent, 0), "corner_hz": round(corner, 0)}


def specs(*names):
    out = {}
    for n in names or ("suite", "calibration", "stability", "probes"):
        p = os.path.join(HERE, "specs", n + ".json")
        if os.path.exists(p):
            for s in json.load(open(p)):
                out[s["name"]] = s
    return out
