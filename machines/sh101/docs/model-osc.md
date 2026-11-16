# SH-101 oscillator section: the model as built

The synth and bender boards from the DAC's sample-and-holds to the source mixer, as transcribed in
`docs/circuit-osc.md`. This covers:
- the MODULATOR and its waveform switch;
- portamento;
- the bender board;
- the pitch summer;
- the CEM3340 with pulse and PWM;
- the 4013 sub-oscillator;
- the noise generator;
- the mixer sliders.

The filter section takes over at the mixing resistors (`docs/model-filter.md`).

| File | What |
|---|---|
| `core/lfo.*` | The MODULATOR (IC3a/IC3b, TR4–TR8, RATE) at the host rate; the processor ticks it and uses its square as the CPU clock |
| `core/osc_circuit.*` | The blocks: `Portamento`, `Bender`, `PitchMixer` (IC12b), `Cem3340`, `PwmNetwork`, `ModSwitch` (S2 and IC18a), `Noise`, `Vco`, `Blep`, `SubLevels`, `PulseStage` |
| `core/osc.*` | `OscSection` joins them at the voice's internal rate; `OscCalibration` holds the trims |
| `tools/spice/osc/` | `blocks.cpp` (target `sh101oscblocks`), the netlists, `compare.py` (model against SPICE), `check.py` (service notes, specs, real units) |

## Conventions at the interface

- `ModFrame::tri` and `Lfo::Out::tri` are IC3a's output relative to the +5 V bus, about
  −4.98 … +5.29 V.
- `ModFrame::square` is CLK: high while IC3b's output is high, and all through CLK RST.
- `OscFrame::mix[i]`: the wiper's voltage (absolute) and resistance as a Thevenin source, behind
  its mixing resistor. Where a source's own resistance changes with its state (TR24 on or off, the
  sub's diodes), the voltage is the Norton equivalent, i.e. the same current into the mixer node
  (which sits at the bus) behind one fixed resistance.
- `modVolts` is IC18a's output relative to the bus.
- `kybdVolts` is the CV line (IC2b, absolute). VR8 and the KYBD slider are the filter section's.
- `vcfBend` and `vcfLfo` are the bender board's VR5 and VR6 wipers (Thevenin, relative to the bus).
- The +15 V rail is 14.87 V, as measured (`circuit-osc.md` 1) and as the filter section has it: it
  sets the LFO's square, the PWM's MAN and LFO sources, the pulse's and the sub's levels and the
  CEM3340's V_CC. The whole-voice SPICE check found the two sections using different values
  (15.0 here), which put PW 7 0.5 % narrow; `tools/spice/osc`'s netlists use 14.87 V too.

## Rates

| Block | Rate | Why |
|---|---|---|
| MODULATOR | host | It runs at 0.07–33 Hz. Edges are placed at their exact time within the sample: the triangle's turnarounds are solved per sample, not quantised. |
| Everything else | internal (host × 4 below 50 kHz, × 2 below 100 kHz) | The VCO's edges, the noise, IC12b's 2.34 kHz pole and the AUTO differentiator all need it. |

## Blocks

### MODULATOR (`Lfo`)

C9 is integrated in closed form. RATE's divider gives the share k of IC3b's ±8 V swing about the
bus. R15 plus the wiper's source resistance set the current. IC3b flips when the triangle reaches
either threshold:
- low: 0.02 V, TR5 saturated;
- high: from R35, R32/TR8 and R31 with S2's load. `setWave()` picks it: 10.83 V, or 12.36 V for
  NOISE.

The time of each crossing is solved within the sample.

**CLK RST** (`setClkRst()`, or `reset()` for a 1.5 ms pulse):
- the node is held at the low threshold, so CLK is high and IC3b flips high at once;
- TR4's base current (68 µA) and, while the square is high, its collector current through D9 and
  R16 (0.49 mA) are added to C9's current;
- the triangle may run below the threshold, as TR4's base current outweighs RATE's current.

`hz()` gives the free-running rate. RATE is an audio-taper track, 15 % at mid-travel (the same law
as every A pot in the voice): (b^t − 1)/(b − 1) with √b = 1/0.15 − 1.

### Waveform switch and IC18a (`ModSwitch`)

| Position | Output |
|---|---|
| TRIANGLE | the triangle through R170 against R188 (100k): 5 + 0.68 (tri − 5) |
| SQUARE | the TR5/TR6 node through R31 against R188 |
| RANDOM | IC11a's 2 × code/12 V through R175 against R188 |
| NOISE | C61's 0.5 Hz high-pass into the R173/R174 bias (4.93 V), then the R172/C60 low-pass (16.3 kHz) |

IC18a is clamped 1.5 V inside its 0 / 15 V supplies.

### Portamento (`Portamento`)

C10 is driven by KCV through VR2: an exact one-pole step per sample with
τ = R_VR2 × 0.33 µF × (`portaTauSec` / 0.66 s). VR2 is the A taper plus a 0.5 % end resistance.

**TR1, when on**, is a 2SK30A in Shichman–Hodges form (I_DSS 1 mA, V_P −1 V). Its gate is at
PORTA IN, and lifted 0.6 V by its junction when charging. So:
- discharging, it limits at 1 mA (3 V/ms on C10);
- small steps see 500 Ω;
- it is integrated in explicit sub-steps (dt below a tenth of 0.17 ms).

**TR1, when off**: the plain RC above.

TR1's state comes from S3:
- **OFF:** on.
- **ON:** off.
- **AUTO:** TR5's base node is solved by Newton (TR5's base-emitter with saturation above R19's
  134 µA, D2, R23, R16 from PORTA OFF), and C11 behind R15 by backward Euler. TR4/TR1 turn on
  while TR5's collector current exceeds 30 µA (R18's 0.6 V). The node is solved for 10 ms after
  each GATE or PORTA OFF edge, then left settled.

### Bender board (`Bender`)

**Lever.** `Performance::bend` (±1) × `leverVolts` is IC3b's deviation. VR4 and VR5 are B-taper
Thevenin sources from IC3b to the bus.

**Grip LFO.** The MOD switch counts as closed while `Performance::modPush` ≥ 0.5. The hardware is
a switch: there is no partial push.
- C9 charges through R22 less TR3's base current until TR3 saturates. TR3's emitter cannot pass
  TR2's emitters, 0.53 V below the bus, so C9 stops near 5 V.
- The tail current is then (4.46 V / R25) − R22's current, about 57 µA. With the MOD released, C9
  discharges through R27.
- TR2 shapes the triangle (through R13/R7) with tanh(v_d / 2V_T).
- IC2a's output is 5 − R12 (I1 − I2) + (R12/R9)(V_VR3 − V3). V3 is solved with the collector
  current, and VR3 is set so the output sits on the bus with the MOD released.

**Summer.** VR6 is an A-taper source from IC2a. IC3a sums VCO MOD IN (R1), VR6 (R2) and VR4 (R4),
each through its source's Thevenin resistance, and is clamped 1.5 V inside 0 / 14 V.

### IC12b (`PitchMixer`)

Inverting about the bus from IC3a (R91) and TUNE (VR9 through R90). The R89/C35 pole is a one-pole
at the internal rate. The output is clamped 1.5 V inside 0 / 15 V.

### CEM3340 (`Cem3340`)

**Pin-15 current:**

    I_C = CV/R104 + RANGE/(R99 + VR5) + V_IC12b/R103 + V_VR7/R108 + I_EG/600

The last term is the pin-7 track from the previous sample's generator frequency.

**Frequency:** f = 1 kHz × exp(−22 (R_S/R_T)(1 − I_C R_Z/3)), with I_C clamped at 0. The comparator
delay then makes it f/(1 + t_d f), t_d = `vcoDelayUs`. The result is capped at 57 kHz (the data
sheet's typical maximum with 1 nF) and at 0.45 × the internal rate.

**Trims, solved from `OscCalibration`:**

| Trim | Setting |
|---|---|
| VR6 | R_S for `vcoScale` octaves per volt |
| VR5 | R99 + VR5 = 100k / `rangeScale` |
| VR7 | Its wiper so that the service notes' U&D test (KCV 4.75 V, RANGE 0, TUNE centred, everything else at rest) gives `vcoTuneHz` |

### PWM (`PwmNetwork`)

S4's source as a Thevenin source:
- LFO: R176/R177;
- MAN: R178;
- ENV: R190.

VR12 (B) runs to the bus. TR25 is followed with its exponential V_BE (from the last sample's
emitter current). Pin 5 is set by R96 against R94 + VR4, through C37 (one-pole, 0.56 ms). VR4 is
trimmed so that PULSE WIDTH 0 gives pin 5 = ½ `pwFullVolts` (50 %). The SH-101 pulse is high
while the phase ≥ pin 5 / `pwFullVolts`.

### VCO waveforms (`Vco`, `Blep`, `SubLevels`, `PulseStage`)

**Phase.** One phase φ ∈ [0, 1) at f, with saw = 10 φ V. The triangle core's tri-to-saw switch
makes a clean wrap with no reset slope.

**Pulse edges.** Within each sample, the pulse's crossings of φ = pw (rise) and the wrap (fall) are
found by linear interpolation of φ − pw, which moves under PWM.

**Sub-oscillator.** Q1 toggles at every wrap and Q2 on Q1's rising edge. The levels at each S5
contact and VR15 come from the 47k / diode network (1S2473: I_S 2.52 nA, n 1.752), solved once per
state, with the 4013 on +14 V (`kV14`; R92, the pulse's pull-up, is on it too: `circuit-osc.md` 1).

**Band-limiting.** Every edge (the saw's wrap, the pulse's two edges, the sub's changes) gets a
4-point cubic B-spline polyBLEP: the residual of the integrated B-spline, two samples either side
of the edge. This adds two internal samples of latency (11 µs at 176.4 kHz).

### Noise (`Noise`)

White Gaussian noise (xorshift128+, Box–Muller) through:
- the C31 pole, `noiseCornerHz`;
- IC12a's response, (23 + s/ω₂)/(1 + s/ω₂) with ω₂ = 1/(R86 C32) = 2π × 3.0 kHz, by bilinear
  transform with prewarping.

σ is set from the filters' power response so that IC12a's output has `noiseRms` V RMS at any
internal rate. The output idles on the bus (R87, R88 and C31 return there).

### Mixer sliders

- **Saw, noise:** linear in the source. A Thevenin source from the wiper fraction, with the bus at
  the far end.
- **Pulse, sub:** per state as above. VR15 runs to ground (OML), the others to the bus. All four
  are B tapers, as marked on the drawing.

## Calibration

`OscCalibration::serviceNotes()` is a unit trimmed as the service notes say, with the parts'
nominal values. `realUnit()` trims the VCO to 440 Hz and takes a real unit's portamento τ (0.78 s);
`roland()` trims the VCO to Roland's 440 Hz and keeps the rest (`docs/model.md`).

| Field | Service notes | What it is |
|---|---|---|
| `vcoTuneHz` | 442 | VR7: U&D at 442 Hz (SN p.4) |
| `vcoScale` | 1.000 | VR6: 1 V/oct (F3–F5 Lissajous) |
| `rangeScale` | 1.000 | VR5: RANGE weighs as KCV (U&D = UP) |
| `vcoDelayUs` | 1.1 | CEM3340 comparator delay per period, the excess the pin-7 track (R100, R199) was sized to cancel (1.15 µs) |
| `pwFullVolts` | 5.0 | CEM3340 pin 5 at 100 % chip width (data sheet typical; 4.6–5.4) |
| `portaTauSec` | 0.66 | 2 MΩ × 0.33 µF. A real unit measures 0.78 s, Roland's plug-in 1.55 s |
| `leverVolts` | 1.48 | Full lever at IC3b, from a real unit's +14.7 semitones at VCO 9 |
| `noiseRms` | 1.05 | IC12a's noise. Not on the schematic. The Real unit calibration uses 0.98, the middle of the two units that agree (below, "Real units"); the Roland calibration 1.30, its plug-in's +2.9 dB (set with the 900 Hz corner; 1150 Hz takes 0.1-0.2 dB off it through the voice) |
| `noiseCornerHz` | 1150 | C31's pole, fitted to three units (1130–1200 Hz; below). SPICE gives it for a junction resistance of about 1.5 kΩ (R83/R84 on +14 V) |

## Validation

### Against SPICE of the transcribed schematic (`tools/spice/osc/compare.py`)

Device models in `tools/spice/osc/models.lib`, from the data sheets at typical values: 2SC1815,
2SA1015, 2SC1583, 2SK30A (I_DSS 1 mA, V_P −1 V), 1S2473. The op-amps are a macro with the IR9022's
26 Vpp and 0.5 V/µs, or the TL062's figures.

**MODULATOR.**

| | Model | SPICE |
|---|---|---|
| Triangle | 0.02 → 10.827 V | −0.01–0.03 → 10.83–10.87 V |
| Triangle, NOISE | 0.02 → 12.36 V | 12.36–12.40 V |
| Frequency error, RATE 0 / 2.5 / 5 / 7.5 / 10 (TRIANGLE) | −0.8 / 0.0 / +0.2 / +0.4 / +0.9 % | — |
| Frequency error, NOISE | similar | — |

The top-end difference is IC3b's slew and switching overshoot.

**CLK RST.** CLK goes high at the reset in both. Taken from the same starting level:
- the triangle ends within 0.1–0.2 V of SPICE (both fall below the threshold by up to 0.5–0.7 V);
- the next rising clock edge is within 1–3 ms (of a 225 ms period).

**Portamento.**
- τ at knob 2.5 / 5 / 10: 32.44 / 101.81 / 660.00 ms against SPICE's 32.44 / 101.82 / 660.08.
- AUTO, 1.8 V detached step: 5 ms after the gate the CV is 1.074 V (SPICE 1.081 V). TR1's 1 mA
  limit leaves about 0.07–0.08 V (a semitone) still to glide.
- Legato glides identically. PORTA OFF jumps in both.

**Grip LFO.**
- IC2a's sine is ±1.191 V against SPICE's ±1.204 V, with an rms shape difference of 0.007 V
  (of 0.86 V).
- The mean when engaged is 4.913 V against 4.906 V.
- Both reach full depth within 0.3 s of the push.

**PWM network.** Pin 5 is within 3.4 mV of SPICE over the slider for MAN, LFO (both triangle ends)
and ENV (rest and peak). 3.4 mV is 0.07 % of width.

**Pulse and sub levels** (current into the mixer node at slider 10):
- pulse 7.436 / −7.337 µA against SPICE's 7.436 / −7.343;
- squares 5.310 / −5.869 against 5.310 / −5.869;
- 25 % pulse 7.115 / 4.930 / −5.435 against 7.113 / 4.928 / −5.435.

**IC3a / IC12b / pin 15.**
- DC within 2.3 mV of SPICE (SPICE's op-amps have finite gain) over VCO MOD, TUNE and bend.
- At VCO MOD 10 IC12b swings 2.79–7.69 V: +2.64 / −2.26 octaves on the square or the triangle.

**Noise amplifier.** The model's colour (1150 Hz pole plus IC12a's shelf) is SPICE's response for a
junction resistance of 1.5–1.6 kΩ within 0.3 dB to 10 kHz (`compare.py noise` still prints the
900 Hz model line).

### Service notes and specifications (`tools/spice/osc/check.py`, 23 of 23 pass)

| Check | Result |
|---|---|
| U&D (KCV 4.75 V, RANGE 0) | 442.000 Hz |
| UP (KCV 0, RANGE 4.75 V) | 442.000 Hz |
| F3 → F5 | 2400.05 cents |
| 8' MIDDLE A4 | 442.000 Hz |
| 1 V/oct from 0.5 to 9.5 V | within 1.3 cents (+0.3 / +0.6 / +1.3 cents at 3 / 6 / 12 kHz: the pin-7 track against the comparator delay) |
| Range | F1 43.85 Hz … C9 8414 Hz |
| TUNE | −63.5 … +64.2 cents (spec ±50; the circuit's R89/R90) |
| VCO MOD 10, triangle peak | +3171 cents |
| Bender VCO 9 | +14.66 semitones (the real unit's 14.7, by construction) |
| LFO | 0.080–36.4 Hz (spec 0.1–30; a real unit 38.2 Hz), 4.99 Hz at mid-travel; NOISE ×0.867 (R31 and R35 100k) |
| Portamento | Two octaves settle to 1 cent in 5.14 s at 10 (spec 0–5 s); 26 ms at 0 |
| PW (MAN) | 50.0 % at 0, 3.0 % at 10 (spec 50 % ~ 0 %; from R178 on +14 V the top of VR12 stops short of 0 %) |
| Sub | 1 OCT 220 Hz 50 %, 2 OCT 110 Hz 50 %, 2 OCT pulse 110 Hz high 75 %, pulse 440 Hz 50 % (VCO at 440 Hz) |
| Saw partials at 2 kHz | h2 −6.04 dB, h4 −12.15 dB; worst alias below 20 kHz −113 dB |
| Saw partials at 8 kHz | h2 −6.37 dB (the BLEP kernel's droop: −0.35 dB at 16 kHz); worst alias −106 dB |
| Noise (the source alone, 50 Hz–16 kHz) | −6.45 dB/octave, centroid 1166 Hz |

### Real units (`private/reference/shootout`)

| Check | Model | Real unit |
|---|---|---|
| Noise colour: each clip against ours with its recording chain (real − ours on the same unit's saw at 10, harmonics to 3 kHz) taken out, third octaves 200 Hz–2.5 kHz, where every clip's codec is clean and FREQ 10 (19 kHz then, four poles; 28 kHz now) takes under 0.3 dB | tilt of real − ours: +0.04 / +0.11 / +0.11 / −0.19 dB/octave (Perfect Circuit, gstormelectro, Failed Muso, AutomaticGainsay); it was +0.56 / +0.63 / +0.62 / +0.32 with the 900 Hz corner | one-pole fits: 1150 / 1190 / 1200 / 1040 Hz (1130 / 1130 / 1130 / 930 on 100 Hz–12.7 kHz with the chain to −60 dB harmonics). With the 900 Hz corner the source gave 1019 Hz on 50 Hz–16 kHz (`check.py`), 979 Hz on the full band and 901 Hz through the voice: FREQ 10's four poles take 2.7 dB at 8 kHz and 9 dB at 16 kHz (now 1166 / 1030 Hz); `units.py`'s full-band centroid and 200 Hz–8 kHz slope mostly measure each unit's top octaves (VCF, chain), which differ by ±2.5 dB at 8 kHz between the clips |
| Bend at VCO 9 | +14.7 semitones | +14.7 semitones (calibrated) |
| Portamento τ at 10 | 0.66 s (schematic) | 0.78 s (the reference fork's measurement): within VR2's and C10's tolerances |
| PWM by LFO | 47–20 % at depth 5, 47–0 % at 10 | 25.7–48.2 % at an unknown depth |
| NOISE slider "about 50 %" | −6.0 dB (linear, as marked) | −8.7 dB re full |
| Levels at 10 re the saw: square / sub 1 OCT / sub 2 OCT / 25 % sub / noise | +1.6 / −0.8 / −0.8 / −2.0 / +0.5 dB (Real unit, `units.py`; +2.0 / −0.25 / −0.3 / −1.4 / +1.1 before R92 and the 4013 went to +14 V and the noise was refitted) | three units (Perfect Circuit, gstormelectro, AutomaticGainsay; sliders read from frames; `units.py`): +1.1 to +1.5 / −0.5 to −0.7 / −0.55 to −0.85 / −1.75 to −2.1. Noise on the band above: Perfect Circuit 1.00 V and gstormelectro 0.96 V at IC12a, AutomaticGainsay 0.72 V (+0.7 / +0.3 / −2.1 dB re the saw). The schematic's estimate (+4.5 square, +2.8 sub) is about 3 dB high |

### Roland's plug-in (cross-check only; `docs/roland-measurements.md`)

`sh101render` with the service-notes calibration against Roland's renders (`measure.py`):

| | Model | Roland |
|---|---|---|
| Pitch | Exact equal temperament at A = 442 | 440 |
| Pulse / sub / 25 % sub / noise at 10, re the saw | +1.6 / −0.8 / −2.0 / +1.0 dB (`units.py`, service notes; the Roland calibration gives +2.6 for the noise) | +1.9 / −1.6 / −2.4 / +2.9 dB |
| PW law | Linear, 50 → 3.0 % (38.3 / 26.5 / 14.8 % at 2.5 / 5 / 7.5) | Linear, 50 → 3.9 % (38.4 / 26.8 / 15.3 %) |
| PWM by LFO (spec `pwm-lfo`) | 19.1–48.0 % | 23.0–49.1 % |
| Portamento τ at 10 | 0.66 s | 1.55 s |
| Portamento τ at 5 | 0.10 s | 0.21 s |
| LFO rate at RATE 0 / 1 / 2.5 / 4 / 5 / 7.5 / 10 | 0.08 / 0.56 / 1.63 / 3.34 / 4.99 / 12.2 / 36.4 Hz | 0.09 / 0.18 / 0.76 / 2.90 / 5.89 / 15.2 / 39 Hz |
| VCO MOD at 10 | +2.64 / −2.26 octaves at IC12b; 5604 cents peak to peak measured as Roland's is (R188 100k, as on real boards) | 5286 cents peak to peak (±26 semitones) |
| Mixer sliders | Linear (the B marking) | Audio |

Roland's plug-in has a more exponential RATE law. The difference near the bottom is the A track's
shape, which the drawing doesn't give.

VCO MOD matches Roland's at 5 (797 against 778 cents peak to peak) and comes within 7 % at 10.
Below 4, Roland's law is steeper (about t^2.7) than VR11's A track, so ours is deeper there: 248
against 145 cents at 2.5.

## Known differences and approximations

- **S3** (portamento switch) contact order is deduced from the three behaviours, not read
  (circuit-osc.md 3).
- **Grip LFO polarity** against VCO MOD depends on which of TR2's collectors feeds IC2a's + input
  (UNCERTAIN).
- **Not modelled:**
  - S&H acquisition (3.3 µs) and droop;
  - the CEM3340's triangle asymmetry, mid-ramp comparator step and PWM-dependent pitch (later
    AS3340s show it, vintage CEM3340s reportedly don't);
  - TR24's and the 4013's switching delays (µs);
  - IC3b's slew within an LFO edge;
  - temperature drift;
  - the TUNE pot's direction (assumed clockwise = sharp).
- **The MOD switch** is a threshold on `modPush` (≥ 0.5), as the hardware is a switch.
- **The noise level** has no schematic value: the Real unit calibration follows the two recorded
  units that agree (0.98 V at IC12a, +0.5 dB re the saw), the Roland calibration Roland's level
  (1.30 V). The service notes keep 1.05 V. The 4013 and R92 are on +14 V, taken as 14.3 V (U-Labor).

## In the processor

The processor calls `Lfo::setWave(controls.lfoWave)` next to `setRate()` (the Schmitt threshold
moves for the NOISE position) and passes CLK RST through with `Lfo::setClkRst(lines.clkRst)`, so
the reset lasts the CPU's exact loop. `reset()` remains as a fixed 1.5 ms CLK RST for tools.
