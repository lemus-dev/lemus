# Research: Access Virus C

Target: the Virus C desktop module on OS 6.6 (368). Plugin `ac-digital-vc`, code `Avrc`. File
hashes and URLs are in `sources.md`.

Tier 2: the firmware, the 80C515 core and a DSP56362 engine are public, but the 80C515's board has
to be written here, and no schematic was found.

## Firmware

- One 512 KB flash image, `virus_c_660x368.bin`, holds the 80C515 program and the DSP program.
  MAME's notes: banks 0-2 are OS code and data, banks 3-6 DSP code and data, banks 8-14 factory
  settings (`acvirus.cpp` header).
- Strings in the image name "VIRUS C", "VIRUS KC" and "Indigo 2", so one image serves those models.
- **Disagreement:** MAME dates OS 6.6 (368) Dec 14, 2004. The image itself holds "368 14.12.2009".
- The 80C515 uploads the DSP program over the host port. The first word is a length, the second an
  address; a short bootstrap then takes chunks with commands 0-2 (store to P, X, Y), 3 (split 24-bit
  words into two 12-bit values in Y) and 4 (start) (`acvirus.cpp` header).

**Recommendation:** run the real firmware on both chips, from reset, as the unit does. The published
Osirus notes say it replaces the 8051 with C++; this machine does not.

## 80C515

- Part: SAF 80C515-L24N (`acvirus.cpp` header), documented in `c515_um.pdf`.
- **Disagreement, settled:** MAME gives 24 MHz, the Osirus notes 12 MHz. The firmware's UART setup
  (TMOD `21`, TH1 `FE`, SMOD 1) makes 31250 baud only from 12 MHz, so 12 MHz (`firmware.md`).
- Board, from `acvirus.cpp`: fixed 32 KB of flash at 0x0000, banked 32 KB at 0x8000 (P5 bits 4-7);
  external RAM with DSP host port at 0x0400-0x0407; key rows through P4 selected by the low bits of
  P5; LED matrix 16 x 8; 32 knobs through eight ADC inputs with a 4-way select on P3 bits 4-5.

**Recommendation:** port MAME's `i8051` core and `sab80c535` with its notice, then check the
80C515's differences against `c515_um.pdf`.

## DSP56362

- MAME's `dsp563xx` core (BSD-3-Clause, last changed 2026-06-04) is a partial, generated
  interpreter with the HI08 host port and the SHI port.
- 3,123 of its 9,849 instruction forms are unimplemented, including every `mac` and `mpy` form. Its
  run loop takes no interrupts, and address updates handle linear mode only. 26.3 % of the OS 6.6
  program words parsed so far decode to an unimplemented form.
- It has **no ESAI, DMA or timers**, and MAME's Virus driver is flagged as making no sound.
- The DSP is 1 MIPS per MHz (`DSP56362.pdf`). The Virus C part runs at 120 MHz (`acvirus.cpp`
  header).
- **Disagreement:** MAME's machine config sets 136 MHz, and the Osirus notes list 136 MHz as an
  overclocked emulator value.
- MAME's boot-mode setup is `set_hard_omr(0xe)`. Mode E is "HDI08 Bootstrap in 8051 multiplexed"
  bus mode, starting at $FF0000 (`DSP56362UM.pdf`, operating modes table), which matches the 80C515
  upload above. The bootstrap program is listed in the same manual, Appendix A.
- ESAI, the audio port, is chapter 8 of `DSP56362UM.pdf`; `AN1848.pdf` is NXP's note on using it.
- The owner's manual gives a maximum of 32 voices (`virus_c_manual.pdf`). It covers OS5, not the
  target OS 6.6, so later features need checking against the firmware itself.

**Decision, 2026-11-09:** use the dsp56300 engine from Gearmulator (GPL-3.0, with asmjit under
Zlib) rather than complete MAME's core. The machine's README names it, as `CONTRIBUTING.md`
requires. The firmware itself sets the PLL to 136 MHz from a 12 MHz crystal (`firmware.md`), so 136
MHz is what this OS asks for, whatever the part's rating.

## Audio path

- Sample rate: 12 MHz / 256 = 46,875 Hz (Osirus notes; not yet checked against the firmware's
  ESAI setup).
- No service manual or schematic was found for the DACs or output stage.

**Recommendation:** run the DSP at its real rate and resample to the host rate. Leave the analog
output unmodelled at first and list it as a known gap.

## Panel

- Display: 2 x 16 character LCD (`acvirus.cpp`; MAME's comment names an LM16255, and the firmware
  has its own display-type test naming ST7066 and KS0066 controllers).
- `virusc.lay` gives MAME's layout. It is a starting point; `panel.json` must be measured from a
  straight-on photo.

**Recommendation:** draw the desktop panel from a Commons photo, with the bar under it from
`host/PanelFrame.h`.

## Reference

- **Oracle:** the Osirus plugin, run as a separate program. Its notes claim bit-exact output. The
  only hardware measurement they report for A, B and C is latency: MIDI to audio about 324 samples
  (+/- 61), input 384 samples.
- **Real units:** two 128 kbit/s MP3 demos so far, which are good only for level, pitch and timing.
  More free recordings are needed before any per-event comparison means much.

**Recommendation:** with the same DSP engine, Osirus no longer checks the DSP independently. Use it
to check the real 80C515 path (same parameters reach the DSP, same output), and use real recordings
for everything the machine claims about the sound.

## CPU

First measurement, 2026-11-09, Apple M5 Max, one thread, clang -O2: MAME's dispatch and instruction
bodies run the firmware's program words at 10.38 ns per instruction (96.4 MIPS). A DSP busy on every
cycle at 120 MIPS would need 125 % of one core. This is a lower bound: unimplemented forms cost
almost nothing here, and it has no interrupts or peripherals. The program counter was forced through
the words in order, so branches and memory patterns are not the real ones.

An interpreter of this shape cannot carry a full patch load in real time, so the DSP needs a
recompiler.

Measured with the dsp56300 engine's recompiler, 2026-11-09, same machine, OS 6.6, the sustaining
preset "Moon PadJS", 30 s of held notes at 46,875 Hz, user CPU time:

| Held notes | CPU time | Share of one core |
|---|---|---|
| 1 | 2.38 s | 7.9 % |
| 8 | 4.17 s | 13.9 % |
| 16 | 8.92 s | 29.7 % |
| 32 | 3.26 s | 10.9 % |

The 32-note figure is lower than the 16-note one and is not understood. Those totals include
Gearmulator's C++ stand-in for the 80C515, not the real one, and no resampling.

Measured on this machine's own board, 2026-11-09, same machine and preset, 30 s of held notes after
the patch has loaded, real 80C515 included, no resampling, wall time of one thread:

| Held notes | Share of one core |
|---|---|
| 1 | 7.9 % |
| 8 | 8.9 % |
| 16 | 10.0 % |
| 32 | 10.9 % |

Worst blocks, 2026-11-09, same machine, core only, blocks of 125 frames (128 samples at 48 kHz,
2.67 ms), 247,808 blocks over all 1024 programs, after the boot-time warm-up:

| Pass | Mean | p99.9 | Max | Over 2.67 ms |
|---|---|---|---|---|
| First (code compiled as it's met) | 0.21 ms | 0.31 ms | 7.99 ms | 3 |
| Second (already compiled) | 0.23 ms | 0.30 ms | 0.91 ms | 0 |

The three late blocks are the first patch changes and the first note on a second program. Each
piece of code compiles once per instance.

## Open questions

1. What is the cost with the resampler, and of many instances at once?
2. Which OS 6.6 date is right, and does it matter?
3. Which free recordings of real units give isolated notes?
4. Which DACs and output stage does the unit use?
