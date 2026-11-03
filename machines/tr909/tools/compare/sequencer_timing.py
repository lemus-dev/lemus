#!/usr/bin/env python3
"""The sequencer timing the voice board sees, from the emulated CPU board, against a real 909.

Runs tr909seq (the v4.0 firmware) and measures the scan, the trigger pulses and groups, MIDI
note, MIDI clock and DIN clock latency, shuffle and flam; then the same shuffle and flam
intervals in the AudioRealism recordings of one real unit (private/reference/real909/packs/
audiorealism_tr909_ultimate: RimShot909-shuffle1-7-112BPM, <voice>-flam1-8), and compares.
docs/firmware.md ("Timing") has the results.

    tools/compare/sequencer_timing.py [--seq BUILD/tr909seq] [--only scan,shuffle,flam,...]

Recordings: each shuffle file is 32 rim shots, 16ths at about 113.8 BPM (two bars). Each flam
file is one flam: the grace note at the file's start, the main note 15-50 ms later. The interval
is read on the same feature of both hits: for BD the cross-correlation of the attack (the click
is the trigger's falling edge), for the toms the step the trigger puts on the output, for SD
the start of the snappy burst; for the toms and SD the feature is looked for near f+4 and f+5
group ticks (f = key - 9) and the stronger one taken.
"""
import argparse
import glob
import os
import random
import statistics
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "../tr909seq"))
sys.dont_write_bytecode = True
import seqrun  # noqa: E402
from seqrun import Run, intervals  # noqa: E402

PACK = os.path.normpath(os.path.join(HERE, "../../private/reference/real909/packs/audiorealism_tr909_ultimate/wav/TR-909-96kHz-24bit"))
ALL16 = " ".join(str(i) for i in range(1, 17))
WRITE = "run 1;SHIFT+PATT1;CLEAR+1;press START"
TEMPO_REC = "analog 0 4d"   # A/D 77: 113.73 BPM, the recordings' tempo (113.8 measured)
FLAM_KEYS = {"BD": (1, 2), "SD": (3, 4), "LT": (5, 6), "MT": (7, 8), "HT": (9, 10)}
FLAM_FILES = {"BD": "BassDrum/BassDrum909-Flam%d-*", "SD": "SnareDrum/Snare909-flam%d-*", "LT": "LowTom/LowTom-flam%d-*",
              "MT": "MidTom/MidTom-flam%d-*", "HT": "HiTom/HiTom-flam%d-*"}


def stats(v):
    return f"mean {statistics.mean(v):.2f} min {min(v):.2f} max {max(v):.2f} (n={len(v)})"


# ---------------------------------------------------------------- the emulated board

def scan():
    r = Run("log off;run 1;scan 10")
    print("## Scan (idle, 10 s)\n")
    print(r.text)
    r = Run("log off;midiout off;" + TEMPO_REC + ";" + WRITE + f";INST+2;p {ALL16};INST+10;p {ALL16};INST+15;p {ALL16};run 0.5;scan 10")
    print("## Scan (a pattern playing BD, HT and crash on every 16th, 10 s)\n")
    print(r.text)


def triggers():
    r = Run(TEMPO_REC + ";" + WRITE + f";INST+2;p {ALL16};INST+10;p {ALL16};INST+11;p {ALL16};run 0.5;log hits;run 30")
    w = r.widths()
    print("## Trigger pulses\n")
    print(f"pulse width (rise to fall of a trigger latch bit): {stats(w)} ms")
    steps = {}
    for t, n, c in r.hits():
        steps.setdefault(round(t / 131.9), {})[n] = t
    d = [s["HT"] - s["BD"] for s in steps.values() if "HT" in s and "BD" in s]
    print(f"group B (HT) - group A (BD) on one step: {stats(d)} ms; B after A on {sum(x > 0 for x in d) / len(d):.0%} of steps")
    print()


def midi_notes():
    random.seed(2)
    s = "run 1;slice 10;log hits"
    for i in range(1000):
        s += f";run {random.uniform(0.02, 0.06):.5f};echo n;midi 99 {0x24 if i % 2 else 0x2A:02X} 64"
    r = Run(s)
    lat, last = {"BD": [], "CH": []}, None
    for t, k, x in r.lines:
        if k == "ECHO":
            last = t
        elif k == "HIT" and last is not None:
            lat[x.split()[0]].append(t - last - 0.96)
            last = None
    print("## MIDI note-on to trigger (after the third byte's stop bit, 0.96 ms after the status byte starts)\n")
    for k, v in lat.items():
        print(f"{k}: {stats(v)} ms")
    print()


def midi_clock():
    print("## MIDI clock in (sync MIDI) to step trigger\n")
    for bpm in (90, 120, 160, 240):
        for sh in (1, 4):
            P = 60000.0 / bpm / 24
            s = (WRITE + f";INST+11;p {ALL16};SHUFFLE+{sh};press STOP;run 0.2;press PATT1;press 1;SHIFT+TAPESYNC;run 0.5;slice 10;"
                 f"log hits;echo s;midi FA;run {P / 1000:.6f}")
            for i in range(int(8000 / P)):
                s += f";echo c;midi F8;run {P / 1000:.6f}"
            r = Run(s)
            clocks = [t for t, k, x in r.lines if k == "ECHO" and x == "c"]
            start = [t for t, k, x in r.lines if k == "ECHO" and x == "s"][0]
            h = [t for t, n, c in r.hits()]
            on, off = [], []
            for k, t in enumerate(h):
                tick = 12 * k + ((sh - 1) if k % 2 else 0)
                if tick // 2 < len(clocks):
                    (off if k % 2 else on).append(t - (clocks[tick // 2] + 0.32 + (P / 2 if tick % 2 else 0)))
            print(f"{bpm} BPM shuffle {sh}: first step {h[0] - start:.2f} ms after FA (on the first F8); "
                  f"on-beat steps {stats(on)}; off-beat steps {stats(off)} ms after their (half-)clock")
    print()


def din_clock():
    print("## DIN sync clock (24 ppqn, rising edge) to step trigger\n")
    for bpm in (90, 120, 160, 240):
        P = 60000.0 / bpm / 24
        r = Run(WRITE + f";INST+11;p {ALL16};press STOP;run 0.2;press PATT1;press 1;SHIFT+TAPESYNC;SHIFT+TAPESYNC;run 0.5;"
                f"slice 10;log hits;din run;echo d;dinclock {bpm} 8")
        t0 = [t for t, k, x in r.lines if k == "ECHO" and x == "d"][0]
        h = [t for t, n, c in r.hits()]
        k0 = int((h[0] - t0) // P)
        lat = [t - (t0 + (k0 + 6 * k) * P) for k, t in enumerate(h)]
        print(f"{bpm} BPM: RUN with a clock edge, the first step on edge {k0} after it; steps {stats(lat)} ms after their edge")
    print()


# ---------------------------------------------------------------- shuffle

def onsets(path, refractory_ms=40, frac=0.3):
    import soundfile as sf
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x[:, 0]
    a = np.abs(x)
    idx = np.nonzero(a > a.max() * frac)[0]
    out, last = [], -10 ** 9
    for j in idx:
        if j - last > refractory_ms * sr / 1000:
            k = j
            while k > 0 and a[k - 1] > a.max() * frac * 0.1 and j - k < sr * 0.002:
                k -= 1
            out.append(k / sr * 1000)
        last = j
    return out


def pair_split(t):
    if len(t) % 2:
        t = t[:-1]
    d = intervals(t)
    a, b = d[0::2], d[1::2]
    return (a, b) if statistics.mean(a) >= statistics.mean(b) else (b, a)


def shuffle():
    print("## Shuffle: the first 16th of each pair (12 + s ticks of 1/48 beat, s = key - 1)\n")
    print("| key | real 909: tick, first step mean (sd, range) | emulated: tick, first step mean (sd, range) | difference |")
    print("|---|---|---|---|")
    for k in range(1, 8):
        f = os.path.join(PACK, f"RimShot/RimShot909-shuffle{k}-112BPM.wav")
        rec = None
        if os.path.exists(f):
            o = onsets(f)
            if len(o) % 2:
                o = o[:32]
            pair = (o[30] - o[0]) / 15
            first = [o[i + 1] - o[i] for i in range(0, 30, 2)]
            rec = (pair / 24, first)
        r = Run(TEMPO_REC + ";" + WRITE + f";INST+11;p {ALL16};SHUFFLE+{k};run 0.5;log hits;run 30")
        t = [h[0] for h in r.hits("RS")]
        if len(t) % 2 == 0:
            t = t[:-1]  # an even number of steps, whole pairs
        first, second = pair_split(t)
        tick = (t[-1] - t[0]) / (len(t) - 1) / 12
        emu = (tick, first)
        cell = lambda x: f"{x[0]:.3f} ms, {statistics.mean(x[1]):.2f} ({statistics.pstdev(x[1]):.2f}, {min(x[1]):.1f}-{max(x[1]):.1f}) = {statistics.mean(x[1]) / x[0]:.2f} ticks"
        diff = f"{statistics.mean(emu[1]) - statistics.mean(rec[1]):+.2f} ms" if rec else ""
        print(f"| {k} | {cell(rec) if rec else 'no file'} | {cell(emu)} | {diff} |")
    print()


# ---------------------------------------------------------------- flam

def flam_record(inst, key):
    import soundfile as sf
    f = glob.glob(os.path.join(PACK, FLAM_FILES[inst] % (key - 8)) + ".wav")
    if not f:
        return None
    x, sr = sf.read(f[0])
    if x.ndim > 1:
        x = x[:, 0]
    return flam_interval(x, sr, inst, key)


def flam_interval(x, sr, inst, key, Tg=4.075):
    """The main note's time (ms) in x, a flam from its grace note's onset (x[0]): the same feature
    of both hits (see the module's docstring), f + 4 or f + 5 group ticks in."""
    from scipy.signal import butter, sosfiltfilt
    x = x[: int(0.07 * sr)]
    ms = lambda i: i / sr * 1000
    fi = key - 9
    if inst == "BD":
        y = sosfiltfilt(butter(2, 1000, 'hp', fs=sr, output='sos'), x)
        e = np.convolve(np.abs(y), np.ones(int(0.0002 * sr)) / int(0.0002 * sr), 'same')
        n = int(0.004 * sr)
        tpl = e[:n] - e[:n].mean()
        best, bi = -1, 0
        for i in range(int(0.012 * sr), len(e) - n):
            seg = e[i:i + n] - e[i:i + n].mean()
            c = np.dot(seg, tpl) / (np.linalg.norm(seg) * np.linalg.norm(tpl) + 1e-12)
            if c > best:
                best, bi = c, i
        return ms(bi)
    if inst == "SD":
        y = np.abs(sosfiltfilt(butter(2, 2000, 'hp', fs=sr, output='sos'), x))
        e = np.convolve(y, np.ones(int(0.0005 * sr)) / int(0.0005 * sr), 'same')
        def score(c):
            i0, i1 = int((c * Tg - 1.0) * sr / 1000), int((c * Tg + 1.5) * sr / 1000)
            base = e[int((c * Tg - 3) * sr / 1000):i0].mean()
            seg = e[i0:i1]
            j = np.argmax(seg > base + 0.5 * (seg.max() - base))
            return seg.max() / (base + 1e-9), ms(i0 + j)
    else:
        y = sosfiltfilt(butter(2, 6000, 'lp', fs=sr, output='sos'), x)
        dy = np.diff(y)
        def score(c):
            i0, i1 = int((c * Tg - 1.0) * sr / 1000), int((c * Tg + 1.0) * sr / 1000)
            j = np.argmin(dy[i0:i1])
            return -dy[i0 + j], ms(i0 + j)
    a, b = score(fi + 4), score(fi + 5)
    return (a if a[0] > b[0] else b)[1]


def flam_emulated(inst, key, first_only):
    a, b = FLAM_KEYS[inst]
    s = TEMPO_REC + ";" + WRITE + f";down INST;run 0.05;pair {a} {b};up INST;run 0.1;p 1 5 9 13;SHUFFLE+{key};run 0.5"
    if first_only:
        s += ";press STOP;run 0.2;press PATT1;press 1;log hits"
        for i in range(10):
            s += f";run {0.05 + 0.023 * i:.3f};press START;run 0.15;press STOP;run 0.1"
    else:
        s += ";log hits;run 20"
    h = Run(s).hits(inst)
    return [t1 - t0 for (t0, n0, c0), (t1, n1, c1) in zip(h, h[1:]) if c1 > c0 and t1 - t0 < 70]


def flam():
    print("## Flam: grace note to main note (ms; group ticks of 4.075 ms in brackets)\n")
    print("Emulated: running pattern (both phases) and the first step after START. Real: one flam per file.\n")
    print("| voice | key | emulated, running | emulated, first after START | real 909 | real within 1 ms of an emulated value |")
    print("|---|---|---|---|---|---|")
    for inst in FLAM_KEYS:
        for key in range(9, 17):
            run = flam_emulated(inst, key, False)
            first = flam_emulated(inst, key, True)
            groups = {}
            for d in run:
                groups.setdefault(round(d / 4.075), []).append(d)
            rs = "; ".join(f"{min(v):.2f}-{max(v):.2f} [{n}]" for n, v in sorted(groups.items()))
            fs = f"{min(first):.2f}-{max(first):.2f} [{round(statistics.mean(first) / 4.075)}]"
            rec = flam_record(inst, key) if os.path.isdir(PACK) else None
            if rec is None:
                print(f"| {inst} | {key} | {rs} | {fs} | | |")
                continue
            near = min(min(abs(rec - lo), abs(rec - hi)) if not lo <= rec <= hi else 0 for v in groups.values() for lo, hi in [(min(v), max(v))])
            print(f"| {inst} | {key} | {rs} | {fs} | {rec:.2f} [{round(rec / 4.075)}] | {'yes' if near <= 1.0 else 'NO'} ({near:.2f}) |")
    print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seq", default=seqrun.SEQ)
    ap.add_argument("--only", default="scan,triggers,notes,clock,din,shuffle,flam")
    args = ap.parse_args()
    seqrun.SEQ = args.seq
    parts = {"scan": scan, "triggers": triggers, "notes": midi_notes, "clock": midi_clock, "din": din_clock, "shuffle": shuffle, "flam": flam}
    for p in args.only.split(","):
        parts[p]()


if __name__ == "__main__":
    main()
