# Semiconductors (models.cir)

Every netlist's transistors, diodes and CMOS come from `core/netlists/models.cir`, run in front of
each block (`circuitrun models.cir,<block>.cir`). The solver's devices (`core/circuit.h`): Gummel-Poon
DC without charge storage (IS BF BR NF NR VAF ISE NE ISC NC; no RB, RE, RC, IKF, so ohmic drops and
the high-current fall of h_FE are not modelled), diodes (IS N RS, no breakdown), JFETs and MOSFETs
as SPICE level 1 (the MOSFET's bulk on its source), op-amps as X elements, comparators as B elements.

**Sources.** Data sheets in `private/ms20/docs/datasheets/` (index with URL and SHA1:
`private/ms20/docs/SOURCES-gathered.md`). "NEC 1981" is the *1981 NEC Discrete & Optoelectronics
Data Book* there (`2sc945/1981_NEC_Discrete_Optoelectronics_Data_Book.pdf`), page numbers as the
PDF's; "NEC 1981 linear" the linear IC book (`upc4558/1981_NEC_Linear_IC_Data_Book.pdf`); "Toshiba
1973" the *1973 Toshiba Semiconductor Handbook* (`1s1555/`). Two sheets live outside the MS-20's
folder: the Mitsubishi 2SC1583 (`private/tb303/docs/datasheets/mitsubishi-2sc1583.pdf`, SHA1
19872953300a4d080a321d0b4efe056a08bcb237, from datasheetgo.com, listed in that folder's SOURCES.md)
and onsemi's MMBF5457 (https://www.onsemi.com/download/data-sheet/pdf/mmbf5457-d.pdf, MMBF5457/D
Rev. 1, January 2023, SHA1 6d63443d24233b389b0f1651e748063ecd443a12, fetched 2026-11-15; not yet
copied into the private folder). Where a sheet is vector (the Renesas reprints of NEC's sheets), its
curves were read from the PDF's paths (pdftocairo -svg), exact to the drawing; scanned curves were
read from 300-400 dpi renders against their grid lines.

**Method** (the Minimoog's, `machines/minimoog/docs/circuit/semiconductors.md`). Each model gives its
sheet's 25 °C typical figures at the sheet's test conditions, computed with the solver's own
equations at its nominal 27 °C (V_T 25.864 mV). Where a sheet gives only a range or a rank, the
target is the range's geometric middle. Parameters no figure constrains are stated as assumptions
with what they were taken from. Every "model gives" number below was computed with the solver's
equations (a Python copy of `bjtEval` and `channel`) and checked through `circuitrun` and ngspice on
one netlist holding every model (section 10).

## 1. Which part where

From the service manual's parts list (scan A, `a-007.png`) and the block netlists as they stand
(2026-11-15).

| Model | Parts list | Where |
|---|---|---|
| **2SC1623** | (inside KORG35 × 2) | IC9/IC10 Q1 (base-collector junction only), Q2, Q3: hpf.cir, lpf.cir |
| **2SA812** | (inside KORG35) | IC9/IC10 Q5, the output buffer's PNP |
| **2SK94** | (inside KORG35) | IC9/IC10 Q4, the output buffer's JFET |
| **2SC945** | 2SC-945(L)K × 1, "special selected" | VCA Q19 (vca.cir) |
| **2SC1685** | 2SC-1685S × 13 | VCO cores (Q2, Q3...), EG/trigger transistors, noise amp, MVCA driver |
| **2SA564A** | 2SA-564(S) × 4 | VCO cores (Q4...), trigger detector |
| **2SC1583** | 2SC-1583G × 2 | VCO expo pairs Q6, Q7 (vcoctl.cir) |
| **2SC644** | 2SC-644R × 1 | noise source TR3 (KLM-128), used as a breakdown junction |
| **2SK30A_O / _GR** | 2SK-30(O) × 4, 2SK-30(GR) × 4 | key hold, VCO buffers and clamps, MG switches, S&H hold |
| **1S1555** | 1S-1555 × 33 | every small diode, the filters' limiters D9/D10, D20/D21 |
| **CD4007N/P** | MC-14007 × 2 | EG1/EG2 switches |
| **CD4069N/P** | MC-14069B × 2 | EG2 hold monostable, ESP F-V |
| **CD4011N/P** | µPD4011C × 1 | VCO 2 ring modulator (XOR of four NANDs) |
| op-amp params | µPC-4558C × 17; 081 × 5, 082 × 1 (alternates TL-071/072, 3140) | everywhere |
| comparator params | µPC339C × 1 | ESP F-V (espfv.cir) |

The parts list has no CA3140 of its own: "(3140)" is printed under the 081 and 082 as an alternate,
and the schematic marks IC5 (key hold) and KLM-128's IC1 (S&H) "3140".

## 2. Bipolar transistors

**One NEC die.** NEC 1981 p. 571 ("Specifications of Mini Mold Device") gives the **2SC945** as the
2SC1623's TO-92 equivalent (and the 2SA733 as the 2SA812's). Their tables agree figure for figure:
h_FE 200 typ at 6 V, 1 mA; V_BE 0.62 V; V_CE(sat) 0.15 V and V_BE(sat) 0.86 V at 100/10 mA; f_T
250 MHz; C_ob 3 pF (NEC 1981 pp. 201 and 645). So the 2SC1623's fit (IS, ISE, NE, BR, VAF) is the
die's, and the 2SC945 takes it with its own rank.

| Model | Parameters | Sheet figures used | Model gives | Assumed |
|---|---|---|---|---|
| **2SC1623** NPN | IS 3.839e-14, BF 238.6, **BR 2.665** (`br1623`), NR 1 (`nr1623`), VAF 450, ISE 1.311e-15, NE 1.181, ISC 0 (`isc1623`) | NEC 1981 pp. 645-647, Renesas D17115EJ5V0DS pp. 1-3: h_FE **200**, V_BE **0.62 V** at 6 V, 1 mA (typ); 25 °C h_FE curve (p. 647, linear scale) **170 / 187 / 200 / 211** at 10 µA / 0.1 / 1 / 10 mA; V_CE(sat) plateaus at I_C = 10 / 20 / 50 I_B (Renesas p. 3, vector) **42.3 / 58.5 / 87.2 mV** at 0.5-1 mA; I_C-V_CE at I_B 5-45 µA (Renesas p. 2, vector): V_A 400-540 V; h-parameters at 1 mA: h_ie 6 kΩ, h_re 6.5e-4, h_fe 220, h_oe 20 µS | h_FE **170.1 / 186.5 / 200.0 / 210.7**; V_BE 0.501 / 0.560 / **0.620** / 0.680 V (the I_C-V_BE curve, Renesas p. 3: 0.500 / 0.559 / 0.617 / 0.678); h_fe 205 at 1 mA; V_CE(sat) **43.6 / 59.3 / 85.2 mV** | NR 1, no ISC (section 3); h_oe is all base feedback, (1 + h_fe) h_re / h_ie = 24 µS > 20 µS, so it gives no Early figure (the Minimoog's argument): VAF from the output curves, which self-heating can only steepen (a lower bound) |
| **2SC945** NPN (rank K) | IS 3.839e-14, BF 521.2, BR 2.665 (`br945`), VAF 212.1, ISE 7.523e-16, NE 1.181 | NEC 1981 p. 201 and Renesas ja D17117JJ2V0DS00: rank K h_FE **300-600** at 6 V, 1 mA (classes R 90-180, Q 135-270, P 200-400, K 300-600); h_FE(0.1 mA)/h_FE(1 mA) **0.92** typ | h_FE 350 / **390 / 424** / 452 at 10 µA / 0.1 / 1 / 10 mA (ratio 0.92); V_BE 0.620 V at 1 mA | the die's IS, NE, BR; VAF scaled by h_FE × V_A held at the typical unit's (200 × 450 V), as one die's beta selections trade base Gummel number for both (Minimoog §3) |
| **2SC1685** NPN (rank S) | as the 2SC1623, BF 338.9 | **no maker's sheet found.** futurlec.com/Transistors/2SC1685pr.shtml (a parts catalogue, read 2026-11-15): TO-92, V_CEO 50 V, 100 mA, 400 mW, h_FE **160-460** at 2 mA, f_T 150 MHz; no rank letters | h_FE **271** at 2 mA, 6 V (265 at 1 mA); V_BE 0.638 V at 2 mA | the NEC die (same 50 V / 100 mA small-signal class); the S rank's own range unknown, so the catalogue's whole range |
| **2SC644** NPN (rank R) | as the 2SC1623, BF 401.9 | **no maker's sheet.** futurlec.com/Transistors/2SC644pr.shtml: TO-92, V_CEO 50 V, 50 mA, 150 mW, h_FE **130-700** at 1 mA, f_T 75 MHz | h_FE **302** at 1 mA | as the 2SC1685. Its MS-20 job (KLM-128 TR3) is an emitter-base junction in breakdown as the noise source: the solver has no breakdown, so the noise block needs a stand-in (noise.cir already has one) |
| **2SC1583** NPN dual (rank G) | IS 8.01e-14, BF 1984, BR 2.665, VAF **30**, ISE 1.782e-14, NE 1.273 | Mitsubishi sheet (above), pp. 1-3: h_FE 250-800, ranks F 250-500, **G 400-800** at 6 V, 1 mA; transfer curve (p. 2, 25 °C) **0.597 V** at 1 mA; relative h_FE (p. 3, 100 at 1 mA) **45 / 58 / 66 / 90 / 140** at 10 µA / 50 µA / 0.1 / 0.5 / 10 mA; output curves (2) (p. 2) at I_B 0.6-1.4 µA, 1.5-6.5 V: V_A **26-31 V**; \|V_BE1 − V_BE2\| 1 mV typ (10 max), h_FE1/h_FE2 0.98 typ | h_FE **566** at 1 mA, V_BE **0.597** V; relative 44 / 60 / 68 / 89 / 142; h_FE 249 at 10 µA | BR the NEC die's (the sheet has only V_CE(sat) ≤ 0.6 V at 10/1 mA); both halves identical (the pair's mismatch is a block's offset source, as vcoctl.cir's `vnull`) |
| **2SA812** PNP | IS 3.843e-14, BF 197.9, BR 1.98, VAF 500 | NEC 1981 pp. 606-608, Renesas D17119EJ4V0DS pp. 1-2: h_FE **200**, V_BE **−0.62 V** at −6 V, −1 mA; h_FE flat 0.1-10 mA (p. 608); h-parameters h_fe 205, h_ie 5.5 kΩ, h_re 7.5e-4, h_oe 28 µS; V_CE(sat) **50 mV** at I_C = 10 I_B, 1-2 mA (Renesas p. 2); I_C-V_CE at I_B −5 / −10 µA flat within a pixel from −14 to −38 V | V_BE **0.620** V, h_FE **200** at 0.1-10 mA; V_CE(sat) **50.0** mV | no ISE (flat h_FE; h_fe 205 ≈ h_FE); VAF 500: above about 500 V on the curves (h_oe is again base feedback: 28.1 µS) |
| **2SA564A** PNP (rank S) | as the 2SA812, BF 360.9 | Blue Rocket 2SA564(A) Rev. E 2016 (`2sa564/2sa564a_bluerocket.pdf`, a second source, the only sheet found): h_FE Q 130-260, R 180-360, **S 260-520** at −10 V, −2 mA; V_CE(sat) 0.3 V typ at 100/10 mA; no V_BE, no curves | h_FE **368** at 2 mA, 10 V; V_BE 0.638 V there | IS, BR, VAF the 2SA812's (the same 45-50 V, 100 mA class); no ISE |

**BR from V_CE(sat).** In the Ebers-Moll and Gummel-Poon equations, a transistor forced into
saturation at I_C = β_f I_B has V_CE(sat) = V_T ln[(1 + (β_f + 1)/BR) / (1 − β_f/BF)] (Stinchcombe's
eq. 20 for the reverse case is the same algebra). Where RC does not count (the sheets' plateaus at
0.1-2 mA: RC + RE about 1 Ω from the slope above 20 mA, so about 1 mV at 1 mA) this is a reverse-mode
figure: it is how the sheets give BR without saying so. For the 2SC1623 the three curves give BR
2.84 / 2.76 / 2.46 alone and 2.665 together (NR 1); the 2SA812's one curve gives 1.98.

## 3. The 2SC1623 in reverse saturation (KORG35 Q2, Q3)

**What the KORG35 asks of it.** Q2 and Q3 sit in series between pins 3, 4 and 5, bases joined and
fed from R3 (KORG35.pdf p. 3), each a resistor of about V_T / (I_B β_R) (Stinchcombe eq. 21-22).
The cutoff is proportional to the base current; the exponential comes from Q1's base-collector
junction against theirs.

**What the sheets say.** No sheet gives reverse-mode data directly. The forward V_CE(sat) curves
(section 2) tie BR to NR: holding V_CE(sat) to the three curves,

| NR | BR fitted | residuals (mV, 10/20/50 at 0.1 and 1 mA) |
|---|---|---|
| 0.98 | 4.35 | 2.5, 3.6, 0.4, 1.4, −3.5, −2.9 |
| 0.99 | 3.39 | 1.8, 2.3, 0.6, 1.0, −2.4, −2.5 |
| **1.00** | **2.68** | 1.3, 1.2, 0.9, 0.7, −1.5, −2.1 |
| 1.01 | 2.14 | 1.0, 0.3, 1.1, 0.3, −0.6, −1.8 |
| 1.02 | 1.72 | 0.7, −0.6, 1.4, 0.0, 0.3, −1.6 |

So the sheet bounds the pair, not BR: about BR 1.7 (NR 1.02) to 4.35 (NR 0.98), all within the curves'
reading error (about 1-2 mV). Stinchcombe's PSpice library model has **BR 4.210** with BF 206.7
(`private/ms20/docs/papers/stinchcombe/MS20_study.txt`, §3.1); its NR is not quoted, and at NR 1 BR
4.21 gives 35 mV at I_C = 10 I_B, 7 mV under the sheet's 42.3. Tanaka (`private/ms20/docs/forum/
tanaka_saturation_tr/tr_utf8.txt`) says ordinary transistors' β_r is "about 1 to 6" and "1 to 7",
hearsay [N]. **Decision (2026-11-23): BR 4.35 with NR 0.98**, the table's high end and within 3 % of
Stinchcombe's 4.21, exposed as `br1623` and `nr1623`; the plausible range is the table's, BR
1.7-4.35 with NR moving the other way. Until then BR 2.665 with NR 1 (SPICE's default NR). The sheet
can't choose; three real units' recordings can (`improvements.md`, Done 2026-11-23): the KORG35's
loop gain rises with BR (Q2/Q3's resistances and Q1's share), and only the high end gives the
self-oscillation from PEAK 8 they show, their resonance under a full-level square (-13.4 dB
recorded; -13.2 at BR 4.35, -17.9 at 3.39, -19.4 at 2.665, PEAK 10) and their self-oscillation's
H2-H5 within 1-4 dB (2.665 gave 3-6 dB less). The low end (1.72) moves the onset to PEAK 9.5.

**ISC/NC.** The plateaus stay flat down to 0.1 mA at I_C = 50 I_B (I_B 2 µA): no base-collector
recombination shows there. The largest ISC the curve hides (2 mV rise at 0.1 mA, 50:1) is
**5.5e-13 with NC 1.5, 1.2e-11 with NC 2**. The KORG35 runs far lower: with the LPF's 3.3 nF and 1 nF,
the nominal pair gives about 440 Hz at 100 nA of total base current, so the cutoff range 50 Hz-15 kHz
spans about 11 nA to 3.4 µA. ISC is exposed as `isc1623` (default 0); NC is 2 in the model line.

**Sensitivity check.** Q2 and Q3 as in the KORG35, pin 3 held at 0 V (the board's divider is a few
hundred ohms), pins 4 and 5 open at DC (the capacitors), a current I_B into the joined bases; small
test currents (±I_B/1000) into pin 4 alone give R_Q2, into pin 5 the pair, R_Q2 + R_Q3. Run as
`circuitrun models.cir,revsat.cir --rate 1000 --param ib=... --param br1623=... --param nr1623=...
--param isc1623=... --input ite=... --input itm=...` (no capacitors: each sample is a DC solve);
ngspice agrees within 0.25 % of each output's range (section 10). The netlist (scratch, not a block):

```
.param ib=1u
IB 0 b {ib}
VA a 0 0
QQ2 a b m 2SC1623
QQ3 m b e 2SC1623
ITM 0 m input itm
ITE 0 e input ite
.output vm m
.output ve e
```

| BR | NR | ISC | I_B (total) | V pin 4 (DC) | R_Q2 | R_Q3 | R_Q2 + R_Q3 |
|---|---|---|---|---|---|---|---|
| 1.7 | 1 | 0 | 10 nA / 100 nA / 1 µA / 10 µA | 12.3 / 12.2 / 12.2 / 12.2 mV | 2.50 M / 250 k / 25.0 k / 2.50 k | 3.96 M / 399 k / 39.9 k / 3.98 k | 6.46 M / 649 k / 64.8 k / 6.48 k |
| **2.665** | 1 | 0 | same | 8.6 / 8.5 / 8.5 / 8.5 mV | 1.70 M / 170 k / 17.0 k / 1.69 k | 2.36 M / 235 k / 23.5 k / 2.34 k | **4.06 M / 405 k / 40.4 k / 4.04 k** |
| 4.21 | 1 | 0 | same | 5.9 / 5.8 / 5.8 / 5.8 mV | 1.14 M / 114 k / 11.3 k / 1.13 k | 1.42 M / 142 k / 14.1 k / 1.41 k | 2.56 M / 255 k / 25.5 k / 2.54 k |
| 1.72 | 1.02 | 0 | same | 16.6 / 17.4 / 18.2 / 19.0 mV | 2.37 M / 234 k / 23.1 k / 2.28 k | 4.29 M / 442 k / 45.1 k / 4.60 k | 6.66 M / 676 k / 68.2 k / 6.88 k |
| 4.35 | 0.98 | 0 | same | 0.3 / −0.6 / −1.6 / −2.5 mV | 1.21 M / 122 k / 12.4 k / 1.26 k | 1.24 M / 121 k / 11.9 k / 1.17 k | 2.45 M / 244 k / 24.3 k / 2.43 k |
| 2.665 | 1 | 1.2e-11 | same | 25.8 / 12.6 / 9.6 / 8.8 mV | 5.58 M / 255 k / 19.3 k / 1.77 k | 12.7 M / 395 k / 27.6 k / 2.46 k | 18.2 M / 650 k / 46.9 k / 4.23 k |

What it shows:

- **R ∝ 1/I_B exactly** without ISC (log-log slope −0.995 to −1.001 over three decades) for every BR
  and NR: BR only scales the cutoff, which the 500 Hz trim (FC ADJ, §7-4) takes out. The pair is
  4.1-4.3 V_T / (I_B BR) end to end (Stinchcombe's V_T / (I_B β_R) per transistor, each taking part
  of I_B).
- **What the trim does not take out:** the two resistors' ratio R_Q3/R_Q2, **1.60 / 1.38 / 1.24** at
  BR 1.7 / 2.665 / 4.21 (1.8-2.0 at NR 1.02 / BR 1.72; 0.92-1.03 at NR 0.98 / BR 4.35), which sets the
  Sallen-Key's damping with the 3.3 nF / 1 nF pair and so the PEAK threshold and the resonance
  (Tanaka works the same out by hand: conductances 2:1 at β_r 1, 8:7 at β_r 7); pin 4's DC offset
  (12 / 8.5 / 6 mV; Tanaka's 18 mV at β_r 1); and the large-signal asymmetry. At an equal
  small-signal resistance (405 kΩ, the nominal pair at 100 nA), a test current of ±0.5 K
  (K = BR I_B / 2 ≈ 133 nA, nearly the same for every BR at equal R) moves pin 5 by +64.3 / −21.2 mV
  at BR 1.7, **+53.9 / −21.4 mV at 2.665**, +49.6 / −21.9 mV at 4.21: current into pin 5 (through the
  pair emitter to collector, the reverse direction) moves it 3.0 / 2.5 / 2.3 times as far as the same
  current out of it, and every case leaves saturation (reverse-active: the node then runs away)
  before +0.75 K. This asymmetry is the KORG35's asymmetric ringing (Stinchcombe: 1.7 kHz against
  2 kHz).
- **ISC at the hidden bound bends the bottom of the range:** the resistance at 10 nA is 4.5 times the
  ISC-free value (2.2 octaves), at 100 nA 1.6 times (0.7 octave), at 1 µA 1.16 and at 10 µA 1.05. With
  the trim at 500 Hz (about 114 nA), the 50 Hz end would sit about 1.5 octaves off the law. The
  specification's LPF range (50 Hz-15 kHz, service manual §1) and Stinchcombe's straight 1 V/oct
  over 7.8 octaves (in SPICE, with the PSpice model) are the checks that can bound it; until then
  ISC 0.
- **NR also sets the exponential's scale** (Q1's base-collector junction against Q2/Q3's: the cutoff
  per volt of CV goes as 1/(NR V_T)); the filters have no scale trim, only FC ADJ. NR 1.02 would
  stretch 8 octaves by 0.16 octave. Stinchcombe's 1 V/oct check (§2.1 of research.md) bounds it.

For the KORG35 block: run the HPF/LPF with `--param br1623=` and `nr1623=` at the table's ends
(1.72/1.02 and 4.35/0.98) for the self-oscillation threshold (k1k2 ≈ 2⅓), the ringing asymmetry and
the 1 V/oct law; the trim takes out the rest.

## 4. The VCA's Q19 (2SC945, rank K, "special selected")

The parts list reads "2SC-945(L)K ... (special selected)": NEC rank K, h_FE 300-600 (NEC 1981
p. 201: "KA 300-600"; the ja sheet: "K 300〜600"). What "(L)" is, no source says. The schematic's
table (scan A `a-003.png`, beside IC10) ranks Q19 Orange, Yellow, Green or Blue: Orange and Yellow get
R126 680 kΩ, Green and Blue R124 1 MΩ, so the first two get 1.47 times the base current. **No source
relates the colours to h_FE figures**: NEC's 2SC945 classes are letters (R, Q, P, K), and the colours
subdivide a unit already ranked K. The order (orange, yellow, green, blue) is the resistor code's
3, 4, 5, 6, which are also the class digits NEC marks on the 2SC1623 (L3-L7: 60-120 ... 300-600, NEC
1981 p. 645); that is an inference, not a source [I]. Q19 works saturated, a resistor whose
conductance is about BR I_B / V_T (as the 909's single-transistor VCAs, `machines/tr909/core/netlists/
models.cir`), so Korg's selection more likely bins that gain than h_FE. The model is a typical rank-K
unit with the die's BR; `br945` lets the VCA block calibrate the gain to the block diagram's
check-point levels (16 → 19) for the resistor it picks (vca.cir: Green/Blue by default).

## 5. The VCO's 2SC1583

A matched dual for the log/antilog pair. Its relevant figures: the exponential (NF 1: the transfer
curve's 25.7 mV per e-fold from 0.25 to 0.92 mA), h_FE at the pair's currents (the core's capacitor
current, about 0.5 µA to 1 mA over the range: h_FE 250 at 10 µA, 0.44 of the 1 mA value, so base
currents matter at the bottom), and VAF 30 V, low because the rank G's base is thin (h_FE × V_A about
17 000 V, against about 90 000 for the NEC die). With VAF 30, a 1 V difference between the two
collectors scales one current by 3 %: the block's collector voltages matter.

## 6. JFETs

| Model | Parameters | Sheet figures used | Model gives | Assumed |
|---|---|---|---|---|
| **2SK30A_O** | VTO −0.841, BETA 1.296 mA/V², LAMBDA 1e-4 | Toshiba 2SK30ATM (2007; `2sk30a/2sk30atm_toshiba.pdf`) p. 1: I_DSS ranks R 0.30-0.75, **O 0.60-1.40**, Y 1.20-3.00, **GR 2.60-6.50** mA at 10 V; V_GS(off) −0.4 to −5 V; \|Y_fs\| ≥ 1.2 mS. p. 3 \|Y_fs\|-I_DSS line: **2.18 mS** at 0.917 mA, **3.92 mS** at 4.11 mA; p. 2 V_GS(off)-I_DSS line (at 0.1 µA): −0.96 V, −2.19 V; static curves flat within a pixel (0.01 mA of 2.9) from 13 to 48 V | I_DSS **0.917** mA, g_fs **2.18** mS at 10 V | VTO = −2 I_DSS / g_fs (square law); the sheet's V_GS(off) at 0.1 µA lies on the real device's tail, 0.13 V (O) and 0.09 V (GR) beyond VTO; LAMBDA the flat curves' bound; gate IS 1e-14 (sheet: I_GSS ≤ 1 nA at −30 V) |
| **2SK30A_GR** | VTO −2.097, BETA 0.935 mA/V², LAMBDA 1e-4 | as above | I_DSS **4.11** mA, g_fs **3.92** mS | as above |
| **2SK94** | VTO −1.50, BETA 1.267 mA/V², LAMBDA 3.5e-3 | **no sheet found.** NEC 1981 p. 572 (Cross Reference Guide to Mini Mold Device): Motorola **MMBF5457 → NEC 2SK94**. onsemi MMBF5457/D (above) p. 2: I_DSS 1.0 / **3.0** / 5.0 mA at 15 V; V_GS(off) −0.5 to −6.0 V at 1 nA; g_fs 1-5 mS; g_os **10** typ (50 max) µmho; p. 3 Fig. 6 (Process 55): I_DSS and g_fs against V_GS(off): **3.0 mA at −1.57 V, g_fs 4.0 mS** there (Fig. 5's typical unit: V_GS(off) −1.8 V, I_DSS 3.7 mA) | I_DSS 3.0 mA at 15 V (2.94 at 10 V), g_fs 4.0 mS, g_os 10 µS | **an assumption:** the 2SK94 is NEC's part, the MMBF5457 only its cross-referenced equivalent. Bounds: I_DSS 1-5 mA, and on Fig. 6's line V_GS(off) −0.78 V (1 mA) to −2.15 V (5 mA). The table's "V_GS −2.5 V typ at I_D 100 mA" has an impossible condition for a 5 mA part, and −2.5 V fits neither Fig. 5's typical unit nor Fig. 6; not used. Stinchcombe's stand-in, the BF245A, was picked for "a similar threshold voltage range" |

In the KORG35, Q4 is a source follower inside the Q5 feedback loop (KORG35.pdf p. 3): its V_GS sets
a DC offset at pin 7, which the board's coupling capacitors block, and its nonlinearity is mostly
fed back. The bounds matter more for the vco1/vco2 buffers and the hold switches, which are 2SK30As.

## 7. Diode

| Model | Parameters | Sheet figures used | Model gives |
|---|---|---|---|
| **1S1555** | IS 1.023e-8, N 1.949, RS 1.33 | Toshiba 1973 pp. 689-690 (1S1553/4/5): I_F-V_F at 25 °C, 0.1-300 mA (p. 689; the scan deskewed against its frame, 24 points 0.11-112 mA); V_F 1.0 typ / 1.4 max V at 100 mA; I_R ≤ 0.5 µA at 30 V; I_R-V_R (p. 690): a few nA rising to about 10 nA by 30 V at 25 °C; C_j 1.3 pF | within **12 mV (rms 6)** of the curve: 0.463 / **0.581** / 0.709 / 0.944 V at 0.1 / 1 / 10 / 100 mA; reverse current IS = 10 nA, the I_R curve's level. Below 0.1 mA an extrapolation: 0.347 V at 10 µA, 0.232 at 1 µA |
| **1S1555LIM** (D20/D21 only) | IS 2.9e-9, N 1.3, RS 1.33 | fitted, 2026-11-25: one unit's LPF resonance (g6OfcNTvJow; scenes 20 and 21): its level along both sweeps and how steadily it rings between the oscillators' edges | the limiter works at a few µA, below the sheet's 0.1 mA; this knee is 0.196 V at 1 µA (the sheet's extrapolation 0.232) and sharper. The HPF's pair keeps 1S1555: the same model there changes nothing measurable |

The table's 1.0 V at 100 mA is 56 mV above the typical curve; the fit follows the curve (the MS-20's
diodes carry milliamps). No breakdown (the sheet's ~35-40 V knee on p. 690 is never reached).

## 8. CMOS

Level 1, bulk on the source, so no body effect; W and L are only a split of beta = KP/2 W/L (L 6 µm,
KP 20 µA/V², the 909's convention). The block netlists put the 4069 on +15 V and ground (espfv.cir),
so the fits are for **V_DD 15 V**:

- **VTO 1.005 V**: from the MC14007UB's 25 °C sink curves (onsemi MC14007UB/D Rev. 12, p. 4 Fig. 4,
  vector): saturation 3.37 mA at V_GS 5 V and 17.9 mA at 10 V; their ratio with the square law.
- **LAMBDA 0.01**: those curves are flat in saturation (17.78 → 17.94 mA from 6 to 8 V; 3.30 → 3.37
  from 3 to 4 V).
- **beta** from each sheet's typical sink current at 15 V / 1.5 V. Every table is symmetric at 15 V
  (I_OH = −I_OL), so the P mirrors the N.

| Model | beta (W) | Sheet (typ, 25 °C) | Model gives |
|---|---|---|---|
| **CD4007N/P** | 0.248 mA/V² (W 148.8 µm) | MC14007UB p. 3: I_OL 1.0 / 2.5 / **10** mA at 5 V/0.4 V, 10 V/0.5 V, 15 V/1.5 V; I_OH −5.0 at 5 V/2.5 V, −1.0 at 5 V/4.6 V, −2.5 at 10 V/9.5 V, **−10** at 15 V/13.5 V; V_IL/V_IH 2.25/2.75, 4.5/5.5, 6.75/8.25 V | 0.76 / 2.18 / **10.0** mA (−24 / −13 / 0 %); source 3.49 mA at 5 V/2.5 V (−30 %); switching point V_DD/2; 10 % / 90 % output at input 5.55 / 9.45 V at 15 V (sheet's typical points 6.75 / 8.25: the model is sharper in the middle, wider at the edges); R_on at small V_DS: 144 Ω (V_GS 15), 224 Ω (10), 505 Ω (5) |
| **CD4069N/P** | 0.218 mA/V² (W 130.9 µm) | MC14069UB p. 3: I_OL 0.88 / 2.25 / **8.8** mA, I_OH −4.2, −0.88, −2.25, **−8.8** mA (same conditions); V_IL/V_IH as the 4007 | 0.67 / 1.92 / **8.8** mA (−24 / −15 / 0 %) |
| **CD4011N** / **CD4011P** | N 0.337 mA/V² (W 202.4 µm), P 0.169 mA/V² (W 101.2 µm) | TI/RCA CD4011B (`upd4011/cd4011b_ti.pdf` p. 2, the equivalent of NEC's µPD4011C; a buffered gate): I_OL 1 / 2.6 / **6.8** mA, I_OH −3.2 (5 V/2.5 V), −1, −2.6, **−6.8** mA | for a one-stage NAND of these (the two N in series, the two P in parallel): the series N pair sinks **6.7** mA and one P sources **6.8** mA at 15 V/1.5 V (circuitrun); 0.51 / 1.48 mA at 5 / 10 V (−49 / −43 %) |

Level 1 cannot give both the 15 V and the 5-10 V drive of these metal-gate parts (their drive grows
more slowly than the square law: with the 5, 10 and 15 V table points together the square law asks
for a negative threshold). If a block runs CMOS on 5 or 10 V, refit beta there. The CD4011B is
buffered (three stages): a one-stage NAND from these models switches less sharply and, with both
inputs together, at a different point; for the ring modulator's logic levels that does not matter.
No input-protection diodes are modelled (the sheets give only their ratings).

## 9. Op-amps and the comparator

X elements: `X<name> in+ in- out opamp <gbw> <gain> <vsat> <sr>` with the `.param`s from models.cir;
B elements for the µPC339. All at ±15 V, 25 °C, typical.

| Part | Param names | GBW | Open-loop gain | Output swing (vsat) | Slew rate | Source |
|---|---|---|---|---|---|---|
| µPC4558C | `gbw4558 gain4558 vsat4558 sr4558` | **3 MHz** (open-loop curve: 100 dB flat to 29 Hz, 0 dB at 2.96 MHz) | **1e5** (100 000 typ, 20 000 min, R_L ≥ 2k) | **±14 V** (R_L ≥ 10k; ±13 V into 2k) | **1.0 V/µs** (A_V 1) | NEC 1981 linear p. 68; Renesas G10518EJAV0DS pp. 3, 5 (vector curve) |
| TL081 / TL082 | `gbwtl gaintl vsattl srtl` | **3 MHz** (the table's figure for the original die: NS/PS packages and TL08xM; today's D/P packages 5.25 MHz) | **2e5** (200 V/mV typ, TL08xC) | **±13.5 V** (R_L = 10k typ) | **20 V/µs** (typ) | TI SLOS081O (2025) §5.8-5.9 |
| CA3140 (alternate) | `gbw3140 gain3140 vsat3140 sr3140` | **4.5 MHz** | **1e5** (100 kV/V) | **+13 / −14.4 V** typ into 2k: vsat 13 (the solver clips symmetrically) | **9 V/µs** | Renesas FN957 Rev. 10.01 p. 3 |

| Part | Params | Figures used | Model |
|---|---|---|---|
| µPC339C | `isink339 vsat339 vg339 vcm339 vos339` | Renesas G11764EJ4V0DS p. 3 (V+ 5 V): output sink **16 mA** typ at V_O ≤ 1.5 V (6 min); V_OL **0.2 V** typ at 4 mA; A_V **200 V/mV** typ into 15 kΩ; input range 0 to **V+ − 1.5 V**; V_IO ±2 mV typ; response 1.3 µs | isink 16 mA; vsat 0.783 V (4.0 mA at 0.2 V, 15.3 mA at 1.5 V); vg 11.7 µV (gain 2e5 at V_O 1.4 V into 15k to 5 V); vcm 13.5 V for V+ = +15 V (set it for the block's supply); vos 0 (typical offset has no sign). The response time is not modelled |

The TL08x figures are TI's current sheet; the 1978 part's slew rate is not on it (the table no
longer separates the dies for slew rate). The 4558's 3 MHz matches vco1.cir's and the 909's figure;
vcoctl.cir's TL081 numbers are these.

## 10. Checks

- **Every model parses and runs**: a scratch netlist with each BJT at a base current, each JFET at
  V_GS 0 / V_DS 10 V, the diode at 1 mA, an inverter of each CMOS pair on 15 V, each op-amp as an
  inverter and the comparator, swept 0-15 V: circuitrun against ngspice
  (`tools/spice/compare_circuit.py`), largest differences 17.5 µV on the V_BEs and the diode (the
  two programs' V_T constants differ in the fifth digit), 0.018 % of range on the CMOS outputs,
  0.09 % on the op-amps, 0.001 % on the comparator. Every block netlist in `core/netlists/` (eg1, eg2,
  esp, espenv, espfv, hpf, lpf, mg, mixer, mvca, noise, phones, sh, vca, vco1, vco2, vcoctl) parses
  and runs with these models.
- **Reverse saturation** (section 3's netlist, test currents inside saturation): circuitrun against
  ngspice within 0.25 % of each output's range at BR 1.7, 2.665 and 4.21.

## 11. For the block authors

- **Names unchanged**; new: `CD4069N/P`, `CD4011N/P` (vco2.cir and espfv.cir still build their 4011
  and 4069 from `CD4007N/P`), and the op-amp/comparator `.param`s (mixer, vco1, vco2, vcoctl define
  the 4558's and TL081's with the same names and values; a block's later `.param` wins). espfv.cir's
  comparators pass only `isink` and `vcm`: `vsat={vsat339} vg={vg339}` are the sheet's.
- **2SC1623**: IS 3.8e-14 against the provisional 1e-14, BR 2.665 against 4.21: the KORG35 needs
  about 1.6 times the base current for the same cutoff, so FC ADJ moves; Q3/Q2 is 1.38 (1.24 with
  BR 4.21). Test `br1623`/`nr1623` at 1.72/1.02 and 4.35/0.98, and `isc1623` up to 1.2e-11 against the
  50 Hz-15 kHz range (section 3).
- **2SK30A_GR**: VTO −2.10 V against the provisional −1.5 (I_DSS 4.11 mA against 3.4): every GR
  source follower's level shift grows by about 0.5 V (vco1.cir's J1). **2SK30A_O**: VTO −0.84.
- **2SC945**: `br945` for the VCA's calibration; rank K. **2SC644**: no breakdown in the model.
- **CMOS on 5 or 10 V** would need refitting (section 8).
- The 2SC1583 sheet is in the TB-303's private folder and the MMBF5457 sheet only at its URL: both
  belong in `private/ms20/docs/datasheets/` and its SOURCES list.

## 12. Open questions

- The 2SC1623's reverse parameters (BR with NR, ISC): a curve tracer on any 2SC1623 or 2SC945, or a
  measured KORG35 filter (cutoff law at the range's ends, Q3/Q2 through the resonance threshold),
  would settle them. Section 3 lists what each changes.
- The 2SK94: an NEC sheet. Q19's colour ranks: any Korg or NEC document on the selection.
- 2SC1685, 2SC644: maker's sheets (rank ranges, V_BE). 2SA564A: an original (period) sheet.
- No RE/RB/RC/IKF: V_BE at tens of mA and h_FE above about 10 mA are junction-only.
