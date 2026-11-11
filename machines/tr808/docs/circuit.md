# Roland TR-808 voice board: circuit transcription

This file transcribes the TR-808's analog circuitry, as Lemus models it, from the Roland service notes: the common trig and accent, the noise generator and the six-oscillator metallic source, the eleven voices, and the mixer. The netlists in `core/netlists/*.cir` are the transcription itself, one per circuit. This document explains them part by part, cites where on the schematic each part comes from, and says where a netlist interprets or departs from the drawing. Part designators are Roland's. The main board (OP3116-130) and the voicing board (VG3116-140) reuse the same numbers (there is a Q25 and an R126 on each), so every section names its board.

Three kinds of number appear here:

- **Read** values are taken from the drawing. They are plain in the text, with a citation.
- **COMPUTED** values are my own arithmetic. The formula or the assumption is given.
- **SIM** values come from running a netlist through the voices' own solver (`build/tr808/circuitrun`, or `voicerun --probe` for the whole board).

**UNCERTAIN** marks anything illegible, inferred, or not settled by the sources.

---

## 0. Sources, citation keys, conventions

`private/` is `machines/tr808/private`, which links to the private tree (`../private/tr808` from the repo root). Nothing under it is in this repository.

| Key | Document | Local file(s) |
|---|---|---|
| **SN** | Roland *TR-808 Service Notes*, First Edition, Jun. 15 1981, carrying "Second Printing (July 12, 1983 E2)" in the header and "3rd Printing June 1986" in the footer. 16 A3 pages, scanned 1-bit at 400 dpi. Every online copy is this one scan (`private/docs/sources.md`). Pages: p.4 trigger and accent theory, p.5 Fig. 11/12 and BD theory, p.6 theory of the other voices with Fig. 13, p.7 block diagram, **p.8 main board, logic half**, **p.9 main board, voice half**, **p.13 power supply, voicing board, Jack board B**, p.14 adjustment and the "Checking voices" table, p.15 design changes, p.16 parts list | `private/docs/tr808-service-notes.pdf`; native 400 dpi page bitmaps `private/docs/schematic/hires/sn-p-{7..13}-400dpi.png`; 300 dpi renders `schematic/sn-p-N.png`; crops `private/docs/crops/` |
| **SN-SF** | Synthfool's 2000 scan of the same three schematic pages, cut into six A4 sheets (sf-1/2 = p.13, sf-3/4 = p.8, sf-5/6 = p.9). The drawings are the same at lower resolution. Useful as a second look. | `private/docs/schematic/alt-synthfool-2000/sf-{1..6}.png` |
| **IDX** | Page index of SN with verbatim transcriptions of the specifications, adjustment table, voice table, design changes and "lot" notes | `private/docs/service-notes-index.md` |
| **DIG** | Research digest of the published analyses (Werner, Abel and Smith on the BD and CY; clone notes; data sheets), compiled 2026-10-11 | `private/docs/papers/digest.md` |
| **W-BD / W-CY** | Werner, Abel, Smith: DAFx-14 bass drum and ICMC/SMC 2014 cymbal papers (cited through DIG) | `private/docs/papers/academic/` |
| **DS** | Data sheets: NEC 2SC945/2SA733, Toshiba 2SA1015/2SK30ATM, Rohm 1S2473, NEC/Renesas µPC4558, Hitachi HD14584B, Motorola MC14584B, Matsushita/Panasonic AN6912, Alfa AS662D, Rohm BA6110 | `private/docs/datasheets/` |
| **NET** | The netlists | `core/netlists/<voice>.cir` |
| **VB** | `struct Calibration`, `unitCalibration()`, the oscillators and the noise scaling | `core/voiceboard.h`, `core/voiceboard.cpp` |

**Grid references.** SN pp.7–12 have a grid of columns 1–40 and rows A–V. Where a netlist header gives one (e.g. "p.9, grid E-K 27-38"), it is repeated here. Page 13 has no grid, so its blocks are named by position.

**Conventions.**

- **Capacitors:** "x/y" is µF/volts (e.g. "33/6.3"); a bare decimal is µF (".015").
- **Pot rotation:** 0 is fully anticlockwise (FCCW) and 1 is fully clockwise (FCW), as `struct Controls` uses. Netlist syntax (core/circuit.h): `P<name> a wiper b <ohms> <control> [taper]`. The control moves the wiper from `b` (rotation 0) to `a` (rotation 1).
- **Tapers:** Roland's letters are Japanese: A = audio (log), B = linear, C = reverse log. Netlist tapers:
  - lin is the default.
  - `a<mid>` gives the share of the track between `b` and the wiper at mid rotation, as two straight segments. `a0.15` puts 15 % of the track there.
  - `c<mid>` is the reverse: `c0.15` puts 85 % between `b` and the wiper at mid.
- **Rheostats:** a pot whose wiper is tied to one end is written with the wiper node equal to `a` or `b`.
- **`.param` lines** hold the schematic's values that a production lot or a fitted unit may change. VB overrides them by `"voice.param"`.
- **`.part` lines** split a voice into partitions that run in order and sleep on their own. They are a runtime matter (`docs/model.md`, "Partitions") and are not discussed further here.
- **Supply rails:** every netlist rail (`b2`, `nb3`, ...) is an ideal 15 V source.
- **Op-amps:** every µPC4558 is `X... opamp {gbw} [1e5 {opsat} {sr}]`. That is a single-pole amplifier, clipping softly at ±13.5 V and slewing at 1 V/µs where the netlist gives `opsat` (§16).

**The bridged T.** Almost every voice uses the bridged-T network of SN p.5 Fig. 11. R1 runs from the junction of C1 and C2 to ground, and R2 bridges C1 and C2 across the op-amp. SN prints:

f = 1 / (2π √(R1·R2·C1·C2)),  Q = √(R2/R1) / (√(C1/C2) + √(C2/C1)).

All "Fig. 11" frequencies below use this formula. It ignores the extra feedback and loading that each voice adds.

**The "Checking voices" table (SN p.14, crops `sn-p14-voice-table-*.png`).**

- **Conditions.** SN measures at each MULTI OUT with LEVEL FCW and the other knobs at 12 o'clock, first with ACCENT FCCW and then FCW. Decay runs to 1/10 of the first swing's Vpp.
- **Status.** SN calls the values "typical and variable". Each voice section ends with its row.
- **Lemus check.** tools/service/check.py runs the same checks against the emulation, with tolerances of 25 % (amplitude, frequency) and 40 % (decay).
- **Suspect entries (DIG).** The SD frequencies predate "PORTION CHANGED" (§5), and the CP accent figure (2 Vpp) is lower than the normal one (6 Vpp).

---

## 1. Power rails

**Supply (SN p.13, POWER SUPPLY PS3116-051; crops `sn-p13-power-supply-*.png`).**

- **±15 V.** Bridge D2 (W-02) and C12/C13 1000/35 feed IC2 TA7179P, a ±15 V tracking regulator, with pass transistors Q1 2SB596(O) and Q2 2SD880(O) (R2/R5 3.3 Ω, R3/R4 15k). The secondary is printed "RED-BLK --- 23VDC ( x 2)" at 0.15 A.
- **+5 V.** Bridge D1 and IC1 µA7805 with C3 1000/16, from "YEL-YEL --- 10VDC". This feeds the CPU and logic, and the "5V" end of the accent pot (§2.3).
- **Back-up.** "1.5V x 3" cells on the BU rail keep the RAM.

**Decoupled rails.** Each circuit group draws its supply through 100 Ω and 47/16 µF. SN p.14 (troubleshooting) calls the "B" supplied to each group "independently filtered".

| Board | Rails | Parts | Users (from the drawing and NET) |
|---|---|---|---|
| Main (p.8, cols 31–37, rows G–J) | +B1…+B8, −B2…−B8 | R151, R179, R209, R237, R265, R293, R329, R380 (+); R178, R208, R236, R264, R292, R328, R379 (−) | B1 common trig and accent (IC6, Q31–Q34); B2 BD; B3 SD, plus IC13 (the SD and LT output amps); B4 LT; B5 MT, plus IC16 (the MT and HT output amps); B6 HT; B7 RS/CL (IC19–IC21); B8 CP/MA |
| Main (p.8, noise generator) | +B9 / −B9 | R125 100 Ω + C28 47/16 from +B1; R126 100 Ω + C30 47/16 from −B3 | Q35 and IC24 |
| Voicing (p.13, top right) | +B1…+B5, −B1…−B5 | "47/16 × 5", "100Ω × 5" | B1 IC2 (trig buffer and CB filter), CB, CY (Q16–Q20, IC5, IC6); B2 Q25 (CY); B3 hats (Q21–Q31, IC7); B4 Q9 and IC3; B5 mixer IC4 |

NET models every rail as an ideal 15 V source. Local RC decoupling appears only where it is part of a voice's circuit: R121/C25 at Q32 (accent), R193/C55 at Q47 (snappy), R86/C35/R85 at Q19 (cymbal), and R136/C61 with R137/R138 at Q24/Q28 (hats).

**The 4.7 V rail for the metallic oscillators (SN p.13, voicing board top left).**

- **Q9** (NPN, 2SC945 by the note on p.8) is an emitter follower from B4. Its base sits on the divider **R61 10K** (to B4) / **R60 5.6K** (to ground). The emitter has **C12 100/6.3**, and the node is circled "5V".
- COMPUTED: the base sits at 15 × 5.6/15.6 = 5.38 V, so the emitter is ≈ **4.7 V**. DIG makes the same reading; Werner's cymbal paper assumes 5 V.
- IC1 HD14584 runs from this rail, and so do the squares' amplitudes. VB's oscillators are 0 to 4.7 V (`Square::level`).

---

## 2. CPU → voice interface: step pulse, common trig, accent, gates

### 2.1 Instrument data lines (SN p.3 pin table, crop `sn-p3-cpu-pin-table.png`; p.8 right of IC4)

The CPU (IC4 µPD650C-085) puts one line per voice, plus accent, on three ports while the common trig fires. Each line is 0/5 V, and each "AND"s with the trig in its voice's gate (§2.5).

| Port (pins) | bit 0 | bit 1 | bit 2 | bit 3 |
|---|---|---|---|---|
| PE0–3 (12–15) | CP | RS | HT | MT |
| PD0–3 (8–11) | CH | OH | CY | CB |
| PF0–3 (16–19) | LT | SD | BD | AC |

- The main-board voices take their lines directly. CB, CY, OH and CH reach the voicing board over jumpers J5 (CB), J6 (CY), J8 (CH) and J7 (OH). The narrowed trig goes over J4 "TRIG".
- VB uses its own bit order: `kBitBD … kBitCH, kBitAC` (voiceboard.h).
- CP and MA share one line and one gate. SW12 picks the circuit.

### 2.2 Step pulse and the 1 ms monostable (SN p.4 "Trigger Gate", Fig. 6; p.8 cols 24–29, rows E–G)

- **Pulse.** PI2 (pin 32) gives a "10µs" pulse per step. It goes through R92 to Q27, whose collector (R97 33k to B1) drives **C23 .001** into IC6 TC4011BP pin 8.
- **Monostable.** IC6 is two NAND gates:
  - pin 8 has **R112 3.3k** to the node that **R100 680K** holds from pin 11;
  - the output, **pin 10**, drives **C27 .001** into pin 12, with **R102 680K** ("SURFACE MOUNTING") from B1 and **R101 100K** ("10K for 1~5 lot") in series;
  - **C201 22P** sits on pin 10.
- **Levels.** Pin 10 rests at B1 (15 V) and falls to 0 V for the pulse. SN gives the low period as "the sum of the time constants of R100 x C23 and R102 x C27" (two × 0.68 ms), "approx. 1ms".
- SN p.14: "Common Trig with pulse width longer or shorter than 1ms will be a cause of deteriorative voices."
- **Lemus:** the monostable is not a netlist. `VoiceBoard::setLines` starts an ideal pulse of `Calibration::monoMs` = **1.0 ms** on the rising edge of the CPU's step pulse. accent.cir's input `mono` is 15 V at rest and 0 V during that pulse.

### 2.3 Common trig and accent: Q31–Q34, VR3 (accent.cir; SN p.8 cols 27–33, rows C–F; theory p.4)

| Part | Connections (read) |
|---|---|
| **VR3 AC LEVEL 10K(B)** | Pin 3 to B1, pin 1 to the 5 V logic rail; wiper → **R114 4.7K** → node x |
| **Q34** (NPN) | Collector at x, emitter on the 5 V rail. Base: **R122 33K** from B1 and **R123 15K** to Q33's collector. |
| **Q33** (NPN) | Base: **R119 22K** from the ACCENT line (PF3), **R118 10K** to ground; emitter grounded |
| **R120 4.7K** | x → node y (Q32's base) |
| **Q31** (NPN) | Collector at y; base **R117 47K** from IC6 pin 10; emitter grounded |
| **Q32** (NPN) | Collector at C25 47/16, fed by **R121 100** from B1; base y; emitter = **COMMON TRIG**, with **R113 4.7K** to ground |

**How it works (SN p.4).**

- Between steps IC6 pin 10 is high, so Q31 holds Q32's base at ground and there is no trig.
- During the 1 ms, Q32 follows x.
- **No accent:** Q33 is off, so R122 saturates Q34 and clamps x to the 5 V rail. SN: "the base of Q32 becomes approx. +5V and trig amplitude is approx. 4V".
- **Accent:** Q33 pulls Q34's base to ≈ 4.7 V (COMPUTED, the R122/R123 divider), so Q34 lets go and x is VR3's wiper. SN: "a voltage between 5V and 15V according to VR3 setting is applied to the base of Q32, and is converted into trig pulses of approx. 4—14V."

**SIM (circuitrun accent.cir, unloaded bus):**

| Case | Trig amplitude |
|---|---|
| No accent | **4.34 V** (4.34–4.36 V across VR3) |
| Accent, AC LEVEL 0 / 0.5 / 1 | **4.32 / 9.25 / 14.22 V** |

The full board gives the same values (`voicerun --probe trig`: 4.36 V and 14.22 V), because NET carries the trig to the voices as a one-way signal.

**Not modelled.** These loads hang on the bus and are not in NET:

- the eleven gates' base currents, which flow only through the gates whose data is high;
- R58 33K / R57 10K, a divider off the bus towards the left of p.8;
- the trigger-output circuits (IC11 AN6912 one-shots for the CB, CP(MA) and AC TRIG OUT jacks: "+15V, 20ms; 1KΩ", SN p.1).

### 2.4 The narrowed trig for CB, CY, OH, CH: IC2a (mtrig.cir; SN p.13, voicing board top centre)

SN p.4: for these four "trig variation range is narrowed to 7V—14V by 1/2 IC2 (pins 1—3) on the voicing board to increase S/N ratio".

**Circuit (read).**

- The trig enters through two paths:
  - **R19 100K** to pin 3;
  - **R18 3.3K** to a node clamped by **D1** to ground, then **R20 15K** to pin 3.
- **R21 3.3K** from pin 3 to ground.
- Gain 1 + R23/R22 = 1 + 100K/3.3K = **31.3**.

COMPUTED: D1 holds R18's far end at a diode drop, which adds a fixed ≈ 0.6 V × 3.2k/18.2k ≈ 0.1 V at pin 3 (≈ 3.3 V at the output). The trig itself reaches pin 3 through R19 at only ≈ 0.026 V/V (≈ 0.82 V per volt at the output). So the low end is lifted, the range is compressed, and the top clips at the op-amp's rail.

**SIM (circuitrun mtrig.cir):**

| Trig in | ntrig out |
|---|---|
| 2 V | 4.6 V |
| 4.34 V | **6.8 V** |
| 9.25 V | **11.1 V** |
| 14.22 V | **12.8 V** (clipped at `ntsat`; 11.4 V in the fitted unit) |

IC2a's output limit is its own parameter, `ntsat`, not the other op-amps' 13.5 V. IC2 runs from +B1: +15 V through R165 (100 Ω), which also feeds IC5 and IC6 (three µPC4558s at 3 mA typical), so about 14.1 V. The µPC4558 swings 1-2 V under its rail at these loads (±14 V typical into 10k, ±13 V into 2k; ±12 / ±10 V minimum; the accented hit draws 1.2-2.4 mA for CY/OH/CH, 13 mA falling to 3 mA for the cowbell), so a typical part tops out near **12.8 V** (the netlist). The archive.org unit's four metal voices all accent as **11.4 V** (within the part's spec), which `unitCalibration()` uses: Fischer's hits aren't accented, so his recordings can't set it. The notes' "7V-14V" top is beyond what IC2a can reach on B1.

### 2.5 Voice gates (SN p.9 and p.13; SN p.7 block diagram)

Every voice ANDs the trig with its data line in the same pair. An NPN, whose base sits on a 22k/10k divider from the data line, pulls a PNP's base low. The PNP's emitter is on the trig bus with a 4.7k base-emitter resistor, so its collector passes the trig pulse while the data line is high.

| Voice | NPN (data divider) | NPN collector → PNP base | PNP (B–E resistor) | PNP load / next stage |
|---|---|---|---|---|
| BD | Q39 (R152 22K / R153 10K) | **R154 10K** "(22k for 1~5 lot)" | Q40 (R155 4.7K) | R157/R158 (Q41), C40/R163 (§4) |
| SD | Q45 (R180 22K / R181 10K) | R182 10K (22k 1~5 lot) | Q46 (R183 4.7K) | resonator inputs, VR9 |
| LT/LC | Q50 (R211 / R212) | R213 10K (22k 1~5 lot) | Q51 (R214 4.7K) | R215 4.7K |
| MT/MC | Q53 (R240 / R241) | R242 10K (22k 1~5 lot) | Q54 (R243 4.7K) | R251 4.7K |
| HT/HC | Q56 (R267 / R271) | R268 10K (22k 1~5 lot) | Q57 (R269 4.7K) | R270 4.7K |
| RS/CL | Q59 (R297 / R303) | R298 10K (22k 1~5 lot) | Q60 (R299 4.7K) | R300 22K |
| CP/MA | Q28 (R103 / R104) | R106 10K (22k 1~5 lot) | Q29 (R105 4.7K) | SW12 → CP (Q69, IC23) or MA (D67); also C.P. TRIG out |
| CB | Q1 (R4 22K / R5 10K) | R9 22K | Q5 (R14 4.7K), on ntrig | R122 1K, D2 → C9 |
| CY | Q2 (R3 / R6) | R10 22K | Q6 (R15 4.7K), on ntrig | R83 22K, R84 → Q19 |
| OH | Q3 (R1 / R8) | R12 22K | Q7 (R17 4.7K), on ntrig | R131 22K / R130 10K (Q21), R135 → Q24 |
| CH | Q4 (R2 / R7) | R11 22K | Q8 (R16 4.7K), on ntrig | R13 22K, R141 → Q28 |

- **NPNs** are 2SC945(P) and **PNPs** 2SA733(P) by the p.8 note.
- The **voicing-board pairs** became 2SC2021R (Q1–Q4, from serial 010500) and 2SA937Q (Q5–Q8, from 051850) for package height (SN p.15). NET keeps 2SC945/2SA733: the pairs work as saturated switches.
- The **main-board 22k → 10k** change (SN p.15 #6, serial 010600) was made to "Ensure sufficiency of gate drive signal voltage at lower COMMON TRIG amplitude". NET uses 10k throughout, as `.param r154 r182 r213 r242 r268 r298 r106`.

---

## 3. Noise generator and the metallic source

### 3.1 Noise generator (noise.cir; SN p.8 cols 30–37, rows J–P; crop `sn-p8-noise-generator-tm4-tp4-both-versions.png`)

**Noise source.** **Q35 2SC828 "RNZ"** (parts list: "2SC828 (R) selected noise") has its emitter-base junction in breakdown:

- B9 → **R124 100** (with **C29 47/16** to ground) → **R128 1M** → Q35's emitter.
- The base is grounded; no collector connection is drawn.
- The noise leaves the emitter through **C31 1/50** to IC24a pin 3, with **R131 100K** to ground.

**Gain stage, IC24a (the "6 lot ~" circuit drawn below the first; SN p.15 #5).**

- Feedback **R130 1M** with **C202 22P** ("SURFACE MOUNTING") across it.
- Pin 2 goes to ground through **TM4 22K(B)** (a rheostat) and **C200 10/16**.
- The original circuit, drawn above, had R127 4.7K + R129 330K and a DC path to ground, with R131 300K.
- Change #5 (serial 010600): "Variations in UPC4558 bias current are transferred to 1/2 IC24 output as an offset … * Decouple DC".
  - SN p.15 prints "R311 330K → 100K", but the p.8 drawing shows R131 300K → 100K. R311 is a rim-shot part, so R131 is meant (IDX).

**White-noise output.** The IC24a output is **WN** and **TP4**. SN p.14: "AC voltmeter to TP-4 … TM-4 130mV rms". p.8 note: "W.N. LEVEL 130mV r.m.s".

**Pink noise, IC24b.** **R133 22K** from WN, then **C32 .039** to the output (a unity follower), **R132 22K**, and **C33 .018** to ground: a Sallen-Key low-pass.

- COMPUTED: f₀ = 1/(2π·22k·√(39n·18n)) = **273 Hz**, Q = 0.74.
- Its output is **PN**. It feeds only the three tom noise paths (§6–8), and only in tom mode.

**Lemus.**

- The breakdown noise is an input `nz`: white noise in volts at Q35's emitter.
- VB scales its density so TP4 reads **130 mV rms** (`Calibration::noiseRms`). It uses IC24a's gain of ≈ 92 (TM4 at mid, 1 + 1M/11k) up to the 7.2 kHz corner of R130/C202: "the rms is the density x sqrt(92² x 7.23 kHz x π/2)".
- TM4 stays at mid rotation in noise.cir. The level is set by `noiseRms`, not by the trimmer.

### 3.2 The six oscillators (VB; SN p.13 voicing board left; IC1 HD14584)

Six HD14584 Schmitt-inverter RC oscillators each have R from output to input and C from input to ground:

| VB input | IC1 pins | R | C | Mix resistor | Frequency |
|---|---|---|---|---|---|
| o1 | 13 → 12 | **TM1 220K(B)** + **R44 150K** ("390K for 1~2 lot") | **C6 .022** | R50 120K | "COWBELL FREQ.1 1.85ms" (540 Hz) at **TP6**, "(1.25ms for 1~2 lot)" |
| o2 | 11 → 10 | **TM2 220K(B)** + **R45 100K** ("330K for 1~2 lot") | **C5 .018** | R48 120K | "COWBELL FREQ.2 1.25ms" (800 Hz) at **TP7**, "(1.85ms for 1~2 lot)" |
| o3 | 9 → 8 | R43 220K | C4 .018 | R46 120K | 522.7 Hz |
| o4 | 1 → 2 | R42 680K | C3 .01 | R39 120K | 304.4 Hz |
| o5 | 3 → 4 | R41 560K | C2 .01 | R37 120K | 369.6 Hz |
| o6 | 5 → 6 | R40 560K | C1 .018 | R35 120K | 205.3 Hz |

- **Fixed four.** The frequencies of o3–o6 are Werner's (W-CY §3, `Calibration::metalHz`). He computed them from the parts with the HD14584's *typical* thresholds at 5 V (V_T+ 2.7 V, V_T− 2.1 V): T = RC·[ln((V−V_T−)/(V−V_T+)) + ln(V_T+/V_T−)] = 0.483·RC. DIG reproduces all four with this formula.
- **Spread.** The data sheet's spread of thresholds (DS Motorola MC14584B: V_T+ 1.8–3.4 V, V_T− 1.6–3.2 V at 5 V) is the mechanism behind the unit-to-unit differences owners report (DIG).
- **Duty cycle.** The output is high while C charges. `Calibration::metalDuty` = **0.47** (COMPUTED from the typical thresholds: 0.232/0.483 = 0.48).
- **Trimmer ranges.** COMPUTED with the same formula (Werner's figures): TM1 spans 254–627 Hz and TM2 spans 359–1150 Hz.
- **Cowbell taps.** o1 and o2 also feed the cowbell through **R62 33K / R63 2.2K** and **R64 33K / R65 2.2K** dividers (§12).
- **Lemus.** The oscillators are not netlists. VB makes band-limited (polyBLEP) squares between 0 and 4.7 V at the calibrated frequencies and feeds them to metal.cir and cb.cir as inputs o1–o6.

### 3.3 Mixer and band-passes IC3 (metal.cir; SN p.13 voicing board lower left)

**Mixer.** The six 120K resistors sum into **R53 1K** to ground. This is a passive average, "TO CYMBAL".

The two band-pass filters are bridged Ts, with the input injected through a series RC into the inverting node and the + input grounded:

| | Input | Bridged T | Output | Fig. 11 (COMPUTED) |
|---|---|---|---|---|
| **IC3b**, high band | C11 .001 + R55 22K | C15 .0033, C16 .0033, R58 560 (centre), R59 82K (bridge) | pin 7 → "hi": CY Q16, Q17; OH Q27; CH Q30 | **7.12 kHz**, Q 6.05 |
| **IC3a**, low band | C10 .0033 + R52 33K | C13 .0068, C14 .0068, R56 560, R57 82K | pin 1 → "lo": CY Q18 | **3.45 kHz**, Q 6.05 |

Werner gives "around 3440 Hz" and "around 7100 Hz" (W-CY §4). SN p.6: "separated into high and low range components by two filters composed of IC3."

---

## 4. Bass drum, BD (bd.cir; SN p.9 cols 28–38, rows F–K; theory p.5; crop `sn-p9-bass-drum.png`)

Signal order:

**Gate.** Q39/Q40 (§2.5). Q40's collector is node t, the gated trig.

**Pulse shaper into the resonator.**

- **C40 .015** in parallel with **R163 100K**, from t to IC12a pin 3.
- At pin 3, **R162 4.7K** to ground and **D53** (anode at ground) clamp the negative edge.
- W-BD §4 calls this a nonlinear low shelf: the rising edge passes whole, while D53 clamps the falling edge at about a diode drop (0.71 V) below ground.

**Attack switch: Q41–Q43.**

- t → **R157 8.2K** / **R158 2.7K** → Q41's base. During the trig, Q41 empties **C38 .1**.
- **R156 1M** from B2 recharges C38. Q42 (base at C38) stays off until C38 reaches about a V_BE.
  - COMPUTED: 0.1 µF × 1 MΩ × ln(15/14.4) ≈ **4.1 ms**, against SN's "4ms which is 1/2 x 1/2 of 16ms of the inherent oscillation period".
- While Q42 is off, its collector p rises through **R160 22K**. That turns on **Q43** through **R159 100K**, and Q43 shorts **R165**.
- p also charges **C39 .033** through **D52**. When Q42 turns back on, C39 drives a negative pulse through **R161 1M** into the resonator's centre. SN: "a retriggering pulse".
- SN says the time constant is halved "when ACCENT is present". In the circuit the shift happens on every trigger; only its size grows with the trig amplitude (W-BD).

**Resonator: IC12a bridged T.**

- **C41 .015** (pin 2 to the centre m) and **C42 .015** (m to the output w); **R167 1M** pin 2 to w.
- Centre leg: **R166 6.8K** to node u, then **R165 47K** to ground. Q43's collector is at u.
- R161 (retrigger) and **R170 470K** (the decay feedback) also load m.
- Fig. 11, COMPUTED:
  - R1 = R165 + R166 = 53.8 kΩ gives **45.7 Hz**.
  - With Q43 on, R1 = 6.8 kΩ gives **128.7 Hz** (×2.8, not SN's ×2).
- Werner puts the loaded resonance at ≈ 49.5 Hz. SN's table says "18 (56)" ms (Hz).

**DECAY: IC12b.**

- **R164 47K** from w into pin 6.
- Feedback **R169 47K**, in parallel with **VR6 500K(B)** as a rheostat (pins 1–2) in series with **C43 33/6.3**.
- Pin 5 through **R168 22K** to ground.
- The inverted output z returns to m through **R170 470K**. SN: "decay time … can be controlled by adjusting feedback amount by VR6".
- NET: `P6 c43 c43 y {p6} decay`, i.e. 0 Ω at rotation 0, 500k at rotation 1.

**TONE.**

- w → **R171 220** → **VR5 10K(C)** (pins 3–2, wiper tied to pin 1), with **R172 10K** across it, → **C45 .1** to ground: a one-pole low-pass.
- COMPUTED: ≈ 7.2 kHz with VR5 at 0 Ω (FCW), ≈ 305 Hz with VR5 at 10k (FCCW).
- NET: `P5 k l l 10k tone c0.15`.

**LEVEL and output.**

- **C47 .47/50** → **VR4 100K(A)** (pin 3; pin 1 ground) → wiper → **C49 .47/50** → **R177 82K** → Q44's base, with **R176 100K** to ground.
  - The R177/R176 divider (0.55) is easy to miss. R177 is marked "SURFACE MOUNTING" on the drawing (presumably fitted on the foil side).
- **Q44** (NPN, collector B2) is an emitter follower: **R173 6.8K** to −B2, **C48 220P** to ground.
- Then **C50 1/50**, **R174 100K** to ground, **R175 1K** to the BD jack. The mix resistor is R5 22K (§15).

**Controls.** VR4 LEVEL 100K(A) (`a0.15`), VR5 TONE 10K(C) (`c0.15`), VR6 DECAY 500K(B) (linear).

**Params.** `r154` 10k (22k in lots 1–5), `r177` 82k, `r170` 470k, `p6` 500k (fitted 585k).

**SN p.14:** 3.5 / 10 Vpp (normal / accent), 18 ms (56 Hz), decay 50 / 300 / 800 ms.

---

## 5. Snare drum, SD (sd.cir; SN p.9 cols 2–14, rows A–H; theory p.6)

**Gate.** Q45/Q46 (§2.5), giving node t.

**Low resonator, IC14a.**

- Input: **R189 100K** in parallel with **C57 .0068**, from t to pin 3, with **R190 680** to ground.
- Bridged T: **C58** and **C59 .027**, **R196 680** centre, **R197 820K** bridge.

**High resonator, IC14b.**

- Input: **R191** from t to pin 5, with **R192 220** to ground.
- Bridged T: **C60 .0068** and **C61**, **R195 2.2K** centre, **R198 1M** bridge.

**"PORTION CHANGED" (p.9 box, crop `sn-p9-portion-changed-sd-cp.png`; no serial number given).** The drawing itself keeps the old values.

| Part | Drawn | Changed to | NET `.param` | Fig. 11, COMPUTED (old → new) |
|---|---|---|---|---|
| C58 | .027 | **.056** | `c58` | low: 249.6 → **173.3 Hz** |
| C61 | .0068 | **.015** | `c61` | high: 499.0 → **336.0 Hz** |
| R191 | 8.2k | **10k** | `r191` | high input 1/38 → 1/46 |
| R200 | 47k | **zero** | `r200` | tone path, below |

- `Calibration::lateSnare` (default true, nominal and fitted) applies the new values. R200 "zero" is written as 0.1 Ω.
- SN's table ("H 2.1 (476) / L 4.2 (238)") matches the *old* values.
- tools/service/check.py notes that Michael Fischer's unit measures **172 Hz**, i.e. the new ones.

**TONE.** **VR8 100K(B)** blends the two resonators:

- pin 3 from the high resonator through **R200** and **C65 .047**;
- pin 1 from the low one through **C63 .047**;
- the wiper goes through **R204 100K** to IC13a. Rotation 0 gives the low resonator.

**SNAPPY envelope.**

- **VR9 10K(B)** sits between t and **R199 1K** to ground. Its wiper → **R186 33K** → **Q47**, an emitter follower whose collector is fed through **R187 330** from a local rail (**R193 100** from B3, **C55 47/16**).
- Q47 charges **C51 .47/50**. The charge runs down **R184 15K** → **C52 .1** → **R185 15K** → node e2 (**C53 .001**).

**Noise VCA (swing type, SN Fig. 12).**

- e2 → **D54** → **Q48**'s collector (**C54 .0027** to ground; emitter **R188 100**).
- Q48's base: bias **R194 2.2M** from B3, white noise through **C56 .022**. The noise chops the envelope at the collector.

**High-pass and buffer.** e2 → **C66 .0018** → **C67 .0018** → **Q49** (emitter follower: **R203 22K** to −B3; **R202 47K** base to ground; **R201 22K** bootstrapped from the emitter to the C66/C67 junction).

- COMPUTED: a Sallen-Key high-pass at 1/(2π·1.8n·√(22k·47k)) = **2.75 kHz**, Q 0.73.
- Then **C69 .01** → **R205 27K** → IC13a.

**Output, IC13a.**

- Inverting sum at pin 2 (pin 3 via **R206 47K**).
- Feedback **R210 470K** in parallel with **C70 220P**, and with **VR7 100K(A)** (pins 1–2) in series with **C68 33/6.3**.
- **R207 1K** to the jack; mix R4 33K.
- This "LEVEL in the feedback" output stage recurs in LT, MT, HT, RS, CP, CY, OH and CH. With LEVEL at 0 the gain is zero; at full it is (470k ∥ 100k)/R_in.

**Controls.** VR7 LEVEL 100K(A), VR8 TONE 100K(B), VR9 SNAPPY 10K(B).

**Params.** `c58 c61 r191 r200` (late values), `r182` 10k, `r194` 2.2M (as printed in the fitted unit too: its doubling stood in for the noise trim, `docs/model.md`, "Calibration").

**SN p.14:** 3 / 10 Vpp, decay 60 ms.

---

## 6. Low tom / low conga, LT/LC (lt.cir; SN p.9 cols 15–27, rows B–H; theory p.6)

The three tom/conga circuits are the same apart from capacitor values. This section describes the low one in full. §7 and §8 give the mid and high ones' parts and differences.

**Gate.** Q50/Q51 (§2.5), giving t, with **R215 4.7K** to ground.

**Input.**

- t → **C73 .0068** → node a (**D56**, anode at ground, clamps the negative edge) → **R226 22K** → IC15a pin 5, with **R225 4.7K** to ground.
- **SW8** at LC adds **R224 1.5K** to ground at pin 5. SN: "Voices are switched by SW8 (C77 — frequency, R224 — level)".

**Resonator, IC15a.**

- **C76 .056** (pin 6 to the centre m); **C78 .012** (m to the output); **R228 820K** bridge.
- SW8 at LT puts **C77 .047** across C78.
- The inverted output of IC15b (**R229 33K**, **R227 33K**) returns to m through **R230 820K**.

**Tuning leg (the pitch glide).**

- m → **R231 1K** → VR11's wiper.
- m → the antiparallel germanium pair **D80/D81 1S188FM** → VR11 pin 1. VR11 pin 3 is grounded.
- VR11 is **500Ω(B)**. NET: `P11 0 tw vr11a 500 tuning`.

COMPUTED from this topology:

- With the diodes off, the leg is R231 + 500 Ω = **1.5 kΩ** at rotation 0 and R231 alone = **1.0 kΩ** at rotation 1.
- With the diodes fully conducting, it is 500 Ω and 333 Ω. Werner's "Req 333.3–500 Ω" (DIG) is this case.
- SN: "While the oscillation is large in amplitude immediately after triggering, it is on a higher frequency due to conductions of D80 and D81 … As the resonance is damped, its frequency is lowered".
- With the diodes off, Fig. 11 lands on the SN table (table below). This resolves DIG's open "×1.7" puzzle: the table's frequencies are the settled pitch, with the diodes out of the leg. With the diodes on, the same formula gives DIG's numbers (LT 137–168 Hz), the start of the glide.

**Tom noise (tom mode only).**

- t → **R219 4.7K** → **D55** → **C72 .039**: a held copy of the trig.
- It discharges through **R218 2.2M** into **C71 .047**, whose other end takes pink noise from **R216 15K / R217 3.3K**.
- → **R220 33K** → **Q52** (collector **R221 4.7K** from B4) → **C74 .047** → **R223 22K** to ground → **R222 22K** / **C75 .018** low-pass.
- **SW8** at LT passes it; at LC that end is grounded. SN: "Pink noise with a slightly longer decay time is mixed for Low Tom Tom to provide artificial reveberation" (the same text covers MT and HT).

**Output, IC13b.**

- **R233 100K** (resonator, from IC15b) and **R232 100K** (noise) → **C81 .047** → pin 6. **R234 47K** at pin 5.
- Feedback **R235 470K** ∥ **C83 .001** ∥ (**VR10 100K(A)** + **C82 33/6.3**).
- **R238 1K** out; mix R2 33K.
- COMPUTED:
  - C81 into the two 100k inputs gives a ≈ **68 Hz** high-pass.
  - The 1 nF feedback capacitor gives a ≈ 1.9 kHz low-pass at full level.

**Comparison of the three tom/conga circuits.**

| | LT/LC | MT/MC | HT/HC |
|---|---|---|---|
| Gate, NPN collector R (`.param`) | Q50/Q51, R213 | Q53/Q54, R242 | Q56/Q57, R268 |
| C1 (pin 6 → m) | C76 .056 | C90 .027, + C89 .0082 in MT | C103 .027 |
| C2 (m → out) | C78 .012, + C77 .047 in LT | C92 .012, + C91 .027 in MT | C105 .0056, + C104 .022 in HT |
| Fig. 11, diodes off (1.5k / 1.25k / 1.0k), COMPUTED | tom 79 / 86 / 97 Hz; conga 175 / 192 / 214 Hz | tom 122 / 134 / 150 Hz; conga 252 / 276 / 309 Hz | tom 166 / 182 / 204 Hz; conga 369 / 404 / 452 Hz |
| SN p.14 (low / mid / high) | LT 80/90/100, LC 165/185/220 | MT 120/135/160, MC 250/280/310 | HT 165/185/220, HC 370/400/455 |

**Controls.** TUNING VR11 500Ω(B), LEVEL VR10 100K(A), SW8 (`conga`: 1 = LC).

**Params.** `r213` 10k, `c76 c78 c77`, `r230`, `p11` 500 (VR11, TUNING). The fitted unit trims each of these by a few per cent (`docs/model.md`, "Calibration").

**SN p.14:** LT 3.5 / 12 Vpp, decay 200 ms; LC 3.5 / 12 Vpp, decay 180 ms.

---

## 7. Mid tom / mid conga, MT/MC (mt.cir; SN p.9 cols 15–27, rows H–N)

As §6, with these parts:

| Block | Parts |
|---|---|
| Gate | Q53 (R240 22K / R241 10K), **R242 10K** "(22k for 1~5 lot)", Q54 (R243 4.7K); R251 4.7K load |
| Input | C86 .0027, D58, R255 22K; R254 6.8K at pin 5; SW9 at MC adds R253 2.2K |
| Resonator | IC17a: C90 .027, C92 .012, R257 820K; IC17b (R258, R256 33K) → **R259 820K** |
| MT switch | SW9 puts **C91 .027** across C92 *and* **C89 .0082** across C90 (C1 and C2 both change) |
| Leg | R260 1K, VR13 500Ω(B), D82/D83 1S188FM |
| Noise | R244 4.7K, D57, C85 .033, R247 2.2M, C84 .047, R245 15K / R246 3.3K, R248 33K, Q55 (R252 4.7K from B5), C87 .033, R249 22K, R250 22K, C88 .015 |
| Output | R262 / R261 100K → C96 .039 → IC16a (R263 47K); R266 470K ∥ C97 .001 ∥ (VR12 100K(A) + C95 33/6.3); R239 1K; mix R1 33K |

- **Switch wiring.** SN draws C89 from the switch to pin 6 and the MT contact to the centre m. NET has the switch on the pin 6 side (`S9C v c89 !conga`, `C89 c89 m`). A switch in series with a capacitor is the same circuit either way round.
- **Output high-pass.** C96 gives ≈ 82 Hz (COMPUTED).
- **Params.** `r242`, `c90 c92 c91 c89`, `r259`, `p13` 500 (VR13, TUNING).

**SN p.14:** MT 3 / 11 Vpp, decay 130 ms; MC 3 / 10 Vpp, decay 100 ms.

---

## 8. Hi tom / hi conga, HT/HC (ht.cir; SN p.9 cols 15–27, rows O–T)

As §6, with these parts:

| Block | Parts |
|---|---|
| Gate | Q56 (R267 22K / R271 10K), **R268 10K** "(22k for 1~5 lot)", Q57 (R269 4.7K); R270 4.7K load |
| Input | C99 .0027, D60, R282 22K; R281 5.6K at pin 5; SW10 at HC adds R280 1.5K |
| Resonator | IC18a: C103 .027, C105 .0056, R284 820K; HT adds **C104 .022** across C105; IC18b (R285, R283 33K) → **R286 820K** |
| Leg | R287 1K, VR15 500Ω(B), D84/D85 1S188FM |
| Noise | R274 4.7K, D59, C100 .033, R276 2.2M, C98 .047, R273 15K / R272 3.3K, R275 33K, Q58 (R279 4.7K from B6), C101 .027, R277 22K, R278 22K, C102 .01 |
| Output | R289 / R288 100K → C108 .033 → IC16b (R290 47K); R291 470K ∥ C110 .001 ∥ (VR14 100K(A) + C109 33/6.3); R294 1K; mix R3 33K |

- **Output high-pass.** C108 gives ≈ 96 Hz (COMPUTED).
- **The HT row.** DIG suspected a copy error because SN's HT row equals its LC row. The circuit puts HT (182 Hz) close to LC (192 Hz) anyway (§6 table).
- **Params.** `r268`, `c103 c105 c104`, `r286`, `p15` 500 (VR15, TUNING).

**SN p.14:** HT 3.5 / 12 Vpp, decay 100 ms; HC 3.5 / 12 Vpp, decay 80 ms.

---

## 9. Rim shot and claves, RS/CL (rs.cir; SN p.9 cols 28–38, rows L–T; theory p.6)

**Gate.** Q59/Q60 (§2.5), giving t, with **R300 22K** to ground.

**IC21 resonator (the RS low tone, "GEN1").**

- t → **C111 .01** (**R307 100K** to ground) → **D62** → **R301 47K** → pin 3 (**R302 10K** to ground).
- Bridged T: **C116 .0047**, **C115 .0047**, **R315 5.6K** centre, **R316 1M** bridge.
- Fig. 11: **452.5 Hz** (SN "L 2.2 (455)").
- Output → **R318 220K** → the VCA node, with **R317 10K** to ground.

**IC20a resonator ("GEN2").**

- t → **C114 .001** (**R306 22K**) → **D64** → **R310 33K** → pin 3 (**R311 10K**).
- Bridged T: **C119 .0022**, **C117 .0022**, **R312 1K** centre, **R308 820K** bridge.
- IC20b inverts it (**R314 33K**, **R309 33K**).

**SW11 picks the voice.**

- **RS:** **C118 .0022** goes across C117. Fig. 11 gives **1787 Hz** (SN "H 0.6 (1667)").
  - IC20b's output joins IC21's at the VCA node through **R320 22K**.
- **CL:** **R313 390K** feeds IC20b's output back into IC20a's centre, and the output goes straight to IC19a through **R319 220K**.
  - Fig. 11 without C118 gives **2526 Hz** (SN "0.4 (2500)").
  - SN: "Disconnected R313 makes IC20b just as a buffer for IC20a output" (in RS).

**RS swing VCA and high-pass.**

- Envelope: t → **D61** → **R107 1K** → **C24 .47/50** → **R108 10K** → **D63** → Q62's collector. SN: "The envelope applied to Q62 is formed by R107 and C24".
- **Q62**: emitter grounded; base biased by **R321 2.2M** from B7 and driven through **C120 10/16** from the VCA node.
- Its collector → **C121 .0047** → **C122 .0047** → **Q63** (**R325 330K** base to ground, **R322 10K** bootstrapped from the emitter, **R323 22K** to −B7).
  - COMPUTED: a high-pass at **589 Hz**, Q 2.9.
- Q63's emitter → **R324** → SW11 (RS).
  - **R324 has no value printed** (read: blank on SN and SN-SF). It alone sets the rim shot's level. The Yocto clone's 220k leaves RS at 2 Vpp against the p.14 table's 3, and about 5 dB under Fischer's unit relative to his other voices. NET uses **150k** (meets the table with the printed parts); the fitted unit 120k. UNCERTAIN.

**Output, IC19a.**

- SW11 → **C125 .01** → **R326 10K** → pin 2 (**R331 47K** at pin 3).
- Feedback **R330 470K** ∥ **C153 22P** ∥ (**C126 33/6.3** + **VR16 100K(A)**).
- **R295 1K** out; mix R6 33K.

**Output mute: Q74 2SK30A(Y).**

- Q74's channel is across VR16.
- Its gate sits at **R305 1M** to ground with **C113 .047**, and **R327 470K** to the output. So Q74 normally conducts and shorts the LEVEL leg, muting IC19a.
- A trigger kicks **Q61** on (base through **C112 .022** from t, **R304 47K** to −B7, emitter −B7). Q61 pulls the gate to −15 V, and C113 lets it back up (τ = R305·C113 = 47 ms).
- SN: "Normally-conducting Q74 remains off only while trigger pulse is transferred … to eliminate noise leaking from IC20, especially for CL".

**Claves R313.** The default `Calibration::claveR313` is the printed **390k**. VB says "the schematic's value can't decay" in this model: with 390k the claves loop does not die away (`docs/model.md`). The fitted unit uses **500k**, with C117/C119 2.318/2.213 nF, C113 36 nF, and R318 165k (the 455 Hz tone against the 1.7 kHz). The default keeps the drawing.

**Params.** `r298` 10k, `r324` 150k, `r313` 390k, `c117 c119` .0022, `c113` .047, `r305` 1M, `r318` 220k, `r320` 22k, `c118` .0022 (the fitted unit's: `docs/model.md`, "Calibration").

**SN p.14:** RS 3 / 10 Vpp, decay 10 ms; C 2.5 / 8 Vpp, decay 25 ms.

---

## 10. Hand clap, CP (cp.cir; SN p.9 cols 2–14, rows H–T; theory p.6, Fig. 13; crop `sn-p9-cp-offset-tm3-vca.png`)

**Gate.** Q28/Q29 (§2.5) → SW12 at CP → node tcp. NET joins it with `R12SW` 0.1 Ω. The gated trig also leaves as C.P. TRIG.

**Noise band-pass, IC21b (multiple feedback).**

- White noise → **R332 27K** ("22k for 1~5 lot") in parallel with **C127 .0018** → node n21, with **R333 10K** to ground.
- **C129 .0047** n21 → output; **C128 .0047** n21 → pin 6; **R334 100K** pin 6 → output; pin 5 grounded.
- COMPUTED (ignoring C127): f₀ = **1.25 kHz**, Q 1.9. The NET comment says "about 1 kHz". IC21b is on B7.

**"Reverb" VCA, Q70.**

- Q69 (base tcp, collector B8) follows the trigger. Its emitter feeds **D69** → **C139 .018** → **R348 1M** → **C138 .047** → **R349 1M** → Q70's base: a slow, smoothed envelope.
- The band-passed noise reaches the same base through **R342 15K** / **R346 680** ("1K for 1~5 lot") and **C137 .033**.
- **Q70**: emitter **R369 330** to ground; collector **R363 10K** from B8, with **C146 .018** to ground. Collector → **C152 .0047** → **R376 47K** → IC19b's input.
- "PORTION CHANGED": "10k(called R200) is connected across −15V line and Q70 emitter". NET `R200CP e70 nb8 {r200cp}` is 1 TΩ (absent) by default; `Calibration::lateClap` (true) sets it to 10k.

**Sawtooth envelope: IC23 AN6912, on −15 V only (pin 3 ground, pin 12 −B8).**

- Comparator C (pins 8−, 9+, out 14): pin 8 ← tcp through **R360 27K**, with **R361 22K** to −B8. A ladder 0 V → **R353 4.7K** → na → **R352 4.7K** → nb → **R351 4.7K** → −15 V sets na = −5 V (for comparators A and B) and nb = −10 V (pin 9).
- Its open-collector output o14 is timed by **R350 1M** to ground and **C140** to ground. SN: the trigger is "integrated by R350 and C140, and converted into pulses of 30ms wide".
- **C140 is printed ".0027"** (read on SN and SN-SF). R350 × 2.7 nF = 2.7 ms. NET uses **0.027 µF** (`.param c140`), "the Yocto clone's value", which "gives the 30 ms pulse the theory describes" (27 ms).
- Comparator B (7+ = o14, 6− = na, out 1 = o1) and comparator A (11+ = na, 10− = o1, out 13 = o13): o13 has **R365 82K** to ground and charges **C144 .47/50** through **D71**.
- Comparator D (5+ = n5 via **R356 1M** from C144, with **C142 220P** to its output; 4− = n4, the **R355 2.7K / R354 5.6K** divider from o1; out 2 = o2 with **R359 10K** to ground).
- **Q73**: base from o2 through **R358 68K**, and from o1 through **C141 .001** (**D70** from −B8) and **R357 68K**; emitter −B8. Its collector, through **R367 330**, slams C144 to −15 V each time.
- SN: C144 then discharges "through R365 and D71" and the comparators re-fire "repeated and advanced to the middle of the third time" (Fig. 13).

**Exponential converter, Q72/Q71 (2SA733 pair).**

- The emitters share **R366 15K** from B8.
- Q72's base ← the envelope (Q73's collector) through **R368 470K**, with **R371 15K** to ground. Q72's collector → **R374 10K** → IC22 pin 1 (I_ABC).
- Q71's collector goes to −B8. Its base has **R364 2.2K** to ground and takes:
  - the offset from **TM3 10K(B) "C.P. OFFSET"** (ends at ground and −B8) through **R370 47K**;
  - the accent memory: Q69's emitter → **D68** → **C143 1/50** → **R362 330K**.
- SN: "converted exponentially by Q72 together with the signals applied to the base of Q71 (offset adj. signal from TM3 and accent signal via D68, C143 and R362)".

**VCA: IC22 BA662(A).**

- Noise from IC21b → **C136 .022** → **R372 39K** → pin 2 (−IN), with **R382 1K** to ground. Pin 3 (+IN) through **R381 1K** to ground.
- Pin 1 ← R374; pin 9 B8; pin 5 to the negative rail; pin 4 (the buffer's bias input) grounded.
- Pin 6 → 7 with **R383 82K** to ground. The buffer's pin 8 → **C150 .001** → **R378 100K** → IC19b's input.

**Output, IC19b (shared with MA).**

- The sum at pin 6 (**R373 47K** at pin 5). NET joins R376/R378 to it with `R390` 0.1 Ω.
- Feedback **R375 470K** ∥ **C149 220P** ∥ (**VR17 100K(A)** + **C145 33/6.3**).
- **R296 1K** out, with **TP5** on that line (after R296: the clap's jack signal); mix R7 33K.

**TM3, the service trim.** SN p.14: "CP (HAND CLAP) OFFSET … write, play CP at a tempo w/ LEVEL FCW … TM-3" to a scope picture marked "6 Vpp" over the bursts and "0.5 Vpp" for a band at the start (crop `sn-p14-cp-offset-waveform.png`).

- NET control `offset` (`P3 0 tm3w nb8 10k offset`) is set from `Calibration::cpOffset`.
- The nominal value is 0.5. The fitted value is **0.37**, where three real units sit (their tails' spectrum and length and their accents: `docs/model.md`, "Unit spread"); Fischer's unit is 0.26 ("the clap's bursts ~25 dB over the gaps between them", the notes' 6 / 0.5 Vpp).

**Params.** `r106` 10k, `r346` 680, `r332` 27k (the late values of SN p.15 #2, serial 010600: "CP sound overmatches the rest in level"), `c140` 0.027u, `r200cp`, `cmpvg` 0.5 mV (§16); `c128`, `c129` 4.7n, `c138` 47n and `c144` .47 as printed (the fitted unit's are in `docs/model.md`, "Calibration").

**SN p.14:** CP "6 / 2" Vpp (the accent figure is suspect, DIG), decay 100 ms.

---

## 11. Maracas, MA (ma.cir; SN p.9 cols 2–14, rows P–T; theory p.6)

SW12 at MA sends the CP gate's output (tma) here instead.

**Window (Q67/Q66).**

- tma → **D67** → **C135 .1** with **R345 150K** to ground → **R344 220K** → **Q67**. Q67's collector has **R343 100K** from B8.
- Then **R341 470K** in parallel with **D66** (anode at Q67's collector) → **C134 .033** → **Q66**'s base. Q66 is a PNP with its collector grounded and its emitter through **R337 10K** to node x.
- The two RC pairs (C135·R345 = 15 ms; C134 with R341 or D66) open and close a window. SN: "Envelope for MA sound generator is generated by Q66 and Q67."

**Noise gate (Q65).**

- White noise → **R335 6.8K** → **C130 .047** → **Q65**'s base (PNP, emitter B8, **R336 1M** base to ground).
- Q65's collector → **D65** → x. While Q66 draws current from x, Q65's noise-modulated current flows. SN: "White noise is gated by Q65".

**Filter and output.**

- x → **C131 .01** (**R338 15K** to ground) → **C132 .001** → **C133 .001** → **Q68** (**R340 68K** base to ground, **R339 3.3K** bootstrapped, **R347 22K** to −B8).
- COMPUTED: 1.06 kHz first order, then a **10.6 kHz** Sallen-Key high-pass (Q 2.3).
- → **C151 .01** → **R377 100K** → IC19b, which is the CP output stage and its LEVEL (VR17).

**SN p.14:** M 3 / 5 Vpp, decay 25 (short) / 35 (long) ms.

---

## 12. Cowbell, CB (cb.cir; SN p.13 voicing board, left and centre; theory p.6)

**Gate.** Q1/Q5 on the narrowed trig (§2.4–2.5).

**Envelope.**

- Q5's collector → **R122 1K** → **D2** → **C9 .47/50** (node env).
- **R82 33K** + **C34 1/50** in series across C9. SN: "A series of R82 and C34 connected in parallel with C9 forms an envelope having abrupt level decay at the initial trailing edge to emphasize attack effect."
- COMPUTED: C9 shares its charge with C34 with τ = R82·(C9 in series with C34) ≈ 10.5 ms; then both drain through the two VCA paths.

**Two swing VCAs.**

- o1 → **R62 33K** / **R63 2.2K** → **C32 10/16** → **Q15**'s base (**R30 470K** bias from B1; emitter **R31 100**).
- o2 → **R64 33K** / **R65 2.2K** → **C33 10/16** → **Q14** (**R32 470K**, **R33 100**).
- The envelope reaches each collector through **R28 100K** → **D3** and **R29 100K** → **D4**.
- COMPUTED: the dividers cut the 4.7 V squares to ≈ 0.29 V p-p at each base.

**The 10 µF couplings matter.**

- C32/C33 charge to where the squares' rectified base drive leaves them. That is "a tenth of a volt off the operating point of the squares' first instant", and until they get there "the VCAs hardly chop and the cowbell is 12 dB down" (VB, `reset()`).
- VB therefore runs CB, CY and OH with the squares for up to 2 s at reset, until they sleep, settled.

**Band-pass, IC2b (bridged T).**

- Each VCA node feeds the centre p through **R26 10K + C30 .0022** and **R27 10K + C31 .0022**.
- **R24 2.2K** p → ground; **C28 .01** p → pin 6; **C29 .0033** p → output; **R25 470K** pin 6 → output.
- Fig. 11 with R1 = R24 alone: **862 Hz**, Q 6.3 (COMPUTED; the input networks load R1 somewhat).

**LEVEL and buffer.**

- **C75 1/50** → **R116 8.2K** → **VR5 5K(A)** (a divider: pin 3 top, pin 1 ground) → wiper → **C76 .022** → IC6a pin 3 (**R117 22K** to ground).
- IC6a is a follower; **R163 1K** out; mix R8 47K.
- Unlike most voices, LEVEL here is a divider before a unity buffer.

**Params.** `c9` .47u, `c34` 1u, `r82` 33k, `r28 r29` 100k. The fitted unit uses C34 1.13 µF, with C9 and R82 as printed (`docs/model.md`, "Calibration").

**SN p.14:** 3.5 / 12 Vpp, "H 1.25 (800) / L 1.85 (540)", decay 50 ms.

---

## 13. Cymbal, CY (cy.cir; SN p.13 voicing board centre and bottom; theory p.6; W-CY)

**Gate.** Q2/Q6 on ntrig, with **R83 22K** load.

**Attack smoother, Q19.**

- **R84 10K** → Q19's base. Its collector is fed through **R85 100** from **C35 47/16** / **R86 100** off B1.
- Q19's emitter e0 charges three envelopes through **D6**, **D7** and **D8**.

**Envelope 1, high band.**

- D6 → node a1: **C38 1/50**; **R87 22K** + **C37 2.2/50** in series to ground; **R94 39K** to b1n (**C47 .0015**).
- b1n → **D5** → Q16's collector. Short.

**Envelope 2, middle band.**

- D7 → node a2: **C40 1/50**; **R88 33K** → **C39 .47/50**; **R90 33K** → b2n (**C50 .0015**) → **D11** → Q17's collector.
- b2n is also pulled by Q20 through **R91 33K** to node j, which sits on **R92 33K** from Q20's emitter and **R89 10K** to ground. SN: Q17's "decay time is controllable".

**Envelope 3, low band and DECAY.**

- D8 → **C41 1/50**, discharged by **R93 470K** + **VR2 2M(B)** (a rheostat: wiper tied to pin 3, pin 1 ground).
- COMPUTED: τ = 0.47–2.47 s.
- **Q20** (collector B1) follows it → **R105 33K** → **C45 2.2/50** → **R104 22K** → l0 (**C48** side) → **D12** → Q18's collector.

**Swing VCAs.**

- **Q16**: hi band through **C42 .022**; bias **R96 1M + R97 1M** from B1; emitter **R95 100**; collector **C36 .0015**.
- **Q17**: hi band through **C44 .022**; bias R100 + R99 1M; emitter R98 100; collector **C43 .0015**.
- **Q18**: lo band through **C46 .022**; bias R102 + R103 1M; emitter R101 100.

**High-passes.**

- **High band:** b1n → **C49 .0033** (**R106 6.8K** to ground) → **IC5a**, a Sallen-Key high-pass (C53, C54 .001; **R109 18K** to ground at pin 3; **R108 18K** fed back from the output; gain 1 + **R110 39K**/**R111 22K** = 2.77).
  - COMPUTED: 7.1 kHz first order, then 8.8 kHz second order.
- **Middle band:** b2n → **C51 .001** → **IC5b** (C52 .001, **R113 18K**, **R107 18K**; gain 1 + **R114 39K**/**R115 39K** = 2).
- **Low band:** l0 → **C48 .0015** → **C59 .0015** → **Q25** (**R127 82K** base to ground; **R124 22K** bootstrapped from the emitter; **R126 22K** to −B2, see §18).
  - COMPUTED: a 2.5 kHz high-pass.
  - Then **C58 .01** → **R123 100K** → **C57 .0033** to ground (a 482 Hz low-pass) → **R121 10K**.

**TONE.**

- IC5a → **C55 .01** → **R112 22K** → node t1.
- IC5b → **C56 .01** → **R120 10K** → node t2. R121 also lands on t2.
- **VR4 20K(B)**: pin 1 at t1, pin 3 through **R125 2.2K** to t2, wiper to ground. It shunts one side or the other.
- **R119 22K** (t1) and **R129 15K** (t2) → **C90 .01** → IC6b.
- SN: "These three signals … are outputted with their level ratio controlled by VR4". NET: `P4 r125 0 t1 20k tone`.

**Output, IC6b.**

- Pin 5 through **R118 47K**.
- Feedback **R128 470K** ∥ **C77 220P** ∥ (**VR6 100K(A)** + **C78 33/6.3**).
- **R166 1K** out; mix R9 47K.
- C90 in series with the 15k/22k inputs makes the stage a differentiator below about 1 kHz (COMPUTED: 1/(2π·C90·R) = 0.72–1.06 kHz). Werner: the level buffer "also acts as a differentiator in the audio band" (W-CY §11).

**Controls.** DECAY VR2 2M(B), TONE VR4 20K(B), LEVEL VR6 100K(A).

**Params.** `p2` 2M, `c41` 1u, `r93` 470k, `c45` 2.2u, `r125` 2.2k, `c37` 2.2u, `c39` .47u, `c38 c40` 1u, `r87` 22k, `r88` 33k. The fitted unit: VR2 1.566M, C41 1.048 µF, R125 3551 (`docs/model.md`).

**SN p.14:** 3.5 / 7 Vpp, decay 350 / 800 / 1200 ms.

---

## 14. Open and closed hi-hat, OH/CH (hats.cir; SN p.13 voicing board right; theory p.6)

One netlist holds both voices, because the CH envelope chokes the OH.

**Gates.**

- OH: Q3/Q7. Q7's collector feeds **R131 22K / R130 10K** to **Q21**'s base and **R135 10K** to **Q24**'s base.
- CH: Q4/Q8. Q8's collector has **R13 22K** load and feeds **R141 33K** to **Q28**'s base.

**OH envelope.**

- **Q24** (collector through **R137 470** from the local rail s3 = **R136 100** off B3 with **C61 47/16**) charges **C62 .47/50** (node eoh).
- C62 leaks through **R139 1M** (COMPUTED: τ 0.47 s).
- **Q29** buffers eoh: base through **R140 33K**, emitter **R143 33K** to ground. It drives the VCA through **R144 33K**.

**OH DECAY timer.**

- **Q21** empties **C60 10/16** (node k) during the trigger.
- C60 then charges from B3 through **VR3 2M(B)** (wiper tied to pin 3) + **R132 100K**.
- When k reaches a V_BE, **Q22** turns on through **R133 10K** and empties C62 through **R134 100K** (COMPUTED: τ = 47 ms).
- So OH DECAY sets *when* the fast fall starts. COMPUTED: C60 reaches ≈ 0.6 V after C60·(R132 + VR3)·ln(15/14.4) ≈ 0.04·C60·(R132 + VR3), i.e. 41 ms / 0.45 s / 0.86 s at VR3 = 0 / mid / full. SN gives OH decays of 90 / 450 / 600 ms.

**The choke.**

- A CH trigger charges **C63 .47/50** (node ech) through **Q28**, whose collector is fed through **R138 470** from s3.
- ech drives **Q23** through **R173 330K**. Q23 sits in parallel with Q22, so the OH envelope falls through R134 at once.
- SN: "When the CLOSED HI-HAT (CH) is triggered while the OH circuit is activated, Q23 turns on by the voltage applied through R173."
- DIG lists the shape of the choke as an open question. By the drawing it is the same R134·C62 = 47 ms fall the timer uses, not an instant cut. COMPUTED; NET reproduces it as drawn.

**CH envelope.** ech → **R142 33K** → the CH VCA.

**VCAs and filters (hi band from IC3b).**

| | OH | CH |
|---|---|---|
| VCA | **Q27**: C67 .022; R148 + R149 1M from B3; emitter R145 100; collector C65 .0015; envelope D13 (anode node noh, C64 .0015) | **Q30**: C71 .022; R151 + R150 1M; R152 100; C70 .0015; D14 (node nch, C69 .0015) |
| High-pass | C66, C68 .0015; R147 2.7K bootstrapped; R146 68K; **Q26**, emitter R156 22K to −B3. COMPUTED: **7.8 kHz**, Q 2.5 | C72, C73 .001; R153 2.7K; R155 68K; **Q31**, R154 22K to −B3. COMPUTED: **11.7 kHz**, Q 2.5 |
| Output | C80 470P + R160 39K → IC7a (R158 47K at +); R159 470K ∥ C79 220P ∥ (VR7 100K(A) + C81 33/6.3); R169 1K; mix R10 56K | C74 470P + R162 33K → IC7b (R157 47K); R161 470K ∥ C82 220P ∥ (VR8 100K(A) + C83 33/6.3); R172 1K; mix R11 47K |

**Controls.** OH DECAY VR3 2M(B), OH LEVEL VR7, CH LEVEL VR8 (100K A).

**Params.** `p3` 2M, `r132` 100k, `c60` 10u, `c62` .47u, `r139` 1M. The fitted unit's timer: VR3 1.948M, R132 119.5k, C60 7.355 µF (`docs/model.md`).

**SN p.14:** OH 3.5 / 7 Vpp, decay 90 / 450 / 600 ms; CH 3 / 6 Vpp, decay 50 ms.

---

## 15. Mixer and master output (mix.cir; SN p.13 voicing board bottom right and JACK-B OP3116-110)

**MULTI OUT jacks and mix resistors (Jack board B).** Each voice's output resistor (1K) feeds its MULTI OUT jack. The jack's normal (switching) contact feeds the mix bus through one resistor, so plugging a jack takes that voice out of the mix. The bus goes over a shielded wire (pin 13) to the voicing board.

| Voice | Out R | Jack | Mix R | IC4a gain (−33K/R), COMPUTED |
|---|---|---|---|---|
| BD | R175 | J1 | **R5 22K** | −1.50 |
| SD | R207 | J2 | **R4 33K** | −1.00 |
| LT/LC | R238 | J3 | **R2 33K** | −1.00 |
| MT/MC | R239 | J4 | **R1 33K** | −1.00 |
| HT/HC | R294 | J5 | **R3 33K** | −1.00 |
| RS/CL | R295 | J6 | **R6 33K** | −1.00 |
| CP/MA | R296 | J7 | **R7 33K** | −1.00 |
| OH | R169 | J10 | **R10 56K** | −0.59 |
| CH | R172 | J11 | **R11 47K** | −0.70 |
| CB | R163 | J8 | **R8 47K** | −0.70 |
| CY | R166 | J9 | **R9 47K** | −0.70 |

Each voice netlist loads its own output with the same resistor (`RMIX`) to ground, standing in for IC4a's virtual ground.

**Step ticks (not on the schematic).** A real unit's mix out ticks at both edges of every step's common trig (`docs/model.md`, "Unit spread"). NET: `RTK1 1k`, `CTK1 29.6n` (a 30 µs low-pass), `CTK2 0.95n` with `RTK2 100k` (95 µs), and `CTK3`/`RTK3` (534 µs) from the trig bus (input `trig`) into the bus node `sum`: a network fitted to that unit's averaged tick, not a drawn path. `rtk3` 1e15 (off) by default; the fitted unit's 107 MΩ with 4.99 pF.

**IC4a.**

- The bus → **C17 1/50** → pin 2 (pin 3 grounded).
- Feedback **R75 33K** ∥ **C18 220P** (COMPUTED: a 22 kHz pole).

**MASTER VOLUME.**

- **C19 1/50** → **VR1 50K(A)** (pin 3 top, pin 1 ground) → wiper → **C23 1/50** → **R77 22K** → IC4b pin 6.
- NET: `P1 vr1 vw 0 50k volume a0.15`.

**IC4b.**

- Feedback **R76 33K** ∥ **C22 220P** ∥ (**R74 330K** + **C21 33/6.3**).
- COMPUTED gain: −30k/22k = −1.36 in the audio band (R74 in parallel), −1.5 at DC.
- **D9/D10** clamp the output to ±B5.

**HI and LO outputs (Jack board B).**

- **C1 10/25 BP** → **R14 1K** → J12 HI.
- From C1, **R13 27K** / **R12 3.3K** to ground → J13 LO (×0.109).
- SN p.1: "Hi 6V p-p/1KΩ; Lo 0.6V p-p/3KΩ (Level: Voices @ red mark; AC @ FCW)".
- NET loads HI with a nominal 100k (`RLOAD`), not a schematic part. VB renders the HI output.

**Power on/off muting.**

- **Q13 2SK30A(Y)**'s channel is across R74. Its gate comes from the power on/off detector Q10–Q12 (through R73 10K, C20 .001).
- SN p.5 gives the detector's point A as "Power on: 0V 1-2sec -15V / Power off: -15V to 0V". While the gate is at 0 V, Q13 conducts, C21 33 µF sits directly across IC4b's feedback, and the audio is shorted.
- In steady state the gate is at −15 V and Q13 is off.
- NET: `J13 n4b q13g c21 2SK30AY`, its gate through `R73` from the input `mute` (point A), with `C20` taken as the gate's filter to ground (UNCERTAIN: the drawing's C20 placement wasn't checked). VB holds `mute` at −15 V and drops it to 0 V for 1.5 s when POWER is switched on (`VoiceBoard::coldStart`).
- The same detector also drives main-board Q64/Q75 "(5 lot~)".

---

## 16. Semiconductors

`core/netlists/models.cir`. The solver has no charge storage, so the models are DC only (SPICE's Gummel-Poon equations; see core/circuit.h).

**Types (SN p.8 note).** "UNLESS OTHERWISE SPECIFIED ALL NPN TRANSISTORS ARE 2SC945(P), ALL PNP TRANSISTORS ARE 2SA733(P), Q23~Q26 2SA1015(GR), ALL DIODES ARE 1S2473, ALL OP AMP'S ARE µPC4558C".

- The Q23–Q26 exception is on the **main board**: the scanning drivers on p.8. The voicing board's Q23, Q25 and Q26 are drawn as NPNs (2SC945) and NET uses 2SC945 for them.
- The parts list (p.16) also allows 2SA733 (P) or (Q), 2SC945 (P) or (Q), 2SC2021 (R)/(Q)/(S), and 2SK30ATM (Y) and (GR).

| Model | Parameters | Where used | Source and reasoning |
|---|---|---|---|
| **2SC945** NPN | IS 1.1e-14, BF 300, BR 6, VAF 110, ISE 1e-14, NE 1.5 | All NPNs (gate NPNs, Q31–Q34, Q41–Q44, Q47–Q49, Q52, Q55, Q58, Q61–Q63, Q67–Q70, Q73; and on the voicing board Q1–Q4 and Q14–Q31; Q9 is not netlisted) | NEC data sheet (DS `nec-2sc945-1981`): rank P h_FE 200–400, typ 200 at 1 mA; V_BE 0.62 V typ (0.55–0.65) at 1 mA; h_FE(0.1 mA)/h_FE(1 mA) 0.92; f_T 250 MHz. BF 300 is mid-rank P. The model's V_BE at 1 mA is 0.65 V (COMPUTED). ISE sets the gain's fall at small currents: the sheet's curve (DS `nec-2sc945`, p.2) has h_FE at 10 µA 0.85 of its value at 1 mA; ISE 1e-14 gives 0.83 (and 0.94 at 0.1 mA), where the 2e-14 used before gave 0.72 (and 0.5 at 1 µA). It matters where an envelope drains only through a base: the clap's reverb tail (Q70) decayed 0.36 octave fast with 2e-14, 0.18 with 1e-14. |
| **2SA733** PNP | IS 1.2e-14, BF 280, BR 6, VAF 90, ISE 2e-14, NE 1.5 | All PNPs (the gate PNPs Q29, Q40, Q46, Q51, Q54, Q57, Q60 and Q5–Q8; Q65, Q66, Q71, Q72) | NEC sheet: rank P 200–400, f_T 180 MHz; its h_FE curve (p.3) has 0.89 at 0.1 mA of the value at 1 mA, as the model (0.90) |
| **2SA1015** PNP | IS 1.0e-14, BF 300, … | **Unused** in the voice netlists (the main-board Q23–Q26 are not part of the voicing) | Toshiba sheet: GR 200–400 |
| **1S2473** Si diode | IS 2.52n, N 1.752, RS 0.568 | All signal diodes | Rohm (not Hitachi) data sheet, `rohm-1s2473.pdf`: fitted to its V_F–I_F curve (Fig. 1, 0.2–100 mA). COMPUTED from the model: 0.58 V at 1 mA, 0.85 V at 100 mA. The sheet's table gives 0.92 V typ (1.2 V max) at 100 mA, so the model is ≈ 70 mV low at the top of the curve, far above the few-mA currents in the voices. 1.5 pF, t_rr 1.5 ns (not modelled). |
| **1S188FM** Ge diode | IS 2e-7, N 1.3, RS 10 | D80–D85, the tom/conga tuning legs | Parts list: "1S188FM Ge diode", a Sanyo germanium point-contact part. **No data sheet exists here** (only a radiomuseum listing: 35 V / 50 mA). The parameters are a **guess** at a germanium point-contact junction: COMPUTED 0.21 V at 0.1 mA, 0.29 V at 1 mA. Werner modelled them as silicon "for lack of data" (DIG). UNCERTAIN; it sets how far the pitch glides. |
| **2SK30AY** N-JFET | VTO −0.9, BETA 2.5e-3, LAMBDA 0.01 | Q74 (RS mute) | Toshiba 2SK30ATM: Y grade I_DSS 1.20–3.00 mA. The model gives I_DSS = BETA·VTO² = 2.0 mA (COMPUTED). |
| **2SK30AGR** N-JFET | VTO −1.5, BETA 2.2e-3 | **Unused** (Q64/Q75 are CPU/RAM protection on p.8) | GR grade 2.60–6.50 mA; the model gives 4.95 mA |
| **µPC4558C** op-amp | `X … opamp {gbw} [1e5 {opsat} {sr}]`: single pole, **GBW 3 MHz**, gain 1e5 where given, soft clip at **±13.5 V** (`opsat`), slew **1 V/µs** (`sr`) | Every op-amp: IC2–IC7 (voicing), IC12–IC21, IC24 (main) | models.cir: "NEC data sheet: typ. 3 MHz unity gain"; clipping "uPC4558C at ±15 V: ±13 V typical into 10k". DS Renesas: V_om ±14 typ (±12 min) into ≥10 kΩ, ±13 into 2 kΩ; slew rate 1.0 V/µs (modelled where `opsat` is given: `docs/model.md`, "The solver"). DIG read the 1981 NEC curve as about 1.5 MHz GBW (UNCERTAIN, between 1.5 and 3 MHz). Op-amps given `opsat` clip: the resonators (IC12, IC14, IC15, IC17, IC18, IC20, IC21a), IC2a and the mixer IC4. The others (output stages IC13, IC16, IC19, IC6, IC7; filters IC2b, IC3, IC5, IC21b; IC24) never clip in the model. |
| **HD14584B** | — (not a netlist) | IC1, the six oscillators | Thresholds at 5 V (Motorola MC14584B, which matches Hitachi's): V_T+ 1.8 / 2.7 / 3.4 V, V_T− 1.6 / 2.1 / 3.2 V (min / typ / max). VB uses the typical frequencies and duty (§3.2), calibrated per unit. |
| **BA662(A)** OTA | `B22 pin1 nb8 ota_p ota_n ota6 ota rs=450`: current into pin 1 through a diode and 450 Ω to −15 V; output g·I_ABC·tanh(v_d/2V_T); buffer `E22` unity, ideal | IC22 (CP VCA) | No Rohm data sheet. A textbook OTA stand-in, "as the TB-303's" (`machines/tb303/docs/circuit.md` §6.1 has the chip's reverse engineering: OML, Alfa AS662, Rohm BA6110). rs = 450 Ω sits between Alfa's ≈ 400 Ω and OML's ≈ 500 Ω control-mirror emitter resistors. The 808 grounds pin 4, the buffer bias. |
| **AN6912** comparator | `B… comparator vg=0.5m vcm=-1.5 vos=-2m` (open collector to −15 V; isink 16 mA, vsat 0.15 V defaults) | IC23 (CP envelope), four sections | Matsushita/Panasonic sheets: common-mode input range 0 to V+ − 1.5 V. The 808 runs it with V+ = ground, so inputs above −1.5 V count as −1.5 V (`vcm`). Input offset 2 mV typ, 5 max (`vos` −2 mV); sink ≥ 6 mA; V_OL 0.2 V typ at 3 mA. `vg` is the input width of the output's transition. "IC23 (AN6912) is constructed with open collector NPN transistors for output and operates on single (negative) power only" (SN p.6). |
| **2SC828 "RNZ"** | — (input `nz`) | Q35, noise | "selected noise" part; modelled as white noise at its emitter (§3.1) |

---

## 17. Design changes by serial number (SN p.15, crops `sn-p15-*.png`; transcribed in IDX)

SN p.15 notes that some changes "were done at the factory on some products bearing serial number earlier than indicated: MAIN BOARD - modification 1, 4 / VOICING BOARD - modification 1". The drawings carry matching "N lot ~" notes. The serials read as lot numbers in digits 3–4 (000300 ↔ "3 lot ~", 010600 ↔ "1~5 lot"); that reading is an inference (IDX).

| # | Serial | Change | Voice-relevant? | Lemus |
|---|---|---|---|---|
| Main 1 | 000300 | INT clock C11 .068 → .047 + C203 .039 | No (CPU clock) | n/a here |
| Main 2 | 010600 | CP R346 1K → 680, R332 22K → 27K ("CP sound overmatches the rest in level") | Yes | **Late**: `r346` 680, `r332` 27k |
| Main 3 | 010600 | R91 15K → 1M (CPU pin 30, battery drain) | No | n/a |
| Main 4 | 010600 | DIN R25 220K → 1.5M; 0.01 to chassis | No | n/a |
| Main 5 | 010600 | Noise generator: R129 → short, R131 (printed "R311") → 100K, R127 4.7K → C200 10µ, C202 22p | Level only (TM4 trims either) | **Late** circuit in noise.cir |
| Main 6 | 010600 | Voice gate R106, R154, R182, R213, R242, R268, R298 22K → 10K | Yes, gate drive at low trig | **Late**: 10k |
| Main 7 | 010600 | IC6 R101 10K → 100K, C201 22p ("High frequency from CP generator induces irregular oscillation on other generators") | Coupling between voices | Not modelled: the 1 ms pulse is ideal |
| Main 8 | 020800 | Q64/Q75 FETs etc. (RAM protection) | No | n/a |
| Main 9 | 031100 | LED SEL2110R → TLR124 | No | n/a |
| Voicing 1 | 000300 | CB C6 .01 → .022, R44 390K → 150K, R45 330K → 100K ("Difficulty in setting COW BELL sound frequency") | Range of TM1/TM2 | Lemus sets the trimmed frequencies directly |
| Voicing 2 | 010500 / 051850 | Q1–Q4 2SC945P → 2SC2021R; Q5–Q8 2SA733P → 2SA937Q (package height) | Gate switches only | Kept as 2SC945/2SA733 |
| p.9 box | (none given) | "PORTION CHANGED": SD C58, C61, R191, R200; CP 10k "R200" | Yes | **Late**: `lateSnare`, `lateClap` true |

- **Lemus plays a late unit throughout.** The fitted unit's serial (103852) is later than every serial in the table.
- **Early CB values (DIG).** With R45 330k and C5 .018, oscillator 2 could not reach 800 Hz, so C5 must also have differed before 000300. SN does not say.
- **Other lot notes.** BD R177 82k "SURFACE MOUNTING" carries no lot note. R102 and C202 are also marked "SURFACE MOUNTING" (presumably added on the foil side).

### 17.1 Calibration: the nominal unit and the fitted one

**Two calibrations.**

- `struct Calibration`'s defaults are the service notes' nominal unit: the netlists' `.param` values plus the trims below.
- `unitCalibration()` is the unit Lemus plays: Michael Fischer's TR-808, serial 103852, fitted to his recordings at every knob mark (tools/compare/fit.py).

Method and results: `docs/model.md`, "Calibration".

| Item | Nominal (schematic / SN) | Fitted unit |
|---|---|---|
| `cbHz1` TM1 (o1) | 1000/1.85 = 540.5 Hz | 558.3 Hz |
| `cbHz2` TM2 (o2) | 1000/1.25 = 800 Hz | 823.6 Hz |
| `metalHz` o3–o6 (R43/C4, R42/C3, R41/C2, R40/C1) | 522.7, 304.4, 369.6, 205.3 Hz | 486.39, 287.82, 378.58, 198.83 Hz |
| `metalDuty` | 0.47 | 0.545 (the cowbell's even lines) |
| `noiseRms` TM4 | 130 mV rms at TP4 | 190 mV (the toms' noise tails) |
| `cpOffset` TM3 | 0.5 | 0.26 |
| `monoMs` | 1.0 ms | same |
| `lateSnare` / `lateClap` | true / true | true / true |
| `claveR313` / `rs.r313` | 390k | 500k |
| `bd.p6` VR6 | 500k | 585k |
| `sd.r194` | 2.2M | 4.7M |
| `lt.c76 / c78 / c77 / r230` | 56n / 12n / 47n / 820k | 53.41n / 11.52n / 47.39n / 839.2k |
| `mt.c90 / c92 / c91 / r259` | 27n / 12n / 27n / 820k | 25.88n / 11.99n / 27.54n / 852k |
| `ht.c103 / c105 / c104 / r286` | 27n / 5.6n / 22n / 820k | 25.22n / 5.741n / 22.24n / 854.7k |
| `rs.c117 / c119 / c113 / r318` | 2.2n / 2.2n / 47n / 220k | 2.318n / 2.213n / 36n / 130k |
| `cb.c9 / c34 / r82` | 0.47µ / 1µ / 33k | 0.6804µ / 1.832µ / 26.27k |
| `cy.p2 / c41 / r125` | 2M / 1µ / 2.2k | 1.566M / 1.048µ / 3551 |
| `oh.p3 / r132 / c60` | 2M / 100k / 10µ | 1.907M / 114.7k / 8.118µ |

- **Oscillators.** VB: "the four fixed ones 3-7 % off the typical thresholds' frequencies", read from the cymbal's and open hat's line spectra. TM1 and TM2 are "a little short".
- **Capacitors.** Every fitted capacitor is within the ±20 % that W-BD §11 gives for the voice circuits.

---

## 18. Discrepancies found

Netlist values checked against SN pp.8, 9 and 13 (400 dpi bitmaps), part by part for every voice.
Both findings below have been fixed in the netlists.

**1. Cymbal Q25's emitter resistor R126 (voicing board). Fixed.**

- **SN:** p.13 (voicing board, below "Q25", right of R127 82K) draws **R126 22K from Q25's emitter to "−B2"**. SN-SF `sf-1.png` shows the same: "R126 22K", arrow to "−B2". Every other emitter follower of this kind goes to the negative rail too: SD Q49 R203, RS Q63 R323, MA Q68 R347, OH Q26 R156, CH Q31 R154.
- **NET (before):** `R126 e25 0 22k`, to ground: Q25 sat cut off at rest (base and emitter both at 0 V) and passed only excursions a V_BE above ground, half-wave rectifying the cymbal's low band.
- **NET (now):** `VNB2 nb2 0 -15`, `R126 e25 nb2 22k`: Q25 idles at ≈ (15 − 0.6)/22k ≈ 0.65 mA, a linear follower (COMPUTED). The cymbal was refitted (`cy.p2`, `cy.c41`, `cy.r125`, §17.1); its log-spectral distance to Michael Fischer's unit went from 4.9 to 1.6 dB, and C41, which had fitted at +46 %, came back to 1.05 µF.

**2. Names of the cowbell's divider nodes (cb.cir), naming only. Fixed.** cb.cir called the R62/R63 and R64/R65 divider taps `tp6` and `tp7`. On SN p.13, **TP6 and TP7 are the oscillator outputs** (IC1 pins 12 and 10, the R44/TM1 and R45/TM2 lines), before the 33K/2.2K dividers. They are now `sq1` and `sq2`. No value differed.

**Netlist departures that the netlists themselves document** (confirmed against the drawing; not errors):

- **cp.cir C140:** printed ".0027" (SN p.9 and SN-SF). NET uses 0.027 µF so that R350·C140 gives the theory's "30ms".
- **rs.cir R324:** no value printed. NET uses 150k (the table's level; the Yocto clone's 220k is 3.5 dB quieter).
- **mtrig.cir:** IC2a clips at its own `ntsat` (12.8 V, a µPC4558 on the 14.1 V +B1), not `opsat` (§2.4).
- **rs.cir R313:** the printed 390k is the default, and the fitted unit uses 500k, because 390k "can't decay" in this model.
- **sd.cir / cp.cir:** the drawn values with the "PORTION CHANGED" values selectable (§5, §10).
- **models.cir header:** "Q23-Q26 2SA1015 (GR)" is the main board's exception. It is correctly not applied to the voicing board's Q23/Q25/Q26 (§16).
- **Not in any netlist:** the trig bus's extra loads (R57/R58, the trigger outs, gate base currents), the monostable, and the power on/off detector Q10–Q12 (§2.3, §2.2, §15).
