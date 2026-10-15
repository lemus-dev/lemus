#!/usr/bin/env python3
"""Check the voice against a second real TB-303: the first note of acidvoice.com's accent-test clips
(private/reference/shootout, "303_60" and "303_100": CUT OFF 60 % and 100 %, RESONANCE 100 %).
The first note holds a steady resonant peak (no env mod), so its frequency tests the cutoff law and
the trim independently of Din Sync's unit. Renders the same D#2 through tb303render and compares
the resonant peak (median over 30-300 ms).

  acidvoice_check.py [--unit] [--cal NAME=VALUE ...]    (arguments go to tb303render)
"""
import io, json, os, subprocess, sys, tempfile
import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dinsync as d

CLIPS = os.path.normpath(os.path.join(d.HERE, "../../private/reference/shootout/roland_tb-303_real/clone_TB3"))
TESTS = [("303_60", 0.60), ("303_100", 1.00)]


def load_mp3(path):
    raw = subprocess.run(["ffmpeg", "-v", "quiet", "-i", path, "-f", "wav", "-ac", "1", "-ar", str(d.SR), "-"],
                         capture_output=True, check=True).stdout
    return sf.read(io.BytesIO(raw))[0]


def peak_hz(x, t0):
    a = int((t0 - 0.005) * d.SR)
    seg = x[a:a + int(0.35 * d.SR)]
    h, w = int(d.HOP * d.SR), int(d.WIN * d.SR)
    n = (len(seg) - w) // h
    fr = seg[np.arange(w)[None, :] + h * np.arange(n)[:, None]] * np.hanning(w)
    spec = np.abs(np.fft.rfft(fr, d.NFFT, axis=1))
    f0 = d.fundamental(x[int(t0 * d.SR):int((t0 + 0.1) * d.SR)])
    pk, stand = d.resonant_peak(spec, f0)
    t = (np.arange(n) * h + w / 2) / d.SR - 0.005
    m = (t > 0.03) & (t < 0.3) & (stand > d.PEAK_DB)
    return float(np.median(pk[m])), f0


def main():
    specs = [{"name": f"av-c{round(c * 100):03d}", "bpm": 120, "lpb": 4, "lines": 8,
              "params": {"waveform": 0, "tuning": 0.5, "cutoff": c, "resonance": 1.0, "envmod": 0.0,
                         "decay": 0.0, "accent": 1.0, "volume": 0.6},
              "notes": [{"line": 1, "note": "D#2", "len": 4}]} for _, c in TESTS]
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "specs.json")
        json.dump(specs, open(path, "w"))
        subprocess.run([d.TB303RENDER, path, "--out", tmp, *sys.argv[1:]], check=True, stdout=subprocess.DEVNULL)
        print(f"{'clip':8s} {'real':>8s} {'ours':>8s}  diff")
        for (clip, c), s in zip(TESTS, specs):
            x = load_mp3(os.path.join(CLIPS, clip + ".mp3"))
            real, f0 = peak_hz(x, d.onset_near(x, 0.0, 0.0, 0.2))
            y = d.load(os.path.join(tmp, s["name"] + ".wav"))
            ours, _ = peak_hz(y, d.onset_near(y, 0.25, 0.05, 0.2))
            print(f"{clip:8s} {real:7.0f}  {ours:7.0f}   {1200 * np.log2(ours / real):+5.0f} c   (CUT OFF {round(c * 100)} %, f0 {f0:.1f} Hz)")


if __name__ == "__main__":
    main()
