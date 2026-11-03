#!/usr/bin/env python3
"""Regression checks of the TR-909 firmware on the emulated CPU board (docs/sequencer.md).

Each check scripts tr909seq through an owner's-manual procedure (or a MIDI / sync case) and
checks what the CPU board does: the triggers and accent DAC codes sent to the voice board, their
timing, the LEDs, the display, MIDI out and the battery RAM. Run after changing core/machine.*,
core/upd7810/ or core/factory.*:

    tools/tr909seq/tests.py [--seq BUILD/tr909seq] [--factory BUILD/tr909factory] [-v] [NAME ...]

The ROMs come from the private folder (../private/roms/tr909); checks that need the v1.0 ROM
are skipped without it. NAME runs the checks whose names contain it; -v prints each run's log.

The scripts use tr909seq's commands and the macros of seqrun.py.
"""
import argparse
import os
import re
import statistics
import subprocess
import sys

sys.dont_write_bytecode = True  # no __pycache__ in the repository
import seqrun  # noqa: E402
from seqrun import Run, intervals, V1, V4

ARGS = None
CHECKS = []


def check(name, ref):
    def wrap(f):
        CHECKS.append((name, ref, f))
        return f
    return wrap


class Fail(Exception):
    pass


def expect(cond, what):
    if not cond:
        raise Fail(what)


# A fresh pattern in Bank I, pattern group 1, number 1, written in step write while it runs.
WRITE = "run 1;SHIFT+PATT1;CLEAR+1;press START"
ALL16 = " ".join(str(i) for i in range(1, 17))
TEMPO_112 = "analog 0 4c"   # A/D 76: 112.6 BPM (the display reads 113), a 48 ppqn tick of 11102 us


# ---------------------------------------------------------------- checks: the CPU board

@check("scan period", "docs/firmware.md, scan")
def _():
    r = Run("run 1;scan 4")
    m = re.search(r"scan entry period: n=(\d+) mean ([\d.]+) us min ([\d.]+) max ([\d.]+)", r.text)
    expect(m, "no scan report")
    mean, mn = float(m.group(2)), float(m.group(3))
    # 10 or 11 A/D interrupts of 192 us: 1.92 or 2.11 ms, mean about 2.04 ms
    expect(1990 < mean < 2080, f"mean scan period {mean} us")
    expect(1890 < mn < 1930, f"shortest scan {mn} us")
    bins = [(int(a), int(c)) for a, b, c in re.findall(r"\s+(\d+)-\s*(\d+) us\s+(\d+)", r.text.split("trigger latch")[0])]
    short = sum(c for a, c in bins if 1890 <= a < 1950)
    long_ = sum(c for a, c in bins if 2090 <= a < 2140)
    expect(short > 300 and long_ > 600, f"scan periods not 1.92/2.11 ms: {short} short, {long_} long")
    return f"mean {mean:.0f} us, {short} of 1.92 ms and {long_} of 2.11 ms"


@check("power-on state", "SN p.13-15, OM p.34, p.38")
def _():
    r = Run("run 1;leds;iram")
    mem = r.dump()
    expect(mem[0xFF41] == 0x09 and mem[0xFF3C] == 0x0A, "MIDI channels at power-on are not rx 10 / tx 11")
    expect(mem[0xFF63] & 3 == 0, "sync mode is not INT at power-on")
    l = r.leds()[0]
    expect({"TRACK1", "PATT1"} <= l["lit"], f"power-on LEDs {l['lit']}")
    w = [s for t, k, s in r.lines if k == "TRIG2"]
    expect(any("00 -> 3C" in s for s in w), "no power-up pulse on trigger latch 2 (400A = 3C)")


@check("version LED", "SN p.7")
def _():
    # TRACK 1 held at power-on lights the version's step LED while it is held: v4.0 step 4,
    # v1.0 step 1 ("the MAIN key 2 ... will blink, if version 2").
    want = {V4: "0009000000000000", V1: "9000000000000000"}
    for rom, steps in want.items():
        if not os.path.exists(rom):
            continue
        r = Run("down TRACK1;run 1.5;ledduty 0.3;up TRACK1", rom=rom)
        expect(r.duty()[0][0] == steps, f"{os.path.basename(rom)}: step LEDs {r.duty()[0][0]}")


@check("cartridge absent", "OM p.33, SN p.12")
def _():
    # AN1 (cartridge sense) reads 0: the firmware never touches 6000-7FFF; SHIFT+COPY+LOAD+BANK I
    # +ENTER (load from the cartridge) leaves the memory alone.
    r = Run(WRITE + ";INST+2;p 1;press STOP;run 0.2;press TRACK1;run 0.2;snap;"
            "down SHIFT;run 0.05;p 11 16 FWD ENTER;up SHIFT;run 0.5;diff;ram 2000 1")
    expect(r.dump()[0x2000] == 0x02, "pattern lost after a cartridge load with no cartridge")
    expect(not [a for a, o, n in r.ram() if a < 0x2F00], f"cartridge load with no cartridge wrote {r.ram()[:4]}")
    # ENTER in track play switches to cartridge mode (VV:33 bit 6, ENTER lit) only with one in.
    mode = {}
    for sense in ("ff", "00"):
        r = Run(f"analog 1 {sense};run 1;press TRACK1;press ENTER;run 0.2;iram;leds")
        mode[sense] = (r.dump()[0xFF33] & 0x40 != 0, "ENTER" in r.leds()[-1]["lit"])
    expect(mode == {"ff": (False, False), "00": (True, True)}, f"cartridge mode on ENTER: {mode}")


# ---------------------------------------------------------------- checks: pattern write

@check("step write example", "OM p.9-11")
def _():
    # The manual's example: CH on every step, SD on 5 and 13, BD on 1, 8, 10, 11, 14,
    # TOTAL ACCENT on 1, 5, 9, 13.
    r = Run("accent 0;run 1;SHIFT+PATT2;CLEAR+1;press START;INST+14;p " + ALL16 + ";INST+4;p 5 13;"
            "INST+2;p 1 8 10 11 14;INST+ENTER;p 1 5 9 13;run 0.5;log hits;echo bar;run 2.4;echo end;press STOP;"
            "ram 2400 10;ram 2500 10;ram 2D00 10")
    mem = r.dump()
    bd = [i + 1 for i in range(16) if mem[0x2400 + i] & 0x03]
    sd = [i + 1 for i in range(16) if mem[0x2400 + i] & 0x0C]
    ch = [i + 1 for i in range(16) if mem[0x2500 + i] & 0x30]
    ta = [i + 1 for i in range(16) if mem[0x2D00 + i] & 0x01]
    expect(bd == [1, 8, 10, 11, 14] and sd == [5, 13] and ch == list(range(1, 17)) and ta == [1, 5, 9, 13],
           f"RAM BD {bd} SD {sd} CH {ch} TA {ta}")
    expect(all(mem[0x2400 + i] & 0x0F in (0, 2, 8) for i in range(16)), "BD/SD not written at normal level")
    h = r.hits(start="bar", end="end")
    expect(len([x for x in h if x[1] == "CH"]) >= 16, "CH not on every step")
    expect({c for t, n, c in h if n == "CH"} == {25}, "CH not at its normal code 25 (TOTAL ACCENT at 0)")


@check("key cycle", "OM p.11, p.25")
def _():
    # BD, SD, toms, CH: none -> normal (dim LED) -> accent (bright) -> none; the other voices:
    # none <-> written. OH is both CH keys.
    r = Run(WRITE + ";INST+1;snap;p 3;diff;p 3;diff;p 3;diff;INST+11;p 4;diff;p 4;diff;"
            "INST+14;p 6;diff;p 6;diff;p 6;diff;down INST;run 0.05;pair 13 14;up INST;run 0.1;p 7;diff;p 7;diff")
    ch = [(a, o, n) for a, o, n in r.ram()]
    want = [(0x2002, 0, 2), (0x2002, 2, 1), (0x2002, 1, 0), (0x2103, 0, 4), (0x2103, 4, 0),
            (0x2105, 0, 0x20), (0x2105, 0x20, 0x10), (0x2105, 0x10, 0), (0x2106, 0, 0x30), (0x2106, 0x30, 0)]
    expect(ch == want, f"RAM changes {[(hex(a), o, n) for a, o, n in ch]}")


@check("step LEDs", "OM p.10-12, p.25")
def _():
    # The step LEDs show the selected voice: dim where it plays at normal level, bright where
    # accented; and the running light.
    r = Run(WRITE + ";INST+2;p 3;p 5;p 5;run 0.2;ledduty 1.5")
    d = [s for t, k, s in r.lines if k == "DUTY"][0].split()[1]
    expect(1 <= int(d[2]) <= 4 and int(d[4]) >= 8 and all(int(c) <= 1 for i, c in enumerate(d) if i not in (2, 4)),
           f"step LED on-times {d} (0-9)")
    return f"step LED on-times {d}"


@check("scale", "OM p.24")
def _():
    # SCALE cycles 16th -> 32nd -> 8th triplet -> 16th triplet -> 16th and sets the last step
    # to 16, 16, 12, 12; step lengths 12, 6, 16 and 8 of the 48 ppqn clock.
    s = TEMPO_112 + ";" + WRITE + ";INST+11;p " + ALL16 + ";run 0.5"
    for i in range(5):
        s += f";log hits;echo s{i};run 1.6;echo e{i};log off;leds;press SCALE;run 0.3"
    s += ";ram 2F80 1"
    r = Run(s)
    P = 11.102
    lens = []
    for i in range(4):
        t = [h[0] for h in r.hits("RS", f"s{i}", f"e{i}")]
        lens.append((t[-1] - t[0]) / (len(t) - 1))
    want = [12 * P, 6 * P, 16 * P, 8 * P]
    expect(all(abs(a - b) < 0.6 for a, b in zip(lens, want)), f"step lengths {lens} ms, want {want}")
    lit = [sorted(x for x in l["lit"] if x.startswith("SCALE")) for l in r.leds()]
    expect(lit[:5] == [["SCALE1"], ["SCALE2"], ["SCALE3"], ["SCALE4"], ["SCALE1"]], f"scale LEDs {lit}")
    return "steps " + ", ".join(f"{v:.1f}" for v in lens) + " ms"


@check("scale sets last step", "OM p.24")
def _():
    r = Run(WRITE + ";snap;press SCALE;diff;press SCALE;diff;press SCALE;diff;press SCALE;diff")
    hdr = [n for a, o, n in r.ram() if a == 0x2F80]
    expect(hdr == [0x1F, 0x2B, 0x3B, 0x0F], f"header byte 0 went {[hex(x) for x in hdr]}")


@check("last step", "OM p.24")
def _():
    r = Run(TEMPO_112 + ";" + WRITE + ";INST+11;p " + ALL16 + ";LAST+5;run 0.5;log hits;echo a;run 3;echo b;ram 2F80 1")
    t = [h[0] for h in r.hits("RS", "a", "b")]
    expect(r.dump()[0x2F80] == 0x04, "last step not 5 in the header")
    # 5 steps a bar: the step LEDs run 1-5 and the hits keep the 16th spacing
    d = intervals(t)
    expect(max(d) < 12 * 11.102 + 5, f"a gap in a 5-step loop: {max(d):.1f} ms")


@check("total accent", "OM p.25, docs/firmware.md accent")
def _():
    # INST + ENTER selects TOTAL ACCENT; its steps add TOTAL ACCENT (A/D >> 3) to every voice.
    out = {}
    for pot in (0, 0.5, 1):
        r = Run(f"accent {pot};" + WRITE + ";INST+2;p 1 2;INST+1;p 3 3 4 4;INST+ENTER;p 2 4;run 0.5;log hits;echo a;run 3;echo b")
        h = r.hits("BD", "a", "b")
        out[pot] = sorted({c for t, n, c in h})
    # step 1 normal, 2 normal + TA, 3 accent, 4 accent + TA
    expect(out[0] == [10, 21], f"TOTAL ACCENT 0: BD codes {out[0]}")
    expect(out[0.5] == [10, 14, 21, 33], f"TOTAL ACCENT half: BD codes {out[0.5]}")
    expect(out[1] == [10, 21, 47], f"TOTAL ACCENT full: BD codes {out[1]}")
    return f"BD codes at TOTAL ACCENT 0 / half / full: {out[0]} / {out[0.5]} / {out[1]}"


def pair_lengths(t):
    """Mean lengths of the two steps of each pair, the longer (shuffled first step) first."""
    if len(t) % 2:
        t = t[:-1]
    d = intervals(t)
    a, b = d[0::2], d[1::2]
    return (statistics.mean(a), statistics.mean(b)) if statistics.mean(a) >= statistics.mean(b) else (statistics.mean(b), statistics.mean(a))


@check("shuffle 16th", "OM p.26, research C.7")
def _():
    # SHUFFLE/FLAM + key 1-7: the pair of 16ths is split 12+s / 12-s ticks, s = key - 1.
    P, got = 11.102, []
    for k in range(1, 8):
        r = Run(TEMPO_112 + ";" + WRITE + f";INST+11;p {ALL16};SHUFFLE+{k};run 0.5;log hits;run 8")
        long_, short = pair_lengths([h[0] for h in r.hits("RS")])
        got.append(long_ / P)
        expect(abs(long_ - (11 + k) * P) < 1.5 and abs(short - (13 - k) * P) < 1.5,
               f"shuffle {k}: {long_:.2f} / {short:.2f} ms, want {(11 + k) * P:.2f} / {(13 - k) * P:.2f}")
    return "first step of a pair " + ", ".join(f"{g:.2f}" for g in got) + " ticks"


@check("shuffle 32nd", "OM p.26")
def _():
    # 32nd scale: 1 = 2, 3 = 4, 5 = 6, 7: the first step of a pair is (12 + s) // 2 ticks.
    P = 11.102
    for k in range(1, 8):
        r = Run(TEMPO_112 + ";" + WRITE + f";INST+11;p {ALL16};press SCALE;SHUFFLE+{k};run 0.5;log hits;run 5")
        long_, short = pair_lengths([h[0] for h in r.hits("RS")])
        want = (12 + k - 1) // 2
        expect(abs(long_ - want * P) < 1.5 and abs(short - (12 - want) * P) < 1.5,
               f"32nd shuffle {k}: {long_:.2f} / {short:.2f} ms, want {want} / {12 - want} ticks")


@check("shuffle off on triplets and an odd last step", "OM p.26")
def _():
    P = 11.102
    r = Run(TEMPO_112 + ";" + WRITE + f";INST+11;p {ALL16};press SCALE;press SCALE;SHUFFLE+7;run 0.5;log hits;run 4")
    d = intervals([h[0] for h in r.hits("RS")])
    expect(abs(statistics.mean(d) - 16 * P) < 0.5 and max(d) - min(d) < 6, f"8th triplets shuffled: {min(d):.1f}-{max(d):.1f} ms")
    r = Run(TEMPO_112 + ";" + WRITE + f";INST+11;p {ALL16};LAST+5;SHUFFLE+7;run 0.5;log hits;run 4")
    d = [round(x / P) for x in intervals([h[0] for h in r.hits("RS")])]
    # steps 1-4 are two shuffled pairs (18 + 6), step 5 keeps 12 ticks
    s = "".join({18: "L", 6: "S", 12: "N"}.get(x, "?") for x in d)
    expect("LSLSN" in s and "?" not in s, f"5-step pattern at shuffle 7: step lengths {d}")


@check("shuffle/flam display", "OM p.26")
def _():
    r = Run(WRITE + ";SHUFFLE+4;SHUFFLE+12;down SHUFFLE;run 0.3;ledduty 0.3;up SHUFFLE;run 0.1;ram 2F81 1")
    steps = r.duty()[0][0]
    expect(steps[3] == "9" and steps[11] == "9", f"SHUFFLE/FLAM held: step LEDs {steps}")
    expect(r.dump()[0x2F81] == 0x33, f"header byte 1 {r.dump()[0x2F81]:02X}, want 33 (flam 3, shuffle 3)")


FLAM_KEYS = {"BD": (1, 2), "SD": (3, 4), "LT": (5, 6), "MT": (7, 8), "HT": (9, 10)}


def flam_run(inst, key, tempo="analog 0 4c", secs=12, first_only=False):
    """Grace-to-main intervals (ms) of a flam on steps 1, 5, 9, 13 (or only the first after START)."""
    a, b = FLAM_KEYS[inst]
    s = (tempo + ";" + WRITE + f";down INST;run 0.05;pair {a} {b};up INST;run 0.1;p 1 5 9 13;SHUFFLE+{key};run 0.5")
    if first_only:
        s += ";press STOP;run 0.2;press PATT1;press 1;log hits"
        for i in range(12):
            s += f";run {0.05 + 0.021 * i:.3f};echo r{i};press START;run 0.15;press STOP;run 0.1"
    else:
        s += f";log hits;run {secs}"
    r = Run(s)
    h = r.hits(inst)
    out = []
    for (t0, n0, c0), (t1, n1, c1) in zip(h, h[1:]):
        if c1 > c0 and t1 - t0 < 70:
            out.append((t1 - t0, c0, c1))
    return out


@check("flam intervals", "OM p.26, research C.7")
def _():
    # The interval is f+4 or f+5 group ticks (f = key - 9; 2 scans of about 2.04 ms each), set by
    # the step's phase against the two trigger groups; HT, in the other group, always f+5.
    res = {}
    for inst in FLAM_KEYS:
        for key in (9, 12, 16):
            d = [x[0] for x in flam_run(inst, key)]
            f = key - 9
            n = sorted({round(x / 4.077) for x in d})
            want = [f + 5] if inst == "HT" else [f + 4, f + 5]
            expect(n == want, f"{inst} flam key {key}: {len(d)} flams of {min(d):.1f}-{max(d):.1f} ms = {n} group ticks, want {want}")
            res[(inst, key)] = (min(d), max(d))
    return "BD key 9 {:.1f}-{:.1f} ms, key 16 {:.1f}-{:.1f} ms".format(*res[("BD", 9)], *res[("BD", 16)])


@check("flam levels and tempo", "OM p.26")
def _():
    # The grace note plays at the normal level, the main note at the accent level; the interval
    # doesn't follow TEMPO.
    slow = [x for x in flam_run("SD", 13, tempo="tempo 0.1")]
    fast = [x for x in flam_run("SD", 13, tempo="tempo 0.95")]
    expect({(c0, c1) for d, c0, c1 in slow + fast} == {(10, 21)}, f"flam levels {set((c0, c1) for d, c0, c1 in slow + fast)}")
    lo = sorted({round(d / 4.077) for d, c0, c1 in slow}); hi = sorted({round(d / 4.077) for d, c0, c1 in fast})
    expect(lo == hi == [8, 9], f"flam key 13 group ticks at slow / fast TEMPO: {lo} / {hi}")


@check("flam after START", "OM p.26")
def _():
    # The first step after START always gets the longer interval (f+5): START is read on a
    # group-A scan and the first step is ready before the next one, a group-B scan.
    d = [x[0] for x in flam_run("SD", 9, first_only=True)]
    n = {round(x / 4.077) for x in d}
    expect(len(d) >= 10 and n == {5}, f"first flams after START: {[round(x, 2) for x in d]}")
    return f"{len(d)} starts, {min(d):.2f}-{max(d):.2f} ms"


@check("trigger pulse and groups", "SN p.5, research C.4")
def _():
    # A trigger is high for one scan (about 2 ms). BD/SD/LT/MT go out on one scan, HT/RS/HC/HH/
    # CR/RD on the next: a step's two groups are one scan apart, in either order.
    r = Run(TEMPO_112 + ";" + WRITE + f";INST+2;p {ALL16};INST+10;p {ALL16};INST+11;p {ALL16};run 0.5;log hits;run 8")
    w = r.widths()
    expect(1.85 < min(w) and max(w) < 2.2, f"trigger widths {min(w):.3f}-{max(w):.3f} ms")
    steps = {}
    for t, n, c in r.hits():
        steps.setdefault(round(t / 133.2), {})[n] = t
    d = [s["HT"] - s["BD"] for s in steps.values() if "HT" in s and "BD" in s]
    expect(all(1.85 < abs(x) < 2.2 for x in d), f"HT - BD on one step: {min(d):.2f} .. {max(d):.2f} ms")
    expect(all(abs(s["RS"] - s["HT"]) < 0.01 for s in steps.values() if "HT" in s and "RS" in s), "HT and RS not on one scan")
    late = sum(1 for x in d if x > 0) / len(d)
    return f"widths {min(w):.2f}-{max(w):.2f} ms (mean {statistics.mean(w):.2f}); group B {min(abs(x) for x in d):.2f}-{max(abs(x) for x in d):.2f} ms after (or before) A, after on {late:.0%} of steps"


@check("tap write and guide", "OM p.14, p.25")
def _():
    # SHIFT+PATTERN 3, SHIFT+TAP; CLEAR+1; START; GUIDE (CYCLE) clicks RS on the beats (DAC6 byte
    # 2D on the bar, 0F on the others); the left key of a voice writes an accent, the right one
    # normal, at the nearest step.
    r = Run(TEMPO_112 + ";run 1;SHIFT+PATT3;SHIFT+BACK;CLEAR+1;press START;press CYCLE;run 1.3;log hits;echo g;run 2.2;echo h;"
            "log off;snap;p 3;run 0.4;p 4;run 0.6;diff;leds")
    g = r.hits("RS", "g", "h")
    expect(sorted({c for t, n, c in g}) == [7, 22] and len(g) in (4, 5), f"guide clicks {[(round(t), c) for t, n, c in g]}")
    ch = r.ram()
    expect(len(ch) == 2 and {n for a, o, n in ch} == {0x04, 0x08} and all(0x2800 <= a < 0x2810 for a, o, n in ch),
           f"tap write RAM {[(hex(a), o, n) for a, o, n in ch]}")
    expect({"BACK", "CYCLE"} <= r.leds()[-1]["lit"], f"LEDs {r.leds()[-1]['lit']}")


@check("tap write CLEAR", "OM p.14")
def _():
    # CLEAR held with a voice's key erases that voice's notes as the steps pass: the left key
    # its accented notes, the right key its normal ones.
    r = Run(TEMPO_112 + ";run 1;SHIFT+PATT3;SHIFT+BACK;CLEAR+1;press START;run 1.5;p 3;run 0.4;p 4;run 0.4;snap;"
            "down CLEAR;run 0.05;down 4;run 2.3;up 4 CLEAR;run 0.1;diff;ram 2800 10")
    mem = r.dump()
    sd = [mem[0x2800 + i] & 0x0C for i in range(16)]
    expect(sd.count(0x04) == 1 and sd.count(0x08) == 0 and len(r.ram()) == 1, f"after CLEAR+4: SD bits {sd}")


@check("bank", "OM p.12-13")
def _():
    # Track mode, SHIFT+BANK II (AVAILABLE MEAS): its LED lights, patterns go to 3000-3FFF.
    r = Run("run 1;press TRACK1;SHIFT+AVAIL;leds;snap;SHIFT+PATT1;CLEAR+1;press START;INST+2;p 1;press STOP;run 0.2;diff;"
            "press TRACK1;SHIFT+FWD;leds")
    l = r.leds()
    expect("AVAIL" in l[0]["lit"] and "FWD" not in l[0]["lit"] and "FWD" in l[1]["lit"], f"bank LEDs {l[0]['lit']} / {l[1]['lit']}")
    expect([a for a, o, n in r.ram()][:1] == [0x3000], f"Bank II write went to {[hex(a) for a, o, n in r.ram()]}")


def three_patterns():
    """Patterns 1-3 of group 1: BD, SD, LT on step 1 (normal)."""
    s = TEMPO_112 + ";run 1;SHIFT+PATT1"
    for p, k in ((1, 2), (2, 4), (3, 6), (4, 8)):
        s += f";CLEAR+{p};press START;INST+{k};p 1;press STOP;run 0.1"
    return s


@check("pattern play", "OM p.14")
def _():
    # PATTERN group, then the number; START; a new number while it plays starts at the next bar;
    # STOP/CONT stops, again continues; two numbers together play the block between them.
    r = Run(three_patterns() + ";press PATT1;press 1;log hits;press START;run 1;press 2;run 3.4;press STOP;run 0.3;"
            "echo c;press STOP;run 2.3;press STOP;echo b;press PATT1;down 1 3;run 0.1;up 1 3;run 0.1;press START;run 8;press STOP")
    a = [n for t, n, c in r.hits(end="c")]
    expect(a[:3] == ["BD", "SD", "SD"], f"pattern change: {a}")
    m = [b for t, b in r.midi("c", "b") if b != 0xF8]
    expect(m[:1] == [0xFB] and r.hits(start="c", end="b"), f"continue: MIDI {m}")
    blk = [n for t, n, c in r.hits(start="b")]
    expect(blk[:4] == ["BD", "SD", "LT", "BD"], f"block play {blk}")


@check("chain", "OM p.28")
def _():
    # In pattern write, the two end keys of a run of patterns + ENTER chain them (header bit 7 on
    # each but the last); any of them plays the chain from its start; key + ENTER releases it.
    r = Run(three_patterns() + ";snap;down 2 4;run 0.1;press ENTER;up 2 4;run 0.1;diff;press PATT1;press 3;log hits;"
            "press START;run 8;press STOP;log off;echo r;SHIFT+PATT1;snap;down 3;run 0.05;press ENTER;up 3;run 0.1;diff")
    set_ = [(a, n) for a, o, n in r.ram(end="r")]
    expect(set_ == [(0x2F88, 0x8F), (0x2F90, 0x8F)], f"chain flags {[(hex(a), hex(n)) for a, n in set_]}")
    play = [n for t, n, c in r.hits()]
    expect(play[:4] == ["SD", "LT", "MT", "SD"], f"chain 2-4 played {play}")
    rel = [(a, n) for a, o, n in r.ram(start="r")]
    expect(rel == [(0x2F88, 0x0F), (0x2F90, 0x0F)], f"release {[(hex(a), hex(n)) for a, n in rel]}")


@check("pattern copy", "OM p.27")
def _():
    # Pattern write: SHIFT+PATTERN 2, key 1 (the destination); SHIFT: COPY, BANK I, PATTERN 1,
    # key 3, ENTER copies group 1 pattern 3 (and its header) into group 2 pattern 1.
    r = Run(three_patterns() + ";SHIFT+PATT2;press 1;snap;down SHIFT;run 0.05;p 11 FWD PATT1 3 ENTER;up SHIFT;run 0.1;diff")
    ch = {a: n for a, o, n in r.ram()}
    expect(ch.get(0x2400) == 0x20 and ch.get(0x2F82) == 0x0F and ch.get(0x2F83) == 0x40, f"copy wrote {[(hex(a), hex(n)) for a, n in ch.items()]}")


@check("CLEAR", "OM p.9, p.37")
def _():
    # CLEAR + key: internal mode clears the voices, TOTAL ACCENT and the header (16 steps, 16th,
    # no shuffle, flam key 13); EXT INST mode clears only the external rows and their accent.
    s = (TEMPO_112 + ";" + WRITE + ";INST+2;p 1 3;INST+ENTER;p 1;LAST+7;SHUFFLE+3;SHUFFLE+11;press STOP;run 0.2;"
         "SHIFT+EXTINST;press START;INST+1;p 2;INST+ENTER;p 2;press STOP;run 0.2;SHIFT+EXTINST;snap")
    r = Run(s + ";CLEAR+1;diff;echo x;SHIFT+EXTINST;CLEAR+1;diff")
    a = sorted((a, o, n) for a, o, n in r.ram(end="x"))
    expect(a == [(0x2000, 2, 0), (0x2002, 2, 0), (0x2C00, 1, 0), (0x2F80, 0x06, 0x0F), (0x2F81, 0x22, 0x40)], f"internal CLEAR {a}")
    b = sorted((a, o, n) for a, o, n in r.ram(start="x"))
    expect(b == [(0x2201, 1, 0), (0x2C01, 2, 0)], f"EXT CLEAR {b}")


def track6():
    """Track 1 of Bank I = patterns 1 2 3 4 1 2 (BD SD LT MT BD SD), written in track write."""
    s = three_patterns() + ";press TRACK1;SHIFT+TRACK1;down SHIFT;run 0.05;p 13 CYCLE ENTER;up SHIFT;run 0.1;press START"
    for p in (1, 2, 3, 4, 1, 2):
        s += f";press PATT1;press {p};press ENTER;display"
    return s + ";press STOP;run 0.2"


PLAY_T1 = ";SHIFT+TRACK1;down AVAIL;run 0.2;display;up AVAIL;run 0.1;press TRACK1;log hits;press START;run 16;log off"


def track_run(edit):
    r = Run(track6() + ";echo e;" + edit + ";echo p" + PLAY_T1)
    disp = [s.strip("[]") for t, k, s in r.section("e") if k == "DISPLAY"]
    return [n for t, n, c in r.hits(start="p")], disp, r


@check("track write", "OM p.15-17")
def _():
    # SHIFT+TRACK 1; SHIFT: DEL, LAST MEAS, ENTER clears it; START; PATTERN + number, ENTER per
    # measure (the display counts the next measure); AVAILABLE MEAS shows 896 less the measures used.
    r = Run(track6() + PLAY_T1)
    disp = [s.strip("[]").strip() for t, k, s in r.lines if k == "DISPLAY"]
    expect(disp[:6] == ["2", "3", "4", "5", "6", "7"], f"measure display {disp}")
    expect(disp[-1] == "890", f"AVAILABLE MEAS {disp[-1]}")
    play = [n for t, n, c in r.hits()]
    expect(play == ["BD", "SD", "LT", "MT", "BD", "SD"], f"track play {play}")
    m = r.midi()
    expect(m and m[-1][1] == 0xFC, "track play does not stop at its end")


@check("track write: a block of patterns", "OM p.14, p.30")
def _():
    # Two numbers together enter the patterns between them as measures (the measure bytes:
    # bits 7-4 pattern - 1, bits 3-2 group - 1).
    r = Run("run 1;press TRACK1;SHIFT+TRACK1;down SHIFT;run 0.05;p 13 CYCLE ENTER;up SHIFT;run 0.1;press START;"
            "press PATT2;press 5;press ENTER;press PATT3;press 16;press ENTER;press PATT1;down 2 4;run 0.1;up 2 4;run 0.1;"
            "press ENTER;press STOP;run 0.2;ram 2C00 5;ram 2F86 2")
    m = r.dump()
    got = [m[0x2C00 + i] & 0xFC for i in range(5)] + [m[0x2F86] | m[0x2F87] << 8]
    expect(got == [0x44, 0xF8, 0x10, 0x20, 0x30, 0x0C04], f"measure bytes and track 1 end {[hex(x) for x in got]}")


@check("track memory full", "OM p.17")
def _():
    # 56 blocks of 16 patterns fill the bank's 896 measures: AVAILABLE MEAS shows 0, and ENTER
    # then shows 0 while it is held and adds nothing (track 1 ends at 0F7F).
    s = ("run 1;press TRACK1;SHIFT+TRACK1;down SHIFT;run 0.05;p 13 CYCLE ENTER;up SHIFT;run 0.1;press START;"
         "press PATT1;down 1 16;run 0.1;up 1 16;run 0.1" + ";press ENTER" * 56 +
         ";display;down AVAIL;run 0.2;display;up AVAIL;run 0.1;press PATT1;press 3;down ENTER;run 0.1;display;up ENTER;"
         "run 0.3;display;press STOP;run 0.3;ram 2F86 2")
    r = Run(s)
    d = [x.strip("[]").strip() for t, k, x in r.lines if k == "DISPLAY"]
    m = r.dump()
    expect(d == ["897", "0", "0", "897"] and m[0x2F86] | m[0x2F87] << 8 == 0x0F7F, f"displays {d}, track end {m[0x2F86] | m[0x2F87] << 8:04X}")


@check("track edit: replace, BACK", "OM p.16")
def _():
    play, disp, r = track_run("SHIFT+TRACK1;press START;p FWD FWD FWD BACK;display;press PATT1;press 4;press ENTER;display;press STOP")
    expect(play == ["BD", "SD", "MT", "MT", "BD", "SD"], f"replace measure 3: {play}")
    expect(disp[-1].strip() == "890", "replace changed the measure count")


@check("track edit: delete", "OM p.28-29")
def _():
    one, d1, r = track_run("SHIFT+TRACK1;down SHIFT;run 0.05;p 3 ENTER;up SHIFT;run 0.1;down SHIFT;run 0.05;p 13 ENTER;up SHIFT;run 0.1")
    expect(one == ["BD", "SD", "MT", "BD", "SD"] and d1[-1].strip() == "891", f"delete 3: {one} {d1}")
    span, d2, r = track_run("SHIFT+TRACK1;down SHIFT;run 0.05;p 2 ENTER;up SHIFT;run 0.1;down SHIFT;run 0.05;p 13 4 ENTER;up SHIFT;run 0.1")
    expect(span == ["BD", "BD", "SD"], f"delete 2-4: {span}")
    last, d3, r = track_run("SHIFT+TRACK1;down SHIFT;run 0.05;p 4 ENTER;up SHIFT;run 0.1;down SHIFT;run 0.05;p 13 CYCLE ENTER;up SHIFT;run 0.1")
    expect(last == ["BD", "SD", "LT"], f"delete 4 to the last: {last}")


@check("track edit: insert", "OM p.30")
def _():
    play, disp, r = track_run("SHIFT+TRACK1;down SHIFT;run 0.05;p 2 ENTER;up SHIFT;run 0.1;press PATT1;press 4;"
                              "down SHIFT;run 0.05;p 12 ENTER;up SHIFT;run 0.1")
    expect(play == ["BD", "MT", "SD", "LT", "MT", "BD", "SD"] and disp[-1].strip() == "889", f"insert 4 at 2: {play} {disp}")


@check("track edit: copy", "OM p.31")
def _():
    # A series copies (inserted before the recalled measure); a single measure does not: the
    # function the manual describes "did not work" (Revolution 909 v5 manual), as here.
    play, disp, r = track_run("SHIFT+TRACK1;down SHIFT;run 0.05;p 2 ENTER;up SHIFT;run 0.1;down SHIFT;run 0.05;p 11 3 ENTER 4 ENTER;up SHIFT;run 0.1")
    expect(play == ["BD", "LT", "MT", "SD", "LT", "MT", "BD", "SD"], f"copy 3-4 to 2: {play}")
    one, disp, r = track_run("SHIFT+TRACK1;down SHIFT;run 0.05;p 1 ENTER;up SHIFT;run 0.1;down SHIFT;run 0.05;p 11 5 ENTER ENTER;up SHIFT;run 0.1")
    expect(one == ["BD", "SD", "LT", "MT", "BD", "SD"] and disp[-1].strip() == "890", f"single-measure copy changed the track: {one}")
    return "a single measure is not copied (the original firmware's bug)"


@check("track play: cycle, measure display", "OM p.18")
def _():
    r = Run(track6() + ";press TRACK1;press CYCLE;log hits;press START;run 14;press CYCLE;run 14;log off;leds")
    play = [n for t, n, c in r.hits()]
    expect(play == ["BD", "SD", "LT", "MT", "BD", "SD"] * 2, f"CYCLE: {play}")
    expect("CYCLE" not in r.leds()[-1]["lit"], "CYCLE LED still lit")
    r = Run(track6() + ";press TRACK1;press START;display;run 2.2;display;run 2.2;display;down TEMPO;run 0.1;display;up TEMPO;run 0.1;display")
    disp = [s.strip("[]").strip() for t, k, s in r.lines if k == "DISPLAY"][-5:]
    expect(disp == ["1", "2", "3", "113", "3"], f"MEAS/TEMPO display {disp}")


@check("track play from a measure", "OM p.18")
def _():
    r = Run(track6() + ";press TRACK1;down SHIFT;run 0.05;p 4 ENTER;up SHIFT;run 0.1;display;log hits;press STOP;run 8;log off;"
            "echo s;down SHIFT;run 0.05;p 4 ENTER;up SHIFT;run 0.1;log hits;press START;run 14")
    a = [n for t, n, c in r.hits(end="s")]
    b = [n for t, n, c in r.hits(start="s")]
    expect(a == ["MT", "BD", "SD"] and b == ["BD", "SD", "LT", "MT", "BD", "SD"], f"STOP/CONT from 4: {a}; START: {b}")


@check("empty track", "OM p.18")
def _():
    r = Run("run 1;press TRACK2;display;log hits;press START;run 1")
    expect(r.lines and [s for t, k, s in r.lines if k == "DISPLAY"][0].strip("[]").strip() == "0" and not r.hits(), "empty track")


@check("tempo display", "OM p.18, p.34")
def _():
    # TEMPO held shows the tempo (A/D 75 -> 112, 76 -> 113, 0 -> 37, 255 -> 290); MIDI sync
    # shows a middle 0, DIN sync a left 0.
    out = []
    for c in ("4b", "4c", "0", "ff"):
        r = Run(f"analog 0 {c};run 1;down TEMPO;run 0.1;display;up TEMPO")
        out.append([s for t, k, s in r.lines if k == "DISPLAY"][0].strip("[]"))
    expect(out == ["112", "113", " 37", "290"], f"tempo display {out}")
    r = Run("run 1;SHIFT+TAPESYNC;down TEMPO;run 0.1;display;up TEMPO;run 0.1;SHIFT+TAPESYNC;down TEMPO;run 0.1;display;up TEMPO")
    d = [s.strip("[]") for t, k, s in r.lines if k == "DISPLAY"]
    expect(d == [" 0 ", "0  "], f"MIDI / DIN sync display {d}")


@check("tempo memory", "OM p.32")
def _():
    # TEMPO+ENTER stores the tempo in the track (Bank I track 1 at 2F8E); TEMPO+TRACK recalls it
    # and the knob no longer works; holding TEMPO and turning the knob gives it back to the knob
    # (the stored tempo stays).
    def bpm(r, a, b):
        f = [t for t, x in r.midi(a, b) if x == 0xF8]
        return 60000.0 / ((f[-1] - f[0]) / (len(f) - 1) * 24)
    r = Run("analog 0 4c;run 1;press TRACK1;snap;TEMPO+ENTER;diff;analog 0 ff;run 0.3;midiout all;TEMPO+TRACK1;echo a;run 0.6;"
            "analog 0 20;echo b;run 0.6;echo c;down TEMPO;run 0.05;analog 0 0;run 0.2;analog 0 ff;run 0.2;up TEMPO;run 0.1;"
            "analog 0 80;run 0.1;echo d;run 0.6;echo e;TEMPO+TRACK1;echo f;run 0.6;echo g")
    expect([a for a, o, n in r.ram()] == [0x2F8E], f"TEMPO+ENTER wrote {r.ram()}")
    a, b, d, f = bpm(r, "a", "b"), bpm(r, "b", "c"), bpm(r, "d", "e"), bpm(r, "f", "g")
    expect(abs(a - 112.6) < 1 and abs(b - 112.6) < 1, f"recalled tempo {a:.1f}, with the knob moved {b:.1f}")
    expect(abs(d - 164) < 3 and abs(f - 112.6) < 1, f"after the release {d:.1f}, recalled again {f:.1f}")


@check("sync: MIDI clock", "OM p.34, SN p.15")
def _():
    # SHIFT+TEMPO MODE: INT -> MIDI. START is ignored; FA starts on the next F8, each 16th is
    # 6 clocks; FC stops; FB continues. Each step comes 0.8-5 ms after its clock.
    r = Run(TEMPO_112 + ";" + WRITE + f";INST+11;p {ALL16};press STOP;run 0.2;press PATT1;press 1;SHIFT+TAPESYNC;"
            "log hits;press START;run 0.5;echo m;midiclock 120 3 start;midi FC;midiclock 120 0.5;echo c;midiclock 120 1 cont;midi FC;run 0.2")
    expect(not r.hits(end="m"), "START played in MIDI sync mode")
    t = [h[0] for h in r.hits(start="m", end="c")]
    d = intervals(t)
    expect(len(t) in (24, 25) and abs(statistics.mean(d) - 125) < 0.5, f"{len(t)} steps, mean {statistics.mean(d):.2f} ms")
    expect(len(r.hits(start="c")) == 8, f"continue played {len(r.hits(start='c'))} steps")


@check("sync: DIN", "OM p.35, SN p.12")
def _():
    # INT -> MIDI -> DIN; START/STOP high runs, 24 ppqn on TEMPO (INT1); CONTINUE high with it
    # continues. The 909 sends FA/FC/FB and an F8 per DIN clock.
    r = Run(TEMPO_112 + ";" + WRITE + f";INST+11;p {ALL16};press STOP;run 0.2;press PATT1;press 1;SHIFT+TAPESYNC;SHIFT+TAPESYNC;"
            "midiout all;log hits;dinclock 120 0.3;echo d;din run;dinclock 120 2;din stop;dinclock 120 0.5;echo c;din cont;dinclock 120 1;din stop;run 0.3")
    t = [h[0] for h in r.hits(start="d", end="c")]
    expect(len(t) == 16 and abs(statistics.mean(intervals(t)) - 125) < 0.5, f"DIN: {len(t)} steps")
    m = r.midi("d")
    f8 = sum(1 for t, b in m if b == 0xF8)
    expect([b for t, b in m if b != 0xF8][:3] == [0xFA, 0xFC, 0xFB] and f8 >= 72, f"DIN MIDI out {[hex(b) for t, b in m if b != 0xF8]}, {f8} F8")
    expect(len(r.hits(start="c")) == 8, "DIN continue")


@check("tape sync output", "SN p.6, OM p.35")
def _():
    # TO (PC4): 1202 Hz stopped (416 us half periods), 1202/2404 Hz alternating per 48 ppqn
    # tick while it plays.
    r = Run(TEMPO_112 + ";run 1;tape 0.5;" + "SHIFT+PATT1;CLEAR+1;press START;run 0.3;tape 1")
    h = [dict((int(a), int(b)) for a, b in re.findall(r"(\d+) us x(\d+)", s)) for t, k, s in r.lines if k == "TEXT" and s.startswith("TAPE SYNC")]
    stopped = max(h[0], key=h[0].get)
    play = sorted(h[1], key=h[1].get)[-2:]
    expect(412 <= stopped <= 420 and sorted(play)[0] in range(204, 213) and sorted(play)[1] in range(412, 421), f"tape sync half periods {h}")


@check("TAPE SYNC mode without a tape", "OM p.36")
def _():
    # TAPE SYNC lights its LED; START then waits for the tape's sync signal, so nothing plays;
    # STOP/CONT leaves the mode.
    r = Run(WRITE + ";INST+2;p 1 5 9 13;press STOP;run 0.2;press PATT1;press 1;press TAPESYNC;leds;log hits;press START;run 1.5;press STOP;run 0.2;leds")
    l = r.leds()
    expect("TAPESYNC" in l[0]["lit"] and not r.hits() and "TAPESYNC" not in l[-1]["lit"], f"TAPE SYNC: {l[0]['lit']}, {len(r.hits())} hits")


@check("tape SAVE, LOAD without a tape", "OM p.19-23")
def _():
    # SAVE (SHIFT+SAVE, ENTER) needs no tape: about 20 s of FSK on PB6-7 (the SAVE/OUT jack's
    # 2-bit DAC). LOAD waits for a pilot tone forever, MIDI frozen; key 1 cancels.
    r = Run("run 1;down SHIFT;run 0.05;p 14 ENTER;up SHIFT;run 0.1;tape 3;run 3;tape 6;run 9;tape 1;leds")
    d = [int(re.search(r"changes: (\d+)", s).group(1)) for t, k, s in r.lines if k == "TEXT" and "TAPE DATA" in s]
    expect(d[0] > 1000 and d[1] > 1000 and d[2] == 0, f"tape data changes {d}")
    r = Run(WRITE + ";INST+2;p 1;press STOP;run 0.2;press TRACK1;snap;down SHIFT;run 0.05;p 16 ENTER;up SHIFT;run 3;"
            "log hits;midi 99 24 64;run 0.3;echo c;press 1;run 1;diff;midi 99 24 64;run 0.2")
    expect(not r.hits(end="c") and len(r.hits(start="c")) == 1 and not r.ram(), f"LOAD: hits {r.hits()}, RAM {r.ram()[:3]}")


@check("foot switch", "OM p.38")
def _():
    r = Run(WRITE + ";INST+2;p 1;press STOP;run 0.2;press PATT1;press 1;echo f;foot down;run 0.2;foot up;run 1;foot down;run 0.2;foot up;run 0.3")
    m = [b for t, b in r.midi("f") if b != 0xF8]
    expect(m == [0xFA, 0xFC], f"foot switch MIDI {m}")


@check("EXT INST", "OM p.37, SN p.15")
def _():
    # SHIFT+EXT INST (pattern write, stopped) switches the keys to 16 external instruments
    # (2200/2300 rows); they play as notes 36-51 on the transmit channel (11), velocity 64, or
    # 64 + TOTAL ACCENT on an accented step; note-off (velocity 0) 5 ticks later.
    r = Run(TEMPO_112 + ";accent 0.5;run 1;SHIFT+PATT1;CLEAR+1;SHIFT+EXTINST;leds;press START;INST+1;p 1 5;INST+16;p 3;"
            "INST+ENTER;p 5;run 0.3;echo o;run 2.2;echo e")
    expect("EXTINST" in r.leds()[0]["lit"], "EXT INST LED")
    b = [x for t, x in r.midi("o", "e") if x != 0xF8]
    ms = [b[i:i + 3] for i in range(0, len(b) - 2, 3)]
    on = sorted({tuple(m) for m in ms if m[2]})
    expect(on == [(0x9A, 36, 64), (0x9A, 36, 80), (0x9A, 51, 64)], f"ext notes {ms[:6]}")
    t = [t for t, x in r.midi("o", "e") if x == 0x9A]
    gate = t[1] - t[0]
    expect(abs(gate - 5 * 11.1) < 1.5, f"gate {gate:.1f} ms")


@check("MIDI in: notes and velocity", "SN p.14-15")
def _():
    keys = {35: "BD", 36: "BD", 37: "RS", 38: "SD", 39: "HC", 40: "SD", 41: "LT", 42: "CH", 43: "LT", 44: "CH",
            45: "MT", 46: "OH", 47: "MT", 48: "HT", 49: "CR", 50: "HT", 51: "RD"}
    s = "run 1;log hits"
    for k in range(34, 53):
        s += f";echo n{k};midi 99 {k:02X} 64;run 0.05"
    s += ";echo v"
    for v in (1, 32, 64, 84, 86, 88, 100, 127):
        s += f";midi 99 24 {v:02X};run 0.05;midi 99 2A {v:02X};run 0.05"
    s += ";echo off;midi 89 24 40;run 0.05;midi 99 24 00;run 0.05"
    r = Run(s)
    got = {k: [n for t, n, c in r.hits(start=f"n{k}", end=f"n{k + 1}" if k < 52 else "v")] for k in range(34, 53)}
    want = {k: [keys[k]] if k in keys else [] for k in range(34, 53)}
    expect(got == want, f"note map {got}")
    vel = [(n, c) for t, n, c in r.hits(start="v", end="off")]
    # table 1F80 (BD .. HC) dips at velocity 86-87 (index 43 = 28h < index 42 = 30h); 1FC0 (HH,
    # CR, RD) is linear
    expect([c for n, c in vel if n == "BD"] == [2, 6, 14, 24, 20, 27, 37, 63] and [c for n, c in vel if n == "CH"] == [1, 16, 32, 42, 43, 44, 50, 63],
           f"velocity codes {vel}")
    expect(not r.hits(start="off"), "note-off or velocity 0 played")


@check("MIDI channels and modes", "OM p.38, SN p.14")
def _():
    # Power-on: receive omni (channel 10 set), transmit 11. SHUFFLE/FLAM + key sets the receive
    # channel (omni off); LAST STEP + key the transmit channel, which sends OMNI OFF and POLY on
    # it. Mode messages 124/125 switch omni.
    r = Run("run 1;log hits;echo a;midi 90 24 64;run 0.1;echo b;SHUFFLE+3;midi 99 24 64;run 0.1;midi 92 24 64;run 0.1;echo c;"
            "LAST+5;run 0.2;echo d;midi B2 7D 00;run 0.1;midi 90 24 64;run 0.1;echo e;midi B2 7C 00;run 0.1;midi 90 24 64;run 0.1")
    expect(len(r.hits(start="a", end="b")) == 1, "omni at power-on")
    expect(len(r.hits(start="b", end="c")) == 1, "receive channel 3")
    tx = [x for t, x in r.midi("c", "d") if x != 0xF8]
    expect(tx == [0xB4, 0x7C, 0x00, 0xB4, 0x7F, 0x00], f"transmit channel 5 sent {[hex(x) for x in tx]}")
    expect(len(r.hits(start="d", end="e")) == 1 and not r.hits(start="e"), "OMNI ON / OFF")


@check("MIDI thru, song select, song position", "SN p.15")
def _():
    r = Run(track6() + ";echo a;midi 99 24 64;run 0.1;echo b;press TRACK2;press TRACK1;SHIFT+AVAIL;press TRACK3;SHIFT+FWD;press TRACK1;"
            "echo c;down SHIFT;run 0.05;p 4 ENTER;up SHIFT;run 0.2;echo d;midi F3 01;run 0.1;leds;midi F3 00;run 0.1;midi F2 30 00;run 0.1;"
            "display;log hits;midi FB;run 4;midi FC;run 0.2")
    th = [x for t, x in r.midi("a", "b") if x != 0xF8]
    expect(th == [0x99, 0x24, 0x64], f"thru {th}")
    ss = [x for t, x in r.midi("b", "c") if x != 0xF8]
    expect(ss == [0xF3, 1, 0xF3, 0, 0xF3, 4, 0xF3, 6, 0xF3, 2, 0xF3, 0], f"song select out {[hex(x) for x in ss]}")
    sp = [x for t, x in r.midi("c", "d") if x != 0xF8]
    expect(sp == [0xF2, 0x30, 0x00], f"song position out {sp}")
    expect("TRACK2" in r.leds()[-1]["lit"], "song select in")
    expect([n for t, n, c in r.hits()][:2] == ["MT", "BD"], "song position + continue in")


@check("song position: measure lengths, pattern play", "SN p.15")
def _():
    # In MIDI sync, track play: F2 goes to that 16th of the track, each measure at its pattern's
    # length. With measure 1 (BD) cut to 12 steps, F2 12 + FB starts measure 2 (SD) at once and
    # F2 16 starts it at its 5th step (LT twelve 16ths later, 1.5 s at 120 BPM). Pattern play
    # ignores F2: FB goes on from where the pattern stopped (step 9 after eight steps).
    def first(r):
        go = next(t for t, k, s in r.lines if k == "ECHO" and s.strip() == "go")
        return [(t - go, n) for t, n, c in r.hits(start="go")][:1]
    short = track6().replace("INST+2;p 1;", "INST+2;p 1;LAST+12;", 1) + ";SHIFT+TAPESYNC;press TRACK1;run 0.3;log hits"
    a = first(Run(short + ";midi F2 0C 00;run 0.05;echo go;midiclock 120 2 cont"))
    b = first(Run(short + ";midi F2 10 00;run 0.05;echo go;midiclock 120 2 cont"))
    expect(a and a[0][1] == "SD" and a[0][0] < 15, f"F2 12 after a 12-step measure: {a}")
    expect(b and b[0][1] == "LT" and abs(b[0][0] - 1500) < 15, f"F2 16 after a 12-step measure: {b}")
    c = first(Run(three_patterns() + ";press PATT1;press 1;SHIFT+TAPESYNC;run 0.3;midiclock 120 0.9 start;midi FC;run 0.3;"
                  "log hits;midi F2 00 00;run 0.05;echo go;midiclock 120 2 cont"))
    expect(c and c[0][1] == "BD" and 950 < c[0][0] < 1150, f"pattern play F2 0 + FB: {c}")


@check("system exclusive bank dump", "SN p.16")
def _():
    # F0 41 51 F7 asks for the current bank: 16 blocks F0 41 52 01 4n <512 nibbles, low first>
    # <checksum> F7, each followed by F0 41 54 F7 (55 after the last) and acknowledged with
    # F0 41 53 F7. Checksum: the 7-bit two's complement of the nibbles' sum. Sending it back
    # (F0 41 50 F7, answered 51) restores the bank.
    s = TEMPO_112 + ";" + WRITE + ";INST+2;p 1 3;press STOP;run 0.2;press TRACK1;run 0.2;echo d;midi F0 41 51 F7;run 0.3"
    for i in range(16):
        s += ";midi F0 41 53 F7;run 0.3"
    s += ";ram 2000 4"
    r = Run(s)
    b = [x for t, x in r.midi("d") if x != 0xF8]
    msgs, cur = [], None
    for x in b:
        if x == 0xF0:
            cur = [x]
        elif cur is not None:
            cur.append(x)
            if x == 0xF7:
                msgs.append(cur)
                cur = None
    blocks = [m for m in msgs if len(m) == 519]
    trail = [m[2] for m in msgs if len(m) == 4]
    expect(len(blocks) == 16 and trail == [0x54] * 15 + [0x55], f"{len(blocks)} blocks, trailers {trail}")
    for m in blocks:
        nib = m[5:517]
        expect(m[:5] == [0xF0, 0x41, 0x52, 0x01, 0x40 + blocks.index(m)] and (sum(nib) + m[517]) & 0x7F == 0, "block header or checksum")
    data = bytes(nib[2 * i] | nib[2 * i + 1] << 4 for m in blocks[:1] for nib in [m[5:517]] for i in range(256))
    expect(data[:4] == bytes([2, 0, 2, 0]), f"block 0 data {data[:4]}")
    load = ["run 1", "midi F0 41 50 F7", "run 0.2"]
    for m in msgs:
        load.append("midi " + " ".join(f"{x:02X}" for x in m))
        if len(m) == 4:
            load.append("run 0.3")
    load += ["run 0.3", "ram 2000 4"]
    r2 = Run(";".join(load) + ";echo end")
    acks = [x for t, x in r2.midi() if x != 0xF8]
    expect(acks[:4] == [0xF0, 0x41, 0x51, 0xF7] and acks.count(0x53) == 16, f"load answers {acks[:12]}")
    expect([r2.dump()[0x2000 + i] for i in range(4)] == [2, 0, 2, 0], "loaded bank differs")


@check("factory patterns", "SN p.7, OM p.7, core/factory.h")
def _():
    if not os.path.exists(V1) or not os.path.exists(ARGS.factory):
        return "skip"
    img = os.path.join(os.environ.get("TMPDIR", "/tmp"), "tr909-factory-test.bin")
    p = subprocess.run([ARGS.factory, "--v1", V1, "--out", img, "--v4", V4], capture_output=True, text=True)
    expect(p.returncode == 0, p.stdout[-400:])
    # A new 909: power on and press START: track 1 of Bank I plays (measure 1 is pattern 5).
    r = Run("run 1;display;leds;log hits;press START;run 3", ram=img)
    l = r.leds()[0]
    expect(l["display"].strip() == "1" and len(r.hits()) > 10, f"factory song: display {l['display']}, {len(r.hits())} hits")
    v4 = Run("run 1;press TRACK1;down TEMPO;run 0.1;display;up TEMPO;TEMPO+TRACK1;down TEMPO;run 0.1;display;up TEMPO", ram=img)
    d = [s.strip("[]") for t, k, s in v4.lines if k == "DISPLAY"]
    expect(d[1] == "110", f"factory track 1 tempo memory {d}")
    # v4.0 ignores TRACK 1 + PATTERN 1 at power-on (its loader is a RET): the memory stays empty.
    r = Run("down TRACK1 PATT1;run 2;up TRACK1 PATT1;run 0.5;ram 2000 10")
    expect(all(v == 0 for v in r.dump().values()), "v4.0 loaded something")
    os.remove(img)
    return p.stdout.strip().splitlines()[-1]


@check("MIDI note latency", "research C.4")
def _():
    # A note-on goes out on the next scan of either group: 0.45-2.2 ms after the message ends.
    import random
    random.seed(2)
    s = "run 1;slice 10;log hits"
    for i in range(200):
        s += f";run {random.uniform(0.02, 0.06):.5f};echo n{i};midi 99 {0x24 if i % 2 else 0x2A:02X} 64"
    r = Run(s)
    lat, last = [], None
    for t, k, x in r.lines:
        if k == "ECHO":
            last = t
        elif k == "HIT" and last is not None:
            lat.append(t - last - 0.96)
            last = None
    expect(len(lat) >= 195 and 0.3 < min(lat) and max(lat) < 2.4, f"{len(lat)} notes, latency {min(lat):.2f}-{max(lat):.2f} ms")
    return f"{min(lat):.2f}-{max(lat):.2f} ms after the message, mean {statistics.mean(lat):.2f}"


@check("accent codes", "OM p.25, docs/firmware.md accent")
def _():
    # Each voice: normal on steps 1 and 9, accent on 5 and 13 (BD-HT, CH), TOTAL ACCENT on 9
    # and 13; the 6-bit DAC codes at TOTAL ACCENT 0 and full (A/D >> 3 = 31).
    voices = {"BD": ("2", "1"), "SD": ("4", "3"), "LT": ("6", "5"), "MT": ("8", "7"), "HT": ("10", "9"), "CH": ("14", "13"),
              "RS": ("11", None), "HC": ("12", None), "OH": (None, None), "CR": ("15", None), "RD": ("16", None)}
    want = {0: {"BD": [10, 21], "CH": [25, 40], "RS": [21], "HC": [21], "OH": [32], "CR": [32], "RD": [32]},
            1: {"BD": [10, 21, 47], "CH": [25, 40, 55], "RS": [21, 47], "HC": [21, 47], "OH": [32, 47], "CR": [32, 47], "RD": [32, 47]}}
    for v in ("SD", "LT", "MT", "HT"):
        want[0][v], want[1][v] = want[0]["BD"], want[1]["BD"]
    got = {0: {}, 1: {}}
    for acc in (0, 1):
        for v, (n, a) in voices.items():
            sel = "down INST;run 0.05;pair 13 14;up INST;run 0.1" if v == "OH" else f"INST+{n}"
            w = sel + ";p 1 9" + (";p 5 5 13 13" if a else "")
            r = Run(f"{TEMPO_112};accent {acc};" + WRITE + f";{w};INST+ENTER;p 9 13;run 0.5;log hits;run 2.2")
            got[acc][v] = sorted({c for t, n2, c in r.hits(v)})
    expect(got == want, f"codes {got}")
    return "normal/accent: BD-HT 10/21, CH 25/40, RS HC 21, OH CR RD 32; with full TOTAL ACCENT 21/47, 40/55, 47, 47"


@check("tape VERIFY without a tape", "OM p.21")
def _():
    r = Run("run 1;press TRACK1;snap;down SHIFT;run 0.05;p 15 ENTER;up SHIFT;run 2;leds;log hits;midi 99 24 64;run 0.3;echo c;press 1;run 1;diff;midi 99 24 64;run 0.2")
    expect(not r.hits(end="c") and len(r.hits(start="c")) == 1 and not r.ram(), "VERIFY")


@check("MIDI clock latency", "research C.6")
def _():
    # Steps follow their F8 (or, for odd 48 ppqn ticks, the extrapolated half-clock) by 0.8-5 ms.
    s = TEMPO_112 + ";" + WRITE + f";INST+11;p {ALL16};SHUFFLE+4;press STOP;run 0.2;press PATT1;press 1;SHIFT+TAPESYNC;run 0.5;slice 10;log hits"
    P = 60000.0 / 120 / 24
    s += f";echo s;midi FA;run {P / 1000:.6f}"
    for i in range(144):
        s += f";echo c{i};midi F8;run {P / 1000:.6f}"
    r = Run(s)
    clocks = [t for t, k, x in r.lines if k == "ECHO" and x.startswith("c")]
    h = [t for t, n, c in r.hits()]
    lat = []
    for k, t in enumerate(h):
        tick = 12 * k + (3 if k % 2 else 0)
        if tick // 2 < len(clocks):
            lat.append(t - (clocks[tick // 2] + 0.32 + (P / 2 if tick % 2 else 0)))
    expect(len(lat) >= 20 and 0.5 < min(lat) and max(lat) < 5.5, f"latency {min(lat):.2f}-{max(lat):.2f} ms")
    return f"{min(lat):.2f}-{max(lat):.2f} ms after the clock, mean {statistics.mean(lat):.2f}"


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("--seq", default=seqrun.SEQ)
    ap.add_argument("--factory", default=None)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("names", nargs="*")
    ARGS = ap.parse_args()
    if ARGS.factory is None:
        ARGS.factory = os.path.join(os.path.dirname(ARGS.seq), "tr909factory")
    seqrun.SEQ, seqrun.VERBOSE = ARGS.seq, ARGS.verbose
    if not os.path.exists(ARGS.seq):
        sys.exit(f"tests.py: no tr909seq at {ARGS.seq} (--seq)")
    failed = skipped = passed = 0
    for name, ref, f in CHECKS:
        if ARGS.names and not any(n in name for n in ARGS.names):
            continue
        try:
            r = f()
            if r == "skip":
                skipped += 1
                print(f"skip  {name}")
                continue
            passed += 1
            print(f"ok    {name}  [{ref}]" + (f"  {r}" if r else ""))
        except Fail as e:
            failed += 1
            print(f"FAIL  {name}  [{ref}]: {e}")
    print(f"{passed} passed, {failed} failed, {skipped} skipped")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
