#!/usr/bin/env python3
"""Compares two sets of renders of the specs (e.g. Roland's plug-in vs ours), spec by spec.

Usage: compare.py REF_DIR TEST_DIR [--specs suite,calibration] [--out DIR] [--only SUBSTR,...]
                  [--noplot] [--quiet]

For each spec rendered in both directories it prints one line:
  gain   level difference TEST-REF over the sounding part (dB)
  lsd    log-spectral distance after gain matching, frame by frame (dB, lower is better)
  lsdA   the same between the time-averaged spectra: insensitive to phase and to random sources
         (noise, the RANDOM wave), so it is the fairer number for those
  cent   spectral-centroid ratio TEST/REF (geometric mean over the sounding part)
  f0     median pitch difference (cents), where both are pitched
and writes OUT/<name>.png (waveform at a steady point, RMS envelope, centroid, pitch, average
spectrum) unless --noplot, plus OUT/summary.json. The MEAN line averages every spec; specs whose
name starts with a group prefix are also averaged per group (osc, vcf, selfosc, env, ...).

The renders are aligned on their first onset, refined by cross-correlation within +-12 samples
(a wider search locks onto the wrong period of a steady wave). Run with the venv's python.
"""
import argparse, json, os, sys
from concurrent.futures import ProcessPoolExecutor
import numpy as np
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import analyse as A

SR = 44100


def load(path):
    x, sr = A.load(path)
    if sr != SR:
        x = signal.resample_poly(x, SR, sr)
    return x


def onset(x, db=-40):
    env = np.abs(x)
    thr = env.max() * 10 ** (db / 20)
    return int(np.argmax(env > thr))


def align(ref, test, search=12):
    lag = onset(test) - onset(ref)
    r0 = onset(ref)
    n = int(0.05 * SR)
    a = ref[r0:r0 + n]
    best, best_lag = -1e18, lag
    for d in range(-search, search + 1):
        t0 = r0 + lag + d
        if t0 < 0 or t0 + n > len(test):
            continue
        c = float(np.dot(a, test[t0:t0 + n]))
        if c > best:
            best, best_lag = c, lag + d
    lag = best_lag
    test = test[lag:] if lag > 0 else np.concatenate([np.zeros(-lag), test])
    return test, lag


def frames(x, hop=0.005, win=0.03):
    h, w = int(hop * SR), int(win * SR)
    n = max(1, (len(x) - w) // h)
    idx = np.arange(w)[None, :] + h * np.arange(n)[:, None]
    return x[idx] * np.hanning(w)[None, :], h, w


def features(x):
    f, h, w = frames(x)
    spec = np.abs(np.fft.rfft(f, 8192, axis=1))
    freqs = np.fft.rfftfreq(8192, 1 / SR)
    rms = np.sqrt(np.mean(f ** 2, axis=1) * 8 / 3)
    p = spec ** 2
    cent = (p * freqs[None, :]).sum(1) / np.maximum(p.sum(1), 1e-20)
    t = (np.arange(len(rms)) * h + w / 2) / SR
    return t, rms, cent, spec, freqs


def compare(name, ref_path, test_path, out_dir, plot=True):
    ref, test = load(ref_path), load(test_path)
    if np.abs(ref).max() < 1e-6 or np.abs(test).max() < 1e-6:
        return dict(name=name, silent=True, gain=float("nan"), lsd=float("nan"), lsdA=float("nan"),
                    cent=float("nan"), cents=float("nan"), lag=0)
    test, lag = align(ref, test)
    n = min(len(ref), len(test))
    ref, test = ref[:n], test[:n]
    t, r_rms, r_cent, r_spec, freqs = features(ref)
    _, s_rms, s_cent, s_spec, _ = features(test)
    loud = r_rms > r_rms.max() * 10 ** (-40 / 20)
    gain = float(A.db(np.sqrt(np.mean(s_rms[loud] ** 2))) - A.db(np.sqrt(np.mean(r_rms[loud] ** 2))))
    g = 10 ** (-gain / 20)
    band = (freqs > 20) & (freqs < 12000)
    lr = A.db(r_spec[loud][:, band]); ls = A.db(s_spec[loud][:, band] * g)
    floor = lr.max() - 80
    lsd = float(np.sqrt(np.mean((np.maximum(lr, floor) - np.maximum(ls, floor)) ** 2)))
    ar = A.db(np.sqrt(np.mean(r_spec[loud][:, band] ** 2, 0)))
    as_ = A.db(np.sqrt(np.mean((s_spec[loud][:, band] * g) ** 2, 0)))
    floor_a = ar.max() - 80
    lsd_a = float(np.sqrt(np.mean((np.maximum(ar, floor_a) - np.maximum(as_, floor_a)) ** 2)))
    cent_ratio = float(np.exp(np.mean(np.log(np.maximum(s_cent[loud], 1) / np.maximum(r_cent[loud], 1)))))
    tp, r_f0 = A.pitch_track(ref, SR, 0.01, 0.04, 25, 5000)
    _, s_f0 = A.pitch_track(test, SR, 0.01, 0.04, 25, 5000)
    ok = np.isfinite(r_f0) & np.isfinite(s_f0)
    cents = float(np.median(1200 * np.log2(s_f0[ok] / r_f0[ok]))) if ok.any() else float("nan")
    if plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(5, 1, figsize=(13, 16))
        li = np.where(loud)[0]
        t_mid = t[li[0] + int(0.3 * (li[-1] - li[0]))] if len(li) else 0.2
        f0 = np.nanmedian(r_f0) if np.isfinite(r_f0).any() else 100
        span = int(min(4 / f0, 0.1) * SR)
        i0 = int(t_mid * SR)
        tt = np.arange(span) / SR * 1000
        ax[0].plot(tt, ref[i0:i0 + span], label="ref", lw=1.2)
        ax[0].plot(tt, test[i0:i0 + span] * g, label="test (gain matched)", lw=1.0)
        ax[0].set_title(f"{name}: waveform at {t_mid:.2f}s   gain {gain:+.1f} dB  lsd {lsd:.1f} dB  lag {lag}")
        ax[0].legend()
        ax[1].plot(t, A.db(r_rms), label="ref"); ax[1].plot(t, A.db(s_rms * g), label="test")
        ax[1].set_ylim(A.db(r_rms.max()) - 60, A.db(r_rms.max()) + 6); ax[1].set_title("RMS (dB)"); ax[1].legend()
        ax[2].plot(t[loud], r_cent[loud], ".", ms=2, label="ref"); ax[2].plot(t[loud], s_cent[loud], ".", ms=2, label="test")
        ax[2].set_yscale("log"); ax[2].set_title(f"spectral centroid (Hz), ratio {cent_ratio:.3f}"); ax[2].legend()
        ax[3].plot(tp, r_f0, label="ref"); ax[3].plot(tp, s_f0, label="test"); ax[3].set_yscale("log")
        ax[3].set_title(f"pitch (Hz), median diff {cents:+.1f} cents"); ax[3].legend()
        ax[4].semilogx(freqs[band], ar, label="ref", lw=0.8)
        ax[4].semilogx(freqs[band], as_, label="test", lw=0.8)
        ax[4].set_title(f"average spectrum (dB), lsdA {lsd_a:.1f} dB"); ax[4].legend()
        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, name + ".png"), dpi=60)
        plt.close(fig)
    return dict(name=name, gain=gain, lsd=lsd, lsdA=lsd_a, cent=cent_ratio, cents=cents, lag=lag)


def _job(a):
    return compare(*a)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref"); ap.add_argument("test")
    ap.add_argument("--specs", default="suite,calibration")
    ap.add_argument("--out", default=None)
    ap.add_argument("--only", default=None)
    ap.add_argument("--noplot", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    out = a.out or os.path.join(a.test, "compare")
    os.makedirs(out, exist_ok=True)
    specs = A.specs(*a.specs.split(","))
    jobs = []
    for name, s in specs.items():
        if s.get("ours_only") or s.get("roland_only"):
            continue
        if a.only and not any(o in name for o in a.only.split(",")):
            continue
        rp, tp = os.path.join(a.ref, name + ".wav"), os.path.join(a.test, name + ".wav")
        if os.path.exists(rp) and os.path.exists(tp):
            jobs.append((name, rp, tp, out, not a.noplot))
    with ProcessPoolExecutor() as ex:
        rows = list(ex.map(_job, jobs))
    fmt = lambda v, f: "   nan" if not np.isfinite(v) else format(v, f)
    for r in rows:
        if not a.quiet:
            if r.get("silent"):
                print(f"{r['name']:30s} SILENT in one of the two")
                continue
            print(f"{r['name']:30s} gain {fmt(r['gain'], '+6.1f')} dB  lsd {fmt(r['lsd'], '5.1f')}  lsdA {fmt(r['lsdA'], '5.1f')} dB"
                  f"  cent {fmt(r['cent'], '5.2f')}  f0 {fmt(r['cents'], '+7.1f')} c", flush=True)
    good = [r for r in rows if not r.get("silent")]
    if good:
        groups = {}
        for r in good:
            groups.setdefault(r["name"].split("-")[0], []).append(r)
        for gname, rs in sorted(groups.items()):
            print(f"{'mean ' + gname:30s} {len(rs):3d} specs  lsd {np.mean([r['lsd'] for r in rs]):5.2f}  "
                  f"lsdA {np.mean([r['lsdA'] for r in rs]):5.2f} dB  gain {np.mean([r['gain'] for r in rs]):+6.1f} dB")
        print(f"{'MEAN':30s} {len(good):3d} specs  lsd {np.mean([r['lsd'] for r in good]):5.2f}  "
              f"lsdA {np.mean([r['lsdA'] for r in good]):5.2f} dB  gain {np.mean([r['gain'] for r in good]):+6.1f} dB")
        json.dump(rows, open(os.path.join(out, "summary.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
