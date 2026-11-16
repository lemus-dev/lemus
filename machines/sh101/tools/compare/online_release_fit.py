#!/usr/bin/env python3
"""Tracks a self-oscillating filter's frequency through an ENV release and fits
log2 f = b + D exp(-t/tau): the rest point (2^b), the depth in octaves at the release's start (D)
and the release time constant. For docs/online-references.md, section 1.1 (Friendly Noise video,
saw with RES 10: note-off at about 103.6 s, release to the video's cut at about 113.4 s).

Usage: online_release_fit.py WAV T_NOTEOFF T_END [--saw HZ]
Run with the venv's python and -I.
"""
import sys

import numpy as np
import soundfile as sf
from scipy import optimize, signal


def track(x, sr, a, b, saw):
    y = x[int(a * sr):int(b * sr)]
    f, t, S = signal.spectrogram(y, sr, nperseg=16384, noverlap=16384 - 1200)
    out = []
    for i in range(len(t)):
        L = 10 * np.log10(S[:, i] + 1e-20)
        m = (f > 30) & (f < 9000)
        pk, _ = signal.find_peaks(L[m], prominence=6)
        fr, lv = f[m][pk], L[m][pk]
        keep = [k for k in range(len(fr)) if fr[k] < 0.8 * saw or abs(fr[k] / saw - round(fr[k] / saw)) * saw > 0.03 * fr[k]]
        if keep:
            k = max(keep, key=lambda k: lv[k])
            out.append((a + t[i], fr[k], lv[k]))
    return np.array(out)


def main():
    path, t_off, t_end = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
    saw = float(sys.argv[sys.argv.index("--saw") + 1]) if "--saw" in sys.argv else 261.5
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(1)
    tr = track(x, sr, t_off - 0.2, t_end, saw)
    sel = (tr[:, 0] >= t_off + 0.2) & (tr[:, 2] > tr[:, 2].max() - 15)
    tt, y = tr[sel, 0] - t_off, np.log2(tr[sel, 1])
    model = lambda p, t: p[0] + p[1] * np.exp(-t / p[2])
    p, _ = optimize.leastsq(lambda p: model(p, tt) - y, [5.0, 6.0, 4.0])
    rms = np.sqrt(np.mean((model(p, tt) - y) ** 2))
    print("points %d  rest %.1f Hz  depth %.2f oct  tau %.2f s  rms %.3f oct  (start %.0f Hz, last %.1f Hz)"
          % (len(tt), 2 ** p[0], p[1], p[2], rms, 2 ** (p[0] + p[1]), tr[sel, 1][-1]))


if __name__ == "__main__":
    main()
