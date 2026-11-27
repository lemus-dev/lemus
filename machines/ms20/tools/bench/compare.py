"""Two sets of ms20bench renders, per note: pitch (cents) and level (dB).

    python tools/bench/compare.py OLD_DIR NEW_DIR

For each workload, the samples are cut at the bench's key events (tools/bench/main.cpp's
scripts, repeated here); in each note's middle half the fundamental is read from the
autocorrelation (parabolic peak) and the level as RMS. Reports the largest difference over the
notes, and the largest sample difference in dB re full scale for scale.
"""
import sys
import wave

import numpy as np


def read(path):
    with open(path, 'rb') as f:
        d = f.read()
    return np.frombuffer(d[44:], dtype=np.float32).astype(np.float64)


def melody(start, step, n, notes, length=0.8):
    return [(start + i * step, start + i * step + step * length) for i in range(n)]


# (rate, note spans in seconds) per workload, as tools/bench/main.cpp plays them
CASES = {
    'note-48k': (48000, [(0.1, 2.0)]),
    'bass-48k': (48000, melody(0.05, 0.25, 15, [0] * 8)),
    'scream-48k': (48000, melody(0.1, 1.0, 3, [0] * 3)),
    'everything-44k': (44100, melody(0.1, 0.5, 9, [0] * 9)),
    'selfosc-48k': (48000, [(0.1, 2.5)]),
}
# the same with the Rev 2 filters
for _name in ('scream-48k', 'everything-44k', 'selfosc-48k'):
    _base, _rate = _name.rsplit('-', 1)
    CASES[f'{_base}-rev2-{_rate}'] = CASES[_name]


def f0(x, rate):
    x = x - x.mean()
    n = len(x)
    spec = np.fft.rfft(x, 2 * n)
    ac = np.fft.irfft(spec * np.conj(spec))[:n]
    lo, hi = int(rate / 4000), int(rate / 20)
    k = lo + int(np.argmax(ac[lo:hi]))
    # the first peak within 90 % of the largest (not a multiple of the period)
    for j in range(lo, hi - 1):
        if ac[j] > 0.9 * ac[k] and ac[j] >= ac[j - 1] and ac[j] >= ac[j + 1]:
            k = j
            break
    a, b, c = ac[k - 1], ac[k], ac[k + 1]
    p = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0
    return rate / (k + p)


def main(old_dir, new_dir):
    for name, (rate, spans) in CASES.items():
        a, b = read(f'{old_dir}/{name}.wav'), read(f'{new_dir}/{name}.wav')
        worst = np.max(np.abs(a - b)) if len(a) == len(b) else float('nan')
        dc, dl = 0.0, 0.0
        for on, off in spans:
            i0 = int((on + 0.25 * (off - on)) * rate)
            i1 = int((on + 0.75 * (off - on)) * rate)
            xa, xb = a[i0:i1], b[i0:i1]
            ra, rb = np.sqrt(np.mean(xa ** 2)), np.sqrt(np.mean(xb ** 2))
            if ra < 1e-5:
                continue
            dl = max(dl, abs(20 * np.log10(rb / ra)))
            fa, fb = f0(xa, rate), f0(xb, rate)
            dc = max(dc, abs(1200 * np.log2(fb / fa)))
        print(f'{name:16s} pitch {dc:7.4f} cents  level {dl:7.4f} dB  (largest sample difference {20 * np.log10(worst + 1e-30):6.1f} dB)')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
