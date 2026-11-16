#!/usr/bin/env python3
"""Render test specs through Roland's SH-101 plug-in (VST3, Roland Cloud) hosted in Onyx Tracker,
driven by its `otk` CLI. The same specs render through our plugin with sh101render.

Usage: roland_render.py SPEC.json [SPEC.json ...] [--out DIR] [--only NAME,...] [--force-ours-only]
                        [--keep-stereo] [--dry]
  --out DIR     where <name>.wav goes (default private/reference/roland-vst/renders); absolute or relative
  --only        render only specs whose name contains one of the substrings
  --dry         print the Roland parameters, note rows and warnings, render nothing
Writes DIR/<name>.wav (mono 32-bit float at Onyx's rate, 44.1 kHz here, trimmed to the spec's
length) and updates DIR/manifest.json with each render's checks (peak, clipped samples, onset
delay of the first note, silence) and the Roland parameter values used.

Run with the analysis venv's python. Onyx Tracker must be running; only one process may drive it.

Spec format (shared with tools/sh101render/sh101render.cpp):
  {"name": "saw-sweep", "bpm": 120, "lpb": 4, "lines": 32, "tail": 2.0,
   "params": {"sawLevel": 1, "vcfFreq": 0.4, "lfoWave": 1, ...},      our parameter IDs
   "notes": [{"line": 0, "note": "C-3", "len": 2, "vel": 100}, ...],   Onyx names, C-3 = MIDI 48
   "bend": [{"line": 4, "value": 0.5}], "mod": [{"line": 8, "value": 1}],
   "buttons": [...], "hold": [...],                                     ours only (sequencer/arp)
   "ours_only": true,                                                   skipped here
   "roland": {"VCO RANGE": 4}, "roland_only": true}                     raw Roland values set after
                                                                        the mapping (probes.json)
Sliders are travel 0-1, choices are item indices, as in plugin/PluginProcessor.cpp.

How a spec becomes an Onyx song (see docs/roland-measurements.md, "Rendering"):
- private/reference/roland-vst/base.otk (tools/compare/make_base.py) is opened fresh for every spec, so
  every render starts from a new plug-in instance in the hardware-equivalent baseline.
- Parameters our spec leaves out take our plugin's defaults (OUR_DEFAULTS), so both renderers
  play the same patch.
- Notes: each note goes on a track; a note that starts on the line another ends on goes on that
  track (the new note releases the old one first, as sh101render sends offs before ons at equal
  times). Overlapping notes (legato) go on other tracks. Onyx plays tracks in order, so a note
  is never put on a track below one whose note ends on the same line.
- Our TRANSPOSE (L/M/H) shifts every note by -12/0/+12 semitones: Roland's OCTAVE only moves its
  own on-screen keyboard and has no effect on MIDI notes (measured).
- bend -> MIDI pitch bend (Onyx 05xx, 8-bit: +1.0 arrives as +0.992), BEND RANGE from bendVco;
  mod -> CC1 (Onyx 04xx). Roland's pitch bend moves only the pitch, and its CC1 does nothing
  (measured), so bendVcf, lfoModDepth and "mod" have no Roland equivalent: specs that rely on
  them are marked ours_only by make_suite.py, and this script warns.
"""
import json, math, os, subprocess, sys, tempfile

OTK = os.path.expanduser("~/src/onyx-tracker/build/Build/Products/Release/otk")
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../.."))
BASE = os.path.join(ROOT, "private/reference/roland-vst/base.otk")
RENDERS = os.path.join(ROOT, "private/reference/roland-vst/renders")
TRACKS = 16          # the CR1604 desk mixes tracks 1-16 only
EXPORT_TAIL = 2.5    # Onyx's export renders 2.5 s after the song ends
INST = "02"

# Our plugin's parameter defaults (plugin/PluginProcessor.cpp, createLayout).
OUR_DEFAULTS = {
    "lfoRate": 0.5, "lfoWave": 0, "vcoMod": 0.0, "range": 1, "pulseWidth": 0.0, "pwmSource": 1,
    "pulseLevel": 0.0, "sawLevel": 1.0, "subLevel": 0.4, "noiseLevel": 0.0, "subMode": 1,
    "vcfFreq": 0.35, "vcfRes": 0.3, "vcfEnv": 0.5, "vcfMod": 0.0, "vcfKybd": 0.5,
    "vcaMode": 0, "attack": 0.0, "decay": 0.4, "sustain": 0.3, "release": 0.2, "trigger": 0,
    "volume": 0.7, "tune": 0.5, "portamento": 0.0, "portaMode": 2,
    "bendVco": 0.2, "bendVcf": 0.0, "lfoModDepth": 0.5, "transpose": 1,
    "sync": 0, "clockDiv": 3,
}

# Roland parameter -> (raw maximum). The VST3 value is raw / max.
ROLAND_RANGE = {
    "MODULATOR WAVEFORM": 5, "MODULATOR RATE": 255, "MODULATOR VCO DEPTH": 255,
    "MODULATOR VCF DEPTH": 255, "ENV TRIG": 2, "VCO PULSE WIDTH": 255, "VCO PULSE WIDTH MOD": 5,
    "VCO RANGE": 5, "MIXER SQR": 255, "MIXER SAW": 255, "MIXER SUB OSC": 255, "MIXER NOISE": 255,
    "SUB OSC TYPE": 2, "VCF FREQ": 255, "VCF RESONANCE": 255, "VCF ENV": 255, "VCF ATTACK": 255,
    "VCF DECAY": 255, "VCF SUSTAIN": 255, "VCF RELEASE": 255, "VCF KEY FOLLOW": 255,
    "VCA ATTACK": 255, "VCA DECAY": 255, "VCA SUSTAIN": 255, "VCA RELEASE": 255, "VCA TONE": 255,
    "EFX CRUSHER": 255, "EFX REVERB LEVEL": 255, "EFX DELAY LEVEL": 255, "EFX DELAY TIME": 255,
    "PORTAMENTO TIME": 255, "PORTAMENTO MODE": 2, "VCA MODE": 1, "BEND RANGE": 23,
    "TEMPO SYNC": 1, "PATCH LEVEL": 255, "ARPEGGIO": 1, "ARP TYPE": 5, "ARP STEP": 5,
    "SCATTER TYPE": 9, "SCATTER DEPTH": 10, "OCTAVE": 6, "KEY HOLD": 1, "MASTER TUNE": 20,
}

# Fixed in the base song (hardware-equivalent baseline); raw values.
BASELINE = {
    "EFX CRUSHER": 0, "EFX REVERB LEVEL": 0, "EFX DELAY LEVEL": 0, "EFX DELAY TIME": 0,
    "VCA TONE": 128,                # centre = 0 (bipolar display)
    "TEMPO SYNC": 0, "ARPEGGIO": 0, "ARP TYPE": 0, "ARP STEP": 0, "KEY HOLD": 0,
    "SCATTER TYPE": 0, "SCATTER DEPTH": 5,   # depth raw 5 = display 0 (range -5..+5)
    "OCTAVE": 3,                    # raw 3 = display 0 (range -3..+3)
    "MASTER TUNE": 10,              # raw 10 = 440 Hz (430 + raw)
    "PATCH LEVEL": 128,             # Roland's VOLUME knob; specs move it only through our "volume"
}

# Choice mappings, our index -> Roland raw (checked by specs/probes.json, docs/roland-measurements.md).
LFO_WAVE = {0: 1, 1: 3, 2: 4, 3: 5}       # ours Tri/Square/Random/Noise -> SIN0 TRI1 SAW2 SQR3 RND4 NOISE5
PWM_SOURCE = {0: 1, 1: 0, 2: 3}           # ours LFO/Manual/Env -> MAN0 LFO1(2) F.ENV3 A.ENV4(5)
SUB_MODE = {0: 1, 1: 0, 2: 2}             # ours 1oct/2oct/2oct-pulse -> raw 0 2OCT square, 1 1OCT, 2 2OCT 25% pulse
VCA_MODE = {0: 1, 1: 0}                   # ours Env/Gate -> GATE0 ENV1
TRIGGER = {0: 2, 1: 0, 2: 1}              # ours Gate+Trig/Gate/LFO -> GATE0 LFO1 GATE+TRIG2
PORTA_MODE = {0: 0, 1: 1, 2: 2}           # ON/AUTO/OFF on both

BEND_SEMITONES_FULL = 12.0    # our bendVco at 10 bends +-12 semitones (research.md S2: ~+-1200 cents, linear B pot)
TUNE_RANGE_CENTS = 50.0       # our TUNE knob end to end / 2: the service notes' "Tune (+-50 cent)"
NOTE_NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]


def otk(*args, stdin=None):
    r = subprocess.run([OTK, *args], input=stdin, capture_output=True, text=True, timeout=600)
    if r.returncode != 0:
        raise RuntimeError(f"otk {' '.join(args)}: {r.stdout}{r.stderr}")
    return r.stdout


def parse_note(s):
    """Onyx/sh101render note name -> MIDI (C-3 = 48)."""
    letter = s[0].upper()
    semi = "C D EF G A B".index(letter)
    pos = 1
    if s[pos] == "#":
        semi += 1; pos += 1
    elif s[pos] == "b":
        semi -= 1; pos += 1
    elif s[pos] == "-":
        pos += 1
    return 12 * (int(s[pos:]) + 1) + semi


def note_name(midi):
    return f"{NOTE_NAMES[midi % 12]}{midi // 12 - 1}"


def bipolar_up(t):
    """Our 0-10 slider -> the upper half of Roland's bipolar range (raw 128 = 0 ... 255 = +127)."""
    return 128 + t * 127


def roland_params(ours):
    """Our parameter dict (missing keys = our defaults) -> (Roland raw values, note shift in
    semitones, bend semitones, warnings)."""
    p = dict(OUR_DEFAULTS); p.update(ours or {})
    unknown = set(ours or {}) - set(OUR_DEFAULTS)
    if unknown:
        raise ValueError(f"unknown parameter(s) {sorted(unknown)}")
    c = lambda k: int(round(p[k]))
    raw = dict(BASELINE)
    raw.update({
        "MODULATOR WAVEFORM": LFO_WAVE[c("lfoWave")],
        "MODULATOR RATE": 255 * p["lfoRate"],
        "MODULATOR VCO DEPTH": bipolar_up(p["vcoMod"]),
        "MODULATOR VCF DEPTH": bipolar_up(p["vcfMod"]),
        "ENV TRIG": TRIGGER[c("trigger")],
        "VCO PULSE WIDTH": 255 * p["pulseWidth"],
        "VCO PULSE WIDTH MOD": PWM_SOURCE[c("pwmSource")],
        "VCO RANGE": c("range"),           # ours 16'/8'/4'/2' = Roland's 64'/32'/16'/8' (same pitch)
        "MIXER SQR": 255 * p["pulseLevel"],
        "MIXER SAW": 255 * p["sawLevel"],
        "MIXER SUB OSC": 255 * p["subLevel"],
        "MIXER NOISE": 255 * p["noiseLevel"],
        "SUB OSC TYPE": SUB_MODE[c("subMode")],
        "VCF FREQ": 255 * p["vcfFreq"],
        "VCF RESONANCE": 255 * p["vcfRes"],
        "VCF ENV": bipolar_up(p["vcfEnv"]),
        "VCF KEY FOLLOW": bipolar_up(p["vcfKybd"]),
        "PORTAMENTO TIME": 255 * p["portamento"],
        "PORTAMENTO MODE": PORTA_MODE[c("portaMode")],
        "VCA MODE": VCA_MODE[c("vcaMode")],
        "PATCH LEVEL": 255 * p["volume"],
    })
    # One envelope on the hardware: both of Roland's ADSRs get the same values.
    for env in ("VCF", "VCA"):
        raw[f"{env} ATTACK"] = 255 * p["attack"]
        raw[f"{env} DECAY"] = 255 * p["decay"]
        raw[f"{env} SUSTAIN"] = 255 * p["sustain"]
        raw[f"{env} RELEASE"] = 255 * p["release"]
    # TUNE: Roland's MASTER TUNE is 430-450 Hz in 1 Hz steps.
    hz = 440 * 2 ** ((p["tune"] - 0.5) * 2 * TUNE_RANGE_CENTS / 1200)
    raw["MASTER TUNE"] = min(20, max(0, round(hz - 430)))
    warnings = []
    if abs(hz - 430 - raw["MASTER TUNE"]) > 0.5:
        warnings.append(f"tune {p['tune']:.3f} = {hz:.1f} Hz is outside MASTER TUNE 430-450 Hz")
    bend = BEND_SEMITONES_FULL * p["bendVco"]
    raw["BEND RANGE"] = min(23, max(0, round(bend) - 1))
    shift = 12 * (c("transpose") - 1)
    return raw, shift, bend, warnings, p


def vst_value(name, raw):
    raw = min(ROLAND_RANGE[name], max(0, int(round(raw))))
    return raw, raw / ROLAND_RANGE[name]


def spec_events(spec):
    """Lines of every event, and the factor lpb must be multiplied by so they are whole lines."""
    lines = []
    for n in spec.get("notes", []):
        lines += [n["line"], n["line"] + n.get("len", 1)]
    for key in ("bend", "mod"):
        lines += [e["line"] for e in spec.get(key, [])]
    lpb = spec.get("lpb", 4)
    for k in (1, 2, 3, 4, 6, 8):
        if lpb * k <= 32 and all(abs(l * k - round(l * k)) < 1e-6 for l in lines):
            return k
    raise ValueError(f"{spec['name']}: note times not on a 1/8-line grid")


def build_rows(spec, k, shift, bend_semis, warnings):
    """Cells per line: {line: {track: cell}}; returns (rows, number of tracks used, last line)."""
    notes = sorted(spec.get("notes", []), key=lambda n: (n["line"], parse_note(n["note"])))
    tracks = []      # per track: end line of its last note
    rows = {}
    last = 0
    for n in notes:
        start = int(round(n["line"] * k))
        end = int(round((n["line"] + n.get("len", 1)) * k))
        midi = parse_note(n["note"]) + shift
        if not 0 <= midi <= 127:
            raise ValueError(f"{spec['name']}: note {n['note']} + {shift} out of MIDI range")
        vel = int(n.get("vel", 100))
        ending_here = [t for t, e in enumerate(tracks) if e == start]
        if ending_here:
            t = max(ending_here)          # replaces that note's OFF; lower tracks' OFFs come first
        else:
            free = [t for t, e in enumerate(tracks) if e < start]
            t = min(free) if free else len(tracks)
            if t == len(tracks):
                tracks.append(0)
        if t >= TRACKS - 2:
            raise ValueError(f"{spec['name']}: more than {TRACKS - 2} overlapping notes")
        tracks[t] = end
        rows.setdefault(start, {})[t] = f"{note_name(midi)} {INST} 0C{min(127, max(1, vel)):02X}"
        rows.setdefault(end, {}).setdefault(t, "OFF")
        last = max(last, end)
    # bend and mod on the last two tracks (after the notes on the same line, as sh101render orders them)
    if spec.get("bend") and bend_semis < 0.5:
        warnings.append("bend events with bendVco ~0: no pitch bend sent")
    for key, track, cmd in (("bend", TRACKS - 1, "05"), ("mod", TRACKS - 2, "04")):
        for e in spec.get(key, []):
            line = int(round(e["line"] * k))
            v = float(e["value"])
            if key == "bend":
                if bend_semis < 0.5:
                    continue
                param = min(255, max(0, int(round((8192 + v * 8191) / 64))))
            else:
                param = min(127, max(0, int(round(v * 127))))
            rows.setdefault(line, {})[track] = f"--- {INST} {cmd}{param:02X}"
            last = max(last, line)
    return rows, last


def render(spec, out_dir, dry=False):
    import numpy as np
    import soundfile as sf
    raw, shift, bend_semis, warnings, p = roland_params(spec.get("params", {}))
    for name, v in spec.get("roland", {}).items():
        if name not in ROLAND_RANGE:
            raise ValueError(f"{spec['name']}: unknown Roland parameter {name}")
        raw[name] = v
        if name == "BEND RANGE":
            bend_semis = v + 1
    if p["bendVcf"] > 0 and spec.get("bend"):
        warnings.append("bendVcf has no Roland equivalent (its pitch bend moves only the pitch)")
    if spec.get("mod"):
        warnings.append("mod (lever push / CC1) has no Roland equivalent; CC1 sent anyway")
    if spec.get("buttons") or spec.get("hold"):
        warnings.append("buttons/hold (sequencer, arpeggio) have no Roland equivalent: ignored")
    k = spec_events(spec)
    bpm = spec.get("bpm", 120)
    if bpm != int(bpm):
        raise ValueError(f"{spec['name']}: Onyx tempos are whole BPM")
    lpb = spec.get("lpb", 4) * k
    line_sec = 60.0 / bpm / lpb
    rows, last = build_rows(spec, k, shift, bend_semis, warnings)
    spec_lines = spec.get("lines", 0) * k
    last_line = max(spec_lines, last)                    # as sh101render: max(lines, note ends)
    tail = spec.get("tail", 2.5)
    want = (last_line * line_sec + tail)                 # seconds, as sh101render renders
    block = max(spec_lines, last + 1)                    # the OFF row must be inside the block
    block = max(block, int(math.ceil(last_line + (tail - EXPORT_TAIL) / line_sec)) + 1)
    params = []
    used = {}
    for name, v in raw.items():
        r, val = vst_value(name, v)
        used[name] = r
        params += [name, f"{val:.6f}"]
    text = []
    for line in sorted(rows):
        cells = ["~"] * TRACKS
        for t, cell in rows[line].items():
            cells[t] = cell
        text.append(f"{line:03d} | " + " | ".join(cells))
    if dry:
        print(f"== {spec['name']}: lpb x{k}, {block} lines, shift {shift:+d}, bend {bend_semis:.1f} st")
        print("  " + ", ".join(f"{n}={v}" for n, v in used.items()))
        print("\n".join("  " + t for t in text))
        for w in warnings:
            print("  warning:", w)
        return None
    otk("open", BASE, "--force")
    otk("set", "tempo", str(int(bpm)))
    otk("set", "lpb", str(lpb))
    otk("set", "tpl", "6")
    otk("set", "tracks", str(TRACKS))
    otk("param", INST, *params)
    otk("block", "lines", "00", str(block))
    otk("clear", "00", "--tracks", f"1-{TRACKS}")
    if text:
        otk("write", "00", "--line", "0", "--track", "1", stdin="\n".join(text) + "\n")
    tmp = os.path.join(tempfile.gettempdir(), f"roland_render_{os.getpid()}.wav")
    otk("export", tmp, "--bits", "32")
    x, sr = sf.read(tmp, always_2d=True, dtype="float32")
    os.remove(tmp)
    stereo = float(np.abs(x[:, 0] - x[:, 1]).max()) if x.shape[1] > 1 else 0.0
    y = x.mean(axis=1)
    n = int(round(want * sr))
    y = y[:n] if len(y) >= n else np.concatenate([y, np.zeros(n - len(y), np.float32)])
    out = os.path.join(out_dir, spec["name"] + ".wav")
    sf.write(out, y, sr, subtype="FLOAT")
    # checks
    peak = float(np.abs(y).max())
    clipped = int(np.sum(np.abs(x) >= 0.9999))
    first = min((n["line"] for n in spec.get("notes", [])), default=None)
    onset_ms = None
    if peak > 0 and first is not None:
        thr = max(peak * 1e-3, 1e-6)
        i = int(np.argmax(np.abs(y) > thr))
        onset_ms = (i / sr - first * k * line_sec) * 1000
    info = {
        "file": out, "rate": sr, "seconds": round(len(y) / sr, 4), "peak": round(peak, 5),
        "peak_dbfs": round(20 * math.log10(max(peak, 1e-12)), 2),
        "rms_dbfs": round(20 * math.log10(max(float(np.sqrt(np.mean(y.astype(np.float64) ** 2))), 1e-12)), 2),
        "clipped_samples": clipped, "silent": peak < 1e-4, "stereo_diff": round(stereo, 6),
        "onset_delay_ms": None if onset_ms is None else round(onset_ms, 2),
        "lpb_factor": k, "note_shift": shift, "bend_semitones": round(bend_semis, 2),
        "roland": used, "warnings": warnings,
    }
    return info


def main():
    argv = sys.argv[1:]
    out_dir = RENDERS
    opts = {}
    for flag in ("--out", "--only"):
        if flag in argv:
            i = argv.index(flag); opts[flag] = argv[i + 1]; del argv[i:i + 2]
    flags = {f for f in ("--force-ours-only", "--dry", "--keep-stereo") if f in argv}
    argv = [a for a in argv if a not in flags]
    out_dir = os.path.abspath(opts.get("--out", out_dir))
    only = opts["--only"].split(",") if "--only" in opts else None
    os.makedirs(out_dir, exist_ok=True)
    man_path = os.path.join(out_dir, "manifest.json")
    manifest = json.load(open(man_path)) if os.path.exists(man_path) else {}
    for path in argv:
        specs = json.load(open(path))
        for spec in specs if isinstance(specs, list) else [specs]:
            if only and not any(o in spec["name"] for o in only):
                continue
            if spec.get("ours_only") and "--force-ours-only" not in flags:
                continue
            info = render(spec, out_dir, dry="--dry" in flags)
            if info is None:
                continue
            manifest[spec["name"]] = info
            flag = []
            if info["silent"]: flag.append("SILENT")
            if info["clipped_samples"]: flag.append(f"CLIP x{info['clipped_samples']}")
            if info["stereo_diff"] > 1e-6: flag.append("STEREO")
            print(f"{spec['name']:34s} peak {info['peak_dbfs']:6.1f} dBFS  rms {info['rms_dbfs']:6.1f}  "
                  f"onset {info['onset_delay_ms'] if info['onset_delay_ms'] is not None else float('nan'):6.2f} ms  "
                  + " ".join(flag) + ("  " + "; ".join(info["warnings"]) if info["warnings"] else ""), flush=True)
            json.dump(manifest, open(man_path, "w"), indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
