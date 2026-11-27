# DASP 24: the instruction set

Alesis's effects DSP ("DASP 24" in the QuadraVerb's service manual, U11; "ZAK 24 ASIC" in the GT's
parts list). The decode below was made on the QuadraVerb's firmware (v1.07) before the GT machine
was started; `core/dasp24.*` is the GT's copy (`machines/qvgt`). The Plus's own configurations check
out against what they must compute (`tools/dspcheck`, 2026-11-27, as they did on the GT): the ring
modulator is a frequency shifter (two 3-section allpass chains as a Hilbert pair, a rotation whose
cos/sin coefficients the 80C31 rewrites every timer tick) that moves a sine cleanly, the other
sideband 31 dB down; the resonators are five combs with a two-tap averaging loop, ringing at their
tuned semitone ratios within the integer delays' rounding; the sampler records into the delay
memory, loops it, and plays it transposed from MIDI notes.

Recovered from the QuadraVerb v1.07 firmware by running it in the emulator, capturing
the DSP programs it builds for all factory presets, and checking candidate semantics
against what each program fragment has to compute. Status: a hypothesis that makes all
90 factory programs behave sensibly (flat EQ is exactly flat, delays land on the
right sample, reverbs decay smoothly). Not yet verified against hardware.

## Control store

The 8031 writes the DSP through the HC138 strobe Y0: XDATA writes to 0x0000-0x01FF
with A0-A8 as the DSP address (STB on DASP pin 40). Addresses 0x000-0x17F hold
96 instruction words of 4 bytes each, executed once per sample (24 MHz / 576 =
41,666.67 Hz; 6 clocks per instruction). 0x180 is a control register that the timer
ISR writes about 3,600 times a second, together with 0x17F (the opcode byte of word 95).

## Word format

| Byte | Bits | Meaning |
|---|---|---|
| 0-1 | 15:0 | A: delay-memory offset (little endian) |
| 2 | 7:0 | coefficient magnitude, bits 10:3 |
| 3 | 7:5 | coefficient magnitude, bits 2:0 |
| 3 | 4 | N: negate the coefficient |
| 3 | 3 | W: write |
| 3 | 2 | L: latch |
| 3 | 1:0 | M: addend select |

Coefficient = (N ? -1 : 1) × magnitude / 2048, so the range is ±0.9995. Programs
reach larger gains by repeating an instruction (for example `a1` of a biquad is
applied twice).

## Execution (per instruction)

- Memory address = (pointer + A) mod 65536. The pointer decrements by 1 every sample,
  so a value written at offset A is read back d samples later at offset A + d. Low
  offsets act as scratch registers and one-sample delays.
- Multiplicand X: memory[addr] for reads; ACC for writes (W=1), which also store ACC
  into memory[addr].
- ACC ← coefficient × X + addend.
- Non-latch instructions (L=0): addend = 0, ACC, R0 or R1 for M = 00, 01, 10, 11.
- Latch instructions (L=1): R[M bit 0] ← X; addend = ACC if M bit 1 is set, else 0.

So the useful idioms are:

| Op (low 4 bits) | Effect |
|---|---|
| 0 | ACC = k·mem |
| 1 | ACC += k·mem |
| 2 / 3 | ACC = k·mem + R0 / R1 |
| 4 / 5 | R0 / R1 = mem; ACC = k·mem |
| 6 / 7 | R0 / R1 = mem; ACC += k·mem |
| 8 | mem = ACC; ACC = k·ACC |
| 9 | mem = ACC; ACC = k·ACC + ACC |
| A / B | mem = ACC; ACC = k·ACC + R0 / R1 (allpass: R0 holds the delayed sample) |
| C / D | mem = ACC; R0 / R1 = ACC; ACC = k·ACC |
| E / F | mem = ACC; R0 / R1 = ACC; ACC = k·ACC + ACC |

Allpass idiom: `op06|N @end, -a` (latch the delayed sample d into R0, ACC = in − a·d),
then `op0A @start, +a` (store, ACC = a·w + d).

## I/O

- ADC: instruction 94 is always a write to offset 0xFFFF. Its data and multiplicand are
  replaced by the ADC sample, so the input appears at offset 0 on the next sample, and
  configs whose instruction 94 latches (op 0C) also leave it in R0.
- DAC: the memory reads of instruction 0 (left) and 95 (right) go to the DAC. Programs
  write the outputs to adjacent offsets (0x10/0x11, or 0x3F/0x40) and read them back
  there with coefficient 0.
- The delay pointer steps between instructions 94 and 95, so instruction 95 already
  uses the next sample's addressing. (Without this, the plate programs' right channel
  reads a damping-filter state that shares the cell.)
- The input is mono: the QuadraVerb sums L+R before its single SAR ADC.

## Control-store writes

The 8031 writes a shadow copy; a write to 0x17F commits it to the running program.
Every program load and every LFO update batch (about 65 us, 2.7 samples) ends with a
write to 0x17F, and the timer ISR writes 0x17F and 0x180 every 250 us. Applying writes
immediately produces glitches when an address byte is written before its carry, about
200-400 per 2 s on chorus and flange. With the commit, there are none.

## Open questions

- Number formats: ACC width, rounding, saturation (OVFLO pin drives the clip LED),
  and the 20-bit DRAM word format (fixed or floating point?).
- Exact pipeline timing of DAC/ADC slots (affects latency by a sample, not sound).
- Meaning of control register 0x180.
- Whether the commit is on 0x17F or 0x180 (both are consistent with the firmware).
