#!/usr/bin/env python3
"""A real unit's recorded output against the model fed the same recording's dry source.

    compare.py RECORDING DRY_START OUT_START SECONDS PROGRAM [--name N] [--qvcompare PATH] [--rom PATH]
               [--work DIR] [--input 0-10]

RECORDING: a WAV (16-bit, stereo) holding both the dry source and the unit's output; DRY_START and
OUT_START (seconds) where the same passage starts in each (tools/compare/align.py finds them).
The dry passage goes through qvcompare at PROGRAM; its output is lined up with the recording by
cross-correlation, then fitted per octave band as real = a dry + w wet (least squares over both
channels, the model's two paths taken apart, in energy frame by frame (10 ms). Prints, per band,
the wet path's level against the dry's relative to the model (0 dB = the model's balance), the
fit's residual (dB under the real band's energy envelope), and per band the decay after the passage's last hit (dB/s, real and
model, from the energy decay curves) where the recording leaves a free tail.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "../../../.."))
BANDS = [125, 250, 500, 1000, 2000, 4000, 8000]


def read_wav(path):
    with wave.open(path) as w:
        n, ch, fs, sw = w.getnframes(), w.getnchannels(), w.getframerate(), w.getsampwidth()
        raw = w.readframes(n)
    if sw == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(float) / 32768
    elif sw == 4:
        x = np.frombuffer(raw, dtype="<f4").astype(float)
    else:
        sys.exit(f"{path}: {sw}-byte samples")
    return x.reshape(-1, ch), fs


def read_float_wav(path):
    # qvcompare writes WAVE_FORMAT_IEEE_FLOAT, which the wave module refuses
    d = open(path, "rb").read()
    i = d.index(b"data")
    return np.frombuffer(d[i + 8:], dtype="<f4").astype(float).reshape(-1, 2)


def write_wav16(path, x, fs):
    with wave.open(path, "wb") as w:
        w.setnchannels(x.shape[1])
        w.setsampwidth(2)
        w.setframerate(fs)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())


def band(x, fs, fc):
    X = np.fft.rfft(x, axis=0)
    f = np.fft.rfftfreq(x.shape[0], 1 / fs)
    m = (f >= fc / np.sqrt(2)) & (f < fc * np.sqrt(2))
    return np.fft.irfft(X * m[:, None], n=x.shape[0], axis=0)


def decay_rate(x, fs, start, seconds=1.0):
    """dB/s of the energy decay curve over [start, start+seconds), between -5 and -25 dB of it"""
    seg = (x[start:start + int(seconds * fs)] ** 2).sum(axis=1)
    if len(seg) < fs * 0.2:
        return None
    edc = np.cumsum(seg[::-1])[::-1]
    edc = 10 * np.log10(edc / edc[0] + 1e-30)
    t = np.arange(len(edc)) / fs
    sel = (edc <= -5) & (edc >= -25)
    if sel.sum() < 10:
        return None
    return np.polyfit(t[sel], edc[sel], 1)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("recording")
    ap.add_argument("dry_start", type=float)
    ap.add_argument("out_start", type=float)
    ap.add_argument("seconds", type=float)
    ap.add_argument("program", type=int)
    ap.add_argument("--name", default="")
    ap.add_argument("--tail", type=float, default=0, help="seconds of free tail after the passage in the recording")
    ap.add_argument("--qvcompare", default=os.path.join(REPO, "../build/qv/qvcompare"))
    ap.add_argument("--rom", default=os.path.join(REPO, "../private/roms/qv/qvv203.bin"))
    ap.add_argument("--work")
    ap.add_argument("--input", type=float, default=5.5)
    args = ap.parse_args()
    work = args.work or tempfile.mkdtemp(prefix="qvcmp")
    os.makedirs(work, exist_ok=True)
    rec, fs = read_wav(args.recording)
    n = int(args.seconds * fs)
    dry = rec[int(args.dry_start * fs):int(args.dry_start * fs) + n]
    tail = int(args.tail * fs)
    real = rec[int(args.out_start * fs):int(args.out_start * fs) + n + tail]
    tag = os.path.join(work, f"p{args.program:02d}{args.name}")
    write_wav16(tag + "-src.wav", dry, fs)
    r = subprocess.run([args.qvcompare, args.rom, tag + "-src.wav", str(args.program), tag, "--input", str(args.input)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(r.stderr)
    full, dryp, wetp = (read_float_wav(f"{tag}-{k}.wav") for k in ("full", "dry", "wet"))
    m = min(len(real), len(full))
    real, full, dryp, wetp = real[:m], full[:m], dryp[:m], wetp[:m]
    # line up: the lag (within 60 ms) that best matches the model's whole output to the recording
    a, b = real.mean(axis=1), full.mean(axis=1)
    N = 1 << int(np.ceil(np.log2(2 * m)))
    c = np.fft.irfft(np.fft.rfft(a, N) * np.conj(np.fft.rfft(b, N)), N)
    lim = int(0.06 * fs)
    cand = np.concatenate([c[:lim], c[-lim:]])
    k = int(np.argmax(cand))
    lag = k if k < lim else k - 2 * lim
    full, dryp, wetp = (np.roll(v, lag, axis=0) for v in (full, dryp, wetp))
    print(f"{r.stdout.strip()}; lined up at {lag:+d} samples ({1000 * lag / fs:+.1f} ms)")
    print(f"{'band':>6} {'wet/dry':>9} {'residual':>9} {'decay real':>11} {'model':>8}")
    body = slice(int(0.05 * fs), n)
    hop = int(0.01 * fs)

    def energy(v):
        e = (v[body] ** 2).sum(axis=1)
        k = len(e) // hop
        return e[:k * hop].reshape(k, hop).mean(axis=1)

    for fc in BANDS:
        rb, db_, wb = band(real, fs, fc), band(dryp, fs, fc), band(wetp, fs, fc)
        # in energy, frame by frame: the two paths' energies add (they don't stay coherent with the
        # recording at high frequencies), so real = a^2 dry + w^2 wet, non-negative
        Er, Ed, Ew = energy(rb), energy(db_), energy(wb)
        A = np.stack([Ed, Ew], axis=1)
        coef, *_ = np.linalg.lstsq(A, Er, rcond=None)
        coef = np.maximum(coef, 1e-12)
        res = Er - A @ coef
        resid = 10 * np.log10((res ** 2).sum() / (Er ** 2).sum() + 1e-30)
        ratio = 10 * np.log10(coef[1] / coef[0])
        coef = np.sqrt(coef)
        dr = dm = None
        if tail > 0:
            fb = band(full, fs, fc)
            dr, dm = decay_rate(rb, fs, n), decay_rate(fb, fs, n)
        fmt = lambda v: f"{v:8.1f}" if v is not None else "       -"
        print(f"{fc:6d} {ratio:+8.1f}  {resid:+8.1f}   {fmt(dr)}  {fmt(dm)}   (a {coef[0]:.3f}, w {coef[1]:.3f})")


if __name__ == "__main__":
    sys.exit(main())
