# TR-808 CPU board and firmware: I/O map, RAM format, timing

Sources: *Roland TR-808 Service Notes* (pages 3 and 8: the uPD650C pin table, the CPU board
schematic with its switch-matrix diodes, Figures 2 and 6; `private/docs/tr808-service-notes.pdf`,
page index in `private/docs/service-notes-index.md`); MAME's `roland_tr808.cpp` (a stub: CPU,
clock and ROM only, "TODO: everything"; copy in `private/reference/mame/`); and runs of the real
ROM in `core/machine.cpp` scripted with `tools/tr808seq`, checked against the owner's manual
(`private/docs/operation-digest.md`). Everything marked *verified* below was confirmed by the
firmware doing what the manual says with that wiring.

## Hardware summary

| Item | Value | Source |
|---|---|---|
| CPU | NEC uPD650C-085 (IC4): CMOS uCOM-43, 2000×8 mask ROM, 96×4 RAM | SN p.3, MAME |
| CPU clock | IFT-1 LC oscillator, 2 µs at TP1: 500 kHz, 4 clocks a machine cycle, 125,000 cycles/s | SN p.8 adjustment, MAME |
| Interrupt | Schmitt RC oscillator, 1.9 ms at TP2 (TM-1), a short pulse on /INT | SN p.8, p.14; MAME |
| ROM | `d650c-085.ic4`, 0x800 bytes, CRC32 `06f0c405`, SHA1 `eddac7af6396c3f220914d9cad2c9e398872b034` | MAME |
| Pattern RAM | 4 × uPD444C (IC7-IC10), 1024×4 static CMOS each, battery-backed; IC5 decodes the chip select | SN p.8 |
| Tempo clock | Separate anti-log oscillator (Q13/Q14, C10, IC1 Schmitt) halved by IC2a into a 24 ppqn clock at TP3 | SN p.8, Fig. 8 |

The CPU core is MAME's uCOM-4 core ported to C++ (`core/ucom43.*`, BSD-3-Clause, hap), with a
64-entry PC/accumulator history for the tools.

## Port map

PH is active low through Q23-Q26: a 0 bit selects a column of the matrix. With every PH bit high
no column is selected, and Q18 opens the status buffer and gate (IC3, Q8-Q11) onto PA.

| Port | Dir | Function |
|---|---|---|
| PA0-3 | in | Switch codes of the selected column (below); with no column, the status lines |
| PB0-3 | in | Step keys of the selected column: key k is column (k-1)/4, row (k-1)%4 |
| PC0-3 | I/O | RAM data bus |
| PD0-3 | out | RAM A4-A7, **and instrument data CH, OH, CY, CB** |
| PE0-1 | out | RAM A8-A9, **and instrument data CP, RS**; also the LED drivers' inputs (below) |
| PE2-3 | out | RAM chip select through IC5, **and instrument data HT, MT** |
| PF0-3 | out | RAM A0-A3, **and instrument data LT, SD, BD, ACCENT** |
| PG0-3 | out | Step LED rows (with PH as columns) |
| PH0-3 | out | Matrix column scan; all high selects the status gate |
| PI0 | out | RAM /WE (low writes). SW1b passes it only in the writing modes |
| PI1 | out | RAM chip enable (high) |
| PI2 | out | The step pulse into IC6, the common trig's 1 ms monostable |

### Status lines (PA, no column selected) — verified

| Bit | Line | From |
|---|---|---|
| PA0 | RUN | START/STOP's flip-flop (IC2b), or DIN SYNC pin 1 |
| PA1 | FILL IN | TAP, or DIN pin 5. The interrupt routine latches it into flag F3, so a short tap is caught |
| PA2 | RESET & START | DIN pin 4 |
| PA3 | TEMPO CLOCK | The internal oscillator's T flip-flop, or DIN pin 3 (24 ppqn) |

### Switch codes (PA, by column) — verified

| Column | Switch | Codes |
|---|---|---|
| PH0 | MODE | PATTERN CLEAR 5, 1st PART 6, 2nd PART 7, MANUAL PLAY 1, PLAY 3, COMPOSE 3. PLAY and COMPOSE share a code: SW1b (write protect) tells them apart, letting /WE through in PATTERN CLEAR, 1st, 2nd and COMPOSE only |
| PH0 | CLEAR key | PA3 |
| PH1 | PRE-SCALE (PA0-1) | positions 1-4 read 3, 1, 2, 0; the firmware's table at 0x478 turns them into 8, 4, 6, 3 tempo clocks a step |
| PH1 | BASIC VARIATION (PA2-3) | A 0, A&B 8, B 4 |
| PH2 | INSTRUMENT-SELECT / RHYTHM TRACK | position k (1 = AC ... 12 = CH) reads 12 - k (verified by writing each instrument and hearing every voice) |
| PH3 | AUTO FILL IN (PA0-2) | MANUAL 0, 16 → 5, 12 → 4, 8 → 3, 4 → 2, 2 → 1 |
| PH3 | I/F VARIATION B | PA3 |

### Instrument data and the step pulse

At each step the firmware puts the step's instrument nibbles on PD/PE/PF, raises PI2 for about
16 µs and holds the data for about 1 ms (it waits with the timer, `STM $81`) while IC6 stretches
the pulse to 1 ms. The trigger circuit ANDs the common trig with each voice's line:

| Port | Bit 0 | Bit 1 | Bit 2 | Bit 3 |
|---|---|---|---|---|
| PE | CP | RS | HT | MT |
| PD | CH | OH | CY | CB |
| PF | LT | SD | BD | AC |

Between steps the same ports carry RAM addresses, so the voice board model only looks at them
while the monostable runs (`plugin/PluginProcessor.cpp`, `renderBoard`).

### LEDs

The 16 step LEDs are multiplexed (PG rows × PH columns); `Machine::updateLeds` averages them over
~30 ms. The four others hang off PE through RC-held drivers (2SC945s, each LED in a collector):
PE1 through 10k into 10 µF (C5) drives Q4 → BASIC VARIATION B, and Q5 inverts it → A; PE0 through
10k into 33 µF (C4) drives Q3 → 2ND PART, and Q2 inverts it → 1ST PART. The firmware holds PE0 at
the part playing (low = 1st, which is also where 1st PART mode writes) and PE1 at the variation
(low = A), high 99 % of the time when set; the only dips are the reads for the step LEDs in the
writing modes (~6 %), which the RCs ride over.

The p.8 schematic draws D78 (1ST PART) in Q3's collector; the PCB (SN p.10 component side, p.11
foil side) puts Q3's LED, with R12, in the lower hole, which is 2ND PART (the board's PRE-SCALE,
BASIC VARIATION and START/STOP sit around the LEDs as on the panel). So the schematic swaps D78
and D79's captions; the model follows the board, and the 1ST PART LED lights while the 1st part
plays. The variation pair is the same on both.

## RAM format

The RAM address is the firmware's own: chip = PE2-3, A8-A9 = PE0-1, A4-A7 = PD, A0-A3 = PF.

| Chip (PE2-3) | Address | Content |
|---|---|---|
| 0 (IC8) | `(part | var<<1)<<8 | pattern<<4 | step` | CH, OH, CY, CB (bits 0-3) |
| 1 (IC10) | same | CP, RS, HT, MT |
| 2 (IC7) | same | LT, SD, BD, AC |
| 3 (IC9) | `0x000 + pattern` | 1st part's length, mod 16 (16 stored as 0) |
| 3 | `0x100 + pattern` | 2nd part's length, mod 16 |
| 3 | `0x200 + pattern` | tempo clocks a step (8, 4, 6, 3), bit 3 set while the 2nd part is empty |
| 3 | elsewhere | the rhythm tracks (compose) |

`pattern` is 0-15: BASIC RHYTHM 1-12, then INTRO/FILL IN 1-4. `part` 0/1 is 1st/2nd, `var` 0/1
is A/B; A and B share their lengths and pre-scale, as on the panel. `Machine::writePattern`
writes this format directly (used by the tools and tests). The CPU's own RAM keeps the current
switch codes at 0x10-0x13 and a per-pattern pre-scale table at 0x40-0x4F.

The whole 4 KB (4096 nibbles) is what a session saves (`Machine::saveState`, base64 in the
plugin state): the battery-backed RAM, nothing else. A new instance starts with the factory
rhythms (`Machine::writeFactoryRhythms`, `docs/sequencer.md`), and so does the host's State:
Reset.

## Timing

- Machine cycles run at 125 kHz against the host sample clock (`Machine::tick`), the interrupt
  every 1.9 ms.
- The firmware polls the tempo clock's status bit from its main loop and interrupt, so a step
  lands up to ~2 ms after the clock edge that makes it: steps jitter by about 2 ms, as on the
  hardware.
- The firmware reads the step keys once per step boundary and clears its key latches each step:
  a press must be held for about a step to register. `tr808seq` holds keys for 150 ms by
  default. A real 808 behaves the same way (it is the firmware), so the plugin doesn't
  stretch presses.
- DIN sync: the firmware steps on the first rising clock edge after RUN goes high. The plugin
  feeds the host's transport as RUN and a 24 ppqn clock (SYNC on); its clock edges lag the host
  grid by 2 ms, so steps land 4.5-5 ms after the grid (the first onset at 5.0 ms). Checked in
  `tr808plugintest` ("sync on grid").

## Verification runs

With `tr808seq` scripts, each against the owner's manual procedure:

- PATTERN CLEAR → CLEAR clears a rhythm; 1st PART, START, setting PRE-SCALE, then CLEAR latches
  the pre-scale; INSTRUMENT-SELECT and step keys write the voices; CLEAR held with a step key sets
  a part's length; 2nd PART adds a second bar.
- PRE-SCALE 1-4 play 8, 4, 6 and 3 clocks a step: 3, 6, 4 and 8 steps a beat at 24 ppqn
  (8th-note triplets, 16th-note triplets, 16ths, 32nds).
- BASIC VARIATION A, A&B (alternating each bar) and B, with the A/B LEDs following.
- COMPOSE writes rhythm numbers per bar in real time and PLAY plays the track; TAP gives a fill
  in the next bar; AUTO FILL IN inserts INTRO/FILL IN every 2-16 bars; I/F VARIATION picks the
  fill's variation.
- Selecting a rhythm during the bar's first beat starts it from the second beat (the manual's
  rule).
- `TR808_RAMTRACE`, `TR808_RAMHIST` and `TR808_STATUS` (environment variables) trace RAM writes,
  the instructions before them, and remap the status bits; they were how the map above was
  found.
