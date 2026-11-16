# Alesis QuadraVerb GT

Emulation of the Alesis QuadraVerb GT guitar preamp and multi-effects (1991), firmware v1.03: plugin
`al-fx-quadgt`, an effect (Lemus FX). Not affiliated with Alesis.

The real firmware runs from reset on an 80C31 (MAME's mcs51 core), loading its effects programs into
a model of Alesis's DASP 24 effects DSP, decoded from the firmware (`docs/dasp24.md`). The first time
it loads, the unit gets its 90 factory programs the way the unit does it (STORE, PAGE UP three times,
STORE); after that its battery-backed RAM is saved with the session. The analog preamp (compressor,
overdrive, distortion, tone, cabinet, bass boost, noise gate, effects loop), the converters, the dry
path and the outputs are circuit models from the service manual's two sheets (`docs/circuit.md`).
**Status: 0.1.0 beta; the known gaps and the plan in `docs/improvements.md`.**

## ROMs

Both are needed, and neither is included:

| File | Size | SHA1 | What |
|---|---|---|---|
| `qvgtv103.bin` | 64 KB | `0ac063e74b6c6ecf9edda10824df9a79c621ccd9` | the 27C512 EPROM (U30) as read from the chip: firmware v1.03, Alesis's copyrighted work. The effects, the programs and the panel are the firmware. |
| `hd44780_a00.bin` | 4 KB | `65cf075a988cdcbb316b9afdd0529b374a1a65ec` | the LCD controller's character ROM, as MAME has it (the DX7 uses the same file): the LCD is drawn from it. |

## Artwork

The panel draws no brand marks of its own. Two places take the user's files (SVG or PNG) in
`artwork/qvgt/` in the ROM folder, or named `al-fx-quadgt-<place>` anywhere in it: `maker` (the
Alesis logo, 27.7 x 4.9 mm) and `name` (the QuadraVerb GT logo, 66.6 x 8.4 mm).

## Code from other projects

- `core/circuit.*`: the circuit solver from `machines/tr909` (this project's).
- `core/i8031.*`: MAME's mcs51 core (BSD-3-Clause, Steve Ellenoff, Manuel Abadia, Couriersud). The
  instruction bodies (`core/i8031_ops.inc`, `core/i8031_ops_decl_body.inc`) are generated from MAME's
  `src/devices/cpu/mcs51/mcs51ops.cpp` and `i8051.h` by `tools/port_i8031.py`; the timers, UART,
  interrupt polling and idle modes follow `i8051.cpp` and `i80c51.cpp`, by way of
  `machines/virusc/core/c515.*`.

## Status

Checked four ways; the numbers are in `docs/improvements.md` and `docs/circuit.md`.

- **The analog stages against ngspice:** each netlist on the same deck (`tools/spice/suite.py`, 15
  cases, worst 0.31 % RMS of the output's range), and every circuit on the plugin's own signals
  (`tools/spice/tapcheck.py`: the shaper 24-28 dB under the signal below 20 kHz, the other circuits
  40-77 dB).
- **The GT's own DSP configurations through the firmware** (`qvgtdspcheck`): the ring modulator
  shifts cleanly (the other sideband 31 dB down), the resonators are in tune within their integer
  delays, the sampler records and loops its 1.573 s, the audio trigger fires at the printed -18 dB.
- **A real GT:** an owner's video stepping through programs 00-09 of the v1.00 factory set, beside
  a direct-recorded guitar through the model with the v1.00 EPROM: the programs' names and the LCD
  match, and a listen side by side found nothing off. Its guitar, playing and recording chain are
  unknown, so it checks each program's character, not levels or tone.
- **CPU:** `qvgtbench`, busiest workload about 15 % of a core at 48 kHz on an M5 Max (budget 17 %),
  its worst 512-sample block 52 %.

What works:
- The firmware boots (banners, program 00), takes MIDI program changes, and runs its programs: EQ,
  pitch, delay, reverb, Leslie, ring modulator, resonators, sampling, as the DSP decode has them.
- The factory programs on first load; the RAM in the session; the POWER switch (off: silent, the RAM
  kept; on: a boot from it).
- The preamp at 4 times the DSP's rate on a circuit solver, each stage checked against ngspice
  (`tools/spice/suite.py`); the firmware sets it through its latches as on the unit.
- The A/D's pre-emphasis and the D/A's sample-and-holds that undo it, the CEM3381 dry path, the
  OUTPUT stages; levels in volts at the jacks (0 dBFS = +4 dBu).
- The 4-LED signal display (-6 dB, clip; the DSP's overflow lights all four) and the firmware's
  level detector from the same envelope.
- The parts' own noise and the A/D's offset servo (HISS); the self-diagnostics pass, the clip LED
  test too. Asleep with HISS on, the hiss goes on.
- Sleep: once the input has been silent for 1.6 s (longer than the longest delay) and the output
  has settled (HISS off: under -120 dB for 1.6 s, then exact zeros; HISS on: a steady level, then
  the hiss goes on from a noise filter fitted to it), the DSP and the analog stages stop; the 80C31
  runs on; input, keys or MIDI wake it.

### Known differences

- **No unit's audio with known settings** has been found: the circuit is the schematic's, the
  stages against SPICE. Unknowns: the 20 kHz filter module's parts, the sample-and-hold's window
  (taken as the one that undoes the pre-emphasis), the pots' tapers.
- The preamp runs at 4 times the DSP's rate (167 kHz): the distorted programs' top octaves (8-24
  kHz) come out 0.2-0.6 dB under the same at 16 times, chosen by ear for a third less CPU.
- The hiss (HISS on the bar, on by default) is from the parts' data sheets, not measured on a unit.
- **The DSP's arithmetic is a hypothesis**: 24-bit accumulator, 20-bit memory, truncation. Nothing
  has been checked against a unit.
- **The panel** is measured from photos and the quick-setup drawing (`docs/panel.json`, about
  +-1.5 mm); the LCD is drawn dot for dot from the HD44780's character ROM. The rear is measured too
  (+-5 mm); its footswitch jacks work.
- The OUTPUT pot's taper is a standard audio law (15 % at mid rotation).
- The ring modulator shifts by 0.71 of the displayed SPECTRUM SHIFT (100 Hz gives 71 Hz): the
  firmware's own timing on this CPU, so taken as the unit's; no recording settles it.
- The signal display's LEDs light at -24, -14, -6.6 dB and clip, where the panel prints -18, -12,
  -6 and CLIP: the rev B board's ladder as drawn. Earlier boards may differ.
- Programs 02, 04 and 08 overflow the DSP (the clip LED) on a hot guitar at INPUT 7; whether the
  unit's DSP saturates there the same way isn't known.

### Not emulated

- The keys' force as a continuous amount: a press is light, or hard with Shift.
- The effects loop's jacks: the send is computed, and the return is as with nothing plugged in, so
  the loop stays out whatever a program sets, as on the unit.

## Tools

| Tool | What it does |
|---|---|
| `qvgtrun ROM [-s s] [-k t:key[:hold]] [-p n] [-i in.wav -o out.wav]` | The board headless: the LCD and latches as they change, keys, a program change, a render at the DSP's rate |
| `qvgtplugintest` | The processor: boot with the factory programs, sound, sleep, session, determinism, POWER |
| `qvgtrender ROM IN.wav OUT.wav [--input n] [--output n] [--program n]` | A WAV through the whole unit at its rate: levels, the signal display, overflows, speed |
| `qvgtpreamprun [--u10 hex] [--u11 hex] [--gate hex] [--pot p]` | The preamp alone with the latches set by hand: levels, VCA gains, solver statistics |
| `qvgtcircuitrun NETLIST ...` | One netlist through the solver, and as an ngspice deck |
| `tools/spice/suite.py` | Every netlist against ngspice |
| `tools/spice/tapcheck.py [WORKLOAD]` | Every preamp circuit against ngspice on a workload's own signals |
| `qvgtbench ROM time\|golden DIR\|check DIR\|list\|run NAME` | The workloads: cost, golden renders, the difference against them |
| `tools/bench/mins.sh` | Millions of instructions per audio second, per workload |
| `qvgtdspcheck ROM` | The GT's own DSP configurations (ring modulator, resonators, sampling, the audio trigger) against what they must compute |
| `tools/service/check.py` | The service manual's self-diagnostics (MIDI looped back, BYPASS and CONFIG at power-on) |
| `qvgtrun ... -l` | MIDI out looped back to MIDI in |
| `tools/port_i8031.py <mame mcs51 folder>` | Regenerates the 80C31's instruction bodies from MAME |

Build: `cmake -S . -B build/qvgt -DLEMUS_MACHINES=qvgt && cmake --build build/qvgt --target qvgtrun qvgtrender qvgtbench qvgtplugintest plugin_qvgt_All`.

## Keys

The panel keys by mouse (a right-click holds one) or computer: Left/Right PAGE down/up, Down/Up
VALUE down/up, Return STORE, P PROG, B BYPASS; Shift makes a press a hard one (the keys sense force:
harder scrolls faster). `?` lists them. REAR shows the rear panel.
