# Trigger and accent interface, TOTAL ACCENT

How the CPU's writes reach the voices: the TRIG latches IC1 and IC10, the accent latches IC2-IC9
with their RM0621 resistor DACs, and the TOTAL ACCENT pot VR1 into the CPU's A/D. No netlist: the
DACs are linear, and the voice netlists take each one as its Thevenin source (`acc` behind
`rdac`). Sources as `docs/circuit.md` §0: **SN** p.11 grid A-N 1-5 (crop
`private/docs/schematic/crops/p11-trigger-latches-cpu-interface.png`; the full page for where each
DAC goes), p.5 (circuit description), p.8 (switch board: CN1-CN3, the A/D), p.2-3 (parts list);
**DS** `private/docs/datasheets/` (HD14174B, MC14174B); **RES** `docs/research.md` A2.2, C.4.

## 1. The latches

**IC1-IC10, HD14174BP** (parts list p.2: "CMOS Hex D-Flip-Flop") on the switch board's +5 V
(IC703 7805, `power.md`); VDD pin 16, VSS pin 8 (SN p.11 inset). From the switch board:

- **CN1 (F3)**: data D1-D6 on pins 6, 5, 4, 1, 2, 3; **Reset** pin 7 to every latch's R (pin 1,
  active low); +5 V pin 8; ground pins 9, 10.
- **CN2 (F4)**: pins 11-20, one strobe per latch into its clock (pin 9), from the address decoders
  IC612/IC613 on the switch board. Which strobe is which address isn't traced here; the firmware
  writes 4001-4008 (DACs) and 4009 / 400A (TRIG1 / TRIG2) (RES C.4).
- Each latch's six D inputs (3, 4, 6, 11, 13, 14) take the data lines; the outputs are Q1-Q6 on
  pins 2, 5, 7, 10, 12, 15.

**TRIG latches.** IC1 (TRIG1) and IC10 (TRIG2) drive the voices' trigger lines directly: 0 / 5 V,
high for one panel-scan tick (1.92-2.1 ms, RES C.4) per hit. TRIG2 bit 3 is the hi-hat's
CLOSED/OPEN level, held between ticks. Bit to voice (RES C.4, from the firmware): TRIG1 b1 BD, b2
SD, b3 LT, b4 MT, b5 HT; TRIG2 b1 RS, b2 HH, b3 CLOSED/OPEN, b4 crash, b5 ride, b6 HC.

**Output resistance** (DS MC14174B, the CL/CP grade the HD14174B matches; 25 °C): 0.88 mA typ,
0.44 min at a 0.4 V drop, sourcing or sinking, at VDD 5 V: **455 Ω** typ, 910 Ω at the minimum
(4.2 mA typ at a 2.5 V drop: the channel is resistive over the DACs' and triggers' currents, under
1 mA). The voice netlists hold `rlatch` = 450-600 Ω in series with their trigger inputs: within
that range, and the inputs (transistor bases through 10-22 kΩ, the rim shot's C111 0.018µ) draw
under 0.5 mA, so the choice moves a trigger's level by 1-2 %.

## 2. The accent DACs

**The array.** Each accent latch drives an **RM0621** (parts list p.3, "RM-0621"; no data sheet
found). The SN p.11 inset draws it as six resistors R, 2R, 4R, 8R, 16R, 32R, "R = 5K", to one
common node: 5k, 10k, 20k, 40k, 80k, 160k. Drawn top to bottom in each array: pins 15, 5, 12, 2,
7, 10 (Q6, Q2, Q5, Q1, Q3, Q4), fed from data lines D6, D5, D4, D3, D2, D1: **D6 into 5k (the MSB) …
D1 into 160k (the LSB)**, as the firmware's codes need (the 6-bit code shifted left by one, MAME's
`data & 0x7e`). The assignment rests on the arrays' drawn order matching the inset's.

**Thevenin source.** With ideal outputs the common node is V_oc = 5 V · code / 63 behind
R_th = 5k ∥ 10k ∥ … ∥ 160k = 5 kΩ · 32/63 = **2.540 kΩ**, the model `docs/circuit.md` gives. The
latch's output resistance r_o adds to each resistor; still a Thevenin source (the outputs are
resistive at these currents), but no longer exactly proportional to the code:

V_oc = 5 V · Σ_on g_k / Σ_all g_k, R_th = 1 / Σ_all g_k, with g_k = 1 / (R_k + r_o).

| Code | 5 V · code / 63 | r_o = 455 Ω (typ) | r_o = 910 Ω (min spec) |
|---|---|---|---|
| 10 (normal, TA 0) | 0.794 V | 0.826 V (+4.1 %) | 0.856 V (+7.8 %) |
| 14 | 1.111 | 1.159 (+4.3 %) | 1.203 (+8.3 %) |
| 18 | 1.429 | 1.456 (+1.9 %) | 1.479 (+3.5 %) |
| 21 (accent at TA 0; normal at TA 31; RS, HC) | 1.667 | 1.705 (+2.3 %) | 1.739 (+4.3 %) |
| 25 (CH normal) | 1.984 | 2.031 (+2.3 %) | 2.071 (+4.4 %) |
| 31 | 2.460 | 2.531 (+2.9 %) | 2.595 (+5.5 %) |
| 32 (OH, crash, ride) | 2.540 | 2.469 (−2.8 %) | 2.405 (−5.3 %) |
| 33 | 2.619 | 2.553 (−2.5 %) | 2.494 (−4.8 %) |
| 40 (CH accent) | 3.175 | 3.128 (−1.5 %) | 3.085 (−2.8 %) |
| 47 (accent at TA 31) | 3.730 | 3.712 (−0.5 %) | 3.697 (−0.9 %) |
| 63 | 5.000 | 5.000 | 5.000 |
| R_th | 2.540 kΩ | **2.694 kΩ** | 2.843 kΩ |

The MSB carries less weight than its share: codes under 32 come out high, codes from 32 low; with
typical latches code 32 (2.469 V) is even under code 31 (2.531 V), a −2.4 % step where the ideal
DAC rises 3.2 %. For the analog voices it lifts the normal level by 0.35 dB and the accented by
0.2 dB at TOTAL ACCENT 0. A unit's RM0621 tolerances (unknown: no data sheet) and its +5 V (the
7805: ±4 %, which scales every code) add to it. **Recommendation:** the board computes `acc` with the formula above (r_o 455 Ω) and the
netlists use `rdac` = 2.69 kΩ; 5 V · code / 63 and 2.54 kΩ are the ideal-latch case.

**Where each DAC goes** (traced on SN p.11; the voice by the firmware's address order, 4001-4008
in IC2-IC9 order):

| Latch / array | Address | Voice(s) | Load on the DAC node | Effect |
|---|---|---|---|---|
| IC2 / RA1 | 4001 | BD | IC11b (+) (×3.13 buffer, R37 10K / R38 4.7K) | none (op-amp input) |
| IC3 / RA2 | 4002 | SD | IC34 (+) | none |
| IC4 / RA3 | 4003 | LT | IC18 (+) | none |
| IC5 / RA4 | 4004 | MT | IC23 (+) | none |
| IC6 / RA5 | 4005 | HT | IC45 (+) | none |
| **IC7 / RA6** | 4006 | **RS and HC** | IC48b (+) (RS) and IC26a (+) (HC) | none |
| **IC8 / RA7** | 4007 | **HH** (CH and OH) | **R481 4.7K** into Q84's (2SA798) base node, which has R489 150 Ω to +5 V and R488 2.2K from RUN/STOP (IC72a; `docs/circuit/hh.md`, which corrects an earlier reading of this row: ground, and the sample DAC) | heavy: the node at R481 is ≈ 0.64 V_oc (4.85k against R_th) |
| IC9 / RA8 | 4008 | **crash and ride** | **R426 4.7K** (crash, into Q70's base, R428 220 Ω, R427 2.2K) and **R435 4.7K** (ride, Q68, R433 220 Ω, R437 2.2K), in parallel | heavy: two 4.9k loads, ≈ 0.48 V_oc |

- The RS/HC and HH rows are swapped against RES A2.2's table (IC7 HH, IC8 RS and CP, from the block
  diagram) and `docs/circuit/rshc.md` (IC8 for RS/HC). On p.11 RA6's node runs right and up to the
  "ACC" line at the rim shot (grid J-6) and on to IC26a, and RA7's node runs down to the "ACC" line
  at R481 (grid B-7). Electrically it changes nothing (the firmware writes RS/HC at 4006 and HH at
  4007 either way); only the IC numbers in the docs.
- RES A2.2 also put R496 100K on the clap's accent input as a 2.5 % load: R496 runs from IC26a's
  (−) input to −15 V (SN p.11 grid J-12, and NAVA), so it loads nothing here.
- **HH and cymbals:** their netlists need the Thevenin source (and for crash and ride each other's
  load), as `docs/circuit.md` says; with r_o the source is 2.69 kΩ, and the node voltage changes by
  under 0.5 % against the ideal-latch model (V_oc and R_th both rise).
- Two voices on one DAC take the last write: if RS and HC (or crash and ride) are triggered in the
  same tick with different levels (only over MIDI), both get the later one (RES C.4).
- At power-on and power-off the RESET line clears every latch (`power.md`): triggers low, all DACs at
  code 0 (0 V).

## 3. TOTAL ACCENT

- **VR1 100K(B)** on the voicing board (SN p.11 grid S-17; parts list: 100K(B),
  EVJFDAF30B15, a linear-taper pot). Its ends go to CN3 "ACC +5V" and GND, its wiper "ACC LEVEL" to
  CN3 and over F1-2 to the switch board.
- On the switch board (SN p.8 row D, left of IC604): **"ACC +5V" is the CPU's A/D reference**: AVcc (pin 43,
  +5 V) through **R617 100Ω** to Vref (pin 42), **C606 10µ/16** to ground, and on to F1-3. The wiper
  goes to **AN3** (pin 37) with **C604 0.01µ** to AVss. (TEMPO VR601 50K(B) on AN0 has R616 22K and
  C605 0.01µ in the same way.)
- So the A/D reads VR1 ratiometrically: code = 256 · rotation (minus the pot's end resistances),
  independent of the +5 V's exact value. The wiper's source resistance (25 kΩ at most, at mid
  rotation) and C604 make a 0.25 ms low-pass: no effect at the firmware's read rate.
- The firmware uses TA = code >> 3 (0-31) on Total Accent steps (RES C.4): TA is 0 below 3 % and 31
  above 97 % of the rotation. The flat ends AudioRealism's sweep shows (0-10 % and 90-100 %, RES
  A2.2) are wider: a carbon pot's end pads (a few per cent of the travel at each end) and the
  pack's knob positions would make up the rest.
- **Model:** AN3 = Vref · rotation into the emulated µPD7811's A/D; no circuit needed.
