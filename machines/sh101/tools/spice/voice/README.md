# The whole voice in SPICE

`voice.cir` joins the SH-101's block netlists into one circuit, from the VCO's outputs to the OUTPUT
jack, so the blocks load and drive each other as on the board:
- the saw, pulse (TR24 and the PWM network), sub (the 4013's outputs, R181/R182, D38/D39, S5) and
  noise generator, each through its slider and mixing resistor into the mixer node, and C45;
- the IR3109 at transistor level (`../filter/ir3109/ir3109t.lib`'s OTAs and buffers), with its expo
  converter driving the OTAs' tails from IC18b's summer and the pin-9 divider;
- the resonance loop and the output sum into C53 and the BA662 (`../filter/ba662.lib`);
- the ENV GEN at transistor level on the same gate, feeding VR19, the VCA (S6 ENV or GATE) and PWM;
- VOLUME, IC1a, C2 and jack board A into 47k.

Only the VCO core is not simulated. `compare_voice.py` drives the saw and the 4013's outputs from
the model's own VCO, its frequency sample by sample from its reset, so both sides play the same
phase. The CEM3340's pulse comparator is a source on SPICE's saw and pin 5.

`blocks.cpp` (target `sh101voiceblocks`) runs the model's `Voice` the same way, with the CPU lines
set directly.

    cmake --build <dir> --target sh101voiceblocks
    ../../.venv/bin/python tools/spice/voice/compare_voice.py [--only NAME,...] [--group osc,noise,vcf,env]
           [--rbh OHMS] [--keep DIR] [--jobs N]

It runs from the machine folder. The 43 cases take about four minutes on ten cores. The results
are in `docs/model.md`, "The schematic, in SPICE".

## Setup

Both sides use the service-notes values. Before the cases, `compare_voice.py` sets three things
in SPICE's own circuit:
- **The expo:** its reference current for the model's 2.4 µA at pin 9 = bus, and its junction
  temperature for 19 mV/oct (44.9 °C).
- **VR4:** trimmed for a square at PULSE WIDTH 0, as the service notes do (SN p.6 (D)).
- **Noise:** the noise generator's current for the model's 1.05 V RMS at IC12a.

VR8 takes the model's trim.

## Things SPICE needs that the circuit doesn't

- **C45's start:** the operating point holds the mixer node at the sources' mean (`.ic v(mix)`), so
  C45 starts settled, as in a unit that has been on a while. The model settles it the same way.
- **The pulse comparator** switches over 20 mV, and the 4013's edges come 300 ns after the saw's
  drop and take 200 ns. Steeper edges, or edges on the same time step, stall SPICE's time step.
- **Floating nodes:** D38/D39's joined cathodes get a stray 10 pF and 10 MΩ for when S5 leaves them
  floating. With the SUB slider at 0 the 4013 is held still: VR15's wiper is grounded, so it can't
  reach the mixer.
- **Pre-roll:** cases whose filter rings slowly (RES 5 and more, or FREQ 3) run 0.3 s before
  t = 0, and 3 s at FREQ 3 with RES 7.5 or more, on both sides.
- **Self-oscillation:** it gets one small current kick into the first stage where the model has its
  circuit noise.

`--rbh` sets the transistor-level IR3109's tail-mirror helper bias, 10 MΩ by default. Its base
current comes out of the expo's current. The decode's first guess, 100k, drew about 33 nA and bent
the cutoff below about 1 µA; real chips stay exponential down to a few nanoamps (Open Music Labs)
and a Juno-106's IR3109 down to 5.7 Hz (`docs/model-filter.md` 2), so the helper is biased
lightly.
