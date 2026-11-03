# Semiconductors (models.cir)

The (S) transistors (every single-transistor VCA) are `2SC2603S`, a 2SC2603 whose reverse beta is
fitted to the real units: `s-transistors.md`.

The voicing board's semiconductors, where the schematic uses each, and the models: SPICE's
Gummel-Poon equations without charge storage (`core/circuit.h`), parameters from the data sheets
in `private/docs/datasheets/` (**DS**; `README.md` there lists each file and its source). Sources
as `docs/circuit.md` §0: **SN** p.11 (type callouts), p.2-3 (parts list), p.12 (power supply);
**NAVA** (`Nava_v1.02_parts.csv`).

## 1. Which part where

**Defaults** (SN p.11, bottom centre): "unless otherwise noted All NPN Transistors 2SC2603-F, All PNP
Transistors 2SA1115-F, All DIODE 1SS133"; beside the master section "Q82, Q83: 2SC2878".

**Other callouts on p.11** (searched over the whole page: the OCR index, then each site on the image):

| Part | Where | Designators | Role |
|---|---|---|---|
| **2SD1469** | snare, grid R-5, R-6 ("Q48 2SD1469", "2SD1469 Q47") | Q47, Q48 | the snappy's VCAs |
| **2SA798** (dual PNP, in a dashed circle) | HH grid B-9 ("Q84 2SA-798"), crash E-17, ride E-24 ("2SA 798") | Q84, Q70, Q68; and Q38 (hand clap, the same dual symbol, no type printed; NAVA: 2SA798) | the sample voices' VCAs; the clap's I_ABC source |
| **2SC2878** | master section, grid A-25 | Q82, Q83 | power-on mute |
| **RD18JB2** (zener) | SD (D57), LT (D16), MT (D34), HT (D72) | D16, D34, D57, D72 | 18 V across each voice's 4069UB (VDD-VSS); the voice netlists leave them out (VDD-VSS stays under 11 V) |
| **RD5.6JB2** (RD6.8JB2 before SN 393000) | power supply board | D701 | reset threshold (`power.md`) |
| 1B4B41 bridges | power supply board | D702, D703 | |
| HD14174BP, HD14006BP, HD14070BP, TC4069UBP, TC4011UBP, TC4013BP, HD14040BP, TC4520BP | | IC1-IC10, IC32/33, IC31, IC16/22/36/44, IC57/59/72, … | parts list p.2 |
| M5218L, AN6912, BA662A | | the op-amps; IC29; IC30 | parts list p.2 |

The parts list (p.2-3) has exactly these transistors: **2SA798-G, 2SA1115-28-F, 2SC2603-28-F,
2SC2878-A or B, 2SD1469-R**; diodes 1B4B41, RD6.8JB2, RD5.6JB2, 1SS-133 (and 1SS-133T-77, taped),
RD18JB2-T. The NAVA copy agrees (its 1N4148s stand for the 1SS133s).

**The "(S)" mark.** A circled S sits beside the single-transistor VCAs: Q6 and Q12 (BD), Q50, Q51
(SD), Q19-Q21, Q26-Q28, Q57-Q59 (toms), Q33 (tom noise), Q65 (RS), Q37 (HC), Q85 (HH), Q71 (crash),
Q69 (ride). The service notes don't define it. What can be said:
- It is not a different type: the parts list has one small-signal NPN, the 2SC2603-28-F, and the
  sheet calls out every other type by designator (Q82/Q83, Q47/Q48, the 2SA798s). NAVA fits 2SC2603s.
- On p.11 the same circled S marks C160 (hi-hat clock), with an arrow to "Polystyrene": for
  capacitors it is the dielectric. On transistors the likely reading is "selected" (as the 808's
  "2SC828 (R) selected noise"), the criterion unstated. These transistors all work as Roland's
  saturating VCAs (the signal into one end through tens of ohms, the envelope into the base), where
  the inverse-mode and saturation behaviour (reverse h_FE, V_CE(sat) offset) set the gain and the
  feedthrough, so a selection for those is plausible.
- No data sheet gives the 2SC2603's reverse h_FE. Published SPICE models of small-signal epitaxial
  NPNs of this class (f_T 200-300 MHz) range from about 0.7 (2N3904) to 6-10 (2N2222, BC547): BR 6
  in models.cir is a typical value, not a measured one, and any value in that range is defensible.
  For positions where BR sets the level, a model of their own (`2SC2603S`, the 2SC2603 with BR to be
  fitted) would let it be fitted without moving every other NPN.

## 2. Models

| Model | Parameters | Data sheet numbers | Reasoning |
|---|---|---|---|
| **2SC2603** NPN | IS 1.1e-14, **BF 350**, BR 6, VAF 110, ISE 1e-14, NE 1.5 | Micro Electronics (second source of the Mitsubishi part; no Mitsubishi sheet found): h_FE rank F 250-500 at 1 mA, 6 V (all ranks 90-800; min 50 at 0.1 mA); V_CE(sat) ≤ 0.3 V at 100 / 10 mA; f_T 200 MHz typ; V_EBO 6 V | BF so h_FE at 1 mA, 6 V is 345 (the rank's geometric middle, with VAF and ISE). The sheet has no V_BE, BR or Early figure: IS (0.65 V at 1 mA), BR, VAF are typical small-signal values. Model: h_FE 302 / 324 / 332 at 0.1 / 1 / 4 mA (6 V) |
| **2SA1115** PNP | IS 1.2e-14, **BF 370**, BR 6, VAF 90, ISE 2e-14, NE 1.5 | Mitsubishi: rank F 250-500 at −1 mA, −6 V; min 50 at −0.1 mA; V_CE(sat) ≤ −0.3 V at −100 / −10 mA; f_T 200 MHz; C_ob 6.5 pF | as the 2SC2603: h_FE 348 at 1 mA, 6 V; 274 at 0.1 mA |
| **2SC2878** NPN | **IS 2.2e-13, BF 480**, BR 150, VAF 100, ISE 1e-14, NE 1.5 | Toshiba (2003 and 1983 sheets): V_BE 0.61 V typ at 4 mA, 2 V; h_FE A 200-700, B 350-1200 at 2 V, 4 mA; **reverse h_FE 150 typ** (V_CE −2 V, I_C −4 mA; the 1983 sheet: −2 mA); V_EBO 25 V min; V_CE(sat) 0.042 V typ at 30 / 3 mA; R_ON 1 Ω typ at I_B 5 mA | IS from V_BE; BF the middle of A ∪ B (the parts list allows either); BR the reverse h_FE. Model: V_BE 0.610 V, h_FE 483 at 4 mA; V_CE(sat) 50 mV at 30 / 3 mA (no RC in the solver). V_EBO 25 V: the bases can sit at −15 V when the mute is off |
| **2SD1469** NPN | **IS 5e-14, BF 260, BR 50**, VAF 100, ISE 1e-14, NE 1.5 | Rohm (medium power, 1 A): rank R 180-390 at 3 V, 100 mA; "V_CE(sat) ≒ 6mV (at 1mA/0.1mA)"; V_CE(sat) 0.08 V typ at 0.5 A / 50 mA; f_T 150 MHz; BV_EBO 5 V; I_CBO ≤ 0.5 µA | BF the rank's middle; BR from the 6 mV at a forced β of 10 (Vt·ln[(1 + 11/BR)/(1 − 10/BF)] = 6 mV gives 52; model 6.2 mV). No V_BE on the sheet: IS 5e-14 (0.61 V at 1 mA), between the small-signal parts and the 2SC2878, an estimate. Whittle found a leaky 2SD1469 (Q47, collector-base) in one unit (`web/whittle`) |
| **2SA798** dual PNP | **IS 2.2e-14, BF 480**, BR 6, **VAF 35**, no ISE | Mitsubishi: rank G 400-800 at −1 mA, −6 V; h_fe 600 typ, h_oe 30 µS at 1 mA, 6 V (p.4); V_BE ≈ −0.63 V at −1 mA (I_C-V_BE curve, p.2); h_FE flat from −0.01 to −3 mA, 40-50 % at −100 mA (p.3); |V_BE1 − V_BE2| 1 mV typ, 10 max; h_FE1/h_FE2 0.98 typ | IS from the curve; BF for 554 at 1 mA, 6 V (the rank's middle); VAF from h_oe (27 V) and the I_C-V_CE curves (45-50 V); no ISE (the flat h_FE). Both halves the same model (matched to 1 mV) |
| **1SS133** diode | **IS 1.45n, N 1.71, RS 1.44** | Rohm: V_F ≤ 1.2 V at 100 mA; I_R ≤ 0.5 µA at 80 V (Fig. 2: 25-30 nA at 25 °C); C_T ≤ 2 pF; Fig. 1 (25 °C) digitised: 0.538 V at 0.22 mA, 0.599 at 1.3, 0.653 at 4, 0.727 at 13, 0.784 at 25, 0.825 at 40, 0.921 at 95 mA | least-squares fit, within 12 mV over 0.2-95 mA: 0.60 V at 1 mA, 0.71 at 10, 0.94 at 100. The model before (the 808's 1S2473 fit: IS 2.52n, N 1.752, RS 0.568) was 20 mV low at 0.2 mA and 77 mV low at 95 mA |
| **M5218L** op-amp | `gbw` **7meg**, `sr` **2.2**, `opsat` 13.5 | Mitsubishi 1984 data book (M5218/M5R4558P, ±15 V): f_T 7 MHz typ; SR 2.2 V/µs typ (G_V 0 dB, R_L 2k); V_OM ±14 typ (±12 min) into 10k, ±13 (±10) into 2k; I_B ≤ 500 nA; R_in 0.3-5 MΩ; load current ±50 mA max. The later M5218A sheet: SR 3.0 V/µs, f_T 7 MHz | `gbw` was 10 MHz and `sr` 4 V/µs (first values). Clipping at ±13.5 V, between the 10k and 2k figures |
| TC4011UBP (4011UB_N / 4011UB_P / DCMOS) | MOSFET (`M`) pairs | Toshiba TC4011UBP, Motorola MC14011UB, TI CD4011UB (`docs/circuit/cymbals.md`, "Clock") | the crash's and ride's clocks; the 4069UB models are in `docs/circuit/solver-mosfet.md` |

Model numbers above were checked by evaluating the equations (V_BE and h_FE at the data sheets'
test points).

## 3. The parts that aren't .models

- **AN6912** (IC29, the clap's quad comparator; netlist element `comparator`). DS Panasonic 1997 /
  Matsushita 1983: input offset 2 mV typ, 5 max; bias ≤ 250 nA; common-mode range 0 to V_CC − 1.5 V
  (on the 909, between ground and −15 V: inputs above −1.5 V count as −1.5 V); sink ≥ 10 mA at
  V_O ≤ 1.5 V; V_OL 0.2 V typ at 3 mA; response 1.3 µs. hc.cir's `vos=-2m vcm=-1.5` and the
  element's defaults (isink 16 mA, vsat 0.15 V) agree.
- **BA662A** (IC30, the clap's OTA; element `ota`). No Rohm sheet exists in public. The clones:
  Coolaudio V662A, g_m 8000 µS typ (4800-12000) at I_control 500 µA, pin 6 output ±500 µA typ at
  that current, input offset ≤ 3 mV, pin 1 bias 0.8 µA; Alfa AS662D, g_m 8500 µS typ (7700-9100)
  at 500 µA. An ideal OTA gives I/(2V_T) = 9.7 mS at 500 µA: the parts reach 0.83-0.88 of it. The
  element's `gm` multiplies the ideal transconductance; hc.cir uses the default 1: **gm 0.85** would
  follow the clones' sheets (the HC agent's call). `rs=450` (the control mirror's emitter resistance,
  from the TB-303's BA662 work) stays.
- **HD14174BP** (latches) at 5 V and **HD14006BP / HD14070BP** at 15 V: output drive in
  `interface.md` §1 and `noise.md` §3-4 (DS Hitachi pp. 34, 130, 156; Motorola MC14174B, MC14006B,
  MC14070B 1978 and 1988).
- **Zeners** (DS NEC 1981, RD-JB series; no sheet lists the JB2 sub-rank): RD18JB V_Z 16.8-19.1 V at
  5 mA, Z_Z ≤ 65 Ω (≤ 80 Ω at 0.5 mA); RD5.6JB 5.3-6.0 V, Z_Z ≤ 60 Ω, Z_ZK ≤ 200 Ω at 0.5 mA;
  RD6.8JB 6.4-7.2 V, Z_Z ≤ 40 Ω. The solver has no breakdown (no BV): a zener that conducts would need
  it (none does in the netlists so far).

## 4. Open questions

- The 2SC2603's (and 2SA1115's) V_BE and reverse h_FE: no sheet gives them. The "(S)" positions'
  reverse h_FE is the one that matters (RS and HC levels, the clap's tail).
- What "(S)" selects for.
- The 2SD1469's IS (V_BE) is an estimate.
