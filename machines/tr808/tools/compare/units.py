#!/usr/bin/env python3
"""The knobless voices (RS, CL, CP, MA, CB, CH) on every real unit we have, against the model: how far
the units and one unit's takes spread on each measure, and where the model falls. A model gap inside
the units' spread is the units differing, not the model.

  F  Fischer's unit (fischer/, one hit per voice, individual outputs, 16/44.1)
  R  Raum 45's free set (raum45_free/, five takes per voice at one setting, 24/88.2)
  A  the archive.org unit ("808 accent levels.flac", mix out, 24/96): its plain hits, and its
     accented ones against the model's accented hit
  V  Reverb's pack (reverb_audiocom/, an early unit through a tube preamp, 16/44.1): two plain
     takes a voice (and two accented, its ACCENT about 0.75)

Measures (from the onset, as fischer.py's):
  att2   the peak envelope (|x|'s maximum over 1.25 ms) 2 ms after the onset, over its maximum
  t90    ms from the onset until that envelope first reaches 0.9 of its maximum
  d10 d20 d40   ms to -10/-20/-40 dB of the 2 ms RMS envelope's peak
  pitch  the strongest line (RS: in 300-700 Hz), 2-40 ms for a short hit
  zc     RS: its low tone's frequency from the zero crossings of 300-800 Hz over 3-11 ms (the
         strongest line moves with the attack's shape: where a 10 ms hit's window starts)
  t2 t3  CP: when the sawtooth's second and third teeth start, ms (the envelope rising 6 dB
         within 1.5 ms)
  cent   spectral centroid of the first 60 ms (flat from the onset for a short hit)
  rs17   RS: its 455 Hz line against its 1.7 kHz line, dB, over the first 80 ms
  hi     CP, MA, CH: the 2-8 kHz band against 0.4-2 kHz over the first 80 ms, dB

The model is hit 8 times in one run, 0.7 s apart: the metal voices' squares run free, so each hit
meets them at another phase (the cowbell's attack is that phase), and the noise voices get another
stretch of noise. Its column is the mean, with the range over the 8 hits.

  units.py [--voices CB,RS,...] [--voicerun PATH] [--cal NAME=VALUE ...] [--set NAME=VALUE ...] [--json OUT]
"""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fischer as fz

REF = fz.REF
ARCHIVE = os.path.join(REF, 'archiveorg_tr-808-samples', '808 accent levels.flac')
RAUM = os.path.join(REF, 'raum45_free', 'TR-808 Clean 24-88', 'TR-808 Clean')
REVERB = os.path.join(REF, 'reverb_audiocom', 'wav', 'Reverb Roland TR-808 Sample Pack_')
REVERB_NAMES = {'RS': ('Rimshot', 'Rimshot2'), 'CL': ('Clava', 'Clava2'), 'CP': ('Clap', 'Clap2'),
                'MA': ('Maraca', 'Maraca2'), 'CB': ('Cowbell', 'Cowbell2'), 'CH': ()}

# The archive.org unit's takes, (accented, plain) onsets in seconds (index.json's detected hits,
# identified by spectrum and decay; accent.py has the drums').
ARCHIVE_TAKES = {
    'RS': [(102.299, 103.631), (104.965, 106.297)],
    'CL': [(107.629, 108.963), (110.297, 111.629)],
    'CP': [(120.962, 122.294), (123.628, 124.960)],
    'CB': [(142.292, 143.625), (144.957, 146.291)],
    'MA': [(152.956, 154.292), (155.624, 156.956)],
    'CH': [(198.057, 200.169), (202.278, 204.388)],
}
VOICES = ['RS', 'CL', 'CP', 'MA', 'CB', 'CH']
MEASURES = ['att2', 't90', 'd10', 'd20', 'd40', 'pitch', 'zc', 'cent', 'rs17', 'hi', 't2', 't3']


def peak_env(x, sr, width=0.00125):
    w = max(1, int(width * sr))
    a = np.abs(x)
    pad = np.pad(a, (w // 2, w - w // 2), mode='edge')
    from numpy.lib.stride_tricks import sliding_window_view
    return sliding_window_view(pad, w + 1).max(axis=1)[:len(a)]


def line_db(x, sr, lo, hi, window=0.08):
    s = x[:int(window * sr)]
    n = 1 << 16
    X = np.abs(np.fft.rfft(s * fz.taper(len(s), True), n))
    f = np.fft.rfftfreq(n, 1 / sr)
    return 20 * np.log10(X[(f >= lo) & (f <= hi)].max() + 1e-30)


def band_db(x, sr, lo, hi, window=0.08):
    s = x[:int(window * sr)]
    n = 1 << 16
    X = np.abs(np.fft.rfft(s * fz.taper(len(s), True), n)) ** 2
    f = np.fft.rfftfreq(n, 1 / sr)
    return 10 * np.log10(X[(f >= lo) & (f < hi)].sum() + 1e-30)


def teeth(x, sr, n=3):
    """The clap's teeth after the first: where its envelope (0.5 ms RMS, smoothed over 1 ms)
    rises 6 dB within 1.5 ms, from 4 ms on, ms."""
    w = int(0.0005 * sr)
    e = np.sqrt(np.convolve(x ** 2, np.ones(w) / w, 'same')) + 1e-9
    k = int(0.001 * sr)
    es = np.convolve(e, np.ones(k) / k, 'same')
    out = []
    i = int(0.004 * sr)
    while i < int(0.045 * sr) and len(out) < n:
        if 20 * np.log10(es[i + int(0.0015 * sr)] / es[i]) > 6:
            out.append(i / sr * 1000)
            i += int(0.006 * sr)
        else:
            i += max(1, int(0.0002 * sr))
    return out + [float('nan')] * (n - len(out))


def measure(v, x, sr):
    """x starts at the hit's onset."""
    e = peak_env(x, sr)
    m = e.max()
    d = fz.decay_times(x, sr)
    e10 = fz.envelope(x, sr)[0]
    p = int(np.argmax(e10))
    after = np.where(e10[p:] < -10)[0]
    out = {
        'att2': float(e[int(0.002 * sr)] / m),
        't90': float(np.argmax(e >= 0.9 * m) / sr * 1000),
        'd10': (after[0] * 2.0) if len(after) else float('nan'),
        'd20': d[1], 'd40': d[2],
        'cent': fz.centroid(x, sr),
    }
    if v in ('RS', 'CL', 'CB'):
        out['pitch'] = fz.pitch(x, sr, fz.PITCH_BAND.get(v, (30, 5000)))
    if v == 'RS':
        out['zc'] = fz.zero_cross_hz(x, sr)
        out['rs17'] = line_db(x, sr, 400, 520) - line_db(x, sr, 1550, 1850)
    if v == 'CP':
        out['t2'], out['t3'] = teeth(x, sr, 2)
    if v in ('CP', 'MA', 'CH'):
        out['hi'] = band_db(x, sr, 2000, 8000) - band_db(x, sr, 400, 2000)
    return out


def from_onset(x, sr, pre_s=0.0002):
    o = fz.onset(x)
    return x[max(0, o - int(pre_s * sr)):]


def fischer_take(v):
    rec = next(r for r in fz.records() if r['instrument'] == v)
    x, sr = sf.read(os.path.join(REF, rec['path']))
    return from_onset(x if x.ndim == 1 else x[:, 0], sr), sr


def raum_takes(v):
    out = []
    for k in range(1, 6):
        x, sr = sf.read(os.path.join(RAUM, '%s-808A%d.wav' % (v, k)))
        out.append((from_onset(x if x.ndim == 1 else x[:, 0], sr), sr))
    return out


def reverb_takes(v, accented):
    out = []
    for n in REVERB_NAMES[v]:
        name = (n.replace('2', '') + ' Accent' + ('2' if n.endswith('2') else '')) if accented else n
        if v == 'RS' and accented:
            name = n + ' Accent'
        x, sr = sf.read(REVERB + name + '.wav')
        out.append((from_onset(x if x.ndim == 1 else x[:, 0], sr), sr))
    return out


_archive = None
def archive_takes(v, accented):
    global _archive
    if _archive is None:
        _archive = sf.read(ARCHIVE)
    x, sr = _archive
    out = []
    for pair in ARCHIVE_TAKES[v]:
        t = pair[0 if accented else 1]
        seg = x[int((t - 0.02) * sr):int((t + 0.6) * sr)]
        out.append((from_onset(seg, sr), sr))
    return out


def model_takes(v, accent, voicerun, sets, cals, rate=96000, hits=8, gap=0.7):
    """The voice hit `hits` times, `gap` s apart, from its jack: each hit from its onset."""
    voice, switch, _ = fz.VOICES[v]
    times = [0.05 + k * gap for k in range(hits)]
    with tempfile.TemporaryDirectory() as tmp:
        jacks = os.path.join(tmp, 'j.wav')
        args = [voicerun, os.path.join(tmp, 'm.wav'), '--rate', str(rate), '--seconds', '%g' % (times[-1] + gap),
                '--jacks', jacks]
        for t in times:
            args += ['--hit', '%.4f:%s%s' % (t, voice, '+ac' if accent else '')]
        for s in sets + ([switch + '=1'] if switch else []):
            args += ['--set', s]
        for c in cals:
            args += ['--cal', c]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode:
            sys.exit('voicerun failed: ' + r.stderr)
        x, sr = sf.read(jacks)
    y = x[:, fz.JACK.index(voice)]
    return [(from_onset(y[int((t - 0.001) * sr):int((t + gap - 0.01) * sr)], sr), sr) for t in times]


def fmt(k, val):
    if val is None or not np.isfinite(val):
        return '     -'
    if k in ('att2',):
        return '%6.2f' % val
    if k in ('rs17', 'hi'):
        return '%+6.1f' % val
    return '%6.0f' % val if val >= 100 else '%6.1f' % val


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--voices', default=','.join(VOICES))
    ap.add_argument('--voicerun', default=fz.VOICERUN)
    ap.add_argument('--cal', action='append', default=[])
    ap.add_argument('--set', action='append', default=[])
    ap.add_argument('--json')
    a = ap.parse_args()
    result = {}
    for v in a.voices.upper().split(','):
        cols = {}
        cols['F'] = [measure(v, *fischer_take(v))]
        cols['R'] = [measure(v, *t) for t in raum_takes(v)]
        cols['A'] = [measure(v, *t) for t in archive_takes(v, False)]
        cols['A+'] = [measure(v, *t) for t in archive_takes(v, True)]
        cols['V'] = [measure(v, *t) for t in reverb_takes(v, False)]
        cols['M'] = [measure(v, *t) for t in model_takes(v, False, a.voicerun, a.set, a.cal)]
        cols['M+'] = [measure(v, *t) for t in model_takes(v, True, a.voicerun, a.set, a.cal)]
        keys = [k for k in MEASURES if k in cols['M'][0]]
        print('\n%s          F   R mean ±sd     A      A+      V  |    M (its range)       M+   units min..max' % v)
        res = {}
        for k in keys:
            def vals(c):
                return [t[k] for t in cols[c] if np.isfinite(t.get(k, np.nan))]
            f, r, ar, aa, vv, m, ma = (vals(c) for c in ('F', 'R', 'A', 'A+', 'V', 'M', 'M+'))
            units = [np.mean(u) for u in (f, r, ar, vv) if u]
            row = {'F': f, 'R': r, 'A': ar, 'A+': aa, 'V': vv, 'M': m, 'M+': ma,
                   'units': [float(min(units)), float(max(units))] if units else None}
            res[k] = row
            rsd = (' ±%-5s' % ('%.2f' % np.std(r) if k == 'att2' else '%.1f' % np.std(r))) if len(r) > 1 else '       '
            # inside: the model's hits and the units' means overlap
            inside = units and m and min(m) <= row['units'][1] and max(m) >= row['units'][0]
            print('  %-6s %s %s%s %s %s %s  | %s (%s..%s) %s   %s..%s%s' % (
                k, fmt(k, np.mean(f) if f else np.nan), fmt(k, np.mean(r) if r else np.nan), rsd,
                fmt(k, np.mean(ar) if ar else np.nan), fmt(k, np.mean(aa) if aa else np.nan),
                fmt(k, np.mean(vv) if vv else np.nan),
                fmt(k, np.mean(m) if m else np.nan), fmt(k, min(m) if m else np.nan).strip(),
                fmt(k, max(m) if m else np.nan).strip(), fmt(k, np.mean(ma) if ma else np.nan),
                fmt(k, row['units'][0]).strip() if units else '-', fmt(k, row['units'][1]).strip() if units else '-',
                '' if not units or not m else ('   inside' if inside else '   OUTSIDE')))
        result[v] = res
    if a.json:
        json.dump(result, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
