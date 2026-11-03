# Roland TR-909

Emulation of the Roland TR-909 Rhythm Composer (1984): machine `tr909` in Lemus, the instruments
host, and its own plugin, **ro-drums-909**. The sequencer is the real thing: the original v4.0
firmware runs on an emulated uPD7811 CPU with the 909's battery RAM, key matrix, LED matrix,
MEAS/TEMPO display, TEMPO and TOTAL ACCENT pots, MIDI port and DIN sync inputs, and drives the
voicing board through its trigger latches and 6-bit accent DACs. The voices are the voicing board
itself: every analog voice, the noise generator's loads, the hi-hat's and cymbals' D/As, VCAs and
filters, and the multi-jack board's mix to the master outputs, each run sample by sample from a
netlist transcribed part by part from the service notes (`core/netlists/`), by a circuit solver
(`core/circuit.*`, the 808's, with a MOSFET element added for the 4069 and 4011 gates). The noise
generator and the sample voices' counters, ROMs and latches are their logic, edge by edge
(`core/noise.*`, `core/hihat.*`, `core/cymbals.*`).

## ROMs

The images in MAME's `tr909` set. Put them, or MAME's `tr909.zip`, anywhere in your Lemus ROM
folder; Lemus finds each by its SHA1 and ships no ROM data.

| Image | What | Without it |
|---|---|---|
| `tr909_v4.ic609` (8 KB, CRC a9b45f30) | the program ROM, version 4.0 | **voice mode**: the voicing board, played from MIDI, with no CPU, so no sequencer and no display, and the panel's keys do nothing |
| `hn61256p__c43.ic69` (32 KB, CRC 2aaae11b) | the hi-hat's samples | the hi-hats are silent |
| `hn61256p__c42.ic62` (32 KB, CRC dc61cf1a) | the crash's | the crash is silent |
| `hn61256p__c44.ic54` (32 KB, CRC 01a9b435) | the ride's | the ride is silent |
| `tr909_v1.ic609` (8 KB, CRC bfb9682b) | the program ROM, version 1.0 | no factory patterns |

Version 4.0 dropped the factory patterns a new 909 held (Bank I: patterns 1-8 and track 1, with a
tempo memory). With the version 1.0 ROM in the folder, Lemus makes them as the service notes say,
by powering the 1.0 firmware on with TRACK 1 and PATTERN 1 held, then plays the memory that leaves
on 4.0, whose format is the same (`core/factory.h`). A new instance holds them, and so does the
host's State: Reset.

A session keeps its patterns in voice mode, and gets the full 909 back when it is reopened with the
ROM in the folder.

The panel's brand marks come from your own artwork; Lemus ships none. As MAME keeps artwork, each
of the four places has a file of its own in `artwork/tr909/` in the ROM folder (or in
`artwork/tr909.zip`), an SVG or PNG with a plain name: `maker` (the Roland logo), `model` (TR-909),
`name` (Rhythm Composer), and `rear_maker` (the Roland logo on the rear panel, 110 x 22 mm), e.g.
`maker.png`. The front places are the panel's logo boxes in `docs/panel.json`. Each file is drawn
as it looks, fitted to its box with its proportions kept and centred; only what it draws counts
(not a page's margins or a PNG's transparent border), and transparent parts show the panel. A PNG
at 10 px/mm stays sharp in the largest window. ROMs on the bar lists the places with their sizes
and what each has, opens the folder, and reads it again (Rescan). The older single-SVG pack,
`tr909.svg` (or an SVG in `tr909.zip`) with the ids `roland`, `tr909`, `rhythm_composer` in panel
millimetres, still fills the places without a file, as outlines in the print colour (the rear
takes the pack's `roland`, in its grey). Earlier versions wrote a blank one (`blank_artwork.h`)
into the ROM folder; an untouched copy is taken out when the 909 loads.

## Building and tools

The machine builds into Lemus from the repo root (see the top-level README). Its own targets:

    cmake --build build/cmake -j --target plugin_tr909_All tr909plugintest tr909seq tr909voicerun \
        tr909circuitrun tr909hhrun tr909cyrun tr909factory dis7810

- `tr909plugintest [OUT_DIR]` runs the processor headless: every voice from MIDI through the
  firmware and in voice mode, accent, the missing sample ROMs, sleep (exact zeros, and a hit after
  it sounding as before), SYNC, the factory patterns and the sequencer's run state, state.
  `--panel OUT.png [SCALE] [running] [--artwork PACK.svg]` renders the editor; `--render HITS.json
  OUT.wav` plays a list of notes through the processor (the main pair and the ten MULTI OUTs, a
  cable in each). It reads the ROMs from `../private/roms/tr909/`.
- `tr909seq [--rom FILE] [COMMAND ...]` scripts the panel and MIDI against the firmware (keys held
  for a time, tempo, TOTAL ACCENT, MIDI bytes, DIN sync) and logs every trigger and DAC write, the
  LEDs, the display, MIDI out, coverage and jump traces: how `docs/firmware.md` and
  `docs/sequencer.md` were worked out. `tools/tr909seq/tests.py` checks 54 procedures against it.
  `dis7810` is MAME's uPD7810 disassembler.
- `tr909voicerun OUT.wav --hit TIME:VOICE[:CODE] ...` renders the voicing board alone (VOICE bd sd
  lt mt ht rs hc ch oh cr rd, CODE the accent DAC's code): knobs (`--set`), part values
  (`--cal voice.param=value`), every MULTI OUT jack as a channel (`--jacks`), board signals
  (`--probe`), each circuit's solver statistics (`TR909_BLOCKS=1`).
- `tr909circuitrun NETLIST,...` runs netlists through the solver with PWL inputs and writes the same
  circuit as an ngspice deck; `tools/spice/compare_circuit.py` runs both and compares them, and
  `tools/spice/suite.py` does that for every case in `tools/spice/cases/` and tabulates the
  differences.
- `tr909replay FILE.rec [OUT.bin]` steps one partition alone on the inputs a run gave it: a run with
  `TR909_RECORD=DIR` (and `TR909_RECORDNAMES=sd:snappy,...` for only some) writes them. It prints
  the CPU cycles and instructions a step and the solver's statistics, and writes the outputs to
  compare two builds bit for bit: how the solver's speed work is measured. `TR909_HIST=1` on
  `tr909voicerun` prints each circuit's steps by kind, with their time, and why linear mode ended.
- `tr909hhrun`, `tr909cyrun` render the hi-hat's and cymbals' digital halves and netlists alone,
  and write ngspice decks with the D/As' steps at their exact times.
- `tr909factory --v1 ROM --out RAM [--v4 ROM]` writes the factory memory and checks it plays on 4.0.
- `tools/compare/real909.py` is the measuring library and the catalogue of the real-909
  recordings (`docs/reference.md`); `tools/compare/<voice>.py` compare each voice with them,
  `reference.py` regenerates the target tables, `sequencer_timing.py` the firmware's timing against
  the recordings'.
- `tools/panel/` measures the panel from its photos (`docs/panel.md`).
- `host/scenes/` are loops for listening to Lemus against Roland's TR-909 plugin in Lemus AB
  (`tools/host/README.md`), written by `tools/ab/scenes.py`. `tools/ab/ar_scenes.py` writes the
  same against the real 909 of AudioRealism's pack, at its known settings, into the private folder
  (`private/reference/scenes/ar/`; the pack can't be published): a beat built from its single hits,
  and its own sequenced takes (rim shot shuffle, open hat cut by closed, flams, snare and clap).
  `tools/ab/factory_song.py` writes a real 909 playing its factory song (a video) against Lemus
  playing the same memory, at the tempo, board, knobs and levels fitted to the video
  (`private/reference/scenes/video/`), and a measure of each of its patterns cut from both, looped,
  with Lemus also live (its hits as MIDI, the fitted knobs and board) to edit while it plays
  (`private/reference/scenes/factory-patterns/`).

Python tools run from the repo's `.venv` (`../../.venv/bin/python tools/...`).

## Using it

The editor is the top panel drawn from measurements (`docs/panel.json`, `docs/panel.md`): every
knob, key, LED, legend and keyline where the real one is, the MEAS/TEMPO display and the lit
buttons as the firmware drives them, brand marks from your own artwork. Drag a knob up or down
(shift for fine, double-click resets it, or use the wheel); hovering shows its value. Click a key
to press it, as long as the button is held. A right-click (ctrl-click) holds a key down until it's
clicked again, for what takes two keys at once (SHIFT or CLEAR with a step key, INSTRUMENT SELECT
with one); Escape lets go of them all. The bar has SYNC (host sync: the 909 in MIDI sync, run from
the DAW's transport), REAR, SERVICE (both below) and `?` (the key list). There is no memory clear,
as there is none on the unit; the host's State: Reset makes the 909 a new instance, its factory
patterns included.

Programming works as on the hardware (the owner's manual; `docs/sequencer.md` has the procedures
and what was checked): SHIFT + PATTERN PLAY 1 for pattern write, CLEAR + a step key to erase,
START, INSTRUMENT SELECT + a step key, then the step keys (twice for an accent), SHUFFLE/FLAM,
SCALE, LAST STEP, TRACK WRITE, BANK, COPY/INS/DEL. The memory is saved with the session.

- **MIDI** goes into the firmware's own MIDI IN, as on the unit: notes 36 BD, 38 SD, 43 LT, 47 MT,
  50 HT, 37 RS, 39 HC, 42 closed hat, 46 open hat, 49 crash, 51 ride (and 35, 40, 41, 44, 45, 48),
  velocity setting the accent DAC's code through the firmware's own table (velocity 50 plays its
  sequencer's normal level, 80 its accent), with the firmware's own scan timing. Control changes move
  the knobs on Roland's TR-8 numbers for the same controls (its MIDI implementation chart): BD 20
  TUNE, 21 ATTACK, 23 DECAY, 24 LEVEL; SD 25 TUNE, 26 SNAPPY, 28 TONE (the TR-8's SD DECAY), 29
  LEVEL; 46/49/52 TUNE, 47/50/53 DECAY, 48/51/54 LEVEL for LT, MT, HT; 57 RS, 60 HC LEVEL; 62 CH
  DECAY, 81 OH DECAY, 63 and 82 HH LEVEL; crash 83 TUNE, 85 LEVEL; ride 86 TUNE, 88 LEVEL; 71
  TOTAL ACCENT; 7 MASTER VOLUME. In voice mode the notes play the voicing board directly with the
  firmware's velocity laws, as measured.
- **MIDI out** is the firmware's MIDI OUT, as on the unit (`docs/firmware.md`, "Out"): the EXT INST
  rows' notes (36-51 on its transmit channel, 11 at power-on; LAST STEP + a step key sets it) and
  everything received but the clock (a merge), each message at the sample its last byte leaves the
  serial port. The unit never sends its own voices. Its clock, start, stop and song select stay in
  the plugin (the host has its own transport).
- **Sync** (off by default): SYNC puts the firmware in its MIDI sync mode (SHIFT + TEMPO MODE,
  pressed for it while the sequencer is stopped) and sends it the host transport as MIDI clock,
  start or song position and continue, and stop. With SYNC off, START, STOP/CONT and TEMPO (36-290
  BPM, the firmware's own law) run the internal clock.
- **Outputs**: the main stereo pair is the master L and R outputs, with the jack board's fixed
  panning (LT and ride left, HT and the hi-hats right, the crash a little right) and its phase
  (inverted against the MULTI OUTs), and the ten MULTI OUT jacks are outputs after it (BD, SD, LT,
  MT, HT, RS, HC, HH, Crash, Ride; `MachineInfo::outputs`), off until the host turns them on, mono
  or stereo (the jack on both sides). Which voices go to them is the 909's own: REAR in the bar
  shows the rear panel, where a click plugs a cable into a MULTI OUT jack (or pulls it out). As
  on the unit, a cable takes its voice out of the mix, to that output; with no cable a voice is in
  the main pair and its output is silent, whatever outputs the host turns on. The cables are saved
  with the session.
- **Power-on**: a new instance (and a session load, and State: Reset) is a 909 that has been on a while:
  the firmware past its boot, the board at its operating points, playable at once. The rear's POWER
  switch turns it off (silence, the panel dark, the memory kept) and on again, and switching it on is
  the real power-on: the firmware boots for 0.6 s, and the master outputs are muted for about 2 s
  while C143 charges, then open; the MULTI OUTs are not muted, and the hi-hat's, crash's and ride's
  thump as their stages charge (`docs/circuit/power.md`). A session always loads switched on.
- **SERVICE** in the bar takes the top panel off, as on the 303 (`docs/boards.md`): the voicing
  board where it hangs under the panel, the switch board folded out in front of it, every part where
  the service notes' board drawings put it (p.9, p.10), drawn as the part it is, the copper showing
  faintly through (generated from the pads, not Roland's tracks). The knobs stay on their pots and the keys on their switches,
  and work as on the panel; the LEDs and the display run. The wheel or a pinch zooms (the parts'
  designators show close up), a drag moves, a double-click shows it all. On the right:
  - **Scope**: on a MULTI OUT or master jack, a trigger line, an accent DAC, or any pin of any part
    the netlists draw (click its caption: by voice, kind, part and pin), triggered by a voice's TRIG
    line; the readouts show its peak to peak and HAND CLAP LEVEL. While a probe is on the board
    doesn't sleep.
  - **Trimmer TM**, the clap's (SN p.11: "LEVEL Center, MULTI OUT 2 Vp-p"), on the board and as a
    control, a parameter saved with the session; a double-click puts it back to the calibrated unit's
    trim (0.30). TEST
    sets the scope up on the clap's MULTI OUT at SN p.9's scale and plays the clap every 0.6 s. The
    model at that trim reads 4.1 Vp-p there on average (3.1-5.6 from hit to hit, with its noise), not 2: its spikes are narrower than the unit's, whose clap
    has 2.4 dB more energy at the same peak to peak, so the notes' 2 Vp-p (TM 0.255) would leave the
    clap 6 dB under the other voices (`docs/circuit/rshc.md`, "TM").
  - **Circuit**: which unit this is, by serial number, from the first (360100) to the late board
    (426700 up, the default): the production changes of SN p.7's change table (`core/revisions.h`:
    the BD's C9 and R495, the cymbals' clocks, the rim shot, the hi-hat's C134, the jacks'
    capacitors, the tom noise), saved with the session.
- **LEVEL knobs** start at 3/4, the others at 12 o'clock, TEMPO at 120.6 BPM (121 on the display).
- Computer keyboard: 1-8 and Q-I are the step keys 1-16; A S D F TRACK PLAY 1-4, G H J PATTERN
  PLAY 1-3, K EXT INST; Z X C V TEMPO (STEP), BACK (TAP), FWD (BANK I), AVAILABLE MEAS (BANK II);
  B N CYCLE/GUIDE (LAST MEAS), TAPE SYNC (TEMPO MODE); Return CARTRIDGE/ENTER (TOTAL ACCENT); Shift
  holds SHIFT; Backspace CLEAR; F1-F4 LAST STEP, SCALE, SHUFFLE/FLAM, INSTRUMENT SELECT; comma
  START, period STOP/CONT. Space is START (STOP/CONT while running) in the standalone app only; in
  a DAW it goes on to the DAW's transport.

**Idle.** Each voice stops once its jack has settled (within 10 µV-2 mV for 0.1 s, its timing
capacitors still), and the whole board once nothing runs: it then puts out exact zeros (the CPU
keeps running, so the sequencer, the LEDs and the display carry on). A trigger or a knob wakes it.
It reports a 2 s tail to the host. A host should leave it awake rather than suspend it: suspended,
the 909 can't run its own sequencer.

## Layout

| Path | What |
|---|---|
| `tr909.cpp` | The driver: ID, name, ROMs (all optional), artwork places, and how Lemus creates the machine |
| `core/upd7810/` | uPD7810/7811 core, ported from MAME (BSD-3-Clause, Juergen Buchmueller) by `tools/port_upd7810.py` |
| `core/machine.*` | The CPU board: memory map, key and LED matrices, display, A/D pots, MIDI, DIN sync, the voicing board's latches |
| `core/factory.*` | The factory memory, made with the user's v1.0 ROM |
| `core/circuit.*` | The circuit solver (nodal DK method, linear mode, partitions, ngspice export) |
| `core/voiceboard.*` | The voicing board: the latches and DACs, its blocks and partitions, the sample voices' rate, sleep, the scope's probes, its parts by designator |
| `core/revisions.h` | The production changes by serial number (SN p.7) |
| `core/noise.*`, `core/hihat.*`, `core/cymbals.*` | The noise generator, the hi-hat's and the cymbals' logic |
| `core/netlists/` | Every circuit as a netlist (`docs/circuit.md`) |
| `core/oversampler.*` | The half-band decimator |
| `plugin/` | JUCE processor, editor, `HardwareView` (the panel, drawn from `docs/panel.json`), `HardwareService.cpp` (SERVICE, drawn from `docs/boards.json`) |
| `tests/host.cpp` | The 909 through the Lemus host |
| `host/` | A/B sources and scenes for `lemus-render` |
| `tools/` | See above |
| `docs/` | `research.md`, `circuit.md` and `circuit/`, `firmware.md`, `sequencer.md`, `reference.md`, `model.md`, `panel.md`/`panel.json`, `boards.md`/`boards.json`, `improvements.md` (what's known to be wrong, and the plan) |
| `private/` | Not in the repo; see below |

## Private material

The docs and tools refer to material this repo can't contain: the service notes and owner's manual,
schematic page images and crops, data sheets, papers and forum copies, photos, recordings of real
909s (the AudioRealism per-knob set, Rob Roy's 1995 set, Regebro's accents, scherer.de's hats and
others), renders of Roland's TR-909 plugin, MAME's driver, the decoded sample ROMs and the brand
marks' tracing. It lives in the Lemus private folder (`~/src/lemus/private/tr909`) at the paths the
docs give, and `tools/link-private.sh` links it in here as `private/`. The ROMs and the artwork pack
are in `~/src/lemus/private/roms/tr909/`.

## Status

Checked six ways; the numbers are in `docs/model.md`, "Validation".

- **The firmware:** Roland's v4.0 ROM runs on the uPD7811 core; every owner's-manual procedure
  behaves as the manual says (`docs/sequencer.md`, 54 checks in `tools/tr909seq/tests.py`).
  Against a real unit's recordings: shuffle within 0.84 ms at all seven settings, the 40 flam
  recordings within 0.7 ms; through a real unit's 2'19" factory song, every hit within about 2 ms.
- **The schematic, in SPICE:** every netlist against ngspice on the same circuit at 96 kHz: BD, SD,
  the toms and the rim shot within 0.12 % RMS (the toms' noise 0.24 %), the clap 0.65 %, the jack board 0.35 %; the hi-hat
  and crash within 0.45 % at their 192 kHz (the ride 0.6 %); the VCOs' pitch within 0.01 %, the
  cymbals' clocks within 0.007 %.
- **A real TR-909** (AudioRealism's recordings of a late unit on a fine knob grid, the one it is
  calibrated to): the BD's end pitch exact, its decays within 0.04 octave; the toms -4/-6/-10 cents
  median over 121 settings; the snare's VCOs within 1.4 %; the rim shot's lines within 1 Hz; the
  hi-hat's clock and closed decays the unit's; the crash's and ride's clocks within 15 cents RMS
  over TUNE; log-spectral distances 0.3-4.2 dB (the BD the largest). Settings held out of each fit
  stay within these figures.
- **Other units:** Rob Roy's early board (the BD's end pitch within 0.2 Hz), scherer.de's hats (the
  closed hat's decay within 6 % RMS over eight settings), Regebro's rim shot and snare accents, and
  a factory-song video (the hi-hat's sample clock within 6 cents).
- **Roland's TR-909 software**, against the same unit: its toms 79-210 cents off (ro-drums-909
  within ±28), its cymbal clocks 29-118 cents under (within ±21), its BD's sweep 45 % long (within
  1 %).
- **The service notes** (`tools/service/check.py`): 62 of 75 checks pass, with the notes' parts and
  with the calibrated unit's: the specifications (tempo range, TRIGGER OUT, the master out's 6 Vp-p),
  the CPU board's trigger pulses and tape sync, the hi-hat's oscillator and counters, the snare's
  supply rails but their peak, the rim shot's three sketches, the crash and ride, the power rails.
  The rest: the snare's V_DD peaks at 4.1 V where the notes draw 5 (its drawn parts can't reach 5);
  the low tom's and the closed hat's accents span about 8 and 6.5 dB where p.9's traces span 5.8
  and 4.7; the hats' positive peaks are 1.2-1.5 times their negative ones against the traces' 1.9-2.0
  (the AudioRealism unit's agree with the model's); the snare's snappy noise bursts as its trigger
  ends, which the traces don't show (the AudioRealism unit's burst is the same size, clip by clip); and at the calibrated unit's TM the clap's level is 4.1 Vp-p against the
  adjustment's 2 (1.93 at the notes' TM).

Known differences and approximations (all of them, with the plan, in `docs/improvements.md`):
- The BD's second harmonic is perhaps 3-6 dB strong against three units, and its DECAY 0 a little
  short (t-20 93 ms against 106).
- The clap's fourth tooth is a spike where three units have a full tooth (a fourth has it 3-12 dB
  down: units differ), and with it the tail at 60-250 ms.
- The rim shot's output is taken from IC50a rather than the drawn IC50b: five units' waveforms say
  so.
- The snare's pitch bend is 3-10 % high in its first milliseconds; the toms' far tail is up to 0.14
  octave short at -40 dB.
- The calibration is one unit's. Others differ: Rob Roy's toms sit 14-92 cents higher, and a
  video's unit takes its crash a semitone below the lowest TUNE here. Earlier boards come from the
  serial number in SERVICE (the service notes' production changes), with the calibrated unit's
  other parts. Two fitted values lie outside their parts' tolerances: the BD's R44 (36k, standing
  for the click's real cause) and the cymbal clocks' 4011UB gates (1.72 times their typical drive).
- Rates: the noisy voices are 0.4-1.2 dB low at 10-16 kHz at their jacks against the board at
  384 kHz; the sample voices' 15-20 kHz is 0.5-0.7 dB low against ngspice.
- CPU: the voices run on two worker threads beside the host's. A busy kit takes about 2.4 G cycles a
  second at 48 kHz over all threads (about three quarters of one core of an Apple M1 Max), its
  slowest block about 55 % of the block's time; every voice at once comes near a full block.
- Not emulated: the tape interface (SAVE, LOAD, VERIFY, TAPE SYNC's input), the cartridge, TRIGGER
  OUT as a plugin output, the MIDI out's clock, start/stop and song select, the analog voices' own
  power-on transients (they start at their operating points) and the supply rails' ramp.
