#!/usr/bin/env python3
"""fit.py VOICES PARAM=DEFAULT:LO:HI ... [--cal FIXED ...] [--fit-set] [--iters N] [--weights pitch,tail,lsd]:
fits part values (voicerun --cal voice.param) to Michael Fischer's recordings of the given voices
(fischer.py's records) by Nelder-Mead in log space, on the pitch error (cents), the tail's decay
rate (log2 ratio) and the log-spectral distance. Prints the best values and the objective."""
import argparse, os, sys
import numpy as np, soundfile as sf
from scipy.optimize import minimize
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fischer as F

ap = argparse.ArgumentParser()
ap.add_argument('voices')
ap.add_argument('params', nargs='+')
ap.add_argument('--cal', action='append', default=[])
ap.add_argument('--set', action='append', default=[])
ap.add_argument('--fit-set', action='store_true')
ap.add_argument('--iters', type=int, default=60)
ap.add_argument('--weights', default='1,1,0.5,0')  # pitch, tail rate, lsd, -20 dB time
ap.add_argument('--accent', action='store_true')
a = ap.parse_args()
w = [float(x) for x in a.weights.split(',')] + [0, 0, 0, 0]
wp, wt, wl, wd = w[:4]


def value(s):
    s = s.strip().lower()
    mult = {'p': 1e-12, 'n': 1e-9, 'u': 1e-6, 'm': 1e-3, 'k': 1e3, 'meg': 1e6}
    for suf in ('meg', 'p', 'n', 'u', 'm', 'k'):
        if s.endswith(suf):
            return float(s[:-len(suf)]) * mult[suf]
    return float(s)


specs = []
for p in a.params:
    name, rng = p.split('=')
    d, lo, hi = (value(x) for x in rng.split(':'))
    specs.append((name, d, lo, hi))

want = set(a.voices.upper().split(','))
recs = []
for rec in F.records():
    if rec['instrument'] not in want:
        continue
    marks = [F.mark(k) for n, k in rec['knobs'].items() if n != 'LEVEL']
    if a.fit_set and any(m in (2.5, 7.5) for m in marks):
        continue
    ref, sr = sf.read(os.path.join(F.REF, rec['path']))
    ref = ref[max(0, F.onset(ref) - 20):]
    recs.append((rec, ref, sr, F.decay_rate(ref, sr), F.pitch(ref, sr, F.PITCH_BAND.get(rec['instrument'], (30, 5000))) if rec['instrument'] in F.TUNED else None, F.decay_times(ref, sr)[1]))
print('%d recordings' % len(recs))


def evaluate(vals, verbose=False):
    cals = list(a.cal) + ['%s=%g' % (s[0], v) for s, v in zip(specs, vals)]
    cents, tails, lsds, d20 = [], [], [], []
    for rec, ref, sr, rate_ref, p_ref, t20 in recs:
        mod, _ = F.render(rec, a.accent, a.set, cals, F.VOICERUN, min(len(ref) / sr, 1.6) + 0.1)
        mod = mod[max(0, F.onset(mod) - 20):]
        n = min(len(ref), len(mod))
        r, m = ref[:n], mod[:n]
        if p_ref:
            cents.append(1200 * np.log2(F.pitch(m, sr, F.PITCH_BAND.get(rec['instrument'], (30, 5000))) / p_ref))
        rm = F.decay_rate(m, sr)
        if np.isfinite(rate_ref) and np.isfinite(rm) and rm < 0:
            tails.append(np.log2(rate_ref / rm))
        t20m = F.decay_times(m, sr)[1]
        if np.isfinite(t20) and np.isfinite(t20m):
            d20.append(np.log2(t20m / t20))
        elif np.isfinite(t20):
            d20.append(1.0)  # never got 20 dB down in the recording's length
        l = F.lsd(r, m, sr)
        if np.isfinite(l):
            lsds.append(l)
    c = np.array(cents) if cents else np.zeros(1)
    t = np.array(tails) if tails else np.zeros(1)
    l = np.array(lsds) if lsds else np.zeros(1)
    d = np.array(d20) if d20 else np.zeros(1)
    obj = wp * np.mean((c / 10) ** 2) + wt * np.mean((t / 0.07) ** 2) + wl * np.mean((l / 2) ** 2) + wd * np.mean((d / 0.07) ** 2)
    if verbose:
        print('  cents %+.0f (rms %.0f)  tail %+.3f (rms %.3f)  lsd %.2f  -20 dB %+.3f (rms %.3f) -> %.3f' % (np.mean(c), np.sqrt(np.mean(c ** 2)),
              np.mean(t), np.sqrt(np.mean(t ** 2)), np.mean(l), np.mean(d), np.sqrt(np.mean(d ** 2)), obj))
    return obj


x0 = np.log([s[1] for s in specs])
lo = np.log([s[2] for s in specs]); hi = np.log([s[3] for s in specs])
def f(x):
    x = np.clip(x, lo, hi)
    return evaluate(np.exp(x))
print('start:'); evaluate(np.exp(x0), True)
res = minimize(f, x0, method='Nelder-Mead', options={'maxfev': a.iters, 'xatol': 0.005, 'fatol': 0.01,
               'initial_simplex': np.vstack([x0] + [x0 + np.eye(len(x0))[i] * 0.08 for i in range(len(x0))])})
best = np.exp(np.clip(res.x, lo, hi))
print('best:'); evaluate(best, True)
print(' '.join('--cal %s=%.4g' % (s[0], v) for s, v in zip(specs, best)))
