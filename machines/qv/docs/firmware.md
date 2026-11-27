# Firmware

The QuadraVerb Plus's v2.03 and the QuadraVerb's v1.07 EPROM images (27C512, U19) on the 80C31
(U21): what the board model gives them and what has been checked. Map and wiring as `core/board.h`
has them (copied from the GT's, `machines/qvgt/docs/firmware.md`, whose board was derived from this
one); their sources in `research.md`.

## The images

The board wires the EPROM's A0-A7 in reverse; `Board::prepareRom` swaps them back (it accepts either
form, and only images with an LJMP at reset and "(C) 1989 ALESIS", so not the GT's). Descrambled,
v1.07 starts `02 00 AB` and v2.03 `02 00 E0`.

## Boot

From reset with a blank RAM (both images): the LCD shows all blocks, clears at about 70 ms, then
the banner (v2.03 "    ALESIS / QUADRAVERB Plus" at 198 ms; v1.07 "***  ALESIS  *** / ** QUADRAVERB
**" at 176 ms), "(C) 1989 ALESIS / CORPORATION" at 1.47 s, and "PROGRAM 00." with "BASIC PROGRAM"
in the edit buffer at 2.9 s. With a blank RAM the program names are empty.

The factory programs, two ways, both through the panel keys:
- STORE, PAGE up three times ("RECALL ALL 90 ALESIS PROGRAMS"), STORE: "ALL ALESIS PROGRAMS
  STORED", landing on program 00. `Unit::recallFactoryPrograms` does this (the plugin, the first
  time; the tools).
- Power up holding BYPASS and PROG (service manual 5.0, "reinitialized"): the programs, and "BASIC
  PROGRAM" left in the edit buffer.

## Factory programs

Listed by MIDI program change after the recall (2026-11-27). Programs 00-79 are the same in both;
80-89 differ: the Plus's show its new configurations (multi-tap delay, sampling, auto-panning,
tremolo, ring modulator, resonators). Names as the LCD shows them, spaces trimmed.

| # | v2.03 (Plus) | v1.07, where different |
|---|---|---|
| 00 | EQ+CRS+DLY+RVB | |
| 01 | PRE EQ REVERB | |
| 02 | POST EQ REVERB | |
| 03 | GATED REVERB | |
| 04 | STEREO  CHORUS | |
| 05 | STEREO  FLANGE | |
| 06 | PINGPONG DELAY | |
| 07 | DETUNE  +12 | |
| 08 | LEZLY aftr tch | |
| 09 | 5 BAND PARA EQ | |
| 10 | SMALL ROOM | |
| 11 | MEDIUM ROOM | |
| 12 | LARGE ROOM | |
| 13 | SMALL HALL | |
| 14 | MEDIUM HALL | |
| 15 | Concert Hall 1 | |
| 16 | Concert Hall 2 | |
| 17 | SML VOCALPLATE | |
| 18 | MED VOCALPLATE | |
| 19 | LRG VOCALPLATE | |
| 20 | LO DIFUZ CHMBR | |
| 21 | RICH CHAMBER | |
| 22 | KINGS  CHAMBER | |
| 23 | SML TIGHT GATE | |
| 24 | BRITELITE GATE | |
| 25 | SMOOTH  GATE | |
| 26 | MEAN GATE | |
| 27 | GATE PLUS VERB | |
| 28 | FLANGE/GATE | |
| 29 | FLANGE/GATE 2 | |
| 30 | MONO  FLANGE | |
| 31 | TRIGGER FLANGE | |
| 32 | TWITCH FLANGER | |
| 33 | MONO  CHORUS | |
| 34 | CHORUS ROOM | |
| 35 | CHORUS REVERB | |
| 36 | CHORUS DELAY | |
| 37 | STEREO PITCH | |
| 38 | LOOSE LEZLIE | |
| 39 | P.BEND LEZLIE | |
| 40 | 167 ms DELAY | |
| 41 | 250 ms DELAY | |
| 42 | 500 ms DELAY | |
| 43 | 1500 ms DELAY | |
| 44 | ECHO 2the LEFT | |
| 45 | ECHO-FLANGE | |
| 46 | FLANGED ECHOES | |
| 47 | FLANGED ECHO 2 | |
| 48 | REVERSE ECHO | |
| 49 | PichReversEcho | |
| 50 | RAD KICK | |
| 51 | AMAZING BASS | |
| 52 | BALLAD SNARE 1 | |
| 53 | BALLAD SNARE 2 | |
| 54 | WET RHYTHM GTR | |
| 55 | WET ECHO STRAT | |
| 56 | SKANK GUITAR | |
| 57 | LEAD GUITAR 1 | |
| 58 | Lead Guitar 2 | |
| 59 | GREASE GUITAR | |
| 60 | CONCERT GUITAR | |
| 61 | ACOUSTIC GTR | |
| 62 | ROCK ACOUSTIC | |
| 63 | CHORUSTRINGS 1 | |
| 64 | CHORUSTRINGS 2 | |
| 65 | Roadz Chorus+ | |
| 66 | HOLMES'Plate | |
| 67 | SUSPEDAL LEZLY | |
| 68 | STEREO SPREAD1 | |
| 69 | STEREO SPREAD2 | |
| 70 | DELAYED REVERB | |
| 71 | DEEP SPACE EKO | |
| 72 | RONG WAY VERB | |
| 73 | VERB SHIFTER | |
| 74 | MULTI  VOCAL | |
| 75 | TAJ MAHAL modw | |
| 76 | VELOCITY ECHO+ | |
| 77 | VELOCITY ECHO- | |
| 78 | VELOCITY+REVRB | |
| 79 | VELOCITY-REVRB | |
| 80 | MULTI TAP | ROUGH TUBE ! |
| 81 | TOUCH PANTAP+ | ROUGH PIPE ! |
| 82 | MEDM TAP PAN~ | BEAM ME UP !!! |
| 83 | MULTITAP 80BPM | VERBATUM DELI |
| 84 | FLASHBACK | QUADREAMVERB |
| 85 | SAMPLING QV | ELECTRIC BLUE! |
| 86 | AUTO-PANNING | DTUNE~SPRD~RVB |
| 87 | TREMOLO QV | CHRUS~ECHO~RVB |
| 88 | RNG MOD+4 RHDS | FLANG~ECHO~RVB |
| 89 | RESONATION+RVB | DEMO CONFIG |

## The panel

Mapped by pressing each key through the firmware (2026-11-27, v2.03; the GT's matrix, with NAME and
MOD where the GT has PRE-AMP and NAME/MOD).

| Key | Name | What the firmware shows | LED (latch, bit; active low) |
|---|---|---|---|
| 0 | EQ 4 | LOW EQ FREQUENCY (program 00) | 0800h b7 |
| 1 | NAME 9 | EDIT NAME | 0800h b6 |
| 2 | VALUE down | the previous program | |
| 3 | VALUE up | the next program | |
| 4 | MIDI 5 | MIDI CHANNEL | 0400h b7 |
| 5 | BYPASS 0 | (bypass) | 0400h b6 |
| 6 | PAGE down | | |
| 7 | PAGE up | (on the program page) COMPARING | |
| 8 | PITCH 3 | PITCH MODE | 0800h b5 |
| 9 | MOD 8 | MOD 1 SOURCE | 0800h b4 |
| 10 | STORE | STORE PROGRAM AT LOCATION | 0400h b5 |
| 11 | PROG | PROGRAM nn | 0600h b7 |
| 12 | DELAY 2 | DELAY TYPE | 0800h b3 |
| 13 | MIX 7 | DIRECT SIGNAL | 0800h b2 |
| 14 | CONFIG 6 | CONFIGURATION | 0800h b0 |
| 15 | REVERB 1 | REVERB TYPE | 0800h b1 |

The other latch bits are the GT's: 0600h b0-b4 the dry VCA's control ladder, b5 the key piezo's
reset (Q1), b6 the level detector's reset (Q2).

**Key force.** As the GT's: a piezo (Z2) under the keypad, timed against C56's ramp (service
manual 4.4). The plugin presses at 0.1 V, and at 2 V with Shift (`core/unit.h`).

## Self-diagnostics

`tools/service/check.py` (service manual 5.0: MIDI out looped to MIDI in, power up holding BYPASS
and CONFIG): "QUAD DIAGNOSTICS", ALL RAM OK, EPROM OK, LED TEST (the module LEDs walk), MIDI IN/OUT
OK, CLIP LED TEST (the DSP overflows), then the normal boot. Both images pass (2026-11-27).

## The Plus's configurations

`qvdspcheck` (v2.03), through SysEx edits (service manual 11.0: `F0 00 00 0E 02 01 <function>
<page> <three value bytes> F7`, the QuadraVerb's id 02; functions 1 reverb, 2 delay, 3 pitch, 4 eq,
7 config, 8 mix, 9 mod, 10 name), all passing (2026-11-27):
- Ring modulator (program 88, configuration 5, PITCH pages 0-2): a frequency shifter, the other
  sideband and the carrier 31 dB under the shifted tone; 100 Hz of SPECTRUM SHIFT moves 1 kHz to
  1,071.5 Hz, 0.71 of the display, as on the GT (its firmware.md, "Timer 0": the CPU oscillator's
  tick rate when its handler runs long). Taken as the unit's own behaviour.
- Resonators (program 89, configuration 6, PITCH pages 0-6): each rings at its semitone ratio
  within the integer delays' rounding (worst -31 cents at +24 semitones).
- Sampling (configuration 7, DELAY pages 0-7): BYPASS records with the audio trigger off, the
  recording loops at its SAMPLE LENGTH (1.55 s at most, 0-155) at the recorded pitches, MIDI notes
  transpose it.
- The audio trigger fires at 0.60 V of envelope, the panel's -18 dB LED (0.63 V under the clip
  LED's 5 V).

## MIDI

MIDI in, the firmware's MIDI THRU (on by default) and MIDI out work; program changes select
programs (the plugin's tests).

## Still open

- The level detector's thresholds against real units (the audio trigger sits at the -18 dB LED, as
  the Plus addendum says).
