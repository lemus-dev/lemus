# Envelope generators and modulation generator (eg1.cir, eg2.cir, mg.cir)

KLM-127, circuit diagram (1), bottom row: **EG 1** (DELAY, ATTACK, RELEASE), **EG 2** (HOLD, ATTACK,
DECAY, SUSTAIN, RELEASE) and the **MODULATION GENERATOR** (WAVE FORM, FREQ), with their trigger
inputs (J20, J27) and outputs (J11, J23, J3, J7, and the lines to KLM-128, the VCA and the VCFs).
Target Rev 1 as the service manual draws it (README).

**Sources** (under `private/ms20/docs/`; URLs and SHA1s in `docs/sources.md`):

| Key | What | Where |
|---|---|---|
| **A** | Scan A, circuit diagram (1), 2217 × 1618 | `manuals/service_archive_a/img/a-003.png` |
| **T** | 1994 TIFF, circuit diagram (1), 1-bit, read rotated 90° clockwise (3496 × 2485) | `manuals/service_hyperreal_1994_tif/tif/korg_ms20_4.tif` |
| **B** | Scan B, circuit diagram (1), read rotated 90° clockwise (3000 × 2132) | `manuals/service_archive_b/img/b-003.png` |
| **G** | The later KLM-127 ("NEW PRODUCTION", Rev 2), 3937 × 3081: legibility only, a value counts where A or T agrees | `schematic/hyperreal_late_klm307_klm127/KLM-127.GIF` |
| **SPEC** | Specifications §1 (control section 8-10; patch panel 6-7) | scan A `a-001.png`; OCR `manuals/service_archive_b/Korg_MS-20_Service_Manual_djvu.txt` |
| **BD** | Block diagram §6, check points 22-30 | scan A `a-008.png` |
| **PL** | Parts list §5 | scan A `a-007.png` |
| **OM** | Korg's original owner's manual, p. 6 (PDF page 12): EG 1, EG 2, MG text and figures | `manuals/korg_original_owners_manual/MS20_E.pdf` |
| **MS10** | MS-10 service manual, circuit diagram (PDF page 5): the MS-10's EG, EG 2's circuit with other designators (its R119, R117, VR19 are R169, R168, VR31) | `schematic/ms10/KorgMS10-ServiceManual.pdf` |
| **DS** | MC14007UB, MC14069UB (onsemi), TL081 (TI SLOS081O), µPC4558 (Renesas), via `models.cir` | `datasheets/`; `docs/circuit/semiconductors.md` |

Crop boxes, as fractions of the image (x0 y0 x1 y1): A: EG 1 0.27 0.72 0.60 0.91, EG 2 0.045 0.58
0.36 0.91, MG 0.39 0.56 0.69 0.74. T (rotated): EG 1 0.315 0.70 0.61 0.91, EG 2 0.086 0.57 0.39
0.91, MG 0.51 0.56 0.69 0.74. B (rotated): EG 2 0.097 0.60 0.30 0.84. G: EG 1 0.31 0.76 0.71 0.98,
EG 2 0.08 0.60 0.39 1.0, MG 0.59 0.58 0.86 0.79. A is legible at 2.5×, T at 2×; every value below
was read in A and T (B checked for EG 2's upper half), G used to follow lines first.

Pin numbers: MC14007UB (DS p. 1): pair A gate 6, P drain 13, N drain 8, sources on VDD (14) / VSS
(7); pair B gate 3, P source 2, P drain 1, N source 4, N drain 5; pair C gate 10, P source 11, N
source 9, drains joined at 12. MC14069UB: inverters 1→2, 3→4, 5→6, 9→8, 11→10, 13→12. The drawings
print these numbers at the gates.

**Triggers.** S-trigger polarity: high idle, low while triggered (SPEC: "EG 1 + EG 2 trigger input
(→GND)", "EG 1 trigger input (→GND)"; a cable from another unit's TRIG OUT brings +5.16 V / 0 V).
J27 "EXT TRIG IN" (the panel's EG 1+2 TRIG IN): its tip is check point 22; its normalling contact
(check point 33) is IC13B's output (`kbdtrig`, ±14 V: +14 V idle, −14 V while a key is held)
through **R80 100K** (keyboard.md §1 "Gate delay, IC13B" and §11; the resistor is in these
netlists). J20 "EG 1 TRIG IN": tip check point 26, its normalling contact the line from check
point 22 (A, T: the line from J27's tip runs down and along the bottom of EG 2 to J20). So
unpatched, both EGs hang on J27's tip behind R80; a plug in J27 drives both; a plug in J20 takes
EG 1 off J27's tip. With both inputs on it the tip sits at **+14.0 V** idle and **−0.40 V** while a
key is held (D24 and D31 conduct, R158 and R193 pull up against R80); EG 2 alone would pull it to
−5.1 V (keyboard.md). The keyboard bridges release-and-press gaps under 8.5 ms and never
retriggers in legato (keyboard.md §6): the shortest gap the EGs see from it is 2.35 ms.

## 1. EG 1 (eg1.cir; A, T, G "EG-1")

**Trigger stage.** The input (J20's tip) goes to the cathode of **D24**; **R158 220K** from +15 V
lifts the D24/D23 node; **D23**, **R157 100K** and **R156 33K** (base to ground) drive **Q20**
(2SC1685, emitter grounded), whose collector **R155 2.2K** pulls to +15 V (check point 60, DELAY's
end). Idle (the tip at +14 V, or a cable's +5 V) Q20 conducts and holds its collector low; a
trigger (the tip low) clamps the D24/D23 node at a diode drop, Q20 turns off and its collector rises
to +15 V.

**Delay.** DELAY **VR27 2MA** (a rheostat) from Q20's collector to check point 61; **R194 3.3K** into
**C39 6.8 µF/16 V tantalum**. **D25** (anode on 61, cathode on Q20's collector) empties C39 when the
trigger ends. **R167 1M** from 61 into IC19 pin 6, **R166 10M** back from the output: with IC19's
pairs A and B as two inverters in series, a Schmitt trigger. Its thresholds: with the inverters
switching at V_DD/2 = 7.5 V an ideal pair would flip at 7.5 × 1.1 = 8.25 V rising and (7.5 × 11 −
15) / 10 = 6.75 V falling; the unbuffered inverters' soft middle starts the feedback early, and the
model flips at **8.0 V** and **7.0 V** (a ramp into the Schmitt trigger alone). With DELAY at 0, the trigger's step divides through R155/R194 to 15 × 3.3 / 5.5 = 9.0 V on
node 61 at once: over the threshold, no delay. That is what R194 is for.

**Switch and capacitor.** IC19's pair C: gate 10 from the Schmitt trigger's output (pins 1/5).
Gate high (after the delay): **N-C** (source 9) ties **C38 3.3 µF/16 V tantalum** (pin 12) through
ATTACK **VR28 2MA** to ground. Gate low: **P-C** (source 11) ties C38 through RELEASE **VR29 2MA**
to check point 46, the divider **R159 1K** / **R160 2K** (circled: 1 % metal film; PL "1/4W 1 % 1kΩ
× 3", "2kΩ × 3") from +15 V: 10 V. So C38 idles at **10 V** and the attack takes it to **0 V**.

**Outputs.** **IC18** (TL082; PL "082 × 1"): half a, a follower of C38 (pin 3 in, 1 out; its oval
reads "EFT", sic, in A, T and G); half b inverts it, **R163 20K** in, **R162 15K** feedback, pin 5
grounded: **eg1 = −0.75 × V(C38)**, −7.5 V idle, 0 V at full attack, to KLM-128 (check point 27).
**R164 2.7K** / **R165 2.7K** halve the follower's output for **REVERSE OUT J11** (check point 28):
+5 V idle, 0 V at full attack. BD's sketch at check point 28 runs from "+5V" down to "0" and back,
SPEC "EG 1 envelope signal reverse output (+5V → 0V)": agrees.

## 2. EG 2 (eg2.cir; A, T, B, G "EG-2")

**Trigger stage.** As EG 1's with **D31**, **R193 220K**, **D27**, **R187 100K**, **R186 33K**,
**Q22** (2SC1685) and **R185 10K** (not 2.2K) to +15 V. Q22's collector is IC23 pin 1: high while
triggered.

**HOLD.** IC23 1→2: triggered, pin 2 (check point 34) is low and empties **C42 10 µF/16 V**
(electrolytic; its negative to ground, the symbol drawn sideways above D28's line in A and T)
through **D30** (anode on C42, cathode on pin 2) at once; untriggered, pin 2 is high and fills C42
through HOLD TIME **VR34 2MA** (check point 35 its other end). **R190 1M** into pin 3, IC23 3→4,
**R189 100K**, 5→6, and **R188 10M** from pin 6 back to pin 3: a Schmitt trigger as EG 1's (8.0 /
7.0 V). IC23 9→8 inverts its output. **Pin 8 is the gate** (high while triggered and for the hold
time after); pin 6 is its inverse.

**Retrigger pulse.** **C43 .022** from Q22's collector to **R191 1M** (to ground) and **D28** (anode
there) into pin 13 with **R192 1M** to ground; IC23 13→12; **D29** (cathode on pin 12, anode on pin
5). A new trigger's rising edge on Q22's collector passes C43 and D28, pin 12 goes low, D29 pulls
pin 5 low: **the gate is forced off** while pin 13 stays above the inverter's threshold, C43
discharging into R191 ∥ R192 (11 ms): up to about 7 ms (section 9).

**Attack-peak comparator and latch.** IC21's output (the envelope) through **R181 43K** against
**R176 150K** to −15 V drives **Q21** (2SC1685, emitter grounded); **R184 100K** from +15 V is its
collector load; **R200 47K** to node x; **C41 .0033** from x to the gate (pin 8) and **D26** (anode
on x, cathode on pin 8); **R183 100K** from x into IC23 pin 11; 11→10; **R182 100K** from pin 10
back to Q21's base. **Pin 10 drives IC22's gate C**: low = attack, high = decay. Gate on: pin 8's
rise kicks x high through C41, pin 10 goes low (attack) and R182 holds Q21 off; when the envelope
reaches the trip (about 5.2 V, Q21's V_BE through R181/R176/R182) Q21 pulls x low, pin 10 goes high
(decay) and R182, now at +15 V, latches Q21 on for any envelope above −1 V. Gate off: D26 holds x a
diode above pin 8 (low), pin 10 high. The latch is reset only by the gate.

**Switches** (IC22, MC14007): **P-C** (gate 10 low) from check point 37 / pin 11, ATTACK **VR33 2MA**
from check point 36, the divider **R180 470Ω** / **R179 680Ω** from +15 V (8.87 V, 278 Ω), into
**C40 6.8 µF/16 V tantalum** (pin 12). **N-C** (gate 10 high) from C40 to pin 9, wired to pin 5, the
drain of **N-B** (gate 3 = the gate, pin 8), whose source pin 4 goes through **R178 1K** and DECAY
**VR32 1MA** (check point 38) to check point 39, SUSTAIN's wiper: the decay runs only while the gate
is on. **N-A** (gate 6 = IC23 pin 6, high while the gate is off) from pin 8 to VSS (pin 7, ground),
RELEASE (drawn "VR31 1MA") from C40 to pin 8. The unused P-A and P-B (pins 1, 2, 13) are wired to
+ ("①②⑬ ⊞" in A, T, B): never conducting, left out of the netlist.

**SUSTAIN.** **R169** from +15 V to check point 40, **R168 2.7K** across SUSTAIN **VR31 10KB** (check
points 40-41, 41 grounded), wiper check point 39. With R169 = 4.3k (section 5) the level runs 0 to
**4.93 V**.

**Outputs.** **IC21** (TL081) follows C40: **TO VCA**, check point 23, `eg2`. **IC20** (4558):
half a has **R177 100K** from IC21 into pin 2, **R173 100K** feedback, **R171 100K** from the
SUSTAIN wiper into pin 3 and **R172 100K** to ground (all four circled: metal film, PL "100kΩ × 23"):
pin 1 = **V_sustain − V_envelope**, through **R170 1K** to **REV OUT J23** (check point 25). Half b
inverts it (**R174 10K**, **R175 10K**, pin 5 grounded): **V_envelope − V_sustain**, through **R161
1K** to **TO VCF** (check point 24). OM p. 6 says exactly this: "another EG-2 output is sent to the
Filter Modulation controls (EG-2/EXT) ... the filter's 'steady state' is the Sustain Level. The
filter's cut-off frequency will start below this level initially, will rise above the Sustain level
during the Attack cycle, will fall to the Sustain level, and then will fall below to the initial
level following trigger release." BD's TO VCF and REV OUT sketches put the sustain plateau on the
"0" line.

## 3. MG (mg.cir; A, T, G "MODULATION GENERATOR")

- **IC14** (TL081) integrator: pin 2 the summing node, **C22 .1** (µF) from pin 6 to pin 2, pin 3
  grounded.
- **IC15a** (½ 4558) Schmitt trigger: **R149 20K** from IC14's output into pin 3, **R150 100K** from
  pin 1 back; pin 2 grounded. Thresholds ±(20/100) × its swing: ±2.8 V with the 4558's ±14 V.
- **IC15b** (½ 4558) inverts pin 1 (pin 6 −, pin 5 grounded); pin 7 drives **R152 4.7K** and
  **LED1** to −15 V, lit while IC15a is low (PL: LED GD4-203RD × 4).
- **FREQ**: **R147 2.2K**, **VR25 10KB**, **R148 48Ω** from IC15a's output to ground; the wiper through
  **R154 8.2K** to the two JFETs' common end. So the integrator's input is the Schmitt trigger's
  ±14 V square, divided by FREQ: the frequency is linear in the knob's voltage share.
- **WAVE FORM**: **VR26 1MB** between **Q17** and **Q18**, its wiper into IC14 pin 2. Q17's gate
  through **R151 10M** from IC15b's output, Q18's through **R153 10M** from IC15a's: one JFET is on
  (gate pulled up, clamped by its own junction), the other off (gate at −14 V). IC15a low: Q17 on,
  the current through VR26's Q17 side, the triangle rises; IC15a high: Q18, the other side, it
  falls. The rise and fall times are in the ratio of the two halves of VR26 (plus R154 and the
  channel), the period stays the same.
- Triangle: IC14's output through **R144 1K** to **J3** (check point 30) and "TO TOTAL MODULATION"
  (J4's normal). Rectangle: IC15a's output through **D14** (anode on pin 1) to **R145 4.3K** /
  **R146 2.7K**: **J7** (check point 29) is (14 − 0.6) × 2.7/7.0 = 5.2 V while IC15a is high, 0 V
  (R146) while it is low.

Orientation of VR26 (the drawing does not say which end is clockwise): OM p. 6's figure has WAVE
FORM 0 give a triangle output that jumps up and falls slowly, with the rectangle mostly high;
10 the reverse. The rectangle is high during the fall, so at 0 the rise (Q17's side) has VR26's
zero end: the netlist's rotation 0 puts the wiper at Q17's end.

## 4. Parts

Nets as the netlists name them. "A T" = read in scan A and the TIFF (and G); B where checked.

### EG 1

| Part | Value / type | Nets | Read | Notes |
|---|---|---|---|---|
| J20 | EG 1 TRIG IN | tip `tip20` (26): `SJ20` from input `eg1trig` (control `jack_eg1trig`), `SJ20N` from `tip27` (`!jack_eg1trig`) | A T G | |
| J27, R80 | see EG 2 | `tip27` | | eg1.cir has its own copy, and EG 2's input stage as a load (D31L ... R185L) |
| D24 | 1S1555 | j24 → tip20 (cathode on the jack) | A T G | PL: 1S-1555 × 33, the only small diode |
| R158 | 220K | p15 - j24 | A T G | |
| D23 | 1S1555 | j24 → d23k | A T G | |
| R157 | 100K | d23k - q20b | A T G | |
| R156 | 33K | q20b - 0 | A T G | |
| Q20 | 2SC1685 (drawn "C1685") | C q20c (60), B q20b, E 0 | A T G | PL 2SC-1685S × 13 |
| R155 | 2.2K | p15 - q20c | A T G | |
| VR27 | 2 MΩ A, DELAY, rheostat | q20c - n61 (61) | A T G | `PVR27`, `eg1_delay` |
| D25 | 1S1555 | n61 → q20c | A T G | bar toward Q20's collector |
| R194 | 3.3K | n61 - c39 | A T G | |
| C39 | 6.8 µF 16 V tantalum | c39 - 0 | A T G | PL tantalum 16V-6.8µF × 2 (C39, C40) |
| R167 | 1M | n61 - g6 | A T G | |
| R166 | 10M | sw - g6 | A T G | PL solid 10 MΩ × 7 |
| IC19 | MC14007 (4007) | A: 6 → 8/13; B: 3 → 1/5; C: 10, 11, 9, 12 | A T G | `MIC19AP` ... `MIC19CN`, CD4007N/P |
| VR28 | 2 MΩ A, ATTACK, rheostat | n9 (pin 9) - 0 | A T G | check points 44, 45; `eg1_attack` |
| R159 | 1K, circled | p15 - n46 (46) | A T G | metal film |
| R160 | 2K, circled | n46 - 0 | A T G | metal film |
| VR29 | 2 MΩ A, RELEASE, rheostat | n46 - n11 (pin 11, 47) | A T G | `eg1_release` |
| C38 | 3.3 µF 16 V tantalum | c38 (pin 12) - 0 | A T G | PL 16V-3.3µF × 1 |
| IC18 | TL082 | a: 3 c38, 2 = 1 ic18a; b: 6 ic18m, 5 gnd, 7 eg1 | A T G | "EFT"/"FET" ovals |
| R163 | 20K | ic18a - ic18m | A T G | |
| R162 | 15K | ic18m - eg1 (27) | A T G | |
| R164 | 2.7K | ic18a - eg1rev (28) | A T G | |
| R165 | 2.7K | eg1rev - 0 | A T G | |
| J11 | REVERSE OUT | eg1rev | A T G | output `eg1rev` |

### EG 2

| Part | Value / type | Nets | Read | Notes |
|---|---|---|---|---|
| R80 | 100K | kbdtrig - n80 | (keyboard.md) | IC13B's output to J27's normal contact |
| J27 | EXT TRIG IN (EG 1+2 TRIG) | tip `tip27` (22): `SJ27N` from n80 (33; control `!jack_trig`), `SJ27` from input `trig` (`jack_trig`) | A T B G | eg2.cir also has EG 1's input stage as a load through J20's normal (SJ20NL, D24L ... R155L) |
| D31 | 1S1555 | j31 → tip27 | A T B G | |
| R193 | 220K | p15 - j31 | A T B G | |
| D27 | 1S1555 | j31 → d27k | A T B G | |
| R187 | 100K | d27k - q22b | A T B G | |
| R186 | 33K | q22b - 0 | A T B G | |
| Q22 | 2SC1685 | C q22c, B q22b, E 0 | A T B G | |
| R185 | 10K | p15 - q22c | A T B G | |
| IC23 | MC14069 (4069) | 1→2 n34, 3→4, 5→6, 9→8 gate, 13→12, 11→10 peak | A T B G | CD4069N/P; PL MC-14069B × 2 |
| VR34 | 2 MΩ A, HOLD TIME, rheostat | n34 (34) - n35 (35) | A T B G | `eg2_hold` |
| D30 | 1S1555 | n35 → n34 | A T B G | empties C42 when triggered |
| C42 | 10 µF 16 V electrolytic | n35 - 0 | A T B G | PL 16V-10µF × 12 |
| R190 | 1M | n35 - i3 | A T B G | |
| R189 | 100K | i4 - i5 | A T B G | |
| R188 | 10M | i3 - i6 | A T B G | |
| C43 | .022 µF | q22c - c43b | A T G | type not printed |
| R191 | 1M | c43b - 0 | A T G | |
| D28 | 1S1555 | c43b → i13 | A T G | |
| R192 | 1M | i13 - 0 | A T G | |
| D29 | 1S1555 | i5 → i12 | A T G | bar on pin 12 |
| R181 | 43K | env - q21b | A T B G | |
| R176 | 150K | q21b - n15 | A T B G | |
| Q21 | 2SC1685 | C q21c, B q21b, E 0 | A T B G | |
| R184 | 100K | p15 - q21c | A T B G | |
| R200 | 47K | q21c - x | A T B G | |
| C41 | .0033 µF | x - gate | A T B G | |
| D26 | 1S1555 | x → gate | A T B G | |
| R183 | 100K | x - i11 | A T B G | |
| R182 | 100K | peak (pin 10) - q21b | A T B G | |
| R180 | 470Ω | p15 - n36 (36) | A T B G | |
| R179 | 680Ω | n36 - 0 | A T B G | |
| VR33 | 2 MΩ A, ATTACK, rheostat | n36 - n11 (37, pin 11) | A T B G | `eg2_attack` |
| IC22 | MC14007 (4007) | P-C 12/10/11, N-C 12/10/9, N-B 5/3/4 (pin 5 = pin 9), N-A 8/6/7 | A T B G | `MIC22CP`, `MIC22CN`, `MIC22BN`, `MIC22AN` |
| R178 | 1K | n4 - n38 (38) | A T B G | |
| VR32 | 1 MΩ A, DECAY, rheostat | n38 - sus (39) | A T B G | `eg2_decay` |
| R169 | drawn 43K; 4.3k in the netlist | p15 - n40 (40) | A T B G | section 5; `.param r169` |
| R168 | 2.7K | n40 - 0 (across VR31) | A T B G | |
| VR31 | 10 kΩ B, SUSTAIN | n40 - sus (39) - 0 (41) | A T B G | `PVR31`, `eg2_sustain` |
| "VR31" | 1 MΩ A, RELEASE, rheostat | c40 - n8 (pin 8; 42, 43) | A T B G | drawn with the same number: `PVR31R`, `eg2_release` |
| C40 | 6.8 µF 16 V tantalum | c40 (pin 12) - 0 | A T B G | |
| IC21 | TL081 | 3 c40, 2 = 6 env | A T B G | TO VCA, check point 23 |
| R177 | 100K, circled | env - i20am | A T B G | |
| R173 | 100K, circled | i20am - ic20a | A T B G | |
| R171 | 100K, circled | sus - i20ap | A T B G | |
| R172 | 100K, circled | i20ap - 0 | A T B G | |
| IC20 | ½ 4558 × 2 | a: 3 i20ap, 2 i20am, 1 ic20a; b: 6 i20bm, 5 gnd, 7 ic20b | A T B G | |
| R170 | 1K | ic20a - eg2rev (25) | A T G | |
| J23 | REV OUT | eg2rev | A T G | output `eg2rev` |
| R174 | 10K | ic20a - i20bm | A T G | |
| R175 | 10K | i20bm - ic20b | A T G | |
| R161 | 1K | ic20b - eg2vcf (24) | A T G | TO VCF, output `eg2vcf`. `eg2.cir` takes 33k (2026-11-25, decided 2026-11-27): one unit's EG 2 sweeps want TO VCF at about 0.6 of the drawn level, 33k into VR17 and VR24's 50k; a board photo of R161 would settle it |

### MG

| Part | Value / type | Nets | Read | Notes |
|---|---|---|---|---|
| IC14 | TL081 ("FET") | 3 gnd, 2 i14m, 6 tri | A T G | |
| C22 | .1 µF | i14m - tri | A T G | type not printed |
| R149 | 20K | tri - i15p | A T G | |
| R150 | 100K | sq - i15p | A T G | |
| IC15 | ½ 4558 × 2 | a: 3 i15p, 2 gnd, 1 sq; b: 6 sq, 5 gnd, 7 sqn | A T G | |
| R152 | 4.7K | sqn - led1a | A T G | |
| LED1 | LED (GD4-203RD, PL) | led1a → n15 (−15 V) | A T G | provisional model in mg.cir |
| R147 | 2.2K | sq - n147 | A T G | |
| VR25 | 10 kΩ B, FREQ | n147 - w25 - n148 | A T G | `mg_freq` |
| R148 | 48Ω | n148 - 0 | A T G | as printed (section 5) |
| R154 | 8.2K | w25 - s1718 | A T G | |
| Q17 | JFET, type not printed | s1718 - v26a, gate q17g | A T G | 2SK30A-O (section 5) |
| R151 | 10M | sqn - q17g | A T G | |
| Q18 | JFET | s1718 - v26b, gate q18g | A T G | |
| R153 | 10M | sq - q18g | A T G | |
| VR26 | 1 MΩ B, WAVE FORM | v26b - i14m (wiper) - v26a | A T G | `mg_wave` |
| R144 | 1K | tri - mg (30) | A T G | J3 and TO TOTAL MODULATION |
| D14 | 1S1555 | sq → d14k | A T G | bar toward R145 |
| R145 | 4.3K | d14k - mgsq (29) | A T G | |
| R146 | 2.7K | mgsq - 0 | A T G | J7 |

## 5. Where the drawing was hard, and how it was settled

- **R169 "43K".** A, T, B and G all print 43K. With R168 2.7K across VR31 10K that puts 15 ×
  2.126 / (43 + 2.126) = **0.71 V** on top of SUSTAIN: full SUSTAIN would hold the envelope at 13 %
  of its 5.2-5.4 V peak. SPEC says "Sustain level (0~5V)", OM's EG 2 figure sustains at about half
  the peak, and the MS-10's EG (MS10), the same circuit, has **R119 4.3K** from + with **R117 2.7K**
  across **VR19 10KB SUS**: 15 × 2.126 / (4.3 + 2.126) = **4.96 V**. A dropped decimal point; the
  netlist takes 4.3k (`.param r169=4.3k`; `r169=43k` is the print: sustain 0-0.70 V).
- **Two VR31s.** SUSTAIN is "VR31 10KB" and RELEASE "VR31 1MA" in every copy. PL has 1MA × 2
  (DECAY VR32 and this one) and 10KB × 4. Near the EGs and MG the numbers run VR25 (FREQ), VR26
  (WAVE FORM), VR27-VR29 (EG 1), VR31-VR34 (EG 2): VR30 is the free one and probably RELEASE's
  real number. The netlist names it `PVR31R`.
- **Labels with misprints**, the same in all copies: "RELFASE" for RELEASE; "EFT" in IC18a's FET
  oval. A boxed "PW" sits beside Q18 / IC14 pin 3 in the MG with no wire: unexplained (the VCO's
  PW pot is VR9).
- **R148 "48Ω"**, all copies: not an E12 value (47 Ω would be). Kept; it sets only FREQ's bottom,
  which is proportional to it (47 Ω would lower the 0.093 Hz by 2 %).
- **Q17, Q18's type** is not printed. PL has 2SK-30(O) × 4 and 2SK-30(GR) × 4; the drawing marks Q1,
  Q12, Q23 and KLM-128's S&H JFET GR and Q5, Q8 O, leaving two O for Q17/Q18 (if KLM-128/129 hold
  no other JFET): `2SK30A_O`.
- **IC23's first Schmitt inverter** reads "9" in G but "3" in A, T and B: pin 3 (the 4069 has 3→4).
- **Diode directions**, each the only one in which the circuit works: D24, D31 cathodes on the
  jacks; D23, D27 anodes on the R158/R193 nodes; D25 cathode on Q20's collector (it empties C39);
  D30 cathode on IC23 pin 2 (HOLD fills C42 slowly and empties it at once, not the reverse, which
  would delay the gate instead of holding it); D28 anode on C43; D29 cathode on pin 12 (the other
  way it would hold the gate on for good); D26 cathode on pin 8; D14 anode on IC15a (J7 0/+5 V,
  as SPEC and BD).
- **Crossings without dots**: IC22's gate-C line (from IC23 pin 10) crosses IC21's output line;
  the SUSTAIN wiper's line to R171 crosses the gate-A and pin-8 lines; R144's line crosses IC15a's
  output line (the dots on that line are R153's and D14's).
- **C42's negative end** ends in a ground symbol drawn just above D28's line (A, T): ground, not
  D28's cathode.
- **Rev 2 (G) against Rev 1 (A, T, B)**: no difference in any of these three blocks.

## 6. The 4007s and 4069: MOSFETs, not switches

The netlists build every CMOS device from `models.cir`'s level-1 **CD4007N/P** (IC19, IC22) and
**CD4069N/P** (IC23), one N and one P per inverter, a single device per switch. Why not `S`
elements with the data sheet's on-resistance:

1. An `S` element is a panel switch: its control is an external position, not a node. Every 4007
   here is driven by a node (EG 1's Schmitt trigger, EG 2's latch and gate), and EG 1's Schmitt
   trigger and all of IC23 are themselves CMOS inverters, whose thresholds (V_DD/2) set the delay,
   hold and retrigger times.
2. The on-resistance depends on V_GS, which differs per switch: EG 1's N-C has its source on
   ground (V_GS 15 V: **144 Ω**, semiconductors.md §8); its P-C sits on the 10 V divider (|V_GS|
   10 V: **224 Ω**); EG 2's P-C on the 8.87 V divider (|V_GS| 8.9 V), saturating at the start of an
   attack; N-C and N-B in series with their sources near the envelope. One model gives each.
3. **The on-resistance sets the shortest times.** EG 1 ATTACK 0: C38 through N-C alone, τ = 3.3 µF
   × 144 Ω = 0.48 ms (measured 0.58 ms). EG 2 ATTACK 0: C40 from the 278 Ω divider through P-C,
   partly saturated: 5.0 ms to the peak. RELEASE 0: N-A alone, τ = 6.8 µF × 144 Ω = 1.0 ms
   (measured 1.08 ms). With the data sheet's *minimum* drive (I_OL 3.4 mA at 15 V / 1.5 V against
   10 mA typical, run as a scratch model with W × 0.34) these become 1.72 ms, 9.9 ms and 3.2 ms; the
   settings from 5 up move about 1 % or less. A real unit's fastest EG 2 attack lies somewhere in that
   range: the one place where the CMOS's spread is audible.

Not modelled: the body effect (the solver's MOSFET has its bulk on the source; P-C's source sits
5-6 V below its +15 V bulk, which raises |V_T| and the on-resistance), the protection diodes, the
gate capacitance except the stand-ins below.

## 7. The netlists

**Interface.** Inputs (volts, stiff): `kbdtrig` (IC13B's output, trig.cir), `trig` (a cable in
J27), `eg1trig` (a cable in J20, eg1.cir only); `mgkick` (below). Jack controls `jack_trig` and
`jack_eg1trig` (1 = a cable in the jack): `S` elements for each jack's tip contact (closed on
`jack_<name>`) and normalling contact (closed on `!jack_<name>`), R80 between `kbdtrig` and J27's
normal. Outputs `eg1`, `eg1rev`; `eg2` (TO VCA), `eg2vcf` (TO VCF), `eg2rev`; `mg`, `mgsq`; probes
`tip20`, `tip27`. `eg2vcf` is a name the README's table doesn't have yet (the filters' EXT CUTOFF
normals take it, not `eg2`). Controls by PluginProcessor's ids: `eg1_delay`, `eg1_attack`,
`eg1_release`, `eg2_hold`, `eg2_attack`, `eg2_decay`, `eg2_sustain`, `eg2_release`, `mg_freq`,
`mg_wave`. Supplies +15 V (and −15 V for R176 and LED1).

**The shared tip.** Unpatched, J27's tip carries both EGs' inputs behind R80, a passive node the
README would not split. Each netlist has its own copy of R80 and the jacks and the other EG's input
stage as a load (eg1.cir: D31L, R193L, D27L, R187L, R186L, Q22L, R185L; eg2.cir: D24L ... R155L,
through J20's normal contact so that it leaves the tip when J20 is patched). Both then give the tip
the same voltage (−0.403 V held in each, +14.0 V idle); without the load it would sit at −5.1 V held.
It only changes the tip's level while held: both EGs are triggered either way, and the edges pass in
the 28 µs of IC13B's slew.

**Pots.** The time pots are "A" (PL: 2MA × 6, 1MA × 2) wired as rheostats, 0 Ω at 0, the README's
placeholder law `a0.15`; SUSTAIN, FREQ and WAVE FORM are "B" (linear). No end resistance.

**Op-amps** use `models.cir`'s `.param`s (TL081/082: `gbwtl gaintl vsattl`; 4558: `gbw4558
gain4558 vsat4558`). **EG 1 and EG 2 leave out the slew rate**: with the slewing stage the solver's
Newton stalls on a residual floor (1.27e-5 against its 1e-5 tolerance, at the op-amps' ports) at
some knob extremes, freezing the state: in a 0.25 s note, SUSTAIN, DECAY, RELEASE all 0 lost 2305
of 12000 samples, SUSTAIN and DECAY at 10 about 9000; without it, none. The steepest envelope slope
(EG 1 ATTACK 0, about 17 V/ms on C38) is far under the 4558's 1 V/µs. The MG keeps it (`sr`): its Schmitt trigger
slews 28 V (28 µs), part of each turn of the triangle.

**Stand-ins** (not on the drawing; each in the netlist's comments):

| Element | Why | Value |
|---|---|---|
| CG6 (eg1), CI3, CI5 (eg2): `cg` | The Schmitt triggers are bistables with no charge in their loops; without it Newton has no solution past the switching point. The gate's own capacitance is 5 pF typ, 7.5 pF max (DS), plus the board's. (With the first, provisional models and the op-amps' slew stage, 5 pF failed a sample at a fast flip and 22 pF stalled where node 61 creeps through the lower threshold; with models.cir's fits and no slew stage 10 pF passes everything below, the creeping cases included) | 10 pF: delays a flip by up to 10 pF × 0.91 MΩ = 9 µs |
| CI11 (eg2): `cl` | The peak latch, the same problem through Q21. A larger value (100 pF was tried) stretches the moment at the gate's start when N-C and N-B both conduct and the decay path pulls the SUSTAIN node toward C40: a 10 µs spike on TO VCF and REV OUT that a sample can land in | 10 pF: 1 µs with R183 |
| D31L ... R185L (eg1), SJ20NL, D24L ... R155L (eg2) | the other EG's input stage on the shared J27 tip (above) | the same parts and values |
| IKICK (mg): input `mgkick` | The MG's DC point is its unstable balance (all at 0 V). From it the solver starts by rounding after a few ms of sample-rate chatter, ngspice at another time. The cases put 10 µA for 1 ms in (−0.1 V on the triangle) | 0 in the machine, or a kick at load |
| `.model GD4-203RD` (mg) | models.cir has no LED and there is no GD4-203RD sheet: about 1.8 V at 5 mA. It only loads IC15b | IS 1e-20, N 1.8, RS 5 |

A cap on pin 13 (the retrigger inverter's input, outside any loop) made things worse (every
sample failed at 48 kHz), so only the inputs inside the loops have one.

**Robustness.** No Newton failures in a sweep of every combination of HOLD {0, 0.3}, ATTACK, DECAY,
RELEASE {0, 0.3, 1}, SUSTAIN {0, 0.5, 1} (EG 2, 162 runs) and DELAY {0, 0.2, 0.5, 1}, ATTACK,
RELEASE {0, 0.3, 1} (EG 1, 36 runs), each a 0.6 s pattern of `kbdtrig` notes with 15 ms and 2.5 ms
gaps, at 44.1, 48 and 96 kHz; nor in the slow crossings (DELAY 5 with a 7.3 s note, from the
keyboard and from a +5 V cable on J20; HOLD 5) at the three rates.

## 8. Checks against the specification

All at 48 kHz with `models.cir`, triggered from `kbdtrig` (±14 V, 28 µs edges) with the jacks
unpatched; knob positions as the panel's 0-10 (rotation × 10). Times from `kbdtrig`'s edge or its
end. Scratch scripts (not in the repo) measured them from
`circuitrun` runs.

(The "5" columns below are for the A pots' old law, 15 % of the track at mid. Since 2026-11-23 they
follow rotation^2.25, 21 % at mid, so the times at 5 read about 1.4x these; the ends are the same.)

**EG 1** (SPEC: delay, attack, release 10 s each):

| | 0 | 5 | 10 |
|---|---|---|---|
| DELAY: trigger to the attack's start | 0.03 ms | 1.47 s | **10.74 s** |
| ATTACK: τ / 10-90 % | 0.58 ms / 1.17 ms | 0.99 s / 2.18 s | **6.60 s / 14.5 s** |
| RELEASE: τ / 10-90 % (starts 0.03 ms after the trigger ends) | 3.9 ms / 7.5 ms | 1.01 s / 2.18 s | **6.66 s / 14.5 s** |

τ = RC: 2 MΩ × 3.3 µF = 6.6 s at 10. DELAY 10: C39 charges through VR27 from +15 V while R167 + R166
(11 MΩ, to the Schmitt trigger's low output) pull node 61 down: 12.7 V through 1.69 MΩ, so (1.69 MΩ ×
6.8 µF) × ln((12.7 − 0.81)/(12.7 − 8.0)) = 10.7 s from C39's idle 0.81 V to the 8.0 V threshold.
C39 idles where Q20's collector sits, and Q20 is short of base drive: with the tip at +14 V its
collector rests at **0.81 V**; with a +5 V cable on J20 (42 µA through R157, of which R156 takes
20 µA: 22 µA of base current, h_FE 277 at 6 mA with models.cir's 2SC1685, where R155 asks 6.8 mA)
at **1.75 V**, and DELAY 10 is then 9.95 s. With a Q20 that saturates (a scratch model with BF 1000)
C39 idles at 0.13 V and DELAY 10 is 11.3 s (+5 V cable). The same transistor sets EG 1's threshold
for a cable on J20: the trigger counts as on below **3.7 V** and off above **4.1 V** (the Schmitt
trigger's hysteresis through Q20); EG 2's for a cable on J27 (Q22, R185 10K) is **2.7 V**. A
+5.16 V TRIG OUT clears both.

**EG 2** (SPEC: hold 20 s, attack, decay, release 10 s, sustain 0-5 V):

| | 0 | 5 | 10 |
|---|---|---|---|
| gate on after the trigger (from rest) | 7.1 ms | 7.1 ms | 7.1 ms |
| ATTACK: gate to the peak / 10-90 % (peak) | 5.0 ms / 3.9 ms (5.37 V) | 1.83 s / 1.42 s (5.18 V) | **12.1 s / 9.4 s** (5.18 V) |
| DECAY (to SUSTAIN 0): τ | 9.3 ms | 1.03 s | **6.78 s** |
| RELEASE (from SUSTAIN 10): τ | 1.08 ms | 1.02 s | **6.80 s** |
| HOLD: trigger's end to the gate off | 1.89 ms | 2.25 s | **16.3 s** |

SUSTAIN (TO VCA while held; linear): 0, 1.23, 2.45, 3.67, **4.93 V** at 0, 2.5, 5, 7.5, 10 (SPEC
0-5 V; with R169 as printed, 0-0.70 V). The peak is the latch's trip: 5.18 V on a slow attack, 5.37 V
at ATTACK 0 (the envelope's overshoot in the flip). DECAY 0 is R178 1k plus N-C and N-B: τ 9 ms, not
the release's 1 ms. HOLD 10: as DELAY's sum, C42 through 2 MΩ from pin 2 against R190 + R188 (11 MΩ to pin 6, low):
16.9 s × ln((12.7 − 0.35)/(12.7 − 8.0)) = 16.4 s (SPEC 20 s). The attack at 10 is
2 MΩ × 6.8 µF × ln(8.87/(8.87 − 5.2)) = 12.0 s.

**MG** (SPEC: frequency 0.1-20 Hz and WAVE FORM "1:1 ~ 1:80". The page prints "Frequency (1:1 ~
1:80)" and "(0.1Hz ~ 20Hz)" under the next line, the control wheel: read as the waveform's ratio and
the frequency's range):

| FREQ | 0 | 2.5 | 5 | 7.5 | 10 |
|---|---|---|---|---|---|
| f (WAVE FORM 5) | **0.093 Hz** | 5.09 Hz | 10.06 Hz | 15.03 Hz | **20.04 Hz** |

| WAVE FORM (FREQ 10) | 0 | 5 | 10 |
|---|---|---|---|
| rise / fall | 0.54 / 49.3 ms (**1:91**), 20.05 Hz | 25.0 / 24.9 ms (1:1) | 50.8 / 0.53 ms (**1:95**), 19.48 Hz |

f ≈ 0.09 + 2.0 × FREQ Hz with VR25 straight from its stop: linear, FREQ being a divider on the square.

**VR25's law, from recordings (2026-11-24).** One vintage unit (atoooy, three videos), MG rate
against FREQUENCY read at 13 settings: 1.39 → 1.06 Hz, 2.5 → 3.73, 2.67 → 4.21, 2.84 → 4.71, 4.15 →
7.05, 4.42 → 7.87, 4.7 → 8.46, 5.0 → 8.74, 5.8 → 10.00, 6.72 → 11.90, 7.21 → 12.77, 7.85 → 14.77,
10 → 19.57 Hz. The top agrees with the drawing (19.48 Hz at WAVE FORM 10); below it the real one is
slower, 0.38 of the drawing's rate at 1.39. A track that starts 0.91 of a knob unit (27°) from the
stop and then runs straight fits them within 6 % RMS (11 % at most); straight from the stop, 31 %;
a power law, 15 %. A second unit (g6OfcNTvJow, 4.0 → 7.21 Hz) falls within 5 %. mg.cir's VR25 is
`j0.091/0.5/0.45` since then: 0.092 Hz at FREQ 0 (SPEC: 0.1 Hz), 3.59 Hz at 2.5, 8.41 at 4.7,
15.30 at 7.85 (WAVE FORM 5). Whether it is the pot's end termination or the knob's position on its
shaft, one unit can't say; the other pots keep their laws (CUTOFF's slope against the knob readings
argues against the same dead zone there). At FREQ 0 WAVE FORM 5 the two
halves differ, 6.5 s rising and 4.3 s falling: the conducting JFET's gate, forward-biased through
its 10M, injects about 1.3 µA into R154's end, against an integrator current of 0.1 µA there. At
WAVE FORM 0, FREQ 0: 0.116 Hz. Triangle **±2.80 V (5.6 Vpp)** at J3 (SPEC and BD 5 Vpp: R149/R150
of the 4558's ±14 V typical swing; ±12.5 V would give 5.0); rectangle **0 / 5.16 V** at J7 (SPEC,
BD "+5V 0V").

**Check-point levels** (BD, the outputs as other blocks take them):

| Point | Signal | Model | BD / SPEC |
|---|---|---|---|
| 22, 26 | J27's and J20's tips (unpatched) | +14.0 V idle, −0.40 V held (`kbdtrig` ±14 V through R80, both inputs on it) | SPEC "→GND" |
| 23 | TO VCA, `eg2` (IC21 output) | 0 → 5.2-5.4 V → SUSTAIN (0-4.93 V) → 0 | sketch only |
| 24 | TO VCF, `eg2vcf` (R161 1k from IC20b) | −V_sus idle, peak − V_sus, 0 while sustaining | sketch: plateau on "0" |
| 25 | REV OUT J23, `eg2rev` (R170 1k from IC20a) | the negative of 24 | SPEC "(⌐¬ 0V)" |
| 27 | EG 1 to KLM-128, `eg1` (IC18b output) | **−7.5 V** idle → 0 V | SPEC "EG 1 normal output (−5V → 0V)" |
| 28 | REVERSE OUT J11, `eg1rev` (1.35 kΩ source) | +5.0 V idle → 0 V | "+5V / 0" |
| 29 | MG rectangle J7, `mgsq` (1.66 kΩ high, 2.7 kΩ low) | 0 / +5.16 V | "+5V 0V" |
| 30 | MG triangle J3, `mg` (R144 1k from IC14) | 5.6 Vpp about 0 V (5.0 with IC15a at ±12.5 V, `vsat15a`, since 2026-11-24) | "5VPP" |

While decaying, the decay current flows into the SUSTAIN divider (about 2 kΩ at its wiper) and lifts
its node: at SUSTAIN 5, DECAY 1 (rotation 0.1), 2.69 V at the peak instead of 2.45, so TO VCF
peaks at 2.69 V rather than 5.38 − 2.45.

## 9. Retriggering

**EG 1** has no retrigger circuit: the delay timer and the switch follow the trigger.
- A held trigger (legato with no gap) does nothing new.
- Trigger off: the release starts within 0.1 ms (Q20 pulls node 61 below 7.0 V through D25), and
  C39 empties through R194 and D25 (τ 22 ms) toward Q20's collector.
- A new trigger restarts the **delay from C39's voltage**, so a short gap shortens it: with DELAY
  at 1.5 (438 ms from rest): 0.04 ms after a 2 ms gap, 54 ms after 10 ms, 312 ms after 50 ms,
  424 ms after 300 ms. The **attack resumes from C38's level**, never from the top.
- From the keyboard: legato never moves `kbdtrig` (keyboard.md §6), so EG 1 runs on; a detached
  note gives a gap of 2.35 ms or more.

**EG 2** retriggers on each new trigger edge:
- Each rising edge on Q22's collector (a trigger after a gap) sends the C43 pulse: the **gate goes
  off** (release switch on, latch reset to decay) and comes back on when the pulse ends; the gate's
  return kicks the latch to **attack, from wherever the release left C40**. With RELEASE short the
  envelope falls nearly to 0 in that time (RELEASE 0: 4.8 V to 0.005 V): a restart from zero.
- The gate-off time grows with the gap, as C43 recharges through R191 while the trigger is low:
  after a 2 ms gap the gate is back 0.46 ms after the new trigger, 2.35 ms (the keyboard's shortest)
  → 0.71 ms, 5 ms → 1.65 ms, 50 ms → 5.9 ms, and from rest **7.1 ms**: every note's EG 2 (and so
  the VCA) starts 7 ms after its trigger. Under about 1.9 ms of gap (1 ms tested) the gate never
  drops (HOLD 0 still holds it 1.9 ms) and C43's pulse is too small: no retrigger.
- From the keyboard: legato never retriggers (`kbdtrig` doesn't move); every detached note does
  (a release-and-press gap of 8.5 ms or more reaches `kbdtrig` as 2.35 ms or more; shorter ones
  are bridged there).
- During HOLD (trigger released, gate still on) a new trigger restarts the attack the same way
  (HOLD 2, RELEASE 0: the gate drops at the new trigger, C40 falls from 4.8 V to 0.005 V, the gate
  is back 7.1 ms later).
- A held trigger with no gap: the envelope stays in decay/sustain.

## 10. Against ngspice

`tools/spice/cases/envelopes.json`, `compare_circuit.py` with ngspice 47,
largest difference in % of each output's range (RMS in brackets):

| Case | Outputs | Logic probes (sw, gate, peak, sq) |
|---|---|---|
| eg1_fast (96 kHz, all 0) | eg1, eg1rev, c38 0.46 % (1.4 mV); c39 0.003 %; n61 0.18 %; tip20 0.21 % | sw one sample at its edges |
| eg1_mid (DELAY, ATTACK, RELEASE 1) | 0.003 % (0.05 mV); n61, c39 ≤ 0.015 % | " |
| eg1_retrigger | 0.007 % (0.1 mV); n61, c39 ≤ 0.019 % | " |
| eg1_jack (J20 patched, +5.16 / 0 V cable) | 0.10 % (0.4 mV) | " |
| eg2_adsr | eg2, c40 0.19 % (0.6 mV); eg2vcf, eg2rev 0.38 % (0.6 mV); c42 0.25 % | one sample |
| eg2_slow | eg2 0.003 % (0.06 mV); eg2vcf 0.75 % (0.2 mV) | " |
| eg2_hold_retrigger | eg2 0.005 % (0.06 mV); eg2vcf 1.9 % (0.4 mV) | " |
| eg2_organ (SUSTAIN 10, the rest 0, retriggers) | eg2 0.68 % (2.2 mV); eg2vcf 2.6 % (2.4 mV) | " |
| eg2_jack (J27 patched, +5.16 / 0 V cable) | eg2 0.005 % (0.1 mV); eg2vcf 2.6 % (0.8 mV) | " |
| mg_triangle_fast (FREQ 10) | mg 0.19 % (6.7 mV); half-periods 24.984 / 24.918 ms against 24.983 / 24.917 | mgsq, sq one sample at edges |
| mg_slow (FREQ 0.5, WAVE FORM 8.5) | mg 0.017 % (0.2 mV); half-periods equal to 1 µs | " |
| mg_ramp (FREQ 6, WAVE FORM 0) | half-periods 82.057 / 0.979 ms against 81.971 / 0.979 (0.1 %): over 0.5 s the steep edge drifts 0.5 ms apart, so the sample-wise difference reads 46 % | " |

The tips (tip20, tip27) differ by 0.21-0.25 % for a sample on `kbdtrig`'s 28 µs edges.

The envelopes agree to 0.7 % or better. TO VCF and REV OUT take the SUSTAIN node, which steps when
the decay starts (its current lands on the divider): where that step falls within a sample decides
one sample's 1-3 %, with RMS under 3 mV. The logic nodes switch within a sample; their difference is
the edge's place in it. The MG's ramp setting runs 0.1 % slower than ngspice's: the long half,
through VR26's whole 1 MΩ, carries the forward-biased JFET's gate current, which the two programs
split slightly differently between the channel's ends.

## 11. Open questions

- **R169** 4.3k against the print's 43K: one look at a board (the resistor's colour bands) or an
  absolute sustain level on a real unit settles it. Board photos (2026-11-24): fitted on a KLM-127F,
  bands unresolvable; hidden behind VR31's lugs on a Rev 1. One unit's plateaus against its peak
  (BwBwJosbQkg, SUSTAIN 5 / 7.5 / 9: -10.0 / -5.9 / -3.4 dB) sit 1-2 dB under ours (-8.2 / -4.2 /
  -2.5); R169 5.6k (-10.2 / -6.0 / -4.2) or a higher peak would explain it, and the specification's
  "0~5V" favours 4.3k (4.96 V; 5.6k: 4.13 V).
- **EG 1's output level**: −7.5 V idle as drawn (R162/R163, 15k/20k on a 10 V swing) against SPEC's
  "−5V → 0V". KLM-128 decides what the MVCA and the VCO's EG 1/EXT do with it (its input stage must
  invert; the MVCA wants 0 to +5 V); a check there may show which is right.
- **EG 2's 7 ms start**: the C43/R191/R192 pulse holds the gate off for 7 ms after every trigger
  from rest, as drawn. A recording with a sharp attack (key click to VCA onset) would confirm it.
- **The fastest times** hang on the 4007's drive (section 6): EG 2 ATTACK 0 between 5 and 10 ms to
  the peak across the data sheet's spread.
- **Q20's saturation** and so C39's idle voltage (0.81 V from the keyboard, 1.75 V from a +5 V
  cable), DELAY 10 (10.7 s from the keyboard; with a +5 V cable 9.95 s, or 11.3 s if Q20 saturated)
  and EG 1's 3.7 V cable threshold hang on the 2SC1685's h_FE, for which no maker's sheet was
  found.
- **The pots' A law** (`a0.15`) sets every time at mid rotation (the "5" columns above).
- **HOLD 10** gives 16.3 s against SPEC's 20 s; ATTACK 10 12.1 s and DECAY/RELEASE τ 6.8 s against
  "10 sec". SPEC doesn't say how it measures a time (to 63 %, 90 %, the end), and the parts'
  tolerances aren't on the drawing or in PL; a timed real unit would say whether anything is off.
- **VR30**: RELEASE's real designator (section 5).
