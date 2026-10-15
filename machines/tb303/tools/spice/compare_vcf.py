#!/usr/bin/env python3
"""The voice model's VCF against a SPICE simulation of the schematic (tools/spice/vcf.cir).

Usage (from the machine folder, after building tb303blocks):
    ../../.venv/bin/python tools/spice/compare_vcf.py [--models datasheet,ds-low,ds-high] [--only ac,ring,signal]

Both sides get the same ladder current (the model's at each CUT OFF setting, service-notes
calibration, at rest) and RESONANCE, and the model's Q21 runs at the simulation's operating point
(beta and tail current from SPICE's .op). Tests:
  ac      small-signal response from the VCO side of R62 to VCF OUT: SPICE's AC analysis against
          the model's impulse response. Prints the gain at 100 Hz (the passband), the peak's
          frequency and height, and the largest difference from 30 Hz to 8 kHz.
  ring    RESONANCE full: how fast the ring after a kick decays (dB per 100 ms; > 0 grows: it
          self-oscillates).
  signal  the saw and the square at C2 and C4 through the filter: output level and log-spectral
          distance (model against SPICE)."""
import os, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(ROOT, "..", ".."))
BLOCKS = os.path.join(REPO, "build", "cmake", "tb303blocks")
MODELS = {"datasheet": os.path.join(HERE, "models-datasheet.lib"),
          "ds-low": os.path.join(HERE, "models-datasheet-low.lib"),
          "ds-high": os.path.join(HERE, "models-datasheet-high.lib")}
FS = 176400
SAW_TOP, SAW_BOTTOM, TRESET = 10.85, 5.6, 1e-6
CUTOFFS = [0.0, 0.5, 0.75, 1.0]


def ladder_current(cutoff):
    return float(subprocess.run([BLOCKS, "iff", str(cutoff), "0", "0.5", "service"], capture_output=True, text=True, check=True).stdout)


def spice(models, body, analysis, outputs, tmp, params):
    deck = os.path.join(tmp, "deck.cir")
    out = os.path.join(tmp, "spice.txt")
    p = " ".join(f"{k}={v}" for k, v in params.items())
    with open(deck, "w") as fh:
        fh.write(f"* compare_vcf\n.param {p}\n.include {models}\n.include {os.path.join(HERE, 'vcf.cir')}\n{body}\n")
        fh.write(f".control\nset wr_singlescale\noption numdgt=10\n{analysis}\nwrdata {out} {outputs}\nquit\n.endc\n.end\n")
    r = subprocess.run(["ngspice", "-b", deck], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        raise RuntimeError(r.stdout[-2000:] + r.stderr[-2000:])
    return np.loadtxt(out)


def operating_point(models, tmp):
    d = spice(models, "Vin nin 0 DC 8.2", "op\nlet b = @q21l[ic]/@q21l[ib]\nlet t = @q20[ic]", "b t", tmp, {"iff": 20e-6, "kres": 0.5})
    d = np.atleast_2d(d)
    return float(d[0, 1]), float(d[0, 2]) * 1e3   # beta, tail mA


LOOPSCALE = None


def ours(args, tmp):
    out = os.path.join(tmp, "ours.txt")
    extra = [str(LOOPSCALE)] if LOOPSCALE is not None else []
    subprocess.run([BLOCKS, "vcf"] + [str(a) for a in args] + [out] + extra, capture_output=True, check=True)
    return np.loadtxt(out)


def test_ac(models, beta, tail, tmp):
    print(f"\nSmall-signal response, VCO side of R62 to VCF OUT ({os.path.basename(models)}; Q21 beta {beta:.0f}, tail {tail:.3f} mA)")
    print(f"{'cutoff':>6} {'res':>4} | {'gain at 100 Hz: ours / SPICE':30} | {'peak: ours / SPICE':34} | max diff 30 Hz-8 kHz")
    for c in CUTOFFS:
        iF = ladder_current(c)
        for res in (0.0, 0.5, 0.85):
            s = spice(models, "Vin nin 0 DC 8.2 AC 1", "ac dec 40 10 20k", "vdb(nout)", tmp, {"iff": iF, "kres": res})
            fs_, gs = s[:, 0], s[:, 1]
            d = ours(["impulse", 0, 0.5, iF, res, beta, tail, 1.2, 0.6], tmp)
            y = d[:, 2] - d[0, 2]
            H = np.fft.rfft(y) / 0.5
            fo = np.fft.rfftfreq(len(y), 1 / FS)
            go = 20 * np.log10(np.abs(np.interp(fs_, fo, np.abs(H))) + 1e-30)
            band = (fs_ >= 30) & (fs_ <= 8000)
            i100 = np.argmin(np.abs(fs_ - 100))
            po, ps = np.argmax(np.where(band, go, -1e9)), np.argmax(np.where(band, gs, -1e9))
            diff = np.max(np.abs(go[band] - gs[band]))
            print(f"{c:6.2f} {res:4.2f} | {go[i100]:+7.1f} / {gs[i100]:+7.1f} dB (diff {go[i100] - gs[i100]:+5.1f}) | "
                  f"{fs_[po]:6.0f} Hz {go[po]:+6.1f} / {fs_[ps]:6.0f} Hz {gs[ps]:+6.1f} dB | {diff:5.1f} dB")


def ring_end(t, y):
    """What is left of the ring at the end (the last 50 ms) against its start (2-4 ms after the
    kick), dB: near 0 or above, it self-oscillates (and ring_rate, which measures down to the end
    level, can't see a decay)."""
    y = y - np.median(y)
    env = np.abs(y)
    start = np.max(env[(t >= 0.002) & (t < 0.004)])
    return 20 * np.log10(np.max(env[t > t[-1] - 0.05]) / start)


def ring_rate(t, y):
    """Decay of the ring (dB per 100 ms): a line through its envelope in 2 ms blocks from 2 ms after
    the kick, while it is more than 20 dB above the floor (the last 50 ms) or 250 ms."""
    y = y - np.median(y)
    env = np.abs(y)
    floor = np.max(env[t > t[-1] - 0.05])
    blocks = [(t0, np.max(env[(t >= t0) & (t < t0 + 0.002)])) for t0 in np.arange(0.002, 0.25, 0.002)]
    pts = []
    for t0, e in blocks:
        if e < 10 * floor:
            break
        pts.append((t0, e))
    if len(pts) < 3:
        return float("-inf")
    tt, ee = np.array([p[0] for p in pts]), np.array([p[1] for p in pts])
    return 100e-3 * np.polyfit(tt, 20 * np.log10(ee), 1)[0]


def test_ring(models, beta, tail, tmp):
    print(f"\nRESONANCE full: ring after a kick, dB per 100 ms (> 0: self-oscillates) ({os.path.basename(models)})")
    for c in CUTOFFS:
        iF = ladder_current(c)
        d = ours(["impulse", 0, 0.5, iF, 1.0, beta, tail, 0.9, 0.6], tmp)
        ro = ring_rate(d[:, 0] - d[0, 0], d[:, 2])
        s = spice(models, "Vin nin 0 DC 8.2 PWL(0 8.2 0.6 8.2 0.600001 8.7 0.600007 8.7 0.600008 8.2)",
                  "tran 2u 0.9 0.6 2u", "v(nout)", tmp, {"iff": iF, "kres": 1.0})
        rs = ring_rate(s[:, 0] - s[0, 0], s[:, 1])
        eo, es = ring_end(d[:, 0] - d[0, 0], d[:, 2]), ring_end(s[:, 0] - s[0, 0], s[:, 1])
        print(f"  cutoff {c:4.2f} (ladder {iF * 1e6:5.1f} uA): ours {ro:+7.1f}   SPICE {rs:+7.1f}   "
              f"left after 300 ms: ours {eo:+6.1f} dB, SPICE {es:+6.1f} dB")


def lsd(a, b):
    n = min(len(a), len(b))
    w = np.hanning(n)
    A = np.abs(np.fft.rfft(a[:n] * w)) + 1e-12
    B = np.abs(np.fft.rfft(b[:n] * w)) + 1e-12
    f = np.fft.rfftfreq(n, 1 / FS)
    band = (f > 20) & (f < 10000)
    la, lb = 20 * np.log10(A[band]), 20 * np.log10(B[band])
    keep = np.maximum(la, lb) > max(la.max(), lb.max()) - 60
    return np.sqrt(np.mean((la[keep] - lb[keep] - np.mean(la[keep] - lb[keep])) ** 2))


def test_signal(models, beta, tail, tmp):
    print(f"\nSaw and square through the filter: level (ours - SPICE) and spectral distance ({os.path.basename(models)})")
    print(f"{'wave':6} {'note':4} {'cutoff':>6} {'res':>4} | {'level':>8} {'LSD':>6} | {'out RMS ours / SPICE (mV)':>26}")
    for wave in ("saw", "square"):
        for note, f in (("C2", 65.406), ("C4", 261.63)):
            for c in (0.0, 0.5, 1.0):
                iF = ladder_current(c)
                for res in (0.0, 0.5, 1.0):
                    d = ours([wave, f, 0, iF, res, beta, tail, 1.0, 0.7], tmp)
                    yo = d[:, 2]
                    if wave == "saw":
                        src = f"Vin nin 0 PULSE({SAW_TOP} {SAW_BOTTOM} 0 {1 / f - TRESET} {TRESET} 0 {1 / f})"
                        body = src + f"\n.ic v(nc17)={0.5 * (SAW_TOP + SAW_BOTTOM)}"
                    else:
                        # The square comes from the model's VCO (the shaper matches SPICE's): its
                        # waveform drives SPICE as a PWL source.
                        dv = ours(["square", f, 0, iF, res, beta, tail, 1.0, 0.0], tmp)
                        pwl = os.path.join(tmp, "sq.pwl")
                        np.savetxt(pwl, dv[:, :2], fmt="%.9f")
                        body = (f"Ain %vd([nin 0]) sqsrc\n.model sqsrc filesource (file=\"{pwl}\" amploffset=[0] amplscale=[1] "
                                f"timeoffset=0 timescale=1 timerelative=false amplstep=false)\n"
                                f".ic v(nc17)={np.mean(dv[len(dv) // 2:, 1]):.4f}")
                    s = spice(models, body, "tran 2u 1.0 0.7 2u", "v(nout)", tmp, {"iff": iF, "kres": res})
                    ys = np.interp(d[:, 0], s[:, 0], s[:, 1])
                    lo, ls_ = np.std(yo), np.std(ys)
                    print(f"{wave:6} {note:4} {c:6.2f} {res:4.2f} | {20 * np.log10(lo / ls_):+7.2f}  {lsd(yo, ys):5.2f} | "
                          f"{lo * 1e3:10.1f} / {ls_ * 1e3:8.1f}")


def main():
    args = sys.argv[1:]
    models = args[args.index("--models") + 1].split(",") if "--models" in args else ["datasheet"]
    only = args[args.index("--only") + 1].split(",") if "--only" in args else ["ac", "ring", "signal"]
    global LOOPSCALE
    if "--loopscale" in args:
        LOOPSCALE = float(args[args.index("--loopscale") + 1])
    for m in models:
        with tempfile.TemporaryDirectory() as tmp:
            beta, tail = operating_point(MODELS[m], tmp)
            if "ac" in only:
                test_ac(MODELS[m], beta, tail, tmp)
            if "ring" in only:
                test_ring(MODELS[m], beta, tail, tmp)
            if "signal" in only:
                test_signal(MODELS[m], beta, tail, tmp)


if __name__ == "__main__":
    main()
