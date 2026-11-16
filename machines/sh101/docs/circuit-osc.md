# Roland SH-101 oscillator section: circuit transcription

This file transcribes the SH-101's pitch and source circuits from the service notes. It covers
the DAC sample-and-holds, portamento, the pitch summer, the bender board, the MODULATOR (LFO) and
its waveform switch, the CEM3340 VCO with pulse and PWM, the 4013 sub-oscillator, the noise
generator and the source-mixer sliders. It ends at the four mixing resistors and the keyboard CV
the VCF's KYBD slider sees. The filter section (VCF, ENV, VCA, output) is in
`docs/circuit-filter.md`. Part designators are Roland's. Bender-board parts are written "bender
R15" etc. where they could be confused with main-board parts.

The model built from it is described in `docs/model-osc.md`.

As in the TB-303's `circuit.md`:

- **Read** values come straight from a drawing, with a citation.
- **COMPUTED** values are derived here. The arithmetic is shown, or the model or SPICE gives the
  number.
- **UNCERTAIN** marks anything illegible, anything where sources conflict, and anything that rests
  on an inference about how the circuit works.

---

## 0. Sources and conventions

| Key | Document | Local file(s) |
|---|---|---|
| **SN** | Roland *SH-101 Service Notes, First Edition*, Nov. 1st 1982, archive.org scan. Page 9 has the main circuit diagram, page 7 the bender board. | `private/docs/sh101-service-notes*.pdf`, renders `private/docs/schematic/sn-p-{1..10}.png` |
| **SN400** | The same notes rescanned at 400 dpi (Din Sync's copy). This is the clearest scan; most readings below come from it. Its page 7 is the main diagram and page 5 the bender board. | `private/docs/schematic/alt-dinsync-400dpi/sn400-p-*.png`; block crops `crops/sn400-p7-*.png`, `crops/sn400-p5-*.png` |
| **OML** | Open Music Labs' errata for the SB01 project, with an annotated copy of the diagram. Gives the values Roland left blank (R24, R31, R35, …) and the wiring corrections. | `private/docs/papers/opensource/diy_openmusiclabs_sb01/` (`oml_wiki_Sh101_schematic_errors.txt`, `SH101_fixed.png`) |
| **SI230** | Roland Service Information 100230 (1984-12-22): changed bender unit, PB-5 to PB-12. | `private/docs/schematic/bulletins/roland-si-100230-*.gif` |
| **SI248** | Roland Service Information 100248 (1986-01-10): selected CEM3340s; R108 470k to 330k. | `private/docs/schematic/bulletins/roland-si-100248-*.gif` |
| **CEM** | Curtis CEM3340/3345 data sheet (1980). | `private/docs/papers/vco_cem3340/curtis_ces_1980_cem3340_3345_datasheet_long.*` |
| **IR9022** | Sharp IR9022 data sheet: 26 Vpp output on ±15 V, slew 0.5 V/µs. | `private/docs/datasheets/ir9022-sharp.pdf` |
| **OM** | Owner's manual. | `private/docs/sh101-owners-manual*.pdf` |
| **CB** | circuitbenders' SH-101 mods page (behaviour notes). | `private/docs/papers/opensource/diy_sh101_mods/circuitbenders_sh101.txt` |

The zoomed crops made for this transcription are `private/docs/schematic/crops/osc-*.png`.

**Symbols (SN400).**

| Symbol | Meaning |
|---|---|
| Dot on a short stub | The **+5 V reference bus**, trimmed by VR1 D/A WIDTH. The bender board's connector confirms it: pin 16 "+5V" runs to the same symbol (`osc-lfo-harness-bender-pins.png`). |
| Half-filled triangle (left half black) | **+15 V**: IC1b's output in the power supply, 3 × the bus through R5/R6. SN p.7 labels it at the bender connector, pin 19 "+15V" (`sn400-p-7.png` near (2400, 2100)). |
| Filled triangle | **+14 V**: TR3's emitter, a V_BE below +15 V. Pin 21 "+14V" on the same connector; on the bender board it is the "+14V" pin too. (Read as +15 V before 2026-11-13.) |
| Open arrow (a plain chevron) | **+9 V** (bender connector pin 15; R8's top in the power supply; jack board A pin 94). |
| Outline triangle (unfilled) | The CPU's logic and memory-backup supply, from jack board A pin 104 "+4.5V" through D15: the TMP80C49's pin 40, the 4050, control connector pin 2. No analogue part uses it. |
| Down-pointing triangle | **−5 V** (op-amp V−, CEM3340 pin 3). |
| Hatched bar | Ground. |

**Conventions.** Slide pots: "100kB" = linear, "100kA" = audio. Positions are travel 0–1, with 0 at
the end Roland draws at the bottom (the 0 of the panel scale). In the model an A track is 15 % of
its resistance at half travel. Capacitors written "x/y" are µF/V. V_T = 25.69 mV (25 °C, as the
filter section; 27 °C until 2026-11-14). The 2SC1815 and 2SA1015 have I_S 3.3·10⁻¹⁴ A (0.62 V at
1 mA), the same part set as the filter's (`tools/spice/osc/models.lib`; 2.0·10⁻¹⁴ before, which
gave 0.637 V at 1 mA against the data sheet's 0.62 V).

**Op-amps.** The IR9022s (IC3, IC4, IC5, IC12, IC16, IC18, bender IC3) swing to about 2 V inside
their supplies (IR9022). IC3 and IC4 run from +15 / −5 V; IC12, IC18 and bender IC3 from +14 V /
ground; IC5 from +9 / −5 V. IC9 and IC11 are TL062s (D/A) on +14 / −5 V, and so is bender IC2
(re-read on both scans, 2026-11-13).

---

## 1. Rails

| Rail | Value | Notes |
|---|---|---|
| +15 V | 15 V (14.87 V measured, research.md §8; U-Labor 15.0 V) | IC1b's output (half-filled triangle): the CEM3340's V+ (pin 16) and R101; IC3 and IC4 (with −5 V); R24 and R35 on the LFO board; R102; bender R10/R11. |
| +14 V | 14.3 V (measured by U-Labor; 14.0 V nominal) | TR3's emitter (filled triangle): the bender board; R92 (the pulse), the 4013's supply and R183, R83/R84 (noise), R177/R178 (PWM, §8), TR25's collector. The model puts the bender board, R92, the 4013 and R177/R178 on it (`kV14`); the filter section's parts on it are in `circuit-filter.md` §1. |
| +5 V bus | 4.931 V once D/A WIDTH is trimmed (research.md §2) | Modelled as exactly 5 V. The D/A trim makes one DAC step exactly 1/12 V against this bus. |
| −5 V | from IC2b | Op-amp V− for IC3/IC4, CEM3340 pin 3, VR7, R14. |

---

## 2. DAC sample-and-holds (SN400 p.7, `crops/sn400-p7-dac-kcv.png`)

The DAC itself is in `docs/firmware.md` and research.md §2.1. One code step is 1/12 V. IC9a
drives the 4052's Y common through R72 220 Ω. The CPU's DB7/DB6 select the hold:

| 4052 | Hold | Buffer | Output |
|---|---|---|---|
| Y0 (pin 1) | C30 0.015 µF | IC9b follower, R73 1k inside the loop | **KCV**, to the CV OUT jack: code/12 V |
| Y1 (pin 5) | C28 0.015 µF | IC11b follower | **RANGE**, through VR5 5kB "RANGE WIDTH" (rheostat) + R99 97.6k (1 %) to CEM3340 pin 15 |
| Y2 (pin 2) | C29 0.015 µF | IC11a, non-inverting gain 2 (R81 100k to ground, R82 100k feedback) | **RANDOM**: 2 × code/12 V, 0–10.5 V |
| Y3 (pin 4) | — | — | not connected (the park position) |

COMPUTED: the holds acquire with τ = 220 Ω × 15 nF = 3.3 µs. Droop is negligible (research.md §2.1),
so neither is modelled.

The KCV leaves through CV OUT and comes back through the CV IN switching jack as the bender board's
PORTA IN (bender pin 19). So portamento acts on the KCV only, and RANGE (with key transpose in
the arpeggio and sequencer) is added after the glide.

---

## 3. Portamento (SN400 p.5 bender board, `crops/osc-bender-portamento.png`, SN p.7 `crops/sn-p7-portamento-transpose.png`)

| Part | Value | Connection (read) |
|---|---|---|
| VR2 | 2 MΩ (A) "PORTAMENTO" | Track between the PORTA IN node and C10. The wiper is tied to the PORTA IN end, so VR2 is a rheostat. |
| C10 | ".33" (handwritten): 0.33 µF | C10 node to ground. |
| IC2b | TL062 | Follower from C10. Its output is the **CV** line (bender pin 18 → main 73). |
| TR1 | 2SK30A | Channel across VR2. Gate to the PORTA IN side through R17 1 MΩ. |
| D1 | 1S2473 | Anode on TR1's gate, cathode on TR4's collector. |
| TR4 | 2SA1015 (PNP) | Emitter +14 V; base R18 22k to +14 V and R19 100k to TR5's collector. Collector to D1, and R14 100k to −5 V. |
| TR5 | 2SC1815 | Emitter ground; base node "X" with R23 100k to ground and D2 (anode ground) as a clamp. |
| R15, C11 | 47k, .0047 µF | GATE IN (bender pin 15) → R15 → C11 → S3's common. |
| R16 | 220k | PORTA OFF (CPU P23, bender pin 21) → S3's common. |
| R24 | 220k | +14 V → one S3 contact. |
| S3 | 3-position slide switch, panel AUTO / OFF / ON | Common = the C11/R16 junction. The contacts are R24, ground and the TR5-base line. |

How it works (COMPUTED / read). With TR5 off, TR4 is off, R14 pulls D1's cathode to −5 V, and
TR1's gate sits about 4 V below its source. TR1 is pinched off, so C10 charges through VR2 with
τ = R_VR2 × 0.33 µF, at most **0.66 s**. A two-octave step settles to 1 cent in 0.66 s × ln 2400 =
5.1 s, the spec's "0–5 s". With TR5 on, TR4 lifts D1's cathode, R17 brings TR1's gate to its
source, and TR1 shorts VR2.

TR1 is not a plain resistor when it is on (COMPUTED, SPICE). Its gate follows PORTA IN, so:
- Discharging C10 (PORTA IN is the source, V_GS = 0), the channel saturates at I_DSS, about 1 mA.
  C10 slews at 3 V/ms.
- Charging, the gate junction lifts V_GS to about 0.6 V, which allows about 2.6 mA.
- Near V_DS = 0 the channel is 500 Ω, which gives τ = 0.17 ms.

**S3 (UNCERTAIN, deduced from the behaviour each position must have).** The drawn arm connects
the common to the TR5-base line. No single-throw reading of the three contacts can produce OFF. If
the slider bridges adjacent pins in the order [common][TR5 base][R24][ground], the positions are:
- **AUTO:** the common joins TR5's base.
- **OFF:** R24 joins TR5's base. That gives 4.7 V, so TR5 is always on and there is no glide.
- **ON:** R24 is grounded and TR5's base is left to R23. TR5 is off and glide is always on.

This matches the owner's manual (AUTO/OFF/ON; "ON: the portamento effect is always obtained") and
its note that portamento works with the arpeggio only in ON.

**AUTO.**
- Each rising edge on GATE IN (0 → about 12 V) pushes a pulse through R15/C11 into TR5's base.
  COMPUTED / SPICE: TR5 stays on for about 0.75 ms, until C11's current (τ ≈ R15·C11 = 0.22 ms)
  falls below what R23 and R16 take.
- That shorts the glide at the start of every new gate, so detached notes jump and legato notes
  glide (research.md §2.1, Tubbutec and Reid).
- Because TR1 limits at I_DSS, a jump of more than about 2 V does not finish within the pulse.
  SPICE leaves 81 mV of a 1.8 V step (about 1 semitone) to glide on at the knob's rate.
- PORTA OFF (P23) holds TR5 on through R16 (5 V into R16/R23: 1.56 V). The firmware raises it
  exactly while the arpeggio runs (docs/firmware.md), so AUTO does not glide in arpeggios.

---

## 4. VCO (SN400 p.7, `crops/sn400-p7-vco.png`, `osc-vco-pins-4-8.png`; CEM)

### 4.1 CEM3340 connections

| Pin | Connection (read) | Notes |
|---|---|---|
| 16 V+ | +15 V, C40 2.2/50 | |
| 15 freq. CV (virtual ground) | the summing resistors of 4.2; R107 560 + C44 .01 to ground | compensation, as the data sheet asks |
| 14 scale | R106 1.69k (1 %) + VR6 300 Ω (B) "VCO WIDTH" to ground | R_S |
| 13 ref. current | R101 1.5M (1 %) from +15 V; R105 560 + C43 .01 to ground | I_REF = 15 V / 1.5 MΩ = 10 µA |
| 12 | ground | |
| 11 timing cap | C41 1000p, circled "S" (selected) | |
| 10 triangle | not used | |
| 9 soft sync | C42 .01 to ground | unused |
| 8 saw | to connector 57 (control board: VR14 SAW slider, R179 → TR41 sub-osc clock) | 0–10 V |
| 7 HF track | R99' 10k to ground (OML: R199 on the board); line to R100 1.5M → pin 15 | |
| 6 hard sync | C38 .01 to ground | unused |
| 5 PWM | R96 100k from TR25's emitter; R94 100k + VR4 50kB "PW" (rheostat) to ground; C37 .01 to ground | |
| 4 pulse (open emitter) | R95 10k → TR24's base, R93 1k base to ground | the pull-down is R95 + R93 |
| 3 V− | −5 V | the internal zener is not used |
| 1, 2 | R98 28k (1 %), R97 5.6k (1 %), returned together to −5 V; C39 2.2/50 | R_Z, R_T |

### 4.2 Pitch summing at pin 15

| Input | Resistor | Source |
|---|---|---|
| CV (post-portamento KCV) | R104 100k (1 %) | connector 73 "CV" |
| RANGE | R99 97.6k (1 %) + VR5 5kB | IC11b |
| IC12b (bender, VCO MOD, TUNE) | R103 100k (1 %) | 4.3 |
| VR7 "VCO TUNE" 100kB, from ground to −5 V | R108 470k (1 %); **330k** from SN 553000 (SI248) | 0 … −10.6 µA |
| HF track | R100 1.5M from pin 7 | I_EG / 600 (COMPUTED: pin 7 sources I_EG/4 into R199 10k) |
| +15 V through a bridgeable pattern break | R102 1.5M (1 %) | open unless VR7 cannot reach 442 Hz (SN p.4) |

COMPUTED, the expo law (CEM; research.md §3.1). With f_ref = 3/(2 R101 C41) = 1.000 kHz (V_CC and
V_T cancel):

    f = f_ref · exp(−22 (R_S / R_T)(1 − I_C R_Z / 3.0))

- 1 V/oct at 100k needs R_S = 3 ln2 R_T / (22 R_Z × 10 µA) = **1.890 kΩ** (R106 + 200 Ω of VR6).
- At I_C = 0 the VCO runs at 0.596 Hz. 442 Hz needs I_C = 95.3 µA.
- IC12b idles at the bus and sends 50 µA through R103, KCV 4.75 V adds 47.5 µA, and VR7 takes off
  the 2.2 µA left over (V_VR7 = −1.04 V). So the pitch offset hangs on the +5 V bus, which is why
  the D/A trim comes first (SN p.4).
- The comparator delay makes the top octaves flat (CEM: above about 5 kHz). The pin-7 feedback
  sharpens them by +2 / +10 / +20 cents at 1 / 5 / 10 kHz. To first order it cancels a fixed
  excess period of 1.15 µs, a design target inferred from the values, not measured.

### 4.3 IC12b and the TUNE knob (`crops/sn400-p7-vco.png`; control board `osc-control-rate-vr10.png`)

| Part | Value | Connection |
|---|---|---|
| IC12b | IR9022 | + input on the bus; output → R103 |
| R89 ∥ C35 | 100k ∥ 680p | feedback: a 2.34 kHz pole |
| R91 | 100k | from connector 72 "VCO" (bender IC3a's output) |
| R90 | 4.7M | from connector 62: the panel **TUNE** VR9 100kB (bus at one end, ground at the other) |

COMPUTED: TUNE moves IC12b's output by 100k/4.7M × 5 V = 0.106 V end to end, which is **±64 cents**
about the centre (spec ±50). The service notes trim VR7 with TUNE centred.

**R104 and VR8 (OML).** The drawing hangs VR8 "VCF WIDTH" 47kB on R91's line. OML's errata say
it is on R104's: the KYBD slider is fed the post-portamento KCV, not the bender. The CV line
drives VR8, a rheostat, into the control board's KYBD slider VR21 (100kB to ground). That part
belongs to the filter section. The oscillator section hands the filter section the CV line
itself.

### 4.4 Pulse and PWM (SN400 p.7, control board `crops/sn400-p7-control-vco.png`, `sn400-p7-control-subosc-mixer.png`)

**Pulse output.**
- The CEM3340's pulse is high while saw/2 < V_pin5: 0 % width at 0 V and 100 % at 5 V (data sheet
  4.6–5.4 V). It rises at the saw's wrap.
- TR24 (2SC1815, common emitter, R92 68k from +14 V) inverts it. So the SH-101's pulse is **low
  from the wrap until the saw reaches 2·V_pin5, then high**. It falls together with the saw, which
  is why saw and pulse add instead of cancelling (research.md §3.1).
- TR24's collector goes to connector 58 → VR13 (PULSE slider, 100kB, its other end on the bus).
- COMPUTED / SPICE, the levels at the top of VR13: 0.01 V with TR24 saturated; 5 + 10 × 100/168
  = **10.36 V** with it off (R92 from +14 V against VR13's 100k to the bus; it was 10.95 V with +15 V).

**PWM network.**

| Part | Value | Connection |
|---|---|---|
| S4 | LFO / MAN / ENV | Common to the top of VR12 |
| VR12 | 100kB "PULSE WIDTH" | Top: S4. Bottom: the bus. Wiper: connector 59 → TR25's base |
| LFO contact | R176 82k from the triangle (LFO board pin 10), R177 150k from +14 V (filled triangle) | PWM by LFO always uses the triangle (CB) |
| MAN contact | R178 120k from +14 V (filled triangle) | |
| ENV contact | R190 18k from the ENV line | The ENV rests on the bus and peaks near 10.4 V (filter section) |
| TR25 | 2SC1815 follower, collector +14 V | Emitter → R96 100k → pin 5 |
| R94 + VR4 | 100k + 50kB "PW" (rheostat) | Pin 5 to ground. Trimmed so that PULSE WIDTH 0 gives a square (SN p.4 (D); the notes misprint the trimmer as VR-2) |
| C37 | .01 µF | Pin 5 to ground. COMPUTED: with R96 ∥ (R94 + VR4) that is τ = 0.56 ms |

COMPUTED / SPICE: VR4 trims to 28.4 kΩ. Pin 5 then goes from 2.50 V (slider 0, wiper on the bus,
whatever S4 says) to:
- MAN: 5.04 V at 10, so the pulse narrows linearly to **0 %** (spec "50 % ~ 0 %").
- LFO at 10: 2.63 V (triangle bottom) to 5.04 V (top), so the width swings 47 % → 0 %.
- ENV at 10: 2.50 V at rest to 5.06 V at the peak.

### 4.5 Sub-oscillator (control board, `osc-subosc-4013.png`)

| Part | Value | Connection |
|---|---|---|
| TR41 | 2SC1815 | Base: the saw through R179 220k, R180 33k to ground. Collector: R183 68k pull-up, the 4013's clock |
| IC17 | 4013 (MB / MC14013) | Two toggle flip-flops; the second is clocked by the first (inferred) |
| R181, R182 | 47k | From Q1 and Q2 to the 1 OCT and 2 OCT contacts of S5 |
| D38, D39 | 1S2473 | Anodes on those two nodes, cathodes joined: the 2 OCT 25 % contact |
| S5 | 1 OCT ⊓ / 2 OCT ⊓ / 2 OCT pulse | Common → VR15 SUB 100kB, other end to **ground** (OML: the drawing shows the bus) |

COMPUTED:
- TR41 conducts above 0.6 × 253/33 = 4.6 V of saw. Its collector, the 4013's clock, rises when
  the saw drops through 4.6 V, which is at the **wrap**. So the sub's edges coincide with the saw's
  reset.
- Levels at the top of VR15, slider at 10 (model and SPICE agree to 3 nA): 1 and 2 OCT squares
  0 / 9.52 V (the 4013's +14 V through 47k into 100k).
- The 25 % pulse: 0 V (both flip-flops low, diodes off), 9.20 V (one high, one diode) and 10.97 V
  (both high). So within its 75 % high part it steps: 11.8 V for a quarter of the cycle, then
  9.9 V for half of it.

### 4.6 Noise generator (SN400 p.7, `crops/sn400-p7-noise-gen.png`)

| Part | Value | Connection |
|---|---|---|
| TR23 | 2SC945, "selected for noise" | Reverse-biased junction: R84 1M from +14 V; C34 10/16 to TR22's base |
| TR22 | 2SC1815 | Collector R83 47k to +14 V; base R85 470k from the collector (collector-feedback bias) |
| C33 | 10/16 | TR22's collector → IC12a pin 3 |
| C31, R87 | .01 µF, 47k | Pin 3 to the bus (OML: the drawing shows ground) |
| IC12a | IR9022 | Non-inverting: R88 10k from pin 2 to the bus (OML), R86 220k ∥ C32 240p feedback. Gain 23 at low frequencies, falling from 3.0 kHz. Output → connector 60 (noise) |

COMPUTED / SPICE (`tools/spice/osc/noise.cir`). TR22's output resistance with C31 sets a first pole
before IC12a's 3 kHz shelf. Where that pole falls depends on the junction's dynamic resistance r_d
(it drives TR22's base):

| r_d | pole |
|---|---|
| 100 Ω | 720 Hz |
| 1 kΩ | 1000 Hz |
| 1.5 kΩ | 1140 Hz |
| 10 kΩ | 2.9 kHz |

Three units' clips (Perfect Circuit, gstormelectro, Failed Muso), each with its recording chain
taken out by its own saw, give 1130–1200 Hz: r_d ≈ 1.5 kΩ. AutomaticGainsay's gives 930–1040 Hz
(r_d ≈ 1 kΩ), and its noise is 2.4 dB quieter, about what the lower r_d alone gives TR22's input
(−2.2 dB at 200 Hz in SPICE): a different junction (`docs/model-osc.md`).

The noise level is not on the schematic.

Connector 60 carries the noise to the control board: VR16 NOISE slider (100kB, other end on the
bus), and the waveform switch's NOISE contact (5.3).

### 4.7 Source mixer (`crops/sn400-p7-vco.png`)

| Connector | Slider (control board, all 100kB) | Resistor | To |
|---|---|---|---|
| 66 | VR14 SAW, other end on the bus | R111 470k | The junction that C45 10/16 BP couples into IC14's first stage (filter section) |
| 67 | VR13 PULSE, other end on the bus | R110 680k | |
| 65 | VR15 SUB, other end on ground (OML) | R112 820k | |
| 68 | VR16 NOISE, other end on the bus | R109 150k | |

The slider-to-resistor mapping is Electric Druid's redraw's (research.md §3.1). The drawing shows
the resistors on connector pins 65–68 but not which slider each pin comes from (UNCERTAIN). The
sliders are marked B (linear). Roland's plug-in uses an audio law. On a real unit, "about 50 %" on
NOISE measures −8.7 dB re full (shootout, Failed Muso), closer to linear (−6 dB) than to an audio
law (about −14 dB).

COMPUTED, with all four sliders at 10, levels re the saw as currents into the mixer node:

| Source | Level |
|---|---|
| pulse (PW 0) | **+1.6 dB** |
| 1 or 2 OCT square | **−0.8 dB** |
| 25 % pulse | **−2.0 dB** |

With R92 and the 4013 on +15 V, as read before 2026-11-13, they were +2.0 / −0.2 / −1.3 dB. Three
real units give +1.1 to +1.5 / −0.5 to −0.85 / −1.75 to −2.1 (`docs/model-osc.md`, "Real units"),
Roland's plug-in +1.9, −1.6 and −2.4 dB (docs/roland-measurements.md). The 14 V-swing estimate in
research.md gave +4.5 and +2.8 dB; the loaded levels above (R92 against VR13, the 47k resistors
against VR15) account for the difference.

---

## 5. MODULATOR (SN400 p.7 LFO, `osc-lfo-core-ic3.png`, `osc-lfo-schmitt-tr5-tr6.png`, `osc-oml-lfo-r24-r31-r35.png`; control board `osc-control-rate-vr10.png`, `osc-waveform-switch-s2*.png`)

### 5.1 Core

| Part | Value | Connection (read) |
|---|---|---|
| IC3a | IR9022 (+15 / −5 V) | Integrator: + input on the bus; C9 0.1 µF from pin 2 to the output (pin 1, the **triangle**) |
| R15 | 120k | From connector 8 (RATE wiper) into pin 2 |
| IC3b | IR9022 | Comparator: pin 5 ← triangle through R30 10k; pin 6 ← the TR5/TR6 node through R29 10k; output (pin 7) is the **square**, to connector 9 |
| TR5 | 2SC1815 | Base: the square through R28 220k, R25 33k to ground. Collector: the node, with R31 **100k** (Din Sync's BOM and a real unit's top rate; OML has 47k, see 5.3) to connector 11 |
| TR6 | 2SC1815 | Base: R26 220k from TR7's collector. Collector: the node, with R35 **100k** (as R31) to +15 V |
| TR7 | 2SC1815 | Base: R36 100k from the CPU's P24 (CLK RST, active low). Collector: R24 **82k** (OML) to +15 V |
| TR4 | 2SC1815 | Emitter on IC3a's pin 2; collector ← D9 ← R16 15k ← the square; base ← R17 47k ← D10 ← TR7's collector |
| TR8 | 2SC1815 | Base: the node through R32 220k, R34 33k to ground. Collector: R33 100k to +9 V, C15 10 nF (OML). This is the **CLK** to the EXT CLK jack's normalling and on, through TR11, to the CPU's T1 |
| VR10 RATE | 100kA + R168 220 Ω | Top: connector 9 (the square; also D35 → TR40 → the RATE LED). Bottom: R168 to the bus. Wiper: connector 8 |

How it works (COMPUTED). RATE takes a share k of the square's swing about the bus:
- k = (a·100k + 220)/(100.22k) for a wiper at track fraction a.
- R15 plus the wiper's source resistance turn that share into C9's current.

IC3b compares the triangle with the node:
- While the square is high, TR5 saturates and holds the node at about **0.02 V** (SPICE). The
  triangle falls to that.
- While the square is low, the node is high: R35 from +15 V against R31 and S2's load, and R32
  into TR8's base.

The square swings 5 ± 8 V (IR9022 on +15 / −5 V), so the frequency is
k × 8 V / (R15 + R_s) / (2 C9 ΔV).

| | Value |
|---|---|
| Upper threshold (S2 on TRIANGLE, RANDOM or SQUARE) | **9.03 V** (47k: 10.83) |
| Upper threshold (S2 on NOISE) | **10.42 V** (47k: 12.36) |
| RATE 0 / 5 / 10 | **0.080 / 5.04 / 36.7 Hz** (spec 0.1–30 Hz; a real unit's top 38.2 Hz; SPICE within 0.1–1.1 %) |

NOISE runs 12 % slower: in that position nothing loads pin 11.

### 5.2 CLK RST

With P24 low, TR7 turns off and R24 lifts its collector:
- TR6 turns on through R26 and holds the node low. TR8 lets go, so **CLK goes high at once**: the
  first arpeggio or PLAY step comes immediately (docs/firmware.md).
- TR4 turns on through D10 and R17. Its base current (8.7 V / 129k = 68 µA) flows into IC3a's
  summing node all the time. While the square is high, the square adds about 0.49 mA through R16
  and D9.
- COMPUTED / SPICE: so the triangle falls at about 5.6 V/ms to the lower threshold, then keeps
  falling at 0.6 V/ms (TR4's base current beats RATE's current), ending about 0.5 V below the
  threshold if CLK RST outlasts the fall.
- When P24 rises again (one CPU loop, about 1.5 ms) the square is low and the triangle climbs from
  near its bottom. The next rising clock edge comes half a period later, plus however long the
  climb back from below the threshold takes.

The restart is not exact (the triangle may not reach its bottom within the loop if it started near
the top). CB's "restart from its peak" describes the same circuit's trigger.

### 5.3 Waveform switch S2 and IC18a (control board, `osc-waveform-switch-s2.png`, `osc-s2-noise-lp-vr11.png`, `sn400-p7-control-vcf.png`)

S2 has two wafers (read).

**Right wafer.** Its arm is the output, to IC18a's + input, which R188 biases to the bus.
**R188 is 100k** (CORRECTED; SN prints 47k): Din Sync took a real SH-101 apart for the RE-101
and changed it to 100k "(actual value used on original sh101) result is increased modulation
depth", and a repair photo of another control board reads brown-black-yellow (`model.md`, "Real
boards").
IC18a is a follower. Its output feeds VR11 VCO MOD (100kA, other end on the bus; its wiper goes
through connector 12 to the bender board's "VCO MOD IN") and VR20 VCF MOD (filter section).

| Position | Contact | Output (COMPUTED) |
|---|---|---|
| TRIANGLE | R170 47k from the triangle (connector 10) | 5 + 0.68 (tri − 5): **1.61–7.74 V** |
| SQUARE | Connector 11, the far end of R31 | The node through R31 against R188: **2.51 / 7.02 V** |
| RANDOM | R175 33k from IC11a (0–10.5 V) | 5 + (V − 5) × 100/133: **1.24–9.14 V** |
| NOISE | The noise (connector 60) through C61 10/16 BP into R173 100k (to the bus) ∥ R174 2.2M (to ground), then R172 1k with C60 .01 to ground | AC: high-pass 0.5 Hz, low-pass 16.1 kHz (R172 ∥ R188 with C60); DC 4.89 V |

**Left wafer.** It connects pin 11 to R169 (to the bus) in the TRIANGLE and RANDOM positions
and leaves it open otherwise. R169 is 100k on Din Sync's board (SN: 47k), the same as R188. So R31
always sees 100k to the bus (R169, or R188 through IC18a)
except in NOISE. That is why the upper threshold, the triangle's amplitude and the rate change
only in NOISE.

With R31 at 100k the triangle leaves IC18a swinging 1.61–7.74 V, centred 0.32 V below the bus,
and the square 2.51–7.02 V, 0.74 × the triangle's swing. The triangle's offset reaches the pitch
through VCO MOD (at 10 it lowers the mean a little over two semitones); nothing trims it (VR3 LFO
MOD OFFSET only nulls the grip's path, 6.2). VCO MOD at 10 on the triangle spans 4699 cents peak to
peak (Roland's plug-in 5286).

R31 and R35 (CORRECTED from OML's 47k to 100k; SN leaves them blank):
- Din Sync's BOM, from a dismantled unit, gives 100k for both.
- A real unit's top LFO rate is 38.2 Hz (Perfect Circuit's LFO rate sweep, 15–17.5 s: sidebands
  38.2 Hz apart around a 349.9 Hz note once RATE has reached the top). 47k gives 30.6 Hz, and 28–35
  Hz across the parts' spread; 100k gives 36.4 Hz, and 33.2–41.5 Hz across the spread (5–95 %;
  the real unit at the 65th percentile, `spread.md`).
- Against Roland's plug-in the LFO's rates come closer at every RATE (`model.md`).
- R31 too is 100k, on a real unit's sound (2026-11-13). R35 at 100k with R31 at 47k would also give
  about 38 Hz, but it would make the square's swing at IC18a equal the triangle's, as Roland's
  plug-in has them. AutomaticGainsay's `vco_mod_deep_lfo_wave_switching` turns WAVE FORM from
  TRIANGLE to SQUARE with VCO MOD held at about 80 %: the square sits at −1443 and +2417 cents (re
  C3), so 854 cents per volt at IC18a with both resistors at 100k (square 2.50 and 7.02 V), which
  puts the triangle's top (7.74 V) at +3032 cents; the clip's triangle peaks at +2950 to +3050. With
  R31 at 47k the square's top would reach the triangle's, about 600 cents above what the clip
  plays.

The research digest put C61, R173 and R174 after the switch. The drawing has them on the noise's
way to the NOISE contact.

### 5.4 Where the modulator goes

| Destination | Path | Notes |
|---|---|---|
| VCO pitch | VR11 → bender IC3a R1 150k (gain 2/3) → IC12b (gain 1) → R103, 1 V/oct | COMPUTED: VCO MOD 10 swings +2.64 / −2.26 octaves on the triangle |
| VCF | VR20 (filter section) | |
| PWM | S4's LFO contact: the triangle only (CB) | |
| CPU clock | CLK (TR8) | |
| ENV LFO trigger | the square (filter section, S7) | |
| Bender LFO MOD | the triangle (connector 10 → bender pin 14) | |

---

## 6. Bender board (SN400 p.5, `osc-bender-ic3-summer.png`, `osc-bender-lever-modsw.png`, `osc-bender-grip-lfo-shaper.png`)

### 6.1 Lever and summer

| Part | Value | Connection (read) |
|---|---|---|
| Bender VR | Lever pot (pins 26/28 to −5 V / +14 V, wiper pins 25/27) | UNCERTAIN: which pin is the centre tap |
| R5 / R6 / R26 | 33k / 4.7k / 100k; **22k / 8.2k / 56k** with the PB-12 lever pot from SN 483000 (SI230) | Lever wiper → R5 → IC3b's input; R6 to the node with R26 from +14 V and connector pin 33 |
| C8 | (no value) | IC3b input to ground |
| IC3b | IR9022 (+14 V / ground) | Follower: the lever voltage |
| VR4 VCO, VR5 VCF | 100kB each | From IC3b's output to the bus. VR4's wiper → R4 100k → IC3a; VR5's wiper → "VCF BEND" (bender pin 1 → control board R185) |
| IC3a | IR9022 | Inverting about the bus: R3 100k feedback. Inputs R1 150k (VCO MOD IN), R2 150k (the grip LFO, VR6's wiper), R4 100k (VR4) |

COMPUTED: IC3a then IC12b give the VCO 1 V/oct from VR4's wiper and 2/3 V/oct from VCO MOD IN and
from the grip LFO.

The lever's full deflection at IC3b is not derivable: the lever turns only part of its pot, and
pin 33's normalling isn't drawn. A real unit bends **+14.7 semitones at VCO 9** (shootout,
AutomaticGainsay). With VR4's loading that puts IC3b's deviation at full lever at **±1.48 V**. The
manual says "up to about ±1200 cents".

### 6.2 Grip LFO MOD

| Part | Value | Connection (read) |
|---|---|---|
| R13, R7 | 100k, 2.2k | The triangle (bender pin 14) → TR2's left base, R7 to the bus |
| R20 | 2.2k | TR2's right base to the bus |
| TR2 | 2SC1583, matched dual | Differential pair. Collectors R10 / R11 68k to +15 V (p.5's half-filled arrow, on both scans; the rest of the bender board is on +14 V), and IC2a's inputs |
| IC2a | TL062 (+14 / −5 V) | Pin 3 ← left collector, R8 22k to the bus, R9 470k to VR3. Pin 2 ← right collector, R12 22k from the output (UNCERTAIN which collector goes to which input: it only sets the vibrato's phase against VCO MOD) |
| VR3 | 200kB "LFO MOD OFFSET" | Trimmed for 0 ± 2 mV between TP-1 and TP-2 (SN p.4). The model nulls IC2a's output with the MOD released |
| TR3 | 2SC1815 | Collector: TR2's emitters (the tail). Emitter: R25 68k to ground. Base: C9 0.47/50, R22 1M from +14 V |
| TR6 | 2SC1815 | Collector → R27 100 Ω → C9. Base: R21 220k from +14 V, pulled to ground by the MOD switch (lever push, bender pins 29/30) or the grip (pin 32) |
| VR6 | 100kA "∿ LFO MOD" | From IC2a's output to the bus. Wiper → R2 (IC3a) and "VCF LFO" (bender pin 2 → control board R184) |

How it works (COMPUTED, SPICE within 1 %):
- R13/R7 attenuate the triangle to ±109 mV across the pair, about ±2.1 V_T, which TR2 rounds into
  a sine.
- With the MOD released, TR6 keeps C9 empty and there is no tail current.
- Pressed, C9 charges through R22. TR3's collector, though, is TR2's emitters, about 0.5 V below
  the bus. So once C9 reaches about 5 V, TR3 saturates and C9 stops: the **fade-in is the first
  0.21 s** of R22·C9's rise, nearly linear, not the 0.47 s time constant or AMSynths' "1.5 s".
- The tail then holds at 4.45 V / 68k less R22's current: **57 µA**. IC2a's output swings
  22k × 57 µA × tanh ≈ **±1.2 V** about the bus. At LFO MOD 10 that is ±0.8 oct at the VCO.
- Release discharges C9 through R27 (47 µs).

---

## 7. What the oscillator section hands on

| Signal | To | Model (`OscFrame`) |
|---|---|---|
| The four slider wipers and their mixing resistors | C45 / IC14 (filter) | `mix[]`: wiper Thevenin, Norton-equivalent behind R_mix + the slider's source resistance |
| IC18a's output (MODULATOR wave) | VR20 VCF MOD | `modVolts` (relative to the bus) |
| CV line (IC2b) | VR8 / KYBD | `kybdVolts` (absolute) |
| The square | S7's LFO trigger | `lfoSquare` |
| VCF BEND (VR5's wiper), VCF LFO (VR6's wiper) | R185, R184 | `vcfBend`, `vcfLfo` (relative to the bus) |

---

## 8. Open questions

1. S3's contact order (3, deduced). A photo of the switch's wiring or a continuity test would
   settle it.
2. Which slider feeds which of connector pins 65–68 (Electric Druid's mapping is used).
3. Settled (2026-11-13): the 4013's supply is +14 V (filled triangle = TR3's emitter; see §0, the
   bender connector's pin 19 "+15V" and pin 21 "+14V"). Still open: whether the second flip-flop
   is clocked by Q1 or Q̄1, which only shifts the 2-octave square by one saw period. The same key
   puts R177/R178 (PWM) on +14 V (their arrow is the solid one, `crops/sn400-p7-control-vco.png`),
   and the model has them there since 2026-11-13: PW 10 on MAN is 3.0 % instead of 0 %, and the
   law 50 / 38.3 / 26.5 / 14.8 / 3.0 % at 0 / 2.5 / 5 / 7.5 / 10 with TR25 on the 25 °C part set
   (2026-11-14; 38.2 / 26.4 / 14.7 / 2.9 % at 27 °C and I_S 20 fA). Roland's plug-in: 49.9 / 38.4 /
   26.8 / 15.3 / 3.9 %. The +14 V
   rail's value (14.0 nominal; 14.3 V on U-Labor's unit, which the model takes) moves the pulse
   and sub by 0.1-0.2 dB.
4. IC2a's collector-to-input assignment (the grip vibrato's polarity against VCO MOD).
5. The noise level: junction noise of a selected 2SC945. The model takes the two recorded units
   that agree (Real unit calibration: 0.98 V at IC12a) or Roland's (Roland calibration).
6. The lever's travel on its pot and pin 33's normalling: the model takes a real unit's bend.
