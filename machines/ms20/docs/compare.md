# MS-20: first comparison against recordings

2026-11-15. The model (ms20render as built in `build/ms20` that day, default calibration) against
YouTube recordings of real units (`private/ms20/reference/youtube/`, `SOURCES.md`). Every number
below comes from `tools/compare/first.py` (it reads the clips from the private folder and renders the
model; `--scan` adds the self-oscillation search) unless it says otherwise. dB figures are ratios
inside one recording: a clip's gain is unknown, so no absolute level is compared.

## Method

**Clips.** Questions 1 and 2 use one owner's unit (atoooy, "KORG MS-20 SOUND" parts 1 and 3,
t8t17yEHHUQ and 2d8UXDNJvkI, Jan-Feb 2013): vintage, **filter revision not stated**. The VCO
shapers and the noise generator are on the main board in both revisions (the late KLM-127, "NEW
PRODUCTION", draws VCO 2's shaper the same, checked for this page); the filters differ (Rev 2's
LM13600 board limits in its feedback path, `research.md` §1.2). The main results here don't lean on
the filters: the level ratios move under 0.4 dB through the model's whole chain, the narrow pulse's
duty comes from spectral nulls, the pink/white ratio cancels the chain. Where a number does depend on
the filters (the triangle's H3, the even harmonics) it says so. Question 3 uses the Rev 1 clips
(YH3g01qU7O8, 96N82NCbKlQ, 1MXo9aPZETA, tIb5X9X_tPk, dxO5ywTETIY).

**Settings, from the videos.** The atoooy clips state their settings only on screen, so their 720p
videos were fetched to the scratch folder (not kept, not in the repo) and read frame by frame:

- t8t17yEHHUQ has one cable (SIGNAL OUT) and on-screen captions: "VCO1" 10 s, saw 14-20 s, triangle
  22-28 s, square 32-36 s, "pulse width change" 40-56 s (VCO 1's WAVE FORM and PW under the left
  hand); "VCO2" 76 s, saw 80-86 s, square 88-90 s, "pulse wave" 92-98 s (VCO 2's WAVE FORM, then its
  SCALE turned at 96-98.5 s); "VCO1+VCO2 saw unison" from 102 s (VCO 1 LEVEL turned up 104-107 s,
  VCO 2 PITCH from 108 s). The YouTube chapter times run 2-5 s late against the audio.
- Knobs, by pointer angle against the same knob's known-off position: VCO 1 LEVEL and VCO 2 LEVEL at
  full whenever their VCO plays (the two pointers sit at the same angle at 30, 85, 89 and 115 s; the
  audio confirms VCO 1's, below: its saw/triangle ratio puts LEVEL at 9.6), HPF CUTOFF about 0, LPF
  CUTOFF about 9-10 (the saw's harmonics fall as the model's do at 10, below), both PEAKs about 0.
- 2d8UXDNJvkI: PINK NOISE patched to EXT SIGNAL IN (4 s), the filters worked 13-54 s, "Mix WHITE
  NOISE from VCO" from 44 s.

**Per event.** An event is the steady part of one held note (from a 50 ms pitch and level track; no
window crosses a retrigger), or one stationary noise stretch. Harmonics: least squares at the
event's fundamental (Hann-weighted, many periods: an average over the event, not one cycle). Noise:
third-octave Welch spectra. The model: ms20render with one held key (key 24 = 524 Hz, key 36 = 1047
Hz, 8'), the same analysis on SIGNAL OUT and on probed board signals.

**Checks on the measurement.** H1 is steady across an event's windows within 0.1 dB (VCO 1 saw
-20.50 / -20.52 / -20.58 dBFS, triangle -18.00 / -17.99 / -17.99, square -16.75 / -16.73 / -16.74). The
recorded saw's harmonics fall faster than an ideal saw's (520 Hz, H2..H7: -6.7 -10.6 -13.5 -15.9
-18.0 -19.9 dB against -6.0 -9.5 -12.0 -14.0 -15.6 -16.9) exactly as the model's chain makes them
(-7.0 -10.8 -13.5 -15.8 -17.7 -19.5 at SIGNAL OUT), and the square's odd harmonics likewise (H3, H5,
H7: recorded -10.0 -15.4 -19.4, model -9.9 -14.9 -18.7): the analysis invents no deficit, and the
filters' settings read from the video hold. One trap found and fixed: from 97 s VCO 2 plays at
264.6 Hz with H2 its strongest line; a fit at 529 Hz put the narrow pulse's nulls at "H8, H16, H24".

## 1. VCO waveform levels and the core's swing

The saw and the triangle scale with the core's swing (node 10/11, model 5.46 Vpp); the pulse and
square don't (the 4558 comparators' ±14 V through R2/R1 or R63/R64: ±0.79 V at the switch). So
triangle/square and saw/square measure the swing: +10 % is +0.8 dB. Saw/triangle doesn't: it
measures LEVEL's loading of the saw's 100k (R4).

### 1.1 VCO 1 (t8t17yEHHUQ)

| Event | Windows (s) | f0 (Hz) | H1 (dBFS) |
|---|---|---|---|
| saw | 12.8-13.3, 14.2-15.0, 16.4-17.4 | 520.2 | -20.53 |
| saw | 17.8-18.7, 19.8-20.4 | 1042.1 | -20.68 |
| triangle | 22.4-23.4, 23.8-24.5, 24.8-25.6 | 520.2 | -17.99 |
| triangle | 26.2-26.75, 27.4-28.5 | 1042.1 | -18.15 |
| square (PW 0) | 32.6-33.5, 34.0-34.5, 35.8-36.3 | 520.2 | -16.74 |
| square | 35.0-35.5, 36.6-37.7 | 1042.1 | -16.91 |

H1 ratios at SIGNAL OUT (dB):

| | saw/square | triangle/square | saw/triangle |
|---|---|---|---|
| recording, 520 Hz | -3.79 | **-1.25** | -2.54 |
| recording, 1042 Hz | -3.78 | **-1.24** | -2.54 |
| model, LEVEL 10 | -4.20 | -1.40 | -2.80 |
| model, LEVEL 9.5 | -3.86 | -1.39 | -2.46 |
| model, LEVEL 9 | -3.53 | -1.38 | -2.15 |
| model, LEVEL 8 | -2.96 | -1.35 | -1.61 |

(model key 24; key 36 within 0.02 dB.) Saw/triangle puts LEVEL at 9.62 in the model; there
triangle/square is -1.40 dB against the recording's -1.25: **VCO 1's swing x 1.017 (5.55 Vpp)**.
The parts' tolerances limit this: the square's level is the 4558's swing times R1/(R1+R2) (5 %
resistors), the triangle's goes through R6/R7 and the rectifier's R8/R9; about ±0.6 dB together,
±7 % on the swing (estimated, not measured). Saw/square agrees once LEVEL is taken from saw/triangle (it's the same pair of
numbers).

The triangle's shape: H2..H5 recorded -22.4 -25.0 -32.3 -36.9 dB, model -29.7 -26.2 -47.5 -34.6. The
odd harmonics agree: the triangle's own H3 (-19.1 dB at the switch) is cancelled down to -25/-26 dB
by the limiters' odd distortion in both, which also checks the limiters' strength at this level (if
this unit is a Rev 1). The
even ones don't: the real triangle has 7 dB more H2 and 15 dB more H4. In the model that H2 comes
from the VCA (the LPF's output has -45 dB, check point 19 -29.7 dB); see 3.

### 1.2 VCO 1's PW sweep (t8t17yEHHUQ 38-57 s)

From the square (-19.1 dBFS, 50 ms RMS) the level falls steadily to -36 dBFS and then to the floor
(about -66) at **44.30-44.45 s and 51.35-52.65 s**: the pulse narrows away (the key releases at
38.00, 39.25, 40.65 and 42.05 s drop straight from the square's level and aren't counted). The pulse
goes when PW's threshold passes node 10's peak; PW's range tops out at 3.49 V (R10/R11/VR9/R12), so
**node 10 is under 6.98 Vpp** (x 1.28; about ±4 % with the parts). The block diagram's 7.5 Vpp
would never lose the pulse (3.5 % at full PW). The model's goes at PW 7.8. Where on the knob the real
one goes is hidden under the hand.

### 1.3 VCO 2's narrow pulse (t8t17yEHHUQ 92-100 s)

The narrow pulse is high while node 11 is above IC6A's threshold, R70/R69's 2.705 V: duty
d = 1/2 - 2.705 / Vpp. Its spectrum has nulls at n = 1/d, 2/d, independent of the chain:

| Window (s) | f0 (Hz) | Nulls | Duty (fit) | Node 11 for 2.705 V |
|---|---|---|---|---|
| 96.45-96.90 | 529.8 | H16 (-48 dB, neighbours -33 / -36) | 6.20 % | 6.18 Vpp |
| 97.00-98.00 | 264.6 | H16 | 6.30 % | 6.19 Vpp |
| 99.00-100.00 | 264.6 | H16, H48 | 6.30 % | 6.19 Vpp |

**d = 1/16 = 6.25 %.** At 1059 and 2121 Hz the nulls fall above the codec's 15 kHz; there the
narrow/square H1 ratio (sin(pi d) for pulses of one height) gives 5.7 % (1059 Hz: narrow -32.0 dBFS
in 92.60-96.40, square -16.97 in 88.05-89.05, ratio -15.04 dB) and 5.8 % (2121 Hz: -32.6 against
-17.70, -14.91 dB). **Node 11 = 6.11-6.19 Vpp, x 1.12-1.13 the model's**; R69/R70's 5 % give about
±6 % (estimated). The 4558's finite gain-bandwidth delays the slow (trailing) edge some µs at these
slopes (about 8 µs at 530 Hz by hand, for 3 MHz), which lengthens the pulse by about 0.4 % of the
period: about 1 % off the swing, inside that.

The model: d = 0.46 % at 5.46 Vpp; its narrow pulse is 57 dB under the square at the mixer, 56 dB
at SIGNAL OUT (key 36, 8'; 66 dB at 4'), against the recording's 15 dB. **The model's narrow pulse is
missing.** For the drawn threshold the swing has to be 6.2 Vpp; for the model's swing, the
threshold has to be 2.39 V (R69 about 19K for 22K, or R70 about 116K for 100K: both outside 5 %).

### 1.4 VCO 2's saw is a third of what the drawing gives (t8t17yEHHUQ 80-92 s, 107.5-108.8 s)

| Event | Windows (s) | f0 (Hz) | H1 (dBFS) |
|---|---|---|---|
| saw | 80.10-80.65 | 986.1 | -20.62 |
| saw | 86.45-86.75 | 1059.2 | -21.03 |
| saw | 86.90-87.80 | 2121.2 | -21.33 |
| square | 88.05-88.40, 88.90-89.05 | 1058.9 | -16.97 |
| square | 88.55-88.80, 90.80-92.00 | 2120.6 | -17.70 |

| | VCO 2 saw/square, H1 | VCO 2 saw / VCO 1 saw, H1 |
|---|---|---|
| recording | -4.06 dB (1059 Hz), -3.63 dB (2121 Hz) | -0.16 / -0.27 dB (unison, both at LEVEL 10: 107.5-108.5 / 107.6-108.8 s, VCO 1 259.85 Hz, VCO 2 264.20 Hz fitted as two harmonic series, 92-93 % of the power); -0.35 dB (solo, 86.45-86.75 against 17.8-20.4 s) |
| model, LEVEL 10 | +4.71 at the mixer, +2.12 at SIGNAL OUT | +9.51 at the mixer, +6.30 at SIGNAL OUT |
| model, LEVEL 8 / 6 | +4.49 / +4.36 mixer, +2.79 / +3.35 SIGNAL OUT | |

In the real unit VCO 2's saw is as loud as VCO 1's (within 0.35 dB, in H1 and H2) and 3.6-4.1 dB
under its own square. In the model, which takes node 11 straight to SW-4 as both Rev 1 drawings
show (scan A, and the late KLM-127 checked again for this page), it is three times VCO 1's (VCO 1's
goes through R4 100k) and 2-5 dB over its square **at any LEVEL**: an 8 dB gap that no setting
closes. The model's VCO 2 saw also overdrives the filters' limiters at LEVEL 10 (H2..H7 at SIGNAL
OUT -11.0 -13.5 -17.1 -19.8 -22.3 -24.6 dB) where the real one is as clean as VCO 1's (VCO 2
86.45-86.75 s: -7.1 -11.7 -15.4 -18.7 -22.0 -24.8; VCO 1 17.8-18.7 s: -7.2 -11.7 -15.3 -18.7 -21.5
-24.8).

The squares settle the alternative that VCO 2's LEVEL sat lower than VCO 1's: VCO 2's square equals
VCO 1's (-16.97 against -16.91 dBFS at about 1050 Hz), as the identical comparator dividers give at
equal LEVEL, and the pointers agree. One unit, one clip, but every reading in it agrees: VCO 2's saw
reaches the switch at about a third of node 11, as through a 100-120k series resistor into LEVEL's
load (VCO 1's arrangement). Whether that is this unit (a repair, a mod) or an undrawn production
change is open.

**Settled 2026-11-24.** A second Rev 1 unit (BwBwJosbQkg, the same arpeggio on each source, both
LEVELs read 9.5-10, filters open; RMS of the F3 note): VCO 1 saw -13.2, VCO 2 saw -13.3, VCO 2
square -8.7 dBFS: VCO 2's saw 4.6 dB under its square and level with VCO 1's. Its Rev 2 unit: -3.7
dB under its square, 2.1 dB over VCO 1's saw. Board photos then showed **R62 100K** fitted where
the circuit diagram draws no part (`circuit/vco.md` §2). With R62 in the model: VCO 2's saw against
its square -3.76 dB (H1, mixer; -2.5 dB RMS at SIGNAL OUT), against VCO 1's saw +1.1 dB (H1): VCO
2's 6.80 V swing (§1.5) over VCO 1's 6.02 V.

### 1.5 What it says about the swing

| Evidence | Swing against the model's 5.46 Vpp |
|---|---|
| VCO 1 triangle/square, 520 and 1042 Hz (1.1) | x 1.017 ± 0.07 |
| VCO 1's pulse vanishing in the PW sweep (1.2) | under x 1.28 |
| VCO 2 narrow-pulse duty, spectral nulls (1.3) | x 1.13 ± 0.07 |
| VCO 2 narrow/square H1 (1.3) | x 1.12 |
| (model) VCO 2's high trim runs out; x 1.11 centres both trims (`improvements.md`) | x 1.11 |

- **VCO 2: move it.** The recording's narrow pulse can't exist at 5.46 Vpp (40 dB apart); 6.2 Vpp
  at node 11 (the core's top-to-trip 6.02 V -> about 6.8 V) gives it, and is what centres VCO 2's
  §7-2 trims. Strong that the model is wrong; medium on the cure (one unit; a 12 % lower threshold
  would also do, but 5 % parts reach 8 % even at both extremes).
- **VCO 1: no evidence for a move**, ±7 %. +11 % would put its triangle/square 0.8 dB over this
  recording, about 1.3 tolerance-sigma: allowed, not asked for.
- **One shared swing** fits both at about x 1.07 (±0.05), but leaves VCO 2's narrow pulse at 3.7 %
  (nulls near H27, H1 4.6 dB down) against 6.25 %. The trims' x 1.11 gives VCO 2 5.4 % and VCO 1's
  triangle/square +0.8 dB. Two separate swings fit best: VCO 1 about 5.5-5.9 Vpp, VCO 2 about
  6.2 Vpp.
- A physical reason for the two to differ: the top a reset reaches. `vco.md` §6's scratch run with
  charge storage put VCO 1's top 0.2 V higher; the same storage time charges VCO 2's half-size C11
  further. Worth an ngspice run per core with charge storage before choosing.

## 2. Pink noise against white

PINK (patched into EXT SIGNAL IN, unity) and VCO 1's WHITE (LEVEL 10 on the panel) reach the mixer
with the same gain, so the ratio of their spectra is the noise board's own pink filter, whatever the
filters and recording chain do. A = PINK alone, 2d8UXDNJvkI 9.1-12.9 s; W = WHITE alone, t8t17yEHHUQ
187.0-202.5 s (the same unit and setup, part 1); B = both, 2d8UXDNJvkI 55.6-58.2 s. Third-octave PSD
(dB re 1/Hz, full scale), selected bands:

| f (Hz) | A pink | W white | A/W | model pink/white | circuit (32.35, pole 32.9 Hz) | W re 500 Hz | model W at SIGNAL OUT re 500 Hz |
|---|---|---|---|---|---|---|---|
| 31 | -45.2 | -74.0 | 28.8 | 27.7 | 27.4 | -5.7 | -4.9 |
| 50 | -45.0 | -70.2 | 25.2 | 25.0 | 25.0 | -1.9 | -2.3 |
| 99 | -49.0 | -68.8 | 19.8 | 20.0 | 20.2 | -0.5 | -0.3 |
| 198 | -54.8 | -67.8 | 13.0 | 14.4 | 14.5 | +0.5 | -0.4 |
| 397 | -59.8 | -68.0 | 8.3 | 8.5 | 8.5 | +0.2 | 0.0 |
| 794 | -65.5 | -68.1 | 2.6 | 2.5 | 2.5 | +0.2 | -0.1 |
| 1587 | -71.9 | -68.6 | -3.4 | -3.5 | -3.5 | -0.3 | -0.4 |
| 3175 | -79.5 | -70.1 | -9.5 | -9.6 | -9.5 | -1.8 | -2.0 |
| 6350 | -89.3 | -74.1 | -15.2 | -16.0 | -15.5 | -5.8 | -7.3 |
| 8000 | -93.8 | -76.6 | -17.1 | -18.3 | -17.5 | -8.4 | -10.5 |
| 10079 | -98.9 | -79.9 | -19.0 | -20.9 | -19.5 | -11.6 | -14.8 |
| 12699 | -106.9 | -84.6 | -22.3 | -23.8 | -21.5 | -16.3 | -20.4 |

(The model's pink and white are the noise block's J19 and J25 probed: ms20render can't patch a cable,
and those are what the cable and the VCO 1 switch carry to the mixer.)

- **Slope, 100 Hz-2 kHz: pink -6.02 dB/octave, white -0.13, pink/white -5.89.** The model's
  pink/white -5.97, the drawn circuit -5.96; a -3 dB/octave pink would read -3.0. One pole fitted to
  A/W over 50 Hz-5 kHz: 36.0 Hz with 29.4 dB below it (residual 0.39 dB), against the drawn 32.9 Hz
  and 30.2 dB. **Supports klm128.md's one pole: "pink" is red above 33 Hz.** A/W's absolute level
  needs the two clips' gains to match (same owner and setup, likely, not known); the slope and the
  pole don't.
- One take, both at once: B fits a·A + b·W with a = -1.19 dB, b = +0.79 dB, 0.62 dB RMS residual
  (40 Hz-12 kHz): the same pink and the same white, the pink 1.2 dB down (the louder sum compressed
  in the limiters). Pink and white cross at 800-1000 Hz; the circuit at equal gains: 1064 Hz.
- By-product, the white's top: from 4 kHz up the model's white at SIGNAL OUT falls faster than the
  recording's, 2.1 dB more at 8 kHz, 3.2 at 10 kHz, 4.1 at 12.7 kHz. The same chain passes the saw's
  harmonics 2-3 dB *less* than the model at 8-10 kHz (VCO 1 saw 1042 Hz, H8..H10), so the real white
  source holds about 4-6 dB more at 8-10 kHz than the model's (rz3 = 10k, its band's top 7.2 kHz):
  it suggests rz3 nearer 1-3k (a band to 11-17 kHz, `klm128.md` §1.4). Weak: one take, through a
  codec.
- **Settled 2026-11-24: rz3 1k, 4.6 µV/√Hz.** VCO 1's WHITE (187-192 s) against its saw (14.2-15.0 s)
  in the same take, both through the same chain (LPF 9.5, LEVEL 9.6): WHITE's 200 Hz-4 kHz band
  stands 9.99 dB under the saw's H1, ours (rz3 10k, 10 µV/√Hz) 11.58, now 9.90. Its spectrum re 400 Hz
  at 6.35 / 8 / 10 / 12.7 kHz: recording -6.4 / -8.9 / -12.2 / -16.8 dB; ours with 10k -8.0 / -11.4 /
  -15.9 / -21.7, with 1k -5.8 / -8.3 / -11.4 / -15.5. The saw's H12-H24 come out 0.4-3.6 dB under the
  model's in the same chain, so the source may be brighter still (rz3 100 Ω-1k all fit). With 1k
  the specification's equal WHITE and PINK levels hold (pink +1.75 dB re white, against +6.25 with
  10k). J25 gives 2.1 Vpp (6 sigma) at this level: the specification's 5 Vpp is VR3's business
  (no procedure in §7).

## 3. Resonance and self-oscillation (Rev 1)

> **Correction, 2026-11-23.** YH3g01qU7O8 is recorded from ESP BAND-PASS OUT (SIGNAL OUT patched to
> ESP SIGNAL IN; read off the video), and both tones below are VCO 1 notes, not self-oscillation:
> the 386 Hz tone is a key held 654.78-684.2 s while the HPF knobs are swept, and the 1042.6 Hz
> "glide" is VCO 1's saw on the top key as its SCALE switch turns from 16' to 8' (the 110 Hz line
> is 2194.9 - 2 x 1042.6 Hz, VCO 2 against it). The harmonic rows from YH3 say nothing about the
> limiter. A real self-oscillation at a held knob is in QjZgZodB614 (8.0-8.3 s, 1673 Hz): H2..H5
> -26.3 -18.8 -35.6 -32.7 dB against the model's -31.5 -22.1 -41.3 -38.0 at PEAK 10
> (`improvements.md`).

**What the clips hold.** Free self-oscillation, with nothing else sounding, is rare in them. The
scan (`--scan`: one spectral line holding 85 % of the power for 0.4 s or more) finds runs in
YH3g01qU7O8 and tIb5X9X_tPk only. Read one by one: tIb5X9X_tPk's follow the keyboard with vibrato or sit on a
VCO's harmonic (played notes through a resonant filter); 96N82NCbKlQ's sweeps ride on VCO notes (and
237-247 s is a falling VCO pitch, not the filter); 1MXo9aPZETA's resonant sweeps (200-230 s) are
bowls over held notes; dxO5ywTETIY's "PEAK at 3/4" (2:11-3:16) shows the resonance lifting the
drone's harmonics with no free line in either unit, as the model's onset at PEAK 8.6 has it (which
half is the vintage unit is on screen only). Two YH3g01qU7O8 stretches are self-oscillation: a held
**386 Hz tone, 668-683 s**, odd-heavy, with no other line within 30 dB (677.6-678.75 s; 668-673 s
carries sidebands 23-55 Hz either side, about 10 Hz apart: a slow modulation); and a **1042.6 Hz
tone, 637.6-639.0 s**, the end of an upward glide from about 580 Hz (636.45-637.20 s, the oscillation
swept by CUTOFF), over a VCO at about 110 Hz (-29.3 dB) that puts sidebands at ±110 Hz (-29.3 /
-27.7 dB): the tone itself is no harmonic of it. Neither has a stated PEAK; the scan's other
YH3g01qU7O8 run (431.90-432.30 s, 442-448 Hz) was not examined.

| Event | f0 (Hz) | H2 | H3 | H4 | H5 (dB re H1) | half-period ratio | H2 phase |
|---|---|---|---|---|---|---|---|
| YH3 672.40-673.15 s | 386.3 | -24.6 | -18.3 | -44.4 | -24.6 | 0.952 | -21° |
| YH3 674.35-674.90 s | 386.9 | -30.5 | -18.1 | -42.3 | -33.8 | 0.968 | -48° |
| YH3 675.00-675.85 s | 386.2 | -29.2 | -18.4 | -39.5 | -34.1 | 0.966 | -40° |
| YH3 676.50-677.20 s | 386.7 | -29.2 | -18.8 | -38.5 | -33.3 | 0.958 | -37° |
| YH3 677.60-678.75 s | 385.9 | -31.2 | -20.0 | -42.1 | -39.5 | 1.002 | -91° |
| YH3 682.50-683.15 s | 386.5 | -33.0 | -15.9 | -37.7 | -33.9 | 1.025 | -147° |
| YH3 637.60-639.00 s | 1042.6 | -27.3 | -26.2 | -46.9 | -49.9 | 1.011 | -94° |
| model PEAK 10, CUTOFF 4.67, SIGNAL OUT | 386.4 | -32.8 | -20.4 | -39.9 | -35.3 | 0.982 | +68° |
| model PEAK 10, CUTOFF 4.67, LPF output | 386.4 | -25.2 | -20.3 | -35.3 | -35.1 | 1.066 | -148° |
| model PEAK 9.5, CUTOFF 6, SIGNAL OUT | 1112.7 | -34.5 | -26.0 | -47.2 | -46.9 | 0.991 | +74° |
| model PEAK 9.5, CUTOFF 6, LPF output | 1112.7 | -27.3 | -25.2 | -41.9 | -44.9 | 1.051 | -153° |

(The full locus, PEAK 8.8-10 at both cutoffs, is in the script's output. Half-period ratio: positive
half over negative; a recording's polarity is unknown, so r and 1/r are the same answer, and
inverting moves H2's phase by 180°.)

- **Harmonics (the limiter).** H3 rises with PEAK in the model (SIGNAL OUT, CUTOFF 4.67: -39 dB
  at 8.8, -33 at 9, -25 at 9.5, -20.4 at 10, the most it can give), so H3 alone only places PEAK.
  The 386 Hz tone's median H3 -18.4 dB (-15.9 to -20.0 over six windows) is **2 dB more than the
  model reaches at PEAK 10**, with H4 and H5 there within 1-2 dB (median -40.8, -33.9 against -39.9,
  -35.3): the real limiter holds the oscillation a little harder, or a little larger against the
  diodes' knee. That
  points the same way as filters.md's open question 3 (check point 19 0.69 Vpp self-oscillating
  against the block diagram's 2 Vpp maximum). The 1042.6 Hz tone matches the model at PEAK 9.5 in
  H3 and H4 (-26.2 / -46.9 against -26.0 / -47.2).
- **Even harmonics and asymmetry.** H2 at SIGNAL OUT is 3 dB (386 Hz, median -29.9) to 7 dB (1042
  Hz, -27.3) over the model's. The model's LPF output carries more (-25.2, -27.3) and its VCA then
  cancels part of it; the recorded H2 lies between the two (386 Hz) or equals the LPF output's
  (1042 Hz). Together with 1.1's triangle on the other unit (7 dB more H2 than the model, whose H2
  there is the VCA's), this fits the VCA's even-order distortion or operating point (Q19's rank and
  reverse beta, the SIGNAL OUT level item) better than the KORG35, but the VCA's drive (EG 2, the
  level into it) is unknown in both clips. The half-period ratio
  is not stable within one held tone (0.952-1.025, H2's phase -21° to -147° across the 386 Hz
  windows), so the waveform asymmetry can't be read from these recordings; the 1042.6 Hz tone's
  1.011 and -94° sit near the model's 0.991 and +74° inverted (1.009, -106°). The ringing asymmetry
  that filters.md checks (1.18:1 at PEAK 6) needs a square through the LPF below oscillation, which
  no Rev 1 clip isolates.

## Conclusions

| Question | Measured | Model | Verdict |
|---|---|---|---|
| VCO 1 triangle/square, saw/square (t8t17yEHHUQ 12-38 s) | -1.25, -3.79 dB (LEVEL 9.6 by saw/triangle) | -1.40, -3.94 at LEVEL 9.6 | supports the model's VCO 1 swing (x 1.02 ± 0.07) |
| VCO 1 PW sweep (38-57 s) | pulse narrows away (44.30, 51.35 s) | goes at PW 7.8 | supports: node 10 under 6.98 Vpp; rules out 7.5 Vpp |
| VCO 2 narrow pulse (92-100 s) | d = 6.25 % (nulls at H16, H48), 15 dB under the square | 0.46 %, 56-66 dB under | **contradicts**: node 11 needs 6.2 Vpp (x 1.13) |
| VCO 2 saw level (80-92, 107.5-108.8 s) | = VCO 1's saw, 3.6-4.1 dB under its square | since R62 (2026-11-24): +1.1 dB over VCO 1's, 3.8 dB under its square (before: 3 x VCO 1's, 2-5 dB over) | agrees (R62 on real boards; §1.4) |
| Pink/white slope (2d8UXDNJvkI, t8t17yEHHUQ) | -5.89 dB/oct, pole 36 Hz | -5.97, 32.9 Hz | supports |
| White noise's top (t8t17yEHHUQ 187-203 s) | 2-4 dB more at 8-13 kHz | rz3 10k | suggests rz3 1-3k (weak) |
| LPF self-oscillation, limiter (QjZgZodB614 8.0-8.3 s; the YH3 row was a VCO note, see §3) | H3 -18.8 dB at 1673 Hz | -22.1 at PEAK 10 (its most) | contradicts: 3-6 dB more distortion H2..H5 |
| Self-oscillation H2 / asymmetry | H2 3-7 dB over the model's SIGNAL OUT; asymmetry unstable | | can't tell (VCA drive, PEAK unknown) |

## What would settle the rest

- **VCO 2's saw**: settled 2026-11-24 (§1.4: a second unit and R62 on real boards).
- **The swing**: a scope trace of check point 10 or 11; another unit's narrow pulse (any VCO 2
  narrow-pulse note below about 900 Hz, where the first nulls fall under the codec's 15 kHz); VCO 1's PW knob position
  at the pulse's vanishing (a video with the knob in view); R69/R70 measured. In the model: the two
  cores' reset with charge storage in ngspice, for a reason the swings would differ.
- **Self-oscillation and the VCA**: the LPF self-oscillating alone at a stated PEAK with EG 2 and
  VOLUME stated (a Rev 1 unit; jmvzCeaDUIQ, MS-20 FS "MK1 filter sweep, peak at 100 %", is a
  reissue but states PEAK), recorded at a level that leaves the VCA's even harmonics readable; and
  any clean triangle with stated EG 2 to separate the VCA's H2 from the shaper's.
- **The white's band**: WHITE NOISE recorded straight from J25 (no filters), for rz3 and VR3.
