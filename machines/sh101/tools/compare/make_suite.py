#!/usr/bin/env python3
"""Writes the test specs that both Roland's SH-101 plug-in (roland_render.py) and ours (sh101render)
render, and that compare.py and measure.py read:

  specs/suite.json        the main suite: each test isolates one block of the voice
  specs/calibration.json  the service notes' adjustment conditions (VCO tune/width, VCF tracking, PW)
  specs/stability.json    long multi-loop runs for the fit's stability check (ours only)
  specs/probes.json       Roland-only probes of the plug-in's own switches and ranges (raw values)

Every spec sets every voice parameter (BASE plus its own), so neither renderer's defaults matter.
Sliders are travel 0-1, switches are our item indices (plugin/PluginProcessor.cpp). Notes are
Onyx names (C-3 = MIDI 48; A-4 = 69 = 442 Hz at 8'/M on a unit trimmed to the service notes).
Lines are 1/16 notes at 120 BPM (0.125 s) unless a spec says otherwise.

"ours_only": Roland's plug-in can't play it (bender VCF, LFO MOD push, sequencer, arpeggio, long
stability runs). "roland_only": raw Roland values ("roland": {NAME: raw}) with no counterpart on
our panel; sh101render skips them. docs/roland-measurements.md says what each group measures.
"""
import json, os

HERE = os.path.dirname(os.path.abspath(__file__))

# A neutral patch: saw only, filter wide open, no resonance or modulation, VCA on the gate,
# envelope square (A 0, S 10), no portamento, transpose M, 8'.
BASE = {
    "lfoRate": 0.5, "lfoWave": 0, "vcoMod": 0.0, "range": 1, "pulseWidth": 0.0, "pwmSource": 1,
    "pulseLevel": 0.0, "sawLevel": 1.0, "subLevel": 0.0, "noiseLevel": 0.0, "subMode": 0,
    "vcfFreq": 1.0, "vcfRes": 0.0, "vcfEnv": 0.0, "vcfMod": 0.0, "vcfKybd": 0.0,
    "vcaMode": 1, "attack": 0.0, "decay": 0.5, "sustain": 1.0, "release": 0.0, "trigger": 0,
    "volume": 0.5, "tune": 0.5, "portamento": 0.0, "portaMode": 2,
    "bendVco": 0.0, "bendVcf": 0.0, "lfoModDepth": 0.0, "transpose": 1, "sync": 0,
}
SOURCES_OFF = {"sawLevel": 0.0, "pulseLevel": 0.0, "subLevel": 0.0, "noiseLevel": 0.0}
RANGES = {"16": 0, "8": 1, "4": 2, "2": 3}
SUB = {"1oct": 0, "2oct": 1, "2oct-pulse": 2}
LFO = {"tri": 0, "sqr": 1, "rnd": 2, "noise": 3}


def pct(v):
    return f"{int(round(v * 100)):03d}"


def held(note="A-4", length=8, line=0, vel=100):
    return [{"line": line, "note": note, "len": length, "vel": vel}]


class Specs(list):
    def add(self, name, notes, lines, bpm=120, lpb=4, tail=None, extra=None, **params):
        assert name not in {s["name"] for s in self}, name
        p = dict(BASE)
        unknown = set(params) - set(BASE)
        assert not unknown, (name, unknown)
        p.update(params)
        s = {"name": name, "bpm": bpm, "lpb": lpb, "lines": lines, "params": p, "notes": notes}
        if tail is not None:
            s["tail"] = tail
        # A real unit's filter at high RES self-oscillates all the time, gate or not; ours starts
        # from the circuit's own noise, which takes up to a few seconds at low cutoffs. sh101render
        # runs this long before line 0 so the oscillation is established, as on the hardware.
        if p.get("vcfRes", 0) >= 0.75:
            s["preroll"] = 4.0
        if extra:
            s.update(extra)
        self.append(s)
        return s


def suite():
    s = Specs()

    # --- Oscillator, filter open, VCA on the gate ------------------------------------------
    # RANGE: A4 at each footage (8' = 442 Hz on a trimmed unit).
    for r, i in RANGES.items():
        s.add(f"osc-saw-{r}-A4", held("A-4"), 8, range=i)
    # Tracking across the keyboard at 8'/M, and the TRANSPOSE switch at the ends.
    for n in ("F-3", "C-4", "C-5", "C-6"):
        s.add(f"osc-saw-8-{n.replace('-', '')}", held(n), 8)
    s.add("osc-saw-8-L-F3", held("F-3"), 8, transpose=0)   # F2 (TRANSPOSE L)
    s.add("osc-saw-8-H-C6", held("C-6"), 8, transpose=2)   # C7 (TRANSPOSE H)
    # TUNE knob ends.
    for t in (0.0, 1.0):
        s.add(f"osc-tune-{pct(t)}", held("A-4"), 8, tune=t)
    # Pulse: manual PW across its travel (0 = square, 10 = narrowest), on A3.
    for w in (0.0, 0.25, 0.5, 0.75, 0.9, 1.0):
        s.add(f"osc-pulse-pw{pct(w)}", held("A-3"), 8, sawLevel=0.0, pulseLevel=1.0, pulseWidth=w)
    # Sub-oscillator alone, each mode, and under the saw.
    for m, i in SUB.items():
        s.add(f"osc-sub-{m}", held("A-3"), 8, sawLevel=0.0, subLevel=1.0, subMode=i)
    s.add("osc-saw-sub-2oct", held("A-3"), 8, subLevel=1.0, subMode=1)
    # Noise alone, full and half level.
    for v in (1.0, 0.5):
        s.add(f"osc-noise-{pct(v)}", held("A-4"), 8, sawLevel=0.0, noiseLevel=v)
    # Mixer slider laws: the saw across its travel, pulse and sub at 5 (levels relative to mix-saw-100).
    for v in (0.1, 0.25, 0.375, 0.5, 0.625, 0.75, 0.9, 1.0):
        s.add(f"mix-saw-{pct(v)}", held("A-3"), 8, sawLevel=v)
    s.add("mix-pulse-050", held("A-3"), 8, sawLevel=0.0, pulseLevel=0.5)
    s.add("mix-sub-050", held("A-3"), 8, sawLevel=0.0, subLevel=0.5, subMode=1)
    # All sources at once (relative levels).
    s.add("mix-all", held("A-3"), 8, pulseLevel=1.0, subLevel=1.0, subMode=1, noiseLevel=0.3)

    # --- VCF, static: saw at 110 Hz (A3 at 16'), FREQ x RES ---------------------------------
    # FREQ 4-10: below 4 the cutoff is under the fundamental and the note is silent.
    for f in (0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0):
        for r in (0.0, 0.5, 0.75, 1.0):
            s.add(f"vcf-saw-f{pct(f)}-r{pct(r)}", held("A-3"), 8, range=0, vcfFreq=f, vcfRes=r)
    for f in (0.3, 0.6):
        s.add(f"vcf-sqr-f{pct(f)}-r000", held("A-3"), 8, range=0, sawLevel=0.0, pulseLevel=1.0, vcfFreq=f)
    # Passband loss with resonance: saw at 55 Hz, FREQ mid.
    for r in (0.0, 0.5, 0.8, 1.0):
        s.add(f"vcf-bass-r{pct(r)}", held("A-2"), 8, range=0, vcfFreq=0.5, vcfRes=r)

    # --- VCF self-oscillation: sources off, RES 10 --------------------------------------------
    # FREQ law with KYBD 0 (the pitch of the oscillation is the cutoff). Long notes at the bottom,
    # where the oscillation is a few hertz.
    for f in [i / 10 for i in range(11)]:
        n = 64 if f < 0.15 else 8
        s.add(f"selfosc-f{pct(f)}", held("A-4", n), n, **SOURCES_OFF, vcfRes=1.0, vcfFreq=f)
    # Onset: RES 7.5-9.5 at FREQ 5.
    for r in (0.75, 0.8, 0.82, 0.84, 0.86, 0.88, 0.9, 0.95):
        s.add(f"selfosc-onset-r{pct(r)}", held("A-4"), 8, **SOURCES_OFF, vcfRes=r, vcfFreq=0.5)
    # KYBD tracking: four keys at KYBD 0/5/10, FREQ 4.
    for k in (0.0, 0.5, 1.0):
        for n in ("F-3", "F-4", "F-5", "C-6"):
            s.add(f"selfosc-kybd{pct(k)}-{n.replace('-', '')}", held(n), 8, **SOURCES_OFF,
                  vcfRes=1.0, vcfFreq=0.4, vcfKybd=k)
    # ENV depth: sustain 10, the oscillation's pitch while held = FREQ 2.5 + the full ENV
    # (selfosc-env000 is the base).
    for e in (0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0):
        s.add(f"selfosc-env{pct(e)}", held("A-4", 16), 16, **SOURCES_OFF, vcfRes=1.0, vcfFreq=0.25,
              vcfEnv=e, attack=0.0, decay=0.5, sustain=1.0)

    # --- Envelope, on the VCA (filter open): A, D, S, R laws ---------------------------------
    EIGHTHS = (0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0)
    for a in EIGHTHS:
        s.add(f"env-attack-a{pct(a)}", held("A-3", 48), 48, tail=1.0, vcaMode=0, attack=a, sustain=1.0)
    for d in EIGHTHS:
        s.add(f"env-decay-d{pct(d)}", held("A-3", 96), 96, tail=1.0, vcaMode=0, decay=d, sustain=0.0)
    for v in (0.0, 0.25, 0.5, 0.75):
        s.add(f"env-sustain-s{pct(v)}", held("A-3", 16), 16, tail=1.0, vcaMode=0, decay=0.2, sustain=v)
    for r in EIGHTHS:
        s.add(f"env-release-r{pct(r)}", held("A-3", 8), 8, tail=12.0, vcaMode=0, release=r, sustain=1.0)
    # The same envelope on the filter: a decaying sweep through a resonant saw.
    for e in (0.5, 1.0):
        for d in (0.25, 0.6):
            s.add(f"env-vcf-e{pct(e)}-d{pct(d)}", held("A-3", 16), 16, range=0, vcfFreq=0.15, vcfRes=0.6,
                  vcfEnv=e, decay=d, sustain=0.0, release=0.3, vcaMode=1)
    # VCA ENV vs GATE on a short phrase (attack clicks, release).
    phrase = [{"line": i * 2, "note": n, "len": 1} for i, n in enumerate(["A-3", "A-3", "C-4", "E-4", "A-4", "E-4", "C-4", "A-3"])]
    for m, i in (("env", 0), ("gate", 1)):
        s.add(f"vca-{m}-phrase", phrase, 18, vcaMode=i, decay=0.3, sustain=0.5, release=0.2, vcfFreq=0.6)

    # --- ENV trigger modes: overlapping (legato) notes, and the LFO trigger --------------------
    legato = [{"line": 0, "note": "A-3", "len": 5}, {"line": 4, "note": "C-4", "len": 5}, {"line": 8, "note": "E-4", "len": 5}]
    for m, i in (("gatetrig", 0), ("gate", 1)):
        s.add(f"trig-{m}-legato", legato, 16, vcaMode=0, decay=0.25, sustain=0.2, trigger=i)
    s.add("trig-lfo-held", held("A-3", 24), 24, vcaMode=0, decay=0.2, sustain=0.0, trigger=2, lfoRate=0.6)

    # --- Portamento ------------------------------------------------------------------------
    # ON: A3 to A5 (two octaves up), long enough at both ends for the slowest glide to settle.
    jump = [{"line": 0, "note": "A-3", "len": 48}, {"line": 48, "note": "A-5", "len": 80}]
    for p in (0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0):
        s.add(f"porta-on-p{pct(p)}", jump, 128, tail=0.5, portaMode=0, portamento=p)
    # AUTO: glides only between overlapping notes.
    s.add("porta-auto-legato", [{"line": 0, "note": "A-3", "len": 9}, {"line": 8, "note": "A-5", "len": 16}], 24,
          portaMode=1, portamento=0.6)
    s.add("porta-auto-detached", [{"line": 0, "note": "A-3", "len": 7}, {"line": 8, "note": "A-5", "len": 16}], 24,
          portaMode=1, portamento=0.6)

    # --- MODULATOR --------------------------------------------------------------------------
    # Rate law: triangle on the VCO (vibrato), RATE across its travel; held A4, long enough for
    # a few cycles at the slow end.
    for r in (0.0, 0.1, 0.25, 0.4, 0.5, 0.6, 0.75, 0.9, 1.0):
        n = 192 if r < 0.05 else 96 if r < 0.2 else 24
        s.add(f"lfo-rate-r{pct(r)}", held("A-4", n), n, lfoRate=r, vcoMod=0.3, vcfFreq=0.8)
    # Waveforms on the VCO at RATE 5.
    for w, i in LFO.items():
        s.add(f"lfo-vco-{w}", held("A-4", 24), 24, lfoRate=0.5, lfoWave=i, vcoMod=0.3, vcfFreq=0.8)
    # VCO MOD depth law.
    for d in (0.1, 0.25, 0.375, 0.5, 0.75, 1.0):
        s.add(f"lfo-depth-m{pct(d)}", held("A-4", 24), 24, lfoRate=0.4, vcoMod=d, vcfFreq=0.8)
    # VCF MOD: triangle and square on a self-oscillating filter.
    for w, d in (("tri", 0.25), ("tri", 0.5), ("tri", 0.75), ("tri", 1.0), ("sqr", 0.5)):
        if True:
            s.add(f"lfo-vcf-{w}-m{pct(d)}", held("A-4", 24), 24, **SOURCES_OFF, vcfRes=1.0, vcfFreq=0.5,
                  lfoRate=0.4, lfoWave=LFO[w], vcfMod=d)
    # PWM from the LFO and from the ENV.
    s.add("pwm-lfo", held("A-3", 24), 24, sawLevel=0.0, pulseLevel=1.0, pwmSource=0, pulseWidth=0.6, lfoRate=0.4)
    s.add("pwm-env", held("A-3", 16), 16, sawLevel=0.0, pulseLevel=1.0, pwmSource=2, pulseWidth=0.8,
          decay=0.5, sustain=0.0)

    # --- Bender, lever push, velocity, volume ------------------------------------------------
    s.add("bend-vco", held("A-4", 24), 24, bendVco=0.5,
          extra={"bend": [{"line": 4, "value": 1.0}, {"line": 10, "value": 0.0}, {"line": 14, "value": -1.0},
                          {"line": 20, "value": 0.0}]})
    s.add("bend-vcf", held("A-3", 24), 24, bendVcf=0.6, vcfFreq=0.3, vcfRes=0.6,
          extra={"ours_only": True, "bend": [{"line": 4, "value": 1.0}, {"line": 12, "value": -1.0}, {"line": 20, "value": 0.0}]})
    s.add("mod-push", held("A-4", 32), 32, lfoModDepth=0.6, lfoRate=0.5,
          extra={"ours_only": True, "mod": [{"line": 4, "value": 1.0}, {"line": 24, "value": 0.0}]})
    for v in (30, 127):
        s.add(f"vel-{v:03d}", held("A-3", vel=v), 8)
    # VOLUME law, with the saw at 5 so the top doesn't clip.
    for v in (0.1, 0.25, 0.375, 0.5, 0.625, 0.75, 0.9, 1.0):
        s.add(f"volume-{pct(v)}", held("A-3"), 8, sawLevel=0.5, volume=v)

    # --- Musical phrases (listening, overall LSD) ---------------------------------------------
    bass = ["C-4", "C-4", "C-5", "C-4", "D#4", "C-4", "G-4", "C-5", "C-4", "A#4", "C-4", "F-4", "C-4", "D#5", "C-4", "G-4"]
    notes = [{"line": i, "note": n, "len": 0.5} for i, n in enumerate(bass * 2)]
    s.add("phrase-bass", notes, 34, bpm=124, range=0, sawLevel=0.0, pulseLevel=0.8, subLevel=0.7, subMode=1,
          vcfFreq=0.25, vcfRes=0.55, vcfEnv=0.45, vcfKybd=0.5, vcaMode=0, decay=0.3, sustain=0.0, release=0.15)
    lead = [{"line": 0, "note": "E-4", "len": 4}, {"line": 4, "note": "G-4", "len": 2}, {"line": 6, "note": "A-4", "len": 6},
            {"line": 12, "note": "C-5", "len": 4}, {"line": 16, "note": "B-4", "len": 8}]
    s.add("phrase-lead", lead, 26, sawLevel=1.0, pulseLevel=0.5, pulseWidth=0.4, pwmSource=0, lfoRate=0.45,
          vcfFreq=0.45, vcfRes=0.4, vcfEnv=0.35, vcfKybd=0.6, vcaMode=0, attack=0.15, decay=0.5, sustain=0.6,
          release=0.35, portaMode=0, portamento=0.35, vcoMod=0.05)

    # --- Firmware behaviour (ours only): arpeggio and the sequencer ---------------------------
    s.add("arp-up-chord", [{"line": 0, "note": n, "len": 32} for n in ("C-4", "E-4", "G-4")], 32,
          vcaMode=1, lfoRate=0.55, vcfFreq=0.5, vcfEnv=0.3, decay=0.2, sustain=0.0,
          extra={"ours_only": True, "buttons": [{"line": 0, "button": "arpUp", "len": 0.5}]})
    return s


def calibration():
    """The service notes' adjustments (p.6 of the 1982 notes), as renders."""
    s = Specs()
    # VCO TUNE / WIDTH: F3, A4 and F5 at 8'/M (A4 = 442 Hz; F5 = 4 x F3).
    for n in ("F-3", "A-4", "F-5"):
        s.add(f"cal-vco-{n.replace('-', '')}", held(n), 8, sawLevel=0.0, pulseLevel=1.0)
    # PW: square at PW 0 (mark/space 1:1).
    s.add("cal-pw", held("A-3"), 8, sawLevel=0.0, pulseLevel=1.0, pulseWidth=0.0)
    # VCF WIDTH: RES max, A4 near 1 kHz, then F4 and F5 one octave apart at KYBD max.
    for n in ("F-4", "A-4", "F-5"):
        s.add(f"cal-vcf-{n.replace('-', '')}", held(n), 8, **SOURCES_OFF, vcfRes=1.0, vcfFreq=0.3, vcfKybd=1.0)
    return s


def stability():
    """Long runs for the fit (ours only): loops of a resonant bass line, self-oscillation under
    LFO and ENV, and noise into maximum resonance. Each must stay bounded and free of slow
    build-ups (fit_roland.py checks the energy under 12 Hz after the first loop). The cutoffs sit
    well above 12 Hz on the schematic's FREQ law (FREQ 0.3 is about 16 Hz there), so a
    self-oscillating filter isn't mistaken for a build-up. Two are checked for boundedness and
    growth only ("check_low": false): stab-noise-r100, because the noise's own low end (about
    -7 dB/oct) passes the lowpass, and stab-all-max, which sweeps the cutoff to the bottom."""
    s = Specs()
    bass = ["A-3", "A-3", "A-4", "A-3", "C-4", "A-3", "E-4", "G-4"]
    notes = [{"line": i, "note": n, "len": 0.5} for i, n in enumerate(bass * 8)]
    s.add("stab-bass-r100", notes, 64, bpm=130, range=0, sawLevel=1.0, subLevel=0.6, subMode=1, vcfFreq=0.45,
          vcfRes=1.0, vcfEnv=0.6, vcaMode=0, decay=0.3, sustain=0.0, extra={"ours_only": True, "loop_lines": 8})
    s.add("stab-selfosc-mod", held("A-4", 160), 160, tail=1.0, **SOURCES_OFF, vcfRes=1.0, vcfFreq=0.4,
          vcfMod=0.5, lfoRate=0.3, vcfEnv=0.5, decay=0.5, sustain=0.5, extra={"ours_only": True, "loop_lines": 16})
    s.add("stab-noise-r100", held("A-4", 160), 160, tail=1.0, sawLevel=0.0, noiseLevel=1.0, vcfRes=1.0, vcfFreq=0.5,
          extra={"ours_only": True, "loop_lines": 16, "check_low": False})
    s.add("stab-all-max", held("C-4", 160), 160, tail=1.0, sawLevel=1.0, pulseLevel=1.0, subLevel=1.0, noiseLevel=1.0,
          vcfRes=1.0, vcfFreq=0.0, vcfEnv=1.0, vcfMod=1.0, vcoMod=1.0, lfoRate=1.0, lfoWave=3, volume=1.0,
          extra={"ours_only": True, "loop_lines": 16, "check_low": False})
    return s


def probes():
    """Roland-only: what the plug-in's own switches and ranges do, in raw values."""
    s = Specs()
    def probe(name, notes, lines, roland, **params):
        return s.add(name, notes, lines, extra={"roland_only": True, "roland": roland}, **params)
    for raw in range(6):     # osc1Range 64'/32'/16'/8'/4'/2': which raw value plays A4 where
        probe(f"probe-range-raw{raw}", held("A-4"), 8, {"VCO RANGE": raw})
    for raw in range(3):     # SUB OSC TYPE: which raw value is which mode
        probe(f"probe-sub-raw{raw}", held("A-3"), 8, {"SUB OSC TYPE": raw}, sawLevel=0.0, subLevel=1.0)
    for raw in range(6):     # MODULATOR WAVEFORM SIN/TRI/SAW/SQR/RANDOM/NOISE, on the VCO
        probe(f"probe-lfo-raw{raw}", held("A-4", 24), 24, {"MODULATOR WAVEFORM": raw}, lfoRate=0.5, vcoMod=0.3)
    for raw in range(6):     # VCO PULSE WIDTH MOD 0,1|2,3,4|5 = MAN, LFO, F.ENV, A.ENV
        probe(f"probe-pwm-raw{raw}", held("A-3", 16), 16, {"VCO PULSE WIDTH MOD": raw}, sawLevel=0.0,
              pulseLevel=1.0, pulseWidth=0.6, lfoRate=0.4, decay=0.5, sustain=0.0, vcaMode=1)
    for raw in (0, 10, 20):  # MASTER TUNE 430/440/450 Hz
        probe(f"probe-tune-raw{raw:02d}", held("A-4"), 8, {"MASTER TUNE": raw})
    for raw in (1, 11, 23):  # BEND RANGE 2/12/24 semitones, full bend up then down
        s.add(f"probe-bend-raw{raw:02d}", held("A-4", 16), 16, bendVco=1.0,
              extra={"roland_only": True, "roland": {"BEND RANGE": raw},
                     "bend": [{"line": 4, "value": 1.0}, {"line": 8, "value": -1.0}, {"line": 12, "value": 0.0}]})
    # The bipolar depths' lower half (no hardware counterpart): ENV and KYBD inverted.
    probe("probe-env-neg", held("A-4", 16), 16, {"VCF ENV": 0}, **SOURCES_OFF, vcfRes=1.0, vcfFreq=0.8,
          decay=0.5, sustain=1.0)
    for n in ("F-3", "F-5"):
        probe(f"probe-kybd-neg-{n.replace('-', '')}", held(n), 8, {"VCF KEY FOLLOW": 0}, **SOURCES_OFF,
              vcfRes=1.0, vcfFreq=0.5)
    # VCF FREQ in fine steps (self-oscillation; complements the suite's tenths).
    for raw in range(0, 256, 16):
        n = 64 if raw < 40 else 8
        probe(f"probe-freq-raw{raw:03d}", held("A-4", n), n, {"VCF FREQ": raw}, **SOURCES_OFF, vcfRes=1.0)
    probe("probe-freq-raw255", held("A-4"), 8, {"VCF FREQ": 255}, **SOURCES_OFF, vcfRes=1.0)
    # MODULATOR VCO DEPTH in raw steps (its law is steep): triangle at RATE 4.
    for raw in range(128, 256, 16):
        probe(f"probe-vcodepth-raw{raw:03d}", held("A-4", 24), 24, {"MODULATOR VCO DEPTH": raw}, lfoRate=0.4)
    probe("probe-vcodepth-raw255", held("A-4", 24), 24, {"MODULATOR VCO DEPTH": 255}, lfoRate=0.4)
    # VCA TONE (Roland's added brightness tilt) at both ends: kept at the centre everywhere else.
    for raw in (0, 255):
        probe(f"probe-tone-raw{raw:03d}", held("A-3"), 8, {"VCA TONE": raw})
    return s


def main():
    os.makedirs(os.path.join(HERE, "specs"), exist_ok=True)
    for name, fn in (("suite", suite), ("calibration", calibration), ("stability", stability), ("probes", probes)):
        specs = fn()
        with open(os.path.join(HERE, "specs", name + ".json"), "w") as f:
            json.dump(specs, f, indent=1)
        ours_only = sum(1 for x in specs if x.get("ours_only"))
        roland_only = sum(1 for x in specs if x.get("roland_only"))
        print(f"specs/{name}.json: {len(specs)} specs ({ours_only} ours only, {roland_only} Roland only)")


if __name__ == "__main__":
    main()
