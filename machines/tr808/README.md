# Roland TR-808

Emulation of the Roland TR-808 Rhythm Composer: machine `tr808` in Lemus, the instruments host,
and its own plugin, **ro-drums-808**. The sequencer is the real thing: the original NEC
uPD650C-085 firmware runs on an emulated uCOM-43 CPU with the 808's four pattern RAMs, switch
matrix, LEDs, status gate and DIN-sync inputs. The voices are the voicing board itself: the common
trig and accent circuit, the noise generator, the six-oscillator metal source, all eleven voice
circuits and the mixer, each run sample by sample from a netlist transcribed part by part from
the service notes (`core/netlists/`), by a circuit solver written for it (`core/circuit.*`).

## ROM

The sequencer needs the CPU's mask ROM, `d650c-085.ic4` (2 KB, CRC 06f0c405, SHA1
eddac7af6396c3f220914d9cad2c9e398872b034), the image in MAME's `tr808` set. Put it, or MAME's
`tr808.zip`, anywhere in your Lemus ROM folder. Lemus ships no ROM data.

Without the ROM the TR-808 loads in **voice mode**: the voicing board, played from MIDI, with no
CPU, so no sequencer, and the panel's keys do nothing. A session keeps its rhythms in voice mode,
and gets the full 808 back when it is reopened with the ROM in the folder.

The panel's brand marks come from your own artwork; Lemus ships none. As MAME keeps artwork, each
of the four places takes an SVG or PNG of its own named after the plugin, anywhere in the ROM
folder: `ro-drums-808-maker` (the Roland logo), `-name` (Rhythm Composer), `-model` (TR-808),
`-tagline` (Computer Controlled), e.g. `ro-drums-808-maker.png`. `artwork/tr808/` in the ROM
folder (or `artwork/tr808.zip`), with the plain names (`maker.png`), still works and wins. The places are the
panel's logo boxes in `docs/panel.json`. Each file is drawn as it looks, fitted to its box with its
proportions kept and centred; only what it draws counts (not a page's margins or a PNG's
transparent border), and transparent parts show the panel. A PNG at 9.6 px/mm stays sharp in the
largest window. ROMs on the bar lists the places with their sizes and what each has, opens the
folder, and reads it again (Rescan). The older single-SVG pack, `tr808.svg` (or an SVG in
`tr808.zip`) with the ids `roland`, `rhythm_composer`, `tr808`, `computer_controlled` in panel
millimetres, still fills the places without a file, as outlines in the print colour. Earlier
versions wrote a blank one (`blank_artwork.h`) into the ROM folder; an untouched copy is taken out
when the 808 loads.

## Building and tools

The machine builds into Lemus from the repo root (see the top-level README). Its own targets:

    cmake --build build/cmake -j --target plugin_tr808_All tr808plugintest tr808seq voicerun circuitrun tr808bench

- `tr808plugintest` runs the processor headless: the sequencer against a host transport (DIN
  sync) and on its own clock, MIDI and accent, sleep (exact zeros, and a hit after it sounding as
  before), state, the factory rhythms, voice mode, SERVICE (below).
  `--panel OUT.png [SCALE] [running|off] [rear] [service] [zoom=Z@X,Y] [setup=N] [--artwork PACK.svg]`
  renders the editor (SERVICE zoomed Z times onto panel mm X, Y; trimmer N's procedure on the
  scope). `--service [DIR]` runs the SERVICE checks alone and writes the view's shots, whole and
  zoomed, into DIR. It reads the ROM from `../private/roms/tr808/`.
- `tr808seq ROM SCRIPT` scripts the panel against the firmware (mode, instrument, keys held for a
  time, tempo, DIN sync, patterns written straight into RAM) and logs every step pulse with its
  instrument data, the LEDs and the RAM: how `docs/firmware.md` was worked out.
- `voicerun OUT.wav --hit TIME:VOICE[+ac] ...` renders the voicing board alone: knobs (`--set`),
  part values (`--cal voice.param=value`, `--cal nominal=1` for the schematic's unit), every jack
  as a channel (`--jacks`), board signals (`--probe`), per-voice solver statistics.
  `TR808_FULL=1` runs each voice as one netlist, `TR808_BLOCKS=1` (with `TR808_PROFILE=1`) reports
  each partition's work.
- `circuitrun NETLIST,...` runs netlists through the solver with PWL inputs and writes the same
  circuit as an ngspice deck; `tools/spice/compare_circuit.py` runs both and compares them, and
  `tools/spice/suite.py` does that for every netlist (70-odd cases, a few minutes) and tabulates
  the differences.
- `tr808bench` times the processor on fixed workloads at 44.1 and 48 kHz (the sequencer, a kit
  from MIDI, every voice, long decays, congas/claves/maracas, knob automation, idle);
  `golden DIR` / `check DIR` keep renders to show speed work leaves the sound alone (sample by
  sample, and by envelope and spectrum where a rounding change moves the free-running squares'
  phase); it counts instructions too, which a loaded machine doesn't change.
- `tools/compare/fischer.py` compares every voice with Michael Fischer's recordings of a real 808
  at every knob mark; `fit.py` fits part values to them; `marks.py` finds the rotation each knob
  tick sets on his unit (the knob laws); `look.py` plots one. `accent.py` compares the accents with
  a second real unit's; `units.py` puts the voices without knobs on all four real units and shows
  where the model falls in their spread.
- `tools/service/check.py [--unit]` runs the service notes' voice check (amplitude, accent,
  frequency, decay of every voice) and says pass or fail.
- `host/scenes/` are loops for listening to Lemus against Roland's TR-808 plugin in Lemus AB
  (`tools/host/README.md`): factory rhythms, a beat, every voice plain then accented, a long kick.
  `tools/ab/scenes.py` writes them: Roland's transport stopped (its own sequencer quiet), plain
  hits velocity 64 and accents 127 (Roland's notes set the trigger level by velocity; Lemus
  accents from 100), and only the voices both play alike (Roland's conga, claves and maracas
  notes play the toms, rim shot and clap).

Python tools run from the repo's `.venv` (`../../.venv/bin/python tools/...`).

## Using it

The editor is the top panel drawn from measurements (`docs/panel.json`, `docs/panel.md`): every
knob, switch, key and LED where the real one is, brand marks from your own artwork. Drag knobs
(shift for fine, double-click resets), click or drag the rotary and slide switches, press the
keys; the LEDs are the firmware's. The bar has SYNC, SERVICE (below) and `?` (the key list).

Programming works as on the hardware (the owner's manual; `docs/sequencer.md` has the procedure
and what was checked): PATTERN CLEAR, 1st PART / 2nd PART with START, INSTRUMENT-SELECT and the
step keys, CLEAR + step for the length, CLEAR while running to latch PRE-SCALE, MANUAL PLAY with
BASIC VARIATION, TAP for intros and fills, AUTO FILL IN, COMPOSE and PLAY for the rhythm tracks.
Hold a step key for about a step: the firmware reads them once a step. The RAM is saved with the
session. A new instance holds the factory rhythms, from the rhythm charts printed with the 808:
ROCK, BOSSA NOVA, MAMBO and SAMBA on BASIC RHYTHM 3-6, the track rhythms on 7-11, the sound check
(every voice on every step) on 12, and three fills on INTRO/FILL IN 1-3 (`core/factory.cpp`).
There is no memory clear, as there is none on the unit; the host's State: Reset makes the 808 a
new instance, its factory rhythms included.

- **Sync** (off by default): with SYNC on and the host playing, the host transport is the DIN sync
  input (RUN and a 24 ppqn clock), so the rhythm starts with the transport and follows its tempo.
  With SYNC off, START/STOP, TEMPO and FINE run the internal clock (about 30-300 BPM).
- **MIDI** plays the voices directly, as Roland's TR-08 does, alongside the sequencer: General MIDI
  drum notes (36 BD, 38 SD, 37 RS, 75 claves, 39 CP, 70 maracas, 41/43 LT, 45/47 MT, 48/50 HT,
  64/63/62 congas, 42 CH, 46 OH, 49 CY, 56 CB). Velocity 100 or more accents. The conga, claves and
  maracas notes play their shared rows; the voice switches pick the sound, as on the panel.
  Control changes move the knobs on Roland's TR-8 numbers for the same controls (its MIDI
  implementation chart): 71 ACCENT; BD 21 TONE, 23 DECAY, 24 LEVEL; SD 25 TONE, 26 SNAPPY,
  29 LEVEL; 46/49/52 TUNING and 48/51/54 LEVEL for LT, MT, HT; 57 RS, 60 CP, 63 CH LEVEL;
  OH 81 DECAY, 82 LEVEL; CY 83 TONE, 84 DECAY, 85 LEVEL; 88 CB LEVEL (the TR-8 has no cowbell; its
  RC); 7 MASTER VOLUME.
- **Outputs**: the main stereo pair is the HI output, and the eleven MULTI OUT jacks are outputs
  after it (BD, SD, LT/LC, MT/MC, HT/HC, RS/CL, CP/MA, CB, CY, OH, CH; `MachineInfo::outputs`), off
  until the host turns them on, mono or stereo (the jack on both sides). Which voices go to them is
  the 808's own: REAR in the bar shows the rear panel, where a click plugs a cable into a MULTI OUT
  jack (or pulls it out). As on the unit, a cable takes its voice out of the mix, to that output;
  with no cable a voice is in the main pair and its output is silent, whatever outputs the host
  turns on. The cables are saved with the session.
- **SERVICE** in the bar takes the top panel off, as on the 303 and the 909 (`docs/boards.md`): the
  main and voicing boards where they hang on their pots, the switch board folded out in front of
  them on its harness, jack boards A and B behind the main board's rear edge, the transformer
  beside its left one (the power supply lies under it, out of sight). Every part is where the
  service notes' board drawings put it (p.10-p.12), drawn as the part it is with its value (the
  netlists', and the INT clock's, the tempo clock's and the six squares' from the drawings), the
  copper showing faintly through (generated from the pads, not Roland's tracks). The knobs stay on
  their pots and the keys on their switches, and work as on the panel; the LEDs run. The wheel or a
  pinch zooms (the designators show close up), a drag moves, a double-click shows it all. On the
  right:
  - **Scope**: on a MULTI OUT, MASTER OUT HI or LO, a trigger line (each voice's instrument data,
    ACCENT, IC6's 1 ms, the COMMON TRIG, IC2a's narrowed trig), a test point (TP2-TP7) or any pin
    of any part the netlists draw (click its caption: by circuit, kind, part and pin); free
    running, on the probe's own rise (auto), or on a voice's gate. The readouts: the sweep's peak to
    peak, and what the procedure reads on its test point: TP2's, TP3's, TP6's and TP7's period (ms),
    TP4's AC volts (mV rms over 0.2 s), TP5's peak to peak. While a probe is on the board doesn't
    sleep.
  - **Trimmers**: the six of the notes' adjustment (SN p.14), each modelled from its part and
    circuit (`core/trimmers.h`): main TM1 INT CLOCK (100k B with R49, C11 and C203: TP2's period),
    TM2 TEMPO CLOCK (10k B through R43 220k into the tempo clock's anti-log pair: TP3), TM3 C.P.
    OFFSET (10k B through R370 47k: the clap's offset), TM4 W.N. LEVEL (22k B in IC24a's gain leg:
    the noise's level), voicing TM1 and TM2 (220k B with R44, R45 and C6, C5: the cowbell's squares,
    TP6 and TP7). Each is a parameter (`tm_int`, `tm_tempo`, `tm_cp`, `tm_noise`, `tm_cb1`,
    `tm_cb2`, after the panel's), saved with the session, turned on its board or along its row;
    a double-click puts it back to the chosen unit's trim, which its row ticks. SET UP clips the
    probe on the test point and sets the time base, volts and trigger the procedure needs; TM3's
    TEST also plays the clap every 0.6 s, unaccented. What the procedure asks of the knobs (TEMPO
    and FINE fully clockwise for TM2, CP LEVEL fully clockwise for TM3) is said, not done. At its
    trims the unit reads 1.9 ms, 8.33 ms, 157 mV rms, 1.79 ms and 1.21 ms; the notes' are 1.9 ms,
    8.33 ms, 130 mV rms, 1.85 ms and 1.25 ms (`docs/model.md`, "SERVICE").
  - **Circuit**: which unit this is, by its serial number (SN p.15 and the drawings' lot notes,
    `core/revisions.h`): SN 000001-000299 (the INT clock's C11 .068 and R49 150k; the cowbell's C6
    .01, R44 390k, R45 330k), 000300-010599 (the clap's R346 1k and R332 22k, the first noise
    generator, the voice gates' 22k; IC6's R101 isn't modelled: the common trig is an ideal 1 ms),
    010600 up (the late board, the default: Fischer's SN 103852). SNARE + CLAP: EARLY is those two
    voices as first drawn, LATE (the default) has the later values of the notes' PORTION CHANGED
    box (SN p.9, no serial given). Both are saved with the session; each change sets the trimmers
    to that unit's trims and rebuilds the board's circuits at their operating points.
- **LEVEL knobs** start at their printed standard mark (the service notes' "red mark"), the rest
  at 12 o'clock, MODE at MANUAL PLAY, TEMPO at 120 BPM.
- Computer keyboard: 1-8 and Q-I are the step keys, Backspace CLEAR, Return TAP, F1-F6 the MODE
  switch. Space is START/STOP in the standalone app only; in a DAW it goes on to the DAW's
  transport.

**Idle.** Each voice stops once its jack has settled (within 10 µV-1 mV for 0.1 s, its timing
capacitors still), and the whole board once nothing runs: it then puts out exact zeros and costs
about 2 % of a core (the CPU keeps running, so the sequencer and LEDs carry on). A trigger or a
knob wakes it. It reports a 2 s tail to the host. A host should leave it awake rather
than suspend it: suspended, the 808 can't run its own sequencer.

## Layout

| Path | What |
|---|---|
| `tr808.cpp` | The driver: ID, name, ROM (optional), artwork places, and how Lemus creates the machine |
| `core/ucom43.*` | uCOM-43 CPU, ported from MAME's ucom4 core (BSD-3-Clause, hap) |
| `core/machine.*` | The CPU board (switch matrix, RAM, LEDs, status gate, trigger lines, pattern writer, the INT clock) and the tempo clock |
| `core/circuit.*` | The circuit solver (nodal DK method, linear mode, partitions, ngspice export) |
| `core/voiceboard.*` | The voicing board: its blocks, partitions, sleep, the calibration, the scope's probes, the parts list |
| `core/revisions.h`, `core/trimmers.*` | The production changes by serial number; the six service trimmers' circuits |
| `core/parallel.*` | The voices' jobs on worker threads (the TR-909's pool) |
| `core/netlists/` | Every circuit as a netlist (`docs/circuit.md`) |
| `core/oversampler.*` | The half-band decimator |
| `plugin/` | JUCE processor, editor, `HardwareView` (the panel, drawn from `docs/panel.json`), `HardwareService.cpp` (SERVICE, drawn from `docs/boards.json`) |
| `tests/host.cpp` | The 808 through the Lemus host: ROM handling, voice mode, slots, sessions, artwork, editor |
| `host/` | A/B sources and a scene for `lemus-render` |
| `tools/` | See above |
| `docs/` | `research.md`, `circuit.md`, `firmware.md`, `sequencer.md`, `model.md`, `panel.md`/`panel.json`, `boards.md`/`boards.json`, `improvements.md` (what's known to be wrong, and the plan) |
| `private/` | Not in the repo; see below |

## Private material

The docs and tools refer to material this repo can't contain: the service notes and owner's
manuals, schematic renders and crops, data sheets, papers and forum copies, photos, real-808
recordings (Michael Fischer's per-knob set and others) and MAME's driver. It lives in the Lemus
private folder (`~/src/lemus/private/tr808`) at the paths the docs give, and
`tools/link-private.sh` links it in here as `private/`. The ROM is in
`~/src/lemus/private/roms/tr808/`.

## Status

Checked four ways; the numbers are in `docs/model.md`, "Validation".

- **The firmware:** the original ROM runs the sequencer; its I/O map and RAM format were worked
  out against the owner's manual procedure by procedure (`docs/firmware.md`), and the plugin's
  tests check it in time with a host transport (16 of 16 steps within 12 ms of the grid, 5 ms
  late as DIN sync makes it) and on its own clock.
- **The schematic, in SPICE:** every netlist against ngspice on the same circuit: BD, SD, the
  toms and congas, cowbell, clap, rim shot, accent, noise and mixer within 0.3 % RMS, maracas
  within 0.6 %, the cymbal within 1.4 % and the hats within 4 % (they run at twice the board's
  rate), the claves 5-6 % from discretisation at 96 kHz (it shrinks with the rate). Running a
  voice in partitions against running it whole: within 0.3 dB in spectrum.
- **Real TR-808s.** Michael Fischer's recordings at every knob mark (`tools/compare/fischer.py`):
  decay times within 0.15 octave (10 %) for most voices (a few single 2 ms envelope steps aside),
  every tom and conga within 12 cents at every knob mark, spectra within 0.5-2 dB log-spectral
  distance for BD, SD, cymbal, hats, congas, rim shot, claves and maracas, 2.5-3.1 dB for the
  toms, clap and the cowbell's attack (the toms' noise tails are at the metric's floor). The fit
  holds on the knob marks it wasn't fitted to. His voices' levels share one recording gain,
  interquartile range 1.0 dB over 13 voices, none more than 2.2 dB from it
  (`tools/compare/levels.py`). The voices without knobs also compare across four real units
  (`tools/compare/units.py`), where most of what's left is the units differing from each other.
- **The service notes** (`tools/service/check.py --unit`): 45 of 56 voice checks pass (46 with
  the nominal parts): every frequency, all the decays but the cymbal's, the amplitudes but those
  below. Each of the 11 fails, model (table):

  | Check | Reads | What explains it |
  |---|---|---|
  | Snare, accent | 14.5 V (10) | The drawn circuit does this in SPICE too. Reverb's unit accents its snare as the model does; the archive.org unit's snappy accents 4 dB less |
  | Low, mid, high tom, accent | 15.2 (12), 14.0 (11), 15.4 V (12) | The archive.org unit accents its toms 13 dB, as the model does, against the table's 10.7 |
  | Mid conga, level | 2.1 V (3) | R253 as printed (2.2k against the other congas' 1.5k); Fischer's mid conga is 3 dB under his low one too |
  | Rim shot, level and accent | 3.9 (3), 17.9 V (10) | Peak to peak of a spiky wave; by RMS its accent is within 0.4 dB of the archive.org unit's |
  | Maracas, level | 4.2 V (3) | Fischer's unit's hotter noise; with the nominal parts it passes |
  | Cowbell, accent | 6.3 V (12) | The archive.org unit accents its cowbell 5.6 dB and the model 5.3; the table's 10.7 dB is more than the narrowed trigger allows |
  | Closed hat, accent | 4.1 V (6) | Fischer's unit's IC2a limit; the archive.org unit's accent is within 0.5 dB |
  | Cymbal, decay | 355 ms (800) | Fischer's unit decays in 380 ms at the same setting |

### Compared with Roland's TR-808 software

Not a reference, but the obvious comparison (`tools/compare/roland.py`, 2026-10-12, the same knobs
against Fischer's unit): Lemus is the closer of the two on 95 of his 99 recordings both can play
(the clap, the open hat at DECAY 0, and the first milliseconds of BD and SD at one short setting
each the exceptions), on the knob marks it wasn't fitted to too; Roland's accents swell its hits
3-6 dB more than the real unit's.

### Known differences

All of them, with the plan, are in `docs/improvements.md`.

- The calibration is Fischer's unit's (its trims, knob laws and noise), with the clap's TM3 at the
  value three other units share. Two fitted values sit outside the parts' tolerances: the
  cymbal's R125 and the rim shot's R318 (`docs/model.md`, "Calibration").
- The clap's level follows its TM3 trim, and real units differ widely: clap against maracas reads
  -3 to +11 dB across four units (`tools/compare/levels.py`), +3.3 here. SERVICE's trimmer sets
  another unit's.
- The snare's snappy is a little short at mid SNAPPY and a little strong at the top.
- The toms' germanium diodes (1S188FM) have no data sheet; their model is a guess the accent
  behaviour of a real unit bears out.
- POWER: off is silent and dark; on restarts the firmware with the patterns kept and the voicing
  board from discharged capacitors behind Q13's 1.5 s mute, as the unit does; the rails' own rise
  and fall aren't modelled.
- CPU, on an Apple M5 Max at 48 kHz (`tr808bench`, the audio thread with the voices on workers /
  everything on one thread): a kit 10 / 12 % of a core, every voice 15 / 29 %, knob automation on
  seven knobs 16 / 37 %, idle under 1 %. The snare, clap, maracas, cymbal, hats, cowbell and mixer
  run at twice the board's rate, which is most of that.
- Not modelled: hum, temperature drift, the rails' sag (about 0.15 dB on a cymbal hit).
- Not emulated: TRIGGER OUT, the footswitch jacks, SYNC OUT. The factory rhythm tracks 1 and 2
  aren't loaded: no document we have lists their bars.
