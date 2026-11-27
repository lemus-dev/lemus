# Research

The Alesis QuadraVerb (1989) and QuadraVerb Plus (1990): a 1U digital multi-effects unit, EQ,
pitch (chorus, flange, detune), delay, reverb and a Leslie simulation in five configurations, four
effects at once; the Plus adds ring modulator, resonators and sampling. Plugin `al-fx-quad`, an
effect (Lemus FX). Sources: `sources.md`.

**Target:** main board rev F (PCB 9-40-1019, the service manual's parts list), as the service
manual's two schematic sheets draw it, with firmware v2.03 (QuadraVerb Plus, 3/10/92, the last
release) or v1.07 (QuadraVerb, 3/8/90). One circuit only; no setting for the earlier boards.

## Summary

| Block | Source | Recommendation |
|---|---|---|
| QuadraVerb and Plus | Plus manual addendum; firmware strings; magazine and listing photos | One machine: the Plus is the same hardware with another EPROM. The firmware the user has decides; both present, v2.03 runs |
| CPU, board, firmware | v2.03 and v1.07 dumps; service manual sheet 2; `machines/qvgt` (the GT's board was derived from this one) | Copy the GT's 80C31 (MAME's mcs51), board model and DASP 24; check the map against sheet 2 and the firmware |
| DSP (DASP 24) | Decoded from v1.07 by this project (`dasp24.md`); runs every GT program, Plus configurations included | Copy as it is |
| A/D, D/A | Sheet 1 (pre-emphasis, the 20 kHz module, the input sample-and-hold, the offset servo) and sheet 2 (PCM54, U8 comparator) | The GT's model with this board's part numbers; the module as the GT's (the decimator) |
| Input stage | Sheet 1: buffers, INPUT pot stages, passive mono sum | Linear: its exact transfer at each pot setting |
| Dry path, outputs | Sheet 1: CEM3381 (U5) into U2's summers, OUTPUT pot, U2's output stages | The GT's model with this board's values |
| Panel | Reference manual front-panel drawing; photos (`private/qv/docs/photos`) | Measure from straight-on photos (panel.json) |
| Reference | Free recordings of real units (`private/qv/reference`) | Per-event comparisons where the program or settings are known |

## QuadraVerb and QuadraVerb Plus

- The Plus manual addendum: the new features "are made possible by a simple software update,
  which requires only the replacement of its EPROM", "The hardware of the QuadraVerb remains
  unchanged", and patches written for the QuadraVerb sound the same on the Plus.
- The service manual covers both: one software history from 1.00 (1/15/89) to 2.03 (3/10/92), and
  the Plus's MIDI implementation (§11).
- Firmware strings (descrambled images): v1.07 offers five configurations (EQ-PITCH-DELAY-REVERB,
  LEZLIE-DELAY-REVERB, GRAPHIC EQ-DELAY, 5 BAND EQ-PITCH-DELAY, 3 BAND EQ-REVERB); v2.03 adds
  RING-DL-REVERB, RESONATOR-DL-REV and SAMPLING, the MULTI TAP DELAY type, the ring modulator's
  SPECTRUM SHIFT, five resonator tunings, reverb predelay and input mix. Both keep 90 factory
  programs (RECALL ALL 90 ALESIS PROGRAMS) and 8 modulation routings. v2.03 boots "QUADRAVERB Plus
  ... SOFTWARE VERSION NUMBER: 2.03".
- The panel: a brand-new Plus in Sound On Sound's competition (February 1991, p. 84) and units sold
  as Plus show the QuadraVerb's panel, print and logo unchanged.

## Board revisions

From the service manual (§3.1, §7):
- Anti-aliasing filter: boards A-E a four-pole active filter (some upgraded to six poles with the
  I/P filter kit, §7.10); rev F and later the "LC block filter", one component (the "20 KHz Low
  Pass Filter" on sheet 1, part ME 20K LPF 7-20-0001).
- Wet VCA: boards A-E pass the wet signal through a VCA (U6); software 1.05 (11/30/89) "accommodates
  new hardware in which VCA after DAC is removed", scaling the effects in software; §7.7 removes U6
  on older boards. Sheet 1 has no wet VCA: U5, the CEM3381, carries the dry signal only.
- So v1.03 and earlier don't fit rev F (they set the effect level through the missing VCA): the
  machine takes v1.07 and v2.03 only.
- Serial range of rev F: not in the manual. Open.

## CPU, board and firmware

Facts (service manual §4, sheet 2):
- 80C31 (U21) at 12 MHz (Z1 24 MHz halved by U24); 27C512 EPROM (U19); 62256 SRAM (U18, 32K,
  battery-backed; sheet 2 still labels it 6264, §1.3 names 58256/62256); 74HC573 (U20); 6N138 MIDI
  in (U30).
- Memory-mapped latches (§4.3): writes with A15 low go to the 74HC138 (U23) on A9, A10 and A12 as
  drawn (the GT's notes: the firmware never sets A12, so it is A11, as on the GT): Y0 the DASP 24's
  STB, Y1 the LCD, Y2-Y4 three 74HC574 latches (U27-U29): the key LEDs, the dry VCA's control
  ladder, Q1 (the piezo's reset) and Q2 (the level detector's reset).
- Keys: a 4 x 4 matrix on P1 (J3), sixteen keys; the bypass and program-advance footswitches on
  P3.4, P3.5 (J8, J7).
- Analog in (§4.4): the input level and the key force are timed against C56 charging through R51
  510K from +12 V, compared by U22 and read on P3.2 and P3.3.
- The EPROM's A0-A7 are wired in reverse (the earlier emulator's notes); descrambled, v1.07 starts
  `02 00 AB` and v2.03 `02 00 E0`.
- Factory programs: v1.07 at ROM D2FEh + 128 n (n 0-89), copied to SRAM CE00h + 128 n by a power-up
  holding BYPASS and PROG (§5.0: "reinitialized").
- Self-diagnostics (§5.0): MIDI out looped to MIDI in, power up holding BYPASS and CONFIG: ROM, RAM,
  button LEDs, MIDI and the clip LED; a failure stops with "ERROR IN {COMPONENT}". The service
  manual's only test.

Recommendation: copy `machines/qvgt`'s 80C31, board and DASP 24; map the keys and LEDs by pressing
each key through the firmware; run the self-diagnostics as the service check.

## DSP

The DASP 24 (U11): SAR, writable control store, ALU, DRAM controller (§4.5). Its instruction set was
decoded from v1.07 by this project before the GT (`machines/qvgt/docs/dasp24.md`, copied as
`dasp24.md`): 96 four-byte words a sample, a 16-bit delay offset, an 11-bit signed coefficient, the
A/D sample written by instruction 94, the D/A read by 0 and 95, writes committed by a write to 017Fh.
On the GT it runs every factory program, the Plus's configurations among them (`qvgtdspcheck`).
Delay memory: five TMS4464 (64K x 4), 64K words of 20 bits, 1.573 s at 41,666.67 Hz.

Recommendation: copy as it is. Not checked against hardware anywhere: the accumulator width,
rounding, saturation and the 20-bit word format are guesses (as on the GT).

## A/D and D/A (sheets 1 and 2)

The same circuit as the GT's sheet 2, with other part numbers:

| Here | GT | Part |
|---|---|---|
| U4 pins 1-3, R23 75K, R24 20K, C43 330p | U16, R142, R144, C41 | Pre-emphasis, (1 + s 95K C) / (1 + s 20K C) |
| R25 5.6K, module, R26 8.2K | R143, module, R138 | The 20 kHz LC low-pass, doubly terminated |
| R48, R47 51K, C13 4.7u, R36 510K to -12 V | R140, R137, C64, R136 | The offset servo from LSTMSB (§4.5's description) |
| U3 pins 5-7, R15 470, U9 (4053), C38 1000p, U3 pins 8-10 | | The input sample-and-hold (§3.2) |
| U8 (LM311), U10 (PCM54), R46 4.7K | U21, U20 | SAR comparator, the 16-bit DAC |
| R42 750, U9, C39/C40 2200p | R154, U19, C59/C60 | The output sample-and-holds; R42 sets the de-emphasis (§7.10) |
| U3 buffers, U4 Sallen-Key: R30/R31, R29/R28 1.1K, C50/C46 .01u, C49/C48 3300p | U18, U16, R133/R134, C51, C49 | The reconstruction low-pass |

The service manual's Bode plot (diagram 9) is the I/P filter kit's on the older boards (at U3 pin
7: flat to about 5 kHz, -3 dB at 12 kHz, a peak at 15 kHz, -20 dB at 17 kHz, -30 dB at 20 kHz), not
the LC module's; the module has no published values or response.

Recommendation: the GT's model with these parts; the module as the GT has it (the decimator's
low-pass), stated in improvements.md.

## Input stage (sheet 1)

- J13/J14: the left jack's switch feeds the right input (R (MONO) on the rear) to the left with no
  plug in the left.
- R2/R5 1K into U1's followers, R3/R6 1M to ground (input impedance 1M).
- C1/C2 4.7u, R1/R4 1K into the INPUT pot's stages: a 10K B dual (R1A, R1B), its track from the
  input resistor to U1's output and its wiper at U1's - input. Gain -R_b / (1K + R_a), R_a the track
  from the input end to the wiper, R_b the rest: -10 (+20 dB) at full, 0 at the bottom. The coupling
  capacitor's corner moves with it: 1 / (2 pi (1K + R_a) 4.7u), 34 Hz at full, 5.6 Hz at mid.
- The two sides through R17/R18 470R to one node: the passive mono sum, (L + R) / 2, into U4's
  pre-emphasis (pin 3) and the level detector's follower (pin 5).
- Each side also feeds the dry path (C9/C10 4.7u, R32/R33 20K into the CEM3381's inputs).

## Level detector, AUDIO SIGNAL DISPLAY (sheet 1)

U4 pins 5-7 and D2: a precision peak detector into R27 330K and C45 0.1u (film), as the GT's. U7's
four comparators against the ladder R38 92K, R39 180K, R40 390K, R41 750K drive the -18, -12, -6 dB
and CLIP LEDs; the DASP's OVFLO sets U24, which holds the CLIP LED (R55 2.2M, C73 0.1u). The
detector's output also feeds U22 (§4.4).

## Dry path and outputs (sheet 1)

- U5, CEM3381 (both halves labelled DRY): its inputs from the INPUT stages through C9/C10 4.7u and
  R32/R33 20K, its current outputs into U2's summers (pins 1-3 left, 5-7 right), with the wet
  through R34/R45 20K; feedback R7/R8 20K with C26/C27 150p.
- VCA C.V.: the latch the firmware writes at 0600h (U29 on the drawing), Q1-Q4 through 1.5K, 3K,
  6.2K and 12K to a node that R75 10K joins to the C.V., Q0 through R77 20K and through D8 and a
  4.7K, onto C52 0.1u (film). The GT's ladder has the same values.
- C4/C5 4.7u into the OUTPUT pot (10K A dual), its wiper into U2's non-inverting stages (pins 8-10,
  12-14): gain 1 + R10/(R11 + 1/sC8) = 2 (R10, R13 2K; R11, R12 2K; C6, C8 4.7u), R9/R14 470R to the
  jacks (§7.9: R11/R12 to 4.7-10K for more output gain, a service option).

Recommendation: the GT's model with these values (its summers had 20K too; its output stages a gain
of 1 + 4.7K/2K and 10u coupling).

## Panel

Front, left to right (reference manual §2 drawing, photos): the four AUDIO SIGNAL DISPLAY LEDs
(CLIP, -6, -12, -18), INPUT and OUTPUT knobs, the 2 x 16 LCD (not backlit on the original), PAGE
and VALUE up/down, PROG and STORE, the ten function keys (REVERB 1, DELAY 2, PITCH 3, EQ 4, MIDI 5,
CONFIG 6, MIX 7, MOD 8, NAME 9, BYPASS 0) with LEDs, POWER. Rear: power, MIDI in/out, footswitches
(BYPASS, PROGRAM ADVANCE), outputs and inputs. Recommendation: measure from the photos
(`panel.json`), as the GT.

## Reference

`private/qv/reference/` (sources.md): recordings of real units from YouTube, the vendor's hardware
recordings on Temecula's QDV1 page, impulse responses on Freesound. No open emulator to compare
against.

## Open questions

1. The serial range of rev F boards.
2. The 20 kHz module's response (no data; the GT's open question too).
3. The DSP's number formats (shared with the GT).
4. The LED and key mapping on the latches and the matrix (by running the firmware).
5. Whether the v1.07 and v2.03 images run with the GT's board unchanged.
