#!/usr/bin/env python3
"""Reads SH-101 slider positions from a video's close-up of the VCF/ENV section (the orange index
line on each cap), frame by frame, and pairs them with the audio's dominant spectral peak and its
pitch. For docs/online-references.md (Jorb's SH-101 video). Run with the venv's python and -I.

Usage: online_sliders.py VIDEO WAV T0 T1 [--fps 4]
Columns (x in the 1280x720 frame) and the 0/10 cap heights are set for that video below.
"""
import subprocess
import sys

import numpy as np
import soundfile as sf

COLS = {"FREQ": 620, "RES": 670, "ENV": 740, "MOD": 790, "KYBD": 840, "A": 1085, "D": 1130, "S": 1185, "R": 1225}
Y0, Y10 = 224.0, 124.0


def frames(video, t0, t1, fps):
    raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-ss", str(t0), "-t", str(t1 - t0), "-i", video,
                          "-vf", f"fps={fps}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], capture_output=True).stdout
    return np.frombuffer(raw, np.uint8).reshape(-1, 720, 1280, 3)


def positions(fr):
    R, G, B = (fr[60:270, :, i].astype(int) for i in range(3))
    orange = (R > 170) & (G > 70) & (G < 170) & (B < 80)
    out = {}
    for name, x in COLS.items():
        ys, _ = np.nonzero(orange[:, x - 12:x + 12])
        out[name] = round((Y0 - (np.median(ys) + 60)) / (Y0 - Y10) * 10, 1) if len(ys) >= 8 else None
    return out


def peak(x, sr, t, win=0.25):
    a = int(t * sr)
    y = x[a:a + int(win * sr)]
    if len(y) < 1000:
        return None, None
    sp = np.abs(np.fft.rfft(y * np.hanning(len(y)), 1 << 17))
    f = np.fft.rfftfreq(1 << 17, 1 / sr)
    m = (f > 25) & (f < 10000)
    i = np.argmax(sp[m])
    return f[m][i], 20 * np.log10(np.sqrt(np.mean(y ** 2)) + 1e-9)


def main():
    video, wav, t0, t1 = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
    fps = float(sys.argv[sys.argv.index("--fps") + 1]) if "--fps" in sys.argv else 4
    x, sr = sf.read(wav)
    if x.ndim > 1:
        x = x.mean(1)
    for i, fr in enumerate(frames(video, t0, t1, fps)):
        t = t0 + i / fps
        p = positions(fr)
        f, lvl = peak(x, sr, t)
        print("%7.2f  %s  peak %7.1f Hz  %6.1f dB" % (t, " ".join(f"{k}={v}" for k, v in p.items()), f or 0, lvl or -99))


if __name__ == "__main__":
    main()
