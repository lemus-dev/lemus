#!/usr/bin/env python3
"""The voices against Michael Fischer's real TR-808 (serial 103852; private/reference/real808/fischer):
each recorded setting rendered through voicerun at the same knobs, LEVEL at max, from the voice's own
jack (MULTI OUT), and compared on shape, not level (the recording chain's gain is unknown):

  envelope  the hit's decay to -10, -20 and -40 dB of its peak (2 ms RMS windows)
  pitch     the strongest spectral peak in 20-120 ms, for the tuned voices (the rim shot: its low
            tone's zero crossings over 3-11 ms, zero_cross_hz)
  centroid  the spectral centroid of the first 60 ms (flat from the onset for a short hit, below)
  tail      the decay rate of the tail, from 30 ms after the peak (clear of a clipped attack)
  lsd       log-spectral distance of 30-330 ms (1/6-octave bands within 40 dB of the top, each set to its total)

A hit 20 dB down within 50 ms (closed hat, maracas, rim shot, claves) is mostly hiss after
30 ms: its pitch is taken over 2-40 ms and its lsd over the first 80 ms, flat from the onset (a
Hann window there would weigh the tail, not the hit).

  fischer.py [--voices BD,SD,...] [--accent] [--cal NAME=VALUE ...] [--set NAME=VALUE ...] [--json OUT]
             [--voicerun PATH] [--held-out | --fit-set]

Fischer's knob marks (0, 2.5, 5, 7.5, 10) are pot rotations 0-1. --fit-set takes the 0/5/10 marks
only, --held-out the 2.5/7.5 ones (the fitting plan: fit on one, check on the other).
"""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
REF = os.path.join(MACHINE, 'private', 'reference', 'real808')
VOICERUN = os.path.normpath(os.path.join(MACHINE, '..', '..', 'build', 'tr808', 'voicerun'))

# Fischer's folder -> (voice board voice, the Controls switch it needs, its knobs -> Controls fields)
VOICES = {
    'BD': ('bd', None, {'TONE': 'bdTone', 'DECAY': 'bdDecay', 'LEVEL': 'bdLevel'}),
    'SD': ('sd', None, {'TONE': 'sdTone', 'SNAPPY': 'sdSnappy', 'LEVEL': 'sdLevel'}),
    'LT': ('lt', None, {'TUNING': 'ltTuning', 'LEVEL': 'ltLevel'}),
    'MT': ('mt', None, {'TUNING': 'mtTuning', 'LEVEL': 'mtLevel'}),
    'HT': ('ht', None, {'TUNING': 'htTuning', 'LEVEL': 'htLevel'}),
    'LC': ('lt', 'lc', {'TUNING': 'ltTuning', 'LEVEL': 'ltLevel'}),
    'MC': ('mt', 'mc', {'TUNING': 'mtTuning', 'LEVEL': 'mtLevel'}),
    'HC': ('ht', 'hc', {'TUNING': 'htTuning', 'LEVEL': 'htLevel'}),
    'RS': ('rs', None, {'LEVEL': 'rsLevel'}),
    'CL': ('rs', 'cl', {'LEVEL': 'rsLevel'}),
    'CP': ('cp', None, {'LEVEL': 'cpLevel'}),
    'MA': ('cp', 'ma', {'LEVEL': 'cpLevel'}),
    'CB': ('cb', None, {'LEVEL': 'cbLevel'}),
    'CY': ('cy', None, {'TONE': 'cyTone', 'DECAY': 'cyDecay', 'LEVEL': 'cyLevel'}),
    'OH': ('oh', None, {'DECAY': 'ohDecay', 'LEVEL': 'ohLevel'}),
    'CH': ('ch', None, {'LEVEL': 'chLevel'}),
}
JACK = ['bd', 'sd', 'lt', 'mt', 'ht', 'rs', 'cp', 'cb', 'cy', 'oh', 'ch']
TUNED = {'BD', 'LT', 'MT', 'HT', 'LC', 'MC', 'HC', 'RS', 'CL', 'CB', 'SD'}


def records():
    d = json.load(open(os.path.join(REF, 'index.json')))
    s = next(s for s in d['sets'] if s['id'] == 'fischer')
    return s['files']


def render(rec, accent, extra_sets, cals, voicerun, seconds):
    voice, switch, knobmap = VOICES[rec['instrument']]
    sets = list(extra_sets)
    for k, v in rec['knobs'].items():
        if k in knobmap:
            sets.append('%s=%g' % (knobmap[k], v['normalised']))
    if switch:
        sets.append(switch + '=1')
    with tempfile.TemporaryDirectory() as tmp:
        jacks = os.path.join(tmp, 'j.wav')
        args = [voicerun, os.path.join(tmp, 'm.wav'), '--rate', '44100', '--seconds', '%g' % seconds,
                '--hit', '0.05:' + voice + ('+ac' if accent else ''), '--jacks', jacks]
        for s in sets:
            args += ['--set', s]
        for c in cals:
            args += ['--cal', c]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode:
            sys.exit('voicerun failed: ' + r.stderr)
        x, sr = sf.read(jacks)
    y = x[:, JACK.index(voice)]
    return y[int(0.05 * sr) - 20:], sr


def onset(x):
    a = np.abs(x)
    return int(np.argmax(a > a.max() * 0.01))


def envelope(x, sr):
    w = int(0.002 * sr)
    n = len(x) // w
    e = np.sqrt(np.mean(x[:n * w].reshape(n, w) ** 2, axis=1) + 1e-30)
    return 20 * np.log10(e / e.max()), w / sr


def decay_times(x, sr):
    e, dt = envelope(x, sr)
    p = int(np.argmax(e))
    out = []
    for lvl in (-10, -20, -40):
        after = np.where(e[p:] < lvl)[0]
        out.append((after[0] * dt * 1000) if len(after) else float('nan'))
    return out


def decay_rate(x, sr, floor_db=-66):
    """The tail's decay in dB/s: a line fitted to the envelope from 30 ms after its peak until it
    is 40 dB down or reaches the recording's floor (dBFS), clear of a clipped attack."""
    e, dt = envelope(x, sr)
    lev = 20 * np.log10(np.sqrt(np.mean(x[:len(x) // int(0.002 * sr) * int(0.002 * sr)].reshape(-1, int(0.002 * sr)) ** 2, axis=1)) + 1e-30)
    p = int(np.argmax(e))
    i0 = p + int(0.03 / dt)
    if i0 >= len(e) - 5:
        return float('nan')
    start = e[i0]
    end = i0
    while end < len(e) and e[end] > start - 40 and lev[end] > floor_db:
        end += 1
    if end - i0 < 5:
        return float('nan')
    t = np.arange(i0, end) * dt
    return float(np.polyfit(t, e[i0:end], 1)[0])


def pitch(x, sr, band=(30, 5000)):
    # 20-120 ms, or 2-40 ms for a hit 20 dB down within 50 ms (the rim shot is over by 20 ms)
    short = decay_times(x, sr)[1] < 50
    t0, t1 = (0.002, 0.04) if short else (0.02, 0.12)
    s = x[int(t0 * sr):int(t1 * sr)]
    if len(s) < 256:
        return float('nan')
    n = 1 << 17
    X = np.abs(np.fft.rfft(s * taper(len(s), short), n))
    f = np.fft.rfftfreq(n, 1 / sr)
    keep = (f > band[0]) & (f < band[1])
    return float(f[keep][np.argmax(X[keep])])


# A voice's pitch band where its strongest line is ambiguous: the rim shot's 455 Hz and 1.7 kHz
# peaks are within 0.3 dB of each other in both real units, so its pitch is the low tone's.
PITCH_BAND = {'RS': (300, 700)}


def zero_cross_hz(x, sr, t0=0.003, t1=0.011, band=(300, 800)):
    """A tone's frequency from its upward zero crossings in `band` over t0-t1 s from the onset: the
    rim shot's low tone, whose strongest line in a window moves with the attack's shape (where
    the window of a hit 40 dB down in 12 ms starts)."""
    from scipy.signal import butter, sosfiltfilt
    y = sosfiltfilt(butter(4, band, 'bandpass', fs=sr, output='sos'), x)
    z = np.where((y[:-1] < 0) & (y[1:] >= 0))[0]
    zt = (z + (-y[z]) / (y[z + 1] - y[z])) / sr
    zt = zt[(zt >= t0) & (zt <= t1)]
    return float((len(zt) - 1) / (zt[-1] - zt[0])) if len(zt) > 2 else float('nan')


def centroid(x, sr):
    s = x[:int(0.06 * sr)]
    X = np.abs(np.fft.rfft(s * taper(len(s), decay_times(x, sr)[1] < 50))) ** 2
    f = np.fft.rfftfreq(len(s), 1 / sr)
    return float((X * f).sum() / (X.sum() + 1e-30))


def taper(n, onset):
    """A Hann window, or for a window from the onset of a short hit a flat start with a Hann
    fall over its last quarter: a full Hann nearly zeroes a hit that is 40 dB down in 12 ms (the
    rim shot) and weighs its ringing tail instead."""
    if not onset:
        return np.hanning(n)
    w = np.ones(n)
    k = n // 4
    w[n - k:] = 0.5 * (1 + np.cos(np.pi * np.arange(k) / k))
    return w


def bands6(x, sr, absolute=False, window=(0.03, 0.33)):
    s = x[int(window[0] * sr):int(window[1] * sr)]
    X = np.abs(np.fft.rfft(s * taper(len(s), window[0] == 0), 1 << 15)) ** 2
    f = np.fft.rfftfreq(1 << 15, 1 / sr)
    edges = 40 * 2 ** (np.arange(0, 49) / 6)
    edges = edges[edges < 18000]
    b = np.array([X[(f >= a) & (f < c)].sum() for a, c in zip(edges[:-1], edges[1:])]) + 1e-30
    return 10 * np.log10(b if absolute else b / b.sum())


# The recordings' floor: Fischer's hiss, about -83 dBFS rms, white.
_floor = {}
def floor_bands(sr, n, window):
    if (sr, n, window) not in _floor:
        rng = np.random.default_rng(1)
        _floor[(sr, n, window)] = bands6(rng.normal(0, 10 ** (-83 / 20), n), sr, True, window)
    return _floor[(sr, n, window)]


def lsd(a, b, sr):
    """Over the bands within 40 dB of the top that stand 10 dB clear of the recording's hiss, from
    30 to 330 ms, or over the first 80 ms for a hit that is 20 dB down within 50 ms (the closed
    hat, maracas, rim shot, claves), whose later part is mostly hiss."""
    window = (0.0, 0.08) if decay_times(a, sr)[1] < 50 else (0.03, 0.33)
    A, B = bands6(a, sr, False, window), bands6(b, sr, False, window)
    keep = (A > A.max() - 40) & (bands6(a, sr, True, window) > floor_bands(sr, len(a), window) + 10)
    return float(np.sqrt(np.mean((A - B)[keep] ** 2))) if keep.any() else float('nan')


def mark(v):
    return v['panel_mark_0_10']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--voices', default='')
    ap.add_argument('--accent', action='store_true')
    ap.add_argument('--cal', action='append', default=[])
    ap.add_argument('--set', action='append', default=[])
    ap.add_argument('--json')
    ap.add_argument('--voicerun', default=VOICERUN)
    ap.add_argument('--held-out', action='store_true')
    ap.add_argument('--fit-set', action='store_true')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    want = set(a.voices.upper().split(',')) if a.voices else set(VOICES)
    rows = []
    for rec in records():
        v = rec['instrument']
        if v not in want or v not in VOICES:
            continue
        marks = [mark(k) for name, k in rec['knobs'].items() if name != 'LEVEL']
        held = any(m in (2.5, 7.5) for m in marks)
        if (a.held_out and not held) or (a.fit_set and held):
            continue
        ref, sr = sf.read(os.path.join(REF, rec['path']))
        if ref.ndim > 1:
            ref = ref[:, 0]
        ref = ref[max(0, onset(ref) - 20):]
        seconds = min(len(ref) / sr, 3.0) + 0.1
        mod, _ = render(rec, a.accent, a.set, a.cal, a.voicerun, seconds)
        mod = mod[max(0, onset(mod) - 20):][:len(ref)]
        ref = ref[:len(mod)]
        row = {'file': os.path.basename(rec['path']), 'voice': v,
               'knobs': {k: mark(x) for k, x in rec['knobs'].items() if k != 'LEVEL'},
               'decay_ref': decay_times(ref, sr), 'decay_mod': decay_times(mod, sr),
               'centroid_ref': centroid(ref, sr), 'centroid_mod': centroid(mod, sr),
               'rate_ref': decay_rate(ref, sr), 'rate_mod': decay_rate(mod, sr),
               'lsd': lsd(ref, mod, sr)}
        if v == 'RS':
            row['pitch_ref'], row['pitch_mod'] = zero_cross_hz(ref, sr), zero_cross_hz(mod, sr)
        elif v in TUNED:
            band = PITCH_BAND.get(v, (30, 5000))
            row['pitch_ref'], row['pitch_mod'] = pitch(ref, sr, band), pitch(mod, sr, band)
        rows.append(row)
        if not a.quiet:
            knobs = ' '.join('%s%g' % (k[0], x) for k, x in row['knobs'].items())
            p = (' pitch %6.1f/%6.1f' % (row['pitch_ref'], row['pitch_mod'])) if 'pitch_ref' in row else ' ' * 19
            print('%-12s %-10s -20dB %6.0f/%6.0f ms -40dB %6.0f/%6.0f  cent %5.0f/%5.0f%s  lsd %5.2f' % (
                row['file'], knobs, row['decay_ref'][1], row['decay_mod'][1], row['decay_ref'][2], row['decay_mod'][2],
                row['centroid_ref'], row['centroid_mod'], p, row['lsd']))
    # summary per voice: median log ratio of the decays, pitch error in cents, centroid ratio, lsd
    summary = {}
    for v in sorted({r['voice'] for r in rows}):
        rs = [r for r in rows if r['voice'] == v]
        def med(f):
            vals = [f(r) for r in rs]
            vals = [x for x in vals if np.isfinite(x)]
            return float(np.median(vals)) if vals else float('nan')
        s = {'n': len(rs),
             'decay20': med(lambda r: np.log2(r['decay_mod'][1] / r['decay_ref'][1])),
             'decay40': med(lambda r: np.log2(r['decay_mod'][2] / r['decay_ref'][2])),
             'rate': med(lambda r: np.log2(r['rate_ref'] / r['rate_mod'])),
             'centroid': med(lambda r: np.log2(r['centroid_mod'] / r['centroid_ref'])),
             'lsd': med(lambda r: r['lsd'])}
        if any('pitch_ref' in r for r in rs):
            s['cents'] = med(lambda r: 1200 * np.log2(r['pitch_mod'] / r['pitch_ref']))
        summary[v] = s
    print('\nvoice   n  decay -20 dB  -40 dB   tail  centroid   pitch     lsd   (model/real, octaves; cents)')
    for v, s in summary.items():
        print('%-4s %4d   %+6.2f    %+6.2f  %+6.2f   %+6.2f   %s  %5.2f' % (v, s['n'], s['decay20'], s['decay40'], s['rate'], s['centroid'],
              ('%+6.0f' % s['cents']) if 'cents' in s else '      ', s['lsd']))
    if a.json:
        json.dump({'rows': rows, 'summary': summary}, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
