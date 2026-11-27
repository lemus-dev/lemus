# KLM-129: the External Signal Processor (esp.cir, espenv.cir, espfv.cir)

The ESP takes an instrument at SIGNAL IN and gives an amplified copy, a band-passed copy, a
pitch-to-voltage CV, an envelope and a trigger. It is board KLM-129, drawn in the lower half of the
service manual's circuit diagram (2); its jacks are on KLM-128 (`klm128.md` §6). Sources as in
`klm128.md` (A, T, B, PCB, PL, BD, SPEC, OM), plus:

| Key | What | Where (under `private/ms20/docs/`) |
|---|---|---|
| **SUP** | Owner's manual supplement for the ESP (8 pp; archive.org OCR) | `manuals/owners_supplement/korgms-20ownersmanualsupplement.pdf` (09eab944…), `…_djvu.txt` |
| **OM7** | Korg's original owner's manual p. 7, "8) Using the External Signal Processor" | `manuals/korg_original_owners_manual/MS20_E.pdf` |
| **339** | µPC339 data sheet (NEC/Renesas) | `datasheets/upc339/upc339_renesas.pdf` |

Crops (fractions of the upright image, x0 y0 x1 y1; T and B share one layout):

| Block | A | T, B |
|---|---|---|
| Preamp, SIG LEVEL | 0.05 0.63 0.30 0.76 | 0.13 0.57 0.32 0.70 |
| Envelope follower, trigger, LEDs, AMP OUT | 0.04 0.75 0.34 0.94 | 0.13 0.67 0.37 0.83 |
| Low cut, high cut, FILTER OUT | 0.29 0.64 0.51 0.80 | 0.30 0.58 0.51 0.72 |
| F-V comparators, monostable | 0.46 0.64 0.69 0.87 | 0.50 0.59 0.72 0.80 |
| 4069 chain, averaging filter, CV ADJ, J18 | 0.44 0.80 0.77 0.95 | 0.49 0.72 0.79 0.84 |
| Decoupling | 0.79 0.70 0.86 0.83 | 0.81 0.64 0.88 0.75 |

The netlist is split at op-amp outputs: `esp.cir` (preamp, filters, AMP OUT, FILTER OUT) gives
`esppre` (IC7b's output) to `espenv.cir` and `espbp` (IC5's output) to `espfv.cir`. Every value was
read in A and T, and in B where marked.

## 1. Preamp (esp.cir)

### 1.1 As drawn

SIGNAL IN (J1, check point 1) through R51 2.2k to one end of SIG LEVEL VR7 (1M B). VR7's wiper is
IC7a's − input (pin 2; pin 3 grounded); its other end goes to the junction of C16 and C15 (1µ/50V
each, their + ends at that junction: back to back). C16's other end is IC7a's output (pin 1), so
VR7 splits into IC7a's input resistor (R51 + the wiper-to-R51 part) and part of its feedback (the
other part, through C16), with R53 1M ∥ C17 100p from pin 2 to pin 1. Mid-band gain

  −(R_upper ∥ 1M) / (2.2k + R_lower),

0 with the wiper at C16's end and −(1M ∥ 1M)/2.2k = **−227 (47.1 dB)** at R51's end. SIG LEVEL turns
the gain up clockwise (SUP: "Set this so that at your loudest signal, the peak light goes on"), so
the wiper reaches R51's end at 10 [I]. C15 and R50 10k into IC7b (pin 6, pin 5 grounded, R49 220k):
**−22 (26.8 dB)**. IC7b's output (pin 7) feeds AMP OUT (J9) through R47 22k / R46 2.7k (×0.109, check
point 2), the envelope follower (D4), and R55 680Ω / R56 1k (×0.595) into the band-pass.

### 1.2 Parts

| Part | Value | Nets (esp.cir) | Read in |
|---|---|---|---|
| J1 | ESP SIGNAL IN | `j1` | A, T, PCB |
| R51 | 2.2k | `j1`, `r51b` | A, T |
| VR7 | 1M B (PL "Printed 1MB") | a `r51b`, wiper `ic7n2`, b `c16p` | A, T, B |
| R53 | 1M | `ic7n2`, `ic7o1` | A, T |
| C17 | 100p ceramic | `ic7n2`, `ic7o1` | A, T |
| IC7 | µPC4558 | a: 3 ground, 2 `ic7n2`, 1 `ic7o1`; b: 5 ground, 6 `ic7n6`, 7 `ic7o7` | A, T |
| C16 | 1µ/50V, + to `c16p` | `c16p`, `ic7o1` | A, T, B |
| C15 | 1µ/50V, + to `c16p` | `c16p`, `c15b` | A, T, B |
| R50 | 10k | `c15b`, `ic7n6` | A, T |
| R49 | 220k | `ic7n6`, `ic7o7` | A, T |
| R47 | 22k | `ic7o7`, `j9` | A, T |
| R46 | 2.7k | `j9`, ground | A, T |
| J9 | AMP OUT | `j9` | A, T, PCB |
| R55 | 680Ω | `ic7o7`, `fin` | A, T |
| R56 | 1k | `fin`, ground | A, T |

### 1.3 Checks

- **Gain against SIG LEVEL** (IC7b's output at 1 kHz; esp.cir, 192 kHz): 0.02 −7.2 dB, 0.1 +6.9,
  0.25 +15.3, 0.5 +23.1, 0.75 +31.1, 0.9 +39.8, 1.0 **+73.3 dB** (peak +73.9). The last tenth of the
  rotation adds 33 dB: the B-taper pot's input resistance only reaches 2.2k at its end.
- **Bandwidth** (−3 dB): 21.8 Hz-4.8 kHz at 0.5, 23.6 Hz-2.6 kHz at 10 (C17 with R53 ∥ the upper part
  of VR7; C15/C16 with R50).
- **Input resistance**: 2.2 kΩ (at 10) to 1.0 MΩ (at 0), to virtual ground. An instrument's own source
  resistance divides into it (a guitar pickup's several kΩ at full level).
- **Levels**: IC7b clips at ±14 V; AMP OUT (J9) is ×0.109 of it (−19.2 dB, ±1.5 V, 2.43 kΩ source).
  SPEC: "Input signal level (0dB max.)", "Signal In (auto pad system) (1.0 ~ +14.0V)" (meaning
  unclear), "Amplifier Out".
- Against ngspice: `esp_step_open` (SIG LEVEL 10, 100 µV step over 4 samples) `esppre` 0.69 % largest,
  0.003 % RMS; `esp_step_mid` 0.31 %; `esp_sine_clip` (0.9, 0.3 V at 220 Hz, clipping) 0.20 %.

## 2. Low cut and high cut (esp.cir)

### 2.1 As drawn

- **LOW CUT** (IC6a, pins 3, 2, 1, follower): C12 0.033µ and C10 0.033µ in series from `fin` to pin 3;
  from their junction R52 2k + a VR6 gang to IC6a's output; from pin 3 R45 2k + a VR6 gang to ground.
  A unity-gain Sallen-Key high-pass with equal R and C: Q 0.5.
- **HIGH CUT** stage 1 (IC6b, pins 5, 6, 7): R44 2k + VR5 to node m1, VR5 + R42 2k to pin 5; C8 0.015µ
  from m1 to the output, C9 0.015µ from pin 5 to ground. Equal R and C: Q 0.5.
- **HIGH CUT** stage 2 (IC5, pins 3, 2, 1; its other half drawn "1/2 NC"): R43 2k + VR5, VR5 + R41 2k;
  **C5 0.15µ** from m2 to the output, C7 0.015µ to ground. C5 is printed "0.15" in all three copies
  (T shows the decimal point), where every other capacitor of the section is printed ".015": as drawn,
  Q = ½√(C5/C7) = **1.58** and the stage's f0 is 1/√10 of stage 1's. `.param c5` (0.15u) lets the
  0.015 reading be tried (§2.4).
- FILTER OUT (J17, check point 3) through R39 10k / R40 2.7k (×0.213, 2.13 kΩ source).
- VR5 is "HIGH CUT FREQ 100KC×4", VR6 "LOW CUT FREQ" with "100KC×4" by its dashed gang line: two
  4-gang 100k C pots, VR6's other two gangs in the F-V's averaging filter (§4). PL lists "Printed
  4-ganged 100KC × 1" only [doubt: one or two such pots]. Each gang is drawn as a variable resistor.
  Both cutoffs rise clockwise (SUP: "fully open setting, (low cut '0', high cut '10')"; OM7: "slowly
  turn the HIGH CUT control clockwise until the desired pitch is picked up"). With a C (reverse audio)
  taper that means each gang uses the half of its track whose resistance falls clockwise (100k, 15k
  at mid-rotation, 0): the frequency then sweeps about logarithmically [I]. The netlists use the
  a-wiper part of a `c0.15` pot.

### 2.2 Parts

| Part | Value | Nets (esp.cir) | Read in |
|---|---|---|---|
| C12, C10 | 0.033µ, 0.033µ | `fin`-`hp1`, `hp1`-`ic6p3` | A, T |
| R52 | 2k | `hp1`, `r52b` | A, T |
| VR6 (gang a) | 100k C | `r52b`, `ic6o1` | A, T |
| R45 | 2k | `ic6p3`, `r45b` | A, T |
| VR6 (gang b) | 100k C | `r45b`, ground | A, T |
| IC6 | µPC4558 | a: 3 `ic6p3`, 2 = 1 `ic6o1`; b: 5 `ic6p5`, 6 = 7 `ic6o7` | A, T |
| R44 | 2k | `ic6o1`, `r44b` | A, T |
| VR5 (a, b) | 100k C | `r44b`-`m1`, `m1`-`r42a` | A, T |
| R42 | 2k | `r42a`, `ic6p5` | A, T |
| C8 | 0.015µ | `m1`, `ic6o7` | A, T |
| C9 | 0.015µ | `ic6p5`, ground | A, T |
| R43 | 2k | `ic6o7`, `r43b` | A, T |
| VR5 (c, d) | 100k C | `r43b`-`m2`, `m2`-`r41a` | A, T |
| R41 | 2k | `r41a`, `ic5p3` | A, T |
| C5 | 0.15µ (as drawn) | `m2`, `ic5o1` | A, T, B |
| C7 | 0.015µ | `ic5p3`, ground | A, T, B |
| IC5 | µPC4558, one half | 3 `ic5p3`, 2 = 1 `ic5o1` | A, T, B |
| R39 | 10k | `ic5o1`, `j17` | A, T |
| R40 | 2.7k | `j17`, ground | A, T |
| J17 | FILTER OUT | `j17` | A, T, PCB |

### 2.3 Nominal corners (1/(2πRC), R = 2k + the gang)

| Rotation | 0 | 0.25 | 0.5 | 0.75 | 1 |
|---|---|---|---|---|---|
| Low cut (33n) | 47.3 Hz | 81.1 | 284 | 508 | 2411 |
| High cut stage 1 (15n) | 104 Hz | 178 | 624 | 1117 | 5305 |
| High cut stage 2 (√(150n·15n)) | 32.9 Hz | 56.4 | 197 | 353 | 1678 |

SPEC: "Low cut frequency (50 ~ 2,500Hz)", "High cut frequency (100 ~ 5,000Hz)": the low cut's and the
first high-cut stage's nominal f0 exactly. The second stage's 0.15µ makes the high cut's real top
about a third of SPEC's figure.

### 2.4 Checks (esp.cir, response from IC7b's output to IC5's, re R55/R56's 0.595)

| LOW CUT, HIGH CUT | Peak | −3 dB band | Low side | High side |
|---|---|---|---|---|
| 0, 10 | +3.75 dB at 1468 Hz | 613-1938 Hz | HP alone: −3 dB 73.6 Hz, −6 dB 47.3 Hz | −19.4, then −22.5 dB/oct |
| 5, 10 | +3.28 dB at 1483 Hz | 810-1950 Hz | HP −3 dB 435 Hz | −19.4, −22.6 |
| 10, 10 | −7.2 dB at 1688 Hz | 1279-2239 Hz | HP −3 dB 2980 Hz | −18.6, −22.8 |
| 0, 0 | −6.6 dB at 33.6 Hz | 26-44 Hz | | −18.3, −22.0 |
| 0, 5 | +3.13 dB at 177 Hz | 105-230 Hz | | −19.2, −22.0 |

- **Low cut**: 12 dB/octave (11.2 dB in the octave below its −3 dB point; Q 0.5, so −6 dB at f0);
  −3 dB from 73.6 Hz to 2.98 kHz. At its top R55 ∥ R56 (405 Ω) adds to R52.
- **High cut** as drawn: 24 dB/octave far above, with stage 2's resonance (+3.8 dB) just below the cut.
- With `c5=0.015u`: LOW CUT 0, HIGH CUT 10 gives −0.25 dB peak at 421 Hz and a 71.5 Hz-2.39 kHz band;
  HIGH CUT 0, 30.5-91.5 Hz (−9.1 dB peak at 55 Hz), −15 to −21.6 dB/oct.
- Against ngspice: `esp_step_open` `espbp` 0.083 %, `hp` 0.63 %, `lp1` 0.20 % largest (≤ 0.003 % RMS);
  `esp_step_mid` 0.34 % (`hp`). |H| ours / ngspice (192 kHz, `esp_step_open`): IC7b 73.30 / 73.31 dB
  at 1 kHz, 66.91 / 66.98 at 5 kHz, 61.05 / 61.18 at 10 kHz, 52.86 / 53.18 at 20 kHz (the trapezoidal
  rule's warping of C17's corner); the band-pass 70.98 / 70.99 at 1 kHz, 38.59 / 38.69 at 5 kHz. Below
  −60 dB re the peak (10-20 kHz) the step method's floor takes over.

## 3. Envelope follower, trigger, LEDs (espenv.cir)

### 3.1 As drawn

- **Follower**: IC7b's output through D4 (anode) and R48 1k charges C1 1µ/50V (+ up); R3 22k to ground.
  IC1a (1/2 4558): + (pin 3) on C1; − (pin 2) to R2 100k to ground and R4 100k from the output (pin
  1): gain 2. The line from pin 2 runs down past pin 3's line to R2 with no dot at the crossing in A,
  T or B. ENV OUT (J29, check point 7) through R11 2.7k / R35 2.7k: the envelope itself, 1.35 kΩ source.
- **Trigger**: IC1b (pins 5, 6, 7) with − (pin 6) on IC1a's output; + (pin 5) through R21 3.3k from
  THRESHOLD LEVEL VR3 (10k, PL "Printed 10KB" [I]), which sits between R20 680Ω from +15 V and R1 820Ω
  to ground (1.07 to 14.11 V); R19 100k from the output to pin 5 (hysteresis). Its output through D1
  (anode) and R9 4.3k / R34 2.7k is TRIG OUT (J30, check point 8): +5.2 V idle, 0 V while the envelope
  is above the threshold (SPEC "Trig Out (+5V → GND)", like the MS-20's other triggers). The trigger
  LED (anode on +15 V, cathode through R8 4.7k to IC1b's output) lights while triggered. Clockwise
  lowers the threshold (SUP: "For longest sustain, set at 10"; OM7: "Turn the Threshold Level control
  clockwise … until it lights").
- **Peak LED** (SPEC "Peak indicator"): TR5 (2SC1685, emitter grounded) with its base on R5 100k from
  IC1a's output and R6 150k to −15 V, its collector through R7 2.7k to the LED (anode on +15 V).

### 3.2 Parts

| Part | Value | Nets (espenv.cir) | Read in |
|---|---|---|---|
| D4 | 1S1555 | `pre` → `d4k` | A, T |
| R48 | 1k | `d4k`, `env` | A, T |
| C1 | 1µ/50V, + to `env` | `env`, ground | A, T |
| R3 | 22k | `env`, ground | A, T, B |
| R2 | 100k | `ic1n2`, ground | A, T, B |
| R4 | 100k | `ic1n2`, `ic1o1` | A, T |
| IC1 | µPC4558 | a: 3 `env`, 2 `ic1n2`, 1 `ic1o1`; b: 5 `ic1p5`, 6 `ic1o1`, 7 `ic1o7` | A, T, B |
| R11, R35 | 2.7k, 2.7k | `ic1o1`-`j29`, `j29`-ground | A, T |
| J29 | ENV OUT | `j29` | A, T, PCB |
| R20 | 680Ω | +15 V, `vr3t` | A, T |
| VR3 | 10k (THRESHOLD LEVEL) | a `vr3g`, wiper `vr3w`, b `vr3t` | A, T |
| R1 | 820Ω | `vr3g`, ground | A, T |
| R21 | 3.3k | `vr3w`, `ic1p5` | A, T |
| R19 | 100k | `ic1p5`, `ic1o7` | A, T |
| LED (trigger) | GD4-203RD (PL × 4) | +15 V → `led1k` | A, T |
| R8 | 4.7k | `led1k`, `ic1o7` | A, T |
| D1 | 1S1555 | `ic1o7` → `d1k` | A, T |
| R9 | 4.3k | `d1k`, `j30` | A, T |
| R34 | 2.7k | `j30`, ground | A, T |
| J30 | ESP TRIG OUT | `j30` | A, T, PCB |
| R5 | 100k | `ic1o1`, `tr5b` | A, T |
| R6 | 150k | `tr5b`, −15 V | A, T |
| TR5 | 2SC1685 | C `tr5c`, B `tr5b`, E ground | A, T |
| R7 | 2.7k | `led2k`, `tr5c` | A, T |
| LED (peak) | GD4-203RD | +15 V → `led2k` | A, T |

### 3.3 Checks

- **Attack and release** (`espenv_burst`: a 4 V, 1 kHz burst): C1 reaches 63 % in 2.35 ms, 10-90 % in
  7.1 ms (each peak charges it through R48 1k for a fraction of the cycle); release time constant
  **22.0 ms** (R3 C1 = 22 ms), 10 % after 50 ms. Plateau 2.59 V for a 4 V peak (D4's drop and the
  droop between peaks), 92 mVpp of 1 kHz ripple. ENV OUT reaches 7.0 V at most (IC1a at 14 V);
  SPEC "ENV Out (0 ~ +5V)".
- **Trigger threshold** (`espenv_ramp`, the envelope in ENV OUT volts, rising / falling):

  | THRESHOLD | 10 | 7.5 | 5 | 2.5 | 1 | 0.5 | 0 |
  |---|---|---|---|---|---|---|---|
  | fires at | 0.79 V | 2.42 | 3.98 | 5.51 | 6.43 | 6.74 | never |
  | releases at | 0.24 V | 1.67 | 3.17 | 4.77 | 5.80 | 6.16 | |

  TRIG OUT: +5.16 V idle, 0 V triggered; trigger LED 5.8 mA.
- **Peak LED** lights once IC1a's output passes **11.0 V** (an envelope of 5.5 V).
- Against ngspice: outside the trigger's two edges, every output within 0.009 % (`espenv_ramp`) and
  0.04 % (`espenv_burst`); at the edge samples IC1b slews 28 V at 1 V/µs and `esptrig` differs by up to
  12.9 % of its range for one sample (`espenv_burst` 1.0 %).

## 4. F-V converter (espfv.cir)

### 4.1 As drawn

IC2 is a µPC339C (PL × 1), four open-collector comparators; the board runs it on ±15 V [I: its
outputs are pulled to −15 V by the TR2 network and the level shift below needs V− = −15 V]. Pins by
the 339 sheet (p. 1): OUT2 1, OUT1 2, V+ 3, −IN1 4, +IN1 5, −IN2 6, +IN2 7, −IN3 8, +IN3 9, −IN4 10,
+IN4 11, V− 12, OUT4 13, OUT3 14.

- **A** (4 −, 5 +, out 2; R28 10k to +15 V): IC5's output through R33 10k into pin 4; pin 5 on R30 1k
  to ground and R29 220k from the output: a zero-crossing Schmitt trigger, ±66 mV of hysteresis
  (14.35 V × 1k/221k).
- **B** (6 −, 7 + grounded, out 1; R31 10k to +15 V): A's output inverted (check point 4).
- **Edges into TR2**: C4 220p from B's output to D3's anode (R15 22k from there to −15 V), D3 and R14
  47k to TR2's base; C3 220p from A's output to D2's anode (R13 22k to −15 V), D2 and R12 47k to the
  base; R10 10k holds the base at −15 V. TR2 (2SC1685), emitter on −15 V, discharges C2 (12000p, PL
  "Styrol 50V-12000pF") on every rising edge of A or B: **twice per input period**.
- **C** (8 −, 9 +, out 14): − on C2 (R16 220k from +15 V; R23 22k from C's output); + on R17 2.2M
  from +15 V, R24 100k from its own output and VR2 (100k B trim ❶, a rheostat) + R25 100k from D's
  output. After a reset C2 charges, C's output is off (high); when C2 crosses the threshold C sinks,
  its threshold drops through R24 to about −14.3 V and R23 holds C2 near −12.3 V, above that: C stays
  low until the next reset. A retriggered monostable of fixed length T.
- **D** (10 −, 11 +, out drawn "3"): its − input and the R32/R26 junction are drawn on the +15 V line
  (A, T, B), its + input on Z (R27 10k from C's output, R32 4.7k to +15 V). So D's output sits low
  (R26 15k from +15 V) and VR2 + R25 return to about −15 V. The output is labelled "3", the chip's
  V+ pin (OUT4 is 13): a drafting slip in the pin number. Read the other way (VR2 + R25 to +15 V) C's
  lower threshold would sit near 0 V, above where R23 holds C2, and the monostable would free-run at
  low input frequencies; as drawn it works [I].
- **Z** (check point 5): 5.5 V while C sinks, about 12 V while it is off: R27/R32 shift C's ±15 V
  swing into the 4069's 0-15 V range [I: the 4069 (IC4, "CD4069", PL MC-14069B) on +15 V and ground;
  its supply pins are not drawn]. Six inverters in series (pins 1-2, 3-4, 5-6, 9-8, 11-10, 13-12):
  an even number, so pin 12 is high (15 V) during T.
- **Averaging**: R37 10k + a VR6 gang, a VR6 gang + R54 10k, C11 0.056µ (to IC3's output) and C6 0.056µ
  (to ground), IC3 (TL081, "FET", follower; VR1 10k B with R22 4.7k to −15 V on pins 1, 5, offset
  null, ❶): a unity-gain Sallen-Key low-pass, Q 0.5, f0 25.8 Hz at LOW CUT 0 to 284 Hz at 10. The
  ripple filter follows the low cut: the lowest note sets both.
- **Out**: R36 470Ω into CV ADJ VR4 (10k A, PL "Printed 10KA"), R38 1k below it; VR4's wiper is F-V CV
  OUT (J18, "FOCV CV.OUT"; check point 6 at R36/VR4). SUP: "CV Adjust: Set at 10 for unison pitch".

### 4.2 Parts

| Part | Value | Nets (espfv.cir) | Read in |
|---|---|---|---|
| R33 | 10k | `bp`, `ic2n4` | A, T |
| R30 | 1k | `ic2p5`, ground | A, T |
| R29 | 220k | `ic2p5`, `ic2o2` | A, T |
| R28 | 10k | +15 V, `ic2o2` | A, T |
| R31 | 10k | +15 V, `ic2o1` | A, T |
| IC2 | µPC339C | BIC2A-D | A, T, B |
| C4, C3 | 220p, 220p ceramic (PL 50V-220pF × 2) | `ic2o1`-`c4b`, `ic2o2`-`c3b` | A, T |
| D3, D2 | 1S1555 | `c4b` → `d3k`, `c3b` → `d2k` | A, T |
| R14, R12 | 47k, 47k | `d3k`, `d2k` → `tr2b` | A, T |
| R15, R13 | 22k, 22k | `c4b`, `c3b` → −15 V | A, T |
| R10 | 10k | `tr2b`, −15 V | A, T |
| TR2 | 2SC1685 | C `c2`, B `tr2b`, E −15 V | A, T |
| C2 | 12000p styrol | `c2`, −15 V | A, T |
| R16 | 220k | +15 V, `c2` | A, T |
| R23 | 22k | `c2`, `ic2o14` | A, T |
| R17 | 2.2M | +15 V, `ic2p9` | A, T |
| R24 | 100k | `ic2p9`, `ic2o14` | A, T |
| VR2 | 100k B trim (PL SR19R 100kB) | `ic2p9`, `vr2w` | A, T |
| R25 | 100k | `vr2w`, `ic2o3` | A, T |
| R27 | 10k | `ic2o14`, `z` | A, T, B |
| R32 | 4.7k | `z`, +15 V | A, T, B |
| R26 | 15k | +15 V, `ic2o3` | A, T, B |
| IC4 | CD4069 (MC14069UB) | MN1-MN6, MP1-MP6 | A, T |
| R37 | 10k | `iv6`, `r37b` | A, T |
| VR6 (gangs c, d) | 100k C | `r37b`-`f1`, `f1`-`r54a` | A, T |
| R54 | 10k | `r54a`, `ic3p3` | A, T |
| C11 | 0.056µ | `f1`, `ic3o6` | A, T |
| C6 | 0.056µ | `ic3p3`, ground | A, T |
| IC3 | TL081 | 3 `ic3p3`, 2 = 6 `ic3o6` | A, T |
| VR1, R22 | 10k B trim, 4.7k | IC3's offset null to −15 V; not modelled | A, T |
| R36 | 470Ω | `ic3o6`, `cp6` | A, T |
| VR4 | 10k A (CV ADJ) | a `cp6`, wiper `j18`, b `r38t` | A, T |
| R38 | 1k | `r38t`, ground | A, T |
| J18 | F-V CV OUT | `j18` | A, T, PCB |
| C13, C14 | 100µ/16V | +15 V-ground, ground-−15 V (pins 9, 11, 10); not modelled | A, T |

KLM-129 has no R18 and no TR1, TR3, TR4 on the drawing.

### 4.3 Checks

- **Scale** (ngspice, 200 Hz): pulse **257.7 µs** at `trim_VR2` 0.5, so 15 V × 2 × T = **7.73 V/kHz** at
  IC3 and **7.42 V/kHz at J18** with CV ADJ at 10 (R38 + VR4 over R36 + VR4 + R38: ×0.959; unloaded).
  VR2's range: 316.2 µs (all 100k) to 192.2 µs (none): **5.53 to 9.10 V/kHz** at J18, that is 181 to
  110 Hz/V. CV ADJ at 0 leaves ×0.087 (0.67 V/kHz). For "unison at 10" the VCO's Hz/V must equal the
  F-V's: the VCO pages have its constant; VR2 covers 110-181 Hz/V.
- **Range**: linear within 0.4 % from 30 Hz to 1.6 kHz (12.5 V at IC3); IC3 clips at 13.5 V; at
  1/(2T) = 1.94 kHz the pulses merge. SPEC "CV Out (F∝V) (0 ~ +8.4V)": 8.4 V is 1.13 kHz at 7.42 V/kHz.
- **Ripple**: the pulse train's fundamental (twice the input frequency, 2·15 V/π · sin(2πfT)) through
  the averaging filter (1/(1 + (2f/f0)²)), mVpp at IC3's output, for inputs above the low cut's
  nominal f0 (the netlist gives 28.8 mVpp at 200 Hz, LOW CUT 0, against 25.3 from the fundamental
  alone):

  | LOW CUT (averaging f0; low cut f0) | 50 Hz | 100 | 200 | 400 | 800 |
  |---|---|---|---|---|---|
  | 0 (25.8 Hz; 47 Hz) | 97 | 51 | 25 | 12 | 4.8 |
  | 2.5 (42.1 Hz; 81 Hz) | | 131 | 67 | 32 | 13 |
  | 5 (114 Hz; 284 Hz) | | | | 228 | 92 |
  | 7.5 (162 Hz; 508 Hz) | | | | | 187 |

  At 800 Hz and LOW CUT 7.5 that is 187 mV on 6 V, 3 %: about ±26 cents of 1.6 kHz wobble on a VCO
  that follows it. Settling to 1 %: 41 ms at LOW CUT 0, 9.3 ms at 5, 3.7 ms at 10.
- **Input sensitivity**: A's ±66 mV hysteresis at IC5's output; FILTER OUT shows ×0.213 of it.
- **Against ngspice** (`espfv_200hz`, `espfv_800hz_lowcut`, 192 kHz): integrated by the
  trapezoidal rule, C2 read 9.1 % and 14.3 % high (mean of `espfv`): TR2 discharges it in about a
  microsecond and the step overshot that reset (C2 to −17.6 V against ngspice's −14.98), so every
  pulse ran long. Since 2026-11-22 C2 is integrated by backward Euler (`be` in espfv.cir): **+1.06 %**
  and **+0.55 %**. What is left is not the sample grid: our solver at 3.07 MHz reads +1.4 %, with the
  reset 2.9 µs later than ngspice's and IC2C tripping at C2 = −0.78 V against −0.88 V (the pulse
  261.7 µs against 259.0). Timing the 339's flips within the sample (sixteenths, as the op-amp
  comparators') moved the 192 kHz means to +2.1 % / +1.3 %: tried and dropped. The comparator
  outputs, `c2` and the 4069's output differ by up to their full swing at single edge samples.
- **Solver**: at a 1 kHz input the default dense Newton stalls (59 726 failures in 67 200 samples);
  `MS20_SOLVER=sparse`, or `MS20_TOL=1e-3`, runs it with none. At 96 kHz the dense solver fails on
  most edges.

## 5. Interface

**Inputs**: `espin` (J1, no normal; 2.2 kΩ-1 MΩ to virtual ground), `esppre` and `espbp` (op-amp
outputs, internal). **Outputs and source resistances**: `espamp` J9 2.43 kΩ, `espfilt` J17 2.13 kΩ,
`espfv` J18 up to 2.9 kΩ (VR4's wiper), `espenv` J29 1.35 kΩ, `esptrig` J30 1.65 kΩ high, 2.7 kΩ to
ground low. **Controls**: `esp_level`, `esp_lowcut` (esp.cir and espfv.cir), `esp_highcut`,
`esp_cvadj`, `esp_threshold`, each the rotation 0-1; trims `trim_VR2` (F-V scale).

## 6. Reading notes and doubts

- **C5 0.15µ** (§2.1): as drawn in all three copies; SPEC's "100 ~ 5,000Hz" fits 0.015µ better.
- **IC2 D**: the output pin "3" is V+ on the 339; read as OUT4 (13), its − input on +15 V (§4.1).
- **The 4-gang pots**: PL lists one; the drawing has two (VR5, VR6).
- **The gangs' wiring and the C taper** (§2.1) and VR3's taper (B by PL's "Printed 10KB") are inferred.
- research.md §1.4 has F-V CV OUT as J16: it is J18.

## 7. Open questions

- C5's value: a photo of a KLM-129, or a recording of FILTER OUT with a known setting.
- The F-V's scale against the VCO's Hz/V on a real unit (VR2's factory setting).
- The supplies of IC2 (±15 V) and IC4 (+15 V/ground), inferred from the levels; a photo would show them.
