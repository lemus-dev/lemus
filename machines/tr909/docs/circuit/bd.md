# Bass drum (bd.cir)

The BASS DRUM of the voicing board 73133090, from its TRIG latch output, its accent DAC and the
noise bus to its output at the connector W5 to the multi-jack board. Sources as `docs/circuit.md`
§0: **SN** p.11 (crop `private/docs/schematic/crops/p11-bd-bass-drum.png`; the full page for the
connections), p.6 (circuit description: "basically the same manner" as the snare), p.7 (change
information), p.9 (MULTI OUT waveforms), p.10 (address map), p.12 (multi-jack board); **NAVA**
(`Nava_v1.02_netlist.txt`, sheet BassDrum); **OLD**, the hand-written model's header
`private/reference/old-models/bd.h` (its .cpp is a stub); **RES**, `docs/research.md` §2
"Bass drum". Every value below was read on the SN crop at 400 dpi and agrees with NAVA unless a note
says otherwise. Address map (SN p.10): IC11a M-12, IC11b M-6, IC12a N-8, IC12b N-9, IC13a M-10,
IC13b M-11; Q1 N-6, Q2 N-7, Q3 L-7, Q4 L-7, Q5 M-7, Q6 L-10, Q7 N-7, Q8 M-8, Q9 M-9, Q10 M-9,
Q11 N-10, Q12 M-12; VR2 M-8, VR3 L-10, VR4 M-12, VR5 M-11. Transistors are 2SC2603 (NPN) and
2SA1115 (PNP), diodes 1SS133, op-amps M5218L (SN p.11 legend; NAVA's parts agree, Q-numbers and
polarities included).

Signal order: TRIG → Q1 → Q2, which lifts the line **n2** to +15 V for the trigger; n2 charges the
pitch envelope (C9), the hold timer (C7) and switches the attack pulse (Q8). Q1's collector **n1**
gates the accent into ENV2 (Q5) and, through C1, resets the VCO (Q7). The VCO (IC12, IC13) runs a
triangle whose rate C9 sets; it is rounded by D10/D11 into the body VCA Q12 (ENV1, C8); the pulse
through the Q9 Sallen-Key and the low-passed noise go into the attack VCA Q6 (ENV2, C12). IC11a sums
the two VCAs' currents.

## 1. Trigger: Q1, Q2, Q7 (grid N-6 to N-7)

- **trig** is IC1 pin 10 (TRIG1, the 4174 on +5 V; RES C.4: bit 1 BD), through `rlatch` 600 Ω
  (the latch's output resistance, `interface.md` §1) → **R1 22K** → Q1's base, **R2 10K** to ground.
  Q1 (emitter grounded) draws 0.13 mA of base current and saturates.
- Q1's collector is **n1**. At rest it sits near +15 V, pulled up through **D4** (anode at node x4)
  from **R3 100K** and **R4 47K** (+15 V to Q2's base); nothing else holds it (D5 is reversed, C1 is
  open), so n1 is a node joined only through junctions and capacitors.
- At x4, **R11 4.7K** + **C6 0.0022** to ground. When Q1 lets go, x4 has to climb back through R3
  before D4 conducts and Q2's base current stops: Q2 releases n2 about **0.3 ms after** the trigger
  ends (the model's n2 falls at 2.3 ms for a 2.0 ms trigger).
- **Q2** (PNP, emitter at +15 V; base between R4 and R3) saturates while Q1 conducts: its collector
  **n2** goes to +14.9 V, loaded by **R19 2.2K** to ground and by the three charging paths (D7/R21
  into C9 draws 6 mA at first, D6/R20 into C7 2 mA, R5 into Q8's base).
- **C1 0.068** from n1 → **R12 100K** → the base of **Q7** (PNP, emitter at +15 V, **R13 47K**
  base to emitter). When n1 falls, C1 pulls Q7's base end down: Q7 conducts (0.14 mA of base
  current, decaying with R12·C1 = 6.8 ms) for as long as the trigger lasts. When the trigger ends n1
  has nothing to sink it: it jumps until C1's far end reaches Q7's base again (to 11.3 V in the
  model) and Q7 turns off at once; n1 then creeps up through R3/R4. So the reset lasts exactly the
  trigger. Q7's collector **q7c** drives the VCO reset (§5).

## 2. Accent buffer and the envelope switches: IC11b, Q3, Q4, Q5 (grid L-M 6-7)

- **acc** is IC2's RM0621 (RA1), its open-circuit voltage behind `rdac` 2.54 kΩ (`interface.md`
  §2), into IC11b pin 5 only: no other load on the DAC node.
- **IC11b**: **R38 4.7K** (−) to ground, **R37 10K** feedback: ×3.13. Output **ac**: 2.48 V at code
  10, 5.21 V at 21, 11.67 V at 47; code 63 (15.6 V) clips at the op-amp's limit.
- **Q5** (PNP, emitter at ac, **R39 1M** base to emitter): its base goes through **R34 100K** and
  **D5** (cathode at n1) to n1. While Q1 conducts, Q5 passes ac to its collector **e2** → ENV2 (§7).
  When n1 jumps back, D5 reverses and R39 turns Q5 off.
- **Hold timer.** n2 → **D6** → **R20 6.8K** → **C7 0.033** (charged to 14.3 V within the trigger,
  τ 0.22 ms). C7 → **R18 470K** → Q3's base, **R17 470K** to ground. **Q3** (NPN, emitter grounded)
  stays saturated while C7 drives it, pulling Q4's base down through **R32 47K**; **Q4** (PNP, emitter
  at ac, **R33 1M** base to emitter) passes ac to **e1** → ENV1 (§3). C7 runs down through R18 into
  Q3's base (τ R18·C7 = 15.5 ms toward the junction's 0.55 V); Q3 drops out of saturation at about
  50 ms (model, code 10: its collector rises from 0.07 V at 45 ms to 1.7 V at 55 ms) and Q4 lets go
  over the next 10 ms. RES's 47 ms (C7·R18·ln(13.4/0.65)) is the same estimate.

## 3. ENV1: C8, the D8 knee, DECAY (grid L-M 10-12)

- e1 → **R35 1.8K** → node j → **D9** → **C8 0.33 (T)** (τ R35·C8 = 0.6 ms: C8 is full within a
  millisecond).
- **D8** from j (anode) to the divider **R36 47K** (+15 V) / **R22 10K** (ground): 2.63 V behind
  8.25 kΩ. Above j ≈ 3.2 V the divider takes current through D8, so C8 rises 0.82 V per volt of ac
  instead of 1. RES called this a clamp at about 2.6 V on C8 ("very high accents therefore saturate
  the body level"): it is a soft knee, not a limit. During the hold C8 sits at **2.05 V** at code
  10, **4.35 V** at 21, **9.56 V** at 47, **11.05 V** at 63 (model, DECAY centre; the DECAY path
  loads it a little: 1.9 V at code 10 with DECAY 0).
- **DECAY: VR5 1M (A)** as a rheostat, wiper to its top end (NAVA: E and S at C8, A to R58), in
  series with **R58 47K** to ground; **R54 470K** to Q12's base in parallel. Once Q4 lets go, C8
  falls with τ = C8 · ((VR5 + R58) ∥ (R54 + Q12)) = 14 ms (DECAY 0) to 107 ms (DECAY 10): RES's
  figures.
- NET: `P5 c8 c8 vr5b 1meg decay a0.15`: 0 Ω fully anticlockwise, 1 MΩ clockwise, the parts list's
  A taper as two straight segments with 15 % of the track at mid rotation (`circuit.h`).

## 4. ENV3, TUNE and the CV: C9, IC12a, IC12b, Q10 (grid M-N 7-9)

- n2 → **D7** → **R21 2.2K** → **C9 0.33 (T)** (0.22 µF before SN 381500, §9). Within the 2 ms
  trigger (2.3 ms of n2) C9 reaches 12.96 V at TUNE centre (τ R21·C9 = 0.73 ms).
- Discharge: **R57 22K** + **VR2 100K (A) TUNE** (a rheostat, wiper to its R57 end; NAVA: E and S
  at R57, A to ground) to ground, in parallel with **R26 330K** into IC12a's virtual ground: τ =
  C9 · ((R57 + VR2) ∥ R26) = **6.8 ms** (TUNE 0) to **29.4 ms** (TUNE 10). TUNE sets only the
  sweep's time constant (RES, Fraser, Whittle).
- NET: `P2 r57b r57b 0 100k tune a0.15`.
- **IC12a**, an inverting summer: **R26 330K** from C9, **R27 1.5M** from +15 V, **R28 150K**
  feedback, (+) to ground through **R25 100K**. va = −(0.4545 v_C9 + 1.5 V): −1.5 V at rest, −7.4 V
  at the top of the sweep.
- **IC12b**, ×(+1) or ×(−1): **R29 100K** va to (−), **R30 100K** feedback, **R31 100K** va to (+).
  **Q10** (PNP, emitter grounded, collector at IC12b's (+)) grounds the (+) input when its base,
  **R23 100K** from the comparator's output with **R24 22K** to ground, is pulled to −2.4 V: then vb
  = −va; with Q10 off, vb = va. Q10 must be a PNP (it shorts a negative node); NAVA has 2SA1115.

## 5. The VCO: IC13a, IC13b, the reset Q11 (grid M-N 9-11)

- **IC13a**, the integrator: **R59 47K** from vb, **C14 0.033** feedback, (+) grounded.
- **IC13b**, the comparator: (−) from the triangle, (+) from **R49 100K** (feedback from its output)
  and **R50 47K** to ground. Its output is loaded by R49 + R50 and R23 + R24 (about 67 kΩ), so it
  swings ±14 V (the data book's typical into 10 kΩ; `opsatlight`, as the toms' comparators):
  thresholds ±14 V · 47/147 = **±4.48 V** (RES: ±4.3 V with 13.5 V).
  While IC13b's output is high, Q10 is off, vb = va < 0 and the triangle rises; at +4.48 V the
  comparator falls, Q10 conducts, vb = −va and the triangle falls. f = |va| / (4 V_th R59 C14) =
  **36.0 Hz per volt** of |va|: 54 Hz at rest, 267 Hz at the sweep's top (RES, with 4.3 V: 37.3 Hz
  per volt, 56 Hz).
- **Reset**: Q7's collector → **R47 330K** → the base of **Q11** (NPN, emitter at IC13a's (−),
  collector through **R51 2.2K** to its output): Q11 empties C14 (τ R51·C14 = 73 µs) for the
  trigger. Q7's collector also feeds **R48 22K** → **D12** (cathode at IC13b's (+)), which holds the
  comparator high. When Q7 lets go, every hit starts with the triangle at 0 V rising, the same
  phase each time.
- Between hits the VCO runs freely at its resting 54 Hz (the VCAs are shut). The reset takes about
  0.4 ms to bring C14 from ±4.5 V to 0, and ENV1 opens within that time: the first half
  millisecond of each hit carries a blip whose size depends on where the triangle was (up to about
  a tenth of the body's swing). A real 909 does the same; it is not repeatable from hit to hit.
- With Q7 off, Q11's base, D12's anode and Q7's collector are held only by junctions.

## 6. Waveshaper and the body VCA: D10, D11, Q12 (grid M 11-12)

- Triangle → **R52 10K** → node sh, **D10/D11** anti-parallel to ground: the triangle's ±4.5 V
  becomes a rounded ±0.6 V wave. sh → **R53 2.2K** → **R43 47** to ground: ±12 mV at Q12's
  collector.
- **Q12 (S)**: collector on the 47 Ω node, emitter into IC11a's virtual ground (NAVA: Q12.C at
  R43/R53, Q12.E at the summing node), base from C8 through R54 470K. It works at V_CE ≈ ±12 mV:
  a conductance about proportional to its base current, which passes nothing until C8 is above a
  V_BE (about 0.5 V). With β_R much smaller than β_F, its gain is set by the reverse β and its
  half-cycles differ. The (S) mark (a selection by the VCA's gain, `s-transistors.md`): Q12 and Q6
  are `2SC2603S` (β_R 6.5). In saturation at V_CE ≈ 0 part of the base current leaves through the
  emitter, a DC into IC11a that follows C8 (a few mV at the output; through AudioRealism's 6.5 Hz
  chain it becomes the slow excursion after the body, §15).

## 7. The attack: Q8, the Q9 Sallen-Key, the noise, Q6, ENV2 (grid L-M 7-11)

**Pulse.** **Q8** (PNP 2SA1115, emitter at +15 V; NAVA agrees) has its base on **R6 22K** (to
+15 V) and **R5 100K** (to n2). At rest n2 is at 0 V, the base at 12.3 V, and Q8 is saturated: its
collector sits at +15 V on **R14 22K** and **C5 0.0068** to ground. While n2 is high the base goes
to +15 V and Q8 is off: its collector falls to 0 V (τ R14·C5 = 0.15 ms). When n2 falls, Q8 turns on
hard and the collector jumps back to +15 V in a few µs. RES read this as "a PNP emitter follower
… a rounded 2 ms pulse, τ ≈ 0.68 ms" (R5·C5): C5 is at the collector, and the pulse is a 15 V
negative pulse with a soft start and a hard end.

**Sallen-Key.** The collector → **C2 0.0033** → **R7 100K** → node ska, **R15 100K** to ground
(a 241 Hz high-pass, RES); ska → **R16 39K** → Q9's base, **C4 100P** to ground; **C3 0.0047** from
ska to Q9's emitter. **Q9** (NPN, collector at +15 V) is the follower; its emitter load is **R8 10K
to −15 V**. A unity-gain low-pass with R = 50k (R7 ∥ R15) and 39k: f₀ = **5.3 kHz**, Q = **3.4**
(RES). The step at the pulse's end rings it: the click is a 5 kHz burst 2.3 ms into the hit; the
softer start gives a smaller one at the trigger. Q9's base-emitter stays forward (0.64-0.68 V in
the model), so its reverse breakdown (OLD's `q9Bv` 7 V) never comes into it.

- SN draws R8's lower end as a −15 V arrow touching the line from Q4 to R35 that runs under it
  (no dot). NAVA has R8 to −15 V; to ground or to that line Q9 would be cut off. The board's copper
  agrees with NAVA: R8's lower pad joins the wide supply strip below it (SN p.10, read 2026-10-26).
- The emitter → **C11 0.47/50** (+ toward R44) → **R44 22K** → the attack sum.

**Noise.** noise → **R10 12K**, **R9 4.7K** to ground (0/15 V becomes 0/4.22 V behind 3.4 kΩ), →
**C15 0.47/50** → **R46 4.7K** → node c13, **C13 0.1** to ground (a 339 Hz low-pass with R46 and
the source, RES) → **R45 4.7K** → the attack sum.

**Sum and ATTACK.** The sum node has **R56 330** to ground and goes through **VR3 500 (B) ATTACK**,
a rheostat (NAVA: E at the sum, A and S at Q6's collector), to **R55 47** to ground at Q6's
collector. At ATTACK 10 VR3 is shorted and Q6's collector is the sum node (330 ∥ 47); at ATTACK 0
it is ×47/547 of it. With R56 the click at Q6's collector changes by 2.3× (7.4 dB) over the knob,
not RES's "about 10×". NET: `P3 sum q6c q6c 500 attack` (500 Ω at 0, 0 Ω at 1).

**Attack VCA Q6 (S)**, like Q12: collector on R55, emitter into IC11a's sum, base from **C12
0.033** through **R42 470K**. **ENV2**: Q5 → **R41 22K** → C12 (τ 0.73 ms: C12 reaches 2.25 V at
code 10, 4.72 V at 21, 10.5 V at 47 within the trigger). C12 has no discharge path but Q6's base
(and R495 on early boards, §9): it falls with τ R42·C12 = 15.5 ms until Q6's base current fades,
then holds about 0.4 V for seconds. So the click and the noise follow the accent; the pitch
envelope doesn't (n2 is the same at every code).

## 8. Output: IC11a, LEVEL, the jack board (grid M 12-13)

- **IC11a**, (+) grounded, sums Q12's and Q6's emitter currents. **VR4 100K (B) LEVEL** is its
  feedback, a rheostat (NAVA: E and S at the output, A at the sum): the level is linear in the
  rotation. NET: `P4 out out sj 100k level`.
- IC11a's output goes straight to CN-5 pin 33 (W5) and the multi-jack board (p.12): **R500 1K**,
  then **C500 0.01** to ground (SN 415300 up), then the BD jack, whose normal contact takes
  **R501 12K** and **R502 12K** to the L and R mix buses (virtual grounds). The netlist puts these
  on IC11a's output as its load (`mix.cir` runs the jack board); `.output out` is the connector.
- IC11a inverts: the body's first swing is negative at the jack (the click too). The p.9 sketch
  starts with a negative spike; the AudioRealism recordings (individual outs) have the model's
  polarity on every file.

## 9. Production changes

| SN | Change | Netlist |
|---|---|---|
| up to 371499 | **R495 150K** from Q6's base to ground, "SN UP to 371499 SURFACE MOUNTED" (p.11, next to R42; not in the p.7 table). NAVA has it (it copies the drawing) | `.param r495=150k` (default 1e12: absent) |
| 381500 | **C9 0.22 → 0.33 µF**, "for expanding the TUNE range" (p.7) | `.param c9=0.22u` (default 0.33u) |
| 415300 | C500 0.01 µF added on the jack board (p.7, p.12) | `.param c500=1p` before (the solver takes no zero capacitor) |

An early unit (before 371500) is `--param c9=0.22u --param r495=150k`; 371500-381499 is
`--param c9=0.22u`. With C9 0.22 µF the sweep's time constant is 4.5-19.6 ms over TUNE (RES).
**R495** turns the attack VCA into a divider: Q6's base needs C12 above 0.6 · (1 + 470/150) ≈ 2.5 V
before it conducts, and C12 then drains through R42 + R495 (τ 20 ms). At code 10 C12 only reaches
2.25 V: on the early board the unaccented BD has almost no click or noise, and accented hits have a
shorter one (RES's "≈ 3.8 ms" was a guess at the node). The R495 1k of SN 415300 (the RS TRIG OUT,
p.7) is another part with the same number, on the rim shot.

## 10. Disagreements

- **SN vs NAVA**: none in the values or connections. NAVA marks C8 and C9 as 0.33/50 electrolytics;
  SN marks them (T), probably tantalum. NAVA keeps R495 (it copies the late drawing, which still
  shows the early part).
- **RES**: Q8 is a switch, on at rest, not an emitter follower (§7); D8 is a soft knee, not a clamp
  at 2.6 V (§3); ATTACK's range is 2.3×, not 10× (§7); R495 is on Q6's base (§9); n2 lasts 0.3 ms
  longer than the trigger (§1).
- **OLD** (bd.h) has the same topology (its `Variant` has R495 on Q6's base and the small C9) and
  per-unit constants it calls fitted: C14, C9, the comparator's swing 13.6 V, the VCA's β_R 2.6,
  β_F 350 / 300, Q9's base-emitter breakdown 7 V. None is used here; §13 and §15 say what the
  recordings ask for.

## 11. The netlist

**Interface.** Inputs `trig` (0/5 V, the latch), `acc` (5 V · code / 63, the DAC's open-circuit
voltage), `noise` (0-15 V, the noise generator's output averaged over each sample,
`noise.md`). Controls `tune`, `level`, `attack`, `decay`, rotation 0-1, default 0.5. Outputs `out`
(the connector) and, as probes, `tri` (IC13a's triangle), `env1` (C8), `env2` (C12), `env3` (C9),
`atk` (the attack sum). Rails b15, nb15. Params `rlatch`, `rdac`, `c9`, `r495`, `c500` (§9), and
the parts a unit's calibration sets, at their printed values: `c14`, `r27`, `c8`, `r44` (§15) and
`r15`, `r52`, `r53`, `r43` (candidates, §15).

**Op-amps.** IC13b (the comparator: `opsatlight`, §5), IC12b (the ±1 switch, which slews at each
flip), IC11b (code 63 clips it) and IC11a have the output limit and slew rate (`opsat`, `sr`);
IC12a (va stays within −1.5 to −7.4 V, slowly) and IC13a (the triangle, under 5 V/ms) are plain
single poles.

**No DC point.** The VCO free-runs, so the circuit has no operating point: `solveDc` doesn't
converge (the integrator's capacitor is open at DC). Run it in: the SPICE cases trigger after
0.5 s (1.5 s at full DECAY, where C8 drains slowly from the start-up). At the board's reset the
voice should run for about a second before its first hit (or start from a saved state).

**Partitions** (`docs/model.md` §Partitions; run in this order):

| Partition | What | Cuts |
|---|---|---|
| trig | Q1, Q2, the C1/R12 half of Q7's drive, IC11b, Q3-Q5, C7, C8 (ENV1), C12 (ENV2), C9 (ENV3), R5 | R12 (q7b), R5 (q8b), R54 (q12b), R42 (q6b), R26 (i12an) |
| vco | IC12a/b, Q10, IC13a/b, Q7, Q11, D12, R52 | R26 (c9), R12 (p1), R52 (sh) |
| shaper | R52, D10, D11, R53 | R52 (tri), R53 (q12c) |
| click | Q8, the Sallen-Key, Q9, C11, R44 | R5 (n2), R44 (sum) |
| noise | R10, R9, C15, R46, C13, R45 (linear) | R45 (sum) |
| vca | R53, R43, Q12, the attack sum, VR3, R55, Q6, IC11a, LEVEL, the jack's load | R53 (sh), R54 (c8), R42 (c12), R44 (c11p), R45 (c13) |

The VCO's loop (comparator → Q10 → IC12b → integrator) stays in one partition: a cut in it delays
every flip by a sample. Q7 goes with the VCO, so its collector's junction-held nodes (Q11's base,
D12) are solved together. Partitioned against whole: §12.

**Sleep.** The VCO keeps running, but its C14 (33 nF) is under the 0.1 µF the voice's sleep check
counts; the jack is quiet once C8 is under Q12's threshold. C12 holds about 0.4 V between hits (it
drains only into Q6's base) and doesn't move.

## 12. SPICE

`tools/spice/cases/bd.py` (`suite.py --only bd`): the whole netlist, the solver against ngspice 47
on the same deck, the trigger's edges as the board runs them (8 substeps, the first 8 quarter steps
by backward Euler); the noise is the LFSR at 320 kHz around the hit. Each case triggers after 0.5 s
(1.5 s at full DECAY: no DC point, §11) and is compared from 1 ms after the trigger (the
free-running VCO's blip, §5). `out`, RMS and largest difference in % of its range, at 96 kHz, with
the solver's comparators located within the sample (2026-10-17):

| Case | Code | Knobs | RMS % | max % |
|---|---|---|---|---|
| bd_10 | 10 | centre | 0.030 | 1.26 |
| bd_21 | 21 | centre | 0.015 | 1.29 |
| bd_47 | 47 | centre | 0.017 | 1.53 |
| bd_63_level1 | 63 | LEVEL 1 | 0.023 | 1.96 |
| bd_21_tune0_attack1_decay0 | 21 | TUNE 0, ATTACK 1, DECAY 0 | 0.025 | 1.95 |
| bd_10_tune1_attack0_decay1 | 10 | TUNE 1, ATTACK 0, DECAY 1 | 0.080 | 0.92 |
| bd_21_attack1_noise0 | 21 | ATTACK 1, noise held | 0.027 | 1.80 |

Before that fix the comparator's flips came 6-30 µs early per half-cycle during the sweep, which
left `out` at 0.2-0.9 % RMS (the VCO's phase); every other node agreed within 0.05 %. Partitioned
against whole (two hits, codes 10 and 21, 1 s at 96 kHz, noise running): `out` within 0.05-0.13 %
RMS of its range (largest 0.67 %), the envelopes within 0.001 %.

## 13. Against the real unit (the drawn parts)

`tools/compare/bd.py`: every AudioRealism BD file (the TUNE × ATTACK × DECAY grid at 0/25/50/75/100
%, LEVEL 50 %, 125 files; the 11 accent files) and Rob Roy's 24, each rendered through
`tr909voicerun` (the board as the plugin plays it, 96 kHz) at the same knobs, at AudioRealism's
codes (the grid at code 21: accented steps at TOTAL ACCENT 0; the accent takes 21-47;
`real909.ar_code`), taken at the jack (R500/C500 into a high-impedance input), for AudioRealism
through its recording chain (`real909.chain`: a 6.5 Hz first-order high-pass), and measured with
`real909.measure`. Both are measured from the trigger: the model's own, the recording's found from
its click (Q8's edge, 2.35 ms after the trigger in the model). A level-threshold onset is no anchor
for this voice: the recordings start about a millisecond after the trigger (their first
millisecond is the VCO's reset blip, cut or under the threshold), and the model's blip is random.
Model against AudioRealism, with the service notes' parts (`--cal nominal=1`):

| | TUNE 0 | 25 % | 50 % | 75 % | 100 % |
|---|---|---|---|---|---|
| f_end Hz (ATTACK 0, DECAY 1) | 54.05 / 49.22 | 54.12 / 49.33 | 54.17 / 49.39 | 54.30 / 49.45 | 54.32 / 49.54 |
| first period Hz | 155 / 147 | 182 / 173 | 199 / 191 | 237 / 205 | 248 / 239 |
| sweep τ ms | 7.2 / 7.6 | 9.1 / 9.7 | 11.1 / 11.8 | 21.0 / 14.5 | 29.1 / 29.1 |

| | DECAY 0 | 25 % | 50 % | 75 % | 100 % |
|---|---|---|---|---|---|
| hold ms (TUNE 50 %, ATTACK 0) | 64 / 67 | 66 / 71 | 68 / 76 | 84 / 79 | 86 / 90 |
| t-20 ms | 88 / 106 | 108 / 152 | 141 / 174 | 228 / 213 | 274 / 289 |
| t-40 ms | 138 / 154 | 157 / 188 | 181 / 219 | 326 / 327 | 410 / 488 |
| tail τ ms | 22 / 29 | 19 / 29 | 23 / 36 | 53 / 52 | 75 / 90 |

| | ATTACK 0 | 25 % | 50 % | 75 % | 100 % |
|---|---|---|---|---|---|
| click dB (TUNE 50 %, DECAY 1) | −34.1 / −38.8 | −32.8 / −37.2 | −31.1 / −35.5 | −29.0 / −34.6 | −26.0 / −30.9 |

- **The VCO is 9.8 % fast**: f_end 54.1-54.3 Hz against 49.2-49.5 (Rob Roy's unit 49.3-49.5 too),
  the sweep's start 4-6 % high (the offset term is relatively larger than the sweep term).
- **TUNE's τ** matches at 0, 25, 50 and 100 % and is 45 % long at 75 %: the two-segment A taper
  puts VR2 at 57.5 % of its track there.
- **DECAY**: the body falls too soon at 0-50 % and holds too long at 75 %; t-40 is 16 % short at
  100 %. Over the grid t-20 is 0.30 octave short, t-40 0.23, the tail's τ 0.43.
- **The click** is 4.3-4.5 dB strong against the body at every ATTACK (the 1-8 kHz band over
  1.5-4.5 ms re the body over 10-40 ms), over 2-14 kHz alike (by band, 2.4-14 kHz: +4 to +6 dB);
  ATTACK's range is right (7.9 dB real, 8.1 model).
- **The body**: 2nd harmonic −21.5 dB re the fundamental (real −28), 3rd −33 (real −26); positive
  over negative peak 0.84 (real 0.95).
- **Level**: one gain fits the 125 cells within 0.29 dB RMS. **Accent** (codes 21 → 47, RMS
  0-50 ms): +6.35 dB against +6.70; +3.92 against +4.60 at code 35.
- Polarity: every AudioRealism file is the model's way up (Rob Roy's chain inverts all 24).

## 14. CPU

On the board (`tr909voicerun`, 48 kHz host, the board at 96 kHz, a BD hit every 0.5 s): 36-38 % of
a core for the whole board against 10-13 % with nothing playing (the shared machine under a load
of about 45), so about 25 % for the BD. Per partition (an earlier, quieter measurement at 96 kHz):

| Partition | % of a core | Newton iterations a sample | linear mode |
|---|---|---|---|
| trig | 5.1 | 0.44 | 74 % |
| vco | 18.0 | 4.3 (3.0 factorisations) | never |
| shaper | 1.0 | 0.71 | |
| click | 0.7 | 0.02 | 99 % |
| noise | 0.3 | 0 (linear) | |
| vca | 2.4 | 1.1 | 53 % |

Whole (one netlist): 155 %. The VCO partition costs 18-22 % whether or not anything plays: its
comparator IC13b sits in its output limit, which the solver never linearises, and every sample
takes 4-4.5 Newton iterations and 3 factorisations (13 ports): the first iteration's residual
comes from the triangle's ramp moving the saturated input stage's port, and the chord steps after
it overshoot on the limiter's flat slope (`TR909_TRACE` on the comparator alone, driven by a
triangle: 4.9 iterations a sample, 3 ports). A solver that held a saturated op-amp as linear (its
output doesn't move) would take the VCO to a few %. Asleep (the board's sleep), nothing.

## 15. Calibration

Fitted to the AudioRealism unit (§13's setup) through `tr909voicerun --cal`, on three rows of its
grid: the TUNE row (ATTACK 0, DECAY 1) for the pitch and TUNE's law, the DECAY row (TUNE 50 %,
ATTACK 0) for C8 and DECAY's law, the ATTACK row (TUNE 50 %, DECAY 1) for the click: 13 of the 125
cells. The other 112 are held out, and Rob Roy's unit is the second check.

**Values** (`unitCalibration()`):

| Part / law | Value | Printed | Evidence |
|---|---|---|---|
| `bd.c14` | 0.0342 µF | 0.033 | f_end and the sweep's start (§13: 9.8 % and 4-6 % high): C14 scales every frequency, R27 only the resting CV's offset; the two fit both (f_end 49.23-49.47 Hz against 49.22-49.54, the fitted start within 1-2 %). C14 +3.6 % |
| `bd.r27` | 1.59 MΩ | 1.5M | with C14 (above): +6 %, a little past a 5 % part; the +15 V rail (7815, ±4 %) feeds the same term |
| `bdTune` law | 0 → 0.02, 0.25 → 0.293, 0.5 → 0.514, 0.75 → 0.579, 1 → 1 | straight | the sweep's τ at each AudioRealism TUNE: VR2 at 0.6 / 8.8 / 17.4 / 28.5 / 100 % of its track (the netlist's A taper, 15 % at mid, gives 0 / 7.5 / 15 / 57.5 / 100): the 100k A pot's real track is flatter in its upper half than two segments make it |
| `bd.c8` | 0.40 µF | 0.33 (T) | the decay at full DECAY, where the pot is at its end and C8 · (VR5 + R58) sets the tail: t-40 482 against 488 ms, the tail's τ 89 against 90 (0.33: 410, 75). +21 %, the edge of a tantalum's ±20 % |
| `bdDecay` law | 0 → 0, 0.25 → 0.442, 0.5 → 0.527, 0.75 → 0.605, 1 → 1 | straight | t-20 at each AudioRealism DECAY: VR5 at 0 / 13 / 20 / 33 / 100 % of its track; the same shape as VR2's (both A pots of one series) |
| `bd.r44` | 36 kΩ | 22K | **outside tolerance (+64 %)**: the click's level against the body, 4.3-4.5 dB strong at every ATTACK, TUNE and DECAY, broadband over 2-14 kHz, and on Rob Roy's unit too (+5 dB). A constant factor in the click's linear path; no part within tolerance gives 4.5 dB (R15 47k would too, and moves the Sallen-Key's tuning); R44, the click's own feed into the attack sum, stands for it |

**Result** (model against AudioRealism; medians of model − real over the cells, octaves unless
dB or Hz, the RMS spread after the slash):

| | fitted rows, drawn parts | fitted rows, calibrated | held out (112 cells), drawn | held out, calibrated | Rob Roy, drawn | Rob Roy, calibrated |
|---|---|---|---|---|---|---|
| f_end | +4.8 Hz | −0.04 Hz | +5.2 Hz | +0.00 Hz | +4.7 Hz | −0.17 Hz |
| sweep τ | −0.08 / 0.17 | +0.005 / 0.02 | −0.06 / 0.25 | +0.003 / 0.05 | −0.08 | −0.12 |
| first period | +0.07 | −0.002 | +0.08 | −0.009 | (trimmed) | |
| f at 50 ms | +0.08 | −0.002 | +0.12 | −0.004 / 0.005 | +0.12 | −0.004 |
| hold | −0.08 | +0.02 | −0.09 | −0.002 / 0.06 | | |
| t-20 | −0.08 | +0.13 | −0.30 / 0.29 | +0.01 / 0.13 | +0.26 | +0.49 |
| t-40 | −0.25 | −0.02 | −0.23 / 0.24 | −0.04 / 0.09 | | |
| tail τ | −0.28 | −0.02 | −0.43 / 0.44 | −0.04 / 0.19 | −0.34 | +0.12 |
| click re body | +4.3 dB | +0.4 dB | +4.5 dB | +0.2 / 0.4 dB | +5.1 dB | +1.2 dB |
| level spread (RMS) | 0.17 dB | 0.18 dB | 0.30 dB | 0.14 dB | | |
| LSD 30-330 ms | | | 11.7 dB | 4.2 dB | | |

- The held-out cells follow the fitted ones: pitch and τ exact over the whole grid (the TUNE law
  holds at every ATTACK and DECAY), the decay within 0.01-0.04 octave (median), the click within
  0.2 dB. f_end's spread (2 Hz RMS) is the cells where the body dies before the sweep has settled
  (DECAY 0-0.25: the 0-350 ms fit has too short a track, the real ones as much as the model's);
  where it lasts (DECAY ≥ 0.5) it is within ±0.6 Hz.
- At the settings Roland's plugin was rendered at (`docs/reference.md`): τ at TUNE 0 / 25 / 50 / 75
  / 100 % (ATTACK and DECAY 50 %) 7.6 / 9.6 / 11.8 / 14.5 / 29.2 ms (AudioRealism 7.6 / 9.6 / 11.8
  / 14.4 / 29.2; Roland 7.2 / 8.9 / 13.8 / 20.9 / 29.4); t-20 at DECAY 0 → 100 % (ATTACK 50 %)
  93 / 152 / 174 / 213 / 317 ms (AR 106 / 151 / 174 / 196 / 289; Roland 91 / 124 / 189 / 246 / 286);
  t-40 140 / 196 / 219 / 281 / 481 (AR 155 / 182 / 225 / 305 / 487; Roland 118 / 176 / 299 / 404 /
  480); click at ATTACK 0 → 100 % (DECAY 50 %) −36.7 / −35.6 / −34.2 / −32.3 / −29.6 dB (AR −37.1 /
  −36.1 / −34.1 / −32.7 / −29.2; Roland −39.9 / −33.6 / −32.4 / −31.6 / −30.6). f_end 49.4-49.6 Hz
  (AR 49.3-49.6, Rob Roy 49.3-49.5; Regebro's and KB6's units and Roland's plugin 52.3-52.7).
- **Rob Roy's unit** has the same end pitch (−0.17 Hz) and the same sweep at dial 0, 3 and 10; its
  dial 7 is 18.0 ms against the model's (and AudioRealism's 75 %) 14 (its TUNE pot's upper half
  differs, `docs/reference.md`). It decays faster than AudioRealism's: t-20 168 / 224 ms at DECAY
  7 / 10 against 198 / 318 (AudioRealism's 100 %: 289), so calibrating to AudioRealism moves the
  decay away from it (+0.26 → +0.49 octave): a unit difference (C8 or VR5 lower in that unit).
- **AudioRealism's chain** explains the "DC-like tail that stops with a kink" (RES §7): the model's
  body leaves a DC step (Q12's emitter current), which the 6.5 Hz high-pass turns into the slow
  excursion after the last cycle. With the chain the DECAY 0 tail's τ went from 6 to 22 ms (real
  29), and the body's positive over negative peak from 0.66 to 0.84.

**What remains**

- **DECAY 0**: t-20 93 against 106 ms, the hold 62 against 67, the tail's τ 21 against 29: the body
  falls faster after the hold where C8 drains through R58 alone. C8's and C9's dielectric absorption
  (tantalum, 2 % in the netlist since 2026-10-22: over the grid t-40 +0.023 → +0.018 octave, the
  tail +0.037 → +0.031) leaves DECAY 0 as it was at 2-10 %. Neither C8 (0.40 or 0.47 µF: t-20
  93 → 96) nor C7 (+10 %: the hold +7 ms everywhere, too long at DECAY 1) moves it alone; the
  shape of Q3/Q4's release and the VCA's law near its threshold are left. At DECAY 1 t-20 is 10 %
  long while t-40 is right (317 / 481 against 289 / 488).
- **The body's harmonics and symmetry**: 2nd −21.5 dB (real −28), 3rd −33 (real −26), positive
  over negative 0.84 (0.95), on every cell and on Rob Roy's unit (2nd −7.5 dB, 3rd +12 dB against
  the model). A smaller swing at Q12's collector takes the 2nd down and a less driven shaper the
  3rd up: R43 20 Ω with R52 14k gives −28.0 / −26.8 dB and 0.93, but both are ±50 % or more off
  and the BD's level then falls by 8 dB, which the levels across the voices (`s-transistors.md`
  §3.1: the BD at +0.1 dB against the others as drawn) rule out; R53 4.7k gives the 2nd and the
  symmetry but a 3rd of −16 dB. Not fitted: the mechanism isn't found. Through the decay (TUNE 0
  and 0.5, DECAY 1, 60 ms windows, 30-330 ms, 25 dB of fall) the unit's 2nd and 3rd stay at −27.5
  and −25.8 dB and the model's at −21.5 and −33: both stages make level-free distortion (the shaper's
  shape; Q12's ±12 mV swing, set by R53/R43, whatever its base current), so this doesn't tell them
  apart. A third unit (scherer.de's
  `909.rar`, 450 hits, knobs and chain unknown) has the 2nd at −24.5 to −27 dB, the 3rd at −31 to
  −33 and positive over negative 0.76-0.78: the 3rd and the symmetry are spread between units (the
  model inside it), the 2nd perhaps 3-6 dB high.
- **Accent**: codes 21 → 47 +6.34 dB against +6.70, +3.92 against +4.60 at code 35 (the takes' TA
  is read from these same levels, assuming level ∝ code, `docs/reference.md`). The accent's t-40
  grows 61 ms over the sweep against the recording's 42.
- **The click's part** (R44 above): unidentified.

## 16. Open questions

- The click's 4.5 dB (§15): which part of the click's path a real board has otherwise.
- The body's 2nd and 3rd harmonics (§15).
- The decay after the hold at low DECAY (§15).
- What (S) means on Q12 and Q6 (`s-transistors.md`).
- The VCO's cost in the solver (§14).
