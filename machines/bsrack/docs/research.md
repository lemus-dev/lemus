# Novation Bass Station Rack: research

Phase 1, 2026-11-24. Paths are relative to `private/bsrack/` unless they start with `machines/`.
Sources and SHA1s: `sources.md`. Marks: [V] checked against a primary source (the ROM, the
owner's manual, a data sheet); [T] taken from the hand-traced schematic, unverified; [?] inferred.

## Summary

- **Target:** Bass Station Rack, firmware v1.5 (MAME `bassstr`, SHA1 2c2dc3be…913e).
  - The 1993 keyboard keeps its program inside the microcontroller. Nobody has dumped it, so the
    keyboard can't be the target.
  - Novation describes the Rack as the keyboard's insides in a 1U case, with patch memory, oscillator
    sync, CV/gate and an external filter input added (`docs/web/novation-bass-station-story.html`,
    the owner's manual p. 1).
- **Firmware:**
  - The 32 KB program runs on a Panasonic MN1880-family microcontroller. MAME has a cycle-by-cycle
    core for it (BSD-3).
  - The firmware does most of what a voice card would: both DCO clocks, both envelopes, the LFO, the
    PWM LFO, portamento, the 303-style 12 dB mode and the power-on tuning.
  - The on-chip peripherals are undocumented. Their register use matches the MN1880023 that MAME
    models for the Drum Station, Novation's 1995 sibling.
- **Circuit:** a hobbyist trace of a 1995 keyboard board gives the analog voice's topology. It covers
  2 DCOs, 2 OTA mixer VCAs, two cascaded Wasp/OSCar-style 2-pole OTA filter sections (12/24 dB) and an
  OTA VCA. **It gives no part values.** The CV multiplexer, the buffers and the digital half aren't
  drawn. No service manual is public.
- **Reference:**
  - polynominal.com has 30 clips of a real Rack, including **the Rack's own built-in demo**. Once
    the firmware runs, the demo is a note-for-note comparison.
  - There are also 3 keyboard demos.
  - Software models (GForce 2025, Novation's own ~2000 plugin) model the keyboard, so they are
    oracles only.
- **Tier:** 2 for the firmware half (our own peripheral decode). 2-3 for the analog half: the
  topology is known, but the values have to come from somewhere (§3.7). This is the decision to make
  before phase 4.

## 1. Target unit and revisions

| Unit | Year | Program | Notes |
|---|---|---|---|
| Bass Station keyboard | 1993 (SOS reviewed it July 1994) | Inside the MCU, undumped | 25 keys, 7 memories, no CV/gate |
| **Bass Station Rack** | 1994-95 (MAME dates the set 1995; SOS reviewed it July 1995) | **v1.5, 27C256** [V] | 100 memories (40 ROM, 60 user), sync, CV/gate in/out, filter input, MIDI thru |
| Super Bass Station | 1997 | v1.9, 27C512 (MAME `sbasssta`) | A different voice (sub-osc, noise, ring mod, arpeggiator). Possibly a later machine. |

- MAME has no other Rack firmware version, and no other version has been found.
- The traced board is a keyboard board, "Issue 4, 1995". A comment on the trace says Novation made
  several board revisions; the source is a forum reply.
- No Rack board has been photographed in public at a usable resolution.

**Recommendation:**
- Model the Rack with v1.5.
- Take the analog voice from the keyboard trace.
- Any Rack-only parts (external input, CV/gate I/O, headphone amp) come from the firmware's use of
  them and the manual, until a Rack board photo turns up.

## 2. Firmware and CPU

### 2.1 What is known [V]

- **MAME `novation/basssta.cpp`** (skeleton, NO_SOUND):
  - CPU: MN1880 at 8 MHz, with the comment "type and clock unknown (custom silkscreen; might be
    MN18P83220)".
  - Program 0000-7FFF.
  - Data map: RAM 0060-03CF, a 2816 EEPROM at 8000-87FF, the button input at 8800, the input-select
    latch at A800.
- **MAME's `cpu/mn1880` core:**
  - It is "a cycle-by-cycle emulation of the 8-Bit Dual Microcomputer architecture": two CPUs that
    share one execution core.
  - Known issues listed in its header: instruction timings partly guessed from the MN1870, interrupt
    vectoring guessed, few SFRs emulated.
  - The MN1880023 (CR-504, Drum Station, PSR-500) and MN18801A peripherals "are emulated only as far
    as [that] firmware uses them". Their register assignments are inferred.
- **Our disassembly** of v1.5 uses MAME's `mn1880d.cpp` in a standalone harness:
  - Vectors are big-endian. CPU0 resets to **0x3580**.
  - CPU1's vectors (0x20-0x3F) all point to stubs at 0x40-0x58. CPU1 resets to 0x0041, which is
    `br 0x0041`. **The second CPU is parked**, so only CPU0 runs the synth.
  - Interrupts in use (CPU0 vector slots):
    - 0x77D4 and 0x77EF reload timer 2 (`(x'22')`).
    - 0x77F3 is a periodic tick that counts to 8.
    - 0x5654 is **MIDI receive**: it reads `(x'1B')`, filters F7/F8 and fills a 32-byte ring at
      0x0100.
    - 0x5653 is a bare `reti`.
  - Init writes the same registers as MAME's MN1880023 map:
    - `(x'31')` timer run = F2
    - `(x'33')` timer control = 14
    - `(x'35')` timer 3 control = 10
    - `(x'20')`/`(x'22')` timers 0/2
    - `(x'1E')` ADC control = 70/71/73, read back from `(x'1C')`
  - **External latches** found from their writes:
    - **0xA000**: a control latch with shadow `(x'96')`. It has three 3-bit or 1-bit fields: bits
      0-3, 4-6 and 7.
    - **0xA800**: the button-matrix row select, read back at 0x8800.
    - **0xB000**: the mode LEDs. They are driven from the mode index `(x'BD')`, 0-5, one bit each.
    - **0xB800**: the two 7-segment digits, written alternately from 0x01AB and 0x01AC with
      `(x'03')` bit 5 as the digit select.
    - 8000-87FF: the EEPROM.
  - The ROM at 0x0100 holds a curve table of 0x00→0x7F. At 0x0200 there is a 16-bit table that falls
    by about 2^(−1/384) per step, from EEE4 down. It looks like DCO timer periods [?].
- **From the keyboard trace's comments:**
  - "The envelopes are digital and get generated inside the CPU."
  - The DCO sync (reset) lines come straight from CPU pins 38 and 65 into a 4070 XOR, "U1 Pins 38 and
    65".
  - The pitch and mod wheels go through an op-amp into two CPU ADC channels.
  - VCA_CV_LIN "is the envelope that comes from the CPU", buffered by an LM324 and a TLC274.
- **From the owner's manual:**
  - "When you switch it on, the BassStation Rack automatically tunes itself to concert pitch". So the
    firmware calibrates something at power-on.
  - 12 dB mode is a firmware mode: "In the 12dB position the MOD SOURCE control does not act as
    shown on the label i.e. there is only positive MOD available" (p. 19).
  - The Write switch is read by firmware.
  - The demo sequence is in ROM and sends a GM drum track to MIDI out.

### 2.2 Not known

- The exact MCU part and its clock. The crystal is X1 on the keyboard board, value unknown.
  - A Rack's CPU (a QFP, about 80 pins, from a Gearspace thread photo, 2025) is printed "NOVATION BASS
    STATION V1.5" and "JAPAN". So the custom marking carries the firmware version.
  - Whether that unit's program sits in the MCU or in an external 27C256, as in MAME's dump, isn't
    visible.
- What drives the CVs. The trace's header names OSCA_CV, OSCB_CV, OSCA/B_PWM_CV, A/B_VOL_CV, VCF_CV,
  RES_CV and VCA_CV_LIN, all coming from "the dull CV multiplexer and buffers".
  - Candidates are the MN1880023's PWM channel (`(x'50')`-`(x'55')`) or a DAC through a 4051 demux.
    The 0xA000 latch's 3-bit field fits a demux address [?].
- How the power-on tuning measures anything: which input it reads, and what it trims (ramp
  amplitude per range, filter tracking or VCA offset).
- What sets the waveform and slope switches (the 4053's A/B/C inputs). Candidates are the 0xA000
  latch or port pins [?].

### 2.3 Recommendation

- Port MAME's `mn1880` core as a standalone class (BSD-3, notice kept), with its disassembler for the
  `bsrackseq` logs.
- Build the peripherals from MAME's MN1880023 model: ports, timers 0-4, ADC, PWM, UART, serial.
  Extend each one only as far as v1.5 needs, decided by running it.
- Map every port, latch and timer output with a scripted driver before trusting any guess above.
- The DCO pitch is crystal-exact (timer periods), so the clock has to be right. Find it from:
  - the timer table and the A=440 claim (the firmware's own numbers);
  - the factory-demo recording's pitch.
  - The two must agree.
- The envelopes, LFO and portamento come out of the firmware as CV values. Model the multiplexer, the
  sample-and-hold and the buffers as circuit once their parts are known (§3.5).

## 3. Circuit

### 3.1 Documents

- The trace, plus the trace's own blog text and comments. There is no service manual.
- A comment says Novation supplies the schematic under NDA. We are not asking anyone for it.
- Data sheets for every named part type are in `docs/datasheets/`.

### 3.2 DCO (×2, identical) [T]

- **Exponential converter:** an op-amp (U24C/D, TLC274) and an FMA1 pair (Q5/Q6).
  - FMA1 is two DTA124E digital PNPs: 22 kΩ base resistor and 22 kΩ base-emitter resistor each,
    common emitters (`FMA1_rohm.pdf`) [V].
  - The built-in resistors halve the base drive and add a base-current term, so the converter is not
    an ideal exponential. Model it from the data sheet.
- **Ramp:** the collector current charges C10 (osc A) or C41 (osc B). A BC848 (Q2/Q1) discharges it.
- **Reset:** the CPU's sync line toggles. A 4070 XOR compares it with an RC-delayed copy of itself
  (R29/C9) and gives a short pulse on **every edge**. So the CPU toggles once per cycle.
- **Wave shaping:** U18A/B (TLC272) is either a non-inverting amplifier or a comparator. One 4053
  section picks which:
  - WF_SW = OSCx feeds the output back through R32/R31, so the stage amplifies: **sawtooth**.
  - WF_SW = OSCx_PWM_CV makes it compare the ramp with the PWM CV: **pulse**.
- **Sync:** the Rack's oscillator sync has no analog path on the trace. It must be firmware: osc B's
  timer restarted from osc A's [?].
- **Model:** these are DCOs whose ramp amplitude depends on how well the exponential CV tracks the
  timer period. Run the ramp as circuit, with BLEP at each reset, from the firmware's timer edges and
  CV values.

### 3.3 Mixer [T]

- Two LM3080 OTAs (U19, U21). Each takes its oscillator through a divider (R26/R27, R33/R34) and has
  a linear bias current from A_VOL_CV / B_VOL_CV (R35, R28).
- The current outputs sum into R36 at OSC_MIX.
- The manual's MIX knob crossfades ext → osc 1 → "1&2" → osc 2 (p. 13). The firmware turns that one
  knob into the two VOL CVs.
- The Rack's external input joins somewhere not on the trace.
- A Rack owner's report (Gearspace, 2011) says a cable plugged into FILTER INPUT by mistake carried
  the oscillators out, with notes that never stopped and the filter and volume knobs dead. So the
  jack's switched contact taps the oscillator mix ahead of the filter and VCA [?].

### 3.4 Filter [T]

- **Topology:** two identical 2-pole sections in series, "closely related to the OSCar filter that
  is, in turn, based off of the Wasp filter". The OSCar's inverting (high-pass) stage is removed.
- **Each section:**
  - input coupling (R49, C30 2.2 µF, R50/R51);
  - OTA (U26/U31, LM3080) into a TLC272/274 integrator (U25A/U22A, C31/C35);
  - divider R55/R56 (R67/R68);
  - second OTA (U27/U32) into a second integrator (U25B/U22B, C32/C36);
  - an R‖C path (R57‖C33, R69‖C37) from the output back to the input node.
  - **Resonance:** a third OTA (U28/U33) with bias from RES_CV (R58/R70). Its input comes from the
    first integrator through R52 (R64) and back-to-back diodes D8/D9 (D10/D11) into R53/R54 (R65/R66).
    These are the diode limiters that set the Wasp-family resonance character.
- **Cutoff:** VCF_CV_LIN goes into an exponential converter (U24A, FMA1 Q3, R37-R39). Its current
  VCF_CV feeds all four integrator OTAs' bias through R59, R60, R71 and R72. R72 is marked "272",
  2.7 kΩ [T].
- **Slope:** a 4053 section selects VCA_IN from 12DB_AUD (after section 1) or 24DB_AUD (after
  section 2).
- **Spec:** cutoff 5 Hz-10 kHz. Resonance "0 to self oscillation (24dB)" (p. 20) [V].
- **Model:** per section, a netlist on the machine's solver with the CA3080's tanh transfer and bias
  dependence (data sheet). Solve the diode limiters implicitly, inside the loop. Check against ngspice.

### 3.5 VCA, output, CV generation [T]

- **VCA:**
  - Input: R73, C61 2.2 µF.
  - Offset bias: R74/R76 with trimmer R77 from +5 V.
  - OTA: LM3080 U36, bias from VCA_CV via R75 and R79‖C60.
  - VCA_CV comes from an exponential converter (U24B, FMA1 Q4) driven by VCA_CV_LIN.
- **Output:** inverting amplifier U22C (TLC274) with R80, then C38 2.2 µF and R99 to the output.
- **CV generation:** not drawn. The keyboard's comments place an LM324 (U16) and a TLC274 (U12) as
  unity buffers on VCA_CV_LIN, with "some passives" between them (an RC smoothing or S&H stage [?]).
- **Model:**
  - VCA and output stage: as circuit.
  - CV path: as circuit once the firmware shows how it outputs CVs (PWM duty or multiplexed DAC);
    the smoothing it needs sets the CV slew (envelope stepping, portamento grain).

### 3.6 Power [T]

- Rails VDD, VSS, +5 V and AGND, from a 9 V DC input. The 9 V is probably split around a mid-rail
  AGND [?]; an LM317 is named in a comment.
- The TLC27x and LM324 output swing limits, against those rails, matter for the clipping of the
  integrators.

### 3.7 The missing values

The trace has the designators and no values. Ways to get them, best first:

1. **Board photos** with readable SMD codes, from any Rack or keyboard.
   - None found as of 2026-11-24.
   - The keyboard photos found are 567 px wide.
   - Of the three Gearspace threads, read in a browser, only one has photos: a Rack's rear socket and
     the CPU corner, 480×640. That is too small for part codes.
2. **The firmware's own numbers.** Timer periods, CV tables and the tuning routine fix the DCO
   converter's working range and the ramp amplitude target.
3. **Specs and recordings.**
   - Specs: cutoff 5 Hz-10 kHz, attack 0.5 ms-5 s, decay/release 3 ms-10 s, portamento 0-5 s.
   - The polynominal clips (filter-2poles, fiolter-4poles, wavefo, pulsewisth) constrain cutoff
     scaling, resonance and output levels.
   - Values chosen this way are **fitted, not sourced**. Each is flagged as such in `circuit.md`,
     within the ranges the part types allow (E24 values, OTA bias limits, the TLC27x supply).

**Recommendation (taken 2026-11-24):**
- No usable photo was found, so the values come from (2) + (3).
- Every fitted value is flagged in `circuit.md` and listed in improvements.md as "waits on data: a
  board photo".
- The topology is fixed by the trace either way.

## 4. Panel [V]

Front panel from `docs/web/polynominal-rack-front.jpg` (straight on, 4061×1244). 18 knobs, 12
switches, 14 buttons, a 2-digit 7-segment display, 5 mode LEDs, plus the edit, write and LFO-speed
LEDs:

| Section | Controls |
|---|---|
| Master | VOLUME, PORTAMENTO, PHONES jack, WRITE switch (PROTECT/ENABLE) with LED |
| Data entry | buttons 1-9, 0, −, + (with second labels in yellow); display; LEDs PROGRAM, MIDI RX CH, MIDI TX CH, UTILITY, SAVE; EDIT and WRITE indicators; AUDITION/DEMO; MODE SELECT |
| LFO | SPEED, DELAY, SPEED LED, SHAPE (R'DOM/TRI/SAW) |
| OSC 1 | MIX (EXT/OSC1/1&2/OSC2), WAVEFORM (saw/pulse) |
| OSC 2 | DETUNE (−/+), SEMI-TONE (0-+12), RANGE (16'/8'/4'/2'), WAVEFORM |
| OSC 1-2 MOD/SYNC | ENVELOPE (−5/+5), LFO (−5/+5), PULSE WIDTH (−5/+5), OSC1-2 SYNC (OFF/ON), OSCILLATOR (1/2), PWM SOURCE (ENV 2/MAN./LFO) |
| FILTER | FREQUENCY, RESONANCE, MOD DEPTH (−5/+5), CUT-OFF (12dB/24dB), MOD SOURCE (LFO/ENV 2) |
| ENVELOPES | ATTACK, DECAY, SUSTAIN, RELEASE, VELOCITY, ENVELOPE (1/2/1&2), TRIGGERING (AUTOGLIDE/SINGLE/MULTI) |
| Rear | CV IN, GATE IN, CV OUT, GATE OUT, FILTER INPUT, OUTPUT, MIDI IN/OUT/THRU, 9V DC |

- The knob positions are "soft". A program loads its values, and a knob takes over only once moved
  "more than a few degrees" (p. 4).
- The ENVELOPE and OSCILLATOR switches choose which envelope or oscillator the shared knobs edit
  (pp. 14, 17).
- These behaviours are the firmware's. The plugin needs the knob positions as pot ADC values and the
  switches as inputs; nothing extra.

## 5. Reference

### 5.1 Recordings of real units (free)

- **Rack (polynominal):**
  - factory-demo: 44.8 s, the built-in sequence. This is the main per-event reference once the
    firmware plays it.
  - filter-2poles, fiolter-4poles, adsr, lfo, wavefo, pulsewisth, detune-range, portamento, xmod,
    onturn.
  - 16 patch clips.
  - All are MP3s, 44.1 kHz mono, with unknown knob settings. Only the demo is fully determined: its
    notes and patches are in ROM.
- **Keyboard (polynominal):** demo1-3. A different unit and firmware, so they give spread and
  character only.
- **Not yet fetched:** a 2010 YouTube Rack demo recorded direct (matrixsynth.com, 2010-05-01).

### 5.2 Software models (oracles only)

- GForce Software, *Novation Bass Station* (2025, developed with Novation, of the keyboard).
- Novation's own Bass Station plugin (around 2000, keyboard).
- Both model the keyboard, not the Rack, and neither is a circuit or firmware emulation.

### 5.3 Recommendation

The factory demo is the first comparison: same notes, same patches, same timing.
- Align per note: pitch in cents, level in dB, envelope times in ms, and the filter's spectral
  envelope.
- The filter clips then check cutoff and resonance scaling.

## 6. Constants

| Constant | Value | Source |
|---|---|---|
| Program ROM | 32 KB, SHA1 2c2dc3be…913e | MAME `bassstr` [V] |
| CPU | MN1880 family, custom mark; MAME guesses 8 MHz | `basssta.cpp` |
| Reset vector | 0x3580 | ROM [V] |
| Stack | 0x0300 | ROM 0x3583 [V] |
| RAM cleared | 0x0061-0x03CF | ROM 0x35C1 [V] |
| MIDI ring buffer | 0x0100-0x011F | ROM 0x566A [V] |
| EEPROM | 2816, 2 KB, 8000-87FF | MAME |
| Osc 1 range | C-2 to C7 | manual p. 20 |
| Osc 2 range | C-3 to C10 (sic), +12 semitones, ±50 cents | manual p. 20 |
| Cutoff | 5 Hz-10 kHz | manual p. 20 |
| Attack | 500 µs-5 s (spec); "1 millisecond … to 10 seconds" (text) | manual pp. 17, 20 |
| Decay, release | 3 ms-10 s | manual p. 20 |
| Portamento | 0-5 s | manual p. 20 |
| Master tune | ±9 steps, "00" = A440 | manual p. 8 |
| MIDI CCs | filter 105-107; env 1 108-112; env 2 114-118 | manual p. 18 (layout unclear for the filter row) |
| DTA124E R1, R2 | 22 kΩ, 22 kΩ | `FMA1_rohm.pdf` [V] |
| Coupling caps | 2.2 µF (C30, C34, C38, C61) | trace [T] |
| R72 | "272" (2.7 kΩ) | trace [T] |

## 7. Open questions

1. The resistor and capacitor values of the voice (§3.7).
2. The MCU's clock and its exact peripheral set.
3. How CVs are made: PWM, or DAC + demux, and the smoothing after it.
4. What the power-on tuning measures and trims.
5. How sync is done: timer restart, or an analog path on the Rack's board that the keyboard lacks.
6. Where the external filter input joins (Rack only), and the CV/gate I/O circuits.
7. Whether the Rack's analog board matches the keyboard's Issue 4 trace part for part.

## 8. Data that would settle them

- A readable photo of a Rack board, both sides: 1, 2 (crystal), 5, 6, 7. Keyboard board photos
  settle 1 for the shared voice.
- Running the firmware with a scripted driver: 2 (the timer and UART rates against MIDI's
  31.25 kbaud), 3, 4, 5.
- The factory-demo recording against our render: the clock (pitch), then the voice values.
