# RAT2 model and validation

How `src/rat/` models the pedal, and how it was checked. The circuit and its sources are in
`docs/circuit.md`; the literature and the reasoning per block are in `docs/research.md`.

## Rates

The pedal runs at 8x the host rate at 44.1/48 kHz, 4x at 88.2/96 kHz and 2x above (`unit.cpp`).
The minimum-phase IIR half-band chain from the Aphex project does the resampling: no latency,
flat to 20 kHz. Hard diode clipping after up to 67 dB of gain needs the headroom.

## Solver

The whole circuit, from the input jack to the VOLUME wiper and the load, is one netlist
(`Pedal::build`). It is solved with the nodal DK method (Yeh 2009; Holters & Zölzer, DAFx-11),
in `dk.h`/`dk.cpp`:
- Each capacitor is discretised with the trapezoidal rule: a conductance 2C/T plus a history
  current, which is the state.
- The MNA matrix is inverted once per knob position. That reduces the circuit to
  `v = D z + E u + F i(v)` in the five voltages the nonlinear devices sense, plus linear updates
  for the outputs and the 11 states.
- Newton solves the nonlinear part every sample. No node is updated explicitly, so the op amp
  inside a loop with up to 67 dB of gain cannot blow up.
- The JFET's drain current does not reach anything upstream of its gate, so the system is block
  triangular. It is solved as a 3 x 3 block (op amp input and output stages, diode pair), then a
  2 x 2 block (JFET).
- Newton starts from a linear prediction, accepts a step under 0.1 µV, and corrects the currents
  to first order. That averages 2.2 iterations per sample with no failures across the stress and
  stability runs. Each Newton step solves its 3 x 3 or 2 x 2 linear system in closed form (the
  adjugate over the determinant), which keeps the chain of dependent operations short.
- Knob moves are smoothed over about 8 ms and recompiled every 64 samples while moving.
- The two channels of a stereo instance are solved together, their Newton iterations
  interleaved, so the processor overlaps them; while both sides have had the same input (a mono
  source), the left channel does the work for both. Either way each side comes out exactly as it
  would alone (`tools/ratgolden --pairs`, the plugin test's stereo check).

Cost, on one Apple Silicon core at 48 kHz: about 7.6 % per channel; a stereo instance about 7 %
with the same input on both sides and 11 % with different sides. Optimising it (2026-10) changed
the output by under −239 dB against renders from before (`tools/ratgolden`), and left every
metric of `compare.py`, `compare_scope.py` and `spice_check.py` the same.

`ratrender --netlist` exports the same circuit to ngspice, with the devices as behavioural
sources using identical equations. `tools/compare/spice_check.py` drives both with the same
signals:

| Case (Datasheet calibration) | Output error vs ngspice | Op amp output error |
|---|---|---|
| 1 kHz, 50 mV, DIST 0 | −87 dB | −65 dB |
| 1 kHz, 50 mV, DIST 5 | −77 dB | −78 dB |
| 1 kHz, 1 V, DIST 10, FILTER 0 | −55 dB | −67 dB |
| 1 kHz, 1 V, DIST 10, FILTER 10 | −67 dB | −67 dB |
| 100 Hz, 0.3 V, DIST 5 | −73 dB | −74 dB |
| Sweep 50 Hz–12 kHz, 0.3 V, DIST 7 | −46 dB | −45 dB |
| 2 kHz bursts, 0.5 V, DIST 10 | −54 dB | −63 dB |

The small-signal response (`Pedal::smallSignalDb`, `ratrender --response`) matches ngspice's AC
analysis of the same netlist to 0.01 dB.

## Devices

- **LM308.** A Boyle-style macromodel inside the MNA, as `docs/research.md` §2 recommends:
  - The input pair is `i = It tanh(vd / 2Vte)`, charging the 30 pF compensation capacitor at the
    integrator node Y.
  - Ry sets the DC gain (110 dB).
  - The output stage follows Y between limits about 1 V from each rail, with a quadratic knee and
    a Norton output resistance.
  - Y is clamped just beyond the output limits (anti-windup). Recovering from that clamp is the
    op amp "sticking" after hard overdrive.
  - Slew rate and gain-bandwidth set the tail current and the pair's effective Vt together:
    `It = SR·Cc` and `gm = 2π·GBW·Cc`. Large-signal and small-signal behaviour therefore come from
    the same two numbers, as on the real chip.
  - Edges at guitar levels are limited by GBW times overdrive, not by the hard slew limit (the
    research's main finding). This structure reproduces that.
- **1N4148 pair.** `Is (e^(v/nVt) − e^(−v/nVt))` behind the 0.568 Ω series resistance, from the
  LTspice model (Is 2.52 nA, N 1.752).
- **2N5458.** Shichman-Hodges, LTspice parameters (β 488.9 µA/V², Vto −2.882 V, λ 3.167 m). The
  operating point, Vs = 2.216 V, matches ngspice with the full LTspice model.
- **Pots.** All three are 100k audio taper. The exponential law is `(b^x − 1)/(b − 1)`, with the
  fraction at mid rotation as the trim (Alpha "15A" = 0.15). Each section has a 20 Ω floor.

`tools/compare/spice_vendor.py` compares the compact device models with the vendors' SPICE
models in `private/reference/spice/rat2.cir` (LTC LM308 macromodel, LTspice 1N4148 and 2N5458). The
diode clip levels agree to 2–6 mV, and the op amp's loaded swing limits to about 0.1 V (1.1–7.9 V
against 1.0–8.0 V). The waveforms differ by −12 to −37 dB. Most of that is the op amp's speed: LTC's
model (0.58 MHz, 0.2 V/µs) is slightly slower than the datasheet's typical figures. The NAM fit
decides between them (below).

## Calibrations

Two calibrations ship (`pedal.cpp`); the plugin's CAL switch picks one.

| Trim | Datasheet | Captured (fitted) |
|---|---|---|
| LM308 slew rate | 0.25 V/µs (NSC typ) | 0.312 V/µs |
| Slew asymmetry (rising ×(1+a), falling ×(1−a)) | 0 | −0.45 (fit bound) |
| LM308 gain-bandwidth | 0.5 MHz (NSC curves, 30 pF) | 0.572 MHz |
| Output swing, op amp pin | 1.0 … 8.0 V, Ro 50 Ω | 1.29 … 8.40 V (fit bound), Ro 111 Ω |
| 1N4148 emission N | 1.752 | 1.808 |
| DISTORTION taper at mid | 0.15 | 0.137 |
| FILTER taper at mid | 0.15 | 0.205 |
| 2N5458 Vto / IDSS | −2.88 V / 4.06 mA | −1.28 V / 4.98 mA |

The Captured set comes from `tools/compare/fit_nam.py`, which runs Nelder-Mead over the 24 captures
with stated knob positions (outmoded and stomptones) and five suite signals. Each round is followed
by a scan of stomptones' unknown input level, which came out at about 0 dBu. Every candidate is then
run through the long-run stability specs, which the fitted set passes. The fit's slew rate and
gain-bandwidth sit between the datasheet and the LTC model. That independently confirms the op
amp's speed.

## Validation against real pedals (NAM captures)

The references are neural captures of real RATs from Tone3000 (`private/reference/nam/index.md`),
rendered with NeuralAmpModelerCore at full model size:
- **outmoded:** 20 captures of a RAT2 at stated clock positions, with input and output calibrated
  at +11.5 dBu. This is the main fitting target.
- **stomptones:** 4 captures at stated clock positions; the input level is fitted.
- **romanrecords:** a 7 x 7 grid of a 1990s RAT, input calibrated at +3.98 dBu, knob positions
  unstated. Held out from the fit.
- **jakkwiat:** a 7 x 7 grid of a RAT2, uncalibrated.

The suite (`make_suite.py`) is seven signals at 48 kHz:
- plucked chords loud and soft;
- 220 Hz sine steps from −42 to −6 dBFS;
- a log sweep;
- two tones;
- noise bursts;
- our TB-303 playing an acid line.

`compare.py` aligns each render to the capture (sub-sample, ±12 samples), matches gain, and
reports:
- ESR, NAM's own error-to-signal ratio;
- log-spectral distance (LSD, STFT, 40 Hz–16 kHz);
- spectral centroid difference.

### Results

LSD and ESR against each capture pack. Lower is better; ESR in dB is 10·log10 of the
error-to-signal energy ratio after gain matching.

| Reference | Datasheet LSD / ESR | Captured LSD / ESR |
|---|---|---|
| outmoded RAT2, 20 captures, calibrated (fitted) | 5.77 dB / −14.7 dB | **4.64 dB / −15.9 dB** |
| stomptones RAT2, 4 captures (fitted, level fitted) | 7.62 / −9.9 | 6.52 / −11.0 |
| romanrecords 1990s RAT, 49 captures, **held out**, default positions | 8.90 / −6.8 | 8.04 / −7.3 |
| romanrecords, held out, knob positions fitted per index | — | 6.52 / −8.4 |
| jakkwiat RAT2, 49 captures, uncalibrated, default positions | 7.57 / −9.6 | 6.88 / −10.6 |
| **Aalto recordings of a real RAT** (97 clips of real guitar, level scanned) | 7.01 / −7.8 | 6.93 / −8.7 |

The outmoded results with the Captured set, by signal:

| Signal | LSD | ESR | Centroid |
|---|---|---|---|
| Two tones 330 + 495 Hz | 3.38 dB | −22.1 dB | −2.8 % |
| 220 Hz sine steps −42 … −6 dBFS | 3.63 | −15.1 | −2.5 % |
| TB-303 acid line | 3.93 | −13.3 | −2.2 % |
| Soft plucks | 4.12 | −17.6 | 0.0 % |
| Loud plucks | 4.80 | −16.5 | −2.4 % |
| Noise bursts | 6.35 | −14.8 | +0.2 % |
| Log sweep | 6.23 | −11.6 | −4.8 % |

By DISTORTION, the LSD is 4.2, 4.8, 4.2 and 5.4 dB at 7:30, 9:00, 10:30 and 12:00.

Listening A/B: `private/reference/ab/` (`tools/compare/ab.py`) has ours and the capture side by side, and
alternating in 2 s segments, for the TB-303 line and the loud plucks.

### What the captures taught

- **The RAT clips asymmetrically, and the components as specified don't.** In every reference,
  positive output peaks are 10–16 % larger than negative ones. That covers the four capture packs
  and the Aalto recordings, whose median is 1.10. The datasheet model gives 1.03. The fit gets
  there by making the op amp's falling edges faster and letting its output swing higher; both
  trims end at their bounds, and the result overshoots to 1.22. The physically plausible
  alternative was to hold the op amp to datasheet values and let only the 2N5458 bias move. It
  needed an IDSS of 0.78 mA, below the part's 2 mA minimum, and scored worse everywhere (5.06 dB on
  outmoded, 7.09 dB held out). So the source of the asymmetry is still open (see below); the
  Captured set carries it in the op amp.
- **The op amp's speed is pinned down.** The fit's 0.31 V/µs and 0.57 MHz agree with the
  datasheet curves and with LTC's model. Most edges at guitar levels are GBW-limited, not
  slew-limited, as `docs/research.md` predicted.
- **The pot tapers are close to the 15 % audio law.** The fitted DISTORTION value is 0.14. FILTER
  comes out at 0.21, slightly more resistance at mid than the Alpha 15A law. The romanrecords
  pedal's fitted positions are monotonic, a sanity check.
- **The Aalto pedal wants less gain at low DISTORTION.** On the 13 clips below 25 %, our output is
  much brighter (+75 % centroid). A steeper taper near zero (0.04 at mid) brings those clips from
  6.6 to 3.5 dB LSD. That pedal's pot law or its knob encoding differs from the captured RAT2s',
  so it is not used for the calibration.
- **VOLUME:** at 3:00 our output is about 2.5–3 dB quieter than outmoded's calibrated capture. The
  Aalto clips show the same: the gain we need rises about 5 dB from VOLUME 50 % to 90 %. The real
  pot gives more level near the top than the exponential 15 % law. This affects level only, not
  tone.

## Real-time cost and robustness

- About 7.6 % of one core per channel at 48 kHz (8x), 2.2 Newton iterations per sample, and no
  Newton failures in any test.
- `tools/compare/stability.py` passes for both calibrations. It runs 10–20 s of full-scale low
  sines, squares and bursts at every knob extreme, then 3 s of silence. Every case settles below
  −106 dBFS, with no Newton failures and no runaway.
- The plugin test (`RATPluginTest`) passes:
  - silence;
  - true bypass;
  - the effect of each knob;
  - 20 s of random knob, level, calibration and op amp jumps;
  - 57 s at full distortion for each calibration, then silence;
  - 44.1–192 kHz at block sizes 1 to 4096;
  - mono;
  - state round trip;
  - the view rendering.
- `auval -v aufx Rat2 Nfat` passes.

## Open points

1. **Where the asymmetry comes from.** Candidates the model doesn't have:
   - the LM308's real output stage sourcing versus sinking current into R8 and the diodes;
   - the 2N5458 departing from square law near its operating point (0.22 mA, close to pinch-off);
   - a mismatched diode pair;
   - asymmetric slew larger than the datasheet's pulse response suggests.
   A measurement of a real pedal's op amp output (pin 6) and clip node would settle it. The Aalto
   and NAM data only see the output.
2. **The VOLUME taper:** fit a two-segment law to the Aalto gain-versus-VOLUME data and the
   outmoded level.
3. **The OP07 variant** uses the original part's datasheet figures (0.6 MHz, 0.3 V/µs, about 2.3 V
   from each rail). None of the captures says which op amp it has, so it is unverified.
4. **Not modelled:**
   - the LED and millennium-bypass circuit (not in the audio path);
   - the footswitch click;
   - supply current through R1 (the macromodel draws none);
   - guitar pickup loading (the source is 1 kΩ).
