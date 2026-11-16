#!/usr/bin/env python3
"""Re-measures the one real-unit clip of a self-oscillating ENV sweep (shootout:
friendly-noise ... selfosc_vcf_env_sweep_res_max_adsr_max: RES 10, FREQ 0, KYBD 10, ENV 10,
A/D/S/R 10, C4), without the pitch tracker's 30 Hz-5 kHz window that real.py used.

The clip also carries a steady C4 (261.6 Hz) from the VCO, so the self-oscillation is followed as
the largest spectral peak off C4's harmonics. The VCA is in ENV mode, so the sweep's resting
point is never heard on its own; it comes out of the release instead, which is an exponential in
octaves (the ENV decays through an RC toward the bus, and the expo makes volts octaves):
    log2 f(t) = log2 f_rest + D exp(-t / tau_R).
The attack, toward +15 V and ending at the ENV's peak, gives the ratio
    k = (V_top - bus) / (peak - bus)     (1.82 on the schematic)
from its curvature: log2 (f/f_rest) = D_top k (1 - exp(-(t - t_gate)/tau_A)).

Usage (from machines/sh101): ../../.venv/bin/python -I tools/spread/clip_sweep.py
"""
import os, sys

import numpy as np
import soundfile as sf
from scipy import optimize
from scipy.signal import medfilt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
CLIP = os.path.join(ROOT, "private", "reference", "shootout", "wav",
                    "roland_sh-101_real__friendly-noise_youtube_eStzYAZoOAI__selfosc_vcf_env_sweep_res_max_adsr_max.wav")


def track(x, sr, n=16384, hop_s=0.02, steady=261.6):
    hop = int(hop_s * sr)
    win = np.hanning(n)
    fr = np.fft.rfftfreq(n, 1 / sr)
    comb = np.zeros_like(fr, bool)
    for k in range(1, 40):
        comb |= abs(fr - k * steady) < 0.02 * k * steady + 6
    band = (fr > 20) & (fr < 16000) & ~comb
    ts, fs, ls = [], [], []
    for s0 in range(0, len(x) - n, hop):
        sp = np.abs(np.fft.rfft(x[s0:s0 + n] * win))
        i = int(np.argmax(sp * band))
        a, b, c = np.log(sp[i - 1:i + 2] + 1e-20)
        ts.append((s0 + n / 2) / sr)
        fs.append((i + 0.5 * (a - c) / (a - 2 * b + c)) * sr / n)
        ls.append(20 * np.log10(sp[i] + 1e-20))
    ts, fs, ls = map(np.array, (ts, fs, ls))
    return ts, medfilt(np.log2(fs), 9), ls - ls.max()


def main():
    x, sr = sf.read(CLIP)
    x = x if x.ndim == 1 else x.mean(1)
    ts, lf, ls = track(x, sr)
    pl = np.where(lf > np.log2(4500))[0]
    top = 2 ** np.median(lf[pl])
    t0, t_on = ts[pl[-1]], ts[pl[0]]
    sel = (ts > t0) & (ls > -45)
    t, y = ts[sel] - t0, lf[sel]
    rel = lambda t, a, D, tau: a + D * np.exp(-t / tau)
    p, cov = optimize.curve_fit(rel, t, y, p0=[3, 8, 4.7], bounds=([-4, 1, 0.5], [12, 16, 30]))
    rms = 1200 * np.sqrt(np.mean((y - rel(t, *p)) ** 2))
    f_rest = 2 ** p[0]
    depth = np.log2(top / f_rest)
    print(f"plateau (ENV peak, S 10): {top:.0f} Hz, {t_on:.2f}-{t0:.2f} s")
    print(f"release: rest {f_rest:.1f} Hz, tau_R {p[2]:.2f} s ({len(t)} frames over {t[-1]:.1f} s, fit rms {rms:.0f} cents)")
    print(f"ENV depth, plateau over rest: {depth:.2f} oct")
    sel = (ts < t_on) & (ls > -35) & (lf > p[0] + 0.5)
    ta, ya = ts[sel], lf[sel] - p[0]
    att = lambda t, k, tau, ts_: depth * k * (1 - np.exp(-(t - ts_) / tau))
    q, cq = optimize.curve_fit(att, ta, ya, p0=[1.8, 4.0, 0.0], bounds=([1.01, 0.3, -2.0], [20, 30, 0.4]))
    rmsa = 1200 * np.sqrt(np.mean((ya - att(ta, *q)) ** 2))
    print(f"attack: k {q[0]:.2f} +- {np.sqrt(cq[0, 0]):.2f} (schematic 1.82), tau_A {q[1]:.2f} s, "
          f"attack time {q[1] * np.log(q[0] / (q[0] - 1)):.2f} s (fit rms {rmsa:.0f} cents)")
    print(f"ENV peak above the bus implied by k: {(14.87 - 5.0) / q[0]:.2f} V (schematic 5.42 V)")
    for k in (1.82, 2.35):
        q2, _ = optimize.curve_fit(lambda t, tau, ts_: att(t, k, tau, ts_), ta, ya, p0=[4.0, 0.0], bounds=([0.3, -2], [30, 0.4]))
        r = 1200 * np.sqrt(np.mean((ya - att(ta, k, *q2)) ** 2))
        print(f"  k fixed at {k}: fit rms {r:.0f} cents")


if __name__ == "__main__":
    main()
