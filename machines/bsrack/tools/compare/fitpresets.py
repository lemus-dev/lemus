#!/usr/bin/env python3
"""Fits the voice's fitted values ([F]) to a real unit playing the 40 factory programs.

    fitpresets.py DATA.json AUDIO [--free NAME,...] [--start NAME=VALUE ...] [--rounds N]
                  [--programs 0-39] [--anchor top|none] [--jobs 8] [--report]

DATA.json describes the recording (private/bsrack/reference/youtube/zibbybone.json): the MIDI pattern
played into the unit (loop length, grid offset, 16th steps, notes), and when the display stepped to
each program. AUDIO is its sound. For each program, ours renders the same pattern in the same phase,
steps to the program with + at the same place (bsrackrender), and is cut to the same window.

Score per program: the RMS of the 1/3-octave band shares' differences (ours - real, each band as dB
of its window's total, 50 Hz - 16 kHz, each floored at -45 dB: below that the video's AAC drops or
smears it); the fit's score is their mean. A
coordinate search in log steps (x1.5, x1.2, x1.08) over the free values. --report prints each
program's score and band differences at the start values and stops.

--anchor top keeps the VCF converter's top at 13.0 kHz (fitdemo.py's anchor) and fits its divider;
--anchor none fits divider and reference both.
"""
import argparse, concurrent.futures, json, os, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "../../../.."))
sys.path.insert(0, HERE)
import fitdemo  # noqa: E402  the converter arithmetic and the anchors

SR = 44100
EDGES = 50 * 2 ** (np.arange(0, 25) / 3)  # 50 Hz - 16 kHz
FLOOR = -45.0  # dB of the window's total


def load(path):
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(out, dtype=np.float32).astype(float)


def shares(x):
    n = 4096
    if len(x) < n:
        return None
    F = np.lib.stride_tricks.sliding_window_view(x, n)[::1024] * np.hanning(n)
    P = (np.abs(np.fft.rfft(F, axis=1)) ** 2).mean(0)
    f = np.fft.rfftfreq(n, 1 / SR)
    tot = P[(f >= EDGES[0]) & (f < EDGES[-1])].sum() + 1e-30
    return np.array([10 * np.log10(P[(f >= lo) & (f < hi)].sum() / tot + 1e-12) for lo, hi in zip(EDGES[:-1], EDGES[1:])])


class Presets:
    def __init__(self, data, audio, programs, render, rom, jobs):
        d = json.load(open(data))
        pat = d["pattern"]
        self.loop, self.off, self.shift = pat["loop_s"], pat["grid_offset_s"], pat["render_shift_s"]
        self.step = self.loop / 32
        self.steps, self.notes, self.length, self.vel = pat["steps"], pat["notes"], pat["length"], pat["velocity"]
        self.ch = {int(k): v for k, v in d["changes_s"].items()}
        self.programs, self.render, self.rom, self.jobs = programs, render, rom, jobs
        real = load(audio)
        self.windows, self.real = {}, {}
        for p in programs:
            a = self.ch[p] + (0.0 if p == 0 else 0.4)
            b = self.ch[p + 1] - 0.1
            self.windows[p] = (a, b)
            self.real[p] = shares(real[int(a * SR):int(b * SR)])

    def render_one(self, p, parts, tmp):
        tc, (a, b) = self.ch[p], self.windows[p]
        v0 = self.off + np.floor((tc - 2 * self.loop - self.off) / self.loop) * self.loop
        t0 = 2.0
        dur = t0 + (b - v0) + 0.5
        prev = max(p - 1, 0)
        path = os.path.join(tmp, "p%02d.wav" % p)
        args = [self.render, self.rom, path, "%.2f" % dur, "--rate=%d" % SR, "--press=key%d@400" % (prev // 10), "--press=key%d@700" % (prev % 10)]
        args += ["--part=%s=%g" % kv for kv in parts.items()]
        k = 0
        while v0 + k * self.loop < b:
            base = v0 + k * self.loop
            for i, (st, nt) in enumerate(zip(self.steps, self.notes)):
                nxt = self.steps[i + 1] if i + 1 < len(self.steps) else 32 + self.steps[0]
                ours = base + st * self.step - v0 + t0 + self.shift
                if ours < dur - 0.05:
                    args.append("--note=%d:%d:%d:%d" % (round(ours * 1000), nt, self.vel, round(self.length * (nxt - st) * self.step * 1000)))
            k += 1
        if p > 0:
            args.append("--press=key_plus@%d" % round((tc - v0 + t0) * 1000))
        subprocess.run(args, capture_output=True, check=True)
        with open(path, "rb") as fh:
            x = np.frombuffer(fh.read()[44:], dtype=np.float32).astype(float)
        return shares(x[int((a - v0 + t0) * SR):int((b - v0 + t0) * SR)])

    def score(self, parts, detail=False):
        with tempfile.TemporaryDirectory(prefix="bspre") as tmp:
            with concurrent.futures.ThreadPoolExecutor(self.jobs) as ex:
                ours = dict(zip(self.programs, ex.map(lambda p: self.render_one(p, parts, tmp), self.programs)))
        per = {}
        for p in self.programs:
            if ours[p] is None or self.real[p] is None:
                continue
            # bands under -45 dB of the window (in both) don't count: the recording's AAC drops or smears
            # what is that far down, and nobody hears it under the note
            d = np.maximum(ours[p], FLOOR) - np.maximum(self.real[p], FLOOR)
            per[p] = (float(np.sqrt((d ** 2).mean())), d)
        total = float(np.mean([v[0] for v in per.values()]))
        return (total, per) if detail else total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("data")
    ap.add_argument("audio")
    ap.add_argument("--render", default=os.path.join(REPO, "../build/bsrack/bsrackrender"))
    ap.add_argument("--rom", default=os.path.join(REPO, "../private/roms/bsrack/v1.5.bin"))
    ap.add_argument("--free", default="vcfRgnd,fmaBeta,c33,r58")
    ap.add_argument("--start", nargs="*", default=[])
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--programs", default="0-39")
    ap.add_argument("--anchor", choices=["top", "none"], default="top")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    lo, hi = (int(v) for v in a.programs.split("-"))
    ps = Presets(a.data, a.audio, list(range(lo, hi + 1)), a.render, a.rom, a.jobs)
    parts = {}
    for s in a.start:
        k, v = s.split("=")
        parts[k] = float(v)
    free = [k for k in a.free.split(",") if k]
    defaults = dict(vcfRgnd=2.98e3, vcfRref=603e3, fmaBeta=952, c33=43e-12, r58=15e3, r34=207, r35=44.1e3, abcDrop=0.55, resFloor=1e-7, vcfFloor=6e-6)
    for k in free:
        parts.setdefault(k, defaults[k])

    def full(pp):
        pp = dict(pp)
        if a.anchor == "top":
            pp["vcfRref"] = fitdemo.top_rref(pp.get("vcfRgnd", 2.98e3), pp.get("fmaBeta", 952))
        return pp

    best, per = ps.score(full(parts), detail=True)
    print("start %.3f  %s" % (best, " ".join("%s=%g" % kv for kv in full(parts).items())), flush=True)
    if a.report:
        for p, (s, d) in sorted(per.items()):
            print("%02d %5.2f  %s" % (p, s, " ".join("%+5.1f" % v for v in d)))
        return
    for step in [1.5, 1.2, 1.08][: a.rounds]:
        improved = True
        while improved:
            improved = False
            for k in free:
                for mul in (step, 1 / step):
                    trial = dict(parts)
                    trial[k] = parts[k] * mul
                    s = ps.score(full(trial))
                    if s < best - 1e-3:
                        best, parts, improved = s, trial, True
                        print("%-8s x%-6.3f %.3f" % (k, mul, best), flush=True)
                        break
    print("final %.3f" % best)
    print(" ".join("--part=%s=%g" % kv for kv in full(parts).items()))


if __name__ == "__main__":
    main()
