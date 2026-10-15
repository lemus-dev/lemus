#!/usr/bin/env python3
"""Builds an Onyx Tracker song for A/B listening: TB-303 patterns on Lemus (track 1) and on
Roland's TB-303 software (track 2), with the same notes and knob positions. Each pattern gets two
blocks, Lemus then Roland, each looping the pattern --repeat times, in sequence; with --together
both play in one block (mute one track to compare).

Usage: ab_song.py OUT.otk --ours-from SONG.otk:SLOT NAME=PATTERN [NAME=PATTERN ...]
           [--bpm 125] [--repeat 4] [--together] [--cutoff 0.3] [--resonance 0.8] ...

--ours-from names a saved Onyx song and the instrument slot in it that holds Lemus with the TB-303
selected (load Lemus into a slot, pick the TB-303, save); its state is copied into the new song's
instrument 01. Roland's comes from private/reference/roland-vst/base-simple-saw.otk (instrument
02: the "Simple Saw" patch, KEYBOARD on, POSITION LOCK off).

PATTERN is the 303's own step format, one token per sixteenth: KEY[flags][:TIME]
(flags u = TRANSPOSE UP, d = DOWN, a = ACCENT, s = SLIDE; TIME N note, T tie, R rest),
the same text core/patterns.h reads. The pattern loops; a slide on the last step slides
into the first.

How it is written: 8 lines per beat (one 303 step = 2 lines, gate = 1 line, as the firmware's
3 of 6 clocks); velocity 127 for accents and 64 otherwise (both plugins accent at >= 100).
A slide must overlap the next note, which one tracker column cannot do, so each track gets a
second note column and notes alternate between the two. Onyx plays a line's extra columns
before its main column, so: a main-column note that slides is released on the line its
(extra-column) successor starts; an extra-column note that slides is released one line after
its (main-column) successor starts. Ties become longer notes. Extra-column notes take their
velocity from the main column's 0Cxx on the same line.
"""
import base64, json, os, subprocess, sys

OTK = os.path.expanduser("~/src/onyx-tracker/build/Build/Products/Release/otk")
HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.normpath(os.path.join(HERE, "../../private/reference/roland-vst/base-simple-saw.otk"))
KEYS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C2"]
NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
MAX_SUB = 7     # kMaxSubNotes in Onyx
TRACKS = 4
LINES_PER_STEP = 2

KNOBS = {"cutoff": 0.30, "resonance": 0.80, "envmod": 0.60, "decay": 0.40, "accent": 0.80,
         "tuning": 0.50, "volume": 0.60, "waveform": 0.0}
OUR_PARAMS = {"waveform": "Waveform", "tuning": "Tuning", "cutoff": "Cut Off Freq", "resonance": "Resonance",
              "envmod": "Env Mod", "accent": "Accent", "decay": "Decay", "volume": "Volume"}
ROLAND_PARAMS = {"waveform": "VCO WAVEFORM", "tuning": "VCO TUNING", "cutoff": "VCF CUTOFF",
                 "resonance": "VCF RESONANCE", "envmod": "VCF ENV MOD", "accent": "VCF ACCENT",
                 "decay": "VCF DECAY", "volume": "VCA VOLUME"}


def otk(*args, stdin=None):
    r = subprocess.run([OTK, *args], input=stdin, capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        raise RuntimeError(f"otk {' '.join(args)}: {r.stdout}{r.stderr}")
    return r.stdout


def parse(text):
    steps = []
    for tok in text.split():
        key, _, time = tok.partition(":")
        flags = ""
        while key and key[-1].islower():
            flags += key[-1]
            key = key[:-1]
        time = time or "N"
        if key == "-":
            time = "R"
        steps.append({"key": KEYS.index(key) if key in KEYS else 0, "up": "u" in flags, "down": "d" in flags,
                      "accent": "a" in flags, "slide": "s" in flags, "time": time})
    return steps


def notes_of(steps):
    """Collapse ties: list of notes {midi, step, steps (length), accent, slide}. C on the panel
    is C2 (MIDI 36); UP/DOWN shift an octave."""
    notes = []
    for i, s in enumerate(steps):
        if s["time"] == "R":
            continue
        if s["time"] == "T" and notes and notes[-1]["step"] + notes[-1]["steps"] == i:
            notes[-1]["steps"] += 1
            continue
        midi = 36 + s["key"] + (12 if s["up"] else 0) - (12 if s["down"] else 0)
        notes.append({"midi": midi, "step": i, "steps": 1, "accent": s["accent"], "slide": s["slide"]})
    # A slide only joins notes on consecutive steps (no rest between); the last note's slide
    # reaches the first note of the next pass.
    n = len(steps)
    for k, nt in enumerate(notes):
        nxt = notes[(k + 1) % len(notes)]
        nt["next"] = nxt
        end = nt["step"] + nt["steps"]
        nt["slide"] = nt["slide"] and (nxt["step"] == end % n)
    return notes


def note_name(midi):
    return f"{NAMES[midi % 12]}{midi // 12 - 1}"


def build(steps):
    """Returns (main[line] = cell text, sub[line] = note byte) for one track and its block length."""
    n = len(steps)
    lines = n * LINES_PER_STEP
    notes = notes_of(steps)
    main, sub = {}, {}
    for k, nt in enumerate(notes):
        nt["col"] = "main" if k % 2 == 0 else "sub"
    if len(notes) % 2 == 1 and notes[-1]["slide"]:
        raise SystemExit("odd number of notes with a wrap-around slide: column alternation breaks at the loop")
    for nt in notes:
        start = nt["step"] * LINES_PER_STEP
        vel = 0x7F if nt["accent"] else 0x40
        if nt["col"] == "main":
            main.setdefault(start, {})["note"] = note_name(nt["midi"])
            main[start]["vel"] = vel
        else:
            sub[start] = nt["midi"] + 1
            main.setdefault(start, {})["vel"] = vel
        # release
        gate_end = start + nt["steps"] * LINES_PER_STEP - 1
        if nt["slide"]:
            nxt_start = nt["next"]["step"] * LINES_PER_STEP
            off = nxt_start if nt["col"] == "main" else nxt_start + 1
        else:
            off = gate_end
        off %= lines
        if nt["col"] == "main":
            main.setdefault(off, {}).setdefault("note", "OFF")
        else:
            sub.setdefault(off, 0xFF)
    return main, sub, lines


def parse_opts(argv):
    opts, rest, flags = {}, [], set()
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--together",):
            flags.add(a[2:])
            i += 1
        elif a.startswith("--"):
            opts[a[2:]] = argv[i + 1]
            i += 2
        else:
            rest.append(a)
            i += 1
    return opts, rest, flags


def cell_rows(cells, inst):
    rows = []
    for line in sorted(cells):
        c = cells[line]
        text = f"{c.get('note', '---')} {inst} 0C{c.get('vel', 0x40):02X}" if "vel" in c else f"{c['note']} {inst}"
        rows.append(f"{line:03d} | {text}")
    return "\n".join(rows) + "\n"


def repeated(cells, subs, lines, times):
    rc, rs = {}, {}
    for k in range(times):
        for line, c in cells.items():
            rc[line + k * lines] = c
        for line, v in subs.items():
            rs[line + k * lines] = v
    return rc, rs, lines * times


def main():
    opts, rest, flags = parse_opts(sys.argv[1:])
    out = os.path.abspath(rest[0])
    patterns = [r.split("=", 1) for r in rest[1:]]
    src, slot = opts.pop("ours-from").rsplit(":", 1)
    bpm = int(opts.pop("bpm", 125))
    times = int(opts.pop("repeat", 4))
    together = "together" in flags
    knobs = dict(KNOBS)
    for k, v in opts.items():
        knobs[k] = float(v)
    ours = next(i for i in json.load(open(src))["instruments"] if i["slot"] == int(slot))
    if ours["plugin"]["name"].lower() != "lemus":
        raise SystemExit(f"slot {slot} of {src} holds {ours['plugin']['name']}, not Lemus")

    # One block per pattern and plugin (or per pattern), each looping the pattern.
    blocks = []   # (name, track numbers, cells, subs, lines)
    for name, text in patterns:
        cells, subs, lines = repeated(*build(parse(text)), times)
        if together:
            blocks.append((name, (1, 2), cells, subs, lines))
        else:
            blocks.append((f"{name} Lemus", (1,), cells, subs, lines))
            blocks.append((f"{name} Roland", (2,), cells, subs, lines))

    otk("open", BASE, "--force")
    otk("set", "tempo", str(bpm))
    otk("set", "lpb", "8")
    otk("set", "tpl", "6")
    otk("set", "tracks", str(TRACKS))
    otk("instrument", "02", "velocity=64")
    for b, (name, tracks, cells, subs, lines) in enumerate(blocks):
        if b > 0:
            otk("block", "new", "--lines", str(lines), "--name", name)
        else:
            otk("block", "lines", "00", str(lines))
            otk("block", "name", "00", name)
        otk("clear", f"{b:02d}", "--tracks", f"1-{TRACKS}")
        for track in tracks:
            otk("write", f"{b:02d}", "--track", str(track), stdin=cell_rows(cells, "01" if track == 1 else "02"))
    otk("sequence", *[f"{b:02d}" for b in range(len(blocks))])
    otk("track", "1", "name=Lemus TB-303")
    otk("track", "2", "name=Roland TB-303")
    otk("save", out)

    # Patch the saved song: Lemus as instrument 01, and the second note column on tracks 1-2.
    song = json.load(open(out))
    song["instruments"] = [i for i in song["instruments"] if i["slot"] != 1] + [dict(ours, slot=1, name="Lemus TB-303", velocity=64)]
    song["subColumns"] = {"0": 1, "1": 1}
    nt = int(song["tracks"])
    for b, (name, tracks, cells, subs, lines) in enumerate(blocks):
        raw = bytearray(lines * nt * MAX_SUB)
        for line, v in subs.items():
            for t in tracks:
                raw[(line * nt + t - 1) * MAX_SUB + 0] = v
        song["blocks"][b]["subNotes"] = base64.b64encode(bytes(raw)).decode()
    json.dump(song, open(out, "w"), indent=2)
    otk("open", out, "--force")
    otk("param", "01", *sum(([OUR_PARAMS[k], f"{v:.4f}"] for k, v in knobs.items()), []))
    otk("param", "02", *sum(([ROLAND_PARAMS[k], f"{v:.4f}"] for k, v in knobs.items()), []))
    otk("save", out)
    print(out)
    print(otk("status"))


if __name__ == "__main__":
    main()
