# Research

The Alesis QuadraVerb GT (1991): a guitar preamp (compressor, overdrive, distortion, tone, bass
boost, cabinet simulator, noise gate, effects loop: all analog) in front of the QuadraVerb Plus's
digital effects (EQ, pitch, delay, reverb, Leslie, ring modulator, resonators, sampling) in one 1U
box. Plugin `al-fx-quadgt`, an effect (Lemus FX). Sources: `sources.md`.

**Target:** firmware v1.03 (10/28/92, the last release), main board drawing 9-50-1050 rev B
(8-22-91), the only schematic published. v1.00 (07/08/91) and v1.01 (05/22/92) differ in the
firmware only (service manual §2.0).

## Summary

| Block | Source | Recommendation |
|---|---|---|
| CPU, board, firmware | v1.03 dump; service manual; the earlier QuadraVerb emulator | Reuse the earlier 80C31 core and board model; map the GT's extra latches by running it; check the core in lockstep against MAME's |
| DSP (DASP 24 / ZAK 24) | Decoded from QuadraVerb v1.07, a working hypothesis | Port the interpreter; check every GT factory program, above all the Plus-only configurations |
| Delay memory | 5 x 4464, 64K x 20 bits | As in the earlier emulator; settle the word format |
| A/D, D/A | SAR in the DSP + LM311 + PCM54; pre/de-emphasis; 20 kHz filter module | Circuit model from sheet 2; the filter module is the one part without data |
| Preamp | Schematic sheet 1, every value; SSM2120, TL084, CD4051/53, 2N5460, 1N4148 data sheets | Transcribe and solve as netlists (tr909 solver), checked against ngspice |
| Dry path, outputs | Sheet 2: CEM3381 dry VCA, output stages | Netlist; VCA from the CEM3381 data sheet |
| Panel | Reference manual drawings, photos still to find | Measure from a straight-on photo; 32-character backlit LCD |
| Reference | 10 YouTube recordings of real GT units | Cut scenes where the LCD or the video gives the program; per-event comparison |

## CPU, board and firmware

Facts (service manual parts list and §2.0; the earlier emulator's notes):
- 80C31 (U32) at 12 MHz from a 24 MHz crystal (Z1); 27C512 EPROM (U30), 32K x 8 SRAM (U29,
  CXK58257, battery-backed by a 3 V lithium cell), 6N138 MIDI opto-isolator, 74HC138 decoder (U35),
  74HC573 address latch, five 74HC574 output latches (U10, U11, U36-U38), 74HC00/74HCU04/74HC74.
- The EPROM's A0-A7 are wired in reverse; descrambled, the reset vector is LJMP 00E0h (the
  QuadraVerb's is 00ABh). Banner strings "ALESIS / QUADRAVERB GT", "(C) 1991 ALESIS CORPORATION".
- The QuadraVerb's memory map (from its schematic and code): XDATA 8000h-FFFFh = SRAM; writes below
  8000h go through the 74HC138 on A9-A11: 0000h DSP (A0-A8), 0200h LCD (A0 = RS), 0400h/0600h/0800h
  latches; keypad a 4 x 4 matrix on P1; P3.4 the bypass footswitch.
- The GT adds two latches for the preamp: U10 and U11 (74HC574, clocked from U35 pins 9 and 10),
  driving the analog switches through J4. Their bits, from sheet 1: U10 = DIST ON, TONE
  PRESENCE/BRIGHT, DIST 3, DIST 2, DIST 1, BASS BOOST, CABINET 1/2, CABINET ON; U11 = overdrive VCA
  control (three bits into R114-R116), COMP 1-3, TONE ON/OFF (bit order to read off the drawing).
- Tested 2026-11-11: the GT v1.03 image boots unmodified in the QuadraVerb board model (banner,
  "PROGRAM 00", DSP program loaded) and renders its factory programs (GUITARBYALESIS, GTRBYAL+OD+RES,
  WATERFALLS, L.A. METAL, DIRTY DELAY) with clean output and decaying tails. Only the preamp, which
  the model doesn't have, is missing.

Recommendation: bring the earlier 80C31 core and board model into `core/`. Map the U10/U11 writes
(and any other differences: the PRE-AMP key, LEDs, the noise-gate threshold output) with a scripted
driver that steps every preamp page. The method asks for MAME's CPU core: run MAME's mcs51 (BSD-3)
in lockstep with ours on boot, program changes and edits; keep ours if they agree, port MAME's if
not.

## DSP: DASP 24 (ZAK 24)

The GT parts list calls it "ZAK 24 ASIC" (AMI); the QuadraVerb service manual "DASP 24": a SAR for
the A/D, a writable control store, an ALU and a DRAM controller in one chip.

The working decode (from the QuadraVerb v1.07 firmware, by running it and checking what each program
fragment has to compute):
- 96 instructions of 4 bytes, executed once per sample: 24 MHz / 576 = 41,666.67 Hz, 6 clocks
  each. The 80C31 writes them at XDATA 0000h-017Fh; 0180h is a control register.
- Word: 16-bit delay-memory offset; 11-bit coefficient magnitude / 2048 with a sign bit (range
  +-0.9995); write, latch and a 2-bit addend select (0, ACC, R0, R1).
- Address = (pointer + offset) mod 65536, the pointer stepping down once a sample, so low offsets
  are registers and one-sample delays.
- Instruction 94 writes the A/D sample; instructions 0 and 95 read the left and right D/A outputs.
- Writes go to a shadow copy, committed by a write to 017Fh (otherwise chorus and flange glitch).
- Status: all 90 QuadraVerb programs behave (flat EQ exactly flat, delays on the right sample,
  smooth reverb decays). Not checked against hardware. Guessed: 24-bit saturating accumulator,
  20-bit memory, truncation.

Recommendation: port the interpreter as it is, with the fixed-point model switchable for tests.
Check every GT factory program, above all the configurations the original QuadraVerb doesn't have
(ring modulator, resonators, sampling): they may use instruction forms v1.07 never wrote.

## Delay memory

Five 64K x 4 DRAMs (4464, U22-U26): 64K words of 20 bits, 1.573 s at 41,666.67 Hz, which matches
the manual's "up to 1.5 seconds" of sampling. Recommendation: as in the earlier emulator. Open: the
20-bit word's format (fixed point assumed; the DSP may store a floating format).

## A/D and D/A

- A/D: successive approximation in the DSP, comparing against the PCM54 (U20, 16-bit) through the
  LM311 (U21) comparator (QuadraVerb service manual). L and R summed to mono before it, as on the
  QuadraVerb (to confirm on sheet 2). An input pre-emphasis and a 20 kHz low-pass module (part
  7-20-0001) before it.
- D/A: the PCM54, sample-and-hold, reconstruction filter, de-emphasis.
- The earlier emulator's model: pre-emphasis zero 5.1 kHz / pole 24 kHz, 16-bit A/D, top 16 bits
  to the D/A, zero-order-hold droop, exact inverse de-emphasis, 56-58 samples of latency at 44.1/48
  kHz.

Recommendation: redo these from sheet 2 as netlists. The filter module has no data: model it from
its role (20 kHz anti-alias) and say so in improvements.md.

## Preamp (sheet 1)

Every part and value is on the drawing. The blocks, in signal order (to be transcribed in
`circuit.md`):
- **Inputs**: J10/J11 (left, right) into U1/U2 (TL084) buffers, the dual 10KB INPUT pot.
- **Compressor**: the SSM2120's (U4) first level detector (log averaging, C21 4.7 uF, R71 1M) and
  first VCA. Seven amounts and off: U14 (CD4051, COMP 1-3) picks one of six resistors (300 ohm to
  8.2K, R107-R112) feeding the detector's control output back into the VCA's Vc-.
- **Overdrive**: an op-amp gain stage (U1 section, R38/R39) with a diode network in its feedback
  (D6-D10, R36 47K), switched in by U3 (CD4053); the amount set by the SSM2120's VCA control (Vc+,
  pin 5) from U11's three bits through R114-R116. Seven amounts and off.
- **Distortion**: eight types and off. U8 (CD4051, DIST 1-3) selects one of eight resistor pairs
  (R15-R30, 1K/3.9K to 220K/9.1K) setting the gain of a stage with clipping diodes D1-D5; Q1
  (2N5460 P-JFET) is driven from the compressor's control output (U4 pin 3 through R75), so the
  distortion follows the guitar's level; U9 (CD4053) switches it in (DIST ON).
- **Preamp tone**: Flat, Presence (treble boost), Bright (treble boost, mid cut): U7 filter
  sections (C22-C24, C34, C35), switched by TONE ON/OFF and TONE PRESENCE/BRIGHT.
- **Bass boost**: on or off (U5, U6 section).
- **Cabinet simulator**: off, Cabinet 1 (2 x 10"), Cabinet 2 (4 x 12"): active filters around U5/U6
  (C14 5600 pF, C15/C16 2700 pF, C17 5600 pF), CABINET ON and CABINET 1/2.
- **Noise gate**: the SSM2120's second detector and VCA; the threshold from U39 pin 1 (sheet 2,
  set by the CPU); 18 settings including AUTO.
- **Effects loop**: send J7, return J8 (U13), in or out by program.

Recommendation: transcribe to `docs/circuit.md` part by part; netlists on the tr909 solver, each
checked against ngspice. The nonlinear parts: the diode networks, the JFET, the SSM2120 (its VCAs'
exponential control, the rectifier and log-averaging detector, as its data sheet gives them), the
TL084s at the rails. The overdrive and distortion stages will need oversampling; measure how much
against ngspice.

## Dry path and outputs (sheet 2)

The CEM3381 (U12) "DRY" VCA mixes the analog direct signal into the outputs (VCA C.V. from the CPU;
the QuadraVerb's equivalent was a 5-bit level on a latch); output stages U15/U16 with the dual 10KA
OUTPUT pot to J9/J10. Recommendation: netlist; the earlier emulator's dry-level law (n/31) to be
checked against this drawing.

## Panel and controls

Backlit 32-character (2 x 16) LCD; keys with pressure-sensitive scrolling ("press harder to scroll
faster"); INPUT and OUTPUT dual pots; LEDs on a pot board and key board; a PRE-AMP key the
QuadraVerb lacks. Rear: inputs, outputs, effect send/return, bypass footswitch (J13, to P3.5),
MIDI in/out. Recommendation: `docs/panel.json` from a straight-on photo (to find: reverb.com
listings, the repair videos); read the key matrix and the pressure sensing from the firmware.

## Constants

| Constant | Value | Source |
|---|---|---|
| Sample rate | 41,666.67 Hz (24 MHz / 576) | QuadraVerb notes; crystal Z1 24 MHz |
| DSP program | 96 x 4 bytes per sample | firmware |
| Delay memory | 65,536 x 20 bits | 5 x 4464 |
| A/D, D/A | 16 bits (PCM54) | parts list |
| Supplies | +5, +12, -12 V (7805, 7812, 7912) | parts list |
| Programs | 100 | reference manual |
| Compression, overdrive, distortion | 7, 7, 8 settings + off | reference manual §4.3 |
| Noise gate | 0-17 (AUTO among them) | service manual MIDI program layout |

## Reference

Ten recordings of real GT units (`sources.md`). No unit has a documented setting yet: the first
job is to find scenes where the LCD shows the program or the parameter. No GT emulation exists to
compare against; QDV1's A/B renders cover the QuadraVerb Plus's digital effects only.

## Open questions

1. The DSP's number formats: accumulator width, rounding, saturation, the 20-bit word format.
2. Whether the Plus-only configurations use instruction forms the v1.07 decode doesn't cover.
3. U10/U11 bit order and timing; how the noise-gate threshold (U39) is set.
4. The 20 kHz filter module (7-20-0001): no data.
5. The pressure-sensitive keys: how the firmware reads them.
6. Our 80C31 core against MAME's.
7. Mono summing before the A/D on the GT (the QuadraVerb sums L+R).
8. A straight-on panel photo.
