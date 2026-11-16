# SH-101 CPU board and firmware: ROM, I/O map and emulation plan

Sources:
- MAME's MCS-48 core and disassembler (copies and licences in `private/reference/mame/`).
- *Roland SH-101 Service Notes*, first edition, 1 Nov 1982 (archive.org item
  `synthmanual-roland-sh-101-service-notes`, `private/docs/sh101-service-notes-archiveorg-text.pdf`).
  PDF pages used: p.3 (block diagram, keyboard circuit, the SN-243200 note), p.4 (CPU PROGRAM
  and flowchart), p.5 (TMP80C49P pinout), p.6 (D/A converter adjustment), p.9 (synth board
  circuit diagram, which holds the CPU) and p.10 (parts list). Pages were read from full-size
  renders (2550–7364 px wide), not from the OCR layer.
- The ROM (`private/roms/sh101/tmp80c49p-6-7301.ic6`, fetched by `private/tools/fetch_roms.sh`),
  studied with `tools/dis49.py` and run on the MCS-48 core in `core/` with the board glue below,
  driven by `tools/sh101seq` (scripts and `--selftest`).
- The *SH-101 Owner's Manual* (`private/docs/sh101-owners-manual-archiveorg-text.pdf`), for what
  the panel is meant to do.
- Joe Britt's commented disassembly, `SH101.asm` in github.com/joebritt/SH101_Disassembled at
  commit `708cde0`. Used as a cross-check; every claim here was checked against the code.
- Secondary sources: Electric Druid's "SH101 replacement processor: a feasibility study"
  (pin-function table) and the Tubbutec SH-1oh1 installation manuals. The Tubbutec manuals say
  pin 40 is about 5.3 V and that VR1 "D/A width" sets it.

## Verdict

**The ROM is plausibly genuine and complete.** The dump is Joe Britt's `80C49_SH101.bin`
(2048 bytes, SHA1 `a84bb1758582dbb800001db3663eaf3fb9d0e25d`, CRC32 `4d9cb13c`). Evidence:

1. **Two independent reads agree.** Its first 175 bytes are byte-for-byte identical to Florian
   Anwander's earlier read of a different chip, made with a different programmer (Galep-4 vs
   Data I/O 2900). That read fails from 0x0AF on (below).
2. **The code is coherent end to end.** Flow tracing from the reset vector reaches every one of
   the 1,161 bytes from 0x000 to 0x488, with no illegal opcodes and no gaps. The rest,
   0x489–0x7FF, is zero, the unused part of the mask.
3. **It uses exactly what the board wires up**:
   - No interrupts: /INT is tied high.
   - No MOVX, INS, PROG or expander opcodes: /RD, /WR, ALE, /PSEN and PROG are not connected.
   - No timer or counter start.
   - P1 is only read; P2 is written in two places; BUS is a latched output; T0 and T1 are each
     tested once.
4. **It reproduces the service notes in a test run:**
   - The test-mode table, voltage for voltage.
   - The transpose/keyboard examples ("H, lowest F = 2.417 V", "highest C = 5.0 V").
   - The CLK RST rule, the 4 × 8 key matrix and main loops of 1.1-3.7 ms (the notes say
     1.5-3.5 ms).
   - Everything the owner's manual says the keyboard, HOLD, KEY TRANSPOSE, the sequencer and the
     arpeggio do, down to the 100-step limit and the pedal stopping PLAY (`docs/sequencer.md`).

**Revision.** The service notes name a single mask, -7301. "Effective from SN-243200" (p.3) is a
hardware change: a heavier ground wire between the GATE OUTPUT jack and the keyboard lug,
because static could make the CPU "overrun". The ROM contains no version string, and no other
SH-101 mask is known. The MC-202 uses a different CPU (µPD78C06G-024), so it is no source of
variants. Vintage Planet sold a "uPD8749C-6-7301 CPU replacement kit", which implies a private
dump of the same code, but they never published it.

## Hardware summary

| Item | Value | Source |
|---|---|---|
| CPU | Toshiba TMP80C49P-6-7301 (IC6): CMOS MCS-48, 2048-byte mask ROM, 128 bytes RAM, 27 I/O lines, 40-pin DIP. "-6" is the 6 MHz grade, 7301 the mask number | parts list, p.5, p.9 |
| Clock | Murata CSA 6 MHz ceramic resonator plus a CSC 300 capacitor pack (X1, C19). ÷15 gives 400 k machine cycles/s, 2.5 µs each | parts list, p.9; MAME divider |
| Program memory | Internal only. EA (pin 7) is grounded. /RD, /WR, ALE, /PSEN and PROG are not connected. No 8243 | p.9; code |
| Data memory | The 128 bytes of internal RAM only; the parts list has no RAM chip. The sequence (100 steps) lives in the CPU, which the six drycells keep powered: the block diagram's "Memory Back-up Battery" line goes to the CPU | p.2, p.3, parts list; code |
| Interrupts | None. /INT (6) is tied to /SS (5) and +5 V, and the code never executes EN I or EN TCNTI. 0x003 and 0x007 hold ordinary code (the middle of `CALL $20A` and the operand of `ADD A,#$82`) | p.9; code |
| Timer | Never started (no STRT T or STRT CNT). The T register is used only as an 8-bit flag store (MOV T,A 12 times, MOV A,T 27 times) | code |
| Reset | /RESET (4) from a power-on circuit: TR13, TR14, C20 1 µF, C21, C22, R40, R51, R52 | p.9 |
| Pin 26 | Labelled /PS by Toshiba (VDD on Intel parts). Tied to the same +5 V point as /SS and /INT | p.5, p.9 |
| D/A reference | The CPU's own +5 V rail. VR1 "+5V (D/A WIDTH)" trims it | p.6, p.9; Tubbutec |
| Main loop | Polled. Steps 3–13 run every 1.5–3.5 ms per the notes; 1.1–3.7 ms in test runs, by mode and keys (`docs/sequencer.md` §1) | p.4; test run |

## ROM sources

| Source | File | Archive SHA1 | ROM SHA1 / CRC32 | Status |
|---|---|---|---|---|
| Joe Britt, `github.com/joebritt/SH101_Disassembled` @ `708cde01906312de7801204989628aa4610a4e45` (2018-10-14; the README says it was read with a Data I/O 2900; the asm header is dated 2013-02-09). The forks `gbraad/` and `cozycactus/SH101_Disassembled` are at the same commit | `80C49_SH101.bin` (raw file, no archive) | – | `a84bb1758582dbb800001db3663eaf3fb9d0e25d` / `4d9cb13c` | **good**; installed as `private/roms/sh101/tmp80c49p-6-7301.ic6` |
| Florian Anwander, `florian-anwander.de/roland_sh101/Roland_Sh101_8049_ROM.bin` (Last-Modified 2015-12-15). The link on his page is now inside an HTML comment | `Roland_Sh101_8049_ROM.bin` | – | `aeab5adeb739e560f01850f874c6e2756448c9a0` / `3e4201c7` | **bad** |
| The same file zipped with Florian's `readme.txt`, on alexanderpeppe.com/eprom-bins, archive.org `httpsdbwbp.com` ("EPROM Pack 90s vintage synths"), archive.org `vintage-synth-firmware-collection`, dbwbp.com/synthbin and seriescircuits.com | `Roland_Sh101_8049_ROM.zip` | `f7f480ee4a49cae6ded8b963db0fea80214919b9` | as above | bad |

Every file was downloaded into its own scratch directory and only hashed, extracted with bsdtar
and disassembled. The Peppe archive mirrors faithfully: its `Roland_Juno-6.zip` matches MAME's
`upd8049c-238.ic56`, SHA1 `eebda5bb…`.

**Why Florian's file is bad.**
- From 0x0B0 on, 1,860 of its 1,872 bytes equal the low byte of their own address
  (`B0 B1 B2 …`). That is a reader sampling a data bus the chip isn't driving. The 12
  exceptions only ever have bits cleared, never set.
- Byte 0x0AF reads 03 where Britt's reads 00. That byte is the initial key number in
  `MOV R3,#n`. With 03, F3 at transpose H comes out at 2.667 V and C6 at 5.25 V. With 00 they
  are the service notes' 2.417 V and 5.0 V.
- His 2011 note says "The code works fine (confirmed by Senso from www.vintageplanet.nl)". That
  cannot be true of this file: its first call lands on `IN A,P2` followed by an illegal opcode.
  Wayback has his 2010–2011 page but not the .bin, so we cannot tell whether he once served a
  better file.

## Validation

**Static** (`dis49.py -t -x private/roms/sh101/tmp80c49p-6-7301.ic6`):
- 1,161 of 2,048 bytes are reached as code, which is all of 0x000–0x488.
- No illegal opcodes, no IDL/HALT (so MAME's lack of the CMOS opcode does not matter), no
  SEL MB0/MB1 or SEL RB0/RB1, no MOVX, INS, MOVD, ORLD, ANLD, JNI, JTF or PSW access.
- Code sits in both 1K halves of the 2K: 0x000–0x3FF and 0x400–0x488. All of it is reached
  with A11 = 0, so bank 0 only.
- The one `MOVP A,@A` (0x14B) reads page 1 of the program itself as a source of random numbers.
- There are 4 subroutines, all called from top level, so the stack never goes deeper than 1.

**Test run.** The first runs used a throwaway Python port of MAME's MCS-48 core; `core/` (the
C++ port) and `tools/sh101seq` replace it, with this board glue:
- the 4 × 8 + 2 switch matrix on DB/P1;
- DAC = ~BUS & 0x3F, latched into the S/H that DB7/DB6 select;
- T0 = hold pedal, T1 = NOT clock.

`sh101seq --selftest` repeats every row below. Results:

| Experiment | Result | Service notes |
|---|---|---|
| Power on holding LOAD + KEY TRANSPOSE | Test mode. P2 = F9: LOAD LED code 1, KEY TRANSPOSE LED on, gate off. KCV = range = 0 | p.4; p.6 "LOAD and TRANSPOSE LEDs are illuminated" |
| PLAY / DOWN / U&D / UP / LOAD | KCV 2.750 / 2.500 / 4.750 / 0 / 0 V; range 0 / 0 / 0 / 4.750 / 0 V; gate off / off / ON / ON / off | identical to the p.4 table |
| HOLD in test mode | Leaves test mode through INITIAL SET; the same press, still down, then turns HOLD on | p.4 |
| Boot with all switches open (RANGE 2', TRANSPOSE H), key F3 / C4 / A4 / C6 | KCV 2.417 / 3.000 / 3.750 / 5.000 V, range channel 4.000 V, gate on while held | "H, lowest F 2.417 V", "highest C 5.0 V", "2' = 4 V" |
| Main loop period (S7 = GATE) | 1.63 ms idle, 1.12-1.76 ms with a key held (the scan stops at the lowest key). Other modes in `docs/sequencer.md` §1: up to 3.7 ms | "1.5 to 3.5 msec" |
| ARP UP, keys F3 A3 C4, 25 Hz clock on T1 | KCV steps 29 → 33 → 36 → 29 … one step per rising clock edge, about 1.2 ms after it; RANDOM (channel 2) takes a new value per step | p.4 steps 6, 7, 11 |
| 1 ms clock pulses | Steps are lost (half of them at 50 Hz in PLAY): T1 is polled once per loop | – |
| New key in ARP mode, or with S7 = LFO | P24 (CLK RST) low for one loop. Not pulsed with S7 = GATE | p.4 step 13 |
| HOLD button, T0 | With T0 = 1 the gate follows the key. The HOLD button toggles hold (P25 low = LED on). T0 = 0 holds | p.4 step 8 |

## Memory map

Program memory (internal mask ROM; addresses confirmed by tracing):

| Address | Contents |
|---|---|
| 000–050 | Reset: read the buttons (BUS = DF). If P1 == 7E (LOAD + KEY TRANSPOSE held), run TEST MODE (00A–04F) until HOLD is pressed |
| 051–059 | START, INITIAL SET: clear RAM 02–1B |
| 05A | MAIN_LOOP, flowchart step 3: RANGE DATA READ (BUS = EF) |
| 086–091 | Step 4: RANGE DATA OUTPUT. A = R5 + RAM[16], then DAC channel 1 |
| 092 | `CALL 0A4` keyboard_read (0A4–159): 4-bank key scan (0B0–), last-note or low-note priority, the T1 clock check (12F), random generator (147–151) |
| 094–098 | Step 7: RANDOM to DAC channel 2 |
| 099–0A1 | `CALL 20A`, then `CALL 15A` cv_dac_val (15A–209 plus 20F–3F9): FUNCTION SW READ (BUS = DF, 15A), hold pedal T0 (1C9), LOAD, PLAY, ARPEGGIO; returns the KCV value. Then KCV to DAC channel 0 |
| 0A2 | `JMP 3FF` |
| 20A–20E | delay2: `MOV R7,#2 / DJNZ / RET`, a 10-cycle (25 µs) settle |
| 3FA–3FE | dac_out: `CPL A / ORL A,#C0 / OUTL BUS,A / RET`. Puts the inverted value on the bus with the mux parked on Y3; the caller then clears DB7/DB6 to pick the S/H |
| 3FF–480 | update_outputs, step 13: build P2 in R6 (gate, portamento off, CLK RST, LEDs), `OUTL P2,A` at 480 |
| 481 | `JMP 05A`: back to step 3 |
| 483–488 | Tail of update_outputs |
| 489–7FF | Zero (unused) |

Internal RAM. Addresses and roles come from the code and test runs (`docs/sequencer.md` has the
behaviour):

| Address | Use |
|---|---|
| 00–07 | Bank 0 registers. R2 = raw buttons (P1 with DB5 low), or 0 to mark a PLAY retrigger loop; R3 = the current key, then the note; R4, R7 = scratch; R5 = RANGE (16' 0C, 8' 18, 4' 24, 2' 30), then the number of keys held, then the note; R6 = TRANSPOSE (L 00, M 0C, H 18) plus 0x40 if S7 = LFO, then the P2 image under construction (bits 0-4 = the mode, inverted; bit 5 HOLD LED; bit 6 KEY TRANSPOSE LED; bit 7 gate) |
| 08–09 | The only stack level used |
| 0A–0D | Keyboard banks 0–3 as scanned this loop, one bit per key |
| 0E | The current key (0-31): the one sounding, by the priority rule |
| 0F–12 | The arpeggio's key buffer (32 bits), copied from 0A–0D while keys are held, kept while latched |
| 13 | Bits 0-5: the random number (`+= ROM[0x100 + RAM[14]] & 0x3F` per clock edge, skipping zeros). Bits 6-7: the release counter (a release counts on the second empty scan, the fourth while a latched arpeggio runs) |
| 14 | The random generator's pointer into page 1 |
| 15 | The mode register: the last P2 image, inverted (bits 0-4 LOAD, PLAY, DOWN, U&D, UP; bit 5 HOLD on; bit 6 KEY TRANSPOSE LED; bit 7 gate on) |
| 16 | Key transpose (−7..+12), added to the RANGE channel; cleared in LOAD |
| 17 | The TRANSPOSE switch (00/0C/18) plus 0x40 if S7 = LFO, added to the KCV; 0x40 only in PLAY and LOAD |
| 18 | The last note played or entered: the arpeggio's current note, LOAD's "same note" check |
| 19 | The buttons pressed in the last loop, for edge detection |
| 1A | The last KCV − 5 (with the transpose), held when nothing plays |
| 1B | The sequence step pointer: 1B = before step 1, else the address of the step last entered or played. Bit 7: REST already stored for this press |
| 02–1B | Cleared by INITIAL SET ("keyboard and switch mode data") |
| 1C–7F | The sequence, 100 bytes, one per step, not cleared at power-on ("INITIAL SET … does not delete the Sequencer data"). A note is 80 \| n (C0 \| n legato), a rest 40 \| n, 00 the end (`docs/sequencer.md` §5.1) |

The T register is never started as a timer; the code uses it as a flag byte:

| Bit | Meaning |
|---|---|
| 0 | U&D direction: 1 = going down |
| 1 | The arpeggio is latched by HOLD |
| 2 | A key is held (after the release counter) |
| 3 | First key-down this loop (no key in the last loop); PLAY sets it when it starts. Drives CLK RST |
| 4 | The HOLD pedal was down in the last loop |
| 5 | A clock edge this loop (T1 fell: CLK rose) |
| 6 | T1 in the last loop |
| 7 | The arpeggio runs this loop: PORTA OFF (P23) |

F1 is the S7 = GATE+TRIG line during the key scan (0 = GATE+TRIG), then "no key held", then the
HOLD state. F0 marks a key found in the scan, then "the pedal was just released" (which stops
PLAY), then the arpeggio's U&D and buffer-replace flags.

## I/O map

Status: **code** = confirmed by the ROM (static or test run); **sch** = traced on the p.9
schematic; **hyp** = hypothesis.

| Pin | Signal | Schematic reference | Status |
|---|---|---|---|
| 12–15 DB0–DB3 | Keyboard bank select, active low (bank 0 = F3–C4, 1 = C#4–G#4, 2 = A4–E5, 3 = F5–C6; scanned FE, FD, FB, F7), **and** DAC bits 0–3 | 150 Ω R56, R55, R54, R53 to keyboard connector 105–108; DAC R61 1M, R60 500k, R59 250k, and DB3 through IC7 4050 to R58 125k | code, sch |
| 16 DB4 | Common for S3 RANGE, S7 (ADSR trigger: GATE / GATE+TRIG / LFO) and the TRANSPOSE switch ("OCT COM"), **and** DAC bit 4 | R66 150 Ω, diodes D19–D25; DAC: IC7 ×2 to R57 62.5k | code, sch |
| 17 DB5 | Common for the seven function buttons, **and** DAC bit 5 (MSB) | R64 150 Ω, SW board diodes D26–D29; DAC: IC7 ×3 to R70 30.1k + VR3 "D/A LINEAR" | code, sch |
| 18 DB6 | 4052 select A | IC10 pin 10 | code, sch |
| 19 DB7 | 4052 select B. INH (pin 6) is grounded, so the mux is always on | IC10 pin 9 | code, sch |
| — | **DAC.** The bus carries the value inverted (dac_out does `CPL A`). IC9a (TL062) sums the bits (R71 16.9k ‖ C65 47p, VR2 "D/A TUNE" offset via R62) and drives the 4052 Y common through R72 220 Ω. Route DB7 DB6: **00** → Y0, C30, IC9b, R73 = **KCV / CV OUT**; **01** → Y1, C28, IC11b, VR5 "RANGE WIDTH" = **VCO RANGE**; **10** → Y2, C29, IC11a (×2) = **RANDOM** (LFO random wave); **11** → Y3, not connected (the park state for every switch scan). Value v means v/12 V once VR1–VR3 are calibrated. The inversion explains p.6: the 2.5 V point (v = 30, bus 100001) uses the MSB, so it trims VR3 | p.3, p.6, p.9 | code, sch |
| 27–34 P10–P17 | Matrix returns, shared by keys and switches. Internal pull-ups; closed = 0. P1 is never written | keyboard connector 34–41, control-board connector 26–33, SW board 42–45, bender connector 22/23 via D17/D18 | code |
| P1 with DB5 low | P10 LOAD, P11 PLAY, P12 ARP DOWN, P13 ARP U&D, P14 ARP UP, P15 HOLD, P16 unused (per Britt), P17 KEY TRANSPOSE | p.3 FUNCTION SW'S | code |
| P1 with DB4 low | P10, P11 = S3 RANGE (open/open 2', P10 closed 4', P11 closed 8', both 16'); P13, P14 = S7 (LFO, GATE+TRIG; neither = GATE); P15 = OCT UP, P16 = OCT DOWN for TRANSPOSE (H = P15 only, L = P16 only, M = both; neither reads as H) | p.9 | code |
| P1 with DB0–DB3 low | One key per bit, lowest key of the bank = bit 0 | p.3 keyboard circuit | code |
| 21–23 P20–P22 | LED code to IC8 4556: E1 = P22, A1 = P20, B1 = P21; E2 = P21, A2 = P22, B2 = P20. 1 LOAD (TR21), 2 PLAY (TR20), 3 ARP DOWN (TR19), 4 ARP U&D (TR18), 5 ARP UP (TR17); 0, 6, 7 = none. Mutually exclusive | p.9 | code, sch (traced independently; agrees with Britt) |
| 24 P23 | **PORTA OFF**, active high: high exactly while the arpeggio runs (2+ keys held or latched). On the bender board it holds the AUTO portamento's glide-short circuit on, so AUTO doesn't glide during an arpeggio (OM p.32) | bender connector 24 "PORTA OFF", R16 220k | code, test run, sch |
| 35 P24 | **CLK RST**, active low, one loop long: at the first key-down with S7 = LFO in normal playing, or in an arpeggio mode; and when PLAY starts | long run to R36 and TR7 in the LFO | code, test run, sch |
| 36 P25 | HOLD LED, active low | R68 33k, TR16 | code (test run), sch |
| 37 P26 | KEY TRANSPOSE LED, active high | R50, TR10, D16, R69, TR15 | code, sch, p.6 |
| 38 P27 | **GATE**, active low, inverted by TR9 to GATE (R49 100k, R47 22k) | p.9 | code |
| 1 T0 | HOLD pedal (connector 102, jack J7) through TR12. **T0 = 1 means not held**; T0 = 0 holds, and lights the HOLD LED. Releasing it stops PLAY. The pedal polarity and what T0 reads with nothing plugged in are unverified | R41/R42/R44, R43 | code (test run), sch |
| 39 T1 | **Clock**, inverted by TR11: T1 = NOT(CLK IN). The internal LFO square reaches CLK (100) through TR8 and is normalled to CLK IN (101) through the EXT CLK IN jack J8 (jack board A). Polled once per loop at 0x12F; a rising clock edge is a falling edge on T1, seen only if one poll finds CLK low and the next high | R45/R46, R134 | code, sch |
| 6 /INT, 5 /SS | Tied together to +5 V | p.9 | sch, code |
| 7 EA | Ground | p.9 | sch |
| 4 /RESET | Power-on reset, TR13/TR14 | p.9 | sch |
| 8 /RD, 9 /PSEN, 10 /WR, 11 ALE, 25 PROG | Not connected | p.9 | sch, code |
| 2, 3 XTAL1/2 | 6 MHz ceramic resonator | parts list | sch |

## Main loop and entry points

1. **Power on (000).** BUS = DF, `CALL delay2`, read P1. If it is 7E, go to TEST MODE.
2. **TEST MODE (00A–04F).** Each pass: read the buttons and latch the last newly pressed one in
   R1. LOAD/PLAY/DOWN/U&D/UP select (KCV, range, P2) = (0, 0, F9), (33, 0, FA), (30, 0, FB),
   (57, 0, 7C), (0, 57, 7D). Write KCV to channel 0, delay, range to channel 1, then P2. HOLD
   leaves through 051.
3. **INITIAL SET (051).** Clear RAM 02–1B.
4. **Main loop (05A–481), once per 1.1–3.7 ms:**
   1. 05A: steps 3–4, read RANGE/S7/TRANSPOSE and output RANGE + key transpose on channel 1.
   2. 092: keyboard_read, steps 5–6. Scan 4 banks into 0A–0D, apply priority (F1), check the
      clock on T1, and generate a random number on a clock edge.
   3. 094: step 7, RANDOM on channel 2.
   4. 09B: cv_dac_val, steps 8–12. Read the buttons and hold pedal; KEY TRANSPOSE plus a key
      sets the transpose; LOAD stores steps; PLAY reads them back; ARPEGGIO steps through the
      key buffer on clock edges. Returns the KCV value. Then KCV on channel 0.
   5. 3FF: update_outputs, step 13. Compute gate, portamento off, CLK RST and LED bits in R6;
      `OUTL P2` at 480; `JMP 05A`.

## Emulation design

MAME's `I8049` at 6 MHz runs this ROM unchanged: it uses no CMOS-only opcode. Board glue:

- **`bus_out_cb`.**
  - Latch DB.
  - If DB7/DB6 ≠ 11, set S/H[DB7:DB6] = (~DB) & 0x3F. Channel 0 = KCV, 1 = VCO range,
    2 = random.
- **`p1_in_cb`.** Start from FF. For every DB0–DB5 bit that is low, clear the P1 bits of the
  closed switches in that column. Key k (0 = F3 … 31 = C6, MIDI 53–84) is column k / 8,
  bit k % 8.
- **`p2_out_cb`.** Gate = !P27. LED code = P22–P20, HOLD LED = !P25, KEY TRANSPOSE LED = P26.
  CLK RST = !P24 resets the LFO phase. Portamento off = P23.
- **Test inputs.** `t0_in_cb` = !hold pedal; `t1_in_cb` = !clock.
- No `AS_IO` map, no IRQ, no NVRAM beyond the internal RAM. Save the 128 bytes of RAM in the
  plugin state: that is the battery-backed sequence. A cold boot re-runs INITIAL SET, which
  keeps 1C–7F.

The cost is about 400 k simple instructions per second, which is negligible.

**Pitch to the voice.**
- VCO CV = (KCV + RANGE) / 12 V, plus bender, LFO and portamento.
- The CV OUT jack carries KCV alone: 0.417–5.0 V across the 3 transpose positions. That is the
  spec's "CV 0.415–5 V".
- Turn S/H updates and P27 into sample-stamped voice events. KCV is written about once per loop,
  and a key or clock event takes effect about one loop (1.1–3.7 ms) later. That latency is
  authentic.

**MIDI to keyboard** (`core/midi.h`, as built). Drive the matrix, not the voice, because the
firmware does note priority, legato, arpeggio and step entry itself:

- Note-on closes key (MIDI − 53 at the panel's TRANSPOSE); note-off opens it.
- Every key change is held for 2 loop boundaries (counted with `Machine::loops()`), so the
  firmware sees each press and each release (a release only counts on the second empty scan).
- A note-on for a key still down lifts it first, so repeated notes retrigger as on the keys.
- In an arpeggio mode a chord that arrives all at once is pressed one key first, so the pattern
  starts on it (`docs/sequencer.md` §6, §10).
- Notes outside 53–84 step the virtual TRANSPOSE switch, which the firmware reads every loop;
  beyond L and H they fold by octaves.
- CC64 drives T0 (the pedal).
- Panel buttons (LOAD, PLAY, ARP UP/U&D/DOWN, HOLD, KEY TRANSPOSE) are switches on the DB5
  column, which the editor presses.

**Host sync to clock** (as built).
- The arpeggiator and sequencer advance one step per rising edge at CLK IN. There is no PPQN
  divider anywhere.
- SYNC on is a cable in EXT CLK IN: a square at the step rate (1/4 to 1/32) from the host's PPQ
  position, 50 % duty, first edge on the first grid line at or after the transport's start. With
  the host stopped there is no clock (the plug cuts the MODULATOR's square off CLK).
- Because T1 is polled once per loop, each half-period must exceed the longest loop (3.7 ms):
  about 135 Hz at 50 % duty, far above musical tempos.
- SYNC off: the MODULATOR's square is the clock, and CLK RST (P24 low) restarts the MODULATOR.

## Phase-2 mapping (done)

`tools/sh101seq` runs the ROM from scripts (keys, buttons, switches, pedal, clock, RAM pokes,
`write`/`seq` for sequences, `stats` for loop periods) and logs the S/H writes, P2 and RAM
against emulated time. `sh101seq --selftest` checks every result; `docs/sequencer.md` is the
specification written from them. In brief:

1. **Sequencer format** (§5.1). One byte per step at 1C–7F: 80 | n for a note (n = key + 12 ×
   TRANSPOSE), C0 | n for a legato note, 40 | n for a rest, 00 for the end. 1B is the step
   pointer. `Machine::writeSequence`/`readSequence` implement it; PLAY reproduces a written
   sequence. Quirk: notes entered with S7 = LFO all get the legato bit.
2. **PLAY** (§5.3). One step per rising clock edge; the gate drops for one loop at the start of
   every step that isn't legato (the old pitch held during the gap), is off for rests, and loops
   after the end byte. TRANSPOSE is ignored; KEY TRANSPOSE moves RANGE; the pedal's release stops
   PLAY; an empty sequence leaves PLAY at the first clock.
3. **Arpeggio** (§6). Runs with 2+ keys held or latched by HOLD; UP, DOWN and U&D (ends played once)
   walk a 32-key bitmap in key order from the current note, one key per clock, with the gate on
   throughout and PORTA OFF high. A single key plays normally. A latched arpeggio takes added keys
   while some are held; a key pressed with none held replaces it. TRANSPOSE and RANGE act at once.
4. **Keyboard** (§2). GATE+TRIG: the newest key, falling back to the lowest held; GATE and LFO:
   the lowest. No gate gap between legato notes in any mode (the GATE+TRIG retrigger is the synth
   board's KCV-change detector). A release counts on the second empty scan, so the gate drops 2
   boundaries after it; a key released for one boundary is not seen.
5. **Portamento AUTO** (§8). P23 is high only while the arpeggio runs; the legato-only glide is
   the bender board's GATE differentiator.
6. **Clock limits** (§1, §7). Loop periods 1.1–3.7 ms by mode (GATE+TRIG idle loops grow with the
   last key played). Each half of the clock must outlast the longest loop: about 135 Hz at 50 %
   duty.
7. **Random** (§7.1). RAM 13 += the next non-zero 6-bit byte of page 1 per clock edge: 54, 25,
   28, 40, 12, … from power-on, period 500 edges.
8. **Power** (§9). INITIAL SET clears 02–1B and keeps the sequence; without batteries the
   sequence is whatever the RAM holds; the plugin state (header + 128 bytes) is a cold boot with
   batteries in.

## Open questions

1. HOLD jack wiring: the firmware treats T0 = 0 as held. Which pedals give that, and what T0
   reads with nothing plugged in (TR12, R41–R44, J7's normalling)?
2. What CLK RST does to the MODULATOR's square (does it restart high, so the first arpeggio or
   sequencer step comes at once?): for the synth-board transcription.
3. Per-unit calibration (VR1–VR3, VR5) and S/H droop. Model the ideal 1/12 V per count.
4. Whether any other mask than -7301 exists. None is known.

Answered: the step format and the role of 1B (the pointer), and P23 (PORTA OFF while the arpeggio
runs); see `docs/sequencer.md`.
