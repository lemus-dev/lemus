# TB-303 emulation research digest

Compiled 2026-10-06 for the circuit-accurate TB-303 plug-in (JUCE AU/VST3, C++).

This digest covers the published analyses, open-source models and forum measurements of
the Roland TB-303 that bear on a high-accuracy real-time model. It is organised by circuit
block. Each block gives:

- what the circuit does;
- the competing models, with their equations;
- every measured or derived number found, with its source;
- where sources disagree;
- a concrete recommendation for our implementation.

Section 10 tabulates every numeric constant. Section 12 lists what is still unknown and
should be measured or simulated from the schematic in `private/docs/schematic/`.

Conventions:

- "My calc" marks numbers I derived from the cited equations. The scripts are in the
  session scratchpad; the formulas are given inline so they can be re-derived.
- Component designators follow the Roland schematic as quoted by the source. Check them
  against `private/docs/schematic/`, which is being assembled separately.

---

## Contents

0. [Sources and local copies](#0-sources-and-local-copies)
1. [Recommended architecture (summary)](#1-recommended-architecture-summary)
2. [VCO: saw and square](#2-vco-saw-and-square)
3. [VCF: diode ladder, resonance, feedback/coupling high-passes](#3-vcf-diode-ladder-resonance-feedbackcoupling-high-passes)
4. [MEG: main (filter) envelope and the cutoff CV](#4-meg-main-filter-envelope-and-the-cutoff-cv)
5. [Accent and the accent sweep circuit](#5-accent-and-the-accent-sweep-circuit)
6. [VCA and VEG (volume envelope)](#6-vca-and-veg-volume-envelope)
7. [Slide](#7-slide)
8. [Sequencer gate and timing](#8-sequencer-gate-and-timing)
9. [Output high-passes and level](#9-output-high-passes-and-level)
10. [Table of numeric constants](#10-table-of-numeric-constants)
11. [Disagreements between sources (summary)](#11-disagreements-between-sources-summary)
12. [Open questions and measurements to take](#12-open-questions-and-measurements-to-take)

---

## 0. Sources and local copies

All paths are relative to `private/docs/papers/`.

| # | Source | Local copy | Notes |
|---|---|---|---|
| S1 | T. E. Stinchcombe, *Analysis of the Moog Transistor Ladder and Derivative Filters*, 25 Oct 2008. https://www.timstinchcombe.co.uk/synth/Moog_ladder_tf.pdf | `stinchcombe/Moog_ladder_tf.pdf` (+ `.txt`) | §3: diode ladders; TB-303 case |
| S2 | Stinchcombe, *Diode Ladder Filters* web page. https://www.timstinchcombe.co.uk/index.php?pge=diode | `stinchcombe/page_diode.{html,txt}`, figures `tb303_vcf.gif`, `tb303_3_vs_4.gif`, `tb303_core_sim.gif` | 18 vs 24 dB, 10-pole TF |
| S3 | Stinchcombe, *A Comprehensive TB-303 Diode Ladder Filter Model*. https://www.timstinchcombe.co.uk/index.php?pge=diode2 | `stinchcombe/page_diode2.{html,txt}`, `tb303_vcf_res_part2.gif`, `tb303_laplace_model.gif`, `tb303_comp_level.gif`, `tb303_lap_v_comp_580.gif` | coupling-cap Laplace blocks |
| S4 | Stinchcombe, *Filter Pole Animations*. https://www.timstinchcombe.co.uk/index.php?pge=poles | `stinchcombe/page_poles.{html,txt}` | |
| S5 | V. Zavalishin, *The Art of VA Filter Design*, rev. 2.1.2, Native Instruments, 2020 (NI URL now dead; Wayback copy) | `zavalishin/VAFilterDesign_2.1.2.pdf` (+ `.txt`) | §5.10 diode ladder, §6.6, §6.9, §6.10, §6.13 |
| S6 | W. Pirkle, *AN-6: Virtual Analog (VA) Diode Ladder Filter*, v2, 19 Jul 2013 (Wayback) | `pirkle/AN-6DiodeLadderFilter.pdf` (+ `AN-6.txt`); also `pirkle/AN-4VirtualAnalogFilters.pdf` | TPT block diagram |
| S7 | R. Schmidt, **Open303**, MIT licence, © 2009. https://github.com/RobinSchmidt/Open303 (clone of sourceforge `open303`) | `open303/` (commit 313bf0d, 2024-03-29) | code read as text only |
| S8 | Open303 variant inside RS-MET (`Libraries/RobsJuceModules/rosic/...`). https://github.com/RobinSchmidt/RS-MET | `open303_rsmet/rosic_{Open303,TeeBeeFilter}.{h,cpp}` | contains Robin's fitting notes |
| S9 | KVR "Open303 – open source 303 emulation project" thread, t=262829 (2009–2023), plus related KVR threads | `forums/kvr_threads_extracts.txt` | Cloudflare blocks archiving; extracts with post IDs |
| S10 | R. Whittle, Devil Fish documentation. https://www.firstpr.com.au/rwi/dfish/ | `forums/Devil-Fish-Manual.{pdf,txt}`, `forums/dfish_303-unique.txt`, `dfish_303-slide.txt`, `dfish_303-mods.txt`, `dfish_index.txt`, `dfish_Resonance-pot-schematic.png` | measured behaviour of stock 303 |
| S11 | ladyada x0xb0x build pages (Wayback) | `forums/x0xb0x/fab_{vco,vcf,env,vca,hp-mix,finish}.txt` | x0xb0x = same analog circuit |
| S12 | subatomicglue x0xb0x mod guide | `forums/subatomicglue_x0xb0x_mod_guide.txt` | |
| S13 | A. Sloane (a1k0n), "303" repo: measurements on a self-built x0xb0x. https://github.com/a1k0n/303 | `a1k0n_303/repo/` (no licence: reference only) | cutoff-vs-current, env fits, WAV samples |
| S14 | Roland TB-303 Service Notes (Feb 1982) adjustment page and Owner's Manual specs, transcribed from scans collected in `docs/` | `forums/roland_service_and_owner_manual_numbers.txt` | |
| S15 | F. Anwander, Roland filter overview. http://www.florian-anwander.de/roland_filters/ | `forums/anwander_roland_filters.txt` | qualitative only |
| S16 | dinsync VCF303 clone manual | `forums/dinsync_vcf303_manual.txt` | qualitative |
| S17 | M. Kernmayer, *The Sound of Acid: Virtual Analog Emulation der TB-303 in Max/MSP*, Diplomarbeit, FH St. Pölten, 2020. https://phaidra.ustp.at/o:4344 | `academic/kernmayer_2020_tb303_maxmsp_thesis.pdf` | only TB-303-specific thesis; by-ear fits + listening test |
| S18 | M. Civolani, F. Fontana, "A nonlinear digital model of the EMS VCS3 voltage-controlled filter", DAFx-08 | `academic/civolani_fontana_dafx08_vcs3.pdf` | first published diode-ladder ODE |
| S19 | S. Zambon, F. Fontana, "Efficient polynomial implementation of the EMS VCS3 filter model", DAFx-11 | `academic/zambon_fontana_dafx11_vcs3_polynomial.pdf` | trapezoid + Newton, 8× OS |
| S20 | A. Huovilainen, "Non-linear digital implementation of the Moog ladder filter", DAFx-04 | `academic/huovilainen_dafx04_moog_nonlinear.pdf` | |
| S21 | J. Parker, V. Zavalishin, E. Le Bivic, "Reducing the aliasing of nonlinear waveshaping using continuous-time convolution", DAFx-16 (ADAA) | `academic/parker_zavalishin_lebivic_dafx16_adaa.pdf` | |
| S22 | V. Välimäki, J. Pekonen, J. Nam, "Perceptually informed synthesis of bandlimited classical waveforms using integrated polynomial interpolation", JASA 131(1), 2012 (polyBLEP) | `academic/valimaki_pekonen_nam_jasa2012_polyblep.pdf` | |
| S23 | F. Esqueda, V. Välimäki, S. Bilbao, "Rounding corners with BLAMP", DAFx-16 | `academic/esqueda_valimaki_bilbao_dafx16_blamp.pdf` | |
| S24 | Others downloaded (see `academic/`). | | |

Others in S24:

- Zavalishin 2008, TPT
- D'Angelo PhD 2014
- Esqueda PhD 2018
- Holters DAFx-19 and EUSIPCO-15
- Levien, ladder
- Werner DAFx-15 (WDF, multiple nonlinearities)
- Werner/McClellan DAFx-20/22
- DAFx-26 stability of time-varying VA filters
- Oyama DAFx-26 (Moog ladder nonlinearity comparison)
- Pekonen JASP 2011 (analog saw model)
- Välimäki et al. TASLP 2010 (DPW)
- Yu et al. DAFx-24 (differentiable all-pole filters, TB-303 clone data)
- Port-Hamiltonian DAFx-21
- Lazzarini/Timoney CMJ 2010

Not obtained (paywalled or not found):

- Fontana & Civolani, *IEEE TASLP* 18(4):760–772, 2010 (equations restated in S19)
- D'Angelo & Välimäki, *Generalized Moog ladder filter* I/II (TASLP 2014)
- Bilbao et al. ADAA (*IEEE SPL* 2017)
- Välimäki & Huovilainen CMJ 2006
- Werner's Stanford PhD (2016)
- Anything from Kevin Lightner or Ken Stone with numbers
- Any TD-3 teardown with measurements

**No peer-reviewed paper models the real TB-303 circuit end to end.** That includes the
VCO shaper, accent sweep, MEG/VEG and slide. The best-founded information is Stinchcombe
(VCF), Whittle (envelopes/accent/slide/sequencer), the Roland service notes, a1k0n's
x0xb0x measurements, and the KVR fits that fed Open303.

---

## 1. Recommended architecture (summary)

| Block | Recommended model | Key numbers | Rate / AA |
|---|---|---|---|
| VCO saw | Phase accumulator driven by an exponential (1 V/oct) CV with per-unit scale error. Falling ramp with a finite-time reset (rounded tip). Optional mild curvature. | 2 V = C2 = 65.41 Hz; A = 110 Hz trim; scale 0.997–1.027 measured; saw 6–11.5 V (5.5 Vpp) | 4× rate, polyBLEP (3rd-order B-spline) on the reset |
| VCO square | Derive from the saw through the real shaper chain: AC coupling (22 k·1 µF ≈ 7.2 Hz) → single-transistor threshold shaper (tanh/exponential, finite gain) → ~61 Hz output HPF. This reproduces the frequency-dependent duty cycle and soft middle edge automatically. | Open303/RS-MET: −tanh(69.98·saw ± 4.37), then HP 60.8 Hz; duty ≈ 43.5–47.5 % fits; square 5–9 V | 4× rate; 1st-order ADAA on the shaper, or 8× locally |
| VCF core | Nonlinear diode-ladder ODE with per-pair tanh. Real cap ratio 18 n/33 n. | k_osc = 17 at ω_z/√2; ω_z = I_f/(2V_T·33 nF) | Trapezoidal (ZDF) + Newton, 2–4 iterations, **4×** (8× HQ), double precision |
| VCF periphery | Stinchcombe's coupling-cap blocks, ideally re-derived from the schematic. In loop: 92 Hz output-amp HPF, 1.18 Hz pot coupling, injection network. Outside: input network, 3.18 Hz output HPF. Input divider 1/100. | max loop gain 18.7 (≈10 % over threshold); self-osc only above ≈2 kHz; ≈8 Hz LF resonance | inside the same implicit solve |
| VCF output amp | Differential pair Q21 as tanh (gain ≈ 106 small-signal), in the resonance loop. **Hypothesis to verify in SPICE.** | | same solve |
| Cutoff CV | Expo converter fed by a CV sum: cutoff pot (50 kA), env-mod pot (50 kA) × MEG, accent-sweep node (via 100 k R72), TM3 trim. Calibrate to a1k0n/Open303 measurements. | nominal 314–2394 Hz (resonant freq.); 64.5 Hz/µA of ladder current (my calc) | control rate ≥ 1 kHz, interpolated per sample |
| MEG | RC-type: near-instant attack, exponential decay. τ from the Decay pot (normal notes); shorted to the minimum time on accent. | 200 ms–2 s (Whittle; definition unclear); antto τ = 0.28–3.85 s | control rate |
| Accent sweep | Simulate the actual RC network: MEG → accent switch → Accent pot → diode → 47 k → 2nd gang of Resonance pot (50 k or 100 k) → 1 µF C13; wiper → 100 k → filter CV. Diode makes charge/discharge asymmetric. | τ_charge ≈ 50–60 ms, τ_discharge ≈ 100 ms (my calc, resonance CW) | control rate (or audio rate) |
| VCA/VEG | Exponential decay VEG, fixed ≈ 3–4 s. Attack ≈ 3 ms after a 1–5 ms delay. Gate-off: 8 ms hold then 8 ms linear fall. Accent adds MEG·accent through 47 k/0.033 µF (τ ≈ 1.55 ms). Model the VCA's nonlinear input if SPICE shows it. | Open303 VEG τ = 1230 ms | audio rate |
| Slide | One-pole RC on the **pitch CV (volts)**, not on Hz. Active during the note after the slide flag. | τ = 100 k × 0.22 µF = **22 ms** (≈ 60 ms audible) | control/audio rate |
| Sequencer | 24 ppqn; 6 clocks per 16th. Gate on at clock 0, off halfway through clock 3 (3.5/6 = 58.3 %). Slide keeps the gate high. Plus a 44 µs CV-latch strobe at each note. | 8 ms/clock at max tempo (312 BPM) | sample-accurate |
| Output | Chain of 1st-order HPFs after the VCF/VCA (equivalent ≈ 47 Hz single pole; 32 Hz is −5 dB). Output impedance 10 kΩ. | Open303: 24 Hz + 14 Hz AP + 7.5 Hz notch; RS-MET: 46.9 Hz | base rate after decimation |
| Decimation | Linear-phase FIR or a high-order elliptic half-band cascade to the host rate. | Open303: 12th-order elliptic quarter-band IIR | |

Reasoning in brief:

- The filter is where the character lives. It has the strongest published basis: an exact
  linear transfer function plus the physically derived tanh ODE.
- Everything else (envelopes, accent sweep, slide) is linear RC behaviour with diodes and
  switches. It is cheap and should be modelled from component values rather than fitted.
- The square-wave shaper is the one place where measured fits (Open303, antto) exist and a
  circuit model is still missing. Build the circuit model and check it against the fits.

---

## 2. VCO: saw and square

### 2.1 Facts

**Pitch control:**

- 1 V/oct.
- Service Notes (S14): TM5 ("WIDTH") sets the scale so CL:CH periods are 2:1. TM4 sets
  the A key to **110 Hz**. Two-octave check 4:1 ± 0.5 %. So **2.0 V = C2 = 65.41 Hz** and
  3.0 V = 130.8 Hz (S11 agrees).
- Sequencer CV accuracy: 1.000 V ± 3 mV per octave (S14).
- Owner's manual (S14): CV out +1…+5 V; "sound range 3 octaves (4 octaves in a TRACK)".
- 6-bit R-2R DAC gives 64 notes ≈ 5.33 octaves; maximum output ≈ 5.333 V ≈ 659 Hz
  (aciddose, S9). The 5.333 V supply rail is quoted by S11 and S3.

**Unit-to-unit scale error:** measured tuning scale factor ≈ **0.997** on ladyada's unit
and **1.027** on rv0's unit. That is note → volts × factor → Hz (antto, S9). Mike Janney
(AudioRealism) published a note-number-vs-frequency plot of his unit (S9, Dec 2009).

**Levels at the waveform switch** (x0xb0x, S11):

- Saw: Vmin = 6 V, Vmax = 11.5 V (5.5 Vpp), DC ≈ 9 V.
- Square: 5–9 V (4 Vpp), DC ≈ 7 V.
- "Note that the tip of the saw isn't 'sharp'": finite reset/flyback.
- "Note that the square wave droops."
- The output is unbuffered, so loading changes the square's shape (S12).
- The mod guide gives 5.33…≈10.5 V (S12).

**Polarity:** Robin Schmidt's measurement note calls the saw a "sawdown" (falling ramp)
(S8). Open303 builds a rising table and negates it (`tmp = -oscillator.getSample()`) (S7).

**Square generation:** a **single transistor** threshold shaper fed from the saw (KVR
"Strange comment in wikipedia…" thread, S9):

- Gamma-UT: "a kind of threshold detector … the only way to adjust this is to replace
  transistor Q8".
- mystran: "because of the limited gain from 'the shaper', the transition in the middle of
  the sawtooth is slower than the transition when the saw resets".
- antto: "not exactly 50% : 50%"; "75% of 303 square sound magic is the fact that it's not
  a square at all".
- aciddose (2019, his own redraw designators): an RC of 22 k·1 µF = **7.23 Hz** into the
  shaper. "R5 & R4 will adjust pulse width, C2 tone/'curve' for low notes". The width of
  the soft middle edge scales with the period ("at 10 Hz it'll be 100× as wide as
  1000 Hz").

**Duty cycle depends on frequency.**

- Mike Janney plotted measured square duty cycle vs note number for a real 303 (S9,
  Dec 2009). The thread only describes the graph; it gives no numbers.
- antto's "variable offset" VO for the shaper threshold vs oscillator frequency:
  - VO = 0.385 at 17.3 Hz;
  - VO = 0.0077 at 109 Hz;
  - VO = −0.033 at 146.6 Hz.
- antto's PW fit (Fc = osc frequency in Hz):
  `(0.90412745 − 0.0082430065·Fc)/(1 + 0.057837048·Fc + 3.1131084e−5·Fc²)`.

**Pulse-width fits to recordings** (S8 comments):

| Target | Pulse width | Input allpass | Input HPF |
|---|---|---|---|
| Real-303 recording "autodafe TB303_square_C100_R0_F2_87Hz.wav" | 47.5 % | 75 Hz | 40 Hz |
| "SquareNoResoC2.wav by rv0" | 45 % | 60 Hz | 95 Hz |
| AudioRealism ABL | 43.5 % | 70 Hz | 200 Hz |

### 2.2 Models found

**Open303 (S7, S8): wavetable.**

```
saw303[n]    = ramp (0→1 over first half, −1→0 over second half; i.e. a centred ramp)
square303[n] = −tanh( 10^(36.9/20)·saw[n] + 4.37 )    // gain 69.98, offset 4.37
               circularly shifted by 180° (core) / 189° (VST default), ×0.5 in the mix
osc out      = −[ (1−blend)·saw + blend·0.5·square ]  // VST default blend = 0.85
```

- Threshold at saw = −4.37/69.98 = −0.0624, so **duty ≈ 46.9 %**. The tanh transition
  (|arg| < 1) lasts **1.43 % of the period** at every pitch (my calc). That is not
  frequency dependent, unlike the real circuit.
- RS-MET note (S8):

  > measurement for square: sawdown(symmetric) -> tanh(a*x+b) with a = db2amp(36.9), b = -4.37 -> HP@60.8 -> filter/dist -> HP@46.9

  So there is an AC coupling of **60.8 Hz after the shaper**. Open303 itself uses a
  44.486 Hz pre-filter HPF instead.
- Anti-aliasing: 2048-point tables, 12 octave-spaced mip-levels (FFT brick-wall).
  Table index = ⌊log₂(increment)⌋ + 2, linear interpolation, running at 4× (176.4 kHz).

**antto (KVR, Oct 2009, S9): circuit-inspired chain** that "matches ~93 %" on the saw.

```
s0 = saw (−1..+1)
s0 = HP1(s0, 14.7 Hz)              // "asymmetry determination" (HPF before shaper)
s0 = tanh(−60·s0)                  // shaper
s0 = (s0 + 2·saw)·0.5              // partial mix with unshaped saw
s0 = HP2(s0, 27 Hz)
s0 = max(s0, −c)                   // negative-only clipper
s0 = HP3(s0, 14.7 Hz)              // DC killer (17 Hz for the saw)
s0 *= 1.6
c  = 0.090355991 + 0.0082714712·Fc + 6.5665602e−5·Fc²
```

Later variants in S9:

- `tanh((x + soff)·−41)`
- square "DC-Killer at 61 Hz", saw "DC-Killer at 17 Hz"
- two HPFs at ≈ 9.25 Hz for symmetry, fixed offset −0.055

antto noted that "at very low frequencies there is a 'bump' on the negative peak".

**Kernmayer (S17): by ear.**

- Saw: `2·tanh(1.5·φ)/tanh(1.5) − 1` (curved ramp), with 2-point polyBLEP at 8×
  oversampling.
- Square: rational shaper `x(|x|+a)/(x² + (a−1)|x| + 1)` with a = 100, then a 2nd-order
  HPF at 38 Hz, Q = 0.3.
- Listening test (51 producers): square similarity 7.1/10; saw patterns 8.3/10.

**Analog saw imperfections (S24, Pekonen et al. JASP 2011).** Model the finite reset of an
analog saw by phase distortion or a 1st-order post-EQ. Not 303-specific.

### 2.3 Disagreements

| Quantity | Values found |
|---|---|
| HPF before the shaper | 7.23 Hz (aciddose, from 22 k/1 µF) vs 14.7 Hz, later 9.25 Hz (antto, fitted) |
| HPF after the shaper | 60.8 Hz (Schmidt), 61 Hz (antto), 27 Hz + 14.7 Hz (antto, earlier) |
| Duty cycle | ≈ 43.5–47.5 % (fits); frequency dependent in measurements (Janney, antto); fixed 46.9 % in Open303 |
| Saw curvature | Kernmayer adds curvature; nobody measured it. x0xb0x confirms only the rounded tip. |

The sources agree that duty and edge softness depend on frequency. Antto pins the
mechanism on the HPF ahead of the threshold ("this weirdness is because of the HighPass
filter before the tanh() shaper"). Aciddose adds that an RC sets the width of the soft
middle edge.

### 2.4 Recommendation

1. **Saw core.** Use a phase accumulator at the 4× rate with frequency
   `f = f_ref·2^{(V_cv − V_ref)·scale}`. Expose scale (default 1.0, "vintage" 0.997/1.027)
   and the TM4 offset as hidden calibration parameters.
   - Generate a **falling** ramp of 5.5 Vpp (6–11.5 V) with a short finite flyback, a few
     µs to tens of µs. Measure it on the schematic simulation. It produces the "not sharp"
     tip.
   - Antialias the reset with **3rd-order B-spline polyBLEP**. S22: perceptually
     alias-free up to f0 = 7.8 kHz at 44.1 kHz; 2-point polyBLEP is only good to 2.1 kHz.
     At 4× the cheap 2-point version is already adequate for the 303's ≤ 660 Hz range.
2. **Square.** Model the circuit, not the fit:
   - saw → 1st-order HPF (22 k·1 µF ≈ 7.2 Hz, or values from `private/docs/schematic/`);
   - → single-transistor shaper: exponential/tanh collector-current law around a fixed
     V_BE threshold, finite gain, collector load;
   - → output AC coupling (≈ 60 Hz per Schmidt/antto, to be checked against schematic
     values).

   Because the HPF ahead of the threshold moves the crossing point with the period, this
   reproduces frequency-dependent duty and edge width without fitting.
   - Validate against Open303's tanh(69.98·x ± 4.37) at ~87 Hz (47.5 % PW).
   - Validate against antto's VO(f) points: 0.385 at 17.3 Hz, 0.0077 at 109 Hz,
     −0.033 at 146.6 Hz.
   - Apply **1st-order ADAA** (antiderivative of tanh = log cosh, S21) to the shaper at the
     4× rate, or evaluate it at 8× locally. The shaper output is strongly band-limited by
     its own soft edges at low notes. The hard edge is the saw reset, already handled by
     polyBLEP.
3. **Waveform switch.** It is a hard switch on the real unit, so do not blend by default
   (Open303's default 0.85 blend is a sound-design choice). Include the loading of the
   unbuffered VCO output by the VCF input network (220 k), since S12 notes that loading
   changes the square.

---

## 3. VCF: diode ladder, resonance, feedback/coupling high-passes

### 3.1 Circuit facts

From Stinchcombe's annotated schematic and component-level SIMetrix deck (S3:
`tb303_vcf_res_part2.gif`, `tb303_comp_level.gif`).

**Ladder:**

- Four stages of diode-connected transistor pairs, driven from the bottom by the
  differential pair **Q12** (2SC1583).
- Topped by the transistor pair **Q22** (2SC2291). Its bases share one bias point, so
  analytically it acts as **one diode per arm (d = 1)** (S1 §3.1).
- Ladder caps, bottom to top: **18 nF, 33 nF, 33 nF, 33 nF**. Stinchcombe's closed forms
  use the ideal C1 = C/2. The real ratio is 18/33 = **0.545**.

**Input:**

- The VCO signal reaches Q12 through a 1 µF coupling cap and a **220 k / 2.2 k divider
  (≈ 1/100, −40 dB)**.
- The 5.5 Vpp saw therefore arrives as ≈ **54 mVpp (±27 mV)** at the input pair. In units
  of 2V_T ≈ 51.7 mV that is ±0.53: mildly nonlinear (tanh(0.53) = 0.485, ≈ 8 % peak
  compression) before feedback (my calc).
- Devil Fish "Overdrive" raises the drive up to 66.6× "normal" by replacing the 220 k path
  with ≈ 3.3 k (S10, S11).

**Output:**

- The top of the ladder is AC-coupled (0.1 µF, 100 k bias resistors) into the
  differential amplifier **Q21** (current-source tail Q20), then emitter follower **Q19**,
  then an electrolytic coupling cap.
- Stinchcombe's block 2 for this stage: **gain −106 with a 1-pole HPF at 92 Hz**
  (578.1 rad/s).
- a1k0n (S13) independently identifies "a 100 Hz high-pass filter in the feedback path
  formed by C27/R113 + C25/R114. This 100Hz high pass filter in the feedback path is part
  of what makes it sound like a 303; without it, it's too resonant at lower frequencies
  and sounds like it's whistling fifths and octaves of the fundamental."
- My reading: 0.1 µF into 100 k alone would give 16 Hz. The ≈ 92–100 Hz corner implies the
  low differential input resistance of Q21 (no emitter degeneration visible) in parallel
  with the 100 k. 1/(2π·0.1 µF·17 kΩ) ≈ 94 Hz.

**Resonance:**

- **VR4 = 50 kB (linear), dual-gang.** Gang 1 taps the buffered filter output (via C14)
  and feeds C15 (1 µF) → Q18 → back into the second base of the input pair.
- **Gang 2 is the accent sweep circuit (§5).** This coupling between resonance and accent
  is unique to the 303 (S10 and `dfish_Resonance-pot-schematic.png`).

**Cutoff:**

- An exponential converter (Q9/Q10/Q11) sets **I_f**, the current drawn from the bottom of
  the ladder.
- The CV sum includes the Cutoff pot (50 kA), the Env Mod pot (50 kA, fed by the MEG),
  the accent-sweep wiper through R72 = 100 k, and the trimmer TM3.
- Service-note calibration (S14): cutoff centre, saw, resonance full, env mod/decay/accent
  minimum. Adjust TM3 so the ringing period at TP6 is **2 ms ± 0.5 ms**, i.e. ≈ 500 Hz
  (400–667 Hz).

### 3.2 Linear core transfer function (consensus of S1, S5, S6, S7, S17)

Stinchcombe eq. (30). a = 2V_T/I_f, d = diodes at top, C1 = bottom cap, C = other caps:

```
−d/G(s) = 16a⁴s⁴·d·C1·C³ + 8a³s³·(C1C²(1+5d) + dC³)
        + 4a²s²·(C1C(4+6d) + C²(1+4d)) + 2as·(C1(3+d) + C(3+3d)) + 1          (S1 eq. 30)
```

TB-303 case, d = 1 and C1 = C/2:

```
G_tb(s) = −1 / (8u⁴ + 32u³ + 40u² + 16u + 1),     u = s·aC = s/ω_z,   ω_z = I_f/(2·V_T·C)
```

**This is exactly Zavalishin's diode ladder** (S5 eq. 5.27): Δ(s) = 1/T₄(1+s), with
T₄(x) = 8x⁴ − 8x² + 1 the 4th-order Chebyshev polynomial. It comes from his eq. (5.18):

```
ẏ1 = ω_c((x + y2) − y1)
ẏ2 = ω_c((y1 + y3)/2 − y2)
ẏ3 = ω_c((y2 + y4)/2 − y3)
ẏ4 = ω_c(y3/2 − y4)
```

So the "textbook" Zavalishin/Pirkle diode ladder, with its gain-1 first stage and ½ gains
on stages 2–4, *is* the TB-303 configuration. The half-size bottom cap is what makes the
first stage twice as fast. Z1202 (Zavalishin) on KVR: "the half-size first cap results in
uniform equations for all stages" (S9). Kunn/mystran derived the same ODE on KVR in
Oct 2009 (S9, "page 40").

Pole and slope facts:

- **Poles at k = 0:** s = ω_z(−1 ± cos π/8), ω_z(−1 ± cos 3π/8) =
  **−0.0761, −0.617, −1.383, −1.924 × ω_z**, all real (S5 eq. 5.28).
- The dominant pole sits at 0.076·ω_z, so the passband droops early. The k = 0 −3 dB point
  is **0.0747·ω_z** (0.0730 with 18/33 caps), which is **3.24 octaves below** the
  self-oscillation frequency (my calc).
- That early droop is the "18 dB/oct-like" knee. The asymptote is 24 dB/oct.
  Stinchcombe's SPICE measured stage slopes of −6.1, −11.9, −17.9 and −24.1 dB/oct (S2).
  Dropping the 4th pole visibly changes the in-band response (`tb303_3_vs_4.gif`).
- Stinchcombe normalises with ω_c,st = ω_z/2^{3/4} = I_f/(2^{7/4}CV_T). That gives
  `H_tb = −1/(s⁴ + 6.727s³ + 14.142s² + 9.514s + 1)`, poles −3.24, −2.33, −1.04 and −0.13.
- Equal caps give `s⁴+7s³+15s²+10s+1`. Halving C1 is ≈ a pure frequency shift of
  2^{1/4} = 1.189 with almost no change of shape, confirmed in SPICE (S2). Stinchcombe
  suspects the halved cap is for stability, not sound.

**With global feedback k** (S5 eq. 5.29):

```
H(s) = 1 / (8(1+s)⁴ − 8(1+s)² + 1 + k)
```

- **Self-oscillation at k = 17**, at **ω = ω_z/√2** (S5 §5.10). Pirkle (S6): "At k = 17,
  the filter self-oscillates; the peak frequency is fc/sqrt(2)". In Stinchcombe's
  normalisation this is k = 17.00 at 1.189·ω_c,st (my calc; equal caps give k = 18.39,
  S1).
- With the **real 18/33 caps**: k_osc = 17.02 at ω = 0.692/(aC). Normalised
  denominator s⁴ + 6.7319s³ + 14.1555s² + 9.5205s + 1, with ω_c = 0.5818/(aC) (my calc). The
  oscillation frequency is **≈ 64.5 Hz per µA** of ladder current with V_T = 25.85 mV and
  C = 33 nF.
- DC gain is 1/(1+k): −24.6 dB at k = 16. Pirkle measured ≈ −26 dB at k = 16 (S6).
- The resonant peak migrates upward with k (my calc, ideal caps):

  | k | peak at | peak vs DC |
  |---|---|---|
  | 4 | 0.317·ω_z | +1.5 dB |
  | 8 | 0.513·ω_z | +7 dB |
  | 12 | 0.619·ω_z | +14 dB |
  | 16 | 0.692·ω_z | +29.5 dB |

- kunn (S9): "in a tb-303 fc = 2a when k = 0 and fc = √2·a when k = 17". This is the same
  migration in his discretised notation.

### 3.3 Nonlinear core

Zavalishin S5 §6.9 (after Fig. 6.34). Mystran gives the same ODE on KVR (S9). Signals are
normalised to 2V_T; x = input minus k·y4:

```
ẏ1 = ω_z ·[ tanh(x)        − tanh(y1 − y2) ]
ẏ2 = ω_z/2·[ tanh(y1 − y2) − tanh(y2 − y3) ]
ẏ3 = ω_z/2·[ tanh(y2 − y3) − tanh(y3 − y4) ]
ẏ4 = ω_z/2·[ tanh(y3 − y4) − tanh(y4) ]
```

Derivation from Stinchcombe's per-stage relations (S1 eqs. 23–24, before linearisation):

- Each pair carries ΔI = I_f·tanh(ΔV/2V_T).
- Each cap obeys 2C_n·dΔV_n/dt = ΔI_{n+1} − ΔI_n.
- So the stage rate is I_f/(4V_T·C_n): **ω_z for the 18 nF stage (ideal ½C) and ω_z/2 for
  the 33 nF stages**. With the real 18 nF cap the stage-1 rate is (33/18)·ω_z/2 =
  **0.917·ω_z**.
- tanh(x) is the Q12 input pair. tanh(y4) is the top Q22 pair.

Mystran's form (S9), in volts:

```
dV1/dt = (I_c/C1)·[tanh(V_in/2V_t) − tanh((V1−V2)/2V_t)]
dV2/dt = (I_c/C2)·[tanh((V1−V2)/2V_t) − tanh((V2−V3)/2V_t)]
…
dV4/dt = (I_c/C4)·[tanh((V3−V4)/2V_t) − tanh(V4/2V_t)]
```

The VCS3 analogue (Fontana & Civolani, as restated in S19) has γ = ηV_T (η = 1.836 for
1N4148) in the inter-stage tanh, a 6γ term at the top (3 diodes), and output
(K + ½)·v_C4. The VCS3 uses diodes; the 303 uses diode-connected transistors, so γ ≈ V_T.
Stinchcombe found a ~6 dB passband anomaly when mixing 1N4148 and transistor junctions
(S1 §3.4). With matched transistor junctions, as in the 303, d = 1 and γ = V_T are
appropriate.

### 3.4 What surrounds the core: Stinchcombe's coupling-cap model (S3)

Five coupling-cap networks surround the core. They were modelled as a SPICE Laplace-block
model and validated against a component-level simulation from 1 Hz to 10 kHz
(`tb303_lap_v_comp_580.gif`). Block values, with HP(f) = (s/2πf)/(1 + s/2πf):

| Block | Circuit | Transfer function |
|---|---|---|
| 1 | input coupling + 220 k/2.2 k divider | 0.01·[17.495·5.407/(2π·15.517·6.164·0.708)]·s·(1+s/2π·17.495)(1+s/2π·5.407) / [(1+s/2π·15.517)(1+s/2π·6.164)(1+s/2π·0.708)] |
| core | ladder (f_c,st = 820 Hz in his plots) | −1/(u⁴ + 6.727u³ + 14.142u² + 9.514u + 1), u = s/2π f_c,st |
| 2 | output diff-amp Q21 (**in loop**) | −106·HP(92 Hz) |
| 3 | output coupling | HP(3.18 Hz) |
| 4 | resonance pot + coupling (**in loop**) | res·0.98·HP(1.18 Hz), res ∈ [0, 1] |
| 5 | feedback injection network (**in loop**) | (1/5.55)·[7.408·0.701/(2π·15.517·6.164·0.708)]·s·(1+s/2π·7.408)(1+s/2π·0.701) / [(1+s/2π·15.517)(1+s/2π·6.164)(1+s/2π·0.708)] |

Closed form (S2, S3), k = res ∈ [0, 1], s in rad/s:

```
H(s) = 1.06·s³(s+109.9)(s+34.0)(s+7.41) /
       { D_tb(s)·(s+97.5)(s+38.5)(s+4.45)(s+578.1)(s+20.0)(s+7.41) + 18.7·k·s⁴(s+46.5)(s+4.40) }
D_tb(s) = s⁴/ω⁴ + 2^{11/4}s³/ω³ + 10√2·s²/ω² + 2^{13/4}s/ω + 1
```

Stinchcombe also states I_f = 25 µA ≈ 1 kHz cutoff (KVR p=3800158, S9).

Consequences, from my numerical evaluation of the block model:

- **In-band loop gain at full resonance = 106·0.98/5.55 = 18.7**, against the k = 17
  threshold. The real 303 has only ≈ 10 % excess loop gain, and only where the in-loop
  92 Hz HPF is negligible.
- **Self-oscillation therefore depends on cutoff.** At res = 100 %, the loop gain at the
  −180° crossing and the frequency where it occurs are:

  | f_c,st | 300 Hz | 820 Hz | 1 kHz | 2 kHz | 5 kHz | 10 kHz |
  |---|---|---|---|---|---|---|
  | crossing frequency | 430 Hz | 1.05 kHz | 1.27 kHz | 2.46 kHz | 6.0 kHz | 12.0 kHz |
  | loop gain | 0.74 | 0.94 | 0.97 | 1.03 | 1.07 | 1.09 |

  **The linear model self-oscillates only when the resonant frequency exceeds ≈ 2 kHz.**
  This agrees with:
  - Whittle: self-oscillation only at mid/high cutoff (S10);
  - ParadoxUn: "in the top frequencies only" (S9);
  - Janney: "it will never go into self osc (but gets pretty close)" (S9);
  - kunn: "not the case with TB-303" (S9).

  Unit-to-unit variation of the Q21 gain and the trimmers easily moves the boundary.
- **Second resonance at ≈ 6.5–8.7 Hz** from the in-loop high-passes: loop gain ≈ 0.5 at
  the −180° crossing near 6.5 Hz (f_c,st = 820 Hz). The closed-loop low peak is −12.5 dB
  at 10.9 Hz (res 0.6) and −10.3 dB at 8.7 Hz (res 0.99). Stinchcombe: "The lower
  resonant peak is very real … [my TBX-303 clone could] oscillate comfortably at around
  8 Hz". Mystran (S9): "The VCF has enough HP poles in the loop to have some low-frequency
  resonance, which the low-frequency content of the transients is going to excite". So
  never reset the filter state on note-on.
- **Passband loss with resonance**, |H(100 Hz)| at f_c,st = 820 Hz:

  | res | 0 | 0.2 | 0.4 | 0.6 | 0.8 | 0.99 |
  |---|---|---|---|---|---|---|
  | dB | −5.0 | −14.4 | −18.8 | −21.7 | −23.9 | −25.6 |

  Because feedback weakens below ~100 Hz, deep bass loses **less** than the midband. antto:
  "the 303 filter looses _alot_ of its output gain when you increase the resonance" (S9).
- **Main peak** at f_c,st = 820 Hz: −12.2 dB at 854 Hz (res 0.6), −8.0 dB at 957 Hz
  (res 0.8), +2.9 dB at 1.03 kHz (res 0.99).

### 3.5 Open303's VCF (S7 `rosic_TeeBeeFilter`)

Mode `TB_303` is "ala mystran & kunn (page 40 in the kvr-thread)", with coefficient fits by
antto (S9, Oct 2009):

```cpp
y0  = in - feedbackHighpass.getSample(k*y4);   // 1-pole HPF @150 Hz in the loop
y1 += 2*b0*(y0 - y1 + y2);
y2 +=   b0*(y1 - 2*y2 + y3);
y3 +=   b0*(y2 - 2*y3 + y4);
y4 +=   b0*(y3 - 2*y4);
return 2*g*y4;
```

**Discretisation:** explicit (forward) Euler of S5 eq. (5.18), with ω_z·T = 2·b0. Linear:
the cubic `shape()` and kunn's tanh variants are commented out. It runs at 4×.

**Euler warping is patched with fits** in fx = (f_c/f_s)/√2:

```
b0 = (0.00045522346 + 6.1922189·fx) / (1 + 12.358354·fx + 4.4156345·fx²)
k  = 16.998792 + 213.87126·fx + 614.95611·fx² − 476.47308·fx³ − 5837.7917·fx⁴ + 7198.6997·fx⁵ + fx⁶
g  = ((k/17 − 1)·r + 1)·(1 + r);   k ← k·r
```

- The fits place the resonant peak near the nominal cutoff: at f_c = 1 kHz and 176.4 kHz,
  ω_z/√2 ≈ 956 Hz (my calc).
- k → 17.0 as f_c → 0, matching the analytic threshold.

**Resonance pot skew:** r = (1 − e^{−3·res})/(1 − e^{−3}), giving 25 % → 0.555,
50 % → 0.818, 75 % → 0.941.

- With k(100 %) ≈ 17, Open303 sits exactly on the oscillation edge at every cutoff.
- The real unit (§3.4) is below the edge under ≈ 2 kHz and slightly above it higher up.
- The real pot is linear (50 kB); the 303's feedback gain is ~linear in pot position
  (Stinchcombe block 4).

**Cutoff clamp:** 200 Hz … 20 kHz.

**Other modes** (LP6…BP6/6, FLAT) are a Moog-style cascade with a polynomial a1
coefficient and k ≈ 4. The `TB_303` exact-coefficient path multiplies Moog k by 17/4.
Schmidt noted that makes "resonant frequency and self-osc gain deteriorate" (S9).

**Feedback HPF provenance:** the 150 Hz is "copied from ABL" (RS-MET comment, S8), not
measured on a 303.

### 3.6 Other published discretisations

**Pirkle AN-6 (S6), linear TPT.** With g = tan(ω_cT/2):

```
G4 = 0.5g/(1+g)
G3 = 0.5g/(1+g−0.5g·G4)
G2 = 0.5g/(1+g−0.5g·G3)
G1 = g/(1+g−g·G2)
S4 = s4/(1+g)
S3 = (0.5g·S4 + s3)/(1+g−0.5g·G4)
S2 = (0.5g·S3 + s2)/(1+g−0.5g·G3)
S1 = (g·S2 + s1)/(1+g−g·G2)
Γ  = G1G2G3G4
Σ  = G4G3G2·S1 + G4G3·S2 + G4·S3 + S4
u  = (x − kΣ)/(1 + kΓ)
```

- Stage input gains a0 = 1, 0.5, 0.5, 0.5.
- "Budget" nonlinearity: tanh at the input only. Pirkle says the full nonlinear version
  would "be a challenge (if not impossible to process)".
- Kernmayer (S17) adds tanh(0.5Σ)/tanh(1) in the global feedback and a cubic
  x − x³/3 in the inter-stage feedback, because tanh there "destroys the filter". His
  Python model went unstable at f_c ≥ 15 kHz with k = 16.

**Zavalishin S5 §5.10, nested ZDF.** He proves each nested instantaneous gain is bounded,
so the linear ZDF never goes instantaneously unstable for k ≥ −1:

```
g23 = g/(1−g²) < 2/3
g12 = g/(1−g·g23) < 3/4
g01 = 2g/(1−2g·g12) < 4
g04 < 1
```

**Mystran's "cheap non-linear zero-delay filters"** (KVR t=349859, S9):

- Replace each tanh(v) by v·T(v₀), with T(x) = tanh(x)/x evaluated at the previous state,
  and do one linear ZDF solve per sample. Example 1-pole:

  ```
  t = T(0.5(x[n]+x[n−1]) − s);  y = (s + f·t·x)/(1 + f·t);  s += 2f·t·x;  f = tan(π f_c/f_s)
  ```

- "works remarkably well for … transistor ladders, diode ladders, OTA cascades".
- 4× oversampling recommended; an optional Heun correction costs about 2× the CPU.
- This is Zavalishin's "linearisation by secant line" (S5 §6.6).

**karrikuh (KVR 2012, MIT code):** ZDF diode ladder with one input tanh,
y0 = tanh(x − k·y4) solved with one iteration, ≥ 2× oversampling, an HPF in the feedback
from v0.2. Urs Heckmann: with a single clipper the multiple series nonlinearities
"replace the cutoff coefficients", so it behaves wrongly under cutoff modulation (S9).

**Full Newton on the multi-dimensional system** (S5 §6.10, "flattened" form y = Φ(y); S19):

- Zambon & Fontana solve the VCS3 tanh ODE with the trapezoidal rule + Newton–Raphson:
  J_F = (T/2)·J_f − I, LU with pivoting, L∞ < 1e−8.
- 3–6 iterations, **8× oversampling**, error below −80 dB.
- About 500–1000 multiplies per oversampled sample; under 10 % of a Core2Duo.
- GriffinBoy 2026 (non-peer-reviewed, S24): 4-state trapezoid/Newton costs 12 % CPU at
  8× and 6 % at 4× on a Ryzen.

**Huovilainen (S20), Moog.**

- Forward Euler with tanh per stage (5 tanh per sample) at 2×.
- A half-sample delay in the feedback keeps the loop phase ≈ 180° up to f_s/4; tuning
  error < 10 %.
- Not recommended here: the explicit form needs tuning fits, like Open303.

**Stability under modulation (S24, DAFx-26 / Werner–McClellan):**

- The trapezoidal rule applied to a stable continuous-time filter stays stable under
  arbitrary cutoff modulation g[n].
- For the equal-cap diode ladder with time-varying k[n], stability is only proven for
  k ≤ 8.90. Irrelevant here: the 303's resonance pot is static.
- **Oyama DAFx-26:** close linear agreement can hide large nonlinear differences, and the
  earliest-stage nonlinearities matter most. Keep the input and stage-1 tanh.

### 3.7 Disagreements

**Feedback HPF.** Values found:

| Source | Feedback HPF |
|---|---|
| Open303 (copied from ABL) | 150 Hz |
| a1k0n (C27/R113 + C25/R114) | ≈ 100 Hz |
| Stinchcombe (output amp, block 2) | 92 Hz, plus 1.18 Hz and the 0.7/6.2/15.5 Hz injection network |
| antto, fits | 54, 70–140 (used 128), 122 Hz |
| neotec, fit | 320 Hz |

A single pole cannot produce the ≈ 8 Hz low resonance.

**Cap ratio.** Every closed form uses ½. The real value is 18/33. The effect is a ≈ 2 %
shift of the oscillation-frequency ratio (0.692 vs 0.707 /aC). Model the real value; it
costs nothing.

**Nonlinearity placement:**

- none: Open303 TB_303 mode;
- input only: Pirkle "budget", karrikuh;
- input + feedback + cubic inter-stage: Kernmayer;
- **every junction pair: Zavalishin, mystran, Civolani/Fontana**, the physically derived
  option.

**"18 dB/oct".** Roland marketed it that way and Janney says the slope "varies between 18
and 24 dB depending on the cutoff frequency" (S9). Stinchcombe: 4 poles, 24 dB/oct
asymptotically, with an early knee.

**Self-oscillation.** "Never" (Janney) vs "only at mid/high cutoff" (Whittle). The
Stinchcombe model puts the boundary at ≈ 2 kHz, marginal and unit-dependent.

**a1k0n's model.** His browser model (S13 `303.js`) uses Moog-style k = 4·reso with
fitted pole tables. His `filt303.py` `laplace303()` is a buffered
(1+s/ω)³(1+2s/ω) form. Neither is the coupled diode ladder. Use only his measurements,
not his model structure.

### 3.8 Recommendation: VCF

1. **Core:** the 4-state tanh ODE of §3.3, with stage-1 rate I_f/(4V_T·18 nF) and stages
   2–4 rate I_f/(4V_T·33 nF). The input pair drive x is the input after the 220 k/2.2 k
   network, minus feedback, over 2V_T. Keep V_T (temperature) as a hidden "drift"
   parameter.
2. **Periphery, in the same implicit solve:**
   - Stinchcombe blocks 2, 4 and 5 inside the loop: the 92 Hz output-amp HPF, the
     1.18 Hz pot coupling and the injection network.
   - Blocks 1 and 3 outside the loop.
   - Better: re-derive these RC networks from `private/docs/schematic/` values so they follow the
     pot positions. Use Stinchcombe's numbers as the check.
   - Loop gain = 18.7 × VR4 position (linear pot, plus loading from the schematic).
3. **Output amp Q21:** model it as a differential pair, I_tail·tanh(v_top/2V_T), into its
   22 k loads, inside the resonance loop. This is a likely amplitude limiter at high
   resonance. **Unverified: confirm with a transient SPICE run before relying on it.**
4. **Solver:** trapezoidal (TPT/ZDF) over the whole loop.
   - Newton–Raphson, warm-started from the previous sample (or a linear-TPT predictor);
     2–4 iterations, tolerance ≈ 1e−9, iteration cap ≈ 8.
   - The Jacobian is tridiagonal (ladder) plus a feedback column, so a Thomas sweep with
     Sherman–Morrison or a small dense LU handles it. tanh′ = 1 − tanh² comes free.
   - Fallback (an "eco" mode): mystran/Zavalishin secant linearisation, one linear solve
     per sample, at 4–8×.
   - Do **not** use explicit Euler with fitted tuning polynomials (Open303). The fits are
     valid only for the linear filter at one oversampling factor.
5. **Rate:** run the VCO → VCF chain at **4× (176.4/192 kHz)**. Offer 8× as HQ/offline.
   - S5 §6.13: "The only thing which is more or less guaranteed to work is heavy
     oversampling … at least the entire feedback loop must be oversampled". ADAA (S21) is
     unproven inside multi-state implicit loops. It could be applied to the input tanh
     only.
   - Use **double precision** state. S5: 32-bit floats run out quickly at high
     oversampling.
6. **Cutoff CV → I_f:** model the expo converter and the CV summing network (§4.3).
   Recompute ω per oversampled sample. With a direct ODE solve this costs only the
   multiplications.
7. **Validation targets:**
   - (a) small-signal |H| against Stinchcombe's closed form, res 0…0.99 at
     f_c,st = 820 Hz, including the 8 Hz peak;
   - (b) self-oscillation onset at a resonant frequency of ≈ 2 kHz, res 100 %;
   - (c) the service-note ringing period of 2 ± 0.5 ms at cutoff centre, res max, env
     mod min;
   - (d) a1k0n's cutoff/envelope fits (§4);
   - (e) the real-303 samples in `a1k0n_303/repo/wav`.

---

## 4. MEG: main (filter) envelope and the cutoff CV

### 4.1 Facts

**Shape:** Whittle (S10 `dfish_303-unique.txt`): MEG has a "sharp attack and exponential
decay". Schmidt (S9, p=3760402): the sweep extracted from a recording by spectrogram fits
an "exponential decay (as delivered by the leaky integrator or multiplicative
accumulator) … very well".

**Decay time:**

- Normal notes: "between 200 ms and 2 seconds – as controlled by the Decay pot" (S10
  manual).
- Accented notes: "the decay time was fixed to 200 ms", i.e. the Decay pot is shorted out,
  equal to the pot fully anticlockwise.
- Whether "200 ms–2 s" is a time constant or an audible duration is not stated.
- antto's fitted decay coefficient (S9, Jan 2010), *before* his 5th-order output shaper:
  `exp(−1/(0.82725·(0.341 + (1−ACC)·dknob·(4.651−0.341))·Fs))`. That is
  τ = 0.282 s (decay min or accent) to 3.85 s (decay max).
- Open303 uses τ = Decay ms directly (200–2000 ms, exponential knob law; 200 ms on accent).
- a1k0n's rough fit: the VCF envelope reaches 10 % in (0.1 + decay_knob) s (S13).
- **Disagreement:** these are all compatible only if the definitions differ.

**Env Mod minimum is not zero.** "The circuitry does not allow this effect to be turned
down to zero even when the pot is fully anti-clockwise" (S10). The Devil Fish range is 3×
wider and starts at 0.

**Cutoff CV settling:** a large CV step lands "99.8 % … instantaneously (less than
1 millisecond), but there is a residual drift towards the final frequency over the next
500 msec or so", from capacitors resistively coupled to the summing point (S10 manual).

**Pots** (S10 `dfish_303-mods.txt`):

- Cutoff 50 kA and Env Mod 50 kA (log at the anticlockwise end).
- Decay 50 kA, though "1 meg linear (1MA)" also appears on the same page. Disagreement.
- Mike Janney: "the cutoff pot is anti log. Anti log in analog is not a perfect curve
  however" (S9).

**Maximum resonant frequency** with no env mod ≈ 2.5 kHz. Devil Fish "≈ 5 kHz … an
octave above the typical maximum for the TB-303" (S10).

**Measured cutoff vs current** (a1k0n on an x0xb0x, S13 `303.js`): "cutoff frequency …
appears to be about 27.6 Hz/mV_R69 + 103 Hz". The envelope endpoints fitted against the
knob positions c, e ∈ [0, 1]:

```
f_peak(c,e) = exp(5.55921003 + 2.17788267·c + 1.99224351·e) + 103   [Hz]
f_base(c,e) = exp(5.22617147 + 1.70418937·c − 0.68382928·e) + 103   [Hz]
```

| c, e | 0, 0 | 0, 1 | 1, 0 | 1, 1 | 0.5, 0 |
|---|---|---|---|---|---|
| f_base (Hz) | 289 | 197 | 1126 | 619 | 538 |
| f_peak (Hz) | 363 | 2007 | **2395** | ≈ 16.9 k | 875 |

- Env mod raises the peak and *lowers* the floor.
- The service-note ringing target of ≈ 500 Hz at cutoff centre matches f_base(0.5, 0) =
  538 Hz.

**Open303 "measured mapping"** (S7 `calculateEnvModScalerAndOffset`, "constants that arise
from the measurements"). The MEG output decays from 1 to 0:

```
c0 = 313.8152786059267 Hz (lowest nominal cutoff),  c1 = 2394.411986817546 Hz (highest)
e = envMod/100;   c = log(cutoff/c0)/log(c1/c0)
envScaler [oct] = (1−c)·(3.773996325111173·e + 0.736965594166206)
                +    c ·(4.194548788411135·e + 0.864344900642434)
envOffset       = 0.048292930943553·c + 0.294391201442418
f_inst = cutoff · 2^( envScaler·(MEG − envOffset) + accentGain·rc2(MEG) )
```

Implications (my calc):

| cutoff knob | env mod | sweep | env end → peak |
|---|---|---|---|
| min | 0 % | 0.74 oct | 270 → 450 Hz |
| min | 100 % | 4.51 oct | 125 → 2.85 kHz |
| centre (867 Hz) | 0 % | 0.80 oct | 727 → 1265 Hz |
| max | 0 % | 0.86 oct | 1.95 → 3.55 kHz |
| max | 100 % | 5.06 oct | 720 Hz → 24 kHz (clamped to 20 k) |

29–34 % of the sweep lies below the knob value. The RS-MET variant (S8) uses a cruder
`(envMod/12)·(MEG − 0.35)` octaves.

**Coincidence worth noting:** Open303's c1 = 2394.4 Hz equals a1k0n's f_peak(1, 0) =
2395 Hz. Whittle's "max ≈ 2.5 kHz" agrees. Open303's c0 = 313.8 Hz lies between a1k0n's
f_base(0,0) = 289 and f_peak(0,0) = 363.

**What "cutoff" means:**

- Open303's nominal cutoff is the **resonant-peak frequency**. Its TB_303 filter is tuned
  so the peak lands there.
- AudioRealism measured "the resonant frequency which is not the same as the cutoff
  frequency" (S9). Their MIDI-modified unit spans about 200–25 000 Hz.
- For the diode ladder, the k = 0 −3 dB point is 3.24 octaves below the
  self-oscillation frequency (§3.2). **Any calibration must state which frequency it
  matches.** a1k0n's "cutoff" is the parameter of his fitted pole model; its relation to
  the resonant peak is not documented.

### 4.2 Disagreements

| Quantity | Values |
|---|---|
| Env sweep at env mod 0 | 0.74–0.86 oct (Open303) vs 0.33 oct at c = 0 to 1.09 oct at c = 1 (a1k0n). Both say non-zero; Whittle agrees. |
| MEG time | Whittle 0.2–2 s; antto τ 0.28–3.85 s (pre-shaper); Open303 τ = 0.2–2 s; a1k0n 10 % point at 0.1 + knob s |
| Floor vs env mod | a1k0n's floor drops as env mod rises (fixed offset 0.29–0.34 of the sweep below the knob). Open303's offset is a fixed fraction of the sweep, so its floor also drops. They agree qualitatively. |

### 4.3 Recommendation

1. **MEG generator:** a capacitor charged near-instantly at gate-on and discharged through
   the Decay pot network (decay pot + series resistor). Accent shorts the pot. Take
   component values from `private/docs/schematic/` and fit the time constant to give 200 ms /
   2 s at the extremes. Then cross-check against antto's τ and a1k0n's 10 % times.
2. **CV summing network:** model it as written in the schematic: Cutoff pot (50 kA law),
   Env Mod pot (50 kA) scaling the MEG, accent-sweep wiper via R72 100 k, TM3, plus the
   resistive non-zero minimum. Then the expo converter:
   I_f = I_ref·exp(V_b/V_T,eff), and I_f → ω_z (§3.3).
   - Calibrate I_ref and V_T,eff so the resonant-peak frequency hits a1k0n's
     f_peak/f_base table, within the spread to Open303's numbers.
   - Calibrate TM3 to the service-note 500 Hz ringing at cutoff centre.
3. **Settling:** reproduce the ~500 ms residual drift by including the coupling caps on
   the summing node. It is only audible at high resonance.
4. **Rate:** update at ≥ 1–4 kHz with per-sample linear interpolation of ln(I_f) at the
   oversampled rate. The MEG attack is fast, so either run the MEG at audio rate or
   interpolate carefully at the attack.

---

## 5. Accent and the accent sweep circuit

### 5.1 Facts

Whittle, "TB-303 unique" page and Devil Fish manual (S10).

**On an accented note:**

- (i) The MEG decay is forced to the short (≈ 200 ms) setting.
- (ii) The MEG voltage passes through a switch that is on only during accented notes,
  then through the **Accent pot** (50 kB). From there it does two things:
  1. "It adds to the control current of the VCA. (Via an RC network of a **47k and a
     0.033uF** to soften the attack a little.) This is the primary reason why accented
     notes are louder." τ = 47 k·33 nF = **1.55 ms** (my calc).
  2. Via the **Accent Sweep Circuit** it adds to the filter control current.

**Accent sweep circuit:**

> a diode and a 47k resistor in series driving the ACW end of a 100k pot, and a 1uF
> capacitor (to ground) hanging off the CW end. The wiper of the pot goes via a 100k mixing
> resistor to one of the two summing nodes for filter frequency … This pot is the second
> section of the Resonance pot.

Designators (`dfish_Resonance-pot-schematic.png`):

- pin 3 ← R46 (47 k from the diode);
- pin 4 (wiper) → R72 (100 k);
- pin 5 → C13 (1 µF) to ground.
- Gang 1 of the same pot: pin 6 ← C14 (filter out), pin 1 → C15 (resonance feedback),
  pin 2 to ground.

**Behaviour:**

- **Resonance anticlockwise:** the filter gets "a sudden hit … largely a direct copy of the
  output of the MEG … basically 100/147 of the MEG voltage minus the capacitor voltage".
- **Resonance clockwise:** "the pulse of voltage driving the filter is smoothed by coming
  from the 1uF cap as it is charged via the diode and 147k of resistance". The filter
  "goes 'Wow.' It rises and falls in a quick, smooth curve".
- **Successive accents** go higher: "Since the capacitor has not discharged fully from the
  one before, the second and subsequent response curves go *higher*".
- **Pot value disagreement:** Whittle's text says 100 k; his parts list says Resonance is
  a dual **50 kB**.
- mystran (S9, 2026): the diode means "there _is_ actually a sort of 'attack' here. Then
  when the envelope falls below the cap, the diode stops conducting … the release is sort
  of 'always active.'" Mike Janney recommends a decay envelope through a one-pole filter.
- **Decay knob:** has no effect while accent is held (dinsync VCF303 manual, S16).

### 5.2 Derived equations (my calc, from the topology above)

Wiper position w ∈ [0, 1] from pin 3 (resonance ACW) to pin 5 (CW); pot resistance R_p;
summing node treated as a virtual ground. Let V_d = accent_pot·MEG(t) − V_diode.

**Diode conducting (V_d > V3):**

```
(V_d − V3)/47k = (V3 − Vw)/(w·R_p)
(V3 − Vw)/(w·R_p) + (Vc − Vw)/((1−w)·R_p) = Vw/100k
C13·dVc/dt = (Vw − Vc)/((1−w)·R_p)
i_cutoff = Vw/100k            (current into the filter-frequency summing node)
```

**Diode off:** node 3 floats, so V3 = Vw. C13 discharges through (1−w)R_p + 100 k.

Limits:

- **w = 0 (resonance ACW):** Vw = V3, so the output is V_d·100/147 minus a small cap
  term. Whittle's statement exactly.
- **w = 1 (resonance CW):** the cap sits on the wiper.
  - Charge τ = C13·(47 k + R_p) ∥ 100 k: **59.5 ms with R_p = 100 k, 49 ms with 50 k**.
  - Steady-state gain 100 k/(100 k + 47 k + R_p): 0.40 (100 k) / 0.51 (50 k).
  - Discharge τ = C13·100 k = **100 ms** into the summing node.
- At 130 BPM a 16th note is 115 ms, so the cap holds roughly 30 % of its charge into the
  next step. That accounts for the build-up on runs of accents.

### 5.3 Models found

**Open303 (S7):**

- accentGain = accent knob (0…1) on accented notes, else 0.
- Cutoff exponent term `accentGain · rc2(MEG)`, where rc2 is a leaky integrator with
  **τ = 15 ms** and a state that is never reset: up to **+1 octave**.
- VCA term `accentGain · 4.0 · MEG`.
- Accent decay 200 ms, accent amp release 50 ms (normal 1 ms).
- Resonance has no influence on the sweep shape.

**Kernmayer (S17):** accent = MIDI velocity ≥ 100. Listening scores for accent/slide
patterns were lower (6.5–6.7/10) than for plain saw patterns (8.3/10), which shows how
hard the accent is to get right.

### 5.4 Recommendation

1. Simulate the actual network of §5.2 at control or audio rate: diode (exponential or
   piecewise-linear), R46 47 k, the gang-2 pot at the **same position as the resonance
   knob**, C13 1 µF, R72 100 k into the CV sum. Resolve the 50 k vs 100 k question from
   the schematic BOM.
   - This gives the resonance-dependent "wow".
   - It gives accumulation over successive accents.
   - It gives the diode-asymmetric attack/release, none of which Open303 has.
2. Accent → VCA: MEG·accent_pot through 47 k/0.033 µF (τ 1.55 ms) into the VCA control
   current (§6).
3. Accent gate logic (§8): the accent bit applies for the whole step. Accented notes force
   the MEG short.

---

## 6. VCA and VEG (volume envelope)

### 6.1 Facts

**VEG** (S10):

- "Sharp attack, exponential decay, fixed decay and rather long time … Drives the VCA and
  does nothing else."
- "The TB-303's VEG decay was fixed at ~ 3 to 4 seconds".
- The owner's manual says the Decay knob lengthens "both the volume and the tone". The
  circuit analyses say Decay sets only the MEG (S14 note). The perceived volume change
  comes from the MEG → VCA accent path and the filter.
- x0xb0x: **R123 = 1.5 MΩ** sets the VEG decay (S11/S12). Capacitor value not captured;
  see schematic.

**Attack and gate-off:**

- "In the TB-303 there was a (typical) **4 ms delay and then a 3 ms attack** time" (S10
  manual).
- "The start of the audible note was between **1 and 5 ms (typically 4 ms)** after the
  start of the gate signal" (S10).
- antto: the VCA envelope charges "up to 5 ms" before it opens (S9).
- a1k0n: one-pole attack τ ≈ 0.39 ms (S13, `vca_attack = 1 − 0.94406088` at
  44.1 kHz).
- **Gate-off, unaccented:** "the decay at the end of an unaccented note was about 16 ms –
  **8 ms of normal volume and 8 ms of linear decay**" (S10). a1k0n fit a post-gate τ of
  ≈ 22 ms (S13).
- **Accented notes:** "since the gate is off the volume drops to nothing very quickly –
  unless it is an accented note, in which case the Main EG drives the VCA as well" (S10).
- Open303: VEG decay τ = 1230 ms; release τ = 1 ms (normal) / 50 ms (accent). A 2nd-order
  Butterworth 200 Hz lowpass "de-clicker" on the control signal. Plus
  `+0.45·MEG + 4.0·accent·MEG` while the gate is on.
- Kernmayer by ear: decay 800 ms, attack 3 ms, release 10 ms.

**VCA nonlinearity:** aciddose: "the filter and vca have significantly non-linear inputs"
(S9). Not quantified anywhere.

### 6.2 Disagreements

- **VEG duration:** "3–4 s" (Whittle, likely an audible duration) vs τ = 1230 ms
  (Open303; −30 dB at 4.2 s and −60 dB at 8.5 s) vs 800 ms (Kernmayer).
- **Normal-note release:** Whittle's 8 ms hold + 8 ms linear fall vs exponential τ = 1 ms
  (Open303) vs τ = 22 ms (a1k0n).

### 6.3 Recommendation

1. Model the VEG as the real RC: charge through the attack path (≈ 3 ms) after the gate
   logic delay (1–5 ms, default 4 ms), then decay through R123 (1.5 MΩ) and the schematic
   cap (target ≈ 3–4 s audible).
2. Model the gate-off behaviour as the circuit does it. Whittle's description (8 ms flat,
   then 8 ms linear) suggests a timer plus a current-limited discharge. Verify against the
   schematic before choosing a shape.
3. The VCA control current is VEG + accent·MEG (47 k/0.033 µF) + any MEG leak present in
   the circuit. Open303's +0.45·MEG on all notes has no circuit citation; check it in
   SPICE.
4. Model the VCA as a transistor VCA (differential pair → tanh on the signal input) if the
   schematic confirms it. Drive levels come from the filter output chain (§9).

---

## 7. Slide

### 7.1 Facts

**Time constant:** "The usual rather fast slide of a TB-303 is caused by the **100K
resistance of the DAC feeding a 0.22uF capacitor**" (S10 manual), so τ = **22 ms**.

- "Normally the slide time is 60 ms" (S10). That is the approximate audible time:
  60 ms ≈ 2.7τ, 93 % of the interval.
- Gordonjcp (S9, 2023): DAC Thevenin "5V/0.00005A = 100kOhm … RC = 100kOhm·0.22uF =
  0.022 seconds"; "nearly 100 ms" for a full swing.
- mystran: "from the capacitor it looks like 100k, because it effectively always has two
  200k paths in parallel".

**Domain:** the slide filters the **pitch CV in volts** ahead of the expo converter, so
pitch approaches the target exponentially in semitones. It also feeds CV out.

**Timing** (S10 `dfish_303-slide.txt`):

- "If there is a slide programmed on that note, then the slide circuit is actually
  activated on the *next* note. The slide starts at the start of clock-pulse 0 and ends
  at the end of clock-pulse 5."
- "The note it is programmed on does not turn off, but the gate stays high and runs into
  the next."
- The slide signal goes high 1/16 after the start of the slid note and stays active until
  the end of the note that follows it.

**CV latch strobe** (mystran, S9):

- The 303 reuses the SLIDE line as the CV latch strobe: "a pulse of 44us at the beginning
  of every note"; "8.75us" strobes; CPU ≈ 440 kHz (2.3 µs/clock).
- 4066 R_on ≈ 20–100 Ω with 0.22 µF gives τ ≈ 4–22 µs, so non-slide pitch changes are
  quasi-instant (≈ 100 µs).
- antto: "slide signal has to be bumped shortly in the beginning of every step". The
  x0xb0x uses a dedicated latch pin instead.

**Open303 (S7):** a one-pole leaky integrator on **frequency in Hz** with τ = 60 ms. The
header claims "The TB-303 had a slide time of 60 ms".

**Kernmayer (S17):** follows Whittle (gate held, glide during the next note).

### 7.2 Disagreements

- **τ:** 22 ms (100 k effective) vs 44 ms (200 k). aciddose's 276 ms wrongly includes 2π.
- **Open303** uses τ = 60 ms in the Hz domain: a ≈ 2.7× slower τ and the wrong curve
  shape for large intervals.

### 7.3 Recommendation

1. One-pole RC, **τ = 22 ms** (100 k × 0.22 µF; take exact values from the schematic), on
   the CV voltage, sample-accurate.
2. Engage it from the start of the step after a slide-flagged step until that step ends.
   Keep the gate high across the boundary. Do not retrigger the MEG or VEG.
3. Optionally model the 44 µs latch strobe on every note. A τ ≈ 20 µs step leaves a
   ~100 µs pitch transition, inaudible but harmless.

---

## 8. Sequencer gate and timing

### 8.1 Facts

- **Clock:** 24 ppqn, so **6 clock pulses per 1/16 step** (S10).
- Service Notes: at full tempo the clock period at TP1 is **8 ms ± 1 ms**, i.e. 312.5 BPM
  (S14). Owner's manual: tempo ♩ = 40–300. Internal clock check: TM2 for 1.8 ms ± 0.2 ms
  (S14).
- **Gate:** "The gate goes high at the start of clock pulse 0 … goes low half-way through
  clock pulse 3 … So there are **3.5 clock pulses on and 2.5 off**", i.e. 58.3 % of the
  step (S10).
- Kernmayer uses the same 3.5/6. Open303 uses `stepLength = 0.5`. a1k0n assumed 50 %.
- **Slide:** the gate stays high into the next step; slide is active during the next step
  (§7).
- **Gate to audio delay** (§6): 1–5 ms, typically 4 ms.
- **CPU:** NEC D650C-133 at ≈ 440 kHz. CV latch strobes as in §7 (mystran, S9).
- **Gate out:** 0 / +12 V. CV out: +1…+5 V, 1 V/oct (S14).
- **Triplet mode:** 1–15 steps per measure (S14 owner's manual).
- Whittle on sequencer "groove" myths: the TR-808 "plays in perfect time with the clock
  pulses". The 303 has no swing either.

### 8.2 Recommendation

- Implement a 24 ppqn clock with sample-accurate edges and gate = 3.5/6 of a step.
- For host-MIDI note mode, map note length directly. Accent from velocity ≥ a threshold
  (Open303 uses ≥ 100); slide from legato overlap.
- Apply the 1–5 ms gate → VCA delay in the envelope model (§6), not in the sequencer.

---

## 9. Output high-passes and level

### 9.1 Facts

**Low end:**

- "In the TB-303, **32 Hz was down by 5 dB**. Now [Devil Fish] it is down by 1 dB",
  caused by **C20/C21 = 0.01 µF** (S10).
- If that were one 1st-order HPF, −5 dB at 32 Hz means f_c ≈ **47 Hz** (my calc). That
  matches the RS-MET measured post-VCF HPF of **46.9 Hz** (S8) remarkably well. If it is
  two equal poles, each is ≈ 28 Hz.

**Other HPFs:**

- a1k0n: "there are several other high-pass filtering effects between the 303 filter
  output and the output amplifier … The 303 actually loses quite a lot of low-end bass
  sound" (S13).
- aciddose: "if you count every one there are **seven** highpass filters between the
  oscillator and output"; "a class-a amplifier inverts the output signal after a highpass
  filter" (S9).

**Fits:**

| Source | Output-chain fit |
|---|---|
| Open303 (S7) | post-filter HPF 24.167 Hz, allpass 14.008 Hz, RBJ notch 7.5164 Hz with bandwidth 4.7 oct; pre-filter HPF 44.486 Hz (all unexplained fits) |
| RS-MET (S8) | 60.8 Hz before the VCF, 46.9 Hz after |
| antto (S9) | "last HP (A/D convertor) at 32Hz", later 44 Hz; some of that is the recording chain |
| neotec (S9) | output HPF "varies with q from 30 → 210 Hz" |
| Stinchcombe block 3 (S3) | 3.18 Hz at the VCF output |

**Level:**

- Output impedance **10 kΩ** (owner's manual, S14). MIX IN 100 kΩ, 1:1.
- No published absolute output level (Vpp) was found. The VCO is 5.5 Vpp at the switch;
  the VCF passband gain is ≈ 1.06 (S3) before the resonance loss.

### 9.2 Recommendation

- Build the post-VCF chain (VCF output coupling, VCA input/output coupling, output stage)
  from schematic RC values as linear one-poles at the base rate.
- Check the overall response against "−5 dB at 32 Hz" (S10) and RS-MET's 46.9 Hz.
- Calibrate output level from the schematic (and a real unit if available). Provide an
  output trim so ≈ 0 dBFS corresponds to a sensible fraction of the real line level.

---

## 10. Table of numeric constants

| Quantity | Value | Source | Notes |
|---|---|---|---|
| **VCO** | | | |
| Pitch law | 1 V/oct; 2.0 V = C2 = 65.41 Hz; A = 110 Hz (TM4); scale trim TM5 (2:1, 4:1 ± 0.5 %) | S14, S11 | |
| Sequencer CV accuracy | 1.000 V ± 3 mV per octave | S14 | |
| CV range | +1…+5 V (owner's manual); 6-bit DAC, 64 notes, max ≈ 5.333 V ≈ 659 Hz | S14; aciddose S9 | |
| Measured scale error | ×0.997 (ladyada unit), ×1.027 (rv0 unit) | antto S9 | |
| Saw level | 6 → 11.5 V (5.5 Vpp), DC ≈ 9 V; rounded tip | S11 | falling ramp ("sawdown", S8) |
| Square level | 5 → 9 V (4 Vpp), DC ≈ 7 V; droops; loading-sensitive | S11, S12 | |
| Shaper input HPF | 22 k × 1 µF = 7.23 Hz | aciddose S9 | fits: 14.7 / 9.25 Hz (antto) |
| Shaper (Open303) | −tanh(69.98·saw + 4.37) (36.9 dB drive); duty 46.9 %; edge 1.43 % of period | S7; my calc | |
| Post-shaper HPF | 60.8 Hz (Schmidt), 61 Hz (antto) | S8, S9 | |
| Square PW fits | 47.5 % (real 303, F2 87 Hz), 45 % (rv0 C2), 43.5 % (ABL) | S8 | |
| antto VO(f) | 0.385 @ 17.3 Hz; 0.0077 @ 109 Hz; −0.033 @ 146.6 Hz | S9 | |
| Square/saw phase | 180° (core), 189° (VST default) | S7 | |
| **VCF** | | | |
| Ladder caps | 18 n, 33 n, 33 n, 33 n (bottom → top) | S3 deck | ratio 0.545 |
| Top diodes d | 1 (Q22 pair) | S1 | |
| Input divider | 220 k / 2.2 k ≈ 1/100 (≈ 54 mVpp saw at Q12) | S3, S11; my calc | Devil Fish overdrive ×66.6 (≈ 3.3 k) |
| Core TF | −1/(8u⁴+32u³+40u²+16u+1), u = s/ω_z, ω_z = I_f/(2V_T·33 nF) | S1 eq. 30; S5 eq. 5.27 | = 1/T₄(1+s) |
| Stinchcombe normalised | s⁴+6.727s³+14.142s²+9.514s+1 (ideal); s⁴+6.7319s³+14.1555s²+9.5205s+1 (real caps) | S1; my calc | |
| Core poles | −0.0761, −0.617, −1.383, −1.924 ×ω_z | S5 eq. 5.28 | |
| k = 0 −3 dB | 0.0747 ω_z (0.0730 real caps) | my calc | 3.24 oct below f_osc |
| Self-osc k | 17.00 (ideal), 17.02 (18/33), 18.39 (equal caps) | S5, S6, S1, my calc | |
| Self-osc frequency | ω_z/√2 (0.692/(aC) real caps); ≈ 64.5 Hz/µA | S5, S6; calc | |
| I_f vs cutoff | 25 µA ≈ 1 kHz cutoff | Stinchcombe on KVR, S9 | |
| DC gain vs k | 1/(1+k); −24.6 dB at k = 16 (Pirkle measured ≈ −26 dB) | S5, S6 | |
| Max loop gain | 18.7 = 106 × 0.98 / 5.55 | S3 | |
| Q21 output stage | gain −106, HP 92 Hz (in loop) | S3 | a1k0n: ≈ 100 Hz (C27/R113 + C25/R114) |
| Output coupling | HP 3.18 Hz | S3 | |
| Resonance path | 0.98·res·HP 1.18 Hz | S3 | VR4 50 kB dual |
| Injection network | poles 0.708, 6.164, 15.517 Hz; zeros 0.701, 7.408 Hz; HF gain 1/5.55 | S3 | |
| Input network | poles 0.708, 6.164, 15.517 Hz; zeros 0, 5.407, 17.495 Hz; HF gain 0.01 | S3 | |
| Closed-loop gain const. | 1.06 | S3 | |
| LF loop resonance | ≈ 6.5–8.7 Hz (clone oscillated at ≈ 8 Hz) | S3; my calc | |
| Self-osc onset | resonant freq ≳ 2 kHz at res max | my calc (S3 model) | |
| Resonance passband loss | −5.0 → −25.6 dB at 100 Hz (res 0 → 0.99) | my calc (S3 model) | |
| Stage slopes | −6.1/−11.9/−17.9/−24.1 dB/oct | S2 | |
| TM3 calibration | ringing period 2 ± 0.5 ms (≈ 500 Hz) at cutoff centre, res max, env mod/decay/accent min | S14 | |
| Max resonant freq | ≈ 2.5 kHz (env mod 0) | S10 | Devil Fish ≈ 5 kHz |
| AudioRealism resonant range | ≈ 200–25 000 Hz over MIDI-CV 0–127 (modified unit) | Janney S9 | |
| Open303 feedback HPF | 150 Hz ("copied from ABL") | S7, S8 | |
| Open303 b0/k fits | b0 = (0.00045522346 + 6.1922189 fx)/(1 + 12.358354 fx + 4.4156345 fx²); k = 16.998792 + 213.87126 fx + … | S7 (antto S9) | fx = (f_c/f_s)/√2, 4× OS |
| Open303 resonance skew | (1 − e^{−3r})/(1 − e^{−3}) | S7 | |
| Filter-slope claim | "varies between 18 and 24 dB" (Janney) | S9 | Stinchcombe: 24 dB asymptotic |
| **Cutoff CV / MEG** | | | |
| Cutoff vs current | 27.6 Hz/mV (across R69) + 103 Hz | a1k0n S13 | |
| Env endpoints | f_peak = e^{5.559+2.178c+1.992e} + 103; f_base = e^{5.226+1.704c−0.684e} + 103 | a1k0n S13 | c, e ∈ [0, 1] |
| Open303 nominal range | 313.815 … 2394.41 Hz | S7 | resonant-peak definition |
| Open303 env sweep | (1−c)(3.774e + 0.737) + c(4.195e + 0.864) oct; offset 0.294 + 0.0483c | S7 | |
| MEG decay | 200 ms – 2 s (normal); 200 ms fixed (accent) | S10; S7 | |
| antto MEG τ | 0.282 s … 3.85 s (pre-shaper) | S9 | |
| a1k0n MEG | reaches 10 % in (0.1 + decay_knob) s | S13 | rough |
| CV settling | 99.8 % < 1 ms; residual drift ≈ 500 ms | S10 | |
| Pots | Cutoff 50 kA, Env Mod 50 kA, Decay 50 kA (or 1 MA), Resonance dual 50 kB, Accent 50 kB, Tuning 50 kB | S10 | |
| **Accent** | | | |
| Accent → VCA | 47 k + 0.033 µF (τ = 1.55 ms) | S10; calc | |
| Accent sweep | diode + R46 47 k → gang-2 pot (100 k text / 50 k BOM) → C13 1 µF; wiper → R72 100 k | S10 | |
| Sweep τ (res CW) | charge ≈ 49–60 ms, discharge ≈ 100 ms; gain 0.40–0.51 | my calc | |
| Sweep gain (res ACW) | 100/147 of MEG minus cap voltage | S10 | |
| Open303 accent | +1 oct × accent, rc2 τ = 15 ms; VCA +4.0·accent·MEG; release 50 ms | S7 | |
| **VCA / VEG** | | | |
| VEG decay | ≈ 3–4 s, fixed | S10 | Open303 τ = 1230 ms |
| VEG resistor | R123 = 1.5 MΩ | S11/S12 | cap: see schematic |
| Note start | 1–5 ms (typ. 4 ms) delay, 3 ms attack | S10 | |
| Note end (unaccented) | 8 ms full + 8 ms linear fall ≈ 16 ms | S10 | |
| Open303 VCA extras | +0.45·MEG while gate on; 200 Hz 2-pole de-clicker | S7 | |
| **Slide** | | | |
| Slide τ | 100 k × 0.22 µF = 22 ms (audible ≈ 60 ms) | S10; Gordonjcp S9 | 44 ms if 200 k |
| CV strobe | 44 µs at each note; 8.75 µs strobes; CPU ≈ 440 kHz | mystran S9 | |
| **Sequencer** | | | |
| Clock | 24 ppqn; 8 ± 1 ms per clock at full tempo | S10, S14 | |
| Tempo | ♩ = 40–300 | S14 | |
| Gate length | 3.5 / 6 clocks = 58.3 % | S10 | Open303 50 % |
| Gate / CV out | 0 / +12 V; +1…+5 V | S14 | |
| **Output** | | | |
| LF response | 32 Hz at −5 dB (C20/C21 = 0.01 µF) | S10 | ≈ 47 Hz 1-pole equivalent |
| Post-VCF HPF fits | 46.9 Hz (RS-MET); 24.167 Hz + 14.008 Hz AP + 7.5164 Hz notch (Open303) | S8, S7 | |
| Output impedance | 10 kΩ; MIX IN 100 kΩ | S14 | |
| **DSP references** | | | |
| Open303 oversampling | 4×; 12th-order elliptic quarter-band IIR decimator | S7 | |
| Zambon/Fontana | trapezoid + Newton, 3–6 iterations, 8× OS, < −80 dB error | S19 | |
| polyBLEP | 2-point: alias-free to 2.1 kHz; 3rd-order B-spline: 7.8 kHz (at 44.1 kHz) | S22 | |
| ADAA | 1st-order ≈ 4× OS ≈ 12× plain OS (hard clipper, gain 10) | S21 | |

---

## 11. Disagreements between sources (summary)

1. **Feedback-loop HPF:** 150 Hz (Open303/ABL) vs ≈ 100 Hz (a1k0n) vs 92 Hz plus
   1.18 Hz plus a 3-pole injection network (Stinchcombe) vs 54–128 Hz (antto fits) vs
   320 Hz (neotec). Prefer Stinchcombe/a1k0n (circuit-based).
2. **Self-oscillation:** "never" (Janney) vs "mid/high cutoff only" (Whittle,
   ParadoxUn). The model says it is marginal above ≈ 2 kHz, so both are consistent with
   unit variation.
3. **Slope:** 18 dB (Roland, Janney "18–24 dB") vs 24 dB (Stinchcombe). Resolved: a
   4-pole filter with an early knee.
4. **Bottom cap:** ½ in every analysis vs 18/33 real.
5. **Nonlinearity placement:** none (Open303) / input (Pirkle) / input + feedback
   (Kernmayer) / every junction (Zavalishin, mystran, Fontana).
6. **Square shaper HPFs and duty:** 7.23 vs 14.7 vs 9.25 Hz in; 61 vs 27 Hz out; duty
   43.5–47.5 %, frequency dependent.
7. **Env sweep at env mod 0:** 0.74–0.86 oct (Open303) vs 0.33–1.09 oct (a1k0n).
8. **MEG time:** 0.2–2 s (Whittle) vs τ 0.28–3.85 s (antto) vs τ = 0.2–2 s
   (Open303).
9. **VEG:** "3–4 s" (Whittle) vs τ 1.23 s (Open303) vs 0.8 s (Kernmayer). The release
   shape is also disputed.
10. **Slide:** τ 22 ms (100 k) vs 44 ms (200 k). Open303 uses 60 ms in Hz, which is
    wrong in both τ and domain.
11. **Gate length:** 58.3 % (Whittle's clock analysis) vs 50 % (Open303, lore).
12. **Accent-sweep pot:** 100 k (Whittle text) vs dual 50 kB (Whittle parts list).
    **Decay pot:** 50 kA vs 1 MA, both on the same page.
13. **Resonance trim:** Janney says "TM4" raises resonance, but TM4 is VCO tune in the
    service notes. Unresolved.
14. **What "cutoff" means:** resonant peak (Open303, AudioRealism) vs k = 0 −3 dB vs the
    a1k0n model parameter. They differ by up to 3.24 octaves.

---

## 12. Open questions and measurements to take

These can be answered from the schematic (`private/docs/schematic/`) with SPICE, or from a real
unit:

1. **Q21 output amplifier:** large-signal transfer and its role as the resonance-loop
   limiter. Gain −106 and the 92 Hz corner at real bias.
2. **Square shaper:** a transient SPICE run of the VCO waveshaper at 33, 65, 130, 260 and
   520 Hz. Extract duty cycle and edge widths; compare with antto's VO(f) and Janney's
   duty-vs-note plot.
3. **VCO reset time and saw curvature:** expo converter, ramp linearity, flyback duration.
4. **MEG:** attack time, decay τ at the pot extremes, exact accent shorting.
   **VEG:** cap value with R123 = 1.5 MΩ; gate-off release mechanism (the 8 ms + 8 ms
   shape).
5. **VCA topology and nonlinearity:** MEG → VCA paths, and whether any MEG leaks on
   unaccented notes (Open303's 0.45·MEG).
6. **Accent sweep:** actual pot value, summing-node impedance, diode type.
7. **CV summing network and expo converter:** check a1k0n's 27.6 Hz/mV + 103 Hz and the
   env-mod minimum.
8. **Output chain:** all HPF corners after the VCF (C20/C21 etc.) and the absolute output
   level.
9. **Real-unit references:**
   - `a1k0n_303/repo/wav` and `mp3` (real 303 / x0xb0x samples);
   - the recordings named in S8 ("autodafe TB303_square_C100_R0_F2_87Hz.wav", "rv0
     SquareNoResoC2.wav"), not downloaded;
   - Schmidt's requested test set (S9, Oct 2009: saw, reso max, env mod/decay/accent
     min, cutoff at 11 panel ticks) is a good template for our own validation suite.
