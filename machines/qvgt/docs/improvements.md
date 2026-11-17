# QuadraVerb GT: what we know is wrong, and the plan

What the model gets wrong or hasn't checked, as the transcription (`docs/circuit.md`,
`docs/firmware.md`, `docs/dasp24.md`) and the comparisons with the real units found it, and what
is planned. "Real" is the GT with firmware v1.03 unless it says otherwise.

## Known inaccuracies

### Sound

- **Nothing is checked against a unit's audio yet.** The analog stages match their schematic in
  ngspice (`tools/spice/suite.py`, 15 cases, worst 0.09 % RMS; `tools/spice/tapcheck.py` on the
  bench's own signals, circuit.md "Integration"); the firmware runs as it is; the
  DSP's arithmetic is the decode made on the original QuadraVerb (24-bit accumulator, 20-bit
  memory, truncation). The recordings found so far are guitars through amps, filmed, with unknown
  input and mostly unknown settings: no per-event comparison of the effects is possible from them
  (see Reference data). Waits on data.
- **The hiss is from the data sheets, not a unit**: each TL084's 18 nV/sqrt(Hz) (white: no 1/f
  noise), the SSM2120 VCAs' (its Figure 1b), the CEM3381's and the output stages'. Idle at OUTPUT
  5: about -79 to -91 dBu by program (the gate open or shut, the gains). No recording has a
  measurable idle floor (amps, cameras); a unit's output with nothing in would settle it.
- **While asleep with HISS on**, the hiss is a 10-pole noise filter fitted to the last 0.25 s of
  idle output: within about 0.6 dB of it above 200 Hz, 3 dB under it below.
- **The preamp at 167 kHz against ngspice** (`tools/spice/tapcheck.py`, the bench's plucks and
  strums): the shaper's output 24-28 dB under the signal below 20 kHz, the input, overdrive and
  gate circuits 40-77 dB under; renders' top octaves (8-24 kHz) on the distorted programs 0.2-0.6
  dB under the same at 16x (at the earlier 333 kHz: the shaper 31-44 dB, 0.05-0.14 dB). Chosen by
  ear for a third less CPU (Done, CPU round 3; docs/circuit.md, "The rate").
- **The D/A's de-emphasis** is the sample-and-hold's leaky charge (R154 750, C59/C60 2200p); its
  window (32 of the DSP's clocks) isn't on the schematic: taken as the one that undoes the
  pre-emphasis (-0.58 to +0.03 dB to 18 kHz). A unit's sweep would settle it.
- **The 20 kHz filter module (7-20-0001)** has no parts on the sheet: the decimator stands in for
  it (flat to 18.75 kHz, 90 dB down from 22.9 kHz), its passband taken as the terminations' ratio.
- **The OUTPUT pot's taper** is a standard audio law (15 % at mid rotation); the INPUT pot (10KB)
  is taken as linear.
- **The signal display's thresholds** come out of the rev B ladder (R146 91K, R148 180K, R149
  390K, R150 750K to 5 V) at -24, -14, -6.6 and 0 dB under the clip LED, where the panel prints
  -18, -12, -6 and CLIP. The firmware's audio trigger, which the manual ties to the -18 dB LED,
  fires 19 dB under 5 V: the print and the firmware fit the earlier ladder (about 180K at R146);
  rev B ("R146 TO 91K") moved the first two LEDs. Modelled as drawn; a rev B unit's LEDs against a
  known level would settle which units do which.
- **The ring modulator shifts by 0.71 of the displayed SPECTRUM SHIFT** (100 Hz gives 71 Hz): its
  CPU oscillator's tick runs at 1,960 a second where the firmware's frequency word assumes about
  2,690. Every sign is that a unit does the same (`firmware.md`, "Timer 0"); waits on a recording.
  The shifter itself checks out: a 1 kHz sine moves cleanly, the other sideband 31 dB down.

### Behaviour and features

- **Not emulated:** the keys' force as a continuous amount (a press is light, or hard with Shift);
  the effects loop's jacks (the send is computed, the return is as with nothing plugged in: the
  loop stays out, as on the unit); the level detector's thresholds (gated effects, sampling triggers) are unchecked.
- **The rear panel** is measured from a listing's whole-rear photo, its width taken as 445 mm
  (no photo shows it against a known length): +-5 mm. Its BYPASS and ADVANCE footswitch jacks
  work (a click is a press).

### Reference data

| Unit | Where | What it has | Known settings |
|---|---|---|---|
| 10 owners' units (YouTube) | `private/qvgt/reference/recordings/youtube` | guitar through amps, filmed; demos of the factory programs | LCD in view in some: 6t48dFOvLQA steps through programs 00-09 of the v1.00 factory set |
| A repaired unit (YouTube 0B6v7kb1h_Y) | checked 2026-11-11, not kept | board views (1080p), a keyboard dry and through ROOM 1 / PLATE 1, camera mic | reverb type only; no test signals; the 20 kHz module a sealed can with three trimmers; R146's bands unreadable |
| 6t48dFOvLQA's programs 00-09 (by ear) | `private/qvgt/reference/scenes` | 12 s of each beside a CC0 direct-recorded guitar through the model on the v1.00 EPROM | listened 2026-11-11: hard to tell apart, nothing sounded off; different guitar, playing and recording chain, so character only |
| One unit's panel (listing photos) | `docs/panel.json` (source) | front, sections, rear | n/a |

First comparison, 2026-11-11: 6t48dFOvLQA's unit shows the v1.00/v1.01 factory set (00
GUITARBYALESIS, 02 SOLID SOLO ... 09 MULTITAP; 01 edited), which the model with the v1.00 EPROM
reproduces name for name, and the LCD's layout matches. Its MULTITAP section (560-717 s) was
measured for echo structure against the model's taps (every 241.8 ms, panned left to right over six
taps, the pattern repeating every 1.46 s): the recording's onset autocorrelation shows nothing
above its background at those lags (continuous playing, camera audio, near-mono). Inconclusive.

## Budget

- **CPU:** `qvgtbench`'s `p30-sweep-48k` (a sine sweep through program 30, the busiest) under
  4,000 Mins/s (`tools/bench/mins.sh`) and 17 % of a core at 48 kHz, HISS on. Last measured
  2026-11-16: 3,618 Mins/s; on 2026-11-12 15 % of a core, its worst 512-sample block 52 %
  (timings swing with the machine's load; instructions don't). The others: p00-pluck 2,776,
  p05-pluck 2,160, p00-strum-hot 3,113, p12-strum-44k 2,703, bypass-pluck 2,783, idle 1,176
  (asleep after 1.6 s, the hiss going on).
- **Check set baseline:** 2026-11-12, golden renders `build/qvgt-golden-6` (7 workloads, HISS on,
  bit-exact on re-render); service checks 9/9; SPICE suite 15/15 at 167 kHz (worst 0.31 % RMS,
  1.31 % peak); `tools/spice/tapcheck.py` (p00-pluck: the shaper 27.7 dB under, the rest 40-77);
  `qvgtplugintest` all; `qvgtdspcheck` all; `lemustest_qvgt`, `lemustest_fx` all; auval passes.
  2026-11-16: all of it again but tapcheck and auval, the same; `qvgtcorners` 0 of 1,106 cases
  failing at each rate.
- **Check set:** `qvgtcorners` (the extremes: unconverged solves, noise from a pure tone, output
  bounds; about a minute), `qvgtbench check` against the newest golden renders, `qvgtplugintest`,
  `qvgtdspcheck`, `tools/service/check.py`, `tools/spice/suite.py` and `tapcheck.py`, the host
  tests (`lemustest_qvgt`, `lemustest_fx`), auval.

## Plan, in order

1. **The preamp's cost, what's left** (cpu): the shaper's clipper is about half the preamp's
   time (at 4x it solves by Newton about one step in four and relinearises every third, on
   p00-pluck and p30-sweep alike);
   the other eight circuits share the rest, then the resamplers and decimators, the 80C31 and the
   DSP. Looser solver tolerances barely help (measured 2026-11-12, every circuit: linear mode's
   1e-4 to 1e-3 V, 1-3 %; with Newton's 1e-5 to 1e-4 V too, 1-6 %), not worth a sound change;
   what's left is the solver's own speed (bit-exact, a big job).
2. The v1.00 firmware (the earlier factory set, as in the demo video): it runs, but only v1.03 is
   offered for now (decided 2026-11-11); later if wanted.

Waits on data: the ring modulator's real shift (a unit at a known SPECTRUM SHIFT); the hiss's level (a unit's output with nothing in); per-event sound comparisons (a DI and the unit's output with a known program, or
isolated notes through a delay or reverb program with the LCD in view); the de-emphasis window and
the 20 kHz module (a sweep of a unit); the signal display's thresholds (a unit's LEDs against a
known level).

## Tried and dropped

- 2026-11-11: the TL084s' slew limit in the preamp's netlists: the solver's Newton failed wherever
  a stage clipped (28 iterations a step, 0.03 x real time); without it a clipping stage moves 80-94
  dB under the signal against ngspice with it. Left out.
- 2026-11-11, cpu round 2: the presence, bright and tone stack partitions as one (with the
  cabinet's): 10 % less, but presence and bright leave linear mode now and then, and joined they
  relinearise together: program 5's error in linear mode against exact Newton rose from -90 to
  -76 dB RMS. Only the cabinet's always-linear partitions were joined.

- 2026-11-12, cpu round 3: the stages after the clipper at a lower rate: they are about 7 % of the
  total (a partition timing of p00-pluck), so about 4 % to save. And the clipper and the overdrive
  at 8x inside a 4x preamp (Chain::setOver): 25 % less than 8x, but its renders no closer to a 16x
  render than plain 4x (the input's linear interpolation droops as much as the coarser step).

## Declined

- 2026-11-11: the shaper at 2x (666 kHz, when the preamp ran at 333 kHz): within 0.02 dB of ngspice per octave where it is within
  0.1 dB at 1x, for 32 % more CPU (p30-sweep about 6,650 Mins/s, 28 % of a core). The preamp's
  Chain can run it (setOver; QVGT_OVER=shaper:2 in the tools).

## Done

- 2026-11-17, fix: the output went past the output stages' rails. Their swing (10.5 V, 6.048 of
  full scale) was clamped only at the DSP's rate, and the resampler up to the host's rate put
  peaks between those samples, up to 2.22 times it: with full-scale content near 20 kHz (programs
  30, 32 and 34, a sweep at +12 dBFS, INPUT and OUTPUT at 10) the output reached 12.5-12.8 of full
  scale (12.9 the most in any case, +22 dBFS), where a unit's output stages clip after the D/A's
  reconstruction. Decided 2026-11-17: the same clamp again at the host's rate (`Unit::process`).
  Those cases now peak at the rail (6.048);
  every sample that was inside it is unchanged, and the golden renders `build/qvgt-golden-6` are
  bit-exact. `qvgtcorners`' bound for the unit is the rail (6.05, was 13.5), and
  `qvgtplugintest`'s random-settings and 60 s checks too (6.05, was 6.5, which its message called
  +16 dBFS; the swing is +15.6). `qvgtcorners` 0 of 1,106 cases failing at each rate, the unit's
  peak 6.048 at both; `qvgtplugintest` and `lemustest_fx` pass.

- 2026-11-16, check: every iterative solve counts the ones that don't converge, and `qvgtcorners`
  (tools/corners) fails on any. The solves: the preamp's circuits (the 909's solver: Newton, 40
  iterations, a failing step redone in 4, 16 and 64 steps, then with the comparators softened;
  one that failed even then was taken as far as it got, counted only in statistics that the
  bench's QVGT_STATS and `qvgtpreamprun` print), their DC solves at prepare (a failure kept the
  state, unreported), the compressor's D11 node (Newton, 30 iterations, steps of 0.1 V at most)
  and the dry VCA's control voltage (Newton inside Newton, 60 iterations each, once per change of
  latch 0600h). Linear mode's window widths (circuit.cpp, `junction`) iterate too, but in closed
  form, within their caps for every input (at worst 1.6e-5 of their residual left): not solves
  of the state, not counted. All of it sums into `SolverStats` (core/solver_stats.h), through
  Unit and the processor (`solverStats()`). Two silent failures turned up:
  - The dry VCA's control voltage at code 1 (D0 alone: programs 07, 34, 46, 70 and 86): the
    diode's inner Newton needs 65 iterations from its start and stopped at 60. The outer Newton
    reached the same voltage all the same (bit-identical). Its cap is 100.
  - U1's input followers with white noise at +24 dBFS at the jacks (40-46 V on their inputs after
    the input resampler): 1 or 2 steps in 1.5 million where Newton circles between the two
    saturated sides of an op-amp's output at every step size (a scan of 17,616 preamp cases: 63
    steps in 56 cases, every one of them that noise). A failed step left a difference peaking
    14-25 dB under full scale in noise at -9 to +11 dBFS RMS: masked. The 64th step now has a last
    resort, damped Newton (its step halved while the residual doesn't fall, up to 200
    iterations), which converges them within a few. It is reached only where everything before it
    failed, so every other sample is as it was.
  `qvgtcorners`, 1,106 cases at each of 44.1 and 48 kHz: the preamp alone with its latches at the
  PRE-AMP pages' extremes, fed through the unit's input resampler (every distortion, tone, bass
  and cabinet at compression and overdrive 7; every compression and overdrive; sweeps, squares, DC
  steps, sines at 20 Hz, 1 kHz and 20 kHz, noise and silence at -20 to +24 dBFS, the INPUT pot at
  both ends, the gate off and at 16; 4 s of noise at +24 dBFS), 948 cases; and the processor with
  every factory program (a 1 kHz sine at 0 dBFS, then a sweep at +12 dBFS, INPUT and OUTPUT at 10)
  and the four with overdrive 7 and distortion on (30, 46, 62, 63) with every signal, 158. 1.1
  billion solves a rate, 2.8-3.1 % retried in shorter steps, none unconverged; 65-80 s. It also
  fails on broadband noise from a 1 kHz sine (the factory programs' worst median flatness 0.004,
  limit 0.1) and on output out of bounds. Checked by crippling: the dry VCA's cap back at 60 fails
  the five programs at each rate; the damped resort off fails the four programs' noise at +24
  dBFS at 44.1 kHz and both 4 s noise cases at 48 kHz; Newton's cap at 3 with it off fails 311 of
  the 411 distortion 8 cases at 48 kHz. Golden renders `build/qvgt-golden-6` bit-exact; Mins/s +0.0 to +0.3 % (p30-sweep 3,607 to
  3,618, p00-pluck 2,771 to 2,776, idle 1,176 unchanged); `qvgtplugintest`, `qvgtdspcheck`, the
  service checks 9/9, the SPICE suite 15/15 (unchanged), `lemustest_qvgt` and `lemustest_fx` pass.
  It also found inter-sample peaks at the output (fixed 2026-11-17, above).

- 2026-11-12, fix: program 10 (PSEUDO WAH, the factory set's one program with EFFECT LOOP IN) was
  silent. J8's switch had been read as grounding the return's tip; it grounds U3's select, so with
  nothing in the return the loop is out whatever the program says, as the owner's manual has it
  ("bypassed if there are no jacks inserted"). Every other program is unchanged (golden renders
  bit-exact).

- 2026-11-12, release: `al-fx-quadgt` 0.1.0 beta, with a standalone app. The plugin is named
  al-fx-quadgt again (it was al-fx-qvgt from 2026-11-11): QUADRAVERB's only registration (Germany,
  1992) has expired, and no register lists QUAD GT, so the README's rule allows a short form of
  the name where no live trademark covers it. The plugin code (Aqvg) and the machine's id (qvgt)
  are unchanged.

- 2026-11-12, cpu round 3: the preamp at 4x the DSP's rate (166,667 Hz) where it was 8x, decided
  2026-11-12 after a blind A/B of 8x, 6x and 4x on five programs and a 303 line
  (`private/qvgt/reference/rate-ab`): no difference heard. Measured against renders at 16x (hiss
  off): 8x 44-56 dB under, 6x 40-52, 4x 35-45; below 4 kHz all within 0.02 dB; the top octaves of
  the distorted programs 0.2-0.6 dB low at 4x (0.05-0.14 at 8x); bypass within 0.02 dB. Against
  ngspice (tapcheck, program 0's pluck): the shaper 39.5 to 27.7 dB under, the overdrive 48.6 to
  39.8, the gate 72.0 to 62.6, the input unchanged (60). The trapezoidal rule's choice per circuit
  still holds at 4x. Mins/s before and after: p30-sweep 5,395 to 3,608, p00-pluck 4,195 to 2,771,
  p05-pluck 3,508 to 2,159, p00-strum-hot 4,594 to 3,106, p12-strum-44k 4,152 to 2,698,
  bypass-pluck 4,186 to 2,780, idle 1,690 to 1,175 (30-38 %); p30-sweep 23 to 15 % of a core, its
  worst 512-sample block 76 to 52 %. The decimators 241 taps (60 a DSP sample, as before); latency
  unchanged. SPICE suite at 167 kHz: worst 0.31 % RMS, 1.31 % peak (was 0.09, 0.48), all pass.
  New golden renders `build/qvgt-golden-6`; the whole check set passes. Tools: QVGT_PREAMP_OVER
  runs the unit at another rate; `tapcheck.py --over` and `suite.py --over` (both default to 4).

- 2026-11-11, accuracy round 3: the shaper's partitions against the netlist solved whole and
  ngspice on the bench's own signals (new `tools/spice/tapcheck.py`: every preamp circuit, its
  inputs tapped at each step, against ngspice at an eighth of the step). The split is as close to
  ngspice as the whole solve (on program 0's pluck: 36.0 against 34.2 dB under, below 20 kHz);
  the gap the bench showed was the unit's sensitivity (an input 1e-7 larger moves the output -86
  dB peak) and where each halves its steps. The real error was the op-amps' poles by backward
  Euler in the shaper, where U6A swings across its diodes and the tone stack reaches its rails:
  now by the trapezoidal rule there (the shaper 31-41 to 31-44 dB under, its top octave from
  0.3-0.4 dB low to within 0.1 dB; the other circuits measured better with backward Euler, kept).
  SPICE suite worst RMS 0.22 to 0.09 %. Renders moved 60-72 dB under their output (levels +0.004
  to +0.016 dB; bypass and idle unchanged). No CPU cost (p30-sweep 5,490 to 5,395 Mins/s). New
  golden renders `build/qvgt-golden-5`. Tools: QVGT_TAP, QVGT_PREROLL, QVGT_WHOLE, QVGT_POLETRAP,
  QVGT_OVER in `qvgtbench`; the preamp's Chain can oversample its partitions in lockstep.

- 2026-11-11, cpu round 2: bit-exact first: each SSM2120 VCA's gain worked out once a step, and
  the hiss written straight into the circuits' input slots (2.5 %). Then, accepted 2026-11-11: the
  shaper's cabinet, bass and sum partitions as one (they never leave linear mode; 6 %; rounding
  order only: -99 to -121 dB RMS against the earlier renders, the same error against exact Newton
  as before), and the hiss from one 64-bit draw a sample, its four 16-bit quarters summed, where
  it took four 32-bit draws (3.4 %; the same level, -99.9 to -100.0 dBFS idle, and spectrum).
  Mins/s before and after: p30-sweep 6,085 to 5,490, p00-pluck 4,750 to 4,208, p05-pluck 4,023
  to 3,480, p00-strum-hot 5,158 to 4,617, p12-strum-44k 4,676 to 4,130, bypass-pluck 4,708 to
  4,162, idle 1,887 to 1,679 (11-13 %); p30-sweep about 26.5 to 23 % of a core. Against
  `build/qvgt-golden-3` (HISS on, so a new hiss sequence): -55 to -97 dB RMS, the largest where a
  gate closes on the hiss at the end of the sweep (p30, 4.0-4.5 s). New golden renders
  `build/qvgt-golden-4`; the whole check set passes. `qvgtbench` renders without the hiss
  (QVGT_HISS=0), one workload, and prints the solver's linear-mode use (QVGT_STATS).

- 2026-11-11, panel: the rear measured (docs/panel.json, "rear"): the jacks' places and sizes,
  their legends, MIDI/OUTPUT/EFFECTS/INPUT brackets, the FCC notice, the maker's mark from the
  user's artwork; seen from behind, as the unit is. OUTPUT and INPUT run L then R (the earlier
  sketch had them reversed).
- 2026-11-11, panel: the LCD drawn dot for dot from the HD44780's character ROM
  (`hd44780_a00.bin`, MAME's, as the DX7 has it), now a required ROM beside the firmware (decided
  2026-11-11: required, no font fallback).
- 2026-11-11, accuracy round 2: the GT's own DSP configurations checked through the firmware
  (`tools/dspcheck`, now in the check set). Ring modulator: a clean frequency shifter (the other
  sideband 31 dB down), its shift 0.71 of the display because its CPU oscillator ticks at 1,960
  where the frequency word assumes 2,690; the chip, the cycle counts and both firmware versions
  agree, so taken as the unit's own (`firmware.md`, "Timer 0"). Resonators: in tune within the
  integer delays (to 31 cents at +24); DECAY's short ring is the firmware's own mapping (a loop
  gain of 0.92 at 90, 0.96 at 99). Sampling: records, loops the whole 1.573 s memory, transposes
  from MIDI. The audio trigger fires 19 dB under the clip LED's 5 V, the printed -18 dB, which
  points at rev B's ladder change (circuit.md). No model change; no CPU change.
- 2026-11-11, accuracy round 1: the parts' own noise and the A/D's idle, behind a HISS setting on
  the bar (on by default). A noise source at each of the 19 op-amps' + inputs (TL084, 18
  nV/sqrt(Hz)), the SSM2120 VCAs' noise rising 0.475 dB a dB of gain (its Figure 1b: -80 dBV at
  unity into 36K), the CEM3381's (10 pA/sqrt(Hz)), the output stages'; the A/D's offset servo
  (LSTMSB, R140, C64, R137 against R136), which keeps it toggling about zero. The self-diagnostics'
  clip LED test now overflows (service checks 8/9 to 9/9); the gate set off is held open by the
  hiss as on the unit. Idle hiss at OUTPUT 5: -79 to -91 dBu by program. Sleep with HISS: once the
  input has been silent 1.6 s and the output's level is steady, the circuit stops and a noise
  filter fitted to the idle output goes on (within 0.6 dB above 200 Hz); HISS off sleeps to exact
  zeros as before. CPU: +19 % (p30-sweep 5,113 to 6,085 Mins/s, about 25 to 26.5 % of a core);
  idle 2,287 to 1,887 (it sleeps sooner). New golden renders `build/qvgt-golden-3`.
- 2026-11-11, cpu round 1: the series diode strings (overdrive D6-D8 and D9-D10, distortion D5-D4
  and D3-D1) as one diode each (N and RS multiplied: the same DC currents to 6 digits). Mins/s
  before and after: p30-sweep 8,157 to 5,113, p00-strum-hot 6,829 to 4,399, p00-pluck 5,871 to
  4,005, p12-strum-44k 5,482 to 3,921, p05-pluck 4,704 to 3,297, bypass-pluck 5,853 to 4,004,
  idle 2,383 to 2,287; share of a core at 48 kHz about 35 to 25 % (p30-sweep). Not bit-exact: the
  renders moved 47-84 dB under their output (RMS; peaks -40 to -81 dBFS; bypass and idle
  bit-exact), about the solver's own distance from ngspice (-59 to -61 dB, unchanged); accepted
  2026-11-11. SPICE suite 15/15 on the new netlists. New golden renders `build/qvgt-golden-2`.
- 2026-11-11, build: phases 0-5 (firmware, DSP decode, the circuit of both sheets, the measured
  panel and the rear's footswitches, the check set). Commits 745818e (phase 3), 0e89fd6 (phase 4),
  and the phase 5 commit.
