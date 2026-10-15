# Roland TB-303 analog voice: circuit transcription

This file transcribes the TB-303 main-board analog circuitry (CPU interface, pitch DAC, slide, VCO, VCF, envelopes, VCA and output) from the Roland service notes. Every value is cross-checked against a second Roland printing, a cleaned redraw and the x0xb0x clone. Part designators are Roland's.

Two kinds of number appear here:

- **Read** values are taken directly from a drawing, with a citation.
- **COMPUTED** values are my own derivations. The arithmetic is shown, and the assumptions are stated where they matter.

**UNCERTAIN** marks anything that is illegible, that conflicts between sources, or that relies on an inference about circuit operation.

---

## 0. Sources, citation keys, conventions

| Key | Document | Local file(s) |
|---|---|---|
| **SN82** | Roland *TB-303 Service Notes, First Edition*, dated Feb. 19 1982, 8 pp., 150 dpi greyscale scan (archive.org item `synthmanual-roland-tb-303-service-notes`). p.1 specs/exploded view, p.2 IC pinouts, p.3 block diagram, p.4 digital/CPU schematic, **p.5 analog main-board schematic**, p.6–7 adjustment + PCB views, p.8 "VCF Envelope Modulation" + parts list | `private/docs/tb303-service-manual.pdf`, `private/docs/tb303-service-manual.txt` (archive.org OCR, noisy), page renders `private/docs/schematic/sn-1982-p-{1..8}.png` (300 dpi render of 150 dpi scan), tiles `private/docs/schematic/tiles/sn1982-p5-*.png` |
| **SN86** | Same service notes, later printing ("Nov. 1984 E-2, 3rd Printing Sep. 1986 B-2"). Only 3 pages exist in the scan (p.1, exploded view, **analog schematic**). Corrects at least R110/R111. | `private/docs/schematic/alt-3rd-printing-1986/tb303-service-notes-3rd-printing-partial.pdf`, `sn-1986-p-{1,2,3}.png`, tiles `private/docs/schematic/tiles/sn1986-p3-*.png` |
| **KL** | Cleaned 1-bit 3000×2080 GIF of the analog page, contributed by Kevin Lightner (emusic-diy.org). It matches SN86, including R110/R111 = 22K, and carries one contributor annotation, "ROLAND TYPO", at R126. **This is the clearest image of the analog page.** | `private/docs/schematic/alt-emusic-diy/tb303.gif`, tiles `private/docs/schematic/tiles/emusic-analog-*.png` |
| **X0X** | x0xb0x "Mainboard Beta Release (3/8/2005)", Limor Fried. Analog section cloned from the 303 with the same designators. (Eagle 4 binary `.sch`, not parsed.) | `private/docs/x0xb0x/mainboard_beta.png`, `mainboard.sch`, `ioboard.sch`, `ioboard_release_v1_0.png` |
| **OM** | Roland TB-303 Owner's Manual, 90 pp., 300 dpi | `private/docs/tb303-owners-manual.pdf`, `.txt` (archive.org OCR) |
| **RW-unique** | R. Whittle, "Investigating some Unique aspects of the TB-303's sound" (accent sweep), 1999 | `private/docs/whittle/303-unique.txt` (raw HTML in `private/docs/whittle/raw/`) |
| **RW-slide** | R. Whittle, "Timing of the Gate, CV and Slide in the TB-303 sequencer", 1999 | `private/docs/whittle/303-slide.txt` |
| **RW-DFM** | R. Whittle, *Devil Fish Manual*. Page numbers are PDF pages. Describes the stock 303 behaviour that the Devil Fish changes. | `private/docs/whittle/Devil-Fish-Manual.txt`, `raw/Devil-Fish-Manual.pdf` |
| **RW-mods** | R. Whittle, "TB-303 modifications and maintenance" (pot values, resonance-pot wiring diagram) | `private/docs/whittle/303-mods.txt`, `raw/303-mods/Resonance-pot-schematic.png` |

Zoomed crops used for this transcription are in `private/docs/schematic/crops/` and are named after the source and the block, e.g. `kl-vcf-ladder-top-output.png` or `sn82-p5-vca.png`. Use them to re-check any value below.

**Symbols on SN p.5**

| Symbol | Meaning |
|---|---|
| ⊕ (circle with cross) | The **5.333 V reference** node. The box note on p.5 reads "5.333V ⊕ connects to pin 7 of IC16 (POWER SUPPLY)". |
| Filled arrow ↑ | The **+12 V** analog rail (nominally 11.85 V, see §1). X0X labels the same nodes "+12V". |
| Open triangle △ | **+6 V** (battery/digital) rail. p.5 power section: "+6V". |
| Bare "T-bar" | Appears only on VR2 pin 3. Unlabeled (UNCERTAIN, see §3.2). |

**Conventions**

- Pots: pin 1 is the CCW end, pin 3 is the CW end, pin 2 is the wiper. Taper letters are Japanese: A = audio/log, B = linear, C = reverse-log.
- Capacitor values written "x/y" are µF/volts, e.g. "1/50" = 1 µF 50 V.
- Bare decimals are µF, e.g. ".033" = 0.033 µF.
- Computations use V_T = kT/q = 25.69 mV at 25 °C, so V_T·ln2 = 17.81 mV per octave.

---

## 1. Power rails and references (SN82 p.5 "POWER SUPPLY", p.3 block diagram, p.6 PCB view)

| Rail | How it is made | Values (read) | Notes |
|---|---|---|---|
| +6 V | 4 × 1.5 V cells, or a 9 V adaptor through series pass Q44 2SB647(C) and its driver Q45 2SB596(O). Zener D4C "RD6.8EB3" (6.8 V); fuse resistor R168 "FRNB 2.7 Ω". Backup diode D2 and C1 100/6.3 feed the CPU/RAM. | +6V label on p.5 and on the p.6 board view | Feeds the CPU/logic (△) and the LA4140 headphone amp (X0X shows IC14 on +6 V). Power-stage values are partly illegible (UNCERTAIN). |
| ≈15 V raw | DC-DC converter: transformer T2 "MC102" (parts list "MC102C DC-DC converter") with Q43 2SD667C, Q42 (KL annotation "Q42 2SC536"), D41 rectifier, C68 47/16. Other parts: R170 22 Ω, C66 .001 (SN82) or .0022 (KL), R171 3.3K, C67 2.2/50 (KL annotation "10UF/16V"), D42 "RD15EB3", R172 100 Ω + C69 47/16 filter, R173 1K + C70 10/16. | "15V" test label on the p.6 PCB view | Only the analog regulators use it. |
| **5.333 V (⊕)** | IC16b AN6562 (pins 5/6/7) with reference D43 "1SZ46A" (parts list possibly "158-46A"; UNCERTAIN). Feedback R177 6.8K, R174 5.6K, **TM6 4.7K "CV"**, R180 39K, R176 6.8K. C60 10/16 on the output; TP5. | "5.333V" (p.5), "5.33V" (p.6 board) | **TM6 trims this rail to set the DAC scale to exactly 1.000 V/octave** (§8). The DAC, VCO expo reference, VCF bias, VCA bias and mixer bias all hang off this node. |
| **+12 V (↑)** | IC16a AN6562 (pins 1/2/3) amplifies the 5.333 V reference: R178 2.2K feedback (pin 1→2), R179 1.8K to ground; C61 10/16 on the output. | "12V" (p.5, p.6 board) | COMPUTED: 5.333 × (1 + 2.2/1.8) = **11.85 V**. The p.3 block diagram labels both "12V IC16" and "5.333V IC16". |
| VCF local rail | +12 V → R95 100 Ω → C28 47/16 (SN82 p.5) | | Feeds the ladder top (Q22), the Q21 collector loads and Q19. |

**Transistor types.** A typed note on SN82 p.4 reads "ALL NPN TRANSISTORS ON THE MAIN BOARD ARE 2SC945(P). ALL PNP TRANSISTORS ON THE MAIN BOARD ARE 2SA733(P)". The parts list (p.8) instead gives 2SA733(P), 2SB647(C), 2SB596(O), 2SD667(C), **2SC2021(R)** (UNCERTAIN: this does not match the p.4 note), 2SC1583(F) "Dual Tr" and 2SC2291(F) "Dual Tr". FETs: 2SK30TM(Y) and 2SK30TM(O). X0X substitutes 2SC536F for the NPNs. A note on the production run (bcaudio, "Roland TB-303 Transistors", 2012) says three NPN types were used: 2SC536F, 2SC1815GR and 2SC945P.

**Gain ranks (data sheets, 2026-10-07; copies in `private/docs/datasheets/`).** The rank letters fix each part's gain within a factor of two:

| Part | Where | Rank | h_FE at 6 V, 1 mA | Source |
|---|---|---|---|---|
| 2SC945 | small-signal NPNs (Q32, ladder "diodes", Q24, Q25, ...) | P | 200–400 (0.92× at 0.1 mA); V_BE 0.62 V (0.55–0.65) | NEC D17117EJ2V0DS |
| 2SC536 / 2SC1815 / 2SC2021 | production alternatives | F / GR / R | 160–320 / 200–400 / ~180–390 | maker tables (2SC2021: Rohm's R rank on related parts) |
| 2SA733 | all PNPs (Q31, Q36, Q9, Q10, Q8, ...) | P | 200–400 | NEC D10868JJ8V0DS00 |
| 2SC1583 | Q12, Q21, Q26 (matched duals) | F | 250–500 (the lower half; G 400–800); about 0.87× at Q21's 0.26 mA; pair ratio 0.8–1.0, ΔV_BE 1 mV typ | Mitsubishi |
| 2SC2291 | Q22 (dual) | F | likely 250–500: Mitsubishi's sheet not found; a seller gives G as 400–800, as for the 2SC1583 | — |
| 2SK30A | Q28 (O), Q39 (Y) | O / Y | I_DSS 0.60–1.40 / 1.20–3.00 mA; V_GS(off) about −1.0 / −1.5 V at 1 / 2 mA | Toshiba 2SK30ATM |

So Q21's gain at its operating point is 220–435 (the service-notes calibration's 300 and the Roland one's 400 both lie inside), and Q32's, which sets how long the VEG is held (§6.2), is about 160–360 in its µA tail.

---

## 2. CPU → analog interface

### 2.1 Connector lines 1–9 from the CPU board (SN82 p.5 left edge; crops `kl-cpu-connectors.png`, `sn82-p5-cpu-inputs-gate.png`)

| Line | Goes to | Function |
|---|---|---|
| 1 | IC9 pin 4 (D1) → Q1 (pin 5) → R82 | pitch bit, weight 1 (LSB) |
| 2 | IC9 pin 13 (D4) → Q4 (pin 12) → R81 | weight 2 |
| 3 | IC9 pin 3 (D0) → Q0 (pin 2) → R80 | weight 4 |
| 4 (unlabeled tab) | IC9 pin 14 (D5) → Q5 (pin 15) → R79 | weight 8 |
| 5 | IC9 pin 6 (D2) → Q2 (pin 7) → IC10 (one 4050 buffer, 3→2) → R87 | weight 16 |
| 6 | IC9 pin 11 (D3) → Q3 (pin 10) → IC10 (four 4050 buffers in parallel) → R90 | weight 32 (MSB) |
| 7 | IC13 (HD14013 D flip-flop) pin 9 = D | accent bit |
| 8 | IC9 pin 9 (CLK) **and** IC13 pin 11 (T/CLK) **and** the "SLIDE ON/OFF" line | note strobe / slide (see §2.3) |
| 9 | R146 22K → Q37 base (R144 10K base-to-ground) | gate |

The ladder weights follow from where each bit enters the R-2R ladder. The resulting connector order 1→6 = 2⁰…2⁵ is a clean binary order, which is a good consistency check. The precise assignment of lines 7 and 8 (accent data vs. strobe) is inferred from the horizontal order of the wires (UNCERTAIN ±1 line). The functions themselves are certain.

### 2.2 Pitch DAC (SN82 p.5; KL; X0X; crops `sn82-p5-dac-slide.png`, `kl-slide.png`, `x0x-ic9-ladder.png`, `x0x-ladder-termination-r78.png`)

**Latch.** IC9 is an HD14174 (hex D flip-flop, positive-edge). It is powered from **5.333 V (⊕, pin 16; reset pin 1 tied high)**, so every logic-high output sits exactly at the reference voltage.

**Ladder.** R74–R90 are **all 200 kΩ**, noted "MATCHED PAIR, .1% SELECTED, MFR: R74–R90". The ladder is a classic R-2R network:

- Series "R" elements: 200 kΩ ∥ 200 kΩ = 100 kΩ. The pairs are R88∥R89, R85∥R86, R83∥R84, R76∥R77, R74∥R75. SN82 prints "R96" where R86 belongs (X0X: R86).
- "2R" bit resistors: R90 (MSB), R87, R79, R80, R81, R82 (LSB).
- Termination: **R78 200 kΩ to ⊕ 5.333 V, not to ground.** SN82, KL and X0X all agree.

**Output.**

- COMPUTED: V_DAC = V_ref·(N + 1)/64, where N = 0…63. The +1 comes from terminating R78 at V_ref.
- 1 LSB = 5.333/64 = **83.33 mV = 1 semitone at 1 V/oct.** This is why the reference is 5.333 V = 64/12 V.
- Examples: N = 11 → 1.000 V (low C); N = 32 → 2.750 V (the "A KEY" 2.75 V in the p.5 tuning note); N = 63 → 5.333 V.
- Thevenin output resistance of the ladder = **100 kΩ** (COMPUTED; Whittle measures the same, RW-DFM p.16).

**Buffer.** IC11b AN6562: pin 5 (+) is the ladder node; pin 6 (−) takes feedback from pin 7 through **R107 2.2K**, so it is a unity follower. The output pin 7 is **TP4**.

**Clamps at TP4.** D31 goes to ⊕ in SN82 (X0X: to +12 V; UNCERTAIN) and D30 goes to ground.

**Users of TP4.** The VCO (§3) and the **CV OUT jack J3**. The block diagram (p.3) shows CV OUT taken from IC11, i.e. after the slide circuit; RW-DFM p.15 agrees.

### 2.3 Slide (glide) circuit (SN82 p.5; KL; X0X; RW-DFM p.16; RW-slide)

**Components.**

- IC12 HD14066 quad bilateral switch, powered from +12 V (X0X):
  - Section C: pins 8↔9, control pin 6.
  - Section D: pins 10↔11, control pin 12.
- **C35 0.22 µF** from the pin 9/10 junction to ground.
- Pin 8 = the DAC ladder node, i.e. IC11b pin 5. Pin 11 = the IC11b output (TP4).

**Control.** "SLIDE ON/OFF" drives Q29's base through R59 100K (X0X also has R60 22K base-to-ground; this is not visible in SN82, UNCERTAIN). The two transistors form a complementary pair:

- Q29 collector: R92 100K pull-up to +12 V, IC12 pin 12, and R91 1M to Q30's base.
- Q30 collector: R93 100K pull-up, IC12 pin 6.
- Both emitters are grounded.
- Result: slide **high** → pin 12 low (switch D open) and pin 6 high (switch C closed). Slide **low** → the reverse.

**Operation (COMPUTED from topology).**

- *Slide off:* switch D connects C35 to the follower output, so C35 tracks the current note voltage. Switch C is open, so the follower input is the bare ladder and the pitch steps instantly.
- *Slide on:* switch C connects C35 to the ladder node and switch D releases it. The follower input becomes the ladder's 100 kΩ Thevenin source charging C35, starting from the previous note's voltage.

**Glide time constant: τ = 100 kΩ × 0.22 µF = 22.0 ms** (exponential, linear in volts and therefore in octaves).

- 10–90 %: 2.197 τ = 48 ms.
- 95 %: 3 τ = 66 ms.
- 99 %: 4.6 τ = 101 ms.
- RW-DFM p.4: "Normally the slide time is 60 ms". RW-DFM p.16: "the 100K resistance of the DAC feeding a 0.22uF capacitor". The 4066 on-resistance (~0.1–0.3 kΩ at 12 V) is negligible.

**Strobe = slide line.** On SN82, SN86 and KL the IC9 clock (pin 9), the IC13 clock (pin 11) and "SLIDE ON/OFF" are drawn as **one net**, a T-junction with no dot (crops `sn82-p5-ic9-clk-slide-line.png`, `kl-ic9-clk-slide-line.png`). The pitch and accent are therefore latched on the rising edge of that line, which also engages the slide switch for as long as it stays high.

- RW-slide measures the timing: slide goes high at the start of the note *following* a slid note and stays high until that note ends. Gate stays high across the tie.
- Inference (UNCERTAIN): on non-slid notes the CPU emits only a brief strobe, so any glide is negligible.
- X0X uses separate NOTE_LATCH and SLIDE nets because it has its own CPU.

### 2.4 Gate input conditioning, gate delay and GATE OUT (SN82 p.5; crops `sn82-p5-cpu-inputs-gate.png`, `x0x-env.png`)

**Circuit.**

- Line 9 → R146 22K → **Q37** (NPN, R144 10K base-to-ground, emitter grounded). The Q37 collector is the inverted gate.
- The Q37 collector drives **R143 10K** into the base of **Q36** (2SA733 PNP):
  - Emitter: R147 22 Ω to +12 V.
  - Base: R142 10K to +12 V, plus **C72 10/16 from base to the +12 V rail**.
  - Collector: R145 10K to ground. This is the internal gate signal, **GATE_INT**, which swings ≈0 to ≈+12 V.
- The Q37 collector also feeds C54 into the Q38 one-shot (§5.1).

**Uses of GATE_INT.**

1. Through **R137 1K and D34** to J4 **GATE OUT**. Spec: "Gate Out: OFF 0V; ON +12V" (SN82 p.1). RW-DFM p.14: "driven by a diode and a 1K resistor".
2. Through **D35 → R133 2.2K → R131 220K** to the emitter supply of the VCA envelope current source Q31 (§6).

**Gate-on delay (COMPUTED).** When Q37 turns on, the Q36 base node moves from 12 V toward 6 V (R142/R143 divider, Thevenin 5 kΩ). C72 slows this: τ = 5 kΩ × 10 µF = 50 ms. Q36 starts conducting after the base has fallen ~0.6 V:

t ≈ −50 ms × ln(1 − 0.6/6) = **5.3 ms**.

Turn-on then completes over ~1–2 ms. RW-DFM p.4/p.6 measured "a (typical) 4 ms delay and then a 3 ms attack" and "between 1 and 5 ms (typically 4 ms)". **This C72 delay is the 303's note-start lag.**

**Gate-off release (COMPUTED; assumes β ≈ 300 and a ~1.3 mA collector load).** After Q37 turns off, C72 (charged to V_EB ≈ 0.7 V) has to discharge through R142 and the Q36 base before Q36 leaves saturation:

- GATE_INT holds ≈12 V for ≈10 ms.
- It then falls roughly exponentially with ≈3–4 ms per e-fold.

RW-DFM p.6 measured "the decay at the end of an unaccented note was about 16 ms – 8 ms of normal volume and 8 ms of linear decay". **SPICE (`tools/spice/env.cir`):** GATE_INT holds for ~4–5 ms after gate-off, then falls; the VCA current is under 10 % after 12.3–14.9 ms across the parts' spread. Two things lengthen it beyond the simple estimate: R147's degeneration of Q36 (26 mV at its ~1.2 mA), and C54 charging through R143 from Q36's base for ~1.5 ms as Q37's collector rises.

### 2.5 Accent latch (SN82 p.5; crop `kl-cpu-connectors.png`)

- IC13 HD14013: D (pin 9) ← accent bit; T (pin 11) ← the strobe/slide line. Supply is △ (+6 V side, UNCERTAIN). Q̄ is pin 12.
- Q̄ → R141 100K → **Q35** base (R140 100K to ground). The Q35 collector has R139 100K pull-up to +12 V and forms the **ACCENT** line.
- Accent latched = 1 → Q̄ = 0 → Q35 off → **ACCENT = +12 V**.
- ACCENT drives the IC12 control pins **5** (switch B: shortens the MEG decay) and **13** (switch A: MEG → accent pot).

---

## 3. VCO (SN82 p.5 "VCO"; SN86 p.3; KL; X0X; crops `kl-vco-expo.png`, `kl-vco-reset-shaper.png`, `sn82-p5-vco.png`, `sn86-p3-vco.png`, `x0x-vco.png`, `x0x-square-shaper.png`, `sn82-p5-vco-tm4-r102.png`)

### 3.1 Exponential converter

**Expo pair.** **Q26 2SC1583(F)**, a matched dual NPN with common emitters. The emitters are driven from the IC11a AN6562 output (pin 1) through **R104 2.2K**.

**Reference side (left half).**

- Base at ⊕ (5.333 V).
- Collector → IC11a pin 2 (−). IC11a pin 3 (+) = ⊕, so the op-amp holds this collector at 5.333 V (V_CB = 0).
- Compensation: **C34 .001 µF** from pin 2 to pin 1.
- The reference current flows into the collector node from two sources:
  - **R103 220K** from +12 V: (11.85 − 5.333)/220k ≈ **29.6 µA** (30.3 µA if the rail is a full 12 V).
  - **R102 100K** from the wiper of **TM4 "TUNE"** trimmer, which sits between ⊕ and +12 V: +0 … +65 µA.
  - SN82 prints R102 as "100" with an adjacent "K"; X0X gives 100K. TM4's value is not legible: X0X says 50K(B), and the parts list's trimmers include 47 kB (UNCERTAIN).
- I_ref ≈ 30–95 µA. COMPUTED: about 43 µA is needed for 110 Hz (§3.3).

**Control side (right half): base node.**

- From TP4 (CV) through **TM5 "WIDTH" 4.7K** (wired as a rheostat; X0X 5K(B)) + **R106 27K**.
- **R100 560 Ω "POSISTOR"** (PTC thermistor, tempco compensation) from the base to ⊕.
- **R118 100K** from the TUNE line (VR2 wiper).
- The right-half collector is the integrator current sink (§3.3).

**Transfer (COMPUTED):** I_C = I_ref · exp[(V_B,R − 5.333 V)/V_T], with V_B,R − 5.333 = k_cv·(V_CV − 5.333) + k_t·(V_TUNE − 5.333).

- k_cv = R100∥R118 / (R100∥R118 + R106 + TM5) ≈ 557/(557 + 27k + TM5).
- For 1.000 V/oct, k_cv = V_T ln2 / 1 V = 0.01781 → R106 + TM5 = 30.9 kΩ → **TM5 ≈ 3.9 kΩ** (mid-range of the 4.7K trimmer).
- TM5 fully out gives 0.876 V/oct; TM5 at 4.7k gives 1.026 V/oct.
- k_t = R100∥(R106+TM5) / (… + R118 + R_VR2) ≈ 0.55 k / 100–112 k ≈ **4.9–5.5 mV per volt of tune**.

### 3.2 Tuning pot

- **VR2 "TUNING" 50K(B)**: pin 1 = ground; pin 2 wiper → TUNE line → R118; pin 3 → an unlabeled "T-bar" symbol in SN82/SN86 (crops `sn82-p5-vr2-tuning.png`, `sn86-p3-vr2-tuning.png`). X0X ties that end to **+5.333 V** (UNCERTAIN for the original).
- Specs disagree on the range:
  - SN82 p.1: "TUNING Control: approx. ±700 cents (perfect fifth)".
  - OM p.89: "TUNING Control: (±500 cents)".
- COMPUTED, assuming 0…5.333 V on the pot: ±2.667 V × (4.9–5.5 mV/V) = ±13–14.6 mV = ±0.73–0.82 oct (±880–980 cents). This overshoots both specs, so the pot's top-end voltage is probably not 5.333 V in the original, or the R100 value differs (UNCERTAIN).
- The tuning pot affects the **VCO only**: it does not touch CV OUT and there is no filter tracking (RW-DFM p.15).
- p.5 header note: **"TUNE: 110 Hz (SET TUNING [knob drawn at centre], CV OUT: 2.75V (A KEY)) WIDTH: 1 OCT/VOLT"**.
- OM p.33 gives A ≈ 220 Hz when tuning in pattern-play/pitch mode, i.e. a different octave of the same note.

### 3.3 Saw core and reset

**Integrator.** **C33 0.01 µF** to ground. The Q26 right collector **sinks** current from it, so the capacitor voltage **ramps down** (a falling sawtooth).

**Reset (SN82, KL, X0X agree).**

| Part | Type | Connections |
|---|---|---|
| Q25 | NPN | emitter = C33 node; base = node B; collector = Q27 base |
| Q27 | 2SA733 PNP | emitter to +12 V; collector → D25 anode |
| D25 | diode | cathode = node B |
| Q24 | NPN, diode-connected | base = collector = node B; emitter → **R101 10K → ⊕** |

How it fires (inferred): when V_C33 falls about one V_BE below node B (≈5.333 V + V_BE24 − V_BE25 ≈ 5.3–5.5 V), Q25 conducts. That turns on Q27, which drives node B high through D25, so the pair regenerates like an SCR and dumps current into C33 through Q25 until C33 approaches node B − V_BE (≈10.5–11 V). Then the pair releases.

**Measured levels.** p.5 scope inset at the Q28 source: falling ramp with **top "12V", bottom "5.5V", GND reference**, i.e. ≈6.5 V p-p as drawn. **SPICE of this core (`tools/spice/vco.cir`) gives 10.85 V top and 5.6 V bottom (5.25 V p-p)**, with a ~1 µs reset: the reset cannot charge C33 above the rail less Q27's V_CE(sat), D25 and Q25's V_BE, so the sketch's "12V" is probably the scale, not the peak. The bottom moves with the JFET (5.3-6.1 V across the 2SK30-O's I_DSS range).

**Frequency (COMPUTED, ignoring reset time):** f ≈ I_C / (C33·ΔV) = I_C / (0.01 µF × 6.5 V) ≈ **15.4 Hz per µA**.

- 110 Hz needs 7.15 µA.
- At CV = 2.75 V: exp(−2.583 V × 0.01781 / 25.69 mV) = 0.167, so I_ref ≈ **43 µA**. TM4 then supplies ≈13 µA (wiper ≈6.6 V).
- Pitch map with the tune pot centred: **f = 110 Hz × 2^(V_CV − 2.75 V)**. That gives 1 V = 32.7 Hz (C1), 2 V = 65.4 Hz, 3 V = 130.8 Hz, 4 V = 261.6 Hz, 5.333 V = 660 Hz.
- The inset "(9ms KEY A)" (110 Hz period 9.09 ms) and the VCF inset "(15 ms KEY C)" (65.4 Hz period 15.3 ms) both fit.

### 3.4 Saw buffer

- **Q28 2SK30(O)** JFET source follower: gate on C33, drain to +12 V, source with **R105 10K** to ground.
- The source is the saw output. It goes to S1 (saw) and to the square shaper, and is also the "Q28 source" check point.
- Gate-source offset ≈ 0.2–1 V (2SK30A-O I_DSS 0.6–1.4 mA; UNCERTAIN).

### 3.5 Saw → square waveshaper (SN82, SN86 and KL agree; X0X drawing ambiguous)

**Q8 2SA733 PNP.**

- Emitter: **R45 22K** from +12 V, and **C11 1/50 (1 µF)** from emitter to ground.
- Collector: **R36 10K to ⊕ (5.333 V)**. The collector is the square output.
- Base drive from the saw (Q28 source):
  - **R35 100K** directly, in parallel with
  - **C10 0.01 µF in series with R34 10K**.
  - This is a lead network that sharpens the switching edge (C10·R34 = 0.1 ms, 1.6 kHz).

**Behaviour.** Q8 conducts during the low part of the falling saw (near its bottom, before reset):

- Q8 off: output = 5.333 V.
- Q8 saturated: output ≈ V_E.
- p.5 scope inset: **"8V" high level, "5V" low level, "(9ms KEY A)"**.

**Self-biasing emitter.** C11/R45 make the emitter voltage, and therefore the switching threshold, settle so that the average emitter current (12 − V_E)/22k equals the duty-weighted collector current D·(V_E − 5.333)/10k.

- COMPUTED equilibrium, assuming a linear 5.5–12 V saw and ignoring the lead network and V_CE(sat): V_E ≈ 8.8 V, duty (high) ≈ 40 %.
- Using the inset's 8 V high level instead gives D ≈ 65 %.
- **Duty cycle is pitch-dependent: SPICE gives 60 % high at C1, 52 % at C2, 48 % at C3, 43 % at C4 and 37 % at C5** (`tools/spice/compare_vco.py`), high level 8.3-8.7 V, low 5.4 V.

**Droop.** During the high phase C11 supplies the collector current, so the top sags at ≈(V_E − 5.333)/(10k·1µF) ≈ **270 V/s**.

- That is ≈1.2 V over the 4.5 ms high time at 110 Hz, more at low notes.
- The inset sketch shows the sloped top. C11·R45 = 22 ms.

### 3.6 Waveform switch

- **S1 "WAVEFORM"** (2-position slide): selects Q28 source (saw) or Q8 collector (square), feeding R62 → VCF (§4.2).
- Owner's-manual symbols: saw / square (OM p.89).

### 3.7 VCO adjustment (SN82 p.7; quoted in §8)

TM5 WIDTH is set for 2:1 periods between the CL and CH keys. TM4 TUNE is set for 110 Hz on A, then a 4:1 ±0.5 % check is made with CH + TRANSPOSE UP.

---

## 4. VCF (SN82/SN86/KL p.5 "VCF"; X0X; SN82 p.8 "VCF ENVELOPE MODULATION"; crops `kl-vcf-ladder-top-output.png`, `kl-vcf-ladder-bottom-expo-feedback.png`, `sn82-p5-vcf-expo-q9-q10-q11.png`, `x0x-vcf-ladder.png`, `x0x-vcf-expo.png`, `x0x-vcf-q9-q10-q11.png`, `kl-vcf-to-pots-routing.png`, `kl-summing-resistor-routing.png`)

### 4.1 Ladder core: transistor "diode" ladder, 4 capacitors

The block diagram (p.3) labels it "TR. C LADDER". From the bottom up:

| Position | Device | Notes |
|---|---|---|
| Input diff pair | **Q12 2SC1583(F)** (matched dual NPN) | Emitters joined → **R69 2.2K** → Q11 collector (expo current sink, §4.5). Collectors = bottom ladder nodes. |
| Cap 1 (bottom) | **C18 0.018 µF** | Across the Q12 collectors / Q13–Q15 emitters. **Half the other values.** |
| Diode pair 1 | Q13 / Q15 (NPN, base tied to collector) | |
| Cap 2 | **C19 0.033 µF** | Across Q13/Q15 tops = Q14/Q17 emitters |
| Diode pair 2 | Q14 / Q17 (diode-connected NPN) | |
| Cap 3 | **C24 0.033 µF** | Across Q14/Q17 tops = Q16/Q23 emitters |
| Diode pair 3 | Q16 / Q23 (diode-connected NPN) | |
| Cap 4 (top, output) | **C26 0.033 µF** | Across Q16/Q23 tops = Q22 emitters. **The filter output is the differential voltage across C26.** |
| Top pair | **Q22 2SC2291(F)** (dual NPN) | Bases tied together to the **R115 10K** (from the VCF rail) / **C30 10/16** bias node, i.e. an AC ground at ≈ the rail. Collectors to the VCF rail. Emitters terminate the ladder top. |

All four capacitor values agree across SN82, SN86, KL and X0X: **C18 = 0.018 µF; C19 = C24 = C26 = 0.033 µF.**

Diode-ladder transistors are "2SC945(P)" per the SN82 p.4 note (X0X: 2SC536F).

### 4.2 Input coupling and drive level

**Signal path.** S1 common → **R62 220K** → **C17 1/50** → Q12 *left* base.

**Bias for both Q12 bases.**

- The bias node is ⊕ via **R68 2.2K**, decoupled by **C16 10/16**, so it sits at ≈5.3 V.
- Left base: through **R70 2.2K**.
- Right base: through **R98 2.2K**.

**Drive level (COMPUTED).**

- Attenuation = R70/(R62 + R70) = 2.2/222.2 = **1/101**.
- Saw 6.5 V p-p → **≈64 mV p-p** differential at Q12. Q12's tanh linear range is ±2V_T ≈ ±51 mV, so the drive is mildly nonlinear.
- Square ≈2.7–3.5 V p-p → ≈27–35 mV p-p.
- Coupling HPF: C17 with 222k → 0.72 Hz, negligible.

### 4.3 Resonance: dual-gang pot and feedback path

**VR4 "RESONANCE"** is a dual 50K(B). SN82 p.5 reads "50K(B)", and the p.8 parts list reads "13219774 K162T00W-50kB x 2 RESONANCE". The SN86 p.1 exploded view and RW-mods give part number 13219775; RW-mods confirms "Dual 50K linear". RW-mods' wiring diagram (`private/docs/whittle/raw/303-mods/Resonance-pot-schematic.png`) matches the trace below.

**Gang A: feedback amount.**

- Pin 3 ← VCF output (after C14, §4.4).
- Pin 1 → ground.
- Wiper (pin 2) → two destinations:
  - **C15 1/50** → **R67 2.2K** → base of **Q18** (NPN emitter follower). Q18's base is biased through **R99 100K** from a node fed by **R66 100K** from ⊕ and decoupled by **C22 1/50**. Q18's collector goes to +12 V; its emitter has **R94 10K** to ground.
  - Also → **R122 100K + 0.01 µF** into the VCA input (§6.5).
- From the Q18 emitter: **C23 1/50** → **R97 10K** → Q12 *right* base, which returns through R98 2.2K to the AC-grounded bias node.

**Gang B: accent sweep (§5.5).**

- Pin 1 (CCW) ← R46 47K ← D24.
- Pin 3 (CW) → **C13 1/50** to ground.
- Wiper → R72 100K → VCF summing node.

**Feedback gain chain (COMPUTED).**

- Pot fraction k_res (0…1) × Q18 follower (≈1) × R98/(R97 + R98) = 2.2/12.2 = **0.180**.
- Then into Q12's differential input.
- The output stage (§4.4) adds ≈ ×115 from the ladder-top differential voltage to the single-ended output.
- So the loop gain from the ladder top back to the ladder input is ≈ **20.7 × k_res**, before the ladder's own transfer.

**High-pass sections inside the resonance loop (COMPUTED).**

| Section | Corner |
|---|---|
| C14 1 µF into ≈50 kΩ (VR4-A ∥ other loads) | ≈3.2 Hz |
| C15 1 µF into 2.2k + (100k ∥ β·10k) ≈ 98 kΩ | ≈1.6 Hz |
| **C23 1 µF into R97 + R98 = 12.2 kΩ** | **13.0 Hz** (Q12's base impedance is ≫ at µA tail currents) |
| **C25 / C27 0.1 µF** ladder-output coupling (§4.4) into R113/R114 100k ∥ r_π(Q21) | **≈49–82 Hz** for β = 500…250 (15.9 Hz if r_π is ignored), plus the ladder's top-node source resistance |

The output coupling caps are therefore the dominant "**feedback high-pass**" of the 303 filter. Its exact corner depends on Q21's β and the ladder current, so its value is UNCERTAIN. Note that published emulations, e.g. Open303, use an empirically fitted feedback HPF of about 150 Hz (from memory, not verified here).

### 4.4 Output differential amplifier and buffer

**Coupling.** Ladder top (across C26) → **C25 0.1 µF** and **C27 0.1 µF** (cross-wired: the left top node goes to Q21's right base and vice versa) → **Q21 2SC1583(F)** differential pair.

**Base bias.**

- Q21 bases: **R113 100K** and **R114 100K** to a bias node fed by **R116 10K from ⊕** and decoupled by **C29 1/50**.
- From the same node, **R112 10K** + **R109 10K** form a divider whose midpoint drives the base of **Q20**, the tail current sink. Q20's emitter has **R108 2.2K** to ground.

**Collector loads: R110 and R111 to the VCF rail.**

| Source | Value |
|---|---|
| SN82 | **2.2K** |
| SN86, KL, X0X | **22K** |

**Use 22K.** DC check (COMPUTED):

- Bias node = 5.333 × 20/30 = 3.56 V → Q20 base 1.78 V → I_tail ≈ (1.78 − 0.6)/2.2k = **0.54 mA** → 0.27 mA per side.
- With 22K the collectors sit at ≈12 − 5.9 ≈ 6.0 V, so the Q19 output is ≈5.4 V. This matches the TP6 inset levels of **5.2–5.8 V**.
- With 2.2K the output would sit at ≈10.7 V, which does not match.

**Gain (COMPUTED).** g_m = 0.268 mA/25.69 mV = 10.4 mS per side. Single-ended gain from the R110 collector = (g_m/2) × 22k ≈ **115**.

**Buffer.** **Q19** NPN emitter follower from the R110 collector: collector to the VCF rail, emitter with **R96 10K** to ground, then **C14 1/50** → **VCF OUT**. VCF OUT feeds VR4-A pin 3 and **R121 220K + C21 0.01 µF** into the VCA.

**TP6.** Located near this output (p.5/p.6 board view). The inset DC levels (5.2–5.8 V) imply it is the Q19 emitter side (UNCERTAIN). SPICE puts Q19's emitter at 5.37 V and the Q21 collectors at 5.98 V, tail 0.52 mA.

**Which side R110/Q19 are on** is not legible against the cross-wired C25/C27. It is fixed by the loop: the feedback through VR4-A, Q18 and R97 into Q12's right base must be negative at low frequencies for the ladder to resonate; the other side gives positive feedback and no resonance (SPICE).

### 4.5 Cutoff control: expo converter and summing (Q9, Q10, Q11)

| Part | Type | Connections |
|---|---|---|
| **Q9** | **2SA733 PNP** emitter follower | Base from **R65 10K** (from ⊕) / **R64 10K** (to ground) = 2.667 V. Emitter = "node E" ≈ **3.27 V**. Collector to ground. Holds node E stiff and sinks the converter currents. |
| **Q11** | NPN (2SC945/2SC536) | Collector → R69 → Q12 emitters, i.e. **Q11's collector current is the ladder current I_f**. Emitter = node E. Base = Q10 emitter. |
| **Q10** | 2SA733 PNP | Emitter = Q11 base = "**reference node**". Base = "**summing node**". Collector → node E. |
| **R71 2.2K** | | Summing node to node E |

The reference node is fed by **R73 100K** + **TM3 "FREQ" 470K** (rheostat; parts list "H1051A023-470kB"; X0X 500K(B)) from the **VR3 CUTOFF FREQ** wiper.

**Transfer (COMPUTED from topology).** V_BE11 = V_BE10 + V_R71, so

**I_f = (I_S11/I_S10) · I_E10 · exp(V_R71 / V_T)**

where I_E10 = (V_VR3,wiper − V_ref,node)/(R73 + TM3), with V_ref,node ≈ V_E + V_BE11 ≈ 3.8 V.

- The **cutoff knob sets the current linearly**, not exponentially.
- **Env mod and accent sweep** act exponentially, through the voltage they develop across R71.
- Roland's own description (SN82 p.8): "Q10 and Q11 in combination convert control voltage to the current in anti-log fashion … The Q9 has a key role … It developes bias for Q10 and Q11 to set initial cutoff point at the center of frequency range."

**Inputs to the summing node** (R71 = 2.2k returns all currents to node E):

| Input | Path | Sensitivity (COMPUTED, 25 °C) |
|---|---|---|
| ENV MOD | VR5 wiper → **R63 220K** | V_R71 = 10 mV per V of wiper voltage (relative to node E) = **1.78 V per octave** |
| Accent sweep | VR4-B wiper → **R72 100K** | 22 mV per V = **0.81 V per octave** |

**Cutoff and env-mod pots (SN82 p.5; RW-mods confirms values).**

- **VR3 CUTOFF FREQ 50K(A)**: pin 3 → +12 V; pin 1 → **R47 10K** → node E; wiper → TM3 → R73.
  - Parts list: "K161B0-50kA CUT OFF FREQ, ENV MOD".
  - Wiper range (unloaded) = V_E + (12 − V_E)·(10k + r)/60k = **4.73 V … 12 V**.
  - COMPUTED knob range: I_E10 ∝ (V_w − 3.8 V) = 0.93 … 8.2 V → **≈ ×9 (≈3.2 octaves)**, ignoring pot loading by R73 + TM3.
- **VR5 ENV MOD 50K(A)**: pin 3 ← MEG output; pin 1 → **R61 10K** → node E; wiper → R63.
  - Because pin 1 returns to node E (≈3.27 V) and the MEG rests at ≈0 V, **turning ENV MOD up lowers the resting cutoff as well as increasing the sweep.**
  - SN82 p.8: "Rotating wiper ② of VR5 closer to terminal ③ increases ENV MOD voltage being fed to base of Q10 while changing the bias which in turn lowers VCF cutoff frequency … This is equal to turning CUTOFF knob counterclockwise".

**Env-mod excursion (COMPUTED; MEG rest 0 V, MEG peak assumed ≈10 V, UNCERTAIN).** Wiper − V_E = (V_MEG − V_E)·(10k + r)/60k.

| Setting | At rest | At peak | Total sweep |
|---|---|---|---|
| Min | −0.55 V → −0.31 oct | +1.12 V → +0.63 oct | ≈0.94 oct (never zero, matching RW-unique) |
| Max | −3.27 V → −1.84 oct | +6.73 V → +3.78 oct | ≈5.6 oct |

The R61 10K in the min-setting factor is what keeps env mod from ever reaching zero (RW-unique: "does not allow this effect to be turned down to zero").

**No keyboard tracking.** Nothing connects the CV to the filter (RW-DFM p.5, p.15).

### 4.6 Calibration, ranges and test-point data

- **Adjustment (SN82 p.7, VCF):** "Check point: TP6. Panel settings: CUTOFF FREQ: center, WAVEFORM: SAWTHOOTH [sic], RES: Full clockwise, ENV MOD, DECAY, ACC: Full counterclockwise. Afdjust [sic] TM3 for: [decaying ring burst] 2ms ± 0.5ms."
  - So the resonance ringing period is 2 ms (**≈500 Hz; 400–667 Hz tolerance**) at mid cutoff with minimum env mod.
  - The p.5 inset shows the same burst: "2ms±.5ms", levels "5.8V" / "5.2V", repeating "(15 ms KEY C: see ADJ. VCF)", which is the 65.4 Hz saw period.
- RW-DFM p.6: "The maximum resonant frequency of the filter, with no Env Mod or Filter Tracking, is approximately 5 kHz. This is an octave above the typical maximum for the TB-303." So the stock maximum is ≈2.5 kHz.
- COMPUTED consistency check: VR3 is A-taper, ≈15 % of track at mid rotation (UNCERTAIN). That gives V_w(mid) ≈ 5.8 V → I(max)/I(mid) ≈ 8.2/2.0 ≈ 4 → ≈2 kHz at full CW for 500 Hz at mid. This is consistent with Whittle's ≈2.5 kHz.
- The absolute constant linking I_f to the cutoff frequency is not derived here; use the TM3 calibration point above. The cutoff scales ∝ I_f/C.

---

## 5. Main envelope generator (MEG) and accent (SN82/KL p.5 "ENV"; X0X; crops `kl-meg.png`, `sn82-p5-meg.png`, `x0x-env.png`, `kl-ic12-decay-routing.png`, `kl-vr6-vr7-routing.png`)

The block diagram (p.3) shows "MONO MULTI" → diode → C62 → "Q40, Q41", with "DECAY TIME VR6" bypassed by an "ACC. ON/OFF" switch and R138.

### 5.1 Trigger: Q38 one-shot

**Circuit.**

- **Q38 2SA733 PNP.**
  - Emitter: **R150 22 Ω** from +12 V, with **C55 47/16** reservoir to ground.
  - Base: **R149 10K** to +12 V, plus **R148 10K** in series with **C54 0.047 µF** from the Q37 collector (inverted gate).
  - Collector: **R151 22K** to ground.
- Each **rising gate edge** (falling Q37 collector) produces a short positive pulse at the Q38 collector.
  - The base drive decays with ≈ C54·R148 = 0.47 ms.
  - Collector pulse ≈1–3 ms (COMPUTED estimate, β-dependent; UNCERTAIN).

**Where the pulse goes.**

- Through **D37 + R152 100 Ω** into **C62 1/35 (1 µF)**: the MEG attack. τ = 100 Ω × 1 µF = 0.1 ms, effectively instantaneous.
- Up to **D36** → the VCA envelope trigger (§6.2).

**Retrigger rule.** Only gate *rising edges* retrigger, so **tied/slid notes do not retrigger either envelope** (RW-slide).

### 5.2 Hold capacitor, buffer and decay

**Buffer.** **C62 1 µF**, buffered by a Darlington:

- SN82 draws **Q41 → Q40**; X0X labels the halves the other way round.
- Both collectors go to +12 V.
- Output emitter load: **Q39 2SK30(Y)** JFET with gate tied to source, i.e. a constant-current sink. Y grade I_DSS ≈ 1.2–3 mA (UNCERTAIN).
- The output is the **MEG** line. It feeds VR5 pin 3 and IC12 pin 1.

**Decay path.** C62 → **R138 68K** → "DECAY" line → **VR6 ENV DECAY 1M(A)** (wired as a rheostat: wiper tied to pin 3) → the IC12 pin 4 node → series diodes **D28, D27, D26** to ground.

- The middle diode is "D27" in SN82/KL, which duplicates the VCA's D27; X0X calls it D29.
- **R117 22K** from ⊕ biases the D27/D26 junction (≈0.2 mA).
- The diode string sets the decay floor at ≈2–3 diode drops. That roughly cancels the Darlington's 2·V_BE, so the MEG output rests at ≈0 V.

**MEG peak (COMPUTED, UNCERTAIN).** V_C62 ≈ 12 − V_CE(sat) − V_D37 ≈ 11.2 V, so the output ≈ 11.2 − 2·0.6 ≈ **10 V**.

**Decay time constant: τ = C62·(R138 + VR6) = 1 µF × (68k … 1.068 MΩ) = 68 ms … 1.07 s** (A-taper, so roughly exponential vs. rotation).

| | τ | Time to 10 % (2.303 τ) | Printed on SN82 p.5 |
|---|---|---|---|
| VR6 min | 68 ms | 157 ms | "DECAY VR MAX T=2.5 sec., MIN T=200ms" (100 % → 10 % sketch) |
| VR6 max | 1.068 s | 2.46 s | (same note) |

RW-DFM p.4: "MEG had a decay time between 200 ms and 2 seconds".

### 5.3 Accent behaviour of the MEG

- **IC12 switch B** (pins 3↔4, control pin 5 = ACCENT) shorts VR6 (DECAY line ↔ diode-string node) on accented notes.
- **On accented notes the decay is forced to the minimum: τ = 68 ms** (T(10 %) ≈ 157 ms), whatever the DECAY knob says.
- RW-unique: "On accented notes, this is shorted out and MEG runs to the relatively short time which is the same as when the Decay pot is fully anti-clockwise". RW-DFM p.4: "On accented notes, the decay time was fixed to 200 ms."
- **IC12 switch A** (pins 1↔2, control pin 13 = ACCENT) passes the MEG to **VR7 ACCENT 50K(B)** pin 3 only on accented notes. Pin 1 → ground; wiper = V_acc.
- Parts list: "K161B0-50kB TUNING, ACCENT".
- V_acc feeds:
  - the VCA (§6.4), through R119 47K + C36;
  - the accent sweep (§5.5), through D24 + R46 47K.

### 5.4 ENV MOD pot

See §4.5 (VR5 50K(A), R61 10K to node E, R63 220K to the summing node).

### 5.5 Accent sweep circuit (VR4 gang B)

**Path.** V_acc → **D24** (anode at the VR7 wiper) → **R46 47K** → VR4-B pin 1 (CCW end).

- 50k track → pin 3 (CW end) → **C13 1 µF** to ground.
- Wiper → **R72 100K** → summing node (≈ node E ≈ 3.3 V).
- SN82, KL and X0X agree. RW-unique describes the same structure but calls the pot "100k". SN82, the parts list and RW-mods all say dual 50 kB, so the pot is 50K.

**Behaviour (COMPUTED).**

- At rest C13 bleeds to the summing-node voltage (≈3.3 V) through the track and R72. D24 therefore conducts only once V_acc ≳ 3.3 + 0.6 ≈ **3.9 V**, so low ACCENT settings give no sweep.

| Resonance setting | What the wiper sees | Time constants |
|---|---|---|
| **Fully CW (max)** | The wiper is C13 itself | Charge τ = ((R46 + 50k) ∥ R72)·C13 = (97k ∥ 100k)·1 µF ≈ **49 ms** while D24 conducts. Discharge (D24 off) τ = R72·C13 = **100 ms** into the summing node. |
| **Fully CCW (min)** | Direct jump of ≈(V_acc − V_D − V_s)·(100∥50)/(47 + 100∥50) = 0.42 × …, rising toward 100/147 = 0.68 × … | τ ≈ (50k + 47k∥100k)·1 µF ≈ 82 ms. RW-unique: "basically 100/147 of the MEG voltage minus the capacitor voltage". |

- **At max resonance** the result is a smooth "wow": a rise over ~50 ms, overlaid on the MEG's own 68 ms accent decay, then a 100 ms fall.
- **Repeated accents** within a few hundred ms start from a partly charged C13 and peak higher each time. This is the well-known escalating accent (RW-unique; RW-DFM p.5: "some charge remains in a capacitor (C13)").
- Sweep sensitivity: **0.81 V per octave** at the VR4-B wiper.
- COMPUTED example: max accent with a ≈10 V MEG peak gives ≈3–4 V at the wiper, i.e. **+4–5 octaves** before the ladder saturates (UNCERTAIN; depends on the MEG peak).

---

## 6. VCA and its envelope (VEG) (SN82/KL p.5 "VCA"; X0X; crops `kl-vca.png`, `sn82-p5-vca.png`, `x0x-vca.png`, `x0x-vca-vcf-mid.png`, `sn82-p5-vca-input-caps.png`)

### 6.1 The VCA element

**IC15 BA662(A)**, Rohm "Vari-conductance amp": an OTA plus an output buffer, SIP-9.

**Pinout (SETTLED).** SN82 p.2 (crop `sn82-p2-ba662-pinout.png`): 1 = C (I_ABC), 2 = −IN, 3 = +IN, 5 = −V, 6 = OUT1, 7 = IN2, 8 = OUT2, 9 = +V; pin 4 is drawn unlabelled. The p.5 schematic (SN82, KL, X0X) agrees and adds pin 4: 1 ← Q31's collector and D27, 2 ← R125, 3 ← R124 and the two input branches, 4 and 9 → +12 V, 5 → ground, 6 → 7 with the output load, 8 → C38. Open Music Labs' reverse engineering of an original (below) and Alfa's AS662 clone (pins 1–8 of its SOIC-16 in the same order) agree: **pin 4 is the buffer's bias input**, tied to the supply as in most circuits. The "web pinout" with pin 2 as a diode bias, 4 −IN, 5 output and 6 −V is the **BA6110**'s family (a different Rohm part with linearising diodes, "different pinout for the first 4 pins"), not the BA662's. The BA662 has **no linearising diodes**.

**Inside (from an original chip; 2026-10-07).** No Rohm data sheet has been found. Sources, copies in `private/reference/ba662/` (with the pages they come from):
- Open Music Labs reverse engineered an original BA662 by driving its pins (http://wiki.openmusiclabs.com/wiki/BA662, .../x0xb0x): a two-transistor current mirror takes I_ABC into the tail of an NPN differential pair (as the CA3080); the pair's collectors go to three-transistor Wilson mirrors, a PNP pair and an NPN one, giving a push-pull output at pin 6 (as the LM13700); the buffer is an NPN emitter follower on a current sink set from pin 4, into a PNP emitter follower with a pull-up to +V (as the BA6110's). Measured on the original: ~500 Ω emitter resistors on the control mirror, a 1:1 buffer bias mirror, the buffer's pull-up (10k); input bias current ~150 nA; input offset 30 µV (the "selected" BA662A from a TB-303) and 250 µV (another); buffer offset −5 and +25 mV, a strong function of the pin 4 voltage. OML built a discrete clone to it.
- Alfa's AS662 (dual BA662, 2021 data sheet v1, `alfa/AS662D.pdf`, SHA1 93d33e20...): the same circuit, with values: control mirror emitter resistors ~400 Ω, buffer bias resistor ~40k from BUF BIAS, buffer NPN collector resistor ~500 Ω, pull-up ~10k (±20 %). AMSynths gives its gm as 7700–9100 (centre 8500).
- Rohm's BA6110 data sheet (`rohm-ba6110/BA6110.pdf`, SHA1 57609786...), same maker and buffer: pull-up 11k; gm 4800/8000/12000 µS min/typ/max at I_ABC 500 µA (±15 V); pin 6 swings to within ~1 V of the rails; the design formula is the ideal A_v = I_ABC/52 mV × R_O.
- AMSynths, "All about the BA662 chip" (https://amsynths.co.uk/2018/01/07/all-about-the-ba662-chip/): Roland graded every BA662 by transconductance into nine colour-dot grades (yellow = "gm 8080"; conditions not given) and the A version for lower offset. (Our notes also listed a CoolAudio "V662A"; AMSynths mentions it as an AS662 pin-compatible part with a wide gm spread, but no data sheet was found.)

**SPICE model.** `tools/spice/ba662.lib` builds the chip from these: OML's circuit with Alfa's resistor values and generic IC transistors of the period (vertical NPN, lateral PNP, substrate PNP). Its gm at I_ABC 500 µA is 8430 µS (AS662 centre 8500, BA6110 typ 8000; the ideal I_ABC/2V_T is 9670). In the 303's circuit (`tools/spice/out.cir`, `compare_out.py`):
- **Gain**: a little under the ideal I_ABC/2V_T, more so as I_ABC rises: −0.26 dB at 48 µA (a plain note's peak), −0.7 dB at 170 µA (full accent), mostly the pair's base currents through R124/R125 2.2k and its emitter resistance. The output saturates at 98.5 % of I_ABC. Across the chip's unknowns (transistor gains 80–400 / 20–100, emitter resistors 350–550 Ω): −0.14 to −0.6 dB at 48 µA.
- **Swing at pin 8**, about its ~5.3 V bias: down to −4.5 V (the OTA's NPN mirror and the buffer's sink saturate at ~0.8 V; −4.4 to −4.5 across the spread), up to where the 10k pull-up can no longer lift pin 8 against its load through C38: VOLUME, R155 and the mixer, 25k at full VOLUME (+4.7 V; +5.0 in SPICE, where C38's charge shifts) to ~50k lower down (+5.5 V).
- **Pin 1** sits at a diode drop plus the emitter resistor: 0.60 V at 5 µA, 0.68 V at 48 µA, 0.76 V at 170 µA (the NPN's saturation current is a typical IC value, ±0.04 V). This matters for Q31 and the accent current (§6.2, §6.4).
- **Offsets** put a small step of the VCA envelope through to the output at each note. The SPICE chip's own (its mirrors' base currents don't cancel, ~90 µV input-referred) shifts pin 8 by +4.3 mV at 48 µA and +14 mV at 170 µA, a −2 to −8 mV step at the jack. Real chips' offsets are 30-250 µV of either sign (OML), so the step is unit-specific; the voice model's BA662 has none.

**Input bias.**

- **R126 2.2K** from ⊕ to a node decoupled by **C37 10/16**.
- Into pin 3 through **R124 2.2K**.
- Into pin 2 through **R125 2.2K**.
- KL marks the R126 value "ROLAND TYPO 2.?K"; X0X: 2.2K.

**Output load.** From pin 6/7 to a 5.333 V node (⊕ through **R162 2.2K**, decoupled by **C40 10/16**):

- **R128 220K** ∥ (**R129 22K + R127 47K**).
- X0X prints R127 as "46K".
- COMPUTED total ≈ 220k ∥ 69k = **52.5 kΩ**.

**Output coupling.** Pin 8 → **C38 1/50** → VOLUME (§7).

**Gain (COMPUTED).** g_m ≈ I_ABC/(2V_T) = I_ABC/51.7 mV (a little less, see above), and the voltage gain from pin 3 to the output ≈ g_m·52.5k (≈48 at I_ABC = 47 µA). The input divider is ≈2.2k/220k = 1/100, so the overall gain from VCF OUT ≈ 0.5 at the VEG peak with minimum resonance.

### 6.2 Volume envelope generator (VEG): Q31/Q32, C42

**Trigger.** Q38 pulse → **D36** → **R134 22K** → base of **Q32**, with **C41 0.1 µF** from base to ground (τ = 2.2 ms smoothing).

- Q32 is NPN: emitter **R130 100 Ω** to ground; collector → **R132 100 Ω** → node N.
- Q32 rapidly (≈0.2 ms) discharges **C42 1/35 (1 µF)** at node N.

**Recharge.** **R123 1.5M** to +12 V recharges C42: **τ_VEG = 1.5 MΩ × 1 µF = 1.5 s.**

**Current source.** **Q31 2SA733 PNP.**

- Base = node N.
- Emitter supplied from **GATE_INT** through **D35 → R133 2.2K → R131 220K**.
- Collector → IC15 pin 1 (I_ABC).

**Envelope current (COMPUTED):** I_VEG(t) = [V_GATE − V_D35 − V_EB31 − V_N(t)] / 222.2k, with V_N(t) = 12 − (12 − V_N0)·e^(−t/1.5 s).

- Peak ≈ (11.9 − 0.6 − 0.6 − ~0.5)/222.2k ≈ **47 µA**.
- The current falls **exponentially toward a negative asymptote**, i.e. it reaches 0 at t ≈ 1.5 s × ln(11.5/1.3) ≈ **3.3 s**.
- RW-DFM p.4: "The TB-303's VEG decay was fixed at ~ 3 to 4 seconds". RW-unique: "Sharp attack, exponential decay, fixed decay and rather long time … Drives the VCA and does nothing else".
- **No front-panel control affects the VEG.** The DECAY knob only changes the MEG.

**Q32 lets go slowly (SPICE, `tools/spice/env.cir`; 2026-10-07).** C41 is charged through D36 and R134 during the one-shot pulse, but after it D36 blocks, so **C41 can only discharge through Q32's own base**. Q32 therefore stays on for some 100 ms, its collector current falling roughly as β·C41·V_T/t (~8 µA at 100 ms with β 300), and holds C42 near 0.1–0.2 V against R123's ~8 µA. The VEG starts to fall about half as fast as R123 alone would make it: at 240 ms after the gate the VCA current is 43.3 µA, not 39.9 µA (−0.7 dB less decay). How long the hold lasts scales with Q32's β (2SC945 rank not known).

**Q31 starts saturated.** Pin 1 sits at ~0.7 V (§6.1) while Q31's base (C42) starts each note near 0.1 V, so Q31's base-collector junction is forward biased for the first tens of ms. On a plain note that only costs a little collector current; on an accented note, **part of the accent current (§6.4) flows from pin 1 through Q31's base-collector junction into C42** instead of into the VCA, until C42 has charged (with Q32 holding it, it hardly does). This takes −1.2 to −1.7 dB off the accent (§6.4). How much depends on pin 1's voltage against Q31's junction: the earlier assumption of a discrete transistor at pin 1 (V_BE ~0.55 V) hid it.

### 6.3 Gate-off release

- Because Q31's emitter supply is GATE_INT, **the VCA closes when the gate ends**, after the C72 hold/fall described in §2.4.
- RW-DFM p.6: "about 16 ms – 8 ms of normal volume and 8 ms of linear decay".
- The **note start** is likewise delayed by the C72 turn-on (≈4–5 ms), even though the VEG capacitor was already discharged at the gate edge.

### 6.4 Accent level boost

**Path.** V_acc (VR7 wiper) → **R119 47K** → node with **C36 0.033 µF** to ground → **R120 22K** → **D27** (cathode at pin 1) → IC15 pin 1. This current **adds to I_VEG** and is **not gated by GATE_INT**.

- Accented notes therefore start without the C72 delay and keep sounding briefly after gate-off, decaying with the 68 ms accent MEG.
- RW-slide: "unless it is an accented note, in which case the Main EG drives the VCA as well". RW-unique: "Via an RC network of a 47k and a 0.033uF to soften the attack a little … This is the primary reason why accented notes are louder."

**Smoothing (COMPUTED).** 47k·33 nF = 1.55 ms; Thevenin (47k ∥ 22k)·33 nF = 0.49 ms.

**Steady-state current.** ≈(V_acc − V_D27 − V_pin1)/69k. At maximum ACCENT and a ≈10 V MEG peak this is ≈**125 µA**, about 2.7 × the VEG peak, so up to ≈+11 dB at the start of an accented note (COMPUTED, with pin 1 at 0.6 V and none of it lost).

**Simulated (SPICE with the BA662 model, §6.1, §6.2).** Pin 1 sits at ~0.75 V on an accent and part of the accent current goes through Q31 into C42, so the VCA current boost is **+4.1 dB at half ACCENT and +9.0 dB at full** with typical parts (+3.7 to +4.7 and +8.3 to +9.8 dB across the parts' spread; with a discrete-transistor pin 1 it was +5.4 and +10.7 dB).

### 6.5 VCA input mixing: resonance level compensation, and the input high-pass

Two series R-C branches feed **pin 3** (IC15 +IN). Both editions and KL read **.01**:

| Branch | Components | Source |
|---|---|---|
| 1 | **R121 220K + C21 0.01 µF** | **VCF OUT** (C14) |
| 2 | **R122 100K + "C22" 0.01 µF** (X0X: C20; SN82 shows a hand-correction mark on "C22", which is also the designator of a VCF part) | **VR4-A RESONANCE wiper**, i.e. the feedback-attenuated output |

The block diagram (p.3) shows the two "+ +" inputs on IC15, one from the VCF AMP and one from the RES pot. The sum develops across R124 2.2k to AC ground.

**Level compensation (COMPUTED).** Input ∝ V_out·(1/220k + k_res/100k). Turning resonance from 0 to max raises the level fed to the VCA by up to **×3.2 (+10 dB)**, compensating the passband loss of the resonant ladder.

**High-pass corners (COMPUTED):**

| Branch | Corner |
|---|---|
| R121/C21 | 1/(2π·222.2k·10 nF) = **71.6 Hz** |
| R122/C22 | 1/(2π·102.2k·10 nF) = **156 Hz** |

These are first-order roll-offs of the bass.

- RW-DFM p.6: "In the TB-303, 32 Hz was down by 5 dB".
- COMPUTED for branch 1 alone: −7.8 dB at 32 Hz relative to high frequencies, or −5.6 dB relative to 100 Hz.

---

## 7. Output stage (SN82/KL p.5 "MIXER", "HEADPHONE"; X0X; crops `kl-mixer.png`, `kl-headphone.png`, `x0x-mixer-headphone.png`, `sn82-p5-pots-lower.png`)

**Path.** VCA (C38 1 µF) → **VR8 VOLUME 50K(A)**: pin 3 = signal, pin 1 = ground. The pot is ganged with the POWER switch; parts list "VM11R851A-5M1411-50KA master vol. & power switch". Its wiper feeds the mixer.

**Mixer (Q33/Q34).**

- **Q34** NPN, common base:
  - Base at ⊕ through **R157 10K**, AC-grounded by **C52 10/16**.
  - Emitter = the summing input, a virtual ground ≈4.7 V.
  - Collector load **R154 100K** from a local rail (+12 V through **R136 100 Ω**, decoupled by **C53 47/16**).
  - Collector → **R158 10K** → base of **Q33** (2SA733 PNP; emitter on the local rail).
- Q33's collector is the output, with **R160 10K** to ground.
- Feedback **R153 100K** from the output back to the Q34 emitter.

**Inputs to the Q34 emitter.**

| Input | Path | Gain (COMPUTED) |
|---|---|---|
| Instrument (volume wiper) | **R155 47K + C59 1/50** | −100k/47k = **−2.13** |
| **MIX IN** (J1) | **R156 100K + C58 1/50** | **−1.0**. Spec: "MIX In: Impedance 100kΩ, Gain: Unity" (SN82 p.1) |

**Outputs.**

| Output | Path |
|---|---|
| **OUTPUT J6** | Mixer out → **R161 1K** → **C56 1/50** → J6, with **R165 100K** to ground. Spec: "Output: Impedance 10kΩ" (SN82 p.1, OM p.90). |
| **HEADPHONE J2** | Mixer out → **R159 33K** → **C51 2.2/50** → **IC14 LA4140** (AF power amp, on +6 V) pin 2. Other parts: C50 10/16 + **R135 4.7K** at pin 3 (feedback/gain), C47 .0068, C45 .068, C44 47/16, C46 .01, C48 100/6.3, C49 100/6.3, D32/D33. Output pin 6 → **C43 1000/6.3** → **R163, R164 10 Ω ×2** → stereo jack J2 (both channels). Spec: "Headphone: Impedance 8Ω to 30Ω". |

**Output-path high-pass corners (COMPUTED):** all below the VCA-input corners of §6.5, so §6.5 dominates the bass roll-off.

| Section | Corner |
|---|---|
| C38 into VR8 50k | 3.2 Hz |
| C59 + R155 | 3.4 Hz |
| C56 into 100k | 1.6 Hz (14.5 Hz into an external 10 kΩ load) |
| C51 + R159 | 2.2 Hz |

---

## 8. Adjustment procedure (SN82 p.6–7, transcribed; crops `sn82-p6-adjust-*.png`, `sn82-p7-adjust-*.png`)

> **ADJUSTMENT.** Adjustments should be done from the component side. Use digital voltmeter or oscilloscope, as appropriate.
>
> **TEMPO.** Check point: TP1. Turn TEMPO full clockwise. Adjust TM1 for: [square wave] **8ms ± 1ms** [period].
>
> **INT CLOCK.** Check point: TP2. Adjust TM2 for: [negative pulses] **1.8ms ± 0.2ms**.
>
> **CAUTION:** The following adjustments will be done in PATTERN WRITE mode. If existing rhythm patterns need to be restored, locate a PATTERN that contains no rhythms or the easiest ones to copy and write it down.
>
> **CV.** Check point: CV out jack. With PITCH mode, press low C key and note the reading – CVL. Press high C key, adjust **TM6** for **CVL + 1.000V ± 3mV** – CVM. While holding down TRANSPOSE UP, press high C key, confirm that the reading is **CVM + 1.000 ± 3mV**.
>
> **VCO.** Check point: Q28 source or S1 WAVEFORM terminal. Set TUNE in its middle position. With PITCH mode. While tapping CL and CH keys alternately, adjust **TM5 WIDTH** for **2:1** waveforms: [two overlaid saws, "WIDTH"]. Press A key and adjust **TM4** for **110Hz**. Confirm **4:1 ± 0.5%** with CL and CH + TRANSPOSE UP.
>
> **VCF.** Check point: TP6. Panel settings: CUTOFF FREQ: center; WAVEFORM: SAWTHOOTH; RES: Full clockwise; ENV MOD, DECAY, ACC: Full counterclockwise. Afdjust **TM3** for: [ringing bursts] **2ms ± 0.5ms**.

**Interpretation.**

- TEMPO: 8 ms at full CW = 300 BPM at 24 ppqn (200 ms/24 = 8.33 ms).
- The 16th-note clock grid is 6 clocks per 16th (RW-slide). The gate is high for 3.5 clocks and low for 2.5 on a normal 16th.

**Trimmers in the parts list:** H1051A016-47kB, H1051A021-220kB, H1051A023-470kB (TM3) and H1021A011-4.7kB. TM5 and TM6 are drawn as 4.7K. Assignments of 47k/220k to TM1/TM2/TM4 are UNCERTAIN.

---

## 9. Test-point waveforms and voltages printed on the schematic (SN82 p.5 / KL)

| Where | Printed data |
|---|---|
| Header near TM4/TM5 | "TUNE: 110Hz (SET TUNING [knob centred]; CV OUT: 2.75V (A KEY)); WIDTH: 1 OCT/VOLT" |
| Power section | "CV KEY C=1.000V, C̿=4.000V" (CV out at the lowest C and at the C three octaves higher, 1 V/oct); "5.333V", "12V", "+6V". The p.6 board view also shows "15V", "5.33V", "12V", "6V". |
| Saw (Q28 source) | Falling ramp, peak "12V", trough "5.5V", "GND" baseline |
| Square (Q8 collector) | Pulse between "8V" and "5V" with "GND" baseline, "(9ms KEY A)" |
| TP6 (VCF out) | Ring bursts "2ms±.5ms", envelope between "5.8V" and "5.2V", "(15 ms KEY C: see ADJ. VCF)" |
| MEG decay | Sketch 100 % → 10 % over T: "DECAY VR MAX T=2.5 sec., MIN T=200ms" |
| TP4 | IC11b output (CV after slide) |
| TP5 | IC16 5.333 V |
| TP1/TP2 | Tempo / CPU clock (see §8) |

---

## 10. Published specifications

**SN82 p.1 (verbatim where quoted).**

- Pattern memory 64 patterns; contents "Pitch, Value, Accent, Slide".
- Scale "3 octaves (PATTERN) w/TRANSPOSE, 4 octaves (TRACK) w/Key shift".
- "TUNING Control: approx. ±700 cents (perfect fifth)"; "TEMPO Control: (♩ = 40 to 300)".
- Connections: "Output: Impedance 10kΩ; Headphone: Impedance 8Ω to 30Ω; Gate Out: OFF: 0V; ON: +12V; CV Out: +1V to +5V (1 volt/oct); SYNC In: DIN connector; MIX In: Impedance 100kΩ, Gain: Unity".
- Power "6V: 1.5V battery × 4; 9V: AC adaptor; 80mA to 120mA". Size 300 × 146 × 55 mm, 1.0 kg.

**OM p.89–90.** Same, except "TUNING Control: (±500 cents)". Also "Mini-Jack, OFF 0V; ON +12V" and "CV Out … +1V–+5V (1 volt/oct)".

**OM p.33 (tone controls).**

- CUTOFF FREQ "will begin to shave off the upper harmonics … making the tone softer and reducing the volume".
- DECAY: "Both the volume and the tone will take a longer time to fade if the knob is turned clockwise".
  - Note: in the circuit DECAY only changes the MEG (filter env + accent). The owner's-manual wording is marketing-level.

**Pot list (parts list p.8).**

| Part | Function |
|---|---|
| K161100FAE-1MC | tempo |
| VM11R851A-5M1411-50KA | master vol. & power switch |
| K161B0-50KB | TUNING, ACCENT |
| K161B0-50kA | CUT OFF FREQ, ENV MOD |
| K161B0-1MA | DECAY |
| K162T00W-50kB × 2 | RESONANCE |

---

## 11. Discrepancies and uncertainties

1. **R110/R111** (VCF output-amp collector loads): SN82 = 2.2K; SN86, KL and X0X = 22K. **22K is correct** (DC levels at TP6, §4.4).
2. **TM3**: 470K (SN, parts list 470 kB) vs 500K(B) in X0X. **TM5**: 4.7K vs 5K(B). **TM4**: value illegible in SN (X0X 50K(B); probably 47 kB).
3. **R102**: printed "100" with a stray "K" in SN; X0X 100K. 100K is used here.
4. **R126**: illegible "2.?K"; KL annotates "ROLAND TYPO"; X0X 2.2K.
5. **Duplicate designators**: "C22" is both the VCF Q18-bias 1 µF and the VCA input 0.01 µF (X0X renames the latter C20). "D27" is both the VCA I_ABC diode and the middle MEG floor diode (X0X: D29).
6. **R127**: 47K (SN) vs 46K (X0X).
7. **Resonance/accent-sweep pot**: dual 50K B per SN, the parts list and RW-mods. RW-unique's "100k pot" is wrong for the stock unit.
8. **Transistor part numbers**: the p.4 note (2SC945(P)/2SA733(P)) vs the parts list (2SC2021(R)?). X0X uses 2SC536F; production used 2SC536F, 2SC1815GR and 2SC945P, all of similar gain (see "Gain ranks").
9. **Tuning range**: ±700 cents (SN) vs ±500 cents (OM). The computed range with VR2 at 0–5.333 V is ±880–980 cents. The supply symbol on VR2 pin 3 is unlabeled in SN.
10. **D31 clamp**: to ⊕ (SN) vs +12 V (X0X).
11. **Strobe/slide net**: SN draws IC9 CLK, IC13 CLK and SLIDE ON/OFF as one net. Assignment of connector lines 7/8 inferred from wire order.
12. **Square-wave duty cycle**: not determinable from values alone (40–65 % by simple analysis); simulate Q8/C11/R45.
13. **Feedback-loop HPF**: C25/C27 (0.1 µF) into Q21's input impedance gives ≈50–80 Hz depending on β and ladder current. C23 gives 13 Hz.
14. **MEG peak voltage** (≈10 V) and therefore the absolute env-mod and accent excursions are estimates.
15. **Power-supply details** (D43 type, some converter values) are partly illegible and not needed for the voice.
16. **Q38 pulse width and Q36 release timing** are computed estimates; Whittle's measurements (RW-DFM) are the better reference.

---

## 12. Quick reference: time constants and key quantities

| Quantity | Value | Basis |
|---|---|---|
| DAC step | 5.333 V/64 = 83.33 mV (1 semitone); V = (N+1)·83.33 mV | R74–R90 200k, R78 to 5.333 V |
| DAC source resistance | 100 kΩ | R-2R |
| Slide τ | 100k × 0.22 µF = **22 ms** (10–90 % 48 ms) | C35 |
| VCO | f ≈ 110 Hz·2^(V_CV − 2.75); C33 = 0.01 µF; saw ≈5.5→12 V falling; ≈15.4 Hz/µA | Q26 expo, TM4/TM5 |
| Expo CV divider | R106 27k + TM5 (≈3.9k) : R100 560 Ω PTC; I_ref ≈43 µA (R103 220k + R102 100k/TM4) | §3.1 |
| Square shaper | Q8 PNP; R45 22k / C11 1 µF emitter (τ 22 ms), R36 10k to 5.333 V; base R35 100k ∥ (C10 10 nF + R34 10k); output 5.333 ↔ ≈8 V | §3.5 |
| Ladder caps | **C18 0.018 µF (bottom)**, C19 = C24 = C26 = 0.033 µF | §4.1 |
| VCF input | 220k : 2.2k (÷101) via C17 1 µF → ≈64 mV p-p saw | §4.2 |
| Resonance feedback | VR4-A k × Q18 × 2.2/12.2 (0.18); C15 1 µF, C23 1 µF (13 Hz HPF); output coupling C25/C27 0.1 µF (≈50–80 Hz in-loop HPF) | §4.3–4.4 |
| Output amp | Q21 diff pair, 22k loads, I_tail 0.54 mA, gain ≈115; Q19 EF; C14 1 µF | §4.4 |
| Filter expo | I_f ∝ I_ref(cutoff pot, linear) · exp(V_R71/V_T); R71 2.2k; env mod R63 220k (1.78 V/oct); accent R72 100k (0.81 V/oct) | §4.5 |
| Cutoff calibration | ≈500 Hz (2 ± 0.5 ms ring) at knob centre, res max, env mod min; max ≈2–2.5 kHz | §4.6 |
| MEG attack | C62 1 µF via R152 100 Ω (0.1 ms), Q38 one-shot pulse ≈1–3 ms | §5.1 |
| MEG decay τ | (68k + 0…1M)·1 µF = **68 ms … 1.07 s** (T10% 157 ms … 2.46 s; SN: 200 ms … 2.5 s) | §5.2 |
| MEG accent | decay forced to 68 ms (IC12B shorts VR6) | §5.3 |
| Accent sweep | D24 + R46 47k → VR4-B 50k → C13 1 µF; wiper → R72 100k; τ_charge ≈49 ms, τ_discharge 100 ms (res max) | §5.5 |
| VEG | C42 1 µF / R123 1.5 MΩ: **τ = 1.5 s**; I_ABC ≈47 µA peak, reaches 0 at ≈3.3 s; Q32 holds C42 low for ~100 ms after the pulse (SPICE) | §6.2 |
| VEG trigger | R134 22k · C41 0.1 µF = 2.2 ms charging; C41 then empties only through Q32's base (~100 ms) | §6.2 |
| Gate on delay / off release | C72 10 µF on Q36: ≈4–5 ms delay; ≈8–10 ms hold + ≈8 ms fall (Whittle 16 ms total) | §2.4 |
| Accent VCA boost | R119 47k / C36 33 nF (τ 1.5 ms) / R120 22k / D27 → I_ABC; up to ≈125 µA computed; SPICE +4.1 dB (half) / +9.0 dB (full), part lost through Q31 | §6.4 |
| VCA input HPF | 71.6 Hz (R121 220k/10 nF), 156 Hz (R122 100k/10 nF); resonance adds up to ×3.2 level | §6.5 |
| Mixer | instrument gain −2.13 (R153 100k / R155 47k); MIX IN gain −1 | §7 |
