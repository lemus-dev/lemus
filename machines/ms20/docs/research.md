# Korg MS-20: research

Phase 0 and 1, 2026-11-14. Everything downloaded is listed with URL and SHA1 in `sources.md`; the
files live in the private folder (`private/`, linked by `tools/link-private.sh`).

**Decided (2026-11-14):** target **Rev 1**, the MS-20 with two KORG35 filter modules on boards
KLM-127, KLM-128 and KLM-129, as the service manual draws it; plugin **ko-mono-20**, code **K20m**;
no standalone app (the MS-20 has no sequencer of its own). The KORG35 is netlisted from its published part
values (Korg's 2000 drawing, Stinchcombe and Tanaka agree), cited to Stinchcombe's public study; no
drawing ships (§2.1). The recommendation per block is at the
end of each section (§1.6, §2.3, §3.2, §4.4).

Marks used below:
- **[V]** verified this session: I read the document, the page or the figure.
- **[I]** inferred from the circuit or the documents. It needs solving or measuring.
- **[N]** not checked: from a search snippet or a single secondary source.

## Summary

| Part | Where it stands | How close we can get |
|---|---|---|
| Circuit (whole instrument) | **Public and complete.** The Keio service manual (10 pp.) has both circuit diagrams, PCB views, parts list, block diagram and the adjustment procedure. Two independent scans; values legible to the part. [V] | Component level, the way the 303, 909 and SH-101 are done. |
| Filters, 2 × KORG35 | Custom resin-sealed module. Korg disclosed its circuit in 2000; Stinchcombe redraws it with values and analyses it in depth (Sallen-Key, two BJTs in reverse saturation as resistors, JFET buffer, diode limiter in the forward path). [V] | Transistor level on our solver. The main risk is the BJT model in reverse saturation and the parts inside the module that no document sizes (§2). |
| VCOs | Discrete: Hz/V control, a log/antilog dual transistor (2SC1583) multiplying by the exponential modulation, a ramp core with a three-transistor latch reset, JFET buffer. [V] | Close: the SH-101 and 909 method (netlist, band-limited resets). Pitch law is the classic Hz/V system, so tuning checks are exact. |
| Keyboard, portamento, triggers | Resistor-ladder keyboard producing Hz/V (403/427 Ω per key, 61.9 kΩ shunts), CA3140 hold, FET key-hold switch, trigger detector. No CPU. [V] | Exact from the schematic. |
| EG1, EG2, MG, noise, S&H, MVCA | Op-amp, CMOS-switch and transistor circuits. The MVCA is an LED/LDR photocoupler (HTV-P873). [V] | Exact, except the photocoupler, which needs a fit to its data sheet or recordings. |
| VCA | A single selected 2SC945 transistor, ranked by colour with two bias resistors (table on the schematic). [V] | Close; its distortion is part of the sound and comes from the transistor model. |
| ESP (external signal processor) | Preamp, 4-gang low/high cut, F-V converter (µPC339, 4069), envelope follower, trigger detector. [V] | Exact from the schematic. Needs an audio input on the plugin. |
| Patch panel | 35 jacks with switched normalling (owner's manual, block diagram). [V] | Exact. A hardware-view feature: working cables. |
| Reference | KORG's own MS-20 mini (2013) reissue uses the KORG35 circuit; free YouTube recordings of vintage units, minis and K35-vs-OTA comparisons; KORG's MS-20 plugin as a free demo (sanity check only). | See §4. |

**Tier: 1.** No firmware, no undecoded chip, circuit and the custom module documented, and Korg's
own hardware reissue to compare against. The weakest input is the reference: recordings have no
stated settings, as for every analog machine.

## 1. Circuit

### 1.1 Documents found [V]

- **Service manual** (Keio Electronic Laboratory, "Monophonic synthesizer service manual MS-20",
  10 pp.): 1 specifications, 2 structural diagram, 3 circuit diagram (1) (KLM-127, the main board)
  and (2) (KLM-128 noise, S&H, MVCA, wheel, phones; KLM-129 the ESP), 4 PCB front views, 5 parts
  list, 6 block diagram with check points, 7 adjustment procedure.
  - Scan A (archive.org `korg_MS-20_SERVICE_MANUAL`): upright, retouched, 72 ppi images about
    2100×1600 per schematic page. The clearest for values.
  - Scan B (archive.org `JL11276`, identical to Synthfool's `Korg_MS_20_Service_Manual.pdf`, same
    SHA1): 2133×3000 pages, schematics rotated. An independent copy for cross-checking values.
  - The archive.org OCR of both is in the private folder; it is poor on the schematics but reads the
    specifications, parts list and adjustment procedure.
- **Stinchcombe, "A Study of the Korg MS10 & MS20 Filters"** (30 Aug 2006, revised 2009, 46 pp.):
  the KORG35 circuit (Fig. 1, with the MS-10 board's values), the OTA version (Fig. 2, KLM-307),
  transfer functions, reverse-saturation control, exponential control, the diode limiters, the
  asymmetric resonance and the 6 dB/oct high-pass.
- **MS-10 service manual** (Synthfool): the same KORG35 filter with MS-10 values, the board
  Stinchcombe redrew; useful to cross-check the module pins.
- **MS-50 schematics** (Synthfool): the sibling modular's VCF/VCA and other modules, same designers
  and period.
- **Owner's manual** (Synthfool, image only, 13 pp.): panel, patch panel, normalling, use.
- Four photos of an MS-20's wiring (Synthfool).
- A fourth copy of the service manual: hyperreal's 1994 TIFF scan.
- **Korg's own documents** (korg.com): the original owner's manual, Setting Examples, Patch Book;
  the MS-20 mini, MS-20 Kit and MS-20M Kit manuals (the Kit p. 14 and M p. 13 describe the
  REV.1/REV.2 filter choice); the mini developer interview pages. The archive.org owner's manual
  supplement covers the ESP.
- **Later boards** (hyperreal, clean high-resolution GIFs): KLM-307, the LM13600 daughterboard, and
  the KLM-127 main board as changed for it ("small changes were made to the main circuit board",
  per the readme). Far more legible than the scans for the VCOs, envelopes, MG and power supply,
  but it is the Rev 2 board: every value read from it is checked against the service manual's
  Rev 1 drawing.
- **KORG35 internals from Korg** (§2.1) and K. Tanaka's analysis pages (KORG35 core, transistors in
  saturation and reverse operation, Korg's PUT VCO core with scope shots of a built core).
- **Korg's own monotron and monotribe schematics:** later discrete filters of the KORG35 family,
  published by Korg.
- Papers: Pirkle's KORG35 low-pass and high-pass app notes (read for the analysis only; their code
  is not used), Zavalishin's *The Art of VA Filter Design* 2.1.2 (§5.8 Sallen-Key, §6.3 feedback
  saturation), DAFx-19 (Parker et al., the Rev 2 low-pass against measurements of a built circuit),
  DAFx-21 (port-Hamiltonian Rev 2 low-pass), DAFx-17 (the MS-50's diode-bridge filter), DAFx-26
  (Sallen-Key stability under modulation).
- Data sheets: 4558, TL081/082, CA3140, LM13600/13700, MC14007UB, MC14069UB, CD4011B, µPC339,
  2SA733, 2SC945 (NEC), 2SK30A (Toshiba), 2SA564A, and **2SC1623 and 2SA812**, the KORG35's
  transistors; NEC 1981 linear and discrete data books and Toshiba's 1973 book (1S1555 diode).
  None for 2SC1583, 2SC1685, 2SC644, 2SK94 or the HTV-P873 photocoupler.

Every file with its URL and SHA1: `sources.md`.

### 1.2 Revisions

- **Rev 1 (target):** two KORG35 modules (IC9 HPF, IC10 LPF) on KLM-127. The service manual's
  parts list has "KORG35 × 2" [V].
- **Rev 2:** the filter moved to a daughterboard, KLM-307, built around LM13600 OTAs with an
  exponential converter of two PNPs; three series diodes in the feedback path instead of one pair
  in the forward path (Stinchcombe §2, §5, §6) [V]. KLM-307 and the matching KLM-127 are in hand
  (§1.1).
- Serial numbers do not separate them: a community table has KORG35 and LM13600 units interleaved
  between about 140018 and 142663 [N, ZASM]. A forum moderator reports at least one revision
  between the filter changes [N]. The board, not the serial, defines our target.
- KORG's MS-20 mini (2013) uses the KORG35 circuit (KORG developer interview) [N]; the MS-20 Kit
  and MS-20M default to the early filter and switch to the later one at power-on with a key
  combination or a switch [N].

### 1.3 The voice [V, scan A circuit diagram (1)]

- **Keyboard and CV.** 37 keys (C to C). Each key contact ties one node of a series ladder to
  the + rail. The ladder alternates 403 Ω and 427 Ω metal-film resistors (one per semitone) with a
  61.9 kΩ shunt to ground at every second node and 6.8 kΩ at the far end: a lossy line whose
  attenuation per section is exponential. Per whole tone, cosh α = 1 + 830/(2 × 61.9 k), α ≈ 0.116,
  so per semitone e^0.058 ≈ 1.0596 against 2^(1/12) = 1.0595 [I]. A nodal solve of the ladder
  (2026-11-14, scratch script; output loaded by R34 + R35 only) gives octave ratios 2.000, 1.991
  and 1.959 from the bottom, semitones 94.5 to 100.1 cents: the load sags the top octave, which is
  what the KBD CV high trim (VR4, ❺) is for [I]. KBD CV is
  therefore **Hz/V**, each octave doubling (the adjustment procedure §7-3 trims C-3 to exactly half
  of C-4). VR4 (100 kB) with R33 68 kΩ loads the output end; R34 2.94 kΩ and R35 4.27 kΩ divide it
  into IC4 (½ 4558, follower). Block diagram: 0 to +8.4 V.
- **Key priority [I]:** with two keys down, the node nearer the output end decides the output and
  the section behind it no longer matters. The output end is the top of the keyboard (it gives the
  highest voltage), so the MS-20 has **high-note priority**, set by the ladder itself. The same
  solve with keys 5 and 20 down gives key 20's voltage to 0.1 mV.
- **Portamento and key hold:** R30 680 Ω + VR10 2 MΩ (A) into C6 0.22 µF, held by a K30A JFET
  (Q23) when no key is down; IC5 (CA3140, R46 100 Ω) buffers it to KBD CV OUT (J33) and, normalled,
  to VCO 1+2 CV IN (J26).
- **Trigger detector:** IC13, IC18 (4558), Q13–Q16 (2SC1685, 2SA564A), LED2 and TRIG OUT (J32,
  +5 V). It retriggers on a key change when the CV moves (C21 0.001 µF into Q16) [I: to settle in
  phase 4].
- **VCO pitch path:** the Hz/V CV goes through a **footage ladder** of eight 100 kΩ 0.1 % resistors
  (R20–R27 for VCO 1, R41–R54 for VCO 2): the SCALE switch picks 100k, 200k, 400k or 800k of it as
  the series input resistor into a virtual ground (corrected in phase 4, `circuit/vco.md`), into
  IC3/IC7 (TL081) and
  one transistor of a **2SC1583 dual (Q6, Q7)**. The other transistor's base takes MASTER TUNE,
  PITCH (VCO 2 only), the VCO pitch trims and TOTAL EXT MODULATION (IC4, IC6), so the collector
  current is the linear CV times an exponential of the modulation: linear keyboard, exponential
  modulation. R39 110 kΩ, R40 5.11 kΩ and R38 100 Ω (the circled resistors look like the
  temperature-compensating or 1 % parts; to confirm from the parts list) set the modulation scale.
- **VCO cores:** the current charges C2 6200 pF (VCO 1) or C11 3000 pF (VCO 2), referred to the
  +15 V rail (phase 4: +18 V feeds only the buffer JFETs' drains); a latch of Q2, Q3, Q4 and D4
  (Q9–Q11, D7) with a 427 Ω/427 Ω divider (R16/R17, R57/R58) resets it; a K30A JFET (Q1, Q12)
  buffers the ramp, and Q5/Q8 (K30A) are gate-grounded cascodes on the converter's collector. Sawtooth 7.5 Vpp
  at check point 10/11 (block diagram). Tanaka identifies the latch as Korg's discrete **PUT**
  (programmable unijunction) core and built one: reset about 0.3 µs measured against 0.6 µs in
  his simulation [V, his page]. The reset time sets the top-octave pitch droop, so it is a check
  for the netlist.
- **VCO 1 waveforms (SW-1):** triangle (IC2 with D2/D3, a rectified saw), sawtooth, pulse with PW
  (IC2 comparator, VR9 1 MΩ B), white noise from KLM-128.
- **VCO 2 waveforms (SW-4):** sawtooth, square (IC6 comparator), a narrow pulse (R69 22 kΩ
  threshold) and **ring modulation**: four 4011 NAND gates combine the VCO 1 and VCO 2 squares
  (an XOR, so a digital ring modulator) [I: gate logic to read in phase 4].
- **Mixer:** IC8 (½ 4558), VCO 1 LEVEL VR13 100 kΩ A, VCO 2 LEVEL VR14 100 kΩ B, EXT SIG IN
  (J13, R90 100 kΩ), R73 100 kΩ feedback, R74 33 kΩ.
- **HPF (IC9, KORG35):** C26 0.0033 µF, C15 0.001 µF, R105 15 kΩ, R103 220 Ω, R104 47 Ω,
  R100 20 kΩ; IC8b with R99 270 kΩ and R101 (its value lost in every scan; 4.7 kΩ as its LPF twin
  R116 and the MS-10's R63, `circuit/filters.md`), D9/D10 back to back (gain about 1 + 270/4.7 = 58
  until the diodes conduct); R106 5.6 kΩ is in the PEAK feedback; PEAK VR19 10 kΩ B with R92 2.2 kΩ, R108 2.2 kΩ, R109 47 Ω. Cutoff:
  CUTOFF FREQ VR20 (R91 470 kΩ), MG/T.EXT VR18 (R79 100 kΩ), EG2/EXT VR17 (R78 100 kΩ),
  FC ADJ VR7 (R76 1 MΩ), summed by IC12 (R93 100 kΩ, R96 10 kΩ, R98 4.7 kΩ).
- **LPF (IC10, KORG35):** the same circuit with C29 0.0033 µF, C28 0.001 µF, R111 220 Ω, R117
  270 kΩ with R116 4.7 kΩ (the gain), D20/D21, R113 5.6 kΩ (PEAK feedback); cutoff summer IC12 (R94 100 kΩ, R95 10 kΩ,
  R97 4.7 kΩ), CUTOFF VR21 (R129 470 kΩ), EG2/EXT VR24 (R130 100 kΩ), FC ADJ VR8 (R128 1 MΩ).
- **VCA:** a single **2SC945, selected** (Q19), driven by EG2 and INITIAL GAIN (J22); a table on
  the schematic picks R124 (1 MΩ) and R126 (680 kΩ) by the transistor's rank (orange, yellow,
  green, blue). R123 6.2 kΩ, R120 100 kΩ, R121 220 Ω. Then IC11b (R118 180 kΩ, C34 100 pF) to
  VOLUME, SIGNAL OUT (2 Vpp, 3.5 kΩ) and the phones amp.
- **Power:** transformer, 7815/7915 (IC16, IC17) for ±15 V, and an unregulated +18 V rail
  (C23 1000 µF) for the VCO buffers' drains [V]. Ripple check ≤ 2 mVpp (§7-1).

### 1.4 Control and modulation side [V, circuit diagrams (1) and (2)]

- **EG1** (DELAY, ATTACK, RELEASE; outputs to VCO FM and MVCA, plus REVERSE OUT): Q20 trigger,
  4007 (IC19) switches, TL082 (IC18), C39 6.8 µF tantalum for the delay, C38 3.3 µF for the
  envelope, VR27/VR28/VR29 2 MΩ A. Spec: delay, attack, release up to 10 s each.
- **EG2** (HOLD, ATTACK, DECAY, SUSTAIN, RELEASE; to VCA and filters, REV OUT): 4007 (IC22)
  switches, TL081 (IC21), C40 6.8 µF tantalum, Q21 comparator for the attack peak, a 4069 (IC23)
  monostable for HOLD with C42 10 µF and VR34 2 MΩ; R179/R180 set the attack target. Spec: hold up
  to 20 s, attack, decay, release up to 10 s.
- **MG (LFO):** TL081 integrator (IC14, C22 0.1 µF) with two JFET switches (Q17, Q18) steering
  through the halves of VR26 (1 MΩ B, WAVE FORM: the rise/fall ratio, saw to triangle to ramp),
  a Schmitt trigger (IC15), FREQ VR25 10 kΩ B in the feedback divider (R147 2.2 kΩ, R148 48 Ω).
  Outputs: triangle-ish 5 Vpp (J3) and rectangle 0 to +5 V (J7), LED1. Spec 0.1 to 20 Hz.
- **Noise (KLM-128):** a 2SC644R junction (TR3) biased through R31/R32, TR2 2SC1685 and IC4
  (4558) to WHITE (5 Vpp, J25) and a pink filter (C2 0.022 µF, R28 220 kΩ) to PINK (J19).
- **S&H (KLM-128):** CA3140 (IC1) holding on a K30A (GR) JFET, clock comparator IC2 (4558) with
  CLOCK IN (J5); spec ±5 V.
- **MVCA (KLM-128):** photocoupler HTV-P873 (an LED and a light-dependent resistor) in IC3's
  feedback (4558), driven by TR1 2SC1685 from CONTROL (J15) or EG1.
- **Control wheel** (10 kΩ B, centre click, ±5 V) and **momentary switch** (J31).
- **Phones amp:** two halves of a 4558 (R41–R47), 120 mW into 8 Ω (spec).
- **ESP (KLM-129):** SIGNAL IN (J1) with SIG LEVEL VR7 1 MΩ B into a preamp (IC7, 4558; its gain
  range to read in phase 4), then LOW CUT and HIGH CUT (the 4-gang 100 kΩ C pot, two second-order sections,
  IC5/IC6) to OUT/FILTER (J17); F-V converter (µPC339 IC2 comparators, TR2, 4069 IC4) to
  F-V CV OUT (J18) with CV ADJ VR4; envelope follower (IC1) to ENV OUT (J29); trigger detect with
  THRESHOLD LEVEL VR3 and an LED to TRIG OUT (J30). Because the MS-20 is Hz/V, the F-V output can
  drive the VCOs directly: pitch-to-voltage for an external signal.

### 1.5 Service-manual checks (§7) [V, scan A p. 10]

Each becomes a case in `tools/service/check.py`:

| § | Check | Value |
|---|---|---|
| 7-1 | Ripple on +15 V and −15 V | ≤ 2 mVpp (a supply model check, not audio) |
| 7-2 | VCO 1, VCO 2: C-1 to C-4 at 8', all other knobs 0, pulse wave | within ±2 cents; trims high ❶/❸, low ❷/❹ |
| 7-2e | Each C at 32', 16', 8', 4' | within ±10 cents |
| 7-3 | KBD CV OUT: C-3 = ½ C-4, C-1 = ½ C-2 (trims ❺ high, ❻ low) | exact halves |
| 7-4.1 | HPF: fc 5, HPF PEAK 10, LPF PEAK 0, VCO levels 0 | oscillates at 500 Hz at PHONES (trim ❶) |
| 7-4.2 | LPF: fc 5, LPF PEAK 10, HPF PEAK 0 | oscillates at 500 Hz (trim ❷) |

Specifications (§1) give further range checks: HPF cutoff 50 Hz to 5 kHz, LPF 50 Hz to 15 kHz,
portamento (the OCR garbles its maximum time), master tune ±100 cents, EG times, MG 0.1 to 20 Hz, output levels at each
jack (block diagram check points 1–33). [V for the ones quoted; OCR is poor on a few numbers.]

### 1.6 Recommendation (target unit)

Model **Rev 1 as the service manual draws it** (KLM-127/128/129, two KORG35), with the panel and
patch panel of the original keyboard MS-20. Rev 2 (KLM-307, LM13600) later as a switch in the bar,
like Korg's own Kit. Netlists for every block on the shared circuit solver (it already has BJTs,
JFETs, diodes, comparators and an OTA element); the VCO resets band-limited; the VCA, filters and
diode limiters at the rate their distortion needs (to measure against ngspice in phase 4).

## 2. The KORG35 module

### 2.1 What is public [V]

- **Korg's own drawing.** Yasuhiko Mori (Korg product planning) sent the module's circuit to
  Martin Czech in a letter dated 2000-12-05; the scan is `KORG35.pdf` (p. 2 letter, p. 3 circuit,
  p. 4 a test circuit). Inside the module: R1 47 kΩ (pin 1, cutoff CV), R2 680 Ω (pin 2), R3
  470 kΩ (pin 8, +V, to the common base node), R4 10 kΩ, R5 820 Ω, R6 10 kΩ (pin 6); Q1, Q2, Q3
  2SC1623; Q4 2SK94 (n-JFET); Q5 2SA812 (PNP). **Q1 is used only as its base-collector junction**
  (emitter open): a diode from the base node to the R1/R2 node. Pins: 3 Q2 collector (signal in),
  4 Q2 emitter and Q3 collector, 5 Q3 emitter and Q4 gate, 7 output (Q4's source through R5, Q5's
  collector), 8 +V (Q5's emitter, R3, R4; corrected in phase 4, `circuit/filters.md`). The
  capacitors and the gain stage are on the board. [V, p. 3]
- The test circuit (p. 4) drives the module with a 0.5 Vpp 100 Hz square and sketches the ringing
  step response it should give; a usable check, at the scale of a sketch [V].
- **Stinchcombe's Fig. 1** redraws the MS-10 filter with exactly these values and types; Tanaka's
  drawing agrees (his text says R2 900 Ω, against his own drawing and Korg's: a typo). Q2 and Q3
  are biased in **reverse saturation** by the base current through R3 and act as current-controlled
  resistors forming the two R's of a **Sallen-Key low-pass**; the board's 3.3 nF and 1 nF are its
  capacitors (C1 = 3 C2, so the equivalent R1 ≈ R2/3 keeps both poles together); Q4 (BF245A in his
  simulation) and Q5 buffer the output; the op amp with back-to-back diodes (270 kΩ / 4.7 kΩ on the
  MS-10, 270 kΩ / 4.7 kΩ on the MS-20) is the forward gain k1; the resonance divider (5.6 kΩ /
  220 Ω with the PEAK pot) is k2.
- **Terms.** Czech's page and the letter say the drawing is Korg's property, shown "for educational
  purposes only", and that Korg "will take legal measures on any attempt to draw economic advantage
  out of this data". The same values are in Stinchcombe (2006) and Tanaka, and a module can be
  measured, but the data came from Korg under that condition. Decided: use the values (§7).
- Results (all from his SPICE runs on the MS-10 values): **1 V/oct cutoff** (8 steps of 1 V from
  about 22 Hz to 4.9 kHz, 7.8 octaves); self-oscillation needs k1k2 ≈ 2⅓ with these unequal parts
  (not 3); **resonance is asymmetric** (ringing about 1.7 kHz on the positive half and 2 kHz on the
  negative, because the signal changes the transistors' currents); the diodes in the forward path
  distort **every** frequency as the level rises (the OTA version only distorts near cutoff); the
  **high-pass is 6 dB/oct**, not 12 (the input is fed into the lifted C2 end, a band-pass in
  parallel with a high-pass).
- Which parts sit inside the module and which on the board is not in Stinchcombe's text: to map
  from the KORG35 pin numbers on the MS-10 and MS-20 schematics and the gathered documents
  (`sources.md`).

### 2.2 What matters most audibly

The filters are the MS-20's sound: the screaming resonance, the asymmetric ringing and the
forward-path distortion. All three come from device behaviour (Q2/Q3 in reverse saturation, the
diodes, the JFET buffer), not from the linear transfer function. So the model must be the
transistor circuit, not a fitted Sallen-Key. The open points are the transistor parameters and
module matching: NEC's 2SC1623 sheet gives forward data only; no sheet anywhere gives reverse-mode
parameters (Stinchcombe's βR = 4.21 and βF = 206.7 come from a PSpice library model), and no
2SK94 sheet was found.

### 2.3 Recommendation

Netlist the KORG35 at transistor level from Korg's drawing, with the board parts from the service
manual. 2SC1623 from NEC's sheet plus a full Ebers-Moll/Gummel-Poon model whose reverse
parameters are stated as assumptions and bounded by the PSpice values; 2SK94 from the 2SK30A
family until a sheet turns up. Check against ngspice, then against Stinchcombe's published results
(1 V/oct, self-oscillation at k1k2 ≈ 2⅓, ringing 1.7 kHz/2 kHz, 6 dB/oct HPF) and Korg's test
sketch, before any recording. Fit only within data-sheet ranges; the 500 Hz self-oscillation trim
(§7-4) sets each filter's offset.

## 3. Firmware

### 3.1 None [V]

No CPU anywhere: the keyboard, trigger, envelope and hold logic are op-amp, transistor and CMOS
(4007, 4011, 4069) circuits. So there is no ROM to supply and no voice mode: the plugin is complete
without any user file. Artwork places stay (maker and model marks), as on every machine.

### 3.2 Recommendation

Model the keyboard, key-hold and trigger detector as circuits. MIDI drives the key contacts: every
held note closes its contact, and the ladder gives high-note priority by itself.

## 4. Reference

### 4.1 Free recordings of real units

30 YouTube recordings, 2026-11-14, in `private/ms20/reference/youtube/` (one folder per video:
original audio, 48 kHz WAV, info, description, chapters; SHA1s and the evidence for each unit's
revision in its `SOURCES.md`). The revision comes from the uploader's own words; "unknown" where
they say nothing. No serial number is given anywhere. Nothing was listened to yet.

| Group | Videos | Notes |
|---|---|---|
| Vintage **Rev 1** (KORG35), stated | YH3g01qU7O8, tIb5X9X_tPk, dxO5ywTETIY, 96N82NCbKlQ, OxL2eC2k19A, and one unit in 1MXo9aPZETA | YH3g01qU7O8: 15 min dry, the longest; 96N82NCbKlQ: dry sweeps; dxO5ywTETIY: level-matched against a mini; OxL2eC2k19A too clipped to measure |
| Vintage Rev 1 by the uploader's "no panel screw" rule | I6kLYRX8F2U (one of two units) | The screw rule is disputed (§1.2) |
| Vintage **Rev 2** (LM13600), stated | hmAQ4YKRAIU, Mp5OcXbQsrY (modded: VCO 1 PWM in, VCO 2 sync), the other unit in 1MXo9aPZETA | |
| Rev 1 against Rev 2, same patches | 1MXo9aPZETA ("no fx"), I6kLYRX8F2U | The two units sit at different left/right levels, so the audio can be split per unit |
| Vintage, filter unknown | g6OfcNTvJow (labelled LPF sweep 0:00–1:40, HPF 1:40–2:50, ring 2:50; modded), t8t17yEHHUQ, p41aRcIXPwA, 2d8UXDNJvkI (labelled waveforms, PW sweep, LFO, envelopes, pink vs white noise), uSMe8mzo3Ks (direct, dry), EyB1ZHhjA9w and oIil0WVC-Ko (full knob and patch settings in the description), Lp448w0LETo, 40jOD0qkvlg (EG2 sustain), YH0ueVofZ28, LSYk5fW8pVg, q112fV1aJN8 (against a mini) | Settings are stated only for EyB1/oIil |
| Version not stated | mmvkUlKoSGc (MK1 vs MK2 filters, exactly mono, very low noise), D4d3aUPinzQ, _dyU5oxpYZ4, h3iqDTnKCpk, bt217IdKJIM | |
| Reissues | SGrrY24YTh0 (MS-20 FS, both filters, the KORG35 "noise leak" with VCOs off, stated PEAK settings), WafNf7k0Xac (MS-20M, REV1 vs REV2), 6y8TFYeXVD0 and PBJAB3YJZTA (mini) | Korg's own hardware; flagged as reissues |

Not obtained: 4Rh3pC-8Viw and MwJk36KnB04 (MS-20 vs MS-50 filter test), both no longer available.
Listed but not downloaded: 3mRI0ercKt4 (13 min dry vintage), jmvzCeaDUIQ (MS-20 FS sweeps on each
filter at PEAK 10).

### 4.2 Measurements

No free measurement of a real Rev 1 unit was found: no filter response, VCO scope trace, envelope
time or noise spectrum. The nearest are Korg's KORG35 test sketch (§2.1), Tanaka's scope shots of
a built PUT core (§1.3), and DAFx-19's measurements of a built Rev 2 low-pass. The service
manual's own numbers (§1.5) and the block diagram's check-point levels are the specification.

### 4.3 Other models

- **KORG MS-20 (KORG Collection, "MS-20 V2"):** KORG's component-modelling (CMT) plugin, based on
  their 2004 Legacy Collection MS-20; free demo with a KORG ID. A vendor model: a sanity check like
  Roland's plugins for the 303, never the truth. Which filter revision it models is not stated [N].
- **KORG MS-20 mini (2013), MS-20 Kit, MS-20M, MS-20 FS:** Korg's own hardware reissues, by the
  original engineer per KORG; the mini is a smaller-scale circuit with the KORG35 design. Recordings
  of them count as extra units, flagged as reissues [N].

### 4.4 Recommendation

- **The circuit first, then the units:** service-manual checks and ngspice decide the model; the
  recordings decide nothing until the netlists pass.
- **Rev 1 units for the comparison:** YH3g01qU7O8 and 96N82NCbKlQ (dry, long), tIb5X9X_tPk, the
  Rev 1 half of 1MXo9aPZETA. dxO5ywTETIY ties a Rev 1 unit to a mini at matched levels, which makes
  the minis usable as further units.
- **Rev 1 against Rev 2 on one patch** (1MXo9aPZETA, I6kLYRX8F2U, WafNf7k0Xac) is the best test of
  what the KORG35 adds: the forward-path distortion and the asymmetric resonance.
- **Settings:** EyB1ZHhjA9w and oIil0WVC-Ko state every knob and patch cord, so they can be
  rebuilt and compared as scenes; their units' filter revision is unknown.
- **Isolated material** for per-event checks: the labelled waveforms, PW sweep, noise and envelope
  clips (t8t17yEHHUQ, p41aRcIXPwA, 2d8UXDNJvkI, g6OfcNTvJow) and the KORG35 noise leak on the FS
  (SGrrY24YTh0).
- **KORG's plugin** (free demo) as a sanity check only, like Roland's for the 303.

## 5. Size versus our machines

- Circuit size: about 200 resistors, 17 4558s, 6 FET-input op amps, 2 KORG35, 2 VCO cores; bigger
  than the SH-101, smaller than the 909. All of it is in one voice, so there is no voice-level
  threading; partitions (VCOs, filters, envelopes, ESP) can be jobs.
- New work: the patch panel (cables in the hardware view, normalled jacks), the ESP's audio input,
  and the KORG35 in reverse saturation.
- CPU: two transistor-level filters with diodes and the VCO cores at a raised rate are the cost;
  the 909's netlists are the yardstick.

## 6. Policy and name

- Recent-machines rule (MAME's old rule, adopted 2026-11-09): the MS-20 shipped in 1978 and left
  production in the early 1980s [N: 1983 per Wikipedia]; Korg's later reissues (mini 2013, Kit, M,
  FS) do not count.
- Name: `ko-mono-20` (maker ko, kind mono, model digits 20), code `K20m` by the formula.
  "MS-20" goes only in the description, with "Not affiliated with Korg."

## 7. Open questions

1. Decided 2026-11-14: the KORG35 is netlisted from the published part values, cited to
   Stinchcombe's study; Lemus is free and ships only values, never the drawing (§2.1). The
   alternatives were the monotron filter with fitted values, or asking Korg first.
2. 2SC1623 and 2SK94 model parameters, especially reverse-mode βR and saturation behaviour
   (no data sheet gives them).
3. The trigger detector's retrigger rule (legato vs. retrigger on a new key while one is held);
   key priority is high note from the ladder (§1.3), to confirm in a recording.
4. The 4011 ring-modulator logic and VCO 2's narrow pulse width.
5. The HTV-P873 photocoupler's response (LED current to resistance, time constants).
6. Which parts the circled resistors are (tempco or 1 % metal film) and the 2SC1583 pair's matching.
7. Rev 1 vs Rev 2 for each vintage recording.
8. Exact panel legends and the patch panel normalling (owner's manual, panel photos).

## 8. Data that would settle the open questions

- Reverse-mode curves of a 2SC1623 (any owner with a curve tracer), and a 2SK94 sheet (Q2).
- Data sheets for 2SC1583, 2SC1685, 2SC644 and the HTV-P873 photocoupler (Q5, Q6).
- Recordings of a known-revision unit with stated settings: filter sweeps at PEAK 10, single notes
  at known footages, EG timing (Q7).
