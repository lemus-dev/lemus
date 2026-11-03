#!/usr/bin/env python3
"""The rim shot and hand clap (core/netlists/rs.cir, hc.cir) against real TR-909s
(private/reference/real909; docs/circuit/rshc.md, "Against the real unit" and "Calibration").
Hits rendered through tr909voicerun (the board as the plugin plays it: 96 kHz, a 2 ms trigger,
LEVEL at centre, the MULTI OUT jack), the service notes' unit and the calibrated one
(`CAL`, the entries for unitCalibration()), measured the same way as the recordings; the model's
side goes through AudioRealism's recording chain (real909.chain: 6.5 Hz first order) before it
is compared with AudioRealism's clips.

  RS  lines       the three resonators' lines in the first 50 ms (Hz, dB re the strongest)
      tau         the 220 and 480 Hz lines' decay over 12-35 ms (amplitude, ms)
      env         the 2 ms RMS envelope's fall to -6, -20 and -40 dB of its peak (ms)
      ratio       the largest positive over the largest negative excursion, 0.5-8 ms
      ncc         the waveform's normalised cross-correlation with AudioRealism's shots, 0-15 ms
      lobe        the slow lobe after the ringing: 60 Hz low-passed, % of the peak, 30-120 ms
      area        the hit's running sum at 10 / 20 / 40 ms, ms x the peak
  HC  teeth       the four pulses' onsets (ms), from the 1.5-6 kHz envelope
      fourth      the fourth pulse's 1.5-6 kHz RMS over 3 ms against the second's and third's (dB)
      tail        the tail's decay over 60-250 ms (amplitude, ms)
      p/t         RMS of 0-35 ms over RMS of 60-120 ms (dB)
      late        IC26b's band (600-1500 Hz) at 0.3 / 0.5 / 0.7 / 0.9 s, dB re the peak
      pulses/tail spectra   the 0-32 ms and 120-300 ms segments' peak and 2/5/8 kHz re peak
  both  level     RMS of the first 50 ms; RS against HC (AudioRealism: one interface gain, every
                  LEVEL at 50 %); accent: the AudioRealism accent sweep at real909.ar_code's codes

Units: AudioRealism (96 kHz, individual outs, LEVEL 50 %: 10 RS and 8 HC shots, 11 accent takes;
the primary reference, the unit the calibration fits), Rob Roy 1995 (44.1 kHz, normalised to both
rails: lines, decay and timing only), Regebro rev 4 (MP3: accent levels, shape), archive.org
Set2 (an early board's rim shot).

  rshc.py [--out DIR] [--voicerun PATH] [--jobs N] [--seeds N] [--only nominal|unit]
          [--cal NAME=VALUE ...] [--fit tm|c65|rslines|rsout] [--adjust] [--netlists DIR] [--plots]

--cal adds or overrides entries of the calibrated set (hc.tm=0.31, rs.c121=0.047u). --fit
renders the grids the calibration was chosen from (docs/circuit/rshc.md, "Calibration").
--netlists DIR reads variant netlists (models.cir, rs.cir, hc.cir) from DIR (TR909_NETLISTS).
--adjust redoes the service notes' TM adjustment on the model (LEVEL at centre, an unaccented
step, 2 Vp-p at the MULTI OUT jack, averaged over --seeds runs of the noise).
"""
import argparse
import concurrent.futures as cf
import json
import math
import os
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import real909  # noqa: E402

MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
REPO = os.path.normpath(os.path.join(MACHINE, '..', '..'))
REF = os.path.join(MACHINE, 'private', 'reference', 'real909')
AR = os.path.join(REF, 'packs', 'audiorealism_tr909_ultimate', 'wav', 'TR-909-96kHz-24bit')
RR = os.path.join(REF, 'packs', 'zoeblade_robroy_1995', 'wav')
AO = os.path.join(REF, 'packs', 'archiveorg_drum_machines_collection', 'wav')
REG = os.path.join(REF, 'videos', 'regebro_soundcloud_rd9_tr909_all_sounds_accent_levels', 'wav', 'hits',
                   'tr909_individual_out')

RATE = 96000
JACK = {'rs': 5, 'hc': 6}  # the voices' channels in voicerun's --jacks file

# The unit's values (AudioRealism's 909): the entries for unitCalibration(), each argued in
# docs/circuit/rshc.md, "Calibration".
CAL = {
    'rs.r421': 1e-3,         # the output is IC50a's: IC50b a follower without its high-pass
    'rs.c121': 100e-6,
    'rs.c122': 100e-6,
    'rs.c112': 0.01027e-6,   # F1 on the unit's 481 Hz (+2.7 %)
    'rs.c113': 0.01027e-6,
    'rs.c115': 0.02646e-6,   # F2 on 222 Hz (-2 %)
    'rs.c116': 0.02646e-6,
    'rs.c117': 0.00485e-6,   # F3 on 1019 Hz (+3.2 %)
    'rs.c118': 0.00485e-6,
    'hc.tm': 0.30,           # TM: the pulses' level against the other voices
    'hc.c65': 0.39e-6,       # the teeth's spacing (-17 %, an electrolytic)
    'hc.c57': 0.38e-6,       # the reverb's level (-19 %, an electrolytic)
}


# ---------------------------------------------------------------------------------------------
# rendering

class Board:
    """Hits through tr909voicerun, the jack of the voice, from the hit: (raw, through AR's chain)."""

    def __init__(self, voicerun, work, netlists=None):
        self.vr = os.path.abspath(voicerun)
        self.work = work
        self.env = dict(os.environ, TR909_NETLISTS=os.path.abspath(netlists)) if netlists else None
        self.n = 0

    def render(self, voice, code, seconds, cal, seed=0, level=0.5):
        self.n += 1
        t0 = 0.02 + 0.0173 * seed  # the noise generator runs on: another stretch of it per seed
        wav = os.path.join(self.work, f'm{self.n}_{os.getpid()}.wav')
        jacks = wav.replace('.wav', '_j.wav')
        args = [self.vr, wav, '--rate', str(RATE), '--seconds', f'{t0 + seconds:.4f}', '--hit', f'{t0:.4f}:{voice}:{code}',
                '--set', f'{voice}Level={level}', '--jacks', jacks]
        for k, v in cal.items():
            args += ['--cal', f'{k}={v}']
        r = subprocess.run(args, capture_output=True, text=True, env=self.env)
        if r.returncode:
            raise RuntimeError(r.stderr)
        x, _ = sf.read(jacks)
        os.remove(wav)
        os.remove(jacks)
        x = x[:, JACK[voice]].astype(float)
        i = int(round(t0 * RATE))
        return x[i:], real909.chain(x, RATE, 'ar')[i:]


def load(path):
    x, fs = sf.read(path)
    if x.ndim > 1:
        x = x[:, 0]
    i = int(np.argmax(np.abs(x) > 0.05 * np.abs(x).max()))
    return x[max(i - int(0.0005 * fs), 0):], fs


# ---------------------------------------------------------------------------------------------
# measures

def rms_env(x, fs, w=0.002):
    k = max(int(w * fs), 1)
    return np.sqrt(np.convolve(x * x, np.ones(k) / k, 'same'))


def db(x):
    return 20 * math.log10(max(x, 1e-15))


def level(x, fs, a=0.0, b=0.05):
    s = x[int(a * fs):int(b * fs)]
    return db(float(np.sqrt(np.mean(s * s))))


def peak_in(f, S, lo, hi):
    m = (f >= lo) & (f <= hi)
    j = np.where(m)[0][int(np.argmax(S[m]))]
    if 0 < j < len(S) - 1:  # parabolic interpolation on dB
        a, b, c = S[j - 1], S[j], S[j + 1]
        p = 0.5 * (a - c) / (a - 2 * b + c)
        return f[j] + p * (f[1] - f[0]), b - 0.25 * (a - c) * p
    return f[j], S[j]


def lines(x, fs):
    s = x[:int(0.05 * fs)]
    s = s * np.hanning(len(s))
    n = 1 << 18
    S = 20 * np.log10(np.abs(np.fft.rfft(s, n)) + 1e-12)
    f = np.fft.rfftfreq(n, 1 / fs)
    p = [peak_in(f, S, lo, hi) for lo, hi in ((150, 300), (380, 620), (850, 1300))]
    top = max(v for _, v in p)
    return [[round(float(fr), 1), round(float(v - top), 1)] for fr, v in p]


def band_tau(x, fs, f0, a=0.012, b=0.035):
    sos = signal.butter(2, [f0 / 1.25, f0 * 1.25], 'bandpass', fs=fs, output='sos')
    e = np.abs(signal.hilbert(signal.sosfiltfilt(sos, x)))
    t = np.arange(len(e)) / fs
    m = (t >= a) & (t < b)
    k = np.polyfit(t[m], np.log(e[m] + 1e-12), 1)[0]
    return round(float(-1e3 / k), 2) if k < 0 else None


def env_times(x, fs):
    e = rms_env(x, fs)
    i = int(np.argmax(e))
    out = []
    for thr in (-6, -20, -40):
        j = np.where(e[i:] < e[i] * 10 ** (thr / 20))[0]
        out.append(round(1e3 * float(i + j[0]) / fs, 1) if len(j) else None)
    return out


def ratio(x, fs):
    s = x[int(0.0005 * fs):int(0.008 * fs)]
    return round(float(s.max() / -s.min()), 2)


def resample(x, fs):
    return x if fs == RATE else signal.resample_poly(x, RATE, fs)


def ncc(model, x, fs):
    """Normalised cross-correlation over 0-15 ms, at the lag (within 1 ms) that fits best."""
    x = resample(x, fs)
    n = int(0.015 * RATE)
    best = 0.0
    for lag in range(-96, 97):
        a = model[max(lag, 0):max(lag, 0) + n]
        b = x[max(-lag, 0):max(-lag, 0) + n]
        k = min(len(a), len(b))
        r = float(np.dot(a[:k], b[:k]) / (np.linalg.norm(a[:k]) * np.linalg.norm(b[:k]) + 1e-12))
        best = r if abs(r) > abs(best) else best
    return round(best, 3)


LOBE_T = (0.03, 0.04, 0.05, 0.06, 0.08, 0.10, 0.12)


def lobe(x, fs):
    y = signal.sosfiltfilt(signal.butter(2, 60, 'low', fs=fs, output='sos'), x)
    pk = float(np.abs(x).max())
    return [round(100 * float(np.mean(y[int(a * fs):int((a + 0.01) * fs)])) / pk, 2) if len(y) > (a + 0.01) * fs else None
            for a in LOBE_T]


def area(x, fs):
    pk = float(np.abs(x).max())
    return [round(1e3 * float(np.sum(x[:int(b * fs)])) / fs / pk, 2) for b in (0.01, 0.02, 0.04)]


def hf_env(x, fs):
    e = np.abs(signal.sosfiltfilt(signal.butter(2, [1500, 6000], 'bandpass', fs=fs, output='sos'), x))
    k = int(0.0003 * fs)
    return np.convolve(e, np.ones(k) / k, 'same')


def teeth(x, fs, count=4):
    """The pulses' onsets: the `count` largest peaks of the 1.5-6 kHz envelope in 0-40 ms, at least
    6 ms apart, each timed where the envelope first rises through half that peak."""
    e = hf_env(x, fs)[:int(0.04 * fs)]
    peaks, _ = signal.find_peaks(e, distance=int(0.006 * fs))
    peaks = sorted(sorted(peaks, key=lambda i: -e[i])[:count])
    on = []
    for p in peaks:
        i = p
        while i > 0 and e[i - 1] > 0.5 * e[p]:
            i -= 1
        on.append(round(1e3 * float(i) / fs, 2))
    return on


def fourth(x, fs):
    """The fourth pulse's 1.5-6 kHz RMS over 3 ms from its onset, in dB re the second's and third's."""
    on = teeth(x, fs)
    if len(on) < 4:
        return None
    e = hf_env(x, fs)
    w = [float(np.sqrt(np.mean(e[int(t * fs / 1e3):int(t * fs / 1e3) + int(0.003 * fs)] ** 2))) for t in on]
    return round(db(w[3]) - db(0.5 * (w[1] + w[2])), 2)


def tail_tau(x, fs, a=0.06, b=0.25):
    e = rms_env(x, fs, 0.005)
    t = np.arange(len(e)) / fs
    m = (t >= a) & (t < b)
    if m.sum() < 10:
        return None
    k = np.polyfit(t[m], np.log(e[m] + 1e-12), 1)[0]
    return round(float(-1e3 / k), 1) if k < 0 else None


LATE_T = (0.3, 0.5, 0.7, 0.9)


def late(x, fs):
    y = signal.sosfiltfilt(signal.butter(4, [600, 1500], 'bandpass', fs=fs, output='sos'), x)
    pk = float(np.abs(x).max())
    return [round(db(float(np.std(y[int(a * fs):int((a + 0.1) * fs)])) / pk), 1) if len(y) >= (a + 0.1) * fs else None
            for a in LATE_T]


def seg_spectrum(x, fs, a, b):
    s = x[int(a * fs):int(b * fs)]
    s = s * np.hanning(len(s))
    n = 1 << 16
    P = np.abs(np.fft.rfft(s, n)) ** 2
    f = np.fft.rfftfreq(n, 1 / fs)
    w = max(int(60 / (f[1] - f[0])), 1)
    P = np.convolve(P, np.ones(w) / w, 'same')
    m = (f > 200) & (f < 8000)
    S = 10 * np.log10(P / P[m].max() + 1e-15)
    j = np.where(m)[0][int(np.argmax(S[m]))]
    return [int(round(f[j]))] + [round(float(np.interp(fr, f, S)), 1) for fr in (2000, 5000, 8000)]


def band_level(x, fs, lo, hi, a, b):
    y = signal.sosfiltfilt(signal.butter(4, [lo, hi], 'bandpass', fs=fs, output='sos'), x)
    s = y[int(a * fs):int(b * fs)]
    return db(float(np.sqrt(np.mean(s * s))))


def interface_gain(vr, work):
    """AudioRealism's interface gain against the model, from the (S) voices whose level is their
    VCA's (docs/circuit/s-transistors.md): the BD, LT, MT, HT at the grid's centre, code 21, LEVEL
    0.5, the service notes' parts; model minus AudioRealism, RMS of 0-50 ms, through the chain."""
    ch = {'bd': 0, 'lt': 2, 'mt': 3, 'ht': 4}
    out = []
    for V, v, knobs in (('BD', 'bd', dict(tune=0.5, attack=0.5, decay=0.5)), ('LT', 'lt', dict(tune=0.5, decay=0.5)),
                        ('MT', 'mt', dict(tune=0.5, decay=0.5)), ('HT', 'ht', dict(tune=0.5, decay=0.5))):
        xr, sr, _ = real909.load_ref(V, 'ar', **knobs)
        jacks = os.path.join(work, f'g_{v}.wav')
        args = [vr, os.path.join(work, 'g.wav'), '--rate', str(RATE), '--seconds', '0.32', '--hit', f'0.02:{v}:21', '--jacks', jacks,
                '--set', f'{v}Level=0.5'] + sum([['--set', f'{v}{k.capitalize()}={x}'] for k, x in knobs.items()], [])
        subprocess.run(args, capture_output=True, check=True)
        x, _ = sf.read(jacks)
        xm = real909.chain(x[:, ch[v]].astype(float), RATE, 'ar')[int(0.02 * RATE):]
        out.append(level(xm, RATE) - level(xr, sr))
    return round(float(np.mean(out)), 2), round(float(np.std(out)), 2)


def rs_measures(x, fs):
    return dict(lines=lines(x, fs), tau=[band_tau(x, fs, 222), band_tau(x, fs, 480)], env=env_times(x, fs),
                ratio=ratio(x, fs), lobe=lobe(x, fs), area=area(x, fs), level=round(level(x, fs), 2))


def hc_measures(x, fs):
    return dict(teeth=teeth(x, fs), fourth=fourth(x, fs), tail=tail_tau(x, fs), late_tau=tail_tau(x, fs, 0.12, 0.3),
                pulses_level=round(band_level(x, fs, 1500, 6000, 0, 0.03), 2), reverb_level=round(band_level(x, fs, 150, 1500, 0.12, 0.2), 2),
                pt=round(level(x, fs, 0, 0.035) - level(x, fs, 0.06, 0.12), 2), late=late(x, fs),
                pulses=seg_spectrum(x, fs, 0, 0.032), tailspec=seg_spectrum(x, fs, 0.12, 0.3),
                level=round(level(x, fs), 2))


def average(ms):
    out = {}
    for k in ms[0]:
        v = [m[k] for m in ms if m.get(k) is not None]
        if not v:
            out[k] = None
        elif isinstance(v[0], list):
            if v[0] and isinstance(v[0][0], list):
                out[k] = [[round(float(np.mean([r[i][j] for r in v])), 1) for j in range(len(v[0][i]))] for i in range(len(v[0]))]
            else:
                n = min(len(r) for r in v)
                out[k] = [None if any(r[i] is None for r in v) else round(float(np.mean([r[i] for r in v])), 2) for i in range(n)]
        else:
            out[k] = round(float(np.mean(v)), 2)
    return out


# ---------------------------------------------------------------------------------------------

class Run:
    """One configuration (a calibration set) rendered and measured."""

    def __init__(self, B, cal, seeds, pool):
        self.B, self.cal, self.seeds, self.pool = B, cal, seeds, pool

    def rs(self, code, seconds=0.25):
        return self.pool.submit(self.B.render, 'rs', code, seconds, self.cal)

    def hc(self, code, seconds=1.0):
        return [self.pool.submit(self.B.render, 'hc', code, seconds, self.cal, s) for s in range(self.seeds)]


def ar_takes():
    rs = [load(os.path.join(AR, 'RimShot', f'RimShot909-shot{i}.wav')) for i in range(1, 11)]
    hc = [load(os.path.join(AR, 'HandClap', f'HandClap909-shot{i}.wav')) for i in range(1, 9)]
    sweep = [round(0.1 * i, 1) for i in range(11)]
    rsa = {k: load(os.path.join(AR, 'RimShot', f'RimShot909-accent{int(round(100 * k)):03d}.wav')) for k in sweep}
    hca = {k: load(os.path.join(AR, 'HandClap', f'HandClap909-accent{int(round(100 * k)):03d}.wav')) for k in sweep}
    return rs, hc, rsa, hca


def evaluate(B, cal, seeds, jobs, ar):
    """Every measure of one configuration against AudioRealism."""
    ar_rs, ar_hc, ar_rsa, ar_hca = ar
    pool = cf.ThreadPoolExecutor(jobs)
    R = Run(B, cal, seeds, pool)
    sweep = sorted(ar_rsa)
    codes = sorted({real909.ar_code('RS', k) for k in sweep})
    rs_f = {c: R.rs(c) for c in codes}
    hc_f = {c: R.hc(c, 1.0 if c == 21 else 0.25) for c in codes}
    rs = {c: f.result() for c, f in rs_f.items()}
    hc = {c: [f.result() for f in fs] for c, fs in hc_f.items()}
    pool.shutdown()
    m = dict(rs=rs_measures(rs[21][1], RATE), hc=average([hc_measures(x[1], RATE) for x in hc[21]]))
    m['rs']['ncc'] = round(float(np.mean([ncc(rs[21][1], x, fs) for x, fs in ar_rs])), 3)
    m['rs_minus_hc'] = round(m['rs']['level'] - m['hc']['level'], 2)
    m['hc']['pp_jack'] = round(float(np.mean([np.ptp(x[0][:int(0.2 * RATE)]) for x in hc[21]])), 3)
    rs0 = level(rs[21][1], RATE, 0, 0.2)
    hc0 = float(np.mean([level(x[1], RATE, 0, 0.2) for x in hc[21]]))
    m['accent'] = [dict(knob=k, code=real909.ar_code('RS', k),
                        rs=round(level(rs[real909.ar_code('RS', k)][1], RATE, 0, 0.2) - rs0, 2),
                        hc=round(float(np.mean([level(x[1], RATE, 0, 0.2) for x in hc[real909.ar_code('HC', k)]])) - hc0, 2))
                   for k in sweep]
    m['hc_teeth_by_code'] = {c: average([dict(t=teeth(x[1], RATE)) for x in hc[c]])['t'] for c in codes}
    return m, rs, hc


def real_measures(ar):
    ar_rs, ar_hc, ar_rsa, ar_hca = ar
    m = dict(rs=average([rs_measures(x, fs) for x, fs in ar_rs]), hc=average([hc_measures(x, fs) for x, fs in ar_hc]))
    m['rs_minus_hc'] = round(m['rs']['level'] - m['hc']['level'], 2)
    rs0, hc0 = level(*ar_rsa[0.0], 0, 0.2), level(*ar_hca[0.0], 0, 0.2)
    m['accent'] = [dict(knob=k, rs=round(level(*ar_rsa[k], 0, 0.2) - rs0, 2), hc=round(level(*ar_hca[k], 0, 0.2) - hc0, 2))
                   for k in sorted(ar_rsa)]
    m['hc_teeth_by_take'] = {k: teeth(*ar_hca[k]) for k in sorted(ar_hca)}
    return m


def other_units():
    o = {}
    for name, path in (('Rob Roy RIM127', os.path.join(RR, 'RIM127.WAV')), ('archive.org Set2 RIMSHOT', os.path.join(AO, 'Set2', 'RIMSHOT.WAV')),
                       ('Regebro RS acc1', os.path.join(REG, 'RS_acc1.wav'))):
        if os.path.exists(path):
            x, fs = load(path)
            d = dict(lines=lines(x, fs), tau=[band_tau(x, fs, 222), band_tau(x, fs, 480)] if len(x) > 0.036 * fs else None)
            if not name.startswith('Rob Roy'):  # RR is normalised to both rails
                d.update(ratio=ratio(x, fs), lobe=lobe(x, fs), env=env_times(x, fs))
            o[name] = d
    for name, path in (('Rob Roy HANDCLP1', os.path.join(RR, 'HANDCLP1.WAV')), ('Rob Roy HANDCLP2', os.path.join(RR, 'HANDCLP2.WAV')),
                       ('Regebro CP acc1', os.path.join(REG, 'CP_acc1.wav'))):
        if os.path.exists(path):
            x, fs = load(path)
            o[name] = dict(teeth=teeth(x, fs), fourth=fourth(x, fs), tail=tail_tau(x, fs) if len(x) > 0.25 * fs else None)
    reg = {}
    for v in ('RS', 'CP'):
        p = [os.path.join(REG, f'{v}_acc{i}.wav') for i in range(1, 5)]
        if all(os.path.exists(q) for q in p):
            lv = [level(*load(q), 0, 0.3) for q in p]
            reg[v] = [round(x - lv[0], 2) for x in lv]
    o['Regebro accent levels acc1-4 (dB re acc1)'] = reg
    return o


# ---------------------------------------------------------------------------------------------

def adjust_tm(B, cal, seeds, jobs):
    """The TM at which the clap's mean peak-to-peak at the MULTI OUT jack is 2 V (code 21, LEVEL
    0.5), the service notes' adjustment done on the model."""
    def pp(tm):
        c = dict(cal, **{'hc.tm': tm})
        with cf.ThreadPoolExecutor(jobs) as ex:
            v = list(ex.map(lambda s: float(np.ptp(B.render('hc', 21, 0.15, c, s)[0])), range(seeds)))
        return float(np.mean(v))
    lo, hi = 0.15, 0.4
    plo, phi = pp(lo), pp(hi)
    for _ in range(10):
        mid = lo + (hi - lo) * (math.log(2) - math.log(plo)) / (math.log(phi) - math.log(plo))
        mid = min(max(mid, lo + 0.1 * (hi - lo)), hi - 0.1 * (hi - lo))
        p = pp(mid)
        lo, plo, hi, phi = (mid, p, hi, phi) if p < 2 else (lo, plo, mid, p)
        if hi - lo < 0.002:
            break
    tm = lo + (hi - lo) * (math.log(2) - math.log(plo)) / (math.log(phi) - math.log(plo))
    return round(tm, 4), round(pp(tm), 3)


def fit(B, what, base, seeds, jobs, ar, real):
    """The grids the calibration was read from: one part at a time, the rest at `base`."""
    pool = cf.ThreadPoolExecutor(jobs)
    if what == 'tm':
        print('TM: the clap at code 21 (fit: tail, p/t on the 8 shots) and 47 (held out: the accent range)')
        print(f"  AudioRealism: tail {real['hc']['tail']} ms, p/t {real['hc']['pt']} dB, 21->47 "
              f"{real['accent'][-1]['hc']:+.2f} dB, fourth {real['hc']['fourth']} dB")
        for tm in (0.255, 0.28, 0.29, 0.30, 0.31, 0.32, 0.33):
            c = dict(base, **{'hc.tm': tm})
            f21 = [pool.submit(B.render, 'hc', 21, 0.4, c, s) for s in range(seeds)]
            f47 = [pool.submit(B.render, 'hc', 47, 0.25, c, s) for s in range(seeds)]
            m = average([hc_measures(f.result()[1], RATE) for f in f21])
            l21 = np.mean([level(f.result()[1], RATE, 0, 0.2) for f in f21])
            l47 = np.mean([level(f.result()[1], RATE, 0, 0.2) for f in f47])
            print(f"  tm {tm:.3f}: tail {m['tail']} ms, p/t {m['pt']} dB, 21->47 {l47 - l21:+.2f} dB, fourth {m['fourth']} dB, "
                  f"level {m['level']} dB, p-p at the jack {np.mean([np.ptp(f.result()[0][:19200]) for f in f21]):.2f} V")
    elif what == 'c65':
        print('C65: the teeth at code 21 (fit: the 8 shots; held out: the 11 accent takes)')
        print(f"  AudioRealism shots {real['hc']['teeth']}; accent takes "
              f"{average([dict(t=t) for t in real['hc_teeth_by_take'].values()])['t']}")
        for c65 in (0.36e-6, 0.38e-6, 0.40e-6, 0.41e-6, 0.42e-6, 0.44e-6, 0.47e-6):
            c = dict(base, **{'hc.c65': c65})
            f = [pool.submit(B.render, 'hc', 21, 0.06, c, s) for s in range(seeds)]
            t = average([dict(t=teeth(g.result()[1], RATE)) for g in f])['t']
            print(f'  c65 {c65 * 1e6:.2f}u: teeth {t}, intervals {np.round(np.diff(t), 2).tolist()}')
    elif what == 'rslines':
        print(f"RS lines: AudioRealism {real['rs']['lines']}")
        for k, pair in (('F1', ('rs.c112', 'rs.c113')), ('F2', ('rs.c115', 'rs.c116')), ('F3', ('rs.c117', 'rs.c118'))):
            nom = {'F1': 0.01e-6, 'F2': 0.027e-6, 'F3': 0.0047e-6}[k]
            for s in (1.0, 1.02, 1.03, 1.04) if k != 'F2' else (0.98, 0.987, 1.0):
                c = dict(base, **{pair[0]: nom * s, pair[1]: nom * s})
                print(f'  {k} C x {s}: lines {rs_measures(pool.submit(B.render, "rs", 21, 0.25, c).result()[1], RATE)["lines"]}')
    elif what == 'rsout':
        print(f"RS output stage: AudioRealism ratio {real['rs']['ratio']}, lobe {real['rs']['lobe']}, area {real['rs']['area']}, "
              f"env {real['rs']['env']}, RS - HC {real['rs_minus_hc']} dB")
        hcl = np.mean([level(f.result()[1], RATE) for f in [pool.submit(B.render, 'hc', 21, 0.06, base, s) for s in range(seeds)]])
        for name, c in (('as drawn', {'rs.r421': 4.7e3, 'rs.c121': 0.01e-6, 'rs.c122': 0.01e-6}),
                        ('C121/C122 0.047u', {'rs.r421': 4.7e3, 'rs.c121': 0.047e-6, 'rs.c122': 0.047e-6}),
                        ('R423 27k, C121/C122 0.047u', {'rs.r421': 4.7e3, 'rs.c121': 0.047e-6, 'rs.c122': 0.047e-6, 'rs.r423': 27e3}),
                        ('no high-pass, gain 2.2', {'rs.r421': 4.7e3, 'rs.c121': 100e-6, 'rs.c122': 100e-6}),
                        ('IC50a out (no high-pass, gain 1)', {'rs.r421': 1e-3, 'rs.c121': 100e-6, 'rs.c122': 100e-6}),
                        ('gain 1, C121/C122 1u', {'rs.r421': 1e-3, 'rs.c121': 1e-6, 'rs.c122': 1e-6})):
            cc = dict(base, **c)
            x21, x47 = pool.submit(B.render, 'rs', 21, 0.25, cc), pool.submit(B.render, 'rs', 47, 0.25, cc)
            m = rs_measures(x21.result()[1], RATE)
            print(f"  {name:34s} ratio {m['ratio']}, lobe {m['lobe']}, area {m['area']}, env {m['env']}, "
                  f"ncc {np.mean([ncc(x21.result()[1], x, fs) for x, fs in ar[0]]):.3f}, RS - HC {m['level'] - hcl:+.2f} dB, "
                  f"21->47 {level(x47.result()[1], RATE, 0, 0.2) - level(x21.result()[1], RATE, 0, 0.2):+.2f} dB")
    pool.shutdown()


def report(real, runs, gain):
    def row(label, *v):
        print(f'  {label:22s}' + ''.join(f'{str(x):46s}' for x in v))
    names = ['AudioRealism'] + list(runs)
    print('\nRIM SHOT (code 21, LEVEL 0.5; the model through AR\'s chain)')
    row('', *names)
    for k in ('lines', 'tau', 'env', 'ratio', 'lobe', 'area', 'ncc'):
        row(k, real['rs'].get(k, ''), *[r['rs'][k] for r in runs.values()])
    print('\nHAND CLAP (code 21, LEVEL 0.5)')
    row('', *names)
    for k in ('teeth', 'fourth', 'tail', 'late_tau', 'pt', 'late', 'pulses', 'tailspec', 'pp_jack'):
        row(k, real['hc'].get(k, ''), *[r['hc'].get(k) for r in runs.values()])
    row('RS - HC (0-50 ms)', real['rs_minus_hc'], *[r['rs_minus_hc'] for r in runs.values()])
    print(f'\nLevels against the other voices: model - AudioRealism - {gain[0]} dB (the BD and toms\' mean, sd {gain[1]} dB)')
    for label, v, k in (('RS 0-50 ms', 'rs', 'level'), ('HC 0-50 ms', 'hc', 'level'), ('HC pulses (1.5-6 kHz, 0-30 ms)', 'hc', 'pulses_level'),
                        ('HC reverb (150-1500 Hz, 120-200 ms)', 'hc', 'reverb_level')):
        print(f'  {label:36s}' + ''.join(f'{r[v][k] - real[v][k] - gain[0]:+8.2f}' for r in runs.values()))
    print('\nAccent sweep (RMS 0-200 ms, dB re the 0 % take): knob, code, RS and HC, AudioRealism then each model')
    for i, a in enumerate(real['accent']):
        r = [run['accent'][i] for run in runs.values()]
        print(f"  {a['knob']:.1f} {r[0]['code']:2d}  RS {a['rs']:+6.2f} " + ' '.join(f"{x['rs']:+6.2f}" for x in r)
              + f"   HC {a['hc']:+6.2f} " + ' '.join(f"{x['hc']:+6.2f}" for x in r))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'tr909', 'compare', 'rshc'))
    ap.add_argument('--voicerun', default=os.environ.get('VOICERUN', os.path.join(REPO, 'build', 'tr909', 'tr909voicerun')))
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--seeds', type=int, default=8)
    ap.add_argument('--only', choices=('nominal', 'unit'))
    ap.add_argument('--cal', action='append', default=[])
    ap.add_argument('--fit', choices=('tm', 'c65', 'rslines', 'rsout'))
    ap.add_argument('--adjust', action='store_true')
    ap.add_argument('--netlists')
    ap.add_argument('--plots', action='store_true')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    unit = dict(CAL)
    for s in a.cal:
        k, v = s.split('=', 1)
        unit[k] = v
    work = tempfile.mkdtemp(prefix='rshc', dir=a.out)
    B = Board(a.voicerun, work, a.netlists)
    ar = ar_takes()
    real = real_measures(ar)
    if a.adjust:
        tm, p = adjust_tm(B, {}, a.seeds, a.jobs)
        print(f'TM adjustment (the service notes\' parts): tm = {tm} gives {p} Vp-p at the jack (target 2, LEVEL 0.5, code 21)')
        return 0
    if a.fit:
        fit(B, a.fit, unit, a.seeds, a.jobs, ar, real)
        return 0
    runs, renders = {}, {}
    for name, cal in (('nominal', {}), ('unit', unit)):
        if a.only and a.only != name:
            continue
        runs[name], rs, hc = evaluate(B, cal, a.seeds, a.jobs, ar)
        renders[name] = (rs[21][1], hc[21][0][1])
    gain = interface_gain(os.path.abspath(a.voicerun), work)
    report(real, runs, gain)
    others = other_units()
    print('\nOther units')
    for k, v in others.items():
        print(f'  {k}: {v}')
    with open(os.path.join(a.out, 'rshc.json'), 'w') as f:
        json.dump(dict(audiorealism=real, runs=runs, others=others, gain=gain), f, indent=1, default=str)
    if a.plots:
        plots(a.out, renders, ar)
    try:
        os.rmdir(work)
    except OSError:
        pass
    return 0


def plots(out, renders, ar):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(4, 1, figsize=(12, 14))
    for i, (name, span, tail) in enumerate((('RS', 0.03, 0.3), ('HC', 0.06, 0.9))):
        x, fs = (ar[0] if name == 'RS' else ar[1])[0]
        ax[2 * i].plot(np.arange(len(x)) / fs * 1e3, x / np.abs(x).max(), label='AudioRealism')
        e = rms_env(x, fs)
        ax[2 * i + 1].plot(np.arange(len(e)) / fs * 1e3, 20 * np.log10(e / e.max() + 1e-9), label='AudioRealism')
        for k, (run, (rs, hc)) in enumerate(renders.items()):
            m = rs if name == 'RS' else hc
            off = 2 * (k + 1) if name == 'HC' else 0
            ax[2 * i].plot(np.arange(len(m)) / RATE * 1e3, m / np.abs(m).max() - off, label=run, alpha=0.8)
            e = rms_env(m, RATE)
            ax[2 * i + 1].plot(np.arange(len(e)) / RATE * 1e3, 20 * np.log10(e / e.max() + 1e-9), label=run)
        ax[2 * i].set_xlim(0, span * 1e3)
        ax[2 * i].set_title(f'{name} waveform (normalised)')
        ax[2 * i + 1].set_ylim(-110, 3)
        ax[2 * i + 1].set_xlim(0, tail * 1e3)
        ax[2 * i + 1].set_title(f'{name} 2 ms RMS envelope (dB re peak)')
        for k in (2 * i, 2 * i + 1):
            ax[k].legend()
    fig.tight_layout()
    fig.savefig(os.path.join(out, 'rshc.png'), dpi=80)


if __name__ == '__main__':
    sys.exit(main())
