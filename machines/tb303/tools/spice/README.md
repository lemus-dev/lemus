# SPICE checks of the TB-303 voice

Each block of the voice is simulated in ngspice from the transcribed schematic (`docs/circuit.md`)
and run against the same block of the model (`tb303blocks`, which builds `core/voice.cpp` into
itself to reach the model's internals) with the same input. Results and what they changed are
in `docs/model.md`, "Validation / SPICE".

| Netlist | Block | Comparison |
|---|---|---|
| `slide.cir` | pitch DAC (R-2R, 5.333 V termination), 4066 switches, C35 | `compare_slide.py` |
| `vco.cir` | saw core and reset, Q28 buffer, Q8 square shaper, the VCF input as load | `compare_vco.py` |
| `vcf.cir` | input network, ladder, Q21 output amp, Q19, the resonance loop | `compare_vcf.py` (`--only ac,ring,signal`) |
| `env.cir` | gate conditioning, one-shot, MEG, accent, accent sweep, cutoff converter, VEG, the BA662's I_ABC input | `compare_env.py` (`--scenarios ...`) |
| `out.cir` | VCA input branches, BA662, output load, VOLUME, mixer, OUTPUT jack | `compare_out.py` (`--ba662 chip,ideal --spread`) |
| `ba662.lib` | the BA662 at transistor level, and the ideal OTA the model's law started from | used by `env.cir` and `out.cir` |
| all four joined | the whole voice: the blocks' netlists joined into one circuit, the gate and accent driven as notes | `compare_voice.py` (`--scenarios ... --keep DIR`) |

Run from the machine folder after building `tb303blocks` and `tb303render`, with the repo's
Python environment and ngspice (`brew install ngspice`):

    ../../.venv/bin/python tools/spice/compare_vcf.py --models datasheet,ds-low,ds-high

**Device models.** `models-datasheet.lib` sets each part from its data sheet at typical values
(NEC 2SA733 and 2SC945, Toshiba 2SK30A and Mitsubishi 2SC1583 sheets, in `private/docs/datasheets`;
the gain ranks are in `docs/circuit.md`, "Gain ranks"). The matched duals (Q12, Q21, Q22) have
their own model, `Q2SC1583`, at the F rank's centre and ends. `models-datasheet-low.lib` and `-high.lib` are
the same at the two ends of the parts' spread: gain 135 / 400, V_BE 0.68 / 0.58 V at 1 mA, and the
JFETs' I_DSS. A second, independent set (`private/reference/spice/models-community.lib`, published
models of unclear licence) is used where it works: its 2SA733 has implausible parameters and
breaks the saw core's reset. A result counts only if it holds across the sets.

**The BA662** has no data sheet or published model. `ba662.lib` builds it from Open Music Labs'
reverse engineering of an original chip (control-current mirror, differential pair, Wilson
mirrors, a complementary buffer biased from pin 4) with the resistor values of Alfa's AS662
clone and generic IC transistors; the sources are listed in `docs/circuit.md` 6.1, copies in
`private/reference/ba662/`. Its gm at 500 µA, 8430 µS, falls in the AS662's published 7700-9100.
`.param ba_bfn ba_bfp ba_is ba_re ba_rpu` move its unknowns; `compare_out.py --spread` runs both
ends.

Notes: SPICE reads "M" as milli; write "Meg". `wrdata` with `wr_singlescale` writes the scale
once, then each vector.
