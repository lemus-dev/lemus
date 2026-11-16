# SH-101 emulation research digest

> **Errata (2026-10-08).** This digest came before the part-by-part transcriptions, and some of its
> readings and estimates turned out wrong. Where they disagree, `circuit-osc.md`,
> `circuit-filter.md` and `model.md` win; `circuit-filter.md` §8 lists the filter-side corrections.
> The main ones:
> - **FREQ:** the slider's top is +14 V, not 5 V. FREQ alone spans about 13 octaves at the expo's
>   data-sheet slope, not 4.4–5.1 (§4, §11).
> - **Resonance loop gain** is 5.1, not 4.95: R132 also loads TR26's emitter. Self-oscillation
>   starts at RES ≈ 0.79.
> - **VCA control:** TR28 is a PNP in common base, and S6's ENV contact (not its common) is on
>   R197.
> - **Output:** the OUT jack is coupled through C2 2.2 µF (2.2 Hz into 47k), not C3.
> - **KYBD at 0** still injects a fixed −29 µA through TR42.
> - **ENV depth:** about 10 octaves at ENV 10 on the schematic. The 15–19 octaves in §4 and §11
>   assumed the wrong FREQ/ENV scaling.
> - **Grip LFO fade-in:** about 0.21 s, not 0.47 s or 1.5 s.
> - **Pulse and sub levels re the saw:** about +2.0 and −0.1 dB once the sliders' loading is
>   included, not +4.5 and +2.8 dB.
> - **R169 and R188** (the modulator's loads, §7) are 100k on real boards, not the 47k printed
>   (2026-10-09; `circuit-osc.md` 5.3).

Compiled 2026-10-06 for the component-level Roland SH-101 plug-in (JUCE AU/VST3, C++). It follows
the structure of the TB-303 project's `docs/research.md`.

This digest collects the service documentation, published analyses, clone designs, open-source
models and forum measurements of the SH-101 that bear on a high-accuracy real-time model. It is
organised by circuit block. Each block covers:

- what the circuit does;
- the competing models, with their equations;
- every measured or derived number found, with its source;
- where sources disagree;
- a concrete recommendation for our implementation.

Section 11 tabulates the numeric constants, §12 lists the disagreements and §13 the open questions.
The main A/B reference is Roland's own SH-101 plug-in (ACB); recordings of real units come second
(§10.4).

Conventions:

- **"My calc"** marks numbers I derived; the formula is given inline. The scripts are in the session
  scratchpad (`rsch/scripts/calc{1,2,3}.py`) and are trivial to re-run.
- **"My reading"** marks topology I read off the 1982 service-note scans (S1). I rendered page 9 at
  native resolution (7340 × 3490 px) and read it tile by tile. The numbers and readings are
  transcribed in `docs/papers/sh101/sh101_service_notes_numbers_transcribed.txt`. Check them against
  the netlist being assembled in `docs/schematic/`.
- Designators are Roland's. The main ("synth") board, control board and bender board each number
  their own parts. Bender-board parts are written "bender R15" etc. when they could be confused.
- Single-supply note: the analog circuits run from +15 V (also labelled +14 V), +9 V, −5 V and a
  trimmed **+5 V reference bus**. Roland draws that bus as a short stub ending in a dot. Audio and
  most CVs swing around +5 V, not 0 V (S1; S10; S12).
- Evidence labels in the context sections: [MFR] manufacturer claim, [MEAS] measured,
  [TRACE] traced from hardware, [LORE] forum impression.

---

## Contents

0. [Sources and local copies](#0-sources-and-local-copies)
1. [Recommended architecture (summary)](#1-recommended-architecture-summary)
2. [KCV / DAC pitch path](#2-kcv--dac-pitch-path)
3. [VCO (CEM3340), sub-oscillator, noise and source mixer](#3-vco-cem3340-sub-oscillator-noise-and-source-mixer)
4. [VCF (IR3109) and its CV summing](#4-vcf-ir3109-and-its-cv-summing)
5. [Envelope generator](#5-envelope-generator)
6. [VCA (BA662A)](#6-vca-ba662a)
7. [LFO / modulator and bender](#7-lfo--modulator-and-bender)
8. [Output stage, power rails and references](#8-output-stage-power-rails-and-references)
9. [CPU-side timing that affects the sound](#9-cpu-side-timing-that-affects-the-sound)
10. [Reference implementations, clones and product context](#10-reference-implementations-clones-and-product-context)
11. [Table of numeric constants](#11-table-of-numeric-constants)
12. [Disagreements between sources (summary)](#12-disagreements-between-sources-summary)
13. [Open questions and measurements to take](#13-open-questions-and-measurements-to-take)

---

## 0. Sources and local copies

All paths are relative to `docs/papers/` unless they start with `docs/`. Every PDF and HTML page
has a `.txt` beside it. Scans without a text layer have hand transcriptions.

| # | Source | Local copy | Notes |
|---|---|---|---|
| S1 | Roland, *SH-101 Service Notes*, 1st ed., 1 Nov 1982 (archive.org `synthmanual-roland-sh-101-service-notes`) | `docs/sh101-service-notes-archiveorg-text.{pdf,txt}`; my transcription `sh101/sh101_service_notes_numbers_transcribed.txt` | specs, block diagram, **CEM3340 pin diagram (p2)**, CPU program, adjustments, full schematic, parts |
| S2 | Roland, *SH-101 Owner's Manual*, 1982 (archive.org) | `sh101/roland_sh101_owners_manual_1982_archiveorg.{pdf,txt}`, `sh101/roland_sh101_owners_manual_1982_figures_transcribed.txt` | trigger modes, portamento, pitch chart (A = 442), patch charts |
| S3 | Curtis Electromusic, *CEM3340/3345 VCO* data sheet, ©1980 | `vco_cem3340/curtis_ces_1980_cem3340_3345_datasheet_long.pdf` (+ manual transcription) | expo/tempco law, waveforms, sync, HF tracking |
| S4 | Alfa AS3340 (v7 2020) and AS3340HYB data sheets, Alfa "VCO vs PWM stability" tip, CoolAudio V3340, R. Hordijk *Tuning the AS3340* (2018) | `vco_cem3340/alfa_*`, `coolaudio_2019_v3340_datasheet.*`, `hordijk_2018_tuning_the_as3340.*` | clone data sheets (stored here although they are data sheets) |
| S5 | T. Wiltshire (Electric Druid), *CEM3340 VCO designs* (2016) and *A study of sub-oscillators* (2017), with SH-101 redraws | `vco_cem3340/wiltshire_electricdruid_*`, `sh101/electricdruid_study_of_sub_oscillators_2017.*`, `sh101/electricdruid_sh101_suboscillator_schematic_2017.jpg` | SH-101 sub-osc and VCO redraws |
| S6 | CEM3340 web notes: SDIY wiki (2026), Holmes (2023), Kassutronics (2018), T. Henry *VCO Maximus* | `vco_cem3340/sdiy_wiki_2026_cem3340.*`, `holmes_2023_3340_vco_notes.*`, `kassutronics_2018_vco_3340.*`, `henry_birthofasynth_vco_maximus_cem3340.*` | PWM-pitch interaction, HF trim |
| S7 | CEM3340 forum extracts: Mod Wiggler (43 posts, 9 threads, via Wayback) and Electric Druid comments | `forums/cem3340_modwiggler_extracts.txt`, `forums/cem3340_electricdruid_comments_extracts.txt` | post IDs and permalinks |
| S8 | IR3109 data-sheet excerpt (10 May 1982, via Open Music Labs) and Alfa Rpar *AS3109* data sheet (2022 v2) | `ota_ir3109/img/` (OML images), `ota_ir3109/alfa_rpar_datasheet_2025_AS3109_vcf.*` | pinout, transistor-level stage (Fig. 2), specs |
| S9 | Open Music Labs (OML): IR3109 reverse engineering (Mod Wiggler thread, 108 posts), *SH-101 schematic errors* wiki, BA662 reverse engineering and clone (for the Superlative SB01), *Minimizing distortion in OTAs* | `forums/ir3109_modwiggler_reverse_engineering_thread.txt`, `ota_ir3109/openmusiclabs_wiki_sh101_schematic_errors.*`, `opensource/diy_openmusiclabs_sb01/`, `vca_ba662/openmusiclabs_*` | **schematic errata (VR18, R104, R137, …)**; measured BA662 and IR3109 internals |
| S10 | T. Wiltshire (Electric Druid), *Roland filter designs: IR3109/AS3109* (2025) and *AS3109 SH-101 filter* Rev. 2 (Jul 2025, CC BY-NC-SA 4.0) | `ota_ir3109/electricdruid_2025_roland_filter_designs_ir3109_as3109.*`, `opensource/diy_electricdruid/` | resonance path, +5 V reference, mixer values |
| S11 | A. Macaulay (AMSynths): *All about the IR3109* (2022), AM8101 VCF / AM8110 VCO / AM8112 VCA+ADSR / AM8114 modulator pages (2022), *All about the BA662* (2018), IR3R01 replica (2019) | `ota_ir3109/amsynths_*`, `sh101/amsynths_*`, `vca_ba662/amsynths_2018_all_about_the_ba662_chip.*`, `opensource/diy_amsynths_am8101/` | clone designer's notes on each block |
| S12 | Other IR3109 material: atosynth Juno/JP-6 LTspice clones, minisystem *Jove* JP-6 clone, Syntaur Juno-106 VCF calibration, Obsolete Technology 80017A teardown, Analogue Renaissance AR3109, Mod Wiggler / KVR / sequencer.de extracts | `ota_ir3109/atosynth_*`, `minisystem_*`, `syntaur_*`, `obsoletetechnology_*`, `analoguerenaissance_*`; `forums/ir3109_modwiggler_other_threads_extracts.txt`, `forums/ir3109_kvr_ota_filter_tanh_thread.txt` | |
| S13 | OTA modelling papers: Ó. Bogason, K. J. Werner, *Modeling circuits with OTAs using wave digital filters*, DAFx-17; Köper, Holters, Esqueda, Parker, *Wasp VCF OTA model*, DAFx-22; A. Huovilainen, *Enhanced digital models for analog modulation effects*, DAFx-05 | `ota_ir3109/bogason_werner_dafx2017_*`, `koeper_holters_esqueda_parker_dafx2022_*`, `huovilainen_dafx2005_*` | measured CA3080 tanh fit with rail terms |
| S14 | H. Gratz, *Operational Transconductance Amplifiers* (2008) | `vca_ba662/gratz_2008_operational_transconductance_amplifiers.*`; `ota_ir3109/gratz_2008_ota_notes_REFERENCE_ONLY.txt` | **author asks that it not be mirrored: exclude from any public release** |
| S15 | BA662 family documents: Rohm BA6110 data sheet (scan, transcribed), CoolAudio V662A, Alfa AS662D, TI LM13700 rev F, Intersil AN6668, Electric Druid OTA articles (2020, 2025), atosynth pinout, OSH Park BA662 clone | `vca_ba662/*` | no Rohm BA662 data sheet survives |
| S16 | BA662 forum extracts (Mod Wiggler t=102698, t=224494; Adafruit t=75592; VSE t=67947; Whittle quote) | `forums/ba662_forum_extracts.txt` | |
| S17 | V. Zavalishin, *The Art of VA Filter Design* rev. 2.1.2 (2020); Zavalishin, *Preserving the LTI system topology…* (2008) | `zavalishin/VAFilterDesign_2.1.2.*`, `zavalishin/zavalishin_2008_keep_topology_tpt.*` | **copied from the TB-303 repo** (`303/docs/papers/zavalishin`, `academic/`) |
| S18 | W. Pirkle, *AN-4 Virtual Analog Filters*, *AN-6 VA Diode Ladder* | `pirkle/AN-4VirtualAnalogFilters.*`, `pirkle/AN-6*` | **copied from the TB-303 repo**; I generated the AN-4 `.txt` |
| S19 | T. E. Stinchcombe, *Analysis of the Moog transistor ladder and derivative filters* (2008); Stinchcombe, *Korg MS-10/MS-20 filters* incl. the OTA version (2009) | `ladder_models/Moog_ladder_tf.*`; `ladder_models/stinchcombe_2009_korg_ms10_ms20_filters_incl_ota_version.*` | 2008 paper **copied from the TB-303 repo** |
| S20 | A. Huovilainen, *Non-linear digital implementation of the Moog ladder filter*, DAFx-04; Huovilainen, MSc thesis TKK 2010; Csound `moogladder` excerpt (LGPL) | `ladder_models/huovilainen_dafx04_moog_nonlinear.*` (**copied from the TB-303 repo**), `ladder_models/huovilainen_2010_msc_*`, `ladder_models/lazzarini_csound_moogladder_*` | |
| S21 | Other ladder/nonlinear-filter papers. Copied from the TB-303 repo: Oyama DAFx-26, D'Angelo PhD 2014, Werner & McClellan DAFx-20, port-Hamiltonian DAFx-21, Levien. New: D'Angelo & Välimäki TASLP 2014 parts I/II, Paschou/Esqueda/Välimäki APSIPA 2017, Daly MSc 2012, Fontana ICMC 2007 | `ladder_models/` | |
| S22 | mystran, "Cheap non-linear zero-delay filters", KVR t=349859 | `forums/ota_kvr_mystran_cheap_nonlinear_zdf.txt` | verbatim with code |
| S23 | Antialiasing papers **copied from the TB-303 repo**: Parker/Zavalishin/Le Bivic DAFx-16 (ADAA), Välimäki/Pekonen/Nam JASA 2012 (polyBLEP), Esqueda/Välimäki/Bilbao DAFx-16 (BLAMP), Esqueda PhD 2018, Holters DAFx-19, DAFx-22 ADAA, Pekonen JASP 2011. Also Nam/Välimäki/Abel/Smith TASLP 2010 *Efficient antialiasing oscillator algorithms using low-order fractional delay filters*: in the 303 repo it is misnamed `valimaki_nam_smith_abel_taslp2010_dpw.pdf`; renamed here | `antialiasing/` | |
| S24 | New antialiasing papers: Stilson & Smith ICMC 1996 (BLIT); Brandt ICMC 2001 (minBLEP); Välimäki/Nam/Smith/Abel TASLP 2010 (DPW); Kleimola & Välimäki SPL 2012 (PTR); Ambrits & Bank SMC 2013 (EPTR); Huovilainen & Välimäki ICMC 2005; Bilbao/Esqueda/Parker/Välimäki SPL 2017 (ADAA); Esqueda ISMRA 2016; Hohnerlein DAFx-17; Timoney et al. DAFx-12 (hard sync); Roth et al. DAFx-26; Schimmel 2012; Pekonen PhD 2014 | `antialiasing/` | |
| S25 | VA method papers **copied from the TB-303 repo**: Werner DAFx-15, Holters & Zölzer EUSIPCO-15, Werner & McClellan DAFx-22, DAFx-26 stability, DAFx-25 neural VCO | `va_methods/` | |
| S26 | T. Wiltshire, *SH-101 replacement processor: a feasibility study* (2016) | `sh101/electricdruid_sh101_replacement_processor_2016.*` | CPU pin functions |
| S27 | Tubbutec *SH-1oh1* user and installation manuals (2020); Tubbutec on AUTO portamento (Mod Wiggler t=267752) | `sh101/tubbutec_sh1oh1_*`, `opensource/diy_tubbutec_sh1oh1/`, `forums/sh101_modwiggler_extracts.txt` | |
| S28 | Sound On Sound: G. Reid, *Synth Secrets* (SH-101 & Axxe), Jul 2001; G. Reid, *SH-01A review* with A/B against his SH-101, May 2018; D. Johnson, *Retrozone*, Feb 1994; P. Nagle, *SH-101 plug-out*, Oct 2014 | `sh101/sos_*`, `context/review_sos_*` | the only hardware A/B timings found |
| S29 | Period press via muzines.co.uk: E&MM *Synthesiser Update* (Dec 1982), *One Two Testing* (Dec 1983), *Electronic Soundmaker* survey (Sep 1983), E&MM *Short Circuit* (Jan 1985) | `sh101/muzines_*` | |
| S30 | Later Roland documents: PLUG-OUT SH-101 manual (2015), Roland Cloud SH-101 manual (2024), German brochure (rolandmuseum.de), Roland AU *Icon Series* | `sh101/roland_*`, `sh101/rolandmuseum_*` | |
| S31 | SH-101 forum extracts: Mod Wiggler (68 posts, 10 threads), Vintage Synth Explorer and KVR | `forums/sh101_modwiggler_extracts.txt`, `forums/sh101_vintagesynth_kvr_extracts.txt` | KVR text came through a fetch summariser (near-verbatim at best) |
| S32 | Mod/installation documents: Kenton filter-socket kit (2003), Synhouse MIDIJACK (2002; measured rails), F. Anwander SH-101 page (2017), Maffez MS-1 mods with SH-101 comparisons | `sh101/kenton_*`, `sh101/synhouse_*`, `sh101/anwander_*`, `sh101/maffez_*` | |
| S33 | TAL, *TAL-BassLine-101* user manual (2020) | `sh101/tal_bassline101_user_manual_2020.*` | an emulator's design notes |
| S34 | Open-source code (read as text only). Each folder has a README with URL, commit and licence: KR-106 (GPL-3.0), MAME VA devices (BSD-3), LabSeven VCO-3340 (CC-BY-NC-SA-4.0), Faust `ve.lowpassLadder4` (MIT), Surge sst-filters (GPL-3.0+), TAL-NoiseMaker (GPL-2.0), JunoX / Juno60 notes (GPL-3.0 / MIT), PicoVintage (GPL-3.0+), mwAudio101 (GPL-3.0+) | `opensource/` (`INDEX_opensource_code.txt`, `found_not_saved.txt`) | licences in §10.1 |
| S35 | DIY clones and mods: OML / Superlative SB01, Electric Druid AS3109 SH-101 filter (CC BY-NC-SA), AMSynths AM8101, sibomots/RolandSH101 (BSD-3), Superlative SB01 manual, Nova-mod / circuitbenders / Kenton notes | `opensource/diy_*`, `forums/opensource_diy_sh101_extracts.txt` | |
| S36 | Product context: Behringer MS-101/MS-1 (Maffez modding guide v2 and traced VCF schematic, reviews, firmware notes); Roland SH-01A manuals and reviews; System-1 plug-out; Roland Cloud / ZEN-Core Model Expansion / FANTOM ACB pages | `context/` (indexed by its README), `forums/context_*.txt` | |
| S37 | F. Anwander, *Roland filter versions* (which chip in which synth) | `forums/anwander_roland_filters.*` | **copied from the TB-303 repo** |
| S38 | Low-value overviews: Wikipedia, Vintage Synth Explorer, Synthtopia MS-101 vs SH-101 shoot-out (2019) | `sh101/wikipedia_*`, `sh101/vintagesynth_*`, `sh101/synthtopia_*` | |

Not obtained:

- Any Rohm BA662 data sheet (OML and R. Whittle both say none survives).
- A Curtis CEM3340 Rev. G or OnChip data sheet, and the Curtis *Synthesource* newsletters.
- Välimäki & Huovilainen, CMJ 2006; Välimäki, IEEE SPL 2005 (DPW); Hélie, TASLP 2010;
  D'Angelo & Välimäki, ICASSP 2013; Corbett 2024 (OTA ZDF ladder).
- Gearspace threads (Cloudflare, no Wayback copies). These include the "TAL vs real unit" thread
  1125700 and "SH-101 filter chip".
- Mod Wiggler t=195051, t=285915 and t=231189.
- The d16 LuSH-101 and Softube Model 82 manuals.
- Any 1982–83 lab review with measurements.
- **Any measured real-unit data** for envelope times (except Reid's maximum attack), LFO rates,
  portamento times, VCF cutoff/tracking, resonance or output level.

Deliberately not downloaded:

- `joebritt/SH101_Disassembled`: a commented 80C49 disassembly plus Roland's mask-ROM dump, with no
  licence. It is the main community source for arpeggiator, sequencer and key-assignment
  behaviour and belongs to the firmware track (`docs/firmware.md`). Not downloaded for this
  research; the firmware track later took its ROM dump as the machine's ROM (`docs/sources.md`).
- `jn60c99`: a C port decompiled from Roland's Juno-60 plug-in binary. This is a copyright and
  clean-room risk.

**No peer-reviewed paper models the SH-101, the IR3109 or the CEM3340.** The best-founded
information is:

- the service notes (S1);
- the CEM3340 and AS3109 data sheets (S3, S8);
- Open Music Labs' reverse engineering (S9);
- the clone designers' redraws (S10, S11);
- Reid's hardware A/B (S28);
- the generic OTA, ladder and antialiasing literature (S13, S17–S25).

---

## 1. Recommended architecture (summary)

| Block | Recommended model | Key numbers | Rate / AA |
|---|---|---|---|
| CPU → DAC → S&H | 6-bit DAC, exact 1/12 V steps. Three demultiplexed holds: KCV, RANGE, RANDOM. Values update on the CPU loop. | LSB = 83.33 mV (5 V·16.9 k/1 M = 84.5 mV untrimmed); full scale 5.25 V; loop 1.5–3.5 ms | control rate, event-quantised |
| Portamento | One-pole RC on the **KCV in volts**, *after* CV OUT and *before* RANGE is added. AUTO: a hardware pulse on each new gate briefly shorts the glide, so only legato notes glide. | τ = R(2 MΩ A-taper)·0.33 µF ≤ **0.66 s**; 2-octave glide settles to 1 cent in 5.1 s, matching the "0–5 s" spec | control rate, ≥ 1 kHz |
| VCO (CEM3340) | Datasheet expo law on the *current* into pin 15, with the SH-101 resistors. **Triangle core**: saw is a 0–10 V ramp from the tri-to-saw switch (no discharge reset). Pin-7 HF-tracking feedback. Pulse = comparator (saw/2 vs V_PWM), inverted by TR24. | I_oct = 10 µA (100 kΩ → 1 V/oct); f_ref = 1.0 kHz at I_C = 3/R_Z = 107 µA; offset ≈ 50 µA from IC12b; trim to 442 Hz at 4.75 V | generate directly at the filter rate (2–4×); polyBLEP on saw, pulse and sub |
| Sub-osc (4013) | Two toggle flip-flops clocked at the **saw wrap**; −1 oct square, −2 oct square, −2 oct 25 %-low pulse (diode-OR). Drive from exact oscillator events, not by thresholding the band-limited saw. | 0–14 V outputs; TR41 switches at 4.6 V on the ramp | polyBLEP at the same sub-sample offset |
| Noise | Junction noise (2SC945) → TR22 → IC12a, whose feedback pole tilts the top end. Calibrate the colour to recordings. | IC12a pole 220 k·240 pF = **3.0 kHz** (my calc); LFO-path noise LP 16 kHz | white source plus a linear filter |
| Mixer | Resistors into the IR3109 stage-1 node (68 k local feedback): gain = 68 k/R. C45 10 µF coupling. | saw 470 k (0.145), pulse 680 k (0.100), sub 820 k (0.083), noise 150 k (0.453) | inside the VCF solve |
| VCF (IR3109) | Four OTA-C stages: C·dv_i/dt = −I_abc·tanh(u_i/2V_T), u_i = R_p(v_{i−1}+v_i)/68 k. Resonance via TR26 phase splitter → C52 → 5.6 k → **D30/D31 diode clamp** → 15 k into stage 1. Output-side Q make-up (1+α). Buffer rail limits. | 104 Hz/µA (pole, 240 pF); k_max ≈ 4.95; self-osc onset at **≈ 81 % RES**; f_osc ≈ 0.98–1.0·f_c; V4 ≈ 0.6 V pk (simulated) | TPT trapezoidal + Newton on 5 unknowns, **2× minimum, 4× default**, double precision |
| Cutoff CV | IC18b inverting sum (R_f 33 k) of FREQ, ENV, MOD, KYBD (PNP follower), bender and grip LFO → R128 4.7 k → thermistor divider (×0.111) → pin 9, about 19 mV/oct; more negative = higher. | KYBD trimmed to 1 oct/V by VR8; FREQ span ≈ 4.4–5.1 oct (my calc) | control rate, interpolated per sample |
| ENV | RC ADSR on C59 4.7 µF. Attack aims beyond a comparator threshold; D/R are exponential. GATE: low-note priority, no legato retrigger. GATE+TRIG: re-attack from the current level. LFO: retrigger at the LFO rate. | τ_min 0.7–1.3 ms; τ_max ≈ 4.7 s; max attack **≈ 3 s measured** (Reid) vs 4 s spec | audio rate (cheap) |
| VCA (BA662A) | OTA: i = k_m·I_abc·tanh((a·v + V_os)/2V_T) into 47 k, unity buffer. Input divider ≈ 1/152 (RES 0) to 1/76 (RES max). Offset gives an envelope-shaped thump. | V_os 0.03–0.25 mV (selected "A" parts); k_m ≈ 0.9; gm ≈ 8–8.5 mS at 500 µA | inside the oversampled block, or ADAA on the tanh |
| LFO | Integrator + Schmitt: triangle and square. RANDOM = **CPU writes a uniform 6-bit value on each clock edge**, ×2 → 0–10 V. NOISE = noise LP 16 kHz. Reset on key-down in LFO-trigger or arpeggio mode. Grip LFO MOD = sine-shaped triangle with a fade-in. | 0.1–30 Hz; fade-in τ ≈ 0.47 s (my calc) vs "1.5 s" (S11) | control rate (audio rate for NOISE mod) |
| Output | VOLUME → follower → 100 µF → 2.2 k / 1 nF / 1 k → jack. No audible coupling HPF in the output stage. | 0 dBm max; Z_out ≈ 3.2 kΩ; LPF 72 kHz | base rate after decimation |
| CPU timing | Event model of the 80C49 loop: key scan, gate logic per trigger mode, clock edges (LFO square / EXT CLK), arpeggio and sequencer stepping, CLK RST. | loop 1.5–3.5 ms; gate out 0/+12 V | sample-accurate scheduling with optional loop-jitter quantisation |

Reasoning in brief:

- **Resonance character.** The 101's resonance comes from a diode-clamped feedback path and a
  phase-splitter make-up gain, not from Juno-style OTA feedback. Each OTA sees only 1/123 of the
  signal, so the OTA tanh is mild: u ≈ 0.1 from the mixer and up to ≈ 0.4 at the clamp limit (my
  calc). The audible nonlinearity is the diode clamp, the IR3109 buffer rail limits and edge slewing.
  A "hot" generic tanh ladder would be wrong.
- **VCO.** The CEM3340's triangle core changes how the saw "reset" must be modelled: a switch,
  not a discharge. The sub-oscillator is phase-locked to the saw wrap.
- **Control paths.** Everything around the VCO and VCF is linear RC, resistor sums and switches. Model
  these from component values, not fits. The values are known from S1 and the OML errata (S9).
- **Calibration.** Use the service-note trims as the definition: 442 Hz at 4.75 V, VCF 1 oct/oct at
  KYBD max, D/A exact. Then A/B against the Roland plug-in and the recordings in §10.4.

---

## 2. KCV / DAC pitch path

The pitch path runs:

1. The CPU writes a 6-bit DAC.
2. Demultiplexed sample-and-holds produce KCV, RANGE and RANDOM.
3. KCV goes out through CV OUT and back in through the CV IN switching jack.
4. Portamento lag on KCV.
5. Sum at the CEM3340 pin-15 node with RANGE (plus key transpose), the bender/LFO-MOD mixer, tune
   trims and the HF-tracking feedback.

### 2.1 Facts

**CPU (S1 p3, S26).**

- IC6 TMP80C49P-6-7301 (8049 family, mask ROM, 6 MHz grade; the crystal value is not printed; S31 says
  a 6 MHz resonator).
- Program steps 3–13 repeat "at 1.5 to 3.5 msec intervals". Every key scan, DAC refresh and gate update
  happens on that loop.
- Pin use (S26):
  - DB0–DB5: DAC value 0–63 (also keyboard group select);
  - DB6–DB7: 4052 channel select;
  - P10–P17: switch and key read;
  - P20–P27: gate, CLK RST and LEDs (via the HD14556);
  - T0: external HOLD; T1: analog clock (LFO square or EXT CLK, via TR11).

**DAC (S1 p9, my reading).** Binary-weighted resistors into an inverting summer:

| Bit | Resistor | Mark |
|---|---|---|
| MSB | R70 30.1 k + VR3 2 kB "D/A LINEAR" | 1 % + trim |
| 4 | R57 62.5 k | 0.1 % |
| 3 | R58 125 k | 0.1 % |
| 2 | R59 250 k | 0.5 % |
| 1 | R60 500 k | 1 % |
| LSB | R61 1 M | 1 % |

- IC7 (4050) buffers the data lines. Its supply is the "+5 V (D/A WIDTH)" reference: IC1b M5218,
  zener D4, pass transistor TR3, trimmer VR1 2 kB.
- IC9a (TL062) is the summer: feedback R71 16.9 k ∥ C65 47 pF; offset R62 15.4 k + VR2 1 kB
  "D/A TUNE" from −5 V.
- My calc:
  - LSB = V_ref·R71/R61 = 5.000·16.9 k/1 M = **84.5 mV**.
  - Exactly 1/12 V needs V_ref = **4.931 V**, so "D/A WIDTH" trims the reference 1.4 % low.
  - Full scale 63 LSB = **5.25 V**. The ideal MSB resistor is 31.25 k, so VR3 sits near 1.15 k.
  - The offset path injects +5.15…+5.49 V against the negative-going bit sum, so the CPU writes
    complemented codes. D/A TUNE nulls code 0 to 0 V ± 1 mV (S1 p4).
- The test-mode voltages confirm a 1/12 V step (S1 p3): PLAY 2.75 V = 33 steps (MSB + LSB);
  ARPEGGIO DOWN 2.5 V = 30 steps (bits 1–4); U&D 4.75 V = 57 steps. Tolerance ±1 mV.
- Static error, my calc: a 1 % error on R61 is 0.85 mV (0.1 cent); 0.1 % on R57 is 1.3 mV;
  a residual MSB mis-trim of ±1 mV is ±1.2 cents at the 31→32 step. None of this is audible on a
  trimmed unit.
- AMSynths (S11) and Electric Druid (S26) confirm the DAC drives only pitch CV and the RANDOM LFO.

**Sample-and-holds.** IC9a → R72 220 Ω → IC10 4052 (Y side; A/B/INH from the CPU) → 0.015 µF
holds with TL062 buffers:

| 4052 channel | Hold | Buffer | Destination |
|---|---|---|---|
| Y0 | C30 | IC9b follower, R73 1 k *inside* the loop | **KCV** → CV OUT jack. S27 uses R73 as the DAC test point (0 / 2.75 / 2.5 V) |
| Y1 | C28 | IC11b follower → VR5 5 k "RANGE WIDTH" + R99 97.6 k (1 %) → CEM3340 pin 15 | **RANGE** (octave + key transpose; transpose too in arp/seq) |
| Y2 | C29 | IC11a ×2 (R81 = R82 = 100 k) | **RANDOM**: "6-bit number … amplifying this by x2, to achieve a 0 – +10V pseudo sample and hold" (S11) |

- My calc: acquisition τ = 220 Ω·15 nF = 3.3 µs.
- My calc: droop. At the 4052's data-sheet maximum leakage (100 nA) a hold sags 6.7 V/s, i.e. up to
  23 mV (0.28 semitone) between 3.5 ms refreshes. That would be a sawtooth ripple at 285–670 Hz.
  Typical leakage (tens of pA) gives µV. Not modelled by default.

**CV values (S1 p3).**

- RANGE: 16′ = 1 V, 8′ = 2 V, 4′ = 3 V, 2′ = 4 V. The KEY TRANSPOSE shift is added to RANGE: lowest-F
  key with 16′ → 0.417 V; "higher C" with 2′ → 5 V.
- KCV with the TRANSPOSE switch: L lowest F 0.417 V, M lowest F 1.417 V, H lowest F 2.417 V, H highest
  C 5.0 V. So KCV = semitones/12 V, the 32-key F…C keyboard spans 31 semitones, and L/M/H add
  0/1/2 V.
- In arpeggio and sequencer modes the L/M/H shift moves to RANGE (steps 4 and 12).
- CV OUT spec: 1 V/oct, 0.415–5 V (S1 p2), i.e. KCV alone.

**Signal order around the jacks.**

- The KCV leaves through CV OUT and returns through the CV IN switching jack into the lag circuit,
  then the VCO. "CV OUT carries no glide", checked on two units (VSE t=57137, S31).
- RANGE/key-transpose ("CV2") is added *after* the glide (S27).
- GATE likewise leaves through GATE OUT and returns through the GATE IN switching jack (S31). Those
  jacks corrode.

**Calibration anchor (S1 p4; S2).**

- Test mode U&D (KCV 4.75 V, RANGE 0 V), panel TUNE centred: VR7 sets **442 Hz**.
- UP mode (KCV 0, RANGE 4.75 V) is trimmed (VR5) to the same pitch, so RANGE and KCV both weigh
  1 V/oct.
- VCO WIDTH (VR6): F3/F5 Lissajous against a reference.
- Owner's manual: at 8′/M "the lowest C note corresponds to the Middle C of the piano". The pitch
  chart uses A = 442 Hz, C = 262.8 Hz (S2).
- My calc: f = 442·2^(V_total − 4.75), so **0 V ↔ 16.43 Hz** (C0 at A = 442; 16.35 Hz at A = 440).
  - At 8′/M the keyboard spans **F3–C6**.
  - 16′/L lowest F = 1.417 V = F1, 43.9 Hz; 2′/H highest C = 9 V = C9, 8.41 kHz.
  - With KEY TRANSPOSE the total reaches 10 V = **16.8 kHz** (S2 gives 16.82 kHz). The bottom is about
    0.83 V (≈ 29 Hz).
  - The −2-octave sub reaches ≈ 11 Hz.
- Tubbutec (S27) measured A = 440 Hz on its unit: highest A at 16′/M = 440 Hz, lowest 110 Hz.

**Portamento (bender board, S1 p7; S27; S31).**

- Circuit: PORTA IN → VR2 **2 MΩ (A taper)** → C10 **0.33 µF** (handwritten ".33") → IC2b (TL062)
  follower → VCO.
- A 2SK30A JFET (TR1; R17 1 M gate–source) across VR2 shorts it. TR4 (R14 100 k to −5 V, R18 22 k,
  R19 100 k) and TR5 pinch it off through D1. S3 selects ON / AUTO / OFF.
- **AUTO is hardware (Tubbutec, MW t=267752).** "Portamento is always on, and each new gate at
  GATE IN fires a short pulse that briefly sets the glide time to about zero."
  - The differentiator is bender R15 47 k / C11 4.7 nF. My calc: τ = 0.22 ms.
  - The CPU's PORTA OFF line only suppresses glide when the arpeggiator is engaged.
  - Legato key changes (no new gate) therefore glide; detached notes jump.
  - S2's AUTO wording is self-contradictory. Reid and *One Two Testing* (S28, S29) confirm "glides only
    while two keys overlap".
- My calc:
  - τ_max = 2 MΩ·0.33 µF = **0.66 s**.
  - A 2-octave jump settles to 1 cent in τ·ln 2400 = **5.1 s**, which matches "0 ~ 5 s" (S1 p2).
  - 5τ = 3.3 s, which matches the German brochure's "0–3 s" (S30).
  - The JFET short is ≈ 300 Ω·0.33 µF ≈ 0.1 ms (R_on assumed, not measured), so off means effectively
    instant.
- The glide filters **volts**: pitch approaches the target exponentially in cents (constant-time, not
  constant-rate).

**VCO CV summing node, CEM3340 pin 15** (S1 p9; pin functions from Roland's CEM3340 block diagram,
S1 p2; OML errata S9):

| Input | Value | Function |
|---|---|---|
| RANGE | R99 97.6 k (1 %) + VR5 5 k | 1 V/oct after trim |
| KCV after portamento | R104 100 k | 1 V/oct. The same line feeds VR8 "VCF WIDTH", the KYBD source. OML's errata (S9) list "R104 connects to VR8" as a drawing correction; the scan joins VR8 to the R91 line instead |
| IC12b (bender + LFO MOD mixer, my reading) | R103 100 k (1 %) | IC12b is inverting around the +5 V bus: R_f R89 100 k ∥ C35 680 pF (2.34 kHz pole). Inputs: R91 100 k (VCO MOD from the bender board) and R90 4.7 M (a small input, probably the panel TUNE VR9; unverified) |
| +15 V via a bridgeable pattern break | R102 1.5 M (1 %) | +10 µA (+1 oct); "short-circuit the break … if [VR7 cannot reach 442 Hz]" (S1 p4) |
| VR7 "VCO TUNE" 100 kB (GND…−5 V) | R108 470 k (1 %) | 0…−10.6 µA (0…−1.06 oct) |
| HF tracking (pin 7) | R100 1.5 M | pin 7 also has 10 k to GND (printed as a second "R99"; OML: R199) |
| stabilisation | R107 560 + C44 10 nF to GND | data sheet asks for 470 Ω + 10 nF on pins 13 and 15 (S3) |

My calc, absolute tuning. Under the S3 law (§3.2), V_total = 4.75 V alone (47.5 µA) gives only 16 Hz;
442 Hz needs I_C = 95.4 µA.

- The missing ≈ 48 µA comes from IC12b. Its non-inverting input sits on the +5 V reference bus
  (Roland's dot-stub), so its output idles at ≈ +5 V and sends ≈ 50 µA through R103.
- Total ≈ 97.5 µA gives ≈ 513 Hz untrimmed. VR7 subtracts ≈ 2 µA to reach 442 Hz, inside its
  0…−10.6 µA range.
- So the *pitch offset depends on the +5 V reference*, which D/A WIDTH trims. That is why S1 insists
  the D/A is trimmed first.

**Bender and grip LFO MOD (bender board).**

- Bender lever → IC3a inverting sum (R1/R2 150 k in, R3 100 k feedback, R4 100 k) → IC3b; depth
  pots VR4 100 kB (VCO) and VR5 100 kB (VCF).
- The LFO triangle is turned into a sine by the TR2 2SC1583 differential pair (S11: "converts the
  triangle waveform to a sine waveform"). Depth VR6 100 kA "LFO MOD"; offset trim VR3 200 kB
  (0 ± 2 mV between TP-1/TP-2, S1 p4).
- The grip button (or pushing the lever) engages it through TR6/TR3 with R22 1 M / C9 0.47 µF.
  My calc: τ = 0.47 s. S11 says the delay "is fixed at 1.5 s" (≈ 3τ); E&MM 1982 says "a very slight
  built-in delay" (S29).
- Bend range "up to about ±1200 cents" (S2). One reviewed unit bent asymmetrically (S29, ES 1983).
  The MG-1 grip bends up only.

### 2.2 Models found

- **TAL-BassLine-101 (S33)** and **mwAudio101 (S34)**: generic exponential glide on note number;
  every mwAudio101 constant is marked "pragmatic invention".
- **Roland ZEN-Core Model Expansion (S36)**: a portamento-curve option labelled "ORIGINAL" [MFR].
  Roland itself treats the curve as a distinctive property.
- **Behringer MS-101**: portamento generated by the CPU [TRACE, S36]. The original's AUTO behaviour came
  back only in firmware 1.0.6.

### 2.3 Disagreements

- **Portamento maximum:** 5 s (S1, S2) vs 3 s (S30 brochure). Both match τ = 0.66 s under different
  definitions (1 cent vs 5τ).
- **Tuning reference:** A = 442 Hz (S1, S2) vs 440 Hz (S27 measurement; most clones).
- **AUTO portamento:** S2's text vs S27/S28/S29 behaviour. The hardware (gate-edge reset) is
  authoritative.

### 2.4 Recommendation

1. **DAC.** Exact 1/12 V steps. Optional "vintage" per-bit errors drawn from the resistor
   tolerances: σ ≈ 0.05 % (0.1 % parts), 0.25 % (0.5 %), 0.5 % (1 %), plus a VR3 mis-trim. Default
   off.
2. **S&H timing.** Update KCV, RANGE and RANDOM on a simulated CPU loop clock (default 2.5 ms; range
   1.5–3.5 ms). This sets latency and legato/arpeggio timing (§9), not the CV value.
3. **Portamento.**
   - One-pole RC on volts: τ = (R_VR2(θ) + R_on)·0.33 µF, using a 2 MΩ A-taper law (≈ 10–15 % at
     mid-travel typical; measure on a unit if possible).
   - Apply it to KCV only. RANGE and key transpose are added after the lag.
   - AUTO = always on, but each new gate edge forces ≈ 0.5 ms of near-zero τ. This replicates
     "legato glides, detached notes jump", including for external gates. PORTA OFF in arpeggio.
4. **Pitch sum.** Sum *currents* into pin 15 using the table above (R103, R104, R99+VR5, R102, R108,
   R100). Run the CEM3340 law of §3 on that current.
   - Expose calibration trims (VR5, VR6, VR7) as hidden parameters.
   - Default the reference to A = 440 Hz, with 442 Hz selectable.
5. **Bender / LFO MOD.**
   - Linear bend into the IC12b path (VCO) and the IC18b path (VCF) with the panel depth pots.
   - Grip LFO = sine-shaped triangle × depth, with a one-pole fade-in. Default τ = 0.47 s, from the
     components; S11's 1.5 s is probably a "full depth" time.

---

## 3. VCO (CEM3340), sub-oscillator, noise and source mixer

### 3.1 Facts

**Pin use in the SH-101.** From Roland's own *CEM3340 block & connection diagram*, S1 p2:

| Pin | Function | SH-101 connection |
|---|---|---|
| 16 | V+ | +15 V (C40 2.2 µF) |
| 15 | frequency CV summing node (virtual ground) | §2 table |
| 14 | scale resistor to GND (R_S) | R106 1.69 k (1 %) + VR6 300 Ω "VCO WIDTH" |
| 13 | reference-current summing node | R101 1.5 M from +15 V; R105 560 + C43 10 nF |
| 12 | GND | |
| 11 | timing capacitor C_F | C41 **1000 pF**, marked "(S)" for a special type |
| 10 | triangle out | **unused** |
| 9 | soft sync | C42 10 nF to GND (unused) |
| 8 | saw out | to the mixer and the sub-osc clock |
| 7 | HF tracking current out | 10 k to GND, R100 1.5 M to pin 15 |
| 6 | hard sync | C38 10 nF (unused) |
| 5 | PWM input | PW mix (below) |
| 4 | pulse out (open emitter) | R95 10 k → TR24 |
| 3 | V− | −5 V direct, so the internal zener is unused (S3) |
| 1, 2 | tempco generator resistors R_Z, R_T | R98 28 k (1 %), R97 5.6 k (1 %); node decoupled by C39 2.2 µF |

**Core (S3, confirmed by Roland's block diagram, which shows "TRI TO SAW CONV.").**

- The CEM3340 is a **triangle-core** oscillator. A reversing current source charges C_F up and down
  between 0 and V_CC/3 (5 V at 15 V).
- The saw is synthesised from the triangle by a switching converter: saw = tri on one half,
  10 V − tri on the other. The result is a 0–10 V (2·V_CC/3) buffered ramp.
- So there is **no discharge reset, no reset time and no reset glitch**. The only "reset" is a fast
  internal switch at the bottom of the triangle.
- Triangle symmetry is 45–55 %. If it is not 50 %, the saw has a slope kink at mid-ramp.
- Derived by the CEM3340 agent: with comparator delay t_d the triangle overshoots by δ = 10·f·t_d V at
  each turn. The period becomes T + 4·t_d and the saw shows a −2δ step at mid-ramp. That is the
  "notch where the two halves join" a forum poster saw on a Rev. G (S7).

**Expo law and temperature compensation (S3; my substitution).**

```
I_OM = (22·V_T/R_T)·(1 − I_C·R_Z/3.0)        multiplier output (pins 1, 2 set R_Z, R_T)
V_B  = I_OM·R_S                              R_S = scale resistor at pin 14
I_EG = I_REF·exp(−V_B/V_T)
f    = 3·I_EG/(2·V_CC·C_F)
⇒ f  = f_ref·exp(−22·(R_S/R_T)·(1 − I_C·R_Z/3.0)),   f_ref = 3·I_REF/(2·V_CC·C_F)
     = f_ref·2^((I_C − 3.0/R_Z)/I_oct),              I_oct = 3.0·ln2·R_T/(22·R_S·R_Z)
```

- V_T cancels. **That is the whole temperature compensation; there is no heater.**
- I_C must stay ≥ 0; a negative I_C acts as zero.
- Most temperature-stable point: V_B = 0, i.e. I_C = 3.0/R_Z (Hordijk, S4).

My calc with SH-101 values (R_T 5.6 k, R_Z 28 k, C_F 1 nF, V_CC 15 V, I_REF = 15 V/1.5 M = 10 µA):

- f_ref = **1.00 kHz** at I_C = 107 µA.
- 1 V/oct (I_oct = 10 µA at 100 kΩ) needs R_S = 1.89 kΩ, inside 1.69–1.99 kΩ (R106 + VR6). The trim
  range is 0.894–1.053 oct/V.

Data-sheet specs (S3):

- Sweep 50 000:1 minimum, 500 000:1 typical.
- Expo scale error 0.2 %/1 % untrimmed, 0.05 %/0.3 % trimmed.
- Multiplier error up to 5 cents five octaves from the trim point.
- Tempco cancellation ±150 ppm max; drift ±50 ppm typ / ±200 ppm max.
- Best accuracy for 50 nA–100 µA (5 Hz–10 kHz with 1 nF).
- f_max ≈ 40/57/80 kHz (min/typ/max) with 1 nF.
- Triangle 0–5 V; saw 0–10 V (9.4–10.6), output impedance 65–150 Ω. More than 40 pF on the saw pin
  causes HF oscillation.
- Loading the triangle detunes (100 k to GND: −0.15 %). Unused in the 101.

**HF tracking (S3; CEM3340 agent's analysis of the SH-101 values).**

- The scale goes flat in the top octaves for two reasons: comparator delay (above ≈ 5 kHz) and
  Q2 bulk emitter resistance (above ≈ 100 µA).
- Pin 7 outputs I_EG/4. In the 101, 10 k to GND and R100 1.5 M into pin 15 give
  ΔI_C = I_EG/600, a **fixed** correction (the data sheet uses a 20 k trimmer).
- My calc: +2 cents at 1 kHz, +10 at 5 kHz, +20 at 10 kHz, +40 at 20 kHz. To first order this cancels a
  fixed excess period τ ≈ 1.1 µs (inferred design target, not measured).

**Pulse (S3; S1 p9).**

- The chip's pulse is high while saw/2 < V_PWM: width 0 % at 0 V, 100 % at 5 V on pin 5.
- The fall is slower than the rise because comparator gain is finite. A 1 MΩ pin-4→pin-5 resistor adds
  hysteresis; **the 101 has none**.
- In the 101, pin 4 → R95 10 k → **TR24, a common-emitter inverter** (R93 1 k, R92 68 k to +14 V).
  So the SH-101 pulse is the *inverse* of the chip's, swinging 0 to ≈ +14 V. A commenter says the
  inversion stops saw + pulse cancelling in the mix (S5 comments).
- PWM: pin 5 ← TR25 follower via R96 100 k (from the PULSE WIDTH slider VR12 100 kB and the S4 LFO/MAN/ENV
  selector, R176 82 k / R177 150 k / R178 120 k / R190 18 k). Also R94 100 k + VR4 50 kB "PW" trim to
  GND and C37 10 nF.
- Spec: PWM 50 % → 0 % (S1). In practice about 5 % minimum (S11). The MS-1 reaches only about 10 %
  (S31).
- PWM-dependent pitch shift is reported on other 3340 circuits (S6, S7): Holmes ≈ 5 % from 90 % to
  10 % width (AS3340); 30 cents at 50 % (early AS3340); a spike at 50 % (Rev. G); "vintage chips
  don't have it" (KSS). The reported fixes are a 0.1 µF bypass on pin 14 and a regulated −5 V. The 101
  has neither.

**Sub-oscillator (S5, S1 p9, S31).**

- IC17 4013 (control board, about +14 V supply, 0–14 V outputs).
- The clock comes from TR41, driven by the saw through R179 220 k / R180 33 k, with collector pull-up
  R183 68 k. My calc: it switches at 0.6·(253/33) = **4.6 V** on the 0–10 V ramp.
- The 4013's rising clock edge falls on the **saw wrap**, so the sub edges coincide with the saw's
  reset edge (S5; MW t=272059 reasoning).
- Outputs, chosen by panel switch S5 (sub-waveform selector):
  - Q1 → 1 OCT DOWN square;
  - Q2 → 2 OCT DOWN square;
  - D38/D39 diode-OR of the two (R181/R182 47 k) → 2 OCT DOWN pulse, high 75 % / low 25 %
    (S5; MW t=188458).
- "Exactly tracks" (ES 1983, S29). Summed with the saw it gives a "sub-saw"; the MC-202 differs by
  90° (lore, MW t=274655).
- Modern fast 4013s glitch here; only the Toshiba TC4013 works in the AMSynths clone (S5, S11).
- Estimated clock delay 1–2 µs from TR41 storage and 68 k × stray capacitance (CEM3340 agent; not
  measured; inaudible).

**Noise (S1 p9; S31; S36).**

- TR23 2SC945 "selected for noise" (reverse-biased junction, R84 1 M / R85 470 k, C34 10 µF) →
  TR22 amplifier (R83 47 k, C33 10 µF, C31 10 nF) → R87 47 k / R88 10 k → IC12a.
- IC12a: R_f R86 220 k ∥ C32 240 pF. My calc: **3.0 kHz** pole in the gain path, so the noise is
  tilted above about 3 kHz until the gain reaches unity.
- Colour reports:
  - "not pure white": the MS-1 is "brighter than the original" (S32 Maffez);
  - TAL defaults to a non-white noise (S33);
  - the SH-01A offers "Original" and a "much bluer Variation" (S28);
  - SOS 1994 just says "white".
- The NOISE level slider VR16 100 kB feeds the mixer; the LFO path takes noise through
  R172 1 k / C60 10 nF (**15.9 kHz**, my calc; S11 "−3 dB at 16 kHz").

**Source mixer (S1 p9; values per S10, which match the drawing).**

- Panel sliders VR13 (pulse), VR14 (saw), VR15 (sub), VR16 (noise), each 100 kB, feed:
  **noise R109 150 k, pulse R110 680 k, saw R111 470 k, sub R112 820 k**.
- These join at a node coupled by C45 10 µF bipolar into the IR3109 stage-1 input (pin 2). That node
  has R114 68 k local feedback, R115 560 Ω to +5 V, and the resonance injection R113 15 k (§4). There
  is no separate input resistor.
- External audio can be injected at R109–R112 (S35 mods).

My calc, DC gain to the stage-1 output = 68 k/R:

| Source | Resistor | Gain | Output level | OTA input at HF | u = V/2V_T |
|---|---|---|---|---|---|
| saw (10 Vpp) | 470 k | 0.145 | 1.45 Vpp | 11 mVpp | 0.11 |
| pulse (14 Vpp) | 680 k | 0.100 | 1.40 Vpp | 11 mVpp | 0.11 |
| sub (14 Vpp) | 820 k | 0.083 | 1.16 Vpp | 9 mVpp | 0.09 |
| noise | 150 k | 0.453 | | | |

**The mixer cannot overdrive the IR3109 inputs.**

### 3.2 Models found

- **MAME `cem3340.cpp` (S34, BSD-3).** Implements exactly the S3 law above:
  iom = (22VT/RT)(1 − I·RZ/3), vb = iom·RS, ieg = (VCC/RR)·e^(−vb/VT), f = 3·ieg/(2·VCC·CF).
  VT = 25.7 mV, RS 1.8 k, RT 5.6 k, CF 1 nF, RR 1.5 M. Ranges: triangle 0..VCC/3, ramp 0..2VCC/3,
  pulse 0..VCC−1.5.
- **MAME `va_vco.cpp` (S34).** PolyBLEP/PolyBLAMP with sub-sample-accurate sync.
- **LabSeven VCO-3340 (S34, CC-BY-NC-SA, VCV Rack).**
  - BLIT plus leaky integrator at 192 kHz; an 8-sample Blackman-Harris "original 3340 impulse
    reconstructed" with leak 0.27 (no data backing it).
  - Sub modes −1 oct, −2 oct, −2 oct 25 % with odd amplitudes 1.255 / −0.25 / −0.005 / −1.
  - PW ranges: LFO ±5 V → 10–90 %; MAN/ENV 50–90 %.
  - ±1 % pitch jitter "observed in the hardware synth" (unverified). Behavioural reference only.
- **Pekonen et al. JASP 2011 (S23).** Analog saw imperfections via phase distortion or a post-EQ.
  Generic; not needed here because the CEM3340 has no slow reset.
- **Antialiasing, quantitative claims at 44.1 kHz (S23, S24).**
  - polyBLEP (JASA 2012), alias-free fundamental limit:

    | Method | Alias-free up to |
    |---|---|
    | 2-point | 2.1 kHz |
    | 3rd-order Lagrange | 3.2 kHz |
    | 3rd-order B-spline | 4.6 kHz |
    | 4th-order Lagrange | 5.1 kHz |
    | 4th-order B-spline | 7.8 kHz |

  - DPW N = 2…6: 600 / 2037 / 4593 / 7851 / 12 221 Hz.
  - PTR (Kleimola 2012) is stateless and 40–54 % cheaper than DPW, with no transients under fast FM.
    EPTR (Ambrits 2013) is a further 30 % cheaper.
  - minBLEP (Brandt 2001): table oversampled 64×, 16 zero crossings, minimum phase. Needs a second-order
    leaky integrator to kill the DC error.
  - polyBLAMP (Esqueda DAFx-16): about 50 dB less aliasing on slope discontinuities.
  - Naive oscillator plus 64× oversampling masks aliasing only up to about 880 Hz (Schimmel 2012).
    Oversampling alone is not viable.

### 3.3 Disagreements

- **CEM3340 pulse high level:** V_CC − 0.9 V (formula, S3) vs "1.5 V below supply" (S3 text) vs
  V_CC − 1.3 V (S5). Irrelevant in the 101, which re-generates the pulse in TR24.
- **Clone parameters vs the CEM3340 (S3, S4, S7):**

  | Parameter | CEM3340 | Clone value |
  |---|---|---|
  | Hard-sync reference | −2.5 V | −1.4 V (AS3340 data sheet, Rev. G measurement) |
  | Zener | 6.5 V | 7.4 V (Alfa) |
  | Pulse pull-down | 10 k | 51 k (Alfa) |
  | Hysteresis resistor | 1 M | 12 M (V3340) |

  None of these is used in the 101.
- **Minimum pulse width:** 0 % (S1) vs ≈ 5 % (S11).
- **Sub-oscillator phase:** the sub-saw lore (aligned with the saw wrap) vs a 90° offset on MC-202
  type circuits. The schematic supports alignment.
- **Noise colour:** "white" (S28 1994) vs "not white" (S32, S33, S28 2018).
- **Electric Druid's VCO redraw routes R100 wrongly** (S7 notes); use Roland's schematic.

### 3.4 Recommendation

1. **Expo at control rate (≥ 1 kHz, interpolated).**
   - Sum the pin-15 currents (§2.4), clamp at ≥ 0, and apply f = f_ref·2^((I_C − 3/R_Z)/I_oct) with
     the SH-101 values.
   - Add the pin-7 term I_C += I_EG/600, using the previous update's I_EG.
   - Add an optional HF-delay correction f /= (1 + τ_d·f), τ_d ≈ 1 µs (net flat ±5 cents to 10 kHz).
   - Hidden "vintage" parameters: scale trim, tempco residual (±50 ppm/°C) and slow drift.
2. **Core.**
   - One phase φ ∈ [0, 1), with saw = 10·φ V.
   - Internal triangle peak at φ = a (a = 0.5 ± tolerance).
   - Optional mid-ramp step −2δ (δ = 10·f·t_d) and a kink when a ≠ 0.5. Default ≈ 0: unmeasured.
3. **Pulse.**
   - Chip pulse high while saw/2 < V_PWM, then **inverted** (TR24) to 0/14 V.
   - Edges at the wrap and at the crossing, found by linear interpolation within the sample.
   - Clamp the PW floor near 5 %. Optional 0–2 cent PW-dependent pitch offset.
4. **Sub-osc.**
   - On each wrap, at its fractional offset, toggle the two flip-flops, form the diode-OR, and apply
     one polyBLEP per changed output (they can share a correction buffer).
   - Optional 1–2 µs delay (inaudible). Levels 0–14 V into R112 820 k.
5. **Antialiasing and rate.**
   - Generate the VCO directly at the VCF's oversampled rate (2× minimum, 4× default) with 2-point
     polyBLEP. At 4× (176.4 kHz) that keeps aliases well below the passband up to the 8.4 kHz top note.
   - If a base-rate path is ever needed, use 4-point B-spline polyBLEP or EPTR.
   - Prefer stateless methods (polyBLEP/PTR) over DPW: portamento and LFO FM are common on the 101.
   - The triangle is unused and sync is absent, so no BLAMP or sync handling is needed.
6. **Noise.** White Gaussian source → one-pole tilt matching IC12a (3.0 kHz) → level. Calibrate the colour
   against the Mr Card raw noise recording (§10.4). The LFO-path copy gets the 16 kHz low-pass.
7. **Mixer.** Exact resistor gains into the VCF stage-1 node (inside the VCF solve), with the C45
   coupling (my calc: < 0.05 Hz, negligible).

---

## 4. VCF (IR3109) and its CV summing

### 4.1 Facts: the chip

**Pinout** (IR3109 data sheet of 10 May 1982 via OML, S8/S9; identical on the AS3109):

| Pin | Function | Pin | Function |
|---|---|---|---|
| 1 | GND / reference | 9 | V_CONT (expo control) |
| 2 | IN1 | 10 | IN3 |
| 3 | CAP1 | 11 | CAP3 |
| 4 | OUT1 | 12 | OUT3 |
| 5 | IN2 | 13 | IN4 |
| 6 | CAP2 | 14 | CAP4 |
| 7 | OUT2 | 15 | OUT4 |
| 8 | −V_EE / substrate | 16 | +V_CC |

**Internals** (OML reverse engineering, S9: Mod Wiggler p1732598/p2618155; AS3109 Fig. 2, S8):

- **OTA cores.**
  - Four plain differential-pair OTAs, BA662/3080 style: **no linearising diodes**.
  - The inverting input is internal, tied to pin 1 through ≈ 560 Ω. That is why every IN pin has an
    external 560 Ω to the reference: offset balance.
  - The stage is **inverting** relative to its IN pin, so the local 68 k feedback is negative.
  - Output compliance is ≈ 0.8 V from the rail (OML: Wilson mirrors; AS3109 Fig. 2: cascode).
- **Buffers.**
  - P-channel MOS source followers, so OUT = CAP + |V_GS|. |V_GS| ≈ 3 V (2014 measurement), 2.9 V
    (2022) on the IR3109; ≈ 3.5 V on the AS3109.
  - Gate leakage < 20 nA.
  - Buffers 1/3 use 20 k pull-ups; buffers 2/4 use 1 mA current sources.
- **Expo converter.**
  - An op-amp/differential-pair loop holds the reference transistor at ≈ 3 µA (1.2 V zener + 20 k).
    The NPN expo pair has a 1.6 k emitter resistor.
  - One mirror feeds all four OTAs.
  - **More negative pin 9 → more current.** Saturates near ≈ 700 µA.

**Data-sheet specs (S8).**

- IR3109: g_m 1 µ℧–10 m℧; offset < ±3 mV.
- AS3109, scale and range: control scale **−19 mV/oct (−17.5…−20.5)**; 15 octaves over
  V9 = −140…+160 mV; 250 Hz at V9 = 0 (68 k/240 pF, per stage).
- AS3109, accuracy and feedthrough: tempco 0.33 %/°C; control error 4–10 %; CV input current 30 nA;
  CV feedthrough −45 dB.
- AS3109, offsets and drive: offset −12 mV per stage, ±0.2 mV for four in series; buffer slew 25 V/µs;
  output source current ≥ 0.6 / 1 / 1.3 mA (stages 1&3 / 2 / 4).
- AS3109, supply: 7.5 mA at V_C = 0, 13 mA at −200 mV.

**OML measurements (S9).**

- Expo slope ≈ 290 dB/V (≈ **20.8 mV/oct**), linear over 100 dB, usable to ≈ 2 nA.
- Linearity error is within ±0.5 % from 30 nA to 5 µA, then rises positive: +1 % at ≈ 12 µA,
  +2…+7 % at 0.1–0.3 mA, up to ≈ +10 % at 0.5 mA, chip-dependent. "Really emphasises the high
  frequencies."
- CV modulation slew-limits above ≈ 4 kHz and rings near 30 kHz.
- OTAs matched to ≈ 5 %.
- Other figures: g_m = 9600 µS at 500 µA (rich_de, S12); the external 560 Ω measured ≈ 432 Ω in one
  unit (Jackdamery, S12).

### 4.2 Facts: the SH-101 circuit

From S1 p9 (my reading), corrected by the OML errata (S9) and Electric Druid's Rev. 2 redraw (S10).

**Supply.** Pin 16 = +9 V, pin 8 = 0 V, pin 1 = the **+5 V reference** (S10; S9). AMSynths
says +4.5 V (S11).

**Stage network.**

| Role | Parts |
|---|---|
| Interstage 68 k | R117 (4→5), R119 (7→10), R124 (12→13) |
| Local feedback 68 k | R114 (4→2), R118 (7→5), R121 (12→10), R122 (15→13) |
| 560 Ω to the +5 V bus (decoupled by C46 2.2 µF) | R115 (2), R116 (5), R120 (10), R123 (13) |
| Integrating caps, 240 pF | C48 (pin 3), C47 (6), C50 (11), C51 (14) |

Each 68 k / 68 k / 560 node sums the previous output and the stage's own output:

```
u_i = R_p·(V_{i−1} + V_i)/68 k,   R_p = 68 k ∥ 68 k ∥ 560 = 550.9 Ω   ⇒ u_i = (V_{i−1} + V_i)/123.4
C·dV_i/dt = −I_abc·tanh(u_i/2V_T)            (V relative to the +5 V bus; stage inverts)
⇒ dV_i/dt = −ω_c·V_s·tanh((V_{i−1} + V_i)/V_s),  V_s = 2V_T·68 k/R_p ≈ 6.34 V,  ω_c = I_abc·R_p/(2V_T·68 k·C)
```

- The ODE is the IR3109 agent's derivation from the AS3109 Fig. 3 network and S1; I checked it.
- My calc and the IR3109 agent agree: **pole frequency ≈ 104 Hz/µA** at 25 °C with 240 pF. So 1 kHz ≈
  9.6 µA, 20 kHz ≈ 200 µA and 10 Hz ≈ 0.1 µA. That is consistent with the spec range and well inside
  the chip's 2 nA–700 µA.
- The slew limit is I_abc/C (e.g. 0.04 V/µs at 10 µA). Large fast edges at low cutoff are
  slew-softened, which is the OTA "soft slew limiting" of Zavalishin §6.9 (S17).

**Stage 1 input (pin-2 node).**

- Inputs: the mixer through C45 (§3.1); the resonance injection R113 15 k; R114 68 k local feedback;
  R115 560 Ω.
- R_p1 ≈ 532–535 Ω, so stage 1's pole sits ≈ 3 % lower.
- Gains to stage-1 output: 68 k/R for each source; resonance 68 k/15 k = **4.53**.

**Resonance path** (S10, S9; confirmed by me on S1):

- OUT4 → VR18 RES 100 kB.
  - The other end goes to **+5 V**, per OML's fix; Roland's drawing and S10 show GND.
  - Wiper → **TR26**, a phase splitter: collector load R136 15 k (+R135 1 k decoupled by C54 to
    +V), emitter R131 10 k. Collector gain ≈ −15 k/10 k = **−1.49**.
- Collector → TR27 emitter follower (emitter resistor R137 **33 k**, unlabelled on the drawing,
  per OML) → C52 10 µF → R129 5.6 k → **D30/D31 anti-parallel diodes to the +5 V bus** → R113 15 k →
  pin 2.
- Linear loop gain at full RES, my calc: 1.49 × 15 k/(5.6 k + 15 k) × 68 k/15 k = **4.95** (IR3109 agent:
  4.96). Against the equal-pole threshold of 4, that is ≈ 24 % excess. **Self-oscillation starts at
  α ≈ 0.81 of VR18 travel** (linear pot, no dead zone).
- C52 coupling corner: 1/(2π·10 µF·20.6 k) = **0.77 Hz**.

**Output and Q make-up.**

- OUT4 → R130 150 k, and TR26's emitter (≈ α·V4) → R132 150 k, both into one node → C53 10 µF
  bipolar → BA662 input (1 k to the bus).
- The output therefore scales with (1+α): **output-side Q compensation**.
- My calc, passband (DC) gain ∝ (68 k/R_in)·(1+α)/(1 + 4.95α). Relative to α = 0:

  | α | Relative gain |
  |---|---|
  | 0.5 | −6.0 dB |
  | 0.8 | −8.8 dB |
  | 1.0 | −9.5 dB |

  Without the make-up it would be −15.5 dB at α = 1.

**Simulated self-oscillation (IR3109 agent: RK4 of the schematic, not measured).**

- At full RES: V4 ≈ **0.60 V peak** (≈ 1.2 Vpp); diode node ≈ 0.48 V peak; OTA-1 input ≈ 15 mV
  peak (u ≈ 0.3).
- Pitch ≈ **0.98–1.0 × f_c**.
- My cruder estimate (clamp at ±0.6 V → stage 1 at 2.7 V peak) is only an upper bound; the
  simulation includes the loading.

**Headroom (IR3109 agent, from the buffer and OTA compliance figures).**

- Stage outputs can swing ≈ −1.3 V below the +5 V bus (0.8 V compliance + 2.9 V |V_GS| on a 9 V
  supply), and ≈ +2.5 V above it on stages 1/3, +3.8 V on stages 2/4.
- In self-oscillation stage 1 "wants" −1.66 V, so the buffer rail and the diode clamp both act.
  With the AS3109's larger |V_GS| the amplitude drops to ≈ 0.30 V.
- **Asymmetric clipping is part of the character.**

**Cutoff CV summing** (control board IC18b; S1 p9, my reading, S9 errata).

IC18b is an inverting summer around the +5 V bus, with R_f = R192 **33 k**:

| Input | Path | Gain (33 k/R) |
|---|---|---|
| FREQ | VR17 100 kB (between a reference rail and GND) → R186 200 k | 0.165 |
| fixed offset from the same rail | R187 680 k | 0.049 |
| VCF BEND (bender board) | R185 100 k | 0.33 |
| grip LFO MOD to VCF | R184 220 k | 0.15 |
| ENV | VR19 100 kB → R189 100 k | 0.33 |
| MOD (LFO, via IC18a follower) | VR20 100 kA → R191 100 k | 0.33 |
| KYBD | post-glide KCV → VR8 47 kB "VCF WIDTH" → VR21 100 kB → TR42 PNP follower (R195 33 k, per OML) → R193 150 k | 0.22 |

- Pin-9 divider: R128 4.7 k → [R127 5.6 k ∥ (R126 1.8 k + TH1 SDT1000)] → pin 9, with R125 820 Ω from
  pin 9 to the +5 V bus.
  - Ratio **0.111** with TH1 = 1 k, 0.088 with 10 k.
  - TH1 is a thermistor that tracks the expo's V_T: the classic tempco divider.
- My calc, at 19 mV/oct (S8):
  - **0.171 V per octave** at IC18b's output.
  - KYBD at full travel without VR8 gives 1.29 oct/V; VR8 trims it to 1.00 (it must cut ×0.78).
  - FREQ 0–5 V spans ≈ **4.8 octaves**. ENV at full depth (≈ 8–10 V) spans ≈ 15–19 octaves, i.e.
    more than the whole range.
  - With 17.9 or 20.8 mV/oct these become 5.1 / 4.4 oct (FREQ) and 1.36 / 1.17 oct/V (KYBD
    untrimmed).
- In-circuit pin-9 range with the FREQ slider: **4.90–5.01 V**, i.e. ≈ −100…+10 mV against the bus
  (Mod Wiggler p3692744, S12). That is ≈ 5.8 oct at 19 mV/oct, consistent with my 4.8 oct given
  unknown KYBD/ENV settings.
- **Key follow uses KCV only.** RANGE and KEY TRANSPOSE are not in it ("key transpose does not affect
  VCF key follow", S2 p36). The L/M/H switch *is* in KCV. KYBD is taken after the glide, so the
  filter glides with the pitch (VR8 sits on the R104 line, S9).
- Polarity: IC18b inverts, so a positive CV gives a more negative pin 9, i.e. a higher cutoff.

**Calibration and behaviour.**

- Calibration (S1 p4, Fig. 2): mixer at 0, RES max, KYBD max, ENV/MOD 0. Hold A4 and set FREQ for
  ≈ 1 kHz. Play F4/F5 and trim VR8 until F5's period is half of F4's: **1 oct/oct self-oscillation
  tracking at KYBD = 100 %**.
- Spec: cutoff 10 Hz–20 kHz; key follow 0–100 % (S1). "At its highest level, self-oscillation will
  begin" (S2). SOS 1994 says "0 Hz".
- Reid used "noise and … sub-oscillator through the self-oscillating filter tracking at 100 percent"
  (S28 2001). The SH-01A tracks 100 % (S28 2018). There are no cents-level tracking data for a real
  101.
- Comparison filters, *not the 101*:
  - Juno-106 (Syntaur, S12): 248 Hz at middle C, 992 Hz two octaves up, resonance 4.8 Vpp.
  - AM8101 Eurorack clone (adapted to ±12 V; R113/R136 10 k, R137 68 k): self-oscillates from < 20 Hz
    to > 20 kHz, 20 Vpp (S11).
  - atosynth LM13700 Juno clone, SPICE: passband −8…−10 dB at high resonance (S12).

### 4.3 Models found

**OTA one-pole vs transistor one-pole (Zavalishin §5.1, §6.9–6.10, S17).**

- Transistor ladder stage: ẏ = ω_c(tanh x − tanh y), equivalent to cutoff reduction.
- OTA stage: ẏ = ω_c·tanh(x − y), i.e. soft slew limiting. The two are linearly identical and differ
  only in where the nonlinearity sits.
- The 101 is an OTA cascade with *summing-node* inputs: tanh((V_{i−1}+V_i)/V_s).
- Flattened system y_n = g·tanh(y_{n−1} − y_n) + s_n, with y_0 = x − k·y_4, solved by
  multidimensional Newton or the "quick" methods: linearise at zero, at the operating point, or by
  secant (§6.6, credited to Voipio). Bisection needs ≈ 10 iterations for −60 dB.

**mystran, "cheap non-linear ZDF" (S22).**

- Freeze T(x) = tanh(x)/x at the half-sample-delayed state and solve the linear ZDF.
- About 4× oversampling; an optional Heun corrector roughly doubles the cost. "Works remarkably well
  for … OTA cascades."

**Huovilainen DAFx-04 (S20).**

- Each stage: dV_c/dt = (I_ctl/C)·(tanh(V_in/2V_t) − tanh(V_c/2V_t)), forward Euler, 5 tanh per sample.
- g = 1 − e^{−2πF_c/F_s}.
- A half-sample delay in the feedback keeps tuning error < 10 % below F_s/4 at 2× (88.2 kHz).
- Csound fits (S20): fcr = 1.8730fc³ + 0.4955fc² − 0.6490fc + 0.9988; acr = −3.9364fc² + 1.8409fc +
  0.9968; feedback 4·res·acr.
- This is a transistor-ladder, explicit, fit-tuned model. Not suitable as is.

**Other ladder models.**

- D'Angelo & Välimäki TASLP 2014 (S21): bilinear with an explicit delay-free-loop method. For N = 4,
  5 tanh / 26 multiplies / 24 adds / 12 delays. Exact linear cutoff; better self-oscillation.
- Oyama DAFx-26 (S21), against SPICE:
  - linear error: Huovilainen 0.116 dB, D'Angelo 0.363 dB;
  - harmonic error 0.04–0.10 % of H1, vs 0.65 % for JUCE's ladder;
  - large-signal cutoff shift ≤ 20 cents, vs 427 cents for JUCE;
  - moving the nonlinearity to the filter boundaries raises the error from 0.05 % to ≥ 0.29 %:
    **keep the early stages nonlinear**.

**OTA device models (S13).**

- Köper et al., DAFx-22 (Wasp): measured CA3080 fit I = α·I_bias·tanh(β·v/2V_T), α = 0.8635,
  β = 0.9408, plus f_H/f_L tanh terms for the output-rail plateaus.
- Bogason & Werner, DAFx-17: wave-digital OTA with linear g_m and fitted parasitics (5 pF / 700 pF /
  50 MΩ). It drifts from SPICE at high amplitude because tanh is missing.

**KR-106 (S34, GPL-3.0; Juno-6/60/106).** The only hardware-calibrated open IR3109 model.

- Structure:
  - four TPT one-poles, each with an OTA tanh via the Padé x(27+x²)/(27+9x²);
  - one Newton step per stage on y = s + g·tanh(x − y);
  - HIIR 2×/4× half-band oversampling;
  - adaptive noise to start self-oscillation.
- Fitted constants:
  - k_J6 = 0.811·(e^{2.128·res} − 1), soft-clipped above 3, capped at 6.6;
  - slopes −18.9 / −22.7 / −26.8 dB/oct at R = 3/5/7 (Juno-6 serial 193284);
  - self-oscillation level flat ±0.5 dB from 112 Hz to 14 kHz;
  - cutoff law log2 f = −12.0674 + 116 879·I(A).
- **Its resonance topology is Juno-style (BA662 feedback with input-side compensation), not the 101's.**
  Use it for behaviour checks, not constants.

**Other open filters (S34).** MAME `va_vcf` (BSD-3: Zavalishin ZDF 4-pole with one feed-forward tanh);
Faust `ve.lowpassLadder4` (MIT, linear ZDF, k = 4 − 1/Q); PicoVintage (TPT, one input tanh, k = 4·res;
tuned against Roland's plug-in, not hardware); TAL-NoiseMaker (Huovilainen-derived; "Juno-ish"
reputation is lore); Surge OB-Xd (not Roland).

### 4.4 Disagreements

| Topic | Positions |
|---|---|
| Q compensation | Present (S10; S11 AM8101 page) vs absent (S11 *All about the IR3109*). TAL keeps "VOLUME COMP" off for "real hardware" behaviour (S33). The schematic shows the R132/TR26-emitter path: present. |
| Reference voltage | +5 V (S9, S10, S1) vs +4.5 V (S11) |
| VR18 bottom end | GND (S1 drawing, S10) vs +5 V (S9 fix) |
| R125 820 Ω (pin 9 → reference) | omitted in S10 Rev. 2; present in S1 |
| R137 | 33 k (S9) vs 68 k (S11, their ±12 V adaptation) vs "68k in og" (Maffez, S36) |
| TH1 | 1 k (S10 label) vs a 10 k NTC (electro-music, for the System-100M part; Jove clone). Changes the CV ratio 0.111 vs 0.088. |
| Expo slope | 19 mV/oct (S8) vs ≈ 20.8 (OML, S9) vs 17.9 (ideal V_T·ln 2 at 300 K) |
| Expo bias at V9 = 0 | 3 µA (S9) vs 2.4 µA (implied by S8) vs 1.8 µA (AS3109 measured) |
| Pin-9 range | −140…+160 mV (S8) vs +108…−120 mV (emuarc) vs −0.36…−0.1 V (Jackdamery) |
| Bottom mirror | Wilson (S9) vs cascode (S8 Fig. 2) |
| Which half clips first with larger V_GS | OML: top (AS3109) vs source-follower analysis: bottom. Unresolved. |
| Hz/µA | atosynth SPICE ≈ 33 Hz/µA (270 pF) vs theory ≈ 93 Hz/µA. Probably they measured the cascade's −3 dB point (≈ 0.435·f_c). |
| Lineage | "a copy of the CEM3320" (Gearspace snippet) vs BA662-style cores (S9). The latter is supported by measurements. |
| Lowest cutoff | 10 Hz (S1) vs "0 Hz" (S28 1994) |

### 4.5 Recommendation: VCF

1. **States.** Four capacitor voltages relative to the +5 V bus, plus the diode-node voltage V_d
   (5 unknowns). Per stage:
   C·dV_i/dt = −g_i·I_abc·tanh(u_i/2V_T), where u_i comes from the actual resistor network. Stage 1
   includes the mixer conductances and the 15 k resonance injection. g_i ~ N(1, 2–5 %) as a hidden
   "unit" parameter.
2. **Buffers.** OUT = CAP + V_GS, where the constant drops out of the AC behaviour. Add a smooth
   asymmetric limit per stage, Wasp-style tanh rail terms (S13): lower ≈ −1.3 V, upper ≈ +2.5 V
   (stages 1/3) / +3.8 V (stages 2/4). Make V_GS a parameter: 2.9 V for the IR3109, 3.5 V for the
   AS3109.
3. **Resonance loop.**
   - V_e = −1.49·α·V4 (TR26 collector; include the TR27 V_BE as DC only).
   - Diode node: (V_e − V_d)/5.6 k = 2·I_s·sinh(V_d/(n·V_T)) + (V_d − u_1)/15 k, solved jointly.
   - C52 as a 0.77 Hz high-pass ahead of it.
   - Output to the VCA = (V4 + α·V4) through the R130/R132 sum (§6).
   - VR18 linear; bottom at the +5 V bus.
4. **Expo.**
   - I_abc = I_0·exp(−V9/(κV_T)), with κV_T·ln2 ≈ 19 mV/oct (trimmable).
   - Add OML's positive error above ≈ 10 µA and a soft cap near 700 µA.
   - Optional V_T(T) drift partly cancelled by the TH1 model.
   - Map current to pole frequency at 104 Hz/µA (240 pF).
5. **CV summing.** Implement IC18b and the pin-9 divider with the component values above, then
   calibrate:
   - FREQ such that the slider covers the spec range together with KYBD;
   - VR8 such that KYBD = 100 % gives 1.000 oct/oct of self-oscillation pitch (the service procedure);
   - optional unit spread of ±1–2 %.
6. **Solver.**
   - Trapezoidal (TPT) on all five unknowns; Newton with an O(n) Jacobian (bidiagonal plus one feedback
     column); 2–3 iterations warm-started from the previous sample.
   - Fallback: mystran/Zavalishin secant ("cheap") with an optional Heun step.
   - Prewarp each stage with tan().
7. **Rate.**
   - At least 2× at 44.1/48 kHz, because self-oscillation reaches 20 kHz. **4× default** for clean diode
     and rail clipping.
   - ADAA does not fit inside the zero-delay loop (S17 §6.13, S23), so rely on oversampling.
   - Double-precision states.
8. **Do not** use a "hot" generic tanh ladder or a Moog transistor-ladder model with k = 4·res. The
   OTA argument is only 1/123 of the node signal, and the 101's resonance is diode-limited and made up
   at the output.
9. **Validation targets.**
   - Self-oscillation onset ≈ 81 % RES.
   - V4 ≈ 1.2 Vpp at full RES (relative level only).
   - Pitch within 0–2 % below f_c.
   - 1 oct/oct tracking after the VR8 calibration.
   - Passband loss −8.8 dB at α = 0.8.
   - 10 Hz–20 kHz range.
   - A/B against the Roland plug-in and the recordings in §10.4.

---

## 5. Envelope generator

### 5.1 Facts

**It is discrete. There is no IR3R01 in the SH-101.**

- The parts legend lists IC3, 4, 5, 12, 16 and 18 as **IR9022** (a dual low-power op-amp).
- The ENV GEN block is IC16 (IR9022), TR29–TR37 and a single timing capacitor, **C59 4.7 µF/16 V**.
  Neither the diagram nor the block diagram (S1) shows an IR3R01.
- Repair technicians (S31: MW t=166240, t=94957, t=171228) describe:
  - IC16a as a comparator, C59 as the integrating cap, release discharging C59 when the gate is off,
    and the output at IC16 pin 7;
  - TL022 as the drop-in for the IR9022; TL062 also works (lower bias current);
  - a TL072 causing drones. That is consistent with a high-impedance cap node (1 MΩ pots on 4.7 µF).
- AMSynths: "dual Op Amp with transistor switches … a different topology to the contemporary Roland
  100M" (S11).
- The IR3R01 claim traces to parts-seller listings ("Roland IR3R01 EG IC Juno 6, Sh-101, Jupiter").
  AMSynths' IR3R01 work lists only the JP-8, Juno-6/60 and MKS-7 (S11, S31).

**Circuit** (S1 p9, my reading and ribbon trace, connector pins 83–92).

- Pots: A VR22 **1 MΩ (A)**, D VR23 **1 MΩ (A)**, S VR24 **100 kΩ (B)**, R VR25 **1 MΩ (A)**.
- One end of each of A, D and R joins the **C59 node** (pin 88).
- The other ends return through **R141 270 Ω (A), R164 150 Ω (D), R148 220 Ω (R)** to switching
  transistors.
- S sits between a reference (pin 90) and pin 89 (the reference/GND stub); its wiper is pin 91.
- IC16b buffers C59 to the ENV output (pin 85). IC16a is the end-of-attack comparator (R158 82 k,
  R161 33 k, R160 47 k, R156 47 k, D33/D34).
- Gate input TR29 (R151 100 k / R150 33 k); TR30, TR32, TR33 switch logic; retrigger TR36/TR37;
  trigger-mode switch S7 (GATE+TRIG / GATE / LFO); TRIG pulse IC5a/5b.
- So it is an **RC ADSR**:
  - attack charges C59 through (270 Ω + R_A) toward a target above the comparator threshold;
  - decay discharges through (150 Ω + R_D) toward the sustain voltage;
  - release discharges through (220 Ω + R_R) toward the rest level.
  - Concave attack, exponential D/R, as the manual's p29 figure shows.
- AMSynths runs its replica's original attack curve "on 12 V … the original uses 15 V … envelope
  voltage amplified by 1.25". Its output is 0 to +10 V (S11).
- My calc:
  - τ_A,min = 1.27 ms, τ_D,min = 0.71 ms, τ_R,min = 1.03 ms.
  - τ_max = (1 MΩ + R_min)·4.7 µF ≈ **4.7 s**, before pot tolerance (±20 % typical) and taper.

**Times.**

- Spec (S1): A 1.5 ms–4 s; D 2 ms–10 s; S 0–100 %; R 2 ms–10 s.
- **Measured** by Reid (S28 2018), on his own SH-101 against the SH-01A: "the disappointing three
  seconds of the SH-101" maximum attack, the same on both. The SH-01A has "slightly longer" maximum
  decay/release and "slightly different decay and release curves". One unit; method not given.
- My calc, attack threshold:
  - A 3 s maximum attack with τ_max = 4.7 s means the comparator trips at 1 − e^{−3/4.7} = **0.47** of
    the charging target.
  - The 4 s spec would mean 0.57.
  - The spec's 10 s D/R is ≈ 2.1 τ_max (≈ 88 % of the way).
- MS-1 clone, for comparison: minimum A 1 ms, D 1.5 ms (MW t=236634, S31). The MS-101 envelope is an
  "exact copy" with a TL062C and C125 = 4.7 µF (S36, [TRACE]).
- Juno-60, measured (Andy Harman, S34, MIT): attack 1 ms–3.25 s, D/R 2 ms–19.8 s, attack curve
  (1 − e^{−x})/0.632, decay e^{−3.5x}. A sanity range for a 1982 Roland RC envelope, not 101 data.

**Behaviour** (S2 pp29–35; S28 2001; S31).

- **GATE**: no new envelope on legato. If keys overlap, the **lowest** sounds (low-note priority). The
  envelope restarts only after all keys are released.
- **GATE+TRIG**: every new key gives "a complete envelope shape", with **last-note priority**. It also
  triggers "when you release a note to allow an older one to sound again" (Reid). The new attack
  starts from the *current* level, not from zero (S2 p29 figure).
- **LFO**: the envelope repeats at the LFO rate while a key is held and stops on release. The LFO
  "will always start from its head", via CLK RST (S1 p3 step 13). The figures show the envelope
  following the LFO square.
- Arpeggio: the gate stays high for the whole arpeggio. GATE+TRIG re-attacks on each clock; GATE gives
  one envelope (S2 p32). One repaired unit "blips off" between notes (MW t=166240): unresolved.
- Sequencer: one gate per step with a short gap; ties hold the gate; rests stay low (S2 p35).
- HOLD holds at the sustain level.
- "When all of the ADSR sliders are set at '0' … only a short 'click' is heard" (S2). "You can hear it
  snap shut when Release is zero" (Reid 2001).
- "Snappy" lore: "the 101 bounces" vs the MS-1 "strikes hard" (MW t=246305) [LORE].

### 5.2 Models found

- MAME `va_eg.cpp` (S34, BSD-3): an OTA-driven capacitor envelope v′ = (I_abc/C)·tanh(ΔV/2V_T),
  integrated trapezoidally; also an RC EG. Good patterns, but not the 101's topology.
- KR-106 (S34): Juno-6 IR3R01 data. Wrong chip for the 101.
- TAL-BassLine-101 (S33), mwAudio101 (S34): generic ADSRs; mwAudio101's times are invented.
- **No open model of the SH-101's own envelope exists.**

### 5.3 Disagreements

- **Envelope chip:** IR3R01 (parts sellers, lore) vs IR9022 op-amps plus transistors (S1, S11, S31).
  **Settled: discrete.**
- **Maximum attack:** 4 s (spec, copied by the MS-101 spec sheet, the Roland plug-in and clones) vs
  ≈ 3 s measured (Reid).
- **MS-101 curve:** owners call it "linear"; Maffez's scope says "not linear at all" (S36).
- **GATE-mode priority:** low-note (S2, Reid) vs "high-note" (a KVR user, S31). The KVR description
  actually shows low-note priority.

### 5.4 Recommendation

1. **Circuit-level RC model, at audio rate (cheap):**
   - one state v (C59) with three switched conductances, each 1/(R_min + R_pot(θ)) and targets
     V_att, V_sus(θ_S), V_rest;
   - a comparator with hysteresis ends the attack at V_th;
   - ENV out = v (IC16b follower).
2. **Calibrate:**
   - the V_th/V_att ratio so that maximum attack ≈ 3 s (Reid), keeping the 4 s spec as an option;
   - the 1 MΩ A-taper law (measure one pot if possible, otherwise a standard 10 % log law);
   - the minimums so that all-zero settings give the documented click (≈ 1–2 ms segments).
3. **Trigger logic in the CPU model (§9):**
   - GATE = low-note priority, legato without retrigger;
   - GATE+TRIG = last-note priority, retrigger on press and on release-to-older-note, attack from the
     current level;
   - LFO = gate follows the LFO square while a key is held, with the LFO phase reset at key-down.
4. Model the trigger pulse (IC5) as a short (≈ 1 ms, to be confirmed) forced-attack window that restarts
   the comparator state.

---

## 6. VCA (BA662A)

### 6.1 Facts

**The chip** (no Rohm BA662 data sheet survives. Sources: OML reverse engineering and clone, S9; Alfa
AS662D and CoolAudio V662A data sheets; Rohm BA6110 sibling data sheet, S15; forum measurements,
S16):

- **Structure.**
  - Control current enters a 2-transistor Widlar mirror (≈ 400–500 Ω emitter resistors), so pin 1 sits
    ≈ 0.6 V above V−.
  - NPN differential pair with PNP and NPN Wilson mirrors. **No linearising diodes.**
  - Buffer: complementary NPN→PNP follower with an internal 10–11 kΩ pull-up; pin 4 sets its bias.
- **Pins (SIP-9):** 1 I_ctl, 2 −in, 3 +in, 4 buffer bias, 5 V−, 6 OTA out, 7 buffer in,
  8 buffer out, 9 V+.
- **Transconductance and output current, at I_abc = 500 µA:**

  | Part | g_m | Peak output current |
  |---|---|---|
  | AS662 | 8.5 mS (7.7–9.1) | 450 µA typ |
  | BA6110 / V662A | 8.0 mS (4.8–12) | 500 µA |
  | Ideal I/2V_T | 9.7 mS | |

  So **k_m ≈ 0.9**. Roland sorted parts into 9 g_m colour grades (S11).
- **Linearity:** ±5 mV for < 1 % g_m error; full switching at ±125 mV (S14). AS662 THD 0.25 % typ
  (1 % max) at 200 µA and 5 mVrms, consistent with a bare tanh (H3 ≈ u²/12).
- **Offset:**
  - BA662A measured 30 µV (selected, from a TB-303), 200 µV and 250 µV; AS662D ≤ 250 µV (white dot)
    or 700 µV; BA6110/V662A ≤ 3 mV.
  - The "A" suffix is offset selection (S16).
  - Input bias ≈ 150 nA. Buffer offset −5 mV (selected) / +25 mV.
- **Noise and leakage:**
  - Residual noise −94 dBm at zero control, −74 dBm typ (−66 max) at 200 µA (BA6110/V662A).
  - Off-state leakage −94 dBm typ (−75 max) for −30 dBm at 20 kHz.
  - LM13700 trimmed CV bleed-through −68 dB re 1 Vpp (OML, S9).
- **Limits:** supply ≤ 34–36 V total; pin 6 swing ±12–14 V; buffer swing ±9–11 V into 47 k; buffer
  load ≥ 33 kΩ. Maximum I_abc 500 µA (BA6110), 650 µA (V662A), 2 mA (AS662).
- **Where Roland used it** (S15, S16, S11):
  - documented: TB-303 and SH-09 VCAs; SH-2 / ProMars / Jupiter-4 filters and portamento; Juno-6/60
    resonance amp; Juno-106 80017A (2× BA662F); JX-3P HPF;
  - lore: Jupiter-8 voice, 100M, TR-808/909.

**SH-101 circuit** (S1 p9, my reading; S15 crop).

- IC15 BA662A ("offset selected", white dot).
- Input: from the R130/R132 150 k sum (§4.2) through C53 10 µF bipolar to pin 3. Pins 2 and 3 each
  have 1 kΩ (R138/R139) to the reference bus. This matches the advice that the diode-less BA662 wants
  ≈ 1 kΩ input impedance (S16).
- Output: pin 6 → R140 47 k → buffer pin 7 → pin 8 → VOLUME (bender board). Pin 4 is tied to the bus.
- Control: pin 1 ← TR28 collector; base via R133 12 k. Selected by S6 ENV/GATE (R197 3.3 k,
  R196 120 k).
- Nearby: R41 33 k from the reference bus (C56 10 µF), C57 47 nF to GND, R142 12 k → TR30 (R145 10 k,
  R144 100 k), R147 220 k. My calc: these could smooth the GATE-mode control with
  τ ≈ 0.4–1.6 ms. **Whether they sit in the VCA control path is unverified.**
- No offset trim is drawn. Two forum sources mention an unpopulated trimmer position on the PCB
  ("100K trimpot … 5M resistor in R201"; Altitude909, S16).
- GATE mode gives "a fixed volume as long as you hold down the key" (S30). Reid: rectangular, but "no
  VCA responds instantaneously … slight lag" (S28 2001).
- Click causes reported (S16): DC offset (Don T) and the abrupt turn-on in GATE mode or with zero
  attack (Solderman). Maffez's MS-1 de-click mod (220 nF) aimed at "the SH-101's softer onset" (S32)
  [LORE].

**Drive level** (my calc).

- The VCA input divider is ≈ **1/152** at RES = 0 (R132's end is AC-quiet) and ≈ **1/76** of the
  out+emitter sum at RES = 1.

| VCF output | RES | OTA drive | u | H3 ≈ u²/12 |
|---|---|---|---|---|
| 0.72 V pk (saw at max level) | 0 | 4.7 mV | 0.09 | 0.07 % |
| 0.72 V pk | max | 18.7 mV | 0.36 | 1.1 % |
| 0.6 V pk (simulated self-oscillation, §4.2) | max | ≈ 15.6 mV | 0.30 | ≈ 0.8 % |

- The BA662 agent's independent estimate is 13–30 mV peak, 0.5–3 % (S15).
- **Distortion is independent of the envelope level**, because the tanh argument does not involve
  I_abc.

### 6.2 Models

- **OTA VCA (S9 OML eq. 36, S14):**

  ```
  i6 = k_m·I_abc·tanh((a·v_in + V_os)/2V_T) + I_abc·δ        a = input divider, δ = mirror mismatch
  v6 = R140·i6 → (soft clip at compliance) → unity buffer
  thump (DC) = R140·I_abc·(k_m·tanh(V_os/2V_T) + δ)          follows the envelope; shaped by downstream AC coupling
  H2/H1 ≈ V_os·V_pk/(2(2V_T)²)
  ```

  OML: you can trim out either bleed-through or second harmonic, not both, and the null moves with
  I_abc.
- KR-106 (S34): VCA law from a LUT of the Shockley equation for 2SA1015 + 22 k. Measured on a
  Juno-106 with clone chips.
- MAME `va_vca` (S34, BSD-3): CA3280 OTA VCA skeleton.
- Huovilainen DAFx-05 (S13): Euler OTA stage with input averaging, 2–4×.

### 6.3 Disagreements

- **g_m:** 8.0 / 8.08 (AMSynths "8080", no units) / 8.5 mS against the ideal 9.7 mS.
- **"A" grading threshold:** OML guessed ≈ 1 mV; measured "A" parts are 30–250 µV.
- **V662A data sheet:** copies BA6110 text on input impedance and suggests 100 kΩ on pin 4. The 101
  ties pin 4 to the bus.
- **Does the VCA chip matter to the sound?** "Little" (guest, S16) vs "holy grail" (Behringer and
  Peake marketing).
- **R138/R139 pin assignment:** read differently by two agents. Both are 1 kΩ, so it does not matter.

### 6.4 Recommendation

1. Memoryless OTA model inside the oversampled block:
   - y = R140·k_m·I_abc·tanh((a·v + V_os)/2V_T), with a from the actual R130/R132/R138 network
     (RES-dependent);
   - V_os default +0.2 mV (unit spread 0.03–1 mV), k_m = 0.9, δ = 0.
   - At 2× this is fine; alternatively use first-order ADAA on tanh (antiderivative 2V_T·ln cosh, S23).
2. Control: I_abc from ENV (or GATE) through TR28. Use a smooth diode knee until the netlist confirms
   the TR28 topology. Add the GATE-mode smoothing only once its position in the circuit is confirmed.
3. Thump: keep V_os and δ so the envelope-shaped DC appears, then pass it through the real downstream
   coupling (§8). It should be small: about −60 dB relative to full signal for V_os = 0.2 mV.
4. Noise: output noise ∝ √I_abc, calibrated to ≈ −74 dBm at 200 µA (BA6110 figure). Default low.

---

## 7. LFO / modulator and bender

### 7.1 Facts

**Core** (S1 p9, my reading; S11).

- IC3a integrator with C9 **0.1 µF**, IC3b Schmitt trigger (R30 10 k, R29 10 k), giving triangle and
  square.
- TR4 steers the integrating current (R15 120 k, R17 47 k, R16 15 k, D9, D10). TR5/TR6 are the square
  drivers (R28 220 k, R25 33 k, R26 220 k).
- RATE: VR10 **100 kΩ (A)** + R168 220 Ω on the control board. The LED driver TR40 makes the LED blink at
  the LFO rate.
- "LFO range set by R15" (S35 mods).
- My calc: the rate-resistor ratio is (100 k + 220)/220 = 455:1, or ≈ 250:1 with R15 120 k in parallel.
  That fits the 300:1 of 0.1–30 Hz if f ∝ 1/R (topology not fully traced).
- **TR7 (R36 100 k) is the CPU's CLK RST**, which restarts the LFO. CLK RST fires on key-down when
  GATE/TRIG = LFO or ARPEGGIO is on (S1 p3 step 13; S2 "always start from its head").
- The LFO square (or EXT CLK, ≥ +2.5 V) is the CPU clock: T1 via TR11 (S1). A jack in EXT CLK cuts the
  internal clock, but the LFO keeps running for modulation (S2 p38).

**Waveform switch S2.**

- Triangle, square, **RANDOM** and **NOISE** (R169/R170 47 k, R175 33 k).
- **RANDOM is CPU-generated, not sampled noise.** On each clock rising edge the CPU makes a random
  number and writes it to the 6-bit DAC (S1 p3 steps 6–7; S26). IC11a doubles it to a **0 to +10 V**
  stepped signal with 64 levels (S11). Manuals and the press call it "S/H" (S2, S28).
- NOISE as a modulator: R172 1 k / C60 10 nF, **16 kHz** low-pass (my calc; S11).
- After the switch: R173 100 k, R174 2.2 M, C61 10 µF bipolar coupling. My calc: ≈ 0.16 Hz into
  100 k.

**Destinations.**

- VCO MOD VR11 100 kA → pin 15 via the IC12b mixer. IC12b's 2.34 kHz feedback pole rounds the square's
  edges slightly (my calc: τ = 68 µs).
- VCF MOD VR20 100 kA via IC18a (buffer) → R191 100 k.
- PWM via S4 (LFO / MAN / ENV).

**Bender board LFO MOD** (§2.1): sine-shaped triangle (TR2 pair), fade-in τ ≈ 0.47 s by
components (my calc) vs "fixed at 1.5 s" (S11) vs "very slight built-in delay" (S29). VCO and VCF
depths via the bender sliders. Bends upward only on the MG-1 grip.

**Other.**

- Low batteries cause LFO bleed into the VCO (MW t=94957, S31).
- The press often calls the LFO "sine" (SOS 1994, Roland AU); only the bender path is sine-shaped.
- TAL offers VCO MOD up to ±24 semitones: an emulator choice (S33). LabSeven: ±5 V → "±2.25 oct like
  the original synth" (unverified, S34).

### 7.2 Disagreements

- **Low end:** 0.1 Hz (S1) vs 0.35 Hz (AMSynths store page; clone artefact, S36).
- **RANDOM:** S&H of noise (press) vs CPU-generated 6-bit values (S1, S26, S11). The latter is
  authoritative.
- **Grip fade-in:** τ 0.47 s (components) vs 1.5 s (S11). Probably the same thing measured to ≈ 95 %.

### 7.3 Recommendation

1. **Triangle/square core.**
   - A phase-accumulator LFO with an exponential (A-taper) rate law fitted so that 0.1–30 Hz spans the
     slider. Measure the real taper if possible.
   - Triangle and square with the Schmitt's slight slew.
   - Phase reset on CLK RST (key-down in LFO-trigger or arpeggio mode).
2. **RANDOM.** On each rising clock edge (LFO square or EXT CLK) draw a uniform integer 0–63 from a
   CPU-like PRNG (the exact algorithm is for the firmware track). Output = 10 V·n/63, held, with the S&H
   settling (≈ µs).
3. **NOISE.** The VCO's noise source through the 16 kHz low-pass and the 0.16 Hz coupling, at audio
   rate.
4. Run the LFO at control rate (≥ 1 kHz), except NOISE and the square/RANDOM edges into the VCO, which
   need audio-rate stepping (or polyBLEP-smoothed steps) to avoid zipper noise.
5. **Grip LFO MOD.** Sine-shaped triangle, one-pole fade-in (default τ = 0.47 s), depth from the
   bender's LFO MOD slider.

---

## 8. Output stage, power rails and references

### 8.1 Facts

**Output chain** (S1 p7, my reading).

- BA662A buffer (pin 8) → bender-board **VOLUME VR1 100 kΩ (A)** → IC1a (M5218) follower → C3
  **100 µF** → jack board A.
- On jack board A: R30 100 k to GND, R29 2.2 k series, C12 1 nF to GND, R28 1 k series → OUT.
- Power-on mute: TR7 (R35 47 k) shunts the R29/R28 node; TR8, D3–D5, R36 220 k, R37 10 k, C13 10 µF,
  C14 47 µF.
- PHONES: IC1b follower → C4 100 µF → R31/R32 82 Ω (R33/R34 to GND).
- My calc:
  - Output LPF 2.2 k·1 nF = **72 kHz**.
  - Coupling HPF 100 µF into 100 k = 0.016 Hz; into a 10 k load ≈ 0.12 Hz.
  - Source impedance ≈ **3.2 kΩ** (2.2 k + 1 k after the op-amp).
  - **No audible high-pass in the output stage.** The relevant LF corners are upstream: VCA C53 into
    76 k ≈ 0.21 Hz; resonance C52 0.77 Hz; mixer C45 < 0.05 Hz. All are far below the audio band, unlike
    the 303's chain.
- Spec: audio out **0 dBm max** (0.775 Vrms); phones 8 Ω (S1); 8–15 Ω (S30 brochure).

**Interfaces** (S1, S2, S32).

- GATE out 0/+12 V (S1, brochure) vs "10 V at 100 kΩ" (S2).
- GATE IN and EXT CLK ≥ +2.5 V.
- CV OUT 1 V/oct, 0.415–5 V. CV IN 0–7 V (S2). Kenton: plays only ≈ MIDI 36–96 through CV IN.
  Synhouse MIDIJACK: "10½ octaves" with its own DAC.

**Power** (S1; S32; S35; S31).

- 6 × 1.5 V cells or 9–12 V DC; 1 W.
- A DC-DC converter (T1, TR1 2SD571, TR2) raises the supply: **≈ +21 V measured, regulated to +15 V
  (14.87 V measured)** (Synhouse, S32). sibomots: ≈ +21 V / −9 V (S35).
- Rails named on the drawing: +15 V (+14 V), +9 V (battery), +5 V, +4.5 V (logic), −5 V.
  - **+5 V reference:** IC1b M5218 + zener D4 + TR3, trimmed by VR1 "D/A WIDTH". My calc: ≈ 4.93 V for
    an exact 1/12 V DAC step (§2.1). It is also the analog signal reference for the VCO offset, VCF,
    VCA and CV summers.
  - **−5 V:** IC2b inverting the reference (R6/R12 10 k, 1 %).
  - S11 says the rails are "all … above GND (GND, +5V, +14V)". That misses the −5 V rail visible on the
    drawing.
- One pole of the power switch also feeds the DAC's +5 V reference (MW t=99094, S31).
- Digital noise: a Tubbutec grounding mod gives ≈ 10 dB less (S27). An owner reports an "RF or digital
  leak" after an output-jack repair (S26 comments). Low batteries leak LFO into the VCO (S31).
- "Very stable tuning — weeks" (S28 1994) [LORE]. The CEM3340's V_T-cancelling law and the TH1 VCF
  tempco support good stability.

### 8.2 Recommendation

- **Output.** Model the output stage as a gain with a 72 kHz one-pole (or omit it) and no HPF. Keep the
  upstream couplings (C45, C52, C53) as one-poles; they matter for thump and DC shifts, not for tone.
- **Level.** Calibrate so a full-level saw with the filter open sits a fixed margin below 0 dBFS, with
  0 dBm max corresponding to about −6 dBFS. Expose an output trim. No published absolute output-level
  measurement exists; measure one from recordings.
- **Reference rail.** Model the +5 V reference as a parameter shared by the DAC LSB, the VCO offset
  (IC12b) and the VCF/VCA biases, so a "D/A WIDTH" mis-trim shifts pitch the way the real unit does
  (optional "vintage" control).
- **Hum and digital noise.** Optional, default off: a CPU-loop-rate tick (1.5–3.5 ms period) at
  −80 dB and battery-sag LFO bleed.

---

## 9. CPU-side timing that affects the sound

### 9.1 Facts

- **Loop.** Steps 3–13 repeat every **1.5–3.5 ms** (S1 p3). Key-to-gate latency and the jitter of
  gate, CV and clock response are therefore up to one loop (S1, S27).
- **Order within a loop:** range read → range output → keyboard read (4 × 8 matrix) → clock check
  (T1) → random output → function switches → LOAD/PLAY/ARPEGGIO → CV output → gate & LED output. So **CV
  is written before the gate**. That matters for the AUTO-portamento gate-edge pulse (§2.1): the new
  CV is already present when the glide is shorted.
- **Note priority** (S2, S28):
  - GATE: lowest-note priority, no retrigger while any key is held.
  - GATE+TRIG: last-note priority; retrigger on every press and on release back to an older held
    note.
  - LFO: retrigger at the LFO rate.
- **Clock** (S1 p3):
  - a low→high transition at T1 (LFO square or EXT CLK, inverted by TR11) makes the CPU generate random
    data and step the arpeggio/sequencer;
  - CLK RST restarts the LFO on key-down in LFO-trigger or arpeggio mode, so the first arpeggio step
    and the first LFO cycle line up with the key press.
- **Arpeggio** (S2 p32, S27):
  - UP, U&D, DOWN, from a 32-key (4-byte) buffer;
  - the gate stays high for the run; GATE+TRIG gives one attack per clock;
  - portamento in ON only (p32 vs p31 conflict);
  - a single held note plays normally (the SH-1oh1 adds single-note retrigger "unlike the original");
  - the arpeggio clock comes from the LFO square, which is restarted at key-down.
- **Sequencer** (S1, S2 p35, S31):
  - 100 steps (S1); "256 notes" (Nagle, S28) is a dispute;
  - rests and ties (LEGATO); one gate per step with a short gap before the next step, ties hold the
    gate;
  - L/M/H is inactive during playback, but the sequence can be transposed live;
  - drops out of LOAD when full; with AUTO portamento, LEGATO steps slide;
  - the sequencer cannot be clocked from the LFO on the MS-101 (a difference reviewers note, S36).
- **KEY TRANSPOSE** shifts RANGE (not KCV), so it does not move VCF key follow (S1, S2). It is "not
  permanent" (Anwander adds a latch, S32).
- **Gate path:** CPU gate → GATE OUT jack (0/+12 V) → GATE IN switching jack → envelope trigger logic
  and the portamento AUTO differentiator (S31, S27).
- The firmware detail (exact gate gap, PRNG, priority tie-breaks, LOAD/HOLD behaviour) belongs to the
  firmware track (`docs/firmware.md`; the joebritt disassembly, not downloaded here).

### 9.2 Recommendation

1. Implement a CPU *behaviour* model that schedules events sample-accurately but can quantise them to a
   simulated loop (default 2.5 ms; option "authentic jitter" 1.5–3.5 ms).
2. Order each loop as CV → gate, as the firmware does, so AUTO portamento and envelope retriggers line
   up.
3. **Host-MIDI mode.** Map MIDI notes onto the 32-key logic (with L/M/H and RANGE), keep the mode
   priorities, and treat overlapping notes as legato. Expose the original priorities as the default.
4. **Arpeggio/sequencer clock.** The LFO square (or host-synced EXT CLK). Clock edges must reach the
   arpeggiator, the RANDOM S&H and the GATE+TRIG retrigger at the same instant.

---

## 10. Reference implementations, clones and product context

### 10.1 Open-source code (S34; read as text only; licences quoted in each folder's README)

| Project | Licence | Use for us |
|---|---|---|
| KR-106 (Ultramaster, Juno-6/60/106), `kr106_ultramaster/` @ bc15caee | GPL-3.0 | Behavioural cross-check of an IR3109 OTA cascade (slope vs resonance, self-oscillation level, cutoff-law form). **Do not copy code or constants** (licence; Juno topology). |
| MAME VA devices, `mame_va_devices/` @ c6eac87a | BSD-3-Clause per file | CEM3340 control-law skeleton (`cem3340.cpp:76-91`), PolyBLEP/BLAMP with sync (`va_vco.cpp`), ZDF 4-pole (`va_vcf`), OTA/RC envelopes (`va_eg.cpp`), OTA VCA (`va_vca`). Reusable with notice and no-endorsement clause. MAME has **no SH-101 driver**. |
| LabSeven VCO-3340 (VCV Rack), `vcv_labseven_ls3340/` @ b74c73a8 | CC-BY-NC-SA-4.0 | Behavioural reference only (sub modes, PW ranges). Not usable commercially; its "3340 impulse" and jitter are unverified. |
| Faust `ve.lowpassLadder4`, `faust_vaeffects/` @ bf9f71af | MIT (this function) | Linear "golden" reference for an equal 4-stage cascade |
| Surge sst-filters, `surge_sst_filters/` @ 34316968 | GPL-3.0-or-later | Generic only (OB-Xd); no Roland model |
| TAL-NoiseMaker, `tal_noisemaker/` (DISTRHO-Ports @ d3b62da2) | GPL-2.0-only | Lore only; Huovilainen-derived |
| JunoX / Juno60 notes, `junox_andyharman/` | GPL-3.0 / MIT | **Measured Juno-60 envelope** times and curves (MIT notes) as a sanity range |
| PicoVintage (PicoFaceJ6), `pico_vintage_juno/` @ 4c49010f | GPL-3.0-or-later | Tuned to Roland's plug-in, not hardware |
| mwAudio101, `mwaudio101/` @ 0be235fe | GPL-3.0-or-later | Every constant marked "pragmatic invention"; its `13-validation-gaps-and-disputes.md` is a useful ledger of disputes |

Not saved (see `opensource/found_not_saved.txt`):

- joebritt/SH101_Disassembled: no licence, contains Roland's ROM; firmware track.
- jn60c99: decompiled Roland binary; excluded for clean-room reasons.
- Several SH-101-titled hobby projects with generic DSP: pedal-sh101 (GPL-3), SH1012 (no licence;
  k = 5.2·res, "IR3109 clipping diodes" unsourced), SH-010 (BSD-3, Max `lores~`), Pd/Python toys.
- reubenwbetts/BA662A-VCA: LTspice, no licence, wrong transistors.
- VCV library scan (567 plugins): no open IR3109 or SH-101 filter and no SH-101 arpeggiator.

Licence implications for a closed-source plug-in (general guidance, not legal advice):

- GPL code: read for ideas only; do not copy or translate.
- Measured facts: cite them.
- BSD-3 and MIT: reusable with the notices.
- CC-BY-NC-SA (LabSeven; Electric Druid's filter docs): no commercial reuse of code or assets.
- No licence: facts only.
- Keep the implementation clean-room with respect to Roland binaries and ROMs.

### 10.2 DIY clones and mods with values (S35, S9, S10, S11, S27)

- **OML / Superlative SB01** (`diy_openmusiclabs_sb01/`, `diy_superlative_sb01/`):
  - service-manual errata: R137 33 k; R195 33 k; R31/R35 47 k; R24 82 k; C15 10 nF; one "R99" is
    R199; one "R41" is R143; R162 100 k missing; **VR18 to +5 V**; R104 to VR8;
  - measured BA662 internals and a discrete clone;
  - SB01 manual: exact GATE / GATE+TRIG / LFO behaviour; RANDOM = CPU S&H per clock; lowest key at
    8′/M = middle C.
- **Electric Druid AS3109 SH-101 filter Rev. 2** (CC BY-NC-SA): core values, mixer values, resonance
  and expo-CV networks. Its drawing omits R125, puts the RES pot bottom at GND and uses 100 k for the
  KYBD follower; OML says 33 k.
- **AMSynths AM8101 / 8110 / 8112 / 8114** (±12 V redesigns): R137 68 k to −12 V, R113/R136 10 k,
  R135 10 Ω. ENV output scaled ×1.25. LFO waveforms normalised to ±5 V.
- **Tubbutec SH-1oh1:** CPU replacement and the authoritative AUTO-portamento explanation; DAC test
  point R73.
- **sibomots/RolandSH101** (BSD-3): full BOM transcription, with typos.
- **Mods:**
  - VCF FM injection at R184–R187; external audio at R109–R112 (Nova-mod, circuitbenders, S35);
  - Kenton filter-socket kit (0–5 V VCF CV, S32);
  - Synhouse MIDIJACK (S32);
  - E&MM 1985 foot-pedal mod (S29).
- **Bottom line:** no DIY or Eurorack source publishes measured cutoff-vs-CV, resonance, envelope or
  sub-level curves from an original SH-101.

### 10.3 Behringer MS-101 / MS-1, Roland SH-01A and Roland's ACB SH-101 (S36, S28)

**Behringer MS-101 (MK1)** [TRACE: Maffez guide v2 and traced VCF schematic].

- VCO: a 3340 on a sub-module; triangle and filter FM added; wider PW range.
- **VCF: not an IR3109.** Two VM13700 (LM13700) chips. Each stage: 68 k in, 68 k local feedback,
  560 Ω to +5 V, 240 pF, a JFET follower and a 13700 buffer. One PNP expo feeds all four bias pins
  through 10 k. The resonance stage uses 33 k ("68k in og"). Maffez: "more like the SH09 filter than the
  101".
- **VCA:** VM13700 instead of the BA662.
- **ENV:** an "exact copy" with a TL062C and a 4.7 µF timing cap.
- Mixer 680 k / 470 k / 470 k / 820 k. Portamento done by the CPU.
- Reviewers A/B against an SH-101 [REVIEW-AB]: filter "a tad brighter/harsher"; decay/release
  "diminish pretty quickly"; no always-on glide; sequencer cannot be clocked from the LFO.
- Firmware 1.0.6 restores OFF/AUTO/ON; 1.0.7 adds note priority "following the original" [MFR].
- MKII "based on the original 662 chips" [MFR]; Synth Anatomy reports it as the CoolAudio V662A.
- The published specs restate the 1982 sheet; they are not measurements.

**Roland SH-01A, System-1 PLUG-OUT, Roland Cloud plug-in, ZEN-Core Model Expansion, FANTOM ACB.**

- Roland's claims [MFR]: modelled "right down to the component level" with input from the original
  engineers. Published specs copy 1982.
- Plug-in extras not in the hardware: separate filter and amp ADSRs, sine and saw LFO, 64′/32′ ranges,
  effects, and an arpeggiator that runs with a single held note.
- ZEN-Core: filter curves R/M/S and an "ORIGINAL" portamento curve.
- FANTOM ACB: CONDITION (ageing) and CIRCUIT MOD (a calibration-trimmer simulation).
- SH-01A: 4 voices; Noise and LFO each have "Original" and "Variation" settings.
- **Reid, SOS 2018, SH-01A vs his SH-101** [REVIEW-AB]: "usually able to make the two synths sound
  indistinguishable" apart from aliasing at the top.
  - Maximum attack ≈ 3 s on both (spec 4 s).
  - Decay/release slightly longer, with slightly different curves.
  - Keyboard tracking 100 %.
  - Pitch and cutoff are audibly stepped (controller quantisation).
- **Nagle, SOS 2014, PLUG-OUT** [REVIEW-AB]: warbles at the highest pitches; lag when moving cutoff;
  high resonance "harder and more edgy" rather than "plummy"; S&H does not lock to the clock.
- Users [LORE]: SH-01A resonance weak above ≈ 75 % (≈ 12 dB level drop); envelopes "the REAL DEAL".
- **Implication for A/B against the Roland plug-in.**
  - Its envelopes and tracking are reported close to hardware.
  - Its top-octave aliasing, controller stepping and high-resonance timbre are reported to differ.
  - So use recordings of real units (§10.4) as the referee for resonance and top-end behaviour.

### 10.4 Real-unit recordings for A/B (S31 and agents; URLs only, nothing large downloaded)

- **Mr Card Synths free SH-101 pack:** 73 WAVs of raw waveforms, noise and FX, "No EQ. No compression".
  mrcardsynths.com/product/roland-sh-101-sample-pack/ (mrcard.gumroad.com/l/sh101samples). Best raw
  reference.
- **Liveschool free SH-101 multisample pack**, including raw cycling waveforms (via
  rekkerd.org/liveschool-releases-free-roland-sh-101-pack-live/).
- **Rob Puricelli (Failed Muso), SH-101 vs MS-101**, ≈ 1 h video: youtube.com/watch?v=6NqaegXGbtE.
- Robert Babicz free waveforms (Gearspace thread 555511; recorded through outboard gear); SampleFocus
  "Wave Tables – Roland SH101".
- The owner's manual's 39 patch charts (S2 pp42–53) as standard A/B settings. Reid analyses the
  Trumpet and Tuba patches (S28 2001).
- Not usable: dead VSE 2008 MP3 links (from a faulty unit), private SoundCloud.

---

## 11. Table of numeric constants

| Quantity | Value | Source | Notes |
|---|---|---|---|
| **CPU / DAC / S&H** | | | |
| CPU | TMP80C49P-6-7301, 6 MHz resonator | S1, S31 | |
| CPU loop (steps 3–13) | 1.5–3.5 ms | S1 p3 | |
| DAC | 6-bit binary-weighted: 30.1 k + 2 k trim, 62.5 k, 125 k, 250 k, 500 k, 1 M; R_f 16.9 k ∥ 47 pF; offset 15.4 k + 1 k trim from −5 V | S1 p9 | |
| DAC LSB | 84.5 mV untrimmed at 5.000 V; 83.33 mV (1/12 V) at V_ref = 4.931 V | my calc | |
| DAC full scale | 63 LSB = 5.25 V | my calc | |
| Test-mode KCV/RANGE | PLAY 2.75/0; ARP DOWN 2.5/0; U&D 4.75/0 (gate on); UP 0/4.75 (gate on); LOAD 0/0; ±1 mV | S1 p3–4 | |
| S&H | 4052 + 3 × 0.015 µF + TL062; R72 220 Ω (τ 3.3 µs) | S1; my calc | |
| RANGE data | 16′ 1 V, 8′ 2 V, 4′ 3 V, 2′ 4 V (+ key-transpose shift) | S1 p3 | |
| KCV | semitone/12 V; L/M/H lowest F 0.417/1.417/2.417 V; H top C 5.0 V | S1 p3 | |
| RANDOM | 6-bit uniform per clock edge, ×2 → 0…+10 V (64 steps) | S1, S26, S11 | |
| CV OUT / CV IN / GATE | 0.415–5 V (no glide) / 0–7 V / 0 or +12 V (S1) vs 10 V (S2) | S1, S2, S31 | |
| **Pitch** | | | |
| Calibration | 4.75 V total ↔ 442 Hz; 0 V ↔ 16.43 Hz (16.35 Hz at A = 440) | S1 p4; my calc | |
| Keyboard at 8′/M | F3–C6 (lowest C = middle C) | S2; my calc | |
| Total range | F1 43.9 Hz (16′/L) … C9 8.41 kHz (2′/H); ≈ 29 Hz … 16.8 kHz with key transpose | my calc; S2 (16.82 kHz) | |
| Bender (VCO) | up to ≈ ±1200 cents | S2 | |
| **Portamento** | | | |
| Circuit | VR2 2 MΩ (A) + C10 0.33 µF + TL062; JFET 2SK30A bypass | S1 p7 | |
| τ | ≤ 0.66 s; 2-oct → 1 cent in 5.1 s; 5τ = 3.3 s | my calc | spec 0–5 s (S1); 0–3 s (S30) |
| AUTO | gate-edge pulse shorts the glide; R15 47 k / C11 4.7 nF (τ 0.22 ms) | S27; S1; my calc | |
| **VCO (CEM3340)** | | | |
| Law | f = f_ref·2^((I_C − 3/R_Z)/I_oct); f_ref = 3·I_REF/(2·V_CC·C_F) | S3 | |
| SH-101 parts | R_T 5.6 k, R_Z 28 k, R_S 1.69 k + 300 Ω trim, R_REF 1.5 M (I_REF 10 µA), C_F 1000 pF, V_CC 15 V, V_EE −5 V | S1 p9 | |
| Derived | f_ref 1.00 kHz; R_S(1 V/oct) 1.89 kΩ; I_oct 10 µA; trim 0.894–1.053 oct/V | my calc | |
| Pin-15 inputs | R99 97.6 k + 5 k (RANGE), R104 100 k (KCV), R103 100 k (IC12b ≈ +5 V idle → ≈ 50 µA offset), R102 1.5 M (+15 V, optional), R108 470 k (VR7, 0…−10.6 µA), R100 1.5 M (HF) | S1, S9; my calc | |
| HF tracking | ΔI_C = I_EG/600: +2/+10/+20/+40 cents at 1/5/10/20 kHz | S3; agent calc | fixed resistor |
| Waveforms | saw 0–10 V (9.4–10.6), Z_out 65–150 Ω; triangle 0–5 V (unused); symmetry 45–55 % | S3 | |
| Pulse | chip: 0 % at 0 V, 100 % at 5 V on pin 5; SH-101: inverted by TR24 to 0…+14 V | S3, S1 | |
| PWM range | 50 % → 0 % (spec); ≈ 5 % minimum in practice | S1, S11 | |
| Specs | sweep ≥ 50 000:1; tempco ±150 ppm max; drift ±50 ppm typ; f_max ≈ 40–80 kHz at 1 nF | S3 | |
| **Sub-osc** | | | |
| Clock | TR41 from the saw via 220 k/33 k → trips at 4.6 V; 4013 toggles at the saw wrap | S1, S5; my calc | |
| Waves | −1 oct square, −2 oct square, −2 oct 25 %-low pulse (diode-OR); 0–14 V | S1, S5 | |
| **Noise** | | | |
| Source | 2SC945 selected; IC12a R_f 220 k ∥ 240 pF → 3.0 kHz pole | S1; my calc | |
| LFO-path noise | 1 k / 10 nF → 15.9 kHz | S1; S11; my calc | |
| **Mixer** | | | |
| Resistors | noise 150 k, pulse 680 k, saw 470 k, sub 820 k → IC14 pin 2 (68 k feedback) | S1, S10 | |
| Gains | 0.453 / 0.100 / 0.145 / 0.083 (68 k/R) | my calc | |
| Coupling | C45 10 µF bipolar | S1 | |
| **VCF (IR3109)** | | | |
| Network | 68 k / 68 k / 560 Ω per node; 240 pF per stage; R_p = 550.9 Ω; u = (V_{i−1}+V_i)/123.4; V_s ≈ 6.34 V | S1; agent; my calc | |
| Cutoff | ≈ 104 Hz/µA (pole); 1 kHz ≈ 9.6 µA; 20 kHz ≈ 200 µA; 10 Hz ≈ 0.1 µA | my calc; agent | |
| Expo | −19 mV/oct (−17.5…−20.5) (S8); ≈ 20.8 mV/oct (S9); 0.33 %/°C; saturates ≈ 700 µA; +1…+10 % error at 12–500 µA | S8, S9 | |
| Supply / reference | pin 16 +9 V, pin 8 0 V, pin 1 +5 V | S10, S9 | |
| Buffers | P-MOS follower, \|V_GS\| ≈ 2.9–3 V (IR3109), 3.5 V (AS3109); swing ≈ −1.3 / +2.5 (1,3) / +3.8 V (2,4) around +5 V | S9, S8; agent | |
| Resonance loop | TR26 gain −1.49; 5.6 k → D30/D31 → 15 k; inject 68 k/15 k = 4.53; k_max ≈ 4.95 | S1, S10; my calc | |
| Self-oscillation | onset ≈ 81 % RES; f ≈ 0.98–1.0·f_c; V4 ≈ 0.6 V pk (simulated) | agent sim | not measured |
| Passband vs RES | (1+α)/(1+4.95α): −6.0 / −8.8 / −9.5 dB at α = 0.5 / 0.8 / 1 | my calc; agent | |
| C52 / C53 corners | 0.77 Hz / 0.21 Hz | my calc | |
| Cutoff CV sum | IC18b R_f 33 k; FREQ 200 k, offset 680 k, bend 100 k, grip LFO 220 k, ENV 100 k, MOD 100 k, KYBD 150 k (TR42) | S1, S9 | |
| Pin-9 divider | 4.7 k → [5.6 k ∥ (1.8 k + TH1)] → pin 9, 820 Ω to +5 V: ×0.111 (TH1 1 k) or ×0.088 (10 k) | S1, S10; my calc | |
| CV scale | 0.171 V/oct at IC18b output (19 mV/oct); FREQ span ≈ 4.4–5.1 oct; ENV full ≈ 15–19 oct | my calc | |
| Pin-9 measured | 4.90–5.01 V over the FREQ slider | MW p3692744 (S12) | |
| Tracking calibration | VR8: F5 period = ½ F4 at KYBD max, A4 ≈ 1 kHz | S1 p4 | |
| Spec | 10 Hz–20 kHz; key follow 0–100 % | S1 | |
| **ENV** | | | |
| Parts | C59 4.7 µF; A/D/R 1 MΩ (A); S 100 kΩ (B); minimums A 270 Ω, D 150 Ω, R 220 Ω; IC16 IR9022 | S1; my reading | |
| τ | min 1.27 / 0.71 / 1.03 ms (A/D/R); max ≈ 4.7 s | my calc | |
| Spec | A 1.5 ms–4 s; D 2 ms–10 s; S 0–100 %; R 2 ms–10 s | S1 | |
| Measured | maximum attack ≈ 3 s (Reid, one unit) | S28 | |
| Attack threshold | 0.47 (3 s) or 0.57 (4 s) of the charging target | my calc | |
| Output | ≈ 0…+10 V (clone design value) | S11 | |
| **VCA (BA662A)** | | | |
| Circuit | inputs 1 k / 1 k; load R140 47 k; I_abc via TR28 / R133 12 k; C53 10 µF in | S1 | |
| Input divider | ≈ 1/152 (RES 0) … 1/76 (RES max) | my calc | |
| g_m | 8.0–8.5 mS at 500 µA (ideal 9.7); k_m ≈ 0.9 | S15 | |
| Offset | 30–250 µV (BA662A measured); ≤ 3 mV (BA6110) | S16, S15 | |
| Noise | −74 dBm typ at 200 µA; −94 dBm at 0 | S15 | |
| **LFO** | | | |
| Core | C9 0.1 µF integrator + Schmitt; RATE 100 kA + 220 Ω | S1 | |
| Rate | 0.1–30 Hz | S1 | |
| Grip LFO MOD | sine-shaped triangle; fade τ 0.47 s (1 M × 0.47 µF) vs "1.5 s" | S1; my calc; S11 | |
| VCO-MOD smoothing | IC12b pole 2.34 kHz | my calc | |
| **Output / power** | | | |
| Output | VOLUME 100 kA → M5218 follower → 100 µF → 2.2 k / 1 nF / 1 k; LPF 72 kHz; Z_out ≈ 3.2 k; 0 dBm max | S1; my calc | |
| Rails | DC-DC ≈ +21 V → +15 V (14.87 V measured); +9 V; +5 V reference ≈ 4.93 V (trimmed); −5 V; +4.5 V logic | S32, S1; my calc | |
| **DSP references** | | | |
| polyBLEP | 2-point 2.1 kHz; 4th-order B-spline 7.8 kHz alias-free at 44.1 kHz | S23 | |
| DPW | N = 2…6: 0.6 / 2.0 / 4.6 / 7.9 / 12.2 kHz | S24 | |
| Huovilainen | < 10 % tuning error below F_s/4 at 2×; 5 tanh per sample | S20 | |
| D'Angelo | 5 tanh / 26 mul / 24 add per sample (N = 4) | S21 | |
| Oyama | harmonic error 0.04–0.10 % (Huovilainen/D'Angelo) vs 0.65 % (JUCE) | S21 | |
| mystran | ≈ 4× OS; Heun ≈ 2× cost | S22 | |

---

## 12. Disagreements between sources (summary)

1. **Envelope chip:** IR3R01 (sellers, lore) vs discrete IR9022 + transistors (S1, S11, S31).
   **Resolved: discrete.**
2. **Maximum attack:** 4 s (spec, copied everywhere) vs ≈ 3 s measured on hardware (Reid). Calibrate
   to 3 s by default.
3. **Portamento maximum:** 5 s (S1, S2) vs 3 s (S30). Both fit τ = 0.66 s under different definitions.
4. **AUTO portamento:** the manual's wording vs hardware behaviour (S27, S28). Gate-edge reset is
   authoritative.
5. **Tuning reference:** A = 442 Hz (S1, S2) vs 440 Hz (S27, most clones).
6. **Q compensation:** present (S10, S11 AM8101) vs absent (S11 IR3109 article; TAL default). The
   schematic shows the output-side make-up path.
7. **VCF reference and RES pot:** +5 V (S9, S10) vs +4.5 V (S11); VR18 bottom at GND (S1 drawing,
   S10) vs +5 V (S9 fix).
8. **R137** 33 k vs 68 k; **R125** present vs omitted; **TH1** 1 k vs 10 k NTC; **KYBD follower**
   33 k vs 100 k (S9 vs S10/S11).
9. **IR3109 expo slope:** 19 (S8) vs 20.8 (S9) vs 17.9 mV/oct (ideal). Expo bias 3 / 2.4 / 1.8 µA.
   Pin-9 ranges inconsistent.
10. **IR3109 bottom mirror:** Wilson (S9) vs cascode (S8). **Which half clips first:** OML vs
    source-follower analysis.
11. **IR3109 lineage:** CEM3320 copy (lore) vs BA662-style cores (S9).
12. **Minimum PW:** 0 % (S1) vs ≈ 5 % (S11).
13. **Lowest cutoff:** 10 Hz (S1) vs 0 Hz (S28 1994).
14. **LFO low end:** 0.1 Hz (S1) vs 0.35 Hz (a clone store page).
15. **RANDOM:** "S&H of noise" (press, manual wording) vs CPU 6-bit values (S1, S26, S11).
16. **Grip LFO delay:** 0.47 s τ (components) vs "1.5 s" (S11) vs "very slight" (S29).
17. **GATE out level:** 12 V (S1) vs 10 V (S2).
18. **GATE-mode priority:** low-note (S2, Reid) vs "high-note" (a KVR user's mislabel).
19. **Arpeggio gate:** continuous (S2) vs "blipping" on one repaired unit (S31). Arpeggio
    portamento: S2 p31 vs p32.
20. **Sequencer length:** 100 steps (S1) vs 256 notes (Nagle).
21. **Noise colour:** white (S28 1994) vs darker than the MS-1's / "not white" (S32, S33, S28 2018).
22. **Rails:** "GND, +5, +14 only" (S11) vs −5 V present (S1).
23. **Press waveform names:** LFO "sine" and VCO "triangle" (S28 1994, S29) vs triangle/square LFO
    and saw/pulse VCO (S1).
24. **CEM3340 clone parameters:** sync reference −2.5 vs −1.4 V; zener 6.5 vs 7.4 V; pulse pull-down
    10 k vs 51 k; hysteresis 1 M vs 12 M; pulse high V_CC − 0.9 / −1.3 / −1.5 V. None is used by the
    101 except the pulse, which TR24 re-generates.
25. **PWM-dependent pitch** on 3340s: present on AS3340 / Rev. G parts vs "vintage chips don't"
    (S6, S7).
26. **MS-101 filter:** "copied the IC" (forums) vs a discrete LM13700 cascade (S36 trace).

---

## 13. Open questions and measurements to take

These can be answered from the netlist (`docs/schematic/`) with SPICE, or from a real unit or
recordings:

1. **ENV topology details.**
   - Which transistor implements which segment.
   - The attack target voltage and the IC16a threshold (gives the attack-time ratio: 3 s vs 4 s).
   - Sustain reference (pin 90) voltage; ENV output range.
   - The GATE+TRIG pulse width (IC5).
   - Pot tolerance/taper of the 1 MA parts.
2. **VCA control path.**
   - TR28's configuration and law (I_abc vs ENV volts).
   - Whether R41/C56/C57/R142 smooth the GATE-mode control (my τ ≈ 0.4–1.6 ms).
   - The reference-bus voltage at the BA662 inputs.
3. **VCF.**
   - TH1 value (1 k PTC vs 10 k NTC) and the resulting CV scale.
   - The real pin-9 slope (19 vs 20.8 mV/oct).
   - FREQ-slider endpoints (absolute cutoff at FREQ 0/10 with KYBD/ENV 0).
   - Self-oscillation amplitude and tracking error across 20 Hz–20 kHz.
   - Resonance onset (predicted 81 %).
   - Passband loss vs RES (predicted −8.8 dB at 0.8).
   - Distortion of a full-level saw at high resonance.
   - Which half clips first.
4. **VCO.**
   - Measured HF tracking with the fixed pin-7 feedback.
   - Saw mid-ramp step/kink (comparator delay, triangle symmetry).
   - PWM-dependent pitch on a real CEM3340 in this circuit.
   - Pulse edge rates through TR24.
   - The IC12b idle offset (is its + input really on the +5 V bus, as my tuning arithmetic requires?).
5. **Sub-oscillator.** Level and DC offset relative to the saw at the mixer node; exact edge timing vs
   the saw wrap (predicted 1–2 µs).
6. **Noise.** The real spectrum of the 2SC945 + TR22 + IC12a chain (calibrate on the Mr Card raw
   noise).
7. **Portamento.**
   - VR2 taper (glide time vs knob position).
   - JFET R_on.
   - The AUTO pulse width from the R15/C11/R16/R24 network.
   - Confirm that C10 = 0.33 µF (handwritten ".33") with a glide-time measurement.
8. **LFO.** RATE-pot law and endpoints (0.1 Hz? 30 Hz?); triangle/square amplitudes; exact grip fade-in;
   LFO square duty.
9. **Levels.** Absolute output level (dBu) for a full saw with the filter open; MOD/ENV depth
   calibration in octaves.
10. **CPU timing.**
    - Gate gap in the sequencer.
    - Arpeggio gate behaviour.
    - PRNG for RANDOM.
    - Exact loop period distribution.
    - These belong to the firmware track; use the disassembly there.
11. **Unit-to-unit spread.**
    - IR3109 OTA matching (≈ 5 %).
    - BA662 offset (30–250 µV) and g_m grade.
    - CEM3340 symmetry (45–55 %).
    - Pot tolerances.
    - These define the "vintage variation" controls.
12. **A/B against the Roland plug-in** for each block above. Where it is reported to differ from
    hardware (top-octave aliasing, controller stepping, high-resonance timbre), use real-unit
    recordings as the referee (§10.3, §10.4).
