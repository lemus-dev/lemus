# The solver: MOSFETs, nodes that only leakage sets, op-amp comparators, speed

Four changes to `core/circuit.*` (the TR-909's copy of the TR-808 solver; the 808's is untouched):
- a MOSFET for the 909's CMOS parts in analog duty, with level-1 models of the 4069UB and 4011UB.
  These are the snare's and toms' VCO switches (4069UB inverters on rails that op-amps drive to
  ±V_cv: IC36, IC16, IC22, IC44) and the sample voices' clocks (4011UB gate astables);
- nodes that only junction leakage sets (the snare's accent gates at rest), solved without a node
  shunt;
- the op-amp comparators of the integrator-and-Schmitt VCOs (BD, SD, toms), timed to within
  0.01 % of ngspice at 88.2-96 kHz at a quarter of the CPU;
- linear mode with error-bounded windows and in-place relinearisation (section 8).

Sources (`private/docs/datasheets/`): **HD** Hitachi HD14069UB (`hitachi-hd14069ub.pdf`), **MC**
Motorola MC14069UB (`motorola-mc14069ub.pdf`), **TI** TI CD4069UB (`ti-cd4069ub.pdf`; Fig. 3
digitized in `ti-cd4069ub-fig3-digitized.json`), **TI11** TI CD4011UB (`ti-cd4011ub-2003.pdf`),
**TC** Toshiba TC4011UBP (`toshiba-tc4011ubp-1985.pdf`).

## 1. The MOSFET

    M<name> drain gate source <model>
    .model <name> NMOS(VTO= KP= LAMBDA= W= L=)      (and PMOS, VTO negative as SPICE's)

SPICE's level 1 (Shichman-Hodges) with the bulk on the source and GAMMA 0, no charge storage, no
gate current. Defaults are SPICE's: VTO 0, KP 2e-5, LAMBDA 0, W = L = 100u. LEVEL other than 1 is
refused; other parameters are ignored and not exported. In the device's sign frame (s = +1 NMOS,
−1 PMOS; v_gs = s·V(g,s), threshold V_T = s·VTO), with β = KP/2 · W/L and v_gst = v_gs − V_T:

- off, v_gst ≤ 0: I_ds = 0;
- linear, 0 < v_ds < v_gst: I_ds = β v_ds (2 v_gst − v_ds)(1 + λ v_ds);
- saturation, v_ds ≥ v_gst: I_ds = β v_gst² (1 + λ v_ds);
- v_ds < 0: drain and source swap (the same with v_gd for v_gs and −v_ds), I_ds < 0;
- plus SPICE's gmin (1e-12 S) across the bulk-drain junction, here drain to source.

The channel is the JFET's code (`channel()`, shared; the JFET's results are bit-identical). The
ports are the JFET's, g-s and g-d, with currents s·I_ds and −s·I_ds, so the gate takes none.
Newton limits a MOSFET's two ports together as mos1load does: fetlim on the gate voltage of
whichever end is the source, then limvds on v_ds (`TR909_NOFETLIM` turns it off; transient
iteration counts are the same either way, but the test VCO's DC solve fails without it). Linear
mode: a channel off at both ends is exactly linear (gmin only) up to V_T; a conducting one keeps
its ports within kLinWindow (2 mV). `toSpice` writes `.model <name> NMOS(LEVEL=1 VTO= KP= LAMBDA=
IS=0)` and `M<name> d g s s <name> W= L=`: no TOX, so no Meyer capacitances; no CBD/CBS/CJ, so no
junction capacitance; IS=0, so the bulk junctions carry gmin only, as here.

## 2. Nodes that only leakage sets; the DC solve; the deck

**The node shunt.** The solver had 1 GΩ (kGshunt) from every node to ground, mirrored by ngspice's
`rshunt=1e9`. At the snare's accent gates (Q41, Q42, 2SA1115: bases on islands of R257/D58 and
R256/C68/D61 that only the diodes' leakage lifts while Q39 is off) its nanoamps exceed that
leakage and turn the PNPs on at rest. Now kGshunt is only on nodes with no DC path to ground at
all, even through a junction or a channel (a node between two capacitors, a MOSFET's gate driven
through one); `toSpice` exports exactly those as `Rshunt_<node>` resistors and no `rshunt`.

**Anchors.** Without the shunts F (the ports' transfer impedances) would be singular or, at
1e-12 S, so large (1e12 Ω) that a port's residual sits on a floating-point floor above Newton's
tolerance. The floor was already over it with 1e9 Ω wherever an op-amp's output drives only
devices (the snare's VCO integrator, F 2.5e8 Ω: Newton stalled at 1e-4 and every step failed). A
port whose self-impedance in the DC network (every node shunted, as before) is over kAnchorR
(1e8 Ω) gets an anchor: kAnchorG (1e-5 S) across it in the linear network, which its device's
current gives back (deviceCurrents). The equations are the same; F stays under ~1e5 Ω there.
Chosen once, at the first build.

**gmin as SPICE places it.** A BJT's gmin now goes where bjtload puts it: into the base current
only, with the leakage terms, not the transport current; and an NPN has gmin from collector to
ground (ngspice's three-terminal BJT is a vertical NPN with the substrate on ground, a lateral PNP
without; measured on ngspice 47: at V_BE −5, V_BC −10 V an NPN's terminal currents are −15, +5,
+20 pA, 10 pA of it to the substrate). At pA-level nodes this decides the voltage: the snare's
gated-accent line at rest is 0.19593 V here, 0.19594 V in ngspice (0.165 V with the old placement).
The DC solve no longer adds gmin from every node to ground (SPICE doesn't).

The snare's rest (sd.cir with an ideal-switch stand-in for IC36), ENV4 at DAC codes 10 / 21 / 47:
−0.1903 / −0.2583 / −0.3117 V here, −0.1903 / −0.2583 / −0.3117 V in ngspice; with the shunts it
was −0.095 / +0.110 / +0.700 V (Q42 on). Q41's and Q42's bases rest at 14.9995 V, both off. The
whole netlist runs without Newton failures (the snare agent's 1e-12 S shunts failed every step).

**The DC solve.** After a direct Newton it now tries gmin stepping (a conductance across every
port, 10 mS down by decades to 0, half decades where a decade fails), then gain stepping (the
op-amps' open-loop gain from 1 to its own, the VCVS output stages and the limiters, following the
solution), then source stepping and the comparators' softening as before. A free-running VCO
has no operating point that Newton reaches from rest (ngspice fails these too): the state is then
left as it was, as before.

**The deck.** `toSpice` writes the state as `.nodeset` (after a converged DC: without rshunt,
ngspice's gmin and source stepping fail at the leakage-defined nodes) or, where solveDc failed, as
`.ic` with `UIC` on `.tran`, so that ngspice starts from the solver's state rather than failing
its own operating point. `cshunt` (ngspice's capacitance from every node to ground, which the
solver doesn't have) is 1e-13 F, not 1e-12: 1 pF ran the snare's VCO 0.06 % slow, and 0.1 pF still
steadies ngspice at a trigger's edge (the toms' IC15b: "timestep too small" at 10 fF, BD's Q1 with
none). Where nanosecond timing matters it still shows: the cymbal clocks' reference decks strip it
(`tools/spice/cases/cymbals.py`).

## 3. Op-amp comparators (the VCOs' Schmitt triggers)

The integrator-and-Schmitt VCOs (`X... opamp {gbw} 1e5 {opsat} {sr}` with 100k/100k or 100k/47k
positive feedback) flipped early at board rates: before the anchors by up to most of a half period
(the SD agent's VCO-2 reproducer: turning points from +0.33 to +7.05 V, 395 Hz for 335.9 Hz); with
them by 5-10 µs a flip (0.6 % sharp at 96 kHz), and their partitions were most of a busy kit's CPU
(4.4-6.6 Newton iterations a sample, never linear). The cause, against ngspice at fine steps: the
op-amps' poles are integrated by backward Euler (needed in negative feedback, where the trapezoidal
rule maps the MHz closed loop near z = −1 and rings), which starts a comparator's slew up to a step
early and, with 2.2 V/µs × 10.4 µs of slew a step, allows a second, "already flipping" solution of
the step's equations. At 2 MHz, making only the comparator's pole trapezoidal takes the SD
reproducer from 0.7 µs early a flip to ngspice's period within 0.03 µs; the integrator's pole's
method doesn't matter.

- **Which op-amps are comparators.** At the first build, for each op-amp with a slew rate and an
  output limit, the feedback from its output to its input difference in the sample-rate network
  (the devices' ports open, the capacitors as their companions): β = F(input stage, out) /
  F(out, out). β over −0.01 (positive feedback, a Schmitt trigger's +0.28 to +0.5, or an open
  loop's 0) makes it a comparator; amplifiers, integrators and followers are −0.3 to −1. The 909's
  netlists have them only in the VCOs (BD 1, SD 2, each tom 3; the same per partition); the 808's
  have none. `TR909_NOFLIPS` turns it off.
- **Its pole by the trapezoidal rule.** No negative feedback holds it, so nothing rings; a full
  reversal of its input stage's current within one step moves the pole by nothing (the rule
  averages the step's two ends), so the spurious second solution is gone.
- **Its flip in sixteenths.** A sample in which a comparator flips (its input stage's current
  reverses by over imax, or its output moves over kFlipVolts, 0.5 V) is redone in kFlipSteps (16)
  steps; one whose input will cross by the last two samples' trend goes there directly (a full
  step would fail at the flip); if the sixteenths fail, quarters as before. The SD reproducer's
  period at 96 kHz: 0.079 % long redone in quarters, 0.008 % short in sixteenths, 0.003 % long in
  sixty-fourths.
- **Linear mode when saturated.** A slew device past kSlewSat (10) linear ranges carries imax to
  4e-9 and may stay so; a limiter in its anti-windup's linear part with its output four knee widths
  past the knee keeps a window in which tanh's curvature stays under kLinTolerance. A comparator in
  a rail no longer stops linear mode; a run that ends at a comparator's window (the VCO's half
  period) re-enters four samples later, and a run of over kLinPays (16) samples (it paid for its
  linearisation) re-enters at once instead of only one of over 256.

VCO frequency against ngspice at 2 MHz steps (cshunt 0 or 1e-15 F; half periods from tri's zero
crossings, ours against ngspice's matched by time):

| circuit | ngspice | before | now, 96 kHz | now, 88.2 kHz |
|---|---|---|---|---|
| SD agent's reproducer (VCO-2 alone, JFET stand-in), at rest | 335.93 Hz | +17.6 % (395.1 Hz) | −0.008 % | +0.003 % |
| BD (bd.cir, bd_21) at rest, half periods 9.2 ms | — | −0.058 % (worst −0.80 %) | −0.007 % (worst −0.07 %) | −0.007 % (worst −0.012 %) |
| BD after the hit (the sweep), 7.0 ms | — | −0.18 % (worst −0.37 %) | −0.004 % (worst −0.012 %) | +0.001 % (worst +0.019 %) |
| test_vco.cir (MOSFET switch), 2.6 ms | — | −0.18 % (worst −0.75 %) | −0.010 % (worst −0.016 %) | |

CPU, `tr909voicerun --seconds 2` with a busy kit (BD ×3, LT ×2, MT ×2, HT ×2, SD ×2, RS, HC;
`TR909_PROFILE=1 TR909_BLOCKS=1`): the VCO partitions 9.80 s → 2.18 s per 2 s of audio, all blocks
14.17 → 4.74 s. A tom's VCO partition is now 0.15-0.36 s (0.9-1.8 Newton iterations a sample,
80-85 % linear; before 0.86-1.53 s, 4.4-6.2, 0 %), BD's 0.13 s (0.66 s), the snare's 0.012 s
(0.04 s).

## 4. Tests against ngspice

`tools/spice/cases/cmos.py` (in the suite) on four test circuits, not parts of the 909, in
`core/netlists/`: `test_inverter.cir` (two 4069UB inverters, one unloaded: transfer curve and
supply current; one into 100 nF ∥ 10k), `test_ringosc.cir` (three inverters, 47k and 22 nF a
stage, ~300 Hz at 5 V), `test_vco.cir` (the snare's VCO-1 topology: op-amp-driven ±cv rails
through 1k, input diodes, Schmitt comparator, integrator) and `test_nand.cir` (a 4011UB NAND, the
series NMOS's middle node tied only through the channels). 96 kHz; inputs ramp over whole samples
on the 96 kHz grid (the squares over 8: a gate switching inside one sample is a sub-sample event
each solver resolves its own way, 1 % RMS at 15 V into 100 nF).

| case | output | range | max % | RMS % |
|---|---|---|---|---|
| cmos_inv_dc5 / dc10 / dc15 (the transfer curves, 0 → V_DD → 0 over 0.2 s) | outa, outb, idd | 5-15 V | ≤ 0.004 | 0.000 |
| cmos_inv_sq5 (500 Hz square, 8-sample edges) | outb (100 nF ∥ 10k) | 4.78 V | 0.21 | 0.054 |
| | outa (10 nF) | 5.0 V | 1.70 | 0.22 |
| cmos_inv_sq15 | outb | 14.8 V | 0.69 | 0.10 |
| | outa | 15.0 V | 5.46 | 0.59 |
| | idd (supply current) | 15 mA | 17.3 | 1.67 |
| cmos_ring5 / ring10 (~300 Hz at 5 and 10 V) | a (a stage's capacitor) | 3.0 / 5.6 V | 0.11 / 0.07 | 0.042 / 0.028 |
| | o1 (an inverter's output) | 5.0 / 9.7 V | 1.6 / 0.6 | 0.14 / 0.060 |
| cmos_vco (cv 2.5 V, ~190 Hz) | tri | 20.3 V | 0.066 | 0.035 |
| | sq (the comparator) | 27 V | 21 | 0.72 |
| | sw (the inverter's output) | 4.9 V | 5.9 | 0.11 |
| cmos_vco_sweep (cv 2 → 4 → 2 V) | tri | 20.3 V | 0.057 | 0.035 |
| | sq / sw | 27 / 7.8 V | 24 / 58 | 0.82 / 1.15 |
| cmos_nand_dc | out, mid | 4.7, 2.9 V | 0.007 | 0.000 |
| cmos_nand_sq (700 and 300 Hz squares) | out | 4.8 V | 2.95 | 0.26 |
| | mid (the NMOS pair's middle) | 2.9 V | 3.9 | 0.75 |

The device equations match ngspice to the tolerance (the transfer curves, the ring's and VCO's
capacitor voltages). What exceeds 0.5 % RMS is a fast edge inside a sample: the inverters' output
swing and shoot-through current while the input crosses the switching point in a fraction of a
sample, the comparator's square and the rails' spikes at its flips, the NAND's floating middle
node. The ring and the test VCO run at ngspice's frequency within 0.005 % at 96 kHz (the rest of its
difference was the old 1 pF cshunt: 0.000 % without it at 384 kHz).

## 5. The 4069UB (HD14069UBP), and the 4011UB

In `core/netlists/models.cir`. One inverter is an N and a P; level 1 has one β and one V_T per
device, so it can't follow a 4000-series device's mobility falling with V_GS over 5-15 V (HD's
sink current at 0.4-1.5 V grows 2.6× from 5 to 10 V and 3.9× from 10 to 15 V; a square law fitted
to 5 and 10 V puts V_T near 0 V, against TI's Fig. 3 where the supply current starts at an input of
1.8 V). So two fits, each for its range; the 909's 4069UBs all run on the VCOs' ±V_cv rails, 2-10 V
across.

**Data.** HD p.2, typical at 25 °C (MC's typical table is the same to the digit): I_OL 0.88 / 2.25
/ 8.8 mA at 5 / 10 / 15 V (V_OL 0.4 / 0.5 / 1.5 V); I_OH 4.2 mA (5 V, V_OH 2.5 V), 0.88 (5 V,
4.6 V), 2.25 (10 V, 9.5 V), 8.8 (15 V, 13.5 V); t_PLH = t_PHL 65 / 40 / 30 ns and t_r = t_f 100 /
50 / 40 ns into 50 pF; C_IN 5 pF. MC adds the formulas against C_L (t_P = 0.90 ns/pF C_L + 20 ns at
5 V, 0.36 + 22 at 10 V, 0.26 + 17 at 15 V; transitions 1.35 + 33, 0.60 + 20, 0.40 + 20). TI's
typical curves (Fig. 3 supply current and output against input at 10 and 15 V, digitized; Fig. 4,
6 drain curves; Fig. 8 t_P against C_L) give the shape: the current's onset (V_T), λ and the delay
slopes. TI's own table is ~15 % stronger than HD's at 5-10 V; its curves are scaled by 0.87.

**Fit** (`scipy.optimize.least_squares` on log current; HD's table points weighted 10, TI's curve
points 0.3 each; λ held at 0.03 for the low fit, free for the 15 V one):

| model | V_T | β (mA/V²) | λ | for |
|---|---|---|---|---|
| 4069UB_N | 1.15 | 0.275 | 0.03 | 3-10 V |
| 4069UB_P | −1.03 | 0.275 | 0.03 | 3-10 V |
| 4069UB15_N | 1.32 | 0.195 | 0.106 | 15 V |
| 4069UB15_P | −1.15 | 0.194 | 0.106 | 15 V |

| quantity | sheet | 4069UB | 4069UB15 |
|---|---|---|---|
| I_OL 5 V, 0.4 V | 0.88 mA | 0.81 | 0.57 |
| I_OL 10 V, 0.5 V | 2.25 mA | 2.40 | 1.73 |
| I_OL 15 V, 1.5 V | 8.8 mA | 11.3 | 8.76 |
| I_OH 5 V, 2.5 V | 4.2 mA | 4.03 | 3.18 |
| I_OH 5 V, 4.6 V | 0.88 mA | 0.84 | 0.59 |
| I_OH 10 V, 9.5 V | 2.25 mA | 2.44 | 1.76 |
| I_OH 15 V, 13.5 V | 8.8 mA | 11.4 | 8.83 |
| t_PHL 5 V, 50 pF | 65 ns (MC: 45 of it C_L's) | 34 | 41 |
| t_P slope 5 V | 0.90 ns/pF (MC); TI Fig. 8 0.57 | 0.57 | 0.69 |
| t_THL 5 V, 50 pF | 100 ns | 67 | 89 |
| t_T slope 5 V | 1.35 ns/pF (MC) | 1.34 | 1.77 |
| t_PHL 15 V, 50 pF | 30 ns | 11 | 10 |
| switching point | V_DD/2 (TI Fig. 2) | 2.56 V at 5 V | 7.6 V at 15 V |
| peak supply current, 10 V | 4.6 mA (TI Fig. 3) | 4.8 | 4.2 |
| transfer gain | sharp at 5 V, soft at 15 V (TI Fig. 3) | 51 (5 V), 13 (15 V) | 19, 5 |

As switches (the VCOs, deep in the linear region): R_on = 1 / (2β (V_GS − V_T)) is 2.1 kΩ (N) and
1.9 kΩ (P) at V_GS 2 V, 640 / 610 Ω at 4 V, 205 / 200 Ω at 10 V: against the 47k into the
integrator, 4.5 % to 0.4 % of the integrating resistance. HD's minimums are half its typicals
(0.44 mA at 5 V), so between units the inverters alone may move a VCO by about ±1 % at low TUNE.

**4011UB** (TC4011UBP: IC57, IC59 crash and ride clocks, IC72 the hi-hat's). 4011UB_N/_P are the
cymbal agent's fit (`docs/circuit/cymbals.md`, "Clock"; models.cir): TC gives minimums only and the
MC14011UB's and CD4011UB's typicals differ threefold, so it is fitted to the CD4011UB's typical 5 V
drive and checked on the hi-hat's astable (31.8 kHz against the 31.57 measured): V_T 2.06 / −2.04
V, β 0.90 / 0.187 mA/V², λ 0.05. A gate is two of each, the P in parallel, the N in series
(`test_nand.cir`). crclk.cir and rdclk.cir run them; at 100 MHz the clock's period is within
0.007 % of ngspice, with cshunt stripped from those reference decks (0.1 pF on the astable's
nodes moves it by about 0.1 %).

**Input diodes.** The MOSFET has no gate current: a CMOS input's protection diodes (TI Fig. 10: one
to V_SS, two in series to V_DD behind a resistor) go in the netlist as D elements; `CMOS_DIN`
(IS 1e-14, N 1) is a generic junction (no sheet gives them).

## 6. Limits

- **No charge, so no delay of its own.** Level 1 has no gate or junction capacitance: an inverter's
  delay here is its drive into whatever capacitance the netlist gives its output (34 ns into 50 pF
  at 5 V); the sheet's ~20 ns more (MC's intercepts: the gate's own charge and the input network)
  is missing. In the snare's and toms' VCOs (100-350 Hz, periods 3-10 ms) the switch flips twice a
  period: 2 × 20-30 ns is 0.002 % of the period at 350 Hz (0.03 cent): no RC is needed for the
  inverter itself. The input capacitance through the 100k from the comparator (R259; the snare has
  three inputs in parallel, 15 pF at HD's 5 pF each: τ 1.5 µs) delays each flip by about 0.3-0.5
  µs, 0.03 % at 350 Hz (0.5 cent), 0.01 % at 117 Hz; a capacitor on the gate node brings it in.
- **Gate astables.** A two-gate CMOS RC astable (the 4011UB sample clocks) switches by an algebraic
  jump when the gates have no capacitance: Newton finds no solution past the fold and the state
  freezes (a 4069UB version at 96 kHz failed every step from its first flip; ngspice runs it only
  with its cshunt). These need the pins' capacitance in the netlist and nanosecond steps, as
  `crclk.cir`/`rdclk.cir` have them (Cymbal runs them at 100 MHz). Their 60-100 ns gate delays are
  0.5-1 % of 16-36 µs periods, so the missing intrinsic ~20-30 ns is worth ~0.2-0.5 % there.
- **Buffered gates.** A 4070B (the noise clock IC31) is three stages; one level-1 inverter can't
  stand for it.
- **Fits.** One level-1 device can't follow mobility over 3-15 V: the low models are 28 % strong at
  15 V, the 15 V ones 35 % weak at 5 V. The tables are typicals; HD's minimums are half of them.
- **VCO CPU.** The flips now are most of what a VCO costs: about 60 % of a free-running snare
  VCO's time, and with the trigger edges' substeps most of the toms' and the snare's busiest
  512-sample block at a hit (section 8). A flip takes two or three samples in sixteenths at about
  3.5 Newton iterations each: the regenerative start, then the output's slew into the rail. The
  flip as an event at its located instant, with steps of any length, was tried and dropped
  (section 8), as were quarters with only the corners' quarters in sixteenths.
- **Oscillators' RMS against ngspice** grows with the run as the phases drift; the cases are short.

## 7. What it moved

`tools/spice/suite.py` over the final netlists (BD, HH, crash/ride, RS/HC, SD, toms, mix; SD with
its inverters still placeholders), RMS % of each output's range against ngspice, before and after
(the decks differ as section 2 says), and how far the solver's own output moved:

| cases | before | after | moved (RMS) |
|---|---|---|---|
| BD (7 cases), out | 0.21-0.87 % | 0.015-0.080 % | 4.6-6.6 % (the VCO now on pitch) |
| LT, MT, HT (15), out | 1.06-5.82 % | 0.019-0.092 % | 0.2-5.7 % |
| SD (9), out | 0.05-0.59 % | 0.004-0.39 % | 2.6-7.7 % (the rest state: Q42 off) |
| HC (6), out | 0.002-0.51 % | 0.002-0.65 % | 0.01-0.86 % (one of the clap's bursts lands a few µs apart in either version) |
| tom noise (3) | 0.11-0.23 % | the same | 0.002 % |
| RS (5), HH (12), crash/ride (10), mix (5) | | within 0.03 % of before | ≤ 0.1 % |

Newton failures: 7 cases have one isolated failed sample (0 before). The 808's netlists through
this solver (its suite's 71 cases, the solver's own output): BD, the toms and congas, rim shot and
claves move by 0.003-0.05 % RMS, the maracas 0.017 %, the clap 0.01-0.24 % (off0: 2.0 %); the
metal voices (hats, cymbal) 0.5-4.5 %, as far as they already differ from ngspice (oh_a 4.66 → 4.64
%, ch_n 2.33 → 2.29 %, cy_a_short 4.36 → 4.37 %, cy_decay0_tone1 1.61 → 1.56 %, cp_off0_a 0.038 →
0.023 %): edge timing that any change at the tolerance moves, against ngspice the same or closer.
The 808 has no op-amp comparators.

## 8. Speed

The changes of 2026-10-17 for CPU (`core/circuit.*`; the toms' `.part` lines):

- **Linear mode's windows bound the error where it lands.** A device's window is where its
  linearisation's current error, times zeff, stays under kLinTolerance. zeff is the most a
  current at that port moves any port, as Newton judges the ports (weighted): the largest entry
  of its column of (I − F G)⁻¹ F, with the devices as linearised. The port's own impedance
  (−F_pp, as before) missed three cases:
  - an off junction near zero bias in a node that only junctions hold (the toms' line A, 1 mV a
    step off Newton);
  - a VCA's current into an op-amp's transimpedance (0.3 mV a step at the toms' C503);
  - a transistor's collector into a floating base (the crash VCA's Q71, 30 mV).

  Further changes to the windows:
  - A junction's window comes from the exponential's exact departure from its tangent either way,
    is e^(v0/nV_T) (e^u − 1 − u), so deep in reverse it reaches down as well as up.
  - A transistor's junctions are judged by the larger zeff of its two ports.
  - An OTA's I_abc and input pair are judged by their effect on the output.
  - An op-amp in feedback in its knee gets a window from tanh's curvature. A comparator's knee is
    its flip, which Newton takes. Before this the toms' accent amplifier, clipping at full accent,
    sent `trig` (24 ports) to Newton for the whole decay: 40 % of a tom's busiest block.

  kLinTolerance is now 100 µV (10 µV on the looser measure before). Every linear step of a busy kit
  was checked against Newton's for the same step (`TR909_LINVERIFY`). The capacitors came within
  2× the tolerance of Newton's, except BD trig's 10 pF island on Q1's emitter (1 mV).
- **Relinearisation in place.** A device that leaves its window in a linear step is linearised
  again where the step put it, and the ports are worked out again.
  - The update is rank k (k = its ports) and goes to the step's matrices directly: K, kv, the
    states' and outputs' rows, and (I − F G)⁻¹ F, by Woodbury.
  - Limits: up to 4 devices and 2 rounds a step; the whole circuit afresh after 256 updates.
  - Refused (the step then goes to Newton) where Newton itself wouldn't step to: a junction more
    than 2 V_T past its critical voltage, a channel 0.5 V away, an update that leaves
    I − F G nearly singular, any port beyond 10 kV.
  - A circuit of up to 8 ports whose updates come at over 0.75 a step (a VCA through a hit) takes
    Newton instead until the rate falls: there Newton is cheaper.
  - The VCAs' transistors, the envelopes' diodes and the shapers now stay in linear mode with an
    update every few samples. Before, they ran Newton every sample.
- **Newton compiled for 12, 14, 15, 18, 19 and 24 ports**, LU for up to 16 active ports.
- **Flips:**
  - A comparator's full-sample trial is capped at 8 iterations: a sample that hard is a flip's,
    and goes straight to its sixteenths.
  - The sample after a redone one, while a comparator is still between its rails, skips the
    trial.
- **The toms' `out`** is cut into `shape1`, `shape2`, `shape3` and `vca` at the shapers' 2.2k into
  the 47 Ω nodes (R165, R176, R167; toms.md §8).

**SPICE** (`tools/spice/suite.py`, all 82 cases, whole netlists, against the solver before):
- Of 291 outputs, 47 are closer to ngspice by over 0.005 points of RMS % and 9 are further.
- The outputs and jacks moved between −0.105 and +0.015 points: mt_21 0.050 → 0.028 %, bd_10
  0.030 → 0.009 %, hc_tm0 0.015 → 0.030 %.
- The further ones: cmos_vco's sw, one sample at an edge on the other side of the grid
  (0.105 → 1.355 %; its crossings are unchanged to 0.2 µs, its tri 0.035 → 0.018 %); SD tune1's
  free-running tri1 (phase); hc, hh and ht by under 0.015 points.

VCO periods against ngspice as in section 3:
- BD at rest −0.007 % (96 kHz) and −0.006 % (88.2 kHz).
- BD after the hit −0.003 % and +0.002 %.
- The SD reproducer −0.007 % and +0.003 %.

**Tried and dropped:**
- **Flips in quarters, with only the quarters that hold a corner in sixteenths.** A corner is a
  junction through 1 µA, a channel through its threshold, an input stage into or out of
  saturation, an output into or out of its knee. The cmos_vco's inverter then switched a sample
  early at every other flip, because the regenerative start inside the input stage's linear range
  is too fast for a quarter step (sw 0.1 → 2.8 % against ngspice; BD tri 0.32 → 0.43 %).
  Refining the quarters where the input stage is short of saturation fixed that, but then nearly
  every slewing quarter is refined, and it ran 8 % slower than all sixteenths.
- Straight-line starts for substeps (more iterations).
- Four running sums in the step's products (slower).
- Plain Newton or a looser tolerance in substeps (under 3 %).
- Fewer edge substeps (8 → 2 samples in quarters: under 3 %).
- Error bounds on the capacitors' and outputs' change as well as the ports': 5-10 % slower, and
  the suite and the bench didn't need it.

**What it bought.** `tr909bench` before and after, at 44.1 / 48 kHz. Wall is the blocks' time
against the audio's; worst is the slowest 512-sample block against its length; parallel voice
jobs, Apple M1 Max:

| workload | wall before | wall after | worst before | worst after |
|---|---|---|---|---|
| busy kit | 62 / 65 | 41 / 44 | 126 / 141 | 73 / 77 |
| every voice | 120 / 126 | 69 / 74 | 224 / 234 | 119 / 130 |
| busy kit, knobs turning | 62 / 65 | 43 / 46 | 127 / 148 | 74 / 81 |
| every voice once, decays up | 89 / 93 | 48 / 51 | 296 / 313 | 141 / 144 |
| LT | 62 / 65 | 33 / 35 | 107 / 126 | 58 / 63 |
| MT | 65 / 69 | 34 / 37 | 112 / 128 | 59 / 63 |
| HT | 69 / 73 | 37 / 39 | 125 / 133 | 65 / 71 |
| SD | 48 / 50 | 37 / 39 | 77 / 86 | 58 / 67 |
| BD | 17 / 18 | 16 / 17 | 43 / 48 | 34 / 36 |
| RS | 10 / 11 | 10 / 11 | 28 / 27 | 24 / 26 |
| HC | 13 / 15 | 12 / 13 | 24 / 25 | 22 / 23 |
| CH | 12 / 13 | 13 / 13 | 32 / 32 | 32 / 32 |
| OH | 16 / 17 | 16 / 17 | 30 / 34 | 29 / 31 |
| crash | 17 / 18 | 19 / 20 | 24 / 26 | 25 / 27 |
| ride | 15 / 16 | 17 / 18 | 23 / 24 | 28 / 29 |
| idle | 8 / 6 | 6 / 6 | 37 / 49 | 34 / 39 |

All the workloads' cycles together: 123 → 91 G a second (−26 %). The crash and ride run 10-20 %
dearer: their VCAs relinearise often, but under 0.75 a step.

The sound, against the solver before (`tr909bench check`; the envelope in 50 ms frames, 1/6-octave
bands): envelopes within 0.54 dB and spectra within 0.74 dB, except two cases. LT and HT with TUNE
and DECAY up reach 2.6 dB in single bands, alternating at the three VCOs' partials. The idle kick's
dying tail differs by 1.1 dB in one band at 48 kHz. There both solvers are
off the fine reference (the same solver with the flips in sixty-fourths and no linear mode): the
one before by 2.9 dB, this one by 1.9 dB. The idle board sleeps to exact zeros as before.

### The second pass (the same day)

Aimed at the toms' and the snare's busiest block. Where that block's time went before it (LT,
the second hit; per-step timing): relinearisation 27 % (the snare 42 %), the VCOs' flips 16 %, the
trigger edges' quarters and sixteenths 21 %, linear steps 18 %, and failed attempts to linearise
during a flip 5 %. The changes:

- **MOSFETs get windows from their own curvature.** A conducting channel had the fixed 2 mV
  window. Its current is β (x² − y²)(1 + λ (x − y)) in the overdrives (β x² (…) pinched off), so
  its second derivatives stay under 2 β K, K = 1 + λ (2 |v_ds| + 4 (|x| + |y|) + 6). On a window
  w at both ports the error is under β K w², and it moves the ports by at most the two ports'
  zeff together: w = √(kLinTolerance / (β K z)), between 0.1 mV and 0.5 V. Off at both ends stays
  exact as before.
- **An op-amp past its linear range stays in linear mode.** Between its linear range and
  saturation (a noise source's buffer, a VCA's output stage moving fast) it went to Newton with a
  doubling wait, so the toms' noise VCA ran Newton for half the busiest block and the snare's
  `snappy` for a fifth of it. Now it gets a window from tanh's departure from its
  tangent, under (I_max / range²) g w², g = tanh·sech² at the window's far end, times zeff. A
  comparator's input stage there is its flip, which Newton takes as before.
- **The states' and outputs' rows from the currents** while updates come often. Each update had to
  carry the rows (M, mConst: nr × (np + 2 w1) multiplies a port). Past a rate set by the
  circuit's sizes (about 0.1-0.25 updates a step) they are left as they were and worked out each
  step from S and the devices' currents as linearised (nr × np more a step); M is rebuilt once
  updates become rare. An update costs about a third less (the VCAs' transistors, snappy's noise).
- **A comparator slewing is caught before the matrices.** Every flip tried to linearise every
  other sample and found the comparator slewing only after building them (np LU solves); the
  test needs only the port's voltage, so it now comes first. 8 % of LT's cycles.
- **The sample after a redone one skips its trial only while a comparator is really flipping**:
  its output short of the rail, or its input within two linear ranges (it was ten). One of the
  snare's VCOs redid every sample of its input's last 10 ranges to the threshold in sixteenths, at
  40 iterations each: half its flip samples, a third of its flip iterations.
- **Four rows a pass** in the step's matrix products (P w, S w, K w, M w) and Newton's residual,
  each row summed in a plain loop's order; a rank update's row in one pass over its k terms.
  6-8 % of everything.

Checks: `tools/spice/suite.py` (82 cases, 291 outputs, against the first pass): 10 closer to
ngspice by over 0.005 points of RMS %, 3 further (cmos_inv_dc5 outb 0.000 → 0.009 %, mt_21 tune 1
decay 0 out 0.013 → 0.021 %, bd_21 tune 0 tri 0.149 → 0.155 %), every output within ±0.01 points;
the most moved is the snare's PULSE line at the other VCO's flips (0.05 % RMS). VCO periods as in
section 3: unchanged (BD at rest −0.0069 → −0.0065 % at 96 kHz, the rest identical). The
plugin's tests pass.

The sound against the first pass (`tr909bench check`): envelopes within 0.09 dB, spectra within
0.1 dB except two. LT with TUNE and DECAY up at 48 kHz, 1.1 dB in single bands 30-35 dB down
(the MOSFETs' windows; against the fine reference both are 1.6-1.7 dB off, this one slightly
closer). The idle kick at 48 kHz, 1.2 dB in bands 30 dB down and −24 dB at its attack: the BD's
VCO free-runs from power-on, starting off the DC point by its rounding (a part in 10⁹ at the 23rd
sample, growing about 1.2× a sample), so its phase when the kick comes moves with any rounding
change; the four-row products alone do it.

**Tried and dropped in this pass:**
- **The flip as a located event, with steps of any length.** The VCO partitions' step matrices
  were changed for a step h by a low-rank update (the capacitors' companion conductances:
  S(h) = S0 + N diag(dg) Nᵀ, by Woodbury), the slow stretches stepped to the next corner (a
  channel's threshold, a limiter's knee, a junction through 1 µA) as the rates predict it, the
  fast ones in sixteenths. Two variants: both were slower than the sixteenths (2.5 % more
  cycles on LT, 3.4 % on the snare), and the cmos_vco's half periods came out 0.37 µs short, from clamp diodes' turn-off
  corners that the rates don't locate. Locating those too would need the junction corners, which
  in the first variant cut the steps to nothing.
- Relinearising a little ahead of where the step put the port, along its last step: far more
  exits to Newton, 25 % slower.
- More devices or rounds of updates a step (8 devices, 3-4 rounds) and a shorter run counting as
  paid (kLinPays 2-8): within 2 %.
- Newton after a linear run starting from its last two steps (the straight line, and flipAhead
  seeing the flip): no faster, and the toms moved by −32 dB.

**What it bought** (`tr909bench`, wall % / worst %, each "44.1 / 48 kHz", Apple M1 Max; before is
the first pass):

| workload | wall before | wall after | worst before | worst after |
|---|---|---|---|---|
| busy kit | 41 / 45 | 34 / 37 | 73 / 77 | 57 / 62 |
| every voice | 69 / 73 | 62 / 66 | 121 / 131 | 104 / 112 |
| busy kit, knobs turning | 43 / 46 | 36 / 38 | 76 / 81 | 59 / 65 |
| every voice once, decays up | 48 / 52 | 43 / 45 | 141 / 147 | 114 / 119 |
| LT | 33 / 35 | 27 / 29 | 58 / 62 | 47 / 50 |
| MT | 35 / 37 | 29 / 31 | 58 / 64 | 48 / 52 |
| HT | 37 / 39 | 31 / 33 | 64 / 71 | 53 / 56 |
| SD | 37 / 39 | 29 / 31 | 58 / 66 | 43 / 48 |
| BD | 16 / 17 | 13 / 14 | 34 / 37 | 30 / 30 |
| RS | 10 / 11 | 9 / 10 | 24 / 26 | 21 / 25 |
| HC | 12 / 13 | 11 / 12 | 23 / 23 | 22 / 22 |
| CH | 13 / 14 | 12 / 12 | 32 / 32 | 29 / 28 |
| OH | 16 / 17 | 15 / 16 | 29 / 31 | 26 / 28 |
| crash | 19 / 20 | 17 / 18 | 26 / 27 | 24 / 25 |
| ride | 17 / 18 | 16 / 17 | 28 / 30 | 26 / 27 |
| idle | 7 / 6 | 6 / 5 | 35 / 39 | 32 / 35 |

All the workloads' cycles together: 91 → 79 G a second (−13 %). `tr909voicerun`, a hit at codes 21
and 47 in a second, cycles at 3.23 GHz, the whole second / the busiest block: LT 44 → 37 % /
65 → 48 %, MT 46 → 39 % / 68 → 48 %, HT 48 → 41 % / 71 → 51 %, SD 49 → 40 % / 68 → 48 %.
