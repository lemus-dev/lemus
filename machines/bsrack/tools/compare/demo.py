#!/usr/bin/env python3
"""Compares a render of the Bass Station Rack's built-in demo (bsrackrender: MODE x3, AUDITION/DEMO)
against a real unit's recording of it, frame by frame.

    demo.py RECORDING RENDER [--ratio R] [--json]

Both are decoded with ffmpeg to mono 44.1 kHz. The render's time is mapped onto the recording's as
recording = render * ratio + offset (ratio: the recording's speed against ours; the reference
recording runs 0.6 % fast in tempo and pitch alike, docs/improvements.md), the offset found by
cross-correlating onset envelopes. Levels: the recording's absolute level is unknown, so both are
normalised to their RMS over the aligned span. Prints per-band level differences (dB, 1/3-octave
bands), the spectral centroid's median ratio, the frame-loudness correlation and the median absolute
loudness difference over the frames where the recording sounds.
"""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np

SR = 44100

def load(path):
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(out, dtype=np.float32).astype(float)

def frames(x, n=2048, hop=512):
    if len(x) < n:
        x = np.pad(x, (0, n - len(x)))
    w = np.hanning(n)
    f = np.lib.stride_tricks.sliding_window_view(x, n)[::hop] * w
    return np.abs(np.fft.rfft(f, axis=1)) ** 2

def onset(P):
    L = np.log1p(1e3 * P)
    d = np.maximum(0, np.diff(L, axis=0)).sum(1)
    return d - np.convolve(d, np.ones(16) / 16, "same")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("recording"); ap.add_argument("render")
    ap.add_argument("--ratio", type=float, default=1.0060)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    rec, ren = load(a.recording), load(a.render)
    # resample the render onto the recording's time base (render time * ratio)
    t = np.arange(int(len(ren) / a.ratio)) * a.ratio
    ren = np.interp(t, np.arange(len(ren)), ren)
    Pr, Pn = frames(rec), frames(ren)
    o1, o2 = onset(Pr), onset(Pn)
    c = np.correlate(o1 - o1.mean(), o2 - o2.mean(), "full")
    lag = c.argmax() - (len(o2) - 1)  # recording frame = render frame + lag
    if lag >= 0:
        Pr = Pr[lag:]
    else:
        Pn = Pn[-lag:]
    m = min(len(Pr), len(Pn))
    Pr, Pn = Pr[:m], Pn[:m]
    f = np.fft.rfftfreq(2048, 1 / SR)
    # 1/3-octave bands 40 Hz - 16 kHz
    edges = 40 * 2 ** (np.arange(0, 28) / 3)
    B = np.array([(f >= lo) & (f < hi) for lo, hi in zip(edges[:-1], edges[1:])])
    br, bn = Pr @ B.T, Pn @ B.T
    gr, gn = br.sum(), bn.sum()
    br, bn = br / gr, bn / gn
    loud_r, loud_n = 10 * np.log10(br.sum(1) + 1e-12), 10 * np.log10(bn.sum(1) + 1e-12)
    sounding = loud_r > loud_r.max() - 40
    band_db = 10 * np.log10(bn[sounding].mean(0) + 1e-15) - 10 * np.log10(br[sounding].mean(0) + 1e-15)
    cen = lambda P: (P * f).sum(1) / (P.sum(1) + 1e-30)
    cr = np.median(cen(Pn[sounding]) / (cen(Pr[sounding]) + 1e-9))
    corr = np.corrcoef(loud_r[sounding], loud_n[sounding])[0, 1]
    mad = np.median(np.abs(loud_r[sounding] - loud_n[sounding]))
    res = {"lag_s": lag * 512 / SR, "frames": int(sounding.sum()), "centroid_ratio": float(cr), "loudness_corr": float(corr),
           "loudness_mad_db": float(mad), "bands_hz": [float(x) for x in edges[:-1]], "band_db": [float(x) for x in band_db]}
    if a.json:
        print(json.dumps(res)); return
    print("offset %.3f s, %d sounding frames" % (res["lag_s"], res["frames"]))
    print("centroid ratio (ours / real), median: %.2f" % cr)
    print("frame loudness: correlation %.3f, median |difference| %.1f dB" % (corr, mad))
    print("band level, ours - real (dB, normalised):")
    print("  " + "  ".join("%5.0f:%+5.1f" % (e, d) for e, d in zip(edges[:-1], band_db)))

if __name__ == "__main__":
    main()
