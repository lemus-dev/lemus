# TB-303 CPU board and firmware: I/O map, timing, and emulation feasibility

Sources: MAME `roland_tb303.cpp` and the uCOM-4 core (copies and licences in
`private/reference/mame/`); *Roland TB-303 Service Notes*, first edition, 19 Feb 1982 (archive.org item
`Roland_TB-303_Service_Notes`: page 2 has the uPD650C pin table, pages 4–5 the main board
schematic); and a static disassembly plus a test run of the real ROM (`rom/d650c-133.ic8`)
in a small uCOM-43 board model written for this investigation (see "Firmware test run").

## Hardware summary

| Item | Value | Source |
|---|---|---|
| CPU | NEC uPD650C-133 (IC8): CMOS uCOM-43, 2000×8 mask ROM, 96×4 RAM, 3-level stack, 35 I/O lines, 42-pin | MAME, schematic p.4 |
| CPU clock | LC oscillator (TI S74230 coil, yellow, plus C12 0.01 µF), 2.2 µs period at TP3 = 454.5 kHz. 4 clocks per machine cycle = 8.8 µs, about 113,600 cycles/s. The part is rated 440 kHz max | MAME (`NEC_D650(config, m_maincpu, 454545)`), schematic |
| Interrupt | A separate Schmitt RC oscillator (IC6 4584, R37 120k + TM2 470k trimmer, C8 0.033 µF) gives a short low pulse every 1.8 ± 0.2 ms on /INT (TP2) | schematic p.4; adjustment p.6; MAME `irq_clock` 1800 µs, 10 % duty |
| ROM | `d650c-133.ic8`, 0x800 bytes, CRC32 `268a8d8b`, SHA1 `7a4236b2bc9a5cd4c80c63ca1a193e03becfcb4c`. Code reaches 0x7CF at most, so the 48 bytes after 2000 are never executed | MAME, disassembly |
| Pattern RAM | 3 × uPD444C (IC-5, IC-3, IC-4), 1024×4 static CMOS each = 3072 nibbles (1.5 KB), battery-backed through the "BU" supply | schematic p.4 |
| Internal timer | Not used: the ROM has no STM/TTM instructions | disassembly |

The service notes title the pin table "µPD650C-085"; the chip on the board and in MAME is
-133. The TR-606 uses -128 on the same kind of board.

## Port map

H is active-low through the switch-board transistors: MAME uses `inp_mux = H ^ 0xF`, so a 0
bit selects a column. H = 0xF selects no column, and then Q5 enables the "status buffer &
gate" (IC7 4001) onto port A.

| Port | Dir | Function |
|---|---|---|
| PA0–3 | in | Key matrix returns (low nibble). With H = 0xF: status lines (below) |
| PB0–3 | in | Key matrix returns (high nibble) |
| PC0–3 | I/O | RAM data bus (3.3 kΩ series, 15 kΩ pull-downs). Reads write 0 to C first, then use IP |
| PD0–3 | out | RAM A0–A3, **and pitch bits 0–3** |
| PF0–3 | out | RAM A4–A7. **PF0–1 = pitch bits 4–5** |
| PE0–1 | out | RAM A8–A9. **PE0 = accent bit** |
| PE2–3 | out | RAM chip select through IC-2 4556 (powered from BU): 0 = none, 1 = IC-5, 2 = IC-3, 3 = IC-4 |
| PG0–3 | out | LED matrix row drive (Q205–Q208) |
| PH0–3 | out | Key/LED column scan (Q209, Q211, Q213, Q212). All high = status select |
| PI0 | out | RAM /WE through Q4 (inverting). **PI0 = 1 writes.** R23/D4 hold /WE off when the CPU is unpowered |
| PI1 | out | Pitch/accent latch strobe (rising edge), **and the SLIDE ON/OFF level** |
| PI2 | out | Gate |

### Pitch DAC (schematic p.5)

- IC9 (4174 hex D flip-flop) latches PD0–3 and PF0–1 on the rising edge of PI1. Arrow nets 1–6
  go to D2, D5, D1, D6, D3, D4, and their Q outputs feed the ladder from LSB to MSB:
  PD0 = bit 0 (R82, ladder bottom), PD1 (R81), PD2 (R80), PD3 (R79), PF0 (R87), PF1 = bit 5 (R90).
  The two MSBs go through IC10 (4050) buffers.
- The ladder is R-2R, built from R74–R90: 200 kΩ, 0.1 % matched pairs, "MFR". The series legs
  are drawn as parallel pairs, which makes R = 100 kΩ. IC9 runs from the regulated 5.333 V rail, so
  1 LSB = 5.333/64 = 83.3 mV = 1/12 V, one semitone at 1 V/oct. IC11b (AN6562) buffers the
  output (TP4). It then goes to the slide circuit (IC12 4066, C35 0.22 µF, Q29/Q30) and to CV out.
- In the firmware, the pitch routine at 0x600 computes the DAC code as
  `base + pitch + transpose`. `base` is 11 when the step's DOWN flag is set (bit 0 of internal
  RAM 0x0B, which takes priority), 35 for UP (bit 1), and 23 otherwise. `pitch` is the step's
  note nibble (RAM 0x0A). `transpose` (RAM 0x2F) is added unless flag F2 is set. If C is stored
  as 1, C lands on DAC 12/24/36 (1, 2 and 3 V). That fits the spec "CV out +1 V to +5 V" and
  "3 octaves (pattern) with transpose, 4 octaves (track) with key shift". The C = 1 encoding is
  inferred, not confirmed.

### Accent, slide, gate (schematic p.5)

- **Accent**: IC13 (4013, flip-flop 2) has D = PE0 and clock = PI1. Its Q̄ output drives Q35,
  then the ACCENT net, then IC12 4066 in the envelope/accent circuit. The firmware sets
  E0 = bit 2 of RAM 0x0B just before the strobe (0x605–0x607). PE0 doubles as RAM A8, so the
  latch is essential.
- **Slide**: there is no separate slide pin. The net labelled SLIDE ON/OFF is the PI1 strobe
  line itself (it joins IC9 CLK and IC13 T). On an ordinary step the firmware pulses PI1 high
  for about 40 µs. If the gate is still on at a step boundary (the previous step had slide, or
  is tied), the firmware raises PI1 to latch the new pitch and leaves it high for the whole step
  (code at 0x2BF–0x2C3 tests the "gate on" flag, RAM x8 bit 1). Q29/Q30 then switch the 4066 so
  the CV goes through the slide RC. This matches Robin Whittle's measurement: the slide signal
  is active from the start to the end of the step after the slid note
  (`private/docs/papers/forums/dfish_303-slide.txt`).
- **Gate**: PI2 drives Q37, then GATE out (0 V / +12 V per the spec) and the envelope triggers
  (Q38). The gate goes on one instruction (about 9 µs) after the strobe. On a normal 16th step
  it goes off on the 3rd tempo clock. Across slid or tied steps it is held. Rests produce
  neither strobe nor gate, so the CV holds the last pitch.

### Inputs

Key matrix, from MAME's input map (labels not yet checked against the panel):

| Column | A0 | A1 | A2 | A3 | B0 | B1 | B2 | B3 |
|---|---|---|---|---|---|---|---|---|
| H0 | MODE sw bit 0 | MODE sw bit 1 | DEL / C# | INS / D# | 1 / C | 2 / D | 3 / E | 4 / F |
| H1 | TRACK sw bit 0 | bit 1 | bit 2 | – | 5 / G | 6 / A | 7 / B | 8 / C |
| H2 | PATTERN CLEAR | FUNCTION | PITCH MODE | TIME MODE | 9 / STEP | 0 / 3n | 100 / A | 200 / B |
| H3 | – | – | – | – | F# | G# | A# | BACK |

MODE switch S4 code: 3 = Track Write, 2 = Track Play, 0 = Pattern Play, 1 = Pattern Write.
TRACK switch S2 code: 0–6 = track 1–7, which are also pattern groups I, I, II, II, III, III, IV.
Both switches are read through matrix diodes D14–D22. MAME's names are the track-selector
legends. The panel drawing on service notes p.1 shows the four right-hand keys (9, 0, 100,
200) with the pitch-mode legends DOWN, UP, ACCENT, SLIDE above them, under the TIME MODE
note-value legends. Next to them are BACK and WRITE/NEXT (TAP). TAP is not in the matrix: it is
status bit PA1, wired in parallel with DIN pin 4.

Status inputs (H = 0xF), via transistor buffers Q1/Q2/Q7/Q3 into IC7 (4001), then diodes D10–D13:

| Bit | Signal | Evidence |
|---|---|---|
| PA0 | RUN/STOP level (1 = run) | The play loop tests the stored PA bit 0 (0x287, 0x549). MAME has Run/Stop on bit 0 |
| PA1 | TAP (the panel WRITE/NEXT key and DIN pin 4). The ISR edge-detects it into flag F1 | MAME has Tap on bit 1; the service notes list "TEMPO CLOCK, START/STOP, TAP" |
| PA2 | unknown (Q7, 1.5 MΩ base resistor) | – |
| PA3 | TEMPO CLOCK, 24 ppqn | The ISR edge-detects it into flag F0. The main loop counts 6 edges per step (0x3CD: RAM 0x36 mod 6). The test run confirms it |

SYNC DIN (input only; the TB-303 has no sync out): pin 1 RUN/STOP, 2 GND, 3 TEMPO CLOCK,
4 TAP. With no plug inserted, the socket's switch contacts route the internal signals to the
buffers. The internal tempo comes from an IC6 Schmitt oscillator (TEMPO pot 1 MΩ(C) + TM1
220k trimmer) through a 4013 divider (IC1), giving a square wave at TP1. The schematic marks
4 ms; the adjustment page trims the period to 8 ms ± 1 ms at full clockwise, which is about
312 BPM at 24 ppqn. The tempo range is ♩ = 40–300, which is 16–120 Hz at 24 ppqn. So the CPU cannot tell DIN sync from its own
clock. The panel RUN/STOP switch S5 also enters through this hardware path (IC6/IC1) rather
than the key matrix, as far as the schematic shows.

## Interrupt and timing

The ISR at 0x03C saves DPh/DPl/ACC (XHR/XLS/XAZ) and writes H = 0xF. It reads port A,
compares it with the previous value in RAM 0x1B, and sets F0 on a PA3 rising edge and F1 on a
PA1 rising edge. It stores PA, then scans one key column through a `JPA` jump table indexed by
RAM 0x1C, and returns with EI.

The tempo clock is therefore only **polled every 1.8 ms**:

- Clock pulses must stay high (and low) for more than 1.8 ms or they are missed. In the test
  run, 1 ms pulses lost steps, while 4 ms pulses and 50 % duty were clean from 40 to 300 BPM.
- Step onsets jitter by one ISR period, plus main-loop latency. At 120 BPM the strobe came
  0.9–2.6 ms after the clock edge and steps measured 124.05–126.09 ms (nominal 125). This is
  authentic behaviour of the unit.
- Gate length in the model, over 743 sixteenth and 66 triplet steps:
  - Sixteenths: 3 clocks (62.8–65.2 ms at 120 BPM). It goes off on the third clock rising
    edge after the edge that started the step.
  - Triplets: 5 of 8 clocks (104.4–106.6 ms).

  This matches the logic-analyser measurements cited in `docs/sequencer.md` §8.1 (Sonic
  Potions, Ultra303). Whittle's 1999 figure of 3.5 clocks is contradicted.

## Firmware test run

A Python port of the MAME uCOM-43 core, with the board glue above, was used: RAM, matrix,
1.8 ms IRQ, 24 ppqn pulses on PA3, and the run level on PA0. With it the firmware boots, and in
Pattern Play it emits strobe, gate, accent and slide exactly as described above. Filling the
pattern RAM with random nibbles gives repeating patterns of various lengths. Some run at 8
clocks per step: triplet mode, which matches the spec's "♩ = 3 steps, 1–15 steps". Accents
appear on PE0, and gate-held steps leave PI1 high. Across 60 random patterns every step was one
of these:

- A plain note: strobe, then the gate on and off.
- A note held into the next step (slide or tie source): strobe, gate on, held.
- A slid-into or tied step: strobe while the gate is already high, with PI1 left high.
  Same-pitch re-strobes like this look like ties.
- A rest: no strobe and no gate, so the CV keeps the previous pitch.

In Pattern Write mode, key presses produce RAM writes.

**This only works after inverting MAME's /WE polarity.** MAME has
`m_ram_we = (data & 1) ? false : true;`, which leaves the RAM in write mode whenever PI0 = 0.
The firmware's write routine (0x400: `SPB 0; RPB 0` on port I) pulses PI0 *high* to write, and
Q4 inverts it. With MAME's polarity the RAM is never read: the unit plays a constant note 23 on
every step and keeps overwriting one address. With `m_ram_we = data & 1` the patterns play.
This is probably one of the reasons the MAME driver is "not working".

## What MAME lacks

- Status inputs: the tempo clock is not connected (IN.4 has only Run/Stop and Tap, marked "todo").
- Outputs: the pitch latch, accent, slide and gate exist only as comments.
- The RAM /WE polarity is inverted (above).
- No NVRAM: the RAM is zero-filled at start and kept only in save states.
- LEDs: the battery LED and 4 other LEDs (D201–D203, D214, D219) are missing. Key names are incomplete.
- No sound: the voice is all discrete.
- Core TODOs: whether IRQ is level- or edge-triggered (MAME assumes edge), whether DPh is 3 or 4
  bits, and the bytes after 2000 (unused here).

## Feasibility in a plugin

Verdict: **straightforward**, and the sequencer can be the real firmware.

- **Cost.** About 113.6k machine cycles/s is 2.6 cycles per sample at 44.1 kHz, which is
  negligible. The core is about 1,300 lines of BSD-3-Clause code with no dependencies, small
  enough to port into `src/` the way hr16 carries its own MCS-51 core.
- **Clocking.** Step the CPU against the sample clock. Generate the ISR every 1.8 ms
  (≈ 204.5 cycles), and drive PA3 from the host PPQ with a 24 ppqn square wave, as the internal
  clock is. Each half-period must be at least about 2 ms. Turn port-I and port-E changes into
  sample-stamped voice events:
  - PI1 rising edge: latch the 6-bit note and accent.
  - PI1 level: slide.
  - PI2: gate.
- **Transport.** Host playing sets PA0 = 1. Pattern step 1 plays on the first clock after run.
  DIN sync has no song position, so to start mid-pattern you can either start at the next
  pattern boundary or fast-forward the emulation silently. Fast-forwarding is cheap: it needs
  about 2 ISR periods per clock, roughly 400 cycles per clock.
- **Tight timing (optional, not authentic).** Re-phase the ISR to just after each clock edge to
  remove the 0–1.8 ms jitter, or add lookahead to hide it.
- **Internal tempo** for the standalone app: emulate the TEMPO knob (40–300 BPM) as the clock
  source.
- **Panel.** All programming is through the key matrix. A virtual front panel with the 13 note
  keys, UP, DOWN, ACCENT, SLIDE, CLEAR, FUNCTION, PITCH/TIME MODE, BACK, TAP, RUN/STOP and the
  two rotary switches gives the authentic workflow. Possible MIDI paths:
  - Program change selects a pattern by simulated key presses.
  - Note-on drives the note keys in pitch mode.
  - A separate "direct MIDI" mode bypasses the firmware to drive the voice (note, velocity as
    accent, legato as slide).
- **State.** Save the 3072 RAM nibbles in the plugin state; that is the battery-backed memory.
  Cold-boot on load, as the hardware does at power-on.

Main unknowns to work out:

1. The panel labels for each matrix key (owner's manual).
2. What PA2 is, and confirming PA1 = TAP.
3. The exact gate-off latency (measured on hardware at about 2.3 ms after the ISR; the
   model is in that range).
4. Triplet mode ("3n"): the test run shows 8 clocks per step. The counter at 0x3CD is mod 6, so
   where the firmware switches to 8 has not been traced, and neither has the triplet gate length.
5. The RAM layout of patterns and tracks, needed for import/export. It can be decoded from the
   firmware.
6. Behaviour with uninitialised RAM (the "dead battery" state).
7. Per-unit variation of the CPU clock and the 1.8 ms ISR (trimmer TM2). Neither affects tempo,
   which comes from the external clock, only latency and jitter.

## Findings from the plugin's board model (src/tb303/machine.cpp)

Worked out by scripting the panel with `tools/tb303seq` and reading the RAM back.

**DAC output.** The firmware's codes are 11/23/35 + key (C = 0 … C′ = 12) for DOWN/normal/UP,
plus transpose. The ladder's termination resistor R78 returns to the 5.333 V reference, so the
output is (code + 1)/12 V: the C key is 2.000 V and the A key 2.750 V (the service notes'
110 Hz calibration point). The voice takes MIDI note = code + 13.

**Accent polarity.** PE0 = 1 latched on the PI1 edge means accent.

**LEDs.** 16 sit in the G × H matrix (column H0: C D E F on G0-G3; H1: G A B C′; H2: DOWN UP
ACCENT SLIDE; H3: C# D# F# G#). The other four, A#, PITCH MODE, TIME MODE and FUNCTION (normal
mode), are driven from PC1, PC2, PC0 and PC3 respectively through 47 k into a transistor with
1 µF on its base (switch-board D203, D214, D202, D219 with Q201/Q210/Q202/Q214), so they show the
average of what the firmware leaves on the data bus. RUN is lit by the run flip-flop.

**Pattern memory.** Group g (0-3 = I-IV), section s (0 = A, 1 = B), pattern p (0-7) starts at
`0x400 + 0x200·g + 0x100·s + 0x10·p` (IC-3 holds groups I-II, IC-4 III-IV). Per step:
- pitch nibble at base + step: 0-12 = C…C′, F = rest; E after the last step = end marker
  (pattern length); D in step 15 = triplet mode (so 15 steps at most);
- flag nibble at base + 0x80 + step: 1 = DOWN, 2 = UP, 4 = ACCENT, 8 = SLIDE.
A tie is stored as the previous pitch repeated with SLIDE set on the step before, which is why
the manual says a slide between equal pitches is a tie. PATTERN CLEAR fills the 16 pitch
nibbles with F (16 steps, all rest) and leaves the flag nibbles, and the pitches already in the
edit buffer, as they were. The pitch-entry edit buffer is in IC-5: pitches count down
from 0x27F and their flags from 0x2FF. `Machine::writePattern` writes patterns directly in this
format; it matches the RAM the firmware writes from the panel byte for byte.

**Power-up.** With the MODE switch on PATTERN PLAY the firmware plays pattern A1 of the selected
group without any key presses.
