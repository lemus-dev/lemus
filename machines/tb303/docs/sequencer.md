# TB-303 sequencer: behavioural specification

This document describes the built-in sequencer of the Roland TB-303 Bass Line (1981/82). It is written as a specification for re-implementing the sequencer, or for checking an emulation of the original firmware (`rom/d650c-133.ic8` under MAME `roland_tb303.cpp`).

## Evidence labels

| Label | Meaning |
|---|---|
| **[OM]** | Roland *TB-303 Owner's Manual* ("'82 APR."), cited by its printed page numbers. Scans: <https://archive.org/details/synthmanual-roland-tb-303-owners-manual>, <https://archive.org/details/RolandTB303OwnersManual>. Local copies are in `private/docs/photos/owners-manual-panel/`. |
| **[SN]** | Roland *TB-303 Service Notes*, First Edition, Feb. 19 1982: spec page, block diagram, schematic, CPU port table, adjustments. <https://archive.org/details/synthmanual-roland-tb-303-service-notes> |
| **[REF]** | A reliable secondary source, named each time: logic-analyser studies (Sonic Potions/RE-303, Ultra303), Robin Whittle (Devil Fish), MAME, x0xb0x firmware, later clones' manuals. |
| **[UNCERTAIN]** | Folklore, inference, or a point where sources disagree. The reason is given. |

Terms used throughout:

- A **step** is a 1/16 note, or 1/3 of a beat in triplet mode.
- A **tick** is one pulse of the 24 ppqn tempo clock.

---

## 1. Hardware context

| Item | Value | Source |
|---|---|---|
| CPU | NEC µPD650C-133, a CMOS mask-ROM µCOM-43 (4-bit, 2 K × 8 ROM, 96 × 4 RAM). Clock from an LC tank, period marked 2.2 µs (≈455 kHz; MAME uses 454 545 Hz). | [SN] parts list and schematic; [REF] MAME; Whittle, <http://www.firstpr.com.au/rwi/dfish/> |
| Interrupt | A separate oscillator on `INT`, trimmed to **1.8 ms ± 0.2 ms** (TM2, TP2). | [SN] p.6 "INT CLOCK" |
| Pattern/track RAM | 3 × µPD444C 1024 × 4 static CMOS RAM, battery-backed. IC3 holds groups I and II, IC4 holds groups III and IV, IC5 holds the "pattern edit buffer and Tracks". | [SN]; [REF] MAME; [REF] Whittle, *303 mods* §3.7, <http://www.firstpr.com.au/rwi/dfish/303-mods/> |
| CPU ports | PA: switches plus a status input (TEMPO CLOCK, START/STOP, TAP). PB: switches. PH: scan. PG: LEDs. PD/PE/PF: RAM address, with PD and PF doubling as **pitch data**. PC: RAM data. **PI0**: RAM /WE. **PI1**: "pitch data latch strobe". **PI2**: "gate signal". | [SN] "µPD650C functional description" |
| CV DAC | IC9 (MC14174 hex D flip-flop) latches 6 bits into a 0.1 %-matched resistor ladder and op-amp: a **6-bit DAC**. | [SN]; [REF] Whittle ("the TB-303's 6 bit DAC") |
| Accent latch | IC13 (MC14013 D flip-flop). | [SN] |
| Slide | IC12 (4066 switch) and C35 (0.22 µF) form the RC glide on the DAC output. | [SN] |

**One CPU line is both the latch strobe and the slide control.** An original CPU was measured with a logic analyser. The DAC note bits and the accent flip-flop are latched by the same line that switches the slide RC, so a new note or accent takes effect only on a pulse of that line:

- a note without slide gets a strobe pulse of about **44 µs** at its start;
- during a slide the line stays high and drops for about **8.75 µs** to latch each new note.

Source: [REF] Julian Schmidt, *Analysis of the µPD650C-133 CPU timing*, Sonic Potions 2017, §3 and §6, <https://www.tubeohm.com/wa_files/analysis_of_the_d650c-133_cpu_timing.pdf>.

The Ultra303 replacement-CPU project found the same constraint: "the accent signal has to be set before the dac strobe ... because the dac strobe is also the strobe for the accent latch ... otherwise the accent is shifted to the next note" ([REF] Ultra303 diary, 24.05.2004, <http://ultra303.ultrafex.de/diary.phtml>).

---

## 2. Front panel

### 2.1 Controls

| Control | Function | Source |
|---|---|---|
| MODE (4-position rotary) | TRACK WRITE, TRACK PLAY, PATTERN PLAY, PATTERN WRITE | [OM] p.9, p.89 |
| TRACK/PATT. GROUP (7-position rotary) | Positions 1–7 select the track. Positions 1–2 select pattern group I, 3–4 group II, 5–6 group III, 7 group IV. | [OM] p.11, p.65; [REF] MAME |
| TEMPO | ♩ = 40–300 | [OM] p.89; [SN] |
| RUN/STOP and its "RUN / BATTERY" LED | Start/stop; "the TB-303 stops immediately". The LED is lit while running and flashes when the batteries are low. | [OM] p.3, p.17 |
| CLEAR/RESET (legend "D.C. \| BAR RESET / PATTERN CLEAR") | PATTERN CLEAR, BAR RESET, or D.C. | [OM] p.11, p.24, p.28 |
| PITCH MODE, TIME MODE, FUNCTION (FUNCTION is labelled BAR beneath, with a "NORMAL MODE" LED) | Sub-mode keys; see §2.2 | [OM] p.12–14, p.69 |
| BACK (segno 𝄋 above) | Back one note, or set the segno | [OM] p.13, p.70 |
| TAP (legend "D.S." / "WRITE/NEXT" / "TAP") | Step, write/next, or D.S. | [OM] p.13, p.25, p.71 |
| 13 note keys C…C′ with LEDs | The white keys are also **pattern selectors 1–8**. C♯ is **DEL**, D♯ is **INS**. | [OM] p.10–12; [REF] MAME |
| DOWN, UP, ACCENT, SLIDE with LEDs | In each mode these four keys mean:<br>• Pitch: octave down, octave up, accent, slide.<br>• Time: **note ●**, **tie ○**, **rest –**.<br>• Normal: STEP, triplet ("3n"), section **A**, section **B**.<br>• BAR digit entry: 9, 0, 100, 200. | [OM] p.14, p.51–54, p.69; [REF] MAME (`"9 Step"`, `"0 3n"`, `"100 A"`, `"200 B"`) |

### 2.2 Key function by mode

| Key | Pattern Write: normal | Pattern Write: pitch | Pattern Write: time | Pattern Play | Track Write | Track Play |
|---|---|---|---|---|---|---|
| White 1–8 | Select pattern | Write pitch and advance | – | Select pattern; two keys in one half chain them | Select pattern for this measure | – |
| Black keys | – | Write pitch | – | – | – | – |
| PITCH MODE + key | – | – | – | Shift, from next bar (§5.3) | Write this measure's shift | Hold to show the shift |
| DOWN | FUNCTION+DOWN: STEP count | Hold + key: octave down | NOTE | – | – | – |
| UP | FUNCTION+UP: triplet | Hold + key: octave up | TIE | – | – | – |
| ACCENT | Section A | TAP held + ACCENT: toggle accent | REST | Section A | Section A | – |
| SLIDE | Section B | TAP held + SLIDE: toggle slide | – | Section B | Section B | – |
| TAP | Tap-write while running (§4.7) | Next note (sounds it) | Next step (shows it) | "Do not press"; used when tuning | WRITE/NEXT | D.S. |
| BACK | – | Back one note | – | – | Set segno | – |
| CLEAR | Hold pattern key + CLEAR: clear | – | – | – | BAR RESET, or D.C. | BAR RESET |
| FUNCTION | Hold for step settings | Exit to normal | Exit to normal | – | BAR (+digits), +DEL, +INS | BAR (+digits) |

All entries are from [OM] pp. 9–73 and its procedure table. A dash means the manual documents no function.

---

## 3. Memory model

### 3.1 Capacity

| Item | Value | Source |
|---|---|---|
| Patterns | **64** = 8 selectors × 2 sections (A/B) × 4 groups (I–IV) | [OM] p.11, p.89; [SN] |
| Steps per pattern | 1–16 in sixteenth mode; 1–15 in triplet mode | [OM] p.57, p.89; [SN] "1 to 16 steps (2, 3 or 4 meter) / 1 to 15 steps (2, 3, 4 or 5 meter)" |
| Pattern contents | "Pitch, Length of Note, Accent, Slide", plus step mode and step count | [OM] p.89 |
| Range | Pattern: 3 octaves. Track: 4 octaves, because shift adds 0 to +12. | [OM] p.79, p.89 |
| Tracks | **7** × 64 measures. A song may continue into the following tracks, up to 256 measures (§6.5). | [OM] p.64, p.66, p.89 |
| Track contents | "Order of Patterns, Shift of Patterns, D.C., 𝄋" | [OM] p.89 |

### 3.2 Logical data model

The bit layout in the RAM has not been published. The structure below is the logical model the manual implies, and the one the RE-303 replacement CPU also uses. It is **not** a 16-step grid of (note, gate) pairs.

```c
struct Pattern {
    bool    triplet;               // false: 4 steps/beat (1/16); true: 3 steps/beat (1/8 triplets)
    uint8_t num_steps;             // 1..16 (1..15 triplet); 16 after PATTERN CLEAR, 12 after selecting triplet
    enum Time { NOTE, TIE, REST } time[16];      // time list: one entry per step
    struct PitchEntry {            // pitch list: one entry per NOTE event, not per step
        uint8_t key;               // 0..12 = C..C' of the panel keyboard
        int8_t  octave;            // -1 (TRANSPOSE DOWN), 0, +1 (TRANSPOSE UP)
        bool    accent;
        bool    slide;             // slide from this note into the next one
    } pitch[16];
};
struct Track {                     // 7 tracks; group fixed by track no. (1,2->I 3,4->II 5,6->III 7->IV)
    struct { uint8_t pattern /*A/B x 1..8 within group*/, shift /*0..12*/; } measure[64];
    uint8_t dc;                    // last measure (one per track; the last one set wins)
    uint8_t segno;                 // D.S. target (one per track)
};
```

Where each part comes from:

- **Accent, slide and octave are attributes of pitch entries.** "When re-writing the pitch after writing an accent or a slide, the accent (or slide) will be cleared. However, this is not the case when you re-write the length of notes" ([OM] p.44; also p.46).
- The RE-303 manual says the same: "There are time steps that only hold step (note on) tie (note length) and rest (no note) information. The pitch steps store the note value, accent, slide and transpose information" ([REF] Sonic Potions, *RE-303 User Guide* v0.911, <https://www.tubeohm.com/wa_files/sonicpotions_re-303_user_guide.pdf>).
- **Slide is written on the first of the two notes**: "To slide from one note to another, write the slide on the first of the two notes" ([OM] p.44).

### 3.3 Battery backup

- The RAM is kept alive by the four C cells (UM-2). "If the batteries are changed within one minute, the memory will hold the DATA". The manual says to keep the batteries fitted even when using the AC adaptor ([OM] p.3; [SN] block diagram "CMOS MEMORY BACK UP").
- Whittle: "Often the on-board capacitor will retain memory data for days, but it is probably best not to trust it". He also reports that corrosion of µPD444C pins 18 (Vcc) and 10 (/WE) causes "all sorts of memory problems, including the inability to write new patterns correctly" ([REF] *303 mods* §3.7).
- No ROM default set is documented. If backup power is lost, the SRAM comes up in an arbitrary state (§10.1).

---

## 4. Pattern Write mode

### 4.1 Sub-modes

Pattern Write has three sub-modes, each with an LED: NORMAL, PITCH and TIME.

- PITCH MODE and TIME MODE enter their sub-modes. FUNCTION returns to normal.
- Time mode also returns to normal by itself after `num_steps` entries ([OM] p.13–16).
- The manual gives no automatic exit from pitch mode: the user presses FUNCTION. **[UNCERTAIN]** The RE-303 exits both modes "when the end of the pattern is reached"; the original's pitch-mode behaviour after 16 entries is not documented.
- Buttons should be pressed and released one at a time: "The TB-303 does not function properly if you go on to the next operation without releasing the button". Some results appear only on release ([OM] p.8).

### 4.2 Selecting and clearing

1. Set MODE to PATTERN WRITE and choose the group.
2. Press ACCENT (section A) or SLIDE (section B). The section LED lights.
3. Press a white key 1–8. Its LED flashes ([OM] p.10).
4. To **clear**, hold the pattern key and press CLEAR. This "clears the chosen memory position of any previous data" ([OM] p.11). After a clear:
   - the pattern is in sixteenth mode with **16 steps** ([OM] p.53);
   - the time list is silent: RUN on a cleared pattern gives "you cannot hear anything because, as yet, you have not written in the timing" ([OM] p.36).

**[UNCERTAIN] Whether clear resets the pitch list.** After three pitches are written into a cleared pattern, the manual says "If you press the [TAP] button more than three times, you will hear a random selection of notes" ([OM] p.13). That points to stale or undefined pitch slots.

### 4.3 The core concept: separate pitch and time lists

The manual presents a pattern as pitch, then length of note, then accent/slide, each entered separately ([OM] p.5 Fig. 1).

- The **time list** has `num_steps` events. Each is NOTE ("sound begins" ●), TIE ("increases the value of the previous note by one sixteenth note value" ○) or REST (–) ([OM] p.49, p.51).
- The **pitch list** is an ordered list of notes. Each note carries its own octave, accent and slide.
- During playback, a pointer into the pitch list resets at the start of the pattern and **advances only on NOTE events**. TIE and REST use up no pitch entry.

The RE-303 manual explains it with this example ([REF] RE-303 guide):

| Time list | NOTE | REST | REST | NOTE | TIE |
|---|---|---|---|---|---|
| Pitch list | C | D | E | F | G |
| Heard | C | – | – | **D**, held 2 steps | |

In its words: "On a classic step sequencer ... you would expect that the two notes C and F would play ... only the two notes C and D will play."

What follows from this:

1. **Editing the time list moves the pitches.** Turn step 2 into a NOTE and steps 2 and 4 now play D and E. Accents and slides move with their pitch entries.
2. **Re-entering pitches keeps the rhythm but clears accent and slide.** Re-entering time keeps them ([OM] p.44, p.46).
3. **If there are more NOTE events than written pitches**, the extra notes read unwritten slots. The manual's "random selection of notes" ([OM] p.13) is the only description. **[UNCERTAIN]**: whether this is stale data, uninitialised RAM or a wrap-around.
4. **More than 16 pitches can be entered.**
   - Anton Savov gives the original's "Pitch buffer length" as **48**, "Retained via battery" ([REF] <https://antonsavov.net/x0xb0x/n0nx0x2/diffs/>). This is probably Whittle's "pattern edit buffer" in IC5.
   - rv0 lists two TD-3 deviations from the original: "You can only enter 16 notes in Pitch Mode", and the TD-3 not being "able to write multiple patterns in one go (which also blocks tap entry across patterns)" ([REF] <https://rv0.be/articles/td3-bugs-thoughts/>).

   So the original evidently lets pitch (and tap) entry run on across several patterns, presumably a chain selected in Pattern Write. **[UNCERTAIN]**: exactly how the 48-entry buffer is split into patterns has not been documented.

### 4.4 Pitch mode

| Action | Result | Source |
|---|---|---|
| PITCH MODE | LED lights. Writing starts at the first pitch entry. | [OM] p.12 |
| Note key | Writes the pitch and advances. "The sound is heard and the indicator illuminates": the CV and gate are output. | [OM] p.12; [SN] p.6 calibrates CV this way |
| Hold DOWN (or UP), then key | Writes one octave down (or up) | [OM] p.19–20 |
| BACK | "backs up one step, and allows you to write the correct pitch in that place" | [OM] p.13 |
| FUNCTION | Back to normal mode | [OM] p.13 |
| PITCH MODE, then TAP repeatedly | Plays each stored note and lights its key. Pressing a note key here overwrites that note. | [OM] p.13 |
| TAP held + ACCENT/SLIDE | Toggles accent or slide on the sounding note. The LEDs show its state. Press FUNCTION when done; there is no need to TAP to the end. | [OM] p.40–43 |

**Slide rules** ([OM] p.44, p.63):

- The slide goes on the first of the two notes.
- "If there is a rest between the two notes, the slide effect does not work".
- "If you write a slide between two notes of the same pitch, the result is a 'TIED' note".
- A slide on a pattern's last note slides into the next pattern's first note, which can be the same pattern when it loops. "If you continually repeat a pattern that ends with a slide, the first note of the pattern will 'receive' the slide". With equal pitches the two bars are tied. If the next pattern starts with a rest, the slide does nothing.

**Accent on a note that is slid into.** With a slide on note 1 and an accent on note 2, "in rebirth the accent is played ... on the original not" ([REF] Ultra303 diary, 10.06.2004). This is consistent with §8.4: the accent latch updates but the envelope is not retriggered. **[UNCERTAIN]**: a single observation.

### 4.5 Time mode

| Action | Result | Source |
|---|---|---|
| TIME MODE | LED lights. Writing starts at step 1. No sound in time mode. | [OM] p.14, p.16 |
| DOWN / UP / ACCENT | Write NOTE / TIE / REST | [OM] p.15, p.51 |
| After `num_steps` entries | Automatically returns to NORMAL | [OM] p.15, p.54 |
| TIME MODE, then TAP `num_steps` times | Shows each step's event on the DOWN/UP/ACCENT LEDs, then returns to NORMAL | [OM] p.15–16 |

- In triplet mode each event lasts 1/3 of a beat ([OM] p.52).
- A tie written at step 1 is "automatically re-written" as a note ([OM] p.63). Step 1 is therefore never a TIE. Only a slide on the last note can carry a note across the bar line.

### 4.6 Step mode and step count

| Action | Result | Source |
|---|---|---|
| FUNCTION + UP ("3n") | Selects triplet mode (♩ = 3 steps). The UP LED lights. The step count becomes **12**. | [OM] p.54 |
| FUNCTION + DOWN (STEP) × N | Step count = N. "If you make a mistake press the FUNCTION button again, and repeat." | [OM] p.56 |
| Leaving triplet mode | Only by clearing the pattern: "After setting the STEP MODE for [triplet] you cannot reset for [sixteenths]" | [OM] p.63 |
| Limits | 1–16 steps (sixteenths); 1–15 steps (triplets) | [OM] p.57, p.89; [SN]. Other sources disagree for triplets: the RE-303 guide says "a maximum of 12 steps", *Music Technology* (Feb 1989) says "up to ... 12 triplet 8ths", and the TB-03 allows 16. Spec: 15, per Roland. |

- The manual's examples: 3/4 = 12 sixteenth steps; 3/4 in triplets = 9 steps; 5/4 is split across two chained patterns because 20 steps is too many ([OM] p.55–62).
- `num_steps` sets how long the pattern plays. A pattern change waits for the end of the current pattern (§5.1).
- **[UNCERTAIN]** The service notes list "The LED above the selected PATTERN switch does not [flash]" as a misoperation and point to the step-number pages ([SN] p.7). The mechanism is not described.

### 4.7 Tap-writing time data in real time

This method enters time data while the pattern runs ([OM] p.35–37, p.55, p.63):

1. Write the pitches, then press RUN. Nothing sounds yet.
2. Press **CLEAR**. A metronome plays on the bass voice at **eighth-note** intervals, or quarter notes in triplet mode. "The lower sound of the metronome is the first step of the pattern."
3. Tap TAP. Each tap starts a NOTE quantised to a step ("slight errors on your part will be corrected"). Tap within one measure: "You cannot write correctly if tapping between two measures". The pattern then loops.
4. **Sustain**: holding any white key while tapping makes each note last until the next tap, so "there are no rests". Without sustain, "the sound is heard only while pressing the TAP button, just like playing a keyboard".
5. Pressing CLEAR while running after writing "will ... clear the timing information, although the pitches will remain".

A model consistent with this: at each step of the recorded bar, write NOTE if TAP was newly pressed; otherwise TIE if TAP or a sustain key is held; otherwise REST.

### 4.8 Mode-change hazards

- "Do not change the MODE selector when the BATTERY CHECK/RUN indicator is alight" ([OM] p.38).
- Going from a TRACK mode to a PATTERN mode needs STOP and CLEAR/RESET first, "Otherwise, improper operation might occur". The recovery given is: power off and on, set TRACK PLAY, press CLEAR/RESET, then go to pattern mode ([OM] p.39 and the "Check before calling for the service" sheet). Track state evidently leaks into pattern mode unless it is reset.

---

## 5. Pattern Play mode

### 5.1 Live selection

- Select the group, the section (A/B) and the pattern (1–8), then press RUN ([OM] p.47–48).
- **A change waits for the end of the bar**: "Whenever you choose a selector switch, the new pattern will begin from the first note of the next measure" ([OM] p.48). The RE-303 queues changes the same way.
- **The group cannot change while running**: "the PATTERN GROUP cannot change. Therefore you can only play patterns from one PATTERN GROUP at a time" ([OM] p.48). **[UNCERTAIN]**: whether a group change is ignored until stop, or picked up later.

### 5.2 Chaining

Press two white keys **in the same half** (1–4 or 5–8) at the same time. The patterns from the lower to the higher number play in order and loop ([OM] p.59–61):

- Keys 1+3 play 1→2→3. Keys 1+4 play 1→2→3→4. Keys 3+4 play 3→4.
- Keys from different halves do not chain: "3 to 6, in this case only 3 will play".
- No descending order and no skipping.
- All patterns in a chain share one group and one section, so the longest chain is 4 patterns.
- The playing pattern's LED flashes. A new selection "does not change until the series of patterns finish".
- A shift applies to every pattern in the chain.

### 5.3 Transpose ("shift")

The manual contradicts itself on whether shift works in Pattern Play:

- **Yes**: "While holding the PITCH MODE button down, you can shift the key by pressing the appropriate key-switch. The shifted pattern will be played from the first note of the next measure. ... Changing to a different pattern removes the shift" ([OM] p.48, and the "alternative method" procedure table).
- **No**: "Shift cannot be done at PATTERN PLAY" ([OM] p.49 note).
- The Devil Fish manual takes the first reading: "In pattern play mode, you can press 'Pitch' and one of the 12 keys to transpose the pattern up by 0 to 12 semitones" ([REF] Whittle, *Devil Fish Manual*, "CV Out").

**Spec choice: the live shift works**, from the next bar, and is cleared when the pattern changes. **[UNCERTAIN]**

The shift is always upward and relative to C. The key pressed sets the interval: C = 0, F = +5, G = +7, C′ = +12 ([OM] p.27).

### 5.4 Other

- "Do not press the TAP button" in Pattern Play ([OM] p.49). No reason is given.
- **Tuning** (Pattern Play, stopped): hold PITCH MODE and press A, then press TAP. "the pitch of A (approx 220Hz) will be heard", and each further TAP repeats it ([OM] p.33).
  - The manual's sound-range diagram labels the TRANSPOSE UP octave (3–4 V) as the "Sound Range of TRANSPOSE UP and TUNING" ([OM] p.88). So the tuning note sounds an octave above the normal A.
  - The normal-octave A key is calibrated to 110 Hz at 2.75 V ([SN] p.5, p.7).

---

## 6. Track Write and Track Play

### 6.1 Concepts ([OM] p.64–65, p.70)

- A track is a list of measures. Each measure holds a pattern (section plus number) and a shift of 0–12.
- A chained pattern is stored as separate measures.
- The pattern group is fixed by the track: tracks 1–2 use group I, 3–4 group II, 5–6 group III, 7 group IV.
- **D.C.** marks the last measure.
  - Only one D.C. counts: "If you set more than one D.C. per TRACK, only the last one will operate".
  - A D.C. set halfway through a track makes that measure the last, and later measures never play.
  - If no new D.C. is set, "the previous D.C. measure will still operate".
- **Segno 𝄋**: one per track, the last one set wins. It must not be on the D.C. measure.

### 6.2 Writing a track (Track Write)

The procedure ([OM] p.23–28 and the procedure table):

1. Stop. Choose the track.
2. Press **BAR RESET** (CLEAR) to go to measure 1.
3. Press **RUN**. The current measure's stored pattern plays. "For convenience, TRACK WRITING is performed while the TB-303 is playing".
4. Select the pattern for this measure. Press its key "even if the Bass Pattern is already being played".
5. Optionally, hold **PITCH MODE** and press a key to set this measure's shift.
6. Press **WRITE/NEXT**. The measure is written and the track moves on. The *old* contents of the next measure now play.
7. Repeat steps 4–6. For the last measure: select its pattern, press **D.C.** (CLEAR), then WRITE/NEXT.
8. Press STOP.

Points to note:

- A track is edited by **overwriting, never by clearing**: "what you do is call back (listen to) the old track, one measure at a time, and replace each measure with a new one" ([OM] p.64).
- A whole chain can be written in one step. A shift applied while chaining applies to all its patterns ([OM] p.65).
- **Checking**: BAR RESET, then RUN. Holding PITCH MODE shows the measure's shift on the key LEDs (the C LED means no shift). TAP moves to the next measure, where you can re-select or re-shift ([OM] p.29–31, p.73).

### 6.3 Track Play

- Set the track, press **BAR RESET**, then RUN. The track plays from measure 1 to the D.C. measure and repeats. The selector LEDs follow the playing pattern ([OM] p.32).
- To play once, stop during the last measure, or "write two blank measures after the last measure to give you room to stop" ([OM] p.65).
- **Resume**: the manual says to press CLEAR/RESET before RUN to restart. Its troubleshooting sheet asks "Did you start the TB-303 after pressing the CLEAR/RESET button?" for the symptom "it does not begin from the 1st measure" ([OM] p.32, service sheet). So RUN without BAR RESET does **not** go back to measure 1. **[UNCERTAIN]**: exactly where it resumes.
- **D.S.** (TAP during play):
  - "After finishing the current measure, the TRACK will return to the 𝄋 measure and continue from there".
  - Pressed before the segno, it jumps forward to the segno.
  - Pressed in the D.C. measure, it goes to the segno rather than measure 1.
  - Do not use it around a change of track ([OM] p.71).

### 6.4 BAR (measure number), DEL, INS

**BAR entry** (stopped):

- Hold FUNCTION (BAR) and type the measure number, **largest digit first**. Keys 1–8 are the digits 1–8; DOWN = 9, UP = 0, ACCENT = 100, SLIDE = 200. Examples: 131 = 100, 3, 1; 55 = 5, 5.
- On release, that measure's pattern LED flashes. RUN then plays or writes from there.
- "Do not designate a measure number greater than the last measure number (D.C. measure)" ([OM] p.69–70).
- **Display**: the first digits are shown lit and the last digit flashes. A repeated digit alternates lit and flashing ([OM] p.69).
- **Queries**: BAR held while running shows the playing measure. BAR+D.C. shows the D.C. measure. BAR+𝄋 shows the segno measure ([OM] p.73).

**DEL** (Track Write): designate the measure, then press FUNCTION + C♯. Later measures move up. "the D.C. measure is corrected automatically but the 𝄋 measure cannot be corrected" ([OM] p.71–72).

**INS** (Track Write) ([OM] p.72–73):

1. Designate the measure to insert *before*.
2. Press FUNCTION + D♯.
3. Press RUN, select the pattern, press WRITE/NEXT, press STOP.

Later measures move back. The D.C. is corrected, the segno is not. "Do not insert after D.C. measure". "If the TRACK has more than 256 measures after a BAR has been inserted, the D.C. Measure will move to the first measure of the next TRACK".

### 6.5 Songs longer than 64 measures

Writing past measure 64 continues into the next track automatically. The pattern group stays the one set by the first track ([OM] p.66 Fig. 4):

| First track | Maximum length |
|---|---|
| 1–4 | 256 measures |
| 5 | 192 |
| 6 | 128 |
| 7 | 64 |

The rules ([OM] p.64–68):

- **Overflow wraps**: "If you exceed the limit ... the TRACK will return to the beginning and write over the first few measures".
- **Start from the first track**: a multi-track song must always be played and corrected from its first track. "If you set it to about halfway through the TRACK, it cannot play correctly"; "DO NOT START HALFWAY THROUGH A TRACK".
- **Unused measures are lost**: "Even if only 1 measure is written into the TRACK, it is impossible to write into the remainder".

### 6.6 Changing track while playing

The current track plays through to its D.C. measure. Then the new track starts from its first measure ([OM] p.67).

---

## 7. Tempo, clock and DIN sync

### 7.1 Internal clock

- **Range.** TEMPO is ♩ = 40–300 ([OM] p.89; [SN]).
  - Calibration ([SN] p.6): turn TEMPO fully clockwise and trim TM1 for an **8 ms ± 1 ms** period at TP1. At 24 ppqn that is ≈312 BPM (278–357 BPM within tolerance). So the internal clock is a 24 ppqn square wave, the same format as DIN sync.
  - Sonic Potions describe the slowest setting as one where "a step takes nearly half a second" (≈30–38 BPM) ([REF] Sonic Potions §7).
  - Both ends of the range vary from unit to unit.
- **Signal paths** ([SN] block diagram; partly **[UNCERTAIN]**, read from the drawing):
  - Clock: TEMPO pot → oscillator (IC6) → D flip-flop divider (IC1a, giving a 50 % square wave) → DIN-jack changeover switch → status buffer (IC7) → CPU port A.
  - Run state: RUN/STOP button → Schmitt trigger (IC6) → toggle flip-flop (IC1b), which goes through the same switch to port A. So "running" is a hardware level, exactly like DIN pin 1.
  - A "MONO MULTI C3 R1" between the run flip-flop and the clock divider restarts the clock on RUN. On the main-board schematic (p.4) the oscillator and divider run whether stopped or not (the selected pattern's LED flashes in time while stopped). RUN's rising edge clears IC1a through C2 and fires the C3/R1 monostable through an IC6 inverter, which holds the oscillator through Q6; when it ends (≈ 10 ms × ln(6/2.3) ≈ 9.6 ms), C4 sets IC1a. So the first clock rising edge comes about 9.6 ms after RUN, clear of the race below. Modelled in `core/tempoclock.h`; in the board model no first step is lost with it (against about 1 start in 10 when the clock rose at RUN).
- **The clock is polled, not interrupt-driven.** TEMPO CLOCK, START/STOP and TAP are read through the status buffer ([SN] port table). This happens in the 1.8 ms ISR ([REF] Sonic Potions §4, by analogy with the TR-808, which uses the same CPU family: "interrupt every 1.9ms, the complete ISR takes about 600µs").
  - Every clock edge is therefore seen on a 1.8 ms grid.
  - **[UNCERTAIN]** (inference) Each half-period of the clock must be longer than ~1.8 ms to be seen, which limits the clock to ~270 Hz (≈690 BPM). That is irrelevant for the knob but matters for fast external clocks.

### 7.2 Clocks per step

| Step mode | Steps/beat | Clocks per step | Clocks per full bar | Source |
|---|---|---|---|---|
| Sixteenth | 4 | **6** | 96 (16 steps) | [REF] Sonic Potions §4.1; Ultra303 ("6 clocks are 1/16 note"); Whittle |
| Triplet | 3 | **8** | 96 (12 steps) | [REF] Sonic Potions §4.2; RE-303 guide |

A pattern lasts `num_steps × clocks_per_step`. Queued changes take effect at the end of the *current pattern*, whatever its length.

### 7.3 DIN SYNC

- **The TB-303 has an input only, no sync output** ([OM] p.74, p.90: "SYNC In ×1: DIN connector (for TR-606, CR-8000, MC-4, CSQ-600)"). Other gear must be the master, for example a TR-606 set to OUTPUT ([OM] p.75).
- **Pinout** ([SN] schematic):

  | Pin | Signal |
  |---|---|
  | 1 | RUN/STOP |
  | 2 | GND |
  | 3 | TEMPO CLOCK |
  | 4 | **TAP** |
  | 5 | not connected |

  The general convention is pin 1 = start/stop (0 V stop, +5 V run) and pin 3 = 0/+5 V clock at 24 ppqn ([REF] Wikipedia, "DIN sync", <https://en.wikipedia.org/wiki/DIN_sync>).
- **The jack contains a switch.** Inserting a plug operates a dual-pole switch inside the socket. It disconnects the internal tempo clock and the internal run/stop, and connects DIN pins 3 and 1 in their place. Sources:
  - [SN] block diagram (the dashed linkage to two changeover contacts);
  - [REF] Sonic Potions §2: "a switch in the DIN jack replaces the internal clock and start/stop signal with the clock coming from the external gear";
  - [REF] Wikipedia;
  - [REF] Whittle: "The DIN Sync socket with switch (Roland part number 13429706) ... its integral dual pole switch is unique and unobtainable".

  Consequences:
  - With a plug in, the TEMPO knob does nothing.
  - **[UNCERTAIN]** The panel RUN/STOP button probably does nothing either; start/stop comes only from pin 1. This is inferred from the block diagram, where RUN/STOP toggles a flip-flop whose output is switched out. Anton Savov's n0nx0x2 x0xb0x firmware, which copies the 303 interface, advertises "Independent Start/Stop from the RUN/STOP button when slaved" as an *added* feature (<https://antonsavov.net/x0xb0x/n0nx0x2/>), which weakly supports this.
- **TAP over DIN.** Pin 4 carries the master's TAP. With a TR-606, "When you press the TAP button of the TR-606, both units will return to the 𝄋 measure" (D.S.). "The TR-808 cannot perform the D.S. function" ([OM] p.75).
- **Before starting**, press CLEAR/RESET on both units ([OM] p.75–78; also for TR-808, CR-8000, CSQ-600, MC-4 and EP-11).
- **No continue.** "It cannot synchronize correctly using the STOP/CONTINUE function of the CSQ-600 (DO NOT START AGAIN AFTER STOPPING HALFWAY)"; the same applies to the MC-4 ([OM] p.77). The TB-303 has no song-position concept. **[UNCERTAIN]** (inference) A restart begins again from step 1 of the current pattern or measure.
- **No song position.** Kenton: the TB-303 ignores MIDI Song Position Pointer (SPP), so with a MIDI-to-DIN converter you "always have to start the sequence from the top" ([REF] <https://kentonuk.com/support/connecting-tb-303-sh-101-mc-202/>). Reports that a slaved 303 is "one step late" or repeats its first step trace back to converters or MPCs sending SPP. They are not a 303 fault ([REF], hearsay, <https://www.mpc-forums.com/viewtopic.php?p=1697658>).
- **Start alignment.** Whittle wrote on the Analogue Heaven list (2003): if "the first Clock pulse" arrives "too soon after the rising edge of the Run/Stop", a TB-303 or TR-808 "will miss that, and actually start their sequence on the second Clock pulse" ([REF] <https://analogue.bytenoise.co.uk/message/736cc620f9fa521b15d06514f29cdc7f76de30b8>). This implies the following model for the spec:
  - **Step 1, tick 0 is the first clock rising edge after RUN goes high**, so the first note sounds on that clock.
  - A clock that arrives less than about one ISR period (~2 ms) after RUN can be missed.
  - DIN-sync masters normally wait about 9 ms between RUN and the first clock ([REF] Wikipedia).
  - **[UNCERTAIN]**: no measurement of this exists.
- **Internal start.** The block diagram's "MONO MULTI C3 R1" (a differentiator on the RUN flip-flop) appears to reset the tempo divider when RUN is pressed, so the internal clock also starts in phase with RUN ([REF] research reading of [SN]; **[UNCERTAIN]**, inference).
- **Unofficial sync out.** With no plug inserted, the internal clock (TP1) and run/stop also reach DIN pins 3 and 1 through 1 kΩ resistors (R2, R3) via the jack switch. A half-inserted plug can therefore make the TB-303 the master. Users report: "Leaving the cable halfway in opens the switch, then DINSync clocks go out, making the 303 the master" ([REF] hearsay, <https://forum.vintagesynth.com/viewtopic.php?t=42515>). Wikipedia agrees: "DIN Sync out can be achieved unofficially if the DIN connector is not completely inserted". The schematic reading is **[UNCERTAIN]**.
- **Pin 4 must be low.** A Squarp Pyramid that held pins 4/5 high left owners with a TB-303 "stuck in pitch mode", because TAP was permanently pressed ([REF] hearsay, <https://squarp.community/t/tb-303-din-sync-issue/519>).
- **Pulse width.** E-RM's DIN-sync tests, which included a TB-303, found that 5 ms clock pulses worked up to 300 BPM. That fits the ~1.8 ms polling ([REF] E-RM, *Implementing DIN Sync*, 2014, <https://www.e-rm.de/data/E-RM_report_HowToDinSync_10_14_EN.pdf>).

---

## 8. Gate, CV, accent and slide timing

Ticks are numbered 0–5 within a sixteenth step, or 0–7 in triplet mode. Tick 0 is the rising edge of the step's first clock. Real hardware adds the latencies in §8.5.

### 8.1 Gate

| Case | Gate high | Gate low | Source |
|---|---|---|---|
| Sixteenth NOTE | ticks 0–2 (**3 clocks**) | ticks 3–5 | [REF] Sonic Potions §4.1 ("3 clocks on and 3 clocks off"); Ultra303 ("the note off is on 3") |
| Triplet NOTE | ticks 0–4 (**5 clocks**) | ticks 5–7 | [REF] Sonic Potions §4.2; RE-303 guide |
| NOTE + k TIEs | from tick 0 of the NOTE to the gate-off tick of the last TIE | rest of the last TIE | [OM] p.79 Table 2 |
| REST | – | whole step | [OM] p.79 ("OFF when Rest") |
| NOTE with slide, then a NOTE | continuous into the next note, with no retrigger | – | [OM] p.79 ("GATE output with SLIDE"); [REF] Sonic Potions §4.3; Whittle |

The manual says only: "The GATE TIME is set a little shorter than the length of the note on the score. (It is the same case when STEP MODE is [triplet].)" ([OM] p.79).

**The sources disagree on the sixteenth gate:**

- Whittle (1999): "high at the start of clock pulse 0 ... low half-way through clock pulse 3 ... 3.5 clock pulses on and 2.5 off" ([REF] <http://www.firstpr.com.au/rwi/dfish/303-slide.html>). He added in 2012: "Perhaps some of what I wrote below is wrong."
- Ultra303 (2004, logic analyser): "the devil fish's description is not 100% correct...the note off is on 3 and not on 3.5 with a small delay of 2.8-3.5 ms which is always fixed independent from the tempo".
- Sonic Potions (2017, logic analyser): 3/3, with measured duty cycle 49.96–55.8% (50.25–50.5% at the slowest tempo, 55–56% at the fastest).
- Zoe Blade's notes give 7/12 ≈ 58 %, which is Whittle's figure (<https://notebook.zoeblade.com/TB-303.html>).

**Spec: 3 of 6 clocks (5 of 8 in triplet mode).** The extra few percent on hardware comes from the gate-off latency being longer than the gate-on latency (§8.5).

Anton Savov's description is a useful mental model ([REF] <https://antonsavov.net/articles/303andmidi/>): "The gate can be raised only in the beginning of a step, and lowered only on a Mid-Step". In triplet mode, take the measured 5/8 point instead of the middle.

### 8.2 CV

- The CV changes **only at tick 0 of a NOTE step**, when the CPU puts 6 pitch bits on PD/PF and pulses the latch strobe ([SN]; [REF] Sonic Potions §3).
- It holds through TIE and REST steps and after STOP. **[UNCERTAIN]**: this is inferred from the latch circuit and from Sonic Potions' remark that during ties and rests "No pitch value change occurs". It has not been measured on its own.
- **Order of events.** Pitch and accent are latched by the same strobe, at "the beginning of each new note". The gate rises in the same ISR pass, about 1 ms after the interrupt ([REF] Sonic Potions §3–4). An implementation can update the CV and accent, then raise the gate, all at tick 0. **[UNCERTAIN]**: the exact µs ordering has not been published. For comparison, the x0xb0x latches the pitch about 10 µs before the gate (§11.1).
- CV OUT carries the voltage after the slide circuit ([REF] Devil Fish manual: "The CV Out carries the voltage of the internal DAC, after it has passed through the slide circuit"). The owner's manual: "when the pitch slides, the CV also changes smoothly" ([OM] p.79).
- Pressing a note key in pitch mode outputs that note's CV and a gate: "The CV can also be used whilst Writing (except when Writing the length of the notes)" ([OM] p.79).

### 8.3 Slide

A slide written on note *N* (the first of the pair) does three things:

1. **The gate does not fall** at the end of N. It stays high into N+1, so the envelopes are not retriggered.
2. **N+1's pitch is latched at N+1's tick 0**, as for any note. There is no early latch. Whittle wrote his page to kill the rumour "that the sequencer anticipates the next note and starts sliding before that note starts" ([REF] Whittle; Sonic Potions §6).
3. **The slide line is high for the whole of N+1**, so the CV glides from N's pitch to N+1's. The sources agree:
   - Sonic Potions: "set high at the beginning of the slid step ... turns off ... at the beginning of the next non slid step" (their "slid step" is N+1).
   - Whittle: "remain active until the end of the note which follows the slid note ... if it is 1, 2 or more 1/16ths notes long".
   - Ultra303: "slide is only hi on the second note...the 303 slide always starts on the second note...not on the first!"

The gate then falls at N+1's normal gate-off (at its last TIE), unless N+1 also has a slide.

Other details:

- **The slide line can rise before N+1 without effect.** Sonic Potions saw it "always high during tie and the following rest steps ... even if no slide attribute was set". Whittle says it rises "1/16 note after the start of slid note", which falls during N's ties if N is long. Abstrakt Instruments say "a slide starts from the last tie" ([REF] <https://www.matrixsynth.com/2015/08/avalon-and-tb-303-behavior-on-long.html>). None of this changes the CV, which moves only at N+1. The audible glide therefore starts at N+1. **[UNCERTAIN]**: Abstrakt's wording is ambiguous.
- **Slide chains** (N, N+1, N+2 all slid): the gate stays high throughout, and the slide line dips for about 8.75 µs at each note to latch it ([REF] Sonic Potions §6).
- **Rests, equal pitches and pattern ends**: a slide across a rest does nothing; a slide between equal pitches is a tie; a slide on the last note wraps to the next first note (§4.4).
- **Glide shape**: an RC with a time constant of about 22 ms (effectively 100 kΩ into 0.22 µF), so the glide takes a fixed *time* exponentially, not a fixed rate ([REF] Devil Fish manual "Slide"; KVR thread summary in `private/docs/papers/forums/kvr_threads_extracts.txt`).

### 8.4 Accent

- Accent is an attribute of a pitch entry. It is latched into IC13 by the same strobe as the pitch, so "it is synchronized to the whole step duration". Stray CPU pulses on the unlatched line, which is high for 2 of the 3 gate-on clocks, are ignored ([REF] Sonic Potions §5).
- **The accent persists** across TIE and REST steps and consecutive accented notes, and "is turned off at the beginning of the next unaccented note" ([REF] Sonic Potions §5).
- **It stays latched after STOP**: accent is active when "the internal sequencer is not running, but the last note had the accent on". The flip-flop powers up in a random state; Devil Fish ECO-07 adds a power-on reset ([REF] Whittle, Devil Fish manual and <http://www.firstpr.com.au/rwi/dfish/ECO/>).
- On a slid-into note, the accent latches but no gate edge occurs. This fits the Ultra303 report that such accents are not heard (§4.4).
- "Accent in the TB-303 has no effect on the CV and therefore cannot be applied to other synthesizers" ([OM] p.79). There is no accent output jack.

### 8.5 Latency and jitter (measured)

Measurements from Sonic Potions, using a logic analyser on an original CPU (2017), with Ultra303 (2004) in brackets:

| Quantity | Value |
|---|---|
| Clock rising edge → gate on | 0.8–3.3 ms; mean 2.06 ms, SD 0.74 ms (Ultra303: 1.8–2.2 ms) |
| Interrupt → gate on | 0.917–1.062 ms |
| Interrupt → gate off | about 2.3–2.4 ms |
| Clock falling edge → gate off | 2.25–5.45 ms; mean 3.51 ms, SD 0.75 ms, at medium tempo (Ultra303: 2.8–3.5 ms) |
| Effect of tempo on latency | none measurable |
| Beating | The fixed 1.8 ms ISR beats against the tempo clock. There is no beating when the beat period is a whole number of ISR periods, e.g. 278 × 1.8 ms → 119.90 BPM. "Especially with higher speeds, a slight beating in the note accentuation can be heard." |

Note: Schmidt measures gate-off from the clock's *falling* edge. Taken literally, that does not match his own duty-cycle figures, which fit a gate-off at the rising edge of tick 3 followed by the ~2.35 ms ISR latency. The model below uses the latter.

**Authentic-feel emulation:**

1. Run a free-running 1.8 ms ISR phase.
2. Sample the clock line in that ISR.
3. Raise the gate about 1.0 ms after the ISR that sees the step's clock edge.
4. Drop it about 2.35 ms after the ISR that reaches the gate-off tick.

The RE-303 has the same choice as a setting: "Timing: on = Original Mode (delayed gate offs) / off = tight mode" ([REF] RE-303 guide).

On the synth side, for context: the audible note starts "between 1 and 5 ms (typically 4 ms) after the start of the gate signal". After the gate falls, an unaccented note lasts "about 16 ms – 8 ms of normal volume and 8 ms of linear decay" ([REF] Devil Fish manual).

### 8.6 Timing diagrams (sixteenth mode; 2 characters per tick)

Three sixteenth notes A, B, C:

```
tick    0 1 2 3 4 5 0 1 2 3 4 5 0 1 2 3 4 5
CV      A A A A A A B B B B B B C C C C C C
GATE    ‾‾‾‾‾‾______‾‾‾‾‾‾______‾‾‾‾‾‾______
SLIDE   ____________________________________
```

Slide written on B. Here "~" means the CV is gliding from B towards C through the slide RC, whose time constant is about 22 ms:

```
tick    0 1 2 3 4 5 0 1 2 3 4 5 0 1 2 3 4 5
DAC     A A A A A A B B B B B B C C C C C C
CV OUT  A A A A A A B B B B B B ~ ~ C C C C
GATE    ‾‾‾‾‾‾______‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾______
SLIDE   ________________________‾‾‾‾‾‾‾‾‾‾‾‾
```

NOTE, TIE, TIE, REST:

```
tick    0 1 2 3 4 5 0 1 2 3 4 5 0 1 2 3 4 5 0 1 2 3 4 5
event   NOTE        TIE         TIE         REST
GATE    ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾__________________
```

---

## 9. Output voltages and note range

| Signal | Spec | Source |
|---|---|---|
| CV OUT | Mini jack, **+1 V to +5 V, 1 V/oct** | [OM] p.90; [SN] |
| GATE OUT | Mini jack, **0 V off, +12 V on**, driven "by a diode and a 1K resistor" | [OM] p.90; [SN]; [REF] Devil Fish manual |
| Accent, slide | No jack | [OM] p.79 |

**Calibration** ([SN] p.5–6):

- Transpose-down C = **1.000 V**; transpose-up high C = **4.000 V**.
- Each octave is set to 1.000 V ± 3 mV, using TM6.
- With TUNE centred, the A key gives **110 Hz** at **2.75 V**.

TUNING moves the VCO only, not CV OUT ([REF] Devil Fish manual). Its range is ±500 cents per [OM] p.89, but "approx. ±700 cents (perfect fifth)" per [SN]; the sources disagree.

**Note numbers.** With key 0–12 (C … C′), octave −1/0/+1 and shift 0–12:

n = 12·(octave+1) + key + shift, giving 0–48, and **CV = 1 V + n/12**.

| Entry | n | CV | Pitch (TUNE centred) |
|---|---|---|---|
| DOWN + C | 0 | 1.000 V | C1 ≈ 32.7 Hz |
| C (left key) | 12 | 2.000 V | C2 ≈ 65.4 Hz |
| A | 21 | 2.750 V | 110 Hz |
| C′ (right key) = UP + C | 24 | 3.000 V | C3 ≈ 130.8 Hz |
| UP + C′ (pattern maximum) | 36 | 4.000 V | C4 ≈ 261.6 Hz |
| UP + C′ with shift 12 (track maximum) | 48 | 5.000 V | C5 ≈ 523 Hz |

Sources: [OM] p.79 and p.88 (a pattern spans 1–4 V; a track spans 1–5 V, "4 octaves, because you can add up to one octave of 'shift'"); [SN] calibration; [REF] Devil Fish manual ("2 Volts is the normal C at the left of the 'keyboard', 1 volt is the C an octave below. 3 Volts is the C at the right ... 4 volts is that C transposed up an octave").

That gives 37 pitches per pattern and 49 per track, within the 6-bit DAC's 64 codes. **[UNCERTAIN]**: the DAC code mapping is inferred (for example code = n + 12 if code 0 = 0 V).

---

## 10. Known quirks, bugs and folklore

### 10.1 Dead battery, corrupted memory, "random patterns"

**What is documented.**

- Pattern and track memory is static RAM kept alive by the C cells. "If the batteries are changed within one minute, the memory will hold the DATA" ([OM] p.3).
- No checksum or ROM default set is documented. **[UNCERTAIN]**: this is inferred from the hardware, since the firmware has not been analysed.
- After a total loss of backup power, the µPD444C contents are arbitrary. The firmware then decodes them as patterns, giving random pitch, time, accent and slide data. Step counts or track pointers may also be out of range, and how the firmware handles those is unknown.

**What is folklore** (all [UNCERTAIN]):

- An old Wikipedia text spoke of a "low voltage failure mode, wherein patterns ... get completely scrambled if the batteries are removed for a time" (mirror: <https://en-academic.com/dic.nsf/enwiki/15917>).
- Synthtopia (2009) described machines producing "interesting random sequences ... when their batteries get old" (<https://www.synthtopia.com/content/2009/10/16/the-mystery-of-self-programming-synthesizers/>).
- A research-catalogue essay says users "would utilize a glitch in the low voltage failure mode of the device to erase the sequence library and generate new, random ones" (<https://researchcatalogue.net/view/1579904/1584290>).
- The story that acid house (Phuture's "Acid Tracks") came from battery-scrambled patterns is **not supported** by first-hand accounts. DJ Pierre says only: "we didn't know how to programme it. We had it synched up, and it was playing some stuff" (Red Bull Music Academy, 2012, <https://daily.redbullmusicacademy.com/2012/12/dj-pierre-interview>).

**For an emulator:** offer an explicit "scramble/randomise all" action (n0nx0x2 has a "Batteries-Out effect (randomize all patterns)") rather than trying to reproduce brown-out behaviour. The low-voltage failure the Devil Fish manual describes in detail is different: a CPU that freezes or stops driving the gate below about 4.5 V (§10.2 item 16).

### 10.2 Checklist of firmware and operating quirks

Each item is described in the section given in brackets.

1. **Pitch and time are separate lists.** Time edits move pitches, accents and slides to other steps (§4.3). [OM]/[REF]
2. **Re-writing pitches erases accent and slide**; re-writing time does not (§4.3). [OM]
3. **Extra NOTE events play "a random selection of notes"** (§4.3). [OM]; cause [UNCERTAIN]
4. **A TIE at step 1 is converted to a NOTE** (§4.5). [OM]
5. **Triplet mode can only be undone by a clear.** It allows at most 15 steps and uses a 5/8 gate (§4.6, §8.1). [OM]/[REF]
6. **A slide on the last note wraps** into the next (or the same) pattern's first note. Equal pitches tie across the bar (§4.4). [OM]
7. **A slide across a rest does nothing.** A slide between equal pitches is a tie (§4.4). [OM]
8. **An accent on a slid-into note is inaudible** (§4.4, §8.4). [REF], one observation
9. **The accent latch persists after STOP** and powers up in a random state (§8.4). [REF]
10. **Track→Pattern without STOP + CLEAR** causes "improper operation". The MODE switch must not be moved while running (§4.8). [OM]
11. **Some key actions happen on release**, and overlapping presses confuse the firmware (§4.1). [OM]
12. **About 1.8 ms jitter and a 50–56 % gate duty** come from the ISR polling the tempo clock (§8.5). [REF]
13. **No STOP/CONTINUE sync** (§7.3). [OM]
14. **DEL/INS do not move the segno.** D.S. is unreliable around a track change (§6.3–6.4). [OM]
15. **Track overflow wraps** and overwrites. Unused measures are lost. A song must start from its first track (§6.5). [OM]
16. **Low batteries** can leave the unit silent while the LEDs still run, or crash the sequencer.
    - The service notes: "suspect aged dry cells in the malfunctioning (silent) TB-303 when LEDs other than RUN/BATTERY are lit up" ([SN] p.7).
    - Whittle: the CPU is rated 4.5–5.5 V and fails when the cells sag below about 5.5 V under load ([REF] Devil Fish manual, "Operation from C-cell batteries").
17. **Corroded RAM pins** (µPD444C pins 18 and 10) corrupt or block pattern writes (§3.3). [REF]

---

## 11. Comparison with reference implementations

### 11.1 x0xb0x (ladyada / Limor Fried, stock firmware v1.0–1.05, 2005–2007)

The x0xb0x copies the TB-303 *synth* circuit closely. Its sequencer was **deliberately not** a copy of the TB-303 data model, so use it as a reference for **timing**, not for **editing behaviour**. Citations use the git mirror of the SourceForge CVS at <https://github.com/a1k0n/x0xb0x/tree/master/firmware>, written as `FW/file:line`.

| Aspect | TB-303 | x0xb0x stock firmware |
|---|---|---|
| Data model | Separate pitch list and time list (NOTE/TIE/REST). Accent and slide are on pitch entries. | **One byte per step**: bit 7 = slide (from this step into the next), bit 6 = accent, bits 5–0 = absolute note number (0x00 = rest, C1 = 0x0B … D♯5 = 0x3E). 0xFF = end of pattern. No tie state and no octave bits (UP/DOWN add ±12 to the stored number). (`FW/pattern.h`, `FW/pattern_edit.c:218-227`; the c0ntr0l `pattern.py` masks `NOTE_MASK=0x3F, ACCENT_MASK=0x40, SLIDE_MASK=0x80`) |
| Ties | TIE events | "1/8th notes, quarter notes, etc. are created by placing multiple identical 1/16th notes in a row and then sliding between them. (Essentially, this is how the original TB-303 did it.)" (x0xb0x manual, *Pattern Edit* page). The manual itself says: "The Roland TB-303 also used a strange method of programming which required entering notes and timebases seperately … This technique is not used." Electrically the "tie = slide to the same note" claim is close: Sonic Potions saw the 303's slide line high during ties (§8.3). |
| Pattern length | 1–16 (1–15 in triplet mode) | 1–16, set by the position of the first 0xFF (`FW/main.c:219`) |
| Memory | 64 patterns in battery-backed SRAM, 7 tracks × 64 measures | 16 banks × 8 = 128 patterns of 16 bytes in SPI EEPROM. 64 tracks of 16 entries (16-bit words holding a pattern number, a ±12 transpose and rest/accent/slide flags) (`FW/pattern.h`, `FW/track.h`) |
| Tempo | ♩ = 40–300 | 20–300 BPM (`MAX_TEMPO 300`, `MIN_TEMPO 20`, `FW/main.h:33-34`) |
| Clocks per step | 6 (8 in triplet mode) | **6.** Note logic runs every 3 clocks, alternating note-on and note-off phases (`FW/main.c:104-461`) |
| Gate | 3 of 6 clocks (measured; 5 of 8 in triplet mode) | **3 of 6 clocks (50%)**. No triplet mode in stock firmware. SokkOS 2.0 adds a triplet mode with 8 clocks per step and up to 15 steps, which matches the TB-303. |
| CV vs gate | Pitch and accent latched by one strobe at note start, gate in the same ISR (~1 ms after the interrupt) | 6 note bits latched into the same IC9 (4174) with a rising edge on NOTE_LATCH, then a ~10 µs delay loop, then the gate rises (`FW/synth.c:52-116`). In the x0xb0x, SLIDE and ACCENT are *not* latched; they change when the port is written. |
| Slide | Gate held from N into N+1; slide line high during N+1 | Gate held at N's note-off. SLIDE goes high at N's note-off phase (the middle of step N) and stays high through N+1, falling at the next note-on. The glide therefore happens during N+1, since the CV changes only at N+1's latch. `prev_note` survives pattern boundaries, so a slide on the last step glides into step 1 of the next loop. On start, `curr_note = REST` prevents a slide into step 1. |
| Rest with slide/accent | A slide across a rest does nothing ([OM] p.44) | A slide *into* a rest keeps the gate high at the same pitch until the rest's note-off, so it works like a tie (`FW/synth.c:81` comment). **This differs from the TB-303.** |
| DIN sync | Input only. Pins 1 run/stop, 3 clock, 4 TAP | In **and** out, 24 ppqn, standard pinout. Sync-in clock polled at 1 kHz. On start, step 1's note-on comes on the **first clock after START**, then every 6 clocks. Internal mode emits DIN and MIDI clocks continuously (`FW/dinsync.c`, `FW/main.c:489-499`). |
| Transpose | Shift 0 to +12, per track measure or live (next bar) | Live: UP+key gives 0 to +12, DOWN+key gives −12 to 0, applied at the next pattern boundary. Track entries: −12 to +12. |

None of the x0xb0x sources contain measured TB-303 sequencer timings. Their 50% gate agrees with the later Sonic Potions measurements, and a MarOS comment says "as the 303 gate length was 1/32 note".

### 11.2 Sonic Potions RE-303 CPU (2017)

This is a drop-in replacement for the µPD650C. It was written after the logic-analyser study cited throughout this document and keeps the TB-303 model: "The basic sequencer concept is the same as on the original 303. Pitch and Time information is stored independently" ([REF] RE-303 User Guide). Its documented behaviours that reflect the original are:

- 6 clocks per step with a 3/3 gate, and 8 clocks per step with a 5/3 gate in triplet mode;
- pattern changes queued to the end of the bar;
- FUNCTION + tapping DOWN N times sets length N, "like on the original 303";
- an optional "original" timing mode with delayed gate-offs.

Its extensions are not original and must not be copied into a TB-303 spec: direct length keys, 8-pattern chains, pattern direction, a randomiser (CLEAR + ACCENT), copy/paste (CLEAR + C♯/D♯), rotation, live recording on BACK, SysEx, and per-measure repeat counts in tracks.

### 11.3 Roland TB-303 software (Roland Cloud plug-in, 2019)

The plug-in uses a modern grid: 16 steps, each with PITCH/GATE, ACCENT, SLIDE and OCTAVE. It adds LAST STEP for length, a SCALE knob ("note length of each step"), a SHUFFLE knob, eight variations per pattern and 64-pattern banks ([REF] `private/docs/roland-plugin/TB-303_Manual_E.txt`). It does **not** reproduce the separate pitch and time lists.

### 11.4 Other clones and re-issues

| Unit | Sequencer model | Deviations from the TB-303 relevant here | Source |
|---|---|---|---|
| **Roland TB-3** (2014) | Step grid | No pitch/time modes. 64 patterns of 1–32 steps. Triplet grid. A slide between equal notes acts as a tie. Syncs to MIDI clock only. | <https://static.roland.com/assets/media/pdf/TB-3_OM.pdf> |
| **Roland TB-03** (Boutique, 2016) | "**Original mode** reproduces the pattern write operation of the TB-303", plus a new step-recording mode | Notes:<br>• Agrees with the original: "when you re-enter the pitches, the accent and slide data you entered are lost".<br>• 96 patterns. 7 tracks with D.C., segno/D.S., insert and delete. Tracks of "up to 256 measures" (SOS review).<br>• The triplet grid allows 16 steps, where the original allows 15.<br>• Adds shuffle (−50 to +50); the original has none.<br>• TRIGGER IN ("steps advance each time a trigger pulse is received") and MIDI clock replace DIN sync.<br>• CV out −1 to +7 V; gate +12 V. | <https://static.roland.com/assets/media/pdf/TB-03_eng03_W.pdf>; SOS <https://www.soundonsound.com/reviews/roland-boutique-tr-09-tb-03> |
| **Behringer TD-3** | Pitch and time modes. Groups I–IV × 8 A/B patterns. ≤ 16 steps. TRIPLET button. Tracks 1–7 tied to groups. | Notes:<br>• Agrees with the original: "If the last note of a pattern is a slide ... if there is only one pattern, it will slide to the beginning of itself ... two or more patterns, it will slide to next pattern".<br>• SYNC IN is a 3.5 mm jack carrying clock plus start/stop, at 1 PPS, 2, 24 or 48 PPQ.<br>• CV +1 to +5 V; gate 0/+12 V.<br>• rv0 lists further deviations: "You can only enter 16 notes in Pitch Mode"; no writing of multiple patterns in one go; no hold-a-pattern-key sustain while tapping; transpose bugs; a MIDI slide bug, fixed in firmware 1.2.6. His verdict: the TD-3 tries to "mimic the original sequencer without understanding how it really works". | Quick-start guide v4, <https://www.adorama.com/col/productManuals/BETD3BB.pdf>; <https://rv0.be/articles/td3-bugs-thoughts/> |
| **n0nx0x2** (Anton Savov, x0xb0x firmware, 303-style UI) | Pitch/time model | Its differences table describes the original as follows:<br>• chains of "any 4 consecutive pattern slots" within 1–4 or 5–8;<br>• "Pitch buffer length 48, retained via battery";<br>• BACK "goes back once";<br>• edits are "saved immediately";<br>• DIN direction set by the "mechanical switch in port". | <https://antonsavov.net/x0xb0x/n0nx0x2/diffs/> |
| **Cyclone Analogic TT-303** (Bass Bot) | Not documented here (no manual found) | Retail listing only: "up to 64 steps", adjustable gate and slide time, DIN-compatible clock and run outputs. | <https://www.thomann.co.uk/cyclone_analogic_tt_303_bass_bot_v2.htm> |

Contemporary press agrees with Roland's figures. *Music Technology* (Feb 1989, "The Bass Race") gives a "16th note or a triplet 8th note as the basic step unit", says "The only method of syncing ... is via its DIN Sync input", and remarks that "tap entry can give less reliable results than step-time entry" (<https://www.muzines.co.uk/articles/the-bass-race/2177>).

---

## 12. Reference playback algorithm (for implementers)

The sketch below is consistent with everything above. Where it fixes a detail the sources leave open, that detail is marked **[UNCERTAIN]** in the earlier sections.

```c
// Called on each rising edge of the 24 ppqn clock (internal or DIN pin 3) while RUN is high.
void on_tick(Seq *s) {
    const Pattern *p = s->pat;
    int tps      = p->triplet ? 8 : 6;        // ticks per step
    int gate_off = p->triplet ? 5 : 3;        // tick at which a plain note's gate falls

    if (s->tick == 0) {                       // ---- step boundary ----
        if (s->step == 0) {                   // start of pattern: take queued pattern/chain/track
            advance_pattern_if_needed(s);     // uses queued selection, chain range, track list, D.C./D.S.
            p = s->pat; s->pitch_idx = 0;
            tps = p->triplet ? 8 : 6; gate_off = p->triplet ? 5 : 3;
        }
        Time ev = p->time[s->step];           // (a TIE at step 0 cannot exist: written as NOTE)
        if (ev == NOTE) {
            PitchEntry e = p->pitch[s->pitch_idx++ & 15];
            int n = 12 * (e.octave + 1) + e.key + s->shift;      // 0..48
            s->slide_line = s->slide_pending;  // slide INTO this note if previous note had slide
            latch(s, cv_volts(1.0 + n / 12.0), e.accent);        // one strobe: CV + accent together
            s->gate = 1;                       // no-op (no retrigger) if the gate was held by a slide
            s->cur_slide = e.slide;
            s->slide_pending = false;
        } else if (ev == REST) {
            s->slide_line = 0; s->cur_slide = false;             // CV and accent latch keep their values
        }                                      // TIE: nothing changes
    }

    if (s->tick == gate_off) {                // ---- end of the audible part of this step ----
        Time next = peek_next_time_event(s);   // looks across pattern/chain/track boundary
        if (next == TIE) {
            /* keep gate high */
        } else if (s->cur_slide && next == NOTE && p->time[s->step] != REST) {
            s->slide_pending = true;           // gate stays high; slide line rises at next NOTE
        } else {
            if (p->time[s->step] != REST) s->gate = 0;
        }
    }

    if (++s->tick == tps) {                    // ---- advance step ----
        s->tick = 0;
        if (++s->step >= p->num_steps) s->step = 0;   // pattern end; next pass picks queued change
    }
}
```

Other rules for an implementation:

- **Pattern changes**: queue them, and apply only at `step == 0`. Do not allow a group change while running. A chain plays from its lowest to its highest key and loops. In Track mode, follow the measure list until D.C., then return to measure 1 or the segno.
- **Shift (transpose)** takes effect at the next pattern start. In Pattern Play it is cleared when a new pattern is selected.
- **RUN** resets `step = tick = 0` and `pitch_idx = 0`. **STOP** is immediate: gate low, CV and accent latch hold.
- **Swing/shuffle**: none. The TB-303 has no swing. The modern Roland TB-03 and TB-303 plug-in add SHUFFLE ([REF] Roland plug-in manual, `private/docs/roland-plugin/TB-303_Manual_E.txt`).
- **For hardware-accurate feel**, add the ISR quantisation and latencies from §8.5.
- **Validate against the ROM.** MAME's driver is `MACHINE_NOT_WORKING | MACHINE_NO_SOUND`. It has no tempo-clock input, and its RAM /WE polarity is inverted, so the firmware never reads its patterns. A small uCOM-43 model with those two things fixed runs the ROM (`docs/firmware.md`). Its first results are below.

### 12.1 Cross-check against the ROM (firmware test run, 2026-10)

These come from `rom/d650c-133.ic8` running in the board model of `docs/firmware.md`: 24 ppqn on status bit PA3, RUN on PA0, the 1.8 ms ISR, and random pattern RAM.

- **The ISR polls the clock.** The disassembly confirms that the 1.8 ms ISR samples port A and edge-detects the tempo clock (§7.1). Clock pulses of 1 ms are missed; a 50 % square wave or 4 ms pulses work from 40 to 300 BPM.
- **Clocks per step and gate length.** Sixteenth steps take 6 clocks and triplet steps 8. Over 743 sixteenth steps the gate was 3 of 6 clocks every time, and over 66 triplet steps 5 of 8 (§7.2, §8.1). Whittle's 3.5 is not reproduced.
- **Jitter.** Step onsets land 0.9–2.6 ms after the clock edge. Steps measured 124.05–126.09 ms at 120 BPM (§8.5).
- **Strobe and slide.** A plain note pulses PI1 for about 40 µs and raises the gate one instruction (about 9 µs) later. When the gate is still high at a step boundary (slide or tie), PI1 rises to latch the pitch and stays high for the whole step. Ties appear as the same pitch latched again (§8.2–8.3).
- **Rests.** A rest produces no strobe and no gate edge, so the CV holds (§8.2).
- **Start.** Step 1 fired on the first clock edge after RUN. It still fired, about 5 ms late, when the edge coincided with RUN in the same ISR sample (§7.3).
- **DAC code.** The pitch routine computes `code = {11, 23, 35}[DOWN / normal / UP] + key + shift`, and the DAC is 1/12 V per code. With the key stored as 1–13 for C…C′, C maps to codes 12, 24 and 36, which is 1, 2 and 3 V (§9). The key encoding is inferred.

---

## 13. Open questions (settle with the ROM in the board model of `docs/firmware.md`, or on a real unit)

1. Does PATTERN CLEAR reset the pitch list? What do surplus NOTE events read (§4.2–4.3)?
2. How does the 48-entry pitch buffer map onto patterns when more than 16 pitches are entered, or when writing "multiple patterns in one go" (§4.3)? Does pitch mode exit by itself?
3. ~~Exactly how long after RUN must the first clock arrive to count as step 1 (§7.3)? Does the internal clock restart in phase on RUN?~~ In the board model a first edge 1.75 ms or more after RUN always counts. The internal clock restarts about 9.6 ms after RUN (§7.1).
4. With a DIN plug inserted, are the TEMPO knob and the RUN/STOP button completely dead (§7.3)?
5. Is a group change while running ignored until stop (§5.1)? Where does Track Play resume after STOP without BAR RESET (§6.3)?
6. What does TAP do in Pattern Play while running, given the manual's "do not press"?
7. What is the physical RAM bit layout? Whittle gives the chip allocation only.
8. Does Pattern Play shift work, given the manual contradicts itself (§5.3)?

---

## 14. Sources

**Primary (Roland)**

- [OM] Roland, *Computer Controlled Bass Line TB-303 Owner's Manual* (English, "'82 APR."). Scans: <https://archive.org/details/synthmanual-roland-tb-303-owners-manual>, <https://archive.org/details/RolandTB303OwnersManual>; local `private/docs/photos/owners-manual-panel/`. The procedure table, sequencer operation (pp. 9–73), CV/gate tables (pp. 78–79, 88) and specifications (pp. 89–90) are used throughout.
- [SN] Roland, *TB-303 Service Notes*, First Edition, Feb. 19 1982. <https://archive.org/details/synthmanual-roland-tb-303-service-notes>. Used for: specifications; µPD650C port table; block diagram (tempo oscillator, DIN-jack switch, latches); main-board schematic (SYNC pinout 1 RUN/STOP, 2 GND, 3 TEMPO CLOCK, 4 TAP; INT 1.8 ms; IC9 4174 DAC latch; IC13 accent latch); adjustment procedure (TEMPO 8 ms at TP1, INT 1.8 ms at TP2, CV 1.000 V/oct, A = 110 Hz at 2.75 V); preliminary diagnosis.

**Measurements and reverse engineering**

- Julian Schmidt (Sonic Potions), *Analysis of the µPD650C-133 CPU timing*, 2017. <https://www.tubeohm.com/wa_files/analysis_of_the_d650c-133_cpu_timing.pdf> (linked from <https://www.tubeohm.com/sp-303cpu.html>). Used for: 24 ppqn; DIN-jack switch; 1.8 ms ISR; gate 3/6 and 5/8 clocks; latencies and jitter; slide and accent latching; strobe widths.
- Sonic Potions, *RE-303 CPU User Guide* v0.911 (2017). <https://www.tubeohm.com/wa_files/sonicpotions_re-303_user_guide.pdf>. Used for: the pitch/time model and example; queued pattern change; tap-length entry "like on the original"; triplet 8-clock steps.
- Olaf Höhmann, *Ultra303 diary* (2004). <http://ultra303.ultrafex.de/diary.phtml>. Entries of 24.05.2004 (logic analyser: 6 clocks per 1/16, gate off on clock 3, latencies, slide only on the second note, accent/DAC strobe) and 10.06.2004 (accent on a slid-into note not heard on the original).
- Robin Whittle, *Investigating the Slide function of the TB-303* (1999, note added 2012). <http://www.firstpr.com.au/rwi/dfish/303-slide.html> (local `private/docs/whittle/303-slide.txt`). Used for: clock/gate/slide diagrams and the 3.5-clock gate claim.
- Robin Whittle, *TB-303 mods and maintenance*. <http://www.firstpr.com.au/rwi/dfish/303-mods/> (local `private/docs/whittle/303-mods.txt`). Used for: RAM chip allocation, RAM corrosion, memory retention.
- Robin Whittle, *Devil Fish Manual* and Devil Fish main page / ECO page. <http://www.firstpr.com.au/rwi/dfish/>, <http://www.firstpr.com.au/rwi/dfish/ECO/> (local `private/docs/whittle/`). Used for: CV OUT 1–5 V mapping; gate-out drive; accent latch persistence and power-on state; slide RC; battery-voltage failures; DIN socket switch; 6-bit DAC; note-start latency.
- MAME, `src/mame/roland/roland_tb303.cpp` (local `private/reference/mame/`). Used for: CPU type and clock, 3 × µPD444C, 1.8 ms IRQ, key and switch names, port notes ("I1: pitch data latch strobe, I2: gate signal").
- Abstrakt Instruments (Avalon Bass) via Matrixsynth, 19 Aug 2015. <https://www.matrixsynth.com/2015/08/avalon-and-tb-303-behavior-on-long.html>. Used for: "a slide starts from the last tie".
- KVR forum extracts on the slide/strobe circuit (local `private/docs/papers/forums/kvr_threads_extracts.txt`).
- Wikipedia, *DIN sync*. <https://en.wikipedia.org/wiki/DIN_sync>. Used for: pinout convention; "The TB-303 has a switch in the DIN socket".

**Clones and re-implementations**

- x0xb0x firmware (ladyada, 2005–2007), git mirror of SourceForge CVS: <https://github.com/a1k0n/x0xb0x/tree/master/firmware>; project <https://sourceforge.net/projects/x0xb0x/>; manual pages mirrored at <https://github.com/x0xb0x/x0xb0x.github.io> (originally `ladyada.net/make/x0xb0x/`); PDF manual <https://sourceforge.net/projects/x0xb0x/files/documentation/manual/x0xmanual.pdf>; adafruit forum thread <https://forums.adafruit.com/viewtopic.php?f=7&t=21131>. SokkOS: <https://github.com/ssidorenko/sokkos>.
- Roland TB-303 software (Roland Cloud) owner's manual (local `private/docs/roland-plugin/TB-303_Manual_E.txt`).
- Roland TB-03 owner's manual: <https://static.roland.com/assets/media/pdf/TB-03_eng03_W.pdf>.
- Roland TB-3 owner's manual: <https://static.roland.com/assets/media/pdf/TB-3_OM.pdf>.
- Behringer TD-3 quick start guide (v4): <https://www.adorama.com/col/productManuals/BETD3BB.pdf>.
- rv0, *Behringer TD-3 bugs and thoughts*: <https://rv0.be/articles/td3-bugs-thoughts/>. Used for what the original allows: more than 16 pitches, multi-pattern writing, sustain keys, the transpose display.
- Anton Savov, *The 303 and MIDI* (2014), <https://antonsavov.net/articles/303andmidi/>. Also *n0nx0x2* and its differences table: <https://antonsavov.net/x0xb0x/n0nx0x2/>, <https://antonsavov.net/x0xb0x/n0nx0x2/diffs/>.
- E-RM, *Implementing DIN Sync* (2014): <https://www.e-rm.de/data/E-RM_report_HowToDinSync_10_14_EN.pdf>.
- Robin Whittle, Analogue Heaven post on Run/Stop and the first clock, and on pin 4 TAP (2003): <https://analogue.bytenoise.co.uk/message/736cc620f9fa521b15d06514f29cdc7f76de30b8>.
- Kenton, *Connecting TB-303, SH-101, MC-202*: <https://kentonuk.com/support/connecting-tb-303-sh-101-mc-202/>.
- Forum reports (hearsay):
  - vintagesynth, half-inserted DIN plug gives sync out: <https://forum.vintagesynth.com/viewtopic.php?t=42515>
  - Squarp community, pin 4 held high: <https://squarp.community/t/tb-303-din-sync-issue/519>
  - MPC-forums, SPP and "one step late": <https://www.mpc-forums.com/viewtopic.php?p=1697658>
- Battery and "random pattern" folklore:
  - <https://en-academic.com/dic.nsf/enwiki/15917>
  - <https://www.synthtopia.com/content/2009/10/16/the-mystery-of-self-programming-synthesizers/>
  - <https://researchcatalogue.net/view/1579904/1584290>
  - DJ Pierre interview, RBMA 2012: <https://daily.redbullmusicacademy.com/2012/12/dj-pierre-interview>
- Simon Trask, "The Bass Race", *Music Technology*, Feb 1989: <https://www.muzines.co.uk/articles/the-bass-race/2177>.
- *Sound On Sound*:
  - Roland Boutique TR-09/TB-03 review: <https://www.soundonsound.com/reviews/roland-boutique-tr-09-tb-03>
  - TD-3-MO review ("splits the pitch and time aspects into separate linear processes"): <https://www.soundonsound.com/node/4926669>
- Zoe Blade, TB-303 notes (7/12 gate figure): <https://notebook.zoeblade.com/TB-303.html>.
- Tony Bacon, "Roland TB-303 Bass Line" review, *Electronics & Music Maker*, April 1982. <https://www.muzines.co.uk/articles/roland-tb-303-bass-line/4250>. Contemporary overview: 64 patterns of up to 16 steps, 7 tracks of 64 patterns, two step modes.
- Open303 (Robin Schmidt), `rosic_AcidPattern.cpp` default `stepLength = 0.5` (local `private/docs/papers/open303/`). An independent software model with a 50% gate.
