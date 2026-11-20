# ProCo RAT2 (LM308) — component-level transcription

Status: research transcription, 2026-10-06. Every value carries a source tag; anything not
confirmed by at least one trace of a real pedal is marked **[?]**. The ngspice version of this
table is `private/reference/spice/rat2.cir`, which runs.

## 0. Source tags

| Tag | Source (saved file) | Kind |
|---|---|---|
| **F86** | Pro Co Sound factory hand-drawn schematic "RAT Rev 7-86, Rev C" (published by JHS) — `private/docs/schematic/jhs/55-RAT-2-Schematic-…png` | **Factory drawing.** Designators and topology; only five values are written on it (R12 1MEG, R13 22M, R14 2.2K, R15 10K, D4/D5 "4001") |
| **MR** | G. Tranter (GGBB @ DIYStompBoxes), "Multi-RAT" Rev G, 31 Aug 2012 — `private/docs/schematic/coda/multi-rat.png` | Compiled from **traces of a 1990 Turbo RAT, a 1992 RAT2 and a 1998 Vintage RAT**, plus RG Keen, Jack Orman, Chittum, Strand and Dosmun. Uses the 1995 Multi-RAT PCB designators. Per-version table for RAT / RAT2 / Turbo / Vintage / YDR |
| **ES** | ElectroSmash "Pro Co Rat Analysis" (archive mirror) — `private/docs/web/electrosmash_proco-rat.md`, schematic `private/docs/schematic/electrosmash/pro-co-rat-schematic-parts.jpg`, PDF `…/ElectroSmash-ProCo-Rat-Analysis.pdf` | Analysis article. Its values match MR's **original RAT** column, not RAT2 |
| **RGK-B** | R.G. Keen drawing "ProCo Rat Distortion, Type B Rev 7-81" (hosted by GGG) — `private/docs/schematic/ggg/ratb.gif` | Early trace |
| **RGK-F** | R.G. Keen drawing revised by Brad Fajardo 7/98, with an "Original / LED Rat / Turbo Rat" table — `private/docs/schematic/ggg/ratc.gif` | Trace plus variant table |
| **RGK-O** | R.G. Keen drawing revised 10/98 by Jack Orman (AMZ) — `private/docs/schematic/muzique/pcrat1.gif` (same drawing as `private/docs/schematic/ggg/rat.gif`) | Early "version A" with an input JFET buffer, 150k DIST, 33k/33k bias |
| **PISO** | M. Badía hand trace, pisotones.com (2005) — `private/docs/schematic/pisotones/pisotones_MBC_RATesquema.jpg` | Hobbyist trace ("very similar to the original", so treat it as partly redrawn) |
| **GGG** | General Guitar Gadgets "Rodent" clone (2009–2014) — `private/docs/schematic/ggg/ggg_rodent_*.pdf` | Clone. Its bias network and JFET bias are its own |
| **AION** | Aion FX Helios 1.1.0 (2024) and legacy Helios docs — `private/docs/schematic/aionfx/` | Clone with notes on versions |
| **PPCB** | PedalPCB Muroidea (2023 doc) — `private/docs/schematic/pedalpcb/` | Clone (original-RAT values) |
| **FC** | Five Cats "Rattus" V1.0 (2021), RAT / RAT2 / Turbo / YDR table — `private/docs/schematic/fivecats/` | Clone with a version table. **Contains apparent errors** (see D7, D8) |
| **JHS** | JHS Pedals, "ProCo RAT pedal myths, history and timeline" — `private/docs/web/jhs_proco-rat-myths-history-timeline.md` | History from about 100 units examined; no values |
| **MAN** | Pro Co owner's instructions; RAT2 spec sheet (secondary copy) — `private/docs/web/proco_rat_owners_instructions_manuals-co-uk.txt`, `private/docs/web/rat2_specs_search-manual.txt` | Factory documentation |

Designators below are **MR** (= 1995 Multi-RAT PCB). F86 uses the same numbering, except that the
100 pF feedback capacitor is **C7** on F86 and the 47 Ω leg's capacitor is **C5** (MR: C5 = 100 pF,
C7 = 2.2 µF; MR notes "C5 is mislabeled on some Multi-RAT PCBs as a duplicate C7"). Section 7 has a
full cross-reference to the other sources' designators.

## 1. Node map (effect ON)

Node numbers are for the MNA model; the names are the ones used in `rat2.cir`.

| # | Name | Meaning |
|---|---|---|
| 0 | `0` | ground (battery −, jack sleeves, V− pin 4) |
| 1 | `vbat` | battery / DC jack + (ideal 9 V source) |
| 2 | `v9` | +9 V rail after R1 (op-amp pin 7, Q1 drain) |
| 3 | `vref` | bias divider, about 4.5 V |
| 4 | `in` | input jack tip with footswitch in effect position |
| 5 | `na` | C3 / R4 / R5 junction |
| 6 | `inp` | op-amp non-inverting input (pin 3) |
| 7 | `inn` | op-amp inverting input (pin 2) |
| 8 | `opout` | op-amp output (pin 6) |
| 9 | `cc1` | op-amp pin 1 (compensation / balance) |
| 10 | `cc8` | op-amp pin 8 (compensation) |
| 11 | `n6` | R6–C7 junction (47 Ω leg) |
| 12 | `n7` | R7–C6 junction (560 Ω leg) |
| 13 | `n8` | R8–C9 junction |
| 14 | `clip` | clipping-diode node |
| 15 | `nf` | FILTER pot – R9 junction |
| 16 | `tone` | R9 / C11 / C12 node (filter output) |
| 17 | `gate` | Q1 gate |
| 18 | `src` | Q1 source |
| 19 | `vtop` | C13 / R15 / VOLUME lug 3 |
| 20 | `out` | VOLUME wiper → output jack tip |

## 2. Netlist table — RAT2 (LED version, LM308), effect ON

"Tol." is the manufacturing tolerance. The RAT sources do not give it; where a value is shown it is a
typical part tolerance (marked *typ*), not a RAT figure. Bourns' pot data: non-linear tapers above 100 kΩ
±20 % total resistance, taper tolerance ±20 % at 50 % (`private/docs/datasheets/Bourns_custom_pot_designer_guide.txt`).

### 2.1 Power and bias

| Ref | Value | Tol. | Nodes (a, b [, c]) | Sources | Notes |
|---|---|---|---|---|---|
| VBAT | 9 V DC | — | 1, 0 | MAN ("standard 9-volt battery or … any external 9-volt power supply", negative ground; the text reads "should not exceed +120VDC", presumably +12.0 V DC) | Treated as ideal. A battery's internal resistance is not modelled |
| R1 | **47 Ω** | 5 % *typ* | 1, 2 | MR (RAT2 "sometimes 100"), ES R10 47, RGK-F 47, RGK-O 47, GGG R14 47, PISO R6 47Ω, AION R9 47R | RGK-B 100 Ω. FC "47r or 100r" (RAT2), 470r (others: see D8) |
| C1 | **100 µF** | 20 % *typ* | 2, 0 | MR, ES C11, RGK-B/F/O (16 V), PISO C3 100µ/16, GGG, AION, PPCB | Electrolytic. The netlist adds 0.1 Ω ESR only for convergence |
| D1 | **1N4004**, shunt: anode 0, cathode 2 | — | 0, 2 | MR 1N4004 (shunt), F86 (shunt, type not written), ES D3 1N4002, RGK 1N4001, PISO 1N4007 | Reverse-polarity protection; reverse biased in use. Clones use a series 1N5817 instead |
| C10 | **10 nF** | — | 2, 0 | MR 10n, RGK-F 0.01 µF, FC 10nF; F86 shows C10 at the op-amp V+ | **[?]** RGK-B and RGK-O 0.001 µF, ES 0.1 µF (as C12), GGG 0.047 µF. No effect with an ideal supply |
| R2 | **100 kΩ** | 5 % *typ* | 2, 3 | MR, ES R11, RGK-B/F, PISO R4, AION, PPCB | RGK-O 33k; GGG 10k (clone) |
| R3 | **100 kΩ** | 5 % *typ* | 3, 0 | MR, ES R12, RGK-B/F, PISO R5, AION, PPCB | RGK-O 33k; GGG 10k |
| C2 | **1 µF** | 20 % *typ* | 3, 0 | MR, ES C13, RGK-B/F (50 V), PISO 1µ/50, PPCB, FC | **[?]** AION 47 µF, GGG 47 µF, RGK-O 47 µF |

Vref corner: (R2‖R3)·C2 = 50 kΩ·1 µF → 3.2 Hz. Supply filter R1·C1 → 34 Hz. Current draw: the
original RAT about 0.6 mA idle and 1.6 mA at full output (RGK-B/F). The RAT2 manual and spec give 3.0 mA,
which includes the LED.

### 2.2 Input network

| Ref | Value | Tol. | Nodes | Sources | Notes |
|---|---|---|---|---|---|
| R12 | **2.2 MΩ** (RAT2 per MR) | 5 % *typ* | 4, 0 | MR RAT2 & Turbo 2M2, "–" (absent) for RAT/VR/YDR; FC RAT2 2.2M | **[?] F86 (1986 Rev C) writes "1MEG".** ES R1 1M, PISO R1 1M, PPCB R1 1M, AION RPD 1M–2M2. Input pull-down only |
| C3 | **22 nF** | 5–10 % *typ* | 4, 5 | MR, F86 (C3), ES C1, all others | Film |
| R4 | **2.2 MΩ** (RAT2 per MR) | 5 % *typ* | 3, 5 | MR RAT2 2M2, Turbo 2M2, RAT/VR/YDR 1M; FC RAT2 2.2M | **[?]** ES R2 1M, AION R1 1M, GGG R1 1M, PPCB R2 1M, PISO R3 1M, RGK-B/F 1M, RGK-F table "LED Rat 220K" (probably a misread of 2M2). F86: value not written |
| R5 | **1 kΩ** | 5 % *typ* | 5, 6 | MR, ES R3, F86 (R5), all | — |
| C4 | **1 nF** | 5–10 % *typ* | 6, 0 | MR, ES C2, F86 (C4), all | Film |

Input HPF: C3 with R4 (Vref is an AC ground) → 3.3 Hz with 2M2, or 7.2 Hz with 1M (ES quotes 7.2 Hz).
The input impedance depends strongly on frequency, which ES does not mention. The source sees
R12 ‖ [C3 + (R4 ‖ (R5 + C4))]. With RAT2 values and an ideal op-amp, |Zin| is about **0.92 MΩ at 100 Hz,
164 kΩ at 1 kHz and 16.7 kΩ at 10 kHz**, because C4 (1 nF) hangs off the input through only 1 kΩ (computed
in this study). The manual's "Input Impedance 1 MOhm" is the low-frequency value. With a passive guitar,
C4 behaves like about 1 nF of extra cable capacitance behind 1 kΩ.

### 2.3 Gain stage (U1)

| Ref | Value | Tol. | Nodes | Sources | Notes |
|---|---|---|---|---|---|
| U1 | **LM308N** (Motorola or National) | — | + 6, − 7, V+ 2, V− 0, out 8, comp 9 & 10 | MR, ES, F86 (IC1), PISO LM308N, JHS, Coda (Motorola LM308 in a 1985 unit) | OP07DP in later RAT2s (see §6). Pinout: 1 comp/bal, 2 −, 3 +, 4 V−, 5 NC, 6 out, 7 V+, 8 comp |
| C8 | **30 pF** | 5 % *typ* (ceramic) | 9, 10 | MR (early big-box 33p → 30p), ES C3, F86 (C8, pins 1–8), RGK, PISO 30pF, GGG, PPCB, FC | AION 1.1 recommends 33 pF for the LM308 (clone advice). NSC: "Bandwidth and slew rate are proportional to 1/Cf" |
| DIST (RR1) | **100 kΩ A (log)**, as a rheostat | ±20 % *typ* | wiper + lug 3 → 7; lug 1 → 8 (resistance = lug 1–wiper) | MR "DIS A100k" (early big-box A150k → A100k), ES "100K-A", F86 RR1, GGG "100k log", PPCB A100K, AION 100kA, PISO "100K LOG", RGK-B/F "100k Log" | **[?] early units: 150k log** (RGK-O, MR). CW = more gain (MAN). R(x) = 100k·A(x) |
| C5 (F86: C7) | **100 pF** | 5 % *typ* | 7, 8 | MR, ES C4, all | Across the DIST pot: fc = 1/(2π·R_DIST·100p) = 15.9 kHz at full DIST |
| R7 | **560 Ω** | 5 % *typ* | 7, 12 | MR, ES R5, all | — |
| C6 | **4.7 µF** | 20 % *typ* | 12, 0 | MR, ES C6, all | Electrolytic (+ to node 12, sitting at about 4.5 V DC). R7·C6 → 60.5 Hz |
| R6 | **47 Ω** | 5 % *typ* | 7, 11 | MR, ES R4, RGK, PISO R7 47Ω, all except FC | **FC: 470 (see D7).** JHS: the 47 Ω was Burnham's accidental substitution for 470 Ω and is "the RAT's trademark sound" |
| C7 (F86: C5) | **2.2 µF** | 20 % *typ* | 11, 0 | MR, ES C5, RGK-F/O, PISO C8 2µ2/25, AION, GGG, PPCB, FC | **[?] RGK-B (Type B Rev 7-81): 4.7 µF.** R6·C7 → 1539 Hz (4.7 µF → 720 Hz) |

Ideal-op-amp gain: G(s) = 1 + Zf/Zg, with Zf = R_DIST ‖ C5 and Zg = (R6 + 1/sC7) ‖ (R7 + 1/sC6).
High-frequency maximum: 1 + 100k/(47‖560) = 2307 (67.3 dB, ES "2305 (67 dB)"). Real maximum: the LM308's
GBW caps it. In ngspice with the LTC LM308 model the small-signal peak is **63.5 dB at 631 Hz** and 58.1 dB at
1 kHz. Chuck D. Bones (PedalPCB) gives about 60 dB at 600 Hz; the manual says "Gain 60dB".

### 2.4 Clipper

| Ref | Value | Tol. | Nodes | Sources | Notes |
|---|---|---|---|---|---|
| R8 | **1 kΩ** | 5 % *typ* | 8, 13 | MR, ES R6, F86 (R8), all | Diode current limit. F86 and MR put R8 first, then C9; ES draws C7 first. A series pair commutes, so this makes no electrical difference |
| C9 | **4.7 µF** (35 V in RGK) | 20 % *typ* | 13, 14 | MR, ES C7, F86 (C9, + toward the op-amp), RGK-B/F/O, PISO C10 4µ7/25, all | Electrolytic (+ at node 13, about 4.5 V DC). AION offers a 1 µF film alternative |
| D2 | **1N4148**, anode 14 → cathode 0 | — | 14, 0 | MR RAT2 1N4148 (RAT 1N914), JHS ("traditional silicon 1N4148"), RGK-B/F/O 1N4148, PISO 1N4148 | ES, GGG, PPCB, AION: 1N914. Same LTspice model (Is = 2.52 nA, N = 1.752, Rs = 0.568 Ω) |
| D3 | **1N4148**, anode 0 → cathode 14 | — | 0, 14 | as D2 | Anti-parallel to D2 |

### 2.5 FILTER

| Ref | Value | Tol. | Nodes | Sources | Notes |
|---|---|---|---|---|---|
| FIL (RR2) | **100 kΩ A (log)**, as a rheostat | ±20 % *typ* | lug 1 → 14; wiper + lug 3 → 15 (PPCB wiring; AION mirrors it with lug 3 + wiper on the diode side) | MR "FIL A100k", ES "RTONE 100K-A", F86 RR2 "Filter", GGG "100k log", PPCB A100K, AION 100kA, PISO "100K LOG", RGK "100k Log" | **Resistance rises CW, so CW = darker** (MAN: "Turn this high cut filter clockwise to decrease the amount of treble"; AION; Coda). R(x) = 100k·A(x). Pre-1981 "TONE" units are wired reversed (JHS, MR) |
| R9 | **1.6 kΩ** | 5 % *typ* | 15, 16 | **MR (1k6; "early big-box 1k5 → 1k6")** | **[?]** ES R7 1.5K, RGK-B/F/O 1.5k, PISO R10 1K5, GGG, AION, PPCB, FC 1.5k |
| C11 | **3.3 nF** | 5–10 % *typ* | 16, 0 | MR, ES C8, all | **[?]** MR: "Chinese are 2n7" (2008+ RAT2); earliest big-box 3n |

Ideal cutoff: fc = 1/(2π(R_FIL + R9)·C11), from 475 Hz (CW) to 30 kHz (CCW) with 1k6 (ES gives 475 Hz–32 kHz
with 1k5). The real source impedance at the clip node changes with level: about 1 kΩ (R8) when the diodes
are off and tens of ohms when they conduct. At FILTER fully CCW the corner therefore moves between
**about 18.6 kHz (diodes off) and 30 kHz (hard clipping)**. A component-level model captures this
automatically. At FILTER noon (15 kΩ, 15 % taper) the corner is about 2.9 kHz.

### 2.6 Output buffer and VOLUME

| Ref | Value | Tol. | Nodes | Sources | Notes |
|---|---|---|---|---|---|
| C12 | **22 nF** | 5–10 % *typ* | 16, 17 | MR, ES C9, F86 (C12), all | C12·R10 → 7.2 Hz |
| R10 | **1 MΩ**, gate to ground | 5 % *typ* | 17, 0 | MR (RAT, RAT2, VR, YDR 1M), ES R8, F86 (R10 to ground), PISO R11, GGG R9, PPCB R8, AION R7, FC R11 | **[?]** Turbo: 2M2 plus R16 2M2 to +9 V (MR). RGK-F table "LED Rat 100K / Turbo 100K". RGK-B: 1 MΩ returned to **+4.5 V** |
| Q1 | **2N5458** N-JFET: D 2, G 17, S 18 | IDSS 2–9 mA, VGS(off) −1…−7 V (datasheet) | 2, 17, 18 | MR, ES, PISO, GGG, PPCB, FC | AION legacy 2N5457, AION 1.1 2N5485 (substitute), RGK-O BF245A. Bias depends on the individual device |
| R11 | **10 kΩ** | 5 % *typ* | 18, 0 | MR, ES R9, F86 (R11), all except RGK-O (1k) | — |
| C13 | **10 µF** (RAT2) | 20 % *typ* | 18, 19 | MR RAT2/Turbo 10u (RAT/VR 1u, YDR "?"), FC RAT2 10µF | **[?]** ES C10 1µF, AION/GGG/PPCB 1µF, PISO 1µ/50, RGK-F "Original 1uf / LED Rat 1uf / Turbo 10uF", RGK-O 10µF. F86: value not written |
| R15 | **10 kΩ**, vtop to ground | 5 % *typ* | 19, 0 | **F86 "R15 10K"**, MR RAT2/Turbo 10k (absent on RAT/VR/YDR), FC RAT2 10k, RGK-F "LED Rat 10K", RGK-O 10k | Reportedly added for the LED bypass, with C13 raised from 1 µF to 10 µF to keep the corner (unverified web summary of the freestompboxes "Rat 2 (early 90s)" thread). Corners: C13·(R15‖100k) → 1.75 Hz; 1 µF·100k → 1.6 Hz |
| VOL (RR3) | **100 kΩ A (log)**, divider | ±20 % *typ* | lug 3 → 19, lug 1 → 0, wiper → 20 | MR "VOL A100k", ES "100K-A" ("standard 100K audio potentiometer"), F86 RR3 "Output", all others A/log | Wiper-to-ground fraction = A(x). CW = louder (MAN) |

DC operating point (ngspice, LTspice 2N5458 model): V(src) = 2.22 V, Id = 0.222 mA. The datasheet allows
VGS up to −3.5 V at 200 µA, so real units sit at roughly 1.5–3.5 V. The follower's AC gain with RAT2 loading
(R11 ‖ R15 ‖ 100k ≈ 4.8 kΩ, gm ≈ 0.66 mS) is about 0.76–0.78. Measured clip-to-out ratio at full volume:
0.78. Output impedance is about 1/gm ‖ R11 ‖ R15 ≈ 1 kΩ at full volume (MAN: "Output Impedance 1 kOhm").
At volume noon it rises to about 13 kΩ ((85k + 1k) ‖ 15k).

**Negative headroom of the follower.** With the gate at 0 V DC, the output can swing down only
Id·R_ac ≈ 0.22 mA × 4.8 kΩ ≈ 1.1 V before Q1 cuts off. That is ample for silicon diodes (±0.64 V) but not
for LEDs (±1.6 V). ngspice with LEDs and RAT2 biasing gives an asymmetric output of +1.29 / −1.06 V. With the
Turbo's 2M2/2M2 gate bias (Vs = 6.2 V) it is symmetric, +1.40 / −1.34 V. This explains MR's Turbo-only R16.

### 2.7 Load and source assumptions (for verification)

* **Source:** ideal voltage source, 0 V DC, with series `rsrc` = 100 Ω (re-amp or interface output). The
  NAM captures are presumably made this way, so the input-impedance interaction above is absent from them.
  To evaluate with a guitar, the netlist has a commented passive-pickup model (6 kΩ + 2.5 H + 500 pF cable).
  Those are generic values, not measured.
* **Load:** 1 MΩ (amplifier or interface input). The netlist uses `RLOAD 1Meg`.

## 3. Pot tapers

The three pots are 100 kΩ **audio (A/log) taper** on every source. **None is linear**, although some
clone builders prefer B or W tapers (PedalPCB thread). Rotation x runs from 0 (fully CCW) to 1 (fully CW).
A(x) is the fraction of total resistance between lug 1 and the wiper.

* DISTORTION: R = 100k·A(x) (rheostat; lug 1 to the output, wiper and lug 3 to pin 2).
* FILTER: R = 100k·A(x) (rheostat; increases CW, so darker CW).
* VOLUME: wiper-to-ground fraction A(x); top section 100k·(1−A(x)).

A real "audio" taper is a 2- or 3-segment piecewise-linear approximation printed in different resistive
inks (geofex "Secret Life of Pots"). The fraction at 50 % rotation is the key parameter:

* **10 %**: Bourns "Z" taper ("attains 10 % resistance value at 50 % of clockwise rotation", Bourns guide);
  geofex "about 10 %".
* **15 %**: the Alpha "15A (JIS:A)" pots used by most modern builders (Chuck D. Bones, PedalPCB tapers
  thread); geofex "15 %–20 %".
* 20–30 %: occasional (Fender uses 25 % and 30 %).

Which law the original pots used (CTS? Alpha? ProCo's Chinese pots after 2008) is **unknown [?]**. It must be
fitted against captures; see the research doc.

Two parameterisations are implemented in `rat2.cir` (`taperlaw`, `tap`):

* Exponential: A(x) = (b^x − 1)/(b − 1), b = ((1 − t)/t)², so A(0.5) = t. t = 0.15 gives b = 32.1;
  t = 0.10 gives b = 81.
* 2-segment piecewise linear: A(x) = 2t·x for x ≤ 0.5, then t + 2(1 − t)(x − 0.5).

| x | exp t=0.10 | exp t=0.15 | exp t=0.20 | PWL t=0.10 | PWL t=0.15 | linear |
|---|---|---|---|---|---|---|
| 0.0 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.00 |
| 0.1 | 0.0069 | 0.0133 | 0.0213 | 0.0200 | 0.0300 | 0.10 |
| 0.2 | 0.0176 | 0.0322 | 0.0494 | 0.0400 | 0.0600 | 0.20 |
| 0.3 | 0.0342 | 0.0589 | 0.0865 | 0.0600 | 0.0900 | 0.30 |
| 0.4 | 0.0600 | 0.0966 | 0.1354 | 0.0800 | 0.1200 | 0.40 |
| 0.5 | 0.1000 | 0.1500 | 0.2000 | 0.1000 | 0.1500 | 0.50 |
| 0.6 | 0.1621 | 0.2255 | 0.2852 | 0.2800 | 0.3200 | 0.60 |
| 0.7 | 0.2584 | 0.3324 | 0.3976 | 0.4600 | 0.4900 | 0.70 |
| 0.8 | 0.4079 | 0.4836 | 0.5460 | 0.6400 | 0.6600 | 0.80 |
| 0.9 | 0.6399 | 0.6974 | 0.7417 | 0.8200 | 0.8300 | 0.90 |
| 1.0 | 1.0000 | 1.0000 | 1.0000 | 1.0000 | 1.0000 | 1.00 |

Consequences with t = 0.15 (exponential) and ideal-op-amp HF gain 1 + R/(47‖560):

| knob | 0.1 | 0.3 | 0.5 | 0.7 | 0.9 | 1.0 |
|---|---|---|---|---|---|---|
| DIST R | 1.3k | 5.9k | 15k | 33k | 70k | 100k |
| HF gain (dB) | 30.0 | 42.7 | 50.8 | 57.7 | 64.1 | 67.3 |
| FILTER fc, diodes conducting | 16.4 kHz | 6.4 kHz | 2.9 kHz | 1.38 kHz | 676 Hz | 475 Hz |

Bourns also notes that low-side end resistance can be up to 4 Ω. The netlist floors every pot section at
`rend` = 1 Ω.

## 4. Footswitch, bypass and LED (not in the audio path when the effect is ON)

* **RAT2 (F86, 7-86 Rev C):** DPDT. SW1-A switches the input jack J2 between C3 (effect) and a bypass wire
  to the output jack J3. SW1-B switches J3 between the VOLUME wiper (effect) and the gate of the LED driver
  Q2. The LED driver is Q2 (JFET; drain at +9 V; source → R14 2.2K → LED D6 → D4, D5 "4001" → ground) with
  R13 "22M" from the gate to +9 V.
  * Effect ON: the gate floats high through 22 MΩ, so the LED is on.
  * Bypass: the gate is tied to the bypass signal line, which the guitar or amp holds at DC ground, so the LED
    is off.
  * Steve Kiraly designed this "millennium bypass" variant (JHS).
  * MR draws the same circuit with R13 10M, D4/D5 1N4148, R14 2k2 (**[?]** 22M versus 10M; 1N4001 versus
    1N4148). MR also notes that 3PDT-equipped RAT2/Turbo/YDR units drop R13, D4, D5 and Q2, and use 1k6 for R14.
  * In effect-ON state nothing of this loads the audio path, so the emulation can ignore it.
* **Original RAT (MR "FS1", RGK-B):** DPDT true bypass. In bypass the circuit input is grounded.
* In bypass the RAT is true bypass: the input jack is wired to the output jack (MAN: "When off the RAT does not
  reduce signal quality"). In the RAT2, the bypass line is then also loaded by the Q2 gate network (22M to +9 V),
  a negligible load.

## 5. Power supply and DC conditions (ngspice, ideal 9 V)

V(v9) = 8.973 V (27 mV across R1 = 0.57 mA quiescent: op-amp 0.29 mA + JFET 0.22 mA + divider 0.045 mA),
V(vref) = 4.486 V, V(opout) = 4.485 V, V(clip) = 0, V(src) = 2.216 V.

GGG's measured clone voltages are pin 6 4.5 V, pin 2 4.5 V, pin 3 4.1 V and pin 1 8.4 V. Pin 3 at 4.1 V is
what a 10 MΩ DMM reads through their 1 MΩ bias resistor (4.5 × 10/11 = 4.09 V), so it is a measurement
artefact, not an offset. In the netlist, V(inp) − V(vref) = −3 mV, which is the 1.5 nA model bias current
in 2M2.

## 6. Version differences

| Version (JHS naming / dates) | Circuit differences vs. RAT2 (LM308) above | Sources |
|---|---|---|
| Bud Box (1977/78, 12 circuits in 11 boxes) | Unknown. No trace located | JHS |
| Big box V1 "Tone" (1979–80) | TONE pot wired reversed (CW = brighter, lugs 1 and 3 swapped). Early big-box values: C8 33p, C11 3n, R9 1k5, DIST A150k. Possibly an input JFET buffer ("version A": BF245A follower 1M/1k/1k, 1 µF into 100k/1n at pin 3, bias 33k/33k + 47 µF, output follower Rs 1k, 10 µF, 10k pull-down) | JHS, MR, RGK-O, GGG instructions ("version 'A' had a JFET on the input"). **[?]** JHS says the circuit was identical until 1981 apart from the filter |
| Big box V2 "Filter" (1981–84) and small box "White Face"/"Black Face" (1984–88) = "the original RAT" | FILTER wiring as RAT2. R4 1M, no R12, no R15, C13 1 µF, D2/D3 1N914. RGK-B (Rev 7-81): R1 100 Ω, C7 4.7 µF, R10 to +4.5 V, C10 1 nF **[?]** | MR (RAT column), ES, RGK-B, RGK-F |
| RAT2 (1988–, LED) | This document: R4 2M2, R12 2M2 (F86 1M), R15 10k, C13 10 µF, 1N4148, R9 1k6 **[?]**, LED/millennium bypass | MR, F86, FC, JHS |
| RAT2 with OP07DP (from 1995/96 per JHS timeline; "around 2002-2003" per MR; "around 2005" in the JHS myths section) **[?]** | U1 = OP07(C/D)P. C8 still fitted across pins 1 and 8, which on the OP07 are **offset-null** pins (TI SLOS099H), not compensation. Otherwise identical | JHS, MR, AION, PedalPCB thread, Coda |
| Chinese RAT2 (Neutrik, 2008–) | C11 2.7 nF. Otherwise "same circuit as the original RAT" (MR box). 3PDT switching in later units | MR, JHS |
| Turbo RAT (1989–) | D2/D3 = 5 mm red LEDs (Vf about 1.6–1.8 V). Gate bias R10 2M2 to ground + R16 2M2 to +9 V. R4/R12 2M2, R15 10k, C13 10 µF. Later units OP07 | JHS, MR, FC (FC shows no R16 **[?]**; RGK-F table says 1N34A and R10 100K **[?]**) |
| Vintage RAT reissue (1991) | Original-RAT values (R4 1M, no R12/R15, C13 1 µF), 1N4148 ("sometimes BAT41" per MR note) | MR, JHS |
| You Dirty RAT (2004) | D2/D3 = 1N34A germanium (later reportedly BAT41 Schottky, per Tampco). OP07 only. R4 1M, R10 1M, C13 "?" | JHS, MR, FC, Tampco |

Model notes for the variants: LED and germanium/Schottky models are in `private/reference/spice/diodes.lib`. The
LED part is a generic placeholder (**[?]** the Turbo's actual LED is unknown). The OP07 changes the op-amp
model (see the research doc), including a narrower output swing.

## 7. Designator cross-reference

| MR / F86 | ES | AION 1.1 | GGG Rodent | PPCB | FC Rattus | Function |
|---|---|---|---|---|---|---|
| R1 | R10 | R9 | R14 | — | R1 | supply series |
| C1 | C11 | C12 | C11 | C11 | C1 | reservoir |
| D1 | D3 | D1 (series) | D3 | D3 (series) | D1 | polarity |
| C10 | C12 | C11 | C13 | — | C10 | supply bypass |
| R2/R3 | R11/R12 | R10/R11 | R15/R16 | R11/R12 | R2/R3 | bias divider |
| C2 | C13 | C13 | C12 | C12 | C2 | Vref cap |
| R12 | R1 | RPD | R17 | R1 | R5 | input pull-down |
| C3 | C1 | C1 | C1 | C1 | C3 | input coupling |
| R4 | R2 | R1 | R1 | R2 | R4 | + bias |
| R5 | R3 | R2 | R2 | R4 | R6 | series input |
| C4 | C2 | C2 | C2 | C4 | C4 | input shunt |
| C8 | C3 | C3 | C4 | C6 | C8 | compensation |
| C5 (F86 C7) | C4 | C4 | C3 | C5 | C5 | feedback cap |
| DIS (F86 RR1) | R_DISTORTION | DISTORTION | R3 | DIST | Distortion | gain pot |
| R7 / C6 | R5 / C6 | R4 / C6 | R4 / C5 | R3 / C2 | R7 / C6 | 560 Ω leg |
| R6 / C7 (F86 C5) | R4 / C5 | R3 / C5 | R5 / C6 | R5 / C3 | R8 / C7 | 47 Ω leg |
| R8 / C9 | R6 / C7 | R5 / C7 | R6 / C7 | R6 / C7 | R9 / C9 | to clipper |
| D2/D3 | D1/D2 | D2/D3 | D1/D2 | D1/D2 | D2/D3 | clippers |
| FIL (F86 RR2) | R_TONE | FILTER | R7 | FILTER | Filter | filter pot |
| R9 | R7 | R6 | R8 | R7 | R10 | filter series R |
| C11 | C8 | C8 | C8 | C8 | C11 | filter cap |
| C12 | C9 | C9 | C9 | C9 | C12 | buffer coupling |
| R10 | R8 | R7 | R9 | R8 | R11 | gate R |
| Q1 | Q1 | Q1 | Q1 | Q1 | Q1 | JFET |
| R11 | R9 | R8 | R10 | R9 | R13 | source R |
| C13 | C10 | C10 | C10 | C10 | C13 | output coupling |
| R15 | — | — | — | — | R14 | output pull-down |
| VOL (F86 RR3) | R_VOLUME | VOLUME | R11 | VOLUME | Volume | volume pot |

## 8. Disagreements (and the choice made in `rat2.cir`)

| # | Item | Values found | Choice and reason |
|---|---|---|---|
| D1 | R12 input pull-down (RAT2) | 1M (F86 1986), 2M2 (MR 1992 trace, FC), absent (original RAT per MR/RGK) | 2M2 (later traced unit). Affects input impedance only |
| D2 | R4 + input bias | 2M2 (MR RAT2/Turbo, FC), 1M (ES, RGK-B/F, PISO, all clones, MR original), 220K (RGK-F "LED Rat") | 2M2 for RAT2. The HPF moves from 7.2 Hz to 3.3 Hz (inaudible); noise and input-Z change slightly |
| D3 | R9 | 1k6 (MR traces), 1k5 (all others) | 1k6. The difference is negligible: fc at FILTER CCW is 30.1 versus 32.2 kHz |
| D4 | C11 | 3n3 (all), 2n7 (Chinese RAT2, MR), 3n (earliest, MR) | 3n3 |
| D5 | C13 / R15 | 10 µF + 10k (RAT2/Turbo: MR, F86 R15, FC, RGK-F) vs 1 µF, no R15 (original: MR, ES, clones) | 10 µF + 10k. This changes the buffer load and gain (0.78 versus about 0.86) and the output impedance |
| D6 | C7 (47 Ω leg capacitor) | 2.2 µF (all but one), 4.7 µF (RGK-B 1981) | 2.2 µF. 4.7 µF would move the 47 Ω-leg corner from 1.54 kHz to 720 Hz, which is audible |
| D7 | R6 | 47 Ω (all, JHS story), 470 Ω (FC table) | 47 Ω. FC is treated as a typo; 470 would cut maximum gain by about 12 dB |
| D8 | R1 | 47 Ω (MR, ES, RGK-F/O, PISO, GGG), 100 Ω (RGK-B; MR "RAT2 sometimes"), 470 Ω (FC) | 47 Ω. Supply only |
| D9 | C8 | 30 pF (all RAT2 sources), 33 pF (early big box per MR; AION 1.1 advice) | 30 pF |
| D10 | DIST pot | A100k (all RAT2), A150k (early big box: MR, RGK-O) | A100k |
| D11 | Diodes | 1N4148 (MR RAT2/VR, JHS, RGK, PISO), 1N914 (MR RAT, ES, clones) | 1N4148, with the identical LTspice model |
| D12 | Q1 gate return | ground via 1M (most, F86), +4.5 V via 1M (RGK-B), 100K (RGK-F table), 2M2 + 2M2 to +9 V (Turbo, MR) | 1M to ground |
| D13 | Q1 type | 2N5458 (most), 2N5457 (AION legacy), BF245A (RGK-O) | 2N5458, LTspice model (IDSS 4.06 mA, Vp −2.88 V). Device spread is large; fit Vs to captures if needed |
| D14 | C2, C10, D1 types | see §2.1 | No audio effect with an ideal battery |
| D15 | LED circuit | R13 22M, D4/D5 1N4001 (F86) vs 10M, 1N4148 (MR) | Omitted (not in the effect-ON audio path) |
| D16 | OP07 changeover date | 1995/96 (JHS timeline), 2002–03 (MR), about 2005 (JHS myths section) | Documented only. Matters for "which RAT2 was captured" |
| D17 | Turbo diodes / R10 | LEDs + 2M2/2M2 (JHS, MR) vs 1N34A + 100K (RGK-F table) vs no R16 (FC) | LEDs + 2M2/2M2 |
| D18 | Taper law | 10 %, 15 % or 20 % at mid; exponential or 2–3 segment | Default 15 % exponential. **Fit to captures** |
| D19 | Input impedance | "494 kΩ" (ES formula), 1 MΩ (MAN), frequency dependent (this analysis) | Model the network itself; do not use a lumped Zin |
| D20 | FILTER direction | CW darker (RAT2, MAN) vs CW brighter (1979–80 "Tone") | CW darker |
