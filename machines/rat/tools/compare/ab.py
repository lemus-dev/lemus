#!/usr/bin/env python3
"""Listening A/B against a NAM capture of a real RAT.

Renders a suite signal (default: our TB-303 line) through our model and through a capture at
the same knob positions and level, gain-matches ours to the capture, and writes to
private/reference/ab/:
  <name>-ours.wav, <name>-nam.wav   the two renders
  <name>-ab.wav                     alternating 2 s segments, ours first, with a short tick
                                    before each switch (tick count: 1 = ours, 2 = capture)

Onyx Tracker can only host instruments, not effects, so the TB-303's ab_song.py approach
(track 1 ours, track 2 the reference plugin) is done here with samples instead: --onyx
imports the two renders into the Onyx library and loads them as instruments 01 and 02
(track 1 ours, track 2 capture; solo a track to compare).

Usage: ab.py [--capture SLUG] [--model SUBSTRING] [--signal NAME] [--cal captured] [--onyx]
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import compare  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "private/reference/ab"
OTK = pathlib.Path.home() / "src/onyx-tracker/build/Build/Products/Release/otk"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture", default="outmoded-rat2-11dbu")
    ap.add_argument("--model", default="D-12_00 F-12_00")
    ap.add_argument("--signal", default="tb303")
    ap.add_argument("--cal", default="captured")
    ap.add_argument("--onyx", action="store_true")
    a = ap.parse_args()
    suite = compare.load_suite()
    spec = next(c for c in suite["captures"] if c["capture"] == a.capture and a.model in c["name"])
    sig = next(s for s in suite["signals"] if s["name"] == a.signal)
    ref, rate = sf.read(compare.ref_path(spec, sig["name"]))
    d, f, v = compare.knobs_for(spec)
    with tempfile.TemporaryDirectory() as td:
        out = pathlib.Path(td) / "ours.wav"
        compare.render_ours(ROOT / sig["file"], out, d, f, v, compare.level_for(spec), a.cal)
        ours, _ = sf.read(out)
    m = compare.metrics(ref, ours, rate)
    ours, _ = compare.align(ref, ours)
    n = min(len(ref), len(ours))
    ref, ours = ref[:n], ours[:n] * 10 ** (m["gain_db"] / 20)
    peak = max(np.abs(ref).max(), np.abs(ours).max())
    ref, ours = 0.7 * ref / peak, 0.7 * ours / peak
    OUT.mkdir(parents=True, exist_ok=True)
    name = f"{a.capture}-{a.signal}"
    sf.write(OUT / f"{name}-ours.wav", ours.astype(np.float32), int(rate), subtype="FLOAT")
    sf.write(OUT / f"{name}-nam.wav", ref.astype(np.float32), int(rate), subtype="FLOAT")
    seg = int(2 * rate)
    tick = 0.25 * np.sin(2 * np.pi * 2000 * np.arange(int(0.01 * rate)) / rate)
    parts = []
    for k in range(4):
        src = ours if k % 2 == 0 else ref
        for _ in range(1 if k % 2 == 0 else 2):
            parts += [tick, np.zeros(int(0.05 * rate))]
        parts.append(src[: seg] if len(src) >= seg else src)
    sf.write(OUT / f"{name}-ab.wav", np.concatenate(parts).astype(np.float32), int(rate), subtype="FLOAT")
    print(f"{name}: LSD {m['lsd_db']:.2f} dB, ESR {m['esr_db']:.1f} dB, gain {m['gain_db']:+.2f} dB -> {OUT}")
    if a.onyx:
        for inst, which in (("01", "ours"), ("02", "nam")):
            path = OUT / f"{name}-{which}.wav"
            subprocess.run([str(OTK), "library", "import", str(path), "--name", f"rat-{name}-{which}"], check=True)
            subprocess.run([str(OTK), "sample", inst, f"rat-{name}-{which}", "--replace"], check=True)
        subprocess.run([str(OTK), "write", "0", "--line", "0", "000 | C-4 01 | C-4 02"], check=True)
        print("Onyx: instruments 01 (ours) and 02 (capture) on tracks 1 and 2; solo one to compare")


if __name__ == "__main__":
    main()
