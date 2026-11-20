#!/usr/bin/env python3
"""Render every suite signal through every NAM capture (the reference), with NAM's own
renderer at the model's full size. Output: build/nam_renders/<capture>/<model>__<signal>.wav.
Existing renders are kept.

Usage: tools/compare/nam_render.py [CAPTURE ...]     (repo venv, ../../.venv; default: all captures)
"""
import concurrent.futures as cf
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
NAM = ROOT / "private/third_party/NeuralAmpModelerCore/build/cmake/tools/render"
OUT = ROOT / "build/nam_renders"


def out_path(spec, signal):
    stem = pathlib.Path(spec["model"]).stem
    return OUT / spec["capture"] / f"{stem}__{signal}.wav"


def render(job):
    spec, sig = job
    dst = out_path(spec, sig["name"])
    if dst.exists():
        return 0
    dst.parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run([str(NAM), "--slim", "1.0", str(ROOT / spec["model"]), str(ROOT / sig["file"]), str(dst)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        # Not every model is slimmable: retry without --slim.
        r = subprocess.run([str(NAM), str(ROOT / spec["model"]), str(ROOT / sig["file"]), str(dst)],
                           capture_output=True, text=True)
    if r.returncode != 0:
        print("FAILED", dst, r.stderr[-500:])
        return 1
    return 0


def main():
    suite = json.loads((ROOT / "tools/compare/specs/suite.json").read_text())
    want = set(sys.argv[1:])
    jobs = [(c, s) for c in suite["captures"] if not want or c["capture"] in want for s in suite["signals"]]
    with cf.ProcessPoolExecutor() as ex:
        fails = sum(ex.map(render, jobs, chunksize=4))
    print(f"{len(jobs)} renders, {fails} failed")


if __name__ == "__main__":
    main()
