#!/usr/bin/env python3
"""Compare two sets of renders of specs/suite.json (e.g. Roland's plugin vs ours).

Usage: compare.py REF_DIR TEST_DIR [--suite specs/suite.json] [--out DIR] [--only SUBSTR]

For each test present in both directories it writes OUT/<name>.png (waveform at a steady
point, RMS envelope, spectral centroid, pitch, average spectrum) and prints one summary line:
  gain   level difference TEST-REF (dB, A-weighting-free RMS over the note)
  lsd    log-spectral distance after gain matching (dB, lower is better)
  cent   mean spectral-centroid ratio TEST/REF over the sounding part
  f0     pitch ratio in cents (median)
Run with the venv python (numpy, scipy, soundfile, matplotlib).
"""
import argparse, json, os, sys
import numpy as np
import soundfile as sf
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))


def load(path, sr_target=44100):
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    if sr != sr_target:
        x = signal.resample_poly(x, sr_target, sr)
    return x, sr_target


def onset(x, sr):
    """Time (samples) the signal first exceeds -40 dB of its peak, refined to the first
    sample above 10 % of that threshold just before."""
    env = np.abs(x)
    thr = env.max() * 10 ** (-40 / 20)
    i = int(np.argmax(env > thr))
    return i


def align(ref, test, sr, max_ms=30):
    """Shift test so its first note onset lines up with ref's, then refine by
    cross-correlation over +-1 ms around that."""
    lag = onset(test, sr) - onset(ref, sr)
    n = int(0.05 * sr)
    r0 = onset(ref, sr)
    a = ref[r0:r0 + n]
    best, best_lag = -1e9, lag
    for d in range(-int(sr / 1000), int(sr / 1000) + 1):
        t0 = r0 + lag + d
        if t0 < 0 or t0 + n > len(test):
            continue
        c = float(np.dot(a, test[t0:t0 + n]))
        if c > best:
            best, best_lag = c, lag + d
    lag = best_lag
    if lag > 0:
        test = test[lag:]
    elif lag < 0:
        test = np.concatenate([np.zeros(-lag), test])
    return test, lag


def frames(x, sr, hop=0.005, win=0.03):
    h, w = int(hop * sr), int(win * sr)
    n = max(1, (len(x) - w) // h)
    idx = np.arange(w)[None, :] + h * np.arange(n)[:, None]
    return x[idx] * np.hanning(w)[None, :], h


def features(x, sr):
    f, h = frames(x, sr)
    spec = np.abs(np.fft.rfft(f, 8192, axis=1))
    freqs = np.fft.rfftfreq(8192, 1 / sr)
    rms = np.sqrt(np.mean(f ** 2, axis=1) * 8 / 3)  # Hann window power correction
    p = spec ** 2
    cent = (p * freqs[None, :]).sum(1) / np.maximum(p.sum(1), 1e-20)
    t = (np.arange(len(rms)) * h + len(f[0]) / 2) / sr
    return t, rms, cent, spec, freqs


def pitch_track(x, sr, t_hop=0.005, fmin=25, fmax=1200):
    """Autocorrelation pitch (YIN-style difference function) per frame."""
    w = int(0.05 * sr)
    h = int(t_hop * sr)
    out = []
    for start in range(0, len(x) - 2 * w, h):
        seg = x[start:start + 2 * w]
        if np.sqrt(np.mean(seg ** 2)) < 1e-4:
            out.append(np.nan); continue
        a = seg[:w]
        tau_min, tau_max = int(sr / fmax), min(int(sr / fmin), w)
        d = np.array([np.sum((a - seg[tau:tau + w]) ** 2) for tau in range(tau_max)])
        cm = d[1:] * np.arange(1, tau_max) / np.maximum(np.cumsum(d[1:]), 1e-20)
        cm = np.concatenate([[1], cm])
        cand = np.where(cm[tau_min:] < 0.15)[0]
        if len(cand) == 0:
            tau = tau_min + int(np.argmin(cm[tau_min:]))
        else:
            tau = tau_min + cand[0]
            while tau + 1 < tau_max and cm[tau + 1] < cm[tau]:
                tau += 1
        if 1 <= tau < tau_max - 1:  # parabolic refinement
            y0, y1, y2 = cm[tau - 1], cm[tau], cm[tau + 1]
            den = y0 - 2 * y1 + y2
            tau = tau + (0.5 * (y0 - y2) / den if den != 0 else 0)
        out.append(sr / tau)
    t = (np.arange(len(out)) * h + w / 2) / sr
    return t, np.array(out)


def db(v):
    return 20 * np.log10(np.maximum(v, 1e-9))


def compare(name, ref_path, test_path, out_dir, plot=True):
    ref, sr = load(ref_path)
    test, _ = load(test_path)
    test, lag = align(ref, test, sr)
    n = min(len(ref), len(test))
    ref, test = ref[:n], test[:n]
    t, r_rms, r_cent, r_spec, freqs = features(ref, sr)
    _, s_rms, s_cent, s_spec, _ = features(test, sr)
    loud = r_rms > r_rms.max() * 10 ** (-40 / 20)
    gain = db(np.sqrt(np.mean(s_rms[loud] ** 2))) - db(np.sqrt(np.mean(r_rms[loud] ** 2)))
    g = 10 ** (-gain / 20)
    band = (freqs > 20) & (freqs < 12000)
    lr = db(r_spec[loud][:, band]); ls = db(s_spec[loud][:, band] * g)
    floor = lr.max() - 80
    lsd = float(np.sqrt(np.mean((np.maximum(lr, floor) - np.maximum(ls, floor)) ** 2)))
    cent_ratio = float(np.exp(np.mean(np.log(np.maximum(s_cent[loud], 1) / np.maximum(r_cent[loud], 1)))))
    tp, r_f0 = pitch_track(ref, sr)
    _, s_f0 = pitch_track(test, sr)
    ok = np.isfinite(r_f0) & np.isfinite(s_f0)
    cents = float(np.median(1200 * np.log2(s_f0[ok] / r_f0[ok]))) if ok.any() else float("nan")
    if plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(5, 1, figsize=(13, 16))
        # steady point: 40% into the loud region
        li = np.where(loud)[0]
        t_mid = t[li[0] + int(0.3 * (li[-1] - li[0]))] if len(li) else 0.2
        f0 = np.nanmedian(r_f0) if np.isfinite(r_f0).any() else 100
        span = int(min(4 / f0, 0.1) * sr)
        i0 = int(t_mid * sr)
        tt = np.arange(span) / sr * 1000
        ax[0].plot(tt, ref[i0:i0 + span], label="ref", lw=1.2)
        ax[0].plot(tt, test[i0:i0 + span] * g, label="test (gain matched)", lw=1.0)
        ax[0].set_title(f"{name}: waveform at {t_mid:.2f}s   gain {gain:+.1f} dB  lsd {lsd:.1f} dB  lag {lag}")
        ax[0].legend()
        ax[1].plot(t, db(r_rms), label="ref"); ax[1].plot(t, db(s_rms * g), label="test")
        ax[1].set_ylim(db(r_rms.max()) - 60, db(r_rms.max()) + 6); ax[1].set_title("RMS (dB)"); ax[1].legend()
        ax[2].plot(t[loud], r_cent[loud], ".", ms=2, label="ref"); ax[2].plot(t[loud], s_cent[loud], ".", ms=2, label="test")
        ax[2].set_yscale("log"); ax[2].set_title(f"spectral centroid (Hz), ratio {cent_ratio:.3f}"); ax[2].legend()
        ax[3].plot(tp, r_f0, label="ref"); ax[3].plot(tp, s_f0, label="test"); ax[3].set_yscale("log")
        ax[3].set_title(f"pitch (Hz), median diff {cents:+.1f} cents"); ax[3].legend()
        ax[4].semilogx(freqs[band], db(np.mean(r_spec[loud][:, band] ** 2, 0) ** 0.5), label="ref", lw=0.8)
        ax[4].semilogx(freqs[band], db(np.mean((s_spec[loud][:, band] * g) ** 2, 0) ** 0.5), label="test", lw=0.8)
        ax[4].set_title("average spectrum (dB)"); ax[4].legend()
        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, name + ".png"), dpi=60)
        plt.close(fig)
    return dict(name=name, gain=gain, lsd=lsd, cent=cent_ratio, cents=cents, lag=lag)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref"); ap.add_argument("test")
    ap.add_argument("--suite", default=os.path.join(HERE, "specs", "suite.json"))
    ap.add_argument("--out", default=None)
    ap.add_argument("--only", default=None)
    ap.add_argument("--noplot", action="store_true")
    a = ap.parse_args()
    out = a.out or os.path.join(a.test, "compare")
    os.makedirs(out, exist_ok=True)
    names = [s["name"] for s in json.load(open(a.suite))]
    rows = []
    for name in names:
        if a.only and not any(o in name for o in a.only.split(",")):
            continue
        rp, tp = os.path.join(a.ref, name + ".wav"), os.path.join(a.test, name + ".wav")
        if not (os.path.exists(rp) and os.path.exists(tp)):
            continue
        r = compare(name, rp, tp, out, plot=not a.noplot)
        rows.append(r)
        print(f"{name:28s} gain {r['gain']:+6.1f} dB  lsd {r['lsd']:5.1f} dB  cent {r['cent']:5.2f}  f0 {r['cents']:+7.1f} c", flush=True)
    if rows:
        print(f"{'MEAN':28s} gain {np.mean([r['gain'] for r in rows]):+6.1f} dB  lsd {np.mean([r['lsd'] for r in rows]):5.1f} dB")
        json.dump(rows, open(os.path.join(out, "summary.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
