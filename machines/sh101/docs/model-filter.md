# SH-101 filter section: the model

The model of the synth board from the source mixer to the OUTPUT jack, as built in `core/`. The
circuit it follows is transcribed in `circuit-filter.md`. Each block is checked against SPICE of
that circuit (`tools/spice/filter/`) and against the service notes; the numbers are below.

| File | Block |
|---|---|
| `core/filter.{h,cpp}` | `FilterSection`: the cutoff CV summer (IC18b), the pin-9 divider, the expo, and the glue; `FilterCalibration` |
| `core/vcf_ir3109.{h,cpp}` | `Ir3109Vcf`: the IR3109 stages, C45, the resonance loop (TR26, TR27, C52, D30/D31) and the output sum (R130, R132, C53, R139) |
| `core/env_gen.{h,cpp}` | `EnvGen`: the ENV GEN, its reset, TRIG and LFO modes, and the GATE level for the VCA |
| `core/vca_ba662.{h,cpp}` | `Ba662Vca`: TR28, the BA662A, R140, VOLUME, IC1a, C2 and jack board A |

## 1. Rates and interface

- Everything runs at the voice's internal rate: 4× below 50 kHz host rates, 2× to 100 kHz, 1×
  above, so 176.4–192 kHz. `Voice` decimates.
- Voltages are real circuit voltages: against the +5 V bus inside the VCF and VCA, absolute in the
  ENV.
- From the oscillator section (`OscFrame`):
  - the four mixer sources as Thevenin sources (absolute volts, ohms);
  - the MODULATOR wave against the bus (`modVolts`, the top of VR20);
  - the post-glide KCV in absolute volts (`kybdVolts`; VR8 and VR21 are modelled here);
  - the bender board's VCF BEND and VCF LFO as Thevenin sources against the bus.
- To the oscillator section: `tickEnv()` returns IC16b's output in absolute volts. It rests at
  5.015 V and peaks at 10.11 V, and is the voltage the PWM switch's ENV position sees.
- Output: the OUTPUT jack's voltage into a 47k load. `Voice::kOutputScale` makes **0 dBFS = 2.2 V
  peak**, which puts the unit at the level of Roland's plug-in at VOLUME 10 (`volume-100`: the
  4 V first chosen here was 5.3 dB quieter). A full saw at VOLUME 10 (the spec's 0 dBm maximum)
  peaks near −5 dBFS; only IC1a's own clipping (+2.6/−3.6 V at the jack) passes full scale.

## 2. VCF (`Ir3109Vcf`)

**Unknowns per sample:** the four capacitor voltages (the CAP pins against the bus), the diode
node, and TR27's emitter. Integration is trapezoidal, with Newton on all six (an analytical
Jacobian, solved along its structure: each capacitor's row holds only itself and the stage before,
plus the diode node on stage 1's, with a diagonal of at least 1, so they eliminate down to a 2×2
system in the diode node and TR27's emitter).

- **OTA stages:**
  - C·dv_i/dt = −I_ABC·tanh(u_i/2V_T) − (I_ABC/2V_T)(v_i − v_rest)/A, where u_i comes from the
    actual resistor network and A = g_m·r_o is the OTA's output resistance: its Wilson mirrors
    (lateral PNPs) give 33 700 in the decode's devices (ngspice, 25 °C; 31 700 at 100 µA). It
    leaves each stage's passband gain 0.4 % short of 1 and raises its pole by as much.
  - The OTA's output current is I_ABC·tanh(u/2V_T) times the mirrors' current gain: the tail
    mirror's and the Wilson mirrors' Early effects and β give 1 + 2.2 % at 1 µA, falling 0.143 %
    per e-fold of I_ABC as their V_BEs rise (the decode's devices, one OTA in ngspice at 25 °C).
    Its input pair also loses transconductance with current, by 1/(1 + I_ABC/6.95 mA) (its emitter
    and base resistance with the 560 Ω, over β): with that the three terms follow the decode within
    0.04 % from 0.3 to 500 µA. The model leaves that droop out: a Juno-106's chip shows none up to
    10 kHz in its law end to end (with it, the fit below goes from 3.0 to 7.0 cents RMS), so a real
    chip's expo converter must rise as much, as Open Music Labs measured its own doing. Since
    2026-11-14.
  - Each capacitor carries its pin's own 2.65 pF as well (2.60–2.68 pF in the decode: the OTA's
    output transistors' and the buffer's junctions), 1.1 % on the 240 pF.
  - Stage 1's node includes the mixer's conductance, R113 and R114.
  - Each stage is prewarped at its linear pole (with its buffer's gain at rest), so the poles and
    the self-oscillation pitch land on their analog frequencies.
- **Buffers** (P-MOS source followers, the decode's; checked at transistor level, `tools/spice/
  filter/ir3109`):
  - OUT = CAP + |V_TO| + √(2·I_D/K'), with I_D what the load leaves: of the 20k pull-up's current
    on stages 1 and 3, of the current source on 2 and 4 (lateral PNPs mirrored off a 22k
    reference: 368.7 µA with OUT at the bus and 123k of output resistance in the decode's devices,
    0.38 mA ideal). The loads are the two 68k each output drives (R122, VR18 and R130 for OUT4).
    K' = K·(1 + λ·V_SD), the P-MOS's channel-length modulation with its drain on pin 8 (λ 0.01/V,
    the decode's), taken at OUT = CAP + |V_TO| plus the stage's overdrive at rest (within 2 mV
    over the swing). K'W/L 1 mA/V² and |V_TO| 2.3 V put |V_GS| at 2.917 V (stages 1, 3) and
    3.138 V (2, 4) at rest, the decode's 2.914 and 3.136; OML measured 2.9–3 V. Neither K nor V_TO
    has maker's data. The λ and the current sources went in on 2026-11-14 (|V_GS| was 2.932 and
    3.172): the outputs at the rails moved 11–51 mV onto the decode's, within 2–7 mV.
  - So the buffers' gain is below 1 and moves with the level: 0.888 at rest on stages 1 and 3,
    0.955 and 0.953 on 2 and 4. Each stage's integrator, and so its pole, runs that much slower;
    the stage's DC gain (−1) is unchanged. With the mirrors' gain and the pins' capacitance the
    poles sit at 97.3 Hz per µA of I_ABC at 1 µA and 97.0 at 10 µA (`Ir3109Vcf::poleHz`, the
    probe the panel's status line shows); an ideal chip's would be ≈104.5.
  - Stages 1 and 3 top out where the follower cuts off: the pull-up against the load, +2.52 V above
    the bus. Stages 2 and 4 top out where their current source saturates, +3.8 V (a softplus
    50 mV wide).
  - The OTA outputs near the rails (since 2026-11-14): each OTA's current gains the Wilson
    mirrors' saturation (circuit-filter.md §2), I_C1·tanh((CAP − pin 8 − V_BE(I_C1))/2V_T) on the
    sink and the same towards pin 16 on the source, with the decode's I_S at 25 °C
    (`otaIs` 3.67·10⁻¹⁶ A). The term is exactly zero more than 0.5 V inside the knees. It is stiff
    (time constants of tens of ns), so it is integrated by backward Euler while the rest stays
    trapezoidal: the trapezoidal rule rang from sample to sample on it. A Newton step that would
    cross a knee stops on it, as SPICE limits a junction's steps. TR27's junction steps in current,
    as the diode node does, and the predictor's start is bounded at the knees and the rails. Before
    this, CAP was clamped hard at −4.3 V whatever the drive. In the full mix (saw, pulse and sub at
    10) every stage's negative swing reaches the floor on alternate cycles, so the hard clamp put
    +4.5 dB on the sub's third harmonic against SPICE: −2.0 dB now, and 0.76 dB RMS over harmonics
    1–12 instead of 1.90. The rails (−5 V / +4 V against the bus) remain as a guard. Cost: about
    +12 % of the machine's instructions (the band test, knee checks, start bounds and the
    per-I_ABC V_BE). In return, Newton converges at the top of the range in self-oscillation, where
    the old code often ran out of iterations.
- **C45** (mixer coupling), **C52** (resonance coupling) and **C53** (VCA coupling) are updated
  explicitly. Their time constants are 0.1–1 s. The start-up speed-up below applies to C45 and
  C53 only: C52 is inside the resonance loop, and a fast C52 would put a 300 Hz high-pass in the
  loop for the first 0.2 s.
- **Resonance loop:**
  - VR18's wiper is a Thevenin source into TR26's base (3 Mohm).
  - TR26's collector is −1.58 × the wiper (R131 ∥ R132 in its emitter), plus R132's push-back from
    C53's node.
  - A softplus floor holds the collector above saturation.
  - TR27 is exponential in V_BE, with R137 and the C52 path as its load, so it cuts off when it
    should.
  - D30/D31: 2·I_S·sinh(d/nV_T), 1N4148 parameters.
- **Solver details:**
  - The start is a linear prediction from the last two samples.
  - The diode node is bounded where its current would exceed 2 mA (more than R129 and R113 can
    feed), and its Newton step is taken in current, as SPICE does.
  - Iterations stop once a step falls under 0.1 µV, or under 0.1 mV when no limit (a knee, the
    diode node's bound, the step clamps) cut it: Newton converges quadratically there, and the step
    after one under 0.1 mV measures 10⁻⁹ to 10⁻¹³ V, so the iteration that would only confirm it
    is skipped (since 2026-11-14; against stopping at 0.1 µV every time, the output differs by
    −162 to −253 dB and self-oscillation's pitch by under 0.001 cent). The trapezoid's slopes at
    the accepted point come from the last evaluation, carried to first order across that last
    step; after a capacitor hits its limit, or without convergence, they are evaluated afresh.
  - The OTAs' tanh is taken through expm1 (within 2 ulp of std::tanh, and faster); each stage's
    prewarp is recomputed only when I_ABC changes, and once for stages 1 and 3, which share a
    buffer.
  - Mean iterations: 1.0 at RES 0 and at RES 0.7 (a 5 V saw at 200 Hz, 10 µA; 1.4 and 2.0 before
    the early stop), 2.6 at RES 10 and 50 µA (3.5), 3.7 at RES 10 and 100 µA (4.6, worst 10).
  - The networks' conductances are multipliers, not divisors (the reciprocals are constants).
- **NaN guard:** a non-finite state resets the VCF; the filter section never returns a non-finite
  sample.

**Expo** (`FilterSection`):

- I_ABC = I₀·2^(−V9/19 mV), with I₀ = 2.4 µA, the AS3109's 250 Hz per stage at V9 = 0.
- At the top it compresses as a real IR3109 does: I_ABC / (1 + (I_ABC/408 µA)³)^(1/3).
- A 5 kHz one-pole sits on pin 9 (OML: the CV input slew-limits above ≈4 kHz).

**The law, from a Juno-106.** No SH-101's cutoff law has been measured end to end, but a Juno-106's
has. KR-106 (`private/docs/papers/opensource/kr106_ultramaster`, `J106DACHzTable.h`; GPL code, used
here only as data) measured one voice card's self-oscillation at 93 of its 12-bit DAC codes, from
5.7 Hz to 52 kHz. The card's VCF is the 80017A module: an SMD IR3109 die with the same 68k, 560 Ω
and 240 pF on each stage as the SH-101, so the frequency per µA is the same (ours 97 Hz/µA). Its CV
path is linear, so log₂ f against the code is the chip's own law. With the DAC's three bit-boundary
steps taken out (−8.7, +22.9, −4.5 cents):

| Range | Against a straight line |
|---|---|
| 5.7–20 Hz | within 8 cents |
| 20 Hz–10 kHz | 1.2 cents RMS, 3.1 at most |
| 12 / 15 / 20 / 25 / 30 / 40 kHz | −8 / −28 / −81 / −168 / −283 / −622 cents |

- **Below 10 kHz it is exponential.** That settles two things:
  - Open Music Labs' "rising linearity error" (+1 % at 12 µA, +10 % at 0.5 mA), which the model used
    to carry, isn't in the whole chain. It put the model 38 cents sharp at 4 kHz and 66 at 10 kHz.
    OML measured the expo converter's current on its own on one chip; the Juno-106 measures what
    the filter does with it.
  - Nothing steals tens of nanoamps from the tail mirrors' master: a fixed 33 nA (the decode's
    beta helper on 100k) would bend the bottom octaves by a semitone or more. OML found the expo
    "relatively linear over 100 dB", to about 2 nA. The transistor-level decode now biases the
    helper with 10 MΩ (`tools/spice/filter/ir3109/ir3109t.lib`), and agrees with the model below
    1 µA.
- **Above 10 kHz it compresses.** The knee above fits the Juno-106 within 2.5 cents RMS, 14 at most,
  from 20 Hz to 28 kHz: −31 / −70 / −130 cents at 15 / 20 / 25 kHz (order 2.9 fits no better).
  The transistor-level decode compresses too, but less (−60 cents at 20 kHz) and from lower down:
  its input pair's transconductance falls with current (above), not phase. Its OTAs add under
  6 ns of delay, and its CAP pins' junctions 0.5 cents of self-oscillation at 4.8 kHz (2026-11-14,
  in ngspice with each part's capacitances taken out). The model takes all of the compression on
  the current.
- **What it changes:** the cutoff between 1 and 10 kHz is up to about half a semitone lower than
  before, before VR8's trim; FREQ 10 in the Real unit calibration was 19.0 kHz (28 kHz once FREQ's top went onto the +15 V rail, 2026-11-13; 14.7 kHz self-oscillating since the expo went to 22.8 mV/oct to meet two real units, 2026-11-16; the spec
  says 20). The Real unit's ENV sweep is unchanged within its fit: rest 31.7 Hz, depth 7.16
  octaves, τ 4.27 s (the real take: 31.2, 7.02, 4.23).
- A Juno-6 measured for the same project (KR-106's `KR106VcfFreqJ6.h`) is exponential in its
  summing current from about 4 Hz up through slider 8.5, which agrees at the bottom.
- The IR3109 has no maker's data on its internals. The OTA law, buffer offsets and limits come
  from Open Music Labs' decode and the AS3109 data sheet (`circuit-filter.md` 2.1).

- **Start-up.** After a reset the three coupling capacitors (C45, C52, C53) charge up to 400 times
  faster, easing back to their real rate over 0.2 s, so they start at the mean of what they couple,
  as in a unit that has been on a while. Without it C45 held whatever instant of the saw it saw
  first and leaked a DC step into every gate (a thump of up to 30 % of the signal at note-on). The
  easing matters: a fast capacitor ripples at the audio rate, and stopping it short froze one
  instant of that ripple instead.
- **The circuit's own noise.** 20 µV RMS of white noise at the mixer node (resistors, the OTAs'
  inputs: far below anything audible, about −108 dB re the saw) starts self-oscillation when every
  source is at 0, as in the real unit. It takes 1 s to build at FREQ 4 and 3.5 s at FREQ 2; below
  about 3 Hz (FREQ 1) the coupling capacitors take too much loop gain and the filter doesn't
  oscillate. A fixed-seed generator keeps renders repeatable. The comparison specs with RES 7.5 or
  more run 4 s before line 0 (`"preroll"`), since the real filter oscillates whether a note plays or
  not.

## 3. CV summer (`FilterSection`)

IC18b is modelled from its parts:

- FREQ (VR17 from +15 V, IC1b's output) and R187, both with the pot's loading;
- ENV (VR19 B);
- MOD (VR20 A);
- KYBD: VR8, VR21 B, and TR42 (R195 from +14 V), with its V_EB at its emitter current and its
  base current through the wiper's resistance;
- VCF BEND and VCF LFO from the oscillator section.

IC18b's output is held 2 V inside its rails (0 and +14 V). The pin-9 divider includes TH1 at 1 kΩ.

## 4. ENV (`EnvGen`)

- **C59** is integrated exactly per sample (exponential steps) toward the active switch's target:
  - attack: +14 V (TR3's emitter, 14.3 V; every rail in the ENV GEN) less TR34's saturation,
    through R141 + ATTACK;
  - decay: the SUSTAIN follower's node, through R164 + DECAY;
  - release: TR31's emitter node, through R148 + RELEASE, with R163 and C58 as a second state.
- **Pots:** 1M A taper, 15 % at mid travel, the 303's law, plus 200 Ω residual.
- **Switches:** 6 mV + 8 Ω × current when saturated.
- **Decay switch TR35:**
  - It sinks up to β times its base current and, run backwards, sources up to the base current
    (it is saturated from IC16a through R159).
  - Its emitter node is SUSTAIN − V_BE(TR37) + V_EB(TR36). TR36 carries all of R159's current
    (through R165 or TR35's base) plus the decay current; TR35's V_BE comes from its base current,
    saturated with next to no collector current (I_S·e^(V/V_T)·(1/β_F + 1/(1 + β_R)), about
    0.56 V). Taking only the junction's share of R159's current, as before, put the sustain level
    50 mV low at S 10 once IC16a's high level fell with the +14 V rail.
- **Threshold and SUSTAIN nodes:** solved together, with TR37's base current loading the wiper.
  This lowers the threshold by ≈40 mV at SUSTAIN 10.
- **Latch:** IC16a's latch (with R162 placed as in `circuit-filter.md` 5.4) is a flag. It is set
  when C59 reaches the threshold and cleared by a reset.
- **Reset** (TR32 on): the gate off, a TRIG pulse in GATE+TRIG, or the LFO square's low half in
  LFO mode.
- **TRIG:** the differentiated KCV (C13·R21, 0.32 ms), ×32 with IC5b's 70 µs pole, beyond a diode
  drop. That gives ≈0.54 ms for a semitone.
- **GATE level** (pin 83, for the VCA's GATE mode): TR30 through R142 and R41 into C57, loaded by
  R133 and TR28 (the last sample's current).

## 5. VCA and output (`Ba662Vca`)

- **TR28:** I_E = (V − 5 V − V_EB(I_E))/R, solved by Newton, with the 2SA1015's I_S
  (3.3·10⁻¹⁴). From the GATE level V is pin 85 through R133 12k. From the ENV it is S6's ENV contact,
  where R196 120k from +14 V meets R197 3.3k from the ENV line, as the board has it
  (`circuit-filter.md` 5.7): a Thevenin source 0.25 V above the ENV (at rest) through 3.21k, then
  R133.
- **BA662**, the TB-303's law:
  - output 0.985·I_ABC·tanh(v/2V_T) with v = (in·L + V_os)/(1 + I_ABC/5.5 mA); the input loss is
    scaled from the 303's 2.2k bias resistors to the 101's 1k;
  - `in` is pin 3's open-circuit voltage, and L its division by the pair's input resistance
    (4βV_T/I_ABC plus R138, β 220 as `ba662.lib` gives it) against pin 3's 987 Ω source:
    1.3 % (0.11 dB) at the ENV's peak;
  - V_os = 0.1 mV;
  - into R140 47k, with pin 8 limited to −4.2/+8 V.
- **VOLUME:** A taper.
- **IC1a:** a soft limit at −3.8/+2.8 V about the bus (M5218 on +9 V).
- **C2's high-pass** into R30 ∥ (R29 + R28 + load).
- **Jack board:** the divider, and C12's 76 kHz pole, warped to be exact at 10 kHz.
- **Jack board A's mute** (`JackBoard`, `core/jack_board.*`, run by `Voice` after the filter
  section):
  - TR7 (2SC1815-GR) can shunt the R29/R28 node. At switch-on C13 10 µF from +9 V lifts node A to
    +9 V, and D4 and R35 47k drive TR7's base until C13 has charged through R36 220k and the base.
  - Solved by Newton on five nodes (node A, D4's cathode, TR7's base, the node, and C2's share of
    TR7's current through R29 and R30), backward Euler for C13 and C2, with Ebers–Moll for TR7 and
    the 1S2473's diode law. The rest of the network comes in by superposition, from the jack's
    voltage with TR7 off. Newton stops once a step falls under 0.1 nV, or under 10 µV when the
    junctions' 0.1 V limit didn't cut it (the step after one under 10 µV is about 1 nV): 2.7
    iterations on average in `sh101bench`'s `bright`, 3.9 before (2026-11-14).
  - Switched on, a note is about 30 dB down for the first quarter second; the negative half comes
    back first (TR7's reverse beta is 5), and the output is open after about 1.4 s. IC1a's step to
    the bus charges C2 through R29 into TR7: about 38 mV at the jack, gone in 5 ms.
  - Long after, the same path (D3 or R36, D4, R35, TR7's base-collector junction) conducts when the
    node swings below about −1 V: a sine of 1 V peak from IC1a loses 0.63 dB on its negative half,
    2 V 2.4 dB, 3 V 3.4 dB. Through C2 that current also lifts the waveform a little.
  - Otherwise, with nothing conducting (node A settled, the node above −0.4 V), it passes the jack's
    voltage through and only lets C2's share decay.
  - Switched off (`JackBoard::powerOff`): +9 V falls, here 9 V × e^(−t/12 ms), an estimate from
    the unit's 1 W (about 110 mA) and the rail's capacitors (C4 100 µF and the rest, about
    150 µF). TR8 (2SA1015-GR) turns on once +9 V is about 0.4 V below C14 (47 µF, charged to
    9 V through D5), after half a millisecond, and C14 drives TR7 through R35: the output is shunted
    for seconds. Meanwhile `Ba662Vca` keeps IC1a under +9 V less 1.2 V and above ground, so its
    output falls from the bus to ground, and takes the rest of the voice as silent once +9 V is
    under 6.9 V (3 ms: the DC-DC converter's 21 V then under the +15 V regulator's ~16 V). C2
    passes IC1a's fall on, and TR7 takes most of it: a thump of about −41 dBFS over the next
    20 ms (−30 for a 5 ms fall, under −45 for 25 ms). What is left after 0.3 s is TR7's
    saturation offset, about 5 mV, which would decay over seconds; it is faded out over 0.2 s so
    the plugin ends on exact zeros. Seven unknowns (the five above, TR8's emitter and base),
    solved by Newton with Gaussian elimination, only during that half second.
  - Against ngspice with the same models (`tools/spice/jack`): within 3 mV through the switch-on,
    within 0.05 dB on the clamp. Switched off with a 1 V note playing: the model mutes about
    50 µs earlier as TR7 turns on (the note's last peak 1.4 dB lower), then agrees within 1 mV.

## 6. Calibration (`FilterCalibration`)

| Field | Service notes | Real unit | Roland | Meaning |
|---|---|---|---|---|
| `vcfMvPerOct` | 19.0 | 22.8 | 22.1 | expo slope at pin 9 (AS3109 data sheet 17.5–20.5; the decode 17.9 at 27 °C, 20.2 at 65 °C; measured on an original chip 20.8). With VR8's trim it sets FREQ's span. Real unit: FREQ 10 where two real units' tops are (13.9, 15.2 kHz); Roland's: its plug-in's 12.2-octave span |
| `vcfI0uA` | 2.4 | 3.30 | 3.9 | expo current at pin 9 = bus (AS3109 2.4; the decode 2.96) |
| `vcfKneeUA`, `vcfKneeOrder` | 408, 3 | 408, 3 | 408, 3 | the expo's compression at the top, as a Juno-106's chip measures (§2); a lower knee as the cause of the SH-101's top was tried and dropped (`improvements.md`) |
| `th1Ohms` | 1000 | 1000 | 1000 | TH1 at 25 °C |
| `vr8Ohms` | 0 | 0 | 0 | VR8 VCF WIDTH; 0 = trimmed by the service procedure for the other values (below) |
| `v15Rail` | 14.87 | 14.87 | 14.87 | the +15 V rail (IC1b) at FREQ's top and R187, as measured |
| `envCvScale` | 1.0 | 0.90 | 0.89 | the ENV's weight at the cutoff summer against the schematic (`docs/model.md`, "Calibrations") |
| `envPotScale` | 1.0 | 0.9 | 0.8 | the 1M A ENV pots against nominal (±20 %) |
| `envPotMid` | 0.15 | 0.15 | 0.15 | the ENV pots' A law at mid-travel (Japanese A: 10–20 %; Roland's curves fit best at 0.15) |
| `vcaOffsetMv` | 0.1 | 0.1 | 0.1 | the BA662A's input offset |
| `vcaKneeShiftVolts` | 0 | 0 | 0 | TR28's turn-on moved down from where it is drawn; 0 since R196 sits on S6's side of R197 as the board has it, kept for tools (`docs/model.md`, "Calibrations") |
| `loadOhms` | 47 000 | 47 000 | 47 000 | the load on the OUTPUT jack |

`realUnit()` is fitted to recordings of a real SH-101, `roland()` to Roland's plug-in
(`tools/compare/fit_roland.py`); `docs/model.md` gives the evidence for each value. The Real unit's
reference current is 1.7× the decode's: that is what puts FREQ 0 where the real unit's sits, with
the measured slope.

**VR8, by the service procedure** (SN p.6, "VCF ADJUSTMENT"):

1. RES 10, KYBD 10, mixer at 0. Hold A4 (KCV 2.75 V) and set FREQ for 1 kHz: FREQ = 0.549.
2. Play F4 (2.417 V) and F5 (3.417 V) and trim VR8 until F5 is exactly an octave above F4. That
   gives **VR8 = 28.58 kΩ** for the service-notes values.

`FilterSection` does this trim itself for whatever expo scale and divider a calibration has (unless
`vr8Ohms` fixes it): one volt of KCV at KYBD 10, through R193 into R192 and the pin-9 divider, must
move pin 9 by one octave of the expo. That gives 28.54 kΩ for the service-notes values (1.0003
oct/oct by the procedure, `tools/service/check.py`), 16.9 kΩ for the Real unit's and 10.1 kΩ for
Roland's (22.1 mV/oct).

Tracking after the trim (self-oscillation pitch, `sh101filterblocks kybd-track`):

| Span | Tracking |
|---|---|
| F3–F4 | 0.998 oct/oct |
| F4–F5 | 1.000 oct/oct |
| F5–F6 | 1.003 oct/oct |
| 4 octaves about A4 | 1.0000 oct/oct |

## 7. Validation

### 7.1 SPICE of the transcribed circuit (`tools/spice/filter/`, ngspice)

Device models are typical-value 2SC1815GR, 2SA1015GR and 1S2473 (`models.lib`), their values
for 25 °C (TNOM=25), and every netlist runs at 25 °C, as the model does. The IR9022 is
behavioural, from its data sheet. The BA662 is the TB-303's transistor-level `ba662.lib`.

**The IR3109 at transistor level** (`ir3109/ir3109t.lib`, `ir3109/compare_ir3109.py`, both at
25 °C). It is built from the decode (`circuit-filter.md` 2.1): bare NPN pairs with the internal
560 Ω on their non-inverting side, Wilson mirrors, P-MOS followers on 20k pull-ups and 22k-set
current sources, and the op-amp-held expo pair. The device models are generic integrated ones
(NPN BF 150, lateral PNP BF 40, the P-MOS above). It replaces the behavioural chip in `vcf.cir`.
Against the model, with the buffers modelled as above:

| Test | Model − transistor level |
|---|---|
| Self-oscillation, I_ABC 2/10/50 µA, RES 0.9/1.0 | +0.3…+22 cents; −0.14…−0.04 dB (2026-11-14; −4.9…+25 cents before the mirrors' gain, the pins' capacitance and the buffers' λ and sources went in) |
| Onset (I_ABC 10 µA) | 0.804 vs 0.801 |
| \|H\| at the VCA input, I_ABC 10 µA, RES 0/0.5/0.75 | within 0.09 dB |
| \|H\|, I_ABC 100 µA | within 0.75 dB |
| \|H\|, I_ABC 1 µA | within 0.07 dB (0.64 dB before) |
| 0–10 V saw through 470k, RES 0 and 0.5 | waveform error 0.2 % and 1.3 % |
| The same saw: harmonics 1–8 | within 0.14 dB (RES 0), 0.05 dB (RES 0.5, 0.7) |
| The saw into the self-oscillating filter (RES 0.9, 1.0) | level −0.14 / +0.64 dB |
| Buffer tops and floors (DC) | +2.52 V (stages 1, 3); floors −1.09…−1.31 V, each within 2–7 mV of the decode's |

With ideal buffers, as the model had before 2026-10, the self-oscillation was 145–165 cents sharp
and the saw into the self-oscillating filter 1.3–3 dB loud. What grows with frequency now is the
decode's input pair losing transconductance with current (about 14 cents of the 22 at 4.8 kHz,
and most of the 0.75 dB at 100 µA), which the model leaves out (section 2), and the discrete
resonance parts' capacitances (TR26, TR27, D30/D31: about 4 cents at 4.8 kHz). Taken out of the
netlist one at a time (2026-11-14), the chip's own capacitances move its self-oscillation by a
constant 19 cents (the pins' 2.65 pF, now in the model) and 0.5 cents more at 4.8 kHz; the
discrete parts' by 0.2, 0.8 and 3.7 cents at 194 Hz, 970 Hz and 4.8 kHz.

The comparisons below (`compare_vcf.py`, with the behavioural chip in `models.lib`) test the
solver and every discrete part around the chip. They predate the buffer model: that chip has ideal
buffers, so its pitch and level now differ from the model by the buffers' share above.

**VCF** (`compare_vcf.py`):

- **Small-signal |H|** (mixer → OUT4 and → VCA input), I_ABC 1/10/100 µA × RES 0/0.5/0.75, up to
  20 kHz or 4·f_c:
  - within **0.07 dB** everywhere up to 10 kHz;
  - one sharp +0 dB resonant peak differs by 1.3 dB (the peak falls between frequency points);
  - at f_c ≈ 10 kHz, 0.9 dB at 15–20 kHz, 40 dB down: the bilinear compression near Nyquist.
- **Passband loss with RES**, at the VCA input: −8.85 dB (SPICE) vs −8.8 dB at RES 0.75.
  OUT4 alone: −13.66 dB.
- **Self-oscillation:**

  | I_ABC | RES | Pitch error | Amplitude error |
  |---|---|---|---|
  | 2 µA | 0.9, 1.0 | −0.3, −0.9 cents | −0.03 dB |
  | 10 µA | 0.9, 1.0 | +1.8, +1.5 cents | −0.11 dB |
  | 50 µA | 0.9, 1.0 | +8, +7 cents | −0.18 dB |

  OUT4 peaks 0.49–0.52 V. The pitch is 0.993–0.997·f_c.
- **Onset:** SPICE 0.787, model 0.789.
- **Large signal**, a 0–10 V saw through 470k:
  - waveform error 1.0 % (−40 dB) at RES 0 and 0.5;
  - self-oscillating (RES 0.9/1.0, not phase-locked to the input): level within 0.1/0.2 dB,
    peaks within 8–45 mV.

**CV summer** (`compare_cv.py`, 16 cases): pin 9 within **2.5 cents** of cutoff. (A constant 7 cents
until 2026-11-13: ngspice took `models.lib`'s 25 °C device values at its default TNOM of 27 °C and
ran the netlist at 27 °C, which put TR42's V_EB 4 mV above the model's.) The exception is the one case where IC18b saturates (FREQ, ENV and KYBD all at
10, top key): 175 cents, past the top of the range in both.

**ENV** (`compare_env.py`, transistor level):

| Scenario | rms error | max error | notes |
|---|---|---|---|
| A3 D4 S5 R4 | 1.5 mV | 5 mV | peak at 0.2214 / 0.2211 s; release to 10 %: 1.070 / 1.071 s |
| all 0 | 18 mV | 148 mV | the 1.8 ms edges |
| A10 D10 S7 R10 | 0.8 mV | 2 mV | peak 3.718 / 3.717 s |
| S10 | 1.7 mV | 5 mV | 23 mV (49 mV off at the sustain) before TR36 carried all of R159's current |
| S0 | 2.2 mV | 6 mV | |
| GATE+TRIG with two retriggers | 2.2 mV | 5 mV | |
| LFO mode at 4 Hz | 4.1 mV | 8 mV | |

**GATE mode VCA** (C57 node loaded by TR28): I_ABC steady 297.9 / 297.9 µA; rise to 90 % 0.590 /
0.582 ms; fall to 10 % 0.855 / 0.843 ms.

**VCA** (`compare_vca.py`):

- TR28's current against the ENV voltage: within 0.01 dB from 57 µA up (0.04 dB at 20 µA, at the
  knee).
- Small-signal gain from pin 3's open-circuit voltage to pin 8 (so the BA662's input resistance
  loading pin 3 is in it): within 0.06 dB from 40 to 300 µA.
- 30 mV at full ENV, where pin 8 clips: +0.17 dB.
- Pin 8 to the jack: within 0.02 dB from 2 Hz to 20 kHz, at VOLUME 10 and 5.

### 7.2 Service notes and spec (SN p.2, p.6)

| Spec | Model |
|---|---|
| VCF cutoff 10 Hz–20 kHz | FREQ alone 1.1 Hz–22 kHz pole frequency (14.2 oct); with ENV or KYBD to the expo's 0.7 mA limit (72 kHz) |
| VCF KYBD 0–100 %, trimmed for 1 oct/oct | 1.000 (F4–F5), 0.998–1.003 per octave |
| Attack 1.5 ms–4 s | 1.8 ms – 3.70 s (to the peak, nominal pots) |
| Decay 2 ms–10 s | τ 1.8 ms – 4.70 s; 90 % of the way in 10.8 s |
| Sustain 0–100 % | linear, 5.0 V to the peak |
| Release 2 ms–10 s | τ 2.3 ms – 4.69 s; 90 % in 10.8 s |
| Audio out 0 dBm max | full saw at VOLUME 10, VCF open: −1.1 dBu RMS (pulse +1.1, sub −1.2, noise +1.8 dBu) |

### 7.3 Against the reference measurements (`roland-measurements.md`)

These are cross-checks only. The model follows the schematic.

| Measurement | Reference | Model |
|---|---|---|
| FREQ span | Roland 12.2 oct (3.4 Hz–15.6 kHz, 239 Hz at mid); a real unit's FREQ 0 near 3.5 Hz | service notes 14.2 oct, 1.1 Hz–22 kHz; Real unit 11.9 oct, 3.75 Hz–14.7 kHz self-oscillating (12.9 oct to 28 kHz before 2026-11-16; two real units' tops 13.9 and 15.2 kHz); Roland 12.3 oct, 3.9 Hz–19.2 kHz (pole frequencies with the buffers' gain, KYBD 0). The digest's 4.4–5.1 octaves assumed FREQ's top at 5 V and is wrong |
| KYBD at 10 | Roland 0.995, real ≈1.00 | 1.000 |
| Passband with RES | Roland: no make-up | the circuit's make-up only partly offsets the loss: −8.8 dB at RES 7.5 |
| Self-oscillation onset | Roland ≈8.2 | 7.9 |
| ENV depth at 10 | Roland 7.65 oct; a real unit 7.0–7.3 oct (KYBD 1.024 oct/oct on the same unit); a second unit fits a weight of 0.80–0.81 (0.72–0.79 with the ENV on +15 V) | service notes 9.7 oct: with VR8 trimmed the schematic can't give less than ≈7.7. Real unit 7.28 and Roland 7.4 with `envCvScale` (`docs/model.md`) |
| Decay/release τ max | 4.94 s | 4.70 s (1 Mohm + 1 kohm, 4.7 µF; pots ±20 %) |
| Attack at 10, to 99 % | Roland 3.6 s; real units about 3 s (Reid) and 3.2–3.3 s to the plateau (Friendly Noise) | 3.70 s nominal; 3.3 s with the Real unit's `envPotScale` 0.9 |
| VOLUME | audio taper | A taper |

### 7.4 Plugin

`sh101plugintest` passes (73 checks). That includes long runs (120 s) at full resonance with all
sources, self-oscillation alone, the noise LFO at full depth, and low cutoff with full resonance
and a fast ENV: levels steady, output finite.

## 8. Known differences and simplifications

- The IR3109's OTAs are matched (no mismatch, no offsets). Real stages have up to ±3 mV of offset
  each, which shifts the DC levels inside the filter and the clipping symmetry.
- The VCA's BA662 has an ideal tanh with the 303's measured losses. Real chips spread by ±15 % in
  g_m (Roland sorted them into colour grades).
- The TRIG detector's pulse width is computed from its RC network. IC4b's input and IC5a's exact
  feedback were not fully traced.
- The LFO line's polarity at S7 deck 2 is assumed: the attack runs in the square's high half.
- R162's position is inferred (`circuit-filter.md` 5.4); the service notes' board diagram and
  Din Sync's BOM confirm it.
- Not modelled: the PHONES output, component tempcos, and TH1's tracking. (TR7's power-on and
  power-off mutes are: 5.)
