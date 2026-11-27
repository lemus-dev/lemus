# VCO 1 and 2, their CV and modulation, wave shapers, ring modulator, mixer

Rev 1 main board KLM-127, service manual circuit diagram (1), top middle. Netlists:
`core/netlists/vcoctl.cir` (jacks to the exponential converters' collector currents),
`vco1.cir`, `vco2.cir` (cores, shapers, WAVE FORM switches, the ring modulator), `mixer.cir`
(VCO MIXER). ngspice cases: `tools/spice/cases/vco.json`.

## 0. Sources

| Key | What | Where (`private/ms20/docs/`) |
|---|---|---|
| **A** | Scan A, circuit diagram (1), 2217 x 1618, upright | `manuals/service_archive_a/img/a-003.png` |
| **T** | 1994 TIF, the same page, 1-bit, rotated: read turned 90° clockwise (3496 x 2485) | `manuals/service_hyperreal_1994_tif/tif/korg_ms20_4.tif` |
| **B** | Scan B, the same page, rotated: read turned 90° clockwise (3000 x 2132, about 0.858 of T) | `manuals/service_archive_b/img/b-003.png` |
| **G** | The later KLM-127 (Rev 2), 3937 x 3081: legibility only | `schematic/hyperreal_late_klm307_klm127/KLM-127.GIF` |
| **PL** | Parts list | `manuals/service_archive_a/img/a-007.png` |
| **BD** | Block diagram with check-point levels | `manuals/service_archive_a/img/a-008.png` |
| **SP** | Specifications | `manuals/service_archive_a/img/a-001.png` |
| **AP** | Adjustment procedure §7-2 | `manuals/service_archive_b/img/b-008.png` (= scan A a-009) |
| **TK** | K. Tanaka, "PUT VCO & 改良型 KORG PUT VCOの動作" (rev 0.20, 2025-08-14), text and scope shots | `forum/tanaka_put_vco/putvco_utf8.txt`, `img/` |

Regions read (pixels, x0 y0 x1 y1, in A and in T turned upright):

| Region | A | T |
|---|---|---|
| r_mod: TOTAL EXT, EXT FREQ, IC6B, IC4B, MASTER TUNE | 660 260 1230 450 | 1100 410 1920 680 |
| r_exp1: SW-2, ladder 1, IC3, Q6, VR1, VR2, R201-R203 | 790 400 1230 700 | 1280 640 1900 1040 |
| r_exp2: ladder 2, IC7, Q7, PITCH, VR5, VR6, R204-R206 | 790 640 1230 1080 | 1280 1000 1900 1600 |
| r_core1: C2-C4, Q1-Q5, D4, R13-R17 | 1190 250 1620 480 | 1900 390 2300 700 |
| r_shape1: IC2A/B, VR9, R1-R12, SW-1 | 1340 300 1820 580 | 2130 440 2560 860 |
| r_core2: C9-C12, Q8-Q12, D7, R57-R61 | 1180 640 1420 900 | 1880 980 2300 1300 |
| r_shape2: IC6A, SW-4, IC1, D1, D8, R3, R63-R70 | 1360 700 1700 1000 | 2180 1000 2620 1440 |
| r_mix: VR13, VR14, IC8A, R73-R77, R90, J13 | 1560 380 1800 960 | 2500 750 2800 1420 |

## 1. How it works, as drawn

**Modulation** [r_mod]. TOTAL EXT MODULATION (J4) is node 8, normalled to the MG triangle; R72
100K loads it and R71 10K feeds IC6B (pins 5, 6, 7; 6 tied to 7), a follower whose output goes
"TO VCF" and to the top of FM MG/T.EXT (VR15 100KA). VCO 1+2 EXT FREQ (J6, drawn "VOC-1+2") is
node 6, normalled to EG 1 ("FG-1"), across FM EG 1/EXT (VR16 100KA). IC4B (pins 5, 6, 7; ½ 4558)
sums the two wipers through R43 / R44 100K and MASTER TUNE (VR11 100KB, + to -) through R36 10M,
with R39 110K feedback and R37 33K on its + input. R40 5.11K / R38 100 take 19.2 mV per volt of
IC4B's output onto **vb**, the two log transistors' shared base. So a full knob gives 21.1 mV per
input volt at vb: 1.18 octaves a volt at 27 °C (SP: "(OCT/V)"; the model has R43 / R44 at 117.8k
for 1.00 since 2026-11-24, `rfm`: improvements.md, FM depth); MASTER TUNE ±3.17 mV, ±212 cents
(SP: ±100 cents, §3).

**Hz/V converter, VCO 1** [r_exp1]. VCO 1+2 CV IN (J26, node 3) feeds the footage ladder R25, R27,
R26, R24, R23, R22, R21, R20 (100K x 8, "0.1 % SELECTED") into IC3's - input (node 5, TL081, + input
grounded). SCALE (SW-2) has its common on node 5: it shorts the ladder from the chosen tap on, so
the input resistor is 100K at 4' (tap 50), 200K at 8' (51), 400K at 16' (52), and all 800K at
32' (an open contact). Each footage halves the current; the converter is linear in the CV.
- Q6 is a 2SC1583 pair with joined emitters (pin 3). Pins 4-5 (collector on node 5, base on vb)
  is IC3's feedback: IC3 drives the emitters through R28 10K, with C5 100P from node 5 to its
  output and D5 from the emitters to ground (it conducts only if they rise above +0.6 V).
- Pins 2-1 is the output: its base is VCO-1 PITCH ADJ (VR1 100KB, + to -) through R19 470K onto
  R18 100 (±3.19 mV, ±213 cents); its collector goes to the core through Q5.
- The output current is the input current times exp((v(b6) - vb) / V_T): **linear (Hz/V) in the
  keyboard, exponential in the modulation**.
- R203 220K / R201 1K from +15 V and R202 10M put +6.8 nA into node 5 at all times (at 0 V CV the
  core still runs, 0.19 Hz at 8').
- VR2 10KB, its wiper through R29 2.2K to -15 V, is on IC3's offset-null pins (1, 5): it moves the
  converter's input offset.

**VCO 2's converter** [r_exp2] is the same: VCO 2 CV IN (J24, node 4, normalled to node 3), the
ladder R41, R54, R52, R53, R48, R49, R50, R51 into IC7 (node 9) with 2' (tap 53), 4' (54), 8' (55)
and 16' (open) on VCO 2's SCALE switch (100K / 200K / 400K / 800K); C8 100P, R45 10K, D6; Q7
(pins 4-5 log, base vb; pins 2-1 output); VR5 10KB / R42 2.2K on IC7's null; R205 220K / R204 1K
/ R206 10M. Its output base takes VCO-2 PITCH ADJ (VR6 100KB through R56 470K) and the panel's
PITCH (100KB, drawn "VR2", through R55 75K: ±20.0 mV, ±1.11 octaves; SP: ±1 octave) onto R47 100.
VCO 2's ladder is twice VCO 1's at each footage name and C11 is about half C2, so the two match.

**The cores** [r_core1, r_core2]. C2 6200P (polystyrene, PL) hangs from +15 V ("+" box); the
converter's collector current, through Q5 (2SK30A-O, **gate grounded: a cascode**) and R15 10K,
pulls its lower plate (the ramp) down. Korg's discrete PUT resets it (TK):
- Q4 (PNP, emitter on +15 V: the anode) and Q3 (NPN, emitter on the ramp: the cathode) cross-
  coupled, Q3's collector on Q4's base;
- D4 from Q4's collector to the gate g1 (Q3's base) — the diode that TK calls the improvement Korg
  added from the PS-3000 series on;
- Q2, diode-connected (collector tied to base on g1), from g1 to the R17 / R16 427 / 427 divider
  (7.5 V), whose matching Vbe holds the threshold against temperature (TK).

When the ramp falls to about 7.5 V + V_T ln(β of Q4), the pair regenerates and dumps C2 back up to
about 1.5 V under +15 V (Q4's saturation, D4 and Q3's Vbe in the path), then lets go. C3 100000P
and C4 10µ/16V decouple the anode rail at the core (TK: the reset's current pulse). Q1 (2SK30A-GR,
drain on **+18 V**, the only use of that rail here) follows the ramp onto R14 10K; C1 10µ/16V
couples it to node 10 (check point 10, R13 10K): the sawtooth, falling (TK's scope: "DOWN SAW"),
7.5 Vpp on BD. VCO 2's core: C11 3000P, Q9 2SA564(S), Q10, Q11 2SC1685, D7, R57 / R58 427, C10
100000P, C9 10µ/16V, Q12 2SK30A-GR, R60 10K, C12 10µ/16V, node 11 (R61 10K), Q8 (cascode) and R59
10K.

**VCO 1's shapers** [r_shape1].
- *Pulse*: IC2A (pins 1, 2, 3) compares node 10 (- input) with PW (VR9 1MB, + input on its wiper)
  between node 56 (R10 300K from +15 V, R11 100K to ground) and node 57 (R12 1K to ground): 0 to
  3.48 V. Its output (the "RING" label: the ring modulator's VCO 1 input) goes through R2 100K onto
  R1 6.8K: ±0.89 V unloaded. Where the threshold passes the saw's peak the pulse goes away (SP:
  "Pulse width adjust 1:1 ~ 1:∞").
- *Triangle*: IC2B (pins 5, 6, 7) rectifies node 10. D2 (node 10 to R8 100K, into the - input)
  inverts the positive half; D3 (from the + input, R5 100K to ground, to node 10) follows the
  negative half; R9 100K feedback. The output, 0 to about -2.7 V, goes through R6 10K onto R7 100K
  from +15 V. A rectified falling saw has no step at the reset: a triangle.
- *Sawtooth*: node 10 through R4 100K.
- WAVE FORM (SW-1), drawn noise, pulse, saw, triangle from the top (the panel's order reversed),
  feeds VCO 1 LEVEL. WHITE NOISE comes from KLM-128.

**VCO 2's shapers and the ring modulator** [r_shape2].
- IC6A (pins 1, 2, 3) compares node 11 (- input) with its + input: R70 100K from +15 V over R69
  22K, 2.70 V (the narrow pulse), or ground where SW-4's second deck grounds it (square, ring).
  Output through R63 100K onto R64 6.8K: ±0.89 V.
- SW-4, drawn ring, narrow, square, saw from the top (panel order reversed). First deck to VCO 2
  LEVEL: ring output, the R63/R64 node (twice), node 11 directly (no series resistor drawn; real
  boards have R62 100K there, below). Second deck:
  a grounded wiper; the ring and square rows are joined to IC6A's + input.
- **Ring modulator**: IC6A's output through D8 (R68 100K to ground) is b, IC2A's output (VCO 1's
  pulse) through D1 (R3 100K to ground) is a: 0 / about 13 V logic. IC1 (µPD4011, four NANDs):
  (8, 9 -> 10) NAND(a, b); (12, 13 -> 11) NAND(10, b); (1, 2 -> 3) NAND(a, 10); (5, 6 -> 4)
  NAND(3, 11) = **a XOR b**. Pin 4 through R65 47K onto R66 100K (to -15 V) and R67 4.7K: -0.61 /
  +0.69 V unloaded. A digital ring modulator of VCO 1's *pulse* (so VCO 1's PW changes it; at PW
  past the saw's peak a stays high and the output is VCO 2's square inverted) and VCO 2's 50 %
  square.

**VCO MIXER** [r_mix]. IC8A (pins 1, 2, 3), inverting, unity per input: VCO 1 LEVEL (VR13 100KA)
from SW-1's common through R75 100K, VCO 2 LEVEL (VR14 100KB) from SW-4's common through R77 100K,
EXT SIGNAL IN (J13, node 12, grounded by the jack's switch when empty) through R90 100K; R73 100K
feedback, R74 33K on the + input. Its output is node 13 ("MAX 10 Vpp", BD), into the HPF.

## 2. Part by part

Read on: the sources (§0) where the value is legible; every value is in at least two Rev 1 scans.

### Modulation and converters (vcoctl.cir)

| Part | Value / type | Nets | Read on |
|---|---|---|---|
| J4 | jack, normalled to MG | n8 (input totalext) | A, T, B |
| R72, R71 | 100K, 10K | n8-0, n8-i6p | A, T, B |
| IC6B | ½ 4558 (pins 5, 6, 7) | + i6p, - = out o6 ("TO VCF") | A, T, B |
| VR15 | 100KA (FM MG/T.EXT) | o6 - w15 - 0 | A, T, B |
| R43 | 100K | w15-s4 | A, T, B |
| J6 | jack, normalled to EG 1 | n6 (input extfreq) | A, T, B |
| VR16 | 100KA (FM EG 1/EXT) | n6 - w16 - 0 | A, T, B |
| R44 | 100K (drawn metal film) | w16-s4 | A, T, B |
| VR11 | 100KB (MASTER TUNE) | +15 - w11 - -15 | A, T, B |
| R36 | 10M | w11-s4 | A, T, B |
| IC4B | ½ 4558 (pins 5, 6, 7) | + i4p, - s4, out o4 | A, T, B |
| R39, R37 | 110K (metal film, PL), 33K | s4-o4, i4p-0 | A, T, B, PL |
| R40, R38 | 5.11K (metal film, PL), 100 (metal film) | o4-vb, vb-0 | A, T, B, PL |
| R25, R27, R26, R24, R23, R22, R21, R20 | 100K x 8, 0.1 % selected | node 3 (vcocv) ... n5 | A, T |
| SW-2 | VCO-1 SCALE: 4' tap 50, 8' tap 51, 16' tap 52, 32' open | common n5 | A, T |
| R203, R201, R202 | 220K, 1K, 10M | +15-j1, j1-0, j1-n5 | A, T |
| IC3 | TL081 | + 0 (offset source), - n5, out o3 | A, T |
| VR2, R29 | 10KB (trim), 2.2K | IC3 pins 1-5, wiper - R29 - -15 | A, T |
| C5 | 100P | n5-o3 | A, T |
| R28 | 10K | o3-e6 | A, T |
| D5 | 1S1555 (PL: all diodes) | e6 (anode) - 0 | A, T |
| Q6 | 2SC1583 (G, PL) | pins 4-5-3: n5, vb, e6; pins 2-1-3: c6, b6, e6 | A, T, PL |
| VR1, R19, R18 | 100KB (trim), 470K, 100 | +15-w1--15, w1-b6, b6-0 | A, T |
| Q5 | 2SK30A-O ("K30A(O)") | D d5, G 0, S c6 | A, T, B |
| R15 | 10K | ramp1-d5 | A, T, B |
| J24 | jack, normalled to node 3 | node 4 (vco2cv) | A, T |
| R41, R54, R52, R53, R48, R49, R50, R51 | 100K x 8, 0.1 % selected | node 4 ... n9 | A, T |
| VCO 2 SCALE | 2' tap 53, 4' tap 54, 8' tap 55, 16' open | common n9 | A, T |
| R205, R204, R206 | 220K, 1K, 10M | +15-j2, j2-0, j2-n9 | A, T |
| IC7 | TL081 | + 0 (offset source), - n9, out o7 | A, T |
| VR5, R42 | 10KB (trim), 2.2K | IC7 pins 1-5, wiper - R42 - -15 | A, T |
| C8, R45 | 100P, 10K | n9-o7, o7-e7 | A, T |
| D6 | 1S1555 | e7 (anode) - 0 | A, T |
| Q7 | 2SC1583 (G) | pins 4-5-3: n9, vb, e7; pins 2-1-3: c7, b7, e7 | A, T |
| VR6, R56 | 100KB (trim), 470K | +15-w6--15, w6-b7 | A, T |
| PITCH ("VR2") | 100KB | +15-wp--15 | A, T |
| R55, R47 | 75K, 100 (metal film, PL) | wp-b7, b7-0 | A, T, PL |
| Q8 | 2SK30A-O | D d8, G 0, S c7 | A, T, G |
| R59 | 10K | ramp2-d8 | A, T |

### VCO 1 (vco1.cir)

| Part | Value / type | Nets | Read on |
|---|---|---|---|
| C2 | 6200P polystyrene (PL) | +15 - ramp1 | A, T, B, PL |
| C3, C4 | 100000P (25 V ceramic, PL), 10µ/16V | +15 - 0 (not in the netlist) | A, T, B |
| R17, R16 | 427 each (1 % metal film, PL) | +15-th1, th1-0 | A, T, B |
| Q2 | 2SC1685 (by VCO 2's twin, PL: 13 x 2SC1685S) | C = B g1, E th1 | A, T, B |
| Q3 | 2SC1685 (as Q2) | C k1, B g1, E ramp1 | A, T, B |
| Q4 | 2SA564 (by VCO 2's Q9 "A564(S)"; PL 2SA-564(S) x 4) | C a1, B k1, E +15 | A, T, B |
| D4 | 1S1555 | a1 (anode) - g1 | A, T, B |
| Q1 | 2SK30A-GR ("K30A GR") | D +18, G ramp1, S s1 | A, T, B |
| R14 | 10K | s1-0 | A, T, B |
| C1 | 10µ/16V, + on s1 | s1-n10 | A, T, B |
| R13 | 10K | n10-0 | A, T, B |
| R10, R11 | 300K, 100K | +15-n56, n56-0 | A, T, B |
| VR9 | 1MB (PW) | n56 - w9 - n57 | A, T, B |
| R12 | 1K | n57-0 | A, T, B |
| IC2A | ½ 4558 (pins 1, 2, 3) | + w9, - n10, out o2a ("RING") | A, T, B |
| R2, R1 | 100K, 6.8K | o2a-pulse, pulse-0 | A, T, B |
| R4 | 100K | n10-saw | A, T, B |
| R62 | 100K | node 11 - SW-4's saw contact (inferred) | not in A, T, B or G's circuit diagram; the manual's board layout (a-005) places it beside R63 at the J13 jumper line; board photos show it fitted on Rev 1 (KLM-127F, a 2008 Flickr set) and Rev 2 (KLM-127G), brown-black-yellow (`private/ms20/docs/photos/boards/`) | where it connects is inferred: VCO 2's saw then matches three real units' level (`compare.md` §1.4); `core/vco.cpp` kSaw2R since 2026-11-24 |
| D2 | 1S1555 | n10 (anode) - d2k | A, T, B |
| R8 | 100K | d2k - s2b | A, T, B |
| D3 | 1S1555 | i2p (anode) - n10 | A, T, B |
| R5 | 100K | i2p-0 | A, T, B |
| R9 | 100K (metal film) | s2b-o2b | A, T, B |
| IC2B | ½ 4558 (pins 5, 6, 7) | + i2p, - s2b, out o2b | A, T, B |
| R6, R7 | 10K, 100K | o2b-tri, tri-+15 | A, T, B |
| SW-1 | VCO-1 WF: noise, pulse, saw, triangle (top to bottom) | common c1 | A, T, B |

### VCO 2 and the ring modulator (vco2.cir)

| Part | Value / type | Nets | Read on |
|---|---|---|---|
| C11 | 3000P polystyrene (PL) | +15 - ramp2 | A, T, PL |
| C10, C9 | 100000P, 10µ/16V | +15 - 0 (not in the netlist) | A, T |
| R57, R58 | 427 each | +15-th2, th2-0 | A, T |
| Q11 | 2SC1685 ("C1685") | C = B g2, E th2 | A, T |
| Q10 | 2SC1685 | C k2, B g2, E ramp2 | A, T |
| Q9 | 2SA564(S) ("A564(S)") | C a2, B k2, E +15 | A, T |
| D7 | 1S1555 | a2 - g2 | A, T |
| Q12 | 2SK30A-GR | D +18, G ramp2, S s12 | A, T |
| R60, C12, R61 | 10K, 10µ/16V, 10K | s12-0, s12-n11, n11-0 | A, T |
| R70, R69 | 100K, 22K | +15-i6p, i6p-0 | A, T |
| IC6A | ½ 4558 (pins 1, 2, 3) | + i6p, - n11, out o6a | A, T |
| R63, R64 | 100K, 6.8K | o6a-sq, sq-0 | A, T (R64's "6.8" overlaps a contact circle on both; G reads 6.8K) |
| D8, R68 | 1S1555, 100K | o6a (anode) - nb, nb-0 | A, T |
| D1, R3 | 1S1555, 100K | VCO 1's "RING" (anode) - na, na-0 | A, T |
| IC1 | µPD4011C (PL) | pins as §1; supply not drawn | A, T, PL |
| R65, R66, R67 | 47K, 100K, 4.7K | pin 4-ring, ring--15, ring-0 | A, T |
| SW-4 | VCO2 W.F., two decks (§1) | common c2 | A, T |

### Mixer (mixer.cir)

| Part | Value / type | Nets | Read on |
|---|---|---|---|
| VR13 | 100KA (VCO 1 LEVEL) | c1 - w13 - 0 | A, T |
| R75 | 100K | w13-s8 | A, T |
| VR14 | 100KB (VCO 2 LEVEL) | c2 - w14 - 0 | A, T |
| R77 | 100K | w14-s8 | A, T |
| J13 | EXT SIG IN, switch to ground | node 12 (input extsig) | A, T |
| R90 | 100K | n12-s8 | A, T |
| R73 | 100K | s8-mix | A, T |
| R74 | 33K | IC8A + - 0 | A, T |
| IC8A | ½ 4558 (pins 1, 2, 3) | + i8p, - s8, out mix (node 13) | A, T |

On node 3 but not in these netlists (node 3 is an input, a stiff source): R207 22M from +15 V
(it pulls an open cable in J26 high) and R46 100 (IC5's output resistor, keyboard page). R46 and
the two ladders (133K at 8'/8') drop 0.075 % of the CV: 1.3 cents, different per SCALE setting,
for whoever joins kbdcv to vcocv.

## 3. Drawing errors and doubts

- **PITCH is labelled "VR2 100KB"** (twice) on A, T, B and G, the same designator as IC3's trim
  VR2 10KB. The panel pots are VR9-VR34; VR12 is the gap. The netlist calls it PVR2P.
- **Q5's gate line and the collector line.** Q5's gate runs left and down to ground, crossing the
  line from Q6's pin 2 to Q5's source; A and G draw a dot there. A junction would ground the
  converter's collector: it is a crossing. Q5 and Q8 are cascodes (research.md §1.3 says "clamp").
- **Node 10 and node 56 at IC2A** cross with no dot (A, T). A junction would bias the saw at 0.4 V
  and leave PW nothing to do: a crossing.
- VCO 2's SCALE switch carries no "SW-3" label (SW-1, -2, -4 do). "VOC-1+2 EXT FREQ" is a typo.
- The six trims carry the same filled "adjust" mark; none is numbered ❶-❹ (§4).
- **+15 V, not +18 V, charges C2 and C11** ("+" box at C2's top on A, T, B); +18 V feeds only Q1 and
  Q12's drains (their sources reach about 14.4 V). research.md §1.3 and README's "VP18 ... for the
  VCO cores" should read so.
- research.md's "each tap halving the voltage": the ladder is a series resistor into a virtual
  ground, each footage doubling it.
- **The saw's level** (§5): BD gives 7.5 Vpp at check points 10 and 11; the drawn core gives
  5.46 Vpp. Its top can't exceed +15 V less Q4's saturation, D4 and Q3's Vbe (13.52 V) and the
  divider sets 7.51 V, so with a +15 V anode 7.5 Vpp is out of reach. Two things say the real one is
  bigger: VCO 2's narrow pulse vanishes below 5.41 Vpp (the 2.70 V threshold), and BD's "MAX 10 Vpp"
  at check point 13 is what two 7.5 Vpp saws give (§5).
- MASTER TUNE: the drawn parts give ±212 cents, SP ±100 cents.
- BD's "MAX 10VnVPP" at 13 is read as 10 Vpp.
- IC1's supply pins aren't drawn: +15 V and ground assumed (its inputs swing 0 to 13 V).

## 4. Trimmers and the §7-2 procedure

AP §7-2: "normal setting" (SCALE 8', WAVE FORM pulse, MASTER TUNE, PITCH and all knobs at 0); VCO 1:
C-4, high ❶; C-1, low ❷; repeat; check C-1..C-4 within ±2 cents; check at 32', 16', 8', 4' within
±10 cents. VCO 2 the same with VCO 1 LEVEL 0, VCO 2 LEVEL 10, high ❸ and low ❹.

The schematic doesn't number its trims, so they are matched by what they do:

| AP | Trim | Why |
|---|---|---|
| high ❶ | **VR1** (VCO-1 PITCH ADJ, 100KB, R19 470K into R18 100) | moves the output base: the same cents on every note, set where the CV is largest |
| low ❷ | **VR2** (10KB, R29 2.2K, IC3's null) | an input offset: largest relative effect at the lowest CV |
| high ❸ | **VR6** (VCO-2 PITCH ADJ) | as VR1 |
| low ❹ | **VR5** (IC7's null) | as VR2 |

The netlist's model of a null (an assumption, no sheet gives its reach): an offset at the TL081's
+ input, -13 to +13 mV over the trim (`vnull`, the TL081C's full-range maximum offset, SLOS081 5.8).

**Computed** (`vcoctl.cir` in our solver for the current at each note, the ngspice core table §6
for its frequency; the keyboard as ideal Hz/V with C-4 = 8.4 V, BD's "0~8.4V", until keyboard.md
gives the ladder's values; targets C-4 = 1046.50 Hz, the C nearest the untrimmed pitch):

| | VCO 1 | VCO 2 |
|---|---|---|
| untrimmed C-1..C-4 (8') | 144.68, 288.77, 576.45, 1150.42 Hz | 149.75, 298.79, 596.56, 1191.26 Hz |
| trims | VR1 0.111, VR2 0.769 (+7.0 mV) | VR6 at its end (0), still +8.4 cents at C-4; VR5 0.961 (+12.0 mV) |
| §7-2 d, 8' (±2 c) | C-1 0.00, C-2 +2.28, C-3 +2.07, C-4 -0.01 | C-1 0.00, C-2 +6.16, C-3 +8.42, C-4 +8.41 |
| §7-2 e, 32' / 16' (±10 c) | +13.0, +10.8, +8.9, +7.0 / +5.0, +5.9, +5.3, +3.8 | 16': +6.4, +10.1, +11.4, +11.3 |
| §7-2 e, 4' (2') | -3.6, -1.0, -1.7, -5.1 | 4': -3.9, +3.2, +5.5, +4.9; 2': -6.8, +0.4, +2.0, +0.1 |

With C-4 = 8.0 V (SP: KBD CV OUT "0~+8V") both reach: VCO 1 VR1 0.309, VR2 0.760; VCO 2 VR6
0.165, VR5 0.740; 8': VCO 1 +2.30 / +2.09 c at C-2 / C-3, VCO 2 +1.36 / +1.21 c; 32' VCO 1 +13.3,
+11.0, +9.0, +7.0 c; 2' VCO 2 -7.3, -4.7, -5.3, -8.4 c.

What bends the tracking (the trims fix two points at 8' only):
- The 2SC1583's Early effect (models.cir VAF 30 V, from the sheet's output curves) against the
  cascode: Q5's source sits 0.78 V up at 5 µA, 0.66 V at 43 µA, so the output transistor's Vce
  falls with the current: the converter's gain is 1.034 at 1.3 µA, 1.023 at 43 µA (about 4 cents an
  octave of current).
- The core (§6): the trip rises at low current (7.534 V at 1 µA, 7.506 V at 40 µA), the reset
  costs 0.5 µs at the top.
- R202's 6.8 nA, nulled at 8' by VR2: four times its weight at 32'.
The 32' low end fails the ±10 cents by 3 cents; VCO 2 can't reach C6 at C-4 = 8.4 V (VR6's
±213 cents fall 8 cents short). Both are within what the provisional data (keyboard CV, 2SC1583,
2SK30A-O) move.

## 5. Levels against the block diagram

`vco1.cir`, `vco2.cir` in test mode with the ngspice core's ramp (7.51 to 13.52 V, 0.5 µs reset),
PW 0, LEVEL 10, the mixer's load on the switch (our solver):

| Point | Model | BD |
|---|---|---|
| ramp (C2, C11) | 6.02 V swing, 7.51 to 13.52 V | |
| node 10, node 11 (check points) | 5.46 Vpp | 7.5 Vpp |
| VCO 1 at SW-1: triangle | -0.65 to +1.15 V (1.81 Vpp) | |
| VCO 1: sawtooth | -0.90 to +0.92 V (1.82 Vpp) | |
| VCO 1: pulse | ±0.79 V (1.58 Vpp); gone above PW 7.8 | |
| VCO 2 at SW-4: sawtooth | -2.68 to +2.76 V (5.44 Vpp) | |
| VCO 2: square / narrow | ±0.79 V; narrow 14 % at a 7.5 Vpp saw, gone at 5.46 Vpp | |
| VCO 2: ring | -0.57 to +0.64 V (1.20 Vpp) | |
| mixer, node 13 | VCO 1 saw alone 1.82 Vpp, VCO 2 saw alone 5.44 Vpp, both ~7.3 Vpp | MAX 10 Vpp |

With BD's 7.5 Vpp saws the mixer's maximum is 2.5 + 7.4 = 9.9 Vpp: BD's 10 Vpp. VCO 2's saw goes to
the switch directly and VCO 1's through R4 100K: at equal LEVEL VCO 2's saw is three times VCO 1's.

**What the mixer hands the HPF**: IC8A's output (an op-amp: its closed-loop output impedance, near
zero), up to 10 Vpp (BD). On A, node 13 goes to the HPF through R100 20K onto node 58 (R104 47
to ground; filters.md), a load of about 20 kΩ.

## 6. The reset

**In our solver.** `vco1.cir` as drawn, from C2 at 1.5 V and 42 µA: the ramp falls to the trip,
then every sample fails Newton (6,273 failures in 7,680 steps at 1.536 MHz, 1.16 s CPU for 5 ms;
the same at 6.144 MHz, 4.65 s for 5 ms) and it never resets. From its DC point (the latch's unstable
equilibrium, conducting the charge current) it sits there with no Newton iterations at all: the
solver integrates the 6-7 pF junction capacitors by backward Euler, which turns the latch's
nanosecond unstable mode into a stable one at the sample's step. The solver has no charge storage
either, which is what times the real reset (TK).

**In ngspice**, `models.cir` alone isn't enough: with static transistors the latch has no fast mode
and ngspice stops with "timestep too small" at the trip. The netlists add the parasitics, marked:
RB 50 Ω on Q3 / Q4 (no sheet gives rbb'), Cob from the sheets (2SA564A 3.5 pF; 2SC945's 3.0 pF for
the 2SC1685), Cbe twice that, D4 3 pF. Then the core runs (full `vco1.cir`, `.ic` ramp 13.4 V):

| I | VCO 1 f | trip / top | trip to top | P - C (top - trip) / I | vs I / (C 6.016 V) |
|---|---|---|---|---|---|
| 1 µA | 26.93 Hz | 7.534 / 13.523 V | 1.25 µs | 5.4 µs | +7.7 c |
| 10 µA | 268.12 Hz | 7.509 / 13.523 | 0.52 µs | 1.02 µs | +0.1 c |
| 40 µA | 1071.64 Hz | 7.507 / 13.523 | 0.47 µs | 0.64 µs | -1.3 c |
| 100 µA | 2676.95 Hz | 7.507 / 13.523 | 0.45 µs | 0.57 µs | -2.6 c |
| 250 µA | 6679.96 Hz | 7.507 / 13.522 | 0.45 µs | 0.54 µs | -5.9 c |
| 400 µA | 10667.9 Hz | 7.508 / 13.522 | 0.45 µs | 0.54 µs | -9.1 c |

VCO 2 (C11 3000 pF): 0.24 µs trip to top at 40 µA, a dead time of 0.27-0.30 µs above 100 µA;
-1.9 c at 2.2 kHz, -3.0 c at 5.5 kHz, -5.9 c at 13.8 kHz. The full tables (both VCOs, 1 µA-400 µA)
were made with the scratch script described in §8.

TK measured 0.3 µs from the trip to the recharge on his built core (2SC1815/2SA1015, 5800 pF,
196 µA, 7.5 V) against 0.6 µs in his simulation (0.7 µs from trip to recharge for the diode
version). The netlist's 0.45-0.54 µs (VCO 1) sits between them. A scratch run with charge storage
instead of the parasitics (on the earlier provisional transistor values; TF 0.64 ns from the
2SC945's f_T 250 MHz, TR 50 ns assumed) gave 0.78 µs and a top 0.2 V higher.

**Top-octave droop** from a dead time t_d: f = f0 / (1 + f0 t_d). At 0.3 µs (TK measured): -1.1 c
at 2.1 kHz (VCO 1 4' C-4), -2.2 c at 4.2 kHz (VCO 2 2' C-4), -5.5 c at 10.7 kHz; at 0.54 µs (the
netlist's VCO 1): -2.0, -3.9, -9.9 c. The §7-2 high trim at 8' C-4 (1 kHz) takes up 0.5-1.0 c
of it.

## 7. The netlists

- **vcoctl.cir**. Inputs `vcocv vco2cv totalext extfreq ramp1 ramp2`; outputs `ia1 ia2` (amperes,
  the drop across a 1 Ω sense resistor at R15 / R59's top, not on the schematic), `tmod` (IC6B's
  output, "TO VCF"); probes `vb b6 b7 o3 o7 o4`. Controls `fm_mg fm_eg1 master_tune vco2_pitch`,
  `vco1_scale_32/16/8/4`, `vco2_scale_16/8/4/2`, trims `trim_VR1 trim_VR2 trim_VR5 trim_VR6`. The
  SCALE switches' open contacts are 1e12 Ω (the default 1 GΩ leaks 0.07 % of the CV at 32').
- **vco1.cir / vco2.cir**. Inputs `ia1` / `ia2` (current sources out of the ramps), `white`,
  `vco1sq` (vco2.cir); outputs `vco1` / `vco2` (the switch commons), `vco1sq` / `vco2sq` (IC2A /
  IC6A); probes `ramp1 g1 n10 saw pulse tri` / `ramp2 g2 n11 sq ring x4`. Controls
  `vco1_pw`, `vco1_wave_tri/_saw/_pulse/_noise`, `vco2_wave_saw/_square/_narrow/_ring`, the
  LEVEL pot of the mixer's load (below), `ramp_ext`.
  - The switch commons carry the mixer's load (VR13 / VR14 and R75 / R77 into IC8A's virtual
    ground, as ground), so `vco1` / `vco2` are the loaded voltages and mixer.cir, driven by them,
    gives the right currents.
  - **Test mode** `ramp_ext = 1` (not on the schematic): SPUT lifts the PUT's anode, SRX (1e15 Ω
    open) drives the ramp from input `ramp1x` / `ramp2x`: the buffer and shapers against ngspice.
  - The PUT parasitics (§6), marked. The ring modulator's NANDs are four level-1 MOSFETs each
    (models.cir's CD4011N / CD4011P), one unbuffered stage.
  - Not modelled: C3/C4, C9/C10 (an ideal rail), charge storage, the 4011's buffering and input
    protection, the rails' ripple.
- **mixer.cir**. Inputs `vco1 vco2 extsig`; output `mix` (node 13); controls `vco1_level
  vco2_level`.
- The 4558 and TL081 numbers are models.cir's (`gbw4558 ... sr4558`, `gbwtl ... srtl`).
- Two splits cross a passive node, against README's rule, both exact to what the solver resolves:
  the ramp (vcoctl.cir takes it as an input, the core takes the current as a source; the cascodes
  make the current independent of the ramp to under 0.005 %), and the switch commons (the mixer's
  load is in vco1.cir / vco2.cir, so the handed-over voltage is the loaded one; IC8A's virtual
  ground sits at under 0.1 mV).

## 8. ngspice and our solver

`tools/spice/cases/vco.json`, largest difference in % of each output's range (rms in brackets).
Every PWL breakpoint sits on the sample grid; a jump inside a sample is a ramp to the solver and a
step to ngspice.

| Case | Rate | Result |
|---|---|---|
| vcoctl_scales_sweep: CV, T.EXT, EXT FREQ ramps, 32' / 2', trims off centre | 96 kHz | all outputs ≤ 0.041 % |
| vcoctl_notes_mod: 7.35 V CV steps, modulation, MASTER TUNE, PITCH | 96 kHz | ia1 12.5 %, ia2 13.2 %, o3/o7 12.3 % at the steps' first samples (rms 0.39-0.41 %); tmod, vb, o4 ≤ 0.004 %. 537 of 2,880 samples fail Newton after the steps (the log converters' MHz loops) |
| vco1_ramp_saw / _tri / _pulse | 192 kHz | n10, ramp ≤ 0.022 %; saw 0.21 %; tri 11.1 % at one sample at the reset (rms 0.26 %); pulse and vco1sq 5.3 % at the comparator's edge where node 10 jumps (rms 0.28 %) |
| vco2_ramp_saw / _square / _narrow / _ring | 192 kHz | n11 ≤ 0.008 %, vco2 0.008-0.32 %, sq ≤ 0.32 %, vco2sq ≤ 0.32 %; ring and x4 1.27 % (a CMOS edge inside a sample; rms 0.04 %) |
| mixer | 96 kHz | mix 0.025 % |

Not counted: the PUT gate probes g1 / g2 (5-6.5 %), which float on leakage in test mode, and VCO 2's
comparator in the saw position (a 18 mV range). The core itself only runs in ngspice (§6): the
f(I) tables came from `circuitrun ... --spice` decks of the full vco1.cir / vco2.cir with
`.ic V(ramp)=13.4`, the period from the ramp's downward crossings of 10.5 V (scratch script, not
kept).

## 9. The running model: recommendation

**Netlist for the charge current, band-limited C++ core with the measured reset**, the Minimoog's
way (`machines/minimoog/core/vco.cpp`, `oscctl.cir`).
- `vcoctl.cir` at the voice rate gives `ia1`, `ia2` each sample (the cascodes make them independent
  of the ramp: under 0.005 % between 7.6 and 13.5 V). Measured with circuitrun (CSV writing included):
  1.0-1.3 µs a sample in linear mode, 4.5 µs while it substeps (constant inputs at 48 kHz): 5-22 %
  of a core at 48 kHz, 10 % at 96 kHz. Its Newton failures after big CV steps want a look (§8).
- The core in C++: the ramp falls at I / C; trip at 7.51 V (7.53 V at 1 µA, from the table); a
  band-limited step back to 13.52 V; a dead time t_d from the trip; the table's period is the
  check. t_d: TK's measured 0.3 µs, or the netlist's 0.5 µs (VCO 1) / 0.27 µs (VCO 2): the choice
  moves VCO 1's 2 kHz by 0.9 cents, its 10 kHz by 4.4 cents.
- The shapers as static transfers of node 10 / 11 (the buffer's gain 0.906 with C1's 1.6 Hz
  coupling; IC2B's rectifier; the comparators at their thresholds with the 4558's 1 V/µs slew, 28 µs
  edges, so BLAMP-shaped), the ring as the XOR of the two slewed squares' crossings of the 4011's
  threshold (about half its supply at the gates' inputs, one diode above that at the 4558: a rising
  edge lands about 22 µs after the comparator decides, a falling one about 6 µs).
- **Not the whole core in the netlist**: our solver can't step through the latch (§6), and even if
  it could, 0.3-0.5 µs needs 6 MHz or more: the shapers alone cost 1.4 µs a step at 1.536 MHz
  (2.2 x real time), the core ran 232 x real time while failing.

## 10. Open questions

1. The saw's level: 5.46 Vpp drawn against BD's 7.5 Vpp, and VCO 2's narrow pulse vanishing below
   5.41 Vpp. A recording's narrow pulse width or the saw/square ratio would settle it.
2. KBD CV at C-4 (8.4 V on BD, 8 V on SP): at 8.4 V VCO 2's high trim can't reach C6.
3. The offset nulls' reach (assumed ±13 mV); VCO 1's low trim needs +7 mV, VCO 2's +12 mV.
4. The reset's dead time: 0.3 µs (TK, measured on 2SC1815/2SA1015) or the netlist's 0.5 µs.
5. Whether the 32' low end really leaves ±10 cents (2SC1583 VAF, 2SK30A-O's V_GS).
6. MASTER TUNE ±212 cents drawn, ±100 cents in SP.
7. µPD4011C buffered or not; its supply.
