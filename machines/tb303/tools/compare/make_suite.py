#!/usr/bin/env python3
"""Writes specs/suite.json: the test suite rendered by both Roland's plugin and ours.

Each test isolates one part of the voice. Knob values are 0-1 pot positions.
"""
import json, os

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = {"waveform": 0, "tuning": 0.5, "cutoff": 0.5, "resonance": 0.0, "envmod": 0.0,
        "decay": 0.5, "accent": 0.5, "volume": 0.6}
suite = []


def test(name, notes, lines, bpm=120, lpb=4, **params):
    p = dict(BASE); p.update(params)
    suite.append({"name": name, "bpm": bpm, "lpb": lpb, "params": p, "notes": notes, "lines": lines})


def held(note="A-2", length=8, accent=False):
    return [{"line": 0, "note": note, "len": length, "accent": accent}]


# Static filter: a held note with no envelope, cutoff and resonance swept.
for c in (0.0, 0.25, 0.5, 0.75, 1.0):
    for r in (0.0, 0.5, 1.0):
        test(f"static-saw-c{int(c*100):03d}-r{int(r*100):03d}", held(), 12, cutoff=c, resonance=r)
for c in (0.0, 0.5, 1.0):
    test(f"static-sqr-c{int(c*100):03d}-r000", held(), 12, waveform=1, cutoff=c)

# Raw oscillator: filter fully open, at several pitches (waveshape, tracking).
for w in (0, 1):
    for n in ("C-1", "C-2", "C-3", "C-4", "C-5"):
        test(f"osc-{'saw' if w == 0 else 'sqr'}-{n.replace('-', '')}", held(n, 4), 8,
             waveform=w, cutoff=1.0)
# Tuning knob range.
for t in (0.0, 1.0):
    test(f"tuning-{int(t*100):03d}", held("C-3", 4), 8, tuning=t, cutoff=1.0)

# Long gate: VCA envelope decay while held and release at gate off.
test("vca-long", held("A-2", 16), 24, bpm=60, cutoff=1.0)

# Filter envelope: env mod and decay.
for d in (0.0, 0.5, 1.0):
    for e in (0.5, 1.0):
        test(f"env-d{int(d*100):03d}-e{int(e*100):03d}", held("A-2", 8), 12, cutoff=0.1,
             envmod=e, decay=d, resonance=0.5)

# Accent: single accented vs plain note, accent knob swept, resonance swept.
for a in (0.0, 0.5, 1.0):
    for r in (0.0, 1.0):
        test(f"acc-single-a{int(a*100):03d}-r{int(r*100):03d}",
             [{"line": 0, "note": "A-2", "len": 2}, {"line": 8, "note": "A-2", "len": 2, "accent": True}],
             16, cutoff=0.2, envmod=0.5, decay=0.5, accent=a, resonance=r)
# Consecutive accents: the accent sweep capacitor builds up.
for r in (0.0, 0.5, 1.0):
    test(f"acc-run-r{int(r*100):03d}",
         [{"line": i, "note": "A-2", "len": 1, "accent": True} for i in range(0, 16, 2)],
         20, cutoff=0.2, envmod=0.5, decay=0.3, accent=1.0, resonance=r)

# Slide: up and down an octave, and a fifth, with and without accent.
for pair, name in ((("C-2", "C-3"), "oct-up"), (("C-3", "C-2"), "oct-down"), (("C-2", "G-2"), "fifth")):
    test(f"slide-{name}",
         [{"line": 0, "note": pair[0], "len": 2, "slide": True}, {"line": 4, "note": pair[1], "len": 4}],
         12, cutoff=0.6, envmod=0.3, decay=0.5, resonance=0.3)

# Gate length (sequencer-style half-step gates vs ties).
test("gate-16ths", [{"line": i, "note": "A-2", "len": 1} for i in range(0, 32, 2)], 36, lpb=8,
     cutoff=0.3, envmod=0.6, decay=0.4, resonance=0.6)

# Volume knob.
for v in (0.25, 0.5, 0.75, 1.0):
    test(f"volume-{int(v*100):03d}", held("A-2", 4), 8, cutoff=1.0, volume=v)

# A classic acid line, for listening.
acid = [("C-2", 0, 0), ("C-2", 1, 0), ("C-3", 0, 1), ("C-2", 0, 0), ("D#2", 1, 0), ("C-2", 0, 1),
        ("G-2", 0, 0), ("C-3", 1, 0), ("C-2", 0, 0), ("A#2", 0, 1), ("C-2", 1, 0), ("F-2", 0, 0),
        ("C-2", 0, 1), ("D#3", 1, 0), ("C-2", 0, 0), ("G-2", 0, 0)]
notes = []
for rep in range(2):
    for i, (n, a, s) in enumerate(acid):
        notes.append({"line": (rep * 16 + i) * 2, "note": n, "len": 1, "accent": bool(a), "slide": bool(s)})
for name, kw in (("acid-saw", dict(cutoff=0.25, resonance=0.85, envmod=0.6, decay=0.4, accent=0.8)),
                 ("acid-sqr", dict(waveform=1, cutoff=0.3, resonance=0.75, envmod=0.5, decay=0.5, accent=0.7))):
    test(name, notes, 68, bpm=130, lpb=8, **kw)

os.makedirs(os.path.join(HERE, "specs"), exist_ok=True)
with open(os.path.join(HERE, "specs", "suite.json"), "w") as f:
    json.dump(suite, f, indent=1)
print(len(suite), "tests")
