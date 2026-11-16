# Roland SH-101

Emulation of the Roland SH-101: machine `sh101` in Lemus, the instruments host, and its own
plugin, **ro-mono-101** (AU, VST3 and a standalone app; plugin code `R101`).
- **Sequencer and arpeggio:** the original TMP80C49P-6-7301 firmware runs on an emulated MCS-48
  CPU with the 101's key matrix, switch columns, 6-bit DAC and sample-and-holds, and P2 latch. The
  100-step sequencer, the arpeggio, key priority and KEY TRANSPOSE are therefore the real thing.
- **The synth board** is modelled from the service-notes schematic part by part:
  - portamento with its AUTO switching;
  - the pitch summer and the bender board with the grip LFO;
  - the CEM3340 VCO with its PWM network and the 4013 sub-oscillator;
  - the noise source and the source mixer;
  - the MODULATOR;
  - the IR3109 VCF with its diode-clamped resonance loop and cutoff CV summer;
  - the discrete ENV generator with its TRIG circuit;
  - the BA662A VCA and the output stage;
  - jack board A's power-on mute.

## ROM

The CPU's mask ROM is `tmp80c49p-6-7301.ic6` (2 KB, CRC 4d9cb13c, SHA1
a84bb1758582dbb800001db3663eaf3fb9d0e25d). That is Joe Britt's dump; MAME has no SH-101 set.
`docs/firmware.md` explains why it is the good dump and the other one in circulation is not. Put
it anywhere in your Lemus ROM folder, under any name. Lemus ships no ROM data.

Without it the SH-101 runs in **voice mode**: MIDI plays the synth board directly
(`core/voice_keys.h`), doing what the firmware does in normal playing (the same KCV for a note, the
keyboard's note priority for the ENV trigger setting, the HOLD pedal, CLK RST in LFO trigger mode).
It sounds as it does with the ROM. There is no sequencer or arpeggio, the panel's keys and buttons
don't respond, and the MODULATOR's RANDOM wave holds still, since the firmware steps it. A session
saved in voice mode keeps the sequence for when the ROM is there.

The panel's brand marks come from your own artwork; Lemus ships none. Each of the two places has a
file of its own, an SVG or PNG named after the plugin and the place, anywhere in the ROM folder
(`ro-mono-101-maker.png`; `artwork/sh101/maker.png` or `artwork/sh101.zip` also work, and win). The
places are `maker` (the Roland logo, 63.5 x 9.7 mm) and `model` (the dot-matrix SH-101, 87.0 x 21.4
mm), the panel's logo boxes in `docs/panel.json`. Each file is drawn as it looks, fitted to its box
with its proportions kept and centred; only what it draws counts (not a page's margins or a PNG's
transparent border), and transparent parts show the panel. A PNG at 8.8 px/mm stays sharp in the
largest window. ROMs on the bar lists the places with their sizes and what each has, opens the
folder, and reads it again (Rescan), so the panel changes without a restart. The older single-SVG
pack, `sh101.svg` (or an SVG in `sh101.zip`) anywhere in the folder with the ids `roland` and
`sh101` in panel millimetres (origin at the rear-left corner of the case's top view), still fills
the places without a file, as outlines in the print colour. Earlier builds wrote a blank one
(`blank_artwork.h`) into the ROM folder; an untouched copy is taken out when the SH-101 loads.

## Building and tools

The machine builds into Lemus from the repo root (see the top-level README). Its own tools:

    cmake -S . -B build/sh101 -DLEMUS_MACHINES=sh101
    cmake --build build/sh101 -j --target plugin_sh101_All sh101plugintest sh101render sh101seq sh101bench sh101oscblocks sh101filterblocks sh101jackblocks sh101voiceblocks

- **`sh101plugintest`** runs the machine's processor headless, covering:
  - MIDI: note folding, the hold pedal, repeated notes, chords into the arpeggio;
  - the host-synced and free-running clocks;
  - the sequencer and arpeggio;
  - pitch at every sample rate;
  - state round trips;
  - 120 s stability runs and random-settings stress;
  - POWER and the power-on mute;
  - voice mode.

  It reads the ROM from `../private/roms/sh101/`. `--panel OUT.png [SCALE] [off|voice|keys|held]
  [--artwork PACK.svg]` draws the panel into a PNG instead (keys: the key list open; held: HOLD and C4
  held by a right-click).
- **`sh101render`** renders test specs through the processor to WAV, for `tools/compare/`.
- **`sh101bench`** times a set of workloads at 44.1 and 48 kHz (CPU %, Mcyc/s, Mins/s, the worst
  block) and checks them against golden WAVs (`golden DIR`, `check DIR`, `time`), so speed work can
  show it leaves the sound alone. `idle` lists a few patches' resting levels after a note and
  whether the voice sleeps.
- **`sh101seq`** scripts the panel against the firmware and logs the DAC holds, P2 and RAM.
  `--selftest` checks everything `docs/sequencer.md` states.
- **`tools/service/check.py`** runs the service notes' adjustments and specifications against the
  emulation and says pass or fail for each: `../../.venv/bin/python tools/service/check.py`.
- **`tools/compare/`** holds the comparison with Roland's SH-101 plug-in:
  - `make_suite.py` writes the specs;
  - `roland_render.py` renders them through Roland's plug-in hosted in Onyx Tracker;
  - `compare.py` and `measure.py` compare and measure;
  - `fit_roland.py` fits the Roland calibration;
  - `real.py` measures recordings of real units;
  - `units.py` compares the model with them per unit and per event, with a limit for each row
    (`../../.venv/bin/python tools/compare/units.py`, about 20 s).
- **`tools/spice/osc`, `tools/spice/filter`, `tools/spice/jack`, `tools/spice/voice`:** ngspice
  netlists of the transcribed schematic, block by block and the whole voice, with
  `sh101oscblocks` / `sh101filterblocks` / `sh101jackblocks` / `sh101voiceblocks` running our
  model of the same circuit. `tools/spice/suite.py` runs every comparison and holds each to its
  limit (about four minutes). The scripts take the build from `SH101_BUILD` or `build/sh101`.

## Using it

The editor is the top panel, drawn from measurements of a straight-on photo (legends and controls
placed to about 0.5 mm), brand marks from your own artwork when your ROM folder has some. The bar
has SYNC (OFF, or HOST with its step: 1/16, 1/8, ...) and `?` (the key list), and works as on
every Lemus machine (the top README, "Machine panels").
- **Sliders:** drag them; shift is fine, double-click resets.
- **Knobs and switches:** drag knobs; click or drag the switches.
- **Keys and buttons:** press the 32 keys and the sequencer, arpeggio, HOLD and KEY TRANSPOSE
  buttons. Their LEDs are driven by the firmware. A right-click (ctrl-click on a Mac) holds a key
  or button down until it is clicked again, for the steps that need two at once (HOLD with a key
  for a legato step, KEY TRANSPOSE with a key); Escape lets go of everything.
- **POWER** switches the unit off (every LED dark, keys and notes ignored, the CPU stopped; the
  battery keeps the sequence) and on. Switching off, the rails fall: within a millisecond jack
  board A's power-off mute (TR8, C14) shunts the output, and IC1a's fall to ground leaves a faint
  thump (about −41 dBFS, over the next 20 ms); after half a second the output is exact zeros. Switched on, the CPU boots and jack board A
  mutes the output while C13 charges: a note is held about 30 dB down for the first quarter
  second, its negative half comes back first, and the output is open after about 1.4 s (the bar
  says MUTED meanwhile). A new instance is a unit that has been on a while, as on every Lemus
  machine: no switch-on mute, only POWER runs it. A session always loads switched on.
- **Bender:** drag the lever sideways to bend and forward for LFO MOD; it springs back.

The sequencer and arpeggio work as on the hardware (see the owner's manual,
`private/docs/sh101-owners-manual.pdf`, and `docs/sequencer.md`):
- **Load:** press LOAD and play one key per step. Hold HOLD (LEGATO) with a key for a legato step;
  KEY TRANSPOSE (REST) enters a rest.
- **Play:** press PLAY.
- **Arpeggio:** press DOWN, U&D or UP and hold two or more keys; HOLD latches them.

Like a new unit, a new instance has no sequence. The sequence memory is saved with the session,
with the model version (`kModelVersion`), so that a later change to the sound can keep old
projects as they were.

- **Sync** (off by default) works like a cable in EXT CLK IN:
  - With SYNC on, the host transport clocks the arpeggio and sequencer, a step per 1/16 (clicking
    SYNC steps through 1/16, 1/8, 1/4, 1/32, 1/16T, 1/8T and back to OFF; shift-click goes back),
    starting on the grid. While the host is stopped there is no clock.
  - With SYNC off, the MODULATOR's square clocks them, as with nothing plugged in.

  Either way the arpeggio and sequencer run only when PLAY or an arpeggio button is on, never from
  MIDI notes alone.
- **Calibration:** the plugin plays the *Real unit* calibration, fitted to recordings of a real
  SH-101:
  - the VCO at 440 Hz;
  - the IR3109's slope as measured on an original chip, with FREQ 0 where that unit's sits;
  - its ENV sweep (7.2 octaves at ENV 10, which a second unit confirms) and ENV times;
  - its portamento.

  Two more are kept for the tools (`sh101render --calibration`), not the plugin: *Service notes*,
  the unit trimmed as the service notes specify with nominal parts (VCO at 442 Hz for A4, VCF WIDTH
  for 1 oct/oct, the data sheets' values for the IR3109's expo), for the service checks; and
  *Roland*, Real unit with the ENV's depth and times and the noise level set to Roland's SH-101
  plug-in, for A/B against it.

  VR8 is re-trimmed by the service procedure in each, and all three pass the long-run stability
  checks.
- **MIDI** plays the keyboard, so the firmware does note priority, the arpeggio and step entry
  itself.
  - Notes 53–84 are the 32 keys at the panel's TRANSPOSE. Notes beyond them move TRANSPOSE or fold
    by octaves.
  - Pitch bend moves the bender lever, CC 1 pushes it (LFO MOD), and CC 64 is the HOLD pedal.
  - Controllers, after Roland's SH-01A:

    | CC | Control | CC | Control | CC | Control |
    |---|---|---|---|---|---|
    | 3 | LFO RATE | 19 | PULSE level | 27 | BENDER VCF |
    | 5 | PORTAMENTO | 20 | SAW level | 28 | VCA mode |
    | 12 | LFO waveform | 21 | SUB level | 29 | ENV trigger |
    | 13 | VCO MOD | 22 | SUB OSC | 30 | S |
    | 14 | RANGE | 23 | NOISE | 31 | portamento mode |
    | 15 | PULSE WIDTH | 24 | VCF ENV | 71 | RES |
    | 16 | PWM source | 25 | VCF MOD | 72 | R |
    | 17 | BENDER LFO MOD | 26 | KYBD | 73 | A |
    | 18 | BENDER VCO | | | 74 | FREQ |
    | | | | | 75 | D |
    | | | | | 76 | TUNE |
    | | | | | 77 | TRANSPOSE |
- **Computer keyboard:**
  - A W S E D F T G Y H U J K O L P ; ' are note keys from C; Z and X shift the octave.
  - 1–7 are LOAD, PLAY, DOWN, U&D, UP, HOLD and KEY TRANSPOSE.
  - Space is PLAY in the standalone app; in a plugin it goes to the host's transport.
  - Left and Right bend, Up pushes the lever, Esc releases everything, and `?` shows the list.

In Onyx Tracker a plugin is only mixed once a track has played a note on it. A song that only runs
the 101's own sequencer or arpeggio therefore needs one note on its track.

**Idle.** Once a note has died away (gate closed, output under −120 dB for 100 ms, ENV, VCA and
portamento settled) the voice sleeps: it puts out exact zeros and skips the synth board model,
about 0.5 % of a core instead of about 4–8 % (`sh101bench` on an M5 Max at 48 kHz, 2026-11-14: a
MIDI line 4.2 %, a bright patch 6.3 %, a self-oscillating filter 8.1 %). A note, the arpeggio or
sequencer, a control or the
bender wakes it; the CPU and the MODULATOR keep running. A bright patch (VCF open, the sources
up, VCA in ENV mode) stays awake: its VCA still leaks about −115 dBFS at rest (R196 holds S6's
contact 0.27 V above the ENV), which is part of the sound. The tail is 36 s: RELEASE at maximum keeps the output above −90 dB that long.

## Layout

| Path | What |
|---|---|
| `sh101.cpp` | The driver: ID, name, ROM (optional: voice mode), artwork pack, and how Lemus creates the machine |
| `blank_artwork.h` | The blank artwork pack earlier builds wrote into the ROM folder, to recognise untouched copies |
| `icon.png` | The standalone app's icon (`tools/icon/make_icon.py 101`) |
| `core/mcs48.*` | MCS-48 CPU, ported from MAME's core (BSD-3-Clause) |
| `core/machine.*` | The CPU board: key matrix, switch columns, DAC and S/Hs, P2 latch, sequence reader and writer |
| `core/midi.h` | The MIDI keyboard driver |
| `core/voice_keys.h` | Voice mode: MIDI straight to the synth board's lines |
| `core/osc*`, `core/lfo*` | The oscillator section and the MODULATOR (`docs/model-osc.md`) |
| `core/filter*`, `core/vcf_*`, `core/env_*`, `core/vca_*` | The filter section (`docs/model-filter.md`) |
| `core/jack_board.*` | Jack board A's mute: TR7, its power-on and power-off drives and the C2 loop |
| `core/voice.*`, `core/oversampler.*` | The two sections joined, oversampled and decimated |
| `plugin/` | JUCE processor, editor, `HardwareView`, generated `PanelData.h` |
| `tools/` | See above; also `panel/gen_panel.py` and `dis49.py`, an MCS-48 disassembler |
| `docs/` | `research.md`, `firmware.md`, `sequencer.md`, `circuit-osc.md`, `circuit-filter.md`, `model.md`, `model-osc.md`, `model-filter.md`, `panel.md`/`panel.json`, `roland-plugin.md`, `roland-measurements.md` |
| `private/` | Not in the repo; see below |

## Private material

The docs and tools refer to material this repo can't contain:
- the service notes and the owner's manual;
- schematic renders and crops;
- data sheets, papers and forum threads;
- photos;
- Roland's plug-in renders and recordings of real units;
- MAME sources.

It lives in the Lemus private folder (`~/src/lemus/private/sh101`) at the paths the docs give, and
`tools/link-private.sh` links it in here as `private/`. The ROM and artwork pack are in
`~/src/lemus/private/roms/sh101/`, and the ROM download script is in `private/tools/`.

## Status

Checked five ways; the numbers are in `docs/model.md`, "Validation", and what is known to be wrong,
with the plan, in `docs/improvements.md`.

- **Service notes:** every adjustment and specification passes (`tools/service/check.py`, 34 of 34).
  - D/A in test mode, through the firmware.
  - VCO TUNE, WIDTH and RANGE WIDTH, through the whole machine.
  - VCF WIDTH.
  - The cutoff, ENV, LFO, portamento, PWM, TUNE and output ranges.
  - The sub-oscillator waveforms.
- **The firmware:** `sh101seq --selftest` checks the service notes' test-mode table, the sequence
  format, PLAY, the arpeggio, key priority, KEY TRANSPOSE, the clock and RANDOM against the ROM.
- **The schematic, in SPICE:** every block of both sections matches a simulation of the
  transcribed schematic (`tools/spice/suite.py`, 28 checks):
  - the LFO, portamento, grip LFO, PWM, mixer and pitch summer within 0.01–1.1 %;
  - the VCF with the IR3109 at transistor level, built from the decode: within 0.09 dB small-signal
    at 10 µA and 0.07 dB at 1 µA, self-oscillation within 6 cents up to 1 kHz and 22 cents at
    4.8 kHz (the decode's input pair losing transconductance with current, which a real chip's law
    doesn't show) and 0.14 dB, a saw's harmonics within 0.14 dB;
  - the cutoff CV within 2.5 cents; the ENV within 18 mV RMS and 0.25 % in time;
  - the VCA and output within 0.08 dB;
  - jack board A's mute, clamp and power-off within 3.3 mV and 0.05 dB, and 0.9 mV after the
    first 2 ms of switching off;
  - the whole voice, every block joined into one circuit from the VCO's outputs to the jack
    (`tools/spice/voice`, 43 cases): with the filter open, level within 0.11 dB, harmonics within
    0.33 dB and a waveform error of −33 to −40 dB; the VCF's cutoff current within 4 cents and its
    self-oscillation within 9 cents; the ENV to the VCA within 0.05 dB.
- **Real units:** recordings of nine real SH-101s, per unit and per event (`tools/compare/units.py`:
  105 rows, 60 within their limits, 4 known gaps, 41 for information; settings read off video frames,
  `docs/online-references.md`):
  - the Real unit calibration's unit: its rest, plateau and sweep (7.28 against 7.27 octaves),
    release τ within 0.04 s;
  - a second unit's ENV sweep in two takes, each within 0.11 octave;
  - portamento at the top on another unit within 0.01 s;
  - the square and subs re the saw within 1 dB on every note of three units;
  - the resonance's height at two of three slider readings, and its peak moving with RES within
    0.04 octave;
  - the noise's colour on three units, each with its own saw taken out (its filter's top and its
    recording chain), and its level inside their 2.7 dB spread;
  - the filter's top: a saw at FREQ 10 at least as bright as the brightest unit's recording.

  Known gaps, with the plan, are in `docs/improvements.md`: one unit's FREQ law reads 5–8 % steeper
  per division and its top RES reading 0.4 division past ours, and the LFO's top rate and FREQ's
  bottom sit at the edge of one unit's reading, inside the parts' spread.
- **Real boards:** part values read from a dismantled unit's parts list (Din Sync), photos of
  boards from six other units and the service notes' own board diagram. They corrected four of the
  modulator's resistors to 100k (R169, R188, R31 and R35), put R196 on S6's side of R197, which is
  why a real unit's VCA fades with the ENV to the end, and confirmed the ENV and cutoff paths
  (`docs/online-references.md` §5). A real unit's sound confirms R31: its square LFO swings 0.74 of
  the triangle's, as 100k gives (`docs/circuit-osc.md` 5.3).
- **Another IR3109:** a Juno-106 voice measured end to end (KR-106's data; the same chip, with the
  same parts on each stage) gives the chip's law: exponential within a few cents from 6 Hz to
  10 kHz, compressing above. The model follows it (`docs/model-filter.md` 2).

### Compared with Roland's SH-101 software

Not a reference, but the obvious comparison (`tools/compare/`, `docs/roland-measurements.md`):
mean log-spectral distance 5.5 dB over 205 tests with the Roland calibration, 6.1 with the Real
unit's, which follows the real units' brighter filter top where Roland's plug-in is darker (8.7 dB
with the service notes', most of it their 442 Hz tuning). With the Roland calibration the VCF is
at 2.7 dB, the mixer 4.0, the ENV 3.5. The parts-spread study (`docs/spread.md`) shows where Roland's plug-in goes beyond what
any real unit does: its portamento is twice as slow, its FREQ law is straight where every unit's
bends, and its KYBD pivots elsewhere. The Roland calibration (the tools') is fitted to it.

### Known differences

- The IR3109 has no maker's data. It is modelled from Open Music Labs' decode and the AS3109 data
  sheet, with its law end to end from one Juno-106 voice. Its expo reference is fitted to a real
  unit at 1.7 times the decode's value. Its buffers' gain (0.89–0.96) is uncertain by a few
  percent.
- As transcribed, the schematic can't sweep less than 7.7 octaves at ENV 10 while KYBD tracks, yet
  two real units sweep about 7.2 and Roland's plug-in 7.65. The Real unit and Roland calibrations
  scale the ENV's weight to match, and the cause is an open question (`docs/model.md`). The
  measurement, the tracking, the chip's law, the parts on that path and the VCA switch are ruled
  out.
- The portamento switch's contact order is inferred. R162, missing from the schematic, is
  where the service notes' own board diagram and every board photographed put it.
- RATE 10 runs 36.4 Hz; a real unit's 38.2 Hz is inside the parts' spread (33–42 Hz).
- The noise level and colour are set from real units (the noise level varies by 3 dB between
  them); the schematic gives neither.
- Switched off, +9 V is taken to fall with a 12 ms time constant (the 1 W rating's current from
  the rail's capacitors), and the rest of the voice to stop once the +15 V regulator drops out.
  The thump's level depends on that fall: −30 dBFS at 5 ms, −41 at 12, under −45 at 25.
- Jack board A's mute, as drawn, also limits loud negative peaks once switched on: TR7's
  base-collector junction conducts through R35, D4 and D3 (or R36) when the output swings below
  about −1 V, so a 1 V peak sine loses 0.6 dB on its negative half and a 2 V one 2.4 dB, through
  C2. ngspice with the project's standard 2SC1815 and 1S2473 models agrees within 0.05 dB
  (`tools/spice/jack`); a real unit's diodes at microamps may start it a little later.

### Not emulated

- CV/GATE in and out, and the EXT CLK jack (SYNC stands in for a clock cable).
- PHONES.
- The battery (the RAM always keeps the sequence).
- The MGS-1 grip's controls beyond the lever's MOD push.
