# Low, mid and hi tom and the tom noise (lt.cir, mt.cir, ht.cir, tomnoise.cir)

The LOW TOM, MID TOM and HI TOM of the voicing board 73133090, from their TRIG latch outputs and
accent DACs to their outputs at the connector W5 to the multi-jack board, and the TOM NOISE circuit
the three share. Sources as `docs/circuit.md` §0: **SN** p.11 (crops
`private/docs/schematic/crops/p11-lt-low-tom.png`, `p11-mt-mid-tom.png`, `p11-ht-hi-tom.png`,
`p11-noise-tom-noise.png`; the full page for the lines between them), p.4 (block diagram, which
names the LT parts with the MT and HT ones in brackets), p.6 (the snare's description, which SN
says the toms follow), p.7 (change information), p.12 (multi-jack board); **NAVA**
(`Nava_v1.02_netlist.txt`, sheets LowTom, MidTom, HiTom, Noise); **OLD**, the hand-written model
`private/reference/old-models/toms.cpp`; **RES**, `docs/research.md` §2 "Toms" and §3 A2.1;
**DS**, `private/docs/datasheets/` (M5218, HD14174B). Every value below was read on the SN crops
at 400 dpi and agrees with NAVA unless a note says otherwise. The MT's designators are used, with
the LT's and HT's in the table of §3.

## 1. The netlists' interface

Each tom is one netlist; the tom noise is a fourth, which the voicing board runs alongside them.
The signals between them carry the board's names (`core/voiceboard.cpp`, `signalName`: a name with
a dot is a board signal as it stands), so the board wires them without special cases.

| Netlist | Inputs | Outputs | Controls |
|---|---|---|---|
| `lt.cir` | `trig` (LT TRIG, IC1), `acc` (the LT accent DAC, IC4 / RA3), `tomnoise.tn` | `out` (IC18a, CN-5 pin 35), `g` (Q17's collector: the board's `lt.g`) | `tune` VR10 10k B, `decay` VR11 500k B, `level` VR12 50k B |
| `mt.cir` | `trig` (MT TRIG), `acc` (IC5 / RA4), `tomnoise.tn` | `out` (IC23a, pin 36), `g` (Q29's collector: `mt.g`) | `tune` VR13, `decay` VR14, `level` VR15 |
| `ht.cir` | `trig` (HT TRIG), `acc` (IC6 / RA5), `tomnoise.tn` | `out` (IC45a, pin 37), `g` (Q62's collector: `ht.g`) | `tune` VR16, `decay` VR17, `level` VR18 |
| `tomnoise.cir` | `noise` (IC33 pin 9), `trig.lt`, `trig.mt`, `trig.ht` (the toms' TRIG lines), `lt.g`, `mt.g`, `ht.g` | `tn` (IC27a: the TOM NOISE bus, the board's `tomnoise.tn` when the block's owner is `tomnoise`) | none |

Order, sample by sample: the three toms, then tomnoise, so that tomnoise takes this sample's G
lines (C54 charges from them during the 2 ms trigger) and the toms take the bus one sample late
(it reaches them only through VCO3's VCA). The couplings are one way and stiff where they are
cut: G is driven through R157 100 Ω and a saturated PNP while D44 charges C54 (4.7 nF, 1 % of the
envelope capacitors' load on G); the bus is an op-amp output that the toms load with 22 kΩ each
(tomnoise holds those three loads, R114, R177, R387). Each netlist holds the TRIG latch's output
resistance (`rlatch` 600 Ω, as the other netlists) and the toms the DAC's (`rdac`; the accent
op-amps' (+) inputs draw nothing, so its value doesn't matter); the toms' R154 and tomnoise's D2
both load the one latch output, each netlist seeing only its own load (about 0.2 mA each, 0.1 V on
the 5 V line, which saturates Q25 and Q34 either way). tomnoise can sleep when all three toms do
(its bus never settles: the noise leaks through R194 all the time).

**Rates.** The board's 88.2 or 96 kHz; §9 has the solver's error against ngspice. **Partitions:**
`trig`, `vco1`, `vco3`, `vco2`, `out` per tom, `filter` and `vca` for the tom noise (§8). On a
trigger edge every partition of the tom should be substepped, not only `trig`: line A's 15 V step
reaches the three VCO partitions, and unsubstepped a comparator could settle in its other state
when the trigger ends (seen in a partitioned run; the whole netlist doesn't show it).

## 2. One tom, stage by stage (MT; SN p.11 grid K-N 18-26)

### 2.1 Trigger and gated accent

- **trig** → RLATCH → **R154 22K** → Q25's base, **R155 10K** to ground; Q25 (2SC2603, emitter
  grounded) conducts for the 2 ms.
- **Line A.** Q25's collector pulls Q24's base through **R153 22K** (**R151 10K** base to +15 V);
  Q24 (2SA1115, emitter through **R152 100** to +15 V) lifts line A to about 14.9 V for the
  trigger. Line A charges the pitch envelope (C30/C31, §2.2) and resets the three VCOs (§2.3).
  Between hits it floats (nothing drives it): the reset transistors' base-collector junctions and
  D35/D50/D51 drag it down with whichever triangle is lowest, about −6.5 V, where it disturbs
  nothing.
- **Accent buffer IC23b.** (+) from the DAC (RA4 through 2.54 kΩ, no other load); **R180 4.7K** (−)
  to ground, **R179 10K** (−) to output, and **R178 220K** from (−) to −15 V. Output
  3.173 · V_dac + 0.682 V: 3.20 / 5.97 / 12.52 V at codes 10 / 21 / 47 (code 63's 16.5 V clips).
  RES put R178 on the (+) input (a −0.17 V offset); the crop has the vertical from the R180/R179
  junction carry the (−) input's dot and go on, across the (+) line without a dot, to R178's top,
  and NAVA has R178 on IC23's −IN (net N$363, as R118 on IC18's and R381 on IC45's); OLD agrees.
- **Gated accent G.** **R156 100K** from IC23b's output to the D42 node, **R158 100K** from there to
  the base of Q29 (2SA1115, emitter through **R157 100** from IC23b's output); **D42** from that node
  to Q25's collector. With Q25 on, D42 pulls Q29's base down and Q29 puts IC23b's output on G, the
  line into the three envelope diodes (D29, D43, D30) and into the tom noise (D44). With Q25 off,
  R156/R158 hold Q29 off and G floats.
- **RGF 10 meg** (not a part). Between hits G floats; on the unit D44 into C54 (tomnoise.cir) takes
  the reverse-biased envelope diodes' leakage and holds G just above C54's voltage. tomnoise.cir
  takes `g` as a stiff source, so the netlist ties G down through 10 MΩ (microamps at most, nothing
  against Q29's drive), which keeps it under C54 at rest. Without it the netlist's G floats at the
  envelope capacitors' voltage (1-3 V for 100 ms), and C54 would be held up through D44 for that
  long: a noise tail the unit doesn't have.

### 2.2 TUNE, VDD and VSS (IC20)

- **TUNE.** **R172 68K** from +15 V, **VR13 10K(B)**, **R173 10K** to ground: the wiper spans
  1.705-3.409 V (2.557 V at mid). Rotation 1 puts the wiper at R172's end (NAVA: VR13.E at R172,
  .A at R173).
- **VDD.** IC20a follows the wiper; its output goes through **R129 1K** (printed "R12a": R129 is the
  one number of the MT's range R121-R181 not used elsewhere; NAVA copies "R12A") to VDD, with the
  (−) input taken after R129, so VDD is the wiper's voltage whatever the 4069 draws.
- **VSS.** IC20b inverts: **R127 100K** from VDD, **R121 2.2M** from C30, **R122 470K** from C31 into
  its (−) input, **R125 100K** feedback from after **R126 1K**: VSS = −(VDD + 0.0455 V(C30) +
  0.213 V(C31)).
- **Pitch envelope.** Line A charges **C30 0.1** through **D37** and **C31 0.047** through **D36** to
  about 14.2 V; they run down into IC20b's virtual ground through R121 and R122: τ = 220 ms and
  22 ms. VSS starts at −(VDD + 3.7 V), −6.3 V at TUNE 50 %. The envelope is on VSS only (SN p.6 says
  the snare's IC35 moves both rails; the toms' IC20a has no envelope input), so it speeds up the
  rising ramps only (§2.3).
- **D34** (RD18JB2, an 18 V zener from VSS to VDD, the 4069's protection) is left out: VDD − VSS is
  at most 2 · 3.41 + 3.7 = 10.5 V, so it never conducts, and the solver has no breakdown.

### 2.3 The three VCOs

Each VCO (VCO1: IC21, C32; VCO3: IC25, C34; VCO2: IC24, C33) is a comparator, two 4069UB inverters
in parallel and an integrator:

- **Comparator IC21a.** (−) from the triangle through **R124 100K**; (+) from its output through
  **R130 100K** (printed "R80"; NAVA R130) with **R128 100K** to ground: thresholds ±V_sat / 2,
  ±7 V at the ±14 V a lightly loaded M5218 swings (§7). Output through **R131 100K** to the
  inverters' inputs.
- **Inverters IC22a and f** (pins 1-2 and 13-12, TC4069UBP) in parallel, run from VDD and VSS: the
  comparator's ±14 V through 100k is clamped by their input protection diodes to VSS − 0.6 V or
  VDD + 0.6 V, and the output is VDD or VSS through a channel. "Inverting buffers configured as
  voltage-dependent resistor" (SN p.6): the current into the integrator is VDD/47k or VSS/47k.
  VCO3 uses gates b and c (3-4, 5-6), VCO2 d and e (9-8, 11-10).
- **Integrator IC21b.** **R134 47K** into (−), **C32 0.018** feedback. The comparator low puts VDD
  on R134 and the triangle falls; it flips at −7 V, the inverter puts VSS on R134 and the
  triangle rises. f = 1 / (2 · 7 · 47k · C · (1/VDD + 1/|VSS|)): VCO1 108 Hz at TUNE 50 % without
  the envelope, 72-144 Hz over TUNE. The pitch envelope's larger |VSS| shortens the rising ramp
  only, f × 2/(1 + VDD/|VSS|): at most × 1.42 at TUNE 50 % (× 1.52 at TUNE 0, × 1.35 at TUNE 1).
- **Reset.** Line A through **D35** and **R123 10K** forces the comparator's (−) input to about
  12.5 V (low output) and through **R133 1M** turns on **Q30** (2SC2603) across C32: emitter at the
  virtual ground, collector through **R132 10K** to the output (the crop's emitter arrow; NAVA Q30.E
  on IC21's −IN). From a triangle above 0 V Q30 conducts forward (β_F · 14 µA) and C32 is empty in
  0.2 ms; from one below, it conducts inverted (β_R · 14 µA ≈ 84 µA, against the 54 µA the
  inverter pushes the other way), and the reset to −0.7 V takes up to 3.5 ms, longer than the 2 ms
  trigger. So a hit starts from wherever its VCOs were when the trigger came, as on the unit (OLD:
  "the reset does not always finish"). After the trigger every VCO first falls from about −0.7 V
  to −7 V (a short first half-cycle) and all three start together.
- **Capacitors.** MT: VCO1 C32 0.018, VCO3 C34 0.01, VCO2 C33 0.027 (§3 for LT and HT): VCO3 runs
  highest, VCO2 lowest (RES quotes Roland/Network-909 for that); the ratio VCO1 : VCO2 : VCO3 is
  1.5 : 1 : 2.7.

### 2.4 VCO1's shaper and its variable clipper Q23

VCO1's triangle → **R164 10K** → node s1, **D38/D39** (1SS133, anti-parallel) to ground → **R165
2.2K** → **R159 47** to ground, Q26's collector. **Q23** (2SC2603) from the triangle through **R163
1K** (collector) to s1 (emitter; NAVA Q23.E on D38/D39), its base through **R162 1M** from **C38
0.0068**, which line A charges through **D31**: while C38 holds Q23's base up, Q23 clamps s1 to the
triangle through 1 kΩ, a hard clip against the diodes; as C38 empties into the base (τ ≈ 6.8 ms on
MT, 10 ms LT, 5.6 ms HT, RES) the clip softens to the diodes' rounding. RES quotes Network-909: "a
slow change from square (hard clipping) to sine (softer clipping)".

### 2.5 VCO3's shaper and the tom noise

VCO3 → **R181 10K** → s3, **D47/D46** → **R176 2.2K** → **R175 47**, Q28's collector, where the TOM
NOISE bus comes in through **R177 22K** (the crop's "NOISE" arrow from the bus along the voice's
bottom edge).

### 2.6 VCO2's shaper

VCO2 → **R166 10K** → s2, **D40/D41** → **R167 2.2K** → **R160 47**, Q27's collector.

### 2.7 Envelopes

All three are charged from G for the trigger and run down into the VCAs' bases:

- **ENV1.** **D29** → **C36 0.15 (T)** → **R168 470K** → Q26's base: τ about 70 ms.
- **ENV3.** **D43** → **C39 0.056** → **R174 470K** → Q28's base: τ about 26 ms. (NAVA calls R174
  R129; the crop prints R174, and R129 is VDD's 1k, §2.2.)
- **ENV2 (DECAY).** **D30** → **C35 0.47 (T)**, which runs down through **VR14 500K(B)** as a
  rheostat (NAVA: VR14.A at C35, .E and .S at R171; rotation 1 is the full 500k) and **R171 56K**:
  τ 26-261 ms. C35 fills **C37 0.47 (T)** through **R161 100K** (τ about 47 ms), and C37 feeds
  **R170 220K** to Q27's base and **R169 220K** to Q26's base. So DECAY sets VCO2's envelope, which
  swells in over about 50 ms, and through R169 the tail of VCO1's as well. RES has DECAY on VCO2
  only and missed R169 (the crop has R169's foot on the C37 line along the ENV2 label, and NAVA has
  R169 on Q26.B and C37's net N$357).

### 2.8 The VCAs

**Q26, Q27, Q28** (2SC2603, marked (S): RES reads it as a selected part; nothing on the sheet says),
collector at the 47 Ω node, emitter into IC23a's virtual ground, base fed from the envelopes. At
V_CE ≈ 0 (the 47 Ω node swings ±12 mV) a transistor passes about β_R · I_B / V_T of conductance into
the virtual ground: the gain follows the base current, and with it β_R (6 in models.cir) sets the
voice's level. Most of the base current leaves through the collector into the 47 Ω; about 2 %
(β_R/β_F) through the emitter, a small DC step at the output that follows the envelopes.

### 2.9 Output

**IC23a**, (+) grounded, **VR15 50K(B)** LEVEL as its feedback rheostat (NAVA: VR15.A at the summing
node, .E and .S at the output: rotation 1 is the full 50k). Its output is the voice's output at
CN-5 pin 36. The multi-jack board's load (SN p.12; `mix.cir` runs it): **R509 1K**, **C504 0.01**
(from SN 415300) to ground, and through the jack's normal contact **R510 12K** and **R511 12K** to the
mix buses (AC ground for this load; IC66's virtual grounds through 1 µF).

Not in the netlists: the decoupling capacitors **C40, C41, C44, C45** (10µ/16 on the rails; the
rails are ideal).

## 3. LT and HT

Same circuit (SN p.6: "similar circuitry except that VCO's have different main tunings"); the
netlists are the MT's with these designators and values (`lt.cir`, `ht.cir`):

| Part | MT | LT | HT |
|---|---|---|---|
| Op-amps: TUNE/VSS, VCO1, VCO3, VCO2, output/accent | IC20, IC21, IC25, IC24, IC23 | IC14, IC15, IC19, IC17, IC18 | IC42, IC43, IC47, IC46, IC45 |
| 4069UB | IC22 | IC16 | IC44 |
| Q: line A, trig, accent; resets VCO1/3/2; clipper; VCAs 1/3/2 | Q24, Q25, Q29; Q30, Q32, Q31; Q23; Q26, Q28, Q27 | Q15, Q16, Q17; Q13, Q22, Q14; Q18; Q19, Q21, Q20 | Q60, Q61, Q62; Q53, Q55, Q54; Q56; Q57, Q59, Q58 |
| TUNE: R from +15 V, pot, R to ground | R172 68K, VR13, R173 10K | R113 68K, VR10, R112 10K | R386 68K, VR16, R384 10K |
| VDD / VSS 1k | R129 / R126 | R68 / R65 | R333 / R338 |
| Pitch envelope C (D), R | C30 (D37) 0.1, C31 (D36) 0.047; R121 2.2M, R122 470K | C16 (D15), C17 (D14); R60, R61 | C95 (D83), C96 (D82); R331, R332 |
| VCO1 / VCO3 / VCO2 C | C32 0.018 / C34 0.01 / C33 0.027 | C18 **0.022** / C20 **0.012** / C19 **0.033** | C97 **0.015** / C103 **0.0082** / C102 **0.022** |
| Clipper C | C38 0.0068 | C24 **0.01** | C101 **0.0056** |
| ENV1 C, R | C36 0.15, R168 470K, R169 220K | C22 **0.22**, R107, R108 | C99 0.15, R354, R353 |
| ENV3 C, R | C39 0.056, R174 470K | C25 0.056, R110 | C106 **0.047**, R378 |
| ENV2 C, C, R, DECAY, R | C35, C37 0.47; R161 100K; VR14; R171 56K; R170 220K | C21, C23 **0.68**; R100; VR11; R111; R109 | C98, C100 0.47; R341; VR17; R385; R352 |
| Accent IC: 4.7K, 10K, 220K; G: 100K, 100K, 100; D | R180, R179, R178; R156, R158, R157; D42 | R120, R119, R118; R95, R97, R96; D18 | R383, R382, R381; R347, R349, R348; D81 |
| Jack board | R509, C504, R510 12K, R511 12K | R506, C503, R507 **8.2K**, R508 **15K** | R512, C505, R513 **15K**, R514 **8.2K** |

Reading notes: the HT's VCO2 reset base resistor (1M, from line A to Q54) has no printed designator;
the netlist calls it R365, the one number of the HT's range R331-R391 not used elsewhere (NAVA
calls it R329, which on the SN is a tape-interface resistor). The HT's VCO1 47 Ω is printed "R35"
with its last digit under Q57's lead; NAVA has R351 there. The block diagram (p.4) lists the
LT parts with the MT's and HT's in brackets (IC15 (21, 43), Q19 (26, 57), Q21 (28, 59), IC18a (23a,
45a)), which agrees with the table.

## 4. Tom noise (tomnoise.cir; SN p.11 grid K-N 14-17)

- **Input.** The noise generator's output, IC33 pin 9 (0/15 V bits, RES A2.1), through **R190 22K**
  with **C161 0.0047** to ground (1.5 kHz).
- **IC27b**, a Sallen-Key high-pass at unity gain: **C51 0.0047**, **C52 0.0047** in series into
  (+), **R192 220K** (+) to ground, **R191 10K** from between the capacitors to the output, (−) on
  the output. f = 1 / (2π · 4.7 nF · √(10k · 220k)) = 722 Hz, Q = ½ √(220k/10k) = 2.35: with the
  low-pass ahead, a band-pass around 0.7-1.5 kHz. (OLD has it the same; RES called it a band-pass
  around IC27b without the topology.)
- **Two paths into IC27a's virtual ground** (**R193 22K** feedback): always **R194** (100k from SN
  426700; 47k before), a steady leak of gain 0.22 (0.47); and through **C53 0.047**, **R195 1K** into
  **R199 47** and the VCA **Q33** (2SC2603 (S), collector at R199, emitter at the virtual ground;
  the crop's emitter arrow, NAVA Q33.E on IC27's −IN).
- **The burst.** Any tom's TRIG through **D1** (LT), **D2** (MT) or **D3** (HT) and **R196 22K**
  (**R197 10K** to ground) turns on **Q34**, whose collector holds Q33's base down through **R200
  100K**. Meanwhile the triggered tom's G charges **C54** (0.0047 from SN 426700; 0.0022 before)
  through **D26** (LT), **D44** (MT) or **D45** (HT), less what **R198 100K** takes into Q34. When the
  trigger ends Q34 lets go and C54 runs down through R198 and R200 into Q33's base: τ about 0.9 ms
  (0.44 ms early), a burst of noise of about a millisecond after each hit's trigger, on top of the
  leak, about 20 dB above it at a plain hit (Q33's conductance, β_R · 12 µA / V_T, against R194). Which of D26/D44/D45 takes which tom: the three lines
  carry the markers □, □□ and □□□ of LT, MT and HT's G lines (as ■ for their TRIG lines and ▲ for
  their accent lines), and D26 is on □; NAVA has D45 on LT and D26 on HT. The cathodes all meet at
  C54, so it changes nothing.
- **The bus** (IC27a's output, the crop's "NOISE" arrow) goes into each tom's VCO3 47 Ω node through
  R114 (LT), R177 (MT), R387 (HT), 22K each: the bus reaches a tom's output only through VCO3's VCA,
  that is while ENV3 (26 ms) is open, and its burst arrives at the trigger's end, when ENV3 is at
  its peak.

## 5. Production changes (SN p.7)

- **SN 426700**, "TOM Noises: capacitor C54 from 0.0022uF to 0.0047uF, R194 47k to 100k ... to
  emphasize attack of TOM TOMs": the netlist's `.param c54` and `r194` are the late values; early
  boards set `c54=0.0022u r194=47k`. The crop prints both (0.0022 and 47K struck through, "SN
  426700-UP 100K", 0.0047 beside C54).
- **SN 415300**, C500-C512 0.01 µF at the multi-jack board's jacks: C503-C505 are in the toms' load
  (§2.9); before SN 415300 there were none (their effect on the toms' outputs is nil: IC23a drives
  them).
- R495 (the BD's surface-mounted part before SN 371499, and the RS TRIG OUT resistor from SN 415300)
  is not on the toms. Nothing else on p.7 touches them.

## 6. Disagreements and readings

- **R178/R118/R381 on the accent op-amp's (−) input** (RES: (+)): §2.1.
- **R169/R108/R353, ENV2 into VCO1's VCA** (RES: DECAY on VCO2 only): §2.7.
- **The reset transistors' orientation.** The crop's arrows and NAVA put the emitter on the
  integrator's (−) input; OLD has the collector there. The solver runs whichever, but the
  orientation sets which half of the triangle resets fast (§2.3).
- **The tom-noise filter** is a Sallen-Key high-pass after an RC low-pass (§4), as OLD has it.
- **Q34 keeps the burst off for the trigger**, which then comes at the trigger's end (OLD agrees;
  RES had "any tom trigger charges C54 through Q34").
- **R12a, R80**: misprints of R129 and R130 (§2.2, §2.3); **R365**, unprinted (§3).

## 7. Semiconductors

- **2SC2603, 2SA1115, 1SS133**: models.cir (shared). The VCAs' gain is β_R · I_B (§2.8), so the
  2SC2603's BR sets the toms' level, and its IS and ISE where the envelopes close them.
- **TC4069UBP (IC16, IC22, IC44)** (parts list p.2): the solver's level-1 MOSFETs, models.cir's
  `4069UB_N`/`_P` (fitted to the HD14069UB/MC14069UB typical table and TI's CD4069UB curves, for
  3-10 V supplies: the VCOs' VDD − VSS is 3.4-10.5 V; `docs/circuit/solver-mosfet.md`). Each pair of
  inverters in parallel is one N and one P of twice a gate's width (`4069UB_N2`/`_P2`, W × 2) with
  their input protection diodes as one (`DCMOS2`, IS × 2, RS / 2), as the snare's `_N3`/`_P3`. The
  channels' resistance (a few hundred ohms at these rails) is under 1.5 % of the 47k they drive, so
  the pitch hangs on VDD, VSS and the thresholds more than on the model. Until the solver had MOSFETs
  the netlists ran the same channels in the JFET equations (VTO 2.05 V, β 0.9 mA/V² a pair): the
  pitch differs by under 2 cents (§13).
- **M5218L**: models.cir's single-pole op-amp (`gbw` 7 MHz, `sr` 2.2 V/µs, `opsat` 13.5 V, from the
  Mitsubishi 1984 data book). The VCO comparators drive about 50 kΩ (R130 + R128, and R131 into the
  clamped inverter input), for which the data book gives VOM ±14 V typical (±12 V minimum; ±13 V
  into 2 kΩ), so they clip at `opsatlight` 14 V (models.cir): thresholds ±7 V. The pitch is
  inversely proportional to the thresholds: at the shared 13.5 V the toms were 4-5 % sharp of the
  AudioRealism unit, at 14 V within about 1 % (§10).

## 8. Partitions and numerics

The toms' cost is their VCOs. Each tom runs as eight partitions (`.part` lines, run in this order):

| Partition | What | Ports | Takes |
|---|---|---|---|
| `trig` | Q24/Q25/Q29, IC23b, TUNE, IC20 (VDD, VSS), the pitch envelope, the three envelopes and their resistors into the VCA bases, and copies of each VCO's reset diode and comparator input | 24 | the triangles, the VCA bases |
| `vco1`, `vco3`, `vco2` | a VCO each: reset diode, comparator, inverters, integrator, reset transistor, and its shaper resistor(s) | 15 | line A, VDD, VSS, the shaper node |
| `shape1` | VCO1's shaper: R164/R163, Q23 with C38 and D31, D38/D39, and R165 | 5 | the triangle, line A, the 47 Ω node |
| `shape3`, `shape2` | VCO3's and VCO2's shapers: their 10k, the diode pair, their 2.2k | 2 | the triangle, the 47 Ω node |
| `vca` | the 2.2k and 47 Ω, the VCAs, the tom noise's R177, IC23a and the jack board's load | 9 | the shaper nodes, the envelopes |

The cuts are where the signal is stiff: line A (Q24 through 100 Ω while it is driven), VDD and VSS
(IC20's outputs, regulated after their 1k), the triangles (op-amp outputs), the envelope
capacitors. Line A between hits floats and is dragged down by the lowest triangle through the
reset diodes and the comparators' input resistors (§2.1), so those are in `trig` as well, with the
triangles as inputs: a copy that takes the comparators' (−) inputs instead held line A up for
about 11 samples at the trigger's end (each sample the comparator input from the sample before),
and the VCOs started 0.1-0.2 ms late. A whole run and a partitioned one then agree to the hit's
first cycles; after that their VCOs drift apart in phase, which is the solvers' sensitivity, not
the cut (§9). The shapers are cut from the VCOs at their resistors from the triangles
(R164/R163, R181, R166: op-amp outputs, whose current through them hardly matters), which keeps
the VCO partitions at 15 ports. They are also cut from the VCAs at their 2.2k into the 47 Ω nodes
(R165, R176, R167). There the node is the 47 Ω's, stiff against the 2.2k, so the shaper sees it a
sample late, about 2 % of its current. This split replaced one 18-port `out` and took 14 % off
a tom's cost (`docs/circuit/solver-mosfet.md` §8). The tom noise: `filter` (linear, the noise in) and
`vca` (13 ports), cut at IC27b's output.

On a trigger edge every partition of the tom is to be substepped and jumped (`Circuit::substeps`,
`edge`, `jump`), not only `trig`: without it a VCO partition took line A's 15 V fall at the
trigger's end as a ramp across the sample and its comparator settled high, the wrong way.

**What the solver does with the VCOs.** A VCO's comparator is an op-amp in positive feedback,
clipped at a rail for milliseconds and slewing through each flip (28 V at 2.2 V/µs: 12 µs, more
than a sample). Before the solver's changes of 2026-10-17 (`docs/circuit/solver-mosfet.md`) this
cost Newton every sample (4.3-6.3 iterations; linear mode refused clipped and slewing op-amps), and
the backward-Euler step of such a comparator had a second solution, the output flipped by its own
feedback: now and then Newton landed in it and a triangle turned early (VCO3 0-7 times a hit, at
+0.5 to +1.8 V rather than +7 V; ngspice never did), and the flips came a few microseconds early
on the sample grid (the VCOs 0-6 cents sharp of ngspice's). The solver now gives an op-amp in
positive or no feedback a trapezoidal pole and redoes its flip samples in 16 steps: no early turns
in six hits of each tom (eighteen runs of each VCO), the VCO partitions 75-85 % linear (1.0-2.0
Newton iterations a step on average), and the toms within 0.05 % of ngspice (§9). What remains of
the cost is around each flip and in `out` (§11).

## 9. SPICE

`tools/spice/cases/toms.py`: per tom a plain, an accented and a full-accent hit (codes 10, 21, 47
with LEVEL 1), and TUNE 0 / DECAY 1 and TUNE 1 / DECAY 0 at code 21; the tom noise with an LT hit,
an HT hit at code 47 and MT and LT 1 ms apart. The toms take a 1 kHz sine for the tom-noise bus.
Each tom case first holds the trigger 10 ms at code 0, so that both solvers' VCOs start the hit
from the same reset (§2.3: a 2 ms reset from a low triangle doesn't finish, and the solvers'
free-running VCOs before it differ: neither has a DC operating point). Whole netlists, 96 kHz,
trigger edges substepped as the board does (`suite.py`).

| Case | out RMS % (attack / 5-50 ms / tail) | Envelope dB RMS (max) | Spectrum dB | Lines, cents |
|---|---|---|---|---|
| lt_10 | 0.047 (0.049 / 0.034 / 0.048) | 0.015 (0.06) | 0.008 | +0.0, +0.0, +1.1 |
| lt_21 | 0.049 (0.040 / 0.031 / 0.052) | 0.018 (0.09) | 0.008 | +0.0, +0.0, +0.0 |
| lt_47_level1 | 0.046 (0.038 / 0.031 / 0.049) | 0.018 (0.08) | 0.008 | +0.0, +0.0, +0.0 |
| lt_21_tune0_decay1 | 0.021 (0.007 / 0.030 / 0.019) | 0.017 (0.06) | 0.005 | +0.0, +0.0, +0.0 |
| lt_21_tune1_decay0 | 0.024 (0.016 / 0.030 / 0.022) | 0.005 (0.01) | 0.006 | +0.0, +0.0, +0.0 |
| mt_10 | 0.049 (0.146 / 0.112 / 0.020) | 0.013 (0.05) | 0.011 | +0.0, +0.0, +0.0 |
| mt_21 | 0.050 (0.128 / 0.111 / 0.023) | 0.018 (0.07) | 0.010 | +0.0, +0.0, +0.0 |
| mt_47_level1 | 0.051 (0.117 / 0.108 / 0.029) | 0.022 (0.09) | 0.010 | +0.0, +0.0, +0.0 |
| mt_21_tune0_decay1 | 0.021 (0.011 / 0.039 / 0.018) | 0.015 (0.05) | 0.007 | +0.0, +0.0, +0.0 |
| mt_21_tune1_decay0 | 0.038 (0.014 / 0.060 / 0.028) | 0.008 (0.02) | 0.010 | +0.0, +0.0, +0.0 |
| ht_10 | 0.031 (0.020 / 0.030 / 0.031) | 0.013 (0.04) | 0.007 | +0.0, +0.0, +0.0 |
| ht_21 | 0.035 (0.020 / 0.028 / 0.036) | 0.019 (0.05) | 0.008 | +0.0, +0.0, +0.0 |
| ht_47_level1 | 0.039 (0.035 / 0.029 / 0.040) | 0.023 (0.07) | 0.009 | +0.0, +0.0, +0.8 |
| ht_21_tune0_decay1 | 0.023 (0.005 / 0.030 / 0.022) | 0.020 (0.08) | 0.007 | +0.0, +0.0, +0.0 |
| ht_21_tune1_decay0 | 0.031 (0.035 / 0.062 / 0.011) | 0.003 (0.01) | 0.007 | +0.0, +0.0, +0.0 |
| tomnoise_lt10 | 0.112 | | | |
| tomnoise_ht47 | 0.232 | | | |
| tomnoise_mt21_lt10 | 0.125 | | | |

Envelope: 5 ms RMS, where within 40 dB of the peak; spectrum: 1/6-octave bands over the hit's first
150 ms; lines: the three strongest in 50-200 ms (the transform's bin is 3-4 cents). Out within
0.02-0.05 % RMS of its range on every tom case (the largest single difference 0.12-0.89 %, at a
flip), the tom noise within 0.25 % (the noise's fastest edges against the trapezoidal rule). The
toms' `g` outputs are within 0.5-0.8 % (their largest difference, 70-76 % of the range, is the one
sample at the trigger's end where G is let go). The DC operating points: neither solver has one
for the VCOs (ngspice now starts from the solver's state); the rest agrees.

Before the solver's comparator changes (§8) the same cases were 1-2 % RMS (the VCOs 0-6 cents
sharp, drifting in phase over the decay) and 5-6 % in `mt_21_tune0_decay1` and
`ht_21_tune0_decay1`, where the solver's VCO3 turned at +0.48 V at 24.5 ms and ngspice's went on
to +7.1 V.

## 10. Against the real units

`tools/compare/toms.py` plays each recorded setting through `tr909voicerun` (the board as the
plugin runs it, the tom noise included, the netlists read from disk), at AudioRealism's accent
code (21: its grid is an accented step at TOTAL ACCENT 0, `real909.ar_code`), puts the render
through the recording chain (`real909.chain`: AR's 6.5 Hz high-pass) and measures both sides with
`real909.measure` (`docs/reference.md`, Measures). The service notes' unit (`--nominal`) against
AudioRealism's, all 121 settings a tom; medians, model against unit (pitch in cents, decays in
octaves, level in dB after one offset for the recording chain, its spread the standard deviation):

| Tom | f_main (RMS) | f_low | f_early | f_main / f_low unit, model | t-20 | t-40 | tail τ | Level (sd) | LSD (pitch-aligned) | Attack HF |
|---|---|---|---|---|---|---|---|---|---|---|
| LT | +43 (74) | +8 | +25 | 1.471, 1.498 | −0.02 | −0.14 | −0.13 | +0.3 (0.3) | 6.8 (3.4) | +1.3 |
| MT | +11 (46) | −7 | −4 | 1.481, 1.497 | −0.04 | −0.17 | −0.15 | 0.0 (0.2) | 4.7 (2.8) | +0.8 |
| HT | +60 (80) | +78 | +58 | 1.478, 1.463 | −0.06 | −0.25 | −0.20 | −0.2 (0.2) | 8.3 (2.9) | +0.6 |

- **Pitch**: the TUNE knob's ends are the circuit's (with the comparators at ±7 V: at the shared
  13.5 V clip every tom was 4-5 % sharp), but the unit's knob moves the pitch little over its first
  and last 8 % (`docs/reference.md`, TUNE law), and each tom's VCO capacitors are a few per cent off
  (VCO1 / VCO2 1.471-1.481 on the unit, 1.498 / 1.497 / 1.463 nominal). §13 fits both.
- **Decay**: t-20 within 0.06 octave; the far tail short (t-40 −0.14 to −0.25, the tail's time
  constant −0.13 to −0.20). §13.
- **Level** within 0.3 dB between the toms and over every TUNE and DECAY: nothing to fit.
- **The tom noise** (attack HF) within 1.3 dB.
- **Accent** (AR's 11 takes, codes 21-47 through the firmware's table): the model rises +6.4-6.5 dB
  over the sweep against the unit's +6.8-7.1, within 0.6 dB at every take but 70-80 % (0.8-1.2 dB
  short). The 30 % takes of LT and HT read as TA 8-11 on the unit (the knob was set by hand per
  voice, `docs/reference.md`, Accent), where the model plays the BD's TA 6-7 (code 20, the
  table's dip): the −0.5 dB there is that.
- **Rob Roy's unit** (16 files a tom; pitch, decay, spectrum; its files are normalised and
  trimmed): f_main −19 / −55 / +4 cents, f_low −22 / +125 / −9 (its MT has VCO1 / VCO2 = 1.66, one of
  that unit's C32/C33 well off value), t-20 −0.08 / −0.01 / +0.01. Its toms sit 14-92 cents above
  AR's (`docs/reference.md`): the two units differ about as much as each differs from the
  service notes' values.

Before the AR grid's code was known (2026-10-17 morning) these comparisons ran at code 10 through
circuitrun; at code 21 the decays are 0.1-0.2 octave closer, the rest unchanged.

## 11. CPU

The solver's time per second of audio at 96 kHz, one core (Apple Silicon, machine lightly loaded),
partitions run in order as the board would (a scratch bench driving `Netlist::partitions`;
process CPU time), four hits in two seconds:

| Block | % of a core | Where |
|---|---|---|
| LT | 55 | `trig` 9.5, `vco1` 7.4, `vco3` 12.1, `vco2` 7.0, `out` 17.6 |
| MT | 58 | 9.9, 8.6, 13.2, 7.3, 18.2 |
| HT | 64 | 10.5, 10.1, 15.9, 7.6, 19.2 |
| tomnoise | 4.2 | `vca` linear between bursts |
| a tom whole (circuitrun) | 1200 | 84 ports |

The VCO partitions are 75-85 % linear; their cost is the flips (each in 16 steps) and the pitch
envelope's sweep. `out` was the largest: three diode shapers swinging through conduction and three
VCAs at V_CE ≈ 0 never stayed inside linear mode's windows (2.2-2.4 Newton iterations a step).
Before the solver's comparator changes the toms were 110-117 % each (the VCO partitions never
linear).

Since 2026-10-17, `out` is cut into `shape1`, `shape2`, `shape3` and `vca` (§8), and the solver
relinearises a device in place when it leaves its window (`docs/circuit/solver-mosfet.md` §8).
The shapers and the VCAs now stay in linear mode, and `trig` stays linear through a hit: before,
the accent amplifier in its knee at full accent sent `trig` to Newton for the decay.

`tr909voicerun`, one tom hit at codes 21 and 47 in a second, cycles at 3.23 GHz (the busiest
512-sample block is the second hit's), before the speed work, after its first pass, and after its
second:

| Tom | before | first | second | busiest block before | first | second |
|---|---|---|---|---|---|---|
| LT | 86 % | 45 % | 37 % | 144 % | 66 % | 48 % |
| MT | 88 % | 46 % | 39 % | 146 % | 68 % | 48 % |
| HT | 93 % | 48 % | 41 % | 149 % | 72 % | 51 % |

The second pass (`docs/circuit/solver-mosfet.md` §8) took most from the VCOs (the 4069UBs'
MOSFETs got windows of their own, a flip's samples stopped trying to linearise at every other
step) and from the tom noise's VCA, whose op-amp past its linear range had sent it to Newton for
half the block. The busiest block now goes about a fifth each to the VCOs' flips, the trigger
edges' substeps (quarters and sixteenths) and the updates of linear mode, the rest to the linear
steps and Newton between them. The toms still run as one job with the tom noise, coupled sample
by sample.

## 12. Open points

- **The cost** (§11): about 0.45 of a core a tom at 96 kHz, the flips the largest part.
- **No DC operating point.** The VCOs are astable: `solveDc` doesn't converge, and leaves the VCOs
  where it stopped. They start oscillating by themselves within milliseconds; a board reset should
  run the toms 50 ms or so after its DC solve. A hit doesn't care (line A resets the VCOs), except
  that where they were decides how far the reset gets (§2.3).
- **The far tail** stays 0.07-0.16 octave short at −40 dB after the calibration (§13).
- **Accent**: the model's sweep over AR's takes is 0.4-0.7 dB short at the top (§10).
- **The early board** (before SN 426700: `c54=0.0022u r194=47k`) is in the netlist's parameters
  but not checked against a recording of an early unit.
- **acc.** The board computes the DAC's open-circuit voltage with the latches' output resistance
  (`docs/circuit/interface.md`); the toms take it into op-amp inputs, so `rdac` doesn't matter.

## 13. Calibration

The service notes' toms against AudioRealism's unit, and what of it the parts' tolerances and the
knobs' laws take up (`unitCalibration()`, `core/voiceboard.cpp`; the lead merges the values). The
fit used AR's settings with TUNE and DECAY both in 0, 0.2 ... 1 (36 a tom) and was checked on the
25 with both in 0.1 ... 0.9, which it never saw, and on Rob Roy's unit. All through
`tr909voicerun` at code 21 with the recording chain (§10); the parts are `.param`s of the netlists
(`lt.c18`, `mt.vr13` ...: the netlists' `.param` line, generated from mt.cir's for LT and HT).

**Pitch.** Each tom's two lines over TUNE (11 settings, DECAY doesn't move them) set three things:
where the knob's track starts and ends (the knob law), the track's span (the pot's value, VR10/13/16
in the ladder R172/R173: V = 15 V · (10k + x · VR) / (78k + VR)), and each VCO's capacitor (the
frequency is 1/C). With the model's own pitch against V (the nominal renders) the fit is a few
lines of least squares; the law fitted jointly for the three toms (a free law per tom did no better
on the held-out settings):

| Value | Nominal | Fitted | Evidence (all from the unit's two lines at 11 TUNE settings) |
|---|---|---|---|
| ltTune, mtTune, htTune law | straight | flat to 0.08, straight to 0.935, flat after | the unit's pitch hardly moves over 0-0.1 and 0.9-1 on every tom (`docs/reference.md`: line fits end at 0.072-0.086 and 0.921-0.929) |
| lt.vr10 (VR10) | 10k | 10.6k (+6 %) | LT's span, ×1.97 on the unit |
| mt.vr13 (VR13) | 10k | 9.75k (−2.5 %) | MT's span, ×1.89 |
| lt.c18 (VCO1) | 22n | 22.4n (+1.8 %) | LT's VCO1 / VCO2 1.471 (C19/C18 is 1.5) |
| mt.c33 (VCO2) | 27n | 26.7n (−1 %) | MT's VCO1 / VCO2 1.481 |
| ht.c97 (VCO1) | 15n | 15.5n (+3.3 %) | HT's lines 60-80 cents sharp at every TUNE, ratio 1.478 (1.467 nominal) |
| ht.c102 (VCO2) | 22n | 22.9n (+4.3 %) | as C97 |

HT's VR and LT's and MT's other capacitor stay nominal: the fit left them within 1 %.

**Decay.** t-20 is right as drawn (−0.02 to −0.06 octave); the far tail is short (t-40 −0.14 to
−0.25, the tail's time constant −0.13 to −0.20), at every DECAY and most at DECAY 0. Three groups of
parts move it, each within its tolerance, on a search over the cross grid (×1, 1.1, 1.2):

- the DECAY pots (VR11/14/17): the tail at DECAY above 0 (×1.2: t-40 +0.05-0.07 octave, t-20 +0.02);
- ENV1's capacitors (C22/C36/C99, (T)): the tail at short DECAY and VCO1's (×1.2: t-40 +0.07, but
  t-20 +0.05-0.10, which was right);
- ENV2's (C35/C37, (T)): both (×1.15: t-40 +0.09, t-20 +0.14).

None takes the tail without lengthening t-20: the unit's tail is shaped differently, not just
longer (§2.8: below −30 dB the VCAs close as the envelope nears V_BE). The DECAY pots alone keep
t-20 and take half the tail's gap, so they are the fit:

| Value | Nominal | Fitted | Evidence |
|---|---|---|---|
| lt.vr11, mt.vr14, ht.vr17 (DECAY) | 500k | 600k (+20 %, the edge of the tolerance) | t-40 and the tail's time constant at DECAY > 0, all three toms short alike |

That all three pots want +20 % is itself suspicious (one batch, or a cause the pots stand in for);
flagged. With ENV1's capacitors at +20 % as well HT's t-40 would come to −0.09, at t-20 +0.06.

**Results**, model against unit, medians (RMS in brackets), nominal → calibrated:

| | Settings | f_main, cents | f_low, cents | t-20, oct | t-40, oct | tail τ, oct | LSD, dB | Level sd, dB |
|---|---|---|---|---|---|---|---|---|
| LT | fit (36) | +19 (54) → −2 (8) | −10 (49) → 0 (8) | −0.03 → +0.02 | −0.13 → −0.07 | −0.12 → −0.03 | 5.2 → 2.8 | 0.3 → 0.2 |
| | held out (25) | +48 (92) → −7 (18) | +19 (84) → −6 (19) | 0.00 → +0.02 | −0.14 → −0.06 | −0.14 → −0.04 | 7.1 → 3.1 | 0.3 → 0.2 |
| | Rob Roy (16) | −19 (39) → −32 (38) | −22 (39) → +1 (16) | −0.08 → −0.05 | −0.22 → −0.12 | | 5.8 → 5.2 | |
| MT | fit | +8 (42) → −2 (9) | −9 (39) → −1 (11) | −0.05 → +0.01 | −0.15 → −0.10 | −0.15 → −0.05 | 4.1 → 2.6 | 0.2 → 0.1 |
| | held out | +11 (50) → −13 (18) | −7 (50) → −12 (17) | −0.04 → +0.02 | −0.19 → −0.10 | −0.15 → −0.06 | 4.8 → 3.3 | 0.2 → 0.2 |
| | Rob Roy | −55 (65) → −58 (64) | +125 (141) → +142 (136) | −0.01 → +0.03 | | | 9.1 → 9.8 | |
| HT | fit | +64 (75) → −6 (10) | +82 (94) → 0 (12) | −0.05 → +0.01 | −0.25 → −0.15 | −0.18 → −0.14 | 9.3 → 3.0 | 0.2 → 0.1 |
| | held out | +41 (85) → −25 (27) | +59 (97) → −20 (23) | −0.04 → +0.02 | −0.26 → −0.16 | −0.21 → −0.14 | 6.7 → 4.2 | 0.2 → 0.2 |
| | Rob Roy | +4 (33) → −63 (63) | −9 (39) → −87 (89) | +0.01 → +0.05 | | | 5.3 → 7.7 | |

- **Held out**: the pitch holds to 18-27 cents RMS (the unit's own scatter is about ±1 Hz, 15-20
  cents, `docs/reference.md`), the decays as on the fitted settings. HT's held-out pitch is 25 cents
  low at the median: its odd TUNE settings lie under the line through its even ones (the
  hand-set knob).
- **Rob Roy's unit** moves away where it differs from AR's: it is 14-92 cents sharper than AR's
  (its HT most), so fitting AR's HT capacitors puts it 63 cents off, and its MT keeps its own
  VCO ratio (1.66). Its decays come closer (t-40 on LT −0.22 → −0.12; its other t-40s are under
  its trimmed tails' floor). Nothing in it argues against the values; it is another unit.
- **Unchanged**: levels (within 0.4 dB of each other, sd 0.1-0.2 dB), the tom noise (attack HF
  +0.5 to +1.2 dB), the accent sweep (§10), f_early (the pitch envelope's start, −6 to −11 cents
  after; −4 to +58 before).

**Accent against Regebro's unit** (his four takes, inferred as codes 10, 14, 21, 33: none, TOTAL
ACCENT only, the step's accent only, both; `docs/reference.md`, Accent), RMS 0-50 ms over the
first take, unit / calibrated model, dB: LT +3.0 / +3.8, +7.9 / +7.6, +10.4 / +11.1; MT +2.7 /
+3.7, +7.3 / +7.4, +10.0 / +10.9; HT +3.6 / +3.7, +7.6 / +7.4, +11.3 / +11.0 (TUNE and DECAY 0.5:
his knobs aren't stated). Within 1 dB at every step.

**Pitch after**, AR at DECAY 0.5, unit / calibrated model, Hz:

| TUNE | 0 | 0.1 | 0.2 | 0.3 | 0.4 | 0.5 | 0.6 | 0.7 | 0.8 | 0.9 | 1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| LT | 62.5 / 62.5 | 63.5 / 63.9 | 71.6 / 71.1 | 78.6 / 78.2 | 84.8 / 85.3 | 90.9 / 92.4 | 99.7 / 99.5 | 107.0 / 106.6 | 114.3 / 113.6 | 122.5 / 120.7 | 123.1 / 123.2 |
| MT | 78.7 / 78.6 | 81.6 / 80.2 | 88.7 / 88.5 | 97.0 / 96.7 | 103.7 / 104.9 | 113.6 / 113.1 | 122.0 / 121.3 | 131.1 / 129.5 | 137.9 / 137.6 | 146.9 / 145.8 | 148.4 / 148.6 |
| HT | 90.8 / 90.9 | 93.1 / 92.9 | 103.2 / 102.7 | 115.2 / 112.4 | 122.5 / 122.2 | 133.8 / 131.9 | 140.1 / 141.6 | 152.1 / 151.3 | 161.8 / 160.9 | 174.0 / 170.6 | 175.1 / 173.9 |

**The 4069UB.** The solver's 4069UB (V_T 1.15 V, fitted to the HD14069UB/MC14069UB tables)
against the JFET stand-in the toms ran on before (V_T 2.05 V): MT's lines within 2 cents at TUNE
0, 0.5 and 1 (78.41 / 78.47, 114.33 / 114.43, 150.01 / 150.15 Hz). The channels are a few hundred
ohms against the integrators' 47k, so neither model matches the units better; the netlists keep the
solver's, fitted to the data sheets.

**Dielectric absorption** (2026-10-22). ENV1's and ENV2's capacitors are tantalum ((T)): with
absorption in the model (`da=`, circuit.h: Dow's four R-C branches, 0.01-10 s) their charge given
back late lengthens the far tail and leaves t-20 alone, the shape the DECAY pots can't give, but
weakly: t-40 over the cross grid at 0 / 0.5 / 1 / 2 / 4 / 8 %: LT −0.07 / −0.07 / −0.04 / −0.04 /
−0.01 / 0.00, MT −0.10 / −0.10 / −0.09 / −0.09 / −0.09 / −0.06, HT −0.17 / −0.15 / −0.14 / −0.13 /
−0.12 / −0.11 octave. It doesn't stand in for the pots (with them nominal, 4 % takes 0.02-0.03 of
their 0.14-0.23). The netlists have 2 % (`dat`; modern MnO2 tantalum is about 0.5 %): over the full
grid t-40 −0.04 / −0.09 / −0.14, t-20 +0.01-0.02, LSD 2.88 / 2.67 / 3.13 dB.

**What remains**: the far tail (t-40 −0.04 / −0.09 / −0.14 octave after; the shape below −30 dB,
where the VCAs close: §2.8, `docs/circuit/s-transistors.md` §3.5), the accent sweep's top (0.4-0.7
dB short), the tom noise's attack (up to +1.2 dB), and the spread to Rob Roy's unit.
