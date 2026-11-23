# Oberheim Xpander: research

Phase 0 and 1, 2026-11-12. Every file is listed with URL and SHA1 in `sources.md`; the files live
in the private folder (`private/`, linked by `tools/link-private.sh`). "SM p55" is the service
manual's printed page; crops and disassemblies cited below are private.

**Decided (2026-11-12):** target the Xpander with **main OS 1.2** and **voice OS 1.4**, voice
board **Rev D**; plugin **ob-poly-xp**, code **Oxpp** (but see section 11: the name may change
before release). The Matrix-12 (same voice boards, two of them, keyboard) is a later machine of
its own.

Marks: **[V]** verified (page, crop or file:line given), **[I]** inferred, **[N]** not checked.

## Summary

| Input | Where it stands | How close we can get |
|---|---|---|
| Circuit | **Public and complete for voice board Rev B**: two scans of the July 1984 service manual (theory, all schematics, parts lists, layouts, ECOs 601-615). No Rev C/D drawing exists in either scan (section 7). | Component level for every block outside the two CEM chips. Rev D = the drawing plus the ECOs plus the PW1/VCA swap the firmware itself detects. |
| CEM3374 (dual VCO), CEM3372 (mixer, 4-pole VCF, VCAs) | Two-page Curtis data sheets; one published analysis of the 3372 core (R. McClellan 2013); Alfa's AS3372E for comparison. No die shots. | Data-sheet chip models, as the Pro-One's CEM3340/3320. The 3372's resonance curve and large-signal behaviour are the biggest uncertainty. |
| Firmware | **Both CPUs dumped** (main 1.2, voice 1.4 and 1.6), SHA1s match MAME. MAME's driver (BSD-3) maps both boards but has no audio; its 6809 core (BSD-3) is ported here. | Exact: the real firmware runs, including auto-tune against the modelled voices. |
| Custom chips | None. | - |
| Reference | No vendor or endorsed model (Arturia Matrix-12 V and GForce Matrix-12 are third party). Free recordings: about 8 Xpanders and 4 Matrix-12s on YouTube, 220 CC0 single notes (section 9). None with full stated settings or from the voice outs. | Per-note comparisons against several units; filter-mode and FM checks from the Matrix-12 clips (same voice board). |

**Tier: 2.** Everything is public except the reference, which is our own work from recordings.

**What decides the build:**
- **Auto-tune is in the firmware.** The voice CPU measures each VCO, PW, resonance and VCF through
  an LM311 on the summed voices and an 8253 (section 5), and stores corrections in battery RAM.
  The model must give that comparator real signals, and the tune tables become saved state.
- **The board-revision check (section 6.5)** opens and closes the VCAs and counts comparator
  edges. A model whose closed VCA or comparator chatters makes voice 1.4 pick the wrong channel
  order (silent voices). The VCA cut-off and the LM311's offset must be modelled.
- **CVs:** one 14-bit DAC, time-multiplexed into 54 smoothing sample-and-holds written by the
  voice CPU every 9 ms. Modelled as switched RC, timed by the emulated bus writes.
- **CPU:** each voice is about a Pro-One voice (two VCOs, a chip filter solved per sample);
  six voices ≈ 60-75 % of a core at 48 kHz in the worst case, split across worker threads
  (section 10).

---

## 1. Documents

### 1.1 What the two scans are

| | Scan A (`service_a`, 24 MB) | Scan B (`service_b`, 10 MB) |
|---|---|---|
| Pages | 79 [V pdfinfo] | 77 [V pdfinfo] |
| Edition | "First edition July 1984, by Michel Doidic, Oberheim part number 950037" (A#2) [V] | Same text and TOC. B has no cover or title page [V B#1] |
| Image format | Colour JPEG. Text pages are ~650×1024 px. Each schematic foldout is one 2048×~1530 px image, and the theory figures are up to 2474 px [V pdfimages] | Greyscale JPEG at 200 dpi, all pages 845×1100 px. Each foldout is split into 2 half-pages [V pdfimages] |
| Schematic legibility | Every value I needed was readable at 2–3× zoom. Colour bleed is slight | Cleaner contrast at about the same effective resolution (a half-sheet of ~0.45 sheet width is 845 px wide; A gives the same area ~880 px). Good for cross-checks, e.g. `voice-s3-rx51-18k-scanA-over-scanB.png` |
| PCB artwork | Colour red/blue overlays for processor, pot, voice, display and power (A#50–54) | Greyscale, split in halves (B#70–77) |
| ECOs | 601–615 complete: 16 pages, 602 has 2 pages (A#64–79) | Only ECO 601 (B#69) |
| Defects | SM p37–38 are missing (TOC says Misc = p36 and Schematics = p39) | p36 is scanned three times (B#36–38), apparently to fill the p37–38 gap. `schematic/synthfool/.../notes.txt` says p37–38 are missing from every copy [V] |

**Verdict:** use A for schematic reading, ECOs and layouts. Use B as the second opinion on any marginal value.

### 1.2 Page map (printed page, A index, B index, content)

| SM page | A# | B# | Content |
|---|---|---|---|
| cover, title | 1–2 | — | cover photo; title page (edition, part no.) |
| TOC | 3 | 1 | table of contents |
| 2–5 | 4–7 | 2–5 | intro, mechanical, overview, boards |
| 6 | 8 | 6 | switching PSU theory (T1/T2, Q1 IRF531 at ~30 kHz, U4 PWM) |
| 7 | 9 | 7 | PSU schematic (excerpt of 1869) |
| 8 | 10 | 8 | dual-CPU interface (HALTREQ*/HALTDS/BA/HALTAKN*) |
| 9 | 11 | 9 | interface schematic (voice sheet 1 excerpt) |
| 10–11 | 12–13 | 10–11 | step encoders (LA226, 30 teeth) + schematic |
| 12–13 | 14–15 | 12–13 | VFD theory + driver schematic |
| 14–15 | 16–17 | 14–15 | gate/CV inputs + schematic |
| 16–17 | 18–19 | 16–17 | VCO temperature compensation + schematic |
| 18–19 | 20–21 | 18–19 | high-resolution DAC + schematic |
| 20 | 22 | 20 | VCO/VCF frequency tuning; PW calibration |
| 21 | 23 | 21 | resonance calibration ("for voice software version 1.1") |
| 22 | 24 | 22 | CEM3374 block diagram |
| 23 | 25 | 23 | CEM3372 block diagram |
| 24–25 | 26–27 | 24–25 | dynamic FM (MDAC) + schematic |
| 26–27 | 28–29 | 26–27 | multi-function VCF (pole-mix theory) + schematic and mode table |
| 28–29 | 30–31 | 28–29 | smoothing S&H + schematic |
| 30–31 | 32–33 | 30–31 | programmable panning + schematic |
| 32–35 | 34–37 | 32–35 | test and calibration |
| 36 | 38 | 36 (×3) | misc: software reset, version, UNTUNE |
| 39 | 39 | 39–40 | **processor board 1851 sheet 1/2** |
| 41 | 40 | 41–42 | processor board 1851 sheet 2/2 (LED2/HALTSET latches, MIDI, cassette, PUP, **noise**) |
| 43 | 41 | 43–44 | pot board 1859 sheet 1/2 (CV/pedal/ADC, gates, switch matrix) |
| 45 | 42 | 45–46 | pot board 1859 sheet 2/2 (encoders, LED1, master volume, outputs, voice-out jacks) |
| 47 | 43 | 47–48 | display board 1841 sheet 1/2 |
| 49 | 44 | 49–50 | display board 1841 sheet 2/2 (VFDs) |
| 51 | 45 | 51–52 | **voice board 1808 sheet 1/4**: CPU, ROM/RAM, 8253, decode, bus interface |
| 53 | 46 | 53–54 | **voice board 1808 sheet 2/4**: DAC, references, S&H address latch, latch decoders, RES S&H |
| 55 | 47 | 55–56 | **voice board 1808 sheet 3/4**: one voice (VCOs, FM, VCF/pole mix, VCA, S&Hs, latches) |
| 57 | 48 | 57–58 | **voice board 1808 sheet 4/4**: panning, OSC comparator, outputs |
| 59 | 49 | 59 | power supply 1869 |
| (unnumbered) | 50–54 | 70–77 | PCB artwork: processor, pot, voice, display, PSU |
| 60–68 | 55–63 | 60–68 | part-number cross reference (board by board, with quantities) |
| 69–(…) | 64–79 | 69 | ECOs (B has only 601) |

### 1.3 Drawings and revisions shown

Every schematic title block has an **empty revision box**. All are drawn by "J.R.". There are no dates in the title blocks [V title-block crops of A#39–49].

The PCB artwork carries letters:

| Board | Schematic | Artwork / layout (as printed on the artwork) | ECO references |
|---|---|---|---|
| Processor | 1851 (2 sheets) | 1852/**1853 B** (A#50) [V] | — |
| Pot | 1859 (2 sheets) | 1861/**1862 D** (A#51) [V] | parts layout 1866D |
| Display | 1841 (2 sheets) | **1844 A** (A#53) [V] | — |
| Voice | 1808 (4 sheets) | top **1810B**, bottom 1809B, silkscreen **1812B**, PCB no. 651808 (mirrored) (A#52, `voice-layout-artwork-revision-labels.png`) [V] | parts layout 1813B |
| Power | 1869 | **1871B** (A#54) [V] | — |

**The manual documents voice board revision B, not D.** [V artwork. I: "Rev D" in MAME means a later artwork letter.]

- The parts lists still give the ROMs as XP 1.0 / CA 1.0 (SM p60, p65) [V].
- The drawings are pre-ECO: RX62 and RX70 are 10K, RX64 is 27K, R186 sits on voice 1's line, and the 422K of ECO 609 is absent.
- The parts list is partly post-ECO: 33K appears ×12, i.e. ECO 602 applied. But 27K is still ×6 (603 not applied) and 422K is ×6 (609 not applied) (SM p66) [V].

### 1.4 ECOs (A#64–79) [V]

| ECO | Date | Board | Change | Reason |
|---|---|---|---|---|
| 601 | 6/6/84 | pot 1859 s1 | remove R75, R76 (47K pedal pull-downs) | pedal reads max when unplugged |
| 602 | 6/29/84 | voice ("1814"[sic] s3,4; layout 1813B) | 12 trace cuts plus jumpers; **RX62 and RX70 10K → 33K** (×6 voices); **R186 10K → 4.7K, moved** from voice 1's line to the LM311 input node (drawing `eco602-p2-rx62-rx70-r186-change.png`); "cut two places" at UX16/VCAOUT [N: exact cut] | final-VCA distortion, VCA bypass |
| 603 | 7/30/84 | voice ("1841"[sic] s1,3) | **RX64 27K → 20K**; voice software 1.1 | more reliable filter oscillation, any brand of 4053 (UX14), more accurate break point |
| 604 | 8/3/84 | pot | filter the ADC +5 V (47 µF); R47 12.1K → 11.3K; R48 15K → 82K | ADC stability, T1 range |
| 605 | 8/3/84 | voice s2,3 | **double the trace from U813 pin 6 (DAC out) to U206 pin 3 (S&H of voice 6)** with a jumper | voices 5 and 6 going sharp |
| 606 | 8/3/84 | voice s3 | 0.1 µF across the +5 V of **U204** | VCO2 glitches on voices 5 and 6 |
| 607 | 7/9/84 | pot | resistor C17 → −5: 100K → 220K | missed short trigger pulses |
| 608 | 8/15/84 | PSU 1869 | R8 3.9K → 1.5K, R23 47K → 22K, C12 0.047 → 0.47 µF | PSU loop stability |
| 609 | 8/22/84 | voice s2 | voice software 1.2; **422K 1% from U813 pin 2 (DAC summing node) to U815 pin 7 (REF)** | spurious PW-tune failures; "bleeding of VCA 1 and 2 (VCO's VCAs)" |
| 610 | 8/3/84 | pot | cancel items 2 and 3 of 604 (R47 back to 12.1K, R48 back to 15K) | — |
| 611 | 8/30/84 | pot | two 1N4148s on the ADC input | ADC input protection |
| 612 | 8/30/84 | pot | 0.001 µF ADC input to ground (C15 location). The form is crossed out and annotated "See 615" | ADC stability |
| 613 | 9/4/84 | pot | cap from 604: 47 µF → 0.1 µF mylar | ground-loop noise on audio |
| 614 | 9/17/84 | voice | 0.1 µF from U205 pin 3 (ground) to pin 8 (+12) | beating between VCO1 and VCO2 of voice 6 |
| 615 | 10/1/84 | pot | remove the cap added by 612 | pedal 1/2 interaction |

---

## 2. Boards and signal flow

| Board | Contents |
|---|---|
| Processor (1851) | Main 68B09 U9 (EXTAL 8 MHz) [V SM p39]. 16 MHz discrete crystal oscillator (Q1 2N3904, X1) → LS393 U10: 8 MHz to both CPUs, 500 kHz to ACIA/ADC, 250 kHz to the 40103 [V p39; x.cpp:508,515,638]. ROM U8/U7/U6/U5 (2764). Battery RAM U2/U3/U4 (6264). 40103 FIRQ timer U12. 6850 MIDI U26 + 6N138. Cassette in/out. PUP power-fail detector (drives RESET and gates RAM CS). LED2 latch U24. HALTSET latch U25. **Noise generator U29** [V p39, p41] |
| Pot (1859) | 8×8 button matrix (U16 LS42, U24 HC244). 6 LA226 encoders with LS74 direction/step flip-flops. CV1–6 + 2 pedals → 4051 U17 → ADC0804 U20. 6 gates + chain advance + trigger → CA3081 U22/Q1 → LS244 U23, with pull polarity from U21 (PULL1–7). LED1 latch U14. Master volume (50K dual) and the U1 TL072 output buffers. L/R/MONO jacks and VOICE1–6 jacks (switched) [V p43, p45] |
| Display (1841) | 3 × FG405A2 VFD (40 chars × 16 segments). 6 × 74LS374 anode latches. 74LS42 + 5 × 4028 grid decode. 11 × UDN6118 HV drivers. Trimmers T1 grid V and T2–T4 brightness (A#53) [V p47–49, p64] |
| Voice (1808) | Voice 68B09 U907, U912/U914 ROM, U909 spare socket, U917 6264. 8253 U921. Bus bridge U901–U904/U916. HS3140 14-bit DAC U811 with reference/HRES circuit. 3 × 74LS42 latch decoders. 54 S&Hs. Per voice: CEM3374, CEM3372, AD7523, 4016, 4053, 3 × 4051, op-amps. Pan 4051s U102–U107. LM311 U108 (OSC). Output summers U101 [V p51–57] |
| Power (1869) | Off-line 13 V → 30 kHz switcher (Q1 IRF531, U4 NE5561) → T2 → +12 (7812), −12 (7912), −5 (7905), +5, +55 V (VFD), 9 VAC filament [V p6–7, p68] |

Signal flow:
1. MIDI and the front panel go to the main CPU.
2. The main CPU halts the voice CPU and writes parameters into voice RAM.
3. The voice CPU computes EGs, LFOs, lag and so on, and writes 54 CVs through the DAC/S&Hs and 18 latches every IRQ.
4. Each voice: CEM3374 → waveform switch network → CEM3372 mixer/VCF → pole-mix network → 3372 VCA → I/V converter → VOICE X.
5. VOICE X goes to two places: a pan 4051 (L bus, R bus or individual out), and through 10K to the LM311 that produces OSC for auto-tune.
6. The L and R buses go to U101 summers → master volume → output buffers.

---

## 3. Voice circuits (voice board 1808, sheet 3 = SM p55 unless noted)

### 3.0 Designators and voice numbering

- **[V]** The output section uses U101–U108, R101–R186 and C101–C107 (SM p57). The per-voice columns on the silkscreen are U2xx–U7xx (A#52). So X runs from 2 to 7.
- **[V]** ECO 605 calls U206 "S&H of voice 6". ECO 606 puts its fix at U204 "voices 5 and 6". ECO 614 puts its fix at U205 "voice 6".
- **[I]** So U2xx is voice 6, and probably X = 8 − voice (U7xx = voice 1).
- MAME's note "UX03 refers to U103 for voice 1" (vb.cpp:62–64) is wrong, since U103 is a pan mux.
- MAME's per-voice `latch*_w` decoding (`m_voices[5 - offset]`, vb.cpp:700–720) and the U804 outputs 0–5 = L1V6*..L1V1* (SM p53) agree [V].

### 3.1 VCOs: CEM3374 UX09

CEM3374 pin mapping, cross-checked against `datasheets/CEM3374_sdiy.pdf`, now in `private/docs/datasheets/` [V both]:

| Xpander net | 3374 pin (datasheet name) | Support parts |
|---|---|---|
| VCO1 exp CV | 12 (Freq **B** cntl) | VCOF1 → UX08 buffer (TL084 pins 12/13/14) → RX26 4.22K 1% → pin. RX27 100 Ω 1% and CX29 0.1 µF to ground. Gain 100/4320 = 0.02315 (τ ≈ 9.8 µs) |
| VCO1 IREF / linear FM | 11 (IREF B) | RX29 806K 1% from +4REF (≈4.95 µA). RX30 470 Ω + CX30 0.01 µF in series to ground. FM via CX27 0.1 µF + RX28 422K 1% |
| VCO1 timing cap | 13 (C_FB) | CX28 1000 pF poly to node (RX25 1.2K ∥ CX26 0.01 µF to ground). This is the datasheet's HF-scale trim network, fixed here |
| VCO1 tri / saw | 14 / 17 | tri 0–4 V, saw 0–8 V as drawn. Saw is made from tri by the internal tri→saw converter |
| VCO1 sync in | 16 (Sync **B**, "3340 type", reversing) | **not connected** |
| VCO2 exp CV | 7 (Freq **A**) | VCOF2 → UX08 (pins 8/9/10) → RX21 4.22K 1%. RX22 100 Ω 1%, CX24 0.1 µF |
| VCO2 IREF | 8 (IREF A) | RX23 806K 1%, RX24 470 Ω + CX25 0.01 µF. No FM input |
| VCO2 timing cap | 6 (C_FA) | CX22 1000 pF poly, RX20 1.2K ∥ CX20 0.01 µF |
| VCO2 tri / saw | 5 / 2 | tri 0–4 V, saw 0–8 V |
| VCO2 sync in | 3 (Sync **A**, standard: "triangle and sawtooth immediately reset to zero") | from the sync network below |
| V_TH A/B | 4, 15 | +4REF (sets the 4 V triangle top) |
| Temp sensor | 10 | → UX05B (TL082 follower, pins 5/6/7) with CX35 2.2 µF → "Tx" → U805 (DAC ref mux) |
| Supplies | 18 +12, 1 −5, 9 GND | — |

Waveform switching. UX10 is a 4016 with VDD +5A (pin 14) and **VSS = −0.6 V** (pin 7, from R843 1K and D801, SM p53) [V].

| Waveform | Path to the mix bus [V] |
|---|---|
| TRI1 | pin 14 → UX10 11→10 (control 12 = TRI1) → RX42 47K → VCO1 bus |
| SAW1 | pin 17 → RX45 100K → node (RX44 10K to ground) → UX10 9→8 (control 6 = SAW1) → node (RX46 47K to ground) → RX47 3.3K → VCO1 bus |
| TRI2 | pin 5 → UX10 1→2 (control 13) → RX31 47K → VCO2 bus |
| SAW2 | pin 2 → RX33 100K, RX32 10K → UX10 3→4 (control 5) → RX34 47K, RX35 3.3K → VCO2 bus |

Saw → pulse [V `voice-s3-vco-cem3374.png`]:
- Ladder: VCO1 saw → RX49 100K 1% → node a → RX50 100K 1% → GND → RX39 100K 1% → node b → RX38 100K 1% → VCO2 saw. So node a = saw1/2 and node b = saw2/2, both 0–4 V.
- UX11 is a TL082 run open-loop as a comparator on ±12 V.
  - PW1 goes to pin 5 (+), node a to pin 6 (−). The output, pin 7, goes through DX05 and RX48 360K to the VCO1 bus.
  - PW2 goes to pin 3 (+), node b to pin 2 (−). The output, pin 1, goes through DX04 and RX37 360K to the VCO2 bus.
- The output is high while PW > saw/2. So high-time fraction = PW/4 V [I]. The diode passes only the positive state.
- There is **no pulse on/off switch**. Pulse is "off" when the PW CV is ≤0 or ≥4 V (the firmware writes PW = 0 at reset) [I, vb ROM C913].

Sync [V]:
- VCO1 saw → RX19 10K → node → RX18 10K → GND, so the node carries saw1/2.
- The node also connects to the anode of DX03, whose cathode goes to the latch bit SYNC. When SYNC = 0 the diode clamps the node to ≈0.6 V, so the 4 V falling step disappears.
- The node then goes through CX21 100 pF to pin 3 (Sync A, 50K input impedance, threshold −1.0 to −1.4 V per the datasheet).
- **Result: VCO2 is hard-reset by VCO1's saw edge.** [V schematic + datasheet]

Mix buses and the CEM3372 inputs [V `voice-s3-cem3372-top.png`]:
- The VCO1 bus goes through CX37 2.2 µF to 3372 pin 6 (INA), with RX56 680 Ω from pin 6 to ground.
- The VCO2 bus goes through CX33 2.2 µF to pin 7 (INB), with RX40 680 Ω to ground.
- Noise also joins the VCO2 bus. It comes from the NOISE net (processor board) → RX61 3.3K → UX14a (4053: common pin 14, ay pin 13 = bus, ax pin 12 = ground, control pin 11 = NOI). The drawing annotates RX61's input as "100 mVpTop".
- Rough levels into 680 Ω [I, resistor arithmetic ignoring the 4016 Ron and loading by the other paths]: tri ≈ 57 mV p-p; saw ≈ 33 mV p-p (after the 100K/10K pre-divider, 47K shunt, 3.3K); pulse ≈ 20 mV; noise ≈ 17 mV p-p. The CEM3372 datasheet gives 75 mV p-p for 5 % THD.
- The coupling-cap corners include the source resistance: 1/(2π(R_src + 680)·2.2 µF) ≈ 1.5–7 Hz [I].

FM: AD7523 MDAC [V `voice-s3-fm-mdac-ad7523.png`, SM p24]:
- VCO2 triangle (pin 5) → CX02 2.2 µF → UX04 pin 15 (VREF).
- MDAC data comes from the UX03 74LS374 (clocked by L1VX*). BD0..BD7 map straight to the LSB..MSB pins 11..4 (the pin scramble is only through the '374 pinout).
- UX04 pins 1 and 2 go to UX05A (TL082). Pin 16 (RFB) is the feedback, with CX03 "300 pF" across it (the parts list says 330 pF ×7) [V SM p65]. Vout = −V_tri,ac · D/256.
- UX05A pin 1 → UX14c (common pin 4, control pin 9 = VCOFM):
  - **VCOFM = 1** → cy (pin 3) → node with RX65 10K to ground → CX27 0.1 µF + RX28 422K → **VCO1 IREF (pin 11): linear FM**. At full scale it gives ±2 V/422K = ±4.7 µA against a 4.95 µA reference [I].
  - **VCOFM = 0** → cx (pin 5) → node with RX41 10K to ground → CX34 0.1 µF → VCFFM → RX55 39K → 3372 FREQ (pin 15). That is about ±49 mV, ≈ ±2.5 octaves exponential at full scale [I].
- FM routing is always to one destination or the other. The amount is the MDAC code.

Volume CVs: VOLA → 3372 pin 5 (LEVELA) and VOLB → pin 8 (LEVELB), directly from their S&Hs (high-Z, unbuffered) [V]. Per the datasheet these are audio-taper VCAs, 0–35× [V CEM3372 datasheet].

Latch UX02 (74C174, clock L2VX*), from the D→Q pairs on the drawing [V `voice-s3-latches-ux01-ux02.png`]:
- bit0 VCOFM
- bit1 SAW2
- bit2 TRI1
- bit3 SYNC
- bit4 TRI2
- bit5 SAW1

### 3.2 Noise (processor board, SM p41) [V `processor-s2-noise-generator.png`]

- Source: Q3 (MPS5172 "SELECTED"), a reverse-biased B-E junction. The emitter is fed from +12 V through R42 100K; base and collector go to ground.
- The noise is coupled by C27 0.1 µF into the inverting input (pin 2) of U29 (LM301), which has R45 100K from pin 2 to ground.
- Feedback is a T-network: R46 4.7M + R43 4.7M, with C28 0.01 µF from the middle to ground. With the capacitor input, gain rises with frequency.
- U29 output → R41 10K → NOISE node, with R44 47 Ω and C26 330 pF to ground.
- [I] The op-amp output must be clipped near ±11 V. 11 V × 47/10047 ≈ ±51 mV matches the "100 mV pTop" annotation. So the noise is clipped, near-binary noise whose spectrum is shaped by the LM301 slew rate and bandwidth. No compensation cap is visible [N: pins 1 and 8 of the LM301].
- The NOISE net goes to voice-board connector B1, then to every voice's RX61 3.3K. Source impedance is ≈47 Ω [V SM p51, p55].

### 3.3 VCF and mixer: CEM3372 UX12 [V `voice-s3-cem3372-top/bottom.png`]

| Pin | Net / parts |
|---|---|
| 18 / 16 / 9 | +12 / −5 / GND |
| 6, 7 | INA, INB (above) |
| 5, 8 | LEVELA = VOLA, LEVELB = VOLB |
| 15 FREQ | RX53 38.3K 1% from VCFF (UX08 buffer, pins 5/6/7); RX52 196K 1% from −5 V; RX54 1K 1% to ground; RX55 39K from VCFFM (AC-coupled, so no DC effect). **FREQ = 0.02532·VCFF − 0.02474 V** [I arithmetic]. At 19 mV/oct (datasheet) that is **0.750 V/oct**. VCFF −4.93…+5.0 V gives FREQ −150…+102 mV (datasheet range −155…+110 mV) |
| 10 RES | RES S&H (sheet 2, below), unbuffered |
| 11 RESIN | VCFOUT → CX48 2.2 µF → RX64 → pin 11, with RX63 1.2K to ground. **RX64 = 27K as drawn, 20K after ECO 603** (datasheet suggests 22K/1K) |
| 4 CELL1 (C_A) | UX14b (4053, common pin 15, control pin 10 = **FD**): bx (pin 2) → **CX46 0.033 µF 5 %**; by (pin 1) → **CX45 100 pF**. FD=1 effectively removes cell 1 |
| 3 CELL2 | CX41 0.033 µF 5 % |
| 2 CELL3 | CX42 0.033 µF 5 % |
| 1 CELL4 (C_D) | CX43 330 pF mica (datasheet: cell 4 = C/100 because the last stage has ×100 gain) |
| 17 VCFOUT | → CX48 2.2 µF → RX68 100K 1% → UX15B (TL082 inverting, RX67 100K 1%) and → RX64 (resonance) |
| 13 VCA LEVEL | VCA S&H → UX08 buffer (pins 1/2/3) → **RX51 18K** → pin, with **CX40 0.1 µF** to ground (τ = 1.8 ms, 88 Hz) |
| 12 VCAIN | pole-mix output → CX47 2.2 µF → RX62 (10K drawn, **33K after ECO 602**) → pin (current input held at −1 V) |
| 14 VCAOUT | → UX16 TL081 inverting I/V, RX70 (10K drawn, **33K after ECO 602**) → CX49 2.2 µF → VOICE X |

Cell buffers, from the cap voltages [V]:

| Stage | Buffer | Gain on cap voltage |
|---|---|---|
| s1 | CELL1 → UX13B (pin 5+, RX80 100K 1% / RX79 1K 1%), output pin 7 | +101 |
| s2 | CELL2 → UX13D (pin 12+, RX60 100K / RX59 1K), output pin 14 → RX77 10K 1% → UX13C (RX78 10K 1%), output pin 8 | **−101** |
| s3 | CELL3 → UX13A (pin 3+, RX57 100K / RX58 1K), output pin 1 | +101 |
| s4 | VCFOUT (= ×100 of the cap-4 voltage per datasheet and MAME `cem3372.cpp`) → UX15B, output pin 7 | **−100** |

**Pole-mix network.** UX17 is a CD4051: VDD +5A, VEE −12, INH grounded. Address A = FA (pin 11), B = FB (pin 10), C = FC (pin 9). The common, pin 3, goes to the summing node of UX15A (TL082, pin 2) with RX66 68K feedback. RX69 10K runs from the summing node to ground (it only affects noise gain).

Each channel input joins a group of 1 % resistors from the stage buses. I traced every resistor to its bus at 3× zoom in scan A and confirmed the ambiguous junctions [V `voice-s3-pole-mix-cd4051.png`, `voice-s3-pole-mix-buses-top-zoom.png`, `voice-s3-pole-mix-buses-bottom-zoom.png`, `voice-s3-pole-mix-stage3-not-joined-to-stage1-bus.png`, `voice-s3-pole-mix-stage1-vs-stage2-bus-bottom.png`]:

| 4051 ch (pin) | s1 (+101) | s2 (−101) | s3 (+101) | s4 (−VCFOUT) | Label, FD high (FD low in brackets) |
|---|---|---|---|---|---|
| 0 (13) | — | RX89 100K | RX90 49.9K | RX91 100K | 2PHP+1PLP (4P BP) |
| 1 (14) | RX88 100K | RX87 33.2K | RX86 33.2K | RX85 100K | 3P HP (3PHP+1PLP) |
| 2 (15) | RX81 100K | RX82 33.2K | RX83 16.5K | RX84 24.9K | 3P AP (3PAP+1PLP) |
| 3 (12) | RX93 100K | RX92 49.9K | RX94 49.9K | — | 2P N (2PN+1PLP) |
| 4 (1) | — | RX75 62K (5 %) | — | — | 1P LP (2P LP) |
| 5 (5) | RX73 100K | RX74 49.9K | RX72 100K | — | 2P HP (2PHP+1PLP) |
| 6 (2) | — | — | — | RX76 56K (5 %) | 3P LP (4P LP) |
| 7 (4) | RX95 100K | RX71 100K | — | — | 1P HP (2P BP) |

This matches MAME `R_MIX` exactly (vb.cpp:402–416) and the stage gains in vb.cpp:384–398 [V]. The parts-list quantities agree: 100K 1 % ×114 (19 per voice), 49.9K ×24, 33.2K ×18, 16.5K ×6, 24.9K ×6, 62K, 56K ×18 (12 of them pan + 6 RX76) (SM p65–67) [V].

**The 16 modes.** Mode = FD·8 + FC·4 + FB·2 + FA. The schematic's table (`voice-s3-filter-mode-table.png`) is identical to MAME's list (vb.cpp:359–376) [V].

Write L = 1/(1+s) (normalised). Taps: FD=0 gives s1..s4 = L..L⁴; FD=1 gives s1..s4 = 1, L, L², L³ because cell 1 is ≈ bypassed. The coefficients below are pre-summer and normalised to 101/100K [I, computed by `private/docs/tools/polemix.py` from the [V] values]:

| ch | Coefficients (s1, s2, s3, s4) | Response (FD=1) | Response (FD=0) | Passband gain at UX15A output, per unit cap-1-equivalent input |
|---|---|---|---|---|
| 0 | 0, −1, +2.004, −0.990 | −L(1−L)² = 2PHP·1PLP | −L²(1−L)² = 4P BP | 68.7 (BP peaks 24.6 / 17.4) |
| 1 | +1, −3.012, +3.012, −0.990 | (1−L)³ = 3P HP | L(1−L)³ | 68.7 |
| 2 | +1, −3.012, +6.061, −3.976 | **1 − 3L + 6L² − 4L³ = ½·(1 − AP₃)**, AP₃ = ((1−s)/(1+s))³ | L·(same) | 68.7 HF |
| 3 | +1, −2.004, +2.004, 0 | (1+s²)/(1+s)²: notch at ω=1 | ·L | 68.7 |
| 4 | 0, −1.613, 0, 0 | −1.61·L | −1.61·L² | **110.8** |
| 5 | +1, −2.004, +1, 0 | (1−L)² = 2P HP | L(1−L)² | 68.7 |
| 6 | 0, 0, 0, −1.768 | −1.77·L³ | −1.77·L⁴ | **121.4** |
| 7 | +1, −1, 0, 0 | 1−L = 1P HP | L(1−L) = 2P BP | 68.7 (BP peak 34.3) |

Findings from this:
- **The "3 POLE PHASE / 3P AP" modes are not flat all-pass.** As built, the response is (dry − 3-pole all-pass)/2: |H| is zero at DC, has a notch at ω = √3·fc, and rises to unity at HF. That is a one-notch phaser [I, exact algebra on the [V] values].
- **LP modes are 1.61× (2P/1P LP) and 1.77× (4P/3P LP) louder** than the HP/BP/notch modes.
- **ch0 (and ch4 and ch6) invert polarity** relative to ch1, 2, 3, 5 and 7.
- Modes 0x5 and 0x8 are both 2PHP+1PLP but with opposite polarity.
- These responses assume the 3372 cells are mutually non-inverting. That is the only polarity under which the designer's labels hold [I].
- **Resonance with FD=1:** the loop still runs VCFOUT → RESIN around all four cells, but cell 1 is now a ~330× higher pole. So the oscillation loop is effectively 3-pole: it oscillates at about √3·fc and needs ≈2× the loop gain (8 against 4) [I]. Whether the firmware compensates is [N].

Inconsistencies in the service manual:
- The theory text says the cell-1 switch "S1 is implemented by one cell of the analog switch UX15" (SM p27). It is UX14b (4053) on the schematic [V].
- The theory text calls b4 "the op amp UX15 (output pin 7)" and the summer "the second cell of UX15 (pin 1)". Those agree with the schematic [V].

### 3.4 VCA and I/V

- 3372 final VCA: current in and current out, linear CV, max gain 0.93 at 5 V, shut-off ≈ +85 mV, ±200 µA maximum recommended input [V datasheet].
- Input: pole-mix output (UX15A pin 1) → CX47 → RX62 33K. Output: RX70 33K I/V (UX16), so the path is ≈ unity at full CV [I].
- Coupling corners [I]: CX47·RX62 = 2.2 Hz; CX48·(RX68 ∥ (RX64+RX63)) ≈ 4.1 Hz; CX49 into the pan load (10–20K) ≈ 4–7 Hz.

### 3.5 Panning, individual outs, main outs (sheet 4 = SM p57; pot board SM p45)

Pan muxes U102–U107 (4051 per voice; VDD +5, VEE −12): IN (pin 3) = VOICE n, A/B/C = PAn/PBn/PCn (UX01 bits 5, 6, 7), INH (pin 6) = **MUTE** [V]. Channel resistors (voice 1 shown; all voices identical) [V `voice-s4-pan-4051-u102-u105.png`]:

| ch | to L bus | to R bus | L gain (−10K/R) | R gain |
|---|---|---|---|---|
| 0 | 10K | — | −1.00 | 0 |
| 1 | 12K | 56K | −0.83 | −0.18 |
| 2 | 15K | 30K | −0.67 | −0.33 |
| 3 | 20K | 20K | −0.50 | −0.50 |
| 4 | 30K | 15K | −0.33 | −0.67 |
| 5 | 56K | 12K | −0.18 | −0.83 |
| 6 | — | 10K | 0 | −1.00 |
| 7 | individual out only (V n) | | | |

This is a linear (equal-sum) pan law and matches MAME `update_pan` (vb.cpp:449–501) [V].

**A voice routed to its individual out (ch 7) is absent from L/R, and vice versa** [V]. The individual out is V n → R8–R13 10K on the pot board → VOICE n jack. It is unbuffered. The jack switches are chained to ground (SM p45) [V] and do not appear to be sensed [I].

Main outs [V `voice-s4-output-summers-u101.png`]:
- L bus → U101A (TL072, pin 2, R101 10K ∥ C106 100 pF). R bus → U101B (R103 10K ∥ C107 100 pF).
- The CASSETTE net is injected into both summing nodes through R102 and R104 22K, with R105 680 Ω to ground. This is a tape monitor [I].
- U101 → pot-board 50K dual master volume → U1 TL072 followers → R2 1K → LEFT and R6 1K → RIGHT. R3 and R5 (1K each) from the two outputs meet at MONO, so MONO = (L+R)/2 [V SM p45, p57].

All signal 4051s (pan, S&H, pole-mix) have VDD = +5 V, so **any signal above ≈ +5.6 V is clamped by their input diodes** [I].

### 3.6 Where MAME differs from the schematic

| Item | MAME | Schematic / evidence |
|---|---|---|
| UX02 bit 1 vs bit 5 | `latch1_w` assigns `m_saw2` for both bit 1 and bit 5. SAW1 is never set (vb.cpp:244–249) | bit 5 = SAW1, bit 1 = SAW2 [V] |
| VCA CV RC | RX51 = 10K, "~159 Hz" (vb.cpp:186–187) | **RX51 = 18K** in both scans and in the parts list (18K ×6) → 88 Hz [V] |
| X ↔ voice | "UX03 = U103 for voice 1" (vb.cpp:62) | X = 2..7; U2xx = voice 6 per ECOs [V/I] |
| S&H channel order | Rev D: PW1 = 0, VCA = 1 (vb.h:53–66) | Manual (Rev B): **VCA = ch0, PW1 = ch1** (`voice-s3-sh-ch0-vca-ch1-pw1.png`). This is consistent with MAME's "earlier revisions" note [V] |
| ECO 609 offset | absent | 422K adds −0.0237·REF to every CV (Rev D: presumably present [I]) |
| DAC scale | `/16383` (vb.cpp:724) | an MDAC is code/16384 [I] |
| Channel-B DC block | R = RX40 680 Ω only (106 Hz) (vb.cpp:131–132, 341) | the source resistors are in series with the coupling cap, so the true corner is ≈1.5–7 Hz [I] |
| VCF FREQ | ignores RX55 39K | RX55 is AC-coupled (CX34), so DC is unaffected; MAME's DC is fine [V] |
| AUTO*/MUTE | not implemented (vb.cpp:696) | MUTE = NOT(AUTO* · RESET*) → pan 4051 INH (SM p51) [V] |
| PIT CLK0, OSC | TODO; DATAIN OSC reads 1 (vb.cpp:573, 677) | CLK0 = OSC (LM311 output, R195 1K pull-up) [V] |
| ROM chips | "3 x 6264 (U909, U912, U914)" (vb.cpp:635) | 2764 EPROMs [V SM p51] (typo) |
| VCOs, ch-A audio, sync, FM | unimplemented (vb.cpp:118–123, 144) | — |
| ADC full scale | 0–5.0 V → 0–255 (x.cpp:286–291) | VR/2 is calibrated to **2.667 V**, i.e. 5.333 V full scale (SM p35) [V]. Only the top 6 bits are used (SM p14), so 64 steps × 83.3 mV = semitones at 1 V/oct [I] |

---

## 4. Control voltages (voice sheet 2 = SM p53)

### 4.1 DAC write mechanics [V `voice-s2-dac-address-latch-u806-u803.png`, `voice-s2-dac-hs3140-hires.png`]

The DAC lives in CPU space $8C00–$8DFF (mirror $8E00) through U918 output 3 → SDAC.

**Write 1 (A0 = 0 → CLEAR*):**
- Clears U806 (74LS273), which deselects every S&H.
- Latches D6..D0 into U810 (74LS374) as DAC bits 1–7 (MSB first).
- Bit 7 is latched as the FAST flag. FAST* = NAND(latched bit 7, A0) (U807, SM p53) [V]. A0 is the live address line, so FAST* is not limited to the enable write: while the S&H stays selected (until the next CLEAR*), it is low on every bus cycle with A0 high, the odd addresses and the $FFFF dummy cycles [I].

**Write 2 (A0 = 1 → VOICEN*):**
- Latches D7..D1 into U809 as DAC bits 8–14. D0 is not connected.
- Latches A1..A8 into U806:

| U806 output | Signal |
|---|---|
| A1–A3 | SHA/SHB/SHC (CV index) |
| A4–A6 | U803 (74C42) voice select 1–6 → SH1*..SH6*; 7 → SH7* (RES mux U816); 0 → none. Also U805 ref-mux address |
| A7 | FTSH (also drives U803 D, which disables every S&H) |
| A8 | HRES* |

- A 6809 `STD $8Dxx` performs both writes in order [V vb ROM, e.g. C8E9].
- The S&H stays selected from the VOICEN* write until the **next** CLEAR* write. Acquisition time is set by the firmware: it pads with `PSHS/PULS` pairs between DAC writes [V vb ROM CB58–CB9A].

### 4.2 Analog chain

**DAC.** U811 is an HS3140C-4/MP7614KN (14-bit current MDAC, ref pin 19). U812 (TL081) is the I/V stage with C804 30 pF, T801 100K offset trim and R847 1.5K to −12 V.

**Summer.** U812 output → R846 10K 1% → U813 (TL081, R845 10K 1% ∥ C803 30 pF) → **DAC net**. The DAC net is clamped at ≈ +5.0 V by D803 into a node fed from +5 V via D802 and held by R844 150 Ω and C802 100 µF. This protects the +5 V 4051s [V; function I].

**References.**
- 4VREF = U815C: D805 LM329 6.9 V, R857 10.7K 1%, R854 6.19K 1%, C809 0.1 µF. 6.9 × 6.19/10.7 = **3.99 V** (service check 3.920–4.080 V).
- R853 18.2K / R855 10K (C807 15 µF) → 1.416 V → U815A follower.
- The follower feeds U805 (4051) ch7 and U814c cy.
- U805 ch1–6 = T1..T6 (each voice's CEM3374 sensor, ≈ 2.5 V); ch0 is not connected. INH = HRES*.
- U814c (control = HRES*) routes the fixed reference when HRES* = 1.
- Either path charges **C806 0.022 µF poly (2.5 %)**, which is buffered by U815B (×(1+2.43K/1K) = **×3.43**) → REF → U811 pin 19 and R850.

**Normal mode (A8 = 1):** V_DAC = (code/16384)·REF_N, with REF_N = 3.43·1.416 = **4.86 V** (MAME: 4.865 V, vb.cpp:529–535) [V].

**High-resolution mode (A8 = 0, used for VCOF1, VCOF2 and VCFF):**
1. Fine write (A7 = 1): U814b (control FTSH) samples U812's output onto **C805 0.022 µF**, buffered by U815D.
2. Coarse write: U814a (control HRES*, edge-shaped by R848 22K ∥ D804 with C808 33 pF) connects C805 through **R849 30.1K** and REF through **R850 17.4K** into U813's summing node.
3. Result: V_DAC = REF_T·[(code + fine/3.01)/16384 − 0.5747].
4. REF_T = 3.43·T_voice ≈ **8.58 V at T = 2.5 V**. The datasheet range is 2.2–2.8 V, so 7.5–9.6 V.
5. Range: −4.93 V … +3.65 V, plus up to +2.85 V of fine. Clamped at +5.0 V.
6. Because REF scales with the chip temperature (+3300 ppm/K ≈ 1/T), the 0 V point and the V/oct slope both track kT/q [V SM p16–19; I for the algebra].

**With ECO 609** add −REF·10K/422K: −0.115 V normal, −0.203 V HRES [I].

Resolution in HRES [I]:
- Coarse LSB = REF_T/16384 = 0.52 mV ≈ **0.68 cent** at the VCO.
- Fine LSB = 0.174 mV ≈ **0.23 cent**.
- For VCFF, the same LSB is 0.70 cent.

### 4.3 Smoothing S&H (per voice: UX06, UX07, UX08) [V `voice-s3-smoothing-sh.png`, `voice-s3-sh-ch0-vca-ch1-pw1.png`, SM p28]

- UX06 (4051, IN = DAC, address SHA–SHC, INH = SHX*) charges **C1 = 0.033 µF** directly. Poly for the pitch/VCF channels, mylar for the others.
- C1 → **R = 1M** → **C2 = 0.0068 µF** → buffer or CEM pin.
- UX07 (4051, same address, INH = SHX* OR FAST* via DX01/DX02 and RX09 10K) writes C2 directly. This is the "sharp" bypass.
- Smoothing τ ≈ 1M · C1C2/(C1+C2) = **5.6 ms**; ≈ 6.8 ms when C1 is held [I].
- MAME `VA_SMOOTH_SH(1M, 0.033µ, 0.0068µ)` (vb.cpp:115–116) [V].

| ch (UX06/UX07 pin) | Rev B (manual) | Rev D (MAME, firmware flag) | C1 / R / C2 | Buffer | Destination |
|---|---|---|---|---|---|
| 0 (13) | **VCA** | PW1 | CX10 / RX14 / CX18 | UX08 1–3 (VCA) | RX51 18K → 3372 pin 13 |
| 1 (14) | **PW1** | VCA | CX09 / RX15 / CX17 | none | UX11 pin 5 |
| 2 (15) | VOLA | VOLA | CX08 / RX16 / CX16 | none | 3372 pin 5 |
| 3 (12) | VCOF1 | VCOF1 | CX11 / RX17 / CX19 (poly) | UX08 12–14 | 4.22K/100 Ω → 3374 pin 12 |
| 4 (1) | VOLB | VOLB | CX04 / RX11 / CX12 | none | 3372 pin 8 |
| 5 (5) | VCFF | VCFF | CX07 / RX10 / CX15 (poly) | UX08 5–7 | RX53 → 3372 pin 15 |
| 6 (2) | PW2 | PW2 | CX05 / RX12 / CX13 | none | UX11 pin 3 |
| 7 (4) | VCOF2 | VCOF2 | CX06 / RX13 / CX14 (poly) | UX08 8–10 | 4.22K/100 Ω → 3374 pin 7 |
| RES | U816 ch(voice−1) | same | C815–C819, C814 / R858–R863 / C820–C825 | none | 3372 pin 10 (**no fast path**) |

### 4.4 Timing

- **The voice IRQ comes from 8253 counter 2:** mode 3, count 0x4650 = 18000 at 2 MHz → **111.1 Hz (9.0 ms)** [V vb ROM C683–C691]. The service manual says "every 10 ms" (SM p28).
- Each IRQ walks all 6 voices: DP = $40 is the voice-1 page, and voices are 5 pages apart [V C946–C960, EE20–EE38].
- Per voice, the update chain writes: fine + VCOF1, fine + VCOF2, fine + VCFF, PW1, PW2, VOLA, VOLB, RES, the MDAC latch ($8000+) and VCA. Reference-mux pre-select dummy writes ($8C0B, $8C0F, $8D05, $8D09, $8D0D, $1EF3/$1EF5) are interleaved [V CB43–CC53]. That every CV is written on every IRQ is [I], from the code layout; I have not traced it branch by branch.
- **Acquisition [I]:** the 4051 Ron (125 Ω typical, 240 Ω maximum at a 15 V span, TI CD4051B data sheet) × 0.033 µF ≈ 4–8 µs. The write windows, logged from the 6809 core with a note held (VOICEN* to the next CLEAR*), are:

| CV | Window |
|---|---|
| FTSH (fine) for VCOF1, VCOF2 | 33 µs |
| FTSH for VCFF | 26 µs |
| VCOF1 | 134.5 µs |
| VCOF2 | 170 µs |
| VCFF | 41 µs |
| PW1 | 33 µs |
| PW2, VOLA | 26 µs |
| VOLB | 24.5 µs |
| RES | 103.5 µs |
| VCA | 611 µs (one voice 1.8 ms) |

  Before the normal-mode writes the fixed reference is pre-selected on C806 for 30 µs. The service routines hold an S&H much longer: DAC MONO keeps VCO 1's selected for 18 ms and counts its frequency meanwhile (firmware.md).

### 4.5 Per-CV ranges into the CEM pins [I from the [V] values]

| CV | DAC mode | S&H range | Into pin |
|---|---|---|---|
| VCOF1/2 | HRES | −4.93 … +5.0 V | VCIN = 0.02315·V → −114 … +116 mV. 3374 is +17.9 mV/oct (datasheet), so **0.773 V/oct at VCOF**, ≈ 12.8 octaves |
| VCFF | HRES | −4.93 … +5.0 V | FREQ −150 … +102 mV, 0.750 V/oct |
| VCA | normal | 0 … 4.86 V (−0.115 with ECO 609) | pin 13 via 18K/0.1 µF; firmware offset $59 = 0x300 codes = 0.23 V [V C77C] |
| VOLA/B | normal | 0 … 4.86 V | pins 5/8, audio taper |
| PW1/2 | normal | 0 … 4.86 V | comparator vs saw/2 (0–4 V); 50 % at ≈ 2 V |
| RES | normal | 0 … 4.86 V | pin 10. With 20K/1.2K, oscillation is expected at ≈ 2.8 V × (1/23)/(1.2/21.2) ≈ 2.2 V. Firmware maps dial 0–62 linearly up to the measured break point and 63 → 5 V (SM p21) |
| FM amount | MDAC latch | 8 bits | ±2 V·D/256 |

---

## 5. Auto-tune and calibration

### 5.1 Measurement hardware (voice sheet 1 = SM p51, sheet 4 = SM p57) [V `voice-s1-pit8253-dataout-datain-mute.png`, `voice-s4-osc-comparator-lm311.png`]

**OSC.**
- U108 (LM311): the − input (pin 3) is the sum of all six VOICE nets through R178–R183 (10K each). R186 runs to ground: 10K on voice 1's line as drawn; **4.7K at the summing node after ECO 602**.
- The + input (pin 2) has R184 47K to ground. C105 330 pF sits between the inputs.
- The output (pin 7) has an R195 1K pull-up to +5 V and is the OSC net.
- **No hysteresis** [V].
- OSC is taken **before** the pan muxes, so it works while outputs are muted.

**DATAOUT (U920 74C174 at $9000):**

| Bit | Signal | Effect |
|---|---|---|
| D0 | HALTDS | blocks main-CPU halts |
| D1 | AUTOST | 8253 GATE0 |
| D2 | PW* | GATE2 = PW* OR OSC (U915 LS139 used as a gate) |
| D3 | AUTO* | **MUTE = NOT(AUTO*·RESET*)** → INH of U102–U107 |

**DATAIN (U916 at $9400):** D0 = AUTODNE* (= OUT0), D1 = OSC [V].

**8253 U921 ($9800–$9803):**
- CLK0 = OSC, GATE0 = AUTOST, OUT0 = AUTODNE*.
- CLK1 = Q (2 MHz), GATE1 = NOT OUT0.
- CLK2 = Q, GATE2 = PW* OR OSC. OUT2 → U919 → IRQ* (cleared on the interrupt-acknowledge cycle via BA*·BS).

Measurement modes [V vb ROM 1.4]:

| Function | Setup | Measure |
|---|---|---|
| Frequency (VCO, VCF) | EB17: DATAOUT = 1 (mute, PW* = 0). PIT: ctr0 mode 1 (one-shot of N OSC edges), ctr1 mode 0, ctr2 mode 0. Latches: MDAC 0, waveforms off, LATCH2 = $6C | EA8E: ctr1 = $FFFF; AUTOST 0→1; wait OUT0 low then high; read ctr1 → 2 MHz counts over N periods. Compare with the table at **$E974** (4-byte entries: note, target count, N) |
| PW duty | DATAOUT = 1 (PW* = 0 → ctr2 counts only while OSC is high) | ED04–ED65: ctr1 = total time, ctr2 = high time over N periods; iterate until \|high − total/2\| < 4 counts (target 50 %; "control value 31", SM p20) |
| Resonance | EE14: N from the table entries for E9 ($E984) and E5 ($E978); RES at max, then at the break point | EEDA: up to 10 successive N-period measurements; each must arrive within 6000 polls per edge and differ from the first by < 4096 counts (`diff & $F000 == 0`, EF31–EF42) ⇒ "oscillating". This is a coarse stability test, not a pitch test |
| VCA ("TUNE VCA") | — | [N]. The main ROM tune page offers `VCOS PW RES VCF VCA ALL` [V main ROM string at the TUNE text]; the service manual does not describe VCA tuning |

Tune target table at $E974 [V ROM dump]:

| Note (code) | Target count | N | Period → frequency |
|---|---|---|---|
| C4 ($30) | 30578 | 4 | 261.6 Hz |
| E5 ($40) | 24268 | 8 | 659.3 Hz |
| G#6 ($50) | 19262 | 16 | 1661.3 Hz |
| C8 ($60) | 30578 | 64 | 4186 Hz |
| E9 ($70) | 24268 | 128 | 10548 Hz |

- These match SM p20 (C4, E5, G#6, C8, E9) [V].
- Resolution is ≈ 0.06–0.09 cent per count [I].
- The note code is semitones with C4 = 48 [I].
- Corrections are stored in main-CPU battery RAM (SM p20) [V]. Below C4 they follow the parabola through the C4, E5 and G#6 corrections, not a straight line: the tuned S&H voltages fit it within 0.8 mV, a line from the C4-E5 slope misses by up to 7.7 mV (12 cents) [V: model runs of voice 1.4's tune].
- VCFs are tuned like VCOs while self-oscillating, and retuned after a RES calibration (SM p20–21) [V].
- UNTUNE zeroes the VCO, PW, RES and VCF corrections (SM p36) [V].
- **There are no analog trims on the voices.** The six trimmers in the unit are T801 (DAC offset), T1 (ADC), and the display board's T1–T4 (grid voltage and 3 brightness) (SM p2, p34; A#53) [V].

### 5.2 Service checks: future automated checks

| # | What | Where | Expected | SM |
|---|---|---|---|---|
| 1 | +12 V | voice-board power connector, vs. ground lug left of the DAC | 11.25–12.75 V | p32 |
| 2 | −12 V | same | −11.25 to −12.75 V | p32 |
| 3 | −5 V | same | −4.5 to −5.5 V | p32 |
| 4 | +5A | same | 4.5–5.5 V | p32 |
| 5 | +5D | same | 4.5–5.5 V | p32 |
| 6 | AGND | same | 0 ± 10 mV | p32 |
| 7 | +5M (memory) | right lead of D4, processor board | power on 3.80–5.00 V (4.4 nominal); power off 2.40–4.00 V (2.7 nominal) | p32 |
| 8 | +55 V | pot-board power connector pin 6 | 55 ± 4 V | p32 |
| 9 | 9 VAC filament | pot-board power connector pins 7–8 | 9 ± 2 VAC at ≈ 30 kHz | p32 |
| 10 | Main ROM/RAM test (MASTER → PAGE 2 → SERVICE → MEM; memory protect off) | — | "RAM 0/1/2 OK, ROM 0–3 OK". Bad chip map: RAM0 = U2, RAM1 = U3, RAM2 = U4, ROM0 = U8, ROM1 = U7, ROM2 = U6, ROM3 = U5 | p33 |
| 11 | Voice ROM/RAM test (VMEM) | — | "CA MEM OK" after ≈ 20 s; else U912, U914, U917. Firmware F47A: non-destructive RAM test $0000–$1FFF, then the 16-bit sum of $C002–$FFFF must equal the word at $C000 (1.4: $EE70, verified) unless that word is $FFFF | p33 |
| 12 | LED test (LEDS) | — | all 16 LEDs on ≈ 2 s, then one at a time | p33 |
| 13 | VFD test (DISP) | — | all segments on all 120 digits; all segments one digit at a time; one segment at a time across display 1, then 1+2, then 1+2+3 | p33 |
| 14 | VFD brightness | display board | set 55 V; brightness trimmers to max, then turn the brightest down to match the dimmest | p34 |
| 15 | 4 V reference | U815 pin 8 | 3.920–4.080 V | p34 |
| 16 | DAC offset (SERVICE → DAC OFFSET) | U812 pin 6, trim T801 | 0 ± 1 mV | p34 |
| 17 | DAC monotonicity (DAC MONO) | audio output | no beeps or very seldom | p35 |
| 18 | ADC reference, DVM method | U20 pin 9 (lower lead of R47), ground at left of R65, trim T1 (pot board) | 2.667 ± 0.002 V | p35 |
| 19 | ADC, audio method | CV1 = 0.000 V, CV2 = 5.000 V gating voices 1 and 2 | stable C0 and C5; trim T1 | p35 |
| 20 | Software reset | hold CLEAR at power on, choose YES | default configuration; patches kept | p36 |
| 21 | VERSION page | MASTER → PAGE 2 → VERSION | main / voice / cassette versions | p36 |
| 22 | UNTUNE | SERVICE → UNTUNE | VCO, PW, RES and VCF corrections = 0 | p36 |
| 23 | Halt watchdog | normal operation | "VOICE PROCESSOR MALFUNCTION" if a halt is not acknowledged within 1 s | p8 |
| 24 | VCO tune | TUNE → VCOS | 12 VCOs within table targets at C4/E5/G#6/C8/E9 | p20 |
| 25 | PW tune | TUNE → PW | 50 % duty at value 31 | p20 |
| 26 | RES tune | TUNE → RES | oscillates at E9 with RES = 5 V; break point found at E5 | p21 |
| 27 | VCF tune | TUNE → VCF | self-oscillation frequencies on the same 5 points | p20 |

---

## 6. Firmware and digital side

### 6.1 Main CPU: 68B09 U9, 2 MHz E [V SM p39; x.cpp:771–815]

| Range | Device |
|---|---|
| $0000–$3FFF | RAM0/RAM1 (U2, U3 6264). WRITEA* is blocked by the MEMORY PROTECT switch SW6 |
| $4000–$5FFF | RAM2 (U4 6264; WRITEB*, never protected). **Battery RAM total 24 KB** (2.9 V lithium) |
| $6000 | DISP*/DISPCLR* (display, A0–A8 → anode latch and grid) |
| $6400 | INTSET* (40103 preset; the firmware writes **110** at BA49 → FIRQ ≈ every 111 × 4 µs ≈ 2.25 kHz [V ROM; I period]) |
| $6800 | HALTSET* (U25: HALTREQ0* … 3*, RES* (voice reset), CASSMUTE*, CASSOUT) |
| $6C06/7 | ACIA 6850 (500 kHz ÷16 = 31.25 kbaud; init $03 then $95 = 8N1, RX IRQ [V ROM A2AF]) |
| $7000 | DATAIN: D0 HALTAKN*, D1 memory protect, D2 cassette data |
| $7400 | LED2* |
| $7C00–$7DFF | pot board via BEN*: DIR*, SW*, LED1*, GATE*, PULL*, SWITCH* (8 columns), CV* (ADC + mux) |
| $8000–$9FFF | ROM0 (U8), or **voice RAM when HALTAKN* is low** |
| $A000–$FFFF | ROM1–3 (U7, U6, U5) |

- Vectors: RESET $B526, IRQ $BA15 (ACIA), FIRQ $BA5A (40103: display multiplex + DISPCLR*). NMI and HALT are tied high [V ROM, SM p39].
- RESET comes from PUP; PUP also gates the RAM chip selects [V].

### 6.2 Voice CPU: 68B09 U907, 2 MHz E (8 MHz from the processor board) [V SM p51; vb.cpp:611–636]

| Range | Device |
|---|---|
| $0000–$1FFF (+ mirrors to $7FFF) | U917 6264 (CS = A15) |
| $8000/$8400/$8800 + voice | LATCH0*/1*/2* → U804/U802/U801 (LS42; outputs 0–5 = voice 6..1) → UX03 (MDAC), UX02 (wave/sync/FM routing), UX01 (FA–FD, NOI, pan) |
| $8C00–$8DFF (+ $8E00) | SDAC (section 4) |
| $9000 | DATAOUT (U920) |
| $9400 | DATAIN (U916) |
| $9800–$9803 | 8253 U921 |
| $A000–$BFFF | U909 spare socket (reads $FF) |
| $C000–$DFFF | U912 `ca1.4-0` |
| $E000–$FFFF | U914 `ca1.4-1` |

- Vectors: RESET **$C675**, IRQ **$C946**. FIRQ, NMI and SWI are unused ($FFFF). FIRQ, NMI and BREQ are tied high [V ROM, SM p51].
- **Main ↔ voice protocol:**
  1. Main sets HALTREQ0* low ($6800).
  2. If the voice CPU's HALTDS = 0, U906 asserts HALT*. BA goes high, U901–U904/U916 bridge the buses, and Q901 pulls HALTAKN* low.
  3. Main polls $7000 bit 0, swaps ROM0 for voice RAM at $8000 and reads or writes.
  4. Main releases HALTREQ0*.
  [V SM p8, p51; ROM `bita $7000` loops at BEB3 and elsewhere]
- The voice IRQ handler brackets its reads of main-written 16-bit variables with HALTDS = 1 (`$9000 ← $0F … $0E`, C94B–C976) [V ROM].
- The connector selects HALTREQ0..3* by jumper (B5–B8); the Matrix-12 uses this [V SM p51].

### 6.3 MIDI, CV/gate, encoders, buttons, VFD

All [V] from SM p39–49, x.cpp, parts lists.

- **MIDI:** 6850 U26; 6N138 opto in; LS04 drivers for out and thru. Thru is a hardware copy of In.
- **CV inputs:** CV1–6 through 470 Ω, plus pedal 1/2 (62K to +12 V; R75/R76 removed by ECO 601), clamped 0–5.6 V by D55–D71 (D63/R39 sets the top). They go to the U17 4051, addressed by U18 LS174 from the CV* write (A0–A3 → A/B/C/INH), then to the ADC0804 U20 (500 kHz clock, VR/2 trimmed by T1).
- **Gates:** six gates, chain advance and trigger, each via 33K into CA3081/Q1, with polarity pulls R57–R63 4.7K from PULL1–7 (U21).
- **Encoders:** 6 × LA226 (30 detents). 10K/0.1 µF RC, LS14 Schmitt, LS74 direction and step flip-flops, LS02 NOR. Read at DIR*/SW*; reading DIR* clears the step flip-flops.
- **Buttons:** 8 columns × 8 rows (U16 LS42, U24 HC244).
- **LEDs:** LED1 (pot board U14), LED2 (processor board U24; VCO1, VCO2 and FM/LAG LEDs via B14/B15/B19).
- **VFD:** 3 × **Itron FG405A2**, 40 digits × 16 segments (14-segment + DP + underline), 55 V anode and grid supply, 9 VAC filament. UDN6118 drivers, 6 anode latches, grid decode 74LS42 + 5 × 4028. Segment patterns are written directly by the main CPU. **No character ROM exists** (the display parts list has no ROM, SM p64) [V]. The font is in main ROM [I, not located].

### 6.4 ROMs and versions [V]

- The SHA1s match MAME (x.cpp:1572–1575; vb.cpp:519–524).
- The voice-ROM version byte sits at $C698 (`LDA #$14` → $1EA0, read by main for the VERSION page "VOICE VERSION").
- 1.4 vs 1.6 differ in **exactly 5 bytes**:

| Address | 1.4 | 1.6 |
|---|---|---|
| $C000–$C001 | checksum $EE70 | $FFFF (VMEM checksum disabled) |
| $C698 | $14 | $16 |
| $C6CA | `LDA #$0A` | `BRA $C746` (skip revision detection) |

- U914 is identical in both.
- [I] 1.6 looks like a minimal binary patch of 1.4.

### 6.5 How voice ROM 1.4 detects the board revision (C6CA–C757) [V disassembly; interpretation I]

1. **At reset:**
   - DATAOUT = 5: HALTDS = 1, PW* = 1, AUTO* = 0 → outputs muted.
   - PIT ctr2 is set up as the 111 Hz IRQ.
   - All latches are set: MDAC = 0; UX02 = $36 (saw1, tri1, saw2, tri2 on); UX01 = $EC (mode $C, NOI 0, pan 7).
   - C8E1 writes DAC code 0 (FAST) to **CV0 and CV1** of all voices, 50 times. In either revision those are VCA and PW1.
2. **Test 1 (C6CF–C6F1):** poll OSC (DATAIN bit 1). If OSC changes state 10 times, with each change seen within 255 polls (each ≈ one C8E1 pass, ≈ 2 ms), the firmware jumps to **C746 = Rev D configuration**. Otherwise it goes to test 2.
3. **Test 2 (C6F3–C744):**
   - C913 writes, 30 times per voice: **CV0 = near full scale**, CV1 = 0, VOLA = VOLB = VCFF = max, VCOF1/2 = code 0x3000 (normal reference, ≈ 3.65 V → ≈ 16 kHz), PW2 = 0, RES = 0.
   - Set PIT ctr0 to mode 1 (one-shot), N = 0x0303 = **771 OSC edges**, and pulse AUTOST.
   - Wait up to 100 C913 passes for OUT0 to go low (counting started), then up to 200 passes for OUT0 to go high (771 edges counted).
   - **Only if both happen** does it keep the **old configuration**. Any timeout → C746 (Rev D).
4. **Configurations:**

| Config | $1E01 | $1EF3 | $1EF5 | Per-voice pointers |
|---|---|---|---|---|
| Rev D | $FF | $8D01 (CV0) | $8D03 (CV1) | $2C (PW1 S&H) = CV0, **$36 (VCA S&H) = CV1** |
| Old | $00 | swapped | swapped | $2C = CV1, $36 = CV0 |

   [V C7ED–C827, CC09–CC53]

   On a Rev B board CV0 = VCA, so test 2 opens the VCAs; the 16 kHz tone toggles OSC and the one-shot completes (old order). On Rev D, CV0 = PW1 and the VCA is shut, so OSC stays quiet and the firmware picks Rev D.

**What a Rev D board must present for 1.4 to work [I]:**
- With every VCA CV at code 0 (≈ 0 V, or −0.115 V with ECO 609) and VOLA/VOLB/VCFF at max, the summed VOICE outputs must **not** make U108 toggle 771 times in ≈ 2 s.
- So the emulation needs:
  - a real VCA cut-off: 3372 shut-off at ≈ +85 mV, feed-through ~ −100 dB;
  - an LM311 model with an input offset (a few mV) and the 4.7K/47K input network;
  - **no chatter from modelled hiss** around 0 V.
- A comparator with an exact zero threshold plus noise will toggle and **make 1.4 pick the wrong order**: VCA and PW1 swapped, silent voices. MAME's remark that the check "became unreliable as components aged" fits VCA bleed doing exactly this.
- The 8253 CLK0 must be OSC.

### 6.6 What MAME leaves unimplemented [V vb.cpp, x.cpp]

- VCOs (CEM3374), VCF channel A, sync, waveform switches and FM routing (latches are outputs only).
- PIT CLK0 and OSC (no auto-tune possible).
- AUTO*/MUTE.
- Individual outs (pan 7).
- Temperature sensors (constant 2.5 V).
- Cassette in.
- ECO 609.
- Driver flags: MACHINE_NOT_WORKING | MACHINE_NO_SOUND (x.cpp:1595).

---

## 7. Revisions

- **Voice board Rev B (this manual):** artwork 1810B / silkscreen 1812B; schematic 1808 without a revision letter.
  - S&H order: **ch0 = VCA, ch1 = PW1** [V].
  - The drawings predate ECOs 602, 603 and 609. The parts list includes 602 only [V].
- **Rev A:** [N]. Presumably pre-production or pre-ECO-602 artwork.
- **Rev C, Rev D (not documented in either scan):**
  - Per MAME, Rev D has **PW1 = ch0, VCA = ch1** (vb.h:53–54, vb.cpp:66–71) [V MAME].
  - The 1.4 firmware supports both orders, so the swap is real [V ROM].
  - [I] A later artwork would also absorb the trace-cut ECOs: 602 (12 cuts, R186 moved to the LM311 node) and 605 (doubled DAC trace to voice 6). It would very likely include 603 (RX64 20K), 606/614 (decoupling) and 609 (422K offset).
  - Rev C ↔ D differences are [N].
- **Other boards:** processor 1853B; pot 1862D (ECOs refer to layout 1866D); display 1844A; PSU 1871B (+ ECO 608) [V].
- **What "Rev D" means for us concretely:**
  1. Map CV index 0 → PW1 and 1 → VCA.
  2. Assume ECOs 602, 603 and 609 (RX62 = RX70 = 33K, RX64 = 20K, R186 4.7K at the U108 input, 422K offset).
  3. Keep everything else as drawn in 1808 Rev B.
  4. Run voice ROM 1.4 and let it detect Rev D through test 2.
  5. Keep the 1.6 image as a fallback (identical except for the check).

---

## 8. Modelling plan and open questions

### 8.1 How to model each block

| Block | Approach |
|---|---|
| Main CPU, voice CPU, 6850, 40103, 8253, ADC0804 (digital part), latches, VFD/LED/encoder/switch logic | **Exact digital**, cycle-counted. The bridge (HALTREQ/HALTDS/BA/HALTAKN, ROM0 ↔ voice-RAM swap) needs per-cycle accuracy for the 1 s malfunction watchdog and the HALTDS critical sections |
| DAC (HS3140 + U812/U813 + fine S&H + C806 reference sample + ECO 609 + 5 V clamp) | **Exact digital value plus event-driven switched RC.** Compute V_DAC from code, fine and REF_T at each bus write. Model C805, C806 and each S&H's C1–R–C2 as small linear ODEs that integrate over the CPU-timed switch windows (4051 Ron ≈ 250 Ω). This keeps the firmware's settle/padding behaviour, costs nothing per sample, and beats MAME's instantaneous sampling. T_voice is a parameter (2.5 V nominal); auto-tune absorbs the absolute error |
| CEM3374 VCO core | **CEM chip model** (behavioural, data-sheet based): exponential converter at 17.9 mV/oct with kT/q and IREF scaling; capacitor charge/discharge between 0 and V_TH (+4REF) with comparator delay; the 1.2K ∥ 0.01 µF HF network on the cap; tri buffer; tri→saw unfold (saw = tri up, 8 V − tri down); Sync A reset (50K input, −1.2 V threshold); linear FM into IREF. Sub-sample event timing (polyBLEP-style or ≥4× oversampling) for the resets, sync and the pulse comparator |
| Waveform network (4016 Ron(V) with VSS −0.6 V, dividers, TL082 comparator with ≈13 V/µs slew and diode, coupling caps, 680 Ω, RX61 noise path) | **Netlist on our solver.** Small and mostly linear. The 4016 Ron nonlinearity matters for the triangle |
| AD7523 MDAC FM | Exact gain D/256 plus a netlist for the I/V stage, 4053 routing, the RX65/RX41 loads and coupling caps |
| Noise generator | **Behavioural** (clipped broadband noise, ±51 mV at NOISE, spectrum fitted to recordings), then the 47 Ω source into the netlist |
| CEM3372 VCF + input VCAs | **CEM chip model**: 4 OTA-C cells with tanh, cell 4 at 1/100 current with ×100 output buffer; resonance input with internal scaling from the datasheet (2.8 V for oscillation at 22K/1K); audio-taper level VCAs at 1/100 into the filter (as MAME `cem3372.cpp`). Cap values per cell from the switched C1 (CX46/CX45) |
| Cell buffers + pole-mix + UX15 summer | **Linear netlist** of the cap-voltage taps, or exact precomputed weights per mode from the table in 3.3. Add op-amp output clipping (TL074/TL082 at ±12 V) and the 4051 Ron in series with the selected channel. Mode changes switch weights and C1 at the latch write time |
| 3372 final VCA + UX16 I/V | CEM chip model (linear CV, 0.93 max, ≈85 mV cut-off, ±200 µA soft limit) plus the netlist for RX62/RX70/CX47/CX49 |
| Pan 4051s, U101 summers, master volume, U1 buffers, MONO network, individual outs | **Linear netlist**, with 4051 Ron and rail clamps (+5 V), MUTE gating, and the cassette injection path |
| OSC comparator (U108) | Behavioural comparator with offset (a few mV), the input RC (C105 330 pF, 10K/4.7K/47K network) and propagation delay. Its output clocks the exact-digital 8253 |
| Power supply, VFD high voltage | Not modelled, except as constants (+5A, −0.6 V, references) |

### 8.2 Open questions and what would settle them

1. **What else changed in Rev C and D** beyond the PW1/VCA swap (R186 position, ECO 602 "cut two places" at UX16, any value changes). *Settle with* a Rev D voice schematic (the Matrix-12 service manual's voice sheets are MAME's likely source) or photos of a Rev D board. A higher-resolution ECO 602 page 1 is now local (`manuals/ecos/xpander_eco602.jpg`).
2. **CEM3374 internal behaviour:** tri→saw converter glitches, comparator delay, sync reset completeness, exact V_TH relation, initial frequency at VCIN = 0 (≈ 620 Hz if the charge current equals IREF). *Settle with* recordings or scope traces of single-VCO saw/tri/sync patches (free recordings), plus the data sheet in `docs/datasheets/`.
3. **CEM3372 details:** relative cell polarity (assumed non-inverting), exact VCFOUT/cap-4 gain (assumed 100), resonance-CV curve, input-VCA taper, behaviour with FD = 1 and resonance (3-pole loop). *Settle with* recordings of mode sweeps with resonance, or the AS3372 datasheet (`datasheets/Alfa_AS3372E.pdf`) for internals.
4. **Noise spectrum and level** of the clipped LM301 generator. *Settle with* a noise-only patch recording; also check LM301 compensation pins on the processor layout (A#50).
5. **S&H acquisition windows and per-CV write order** per IRQ. *Settle by* logging DAC bus writes with timestamps from our 6809 core running 1.4. No hardware needed.
6. **VCA tune routine** (TUNE → VCA) and the $59 = 0x300 VCA offset. *Settle with* voice ROM disassembly of the tune dispatcher (around $E988/$EC3C).
7. **X ↔ voice numbering** (X = 8 − voice assumed from ECOs 605/606/614). *Settle by* tracing on the 1812B silkscreen (A#52) from the RES S&H outputs (C820–C825 "VOICE 1..6") to a column.
8. **ADC scaling** in firmware (MAME 5.0 V vs. 5.333 V full scale, 6-bit use). *Settle by* tracing the main ROM CV-input code.
9. **Exact 40103 FIRQ period and VFD refresh** (preset 110; MAME's ±1 handling). *Settle with* the 40103 datasheet (now local) and an emulator run.
10. **Is ECO 609's 422K on Rev D?** It changes every CV by −2.4 % of REF. *Settle with* the Rev D schematic or board photo (as in 1). Auto-tune hides it for pitch but not for VCA/VOL zero points.
11. **The font table** location in main ROM (for a VFD panel renderer). *Settle by* searching exp1.2 for a 16-bit segment table indexed by ASCII.

---

Sources and tools: schematic crops in `private/docs/schematic/crops/`; ROM disassemblies (capstone M680X, `private/docs/tools/dis.py`) in `private/docs/disasm/voice14.dis` and `main12.dis`; pole-mix algebra in `private/docs/tools/polemix.py`.

---

## 9. Reference recordings

All in `private/reference/` with `youtube/SOURCES.md` (what is played and when, from captions and
descriptions; stereo and correlation checks). Standard YouTube licence unless noted; private
reference only. No serial numbers or revisions are stated anywhere.

| Source | What | Use |
|---|---|---|
| **Matrix-12** NuBRcBlNAH8 (gstormelectro) | all 15 filter modes on one sawtooth, then each at maximum resonance; mono, floor −86 dBFS, no clipping | filter-mode responses and self-oscillation (mode labels on screen only) |
| **Matrix-12** Yxxwb4d8GHA (Vulture Culture, 2026) | A/B against the GForce plugin, level-matched, mono 1:30-27:59; captions give resonance values (63 oscillates, 62 just short), filter FM, PW range, noise colour, cross-mod | the only stated-setting material; cut the hardware takes |
| freesound packs 17452/17457/17460/17463 (Modular Samples, **CC0**) | 220 single notes, MIDI 36-96 at velocity 127, 4 Xpander patches, dry | per-note pitch, tracking and voice spread (patch settings unknown; use the MIDI number in the file name, the titles are wrong) |
| qfg9Jni8nN8 (synthlegends) | 18 min, Xpander alone, no effects, panned voices; some clipping | general sound, per-note level and spectra |
| R5QXJDEJlyk (tomm noise) | dry presets, dual mono; some clipping | presets |
| VS7MldWcmYM, 6n_ONjfJzZ0 (LESINDES, one unit) | no FX, KeyStep sequences, panned | gate and envelope timing |
| 11MKJkpqzwQ (Collector Current) | VCO FM bells, mono, no effects, no clipping | FM |
| h_BU2jK6czw (AustinSynthLabs), wOHqSobYrhw (HanzVolt) | filter-mode demos on two more units; mode order on screen | unit spread on the filter |
| **Matrix-12** Iz_dFadQSuI (gstormelectro) | VCO FM with routing and waveforms in on-screen text, no effects | FM |
| SplzA8MS0Mo, 3DFsES1SgkM | short A/B with an OB-Xa; six-voice chords after a passing auto-tune | low |
| YbxNYpw_9g0, gzVSwz7uT6M, A_8rvhUjONc, archive.org `ecsm-01-1984-10` | room microphone, decorrelated "dry" stereo, probable reverb, 1984 cover-tape mix | lowest |

**Gaps:** no recording with full stated settings (no init patch, no single VCO at a stated pitch);
nothing from the individual voice outs; no clean filter sweep with stated resonance on an Xpander;
no measured envelope/LFO timings; no isolated VCO waveform or PW sweep on an Xpander; no noise-floor
or VCA-bleed recordings; no scope shots. Units can't be identified across videos.

**Third-party models** (not references): Arturia Matrix-12 V, GForce Oberheim Matrix-12 (the
Yxxwb4d8GHA A/B gives level-matched hardware takes beside it).

## 10. Size and CPU

| | Pro-One (built) | Xpander |
|---|---|---|
| Voices | 1 | 6 (independent until the pan muxes and summers) |
| Per voice | 2 × CEM3340, CEM3320, 2 × CEM3310, CA3280, LFO | 1 × CEM3374 (2 VCOs), 1 × CEM3372, pole-mix network, MDAC FM, 9 S&Hs |
| CPU core | 8021 (ours: own firmware) | 2 × 6809 at 2 MHz (real firmware), 8253, 6850, ADC0804, 40103 |
| Measured / estimated | 9.6-12.5 % of a core at 48 kHz | ≈ 10-12 % per voice ⇒ 60-75 % for six, plus the CPUs (≈ 1 %) |

The voices run as independent jobs on worker threads, bit-identical to one thread (the TR-909's
`core/parallel.*` pattern). Sleep: voices settle to exact zeros when their VCAs are shut and their
S&Hs settled; the CPUs keep running (cheap).

## 11. Policy and name

- **Recent machines:** the Xpander was made 1984-1988 and is not sold new. A future hardware
  product under the XPANDER mark would be a reissue, which the policy doesn't count.
- **Trademark [V, `private/docs/trademark/notes.md`]:** XPANDER is a live mark for exactly this kind
  of product, owned by Marion Systems, LLC (San Francisco): US Reg. 7475329 (class 9, music
  software, registered 2024-08-13), US Serial 98434046 (class 15, synthesizers, allowed, awaiting a
  statement of use), EUTM 019074503 (class 15, registered 2025-01-09), UK00004095101 (class 15).
  Oberheim Electronics' own 1985 registration was cancelled in 1992.
- **Name:** the README allows a short form of a model's word name only where no live trademark
  covers it; otherwise the model part is its initials. By that rule the name would be
  **ob-poly-x** (code Oxpo), not ob-poly-xp. Decided 2026-11-13: keep ob-poly-xp.
- The original's name appears only in descriptions, with "Not affiliated with Oberheim."
