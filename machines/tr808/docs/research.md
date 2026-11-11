# TR-808 emulation research digest

Compiled 2026-10-12 for the circuit-level TR-808 in `machines/tr808`.

This digest covers what the published sources say about the Roland TR-808's trigger and accent
circuit, its eleven voice circuits, the shared noise and metallic sources, the mixer and the
sequencer, and what Lemus chose where the sources leave a gap or disagree. It is organised by
circuit block. Each block gives:

- what the circuit does and the numbers the sources give, with the source;
- where sources disagree;
- which models exist, and what they were validated against;
- **Lemus:** what the emulation does.

Section 13 tabulates the disagreements. Section 14 lists what is still open.

Conventions:

- `private/` is the machine's link to the private material (`machines/tr808/private`, made by
  `tools/link-private.sh`). None of it is in this repository. Paths below are relative to that link.
- The long-form notes, with fuller quotes, are `private/docs/papers/digest.md` (voices; its
  citation keys are used here), `private/docs/service-notes-index.md` (the service notes page by
  page, design changes and lot notes) and `private/docs/operation-digest.md` (the manuals).
- Service-notes page numbers are the printed ones (= PDF pages).
- "(our arithmetic)" marks numbers derived from the cited values, with the formula inline.
  "(Lemus measurement)" marks numbers this project measured, on the netlists or on the private
  reference recordings.
- Designators follow the service-notes schematics (pp. 8, 9, 13). The netlists are
  `core/netlists/*.cir`; the fitted unit Lemus plays is `unitCalibration()` in `core/voiceboard.cpp`.

---

## Contents

0. [Sources and local copies](#0-sources-and-local-copies)
1. [Architecture summary](#1-architecture-summary)
2. [Shared circuits](#2-shared-circuits)
3. [Bass drum (BD)](#3-bass-drum-bd)
4. [Snare drum (SD)](#4-snare-drum-sd)
5. [Toms and congas (LT/LC, MT/MC, HT/HC)](#5-toms-and-congas-ltlc-mtmc-hthc)
6. [Rim shot and claves (RS/CL)](#6-rim-shot-and-claves-rscl)
7. [Hand clap (CP)](#7-hand-clap-cp)
8. [Maracas (MA)](#8-maracas-ma)
9. [Cowbell (CB)](#9-cowbell-cb)
10. [Cymbal (CY)](#10-cymbal-cy)
11. [Open and closed hi-hat (OH, CH)](#11-open-and-closed-hi-hat-oh-ch)
12. [Sequencer and firmware sources](#12-sequencer-and-firmware-sources)
13. [Disagreements between sources (summary)](#13-disagreements-between-sources-summary)
14. [Open questions](#14-open-questions)

---

## 0. Sources and local copies

| Key | Source | Local copy (under `private/`) | Notes |
|---|---|---|---|
| SN | Roland, *TR-808 Service Notes*, First Edition 15 Jun 1981 (this copy: 3rd printing, June 1986). https://archive.org/download/synthmanual-roland-tr-808-service-notes/rolandtr-808servicenotes.pdf | `docs/tr808-service-notes.pdf`; 400 dpi renders `docs/schematic/hires/`; crops `docs/crops/`; index `docs/service-notes-index.md` | The only primary source. Image-only scan; the OCR text is unreliable, so numbers were read off the page images. Every free copy online is the same scan |
| OM, JA, IN | Roland owner's manual (https://cdn.roland.com/assets/media/pdf/TR-808_OM.pdf), Japanese manual (http://lib.roland.co.jp/support/jp/manuals/res/1810124/TR-808_j.pdf), "Introduction for easier operation" booklet (https://archive.org/download/manualsbase-id-576201/576201.pdf) | `docs/tr808-owners-manual*.pdf`, `docs/tr808-introduction-for-easier-operation.pdf`; digest `docs/operation-digest.md` | Behaviour only. JA is much longer than OM and settles several points OM leaves open |
| W-BD | K. J. Werner, J. S. Abel, J. O. Smith, "A Physically-Informed, Circuit-Bendable, Digital Model of the Roland TR-808 Bass Drum Circuit", DAFx-14. https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf | `docs/papers/academic/werner_abel_smith_dafx14_tr808_bass_drum.pdf` | Peer reviewed; validated against SPICE |
| W-CY | Werner, Abel, Smith, "The TR-808 Cymbal: a Physically-Informed, Circuit-Bendable, Digital Model", ICMC/SMC 2014. https://zenodo.org/record/850891 | `docs/papers/academic/werner_abel_smith_icmc_smc14_tr808_cymbal.pdf` | CC BY; validated against SPICE |
| W-PhD | K. J. Werner, *Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters*, PhD, Stanford, 2016. https://purl.stanford.edu/jy057cz8322 | `docs/papers/academic/werner_2016_phd_wdf_virtual_analog_tr808_bd.pdf` | WDF model of the whole BD; component tables |
| W-blog/\<id\> | Werner's research blog, 2013-14. https://kurtjameswerner.tumblr.com/post/<id> | `docs/papers/web/werner-tumblr/` | ChucK sketches, clone notes, SPICE |
| DER | E. Deruty, "Harmonic and Transposition Constraints Arising From the Use of the Roland TR-808 Bass Drum", ISMIR 2024 (arXiv:2502.07524) | `docs/papers/academic/deruty_ismir24_tr808_bass_drum_harmonic_constraints.pdf` | BD pitch from sample libraries |
| ARCH | Eric Archer, TR-808 clone pages and DIY PDFs, 2007-09 (Wayback of ericarcher.net) | `docs/papers/web/archer/` | The PDFs carry the original SN schematic pages |
| WHIT | Robin Whittle, TR-808 mod manuals. http://www.firstpr.com.au/rwi/tr-808/ | `docs/papers/web/whittle/` | Behaviour of stock units |
| SOS | Sound On Sound, Gordon Reid's *Synth Secrets* (2002) and reviews | `docs/papers/web/sos/` | Block-level descriptions |
| TT | Tiptop Audio XX808 module manuals. https://www.tiptopaudio.com/manuals/ | `docs/papers/web/tiptop/` | Clone makers who compared several real units |
| MI | Mutable Instruments Peaks and Plaits drum code (MIT). https://github.com/pichenettes/eurorack | `docs/papers/web/mutable/` | 808-style DSP |
| WOLF | L. Laguna Ruiz, "Digital Vintage Sound", Wolfram blog, 2021. https://blog.wolfram.com/2021/12/29/digital-vintage-sound-modeling-analog-drums-with-the-wolfram-language-and-system-modeler/ | `docs/papers/web/wolfram/` | BD model compared with a ±12 V replica |
| SJ | SyntherJack, RS/CL and CB clone notes, 2016. https://syntherjack.net/the-perfect-808-rimshot-clave/ | `docs/papers/web/syntherjack/` | |
| YOCTO, YOCTO-F | e-licktronic Yocto (808 clone) schematic and forum | `docs/papers/web/yocto/`, `docs/papers/web/forums-elicktronic/` | Fills values the SN leaves blank |
| DELP, ED, HEX, ERICA, MB808, SLOS, FM, GS, MW, LMNC, EM, KVR | Delp (LDB-1), Electric Druid (DR-110), Hexinverter and Erica/Klein manuals, MIDIbox MB808 wiki, Secret Life of Synthesizers (noise transistor), Future Music 326 TR-08 review, Gearspace / ModWiggler / LMNC / electro-music threads, KVR clap thread | `docs/papers/web/...`; URLs in `docs/papers/sources.md` | KVR is a summary, not verbatim |
| DS | Datasheets (µPC4558, HD14584B, 2SC945, 2SA733, 1S2473, AN6912, BA6110/AS662D, ...) | `docs/datasheets/` | No sheet exists for the BA662A or the 1S188FM |
| MF | Michael Fischer, *Roland TR-808 Sound Sample Set 1.0.0*, 1994, serial 103852. http://machines.hyperreal.org/manufacturers/Roland/TR-808/samples/TR808all.zip | `reference/real808/fischer/`; index `reference/real808/index.md` | Real unit, individual outputs, LEVEL max, knobs at 0/2.5/5/7.5/10 |
| AO | Internet Archive "TR 808 Samples", 2026. https://archive.org/details/tr-808-samples | `reference/real808/archiveorg_tr-808-samples/` | Early-revision unit, every voice accented then plain; voices unlabelled |
| RAUM45, BAMTV | Raum 45 free "TR-808 Clean" (https://www.raum45.de/en/downloads/); BAMTV, two 808s playing one pattern (https://www.youtube.com/watch?v=ACxNgFmIpkU) | `reference/real808/raum45_free/`, `reference/real808/bamtv_two808s/` | Take-to-take and unit-to-unit spread; no settings |
| MAME | MAME `roland_tr808.cpp` (hap). https://github.com/mamedev/mame/blob/master/src/mame/roland/roland_tr808.cpp | `reference/mame/roland_tr808.cpp` | Skeleton driver |

Not obtained:

- Werner, Abel, Smith, "More Cowbell: a Physically-Informed, Circuit-Bendable, Digital Model of the
  TR-808 Cowbell", AES 137th Convention, 2014, paper 9207 (paywalled).
- Werner, "A Punchier Crisp Bass" (listed as in submission on his homepage; not found published).
- Commercial sample sets with documented knob grids: Raum 45 *AchtNullAcht* (all 11 marks, 12
  takes), Cluster Sound *CS-808* (BD accent × tone × decay). See `reference/real808/index.md`.

**Source quality.** The service notes are the only primary source: block diagrams, schematics, a
page of circuit description per voice, an adjustment table and a "Checking voices" table of
"typical and variable" amplitudes, frequencies and decay times. The only peer-reviewed circuit
analyses are Werner's (BD and CY), and they are validated against **SPICE simulations of the
schematic, not a real 808**. W-BD §12 adds a qualitative comparison with MF. The only model compared
with hardware is WOLF's, and that hardware was a replica. Everything else is clone documentation,
mod notes, magazine descriptions, forum posts and DSP code. Published measurements of real units
are thin: BD pitch (DER, GS, Werner's frequency tracking), by-ear comparisons (FM, TT, WHIT), and a
description of the noise transistor (SLOS). Nobody has published scope captures of the trigger or
accent pulses, oscillator-bank frequencies, noise spectra or mixer levels on a real unit.

**No circuit-level model has been published for the SD, toms/congas, RS/CL, CP/MA, CB, OH/CH, the
noise generator, the accent circuit or the mixer.** For those the SN schematic and its text, plus
clone notes, are all there is. Lemus's own reference data is MF (shape, pitch and decay at known
knob positions) and AO (accent ratios).

---

## 1. Architecture summary

| Block | Lemus model | Basis | Fitted to a unit? |
|---|---|---|---|
| CPU board | Original µPD650C-085 firmware on a µCOM-43 core (a port of MAME's `ucom4`), 500 kHz clock, 1.9 ms INT; switch matrix, four µPD444 RAMs, LEDs, status inputs, 24 ppqn tempo clock (`core/machine.*`, `core/ucom43.*`) | SN pp. 3-5, 8 | No |
| Common trig and accent | `accent.cir` (Q31-Q34, VR3); IC6 as a timed 1 ms pulse; `mtrig.cir` (IC2a, the narrowed trig for CB/CY/OH/CH) | SN pp. 4, 8, 13 | No |
| Voices | One netlist per voice, transcribed part by part from SN p. 9 (main board) and p. 13 (voicing board) | SN schematics | Part values, see each section |
| Noise | Gaussian white source at Q35's emitter, scaled so TP4 reads 130 mV rms, into `noise.cir` (the "6 lot ~" circuit) | SN pp. 8, 14 | No |
| Metal source | Six band-limited (polyBLEP) squares, 0 to 4.7 V, into `metal.cir` (R35-R50 mix, IC3 band-passes) | SN p. 13, W-CY | Yes: all six frequencies |
| Mixer | `mix.cir` (IC4, MASTER VOLUME, HI and LO outs) | SN p. 13 | No |
| Devices | Gummel-Poon DC equations without charge storage; op-amps single-pole (3 MHz GBW), soft clipping at ±13.5 V; OTA and comparator behavioural blocks (`models.cir`) | DS, SN parts list | No |
| Solver | Nodal DK method: MNA with trapezoidal capacitors, Newton on the nonlinear ports each sample; each voice split into partitions that run in order and sleep once settled (`core/circuit.*`) | Yeh/Abel/Smith 2010, Holters/Zölzer 2015 | — |
| Rate | Host rate ×2 (×1 at 88.2 kHz and up), half-band FIR decimation | — | — |

Reasoning in brief:

- The service notes give nearly every part value, so the voices are built from the schematic, not
  from fits to recordings; recordings only adjust part values. Pitch glides, accent-dependent
  timbre and retrigger artefacts then come out of the circuit.
- Each netlist also exports to ngspice (`tools/spice`) and is checked against the SN's voice table
  (`tools/service/check.py`).
- Unit-to-unit spread is large (§2.5, §3, §4), so one real unit is chosen and fitted: Fischer's
  serial 103852 (MF), the only obtainable set with documented knob positions. The fit
  (`tools/compare/fit.py`, `fischer.py`) compares shape, pitch and decay, not level (the recording
  chain's gain is unknown and loud hits are limited near −2.45 dBFS). It fits on the 0/5/10 knob
  marks and holds out 2.5/7.5.

---

## 2. Shared circuits

### 2.1 Trigger, common trig and accent

**Facts:**

- The CPU emits a 10 µs step pulse on PI2 for every step, "regardless of TR-808 function mode". The
  TC4011 monostable IC6 widens it to about 1 ms; its period is set by R100·C23 + R102·C27 (SN p. 4).
- The accent circuit (Q31-Q34) turns IC6's output into COMMON TRIG, which reaches every voice's gate
  in parallel. Each gate ANDs it with that voice's instrument data (5 V, held on the CPU ports), so
  "the output of the sound generator has the amplitude in proportion to the common trig signal"
  (SN p. 4).
  - No accent: Q34 holds Q32's base at about +5 V; trig "approx. 4V".
  - Accent: VR3 (AC LEVEL, 10k B) puts 5-15 V on Q32's base, giving trig pulses "of approx. 4—14V".
  - CB, CY, OH and CH: the range is "narrowed to 7V—14V" by IC2a on the voicing board "to increase
    S/N ratio".
- Spec: "Accent level ... 0dB-10dB"; trigger outputs (CB, CP, AC) +15 V, 20 ms, 1 kΩ (SN p. 1).
- The common trig fires on every step, not only on steps with notes (MB808).
- SN p. 14: a common trig "longer or shorter than 1ms" makes the voices deteriorate.
- Design changes at serial 010600 (SN p. 15): voice-gate resistors R106, R154, R182, R213, R242,
  R268, R298 22k → 10k (more gate drive at low trig levels); common trig R101 10k → 100k and C201
  22p added (CP high frequencies were upsetting other voices triggered on the same step).
- The 1 ms pulse's falling edge also excites the resonators. Without Archer's diode-cap-resistor
  network a clone's drums "trigger on the negative edge" too (ARCH/tr808-clone); in the BD the
  diode D53 shapes that edge (W-BD §4).
- Thresholds: the cymbal and hats need about 4.5 V before they sound, the other drums about 2 V;
  the clap is "generally insensitive to the trigger level" (WHIT).

**Disagreements.** Unaccented level: 4 V (SN, W-PhD's sweep 4.0-14.0 V, Hartman in EM) vs 5 V
(Whittle, MB808) vs an "8 V normal" (WOLF, an outlier).

**Lemus measurement** (DC operating points of `accent.cir` and `mtrig.cir`): common trig **4.3 V**
plain and **14.2 V** accented at full ACCENT LEVEL, matching the SN's "approx. 4-14V". The narrowed
trig is **6.8 V / 13.5 V**: IC2a clips at the op-amp's limit, which is why its top is below 14 V.
The ratios are 10.4 dB and 6.2 dB (our arithmetic: 20·log10(14.2/4.3), 20·log10(13.5/6.6)).

**Lemus:** `accent.cir` and `mtrig.cir` from SN pp. 8 and 13; IC6 as a 1 ms pulse
(`Calibration::monoMs`); the late (010600 and later) gate resistors, 10k, in every voice.

### 2.2 Bridged-T resonator

**Facts:**

- SN p. 5, Fig. 11, with R1 from the junction of C1 and C2 to ground and R2 bridging them:
  `f = 1/(2π√(R1·R2·C1·C2))`, `Q = √(R2/R1) / (√(C1/C2) + √(C2/C1))`. "The decay time becomes
  longer as Q increases."
- Werner: the 808 uses it "in every single one of its sound generators in some form"; BD, SD,
  toms/congas and RS/CL as decaying pseudo-sinusoids, CP, CB, CY and OH/CH as band-pass filters
  (W-BD §5).
- The BD, toms and claves add a "multi-feedback" path (SN pp. 5-6): an inverting stage feeds the
  output back into the centre node (R170 in the BD, R230/R259/R286 in the toms, R313 in the
  claves). Lowering that resistance lengthens the decay and eventually self-oscillates
  (W-blog/66521011111, WHIT, TT).

**The formula against the table** (our arithmetic, component values from the SN schematic as
transcribed in the netlists):

| Voice | R1 | R2 | C1, C2 | Formula | SN table |
|---|---|---|---|---|---|
| BD | R165+R166 53.8k | R167 1M | 15n, 15n | 45.7 Hz | 56 Hz |
| SD low, early / late | R196 680 | R197 820k | C58 27n or 56n, C59 27n | 249.6 / 173.3 Hz | 238 Hz |
| SD high, early / late | R195 2.2k | R198 1M | C60 6.8n, C61 6.8n or 15n | 499.0 / 336.0 Hz | 476 Hz |
| LT (LC) | R231 1k + 0-500 Ω of VR11 | R228 820k | 56n, 59n (LC: 12n) | 97-79 Hz (LC 214-175) | 80-100 (LC 165-220) |
| MT (MC) | R260 1k + VR13 | R257 820k | 35.2n, 39n (MC: 27n, 12n) | 150-122 Hz (MC 309-252) | 120-160 (MC 250-310) |
| HT (HC) | R287 1k + VR15 | R284 820k | 27n, 27.6n (HC: 27n, 5.6n) | 204-166 Hz (HC 452-369) | 165-220 (HC 370-455) |
| RS low (IC21) | R315 5.6k | R316 1M | 4.7n, 4.7n | 452.5 Hz | 455 Hz |
| RS high (IC20a with C118) | R312 1k | R308 820k | 2.2n, 4.4n | 1786 Hz | 1667 Hz |
| CL (IC20a) | R312 1k | R308 820k | 2.2n, 2.2n | 2526 Hz | 2500 Hz |

- Werner's Table 2.2 (W-PhD p. 112) uses an equivalent 333-500 Ω for the toms' R1 leg, which gives
  frequencies about 1.7× the table; the private digest left that unresolved. Read as R231 (1k) in
  series with the tuning pot, as the schematic draws it, the formula lands on the table for all six
  tom and conga voices (our arithmetic above).

**Lemus:** every resonator is in its voice's netlist as drawn, with its multi-feedback stage; no
voice uses the formula.

### 2.3 Swing-type VCA

**Facts:**

- SN p. 5, Fig. 12: a single transistor whose collector is fed by the envelope (through R2 and a
  diode) and whose base takes the audio; its output has "many high harmonic components".
- Electric Druid: "this uses an audio signal to control the output of an envelope", so the envelope
  leaks through as a "DC thump" unless high-passed (ED).
- Archer: it "only passes audio of one polarity"; used in the hats, cymbal, snare noise, tom noise,
  clap, rim shot and maracas (ARCH/tr808-clone); the cowbell's pair is fed "through R28-D3 and
  R29-D4" (ARCH/cowbell).
- Werner's analysis (W-CY §8): biasing via 1M resistors is "inherently unstable to temperature
  change" and strongly β dependent; modelled with Ebers-Moll plus a fitted two-exponential "lower
  edge" curve and a soft clip (α = 3.5) fitted by eye to SPICE.
- DSP stand-ins: half-wave rectify and multiply (Werner's ChucK snare); Plaits `SwingVCA` adds an
  envelope bleed term (MI).

**Lemus:** each swing VCA is its transistors and diodes in the netlist (2SC945 Gummel-Poon, 1S2473
diodes); no fitted curve. The base bias resistors are where the fit had to move parts (SD R194, §4).

### 2.4 Noise generator

**Facts:**

- Q35, a 2SC828 (R) "selected noise" part marked "RNZ", with a junction in breakdown; IC24 (µPC4558)
  amplifies it; TM4 (22k B) trims "W.N. LEVEL 130mV r.m.s" at TP4 (SN pp. 8, 14, 16).
- IC24b is a low-pass for the "pink" noise the toms use (SN p. 6; "Pink noise ... to provide
  artificial reveberation"). The snare, clap and maracas use the white output.
- Design change at 010600 (SN p. 15): R129 330k → short, R127 4.7k → 10 µF (C200), C202 22p
  added ("Decouple DC"; the table's "R311 330K → 100K" is R131 300k → 100k in the p. 8 drawing).
  Both circuits are drawn on p. 8. Tiptop's SD808 uses the later one.
- SLOS: not every unit has the 2SC828; two serviced units had "selected" 2SC945 and 2SC536 parts.
  The noise is described only by ear.
- Whittle: avalanche noise, and "no significant difference" between units "other than the effects
  of TM4" (WHIT/TR-808-Sound-Mods §10).
- Werner calls the tom noise "single pole lowpass filtered white noise" (W-blog/67450341748). The
  IC24b filter has two capacitors: R133 22k, R132 22k, C32 39n, C33 18n, a Sallen-Key low-pass at
  about 270 Hz (our arithmetic: 1/(2π·22k·√(39n·18n))).
- No published spectrum, level or crest-factor measurement of real 808 noise exists beyond the
  130 mV rms trim target.

**Lemus:** `noise.cir` is the "6 lot ~" circuit. Q35 is replaced by a Gaussian white voltage at its
emitter, scaled so TP4 reads 130 mV rms: IC24a's gain of about 92 rolls off above 7.2 kHz (C202 22p
across R130 1M), and the source density is set from that (`VoiceBoard::prepare`). It runs all the
time.

### 2.5 Six-oscillator metallic source (CY, OH, CH; CB uses two)

**Facts:**

- One HD14584 hex Schmitt inverter makes six RC oscillators. Werner gives nominal frequencies of
  205.3, 369.6, 304.4 and 522.7 Hz for the four fixed ones and ranges of 359.4-1149.9 and
  254.3-627.2 Hz for the two trimmed ones (W-CY §3). These are **computed** from the parts with the
  datasheet's typical thresholds, not measured.
- Parts (SN p. 13): R40 560k/C1 18n (205.3), R41 560k/C2 10n (369.6), R42 680k/C3 10n (304.4),
  R43 220k/C4 18n (522.7); TM2 220k B + R45 100k/C5 18n (cowbell, 800 Hz); TM1 220k B + R44
  150k/C6 22n (cowbell, 540 Hz). Each output goes through 120k into R53 1k (the cymbal's sum).
  `T = RC·[ln((V−VT−)/(V−VT+)) + ln(VT+/VT−)]` with V = 5 V, VT+ 2.7 V, VT− 2.1 V reproduces
  Werner's figures exactly (our arithmetic); the same thresholds give a duty cycle of about 0.48.
- The HD14584's datasheet spread is wide: at 5 V, VT+ 1.8-3.4 V and VT− 1.6-3.2 V (DS/hitachi-hd14584b).
  The SN itself blames "Variations in HD14584 hysterisis" for an out-of-range clock (p. 15).
- Supply: emitter follower Q9 off R61 10k/R60 5.6k on +15 V, marked "5V"; the emitter sits near
  4.7 V (our arithmetic: base at 15·5.6/15.6 = 5.38 V).
- Factory tuning (SN p. 14): TM-1 for 1.85 ms (540 Hz) at TP-6, TM-2 for 1.25 ms (800 Hz) at TP-7.
  The schematic notes say the two were the other way round in lots 1-2, and voicing-board change 1
  (serial 000300) changed C6, R44 and R45 to widen the trim range.
- Unit spread: "Each TR-808 has a unique tonality to its Cymbal and Hi Hat sounds" (WHIT); Tiptop
  measured "large differences" between real units but published no numbers (TT); FM names hats and
  cymbal as the biggest unit differences.
- Plaits' six ratios (414-1050 Hz) and Klein's six (TR-606 values) share only the cowbell pair with
  the 808 schematic (MI, ERICA).

**Lemus:** six polyBLEP squares between 0 and 4.7 V, duty 0.47, starting low (discharged capacitor).
The nominal calibration uses Werner's frequencies and the trimmed 540/800 Hz. The unit Lemus plays
uses MF's, fitted from the line spectra of its cymbal, open hat and cowbell: **198.8, 378.6, 287.8
and 486.4 Hz** for R40/C1, R41/C2, R42/C3, R43/C4 (2-7 % off the typical-threshold values), and
**558.3 / 823.6 Hz** for TM1/TM2 (periods 1.79/1.21 ms, "a little short" of the SN's 1.85/1.25;
a Yocto clone tuned by ear measured 1.78/1.19 ms, YOCTO-F). `metal.cir`: R35-R50 into R53, IC3b
band-pass about 7.1 kHz (CY high bands, both hats), IC3a about 3.4 kHz (CY low band).

### 2.6 Mixer and outputs

**Facts:**

- SN p. 1: master output "Hi 6V p-p/1KΩ; Lo 0.6V p-p/3KΩ (Level: Voices @ red mark; AC @ FCW)";
  multi outputs 1 kΩ.
- Plugging a MULTI OUT removes that voice from the master mix; MASTER VOLUME does not act on the
  multi outs (OM p. 6).
- Most voices end in an inverting buffer with 470k feedback and a small capacitor across it
  (220 pF in SD, CP, CY and the hats, a pole near the top of the audio band that Werner puts
  "around 20k" for the cymbal; 1 nF in the toms; 22 pF in RS/CL), LEVEL in its feedback and a 1k
  build-out resistor to the jack (ARCH PDFs, W-blog/71732181844). The BD and CB end in followers.
- IC4 (µPC4558) on the voicing board is the mixer and master output; the Jack board B carries the
  multi-out jacks and the HI/LO divider (SN p. 13).
- Nobody has published a mixer analysis or measured output levels on a real unit.

**Lemus:** `mix.cir` as SN p. 13 draws it: each voice's jack feeds IC4a's virtual ground through the
jack's normal contact and its mix resistor (BD 22k; SD, toms, RS, CP 33k; CB, CY, CH 47k; OH 56k),
R75 33k ∥ C18 220p feedback, MASTER VOLUME VR1 (50k A), IC4b, HI through C1 and R14 1k, LO from the
R13 27k / R12 3.3k divider. (Our arithmetic: LO is 0.109 of HI, a 3.3/30.3 divider with a 2.94 kΩ
source, matching the spec's 6 → 0.6 V p-p and "3KΩ".) Each voice's netlist loads its output with
its own mix resistor.

### 2.7 The "Checking voices" table (SN p. 14) and real accent ratios

Method (SN p. 14): scope on the voice's MULTI OUT, voice LEVEL full, DECAY/TONE/TUNING at 12
o'clock; amplitude is the first peak-to-peak with ACCENT LEVEL fully anticlockwise ("normal") and
then fully clockwise ("accent"); decay is to 1/10 of that Vpp. The values are "typical and
variable". Frequencies are printed as a period in ms with Hz in brackets (rounded: 18 ms is 55.6 Hz).

| Voice | Normal Vpp | Accent Vpp | Freq. low / mid / high, Hz | Decay short / mid / long, ms | Table accent ratio (our arithmetic) | AO ratio, p-p (Lemus measurement) |
|---|---|---|---|---|---|---|
| BD | 3.5 | 10 | — / 56 / — | 50 / 300 / 800 | 9.1 dB | 9.7 dB |
| SD | 3 | 10 | H 476, L 238 | — / 60 / — | 10.5 | 9.1 |
| LC | 3.5 | 12 | 165 / 185 / 220 | — / 180 / — | 10.7 | — |
| LT | 3.5 | 12 | 80 / 90 / 100 | — / 200 / — | 10.7 | 13.3 |
| MC | 3 | 10 | 250 / 280 / 310 | — / 100 / — | 10.5 | — |
| MT | 3 | 11 | 120 / 135 / 160 | — / 130 / — | 11.3 | 12.8 |
| HC | 3.5 | 12 | 370 / 400 / 455 | — / 80 / — | 10.7 | — |
| HT | 3.5 | 12 | 165 / 185 / 220 | — / 100 / — | 10.7 | 13.0 |
| C (claves) | 2.5 | 8 | — / 2500 / — | — / 25 / — | 10.1 | 9.5 |
| RS | 3 | 10 | H 1667, L 455 | — / 10 / — | 10.5 | 13.3 |
| M (maracas) | 3 | 5 | — | 25 / — / 35 | 4.4 | — |
| CP | 6 | 2 | — | — / 100 / — | −9.5 | — |
| CB | 3.5 | 12 | H 800, L 540 | — / 50 / — | 10.7 | 5.0 |
| CY | 3.5 | 7 | — | 350 / 800 / 1200 | 6.0 | 4.0 (TONE high), 6.8 (TONE low) |
| OH | 3.5 | 7 | — | 90 / 450 / 600 | 6.0 | 4.2 (long), 4.8 (DECAY 0) |
| CH | 3 | 6 | — | — / 50 / — | 6.0 | 4.0 |

Notes:

- **AO** is a real unit, described as an early revision, recorded, apparently, at the mix out,
  every voice accented and then plain (`reference/real808/archiveorg_tr-808-samples/808 accent
  levels.flac`). Its voices are unlabelled and were identified by ear and spectrum; the ACCENT knob
  position is not stated, and its knobs moved between takes. Its snare rings at 186/341 Hz, the
  late parts', so it is not from before that change. The set first taken for its cymbal (4.4 dB)
  is an open hat at DECAY 0 (its band profile and 60-75 ms decay are MF's OH00, and it lacks the
  cymbal's 3-4.5 kHz band); its cymbal hits are two quiet pairs, near -45 dBFS, matching MF's
  CY10xx and CY0010. The metal voices' ratios are RMS over 30 ms (p-p of noise varies hit to hit).
- The table's ratios follow the trig: about 10 dB (the 4.3/14.2 V common trig) for most main-board
  voices, 6 dB (the 6.8/13.5 V narrowed trig) for CY, OH and CH. **CB is the exception**: the table
  gives 10.7 dB although its trigger is the narrowed one; AO's ~5 dB fits the narrowed trig.
- The toms' and rim shot's accent in AO is about 3 dB over the trig ratio. In the toms the
  germanium diodes D80/D81 conduct harder on accented hits (§5); AO confirms it with a glide of
  92 → 83 Hz on accented LT and none on plain LT (Lemus measurement).
- Suspect entries: **CP accent 2 Vpp** is lower than normal (Werner: "maybe the accented value
  should be 12V?", W-blog/50876992798); **HT repeats the LC row** exactly (but see §5: the parts put
  both near 165-215 Hz); **SD's 238/476 Hz** are the early parts (§4); **MA has no DECAY knob** yet
  gets two decay figures.

**Lemus:** `tools/service/check.py` runs the table against the emulation (pass within 25 % on
amplitude and frequency, 40 % on decay), with SD checked at 172 Hz and the CP accent value skipped.
Accent is not fitted per voice: the ratios come out of the circuit.

### 2.8 Parts and semiconductor models

**Facts:**

- Schematic note (SN p. 8): unless marked, NPNs are 2SC945 (P), PNPs 2SA733 (P), Q23-Q26 2SA1015
  (GR), diodes 1S2473, op-amps µPC4558C. The parts list (SN p. 16) adds the 2SC828 (R) noise part,
  2SC2021, 2SK30ATM (Y, GR) FETs, the 1S188FM "Ge diode", BA662A "Vari-conductance amp." and AN6912
  quad comparator.
- Tolerances: "±20% capacitors and ±5% resistors" (W-BD §11).
- hFE ranks: 2SC945/2SA733 P = 200-400 (DS). 2SK30ATM IDSS: Y 1.2-3.0 mA, GR 2.6-6.5 mA.
- 1S2473: a Rohm part with a VF-IF curve from 0.2 mA (DS/rohm-1s2473). Werner used a 1N4148
  (IS 2.52 nA) in its place.
- 1S188FM: a Sanyo germanium point-contact diode; only a radiomuseum listing, **no datasheet**.
  Werner modelled it as silicon for lack of data (W-PhD App. C).
- BA662A: a Roland-custom Rohm OTA with no public sheet; stand-ins are the BA6110 and the Alfa
  AS662D clone sheet (DS).
- µPC4558: slew rate 1.0 V/µs (DS). Its gain-bandwidth is given only as a curve; the private digest
  reads about 1.5 MHz from the 1981 NEC sheet.

**Lemus** (`models.cir`): 2SC945, 2SA733, 2SA1015 Gummel-Poon (IS 1.0-1.2e-14, BF 280-300, VAF
90-110); 1S2473 IS 2.52 nA, N 1.752, RS 0.568 Ω; **1S188FM IS 0.2 µA, N 1.3, RS 10 Ω, a generic
germanium guess**; 2SK30A Y and GR as JFETs with IDSS 2.0 and 5.0 mA (our arithmetic: BETA·VTO²,
inside their ranks); op-amps single-pole at 3 MHz with soft clipping at ±13.5 V; the BA662 as a
textbook OTA; the AN6912 as an open-collector comparator on −15 V.

---

## 3. Bass drum (BD)

**Facts:**

- SN p. 14: 3.5 / 10 Vpp; 18 ms (56 Hz); decay 50 / 300 / 800 ms.
- SN p. 5: a multi-feedback bridged-T around IC12a; VR6 sets the decay by the feedback amount. On a
  trigger Q41-Q43 short R165, so the resonator runs at "twice its inherent frequency for a half
  cycle", for 4 ms (R156·C38), "1/2 x 1/2 of 16ms" (a 62.5 Hz period, not the table's 18 ms). When
  Q42 turns back on, C39 discharging through R161 "produces a retriggering pulse".
- Block structure (W-BD §2-9): trigger gate (Q39/Q40) → pulse shaper (R162, R163, C40, D53; a
  nonlinear low shelf) → bridged-T with a feedback buffer (IC12b, VR6, C43, into the centre through
  R170) → tone low-pass (VR5) → level → Q44 follower.
  - Pulse shaper falling edge ≈ `Vtrig·R162/(R162+R163) + 0.71` V.
  - Bridged-T alone: 45.7 Hz (our table, §2.2). With the R161 and R170 loading, "≈ 49.5 Hz".
  - Attack: with R165 grounded the resonator jumps to 2.81× (128.7 Hz, W-blog/50274769999) for
    "≈6 ms" (W-BD §8.1) or "≈5.4 ms" (W-PhD), giving the "punchier" attack.
  - "Sigh": a separate, slow downward glide from leakage through Q43's base when the resonator swings
    below a diode drop; larger hits glide more (W-BD §8.2; fitted `iC = (m/α)·log(1+e^(α(V−V0)))`,
    α 14.315, V0 0.556, m 1.4765e-5).
  - Decay is roughly linear in the DECAY knob; repeated hits differ because the resonator keeps its
    state (W-PhD conclusion). JA p. 9 calls the uneven level of quick repeats "not a fault".
- Output (W-PhD Table 1.1): R171 220, R172 10k, VR5 10k (C), C45 0.1 µF, VR4 100k (A), R176 100k,
  **R177 82k**, Q44, R173 6.8k, C48 220p, C50 1 µF, R174 100k. SN p. 9 marks R177 "SURFACE MOUNTING".
- Measured pitch: Deruty's library samples peak at 49.48 Hz, with a secondary 51.05 Hz for "short"
  presets and a median sweep of about a half-tone (DER §4.1). Owners report G (≈49 Hz) rising to A
  at full decay, "around 52hz", 49-56 Hz from video clips (GS). Werner's tracking of a recording
  settles near 49-50 Hz.
- Unit spread and drift: each original unit TT tested sounded different; some need 20 minutes to
  warm up (SOS).

**Disagreements:**

- Base frequency: 56 Hz (SN) vs 45.7 Hz (bare resonator) vs 49.5 Hz (Werner with loading, DER).
- Attack step and length: ×2 for 4 ms, "when ACCENT is present" (SN) vs ×2.81 for 5-6 ms on every
  hit (Werner) vs 5.3 ms simulated and 6.2 ms measured on a replica (WOLF) vs Plaits' ×2.7 for 6 ms
  and Peaks' +17 semitones for 4 ms (MI).
- W-PhD gives R164 as 47 kΩ (Table 2.3) and 4.7 kΩ (Table 4.2).
- Deruty's "half-tone" sweep is the slow sigh; W-BD warns that discussions conflate it with the
  >1 octave attack step.

**Models:** W-BD (Gen~ blocks, bilinear transform, SPICE-validated, qualitatively compared with MF);
W-PhD (full WDF model, needs 8× oversampling, validated against SPICE only); WOLF (Modelica, 160
equations, compared with a ±12 V replica); Plaits `AnalogBassDrum` and Peaks (behavioural, comments
naming circuit parts); clones by Archer and Tiptop.

**Lemus measurement:** without the R177/R176 divider (−5.2 dB at Q44's base, our arithmetic:
100/(100+82)), the BD reads 5 dB over the SN table's amplitude.

**Lemus:** `bd.cir`, every part as printed, including R177/R176, R164 47k, R159 100k and both
Q41-Q43 effects, so the attack step and the sigh come from the circuit on every hit. The fitted
unit changes only VR6's track: **585k** for the 500k (B) DECAY pot, which gives that unit's range
(to about 1.5 s at full).

---

## 4. Snare drum (SD)

**Facts:**

- SN p. 14: 3 / 10 Vpp; H 2.1 ms (476 Hz), L 4.2 ms (238 Hz); decay 60 ms.
- SN p. 6: two bridged-Ts for the fundamental and a harmonic; VR8 (SD TONE, 100k B) sets their
  ratio; VR9 (SNAPPY, 10k B) the amplitude of the snappy envelope.
- **"PORTION CHANGED" box (SN p. 9):** C58 .027 → .056, C61 .0068 → .015, R191 8.2k → 10k,
  R200 47k → zero. No serial number is given, and the schematic still prints the old values.
  - Early parts: 249.6 / 499.0 Hz; late parts: 173.3 / 336.0 Hz (§2.2). The table's 238/476 Hz
    matches the early parts. Werner reached the same conclusion: "the table ... was not updated with
    the circuit diagram" (W-blog/51352144814).
  - Owners hear it: early units have a higher-pitched snare (`docs/papers/web/matrixsynth/`); FM's
    calibrated unit was "a few semitones higher" than the TR-08; Whittle gives "about 250 and 500 Hz".
- Snappy path: VR9 divides the gated trigger into Q47, which charges C51; the envelope runs down
  R184/R185 into the swing VCA Q48, which chops the white noise; Q49 is a 2-pole Sallen-Key
  high-pass (C66/C67 1.8n, R201 22k, R202 47k), about 2.7 kHz (our arithmetic:
  1/(2π·1.8n·√(22k·47k))). Reid's block diagram sends the snappy signal back into the resonators;
  Werner calls that "a small mistake" from ambiguous arrows in Roland's block diagram.
- Unit spread: TT found "a lot of variation" in clarity, envelope length and accent response.

**Disagreements:** 238/476 Hz (table) vs 250/499 Hz (early parts) vs 173/336 Hz (late parts). Noise
filter: Plaits band-passes the noise at 16·f0 (MI); the circuit and Whittle's "Snappy noise pulse
high pass filter" say high-pass.

**Models:** Werner's 2013 ChucK sketch ("far from a complete circuit model"); Plaits
`AnalogSnareDrum` (two modes at 1:2, band-passed noise); Shier et al. 2024 (a differentiable
top-down snare after Reid). **No circuit-level SD model has been published.**

**Lemus measurement:** MF's snare's low resonator sits at **172 Hz**, i.e. that unit has the late
values.

**Lemus:** `sd.cir` with the "PORTION CHANGED" values (`Calibration::lateSnare`, default on). The
fitted unit raises the snappy VCA's bias resistor R194 from 2.2M to **4.7M**: half the collector
current, as a lower-gain 2SC945 would give, lets more noise through at normal level (log-spectral
distance to MF 3.4 → 1.6 dB).

---

## 5. Toms and congas (LT/LC, MT/MC, HT/HC)

**Facts:**

- SN p. 14 (low / mid / high tuning): LT 80/90/100 Hz, MT 120/135/160, HT 165/185/220, LC
  165/185/220, MC 250/280/310, HC 370/400/455; normal / accent 3-3.5 / 10-12 Vpp; mid decay
  80-200 ms.
- SN p. 6: a multi-feedback bridged-T per voice. SW8-SW10 switch tom/conga (a capacitor for the
  frequency, a resistor for the level). The large early swings make the diodes D80/D81 conduct,
  which "reduce time constant", so the pitch starts high and falls as the resonance dies. "Pink
  noise with a slightly longer decay time is mixed" in for "artificial reveberation"; the congas
  have none (WHIT §5, SOS Concussor review).
- The diodes are **1S188FM germanium** (SN p. 16, W-PhD App. C). Werner's blog says the 808 uses
  "only 1S2473 diodes" (W-blog/71221571621); the parts list contradicts it.
- The R1 leg: R231/R260/R287 (1k) to the tuning pot's wiper (VR11/VR13/VR15, 500 Ω B, the panel
  TUNING knobs), with the diode pair across to the pot's other end (SN p. 9). The inverting stage
  (IC15b etc.) feeds the output back into the centre through R230/R259/R286 (820k).
- With R1 = 1.0-1.5 kΩ the Fig. 11 formula lands on the table for all six voices (§2.2), and
  **HT really does overlap LC**: 166-204 Hz vs 175-214 Hz (our arithmetic). The duplicated table
  row may not be a copy error.
- No published measurement of a real tom's glide or decay exists (AO gives one, below). The RD-8's
  congas have "a less pronounced pitch envelope" than the 808's (SOS RD-8 review).

**Disagreements:** tom diode type (1S2473 vs 1S188FM); tom formula vs table (resolved above);
HT = LC row (likely genuine).

**Models:** none at circuit level. W-PhD treats only the linearised resonator as a WDF case study;
Hexinverter's AteOhAte toms keep the germanium pair.

**Lemus measurement:** AO's accented LT glides from 92 to 83 Hz; its plain LT does not glide. The
toms' accent ratio in AO is 12.8-13.3 dB, about 3 dB over the trig's 10.4 dB, which the diodes
account for.

**Lemus:** `lt.cir`, `mt.cir`, `ht.cir` as drawn, including the noise path (Q52/Q55/Q58, switched
out in conga mode) and the 1S188FM pair with the generic germanium model of §2.8. The fitted unit's
resonator parts, all within tolerance: LT C76 53.41n, C78 11.52n, C77 47.39n, R230 839.2k; MT C90
25.88n, C92 11.99n, C91 27.54n, R259 852k; HT C103 25.22n, C105 5.741n, C104 22.24n, R286 854.7k.

---

## 6. Rim shot and claves (RS/CL)

**Facts:**

- SN p. 14: CL 2.5 / 8 Vpp, 0.4 ms (2500 Hz), decay 25 ms; RS 3 / 10 Vpp, H 0.6 ms (1667 Hz),
  L 2.2 ms (455 Hz), decay 10 ms.
- SN p. 6: CL comes from the multi-feedback bridged-T around IC20 and goes to IC19; IC21's output
  "can be ignored" in CL mode. In RS mode, with R313 disconnected, IC20b is just a buffer; IC20's
  and IC21's outputs drive the swing VCA Q62, whose envelope is R107/C24. Q74 (2SK30A) keeps the
  output muted except while a trigger passes, "to eliminate noise leaking from IC20", which rings at
  high Q for the claves.
- SJ (from the SN schematic): GEN1 = IC21, GEN2 = IC20; for RS both are mixed into the Q62 VCA and
  the Q63 high-pass (R322 10k); for CL, R313 (390k) adds the feedback loop and the VCA is unused.
  The only panel controls are LEVEL and the RS/CL switch. A commenter fixed claves on a ±12 V clone
  by changing R313 from 390k to 220k.
- Which network makes RS H 1667 Hz was left open in the private digest. The schematic answers it:
  SW11 switches C118 (2.2n) across C117 in RS mode, so IC20a rings at about 1.8 kHz in RS and
  2.5 kHz in CL (§2.2).
- R324 (Q63's output resistor) has no printed value; the Yocto clone, which keeps Roland's
  designators, uses 220k (YOCTO).

**Models:** none at circuit level; SOS describes the claves as a bridged-T "kicked by a trigger and
allowed to decay".

**Lemus measurement:** with R313 at the printed 390k the claves do not decay in the model. MF's
claves fit R313 = **500k** with the mute capacitor C113 = **36n** (47n printed).

**Lemus:** `rs.cir` as drawn, R324 = 220k (Yocto). The fitted unit: R313 500k, C117 2.318n,
C119 2.213n, C113 36n, and R318 130k (220k in the netlist) to set the balance between the 455 Hz
and 1.7 kHz resonators into the rim shot's VCA. `Calibration::claveR313` keeps 390k for the nominal
unit.

---

## 7. Hand clap (CP)

**Facts:**

- SN p. 14: 6 / 2 Vpp (the accent figure is suspect, §2.7), decay 100 ms. TM-3 "C.P. OFFSET" is
  adjusted at TP-5 against a scope picture marked "6 VPP" and "0.5 VPP".
- SN p. 6 and Fig. 13: white noise through the band-pass IC21b feeds two VCAs. The swing VCA Q70,
  with a long envelope, is the "reverberation". The BA662 OTA (IC22), controlled through the
  exponential pair Q71/Q72, gives the sawtooth bursts. The burst envelope comes from the AN6912
  comparators (IC23, on −15 V only): a 30 ms window (R350·C140), Q73 snapping C144 to −15 V, C144
  discharging through R365/D71, re-fired until "the middle of the third time". Accent reaches Q71's
  base through D68, C143 and R362; TM3 sets its offset.
- Design changes at 010600 (SN p. 15): R346 1k → 680 and R332 22k → 27k, because the clap
  "overmatches the rest in level". The "PORTION CHANGED" box adds a 10k ("called R200") from Q70's
  emitter to −15 V.
- C140 is printed **.0027** µF on SN p. 9. Archer's redrawn clap (ARCH/clap-raca) and the Yocto
  clone use **27n**, and R350·C140 = 1M·27n = 27 ms matches the SN's "pulses of 30ms wide" (our
  arithmetic); 2.7n would give 2.7 ms.
- Number of bursts: three short bursts and then a long decay in SN Fig. 13 and the TM-3 picture;
  Archer and Delp count four peaks ("quadruple envelope"); Whittle "three or four". Counting the
  long last one as a burst would reconcile them (our reading).
- "No two Hand Clap sounds are the same" (WHIT §6-7); the clap is insensitive to trigger level.
- Timing claims beyond the SN are weak: a KVR summary only, and FM's "more flam" than the TR-08.

**Models:** none at circuit level.

**Lemus:** `cp.cir` as drawn, with C140 = 27n, the late R346/R332/R106 and the added R200
(`Calibration::lateClap`), the BA662 as an OTA and the AN6912 as comparators. The fitted unit sets
TM3 to **0.26** of its rotation, fitted to Michael Fischer's clap (envelope and spectrum; the SN
picture's 6 / 0.5 Vpp is bursts 21.6 dB over the gaps).

---

## 8. Maracas (MA)

**Facts:**

- SN p. 14: 3 / 5 Vpp, decay 25 (short) and 35 (long) ms, although MA has only a LEVEL control
  (OM p. 22).
- SN p. 6: white noise gated by Q65 and fed through the high-pass Q68 into the buffer IC19, which
  it shares with the clap; the envelope comes from Q66/Q67.
- The table's 4.4 dB accent ratio is the smallest of the voices on the common trig, apart from
  the suspect CP figure.
- Tiptop's MA808 clone has an attack knob whose mark is "the attack position of the original
  circuit" (TT).

**Models:** none.

**Lemus:** `ma.cir` as drawn (Q67/Q66 window from C135/R345 and C134/R341, Q65 noise gate, D65,
Q68 high-pass, IC19b shared with the clap via SW12); no fitted parts.

---

## 9. Cowbell (CB)

**Facts:**

- SN p. 14: 3.5 / 12 Vpp; H 1.25 ms (800 Hz), L 1.85 ms (540 Hz); decay 50 ms.
- SN p. 6: the two trimmed Schmitt oscillators (§2.5) each pass a gate (the swing VCAs Q14, Q15)
  and are mixed by the band-pass IC2b. R82 and C34 in parallel with C9 give the envelope an "abrupt
  level decay" at first, for attack.
- Archer: "two square wave oscillators, two VCAs, a triggered envelope generator, Hi-Q bandpass
  filter, and buffer amp"; 800/540 is "a detuned perfect 5th"; the band-pass is a bridged-T "easily
  tunable by varying R24" with enough Q to add "a thump of its own" (ARCH/cowbell).
- SOS's 587/845 Hz were measured on a CR-8000, not an 808.
- Werner found 1N914 vs 1N4148 diodes made no audible difference in SPICE (W-blog/72061773154).
- FM: the 808's cowbell was "fuller" than the TR-08's.
- Its trigger is the narrowed one (§2.1), yet the table's accent ratio is 10.7 dB; AO shows ~5 dB.

**Models:** Werner, Abel and Smith's "More Cowbell" (AES 2014) was not obtained. Clones: Tiptop
CB808, SyntherJack.

**Lemus measurement:** the swing VCAs' 10 µF coupling capacitors (C32, C33) settle into an AC steady
state set by the free-running squares, about a tenth of a volt off the operating point of the
squares' first instant. Until they reach it the VCAs hardly chop and the cowbell is 12 dB down. At
that state the cowbell is about 3 V p-p at the jack (table: 3.5 V).

**Lemus:** `cb.cir` as drawn. The board runs the metal source and the voices it feeds until they
settle at power-on (`VoiceBoard::reset`), so the first hit is already at steady state. The fitted
unit: TM1/TM2 558.3 / 823.6 Hz (§2.5) and the envelope C9 0.6804 µF (0.47 printed), C34 1.832 µF
(1 µF), R82 26.27k (33k).

---

## 10. Cymbal (CY)

**Facts:**

- SN p. 14: 3.5 / 7 Vpp; decay 350 / 800 / 1200 ms.
- SN p. 6: the six oscillators' sum is split into high and low bands by IC3. The high band splits
  again: Q16's gate passes the highest component with a short decay, Q17's a slightly lower one
  whose decay is controllable; Q18 gates the low band. VR4 (TONE, 20k B) sets their level ratio.
- W-CY:
  - Band-passes: third-order, centres "around 3440 Hz" and "around 7100 Hz" (§4). Electric Druid
    computes 3.45 and 7.12 kHz, Q 6.05, for the DR-110's near-identical pair (ED).
  - Attack smoother Q19 (§6): a one-pole on Vtrig − VBE, VBE 0.7258, τ 0.10244 ms.
  - Three envelopes via D6-D8 (§7): the decay pot VR2 (2M B) discharges C41 through R93; Q20
    follows it.
  - Three swing VCAs (§8); high-passes (§9): a Sallen-Key with Q25, a second-order op-amp stage and
    a third-order stage resonant "around 10500 Hz".
  - Tone stage (§10): fifth-order per band; mainly sets the third band's attenuation; controls are
    "weakly-separated, non-orthogonal". Output buffer (§11) rises 6 dB/octave in band.
  - Implementation: bilinear transform at 4×, unit delays in the EG/VCA loops; validated against
    SPICE only.
- An early unit's cymbals were less "bright and clean" than a later one's
  (`docs/papers/web/matrixsynth/`); FM names hats and cymbal as the biggest unit differences.
- Rickardsson's simulation puts the hi-hat filter region at "15-18 kHz" (MW); W-CY's third
  high-pass is around 10.5 kHz. They may describe different stages.

**Models:** W-CY (the only published circuit-level model of a metallic voice); Plaits `HiHat`
pushed "to the CY territory" (MI).

**Lemus:** `cy.cir` from SN p. 13, read with W-CY as a guide: Q19 charging the three envelopes,
Q20 coupling the low envelope into the middle one, the three swing VCAs, IC5a/IC5b and Q25
high-passes, TONE and IC6b. Q25's emitter resistor R126 returns to −B2 as drawn (an early netlist
had it to ground, which cut Q25 off at rest and half-wave rectified the low band: the cymbal's
spectral distance to MF went from 4.9 to 1.6 dB when it was put right). The fitted unit: VR2 track
1.57M (2M), C41 1.05 µF (1 µF), R125 3.55k (2.2k, TONE's balance), and MF's metal frequencies
(§2.5).

---

## 11. Open and closed hi-hat (OH, CH)

**Facts:**

- SN p. 14: OH 3.5 / 7 Vpp, decay 90 / 450 / 600 ms; CH 3 / 6 Vpp, decay 50 ms.
- SN p. 6: both take IC3's high band. OH: gate Q27, filter Q26, buffer IC7a. CH: gate Q30, filter
  Q31, buffer IC7b. A CH trigger turns Q23 on through R173, and "the decay time of the OH circuit
  terminates".
- Filters (SN p. 13 values): OH C66/C68 1.5n, R147 2.7k, R146 68k; CH C72/C73 1n, R153 2.7k, R155
  68k. Equal-C Sallen-Key high-passes at about 7.9 kHz (OH) and 11.8 kHz (CH) (our arithmetic,
  f = 1/(2πC√(R1R2))).
- The OH envelope (Q24, C62) is held up by its decay timer: Q21 empties C60 at the trigger, C60
  recharges through OH DECAY (VR3, 2M B) and R132, and when Q22 turns on it dumps C62 through R134
  (100k). The CH envelope drives Q23, which does the same at once (SN p. 13, as read in
  `hats.cir`).
- Werner and Tong's clone sounded "pretty close to the original" once the final high-pass stage
  was added (W-blog/67452426655).

**Disagreements:** the choke's shape. SN: the OH decay "terminates"; Tiptop: the CH envelope is
"forced on" the OH VCA; Hexinverter: the tail is shortened, a fade (their "FADE" mode). The circuit
as read gives a fast discharge of C62 through R134, τ about 47 ms (our arithmetic: 0.47 µF ×
100k), which is closer to Hexinverter's fade than to a cut.

**Models:** none at circuit level; Plaits and Peaks have 808-style hats; W-CY covers the shared
oscillators, band-pass and VCA topology.

**Lemus:** one netlist for both (`hats.cir`), so the choke is the circuit's: Q23 discharging C62
through R134. The fitted unit's OH DECAY timer: VR3 track 1.907M (2M), R132 114.7k (100k), C60
8.118 µF (10 µF).

---

## 12. Sequencer and firmware sources

Kept brief: `docs/sequencer.md` covers the behaviour.

- **CPU and timing (SN pp. 3-5, 8, 14).** NEC µPD650C-085 (µCOM-43 family) at 500 kHz (IFT-1, 2 µs);
  INT clock 1.9 ms (TM-1); scan cycle 7.6 ms. Instrument data on the ports: PE0-3 = CP, RS, HT, MT;
  PD0-3 = CH, OH, CY, CB; PF0-3 = LT, SD, BD, AC; step pulse on PI2. Four µPD444C RAMs (map on
  p. 4). Tempo clock: 24 clocks per quarter note, 8.33 ms at maximum (TM-2) to 65 ± 5 ms (p. 14),
  i.e. 300 to about 38 BPM (our arithmetic: 60/(24·T)); spec 40-300.
- **DIN sync.** OM gives the pins (1 start/stop, 2 GND, 3 clock) but not the clock rate. The SN's
  tempo clock is the clock on that line (pin 3, SN p. 8), so it is 24 ppqn (SN p. 5).
- **Design changes affecting the CPU board (SN p. 15).** INT-clock capacitor (000300), back-up
  current R91 (010600), DIN pin 5 filtering (010600), power-on/off RAM protection FETs Q64/Q75
  (020800).
- **Owner's manuals.** OM, JA and IN, digested in `private/docs/operation-digest.md`: pre-scale
  (steps per quarter: 1→3, 2→6, 3→4, 4→8), 1st/2nd parts up to 32 steps, A/AB/B variations,
  intro/fill and AUTO FILL IN, real-time compose of 12 tracks × 64 bars, accent as instrument 1,
  trigger outs on the CB, CP and AC rows. The manuals leave open what the 1st/2nd PART LEDs show,
  how AUTO FILL counts bars, the factory tracks' contents and the power-on defaults; running the
  firmware answers most of these.
- **ROM.** `d650c-085.ic4`, 2048 bytes, SHA1 `eddac7af6396c3f220914d9cad2c9e398872b034` (MAME's
  dump), kept in the private ROM folder, never in this repository.
- **MAME.** `roland_tr808.cpp` is a skeleton: the µPD650 at 500 kHz, a 1.9 ms IRQ clock, the ROM
  definition, and "TODO: everything"; flagged `MACHINE_NO_SOUND | MACHINE_NOT_WORKING`.

**Lemus:** the real firmware runs on `core/ucom43.cpp`, a standalone port of MAME's `ucom4` core
(a copy of the TB-303's), inside a model of the CPU board (`core/machine.*`): switch matrix, RAMs
and decoder, LEDs and drivers, the status inputs (run, fill-in, reset & start, the 24 ppqn clock).
The step pulse and instrument data drive the voice board's trigger circuit. `tools/tr808seq`
scripts the panel and logs the triggers to check the firmware against the manuals.

---

## 13. Disagreements between sources (summary)

| # | Quantity | Values found | Lemus |
|---|---|---|---|
| 1 | Unaccented trig | 4 V (SN, W-PhD, Hartman) / 5 V (Whittle, MB808) / 8 V (WOLF) | 4.3 V (netlist) |
| 2 | Accented trig | 4-14 V (SN) / 3.5-14.5 V (Whittle's old knobs) | to 14.2 V; narrowed 6.6-13.5 V |
| 3 | Accent / normal ratio | Table ~9-11 dB (CB 10.7), CY/OH/CH 6 dB / AO: BD 9.7, SD 9.1, toms 12.8-13.3, RS 13.3, CL 9.5, CB 5.5, OH 4.2-4.4, CH 4.3, CY 5.3-6.6 dB (RMS) | From the circuit; the metal voices' through IC2a's output limit (11.4 V in the fitted unit) |
| 4 | BD frequency | 56 Hz (SN) / 45.7 (bare formula) / 49.5 (Werner, DER) / 49-56 (owners) | Printed parts |
| 5 | BD attack step | ×2 for 4 ms on accent (SN) / ×2.81, 5-6 ms, every hit (Werner) / 6.2 ms (WOLF replica) | Circuit (Q41-Q43) |
| 6 | BD R164 | 47k (W-PhD Table 2.3) / 4.7k (W-PhD Table 4.2) | 47k |
| 7 | SD frequencies | 238/476 Hz (table) / 250/499 (early parts) / 173/336 (late parts) / 172 (MF) | Late parts |
| 8 | SD noise filter | Band-pass at 16·f0 (Plaits) / high-pass (circuit, Whittle) | High-pass, ~2.7 kHz |
| 9 | Tom frequencies | ×1.7 the table with Werner's 333-500 Ω leg / on the table with R231 1k + VR11 | Schematic leg |
| 10 | Tom diodes | 1S2473 (W-blog) / 1S188FM Ge (SN parts list, W-PhD) | 1S188FM, generic Ge model |
| 11 | HT row = LC row | Copy error? / the parts put both at ~165-215 Hz | Parts |
| 12 | RS H 1667 Hz | Unattributed / IC20a with C118, ~1.8 kHz (schematic) | Schematic |
| 13 | Claves R313 | 390k (SN, SJ) / 220k (±12 V clone fix) / 500k (MF fit) | 500k (fitted unit) |
| 14 | CP C140 | .0027 µF (SN p. 9) / 27n (Archer, Yocto, the SN's 30 ms) | 27n |
| 15 | CP bursts | Three plus a long decay (SN) / four (Archer, Delp) / three or four (Whittle) | Circuit |
| 16 | CP accent Vpp | 2 Vpp, below normal (SN table) / "maybe ... 12V" (Werner) | Not used |
| 17 | Metal frequencies | 205/370/304/523 Hz computed (W-CY) / Plaits 414-1050 / Klein (TR-606) / MF fit 199/379/288/486 | MF fit |
| 18 | Cowbell trims | 1.85/1.25 ms (SN) / swapped in lots 1-2 / 1.79/1.21 ms (MF fit) / 1.78/1.19 (a clone) | MF fit |
| 19 | OH choke | "terminates" (SN) / CH envelope forced on OH (Tiptop) / fade (Hexinverter) | Circuit: ~47 ms discharge |
| 20 | Hat filter region | 15-18 kHz (Rickardsson sim) / ~10.5 kHz (W-CY HPF #3) / OH 7.9, CH 11.8 kHz (SN values) | SN values |
| 21 | Tom pink noise | "single pole" (Werner) / 2-pole Sallen-Key ~270 Hz (SN values) | SN values |
| 22 | Noise circuit | Original / "6 lot ~" (SN p. 8) | "6 lot ~" |
| 23 | µPC4558 GBW | ~1.5 MHz (read from the 1981 curve) / 3 MHz (`models.cir`) | 3 MHz |
| 24 | Resonator name | "twin-T" (MB808, Hexinverter) / bridged-T (SN, Werner, Archer, Klein) | Bridged-T |

---

## 14. Open questions

1. **Accent level per voice.** AO is one early unit with an unknown ACCENT knob and unlabelled
   voices. A second accent reference with documented settings (Cluster Sound CS-808 has a BD
   accent × tone × decay grid) would show whether the toms' and rim shot's extra 3 dB and the
   cowbell's ~5 dB are typical, and whether the table's CB figure is simply wrong.
2. **1S188FM.** No datasheet; the model is a generic germanium guess. The toms' glide and accent
   boost depend on it. Fit IS/N/RS to AO's 92 → 83 Hz accented LT glide and MF's tunings.
3. **Claves R313.** Why the printed 390k does not decay in the model: a misprint, a unit-specific
   value, or a model error (op-amp bandwidth, the Q74 mute, C113)?
4. **Fits outside tolerance.** The cymbal's R125 (+61 %) and the rim shot's R318 (115k against
   220k) remain. Settled: the cowbell's C9 and C34 (+45 %, +83 %) were standing in for the metal
   squares' duty cycle (0.545, not 0.47; C34 is now +13 %), SD R194 (doubled) for a hot noise trim
   (TM4 190 mV rms against the notes' 130), and the cymbal's C41 (+46 %) for R126's connection.
5. **BD base frequency.** The SN's 56 Hz against ~49.5 Hz everywhere else; check the netlist's
   settled pitch at each DECAY mark against MF and DER.
6. **Early vs late units.** Lemus plays a late unit (MF's). The early snare, the lots 1-2 cowbell
   assignment and the pre-000300 cowbell values (C5 not given; R45 330k could not reach 800 Hz) are
   not modelled as options.
7. **Unprinted or misprinted values.** R324 (the Yocto clone's 220k leaves the rim shot 3-5 dB
   quiet against the table and MF: 150k nominal, 120k for MF's unit) and C140 (.0027 vs 27n).
8. **Noise.** Gaussian white noise is assumed. No spectrum or crest factor of a real 2SC828 source
   is published; RAUM45's clean 24-bit takes could give one.
9. **OH choke.** The ~47 ms discharge is a reading of the netlist; check against a recording of an
   OH choked by a CH (BAMTV or RAUM45, if they contain one).
10. **Metal oscillators.** Only one unit's six frequencies are known (MF, fitted from spectra).
    The duty cycle comes from MF's cowbell lines (0.545; the HD14584's typical thresholds give
    0.47); the 4.7 V rail is not measured.
11. **MA decay figures.** The table gives MA two decay times though it has no DECAY control.
12. **Op-amp bandwidth.** 1.5 vs 3 MHz; matters most for the claves (2.5 kHz at high Q) and the
    cymbal's 7-10 kHz stages.
13. **Sequencer.** Open points from the manuals (1st/2nd PART LEDs, AUTO FILL counting, factory
    track contents, power-on defaults) are settled by running the firmware; see `docs/sequencer.md`.
