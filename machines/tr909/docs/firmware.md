# TR-909 CPU board and firmware: I/O map, RAM format, MIDI, timing

Sources: *Roland TR-909 Service Notes* (1st ed., 15 Jun 1984; `private/docs/tr909-service-notes.pdf`,
page images and index in `private/docs/schematic/`; cited "SN p.n"), the *Owner's Manual*
(`private/docs/tr909-owners-manual.pdf`, printed page numbers; "OM p.n"), MAME's `roland_tr909.cpp`
(AJR; a skeleton: memory map and key matrix, "TODO" the rest) and its uPD7810 core
(`private/reference/mame/`), NEC's 87AD (uPD78C18) user's manual for the serial and interrupt
timing the uPD7810 shares (`private/docs/papers/nec/`), and runs of the real firmware v4.0
(`tr909_v4.ic609`, CRC32 a9b45f30) and v1.0 (`tr909_v1.ic609`, bfb9682b) on `core/machine.cpp`,
scripted with `tools/tr909seq` and checked against the owner's manual (`docs/sequencer.md`).
`tools/tr909seq/tests.py` re-runs those checks; `tools/compare/sequencer_timing.py` makes the
timing numbers below. Addresses are the firmware's; "VV:xx" is the CPU's internal RAM at FFxx
(the firmware keeps V = FF).

## Hardware summary

| Item | Value | Source |
|---|---|---|
| CPU | IC604 uPD7811G-033-036, MODE0 = MODE1 = 1 (R630, R631 to +5 V): the internal ROM is off, 64 KB external memory, i.e. a uPD7810 | SN p.5, p.8 |
| Clock | 12.00 MHz crystal; a CPU state is 3 clocks (250 ns), 4 M states/s | SN p.3, p.8 |
| Program ROM | IC609, 8 KB at 0000 (M5M2764 or 2364 mask ROM) | SN p.7, p.8 |
| RAM | IC608 TC5565PL-15, 8 KB at 2000, backed by two AA cells | SN p.8, OM p.4 |
| Panel | IC610 LS244 reads the key returns at 4000; IC602 M54516 (column sources) and IC603 M54562 (row sinks) drive the LEDs; IC601 4511 and D636 LB-603VK the display | SN p.8 |
| Voice board latches | IC2-IC9 (4174) the eight accent DACs (RM0621 ladders), IC1 and IC10 the trigger latches, decoded by IC612/IC613 (LS138) | SN p.5, p.11 |
| MIDI | IN through IC605 (PC900 opto) to RxD; OUT from TxD through IC607 and Q604 to both OUT jacks | SN p.8, p.12 |

The core is MAME's uPD7810 (`core/upd7810/`, BSD-3-Clause, Juergen Buchmueller), ported by
`tools/port_upd7810.py` with three fixes (Emulation notes, below).

## Memory map

| Address | What |
|---|---|
| 0000-1FFF | firmware ROM |
| 2000-2FFF, 3000-3FFF | battery RAM: Bank I, Bank II (layout below) |
| 4000 (read, mirrored to 5FFF) | key returns of the rows selected on PA0-4 |
| 4001-4008 (write, low nibble decoded) | DAC1-DAC8, the 6-bit code in D1-D6 |
| 4009, 400A (write) | trigger latch 1, 2 |
| 6000-7FFF | cartridge (M-64C: 6000 Bank I, 7000 Bank II, the same layout); reads FFh with none |
| FF00-FFFF | CPU internal RAM (MM = 08); the stack from FFFF down |

## CPU set-up at reset (02CD)

- V = FF, SP = FFFF, MM = 08; PA, PB outputs (MA = MB = 0), PA = PB = PF = 0; internal RAM
  cleared; 2FFE/2FFF and 3FFE/3FFF cleared; then a 0.6 s delay (1509) before anything else.
- MC = EE, MCC = 1B: PC0 TxD, PC1 RxD, PC3 INT2/TI, PC4 TO in control mode; PC2, PC5, PC6, PC7
  port inputs.
- SML = 4E, SMH = 0E: asynchronous, 8 data bits, no parity, 1 stop, x16 of the internal clock
  (12 MHz / 24): 31250 baud; transmit and receive on.
- TM0 = D0, TM1 = 02, TMM = 61: timer 0 counts 1 us, timer 1 counts timer 0's matches, TO
  toggles on timer 1: a 416 us half period, 1202 Hz on PC4 (TAPE SYNC out).
- ETMM = 0C: ECNT counts 1 us and clears when it matches ETM1 (the tempo tick, INTE1).
- ANM = 00: the A/D converter scans AN0-AN3 continuously, 48 us (192 states) a conversion,
  INTAD after every four (192 us).
- MKH = 04, MKL = BF: INTE1, INTAD and INTSR enabled; INTST is unmasked while there is MIDI to
  send, INT1 in DIN sync, INT2 for the tape.
- MIDI receive channel VV:41 = 09 (channel 10, omni on), transmit VV:3C = 0A (channel 11);
  sync mode INT. None of this is kept in the RAM (OM p.34, p.38).
- If AN1 < 40h (a cartridge) and 7FFE/7FFF read 96 69 and 6000 is not writable (a ROM), it
  calls 6000 (a test cartridge, presumably).
- Writes 4007 = 0, 4008 = 0, 400A = 3C: the HH, crash and ride trigger lines go high once, for
  0.4 ms, at power-up.
- TRACK 1 + PATTERN 1 held (VV:22 = 22h at 037F) calls the factory loader (v4.0: 1DF4, a RET;
  v1.0: 1F2E). TRACK 1 held alone lights a version LED while it is held (v4.0: step 4).

## Ports

| Pin | Use | Board (SN p.8, p.12) |
|---|---|---|
| PA0-4 | key rows (one-hot, high = selected) | through D637-D659 to the keys |
| PA0-7 | LED rows, and PA0-3 the 4511's BCD | IC603 sinks |
| PB0-4 | LED columns; PB0-2 also the display digits (right, middle, left) | IC602 sources |
| PB5 | 4511 LE: transparent while low, latched while high | IC601 pin 5 |
| PB6-7 | tape data out, a 2-bit code into the R318-R322 ladder (SAVE/OUT) | voicing board |
| PC0 / PC1 | MIDI TxD / RxD | IC607, Q604 / IC605 |
| PC2 | foot switch: high = pressed (R633 47K pull-up; the jack's switch grounds it with no plug) | JK-2 |
| PC3 | INT2: tape in (load, verify, tape sync), falling edges | IC41 |
| PC4 | TO: TAPE SYNC out (1202 / 2404 Hz FSK) | voicing board |
| PC5 | DIN START/STOP, inverted by Q603 (R628 pull-up: high with no cable) | JK-2 pin 1 |
| PC6 | DIN CONTINUE, inverted by Q602 (R624: high with no cable) | JK-2 pin 5 |
| PC7 | tied to ground | |
| INT1 | DIN TEMPO clock through the emitter follower Q601; rising edges | JK-2 pin 3 |
| AN0 | TEMPO, VR601 50K(B) | |
| AN1 | cartridge SENS: 47K pull-up on F5 pin 3, grounded by an M-64C (SN p.13); the firmware takes CR1 < 40h as a cartridge | |
| AN3 | TOTAL ACCENT (VR1 on the voicing board) | |
| AN2, AN4-AN7 | tied to AVss | |

### Key matrix — verified

PA0-4 drive the rows through diodes; the returns are pulled down (RA600), so a pressed key on
the selected row reads 1. (MAME's skeleton reads it active low, which the firmware's one-hot-high
scan contradicts.) Bit = return line, left to right on SN p.8:

| Row (PA bit) | bit 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| 0 | step 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| 1 | step 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
| 2 | SHIFT | SCALE | (PC2, foot switch) | INST SELECT | SHUFFLE/FLAM | LAST STEP | STOP/CONT | START |
| 3 | CLEAR | TRACK 1 | TRACK 2 | TRACK 3 | TRACK 4 | PATTERN 1 | PATTERN 2 | PATTERN 3 |
| 4 | ENTER/ACCENT | TAPE SYNC | CYCLE | AVAILABLE MEAS | FWD | BACK | TEMPO | EXT INST |

SHIFT functions (OM p.3): steps 1-10 are digits 1-9, 0; 11 COPY, 12 INS, 13 DEL, 14 SAVE, 15
VERIFY, 16 LOAD; TRACK = track write, PATTERN = pattern write; TEMPO = STEP (step write), BACK =
TAP (tap write), FWD = BANK I, AVAILABLE MEAS = BANK II, CYCLE = LAST MEAS, TAPE SYNC = TEMPO MODE.

### LEDs and display — verified

PB0-4 select a column (one per scan, so each is lit 1 scan in 5), PA0-7 the rows:

| Column | PA0 | PA1 | PA2 | PA3 | PA4 | PA5 | PA6 | PA7 | digit |
|---|---|---|---|---|---|---|---|---|---|
| PB0 | step 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | right |
| PB1 | step 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | middle |
| PB2 | - | TRACK 1 | TRACK 2 | TRACK 3 | TRACK 4 | PATTERN 1 | PATTERN 2 | PATTERN 3 | left |
| PB3 | ENTER | EXT INST | TEMPO | BACK | FWD | CYCLE | AVAILABLE MEAS | TAPE SYNC | |
| PB4 | SCALE (D635) | SCALE (D634) | SCALE (D633) | SCALE (D632) | | | | | |

The scale LEDs light PB4 bit 0 for 16ths, bit 1 for 32nds, bit 2 for 8th triplets, bit 3 for
16th triplets. A "dim" step LED (a voice at normal level, OM p.25) is lit on about one column
pass in four, a bright one on every pass; blinking LEDs (the pattern-write group, the selected
pattern number) about half the time. `tr909seq ledduty` measures it.

The scan writes the digit's BCD with PB5 low, raises PB5, then puts the LED row data on PA and
raises the column. Digits come from VV:2B (left), VV:28, VV:25 (right); 0F is blank.

## Interrupts

| Vector | Source | Handler |
|---|---|---|
| 0000 | reset | 02CD |
| 0010 | INT1 (DIN clock) / INT2 (tape in) | DIN sync (VV:63 bit 1): 00BC, sends F8 and calls 17F8; otherwise 00C5, the tape FSK half-period decoder (ECNT free-running) |
| 0018 | INTE1 (ECNT = ETM1: the 48 ppqn tempo tick) | 00E6: INT sync: one tick (12A8), an F8 every second tick, ETM1 reloaded from VV:44/45; MIDI/DIN sync: the extrapolated in-between tick |
| 0020 | INTAD (every 192 us) | VV:5A counts down from 9; on the 10th, the panel/voice scan (012B) |
| 0028 | INTSR / INTST | 162F: MIDI receive (with INTST unmasked, SKIT FSR tells them apart); 15A7 sends the next byte |

## The panel/voice scan (012B)

Every 10th A/D interrupt, with interrupts off until 01A9, then with INTAD masked and the others
allowed (tempo and MIDI interrupts nest in it):

1. Keys. Each scan reads row 0 and row 1 (steps); it alternates between rows 2 and 3 and row 4
   (VV:64 bit 7). Row 2 is ORed with PC2 (the foot switch) at bit 2. States and edges: VV:17,
   VV:1F (steps 1-8, 9-16), FF20/FF21 (row 2), FF22 (row 3), VV:23/VV:24 (row 4).
2. One LED column and digit (the phase in VV:6A).
3. Triggers: the scans alternate between two groups (the same VV:64 bit 7):
   - group A (the scans that read rows 2 and 3): BD, SD, LT, MT, from VV:0D (the steps) and
     VV:0C (flam second notes, released when the flam counter VV:5C is 0);
   - group B: HT, RS, HC, HH, crash, ride, from VV:0F and VV:0E (HT's flam note, released when
     VV:5C, decremented on each group-B scan, goes below 0).
   The accent codes for the triggered voices go into the image FF00-FF07, the trigger bits into
   FF08/FF09.
4. `DI`, `BLOCK` copies FF00-FF09 to 4001-400A (DAC1-8, then the two trigger latches, 3.25 us a
   byte); then FF08 = 0, FF09 &= 08 (the closed/open hi-hat bit is a held level). So a trigger is
   high from one scan's BLOCK to the next one's, and a DAC keeps its last code.
5. INTAD is unmasked again (`ANI MKH,FE; EI; RETI`).

The scan takes 300-600 us. An A/D interrupt that comes while INTAD is masked waits; a second one
in that time is lost. So the period is 10 x 192 us = 1.92 ms or, when the scan ran past the
second one, 11 x 192 = 2.11 ms (Timing, below).

## The voice board latches

| Instrument | MIDI notes in | Trigger | DAC |
|---|---|---|---|
| BD | 35, 36 | latch 1 bit 1 (02) | DAC1 |
| SD | 38, 40 | latch 1 bit 2 (04) | DAC2 |
| LT | 41, 43 | latch 1 bit 3 (08) | DAC3 |
| MT | 45, 47 | latch 1 bit 4 (10) | DAC4 |
| HT | 48, 50 | latch 1 bit 5 (20) | DAC5 |
| RS | 37 | latch 2 bit 1 (02) | DAC6, shared with HC |
| HC | 39 | latch 2 bit 6 (40) | DAC6 |
| CH | 42, 44 | latch 2 bit 2 (04) with bit 3 (08) high | DAC7 |
| OH | 46 | latch 2 bit 2 (04) with bit 3 low | DAC7 |
| crash | 49 | latch 2 bit 4 (10) | DAC8, shared with ride |
| ride | 51 | latch 2 bit 5 (20) | DAC8 |

Latch 2 bit 3 is a level held between triggers: 1 after a CH, 0 after an OH (the hi-hat's
decay select). Latch 1 bits 0, 6, 7 and latch 2 bits 0, 7 are unused. A DAC byte is the 6-bit
code shifted left by one. Two voices sharing a DAC (RS/HC, crash/ride) triggered in one scan
get the code written last.

### Accent codes — verified

At each step the firmware takes TA = (AN3 code) >> 3 (0-31: TOTAL ACCENT has 32 positions) if
the step carries TOTAL ACCENT, else 0, and gives each triggered voice the code
`table[(base + TA) >> 1] >> 1`, with table 1F80 (BD-HT, RS, HC; an expanding curve) or 1FC0
(HH, crash, ride; linear). Bases: normal 32h and accent 50h for BD, SD, the toms and CH; RS
and HC 50h; OH, crash and ride 40h. The codes the voice board gets (`tests.py`, "accent codes"):

| Voice | normal | accent | normal + full TOTAL ACCENT | accent + full TOTAL ACCENT |
|---|---|---|---|---|
| BD, SD, LT, MT, HT | 10 | 21 | 21 | 47 |
| CH | 25 | 40 | 40 | 55 |
| RS, HC | 21 | - | 47 | - |
| OH, crash, ride | 32 | - | 47 | - |

So "normal + full TOTAL ACCENT" equals "accent" for the analog voices, as the manual warns (OM
p.25); the sequencer never reaches code 63. At half TOTAL ACCENT (TA = 15): 14 and 33.
A flam's grace note gets the normal code and its main note the accent code (OM p.26).
The guide (CYCLE/GUIDE in tap write) is an RS trigger with DAC6 byte 2D (code 22) on the bar's
first beat and 0F (code 7) on the others.

MIDI note-on velocity v sets the code `table[v >> 1] >> 1` directly (the same two tables; TOTAL
ACCENT plays no part): BD-HT, RS, HC v 1 -> 2, 32 -> 6, 64 -> 14, 84 -> 24, 86 -> 20, 88 -> 27,
100 -> 37, 127 -> 63 (table 1F80 dips at entry 43: velocities 86-87 play quieter than 84-85);
HH, crash, ride about v / 2 (1 -> 1, 64 -> 32, 100 -> 50, 127 -> 63). The same in v1.0 and v4.0.

## RAM layout — verified

A bank is 4 KB (Bank I at 2000, Bank II at 3000; a cartridge's at 6000 and 7000). VV:33 holds
the current bank's page (20, 30, 60, 70) and its flags. With g = pattern group 0-2, p = pattern
0-15, s = step 0-15, t = track 0-3, offsets in the bank:

| Offset | Contents |
|---|---|
| 000 + 400g + 10p + s | BD bits 0-1, SD bits 2-3, LT bits 4-5, MT bits 6-7 |
| 100 + 400g + 10p + s | HT bits 0-1, RS bit 2, HC bit 3, hi-hat bits 4-5, crash bit 6, ride bit 7 |
| 200 + 400g + 10p + s | external instruments 1-8 (keys 1-8), one bit each |
| 300 + 400g + 10p + s | external instruments 9-16 |
| C00 + 100g + 10p + s | bit 0 TOTAL ACCENT, bit 1 the external instruments' accent; bits 2-7 track data (below) |
| C00-F7F | the tracks' measures, one byte each (896: OM p.15): bits 7-4 pattern number - 1, bits 3-2 group - 1. They share the bytes with the accent bits above |
| F80 + 8p + 2g | pattern header, 2 bytes: byte 0 bit 7 chained to the next pattern, bits 5-4 scale (0 16th, 1 32nd, 2 8th triplet, 3 16th triplet), bits 3-0 last step - 1; byte 1 bits 6-4 flam (key - 9), bits 2-0 shuffle (key - 1). CLEAR sets 0F 40 |
| F86 + 10t | track t+1's end: the bank offset of its last measure (16 bits, low byte first). The tracks follow each other from C00; an empty track holds the previous track's end (0BFF before track 1) |
| F8E + 10t | track t+1's tempo memory: the filtered TEMPO A/D value (VV:30) |
| FE6/FE7 | the current measure's address (2C00 = measure 1 of Bank I) |
| FEF | 5A (Bank I) or A5 (Bank II) while a track edit is under way; at power-on the firmware checks and repairs the tracks (0395, 108C) |

A voice's two bits: the higher one normal, the lower one accent, both a flam (BD 02/01/03,
SD 08/04/0C, ...); the hi-hat 20 CH, 10 CH accent, 30 OH. The step routine reads TOTAL ACCENT
live, so the RAM holds no levels. The whole 8 KB is the machine's state (`Machine::saveState`);
nothing else survives a power cycle.

## Factory patterns

"The TR-909 contains factory-rhythm patterns in BANK I, TRACK 1 under as-delivered condition";
holding TRACK 1 and PATTERN 1 while turning the unit on reloads them (SN p.7). The owner's manual
calls it "simple demonstration data ... (Bank I, Track 1, Rhythm Patterns 1 to 8)" (OM p.7).

- v1.0's loader (1F2E) clears Bank I, writes the default header 0F 40 into every pattern, and
  copies 3 x 128 bytes from the ROM into 2000, 2100 and 2C00 (patterns 1-8 of group 1: voices,
  TOTAL ACCENT, and the measure bytes) and 2 x 8 pairs into the headers (patterns 1-8's headers;
  the track pointers and tempo memories). The result: patterns 1-8 of group 1, track 1 of 65
  measures starting on pattern 5, with a tempo memory (TEMPO + TRACK 1 recalls 110 BPM); tracks
  2-4 and Bank II empty. 342 non-zero bytes.
- v4.0 dropped the data (FFh) and the loader (1DF4 is a RET), but not the format: v4.0 powers up
  with that memory unchanged and plays track 1 with the same voices and codes as v1.0, hit for
  hit, the timing within the scan jitter (`tr909factory --v4`: 1599 hits, 0 level mismatches,
  times within 5.6 ms).
- `core/factory.h`, `factoryRam()`: runs the user's v1.0 ROM on a `Machine` with the two keys
  held from power-on, as the service notes say, and returns the 8 KB it leaves; no ROM data is in
  the repository. `tools/factory` (`tr909factory --v1 ROM [--out FILE] [--v4 ROM]`) writes it and
  checks it on v4.0. Without a v1.0 ROM there are no factory patterns (and none are printed in the
  owner's manual: its pattern sheets, OM p.40-41, are blank).
- The SN's second procedure, "RE-LOADING BANKs I, II, TRACK 4", writes a measure into track 4
  of each bank to dodge a bug of the Group A (unlabelled) ROMs; v1.0 and v4.0 don't have it.

## MIDI — verified

**In** (31250 baud on RxD; the receiver hands a byte over at the stop bit's sample):

- Note-on 9n kk vv, vv > 0, on the receive channel: notes 35-51 play the voices of the table
  above, at the velocity's code; the trigger goes out on the next scan of either group. Other
  notes, note-off and velocity 0 are ignored.
- Channel: omni at power-on (basic channel 10). SHUFFLE/FLAM + step key n (track play mode, OM
  p.38) sets the receive channel n and omni off. Mode messages on the basic channel: 7C omni
  off, 7D omni on, 7E mono (M channels from the basic one: 7E 03 on channel 3 takes 3, 4, 5),
  7F poly.
- Start FA (the sequencer starts on the next F8 in MIDI sync; at once in INT), continue FB,
  stop FC, in every sync mode; timing clock F8 only in MIDI sync. Song select F3 0-7 (Bank I
  tracks 1-4, Bank II tracks 1-4) and song position F2 in track play, stopped: the 909 goes to
  that 16th of the track, each measure counted at its pattern's own length (last step and scale:
  a 16-step measure of 16ths is 16, of 12 steps 12, of 32nds 8), mid-measure too. Pattern play
  ignores F2: FB goes on from where the pattern stopped.
- System exclusive: the bank dump (below). FF (system reset) is passed on and otherwise ignored
  (its table entry, like F9, FD and FE, only sends it on).
- While the tape interface runs (SAVE, VERIFY, LOAD) MIDI is not read at all (SN p.15).

**Out** (both OUT jacks; bytes back to back, 320 us each):

- Timing clock F8 at 24 ppqn all the time, stopped too: from the tempo in INT, the received F8
  in MIDI sync, one per DIN clock in DIN sync.
- FA / FB / FC when it starts / continues / stops; F3 n when a track or bank is selected; F2 when
  a measure is recalled (SHIFT + number + ENTER).
- External instruments (EXT INST rows): note-on 9n, notes 36-51 for keys 1-16, on the transmit
  channel (11 at power-on; LAST STEP + key n sets channel n and sends OMNI OFF Bn 7C 00 and POLY
  Bn 7F 00 on it), velocity 64, or 64 + TA (up to 95) on a step with the external accent;
  note-off as velocity 0, 5 ticks of the 48 ppqn clock later (5/12 of a 16th). The internal
  voices are never sent.
- Thru: every message received, but F8 (sent as the 909's own clock in MIDI sync) and system
  exclusive, is sent on (SN p.15: "MIDI OUT works as a merge").

**System exclusive bank dump** (SN p.16; track play or stop only), the current bank:

- Host -> 909 F0 41 51 F7. The 909 sends 16 blocks F0 41 52 01 4n <512 bytes> <checksum> F7,
  n = 0-15, each followed by F0 41 54 F7 (55 after the last: "no data follow"), and waits for the
  host's F0 41 53 F7 (71 = error) before the next. The 512 bytes are the block's 256 RAM bytes as
  nibbles, low nibble first; the checksum is the 7-bit two's complement of their sum (the nibbles
  and the checksum add up to 0 mod 128).
- Host -> 909 F0 41 50 F7: the 909 answers F0 41 51 F7 and takes the 16 blocks in the same
  format, acknowledging each with F0 41 53 F7. A dumped bank sent back restores it byte for byte.

## Sync

VV:63 bits 0-1: 0 INT, 1 MIDI, 2 DIN. SHIFT + TEMPO MODE (TAPE SYNC) steps INT -> MIDI -> DIN
-> INT, stopped only (147B); DIN unmasks INT1 (MKL bit 3). Power-on is INT. In MIDI and DIN
the panel's START and STOP/CONT do nothing (OM p.34). The TEMPO display shows the tempo in INT,
a middle 0 in MIDI and a left 0 in DIN (OM p.34).

- **INT.** The tempo is A/D-driven and crystal-accurate: v = AN0's code filtered with a dead band
  (13FC: VV:31 is 128 at power-on; when the code is two or more counts away it moves to a count
  short of it, so after a power-on v is the code + 1 below 127, the code - 1 above 129, 128
  between; VV:30 takes it, or a track's tempo memory), B = 32 +
  floor(225 v / 256), and ETM1 = floor(1099138 / B) us a tick of 48 ppqn: 36.4-290.0 BPM in
  224 steps of about 1.14 BPM. The display shows v + 36 (37-290), within a BPM of the tempo
  (A/D 76 approached from above: 112.6 BPM, display 113).
- **MIDI.** Each F8 is a 24 ppqn clock: the firmware reads ECNT (us since the last one), loads
  half of it into ETM1 and makes a tick at once; ETM1 makes the tick in between (17F8). FA
  starts the pattern on the next F8, FB continues, FC stops.
- **DIN** (rear SYNC IN, SN p.12: 1 START/STOP, 3 TEMPO, 5 CONTINUE). START/STOP high runs it
  and sends FA (raised together with a clock edge, the first step comes on the next edge);
  CONTINUE high with it continues and sends FB; START/STOP low stops and sends FC. Each clock edge is handled like an
  F8 (and sent as one). Steady clocks of 60-300 BPM for 60 s ran without a fault; the "freezes
  when synchronized to DIN" of v4 (Revolution 909 v5 manual) was not reproduced.
- **TAPE SYNC** out (TO, PC4): 1202 Hz while stopped (416 us half periods); playing, the
  frequency alternates 1202 / 2404 Hz on every 48 ppqn tick (SN p.6). TAPE SYNC in (INT2) is not
  emulated: with the TAPE SYNC button lit, START waits for a tape that never comes.

## Timing (v4.0 on the emulated board; `tools/compare/sequencer_timing.py`)

| Quantity | Value |
|---|---|
| A/D interrupt | 192 us (48 us a conversion, four channels) |
| Panel/voice scan | 1.92 ms (10 interrupts) or 2.11 ms (11, when the scan masked one too long); idle 38 % / 61 %, mean 2.040 ms; with a pattern of BD, HT and crash on every 16th 50 % / 49 %, mean 2.018 ms; 0.6-1.1 % longer still (2.3 ms) when MIDI or tempo interrupts nest |
| Scan entry to the trigger latch write | 300-600 us (mean 345) |
| Trigger pulse | one scan, 1.89-2.16 ms, mean 2.04 ms (SN p.5: "5V positive going pulse ... for 2ms") |
| Trigger groups | BD/SD/LT/MT and HT/RS/HC/HH/CR/RD go out on alternate scans: on one step the two groups are 1.89-2.16 ms apart, either first (at 113.7 BPM, group B after A on 49 % of steps) |
| Sequencer clock | 48 ppqn; a 16th is 12 ticks, a 32nd 6, an 8th triplet 16, a 16th triplet 8 |
| Shuffle | the first 16th of a pair is 12 + s ticks, the second 12 - s, s = key - 1 (0-6): 50-75 % swing. 32nds: (12 + s) // 2 ticks (keys 1=2, 3=4, 5=6, 7). None on triplets or on an odd last step |
| Flam | f + 4 or f + 5 group periods (2 scans, 4.04-4.08 ms) after the grace note, f = key - 9: key 9 15.9-20.6 ms, key 16 43.8-49.0 ms; which one depends on the step's phase against the groups. HT (group B) always f + 5 (rarely f + 6). The first flam after START is always f + 5 (START is read on a group-A scan). Tempo-independent (OM p.26) |
| MIDI note-on to trigger | 0.32-2.23 ms after the message's last stop bit (mean 1.2), any voice |
| MIDI clock to step | the first step on the first F8 after FA; then 0.8-5.0 ms after its clock (mean 2.6-3.4), shuffled steps the same after their extrapolated half-clock |
| DIN clock to step | 0.9-5.1 ms after the rising edge (mean 2.7-3.2) |
| Tape sync | 1201.9 / 2403.8 Hz |

**Against a real 909** (AudioRealism pack, `private/reference/real909/packs/audiorealism_tr909_ultimate/`:
`RimShot909-shuffle1-7-112BPM` and `<voice>-flam1-8`, one unit, 96 kHz):

- Tempo: the shuffle files run at 113.80-113.85 BPM (263.50-263.63 ms a pair of 16ths), which
  is the internal clock at B = 100 (113.73 BPM, A/D 77 from above), 0.09 % faster (the files say
  112 BPM; the 909's display would read 114). The emulation uses A/D 77.
- Shuffle (the first 16th of each pair, 15 pairs a file against 1000+ emulated):

  | Key | real: mean (sd, range) ms | emulated: mean (sd, range) ms | difference |
  |---|---|---|---|
  | 1 | 131.58 (2.00, 129.2-135.1) | 132.05 (1.53, 128.4-134.0) | +0.47 |
  | 2 | 142.85 (1.83, 140.7-146.5) | 142.85 (1.79, 140.0-146.0) | -0.00 |
  | 3 | 153.96 (1.04, 151.6-155.6) | 153.87 (1.62, 149.8-157.7) | -0.09 |
  | 4 | 165.15 (1.70, 161.7-167.1) | 164.32 (1.72, 161.5-168.6) | -0.83 |
  | 5 | 175.12 (1.66, 172.8-178.7) | 175.82 (1.64, 172.2-178.8) | +0.70 |
  | 6 | 186.48 (1.50, 182.9-189.7) | 186.82 (1.69, 183.9-190.7) | +0.34 |
  | 7 | 196.96 (1.98, 193.9-199.8) | 197.78 (1.71, 193.7-201.0) | +0.82 |

  Both are 12 + s ticks to 0.06 tick (the real tick 10.976-10.985 ms, the emulated 10.990-10.992),
  with the same spread from the scan (sd 1.0-2.0 ms); the means agree within 0.9 ms.
- Flam (one flam a file; the interval read on the trigger's mark in the waveform: the attack's
  cross-correlation for BD, the trigger step on the toms, the snappy burst for SD):

  | Key | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
  |---|---|---|---|---|---|---|---|---|
  | BD | 20.29 | 20.28 | 24.29 | 32.48 | 32.14 | 36.09 | 44.76 | 48.57 |
  | SD | 20.90 | 23.61 | 29.04 | 32.47 | 36.52 | 40.95 | 44.92 | 48.78 |
  | LT | 20.46 | 24.71 | 28.34 | 28.09 | 36.41 | 40.74 | 44.75 | 48.90 |
  | MT | 20.58 | 24.40 | 29.46 | 32.53 | 36.83 | 40.86 | 44.98 | 48.66 |
  | HT | 20.21 | 24.96 | 28.62 | 32.43 | 36.88 | 40.98 | 45.12 | 48.79 |
  | emulated f + 4 | 15.9-16.4 | 20.0-20.7 | 24.0-24.6 | 27.9-28.7 | 31.9-32.8 | 35.9-36.7 | 39.9-40.6 | 43.8-45.0 |
  | emulated f + 5 | 20.0-20.6 | 23.9-24.8 | 27.9-28.8 | 31.9-32.7 | 35.9-36.9 | 39.9-40.8 | 43.8-45.0 | 47.8-49.0 |

  Every recorded interval is an emulated value to within 0.7 ms (37 of the 40 within 0.25 ms).
  The real unit's HT is f + 5 on all eight, as the firmware makes it; 27 of the other 32 are f + 5
  too, which is what the emulation gives for the first flam after START (the files are probably
  first hits after START). The earlier published figures, 10-38 ms from a scope (EchoBoy, research
  C.7), are about 10 ms short of both.

## Emulation notes

Checked against MAME's driver and core and the NEC manuals: the A/D converter (192 states, scan
mode, INTAD every 4), the timers (timer 1 cascaded on timer 0, TO), the event counter (ETMM 0C,
clear on ETM1; free-running for the tape decoder), the interrupt priorities, masking and
flag-clearing rules, the serial modes, the key matrix and the LEDs. Fixed:

- **Serial transmit** (core, `tools/port_upd7810.py` FIXES). MAME raised INTST only after a
  whole frame and started the next one a bit later, so the 909's MIDI bytes went out 11 bit
  times apart. The uPD7810 double-buffers: INTST means "TXB empty" and comes with the stop bit
  (NEC 87AD manual ch. 7). Bytes now follow each other at 320 us.
- **Interrupt acknowledge** (core). MAME takes an interrupt in no time; the CPU spends 16 states
  on it (NEC 87AD manual 9.4), about 2 % of the 909's time (an A/D interrupt every 192 us). It
  moves the scan's length against its 384 us limit, and with it the 1.92 / 2.11 ms mix.
- **Short slices** (the port's `run()`). It ran an instruction even when the slice's budget was
  still overdrawn by the last one, so slices of a few states (high sample rates, tools) ran the
  CPU fast: 4-state slices made the 1202 Hz tape tone about 3.5 kHz.
- **Cartridge sense** (`Machine`). AN1 read 0, which the firmware takes for an inserted
  cartridge: ENTER then switched to cartridge mode and a cartridge LOAD filled the bank with FFh.
  With none inserted AN1 now reads high, as the pull-up makes it.
- **Port C** (`Machine`). Unwired, PC read 0: DIN START/STOP and CONTINUE asserted. Now PC5/PC6
  read the DIN jack through their inverters, PC2 the foot switch, PC7 0.
- **MIDI in** (`Machine`): a byte is handed to the serial port at the stop bit's sample (9.5 bit
  times after its start) rather than at its end. (MAME's receiver samples RxD once per bit, at
  an arbitrary phase; driving RxD bit by bit drops bits, so `Machine` passes whole bytes.)

Not modelled: the INT1/INT2 input filter (an edge counts after 12 states high/low), a serial
receive overrun (the firmware reads each byte well within its 320 us), the tape input (LOAD,
VERIFY and TAPE SYNC wait forever, key 1 cancels a LOAD or VERIFY), the cartridge slot itself.
Whether ECNT's match clears it on the matching count (MAME) or one count later is not known: 1 us
in 4-35 ms, below what the recordings show (their tempo agrees to 0.09 %).

Additions to `Machine` for this: `setDinSync(run, clock, cont)`, `setFootSwitch(pressed)`,
`tapeSyncOut()`, `statesNow()` (the time inside `run()`, for trace callbacks).

## Tools

- `tr909seq` (`tools/tr909seq/tr909seq.cpp`): scripts keys, pots, MIDI (and `midiclock`), DIN
  sync (`din`, `dinclock`), the foot switch; logs trigger changes, hits (`log hits`: voice, code,
  pulse width), MIDI out, LEDs and their on-times (`leds`, `ledduty`), the display, RAM
  (`ram`, `snap`/`diff`), the scan's timing (`scan`), the tape outputs (`tape`); `cov`, `flow`
  for exploring the code. `tests.py` runs the regression checks.
- `dis7810 ROM [START END]`: MAME's disassembler.
- `tr909factory`: the factory memory (above).
