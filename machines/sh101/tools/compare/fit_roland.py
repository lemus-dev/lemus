#!/usr/bin/env python3
"""Fits calibration fields of the voice to Roland's SH-101 plug-in by grid search: renders the
fit subset of the specs through sh101render for every combination (--cal NAME=VALUE, the fields
sh101::Calibration::set accepts), scores each against Roland's renders, and rejects any
combination that is unstable over the long runs in specs/stability.json.

Usage: fit_roland.py NAME=V1,V2,... [NAME=...] [--only GROUP,...] [--base service|real|roland]
                     [--render PATH] [--keep]
  --only    score only these groups (default all): pitch freq res selfosc kybd envdepth envtime
            porta lsd
  --base    the calibration the grid starts from (default roland)
  --render  sh101render binary (default: build/sh101/..., else build/sh101-ref/...)
Prints the best combinations, their per-group errors and the stability verdicts. The result
belongs in Calibration::roland() (core/osc.cpp, core/filter.cpp) and docs/model.md.

Score: the RMS of each group's errors, summed with these weights (units in brackets):
  pitch    A4 and the keyboard's tracking (cents / 10)
  freq     the FREQ law, self-oscillation frequency at slider 1..10 (cents / 100)
  res      resonant-peak heights at FREQ 6-8, RES 5 / 7.5 (dB / 2)
  selfosc  self-oscillation level re the saw at 10 (dB / 3)
  kybd     tracking slope at KYBD 5 and 10 (oct/oct x 10)
  envdepth ENV depth in octaves at 2.5-10 (oct)
  envtime  attack t10-90 and decay / release tau, as log ratios (x 3)
  porta    glide tau as log ratio (x 3)
  lsd      mean LSD of the osc, vcf and env specs (dB / 2)
Groups that Roland gets wrong against the hardware (docs/roland-measurements.md, "What this means
for the fit") should be left out with --only when fitting the service-notes side.
"""
import itertools, json, math, os, subprocess, sys, tempfile
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../.."))
REPO = os.path.normpath(os.path.join(ROOT, "../.."))
REF = os.path.join(ROOT, "private/reference/roland-vst/renders")
sys.path.insert(0, HERE)
import analyse as A
import compare as C
import measure as M

FIT_PREFIXES = ("osc-saw-8-", "osc-saw-8-A4", "selfosc-f", "selfosc-kybd", "selfosc-env", "vcf-saw-f06", "vcf-saw-f07",
                "vcf-saw-f08", "env-attack", "env-decay", "env-release", "porta-on", "mix-saw-100", "osc-pulse-pw000",
                "osc-sub-2oct", "cal-")
GROUPS = ("pitch", "freq", "res", "selfosc", "kybd", "envdepth", "envtime", "porta", "lsd")
WEIGHTS = {"pitch": 0.1, "freq": 0.01, "res": 0.5, "selfosc": 1 / 3, "kybd": 10, "envdepth": 1, "envtime": 3,
           "porta": 3, "lsd": 0.5}


def renderer(path=None):
    for p in [path] if path else [os.path.join(REPO, "build/sh101/sh101render_artefacts/Release/sh101render"),
                                  os.path.join(REPO, "build/sh101-ref/sh101render_artefacts/Release/sh101render")]:
        if p and os.path.exists(p):
            return p
    raise SystemExit("sh101render not built")


def render(binary, cal, base, out):
    os.makedirs(out, exist_ok=True)
    args = [binary, os.path.join(HERE, "specs/suite.json"), os.path.join(HERE, "specs/calibration.json"),
            os.path.join(HERE, "specs/stability.json"), "--out", out, "--calibration", base,
            "--only", ",".join(FIT_PREFIXES + ("stab-",))]
    for k, v in cal.items():
        args += ["--cal", f"{k}={v}"]
    subprocess.run(args, check=True, capture_output=True)


def rms(v):
    v = [x for x in v if x is not None and np.isfinite(x)]
    return math.sqrt(np.mean(np.square(v))) if v else float("nan")


def errors(d, ref, groups):
    """Per-group RMS error of renders in d against measurements `ref` of Roland's."""
    out = {}
    if "pitch" in groups:
        p = M.g_pitch(d)
        e = [A.cents(p["A4_hz"], ref["pitch"]["A4_hz"])] if p.get("A4_hz") else []
        e += [p[k]["cents_vs_ET"] - ref["pitch"][k]["cents_vs_ET"] for k in p if isinstance(p[k], dict) and k in ref["pitch"]]
        out["pitch"] = rms(e)
    if "freq" in groups or "selfosc" in groups:
        f = M.g_freq(d)
        sl = [k for k in f if k.startswith("slider") and k != "slider0"]
        if "freq" in groups:
            out["freq"] = rms([A.cents(f[k]["hz"], ref["freq"][k]["hz"]) for k in sl if f[k].get("hz") and ref["freq"][k].get("hz")])
        if "selfosc" in groups:
            ours, theirs = M.level_db(d, "mix-saw-100"), M.level_db(REF, "mix-saw-100")
            out["selfosc"] = rms([(f[k]["dbfs"] - ours) - (ref["freq"][k]["dbfs"] - theirs) for k in sl
                                  if f[k].get("dbfs") is not None and ours is not None])
    if "res" in groups:
        r = M.g_res(d)
        out["res"] = rms([r[k]["peak_db"] - ref["res"][k]["peak_db"] for k in r
                          if isinstance(r[k], dict) and any(f"-f0{f}-r{x}" in k for f in "678" for x in ("050", "075"))])
    if "kybd" in groups:
        k = M.g_kybd(d)
        out["kybd"] = rms([k[x]["oct_per_oct"] - ref["kybd"][x]["oct_per_oct"] for x in ("kybd050", "kybd100") if x in k])
    if "envdepth" in groups:
        e = M.g_envdepth(d)
        out["envdepth"] = rms([e[x]["octaves"] - ref["envdepth"][x]["octaves"] for x in e if x.startswith("env0") or x == "env100"
                               if isinstance(e[x], dict) and x in ref["envdepth"]])
    if "envtime" in groups:
        e = M.g_env(d)
        v = []
        for k, x in e.items():
            key = "t10_90" if "attack" in k else "tau"
            if k.endswith(("000",)) or key not in x or x[key] is None or ref["env"][k].get(key) is None:
                continue
            v.append(math.log(x[key] / ref["env"][k][key]))
        out["envtime"] = rms(v)
    if "porta" in groups:
        p = M.g_porta(d)
        out["porta"] = rms([math.log(p[k]["tau"] / ref["porta"][k]["tau"]) for k in p
                            if k.startswith("porta-on") and p[k].get("tau") and ref["porta"][k].get("tau")])
    if "lsd" in groups:
        v = []
        for name, s in A.specs("suite").items():
            if s.get("ours_only") or not name.startswith(("osc-", "vcf-", "env-")):
                continue
            rp, tp = os.path.join(REF, name + ".wav"), os.path.join(d, name + ".wav")
            if os.path.exists(rp) and os.path.exists(tp):
                v.append(C.compare(name, rp, tp, d, plot=False)["lsd"])
        v = [x for x in v if np.isfinite(x)]
        out["lsd"] = float(np.mean(v)) if v else float("nan")
    return out


def stable(d, limit=-30.0):
    """Every stability run stays bounded and free of slow build-ups: after the first loop, the
    energy under 12 Hz stays `limit` dB under the total (unless the spec says "check_low": false)
    and the level doesn't grow by more than 6 dB from the second loop to the last."""
    import soundfile as sf
    from scipy import signal
    verdicts = {}
    for s in json.load(open(os.path.join(HERE, "specs/stability.json"))):
        p = os.path.join(d, s["name"] + ".wav")
        if not os.path.exists(p):
            continue
        x, sr = sf.read(p)
        if not np.all(np.isfinite(x)) or np.abs(x).max() > 4:
            verdicts[s["name"]] = "blew up"
            continue
        loop = int(sr * 60 / s.get("bpm", 120) * s.get("loop_lines", 16) / s.get("lpb", 4))
        y = x[loop:]
        f, pw = signal.welch(y, sr, nperseg=1 << 15)
        low = 10 * math.log10(pw[f < 12].sum() / max(pw.sum(), 1e-30) + 1e-30)
        second = A.rms(x[loop:2 * loop]); last = A.rms(x[-loop:])
        grow = 20 * math.log10(max(last, 1e-9) / max(second, 1e-9))
        ok = (low < limit or not s.get("check_low", True)) and grow < 6
        verdicts[s["name"]] = "ok" if ok else f"under 12 Hz {low:+.1f} dB, growth {grow:+.1f} dB"
    return verdicts


def score_dir(args):
    d, ref, groups = args
    e = errors(d, ref, groups)
    total = sum(WEIGHTS[g] * v for g, v in e.items() if np.isfinite(v))
    return total, e, stable(d)


def main():
    argv = sys.argv[1:]
    opts = {}
    for flag in ("--only", "--base", "--render"):
        if flag in argv:
            i = argv.index(flag); opts[flag] = argv[i + 1]; del argv[i:i + 2]
    keep = "--keep" in argv
    argv = [a for a in argv if a != "--keep"]
    grid = {}
    for a in argv:
        k, vs = a.split("=", 1)
        grid[k] = [float(v) for v in vs.split(",")]
    if not grid:
        raise SystemExit(__doc__)
    groups = opts.get("--only", ",".join(GROUPS)).split(",")
    binary = renderer(opts.get("--render"))
    ref_json = os.path.join(ROOT, "private/reference/roland-vst/measurements.json")
    ref = json.load(open(ref_json)) if os.path.exists(ref_json) else M.measure(REF, list(M.GROUPS))
    names = list(grid)
    combos = list(itertools.product(*(grid[n] for n in names)))
    os.makedirs(os.path.join(REPO, "build"), exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="fit101-", dir=os.path.join(REPO, "build"))
    dirs = [os.path.join(tmp, "-".join(f"{n}{v:g}" for n, v in zip(names, c))) for c in combos]
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
        list(ex.map(lambda cd: render(binary, dict(zip(names, cd[0])), opts.get("--base", "roland"), cd[1]),
                    zip(combos, dirs)))
    with ProcessPoolExecutor() as ex:
        results = list(zip(combos, ex.map(score_dir, [(d, ref, groups) for d in dirs])))
    results.sort(key=lambda r: r[1][0])
    for combo, (total, e, verdicts) in results[:12]:
        bad = {k: v for k, v in verdicts.items() if v != "ok"}
        print(" ".join(f"{n}={v:g}" for n, v in zip(names, combo)), f" score {total:7.3f} ",
              " ".join(f"{g} {v:.3g}" for g, v in e.items()), " UNSTABLE " + str(bad) if bad else "")
    best = next((r for r in results if all(v == "ok" for v in r[1][2].values())), None)
    if best:
        print("best stable:", dict(zip(names, best[0])))
        json.dump(dict(zip(names, best[0])), open(os.path.join(tmp, "best.json"), "w"))
    else:
        print("no stable combination")
    print(tmp if keep else "")
    if not keep:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
