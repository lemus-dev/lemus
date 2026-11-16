# SH-101 sequencer, arpeggio and keyboard: behavioural specification

This document describes what the SH-101's CPU does with the keyboard, the seven function buttons,
the HOLD pedal and the clock: note priority and the gate, KEY TRANSPOSE, HOLD, the 100-step
sequencer and the arpeggio. It is written from the firmware (the TMP80C49P-6-7301 mask ROM run on
the MCS-48 core in `core/`), checked against the owner's manual and service notes. Every rule
here is exercised by `sh101seq --selftest` (`tools/sh101seq/selftest.cpp`) against the real ROM.
`docs/firmware.md` has the CPU, the I/O map, the RAM map and the code layout.

## Evidence labels

| Label | Meaning |
|---|---|
| **[CODE 0x...]** | Read from the ROM (`tools/dis49.py -t -x`), at the address given. |
| **[RUN]** | Measured by running the ROM in `tools/sh101seq` (scripts or the self-test). |
| **[OM p.N]** | *SH-101 Owner's Manual*, printed page N (`private/docs/sh101-owners-manual-archiveorg-text.pdf`). |
| **[SN p.N]** | *SH-101 Service Notes*, first edition, PDF page N (`private/docs/sh101-service-notes*.pdf`). |
| **[SCH]** | Traced on the service-notes schematic (`private/docs/schematic/`). |
| **[UNCERTAIN]** | Inference, or where the sources disagree; the reason is given. |

Notation: keys are numbered 0 (F3) to 31 (C6). A KCV *code* is the 6-bit value on the DAC's KCV
channel, 1/12 V per count; code c plays MIDI note 36 + c at RANGE 8' with no key transpose (code
24 = 2 V = C4). A *loop* is one pass of the firmware's main loop; a *boundary* is the P2 write
that ends it.

---

## 1. The main loop

The firmware is a polled loop with no interrupts and no timer [CODE; SN p.4 flowchart]. Each
pass, in this order [CODE 0x05A-0x481]:

1. Read RANGE, ENV TRIG (S7) and TRANSPOSE; write RANGE + key transpose to the DAC's RANGE
   channel.
2. Scan the 32 keys (4 banks of 8), apply the note priority, poll the clock on T1, and on a
   clock edge make a new random number.
3. Write the random number to the RANDOM channel.
4. Read the buttons and the HOLD pedal (T0); run KEY TRANSPOSE, LOAD, PLAY or the arpeggio;
   work out the KCV and write it.
5. Work out the gate, PORTA OFF, CLK RST and the LEDs, and write them all to P2 at once.

So within a loop the KCV is written 0.15-0.2 ms before the gate [RUN].

**Loop period** [RUN; MAME cycle counts, 6 MHz ÷ 15 = 2.5 µs per machine cycle]:

| State | Loop |
|---|---|
| GATE or LFO, idle | 1.63 ms |
| GATE, a key held | 1.12 ms (F3) to 1.76 ms (C6): the scan stops at the lowest key |
| GATE+TRIG, idle | 1.75 ms after F3 was the last note, up to 3.16 ms after C6 (see below) |
| GATE+TRIG, a key held | 2.44 ms |
| LOAD, a key held | 2.61 ms |
| PLAY | 1.95-3.49 ms (depends on the last key, as in GATE+TRIG) |
| Arpeggio running | 2.8-3.7 ms (3.2-3.7 ms with all 32 keys) |
| Test mode | 0.21 ms |

The service notes give 1.5-3.5 ms [SN p.4]. In GATE+TRIG the scan counts keys up to the last
note's key to see whether it is still held; if it isn't, it scans all four banks again for the
lowest key [CODE 0x0C9-0x0D5], so an idle loop is longer the higher the last note was.

Inputs are read once per loop, so a key, button or clock change takes effect up to one loop
later, and the output follows at the next boundary. In the emulation (`core/machine.cpp`) the
panel inputs are latched at each boundary and read by the loop after, so a TRANSPOSE change and
the key changes that go with it are always seen together.

---

## 2. Keyboard and gate

### 2.1 Note priority

| ENV TRIG (S7) | Priority | Source |
|---|---|---|
| GATE+TRIG | **Last note.** A key newly pressed (since the last scan) becomes the note; of several pressed in the same scan, the lowest. When the sounding key is released, the **lowest** key still held becomes the note, not the one played before it. | [CODE 0x0A4-0x0E2]; [RUN]; [OM p.29] "last note priority" |
| GATE, LFO | **Lowest note**, always. | [CODE]; [RUN]; [OM p.29] "lower note priority" |

Example [RUN]: hold A4, add F3, add C6, release C6, release F3. GATE+TRIG plays A4 → F3 → C6 →
F3 → A4; GATE plays A4 → F3 → A4.

### 2.2 The gate

- The gate is on while any key is held [CODE 0x40B-0x412]. There is **no gate gap between
  legato notes** in any mode: a new key pressed while another is held only changes the KCV
  [RUN].
- **GATE+TRIG's retrigger is not the CPU's.** The synth board's TRIG circuit (IC5a/5b with
  D11/D12 and C13 0.0047 µF) differentiates the KCV and fires the envelope on any pitch change,
  up or down [SCH p.9 "TRIG"; OM p.29 "each new key you press will have a complete envelope
  shape, even if you play in legato manner"]. The emulation of that circuit belongs to the
  synth board (`core/filter.*`). [UNCERTAIN: the exact input of C13 is for the circuit
  transcription to confirm.]
- **A release is only counted on the second empty scan.** The first loop that finds no key
  pressed keeps the "key held" flag and sets a one-loop counter in the top bits of RAM 0x13; the
  next one ends the note [CODE 0x107-0x121]. So the gate drops two boundaries after a release,
  3.5-5 ms [RUN], and a key released and pressed again within one loop boundary does not
  retrigger at all. Released for two boundaries or more, the gate drops for exactly one loop
  [RUN; self-test "Release and re-press"]. While a latched arpeggio runs the counter allows up
  to four loops [CODE 0x11A].
- At a new note the KCV moves first and the gate rises 0.15-0.2 ms later, in the same loop
  [RUN].

### 2.3 CLK RST

P24 goes low for one loop, which restarts the MODULATOR (TR7) [SCH p.9], on the first key-down
(the loop where the keyboard goes from no key to a key) when [CODE 0x466-0x477, 0x483-0x487]:

- ENV TRIG is on LFO, in normal playing; or
- an arpeggio mode is on (UP, U&D or DOWN), whether or not the arpeggio then runs.

PLAY also pulses it when it starts [CODE 0x29E-0x2A3]. It is never pulsed in GATE or GATE+TRIG
normal playing, or in LOAD [RUN]. The manual: "the LFO's output waveform will always start from
its head" [OM p.29].

### 2.4 Switches

- **RANGE** (16', 8', 4', 2') sets the RANGE channel to 1, 2, 3 or 4 V [CODE 0x05D-0x071; SN
  p.3].
- **TRANSPOSE** (L, M, H) adds 0, 12 or 24 to the KCV: F3 plays 0.417, 1.417 or 2.417 V and C6
  3.0, 4.0 or 5.0 V [CODE 0x072-0x07E; SN p.4; RUN]. It is read every loop, so it moves a held
  note, a held arpeggio and the CV OUT at once [RUN]. PLAY ignores it (§5.2).
- **ENV TRIG** (S7) chooses the priority (§2.1) and CLK RST (§2.3); on the synth board it also
  switches the envelope's trigger.

---

## 3. HOLD

- The **HOLD button** toggles hold; its LED (P25) shows it [CODE 0x181-0x1C5]. With hold on the
  gate stays on after the keys are released; new keys change the pitch under the held gate
  [RUN; OM p.36].
- The **HOLD pedal** (jack J7 → TR12 → T0) holds while T0 is low and lights the HOLD LED too
  [CODE 0x1C8-0x1DD; RUN]. Releasing it ends the hold, and **stops PLAY** (§5.2) [CODE 0x1D1-0x1D5,
  0x2D4-0x2D7; OM p.35 "you can stop the sequence by stepping on the Pedal and releasing it"].
  [UNCERTAIN: which pedal polarity gives T0 low, and what T0 reads with nothing plugged in, is
  for the circuit transcription; the emulation's default is "not held".]
- The HOLD press that leaves test mode is still down when the main loop starts, so it turns HOLD
  on [RUN].

---

## 4. KEY TRANSPOSE

Hold KEY TRANSPOSE and press a key [CODE 0x15A-0x17F; OM p.36]:

| Key | Transpose |
|---|---|
| F3 … B3 (keys 0-6) | −7 … −1 |
| C4 (key 7) | 0 (LED off) |
| C♯4 … C5 (keys 8-19) | +1 … +12 |
| C♯5 … C6 (keys 20-31) | +1 … +12 again |

- The transpose is added to the **RANGE** channel, not the KCV, so the VCF's key follow does not
  move [CODE 0x086-0x08F; OM p.36 note]. The KEY TRANSPOSE LED (P26) is lit while it is not
  zero [CODE 0x413-0x41B].
- The key pressed with KEY TRANSPOSE does not play [CODE 0x17D].
- It works while playing, under HOLD, on a latched arpeggio and on a playing sequence [RUN; OM
  p.36].
- **LOAD cancels it**: RAM 0x16 is cleared on every LOAD loop [CODE 0x276; RUN].
- [UNCERTAIN] The manual says pressing "any C note (except for the highest C)" returns to normal
  and that "the two upper C keys" transpose up an octave [OM p.36], which contradict each other.
  The code: C4 returns to normal, C5 and C6 both give +12.
- It is cleared at power-on (INITIAL SET) [CODE 0x051].

---

## 5. The sequencer

### 5.1 Memory format

The sequence is 100 bytes of the CPU's internal RAM, 0x1C-0x7F, one byte per step [CODE; RUN].
The six drycells keep the CPU powered, so it survives power-off [SN p.2].

| Byte | Meaning |
|---|---|
| `1 0 n n n n n n` | A note: n = key + 12 × TRANSPOSE (0-55; KCV = n + 5) |
| `1 1 n n n n n n` | A **legato** note: PLAY doesn't retrigger at its start (a slur, or a tie on the same note) |
| `0 1 n n n n n n` | A rest; n is the last key entered (+ transpose), which PLAY ignores |
| `0x00` | The end. A full sequence has no end byte: step 100 is followed by step 1. |

RAM 0x1B is the **step pointer**: 0x1B before the first step, otherwise the address of the step
last entered or played. Its bit 7 is a one-press flag for REST. Normal playing sets it to 0x1B
every loop [CODE 0x1EB-0x1F0]; INITIAL SET clears it [CODE 0x051]. (Britt's notes count 0x1B as
part of the sequence; it is the pointer.)

`Machine::writeSequence` / `readSequence` write and read this format; `sh101seq`'s `write` and
`seq` commands use them.

### 5.2 LOAD

[CODE 0x20F-0x284; OM pp.33-34]

- **LOAD** lights its LED and enters steps from the keyboard. Each key press stores one step:
  the note is the key plus 12 × the TRANSPOSE switch at that moment, so the 32 keys cover 56
  notes [OM p.34]. Holding a key stores it once; a new key while one is held stores the new one
  (by the priority of §2.1); the same key pressed again stores it again.
- **LEGATO** (the HOLD button) held while pressing the key marks the step legato [CODE
  0x25E-0x265]. Its LED lights while it is held [CODE 0x44A-0x44F].
- **REST** (the KEY TRANSPOSE button) stores a rest, one per press [CODE 0x269-0x275]. Its LED
  lights while it is held [CODE 0x443-0x449].
- While a key is held in LOAD the gate is on and the KCV plays it, so you hear what you enter.
- **Starting from the beginning clears the old sequence.** If the pointer is at 0x1B (LOAD
  pressed from normal playing), the first key or REST clears all 100 bytes before storing
  [CODE 0x21F-0x22F; OM p.35 "If you start re-loading from the beginning, the previous data will
  be all cancelled immediately"]. LOAD pressed and released with nothing entered changes
  nothing [RUN].
- **Editing.** LOAD pressed during PLAY keeps the pointer, so new steps overwrite from the step
  after the one playing, and the rest of the old sequence stays [OM p.35; RUN].
- **The 100-step limit.** After step 100 (0x7F) the LOAD LED stays lit; the next key press
  leaves LOAD and plays as a normal key, without being stored [CODE 0x24F-0x253; RUN; OM p.34 "If
  all 100 steps are stored, the SH-101 will return to normal playing condition, even if the
  sequencer is in LOAD mode"].
- **Quirk: entered with ENV TRIG on LFO, every note is legato.** The firmware adds the TRANSPOSE
  value from RAM 0x17, whose bit 6 is the S7 = LFO flag, so bit 6 of every note byte is set [CODE
  0x212-0x214, 0x080-0x085; RUN]. Played back in GATE or GATE+TRIG those steps have no gate gaps.
- LOAD cancels any key transpose (§4) and leaves the TRANSPOSE switch in effect.

### 5.3 PLAY

[CODE 0x286-0x2D9; OM p.35]

- **PLAY** lights its LED and pulses CLK RST [CODE 0x29C-0x2A3]. Each rising edge at the clock
  input (§7) advances one step; the first edge plays step 1. Before it the gate stays off.
- **The gate per step** [CODE 0x28F-0x295, 0x2C3-0x2D0; RUN]:
  - a note step that is not legato: on the clock-edge loop the gate goes off and the KCV keeps
    the old pitch; on the next loop the new pitch and the gate come on together. The gap is one
    loop (1.95 ms in the self-test);
  - a legato step: the new pitch with no gap;
  - a rest: gate off, the pitch held.
  So the gate is high for the whole step except that one loop at its start, whatever the tempo
  [OM p.35 "All are played in non-legato except for those stored in a legato manner"].
- **Looping.** After the end byte (or step 100) it goes back to step 1 on the same clock edge
  [CODE 0x2AD-0x2C1; OM p.35].
- **No sequence.** With 0x00 at step 1, PLAY waits for the first clock edge, then leaves PLAY
  [CODE 0x2BA-0x2BD; RUN].
- **TRANSPOSE is ignored** during PLAY: the KCV is the stored note + 5 [CODE 0x27A-0x27F; OM
  p.31 "When the built-in sequencer is operating, this TRANSPOSE switch does not work"]. **KEY
  TRANSPOSE** transposes the playing sequence through RANGE [OM p.36; RUN]. Keys pressed alone do
  nothing [RUN].
- **HOLD** (button or pedal) keeps the gate on through every step and rest. Releasing the
  **pedal stops PLAY**; the button doesn't [CODE; RUN].
- **PLAY again** stops, gate off [OM p.33]. Pressing LOAD switches to editing (§5.2).
- With ENV TRIG on LFO the envelope repeats at the clock, giving a different articulation for
  sequences of single steps [OM p.35].

---

## 6. The arpeggio

[CODE 0x2DB-0x3F8; OM p.32]

- **UP, U&D, DOWN** select the pattern; pressing the lit button again turns it off [CODE
  0x192-0x1A6; OM p.32]. The buttons are exclusive (LED code on P20-P22).
- **It runs while two or more keys are held**, or while it is latched by HOLD. A single key
  plays normally, with its own gate [CODE 0x2DC-0x2F8; RUN; OM p.32 "When you press a single tone
  in non-legato manner, it will prove a normal playing"].
- **The keys** are copied into a 32-bit buffer (RAM 0x0F-0x12) every loop while they are held
  [CODE 0x30B-0x318]. The pattern walks it in key order, one key per rising clock edge:
  - **UP**: up from the current note, wrapping to the lowest;
  - **DOWN**: down, wrapping to the highest;
  - **U&D**: up, then down from the top, then up from the bottom; the top and bottom notes are
    played once per turn (C E G C' G E C E …) [RUN].
- **The first note.** Before the first clock edge the arpeggio plays the note it last played (RAM
  0x18), which normally is the first key of the chord, played as a single key in the loop or two
  before the second key arrived. The pattern then continues from that key. So a chord whose
  keys arrive in the same scan starts from whatever note played last [CODE 0x336-0x33A; RUN; OM
  p.32 "If you fail to press each key of the chord at precisely the same moment, the first
  pattern of the Arpeggio may prove imperfect"].
- **The gate stays on** for the whole run; in GATE+TRIG each step retriggers the envelope
  through the KCV change (§2.2), in GATE the envelope runs once, in LFO it repeats at the LFO
  [RUN; OM p.32 diagram].
- **PORTA OFF (P23) is high exactly while the arpeggio runs** [CODE 0x2FA-0x2FD, 0x478-0x47E;
  RUN]. It turns off AUTO portamento (§8): "When an Arpeggio is being played, the portamento
  effect does not work" in AUTO or OFF [OM p.31, p.32].
- **HOLD latches it** (T bit 1): it keeps running after the keys are released [CODE 0x31A-0x323;
  OM p.32]. While latched:
  - keys pressed while some are still held are added to the pattern;
  - a key pressed with none held starts a new pattern from the keys pressed now, so a new chord
    replaces the old one, and a single key replaces it with that note, held [CODE 0x2FA-0x318,
    0x325-0x32B; RUN].
  HOLD off unlatches it, and with no keys held it stops [CODE 0x1D9-0x1DB].
- **TRANSPOSE and RANGE** act at once on the running arpeggio; KEY TRANSPOSE moves a latched one
  [RUN; OM p.36].
- **CLK RST** is pulsed at every first key-down in an arpeggio mode (§2.3), so the MODULATOR
  restarts with the chord.
- With all 32 keys held, UP plays F3 to C6 and round again, one key per clock [RUN].

---

## 7. The clock

- The CPU's clock input is T1 = NOT(CLK) through TR11 [SCH p.9]. CLK is the MODULATOR's square,
  normalled through the EXT CLK IN jack (J8); a plug in EXT CLK IN replaces it [SCH p.7 jack
  board; OM p.38]. A **rising edge at CLK** steps the arpeggio and the sequencer and makes a new
  random number. There is no divider [CODE 0x12D-0x141].
- **T1 is polled once per loop** [CODE 0x12F]: an edge is seen only if one poll finds CLK low and
  the next finds it high. So each half of the clock must last longer than the longest loop in the
  mode (up to 3.7 ms, §1). At 50 % duty that allows about 135 Hz; in the self-test 150 Hz with
  3.3 ms halves loses nothing in PLAY, and 1 ms pulses at 50 Hz lose half the steps [RUN].
- A step happens up to one loop after its clock edge.

### 7.1 RANDOM

On each clock edge the CPU adds the next non-zero byte of program page 1 (0x100-0x1FF, masked to
6 bits; the pointer is RAM 0x14) to RAM 0x13, and writes RAM 0x13 mod 64 to the RANDOM channel
every loop [CODE 0x142-0x158]. IC11a doubles it, giving 0-10 V in 64 steps for the MODULATOR's
RANDOM wave.

- Page 1 has 250 non-zero bytes per pass, whose sum is 32 mod 64, so **the sequence repeats every
  500 clock edges**. It covers every value 0-63, unevenly (2-13 times per period) [RUN].
- INITIAL SET clears 0x13 and 0x14, so after every power-on it starts the same way: **54, 25,
  28, 40, 12, 57, 49, 4, 50, 36, …** [RUN].
- It advances on every clock edge in every mode, whether or not anything else uses the clock.

---

## 8. Portamento AUTO

The CPU's part is PORTA OFF (P23), high only while the arpeggio runs (§6). The rest is on the
bender board [SCH p.7 "PORTAMENTO"]: GATE IN through R15 47 kΩ and C11 0.0047 µF is
differentiated, and in the AUTO position of S3 the pulse at each gate rise turns on TR5/TR4 and
the FET TR1, which shorts the portamento pot for a moment, so a new note after a gap jumps and
only legato notes glide [OM p.31 "only when you are playing in a legato manner"]. PORTA OFF
through R16 220 kΩ holds the same node on, so in AUTO an arpeggio, whose gate never drops,
doesn't glide [OM p.32]. In ON the arpeggio glides. The emulation of this circuit belongs to the
oscillator section (`core/osc.*`).

---

## 9. Power

- **Reset** runs the power-on code at 0x000. With LOAD and KEY TRANSPOSE held it enters **test
  mode**, which outputs the service notes' calibration voltages per button (LOAD, PLAY, DOWN,
  U&D, UP: KCV 0, 2.75, 2.5, 4.75, 0 V; RANGE 0, 0, 0, 0, 4.75 V) until HOLD is pressed [CODE
  0x000-0x04F; SN p.4; RUN].
- **INITIAL SET** clears RAM 0x02-0x1B: the keyboard state, mode, HOLD, key transpose, random
  generator and step pointer [CODE 0x051-0x058; SN p.4 "does not delete the Sequencer data"].
  The sequence (0x1C-0x7F) is kept.
- **Without batteries** the RAM comes up with whatever the chip holds. INITIAL SET still clears
  0x02-0x1B, so only the sequence is random: PLAY plays the garbage up to the first 0x00 byte
  [CODE; RUN with a junk RAM image].
- **The emulation's state** is the 128 bytes of RAM behind a 5-byte header
  (`Machine::saveState`); `loadState` + `powerOn` is a cold boot with batteries in, which keeps the
  sequence [RUN; self-test "Power"].

---

## 10. In the plugin

What `plugin/PluginProcessor.cpp` and `core/midi.h` add around the firmware. None of it changes
what the firmware does.

- **MIDI notes press keys** (`KeyboardDriver`). MIDI 53-84 are the 32 keys at the panel's
  TRANSPOSE; notes outside move the TRANSPOSE switch an octave for as long as they are the newest
  note, and notes beyond L and H fold by octaves. The driver plays the keyboard as fingers can:
  - every key change lasts at least two loop boundaries, so the firmware sees every press and
    release (§2.2);
  - a note-on for a key that is still down (a repeated note, note-off and note-on at the same
    instant) lifts the key first, so the gate drops for a loop, as a repeated key does;
  - with an arpeggio mode on, a chord arriving all at once is pressed lowest key first (highest
    for DOWN) and the rest two loops later, so the pattern starts on that key (§6). In normal
    playing chords are pressed at once and the firmware picks the note;
  - with no notes held, the TRANSPOSE switch goes back to the panel's position only after the
    release has registered.
- **CC 64** is the HOLD pedal; **all notes off** releases every key.
- **SYNC** is a cable in EXT CLK IN. With SYNC on, the host transport is the clock (a square at
  the CLOCK division: 1/4 to 1/32), and while the host is stopped there is none, so a running
  arpeggio or sequence waits. The first edge falls on the first grid line at or after where the
  transport starts. With SYNC off (the default) the MODULATOR's square is the clock.
- The arpeggio and sequencer only run when PLAY or an arpeggio button is pressed on the panel;
  MIDI notes alone never start them.
- **No sequence at first.** Like a new unit, a new instance's RAM holds no sequence (the end
  marker at 1C); a session's RAM replaces it. `Machine::demoSequence()` stays for the tests and
  tools: entered at TRANSPOSE L, C3 C3 – C4 B♭3~ G3 – C3 E♭3 – F3 G3~ – C3 C4 B♭3 (16 steps; ~ =
  legato, – = rest).

---

## 11. Open questions

1. The HOLD jack's normalling and the pedal polarity that gives T0 low (§3).
2. The TRIG circuit's input (§2.2) and the CLK RST effect on the MODULATOR (does the square
   restart high, giving an immediate step?): both for the synth-board transcription.
3. Whether loop jitter is worth modelling beyond what the emulated CPU already gives: the
   emulation runs the firmware cycle by cycle, so the latencies above are exact for the
   MAME cycle counts.
