# Firmware

The GT's v1.03 EPROM (27C512, U30) on the 80C31 (U32): what the board model gives it and what has
been checked. Map and wiring as `core/board.h` has them; their sources in `research.md`.

## The image

The board wires the EPROM's A0-A7 in reverse; `Board::prepareRom` swaps them back (it accepts either
form). Descrambled, the reset vector is `02 00 E0` (LJMP 00E0h) and the banners read "ALESIS",
"QUADRAVERB  GT", "(C) 1991 ALESIS CORPORATION".

## Boot

From reset with a blank RAM: the LCD shows all blocks, clears at 74 ms, writes "ALESIS / QUADRAVERB
GT" at 202 ms, "(C) 1991 ALESIS / CORPORATION" at 1.46 s, and "PROGRAM 00." at 2.94 s, loading the
first DSP program. With a blank RAM the program names are empty (character code 0): the unit needs
its factory programs, which `Unit::recallFactoryPrograms` loads with the panel keys as reference
manual 3.6 says (STORE, PAGE UP three times, "RECALL ALL 90 ALESIS PROGRAMS", STORE). The keys it
uses have the QuadraVerb's numbers (STORE 10, PAGE UP 7), so the GT's key matrix shares at least
those. Afterwards program 00 is "GUITARBYALESIS" and program 05 "WATERFALLS".

## The CPU core

MAME's mcs51 (BSD-3), ported as `core/i8031.*` (README, "Code from other projects"). Checked in
lockstep on 2026-11-11 against the 80C31 core of an earlier QuadraVerb emulator written for this
project, both running the GT firmware with the same key presses for 10 s (boot, the factory recall,
a program change through the keys):
- At the same cycle counts (the earlier core's 1.5 % overrun corrected, below), the SRAM, the LCD
  and the DSP's control store are identical for the first 2.93 s.
- After that, their interrupt timing differs slightly (MAME samples interrupt flags a cycle late and
  holds off a poll after RETI or an IE/IP write), so the 80C31's timer-driven LFO updates land at
  different instants; both reach the same states (LCD and SRAM equal) at every checkpoint.
- The earlier core ran each sample's 24 machine cycles from wherever the last instruction ended,
  dropping the overshoot: 10,154,244 cycles in 10 s instead of 10,000,000, so its LFOs ran 1.5 %
  fast. `Board` carries the overshoot to the next sample.

## The panel (mapped 2026-11-11 by pressing each key through the firmware)

The GT's sixteen keys sit on the QuadraVerb's 4 x 4 matrix; NAME/MOD and PRE-AMP take the places of
the QuadraVerb's NAME and MOD. The layout is the quick-setup sheet's (`docs/manual/gt-_QuickSetup.pdf`).

| Key | Name | What the firmware shows | LED (latch, bit; active low) |
|---|---|---|---|
| 0 | EQ 4 | EQ PRESET | 0800h b7 |
| 1 | PRE-AMP 9 | COMPRESSION | 0800h b6 |
| 2 | VALUE down | | |
| 3 | VALUE up | | |
| 4 | MIDI 5 | MIDI CHANNEL | 0400h b7 |
| 5 | BYPASS 0 | (bypass) | 0400h b6 |
| 6 | PAGE down | | |
| 7 | PAGE up | | |
| 8 | PITCH 3 | PITCH MODE | 0800h b5 |
| 9 | NAME/MOD 8 | EDIT NAME | 0800h b4 |
| 10 | STORE | the store pages | 0400h b5 |
| 11 | PROG | PROGRAM nn | 0600h b7 |
| 12 | DELAY 2 | DELAY TYPE | 0800h b3 |
| 13 | MIX 7 | DIRECT LEVEL | 0800h b2 |
| 14 | CONFIG 6 | CONFIGURATION | 0800h b0 |
| 15 | REVERB 1 | REVERB TYPE | 0800h b1 |

**Key force.** The "touch sensitive" keys sense force with a piezo (Z2, on the key board) under the
keypad: U39 buffers it, a comparator sets P3.3 while it is above the level detector's ramp (C108,
charged through R209 510K from +12 V, discharged by latch 0600h bit 6), and latch 0600h bit 5 (Q4)
shorts it. The firmware times the comparator against the ramp. Holding VALUE up for 3 s from program
00, at piezo volts: 0 V, 3 programs; 0.05 V, 4; 0.2 V, 5; 0.5 V, 12; 1 V and up, 58 (the most). The
plugin presses at 0.1 V, and at 2 V with Shift (`core/unit.h`).

## The preamp's controls

Swept page by page through the PRE-AMP pages (VALUE taps), logging the latches. The bits as the
80C31 writes them; the switches they drive on sheet 1 (`circuit.md`).

| Page | Values | Latch, bits |
|---|---|---|
| COMPRESSION | off, 1-7 | U11 (0A00h) b3-b1 = the amount bit-reversed (1 = b3), U14's select; 0: channel 0 (grounded) |
| OVERDRIVE | off, 1-7 | U11 b4-b6 = the amount (b4 the least): the VCA control's 27K/13K/6.8K; off switches the stage out (D12-D14) |
| DISTORTION | off, 1-8 | U10 (0C00h) b0 = on; b2-b4 = U8's select, inverted and reversed: type n gives ~(n - 1) with b4 its lowest bit |
| PREAMP TONE | flat, presence, bright | U10 b1 = 1 for flat; U11 b0 = 1 presence, 0 bright |
| BASS BOOST | off, on | U10 b5 = 1 on |
| CAB SIMULATOR | off, cabinet 1, cabinet 2 (and program/global) | U10 b7 = 1 on; b6 = 1 cabinet 2 |
| EFFECT LOOP | out, in | U11 b7 = 1 in |
| NOISE GATE | auto, off, 1-16 | 0400h b4 = 1 off; b0-b3 = 16 - n (the threshold DAC, U37 into U39); auto sets n from the compression and distortion (n = 5 at compression 4, distortion 7) |
| PREAMP OUT LEVEL | 0-99 | in the DSP (after the effects return), not a latch |

Two of sheet 1's labels disagree with the firmware: U11 Q19 is drawn "TONE: ON/OFF" and U10 Q13
"TONE: PRESENCE/BRIGHT", but the firmware uses U10 b1 as flat/on and U11 b0 as presence/bright. And
U11 Q12, drawn "+5V", is the effects loop's switch (its other end is labelled "FX LOOP U11 PIN 12").
U10's D inputs are drawn in the reverse order of U11's on the J4 bus; the firmware's bits fit U10
taken straight (D0 = b0).

Sheet 2: U35's C input is labelled A12; the firmware writes the latches at 0800h, 0A00h and 0C00h
and never sets A12, so it is A11, as on the QuadraVerb (a drawing error).

The other latches (sheet 2): U37 = 0400h (b0-b4 the gate threshold DAC, b5-b7 the STORE, BYPASS,
MIDI LEDs); U36 = 0600h (b0-b4 the dry VCA's control DAC: b1 1.5K, b2 3K, b3 6.2K, b4 12K, b0 through
D18; b5 the key piezo's reset, b6 the level detector's reset, b7 the PROG LED); U38 = 0800h (the
eight module LEDs).

## Timer 0 and the CPU's oscillators

Timer 0 (mode 1) drives the firmware's periodic work. Its ISR (vector 000Bh, at 00B8h) reloads
FF06h (250 cycles), counts the key piezo's and the level detector's comparator times (P3.3, P3.2),
and unless bit 2Ah.0 is set jumps through 60h/61h to the configuration's handler. A handler that
runs long re-arms the timer itself at its end (FF80h, 128 cycles, and clears TF0), so the ISR's
rate is the handler's length plus 128 cycles. Measured in the model (2026-11-11): about 3,855 a
second in most configurations; 1,960 in the ring modulator's and 1,898 in the Lezlie's, whose
handlers step oscillators on the CPU (a two-variable rotation, written into the DSP's
coefficients each tick: instructions 17, 18, 20, 21 for the ring modulator).

The ring modulator's frequency word is 153 per hertz of SPECTRUM SHIFT (XRAM 8631h), which is right
for about 2,690 ticks a second; at 1,960 the shift comes out at 0.71 of the display (100 Hz shifts
by 71 Hz). The chip is a standard 80C31 at 12 MHz (24 MHz halved by U33), the core counts the 8051
data sheet's cycles, and v1.00 and v1.03 run the same code with the same word, so a unit should do
the same: taken as the GT's own behaviour, not the model's. A recording of a unit's ring modulator
at a known shift would settle it.

## MIDI

MIDI in, the firmware's own MIDI THRU (on by default: what comes in goes out) and MIDI out work; with
SYSEX ENABLE on, a dump request (`F0 00 00 0E 07 03 64 F7`, the edit buffer) returns a 155-byte dump.

## Still open

- The level detector (gated effects, sampler triggers) now takes sheet 2's envelope in volts
  (`circuit.md`); how the firmware's thresholds sit against it is unchecked.
- The rear footswitch jacks (bypass, program advance): in the board model, not on the panel yet.
