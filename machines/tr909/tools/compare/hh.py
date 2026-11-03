#!/usr/bin/env python3
"""The hi-hat against real TR-909s: renders each recording's settings through the voicing board
(tr909voicerun, the calibration as given) and measures both the same way (docs/circuit/hh.md,
"Against the real 909" and "Calibration").

    hh.py [--voicerun PATH] [--out DIR] [--jobs 2] [--only ar,rr,scherer,cut] [--nominal]
          [--cal KEY=VALUE ...]

--cal goes to tr909voicerun (a part, hh.c135=1.1u; a knob law, law.ohDecay=0,0,0.335,0.5,...);
--nominal starts from the service notes' unit (nominal=1) rather than unitCalibration().

The recordings (real909.py; private/reference/real909/index.md):
  ar       AudioRealism, a late unit (C134 0.01u), LEVEL 50 %: CH DECAY 0-100 % (OH DECAY at 0:
           its CH decays need it), the 11 accent takes of CH (CH and OH DECAY at 100 %: their
           decay is the model's there) and OH (OH DECAY 100 %) at 96 kHz, OH DECAY 0-100 % in the
           44.1 kHz edition; OH cut by CH (master out).
  rr       Rob Roy 1995, an early unit (C134 10u), LEVEL full, normalised, 44.1 kHz: CH (OH DECAY 0)
           and OH DECAY dial 0-10 in 2s.
  scherer  a third unit, CH and OH DECAY turned together, 8 states (each OH from bars 2k-1, 2k, its
           CH from bars 2k-2, 2k-1, as the knobs moved between steps 1 and 7 of the even bars;
           state 7's OH bar 13, state 8's bars 15-16): each state's rotation is the one whose OH
           decay the model gives at the recording's, and the CH is checked there.
The model plays each at the firmware's accent code (real909.ar_code: CH 40, OH 32; the accent
takes through AR_ACCENT_TA); its output is the board's HH jack turned into a plugged one (the mix
resistors off, a 100k input: R521 / C508 at 15.9 kHz), and AR's recording chain (real909.chain)
for AR.

Measures, on both:
  tau      the VCA's decay: the gain of each window (3 ms CH, 8 ms OH) against the ROM's data
           played at the clock and aligned (least squares, 1.5-12 kHz; a recording's clock refined
           over its first 300 ms), a line fitted to its log from 3 ms after the trigger to 30 dB
           down (the stored sample's own decay cancels)
  t20 t40  the last time the 10 ms RMS is above -20 / -40 dB re its peak; tail: the decay's time
           constant from -10 to -40 dB (real909.measure, as docs/reference.md's tables)
  level    within a series, the take's energy against the series' reference take
  lsd      log-spectral distance (real909.lsd, sixth octaves, first 80 ms) and the octave bands
           500 Hz-16 kHz, energy matched (recording minus model)
  asym     the first 20 ms's positive against negative peaks (99.9th / 0.1th percentile, the
           recording's polarity from its correlation with the ROM): Q85's exponential
  gap      (cut) where the open hat's data stops and the closed hat's starts (HiHat::kClosedLead)
"""
import argparse
import concurrent.futures as cf
import hashlib
import json
import math
import os
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import real909 as R  # noqa: E402

REPO = os.path.normpath(os.path.join(HERE, '../../../..'))
CLOCK = 0.5 / (2.808 * 12e3 * 470e-12)  # HiHat's default: 31.571 kHz
AR44 = os.path.join(R.REAL909, 'packs', 'audiorealism_tr909_ultimate', 'wav', 'TR-909-44.1kHz-16bit', 'OpenHat')
AR96 = os.path.join(R.REAL909, 'packs', 'audiorealism_tr909_ultimate', 'wav', 'TR-909-96kHz-24bit', 'OpenHat')
OCT = [500, 1000, 2000, 4000, 8000, 16000]
VOICE_CH = 7  # tr909voicerun --jacks: the HH channel


# ---------------------------------------------------------------------------------------------
# The model

class Model:
    def __init__(self, voicerun, out, cal):
        self.vr, self.cal = voicerun, list(cal)
        self.work = os.path.join(out, 'renders')
        os.makedirs(self.work, exist_ok=True)

    def render(self, hits, knobs, seconds, cal=()):
        """hits: [(t, 'oh'|'ch', code)], knobs: Controls fields. The HH's MULTI OUT jack with a
        cable in, at 96 kHz."""
        args = ['--rate', '96000', '--seconds', f'{seconds:.3f}']
        for t, k, c in hits:
            args += ['--hit', f'{t}:{k}:{c}']
        for k, v in knobs.items():
            args += ['--set', f'{k}={v}']
        for c in self.cal + list(cal):
            args += ['--cal', c]
        key = hashlib.md5(' '.join(args).encode()).hexdigest()[:20]
        jp = os.path.join(self.work, key + '.wav')
        if not os.path.exists(jp):
            mp = os.path.join(self.work, key + '_master.wav')
            r = subprocess.run([self.vr, mp, *args, '--jacks', jp], capture_output=True, text=True)
            if r.returncode != 0:
                raise RuntimeError(r.stderr)
            os.remove(mp)
        x, sr = sf.read(jp)
        return signal.lfilter(*plugged(sr), x[:, VOICE_CH]), sr

    def hit(self, which, code, knobs, seconds=None, cal=(), source='ar', sr_to=None):
        """One hit from 1 ms before its trigger, through `source`'s chain, at sr_to."""
        seconds = seconds or (0.45 if which == 'ch' else 0.95)
        y, sr = self.render([(0.01, which, code)], knobs, seconds + 0.01, cal)
        y = R.chain(y, sr, source)[int(0.009 * sr):]
        if sr_to and sr_to != sr:
            y, sr = resample(y, sr, sr_to), sr_to
        return y, sr


def plugged(sr, r_in=100e3):
    """The board's jack (mix.cir's, nothing plugged in: R521 1k into C508 and the mix resistors
    R522 15k / R523 8.2k) to the same jack with a cable in (the mix resistors off, the cable's
    r_in): the ratio of the two first-order dividers, (b, a) at sr."""
    rm, r, c = 15e3 * 8.2e3 / 23.2e3, 1e3, 0.01e-6
    num = np.poly1d([r * rm * c, rm + r]) * r_in / rm  # 1 / H_unplugged ...
    den = np.poly1d([r * r_in * c, r_in + r])          # ... times H_plugged
    return signal.bilinear(num.coeffs, den.coeffs, fs=sr)


def resample(y, sr, to):
    g = math.gcd(int(sr), int(to))
    return signal.resample_poly(y, int(to) // g, int(sr) // g)


def linear_chain(sr, late=True):
    """The chain from the D/A's voltage to the jack as a linear filter (the VCA a constant):
    C157 into 22 ohm, the two Sallen-Keys, C134 / R444, R521 / C508. (b, a) at sr."""
    num, den = np.poly1d([22 * 10e-6, 0]), np.poly1d([10e-6 * 2712, 1])
    for c1, c2 in ((1200e-12, 1000e-12), (2700e-12, 390e-12)):
        den = den * np.poly1d([5.6e3 ** 2 * c1 * c2, 2 * 5.6e3 * c2, 1])
    t134 = 22e3 * (0.01e-6 if late else 10e-6)
    num, den = num * np.poly1d([t134, 0]), den * np.poly1d([t134, 1])
    den = den * np.poly1d([1e-5, 1])
    return signal.bilinear(num.coeffs, den.coeffs, fs=sr)


# ---------------------------------------------------------------------------------------------
# Measures

def zoh(data, rate, sr, n, t0=0.0):
    """The ROM's data as a zero-order hold at `rate`, its first sample at t0 (s), averaged over
    each output sample."""
    pos = (np.arange(n + 1) / sr - t0) * rate
    k = np.floor(pos).astype(int)
    cum = np.concatenate([[0.0], np.cumsum(data)])
    kk = np.clip(k, 0, len(data))
    integ = cum[kk] + np.where((k >= 0) & (k < len(data)), data[np.clip(k, 0, len(data) - 1)], 0.0) * (pos - k)
    integ = np.where(pos < 0, 0.0, integ)
    return np.diff(integ) * sr / rate


def played(which):
    """The ROM's data in the order the 909 plays it: the closed region from 6041 on, its 65 silent
    samples last (HiHat::kClosedLead)."""
    d = R.rom(which)
    return np.roll(d, -65) if which == 'ch' else d


def align(x, sr, which, rate=CLOCK):
    """Where the ROM's first sample plays in x (seconds, may be negative: x may start after it;
    the trigger is 24 us before it), and the polarity, by cross-correlation of the band-passed
    first 40 ms of sound (to a quarter of an output sample)."""
    data = played(which)
    pad = int(0.005 * sr)
    xb = R.bandpass(np.concatenate([np.zeros(pad), x[:int(0.1 * sr)]]), sr, 1500, 12000, 2)
    best = (0.0, 0.0, 1.0)
    L = int(0.04 * sr)
    for q in range(4):
        ref = R.bandpass(zoh(data, rate, sr, int(0.06 * sr), -q / (4 * sr)), sr, 1500, 12000, 2)[:L]
        c = signal.correlate(xb, ref, mode='valid')
        e = np.sqrt(np.convolve(xb * xb, np.ones(L), mode='valid')) * np.linalg.norm(ref) + 1e-30
        r = c / e
        lag = int(np.argmax(np.abs(r)))
        if abs(r[lag]) > abs(best[0]):
            best = (r[lag], (lag - pad) / sr - q / (4 * sr), float(np.sign(r[lag])))
    return best[1], best[2]


def trim_trig(x, sr, which, rate=CLOCK):
    """x from 1 ms before the trigger (from the ROM alignment), padded if it starts later."""
    t0, _ = align(x, sr, which, rate)
    i = int(round((t0 - 24e-6 - 1e-3) * sr))
    return x[i:] if i >= 0 else np.concatenate([np.zeros(-i), x])


def clock(x, sr, which, t1=0.3):
    """A recording's sample clock: real909.rom_rate (its first 43 ms), refined to 0.5 Hz on the
    summed window-by-window NCC over 5 ms-t1 (a clock 10 Hz off loses the ROM by 150 ms), coarse
    to fine (8, 2, 0.5 Hz steps)."""
    r0 = R.rom_rate(R.trim(x, sr)[0], sr, which)['rate_hz']
    x = trim_trig(x, sr, which, r0)
    xb = R.bandpass(x[:int((t1 + 0.01) * sr)], sr, 1500, 12000, 2)
    W = int(0.01 * sr)

    def score(r):
        t0, _ = align(x, sr, which, r)
        ref = R.bandpass(zoh(played(which), r, sr, len(xb), t0), sr, 1500, 12000, 2)
        a, b = xb[int(0.005 * sr):], ref[int(0.005 * sr):]
        s = 0.0
        for k in range(0, len(a) - W, W):
            aa, bb = a[k:k + W], b[k:k + W]
            s += abs(float(np.dot(aa, bb))) / math.sqrt(float(np.dot(aa, aa)) * float(np.dot(bb, bb)) + 1e-30)
        return s

    best = r0
    for span, step in ((40.0, 8.0), (8.0, 2.0), (2.0, 0.5)):
        best = max(np.arange(best - span, best + span + step / 2, step), key=score)
    return float(best)


def vca_gain(x, sr, which, win_ms, rate=CLOCK):
    """(t from the trigger s, gain, ncc, polarity) per window against the ROM data, aligned and
    played at `rate`; x from about 1 ms before the trigger."""
    t0, sgn = align(x, sr, which, rate)
    ref = zoh(played(which), rate, sr, len(x), t0)
    xb = R.bandpass(x, sr, 1500, 12000, 2) * sgn
    rb = R.bandpass(ref, sr, 1500, 12000, 2)
    W = int(win_ms * sr / 1000)
    ts, gs, cs = [], [], []
    for s in range(max(0, int(t0 * sr)), len(x) - W, W):
        a, b = xb[s:s + W], rb[s:s + W]
        bb = float(np.dot(b, b))
        if bb < 1e-9:
            continue
        ts.append((s + W / 2) / sr - t0 + 24e-6)
        gs.append(float(np.dot(a, b)) / bb)
        cs.append(float(np.dot(a, b)) / math.sqrt(float(np.dot(a, a)) * bb + 1e-30))
    return np.array(ts), np.array(gs), np.array(cs), sgn


def fit_tau(t, g, c, t_from=3e-3, range_db=30.0, ncc_min=0.85):
    """The VCA's time constant (ms): a line through log gain from t_from to range_db down."""
    m = (t >= t_from) & (c > ncc_min) & (g > 0)
    if m.sum() < 4:
        return float('nan')
    tt, lg = t[m], np.log(g[m])
    top = np.median(lg[:3])
    k = np.nonzero(lg < top - range_db / 8.686)[0]
    end = k[0] if len(k) else len(lg)
    if end < 4:
        return float('nan')
    p = np.polyfit(tt[:end], lg[:end], 1)
    return -1000 / p[0] if p[0] < 0 else float('nan')


def asym(x, sr, sgn=1.0, t1=0.02):
    s = sgn * x[:int(t1 * sr)]
    return float(np.percentile(s, 99.9) / -np.percentile(s, 0.1))


def octave_diff(a, sr_a, b, sr_b, t1=0.08):
    fa, A = R.band_levels(a, sr_a, (0.0, t1), per_oct=1, fmin=354, fmax=23000, kind='flat')
    fb, B = R.band_levels(b, sr_b, (0.0, t1), per_oct=1, fmin=354, fmax=23000, kind='flat')
    n = min(len(A), len(B))
    A, B = A[:n], B[:n]
    g = 10 * np.log10(np.sum(10 ** (A / 10))) - 10 * np.log10(np.sum(10 ** (B / 10)))
    return fa[:n], A - B - g  # recording minus model, energy matched


def measures(x, sr, which, rate):
    """The measures of one hit (x from about 1 ms before its trigger)."""
    t, g, c, sgn = vca_gain(x, sr, which, 3 if which == 'ch' else 8, rate)
    m = R.measure(R.trim(x, sr)[0], sr, which.upper())
    return dict(tau=fit_tau(t, g, c), t20=m['t20'], t40=m['t40'], tail=m['tail_tau_ms'], energy=R.energy_db(x, sr),
                sgn=sgn, rate=rate)


_cache, _cache_path = {}, None


def recording(path, which):
    """A recording from 1 ms before its trigger, its clock and measures (cached in --out)."""
    x, sr = R.load(path)
    key = f'{path}:{os.path.getmtime(path)}'
    if key not in _cache:
        rate = clock(x, sr, which)
        _cache[key] = measures(trim_trig(x, sr, which, rate), sr, which, rate)
        json.dump(_cache, open(_cache_path, 'w'), indent=1, default=float)
    m = _cache[key]
    return trim_trig(x, sr, which, m['rate']), sr, m


def _measure_rec(job):
    path, which = job
    key = f'{path}:{os.path.getmtime(path)}'
    if key in _cache:
        return path, which, _cache[key]
    x, sr = R.load(path)
    rate = clock(x, sr, which)
    return path, which, measures(trim_trig(x, sr, which, rate), sr, which, rate)


# ---------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--voicerun', default=os.environ.get('VOICERUN', os.path.join(REPO, 'build/tr909/tr909voicerun')))
    ap.add_argument('--out', default=os.path.join(REPO, 'build/tr909/hhcompare'))
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--only', default='ar,rr,scherer,cut')
    ap.add_argument('--nominal', action='store_true')
    ap.add_argument('--cal', action='append', default=[])
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    global _cache, _cache_path
    _cache_path = os.path.join(a.out, 'recordings.json')
    if os.path.exists(_cache_path):
        _cache = json.load(open(_cache_path))
    model = Model(a.voicerun, a.out, (['nominal=1'] if a.nominal else []) + a.cal)
    only = a.only.split(',')

    # (series, label, path, voice, knobs, code, extra cal, source, reference label)
    jobs = []
    if 'ar' in only:
        for d in (0, 0.25, 0.5, 0.75, 1):
            jobs.append(('AR CH DECAY (OH DECAY 0)', f'{int(d * 100):3d} %', R.get('CH', 'ar', decay=d).path, 'ch',
                         {'chDecay': d, 'ohDecay': 0, 'hhLevel': 0.5}, R.ar_code('CH'), (), 'ar', '100 %'))
        for d in (0, 25, 50, 75, 100):
            jobs.append(('AR OH DECAY (44.1 kHz)', f'{d:3d} %', os.path.join(AR44, f'OpendHat909-decay{d:03d}.wav'), 'oh',
                         {'ohDecay': d / 100, 'chDecay': 0.5, 'hhLevel': 0.5}, R.ar_code('OH'), (), 'ar', '100 %'))
        for v in ('CH', 'OH'):
            for acc in [k / 10 for k in range(11)]:
                jobs.append((f'AR {v} accent (DECAYs 100 %)', f'{acc:.1f} (code {R.ar_code(v, acc)})',
                             R.get(v, 'ar', accent=acc, decay=1).path, v.lower(), {'chDecay': 1, 'ohDecay': 1, 'hhLevel': 0.5},
                             R.ar_code(v, acc), (), 'ar', f'1.0 (code {R.ar_code(v, 1.0)})'))
    if 'rr' in only:
        for v in ('CH', 'OH'):
            for dial in range(0, 11, 2):
                knobs = {'chDecay': dial / 10, 'ohDecay': 0, 'hhLevel': 1} if v == 'CH' else \
                    {'ohDecay': dial / 10, 'chDecay': 0.5, 'hhLevel': 1}
                jobs.append((f'RR {v} DECAY' + (' (OH DECAY 0)' if v == 'CH' else ''), f'dial {dial:2d}',
                             R.get(v, 'rr', decay=dial / 10).path, v.lower(), knobs, 40 if v == 'CH' else 32, ('hh.c134=10u',),
                             'rr', 'dial 10'))

    def one(j):
        series, label, path, which, knobs, code, cal, source, ref = j
        x, sr, mx = recording(path, which)
        y, _ = model.hit(which, code, knobs, cal=cal, source=source, sr_to=sr)
        my = measures(y, sr, which, CLOCK)
        n = min(len(x), len(y))
        ox, oy = x[int(0.001 * sr):n], y[int(0.001 * sr):n]
        fb, od = octave_diff(ox, sr, oy, sr)
        return dict(series=series, label=label, ref=ref, x=mx, y=my, lsd=R.lsd(ox, sr, oy, sr, window=(0.0, 0.08)),
                    oct=od, fb=fb, asym_x=asym(ox, sr, mx['sgn']), asym_y=asym(oy, sr, my['sgn']))

    # the recordings' clocks and measures first, in processes (cached)
    todo = sorted({(j[2], j[3]) for j in jobs})
    if 'scherer' in only:
        todo += [(c.path, c.voice.lower()) for c in R.catalogue() if c.source == 'scherer' and c.extra.get('out') == 'individual_out']
    with cf.ProcessPoolExecutor(a.jobs) as ex:
        for path, which, m in ex.map(_measure_rec, todo):
            _cache[f'{path}:{os.path.getmtime(path)}'] = m
    json.dump(_cache, open(_cache_path, 'w'), indent=1, default=float)
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        res = list(ex.map(one, jobs))
    series = []
    for r in res:
        if r['series'] not in series:
            series.append(r['series'])
    for s in series:
        rows = [r for r in res if r['series'] == s]
        refrow = next((r for r in rows if r['label'] == r['ref']), rows[-1])
        print(f'\n{s}: real / model (level re {refrow["ref"]}; octaves {", ".join(f"{f / 1000:g}k" for f in OCT)}, real - model dB)')
        print(f'  {"take":18s} {"clock":>6s} {"tau ms":>13s} {"t-20 ms":>11s} {"t-40 ms":>11s} {"tail ms":>11s} {"level dB":>13s} {"lsd":>5s} {"asym":>11s}   octaves')
        for r in rows:
            X, Y = r['x'], r['y']
            lx, ly = X['energy'] - refrow['x']['energy'], Y['energy'] - refrow['y']['energy']
            oc = ' '.join(f'{v:+5.1f}' for f, v in zip(r['fb'], r['oct']) if 350 < f < 18000)
            print(f'  {r["label"]:18s} {X["rate"]:6.0f} {X["tau"]:6.1f}/{Y["tau"]:6.1f} {X["t20"]:5.0f}/{Y["t20"]:5.0f} {X["t40"]:5.0f}/{Y["t40"]:5.0f}'
                  f' {X["tail"]:5.1f}/{Y["tail"]:5.1f} {lx:+6.2f}/{ly:+6.2f} {r["lsd"]:5.2f} {r["asym_x"]:5.2f}/{r["asym_y"]:5.2f}   {oc}')

    if 'scherer' in only:
        scherer(model, a.jobs)
    if 'cut' in only:
        cut(model)


def scherer(model, jobs):
    """scherer's unit, both DECAY knobs turned together: each state's rotation from its OH decay
    through the model's OH DECAY law, the CH checked there."""
    def rec(voice, bars, loud=False):
        out = []
        for c in R.catalogue():
            if c.source == 'scherer' and c.voice == voice and c.extra.get('out') == 'individual_out' and c.extra['bar'] in bars:
                if voice == 'CH' and (c.extra.get('accent_inferred') == 'on') != loud:
                    continue
                out.append(recording(c.path, voice.lower())[2])
        return out
    states = []
    for k in range(1, 9):
        ob = [2 * k - 1, 2 * k] if k < 7 else ([13] if k == 7 else [15, 16])
        cb = [2 * k - 2, 2 * k - 1] if k < 8 else [14, 15]
        o, c = rec('OH', ob), rec('CH', cb)
        states.append((np.mean([m['tau'] for m in o]), np.mean([m['tau'] for m in c]), np.mean([m['t20'] for m in c])))
    xs = np.linspace(0, 1, 21)
    with cf.ThreadPoolExecutor(jobs) as ex:
        curve = list(ex.map(lambda x: measures(model.hit('oh', 32, {'ohDecay': x, 'hhLevel': 0.5}, source='scherer')[0], 96000, 'oh', CLOCK)['tau'], xs))
    rot = [float(np.interp(toh, curve, xs)) for toh, _, _ in states]
    with cf.ThreadPoolExecutor(jobs) as ex:
        ch = list(ex.map(lambda x: measures(model.hit('ch', 40, {'chDecay': x, 'ohDecay': x, 'hhLevel': 0.5}, source='scherer')[0], 96000, 'ch', CLOCK), rot))
    print('\nscherer, CH and OH DECAY together: each state at the rotation whose OH decay is the recording\'s')
    print(f'  {"state":5s} {"OH tau":>7s} {"rotation":>8s} {"CH tau real/model":>18s} {"CH t-20 real/model":>19s}')
    errs = []
    for k, ((toh, tch, t20), x, m) in enumerate(zip(states, rot, ch)):
        errs.append(math.log(m['tau'] / tch))
        print(f'  {k + 1:5d} {toh:7.1f} {x:8.3f} {tch:8.1f}/{m["tau"]:6.1f} ({100 * (m["tau"] / tch - 1):+3.0f} %) {t20:7.0f}/{m["t20"]:5.0f}')
    print(f'  CH tau rms {100 * math.sqrt(np.mean(np.square(errs))):.0f} %')


def cut_times(x, sr, rate, win_ms=0.5):
    """For an open hat cut by a closed one in x (from 1 ms before the open hat's trigger): the
    time the open hat's ROM data stops matching (the first of four 0.5 ms windows in a row under
    NCC 0.5) and the time the closed hat's data starts (aligned on its first 10 ms), both from
    the open hat's trigger, seconds. `rate`: the clock."""
    t0, _ = align(x, sr, 'oh', rate)
    rb = R.bandpass(zoh(played('oh'), rate, sr, len(x), t0), sr, 1500, 12000, 2)
    xb = R.bandpass(x, sr, 1500, 12000, 2)
    W = int(win_ms * sr / 1000)
    nc = []
    s0 = int(max(t0, 0) * sr)
    for s in range(s0, len(x) - W, W):
        a_, b_ = xb[s:s + W], rb[s:s + W]
        nc.append(abs(np.dot(a_, b_)) / math.sqrt(np.dot(a_, a_) * np.dot(b_, b_) + 1e-30))
    nc = np.array(nc)
    bad = np.convolve(nc < 0.5, np.ones(4), 'valid') >= 4
    if not bad.any():
        return None
    t_end = (s0 + int(np.argmax(bad)) * W) / sr
    ch = played('ch')
    lo = max(0, int((t_end - 0.01) * sr))
    seg = xb[lo:lo + int(0.04 * sr)]
    L = int(0.01 * sr)
    best = (0.0, 0.0)
    for q in range(8):
        r2 = R.bandpass(zoh(ch[:800], rate, sr, int(0.02 * sr), q / (8 * sr)), sr, 1500, 12000, 2)[:L]
        c = np.correlate(seg, r2, 'valid')
        en = np.sqrt(np.convolve(seg ** 2, np.ones(L), 'valid')) * np.linalg.norm(r2) + 1e-30
        r = c / en
        i = int(np.argmax(np.abs(r)))
        if abs(r[i]) > abs(best[0]):
            best = (r[i], (lo + i) / sr + q / (8 * sr))
    return t_end - t0, best[1] - t0


def cut(model):
    print('\nAR OH DECAY 100 % cut by CH (master out): where the open hat\'s data stops and the closed hat\'s starts')
    print('(ms after the open hat\'s trigger; gap = closed start - open end; without HiHat::kClosedLead the model\'s would be 2.06)')
    gaps = ([], [])
    for cd in ('000', '100'):
        for sh in range(1, 8):
            x, sr = R.load(os.path.join(AR96, f'OpenHat-Dec100-cut-ClosedHat-Dec{cd}-Shuffle{sh}.wav'))
            rate = clock(x[:int(0.12 * sr)], sr, 'oh', t1=0.1)  # before the closed hat
            x = trim_trig(x, sr, 'oh', rate)
            rx = cut_times(x, sr, rate)
            if rx is None:
                continue
            y, ry = model.render([(0.01, 'oh', 47), (0.01 + rx[1] - 24e-6, 'ch', 55)],
                                 {'ohDecay': 1, 'chDecay': int(cd) / 100, 'hhLevel': 0.5}, rx[1] + 0.06)
            y = y[int(0.009 * ry):]
            ry_ = cut_times(y, ry, CLOCK)
            if ry_ is None:
                continue
            gaps[0].append(rx[1] - rx[0])
            gaps[1].append(ry_[1] - ry_[0])
            print(f'  CH DECAY {int(cd):3d} % shuffle {sh}: real {rx[0] * 1e3:6.1f} -> {rx[1] * 1e3:6.1f} (gap {(rx[1] - rx[0]) * 1e3:+.2f})'
                  f'   model {ry_[0] * 1e3:6.1f} -> {ry_[1] * 1e3:6.1f} (gap {(ry_[1] - ry_[0]) * 1e3:+.2f})')
    print(f'  median gap: real {np.median(gaps[0]) * 1e3:+.2f} ms, model {np.median(gaps[1]) * 1e3:+.2f} ms')


if __name__ == '__main__':
    sys.exit(main())
