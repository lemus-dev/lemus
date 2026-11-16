# SH-101: unit-to-unit spread

How much real SH-101s should differ from each other under their parts' tolerances, and whether
the observations of real units that disagree with our model fall inside that spread.

Tools: `tools/spread/` (run from the machine folder with `../../.venv/bin/python`).
- `run.py` draws 20 000 units and prints the table below; `--slope`, `--i0` and `--swing` run
  the scenarios.
- `measures.py` holds the closed forms. `parts.py` holds the tolerances, each with its source.
- `spice_check.py` checks the closed forms against SPICE of the transcribed circuit, on the same
  random units.
- `clip_sweep.py` re-measures the one real-unit clip of the ENV sweeping a self-oscillating
  filter.

Outputs are in `private/reference/spread/`.

## Method

- **Tolerances** (each one's source is in `parts.py`), drawn uniformly within the band, which is
  conservative:
  - resistors ±5 % carbon film; electrolytics ±20 %; film ±10 %; the 240 pF and 1 nF ±5 %;
  - pot tracks ±20 %, the A taper's mid-travel point 10–20 %, and the pots' residual at 0;
  - 2SC1815GR / 2SA1015GR hFE 200–400; V_BE and diode V_F scatter;
  - +15 V ±2 %, +14 V ±3 %, the bus 4.93–5.0 V after the D/A trim;
  - the room at 15–35 °C, with trims done at 25 °C;
  - the CEM3340's ramp ±3 % and its pin-5 full-scale 4.6–5.4 V;
  - the BA662A's offset 0.03–0.25 mV;
  - the IR3109: its OTAs matched to ±5 % (OML), its expo slope 17.5–20.8 mV/oct at 25 °C (AS3109
    data sheet to OML's measurement), and **I0 log-uniform over ×/÷2 about the AS3109's 2.4 µA**.
    That last one is an assumption: there are no data for the original chip.
  - TH1 is taken as 1 k at 25 °C with +3500 ppm/K. In this divider its sign hardly matters: ±10 K
    moves the ratio by 0.2 %.
- **Trims** are redone on every unit as the service notes do. VR8 (VCF WIDTH) is set for
  1 oct/oct by the F4/F5 procedure, and the result is recorded when it runs out of travel. The
  pulse-width trim makes PW 0 a square. The VCO trims make 1 V/oct exact.
- **Frequencies** are the VCF's pole frequency (the geometric mean of the four stages) and the
  self-oscillation frequency (where the four stages' phase reaches 180°). With the stages matched
  to 5 %, the two agree to within 1 %.

**The closed forms against SPICE** (`spice_check.py`, 30 random units at 25 °C, closed form minus
`cv.cir` / `env.cir`):

| Quantity | Mean | Max |
|---|---|---|
| Pin 9 at FREQ 0 and 10 | −0.43 mV | 0.88 mV (a common offset: the span is exact) |
| ENV-10 step at pin 9 | −0.3 cents | 0.4 cents |
| KYBD F4→F5 with the unit's VR8 | 0.0 cents | 2.3 cents |
| ENV peak | −0.1 mV | 6 mV |
| Attack time at 10 | 0.0 % | 0.2 % |

The remaining quantities come straight from the transcriptions' formulas:
- LFO rate, portamento τ, TUNE, the MOD depths, pulse width, the mixer levels, and the output
  level;
- self-oscillation onset, from the loop gain in `circuit-filter.md` 3.2.

## The spread (20 000 units)

| Measure | Nominal | Median | 5–95 % |
|---|---|---|---|
| VR8 after the trim | 27.8 k | 26.5 k | 12.5–42.8 k (4.8 % of units run out of VR8's 47 k) |
| KYBD tracking when played at 15–35 °C | 1.00 | 1.00 | 0.97–1.03 oct/oct |
| FREQ span, 0 → 10 (KYBD 0) | 13.5 oct | 13.4 | 11.8–15.2 oct |
| FREQ: octaves in the top division over the middle one | 1.39 | 1.39 | 1.31–1.47 (the loaded B pot) |
| FREQ 0, KYBD 0 | 1.05 Hz | 1.15 | 0.44–2.9 Hz (I0 dominates) |
| FREQ 10, KYBD 0 | 12.2 kHz | 12.8 | 5.1–33 kHz (I0 dominates) |
| ENV peak above the bus | 5.38 V | 5.40 | 4.88–5.86 V (SUSTAIN's track, R158) |
| ENV depth at ENV 10 | 10.4 oct | 10.3 | 8.8–12.0 oct (lowest unit 7.4) |
| Attack at 10 / 0 | 3.70 s / 1.74 ms | 3.64 s / 1.96 ms | 2.65–4.89 s / 1.17–3.0 ms |
| Decay τ at 10 / 0 | 4.70 s / 1.65 ms | 4.64 s / 1.96 ms | 3.48–6.06 s / 1.0–3.1 ms |
| Release τ at 10 / 0 | 4.70 s / 2.19 ms | 4.63 s / 2.50 ms | 3.48–6.06 s / 1.5–3.7 ms |
| RES loop gain at 10 | 5.13 | 5.13 | 4.69–5.61 |
| Self-oscillation onset, RES travel | 0.780 | 0.782 | 0.715–0.855 |
| Self-oscillation level re the saw | 0 dB | 0 | ±0.74 dB |
| Portamento τ at 10 | 0.660 s | 0.658 | 0.53–0.80 s |
| LFO rate at 0 / 5 / 10 (R31, R35 100k; rerun 2026-11-13) | 0.081 / 5.07 / 37.0 Hz | 0.156 / 5.06 / 37.0 | 0.087–0.23 / 3.6–6.7 / 33.2–41.5 Hz |
| TUNE range | ±64 cents | ±63 | ±59–68 cents |
| VCO MOD at 10 | ±29.4 st | ±29.4 | ±27.3–31.6 st |
| VCF MOD at 10 (CV; the cutoff stops at the range's ends) | ±7.6 oct | ±7.6 | ±6.7–8.7 oct |
| Pulse width at PW 10 (MAN) | 0 % | 0 % | 0–4.5 % (34 % of units leave a pulse over 0.5 %) |
| Pulse / sub re the saw | +2.0 / −0.1 dB | +2.0 / −0.1 | +1.2…+2.7 / −1.0…+0.7 dB |
| Output at VOLUME 10 | 0 dB | −0.1 | −1.9…+1.7 dB |

**Knife-edges.**
- **Pulse width at 10.** VR4 trims PW 0 to a square, so PW 10 sits at 1.008 × the CEM3340's full
  scale, just past it, on the nominal unit. A PULSE WIDTH track or R178 a few percent off leaves a
  thin pulse instead of silence: a third of units, up to 7 %. Roland's 3.9 % is a perfectly
  ordinary unit (92nd percentile).
- **The KYBD trim** runs out of VR8's travel on 4.8 % of units. These are the ones with a
  low-slope chip, which need more than 47 k.
- **Self-oscillation at RES 10** happens on every unit. The loop gain is 4.7–5.6 against the
  four stages' 4.0, so it's not a knife-edge.

## The real-unit clip, re-measured

> **Correction (after this study).** The online-references check read the clip's panel at 1080p:
> RANGE 16', TRANSPOSE L and the top key, so the key CV is 3.0 V, not the 2.0 V of C4 at
> TRANSPOSE M assumed below. With the unit's own KYBD tracking (1.024 oct/oct), the 31.9 Hz rest
> puts its FREQ 0 at KYBD 0 near 3.8 Hz. With the slope measured on an original IR3109
> (20.8 mV/oct) that needs I0 ≈ 6 µA, about twice the decode's 2.96 µA and inside the I0 range
> below, not 9–33 µA. The ENV depth verdict stands. `model.md` ("Calibrations") has the Real unit
> calibration that follows: 20.8 mV/oct, I0 6 µA, and the ENV's weight scaled by 0.77.

`real.py` read the clip `friendly-noise … selfosc_vcf_env_sweep_res_max_adsr_max` (RES 10, FREQ 0,
KYBD 10, ENV 10, A/D/S/R 10, C4) as 51 Hz → 4.86 kHz, 6.6 octaves. Both ends of that are its
pitch tracker's window (30 Hz–5 kHz, 2nd and 98th percentiles), so it is not the sweep.

`clip_sweep.py` follows the self-oscillation as the largest spectral peak off the steady C4 that
the clip also carries.
- **Plateau:** 4.84 kHz from 3.35 s to 7.19 s. That is the ENV at its peak, with S 10.
- **Release:** an exponential in octaves, fitted to 9.8 s of it with a 23-cent RMS residual. The
  rest is 31.9 Hz (FREQ 0, KYBD 10, C4) and τ_R is 4.15 s. The VCA is in ENV mode, so the rest is
  never heard on its own; the release is the only way to it.
- **ENV depth:** 4.84 kHz over 31.9 Hz = **7.24 octaves**.
- **Attack:** 3.24 s to the plateau. Its curvature puts the charging target at k = 2.05 ×
  the ENV's peak (schematic 1.82), i.e. a peak 4.82 V above the bus. That is weakly determined:
  k = 1.82 and 2.35 fit within 10 cents of it. Another unit, measured with a meter, peaks
  5.26 V above the bus, 2 % under the drawing (`online-references.md` §5). So the curvature
  probably reflects the expo near the rest rather than the ENV's peak.

The slider positions are read off video; the online-references work is checking them.

## Verdicts

| Observation | Where it falls | If outside, what would have to be off |
|---|---|---|
| The clip's ENV depth, 7.24 oct (re-measured) | below every unit (lowest 7.4, 5th %ile 8.8) | ENV→pin 9 against the expo slope, 25 % weaker. With this unit's other parts, the slope would have to be 24–31 mV/oct (median 27), against 17.5–20.8 assumed; or the ENV's peak about 4.4 V (VR24 or R158 30 % off), or R189 about 130 k. Real boards rule out the last two: R189 100k, R193 150k and R158 82k (a photo and Din Sync's BOM), and one unit's peak measured 5.26 V |
| The clip's ENV depth, 6.6 oct (as real.py measured it) | outside | a tracker artefact (above) |
| Roland's ENV depth, 7.65 oct | below every unit | the same, a little less |
| The clip's plateau, 4.84 kHz | inside (38th %ile) | — |
| The clip's rest, 31.9 Hz | above every unit | I0 of 9–33 µA (median 16.5) instead of 1.2–4.8. I0 alone can't fix both ends, since the depth needs the slope |
| The clip's ENV peak, 4.82 V | 2nd %ile | SUSTAIN's track at −20 % or R158 +20 %, both at the band's edge. Another unit measures 5.26 V |
| The clip's release τ 4.15 s / attack 3.24 s | inside (26th / 28th %ile) | — |
| Reid's ≈3 s maximum attack | inside (16th) | — |
| The spec's 4 s attack | inside (69th) | — |
| The real unit's portamento τ, 0.78 s | inside (91st) | — |
| The spec's 10 Hz at FREQ 0 | above every unit | I0 or the slope, as for the clip's rest |
| The spec's 20 kHz at FREQ 10 | inside (78th) | — |
| The spec's 10 Hz–20 kHz as a span (11 oct) | 0.3th %ile | a slope of about 23 mV/oct |
| Roland's FREQ span, 12.2 oct | inside (11th) | — |
| Roland's FREQ 10, 15.6 kHz / FREQ 0, 3.4 Hz | inside (63rd) / 98th | — |
| Roland's straight FREQ law | outside | the FREQ pot's loading by R186 is structural: every unit bends 1.31–1.47 |
| The spec's LFO 0.1 Hz / 30 Hz | inside (12th) / below every unit | — (R31 and R35 at 47k put 30 Hz at the 35th percentile; real boards have 100k) |
| A real unit's 38.2 Hz at 10 (Perfect Circuit) | inside (65th) | — |
| Roland's LFO 5.9 Hz at mid / 39 Hz at 10 | 77th / 76th | — |
| The spec's TUNE ±50 cents | below every unit (59–68) | R90 6.0 M instead of 4.7 M: a round number in the spec, or a misread |
| Roland's VCO MOD ±26 st | at the low edge: 7 % under ours, measured the same way on both renders | — (with the service notes' 47k for R188 it was above every unit; real boards have 100k) |
| Roland's pulse width 3.9 % at 10 | inside (92nd) | — |
| Roland's pulse +1.9 dB / sub −1.6 dB re the saw | inside (44th) / 0.2th | the sub's source about 16 % lower than modelled. The 4013's supply is uncertain (`circuit-osc.md` §8) |
| Roland's self-oscillation onset, 8.2 | inside (80th) | — |

### One set of values that fits the real unit

`run.py --slope 22,25 --i0 6,14 --swing 0.9`: the expo at 22–25 mV/oct, I0 6–14 µA, and the ENV's
peak 10 % low.

Every real-unit and spec observation then falls inside the 5–95 % spread:
- the clip's depth (32nd %ile), rest (15th), plateau (8th), ENV peak (45th), attack (58th) and
  release;
- Reid's attack;
- the spec's 10 Hz (33rd), 20 kHz (35th) and 11-octave span (52nd);
- Roland's ENV depth (55th), span (94th) and FREQ 10 (16th).

Only Roland's FREQ 0 (3.4 Hz) stays out: the units now start at 6–22 Hz.

The cost is the KYBD trim. At that slope, VR8 lands at 0–15 k (median 3.4 k) of its 47 k, and on
31 % of units it bottoms out before 1 oct/oct (tracking down to 0.93). A designer centres a trim,
and VR8 sits mid-travel (27 k) at 19–20 mV/oct. So the higher slope implies one of two things:
- the KYBD path has 10–25 % more gain than transcribed (R193 near 120–130 k instead of 150 k, or
  more KCV into VR8);
- or real units are often short of 1 oct/oct at KYBD 10.

One clip (tonelab) tracks at about 1.00 over 2.2 octaves, but it doesn't measure the slope.

## What it means for the calibration

- **Inside the spread:** the attack, decay and release times, the portamento, the LFO's range
  (a real unit's 38.2 Hz at 10; the spec's 30 Hz is under every unit with R31 and R35 at 100k),
  self-oscillation and its onset, the levels and pulse width. A calibration can move these within
  tolerance without stretching anything.
- **Outside, with the data-sheet expo:** the cutoff law. Three independent sources agree that our
  pin-9 scale is too strong for the slope we assume: the real clip's depth and rest, and the
  spec's span (Roland's depth too). One consistent set of values (slope ≈ 23–24 mV/oct, I0 ≈ 9 µA,
  ENV peak ≈ 4.8–4.9 V) fits all of them, and puts FREQ 0 near 10–13 Hz and FREQ 10 near
  20 kHz.
- **The unresolved point** is the KYBD path's gain. Either it is larger than transcribed, or the
  real units are trimmed at VR8's end. A hardware-faithful calibration should take the cutoff
  numbers above and keep KYBD at 1 oct/oct. It should say that this needs the KYBD path's gain
  about 10 % above the transcription's (equivalent to R193 ≈ 135 k), until a photo or a
  measurement of the board settles R193.
- **Settled since (2026-10-09):** a photo of a real control board reads R193 150k, R189 100k and
  R192 33k, and Din Sync's BOM (from a dismantled unit) agrees. The KYBD and ENV paths are as
  transcribed, and one unit's ENV peak measures 2 % under the drawing. What's left is the filter
  chip's law away from the middle of its range (near the rest, or at the top): the slope alone
  can't take the depth under 8.1 octaves while VR8 keeps KYBD at 1 oct/oct.
