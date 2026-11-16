# Roland SH-101 plug-in (Roland Cloud / ACB): parameters and differences from the hardware

Installed version: **1.2.0** (AU `jp.co.roland.SH101AuPlugin`, component "Roland: SH-101", subtype `Vas0`; there is also a VST3 and an AAX build). This is the 2014 "SH-101 PLUG-OUT Software Synthesizer", now sold through Roland Cloud.

This is the repo's copy of `private/docs/roland-plugin/notes.md`; the sources it cites are in
`private/docs/roland-plugin/`. What the plug-in's controls actually do, measured by rendering it,
is in `roland-measurements.md`.

Sources in `private/docs/roland-plugin/`:
- `SH-101_Manual_E.pdf`: the manual bundled in the plug-in at `Contents/Resources/Manual/`. It is byte-identical to Roland's current download `PG-SH101_Manual_eng06_W.pdf` (edition 06, © 2017, PDF built 2024-12-04, 13 pages). Page references below ("M p.N") are to this file.
- `SH-101_PlugoutSoftSynth_OM-2014.pdf`: the first edition (2014, 10 pages). It has the same synth panel. The later editions add memory banks, SYSTEM-1m, and SYSTEM-8 PLUG-OUT and correspondence pages.
- `plugin-script-parameters.txt`: names, raw ranges and defaults read from the plug-in's own `Script.xml` and `ScriptEdit.xml`, which are plain XML. The manual gives no numeric ranges, so the ranges below come from this file ("X").
- `EXM004_SH101_ParameterGuide_eng01_W.pdf`: the ZEN-Core "SH-101 Model Expansion" parameter guide (2021). It covers a *different* Roland product (ZENOLOGY, FANTOM and JUPITER-X) and is kept because it explains CONDITION (see the end of this file).

Hardware page references ("OM p.N") are to `private/docs/sh101-owners-manual-archiveorg-text.pdf`. Its PDF page numbers match the printed numbers.

## Parameters (main window, SH-101 layout; M p.4)

All continuous controls are 8-bit (0–255) unless stated. "Bipolar" means the UI shows −128…+127 with 0 at the centre (X, `offsetValue=-128`).

| Section | Parameter | Values / range | Default |
|---|---|---|---|
| MODULATOR | WAVEFORM | SIN, TRI, SAW, SQR, RANDOM, NOISE (6 positions) | SIN |
| | RATE | 0–255 free. With TEMPO SYNC on: 2/1 … 1/128, including dotted and triplet values (25 divisions) | 0 |
| | VCO (mod depth to pitch) | 0–255, bipolar | centre |
| | VCF (mod depth to cutoff) | 0–255, bipolar | centre |
| VCO | RANGE (FEET) | 64′, 32′, 16′, 8′, 4′, 2′ (X string table `osc1Range`) | 16′ |
| | PULSE WIDTH | 0–255 (manual width when MOD = MAN; depth otherwise) | 0 |
| | MOD (PWM source) | MAN, LFO, F.ENV (VCF envelope), A.ENV (VCA envelope) | MAN |
| SOURCE MIXER | Pulse/Square level, Saw level, SUB OSC level, NOISE level | 0–255 each | Square 255, others 0 |
| | SUB OSC TYPE | 1 OCT DOWN, 2 OCT DOWN, 2 OCT DOWN (narrow pulse) | 2 OCT DOWN |
| VCF | FREQ | 0–255 | 255 |
| | RES | 0–255 | 0 |
| | ENV | 0–255, **bipolar** ("direction and amount", M p.4) | centre |
| | KYBD (key follow) | 0–255, **bipolar** | centre |
| | VCF ADSR | A, D, R 0–255; S 0–255 | 0, 0, 255, 0 |
| VCA | TONE | 0–255, bipolar ("brightness") | centre |
| | MODE | ENV, GATE | ENV |
| | VCA ADSR | A, D, R 0–255; S 0–255 | 0, 0, 255, 0 |
| ENV | TRIG | GATE+TRIG, GATE, LFO | GATE+TRIG |
| PORTAMENTO | MODE | OFF, AUTO, ON | ON (X default 0 = ON) |
| | TIME | 0–255 | 0 |
| Controller | BEND RANGE | 1–24 (semitones, MIDI pitch bend) | 2 |
| | TEMPO SYNC | off/on; syncs LFO RATE and DELAY TIME to the host, 40–300 BPM (M p.4) | off |
| ARPEGGIO | ARPEGGIO | off/on | off |
| | ARP TYPE | 1OCT UP, 1OCT UP+DOWN, 1OCT DOWN, 2OCT DOWN, 2OCT UP+DOWN, 2OCT UP | 1OCT UP |
| | ARP STEP | 1/4, 1/8, 1/16, 1/4T, 1/8T, 1/16T | 1/4 |
| EFFECTS | CRUSHER | 0–255 | 0 |
| | REVERB (level) | 0–255 | 0 |
| | DELAY (level) | 0–255 | 0 |
| | TIME (delay) | 0–255 free; synced: 1/32 … 1/1, including dotted and triplet values | 0 |
| Global | VOLUME (PATCH LEVEL) | 0–255 | 128 |
| | TUNE (master) | 430–450 (X: raw 0–20, offset +430; presumably A4 in Hz) | 440 |
| SYSTEM-1 extras (X; also on the panel) | KEY HOLD | off/on | off |
| | OCTAVE | −3…+3 | 0 |
| | SCATTER TYPE / DEPTH | 1–10 / −5…+5 | 1 / 0 |
| Hidden ("Show Added Parameters" in the OPTION menu; X and view-binary strings; **not in the manual**) | CONDITION | 0–255, bipolar | centre |
| | CIRCUIT MOD "AMOUNT" | 0–255 | 0 |

Other parts of the plug-in:
- Memories are kept in banks of 64 patches with 16-character names. Banks can be imported, exported, sent and fetched (M pp.5–7).
- Skins: SH-101 Layout in Gray, Red or Blue, or the SYSTEM-1 Layout (M p.12).
- SYSTEM-1 and SYSTEM-8 PLUG-OUT and control-surface use; the SYSTEM-8 correspondence table is on M p.11.
- `voiceCount` is fixed at 1 (X), so the plug-in is monophonic.

## Differs from the hardware (1982 SH-101)

- **Two ADSR envelopes.** The plug-in has separate VCF and VCA envelopes, and PWM can follow either (F.ENV or A.ENV). The hardware has a single ENV that drives both the VCF and the VCA (OM p.28), and its PWM switch is LFO / MAN / ENV (OM p.25).
- **LFO waveforms.** The plug-in has 6 waveforms (adds sine and saw). The hardware has 4: triangle, square, RANDOM (S/H) and NOISE (OM p.30).
- **VCO range.** The plug-in's string table lists 64′ and 32′ in addition to the hardware's 16′–2′. The hardware range is 16′, 8′, 4′, 2′ (service notes spec; OM p.24). Measured (`roland-measurements.md`): the labels are two octaves low. Raw 0–3 ("64′"–"8′") play MIDI notes at the hardware's 16′–2′ pitches, and raw 4–5 add two octaves above 2′.
- **Bipolar depths.** VCF ENV, KYBD and both MODULATOR depths are bipolar, so negative amounts are possible. On the hardware they are 0–10 unipolar sliders.
- **Added controls:**
  - VCA TONE (a brightness tilt).
  - The effects section: CRUSHER, REVERB, and DELAY with level and time.
  - PATCH LEVEL and patch memories.
  - TEMPO SYNC to the host.
  - KEY HOLD, OCTAVE ±3, and the SCATTER effect from the SYSTEM-1.
  - The hidden CONDITION and CIRCUIT MOD AMOUNT.
- **Bender and controllers.** The plug-in has MIDI pitch bend with a range of 1–24 semitones that acts on pitch. Its patch data also contains unused BEND SENS PITCH and FILTER and MOD SENS fields (X, `NAME2`). The hardware has a bender lever with three sliders: VCO bend (up to ~1200 cents), VCF bend, and LFO MOD depth when the lever is pushed (OM p.31). The optional MG-1 grip is covered on OM p.40.
- **Arpeggio.** The plug-in has 6 types over 1 or 2 octaves, at tempo-synced divisions. The hardware has UP, U&D and DOWN, clocked by LFO/CLK RATE or EXT CLK IN (OM pp.32, 38). Its range follows the keys held and the TRANSPOSE switch.
- **No step sequencer in the UI.** The hardware has a 100-step sequencer (OM pp.33–35) and key transpose (OM p.36). The plug-in's patch struct `PATCH3` holds SYSTEM-8-style `STEP SEQ` data (64 steps × 8 voices), but no panel control refers to it.
- **No hardware TRANSPOSE switch, CV/GATE, EXT CLK or HOLD jack.** The plug-in is MIDI only. The manual gives no fixed CC map; MIDI learn is offered only as "Set MIDI Control Mapping for SYSTEM-1" (M p.12).
- **Key priority.** The hardware uses low-note priority in GATE mode and last-note priority in GATE+TRIG (OM p.29). The plug-in manual describes only the triggering, not the priority.
- **Velocity.** The view state has a `velSense` switch (X, not in the manual). The hardware has no velocity.

### Related, not the plug-in: the ZEN-Core "SH-101 Model Expansion" (EXM004 guide)

The guide documents a separate Roland model of the SH-101 for ZENOLOGY, FANTOM and JUPITER-X:
- **CONDITION:** "Simulates the changes that occur as a unit ages."
- **PITCH DRIFT.**
- **Key modes:** SOLO, POLY, UNISON and SL-UNISON.
- **Response curves:** ORIG, LIN, EXP-1 and EXP-2.
- **PRM EXP**, which extends the LFO rate, cutoff, resonance and env depth ranges.
- **Filter "vintage" types R, M and S.**
- **Aftertouch and MFX.**

Unlike the plug-in, it keeps the hardware's PW MODE (LFO / MAN / ENV), RANGE (16′–2′) and a single ENV. Its LFO list is TRI, SQR and S&H.
