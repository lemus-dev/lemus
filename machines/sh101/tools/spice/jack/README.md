# SPICE check of jack board A's mute

`mute.cir` is jack board A (OPH178-2) as drawn, driven through C2 by IC1a's output: R30, R29, C12,
R28 into a 47k load, the power-on mute (TR7, R35, D4, D3, R36, C13 from +9 V) and the power-off
mute (TR8, R37, D5, C14), with the project's 2SC1815-GR, 2SA1015-GR and 1S2473 models
(`../filter/models.lib`). `blocks.cpp` (target `sh101jackblocks`) runs the model's `JackBoard`
(`core/jack_board.*`) on the same input.

    cmake --build <dir> --target sh101jackblocks
    ../../.venv/bin/python tools/spice/jack/compare.py [--only on,clamp,off]

It runs from the machine folder, in about 20 s.
- `on`: switched on with a 1 V peak sine from IC1a; the jack's peaks over the first 3 s.
- `clamp`: switched on long before; the peaks from 0.5 to 3 V, where TR7's base-collector path
  limits the negative half.
- `off`: switched off with a 1 V peak sine playing (`poweroff.cir`): +9 V falling with
  `JackBoard`'s 12 ms, IC1a under +9 V less 1.2 V and above ground, the sine stopping below 6.9 V;
  the jack's peak and largest difference in five windows over 0.3 s.

The results are in `docs/model-filter.md` 5. SPICE's supply ramps over 10 µs, so its first
period after switch-on has a sub-2 µs edge the model, a sample at a time, doesn't have.
