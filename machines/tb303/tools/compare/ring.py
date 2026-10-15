#!/usr/bin/env python3
"""Ring frequency after each saw reset (service-notes VCF adjustment): average period of the
decaying ring between resets, from zero crossings of the band-passed signal.
Usage: ring.py WAV [WAV ...]"""
import sys
import numpy as np, soundfile as sf
from scipy import signal

for path in sys.argv[1:]:
    x, sr = sf.read(path, always_2d=True); x = x.mean(1)
    seg = x[int(0.3 * sr):int(0.9 * sr)]
    # The ring dominates above the first few harmonics: high-pass at 4x the note (262 Hz).
    b, a = signal.butter(4, 200 / (sr / 2), 'high')
    y = signal.filtfilt(b, a, seg)
    # dominant frequency of the ring: spectral peak of the high-passed signal, refined with
    # the envelope of zero-crossing intervals
    n = 1 << 20
    sp = np.abs(np.fft.rfft(y * np.hanning(len(y)), n)); fr = np.fft.rfftfreq(n, 1 / sr)
    k = np.argmax(sp)
    zc = np.where(np.diff(np.signbit(y)))[0]
    periods = np.diff(zc[::2]) / sr
    med = np.median(periods[(periods > 1 / 5000) & (periods < 1 / 150)]) if len(periods) else float('nan')
    print(f"{path.split('/')[-1]:40s} spectral peak {fr[k]:7.1f} Hz   zero-crossing period {med*1000:6.3f} ms ({1/med:6.1f} Hz)")
