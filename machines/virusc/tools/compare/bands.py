"""Spectral shape in 1/3-octave bands, each signal normalised to its own level over 100 Hz-8 kHz.
Usage: bands.py ref.wav a b  out.wav a b  [label]   (a, b: seconds)"""
import sys, wave, numpy as np


def load(p, a, b):
    w = wave.open(p)
    sr, ch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
    raw = w.readframes(w.getnframes())
    if sw == 2:
        d = np.frombuffer(raw, dtype=np.int16).astype(float) / 32768
    else:
        d = np.frombuffer(raw, dtype=np.float32).astype(float)
    d = d.reshape(-1, ch).mean(1)
    return d[int(a * sr):int(b * sr)], sr


def wav_float(p):
    # viruscrun writes float WAV (format 3), which the wave module refuses: read the data chunk
    b = open(p, 'rb').read()
    if b[20:22] == b'\x03\x00':
        i = b.find(b'data')
        n = int.from_bytes(b[i + 4:i + 8], 'little')
        ch = int.from_bytes(b[22:24], 'little')
        sr = int.from_bytes(b[24:28], 'little')
        return np.frombuffer(b[i + 8:i + 8 + n], dtype=np.float32).astype(float).reshape(-1, ch).mean(1), sr
    return None


def bands(d, sr):
    n = 16384
    acc = np.zeros(n // 2 + 1)
    for i in range(0, len(d) - n, n // 4):
        acc += np.abs(np.fft.rfft(d[i:i + n] * np.hanning(n))) ** 2
    f = np.fft.rfftfreq(n, 1 / sr)
    centres = 1000 * 2 ** (np.arange(-10, 14) / 3)  # 100 Hz .. 20 kHz
    out = []
    for c in centres:
        lo, hi = c * 2 ** (-1 / 6), c * 2 ** (1 / 6)
        out.append(acc[(f >= lo) & (f < hi)].sum())
    out = np.array(out)
    ref = out[(centres >= 100) & (centres <= 8000)].sum()
    return centres, 10 * np.log10(out / ref + 1e-20)


def get(p, a, b):
    r = wav_float(p)
    if r is not None:
        d, sr = r
        return d[int(a * sr):int(b * sr)], sr
    return load(p, a, b)


ref, rsr = get(sys.argv[1], float(sys.argv[2]), float(sys.argv[3]))
out, osr = get(sys.argv[4], float(sys.argv[5]), float(sys.argv[6]))
c, rb = bands(ref, rsr)
_, ob = bands(out, osr)
label = sys.argv[7] if len(sys.argv) > 7 else ''
print(f'{label}: band   real   ours   diff (dB, each relative to its own 100 Hz-8 kHz level)')
diffs = []
for k in range(len(c)):
    if c[k] > 12500:
        break
    d = ob[k] - rb[k]
    if c[k] <= 8000:
        diffs.append(d)
    print(f'  {c[k]:7.0f} {rb[k]:6.1f} {ob[k]:6.1f} {d:+6.1f} ' + ('+' * int(min(20, max(0, d))) if d > 0 else '-' * int(min(20, -d))))
diffs = np.array(diffs)
print(f'  100 Hz-8 kHz: mean |diff| {np.abs(diffs).mean():.1f} dB, tilt {np.polyfit(np.log2(c[c <= 8000][:len(diffs)]), diffs, 1)[0]:+.2f} dB/octave')
