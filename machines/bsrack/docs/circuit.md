# Bass Station Rack: circuit

This is a transcription of the hand-traced keyboard schematic
(`private/bsrack/docs/schematic/zeninstruments-bass-station-schematic.png`, 4206×1405, board
"Novation Bass Station SM, Issue 4, 1995"). Pixel positions refer to that image.

Marks:
- [T] read from the trace;
- [D] from a data sheet;
- [O] from the OSCar's "Filters and VCA" service sheet (`docs/schematic/blocks/oscar4.jpg`). Same
  designer, same filter topology. Used where the Bass Station's trace has the matching part; the
  OSCar uses LM13600s with their buffers, the Bass Station LM3080s with op-amp integrators. Every
  [O] value waits on data: a readable board photo.
- [F] **fitted**: the trace gives no value (research.md §3.7), so the value is chosen to meet the
  owner's manual and the recordings. Every [F] value waits on data: a readable board photo.

Values are filled in as phase 4 fits them. Until then a value cell says "—".

## Rails [T]

| Net | Notes |
|---|---|
| VDD | Positive analog rail. OTA pin 7, op-amp V+. |
| VSS | Negative analog rail. OTA pin 4, op-amp V−. |
| AGND | Analog ground, the signal reference |
| +5V | Logic rail. The 4053 VCC, the 4070, the VCA offset trimmer R77 |

The supply comes from a 9 V DC adapter. The split between VDD, VSS and AGND is not drawn. The
4053's VEE pin goes to VSS and its GND pin to AGND.

## Exponential converters (×4: OSCA, OSCB, VCF, VCA) [T][D]

Each converter is a TLC274 section plus an FMA1 dual digital PNP (two DTA124E, common emitters):
- built-in base resistor R1 = 22 kΩ, range 15.4-28.6 kΩ;
- base-emitter resistor R2 with R2/R1 = 0.8-1.2 (`FMA1_rohm.pdf`, p. 2).

| Pin | Use (as drawn) |
|---|---|
| 3 | Input of transistor 1: AGND |
| 2 | Collector 1: the op-amp's − input, with the reference resistor to VSS and the comp cap to the op-amp output |
| 4 | Common emitters: the op-amp output |
| 5 | Input of transistor 2: the CV through a divider |
| 1 | Collector 2: the output current |

| Converter | Op-amp | FMA1 | Reference R | Comp C | CV divider | Output |
|---|---|---|---|---|---|---|
| OSC A | U24D | Q6 | R46 | C29 | R48 (from OSCA_CV), R47 (to AGND) | Charges C10 |
| OSC B | U24C | Q5 | R43 | C28 | R45 (from OSCB_CV), R44 (to AGND) | Charges C41 |
| VCF | U24A | Q3 | R37 | C26 | R39 (from VCF_CV_LIN), R38 (to AGND) | VCF_CV: OTA bias for U26, U27, U31, U32 |
| VCA | U24B | Q4 | R40 | C27 | R42 (from VCA_CV_LIN), R41 (to AGND) | VCA_CV: U36's bias |

Because of the built-in R1/R2, the base sees about half of the pin's swing. A base-current term is
added on top. Model both transistors from the data sheet: Ebers-Moll with R1 and R2, solved
together with the op-amp loop.

## DCO (×2) [T]

| | OSC A | OSC B |
|---|---|---|
| Ramp cap | C10 | C41 |
| Reset transistor | Q2 BC848 (base via R30) | Q1 BC848 (base via R23) |
| Edge pulse | U20B (4070): pins 5 = OSCA_SYNC, 6 = the same delayed by R29/C9 | U20A: pins 1 = OSCB_SYNC, 2 = delayed by R22/C40 |
| Shaper | U18B (TLC272): + = ramp, − = R31 to VSS and R32 to OSCA_WF_SW | U18A: R24, R25, OSCB_WF_SW |
| Output | OSCA (U18B pin 7) | OSCB (U18A pin 1) |

- **Edge pulse:** the 4070 XOR outputs a pulse as wide as the RC delay **on every edge** of the CPU's
  sync line. That pulse turns the BC848 on and empties the ramp cap.
- **Wave select (U17, 4053):**
  - X section: OSCB_WF_SW ↔ X0 = OSCB, X1 = OSCB_PWM_CV.
  - Y section: OSCA_WF_SW ↔ Y0 = OSCA, Y1 = OSCA_PWM_CV.
  - Section 0 closes U18's loop (sawtooth, gain 1 + R32/R31). Section 1 makes U18 a comparator
    against the PWM CV (pulse).
  - The 4053's A, B and C inputs (pins 11, 10, 9) are drawn unconnected. They come from the CPU side.

## Mixer [T]

| | OSC A | OSC B |
|---|---|---|
| OTA | U21 LM3080 | U19 LM3080 |
| Input divider | R33, R34 (to AGND) | R26, R27 (to AGND) |
| Bias | R35 from A_VOL_CV | R28 from B_VOL_CV |
| − input | AGND | AGND |

Both OTA outputs (pin 6) join at OSC_MIX, which has R36 to AGND. The Rack's FILTER INPUT joins here
too [?] (research.md §3.3).

## Filter, two sections [T]

Section 1 feeds section 2. Each section is a 2-pole lowpass built from two OTA integrators, with an
OTA resonance path through back-to-back diodes.

| Part | Section 1 | Section 2 |
|---|---|---|
| Input resistor (from the previous stage) | R49 from OSC_MIX | R61 from 12DB_AUD |
| Shunt before the cap | R51 | R63 |
| Coupling cap | C30 2.2 µF | C34 2.2 µF |
| Input node A: shunt, first OTA's +, feedback, diodes | R50 | R62 |
| OTA 1 (+ = node A, − = AGND) | U26 | U31 |
| Integrator 1 (OTA current into −, + = AGND) | U25A with C31 | U22A with C35 |
| Divider into OTA 2's − input | R55, R56 (to AGND) | R67, R68 |
| OTA 2 (+ = AGND) | U27 | U32 |
| Integrator 2 | U25B with C32 | U22B with C36 |
| Section output | 12DB_AUD | 24DB_AUD |
| Feedback, output → node A | R57 ‖ C33 | R69 ‖ C37 |
| Integrator 1 out → node B | R52 | R64 |
| Diodes, node B ↔ node A (antiparallel) | D8, D9 | D10, D11 |
| Node B → resonance OTA + (node C) | R53, with R54 to AGND | R65, R66 |
| Resonance OTA (− = AGND, out → the node before the coupling cap) | U28, bias R58 from RES_CV | U33, bias R70 from RES_CV |
| OTA 1 bias from VCF_CV | R59 | R71 |
| OTA 2 bias from VCF_CV | R60 | R72 "272" (2.7 kΩ?) |

- **Slope:** the 4053's Z section takes VCA_IN from Z0 = 24DB_AUD or Z1 = 12DB_AUD.
- The trace's own note says the OSCar's inverting stage is missing: lowpass only.
- RES_CV is set by the CPU. Resonance can't be modulated beyond that (the blog text).

## VCA and output [T]

- **VCA_IN:** R73, then C61 2.2 µF, into U36 (LM3080) + input. The − input goes to AGND.
- **Offset:** R77 is a trimmer from +5 V. Its wiper feeds R76 to the + input node, which has R74 to
  AGND.
- **U36 bias:** from VCA_CV through R75. The pin 5 node also has R79 and C60 to VSS.
- **Output stage:** U22C (TLC274) is a transimpedance stage: − = U36 out, + = AGND, feedback R80.
  Then C38 2.2 µF and R99 to the output jack.

## CV header JP1 [T]

24 pins. Odd/even pairs:

| Pins | Nets |
|---|---|
| 1, 2 | VDD, VSS |
| 3, 4 | AGND, A_VOL_CV |
| 5, 6 | OSC_MIX, VCF_CV |
| 7, 8 | OSCA_PWM_CV, 12DB_AUD |
| 9, 10 | +5V, RES_CV |
| 11, 12 | OSCB_PWM_CV, VCA_CV |
| 13, 14 | OSCB, B_VOL_CV |
| 15, 16 | OSCB_SYNC, 24DB_AUD |
| 17, 18 | OSCA_SYNC, +5V |
| 19, 20 | OSCA, VCA_CV_LIN |
| 21, 22 | OSCB_CV, OSCA_CV |
| 23, 24 | VCF_CV_LIN, VSS |

Owners who probed their keyboards' header give other pin functions:
- one (Gearspace, 2014): 19 osc 1 shaped, 20 osc 1 square, 21 osc 2 square, 22 osc 2 shaped, 8 the
  filter out;
- another (sdiy.org): 6 env 2, 10 env 1, 24 LFO, 21/22 osc 2.

This fits the forum's "several revisions of the bass station circuit board". The header differs
between revisions, so the trace is one revision.

## Not drawn

- The CV multiplexer and buffers between the CPU and the CV nets.
- The CPU, the crystal, the EEPROM and the latches.
- The MIDI optocoupler (6N138, named in a comment).
- The Rack's CV/gate I/O, FILTER INPUT and headphone amplifier.
