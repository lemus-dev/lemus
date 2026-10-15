# Roland TB-303

Emulation of the Roland TB-303 Bass Line: machine `tb303` in Lemus, the instruments host. The sequencer is the real thing: the original NEC uPD650C-133 firmware runs
on an emulated uCOM-43 CPU with the 303's pattern RAM, key matrix, LEDs, DIN-sync inputs and
CV/gate/accent/slide latches. The voice is modelled from the service-notes schematic part by
part: pitch DAC and slide, expo VCO with its saw core and the Q8 square shaper, the
four-capacitor transistor ladder with the Q21 output amp in the resonance loop, MEG, VEG,
accent sweep, gate conditioning, the BA662 VCA and the output stage.

## ROM

The sequencer needs the CPU's mask ROM, `d650c-133.ic8` (2 KB, CRC 268a8d8b, SHA1
7a4236b2bc9a5cd4c80c63ca1a193e03becfcb4c), the image in MAME's `tb303` set. Put it, or MAME's
`tb303.zip`, anywhere in your Lemus ROM folder. Lemus ships no ROM data.

Without the ROM the TB-303 loads in **voice mode**: the voice, played from MIDI, with no CPU, so
no sequencer, and the panel's keys do nothing. A session keeps its patterns in voice mode, and
gets the full 303 back when it is reopened with the ROM in the folder.

The panel's brand marks come from an optional artwork pack, `tb303.svg` (or an SVG in
`tb303.zip`), in the same folder. Any SVG works; the marks are the elements with these ids, in
panel millimetres (origin at the panel's top-left corner, as in `docs/panel.json`): `roland`,
`bass_line`, `tb303`, `computer_controlled`. Lemus ships no pack. When the folder has no
`tb303.svg`, Lemus writes a blank one (`blank_artwork.h`): the four slots with their boxes,
commented out, so it draws nothing until they are filled in.

## Building and tools

The machine builds into Lemus from the repo root (see the top-level README). Its own tools:

    cmake --build build/cmake -j --target tb303plugintest tb303render tb303seq tb303bench

- `tb303plugintest` runs the machine's processor headless: host-synced sequencer, MIDI, internal
  clock, random-settings stress, state round trip. It reads the ROM from
  `../private/roms/tb303/`.
- `tb303render` renders test specs through the voice model to WAV, for `tools/compare/`.
- `tb303seq` scripts the panel against the firmware and logs CV/gate/accent/slide and LEDs.
- `tb303bench` times the machine (processor, voice and CPU board) on fixed workloads at 44.1 and
  48 kHz, best of N runs: the internal sequencer, an acid line from MIDI, knob extremes, knob
  automation, waveform switching, a 20 s run at full resonance, and idle. `golden DIR` writes the
  renders, `check DIR` compares a later build's with them (max difference in dBFS): speed work
  must leave them unchanged to float precision. `time [RUNS [NAME]]` only times.
- `tools/compare/dinsync.py` compares the voice (nominal parts and the unit Lemus plays) and
  Roland's software with 400 single notes from a real TB-303 at documented knob positions (Din
  Sync's reference set, in the private folder): `specs`, `real`, `render [--roland]`, then
  `compare`. `dinsync_fit.py` fits the unit's open part values to them;
  `acidvoice_check.py` checks the cutoff against a second real unit.
- `tools/service/check.py` runs the service notes' adjustments and specifications against the
  emulation (CV through the firmware, VCO and VCF through the voice) and says pass or fail for each:
  `../../.venv/bin/python tools/service/check.py`.

## Using it

The editor is the top panel drawn from measurements of a straight-on photo (every legend,
keyline and control placed to about 0.3 mm), brand marks from the artwork pack (`tb303.svg`) when your ROM folder has one, knurled knobs,
bar-knob rotary switches, key caps and LEDs driven by the firmware. Drag knobs (shift for
fine, double-click resets), click or drag the MODE and TRACK/PATT.GROUP switches, press the
keys. The WAVEFORM switch, which is on the rear of the real unit, sits at the rear edge above
its legend. The bar has SYNC, CUT OFF MAX, SERVICE and `?` (key list).

Programming works as on the hardware (see the owner's manual, `private/docs/tb303-owners-manual.pdf`): PATTERN WRITE, pick
section A/B (ACCENT/SLIDE) and a pattern key, hold it and press PATTERN CLEAR, PITCH MODE to
enter notes, TIME MODE for note/tie/rest, TAP+ACCENT/SLIDE in pitch mode for accents and slides.
For the two-key steps with the mouse, a right-click (ctrl-click on a Mac) holds a panel key
down until it is clicked again; Escape lets go of all.
Like the original, there are no factory patterns: a new instance has every pattern as PATTERN
CLEAR leaves it (16 steps, all rest). Pattern memory is saved with the session.

- **Sync** (off by default): with SYNC on and the host playing, the host transport is the DIN
  sync input (run level and a 24 ppqn clock), so the pattern starts with the transport. Leave it
  off when you play the voice from MIDI, or the 303's own pattern runs alongside your notes.
  With SYNC off, RUN/STOP and TEMPO (40-300 BPM) run the internal clock.
- **The unit:** the trims and the part values the schematic leaves open are fitted to a real
  TB-303 (Din Sync's 400 reference notes): TM3 at a 2.5 ms ring (the slow end of the service
  tolerance), Q21 β 385, the MEG peaking at 9.3 V, an 860k DECAY pot, and the A-taper pots as
  two straight segments (21.5 % of the track at mid rotation for CUT OFF and ENV MOD, 14 % for
  DECAY). It is checked for long-run loop stability at both ends of TM3's tolerance (no slow
  "bubbling" build-up at high resonance).
- **SERVICE** takes the panel off: the main board and the switch board, every part where the
  service notes' board drawing puts it (p.6, digitised into `docs/boards.json`), with its
  designator printed where the board prints it, the knobs left on their pots as they are when the
  panel comes off. On the right is a control for each of the six trimmers, named as the board
  prints them (TM1, TM2 INT, TM3 FREQ, TM4 TUNE, TM5 WIDTH, TM6) with what each one sets. Click
  one (or the trimmer itself, or up/down) and it lights up on the board, and the scope, the
  frequency counter on the VCO and the meter on CV OUT are hooked onto its check point with the
  notes' procedure underneath. Drag along a control to turn its trimmer (shift: a tenth as far;
  left/right keys; the wheel), or the trimmer on the board; green on the track is where it is in
  spec, the tick is the factory's trim, a double-click puts it back, FACTORY TRIM puts all six
  back. A dot on SERVICE shows a unit out of its factory trim. The pots,
  switches and keys work on the boards too, and the trims are saved with the session. How each
  trimmer acts is in `docs/model.md` ("Service trimmers").
- **CUT OFF MAX** (FULL CUT OFF): how high full CUT OFF reaches (the ring at full RESONANCE, no
  env mod), 1.2 to 3 kHz, as a unit whose CUT OFF pot has a different law; cutoff centre stays
  where TM3 puts it. Drag it (shift for fine steps, the mouse wheel works too); double-click goes
  back to 2.3 kHz, where it ships: a typical unit's. The fitted unit's own law gives 1.35 kHz (Din
  Sync's unit, at the dark end), Roland's TB-303 software reaches ~1.8 kHz, the service notes'
  nominal parts ~2.2 kHz, a typical unit ~2.5 kHz (Whittle). Raising it brightens the whole upper
  half of CUT OFF, not only the top.
- **Power:** VOLUME is ganged with the POWER switch, as on the unit: turned to OFF, the 303 is
  off (silent, every LED dark, no keys or MIDI notes); turned back up, the CPU starts afresh,
  stopped, with its pattern memory kept (battery backed). Automating VOLUME to 0 does the same.
- **MIDI** plays the voice directly, as Roland's own TB-303 software does with KEYBOARD on:
  velocity 100 or more is an accent, a note started while another is held slides to it.
  CC 74 cutoff, 71 resonance, 12 env mod, 75 (or 13) decay, 16 accent, 104 tuning, 11 volume.
- Computer keyboard: A W S E D F T G Y H U J K are the note keys, Z X C V are
  DOWN UP ACCENT SLIDE, Q PITCH MODE, R TIME MODE, I FUNCTION, Backspace PATTERN CLEAR,
  `,` BACK, Return TAP, 1-7 the track/group switch, F1-F4 the mode switch. Space is RUN/STOP
  in the standalone app only; in a DAW it goes on to the DAW's transport.

In Onyx Tracker a plugin is only mixed once a track has played a note on it, so a song that
only runs the 303's own sequencer needs one note (e.g. a short note at the end) on its track.

**Idle.** Once a note has died away (gate closed, output under −120 dB for 100 ms, envelopes
settled) the voice sleeps: it puts out exact zeros and skips the circuit model, about 0.1 % of a
core instead of about 15 %. Any change on its lines (a note, from MIDI or the sequencer) or a
knob wakes it. The CPU keeps running, so the sequencer and LEDs carry on. Its tail is 0.5 s
(under −120 dB within 0.34 s). A host should leave it awake rather than suspend it: suspended,
the 303 can't run its own sequencer.

## Layout

| Path | What |
|---|---|
| `tb303.cpp` | The driver: ID, name, ROM, and how Lemus creates the machine |
| `core/ucom43.*` | uCOM-43 CPU, ported from MAME's ucom4 core (BSD-3-Clause, hap) |
| `core/machine.*` | The CPU board: pattern RAM, key matrix, LEDs, status inputs, latches, pattern writer |
| `core/voice.*` | The voice board model (see `docs/model.md`) |
| `core/midi.h`, `patterns.h` | MIDI-to-CV/gate logic; pattern text format and the tools' test pattern |
| `plugin/` | JUCE processor, editor, `HardwareView` (the panel) and `HardwareService.cpp` (SERVICE), generated `PanelData.h` and `BoardData.h` |
| `tools/tb303render`, `tb303seq`, `plugintest` | See above |
| `tools/compare/` | Test suite, Roland plugin renderer (through `tools/host`'s lemus-render, or Onyx Tracker's `otk`), comparison and analysis scripts |
| `tools/panel/` | `gen_panel.py` (panel.json to PanelData.h), `gen_boards.py` (boards.json to BoardData.h) |
| `tools/service/check.py` | The service notes' checks, pass or fail |
| `tools/dis650.py` | Disassembler |
| `docs/` | `research.md`, `circuit.md`, `firmware.md`, `sequencer.md`, `model.md`, `panel.md`/`panel.json`, `boards.json` |
| `private/` | Not in the repo; see below |

## Private material

The docs and tools refer to material this repo can't contain: the service notes and owner's
manual, schematic renders, papers and forum threads, photos, Roland's TB-303 plugin renders,
real-303 recordings and MAME sources. It lives in the Lemus private
folder (`~/src/lemus/private/tb303`) at the paths the docs give, and `tools/link-private.sh`
links it in here as `private/`. The ROM and artwork pack are in `~/src/lemus/private/roms/tb303/`,
and the ROM download script is in `private/tools/`.

## Status

Checked four ways; the numbers are in `docs/model.md`, "Validation".

- **Service notes:** every adjustment and specification passes (`tools/service/check.py`, 16 of
  16): CV through the firmware, VCO tuning and octave ratios, TUNING range, the VCF ring, the
  clocks.
- **The schematic, in SPICE:** every block of the voice matches a simulation of the transcribed
  schematic (`tools/spice`), with typical parts and across their spread: the DAC and slide
  exactly; the saw and square within 0.03 dB; the filter's passband within 0.5 dB and its
  resonant peaks within 0.7 dB; the envelopes, accent and cutoff converter within a few percent;
  the VCA (with a transistor-level BA662) and output within 0.4 dB; and the whole voice, all the
  blocks joined into one circuit, within 0.1-0.3 dB in level and 0.2-1.4 dB in its harmonics.
  The SPICE work corrected the
  saw's levels, the resonance loop's gain, the gate release, the cutoff converter, the output
  stage, and the VCA and its envelope (Q31 and Q32 as real transistors: accent +4 dB at half
  ACCENT, a slower start to the VEG's decay).
- **A real TB-303** (Din Sync's 400 single notes, `tools/compare/dinsync.py`, with CUT OFF MAX at
  the fitted unit's 1.35 kHz, not the 2.3 kHz the plugin ships at): log-spectral
  distance 3.0 dB (the unit against its own retakes: 2.4 dB; Roland's software: 3.3 dB),
  resonant peak within 140 cents on average (Roland's: 360), level within 0.8 dB (Roland's:
  1.7). Note by note it is closer than Roland's software on 63 % of notes for the spectrum and
  76-88 % for brightness, resonant peak and level.
- **Roland's TB-303 software:** mean log-spectral distance 2.8 dB over 56 tests (2.2 dB with
  the earlier calibration fitted to it, which the real unit replaced). Where they still differ
  (accent at half ACCENT, VOLUME at full, TUNING's range, the level tilt across the octaves),
  the model follows the schematic and Roland's software doesn't.

Known differences and approximations:
- At cutoff 0 and full resonance the peak is ~3.6 dB weaker than Roland's (+7.4 vs +11.0 dB).
- The BA662 VCA has no Rohm data sheet: it is modelled from Open Music Labs' reverse
  engineering of an original chip and its clones' data sheets (`docs/circuit.md` 6.1).
- Units differ: the fitted unit tops out at ~1.4 kHz of resonant peak at full CUT OFF, as Din
  Sync's does; acidvoice.com's unit reaches ~3.3 kHz, Whittle gives ~2.5 kHz as typical.
  Brightness in the first 100 ms of a note is still ~25 % off the real unit's.
- Not emulated: MIX IN, CV/GATE out, headphone amp, battery LED behaviour.
