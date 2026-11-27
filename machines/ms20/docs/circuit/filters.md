# Filters and VCA (KLM-127): VC HPF, VC LPF, VCA, SIGNAL OUT

Netlists: `core/netlists/hpf.cir` (IC9 and its board), `lpf.cir` (IC10 and its board),
`vca.cir` (Q19 to SIGNAL OUT). Cases: `tools/spice/cases/filters.json`. Run each as
`models.cir,<block>.cir`.

**Sources.** Circuit diagram (1), right-hand side, in three copies of the Rev 1 drawing:

| Key | File (private/ms20/docs/) | Orientation |
|---|---|---|
| A | `manuals/service_archive_a/img/a-003.png` (2217 x 1618) | upright |
| T | `manuals/service_hyperreal_1994_tif/tif/korg_ms20_4.tif` (1-bit) | rotated 90 degrees clockwise to upright (3496 x 2485) |
| B | `manuals/service_archive_b/img/b-003.png` | rotated 90 degrees clockwise to upright (3000 x 2132) |

Crop boxes below are fractions of the upright page (x0 y0 x1 y1). HPF and its summer: A 0.76 0.16
0.99 0.41, T 0.75 0.17 0.97 0.52, B 0.80 0.18 0.97 0.43. LPF and its summer: A 0.74 0.60 0.87 0.82,
T 0.75 0.57 0.87 0.93, B 0.75 0.58 0.87 0.74. VCA to SIGNAL OUT: A 0.82 0.60 0.95 0.78, T 0.82 0.60
0.95 0.80, B 0.84 0.60 0.95 0.73. Q19's rank table: A 0.60 0.74 0.75 0.92, T 0.60 0.74 0.74 0.90.
Every value below was read in at least two of A, T and B unless the table says otherwise. The
later KLM-127/KLM-307 drawings (`schematic/hyperreal_late_klm307_klm127/`) are Rev 2 and were used
only where the table says so. The parts list is scan A's a-007 (§5); the block diagram a-008 (§6);
the adjustment procedure a-009 (§7).

**The KORG35's insides** are Korg's drawing (`schematic/korg35_korg_2000/KORG35.pdf` p. 3, Y. Mori,
2000-12-05; never shipped), cited to T. E. Stinchcombe, "A Study of the Korg MS10 & MS20
Filters" (2006, revised 2009), Fig. 1 on p. 5 (`papers/stinchcombe/MS20_study.pdf`), which has the
same values and types. K. Tanaka's redrawing (`forum/tanaka_korg35/img/35.jpg`) agrees on R1 47k,
R2 680, R3 470k and the 3.3 nF / 1 nF capacitors. Stinchcombe draws the MS-10 board; its values are
used only to read the module's pins against the MS-20 board, never for the MS-20's own parts.

**Element names.** The README's `<IC>_<part>` (`IC10_Q2`) cannot start a netlist line (the
first letter is the element's kind), so a KORG35 part is its kind letter followed by that name:
`QIC10_Q2`, `RIC10_R3`, `JIC9_Q4`.

## The KORG35 (IC9, IC10)

**How it works.** A Sallen-Key low-pass whose two resistors are transistors in saturation. Pin 8
(+15 V) feeds the base node of Q2 and Q3 through R3 470k (about 30 uA). Q2 runs from pin 3 to
pin 4, Q3 from pin 4 to pin 5, both with base and collector junctions forward biased (their
collectors and emitters sit near 0 V, the base node at 0.38 V): each collector-emitter path is a
resistance near V_T / (B_R I_B), set by the base current. Q1 is used only as a junction: base on the
base node, collector on the R1/R2 node, emitter open, so it is a diode that takes part of R3's
current away from Q2/Q3. Its cathode sits at V(pin 2) + (V(pin 1) - V(pin 2)) x 680 / 47680, so the
share left for Q2/Q3 is exponential in the pin 1 voltage: about 1 V per octave at pin 1 (checked
below). Pin 4 and pin 5 take the board's capacitors (3.3 nF to the feedback node, 1 nF to ground
or the input). The output buffer is a compound follower: Q4 (n-JFET) follows pin 5; its drain
current through R4 10k turns on Q5 (PNP, emitter on +15 V), whose collector feeds pin 7 and, through
R5 820, Q4's source, so Q4's drain current stays near V_BE / R4 and pin 7 follows pin 5 with a
gain of 1.08 and a DC offset of +2.38 V; R6 10k takes the source to pin 6 (the filtered -15 V).

| Part | Value / type | Pins, nets | Source |
|---|---|---|---|
| R1 | 47k | pin 1 - X (Q1's collector) | Korg p. 3; Stinchcombe Fig. 1 "R1 47k"; Tanaka |
| R2 | 680 | pin 2 - X | Korg p. 3; Stinchcombe "R2 680"; Tanaka "R0 680" (his text's 900 is a typo, research.md §2.1) |
| R3 | 470k | pin 8 - base node | Korg p. 3; Stinchcombe "R3 470k"; Tanaka |
| R4 | 10k | pin 8 - Q4 drain / Q5 base | Korg p. 3; Stinchcombe "R4 10k" |
| R5 | 820 | Q4 source - pin 7 | Korg p. 3; Stinchcombe "R5 820" |
| R6 | 10k | Q4 source - pin 6 | Korg p. 3; Stinchcombe "R6 10k" |
| Q1 | 2SC1623 | C X, B base node, E open | Korg p. 3; Stinchcombe "Q1 Q2SC1623" (same drawing: emitter unconnected) |
| Q2 | 2SC1623 | C pin 3, B base node, E pin 4 | Korg p. 3 (arrow toward pin 4); Stinchcombe |
| Q3 | 2SC1623 | C pin 4, B base node, E pin 5 | Korg p. 3; Stinchcombe |
| Q4 | 2SK94 (n-JFET) | D Q5 base, G pin 5, S R5/R6 | Korg p. 3 (Stinchcombe simulates a BF245A) |
| Q5 | 2SA812 (PNP) | E pin 8, B Q4 drain, C pin 7 | Korg p. 3: the arrow is on the lead to pin 8, pointing in; Stinchcombe "Q2SA812" the same |

Pins, read against the MS-20 board: 1 cutoff CV (IC12's output), 2 bias divider, 3 Q2's collector
(the low-pass input; grounded on the HPF), 4 Q2/Q3 junction (3.3 nF), 5 Q3's emitter and Q4's gate
(1 nF), 6 R6 to the filtered -15 V, 7 output, 8 +15 V. Stinchcombe's Fig. 1 has the MS-10's board on
the same pins (pin 2 on 680 into 220/15k, pin 6 through his R89 1k to -15 V).

## VC HPF: IC9, IC8b, IC12a (`hpf.cir`)

**How it works.** The KORG35 as Stinchcombe's high-pass (his §7): pin 3 grounded, the input fed into
the 1 nF capacitor's lifted end. The mixer's output (check point 13, IC8a) comes in through R100
20k into R104 47 ohm (node 56: a 426:1 divider, so the KORG35 sees millivolts), C15 0.001 uF from
there to pin 5; C26 0.0033 uF from pin 4 to the feedback node, R107 220 ohm to ground. Pin 7 drives
IC8b (non-inverting, 1 + R99/R101 for AC through C16) with D9/D10 back to back across R99: the
forward-path limiter. IC8b's output, through C14 33 uF, is check point 14 (to the LPF through
R108/R109) and the top of PEAK (VR19 10 kB, R92 2.2k from its top to its wiper); the wiper feeds
back through R106 5.6k || C27 0.001 uF into the feedback node: k2 = 220 / (5600 + 220) at full
PEAK. Stinchcombe's analysis: a 6+6 dB band-pass in parallel with a 12 dB high-pass, so 6 dB/oct
below cutoff.

The filtered -15 V: R110 470 ohm from -15 V with C25 220 uF to ground feeds pin 6 of both KORG35s
and both pin-2 dividers (R105 15k / R103 220 for IC9, R115 15k / R112 220 for IC10), so pin 2 sits
at -0.186 V; the rail settles at -12.874 V with 2.26 mA drawn by each filter.

| Part | Value | Nets | Read in | Notes |
|---|---|---|---|---|
| R110 | 470 ohm | -15 V - nf | A, T | |
| C25 | 220 uF / 16 V | nf (-) - ground (+) | A, T | parts list "16V-220uF x 1" |
| R105 | 15k | nf - h2 (pin 2) | A, T | |
| R103 | 220 ohm | h2 - ground | A, T | |
| IC9 | KORG35 | 1 hcv, 2 h2, 3 ground, 4 h4, 5 h5, 6 nf, 7 h7, 8 +15 V | A, T, B | parts list "KORG35 x 2" |
| R100 | 20k | mix (check point 13) - hin (node 56) | A, T | |
| R104 | 47 ohm | hin - ground | A, T | |
| C15 | 0.001 uF | h5 - hin | A, T | |
| C26 | 0.0033 uF | h4 - hfb | A, T | |
| R107 | 220 ohm | hfb - ground | A, T | |
| R106 | 5.6k | hfb - hpk (VR19's wiper) | A, T, B | the resistor beside C27 (label above it); research.md §1.3 and the brief call it IC8b's gain resistor: it is not |
| C27 | 0.001 uF | hfb - hpk | A, T | |
| IC8b | 1/2 uPC4558 | + pin 5 h7, - pin 6 h6n, out pin 7 hout | A, T, B | |
| R99 | 270k | h6n - hout | A, T, B | |
| D9, D10 | 1S1555 | back to back across R99 | A, T, B | parts list "1S-1555 x 33" |
| C16 | 10 uF / 16 V | h6n (+) - h101 | A, T, B | |
| R101 | **4.7k, PROVISIONAL** | h101 - ground | A, T, B: label "R101", value lost | see "Drawing doubts" |
| C14 | 33 uF / 16 V | hout (+) - hpf (check point 14) | A, T, B | parts list "16V-33uF x 2" (C14, C32) |
| R92 | 2.2k | hpf - hpk | A, T, B | |
| VR19 | 10 kB, PEAK | top hpf, wiper hpk, bottom ground | A, T, B | PCB view a-005 "HPF PEAK"; parts list "10KB x 4" |
| R108 | 2.2k | hpf - lin (LPF pin 3) | A, T, B | in `lpf.cir` (its input divider) |
| R109 | 47 ohm | lin - ground | A, T, B | in `lpf.cir` |

The cutoff summer IC12a (1/2 4558, pins 1-3): the panel's sources into a passive sum on R96 10k at
the + input, amplified 1 + R93/R98 = 22.3 (pin 2 on R98 to ground, read at A 9x: the line from
pin 2 runs down past pin 3 without a dot to R98; pin 3's lead runs right to R96 and the sum).

| Part | Value | Nets | Read in | Notes |
|---|---|---|---|---|
| IC12a | 1/2 uPC4558 | + pin 3 hbus, - pin 2 hsn, out pin 1 hcv | A, T | |
| R93 | 100k | hcv - hsn | A, T | |
| R98 | 4.7k | hsn - ground | A, T | |
| R96 | 10k | hbus - ground | A, T | |
| R91 | 470k | VR20's wiper - hbus | A, T | |
| VR20 | 100 kB, CUTOFF FREQ | + (+15 V) to - (-15 V) | A, T | `hpf_cutoff` |
| R78 | 100k | VR17's wiper - hbus | A, T | |
| VR17 | 100 kA, EG 2/EXT | top: J14's tip (EXT CUTOFF FREQ, normalled to FROM EG-2), bottom ground | A, T | `hpf_eg2`; check point 15 on J14 |
| R79 | 100k | VR18's wiper - hbus | A, T | |
| VR18 | 100 kA, MG/T.EXT | top: FROM T.EXT, bottom ground | A, T | `hpf_mg` |
| R76 | 1M | VR7's wiper - hbus | A, T | |
| VR7 | 100 kB trim, FC ADJ (adjustment 1) | + to - | A, T | `trim_vr7`; parts list "SR19R(100kB) x 7" |

## VC LPF: IC10, IC11a, IC12b (`lpf.cir`)

The same circuit as the low-pass: check point 14 through R108 2.2k into R109 47 ohm (47.8:1) at pin
3, C29 0.0033 uF pin 4 to the feedback node (R111 220 ohm), C28 0.001 uF pin 5 to ground; IC11a with
R117 270k, R116 4.7k / C31 10 uF and D20/D21; C32 33 uF to check point 16; PEAK VR22 10 kB with R127
2.2k; feedback R113 5.6k || C30 0.001 uF. Stinchcombe's k1 = 1.08 x (1 + 270/4.7) = 63.1, k2 max =
220/5820.

| Part | Value | Nets | Read in | Notes |
|---|---|---|---|---|
| R115 | 15k | nf - l2 (pin 2) | A, T | |
| R112 | 220 ohm | l2 - ground | A, T | |
| IC10 | KORG35 | 1 lcv, 2 l2, 3 lin, 4 l4, 5 l5, 6 nf, 7 l7, 8 +15 V | A, T, B | pin 3 from R108/R109 (A: the line from R108/R109 runs down the right edge and back along a faded, dashed-looking stretch to pin 3) |
| C29 | 0.0033 uF | l4 - lfb | A, T, B | |
| C28 | 0.001 uF | l5 - ground | A, T, B | |
| R111 | 220 ohm | lfb - ground | A, T, B | |
| R113 | 5.6k | lfb - lpk (VR22's wiper) | A, T, B | beside C30, as R106 (the brief has it as the gain resistor: it is not) |
| C30 | 0.001 uF | lfb - lpk | A, T, B | |
| IC11a | 1/2 uPC4558 | + pin 3 l7, - pin 2 l2n, out pin 1 lout | A, T, B | |
| R117 | 270k | l2n - lout | A, T, B | |
| D20, D21 | 1S1555 | back to back across R117 | A, T, B | |
| C31 | 10 uF / 16 V | l2n (+) - l116 | A, T, B | |
| R116 | 4.7k | l116 - ground | A, T, B | IC11a's gain resistor: 1 + 270/4.7 = 58.4, as the MS-10's R63 (Stinchcombe Fig. 1) |
| C32 | 33 uF / 16 V | lout (+) - lpf (check point 16) | A, T, B | |
| R127 | 2.2k | lpf - lpk | A only (rotated label "R127", "2.2K" beside C32, read rotated at 12x) | T and B print it over C32; Rev 2 KLM-307 has 2.2k + 10 kB for both PEAKs |
| VR22 | 10 kB, PEAK | top lpf, wiper lpk, bottom ground | A (rotated "VR22", "10KB"), T | PCB view a-005 "LPF PEAK"; `lpf_peak` |

IC12b (pins 5-7) as IC12a: R94 100k (out to -), R97 4.7k (- to ground), R95 10k (+ to ground);
R129 470k from CUTOFF FREQ VR21 100 kB (+/-15 V, `lpf_cutoff`), R130 100k from EG 2/EXT VR24 100 kA
(top: J21's tip, normalled to FROM EG2; check point 17; `lpf_eg2`), R131 100k from MG/T.EXT VR23
100 kA (FROM T.EXT; `lpf_mg`), R128 1M from FC ADJ VR8 100 kB (adjustment 2, `trim_vr8`). All read
in A and T (A 0.74 0.66 0.87 0.82 at 3x, T 0.75 0.68 0.87 0.93 at 2.4x).

## VCA: Q19, IC11b, VOLUME, SIGNAL OUT (`vca.cir`)

**How it works.** Q19 is the KORG35's trick once more: a saturated transistor as a current-set
resistor. Check point 16 comes through R123 6.2k into R121 220 ohm (29:1) at Q19's collector; its
emitter goes through C35 10 uF to IC11b's virtual ground (inverting, R118 180k || C34 100 pF), R120
100k holding it at 0 V for DC. Q19's base takes its current from EG 2 (FROM EG2, R125 100k with
R122 1M to ground) or from INITIAL GAIN (J22, through D22) through the one fitted base resistor
(R124 1M or R126 680k). With no base current the collector-emitter path is open; with I_B it is
about V_T / (I_B B_F B_R / (B_F + B_R)), so the gain is about 180k / (29 r_CE): linear in the base
current and in Q19's reverse beta. IC11b's output goes through C33 1 uF and R119 1k to check point
19 and the VOLUME pot, whose wiper is SIGNAL OUT (J35) and the phones amp's input.

Q19's orientation: A and B (7x) show the arrow on the right-hand lead (to C35/R120), pointing out;
the Rev 2 KLM-307 drawing draws its VCA transistor Q3 the same way (emitter to IC-5's virtual
ground, collector on the 220 ohm signal node).

| Part | Value | Nets | Read in | Notes |
|---|---|---|---|---|
| R123 | 6.2k | lpf (check point 16) - vc | A, T, B | |
| R121 | 220 ohm | vc - ground | A, T, B | |
| Q19 | 2SC945, "C945 SELECTED" | C vc, B vb, E ve | A, T, B | parts list "2SC-945(L)K x 1 (special selected)" |
| R120 | 100k | ve - ground | A, T, B | |
| C35 | 10 uF / 16 V | ve - v6n (+) | A, T, B | |
| IC11b | 1/2 uPC4558 | + pin 5 ground, - pin 6 v6n, out pin 7 vout | A, T | |
| R118 | 180k | v6n - vout | A, T | |
| C34 | 100 pF | v6n - vout | A, T | |
| C33 | 1 uF / 50 V | vout (+) - v33 | A, T | parts list "50V-1uF x 6" |
| R119 | 1k | v33 - cp19 (check point 19) | A, T | |
| VOLUME | 10 kB (no designator) | top cp19, wiper sigout, bottom ground | A, T: drawn without designator or value | value from the Rev 2 KLM-307 drawing ("10KB"); parts list has "Printed 10KA x 1" and "Printed 10KB x 1" |
| J35 | SIGNAL OUT | tip on the wiper | A, T | spec: 2 Vpp, 3.5 kohm |
| R125 | 100k | eg2 (FROM EG2, TO VCA) - vctl | A, T | |
| R122 | 1M | vctl - ground | A, T | |
| D22 | 1S1555 | anode initgain (J22, check point 18), cathode vctl | A (10x), T | J22 has no normal: unplugged its tip is open |
| R124 | 1M | vctl - vb | A, T, B | fitted for rank Green or Blue |
| R126 | 680k | vctl - vb | A, T, B | fitted for rank Orange or Yellow |

The phones amp (KLM-128, `phones.cir`) takes the wiper through R42 and R47 4.7k into two virtual
grounds: `vca.cir` carries them as R42L/R47L (2.35 kohm to ground), so the split at the wiper is
exact. `rload` (100k) is whatever J35 drives.

### The VCA's rank

The table beside the drawing (A and T; the Rev 2 KLM-127 and KLM-307 drawings have the same idea
with R44 330k / R45 240k):

| Q19 rank | R124 1M | R126 680k |
|---|---|---|
| Orange | not fitted | fitted |
| Yellow | not fitted | fitted |
| Green | fitted | not fitted |
| Blue | fitted | not fitted |

What it means: Korg sorted the 2SC945s (NEC rank K, h_FE 300-600 at 6 V, 1 mA, the parts list's
"(L)K") into four colour grades in the usual ascending order (orange, yellow, green, blue: the order
of the O/Y/GR/BL h_FE grades), and fitted the larger base resistor to the higher grades. Since the
VCA's gain is the base current times the transistor's gain-like term, the table makes every unit's
full-envelope gain about the same: 1M against 680k is 1.47:1, the spread between the two halves of
the sort. No document says what was measured or where the grade boundaries lie.

Chosen: **Orange/Yellow (R126 680k, R124 open)**, as `.param r124=1e12 r126=680k`. Reasons: (1)
`models.cir` gives the 2SC945 h_FE 424 (the geometric middle of rank K), which an even split of
300-600 into four grades puts on the yellow/green boundary, so the data alone cannot choose; (2)
the block diagram gives check point 19 "MAX 2VPP", and with the drawn parts and the fitted models
the largest level the VCA delivers there is 0.95 Vpp with R126 against 0.69 Vpp with R124 (a 10 Vpp
saw through both filters wide open, EG 2 at 5 V; below): R126 is the nearer, and both fall short
(open question 3). Green/Blue is `--param r124=1meg --param r126=1e12` (the case
`vca_rank_green_blue`). `models.cir`'s `br945` (Q19's reverse beta, 2.665 from the 2SC1623 die) was
left alone: reaching 2 Vpp would need about 5.6 with R126, outside the 1.7-4.4 that the die's
V_CE(sat) curves allow (semiconductors.md).

## Drawing doubts and errors

1. **R101's value is lost** in all three copies: the label "R101" is printed, the value is a lone
   "K" with the digits gone under the ground symbol (A at 18x, T and B pixel by pixel). The netlist
   takes 4.7k (`.param r101`), the value of the same-role R116 in the LPF (legible) and of R63 on
   the MS-10 (Stinchcombe Fig. 1). The circuit argues for it: with 4.7k the HPF starts to oscillate
   at PEAK 8.6, as the LPF does; with 1k (the other reading the faint mark allows) it would oscillate
   from PEAK 0.4 (below), leaving almost the whole PEAK knob in self-oscillation, which no reading
   of the panel ("flat ~ self OSC", spec §1) supports.
2. **research.md §1.3 and the brief** have IC8b's gain as 270k/5.6k: R106 (5.6k) is the PEAK
   feedback resistor beside C27, and R113 likewise in the LPF; the gain resistors are R101 and R116.
   research.md §2.1's "7 output (Q4 source via R5, Q5 emitter)" has Q5 the wrong way round: Q5's
   emitter is on pin 8, its collector on pin 7 (Korg p. 3, Stinchcombe Fig. 1).
3. **R127's value** is legible only in A (rotated beside C32); T and B print it over C32.
4. **VOLUME** has no designator or value on the Rev 1 drawing (A, T, B); the parts list's "Printed
   10KA" and "Printed 10KB" are both candidates; 10 kB is the Rev 2 KLM-307's.
5. The parts list's rotary pots have "100KA x 8" and "100KA x 4" and no 100 kB, while the drawing
   (A, T) has VR20 and VR21 (CUTOFF) and VR14 as 100 kB: the second line is probably 100 kB.
6. SIGNAL OUT's specified "output impedance 3.5 kohm" cannot come from these parts: R119 1k into a
   10k pot loaded by the phones amp's 2.35 kohm gives 0.66 kohm at full volume and 1.27 kohm at
   most.
7. The specification's cutoff ranges (HPF 50 Hz-5 kHz, LPF 50 Hz-15 kHz) are narrower than the
   drawn parts give (below); the two filters' control paths are identical, so the different upper
   figures are not a circuit difference.

## Splits, stiff nodes and partitions

- Inputs: `mix` (IC8a's output), `eg2` (EG 2's TO VCA, IC21's output), cutoff jacks and T.EXT: all
  op-amp outputs or jacks. The filters' FROM EG-2 normal (J14, J21) is EG 2's TO VCF, which
  `eg2.cir` gives through R161 (printed 1k, 33k there since 2026-11-25): VR17 and VR24 load it
  with 50 kohm, which `eg2.cir` carries (RVCFLOAD).
- Check points 14 and 16 are C14/C32's far sides (33 uF, 4.8 ohm at 1 kHz): split there, with the
  next block's input as a load (R108L/R109L in `hpf.cir`, R123L/R121L in `lpf.cir`). Left out: the
  KORG35's pin 3 current through R109's 47 ohm node and Q19's path in parallel with R121.
- The filtered -15 V (C25: 7 ohm at 100 Hz) is shared: each netlist has R110/C25 (the LPF as
  R110L/C25L) and the other filter's DC draw (2.2615 mA) as a constant current (`ilpf`, `ihpf`),
  from the joined operating point (-12.874 V). The signal currents' cross-talk through the rail is
  left out.
- SIGNAL OUT (the VOLUME wiper) is not stiff, but the phones amp's inputs are virtual grounds, so
  with R42L/R47L in `vca.cir` the split is exact.
- Inside each filter there is no stiff node: the KORG35, its gain stage and the PEAK network form
  the resonance loop and must stay in one partition (a split would put a sample's delay in the
  loop). The cutoff summer (IC12a/b, up to the op-amp's output; R1 47k is the KORG35's) can be its
  own linear partition.

## Checked

All numbers with `models.cir` as of 2026-11-15 (the fitted models: 2SC1623 B_R 2.665, 2SK94 as the
MMBF5457, 1S1555 IS 10 nA N 1.95, 4558 3 MHz / 1 V/us), FC ADJ at the 7-4 setting, R101 4.7k,
Q19 Orange/Yellow. Scratch scripts, not in the repo; each number says how it was made.

### ngspice

`tools/spice/suite.py --only filters` with **MS20_LINTOL=1e-6** (largest difference over the case,
in % of each output's range; all outputs of each case):

| Case | What | Worst |
|---|---|---|
| hpf_sine_drive | 500 Hz, 8 Vpp at check point 13, PEAK 6: deep in the diodes | 0.30 % |
| hpf_square_cv_sweep | 200 Hz square, 2 Vpp, PEAK 8, EXT -2 to +1 V (768 kHz) | 0.87 % |
| hpf_mg_sine | 2 kHz sine, MG/T.EXT 7 with a 40 Hz +-5 V triangle, CUTOFF 3, PEAK 4 | 0.51 % |
| hpf_self_osc_locked | PEAK 10, CUTOFF 6, a 1 kHz 4 Vpp sine pulling it (384 kHz) | 0.41 % |
| lpf_square_ringing | 100 Hz square, +-70 mV at check point 14, CUTOFF 7.2, PEAK 6 | 0.24 % |
| lpf_sine_drive | 300 Hz, 0.6 Vpp, CUTOFF 6, PEAK 8 | 0.06 % |
| lpf_eg_saw | 110 Hz saw, 0.4 Vpp, EG 2 0.8 with an envelope on J21, PEAK 7 | 0.90 % |
| lpf_self_osc | PEAK 10, CUTOFF 6, started by a kick (768 kHz, 7 ms) | 0.70 % |
| vca_env | 500 Hz, 0.6 Vpp, EG 2 0 -> 5.3 -> 3 -> 0 V | 0.04 % |
| vca_initgain | 1 kHz, INITIAL GAIN 0 -> 3 -> 0 V, VOLUME 6 | 0.36 % |
| vca_rank_green_blue | vca_env with R124 1M | 0.06 % |

**With the solver's default linear-mode tolerance (1e-4 V)** every KORG35 case is off by 1.5-64 %
(phase drift: the cutoff is exponential in the base node's voltage, so 0.1 mV there is 0.4 % in
frequency) while the VCA stays at 0.04-0.36 %. At 1e-6 V the KORG35 blocks still take 0.01-0.35
Newton iterations a sample. **The filter partitions need MS20_LINTOL 1e-6 (or a per-circuit
tolerance)**: an open item for the solver. Free-running oscillation is compared over a few
cycles only: its frequency converges from both sides as the step falls (LPF at CUTOFF 7, PEAK 10:
ours 2474.9 / 2474.4 / 2474.1 / 2473.8 Hz at 192 / 384 / 768 / 1536 kHz, ngspice 2473.3 / 2473.5 /
2473.6 / 2473.7 Hz), so the phase drifts by a percent within 10-30 cycles.

### Cutoff law (Stinchcombe: 1 V/oct)

ngspice AC, LPF, PEAK 0, pin 3 to pin 7; the natural frequency (pin 7 at -90 degrees) against the
EXT CUTOFF voltage (J21, VR24 at 10, CUTOFF 5):

| J21 (V) | pin 1 (V) | f_n (Hz) | -3 dB (Hz) |
|---|---|---|---|
| -3 | -5.63 | 11.8 | 4.4 |
| -2 | -3.82 | 38.5 | 16.1 |
| -1 | -2.01 | 137.3 | 59.2 |
| 0 | -0.20 | 500.2 | 217.4 |
| +1 | +1.61 | 1814 | 785 |
| +2 | +3.42 | 6347 | 2689 |
| +3 | +5.24 | 20116 | 7970 |

At pin 1: 0.97-1.03 V per octave from 21 Hz to 6.3 kHz, 1.06 from 6.3 to 11.5 kHz, 1.1-1.6
above (Q1 takes less and less of R3's current); below about 12 Hz the law breaks. At J21: 1.85-1.87 octaves per
volt in the middle (spec: "2OCT/V"). The HPF's core is the same circuit.

### Self-oscillation onset (Stinchcombe: k1k2 about 2 1/3)

The PEAK setting from which a 20 mV kick grows instead of dying (circuitrun 192 kHz, bisected):
since BR 4.35 (2026-11-23), HPF and LPF alike at PEAK 7.4-7.6 for CUTOFF 3-7 and 6.3 at CUTOFF 9
(real units: self-oscillating at PEAK 8, both filters, `improvements.md`). With BR 2.665 it was
8.57-8.63 for CUTOFF 3-9, where k1k2 = 63.1 x 0.0343 = 2.17 (k2 with R92/R127 across VR19/VR22's
upper part); full PEAK is k1k2 = 2.39: Stinchcombe's 2 1/3 is the linear threshold of the drawn
values with ideal transistor resistances, and Q2/Q3's reverse beta moves the real one. With R101 =
1k the HPF would start at PEAK 0.40-0.51 (BR 2.665). The oscillation is limited by the diodes: 0.355 Vpp at check point 14/16
at 500 Hz.

### Asymmetric ringing (Stinchcombe: 1.7 kHz positive, 2 kHz negative)

LPF, 100 Hz square into check point 14, CUTOFF 6.55, PEAK 6 (Stinchcombe's VR13 at 60 %),
circuitrun 384 kHz, 0.3-0.4 s (settled): the ringing after each edge, from the first four zero
crossings after removing the slow drift (the LPF does not invert: a rising input edge is the
positive half):

| Square at check point 14 | Output | Positive half | Negative half | Ratio |
|---|---|---|---|---|
| +-5 mV | 0.025 Vpp | 1668.5 Hz | 1682.1 Hz | 1.008 |
| +-20 mV | 0.096 Vpp | 1642.3 Hz | 1699.4 Hz | 1.035 |
| +-50 mV | 0.208 Vpp | 1571.5 Hz | 1710.7 Hz | 1.089 |
| +-70 mV | 0.259 Vpp | 1515.4 Hz | 1709.1 Hz | 1.128 |
| +-90 mV | 0.298 Vpp | 1490.7 Hz | 1754.7 Hz | 1.177 |

The positive half rings lower than the negative, as Stinchcombe's, and the split grows with level;
at Stinchcombe's output level (about 0.25-0.3 Vpp) it reaches his 1.18:1. Beyond that the diodes
flatten the ringing.

### HPF slope (Stinchcombe: 6 dB/oct)

ngspice AC, node 56 to pin 7: 5.97-5.99 dB/oct between f_3dB/64 and f_3dB/32 at PEAK 0 (CUTOFF 3, 5,
7), 5.9-6.0 dB/oct at PEAK 6 and 9.

### Diodes in the forward path (Stinchcombe: distortion at every frequency)

THD at check point 16 / 14 against level (circuitrun 192 kHz, 0.1 s of whole cycles):

| Where | 0.1 / 1 Vpp in | 0.4 / 2 Vpp | 1 / 4 Vpp | 2 / 8 Vpp | 8 / 16 Vpp |
|---|---|---|---|---|---|
| LPF passband (200 Hz, CUTOFF 9.2, PEAK 3; LPF column 1) | 0.55 % | 7.1 % | 17 % | 23 % | 28 % |
| LPF at cutoff (1 kHz, CUTOFF 6.2) | 2.6 % | 10.8 % | 19 % | 25 % | 41 % |
| LPF stopband (4 kHz, CUTOFF 4) | 0 | 1.0 % | 2.4 % | 4.7 % | 19 % |
| HPF passband (4 kHz, CUTOFF 3; HPF column 2) | 0.65 % | 2.6 % | 8.0 % | 15 % | 21 % |
| HPF stopband (100 Hz, CUTOFF 5) | 0.31 % | 0.64 % | 1.7 % | 4.9 % | 12.7 % |

(LPF rows: input at check point 14, 0.1, 0.4, 1, 2, 8 Vpp; HPF rows: at check point 13, 1, 2, 4,
8, 16 Vpp.) Odd harmonics in the passbands (the symmetric limiter), second harmonic near and below
cutoff (the KORG35's own asymmetry).

### Service manual §7-4: 500 Hz at CUTOFF 5, PEAK 10

Bisected on circuitrun 384 kHz, measured over 40-80 ms: **VR7 at 0.4616** of its rotation (wiper
-1.15 V) gives the HPF 500.0 Hz; **VR8 at 0.4628** (-1.12 V) the LPF 500.0 Hz; both inside the
trims' range. Since BR 4.35 (2026-11-23) `ms20service calibrate` gives **VR7 0.3305**, and since
D20/D21 moved to the 1S1555LIM model (2026-11-25) **VR8 0.3370** (was 0.3316): the netlists' defaults
and the machine's calibration. Over the 2SC1623 reverse-mode range that
`models.cir` allows (B_R 1.72 / N_R 1.02 to B_R 4.35 / N_R 0.98) the settings move to 0.589 and
0.330: still inside. The full trim range at CUTOFF 5 is 137 Hz to 7 kHz.

### Specification ranges (§1: HPF 50 Hz-5 kHz, LPF 50 Hz-15 kHz)

With the 7-4 trims, CUTOFF 0 to 10: LPF natural frequency 9.7 Hz to 24.6 kHz (-3 dB at PEAK 0:
3.4 Hz to 9.5 kHz); HPF -3 dB 13.2 Hz to 36.2 kHz; self-oscillation (PEAK 10) 13.1 Hz to 38.6 kHz
(LPF) and 14.3 Hz to 39.1 kHz (HPF). The CUTOFF knob moves pin 1 by 11.5 V (R91/R129 470k into a
7.9-8.3 kohm sum, x 22.3): about 11.5 octaves where the specification claims 6.6 (HPF) and 8.2
(LPF). Every value in that path is legible in two copies; open question 2.

### Korg's test circuit (KORG35.pdf p. 4)

Netlisted from the drawing (the module as in `hpf.cir`; C1 47 nF pin 4 to pin 7, C2 1.5 nF, R1
4.7k / R2 47 ohm at pin 3, pin 1 on R3 1.5k with VR 2k to -15 V, the 2SC644 output stage) with
0.5 Vpp 100 Hz square: Korg's sketch shows a damped ringing of about four visible cycles per
half-period, period T about 1.4 ms (a sketch: no scale for V1). The netlist rings at 3.05 / 2.59
kHz (VR 2k, pin 1 -6.3 V), 1.85 / 1.56 kHz (1.5k, -7.4 V), 0.86 / 0.72 kHz (1k, -8.9 V), the two
numbers after the module's output steps up and down: the same 1.17:1 asymmetry; successive peaks
1, 0.44, 0.22, 0.14 (the sketch looks slower, about 0.7 a cycle). At VR 1k the period (1.2-1.4 ms)
matches the sketch's T. The fixture is not in the repo (it would be a fourth netlist): open item.

### Oversampling

Self-oscillation (PEAK 10, kicked, 10-20 ms) against ngspice at 1.536 MHz (started from the
solver's operating point):

| | ngspice | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| LPF CUTOFF 5 | 499.7 Hz | -0.4 c | -0.1 c | 0.0 c |
| LPF CUTOFF 7 | 2473.8 Hz | -4.3 c | +0.5 c | +0.9 c |
| LPF CUTOFF 8 | 5772.0 Hz | -6.5 c | +9.3 c | +7.9 c |
| LPF CUTOFF 9 | 14505.8 Hz | +17 c | +170 c | +88 c |
| LPF CUTOFF 9.5 | 23617.5 Hz | -526 c | +130 c | +107 c |
| HPF CUTOFF 5 / 7 / 8 | 499.7 / 2471.8 / 5780.9 Hz | -0.4 / -4.5 / -8.5 c | -0.1 / +0.4 / +9.0 c | 0.0 / +0.8 / +7.8 c |
| HPF CUTOFF 9 | 14609.5 Hz | -184 c | +157 c | +92 c |

The solver's own frequency at LPF CUTOFF 9 goes 15266 / 14844 / 14673 / 14589 Hz at 192 / 384 /
768 / 1536 kHz: it converges on ngspice's, but only about first order (the error halves per
doubling), so the self-oscillating top octave stays sharp: about 40 cents at 8x. The small 48 kHz
figures in the middle rows are two errors of opposite sign cancelling, not accuracy. The
oscillation's level (0.35-0.44 Vpp) is the same at every rate.

Small-signal ringing at PEAK 8 (circuitrun at 48 / 96 / 192 kHz against 1.536 MHz): 2.46 kHz
-15 / -4.6 / -1.5 cents; 5.55 kHz -61 / -18 / -5.8 cents; 12.8 kHz -137 / -28 / 0 cents (above 15
kHz the zero-crossing measurement itself is too coarse).

**Since 2026-11-22** the gain stages IC11a / IC8b (gain 58, a 51 kHz loop inside the resonance)
have their op-amp poles by the trapezoidal rule (`trap`): the first-order error above was their
backward-Euler pole, which lagged too little. At 192 kHz against ngspice now (LPF): CUTOFF 7
+1.3 c, 8 -3.7 c, 9 -14.5 c, 9.5 +5.2 c; HPF 9 -15.6 c; converging second order (-3.8 c at 384 kHz
at CUTOFF 9). The other op-amps keep backward Euler (MHz follower loops, which the trapezoidal rule
would ring at Nyquist).

**Recommendation:** run the two KORG35 partitions at 4x (192 kHz from 48 kHz). At 1x the
small-signal resonance is 15 cents flat at 2.5 kHz and over a semitone at 12.8 kHz, and the diode
limiter's harmonics (odd, 20-40 % THD when driven) alias; at 2x the resonance up to 12.8 kHz is
still 18-28 cents flat and self-oscillation above 10 kHz 1.5-2 semitones sharp. At 4x the
resonance is within 6 cents up to 12.8 kHz and self-oscillation within 8 cents up to 6 kHz, but
the self-oscillating top octave (CUTOFF 9-10, 14-40 kHz) is still about a semitone sharp: that
error converges only first order (above), which points at the solver rather than the rate; worth
a look before deciding on 8x. The cutoff summers are linear and fine at 1x; the VCA (a few % of
mostly second harmonic) can share the filters' rate or run at 2x.

**Since 2026-11-23 the KORG35 partitions run at 8x where they need it** (first always; since the CPU
round the same day, per block while its pin-1 CV is above 2.45 V, about 6.5 kHz, back to 4x below
2.15 V, about 5 kHz, at most once in 50 ms: `Machine::pickFilterRates`, `Board::setOver`). At 8x
the block's outputs come from the first of its two steps a sample later, so its delay is 3 samples
at either rate, and a change of rate resamples the decimation's history (`Circuit::retime`): no
click at a switch (onset flux at the switches as at 8x throughout, A/B scenes 30, 46). With BR 4.35 the filters self-oscillate more readily near the
top, and at 4x an oscillation at 15-37 kHz folds its harmonics at 192 kHz into audible lines (a
744 Hz line 36 dB under a 21.4 kHz tone) and, near its onset there, bursts on and off as ticks: at
CUTOFF 10, PEAK 9.5, nothing in, ticks to -41 dBFS over a -85 dBFS floor at 4x, a steady -78 dBFS at
8x; at PEAK 9 -45 -> -70 dBFS. The audible workloads move under 0.02 cents and 0.01 dB.

### VCA

`vca.cir`, Orange/Yellow (R126 680k), gain from check point 16 to 19 (20 mVpp, 500 Hz):

| EG 2 (V) | 0.5 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| Orange/Yellow (dB) | -30.8 | -12.5 | -3.4 | +0.9 | +3.6 | +5.6 |
| Green/Blue (dB) | -32.8 | -15.3 | -6.2 | -2.0 | +0.8 | +2.9 |

Distortion at EG 2 5 V (Orange/Yellow): 2.6 % THD at 0.2 Vpp in, 6.4 % at 0.5, 12.8 % at 1 Vpp,
almost all second harmonic (the forward/reverse asymmetry of the saturated transistor). An EG 2
step 0 to 5 V with no signal moves check point 19 by -59 mV (Green/Blue -31 mV): the VCA's thump.

Levels through the chain (the three netlists joined, 192 kHz, EG 2 5 V, VOLUME 10): a 7.5 Vpp 110
Hz saw at check point 13 (block diagram: 7.5 Vpp VCO, "MAX 10Vp-p" there), both filters open,
PEAK 0: check point 14 0.49 Vpp, 16 0.39 Vpp, 19 0.88 Vpp (Green/Blue 0.64); at 10 Vpp: 0.54, 0.41,
0.95 (0.69); the LPF self-oscillating at CUTOFF 5: 16 0.35 Vpp, 19 0.69 (0.50). The block diagram
says "MAX 2VPP" at 19.

## Open questions

1. R101 (above). One Rev 1 board photo (a 2008 Flickr set, 1024x768, oblique) reads
   yellow-violet-red-gold beside C14/C16 and the HPF's KORG35: 4.7k, likely (2026-11-24,
   `private/ms20/docs/photos/boards/`); the same photo's R116 reads the same. A second, sharper
   photo would make it certain.
2. The CUTOFF range is about 11.5 octaves against the specification's 6.6-8.2.
3. Check point 19 reaches 0.95 Vpp against "MAX 2VPP": the 1S1555's knee at a few microamps (the
   limiter's level), Q19's reverse beta, or the block diagram's figure. A real unit's level at
   check point 16 or SIGNAL OUT would tell which.
4. The KORG35 test fixture as a netlist and case needs a file of its own.
5. The solver's linear-mode tolerance for these partitions (above).
