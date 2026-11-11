#!/usr/bin/env python3
"""look.py FILE.WAV [--cal ...] [--set ...] [--accent] [--out PNG]: one Fischer recording against the
model at its settings: waveforms (first 60 ms), envelopes (dB) and spectra (first 300 ms)."""
import argparse, os, sys
import numpy as np, soundfile as sf
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fischer as F

ap = argparse.ArgumentParser()
ap.add_argument('file')
ap.add_argument('--cal', action='append', default=[])
ap.add_argument('--set', action='append', default=[])
ap.add_argument('--accent', action='store_true')
ap.add_argument('--out', default='look.png')
a = ap.parse_args()
rec = next(r for r in F.records() if os.path.basename(r['path']).upper() == a.file.upper())
ref, sr = sf.read(os.path.join(F.REF, rec['path']))
ref = ref[max(0, F.onset(ref) - 20):]
mod, _ = F.render(rec, a.accent, a.set, a.cal, F.VOICERUN, min(len(ref) / sr, 3.0) + 0.1)
mod = mod[max(0, F.onset(mod) - 20):][:len(ref)]
ref = ref[:len(mod)]
# levels: match the peaks for the waveform view
g = np.abs(ref).max() / (np.abs(mod).max() + 1e-12)
fig, ax = plt.subplots(3, 1, figsize=(12, 11))
t = np.arange(len(ref)) / sr * 1000
n = int(0.06 * sr)
ax[0].plot(t[:n], ref[:n], label='real'); ax[0].plot(t[:n], mod[:n] * g, label='model x%.2f' % g, alpha=0.75)
ax[0].legend(); ax[0].set_xlabel('ms')
for x, lab in ((ref, 'real'), (mod, 'model')):
    e, dt = F.envelope(x, sr)
    ax[1].plot(np.arange(len(e)) * dt * 1000, e, label=lab)
ax[1].set_ylim(-80, 3); ax[1].legend(); ax[1].set_xlabel('ms'); ax[1].grid(alpha=.3)
for x, lab in ((ref, 'real'), (mod * g, 'model')):
    s = x[:int(0.3 * sr)]
    X = np.abs(np.fft.rfft(s * np.hanning(len(s)), 1 << 16))
    f = np.fft.rfftfreq(1 << 16, 1 / sr)
    k = np.ones(31) / 31
    ax[2].semilogx(f[1:], 20 * np.log10(np.convolve(X, k, 'same')[1:] + 1e-9), label=lab)
ax[2].set_xlim(20, 20000); ax[2].legend(); ax[2].grid(alpha=.3, which='both')
ax[0].set_title(rec['path'] + '  ' + ' '.join(a.cal + a.set))
plt.tight_layout(); plt.savefig(a.out, dpi=70)
