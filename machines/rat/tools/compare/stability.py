#!/usr/bin/env python3
"""Long-run stability check (the TB-303's lesson: short renders miss slow instabilities).

Renders tools/compare/specs/stability.json's cases: long, hot, low-frequency signals and
silences at the knob extremes, with the given calibration overrides. Fails a case if the
output is not finite, if any Newton solve failed, if the output does not settle back to
silence (no sustained oscillation or DC creep after the signal stops), or if it runs away.

Usage: stability.py [--cal datasheet|captured] [--opamp lm308|op07] [--set NAME=VALUE ...]   (repo venv, ../../.venv)
Library: check(cal, sets, opamp) -> (ok, report)
"""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile

import numpy as np
import soundfile as sf

ROOT = pathlib.Path(__file__).resolve().parents[2]
RATRENDER = pathlib.Path(os.environ.get("RATRENDER", ROOT.parents[1] / "build/rat/ratrender"))  # RATRENDER: another build
RATE = 48000


def stimulus(kind, seconds, level_db):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    a = 10 ** (level_db / 20)
    if kind == "lowsine":
        x = a * np.sin(2 * np.pi * 30 * t)
    elif kind == "bursts":
        env = ((t % 1.0) < 0.5).astype(float)
        x = a * env * np.sin(2 * np.pi * 110 * t)
    elif kind == "chirp":
        x = a * np.sin(2 * np.pi * (20 * t + 0.5 * (8000 / seconds) * t * t))
    elif kind == "square":
        x = a * np.sign(np.sin(2 * np.pi * 55 * t))
    else:
        raise ValueError(kind)
    # Signal, then 3 s of silence to watch it settle.
    return np.concatenate([x, np.zeros(3 * RATE)])


def check(cal="datasheet", sets=(), opamp=None, parts=None):
    spec = json.loads((ROOT / "tools/compare/specs/stability.json").read_text())
    report, ok = [], True
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        for c in spec["cases"]:
            x = stimulus(c["signal"], c["seconds"], c["level_dbfs"])
            inp, out = td / "in.wav", td / "out.wav"
            sf.write(inp, x.astype(np.float32), RATE, subtype="FLOAT")
            cmd = [str(RATRENDER), str(inp), str(out), "--dist", str(c["dist"]), "--filter", str(c["filter"]),
                   "--volume", str(c["volume"]), "--level", str(c.get("level_dbu", 4.0)), "--cal", cal, "--stats"]
            if opamp:
                cmd += ["--opamp", opamp]
            if parts:
                cmd += ["--parts", parts]
            for k, v in sets:
                cmd += ["--set", f"{k}={v:.10g}"]
            r = subprocess.run(cmd, capture_output=True, text=True)
            y, _ = sf.read(out)
            fails = int(r.stderr.split("failures")[0].split(",")[-1].strip()) if "failures" in r.stderr else -1
            tail = y[-RATE:]
            tail_db = 20 * np.log10(np.sqrt(np.mean(tail ** 2)) + 1e-12)
            peak = float(np.max(np.abs(y)))
            good = np.all(np.isfinite(y)) and fails == 0 and tail_db < -80 and peak < 4.0
            ok &= bool(good)
            report.append(f"{c['name']:24s} {'ok ' if good else 'BAD'} fails={fails} peak={peak:.3f} tail={tail_db:.1f} dBFS")
    return ok, report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cal", default="datasheet")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--opamp", choices=["lm308", "op07"])
    a = ap.parse_args()
    sets = [(kv.split("=")[0], float(kv.split("=")[1])) for kv in a.set]
    ok, report = check(a.cal, sets, a.opamp)
    print("\n".join(report))
    print("STABLE" if ok else "UNSTABLE")
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
