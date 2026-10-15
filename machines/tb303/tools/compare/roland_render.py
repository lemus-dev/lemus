#!/usr/bin/env python3
"""Render a test spec through Roland's TB-303 VST3 (or ours: --plugin ours).

Usage: roland_render.py SPEC.json [SPEC.json ...] [--out DIR] [--plugin roland|ours] [--only NAME,...]
                        [--via host|onyx] [--jobs N]

By default it plays the specs through lemus-render (tools/host), headless and in parallel, with
Roland's plugin in the state of private/reference/roland-vst/base-simple-saw.otk (the "Simple
Saw" patch, KEYBOARD on, POSITION LOCK TO DAW off). Notes go out as Onyx sends them (at equal
times note-ons first, so a slid note held to the next one is legato), and the renders match the
Onyx references (renders/, 2026-10-06) in level. --via onyx drives Onyx Tracker through otk
instead (one render at a time; the app must be free).

Spec format (shared with tb303render, see tools/compare/README.md):
  {"name": "acc-basic", "bpm": 120, "lpb": 4,
   "params": {"waveform": 0, "tuning": 0.5, "cutoff": 0.3, "resonance": 0.5,
              "envmod": 0.6, "decay": 0.5, "accent": 1.0, "volume": 0.6},
   "notes": [{"line": 0, "note": "C-3", "len": 2, "accent": false, "slide": false}, ...],
   "lines": 32}
A note with "slide": true overlaps the next note (MIDI legato), as the 303 holds the gate
into a slid step. Accent is velocity 127, otherwise velocity 64.
"""
import json, os, shutil, subprocess, sys, tempfile

OTK = os.path.expanduser("~/src/onyx-tracker/build/Build/Products/Release/otk")
HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.normpath(os.path.join(HERE, "../../private/reference/roland-vst/base-simple-saw.otk"))

PARAMS = {
    "waveform": "VCO WAVEFORM", "tuning": "VCO TUNING", "cutoff": "VCF CUTOFF",
    "resonance": "VCF RESONANCE", "envmod": "VCF ENV MOD", "accent": "VCF ACCENT",
    "decay": "VCF DECAY", "volume": "VCA VOLUME",
}
# Our plugin, loaded fresh into a new song (--plugin ours).
OURS = "TB-303 Bass Line"
OUR_PARAMS = {
    "waveform": "Waveform", "tuning": "Tuning", "cutoff": "Cut Off Freq", "resonance": "Resonance",
    "envmod": "Env Mod", "accent": "Accent", "decay": "Decay", "volume": "Volume",
}
PLUGIN = "roland"
TRACKS = 16  # Onyx caps a song at 16 tracks


def otk(*args, stdin=None):
    r = subprocess.run([OTK, *args], input=stdin, capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        raise RuntimeError(f"otk {' '.join(args)}: {r.stdout}{r.stderr}")
    return r.stdout


def cells(spec):
    """Each note gets its own track, counting down, so a note that starts on the line where
    the previous note ends is sent before that note's OFF (tracks play in order)."""
    rows = {}
    notes = sorted(spec["notes"], key=lambda n: n["line"])
    for i, n in enumerate(notes):
        track = TRACKS - (i % TRACKS)
        vel = n.get("vel", 0x7F if n.get("accent") else 0x40)
        start, end = n["line"], n["line"] + n.get("len", 1)
        if n.get("slide") and i + 1 < len(notes):
            end = max(end, notes[i + 1]["line"])  # held until the next note has started
        rows.setdefault(start, {})[track] = f"{n['note']} 02 0C{vel:02X}"
        rows.setdefault(end, {}).setdefault(track, "OFF")
    return rows


def render(spec, out_dir):
    if PLUGIN == "ours":
        otk("new", "--force")
        otk("load", "02", OURS)
        otk("unload", "01")
    else:
        otk("open", BASE, "--force")
    otk("set", "tempo", str(spec.get("bpm", 120)))
    otk("set", "lpb", str(spec.get("lpb", 4)))
    otk("set", "tpl", "6")
    otk("set", "tracks", str(TRACKS))
    p = spec.get("params", {})
    args = []
    names = OUR_PARAMS if PLUGIN == "ours" else PARAMS
    for k, v in p.items():
        args += [names[k], f"{float(v):.6f}"]
    if args:
        otk("param", "02", *args)
    rows = cells(spec)
    lines = max(spec.get("lines", 0), max(rows) + 1)
    otk("block", "lines", "00", str(lines))
    otk("clear", "00", "--tracks", f"1-{TRACKS}")
    text = []
    for line in sorted(rows):
        for track, cell in sorted(rows[line].items()):
            text.append((line, track, cell))
    for line, track, cell in text:
        otk("write", "00", "--line", str(line), "--track", str(track), cell)
    out = os.path.join(out_dir, spec["name"] + ".wav")
    otk("export", out, "--bits", "32")
    return out


LEMUS_RENDER = os.path.normpath(os.path.join(HERE, "../../../../build/cmake/lemus-render_artefacts/Release/lemus-render"))
def scene_for(spec):
    """The spec as a lemus-render scene: lines become beats, a slid note is held to the next."""
    lpb = spec.get("lpb", 4)
    notes = sorted(spec["notes"], key=lambda n: n["line"])
    out = []
    for i, n in enumerate(notes):
        start, end = n["line"], n["line"] + n.get("len", 1)
        if n.get("slide") and i + 1 < len(notes):
            end = max(end, notes[i + 1]["line"])
        out.append({"at": start / lpb, "len": (end - start) / lpb, "note": n["note"],
                    "vel": n.get("vel", 0x7F if n.get("accent") else 0x40)})
    use = "tb303.lemus" if PLUGIN == "ours" else "tb303.roland"
    return {"name": spec["name"], "bpm": spec.get("bpm", 120), "rate": 44100,
            "beats": spec.get("lines", 0) / lpb, "tail": 2.5, "params": spec.get("params", {}),
            "onsFirst": True, "notes": out, "sources": [{"use": use, "label": "out"}]}


def render_host(specs, out_dir, jobs):
    """All the specs through lemus-render, `jobs` processes at once; returns the WAV paths."""
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for spec in specs:
            path = os.path.join(tmp, spec["name"] + ".json")
            json.dump(scene_for(spec), open(path, "w"))
            paths.append(path)
        chunks = [paths[i::jobs] for i in range(jobs)]
        procs = [subprocess.Popen([LEMUS_RENDER, *c, "--out", tmp], stdout=subprocess.DEVNULL) for c in chunks if c]
        if any(p.wait() != 0 for p in procs):
            raise RuntimeError("lemus-render failed")
        done = []
        for spec in specs:
            dst = os.path.join(out_dir, spec["name"] + ".wav")
            shutil.move(os.path.join(tmp, spec["name"] + "-out.wav"), dst)
            done.append(dst)
        return done


def main():
    argv = sys.argv[1:]
    via, jobs = "host", os.cpu_count() or 4
    if "--via" in argv:
        i = argv.index("--via"); via = argv[i + 1]; del argv[i:i + 2]
    if "--jobs" in argv:
        i = argv.index("--jobs"); jobs = int(argv[i + 1]); del argv[i:i + 2]
    out_dir = os.path.normpath(os.path.join(HERE, "../../private/reference/roland-vst/renders"))
    if "--out" in argv:
        i = argv.index("--out"); out_dir = argv[i + 1]; del argv[i:i + 2]
    global PLUGIN
    if "--plugin" in argv:
        i = argv.index("--plugin"); PLUGIN = argv[i + 1]; del argv[i:i + 2]
    only = None
    if "--only" in argv:
        i = argv.index("--only"); only = argv[i + 1].split(","); del argv[i:i + 2]
    os.makedirs(out_dir, exist_ok=True)
    todo = []
    for path in argv:
        specs = json.load(open(path))
        todo += [s for s in (specs if isinstance(specs, list) else [specs])
                 if not only or any(o in s["name"] for o in only)]
    if via == "host":
        for path in render_host(todo, out_dir, jobs):
            print(path, flush=True)
    else:
        for spec in todo:
            print(render(spec, out_dir), flush=True)


if __name__ == "__main__":
    main()
