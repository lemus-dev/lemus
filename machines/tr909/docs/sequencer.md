# TR-909 sequencer: behaviour, and how Lemus runs it

The sequencer is the original firmware (v4.0, `tr909_v4.ic609`) on the emulated uPD7810 and CPU
board (`docs/firmware.md`), so its behaviour is the 909's own: this document says what that
behaviour is, which parts were checked against the owner's manual, and how. Without the ROM there
is no sequencer: the voices play from MIDI only.

## Evidence labels

| Label | Meaning |
|---|---|
| **[OM]** | Roland *TR-909 Owner's Manual*, Dec. 1983, printed page numbers. `private/docs/tr909-owners-manual.pdf` |
| **[SN]** | *TR-909 Service Notes*, 1st ed., 15 Jun 1984. `private/docs/tr909-service-notes.pdf` |
| **[FW]** | Checked by running the firmware with `tools/tr909seq`; `tools/tr909seq/tests.py` has the check of the same name |
| **(?)** | The manuals don't settle it |

## 1. Structure

- Two banks (I, II), each of three pattern groups of 16 patterns: 48 patterns a bank [OM p.12].
  A pattern has 1-16 steps (LAST STEP) at one of four scales: 16th, 32nd, 8th triplet, 16th
  triplet [OM p.24]; adjoining patterns can be chained [OM p.28].
- Each pattern holds the eleven voices (BD, SD, the toms and CH with normal, accent and, for
  BD-HT, flam; RS, HC, OH, crash, ride on/off), TOTAL ACCENT per step, and 16 external
  instruments with their own accent per step (EXT INST) that share the scale, last step and
  shuffle [OM p.37].
- Four tracks a bank, 896 measures in all, one pattern (group and number) a measure, with a
  tempo memory each [OM p.15, p.17, p.32].
- The RAM format is in `docs/firmware.md` ("RAM layout"); a bank is 4 KB, the same on a
  cartridge and in a system exclusive dump.

## 2. Writing a pattern [FW]

**Step write** [OM p.9-11]: stopped, SHIFT + PATTERN n (its LED blinks: pattern write, group
n); CLEAR + key k erases pattern k and selects it; START (the step LEDs run); INSTRUMENT SELECT +
a voice's key; then the step keys write it. The manual's example (CH on every step, SD on 5 and
13, BD on 1, 8, 10, 11, 14, TOTAL ACCENT on 1, 5, 9, 13) writes and plays as charted ("step
write example").

- BD, SD, the toms and CH: a step key goes none -> normal -> accent -> none (either key of the
  voice); the step LED is dim for normal, bright for accent [OM p.11, p.25] ("key cycle", "step
  LEDs": on about one multiplex pass in four, against every pass). RS, HC, OH, crash, ride: on /
  off. OH is the two CH keys together. ("key cycle")
- INST SELECT + both keys of BD-HT selects flam write [OM p.10, p.26]; INST SELECT + ENTER selects
  TOTAL ACCENT [OM p.25].
- The step LEDs show the selected voice, the TEMPO (STEP) LED step write, FWD / AVAILABLE MEAS
  the bank.

**Tap write** [OM p.14]: SHIFT + PATTERN 3, SHIFT + TAP (BACK lights instead of TEMPO), CLEAR
+ 1, START; CYCLE/GUIDE turns the guide on: a rim shot on every beat, louder on the bar's first
("tap write and guide": DAC6 codes 22 and 7). A key writes its voice at the nearest step, the
left key of a voice accented, the right one normal. CLEAR held with a voice's key erases it as
the steps pass, for as long as both are held: the left key erases its accented notes, the right
key its normal ones (the manual says "3 or 4" for SD) ("tap write CLEAR").

**Scale, last step** [OM p.24] (while it runs): SCALE steps 16th -> 32nd -> 8th triplet -> 16th
triplet -> 16th, lighting one of the four scale LEDs, and sets the last step to 16, 16, 12, 12
("scale", "scale sets last step": 12, 6, 16 and 8 ticks of the 48 ppqn clock); LAST STEP + key
n sets n steps ("last step").

**Accent** [OM p.25]: TOTAL ACCENT on a step raises every voice on it by the TOTAL ACCENT knob,
read when the step plays (32 positions); a voice's own accent is fixed. With the knob high the
voice accent may not be noticed, as the manual says: normal + full TOTAL ACCENT is the accent's
code (`docs/firmware.md`, "Accent codes"; "total accent", "accent codes").

**Shuffle** [OM p.26] (while it runs): SHUFFLE/FLAM + key 1-7 delays every second 16th by
key - 1 ticks of 1/48 beat (key 1 none, key 7 a 75 % swing) ("shuffle 16th"); in 32nds 1 = 2,
3 = 4, 5 = 6, 7 ("shuffle 32nd"); no shuffle on triplets, and an odd last step keeps its length
("shuffle off on triplets and an odd last step"). SHUFFLE/FLAM held lights the shuffle key and
the flam key ("shuffle/flam display").

**Flam** [OM p.26]: SHUFFLE/FLAM + key 9-16 sets the interval: 16-20 ms (key 9) to 44-49 ms
(key 16), longer towards 16, the same at any tempo; the grace note on the step at normal level,
the main note after it at accent level ("flam intervals", "flam levels and tempo"). The interval
is one of two values a group period (about 4.07 ms) apart, set by the step's timing against the
scan's two trigger groups (`docs/firmware.md`, "Timing"); HT always has the longer one, and so
has the first flam after START ("flam after START"). The real unit's recordings agree to 0.7 ms.

**EXT INST** [OM p.37]: in pattern write, stopped, SHIFT + EXT INST switches the keys to 16
external instruments (EXT INST lit); they are written as the voices are (INST SELECT + key,
then steps; no flam; INST SELECT + ENTER is their accent) and play as MIDI notes 36-51 on the
transmit channel, in both modes ("EXT INST").

**CLEAR** [OM p.9, p.37]: CLEAR + key in internal mode erases the voices and TOTAL ACCENT and
resets the scale, last step, shuffle and flam (16 steps, 16th, no shuffle, flam key 13); in EXT
INST mode only the external rows and their accent ("CLEAR").

**Copy** [OM p.27]: in pattern write, SHIFT + PATTERN group and the key pick the destination;
SHIFT held: COPY, BANK, PATTERN group, key, ENTER copies that pattern there, header included,
from either bank ("pattern copy").

**Chain** [OM p.28]: in pattern write, the two end keys of a run of patterns held + ENTER chain
them; any of them then plays the whole chain from its first pattern; a key held + ENTER takes
that pattern out, which leaves the patterns after it chained, as the manual's example says
("chain").

**Banks** [OM p.12-13]: in track mode, SHIFT + BANK I (FWD) / II (AVAILABLE MEAS); the bank's
LED lights ("bank").

**Key timing.** The firmware reads each key row every 2 ms, and some functions wait for a key's
release:

- A modifier (SHIFT, INST SELECT, CLEAR, SHUFFLE/FLAM, LAST STEP) must be down before the key it
  modifies: pressed in the same scan, the key may act unmodified. `tests.py` holds it 50 ms first.
- PATTERN group and the pattern number are pressed one after the other, not together: the
  firmware waits for the group key's release and ignores a number pressed meanwhile [OM p.14:
  "a Pattern Group Button and a Main Key"]. The same for TRACK.
- The two keys of a voice (flam, OH) pressed inside one scan: for keys 9-16 (HT, OH) they register
  as one key; a few ms apart they work. Keys 1-8 work either way. (A firmware property, kept.)

## 3. Playing

**Pattern play** [OM p.14] [FW] ("pattern play"): PATTERN group, then the number; START; a new
number while it plays starts at the next bar; STOP/CONT stops, STOP/CONT again continues where it
stopped; two numbers pressed together play the patterns between them in turn (block play).

**Track play** [OM p.18] [FW]: TRACK n selects the track at its first measure (an empty one
shows 0, "empty track"); START plays it and stops at its end ("track write"); STOP/CONT stops and
continues; CYCLE (its LED lit) repeats it, and CYCLE again while it plays stops it at the end
("track play: cycle, measure display"); SHIFT + measure number + ENTER, then STOP/CONT, plays from
that measure (START still plays from measure 1) ("track play from a measure"). The display
shows the measure playing (MEAS/TEMPO), and the tempo while TEMPO is held.

**Foot switch** [OM p.38]: starts and stops, like START then STOP ("foot switch").

## 4. Track write and edit [OM p.15-17, p.28-31] [FW]

- **Write** ("track write"): SHIFT + TRACK n (track write); SHIFT held: DEL, LAST MEAS, ENTER
  clears the track; START; then PATTERN group, number, ENTER for each measure: the display shows
  the next measure's number. A block of patterns (two numbers together) enters as many measures.
  AVAILABLE MEAS, held in track write, shows the measures left of the bank's 896.
- **Replace** ("track edit: replace, BACK"): in track write, START, FWD (or ENTER) to the measure,
  the new pattern, ENTER; BACK steps back. The count stays.
- **Recall**: SHIFT + number + ENTER goes to a measure, and sends MIDI song position.
- **Delete** ("track edit: delete"): from the recalled measure, SHIFT: DEL, ENTER deletes it;
  SHIFT: DEL, number, ENTER deletes to that measure; SHIFT: DEL, LAST MEAS, ENTER to the end.
- **Insert** ("track edit: insert"): recall the measure, select the pattern, SHIFT: INS, ENTER
  inserts it before the recalled measure.
- **Copy** ("track edit: copy"): recall the destination; SHIFT: COPY, first, ENTER, last, ENTER
  inserts a copy of the series there. **A single measure (COPY, number, ENTER, ENTER) is not
  copied**: the track is left as it was. The manual describes it; the Revolution 909 v5 manual
  lists it among the original firmware's faults ("although this function is written in the
  original manual, it did not work"). Kept.
- **Memory full** ("track memory full"): after 896 measures AVAILABLE MEAS shows 0, and ENTER
  shows 0 while it is held "instead of the next measure number" and adds nothing [OM p.17]. The
  measures share their RAM bytes with the patterns' accent bits (`docs/firmware.md`).

## 5. Tempo and the display [OM p.18, p.32, p.34] [FW]

- TEMPO: 36.4-290 BPM in 224 steps of about 1.14 BPM, from the knob's A/D code with a 1-count
  dead band; crystal-timed. The display (TEMPO held) shows the A/D value + 36, within a BPM of
  the tempo: 37 at the left end, 290 at the right ("tempo display").
- **Tempo memory** ("tempo memory"): TEMPO + ENTER stores the tempo in the selected track; TEMPO
  + TRACK n selects the track at its stored tempo, and the knob then does nothing; holding TEMPO
  and turning the knob gives the tempo back to the knob ("rotate the Tempo Knob through left to
  right while holding Tempo Button"). The stored tempo itself stays: TEMPO + TRACK n recalls it
  again, and a new TEMPO + ENTER replaces it. The manual calls this erasing the tempo data (?).

## 6. Sync and MIDI [OM p.34-38] [FW]

- **Sync modes** ("sync: MIDI clock", "sync: DIN"): SHIFT + TEMPO MODE, stopped, steps INT ->
  MIDI -> DIN; the TEMPO display shows the tempo, a middle 0, a left 0. In MIDI and DIN, START and
  STOP/CONT do nothing. MIDI: FA starts on the next F8, a 16th is 6 clocks, FB continues, FC
  stops. DIN: START/STOP high runs it on the clock, CONTINUE with it continues. Each step comes
  0.8-5 ms after its clock (`docs/firmware.md`, "Timing").
- **TAPE SYNC** [OM p.35-36] ("tape sync output", "TAPE SYNC mode without a tape"): the TAPE SYNC
  output always carries the FSK tone, 1202 Hz stopped, alternating 1202/2404 Hz with the 48 ppqn
  clock while it plays (record it to sync to later). TAPE SYNC lights its LED and makes START wait
  for that tone on the input: with no tape nothing plays; STOP/CONT leaves the mode.
- **MIDI channels** [OM p.38] ("MIDI channels and modes"): at power-on the 909 receives omni and
  sends on 11. In track play, SHUFFLE/FLAM + key sets the receive channel (omni off) and LAST
  STEP + key the send channel (it then sends OMNI OFF and POLY ON there). Not kept over power-off.
- **MIDI in/out** ("MIDI in: notes and velocity", "MIDI thru, song select, song position",
  "system exclusive bank dump"): notes 35-51 play the voices at the velocity's level; start,
  continue, stop, song select and song position are followed; MIDI out carries the clock, start/
  stop, song select and position, the external instruments' notes, a copy of MIDI in, and the
  system exclusive bank dump and load. Details in `docs/firmware.md` ("MIDI").

## 7. Tape and cartridge [OM p.19-23, p.33] [FW]

- **SAVE** (SHIFT + SAVE, ENTER) needs no tape and runs: about 6 s of pilot tone on the SAVE/OUT
  jack's data output, then the data (7 s for a nearly empty bank; a 1 bit takes longer than a 0,
  and the manual gives about 13 s), a second of tone, and back; the SAVE key's LED and two scale
  LEDs (SCALE 1 + 2 for the tone, 1 + 3 for the data) show it [OM p.20] ("tape SAVE, LOAD without a
  tape"). **VERIFY** and **LOAD** wait for a pilot tone that never comes, MIDI frozen meanwhile
  (as SN p.15 says); key 1 cancels and the memory is untouched ("tape VERIFY without a tape").
  The tape input is not emulated.
- **Cartridge**: none is inserted (the sense line reads high): ENTER does not switch to
  cartridge mode, and SHIFT: COPY, SAVE/LOAD, BANK, ENTER do nothing ("cartridge absent"). With
  the sense line low (`Machine::setAnalog(1, 0)`, an M-64C) ENTER does switch, but the slot
  itself reads FFh: a cartridge is not emulated.

## 8. Factory patterns [SN p.7, OM p.7]

A new 909 holds the demonstration in Bank I: patterns 1-8 of group 1 and track 1 (65 measures,
from pattern 5, with a tempo memory of 110 BPM). Powering on and pressing START plays it (the
display shows measure 1). Only v1.0-v3 carry it, loaded by powering up with TRACK 1 and PATTERN 1
held; v4.0 does nothing for those keys ("factory patterns"). `core/factory.h` makes that memory
from the user's v1.0 ROM by running its loader, for a v4.0 machine to start from; v4.0 plays it
as v1.0 does (`docs/firmware.md`, "Factory patterns").

## 9. Checks

`tools/tr909seq/tests.py` runs 54 checks (about 25 s); every procedure above behaves as the
manual says, with these differences, all the firmware's own:

| Procedure | Manual | Firmware |
|---|---|---|
| Track copy of one measure [OM p.31] | "skip assigning the last measure" | nothing is copied |
| Tap write CLEAR [OM p.14] | "3 or 4" for SD | the left key erases the accented notes, the right key the normal ones |
| Tempo memory erase [OM p.32] | "erase the tempo data" | the knob takes over; the stored tempo stays |
| CLEAR in EXT INST mode [OM p.37] | scale, last step, shuffle, flam "will remain" | they remain, and so do the internal voices |
| System reset FF [OM, MIDI implementation chart] | "Reset O" | passed to MIDI out, no other effect |

## 10. Open points

- The v4 "freezes when synchronized to DIN" bug (Revolution 909 v5 manual): not reproduced with
  steady clocks; the conditions are unknown.
- TAPE SYNC, LOAD and VERIFY from a tape: the tape input (INT2, FSK half periods timed with ECNT)
  is not driven.
- Group A (unlabelled) ROMs and Ver. 2: not dumped; the SN's track 4 procedure is for them.
