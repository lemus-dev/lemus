# Voice model and validation

How `src/tb303/voice.cpp` models the TB-303 voice board, where each choice comes from, and how
the result compares with Roland's TB-303 software and with recordings of a real 303.
Component values and designators are from `docs/circuit.md` (service notes, Feb 1982, checked
against the 1986 printing, a cleaned redraw and the x0xb0x); the literature is digested in
`docs/research.md`.

## Rates

The audio path runs at the host rate x 4 (x 2 at 88.2/96 kHz, x 1 at 176.4/192 kHz) and is
decimated with linear-phase half-band FIRs. The VCO and square shaper run at twice that again,
with one more half-band. Control circuits (gate conditioning, MEG, VEG, accent, expo converter)
step once per host sample; the ladder current and the VCA bias current are interpolated per
internal sample (log-linear for the ladder current).

## Blocks

**CPU lines.** The firmware (see `docs/firmware.md`) latches a 6-bit DAC code and the accent
bit on PI1's rising edge; PI1's level is also SLIDE ON/OFF and PI2 is the gate. The R-2R
ladder's termination returns to the 5.333 V rail, so V = (code + 1)/12 V: the C key (code 23)
is 2.000 V = C2, the A key 2.75 V = 110 Hz as the service notes calibrate. MIDI notes map to
the same voltage (MIDI 36 = C2 = 2 V).

**Slide.** With SLIDE high, C35 (0.22 µF) charges from the ladder's 100 kΩ Thevenin source:
an exponential glide in volts, τ = 22 ms. With SLIDE low the follower sees the ladder directly.

**VCO.** 1 V/oct exponential converter (TM5 and TM4 calibrated: exactly 1 V/oct, A = 110 Hz;
SERVICE turns them, see "Service trimmers"),
TUNING ±700 cents (service notes). The saw core ramps from 10.85 V down to 5.6 V (Q28 source)
and resets in about 1 µs, which adds to each period. These levels come from a SPICE simulation of
the saw core (`tools/spice/vco.cir`; see "SPICE" below), not from the service notes' scope sketch,
which is labelled 12 V and 5.5 V: the reset can't charge C33 above the rail less Q27's saturation
voltage, D25 and Q25's V_BE. The reset is a 2-point polyBLEP step.

**Square shaper.** Q8 (PNP) is solved per sample (backward Euler, 4x4 Newton, Ebers-Moll with
β_F 250 / β_R 5): emitter R45 22 k / C11 1 µF self-biasing, collector R36 10 k to 5.333 V,
base fed through R35 100 k in parallel with C10 10 nF + R34 10 k, and the VCF input's 222 k
load through C17 when SQUARE is selected. Duty cycle, the sloped top and the soft middle edge
come out of the circuit, not from a fitted curve.

**VCF input.** R62 220 k / C17 1 µF / R70 2.2 k: the 5.25 Vpp saw arrives as ~52 mVpp at Q12.

**Ladder.** Four caps, bottom to top C18 18 nF, C19, C24, C26 33 nF. State is the differential
voltage on each cap; each diode pair and the Q12 input pair pass I_f·tanh(ΔV/2V_T), the Q22
top pair I_f·tanh(u4/2V_T); du_n/dt = (ΔI_in − ΔI_out)/(2C_n). Solved with the trapezoidal rule
and Newton on all four states, including the feedback loop (zero delay).

**Output amp and resonance loop.** The top differential voltage reaches Q21 through C25/C27
0.1 µF into R113/R114 100 k in parallel with r_π (β and tail current from the calibration; the
schematic's own bias network gives 0.52 mA). The same network draws current from C26, which
damps the ladder at low cutoff. Q21 is a tanh pair with 22 k loads, then Q19 and C14 (3.2 Hz).
The loaded RESONANCE pot (50 k B into ~50 k) feeds C15 (1.6 Hz), R67 into Q18, C23 (13 Hz) and
the R97 10 k / R98 2.2 k divider into Q12's other base. The real stages lose about 10 % of the
ideal loop gain, and the model includes it: Q21's output resistance (Early voltage ~100 V) and
Q19's input in parallel with R110, the Q19 and Q18 followers (0.994, 0.989) and R67 into Q18's
~94 k input (0.977). SPICE gives the Q21 stage a further 2.5 % less gain, flat across cutoff
(its source isn't pinned down): with it the passband level and the ring's decay at full
resonance match SPICE at every cutoff. Without these the loop runs hot and self-oscillates
where the circuit doesn't (see "SPICE" below).

**Cutoff.** The CUT OFF FREQ pot (50 k A, with R47 10 k to node E) sets Q10's emitter current
linearly through R73 100 k + TM3 into the reference node, one V_BE11 above node E; ENV MOD (50 k
A, R61 10 k to node E, R63 220 k) and the accent-sweep wiper (R72 100 k) set the voltage across
R71 2.2 k, which scales the ladder current exponentially. V_BE11 follows the ladder current (it
moves ~90 mV between 1 and 40 µA, taking headroom from the pot at high currents), and the pot's
current also feeds Q11's base; the converter is solved by Newton on the ladder current. Node E
is Q9's emitter and moves with the current Q9 sinks (~3.22 V at rest, ~40 mV higher at the top
of an env-mod sweep, while C13 holds the sweep's voltage). TM3 is solved so the ring at cutoff
centre and full resonance has the calibration's nominal period; SERVICE turns it as the
rheostat it is (see "Service trimmers"). The ladder current is soft-limited at 0.65 mA, where Q11 saturates.

**Calibration.** Two unit calibrations, switched in the plugin:

| | TM3 ring | Q21 β | Q21 tail | What it is |
|---|---|---|---|---|
| Service notes | 2.0 ms | 300 | 0.52 mA | the service-notes trim, and the schematic's own operating point (SPICE: typical β, the bias network's tail current) |
| Roland | 2.47 ms | 435 | 0.52 mA | fitted to Roland's TB-303 software (`tools/compare/fit_roland.py`) |

Roland's unit rings slower at every cutoff (its trim is at the slow end of the tolerance) and
fits best with Q21 at the top of its 2SC1583 F rank (h_FE 250-500 at 1 mA, 220-435 at its
operating point; `docs/circuit.md`, "Gain ranks") at the schematic's own tail current. The fit
searches ring × β × tail, within those ranges, against the
resonant-peak heights at RES 50/100 % across the cutoff range, the ring frequency at five
cutoffs and the static tests' log-spectral distance.

**Service trimmers.** The plugin's SERVICE view turns the six trimmers, each as a rotation
(0-1, a linear track) wired so clockwise raises what the service notes measure with it
(`core/trims.h`). Their defaults are the unit: every adjustment on spec, TM3 at the fitted 2.5 ms.

| Trimmer | In the circuit | The model | Travel |
|---|---|---|---|
| TM1 TEMPO | R18 + TM1 beside the TEMPO pot in the IC6 oscillator | full-clockwise period × (0.6 + 0.8 p) | 5.0-11.7 ms at TP1 (assumed: ±40 %) |
| TM2 INT CLOCK | the CPU's interrupt oscillator | 1.8 ms × (0.6 + 0.8 p) | 1.08-2.52 ms (assumed: ±40 %) |
| TM3 FREQ | 470k rheostat in series with R73 100k into the cutoff expo's reference | ring ∝ 1/(R73 + TM3), 2 ms at mid travel (assumed) | 0.6-3.4 ms ring |
| TM4 TUNE | wiper between 5.333 V and +12 V, through R102 100k into the expo's reference collector, beside R103 220k from +12 V; 47k, its wiper resistance loading R102 | pitch ∝ I_ref, 43 µA = A at 110 Hz | 30-95 µA: −645 to +1369 cents |
| TM5 WIDTH | 4.7k rheostat in series with R106 27k from the CV into the expo's base, against R100 560R ∥ R118 100k | octaves per volt = (R100∥R118)/(R100∥R118 + R106 + TM5)/(V_T ln 2), pivoting about the 5.333 V reference (2.583 V above A) | 0.881-1.032 V per octave |
| TM6 CV | the 5.333 V reference (IC16b), which the DAC and +12 V follow | CV OUT and the expo's CV and reference scale together: width × k, pitch × k at the reference | ±5 % (assumed) |

TM5 turns the pitch about the reference level, not about A, so widening the octave also lowers
A: the notes set WIDTH first, then TUNE for 110 Hz, and a unit trimmed so has its octave pivot
at A. What isn't known: which trimmer TM1 and TM2 are (47k or 220k), TM6's range (the IC16b
network isn't worked out), and where in TM3's travel 2 ms falls; those ranges are assumptions.
TM6 moves only the pitch side (CV and the VCO's reference currents), not the VCF and VCA
biases that also hang off the reference. At their defaults the trims give the voice exactly the
calibration above (the plugin test checks A = 110 Hz and 2:1 to 10⁻⁵, and each trimmer's
effect against `core/trims.h`).

The view's instruments measure the running model: the counter reads the VCO, the meter CV OUT
(TM6's volts), and the scope taps the WAVEFORM switch's common (the saw at Q28's source, or the
square), TP6 (the VCF output, AC), TP1 (the tempo clock) and TP2 (the interrupt pulses, low for
0.15 ms). It triggers on the saw's reset (TP6 too), TP1's rise or TP2's fall, and times what the
notes ask: the period, the octave between the last two notes held an octave apart, A, or the
ring on TP6 (the median period between rising zero crossings after the reset, high-passed at
200 Hz, as `tools/service/check.py` does): 2.57 ms at the unit's 2.5 ms trim.

**Loop stability.** The fit also rejects any calibration that is unstable over a long run.
Q21's r_π sets C25/C27's corner: about 86 Hz with the service-notes values. A high β or a low
tail current raises r_π and pulls that corner down, to 29 Hz at β 1000 / 0.44 mA. The loop
then keeps gain at a few Hz, where its four high-passes (C25, C14, C15, C23) turn the
feedback positive. At high resonance a ~6 Hz oscillation builds up over several loops and
modulates the filter, which is audible as "bubbling". The short static tests don't show it.
`specs/stability.json` plays eight loops of an acid line at high resonance, and the energy
under 12 Hz after the first loop must stay 30 dB under the total. Every calibration within
the parts' ranges passes (β up to 435; earlier fits outside them, β 1000 / 0.44 mA, did not).
The plugin test runs the same check through the plugin for both calibrations.

**Gate conditioning.** Q37 pulls Q36's base down through R143 against R142 with C72 10 µF to
the rail; Q36 is modelled with its junction (I_S 3.3e-14, β 300, β_R 5), its base resistance
and R147 22 Ω in the emitter, solved by Newton. C72 takes ~5 ms to reach turn-on (the
note-start delay). After gate-off Q36 stays saturated for a few ms (C54 charging through R143
keeps pulling its base for ~1.5 ms), then GATE_INT falls, slowed by R147's degeneration: the
VCA current is under 10 % ~13 ms after gate-off, as SPICE gives (12.3–14.9 ms across the parts'
spread) and Whittle measured (~16 ms). A Q38 one-shot (~1.3 ms) fires on each rising gate edge
only, so tied and slid notes retrigger nothing.

**MEG.** C62 1 µF charged to ~11.2 V through D37/R152 during the pulse, discharged through R138
68 k + DECAY (1 M A) into the D28/D27/D26 string (with R117's bias), buffered by the Darlington
(two V_BE, floor at 0 V): τ 68 ms to 1.07 s. On accented notes IC12-B shorts DECAY.

**Accent.** IC12-A passes the MEG to ACCENT (50 k B) only on accented notes. The wiper node is
solved implicitly against its two loads: R119 47 k / C36 33 nF / R120 22 k / D27 into the VCA
bias input, and D24 / R46 47 k into the sweep network on the RESONANCE pot's second gang with
C13 1 µF, whose wiper feeds the cutoff summing node through R72 100 k.

**VEG and VCA.** The pulse charges C41 through D36 and R134 and Q32 (Ebers-Moll, R130 and R132
as a collector-current limit) empties C42 1 µF; after the pulse C41 discharges only through
Q32's base, so Q32 keeps sinking a few µA for ~100 ms against R123 1.5 M, and the VEG starts to
fall about half as fast as τ 1.5 s alone. Q31 (Ebers-Moll, so it can saturate) is fed about
(GATE_INT − D35 − V_E)/222 k; its collector and the accent current through D27 meet at the
BA662's I_ABC input (pin 1: a diode-connected NPN with ~450 Ω in its emitter, ~0.7 V). The node
is solved each control step: early in a note Q31's base is below pin 1, its base-collector
junction conducts, and part of the accent current goes into C42 instead of the VCA. Both C41 and
C42 step implicitly. The BA662: 0.985·I_ABC·tanh(v_eff/2V_T) into ~52.5 k, where
v_eff = v_in/(1 + I_ABC/2.5 mA) (the pair's base currents through R124/R125 and its emitter
resistance), with the input from VCF OUT through R121 220 k + C21 10 nF and from the resonance
wiper through R122 100 k + C22 10 nF. Its output swings −4.5 V and up to what the buffer's 10 k
pull-up can drive into the VOLUME load (+4.7 V at full VOLUME, +5.5 V lower down), with sharp
knees (`tools/spice/ba662.lib`, from the reverse-engineered chip and its clones' data sheets).

**Output.** C38, VOLUME (50 k A) into R155 47 k, the Q33/Q34 mixer, DC blocking. Q34's emitter
is not quite a virtual ground (~3.4 k at its few µA), so the mixer gains 1.98, not 2.13. Its
output idles at ~4.1 V on the 11.8 V rail and clips asymmetrically, ~6.9 V above and ~4.25 V
below (hard-knee limiters).
2.72 V peak at the OUTPUT jack is digital full scale, which matches Roland's software's level
at the same VOLUME (mean over the comparison suite).

## Validation

### Service notes

`tools/service/check.py` runs every adjustment and specification the service notes give
against the emulation, pass or fail (16 of 16 pass):
- TEMPO full clockwise: clock period 8.33 ms (notes: 8 ± 1 ms); TEMPO 40-300 BPM; internal
  clock 1.8 ms (notes: 1.8 ± 0.2 ms, set to nominal).
- CV, through the firmware in PATTERN WRITE pitch mode: low C to high C 1.000 V, and 1.000 V
  more with TRANSPOSE UP (notes: 1.000 V ± 3 mV); DAC codes 23, 35, 47.
- VCO: A = 110.03 Hz at TUNING centre; low C : high C 2.0006 : 1; with TRANSPOSE UP 4.0013 : 1
  (notes: 4 : 1 ± 0.5 %); TUNING ±700 cents (notes: approx. ±700 cents).
- VCF adjustment (C key, saw, CUT OFF centre, RES max, the rest min): ring period 1.97 ms
  (service-notes calibration) / 2.41 ms (Roland), within 2 ± 0.5 ms.
- MEG: 173 ms to 2.47 s to 10 %; the notes print 200 ms to 2.5 s.
- Gate: 3 of 6 clocks per sixteenth (firmware), held across ties and slides.

### SPICE (the schematic)

Every block of the voice was simulated in ngspice from the transcribed schematic
(`tools/spice`, see its README) and run against the same block of the model with the same
input. Each comparison was made with device models set from the parts' data sheets at typical
values and at both ends of their spread (gain, V_BE, the JFETs' I_DSS), and for the VCO also
with an independent set of published models; a result counts only if it holds across them.
The BA662, which has no data sheet, is a transistor-level model (`tools/spice/ba662.lib`): the
circuit Open Music Labs reverse engineered from an original chip, with the values of Alfa's
AS662 clone; its gm at 500 µA (8430 µS) lands in the clone's published range. Its unknowns
(transistor gains, emitter resistors, pull-up) are run at both ends too. Typical-part results:

| Block | Compared | Model against SPICE |
|---|---|---|
| Pitch DAC and slide | CV for all 64 codes; slide time constant | exact; 22.00 / 22.04 ms |
| Saw core and Q28 | levels, reset | 10.85-5.60 V / 10.85-5.61 V (spread 10.65-10.87 top, 5.34-6.08 bottom); reset ~1 µs |
| Q8 square shaper | levels, duty cycle, level, C1-C5 | within 0.03 dB (±0.3 dB across the spread), same duty; 4-8 kHz content +2.8 dB at C5 (junction capacitances soften the real edges) |
| VCF, small signal | gain VCO side of R62 to VCF OUT, 4 cutoffs × RES 0/0.5/0.85 | passband within 0.0-0.5 dB (0.0-0.1 at RES 0); resonant peak within 0.2-0.5 dB, its frequency within ~6 %; ≤ 2.5 dB anywhere 30 Hz-8 kHz except the lowest cutoff's bass |
| VCF, saw and square | C2/C4 × 3 cutoffs × RES 0/0.5/1: level, spectrum | levels within ±0.8 dB; spectral distance ≤ 0.5 dB except full cutoff with full resonance (~3.5 dB, the self-oscillation threshold) |
| VCF, full resonance | ring after a kick: decay rate at cutoff 0 / 0.5 / 0.75 / 1, and what is left after 300 ms | −242 / −245 / −247 / −184 against −251 / −249 / −242 / −174 dB per 100 ms; neither self-oscillates with any parts in the spread. With Q21 at the top of its F rank (gain 435) SPICE's top of the range decays only ~5 dB per 100 ms, ours 26 |
| Gate conditioning | note-start delay (first note, later notes); release (VCA current under 10 % after gate-off) | 6.7 / 7.0 ms, 5.8 / 5.8 ms; 13.8 / 14.1 ms (SPICE 12.3-15.3 ms across the spread) |
| MEG | peak; fall to 10 % at DECAY min / mid / max | 10.00 / 10.04 V; 173 / 173, 530 / 530, 2469 / 2437 ms |
| VEG | VCA current peak; 240 ms into the note | 47.0 / 47.2 µA; 43.2 / 43.3 µA |
| Accent | VCA current boost at ACCENT half / full | +4.1 / +4.1 dB; +8.9 / +9.0 dB (SPICE +3.7 to +4.7, +8.3 to +9.8 dB across the spread) |
| Accent sweep | wiper excursion at RES full / 0 | 1.09 / 1.08 V; 2.53 / 2.63 V |
| Cutoff converter | ladder current at rest; full env mod peak; accented, RES 0 | 18.0 / 18.2 µA; 49.8 / 49.3 µA; 145 / 146 µA |
| VCA input, output, VOLUME | gain VCF OUT to output at 30 Hz / 100 Hz / 1 kHz, RES × VOLUME | within 0.0-0.4 dB (bass roll-off, resonance compensation, VOLUME law) |
| BA662 gain | gain against I_ABC, 5-300 µA | within 0.11 dB (at the ends of the chip's unknowns SPICE is 0.8 dB lower to 0.13 dB higher, 5-170 µA) |
| BA662 swing | pin 8 peaks driven hard, VOLUME 0.3 / full | +5.46 / −4.50 V against +5.54 / −4.50 V; +4.69 / −4.50 V against +4.97 / −4.50 V |
| BA662 pin 1 | voltage at 5-300 µA | within 1 mV (0.60-0.84 V) |
| Output clipping | peaks at full VOLUME, driven hard | +6.64 / -4.24 V against +6.71 / -4.14 V |
| **Whole voice** | the four netlists joined into one circuit (only the pitch expo is a fixed current), against the model through the plugin's own interface: 4 scenarios, 9 notes (plain, accented, saw and square, full resonance, a 1 s note) | note level within 0.1-0.3 dB; harmonic levels within 0.2-1.4 dB RMS; waveform, VCO phase lined up, −11 to −25 dB below the signal; DC step at note start within 1-6 mV (SPICE's BA662 adds its own: below) |

What the simulations changed in the model: the saw's levels and reset; the resonance loop's
real losses (~12 % less loop gain, so it no longer self-oscillates at full cutoff where the
circuit doesn't); Q36's emitter and base resistance and C54's pull after gate-off (the release
went from ~6 to ~13 ms); the converter's V_BE11 and node E (the env-mod peak was 12 % high); the
mixer's gain and its asymmetric clipping. The service-notes calibration now uses the
schematic's own operating point (Q21 tail 0.52 mA from its bias network, typical β 300).
The BA662 model then changed the VCA (gain 0.26 dB under the ideal at a plain note, 0.7 dB on
accents; swing −4.5 V / +4.7-5.5 V instead of an assumed ±4.6 V) and, through pin 1's voltage,
the VEG: Q31 saturating early in each note, Q32 letting go of C42 slowly. The accent boost fell
from +5.7 to +4.1 dB at half ACCENT, the note start moved from 5.3 to 6.7 ms (SPICE 7.0), and the
VEG decays more slowly over its first few hundred ms.

The whole-voice run (`compare_voice.py`) shows that nothing is lost between the blocks: the VCO,
filter, envelopes, VCA and output load and drive each other in the model as on the board. Its
one systematic difference is the DC step at each note's start: SPICE's BA662 shifts its output
by +4.3 mV at a plain note's I_ABC and +14 mV on an accent (its mirrors' base currents don't
cancel: ~90 µV of input offset), which the output stage turns into a −2 to −8 mV step. Real
chips have their own offsets (30-250 µV, either sign, measured), so this step is unit-specific;
the model's BA662 has none.

What it settles: the schematic gives about +4 dB of accent at half ACCENT (Roland's software
gives +11.5 dB), loses level at the same rate as the model as resonance rises (−2 dB to
−23.5 dB at 100 Hz between RES 0 and 0.85, at full cutoff), and puts full resonance just below
self-oscillation with typical parts. Where the model now differs from Roland's software on
these, it is Roland's software that differs from the schematic.

### Roland TB-303 software

The figures in this section and the next were measured with the two calibrations the plugin had
before the fit to a real unit: "service notes" (2.0 ms ring, Q21 β 300) and "Roland" (fitted to
Roland's software: 2.47 ms, β 435). The fitted unit that replaced them is 2.8 dB from Roland's
software over the suite; its comparison with the real unit is in "Real TB-303 single notes".

61 tests (`tools/compare/specs/suite.json`, `calibration.json`) rendered through Roland's
plugin in Onyx Tracker with KEYBOARD on, POSITION LOCK off, the "Simple Saw" patch (no drive
or delay), and through our model, with identical MIDI and pot positions. `compare.py` aligns
onsets, matches gain and reports log-spectral distance (LSD), centroid ratio and pitch.

LSD and level (ours − Roland) with each calibration:

| Group | Tests | LSD, service notes | LSD, Roland | Level, Roland |
|---|---|---|---|---|
| static filter, saw (cutoff × resonance) | 15 | 2.9 dB | 1.4 dB | +0.1 dB |
| static filter, square | 3 | 3.0 dB | 2.2 dB | −2.4 dB |
| raw oscillator, C1-C5 saw and square | 10 | 3.6 dB | 2.8 dB | −0.8 dB |
| filter envelope (decay × env mod) | 6 | 2.3 dB | 1.3 dB | +1.2 dB |
| accent (single, runs) | 9 | 3.2 dB | 2.5 dB | −0.8 dB |
| slide | 3 | 3.0 dB | 1.1 dB | +0.2 dB |
| VCA long note | 1 | 2.2 dB | 1.1 dB | −0.3 dB |
| acid lines, saw and square | 2 | 4.6 dB | 4.1 dB | −0.4 dB |
| all 56 | 56 | 3.3 dB | 2.2 dB | −0.2 dB |

The VCA long note went from 2.3 to 1.1 dB with the BA662 work: Roland's software also has the
VEG's slow first few hundred ms that Q32 gives.

Details:
- Pitch: within ±7 cents over five octaves; Roland's unit is scaled about 0.3 % wide.
- Cutoff law: ring frequency relative to cutoff centre, knob 0/0.25/0.5/0.75/1: Roland
  0.50/0.66/1/1.87/4.25, ours (Roland calibration) 0.51/0.63/1/1.86/4.08. Absolute: Roland is
  trimmed to 2.5 ms (399 Hz); ours is 1.97 ms (507 Hz) with the service-notes calibration, 2.41
  ms (416 Hz) with the Roland one, and then within 6 % of Roland's ring at every cutoff.
- Resonance (height of the strongest harmonic, weighted by n, over the fundamental), ours /
  Roland's at RES 100 %:

  | Cutoff | 0 | 25 % | 50 % | 75 % | 100 % |
  |---|---|---|---|---|---|
  | service notes | +6.9 | +11.1 | +15.0 | +20.0 | +24.9 dB |
  | Roland calibration | +7.4 | +7.0 | +14.9 | +18.9 | +24.8 dB |
  | Roland's software | +11.0 | +8.1 | +15.7 | +18.9 | +22.8 dB |

- Note start: with nominal parts the VCA current reaches half its peak ~5.8-6.7 ms after the gate,
  as the schematic gives (SPICE 5.8-7.0 ms); the fitted unit opens faster (below, "The attack").
  Roland's sound begins 5.5-5.8 ms after the gate.
- Release: the VCA current is under 10 % ~14 ms after gate-off, as the schematic gives
  (Whittle measured ~16 ms on a unit); Roland's takes ~20 ms.
- Accent: half ACCENT boosts the VCA current by +4.1 dB (schematic +4.1 dB, +3.7 to +4.7 across
  the parts' spread); Roland's software boosts ~+11.5 dB, so accented notes at half ACCENT are
  ~6.5 dB quieter than Roland's.
- VOLUME at full is ~4.5 dB louder than Roland's; the model's VOLUME law matches the schematic's
  within 0.1 dB.
- TUNING: ±700 cents, as the service notes specify; Roland's software spans ±1 octave, so the
  test at TUNING's end is the suite's worst (LSD ~12 dB, a different pitch).
- Aliasing: inharmonic energy above 5 kHz on a 523 Hz saw is below −110 dB (Roland −83 dB).

### Real TB-303 recordings (acidvoice.com shootout)

The clips have no published patterns or knob settings except three accent tests
(`private/reference/shootout/index.md`), so they are used for behaviour rather than sample matching.

- Resonant peak at CUT OFF 60 %, RES 100 %: ~700 Hz in the real clip, ~700 Hz in ours with the
  service-notes calibration (Roland's software ~530 Hz, ours with the Roland calibration ~550 Hz). At CUT OFF 100 % the real unit reaches ~3.3 kHz, above both models (unit
  trim and pot tolerance).
- Note end: the real unit drops ~25 dB within ~10 ms of gate-off and leaves a low-frequency
  step at −40 to −55 dB that decays over tens of ms. Ours has the same step (the VCA's DC shift
  through the output coupling) but now releases a few ms more slowly, as the schematic and
  Whittle's measurement give; Roland's decays more slowly still and has less of the step.
- Square: the same sharp falling edge, sloped top and soft rising edge as ours.

### Real TB-303 single notes (Din Sync reference set)

Din Sync's reference recordings for x0xb0x builders (2010, `private/reference/dinsync/index.md`):
400 low Cs from one real unit, 16-bit/44.1 kHz, in 25 sets. Each set sweeps one of CUT OFF,
RESONANCE, ENV MOD, DECAY and ACCENT through 25/50/75/100 % with the other four at 0, 25, 50, 75
or 100 %, plain and accented, saw and square. The unit's trims, TUNING and VOLUME aren't stated
and the knob positions are approximate (0 and 100 % aren't). `tools/compare/dinsync.py` renders
the same notes (168 distinct settings) through the model and Roland's software and compares every
note; the same setting recorded in several sets gives the real unit's own repeatability.

**The unit is fitted to them** (`tools/compare/dinsync_fit.py`, `tb303::unitCalibration()`). The
score per note is the log-spectral distance on sixth-octave bands, the level error and the
resonant-peak error (200 cents counting as 1 dB), over all 400 notes; the search keeps each value
within its part's tolerance. The real unit against its own retakes scores 2.39.

| | Nominal (service notes) | Fitted | Range searched |
|---|---|---|---|
| TM3: ring at cutoff centre | 2.0 ms | 2.5 ms (the slow end of the tolerance) | 1.5-2.5 ms |
| Q21 β | 300 | 385 | 120-700 |
| MEG peak | 10 V | 9.3 V | 8-10.6 V |
| VR6 DECAY | 1M | 860k | 0.6-1.4M |
| C62 | 1 µF | 1 µF | 0.5-1.3 µF |
| CUT OFF, ENV MOD (50k A, one part) | exponential, 15 % at mid | two segments, 21.5 % at mid | |
| DECAY (1M A) | exponential, 15 % at mid | two segments, 14 % at mid | |
| BA662 pin 1 against Q31's V_EB | 0 | −80 mV | fitted to the attack (below), not the score |
| BA662 output swing | ±4.5-5.4 V | a fifth: ~±1 V | fitted to the accents' peaks (below) |
| VR4-B (RESONANCE's accent-sweep gang, 50k B) | 50k | 37.5k | fitted to the accent sweeps (below), not the score |
| Score | 9.76 | 4.89 | (Roland-fitted calibration: 7.11) |

The pot laws carry most of it. With the nominal parts every model is an octave too bright with
CUT OFF, RESONANCE, ENV MOD and DECAY at 100 % (e.g. the resonant peak at 0.4 s: real 5.2 kHz,
nominal 10.3 kHz), mostly from DECAY's top end: going from 75 to 100 % raises the late cutoff by
0.57 octave on the real unit and by 1.5 octaves with an exponential 1M taper. Real A-taper
carbon pots are two straight resistive segments meeting at mid rotation; with that law and these
values the notes at 25, 50 and 75 % no longer show the S-shaped error (too bright at 25-50 %,
too dark at 75 %) the exponential law leaves.

Mean difference from the real unit (saw / square):

| | LSD | brightness 0-0.1 s | brightness 0.6-1.2 s | resonant peak | level |
|---|---|---|---|---|---|
| Real unit, retake | 2.5 / 2.2 dB | 41 / 60 c | 33 / 30 c | 95 / 43 c | 0.1 dB |
| Fitted unit | 3.0 / 2.9 dB | 375 / 392 c | 184 / 174 c | 145 / 135 c | 0.8 dB |
| Roland's software | 3.3 / 3.3 dB | 718 / 812 c | 374 / 326 c | 385 / 333 c | 1.8 / 1.6 dB |
| Nominal parts | 4.6 / 4.7 dB | 495 / 508 c | 406 / 378 c | 619 / 513 c | 1.1 / 1.0 dB |

Note by note the fitted unit is closer to the real one than Roland's software on 63 % of the
notes for the spectrum, 76 % for early brightness, 84 % for late brightness, 88 % for the
resonant peak and 76 % for level. Median errors: LSD 2.7 dB (Roland 2.9), late brightness 9 %
(23 %), resonant peak 6 % (22 %), level 0.4 dB (1.4 dB). What remains: brightness in the first
100 ms (~25 %), and the sets with the other knobs at 25 % (resonant peak ~250 cents high).

**The attack.** Measured against the same point of the waveform three periods later (so the wave
shape cancels), the real unit's plain notes are within 1.3 dB of full level 1 ms after the sound
starts (16 notes with a still filter); with nominal parts ours was still 7.7 dB down then and took
~8 ms. acidvoice.com's unit opens as fast as Din Sync's. The slow climb is the schematic's: SPICE
gives it too (I_ABC 27 % of its peak 1 ms after it starts to rise, 83 % at 5 ms, with typical
parts), from Q31 starting each note saturated with C42 near 0 V and its base-collector junction
taking the emitter current until C42 has charged. How deep it saturates rests on pin 1's voltage
against Q31's V_EB, the two least certain figures in that path; 80 mV less (both within their
spreads) fits the real attack (RMS error over the first 5 ms 4.1 dB → 1.7 dB). The sound still
starts ~6 ms after the gate (Whittle: "a typical 4 ms delay and then a 3 ms attack") and now
reaches half level 1.7 ms later instead of 7.5 ms; the release, the accent's level (within 0.05 dB)
and the score are unchanged.

**The VCA's swing.** Both real units flatten their loudest accents: over the first 100 ms of a note,
peak against RMS is 13.5 dB on Din Sync's accented notes at full ACCENT and was 16.5 dB in ours,
and the deepest (negative, at the jack) spikes of an accent's first cycles stand 10.9 dB over its
positive peaks against 15.1 dB in ours; acidvoice.com's accents are 4-5 dB less peaky than ours
too. Plain notes match (13.1 / 13.1 dB at ACCENT 50 %), and so do accents at half ACCENT, so it
grows with the accent's level, after the VCA's gain: the VCO and the filter don't know a note is
accented. In the circuit as transcribed nothing comes near a limit there (the BA662's output peaks
at ~1.3 V on a full accent, its swing is −4.5 / +4.7-5.4 V; VOLUME doesn't move it), so the real
chip's output saturates far sooner than the reverse-engineered one. A fifth of its swing (~±1 V
about the bias, same knee) fits: Din Sync's accents 14.4 dB (real 14.1, ACCENT 75-100 %), spikes
+10.4 dB (real +10.9), acidvoice.com's accents 5.9 / 12.5 / 12.1 dB at CUT OFF 0 / 60 / 100 %
(real 6.2 / 9.6 / 11.3; ours were 10.6 / 14.9 / 15.7). Plain notes don't reach it; the score
moves from 4.90 to 4.89. Why the chip saturates there isn't known: its output stage as reverse
engineered swings much further. This is also what the comparisons used to put down to acidvoice.com's
recording chain (a soft clip on the plugins); it is the unit's.

**The accent sweep.** Over an accent's first ~40 ms the resonant peak climbs as C13 charges
through D24, R46 and VR4-B (circuit.md §5.5). On acidvoice.com's "303_60" (RESONANCE and ACCENT
100 %, ENV MOD 0), at the settings that match its plain notes (FULL CUT OFF 2.05 kHz, CUT OFF 51 %,
DECAY 65 %, VOLUME 1), every accent topped out 1.3-2.3 semitones under the real unit's: A#2
~1310 Hz against 1420, A#1 ~1410 against 1600, the second of two accents in a row 1680 against
1850. The climb started the same and stopped short. Din Sync's accented notes are short the same
way where the accent's own sweep dominates (ENV MOD or DECAY at 25 %): 200-760 cents over the first
60 ms. A larger C62 (a slower MEG on accents) closes it but slows every short decay too (score
4.97-5.50 at 1.15-1.45 µF). VR4-B 25 % under its 50k (37.5k; carbon tracks are ±20-30 %) charges
C13 faster instead: acidvoice.com's accents now top out within −70 to +30 cents of the real ones
(mean error over each accent's first 60 ms 160 → 68 cents, its plain notes 108 → 86), Din Sync's
accented notes' resonant peak over their first 150 ms 200 → 180 cents, and the score 4.885 → 4.890.
The climb still gets halfway 2-3 ms sooner than the real unit's (10 / 13 ms against 13 / 15 ms on
A#2 / A#1), inside the measurement's resolution of one period.

**A second unit.** acidvoice.com's accent tests (`tools/compare/acidvoice_check.py`): the first
note of each clip holds a steady resonant peak, 702 Hz at CUT OFF 60 % and ~3.3 kHz at 100 %
(RESONANCE 100 %, no env mod). The fitted unit gives 622 Hz (-208 cents) and 1.4 kHz (-1470
cents): Din Sync's unit tops out much lower than acidvoice's (its cutoff sweep with everything
else at 0 matches the fitted unit within 50 cents at every position, including 100 %). Whittle
gives ~2.5 kHz as the typical stock maximum. Units spread this far; TM3 moves the whole cutoff
law, not the top alone, so the FULL CUT OFF control (CUT OFF MAX) sets the top,
1.2 to 3 kHz of ring at full CUT OFF: the CUT OFF pot's share of its track at mid rotation (TM3
holding cutoff centre) that reaches it, from a table of the model's own top against that share
(`fullCutOffHz`), within ~1 % in the running voice. The fitted unit's law (21.5 %) gives 1.35
kHz; Roland's software reaches ~1.8 kHz (14.3 %), the nominal parts ~2.2 kHz (8.7 %), Whittle's
typical ~2.5 kHz (6.4 %), 3 kHz takes ~3 %. Reaching acidvoice.com's 3.3 kHz this way takes an
extreme law; that unit is probably trimmed brighter as well. The plugin ships at 2.3 kHz, a typical
unit's, rather than the fitted unit's 1.35 kHz, which is at the dark end of the units we know of;
the comparisons with Din Sync's unit (and `tb303render`, which starts from the fitted unit) use
1.35 kHz.

A lower share at mid steepens the whole upper half of the pot, not only its end: at 2.5 kHz,
CUT OFF 60 % rings ~1040 Hz on acidvoice.com's "303_60" settings, where that unit's resonance
sits at ~700 Hz (Roland's software: 703 Hz; ours at CUT OFF 54 % matches it). That unit is like
Din Sync's at 60 % but reaches more than twice as high at 100 %, which no single law of this kind
gives; comparisons with it set CUT OFF where its resonance is.

- Pitch: the real unit plays C2 + 5 cents (TUNING not stated); the comparison leaves TUNING centred.
- Against Roland's software the suite's mean LSD is now 2.8 dB (2.2 with the calibration that
  had been fitted to it).

## Open points

- The BA662 is modelled from Open Music Labs' reverse engineering of an original chip and its
  clones' data sheets (Alfa AS662, Rohm BA6110), not Rohm's own data. Its least certain figure is
  pin 1's diode drop (±0.04 V), which sets how much accent Q31 diverts (±0.5 dB) and, against
  Q31's V_EB, how fast the VCA opens: the unit takes it from the real attack. Its output swing
  is fitted too (a fifth of the reverse-engineered chip's), and the reason isn't known. Q32's β (the
  2SC945's rank is not known) sets how long it holds the VEG.
- TM3, Q21, the MEG peak, DECAY's pot and the pot laws come from the fit to Din Sync's unit; the
  other trims are on the service notes' spec. SERVICE turns all six; the ranges of TM1, TM2 and
  TM6, and where 2 ms falls in TM3's travel, are assumptions ("Service trimmers").
- One documented unit is not the spread: a second set of recordings at known settings (above
  all full CUT OFF with resonance and no env mod) would show how typical Din Sync's unit is.
- **Accented low notes have a notch the model doesn't.** On both real units, accented notes low
  in CUT OFF with no resonance lose a band around 260-330 Hz: acidvoice.com's accented A#1s at
  CUT OFF 0 (eight notes, all alike) have their 5th harmonic (291 Hz, a D) 16 dB under ours and
  the 10th-11th 7-8 dB under, against the 3rd, 6th and 12th (Fs) within 2-6 dB: ours has a D in
  it where the real one has an F. Din Sync's accented C2 at CUT OFF 0 dips at the 4th-5th harmonic where ours
  dips at the 6th. Plain notes match, and so do accented A#2s, whose harmonics fall between the
  notches. Notches ~300 Hz apart point at something about 3 ms long in every cycle of an accent,
  or a once-a-cycle wobble of the VCA's gain, on accents only. Nothing in the circuit as
  transcribed is that strong: the BA662's control mirror sees the input signal at ~0.05 % of
  I_ABC, the 5.333 V reference and the 12 V rail are regulated and decoupled, and the accent
  network (C36 with R119, R120: 0.5-1.5 ms) is fed only from the MEG. Tried and ruled out: the
  filter's cutoff and resonance (matched), a hotter VCO into the ladder, BA662 input overdrive
  (squashes plain notes 1-3.6 dB), the BA662's swing (fits the accents' peaks, not this), IC12-A's
  on-resistance (2 kΩ fixes the accent's brightness at full ACCENT, +98 → +20 cents, but not the
  notch, and the score gets worse). Wanted: recordings of single accented low notes, CUT OFF 0,
  RESONANCE 0, ENV MOD 0, ACCENT 25/50/75/100 %, at two or three pitches, from any unit.
