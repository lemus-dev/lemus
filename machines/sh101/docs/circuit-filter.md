# Roland SH-101 filter section: circuit transcription

This file transcribes the SH-101's synth board and control board from the source mixer to the
OUTPUT jack. That covers the IR3109 VCF and its resonance loop, the cutoff CV summer, the ENV
generator with its trigger modes, the BA662A VCA and the output stage. The oscillator section
(pitch path, VCO, sub-oscillator, noise, mixer sliders, modulator and bender board) is in
`circuit-osc.md`. Designators are Roland's: the synth board (OPH177-2), the control board
(OPH177-1), the bender board (OPH178-1) and jack board A (OPH178-2) each number their own parts,
so "bender R15" and similar names are used where two boards share a number.

Each number is labelled with how it was obtained:

- **Read**: taken directly from a drawing, with a citation.
- **COMPUTED**: derived by me, with the arithmetic shown.
- **CORRECTED**: the drawing is wrong, or `research.md` read it wrongly; the reason is given.
- **UNCERTAIN**: illegible, conflicting, or an inference about how the circuit works.

---

## 0. Sources and conventions

| Key | Document | Local file(s) |
|---|---|---|
| **SN** | Roland *SH-101 Service Notes*, First Edition (Nov. 1st 1982). p.7 of the 400 dpi scan is the circuit diagram (p.9 of the 1982 printing); p.5 has the bender board, jack board A and the output | `private/docs/schematic/alt-dinsync-400dpi/sn400-p-{5,7}.png`, the 1982 scan `private/docs/schematic/sn-p-9.png` |
| **Crops** | Zoomed crops of SN, by block | `private/docs/schematic/crops/sn400-p7-vcf.png`, `-control-vcf.png`, `-env.png`, `-vca.png`, `-control-env-vca-mode.png`, `-power-supply.png`, `sn400-p5-output-volume.png`, `-jack-board-a.png`, `-bender-board.png`, and the 1982 crops `sn-p9-*.png` |
| **OML** | Open Music Labs, "SH-101 schematic errors" (B. Stamas, for the SB01), and their IR3109/BA662 reverse engineering | `private/docs/papers/ota_ir3109/openmusiclabs_wiki_sh101_schematic_errors.txt`, `img/openmusiclabs_sh101_fixed_schematic_crop_vcf_vca.png`, `private/docs/datasheets/ir3109-*-openmusiclabs.*` |
| **ED** | Electric Druid, *AS3109 SH-101 Filter* Rev. 2 (2025), a redraw of the VCF | `private/docs/papers/ota_ir3109/img/electricdruid_2025_sh101_filter_rev2_pg{1,2}_*.jpg` |
| **DS** | Data sheets: AS3109 (Alfa), IR3109 (Roland, 1982 excerpt), IR9022 (Sharp), BA6110 (Rohm), AS662 (Alfa), M5218 (Mitsubishi) | `private/docs/datasheets/` |
| **BOM** | sibomots/RolandSH101 parts list (transistor types; its values have slips) | `private/docs/papers/opensource/diy_sibomots_rolandsh101/bom/` |
| **R** | `docs/research.md` (the research digest) | |

**Supply and reference symbols on SN p.7.** I matched these against the labelled connector pins:
the main board's connector to the bender board (pin 19 "+15V" with the half-filled arrow, pin 21
"+14V" with the filled one) and jack board A ("+9V" pin 41, "−5V" pin 40). In the power supply
(crop `sn400-p7-power-supply.png`) the half-filled arrow sits on IC1b's output and the filled one
on TR3's emitter.

| Symbol | Meaning |
|---|---|
| Filled arrow ▲ | **+14 V**: TR3's emitter, a V_BE below IC1b's output, the rail that carries the load (14.3 V measured by U-Labor, `private/docs/photos/pcb/ulabor_power_supply_voltages.png`). CORRECTED (2026-11-13): read as +15 V before |
| Half-filled arrow (left half black) | **+15 V**: IC1b's output, 3 × the bus through R5/R6 (14.87 V measured by Synhouse at the CEM3340's pin 16, R §8). In this section only the top of the FREQ slider and R187 (§4.1). CORRECTED (2026-11-13): read as +14 V before |
| Open chevron ↑ | **+9 V** (battery or adapter). It feeds the IR3109's pin 16 and the M5218 output op-amp |
| Open triangle ▽ | **−5 V** |
| Short stub ending in a dot | The **+5 V reference bus** (IC2a's output, trimmed by VR1 D/A WIDTH). Audio and most CVs swing about it |
| Hatched stub | Ground |

**Transistor types** (BOM, matching SN's legend "OTHER NPN TR: 2SC1815GR, ALL PNP TR: 2SA1015GR"):
NPN TR26, TR27, TR29, TR31, TR32, TR35, TR37; PNP TR28, TR30, TR33, TR34, TR36, TR42.

**Pots** are named by travel, 0 at the panel's bottom (the slider down). Taper letters are
Japanese: A = audio (log), B = linear. All calculations use V_T = 25.69 mV (25 °C).

---

## 1. Rails and reference used by this section

| Rail | Use here | Value used |
|---|---|---|
| +15 V | VR17 FREQ top, R187 | 14.87 V (measured, R §8) |
| +14 V | IC16, IC18 (IR9022) supply; the ENV's switches (TR30, TR33, TR34) and TR37's collector; R158, R161, R145, R153, R154; R195; R196; R135 (TR26/TR27); the BA662's pin 9 and C55 | 14.3 V (measured by U-Labor). The Goob's unit read 14.81 V on R158's top with the bus at 5.03 V (§5.5), which fits neither TR3's emitter (≈14.4 V) nor IC1b (15.09 V) |
| +9 V | IR3109 pin 16; IC1 (M5218) | 9.0 V |
| +5 V bus | IR3109 pin 1 and the 560 Ω returns; IC18b's +input; the VCA's bias network; VOLUME's bottom | 5.000 V (VR1 trims it; the D/A wants 4.93-5.0 V) |

---

## 2. IR3109 VCF (IC14) and its input (crop `sn400-p7-vcf.png`; OML; ED)

### 2.1 The chip

Pins (Roland's 1982 data sheet): 1 GND (reference), 2 IN1, 3 CAP1, 4 OUT1, 5 IN2, 6 CAP2, 7 OUT2,
8 −V_EE (substrate), 9 V_CONT, 10 IN3, 11 CAP3, 12 OUT3, 13 IN4, 14 CAP4, 15 OUT4, 16 +V_CC.
There are four transconductance amplifiers ("1 µ℧ to 10 m℧"), four MOS P-channel high-impedance
buffers, and an "exponential function generator" shared by the four.

The SH-101 wires it as follows (Read, SN p.7):

- Pin 16 to **+9 V**, decoupled by C49 10/16; pin 8 to ground.
- **Pin 1 to the +5 V bus.** The capacitors return to pin 1 (ED Rev. 2 agrees: "C1-C4 to +5V,
  not Gnd").
- Pin 9 is fed from the cutoff CV divider (§4.4).

Inside, from OML's reverse engineering (R §4.1; the schematic and sketches in
`private/docs/papers/ota_ir3109/img/openmusiclabs_2014_*`), Roland's 1982 data-sheet excerpt and
the AS3109 data sheet (Fig. 2). There is no maker's circuit, so this is UNCERTAIN in detail. A
transistor-level subcircuit built from it is in `tools/spice/filter/ir3109` (`model-filter.md`
7.1).

- Each stage is a bare differential-pair OTA with no linearising diodes (BA662-style, Wilson
  mirrors). Its inverting input is the IN pin; the non-inverting input is internal, to pin 1
  through ≈560 Ω (AS3109: "Rin in each amplifier is approximately 560 Ω"). The output current
  I_ABC·tanh((V_IN − V_1)/2V_T) is taken from CAP. The stage inverts.
- The buffers are P-MOS source followers: OUT = CAP + |V_GS|. OML measured |V_GS| ≈2.9–3 V on the
  IR3109; drain to pin 8. Stages 1/3 have 20k pull-ups to pin 16; stages 2/4 have current sources
  set by a 22k reference (OML), i.e. ≈0.38 mA from the SH-101's 9 V. ("1 mA" at the ±15 V the
  AS3109 data sheet uses.) AS3109: "Stages 1 and 3 have passive resistor type load, but stages 2
  and 4 have active current source load".
  - COMPUTED: in the SH-101 the 20k pull-up carries 200 µA at rest. Against the two 68k loads,
    stages 1 and 3 top out at +2.52 V above the bus, where the follower cuts off.
  - COMPUTED: a follower's gain is g_m/(g_m + G_load), so 0.89 on stages 1/3 and 0.97 on 2/4 for a
    P-MOS with K'W/L 1 mA/V². The decode gives no K; this one matches the measured |V_GS| with
    |V_TO| 2.3 V.
- The OTA outputs stop softly a V_BE from either rail, not at a fixed compliance (COMPUTED from the
  decode at transistor level, `ir3109t.lib`, 2026-11-14). The current is I_C2, which a PNP Wilson
  sources into CAP, less I_C1, which a PNP and an NPN Wilson sink from it. Once CAP comes within a
  V_BE of pin 8, the NPN Wilson's output transistor saturates, and its base-collector junction turns
  the mirror's current round into CAP: the sink gives I_C1·tanh((CAP − pin 8 − V_BE(I_C1))/2V_T),
  within a few mV from 0.3 to 210 µA (I_S 3.67·10⁻¹⁶ A at 25 °C). The PNP Wilson does the same
  towards pin 16 with V_EB(I_C2). So the floor drops the harder a stage is driven: CAP about
  −4.32 V against the bus at 230 µA with no drive, −4.18 to −4.29 V in use, −4.50 V at 0.24 µA.
  The outputs' floor is −1.0 to −1.2 V. On a real chip its level moves with the chip's I_S
  (about ±26 mV per factor e), which is unknown.
- The expo (OML's sketch): a temperature-compensated ≈3 µA reference (1.2 V zener, 20k, 470k) is
  held through one transistor of an NPN pair by an op-amp, which drives the joined emitters through
  1.6k. Pin 9 is the other transistor's base, with no internal divider; the AS3109 states a 30 nA
  input current, so pin 9 doesn't load R125. One mirror feeds the four OTAs. More negative pin 9
  (against pin 1) gives more current.
  - I = I_ref·exp(−V9/V_T). At transistor level (generic devices): I(V9 = 0) = 2.96 µA, and
    17.9 / 19.0 / 20.2 mV/oct at 27 / 45 / 65 °C, the AS3109's 0.33 %/°C.
  - AS3109 data sheet: −19 mV/oct (−17.5 to −20.5) at 25 °C ambient, 15 octaves over
    −140…+160 mV, "250 Hz at V_C = 0" per stage (68k/560/240 pF, i.e. 2.4 µA).
  - OML measured ≈20.8 mV/oct (temperature not stated), with a rising linearity error above ≈10 µA
    (+10 % at 0.5 mA) and saturation near 700 µA. Generic devices give the opposite sign (−10 % at
    0.5 mA, from emitter resistance); the measured error stays in the model.

### 2.2 The stage networks (Read, SN p.7; ED agrees)

| Role | Parts |
|---|---|
| Local feedback, OUT → own IN, 68k | R114 (4→2), R118 (7→5), R121 (12→10), R122 (15→13) |
| Interstage, OUT → next IN, 68k | R117 (4→5), R119 (7→10), R124 (12→13) |
| IN to the 560 Ω return, which goes to the bus, decoupled by C46 2.2/50 | R115 (2), R116 (5), R120 (10), R123 (13) |
| Integrating capacitors to pin 1, 240 pF | C48 (3), C47 (6), C50 (11), C51 (14) |
| Pin 9 to the bus | R125 820 Ω |

COMPUTED. Each stage-2…4 input node sums the previous output and the stage's own output through
68k each, with 560 Ω to the bus. R_p = 68k ∥ 68k ∥ 560 = 551 Ω, so the OTA sees
u_i = R_p(V_{i−1} + V_i)/68k (voltages against the bus). The stage's small-signal pole is
ω = I_ABC·R_p/(2V_T·68k·C), which works out to **104.5 Hz/µA** with C = 240 pF. At DC each stage
inverts with unity gain, so the four in series are non-inverting.

### 2.3 Stage 1's input: the source mixer and C45 (Read, SN p.7)

The four mixing resistors meet at one node: R111 470k (saw), R110 680k (pulse), R112 820k (sub)
and R109 150k (noise), each from its level slider. **C45 10/16 BP** couples that node into pin 2.
Pin 2 also has R114 68k (local feedback), R113 15k (the resonance injection, §3) and R115 560.
COMPUTED: the gain from a source to OUT1 is −68k/R (saw 0.145, pulse 0.100, sub 0.083,
noise 0.453). C45 into R_mix + R_p1 puts the coupling corner at 0.02-0.2 Hz, depending on the
sliders.

---

## 3. Resonance loop and output sum (crop `sn400-p7-vcf.png`; OML fixed crop; ED)

### 3.1 Parts (Read, SN p.7, plus OML)

- **VR18 RES 100k B** on the control board. Top: OUT4 via connector pin 75. Wiper: pin 76 to
  TR26's base. Bottom: pin 77, which is the bus on the synth board. CORRECTED: the control board's
  drawing returns VR18's bottom to ground; OML: "VR18 tied to +5V, not GND". With the bottom on
  the bus, the wiper's DC stays at 5 V and TR26's bias doesn't move with RES.
- **TR26** (2SC1815): emitter R131 10k to ground; collector R136 15k to the C54 node (R135 1k from
  +14 V, C54 10/16 to ground).
- **TR27** (2SC1815): an emitter follower off TR26's collector, collector on the C54 node, emitter
  **R137 33k** to ground. R137 is unlabelled on SN; the value is from OML.
- From TR27's emitter: **C52 10/16** (+ toward TR27) → **R129 5.6k** → the diode node → **R113 15k**
  → pin 2. **D30, D31** (1S2473) are anti-parallel from the diode node to the bus.
- Output sum: **R130 150k** from OUT4 and **R132 150k** from TR26's emitter meet at one node →
  **C53 10/16 BP** → IC15 pin 3, with **R139 1k** to the bus.

### 3.2 Operating point and gains (COMPUTED; checked in SPICE, `tools/spice/filter/vcf.cir`)

- TR26: base 5.00 V, emitter 4.38 V, so I_E = 0.438 mA. C54 node 13.67 V; TR26 collector 7.1 V;
  TR27 emitter 6.5 V (0.196 mA in R137). SPICE gives 4.40 / 7.07 / 6.50 / 13.66 V.
- The emitter's AC load is R131 ∥ R132, not R131 alone. R132's far end is nearly AC ground
  (1/152 of the sum), so the collector gain is
  15k·α_F·(1/10k + 1/150k)·0.994 (follower) ·0.996 (TR27's base loading) = **1.58**.
  SPICE gives 1.575. R §4.2 has 1.49 because it misses R132.
- TR27's follower gain into its load (R137 ∥ the C52 path) is 0.990.
- Diode node: d = 0.728·e (15k/(5.6k + 15k)) while the diodes are off. Then
  y1 = −(68k/15k)·d = −4.53·d.
- Loop gain at full RES: 1.58 × 0.990 × 0.728 × 4.53 × 0.994 = **5.1**. Four equal poles
  oscillate at a loop gain of 4, so **self-oscillation starts at VR18 ≈ 0.79**. SPICE gives
  0.787; R estimates 0.81; Roland's plug-in about 8.2 (reference measurements).
- C52's coupling corner: 1/(2π·10 µF·20.6k) = 0.77 Hz.
- TR26 saturates when its collector falls to 0.15 V above its emitter, i.e. for a wiper swing
  above about +1.0 V (+1.3 V with R135 on +15 V). TR27 cuts off when its emitter can't sink the
  load current through R137 (about −1.5 V of swing with the diodes clamping). Both are beyond self-oscillation's swing (OUT4
  ≈ ±0.5 V) and are reached only with large inputs at high RES.
- Output sum (COMPUTED): pin 3 = (OUT4 + TR26's emitter)/(2 + 150k/1k) = (1 + α)·OUT4/152, where
  α is the wiper's share. This is the output-side "Q make-up". The passband still falls with RES:
  (1 + α)/(1 + 5.1α) gives −8.8 dB at α = 0.75 against α = 0 (OUT4 alone: −13.7 dB). SPICE
  gives −8.85 dB and −13.66 dB.

---

## 4. Cutoff CV summer (control board; crops `sn400-p7-control-vcf.png`, `sn400-p7-vcf.png`)

### 4.1 IC18b and its inputs (Read, SN p.7)

IC18b (IR9022, on +14 V and ground) is an inverting summer about the bus: +input (pin 5) on the
bus, R192 33k from its output to pin 6. Inputs to pin 6:

| Input | Path | Notes |
|---|---|---|
| FREQ | **VR17 100k B** from **+15 V** to ground, wiper → **R186 200k** | CORRECTED: R §4.2 and R's FREQ span assume 0-5 V. VR17's top carries the half-filled arrow, which is +15 V (§0) |
| Fixed offset | **R187 680k** from the same +15 V | |
| ENV | **VR19 100k B** from the ENV line to the bus, wiper → **R189 100k** | The ENV line is IC16b's output (connector pin 85) |
| MOD | **VR20 100k A** from IC18a's output to the bus, wiper → **R191 100k** | IC18a is a follower of the MODULATOR wave; R188 47k from its +input to the bus |
| KYBD | post-glide KCV → **VR8 47k B "VCF WIDTH"** (rheostat, synth board) → pin 79 → **VR21 100k B** to ground; wiper → **TR42** (PNP follower; emitter **R195 33k** to +14 V, collector to ground) → **R193 150k** | R195 is unlabelled on SN; the value is from OML. CORRECTED (OML): VR8 hangs on the KCV line (R104), not on R91 |
| VCF BEND | bender board VR5 (100k B) wiper, connector 70 → **R185 100k** | |
| VCF LFO | bender board VR6 (LFO MOD, 100k A) wiper, connector 71 → **R184 220k** | the grip's LFO MOD |

### 4.2 KYBD's offset (COMPUTED)

TR42's emitter sits a V_EB (≈0.6 V) above its base even with KYBD at 0, so R193 always draws
(0.6 − 5)/150k ≈ −29 µA from the summing node. That is a fixed +0.97 V at IC18b's output. It is
part of the design: the FREQ range below already includes it.

### 4.3 Laws (COMPUTED from the parts; checked in SPICE, `tools/spice/filter/cv.cir`, within 7 cents)

At IC18b's output (against the bus), V18 = −33k·Σ(i):

- FREQ, slider 0 → 10, with R187 and KYBD at 0: V18 from +1.31 V to −1.14 V (2.45 V).
- ENV at VR19 10: the ENV's swing (5.11 V above the bus at its peak) × 33k/100k = −1.69 V.
- KYBD at 10, after VR8 (§4.5): −0.0244·r8 V per volt of KCV, where r8 = 100k/(100k + VR8).
- IR9022's output stays 2 V from either rail, so V18 is held between −3 V and +7.3 V.

### 4.4 The pin-9 divider (Read, SN p.7)

V18 → **R128 4.7k** → [**R127 5.6k** ∥ (**R126 1.8k** + **TH1 SDT1000**)] → pin 9, with
**R125 820 Ω** from pin 9 to the bus. TH1 is the tempco thermistor; 1 kΩ is assumed at 25 °C (ED
labels it 1k). COMPUTED ratio: 820/(4.7k + 1.867k + 820) = **0.111** (0.088 with a 10 kΩ TH1).

COMPUTED cutoff ranges at 19 mV/oct (the data sheet's), with KYBD at 0:

- FREQ alone moves pin 9 from **+146 mV to −126 mV**, 14.3 octaves (SPICE: within 1 cent). With
  the data sheet's 250 Hz per stage at 0 mV, that is 1.2 Hz to 25 kHz pole frequency before the
  expo compresses, roughly exponential along the travel. FREQ's top on +15 V matters here: with
  5 V there, the span would be 4.8 octaves (R) and the top end about 300 Hz; with 14.0 V (as read
  before 2026-11-13) it was 13.4 octaves, +150 to −106 mV. Two weak in-circuit readings: pin 9 at
  −100 mV with FREQ at the top (Jackdamery, R §4.2), and an AS3109 retrimmed to an original
  IR3109 spanning +108 to −120 mV (emuarc; `online-references.md` §2). Roland's plug-in spans
  12.2 octaves (3.4 Hz–15.6 kHz).
- ENV at 10 adds −187 mV, i.e. **9.9 octaves**. Real-unit (7.0–7.3 oct) and Roland (7.65 oct)
  measurements are lower. With VR8 trimmed for 1 oct/oct (§4.5), the schematic can't give less
  than ≈7.7 octaves: 1.5 (R193/R189) × 5.11 V of ENV swing per volt of KCV, for VR8 at 0 (8.1
  with the swing of 5.42 V the ENV had on a +15 V rail).
- KYBD at 10 untrimmed gives 1.29 oct/V. VR8 trims it to 1.000 (§4.5).
- VCF BEND at 10: the lever's swing (`circuit-osc.md`) × 0.33 × 0.111 at pin 9. VCF LFO via R184:
  × 0.15 × 0.111.

### 4.5 VR8 VCF WIDTH (SN p.6 "VCF ADJUSTMENT", transcription line 376)

"Hold the A4 key down, and set the CUTOFF FREQ. for approximately 1 kHz. Alternately, play the F4
and F5 keys, and adjust VR-8 (VCF WIDTH) until [F5's period is half of F4's]" (Fig. 2: RES at
maximum, mixer at 0, KYBD at maximum). COMPUTED: the trim needs r8 ≈ 0.778, so VR8 ≈ 28.6 kΩ,
inside its 0-47k. The model's own trim with that procedure gives 28.58 kΩ (`model-filter.md`).

---

## 5. ENV GEN (crops `sn400-p7-env.png`, `sn-p9-env.png`, `sn400-p7-control-env-vca-mode.png`)

### 5.1 It is discrete

IC16 is an **IR9022** (Sharp low-power dual op-amp: 130 µA supply, ±6 mA output short-circuit
current, 0.5 V/µs slew, output 2 V from the rails). The ENV is built from IC16, TR29–TR37 and
**C59 4.7/16**. There is no IR3R01 (R §5.1 agrees).

### 5.2 Connector pins 83–92, control board ↔ synth board (Read; the two boards' pins line up)

| Pin | Synth board | Control board |
|---|---|---|
| 83 | the GATE level (TR30, R142, R41, C57) | S6's GATE contact |
| 84 | R147 220k → TR32's base | S7 deck 2's common |
| 85 | IC16b's output (ENV) | R196 120k from +14 V and R197 3.3k → S6's ENV contact; VR19's top (ENV amount) |
| 86 | R141 270 → TR34 | VR22 ATTACK (1M A) as a rheostat |
| 87 | R148 220 → TR31 | VR25 RELEASE (1M A) as a rheostat |
| 88 | C59 and IC16b's +input | the common end of VR22, VR23, VR25 |
| 89 | bus | VR24 SUSTAIN's bottom |
| 90 | IC16a's −input; R158 82k from +14 V | VR24's top |
| 91 | TR37's base | VR24's wiper |
| 92 | R164 150 → TR35 | VR23 DECAY (1M A) as a rheostat |

### 5.3 The circuit (Read, SN p.7)

- **Gate in:** the CPU's GATE (0/+12 V) → R151 100k → **TR29**'s base (R150 33k to ground).
  TR29's collector, node G: R144 100k to TR30's base (R145 10k to +14 V); R149 220k to TR32's base.
- **GATE level for the VCA (TR30, PNP):** emitter on +14 V; collector → R142 12k → pin 83, with
  **R41 33k** to the bus (C56 10/16 decouples the bus there) and **C57 0.047** to ground. OML
  notes this R41 is R143 on the board; Din Sync, that the board's R143 is the drawn R142 and its
  R142 the drawn R41. The values stay 12k in series and 33k to the bus. COMPUTED: 11.8 V with the gate on (R142/R41 from 14.29 V
  and 5 V; τ 0.41 ms), 5 V with it off (τ 1.55 ms).
- **Reset, TR32 (NPN):** base from G through R149 (on while the gate is off) and from pin 84
  through R147. Collector, node K → **R152 100k** → TR33's base (R153 10k to +14 V).
  **TR33** (PNP, emitter +14 V) → node P: R146 100k to the bus; **D32** to node Q; **R157 47k** to
  TR31's base. **D33**: anode on IC16a's +input node (M3), cathode on K.
- **Release, TR31 (NPN):** collector → R148 220 → pin 87 → RELEASE → C59. Emitter: **R163 47** to
  the bus and **C58 10/16** to ground.
- **Attack, TR34 (PNP):** emitter on +14 V; base, node S: R154 10k to +14 V, R155 10k from Q.
  Q: R156 47k from IC16a's output, and D32 from P. Collector → R141 270 → pin 86 → ATTACK → C59.
- **End of attack, IC16a:** −input = pin 90 (R158 82k from +14 V, SUSTAIN's 100k to the bus);
  +input = node M3. N: **R161 33k** from +14 V, **R160 47k** from IC16a's output (labelled "+7k"
  on SN; OML: 47k), **D34** from N (anode) to M3 (cathode).
- **Decay, TR35 (NPN):** base R159 33k from IC16a's output, R165 10k base to emitter. Emitter,
  node T, is held by **TR36** (PNP: emitter T, collector ground). TR36's base is **TR37**'s emitter
  (NPN follower: collector +14 V, base SUSTAIN's wiper), with R166 33k to ground. Collector →
  R164 150 → pin 92 → DECAY → C59. TR37/TR36 is a complementary follower: T ≈ SUSTAIN with the
  two V_BEs cancelling, and it can only sink.
- **C59 4.7/16** from pin 88 to ground; **IC16b** follows it (pin 5 = C59, pins 6/7 tied) to pin 85.

### 5.4 The missing R162 (CORRECTED; checked in SPICE)

OML lists "Missing components: R162 – 100k" but doesn't say where it goes. As drawn, with M3
wired straight to IC16b's output, the circuit can't work:

1. Whenever TR32 is on (gate off), D33 would pull the ENV output down to ≈0.8 V. IC16b's output
   is limited to ±6 mA; TR32 can sink 9–17 mA. That would kill the release on the VCA and VCF.
2. Nothing could latch IC16a high: D34 can't lift a node that an op-amp drives. The ENV would hang
   at the threshold for as long as the gate is on, and DECAY and SUSTAIN would do nothing.

With **R162 100k between IC16b's output and node M3** (IC16a's +input, D33's anode and D34's
cathode), both problems go away (COMPUTED):

- With IC16a low, N's Thevenin source is 8.6 V through 19.4k (R161 from +14 V). For ENV below
  about 8.0 V, D34 lifts M3 to 0.16·ENV + 6.7 V, still below the threshold. Above 8.0 V, D34 is
  off and M3 = ENV, so IC16a trips at ENV = threshold.
- With IC16a high (12.3 V), N's source is 13.5 V. D34 holds M3 at 0.16·ENV + 10.8 V, above the
  threshold for any ENV. The decay can then run without un-tripping IC16a.
- TR32 resets it: D33 pulls M3, not the ENV output, to ≈0.7 V.

The transistor-level SPICE netlist with this placement (`tools/spice/filter/env.cir`) runs
attack, latch, decay, sustain and release as the owner's manual describes. The model matches it
within 1–18 mV RMS (`model-filter.md`). The board confirms it: the service notes' own
circuit-board diagram draws R162 beside IC16, between IC16b's side and D33, and Din Sync's BOM
(from a dismantled unit) fits it as 100k (`online-references.md` §5).

### 5.5 Levels and times (COMPUTED)

- **Threshold** (peak): 5 + (14.3 − 5)·100k/(82k + 100k) = **10.11 V** (R158 on +14 V; 10.42 V as
  read before 2026-11-13, on +15 V). It is ≈40 mV lower with SUSTAIN at 10, because TR37's base
  current loads the divider. One unit's meter readings (the Goob, ModWiggler 2016; S max, gate
  on): ENV 10.29 V, the threshold node 10.26 V, the bus 5.03 V and R158's top 14.81 V. Its
  threshold sits 0.541 of the way from the bus to R158's top, as the divider gives (0.549, less a
  10 MΩ meter's loading); its rail is 0.5 V above U-Labor's 14.3 V, and so its peak (5.26 V above
  the bus) 0.15 V above ours.
- **SUSTAIN**: from 5 V (S 0) to the threshold (S 10), linear (VR24 B).
- **Rest**: 5.015 V, a little above the bus: TR31's base current in R163 plus its saturation
  voltage (SPICE).
- **Attack**: C59 charges toward +14 V through 270 Ω + ATTACK and ends at the threshold:
  t = 0.80·τ. At ATTACK 10, τ = 4.7 s, so **3.7 s**. Spec 4 s; Reid measured ≈3 s on one unit,
  i.e. a pot about 20 % low. At 0, 1.3-1.8 ms (spec 1.5 ms), depending on the pot's residual
  resistance.
- **Decay** toward SUSTAIN through 150 Ω + DECAY; **release** toward the bus through
  220 Ω + RELEASE + R163. τ_max = 4.7 s; 90 % of the way takes 10.8 s (spec 10 s). τ_min ≈ 1-2 ms
  (spec 2 ms).
- The ENV's swing is 5.11 V above the bus. AMSynths' replica scales it to 0-10 V "by 1.25" on a
  12 V supply (R §5.1).

### 5.6 S7, deck 2 (pin 84) and the TRIG circuit (Read, SN p.7; crop `cb_conn`, `trig`)

S7 (GATE+TRIG / GATE / LFO) has two decks. Deck 1 is the CPU's switch reading (`firmware.md`).
Deck 2's common is pin 84, which drives TR32 through R147:

- **GATE+TRIG:** the TRIG circuit's output. IC4b buffers a CV, which IC5 then detects:
  C13 4.7 nF into R21 68k differentiates it, IC5b amplifies ×(1 + 470k/15k) with C14 150 pF across
  R23, and D11/D12 with IC5a form a window comparator. UNCERTAIN: IC4b's input, which I take to be
  the KCV sample-and-hold (normalled to CV IN), and IC5a's exact feedback. COMPUTED pulse: a
  semitone step gives ≈0.5 ms, an octave ≈1.3 ms. Each new pitch therefore retriggers the
  envelope from its current level. The firmware fork found that the CPU makes no gate gap for
  legato notes, so this is the only source of the retrigger.
- **GATE:** open. Legato notes don't retrigger.
- **LFO:** a line from the MODULATOR's square. UNCERTAIN: its polarity. The model releases in the
  square's low half and attacks in the high half, which starts at CLK RST, so the first
  envelope begins at key-down.

### 5.7 The VCA's control (S6) — CORRECTED against R §6.1

S6 (VCA ENV/GATE, control board): **the ENV contact goes through R197 3.3k to the ENV line**
(pin 85). The GATE contact goes to pin 83. The common goes to pin 80 → **R133 12k** → TR28. R §6.1
has the common on R197 instead.

CORRECTED: **R196 120k from +14 V sits on S6's ENV contact, not on the ENV line.** The circuit
diagram (and OML's corrected one) draw it at the ENV line's junction; the service notes' own board
diagram (1982 scan, control board) has the traces S6 ENV pad → R196's pad → R197's pad, and R197's
other pad → pin 85. So in ENV mode S6's contact sits 9.3 V × 3.3k/123.3k = 0.25 V above the ENV at
rest, and TR28 opens that much lower: I_ABC is 40 dB under its peak at bus + 0.28 V, against
+ 0.52 V as drawn, and the peak is 304 µA (drawn 295). GATE mode is unchanged. This is what a real unit's
release shows (`model.md`, "The VCA's turn-on").

---

## 6. VCA: IC15 BA662A (crops `sn400-p7-vca.png`; OML)

- **TR28 (2SA1015, PNP) in common base:** base on the bus, emitter from R133, collector into
  pin 1 (I_ABC). COMPUTED: I_ABC = α(V_ctl − 5 − V_EB)/R. ENV mode: R = 3.3k + 12k, 0.30 mA at
  the ENV's peak; off below ≈5.3 V. GATE mode: 11.8 V through R142∥R41 (8.8k) + R133:
  **0.30 mA, the same as the ENV's peak**, by design (both on +14 V).
- **BA662A:** pin 2 R138 1k to the bus; pin 3 from C53 with R139 1k to the bus; pin 4 (buffer bias)
  on the bus; pin 5 ground; pin 6 = pin 7 with **R140 47k** to the bus; pin 8 the buffer's output
  to VOLUME; pin 9 +14 V with C55 22/16. The chip is the TB-303's (its `circuit.md` §6.1 and
  `ba662.lib`): no linearising diodes, g_m ≈ 0.9·I/2V_T. "A" means offset selected (0.03-0.25 mV).
- **Unpopulated:** the board has dashed positions for **VR32 "VCA OFFSET"** and **R201** next to
  IC15 (SN p.6). Forum posts give 100k and about 5 MΩ for them: a trim of the OTA's DC offset,
  i.e. the thump, which Roland left out ("offset selected" BA662A instead). They don't move the
  VCA's turn-on.
- The pair's base current loads pin 3 (`ba662.lib`, ngspice): with R138 in series its input
  resistance is 4βV_T/I_ABC + 1k, β ≈ 220 (BF 200 and the Early effect at the pair's V_CE), so
  pin 3's 987 Ω source (R139 ∥ 75k) loses 1.3 % (0.11 dB) at the ENV's peak, 0.2 % at 45 µA.
- COMPUTED gain from OUT4 to pin 8 at the ENV's peak:
  (1 + α)/152 × 0.985 × 0.30 mA/51.4 mV × 47k = **1.80(1 + α)**, before the input losses.

---

## 7. Output stage (crops `sn400-p5-output-volume.png`, `sn400-p5-jack-board-a.png`)

- **VOLUME VR1 100k (A)** (bender board) from pin 8 (connector 111 → bender 5) to the bus (bender
  pin 4, "+5V", C1 2.2/50). Wiper → **IC1a** (M5218, on +9 V) follower, and IC1b for PHONES.
- CORRECTED against R §8.1: the **OUT** line leaves IC1a through **C2 2.2/50**, not C3. C3 100/16
  (from IC1a) and C4 100/16 (from IC1b) feed the two PHONES lines.
- **Jack board A:** **R30 100k** to ground, **R29 2.2k** in series, **C12 0.001** to ground,
  **R28 1k** in series to J1 OUT. TR7 (R35 47k) shunts the R29/R28 node at power-on (mute):
  - C13 10/16 from +9 V to node A; R36 220k and D3 (anode on ground) from node A to ground; D4 from
    node A to node B; R35 47k from node B to TR7's base; TR7's emitter on ground.
  - TR8 (emitter on C14 47/25, which D5 charges from +9 V; base through R37 10k to +9 V; collector
    on node B) is the power-off mute: it dumps C14 into TR7's base when +9 V falls.
  - Switched on, TR7's base-collector junction, D4 and D3 (or R36) also conduct when the node goes
    below about −1 V (`model-filter.md` 5).
- COMPUTED, into a 47k load: C2 gives a high-pass at 2.2 Hz (0.7 Hz unloaded); C12 a low-pass at
  76 kHz; −0.57 dB of loss. IC1a's swing on +9 V (≈1.2–7.8 V) limits the output to about
  +2.6/−3.6 V at the jack.
- Spec: "Audio (0dBm max.)". COMPUTED: a full saw at VOLUME 10 with the VCF open is ≈0.69 V RMS.

---

## 8. Corrections to research.md and open points

| Item | research.md | Here |
|---|---|---|
| FREQ slider top | 0–5 V (4.4–5.1 oct span) | +15 V: 14.3 oct span, 1.2 Hz–25 kHz |
| TR26's collector gain | 1.49 | 1.58: R132 loads the emitter |
| Self-oscillation onset | ≈0.81 | 0.79 (SPICE 0.787) |
| S6 wiring | common on R197 | the ENV contact on R197, the common to R133 |
| TR28 | unspecified ("diode knee") | a PNP in common base, base on the bus |
| R162 (OML: missing) | — | between IC16b's output and IC16a's +input; nothing else works. The SN's board diagram agrees |
| Output coupling | C3 100 µF, "no audible high-pass" | C2 2.2 µF: 2.2 Hz into 47k, 6 Hz into 10k |
| ENV's top | ≈8–10 V | 10.11 V (5.11 V above the bus, on +14 V) |

Open points:

- +14 V's value from unit to unit (U-Labor 14.3 V; the Goob's unit 14.81 V on R158, §5.5): the
  ENV's swing, and so its depth at the cutoff, scale with it. TH1's value, and the expo's slope
  and bias (19 mV/oct and 2.4 µA are the data sheet's): these set the cutoff in Hz, which the
  calibrations fit.
- The LFO line's polarity at S7 deck 2, and IC4b's input. (R162's position: settled by the SN's
  board diagram, §5.4.)
- The IR3109's internals are a community decode; the OTAs' mismatch and offsets are not modelled.
