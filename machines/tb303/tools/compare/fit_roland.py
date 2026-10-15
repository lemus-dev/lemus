#!/usr/bin/env python3
"""Fits the voice's unit calibration (TM3 ring, Q21 beta and tail current) to Roland's TB-303
software at high resonance: resonant-peak heights at RES 50/100 % across the cutoff range,
the ring frequency of the service-notes VCF test across the range, and the log-spectral
distance of the static-filter tests. Calibrations that are not stable over a long run are
rejected: with too little C25 coupling loss (high Q21 beta) the resonance loop keeps gain at a
few Hz, where its four high-passes turn the feedback positive, and a slow oscillation builds up
over several seconds at high resonance and modulates the filter (specs/stability.json: eight
loops of an acid line; the energy under 12 Hz after the first loop must stay 30 dB under the
total).

Usage: fit_roland.py [--quick]   (run with the analysis venv's python). The result is kRolandCal
in plugin/PluginProcessor.cpp (ring as a period: 1000 / vcfRingHz ms).
"""
import itertools, json, math, os, subprocess, sys, tempfile
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../.."))
RENDER = os.path.join(ROOT, "../../build/cmake/tb303render_artefacts/Release/tb303render")
REF = os.path.join(ROOT, "private/reference/roland-vst/renders")
sys.path.insert(0, HERE)
import compare as C
from peaks import harmonics

CUTS = ("000", "025", "050", "075", "100")


def peak(path):
    a = harmonics(path)
    w = a * np.arange(1, len(a) + 1)
    k = int(np.argmax(w[1:])) + 1
    return 20 * math.log10(w[k] / w[0]), 110 * (k + 1)


def ring_hz(path):
    import soundfile as sf
    from scipy import signal
    x, sr = sf.read(path, always_2d=True); x = x.mean(1)
    seg = x[int(0.3 * sr):int(0.9 * sr)]
    b, a = signal.butter(4, 200 / (sr / 2), 'high')
    y = signal.filtfilt(b, a, seg)
    zc = np.where(np.diff(np.signbit(y)))[0]
    p = np.diff(zc[::2]) / sr
    p = p[(p > 1 / 5000) & (p < 1 / 150)]
    return 1 / np.median(p) if len(p) else float("nan")


def score(dirpath, verbose=False):
    err_pk, err_ring, lsd = [], [], []
    rows = []
    for c in CUTS:
        for r in ("050", "100"):
            name = f"static-saw-c{c}-r{r}.wav"
            pr, fr = peak(os.path.join(REF, name))
            po, fo = peak(os.path.join(dirpath, name))
            err_pk.append(po - pr)
            lsd.append(C.compare(name[:-4], os.path.join(REF, name), os.path.join(dirpath, name), dirpath, plot=False)["lsd"])
            rows.append(f"c{c} r{r}: peak {po:+5.1f} vs {pr:+5.1f} dB")
        ring_name = "cal-ring.wav" if c == "050" else f"cal-ring-c{c}.wav"
        rr, ro = ring_hz(os.path.join(REF, ring_name)), ring_hz(os.path.join(dirpath, ring_name))
        err_ring.append(1200 * math.log2(ro / rr))
        rows.append(f"c{c} ring {ro:6.0f} vs {rr:6.0f} Hz")
    s = math.sqrt(np.mean(np.square(err_pk))) + math.sqrt(np.mean(np.square(err_ring))) / 100 + np.mean(lsd)
    if verbose:
        print("\n".join(rows))
    return s, math.sqrt(np.mean(np.square(err_pk))), math.sqrt(np.mean(np.square(err_ring))), float(np.mean(lsd))


def stable(dirpath, verbose=False, limit=-30.0):
    """True when every stability run stays free of the slow loop oscillation (~6 Hz): the
    energy under 12 Hz after the first loop, relative to the total, stays under `limit` dB.
    The lowest note in the runs is C1 (32.7 Hz), so nothing musical lands there."""
    import soundfile as sf
    from scipy import signal
    ok = True
    for s in json.load(open(os.path.join(HERE, "specs/stability.json"))):
        x, sr = sf.read(os.path.join(dirpath, s["name"] + ".wav"))
        loop = int(sr * 60 / s["bpm"] * 32 / s["lpb"])
        f, p = signal.welch(x[loop:], sr, nperseg=1 << 16)
        ratio = 10 * math.log10(p[f < 12].sum() / p.sum())
        ok = ok and ratio < limit
        if verbose:
            print(f"{s['name']}: under 12 Hz {ratio:+.1f} dB")
    return ok


def render(cal, out):
    os.makedirs(out, exist_ok=True)
    args = [RENDER, os.path.join(HERE, "specs/suite.json"), os.path.join(HERE, "specs/calibration.json"),
            os.path.join(HERE, "specs/stability.json"), "--out", out, "--only", "static-saw,cal-ring,stab-"]
    for k, v in cal.items():
        args += ["--cal", f"{k}={v}"]
    subprocess.run(args, check=True, capture_output=True)


def main():
    quick = "--quick" in sys.argv
    # Within the parts: Q21 is a 2SC1583 F (h_FE 250-500 at 1 mA, about 0.87 of that at its
    # 0.26 mA per side: 220-435); its tail is set by 5 % resistors (0.52 mA nominal); TM3 is the
    # unit's trim.
    rings = [400] if quick else [395, 405, 415, 425]
    betas = [220, 435] if quick else [220, 300, 360, 435]
    tails = [0.48, 0.56] if quick else [0.48, 0.52, 0.56]
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="fit303-", dir=os.path.join(ROOT, "build"))
    combos = list(itertools.product(rings, betas, tails))

    dirs = [os.path.join(tmp, f"r{ring}-b{beta}-t{tail}") for ring, beta, tail in combos]

    # Render in threads (each a process), score in processes (the analysis holds the GIL).
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
        list(ex.map(lambda cd: render({"vcfRingHz": cd[0][0], "q21Beta": cd[0][1], "q21TailmA": cd[0][2]}, cd[1]),
                    zip(combos, dirs)))
    with ProcessPoolExecutor(max_workers=os.cpu_count()) as ex:
        results = list(zip(combos, ex.map(score, dirs), ex.map(stable, dirs)))
    unstable = [cb for cb, _, ok in results if not ok]
    print("unstable:", " ".join(f"r{r}-b{b}-t{t}" for r, b, t in unstable) or "none")
    results = [(cb, sc) for cb, sc, ok in results if ok]
    results.sort(key=lambda r: r[1][0])
    print("ring  beta  tail   score  peakRMS  ringRMS(c)  LSD")
    for (ring, beta, tail), (s, pk, rg, l) in results[:12]:
        print(f"{ring:4d} {beta:5d} {tail:5.2f}  {s:6.2f}  {pk:6.2f}  {rg:8.1f}  {l:5.2f}")
    best = results[0][0]
    print("best", best)
    score(os.path.join(tmp, f"r{best[0]}-b{best[1]}-t{best[2]}"), verbose=True)
    stable(os.path.join(tmp, f"r{best[0]}-b{best[1]}-t{best[2]}"), verbose=True)
    json.dump({"vcfRingHz": best[0], "q21Beta": best[1], "q21TailmA": best[2]}, open(os.path.join(tmp, "best.json"), "w"))
    print(tmp)


if __name__ == "__main__":
    main()
