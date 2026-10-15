#!/usr/bin/env python3
"""Fit the voice's open part values to the real TB-303 in Din Sync's recordings (dinsync.py).

Renders all of specs/dinsync.json with tb303render --cal ... in parallel, scores every note against
the real one, and searches the parameters one at a time within the parts' tolerances (coordinate
search, steps halving). Score per note: log-spectral distance on sixth-octave bands from 40 Hz (dB,
floored 60 dB down, after per-note gain) + |level error| (dB, after one offset for the set) + |resonant-peak error| / 200 c,
averaged over all 400 notes. The real unit against its own retakes scores about 2.

  dinsync_fit.py [--start NAME=VALUE ...] [--fix NAME,...] [--rounds N] [--jobs N]
  dinsync_fit.py --eval NAME=VALUE ...       score one parameter set and print the breakdown

Writes the log to private/reference/dinsync/fit/log.jsonl (one line per evaluation); sets already in
the log aren't rendered again. Renders are deleted once scored.
"""
import argparse, hashlib, json, os, pickle, shutil, subprocess, sys
from concurrent.futures import ProcessPoolExecutor
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dinsync as d

FIT = os.path.join(d.DIN, "fit")

# name: (default, lower, upper, first step, log-scaled?)
PARAMS = {
    "vcfRingHz":    (500.0, 400.0, 667.0, 0.08, True),   # TM3, service notes 2 ms +-0.5
    "q21Beta":      (300.0, 120.0, 700.0, 0.25, True),   # 2SC1583 ranks
    "megPeakV":     (10.0, 8.0, 10.6, 0.04, True),       # MEG top: rail less Q38, D37, 2 V_BE
    "decayPotOhms": (1e6, 0.6e6, 1.4e6, 0.12, True),     # VR6 1M A, +-20-30 %
    "megCapUF":     (1.0, 0.5, 1.3, 0.12, True),         # C62 1 uF electrolytic, aged
    "taperAMid":    (0.15, 0.06, 0.25, 0.15, True),      # CUT OFF / ENV MOD 50k A: share at mid rotation
    "taperAShape":  (0.0, 0.0, 1.0, 0.25, False),        # 0 exponential law, 1 two straight segments
    "decayTaperMid": (0.15, 0.06, 0.25, 0.15, True),     # DECAY 1M A: the same
    "decayTaperShape": (0.0, 0.0, 1.0, 0.25, False),
}

BANDS = 2 ** (np.arange(np.log2(40), np.log2(20000), 1 / 6))


def band_matrix():
    f = np.fft.rfftfreq(d.NFFT, 1 / d.SR)
    m = np.zeros((len(BANDS) - 1, len(f)))
    for i in range(len(BANDS) - 1):
        m[i, (f >= BANDS[i]) & (f < BANDS[i + 1])] = 1
    keep = m.sum(1) > 0
    return m[keep]


BM = band_matrix()


def features(x, t0):
    """Per-note features: frame times, RMS, sixth-octave band power, resonant peak, level."""
    a = int((t0 - 0.005) * d.SR)
    n_seg = int(d.NOTE_LEN * d.SR)
    seg = x[a:a + n_seg]
    if len(seg) < n_seg:
        seg = np.concatenate([seg, np.zeros(n_seg - len(seg))])
    h, w = int(d.HOP * d.SR), int(d.WIN * d.SR)
    n = (len(seg) - w) // h
    idx = np.arange(w)[None, :] + h * np.arange(n)[:, None]
    fr = seg[idx] * np.hanning(w)[None, :]
    spec = np.abs(np.fft.rfft(fr, d.NFFT, axis=1))
    rms = np.sqrt(np.mean(fr ** 2, axis=1) * 8 / 3)
    t = (np.arange(n) * h + w / 2) / d.SR - 0.005
    gate = (t >= 0) & (t < d.GATE_S)
    f0 = d.fundamental(seg[int(0.005 * d.SR):int((0.005 + d.GATE_S) * d.SR)])
    peak, stand = d.resonant_peak(spec, f0)
    peak[(stand < d.PEAK_DB) | (t < 0) | (t > d.GATE_S)] = np.nan
    bands = (spec ** 2) @ BM.T
    return dict(t=t, rms=rms.astype(np.float32), bands=bands.astype(np.float32), peak=peak,
                level=10 * np.log10(np.mean(rms[gate] ** 2) + 1e-20))


def note_pair(path):
    x = d.load(path)
    t1 = d.onset_near(x, d.FIRST * d.LINE, 0.05, 0.2)
    t2 = d.onset_near(x, t1 + (d.SECOND - d.FIRST) * d.LINE, 0.02, 0.05)
    return features(x, t1), features(x, t2)


def real_note(args):
    s, k = args
    x, on = d.real_set(s)
    return (s, k), features(x, on[k])


def real_features(jobs):
    cache = os.path.join(FIT, "real.pkl")
    if os.path.exists(cache):
        return pickle.load(open(cache, "rb"))
    os.makedirs(FIT, exist_ok=True)
    keys = sorted({(s, k) for _, _, s, k in d.takes()})
    with ProcessPoolExecutor(jobs) as ex:
        real = dict(ex.map(real_note, keys, chunksize=8))
    pickle.dump(real, open(cache, "wb"))
    return real


def note_score(ref, test, gain):
    """Band LSD (dB), level error (dB) and resonant-peak error (cents) of one note."""
    loud = (ref["t"] >= 0) & (ref["t"] < d.GATE_S + 0.3) & (ref["rms"] > ref["rms"].max() * 10 ** (-40 / 20))
    g = 10 ** (-(test["level"] - ref["level"]) / 10)
    lr = 10 * np.log10(ref["bands"][loud] + 1e-30)
    lt = 10 * np.log10(test["bands"][loud] * g + 1e-30)
    floor = lr.max() - 60  # the recording's hiss sits ~60-75 dB under a note's loudest band
    lsd = float(np.sqrt(np.mean((np.maximum(lr, floor) - np.maximum(lt, floor)) ** 2)))
    both = np.isfinite(ref["peak"]) & np.isfinite(test["peak"])
    peak = float(np.mean(1200 * np.log2(test["peak"][both] / ref["peak"][both]))) if both.sum() >= 5 else np.nan
    return lsd, test["level"] - gain - ref["level"], peak


def key(cal):
    return hashlib.sha1(json.dumps(cal, sort_keys=True).encode()).hexdigest()[:12]


def render(cal, jobs):
    out = os.path.join(FIT, key(cal))
    names = [s["name"] for s in json.load(open(d.SPECS))]
    todo = [n for n in names if not os.path.exists(os.path.join(out, n + ".wav"))]
    if todo:
        os.makedirs(out, exist_ok=True)
        json.dump(cal, open(os.path.join(out, "cal.json"), "w"))
        extra = sum((["--cal", f"{k}={v:.6g}"] for k, v in cal.items()), [])
        chunks = [todo[i::jobs] for i in range(jobs)]
        procs = [subprocess.Popen([d.TB303RENDER, d.SPECS, "--out", out, "--only", ",".join(c), *extra],
                                  stdout=subprocess.DEVNULL) for c in chunks if c]
        for p in procs:
            if p.wait() != 0:
                raise RuntimeError("tb303render failed")
    return out, names


_done = {}


def evaluate(cal, real, jobs, detail=False, keep=False):
    if not detail and key(cal) in _done:
        return _done[key(cal)]
    out, names = render(cal, jobs)
    with ProcessPoolExecutor(jobs) as ex:
        pairs = dict(zip(names, ex.map(note_pair, [os.path.join(out, n + ".wav") for n in names], chunksize=4)))
    if not keep:
        shutil.rmtree(out)  # ~260 MB a set
    takes = d.takes()
    gain = float(np.median([pairs[n][int(acc)]["level"] - real[(s, k)]["level"] for n, acc, s, k in takes]))
    rows = []
    for n, acc, s, k in takes:
        lsd, dl, pk = note_score(real[(s, k)], pairs[n][int(acc)], gain)
        rows.append((n, acc, s, k, lsd, dl, pk))
    lsd = np.mean([r[4] for r in rows]); lev = np.mean([abs(r[5]) for r in rows])
    pk = np.nanmean([abs(r[6]) for r in rows])
    score = lsd + lev + pk / 200
    res = dict(cal=cal, score=float(score), lsd=float(lsd), level=float(lev), peak=float(pk), gain=gain)
    _done[key(cal)] = res
    if detail:
        for g in "ABCDE":
            sel = [r for r in rows if r[2][0] == g]
            print(f"  others {g}: lsd {np.mean([r[4] for r in sel]):4.2f} dB  |level| {np.mean([abs(r[5]) for r in sel]):4.2f} dB  "
                  f"peak {np.nanmean([r[6] for r in sel]):+5.0f} c (|{np.nanmean([abs(r[6]) for r in sel]):4.0f}|)")
    return res


def log(res):
    with open(os.path.join(FIT, "log.jsonl"), "a") as f:
        f.write(json.dumps(res) + "\n")
    c = " ".join(f"{k}={v:.4g}" for k, v in res["cal"].items())
    print(f"score {res['score']:6.3f}  lsd {res['lsd']:5.2f}  level {res['level']:4.2f}  peak {res['peak']:5.0f} c   {c}", flush=True)


def parse_kv(items):
    out = {}
    for it in items or []:
        k, v = it.split("=")
        if k not in PARAMS:
            raise SystemExit(f"unknown parameter {k}")
        out[k] = float(v)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--start", nargs="*")
    ap.add_argument("--eval", nargs="*")
    ap.add_argument("--fix", default="")
    ap.add_argument("--rounds", type=int, default=5)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()
    real = real_features(a.jobs)
    log_path = os.path.join(FIT, "log.jsonl")
    if os.path.exists(log_path):  # parameter sets already scored
        for line in open(log_path):
            r = json.loads(line)
            _done[key(r["cal"])] = r
    cal = {k: v[0] for k, v in PARAMS.items()}
    if a.eval is not None:
        cal.update(parse_kv(a.eval))
        res = evaluate(cal, real, a.jobs, detail=True)
        log(res)
        return
    cal.update(parse_kv(a.start))
    free = [k for k in PARAMS if k not in a.fix.split(",")]
    steps = {k: PARAMS[k][3] for k in free}
    best = evaluate(cal, real, a.jobs)
    log(best)
    for rnd in range(a.rounds):
        improved = False
        for k in free:
            lo, hi = PARAMS[k][1], PARAMS[k][2]
            for direction in (1, -1):
                trial = dict(best["cal"])
                v = trial[k] * np.exp(direction * steps[k]) if PARAMS[k][4] else trial[k] + direction * steps[k]
                trial[k] = float(np.clip(v, lo, hi))
                if trial[k] == best["cal"][k]:
                    continue
                res = evaluate(trial, real, a.jobs)
                log(res)
                if res["score"] < best["score"] - 1e-3:
                    best, improved = res, True
                    # keep going the same way while it helps
                    while True:
                        t2 = dict(best["cal"])
                        v = t2[k] * np.exp(direction * steps[k]) if PARAMS[k][4] else t2[k] + direction * steps[k]
                        t2[k] = float(np.clip(v, lo, hi))
                        if t2[k] == best["cal"][k]:
                            break
                        r2 = evaluate(t2, real, a.jobs)
                        log(r2)
                        if r2["score"] < best["score"] - 1e-3:
                            best = r2
                        else:
                            break
                    break
        print(f"--- round {rnd + 1}: best {best['score']:.3f}", flush=True)
        if not improved:
            steps = {k: s / 2 for k, s in steps.items()}
    print("best:", json.dumps(best["cal"]))
    evaluate(best["cal"], real, a.jobs, detail=True)


if __name__ == "__main__":
    main()
