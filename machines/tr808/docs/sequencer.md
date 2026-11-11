# TR-808 sequencer: behaviour, and how Lemus runs it

The sequencer is the original firmware (`d650c-085.ic4`) on the emulated uCOM-43 and CPU board
(`docs/firmware.md`), so its behaviour is the 808's own: this document says what that behaviour is,
which parts were checked against the owner's manual, and what the plugin adds around it (host
sync, MIDI, the panel). Without the ROM there is no sequencer: the voices play from MIDI only.

## Evidence labels

| Label | Meaning |
|---|---|
| **[OM]** | Roland *TR-808 Operation Manual* (English), printed page numbers. `private/docs/tr808-owners-manual.pdf` |
| **[JA]** | The much longer Japanese owner's manual, used where the English one is vague. `private/docs/tr808-owners-manual-ja.pdf` |
| **[IN]** | *Introduction for easier operation*. `private/docs/tr808-introduction-for-easier-operation.pdf` |
| **[FW]** | Checked by running the real firmware with `tools/tr808seq` |
| **(?)** | The manuals don't settle it |

A digest of all three manuals, with page references and every rhythm chart transcribed, is in
`private/docs/operation-digest.md`.

## 1. Structure

- 32 patterns: BASIC RHYTHM 1-12 and INTRO/FILL IN 1-4, each with variations A and B (the I/F
  keys are step keys 13-16) [OM p.22].
- A pattern is a 1st part of 1-16 steps and a 2nd part of 0-16, so 1-32 steps; PATTERN CLEAR
  leaves 16 + 0 [OM p.15, 17]. A and B of one key share both lengths and the pre-scale [OM p.16,
  JA p.18, 22] [FW].
- PRE-SCALE 1-4 sets the steps per beat: 3, 6, 4, 8 (8th triplets, 16th triplets, 16ths, 32nds)
  [JA p.22]; the firmware runs them as 8, 4, 6 and 3 ticks of the 24 ppqn clock [FW].
- Accent is instrument 1 (AC), written like any voice [OM p.3] [FW].
- 12 rhythm tracks of 64 bars, one rhythm *number* per bar (no variation, no pattern data); a
  composition runs on into the next track, track 12 wraps to 1 [OM p.19, JA p.41].

## 2. Writing a pattern [FW]

Each step below was scripted with `tr808seq` and the RAM inspected afterwards:

1. MODE → PATTERN CLEAR, pick the key and the variation (AB clears both), press CLEAR.
2. MODE → 1st PART, pick the key, START. The pattern loops while you write.
3. Set PRE-SCALE and press CLEAR while running: CLEAR latches the pre-scale (it doesn't apply
   until then).
4. INSTRUMENT-SELECT picks the voice (or AC); a step key toggles it on that step of the part
   MODE selects. The step LEDs show that voice's steps in that part, with a running light.
5. CLEAR held + step key n sets the selected part's length to n.
6. MODE → 2nd PART to write the second bar's half (it starts empty: 0 steps).
7. TAP writes the selected instrument at the nearest step in real time [JA p.33].

**Key timing.** The firmware reads the step keys once per step boundary and clears its key latch
each step, so a press shorter than a step can be missed, and two presses inside one step register
as one. This is the firmware's behaviour, kept as is: hold a key for about a step at slow
tempos. `tr808seq` holds keys 150 ms by default.

## 3. Playing

**MANUAL PLAY** [OM p.11-12, JA p.8-12] [FW]

- A BASIC RHYTHM key queues that rhythm; it starts at the next bar. Selected during the bar's
  first beat, it starts at beat 2 of the same bar, from its own beat 2.
- BASIC VARIATION: A, B, or AB alternating every bar (starting on A); a change takes effect at
  the next bar. The A/B LEDs show what is playing.
- TAP while stopped arms the selected I/F as an intro (its LED blinks); while running, TAP plays
  the selected I/F once in place of the next bar. I/F VARIATION picks its variation.
- AUTO FILL IN (2, 4, 8, 12, 16) inserts the selected I/F every N bars; the intro isn't counted.

**PLAY** (rhythm track) [OM p.19, JA p.38, 41-42]: plays the selected track from bar 1, loops to
bar 1 of the start track. A rhythm key pressed while it plays replaces one bar (not recorded).
TAP and AUTO FILL IN do nothing here. AB alternates by absolute bar number.

**COMPOSE** [OM p.19-21, JA p.36-43] [FW]: real time, not step entry. CLEAR (stopped) clears the
track; START records the rhythm number of every bar played, as you switch rhythms the MANUAL PLAY
way; the bar in which you press STOP is the last. Without CLEAR, START plays the track and key
presses overwrite bars (edit mode; the length can't change).

The mode switch should only be moved while stopped [IN p.2]. RAM writes are blocked in PLAY and
MANUAL PLAY by the mode switch's second wafer (SW1b), which is also how the firmware tells PLAY
from COMPOSE (they share a code) [FW].

## 4. Timing and sync

- The internal clock: TEMPO and FINE drive the anti-log oscillator, halved into 24 ppqn
  (`TempoClock`). Full TEMPO and FINE is 300 BPM (8.33 ms a clock, TM-2), full anticlockwise
  TEMPO about 70 ms a clock (the notes: 65 ± 5 ms). START holds the oscillator for 9 ms, so the
  first clock edge comes 9 ms after START and the clock runs in phase from there.
- A step starts up to ~2 ms after the clock edge that makes it (the firmware polls; INT is
  1.9 ms), so steps jitter by about 2 ms, as on the hardware.
- **SYNC** (plugin, off by default): with SYNC on and the host playing, the host's transport is
  the DIN sync input: RUN follows play, and a 24 ppqn clock follows the host's beat position
  (its rising edges 2 ms after the grid). The firmware steps on the first rising edge after RUN,
  so steps land 4.5-5 ms after the host's grid (`tr808plugintest`: 16 of 16 onsets within 12 ms
  of the grid, the first at 5.0 ms). The pattern starts where the host starts; there is no song
  position (the 808 has none). When the host stops, the 808 stops; START/STOP runs it on its own
  again afterwards.
- RESET & START (DIN pin 4) is wired in `Machine` but the plugin doesn't drive it.

## 5. MIDI and the panel (plugin)

- **MIDI** plays the voices directly, alongside the sequencer, as Roland's TR-08 does: General
  MIDI drum notes (36/35 BD, 38/40 SD, 37 RS, 75 claves, 39 CP, 70 maracas, 41/43 LT, 45/47 MT,
  48/50 HT, 64/63/62 low/mid/high conga, 42/44 CH, 46 OH, 49/57 CY, 56 CB). A note makes its own
  1 ms common trig with its voice's line; notes in the same sample share one trigger, as voices on
  one step do. Velocity 100 or more adds ACCENT. The conga, claves and maracas notes play their
  shared rows: the voice switches decide which sound that row makes, as on the panel. CC 7 is
  MASTER VOLUME.
- **The 1st/2nd PART and A/B LEDs** show the part and the variation playing, in every mode (in
  1st/2nd PART mode too, not the part MODE selects); stopped, 1st PART and the selected
  variation. The manuals don't say; it is what the firmware does on the PCB's wiring (the
  schematic swaps the two part LEDs: `docs/firmware.md`, LEDs) [FW].
- **Panel keys** go through a queue into the next audio block: step keys, CLEAR, TAP, START/STOP.
  The computer keyboard plays them (`?` in the bar lists the keys): 1-8 and Q-I are step keys
  1-16, Backspace CLEAR, Return TAP, F1-F6 the mode switch; Space is START/STOP in the standalone
  app only (in a DAW it goes to the DAW's transport).
- **Memory** is the battery-backed RAM, saved with the session. A new instance starts with the
  factory rhythms (§6), and so does the host's State: Reset (a new instance). Without the ROM (voice mode) a
  session keeps its RAM, and it plays again when reopened with the ROM.
- The MULTI OUT jacks are plugin outputs (the README, "Outputs").
- Not emulated: TRIGGER OUT (CB, CP, AC), the footswitch jacks, SYNC OUT.

## 6. Factory rhythms

A new instance's memory is a new 808's, as the rhythm charts printed with it give it (RC p.1-7;
`core/factory.cpp` has each chart as a grid, `x` a hit and `a` an accent):

| Key | Contents |
|---|---|
| BASIC RHYTHM 1, 2 | "Practice Program": no chart, empty here (16 steps, PRE-SCALE 3) |
| BASIC RHYTHM 3-6 | ROCK, BOSSA NOVA, MAMBO, SAMBA, A and B (16 steps, PRE-SCALE 3) |
| BASIC RHYTHM 7-11 | "The Rhythm For Track Memory", A drum kit, B mostly the latin voices |
| BASIC RHYTHM 12 | "The Rhythm For Checking Sounds": every voice on all 16 steps, A and B (the chart is notation only; whether it accents can't be read, and here it doesn't) |
| INTRO/FILL IN 1 | Fill in 1, A and B |
| INTRO/FILL IN 2 | Fill in 2, A only (B empty) |
| INTRO/FILL IN 3 | Fill in 3, A only: 12 + 12 steps at PRE-SCALE 2 (16th-note triplets) |
| INTRO/FILL IN 4 | "Practice Program": empty |

A row is a RAM row: LT plays LC, MT MC, HT HC, RS CL and CP MA when the voice switches are up, as
the charts' "LT(LC)" labels mean. The factory had composed two rhythm tracks: 1 rock (voice switches down, BASIC
VARIATION A) and 2 latin (switches up but CP, variation B) [IN p.8, JA p.6-7]. Their bar lists
are in no document we have, so the tracks start empty. Checked with `tr808seq` (`factory`):
ROCK A and B, Fill in 3 as an intro into BASIC RHYTHM 12, each step's voices as charted.

## 7. Open points

- Other points the manuals leave open (the digest's §7): whether AUTO FILL replaces bar N
  or follows N bars (the firmware decides; not separately measured here), the DIN clock rate
  (24 ppqn on the CPU board's clock input, as the tempo oscillator's divider gives).
