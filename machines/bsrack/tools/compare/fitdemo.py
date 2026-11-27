#!/usr/bin/env python3
"""Fits the voice's fitted values ([F] in docs/circuit.md) to the built-in demo's recording.

    fitdemo.py RECORDING [--render PATH] [--rounds N] [--start NAME=VALUE ...] [--free NAME,NAME,...]

Each evaluation renders the demo with bsrackrender --part=NAME=VALUE and scores it against the
recording, aligned as demo.py aligns it (0.6 % ratio, onset cross-correlation), both normalised to
their RMS:
  - loudness: the standard deviation of the per-half-second level difference (dB): the balance
    between the demo's parts and their tails;
  - brightness: the RMS of the per-half-second log2 centroid ratio;
  - bands: the RMS of the 1/3-octave band level differences, 40 Hz - 16 kHz (dB). The recording's
    hiss is about 55 dB under the demo's level in every band up there, so the top bands are the unit's.
Score = loudness + 6 * brightness + 0.5 * bands. A coordinate search in log steps (x1.5, then x1.2,
then x1.08) over the free values; prints each improvement and the final --part list.

--anchor full (the default) doesn't fit the VCF converter's divider and reference: they follow from the
FMA1's beta and two facts:
  - the top: the 24 dB filter at code 000 rings at 13.0 kHz (fiolter-4poles, 8.75-9.25 s: the
    resonant peak sits on the note's 49th-50th harmonics, 12.7-13.2 kHz, at a recording 0.6 % fast);
  - key tracking: the firmware adds 32 codes per semitone in 24 dB (docs/firmware.md), taken as
    full tracking: one octave per 384 codes about code 600.
--anchor top takes only the top (the divider is fitted, --free vcfRgnd,...); --anchor none neither.
"""
import argparse, os, subprocess, sys, tempfile
import numpy as np

SR = 44100
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "../../../.."))

def load(p):
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", p, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"], capture_output=True, check=True).stdout
    return np.frombuffer(out, dtype=np.float32).astype(float)

def windows(x, w):
    n = len(x) // w
    return x[: n * w].reshape(n, w)

class Scorer:
    def __init__(self, rec_path, ratio=1.006):
        self.rec = load(rec_path)
        self.ratio = ratio
        self.lag = None
    def score(self, ren):
        t = np.arange(int(len(ren) / self.ratio)) * self.ratio
        ren = np.interp(t, np.arange(len(ren)), ren)
        if self.lag is None:
            # onset cross-correlation once (the firmware's timing doesn't change with the voice)
            def onset(x):
                fr = windows(x, 512)
                e = np.log1p(1e3 * (fr ** 2).mean(1))
                d = np.maximum(0, np.diff(e))
                return d - d.mean()
            a, b = onset(self.rec), onset(ren)
            c = np.correlate(a, b, "full")
            self.lag = (c.argmax() - (len(b) - 1)) * 512
        rec = self.rec
        if self.lag >= 0:
            ren = np.pad(ren, (self.lag, 0))
        else:
            ren = ren[-self.lag:]
        n = min(len(rec), len(ren))
        a, b = rec[:n] / np.sqrt((rec[:n] ** 2).mean()), ren[:n] / (np.sqrt((ren[:n] ** 2).mean()) + 1e-12)
        w = SR // 2
        A, B = windows(a, w), windows(b, w)
        la = 10 * np.log10((A ** 2).mean(1) + 1e-12)
        lb = 10 * np.log10((B ** 2).mean(1) + 1e-12)
        m = la > la.max() - 30
        loud = np.std((lb - la)[m])
        f = np.fft.rfftfreq(w, 1 / SR)
        SA = np.abs(np.fft.rfft(A * np.hanning(w), axis=1)); SB = np.abs(np.fft.rfft(B * np.hanning(w), axis=1))
        ca = (SA * f).sum(1) / (SA.sum(1) + 1e-12); cb = (SB * f).sum(1) / (SB.sum(1) + 1e-12)
        bright = np.sqrt((np.log2((cb[m] + 1) / (ca[m] + 1)) ** 2).mean())
        edges = 40 * 2 ** (np.arange(0, 27) / 3)
        PA, PB = (SA[m] ** 2).sum(0), (SB[m] ** 2).sum(0)
        band = []
        for lo, hi in zip(edges[:-1], edges[1:]):
            k = (f >= lo) & (f < hi)
            band.append(10 * np.log10(PB[k].sum() / PB.sum() + 1e-15) - 10 * np.log10(PA[k].sum() / PA.sum() + 1e-15))
        bands = np.sqrt(np.mean(np.square(band)))
        return loud + 6 * bright + 0.5 * bands, dict(loud=loud, bright=bright, bands=bands)

# The converter as core/voice.cpp's Voice::expoCurrent, and section 1's ring frequency against each
# OTA's bias (bsrackcircuitrun at 192 kHz, full resonance, C33 100p as core/netlists/vcf1.cir): the
# anchors' arithmetic.
KVT, KIS = 0.025852, 1e-14
RING = [(1e-6, 29.0), (3.3e-6, 96.3), (1e-5, 289.6), (3.3e-5, 955.4), (1e-4, 2907.8), (3.3e-4, 9660.4),
        (4.4e-4, 12631.6), (4.8e-4, 13963.6), (5.2e-4, 14769.2)]

def expo(vp, iref, beta, r1=22e3, r2=22e3):
    r = r1 / r2
    veb1 = KVT * np.log(iref / KIS)
    ve = veb1 * (1 + r) + r1 * iref / beta
    x = veb1 - vp / (1 + r)
    for _ in range(60):
        ic = KIS * np.exp(min(x / KVT, 80.0))
        g = ve - vp - x * (1 + r) - r1 * ic / beta
        dg = -(1 + r) - r1 * ic / (beta * KVT)
        st = g / dg
        x -= max(-0.1, min(0.1, st))
        if abs(st) < 1e-12:
            break
    return KIS * np.exp(min(x / KVT, 80.0))

def ring_hz(iota):
    li, lf = np.log([p[0] for p in RING]), np.log([p[1] for p in RING])
    return float(np.exp(np.interp(np.log(iota), li, lf)))

def vcf_current(code, rg, rref, beta, rin=33e3, vss=-4.0, full=5.0):
    v = vss + full * code / 4096
    return expo(v * rg / (rin + rg), -vss / rref, beta)

def top_rref(rg, beta, top=13.0e3, rin=33e3):
    lo, hi = 1e3, 1e10
    for _ in range(80):
        m = np.sqrt(lo * hi)
        if ring_hz(vcf_current(0, rg, m, beta, rin) / 4) > top:
            lo = m
        else:
            hi = m
    return m

def tracking(rg, rref, beta, mid=0x600):
    """Octaves per 384 codes (one octave of keys in 24 dB) about code mid."""
    return float(np.log2(vcf_current(mid - 192, rg, rref, beta) / vcf_current(mid + 192, rg, rref, beta)))

def anchored_vcf(beta, top=13.0e3, mid=0x600, rin=33e3):
    def rref_for(rg):
        lo, hi = 1e3, 1e10
        for _ in range(80):
            m = np.sqrt(lo * hi)
            if ring_hz(vcf_current(0, rg, m, beta, rin) / 4) > top:
                lo = m
            else:
                hi = m
        return m
    lo, hi = 300.0, 30e3
    for _ in range(60):
        rg = np.sqrt(lo * hi)
        rr = rref_for(rg)
        octs = np.log2(vcf_current(mid - 192, rg, rr, beta, rin) / vcf_current(mid + 192, rg, rr, beta, rin))
        if octs > 1.0:
            hi = rg
        else:
            lo = rg
    return rg, rr

def render(parts, path, rom, exe):
    args = [exe, rom, path, "48", "--rate=44100", "--press=mode@300", "--press=mode@600", "--press=mode@900", "--press=audition@1200"]
    parts = apply_anchor(parts)
    args += ["--part=%s=%g" % kv for kv in parts.items()]
    subprocess.run(args, capture_output=True, check=True)
    import wave
    with open(path, "rb") as fh:
        data = fh.read()
    return np.frombuffer(data[44:], dtype=np.float32).astype(float)

ANCHOR, BETA0 = "full", 952.0  # beta as core/voice.h has it

def apply_anchor(parts):
    parts = dict(parts)
    beta = parts.get("fmaBeta", BETA0)
    if ANCHOR == "full":
        parts["vcfRgnd"], parts["vcfRref"] = anchored_vcf(beta)
    elif ANCHOR == "top":
        parts["vcfRref"] = top_rref(parts.get("vcfRgnd", 3.9e3), beta)
    return parts

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("recording")
    ap.add_argument("--render", default=os.path.join(REPO, "../build/bsrack/bsrackrender"))
    ap.add_argument("--rom", default=os.path.join(REPO, "../private/roms/bsrack/v1.5.bin"))
    ap.add_argument("--free", default="fmaBeta,r34,r74,vcaRgnd,vcaRref,r79,cvTau,r35")
    ap.add_argument("--anchor", choices=["full", "top", "none"], default="full")
    ap.add_argument("--start", nargs="*", default=[])
    ap.add_argument("--rounds", type=int, default=3)
    a = ap.parse_args()
    global ANCHOR
    ANCHOR = a.anchor
    # the values as of accuracy round 3 (core/voice.h, core/netlists)
    defaults = dict(vcfRgnd=2.98e3, vcfRref=603e3, fmaBeta=952, r34=207, r74=2.69e3, vcaRgnd=2.7e3, vcaRref=7.15e6, r79=1.47e6, cvTau=0.40e-3, r35=44.1e3, r32=16.9e3, r33=33e3, c33=43e-12, resFloor=1e-7, vcfFloor=6e-6)
    parts = {k: defaults[k] for k in a.free.split(",")}
    for s in a.start:
        k, v = s.split("="); parts[k] = float(v)
    sc = Scorer(a.recording)
    tmp = tempfile.mkdtemp(prefix="bsfit")
    best, info = sc.score(render(parts, os.path.join(tmp, "x.wav"), a.rom, a.render))
    print("start  %.3f  %s" % (best, info), flush=True)
    for step in [1.5, 1.2, 1.08][: a.rounds]:
        improved = True
        while improved:
            improved = False
            for k in list(parts):
                for mul in (step, 1 / step):
                    trial = dict(parts); trial[k] = parts[k] * mul
                    s, inf = sc.score(render(trial, os.path.join(tmp, "x.wav"), a.rom, a.render))
                    if s < best - 1e-3:
                        best, info, parts = s, inf, trial
                        improved = True
                        print("%-8s x%-6.3f %.3f  %s" % (k, mul, best, {x: round(y, 3) for x, y in info.items()}), flush=True)
                        break
    print("final %.3f %s" % (best, info))
    parts = apply_anchor(parts)
    if "vcfRgnd" in parts:
        print("VCF tracking %.3f octaves per 384 codes; top %.0f Hz" % (
            tracking(parts["vcfRgnd"], parts["vcfRref"], parts.get("fmaBeta", BETA0)),
            ring_hz(vcf_current(0, parts["vcfRgnd"], parts["vcfRref"], parts.get("fmaBeta", BETA0)) / 4)))
    print(" ".join("--part=%s=%g" % kv for kv in parts.items()))

if __name__ == "__main__":
    main()
