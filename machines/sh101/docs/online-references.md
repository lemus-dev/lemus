# SH-101: real-unit references found online

A search (2026-10-08) for anything that does for the SH-101 what Din Sync's 400-note set did for
the TB-303: recordings of real units at documented settings, or bench measurements. There is no
such set. What there is:
- one YouTube comparison whose panel photos are sharp enough to read every slider (Friendly
  Noise, below);
- Open Music Labs' bench measurements of an original IR3109;
- a few forum readings.

The downloads are in `private/reference/online/` (one folder per source, each with a README:
URL, date, terms, SHA1, settings). The analysis scripts are `tools/compare/online_notes.py` and
`online_sliders.py`, plus the release fit below.

Reliability labels: **solid** = settings read from a sharp photo and a clean measurement;
**fair** = one reading uncertain or indirect; **weak** = context or instrument unclear.

## 1. Friendly Noise: SH-101 vs Softube Model 82 (YouTube eStzYAZoOAI)

A real grey SH-101, each chapter preceded by a still photo of its panel. We read the photos at
720p and 1080p; the earlier shootout notes used 360p. Takes are peak-normalised separately, so
levels compare only within a take. The audio is YouTube's Opus.

### 1.1 The saw with RES 10 and the ENV at 10 (video 96-113.6 s)

**Settings (solid).** Saw 10 only; FREQ 0, RES 10, ENV 10, MOD 0, KYBD 10; VCA ENV, GATE+TRIG;
A D S R all 10; RANGE 16', TRANSPOSE L, portamento off.

**The note.** The saw plays at 261.5 Hz (C4 at A440). On this unit's tuning, KCV + RANGE ≈ 4.0 V,
so with RANGE 16' (1 V) the KCV is ≈ 3.0 V. That would be the top C at TRANSPOSE L.

**The take.** The resonant peak, which self-oscillates over the saw, sweeps up with the attack,
holds, and falls with the release. The video's own edit cuts the release at about 113.5 s, where
it switches to the Model 82. So the 51 Hz at the end of the shootout clip is the falling release
when the edit cuts it, **not the ENV's rest point**. `roland-measurements.md` reads it as
"51 Hz → 4.86 kHz, 6.6 octaves", and that's wrong.

**Release fit (solid).** We tracked the peak at 25 ms steps from note-off (video ≈ 103.6 s) to the
cut and fitted log2 f = b + D·e^(−t/τ). `tools/compare/online_release_fit.py fn_full.wav 103.6
113.4` gives rest 31.2 Hz, depth 7.02 oct, τ 4.23 s. Sub-windows:

| Fit window | Rest (ENV 0) | ENV-10 depth | τ | RMS error |
|---|---|---|---|---|
| whole release | 31.7 Hz | 7.03 oct | 4.17 s | 0.025 oct |
| first 3.2 s | 25.9 Hz | 7.31 oct | 4.41 s | 0.036 oct |
| last 5.9 s | 32.2 Hz | 7.13 oct | 4.09 s | 0.016 oct |
| τ forced to 4.7 s (schematic) | 25.6 Hz | 7.18 oct | – | 0.056 oct |

- The sustained peak is ≈ 4.8 kHz (S 10, so sustain = ENV peak).
- **Rest ≈ 26-32 Hz, ENV-10 depth ≈ 7.0-7.3 octaves, release τ at R 10 ≈ 4.1-4.4 s.**

**Attack (fair).** From 53 Hz to 3.8 kHz in 2.95 s, reaching the plateau at about 3.2-3.3 s. That
is roughly linear in octaves (≈2.2 oct/s). Reid's "about 3 s" agrees.

**Self-oscillation level (fair).** At the plateau the 4.8 kHz self-oscillation line is +0.9 dB
over the saw's fundamental (spectral peaks). As band power it is +10.6 dB over all the saw's
harmonics together, about 91 % of the output power; the line is broad, so its band power exceeds
its peak.

### 1.2 The sub with RES 10 (video 327-348 s): KYBD tracking

**Settings (solid).** Sub 10 only (2 OCT DOWN square), RANGE 16'; FREQ ≈ 1.5, RES 10, ENV 0,
MOD 0, KYBD 10. The notes come from a sequencer through CV/GATE IN.

**Measurement.** Each sub edge rings the filter at its resonance. Across 43 notes (sub 30.4-84.8 Hz,
1.48 octaves), the ringing frequency against the note fits **1.024 ± 0.021 oct/oct** (RMS 0.075 oct;
`online_notes.py`). The ringing sits at ≈2.75× the sub's fundamental. **(solid)**

**A FREQ-law point (fair).** The VCO is 4× the sub. On the 440 Hz tuning of §1.1, the lowest note
(sub 30.6 Hz, VCO 122 Hz) is at KCV ≈ 1.90 V, with its resonance at 82.6 Hz. Taking KYBD as
1.024 oct/V, that means **≈ 21 Hz at KCV 0 with FREQ ≈ 1.5**.

Against §1.1 (28 Hz at KCV 3.0 V, so ≈ 3.4 Hz at KCV 0 with FREQ 0), FREQ 0 → 1.5 spans
≈ 2.6 octaves. The FREQ reading is ±0.3 division, so that is 1.5-2.2 octaves per division at the
bottom of the slider.

### 1.3 The same patch with RES 0 (video 69-81 s): a conflict

**Settings (solid).** The same as §1.1 except RES 0.

**The conflict.** The plateau spectrum (saw at 261.5 Hz) falls faster than an ideal saw:
−2.5 / −5.6 / −8.6 / −20 / −37 dB extra at h2 / h3 / h4 / h8 / h16. Fitting four equal poles to
those numbers puts the pole at 0.8-1.5 kHz. That is about 1.7 octaves below the 4.8 kHz
self-oscillation of §1.1.

Either the sliders weren't where the photos show for one of the takes, or resonance raises this
unit's cutoff a lot; the four-pole fit is also poor, which needs explaining. **(conflicting;
don't fit to it alone)**

### 1.4 What it means

| Quantity | This unit | Roland plug-in | Our schematic model |
|---|---|---|---|
| ENV 10 depth | 7.0-7.3 oct | 7.65 oct | ≥ 8.1 oct with KYBD trimmed (Roland cal. 9.6) |
| KYBD 10 | 1.02 oct/oct | 0.995 | 1.000 (trimmed) |
| FREQ 0, KYBD 10, KCV 3.0 V | 26-32 Hz | about 54 Hz¹ | Roland cal. ≈ 27 Hz, service cal. ≈ 8.5 Hz |
| FREQ 0 at KCV 0 | ≈ 3.4 Hz | 3.4 Hz (its pivot differs) | Roland cal. ≈ 3.5 Hz, service ≈ 1 Hz |
| Release τ at R 10 | 4.1-4.4 s | 4.94 s | 4.7 s (envPotScale 1.0) |
| Attack at A 10 | ≈ 3.2-3.3 s | 3.6 s | 3.7 s |

¹ Roland's KYBD pivots near C0, so its value at KCV 3.0 V isn't comparable one to one.

- On one unit, **KYBD at 1.02 oct/oct and ENV-10 at ~7.2 octaves together break the transcribed
  ENV/KYBD ratio**, which needs at least 8.1 octaves when KYBD tracks 1 oct/oct. The ENV path, or
  the ENV's swing, is about 0.85-0.9 of the schematic's on this unit, or the transcription is off
  there (R189, R193, R192, the ENV threshold divider R158 / SUSTAIN).
- The ENV's timing on this unit is about 0.9× nominal, for both attack and release.

## 2. The IR3109 itself (Open Music Labs' bench work, via the Mod Wiggler reverse-engineering thread)

From `private/docs/papers/forums/ir3109_modwiggler_reverse_engineering_thread.txt`, measured on
original chips:
- **Expo slope:** gain about −290 dB/V, i.e. **≈ 20.8 mV/oct**, linear over 100 dB. It saturates
  internally at 700 µA and was measurable down to about 2 nA. **(solid, original chip)**
- **Expo bias:** the original's expo runs at **3 µA** where an AS3109 runs at 1.8 µA ("some
  trimming might be required"). That is the OTAs' current at 0 V on the control input, so it's
  the closest thing to our `vcfI0uA` for an original chip. **(fair: one comparison, method not
  detailed)** Our calibrations use 2.4 µA (service) and 5.0 µA (Roland).
- **emuarc (2022):** an AS3109 retrimmed to match an original SMD IR3109 spans "+108 to −120 mV"
  of control and resonates from below 50 Hz to beyond 20 kHz. **(weak: host circuit not given)**
- **Jackdamery:** an original IR3109's pin 9 reads −0.36 V with no filtering and −0.1 V fully
  open. **(weak: instrument and reference point not given)**
- **Pugris (Mod Wiggler p3692744):** pin 9 on an SH-101 moved 4.90-5.01 V with FREQ, at RES max.
  That unit was faulty (VCF WIDTH did nothing, no KYBD response) and the bus wasn't measured.
  **(unreliable; research.md §4 cites it)**

## 3. Leads not finished

- **Jorb, "Classic, Simple, & Still Very Special // Roland's SH-101"** (YouTube jl52FJCdrgk;
  435-730 s in `private/reference/online/jorb_jl52FJCdrgk/`).
  - Split screen: an oscilloscope, a sharp close-up of the VCF/VCA/ENV sliders, and the whole unit.
  - `online_sliders.py` reads the VCF and ENV slider positions frame by frame (the caps' orange
    index lines).
  - Notes are hand-played with sliders moving, and RES reaches only 7.4 in the filter chapter.
  - Excerpt 58.5-75 s holds FREQ 2.1, RES 7.4, ENV 5.3, KYBD 9.4, S 10 while notes play: a test
    case for the ENV slider's law at 5.3, if the notes can be pinned.
  - A Strymon Volante sits in the setup, so the audio may carry its echo.
- **Starsky Carr, Model 82 vs SH-101** (in the shootout; 655-690 s fetched to
  `private/reference/online/starsky-carr_czXml-N7eOI/`): a self-oscillation sweep with KYBD off,
  filmed from above with a spectrum analyser overlaid. The hand covers the VCF sliders while it
  sweeps, and the uploader calls the unit uncalibrated, so the sweep isn't usable.

## 4. Checked, nothing usable

- **Din Sync:** only the RE-101/R-101 replica announcements (Superbooth 2023, 2026). No reference
  recordings like the TB-303 set.
- **Freesound:** the sh101 tag has 25 sounds; those with real units are already in the shootout,
  and none gives settings.
- **archive.org:** manuals, service notes, MusicRadar's samples (no settings), live jams.
- **Academic work:** no DAFx or AES paper models the SH-101 or the IR3109 from measurements.
- **GitHub:** SH-101 projects are firmware, sequencers, Pure Data patches and remakes, with no
  real-unit data. mwaudio101 is already local.
- **Sample packs:** Liveschool, Cluster Sound, Goldbaby, Mr. Card and Francis Prève don't log
  per-sample settings.
- **Forums:** no measured cutoff curve, ENV times or LFO range for a real unit beyond §2. Roland's
  spec (10 Hz-20 kHz, ENV 1.5 ms-4 s / 2 ms-10 s, LFO 0.1-30 Hz) is all there is.
- **YouTube:** searches for calibration, tracking, filter, envelope and LFO tests found nothing new
  with legible settings and measurable audio.
  - Scott's Synth Stuff's restoration (23T7WGZgzJk) has a 20 s calibration chapter and fits
    Tubbutec's replacement envelopes.
  - Other channels' clips are performances.

## 5. Real boards: part values and voltages (2026-10-09)

A search for photos and measurements of real boards, to check the parts behind the two
unexplained calibrations (the ENV's weight, the VCA's turn-on). The material is kept in
`private/docs/papers/opensource/dinsync_re101/` and `private/reference/online/`.

- **The service notes' own circuit-board diagram** (p.6 of the 1982 printing): R162 sits beside
  IC16, between IC16b's side and D33, where `circuit-filter.md` 5.4 places it. The VCA area also
  has two dashed (unpopulated) positions, **VR32 "VCA OFFSET"** and **R201**. They null the BA662A's
  DC offset (the thump); they don't move where the VCA opens. A dashed **R202** sits near TR32.
  **(solid)**
- **Din Sync, RE-101 BOM v0.28 and build guide v0.14** (2023–2024; `RE101-BUILD-AND-BOM.zip`
  from shop.re-303.com). "We took apart an actual SH-101 for this project and there are some
  subtle differences between the service notes and the actual machine."
  - **R188 100k**, not the 47k printed: "actual value used on original sh101) result is increased
    modulation depth". **R169 100k** too. **(solid: a dismantled unit)**
  - R142/R143/R41 are a labelling mix-up, values unchanged: 12k in series from TR30, 33k to the
    bus.
  - Every other value on the ENV, VCA and cutoff-CV paths matches the transcription: R158 82k,
    R160 47k, R162 100k, R133 12k, R197 3.3k, R196 120k, R189 100k, R192 33k, R193 150k, VR19 and
    VR24 100kB.
  - Conflicts, not adopted: R31 and R35 100k (OML: 47k; `circuit-osc.md` 5.3 says why 47k is
    kept); C57 0.47 µF (drawn 0.047; Roland's 2–3 ms GATE edges fit 0.047); jack board C12
    4.7 nF (drawn 0.001; it would put the output's low-pass at 15 kHz).
- **Driessen Music, control-board repair photo** (`private/docs/photos/pcb/driessen_control_board_ic18_ir9022.jpg`,
  2560 × 1920): colour bands read R186 200k, R187 680k, **R188 100k**, R189 100k, R191 100k,
  R192 33k, R193 150k, all sure. That's a second board with R188 at 100k. **(solid)**
- **The Goob, ModWiggler "SH-101 Envelope Sustain/Release Issue. Help!" (2016-08-15)**: meter
  readings written on the ENV circuit, one unit. Gate on, S 10: ENV **10.29 V**, threshold node
  10.26 V, bus 5.03 V, +15 V rail 14.8 V. So the peak is **5.26 V above the bus**, against 5.37 V
  from the drawn parts at his rails (2 % under). At rest the ENV reads 5.05 V. IC16 was a TL072 at
  the time, fitted while chasing a release fault; the gate-on readings don't touch that fault.
  His redraw has R162 where we put it. **(fair: one unit, a meter)**
- **Synth-board photos of five more units** (Reverb listings 92734900, 26100675, 36824606,
  87937383; Secret Life of Synthesizers; a Delatronics video; `private/docs/photos/sources.md`):
  - R158 82k on three units, R160 47k (so the schematic's "+7k" is 47k), R161 and R159 33k,
    R146 and R144 100k, R148 220 Ω, R163 47 Ω, R133 12k, R138/R139 1k, R140 47k. **(solid)**
  - R162 is fitted on every unit seen, beside IC16. R202, R201 and VR32 are empty on every one.
  - The board labels R142 33k and R143 12k: the swap Din Sync describes, values unchanged.
  - Slider legends on two bare control boards: VR19 ENV 100KB, VR20 MOD 100KA, VR24 S 100KB.
  - IC16 is a Sharp IR9022 on three units. The component side reads 052H398-1 and -2 on about
    eight units, with no B or C suffix.
  - Not readable on any photo found: R31, R35, R169, and the control board's resistors other than
    the Driessen close-up's.
- **AMSynths AM8112** (SN-101 VCA & ADSR): "the envelope voltage amplified by 1.25" to 0–10 V on
  12 V. From the schematic, not a measurement.

What it settles:
- R162's place. R189, R192 and R193 (the ENV-against-KYBD ratio) are as drawn. So is the ENV's
  peak, within 2 % on the one unit measured. None of them explains the Real unit calibration's
  0.77 ENV weight. What's left is the filter chip's law away from the middle of its range.
- **R169 and R188 are 100k.** The model uses them since 2026-10-09: VCO MOD at 10 is now 7 % from
  Roland's (it was 25 % short), and the LFO's top is 30.6 Hz against the spec's 30.

Searched with nothing more: ModWiggler threads through the Wayback Machine (the live site
blocks); Gearspace (blocked, not archived); Electro-Music; Vintage Synth Explorer; GroupDIY;
Hyperreal; AMSynths, Superlative, Bubblesound, Electric Druid, Tubbutec pages; Japanese searches
for ENV-out mods and IC16/TR28 measurements. No healthy unit's voltages at TR28 or IC15, and no
measured VCF cutoff or ENV sweep, turned up.

## 6. Round two (2026-10-23)

- **A real unit's top LFO rate** (Perfect Circuit, YouTube biuWnvJ0f7I, 3:38.5–3:56.5; the shootout's
  `roland_sh-101_real__perfect-circuit_…__lfo_rate_sweep.wav`). The LFO modulates a 349.9 Hz tone
  while RATE is swept up. From 14 s on, its sidebands sit steadily 38.2 Hz apart (27.6 Hz at 12–14.5 s,
  still rising): RATE 10 is 38.2 Hz. With R31 and R35 at 47k the model gave 30.6 Hz (28–35 across the
  parts' spread); at 100k, as Din Sync's BOM has them, 36.4–36.7 Hz. The model uses 100k.
- **R196's place** (the service notes' 1982 board diagram, control board): the traces run S6's ENV
  pad → R196 → R197 → pin 85, so R196 feeds S6's side of R197, not the ENV line as the circuit
  diagram draws it. That accounts for the real unit's VCA fading with the ENV to the end of the
  release (`model.md`, "The VCA's turn-on").
- **Another IR3109, measured end to end:** KR-106 (an open-source Juno emulation, GPL; local copy
  under `private/docs/papers/opensource/kr106_ultramaster`) holds a Juno-106 voice card's
  self-oscillation at 93 DAC codes, 5.7 Hz to 52 kHz ("lfrancis V4 slider-sweep measurements").
  The card's 80017A is an SMD IR3109 with the SH-101's stage parts (68k, 560 Ω, 240 pF). Its law is
  exponential within a few cents from 6 Hz to 10 kHz and compresses above (`model-filter.md` 2).
  That rules out a chip law bent near the rest as the cause of the ENV-sweep gap, settles the
  decode's tail-mirror bias, and replaces Open Music Labs' rising linearity error. The project's
  Juno-6 measurements (`KR106VcfFreqJ6.h`: 19 points, slider 0–8.5) agree at the bottom. The data
  is used for comparison only; none of KR-106's code is used.
