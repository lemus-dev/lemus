# TR-909 emulation research digest

Compiled 2026-10-06 for the component-level TR-909 plug-in (JUCE AU/VST3, C++).

The plug-in runs the original v4.0 firmware on an emulated µPD7811. It models the analog voices part by part from the service notes, and plays the original 6-bit hi-hat, crash and ride ROMs through a model of the playback circuit.

This digest collects what is known about the 909's circuits, firmware and sound from these sources:

- Roland's own documents;
- published analyses and clone projects;
- emulator code;
- forum measurements;
- our own measurements on recordings of real 909s, collected in `reference/real909/`.

Each part gives:

- the circuit or mechanism, with Roland designators;
- every number found, with its source, and derived numbers marked **"my calc"**;
- measurements on real-909 recordings, marked **"meas."**;
- where sources disagree;
- a recommendation for our implementation;
- open questions.

Section 8 tabulates the main constants. Section 10 lists what is still unknown and should be measured or simulated.

**Conventions**

- Designators are Roland's: R59, C9, IC12, VR2 and so on, from the voicing-board schematic, Service Notes p.11.
- "p.N" is a page of the Service Notes PDF (`docs/papers/roland/TR-909_service_notes_A.pdf`, 16 pages, first edition, 15 Jun 1984).
- "SN" is a serial number. Several parts changed in production: see section 9.
- Citations are per section: [A1-n] analog voices I, [A2-n] analog voices II, [B-n] sample voices, [C-n] firmware, [D-n] emulations. Each section ends with its own source table (URL plus local copy).
- All local copies are under `docs/papers/`. The full file list is `docs/papers/SOURCES.md`.
- The research was done in parallel by several agents ("forks"). Phrases such as "fork E's measurements" refer to the work behind the reference-audio index (E = sample packs, F = videos). Their scripts are kept with the data:
  - `reference/real909/packs/_measurements_forkE/`;
  - `_measurements_forkA1/`;
  - `_measurements_forkA2/`;
  - `reference/rom_decoded/scripts/`.

---

## Corrections found since

This digest was written before the circuits were transcribed and the firmware run procedure by
procedure. Where the transcription (`docs/circuit.md` and `docs/circuit/*.md`), the firmware notes
(`docs/firmware.md`, `docs/sequencer.md`) or the reference measurements (`docs/reference.md`) found
it wrong, they win:

- **Accent DACs (A2.2).** RS and HC share IC7/RA6 (address 4006) and the hi-hat has IC8/RA7 (4007),
  not the other way round; the firmware's addresses were right. R496 runs from IC26a's (-) input to
  -15 V: it doesn't load the clap's DAC. The 4174s' own output resistance (455 ohms typ at 5 V)
  moves the low codes by up to 4 % (`circuit/interface.md`).
- **Bass drum (2).** Q8 is a PNP switch, on at rest, not an emitter follower; R8 goes to -15 V; D8
  is a soft knee (0.82 V/V above about 3.2 V), not a 2.6 V clamp; ATTACK's range is 2.3x (R56), not
  10x (`circuit/bd.md`).
- **Hi-hat (4).** The closed region's first 65 samples aren't played: both real units keep an open
  hat sounding right up to the closed hat's sound, which starts at 6041h (`circuit/hh.md`). The
  "ANTI-LOG" of SN p.5 is IC67a/Q84, not the D/A (a plain resistor network). SN p.5's drawing
  labels the 4040 IC70 and the 4520 IC71; p.11 and the p.10 address map have them the other way
  round.
- **Cymbals (4).** The ride's counter doesn't match the crash's (IC58b, a 4520, counts first, then
  IC55); playback is the same. The accent is 6.7 dB/V once the other cymbal's load on IC9 is
  counted, not 9.0 (`circuit/cymbals.md`).
- **The AudioRealism pack (7).** Its grid is played as accented steps at TOTAL ACCENT 0 (DAC code
  21 on the analog voices), and its "accentNNN" takes are a TOTAL ACCENT sweep on an accented step
  (`reference.md`, "Accent").
- **Sequencer (5).** Sync mode steps INT -> MIDI -> DIN, while stopped only. Flam offsets are f+4 or
  f+5 group periods of about 4.07 ms (15.9-49 ms), against EchoBoy's 10-38 ms (`sequencer.md`).
- **Noise (A2.1).** The clock is about 320 kHz with typical parts (280-340 kHz over their spreads),
  not 455 kHz: R185 into IC31b's input capacitance and the gates' delays slow it
  (`circuit/noise.md`).

## Contents

0. [Sources at a glance](#0-sources-at-a-glance)
1. [Recommended architecture (summary)](#1-recommended-architecture-summary)
2. [Analog voices I: bass drum, snare drum, toms](#2-analog-voices-i-bass-drum-snare-drum-toms)
3. [Analog voices II: noise, TRIG/ACCENT, rim shot, hand clap, mixer and outputs](#3-analog-voices-ii-noise-trigaccent-rim-shot-hand-clap-mixer-and-outputs)
4. [Sample voices: hi-hat, crash, ride and the sample ROMs](#4-sample-voices-hi-hat-crash-ride-and-the-sample-roms)
5. [CPU board, firmware, MIDI and sequencer timing](#5-cpu-board-firmware-midi-and-sequencer-timing)
6. [Existing emulations, clones and replicas](#6-existing-emulations-clones-and-replicas)
7. [Reference audio](#7-reference-audio)
8. [Table of main constants](#8-table-of-main-constants)
9. [Hardware revisions (serial-number changes)](#9-hardware-revisions-serial-number-changes)
10. [Open questions and measurements to take](#10-open-questions-and-measurements-to-take)

---

## 0. Sources at a glance

| what | where (local) | notes |
|---|---|---|
| Roland TR-909 Service Notes, 1st ed., 15 Jun 1984. https://archive.org/details/synth_Roland_TR-909_service_notes | `roland/TR-909_service_notes_A.pdf`, OCR `TR-909_service_notes_A.txt`, full-resolution page scans `roland/service_notes_pages/p01–p16.png` (6614×4672 and 8110×5732 px) | Contents: block diagram (p.4), circuit descriptions (p.5–6), change information (p.7), waveforms (p.9), switch/CPU board (p.10), **voicing board (p.11)**, jack/PSU boards (p.12), MIDI implementation (p.13–16). Second scan (lower resolution): `roland/TR-909_service_manual_B.pdf` |
| Roland TR-909 Owner's Manual. https://archive.org/details/synthmanual-roland-tr-909-owners-manual | `roland/TR-909_owners_manual.pdf`, `.txt` | operation, specs, MIDI chart |
| NEC µPD7810/7811 datasheets and user's manuals | `nec/` | see section 5 |
| Firmware notes, Roland service bulletins, disassembly | `firmware/` (including `disasm/`, `whittle/`) | see section 5 |
| Clone schematics and notes: 9090 (Trevor Page), e-licktronic Nava (Eagle netlist with Roland designators), Hexinverter, Tiptop, Behringer RD-9 (maffez), Schmitz | `clones/`, `emulations/code/e-licktronic_Nava-v1.0/` | Nava is a one-to-one copy of a late-revision voicing board: the best machine-readable cross-check of the schematic |
| Analyses on the web: network-909, Colin Fraser, Robin Whittle, Normal Sounds, Jacob Korn, Electric Druid | `web/`, `samples/`, `emulations/web/` | |
| Sound On Sound "Synth Secrets" (Gordon Reid, 2002): bass drum (two parts), snare (two parts), cymbals, cowbells/claves | `sos/` | qualitative; some errors (see sections 2 and 4) |
| Kurt James Werner: TR-808 bass drum (DAFx-14), cymbal (ICMC-14), cowbell (AES 137), PhD thesis (Stanford 2016) | `academic/` | method templates; **nothing peer-reviewed exists on the 909's circuits** |
| Roland Cloud TR-909, TR-8, TR-8S, TR-09 manuals and MIDI charts; Behringer RD-9 manual; reference copies of open-source code | `emulations/` | see section 6 |
| Hitachi HN61256P datasheet; Colin Fraser's cymbal pages and WAVs; Hoshiai interviews | `samples/` | see section 4 |

**Searched with nothing 909-specific found:**

- Eric Archer: TR-808 clones only.
- Yusynth and Barton Musical Circuits: no 909 voice modules.
- Erica Synths: no 909 voice clone; its drum modules are original designs.
- DAFx, AES, ICMC and arXiv: no circuit-level 909 paper.
- KVR t=213411 and Elektronauts "Synthesizing a 909 kick": no measurements.

**Blocked:**

- Gearspace and Mod Wiggler threads were mostly unreachable (Cloudflare 403). Some were read through older Wayback copies. Others survive only as search snippets, and are marked as such where used.

## 1. Recommended architecture (summary)

The rows below summarise sections 2–5; details and sources are there. In this section, "meas." means our measurements on real-909 recordings (mostly the AudioRealism 96 kHz knob-sweep pack, cross-checked on the 1995 Rob Roy unit).

| block | recommended model | key numbers | rate / anti-aliasing |
|---|---|---|---|
| **CPU and firmware** | Run the v4 ROM (v1 selectable) on a cycle-counted µPD7810 core ported from MAME's `upd7810`. The 7811 is strapped MODE0 = MODE1 = 1, i.e. a ROM-less 7810, so its mask ROM is unused. 12 MHz crystal, 250 ns states. Memory map: 0000–1FFF EPROM, 2000–2FFF RAM Bank I, 3000–3FFF RAM Bank II (TC5565 with battery), 4000 key matrix, 4001–4008 accent DACs, 4009/400A trigger latches, 6000–7FFF M-64C cartridge (same format as RAM; sense on AN1). Peripherals that must be exact, because the firmware derives all its timing from them: ADC (AN0 TEMPO, AN3 TOTAL ACCENT; 192 µs INTAD in 4-channel scan), timers, event counter, UART. | MIDI 31 250 baud 8N1; tempo period ⌊1 099 138/B⌋ µs per 48-ppqn tick, B = 32 + ⌊225·ADC/256⌋ → 36.4–290 BPM in ≈1.14 BPM steps (crystal-timed, ±1-count ADC hysteresis) | lock-step with audio, cycle-scheduled inside each block |
| **Voice interface** | 8 accent latches, each a 4174 driving an RM0621 6-bit resistor DAC: V = 5·code/63, 2.54 kΩ source, held until rewritten. Order 4001–4008: BD, SD, LT, MT, HT, RS+HC (shared), HH (shared by CH/OH), crash+ride (shared). TRIG1 (4009): b1 BD … b5 HT. TRIG2 (400A): b1 RS, b2 HH, b3 CLOSED/OPEN level, b4 crash, b5 ride, b6 HC. | Triggers go out only on the INTAD scan tick: every 10th INTAD, 1.92 ms nominal, **probably ≈2.11 ms** because the handler absorbs one request. Two alternating groups (BD/SD/LT/MT, then HT/RS/HC/HH/CY), so each voice sits on a ≈4 ms grid. The trigger pulse is one tick (the "2 ms"). This is the source of the 909's timing jitter: 4.5 ms internal, 6.4 ms DIN, 7.8 ms MIDI max (published). | event-driven, sample-accurate |
| **Noise** | One shared 31-bit LFSR (x³¹ + x¹³ + 1; two 4006s give 36 stages, taps at 13 and 31), XOR-clocked, all-ones start. It feeds the BD attack, SD snappy, tom noise and the clap, so SD and CP phase against each other as on the real machine. | clock 300–455 kHz (unmeasured); ±7.5 V bit stream | integrate the bit stream over each internal sample (box filter) |
| **Bass drum** | Triangle VCO: integrator plus Schmitt comparator, ±4.3 V thresholds, hard reset at each trigger. Its frequency is **linear** in a CV built from C9: 2 ms charge through 2.2 k, discharge through (22 k + TUNE) ∥ 330 k. The triangle then goes through a 10 k + anti-parallel-diode shaper into a single-transistor VCA (≈0.5 V threshold). ENV1 is hold-then-decay (Q3/Q4 switch, clamp ≈2.6 V). Attack: 2 ms pulse → 5.3 kHz Q 3.4 Sallen–Key plus 339 Hz-LP noise → ENV2 VCA, with ATTACK a 500 Ω rheostat. | f = 37.3 Hz/V·\|V_A\|; f∞ ≈ **49.5 Hz** meas. (56 Hz calc, so calibrate); f(0) ≈ 275 Hz; sweep τ **7.4–29 ms** over TUNE (meas. = calc); body hold ≈60–68 ms, then τ 14–107 ms (calc) over DECAY; accent caps at +6.6 dB | 4× oversampling for the shaper and VCA (or ADAA on the diode pair) |
| **Snare** | Two triangle VCOs whose 4069 inverter rails are driven to ±V_cv, so f ∝ V_cv. The CV gets a ×2 bump that decays with τ ≈ 5.8 ms (the "20 ms pitch bend"). Diode shapers feed transistor VCAs (τ ≈ 21–24 ms). Snappy: noise → 8.5 kHz LP → path A (1.5 kHz HP, VCA held flat for 24 ms, then τ 32–76 ms toward −0.1·V_acc; TONE sets this decay) plus path B (4 kHz HP, very short). SNAPPY is the feedback pot of the noise summer. | VCO-1 117→231 Hz, VCO-2 176→346 Hz over TUNE (meas.); ratio 1.44–1.50 depending on the unit; the calc is ≈18 % low (open question); TONE has no effect when SNAPPY = 0 | 4× |
| **Toms** | Three triangle VCOs per tom, in the ratio 1.5 : 1 : 2.7. The pitch envelope acts only on V_SS (τ 22 ms and 220 ms), so the start is ×1.35–1.52 above the settled pitch. VCO-1 has a variable clipper (Q23, soft-to-hard over 6–10 ms). DECAY acts only on the lowest VCO (τ 26–261 ms, with a slow swell). A shared, band-passed tom-noise bus with a constant leak (R194). | VCO-1: LT 61–122, MT 75–149, HT 90–179 Hz (calc, within 1–6 % of meas.) | 4× |
| **Rim shot** | 2.2 V / 0.26 ms excitation into three bridged-T resonators (op-amp, non-inverting), **with op-amp rail clipping inside each loop** → resistor sum → ±0.6 V anti-parallel diode clipper → Q65 VCA (τ ≈ 47 ms) → level stage → Sallen–Key HP (229 Hz, Q 1.58, ×2). | 495 / 219 / 1053 Hz, Q 7.3 / 6.1 / 7.3 (calc); meas. 478 / 223 / 1014 Hz; R417 12 k → 3.3 k at SN 415300 | 4× |
| **Hand clap** | Shared noise → MFB band-pass → BA662 OTA, driven by the 808-style AN6912 sawtooth generator. That generator is event-driven: piecewise-exponential RC segments plus comparator events and a final kick at about 31.7 ms. A second "reverb" path: band-pass ≈78–440 Hz into the Q37 VCA with a two-node RC envelope. TM is the only trimmer in the machine. | BP 963 Hz, Q 2.13; hits at **0, 10.5, 22.3, 32.5 ms** (meas.; C65 at −15 % reproduces this); tail τ ≈ 82–99 ms meas. (123 ms calc) | 2–4× |
| **Hi-hat, crash, ride** | The ROMs are already in playback order: sample = `byte >> 2`, offset binary (0x80 = 0). Model the counters literally: OH reads 0x0000–0x5FFF and stops at 0x6000; CH forces A13/A14 high and reads 0x6000–0x7FFF; cymbals read all 32 K. Signal path: zero-order-hold DAC → 10 µF/22 Ω → transistor VCA → **two Sallen–Key stages (≈4th-order Butterworth at 26.8 kHz, which pass the first image band)** → level stage → jack RC. HH envelope: 2 ms hold, then the C135 RC. Crash/ride envelope: a 6-bit DAC on address bits A9–A14. Accent is exponential in V_acc. | HH clock **31.57 kHz** fixed; crash/ride **28.06 → 36.7 → 53.8 / 56.1 kHz** over TUNE (meas., two units); τ_OH 69–183 ms (calc), 77–202 ms (meas.); τ_CH 8.7–69 ms (calc), depends on the OH knob; cymbal gain ((63 − n>>9)/63)^1.10; accent 6.4 dB/V (HH), 9.0 dB/V (cymbals); HH C134 high-pass 0.72 Hz → **723 Hz** from SN 415300 | Band-limited ZOH steps (BLEP) at ≥2× the host rate; keep the images between 15.8 and 27 kHz |
| **Mixer and outputs** | Per voice: 1 k series, then 10 nF to ground on SN ≥ 415300 (a 15.9 kHz pole), then the jack. Plugging a jack removes the voice from the mix. Fixed pan resistors into two inverting summers (C153/C154 1 µF give a ≈130 Hz HP), then the MASTER rheostat, then the output network (≈6.6 kHz LP). | Pan: BD, SD, MT, RS and CP centred; LT, HT, HH and ride ±5.2 dB; crash 1.6 dB (L/R orientation unconfirmed). Main outs **phase-inverted** relative to the individual outs. Master out 6 Vp-p at 1 kΩ. | host rate (linear) |
| **Revisions** | Expose switches for the documented production changes: the BD C9, the rim shot R417, the HH C134 and the multi-out capacitors, the crash/ride oscillator series resistor, the tom-noise parts and R495. | see section 9 | — |
| **Sequencer behaviour** | Do not reimplement it: the firmware provides it. Use these as validation targets (from the disassembly, not yet run). | 48 ppqn, 12 ticks per 16th. Shuffle splits a pair of 16ths as 12+s / 12−s ticks, s = 0…6 (50–75 % swing). Flam = (f+4 or f+5) group ticks for f = 0…7, ≈15–51 ms by code reading; one scope measurement gives 10–38 ms, unresolved. Accent code = table[(base + TA) >> 1] >> 1, with TA = TOTAL ACCENT ADC >> 3 (0–31); analog normal/accent codes 10/21 at TA = 0, 21/47 at full TA. RS/HC (base 50h) and OH/CR/RD (40h) have the global accent only. MIDI: rx ch 10 (omni), notes 35–51; tx ch 11, Ext-instrument rows only. | — |

Rationale:

- **Analog voices.** Nearly every analog voice is RC networks plus triangle VCOs, diode shapers and single-transistor VCAs. Component values predict the pitches to within a few per cent with no fitting (toms 1–6 %, BD sweep 10 %). So model the circuits from their values, and use the recordings to calibrate a few per-unit constants: comparator thresholds or V_sat, the VCA threshold, C65, the noise clock.
- **Hi-hat, crash and ride.** These are almost fully determined by the ROM data, two measured clock rates, and the RC/VCA networks. Their character comes from the 6-bit zero-order hold and its images, which the 26.8 kHz filter barely removes, and from the address-driven cymbal envelope.
- **Firmware.** Timing quirks (the ≈2 ms trigger scan with alternating groups, tempo-ADC hysteresis), the accent arithmetic and the MIDI behaviour all come from the firmware. That is why we emulate the CPU rather than rewriting the sequencer.

---

## 2. Analog voices I: bass drum, snare drum, toms

Scope: circuit analysis of the BD, SD and LT/MT/HT voices, with every number found. Rim shot,
hand clap, the noise generator, the accent DACs, the mixer and the outputs are in the A2 section.

How this section was built:

- **Primary source: the Roland service notes** [A1-1] (16 pages, first edition, 15 Jun 1984).
  - p4 block diagram, p5–6 circuit descriptions, p7 change information.
  - p9 "waveforms observed at MULTI OUT with all knobs at centre" (hand-traced scope drawings).
  - p11 voicing-board schematic (VG 73133090, PCB 2291084903 3/3).
  - All designators below are Roland's own, read from crops of the 8110×5732 px scan.
- **Cross-checks:**
  - Network-909's annotated schematic crops and descriptions [A1-2].
  - Trevor Page's 9090 clone schematics, which are vector PDFs and give transistor polarities unambiguously [A1-3].
  - Jörg Schmitz's hand-redrawn BD [A1-4].
  - The Colin Fraser [A1-5] and Robin Whittle [A1-6] mod write-ups.
- **"My calc"** marks numbers derived from component values. The formulas are given inline.
- **"Meas."** marks numbers I measured from real-909 recordings already collected in
  `reference/real909/`. The main set is AudioRealism's 96 kHz/24-bit multisample pack with stated knob
  positions [A1-20]. Zoe Blade's RobRoy 1995 set [A1-21] and an archive.org set [A1-22] are used as
  second and third units. Method:
  - BD: zero-crossing period tracking after a 600–700 Hz low-pass, then a least-squares fit of
    f(t)=f∞+A·e^(−t/τ).
  - SD and toms: windowed FFT peak picking.
  - The scripts are in `reference/real909/packs/_measurements_forkA1/scripts/`.
- **Shared building blocks** (identical in every analog voice; described once here):
  - **Op-amps:** M5218L dual (4558-class) on ±15 V [A1-1 parts list]. I assume Vsat ≈ ±13.5 V.
  - **Triangle VCOs (all voices):** integrator plus Schmitt comparator.
    - In SD and toms, the integrator's input "switch" is a CMOS 4069UB inverter (IC36, IC16, IC22, IC44)
      whose supply pins are driven to ±V_cv. The inverter output is then ±V_cv and the
      frequency is ∝ V_cv. Roland calls these "inverting buffers configured as
      voltage-dependent resistor".
    - In the BD, the integrator is driven by a ±1 polarity switcher (IC12b + PNP Q10) instead.
    - Every VCO is **hard-reset at each trigger**: a transistor across the integrator cap,
      plus a diode that forces the comparator state. Every hit therefore starts at the same phase.
  - **Waveshapers:** the triangle goes through 10 kΩ into an anti-parallel 1SS133 diode pair to
    ground. That node then drives 1.5–2.2 kΩ into a 47 Ω resistor to ground, the VCA input node.
    Result: a soft-clipped "sine-ish" wave, then a ≈1/48 attenuator.
  - **VCAs:** single transistors with the signal on the emitter side (47 Ω node) and the other
    terminal into an op-amp virtual ground. Base current comes from an RC envelope through
    220–470 kΩ.
    - The transistor works near V_CE≈0 as a conductance ∝ base current. It passes nothing until the
      envelope is above ≈0.4–0.6 V (Whittle [A1-6]).
    - The VCA transistors are marked "(S)" on the schematic: Q12, Q6, Q50, Q51, Q19–Q21, Q26–Q28,
      Q57–Q59. The legend I could read does not define "(S)".
    - The parts list adds 2SC2878-A/B to the generic 2SC2603/2SA1115. The schematic tags Q82/Q83
      (master mute) as 2SC2878. Q47/Q48 (snare noise) are tagged 2SD1469.
    - Trevor Page: the 2SD1469 parts were chosen for low V_CE(sat). A BC549 works, but the
      snappy is quieter [A1-3].
  - **Accent:**
    - Each voice has a non-inverting op-amp of gain 1+10k/4.7k = **3.13**:
      IC11b (BD), IC34b (SD), IC18b/IC23b/IC45b (LT/MT/HT).
    - Its input is that voice's 6-bit accent DAC output: a 4174 latch into an RM0621 binary-weighted
      network with R=5 kΩ (5k–160k), Thevenin 2.54 kΩ.
    - The tom stages add 220 kΩ to −15 V at the + input: R118/R178/R381, about −0.17 V offset.
    - The output is gated by PNP switches into the envelope caps. A2 covers the DAC itself.

### Bass drum

#### Circuit, block by block (designators from [A1-1] p11, roles from p4–6 and [A1-2])

1. **Trigger and gating.**
   - TRIG (+5 V, 2 ms, from latch IC1) → R1 22k (R2 10k to ground) → Q1 (NPN).
   - Q1's collector (node N1) pulls the base of PNP Q2 low through D4 / R3 100k (R4 47k pull-up;
     C6 2.2 nF + R11 4.7k speed-up).
   - Q2's collector (node N2, loaded by R19 2.2k) therefore swings to ≈+15 V for the trigger
     duration. **N2 does not depend on accent.**
   - N1 also feeds:
     - C1 0.068 µF + R12 100k into the base of PNP Q7 (R13 47k pull-up), a VCO-reset pulse that
       is AC-coupled;
     - R34 100k / D5 to the base of PNP Q5 (the accent gate for ENV2).
   - Polarities are confirmed from the 9090 redraw [A1-3]: Q2, Q7, Q4, Q5 and Q10 are PNP (BC559 in the 9090).
2. **Pitch envelope ENV3 and TUNE.**
   - N2 → D7 → R21 2.2k → C9 0.33 µF (tantalum; **0.22 µF before serial 381500**, enlarged
     "for expanding the TUNE range" [A1-1 p7]).
   - Discharge: R57 22k + VR2 100k(A) TUNE (rheostat), in parallel with R26 330k.
   - TUNE therefore sets only the **decay** of the pitch sweep, not the base pitch. Colin Fraser [A1-5] and
     Whittle [A1-6] agree.
3. **CV generator (IC12a/b).**
   - IC12a: inverting summer with R26 330k from C9, R27 1.5M from +15 V and R28 150k feedback.
     V_A = −(0.4545·V_C9 + 1.5 V).
   - IC12b: R29/R30/R31 100k with PNP Q10 grounding its + input. This is a **×(+1)/×(−1) polarity
     switch** driven from the comparator output (R23 100k, R24 22k).
   - Q10 must be PNP (it shorts a negative node), or the loop would latch. The 9090 confirms it
     (BC559) [A1-3].
4. **VCO (IC13a/b).**
   - IC13a integrator: R59 47k, C14 0.033 µF.
   - IC13b comparator with hysteresis: R49 100k feedback, R50 47k to ground, so
     thresholds = ±Vsat·47/147 ≈ **±4.3 V** (my calc).
   - Output is a symmetric triangle of ≈8.6 Vpp.
   - **f = |V_A| / (4·V_th·R59·C14) ≈ 37.3 Hz per volt of |V_A|** (my calc). So f is *linear* in
     V_C9, and the sweep is exponential in Hz, not in log-frequency.
   - Reset: Q7 → R47 330k → base of Q11 (discharges C14 through R51 2.2k), and R48 22k / D12
     force the comparator's + input high. This is network-909's "positive pulse at the base of Q11,
     discharging C14 and resetting VCO at every TRIG".
5. **Waveshaper.** Integrator output → R52 10k → D10/D11 anti-parallel to ground → R53 2.2k →
   R43 47 Ω.
   - Without the diodes the node would peak at 0.79 V (my calc). With them it is clipped to about
     ±0.55–0.6 V, which rounds the triangle (Whittle: "turn on around 0.5 to 0.6 V").
   - Replacing R52 with a smaller resistance is the classic "drive/overdrive" mod: 10k pot + 1k [A1-5, A1-3].
6. **Body VCA Q12 and ENV1 (DECAY).**
   - Q12's base is fed through R54 470k from C8 0.33 µF (T).
   - **ENV1 is not a plain AR**:
     - N2 → D6 → R20 6.8k charges C7 0.033 µF.
     - C7 drives NPN Q3 (R18 470k, R17 470k), and Q3 pulls the base of PNP Q4 low (R32 47k, R33 1M).
     - Q4 connects IC11b's output (the accent voltage ×3.13) to R35 1.8k → D9 → C8.
     - C8 is therefore **held at the accent level for as long as C7 keeps Q3 on**, then decays
       through VR5 1M(A) DECAY + R58 47k (and into Q12's base through R54).
   - Network-909 draws ENV1 as "looks like a compressed drum sound … the reason for the big
     punch" [A1-2].
   - Jörg Schmitz's clone adds a "compression" pot exactly on C7/R18 [A1-4].
   - **Clamp:** R36 47k / R22 10k divide +15 V to 2.63 V, and D8 clamps the R35/D9 junction, so
     C8 cannot exceed ≈2.6 V (my reading). Very high accents therefore saturate the body level.
     Meas. supports this: the body level stops rising above accent 90 %, as shown below.
   - IC11a sums the two VCA currents. VR4 100k(B) LEVEL is its feedback resistor, so level is linear
     in the resistance.
7. **Attack: pulse and noise, VCA Q6, ENV2 (ATTACK).**
   - *Pulse:*
     - N2 → R5 100k with C5 6.8 nF → PNP emitter follower Q8 (R6 22k to +15, R14 22k) gives a
       rounded 2 ms pulse, τ≈0.68 ms.
     - C2 3.3 nF + R7/R15 100k: high-pass, ≈241 Hz.
     - Q9 unity-gain Sallen–Key low-pass (R16 39k, C3 4.7 nF, C4 100 pF): **f0 ≈ 5.3 kHz,
       Q ≈ 3.4**. So the click is a ringing ~5 kHz burst. All my calc.
     - Then C11 0.47 µF → R44 22k.
     - Network-909 calls this "low pass filter (Q8 …) and a band pass filter (Q9 …)".
   - *Noise:*
     - Noise bus → R10 12k / R9 4.7k: a 0/15 V digital noise becomes **4.22 Vpp**. Hexinverter
       quotes "exactly 4.22 V amplitude white noise after the divider" [A1-9].
     - C15 0.47 µF → R46 4.7k / C13 0.1 µF low-pass at **339 Hz** (my calc) → R45 4.7k.
     - It is summed with the pulse at a node with R56 330 Ω to ground.
   - That node → VR3 500 Ω(B) ATTACK (rheostat) → R55 47 Ω → **Q6 VCA**.
     - ATTACK never reaches zero: VR3 changes the 47 Ω divider ratio by about 10×.
     - Meas.: the high-pass click RMS changes 2.9× between ATTACK 0 and 100.
   - ENV2: Q5 passes the accent voltage only while TRIG is high → R41 22k → C12 0.033 µF.
     Charge τ 0.73 ms; decay through R42 470k into Q6's base, τ≈15.5 ms.
     - **Serial numbers up to 371499** also have R495 150k, surface-mounted on the foil side,
       which shortens it to ≈3.8 ms (my calc; exactly which node R495 loads should be checked).
   - So the click and noise level follow accent, while the pitch envelope does not.

#### Numbers

| quantity | value | source |
|---|---|---|
| final (resting) pitch f∞ | **56 Hz** nominal (V_A=−1.5 V, V_th=4.32 V) | my calc |
| | **49.4–49.7 Hz** (AudioRealism unit, all TUNE settings); **49.4–49.5 Hz** (RobRoy unit); 52.4–52.9 Hz (archive.org Set2) | meas. [A1-20,21,22] |
| | "about 55 Hz" | Normal Sounds [A1-10] |
| | "about 52 Hz (G#)" | Gearspace thread (search snippet only; page blocked) [A1-15] |
| initial pitch at trigger end | 261–274 Hz (TUNE min–max; C9 charged to 12.0–12.9 V in 2 ms) | my calc |
| sweep amplitude A (f∞+A at t=0, t from onset) | **226–229 Hz** (AudioRealism, independent of TUNE); 211–222 Hz (RobRoy); ≈225 Hz (Set2). So f(0)≈275 Hz | meas. |
| pitch-sweep τ vs TUNE 0/25/50/75/100 % | **7.4 / 9.4 / 11.6 / 14.3 / 29.1 ms** (AudioRealism); RobRoy TUNE 0/3/7/10: 7.7 / 10.6 / 18.0 / 29.4 ms | meas. |
| pitch-sweep τ range | C9·((R57+VR2)‖R26) = **6.8 ms (min) … 29.4 ms (max)**; 4.5–19.6 ms with C9=0.22 µF | my calc (matches meas. end-points within 10 %) |
| "Tune range" | "from around 30 to 120 milliseconds" (an audible length, ≈4τ) | Colin Fraser [A1-5] |
| VCO reset | integrator shorted through 2.2k while TRIG is high (≈2 ms). The first zero crossing is ≈3.3 ms after onset | [A1-1]; meas. |
| triangle amplitude / shaper | ±4.3 V → 10k → diode pair → 2.25k: ≈±0.55–0.6 V → R53/R43 divider (÷48) → ≈±12 mV at the VCA | my calc |
| ENV1 hold (Q4 on) | C7·R18·ln(13.4/0.65) ≈ **47 ms** | my calc |
| | body flat to within ≈−2 dB until **≈60–68 ms**, then decays | meas. |
| ENV1 decay τ | C8·((VR5+R58)‖R54) = **14 ms (DECAY min) … 107 ms (max)** | my calc |
| | 39 / 48 / 64 / **102** ms for DECAY 25/50/75/100 % (fit 70 %→5 % of peak). DECAY 0: −6 dB at 67 ms, gone (−28 dB) by 77 ms | meas. |
| −20 dB / −40 dB points, DECAY 100 % | 282 ms / 432 ms | meas. |
| accent effect on body | ×2.13 (+6.6 dB) from accent 0→90 %; saturates at 90–100 % | meas. (AudioRealism "accent000–100") |
| accent effect on click | rises to accent 60–70 % then falls slightly. The overall peak saturates at accent ≥60 % | meas. |
| click filter | f0≈5.3 kHz, Q≈3.4; HP ≈241 Hz | my calc |
| noise in attack path | 4.22 Vpp before filtering; LP 339 Hz | my calc; [A1-9] |
| ENV2 (attack VCA) | τ_charge 0.73 ms, τ_decay ≈15.5 ms (≈3.8 ms on early units with R495) | my calc |
| R59 sensitivity | f ∝ 1/R59. Replacing it with 40k + 25k pot gave 39–66 Hz | Normal Sounds [A1-10] (consistent) |
| useful R59 limit | "around 100k" | Mod Wiggler t=227027 [A1-12] |
| Roland's own BD drawing (p9) | 200 mV/div, 5 ms/div, "with accent" ≈2× "w/o accent"; ≈6 flat cycles then decay. Hand-traced with no graticule, so the time axis cannot be scaled | [A1-1] |

#### Disagreements and corrections

- **Waveform.** Sound On Sound [A1-7] says the oscillator "produces a sawtooth wave". The
  schematic and Roland's text say triangle: an integrator plus Schmitt comparator with a symmetric ±1 polarity switch. So do
  network-909, Tiptop [A1-8] and Whittle. SOS also calls ENV1 a plain "AR envelope"; the
  hold-then-decay ENV1 above is the more accurate description.
- **TUNE.** Some web summaries call TUNE a pitch control. It is purely the pitch-sweep decay
  time. f∞ is fixed by R27, R28, R59, C14 and the comparator thresholds. That is why every
  mod adds a separate "pitch" pot [A1-5, A1-6, A1-3].
- **Unit-to-unit f∞.** It varies from 49.5 to 55 Hz across the units I found. My nominal calc
  (56 Hz) assumes Vsat=13.5 V. A higher op-amp swing or component tolerance lowers it. Treat
  f∞ as a per-unit calibration constant, default **≈50 Hz** to match the best-documented
  recordings.
- **ENV1 hold.** I calculate 47 ms but measure ≈60–68 ms. Q3's turn-off is soft and C8 then has
  to fall from ≈2.6 V to the VCA threshold. A SPICE run of Q3/Q4/C7/C8 would settle this.

#### Recommendation for our model

- **CV path:** model it as the linear control path above:
  - C9 RC (charge through 2.2k for the trigger length, discharge through
    (22k+VR2·taper(A))‖330k);
  - V_A = −(R28/R26)·V_C9 − (R28/R27)·15;
  - f = |V_A|/(4·V_th·R59·C14).
  - Calibrate V_th (or R27) to f∞=49.5 Hz, and check τ(TUNE) against the meas. table.
  - The A taper meas. gives VR2 ≈ 9k / 17k / 28k at 25/50/75 %.
- **VCO:** run it as a true triangle integrator with hard reset at trigger (the integrator held at 0 V
  for the trigger length), not as a free-running phase. This reproduces the identical first cycle
  of every hit.
- **Waveshaper:** solve it as a static nonlinearity: 10k source into an anti-parallel diode pair,
  Is≈2–4 nA, n≈1.9 for 1SS133, in parallel with 2.25k. Oversample ≥4× or use ADAA, because the
  overdrive mods push it hard.
- **VCA:** model the single-transistor VCA as a base-current-controlled conductance with an
  ≈0.5 V threshold. The simplest form is g = k·max(0, V_env−V_BE)/470k. Better: an Ebers–Moll
  transistor at V_CE≈0, so the slight asymmetry between half-cycles is reproduced. Meas. shows
  alternating half-cycle peaks of ≈0.72 and 0.9–1.0, i.e. strongly asymmetric output.
- **ENV1:** model it as C7 driving a Q3/Q4 switch, with the accent voltage clamped near 2.6 V, then
  the C8 RC decay. Calibrate against meas.: hold ≈60–68 ms, τ_decay 14–107 ms over DECAY.
- **Attack:** the 2 ms trigger pulse through the RC/HPF/Sallen–Key filter (5.3 kHz, Q 3.4) plus
  339 Hz-low-passed shared noise, both through the ENV2 VCA, scaled by the ATTACK divider.
- **Revision options:** C9 = 0.22 µF (SN < 381500); R495 present (SN ≤ 371499).

#### Open questions

- Is "(S)" a selected-hFE part or 2SC2878? It matters for the VCA nonlinearity.
- How long is the reset on Q7 after TRIG ends? It depends on how fast N1 recovers through
  D4/R3/R4 against C1.
- Which node does R495 load: the base of Q6, or C12?
- Is the D8 clamp level (≈2.6 V on C8) exact? It sets the maximum body amplitude.
- What is the accent DAC voltage range? This is A2's area, but it decides how close ordinary
  accents come to the clamp.

### Snare drum

#### Circuit

1. **Trigger.**
   - TRIG → R239 22k (R242 10k) → Q39 (NPN) → PNP Q40 (R244 10k, R246 22k, R245 100 Ω emitter)
     gives a +15 V pulse. Note on p10: "On early PCBs two Q-40's exist, one of which is denoted as
     Q-86 on later PCBs".
   - The pulse:
     - resets both VCOs (D62/R275 10k and D63/R274 10k force the comparators; Q44/Q45 discharge
       C69/C71 through R263/R268 10k);
     - mutes VCO-1's VCA through Q46 (R281 100k, R282 22k), "muting unwanted noises in the VCO-1
       path" [A1-1 p6];
     - starts ENV1 through D56.
2. **Pitch CV (IC35a/b).**
   - TUNE: VR6 10k(B) between R287 100k (to +15) and R284 10k (to ground). The wiper spans
     **1.25–2.50 V**.
   - The wiper → R249 100k, and ENV1 (C66 0.01 µF charged from the Q40 pulse through R247 47k) →
     R248 470k, are summed at IC35a's + input.
   - IC35a (follower + R250 1k) supplies **+V_cv** to the 4069 (IC36) V_DD pin. IC35b (inverter,
     R251/R252 100k, + R253 1k) supplies **−V_cv** to V_SS. D57 RD18JB2 (18 V zener) protects
     V_DD−V_SS.
   - Roland's own figure [A1-1 p6] shows V_DD stepping from ≈2 V to ≈5 V during the 2 ms trigger and
     returning over ≈20 ms. V_SS mirrors it.
   - My calc: charge τ 0.47 ms, decay τ ≈5.8 ms, peak CV ≈4.0 V at TUNE centre.
   - Whittle: the pulse "is the same for all SD notes, and is not related to the accent level" [A1-6].
3. **VCO-1 / VCO-2 (IC37, IC38).**
   - Comparators IC37a/IC38a with 100k/100k hysteresis give thresholds ±½Vsat ≈ ±6.75 V.
   - Each comparator drives its inverter gates' inputs (R259/R270 100k). The gate outputs
     (±V_cv) → R261/R264 47k → integrators IC37b/IC38b with **C69 0.01 µF / C71 0.0068 µF**.
     Feedback to the comparator is through R269/R273 100k.
   - My calc: **f1 = 78.8 Hz/V·V_cv, f2 = 115.9 Hz/V·V_cv**. The nominal ratio is 1.47.
4. **Shapers and VCAs.**
   - VCO-1: R283 10k → D65/D66 → R288 1.5k → R289 47 Ω → Q50 (S).
   - VCO-2: R305 10k → D69/D70 → R304 2.2k → R303 47 Ω → Q51 (S).
   - VCO-1 envelope "ENV3": D68 → C83 0.1 µF → R291 220k → C70 4.7 nF → R290 22k → base of Q50.
     τ≈24 ms, with a ≈1 ms smoothed onset.
   - VCO-2 envelope "ENV2": D67 → C82 0.047 µF → R295 220k → C79 0.01 µF → R293 220k → base of Q51.
     τ≈21 ms.
   - Both are charged from the accent-gated pulse (Q41 passes IC34b's output during TRIG), so the
     levels follow accent.
5. **Snappy (noise).**
   - Noise → R286 10k / R285 3.3k (÷4) → C85 15 nF (high-pass ≈48 Hz with R302 220k) → IC40a
     Sallen–Key low-pass (R301/R300 22k, C84 220 pF, C81 3.3 nF): **f0 ≈ 8.5 kHz, Q ≈ 1.9** (my calc).
   - Path A ("ENV4", the main snappy):
     - C80 22 nF into R299 4.7k + R298 47 Ω: high-pass ≈1.5 kHz → Q48 (2SD1469) VCA.
     - Base through R297 220k from C67 0.47 µF/50 V **bipolar** (ENV4).
     - ENV4 is charged from the accent voltage through PNP Q42 / D60. Q42 stays on for a
       **flat ≈24 ms** after the trigger (C68 6.8 nF / R256 1M; Whittle measured 24 ms and
       removes C68 in his mod).
     - It then decays through VR7 500k(B) **TONE** + R254 100k, **towards IC34a's output, which
       is −0.1×V_accent** (IC34a: R255 100k in, R243 10k feedback). This is why C67 is bipolar and
       why the decay is "faster than … exponential … to a negative voltage" [A1-6].
     - D59 clamps ENV4 at about −0.6 V.
     - My calc: τ ≈ 32 ms (TONE min) … 76 ms (max), including R297's load.
   - Path B ("ENV5", the high snap):
     - IC39a Sallen–Key **high-pass** (C76/C75 2.2 nF ×2, R277 47k, R276 6.8k): **f0 ≈ 4.0 kHz,
       Q ≈ 1.3** (my calc) → R278 2.2k / R279 47 Ω → Q47 (2SD1469).
     - Base through R280 220k from C73 470 pF, charged through D64 from the accent-gated pulse.
       Q43 (R266 100k, R265 22k) also acts on this node.
     - Very short (C73·R280 ≈ 0.1 ms plus the trigger length). Whittle calls it "quiet and very
       brief … inaudible in the presence of [path A]" [A1-6]. Roland: "ENV 5 determines the amount
       of high frequency noise components in the SNAPPY" [A1-1].
   - IC39b sums paths A and B with **VR9 10k(B) SNAPPY as the feedback resistor** (C77 10 pF) → C78
     0.47 µF → R294 47k into the output mixer IC40b. The tone VCAs (Q50, Q51) feed IC40b directly.
     VR8 50k(B) LEVEL is the feedback resistor.

#### Numbers

| quantity | value | source |
|---|---|---|
| VCO-1 resting frequency vs TUNE 0/25/50/75/100 % | **117 / 145 / 174 / 201 / 231 Hz** (AudioRealism) | meas. [A1-20] |
| VCO-2 resting frequency | **176 / 217 / 260 / 301 / 346 Hz** (AudioRealism) | meas. |
| other units | RobRoy: VCO-1 131→234 Hz over TUNE 0→10, ratio 1.439–1.444. Echo Sound Works: ratio 1.448–1.455. archive.org Set2: VCO-1 111–218 Hz, ratio **1.500** | meas. [A1-21,22,23] |
| TUNE span | ×1.97 (meas., AudioRealism) vs ×2.00 (my calc, wiper 1.25–2.5 V) | |
| nominal VCO frequencies | VCO-1 99 / 148 / 197 Hz, VCO-2 145 / 217 / 290 Hz (TUNE min/centre/max) | my calc (Vsat 13.5 V). Meas. is ≈18 % higher in all three units; cause unknown, see Open questions |
| VCO-2 / VCO-1 | 1.47 nominal (C69/C71) | my calc |
| | **1.500 ± 0.003** in 2 of 4 units (AudioRealism at all 5 TUNE settings; archive.org Set2); 1.44–1.455 in the other two | meas. |
| pitch bend | CV ≈2 → 5 V (×2.5) over 2 ms, back in ≈20 ms | [A1-1 p6] figure |
| | ×2.1 peak at TUNE centre; τ≈5.8 ms | my calc |
| | e.g. TUNE 50: dominant peak 258 Hz (0–10 ms) → 210 Hz (10–20 ms) → 177 Hz (20–35 ms) → 174 Hz | meas. |
| snappy flat-top | ≈24 ms before decay | Whittle [A1-6] scope (10 ms/div) |
| snappy (HP > 2.5 kHz) decay vs TONE 0/25/50/75/100 % | −10 dB at 53 / 86 / 101 / 88 / 111 ms; −20 dB at 82 / 142 / 169 / 175 / 202 ms; −40 dB at 98 / 178 / 225 / 254 / 289 ms | meas. (single takes; TONE 75 % is out of order) |
| snappy decay τ | 32–76 ms towards −0.1·V_acc | my calc |
| noise filters | in: LP 8.5 kHz (Q 1.9), HP 48 Hz; path A HP 1.5 kHz; path B HP 4.0 kHz (Q 1.3) | my calc |
| noise level into snappy filter | ≈3.7 Vpp (15 V × 3.3/13.3) | my calc |
| SNAPPY / LEVEL | linear transimpedance pots (10k B / 50k B) | [A1-1] |

#### Disagreements

- **Which envelope follows accent.** Whittle: ENV5 is "the same irrespective of the accent level"
  and its VCA is "Q41". Roland: Q41 is the accent *gate* and ENV5 is accent-derived. The
  schematic shows D64 fed from the Q41 path, which supports Roland. Whittle's ENV4 description
  (accent sets the start via D60, 24 ms flat, negative target) matches the schematic.
- **Partial frequencies.** SOS [A1-7] models the snare's "0,1 mode" partials at 180 and 330 Hz
  (theory, not a 909 measurement). The 909 at TUNE centre gives ≈174 / 260 Hz (meas.).
- **Waveshaping depth.** RD-9 modder maffez finds the clone's wave shaping softer than a real 909,
  and that real 909 snares/toms have "more body". He adjusts R633/R634 (= TR-909 R288/R289 area)
  [A1-11]. This is a hint that shaper drive and VCA levels need calibrating against recordings.

#### Recommendation for our model

- **VCOs:** model both as integrator plus Schmitt triangles whose integrating current ∝ V_cv. Include
  the shared V_DD/V_SS rails with their **1 kΩ source impedance (R250, R253)** and the CMOS output
  resistance. This is the plausible mechanism for the 3:2 locking seen in some units (my
  hypothesis). Expose a "VCO-2 ratio/lock" calibration that ranges from free-running 1.44–1.47 to
  locked 1.5.
- **Envelopes:** use the RC networks above, gated by the 2 ms trigger and accent. ENV4 needs the
  C68 hold and the negative decay target (−0.1·V_acc). The VCAs use the same
  threshold/conductance model as the BD.
- **Noise:** feed the noise from the shared 32-stage LFSR model (A2), not white noise. The
  snare and the clap share it, and the AudioRealism pack documents the resulting phasing between
  SD and CP on the master output [A1-20 Readme].

#### Open questions

- Why are measured SD frequencies ≈18 % above my calc when the tom calc matches meas. within 5 %?
  - Candidate: an asymmetric comparator threshold. At rest D62/R275 may be forward-biased,
    depending on where the Q40 collector rests. If so, the lower threshold rises from −6.75 V
    towards ≈−1.6 V.
  - Check in SPICE. This also decides the triangle's symmetry.
- What drives Q43, and what exactly is ENV5's timing?
- Which units lock at 3:2, and why? A SPICE simulation of the IC35 rail coupling would settle it.

### Toms (LT / MT / HT)

All three are identical in topology (Roland: "similar circuitry except that VCO's have different
main tunings"). Designators below are for **MT**, with LT and HT equivalents in the table.

#### Circuit

1. **Trigger.** TRIG → R154 22k (R155 10k) → Q25 (NPN).
   - A non-accent pulse via Q24 (R151 10k, R153 22k, R152 100 Ω): ENV4 and the VCO resets.
   - An **accent-gated** pulse via Q29 (R157 100 Ω emitter from the accent amp IC23b; R156 100k,
     D42, R158 100k): the VCA envelopes.
2. **Pitch CV (IC20a/b) and ENV4.**
   - TUNE: VR13 10k(B) between R172 68k (to +15) and R173 10k (to ground). The wiper spans
     **1.70–3.41 V**.
   - IC20a (follower + R120 1k) drives **V_DD = +V_tune**.
   - IC20b inverting summer (R127 100k from V_DD, R121 2.2M from C30 0.1 µF, R122 470k from C31
     0.047 µF, R125 100k feedback from after R126 1k) drives **V_SS = −(V_tune + 0.045·V_C30 +
     0.213·V_C31)**.
   - C30 and C31 are charged to ≈14 V by the trigger through D37/D36. They discharge into
     IC20b's virtual ground with **τ = 220 ms (C30·R121) and 22 ms (C31·R122)** (my calc).
   - The same values (0.1/0.047 µF, 2.2M/470k) appear in all three toms.
   - *Consequence (my reading):* the envelope only enlarges the negative rail. The inverter's
     two output levels become +V_tune and −(V_tune+e). That speeds up one ramp of each
     triangle, giving
     **f(t) = f0·2/(1 + V_tune/(V_tune+e(t)))**, bounded below ×2 and with a skewed triangle
     during the sweep.
   - Predicted initial factor: ×1.52 / 1.42 / 1.35 (TUNE min/centre/max). After the 22 ms part
     dies: ×1.16 / 1.11 / 1.09, gliding to ×1.0 with τ 220 ms (my calc).
3. **Three VCOs** (comparator 100k/100k, so ±6.75 V thresholds; integrator 47k; reset transistors
   through 1M bases and 10k discharge resistors).
   - VCO-1 (C32), VCO-2 (C33), VCO-3 (C34).
   - Roland/network-909: "VCO-3 runs at highest frequency, VCO-2 runs at lowest".
4. **Shapers and VCAs.**
   - VCO-1 → R164 10k → D38/D39, **with an extra transistor Q23 across the clipper**. Its base is
     fed through R162 1M from C38 6.8 nF, charged by the trigger through D31/R163 1k. Network-909:
     "a slow change from square (hard clipping) to sine (softer clipping)". τ ≈ 6.8 ms on MT,
     10 ms LT, 5.6 ms HT (my calc).
     Then R165 2.2k → R159 47 Ω → Q26 VCA. ENV1: D29 → C36 0.15 µF → R168 470k (τ≈70 ms).
   - VCO-3 → R181 10k → D46/D47 → R176 2.2k → R175 47 Ω → Q28 VCA, **with tom noise injected
     through R177 22k** at the same node. ENV3: D43 → C39 0.056 µF → R174 470k (τ≈26 ms).
   - VCO-2 → R166 10k → D40/D41 → R167 2.2k → R160 47 Ω → Q27 VCA. ENV2: D30 charges C35 0.47 µF.
     - C35 decays through **VR14 500k(B) DECAY + R171 56k**: τ 26–261 ms.
     - C35 also feeds C37 0.47 µF through R161 100k (≈47 ms rise), and C37 → R170 220k → base of Q27.
     - So **DECAY affects only the lowest oscillator (VCO-2)**, and that oscillator swells in
       slowly. The schematic draws ENV2 as a rounded hump.
   - IC23a sums the three VCA currents; VR15 50k(B) LEVEL is the feedback resistor.
5. **Tom noise (shared, IC27):**
   - Noise → R190 22k / C161 4.7 nF (LP ≈1.5 kHz) → C51/C52 4.7 nF + IC27b (R191 10k, R192 220k)
     band-pass → C53 0.047 µF → R195 1k / R199 47 Ω.
   - VCA Q33: base through R200 100k from C54, which any tom trigger (D1–D3) charges through Q34
     (R196 22k, R197 10k). Accent comes in via D26/D44/D45.
   - R194 47k is a bypass around the VCA into IC27a (R193 22k feedback), so some noise is always
     present on the tom-noise bus.
   - **Serial 426700 changes:** C54 0.0022 → 0.0047 µF and R194 47k → 100k, "to emphasize attack
     of TOM TOMs" [A1-1 p7]. That means a longer noise burst and less steady leakage. The noise
     reaches each tom's VCO-3 VCA through R114/R177/R387 22k.

#### Component table (from [A1-1] p11)

| | LT | MT | HT |
|---|---|---|---|
| TUNE pot / ladder | VR10 10k(B), R113 68k, R112 10k | VR13, R172 68k, R173 10k | VR16, R386 68k, R384 10k |
| pitch env caps / resistors | C16 0.1 µ, C17 0.047 µ / R60 2.2M, R61 470k | C30, C31 / R121, R122 (same values) | C95, C96 / R331, R332 (same values) |
| VCO-1 integrator C | C18 0.022 µF | C32 0.018 µF | C97 0.015 µF |
| VCO-2 C (lowest) | C19 0.033 µF | C33 0.027 µF | C102 0.022 µF |
| VCO-3 C (highest) | C20 0.012 µF | C34 0.01 µF | C103 0.0082 µF |
| VCO-1 variable clip C | C24 0.01 µF | C38 0.0068 µF | C101 0.0056 µF |
| ENV1 (VCO-1) | C22 0.22 µF, R107 470k | C36 0.15 µF, R168 470k | C99 0.15 µF, R354 470k |
| ENV3 (VCO-3 + noise) | C25 0.056 µF, R110 470k | C39 0.056 µF, R174 470k | C106 0.047 µF, R378 470k |
| ENV2 (VCO-2, DECAY) | C21 0.68 µF / C23 0.68 µF, R100 100k, VR11 500k(B), R111 56k, R109 220k | C35/C37 0.47 µF, R161 100k, VR14, R171 56k, R170 220k | C98/C100 0.47 µF, R341 100k, VR17, R385 56k, R352 220k |
| LEVEL | VR12 50k(B) | VR15 50k(B) | VR18 50k(B) |

#### Numbers

| quantity | LT | MT | HT | source |
|---|---|---|---|---|
| VCO-1 Hz/V; range over TUNE | 35.8; **61–122 Hz** | 43.8; **75–149 Hz** | 52.5; **90–179 Hz** | my calc (±6.75 V, 47k) |
| VCO-1 meas. TUNE 0 / 50 / 100 % (late window) | 58.8 / 87.2 / 119.0 | 74.0 / 108.8 / 143.4 | 85.3 / 128.0 / 169.4 | meas. [A1-20] |
| VCO-2 calc range | 41–81 Hz | 50–99 Hz | 61–122 Hz | my calc |
| VCO-2 meas. TUNE 0 / 50 / 100 % | 39.9 / 59.1 / 80.9 | 50.0 / 73.4 / 96.9 | 57.7 / 86.6 / 114.6 | meas. |
| VCO-3 calc range | 112–224 Hz | 134–269 Hz | 164–328 Hz | my calc |
| VCO-1 : VCO-2 : VCO-3 | 1.5 : 1 : 2.75 | 1.5 : 1 : 2.7 | 1.47 : 1 : 2.68 | C ratios; meas. VCO-1/VCO-2 = 1.475–1.48 |
| ENV1 / ENV3 τ | 103 / 26 ms | 70 / 26 ms | 70 / 22 ms | my calc |
| ENV2 (DECAY) τ min–max; rise | 38–378 ms; 68 ms | 26–261 ms; 47 ms | 26–261 ms; 47 ms | my calc |
| slow pitch glide | e.g. MT TUNE 50: VCO-1 123.8 (15–40 ms) → 118.5 (40–100) → 112.4 (100–250) → 108.8 Hz (250–600 ms) | | | meas.; consistent with the 220 ms component |
| Roland drawing (p9) | LT 0.5 V/div at 50 ms/div, with/without accent: "noise" burst at the start, then a decaying oscillation | | | [A1-1] |

The tom calc matches the meas. ranges to within 1–6 % without any fitting. This is good evidence that
the "inverter supply = ±V_cv, frequency = V/(4·V_th·R·C)" model is right, and that V_th ≈ 6.75 V.

#### Recommendation for our model

- Model the three triangle VCOs per tom with V_DD = V_tune and V_SS = −(V_tune+e). Verify the
  asymmetric-rail reading in SPICE first: it changes both the sweep law and the triangle symmetry.
- Hard-reset all three VCOs at the trigger. Add the Q23 variable clipper on VCO-1 (≈6–10 ms from
  hard to soft clip).
- Use the envelope RC values from the component table. Accent scales ENV1/2/3 but not the pitch
  envelope.
- Inject the tom-noise bus (band-passed, VCA'd, with a constant R194 leak) into VCO-3's VCA. Offer
  the pre/post-426700 values as an option.
- Meas. tom recordings show 50 Hz hum harmonics (150/250/450 Hz) in the tail. That is a property of
  the recording, not the voice: ignore those bins when calibrating.

#### Open questions

- Does the asymmetric V_SS-only envelope (my reading of R125 taking feedback after R126) hold in SPICE?
- How large is the fast initial pitch jump? Predicted ×1.35–1.52; not resolved in my FFT analysis,
  which needs a cleaner per-VCO tracking method.
- What are the exact tom-noise envelope (C54, R200) and the role of the R194 leak?

### Cross-check with fork E's independent measurements (same packs; `reference/real909/packs/_measurements_forkE/`)

Fork E analysed the same recordings with its own scripts. They agree with the numbers above, and
with the component-value model, as follows.

- **BD:**
  - f∞ is 49.2–49.7 Hz on both units at every TUNE setting. That is ≈12 % below my nominal 56 Hz,
    so calibrate V_th or R27 per unit.
  - τ is 7.5 / 9.6 / 11.7 / 14.4 / 28.9 ms at TUNE 0–100 % (my fit: 7.4 / 9.4 / 11.6 / 14.3 / 29.1;
    my calc: 6.8–29.4 ms).
  - The first measurable cycle at about 6 ms is 148 Hz at TUNE 0 and 239 Hz at TUNE 100.
    Extrapolating the exponential back to onset gives 280–300 Hz (my calc: 261–274 Hz at the end
    of the 2 ms trigger).
  - −20 dB is reached at 73 / 75 / 114 / 155 / 236 ms for DECAY 0–100 %. This is consistent with
    a hold of about 60–70 ms followed by τ 14–107 ms.
  - E also sees:
    - a slow sag to about 48 Hz;
    - a DC-like tail at about −41 dB after the oscillation stops, with a kink at 0.38–0.58 s;
    - ATTACK adding +3.9 dB of peak and +8 dB of energy above 1 kHz in the first 5 ms.
  - The slow sag and the tail are not explained by my model yet. Candidates are C9's long tail
    through R26 330k, or the IC12 offset and VCA leakage as C8 crosses the transistor threshold.
    This is an open item.
- **SD:**
  - The ratio is ≈1.5; TUNE 0 gives 116/177 Hz and TUNE 100 gives 231/346 Hz. These match my
    numbers.
  - The pitch is 12–29 % higher for the first 20 ms. That is consistent with the ENV1 bump
    (τ≈5.8 ms) averaged over the analysis window; the instantaneous peak should be larger (≈×2).
  - TONE has no effect with SNAPPY at 0. This agrees with the circuit, where TONE acts only on ENV4.
  - With SNAPPY at 100, TONE moves −20 dB from 86 to 220 ms (mine: 82–202 ms). The noise
    centroid is ≈6.7 kHz.
- **Toms:**
  - Over the TUNE range: LT 62→123 Hz, MT 79→148 Hz, HT 91→175 Hz.
    - These match my VCO-1 calc (61–122 / 75–149 / 90–179 Hz).
    - E's two tones at ratio 1.47–1.48 are VCO-1 and VCO-2.
  - The pitch starts 25–35 % above its settled value. This is consistent with my asymmetric-rail
    prediction of ×1.35–1.52 at t=0, falling with τ 22 ms to ×1.09–1.16 and then with τ 220 ms.
    That supports the "envelope only on V_SS" reading. A symmetric ±(V_tune+e) drive would give
    ×(1+e/V_tune) ≈ ×2.1–3.2.

### Revision and production changes affecting BD/SD/toms ([A1-1] p7 "Change information")

| serial from | change | effect |
|---|---|---|
| ≤371499 | R495 150k surface-mounted (BD, at Q6) | shorter BD attack envelope (my reading) |
| 381500 | BD C9 0.22 µF → 0.33 µF | TUNE (pitch-sweep) range ×1.5 longer |
| 415300 | 0.01 µF caps added across each multi-out jack (C500–C512) | HF roll-off on outputs (A2) |
| 426700 | tom noise C54 0.0022 → 0.0047 µF, R194 47k → 100k | stronger tom attack, less steady tom noise |
| — | the only trimmer in the voicing section is TM (hand clap) | **no factory tuning trims for BD/SD/toms**: pitch spread is purely component tolerance |

### Published measurements and specs: summary of what exists

- **No peer-reviewed paper models any 909 analog voice.**
  - Kurt Werner's work covers the TR-808 bass drum, cymbal and cowbell [A1-13a–d]. It is the
    methodological template (physically informed, component-level, circuit-bendable) and is relevant
    to the 909 only by analogy. His thesis [A1-13e] gives WDF tools.
  - Searches of DAFx, AES, ICMC and arXiv for "TR-909" turned up nothing circuit-level.
- **Numbers published by hobbyists:**
  - Fraser: TUNE 30–120 ms.
  - Normal Sounds: BD ≈55 Hz.
  - Gearspace: ≈52 Hz G#.
  - Whittle: snappy flat-top 24 ms, VCA threshold ≈0.4 V.
  - maffez: RD-9's R59 measured ≈52k against the original 47k.
  - The only systematic measurement data is the knob-swept multisample packs, analysed above.

### For other sections

- **Firmware (C):**
  - Whittle: v4 fixes the 909 missing the first MIDI Clock ($F8) when it arrives <1 ms after Start
    ($FA). Roland's note says "about 1 msec" after the MPU-401's Start.
  - v4 lacks the factory-pattern reload of v1 (Track 1 + Pattern 1 at power-up).
  - Version check: power on with TRACK 1 held; one of the LEDs 1–4 lights. The service notes
    instead say BASS DRUM key 2 blinks for Ver. 2.
  - Whittle hosts `TR-909-firmware-V4.zip` and `TR-909-Firmware.pdf`, saved to `web/whittle/`.
  - Service notes p7: ROM groups A (no version label, 2764) and B (Ver. 1, 2764 EPROM or 2364
    mask ROM 15179646). "Software revision 2 cures" stop/continue lag behind MIDI clock.
  - Service notes p14–16 have the MIDI implementation:
    - note map BD 35/36, RS 37, SD 38/40, HC 39, LT 41/43, CH 42/44, MT 45/47, OH 46, HT 48/50,
      CC 49, RC 51;
    - receive channel 10 and transmit channel 11 at power-up.
- **Samples / cymbals (B):**
  - Whittle and Fraser: the HH oscillator timing resistor is R492 12k with C160 470 pF. The tune mod
    replaces R492 with 6.8–6.9k + a 10–22k pot; the range is 0.43–1.7× the original.
  - Normal Sounds says crash and ride use 6.8k + 10k lin pots.
  - Whittle found a second soft OH burst at low HH tune, fixed with a diode on Q84's emitters.
  - Service notes change: HH C134 10 µF → 0.01 µF at SN 415300 "for rolling off unnecessary lower
    frequencies".
  - The 9090 page links EPROM dumps of the three cymbal ROMs (nuxx.net mirror).
- **Noise / clap / rim (A2):**
  - The noise generator is 2× HD14006BP (IC32/IC33, "32 stages") clocked by an XOR-gate RC
    oscillator: IC31, R185 33k, C47 100 pF, R184 10k. D48/C46 kick-start it at power-up.
    Hexinverter: "32-bit LFSR".
  - Noise amplitude after the BD divider is 4.22 V.
  - Whittle has clap mods (BA662A VCA, TM trimmer via R494; reverb via C61). Zifcak on e-licktronic:
    C61 0.01 µF sets the clap reverb tail; R207 is used for clap tune.
  - RS: R417 changed 12k → 3.3k at SN 415300 (p9 has drawings of both).
- **Mixer (A2):** each multi-out has 1k series + 0.01 µF (from SN 415300) and 12k / 8.2k / 15k / 10k
  mix resistors to L/R on the Multi Jack board (p12). maffez notes the RD-9 uses 10 µF main output
  coupling against Roland's 1 µF.
- **Emulations / clones (D):**
  - Tiptop BD909/SD909 manuals (saved): "exact recreation", with added tune/overload controls.
  - Behringer RD-9 per maffez: TL074/TL064 op-amps, high-hFE VCA transistors, a discrete BA662
    clone, and noise from an XOR gate and octal buffer, so the RD-9 noise source differs from the
    909's. It has an STM32F746 CPU. RD-9 R575 = 909 R59, R314 = R54, R656 = R52.
  - 9090 (Trevor Page) is a faithful voice clone with modern transistors and a CA3080 in place of the
    BA662.
  - Nava (e-licktronic) uses the original component numbers.
- **Reference audio (E/F):** the AudioRealism pack has a 50 Hz hum family at about −60 dB in tom
  tails. Its Readme says individual outputs, LEVEL at 50 %, 96 kHz/32-bit float.

### Sources

| id | source | URL | local copy (relative to docs/papers) |
|---|---|---|---|
| A1-1 | Roland TR-909 Service Notes, 1st ed., 15 Jun 1984 | https://archive.org/details/synth_Roland_TR-909_service_notes | `roland/TR-909_service_notes_A.pdf`, `roland/service_notes_pages/p01–p16.png`, OCR `.txt` (downloaded by the parent) |
| A1-2 | network-909.de "Circuit Analysis" (BD, SD, toms, RS, HC, HH/cymbal pages and annotated schematic crops) | http://www.network-909.de/circuit.htm | `web/network909/*.htm, *.txt, *.jpg` |
| A1-3 | Trevor Page, 9090 clone: user guide (16 Nov 2002), schematics (11 Sep 2006), parts list | http://nuxx.net/files/9090/ (mirror of introspectiv.org); http://www.synthdiy.com/show/?id=452 | `clones/9090_trevor_page/` |
| A1-4 | Jörg Schmitz, TR909 bass drum PCB/schematic (based on Fraser and the 9090) | http://www.analog-synth.de/synths/tr909/tr909.htm | `clones/analog-synth_de_tr9090/` |
| A1-5 | Colin Fraser, "909 Sound Mods" (2004) | http://www.colinfraser.com/tr909/909mods/909mods.htm | `web/colinfraser/909mods.{htm,txt}` |
| A1-6 | Robin Whittle, TR-909 page and "TR-909 sound mods" PDF (2018) | https://www.firstpr.com.au/rwi/tr-909/ | `web/whittle/` (index, Sound-Mods PDF+txt, Firmware PDF, voicing-board PNG) |
| A1-7 | G. Reid, Synth Secrets: "Practical Bass Drum Synthesis" (SOS Feb 2002), "Synthesizing Drums: The Bass Drum", "… The Snare Drum", "Practical Snare Drum Synthesis" | https://www.soundonsound.com/techniques/practical-bass-drum-synthesis (etc.) | `sos/*.html, *.txt` |
| A1-8 | Tiptop Audio BD909 and SD909 manuals | http://www.tiptopaudio.com/manuals/Tiptop_Audio_BD909_ns.pdf (via Wayback) | `clones/tiptop/` |
| A1-9 | Hexinverter NeinOhNein KICK: page, schematic, assembly and modification guides | http://www.hexinverter.net/neinohnein-kick | `clones/hexinverter/` |
| A1-10 | Normal Sounds, "TR-909 Modifications" | https://normalsounds.com/blog/tr-909-modifications/ | `web/voices_normalsounds_tr909_mods.txt` |
| A1-11 | maffez, "Behringer RD9 Modifications" V2 (2021) | https://maffez.com/wp-content/uploads/2021/07/RD9_MODIFICATIONS_V2.pdf | `clones/behringer_rd9/` (A2 saved the same PDF under `clones/mix_rd9_maffez/`) |
| A1-12 | Mod Wiggler t=227027 "TR-909 (rare) cv mod of bass drum & snare pitch" (2020) | https://modwiggler.com/forum/viewtopic.php?t=227027 | `web/voices_modwiggler_t227027_909_bd_sd_cv_mod.txt` (via Wayback) |
| A1-13a | K. J. Werner, J. Abel, J. O. Smith, "A Physically-Informed, Circuit-Bendable, Digital Model of the Roland TR-808 Bass Drum Circuit", DAFx-14 | https://www.dafx.de/paper-archive/2014/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf | `academic/werner_dafx14_tr808_bass_drum.{pdf,txt}` |
| A1-13b | Werner et al., "The TR-808 Cymbal: a Physically-Informed, Circuit-Bendable, Digital Model", ICMC/SMC 2014 | http://smc.afim-asso.org/smc-icmc-2014/papers/images/VOL_2/1453.pdf | `academic/werner_icmc14_tr808_cymbal.{pdf,txt}` |
| A1-13c | Werner et al., "More Cowbell: … TR-808 Cowbell", AES 137th Conv. 2014, paper 9207 | https://michaelzfreeman.org/wp-content/uploads/2018/05/More-cowbell-a-physically-informed-circuit-bendable-digital-model-of-the-TR-808-cowbell.pdf | `academic/werner_aes137_tr808_cowbell.{pdf,txt}` |
| A1-13d | Werner et al., "Three Models of Circuit-Bent TR-808 Voices" (not obtained; ResearchGate only) | https://www.researchgate.net/publication/280534006 | — |
| A1-13e | K. J. Werner, *Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters*, PhD, Stanford 2016 | https://purl.stanford.edu/jy057cz8322 | `academic/werner_2016_phd_wdf_virtual_analog.{pdf,txt}` |
| A1-14 | Jacob Korn, TR-909 tune/decay mods list | http://jacobkorn.de/news/roland-tr-909-tunedecay-mods-firmware-upgrade.html | `web/voices_jacobkorn_tr909_mods.txt` |
| A1-15 | Gearspace "Synthesizing a TR-909 kick drum" (Cloudflare-blocked; only the search snippet "about 52 Hz G#" was seen) | https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/1099922-synthesizing-tr-909-kick-drum.html | — |
| A1-16 | e-licktronic forum t=772 "collection of 909 mods" (Nava clone; Zifcak clap notes) | http://www.e-licktronic.com/forum/viewtopic.php?f=26&t=772 | `web/voices_elicktronic_t772_collection_of_909_mods.txt` |
| A1-17 | C.Y. Kong TR-909 mods page | http://www.cykong.com/Synths/Roland%20TR-909/RolandTR-909.htm | `web/voices_cykong_tr909_mods.txt` |
| A1-18 | Mod Wiggler "TipTop 909 Bassdrum disappointing" t=99432 and "NeinOhNein KICK" t=65165 (both blocked; leads only) | https://modwiggler.com/forum/viewtopic.php?t=99432 | — |
| A1-20 | AudioRealism "TR-909 Ultimate" multisample pack (96 kHz/24-bit, knob sweeps) | see `reference/real909/index.md` (fork E) | `reference/real909/packs/audiorealism_tr909_ultimate/` |
| A1-21 | Zoe Blade "RobRoy" 1995 TR-909 set | see `reference/real909/index.md` | `reference/real909/packs/zoeblade_robroy_1995/` |
| A1-22 | archive.org drum-machines collection, Roland TR-909 Set2 | see `reference/real909/index.md` | `reference/real909/packs/archiveorg_drum_machines_collection/` |
| A1-23 | Echo Sound Works "TR909 Day" (2018) | see `reference/real909/index.md` | `reference/real909/packs/echosoundworks_tr909day_2018/` |

---

## 3. Analog voices II: noise, TRIG/ACCENT, rim shot, hand clap, mixer and outputs

Scope of this part:

- the shared noise generator;
- TRIG and ACCENT distribution, including TOTAL ACCENT;
- rim shot (RS);
- hand clap (CP / HCP);
- the multi-jack mixer, master section and output stages;
- power rails;
- the service-notes adjustment procedure, change history and scope figures for all voices.

BD/SD/toms are in A1, and hats/cymbals/ROMs in B.

Designators are from the TR-909 Service Notes, 1st edition, 15 June 1984 [A2-1]. Page numbers are the PDF pages. The voicing-board schematic is p.11, the multi-jack/DIN/PSU boards p.12, the block diagram p.4, the circuit descriptions p.5–6, change information p.7 and the scope figures p.9. "My calc" marks numbers I derived from component values. "Measured" marks numbers I measured from real-909 recordings collected by forks E/F. The analysis scripts are in `reference/real909/packs/_measurements_forkA2/scripts/` (`an_*.py`, `sim_clap2.py`).

---

### A2.1 Noise generator (shared by BD attack, SD snappy, toms, clap)

#### Circuit

- **Clock.** IC31a and IC31b (4070 XOR) form a two-gate CMOS astable. Gate a has pin 1 tied to +15 V, so it acts as an inverter; gate b has pin 5 tied to GND, so it acts as a buffer. The timing parts are R184 10 k and C47 100 pF; R185 33 k is the input-protection resistor. Both 4006 clock pins (pin 3) share this clock [A2-1 p.11].
- **Shift register.** IC32 and IC33 are HD14006BP 18-bit static shift registers. Roland's IC data page (p.3) gives the pinout:
  - D1 (1) → 4 stages → 13
  - D2 (4) → 4 stages → **11 (D2+4)** → 1 stage → **12 (D2+5)**
  - D3 (5) → 4 stages → 10
  - D4 (6) → 4 stages → **8 (D4+4)** → 1 stage → **9 (D4+5)**
- **Chain.** Each chip is wired 1 → 13 → 4 → 12 → 5 → 10 → 6 → 9, which is 4+5+4+5 = 18 stages. IC32 pin 9 drives IC33 pin 1, so there are 36 stages in total.
- **Feedback.** XOR IC31c takes IC32 pin 10 (stage 13) and IC33 pin 10 (stage 31). Its output goes through R187 47 k into IC32 D1. The noise output is IC33 pin 9 (stage 36). The output is a 0/+15 V CMOS bit stream.
- **Start-up.** At power-on, IC31d plus C46 4.7 µ, R182/R183 10 k, R186 100 k and D49 force "1"s into D1 through D48 for roughly 50 ms (my estimate, from R182 · C46 = 47 ms). This prevents the all-zero lock-up state.
- **Description.** The service notes call it "a quasi-random noise generator having two shift registers … making up 32 stages … Two Ex-OR gates of IC31 clock the shift registers at a higher frequency" [A2-1 p.6].

#### Numbers

- **Recurrence.** s[n] = s[n−13] ⊕ s[n−31]. The characteristic polynomial is x³¹ + x¹³ + 1. It is primitive: I checked that x^(2³¹) ≡ x mod p, and 2³¹−1 is prime. So the period is **2³¹ − 1 clocks**. The extra 5 stages after the feedback tap are only a delay.
- **Clock rate (my calc).** f_clk ≈ 1/(2.2 · R184 · C47) = **≈ 455 kHz**, with roughly ±30 % uncertainty from CMOS thresholds at 15 V and gate delays. At that rate the sequence period is about 79 minutes, and the spectrum is white up to about f_clk/2 (sinc² envelope, first null at f_clk).
- **Level.** ±7.5 V around a +7.5 V DC level (4006/4070 at +15 V). Every consumer AC-couples or band-limits it. The clap, for example, takes it through R208 47 k into its band-pass.
- **Measured.** On the AudioRealism TR-909 [A2-19], the normalised autocorrelation of the snare noise (high-passed at 3 kHz, 20–200 ms) is ≤ 0.07 for all lags from 0.05 to 20 ms. The clap tail shows only the 1.04 ms ringing of its own band-pass. **No short period is audible.** This matters because misreading the 4006 as four 4-stage sections gives taps at 12/28 and x²⁸ + x¹² + 1 = (x⁷ + x³ + 1)⁴. That would give a period ≤ 508 clocks (≈ 1.1 ms), a strong ≈ 900 Hz buzz, which the recordings rule out.
- **Correlation between voices.** The SD and the clap read the same source, so hitting them together produces audible phasing/comb filtering:
  - AudioRealism sampled "Snare + Hand Clap … from the master output to capture the phasing interference that occurs when both use the same noise generator" [A2-19].
  - The e-licktronic thread mentions a 9090 "secondary noise source board" that removes it [A2-17].
  - Regebro says SD and CP take the noise out of phase (fork F, video `regebro_rd9_vs_tr909_deeper`). This is consistent with the clap's band-pass being an inverting MFB stage (see A2.4).

**Cross-checks.**

- The e-licktronic Nava v1.02 netlist [A2-24] has exactly this wiring: IC32 D1+4→D2, D2+5→D3, D3+4→D4 (and XOR input), D4+5→IC33 D1; the same inside IC33; XOR inputs on IC32.D3+4 and IC33.D3+4; NOISE taken from IC33.D4+5; clock IC31 output through R184 10 k to C47/R185.
- Fork D reports that Electric Druid and ryandorward model the 909 noise as the same 31-bit LFSR, x³¹ + x¹³ + 1, clocked at about **300 kHz** with a start-up fill. That is about 2/3 of my 455 kHz component estimate. The real rate is set by the 4070's threshold and propagation delay at 15 V, so 300–450 kHz is the plausible range.

#### Disagreements

- **Stage count.** The text says 32 stages; the schematic plus IC data give 36 stages with feedback taps at 13 and 31. The real LFSR length is 31.
- **Clock rate.** ≈ 300 kHz (Electric Druid/ryandorward, via fork D) vs ≈ 455 kHz (my 1/(2.2RC)). Either is inaudible as a pitch.
- **RD-9.** The Behringer RD-9 does not copy this circuit: "System noise is done with an XOR gate, an octal buffer and a transistor … shared by snare and clap (phasing effect)" [A2-14].

#### Recommendation for our model

- Use one shared instance of the exact 31-bit LFSR (taps 13 and 31, XOR, all-ones start state) stepped at f_clk.
- Make f_clk a per-unit parameter, default 455 kHz.
- To get it to the model rate (e.g. 4× host), integrate the ±7.5 V bit stream over each output sample (box filter). That is exact for a zero-order-hold stream and removes the aliasing of the 455 kHz chip rate.
- Keep one instance so that BD/SD/tom/clap noise stays correlated as on the real machine.

#### Open questions

- The true clock rate is unknown; measure it on a real unit, or estimate it from the HF spectrum of the raw noise if anyone has recorded it.
- I have not checked whether R187 + D48 cause any asymmetry.

---

### A2.2 TRIG and ACCENT distribution, TOTAL ACCENT

#### Circuit

- **Overview (p.5).** "Accent data on the CPU bus is latched into one of ACCENT latches (IC2–IC9) selected by Address Decoder (IC612, 613). Latched ACCENT code is converted to analog equivalent at the output of associated resistor array RM0621. The voltage is clamped to the level until it is replaced by the next incoming data. Almost concurrent with ACCENT, TRIG is latched into IC1 or IC10, and appears as 5V positive going pulse on the correct output pin for 2ms" [A2-1 p.5].
- **DACs.** IC1–IC10 are 4174 hex D flip-flops on the +5 V supply.
  - Each accent latch drives an **RM0621**, a binary-weighted array with "R = 5 K": 5 k, 10 k, 20 k, 40 k, 80 k and 160 k, all tied to one output node (p.11 legend).
  - That makes a **6-bit voltage DAC**: V = 5 V · code/63 unloaded, LSB 79.4 mV, source impedance 5 k ∥ … ∥ 160 k = **2.54 kΩ** (my calc).
- **Bit order.** The CN1 labels give D6 → 5 k (MSB), D5 → 10 k, D4 → 20 k, D3 → 40 k, D2 → 80 k, D1 → 160 k (LSB). This matches MAME's `dacN_w(data & 0x7e)` at 0x4001–0x4008 [A2-21].
- **Assignment (block diagram p.4).** The rim shot and clap share one accent DAC, and so do crash and ride:

  | Latch | Voice |
  |---|---|
  | IC2 | BD |
  | IC3 | SD |
  | IC4 | LT |
  | IC5 | MT |
  | IC6 | HT |
  | IC7 | HH (CH and OH) |
  | IC8 | **RS and CP** |
  | IC9 | **crash and ride** ("CYMBAL accent level") |

- **TRIG latches.**
  - IC1 carries BD, SD, LT, MT and HT triggers. It uses D1–D5 (Q outputs on pins 5, 12, 2, 7, 10); D6 is unused, which matches MAME `trig1_w(data & 0x3e)`.
  - IC10 carries RS, HC, ride, crash, HH and CLOSED/OPEN. It uses D1–D6, matching `trig2_w(data & 0x7e)`.
  - The block diagram lists BD, SD, LT, MT, HT top to bottom. A guess was BD = D5 … HT = D1. **The firmware says otherwise** (section 5, C.4): TRIG1 b1 BD, b2 SD, b3 LT, b4 MT, b5 HT; TRIG2 b1 RS, b2 HH, b3 CLOSED/OPEN, b4 crash, b5 ride, b6 HC. The DACs at 4001–4008 are BD, SD, LT, MT, HT, RS+HC, HH, crash+ride.
- **TOTAL ACCENT.**
  - VR1 is a 100 kΩ (B) pot on the voicing board. Its ends go to CN3 "ACC +5V" and GND; its wiper "ACC LEVEL" goes to switch board F1-2 and then to the µPD7811 analog input **AN3** ("Analog Voltage Input from TOTAL Control", p.5). In MAME this is `an3_func → "TOTAL"`.
  - The CPU therefore reads the knob with its 8-bit ADC and computes the DAC codes. There are no per-voice accent knobs in hardware.
- **Owner's manual [A2-2 p.25–26].**
  - "There are 2 types of Accents": "Accent A … set by each step. All the voices in that step will take on accents" (TOTAL ACCENT, intensity set by the Total Accent knob), and "Accent B … any voice in any step … available only for Bass Drum, Snare Drum, Low Tom, Mid Tom, Hi Tom, and Closed Hi-hat".
  - "The intensity of this Accent cannot be changed."
  - "It is possible to set both Total Accent and Accent at the same time, but if the Total Accent Knob is set high, Accent may not be noticed."
  - The spec page marks BD, SD, LT, MT, HT and CH with "*" for "the sound with and without accent".
- **How the analog side uses the DAC voltage (RS and CP).**
  - Each has a ×(1 + 10 k/4.7 k) = **×3.13** non-inverting buffer: IC48b for RS, IC26a for CP. The CP buffer input has R496 100 k to GND, which loads the 2.54 kΩ DAC by 2.5 %.
  - The buffer output sets the envelope peak voltage. VCA gain is roughly linear in that voltage above a V_BE offset (A2.3, A2.4). The M5218 swing limit of about ±13.5 V caps the ×3.13 output when the DAC is above about 4.3 V.
  - BD/SD/tom/hat use of the accent voltage is covered in A1/B.

#### Numbers

**AudioRealism TR-909 pack [A2-19] (measured).** 96 kHz, individual outs, part level 50 %. The "accentNNN" files are 11 settings, consistent with a TOTAL ACCENT knob sweep on an accented step.

- The 0 % file has the same level as the unaccented "shot" files: RS −28.3 vs −27.6 dB RMS, CP −28.0…−28.9 vs −28.0.
- Gain at 100 % relative to 0 % (RMS over the first 200 ms):

  | Voice | Gain |
  |---|---|
  | RS | +4.3 dB |
  | CP | +5.1 dB |
  | SD | +5.9 dB |
  | BD | +6.7 dB |
  | LT | +7.1 dB |
  | MT | +7.0 dB |
  | HT | +6.8 dB |
  | CH | +7.1 dB |
  | OH | +7.3 dB |
  | crash | +7.8 dB |
  | ride | +7.7 dB |

- The response is flat between 0 and 10 % and between 90 and 100 %, so the knob has dead zones at both ends. Between 10 and 90 % the linear amplitude rises roughly linearly with knob position. For RS it goes 0.61 → 1.0, about +0.05 per 10 %.

**Regebro's rev-4 TR-909 (fork F, `regebro_soundcloud_*`).** Individual outs, RMS over 300 ms, relative to no accent. The mapping of the four takes to these columns is inferred by fork F.

| Voice | TOTAL ACCENT only | Step (instrument) accent only | Both |
|---|---|---|---|
| BD | +3.1 dB | +7.7 dB | +10.4 dB |
| SD | +3.1 dB | +7.4 dB | +9.8 dB |
| HT | +3.7 dB | +7.7 dB | +11.5 dB |
| CH | +5.7 dB | +9.2 dB | +13.4 dB |

- RS, CP, OH, CR and RD respond only to the global accent, by +2.4 to +4.3 dB.
- "Both" is louder than either alone. So in this unit the two accents add, at least at the knob setting used, rather than taking the larger of the two.

**Service-notes scope figures (p.9, "all knobs set to center").** These are hand-traced drawings without a graticule, so the numbers are rough. Accented/unaccented peak-to-peak ratios: BD ≈ 2.7×, SD ≈ 2.1×, LT ≈ 1.65×.

#### Disagreements

- The owner's manual implies TOTAL ACCENT can mask the instrument accent ("may not be noticed"). Regebro's data show the two adding. Both could be true if the firmware combines them additively and the sum saturates at code 63 for high knob settings.
- The firmware rule (base code, accent code, how the knob maps to codes) is the authority (fork C).

#### Recommendation for our model

- Emulate the 8 latches and DACs exactly: 6 bits, 0–5 V, 2.54 kΩ source, the load each voice presents. Let the emulated µPD7811 firmware write them. No accent logic outside the firmware.
- Feed TOTAL ACCENT to AN3 as a linear 0–5 V wiper. The dead zones and curve should then come out of the firmware.
- Validate against the AudioRealism sweep and the Regebro four-level data.

#### Open questions

- Which LS138 outputs (IC612/613) select which latch, and which TRIG bit drives which voice. **Answered by the firmware** (section 5, C.4) as far as the CPU side goes. The decoder wiring itself is untraced.
- Whether "unaccented" equals a non-zero DAC code (it must, since the voices sound), and which code. **Answered in C.4:** the analog voices use code 10 normal and 21 accented at TOTAL ACCENT 0.
- The accent codes per instrument: tabulated in C.4.

---

### A2.3 Rim shot

#### Circuit (p.11 grid J-14 to G-11; Network 909 description [A2-4])

**Trigger and excitation.**

- TRIG from IC10 goes through C111 0.018 µ (a differentiator) to node A.
- Node A drives two paths:
  - R399 22 k into the base of Q63, with R398 10 k from base to GND.
  - D89 → R396 22 k → node B → R395 22 k → GND.
- Node B feeds the **non-inverting inputs of all three resonators**: IC48a, IC49a and IC49b.
- D89 passes only the rising edge. So the resonators get a positive exponential pulse of about **2.2 V peak with τ ≈ 0.26 ms** (my calc: (5 − 0.6)/2 V; τ = 0.018 µ · (22 k ∥ 44 k)).

**Resonators.** Each is a bridged-T in the feedback of a non-inverting op-amp. A bridge resistor R1 runs from output to the − input; two equal capacitors C run from the − input and the output to a mid node; a resistor R2 runs from the mid node to GND.

- My calc gives H(s) = 1 + s·R1·C/(s²·R1·R2·C² + 2·s·R2·C + 1). That is a unity path plus a band-pass with:
  - ω₀ = 1/(C·√(R1·R2));
  - Q = ½√(R1/R2);
  - peak band-pass gain R1/(2·R2);
  - decay τ = 2Q/ω₀ = R1·C.

| | Op-amp | R1 (bridge) | C | R2 | f₀ (my calc) | Q | peak gain | τ_decay | service-notes annotation |
|---|---|---|---|---|---|---|---|---|---|
| F1 | IC48a | R407 470 k | C113 = C112 = 0.01 µ | R394 2.2 k | **495 Hz** | 7.3 | 107 | 4.7 ms | "T1 ≈ 2 ms, 30 Vp-p, 20 ms" |
| F2 | IC49a | R414 330 k | C116 = C115 = 0.027 µ | R411 2.2 k | **219 Hz** | 6.1 | 75 | 8.9 ms | "T2 ≈ 4.5 ms, 15 Vp-p, 25 ms" |
| F3 | IC49b | R416 470 k | C118 = C117 = 0.0047 µ | R404 2.2 k | **1053 Hz** | 7.3 | 107 | 2.2 ms | "T3 ≈ 1 ms, 30 Vp-p, 5 ms" |

- Network 909 gives f1 = 500 Hz, f2 = 220 Hz, f3 = 1000 Hz [A2-4].
- With the 2.2 V/0.26 ms excitation, F1 and F3 would reach amplitudes of about 25–30 V (my calc). **They rail-clip at about ±13.5 V for the first cycles.** This matches the "30 Vp-p" annotations. F2 reaches about 19 Vp-p by my calc; the annotation says 15 Vp-p.

**Sum and clipper.**

- F1 → R408 12 k, F2 → R415 12 k and F3 → **R417** feed one node. R417 is 12 k on early units and **3.3 k from SN 415300**, "for giving the voice more realistic sound" (p.7).
- The node has **D91/D92** (1SS133) to GND, an anti-parallel diode clipper at ±0.6 V. It is loaded by R423 12 k and R418 680 Ω to GND.
- For the first several milliseconds the sum is therefore close to a square wave.

**VCA.**

- Q65 is a single-transistor "S" VCA, the Roland 808-type transistor attenuator.
- The signal enters through the R423/R418 divider (×0.054, so ≤ ~35 mV).
- The output current goes into the virtual ground of IC50a.
- The control is base current through **R403 1 M** from the ENV node.

**Envelope.**

- IC48b is the ×3.13 accent buffer (R397 4.7 k, R406 10 k).
- While Q63 conducts (the 0.2–0.3 ms excitation spike), Q63 turns on PNP Q64 (R400 47 k, R402 1 M, D90). Q64 then charges **C119 0.047 µ** through **R401 2.7 k** (τ = 0.13 ms) to about 0.86 × 3.13 × V_acc (my calc).
- C119 then discharges into Q65's base through R403 1 M (τ ≈ 47 ms, my calc, ignoring the nonlinear base load).

**Output.**

- IC50a: VR19 100 kΩ (B) LEVEL in parallel with C120 220 p, as a transimpedance stage. Its low-pass is 7.2 kHz at full level and 14.5 kHz at centre (my calc).
- Then C121/C122 0.01 µ, R422 22 k, R420 220 k and IC50b with R421 = R419 = 4.7 k.
- R422 returns to the − input, which sits at V_out/2 = V+. So this is a **unity-feedback Sallen-Key high-pass followed by ×2**, with **f₀ = 229 Hz, Q = ½√(220 k/22 k) = 1.58**. It peaks +4.4 dB at about 256 Hz, then gives +6 dB of overall gain (my calc). This peak boosts F2.

**TRIG OUT.**

- Q66/Q67, R409/R410 47 k, C114 0.1 µ, R412 22 k and R413 1 k drive the DIN-board TRIG OUT. The spec is "Rim Shot: +14 V, 20 ms pulse" [A2-1 p.1; A2-2 p.39].
- From SN 415300: R495 1 k replaces a jumper, R393 and R392 become 1 M, and C600 0.1 µ is added at the jack. This keeps the RS trigger off the tape-sync line.

#### Numbers (measured)

**AudioRealism unit, 96 kHz [A2-19].**

- FFT of the first 50 ms: peaks at **223 Hz (0 dB), 478 Hz (−4 dB), 1014 Hz (−3 dB)**, plus intermodulation products at 1382 and 2027 Hz (−18 dB).
- Per-band decay after clipping ends (12–35 ms): 223 Hz τ ≈ 7.7–8.1 ms (calc 8.9); 478 Hz τ ≈ 4.1–4.2 ms (calc 4.7). The 1 kHz band is contaminated by clipper harmonics.
- Overall envelope: −6 dB at 5.8 ms, −20 dB at 17–21 ms, −40 dB at 86–133 ms (shot vs accent).
- The waveform shows square-topped clipping for about 0–9 ms (dense 1 kHz cycles), then 500/220 Hz ringing. This matches the service-notes "R417 = 3.3 K" trace.

**Older recordings (fork E packs).**

- archive.org Set2 `RIMSHOT.WAV`: 225/472/**1124** Hz with the 1 kHz component at −13 dB.
- Zoe Blade `RIM127/RIM63`: 223/453/**1119** Hz at −14 dB.
- These look like pre-415300 units (R417 = 12 k), because the 1 kHz partial is about 10 dB weaker. Regebro also notes "rev 3 raised the RS pitch".

#### Disagreements

- **Output HPF.** A Gearspace snippet says "no highpass" [A2-15]; maffez only says "Rim Shot output follows the actual Roland boards" [A2-14]. Against that, both the service notes and the late-revision Nava copy [A2-24] have the IC50b Sallen-Key with R422 returned to IC50b's − input. Keep the HPF.
- **Other descriptions.** Fork D reports that Roland's Peter Brown calls the 909 rim shot "three layers", and the ER-99 emulation uses band-passes at 220, 500 and 950 Hz. Both are consistent with F2/F1/F3 here.
- **Mixer resistors.** Network 909 says the frequencies depend on 5 % parts and vary between machines. The measured 1014–1124 Hz spread for F3 confirms this.

#### Recommendation for our model

- Three linear 2nd-order resonators in state-space form, implicit/trapezoidal. **Include op-amp output saturation (±13.5 V, soft) inside each resonator loop**, because the clipping alters the resonator state.
- Drive them with the 2.2 V/0.26 ms exponential pulse.
- Sum the three through the resistors into a memoryless anti-parallel-diode node. Solve V from Σ(V_i − V)/R_i = V/R_load + 2·I_s·sinh(V/(n·V_T)) with Newton or a table.
- Q65: Ebers–Moll "transistor attenuator", or a fitted gain ∝ (V_env − V_BE)₊.
- IC50a level stage including the 220 p, then the SK HPF (229 Hz, Q 1.58, ×2).
- Provide a "pre/post SN 415300" switch (R417 12 k / 3.3 k).
- Expose component-tolerance detune so that F3 can sit anywhere from 1.0 to 1.12 kHz.

#### Open questions

- The Q65 VCA transfer (base current to gain) needs a SPICE or Ebers–Moll fit.
- Whether the real boards have IC50b (see Disagreements).

---

### A2.4 Hand clap

#### Circuit (p.11 grid J-13 to G-17; 808 equivalent: TR-808 SN p.6 and p.9 [A2-3]; Network 909 [A2-4])

Roland reused the TR-808 CP envelope generator almost part for part (808 → 909): IC23 → IC29 (AN6912), C144 → C65 0.47 µ, R365 → R220 82 k, D71 → D54, R367 → R223 330, Q73 → Q40, R368/R371 → R224/R238 470 k/15 k, Q71/Q72 → Q38, TM3 → TM, BA662 IC22 → IC30. The 808 notes describe the operation in detail (quoted in A2-3).

**Noise band-pass (IC26b).**

- Noise → R208 47 k → node with R209 10 k to GND, C43 4.7 n to the output and C42 4.7 n to the − input. R207 150 k runs from the − input to the output. This is an inverting MFB band-pass.
- My calc: **f₀ = 963 Hz, Q = 2.13, centre gain −1.6**, and the node impedance is 8.2 kΩ, as Whittle notes [A2-5].
- Oden on KVR got 1140 Hz and Q 1.95 [A2-9]; Baratatronix quotes about 1 kHz for the 808 [A2-10].

**Path 1, "clap" pulses (OTA).**

- C56 0.022 µ + R202 39 k / R201 1 k: ×0.025 attenuation and a 181 Hz high-pass (my calc) into the **BA662A** OTA (IC30), input − at pin 2. Whittle and the electro-music repair thread mention the BA6110 as a substitute [A2-5, A2-16].
- OTA output → R237 82 k to GND and **C58 1 n** into the IC28a summing node. The current entering IC28a is high-passed at 1/(2π · 82 k · 1 n) = **1.94 kHz** (my calc).
- IC28a feedback is VR20 50 kΩ (B), the LEVEL control.
- I_abc comes from **Q38**, a dual PNP pair used as a "log to linear converter" [A2-5]:
  - Tail: R235 15 k. By my reading of the drawing it is fed from the accent buffer IC26a output, so accent scales the clap pulses linearly.
  - Base L: ENV1 · 15 k/485 k (R224 470 k / R238 15 k).
  - Base R: the **TM** trimmer (10 kΩ B between GND and −15 V) via R494 47 k / R230 2.2 k, which gives 0 to −0.67 V.
  - I_OTA ≈ I_tail / (1 + exp((V_bL − V_bR)/V_T)).

**Sawtooth envelope (IC29 AN6912 quad comparator).** The comparator has open-collector NPN outputs on a single −15 V supply: pin 3 is GND, pin 12 is −15 V, and the outputs pull down to −15 V. The pinout is the same as the LM339 [A2-3, Mod Wiggler].

- **Reference ladder.** R219, R217 and R214 (all 4.7 k) from GND to −15 V give −5 V and −10 V.
- **Comparator a (window start).**
  - − input: the trigger node R213/R218 (22 k/22 k from the Q36 collector to −15 V). This sits at −15 V idle and rises during the 2 ms TRIG.
  - + input: −10 V.
  - While triggered, its output discharges **C62 0.027 µ** (with R229 1 M pull-up to GND) to −15 V.
- **Comparator b (window).**
  - + input: C62; − input: −5 V.
  - Its output is low while C62 < −5 V, i.e. for 2 ms + R229 · C62 · ln 3 = 2 + 29.7 ≈ **31.7 ms** (my calc). The 808 notes say "pulses of 30 ms wide".
- **Comparator b's output also:**
  - sets the sawtooth threshold through R227 2.7 k / R228 5.6 k: **P = −10.1 V** during the window, 0 V after;
  - drives comparator c (− input; + input = −5 V);
  - drives the C64 1 n / D55 / R226 68 k differentiator, the end-of-window kick.
- **Comparator c (discharge-path enable).** During the window c's output is released, so R220 82 k + D54 pull ENV1 (C65) up toward 0 V. After the window c holds D54 off.
- **Comparator d (sawtooth).**
  - + input: ENV1 via R225 1 M, with **C63 220 p** positive feedback from the output. − input: P.
  - When ENV1 rises above P, d releases its output. R222 10 k then pulls the node to GND, and through R221 18 k this turns on **Q40** (emitter at −15 V).
  - Q40 slams C65 back toward −15 V through R223 330 Ω (τ = 0.16 ms). Its on-time is set by the R225·C63 lag (about 0.2 ms).
- **End of window.** b releases, P → 0 V and D54 is disabled. The C64 edge pulses Q40 on once more through R226 (τ = 68 µs). This is the 4th sawtooth, and it then decays only through R224 + R238 = 485 k (τ ≈ 228 ms in volts).

**Drawing error, now confirmed.** On p.11 the d-output/C63/R222 node is drawn joined to the Q40 base node, which shorts R221. The 808 drawing (R359 10 k pull-up, R358 68 k series base resistor, R357 68 k kick resistor) shows the intended topology.

The e-licktronic Nava v1.02 netlist settles it [A2-24]. Nava is a one-to-one copy of the late-revision voicing board that keeps Roland's designators:

- {IC29.OUT, C63, R221.1, R222} form one net;
- {Q86.B, R221.2, R226.2} form another;
- Q86.E goes to −15 V.

So R221 18 k is in series with the base, and the C64/R226 kick goes straight to the base. The service notes' address-map note (p.10) says "On early PCBs two Q-40's exist one of which is denoted as Q-86 on later PCBs"; the clap's Q40 is that Q86.

The same netlist confirms:

- R235 is fed from the IC26a output (net with Q36.E, R211, R212);
- C65 has + to GND, so ENV1 swings negative;
- D54's anode is on the comparator-c output with R220 82 k to GND;
- all four comparator connections are as described above.

**Path 2, "reverb".**

- IC26b out → C55 0.47 µ → R204 1 k → C57 0.47 µ to GND → R205 3.3 k / R203 47 Ω (×0.014) → the emitter/collector of **Q37**, another "S" transistor attenuator, whose output goes into IC28a.
- Filtering (my calc): high-pass about 78 Hz, low-pass about 440 Hz.
- Envelope: during TRIG, Q36 (gated by Q35 via R215/R216) passes the accent-buffer voltage through D52 into **C59 0.1 µ** → R233 470 k → **C61 0.1 µ** (D53 clamp) → R234 470 k → Q37 base.
- This two-node RC has modes at τ ≈ 18 ms (rise) and **≈ 123 ms (decay)** (my calc, ignoring V_BE).

**Adjustment.** TM is the only trimmer in the TR-909. The parts list has one trimmer: 13299114 H1051A013 10 kΩ (B). The schematic says "ADJUSTMENT LEVEL: Center, MULTI OUT 2 Vp-p".

#### Numbers

**Measured, AudioRealism unit, 10 takes (8 shots and accents 0/100).**

- Pulse onsets at **0, 10.5 ± 0.6, 22.3 ± 0.5, 32.5 ± 0.8 ms**, so the spacings are 10.5, 11.8 and 10.2 ms.
- The last interval is shorter, consistent with the 4th pulse being the forced end-of-window kick at about 31.7 ms. The 808 notes say the window ends "in the middle of the third time".
- Pulses 1–3 decay fast (spiky); pulse 4 merges into the tail.
- Tail amplitude τ ≈ 82–99 ms (fit from 60 to 250 ms).
- Spectrum:

  | Segment | Peak | −3 dB band | Level at 2 kHz | Level at 5 kHz | Level at 8 kHz |
  |---|---|---|---|---|---|
  | 0–32 ms (pulses) | 1031 Hz | 750–1406 Hz | −6 dB | −13 dB | — |
  | 120–300 ms (late tail) | 844 Hz | 750–1031 Hz | −16 dB | −32 dB | −42 dB |

  The pulses are brighter, from the 1.94 kHz C58 high-pass and OTA drive. The late tail is darker, confirming the ~440 Hz low-pass in the reverb path.
- Service-notes figure: 4 spikes and a tail, 500 mV/div, 10 ms/div; peak about 2 Vp-p at level centre.

**My behavioural simulation (`sim_clap2.py`).**

- Nominal C65 0.47 µF: teeth at 0, 12.6, 25.1 and 31.7 ms (kick).
- C65 0.40 µF, within electrolytic tolerance: 0, 11.1, 22.1, 31.7 ms, matching the measurement.
- Tooth period = τ · ln((V₀ − V_f)/(P − V_f)), with τ = (82 k ∥ 485 k) · C65 = 32.9 ms, V₀ ≈ −14.3 V, P = −10.1 V, V_f ≈ −0.5 V.

**Published claims.**

- "four times … roughly 11 ms apart" [A2-9].
- 808: three 10 ms cycles in a 30 ms window, then a final 20 ms discharge; "sawtooth" about 50 ms, "reverb" about 100 ms [A2-10].

#### Disagreements

- **BPF centre.** 963 Hz (my calc, matches measurement) vs 1140 Hz (KVR).
- **The drafting error around R221/Q40.** Resolved by the Nava netlist (see Circuit).
- **R235 supply.** The IC26a output, so accent scales the pulses. Confirmed by the Nava netlist.
- **RD-9 clap.** It uses a discrete BA662 clone; the maffez mods only touch C233 / R503 / C40 [A2-14].
- **Clone controls.** Tiptop CP909 describes a "triple-saw envelope generator" plus a "trashing" control on the noise clock [A2-13]. Hexinverter NeinOhNein exposes TM as "INTENSITY" and adds noise-level pots [A2-11].

#### Recommendation for our model

- Shared LFSR noise → IC26b MFB band-pass (exact component values).
- Path 1: C56/R202/R201 → OTA as tanh(V_in/2V_T) · I_abc, with I_abc from the Q38 differential pair (logistic) → R237 ∥ C58 → IC28a. LEVEL = VR20.
- Sawtooth generator as an event-driven piecewise-exponential state machine. Use exact closed-form RC segments plus the comparator events, including the C63 hold time and the C64 kick. Alternatively do a small nodal simulation at 1–4× rate; it is cheap.
- Expose TM as a hidden calibration parameter. Default: 2 Vp-p at MULTI OUT with LEVEL at centre. TM sets how spiky the pulses are.
- Path 2: two-node RC envelope → Q37 transistor-attenuator model (base current).
- Apply the per-unit tolerances: C65 −15 % reproduces the measured timing.
- Validate against the AudioRealism shots (onsets, per-pulse decay, tail τ, spectrum per segment).

#### Open questions

- R221 and R235 are settled by the Nava netlist; ideally confirm on a real Roland PCB as well.
- TM setting on real units.
- Q37/Q65 VCA characteristics.

---

### A2.5 Mixer, multi outs, master outs

#### Circuit (multi-jack board JK-1 p.12; master section p.11 grid B-24 to A-26)

**Per voice.**

- Final op-amp → **1 kΩ series** (R500, R503, R506, R509, R512, R515, R518, R521, R524, R527) → **0.01 µF to GND** (C500 and C502–C510, added from SN 415300; "Noise is high in OUTPUT … add capacitor 0.01 µF across jack circuits", p.7) → MULTI OUT tip.
- The jack's normally-closed contact feeds the voice's two summing resistors. **Plugging a cable into a multi out removes that voice from the stereo mix.**

**Pan resistors** (left bus / right bus):

| Voice | to Left | to Right | Balance (my calc) |
|---|---|---|---|
| BD | R501 12 k | R502 12 k | centre |
| SD | R504 12 k | R505 12 k | centre |
| LT | R507 8.2 k | R508 15 k | **left, 5.2 dB** |
| MT | R510 12 k | R511 12 k | centre |
| HT | R513 15 k | R514 8.2 k | **right, 5.2 dB** |
| RS | R516 12 k | R517 12 k | centre |
| CP | R519 12 k | R520 12 k | centre |
| HH | R522 15 k | R523 8.2 k | **right, 5.2 dB** |
| Crash | R525 12 k | R526 10 k | right, 1.6 dB |
| Ride | R528 8.2 k | R529 15 k | **left, 5.2 dB** |

- The left/right assignment comes from tracing: the upper resistors go to the pin-45 bus, which reaches IC66b → pin 47 → R530 → the "Left" jack.
- The multi-jack board labels pin 43 "LMIX" and pin 45 "RMIX", but the voicing board labels pin 43 "MIX IN (R)" and pin 45 "MIX IN (L)". I followed the actual wiring to the jacks. **The sides need confirming with a stereo recording.** The Regebro main-out recordings are dual-mono, so they cannot decide it.
- Gearspace posters recall "hi-hat right, toms going left to right" [A2-15]. My table has the HH and HT on the right and the LT on the left, which is consistent.
- AudioRealism's ADM claims to "replicate the real TR-909" panning (fork D).
- The Nava netlist [A2-24] reproduces the same two pan-resistor groups (upper set R501/R507 8.2 k/R513 15 k/R522 15 k/R528 8.2 k… on one bus; lower set on the other). Its connector pins are not named in the netlist, so it does not settle which side is which.

**Master section.**

- Buses → **C154/C153 1 µF** into IC66a/b (M5218), inverting summers with feedback **VR28 50 kΩ (B) dual MASTER VOLUME** wired as a rheostat.
- → R473/R474 1 k → C152/C155 10 µ/16 → (multi-jack board) **R530/R532 2.2 k** → **C511/C512 0.01 µF** with R531/R533 10 k to GND → L and R/MONO jacks.
- **Muting.** Q82/Q83 2SC2878 shunt the IC66 outputs at power on/off. They are driven from Q79, D193–D195, C142 10 µ and C143 47 µ.

#### Numbers (my calc unless marked)

- Multi-out low-pass: 1/(2π · 1 k · 10 nF) = **15.9 kHz**, first order, into a high-impedance load. Output impedance is 1 kΩ.
- The voice-to-bus tap sees the 1 k/10 n loaded by the pan resistors: ≈ 18.6 kHz and −1.3 dB.
- Bus impedance is about 1.21–1.23 kΩ (all ten branches in parallel, each R_pan + ~0.9 k). With C153/C154 1 µF this gives a **first-order high-pass at ≈ 130 Hz** on the stereo mix. A Gearspace snippet says "~1k1 … ~140 Hz" [A2-15].
- Main-out low-pass (SN ≥ 415300): Thevenin (1 k + 2.2 k) ∥ 10 k = 2.42 kΩ with 10 nF gives **≈ 6.6 kHz**. A 10 kΩ load would move it to about 8.2 kHz and drop the level by 4.3 dB.
- Mix gain per voice: −VR28/(R_pan + 0.9 k). BD at full master: 50 k/12.9 k = 3.9 (+11.8 dB); at centre: 1.94.
- **The main outs are inverted relative to the multi outs** (IC66 is inverting).
- Specs:
  - Master Out: "6 Vp-p, 1 kΩ" [A2-1 p.1, A2-2 p.39].
  - Multi Out: printed blank ("[ dB, Ω]") in the owner's manual; the service notes say "See P.9".
  - Clap Multi Out: 2 Vp-p at level centre (TM adjustment).

**Measured.**

- **AudioRealism open hat, master out vs individual out** (the hat is ROM playback, so the comparison is deterministic), relative to 1 kHz:

  | Frequency | 63 Hz | 100 Hz | 160 Hz | 4 kHz | 6.3 kHz | 10 kHz | 16 kHz | 20 kHz |
  |---|---|---|---|---|---|---|---|---|
  | Master/individual | −5.0 | −3.9 | −1.5 | −1.1 | −2.5 | −4.7 | −7.4 | −8.9 dB |

  - The prediction from H(18.6 k) · H(6.6 k)/H(15.9 k) is −1.1, −2.7, −4.9, −7.6 and −9.0 dB at 4, 6.3, 10, 16 and 20 kHz.
  - The high-pass fits a first-order corner of about 100–120 Hz.
- **Regebro rev-4 unit** (MP3, OH/RD/CR): main/individual is −2.6 to −4.0 dB at 100 Hz, −2.2 dB at 6.3 kHz, −4.5 dB at 10 kHz and −7 dB at 16 kHz. **Polarity is inverted** (cross-correlation sign −1 for OH, RD, CR and CH).
- Regebro's own statements (fork F): "15 kHz 6 dB/oct LPF on the individual outs; extra ~7 kHz 6 dB/oct LPF plus a low-cut on the main outs; main outs phase-inverted".

The schematic, two real units and Regebro's statements all agree.

#### Disagreements

- **RD-9.** Main-output coupling capacitors are 10 µF ("Roland used 1uf"), so there is no 130 Hz low cut. It uses 4580 op-amps on the BD, RS and CP outputs and has the 0.01 µF output capacitors like late 909s [A2-14].
- **Pre-SN-415300 units.** They lack C500–C512, so they have no 15.9 kHz or 6.6 kHz poles and are brighter.

#### Recommendation for our model

- Model the jack board explicitly:
  - per-voice 1 k + optional 10 n;
  - per-voice "individual out in use" flag that removes the voice from the mix (multi-bus plug-in layouts);
  - pan conductances;
  - bus coupling capacitor and IC66 inverting gain with a linear VR28;
  - output network 1 k + 2.2 k / 10 n / 10 k.
- Provide an "early unit" option (no C500–C512).
- Keep the polarity inversion on the main outs.
- All of this is linear and can run at the host rate after decimation.

#### Open questions

- The L/R orientation (label conflict above).
- Whether the R/MONO jack sums L when L is unplugged. The jack switching on JK-1 suggests the L tip contact is normalled to R, but I have not traced it.

---

### A2.6 Power rails and references

- **Rails.** ±15 V from 7815/7915 (IC701/IC702) and +5 V from 7805 (IC703) (p.12).
- **Op-amps.** Voicing-board op-amps are M5218L duals on ±15 V, swinging about ±13.5 V: IC11–15, 17–21, 23–28, 34–35, 37–43, 45–52, 64–67.
- **Logic.** The 4174 latches and 4040/4520 counters run at +5 V (IC1–10, 53, 55, 58, 61, 63, 68, 70–71). The noise generator's 4006 and 4070 run at +15 V. The AN6912 runs between 0 and −15 V.
- **Reset.** D701 (RD5.6JB2 from SN 393000) and Q701/Q702 hold RESET until the IC703 input reaches 7.0 V. This also resets the TRIG/ACCENT latches IC1–IC10 (p.6).
- **Recommendation.** Use the nominal rails as clip limits in the op-amp models: ±13.5 V for M5218, and 0/−15 V for the comparator.

---

### A2.7 Service-notes adjustment, change history, scope figures (all voices)

#### Adjustment

There is no separate adjustment section. The only trimmer is the clap **TM**: "ADJUSTMENT LEVEL: Center, MULTI OUT 2 Vp-p" (p.11). Everything else is fixed.

#### Change information (p.7)

| Serial from | Part | Change | Why (Roland) |
|---|---|---|---|
| 370600 | battery lid | rubber → metal | |
| 381500 | TAPE SYNC filter/amp | C87 10 p deleted; R306 4.7 k → 47 k; R312 470 k → 4.7 M | tape load waveform |
| 381500 | **BD** | **C9 0.22 µ → 0.33 µ** | "expanding the TUNE range" (Regebro "rev 2") |
| 393000 | reset | D701 RD6.8JB2 → RD5.6JB2 | brown-out stops |
| 415300 | **RS** | **R417 12 k → 3.3 k** | "more realistic sound" (Regebro "rev 3", "RS pitch raised") |
| 415300 | **HH** | **C134 10 µ → 0.01 µ** | "rolling off unnecessary lower frequencies" (Regebro: HH high-pass about 0.7 Hz → about 700 Hz) |
| 415300 | RS TRIG OUT | R495 1 k replaces a jumper; R393 10 k → 1 M; R392 22 k → 1 M; C600 0.1 µ at TRIG OUT | stop RS trigger glitching tape sync |
| 415300 | multi jack | C500–C512 0.01 µ added | "Noise is high in OUTPUT" (fault guide) |
| 426700 | **tom noise** | **C54 0.0022 → 0.0047 µ; R194 47 k → 100 k** | "emphasize attack of TOM TOMs" (Regebro "rev 4") |

Other notes:

- Firmware: Group A = 2764 EPROM with no version label (Rev 0, SN 360100–393899). Group B = "Ver. 1" EPROM or 2364 mask ROM (SN 403900 on). "Ver. 2" fixes MIDI-clock STOP/CONTINUE glitches; to check it, hold TRACK 1 at power-on and see whether BD key 2 blinks. Details are in fork C.
- "SN up to 371499: surface mounted" notes appear on some clap parts (R496 / R215 area, C61/D53).

#### Scope figures (p.9, "Observed at MULTI OUT jack with all knobs set to center")

| Voice | Scale | Shows |
|---|---|---|
| BD | 200 mV/div, 5 ms/div | with/without accent |
| SD | 0.5 V/div, 2 ms/div | with/without accent |
| LT | 0.5 V/div (time base not printed for the detail traces; probably 5 ms/div) | with/without accent, the "noise" click marked |
| LT, MT/HT envelopes | 50 ms/div | with accent |
| RS | 2 ms/div | R417 = 3.3 k vs 12 k; the 12 k trace has fewer 1 kHz cycles |
| Hand clap | 500 mV/div, 10 ms/div | 4 spikes, then tail |
| HH closed | 500 mV/div, 20 ms/div | with/without accent |
| HH open | 500 mV/div, 0.1 s/div | |
| Crash | 500 mV/div, 0.1 s/div | |
| Ride | 500 mV/div, 0.1 s/div | |

The traces are hand-drawn and have **no graticule**. Use them only for shape, number of events and relative amplitude.

#### Rim-shot resonator sketches (p.11)

| | Period | Amplitude | Duration |
|---|---|---|---|
| F1 | 2 ms | 30 Vp-p | 20 ms |
| F2 | 4.5 ms | 15 Vp-p | 25 ms |
| F3 | 1 ms | 30 Vp-p | 5 ms |

---

### A2.8 For other sections

- **A1 (BD/SD/toms):**
  - The noise is a 31-bit LFSR at about 455 kHz. Its stream drives the BD attack (block diagram: Noise Gen → LPF → VCA Q6 with ENV2), the SD snappy and the tom noise (Q33 VCA, D26/D44/D45 OR from the three tom triggers).
  - Accent DACs are 6-bit, 0–5 V, 2.54 kΩ source.
  - Revisions: SN 381500 C9 (BD TUNE range); SN 426700 C54/R194 (tom attack).
  - Scope figure ratios: BD 2.7×, SD 2.1×, LT 1.65× accented/unaccented at TA centre.
  - Regebro four-level accent data for BD/SD/HT.
- **B (hats/cymbals):**
  - C134 10 µ → 0.01 µ at SN 415300 (≈ 700 Hz HH high-pass on late units).
  - HH and cymbal accent DACs are shared: IC7 for CH and OH, IC9 for crash and ride.
  - Pan: HH right, crash slightly right, ride left (my reading).
  - Multi-out 15.9 kHz pole and main-out 6.6 kHz pole affect perceived hat brightness.
  - AudioRealism OH master-vs-individual is a clean transfer-function probe.
  - Whittle's HH tuning mod: R492 12 k / C160 470 p set the HH oscillator [A2-5].
- **C (firmware):**
  - DAC writes are 6 bits on D1–D6 (D6 = MSB).
  - TRIG writes are 2 ms 5 V pulses; IC1 uses D1–D5 (BD…HT), IC10 uses D1–D6 (RS, HC, ride, crash, HH, closed/open).
  - Needed: the bit-to-voice mapping, the LS138 latch selection, the accent code rule (normal vs instrument accent vs total accent, additive or not; dead zones at 0–10 and 90–100 % of the knob) and the AN3 scaling.
  - RS and CP share one accent DAC, and so do crash and ride.
- **D (emulations):**
  - RD-9 deviations per maffez [A2-14]: different noise generator, 10 µF main coupling (no low cut), 4580 op-amps on BD/RS/CP outs, discrete BA662 clone, STM32F746 CPU.
  - Tiptop CP909/RS909 claim exact reproductions plus extras [A2-13].
  - Hexinverter NeinOhNein CLAP/RIM [A2-11].
- **E/F (reference audio):**
  - The AudioRealism pack is the best single source for RS/CP: 96 kHz, accent sweeps, shots, SD + CP from the master out.
  - The Regebro SoundCloud file (individual + main, 4 accent levels) is dual-mono. A true stereo master-out recording of solo LT/HT/HH/ride is needed to settle panning.

---

### References (A2)

| # | Source | URL | Local copy (docs/papers/ unless noted) |
|---|---|---|---|
| A2-1 | Roland, *TR-909 Service Notes*, 1st ed., 15 Jun 1984 | https://archive.org/details/synth_Roland_TR-909_service_notes | `roland/TR-909_service_notes_A.pdf`, `…_A_text.pdf`, `.txt`, page scans `roland/service_notes_pages/pNN.png` |
| A2-2 | Roland, *TR-909 Owner's Manual* | https://archive.org/details/synthmanual-roland-tr-909-owners-manual | `roland/TR-909_owners_manual.pdf`, `.txt` |
| A2-3 | Roland, *TR-808 Service Notes*, 15 Jun 1981 (CP/MA circuit description, Fig. 13 "Hand clap generating cycle", schematic p.9) | https://archive.org/details/synthmanual-roland-tr-808-service-notes | `clones/hc_tr808_ref/TR-808_service_notes.pdf`, `TR-808_service_notes_djvu.txt` |
| A2-4 | Network 909, TR-909 circuit pages (Hand Clap, Rim Shot) | http://www.network-909.de/circuit.htm | `web/network909/handclap.*`, `rimshot.*` (downloaded by A1) |
| A2-5 | R. Whittle, *TR-909 sound mods* (HCP section: Q38 log-linear converter, TM, R201, R208 impedance) | https://www.firstpr.com.au/rwi/tr-909/TR-909-Sound-Mods.pdf | `web/whittle/TR-909-Sound-Mods.pdf`, `.txt` (A1) |
| A2-6 | G. Reid, *Synth Secrets 41: Synthesizing Cowbells & Claves*, SOS Sep 2002 (808 claves/rimshot bridged-T) | https://www.soundonsound.com/techniques/synthesizing-cowbells-claves | `sos/synthesizing-cowbells-claves.{html,txt,md}` |
| A2-7 | K. J. Werner, PhD thesis 2016, Table 2.2 (bridged-T values of 808 voices incl. rimshot/clave) | (see A1) | `academic/werner_2016_phd_wdf_virtual_analog.pdf` (A1) |
| A2-8 | K. J. Werner et al., DAFx-14, TR-808 bass drum (bridged-T theory; notes that the 808 HC/CB/CY/HH use bridged-T band-passes) | (see A1) | `academic/werner_dafx14_tr808_bass_drum.pdf` (A1) |
| A2-9 | KVR, "Emulating the TR-909(808) clap in detail", t=466450 | https://www.kvraudio.com/forum/viewtopic.php?t=466450 | `web/voices2_kvr_t466450_909_clap.txt` (extract) |
| A2-10 | Baratatronix, "Roland TR-808 Clap Synthesis" | https://www.baratatronix.com/blog/cascadia-808-clap-synthesis | `clones/hc_misc/baratatronix_808_clap_synthesis.{html,txt}` |
| A2-11 | Hexinverter, NeinOhNein CLAP v1.0 (DIY 909 clap): assembly guide and page | http://www.hexinverter.net/neinohnein-clap ; http://data.hexinverter.net/NeinOhNein_CLAP_v1_assemblyguide.pdf | `clones/hc_hexinverter_neinohnein/` |
| A2-12 | Delptronics (mickeydelp), *Thunderclap* (909-style clap, LM13700 replacing BA662) and 909 clap schematic crop | https://mickeydelp.com/blog/thunderclap | `clones/hc_misc/mickeydelp_*` |
| A2-13 | Tiptop Audio, 909 analog modules (CP909, RS909) | https://tiptopaudio.com/909-2/ | `clones/hc_misc/tiptop_909_modules.{html,txt}` |
| A2-14 | maffez, *Behringer RD9 Modifications V2*, 27 Jul 2021 (RD-9 vs 909 deviations) | https://maffez.com/wp-content/uploads/2021/07/RD9_MODIFICATIONS_V2.pdf | `clones/mix_rd9_maffez/RD9_MODIFICATIONS_V2.{pdf,txt}` |
| A2-15 | Gearspace threads 955799 (pan) and 750724 (mix-out HPF) — search snippets only (403) | see file | `web/voices2_gearspace_pan_and_mixout_snippets.txt` |
| A2-16 | electro-music.com topic 58143 (909 clap repair, signal path) | https://electro-music.com/forum/topic-58143.html | `web/voices2_electro-music_t58143_909_clap_repair.txt` |
| A2-17 | e-licktronic forum, collection of 909 mods (noise phasing, 9090 second noise board, clap mods) | (A1) | `web/voices_elicktronic_t772_collection_of_909_mods.txt` (A1) |
| A2-18 | Jacob Korn TR-909 mods (RIM tune R414, CLAP tune R207, TM) | (A1) | `web/voices_jacobkorn_tr909_mods.txt` (A1) |
| A2-19 | AudioRealism, *TR-909 sample pack* (853 samples, 96 kHz/24-bit, individual outs, accent sweeps, master-out combos) | https://audiorealism.se/downloads/TR-909-96kHz-24bit.zip | `reference/real909/packs/audiorealism_tr909_ultimate/` (fork E) |
| A2-20 | Regebro, RD-9 vs TR-909 videos and SoundCloud set (rev-4 unit; accent levels; individual and main outs) | https://www.youtube.com/watch?v=nMskZzaZ92U ; https://soundcloud.com/regebro/rd-9-and-tr-909-sounds-individual-and-main-outs/s-NtizHWJ4tcg | `reference/real909/videos/regebro_*` (fork F) |
| A2-21 | MAME `roland_tr909.cpp` (DAC/TRIG write masks, AN3 = TOTAL) | https://github.com/mamedev/mame/blob/master/src/mame/roland/roland_tr909.cpp | `reference/mame/src/mame/roland/roland_tr909.cpp` |
| A2-22 | Noise Engineering, "Percussion Synthesis part 3: The Sound of One Synth Clapping" (general clap synthesis) | https://noiseengineering.us/blogs/loquelic-literitas-the-blog/percussion-synthesis-part-3-the-sound-of-one-synth-clapping/ | `clones/hc_misc/noiseengineering_clap_blog.{html,txt}` |
| A2-23 | SyntherJack, "The perfect 808 rimshot / clave" (bridged-T clone notes, 808) | https://syntherjack.net/the-perfect-808-rimshot-clave/ | `clones/rs_misc/syntherjack_808_rimshot_clave.{html,txt}` |
| A2-24 | e-licktronic Nava v1.02 (open-source 909 clone): Eagle schematic, parts list and extracted netlist; one-to-one copy of the late-revision voicing board with Roland designators | (see fork D) | `emulations/code/e-licktronic_Nava-v1.0/Eagle/Nava_v1.02_{netlist.txt,parts.csv,sch}` (fork D) |

---

## 4. Sample voices: hi-hat, crash, ride and the sample ROMs

This section covers the TR-909's three "digital" voices: open and closed hi-hat (OH and CH), crash and ride. It goes from the trigger to the instrument output, then covers the ROM data format and measurements from two real 909s.

"My calc" marks numbers derived from component values. "Measured (this work)" marks numbers obtained by cross-correlating the ROM data with real-909 recordings. The scripts are in `reference/rom_decoded/scripts/` and the results in `reference/rom_decoded/measurements.json`.

Sources for this section are tagged [B-n] and listed at its end.

### B.1 Summary for the model

- **Playback order.** All public copies of the three ROMs are in playback (counter) order: the MAME `hn61256p__c4x` files, Colin Fraser's 2002 WAVs and Trevor Page's 9090 `.hex` files are byte-identical.
  - Each byte holds a **6-bit DAC code in bits 7..2**. The code is offset binary, so 0x80 (code 32) is zero. Bits 1..0 are not used by the 909.
  - **Hi-hat ROM:** OH = 0x0000–0x5FFF (24 576 samples). CH = 0x6000–0x7FFF (8 192 samples). The CH starts with 65 zero samples (2.06 ms).
  - **Crash and ride:** each plays all 32 768 samples.
- **Sample clock (measured on two different 909s).**
  - Hi-hat: **31.57 kHz, fixed**. This is half of an RC oscillator at ≈63.1 kHz (R492 12 k, C160 470 pF).
  - Crash and ride, with TUNE: **≈28.0 kHz (min) → ≈36.7 kHz (centre) → 53.8–56.1 kHz (max)**, i.e. about one octave.
  - Playback lengths: OH 778 ms, CH 259 ms, crash/ride 1.17 s (TUNE min) to 0.6 s (TUNE max).
- **Hi-hat envelope.** The VCA gain is held at maximum for **2.0 ms** (the TRIG pulse), then decays as a pure exponential. **τ_OH = C135·((100 k + VR23) ∥ 220 k) ≈ 70–200 ms.** **τ_CH ≈ 8–51 ms**, and τ_CH also depends on the OH DECAY knob.
  - The 220 k input resistor of the envelope amplifier (R453) loads C135. This is why the OH DECAY maximum is ≈ 0.2 s, not the ≈ 1.1 s the 1 MΩ pot suggests.
  - The sound is cut hard when the counter stops.
- **Crash/ride envelope.** The VCA gain is driven by a 6-bit DAC on the ROM address bits A9–A14. It is **not exponential**.
  - Measured law: **g = ((63 − ⌊n/512⌋)/63)^1.10**, where n is the ROM address. The fit error is 0.27 dB.
  - The gain falls in 64 steps of 512 samples. It is identical at every TUNE setting, so the envelope always ends with the sample.
- **Accent.** Accent sets the base voltage of a matched PNP pair, so the gain is exponential in the accent-DAC voltage. My calc: 6.4 dB/V for the hi-hat and 9.0 dB/V for crash/ride.
  - Measured AudioRealism `accent000→100`: +7.0 dB (CH), +7.3 dB (OH), +8.2 dB (crash), +7.6 dB (ride).
- **Output chain.**
  1. 6-bit binary-weighted resistor DAC (RM0621, R = 5 k).
  2. 10 µF AC coupling into 22 Ω.
  3. Transistor "voltage-controlled potentiometer" VCA into an op-amp I→V stage (47 k).
  4. Two unity-gain Sallen-Key stages forming a ≈4th-order Butterworth low-pass at ≈26–28 kHz (my calc).
  5. LEVEL amplifier (22 k in, 50 k(B) feedback, gain 0…2.27).
  6. Multi-out RC of 1 k / 0.01 µF (15.9 kHz).
- **Revision differences.**
  - From serial 415300, the **hi-hat level-stage coupling cap C134 changed from 10 µF to 0.01 µF**. The high-pass moves from 0.7 Hz to **723 Hz**.
  - From serial 392600, the crash/ride oscillator input resistors R442/R440 changed from 33 k to 10 k.
  - Two reference units show both hi-hat variants.

### B.2 Circuit chain (Service Notes, voicing board) [B-1]

The digital voice circuits are on voicing board 73133090, schematic page 11 rows A–G. The circuit description is on page 5 and the waveforms on page 9. High-resolution crops are in `docs/papers/samples/service_notes_crops/`.

The three voices are built the same way. The Service Notes: *"Circuit configurations and operations of these voices are basically the same. The following description takes Hi-Hat as a representative."*

#### B.2.1 Trigger, run/stop and clock

**Trigger.**
- The CPU latches TRIG (a 5 V, 2 ms pulse) into IC1/IC10. For the hi-hat it also latches the CLOSED/OPEN line.
- A 100 pF / 100 kΩ differentiator turns the TRIG edge into a ≈10 µs reset pulse (τ = 10 µs). These are C159/R491 (HH), C50/R189 (crash) and C40/R188 (ride).
- The reset pulse clears the address counters and the timing flip-flops.

**Address counters.**
- Each voice uses a CD4040 (12 bits, counter bits 0–11) followed by half of a CD4520 clocked on its EN input (bits 12–15).
  - Hi-hat: IC71 + IC70b.
  - Crash: IC61 + IC58a.
  - Ride: IC55 + IC58b.

**Run/stop.**
- **Hi-hat:** NAND IC72a produces RUN/STOP. RUN gates the oscillator (IC72d pin 13), drives the ROM's CS pin (26) and holds the VCA open through R488 2.2 k.
  - The stop condition is `Q2B ∧ (Q3B ∨ CLOSED)`. The counter therefore stops at **0x6000 in OPEN mode** and **0x2000 in CLOSED mode**, as in the Service Notes text.
- **Crash/ride:** counter bit 15 (4520 Q4A/Q4B) drives ROM CS. Through NAND 59d (crash) or 57d (ride), the inverted bit 15 gates the oscillator and mutes the VCA (R427 / R437 2.2 k).
  - Each cymbal therefore plays exactly 32 768 samples and stops.

**Oscillator.**
- Each oscillator is the classic two-gate CMOS RC astable built from unbuffered 4011UB NAND gates. Trevor Page's 9090 guide notes that the 4011 *must* be the unbuffered type [B-6].
- **HH:** IC72c/d, timing R **R492 12 k**, input resistor R493 33 k, **C160 470 pF polystyrene**.
- **Crash:** IC59c/b, timing R **R443 6.8 k + VR25 10 k(B) TUNE**, **C131 470 pF**, input resistor R442 33 k (**10 k from SN 392600**, annotated on the schematic).
- **Ride:** IC57b/c, **R441 6.8 k + VR27 10 k(B) TUNE**, **C130 470 pF**, R440 33 k→10 k.
- The Service Notes say the hi-hat oscillator *"outputs about 60kHz, which is divided by two and shaped up by IC73 flip-flop (TIMING GEN), clocking the address counters"*.

**Timing generator.**
- Two 4013 toggle flip-flops are clocked on opposite oscillator phases: 73a/b (HH), 60a/b (crash) and 56a/b (ride).
- They give two quadrature f_osc/2 square waves:
  - one clocks the 4040;
  - one drives the ROM's clocked chip-enable (pin 22, CE);
  - a NAND of the two clocks the 40174 data latch.
- **Sample rate = f_osc / 2.**
- Colin Fraser notes the HH ROM's CE is mask-programmed active-high and the crash/ride CE active-low, so the hi-hat uses the Q output and the cymbals use /Q [B-3].

**HN61256P ROM.** It is a slow CMOS mask ROM with an internal latch (Hitachi datasheet [B-2]):
- 32 K × 8, address access 3.5 µs max, cycle time 4.0 µs min. That allows up to 250 kHz, so it never limits playback.
- CE is clocked (address latched on the CE edge).
- One mask-programmable chip select (CS, pin 26) and a mask-programmable CE active level.
- Pinout: 1 NC (A15 in ×4 mode), 2 A13, 3 A7, 4 A6, 5 A5, 6 A4, 7 A3, 8 A2, 9 A1, 10 A0, 11–13 D0–D2, 14 Vss, 15–19 D3–D7, 20 A11, 21 A10, 22 CE, 23 A12, 24 A9, 25 A8, 26 CS, 27 A14, 28 Vcc.
- This differs from a 27256: pin 20 is /CE, 22 is /OE, 23 is A11, 26 is A13, 2 is A12 and 27 is A14 on a 27256.

#### B.2.2 Address and data scrambling on the PCB

I read the following from schematic page 11 and the Hitachi pinout. Colin Fraser confirms that *"the address and data lines on the Roland ROMs are also jumbled up, presumably to simplify PCB routing"* [B-3].

**Counter bit k → ROM pin → HN61256 address line:**

| counter bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ROM pin | 10 | 9 | 20 | 21 | 23 | 24 | 7 | 6 | 8 | 5 | 4 | 25 | 3 | 27 | 2 |
| chip addr | A0 | A1 | A11 | A10 | A12 | A9 | A3 | A4 | A2 | A5 | A6 | A8 | A7 | A14 | A13 |

- For the hi-hat, counter bits 13 and 14 are diode-ORed with CLOSED (D196–D199, pull-downs R486/R487 10 k). A closed hat therefore reads the top 8 K.
- The same pin sequence is drawn for the crash (IC62) and the ride (IC54).

**Data path.**
- ROM D0..D5 (pins 11, 12, 13, 15, 16, 17) enter the 40174 on pins 14, 13, 11, 6, 4, 3. The outputs (pins 15, 12, 10, 7, 5, 2) drive the RM0621 network.
- The legend on page 11 gives the network as **R, 2R, 4R, 8R, 16R, 32R with R = 5 k**.
- If the array resistors are drawn in that order, the DAC weights are D2 = 32, D3 = 16, D1 = 8, D4 = 4, D0 = 2, D5 = 1. D6 and D7 are unused.
- The drawing-order assumption is supported by two other DACs drawn in the same convention. The crash address DAC (RA11) comes out in natural binary order (bit 14 = R … bit 9 = 32R), and so do the accent DACs.
- **Only 6 of the ROM's 8 outputs are connected** (pins 11–13, 15–17). Pins 18/19 (D6/D7) go nowhere.
- Two clones that re-route the board for 27C256 EPROMs with the public (playback-order) data confirm this:
  - **e-licktronic Nava v1.02** keeps Roland's designators [B-21]. Its netlist has 4040 Q1..Q12 → A0..A11, 4520 Q0 → A12, Q1/Q2 (OR CLOSED via D196–D199) → A13/A14, and **EPROM D7..D2 → 74HC174 D0..D5 → RA9 5 k (MSB) … 160 k (LSB)**. EPROM D1/D0 are not connected.
  - The **9090** is wired the same way [B-6].

**Consequence: the public images are already descrambled.**
- A raw chip dump would be scrambled in both address and data. The public images are not:
  - I tested all 105 transpositions of the 15 address bits: every one makes the spectrum flatter (whiter) than the file order.
  - Of the 720 permutations of the 6 data bits, the file order gives the least-white spectrum, so the bit order is the natural one.
- Colin Fraser explains why. He *"got hold of a real 909 … solder[ed] wires onto the PCB to extract the sample data"*, *"didn't bother working out the counter bit to address pin mapping … just extracted the data byte by byte and used the nominal address lines for my clone … only had to work out which bits were which on the data lines"*. His files have *"each sample … shifted by two bits to occupy eight bits (unsigned)"* [B-3][B-4].
- The MAME files, Colin's `r909hh/cr/rd.wav` (32 768 bytes of data each) and the 9090 `909hh/crash/ride.hex` [B-6] are **byte-identical** (checked).
- So **the emulator can use the ROM files directly as time-ordered 6-bit samples**.
- Anyone programming EPROMs for a real 909 or RE-909 socket would need the scramble above, or an adapter that re-routes the lines.
  - A search-engine snippet of a later post in [B-9] describes a 27C256 adapter mapping pins 2→26, 20→23, 22→20, 23→2 and 26→22. That is a plain pin-function translation; it does not undo the scramble. I could not retrieve the post itself.

#### B.2.3 DAC, VCA and output filter

**DAC.**
- Six CMOS latch outputs (0/5 V) drive the binary-weighted RM0621 resistors (5 k … 160 k) into a common node.
- My calc: open-circuit V = 5 V·code/63, Thevenin resistance 2.54 kΩ.

**Coupling.**
- C157 (HH), C127 (crash) or C128 (ride), each 10 µF/16 V, feeds a node shunted by **22 Ω** (R482 / R431 / R438).
- My calc: high-pass at 6.2 Hz. The 22 Ω turns the DAC into a small current source, about 43 mV p-p across the shunt at full scale.

**VCA.**
- One transistor marked (S), "selected": Q85 (HH), Q71 (crash) or Q69 (ride). It sits between the 22 Ω node and the virtual ground of an op-amp I→V stage with 47 k feedback (R483 / R425 / R424).
- The Service Notes call it *"configured as a voltage controlled potentiometer"*. With millivolt V_CE it works as a conductance roughly proportional to its base current.
- Base drive comes from the collector of a matched dual PNP (2SA798: Q84 HH, Q70 crash, Q68 ride), wired as a log/antilog current mirror:
  - An op-amp (IC67a HH, IC52b crash, IC52a ride) with its + input at **+5 V** forces the reference transistor's collector current to equal the envelope input current. This op-amp is the "anti-log" stage.
  - The output transistor's current is I_ctl = I_env · exp(ΔV_B/V_T), where ΔV_B is the difference between the two base voltages.
  - The +5 V reference is confirmed by the 9090 redraw, which labels these nodes "+5" [B-6].
- **Envelope input current I_env.**
  - **HH:** I_env = (5 V − V_C135)/R453 220 k, i.e. 22.7 µA at the trigger.
  - **Crash:** I_env = (5 V − V_RA11)/(R432 270 k + 2.54 k) = 18.3 µA·(63 − code)/63.
  - **Ride:** the same with R439 470 k, i.e. 10.6 µA·(63 − code)/63.
- **Base network: accent, and mute when stopped.**
  - HH: reference base R489 150 Ω to +5 V, R481 4.7 k from the accent DAC, R488 2.2 k from RUN/STOP; the other base R490 100 Ω to +5 V.
  - Crash: R428 220 Ω, R426 4.7 k (accent), R427 2.2 k (run), R429 100 Ω.
  - Ride: R433 220 Ω, R435 4.7 k, R437 2.2 k, R434 100 Ω.
  - My calc: ΔV_B = −(5 − V_acc)·0.0190 (HH) and −(5 − V_acc)·0.0269 (crash/ride), i.e. **6.4 dB/V and 9.0 dB/V**.
  - When stopped, the 2.2 k pulls ΔV_B to about −0.3 V, which is below −100 dB, so the VCA is muted.
- The Service Notes describe the crash envelope as *"converted to analog voltages through RA11, anti-log tapered by IC52b and Q70, and … applied to the base of Q71 (VCA)"*. In fact the exponential acts on the accent voltage. The address DAC enters as a *linear* current; see the measurement in B.5.

**Low-pass after the I→V stage (my calc).** Two unity-gain Sallen-Key sections with emitter-follower buffers, identical for all three voices:
- R480/R475, R456/R457, R462/R463 = 5.6 k; C148/C136/C144 = 1200 pF bootstrap; C149/C137/C145 = 1000 pF to ground. **f0 = 25.9 kHz, Q = 0.548.**
- R477/R479, R459/R461, R465/R467 = 5.6 k; C150/C138/C146 = 2700 pF bootstrap; C151/C139/C147 = 390 pF. **f0 = 27.7 kHz, Q = 1.316.**
- Together these are close to a **4th-order Butterworth at ≈26.8 kHz** (Butterworth Qs are 0.541 and 1.307).
- With f_s ≈ 31.6 kHz this filter does *not* remove the first image band (15.8–31.6 kHz). Images up to ≈27 kHz pass, and part of the hats' top-end "sizzle" is aliasing image energy.

**Level stage.**
- HH: C134 → R444 22 k → IC65b, with VR22 50 k(B) in the feedback.
- Crash: VR24 around IC65a.
- Ride: C133 10 µF → R446 22 k → IC64a, VR26 50 k(B).
- Gain 0 … −2.27, linear in rotation.
- **C134 (HH only) changed from 10 µF to 0.01 µF at SN 415300**, *"For rolling off unnecessary lower frequencies"* (page 7, Change Information). With R444 22 k into the virtual ground that is **0.72 Hz → 723 Hz**. Regebro's YouTube comment, relayed by fork F, describes the same change.

**Jacks.**
- Multi-jack board: R500… 1 k series, C500–C512 0.01 µF to ground. Fitted from SN 415300; the fault guide says to add them on earlier units for noise.
- That is a 15.9 kHz first-order low-pass at the individual outputs before the 12 k/15 k mix resistors.

**Observed in the recordings.** I estimated the transfer function from recording vs ROM render with `tf.py`:
- The AudioRealism unit's hats are ≈ −10 dB at 200 Hz, −2.5…−6.5 dB at 400 Hz and −1.3 dB at 1 kHz. That is the 723 Hz high-pass, so a late unit.
- The Rob Roy unit is flat down to ≈ 200 Hz, so an early unit with C134 = 10 µF.
- Both lose 3–5 dB at 15–20 kHz, consistent with the 15.9 kHz jack RC plus the 4-pole filter.

#### B.2.4 Hi-hat envelope generator

**Circuit.**
- C135 (1 µF/50 V) sits on the input of R453 (220 k → IC67a virtual node at +5 V).
- **TRIG** (2 ms) turns on Q74 (NPN; R454 22 k / R455 10 k divider), which discharges C135. The envelope is therefore held at maximum for the length of the trigger pulse.
- **CLOSED** (high) turns on Q72 (R447 22 k / R448 10 k), which turns on PNP switch Q73 (R450 10 k / R449 22 k). C135 then charges from +5 V through **R451 10 k + VR21 100 k(B) "CH DECAY"**.
- The **R452 100 k + VR23 1 M(A) "OH DECAY"** path from +5 V is connected in both modes.
- Service Notes: *"Since this charging path is 1/tenth the total resistance of R452 and VR23, the charging rate of C135 depends on VR21 setting."*

**Time constants (my calc).** C135 charges toward +5 V, which is also R453's virtual-node voltage. So I_env = (5 − V_C135)/220 k decays as a pure exponential to zero, and R453 itself adds to the charging admittance:
- **τ_OH = C135·((R452 + VR23) ∥ R453) = 1 µF × (68.8 k … 183 k) = 69 … 183 ms.**
- **τ_CH = C135·((R451 + VR21) ∥ (R452 + VR23) ∥ R453) = 8.7 … 69 ms.** CH depends on the OH DECAY knob.

**Measured (this work).** Gain extracted against the aligned ROM data, so the decay of the stored sample is removed (fig. `reference/rom_decoded/fig_hihat_envelopes.png`):
- The fit **g = 1 for t < 2.0 ms, then exp(−(t − 2 ms)/τ)** reproduces every take to within 0.2–0.4 dB rms. The fitted hold is 2.0 ms in every take.
- Rob Roy 909, CH DECAY dial 0, 2, 4, 6, 8, 10: τ = **10.4, 19.9, 31.8, 42.6, 48.7, 50.6 ms**.
- Rob Roy 909, OH DECAY dial 0 … 10: τ = **77, 105, 129, 149, 185, 202 ms**.
  - With C135 ≈ 1.1 µF this matches the formula above: OH min 68.8 k → 76 ms, OH max 183 k → 201 ms.
  - The intermediate values follow the audio taper of VR23 (1 M(A)).
- AudioRealism 909 (`decay000…100`): CH τ = 7.5, 25.7, 33.0, 40.9, 41.1 ms; OH at `decay100` τ = 182.5 ms.
  - The CH maximum (≈ 41 ms) matches the formula if OH DECAY was at minimum during the CH takes: 110 k ∥ 100 k ∥ 220 k = 40.7 k → 41 ms.
- **Cross-check with fork E** (independent overall-level measurement on the AudioRealism pack): CH −20 dB at 25–92 ms across DECAY 0–100 %, and OH −20 dB at 262 ms at full DECAY.
  - Using the stored-sample envelope times my VCA fits, I predict CH −20 dB at 23 ms (τ 7.5 ms) to 92 ms (τ 41 ms) and OH −20 dB at 265 ms (τ 182 ms). Both agree.
  - The stored OH sample alone falls 20 dB only after ≈ 770 ms. The audible decay is set by the VCA.

**Hard stop.** The OH plays at most 24 576 samples (778 ms at 31.57 kHz); the stored data ends at 0x5FBA, 776 ms. At the maximum τ of 0.2 s the VCA is about −36 dB by then, so the cut is barely audible.

**Choke.** A new trigger resets the counter, switches the address range (CLOSED/OPEN) and re-discharges C135. A CH cuts an OH instantly and the new envelope starts at maximum. The AudioRealism `OpenHat-Dec100-cut-ClosedHat-*` files show this.

#### B.2.5 Crash and ride envelope

**How it works.**
- Counter bits 9–14 drive a second RM0621 DAC: RA11 (crash), RA13 (ride). Bit 14 has weight R (MSB) and bit 9 has 32R.
- The DAC output therefore ramps up in 64 steps of 512 samples. Through R432/R439 and the log/antilog pair it gives a control current that ramps **down linearly** to ≈ 0 at the last step.
- Because the envelope is clocked by the address counter, *"the decay rate of the AR envelope [is] dependent upon the progress of the address counter, thus ensuring that, no matter how much you mess with the clock, the VCA always shapes the sound correctly"* (Gordon Reid [B-11]).
- Colin Fraser: *"…converted to an exponential decay with the same length as the sample regardless of tune setting"* [B-4]. The "same length" part is correct; the "exponential" part is not (see the measurement).

**Measured (this work).** Gain per 512-sample block on the AudioRealism unit (crash and ride at TUNE 0/50/100; fig. `fig_cymbal_env_vs_code.png`):
- All six curves coincide as a function of code, i.e. the envelope does not depend on TUNE.
- They are fitted by **g = ((63 − code)/63)^1.10**, rms error 0.27 dB.
  - −6.2 dB at code 32, −14.5 dB at code 48, −21.7 dB at code 56, −31 dB at code 60.
  - A pure linear current law gives −6.2, −12.5, −19.1 and −26.4 dB. The extra curvature (exponent 1.1) is attributed to the Q71/Q69 transistor VCA.
- An exponential decay (straight line in dB) does not fit at all.
- The stored crash/ride data are themselves "flattened": block RMS falls only ≈ 9 dB across the ROM. The Service Notes: *"Before being stored into the ROM, the envelope of CRASH is changed…"*. Hoshiai: *"No EQ or compression was applied but the amplitude was processed on a computer after digitization"* [B-12].

### B.3 ROM data format and layout

**Files.**
- ROOT/rom, from the MAME `tr909` set: `hn61256p__c43.ic69` (hi-hat), `__c42.ic62` (crash), `__c44.ic54` (ride), 32 768 bytes each [B-5].
- Service-notes parts list: 15179633 HN61256P-PC42 crash, 15179634 -PC43 hi-hat, 15179635 -PC44 ride.

**Byte format.**
- 6-bit code = `byte >> 2`, bit 7 = MSB, offset binary: 0x80 is code 32, the zero level.
- Ranges: HH −32…+31, crash −29…+28, ride −27…+28.
- **Bits 1..0:**
  - Always 0 in the crash and ride images.
  - In the HH image they are non-zero only in 0x3FC1–0x5FBA, the quiet OH tail. There they correlate with the signal like real extra LSBs: corr 0.35 with the residual, and they lower the high-frequency residual.
  - They are presumably the two unused chip outputs (D6/D7), which Colin's capture placed at bits 1..0, carrying LSBs of Roland's 8-bit master in that region.
  - **The 909 hardware ignores them**: only 6 data lines reach the latch. The same is true of the clones with this data: Nava and 9090 leave EPROM D1/D0 unconnected.
  - **The disagreement is only about wording.** Colin says the lowest 2 bits are "not used", and the GPL hobby players mask them as "dirty" (naokiiwakami `utils/read_wav.py`: `sample & 0xFC`) [B-22]. The HH file nevertheless contains data in those bits. Both statements are true: present in the file, unused by the circuit.
  - The emulator must use `byte >> 2` (or `byte & 0xFC`).

**Hi-hat ROM layout.** The address table on Service Notes page 5 reads "OPEN HI-HAT 000 0000 0000 0000 ↓ COMMON ADDRESS 110 0000 0000 0000 ↓ CLOSED HI-HAT 111 1111 1111 1111".

| region | addresses | samples | content |
|---|---|---|---|
| OH | 0x0000–0x5FFF | 24 576 | Short ≈ 3 ms onset. Block RMS 37.5 → 4.9 codes by 0x5C00. Last non-zero 0x5FBA, then 69 zeros. |
| CH | 0x6000–0x7FFF | 8 192 | 65 zero samples (0x6000–0x6040, 2.06 ms), then the sound (RMS ≈ 46 → 0.6). Last non-zero 0x7C79, 0x80 to the end. |

- The OPEN counter stops at 0x6000. The CLOSED counter stops at 0x2000, with A13/A14 forced high, so it reads 0x6000–0x7FFF.
- The CH's 2.06 ms of leading silence lines up with the 2 ms envelope hold. As a result the CH sound starts just as the decay begins, about 2 ms after the trigger. Recording trims in both sample packs start ≈ 2.3 ms into the ROM.
- **The hardware open/closed split is exactly 0x6000.** It is set by the counter logic (bits 13/14 ORed with CLOSED) and agrees with the Service Notes address table.
- Hobby players split the ROM elsewhere:
  - The naokiiwakami TR-909 clone firmware starts the CH at 0x6040 (`ADDRESS_CLOSED_HI_HAT_START`) [B-22]. That skips the 65 leading zeros, so its closed hat sounds 2 ms earlier relative to the trigger than on a real 909.
  - TylerK07's AVR909 packs four 6-bit samples into 3 bytes. Its segment ends at bytes 18 444 and 24 576, i.e. samples 24 592 (0x6010) and 32 768. It plays at a fixed 32 kHz (20 MHz / 625) [B-23].
  - Neither is a reference for the split; both fall inside the 65-sample silent lead-in of the CH region.

**Crash / ride layout.**
- Data from 0x0001 to 0x7EA2 (crash) / 0x7FBD (ride), then 0x80.
- Both are the same cymbal, a Paiste 18″ Thin Crash: *"For the crash I struck it on the edge and for the ride I struck it on the bow"* [B-12].
- Hi-hat: 14″ Paiste Sound Edge top and 14″ A. Zildjian New Beat bottom. Recorded with a Sony C-38 to a Pioneer reel-to-reel on the office floor at night, through an in-house A/D. Edited on a CP/M-80 computer [B-12][B-13][B-14].

**Decoded files** in `reference/rom_decoded/` (all ROM-exact):
- `hh_open.wav`, `hh_closed.wav`: 31 570 Hz.
- `crash.wav` (36 680 Hz), `ride.wav` (36 830 Hz): TUNE centre.
- `crash_tune_min/max.wav`, `ride_tune_min/max.wav`.
- `hh_open_with_lowbits_8bit.wav`.
- `*_raw8.wav`: unsigned 8-bit, byte-identical to the ROMs.
- `fig_rom_overview.png`.

### B.4 Playback rates

**Method.**
- Render the ROM as zero-order hold at a trial clock, band-pass both signals 1.5–12 kHz, and maximise the normalised cross-correlation with real-909 recordings. Peak NCC is 0.94–0.98 and drops below 0.6 for a 0.25 kHz error.
- Lag tracking then confirms the rate over the whole sample (drift < 0.05 %).
- Recordings: the AudioRealism 96 kHz/24-bit pack [B-15], and the Rob Roy 1995 44.1 kHz pack upsampled to 96 kHz [B-16].

| voice / setting | AudioRealism 909 | Rob Roy 909 | samples → duration |
|---|---|---|---|
| CH, OH (fixed) | 31 570 / 31 580 Hz | 31 560–31 580 Hz | OH 24 576 → 778 ms; CH 8 192 → 259 ms |
| crash TUNE 0 % | 28 060 | 27 620 (dial 0) | 32 768 → 1.17 s |
| crash 10/20/30/40 % | 28 375 / 30 135 / 32 370 / 34 275 | 29 530 (dial 2), 33 660 (dial 4) | |
| crash 50 % | 36 680 | — | 0.89 s |
| crash 60/70/80/90 % | 38 980 / 42 075 / 46 610 / 51 945 | 41 270 (dial 6), 48 470 (dial 8) | |
| crash 100 % | 53 835 | 55 780 (dial 10) | 0.61 s |
| ride 0 % | 28 065 | 26 750 | 1.17 s |
| ride 50 % | 36 830 | 38 630 (dial 4 & 6, duplicate files) | 0.89 s |
| ride 100 % | 56 110 | 55 480 | 0.58 s |

Full AudioRealism table: ride 0–100 % in 10 % steps = 28 065, 28 585, 30 090, 32 395, 35 000, 36 830, 39 820, 43 830, 48 185, 54 435, 56 110 Hz.

**Fork E agrees** (separate method on the same AudioRealism pack): crash 28.05 → 53.8 kHz, ride 28.06 → 56.1 kHz, hats ≈ 31.57 kHz. Fork E locked only the ride on the Rob Roy pack; my lag-tracked method locked all of them (NCC 0.95–0.98).

**Rates vs component values.**
- The hi-hat clock 31.57 kHz implies f_osc = 63.14 kHz, as the Service Notes' "about 60 kHz ÷ 2" says. The constant is **k = T_osc/(R·C) = 2.81** (R492 12 k, C160 470 pF).
- An idealised CMOS astable (threshold VDD/2, input clamp diodes, series input resistor; `astable.py`) gives k = 2.10–2.20, i.e. 40.8 kHz. The real 4011UB at 5 V is ≈ 1.3× slower: soft transfer, ≈ 1–3 kΩ output resistance, and a threshold not at VDD/2.
- **Do not compute the clock from the textbook 2.2RC; calibrate to the measured rates**, or simulate with a real 4011UB model.
- Crash/ride: the period is nearly linear in TUNE rotation, about **T_s ≈ 35.6 µs − 17.1 µs·x** (x = 0…1, AudioRealism crash), with a dead zone over the first ≈ 10 %.
  - The TUNE max end implies k ≈ 2.9 with R = 6.8 k.
  - The span (1.92–2.0×) is narrower than the nominal (6.8 k + 10 k)/6.8 k = 2.47×. The explanation is probably pot tolerance plus the gate output resistance and the Rs dependence of k. Both reference units appear to be late enough to have Rs = 10 k.
- Musically: the hats run at a fixed clock, which corresponds to the crash/ride TUNE at about 26 %. TUNE centre plays the cymbal data ≈ 16 % (2.6 semitones) faster than the hat rate.

**HH tune mods.**
- Colin Fraser: replace **R492** with 6k9 + a 22 k linear pot [B-17].
- The 9090 uses 6.8 k + 10 k(B) like the cymbals [B-6].
- The Behringer RD-9 has an "HH tune" in enhanced mode [B-19].

### B.5 Accent and levels

- **Accent path.** The CPU writes a 6-bit value into the accent latches IC2–IC9 (4174 + RM0621). MAME's `dac1..8_w` mask the data with 0x7E, i.e. bits 1–6. The value appears as V_acc (0–5 V, Thevenin 2.54 k) at the "ACC" input of each voice.
- In the cymbal voices V_acc enters as a base-voltage offset of the 2SA798 pair, so the gain in dB is linear in V_acc. My calc: 6.4 dB/V (HH), 9.0 dB/V (crash/ride). Which DAC feeds which voice, and how TOTAL ACCENT maps to V_acc, are covered in the firmware/accent sections.
- **Measured AudioRealism `accentNNN` series** (the pack does not define NNN), level relative to accent100:

| accent | 0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 | 100 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| CH | −7.0 | −7.0 | −6.1 | −5.6 | −4.6 | −3.6 | −2.8 | −1.9 | −0.9 | 0.0 | 0.0 |
| OH | −7.3 | −7.2 | −6.4 | −5.9 | −4.9 | −3.9 | −2.4 | −2.0 | −1.0 | −0.1 | 0.0 |
| crash | −8.2 | −7.7 | −7.1 | −6.0 | −5.0 | −4.0 | −2.9 | −1.8 | −1.1 | −0.4 | 0.0 |
| ride | −7.5 | −8.2 | −6.6 | −6.5 | −4.9 | −4.0 | −2.9 | −2.3 | −1.2 | +0.2 | 0.0 |

- The series is ≈ linear in dB between 10 and 90 and flat at both ends. A +7…8 dB range implies a ≈ 1 V swing of V_acc.
- Service Notes page 9 shows the CH "with accent" and "w/o accent" waveforms at 500 mV/div. The scale is unreadable, so no number is taken from it.

### B.6 Prior extractions and implementations

- **Colin Fraser (2002–2004)** [B-3][B-4][B-17].
  - In-circuit extraction from a borrowed 909: 6-bit data shifted into 8 bits, WAVs at an *"arbitrary 32 kHz … pretty close to the rate used for the hats"*.
  - The source of every public copy, including MAME.
  - Notes the inverted HH CE polarity, the pin 20/26 difference and the scrambling.
  - Hi-hat tune mod (R492) and bass-drum mods (useful for the BD section).
- **Trevor Page, 9090 (1999–2006)** [B-6].
  - Full voice clone with 27C256 EPROMs in nominal order (4040 Q1..Q12 → A0..A11, 4520 → A12..A14; D7..D2 → 40174 → 5k1/10k/20k/40k2/80k6/160k 1 % DAC).
  - Otherwise the same values: 220 k, 1 µF, 1 M(A)/100 k(B), the same 4-pole filter, C152 0.01 µF (late HH coupling).
  - Adds an HH tune. Schematics and hex mirrored at nuxx.net.
- **e-licktronic Nava v1.x** [B-21]. A one-to-one copy of the voicing board, keeping Roland's designators, with 27C256 EPROMs carrying the public data in nominal address order.
  - Every cymbal and hat value matches the Service Notes. It follows the late revision (C134 0.01 µF; R440/R442 10 k).
  - It is the most convenient machine-readable netlist of these circuits.
- **Hobby AVR hat players** [B-22][B-23]. Plain sample playback at 32 kHz with software envelopes, LSBs masked. They are not circuit models.
- **MAME** `roland_tr909.cpp` [B-5]. Skeleton driver (no sound). It maps the three ROM regions and the 8 accent DACs and 2 trigger latches at 0x4001–0x400A. It does not emulate the playback.
- **Gearspace "analog 909 hi hats" (2009)** [B-7].
  - "living sounds" recorded the hats at 192 kHz (scherer.de/Download/909Hats.rar, a possible further reference). They claimed *"the sampling frequency … around 60khz … The mirror image seems to start at around 31 khz"*: the image structure was misread as Nyquist; the real clock is 31.6 kHz.
  - Also noted: *"every hit sounds slightly different, maybe the clock isn't too stable and the analog VCAs play their part"*.
  - network-909 found the Jomox XBase 09 hats indistinguishable from the 909 ("same structure, heavily compressed samples + analog VCA").
- **Mod Wiggler threads** [B-8][B-9][B-10]:
  - 2012 request for the samples (pointed to Colin and the 9090 Yahoo group);
  - 2020–24 "909 EPROM" (9090 files; *"TR-909 and RE-909 are using Hitachi Mask rom with different address bus pinout"*);
  - "Chips for 909 clones": CR-9 / RD-9 / HH-9 chips replacing ROM, counter and DAC, with a raw 6-bit stream *"at 32KHz sample frequency"* (search snippet only).
- **Gordon Reid, SOS Synth Secrets "Practical Cymbal Synthesis" (Jul 2002)** [B-11]. Block diagram (`docs/papers/sos/sos_cymbal_fig02_tr909_cymbal_generator.gif`): ~30 kHz oscillator → address counter → 6-bit table → DAC → VCA → LPF → amp, with an address DAC → "anti-log voltage shaper" (plus accent) → VCA.
- **Tiptop Audio HATS909 / CYMBL909** [B-18]. *"low-fi 6-bit sound samples processed through analog elements for envelope and filtering"*, *"true to the original circuit"*, with tune CV and a 909/RAW switch.
- **Behringer RD-9** [B-19]. An STM32F746 plays the hat and cymbal samples; the rest of the voices are analog. HH tune in enhanced mode. No published details of its rates or envelopes.
- **Roland ACB (TR-8/8S, TR-09) and plugins**: see the emulations section. Nothing found about their hat internals.

### B.7 Numbers table

| quantity | value | source |
|---|---|---|
| HH sample clock | **31.57 kHz** (two units, ±0.03 %) | measured |
| HH oscillator | ≈ 63.1 kHz ("about 60 kHz") | measured / [B-1] |
| HH timing parts | R492 12 k, R493 33 k, C160 470 pF PS, 4011UB, ÷2 by 4013 | [B-1] |
| crash/ride clock, TUNE min/centre/max | 28.0 / 36.7 / 53.8–56.1 kHz | measured |
| crash/ride timing parts | 6.8 k + 10 k(B), 470 pF, Rs 33 k → 10 k (SN 392600) | [B-1] |
| OH / CH / cymbal sample counts | 24 576 / 8 192 / 32 768 | [B-1] + ROM |
| OH / CH lengths at 31.57 kHz | 778 / 259 ms | calc |
| CH leading silence | 65 samples = 2.06 ms | ROM |
| HH envelope hold | 2.0 ms (TRIG width) | measured / [B-1] |
| τ_OH | 69–183 ms (calc, C135 1 µF); 77–202 ms (Rob Roy); 182 ms at max (AudioRealism) | calc / measured |
| τ_CH | 8.7–69 ms (calc); 10.4–50.6 ms (Rob Roy); 7.5–41 ms (AudioRealism) | calc / measured |
| cymbal VCA law | g = ((63 − ⌊n/512⌋)/63)^1.10 | measured |
| envelope input currents | HH 22.7 µA·e^{−t/τ}; crash 18.3 µA·(63 − c)/63; ride 10.6 µA·(63 − c)/63 | calc |
| accent sensitivity | 6.4 dB/V (HH), 9.0 dB/V (cym) | calc |
| accent range (AudioRealism 0→100) | +7.0 (CH), +7.3 (OH), +8.2 (crash), +7.6 (ride) dB | measured |
| DAC | RM0621 binary-weighted, R = 5 k, Thevenin 2.54 k, 6 bits | [B-1] |
| AC coupling | 10 µF into 22 Ω (6.2 Hz) | calc |
| reconstruction LPF | 2 × Sallen-Key: 25.9 kHz Q 0.548 + 27.7 kHz Q 1.316 (≈ 4th-order Butterworth ≈ 26.8 kHz) | calc |
| HH level-stage HPF | 0.72 Hz (C134 10 µF) → 723 Hz (0.01 µF, SN ≥ 415300) | [B-1] + calc |
| multi-out RC | 1 k / 0.01 µF = 15.9 kHz (SN ≥ 415300) | [B-1] |
| level stage | 22 k in, 50 k(B) feedback → 0…2.27 | [B-1] |
| HN61256P timing | t_ACC 3.5 µs max, cycle 4 µs min | [B-2] |

### B.8 Disagreements

1. **"~60 kHz" vs ≈ 30 kHz.**
   - The Service Notes say the oscillator is ~60 kHz and the sample clock half that. That is right: the measured oscillator is 63.1 kHz and the clock 31.57 kHz.
   - Reid's figure labels the *oscillator* "~30 kHz", which is wrong by ×2 as an oscillator frequency but right as the sample rate.
   - The Gearspace "60 kHz sampling" claim is wrong.
   - An unattributed web claim of "6 bit / 18 kHz" is wrong.
   - Colin's 32 kHz is 1.4 % high for the hats.
2. **Exponential vs linear cymbal envelope.** The Service Notes ("anti-log tapered"), Reid ("anti-log voltage shaper") and Colin ("exponential decay") all imply an exponential. Measurement and the circuit (op-amp + at +5 V, address DAC into the collector-current input) give a ≈ linear-in-amplitude ramp, g ∝ (63 − code)^1.1. The anti-log acts on the accent voltage.
3. **"Compressed before digitizing" vs "no compression".** The Service Notes say the hat sounds "have been compressed before being digitalized". Hoshiai says no compression was applied, *"but the amplitude was processed on a computer after digitization"*. Both describe the flattened stored envelope that the VCA restores.
4. **HN61256 pins 20/26.** Colin says pin 20 is a chip select and pin 26 an address. The Hitachi datasheet says pin 20 = A11 and 26 = CS. Colin is evidently describing the swap needed relative to a 27256.
5. **Code-derived τ_OH vs the pot value.**
   - The naive τ_OH,max = (100 k + 1 M)·1 µF = 1.1 s.
   - Including R453 gives 183 ms, which matches both units.
   - Any model that leaves out the 220 k load will make the OH decay far too long.
6. **Is the dump raw?** MAME lists the files as HN61256P dumps, but they are Colin Fraser's 2002 in-circuit capture: playback order, and descrambled under my reading of the schematic. No raw chip dump is public.

### B.9 Recommendations for our model

1. **Sample source.** Use the ROM images as time-ordered 6-bit codes (`byte >> 2`), with the HH split at 0x6000 and the address logic as above.
   - Model the counter literally: stop at 0x6000 (OH), 0x2000 (CH, reading 0x6000+) or 0x8000 (cymbals).
   - A trigger resets the counter. A CH trigger during an OH switches the region at once.
2. **Clocks.** Use measured rates, not 2.2RC.
   - HH: 31 570 Hz (optionally ±0.1 % unit trim and a little slow drift).
   - Crash/ride: f_s(x) = 1/(35.6 µs − 17.1 µs·x) with a 10 % dead zone at TUNE min, or a lookup through the measured points.
   - The oscillator is gated, so each hit starts with the same clock phase relative to the trigger (after the ≈ 10 µs reset). Any per-hit variation is analog (VCA, noise), not random clock phase.
3. **DAC and images.**
   - Generate the zero-order-hold staircase *band-limited* at the plugin's internal rate, e.g. 4× oversampling with BLEP/polyBLEP-corrected steps at the exact fractional sample instants.
   - Then apply the analog chain: 6 Hz AC coupling, VCA, the two Sallen-Key sections (25.9 kHz Q 0.548, 27.7 kHz Q 1.316), the level-stage high-pass (0.72 Hz or 723 Hz; offer an "early/late HH" switch) and the 15.9 kHz jack RC. Then decimate.
   - Reproducing the 15.8–27 kHz image band correctly matters for the sound. At 44.1/48 kHz host rates, the images that land between 15.8 kHz and Nyquist must survive.
4. **HH VCA envelope.**
   - Model C135 and its resistor network as a one-pole RC: target +5 V, R = (R451 + VR21·CH) ∥ (R452 + VR23·OH) ∥ 220 k with Q73 switched by CLOSED.
   - Discharge it to 0 for the 2 ms TRIG pulse.
   - I_env = (5 − V_C)/220 k; gain ∝ I_env·exp(ΔV_B(accent)/V_T).
   - Calibrate C135 to ≈ 1.0–1.1 µF so the decays match the measurements above. Use the A taper for VR23 and B for VR21.
5. **Cymbal VCA.** gain = ((63 − (n >> 9))/63)^1.1 × accent factor. This can be generated from the counter, or as I_env = (5 − 5·code/63)/272.5 k (crash) or /472.5 k (ride) followed by a mildly nonlinear VCA (exponent 1.1).
6. **Accent.** Exponential in V_acc: 6.4 dB/V (HH) and 9.0 dB/V (cymbals) for 0–5 V. Calibrate the overall range against the AudioRealism series (+7…8 dB) once the firmware's accent-DAC values are known.
7. **Validation.** Re-run `reference/rom_decoded/scripts/ratefit.py`, `track.py` and `hhenv.py` on plugin renders and check:
   - rate (NCC peak);
   - τ;
   - the code-law against the AudioRealism and Rob Roy measurements;
   - the transfer function (`tf.py`).

### B.10 Open questions

- **Data-bit weights.** They depend on reading the RM0621 resistor order from the drawing. The public images are already decoded, so this matters only for raw-chip work.
- **Q71/Q69/Q85 VCA.** The exact transfer and distortion of the selected transistors (base-current to conductance) is unknown. The 1.1 exponent is measured, the mechanism is not. Simulate it in SPICE, including signal-level dependence; at ±20 mV across 22 Ω it should be nearly linear.
- **Accent mapping.** How TOTAL ACCENT and per-step accent map onto V_acc, and which accent DAC serves which voice. **Answered by the firmware** (section 5, C.4): 4007 drives the HH (CH and OH share it) and 4008 is shared by crash and ride. Codes at TA 0 → 31 are CH 25/40 → 40/55 (normal/accent) and OH/crash/ride 32 → 47. What remains is to check the resulting dB levels against the AudioRealism series.
- **Same-step CH + OH.** What happens when CH and OH fall on the same step, and the exact TRIG/CLOSED timing. The firmware holds TRIG2 b3 as a level (CH sets it, OH clears it; section 5, C.4). Which one wins on a shared step still has to be checked in the running emulator.
- **Early units.** The TUNE range of units with Rs = 33 k (before SN 392600) is unmeasured, and so is unit-to-unit spread of the HH clock beyond the two units here.
- **Hit-to-hit variation.** Measure on multi-hit recordings, e.g. the AudioRealism shuffle files.
- **Other references.** The 192 kHz "909Hats.rar" recordings (Gearspace 2009) may still be online and would be a third reference unit.

### Sources (B)

| # | Source | URL | Local copy |
|---|---|---|---|
| B-1 | Roland TR-909 Service Notes, 1st ed., 15 Jun 1984 (p. 5 circuit description, p. 7 change information, p. 9 waveforms, p. 11 voicing-board schematic) | https://archive.org/details/synth_Roland_TR-909_service_notes | `docs/papers/roland/TR-909_service_notes_A.pdf`, `…_text.pdf`, `.txt`, `service_notes_pages/p01–p16.png`; crops `docs/papers/samples/service_notes_crops/` |
| B-2 | Hitachi IC Memories Data Book (1985), HN61256P/FP datasheet pp. 226–227 | https://bitsavers.trailing-edge.com/components/hitachi/_dataBooks/1985_M11_Hitachi_IC_Memories_Data_Book.pdf | `docs/papers/samples/hitachi_HN61256P_datasheet_1985_IC_Memories_pp226-227.pdf`, `hitachi_HN61256P_pinout.png` |
| B-3 | Colin Fraser, "Cloning the TR-909 Cymbals" (2003) | http://www.colinfraser.com/tr909/909cyms.htm | `docs/papers/samples/colinfraser_909cyms.{html,txt}` |
| B-4 | Colin Fraser, "808/909 Clone — Nine-Oh-Ninestein" (2002) + r909hh/cr/rd.wav | http://www.colinfraser.com/tr909/my909.htm | `docs/papers/samples/colinfraser_my909.{html,txt}`, `colinfraser_wavs/` |
| B-5 | MAME `src/mame/roland/roland_tr909.cpp`; ROM set `tr909.zip` | https://github.com/mamedev/mame | `reference/mame/…`, `rom/`, `tools/fetch_roms.sh` |
| B-6 | Trevor Page, 9090 Analogue Drum Synth: schematics 11 Sep 2006, user guide 16 Nov 2002, ROM hex (nuxx.net mirror) | https://nuxx.net/wiki_archive/A/9090 , http://nuxx.net/files/9090/ | `docs/papers/samples/trevorpage_9090/` |
| B-7 | Gearspace thread 438818 "analog 909 hi hats" (Nov 2009) | https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/438818-analog-909-hi-hats.html (read via web.archive.org) | `docs/papers/samples/forum_gearspace_438818_analog_909_hi_hats_2009.txt` |
| B-8 | Mod Wiggler t=71285 "Anyone have original 909 6-bit rom samples?" (2012) | https://modwiggler.com/forum/viewtopic.php?t=71285 (via web.archive.org) | `docs/papers/samples/forum_modwiggler_t71285_909_6bit_rom_samples_2012.txt` |
| B-9 | Mod Wiggler t=229113 "909 EPROM" (2020–2024) | https://www.modwiggler.com/forum/viewtopic.php?t=229113 (via web.archive.org) | `docs/papers/samples/forum_modwiggler_t229113_909_eprom_2020-2024.txt` |
| B-10 | Mod Wiggler t=158736 "Chips for 909 clones" (search snippet only; 403, not archived) | https://www.modwiggler.com/forum/viewtopic.php?t=158736 | — |
| B-11 | G. Reid, "Synth Secrets: Practical Cymbal Synthesis", Sound On Sound, Jul 2002 | https://www.soundonsound.com/techniques/practical-cymbal-synthesis | `docs/papers/sos/sos_cymbal_practical_cymbal_synthesis_2002-07.{html,txt}`, `sos_cymbal_fig02_tr909_cymbal_generator.gif`, `sos_cymbal_fig04_tr808_cymbal_block.gif` |
| B-12 | A. Douglas, "Atsushi Hoshiai: the man behind the TR-909", Attack Magazine, 20 Aug 2024 | https://www.attackmagazine.com/features/interview/atsushi-hoshiai-the-man-behind-the-tr-909/ | `docs/papers/samples/interview_attackmag_2024_hoshiai_tr909.txt` |
| B-13 | K. Tsuboi, "Roland Engineering: Atsushi Hoshiai and the TR-909", Roland Articles, 4 Sep 2020 | https://articles.roland.com/atsushi-hoshiai-tr-909/ | `docs/papers/samples/interview_roland_articles_2020_hoshiai_tr909.txt` |
| B-14 | Wikipedia, "Roland TR-909" (sampling paragraph) | https://en.wikipedia.org/wiki/Roland_TR-909 | `docs/papers/samples/wikipedia_tr909_sampling_paragraph.txt` |
| B-15 | AudioRealism TR-909 Ultimate Sample Pack (96 kHz/24-bit; tune, decay and accent sweeps) | https://www.audiorealism.se/TR-909-SamplePack.html | `reference/real909/packs/audiorealism_tr909_ultimate/` |
| B-16 | Rob Roy 1995 TR-909 samples (zoeblade archive; 44.1 kHz/16-bit, decay and tune dials 0–10) | see `reference/real909/index.md` | `reference/real909/packs/zoeblade_robroy_1995/` |
| B-17 | Colin Fraser, "909 Sound Mods" (BD mods; HH tune via R492) (2004) | http://colinfraser.com/tr909/909mods/909mods.htm | `docs/papers/samples/colinfraser_909mods.{html,txt}` |
| B-18 | Tiptop Audio 909 series (HATS909, CYMBL909) | https://tiptopaudio.com/909-2/ | `docs/papers/samples/product_tiptop_909_series_HATS909_CYMBL909.txt` |
| B-19 | Behringer RD-9: STM32F746 sample playback for hats/cymbals; HH tune in enhanced mode (search-result summaries of Mod Wiggler t=206572 p. 4 and retailer pages) | https://www.modwiggler.com/forum/viewtopic.php?t=206572&start=100 , https://www.sweetwater.com/store/detail/RD9--behringer-rd-9-analog-drum-machine | — |
| B-20 | This work: decoded WAVs, figures, measurements and scripts | — | `reference/rom_decoded/` (`measurements.json`, `files.json`, `fig_*.png`, `scripts/`) |
| B-21 | e-licktronic Nava v1.02 Eagle files (909 clone with Roland designators). Parts list confirms R492 12 k, R493 33 k, C160 470 p, R442/R440 10 k, R443/R441 6.8 k, VR25/27 10 kB, C130/131 470 p, R432 270 k, R439 470 k, R453 220 k, C135 1 µ/50, R451 10 k, VR21 100 kB, R452 100 k, VR23 1 MA, C134 0.01 (late revision), the filter parts and the 2SA798-pair resistors. Netlist shows 27C256 nominal wiring with D7..D2 used. | https://github.com/e-licktronic (collected by fork D) | `docs/papers/emulations/code/e-licktronic_Nava-v1.0/Eagle/Nava_v1.02_{parts.csv,netlist.txt}` |
| B-22 | naokiiwakami tr909-clone-firmware: AVR hat player, CH start 0x6040, masks "dirty" LSBs | https://github.com/naokiiwakami (collected by fork D) | `docs/papers/emulations/code/naokiiwakami_tr909-clone-firmware/` |
| B-23 | TylerK07 AVR909-HiHat: AVR hat player, 32 kHz, packed 6-bit data | https://github.com/TylerK07/AVR909-HiHat | `docs/papers/emulations/code/TylerK07_AVR909-HiHat/` |

---

## 5. CPU board, firmware, MIDI and sequencer timing

This section covers the CPU (switch) board and the v1/v4 firmware: how the CPU drives the voice board,
the MIDI/sync/tape interfaces, and the sequencer timing. It draws on the Roland documents, MAME, NEC
documentation, a disassembly of both ROMs made for this survey, and forum and modder reports.

The disassembly was made with a small Python port of MAME's µPD7810 opcode tables
(`docs/papers/firmware/disasm/d7810.py`). The listings are `tr909_v1_linear.dis.txt` and
`tr909_v4_linear.dis.txt`, and the addresses and variables are summarised in
`tr909_v4_annotations.txt`. These are my readings of the code, and they should be checked by running the
firmware in the emulator. Statements that rest only on the disassembly are marked **(disasm)**.

### C.1 CPU and memory map

- **CPU**: IC604 on the switch board, a µPD7811G-033-036 according to the service notes [C-1, p.5]. MAME's
  comment calls it a "D7811G 037". This is the NMOS µPD7810-family part: 16-bit ALU, 256 B on-chip RAM,
  8-channel 8-bit ADC, 2 × 8-bit timers, a 16-bit timer/event counter and a USART [C-6, C-7].
  - The crystal is 12.00 MHz (part 12389717) [C-1 parts list].
  - One state is 3 clocks (250 ns). Instructions take 4–19 states, which is about 1 µs per typical instruction.
- **Mask ROM unused.** The µPD7811 has a 4 KB mask ROM. MAME notes that "mode pins are both pulled to 5 V".
  The NEC user's manual (Table 2-3) says the 7811 normally runs with MODE1 = 1 and MODE0 = 0. With
  MODE0 = MODE1 = 1 it runs as a ROM-less 7810 with 64 KB external memory [C-8 p.2-2].
  - The program runs from the external 8 KB EPROM at 0000–1FFF. MAME models the chip as a `UPD7810` [C-4].
  - The internal mask ROM is irrelevant to emulation. Our core should implement the ROM-less 7810 with
    external fetches from 0000.
- **Program ROM IC609** [C-1 p.7]:
  - 2764 EPROM (250 ns, part 15179645), or a 2364 mask ROM (part 15179646). The early "Revision 0" and
    "Revision 1" chips "contain the same program".
  - The firmware also writes the memory-mapping register (`MM = 08h`) at reset **(disasm)**.
- **RAM**: TC5565PL-15, 8 KB CMOS SRAM (IC608), backed by 2 × AA cells [C-1, C-2 p.4].
- **Memory map.** MAME [C-4] plus the disassembly:

| Address | Function |
|---|---|
| 0000–1FFF | 8 KB firmware EPROM |
| 2000–2FFF | RAM, **Bank I** (4 KB) |
| 3000–3FFF | RAM, **Bank II** (4 KB) |
| 4000 (mirrored to 5FFF) | read: key-matrix row (PA0–4 select the column, active low) |
| 4001–4008 (mirror mask 1FF0) | write: 8 **accent DAC latches** (IC2–IC9 on the voicing board) |
| 4009 | write: TRIG latch 1 (IC1) |
| 400A | write: TRIG latch 2 (IC10) |
| 6000–7FFF | memory cartridge M-64C (8 KB): 6000–6FFF = cartridge Bank I, 7000–7FFF = Bank II |
| FF00–FFFF | on-chip RAM, working variables (`V = FFh`); stack at FFFF |

- **Cartridge format = RAM image (disasm).** The firmware keeps the current bank's page in a variable:
  20h/30h for RAM Bank I/II and 60h/70h for cartridge Bank I/II.
  - That is how the 909 "plays Rhythm Tracks from the cartridge without loading" [C-2 p.33].
  - An M-64C therefore holds exactly the two 4 KB bank images, in the same layout as internal RAM. The
    TR-909 uses the **M-64C**. The owner's manual spec page misprints it as "M-46C". The 2 KB M-16C is a
    different product.
  - Cartridge presence is read on **AN1** (`CR1 ≥ 40h` means present). The service notes list AN1 as
    "unused" [C-1 p.5], but both MAME (`cart_sense_r`) and the firmware use it.
- **Diagnostic-cartridge hook (disasm).** At reset, if a cartridge is sensed, 7FFE/7FFF read `96 69`, and
  6000 is not writeable (that is, a ROM), the firmware executes `CALL 6000`. This is presumably a factory
  test-cartridge hook. It is the same in v1 and v4.
- **Bank layout (partly disasm, partly hypothesis):**
  - The pattern headers sit at bank + F80h: 16 records of 8 bytes. Each record holds three 2-byte
    headers, one per pattern group, plus 2 further bytes.
    - Header byte 0: b7 = chain flag(?), b5–4 = scale, b3–0 = last step − 1.
    - Header byte 1: b6–4 = flam setting 0–7, b2–0 = shuffle 0–6.
    - The v1 factory default is `0F 40`: 16 steps, 16th scale, no shuffle, flam 4.
  - The constant 37Fh = 895 limits measure numbers. That matches the documented "896 measures per bank"
    [C-2] and suggests the measure list is bank + C00h…F7Fh (896 bytes). If so, the 48 patterns occupy
    bank + 000h…BFFh, which is 64 bytes each. This is unverified.
  - The SysEx dump (C.5) transfers a bank as 16 × 256 bytes = 4096 bytes, which is consistent with 4 KB banks.

### C.2 I/O ports (service notes p.5 [C-1], confirmed in the disassembly)

| Pin(s) | Use |
|---|---|
| PA0–4 | key-matrix column scan; MAME reads 5 rows of 8 keys at 4000 |
| PA5–7 | LED drive |
| PB0–4 | LED row scan |
| PB5 | latch for the 7-segment driver (4511) |
| PB6–7 | 2-bit tape-data output into the R318–R322 resistor DAC |
| PC0 | TxD (MIDI OUT) |
| PC1 | RxD (MIDI IN) |
| PC2 | foot switch (DP-2 start/stop) |
| PC3 | tape data in (TI/INT2) |
| PC4 | tape-sync out (timer output TO) |
| PC5 | DIN Start/Stop |
| PC6 | DIN Continue |
| PC7 | unused. The clock routine XORs PC7 every tick (`XRI PC,80h`), but `MC` configures PC7 as an input, so the write has no external effect |
| PD | multiplexed AD0–7 |
| PF | A8–15 |
| NMI | unused |
| INT1 | DIN sync clock (rising edge) |
| AN0 | TEMPO pot |
| AN1 | cartridge sense |
| AN3 | TOTAL ACCENT pot |
| AN2, AN4–7 | unused |

Reset-time register setup **(disasm)**:

- **Port C**: `MC = EEh`, `MCC = 1Bh`. PC0/PC1/PC3/PC4 are in control mode (TxD, RxD, TI/INT2, TO).
- **Serial**: `SML = 4Eh`, `SMH = 0Eh`, which gives asynchronous 8N1 with a ×16 internal clock
  (12 MHz/24 = 500 kHz), so **31 250 baud**.
- **Timers**: `TM0 = D0h`, `TM1 = 02h`, `TMM = 61h`. Timer 0 counts at 12 MHz/12 = 1 µs and is cascaded
  into timer 1, which drives the TO flip-flop.
- **Event counter**: `ETMM = 0Ch`, so ECNT counts at 1 µs and is cleared on an ETM1 match. `EOM = A0h`.
- **ADC**: `ANM = 00h`, so it continuously scans AN0–AN3.
- **Interrupt masks**: `MKL = BFh`, `MKH = 04h`, so only INTE1, INTAD and INTSR are enabled.
  - INT1 (DIN) is unmasked only in DIN mode (`ANI MKL,F7h`).
  - INT2 (tape) is unmasked only when needed.

### C.3 ADC inputs (TEMPO, TOTAL ACCENT)

- **TEMPO (AN0).** Read in routine 13FC **(disasm)**.
  - The firmware takes `t = CR0` (0–254; a reading of FFh is ignored) with ±1-count hysteresis.
  - It then computes **B = 32 + ⌊225·t/256⌋**, which runs from 32 to 255.
  - The internal clock period is **P = ⌊1 099 138 / B⌋ µs**, computed by a two-stage DIV on 10C5h/82h and
    loaded into ETM1. **The internal sequencer clock is 48 ppqn.**
  - So **BPM = 60·10⁶/(48·P) ≈ 1.1373·B**. That covers **36.4–290.0 BPM** in 224 steps of about 1.14 BPM,
    which matches the spec of ♩ = 37–290 [C-1, C-2].
  - The tempo pot is a 50 kB linear part (EWHLNAF20B54) [C-1].
  - The timer base is the 12 MHz crystal, so the TR-909 tempo is crystal-accurate. It is **not** an
    analogue oscillator, despite folklore [C-17].
- **TOTAL ACCENT (AN3).** Read live at every step: the firmware uses `CR3 >> 3`, a value from 0 to 31.
  The knob is therefore quantised to 32 levels and acts digitally on the accent DAC codes (C.4).
- **ADC timing matters.** The NMOS 7810/11 converts in **48 µs** (192 states) [C-7 p.4-82].
  - In 4-channel scan mode the ADC raises INTAD after every 4 conversions, that is every **192 µs**.
  - The firmware uses INTAD as its system tick (C.4). MAME models it exactly this way: 192 states, INTAD
    when the result pointer wraps [C-5].

### C.4 Voice interface: accent DACs, trigger latches and the scan tick

From the service notes [C-1 p.5]:

- "Accent data on the CPU bus is latched into one of ACCENT latches (IC2–IC9) selected by Address Decoder
  (IC612, 613)… converted to analog… at the output of resistor array RM0621. The voltage is clamped to the
  level until it is replaced by the next incoming data."
- "TRIG is latched into IC1 or IC10, and appears as 5 V positive going pulse… for 2 ms."

**DAC and trigger mapping (disasm).** The MIDI note table at 173B and the sequencer's output routine give:

- **DACs.** The latch byte is the 6-bit code shifted left by 1 (bits D1–D6). MAME logs `data & 7Eh`.
  - 4001 BD
  - 4002 SD
  - 4003 LT
  - 4004 MT
  - 4005 HT
  - **4006 RS + HC, shared**
  - **4007 HH, shared by CH and OH**
  - **4008 crash + ride, shared**
- **TRIG1 (4009).** b1 BD, b2 SD, b3 LT, b4 MT, b5 HT. MAME masks `3Eh`.
- **TRIG2 (400A).** MAME masks `7Eh`.
  - b1 RS.
  - b2 HH trigger.
  - **b3 HH CLOSED(1)/OPEN(0) level.** This is held between ticks. CH sets it and OH clears it.
  - b4 crash, b5 ride, b6 HC.
- **Shared DACs.** If RS and HC, or crash and ride, are triggered in the same tick with different levels
  (possible over MIDI), the last-written level applies to both.
- **Power-up write.** At reset the firmware writes `4007 = 0`, `4008 = 0` and `400A = 3Ch`. That raises
  the HH (closed), crash and ride trigger lines once at power-up. The probable purpose is to run the sample
  address counters to their stop state; the reason is unconfirmed.

**The scan tick and trigger timing (disasm).** This is the most important timing fact for emulation.

- **Output only on scan ticks.** Voice triggers do not go out when the sequencer clock fires. They go out
  from the **INTAD service routine**, which runs on every 10th INTAD, a **nominal 1.92 ms** tick (probably ≈2.1 ms in practice; see below).
- **What the routine does** (vector 0020 → 012B):
  - scans the keys and multiplexes the LEDs and 7-segment display (5 phases);
  - computes the pending triggers and accent codes into an image at FF00–FF09;
  - block-copies that image to 4001–400A (`BLOCK` at 02B4);
  - clears the trigger bytes, except the HH closed bit.
- **Trigger pulse width.** Each trigger pulse is therefore exactly one tick, about 1.92–2.1 ms. That is the
  service manual's "2 ms" [C-1].
- **Two groups.** Instruments are serviced on **alternating ticks**:
  - Group A on one tick: BD, SD, LT, MT.
  - Group B on the next: HT, RS, HC, HH, crash, ride.
  - So each instrument can only start on a ~3.84–4.2 ms grid, and the two groups are offset by one tick.
- **Sequencer path.** The tempo timer interrupt (INTE1, vector 0018) only increments a pending-tick counter.
  - The **main loop** consumes the ticks, counts down the step length and runs the step routine (08C9).
  - The step routine reads the pattern and the live TOTAL ACCENT value, then writes the pending trigger
    bits (FF0C–FF0F) and the accent codes.
  - Step-to-trigger latency is therefore main-loop latency plus 0–~4 ms of group quantisation (one group period).
- **This is the 909's "sloppy timing".** Innerclock measured a maximum step-to-step jitter of
  **4.46 ms** (214 samples at 48 kHz) on the internal sequencer, **6.40 ms** on DIN sync and **7.79 ms** on
  MIDI clock [C-18]. Roland's own bulletin and Innerclock agree that v4 did not change it [C-3, C-18]. An
  older folk explanation is "the CPU polls the tempo oscillator every 2 ms" (Colin Fraser, via [C-17]).
  The disassembly shows the real mechanism: a 1.92 ms output tick, alternating groups, and foreground
  step processing.
- **MIDI path.** A received note-on writes the DAC image and trigger bit directly in the serial ISR. It
  goes out on the **next** tick of either group, so MIDI-played notes are quantised to the ~1.92–2.1 ms tick. On top of
  that comes the 0.96 ms transmission time of a 3-byte message.
- **The real tick is probably longer than 1.92 ms.** The INTAD routine runs with interrupts disabled
  until `ORI MKH,01h / EI` at 01A9; from there only INTAD is masked, so tempo and MIDI interrupts can nest
  inside it.
  - INTAD requests keep arriving every 192 µs, but only one can be pending. While the routine runs, every
    request after the first is lost.
  - Summing MAME's state counts (`upd7810_table.cpp`) along a typical path gives roughly **0.4–0.55 ms**
    for the routine: entry, key read, one LED phase, key processing, the ext-MIDI check, four accent
    calculations and a 10-byte `BLOCK`. That is longer than two INTAD periods, plus any nested ISRs.
  - So typically one request is absorbed per scan, and the scan period becomes **11 × 192 µs ≈ 2.11 ms**
    (group period ≈ 4.2 ms). It can be longer when MIDI or tempo interrupts nest.
  - This fits the "4 ms" granularity seen in real-909 flam measurements and in Roland's own TR-909
    software (C.7). **Treat 1.92 ms as a lower bound and ≈2.1 ms as the likely value.** The cycle-exact
    emulation will give the real figure automatically; it is a good acceptance test.
  - Against this, the Revolution 909 manual quotes a v5 (rewritten firmware) ext-note duration of
    "1–256, around 2 ms to 470 ms", which is about 1.84 ms per unit [C-15].

**Accent level arithmetic (disasm).**

- At each step the firmware stores `TA = CR3 >> 3` (0–31) if the step carries **Total Accent** (step
  flag bit 0), otherwise `TA = 0`.
- Each triggered instrument gets **code = table[(base + TA) >> 1] >> 1**:
  - **Table 1F80** (analog voices, RS and HC). 64 entries (code = byte/2; the ROM's bytes aren't
    reproduced here: `tools/compare/real909.py` reads them from the user's ROM). The curve is
    expanding: small steps at the bottom, larger at the top.
  - **Table 1FC0** (HH, crash, ride). Linear: byte = 2·index, except index 0 = 02.
- Bases:

| Instrument | Normal base | Accented base |
|---|---|---|
| BD, SD, LT, MT, HT | 32h | 50h |
| CH | 32h | 50h |
| RS, HC | 50h always (one key each, so no accent option) | – |
| OH, crash, ride | 40h always | – |

- Resulting 6-bit DAC codes for TA = 0 / 8 / 16 / 24 / 31:

| Voice / case | TA = 0 | TA = 8 | TA = 16 | TA = 24 | TA = 31 |
|---|---|---|---|---|---|
| Analog normal | 10 | 12 | 14 | 18 | 21 |
| Analog accent, RS, HC | 21 | 27 | 33 | 41 | 47 |
| CH normal | 25 | 29 | 33 | 37 | 40 |
| CH accent | 40 | 44 | 48 | 52 | 55 |
| OH, crash, ride | 32 | 36 | 40 | 44 | 47 |

  - So **"normal + full TOTAL ACCENT" equals "accent with TOTAL ACCENT at zero"** for the analog drums.
  - The internal sequencer never reaches code 63. Only MIDI velocity 126–127 does.
  - This agrees with the manual's warning that the per-voice accent may go unnoticed when TOTAL ACCENT is
    set high [C-2 p.25].
- **Firmware quirk.** Entry 43 of table 1F80 is `28h`, which is *lower* than entry 42 (`30h`). It is
  identical in v1 and v4.
  - An accented analog step with TA = 6–7 (TOTAL ACCENT around 20–25 %) is slightly *quieter* (code 20)
    than with TA = 0–5 (codes 21–24).
  - MIDI velocities 86–87 play quieter than 84–85.
  - This is worth confirming on real hardware.
- **MIDI velocity → code.** Analog voices use table 1F80 indexed by velocity/2 (for example v1→2, v64→14,
  v80→21, v100→37, v127→63). HH and cymbals use 1FC0, which is about velocity/2.
- **Guide/metronome.** It is a rim-shot trigger with fixed DAC6 codes `2Dh` (bar start) and `0Fh` (beats).

### C.5 MIDI implementation (service notes p.13–16 [C-1]; owner's manual p.38 and chart [C-2])

- **Hardware.** MIDI IN and 2 × MIDI OUT. Both outputs share one driver, so a known weakness is driving two
  long cables [C-16].
- **Channels.**
  - Receive: default **channel 10**, in **Mode 1 (OMNI ON/POLY)**. The display shows "0" for omni.
    - Selecting a channel with SHUFFLE/FLAM + key 1–16 switches to Mode 3 (OMNI OFF/POLY).
    - Mode messages 124–127 are recognised. Mono (126) with M > 1 accepts channels N…N+M−1.
  - Transmit: **channel 11**, set with LAST STEP + key. Setting it transmits OMNI OFF and POLY ON on the
    new channel.
  - None of this is saved: the 909 always powers up as Rx omni/channel 10, Tx channel 11, sync INT
    [C-1, C-20].
- **Notes received** (note-on only, as triggers; note-off and velocity 0 are ignored; velocity 1–127 sets
  the accent DAC). Keys not listed are ignored. "Two keys are for duplicating a sound with convenient key
  play" [C-1 p.14].

| Note | Instrument |
|---|---|
| 35, 36 | BD |
| 37 | RS |
| 38, 40 | SD |
| 39 | HC |
| 41, 43 | LT |
| 42, 44 | CH |
| 45, 47 | MT |
| 46 | OH |
| 48, 50 | HT |
| 49 | crash |
| 51 | ride |

- **Notes transmitted: External Instrument rows only.** Notes **36–51**: pattern keys 1–16 map
  chromatically from C (36) up to E♭ (51) [C-2 p.37, C-20].
  - Velocity is **64**, or **64 + TA** (up to 95) on accented external steps. The chart says "64–96
    (accent min–max)" [C-1 p.15]; the disassembly shows it as 40h + TA.
  - Note-off is sent as note-on with velocity 0.
  - **The internal drum voices are not sent over MIDI.** This is confirmed by users [C-23, C-20] and by the
    disassembly: the transmit routine at 0B36 only walks the Ext-instrument bitmaps.
  - Flam is not available for external instruments, and the gate time is fixed.
- **System messages.**
  - Transmitted: timing clock F8 at 24 ppqn, **continuously, even when stopped** (Revolution 909 lists
    stopping this as a v5 change [C-15]); Start, Continue and Stop.
  - Song Position Pointer: sent only in Track Play or stop, after a measure number is set.
  - Song Select 0–7 = Bank I tracks 1–4 then Bank II tracks 1–4. It is sent when a track or bank is
    selected and also applies to the cartridge.
  - Received: clock (only when TEMPO MODE = MIDI), Start, Continue, Stop, SPP and Song Select (Track Play
    and Stop only), and **System Reset (FF)**.
  - Not supported: Active Sensing, All Notes Off, program change, control change, pitch bend and
    aftertouch [C-1, C-2].
- **Soft thru.** "All valid MIDI IN messages are transferred to MIDI OUT except Timing Clock and System
  Exclusive" [C-1 p.15]. MIDI OUT works as a merge.
- **Tape freezes MIDI.** "While the Tape Interface is functioning (SAVE/LOAD/VERIFY), all MIDI routine is
  frozen."
- **System exclusive bank dump** [C-1 p.16]. Roland ID 41h, no device ID. Works only in TRACK PLAY or STOP.
  - **Save to host.**
    1. Host → 909: `F0 41 51 F7`.
    2. 909 → host: `F0 41 52 01 4n <512 nibble bytes> cs F7`. `4n` is block n = 0–15. The data bytes are
       `0000xxxx 0000yyyy`, giving the byte `yyyyxxxx` (256 bytes per block). `cs` is a 7-bit checksum
       over the 512 bytes; the algorithm is not documented. Then `F0 41 54 F7`, or `55` for "no data
       follow". Opcode `70` means abort.
    3. Host → 909: acknowledge `F0 41 53 F7`, or `71` for error. Repeat for 16 blocks, 4096 bytes in all.
  - **Load from host.** The host sends `F0 41 50 F7`. The 909 answers `F0 41 51 F7` (or `70`). Data and
    acknowledgements then follow as above, with the roles reversed.
- **Velocity → level** mapping is as in C.4.

### C.6 Sync: internal, MIDI, DIN and tape

- **TEMPO MODE** is set with SHIFT + TEMPO MODE and cycles INT → MIDI → DIN. It always powers up in INT.
  In MIDI or DIN mode "only the external unit can start or stop the units" [C-2 p.34].
  - INTERNAL: MIDI clock and DIN inputs are ignored.
  - MIDI: internal tempo and DIN are ignored.
  - DIN: the 909 syncs "to the positive going edge of clock pulses from DIN jack" [C-1 p.15].
- **DIN sync (input only).** The 5-pin DIN pinout is 1 Run/Stop, 2 GND, 3 Clock, 5 Continue [C-1, C-2].
  - Clock is 24 ppqn on INT1. Start/Stop is on PC5 and Continue on PC6.
  - Each DIN clock is also echoed as MIDI F8 **(disasm)**.
- **Clock doubling (disasm, routine 17F8).** External clocks (DIN pulse or MIDI F8, both 24 ppqn) are
  doubled to the internal 48 ppqn like this:
  1. ECNT (µs since the previous clock) is read.
  2. It is halved and loaded into ETM1.
  3. One tick is issued immediately.
  4. ETM1 then produces the in-between tick.

  So every odd 48-ppqn tick is *extrapolated from the previous clock interval*. That explains why slaved
  timing is worse than internal timing (6.4 and 7.8 ms against 4.5 ms [C-18]) and why shuffled
  (odd-tick) steps move when the master's tempo changes.
- **Firmware sync history.**
  - **v1 bug.** The 909 "will rarely miss-read a $F8 just after Start data" when the gap is about 1 ms, as
    from an MPU-401. Roland Service Information No. 100236, 6 Feb 1985, fixed it with PROM Ver 1.0 → Ver 4.0
    [C-3, C-13].
  - Users describe v1 as drifting or lagging when slaved over MIDI, and Innerclock says "Rev 1 units will
    not sync to MIDI clock" [C-18, C-21]. DIN sync on v1 is generally reported to be fine.
  - The June 1984 service notes describe an earlier "Ver. 2" that fixes "falls behind if STOP then CONTINUE
    is pressed" [C-1 p.7].
  - The Revolution 909 developers, who disassembled v1 and v4, report a separate **v4 bug**: the 909 "freezes
    when synchronized to DIN" [C-15].
- **Tape sync / FSK** [C-1 p.6] (disasm agrees).
  - Output is the timer output TO on PC4. TM0 = D0h/68h with TM1 = 2 gives a 416 µs or 208 µs half period,
    so **1201.9 Hz or 2403.8 Hz**.
  - Stopped: continuous 1200 Hz.
  - Playing: the frequency alternates 1200/2400 Hz **at every 48-ppqn tick**, which the service notes
    describe as "every half-period of T, T = ♩/24".
  - Input (PC3/INT2) is differentiated, smoothed by IC41b and squared by IC41a. The CPU measures every
    half-period. Each frequency change is one tick.
  - Hardware change at serial 381500: C87 deleted, R306 4.7 k → 47 k, R312 470 k → 4.7 M. At 415300, R392
    and R393 changed to 1 M and C600 was added, to stop the rim-shot trigger-out leaking into tape sync [C-1 p.7].
- **Tape save/load format** [C-1 p.6, C-2 p.19–22].
  - The CPU outputs 2-bit codes on PB6/PB7 into a resistor DAC (R318–R322). There are two alternative codes
    each for "0" and "1", so adjacent bits stay distinguishable.
  - The figure shows about 3.2 kbaud, with 500 µs and 125 µs segments.
  - Load decision: a half-period **under 300 µs is "0"; over 300 µs is "1"**.
  - A save takes about 6 s of pilot tone then about 13 s of data per bank. Banks I and II are saved
    separately. Files are numbered 0–999.
- **Trigger Out.** This is a rim-shot-programmed pulse of **+14 V for 20 ms** [C-1 spec, C-2 spec]. The
  manual text also says "18 ms" [C-2 p.38]. It is generated on the voicing board, not by the firmware
  directly; at 415300 R495 1 k was added.
- **Foot switch.** A DP-2 on PC2 toggles start/stop [C-2 p.38].

### C.7 Sequencer timing: resolution, steps, shuffle, flam, scale, last step

- **Resolution.** The internal clock is **48 ppqn** **(disasm)**:
  - MIDI F8 is sent on every second tick (24 ppqn).
  - Tape FSK toggles every tick.
  - A 16th step is 12 ticks.
- **Scale** (SCALE button) cycles 16th → 32nd → 8th triplet → 16th triplet [C-2 p.24]. Choosing a scale
  sets LAST STEP automatically to 16 (straight) or 12 (triplet). Step lengths in ticks **(disasm)**:
  - 16th: 12
  - 32nd: 6
  - 8th triplet: 16
  - 16th triplet: 8

  The same 6/3/8/4 ticks-per-step table at 24 ppqn appears in the Song Position code.
- **Shuffle** (SHUFFLE/FLAM + keys 1–7; 16th and 32nd scales only) [C-2 p.26].
  - In 16th scale the pair of steps is split **12 + s / 12 − s** ticks with s = key − 1 = 0…6 **(disasm)**.
    The second (even) 16th is delayed by s/48 of a beat.
  - Swing ratios: **50.0, 54.2, 58.3, 62.5, 66.7, 70.8 and 75.0 %**.
  - This matches the forum statement that "the 909 delays each even 1/16 by 2/96 of a beat for setting 1 …
    12/96 for 6" [C-19].
  - In 32nd scale the first step of the pair is ⌊(12 + s)/2⌋ ticks. That is why the manual says
    "1 = 2, 3 = 4, 5 = 6": delays of 0, 0, 1, 1, 2, 2, 3 ticks, giving 50/58.3/66.7/75 %.
  - Triplet scales ignore shuffle.
  - "If the total number of steps is odd, the last step will not take on the bounce." The last step is
    forced to 12 (or 6) ticks [C-2] **(disasm)**.
- **Flam** (BD, SD and the three toms; INSTRUMENT SELECT + both keys of a voice; interval on SHUFFLE/FLAM +
  keys 9–16; "longer interval towards 16"; "the first sound is in time for a step"; **"the position of the
  Tempo knob does not affect the interval"** [C-2 p.26]).
  - Firmware mechanism **(disasm)**:
    - The pattern header stores flam f = key − 9 = 0…7 (routine 0C11).
    - Every step, the step routine loads the counter VV:5C with **f + 5** (0997).
    - The counter is decremented only on group-B scan ticks (0226).
    - The BD/SD/LT/MT second hits (pending in FF0C) are released on the first group-A tick where the
      counter is 0. The HT second hit is released on the group-B tick where it underflows.
    - Both hits go out on scan ticks, so the interval is a whole number N of group periods T_g. Depending
      on the phase of the step relative to the A/B alternation, **N = f + 4 or f + 5**.
    - Flam is encoded as both of a voice's two bits set. The **grace note goes out on the step at the
      *normal* level (base 32h), and the delayed main note at the *accent* level (base 50h)**, with
      TOTAL ACCENT added to both.
  - Predicted interval:
    - T_g = 3.84 ms (1.92 ms tick): (f+4…f+5)·3.84 ms = **15–19 ms (key 9) to 42–46 ms (key 16)**.
    - T_g ≈ 4.2 ms (2.11 ms tick, C.4): **17–21 ms to 46–51 ms**.
    - It is tempo-independent, as the manual says.
  - Published figures disagree:

| Source | Key 9 … key 16 | Notes |
|---|---|---|
| EchoBoy, scope on the TRIG line of a real 909 (e-licktronic forum, 22 Jul 2017) [C-26] | **10, 14, 18, … 38 ms** (10 + 4i); "min" 6…34 ms | The second trigger shows **two positions 4 ms apart** ("due to the jitter of the sequencer"), and the later one is more frequent. Not tempo-dependent. |
| e-licktronic Nava firmware (cites EchoBoy) and Roland Cloud TR-909 (FLAM TYPE 2–9) [fork D] | 20, 24, … 48 ms (= 4·(f+5)) | Matches the firmware's f+5 count with T_g = 4 ms. |
| This disassembly | (f+4 or f+5) × T_g, T_g ≈ 3.84–4.2 ms | |

  - EchoBoy's bimodal second trigger, 4 ms apart, confirms the mechanism: group-tick quantisation, two
    phase cases, and a ~4 ms group period, which supports the ≈2 ms tick.
  - His absolute values sit about 2½ group periods (~10 ms) *below* the firmware count. His half-integer
    multiples (6 and 10 ms = 1.5 and 2.5 × 4 ms) also suggest the two hits fell on *different* groups.
  - I could not reconcile this statically. Possible causes: I mis-read where the counter is loaded
    relative to the first hit, or EchoBoy's unit or measurement point differs.
  - **This is the first thing to measure in the running emulator.** Set flam keys 9 and 16 on SD and log
    the 4009 writes. If an audio check is wanted, the GWEM flam tutorial (`reference/real909/videos/gwem_tr909_flam_tutorial`,
    2:04–2:58, min and max flam on SD) is available, but its narration over the audio defeated a quick
    onset analysis.
- **Last step** 1–16 is set with LAST STEP + key. **Chain** links patterns for more than 16 steps. Changing
  last step while playing resets or restarts the pattern [C-15].
- **Accent.**
  - "Accent B" is per instrument: BD, SD, the three toms and CH. Pressing a step twice gives an accent.
  - "Accent A" is **TOTAL ACCENT**, per step and affecting all voices on that step. It is written with
    INSTRUMENT SELECT + TOTAL ACCENT key, and its depth comes from the TOTAL ACCENT knob, sampled when the
    step plays (C.4) [C-2 p.25].
  - In Tap Write the left key of a pair gives accent and the right key gives normal.
- **Memory.** Each bank holds 3 pattern groups × 16 patterns, plus 16 External Instrument rows per pattern
  (whose scale, last step and shuffle are shared with the pattern), and 4 tracks with 896 measures in all.
  There are two banks [C-2].
  - Track mode supports a tempo memory per track, recalled with TEMPO + TRACK.
  - The edit functions include insert, delete and copy. Single-measure copy "did not work" in the original
    firmware [C-15].
- **Factory patterns.** v1 holds factory patterns, loaded by powering up with TRACK 1 + PATTERN 1 held.
  - The routine at 1F2E clears Bank I and copies data from ROM at 1D8E into 2000/2100/2C00 and 2F80 **(disasm)**.
  - **v4 removed both the routine (now a bare `RET` at 1DF4) and the data** [C-13, C-15, disasm].

### C.8 Firmware versions and differences

- **Versions known.**
  - v1.0 is the common one. Its ROM may carry no version label ("Revision 0") or a "Ver. 1" label.
  - v2 is mentioned in the June 1984 service notes. v3 is believed to exist.
  - v4.0 was Roland's last version, a service part from Feb 1985 [C-1, C-3, C-16].
  - The modders' consensus is that v1–v3 are identical apart from the version ID. No clean v2/v3 image is
    public [C-16].
  - The aftermarket v5 (Revolution 909, 2024) keeps the "functionality and timing of the original
    sequencer" and still contains v1 and v4 [C-15].
- **Version display.** Power up with TRACK 1 held, and a main-key LED shows the version. The firmware
  writes the LED mask 01h in v1 and 08h (LED 4) in v4 **(disasm)**. The service notes say "Ver. 2" blinks key 2.
- **ROMs here.**
  - `rom/tr909_v1.ic609`, SHA1 bcd75490….
  - `rom/tr909_v4.ic609`, SHA1 b94374e2…, byte-identical to Robin Whittle's distributed `TR909-V4.EPR` [C-13].
  - The two differ in 7715 bytes, but most of that is relocation (the code shifted by a few bytes). An
    instruction-level diff (`disasm/`) puts the real changes in four places:
    1. the clock-tick and start/stop/continue logic (v4 12A8–13E9; extra handling of the sync state and
       the tape-FSK timer);
    2. the MIDI receive parser (v4 1638–16A4 and 15A7–15C5). In v4, a received F8 is queued through a
       second transmit buffer (`CALF 0D08`) rather than overwriting the single one;
    3. the external-clock routine (17D3–17F6);
    4. the deleted factory patterns (v1 1D8E–1F76, 0.5 KB, which v4 fills with FFh).
  - The interrupt vectors, I/O map, scan routine, note table, velocity tables, tempo formula and shuffle
    logic are the same in both.
- **Hardware revisions vs firmware versions.** "Rev 2/3/4" in user talk refers to *voicing-board*
  changes listed in the service-notes change table [C-1 p.7]. Regebro's summary is in
  `reference/real909/videos/regebro_rd9_vs_two_tr909_results`.
  - Serial 381500: BD C9 0.22 → 0.33 µF, a wider TUNE range ("rev 2"). The tape-sync filter was also changed.
  - Serial 393000: reset zener D701 6.8 → 5.6 V.
  - Serial 415300: HH C134 10 µF → 0.01 µF, so the hi-hat HPF moved from about 0.7 Hz to about 700 Hz;
    RS R417 12 k → 3.3 k; and the trigger-out isolation (R392, R393, R495, C600) ("rev 3").
  - Serial 426700: tom noise C54 0.0022 → 0.0047 µF and R194 47 k → 100 k, a stronger tom attack ("rev 4").
  - The ROM table in the service notes is separate: serials 360100–393899 got an unlabelled ROM
    ("Revision 0"), 403900 onward a "Ver. 1" EPROM or 2364 mask ROM with the same program.
  - The modders' belief is that the firmware version ID (1/2/3) only tags the production batch [C-16].
    The service notes, however, credit "Ver. 2" with a STOP/CONTINUE MIDI-sync fix. Without v2/v3 dumps
    this cannot be settled.
  - **For us, the hardware revision is a voice-model option (component values), independent of the
    firmware choice.**
- **Implication for us.** Ship v4 as the default because of its MIDI-start fix. Keep v1 selectable: it is
  needed for the factory-pattern reset and to reproduce v1 behaviour. Pattern RAM is format-compatible
  between them.

### C.9 MAME status

- `roland_tr909.cpp` is by AJR and still a **skeleton** (`MACHINE_NOT_WORKING | MACHINE_NO_SOUND`). Its
  last functional change predates 2022; the 2022–2024 commits are only refactors [C-4].
- It provides the address map and the 5×8 key matrix.
- It logs the DAC and TRIG writes, with the masks 7Eh and 3Eh/7Eh.
- It leaves these as TODO:
  - LED matrix, 4511 digits and the tape outputs;
  - TEMPO and TOTAL as analog inputs;
  - cartridge sense (returns 0);
  - the Port C handlers (foot switch, tape in, DIN start/continue);
  - INT1 (DIN clock);
  - the cartridge slot device (empty);
  - the software list.
- The upd7810 core [C-5] implements what the 909 needs:
  - timers 0 and 1 with the TO flip-flop;
  - the event counter (ETM0/ETM1/ECNT at 12 clocks);
  - the ADC at 192 states per conversion, in scan and select modes;
  - the internal-clock UART;
  - string-effect (L0/L1) and skip semantics;
  - the `TABLE`, `BLOCK`, `MUL` and `DIV` instructions used here.

  It is based on the NEC User's Manual of April 1987, which is now in `docs/papers/nec/` [C-8].

### C.9b Cross-checks against reference recordings (from the reference-audio survey)

The video survey's findings agree with the disassembly:

- **Shuffle.** Starsky Carr quotes the 909 shuffle steps as **0, 8.3, 16.6, 25, 33, 41.6 and 50 %**
  (`videos/starskycarr_tr909_vs_rd9_tr09`, 18:10–23:03). These are percentages *of a 16th note* of
  delay: s/12 with s = 0…6 ticks. That is exactly the 12+s / 12−s split found in the firmware, which is
  a swing ratio of 50–75 %. He also shows MIDI-recorded 909 notes sitting off the grid, as expected from
  the ~2 ms output tick.
  - The KS Music Lounge guide (`ksmusiclounge_tr909_guide`) says keys 1–6. That is slightly off: key 1 is
    "off" and keys 2–7 are the six shuffle amounts.
- **Flam.** Keys 9–16, BD/SD/toms only (`gwem_tr909_flam_tutorial`, `ksmusiclounge_tr909_guide`), as in
  the manual and firmware.
- **Accent.** Regebro's SoundCloud set (`regebro_soundcloud_rd9_tr909_all_sounds_accent_levels`) records
  each sound at four accent levels: none, TOTAL only, instrument only, and both. For RS, CP, OH, CR and RD,
  "instrument only" equals "none" and "both" equals "TOTAL only". That matches the firmware's fixed bases
  for those voices (RS/HC 50h; OH/CR/RD 40h). BD, SD, the toms and CH respond to both.
  - These hits give an audible check of the C.4 code ladder: TA = 0 normal 10 / accent 21 for the analog
    voices, and so on. The TOTAL ACCENT knob position was not stated.
- **Timing.** Regebro (`regebro_rd9_vs_tr909_groove_timing`) measures:
  - millisecond-scale hit jitter on the RS and hats, which changes with BPM and from day to day;
  - the TRIG OUT (the RS trigger) as "microsecond-exact at some BPMs";
  - the TEMPO pot as going to the CPU's 0–255 ADC with hysteresis.

  This is what the disassembly predicts. The output-tick phase is deterministic aliasing between the
  crystal-timed step period (12·P µs) and the ~2/~4 ms scan grid (tick and group period). At BPMs where the step period is
  close to a multiple of the scan grid, the error is constant (looks exact); elsewhere it cycles. The
  "day-to-day" variation fits the ±1-count ADC hysteresis: the same knob position can land on a
  neighbouring B, about 1.14 BPM apart, which changes the aliasing pattern.
- **Factory song.** Holding TRACK 1 + PATTERN 1 at power-on loads the factory patterns and song
  (`zibbybone_tr909_factory_song`). This is a v1-only feature: the v4 routine is a stub. A recording of
  the demo therefore comes from a v1 (or v2/v3) unit. Playing it on our emulated v1 is a direct
  end-to-end test of the CPU, sequencer and voices.

### C.9c Cross-check with prior firmware work: Jacquot-SFE `tr-909-analysis`

The repository is https://github.com/Jacquot-SFE/tr-909-analysis (no licence), local copy
`docs/papers/emulations/code/Jacquot-SFE_tr-909-analysis/` [C-25]. It contains:

- MAME-`unidasm` listings of v4 and v1, untouched and partly annotated;
- a globals map (`disassembly/globals.md`);
- CPU and board notes, with **LED and switch matrix tables** (`datasheets/board-notes.md`);
- the NEC datasheets and the user's manual;
- ROM images. All three are byte-identical to ours, including an independently dumped "german-chip"
  v4 (SHA1 b94374e2…). That confirms `rom/tr909_v4.ic609` is a genuine v4.

What it covers, and how it compares:

- **Agrees.**
  - Vectors: reset → 02CD; 0010 INT1/INT2 → C5/BC selected by VV:63 bit 1 (DIN); 0018 timer/event →
    E6; 0020 AD-complete counter (VV:5A); 0028 serial → 162F.
  - V = FF with globals at FF00–FFFF.
  - Serial 31 250 8N1.
  - ADC channel 0 = tempo, channel 3 = total accent.
  - The MKH/MKL usage notes.
  - The MIDI note map.
  - VV:3C initialised to 0Ah and VV:41 to 09h.
- **Settles its open questions.**
  - *"Transmit ch 11, receive ch 10?"* **Yes.** VV:3C = 0Ah is the transmit channel index (ch 11) and
    VV:41 = 09h the receive channel (ch 10). Receive also starts in OMNI ON mode. This matches the
    service notes p.14–15.
  - *"Velocity mapped in the 64–96 range?"* **Partly.** 64–95 is the *transmit* velocity for External
    Instrument notes: 40h + (CR3>>3) on accented ext steps, else 64. *Received* velocities 1–127 map
    through the 64-entry tables at 1F80/1FC0 to 6-bit DAC codes (C.4). The internal voices are never
    transmitted.
- **Differs (an error in its annotation).** At 034A it labels `MOV A,CR1` "conversion result 3 – mstr
  accent". CR1 is AN1, the **cartridge sense** (the `CR1 ≥ 40h` test before the 96 69 signature check).
  TOTAL ACCENT is read from CR3 at 0967/0973.
- **What it adds.** The LED and switch matrix layouts:
  - Switches, PA0–4 × D0–7:
    - row 0: steps 1–8;
    - row 1: steps 9–16;
    - row 2: shift, scale, –, inst, shuffle, last, stop, start;
    - row 3: clear, track 1–4, pattern 1–3;
    - row 4: enter/accent, tape sync, cycle, available, fwd, back, tempo, ext inst.
  - LEDs: PA × PB0–4.

  The first three rows agree with MAME's IN1–IN5 ports.
- **What it does not cover.** The tempo formula, the scan tick and trigger groups, the accent tables,
  shuffle, flam and the bank format. These are only in this survey's annotations (`firmware/disasm/`).

### C.10 Known quirks (summary)

1. **Trigger quantisation and jitter.** Triggers go out only on ~1.92–2.1 ms ticks, with BD/SD/LT/MT and
   HT/RS/HC/HH/CY on alternate ticks. Step processing runs in the foreground. Measured maximum step jitter
   is 4.46 ms internal, 6.40 ms DIN and 7.79 ms MIDI [C-18] **(disasm for the mechanism)**.
2. **Slave sync** extrapolates every second 48-ppqn tick from the last clock interval. v1 can miss the first
   F8 after FA [C-3]. v4 can freeze on DIN [C-15].
3. **MIDI notes** are quantised to the ~2 ms tick. Note-off is ignored. Shared DACs (RS/HC, CR/RD, CH/OH)
   take the last written velocity.
4. **TOTAL ACCENT** is digital, 32 steps, and added to every voice on total-accent steps. Normal + full TA
   equals accent alone. Accented steps at TA 6–7 dip because of the table glitch (C.4).
5. **Flam** is tempo-independent, an integer number of group ticks (f+4 or f+5). The firmware predicts
   ≈15–51 ms; real-909 scope readings give 10–38 ms (unresolved, C.7). The grace note plays at normal level
   and the main note at accent level.
6. **Shuffle** delays the even 16th by 0–6 ticks of 1/48 beat: 0–50 % of a 16th, a 50–75 % swing ratio.
   In 32nd scale only 4 settings differ. It is absent on triplet scales and on the odd last step.
7. **MIDI OUT** sends F8 continuously, Ext-instrument notes only, and merges MIDI IN except clock and SysEx.
8. **The power-up state is not stored**: sync INT, Rx omni/channel 10, Tx channel 11. Factory patterns are
   v1 only.
9. **Power-up trigger.** The CPU raises the HH/crash/ride trigger lines once at power-up (400A = 3Ch).

### C.11 Numbers

| Quantity | Value | Source |
|---|---|---|
| CPU clock | 12.00 MHz crystal; state 250 ns | [C-1], [C-7] |
| ADC conversion / INTAD period | 48 µs / 192 µs (4-channel scan) | [C-7], [C-5] |
| Scan / trigger-output tick | 10 × INTAD = 1.92 ms nominal; probably ≈2.11 ms (11 × INTAD, handler ≈0.4–0.55 ms) | disasm |
| TRIG pulse width | 1 tick ≈ 1.92–2.1 ms (service notes: "2 ms", +5 V) | [C-1], disasm |
| Per-instrument trigger grid | 2 ticks ≈ 3.84–4.2 ms (two alternating groups) | disasm |
| Internal sequencer resolution | 48 ppqn (MIDI out 24 ppqn) | disasm |
| Tempo formula | P = ⌊1 099 138/B⌋ µs per tick, B = 32 + ⌊225·ADC/256⌋ | disasm |
| Tempo range | 36.4–290.0 BPM (spec 37–290), about 1.14 BPM steps | disasm, [C-1] |
| Event counter / timer clock | 1 µs (12 MHz/12) | [C-5], [C-7] |
| MIDI | 31 250 baud, internal clock ×16 | disasm, [C-5] |
| Shuffle | even-step delay s/48 beat, s = 0…6 → 50–75 % | disasm, [C-19] |
| Flam | (f+4 or f+5) × group tick (3.84–4.2 ms), f = 0…7 → ≈15–51 ms; measured 10–38 ms (EchoBoy); Nava/Roland Cloud 20–48 ms | disasm, [C-26] |
| Accent DAC | 6-bit (D1–D6), 8 channels; table 1F80 / 1FC0 | disasm, [C-4] |
| Accent codes (TA = 0) | normal 10, accent 21 (analog); CH 25/40; OH/CY 32; RS/HC 21 | disasm |
| TOTAL ACCENT resolution | 32 steps (ADC >> 3) | disasm |
| MIDI TX velocity | 64 or 64 + TA (≤ 95) | [C-1], disasm |
| Tape sync | 1201.9 / 2403.8 Hz; toggles per 48-ppqn tick; idle 1200 Hz | [C-1], disasm |
| Tape data | ~3.2 kbaud; 300 µs half-period threshold | [C-1] |
| DIN sync | 24 ppqn, clock on pin 3, rising edge; pin 1 run/stop, pin 5 continue | [C-1], [C-2] |
| Trigger Out (RS) | +14 V, 20 ms (manual text: 18 ms) | [C-1], [C-2] |
| Measured jitter (max step-to-step) | INT 4.46 ms, DIN 6.40 ms, MIDI 7.79 ms | [C-18] |
| SysEx bank dump | 16 blocks × 256 B (512 nibbles) = 4 KB per bank | [C-1] |
| RAM / banks | 8 KB; Bank I 2000–2FFF, Bank II 3000–3FFF | [C-4], disasm |
| Cartridge | M-64C 8 KB at 6000–7FFF; sense on AN1 (≥ 40h) | [C-2], [C-4], disasm |

### C.12 Recommendations for our emulator

1. **CPU core.** Use a cycle-counted µPD7810 core (port or adapt MAME's). Model the peripherals faithfully,
   because the firmware derives *all* its timing from them:
   - the ADC: 192 states per conversion, continuous 4-channel scan, INTAD after 4. This is the ~2 ms
     output tick, the trigger width and the flam timebase;
   - the timer/event counter: ECNT at fosc/12, the ETM1 compare and clear;
   - timers 0 and 1 cascaded into the TO output (tape sync);
   - the UART at 31 250 baud;
   - interrupt masking and priorities, including nested `EI` in the INTAD handler;
   - the skip and string-effect rules.

   Run the CPU at its true 12 MHz (4 M states/s) in lock-step with the audio, scheduling by cycles inside
   each audio block, so trigger edges land with sub-sample accuracy.
2. **Voice interface.** Latch 4001–4008 writes as 6-bit codes into the accent-DAC model (the RM0621 ladder)
   and 4009/400A as trigger lines, with the voice/DAC sharing listed in C.4.
   - Hold each DAC level until it is rewritten.
   - Hold the HH CLOSED/OPEN line as a level.
   - Feed trigger edges into the analog voice models at the exact emulated time of the `BLOCK` write. The
     pulse width then comes out of the emulation (about 2 ms).
3. **Analog inputs.**
   - Map the TEMPO and TOTAL ACCENT knobs to 0–5 V on AN0 and AN3. The firmware does the quantisation.
   - Tie AN1 low for no cartridge. Optionally emulate an M-64C as 8 KB of RAM at 6000 with the sense
     input ≥ 40h.
4. **NVRAM.**
   - Persist the 8 KB RAM in the plugin state.
   - To give users the factory patterns, run v1 once with TRACK 1 + PATTERN 1 held, then switch to v4.
     Alternatively, ship a RAM image made that way.
   - Support the SysEx bank dump for import and export.
5. **Host sync.** Offer three modes:
   - **Authentic**: feed host MIDI clock or Start/Stop into the emulated UART as real 31 250-baud bytes,
     with TEMPO MODE = MIDI. This reproduces the slave jitter.
   - **DIN**: drive INT1/PC5/PC6 with 24-ppqn pulses derived from the host transport.
   - **Locked**: internal tempo, with the TEMPO ADC chosen to match the host BPM. Resolution is only about
     1.14 BPM, so either scale the ETM1 period (patch the 44h/45h variables) or slightly scale the
     emulated CPU clock so the host tempo is exact. Document that the latter also scales the ~2 ms tick
     by the same small factor.
6. **MIDI from the host.** Serialise incoming events into the UART at 31 250 baud for authentic latency.
   Offer a "fast" option that injects bytes back-to-back. Expose MIDI OUT (F8 and Ext-instrument notes).
7. **Firmware.** Ship v4 as the default and v1 as an option. Both ROMs are already in `rom/`.

### C.13 Open questions and measurements to take

- **The flam discrepancy**: EchoBoy's 10–38 ms against the firmware's (f+4…5)·T_g (C.7).
- The exact scan-tick period on real hardware: 1.92 ms nominal, ≈2.11 ms if one INTAD is absorbed per
  scan (C.4), against the ≈1.84 ms implied by the Revolution 909 manual. Possible measurements:
  - the TRIG pulse width on IC1/IC10 with a scope;
  - the flam interval in recordings;
  - in our emulator, a statistic of INTAD-handler duration and lost INTADs.
- Confirm the group-alternation offset audibly. BD and HH on the same step should sit one tick (~2 ms) apart, in
  either order.
- Whether the accent-table glitch (index 43) is audible: an accented step with TOTAL ACCENT around 20 %.
- The pattern-memory layout beyond the headers. This is only needed for the importer and editor UI.
- The SysEx checksum algorithm.
- The behaviour behind the v4 "freezes on DIN sync" bug [C-15].
- v2 and v3 images. None are public; they are probably identical to v1.
- What the power-up write `400A = 3Ch` does to the sample counters (coordinate with the cymbal/HH section).

### C.14 Sources

| # | Source | Local copy |
|---|---|---|
| C-1 | Roland *TR-909 Service Notes*, 1st ed., 15 Jun 1984. https://archive.org/details/synth_Roland_TR-909_service_notes | `roland/TR-909_service_notes_A.pdf` (+ `_text.pdf`, `.txt`, `service_notes_pages/pNN.png`) |
| C-2 | Roland *TR-909 Owner's Manual*, Dec 1983. https://archive.org/details/synthmanual-roland-tr-909-owners-manual | `roland/TR-909_owners_manual.pdf` (+ `_text.pdf`, `.txt`) |
| C-3 | Roland Service Information No. 100236, "TR-909 PROM version up", 6 Feb 1985, via R. Whittle (original diprec.com) | `firmware/roland_service_information_100236_1985-02-06_TR-909_PROM_v1_to_v4.gif` |
| C-4 | MAME `src/mame/roland/roland_tr909.cpp` (AJR). https://github.com/mamedev/mame/blob/master/src/mame/roland/roland_tr909.cpp | `reference/mame/src/mame/roland/roland_tr909.cpp` |
| C-5 | MAME µPD7810 core and disassembler (Juergen Buchmueller et al.). https://github.com/mamedev/mame/tree/master/src/devices/cpu/upd7810 | `reference/mame/src/devices/cpu/upd7810/` |
| C-6 | NEC µPD7810/7811 data sheet, *1984 NEC Microcomputer Catalog* pp. 4-51…4-64. http://bitsavers.trailing-edge.com/components/nec/_dataBooks/1984_NEC_Microcomputer_Catalog.pdf | `nec/uPD7810_7811_datasheet_NEC_1984_catalog.pdf` (+ `.txt`) |
| C-7 | NEC µPD7810/11 data sheet, *1987 Microcomputer Products Vol. 1* pp. 4-75…. http://bitsavers.trailing-edge.com/components/nec/_dataBooks/1987_Microcomputer_Products_Vol_1.pdf | `nec/uPD7810_7811_datasheet_NEC_1987_microcomputer_products_vol1.pdf` (+ `.txt`) |
| C-8 | NEC *User's Manual: µPD7810/11, 7810H/11H, 78C10/C11/C14*, Apr 1987 (scan, no text layer). https://datasheets.chipdb.org/NEC/uPD78C1x/uPD78C10_Details.pdf | `nec/NEC_uPD7810-11_7810H-11H_78C10-C11-C14_Users_Manual_Apr1987.pdf` |
| C-9 | NEC µPD7810/11 NMOS data sheet (scan). https://datasheets.chipdb.org/NEC/781x/uPD7810.pdf | `nec/uPD7810-11_datasheet_NMOS_chipdb.pdf` |
| C-10 | NEC µPD78C10/C11/C14 data sheet, Nov 1987. https://datasheets.chipdb.org/NEC/uPD78C1x/uPD78C10_Summary.pdf | `nec/uPD78C10-C11-C14_datasheet_Nov1987.pdf` |
| C-11 | NEC µPD78C10A/11A/12A data sheet, 1995. http://bitsavers.trailing-edge.com/components/nec/_dataSheets/uPD78C10A_78C11A_78C12A_199502.pdf | `nec/uPD78C10A_78C11A_78C12A_datasheet_1995.pdf` (+ `.txt`) |
| C-12 | NEC *87AD Series µPD78C18 User's Manual*, U10199EJ5V0UM00, 1995 (full 87AD instruction set). https://datasheets.chipdb.org/NEC/uPD78C1x/uPD78C18.pdf | `nec/NEC_87AD_uPD78C18_Users_Manual_1995.pdf` (+ `.txt`) |
| C-13 | R. Whittle, *Modifications for the TR-909* and *TR-909 Version 4 firmware* (2018). https://www.firstpr.com.au/rwi/tr-909/ | `firmware/whittle/` (page text, firmware PDF, sound-mods PDF, voicing-board schematic PNG) |
| C-14 | This survey's disassembly: v1/v4 listings, annotations and tool | `firmware/disasm/` |
| C-15 | Addictive Instruments, *Revolution 909 v5 Owner Manual* and product page (2024). https://addictiveinstruments.fr/en/kits/1-revolution909.html | `firmware/revolution909_v5_owner_manual_en.pdf` (+ `.txt`), `firmware/revolution909_v5_addictiveinstruments.txt` |
| C-16 | The Modlin Beat, "Roland TR-909 Digital Mods" (Crowd Supply): OS versions, MIDI driver. https://www.crowdsupply.com/modlin-beat/909-mods | `firmware/crowdsupply_modlin_beat_909_mods.txt` |
| C-17 | Mod Wiggler, "TR-909 OS Rom File or how is the shuffle exactly implemented" (2015), quoting Colin Fraser on the 2 ms scan. https://www.modwiggler.com/forum/viewtopic.php?t=135002 | `firmware/modwiggler_t135002_909_shuffle_implementation_2015.txt` |
| C-18 | Innerclock Systems timing tests and statements (via Matrixsynth, 22 Jul 2014). Jitter figures from the Gearspace thread "Roland TR909 jitter test results" (t=1111455; page blocked to scripts, numbers from search excerpts). https://www.matrixsynth.com/2014/07/roland-tr-909-timing-issues.html · https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/1111455-roland-tr909-jitter-test-results-internal-sequencer-din-sync-midi-clock-slaved.html | `firmware/matrixsynth_2014-07_tr909_timing_innerclock.txt` |
| C-19 | Gearspace, "Getting the TR 909 swing on the Jomox Xbase 999" (2/96-per-setting statement; page blocked, from search excerpt). https://gearspace.com/threads/getting-the-tr-909-swing-on-the-jomox-xbase-999.902028/ | — |
| C-20 | D. Johnson and D. Poyser, "Roland TR909 (Retro)", *Sound On Sound*, Nov 1995. https://www.soundonsound.com/reviews/roland-tr909 | `firmware/sos_1995-11_tr909_retro_review_johnson_poyser.txt` |
| C-21 | Matrixsynth, "Roland TR-909 with ver 4 EPROM" (2008), user comments on v1/v4 sync. https://www.matrixsynth.com/2008/06/roland-tr-909-with-ver-4-eprom.html | `firmware/matrixsynth_2008-06_tr909_v4_eprom_comments.txt` |
| C-22 | Colin Fraser, "909 Sound Mods" (BD ENV3 tune range "30 to 120 ms", C8/C9; voice-relevant). http://www.colinfraser.com/tr909/909mods/909mods.htm (Wayback) | `firmware/colinfraser_909_sound_mods.txt` |
| C-23 | KVR "Roland TR-909 Midi Problems" (t=206368) and Gearspace "Roland tr 909 external midi problem" (900868): MIDI OUT sends only Ext-instrument data (search excerpts; pages blocked). https://www.kvraudio.com/forum/viewtopic.php?t=206368 | — |
| C-24 | Reference videos and audio collected by the reference-audio survey: Starsky Carr, Regebro (timing, accent levels, revisions), GWEM flam tutorial, KS Music Lounge guide, Zibbybone factory song | `reference/real909/videos/<id>/` (each with `.description` and `.transcript.txt`) |
| C-25 | Jacquot-SFE, *tr-909-analysis* (no licence): marked-up v4/v1 disassembly, globals, scan matrix, NEC docs, ROM dumps. https://github.com/Jacquot-SFE/tr-909-analysis | `emulations/code/Jacquot-SFE_tr-909-analysis/` (cloned by the emulations survey) |
| C-26 | EchoBoy, "Flam timings", e-licktronic forum, 22 Jul 2017: scope of the TRIG line, flam 9–16 → 10–38 ms. http://www.e-licktronic.com/forum/viewtopic.php?f=25&t=1430 | `emulations/web/elicktronic_forum_flam_t1430.txt` (saved by the emulations survey) |

---

## 6. Existing emulations, clones and replicas

This section covers how other people have rebuilt the TR-909 in hardware and software, what
they publish about it, and the constants they use. Sources are `[D-n]` and are listed at the end of
the section. Local copies are under `docs/papers/emulations/` (written as `emulations/` below).

### D.0 Summary

- **Most products are not circuit simulations.** Only Roland's ACB (TR-8, TR-8S, TR-09, Roland
  Cloud TR-909, TR-1000) is claimed to be a component-level simulation, and Roland publishes no
  technical detail. Everything else falls into one of these groups:
  - analog hardware copies of the voicing board: DinSync RE-909, Steda SR-909, e-licktronic Nava,
    9090, Behringer RD-9, Tiptop and Erica Eurorack modules;
  - "VA" synthesis fitted by ear: D16 Drumazon, AudioRealism ADM, ER-99, Plaits;
  - sample players: andremichelle, netpd, VCV DrumKit, ESP-909, Wave Alchemy.
- **Every serious product keeps the original 6-bit cymbal/hat data in some form, or a re-sample of
  it.** Two open-source hi-hat players [D-44][D-45] treat the low two ROM bits as garbage and mask
  them. They also split the hat ROM into an open part (0x0000 to about 0x6010–0x6040) and a closed
  part (from there to 0x7FFF).
  - This disagrees with the service-notes address table and with our own ROM statistics: the low
    bits are only populated in the 0x3FC1–0x5FBA region.
  - **Settled in section 4 (B.3):** the circuit uses only bits 7..2 (six data lines; the Nava netlist agrees). The populated low bits are present in the file but unused. The hardware split is exactly 0x6000; the players' 0x6010/0x6040 fall inside the closed hat's 65-sample silent lead-in.
- **The best machine-readable cross-check of the schematic is the e-licktronic Nava Eagle file**
  [D-32].
  - It is a one-to-one copy of the voicing board and keeps Roland's designators. Examples: R59 = 47 k,
    R52 = 10 k, R53 = 2.2 k, R54 = 470 k, C9 = 0.33 µ, C69 = 0.01 µ, C71 = 0.0068 µ, R417 = 3.3 k and
    C134 = 0.01 µ.
  - The R417 and C134 values are the late-serial values, so the file follows a late revision.
  - I extracted a parts list (1695 parts) and a netlist (1017 nets, 15 sheets, one per voice) to
    `emulations/code/e-licktronic_Nava-v1.0/Eagle/Nava_v1.02_{parts.csv,netlist.txt}`.
  - This is the fastest route to SPICE decks of each voice, and it lets us check our own service-notes
    transcription.
- **Roland itself warns that the published service notes contain errors.** "If you search for the
  808 circuit or 909 circuit, you can find many service notes. But some portions of the circuits are
  wrong. If you just copy it, the sound is very different" (Daichi, Roland, on the TR-1000 [D-18]).
  - Roland also says different production runs sound different: "the overall sound of the 909, with
    later serial numbers being drastically different" [D-18].
  - We should model the documented serial-number revisions as switchable variants. See A1/A2 for the
    change list.
- **Sequencer timing published by emulators disagrees.**

  | Source | Flam spacing |
  |---|---|
  | EchoBoy, scope measurement of a real 909 [D-33] | 10–38 ms in 4 ms steps (flam settings 9–16), not tempo-dependent, about 4 ms sequencer jitter |
  | andremichelle [D-41] | 10 + 4·i ms |
  | Nava firmware [D-32] | 20–48 ms |
  | Roland Cloud TR-909 [D-1] | "TYPE1 = 0 ms, TYPE2–9 = 20–48 ms in 4 ms units" |

  - Shuffle is just as inconsistent; see D.7.
  - Because we run the real v4 firmware, these behaviours should emerge by themselves. These numbers
    are validation targets, not inputs.

---

### D.1 Roland Cloud TR-909 (installed; ACB, 2018)

Sources:

- the plug-in manual (©2018), copied to `emulations/roland_cloud_TR-909_Manual_E.{pdf,txt}` [D-1];
- a read-only look at the VST3 bundle (version 1.1.3, build 22072, binary dated 2026-01-14), written
  up in `emulations/roland_cloud_TR-909_bundle_notes.txt` [D-2].

The plug-in was not run.

**Architecture revealed by the bundle** [D-2]:

- **It is the TR-8S engine.**
  - The parameter database is the TR-8S one: `CPrmDSPTr8s`, bank header `KoaBankFile00003PG-TR909`,
    TR-8S address map.
  - There is an XML comment "parameter specialized in TRX plugin (software only)".
  - It can send and receive patterns to and from a TR-8S.
- **Each kit slot is fixed to a TR-8S ACB "tone".** `INST TONE` is 27, 31, 33, 34, 35, 36, 37, 38,
  39, 40, 41.
  - The TR-8S tone-name table in the bundle (`ScriptResource.xml` `instName`, 0-based) maps these to:
    26 = 909 Bass1, **27 = 909 Bass2**, 28 = Attack Bass, 29 = Low Bass, 30 = 909 Snare1,
    **31 = 909 Snare2**, 32 = Attack Snare, then 33–41 = LT, MT, HT, RS, HC, CH, OH, CC, RC.
  - So the plug-in uses the *second* ACB 909 kick and snare.
  - SOS says the TR-1000's ACB kicks "can be switched between the TR8 and Boutique versions" [D-17].
    This suggests (my inference) that Bass1 and Snare1 are the TR-8 (2014) models and Bass2 and
    Snare2 the TR-09 (2017) revision.
- **DSP class names (C++ RTTI strings) show how Roland split the voices:**
  - `CDSPSimTr909`;
  - BD = `CDSPTr909Bd` with `Bd1`, `Bd2`, `BdAtk`, `BdLow`;
  - SD = `CDSPTr909Sd` with `Sd1`, `Sd2`, `SdAtk`;
  - `CDSPTr909Tom` / `Lt` / `Mt` / `Ht`, `CDSPTr909Rs`, `CDSPTr909Hc`, `CDSPTr909Ch`, `Oh`, `Cc`, `Rc`;
  - TR-8S mixer and trigger classes (`CDSPTr8sMix`, `CDSPTr8sMixInst`, `CDSPTr8sTrig`, `CDSPTr8sXTrig`)
    and `CDSPRev`.
  - So the kick has two oscillator/shaper sub-blocks, an attack (click/noise) generator and a "low"
    part. The snare has two oscillators and an attack part. This matches the part-by-part split we
    plan.
- **Sample data.** A localisation key `LOADINGSAMPLES` exists, so sample data is loaded at start-up,
  presumably the cymbal and hat PCM.
  - Forum users say the TR-8 uses the original 909 samples processed through ACB (Gearspace, quoted
    in search results, page blocked).
  - Roland has not confirmed this.

**Parameter list.** Every voice parameter is stored as 0–255 with default 128. Exceptions: LEVEL
defaults to 255, and GAIN uses 0–161, which maps to −INF, then −40.0 … +40.0 dB in 0.5 dB steps, with
81 = 0 dB. The CC numbers are the plug-in's default receive map [D-1 p.8].

| Voice | Original-panel controls (CC) | Extra controls (not on a real 909) | Internal field |
|---|---|---|---|
| BD | LEVEL (24), ATTACK (21), DECAY (23) | **TUNE (20)**, shown in the PANEL/option view; GAIN; PAN | `INST LEVEL/DECAY/TUNE`, `sp.saveKit.BD ATTACK` |
| SD | TUNE (25), TONE (28), SNAPPY (26), LEVEL (29) | GAIN, PAN | TONE is bound to `INST DECAY`; `SD SNAPPY` 0–255 |
| LT/MT/HT | TUNE (46/49/52), DECAY (47/50/53), LEVEL (48/51/54) | GAIN, PAN | |
| RS | LEVEL (57) | GAIN, PAN | |
| HC | LEVEL (60) | GAIN, PAN | |
| CH | DECAY (62), LEVEL (63) | **separate CH level**, GAIN, PAN | the original has one HI-HAT LEVEL for both CH and OH |
| OH | DECAY (81), LEVEL (82) | separate OH level, GAIN, PAN | |
| CC | TUNE (83), LEVEL (85) | GAIN, PAN | |
| RC | TUNE (86), LEVEL (88) | GAIN, PAN | |
| Global | TOTAL ACCENT (71), SHUFFLE (9), VOLUME (7) | per-instrument shuffle amount; kit level (0–127, default 107) | `ptnCmn.ACCENT` 0–255; `SHUFFLE(COMMON)` 0–255 (default 128) × `INSTnn SHUFFLE` 0–255 (default 255) |

Panel GAIN is stored as `INSTn BOOST`, 0–255 shown as −128…+127. MIDI Learn can remap any control.

**Pattern model (an extension of the original):**

- 8 variations (A–H) per pattern.
- Per-instrument LAST STEP (1–16).
- SCALE: 4 values, default index 2.
- Per-step strong and weak beats, using the masks `ACCENT PTN` and `ACCENT PTN_WEAK`.
- A TOTAL ACCENT step row whose strength is set by the TOTAL ACCENT knob.
- FLAM TYPE 1–9 per pattern: TYPE1 = 0 ms, TYPE2–9 = 20–48 ms in 4 ms steps.
- SUB STEP for duplets, triplets and quadruplets.
- Banks of 128 patterns and 128 kits.
- When LAST STEP differs per instrument, the accent row follows the instrument that is outlined in
  red [D-1 p.5].

**Outputs:** the main stereo mix plus 11 mono sub-outs, in the order BD, SD, LT, MT, HT, RS, HC, CH,
OH, CC, RC [D-1 p.8]. Note that CH and OH are separate here; the original has one hi-hat output.

**MIDI note map (receive)** [D-1 p.8]:

| Note | Function | Note | Function |
|---|---|---|---|
| 24–31 | select variation A–H (immediately, not at the bar) | 41, 43 | Low Tom |
| 32 | start sequencer | 42, 44 | Closed HH |
| 33 | stop sequencer | 45, 47 | Mid Tom |
| 35, 36 | Bass Drum | 46 | Open HH |
| 37 | Rim Shot | 48, 50 | High Tom |
| 38, 40 | Snare | 49 | Crash |
| 39 | Hand Clap | 51 | Ride |

The 35–51 part matches the original TR-909 receive map recorded in the firmware notes of [D-47]:

- 35/36 BD, 37 RS, 38/40 SD, 39 HC, 41/43 LT, 42/44 CH, 45/47 MT, 46 OH, 48/50 HT, 49 CC, 51 RC.
- [D-47] adds: "defaults to transmit on channel 11, receive on channel 10?" and "velocity is mapped in
  the 64–96 range?". **Settled in section 5 (C.5, C.9c):** receive is channel 10 (omni) and transmit channel 11. 64–95 is the *transmit* velocity of External Instrument notes (64 + TA). Received velocities 1–127 map to DAC codes through the firmware tables.

**About ACB, the manual says nothing technical.**

Roland's public description of ACB [D-10][D-11]:

- "original design specs, consultation with original engineers, and a detailed, part-by-part
  analysis of each analog circuit in our own pristine units";
- it "reproduces each analog component by thoroughly analyzing each detail of the original design
  drawings. By combining the analyzed components in exactly the same manner…";
- "It is not possible to reproduce the original sounds just by logically analyzing the analog
  circuits".

### D.2 Roland ACB hardware: TR-8 (2014), 7X7-TR8, TR-8S (2018), TR-09 (2017), T-8, TR-1000

**TR-8 (2014)** [D-5][D-6][D-12][D-13]

- First ACB product: 808 + 909 "bottom-up synthesis and no sampling" (SOS TR-8S review [D-14]), 32-bit
  96 kHz engine (MusicRadar [D-13]).
- Extras: BD/SD compressor (CC 22/27), BD ATTACK, SD SNAPPY, and TUNE/DECAY for every voice including
  RS, HC, CH, OH, CC and RC.
- An alternate long-decay 808 kick "shifts its pitch on repeated hits"; the 909 has no such variant
  [D-12].
- SOS (Paul Nagle, who had a real 909 on the bench): "kits set up the same way were as
  interchangeable as two different 808s or 909s would be… I frequently failed to identify which was
  which". Nagle also confirms the snare+clap "swoosh" when both play together, caused by the shared
  noise source played through two different filters [D-12].
- **MIDI map (v1.11)** without 7X7: BD 35/36, SD 38/40, LT 41/43, MT 45/47, HT 48/50, RS 37, HC 39,
  CH 42/44, OH 46, CC 49, RC 51; channel 10 by default.
  - With 7X7-TR8: BD2 35, SD2 40, cowbell 56, tambourine 54 [D-6].
- **CC map (TR-8):** 9 shuffle, 12/13 ext in, 16–18 delay, 20 BD TUNE, 21 BD ATTACK, 22 BD COMP,
  23 BD DECAY, 24 BD LEVEL, 25 SD TUNE, 26 SD SNAPPY, 27 SD COMP, 28 SD DECAY, 29 SD LEVEL, 46–54
  toms, 55–57 RS, 58–60 HC, 61–63 CH, 68–70 scatter, 71 ACCENT, 80–82 OH, 83–85 CC, 86–88 RC, 89–91
  reverb [D-6].
  - The Roland Cloud plug-in kept the same numbers, as did the TR-09 below.

**TR-09 (Boutique, 2017)** [D-3][D-4][D-15][D-16]

- The voice set and panel of the 909, plus BD/SD compressors.
- Hidden parameters via the menu or CC: gain, pan, tune for RS/HC/CH/OH, decay for RS/HC/CC/RC.
- Tempo 40.00–300.00 BPM (the original's range is 37–290).
- Step entry cycles **off → weak → strong for BD, SD, toms and CH; off → strong for the others**.
  This mirrors which 909 voices have two-level per-step accent.
- Shuffle is set with SHUFFLE/FLAM + keys 1–7 (key 8 = off). Flam spacing is set with SHUFFLE/FLAM +
  keys 9–16 (8 values), and flams apply to BD, SD and toms only. Both match the original UI (see
  [D-33]).
- 32-step "backbeats": ENTER + step enters a note half a step later.
- **TR-09 MIDI (v1.00):** default channel 10.
  - Transmit: BD 36, RS 37, SD 38, HC 39, CH 42, LT 43, OH 46, MT 47, CC 49, HT 50, RC 51, with
    velocity 80 for unaccented notes.
  - Receive adds 35, 40, 41, 44, 45 and 48.
  - Clock, Start, Continue and Stop are sent and received; Song Position and Song Select 0–7 are
    supported.
  - CC map as the TR-8, plus 55–60 RS/HC tune/decay/level and 61 CH TUNE [D-4].
- **SOS (Nagle):**
  - "the ACB modelling this time out sounds even more like an example of a TR-909 you'd encounter
    today… perhaps Roland have revisited the circuits and updated ACB to include simulated component
    ageing";
  - "after a prolonged session… something about the top end begins to grate" (also reported for the
    TR-8);
  - the clap/snare phasing from the common noise source is reproduced [D-15].

  The "grating top end" may be aliasing or DAC imaging. It argues for generous oversampling in our
  model.

**TR-8S (2018)** [D-7][D-8][D-9][D-14]

- The full ACB set (808, 909, 606, 707, 727, CR-78…) plus samples.
- Each ACB tone has one "CTRL" parameter: 909 BD → *Attack* (0–255), SD → *Snappy* (0–255), toms →
  *Color* ("amount of ambience (amount of noise)", −128–0–+127) [D-8].
- **Default note map:** BD 36 (alt 35), SD 38 (40), LT 43 (41), MT 47 (45), HT 50 (48), RS 37 (56),
  HC 39 (54), CH 42 (44), OH 46 (58), CC 49 (61), RC 51 (63). Configurable.
- **CC:** 20/23/24 BD tune/decay/level and 96 BD CTRL, … 110 RC CTRL [D-9].

**TR-1000 (2025)**

- Analog 909 BD/SD/toms/RS/HC rebuilt with modern parts, plus ACB and "circuit-bent ACB" [D-17]–[D-19].
- Peter Brown (Roland): ACB's "component modeling is inherently modular, which makes it possible to
  start breaking and modifying connections"; "the 909 Rim Shot… the sound itself is three layers"
  [D-19].
- Roland "tested a variety of original and modified TR-808 and 909s, even ones considered wonky"
  [D-19], and "created a composite" sound after asking artists [D-18].
- SOS compares the TR-1000's analog 909 kick with the ACB kick in default settings: "the ACB version
  having a kind of subtle grind to its tone… compared to the purer starting point of the analogue",
  and "with the 909 I'd say it's too close to call" against YouTube originals [D-17].

**Original design team.** Atsushi Hoshiai did the 909's digital circuits and programming. The team
was two people: Hoshiai and a senior analog engineer.

- He sampled his own hi-hats (Paiste top, Zildjian bottom) "at 6-bit" in the office at night.
- He edited the waveform on a CP/M-80 computer.
- He noticed a delay between pressing the button and the sound, which he attributed to the attack
  shape of the waveform.
- Development names were TR-880, TR-880D, TR-909D and finally TR-909; about 10,000 units were made
  [D-20][D-21].

### D.3 Behringer RD-9 (2021)

Sources: the user manual [D-23], maffez's mod document [D-24] and reviews [D-25].

**What it is.**

- An analog copy of BD, SD, toms, RS and HC. The spec sheet lists "Digital (Sampling) Closed hat,
  Open hat, Crash, Ride".
- An STM32F746 CPU.
- "Sample instruments from DA conversion onwards are like on the 909" [D-24].
- The hat, crash and ride pitch CVs go to IC30 pin 12, IC31 pin 5 and IC32 pin 5 (0–3.3 V, diode-
  clipped), so the MCU sets the sample playback clock from a CV.
- VCA CV points: hats T42, crash T44, ride T46.
- "System noise is done with an XOR gate, an octal buffer and a transistor… shared by snare and clap
  (phasing effect)"; "Tom Noise processing is as per Roland schematics" [D-24].

**Deviations listed by maffez** [D-24]:

- TL074/TL064 and LM4580 op-amps instead of M5218L. maffez: "old op-amp types have a higher input
  offset and slower slew rate, which may well account for the RD9 sounding brighter".
- High-hFE VCA transistors.
- 0.01 µF caps on the individual outs (as on late 909s).
- Main output coupling caps of 10 µF instead of 1 µF.
- A discrete clone of the BA662 OTA in the clap.
- R575 (= 909 R59, nominally 47 k, the BD pitch resistor) measured at "some 52k" on his unit.

Cross-reference designators maffez gives (RD-9 → 909):

- R575 = R59, R656 = R52, R189 = R53 and R460 = R403.
- R314 is given as both "TR909-R25" and "TR909-R54"; the document is inconsistent.
- C316 = C83 (the "snare body" ring capacitor).

Against Obie69's recordings of Peter Grandl's (refurbished, extended-BD-decay) 909, maffez found:

- the RD-9 kick VCO waveform "a bit softer (more towards sine)";
- RD-9 toms brighter and shorter. Even with 220 nF across the output the RD-9 "was still a teeny bit
  brighter";
- the original snare has more body.

**Enhanced mode.** It adds BD PITCH and PITCH DEPTH and a hi-hat TUNE. In "OG" mode the BD TUNE knob
controls pitch-envelope depth, as on the 909. maffez found the enhanced pitch-depth range too large
and changed R721 from 10 k to 22 k.

**Sequencer.**

- 64 steps, 24 PPQ default clock.
- Swing as a percentage: "25 % negative swing, 50 % straight to 75 % full swing".
- Adjustable FLAM width, probability, polymeter.
- Trigger outs +5 V, 2 ms.

**MIDI map.** The default table printed in the manual looks copied from the RD-8: "Snare 40,
Low Tom/Low Conga 45, Mid 47, Hi 50, Rim Shot/Claves 37, Clap/Maracas 39, Cowbell 56, Cymbal 51,
Open Hat 46, Closed Hat 42, BD 36", and crash is not listed [D-23]. Treat it as errata, not as 909
behaviour.

**Samples.** Whether Behringer licensed or re-sampled the 909 ROM data is not documented. Users
describe RD-9 hats as "very clean/flat and a bit more bright". This is an open question.

### D.4 Other hardware clones and replicas

| Product | Type | Voices | What they say about method or measurements |
|---|---|---|---|
| DinSync RE-909 (2020) [D-29][D-28] | 1:1 replica (DIY kit / assembled by Kumptronics) | all; uses EPROMs for the cymbal ROMs, with an adapter because the Hitachi mask ROM pinout differs | "We took a pristine TR-909 to pieces and painstakingly created a true 1:1 replica… made as spare parts interchangeable with the original". They planned slightly "sharpened" samples (forum). andremichelle sampled an RE-909 for his web emulation [D-41]. |
| Steda SR-909 [D-28] | DIY replica | all | "can use it for parts to repair an original" |
| e-licktronic Nava (2016) [D-32] | DIY replica + new ATmega1284 sequencer | all; three 27C256 EPROMs hold the cymbal/hat data; 4040/4520/4013/4174 counters; M5218, 2SC2603/2SA1115/2SA798/2SD1469 as original | Eagle schematic and board published (no licence file). Netlist extracted (see D.0). Firmware v1.028b: 96 PPQN; shuffle = 0…−6 ticks on alternate steps; flam timer 20–48 ms "from EchoBoy" |
| 9090 / TR-9090 [web] | DIY rack voice clone (MIDI, no sequencer) | all | "accurate clone voices", extra BD controls |
| Colin Fraser "my909" (1997) [D-30] | DIY voice clone | all | Extracted the cymbal data by wiring into a real 909. "Each sample is shifted by two bits to occupy eight bits… lowest 2 bits are not used. I chose an arbitrary sample rate of 32 kHz as this is pretty close to the rate used for the hats". "Cymbal envelopes are generated by extra DACs on the address lines… exponential decay with the same length as the sample regardless of tune setting." |
| Tiptop Audio BD909, SD909, TOMS909, RS909, CP909, HATS909, CYMBL909 [D-26] | Eurorack, analog plus 6-bit samples | per module | BD909: "faithfully recreates… reverse-engineered", adds tune CV, tune attack, waveshaper. TOMS909: "9 triangle oscillators" (3 per tom). RS909: "three T-bridge oscillators producing damped sine waves". CP909: "triple-saw envelope generator controlling a VCA, fed by a quasi-random logic-based noise generator". HATS/CYMBL909: "low-fi 6-bit sound samples processed through analog elements", tunable. |
| Erica Synths (with e-licktronic): Bass Drum2, Snare, Clap, Toms, Hi-Hats D, Cymbals [D-27][D-28] | Eurorack | per module | Hi-Hats D: "6-bit discrete DAC", AS3330 VCA. Cymbals: "10 sets of custom cymbal samples" (not original data). |
| Hexinverter Mutant BD9 [D-53] | Eurorack, 909-kick-derived | BD | VCO about 30–240 Hz, sub-osc, pitch-decay circuit |
| Jomox XBase 09 (1998) [D-31] | Analog BD/SD, 8-bit samples for hats, cymbals, RS and clap | — | "six 909-derived samples" |
| Behringer RD-9 | see D.3 | | |
| Roland TR-1000 | see D.2 | | |

Other DIY pieces: NeinOhNein CLAP (909 clap PCB), analog-synth.de 909 BD PCB, and the network-909
and gzifcak 909 pages cited by maffez [D-24]. These are left to A1/A2.

### D.5 Commercial software

| Product (vendor, year) | Method | Claims about modelling or measurement |
|---|---|---|
| **Roland Cloud TR-909** (Roland, 2018) | ACB (TR-8S engine), see D.1 | part-by-part analysis of pristine units [D-10] |
| **Drumazon** (D16 Group, 2007) [D-34] | VA synthesis for the analog voices; "6-bit sampled hi-hats and cymbals" emulated | eMusician: emulates "the common noise generator… every time you trigger a snare or a clap, you hit on a different period of this noise cycle". Extra Pitch/Tune Depth, tom Tone and Tune Decay, RS/hat/cymbal tune and decay. |
| **Drumazon 2** (D16, 9 Sep 2023) [D-35][D-36] | "each sound is now fully synthesized" | "matching each individual instrument with meticulous accuracy". No technical detail. A German review quoted by D16 compared it to "well-documented recordings of a TR-909 sample session". |
| **AudioRealism ADM** (2007; ADM1x 2022) [D-37][D-38] | "analogue modelling… for the 606 and 808; combination of samples and synthesis for the 909" | ADM1x 1.5.2: "Improved 909 Emulation (Bass Drum, Snare modelled from scratch)". "The 909 Model is also stereo now with the default panning set to replicate the real TR-909." Fork A2 should check the original's L/R mixing. |
| **Novation DrumStation** (1996) [D-39] | "analogue sound modelling" digital 808/909 rack, no sequencer | — |
| **Propellerhead ReBirth RB-338** (909 added in v2, 1998) | sample-based | — |
| **Wave Alchemy Revolution** (2017, Kontakt) [D-52] | Deep multisampling of a real 909 | "20,000 samples were taken of the 909 bass drum alone" at 96 kHz across knob positions, with round robins. A potential (paid) reference dataset. |
| Sonic Academy KICK, NI Battery kits, Ableton/Reason kits, Samples From Mars 909, etc. | samples or generic kick synthesis | — |

### D.6 Open-source code

All clones are reference text only (shallow copies or selected files, under
`emulations/code/`). Nothing was built or run.

| Project | URL | Licence | Method | Constants worth noting |
|---|---|---|---|---|
| MAME `roland_tr909.cpp` (AJR) | in `reference/mame/` | BSD-3 | skeleton driver, no sound | memory map: DAC1–8 at 0x4001–0x4008 (6-bit, D1–D6), TRIG1/2 at 0x4009/0x400A, switch rows 0x4000, cartridge 0x6000–0x7FFF; µPD7810 at 12 MHz. VintageEmulatorStudio's "patched MAME 0.289" copy of this file is identical [D-51]. |
| **Jacquot-SFE/tr-909-analysis** | https://github.com/Jacquot-SFE/tr-909-analysis | none (all rights reserved) | **firmware reverse engineering**: marked-up V4 and V1 disassembly (unidasm), globals map, scan-matrix tables, µPD7811 / µPD78C10 datasheets, ROM dumps, 909 schematic PDF | Serial set to 31250 8N1. ADC ch0 = tempo, ch3 = total accent. Interrupt vector notes. Globals in on-chip RAM 0xFF00–0xFFFF via the V register. MIDI note map (above). **Relevant to fork C.** |
| **e-licktronic Nava v1.0** | https://github.com/e-licktronic/Nava-v1.0 | none stated | replica hardware + ATmega firmware | 96 PPQN (16th = 24 ticks); shuffle table {0,−1…−6} ticks applied with alternating polarity; 7 shuffle types; flam OCR3A {4999…11999} @ 250 kHz = 20…48 ms. Plus parts and netlist (D.0). Forks: Oortone, Modularsoundsystems (fixes "timing jitter"), sandormatyi, BenZonneveld. |
| **naokiiwakami/tr909-clone-firmware** | https://github.com/naokiiwakami/tr909-clone-firmware | GPL-3.0 | AVR firmware for an analog BD/SD/RS/HC + PCM-hat clone | Embeds the **original hat ROM** (`hi_hat_wav.hpp`, bytes identical to c43). **OH plays 0x0000–0x603F, CH plays 0x6040–0x7FFF.** `read_wav.py` masks the "dirty two LSBs". The PCM clock is variable (Timer2), giving about 11–40 kHz sample updates, so it is not authoritative. Noise LFSR clocked at about 250 kHz. |
| **TylerK07/AVR909-HiHat** | https://github.com/TylerK07/AVR909-HiHat | GPL-3.0 | AVR player of the original hat ROM | Source WAV = the raw c43 ROM as 8-bit 32 kHz (identical bytes). Packs 6 bits/sample (masks `0xFC`). Segment ends at 18444 and 24576 packed bytes, i.e. **OH = samples 0–24591, CH = 24592–32767**. Plays at 20 MHz / 625 = **32 kHz**. |
| **ryandorward/tr-909-noise-source** | https://github.com/ryandorward/tr-909-noise-source | MIT (DSP) | 909 noise LFSR as a plug-in | **31-bit LFSR, x³¹ + x¹³ + 1, clocked at about 300 kHz**, start-up forces the input high for 7500 clocks (about 25 ms); 16-tap windowed-sinc decimation. From Electric Druid's analysis [D-46]. |
| **andremichelle/tr-909** | https://github.com/andremichelle/tr-909 | MIT | web emulation; recorded one cycle of the BD and an attack transient from a DinSync RE-909, plus whole-hit samples for the other voices | BD: one-cycle wavetable swept **274 Hz → 53 Hz** exponentially, with τ_pitch = 7–29 ms via TUNE (exp mapping); amplitude τ = 12–100 ms after a **60 ms** hold. Tom decay τ 40–150 ms; SD noise τ 40–200 ms; OH τ 30–160 ms; CH τ 8–60 ms. Tempo 37–290. **Shuffle delay = index/32** (the author says this replaced KVR's "2/96"). **Flam = 10 + 4·i ms** (from [D-33]). Scale ratios 3/16, 3/32, 1/32, 1/16. |
| **Mutable Instruments Plaits / Peaks** | https://github.com/pichenettes/eurorack | MIT (STM32 code) | generic drum models | `synthetic_bass_drum.h` "Inadvertently 909-ish": triangle → 2x/(1+\|x\|) sine shaper, FM pitch envelope 8 ms (×1–5), 1 ms body pulse, 5 ms click, transistor-VCA soft clip. `synthetic_snare_drum.h` uses "magic numbers taken from the 909 schematics": **mode ratio 1.47**, "Q40's collector resetting all oscillators allows some intermodulation", noise HP at 10·f0 and LP at 35·f0, snare hold 40–70 ms. Peaks and the Plaits analog models are 808-based. |
| **matthewcieplak/er-99** (Extralife ER-99, 2022) | https://github.com/matthewcieplak/er-99 | GPL-3.0 | WebAudio synthesis + cymbal samples | BD 80 Hz, pitch env ×2.5 over 50 ms, decay 300 ms; SD 220 Hz; toms 100/200/300 Hz; **RS = 3 bandpasses at 220/500/950 Hz, Q 10.5**; clap as 5 delayed noise bursts. Fitted by ear. |
| SauersML/patina `docs/tr909-circuit-reference.md` | https://github.com/SauersML/patina | none | third-party text extraction of the service notes (looks machine-assisted) | Useful cross-check for A1/A2 but **unverified**: it calls the BD a "bridged-T resonator", which conflicts with the service notes' VCO description. Derived RS bandpass centres 219/496/1054 Hz. Lists serial-number changes: S/N 381500 BD C9 0.22→0.33 µ; 415300 RS R417 12 k→3.3 k, hat C134 10 µ→0.01 µ, 0.01 µ shunts on the multi-outs; 426700 tom noise C54 2.2 n→4.7 n, R194 47 k→100 k. |
| reduzent/netpd-instruments `tr909*.pd` | https://github.com/reduzent/netpd-instruments | none | Pure Data, samples embedded as arrays | — |
| SVModular/DrumKit "Bass Drum 9" | https://github.com/SVModular/DrumKit | other | VCV Rack, 16 BD samples | — |
| Slider2732/ESP-909 (Jan Ostman's code) | https://github.com/Slider2732/ESP-909 | GPL-3.0 | ESP8266 sample player, 44.1 kHz | — |
| Others (web toys) | sonicarchetype/TR909, CarlosFranzetti/LoS.9x9 ("6-bit quantization" synthesis), L-A-Marchetti/nuovo, jp206100/909-Kick-Builder-VST, AngelAriasGlez/TR909 | various | sample- or ad-hoc synthesis | none useful |
| **Datasets** (for forks E/F) | patchbanks/WaivOps-EDM-TR9 (TR-909 rhythm dataset); Gearspace thread "Free Open Source TR-909 and TR-808 Audio Datasets" | — | — | — |

I found no Faust, SuperCollider, Csound or VCV module that models the 909 *circuit*. MAME has no
909 sound.

### D.7 What the emulators say about measurements

**Flam.** EchoBoy (e-licktronic forum, 22 Jul 2017) scoped the TRIG line of a real 909 [D-33]:

- flam 9–16 → **10, 14, 18, 22, 26, 30, 34, 38 ms** between the two triggers;
- the minimum observed was 4 ms lower (6…34 ms) "due to the jitter of the sequencer";
- "the timing of the flam is not affected by the tempo".

Nava's firmware cites EchoBoy but uses 20–48 ms. Roland Cloud also uses 20–48 ms. So either the
firmware's flam set differs from what EchoBoy measured, or those products are off by 10 ms. Run the
v4 firmware and measure.

**Shuffle.**

- Nava: 1…6 ticks of 96 PPQN on the off-beat 16ths. At most 6/24 of a 16th, i.e. a 50 → 62.5 % swing.
- andremichelle: index/32 (unit as in his code, "from my hearing").
- RD-9: a continuous percentage.
- Roland Cloud: SHUFFLE 0–255 × a per-instrument amount.
- None of these is an authoritative measurement. The firmware is the authority.

**Sequencer jitter.** EchoBoy saw about 4 ms of trigger jitter, and Modularsoundsystems' Nava
firmware fixes "timing jitter". This matches the known 909 MIDI/sync timing looseness; fork C covers
it.

**Hats.**

- The original is said to run the hat counter at "about 60 kHz ÷ 2" (service notes, fork B).
- Clones use 32 kHz (Fraser, AVR909) or an arbitrary variable rate.
- Search results also cite "around 60 kHz at stock tuning judging from spectral analysis" (Gearspace,
  page blocked).

**Kick.**

- andremichelle's by-ear fit of an RE-909: 274 → 53 Hz.
- maffez: R59 sets the base pitch.
- Hexinverter Mutant BD9: about 30–240 Hz range (not a 909 measurement).

**Unit-to-unit spread.** Roland [D-18][D-19], SOS [D-12][D-17] and maffez [D-24] all stress large
variation between 909s and between revisions.

### D.8 Lessons for our model

1. **Use the real firmware as the timing authority.** Validate it against EchoBoy's flam
   measurement (10–38 ms, ±4 ms jitter) and look for the shuffle-tick table in the v4 disassembly
   (Jacquot's marked-up listing is a head start).
2. **Model one shared noise generator** (31-bit LFSR, x³¹+x¹³+1, about 300 kHz, start-up fill) that
   feeds SD, HC and the toms. Reviewers single out the snare+clap phasing as a 909 signature
   [D-12][D-15][D-34].
3. **Keep the original control set and ranges by default.** Extras the commercial clones add (BD
   TUNE/PITCH DEPTH, hat/RS tune, separate CH/OH levels, compressors) can be optional "mod"
   parameters. Name them as Roland Cloud or TR-09 do and use the same CC numbers where it costs
   nothing:
   - CC 20/21/23/24 for BD; 25/26/28/29 for SD; 46–54 for toms; 57 RS; 60 HC; 62/63 CH; 81/82 OH;
     83/85 CC; 86/88 RC; 71 total accent; 9 shuffle.
4. **MIDI note map:** use the original's 35/36 BD, 37 RS, 38/40 SD, 39 HC, 41/43 LT, 42/44 CH,
   45/47 MT, 46 OH, 48/50 HT, 49 CC, 51 RC. It is what the firmware does anyway, and the Roland ACB
   products and Roland Cloud all agree.
5. **Revision switch.** Provide early and late voicing-board variants, following the serial-number
   changes in the service notes (A1/A2), because Roland says later units sound "drastically
   different".
6. **Cross-check the transcription.** Diff our service-notes transcription against the Nava netlist.
   It keeps the designators and is machine-readable. Then derive SPICE decks per voice and A/B them
   against our model.
7. **Hat ROM.** Decide from the circuit (counter-stop logic and diode-OR start addresses) whether
   OH and CH are two regions or one region with different start and stop addresses. Also decide
   whether the hat DAC sees the two LSBs that are populated in the 0x3FC1–0x5FBA region. The
   hobby clones assume two regions and mask the LSBs (fork B).
8. **Oversample enough** that the "grating top end" reported on ACB products does not appear. 6-bit
   playback at about 30 kHz plus analog filtering should be done at an internal rate that is a
   multiple of the real sample clock, or band-limited properly.
9. **Possible validation data:** Wave Alchemy Revolution's knob-position multisamples (paid);
   Obie69's 909 vs RD-9 recordings (cited by maffez; SoundCloud set uibkmedan/rd9_modifications);
   and fork F's shootouts.

### D.9 Open questions

- Does Roland ACB (TR-8, TR-8S, TR-09, Cloud) play the original ROM data for HH, CC and RC? At
  which clock rate, and through which filter model? Are "909 Bass1/Bass2" and "Snare1/Snare2" the
  TR-8 and TR-09 models, or different hardware revisions?
- Behringer RD-9: where does its sample data come from, and at what rate and bit depth? What is its
  real default MIDI map? (The manual table is copied from the RD-8.)
- The flam discrepancy (10–38 ms measured vs 20–48 ms in Nava and Roland Cloud), and the real
  shuffle tick amounts.
- Velocity. SOS's 1994 retrospective says the 909 plays "with full velocity response" over MIDI;
  Jacquot guesses "velocity mapped in the 64–96 range?". How does MIDI velocity map onto the
  per-instrument accent DACs (MAME's DAC1–8)? Fork C.
- Patina's extraction is unverified. Its BD-as-bridged-T description in particular must not be used
  without checking the schematic.

### D.10 Sources

Local paths are relative to `docs/papers/emulations/`.

- [D-1] Roland, *TR-909 Software Rhythm Composer Owner's Manual* (©2018). Installed copy
  `/Library/Audio/Plug-Ins/VST3/Roland/TR-909.vst3/Contents/Resources/Manual/TR-909_Manual_E.pdf`;
  online https://www.rolandcloud.com/getmedia/38e821fb-17fd-4b17-95ca-04cd728fccde/TR-909-Manual-E.pdf?ext=.pdf.
  Local: `roland_cloud_TR-909_Manual_E.{pdf,txt}`.
- [D-2] Read-only inspection of the Roland Cloud TR-909 VST3 bundle (v1.1.3): `Script/Script.xml`,
  `ScriptResource.xml`, `ScriptViewCommon.xml`, `strings` of the binary. Local:
  `roland_cloud_TR-909_bundle_notes.txt`.
- [D-3] Roland TR-09 Owner's Manual. https://static.roland.com/assets/media/pdf/TR-09_eng03_W.pdf.
  Local: `roland_TR-09_eng03_W.{pdf,txt}`.
- [D-4] Roland TR-09 MIDI Implementation Chart v1.00 (30 Aug 2016).
  https://static.roland.com/assets/media/pdf/TR-09_MIDI_Imple_e01_W.pdf. Local:
  `roland_TR-09_MIDI_Imple_e01_W.{pdf,txt}`.
- [D-5] Roland TR-8 Owner's Manual. https://static.roland.com/assets/media/pdf/TR-8_OM.pdf. Local:
  `roland_TR-8_OM.{pdf,txt}`.
- [D-6] Roland TR-8 MIDI Implementation Chart v1.11 (18 Nov 2014).
  https://cdn.roland.com/assets/media/pdf/TR-8_MIDI_Imple_Chart_e02_W.pdf. Local:
  `roland_TR-8_MIDI_Imple_Chart_e02_W.{pdf,txt}`.
- [D-7] Roland TR-8S Owner's Manual. https://static.roland.com/assets/media/pdf/TR-8S_eng03_W.pdf.
  Local: `roland_TR-8S_eng03_W.{pdf,txt}`.
- [D-8] Roland TR-8S Reference Manual.
  https://static.roland.com/assets/media/pdf/TR-8S_Reference_eng03_W.pdf. Local:
  `roland_TR-8S_Reference_eng03_W.{pdf,txt}`.
- [D-9] Roland TR-8S MIDI Implementation Chart v1.02 (20 Feb 2018).
  https://static.roland.com/assets/media/pdf/TR-8S_MIDIImpleChart_eng01_W.pdf. Local:
  `roland_TR-8S_MIDIImpleChart_eng01_W.{pdf,txt}`.
- [D-10] Roland Australia, "What is Analog Circuit Behavior (ACB)?"
  https://rolandcorp.com.au/blog/what-is-analog-circuit-behavior-acb. Local:
  `web/rolandau_what_is_acb.{html,txt}`.
- [D-11] Matrixsynth, "New Roland AIRA Details – ACB" (Feb 2014).
  https://www.matrixsynth.com/2014/02/new-roland-aira-details.html. Local:
  `web/matrixsynth_aira_acb_details.*`.
- [D-12] P. Nagle, "Roland TR8 Rhythm Performer", *Sound On Sound*.
  https://www.soundonsound.com/reviews/roland-tr8-rhythm-performer. Local: `web/sos_tr8_review.*`.
- [D-13] MusicRadar, Roland Aira TR-8 review.
  https://www.musicradar.com/reviews/tech/roland-aira-tr-8-594315. Local:
  `web/musicradar_tr8_review.*`.
- [D-14] *Sound On Sound*, Roland TR-8S review. https://www.soundonsound.com/reviews/roland-tr-8s.
  Local: `web/sos_tr8s_review.*`.
- [D-15] P. Nagle, "Roland Boutique TR-09 & TB-03", *Sound On Sound*, June 2017.
  https://www.soundonsound.com/reviews/roland-boutique-tr-09-tb-03. Local:
  `web/sos_tr09_tb03_review.*`.
- [D-16] Resident Advisor, TR-09 review. https://ra.co/reviews/20120. Local:
  `web/ra_tr09_review.*`.
- [D-17] *Sound On Sound*, Roland TR-1000 review. https://www.soundonsound.com/reviews/roland-tr-1000.
  Local: `web/sos_tr1000_review.*`.
- [D-18] Attack Magazine, "Drop The Boom: The Story Of The Roland TR-1000" (Daichi and Peter Brown
  quotes). https://www.attackmagazine.com/features/long-read/drop-the-boom-the-story-of-the-roland-tr-1000/.
  Local: `web/attackmag_tr1000_story.*`.
- [D-19] Perfect Circuit, "Freak the Sound: Talking TR-1000 with Peter Brown" (via Wayback).
  https://www.perfectcircuit.com/signal/roland-tr-1000-interview. Local:
  `web/perfectcircuit_tr1000_peter_brown_interview.*`.
- [D-20] K. Tsuboi, "Roland Engineering: Atsushi Hoshiai and the TR-909", Roland Articles,
  5 Sep 2020. https://articles.roland.com/atsushi-hoshiai-tr-909/. Local:
  `web/roland_articles_hoshiai_tr909.*`.
- [D-21] Attack Magazine, "Atsushi Hoshiai: The Man Behind The TR-909".
  https://www.attackmagazine.com/features/interview/atsushi-hoshiai-the-man-behind-the-tr-909/.
  Local: `web/attackmag_hoshiai_interview.*`.
- [D-22] Roland, "A Behind-the-Scenes Interview with the Developers" (Boutique).
  https://www.roland.com/global/promos/roland_boutique/interview_1/ (and /de/). Local:
  `web/roland_boutique_interview_1{,_en}.*`.
- [D-23] Behringer, *RD-9 Rhythm Designer User Manual*.
  https://gzhls.at/blob/ldb/5/c/d/a/f45872d020af698f563961b0c91d8a12ccca.pdf. Local:
  `behringer_RD-9_user_manual.{pdf,txt}`.
- [D-24] maffez, *Behringer RD9 Modifications* V2, 27 Jul 2021 (declared open source).
  https://maffez.com/wp-content/uploads/2021/07/RD9_MODIFICATIONS_V2.pdf. Local:
  `maffez_RD9_MODIFICATIONS_V2.{pdf,txt}`.
- [D-25] Just Hardware Sound, RD-9 review.
  https://www.justhardwaresound.com/reviews/behringer/rd-9-review. Local:
  `web/justhardwaresound_rd9_review.*`.
- [D-26] Tiptop Audio 909 series https://tiptopaudio.com/909-2/; ModularGrid HATS909 and BD909 pages.
  Local: `web/tiptop_909_series.*`, `web/modulargrid_tiptop_{hats909,bd909}.*`.
- [D-27] Erica Synths Hi-Hats D, Cymbals and Bass Drum2 pages
  (https://www.ericasynths.lv/shop/eurorack-modules/by-series/drum-series/…). Local:
  `web/erica_{hihats_d,cymbals,bass_drum2}.*`.
- [D-28] Gearnews, "909 Day: … Emulations and Clones". https://www.gearnews.com/909-day-synth/.
  Local: `web/gearnews_909day_emulations_clones.*`.
- [D-29] Sonic State, "Replica 909" (DinSync RE-909, 8 Jun 2020).
  https://sonicstate.com/news/2020/06/08/replica-909/. Local:
  `web/sonicstate_dinsync_re909_replica.*`.
- [D-30] C. Fraser, "808/909 Clone". http://www.colinfraser.com/tr909/my909.htm. Local:
  `web/colinfraser_my909_clone.*`.
- [D-31] *Sound On Sound*, Jomox XBase 09 review. https://www.soundonsound.com/reviews/jomox-xbase-09.
  Local: `web/sos_jomox_xbase09_review.*`.
- [D-32] e-licktronic, Nava v1.0 (Eagle schematic and board, firmware v1.028beta).
  https://github.com/e-licktronic/Nava-v1.0. Local: `code/e-licktronic_Nava-v1.0/`, plus extracted
  `Eagle/Nava_v1.02_parts.csv` and `Eagle/Nava_v1.02_netlist.txt`.
- [D-33] EchoBoy, "Flam timings", e-licktronic forum, 22 Jul 2017.
  http://www.e-licktronic.com/forum/viewtopic.php?f=25&t=1430. Local:
  `web/elicktronic_forum_flam_t1430.*`.
- [D-34] eMusician review of D16 Drumazon (reposted by Plugin Boutique).
  https://www.pluginboutique.com/articles/41. Local: `web/emusician_drumazon_review.*`.
- [D-35] D16 Group, Drumazon 2 product page. https://d16.pl/drumazon2. Local:
  `web/d16_drumazon2_product.*`.
- [D-36] MusicTech, "D16's Drumazon 2…". https://musictech.com/news/gear/d16-group-drumazon-2/.
  Local: `web/musictech_drumazon2_news.*`.
- [D-37] AudioRealism, "ADM1x available now" (10 Oct 2022).
  https://news.audiorealism.se/2022/10/10/audiorealism-drum-machine-adm1x-available-now/. Local:
  `web/audiorealism_adm1x_news.*`.
- [D-38] MusicRadar, AudioRealism ADM review.
  https://www.musicradar.com/reviews/tech/audio-realism-adm-121891. Local:
  `web/musicradar_adm_review.*`.
- [D-39] Wikipedia, "Novation DrumStation" and "Roland TR-909". Local:
  `web/wikipedia_novation_drumstation.*`, `web/wikipedia_tr909.*`.
- [D-40] Internet Tattoo, "Best TR-909 emulations" roundup. Local:
  `web/internettattoo_best_909_emulations.*`.
- [D-41] A. Michelle, tr-909 (MIT). https://github.com/andremichelle/tr-909. Local:
  `code/andremichelle_tr-909/` (TypeScript DSP and memory files).
- [D-42] E. Gillet, Mutable Instruments eurorack (Plaits, Peaks; MIT).
  https://github.com/pichenettes/eurorack. Local: `code/pichenettes_eurorack_drums/`.
- [D-43] M. Cieplak, ER-99 (GPL-3.0). https://github.com/matthewcieplak/er-99. Local:
  `code/matthewcieplak_er-99/`.
- [D-44] N. Iwakami, tr909-clone-firmware (GPL-3.0).
  https://github.com/naokiiwakami/tr909-clone-firmware. Local:
  `code/naokiiwakami_tr909-clone-firmware/`.
- [D-45] TylerK07, AVR909-HiHat (GPL-3.0). https://github.com/TylerK07/AVR909-HiHat. Local:
  `code/TylerK07_AVR909-HiHat/` (the ROM-copy WAV was not kept).
- [D-46] R. Dorward, tr-909-noise-source (MIT), https://github.com/ryandorward/tr-909-noise-source;
  and Electric Druid, "A look at the TR-909's noise generator",
  https://electricdruid.net/tr-909-noise-generator/. Local: `code/ryandorward_tr-909-noise-source/`,
  `web/electricdruid_tr909_noise_generator.*`.
- [D-47] Jacquot-SFE, tr-909-analysis (no licence). https://github.com/Jacquot-SFE/tr-909-analysis.
  Local: `code/Jacquot-SFE_tr-909-analysis/`.
- [D-48] SauersML, patina, `docs/tr909-circuit-reference.md` (no licence).
  https://github.com/SauersML/patina. Local: `code/SauersML_patina/tr909-circuit-reference.md`.
- [D-49] reduzent, netpd-instruments (`tr909*.pd`). https://github.com/reduzent/netpd-instruments.
  Not copied.
- [D-50] SVModular, DrumKit (VCV Rack). https://github.com/SVModular/DrumKit. Not copied.
- [D-51] MAME `src/mame/roland/roland_tr909.cpp` (in `reference/mame/`); A. Grazioli,
  VintageEmulatorStudio (AGPL-3.0), https://github.com/antoniograzioli/VintageEmulatorStudio (its
  tr909 file is unmodified).
- [D-52] Attack Magazine, Wave Alchemy Revolution review.
  https://www.attackmagazine.com/reviews/gear-software/wave-alchemy-revolution/. Local:
  `web/attackmag_wave_alchemy_revolution_review.*`.
- [D-53] ModularGrid, Hexinverter Mutant BD9.
  https://modulargrid.net/e/hexinverter-Electronique-mutant-bd9. Local:
  `web/modulargrid_hexinverter_mutant_bd9.*`.
- [D-54] *Sound On Sound*, "Roland TR909" retrospective. https://www.soundonsound.com/reviews/roland-tr909.
  Local: `web/sos_tr909_1984_review.*`.
- [D-55] Michael Z Freeman, ACB blog post (mostly quotes Werner's 808 papers). Local:
  `web/michaelzfreeman_acb_evidence.*`.

Gearspace and KVR threads could not be fetched (Cloudflare 403, and not in the Wayback Machine).
Their claims are quoted only from search-engine snippets and are marked as such.

---

## 7. Reference audio

The full index is `reference/real909/index.md` (with `index.json`). Summary:

- **Packs** (`reference/real909/packs/`, about 560 MB, all lossless WAV): 1,387 single hits from four free sources.
  - **AudioRealism "TR-909 Ultimate"** (848 clips; 96 kHz/24-bit mono; individual outs, LEVEL 50 %, not normalised). This is the main measurement source. Knob positions are in the file names:
    - BD TUNE×ATTACK×DECAY and SD TUNE×TONE×SNAPPY on a 0/25/50/75/100 % grid;
    - each tom TUNE×DECAY in 10 % steps (11×11);
    - crash and ride TUNE in 10 % steps, CH DECAY at 5 positions;
    - 11 accent levels per instrument (probably the TOTAL ACCENT knob on an accented step);
    - flam 1–8, RS shuffle 1–7 at 112 BPM, SD+CP and OH/CH choke from the master out.
    - The licence forbids redistribution, so it is for internal reference only.
  - **Rob Roy 1995** (160 clips; 44.1 kHz/16-bit; a second unit, early revision; dial 0/3/7/10 on the drums and 0–10 in steps of 2 on hats and cymbals). Normalised, tails trimmed.
  - archive.org KB6-era sets (314 clips; no settings) and Echo Sound Works "TR909 Day" (65 clips; probably processed). Low weight.
- **Videos and web audio** (`reference/real909/videos/`, 41 entries, about 4.7 GB including the original streams): lossy YouTube/SoundCloud audio, with chapters, descriptions and captions kept. The most useful:
  - Regebro's SoundCloud set: all 11 sounds from a rev-4 909 and an RD-9, individual and main outs, 4 accent states each. Cut into 176 hits.
  - sikdelphi303: an original (SN 360312) and a revised (SN 438526) 909 compared, same settings and chain.
  - funkyberlin303: RD-9 against a 909, line out; BD TUNE min/max chapters cut.
  - Starsky Carr: 909 vs RD-9 vs TR-09, with shuffle, flam and TOTAL ACCENT sections; and TR-1000 vs 909, individual against main out.
  - Charterhouse: 909 vs TR-8S vs MC-707, per instrument, no talking.
  - Wine&Synths: RD-9 vs 909 with every knob at min, middle and max.
  - BB Squelch: 909 demo.
  - Bonedo: lossless WAVs of a TR-09 against a 909.
  - The factory song (v1 only).
  - Flam and shuffle tutorials.
  - A&B comparisons against the TR-8, TR-8S, TR-09, RD-9, TR-1000, RE-909, SR-909, 9090 and Drumazon/ADM/Roland Cloud.
- **Decoded ROMs** (`reference/rom_decoded/`): open and closed hat at 31 570 Hz; crash and ride at TUNE min, centre and max; raw copies; figures and the scripts that reproduce the rate, envelope and transfer-function measurements.

**Measurements made on this material** (details in sections 2–4; scripts are next to the data):

- **BD**:
  - f∞ is 49.2–49.7 Hz on two units, 52.5 Hz on a third.
  - Sweep τ is 7.5 to 29 ms over TUNE, extrapolating to f(0) ≈ 275–300 Hz.
  - Time to −20 dB is 73 to 236 ms over DECAY.
- **SD**: VCO-1 116/231 Hz and VCO-2 177/346 Hz at TUNE min/max. TONE has no effect when SNAPPY = 0. Snappy −20 dB at 86–220 ms.
- **Toms**: LT 62→123 Hz, MT 79→148 Hz, HT 91→175 Hz, with a second tone at ÷1.48.
- **RS**: 223/478/1014 Hz, with the 1 kHz partial about 10 dB weaker on early units.
- **HC**: hits at 0/10.5/22.3/32.5 ms; tail τ 82–99 ms.
- **Hat/cymbal sample clocks**, found by correlating against the ROM data: HH 31.57 kHz; crash 28.06→53.8 kHz; ride 28.07→56.1 kHz.
- **Hat VCA**: τ_OH 77–202 ms and τ_CH 10–51 ms (Rob Roy); cymbal VCA law ((63 − n>>9)/63)^1.10.
- **Accent**:
  - The TOTAL ACCENT sweep spans +6–8 dB (RS/HC +4–5 dB), with dead zones at 0–10 % and 90–100 %.
  - Regebro's four states, BD: TA only +3.1 dB, step accent only +7.7 dB, both +10.4 dB.
- **Main out against individual out** (open hat): −3.9 dB at 100 Hz and −8.9 dB at 20 kHz. The main out is polarity-inverted.

**Caveats:**

- The unit, firmware and calibration behind the packs are unknown. The AudioRealism unit is a late revision (HH high-pass at 723 Hz) and the Rob Roy unit an early one.
- The AudioRealism tom tails contain 50 Hz hum harmonics at about −60 dB.
- All YouTube audio is lossy, and it is the uploader's mix.
- **Still missing:**
  - a true stereo main-out recording of solo LT/HT/HH/ride, to settle the pan orientation;
  - long-held multi-hit recordings, for hit-to-hit variation;
  - a scope trace of the TRIG lines, for flam and the scan tick.

---

## 8. Table of main constants

"calc" = derived from schematic values; "meas." = measured on real-909 recordings (AR = the AudioRealism unit, RR = the Rob Roy unit). Firmware numbers come from the disassembly and are listed in full in section 5 (C.11).

| block | quantity | value | basis | section |
|---|---|---|---|---|
| global | op-amp rails / swing | ±15 V; M5218 swing ≈ ±13.5 V | SN | 3 (A2.6) |
| global | accent DAC | V = 5·code/63, LSB 79 mV, R_th 2.54 kΩ; per-voice buffer ×3.13 | calc | 3 (A2.2) |
| global | TRIG | +5 V, 1 scan tick (≈1.92–2.11 ms) | SN + disasm | 5 (C.4) |
| noise | LFSR | x³¹ + x¹³ + 1 (36 stages, taps 13/31), period 2³¹−1 | calc (Nava netlist agrees) | 3 (A2.1) |
| noise | clock | ≈455 kHz (calc), ≈300 kHz (Electric Druid) | calc / web | 3 (A2.1) |
| BD | VCO law | f = \|V_A\|/(4·V_th·R59·C14) = 37.3 Hz/V; V_th ≈ ±4.3 V | calc | 2 |
| BD | resting pitch f∞ | 56 Hz calc; **49.2–49.7 Hz** meas. (AR, RR); 52.5 Hz (third unit) | calc / meas. | 2 |
| BD | start pitch | 261–274 Hz calc; ≈275–300 Hz meas. (extrapolated); first full cycle 148 Hz (TUNE 0) … 239 Hz (TUNE 100) | | 2 |
| BD | sweep τ vs TUNE 0/25/50/75/100 % | **7.4 / 9.4 / 11.6 / 14.3 / 29.1 ms** meas.; 6.8–29.4 ms calc (C9 0.33 µ); 4.5–19.6 ms with C9 0.22 µ (SN < 381500) | | 2 |
| BD | body envelope | hold ≈60–68 ms meas. (47 ms calc), then τ 14–107 ms calc over DECAY; −20 dB at 73/75/114/155/236 ms meas. | | 2 |
| BD | click | 2 ms pulse → HP 241 Hz → Sallen–Key 5.3 kHz, Q 3.4; noise 4.22 Vpp → LP 339 Hz; ENV2 τ 0.73 ms charge / 15.5 ms decay | calc | 2 |
| BD | accent | body ×2.13 (+6.6 dB), saturates ≥ 90 % (D8 clamp ≈2.6 V) | meas. / calc | 2 |
| SD | VCO-1 / VCO-2 vs TUNE 0…100 % | **117/145/174/201/231 Hz** and **176/217/260/301/346 Hz** meas. (AR); calc ≈18 % lower | meas. | 2 |
| SD | VCO-2/VCO-1 | 1.47 calc; 1.500 (2 units), 1.44–1.455 (2 units) | | 2 |
| SD | pitch bend | ≈×2.1–2.5 at onset, τ ≈5.8 ms, gone by ≈20 ms | calc / SN | 2 |
| SD | snappy | noise LP 8.5 kHz Q 1.9; path A HP 1.5 kHz, hold 24 ms then τ 32–76 ms toward −0.1·V_acc; path B HP 4.0 kHz Q 1.3; −20 dB 82–202 ms over TONE (meas.) | calc / meas. | 2 |
| toms | VCO-1 range (calc) | LT 61–122, MT 75–149, HT 90–179 Hz (meas. LT 59–119, MT 74–143, HT 85–169) | calc / meas. | 2 |
| toms | VCO ratio | VCO-1 : VCO-2 : VCO-3 = 1.5 : 1 : ≈2.7 | calc | 2 |
| toms | pitch envelope | V_SS only; τ 22 ms + 220 ms; start ×1.35–1.52 | calc (meas. 25–35 % above) | 2 |
| toms | DECAY (VCO-2 only) | τ 26–261 ms (MT, HT), 38–378 ms (LT); rise ≈47–68 ms | calc | 2 |
| RS | resonators | 495 / 219 / 1053 Hz, Q 7.3 / 6.1 / 7.3, τ 4.7 / 8.9 / 2.2 ms calc; meas. 478 / 223 / 1014 Hz | | 3 (A2.3) |
| RS | chain | ±0.6 V diode clip; VCA τ ≈47 ms; Sallen–Key HP 229 Hz, Q 1.58, ×2 | calc | 3 |
| HC | noise band-pass | 963 Hz, Q 2.13 (meas. peak 940–1030 Hz) | calc / meas. | 3 (A2.4) |
| HC | hits | window 31.7 ms calc; hits at 0 / 10.5 / 22.3 / 32.5 ms meas. (C65 ≈0.40 µF reproduces this) | | 3 |
| HC | tail | reverb path BP ≈78–440 Hz, τ ≈123 ms calc; meas. τ 82–99 ms | | 3 |
| HH | sample clock | **31.57 kHz** (two units); oscillator ≈63.1 kHz ÷ 2 (R492 12 k, C160 470 pF) | meas. | 4 |
| CR/RD | sample clock vs TUNE 0/50/100 % | crash **28.06 / 36.68 / 53.84 kHz**; ride **28.07 / 36.83 / 56.11 kHz** (AR); T_s ≈ 35.6 µs − 17.1 µs·x | meas. | 4 |
| HH | ROM regions | OH 0x0000–0x5FFF (24 576 samples, 778 ms); CH 0x6000–0x7FFF (8 192 samples, 259 ms; first 65 samples silent) | ROM + SN | 4 |
| HH | envelope | 2 ms hold, then exponential; τ_OH = C135·((100 k + VR23) ∥ 220 k) = 69–183 ms calc, 77–202 ms meas.; τ_CH 8.7–69 ms calc, 10–51 ms meas. | | 4 |
| CR/RD | envelope | g = ((63 − ⌊n/512⌋)/63)^1.10, independent of TUNE | meas. | 4 |
| HH/CR/RD | accent | exponential: 6.4 dB/V (HH), 9.0 dB/V (cymbals); TA sweep +7.0 to +8.2 dB | calc / meas. | 4 |
| HH/CR/RD | reconstruction LPF | 25.9 kHz Q 0.548 + 27.7 kHz Q 1.316 (≈4th-order Butterworth at 26.8 kHz) | calc | 4 |
| HH | level-stage HP | 0.72 Hz (C134 10 µ) → **723 Hz** (0.01 µ, SN ≥ 415300) | calc | 4 |
| outputs | individual out | 1 kΩ series + 10 nF (SN ≥ 415300) → 15.9 kHz pole | calc | 3 (A2.5) |
| outputs | stereo mix | bus HP ≈130 Hz (1 µF into ≈1.2 kΩ); main-out LP ≈6.6 kHz; inverted relative to the individual outs; master 6 Vp-p, 1 kΩ | calc / meas. | 3 |
| outputs | pan | BD, SD, MT, RS, CP centre; LT and ride one side, HT and HH the other, all 5.2 dB (8.2 k/15 k); crash 1.6 dB (12 k/10 k) | calc | 3 |
| firmware | sequencer | 48 ppqn; tempo P = ⌊1 099 138/B⌋ µs, B = 32 + ⌊225·ADC/256⌋ | disasm | 5 |
| firmware | shuffle | 12+s / 12−s ticks, s = 0…6 (keys 1–7) → 50, 54.2, 58.3, 62.5, 66.7, 70.8, 75 % | disasm (matches video) | 5 |
| firmware | flam | (f+4 or f+5) group ticks ≈15–51 ms (disasm); 10–38 ms (scope, EchoBoy); 20–48 ms (Nava, Roland Cloud) | conflicting | 5 |
| firmware | accent codes | analog normal 10 → 21, accent 21 → 47 (TA 0 → 31); CH 25/40 → 40/55; OH/CR/RD 32 → 47; RS/HC 21 → 47 | disasm | 5 |
| firmware | MIDI | rx ch 10 omni, notes 35/36 BD, 37 RS, 38/40 SD, 39 HC, 41/43 LT, 42/44 CH, 45/47 MT, 46 OH, 48/50 HT, 49 CR, 51 RD; velocity → DAC code via tables; note-off ignored; tx ch 11, Ext-instrument rows only, notes 36–51, velocity 64 or 64 + TA | SN p.14–16 + disasm | 5 |
| firmware | sync | DIN 24 ppqn (pin 3 clock, 1 run/stop, 5 continue), doubled to 48; MIDI F8 sent continuously; tape FSK 1201.9/2403.8 Hz toggling per tick | SN + disasm | 5 |

---

## 9. Hardware revisions (serial-number changes)

From the Service Notes p.7 "Change information" (and annotations on p.11), cross-checked against the Nava netlist (late revision) and against recordings. "Rev 2/3/4" are the names used by owners (Regebro); they are hardware revisions, not firmware versions.

| from SN | part(s) | change | audible effect | owners' name |
|---|---|---|---|---|
| up to 371499 | R495 150 k (BD attack, surface-mounted); clap parts R496/R215, C61/D53 marked "surface mounted" | extra parts on early boards | shorter BD click envelope (my reading) | — |
| 370600 | battery lid | rubber → metal | — | — |
| 381500 | **BD C9** | 0.22 µF → **0.33 µF** | pitch-sweep τ ×1.5 longer ("expanding the TUNE range") | rev 2 |
| 381500 | tape-sync filter | C87 deleted, R306 4.7 k → 47 k, R312 470 k → 4.7 M | — | — |
| 392600 | crash/ride oscillator R442/R440 | 33 k → 10 k | TUNE range (early range unmeasured) | — |
| 393000 | reset zener D701 | RD6.8JB2 → RD5.6JB2 | brown-out behaviour | — |
| 360100–393899 / 403900 on | firmware ROM | unlabelled 2764 ("Revision 0") / "Ver. 1" 2764 or 2364 mask ROM, same program | — | — |
| 415300 | **RS R417** | 12 k → **3.3 k** | stronger 1 kHz partial (≈+10 dB) | rev 3 |
| 415300 | **HH C134** | 10 µF → **0.01 µF** | hi-hat high-pass 0.72 Hz → 723 Hz | rev 3 |
| 415300 | multi-out C500–C512 | 0.01 µF added at each jack | 15.9 kHz pole on individual outs; ≈6.6 kHz main-out LP | rev 3 |
| 415300 | RS TRIG OUT | R495 1 k, R392/R393 → 1 M, C600 0.1 µF | stops the RS trigger leaking into tape sync | — |
| 426700 | **tom noise** C54, R194 | 0.0022 → 0.0047 µF; 47 k → 100 k | stronger tom attack, less steady noise | rev 4 |
| Feb 1985 | firmware | Ver 1.0 → **Ver 4.0** (Service Information 100236): fixes missing an F8 just after Start; removes the factory patterns | MIDI start reliability | — |

Recommendation: expose a "board revision" option (early / SN ≥ 381500 / ≥ 415300 / ≥ 426700) that switches these component values. Default to the latest board, which matches the Nava copy and the AudioRealism unit. Keep the firmware choice (v1 or v4) independent of it.

---

## 10. Open questions and measurements to take

Ordered roughly by impact on the sound.

1. **SPICE checks of the analog voices.** Build netlists from the Nava netlist plus our transcription of p.11. Questions to settle:
   - BD ENV1 hold time: 47 ms calc against 60–68 ms meas.
   - BD f∞ (56 Hz calc against 49.5 Hz meas.), together with the comparator thresholds and V_sat.
   - The slow sag of the BD pitch to ≈48 Hz, and its −41 dB tail ending in a kink at 0.38–0.58 s.
   - The **SD frequencies, 18 % above calc** (perhaps an asymmetric comparator threshold through D62/R275), and why some units lock VCO-2/VCO-1 at exactly 3:2.
   - The tom "envelope on V_SS only" reading.
   - The transfer of the single-transistor "(S)" VCAs: Q6, Q12, Q50/51, Q26–28, Q37, Q65, and the cymbal VCAs with their measured exponent of 1.1.
2. **Unit calibration constants.** Fit per-unit values (V_th or R27 for the BD, the SD thresholds, C65 for the clap, the noise clock, the TM trimmer) to the AudioRealism unit, then check them against Rob Roy.
3. **Noise clock rate**: 300 or 455 kHz. Estimate it from the high-frequency spectrum of a raw noise recording, if one can be found or made.
4. **Pan orientation.** The two boards label L and R inconsistently. A stereo main-out recording of solo LT, HT, HH and ride is needed.
5. **Firmware timing, to verify in the running emulator.**
   - Scan-tick period: 1.92 ms or ≈2.11 ms.
   - Flam intervals: disasm ≈15–51 ms against the 10–38 ms scope reading.
   - The A/B group offset.
   - The accent-table glitch at TA 6–7 (index 43).
   - The power-up write 400A = 3Ch.
   - What happens when CH and OH fall on the same step.
   - The v4 "freezes on DIN sync" report.
6. **Accent mapping against the recordings.** Combine the firmware codes (section 5) with the voice models and check them against the AudioRealism 11-level sweep and Regebro's 4-state hits. This also settles what "accent000–100" means in the AudioRealism pack.
7. **Early-unit values.** The crash/ride TUNE range with R440/R442 = 33 k, and the early BD sweep with C9 = 0.22 µF, on a pre-381500 unit (the sikdelphi303 "original" unit is SN 360312).
8. **Hit-to-hit variation.** The analog VCAs and the noise phase make every hit different; the cymbal clock does not, being gated and reset per hit. Measure on the AudioRealism repeated shots (RS ×10, HC ×8) and shuffle files.
9. **Firmware details for tools rather than sound**: the pattern-memory layout beyond the headers, the SysEx checksum, and v2/v3 images (none public).
