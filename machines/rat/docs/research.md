# ProCo RAT2 emulation: research digest

Scope: a component-level (nodal DK / MNA + Newton, trapezoidal) model of the RAT2 with LM308, checked
against ngspice and NAM captures. Companion files:

* `docs/circuit.md`: part list, node table, per-value sources, disagreements, version differences.
* `private/reference/spice/rat2.cir`: runnable ngspice netlist.
* `private/reference/spice/tests/opamp_char.cir`: op-amp model characterisation.
* `docs/sources.md`: every saved file with its URL and SHA1.

Source tags (F86, MR, ES, RGK-*, PISO, GGG, AION, PPCB, FC, JHS, MAN) are defined in `circuit.md` §0.
Further tags: **NSC** = National LM108/208/308 datasheet (Dec 1994); **TI308A** = TI LM108A/208A/308A
datasheet; **OP07DS** = TI OP07 datasheet SLOS099H; **LTC** = Linear Technology LM308 macromodel header;
**FSC-D** = Fairchild 1N914/4148 datasheet; **FSC-J** = Fairchild/onsemi 2N5457-9 datasheet;
**CDB** = Chuck D. Bones posts in the PedalPCB threads.

All "ngspice" numbers below were produced in this study with ngspice-47, using the files in
`private/reference/spice/`.

---

## 1. Input network (R12, C3, R4, R5, C4)

### Facts
* Topology is undisputed (F86, MR, ES, all clones): input → C3 22 nF → node `na`; R4 from Vref to `na`;
  R5 1 kΩ to pin 3; C4 1 nF from pin 3 to ground; R12 pull-down at the jack.
* Values: R12 is 2M2 (MR, RAT2) or 1M (F86 1986), and absent on the original RAT. R4 is 2M2 (MR, RAT2) or
  1M (original RAT, ES and all clones). See `circuit.md` D1 and D2.
* ES: C1 (= C3) "blocks DC… harmonics below 7.2 Hz will be attenuated" (with 1 MΩ); "the 1nF cap C2 shunts
  high freqs to ground". ES gives Zin = (R1‖R2)‖(R3 + Zin_LM308) = 494 kΩ.
* MAN (RAT2 spec): "Input Impedance 1 MOhm".
* NSC note 2: the LM308 inputs are "shunted with back-to-back diodes for overvoltage protection…
  excessive current will flow if a differential input voltage in excess of 1V is applied … unless some
  limiting resistance is used". R5 (1 kΩ) is that limiting resistance on the + side. The LTC model
  includes the diodes (DDM1/DDM2).
* Computed in this study: because C4 hangs off the input behind only R5, |Zin| is about 0.92 MΩ at
  100 Hz, 164 kΩ at 1 kHz and 16.7 kΩ at 10 kHz (RAT2 values, ideal op-amp). This is a treble load for a
  passive guitar and is invisible with a low-impedance re-amp source.
* ngspice (LTC model, DIST = 1, FILTER = 0): the op-amp differential input peaks at ±0.092 V (100 mV
  input), ±0.29 V (300 mV) and is clamped at ±0.66 V with 1 V input. In the last case the clamp diodes
  conduct and about 98 µA flows through R5 (versus 1.8 µA at 300 mV). V− is held near Vref by the
  47 Ω / 2.2 µF leg while the op-amp is saturated, so V+ − V− ≈ the input signal. This only matters for
  very hot inputs.

### Models found
* ES: lumped 7.2 Hz HPF and a single Zin figure; no C4 loading.
* Yeh 2009 thesis and DAFx-07: input sections of other pedals as linear filters (bilinear transform).
* BYOD Mouse Drive (Chowdhury, GPL-3): models exactly this network in WDF (CapacitiveVoltageSource C1
  22n, R2 1M to 4.5 V, R3 1k, C2 1n) with an ideal voltage source. `private/docs/web/code/chowdsp_byod_mouse_drive/`.

### Disagreements
R12 (1M / 2M2 / absent); R4 (1M / 2M2 / "220K" in RGK-F); the ES Zin formula versus the
frequency-dependent reality.

### Recommendation
Model the network element by element in the MNA: it is linear and costs nothing. Expose the source
impedance as a parameter:

* `rsrc` ≈ 100 Ω for re-amp or capture matching.
* An optional passive-pickup model (R + L + cable C) for "played through a guitar" realism.

Keep the LM308's input-clamp diode pair (Is about 1e-15 A) as a nonlinear port, or at least check that it
never conducts at the target input levels. Default R4 = R12 = 2M2 (RAT2 trace). Both only shift
sub-audio corners and the input impedance.

---

## 2. Op-amp gain stage (U1, C8, DIST, C5, R6/C7, R7/C6)

### Facts: circuit
* Non-inverting stage. G(s) = 1 + Zf/Zg, Zf = R_DIST ‖ C5, Zg = (R6 + 1/sC7) ‖ (R7 + 1/sC6) (ES).
* Corners: R6·C7 = 47 Ω·2.2 µF → 1539 Hz; R7·C6 = 560 Ω·4.7 µF → 60.5 Hz; R_DIST·C5 → 15.9 kHz at
  100 kΩ (ES).
* Ideal-op-amp maximum gain 1 + 100k/(47‖560) = 2307 (67.3 dB) (ES gives 2305, 67 dB).
* The real gain is GBW-limited. ngspice (LTC LM308 model, small signal, DIST = 1): opout/in is 63.5 dB
  peak at 631 Hz and 58.1 dB at 1 kHz. CDB: "the gain of a Rat peaks out around 60dB and 600Hz". MAN:
  "Gain 60dB". At DIST = 0.5 (15 kΩ, 15 % taper): 47.4 dB at 1 kHz, peak 49.8 dB near 1.6 kHz.
* JHS: the 47 Ω (where Burnham intended 470 Ω) is why "the op amp is actually run at such a high rate
  of gain that it distorts before the hard clipping distortion is ever started".
* The op-amp output is followed by R8 1 kΩ + C9 into the diodes. When they conduct, the op-amp drives about
  1 kΩ (CDB). The op-amp itself runs into its output limits at moderate DIST settings.
  ngspice, DIST = 0.5, 100 mV at 1 kHz: opout swings 1.02–8.01 V, i.e. rail-limited.

### Facts: LM308 (NSC 1994, LM308 column, ±15 V)
* Vos 2 mV typ (7.5 max); Ib 1.5 nA typ (7 max); Ios 0.2 nA; Rin 40 MΩ typ (10 min); Is 0.3 mA typ
  (0.8 max); Avol 300 V/mV typ (25 min); output swing ±14 V typ / ±13 V min at RL = 10 kΩ; CMRR and PSRR
  100/96 dB typ.
* Compensation: standard circuit with Cf between pins 1 and 8, "Cf ≥ R1·Co/(R1+R2), Co = 30 pF";
  "**Bandwidth and slew rate are proportional to 1/Cf**". 30 pF is the datasheet's standard value; ES
  calls it "the typical and suggested value".
* Internal schematic: `private/docs/datasheets/LM308_NSC_p3_internal_schematic.png` (also TI308A p5). Pin 1
  ("compensation") sits on the first-stage output (Q8/Q10). Pin 8 ("compensation") is on the second-stage
  output (Q12/Q14). C8 is therefore the Miller capacitor of the second stage, and it also sets the slew
  rate: SR = I(first stage)/Cf.
* Typical curves, LM308, Cf = 30 pF (`private/docs/datasheets/LM308_NSC_p5_typical_curves_LM308.png` and the two
  crops). Values read off the graphs in this study (pixel-measured):
  * Open-loop gain: 108 dB DC, dominant pole about 2 Hz; about 60 dB at 435 Hz, 48.8 dB at 1.78 kHz,
    27.3 dB at 21 kHz, 9 dB at 176 kHz → **GBW ≈ 0.45–0.50 MHz**. CDB: "around 52dB gain at 1KHz".
  * Cf = 3 pF curve: GBW ≈ 5–6 MHz (×10, consistent with 1/Cf).
  * Voltage-follower pulse response (±5 V step, ±15 V, Cf = 30 pF): falling edge about 10 V in 35 µs
    (**≈ 0.27–0.3 V/µs**); rising edge about 9 V in 45 µs (**≈ 0.2 V/µs**). The two edges are not equal.
  * Large-signal frequency response (Cf = 30 pF): full ±14.5 V only to about 1.5 kHz, about 2 V peak at
    10 kHz. This implies roughly 0.13–0.15 V/µs at the onset of sine distortion, which is the input stage
    leaving its linear range, not the hard slew limit.
* NSC product-folder page (appended to the datasheet PDF), LM108: "Bandwidth, typ 1 MHz; Slew Rate, typ
  .25 V/µs". The compensation for that figure is not stated.
* LTC macromodel header (1989, `private/reference/spice/LM308_LTC.lib`): "GBP=6.0E+05Hz, PHASE MAR.= 70 DEG,
  SR(+)=2.0E-01V/US, SR(-)=1.9E-01V/US, AV= 110 DB, VSAT(+)=1.00V, VSAT(-)=1.00V, ISC=+/- 6.0MA,
  IQ= 300UA", "USE C=30 PF".
* Supply current is nearly independent of supply voltage (NSC graph: about 0.25–0.3 mA from ±5 to
  ±20 V). Slew and GBW should therefore be roughly the same on 9 V as on ±15 V. This is an inference: there
  is no 9 V datasheet curve.
* CDB: genuine LM308 supply current 250–300 µA ("if … far above 300μA, then it's counterfeit"). JHS and
  AION: most "NOS" LM308s sold now are relabelled fakes.

### Facts: what ngspice gives for the saved models (single 9 V supply, `tests/opamp_char.cir`)

| Model | A(1 kHz) | UGF | Phase margin | SR+ / SR− | Output swing (10k to 4.5 V) |
|---|---|---|---|---|---|
| LM308_LT (LTC 1989), 30 pF | 55.7 dB | 0.577 MHz | 70.7° | 0.21 / 0.19 V/µs | 1.00 … 8.00 V (1k load: 1.02 … 7.98) |
| LM308 (TI/Parts 4.01), 30 pF | 56.2 dB | 0.597 MHz | — | 0.20 / 0.20 V/µs | 2.01 … 6.99 V |
| OP07 (TI SLOM536 "Final 1.2", 2025) | 63.1 dB | 1.23 MHz | — | 0.60 / 0.60 V/µs | 2.30 … 6.70 V |

In the RAT with the LTC model at DIST = 1, the maximum dV/dt of the op-amp output during transitions
depends on input level: **0.037 V/µs at 10 mV, 0.13 at 100 mV, 0.175 at 300 mV and 0.199 at 1 V peak**.
Below about 1 V the edges are limited by the input stage's limited linear range (about ±50–100 mV for a
bipolar pair) feeding the Miller integrator, i.e. by GBW × overdrive. Only loud transients reach the hard
slew limit Iee/Cc. **This is the key mechanism behind the "op-amp collapsing" RAT sound.** A model with a
hard slew limiter bolted onto an ideal or linear op-amp will get this transition wrong at guitar levels.

### Facts: OP07 (the replacement part)
* OP07DS (TI SLOS099H): unity-gain bandwidth 0.4 min / 0.6 typ MHz; SR 0.3 V/µs typ; Avol 400 V/mV
  typ; output swing ±12.8 V (2 kΩ), ±13 V (10 kΩ), ±12 V (1 kΩ) at ±15 V, i.e. 2–3 V from the rails,
  which is much worse than the LM308's 1 V; PD 80 mW typ at ±15 V. **Pins 1 and 8 are OFFSET NULL N1/N2.**
  There is no compensation pin; the device is internally compensated.
* The TI 2025 model reflects a die change (PCN 20231031008.1: "GBW = 1.4MHz, UGF = 1.2MHz"). A
  1996–2008 OP07DP should be nearer the datasheet's 0.6 MHz / 0.3 V/µs.
* ProCo kept the 30 pF across pins 1 and 8 on OP07 RAT2s (PedalPCB thread, spi: "the production Rats
  still do it even though they switched years ago"). BuddytheReow: "Pulling the cap out in a OP07
  doesn't change a thing!". AION: "C3 (33pF) should be omitted when using the OP07".
* JHS: "this OP07 is a perfect replacement for the LM308. Its response, slew rate and frequency
  attenuation are identical". Their evidence is frequency-response plots at three settings. AION agrees
  ("performs and sounds identical"). GGG: "little or no difference".
* Dissenting reports: PedalPCB "plot thickens" thread. jesuscrisp: "the 308 is spongier and more saggy at
  high gain"; Laundryroom David: OP07 "thinner" at low gain. CDB adds that differences beyond slew/GBW
  come from "output impedance and drive capability" and "recovery from saturation", which the
  datasheets do not characterise. Tampco hears a level-dependent "compression" with metal-can LM308s.
  sbn31 hears more 800–1000 Hz on a Chinese OP07 RAT2 than on an LM308 clone.
* Consistent with the table above: the OP07's output swing on 9 V (about 2.3 V from each rail in the TI
  model and datasheet) is narrower than the LM308's (about 1 V). In ngspice this lowers the drive into
  the diodes (diode node peak 0.607 V for OP07 versus 0.635 V for LM308 at DIST = 1). That is a
  measurable, non-small-signal difference which JHS's frequency-response comparison would not show.
* TL071/TL072: SR 13 V/µs (ES, about 40× the LM308); "brighter", "plinking" attack (GGG); needs C8
  removed (PedalPCB, Nostradoomus).

### Models found
* **Yeh 2009 thesis §2.3.3 and DAFx-07:** the op-amp stage (Boss DS-1) is a linear 2nd-order filter
  followed by hard clipping at the supply. The thesis says plainly that the op-amp's behaviour is "much
  slower because it is open loop and needs to recover from overdrive. It is also typically asymmetrical
  in behavior, leading to significant even-order harmonics… Refinements … can be based upon the
  macromodeling technique as done in SPICE (Boyle et al., 1974)."
* **Boyle macromodel (1974)** and its SPICE descendants: the TI/National `LM308`, the TI OP07 and the LTC
  `LM308_LT` (which adds input clamps, a current limit and a separate output stage). In all of them:
  * a bipolar differential pair, with full-steering current Iee, drives a transconductance into a Miller
    integrator (Cc external on the LM308);
  * GBW = gm/(2πCc) and SR = Iee/Cc;
  * the output is clamped by diodes to rail-referenced voltage sources.

  This structure reproduces both GBW and SR, and the soft transition between them, with three
  parameters. The TI Boyle models reference internal nodes to mid-supply (node 99). The LTC model
  references several internal elements to global ground. On a single supply this gives non-physical
  supply-pin current, but correct signal behaviour (see the table).
* **Werner et al., EUSIPCO 2016** (WDF with op-amps): nullor and linear controlled-source macromodels;
  "nonlinear behaviors including slew-rate limiting … are often included in op-amp macromodels …
  Future work will incorporate such nonlinear macromodels". Slew is **not modelled**.
* **Müller & Hélie, DAFx-19**: minimal passive op-amp with saturation and explicit supply ports.
  Memoryless ("infinite bandwidth, infinite slew rate"). Useful for the rail/supply formulation only.
* **Holters & Zölzer, EUSIPCO 2015** (ACME.jl basis): op-amps as ideal (nullor) or "soft-clipping op amp
  … Vmax·tanh" elements inside a generalized nonlinear state-space. Memoryless clipping, no slew.
* **Bogason & Werner, DAFx-17**: OTA (tanh transconductance) elements in WDF. This is the same
  tanh-gm building block that a Boyle-style first stage needs.
* **BYOD Mouse Drive** (Chowdhury): WDF R-type adaptor with a linear op-amp, Ag = 100 (!), Ri = 10 MΩ,
  Ro = 0.1 Ω. **No GBW, no slew, no rails.** Diode pair Is = 5 nA, Vt = 25.85 mV, nDiodes = 2. DIST
  mapped as R = 1 + 100k·x⁵. Clipping stage only (no FILTER or buffer); volume is a dB gain. A useful
  baseline for what is lost without op-amp dynamics.
* **ElectroSmash**: qualitative. Slew-rate bandwidth fmax = SR/(2πVp) = 5.3 kHz with SR 0.3 V/µs and
  Vp = 9 V. The output only swings about ±3.5 V around 4.5 V, so the same formula gives about 13.6 kHz
  (0.3 V/µs) or 9.1 kHz (0.2 V/µs).
* No published component-level DK/WDF model of the full RAT with op-amp dynamics was found. The
  searches covered DAFx archive titles and full text (slew, op-amp, distortion pedal, RAT), arXiv and
  Google.

### Disagreements
* LM308 slew rate: 0.3 V/µs (ES; Cybercow), 0.15 V/µs (GGG), 0.25 V/µs (NSC product folder),
  0.20 / 0.19 V/µs (LTC model), and roughly 0.2 (rising) / 0.27–0.3 (falling) V/µs (NSC pulse graph).
  The sine-onset reading is about 0.14 V/µs.
* GBW with 30 pF: about 0.45–0.5 MHz (NSC graph; CDB's 52 dB at 1 kHz), 0.6 MHz (LTC), 0.6–0.65 MHz (TI
  Boyle), "1 MHz" (NSC product folder, compensation unstated).
* Output swing on 9 V: 1.0 V from the rails (LTC; NSC ±14 V on ±15 V) versus 2 V (TI Boyle LM308).
* OP07 = LM308? JHS, AION and GGG say yes; several players say no. The TI 2025 OP07 model is 2× faster
  than the classic OP07.
* OP07 changeover date: 1995–96, 2002–03 or about 2005 (see `circuit.md` D16).

### Recommendation
Implement a **Boyle-type behavioural macromodel** as part of the MNA, not as a post-filter:

1. Nodes: p (pin 3), n (pin 2), x (pin 1, the first-stage output and high-impedance integrator input),
   y (pin 8, the second-stage output), o (pin 6). Internal reference vm = (V+ + V−)/2.
2. Input: R_id = 40 MΩ between p and n (NSC typ). Optional anti-parallel clamp diodes p–n, Is ≈ 1e-15 A
   (LTC DM2 = 8e-16).
3. First stage, nonlinear VCCS into x: i1 = Iee·tanh((vp − vn)/(2·VT1)).
   * Iee = SR·Cc and gm1 = Iee/(2·VT1) = 2π·GBW·Cc.
   * With Cc = 30 pF, GBW = 0.6 MHz, SR = 0.2 V/µs (LTC-consistent): Iee = 6.0 µA, gm1 = 113 µS,
     VT1 = 26.5 mV. This matches a plain bipolar pair, which is a good sign the numbers are self-consistent.
   * With the NSC-graph numbers (GBW 0.5 MHz, SR 0.25 V/µs): Iee = 7.5 µA, gm1 = 94 µS, VT1 = 40 mV.
   * Optional asymmetry: separate Iee+ and Iee−. The NSC pulse graph shows the falling edge faster; LTC
     shows the rising edge slightly faster. Fit it.
4. Second stage: inverting VCCS from (vx − vm) into y with R_y to vm (gain A2 ≈ 10⁴). R_x from x to vm
   sets the DC gain: A0 = gm1·R_x·A2 ≈ 3·10⁵ (108–110 dB), e.g. R_x ≈ 265 kΩ for A2 = 10⁴. **C8 = 30 pF is
   a real circuit element between x and y**, so changing it changes GBW and SR together, as on the real
   chip (NSC "proportional to 1/Cf").
5. Output: o = buffer(y) with Ro ≈ 100–150 Ω (LTC RO1 = 111 Ω). Clamp the swing smoothly to
   [V− + 1.0 V, V+ − 1.0 V] (LTC VSAT; NSC ±14/15 V), and the current to ±6 mA (LTC ISC; NSC
   output-swing-versus-current graph). Clamp the integrator node as well (anti-windup). Real recovery from
   saturation is uncharacterised: expose a parameter and fit it to captures. In the LTC model, node x/y
   swings to 8.69/0.34 V while the output is clamped at 8.02/1.02 V.
6. A second pole near 1.6 MHz (for a 70° phase margin) is optional; it is above audio and above typical
   oversampled Nyquist.
7. Draw the quiescent 0.3 mA and the output current from the rails, so that supply sag through R1 47 Ω
   is physical. This is optional.

Treat GBW (0.45–0.65 MHz), SR (0.15–0.3 V/µs, possibly asymmetric) and Vsat (0.9–1.2 V) as calibration
parameters for each captured pedal. For the OP07 variant use GBW 0.6 MHz, SR 0.3 V/µs, Vsat ≈ 2.2 V
(OP07DS) for a vintage OP07DP, or 1.4 MHz / 0.6 V/µs for a post-2023 TI die. Its pins 1 and 8 have no
model, so drop C8.

Nonlinear ports this adds to the DK/Newton system: tanh VCCS (1-D), output clamp (1–2), optional input
clamp (1). Expect strongly stiff behaviour at high DIST. Use damped Newton or homotopy fallbacks
(Holmes & van Walstijn, DAFx-15), and 4–8× oversampling: slewing edges plus hard diode clipping alias
readily (Yeh DAFx-07 "clipode" discusses oversampling and stiff solvers; Holters DAFx-19 ADAA for
stateful systems is an alternative).

---

## 3. Clipping diodes (R8, C9, D2, D3)

### Facts
* Two anti-parallel silicon diodes to ground after R8 1 kΩ and C9 4.7 µF (all sources). The RAT2 and
  Vintage RAT use 1N4148 (MR, JHS, RGK, PISO); the original RAT uses 1N914 (MR, ES, most clones). ES:
  "any other silicon compatible like 1N4148, 1N4448, or 1N916 can be replaced without troubles".
* FSC-D: VF 620–720 mV at 5 mA (1N914B/4448); ≤ 1.0 V at 10 mA (1N914/4148); trr 4 ns; CT 4 pF.
* LTspice/OnSemi model (identical for 1N914 and 1N4148): Is = 2.52 nA, N = 1.752, Rs = 0.568 Ω,
  Cjo = 4 pF, M = 0.4, TT = 20 ns. Computed Vf: 0.58 V at 1 mA, 0.67 V at 5 mA, 0.69 V at 10 mA. NXP 1N4148:
  Is = 4.352 nA, N = 1.906, Rs = 0.6458 Ω (0.61 V at 1 mA).
* ngspice in the RAT (100 mV, 1 kHz): diode node ±0.635 V with the LTC LM308, which drives about
  ±2.9 mA through R8. It is ±0.615 V with the TI LM308 model (2 V swing headroom) and ±0.607 V with OP07.
* Op-amp clipping comes first and the diodes clip again afterwards (JHS, Tampco, ES "op-amp collapsing").
  The waveform at the diode node is therefore a hard-clipped version of an already slew- and
  rail-limited signal.
* C9 is DC blocking. Its value interacts with the asymmetric charge pumping during asymmetric clipping
  (time constant about 4.7 µF × 1 kΩ = 4.7 ms when the diodes conduct). It behaves like a "bias shift",
  which a state-ful model captures automatically.

### Models found
* Yeh, Abel & Smith DAFx-07 "Simulation of the diode limiter … ODE" and the thesis ch. 3: an RC-diode
  clipper as a 1st-order ODE. Compares BE, TR, BDF2 and explicit methods; uses Is = 2.52 nA,
  Vt·n = 45.3 mV, R = 2.2 kΩ, C = 10 nF (DS-1 values).
* Yeh DAFx-07 "Simplified, physically-informed models": static clipping functions fitted to the
  ODE (tanh, arctan, etc.) plus oversampling.
* BYOD Mouse Drive: WDF DiodePair (Is = 5 nA, Vt = 25.85 mV, nDiodes = 2; the Wright-omega solution).
* Turbo / YDR variants: red LEDs; 1N34A Ge; BAT41 Schottky (models in `private/reference/spice/diodes.lib`;
  the Turbo LED part number is unknown).

### Disagreements
1N914 versus 1N4148 (electrically equivalent; same model); YDR 1N34A versus BAT41; Turbo LED type.

### Recommendation
In the MNA, treat D2/D3 as one nonlinear one-port: i(v) = Is·(e^{v/(N·Vt)} − e^{−v/(N·Vt)}), with
Is = 2.52 nA and N·Vt = 45.3 mV at 25 °C. Rs = 0.57 Ω is negligible at the ≤3 mA currents here; keep it
only if it is free in your formulation. Junction capacitance (4 pF) is irrelevant with a 1 kΩ source
(pole about 40 MHz) and can be dropped. Expose Is/N for fitting (temperature and part spread). Allow
Is+ ≠ Is− to model mismatched pairs, which produce even harmonics. Use the full stateful circuit (C9, R8,
the op-amp output impedance) rather than a static waveshaper: the FILTER corner and the C9 bias shift
both depend on the diode state.

---

## 4. FILTER (FIL, R9, C11)

### Facts
* A passive 1st-order low-pass: the rheostat FIL (100 kΩ A) plus R9 into C11 3.3 nF (all sources).
  ES: fc = 1/(2π(R_TONE + R7)·C8), 475 Hz to 32 kHz with R7 = 1k5. With MR's R9 = 1k6: 475 Hz to 30 kHz.
* CW = more resistance = darker (MAN: "Turn this high cut filter clockwise to decrease the amount of
  treble"; AION; Coda). Before 1981 the control was labelled TONE and wired reversed (JHS; MR).
* MAN (RAT2 spec): "Filter 6dB/Oct Low-Pass".
* The source driving the filter is the clip node. Its impedance is about R8 (1 kΩ) when the diodes are
  off and about N·Vt/I (tens of ohms) when they conduct. With FILTER fully CCW the corner therefore moves
  between about 18.6 kHz and 30 kHz with signal level (computed in this study). The downstream load is
  C12 22 nF + R10 1 MΩ (7.2 Hz HPF), which is negligible.
* MR: C11 2.7 nF on Chinese RAT2s (2008+), 3 nF on the earliest big-box units.

### Models found
ES formula; ES also shows the "shark fin" waveform with the filter maxed. Holters & Zölzer DAFx-11
(nodal DK with variable parts) and Dempwolf, Holters & Zölzer DAFx-10 (parametric circuits) show how to
handle the pot efficiently.

### Disagreements
R9 1k5 versus 1k6; C11 3n3 / 2n7 / 3n; taper law (§7).

### Recommendation
Keep it in the MNA as R(x) + R9 + C11, so that the level-dependent source impedance comes for free. Use
Holters & Zölzer's low-rank update for the three pots (or recompute the small matrices on knob change,
with smoothing). The FILTER is the most taper-sensitive control: at noon a 10 % taper puts fc near
4.1 kHz, a 15 % taper near 2.9 kHz. **Fit the taper to captures.**

---

## 5. JFET output buffer (C12, R10, Q1, R11)

### Facts
* Common-drain follower. Q1 = 2N5458 (most sources), drain to +9 V, gate 1 MΩ to ground, source 10 kΩ to
  ground (F86, MR, ES). ES: "high input impedance (approx R8=1MΩ) and a medium-low output impedance (few
  hundred of ohms)". Its role (ES; CDB): to stop the next device loading the FILTER network.
* FSC-J (2N5458): VGS(off) −1.0 to −7.0 V; IDSS 2.0 / 6.0 / 9.0 mA (min/typ/max); |VGS| ≤ 3.5 V at
  ID = 200 µA; gfs 1.5–5.5 mS; Ciss 4.5–7 pF; Crss 1.5–3 pF.
* LTspice 2N5458 model: Vto = −2.882 V, Beta = 488.9 µA/V² (IDSS 4.06 mA), Lambda = 3.167e-3,
  Is = 181.3 fA, Cgs = 4.627 pF, Cgd = 4 pF. ngspice ignores its Isr/Nr/Alpha/Vk parameters (gate
  recombination and impact ionisation, irrelevant here) with a warning.
* ngspice operating point: V(src) = 2.216 V, Id = 0.222 mA.
* Small-signal gain (computed): gm ≈ 2·√(β·Id) ≈ 0.66 mS. With the RAT2 load R11 ‖ R15 ‖ 100k ≈ 4.8 kΩ
  the gain is about 0.76–0.78 (ngspice clip→out at full volume: 0.78). With the original RAT's 1 µF and no
  R15, the load is 9.1 kΩ and the gain about 0.86.
* Negative headroom: with the gate at 0 V DC the source can fall only about Id·R_ac ≈ 1.1 V (RAT2) below
  its bias before cut-off. This is fine for ±0.64 V silicon clipping. With LEDs (±1.6 V) ngspice gives
  +1.29 / −1.06 V (asymmetric) unless the gate is biased up as in the Turbo (MR R16 2M2 + R10 2M2 →
  +1.40 / −1.34 V). CDB: "The buffer FET is biased from ground, so we want a large Vp for maximum headroom.
  Ideally, the source voltage should be at 4.5V, but anywhere from 3V to 6V is good."
* Square-law transfer: gm changes with the signal current (±0.13 mA on 0.22 mA at ±0.64 V output),
  giving a small even-order component (inference from the device law; not measured here).
* AION and CDB substitutes: 2N5485, J111/J112, MPF4393; MPF102 is a common equivalent (LTspice MPF102:
  Vto −3.41 V, IDSS ≈ 12 mA).

### Models found
Eichas et al. DAFx-14 (MXR Phase 90: JFETs in a DK model; JFETs as variable resistors). Yeh 2012 TASLP
Part II (transistor examples in the DK method). The Shichman–Hodges (SPICE level-1) NJF model is
standard.

### Disagreements
Q1 type and individual Vp/IDSS spread (the biggest unit-to-unit variable in this block); R10 value and
return (1M to ground versus 1M to +4.5 V in RGK-B; 100K in the RGK-F table; 2M2/2M2 Turbo).

### Recommendation
Model Q1 as a level-1 NJF with two nonlinear currents: the drain current Id(vgs, vds) with λ, and a
gate-junction diode, which can be omitted since it is never forward biased here. Use Vto = −2.88 V,
β = 489 µA/V², λ = 3.2e-3 as the default, then fit Vto (or measure V(src) on the captured pedal) so that
V(src) matches; the spec allows about 1.5–3.5 V. Capacitances (≤ 5 pF against ≤ 13 kΩ) are irrelevant
below 1 MHz and can be dropped. For the Turbo variant add R16.

---

## 6. VOLUME and output (C13, R15, VOL, load)

### Facts
* C13 10 µF and R15 10 kΩ on the RAT2. F86 shows "R15 10K", so the 10k is factory. MR and FC give RAT2 and
  Turbo 10 µF/10k; the original RAT has 1 µF and no R15 (MR, ES). The reason given online is that the 10k
  was added in parallel with the volume control for the LED bypass circuit, and C13 was raised tenfold to
  keep the cutoff (1 µF + 100k ≈ 10 µF + 10k). This comes only from a web-search summary of the
  freestompboxes "Proco - Rat 2 (early 90s)" thread, which could not be fetched (see open questions).
  Treat it as unverified secondary. Computed corners: 1.75 Hz versus 1.6 Hz. The F86 drawing shows R15
  tied into the RR3/SW1-B area, consistent with that explanation.
* VOLUME 100 kΩ audio (A) taper: lug 3 to C13 and R15, lug 1 to ground, wiper to the jack (all sources;
  ES "standard 100K audio potentiometer which bleeds part of the input signal to ground").
* MAN (RAT2 spec): "Output Impedance 1 kOhm". Computed: about 1 kΩ at full volume (1/gm ‖ 10k ‖ 10k), about
  13 kΩ at noon (15 % taper).
* ngspice: output peak about ±0.5 V at DIST = 1, FILTER = 0, VOL = 1 with silicon diodes (0.635 V × 0.78).

### Models found
Trivial linear network. BYOD replaces it with a dB gain.

### Recommendation
Keep C13, R15, the pot and the load in the MNA. Load = 1 MΩ to match the captures (an interface input;
check the actual capture interface). Smooth the volume control as a pot (resistive), not as gain,
so the output impedance follows the setting.

---

## 7. Pots (DIST, FILTER, VOLUME) and tapers

### Facts
* All three are 100 kΩ audio/log (A) (MR, ES, F86 schematic labels, GGG "100k Log", PPCB A100K,
  AION 100kA, FC A100k, PISO/RGK "100K LOG"). None is linear on any source.
* Taper construction: piecewise-linear 2–3 segment inks (geofex "Secret Life of Pots"). Typical value at 50 %:
  * 10 % (Bourns "Z" taper: "attains 10% resistance value at 50% of clockwise rotation"; geofex);
  * 15 % (Alpha "15A (JIS:A)", CDB);
  * 15–20 % (geofex);
  * 25–30 % (Fender amps, CDB).
* Tolerances (Bourns guide): non-linear tapers >100 kΩ ±20 % total; taper tolerance ±20 % of nominal at
  50 %; low-side end resistance up to 4 Ω.
* JHS: Chinese RAT2 pots "are prone to breakage"; "the up-to-20% parts tolerance from pot manufacturing
  is a much bigger factor in perceived tone change across various units".
* JHS (1981 "Filter" change): "The taper is different, but it is identical in its filtering capabilities".
  Reversing lugs 1 and 3 mirrors the taper.

### Recommendation
Implement A(x) with a selectable law (exponential with A(0.5) = t, or 2-segment PWL through (0.5, t)) and
fit t per pot to the captures. Start at t = 0.15. Tables are in `circuit.md` §3. DIST and FILTER are
rheostats (lug 1 to wiper); VOLUME is a divider.

---

## 8. Power supply and bias (R1, C1, D1, C10, R2, R3, C2)

### Facts
* 9 V battery or negative-ground adapter (MAN). R1 47 Ω (RAT2 "sometimes 100", MR) into C1 100 µF, with D1
  as a shunt reverse-polarity diode (MR, F86, ES, RGK). C10 decoupling (values disagree: 1–100 nF).
* ES: "R10 47Ω … together with the caps C11 and C12 will create a low pass filter". Corner R1·C1 = 34 Hz.
* Bias: 100k/100k divider, C2 1 µF → Vref ≈ 4.5 V (3.2 Hz corner). ngspice: 4.486 V.
* Current: original about 0.6 mA idle and 1.6 mA at full output (RGK-B/F). RAT2 3.0 mA including the LED
  (MAN spec). Op-amp quiescent 0.3 mA (NSC); JFET about 0.22 mA; divider 45 µA.
* ngspice (LTC model) quiescent: 27 mV across R1 = 0.57 mA. That matches the expected draw: 0.29 mA op-amp
  (model IP source) + 0.22 mA JFET + 0.045 mA divider. However, the LTC model sources the op-amp's *load*
  current from internal elements tied to global ground (GB, RO2, …), not from pin 7, so
  signal-dependent supply current (and therefore supply sag) is not physical with that model.

### Models found
None specific. Müller & Hélie DAFx-19 is an example of an op-amp model with explicit, non-ideal supply
ports.

### Recommendation
For the reference model, use an ideal 9 V source and keep R1, C1, R2, R3 and C2 (cheap, linear). A
"battery sag" option can come later via a battery source resistance (a fresh alkaline 9 V is about
1–2 Ω; a worn one tens of ohms; not RAT-sourced) and a physical op-amp supply current. D1 and C10 can
be dropped. In ngspice keep the 0.1 Ω ESR on C1: without it the Boyle-type op-amp models fail at the
first transient step in ngspice-47, although the LTC model does not need it.

---

## 9. Numeric constants

| Quantity | Value (default) | Range / alternatives | Source |
|---|---|---|---|
| Supply | 9.0 V | adapter or battery | MAN |
| R1 / C1 | 47 Ω / 100 µF | 100 Ω | MR |
| Vref (R2 = R3, C2) | 100 kΩ, 1 µF → 4.49 V | 47 µF (clones) | MR, ES; ngspice |
| R12 | 2.2 MΩ | 1 MΩ (F86), absent (original) | MR, F86 |
| C3 | 22 nF | — | all |
| R4 | 2.2 MΩ | 1 MΩ (original) | MR |
| R5 / C4 | 1 kΩ / 1 nF | — | all |
| C8 (comp) | 30 pF | 33 pF (earliest) | MR, ES, F86 |
| DIST | 100 kΩ A rheostat | 150 kΩ A (earliest) | MR |
| C5 (feedback) | 100 pF | — | all |
| R7 / C6 | 560 Ω / 4.7 µF (60.5 Hz) | — | all |
| R6 / C7 | 47 Ω / 2.2 µF (1539 Hz) | C7 4.7 µF (RGK-B) | all but RGK-B |
| Max ideal gain | 2307 (67.3 dB) | — | ES (2305) |
| Max real small-signal gain | 63.5 dB at 631 Hz (LTC model) | about 60 dB at 600 Hz | ngspice, CDB, MAN |
| R8 / C9 | 1 kΩ / 4.7 µF | 1 µF film (AION) | all |
| D2, D3 | 1N4148: Is 2.52 nA, N 1.752, Rs 0.568 Ω, Cjo 4 pF | 1N914 identical | LTspice/OnSemi, Yeh |
| Diode node clip level | ±0.635 V (LM308, 100 mV in) | ±0.607 V (OP07 model) | ngspice |
| FILTER | 100 kΩ A rheostat, CW darker | — | MR, MAN |
| R9 | 1.6 kΩ | 1.5 kΩ | MR / others |
| C11 | 3.3 nF | 2.7 nF (Chinese), 3 nF (earliest) | MR |
| Filter fc range | 475 Hz – 30 kHz (diodes conducting) | 470 Hz – 18.6 kHz (diodes off) | computed |
| C12 / R10 | 22 nF / 1 MΩ (7.2 Hz) | R10 2M2 + R16 2M2 (Turbo) | MR, ES |
| Q1 2N5458 | Vto −2.882 V, β 488.9 µA/V², λ 3.167e-3 | VGS(off) −1…−7 V, IDSS 2–9 mA | LTspice, FSC-J |
| Q1 bias | Vs 2.22 V, Id 0.222 mA | — | ngspice |
| R11 | 10 kΩ | — | all |
| C13 / R15 | 10 µF / 10 kΩ | 1 µF / none (original) | MR, F86 |
| VOLUME | 100 kΩ A divider | — | all |
| Buffer gain | 0.78 | 0.86 (original) | ngspice / computed |
| Output impedance | ≈ 1 kΩ at full volume | about 13 kΩ at noon | MAN, computed |
| Input impedance | 0.92 MΩ @100 Hz, 164 kΩ @1 kHz, 16.7 kΩ @10 kHz | MAN "1 MOhm" | computed |
| Taper at 50 % rotation | 0.15 | 0.10–0.20 | Alpha 15A, Bourns Z, geofex |
| LM308 GBW (Cf 30 pF) | 0.6 MHz (LTC) | 0.45–0.5 MHz (NSC graph) | LTC, NSC |
| LM308 SR (Cf 30 pF) | 0.20 / 0.19 V/µs (LTC) | 0.2–0.3 (NSC pulse), 0.25 (NSC folder) | LTC, NSC |
| LM308 Avol | 110 dB (LTC), 108 dB (NSC graph) | ≥ 25 V/mV min | LTC, NSC |
| LM308 Vsat | 1.0 V each rail | ±13 V min on ±15 V | LTC, NSC |
| LM308 Isc | ±6 mA | about 5–6 mA (graph) | LTC, NSC |
| LM308 Iq | 0.3 mA | 0.25–0.3 mA (CDB) | NSC, LTC |
| LM308 Rin (diff) | 40 MΩ | 10 MΩ min | NSC |
| LM308 Ib | 1.5 nA | 7 nA max | NSC |
| Macromodel Iee, gm1, VT1 | 6.0 µA, 113 µS, 26.5 mV | 7.5 µA, 94 µS, 40 mV (NSC numbers) | derived |
| OP07 GBW / SR | 0.6 MHz / 0.3 V/µs (datasheet) | 1.4 MHz / 0.6 V/µs (TI 2025 die/model) | OP07DS, TI model |
| OP07 Vsat (9 V) | ≈ 2.2–2.3 V | — | OP07DS, TI model |
| TL071 SR (comparison) | 13 V/µs | — | ES |
| Current draw | 0.6 mA (orig, idle) / 3.0 mA (RAT2 with LED) | 1.6 mA at full output (orig) | RGK, MAN |

---

## 10. Open questions and blockers

1. **Which RAT was captured?** Year and serial decide LM308 versus OP07 (date disputed: 1995/96,
   2002–03 or about 2005), R4/R12 (1M or 2M2), C11 (3n3 or 2n7 Chinese) and R9 (1k5 or 1k6). Without
   capture metadata, these must be fitted, or measured inside the pedal if available.
2. **LM308 dynamic parameters of the actual chip:** GBW (0.45–0.65 MHz), SR (0.15–0.3 V/µs, possibly
   asymmetric), output saturation voltage and **recovery from saturation**, which no datasheet or
   model characterises (CDB). This is the single biggest risk to a faithful model; plan to fit it
   against NAM captures at high DIST with transient-rich input.
3. **Pot taper law and tolerance** of the original pots (10 %, 15 % or 20 %; segment break points;
   ±20 % value). This decides where "noon" sits for all three controls.
4. **Q1 operating point:** the 2N5458 spread is huge (VGS(off) −1…−7 V). V(src) on the captured
   unit would pin it down.
5. Real **diode** Is/N of the parts used (1N4148 batches vary) and temperature.
6. **OP07 die and pins 1/8:** does the 30 pF across the OP07 null pins do anything? Reports say no;
   this is unverified. Which OP07 die is in a given unit?
7. **Early versions:** did the 1979–80 big box have an input JFET buffer (RGK-O, GGG "version A")?
   JHS's history implies not. Also: Turbo R16 presence (MR versus FC), YDR 1N34A versus BAT41, and the
   Turbo LED part.
8. **Inaccessible primary threads.** These sit behind a Cloudflare bot check that was deliberately not
   bypassed; please open them in a browser if needed:
   * freestompboxes.org "Proco - Rat 2 (early 90s)" `viewtopic.php?t=890`
   * freestompboxes.org "RAT2 (made in china)" `t=12468`
   * R. Strand's `diystompboxes.com/pedals/rstrand/rat/rat_versions.htm`
   * DIYstompboxes topics 96351 and 80809

   The Internet Archive was also unavailable (HTTP 429 / offline) during this session.
9. **ElectroSmash main site** (electrosmash.com) did not resolve; the archive mirror
   electrosmash.mas-effects.com was used, and its PDF was saved for cross-checking.
10. analog.com (ADI OP07 datasheet and model) timed out from this machine; the TI equivalents were used.
11. Not downloaded: Kurt Werner's 2016 Stanford thesis (https://purl.stanford.edu/jy057cz8322; no direct
    PDF link found); the Boyle et al. 1974 JSSC paper (paywalled).
12. **A possible extra verification target:** the "Datasets for asymptotically stable recurrent neural
    networks" (Kallinen, Zenodo, doi:10.5281/zenodo.20406285) contain ProCo RAT recordings over all
    three controls (arXiv 2509.15622). Not downloaded; it needs a decision, since it is a large data
    download.

---

## 11. Findings added 2026-10-09

Material filed on 2026-10-09; every file is listed in `docs/sources.md`. The Japanese pages have English summaries
next to them (`*.en.md`); the numbers below were checked against the Japanese originals. "Read by eye" marks values
taken from screenshots rather than from text. Further tags: **AN29** = Widlar, AN-29 (TI SNOA624B); **KGB** =
kanengomibako RAT2 page; **JC** = jazzcaster.com articles.

### Real-unit DC operating points and current parts
* PedalPCB thread 24340 (`private/docs/web/forums/pedalpcb_proco-rat-2-weak-volume-solved.txt`, Dec 2024): a working
  RAT2 (the fault was the footswitch) on a "RAT 3 Rev. B" board; op-amp type not stated. DC to ground with a hobby
  meter: IC pin 1: 8.4, 2: 4.2, 3: 2.0, 4: 0, 5: 0, 6: 4.2, 7: 8.5, 8: 8.4 V; JFET (flat side facing, left to right,
  D-S-G) 8.53 / 2.81 / 0 V.
  * Vref is 4.2 V on an 8.5 V rail. Pin 3 should also sit at Vref through R4; the 2.0 V reading is most likely the
    meter loading R4 (a 1 MΩ-input meter on R4 = 1 MΩ reads about 2.1 V). Pins 1 and 8 near V+ fit an OP07, whose
    offset-null pins sit close to V+. Both are inferences, not stated in the thread.
  * JFET source 2.81 V, against 2.216 V for the LTspice 2N5458 model in ngspice (§5).
* KGB (`private/docs/web/kanengomibako_00377_rat2.md`, `.en.md`): three Motorola 2N5458 pulled from a ProCo BRAT, in
  a test jig wired like the RAT follower (+9 V, 1 MΩ gate to ground, 10 kΩ source, 1 µF into 100 kΩ), driven with
  1.35 Vpp (the most the diodes pass) at about 740 Hz: source 2.8 / 2.6 / 2.9 V, THD 0.23 / 0.28 / 0.25 %. Other
  parts in the same jig: 2N5457 1.2 V / 0.85 %, Toshiba 2SK30ATM-GR 1.9 V / 0.44 %, J112 3.2 V / 0.08 %.
  * Real 2N5458 followers therefore sit at 2.6–2.9 V, 0.4–0.7 V above the LTspice model. Fitting Vto to
    V(src) ≈ 2.8 V (§5 recommendation) moves the model towards them.
* Current RAT2, KGB trace "RAT 3A SEC-5-0 REV B" (Sept 2025,
  `private/docs/web/images/kanengomibako/09_377_51_ProCo_RAT2_schematic.png`): IC1 OP07CP (TI logo on the parts
  photo `09_377_02_rat2_parts.jpg`), Q1 2SK30ATM-GR (maker unknown), C11 2.7 nF (a green film capacitor marked 2A272J
  in the photo, read by eye). The same trace has R4 1 MΩ, no separate input pull-down, **C13 1 µF into VOLUME and
  no 10 kΩ across VOLUME**, R9 1.5 kΩ, C8 30 pF (KGB measured 31.8 pF in a RAT2), LED via 1.5 kΩ.
  * freestompboxes t=12468 (`private/docs/web/forums/freestompboxes_t12468_proco-rat2-made-in-china.txt`, 2011)
    lists the same for a Chinese RAT2 (OP07DP, R4 1M, R9 1k5, C13 1 µF, no 10k; a 33k "pull-down" on the switch
    where 1M was expected), with C11 3.3 nF on that unit and 2n7 on another (culturejam); one unit had reverse-log
    pots (Chugs, 2012). Chinese RAT2s thus differ from the US RAT2 values in §9 (R4 2M2, C13 10 µF + R15 10k).
* R15 10k and C13 10 µF on US RAT2s (§6 had this only from a search summary): ggbb, author of the Multi-RAT drawing,
  in freestompboxes t=890 (`private/docs/web/forums/freestompboxes_t890_proco-rat-2-early-90s.txt`, 2012): "The 10K
  resistor was added in parallel with the volume control so that the LED bypass circuit would work. In order to keep
  the cutoff frequency the same … they changed the output cap by the same factor in the opposite direction."
  soulsonic (2007) gives the same reason. JHS (2007) gives a tonal one ("softer, smoother"); ggbb also disputes the
  "100k/220k bias resistor after the 22nF" claim made there ("never seen anything but 1M").

### Op-amp measurements
* JC RAT2 (`private/docs/web/jazzcaster_proco-rat2-measurement-results.md`, `.en.md`): the author's RAT2 with a
  Motorola LM308N, dated "around 1990" from the op-amp. At DISTORTION 0, FILTER 0, VOLUME 10 the output is about 90 %
  of bypass, with the 2nd harmonic at −80 dBu on a −20 dBu fundamental. With −20 dBu (0.11 V peak) in, clipping is
  visible from DISTORTION ≈ 2.5 and the waveform is near-square from 7.5 on. With rising DISTORTION the swept
  response loses highs and centres on the mids; the FILTER sweeps cut "from somewhere between 400 Hz and 500 Hz".
  All read by the author from screenshots.
* JC op-amp stage test (`private/docs/web/jazzcaster_cabapedals-ratest-lm308-distortion.md`, `.en.md`): the RAT gain
  stage alone on 9 V, gain 1 + 4.62k/560 = 9.25 (19.3 dB; the page prints "9.7 dB"), no 100 pF, no diodes,
  AC-coupled output into 100 kΩ; 1, 4 and 10 kHz at −10 and −12 dBu.
  * Reference Motorola LM308AN at −10 dBu (346 mV peak), 1 kHz: just reaches +4 V and −4 V about the bias point;
    with more gain **the positive side saturates first**. About 8 Vpp on 9 V is closer to the rails than the LTC
    model's 1.0 V (§2); AN-29 says "within a volt".
  * The 8 LM308s (Motorola and National, DIP and can) saturate positive-first or nearly symmetrically, and their
    10 kHz slewing differs widely, even within one lot: the reference reaches ±4 V, a same-lot LM308AN only about
    ±2.5 V with a triangle wave.
  * TI OP07: **saturates on the negative side first, and earlier: about −3 V against +4 V**. The TI OP07 model (§2)
    clips symmetrically about 2.3 V from each rail.
  * All voltages read by the author from scope shots.
* JC FFT companion (`private/docs/web/jazzcaster_cabapedals-ratest-lm308-fft.md`, `.en.md`): full RAT clone (1N914,
  2N5458), six op-amps, DISTORTION ≈ 1 and 3, −30/−20/−10 dBu. At DISTORTION 1, even harmonics appear first as the
  level rises (asymmetric op-amp saturation) and odd harmonics join when the diodes clip; one Motorola LM308AN shows
  only odd harmonics; the OP07 is odd-dominated. At DISTORTION 3 the op-amp output is already about ±3.5 V at
  −30 dBu, odd harmonics dominate, and even harmonics grow as saturation deepens ("duty ratio becomes asymmetric").
* KGB slew and GBW, Cf = 30 pF (scope `private/docs/web/images/kanengomibako/09_377_21_SR30V.png`): LM308N at
  ±15 V about **0.17 V/µs**, "all units about the same", **the falling edge slightly faster**; GBW 484 kHz (+33.67 dB
  at 10.03 kHz in a 40 dB stage). At ±4.5 V: LM308N 0.16 V/µs / 457 kHz, LM308AN 0.16 / 434, LM308AH 0.18 / 482,
  OP07CP (TI) 0.18 / 482, OP07N (HTC) 0.21 / 566.
  * This supports the low end of the §2 ranges on a 9 V-equivalent supply: SR 0.16–0.18 V/µs, GBW 0.43–0.48 MHz,
    falling edge faster (as in the NSC pulse graph; the LTC model has the rising edge slightly faster). The measured
    TI OP07CP equals the LM308AH, far from the 2025 TI model's 0.6 V/µs / 1.2 MHz (§2).
* Chuck D. Bones, new Chinese RAT2 with OP07, pin 6 at full DISTORTION (`private/docs/web/forums/pedalpcb_rat-2.txt`,
  thumbnails in `private/docs/web/images/pedalpcb/rat-2/`): "Clipping is asymmetric and contains a lot of even-order
  harmonics"; "The slew is much faster than the LM308 and is very asymmetric". Read by eye from the 274×150
  thumbnails (only these are served without a login): input 198 mVpp at 249 Hz, pin 6 6.32 Vpp, duty cycle 45.0 %
  (FFT shot); input 197 mVpp at 1.00 kHz, pin 6 6.38 Vpp, duty 45.8 % (slew shot). The digits are barely legible.

### Pot measurements
* JC 1979 Large Box RAT (`private/docs/web/jazzcaster_proco-rat-1979-largebox-potentiometer-value-check.md`,
  `.en.md`): DISTORTION 23.7 Ω at 0, 8.65 kΩ at mid, 85.6 kΩ at max (mid/max 10.1 %); TONE 104.0 kΩ / 56.6 kΩ /
  43.2 Ω (54 % at mid: linear, B); VOLUME 106.6 kΩ end to end, residuals 17.1 Ω and 77.2 Ω. Not a RAT2, and "mid" is
  not defined more exactly, but it is the only measured vintage RAT taper found: it supports a 10 % law for that
  era's DIST pot.
* KGB states, without a source, that "CTS pots are 10A", and suggests C11 went to 2.7 nF when the pots changed to a
  15A type. The 1979 DIST reading fits the first part; the C11 explanation is speculation.
* Holmes & van Walstijn, DAFx-19 (`private/docs/papers/holmes19_dafx_potentiometer_law.pdf`), and companion page
  (`private/docs/web/holmes_dafx19/`):
  * Alpha specifies each log law by the percentage of total resistance between terminals 1 and 2 at 50 % rotation
    (05A … 30A).
  * A linear-cubic piecewise function fits the specified log laws with 7 sections (peak error 2 %, for 15A) and the
    linear laws with 5. Fitted y at x = 0.51 (companion spec-fit table): 05A 0.057, 10A 0.111, 15A 0.162, 20A 0.210,
    25A(K) 0.259, 30A 0.311.
  * Measured Alpha A100k pots (`alpha_pots_csv/`, 0–300° in 15° steps): totals 98.1 / 83.4 / 100.4 kΩ; at 150°
    16.5 / 13.5 / 15.2 kΩ = 16.8 / 16.2 / 15.1 % of total; end resistance about 1.0–1.2 Ω. The measured log pots
    reach their flat top at lower rotation than the specification (paper §2.2.2). MATLAB code:
    `linearCubicPiecewise.m` (CC0).
  * For §7: modern Alpha "A" pots measure 15–17 % at mid, the 1979 RAT DIST pot 10 %. The linear-cubic law with the
    published transition points is a ready-made alternative to a plain exponential.

### LM108/LM308 internals
* AN29 (Widlar 1969, `private/docs/papers/ti_snoa624_an29_widlar_lm108.pdf`), National's LM108 design:
  * Superbeta NPN input pair with 6 µA total input-stage current at 25 °C; lateral-PNP differential second stage with
    active load; **complementary class-B output stage**, biased "on the verge of conduction" to remove the dead zone.
  * "The output swings to within a volt of the supplies … The output falls off rapidly as the current increases above
    a certain level and the short circuit protection goes into effect. The useful output drive is limited to about
    **±2 mA**."
  * The current limit works through different circuits for the two directions (positive-going: R10/Q17 remove base
    drive from Q18; negative-going: the drop across R11 forward-biases Q17's collector-base junction and clamps Q19's
    base). AN-29 gives no limit values, so whether the two limits differ is an inference.
  * With 30 pF (unity-gain) compensation: 1 MHz small-signal bandwidth, full output swing only up to 2 kHz, "a slew
    rate of 0.3 V/µs". Input protection is a transistor clamp pair between the inputs with no series resistors.
  * In the RAT the op-amp drives about ±2.9 mA into R8 and the diodes (ngspice, §3), more than AN-29's "useful"
    ±2 mA; a real LM308 may sag there where the LTC model (ISC ±6 mA) does not (inference).
  * 6 µA / 30 pF = 0.2 V/µs, the same Iee as the §2 macromodel estimate. This is arithmetic done in this study;
    AN-29 does not tie the slew current to the input-stage current.
* Shirriff (`private/docs/web/righto_inside-lm108-op-amp-superbeta.md`): a National-branded LM308 die matched the
  **Motorola** LM108 datasheet schematic, not National's (classic LM108 input stage, LM11-style second stage and
  current source, same output stage); Raytheon's LM108 differs again in second stage, output stage and biasing. AN-29
  therefore describes National's design only; RAT-era Motorola LM308s may differ inside (his footnote 6 raises this
  for distortion pedals).
* Solomon 1974 (`private/docs/papers/solomon74_jssc_monolithic_opamp.pdf`; NSC reprint
  `solomon74_monolithic_opamp_nsc_an-a.pdf`), §4–5:
  * Inverting slew rate is 2I1/Cc.
  * In non-inverting (follower) use, a large input step gives a fast step followed by normal slewing in one direction
    ("enhancement") and a slower slope 2I1/(Cc + Cs) in the other ("degradation"), Cs being the input-stage tail
    capacitance. For an NPN input stage the positive-going edge is enhanced and the negative-going one degraded; PNP
    is the reverse. Examples: LM118 100 → 70 V/µs; µA741 under 12 %.
  * The RAT stage is non-inverting and its input pair is driven far outside its linear range while clipping (§2), so
    this mechanism can make the two edges differ. For the NPN LM108 input it predicts a slower falling edge, but the
    NSC pulse graph and KGB show the falling edge faster: fit the asymmetry rather than assume its sign.

### Other models found
* Leete, Tarr & Ko, AES 152nd Convention, Engineering Brief 666 (2022)
  (`private/docs/web/aes_leete-tarr-ko_2022_ebrief666.md`; the paper is paywalled and was not read): adding the
  LM308's slew rate, GBW and supply limits to a RAT model "results in a more accurate recreation". The first author's
  MATLAB code (`private/docs/web/code/spicechateaudsp_aes-student-matlab-2022/`) chains:
  1. a linear (ideal op-amp) gain stage;
  2. a one-pole GBW low-pass set by the DIST pot and a per-op-amp capacitor (LM308: 3.5 nF);
  3. a per-sample hard slew limiter (LM308 0.2 V/µs);
  4. a hard clip to 0…9 V, all at 16× oversampling.

  This is the "hard slew limiter bolted on" structure that §2 expects to get the guitar-level transitions wrong.
* J. Falejczyk, LTspice RAT2 (2020, `private/docs/web/code/joshfalejczyk_proco-rat-2-ltspice/`): an ElectroSmash-based
  netlist converted to RAT2 values from the Multi-RAT drawing; the pots are replaced by fixed resistors.
* Dirk Hendrik's 2006 trace (`private/docs/schematic/dirk-hendrik/proco_rat.pdf`): LM308, 150k log gain pot, 1 µF
  output capacitor, LED switching.
* R. Strand's version table (`private/docs/web/diystompboxes_rstrand_rat_versions.md`, 2002) gives battery currents
  of 600 µA (−1.6 mA) for the original, 3 mA for the RAT2, 800 µA for the Vintage and 7.8 mA for the Turbo.
* Filed for reference, not yet reviewed here: Werner's 2016 thesis
  (`private/docs/papers/werner16_thesis_stanford_augmented.pdf`, §10 item 11), Holmes' 2019 thesis
  (`private/docs/papers/holmes19_thesis_qub_pedal_emulation.pdf`), the Qucs op-amp macromodel tutorial
  (`private/docs/papers/qucs_brinson_opamp_modelling_tutorial.pdf`), the MIT copies of the NSC LM108 and ADI OP07 rev. A
  datasheets, and DIYstompboxes topic 96351 (`private/docs/web/forums/diystompboxes_topic96351_lm308n-vs.md`).

### Status of blocked sources (§10 items 8 and 11)
* Now read, through Wayback Machine copies: freestompboxes t=890 (both pages) and t=12468, R. Strand's
  `rat_versions.htm`, DIYstompboxes topic 96351 (whole topic, from the print view) and Dirk Hendrik's
  `proco_rat.pdf`. Werner's thesis is downloaded. DIYstompboxes topic 80809 is still unread.
* Still blocked when fetched directly on 2026-10-09:
  * freestompboxes.org and diystompboxes.com: 403.
  * electrosmash.com: DNS does not resolve.
  * forum.pedalpcb.com: Cloudflare 403. Thread 29854 page 2 is not saved, and full-size attachments need a login
    (only 274×150 thumbnails are served).
  * tdpri.com: JavaScript challenge (HTTP 402 earlier, 202 now).
  * thefretboard.co.uk: challenge page, so the 2018 C11 = 2.7 nF report is not saved.
  * aes.org: 403, and the Leete/Tarr/Ko paper itself is paywalled.

  Details are in `docs/sources.md`.
