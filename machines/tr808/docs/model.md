# Voice board model and validation

The 808's sound is its voicing: the common trig and accent circuit, a noise generator, six square
oscillators, eleven voice circuits and a mixer, about 1100 parts. Lemus runs them the way the
schematic draws them: each block is a netlist (`core/netlists/*.cir`, transcribed part by part,
`docs/circuit.md`) solved sample by sample by a small circuit simulator (`core/circuit.*`). No voice
is a hand-made DSP approximation; what makes it fast is how the solver runs the circuits, not
changes to them.

## Blocks

`VoiceBoard` (`core/voiceboard.*`) holds the board's signals by name and runs, each sample:

| Block | Netlist | What it is |
|---|---|---|
| Common trig and accent | `accent.cir` | Q31-Q34, VR3: the CPU's 1 ms monostable gates Q32, whose emitter is the COMMON TRIG bus: ~4.3 V, or VR3's wiper (to ~14.2 V) with ACCENT data |
| Narrowed trig | `mtrig.cir` | IC2a on the voicing board: the trig for CB, CY and the hats, ~6.8 V / 13.5 V (it clips) |
| Noise | `noise.cir` | The white noise amplifier and pink filter (IC24; the first generator by params); the noise source itself is Gaussian, scaled by TM4's `noiseRms` (TP4 reads 0.83 of it: SERVICE) |
| Metal source | `metal.cir` | The six HD14584 oscillators' squares (phase accumulators at the calibrated frequencies, 47 % duty, 4.7 V) mixed and band-passed by IC3a/b into `hi` and `lo` |
| Voices | `bd sd lt mt ht rs cp ma cb cy hats` | Each voice from its gate transistors to its jack; `ma` is the maracas' part of the clap circuit, `hats` both hi-hats |
| Mixer | `mix.cir` | IC4 and MASTER VOLUME to the HI output |

The CPU's step pulse starts the 1 ms monostable (`Calibration::monoMs`); the instrument data
lines are 0/5 V inputs. MIDI notes make the same pulse with their voice's line
(`plugin/PluginProcessor.cpp`). The master output is volts / 10 (`kVoltsFullScale`): 0 dBFS is
10 V at the HI output.

## Rates

The board runs at twice the host rate below 80 kHz (88.2 or 96 kHz) and at the host rate above,
then decimates with half-band FIRs (`core/oversampler.*`; 100-110 dB stop band). The trigger edges
are stepped finely: every block a trigger reaches runs its next 8 samples in substeps.
`TR808_FACTOR` forces another factor (tools).

The **metal group** runs at twice the board's rate below 150 kHz (176.4 or 192 kHz): the six
squares, the metal source, the cowbell, cymbal and hats, and the mixer. Their swing VCAs chop at up
to 7 kHz, 14 samples a cycle at 96 kHz, which left the cymbal and hats 1-3 dB low at 12-20 kHz
against a board at 8x; now within 0.5 dB. The mixer runs with them for its own sake: IC4a and IC4b
each roll off at 22 kHz (33k with 220p), which the trapezoidal rule pulls down to 19 kHz at 96 kHz,
1 dB off every voice at 18 kHz (0.2 dB at 192 kHz). The other voices' jacks reach the mixer through
half-band interpolators (flat to 20 kHz), the group's through a delay of the same 15 samples, and
the group's jacks come back to the board's rate through half-bands for the tools and the jack
outputs (`VoiceBoard::voiceOut`). `TR808_GROUP=1` runs it all at the board's rate (tools).

The **snare, clap and maracas** run in the group too (2026-11-04), their white noise (`wn`) brought
up to its rate through a half-band (`wn.up`). At 96 kHz their 16-20 kHz were 0.7-1.4 dB low
against ngspice, and with a noise-like input (random points every 21 µs) the snare's snappy came
out 0.9 dB loud over 30-100 ms and 3.3 dB loud over 100-200 ms at SNAPPY 5; at 192 kHz 0.3 and
0.8 dB. Fitted against Fischer's unit at 96 kHz, that excess had hidden part of the snappy's
early decay (improvements.md, S2).

## The solver

`core/circuit.*`, the nodal DK method (Yeh, Abel and Smith 2010; Holters and Zölzer 2015):

- Modified nodal analysis with trapezoidal capacitor companions, reduced to a state-space system
  whose only implicit part is the nonlinear devices' port voltages, solved by Newton each sample.
- Devices: diodes and bipolar transistors with SPICE's DC equations (Gummel-Poon without charge
  storage; gmin across junctions as SPICE adds it), JFETs (SPICE level 1), the BA662 as an OTA,
  the AN6912 as open-collector comparators, op-amps as single poles (3 MHz GBW) whose outputs are
  linear to 1.5 V under the rails and then clip softly at ±13.5 V. Those with an output limit (the
  resonators, IC2a, IC4) also slew: the current into the pole saturates as a bipolar input pair's
  does, imax tanh(v / (r imax)), at the µPC4558's 1 V/µs (its linear range, 1 V/µs over
  2π · 3 MHz, is 53 mV: a plain pair's 2 V_T). A clipped op-amp's internal
  node stops 2.5 V (in output volts) past the rail, as a real one's stages saturate, so it comes
  back from clipping within a sample rather than unwinding its integrator (which held the
  accented narrowed trig 125 µs too long). Models: `models.cir` (`docs/circuit.md`,
  "Semiconductors").
- Capacitors can carry `da=` (dielectric absorption: Dow's four series R-C branches, 0.01-10 s)
  and `leak=` (a resistor across), as plain R and C elements, as the 909's solver has them. No
  netlist uses them: tried on the clap's C144 and the snare's C51 (Unit spread, Open points).
- The op-amps' internal pole capacitors are integrated by backward Euler, everything else by the
  trapezoidal rule. Their closed loops are in the MHz; the trapezoidal rule maps such a pole near
  z = -1, so an op-amp hit by a step rang at Nyquist (±1-6 V on the narrowed trig for ten
  samples after each edge). Backward Euler damps it; at audio frequencies the difference is
  under the op-amps' own error (their loop gain is hundreds). The 10 pF the loader puts on nodes
  joined only through junctions (a stand-in for the junctions' own capacitance) are integrated
  the same way: through a conducting junction their time constant is nanoseconds, and the
  cowbell's VCA rang at Nyquist on them (an extra Newton iteration a sample, and 0.6 % RMS of the
  cymbal's SPICE agreement).
- **Trigger edges** run in quarter steps for 8 samples, the first 8 quarter steps by backward
  Euler (`Circuit::edge`), as SPICE drops to backward Euler after a breakpoint. The edge's own
  sample takes the inputs at their new values from its start, rather than as a ramp across it (a
  trigger's edge takes well under a microsecond), and runs in sixteenths (`Circuit::jump`): C57
  couples the snare's edge into IC14a's input as a 5 µs spike, faster than the op-amp can follow.
  Slewing on it, IC14a gives the snare's tone a 9.8 dB accent rather than the trig's 10.4 (ngspice
  at 0.1 µs steps: 9.82; the solver 9.79), and 0.5 dB less level; a ramped edge hid the spike, and
  with it the slewing. A capacitor that
  couples an edge into a junction rings with the trapezoidal rule (its companion current flips
  sign each step while the junction clamps the node): the claves' and rim shot's mute timer
  (C112 into Q61) fell to -28 V where SPICE gives -15, so the claves were cut 10 ms early at
  44.1/48 kHz and 24 ms early at 4x; now they mute at 33 ms at every rate, as SPICE does.
- Newton converges when every port's residual is under 10 µV (0.04 % of a junction's current;
  1 µV costs 6 % more CPU and moves no measure here by more than 0.1 dB), an op-amp's limiter port
  counted in output volts (its port is the pole node, 10^5 times smaller than the output it sets): judged in
  port volts, the narrowed trig came out of clipping 25 mV off for 100 µs, which moved the
  accented hats' first 15 ms by -31 dB depending on the tolerance.
- Pots and switches are part of the linear system: moving one rebuilds the matrices (a few
  microseconds).
- A sample whose Newton solve fails is redone in 4, 16 or 64 steps; the next 32 samples then run
  in 4 steps each. If even the smallest step fails (the clap's comparator multivibrator, about
  once a hit), it is retried with the comparators' transitions widened 4-64 times, as the DC
  solve does, rather than left undone.
- **Linear mode.** While every device stays where it is linear to within 10 µV at its ports (a
  junction off, an op-amp inside its knee, a conducting junction within 2 mV of where it was
  linearised), a step is the linearised system's state update, with no Newton. Most of a
  resonator's ring runs this way (the BD's trigger block 99.6 % of its steps, the toms' resonators
  80-90 %). `TR808_NOLINEAR` turns it off; the output is the same to within the tolerance.
- Every netlist exports to an ngspice deck (`Circuit::toSpice`, `tools/circuitrun --spice`), so
  the solver can be checked against SPICE on the same circuit (below).

## Partitions

A voice's netlist is split into partitions by its `.part` lines (`Netlist::partitions`): its
trigger, its resonator, its envelope, its VCA, its output stage. They run in order each sample;
the voltage at a cut is passed on, this sample's if the partition that makes it runs first, else
the last sample's. Small systems factor faster and settle into linear mode separately, which is
where most of the speed comes from. Rules the netlists follow:

- A cut must not pass a spike shorter than a sample: where a trigger's divider feeds a resonator
  through a capacitor (SD, RS), the divider is in both partitions, not across the cut.
- A partition marked `sleep` (the toms' noise, the clap's VCA and OTA, the cymbal's three bands,
  each hi-hat's VCA) stops on its own once its outputs have been within 0.3 mV for a quarter
  window and the voltages it takes from the partitions before it have stopped moving (20 mV
  wakes it). It judges by its jack and what it passes on down the voice, not by what it gives
  back to the partitions before it (an envelope's last trickle, which creeps).

`TR808_FULL` runs every voice as one netlist. Partitioned against whole, a normal and an accented
hit of each voice (48 kHz host): envelope within 0.02-0.14 dB rms and spectrum within 0.0-0.45 dB
for BD, SD, the toms, RS, claves, MA, CB, CY, OH and CH; the clap's envelope 1.4 dB (its noise
bursts, which are chaotic: any numerical change moves them) (`TR808_FULL` against default,
`voicerun`).

## Sleep and power-on

- **A voice sleeps** once its jack has stayed within a floor for 0.1 s and none of its timing
  capacitors (0.1 µF and up) has moved more than 5 mV across that window. Floors: 10 µV; the snare,
  clap and maracas 1 mV and the cymbal and hats 0.1 mV (they leak the noise and the metal squares
  a little at rest); the cowbell 1 mV with 25 mV for its capacitors (its C9 runs down through the
  diode knees for seconds after a hit, under -100 dBFS at the master by 0.7 s). A trigger with its
  data line, or a knob, wakes it.
- **The board sleeps** when nothing runs: the processor skips it and puts out exact zeros. The
  noise generator, the metal source and the CPU keep running while any voice is awake; while the
  board sleeps, only the CPU runs. Idle costs about 2 % of a core (the CPU board and the checks).
- **A new instance** (`VoiceBoard::reset`) is an 808 that has been on a while: every circuit at
  its DC operating point, then the voices fed by the metal squares (CB, CY, hats) run with them
  until they settle (at most 2 s of model time, ~0.1 s of CPU at prepare). The cowbell's swing
  VCAs take their drive through 10 µF (C32, C33) into the transistor bases, which rectify it:
  their steady state sits 0.1 V off the operating point of the squares' first instant, and until
  they get there the VCAs hardly chop and the cowbell is 12 dB down.
- **POWER switched on** (`VoiceBoard::coldStart`): every capacitor discharged, every signal at
  0 V, and the power-on detector's point A at 0 V for 1.5 s (SN p.5: "0V 1-2sec -15V"), which
  turns Q13 on across R74 in the mixer (`mix.cir`): C21 then shorts IC4b's feedback and the
  master is muted (-42 dB against a hit after it) while the board charges. The rails come up at
  once (the supply's own rise, tens of ms, is under the mute). From discharged capacitors the
  cowbell is within 0.2 dB of a warm one by 0.2 s, so after the mute only the first BD differs
  (0.7 dB); the MULTI OUT jacks aren't muted, as on the unit.
- **POWER switched off** fades the output over 5 ms and stops the triggers: the detector turns
  Q13 on as the supply falls, and then the rails collapse; the board keeps its state.

## Calibration

`Calibration` (`core/voiceboard.h`) holds the trims and the part values the schematic leaves
open; its defaults are the service notes' nominal unit (`voicerun --cal nominal=1`).
`unitCalibration()` is the unit Lemus plays: Michael Fischer's TR-808 (serial 103852), fitted to his
recordings at every knob mark (`tools/compare/fit.py`, Nelder-Mead in log space on pitch, tail
decay rate, log-spectral distance and the -20 dB time), with two exceptions taken from the other
real units (Unit spread): the clap's TM3, where Fischer's unit is the odd one of four, and the
step ticks, which the archive.org and Synthmania units let us hear. Fitted values, with the schematic's:

| Part | Schematic | Fitted | What it sets |
|---|---|---|---|
| TM1 / TM2 (CB oscillators) | 540 / 800 Hz | 558.3 / 823.6 Hz | the cowbell's two lines |
| Metal oscillators 3-6 | 522.7, 304.4, 369.6, 205.3 Hz | 486.4, 287.8, 378.6, 198.8 Hz | the cymbal and hats' line spectra (3-7 % off the HD14584's typical thresholds) |
| The six squares' duty | 0.47 | 0.545 | the cowbell's even lines (2f1, 2f2, 4f1, 4f2, 6f1, 2f2 - f1 all within 0.5 dB; 4-6 dB low at 0.47) and how long its VCAs conduct; the cymbal and hats agree |
| TM4 (noise) | 130 mV rms | 190 mV | the toms' noise tails (3.3 dB hotter, flat 125 Hz-2 kHz and 30-450 ms); the snare and clap agree |
| TM3 (clap offset) | mid | 0.37 | the clap's offset: Raum 45's, the archive.org and Reverb's units (their tails within 0.6-1.3 dB RMS from 0.9 to 7 kHz, their tails' -40 dB times, their accents within 1.3 dB); Fischer's unit is 0.26 (Unit spread) |
| IC2a's limit (`mtrig.ntsat`) | 12.8 V (a typical µPC4558 on the 14.1 V +B1) | 11.4 V | the metal voices' accent (the archive.org unit's four agree; Fischer's hits aren't accented) |
| BD VR6 (DECAY pot) | 500k | 585k | the decay range at full DECAY |
| LT C76, C78, C77, R230, VR11 | 56n, 12n, 47n, 820k, 500 | 53.1n, 11.6n, 47.7n, 838k, 490 | pitch, decay and the TUNING range (LT and LC) |
| MT C90, C92, C91, R259, VR13 | 27n, 12n, 27n, 820k, 500 | 27.2n, 11.2n, 26.2n, 862k, 527 | pitch, decay and the TUNING range |
| HT C103, C105, C104, R286, VR15 | 27n, 5.6n, 22n, 820k, 500 | 25.0n, 5.71n, 21.9n, 851k, 540 | pitch, decay and the TUNING range |
| RS R313, C117, C119, C113 | 390k, 2.2n, 2.2n, 47n | 500k, 2.32n, 2.21n, 36n | the claves' decay and mute (R313 as printed can't decay) |
| RS R318, C118 | 220k, 2.2n | 165k, 2.35n | the rim shot's 455 Hz against its 1.7 kHz (-6 dB from the onset, in Fischer's unit and the archive.org one alike), and that line on the unit's 1712 Hz (C118 is in circuit only for RS) |
| RS R324 (unprinted) | 150k (the table's level) | 120k | the rim shot's level against the other voices |
| CB C34 | 1µ | 1.13µ | the cowbell's tail (C9 and R82 as printed) |
| CY VR2, C41, R125 | 2M, 1µ, 2.2k | 1.57M, 1.05µ, 3.55k | the cymbal's long envelope and TONE |
| OH VR3, R132, C60 | 2M, 100k, 10µ | 1.95M, 120k, 7.36µ | the open hat's DECAY timer (C60 refitted at the metal group's rate) |
| CP C128, C129, C138 | 4.7n, 4.7n, 47n | 5.64n, 5.05n, 56.4n | IC21b's band-pass (the reverb tail's spectrum) and the reverb envelope (its decay), each within 20 % |
| CP C144 (0.47/50, electrolytic) | 0.47µ | 0.40µ | the sawtooth's teeth: the second and third start 9.1-9.8 and 20.8-22.8 ms after the onset in all four real units, 10.9 and 23.7 as drawn; now 9.7 and 21.0 (Unit spread) |
| Step ticks (`mix.rtk3`, `mix.ctk3`; not on the schematic) | none | 107 MΩ, 4.99 pF | the common trig's edges into the mix: the archive.org unit's averaged tick, peaks 0.99 ms apart, at the level between it and Synthmania's unit (mix.cir; Unit spread) |

**Knob laws.** Where Fischer's marks show it, a knob's tick sets a different rotation than its
number: `Calibration::knobLaws`, the rotation at the 2.5, 5 and 7.5 ticks (straight between
them and the ends), from `tools/compare/marks.py` (the rotation at which the model meets the unit
at each recording):

| Knob | 2.5 | 5 | 7.5 | Evidence |
|---|---|---|---|---|
| LT / MT / HT TUNING (500 Ω B) | 0.163 / 0.142 / 0.167 | 0.458 / 0.442 / 0.483 | 0.735 / 0.740 / 0.797 | pitch, with each pot's value fitted (Calibration); each pot's tom and conga agree within 0.02 |
| CY TONE (20k B) | 0.154 | 0.471 | 0.833 | centroid, five recordings a tick |
| BD DECAY | 0.153 | 0.471 | 0.800 | the shape above (a track from about tick 1.3 to 9.1), which fits its held-out ticks better than straight: LSD 0.89 -> 0.64 |
| SD SNAPPY | 0.18 | 0.55 | 0.90 | LSD over all 25 recordings with the snare at the group's rate (2026-11-04; it had BD DECAY's shape at the board's rate): median 1.73 -> 1.48, the 2.5 ticks 2.3-4.7 -> 1.3-2.3 (5 ticks), the -20 and -40 dB times on Fischer's; the centroid alone puts 2.5 at 0.177 and asks less at 5 and 7.5 (0.436, 0.667), the decay more (S2) |

The open hat's and cymbal's DECAY and the snare's TONE fit a straight scale better; LEVEL,
ACCENT and the BD's TONE have no evidence, so they stay straight. The laws were taken from every
mark, so the 2.5/7.5 marks aren't held out for the TUNING pots and the cymbal's TONE.

The snare and clap use the service notes' later ("PORTION CHANGED") values (`lateSnare`,
`lateClap`), as Fischer's unit has them. Two fits are still well outside the parts' tolerances
(CY R125 +61 %, RS R318 -25 %) and are open points. Three earlier ones turned out to stand in
for something else: the cowbell's C9 and C34 (+45 %, +83 %) for the squares' duty, the snare's
R194 (doubled) for the noise trim, and the cymbal's C41 (+46 %) for R126's connection.

## SERVICE: the trimmers, the unit, the probe

**The trimmers as parts** (`core/trimmers.*`). Each of the notes' six trimmers (SN p.14) is its
rotation (0 fully anticlockwise, 1 fully clockwise) through the resistance it puts in circuit,
turned clockwise for a faster oscillator or a higher level; the processor holds them as
parameters. A trimmer at its unit's own rotation sets that unit's value as it is (the fitted
unit's 558.3 Hz, not one worked back from a rotation), so the default runs to the bit as before.

| Trimmer | In circuit | What it sets | Late board's travel | Nominal / fitted unit |
|---|---|---|---|---|
| main TM1 100k(B) | in series with R49 100k in IC1's oscillator, C11 .047 + C203 .039 to ground, D13 + R48 10k discharging it (/INT low) | `Machine::setIntClock`: TP2's period | 2.49-1.31 ms | 0.50 / 0.50 (1.9 ms) |
| main TM2 10k(B) | from -15 V to ground, its wiper through R43 220k into Q14's base node | `TempoClock::bpm(tempo, fine, tm2)`: TP3 at TEMPO and FINE fully clockwise | 17.3-4.1 ms (fully anticlockwise TEMPO 145-34 ms) | 0.50 / 0.50 (8.33 ms, 69.6 ms) |
| main TM3 10k(B) | C.P. OFFSET, through R370 47k to Q71's base | cp.cir's `offset` control | its wiper -15 to 0 V | 0.50 / 0.37 |
| main TM4 22k(B) | IC24a's gain leg (with C200; R127 4.7k on the first generator) | `noiseRms`, by IC24a's gain against its mid travel | x0.51-x49 (the track's end 1 %) | 0.59 / 0.66 |
| voicing TM1 220k(B) | with R44 150k and C6 .022 in IC1's square | `cbHz1`: TP6 | 3.93-1.59 ms | 0.89 / 0.92 |
| voicing TM2 220k(B) | with R45 100k and C5 .018 | `cbHz2`: TP7 | 2.78-0.87 ms | 0.80 / 0.82 |

- **The squares** use the cymbal's formula (Werner's typical thresholds, T = 0.483 RC).
- **The INT clock** is the same chip on 5 V ("P14 = 5V"), charging through R49 and TM1 and
  discharging through D13 and R48 as well. With the typical part's 0.6 V of hysteresis the late
  board couldn't reach 1.9 ms (2.25 ms at the end of TM1's travel): the notes fit C203 or leave it
  out by the chip's hysteresis (SN p.15 change 1, "To down"/"To up"). No unit's thresholds are
  known, so the model takes a chip centred on 2.4 V whose hysteresis (0.35 V, near the data sheet's
  least) puts 1.9 ms at TM1's mid travel; /INT is then low 0.15 ms a cycle.
- **TM2**'s own current wasn't in the tempo law (the 8.33 ms at full TEMPO and FINE was set
  directly); with it, mid travel is that trim, and the whole travel moves Q14's base 37 mV, a
  factor of 4.3 in tempo.
- **TM4.** The noise circuit runs with TM4 at its mid travel as before; the trimmer scales the
  noise's density by IC24a's gain at its rotation against mid, on the noise transistor's level
  (the nominal unit's: `noiseRms` 0.130 at mid). TP4 reads 0.83 of `noiseRms` at a 96 kHz board:
  the density formula takes IC24a's gain flat to C202's 7.23 kHz and on beyond Nyquist, where the
  circuit (its impulse response, `VoiceBoard::tp4PerNoiseRms`) has its own band. So the nominal
  unit's TM4 sits a little up (TP4 130 mV is `noiseRms` 0.157), and the fitted unit's 0.19 reads
  157 mV rms at TP4.
- **The unit's trims.** Its calibration's. Choosing a board sets the trimmers there; one that
  can't reach its target stays at the end of its travel and says so. The notes' nominal unit
  (`--cal nominal=1`, the tools only) reads the procedure's TP2 1.9 ms, TP3 8.33 ms, TP4 130 mV
  rms, TP6 1.85 ms, TP7 1.25 ms, TM3 at mid.

**The unit by serial number** (`core/revisions.h`, `TR808Processor::setSerial`). A unit has every
change of SN p.15 whose serial it has reached; before each, the parts' values the change lists
and the drawings' lot notes give:

| Serial | Parts (late values in brackets) | In the model |
|---|---|---|
| 000001-000299 | INT clock C11 .068, no C203, R49 150k (the lot note; .047 + .039, 100k); cowbell C6 .01, R44 390k, R45 330k (.022, 150k, 100k) | TM1's ranges (2.43-1.50 ms; 1.9 ms at 0.57). The cowbell's: TP6 1.88-2.95 ms, TP7 2.87-4.78 ms, so neither reaches the notes' 1.85 and 1.25 ms (the lot notes give "1.25ms" and "1.85ms for 1~2 lot", swapped) with C5 as drawn: its early value isn't known (`circuit.md` §17) |
| 000300-010599 | CP R346 1k, R332 22k (680, 27k); the first noise generator: R127 4.7k under TM4 to ground, R129 330k into pin 2, R131 300k, no C200 or C202 (10µ, short, 100k, 22p); voice gates R106, R154, R182, R213, R242, R268, R298 22k (10k); IC6 R101 10k, no C201 (100k, 22p) | `noise.cir`'s params (C200 kept in series with R127: the first generator is DC coupled, which only its offsets would show, and the model has none; R129 carries only the op-amp's bias current); the netlists' gate and clap params. IC6 isn't modelled: the common trig is an ideal 1 ms |
| 010600 up | the late board, every netlist's default | Fischer's unit, SN 103852 |

The other changes (CPU R91, DIN R25, the FET switches, the LEDs, the voicing board's transistor
types) touch nothing the model runs. R332 22k and R346 1k on their own (the same noise) give the
clap's bursts +1.16 dB (IC21b's band-pass with its admittances, over the noise's flat band: +1.03)
and its reverb +4.59 dB (with R342/R346: +4.20); the 000300-010599 board's clap, its gate at 22k
too (less drive to Q29 at an unaccented trig, a weaker trig into both envelopes), is +2.5 dB at the
master out. The portion (`lateSnare`, `lateClap`; SNARE + CLAP, EARLY or LATE, in the view) is a
choice of its own. Each change rebuilds the board (`setCalibration`), its
voices at their operating points; `checkJobs` passes on every board and portion.

**The probe** (`VoiceBoard::setProbe`). A probe on a netlist node loads that node's partition
again with the node as an output, its state, rate and controls kept (`Block::retap`), and reads
it where the partition steps: after the trigger circuits and the noise, after the squares and
the metal source, in its voice's job, after the mixer; a board signal is read where it is
written. Each board sample records the probe's value after the sample's last step (a voice at
twice the board's rate, its second), and the voices' gates (the common trig's 1 ms with each
voice's data line). The voices' jobs and their grouping are unchanged (threaded a busy 3 s with
two probes takes 1.0 s against 2.4 s on one thread). TP2 and TP3 are the CPU board's, recorded at
the host rate. The reload nudges the solver's path once (linear mode and the predictor start
again), so a probed hit isn't bit for bit the unprobed one; with the probes off nothing is
reloaded and the board runs as before, to the bit. The board doesn't sleep while a probe is on.

## Validation

### Service notes (`tools/service/check.py --unit`)

The p.14 voice check: each voice at its MULTI OUT, LEVEL full, its other knobs at 12 o'clock;
amplitude (first swing, Vpp) with ACCENT LEVEL fully anticlockwise and fully clockwise, frequency,
and decay to 1/10 of the first swing. The notes call the values "typical and variable"; a value
passes within 25 % (amplitude, frequency) or 40 % (decay).

| Voice | Vpp normal (table) | Vpp accent (table) | Hz (table) | Decay ms (table) |
|---|---|---|---|---|
| BD | 3.7 (3.5) | 11.5 (10) | 53 (56) | 352 (300) |
| SD | 3.4 (3) | **14.5** (10) | 173 (172) | 71 (60) |
| LC | 3.2 (3.5) | 14.3 (12) | 200 (185) | 187 (180) |
| LT | 3.1 (3.5) | **15.2** (12) | 89 (90) | 206 (200) |
| MC | **2.1** (3) | 9.3 (10) | 281 (280) | 97 (100) |
| MT | 3.1 (3) | **14.0** (11) | 135 (135) | 135 (130) |
| HC | 3.1 (3.5) | 12.1 (12) | 412 (400) | 85 (80) |
| HT | 3.4 (3.5) | **15.4** (12) | 188 (185) | 95 (100) |
| Claves | 3.0 (2.5) | 9.4 (8) | 2424 (2500) | 24 (25) |
| RS | **3.9** (3) | **17.9** (10) | | 12 (10) |
| MA | **4.2** (3) | 5.6 (5) | | |
| CP | 6.9 (6) | 10.1 | | 95 (100) |
| CB | 3.5 (3.5) | **6.3** (12) | 824 (800) | 51 (50) |
| CY | 3.9 (3.5) | 6.9 (7) | | **355** (800) |
| OH | 3.8 (3.5) | 6.9 (7) | | 320 (450) |
| CH | 2.6 (3) | **4.1** (6) | | 42 (50) |

45 pass, 11 fail (bold); with the nominal parts (`check.py` without `--unit`) 46 pass. Every
frequency and all but two decays pass. The fails:

- **Accent of the toms and snare** (27-45 % over): the second real unit below accents its toms
  exactly as the model does (13 dB, the germanium diodes' boost), against the table's ~10.7 dB,
  so the table and that unit disagree; the snare is hot against both (Open points). The rim
  shot's 17.9 V (and its plain 3.9 V, since R318 lets the 1.7 kHz click through 2.4 dB stronger
  at the same RMS) is the peak to peak of a spiky wave; by RMS its accent is within 0.4 dB of the
  second unit's.
- **CB and CH accent:** the table's CB 12 V (10.7 dB) is more than the narrowed trig allows; the
  second unit accents CB 5.6 dB and the model 5.3. CH's 4.1 V is the fitted unit's IC2a limit
  (11.4 V) with its swing VCA at the metal group's rate; the nominal 12.8 V gives more.
- **CY decay:** Michael Fischer's unit (which the model is fitted to and matches) decays to
  -20 dB in 380 ms at 12 o'clock, half the table's 800.
- **MC, MA levels:** MC's R253 (2.2k against LC's and HC's 1.5k) is printed, and Fischer's MC
  is 3 dB under his LC too. MA is the fitted unit's hotter noise (with the nominal parts it passes:
  3.6 V). The clap passes since its TM3 is the other units' 0.37 (at Fischer's 0.26 it read
  2.5 Vpp and 10 ms); TP-5, where the notes set its 6 Vpp, is on the jack line (after R296).

Measured as the notes' sketch (first swing over a cycle and a quarter); the cowbell (two tones
that take a few cycles to ring up through its band-pass) and the metal voices by their largest
swing, the maracas and clap (noise) as a scope shows it: the 1st to 99th percentile over the 5 ms
of largest RMS.

### Michael Fischer's unit (`tools/compare/fischer.py`)

His recordings of every voice at every knob mark (0, 2.5, 5, 7.5, 10 on each knob; 106 files),
against the fitted unit. Decay times and tail rates as log2(model/real), octaves (0.1 = 7 %);
pitch in cents; log-spectral distance in 1/6-octave bands over 30-330 ms (first 80 ms for hits
20 dB down within 50 ms), on the bands clear of the recordings' hiss. Medians per voice:

| Voice | Files | -20 dB | -40 dB | Tail rate | Centroid | Pitch (cents) | LSD (dB) |
|---|---|---|---|---|---|---|---|
| BD | 25 | +0.04 | +0.02 | +0.13 | -0.01 | -11 | 0.77 |
| SD | 25 | 0.00 | 0.00 | +0.02 | +0.08 | +10 | 1.52 |
| LT / LC | 5 / 5 | 0.00 / 0.00 | 0.00 / 0.00 | -0.05 / 0.00 | -0.02 / 0.00 | -7 / -3 | 2.99 / 0.69 |
| MT / MC | 5 / 5 | +0.03 / 0.00 | -0.09 / +0.02 | 0.00 / +0.01 | -0.01 / 0.00 | -5 / 0 | 3.01 / 0.49 |
| HT / HC | 5 / 5 | -0.11 / 0.00 | -0.05 / 0.00 | -0.02 / +0.01 | -0.01 / 0.00 | 0 / -3 | 3.06 / 0.53 |
| RS / Claves | 1 / 1 | 0.00 / -0.13 | 0.00 / 0.00 | | +0.10 / +0.01 | -10 / 0 | 1.87 / 1.57 |
| CP / MA | 1 / 1 | +0.37 / +0.26 | -0.23 / +0.22 | -0.98 / | +0.29 / +0.07 | | 2.43 / 1.89 |
| CB | 1 | -0.12 | +0.07 | +0.01 | 0.00 | -4 | 2.53 |
| CY | 25 | -0.07 | -0.04 | +0.01 | 0.00 | | 1.57 |
| OH / CH | 5 / 1 | +0.05 / -0.14 | -0.04 / -0.08 | +0.07 / -0.04 | +0.02 / +0.01 | | 1.36 / 1.94 |

A hit 20 dB down within 50 ms (the rim shot, claves, maracas, clap, cowbell, closed hat and the
short BD and SD settings) is measured over its first 80 ms from the onset with a flat start
(2026-10-16; a Hann window there weighed the ringing tail and nearly dropped the hit), which
moved those voices' LSD: the snare's short cells and the clap closer (e.g. SD at TONE 10 SNAPPY 0
1.71 -> 0.83), the cowbell (0.42 -> 2.53, its attack: Open points), BD at DECAY 0 (0.5-0.7 ->
0.75-1.16) and the closed hat further. Pitches come from a finer transform than before (2 cents
per bin at 120 Hz): the BD's -11 cents was 0 on the coarser one. The clap is at the other units'
TM3 (Calibration), so against Fischer's own its tail is short (0.98 octave fast; at his 0.26 it
was within 0.07). The rim shot's pitch is its low
tone's zero crossings over 3-11 ms (`zero_cross_hz`, 2026-10-23): the strongest line of a hit
40 dB down in 12 ms moves with where its window starts and with the attack's shape (the model's
first peak comes 0.5 ms later than the real ones), and read it 56 cents sharp at 44.1 kHz and
32 cents at 96 kHz, against -10 by its period (Unit spread).

The part fits used the 0/5/10 marks; on the 2.5/7.5 marks they weren't fitted to, the numbers
hold (BD LSD 0.71, SD 1.83, CY 1.57, OH 1.55; the toms' 2.7-3.4; decays within 0.19 octave but
LT's -20 dB time, +0.30 on its two held-out files; the congas' 0.22-0.63 aren't held out, their
TUNING laws came from those marks too). Single-envelope-window moves (2 ms) account for
the MA -20 dB, CH -20 dB and CB -20 dB figures.

- **The toms' spectra** are their noise path, not harmonics: above about 1.3 f0 both the real
  and the modelled tom are smooth noise (no lines at k f0), and a noise-only render traces the
  model there. Fischer's unit has 3.3 dB more of it, flat from 125 Hz to 2 kHz and 30 to 450 ms,
  which is the noise trim (TM4, Calibration). The remaining 2.7-3.2 dB is at the metric's floor
  for a noise-dominated tail: the model against itself with another noise realisation scores
  2.8-3.3.
- **The congas' LSD** was pitch: the 2.5 and 7.5 marks were 20-31 cents off, and matching them
  needs rotations of 0.14-0.18 and 0.73-0.80, in tom and conga modes alike: the TUNING pots' laws
  (Knob laws); MT's and HT's ranges were 1-2 % narrow, which the TUNING pots' values set
  (Calibration). With both the congas' LSD is 0.49-0.69.
- **The cowbell** owes its steady spectrum to the squares' duty (Calibration: LSD 0.42 over
  30-330 ms with a Hann window, its even lines the squares' even harmonics through the swing VCAs);
  its attack is the open point.

### A second real unit: accent (archive.org "TR 808 Samples")

An 808 recorded at the mix out, each voice at full accent and without
(`private/reference/real808/archiveorg_tr-808-samples/808 accent levels.flac`). Its knob settings
aren't documented (and moved between takes), so only the accent ratios compare (accented /
normal in dB, peak to peak / RMS over the first 30 ms; the peak to peak of the noisy and metal
voices varies hit to hit, so RMS is the measure there; `tools/compare/accent.py`, the model at
ACCENT fully clockwise). Its snare has the late parts (186/341 Hz).
The metal voices are compared at the knob settings their spectra and decays match: the set once
taken for its cymbal is an open hat at DECAY 0, and its cymbal is two quiet pairs at about
-45 dBFS (TONE high, and TONE low with a long DECAY).

| Voice | Real | Model | | Voice | Real | Model |
|---|---|---|---|---|---|---|
| BD | 9.7 / 7.6 | 9.8 / 7.7 | | RS | 13.3 / 13.0 | 13.1 / 13.4 |
| SD (TONE 0, SNAPPY 0.5) | 9.0 / 8.9 | 13.2 / 10.8 | | Claves | 9.5 / 9.2 | 9.9 / 9.9 |
| LT | 13.3 / 13.5 | 13.8 / 14.0 | | CB | 5.0 / 5.6 | 5.2 / 5.3 |
| MT | 12.8 / 13.0 | 13.0 / 13.2 | | OH (long / DECAY 0) | 4.2 / 4.2, 4.8 / 4.3 | 5.6 / 4.5, 5.6 / 4.6 |
| HT | 13.0 / 12.7 | 13.1 / 13.2 | | CH | 3.9 / 4.2 | 4.3 / 3.9 |
| CP (TM3 0.26 / 0.38) | 1.0 / 4.5 | 5.4 / 8.0, 1.9 / 3.9 | | CY (TONE high / low) | 4.0 / 5.3, 6.8 / 6.6 | 6.0 / 5.4, 6.5 / 6.1 |
| MA | 2.1 / 2.5 | 1.7 / 2.8 | | | | |

BD, the toms, RS and claves are within 0.7 dB by RMS, which checks the trig levels
(4.3 / 14.2 V) and the toms' germanium diodes (they double the toms' gain at accent; the real LT
also glides 92 → 83 Hz accented and not at all without). The metal voices are within 0.5 dB by
RMS: all four take the narrowed trig, whose accented level is IC2a's output limit, and all four
fit 11.4 V (Calibration); at the other op-amps' 13.5 V they were 1.1-1.8 dB hot. The maracas are
within 0.3 dB. The clap is within 0.7 dB at this unit's own TM3 (about 0.38, which its tail's
spectrum and length also give: Unit spread) and 3.5 dB hot at Fischer's 0.26: the offset sets how
far the bursts stand over what the accent adds.

The snare's three pairs are dark (centroid 1.5-1.7 kHz plain, the low resonator's 183 Hz the
strongest line): the model's spectrum matches at TONE 0, SNAPPY 0.5, not at 12 o'clock, where the
snare looked 3.2 dB hot. There it is 1.9 dB hot, and the bands say where: accented over plain, the
tone (100-500 Hz) gains 8.6 dB in the real unit and 9.5 in the model, the snappy (2-8 kHz) 10.6
and 14.6. Reverb's unit accents its snare as the model does, band by band, at seven TONE x SNAPPY
settings (Unit spread), so this unit's snappy accents less than another's: unit spread.

### Unit spread (`tools/compare/units.py`)

The voices without knobs (RS, claves, CP, MA, CB, CH) compare across every real unit we have:
Fischer's (individual outputs), Raum 45's free set (five takes a voice at one setting, 24/88.2:
`private/reference/real808/raum45_free`), the archive.org unit (mix out, plain and accented) and
Reverb's pack (`reverb_audiocom`, two takes a voice, plain and accented, through a tube preamp;
an early unit, below).
The model is hit 8 times in a run, 0.7 s apart, so its free-running squares meet each hit at
another phase and the noise voices get another stretch of noise; its range over the 8 hits is
compared with the spread of the units' means. (BAMTV's two units play patterns with the bass drum
under every hit, normalised, at unknown settings: no use for measures.)

Raum 45's takes behave as accented hits: their decays (RS -40 dB 24 ms, CB -20 dB 115 ms, CH,
MA, CP) match the archive.org unit's accented hits and the model's, not the plain ones. One
unit's takes scatter little: 0.3 dB on the snare's snappy against its tone, 0.15 on the cowbell's
early envelope beyond what the phase does, 2-4 Hz on the rim shot's pitch.

| Measure | Fischer | Raum 45 (5) | archive.org | Reverb (2) | Model (8 hits) | Verdict |
|---|---|---|---|---|---|---|
| CB at 2 ms (of its peak) | 0.95 | 0.44-0.92 | 0.79 | 0.88 | 0.40-1.00 | the squares' phase at the hit |
| CB time to 0.9 of its peak, ms | 1.3 | 1.6-3.5 | 3.4 | 2.5 | 1.4-3.9 | the same |
| RS low tone (zero crossings), Hz | 473 | 474-482 | 455 | 462 | 471 | units spread 5 % |
| RS 455 Hz against 1.7 kHz, dB | -3.4 | +1.1 (accented) | -3.1 | -2.4 | -3.9 | within 1.5 dB |
| RS time to its peak, ms | 1.1 | 1.3 | 1.3 | 1.7 | 1.8 | at the late end |
| CP 2nd / 3rd tooth, ms | 9.1 / 21.6 | 9.2 / 20.8 | 9.7 / 22.8 | 9.4 / 21.2 | 9.7 / 21.0 | C144 (Calibration; 10.9 / 23.7 as drawn) |
| CP -40 dB, ms | 229 | 221 | 180 | 166 | 154-226 | TM3 (264-342 at Fischer's 0.26) |
| CH -20 dB, ms | 42 | 47 (accented) | 41 | | 32-42 | inside |
| MA, claves | | | | | | inside on every measure |

- **The cowbell's attack** (an open point since 2026-10-16) is the phase of its two free-running
  squares when the hit comes: at 2 ms the model's hits reach 0.40-1.00 of their peak, and the real
  takes 0.44-0.95. Measured on one rendered hit against Fischer's one hit it looked 1 ms slow.
- **The rim shot's pitch** by its period sits on Fischer's unit (-8 cents), between the
  archive.org and Reverb units (3.5 and 2 % lower) and Raum 45's (1-2 % higher); see Michael
  Fischer's unit above for the old figure.
- **The clap's teeth**: the second tooth came 1.4 ms and the third 1.8 ms late against all four
  units, the fourth (the end of C140's 30 ms gate) on time. C144, the electrolytic that the
  sawtooth recharges, at 0.40 µF puts both inside the four units (at TM3 0.37: the offset sets
  the floor between the teeth, so where a rise is seen to start). Dielectric absorption in C144
  (`da=`, The solver) shortens the first interval more than the second, but 10 % moves it only
  0.2 ms, and no figure for the part is published.
- **The clap's TM3** differs from unit to unit, and three measures agree on it: Raum 45's, the
  archive.org unit's and Reverb's tails are 4-10 dB brighter than Fischer's above 2.5 kHz (it is
  the offset's bright burst path still open, through C150/R378's 1.6 kHz high-pass), their tails
  fall about 10 dB further from 100 to 400 ms, and the archive.org and Reverb units accent their
  claps 4.5 and 4.6 dB. The model at TM3 0.38 meets the three units' tail spectrum within
  0.6-0.8 dB RMS (0.9-7 kHz), Raum 45's and archive.org's tail within 1-3 dB at 300-400 ms, and
  the archive.org unit's accent within 0.7 dB (Reverb's accent says about 0.32); Fischer's unit
  is 0.26. Since 2026-10-23 the fitted unit takes 0.37 (Calibration): the tails' -40 dB times
  (154-226 ms) fall among the four units' (166-229), the accents within 0.4 dB (archive.org) and
  1.2 dB (Reverb). (C202, the noise's
  7.2 kHz roll-off that early units lack, can't explain it: 1 dB at 3.5 kHz where the units
  differ by 4-5.)
- **Reverb's unit is an early one**: its snare rings at 255 and about 500 Hz at every TONE, the
  resonators before the "PORTION CHANGED" values (`lateSnare=0` gives 249 Hz), against the late
  units' 180 / 340. Its ACCENT is about 0.75 of the rotation: there its toms and congas (15
  pairs, +0.13 dB on average, sd 0.44), rim shot and claves (+0.05) and snare (7 TONE x SNAPPY
  pairs at MIN/MID/MAX, early values: -0.1 dB, sd 0.3) all accent as the model does, the snare's
  tone band -0.2 dB and its snappy band -0.0 (medians). Its plain snare's snappy against its tone
  matches at every TONE x SNAPPY corner (+0.3 dB median), which checks the knob laws on a unit they
  weren't fitted to, and so do the tone's decays; the snappy's -20 dB comes at 64 ms against its
  80 at SNAPPY MID (right at MAX: Open points).
- **Reverb's knob voices** at MIN / MID / MAX (plain hits): the toms and congas decay within 0.16
  octave of the model at their TUNING marks and sit 20-40 cents flatter (MT 10-20 sharper) than
  Fischer's, whose TUNING pots the model has: the units' tuning. The open hat at DECAY MIN decays
  a little longer than the model, Fischer's a little shorter. Its cymbal rings about 1.6 times as
  long as Fischer's at the same DECAY marks (at TONE 0, which its spectrum gives: -20 dB at 355,
  952 and 1303 ms against the model's 269, 501 and 790; the service notes' table says 800 ms at
  12 o'clock), Raum 45's like Fischer's; the schematic's parts decay like the fitted ones, so
  that is the unit. Its kick files can't be used: three with different TONE labels are the same
  hit.
- **Hum**: Raum 45's quiet tails hold 50 Hz at -102 to -106 dBFS and 150 Hz at -109 to -111, about
  100 dB under the hits; Reverb's and Synthmania's (`synthmania_209265`) 60/180/300/420 Hz at
  -80 to -85 dBFS. Each is the unit with its studio's chain; which part is the 808's can't be told.
- **Step ticks**: between the archive.org unit's hits, a tick comes on every step (0.264 s, its
  sixteenths at 56.75 BPM), at both edges of the 1 ms common trig: single ticks peak at -79 to
  -81 dBFS at the mix out, about 67 dB under a plain bass drum's peak (-12.6 dBFS), over a floor
  of -96 dBFS rms. The common trig fires on every step whether or not a voice plays. Averaged
  over the file's 1026 quiet steps (noise of the mean -119 dBFS): a positive spike at the trig's
  rising edge and a negative one at its falling edge, each about 0.12 ms wide with a slower swing
  the other way after it, 0.3-3 kHz; a 30 µs low-pass and high-passes of 95 and 534 µs fit it
  (12 % residual). Synthmania's unit (serial 209265, `synthmania_209265`) has it too: its gaps
  halfway between hits, averaged (14-41 a file over a floor of -66 to -83 dBFS), show the same
  positive then negative spike 1.0 ms apart. Through the same 0.3-4 kHz band and against the same
  voices' hits the two units agree: the rim shot's tick 72.2 dB under its hit in the archive.org
  unit and 71.3-72.6 in Synthmania's, the cowbell's 70.2 and 63.8-68.5 (the range: its gaps on
  the step grid, and aligned to the tick by correlation, which with so few gaps leans on the
  noise). Modelled since 2026-10-23 (`mix.cir`): that network couples the trig bus into the
  mixer's summing node, at the level between the two units (-90 dBFS at the master out against
  the voices at their standard LEVEL; the archive.org unit's alone gives -91), so the HI and LO
  outputs tick on every step, and twice as much (14 against 4.3 V) on accented ones; the MULTI
  OUT jacks don't.

### Levels (`tools/compare/levels.py`, 2026-11-09)

Fischer's set is one chain with every LEVEL at max, so recording minus model per voice should be
one number, the chain's gain, and it is: -7.6 dB, interquartile range 1.0 dB over 13 voices, none
further than 2.2 dB from it (the open hat), files above -3 dBFS left out (his chain limits at
-2.45). The clap's own file sits at -2.95 dBFS, so its windows stand against that gain: at
Fischer's TM3 0.26 it is -0.95 dB in the first 30 ms (+2.5, +1.8, -2.9 in the later windows, to 250
ms), so the circuit's level is right. TM3 is the difference: from 0.26 to 0.37 the clap rises 10 dB.

Clap minus maracas (RMS, first 30 ms) cancels the LEVEL knob and the chain, since the two share a
knob and a jack: -3.05 dB in Fischer's unit, -0.79 Raum 45's, +4.65 Reverb's, +10.80 archive.org's
(whose knobs moved between takes). The model reads -5.88 at TM3 0.26, -1.85 at 0.30, +0.62 at 0.33,
+3.27 at 0.37, +5.79 at 0.42. So the units span 14 dB, the trim explains it, and the fitted 0.37 is
inside; the notes' 6 Vpp at TP5 falls about at 0.35 (0.37 reads 6.9). On this measure Fischer's own
trim would be about 0.29.

### Roland's TR-808 software (`tools/compare/roland.py`)

Roland's plugin (Roland Cloud, an emulation itself, not a reference) played at the same knob marks
as each of Fischer's recordings, through lemus-render with the transport stopped (its own
sequencer otherwise plays along), and measured against the real unit the way `fischer.py` measures
Lemus. Its MIDI notes set the trigger level continuously (velocity 1 is silent on the toms, 100 and
up saturates; AC LEVEL acts only on its own sequencer), so a plain hit is velocity 64, which gives
its BD a 10.6 dB accent at 127 against a real unit's ~10. Its conga, claves and maracas notes play
the toms, rim shot and clap (their switches aren't host parameters), so those five aren't compared.

Medians against the real unit (decays in octaves, pitch in cents, LSD in dB), Roland / Lemus:

| Voice | -20 dB | Pitch | LSD | | Voice | -20 dB | Pitch | LSD |
|---|---|---|---|---|---|---|---|---|
| BD | -0.22 / +0.04 | -102 / -11 | 6.93 / 0.77 | | RS | 0.00 / 0.00 | -6 / +56 | 3.27 / 1.87 |
| SD | -0.10 / 0.00 | -38 / +10 | 5.32 / 1.52 | | CP | +0.12 / +0.12 | | 2.51 / 2.62 |
| LT | -0.05 / 0.00 | +67 / -7 | 5.75 / 2.99 | | CB | +0.64 / -0.12 | -23 / -4 | 3.28 / 2.53 |
| MT | -0.06 / +0.03 | 0 / -5 | 3.62 / 3.01 | | CY | -0.26 / -0.07 | | 2.89 / 1.57 |
| HT | -0.07 / -0.11 | -44 / 0 | 5.36 / 3.06 | | OH / CH | -0.20 / +0.05, 0.00 / -0.14 | | 2.31 / 1.36, 2.82 / 1.94 |

On the knob marks Lemus wasn't fitted to (2.5 and 7.5) it holds: LSD BD 6.88 / 0.71, SD 5.73 /
1.83, toms 3.4-8.5 / 2.3-3.7, CY 2.90 / 1.57, OH 2.60 / 1.55. File by file Lemus is the closer of
the two on 95 of 99 (the clap since, at 2.43 with C144 and TM3 0.37, on 96: Unit spread); the exceptions were the clap
(2.62 against 2.51), the open hat at DECAY 0
(2.17 against 2.14; its decay is 17 % long), and BD at TONE 5 DECAY 0 (1.16 / 1.05) and SD at
TONE 5 SNAPPY 0 (1.51 / 1.26), the last two since the short hits are measured from the onset. Part of Roland's distance is tuning (its BD 89
cents flat, its LT 58 sharp and HT 46 flat of this unit), which differs from unit to unit; its
decays and centroids, which don't depend on tuning, are also further off (BD -0.22, CB +0.64,
CY -0.26, OH -0.20 octave at -20 dB). Lemus against Roland directly: 2.5-6.4 dB.

(This comparison is from 2026-10-12, before the clap's C144 and with the rim shot's pitch read
from its strongest line, 56 cents sharp then; by its period, Lemus is -10.)

Accent (accented over plain, peak to peak / RMS over 30 ms, dB; Roland 127 against 64):

| Voice | Real (archive.org) | Roland | Lemus |
|---|---|---|---|
| BD | 9.7 / 7.6 | 11.1 / 13.2 | 9.8 / 7.7 |
| SD | 9.0 / 8.9 | 13.1 / 17.1 | 12.0 / 12.1 |
| LT, MT, HT | 13.3, 12.8, 13.0 / 13.5, 13.0, 12.7 | 13.3, 12.6, 11.9 / 17.1, 16.3, 15.7 | 13.8, 13.0, 13.1 / 14.0, 13.2, 13.2 |
| RS | 13.3 / 13.0 | 10.3 / 14.6 | 13.1 / 13.4 |
| CB, CY | 5.0, 4.0 / 5.6, 5.3 | 7.8, 6.2 / 8.4, 8.1 | 5.2, 5.7 / 5.3, 5.4 |
| OH, CH | 4.2, 4.0 / 4.2, 4.2 | 9.1, 7.8 / 8.0, 7.9 | 5.4, 3.0 / 4.5, 4.0 |

Whatever velocity counts as plain, Roland's accent raises the RMS 3-6 dB more than a real unit's
does against its peak (its accented hits are fuller, not just louder); Lemus follows the real
unit within about 1 dB except the snare (Open points).

### SPICE

Every netlist against ngspice on the same circuit (`tools/spice/suite.py`, which runs
`compare_circuit.py`, which runs `circuitrun --spice`), at the board's rate, 96 kHz (the metal
group, marked *, at its 192 kHz), with the trigger edges substepped as the board does;
ngspice with its step held under 1/8 sample, so the numbers are the solver's own error. Normal and
accented triggers, knob extremes, the conga/claves/maracas switches; difference in % of the
output's range, RMS:

| Block | RMS % | | Block | RMS % |
|---|---|---|---|---|
| BD (5 cases) | 0.02-0.07 | | CB* (n, a, no squares) | 0.003-0.03 |
| SD (5) | 0.06-0.21 | | CY* (6) | 0.02-1.4 (96 kHz: 1.2-5.2) |
| LT, MT, HT, congas (21) | 0.01-0.10 | | OH, CH, choke* (7) | 0.01-4.0 (96 kHz: 1.6-4.8) |
| RS (n, a) | 0.12-0.27 | | Accent (6) | 0.000 |
| Claves (n, a) | 5.0-6.0 | | Narrowed trig (n, a) | 0.02-0.06 |
| CP (n, a, TM3 0 and 1) | 0.03-0.05 | | Metal hi / lo* | 0.34 / 0.09 (96 kHz: 1.44 / 0.51) |
| MA (n, a) | 0.44-0.52 | | Noise; mixer*, settled and through Q13's power-on mute | 0.001-0.07; 0.002-0.03 |

The trigger edges take 0.1 µs and run as the board runs them (the edge's sample in sixteenths
from the new input values, 8 substepped samples, the first 8 quarter steps by backward Euler, on
the trigger and data lines only; the metal squares and the noise aren't substepped). The DC operating points agree everywhere. Where they differ, it is discretisation
and shrinks with the rate: the claves' clipped 2.4 kHz ring runs 6.5 Hz flat at 96 kHz (5 cents;
2.4 Hz at 192 kHz), which drifts its phase over the 40 ms ring; the hats' and cymbal's 7 kHz swing
VCAs (14 samples a cycle) overshoot some cycles (4.6 % at 96 kHz, 0.65 % at 384 kHz for the open
hat); the metal source's 7.1 kHz bridged-T is 1.7 % low by the trapezoidal rule's warping. The
edges' backward Euler costs the BD 0.1 % (it is first order through the edge), and buys the
claves' and rim shot's mute timing. What the metal voices' few percent amounted to at 96 kHz,
rendering the board at 2x against 8x: envelopes within 0.4 dB, spectra within 0.6 dB up to
12 kHz, the cymbal and hats 1-3 dB low at 12-20 kHz, and intermodulation of the squares'
harmonics at -40 to -48 dB under 2 kHz that depends on how many of them fit below Nyquist; hence
the metal group's rate (Rates). The comparisons found and fixed: the
op-amps' Nyquist ringing and wind-up, the junction nodes' ringing, the edges' ringing and the
limiter ports' convergence (The solver, above), and the comparator clamp in the SPICE export (it
read inputs far below the AN6912's common-mode limit wrongly, so ngspice's clap didn't match).
Per-voice partitioned against whole: Partitions, above.

The op-amps' internal poles (`c<X>_pole`) are integrated by backward Euler, except in the rim
shot's netlist, which the board runs by the trapezoidal rule (`Circuit::setPoleTrap`; the suite
does the same, `POLE_TRAP`). Measured per 1/3-octave band against ngspice (2026-11-04): by
backward Euler the unaccented claves' ring came out 5 dB low at its peak and 4-5 dB low at
8-20 kHz, since the first-order rule damps it; by the trapezoidal rule they are within 0.2 dB, and
above 22 kHz as ngspice, so the MHz loops don't ring there. Every other voice, the metal group at
192 kHz included, is within 0.1 dB of itself under either rule. What remains at the top is the
capacitors' warping at 96 kHz: the snare, clap and maracas 0.7-1.4 dB low at 16-20 kHz.

### CPU

`tr808bench` (best of several runs, Apple Silicon), in % of a core: the host's audio thread
with the voices on worker threads (Threads, below), then everything on one thread
(`TR808_WORKERS=0`). The metal group at twice the board's rate (Rates) costs about 45 % of the
one-thread figure; the slewing op-amps and the edges' sixteenths 2 %. Summed over the bench: 726 %
on one thread before the 2026-10-24 pass, 606 % after it (instructions retired 101.9 to 80.6 G a
second of audio), 543 % with the voices in blocks, and 303 % on the audio thread with the workers;
after P14 613 % on one thread (88.7 G) and 336 % on the audio thread, after the 2026-11-05 round
494 % (70.2 G) and 259 %. A kit at 48 kHz: 3002 M instructions a second on one thread.

| Workload | 44.1 kHz | 48 kHz |
|---|---|---|
| The sequencer: BASIC RHYTHM with BD, SD, CH, CP, accents | 14 / 23 | 15 / 25 |
| A kit from MIDI (BD, SD, CH, OH) | 15 / 19 | 16 / 20 |
| Every voice | 22 / 45 | 24 / 48 |
| Every voice, BD/CY/OH decays and ACCENT at full | 24 / 51 | 26 / 54 |
| Every voice, congas, claves, maracas | 23 / 41 | 25 / 44 |
| Knob automation on seven knobs, every block | 25 / 58 | 27 / 62 |
| Idle (a kick, then asleep) | 1.4 / 1.4 | 1.4 / 1.4 |

**Threads.** A block runs in three parts (`VoiceBoard::render`): the trigger circuits, the noise,
the squares and the metal source through the whole block, keeping what they give the voices at
each sample; then the voices, each a job through the block on its own copy of the board's signals
(`core/parallel.h`, the TR-909's: up to four worker threads besides the audio thread, in the
host's audio workgroup on macOS); then the mixer. No voice takes anything from another or from the
mixer within a sample, which `Impl::checkJobs` confirms from the netlists at prepare (else the
board steps everything sample by sample, as short blocks do: a trigger pulse is rendered a sample
at a time), so the output is the same to the bit with threads, without them, or interleaved
(`TR808_INTERLEAVED`); ThreadSanitizer finds nothing. A block takes about as long as its heaviest
voice, the cymbal or the hats, plus the shared circuits and the mixer (about 6 % of a core). Even on
one thread the voices in blocks run up to 15 % faster than interleaved (each circuit's data stays
in the cache). (The mixer's LO output had the board signal `lo`, the metal source's low band the
cymbal reads; harmless in the old order, it is `mix.lo` now.)

The voices' cost is their switching VCAs (the cymbal's three bands, the cowbell's, the hats', the
snappy, the clap's), which need Newton every sample, then the BD's and rim shot's resonators
(`TR808_BLOCKS=1 TR808_PROFILE=1 voicerun ...` times each partition). What made it faster, with
the output unchanged (bit for bit but where noted):

- Newton: a small system's first iteration of a step reuses the last step's factorisation; the
  loop, `factor`, `solveActive` and the LU are compiled per port count up to 16 (the same
  operations, unrolled); the residual sums every current up to 16 ports.
- Newton may start from the ports' straight-line prediction; each circuit keeps whichever start
  needs fewer iterations, re-trying the other now and then (the BD's resonator: a third fewer).
- The step's products are one loop per row over [states | varying inputs | currents]; the state
  stays as the companion states between full-sample steps; the per-step copies are inline.
- Newton's tolerance 10 µV rather than 1 (6 %; envelopes within 0.1 dB, the rest unchanged).
- Rebuilds (knob moves) build only the full-sample level, the substep and edge levels when used,
  and solve all their right-hand sides at once; devices multiply by precomputed reciprocals.
- 2026-10-24, from the TR-909's solver (726 % summed to 606, instructions retired 21 % fewer):
  the step's products run down the matrices' columns two rows at a time (`ColMat`: the ports
  before Newton, the outputs and next states after it, Newton's residual, and the linear step's
  ports, next states and outputs in one pass); Newton's LU two columns at a time, compiled for up
  to 16 ports; its ports grouped by how it limits them; the device currents try a transistor
  first; a block its voice judges skips its own quiet checks. Not bit for bit: each product's terms
  are fused multiply-adds now, where the compiler had vectorised parts of the old loops with
  separate multiplies, so every render moves at -106 dBFS and below. The envelope (50 ms frames)
  and the spectrum (1/6-octave bands) are the same within 0.001 dB on every workload but the long
  decays, where the cymbal's six free-running squares beat at full decay and any rounding change
  moves their phase: 5.6 dB in one 50 ms frame near the end and 0.6 dB in one band, as much as a
  0.01 % change to Newton's tolerance moves it. (`tr808bench check` shows both.) The SPICE suite:
  76 of its 83 outputs within 0.01 % of their agreement with ngspice before; the other seven are
  the metal voices' (the squares' phase again: the cymbal's short hit 4.4 to 4.9 % rms).
- 2026-11-05, a CPU round, bit for bit (a kit 3770 to 3002 M instructions a second, -20 %; the
  bench 88.7 to 70.2 G): each product's odd last row runs as a pair with its matrix's padding row
  (alone it was one chain of dependent multiply-adds); a step reads its states, inputs and
  currents where they are kept and writes the next states and outputs in one pass into the next of
  three state buffers that go round (`x()`, `xn()`, `xpLast()`; a linear step its ports too, so
  nothing is copied); the whole step (`advanceT`), Newton's residual and the ports' rows before it
  are compiled for the port count; with every port active the factorisation takes the whole
  matrix and the solve gathers nothing, and the LU's decomposition is unrolled; a circuit without
  devices (the noise, the metal source, the output stages) steps by its products alone, and a
  linear step's common case is inline (`Circuit::step`); transistors and diodes run in loops of
  their own; `linearize` forms I - F G over the device blocks; a voice's job copies only the board
  signals it reads; a half-band filter skips a run of identical samples once its windows hold
  nothing else (a sleeping voice's jack). The SPICE suite's outputs are the same to the byte.
  Where the hats go: the closed hat's VCA (`oh:ch`, Newton every sample at 192 kHz, about 4200
  instructions a step) is about a quarter of a kit, the open hat's envelope (in Newton half the
  time) an eighth; then the mixer, the BD's resonator and the snappy. The solver's plain
  `s -= a * b` loops are fused or not as the compiler shapes each loop (`solveActive`'s inactive
  rows: separate multiplies in its four-way unrolled body, fused in the rest), so a rewrite has to
  keep a loop's shape, or its arithmetic moves at -160 dBFS (as unrolling `luSolveN` did);
  `tr808bench check` is the guard.

Automating knobs keeps the voices they belong to awake and rebuilding their matrices.

## Open points

The evidence for each; `improvements.md` tracks them with the plan.

- **The snare's accent** is unit spread: Reverb's unit accents its snare as the model does, band
  by band (Unit spread), the archive.org unit's snappy 4 dB less. Against that unit, at the
  settings its three pairs were played at (TONE 0, SNAPPY 0.5 by their spectrum), 10.8 dB by RMS
  against 8.9: the tone gains 9.5 dB to the real 8.6, the snappy 14.6 to 10.6 (A second real unit). The snappy is superlinear in the model
  because its envelope comes through two junctions (Q47's V_BE as it charges C51, then D54): the
  plain trig's 2.3 V at VR9's wiper leaves 1.25 V at e2, the accented 7.7 V leaves 6.6. A
  10.6 dB snappy accent through those junctions needs a plain trig near 5.7 V, not 4.3; but the
  common trig is one emitter follower for every voice (the snare's 5 kΩ load moves it 1 %), and
  the bass drum, toms, rim shot and claves accent as the unit does. Not moved by R194 (2.2-10M:
  0.6 dB), the noise level with SNAPPY refitted (TM4 down to a quarter: 1.4 dB), Q48's gain or
  Q47's breakdown; every part checks against the 400 dpi p.9, ngspice agrees. The unit's
  high-resonator pair (TONE up) accents 5.0 dB and its fifth pair 6.5. IC14a's slew rate takes
  0.6 dB off the tone's accent (The solver).
- **The snare's onset is 0.08 octave bright** (spectral centroid): with the SNAPPY knob's law
  (Knob laws) its gate is shut at 2.5 as Fischer's is, but its snappy is still strong at 5
  (centroid 10-40 % high) and 10 (5-13 %). D54's knee isn't it (it conducts tens of µA there,
  below the Rohm curve's 0.2 mA, but a knee 100 mV higher moves the SNAPPY 5 cells only 13 %).
- **The clap**: its reverb tail was 2-4 dB short at 0.4-0.8 kHz and 2-3 dB over at 1.3-8 kHz
  against Fischer's unit, as if IC21b's band-pass sat higher, and decayed 0.36 octave fast. Half
  the decay was the 2SC945 model's gain at microamps (the envelope on C139 and C138 drains only
  through Q70's base; `circuit.md`, "Semiconductors"); the rest, and the spectrum, are fitted
  within the parts' tolerances (Calibration): the tail is within 1.1 dB RMS of Fischer's from 0.4
  to 8 kHz (2.3 before), its rate within 0.09 octave. Two of the three fits sit at the 20 % edge.
  Its teeth are C144 now, and its TM3 the other three units' 0.37 (Calibration), which gives
  their brighter, shorter tail and smaller accent (Unit spread). At Fischer's 0.26 its tail from
  100 ms on was 2-5 dB over his (Q70's gain moved it 2 dB, R200 1-2). The clap's level is its TM3
  (Levels, above).
- **Rim shot**: its spectra were measured through a Hann window over its first 80 ms, which on a
  hit 40 dB down in 12 ms weighs the ringing tail and nearly drops the hit; that made it look
  6 dB short at 300-340 Hz and 8-10 dB short at 125-160 Hz, and fitted R318 at 115k. Measured from
  the onset (`fischer.py` now does so for every hit 20 dB down within 50 ms), its low end
  matches both real units (-36 dB against the 1.7 kHz in all three), and the two units agree on
  the 455 Hz tone at -6 dB against the 1.7 kHz, which R318 at 165k gives (-25 % of the printed
  220k, still outside tolerance; IC21 itself rings at 451 Hz, as the drawing's 452.5): LSD 3.87 ->
  1.87. Its pitch is right by its period (Unit spread; the "56 cents sharp" was the strongest
  line's). Its first peak comes 1.8 ms after the onset, at the late end of the units' 1.1-1.7.
  Open: its first swing reads 3.9 Vpp against the notes' 3.
- **Tuning ranges**: fixed by the TUNING pots' values (Calibration, 490-540 ohms): every tom and
  conga within 12 cents of Fischer's at every mark (4.7 cents RMS, about the pitch measure's
  bin).
- **The 1S188FM** (the toms' germanium diodes) has no datasheet; its model is a guess that the real
  unit's accent behaviour bears out. The second unit's LT has a second harmonic at -37 dB (D81's
  IS 3-10x D80's reproduces it; Fischer's toms would bury it in noise).
- **Fitted parts outside tolerance**: the cymbal's R125 (+61 %) and the rim shot's R318 (-25 %).
  R125 at its printed 2.2k with the TONE law measured again and the band envelopes (C39, C40,
  C41, VR2) refitted within tolerance does worse (fit objective 8.6 against 4.7: the spectrum
  0.1 dB better, the -20 dB time 0.1-0.2 octave short): it stands in for the bands' balance in
  time.
- **The cowbell's attack** is not a gap: it is the phase of the free-running squares at the hit,
  which spreads the model's hits as widely as the real takes (Unit spread). `fischer.py` renders
  one phase, so its cowbell onset LSD (2.5 dB) and the metal voices' onset figures carry that
  phase's luck.
- **The open hat at DECAY 0** decays 17 % long against Fischer's unit (84 against 72 ms to
  -20 dB; R132 would have to be about 70k, 30 % under its printed 100k), and short against
  Reverb's (88 ms, -40 dB at 122 against 104): between two units (Unit spread).
- **The snare's snappy** against its tone runs +1.3 dB over the first 40 ms and +0.3 dB after
  (SNAPPY 5-10, the tone's own envelope within 1 dB); within the parts' tolerances nothing moves
  the two apart. It isn't scatter: one unit's takes scatter 0.3 dB on that balance (Raum 45's
  five), and the archive.org unit shows it too (at its settings the balance falls 4.3 dB from the
  first 40 ms to 40-120 ms, the model's 1.4-1.8), and Reverb's (1.7-4.8 dB, the tone's decays
  matching). There it is the snappy's decay at SNAPPY MID: -20 dB at 64 ms against 80 (at MAX
  80 against 72-84). With the model's SNAPPY set so each real snare's early snappy against its
  tone matches (2026-10-23), the snappy's -20 dB time is 0.09 octave short at Fischer's mid marks
  and right at his 10s (-0.08 over all 15), 0.15-0.32 short at Reverb's MID, and those matches
  need rotation 0.62-0.67 at Fischer's 7.5 marks and 0.80-0.93 at his 10s, where the law gives
  0.80 and 1: the model's snappy grows too strong at the top and dies early at mid. The snappy
  sounds only while its envelope (e1) stands over about 0.4 V, D54 and Q48's saturation, after
  Q47's V_BE took its share of the trig: a smaller dead zone in the units would do both, but the
  parts' data sheets don't put it there. R194 lengthens the snappy at every SNAPPY (3.3M: 0.3
  octave long against Fischer and Reverb), so it isn't this; the archive.org unit's snappy is
  long everywhere (0.7 octave), as R194 at 4.7M gives (a lower-gain Q48: unit spread).
  Dielectric absorption in C51 (0.47/50) moves it 0.15 dB at 15 %.
- **Discretisation**: the claves' ring runs 5 cents flat at 88.2/96 kHz. The metal group runs at
  twice that (Rates), where the hats and cymbal agree with SPICE within 0.2-4 % (the accented
  hats' 3-4 % is their chopping's phase drifting over the decay; their envelopes agree).
- **Partition sleep** freezes a hat VCA's coupling capacitors with the squares' last phase: a hit
  that wakes it while the previous one fades differs sample by sample from a never-sleeping run
  (-25 dB), but its envelope is within 0.06 dB and its spectrum within 0.05 dB (`TR808_NOPARTSLEEP`
  runs every partition without sleeping, for such comparisons).
- **POWER**: on is the circuit (Q13's mute, the board from discharged capacitors: Sleep and
  power-on); the rails' own rise and fall are not (ideal sources that are simply on), nor the
  detector's circuit (Q10-Q12: its 1.5 s is taken from the notes' 1-2 s). Off is behavioural
  (a 5 ms fade).
- **The step ticks** are two units', each heard at one output (the archive.org unit's mix out;
  Synthmania's "directly from the output"); whether they reach the MULTI OUT jacks, and whether
  they ride on the accent (a coupling from the common trig, as modelled) or not (from the 15 V
  monostable), isn't known: the accented steps always carry a hit. The network is fitted, not a
  drawn path.
- **Not modelled**: the trigger outputs; hum (-80 to -106 dBFS in three sets, source unknown); the rails' sag
  through their 100 Ω feeds (+B1 dips 0.1-0.2 V on a cymbal hit: about 0.15 dB on a hit that
  lands with it, through the common trig's and IC2a's levels, which follow +B1); component drift
  with temperature ("some 808s need to warm up").
