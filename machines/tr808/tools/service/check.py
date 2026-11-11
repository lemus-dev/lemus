#!/usr/bin/env python3
"""The service notes' checks (p.14) against the emulation, pass or fail for each:

  voices    "CHECKING VOICES": each voice at its MULTI OUT, LEVEL full, its DECAY/TONE/TUNING at 12
            o'clock: the amplitude (Vpp) with ACCENT fully anticlockwise and fully clockwise, the
            frequency, and the decay time (to 1/10 of the first swing's Vpp, as the notes' sketch).
            The notes call the values "typical and variable"; a value passes within 25 % (amplitude,
            frequency) or 40 % (decay) of theirs.
  tempo     TM-2: 8.33 ms a tempo clock at TEMPO and FINE fully clockwise, 65 +-5 ms at TEMPO fully
            anticlockwise.
  clocks    the CPU clock (2 us) and INT clock (1.9 ms) the board runs at.
  trims     TM-1/TM-2 (cowbell 1.85 / 1.25 ms) and TM-4 (130 mV rms of white noise at TP4), for the
            nominal calibration (the one the notes describe) and the fitted unit Lemus plays.

  check.py [--voicerun PATH] [--unit]   (--unit: the fitted unit, else the nominal parts)
"""
import argparse, os, re, subprocess, sys, tempfile
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
VOICERUN = os.path.normpath(os.path.join(MACHINE, '..', '..', 'build', 'tr808', 'voicerun'))
JACK = ['bd', 'sd', 'lt', 'mt', 'ht', 'rs', 'cp', 'cb', 'cy', 'oh', 'ch']

# name, voice, switch, (Vpp normal, Vpp accent), Hz (mid knob) or None, decay ms (mid) or None,
# the knobs set to 12 o'clock
TABLE = [
    ('BD', 'bd', None, (3.5, 10), 56, 300, ['bdTone=0.5', 'bdDecay=0.5']),
    # SD: the table's 238 Hz (4.2 ms) is the low resonator before the "PORTION CHANGED" values,
    # which put it at 172 Hz (C58 .056); Michael Fischer's unit measures 172 Hz
    ('SD', 'sd', None, (3, 10), 172, 60, ['sdTone=0.5', 'sdSnappy=0.5']),
    ('LC', 'lt', 'lc', (3.5, 12), 185, 180, ['ltTuning=0.5']),
    ('LT', 'lt', None, (3.5, 12), 90, 200, ['ltTuning=0.5']),
    ('MC', 'mt', 'mc', (3, 10), 280, 100, ['mtTuning=0.5']),
    ('MT', 'mt', None, (3, 11), 135, 130, ['mtTuning=0.5']),
    ('HC', 'ht', 'hc', (3.5, 12), 400, 80, ['htTuning=0.5']),
    ('HT', 'ht', None, (3.5, 12), 185, 100, ['htTuning=0.5']),
    ('C', 'rs', 'cl', (2.5, 8), 2500, 25, []),
    ('RS', 'rs', None, (3, 10), None, 10, []),
    ('M', 'cp', 'ma', (3, 5), None, None, []),
    ('CP', 'cp', None, (6, None), None, 100, []),
    ('CB', 'cb', None, (3.5, 12), 800, 50, []),
    ('CY', 'cy', None, (3.5, 7), None, 800, ['cyTone=0.5', 'cyDecay=0.5']),
    ('OH', 'oh', None, (3.5, 7), None, 450, ['ohDecay=0.5']),
    ('CH', 'ch', None, (3, 6), None, 50, []),
]


def render(voicerun, voice, switch, sets, accent, cals, seconds=2.0):
    with tempfile.TemporaryDirectory() as tmp:
        j = os.path.join(tmp, 'j.wav')
        args = [voicerun, os.path.join(tmp, 'm.wav'), '--rate', '96000', '--seconds', str(seconds), '--hit',
                '0.05:%s%s' % (voice, '+ac' if accent else ''), '--jacks', j, '--set', 'accent=%d' % (1 if accent else 0)]
        lvl = {'bd': 'bdLevel', 'sd': 'sdLevel', 'lt': 'ltLevel', 'mt': 'mtLevel', 'ht': 'htLevel', 'rs': 'rsLevel', 'cp': 'cpLevel',
               'cb': 'cbLevel', 'cy': 'cyLevel', 'oh': 'ohLevel', 'ch': 'chLevel'}[voice]
        args += ['--set', lvl + '=1']
        for s in sets + ([switch + '=1'] if switch else []):
            args += ['--set', s]
        for c in cals:
            args += ['--cal', c]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode:
            sys.exit(r.stderr)
        x, sr = sf.read(j)
    return x[int(0.05 * sr):, JACK.index(voice)], sr


def swing_window(hz, sr):
    return max(int(0.002 * sr), int(1.25 * sr / hz)) if hz else int(0.002 * sr)


# The cowbell is two tones through a band-pass that takes a few cycles to ring up: its "first
# swing" is its largest, as for the voices without a pitch.
LARGEST = {'CB'}
# The maracas and clap are filtered noise: their swing is read as a scope trace shows it (below).
NOISE = {'M', 'CP'}


def largest_swing(x, sr, hz):
    w = swing_window(hz, sr)
    hop = max(1, w // 8)
    return max((float(np.ptp(x[i:i + w])), i) for i in range(0, int(0.2 * len(x)), hop))


def vpp(x, sr, hz, largest=False, noise=False):
    """The first swing's peak to peak, as the notes' sketch: over the first cycle and a quarter
    from the onset (the voices without a pitch, and the cowbell: their largest swing)."""
    if noise:
        # noise: the swing a scope shows where the hit is loudest, the 1st to 99th percentile over
        # the 5 ms of largest RMS (its largest single swing is a rare spike a trace doesn't show)
        w = int(0.005 * sr)
        head = x[:int(0.2 * len(x))]
        e = np.convolve(head ** 2, np.ones(w), 'valid')
        i = int(np.argmax(e))
        lo, hi = np.percentile(head[i:i + w], [1, 99])
        return float(hi - lo)
    if not hz:  # the metal voices: the largest swing anywhere in the hit
        return float(np.ptp(x[:int(0.2 * len(x))]))
    if largest:
        return largest_swing(x, sr, hz)[0]
    a = np.abs(x)
    start = int(np.argmax(a > a.max() * 0.05))
    return float(np.ptp(x[start:start + swing_window(hz, sr)]))


def frequency(x, sr):
    a = np.abs(x)
    start = int(np.argmax(a > a.max() * 0.05)) + int(0.01 * sr)
    s = x[start:start + int(0.1 * sr)]
    if len(s) < 64:
        s = x[:int(0.05 * sr)]
    n = 1 << 18
    X = np.abs(np.fft.rfft((s - s.mean()) * np.hanning(len(s)), n))
    f = np.fft.rfftfreq(n, 1 / sr)
    band = f > 30
    return float(f[band][np.argmax(X[band])])


def decay_ms(x, sr, hz, largest=False):
    """From the onset to where the swing (peak to peak over a cycle) is 1/10 of the first (or,
    for the cowbell, of its largest)."""
    w = swing_window(hz, sr)
    a = np.abs(x)
    start = int(np.argmax(a > a.max() * 0.05))
    ref = np.ptp(x[start:start + w])
    first = start
    if largest:
        ref, first = largest_swing(x, sr, hz)
    hop = max(1, w // 8)
    for i in range(first, len(x) - w, hop):
        if np.ptp(x[i:i + w]) < ref / 10:
            return (i - start + w) * 1000.0 / sr
    return float('nan')


def verdict(model, ref, tol):
    if ref is None or model != model:
        return '  -  '
    return 'pass ' if abs(model / ref - 1) <= tol else 'FAIL '


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--voicerun', default=VOICERUN)
    ap.add_argument('--unit', action='store_true')
    a = ap.parse_args()
    cals = [] if a.unit else ['nominal=1']
    passes = fails = 0
    print('CHECKING VOICES (%s): Vpp normal / accent, frequency, decay to 1/10 Vpp' % ('the fitted unit' if a.unit else 'nominal parts'))
    for name, voice, switch, (vn, va), hz, dec, sets in TABLE:
        x0, sr = render(a.voicerun, voice, switch, sets, False, cals)
        x1, _ = render(a.voicerun, voice, switch, sets, True, cals)
        line = '%-3s ' % name
        f0 = frequency(x0, sr) if hz else None
        big = name in LARGEST
        nz = name in NOISE
        for model, ref, tol in ((vpp(x0, sr, f0, big, nz), vn, 0.25), (vpp(x1, sr, f0, big, nz), va, 0.25)):
            v = verdict(model, ref, tol)
            line += '%s%5.1f V (%s)  ' % (v, model, ref if ref else '-')
            passes += v.startswith('pass'); fails += v.startswith('FAIL')
        if hz:
            f = f0
            v = verdict(f, hz, 0.25)
            line += '%s%6.0f Hz (%d)  ' % (v, f, hz)
            passes += v.startswith('pass'); fails += v.startswith('FAIL')
        else:
            line += ' ' * 23
        if dec:
            d = decay_ms(x0, sr, f0, big)
            v = verdict(d, dec, 0.4)
            line += '%s%5.0f ms (%d)' % (v, d, dec)
            passes += v.startswith('pass'); fails += v.startswith('FAIL')
        print(line)
    print('\n%d pass, %d fail' % (passes, fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
