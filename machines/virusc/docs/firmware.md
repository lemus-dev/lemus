# Firmware and board

What the 80C515 sees, and how each line was established. Run with `tools/viruscrun`.

## Clocks

| Clock | Value | Source |
|---|---|---|
| 80C515 crystal | 12 MHz | The firmware's UART setup: TMOD `21`, TH1 `FE`, SMOD 1, which gives 31250 baud only from 12 MHz (`viruscrun`). MAME's 24 MHz is wrong for this OS |
| DSP clock | 136 MHz from EXTAL 12 MHz | The firmware writes PCTL `B40087`. It first writes `B4006B` (108 MHz), then `B40087` (`VIRUSC_DSP_LOG=1`) |
| Sample rate | 46875 Hz (12 MHz / 256) | Osirus notes; not yet checked against the unit |

## 80C515 memory (MAME `acvirus.cpp`, and what the firmware does)

- Code: `$0000-$7FFF` the first 32 KB of flash; `$8000-$FFFF` the 32 KB bank P5 bits 4-7 select.
- Data: `$0000-$7FFF` RAM; host port at `$0400-$0407` while P3.3 is low ("Host port boot"), RAM there
  otherwise.
- Data `$8000-$FFFF`: the battery-backed RAM, four 32 KB banks chosen by P5 bits 4-5, whatever
  P5 bit 3 is. MAME switches the bank only when bit 3 is set, but bit 3 belongs to the key and LED scan.
  - The firmware reads programs of banks C-H as code (MOVC), from the flash bank P5 selects.
  - It reads banks A and B and the multis through this window, at P5 banks 10, 11 and 9: battery
    banks 2, 3 and 1. The owner's manual agrees: A and B are "located in the RAM", C-H are in the flash
    ROM and "can not be overwritten via STORE"; the RAM is battery-buffered (`virus_c_manual.pdf`).
  - STORE writes the 256 bytes of a program straight into the window. Traced: "Single-Program
    ...stored!" follows with no flash command. The flash's unlock sequences ($5555/$2AAA, in bank 7)
    belong to the OS update.
  - A new instance starts as a unit fresh from the factory might: battery banks 1-3 hold what the
    flash has at banks 9-11 (the multis, and copies of banks C and D), the rest is clear. **Assumption:**
    what Access put in RAM at the factory isn't known.
- The firmware's RAM test walks over the host port's addresses at boot, as it would on the unit.

## Ports

| Line | Use | Source |
|---|---|---|
| P1 bits 1-4 | LCD DB4-DB7 | MAME |
| P1 bit 5, 6, 7 | LCD E, R/W, RS | MAME |
| P3.0, P3.1 | MIDI in, out | MAME |
| P3.2 (external interrupt 0) | The host port's HREQ, low while the DSP has a word for the 80C515 | The firmware: it polls P3.2 before reading the port ($25AE) and enables interrupt 0 after a patch upload ($270E); see "Host port" |
| P3.3 | The host port's select, low around every access to it | The firmware ("Host port boot") |
| P3 bits 4-5 | Knob multiplexer | MAME |
| P4 | Key rows in; LED rows out | MAME |
| P5 bits 0-3 | Key and LED scan | MAME |

## Panel keys

Each matrix position pressed once on a fully booted unit, program A0, blank RAM (`viruscrun` with
`VC_SCRIPT`, 2026-11-09). "Edits" means the program name turned lower case, the firmware's mark for
an edited program. MAME's names (`acvirus.cpp`) are given where the screen confirms or contradicts them.

| Row/bit | Screen after the press | MAME's name |
|---|---|---|
| 0/0 | ARPEGGIATOR page | Arp: Edit (confirmed) |
| 0/1 | PHASER page | Effects: Edit (confirmed) |
| 0/2 | DELAY/REVERB page | Delay: Edit (confirmed) |
| 0/3 | edits | Arp: On |
| 0/4 | edits | Effects: Select |
| 0/5 | LFO 1 page | LFOs/Mod: Edit (confirmed) |
| 0/6 | LFO 1, "Osc 1 +40" while held | LFOs/Mod: Amount |
| 1/0 | edits | LFOs/Mod: Select |
| 1/1 | LFO 1 "Shape" | LFOs/Mod: Shape (confirmed) |
| 1/2 | an edit menu page (CLOCK or COMMON), its LED 4A0 lights | Edit |
| 1/3 | a system menu page (MIDI DUMP TX), its LED 4A1 lights; once RANDOM | Global: the SYSTEM / MULTI EDIT key |
| 1/4 | OSCILLATOR 1 page | Osc: Edit (confirmed) |
| 1/5 | edits | Osc: Sync |
| 1/6 | edits | Osc 1: Select |
| 2/0 | nothing visible | Random |
| 2/1 | "U N D O" | Undo (confirmed) |
| 2/2, 2/3 | edits | Transpose -, + |
| 2/4 | "[STORE] TO..." | Store (confirmed) |
| 2/5 | second line clears | Multi |
| 2/6 | "Off" while held | Single |
| 3/0 | edits | Osc 2: Select |
| 3/1 | "No more sounds in this category" | Part - |
| 3/2, 3/3 | nothing visible | Parameter <, Value - |
| 3/4 | A126 "- Init -" | Part + |
| 3/5 | B0: next bank | Parameter > |
| 3/6 | A1: next program | Value + (confirmed) |
| 4/0, 4/1 | edits | Osc 3: Select, Osc 3 On |
| 4/2 | SATURATION page | Filters: Edit (confirmed) |
| 4/3, 4/4 | edits | Filter 1 Mode, Filter 2 Mode |
| 4/5, 4/6 | nothing visible | Filter 1 Select, Filter 2 Select |
| rows 5-7 | nothing | unused |

Keys that change only LEDs show "nothing visible" here; the LEDs settle the rest.

How long a key must be held: browsing programs with VALUE+, 11 of 12 taps of 40 ms registered and every
tap from 60 ms. Inside the STORE dialog one 0.1 s tap in eight was missed when started 10 s later than
the first trial, and none at 0.15 s or 0.2 s. The firmware's key handling, not a gap in the scan (the
scan ran throughout, `VC_SCANLOG`).

**In SINGLE mode, PART - and PART + (3/1, 3/4) step through the programs of the selected category**,
the manual's category search: hold SINGLE and step PARAMETER to choose one (OS 6.6 lists them
alphabetically: Off, Acid, Arpeggiator, Bass, ...). With Bass chosen, PART + went A0, A2, A5, A9,
bank C's programs whose category 1 or 2 is Bass (2), and PART - back. A fresh memory has Off chosen,
which matches the programs with no category: only C126 "- Init -" and C127 "- START -" in bank C
(and their copies in A), hence A0 to A126, and from B2 to C126 then C127.

**VALUE - and VALUE + step from the panel's own copy of the program number** (external RAM $441B,
the value of whatever the display edits). Entering the program view refreshes it ($33E1 reads the
live value back); a MIDI program change while the view is up doesn't. So after A3 on the panel and
program change 7 over MIDI, VALUE + goes to A4, not A8; after EDIT and SINGLE it goes to A8. Taken as
the firmware's own behaviour: the code that loads the program is the same for both, and only the
panel's path writes $441B. **Not confirmed on a unit.** VALUE stops at 0 and 127; PARAMETER changes
the bank and keeps the number.

**At power-on the firmware reads the keys once** (code $5C98, 30 ms in, key row 0): ARP EDIT, DELAY
EDIT and EFFECTS SELECT held together (row 0 reads $0A in bits 0-4) fill the four battery RAM banks
with $FF, then the firmware reports "Illegal RAM !!! / Low Battery ???", starts again and refills
banks A and B from the flash: a reset of the RAM. Nothing else reads the keys before 5.5 s, so this
OS has none of the test modes MAME lists (those keys belong to the Virus A and B panel). The plugin
can't hold keys at power-on.

## LEDs and key scan

From the scan routine at bank 2 $A800 (`tools/dasm/dasm51.py`) and port traces (`viruscrun`,
`VC_PORTS`). One step of the scan every 2.3 ms:

1. Both LED latches are cleared (P4 bit 7 and P5 bit 3 pulsed with P4 bits 0-4 low), P4 bits 0-6 are
   set high as inputs, P5 bits 0-2 select the key row (0-4), and P4 is read: a key down reads 0.
2. P5 bits 0-2 go to 7 (no column). The next LED column's two bytes are put on P4: the first while
   P4 bit 7 is high (**latch A**), the second while P5 bit 3 is high (**latch B**). Each latch follows
   P4 bits 0-6 while its enable is high and holds them when it falls.
3. P5 bits 0-2 select the LED column (0-6), which then stays lit until the next step.

So the LEDs are 7 columns of two latches, bits 0-4 of each (70 positions); bits 5 and 6 of each latch
carry four steady lines from the firmware's flags at 29h.4-7, not yet identified. MAME's 16 x 8 model
with a pattern latched on P5 bit 3 doesn't match this. The emulation keeps each LED's lit share of its
column's time, so dimmed and pulsing LEDs come out as on the unit.

That share is taken over at least two passes of the scan (about 30 ms). Read once per audio block, it
went wrong: with 128-sample blocks each of the 23 lit LEDs read dark in some blocks, because its column
had not come round, so the panel flickered (`viruscrun`, `VC_LEDWIN=from,frames`; idle on A0, no LED
changes when read over 16 ms or more).

**The two LFO RATE LEDs are not in the scan.** The DSP drives them with PWM from its timers: timer 2
for LFO 1, timer 1 for LFO 2/3 (Gearmulator reads them there; `VC_TIMERS` logs both). The firmware sets
each timer to PWM mode with a load value of $FFDFF8, a period of 8199 counts, and moves the compare
value with the LFO. On A0 the LED is dark when no note plays and pulses to full with LFO 1 while one
does. As Gearmulator has it, the LED is lit while the pin is low. **Assumption:** the LEDs get the same
current as the scanned ones, which are lit one seventh of the time, so a PWM duty of 1/7 is drawn as
fully lit.

Which LED each position lights is in `panel.json` (`matrix` on each LED), found by stepping every LED
group through its states after boot and watching what changed (`viruscrun`, `VC_SCRIPT`): the four
LFO selects, the five shapes, LFO 1's six destinations, the six ASSIGN LEDs, the effect type, both
filters' modes and selects, the oscillator selects, SYNC, OSC 3 ON, ARP ON, MULTI/SINGLE, transpose
and the eight EDIT-type keys. The order runs through the columns in panel groups (column 0 latch A:
the ARP, effects and delay keys; then LFO 1's destinations, the LFO selects, LFO 2's and LFO 3's
destinations, the shapes, the ASSIGNs; column 4 onwards the centre keys, oscillators and filters).
**Placed by that order only, not seen to change:** LFO 2's and LFO 3's destinations (they light only
with a destination set) and BPM (4A2, always lit here). 6B2, 6B3 and 6B4 stay lit and have no LED
left for them. (An earlier note had 6B3 and 6B4 blinking five times a second as the RATE LEDs: that was
reads 0.1 ms apart catching the scan between columns.) Program A0 is transposed an octave down, so
its transpose LED is -1, not 0.

## Interrupts in use

Vectors with code in the fixed flash: external 0, timer 0, external 1, timer 1, serial, timer 2. The
A/D and external 2-6 vectors hold data, so those sources are unused here.

## Host port boot

The firmware sends a length, an address and that many words (81 words to `$100`); `dsp56kEmu`'s
`DspBoot` stores them and starts the DSP there, standing in for the DSP's bootstrap ROM, so no extra ROM
is needed. From then on the 80C515 talks to the DSP's own program. MIDI bytes go across one at a time in
the top byte of a word, with HF0 and HF1 both set.

**HREQ on P3.2.** The firmware keeps receive requests on (ICR $31: RREQ, HF1, HLEND), so HREQ goes low
whenever the DSP has a word for it, and the 80C515 reads what the DSP sends unasked (status words such
as $0000F5, $000001) in its external interrupt 0. A patch upload ($26A0) sends 90 words, disables that
interrupt, polls for the DSP's answer until it reads $F4F4F4, then enables it again. As on the HI08, the
DSP's word moves into the host's receive registers as soon as they are free, not when the 80C515 next
reads the port.

Without HREQ (P3.2 read high) the unasked words piled up, each upload read the one left from the
previous upload as its first answer, and with some timings the answers ran out of step: the firmware
then waited for $F4F4F4 for good, its keys dead while the LEDs ran on. Pressing VALUE+ every 0.4-0.55 s
at 48 kHz in 512-sample blocks hung it at about the 20th press, the same frame by frame on a bare board
(`viruscplugintest`, "VALUE+ steps through 30 programs").

**P3.3 selects the port.** The firmware drives P3.3 low around its accesses to $0400-$0407 and keeps
part 1's edit buffer in the RAM at the same addresses, which it reaches with P3.3 high. Counted over a
boot and the oracle replay's sysex run (18 s of sysex changes, a single dump and multi mode;
`viruscrun`): 49,120 reads and 150,366 writes there with P3.3 low, 30 reads and 27 writes with it
high. In STORE TO FLASH the programmer's copy goes there with P3.3 high too. With the port answering whatever P3.3 was, the firmware read part 1's first bytes
back as host port registers, and in multi mode part 1 went to the DSP with them wrong (the oracle
replay against Osirus). The golden renders moved with it: level within 0.01 dB, the residual -39 to
-45 dB.

**RESET resets the port's host side**, as the HI08's reset does: no byte order swapped (ICR 0), no
host command (CVR $32), the vector as at power-on (IVR $0F). The firmware relies on it after STORE TO
FLASH: it boots the DSP again without setting the byte order, which its other routines leave swapped
(ICR $31 has HLEND).

## Running the DSP

Each frame (`Machine::runFrame`) the DSP runs until the ESAI hands over the next stereo frame. The
engine's frame clock counts DSP instructions, `getCyclesPerSample()` (1450 at 136 MHz) per slot, two
slots a frame, and waits while the DSP hasn't taken an interrupt request. In a stretch with interrupts
masked, such as a patch change, a frame then comes several frame periods late and the ones behind it
back to back. So the DSP may run up to `kDspBudgetFrames` (16) frame periods for one frame.

With a budget of one frame period (2900 instructions), the DSP fell behind real time whenever it ran
late, and each frame it hadn't made repeated the last sample: 2 % of frames idle, 13 % under a six-note
chord on E73. Everything played flat by the share missed, and the repeats were audible grit. A4 on
C126 "- Init -" came out at 419.6 Hz, on E73 PowerStrnJ at 411 Hz. With 3 or 4 frame periods, 0.8 %
of frames still came late (stalls of 4 to 16 periods while E73 played); with 16, none
(`viruscrun`, `VC_MISSED=1`). Gearmulator's Osirus runs the same engine without a budget.

Now A4 on "- Init -" is 440.65 Hz here and 440.53 Hz on Osirus, and E73's chords match Osirus within
1 dB per third octave (`tools/compare/bands.py`). Before the ESAI sends its first frame, the budget is
one frame period, so boot isn't slower. CPU is unchanged; the worst 512-sample block went from 1.8 to
2.6 ms (`viruscbench time`), the DSP catching up after a stall inside one block.

**The idle loop is skipped where it can't matter.** Between blocks of frames the DSP program goes round
$2C087-$2C0A7, working out from r7 (which the audio interrupt moves) and Y:$2C082 and Y:$2C084 whether
the next block is due: 89 % of the DSP's instructions with one note on "- Init -", 64 % with an
eight-note pad (`viruscrun`, `VC_DSPPROF`). Once a pass found what it reads (r7, the two words, SR, OMR,
the stack) as the pass before did and no interrupt waits, the passes up to the next time the peripherals
run change nothing, and nothing else can come before then: no instruction in the loop touches a
peripheral, so they run only between the steps, which end at $2C089, and the 80C515 doesn't run during
the DSP's turn. The board counts those passes as run (the engine's `fastForward`, by one pass's
instructions and cycles) up to the first step at or past that time, where the run would have the
peripherals run (`Machine::idleStep`). Only with OS 6.6's code at those addresses (a hash of the 33
words). Bit-identical: the golden renders, and all 768 flash programs rendered with and without it
(`VIRUSC_NO_IDLE_SKIP=1`, `tools/compare/replay.py run --only ours`).

## Audio in and out

The ESAI's transmitters 0, 1 and 2 carry OUT 1, OUT 2 and OUT 3, left in slot 0 and right in slot 1.
Its receiver 0 carries the inputs, IN L in slot 0 and IN R in slot 1. Gearmulator maps them this way
(`DspSingle::processAudio`), and the firmware's own routing confirms it (`viruscplugintest`):

- SECOND OUTPUT Select (page C 45) at 5, OUT 2 L+R, with its balance (page B 58) at 127 puts a note on
  "- Init -" on transmitter 1 only. Its values are 0 for off, then the OUTPUT SELECT list.
- In MULTI mode (page C 122 = 1), part 0's OUTPUT SELECT (page C 41; 0 OUT 1 L, 1 OUT 1 L+R ... 7 OUT 3
  L+R ... 14 AUX 2 R) at 7 puts it on transmitter 2 only.
- INPUT Static (page A 101 = 2) on IN L+R (page A 102 = 1) plays a sine fed to receiver 0 through the
  filters. With nothing fed, its floor is about -98 dB.

The DSP reads its input `Machine::kInputAhead` (32) frames, 0.68 ms, ahead of the frame it sends. The
input converter and its delay aren't known.

**The DACs** are three Cirrus Logic CS4390s, one per transmitter: read off a repair shop's photos of
the board (Studio Repair's Virus C gallery; board "VIRUS MB 4.0"). The plugin puts each pair through one
(`core/dac.h`), from the datasheet (DS264F1):

- Their filter is the resampler's kernel at host rates from 48 kHz up: a Kaiser-windowed sinc, 25
  samples a side, beta 7.25, cut at Fs / 2. Against the datasheet's plots of the transition it is
  within 0.16 dB rms (-1.1 dB at 0.48 Fs, -2.9 at 0.49, -6.0 at 0.50, -11.0 at 0.51, -18.4 at 0.52);
  flat within 0.002 dB to 0.4535 Fs (21.26 kHz here), its stopband 74-80 dB down where the plots show
  72-90 dB, and 25 samples of delay, as specified. At 44.1 kHz the
  host's band ends below the DAC's, and the resampler keeps its own edge (-6 dB at 20.3 kHz).
- Their noise: 103 dB under a full-scale sine in 10 Hz-20 kHz (the dynamic range, typical, 24-bit),
  white at the DSP's rate, so 105.3 dB under it over the whole 23.4 kHz.
- Their auto-mute: after 8,192 frames of words all 0 or all 1 on both channels, silent until the data
  moves. **Assumption:** that the board has it on (the AUTO_MUTE pin low). Asleep, the DSP is held at
  silence and the noise runs on until the DACs mute; then the output is exactly zero.

`viruscrun` writes the DSP's samples as they are, for comparisons with Osirus; the plugin with
`VIRUSC_NO_DAC=1` does the same. Not modelled: the op-amp stage between the DACs and the jacks.

## The recompiler at run time

The engine compiles DSP code the first time it runs, on the thread running the DSP: the audio thread,
once booted. `tools/viruscbench` times every block over all 1024 factory programs (patch change, a
four-note chord, release):

- The deepest stack used is 54 KB during boot and 32 KB after it.
- The first note after boot was the worst block (8.9 ms), so `Machine::warmUp` plays a chord at the end
  of boot, unheard, and waits for its tail. This compiles through normal execution. Compiling blocks
  ahead from a list of addresses was rejected: what a block compiles to depends on the DSP's state
  when it first runs (interrupt mode, the loops known so far).
- What's left is the first use of code no earlier program used. See `research.md`, "CPU". With the
  warm-up, as the plugin boots (`viruscbench <rom> spikes 128 16 1`: 16 programs a bank, 2.67 ms
  blocks): 3 blocks over the deadline on the first pass, worst 8.4 ms (A3's patch change), then 4.5
  and 4.3 ms (A1); none on the second pass, worst 0.41 ms. Playing more programs unheard at boot would
  cover more of it, but the firmware keeps what it plays (the edit buffer, UNDO, the program shown),
  so the unit wouldn't start as it does; left as it is.
- Several instances share nothing: each has its own engine, recompiled code and board. Two on two
  threads at once render what one renders alone, sample for sample (`viruscplugintest`).

## Banks

CC 32 selects the bank, in LCD letters: A and B in battery RAM; C = flash bank 10, D = 11, E = 12,
F = 13, G = 14, H = 15 (found by the names shown). Program 69 of bank F is "Moon PadJS".

## MIDI out

At 31,250 baud, as the unit: knobs as their controllers (LFO RATE CC 67, OSC SHAPE CC 17), page B
keys as sysex (ARP ON: `F0 00 20 33 01 00 71 40 01 01 F7`), TRANSPOSE as CC 93. A program chosen on
the panel sends nothing (the manual doesn't say it should). The SYSTEM menu's MIDI DUMP TX, started
with STORE, sends a single dump of the edit buffer: `F0 00 20 33 01 00 10 00 7F`, 256 bytes, the
checksum by the manual's formula; on C9 the bytes are the flash's but for page A bytes 1 and 2,
which the firmware sets to 0 and the bank (C = 2), as it does in what it sends the DSP.

## Store to flash

The SYSTEM menu's STORE TO FLASH copies a RAM bank into a flash bank ("[STORE] Bnk A>C", VALUE picks
the target; then "New ROM-Bank? [VAL+] execute!"). The flash is an AMD Am29F040B in eight 64 KB
sectors, each two banks, so the firmware rewrites a whole sector (the code at bank 0 $5B70):

1. It sends the DSP command $F456: the DSP sets OMR to $438E (boot mode E: host port, 8051 bus),
   executes RESET and jumps to its bootstrap ROM at P:$FF0000.
2. It gives the ROM a length of 0 and the address $1000xx, xx the sector's first bank with bit 7 set
   ($8A for C and D). The ROM loads nothing (a DO loop with a count of 0 doesn't run) and jumps there.
3. It sends the sector a byte a word, `10 00 yy`: for each bank, its marker byte, then for each 256-byte
   page its number (0-$7F) and its bytes, from the RAM bank for the target and from the flash for the
   other; then $FF.
4. It copies its flash programmer (bank 7, $E000-$EFFF, "FlashProgramming AMD 29F040 (c) Joerg
   Meissner 1999") over RAM $0000-$0FFF, turns the interrupts off and jumps to $0040, `CLR P1.0`.
   **P1.0 low swaps code and data below $8000:** instructions come from the RAM, data reads and writes
   go to the flash (above $8000, to the bank P5 bits 4-7 select). The next instruction, at $0042,
   comes from the copy.
5. The programmer ("WAIT ! DO NOT TOUCH ME") asks the DSP for the bytes back: $000000 to start over,
   then a word whose last byte is $20 for each byte, read from receive register 5. A first pass finds
   the sectors to erase (a marker's bits 1-3), erases them ($AA at $5555, $55 at $2AAA, $80, again,
   then $30 at the sector), a second programs them ($A0 then the byte, then polls the bank's last byte
   for DQ7). It shows "programming 0A", "0B", then "programming OK!" with three error counts, sets
   $7B to $BA and jumps to $0040 in the copy, `SETB P1.0`: back to the flash, and the firmware starts
   again from $0042. Its first word to the DSP, $606060, ends the program holding the sector; then it
   initialises its global memory and boots the DSP as at power-on.

What the DSP runs at P:$1000xx isn't in what this board has: the DSP program loaded at power-on has
audio code at P:$8A, and its address attributes (AAR0 $000539, AAR1 $030839) map external memory at
$000000-$07FFFF and $030000-$03FFFF, not $100000. The board stands in for it from what the firmware
does with it (`Machine::serverWord`): it keeps the bytes, starts over on $000000, answers each $20 with
the next byte, and stops at $606060, going back to the bootstrap ROM through RESET as $F456 did. The rest is the hardware as the firmware uses it: P1.0's swap, the
flash's commands (`Machine::flashWrite`; a program answers one read with its status, as the chip
does while it works, because the firmware polls the bank's last byte; erases finish at once), and the
DSP's jump to its bootstrap ROM, which the engine doesn't have: the recompiler stops past the end of P
memory and the board waits for a program over the host port (`Machine::dspToBootRom`, `bootWord`).
Before that, the recompiler ran through empty memory until the stack overflowed and took the host with
it. The programmer's copy at $0400-$0407 is RAM: the firmware copies it with P3.3 high.

Bank A over E: "programming OK! 00 00 00", E0 is then "AutoBendBC", F (the same sector) as it was.
The plugin keeps the written sectors in the session, over the ROM file, which isn't changed.

## Sleep

When the Virus is idle, the 80C515 doesn't touch the host port at all, not even for LFOs (traced
over 5 s on "Moon PadJS"). So sleep holds the DSP only: the 80C515, the LCD and the panel run on,
and the 80C515's first access to the host port wakes the DSP, as do the inputs once they are heard
(over -120 dB). Asleep costs 1.2 % of a core (M5 Max).

A MIDI note sent at the same moment as a program change is lost while the firmware uploads the patch.
