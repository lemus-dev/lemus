#!/usr/bin/env python3
"""Resonant peak of the static-filter tests: harmonic amplitudes of the held A-2 (110 Hz)
note, weighted by n to undo the saw's 1/n, and the frequency/height of the maximum.
Usage: peaks.py DIR [DIR ...]"""
import sys, os
import numpy as np, soundfile as sf

def harmonics(path, f0=110.0, t0=0.3, t1=0.9, nh=60):
    x, sr = sf.read(path, always_2d=True); x = x.mean(1)
    seg = x[int(t0 * sr):int(t1 * sr)]
    seg = seg * np.hanning(len(seg))
    n = 1 << 20
    sp = np.abs(np.fft.rfft(seg, n)); fr = np.fft.rfftfreq(n, 1 / sr)
    # refine f0 from the strongest of the first harmonics
    amps = []
    for k in range(1, nh + 1):
        band = (fr > f0 * k - 20) & (fr < f0 * k + 20)
        amps.append(sp[band].max() if band.any() else 0)
    return np.array(amps)

for d in sys.argv[1:]:
    print(d)
    for c in ("000", "025", "050", "075", "100"):
        row = []
        for r in ("000", "050", "100"):
            p = os.path.join(d, f"static-saw-c{c}-r{r}.wav")
            if not os.path.exists(p):
                continue
            a = harmonics(p)
            w = a * np.arange(1, len(a) + 1)
            k = int(np.argmax(w[1:])) + 1
            pk = 20 * np.log10(w[k] / w[0])
            row.append(f"r{r}: peak {110*(k+1):5.0f} Hz {pk:+5.1f} dB, h1 {20*np.log10(a[0]+1e-12):6.1f}")
        print(f"  c{c}  " + " | ".join(row))
