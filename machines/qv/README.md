# Alesis QuadraVerb (al-fx-quad)

An emulation of the Alesis QuadraVerb multi-effects (1989) and the QuadraVerb Plus (1990), main board
rev F. The Plus is the same unit with a newer EPROM, so one plugin is both, and the firmware you give
it decides which: v2.03 (the Plus) if it finds it, otherwise v1.07. Not affiliated with Alesis.

The real firmware runs on an 80C31 (MAME's mcs51 core) and loads its effects programs into our model
of Alesis's DASP 24 effects chip, decoded from the firmware (`docs/dasp24.md`). The first time it
loads, the unit gets its 90 factory programs the way the unit does (STORE, PAGE UP three times,
STORE), and after that the session keeps its battery-backed RAM. The input stages, the converters,
the dry path and the outputs run from the service manual's sheet 1 (`docs/circuit.md`). The core is
the QuadraVerb GT's (`machines/qvgt`), because Alesis built the GT's board from this one.

## Status

We think it's really close. It passes the service manual's self-tests on both firmwares, and against
the one real unit we have with its dry source it sits within about 2 dB band by band. The gaps we
know about are under "Known differences" below, and `docs/improvements.md` has the details.

- **The service manual's self-tests** run in `tools/service/check.py` and all pass on both
  firmwares (MIDI looped back, and BYPASS and CONFIG held at power-on).
- **Every analog stage** matches ngspice running the same circuit within 0.035 dB and 0.5 degrees up
  to 10 kHz (9 cases, `tools/spice/ac.py`).
- **Against real units** we only have free recordings, since there's no unit here. One is a
  QuadraVerb recorded from its outputs, with the drum machine going into it played dry too. On six
  of its programs ours sits within -1.5 and +2.2 dB of it band by band from 250 Hz to 8 kHz, and our
  dry path within 0.6 dB. On recordings of a Plus, the Leslie's rotors run within 0.3 % and 1.3 % of
  ours and the ping-pong delay within 1 ms. There are 7 A/B scenes for listening.
- **The Plus's own programs** (the ring modulator, the resonators, sampling and the audio trigger)
  compute what they should (`qvdspcheck`).
- **CPU.** We measure at 48 kHz on an M5 Max (`qvbench`). The heaviest workload takes 2.7 % of a
  core.

### Known differences

- **The tremolo** on program 87 runs at 4.19 Hz, where one recorded Plus runs at 4.48 Hz. We don't
  know that unit's firmware version, and Alesis changed the factory programs in 2.01, so this stays
  open.
- **The top octave** has a stand-in for the 20 kHz filter module, because Alesis never published
  what's in it. Our wet sound is 1.1 dB down at 10 kHz and 3.3 dB down at 18 kHz.
- **The plates' highest octaves** ring on longer than in the one recording we have of them, though
  that recording went through two lossy encodes, and those strip quiet highs.
- **The dry level** comes from one unit. We set the dry VCA 1.3 dB under its data sheet's typical
  gain so that unit's balance of wet and dry matches.
- **The ring modulator** shifts by about 0.7 of the SPECTRUM SHIFT on the display, which is what
  the firmware's timing gives. We haven't heard a real one at a known shift to check it.
- **The DSP's arithmetic** (word width, rounding, clipping) is our best reading of the decode, and
  no recording we have pins it down.
- **The hiss** comes from the parts' data sheets, not from a real unit.

### Not emulated

- How hard you press a key, beyond light and hard (Shift makes a press a hard one).
- The D/A's images above the host's band.
- Firmware v1.03 and earlier, which ran the boards before rev F with a VCA on the wet signal.
- Part tolerances, so every QuadraVerb here is the same unit.

## ROMs

Both are needed, and neither is included:

| File | Size | SHA1 | What |
|---|---|---|---|
| `qvv203.bin` | 64 KB | `5f97c5cb6b209643bed6ec4046662c87c317a9ee` | the 27C512 EPROM (U19) as read from the chip: the QuadraVerb Plus's firmware v2.03, Alesis's copyrighted work. The effects, the programs and the panel are the firmware. |
| or `qvv107.bin` | 64 KB | `224b6a308552921e0fc6a916f900f57f6d5fa623` | the QuadraVerb's firmware v1.07, taken in v2.03's place when v2.03 isn't there (host `RomSpec::alternatives`). The session's RAM is marked with its firmware; opened with the other, the unit starts new and the session keeps the RAM aside. |
| `hd44780_a00.bin` | 4 KB | `65cf075a988cdcbb316b9afdd0529b374a1a65ec` | the LCD controller's character ROM, as MAME has it: the LCD is drawn from it. The datasheet copy (CRC 01d108e2) is taken too, corrected (host/Roms.cpp). |

Earlier firmware (v1.03 and before) is for the boards before rev F, which had a VCA on the wet
signal (service manual, software history 1.05); it isn't taken.

## Artwork

The panel draws no brand marks of its own. Two places take the user's files (SVG or PNG) in
`artwork/qv/` in the ROM folder, or named `al-fx-quad-<place>` anywhere in it: `maker` (the
Alesis logo) and `name` (the QuadraVerb logo), sized as `docs/panel.json` measures them.

## What works

- Two firmwares (v2.03 ahead of v1.07); the factory programs on first load; the RAM in the
  session; POWER (off: silent, the RAM kept; on: a boot from it); MIDI in and out.
- The analog stages at 4 times the DSP's rate: the INPUT stages (0 to +20 dB, clipping at the
  rails), the mono sum, the pre-emphasis and the sample-and-holds that undo it, the CEM3381 dry
  path, the OUTPUT stages; levels in volts at the jacks (0 dBFS = +4 dBu).
- The signal display (the DSP's overflow lights all four) and the firmware's level detector.
- The parts' own noise and the A/D's offset servo, always on.
- Sleep: once the input has been silent for 1.6 s and the output has settled, the DSP and the
  analog stages stop and the hiss goes on from a noise filter fitted to it; input, keys or MIDI
  wake it.
- Mono in feeds both sides (the rear's R (MONO) feeds the left with no plug in L); mono out is the
  left output.

## Keys

The panel keys by mouse (a right-click holds one) or computer: Left/Right PAGE down/up, Down/Up
VALUE down/up, Return STORE, P PROG, B BYPASS; Shift makes a press a hard one (the keys sense force:
harder scrolls faster). `?` lists them. REAR shows the rear panel; its BYPASS and ADVANCE jacks take
a footswitch press.

## Code from other projects

- `core/i8031.*`: MAME's mcs51 core (BSD-3-Clause, Steve Ellenoff, Manuel Abadia, Couriersud). The
  instruction bodies (`core/i8031_ops.inc`, `core/i8031_ops_decl_body.inc`) are generated from MAME's
  `src/devices/cpu/mcs51/mcs51ops.cpp` and `i8051.h` by `tools/port_i8031.py`; the timers, UART,
  interrupt polling and idle modes follow `i8051.cpp` and `i80c51.cpp`, by way of
  `machines/virusc/core/c515.*`.

## Tools

| Tool | What it does |
|---|---|
| `qvrun ROM [-s s] [-k t:key[:hold]] [-m t:hex] [-p n] [-l] [-i in.wav -o out.wav]` | The board headless: the LCD and latches as they change, keys, MIDI, a program change, MIDI looped back, a render at the DSP's rate |
| `qvplugintest` | The processor: boot with the factory programs, sound, sleep, session, layouts, MIDI, determinism, POWER |
| `qvrender ROM IN.wav OUT.wav [--input n] [--output n] [--program n]` | A WAV through the whole unit at its rate: levels, the signal display, overflows, speed |
| `qvbench ROM time\|golden DIR\|check DIR\|list\|run NAME` | The workloads: cost, golden renders, the difference against them |
| `tools/bench/mins.sh` | Millions of instructions per audio second, per workload |
| `qvanalog BLOCK [P] < freqs`, `tools/spice/ac.py` | Each linear analog stage's response as the model runs it, and against ngspice |
| `qvresponse ROM [--input n] [--output n]` | The whole unit's frequency response, wet and dry, on flat programs |
| `qvcompare ROM DRY.wav PROGRAM OUT`, `tools/compare/compare.py`, `tools/compare/stevie177.sh` | A recording's dry source through the model (whole, dry path, wet path) held against the unit's recorded output, per octave band |
| `qvdspcheck ROM` | The Plus's own DSP configurations (ring modulator, resonators, sampling, the audio trigger) against what they must compute (v2.03) |
| `qvcorners [--rate hz] [--only text] [--wav dir]` | The extremes (every factory program and a few under every signal, both rates): fails on an unconverged solve, output out of bounds, or noise from a pure tone |
| `tools/service/check.py` | The service manual's self-diagnostics (MIDI looped back, BYPASS and CONFIG at power-on), both firmwares |
| `tools/port_i8031.py <mame mcs51 folder>` | Regenerates the 80C31's instruction bodies from MAME |

Build: `cmake -S . -B build/qv -DLEMUS_MACHINES=qv && cmake --build build/qv --target qvrun qvrender qvbench qvplugintest`.
