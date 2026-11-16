#!/usr/bin/env python3
"""Per-note analysis of a real-unit recording with the filter self-oscillating over a pitched
source: for each held note, the source's fundamental and the self-oscillation peak (the strongest
spectral peak that is not a harmonic of the source). Used for docs/online-references.md.

Usage: online_notes.py WAV START END [--sub N] [--minlen S]
  --sub N   the source is the sub-oscillator N octaves below the VCO (reported VCO = 2^N x f0)
Run with the venv's python and -I (the recordings are downloads).
"""
import sys
import numpy as np
import soundfile as sf
from scipy import signal


def main():
    path, a, b = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
    sub = int(sys.argv[sys.argv.index("--sub") + 1]) if "--sub" in sys.argv else 0
    minlen = float(sys.argv[sys.argv.index("--minlen") + 1]) if "--minlen" in sys.argv else 0.12
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(1)
    y = x[int(a * sr):int(b * sr)]
    hop = int(0.005 * sr)
    env = np.array([np.sqrt(np.mean(y[i:i + hop] ** 2)) for i in range(0, len(y) - hop, hop)])
    db = 20 * np.log10(env + 1e-9)
    # onsets: rises of 6 dB over 15 ms
    on = [i for i in range(3, len(db)) if db[i] - db[i - 3] > 6 and db[i] > db.max() - 40]
    notes = []
    for i in on:
        if notes and i - notes[-1] < int(minlen / 0.005):
            continue
        notes.append(i)
    notes.append(len(db))
    for k in range(len(notes) - 1):
        s0 = (notes[k] + 8) * hop          # skip 40 ms of attack
        s1 = min(notes[k + 1] * hop, s0 + int(0.6 * sr))
        if s1 - s0 < int(0.06 * sr):
            continue
        w = y[s0:s1] * np.hanning(s1 - s0)
        n = 1 << 18
        sp = np.abs(np.fft.rfft(w, n))
        fr = np.fft.rfftfreq(n, 1 / sr)
        L = 20 * np.log10(sp + 1e-12)
        m = (fr > 20) & (fr < 8000)
        pk, _ = signal.find_peaks(L[m], prominence=12, distance=20)
        pf, pl = fr[m][pk], L[m][pk]
        if len(pf) == 0:
            continue
        # source fundamental: lowest peak within 30 dB of the strongest
        cand = pf[pl > pl.max() - 30]
        f0 = cand.min()
        # self-oscillation: strongest peak not within 1.5 % of a harmonic of f0
        nonh = [(l, f) for f, l in zip(pf, pl) if abs(f / f0 - round(f / f0)) * f0 > 0.015 * f]
        so = max(nonh)[1] if nonh else float("nan")
        sol = max(nonh)[0] if nonh else float("nan")
        print("%8.3f  f0 %8.2f Hz  VCO %8.2f Hz  selfosc %8.1f Hz (%5.1f dB; strongest harmonic %5.1f dB)"
              % (a + s0 / sr, f0, f0 * 2 ** sub, so, sol, pl.max()))


if __name__ == "__main__":
    main()
