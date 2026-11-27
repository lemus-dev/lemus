# Keyboard, portamento, key hold, trigger detector and power supply (kbd.cir, trig.cir)

The left side of circuit diagram (1), KLM-127: the 37-key ladder ("KEY BOARD"), the KBD FOLLOWER
(IC4A, PORTAMENTO, the key-hold JFET Q23, IC5) to KBD CV OUT (J33) and the normal of VCO 1+2 CV IN
(J26); the TRIG DETECTOR (IC13, Q13-Q16) to TRIG OUT (J32), the EG trigger normal and LED2; and the
POWER SUPPLY, as notes. The VCOs that take the CV are [vco.md](vco.md); the envelopes that take the
trigger are [envelopes.md](envelopes.md).

**Sources** (under `private/ms20/docs/`, not in the repo; `docs/sources.md` has the downloads):

| Key | What | File (SHA1) |
|---|---|---|
| **A** | Scan A, circuit diagram (1), upright, 2217 x 1618 | `manuals/service_archive_a/img/a-003.png` (da701a73179cd2f656494f7499963727bdee430d) |
| **T** | 1994 TIFF of the same page, 2485 x 3496, 1-bit, read turned 90 degrees clockwise (3496 x 2485) | `manuals/service_hyperreal_1994_tif/tif/korg_ms20_4.tif` (1a84e314d180a86443aafe4199028d8f60480760) |
| **B** | Scan B of the same page, 2132 x 3000, read turned the same way (3000 x 2132) | `manuals/service_archive_b/img/b-003.png` (a99b3f3ddb4d7134c2dcece146486cd34f88e78f) |
| **G** | The later KLM-127 ("NEW PRODUCTION", Rev 2), 3937 x 3081: legibility only | `schematic/hyperreal_late_klm307_klm127/KLM-127.GIF` (ae5e165a1fd9a7f90fbdedb991ffb99770d99651) |
| **PCB** | Service manual 4, front view of KLM-127 (sections KBD FOLLOWER, TRIG DETECTOR, POWER SUPPLY) | `manuals/service_archive_a/img/a-005.png` (d26eddf8a72efe0118acc0096e1aba5b2b04fbb1) |
| **PL** | Service manual 5, parts list | `manuals/service_archive_a/img/a-007.png` (ed8d4ef1ecf2ba272ee2521186e96af84552975a) |
| **BD** | Service manual 6, block diagram with check points | `manuals/service_archive_a/img/a-008.png` (49846e3d6c445d7e6cfb5e57ceca820ae1a1712d) |
| **ADJ** | Service manual 7, adjustment procedure, 7-3 and its Fig. 1 | `manuals/service_archive_a/img/a-009.png` (2b1a5e6a09062c5e232ec614a6e7586f97a8b6b5) |
| **SPEC** | Service manual 1, specifications | `manuals/service_archive_a/img/a-001.png` (07e91b5813113448c6407ad49e557c1632cae3f7) |
| **MINI** | MS-20 mini owner's manual (Korg), text: "When two or more keys are pressed, the highest tone played will be produced." | `manuals/ms20mini_owners_manual/mini.txt` |
| **DS** | Data sheets: 2SK30A (Toshiba 2SK30ATM), 2SA564A (Blue Rocket), CA3140 (Renesas FN957), uPC4558 (Renesas G10518), NJM4558 | `datasheets/2sk30a/`, `2sa564/`, `ca3140/`, `upc4558/` |

Crop boxes are fractions of the upright image (x0 y0 x1 y1); T and B share them. A: ladder
0.081 0.272 0.235 0.396, PORTAMENTO and IC5 0.217 0.260 0.374 0.433, trigger detector 0.063 0.377
0.289 0.593, power supply 0.063 0.167 0.271 0.272. T and B: ladder 0.097 0.258 0.229 0.374,
PORTAMENTO and IC5 0.212 0.258 0.378 0.427, trigger detector 0.077 0.362 0.303 0.580, power supply
0.086 0.153 0.292 0.266. G: ladder 0.084 0.273 0.224 0.341, trigger detector 0.076 0.373 0.300
0.558, IC5 and the jacks 0.330 0.227 0.437 0.461, power supply 0.122 0.140 0.310 0.237. PCB: KBD
FOLLOWER 0.064 0.718 0.329 0.920, TRIG DETECTOR 0.400 0.515 0.557 0.883. A is legible at 3x for a
crop, T at 2x, B at 1.6x. Every value below was read in A, T and B; where G is cited it agrees.

On the drawing a boxed "+" is +15 V, a boxed "-" -15 V (the power supply's outputs carry the same
boxes, "+15V", "-15V"); "+18V" is the raw supply. Circled numbers are the block diagram's check
points.

## 1. How it works, as drawn

**Ladder (KEY BOARD).** A bus bar on +15 V, and 37 contacts ("37 KEY", the dashed middle of the
drawing), each putting one node of a resistor string on the bar. From the bottom key's node (k0,
low C): 403, 427, 403, 427 ... ohm in series (metal film, oval symbols; PL: 1/4 W 1 %), 36 of them
to the top key's node (k36), then a 403 drawn as a plain zigzag from k36 to the ladder's output,
check point 1 (node `l`). A 61.9K (1 %) goes to ground from every node after a 403 (k1, k3 ... k35:
18), and 6.8K from k0. Both ends are drawn: left, k0 with its contact and 6.8K, 403, k1 with its
contact and 61.9K, 427; right, an oval, k35 with its contact and 61.9K, 427, k36 with its contact,
the zigzag 403, check point 1. The ladder parts carry no designators, and they are not on PCB's
KLM-127 (they are on the keyboard).

A key forces its node to +15 V; the network between that node and the output is a lossy line whose
attenuation per whole tone is the same throughout (series 830, shunt 61.9K). Seen from k36, the
line to the right needs about 7.17K to look endless (Z = 403 + 61.9K || (427 + Z)); it gets RA36 403
plus the load at check point 1: R34 2.94K + R35 4.27K (7.21K, into IC4A) in parallel with VR4 (0
to 100K) + R33 68K. VR4 at 46.5K makes the load 6.77K, the match; its range (6.52K to 6.91K)
straddles it. So each key's voltage at `l` is +15 V times an exponential of its distance from the
top: 14.16 V for C-4, 1.77 V for C-1 (section 9).

**Key priority.** With several keys down every closed node sits on +15 V; the voltage at `l` depends
only on the network from the highest closed node to the output, so the highest key wins outright:
**high-note priority**, built into the ladder. MINI says the same of the reissue. In the model the
contacts' 0.1 ohm let a lower key move the result by microvolts (key 5 with key 20: 3.328077 V
against 3.328050 V for key 20 alone, 0.01 cent).

**Divider and IC4A.** R34 2.94K and R35 4.27K (1 %) bring `l` into IC4A's input (pin 3; IC4A, half
a 4558, a follower: pin 2 on its output). The divider is 0.5922: 14.16 V at C-4 becomes 8.39 V,
inside the 4558's input range (DS uPC4558: +-12 V min, +-14 V typ; 14.16 V would not be). This is
the block diagram's "+0~8.4V" and SPEC's "Keyboard control voltage output (exponential) (0~+8V)".

**PORTAMENTO.** IC4A's output, R30 680 ohm, check point 48 (`na`), PORTAMENTO VR10 2M A drawn as
a rheostat (its wiper arrow on the far end), check point 49 (`nb`), Q23's channel (2SK30A GR, drawn
"K30A (GR)"), C6 0.22 uF (PL: polypropylene 200 V, the only 0.22 uF) to ground. R31
10M ties Q23's gate to `na`; C7 100 pF from the gate to ground; the gate is also wired down and left
to Q13's collector in the trigger detector.

**Key hold.** While Q13 is off, R31 holds Q23's gate at `na`: going up, the gate is above the
channel and Q23 is fully on (the forward gate junction passes R31's microamp); C6 charges through R30
+ VR10, an exponential. Going down, the gate is at `na`, below `nb` by the current times VR10, so
Q23 sits at its threshold and passes about |VTO| / R_VR10: a straight-line fall at |VTO| / (R_VR10
C6) volts per second, then an exponential tail (section 9). While Q13 conducts it pulls the gate to
about -5.1 to -6 V, at least 6 V under the channel for any key (C-1 is 1.05 V), and C6 holds.

**IC5 to the jacks.** C6 into IC5's pin 3 (CA3140, "FET"), pin 2 on its own output (pin 6, check
point 2): a follower. VR3 10K B across the offset-null pins 1 and 5, its wiper through R32 1K to
-15 V. R46 100 ohm, outside the loop, to KBD CV OUT (J33's tip) and to J26's normalling contact: an
unpatched VCO 1+2 CV IN takes the keyboard CV through R46. (R207 22M, from +15 V to J26's tip line,
is drawn in the same dashed jack outline: the VCOs' input, [vco.md](vco.md). A and T label the
outline "KLM-128A"; PCB places R46 on KLM-127, next to R30.)

**Key-down comparator, IC13A.** `l` through R141 100K into pin 2; pin 3 at 15 V x 1K / 34K = 0.441
V (R88 33K from +15 V, R87 1K to ground). No key: pin 1 is high (+14 V); any key (C-1 gives 1.77 V)
puts it low (-14 V). R88's lead crosses the pin 2 line without a dot (G shows it plainly; A, T and B
agree in shape, and joining them would short the inputs).

**Hold switch Q13.** Q13 (2SC1685) has its collector on Q23's gate, its emitter on R134 4.7K to
ground and R132 6.8K to -15 V (-6.13 V with Q13 off), R133 100K from base to emitter. Its base is
driven three ways, OR-ed by diodes: from IC13A's output through R86 47K and an undesignated diode
(DX13 here), and from the change detector's two collectors through D12 and D13. With no key down
IC13A is high, Q13 is on and the gate sits at -5.07 V: the hold. With a key down Q13 is off except
while the change detector pulses.

**Change detector.** `l` through C21 0.001 uF and R142 68K into Q16's base (2SC1685, emitter on
ground, R140 6.8K collector load from +15 V, R143 3.3M and C20 22 pF collector to base): an inverting
amplifier with a gain of about 3.3M / 68K for edges, nothing for DC. Its collector (idle 9.9 V)
through C19 0.022 uF into Q15's base (R139 100K to ground). Q15 and Q14 (2SA564A, rank S: drawn
"A564A (S)" between them; PL "2SA-564(S)") are PNPs with their emitters on R137 / R135 100K from +15
V, joined by C18 1 uF 50 V, their collectors on R138 / R136 47K to -15 V (-8.2 V idle, 144 uA
each); Q14's base is grounded. Any step at `l` swings Q16's collector by volts and C19 drives Q15's
base with it; through C18 the pair answers with a collector rising toward ground on one side or the
other (check point 31, Q15's; 32, Q14's), which D13 / D12 pass to Q13's base (check point 20). It
does not settle at once: C19 / R139 (2.2 ms) and C18 make it swing to the other side and back, so
Q13 is held on for 2 ms after a semitone and 8-10 ms after an octave or more (section 9).

**Gate delay, IC13B.** IC13A's output through R85 1M into C17 0.01 uF, then R84 1M into IC13B's pin
5, with R83 10M from pin 7 back to pin 5 and pin 6 on ground: a Schmitt trigger that switches when
C17 passes -1/10 of the output (+-1.4 V). From +-14 V the crossing takes 8.4 ms either way. IC13B's
output (pin 7) drives D11, R82 4.3K into TRIG OUT (J32; R81 2.7K to ground, check point 21): +5.16 V
idle, 0 V while a key is held, SPEC's "Keyboard trigger output (+5V to GND)" and the drawing's
waveform at J32. The same pin 7 lights LED2 through R89 4.7K (LED2's anode on +15 V: lit while the
output is low) and goes through R80 100K to EG 1+2 TRIG IN's normalling contact (J27, check point
33): the envelopes' trigger.

**Power supply.** Mains through the 0.5 A fuse and the switch to the transformer; its secondary has
two ends and a centre tap on ground (circled marks T+, T-, T+). D16 and D19 rectify the two ends
onto the +18 V raw rail, C23 (drawn 1000 uF 25 V) to ground; D18 and D15 the negative raw rail, C24
470 uF 25 V ("+" on its ground side). The diodes are labelled "1S-1885 x 6 (SM-1-02)": D15, D16,
D18, D19 and D40, D41. IC16 7815 (pin 1 in, 2 ground, 3 out) gives +15 V with C37 10 uF 16 V and
D40 (cathode on +15 V) across its output; IC17 7915 (2 in, 1 ground, 3 out) gives -15 V with C36 10
uF 16 V and D41. PL lists the regulators as uPC14315 and uA79M15. ADJ 7-1: ripple at most 2 mVpp
on each rail. The netlists use `VP15 p15 0 15`, `VN15 n15 0 -15` and (VCO cores) `VP18 p18 0 18`;
neither the raw rails' ripple nor the regulators are modelled. At POWER on and off the machine
scales those constants as the rails rise and fall: C23 charging and discharging into the
regulators, an estimate (`core/machine.cpp`, "POWER").

## 2. Parts

Nets as kbd.cir / trig.cir name them. "A T B" = read in all three scans; G only where noted.

| Part | Value / type | Nets | Read in | Notes |
|---|---|---|---|---|
| contacts | 37 | kbus (+15 V bar) - k0 ... k36 | A T B (both ends drawn) | `SK0`...`SK36`, controls key_0 ... key_36 |
| string | 403 / 427 alternating, 1 % | k0-k1 403, k1-k2 427 ... k35-k36 427 | A T B | `RA0`...`RA35`; PL 1/4 W 1 % 403 x 18, 427 x 22 (4 are VCO 1/2's R16, R17, R57, R58) |
| end resistor | 403 (zigzag) | k36 - l | A T B, G | `RA36`; section 3 |
| shunts | 61.9K 1 % | k1, k3 ... k35 - 0 | A T B (the first and last drawn) | `RB1`...`RB35`; PL x 17; section 3 |
| bottom | 6.8K | k0 - 0 | A T B | `RB0`; does nothing while a key is down |
| VR4 | 100K B semi-fixed, as a rheostat | l - r33a | A T B | trim_VR4; KBD CV high (section 4); PL SR19R(100kB) |
| R33 | 68K | r33a - 0 | A T B | |
| R34 | 2.94K 1 % | l - kin | A T B | PL 2.94k x 1 |
| R35 | 4.27K 1 % | kin - 0 | A T B | PL 4.27k x 1 |
| IC4A | 1/2 uPC4558, pins 1-3 | + kin, - o4, out o4 | A T B | IC4B is the VCO's modulation summer |
| R30 | 680 ohm | o4 - na | A T B | |
| VR10 | 2M A, PORTAMENTO, rheostat | na - nb | A T B | control `portamento`, law a0.15 (README placeholder) |
| R31 | 10M | na - gate | A T B | PL solid 10 % 10M |
| Q23 | 2SK30A GR | D nb, G gate, S c6 | A T B | "K30A(GR)"; PL 2SK-30(GR); models.cir 2SK30A_GR |
| C7 | 100 pF | gate - 0 | A T B | PL ceramic 50 V 100 pF |
| C6 | 0.22 uF | c6 - 0 | A (".22"), T B ("22"), G (".22") | PL polypropylene 200 V 0.22 uF |
| IC5 | CA3140, follower | + i5p, - o5, out o5 (pin 6) | A T B | "FET 3140"; PL lists 3140 as the 081/082 alternate |
| VR3 | 10K B semi-fixed | pins 1-5, wiper to R32 | A T B | trim_VR3; KBD CV low; modelled as an offset source (section 8) |
| R32 | 1K | VR3 wiper - -15 V | A T B | inside the stand-in's range estimate |
| R46 | 100 ohm | o5 - kcv | A T B | PL 1 % 100 ohm x 2; PCB on KLM-127 |
| J33 | KBD CV OUT | kcv | A T B | output `kbdcv` |
| J26 | VCO 1+2 CV IN, normal from kcv | | A T B | the VCO block's input `vcocv` |
| R141 | 100K | l - i13n | A T B | |
| R88 | 33K | +15 V - i13p | A T B | |
| R87 | 1K | i13p - 0 | A T B | |
| IC13A | 1/2 uPC4558, pins 1-3 | + i13p, - i13n, out ic13a | A T B | output `ic13a` |
| C21 | 0.001 uF | l - c21b | A T B | |
| R142 | 68K | c21b - q16b | A T B | |
| Q16 | 2SC1685 | C q16c, B q16b, E 0 | A T B | PL 2SC-1685S |
| R143 | 3.3M | q16c - q16b | A T B | |
| C20 | 22 pF | q16c - q16b | A T B | PL ceramic 50 V 22 pF x 1 |
| R140 | 6.8K | +15 V - q16c | A T B | |
| C19 | 0.022 uF | q16c - q15b | A T B | |
| R139 | 100K | q15b - 0 | A T B | |
| R137 | 100K | +15 V - e15 | A T B | |
| R135 | 100K | +15 V - e14 | A T B | |
| C18 | 1 uF 50 V | e15 - e14 | A T B | "1u/50V", "+" toward R137; PL electrolytic 50 V 1 uF |
| Q15 | 2SA564A (S) | C c15, B q15b, E e15 | A T B | check point 31 on its collector |
| Q14 | 2SA564A (S) | C c14, B 0, E e14 | A T B | check point 32 on its collector |
| R138 | 47K | c15 - -15 V | A T B | |
| R136 | 47K | c14 - -15 V | A T B | |
| D13 | 1S1555 | c15 -> q13b | A T B | PL's only small diode, 1S-1555 x 33 |
| D12 | 1S1555 | c14 -> q13b | A T B | |
| R86 | 47K | ic13a - dxa | A T B, G | |
| DX13 | 1S1555 | dxa -> q13b | A T B, G, PCB | no designator anywhere (section 3) |
| R133 | 100K | q13b - q13e | A T B | |
| R134 | 4.7K | q13e - 0 | A T B | the ground drawn as a short bar (section 3) |
| R132 | 6.8K | q13e - -15 V | A T B | |
| Q13 | 2SC1685 | C gate, B q13b, E q13e | A T B | check point 20 on its base |
| R85 | 1M | ic13a - c17 | A T B | |
| C17 | 0.01 uF | c17 - 0 | A T B | |
| R84 | 1M | c17 - i13bp | A T B | |
| R83 | 10M | kbdtrig - i13bp | A T B | |
| IC13B | 1/2 uPC4558, pins 5-7 | + i13bp, - 0, out kbdtrig | A T B (label: section 3) | output `kbdtrig` |
| D11 | 1S1555 | kbdtrig -> d11k | A T B | |
| R82 | 4.3K | d11k - j32 | A T B | |
| R81 | 2.7K | j32 - 0 | A T B | check point 21 |
| J32 | TRIG OUT | j32 | A T B | output `trigout` |
| R89 | 4.7K | kbdtrig - LED2 cathode | A T B | not in trig.cir (no effect on an ideal op-amp) |
| LED2 | LED (PL GD4-203RD) | anode +15 V | A T B | lit while a key is held; on KLM-128C (a-006) |
| R80 | 100K | kbdtrig - J27's normal | A T B, G | in the envelopes' netlists (section 11) |
| fuse, switch, transformer | 0.5 A | | A T B | notes only |
| D15, D16, D18, D19 | 1S-1885 (SM-1-02) | rectifiers | A T B | notes only |
| C23 | 1000 uF 25 V | +18 V raw | A T B | PL 25 V 10000 uF x 1 (section 3) |
| C24 | 470 uF 25 V | negative raw | A T B | PL 25 V 470 uF x 1 |
| IC16 | 7815 (PL uPC14315) | +15 V | A T B | |
| IC17 | 7915 (PL uA79M15) | -15 V | A T B | |
| C36, C37 | 10 uF 16 V | regulator outputs | A T B | PL 16 V 10 uF x 12 |
| D40, D41 | 1S-1885 | across the outputs | A T B | |

## 3. Where the drawing was hard, and how it was settled

- **IC13B's label.** The op-amp with pins 5, 6, 7 reads "IC18" or "IC1B" in A, T and B, where the
  first half reads "IC13". G labels both "IC13"; PCB has a single IC-13 in the TRIG DETECTOR and
  IC-18 in EG 1, which the drawing labels "TL082 IC18". Pins 5-7 are a dual's second half: IC13B.
- **Check point 1's level.** The drawing puts check point 1 on the ladder's output, before R34
  (all three scans, and G); BD gives it "+0~8.4V". The ladder's output reaches 14.16 V at C-4; 8.39
  V is IC4A's input and the KBD CV. The level belongs after the divider; the circle is placed one
  node early.
- **The ladder against the parts list.** As drawn (both ends, the alternation, a shunt after each
  403) the ladder has 19 x 403, 18 x 427 and 18 x 61.9K; PL counts 403 x 18, 427 x 22 (18 after the
  VCO cores' four) and 61.9K x 17. Each part the counts would remove breaks the scale: without the
  end 403 the top C is 8.88 V (BD: 8.4) and VR4 cannot bring the top octave under 1225.9
  cents; without the shunt at k1 the lowest semitone is 89.5 cents; without the one at k35 a
  semitone near the top is 89.3 cents and the top octave 1127 cents (computed with the netlist's
  ladder). The drawing is followed; the counts are an open question (the ladder is on the
  keyboard, perhaps listed apart).
- **The end resistor's symbol.** Every other ladder resistor is an oval (metal film); the 403 from
  k36 to the output is a zigzag in A, T, B and G. 403 is not an E12 value, so it is taken as the
  same 1 % part.
- **R134's ground.** R134 ends on a short bar, not the hatched ground used elsewhere (A; faint ticks
  under it in T and G). Read as ground: floating, Q13 is never off (D12 / D13 hold its base above
  its emitter on R132) and the CV sticks near -8 V (tested in a copy of the netlist).
- **DX13.** The diode from R86 to Q13's base has no designator in A, T, B or G; PCB shows a diode
  beside R86 without one. Its direction (cathode on the base) is the only one that holds C6 with no
  key down.
- **C6** reads ".22" in A and G, "22" in T and B (the point lost); PL's only 0.22 uF part settles it.
- **C23** is drawn "1000u/25V" in all three; PL lists "25V-10000uF x 1" and no 1000 uF. Not
  modelled (the raw rail is a constant).
- **ADJ 7-3 b** says to set the low trim "so that C-2 produces exactly half the voltage of C-4";
  step c ("each of C-1, C-2, C-3 and C-4 is exactly half that of the next") and step a (C-3 = half
  C-4) make it C-1 = half C-2, which is what section 4 sets.
- **SPEC's portamento** reads "max. 00 sec" in A, B and the TIFF's page 2: a misprint; no figure.

## 4. Trims and the service check 7-3

Two semi-fixed resistors are in this block, both reached through the panel (ADJ Fig. 1: "KBD CV
ADJ", hole 5 under MASTER TUNE, hole 6 under PORT):

| Trim | Check | Evidence |
|---|---|---|
| **VR4** 100K B (rheostat with R33) | **KBD CV high, 5**: C-3 = 1/2 C-4 | PCB puts VR4 beside MASTER TUNE (VR11); it sets the ladder's termination, which moves the top octave most (1224.6 to 1188.4 cents over its range, the bottom octave 1201.5 to 1199.3) |
| **VR3** 10K B (IC5's offset null) | **KBD CV low, 6**: C-1 = 1/2 C-2 | PCB puts VR3 beside PORTAMENTO (VR10); an offset moves the lowest octave most (1227.5 to 1177.7 cents over the stand-in's +-30 mV, the top octave 1206.7 to 1194.4) |

The schematic marks both with the same black slotted circle (a screwdriver slot, no number).

**7-3 from the netlist** (the harness of section 9 solving both conditions on kbd.cir's DC point):
trim_VR4 = 0.4649 (VR4 46.5K: the line's match, section 1) and trim_VR3 = 0.5413, an offset of
+2.48 mV at IC5's input. Then C-1 1.047986 V, C-2 2.095972 V, C-3 4.193681 V, C-4 8.387362 V: C-1 =
C-2 / 2 and C-3 = C-4 / 2 to 1e-10 V; the middle octave is 1200.72 cents. These are kbd.cir's
defaults. The +2.48 mV is this circuit's need; a real IC5's own offset (FN957: 5 mV typ, 15 max)
adds to it, so a unit's VR3 sits elsewhere.

## 5. Key priority

High note, by the ladder (section 1): with keys 5 and 20 closed, kbdcv is key 20's (3.328077 V,
ngspice the same; key 20 alone 3.328050 V). MINI: "the highest tone played will be produced". A
key below a held one changes nothing at `l`, so it neither moves the CV nor wakes the change
detector.

## 6. The trigger rule

- **Gate.** IC13A follows "any key down" (`l` above 0.441 V); IC13B follows IC13A 8.4 ms later on
  both edges (8.45 ms after the first key goes down, 8.40 ms after the last is released). TRIG OUT,
  LED2 and the EG normal are IC13B's output: S-trigger polarity, +5.16 V idle at J32, 0 V held.
- **Legato** (a second key while one is held, or releasing the higher of two): `l` stays above the
  threshold, IC13A does not move, **no retrigger**. The change detector holds the CV for 2 ms
  (a semitone) to 10.5 ms (three octaves down), then the CV moves to the new key (with portamento if
  set). A key below the held one changes nothing at all.
- **Release and press again:** a gap up to 8.3 ms is bridged (no release, no retrigger); from 8.5 ms
  the trigger goes off 8.45 ms after the release and on again 2.35 ms (gap 8.5 ms) to 8.4 ms (long
  gaps) after the new key.
- **A CV change** never touches the gate: the change detector drives only Q13, the hold.
- **The order in a fresh note.** A press after a release steps `l` up from 0 V, a large step
  whatever the interval, so the hold lets go 7.6-10 ms after the key and the CV is within 1 cent
  9.0-11.6 ms after it (a semitone up from C-1: 9.0 ms; C-4 down to C-1: 10.9 ms); the trigger comes
  at 8.45 ms. So every detached note with a new pitch starts its envelope 0.6-3 ms before the CV has
  arrived, gliding from the old pitch (section 9). In legato the gate does not retrigger and small
  intervals land in 3-7 ms.

## 7. How the keys enter the netlist

**37 S switches, controls key_0 ... key_36**, each from the bus bar (`kbus`) to its node, closed at
0.1 ohm, open at 1e12. Not a per-key voltage table. Why:

- The ladder then is the circuit: priority, the source impedance the change detector sees through
  C21 (403 ohm at C-4 to 7.4K at C-1), and VR4 as a live trim all come out of it. A table would need
  the board to pick the highest key, would be stiff at `l` (the detector's input time constant off
  by up to 11 %) and would have to be recomputed when VR4 moves.
- It costs nothing per sample: the 38 ladder nodes, 56 resistors and 37 switches are linear, so
  the state-space system has the same 10 states and 22 ports with or without them. A key event
  rebuilds the matrices: 41 us at 192 kHz, 0.19 ms at 96 kHz for the step that carries it (the
  latter includes the detector's Newton work); a steady held key runs about 0.8 us a step at 96 kHz
  (circuitrun, 5 s).
- MIDI maps straight onto contacts: note on closes key_n, note off opens it, any number held. The
  reissue's keyboard sends notes 48-84 (MINI's MIDI chart), so key_0 = note 48 is a natural default.

**kbtest.** circuitrun holds controls for a whole run, so the cases cannot close a contact in time.
kbd.cir's bus is `VKBUS kbus p15 input kbtest`: +15 V plus `kbtest`, which the instrument holds at
0. A case closes some keys and moves the bar: -15 V is the ladder with no key down (every node at 0
V, as with all contacts open; only the source impedance at `l` differs, 0 instead of the open
ladder's); because the ladder is linear, the bar at 15 x V_i / V_j with key j closed puts key i's
voltage on `l`, so a step of the bar is a legato from key i to key j (C-2 on key 24: kbtest
-7.507538; C-1 on key 36: -13.129652).

## 8. The netlist

**kbd.cir.** Input `kbtest`. Outputs `kbdcv` (J33 and J26's normal), `ic13a` (trig.cir's input);
probes `l`, `kin`, `na`, `nb`, `c6`, `q16c`. Controls key_0 ... key_36, `portamento`, `trim_VR4`
(default 0.4649), `trim_VR3` (0.5413). Param `vnull3` (30m).

**trig.cir.** Input `ic13a`. Outputs `kbdtrig` (IC13B pin 7), `trigout` (J32); probe `c17`.

**Split.** Only at IC13A's output: everything from the ladder to Q13 shares passive nodes (`l` feeds
both IC4A's divider and the detector; Q13's collector is Q23's gate).

**Stand-ins.** VR3: CA3140 offset nulling has no pin in an ideal op-amp, so VR3 is a pot between
+-`vnull3` whose wiper voltage EOS5 adds in series with IC5's pin 3 (no current from C6). The reach,
+-30 mV, is an assumption: FN957 gives 4.7K from pin 1 or 5 to pin 4 for its maximum offset (15
mV); here an end of VR3 has R32 1K, so more. 7-3 uses +2.48 mV of it. Contacts are 0.1 ohm.
Nothing else is added.

**Semiconductors** from models.cir (fits in semiconductors.md): 2SK30A_GR (VTO -2.10 V, beta 0.935
mA/V^2: I_DSS 4.1 mA), 2SC1685, 2SA564A, 1S1555; op-amps by models.cir's uPC4558C (3 MHz, 1e5,
+-14 V, 1 V/us) and CA3140 (4.5 MHz, 1e5, 13 V, 9 V/us) parameters.

**Rate.** At least 64 kHz. The detector's first pulse lasts a few microseconds; at 44.1-48 kHz it is
averaged into the sample, Q23's gate only reaches about -2 V, and Q23 leaks before the second pulse
holds it: C-1 to C-4 legato moves the CV 100 cents (48 kHz) or 1038 cents (44.1 kHz) during the
hold, 14 / 39 cents with PORTAMENTO at 0.5. None at 64, 96 or 192 kHz. Substeps at the key event
(Circuit::substeps, jump) did not cure it at 48 kHz (tested); a higher rate does.

**DC operating point.** With no key down, `solveDc` leaves C6 where Q23 leaks it to: about -2.96 V
(the gate at -5.07 V less VTO), the circuit's true end state after a very long time. A power-on
starts C6 at 0 V (the VCOs then sit at 0 Hz until the first key); the board should set C6's state.
With a key down the operating point is that key's voltage.

## 9. Measurements (kbd.cir and trig.cir; harness and circuitrun at 96 kHz unless stated)

"Harness" is a scratch program (not in the repo) that loads `models.cir,kbd.cir` into
ms20::circuit as the board will: it solves the DC point per key, solves 7-3 for the two trims by
Newton, and steps the circuit with key controls switched at given times. circuitrun runs trig.cir.

**Per key** at the 7-3 setting (KBD CV OUT, and `l`; cents from the semitone below and from equal
temperament on C-1):

| Key | kbdcv V | l V | step | dev | Key | kbdcv V | l V | step | dev |
|---|---|---|---|---|---|---|---|---|---|
| C-1 | 1.047988 | 1.7654 | | 0.00 | G-2 | 3.141146 | 5.2998 | 100.07 | +0.40 |
| C#-1 | 1.110270 | 1.8706 | 99.95 | -0.05 | G#-2 | 3.328050 | 5.6154 | 100.06 | +0.47 |
| D-1 | 1.176260 | 1.9820 | 99.96 | -0.10 | A-2 | 3.526081 | 5.9498 | 100.07 | +0.53 |
| D#-1 | 1.246183 | 2.1001 | 99.97 | -0.13 | A#-2 | 3.735886 | 6.3041 | 100.06 | +0.59 |
| E-1 | 1.320269 | 2.2252 | 99.98 | -0.15 | B-2 | 3.958179 | 6.6794 | 100.06 | +0.66 |
| F-1 | 1.398769 | 2.3577 | 99.99 | -0.16 | C-3 | 4.193682 | 7.0771 | 100.06 | +0.71 |
| F#-1 | 1.481942 | 2.4982 | 100.00 | -0.16 | C#-3 | 4.443199 | 7.4984 | 100.06 | +0.77 |
| G-1 | 1.570071 | 2.6470 | 100.01 | -0.15 | D-3 | 4.707533 | 7.9448 | 100.05 | +0.82 |
| G#-1 | 1.663447 | 2.8046 | 100.02 | -0.14 | D#-3 | 4.987593 | 8.4177 | 100.05 | +0.87 |
| A-1 | 1.762386 | 2.9717 | 100.02 | -0.11 | E-3 | 5.284272 | 8.9186 | 100.03 | +0.90 |
| A#-1 | 1.867215 | 3.1487 | 100.03 | -0.08 | F-3 | 5.598593 | 9.4494 | 100.03 | +0.93 |
| B-1 | 1.978288 | 3.3363 | 100.04 | -0.04 | F#-3 | 5.931547 | 10.0116 | 100.01 | +0.94 |
| C-2 | 2.095974 | 3.5350 | 100.04 | 0.00 | G-3 | 6.284290 | 10.6072 | 100.01 | +0.95 |
| C#-2 | 2.220669 | 3.7456 | 100.05 | +0.05 | G#-3 | 6.657919 | 11.2381 | 99.99 | +0.94 |
| D-2 | 2.352786 | 3.9686 | 100.05 | +0.10 | A-3 | 7.053740 | 11.9065 | 99.98 | +0.92 |
| D#-2 | 2.492772 | 4.2050 | 100.06 | +0.15 | A#-3 | 7.472961 | 12.6144 | 99.95 | +0.87 |
| E-2 | 2.641087 | 4.4555 | 100.06 | +0.21 | B-3 | 7.917060 | 13.3642 | 99.94 | +0.81 |
| F-2 | 2.798236 | 4.7208 | 100.06 | +0.27 | C-4 | 8.387364 | 14.1584 | 99.90 | +0.71 |
| F#-2 | 2.964734 | 5.0019 | 100.06 | +0.34 | | | | | |

Semitones 99.90 to 100.07 cents; equal temperament within -0.16 to +0.96 cents; octaves 1200.00,
1200.72, 1200.00. **0 to +8.39 V** at IC4A's input and KBD CV OUT (0 V there with no key; KBD CV
OUT keeps the last key). Released, the CV drops 0.11 mV (384 kHz; 0.19 cent at C-1, 0.02 at C-4)
and then holds.

**Hold after a key event** (PORTAMENTO 0, legato; ms after the key: the CV's first 1 mV move, and
within 1 cent):

| From C-1 up to | C#-1 | D-1 | E-1 | G-1 | C-2 | C-3 | C-4 |
|---|---|---|---|---|---|---|---|
| first move | 2.00 | 2.21 | 2.55 | 5.28 | 7.32 | 8.22 | 8.50 |
| within 1 cent | 3.33 | 3.71 | 4.22 | 7.12 | 9.37 | 10.74 | 11.56 |

| From C-4 down to | B-3 | A#-3 | G#-3 | F-3 | C-3 | C-2 | C-1 |
|---|---|---|---|---|---|---|---|
| first move | 3.71 | 4.54 | 7.32 | 8.48 | 9.41 | 10.22 | 10.49 |
| within 1 cent | 5.10 | 6.18 | 9.26 | 10.72 | 11.97 | 13.26 | 13.82 |

After a release (50 ms later, or more), a new key; first move / within 1 cent, ms:

| Released, pressed | C-1, C#-1 | C-2, C#-2 | C-3, D-3 | C-4, B-3 | C-1, C-4 | C-4, C-1 | C-2, C-3 | C-3, C-2 |
|---|---|---|---|---|---|---|---|---|
| first move | 7.70 | 8.63 | 9.29 | 10.06 | 8.57 | 7.62 | 8.68 | 8.62 |
| within 1 cent | 9.02 | 9.97 | 10.88 | 11.47 | 11.64 | 10.95 | 10.93 | 10.89 |
 The same at 48, 96, 192 and 384 kHz within 0.05 ms. With the 2SA564A's emitter-base
breakdown added as a stand-in (a diode at 5, 6, 7 or 9 V, scratch copy) the holds change by -0.6 to
+1.4 ms: the drawn circuit's reverse bias reaches 8.4 V on Q14 (DS: V(BR)EBO 5 V min), so the real
pair breaks down; left out, as no breakdown figure is published.

**Portamento**, C-1 to C-4 and back (legato; within 1 cent of the target, from the key):

| PORTAMENTO | 0.1 | 0.25 | 0.5 | 0.75 | 1 |
|---|---|---|---|---|---|
| VR10 | 60K | 150K | 300K | 1.15M | 2M |
| up | 0.107 s | 0.251 s | 0.489 s | 1.81 s | 3.07 s |
| down | 0.157 s | 0.370 s | 0.723 s | 2.72 s | 4.69 s |

Up is an exponential, tau (R30 + VR10 + Q23's channel) C6; down starts as a straight line at about
|VTO| / (VR10 C6) (4.8 V/s at 2M) while Q23 sits at its threshold, then an exponential: slower, and
its speed set by the JFET's VTO (DS: V_GS(off) -0.4 to -5 V; models.cir -2.10 V).

**Trigger** (trig.cir): kbdtrig +14.00 V idle, -14.00 V held; TRIG OUT 5.162 V idle, 0.000 V held;
the gate 8.45 ms after IC13A's first edge, 8.40 ms after its last. Gaps between notes: 2, 5, 8,
8.3 ms bridged; 8.5 ms off at 8.45 ms, on 2.35 ms after the new key; 9 ms 2.80; 12 ms 4.75; 20 ms
7.08.

## 10. Against ngspice

`tools/spice/cases/keyboard.json`, 768 kHz (the switching edges need it), kbd cases compared from 5
ms (circuitrun's DC solution, at its 10 uV tolerance, leaves Q16's collector 6 uV off and the
high-gain detector rings for 2 ms). Largest difference in % of each output's range (and in mV):

| Case | kbdcv | ic13a | l | kin | na | nb | c6 | q16c |
|---|---|---|---|---|---|---|---|---|
| kbd_c3_release_press_c2 | 0.310 (6.5) | 0.216 | 0.020 | 0.021 | 0.109 | 0.109 | 0.310 | 0.032 |
| kbd_c1_release_press_c4 | 0.081 (6.0) | 0.265 | 0.004 | 0.003 | 0.098 | 0.098 | 0.081 | 0.086 |
| kbd_c4_release_press_c1 | 0.433 (31.8) | 0.265 | 0.004 | 0.003 | 0.137 | 0.137 | 0.433 | 0.086 |
| kbd_priority | 0.253 (4.2) | 0.179 | 0.036 | 0.037 | 0.109 | 0.109 | 0.253 | 0.059 |
| kbd_legato | 0.571 (12.0) | 0.210 | 0.015 | 0.014 | 0.090 | 0.090 | 0.571 | 0.028 |
| kbd_glide | 0.012 (0.85) | 0.266 | 0.005 | 0.006 | 0.091 | **6.191** | 0.012 | 0.076 |
| kbd_trim_ends | 0.238 (17.5) | 0.264 | 0.004 | 0.003 | 0.098 | 0.098 | 0.238 | 0.086 |

| Case | kbdtrig | trigout | c17 |
|---|---|---|---|
| trig_press_release | 1.203 | 2.479 | 0.001 |
| trig_gaps | 1.203 | 2.479 | 0.001 |

The held CVs agree to the microvolt (C-1 1.047989, C-2 2.095974, C-3 4.193682, C-4 8.387364 V in
both; after the release 1.047756 against 1.047753). The kbdcv differences are where the CV is
slewing: the hold lets go 5-12 us apart. They come from Q13 switching: after a detector pulse the
gate is left 0.44 V apart for 150 us (the solver higher), which moves Q23's turn-on; `nb`'s 6.2 % in
kbd_glide is that turn-on, 0.52 V for under 0.1 ms. With MS20_TOL=1e-7 (the solver's Newton
tolerance, default 1e-5 V) they shrink three- to four-fold (nb 1.9 %, the gate 1.2 %, q13b 1.6 %), at
150 Newton iterations a step. The gate and Q13's base are therefore not outputs. trig's 1.2 % / 2.5 %
is IC13B switching 0.36 us apart from ngspice: it does not shrink with the rate (192 kHz to 3
MHz), and is 0.004 % of the 8.4 ms delay. c17, the delay itself, agrees to 0.3 mV.

## 11. What the other blocks take

- **VCOs** (`vcocv`, J26 unpatched): kbdcv, 1.047988 V (C-1) to 8.387364 V (C-4), Hz/V (each octave
  doubles); the source is IC5's output (CA3140) through R46 100 ohm, outside the loop, so 100 ohm.
  The two footage ladders load it with hundreds of kilohms: a fixed scale error of about 100 / R_load
  (0.03 % at 400K), which the VCOs' scale trims take out; a stiff `vcocv` input is right to that
  accuracy. R207 22M to +15 V (0.7 uA, 70 uV across R46) is the VCO side's. The CV moves only after
  the hold: 2-14 ms after a key (section 9), with the trigger at 8.45 ms.
- **Envelopes** (`trig`, J27 unpatched): not a 0 / +5 V source. J27's normal is IC13B's output
  (`kbdtrig`, +-14 V, an op-amp: stiff) through R80 100K. With EG 2's input alone on the tip (D31,
  R193 220K to +15 V) the tip sits near +14 V idle and about -5.1 V held (14.4 V across R193 + R80
  with D31 on). EG 1's J20 normal hangs on the same tip. The envelope netlists should put R80 between
  `kbdtrig` and J27's tip when unpatched. TRIG OUT (`trigout`, 0 / +5.16 V behind 1.66K) is the jack
  only.
- **Panel**: LED2 is lit while kbdtrig is low (about 5.8 mA through R89 at -14 V with a 1.8 V drop
  across it: an estimate, no sheet for the GD4-203RD).

## 12. Open questions

- The ladder's part counts against PL (section 3).
- The change detector's timing (2-14 ms of hold, the trigger at 8.4 ms) rests on C18 1 uF, C19
  0.022 uF, the 2SA564A's h_FE and its emitter-base breakdown (unpublished). A fast-attack recording
  with large leaps (a pitch scoop in the first milliseconds) or of the KBD CV itself would check it.
- VR3's reach (+-30 mV assumed) and the CA3140's own offset: they set where VR3 sits, not the
  calibrated result.
- Q23's VTO sets the downward portamento's speed (the straight-line part); the GR rank allows -0.4
  to -5 V. A unit's down/up portamento times would fit it.
- Contact resistance (0.1 ohm assumed) and bounce; the real contacts' resistance would make the
  priority slightly imperfect (27 uV here).
- PORTAMENTO's law (a0.15) and the spec's maximum time (misprinted).
