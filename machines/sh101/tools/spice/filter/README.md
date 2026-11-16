# SPICE checks of the SH-101 filter section

Each block of the filter section is simulated in ngspice from the transcribed circuit
(`docs/circuit-filter.md`) and run against the same block of the model (`sh101filterblocks`,
built from `blocks.cpp`) with the same input. The results are in `docs/model-filter.md` 7.1.

| Netlist | Block | Comparison |
|---|---|---|
| `vcf.cir` | mixer input and C45, IR3109 with its networks, resonance loop (VR18, TR26, TR27, C52, R129, D30/D31, R113), output sum into C53/R139 | `compare_vcf.py --only ac,osc,onset,saw` |
| `cv.cir` | IC18b's summer (FREQ from +15 V, R187, ENV, MOD, KYBD with VR8/VR21/TR42, bend), the pin-9 divider | `compare_cv.py` |
| `env.cir` | the ENV GEN at transistor level (TR29–TR37, IC16 as IR9022s, R162 placed as in `circuit-filter.md` 5.4), the GATE level (TR30, R142, R41, C57) | `compare_env.py` (`--only mid,fast,slow,sustain10,sustain0,trig,lfo,gatevca`) |
| `vca.cir` | TR28 in common base, the BA662 (`ba662.lib`), R140, VOLUME, IC1a, C2, jack board A | `compare_vca.py --only ctl,gain,thd,out` |
| `models.lib` | 2SC1815GR, 2SA1015GR, 1S2473, the IR3109 (behavioural, the model's decode), the IR9022 (behavioural, from its data sheet) | |
| `ba662.lib` | the BA662 at transistor level (from the TB-303) | |

Run from the machine folder after building `sh101filterblocks` (`cmake --build <dir> --target
sh101filterblocks`), with the repo's Python environment and ngspice (`brew install ngspice`):

    ../../.venv/bin/python tools/spice/filter/compare_vcf.py
    ../../.venv/bin/python tools/spice/filter/compare_cv.py
    ../../.venv/bin/python tools/spice/filter/compare_env.py
    ../../.venv/bin/python tools/spice/filter/compare_vca.py

`--keep DIR` (VCF, ENV) keeps the netlists and the raw data. The scripts find `sh101filterblocks`
in `$SH101_BUILD` (relative to the repo), `build/sh101` or `build/cmake`.

`sh101filterblocks` also runs the service-note checks: `kybd-track` (the VR8 procedure), `env`
(times), `voice-level` (the output level with the oscillator section), `vcf-bench` (the solver's
iterations).

Notes: SPICE reads "M" as milli, so write "Meg". With `wr_singlescale`, `wrdata` writes the scale
once and then each vector.

## The IR3109 at transistor level (`ir3109/`)

`ir3109/ir3109t.lib` builds the IR3109 at transistor level from the published decodes:
- Open Music Labs' reverse-engineered schematic and sketches;
- Roland's 1982 data-sheet excerpt;
- the AS3109 data sheet's Fig. 2.

All of these are in `private/docs/papers/ota_ir3109`. The subcircuits are:

| Subcircuit | What |
|---|---|
| `IR3109T` | Four OTAs (bare NPN pairs, the internal 560 Ω on their non-inverting side, Wilson mirrors), P-MOS source-follower buffers (20k pull-ups on stages 1/3, current sources from a 22k reference on 2/4). I_ABC is a parameter, as in the behavioural chip |
| `IR3109TI` | The same OTAs with the behavioural (ideal) buffers, to separate the OTAs' and the buffers' shares of a difference |
| `IR3109EXPO` | The expo converter alone: a 3 µA reference held by an op-amp through one transistor of the pair, pin 9 on the other's base |

The device models are generic integrated ones, because no IR3109 device data exists:
- NPN: BF 150, VAF 80.
- Lateral PNP: BF 40, VAF 40.
- P-MOS: level 1, |V_TO| 2.3 V, K 1 mA/V², which matches OML's measured |V_GS| of 2.9–3 V.

`ir3109/compare_ir3109.py` swaps the chip into `vcf.cir` (both at 25 °C) and runs it against the
model (`sh101filterblocks`). It covers the operating point, the buffers' tops and floors, the AC
response, self-oscillation, onset, a 0–10 V saw (waveform, harmonics, and into the self-oscillating
filter) and the expo law:

    ../../.venv/bin/python tools/spice/filter/ir3109/compare_ir3109.py [--only dc,limits,ac,osc,onset,saw,harm,expo] [--chip IR3109T|IR3109TI|IR3109] [--iscale X]

`--iscale` scales the model's I_ABC, to separate a uniform cutoff offset from differences of
shape.

The saw starts at its mean voltage. A 0 V start leaves C45 5 V away from its mean, and its 1–8 s
recovery is a slow DC transient into the filter that the model (which settles C45 at start-up)
doesn't have. The runs last 0.6 s, so the model's 0.2 s start-up settling is over before the
compared window. `compare_vcf.py`'s saw test does neither, so with today's start-up settling its
self-oscillating cases read 1–2 dB apart.
