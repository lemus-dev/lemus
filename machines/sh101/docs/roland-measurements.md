# Roland's SH-101 plug-in as a reference: what it does, measured

Roland's SH-101 plug-in (Roland Cloud / PLUG-OUT, v1.2.0; `docs/roland-plugin.md` lists its
parameters and how it differs from the hardware) is the main A/B reference, as Roland's TB-303 was
for the TB-303. Its panel is not the hardware's: two ADSRs, bipolar depths, 8-bit values,
MASTER TUNE in hertz, extra LFO waves and footages. So before its renders mean anything, we
measure what its controls do and write it down here. Recordings of real units (last section)
check both of us.

Tools (`tools/compare/`, run with `../../.venv/bin/python` from the machine folder):
- `make_suite.py` writes the specs: `specs/suite.json` (the A/B suite), `calibration.json` (the
  service notes' adjustments), `stability.json` (long runs, ours only) and `probes.json` (Roland
  only: raw values of its own switches and ranges).
- `make_base.py` builds `private/reference/roland-vst/base.otk`, the Onyx Tracker song every Roland
  render starts from: the plug-in on instrument 02 with its extras neutral.
- `roland_render.py` renders specs through the plug-in in Onyx Tracker via `otk`. It writes
  `private/reference/roland-vst/renders/<name>.wav` and a `manifest.json` with each render's checks
  and the raw values used.
- `sh101render` (the plug-in's processor, headless) renders the same specs through ours.
- `measure.py DIR [DIR2]` measures every law below from a set of renders, or two sets side by
  side, using `analyse.py` (pitch, partials, envelopes, glides, modulation, pulse width, noise).
- `compare.py REF TEST` compares renders spec by spec: gain, LSD, time-averaged LSD, centroid and
  pitch, with a mean per group.
- `fit_roland.py NAME=V1,V2 ...` grid-searches calibration fields against these measurements and
  rejects combinations that are unstable over `stability.json`.
- `real.py` measures the real-unit clips listed under "Real recordings".

## Rendering

How our parameters become Roland's (`roland_render.roland_params`):

- Sliders map linearly onto the 8-bit values: travel t → raw 255·t. That keeps "the same
  position on the panel" on both sides, so whatever law the plug-in has shows up in the
  measurements instead of being hidden in the mapping.
- The bipolar depths (VCF ENV, VCF KEY FOLLOW, MODULATOR VCO and VCF DEPTH) get the upper half:
  raw 128 + 127·t, so slider 0 is Roland's centre (no effect) and slider 10 its maximum.
- One envelope on the hardware: both of Roland's ADSRs get the same A, D, S and R.
- Choices: LFO waves TRI/SQR/RANDOM/NOISE are raw 1/3/4/5 (SIN 0 and SAW 2 have no hardware
  counterpart); PWM MAN/LFO/ENV are raw 0/1/3; ENV TRIG GATE/LFO/GATE+TRIG are raw 0/1/2;
  VCA MODE GATE/ENV raw 0/1; PORTAMENTO ON/AUTO/OFF raw 0/1/2. RANGE and SUB OSC TYPE are
  measured below (the plug-in's own tables are ambiguous).
- TRANSPOSE L/M/H shifts the MIDI notes by an octave: Roland's OCTAVE only moves its on-screen
  keyboard.
- TUNE: Roland's MASTER TUNE (430–450 Hz in 1 Hz steps) for our ±50 cents.
- VOLUME: Roland's PATCH LEVEL, linearly; the comparison matches gain anyway.
- Bender: MIDI pitch bend with BEND RANGE set from our bender law. Roland's bend moves only the
  pitch and its CC1 does nothing, so the bender's VCF and LFO MOD paths are ours only.
- Fixed in the base song: effects off (CRUSHER, REVERB, DELAY 0), VCA TONE centre, TEMPO SYNC,
  ARPEGGIO and KEY HOLD off, SCATTER 0, OCTAVE 0. CONDITION and CIRCUIT MOD are not host
  parameters; every instance starts with the plug-in's first patch's values.

## What needs measuring

| # | Question | Specs | How |
|---|---|---|---|
| 1 | Which raw RANGE plays a MIDI note at which footage: is our 8' (A4 = 442 Hz on a trimmed unit) Roland's raw 1 ("32'")? | `probe-range-raw0`–`5`, `osc-saw-*-A4` | f0 of A4 per raw value |
| 2 | Which raw SUB OSC TYPE is which mode | `probe-sub-raw0`–`2` | f0 (one or two octaves down) and the missing 4th harmonics of the 25 % pulse |
| 3 | LFO waves and the PWM source table | `probe-lfo-raw*`, `probe-pwm-raw*` | shape of the pitch / pulse-width modulation |
| 4 | Pitch: MASTER TUNE, key tracking across the keyboard, TRANSPOSE | `probe-tune-*`, `osc-saw-8-*`, `osc-tune-*` | f0, cents from equal temperament at A = 440 / 442 |
| 5 | VCF FREQ law: cutoff against slider position (hardware: about 10 Hz–20 kHz, ~4.4–5.1 octaves of FREQ alone by the schematic) | `selfosc-f*`, `probe-freq-raw*` | frequency of the self-oscillation, KYBD 0 |
| 6 | Self-oscillation: does RES 10 oscillate with the sources off; onset; amplitude across the range | `selfosc-*`, `selfosc-onset-*` | level and frequency |
| 7 | RES law: resonant peak height against RES at several FREQ; passband loss | `vcf-saw-*`, `vcf-bass-*` | harmonic amplitudes ×n (a saw's 1/n undone) |
| 8 | KYBD law: tracking in octaves per octave at KYBD 0 / 5 / 10 (hardware: 0–100 %, trimmed to 1 oct/oct at 10) | `selfosc-kybd*` | self-oscillation frequency across F3–C6 |
| 9 | ENV depth: octaves of cutoff at ENV 2.5–10 (hardware: ENV 10 ≈ 15–19 octaves at the CV summer) | `selfosc-env*` | self-oscillation frequency while held at S 10, FREQ 0 |
| 10 | ENV time laws: attack (hardware 1.5 ms–4 s, ~3 s measured), decay and release (2 ms–10 s), sustain level | `env-*` | RMS envelope: 10–90 % rise, decay and release time constants, sustain ratio |
| 11 | VCA ENV vs GATE (GATE's click and release) | `vca-*-phrase` | envelope per note |
| 12 | ENV trigger modes on legato notes and the LFO trigger | `trig-*` | re-attacks |
| 13 | Portamento law (hardware 0–5 s; τ ≤ 0.66 s by the schematic) and AUTO | `porta-*` | pitch track: time constant |
| 14 | LFO rate law (hardware 0.1–30 Hz) and shapes; VCO and VCF MOD depth laws | `lfo-*` | instantaneous frequency of the note / of the self-oscillation |
| 15 | PW law (hardware 50 % at 0 → about 0–5 % at 10), PWM depth | `osc-pulse-*`, `pwm-*` | duty cycle from the harmonic amplitudes |
| 16 | Mixer levels and laws, noise spectrum | `mix-*`, `osc-noise-*`, `osc-sub-*` | level relative to the saw at 10; Welch spectrum |
| 17 | Output: PATCH LEVEL law and absolute level; velocity ignored | `volume-*`, `vel-*` | RMS in dBFS |
| 18 | Bend range | `probe-bend-*`, `bend-vco` | f0 at full bend |
| 19 | Latency of a note in Onyx | every render | onset after the note's line (manifest) |

## Results

Measured 2026-10-08 from 263 renders (`private/reference/roland-vst/renders`, all at 44.1 kHz,
none clipped or silent; the numbers are in `private/reference/roland-vst/measurements.json`, from
`measure.py`). "Slider t" is our travel 0–1 (= raw 255·t, or 128 + 127·t for the bipolar depths).

**Timing.** Most notes start 0.3–0.5 ms after their line; in 35 of 263 renders the first note
started one Onyx block (about 10 ms) late. `compare.py` aligns onsets and `measure.py` uses rise
times and time constants, so neither depends on it.

### Switches and pitch

- **RANGE.** Raw 0–3 play A4 (MIDI 69) at 220 / 440 / 880 / 1760 Hz, so they are the hardware's
  16′ / 8′ / 4′ / 2′ although the plug-in labels them 64′–8′; raw 4 and 5 are two more octaves up.
  The mapping (ours = raw) stands.
- **SUB OSC TYPE.** Raw 0 = 2 OCT square, raw 1 = 1 OCT square, raw 2 = 2 OCT pulse, low for 25 %
  of the cycle (every 4th harmonic missing). As mapped.
- **MODULATOR WAVEFORM.** Raw 0 sine, 1 triangle, 2 saw, 3 square, 4 RANDOM (steps at the LFO
  rate), 5 NOISE. As mapped.
- **PWM source.** Raw 0 MAN, 1 and 2 LFO, 3 F.ENV, 4 and 5 A.ENV. As mapped.
- **MASTER TUNE.** A4 = 430 + raw Hz exactly (429.9 / 439.9 / 449.9 Hz). Tracking from F2 to C7 is
  within ±0.6 cent of equal temperament. Its 440 Hz against the service notes' 442 Hz makes our
  service-notes calibration 7.9 cents sharp of it; the fit takes that up.
- **BEND RANGE.** Raw + 1 semitones, exactly (±1.99, ±11.9, ±23.8).
- **Velocity.** Ignored (30 and 127 are the same level), as on the hardware.

### Oscillator and mixer

- **Saw.** An ideal 1/n spectrum (h2 −6.0, h4 −12.1, h8 −18.2 dB), like a real unit's (−6.0,
  −11.9, −18.0 dB; "Real recordings" below).
- **PW law.** Linear: duty 49.9 / 38.4 / 26.8 / 15.3 / 8.3 / 3.9 % at slider 0 / 2.5 / 5 / 7.5 /
  9 / 10, about 50 − 46·t %. The hardware's spec says 50 % → 0 %; ~5 % is the usual practical
  minimum. PWM swings from 50 % down to the slider's width (LFO at slider 6: 23–49 %; ENV at 8:
  17–50 %), as the hardware's PW slider sets the depth.
- **Mixer slider law** (saw, level re the saw at 10): −27.2 / −19.9 / −16.5 / −14.0 / −10.1 /
  −5.4 / −1.5 dB at 1 / 2.5 / 3.75 / 5 / 6.25 / 7.5 / 9. That is an audio taper (−14 dB at
  half), the same for every source.
- **Levels at 10** (RMS, re the saw): square +1.9 dB, sub square −1.6 dB (1 OCT and 2 OCT alike),
  25 % sub pulse −2.4 dB, noise +2.9 dB. The schematic's mixer resistors with the CEM3340's 0–10 V
  saw and the 0–14 V pulse and sub give square +4.5 dB and sub +2.8 dB, so Roland's pulse and sub
  are 2.6 and 4.4 dB weaker than the schematic predicts. Three real units (below, "Real
  recordings") put the square at +1.2 to +1.5 dB, the subs at −0.55 to −1.5 and the noise at
  −2.5 to +0.7: the schematic's estimate is too high, Roland's subs are about 1 dB low against two
  of them, and its noise is louder than any.
- **Noise.** Not white: −7.3 dB/octave from 200 Hz to 8 kHz, centroid 1.09 kHz, −3 dB at 0.55 kHz
  re 200–500 Hz. A real unit measures −7.9 dB/octave and 0.95 kHz, so Roland's colour is close.

### VCF

- **FREQ law.** Exponential: f = 239 Hz · 2^((raw − 128)/20.8) within ±6 cents from raw 16 to
  224 (5.7 Hz to 5.8 kHz), compressing at the top (−13 cents at 240, −75 at 255) and at the bottom.
  Slider 0 / 1 / 2 / … / 10 gives 3.4, 8.0, 18.4, 42.3, 101, 239, 550, 1264, 2997, 7081 and 15636 Hz.
  That is 12.2 octaves end to end, about 1.2 octaves per slider division. The hardware spec says
  10 Hz–20 kHz (11 octaves). The research digest's estimate of a 4.4–5.1 octave FREQ span
  (research.md §4, §11) disagrees with both and should be checked against the schematic.
- **Self-oscillation.** Sources off, RES 10: a clean sine (h2 −42 dB, h3 −50 dB) at −9.3 dBFS
  (volume 5), level flat within 1 dB from 3 Hz to 15.6 kHz. Onset: RES 8.0 rings (−37 dBFS),
  RES 8.2 sustains (−20 dBFS), then −16.6 / −13.4 / −12.2 / −10.5 dBFS at 8.4 / 8.8 / 9 / 9.5. The
  research predicted onset at ≈ 81 %.
- **Resonance** (saw on 110 Hz, harmonic amplitudes × n: the peak's height over the fundamental):

  | FREQ | cutoff | RES 0 | RES 5 | RES 7.5 | RES 10 |
  |---|---|---|---|---|---|
  | 6 | 550 Hz | – | +9.1 dB at 446 Hz | +18.5 at 537 | +27.2 at 544 (oscillating) |
  | 7 | 1.26 kHz | – | +10.0 at 1074 | +24.0 at 1208 | +33.4 at 1220 |
  | 8 | 3.0 kHz | – | +9.9 at 2534 | +25.0 at 2874 | +40.9 at 2976 |

  Passband loss (fundamental, FREQ 7–9): −7.2 dB at RES 5 and −8.4 dB at RES 7.5. That is the
  schematic's (1+α)/(1+4.95α), −6.0 / −8.8 dB, with no Q make-up.
- **KYBD.** Linear in the slider: 0.000 / 0.503 / 0.995 octaves per octave at 0 / 5 / 10. The pivot
  is near MIDI 12, so at KYBD 10 the cutoff at F3 is 3.41 octaves above KYBD 0. On the hardware
  KYBD adds the KCV, which is 0 V at C2 for TRANSPOSE M: 1.42 octaves at F3. Roland's FREQ 0
  (3.4 Hz) is correspondingly lower. A real unit (Friendly Noise, `online-references.md`) rests
  at 26–32 Hz with FREQ 0, KYBD 10 and KCV 3.0 V, which puts its FREQ 0 at KYBD 0 near 3.4 Hz.
  That is Roland's FREQ 0 too.
- **ENV depth** (S 10, octaves above FREQ 2.5): 0.29 / 0.67 / 1.18 / 1.86 / 2.71 / 3.92 / 5.52 /
  7.65 at 1.25 / 2.5 / … / 10. That is roughly quadratic. A real unit at ENV 10 sweeps 7.0–7.3
  octaves (`online-references.md`), so Roland reaches about 0.4 octave further. Negative ENV (raw 0) takes the cutoff 7.6
  octaves down (3.0 kHz to 15 Hz).
- **MOD depth** (triangle, cents peak to peak of the oscillation): 2227 / 4397 / 6374 / 8296 at
  2.5 / 5 / 7.5 / 10, near linear, ±3.5 octaves at 10.

### Envelope (on the VCA, filter open)

| slider | attack t10–90 | attack t90 | decay τ | release τ | sustain |
|---|---|---|---|---|---|
| 0 | 2.5 ms | 4.5 ms | 0.7 ms | 1.1 ms | 0.000 |
| 1.25 | 34 ms | 40 ms | 53 ms | 55 ms | |
| 2.5 | 115 ms | 130 ms | 119 ms | 122 ms | 0.250 |
| 3.75 | 0.25 s | 0.27 s | 0.35 s | 0.36 s | |
| 5 | 0.50 s | 0.55 s | 0.50 s | 0.50 s | 0.493 |
| 6.25 | 0.82 s | 0.90 s | 0.73 s | 0.73 s | |
| 7.5 | 1.35 s | 1.49 s | 1.18 s | 1.18 s | 0.731 |
| 8.75 | 2.08 s | 2.28 s | 1.96 s | 1.95 s | |
| 10 | 2.86 s | 3.13 s | 4.94 s | 4.95 s | |

- Decay and release share one law. Its top, τ 4.94 s, is the schematic's 1 MΩ × 4.7 µF (4.7 s),
  and its middle (10.7 % of 1 MΩ at half) is an audio taper.
- Attack at 10 reaches 99 % in 3.6 s. The hardware spec says 4 s; Reid measured about 3 s on one
  unit; the real clip below reaches 99 % in about 2.6 s.
- Sustain is linear.
- **Trigger modes.** GATE+TRIG re-attacks on each legato note (3 attacks for 3 overlapping notes),
  GATE does not (1), LFO retriggers at the LFO rate.
- **VCA GATE** rises in 2–3 ms and falls in 3 ms (a de-clicked gate). ENV with A 0 rises in 1–4 ms.

### Portamento, modulator, bender

- **Portamento** (ON, A3 → A5) is exponential in cents, an RC on the CV, with τ 23 / 60 / 118 /
  206 / 340 / 560 / 925 / 1554 ms at 1.25 / 2.5 / … / 10. From 3.75 up that is
  τ ≈ 1.55 s · e^(4.0(t − 1)).
  - At 10 it is twice a real unit's: the schematic gives τ ≤ 0.66 s (2 MΩ × 0.33 µF), and one
    real unit at maximum glides with τ 0.78 s both ways.
  - AUTO glides between overlapping notes (τ 0.30 s at 6) and jumps between detached ones.
- **LFO rate**: 0.088 / 0.18 / 0.76 / 2.90 / 5.89 / 9.61 / 15.2 / 23.1 / 39.1 Hz at
  0 / 1 / 2.5 / 4 / 5 / 6 / 7.5 / 9 / 10. The hardware spec says 0.1–30 Hz.
- **VCO MOD depth** (triangle, cents peak to peak): 62 / 151 / 370 / 778 / 2326 / 5287 at 1 / 2.5 /
  3.75 / 5 / 7.5 / 10. The law is steep, about t^2.7, so slider 10 swings ±26 semitones. Raw
  sixteenths in `probes.json` give the same curve.
- **Bender.** Pitch only, BEND RANGE semitones. The hardware's VCF bend and LFO MOD lever have no
  Roland counterpart.

### Output

- **PATCH LEVEL law** (re 10): −30.6 / −21.2 / −16.3 / −12.3 / −9.0 / −5.8 / −2.2 dB at 1 / 2.5 /
  3.75 / 5 / 6.25 / 7.5 / 9, an audio taper like the hardware's 100 kΩ (A) VOLUME.
- **Level.** A full saw at volume 5 is −14.8 dBFS RMS, −9.9 dBFS peak. At volume 10 the saw peaks
  at about +2 dBFS and clips, so the suite keeps VOLUME at 5, or the saw at 5 for the volume law.

### What this means for the fit

- The mapping is right for every switch.
- The continuous laws Roland chose follow the hardware's tapers in shape: audio tapers on the
  mixer, VOLUME, ENV times and portamento, and linear sustain, KYBD and PW.
- Some ranges differ from the schematic and from real units:
  - portamento, about twice as slow at the top;
  - attack, about 1.2–1.4× slower at the top;
  - ENV depth, about 1 octave more;
  - the pulse and sub levels in the mixer;
  - the FREQ end points.
- The A/B comparison should therefore weight the parts where Roland agrees with the hardware:
  waveforms, filter response and resonance, self-oscillation, envelope shapes, decay and release
  times, tracking.
- Where Roland and the hardware disagree, `docs/model.md` should say which one each calibration
  follows, as the TB-303 did.

## Real recordings

`private/reference/shootout` holds 429 clips; 310 are of real SH-101s (281 confirmed from the
video, 29 probably). 274 confirmed clips have no speech over the audio. Nearly all are lossy
(YouTube opus/aac). Only 15 are lossless, and those have no patch notes. Slider positions are
read off video, so these clips check behaviour and the shape of laws, not absolute settings.
`real.py` measures the ones below (numbers in `private/reference/shootout/real-measurements.json`).

Usable, with what they show (clip names are `wav/roland_sh-101_real__<name>.wav`):

| Behaviour | Clip | Measured | Roland |
|---|---|---|---|
| Raw saw spectrum | `italo-brutalo_youtube_Cr4Le00_MuA__saw_single_note_d2_sh101` (saw 10, filter open, panel legible) | ideal 1/n: h2 −6.0, h4 −11.9, h8 −18.0 dB | −6.0, −12.1, −18.2 |
| Noise colour | `failed-muso_youtube_6NqaegXGbtE__noise_full_level_open_filter` (and `_half_level_`) | −7.9 dB/oct, centroid 0.95 kHz | −7.3 dB/oct, 1.09 kHz |
| Self-osc tracking at KYBD 10 | `tonelab_youtube_zR3U7za05dI__self_osc_sources_off_kybd_max_notes_played` (frame-verified RES 10, KYBD 10) | intervals over 2.2 octaves within 14 cents of whole semitones: about 1.00 oct/oct | 0.995 |
| ENV as cutoff (ENV 10, FREQ 0, KYBD 10, RANGE 16', TRANSPOSE L, top key: KCV 3.0 V) | `friendly-noise_youtube_eStzYAZoOAI__selfosc_vcf_env_sweep_res_max_adsr_max` | rest 26–32 Hz → plateau 4.8 kHz: 7.0–7.3 octaves; release τ 4.1–4.4 s. Corrected: the first reading (51 Hz → 4.86 kHz, 6.6 octaves) was cut off by `real.py`'s 30 Hz pitch floor and by the video's edit, and assumed C4 at TRANSPOSE M (`online-references.md`) | 7.65 octaves at ENV 10 |
| Attack at 10 | `friendly-noise_youtube_eStzYAZoOAI__saw_vcf_env_sweep_no_res_adsr_max_c4` (A/D/S/R 10) | 99 % in about 2.6 s (the VCF sweep also shapes the level, so approximate) | 3.6 s |
| Portamento at maximum | `once-upon-a-synth_youtube_PFXO4ZRsVC4__portamento_on_max_time_glide_up_then_down` | exponential in cents, τ 0.78 s up and down | τ 1.55 s |
| Portamento at 3, then 10 | `automaticgainsay_youtube_rA93zzTjk1A__portamento_on_glides_time_40_to_55pct` | the knob, from 720p frames (2026-11-13), at about 3 (10 o'clock) until 617 s, then 10 from 621 s (the video watched through, 2026-11-15): four short glides τ 0.053–0.056 s, the two long ones 0.63 and 0.62 s (`units.py`; the 0.31 s before was a fit across a tracker dropout, the 40–55 % a misread 360p frame) | 0.12–0.34 s at 3.75–6.25 |
| Bender VCO at ~90 % | `automaticgainsay_youtube_rA93zzTjk1A__bender_vco_depth90_bends_a2` | +14.7 semitones up (the down bend isn't held long enough to measure) | (pitch bend only) |
| PWM by LFO | `gstormelectro_youtube_SsHcY4YQOfw__pwm_lfo_held_a3_rate_sweep` | duty 25.7–48.2 % | 23–49 % at PW 6 |
| Arpeggio clock | `automaticgainsay_youtube_5k-poyZyg2w__arp_up` | 0.22 s steps (LFO clock) | (no arpeggiator) |
| Sub modes | `automaticgainsay_youtube_8jCX_EUNocI__sub_2oct_pulse25_only`, `tonelab_…__sub_osc_only_1oct_vs_2oct_square_switching` | 25 % pulse: every 4th harmonic missing; 1 OCT / 2 OCT squares | the same |
| Levels at 10, re the saw | `perfect-circuit_youtube_biuWnvJ0f7I__{saw,square,sub_1oct_square_only,sub_2oct_only,noise_only}_f2` (sliders read at 1080p), `gstormelectro_youtube_SsHcY4YQOfw__*_only` (stills; sliders assumed at 10), `automaticgainsay_youtube_8jCX_EUNocI__*` (720p) | square +1.5 / +1.2 / –; sub 1 OCT −0.6 / −0.7 / −0.55; sub 2 OCT −0.6 / −0.8 / −1.5; 25 % sub −1.7 / – / −2.5; noise +0.6 / +0.7 / −2.5 dB | +1.9, −1.6, −1.6, −2.4, +2.9 |
| Resonance against RES | `automaticgainsay_youtube_VQXq7OmZCtY__vcf_sweep_res{0_saw_g2,25_saw_d3_a,50_saw_d3,_edge_of_selfosc_saw_d3}` (RES read at 2.3 / 4.7 / 7.1 from 720p frames), `loopop_youtube_3JBFoXgtHvE__vcf_resonance_raise_saw_c2` | peak +2.2 / +8.9 / +23.2 dB, fundamental's loss 4.2 / 6.9 / 8.4 dB; loopop: the loss against the peak's height matches ours within 0.04 dB | at 5: +9.1 to +10.0 / 7.2; at 7.5: +18.5 to +25 / 8.4 |
| ENV weight, a second unit | `loopop_youtube_3JBFoXgtHvE__vcf_env_decay_res_sweep_saw_c2_set{A,B}` (frames at 429–437 s and 475 s: ENV 10, KYBD 0, S 0, VCA GATE) | weight 0.72–0.79 of the schematic's, as the Friendly Noise unit | 0.80 |
| Trigger modes, VCA GATE vs ENV | `automaticgainsay_youtube_GvF-Bddmj9k__*`, `tonelab_…__env_gate_mode_overlapping_notes_no_retrigger` | GATE+TRIG retriggers legato notes, GATE doesn't; VCA GATE attacks 10–40 ms vs ENV 100–480 ms on that patch | the same rules |

For listening, `private/sh101/reference/scenes/shootout-real` holds twelve of these clips as Lemus AB
scenes against Lemus at the settings read off each video (its README lists them).

Also usable, qualitatively:
- the resonance series `automaticgainsay_youtube_VQXq7OmZCtY__vcf_sweep_res{0,25,50,_edge_of_selfosc}`;
- the passband loss with resonance, `loopop_youtube_3JBFoXgtHvE__vcf_resonance_raise_saw_c2`;
- the LFO waveform clips on the VCF and VCO;
- `failed-muso…vcf_res_full_env_half_cutoff_half`, a ping near 700 Hz at FREQ 5, RES 10, ENV 5.

Not usable for numbers:
- clips with speech over the audio;
- the 6 `_multi` clips, where the take order is unknown;
- the self-oscillation melody at KYBD ~85 %, whose span gives an implausible 1.7 oct/oct, so the
  notes are probably misread;
- the bender at ~50 %, whose bends are melodic and have no steady centre.

The index's own gaps list stands. There is no calibrated series of FREQ, envelope, portamento or
LFO positions, and no lossless dry notes with documented settings. Absolute laws therefore come
from the schematic and Roland, and the real units bound them.
