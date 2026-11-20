# RAT: what we know is wrong, and the plan

What the model gets wrong or hasn't checked, as the transcription and the comparisons with real
pedals found it (`docs/circuit.md`, `docs/research.md`, `docs/model.md`, the README's "Status"), and
what is planned. The target is the original RAT as the 1984 "White Face" small box (LM308; R4 1M,
no R12, no R15, C13 1 µF: `circuit.md` §6), since 2026-11-19; before that it was the US RAT2
(1988-), which is still sold new. The RAT2's values and the OP07 stay in the tools for the
comparisons. "Real" is outmoded's RAT2 (20 NAM captures, calibrated at +11.5 dBu) unless it says
otherwise.

`docs/model.md`'s Calibrations and Results tables predate the split LM308 and OP07 calibrations of
2026-10-09 (2a21467): the current trims are `capturedCalibration()` in `core/pedal.cpp`, the
current results the README's "Status".

## Known inaccuracies

### Sound

- **Asymmetry.** An LM308 clips its positive side slightly sooner (negative peaks 4.5 % larger on
  the 1990 RAT2), an OP07 its negative side much sooner (positive peaks 6-25 % larger); the model
  carries this in each op amp's swing limits (README, `pedal.cpp`). EGFxSet's China-made 2010 RAT2
  clips more asymmetrically than the model: 1.25 against 1.13 (README). Mechanisms the model
  doesn't have: the LM308's output stage sourcing and sinking into R8 and the diodes (AN-29's
  useful drive ±2 mA, about ±2.9 mA needed here: `research.md` §11), the 2N5458 off square law
  near pinch-off, a mismatched diode pair (`model.md`, "Open points"). A scope trace of a real
  pedal's pin 6 and clip node would settle it.
- **LM308 trims at their bounds.** The LM308's slew rate (0.3 V/µs; bench 0.11-0.24 V/µs at 9 V,
  0.2-0.28 at 15 V) and swing ended at the bounds of their bench ranges (README, `pedal.cpp`). The
  two LM308 captures that set them (pong's 1996 USA RAT2, rbrt's ~1994 RAT, `fit_lm308.py`) are
  uncalibrated. Recovery from saturation is characterised by no datasheet or model
  (`research.md` §10.2); the Y clamp stands for it, fitted only through the captures.
- **DISTORTION near zero.** On the 1990 RAT2 and the Aalto pedal the bottom of the range has less
  gain than the shipped law (README). On Aalto's 13 clips below 25 % the first fit was much
  brighter (+75 % centroid); a taper of 0.04 at mid took those clips from 6.6 to 3.5 dB LSD, but the
  captured RAT2s don't want it, so it isn't used (`model.md`). The only measured vintage DIST pot is
  a 1979 large box's, 10.1 % at mid (`research.md` §11).
- **VOLUME.** The first fit was 2.5-3 dB quiet at 3:00 against outmoded, and the Aalto clips needed
  about 5 dB more gain from VOLUME 50 % to 90 % (`model.md`). The refit's two-part law (mid 0.288,
  end 0.934, `pedal.cpp`) puts the level within 0.75 dB of outmoded (README); the Aalto trend is not
  re-measured.
- **Fit quality.** OP07, Captured: 5.2 dB LSD and -14.0 dB ESR against outmoded (fitted); held out
  6.2 dB (jakkwiat), 7.7 dB (romanrecords), 3.5 dB (EGFxSet). LM308 against the 1990 RAT2's scope
  shots: duty within 0.002, peak ratio within 1 %, even harmonics within 1 dB at DISTORTION 5-10
  (README). These are per capture and signal, not per pluck.
- **Waking from sleep.** Some of the circuit's DC settles over minutes; after a long silence the
  first ~50 ms of a note are as after a 2 s one (-20 dB difference at the attack, gone after a
  second). A host that suspends plugins by their tail does the same (README, "Idle").

### Behaviour and features

- **Part values.** The circuit's disagreements D1-D20 are settled by choice (`circuit.md` §8), none
  on a unit. The 1984 values (no R12, R4 1M, C13 1 µF, no R15) are the Multi-RAT's RAT column and
  Electrosmash's; no parts-side photo of a 1984 White Face has been read to confirm them (JHS's gut
  shot shows the pot side). The RAT2's (`--parts rat2`) and the China-made RAT2's (`--parts
  china`: C11 2.7 nF) are in the tools only.
- **Reference units are RAT2s.** Every fitted reference is a RAT2 or a 1990s RAT. The one capture
  of the target unit, a 1984 White Face (`imposteringtheband-1984-whiteface`, uncalibrated, one
  setting read from tick counts), is held out. With the RAT2 fit's pot laws the real pedal had
  more highs than the model (heard first on scenes 26-27): +2.1 / +3.5 / +5.4 dB at 2-4, 4-8 and
  8-16 kHz on a guitar DI. With the 1979 pot's 10.1 % law it was +1.1 / +1.6 / +2.5; with the
  FILTER law fitted to it (6 % at mid, 2026-11-19) +0.9 / +0.8 / +0.8, and within 0.2 dB on the
  TB-303 line. The fit could stand for the FILTER pot's law, its total resistance, C11 (2.7 nF does
  much the same) or the owner's knob reading: one capture can't tell them apart.
- **Pot laws.** DISTORTION and VOLUME are the one measured vintage RAT pot's (a 1979 big box's
  DISTORTION, 10.1 % at mid); FILTER is fitted to the White Face capture (6 %). No 1984 pot has
  been measured. Later units are darker at the same FILTER: against the 6 % law the 2010 China-made
  RAT2 (scenes 14-15) is 3.6-5.6 dB darker above 4 kHz, outmoded's RAT2 up to 5-6 dB darker at
  DISTORTION noon (scenes 19, 24); the Aalto RAT mostly fits better (6 of 13 scenes better, 3
  slightly worse).
- **DISTORTION-dependent top end.** On outmoded's RAT2 the model's error in the highs changes sign
  with DISTORTION (too dark at 7:30-9:00, too bright at noon) whatever the FILTER law: something
  gain-dependent is off (seen 2026-11-19, not looked into). The 1984 values make the model 0.2-1.1 dB louder at the same VOLUME (median 1.0 dB: no
  R15 loading the buffer) and move band levels by up to 1 dB on the guitar renders (85 golden
  cases, 2026-11-19); the fits were made with the RAT2's values.
- **Panel.** Measured from photos of 1984 units (`docs/panel.md`): the print to about 0.05 mm
  relative, the whole to +-1.2 % (two scale sources 2.4 % apart: the pot pitch against CTS 450
  bodies, and the leaflet's 4 1/16 in); knob, footswitch and front-face sizes to 0.5-2 mm. No unit
  measured with a rule.
- **Not emulated:** the footswitch click, supply sag through R1 (the macromodel draws no current),
  pickup loading (the source is 1 kΩ), the other RAT versions (`circuit.md` §6).
- **Unread:** DIYstompboxes topic 80809; Werner's 2016 and Holmes' 2019 theses are filed, not
  reviewed (`research.md` §11).

### Reference data

| Unit | Where | What it has | Known settings |
|---|---|---|---|
| outmoded RAT2 (op amp unknown, clips as an OP07) | `private/rat/reference/nam/outmoded-rat2-11dbu` | 20 NAM captures | clock positions; +11.5 dBu |
| stomptones RAT2 | `.../nam/stomptones-rat2` | 4 captures | clock positions; level fitted (about 0 dBu) |
| romanrecords 1990s RAT, jakkwiat RAT2 | `.../nam/romanrecords-rat-90s`, `jakkwiat-rat2` | 7 x 7 grids, held out | grid index only; +3.98 dBu / uncalibrated |
| pong 1996 USA RAT2 (LM308), rbrt ~1994 RAT (LM308 by era) | `.../nam/pong-rat2-1996-usa`, `rbrt-rat-1990s` | 3 and 2 captures | stated; uncalibrated |
| autoguitarrig RAT2, octaviofotografia RAT | `.../nam/` | 4 and 1 captures | stated; uncalibrated |
| 12 more packs | `.../nam/` | excluded: large box, Turbo, modded, unknown knobs (`nam/index.md`) | |
| Aalto's RAT | `private/rat/reference/aalto` | 97 clips, dry and wet guitar | knobs in the names; level scanned |
| EGFxSet's 2010 China-made RAT2 | `private/rat/reference/egfx` | 690 notes, dry and wet, not aligned | DIST 1.0, FILTER 0.5, VOLUME 1.0; normalized |
| A 1990 LM308 RAT2 (jazzcaster) | `private/rat/reference/measurements` | scope shots and FFTs, digitised | stated, -20 dBu in |
| Bench parts (KGB, jazzcaster, PedalPCB) | `research.md` §11 | LM308/OP07 slew and GBW, DC points of real units | |

No measurement of a real pedal's internal nodes; no real hardware here.

## Budget

- **CPU:** `ratgolden --bench`'s "guitar, defaults" (Captured, LM308, knobs 5/4/5) under 4.0 % of a
  core per channel at 48 kHz on a quiet machine (proposed, to be confirmed): the last measurement
  plus about 10 %. Last measured: 3.6 % (2026-11-19, after the cpu round; 4.4 % before it); before: 7.6 % (2026-10-09,
  f70943c); knob automation 9.9 %; a stereo instance 7.2 % on a mono source, 11 % with different
  sides.
- **Check set:** there are `rat2plugintest` (sleep, stress, long run, rates, mono, stereo, state,
  view), `tests/host.cpp`, `ratgolden` (`--write/--check/--pairs/--bench`; 85 golden renders in
  `build/rat-golden-2`, 2026-11-19), `ratcorners` (the extremes: every solve converged, output
  bounded; 2026-11-16), the SPICE suite (`tools/compare/spice_check.py`,
  `spice_vendor.py`) and the unit comparisons (`compare.py`, `compare_scope.py`,
  `compare_aalto.py`, `compare_egfx.py`, `stability.py`) and the A/B scenes
  (`private/rat/reference/scenes/make_scenes.py`: 27 Lemus AB scenes, real pedals and NAM captures
  beside ours and the clean input, pre-rendered; `ab.py` still writes two-signal A/B WAVs). auval
  (`pr-dist-r`, since 2026-11-19). Missing: public `host/` scenes (all the material is private);
  the bench's worst block, time asleep and instruction counts. No service-manual checks:
  the pedal has no service manual.

## Plan, in order

1. The DISTORTION-dependent top end (Known inaccuracies): the highs' error against outmoded's
   RAT2 flips sign between low DISTORTION and noon (accuracy).
2. `docs/model.md` brought up to the split calibrations and the 1984 values (tooling).
3. The bench's worst block, time asleep and instructions; per-pluck levels in `compare.py`
   (tooling).
4. LM308: fit the constrained alternative (slew and swing inside the bench ranges), compare both on
   held-out data, write down which ships (accuracy).

Waits on data: the asymmetry's source (a scope trace of a real pedal's pin 6 and clip node); the
LM308 trims (a calibrated LM308 capture); DISTORTION near zero (more units with stated low
settings, or a measured RAT2 DIST pot).

## Tried and dropped

- 2026-11-19, cpu: skipping the state matrices' structural zeros (90 of A's 144 entries; the
  circuit's sections meet only through the nonlinear parts), per row as index lists (bit-exact,
  5 % slower) and per pair of rows as column spans (bit-exact, no faster): the dense two-wide
  loops keep more sums in flight. A quadratic predictor for Newton's start: 1.94 to 1.62
  iterations at 1e-4, but slower overall (the extra history). 4x oversampling at 44.1/48 kHz
  instead of 8x: 3.6 to 2.1 %, band levels within 0.2 dB, but aliasing 15-20 dB higher (a 2.5 kHz
  tone at full DISTORTION −58 to −40 dB, 5 kHz −45 to −30 dB): not taken.

- By 2026-10-09: the datasheet op amp with only the 2N5458's bias free to make the asymmetry. It
  needed IDSS 0.78 mA, under the part's 2 mA minimum, and scored worse everywhere (5.06 dB LSD on
  outmoded, 7.09 dB held out) (`model.md`).
- By 2026-10-09: the Aalto pedal's DISTORTION taper (0.04 at mid): 6.6 to 3.5 dB LSD on its 13 low
  clips, not used as the captured RAT2s don't follow it (`model.md`).

## Declined

- 2026-11-19: the OP07 and the China-made RAT2's parts as settings; the 1984 RAT had the LM308
  only. Both stay in the tools.

## Done

- 2026-11-19, cpu round: Newton stops at a 3e-5 V step for the op amp and diodes and 1e-4 V
  for the JFET (was 1e-7 V for both): it converges quadratically, so the step before already
  had the solution within about 1e-9 V. 2.38 to 1.94 iterations a sample; per channel at
  48 kHz, back to back with the old build, guitar 5.0 to 4.1 % (−18 %), full distortion 5.95 to
  4.65 % (−22 %), knob automation 6.6 to 5.7 % (−14 %); 3.6 % on a quiet machine. Output within
  −129 dB of the old (`ratgolden --check`, worst case; the bar is −120 dB); `--pairs` 85 of 85,
  `ratcorners` 0 unconverged, SPICE check unchanged, plugin and host tests pass. New goldens
  `build/rat-golden-6`.
- 2026-11-19, feature: INPUT back in the bar (decided then): a gain in front of the pedal, −12 to
  +24 dB, gliding over 10 ms, not made up at the output, skipped by the bypass; 0 dB is the old
  +4 dBu. The goldens are bit-identical at 0 dB. Five parameters.
- 2026-11-19, feature: no LEVEL or CAL in the bar (decided then): the plugin is the Captured
  calibration at +4 dBu, and drive is the track's level. Four parameters: the knobs and the
  footswitch. The tools keep `--cal` and `--level`.
- 2026-11-19, accuracy: the FILTER law fitted to the 1984 White Face capture, 6 % at mid (was the
  1979 pot's 10.1 %), heard first: the real one was still a touch brighter on the guitar. Scenes:
  White Face guitar 1-16 kHz band error 1.6 to 0.7 dB rms, TB-303 0.9 to 0.2; Aalto 6 better, 3
  slightly worse, 4 the same; EGFxSet's China RAT2 1.6-1.8 to 3.3-3.6 dB (ours now brighter); outmoded's
  RAT2 4 better, 5 worse (the noon-DISTORTION ones). Every scene has "lemus 6%" and "lemus 10%"
  beside the real one for the listen. Goldens `build/rat-golden-4`; `ratcorners`, plugin and host
  tests pass.
- 2026-11-19, accuracy: the pots' laws are the measured 1979 RAT pot's, 10.1 % at mid, for
  DISTORTION, FILTER and VOLUME (were the RAT2 fit's 18.5, 14.6, 28.8 %), heard first: the real
  1984 White Face had more highs than ours on scenes 26-27. Against it: LSD 8.25 to 6.50 dB, ESR
  -9.1 to -13.3 dB, the 8-16 kHz gap +5.4 to +2.5 dB (guitar). The Aalto RAT's level error across
  its VOLUME settings went from an 8.7 dB spread to 2 dB (LSD 7.34 to 7.29); the 1990 RAT2's scope
  shots: harmonic error 5.9 to 4.4 dB, pos/neg error 0.015 to 0.008. The RAT2 captures as a set:
  LSD 7.43 to 8.54 dB. At noon on all three knobs the pedal is about 8 dB quieter, with less gain,
  as a 10 % pot is at half turn. Goldens `build/rat-golden-3`; `ratcorners` passes; CPU unchanged
  (4.5 %); scenes re-rendered.
- 2026-11-19, feature: the 1984 White Face's panel, measured from photos of 1984 units
  (`tools/panel/measure_panel.py`, `docs/panel.md`): a fitted camera pose (EXIF focal length; tilt
  2.46 degrees, residual 0.045 mm), the scale from the pot pitch and the leaflet (+-1.2 %), heights
  from the knobs' and footswitch's lean. The view draws it in code: no LED, the legends in Nimbus
  Sans Bold fitted to their ink boxes, the white block plain unless the user's artwork fills its
  places. Artwork places `model` (RAT), `maker` (Pro Co) and `front` (Pro Co Sound Inc), the
  processor an `ArtworkUser`; the RAT2's traced legends and LED window are out of the code. The bar,
  key list and tooltips are `host/PanelFrame.h`'s. `pr-dist-r` (Prdi) builds, auval passes;
  `kModelVersion` 1 in the state (bab8054). The A/B scenes re-rendered with the 1984 values, with
  the White Face capture as scenes 26-27; that capture's index entry corrected (it is a small-box
  White Face, not a big box). The host test checks the places match the panel's boxes.
- 2026-11-19, feature: retargeted from the RAT2 (still sold new) to the original RAT, 1984 White
  Face small box. The machine is `rat` (was `rat2`; no saved session used the old id), the plugin
  to be `pr-dist-r` (code Prdi). Default parts: no R12, R4 1M, C13 1 µF, no R15 (`Parts{}`); the
  RAT2's are `rat2Parts()`, `--parts rat2`. The OP07 switch is gone from the plugin. Against the
  RAT2 values on the 85 golden cases: 0.2-1.1 dB louder (median 1.0), band levels within 1 dB on
  guitar, up to 2.9 dB in sparse bands of the synth line; new goldens `build/rat-golden-2`.
  `ratcorners` passes (0 unconverged), `ratplugintest` and the host test pass; bench 4.5 % of a
  core per channel at 48 kHz (guitar, defaults).
- 2026-11-18, tooling: A/B scenes for the ship gate's listen, `private/rat/reference/scenes/`
  (`make_scenes.py`, README): `aalto/` 13 of Aalto's real-RAT clips across DISTORTION x FILTER
  (+9 dBu, the set's best fit with either op amp), `egfx/` EGFxSet's 2010 RAT2 on 8 notes, Bridge
  and Neck (+6 dBu; with the China parts too), `outmoded/` the calibrated NAM captures at 5
  settings on a guitar DI and our TB-303 line (+11.5 dBu). Each: the reference, lemus OP07, lemus
  LM308 (Captured), the clean input; all pre-rendered with ratrender and NAM's renderer. Loaded
  and played through Lemus AB (`--snapshot`, `--selftest`).
- 2026-11-16, tooling: a check for solves that run out of iterations, which every other check
  passes silently (on the SH-101 one turned a self-oscillation into white noise). The pedal counts
  its solves' outcomes (`Pedal::SolverStats`: solves, retries from the last sample's solution,
  unconverged, operating points), and `ratcorners` (`tools/corners`) renders 636 cases of the
  extremes at 44.1, 48, 96 and 192 kHz: every knob at its ends, both calibrations and op amps, the
  China parts, input at full scale and 12 dB over with the input level at -10 and +20 dBu (sweeps,
  squares, DC steps, impulses, noise, silence), knobs jumping, the footswitch and the level
  switching; it fails on any unconverged solve or operating point, or output past 3 V (17 s).
  None found: 0 unconverged in 1.17 billion solves, 271 retried (all at 192 kHz, 2x, with the
  knobs jumping at +12 dBFS), output peak 1.12 V. With the op amp block's iterations cut to 3 it
  fails 588 of 636 cases at 48 kHz. The sound is unchanged: `ratgolden --check` 85 of 85
  bit-identical against renders from before, `--pairs` 85 of 85, `rat2plugintest` passes.
- 2026-10-10: Space toggles the footswitch only in the standalone app (7902076); "Lemus" in the
  docs (99f2a6b).
- 2026-10-09, cpu round: per channel at 48 kHz 10.7 to 7.6 % (guitar), 14.7 to 9.9 % (knob
  automation); a stereo instance on a mono source 22 to 7.2 %. A mono source solved once, the two
  sides interleaved, Newton's 3 x 3 and 2 x 2 in closed form (the one change not bit-exact: under
  -239 dB on 85 golden renders), sparse rebuilds on knob moves. Captures, scope shots and ngspice
  results unchanged (f70943c).
- 2026-10-09: sleep: input under -200 dBFS, output under -120 dB for 100 ms, exact zeros; tail
  measured 0.84 s to -90 dB, reported 1 s (was none) (16e26fa).
- 2026-10-09, accuracy round: separate LM308 and OP07 calibrations, the OP07's trims fitted to 24
  captures of two RAT2s (`fit_nam.py`), the LM308's to the 1990 RAT2's scope shots and two LM308
  captures (`fit_lm308.py`); EGFxSet and scope-shot comparisons (2a21467).
- 2026-10-09: no reference artwork pack; the mark tracers moved to the private folder (a26934e).
- 2026-10-08: the hardware view is the only editor (358e88d).
- 2026-10-07: the port into Lemus FX (6a5b11d): the whole circuit one DK netlist at 8x with
  Newton; against ngspice -46 to -87 dB, the small-signal response within 0.01 dB; Datasheet and
  Captured calibrations.
