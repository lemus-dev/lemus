# MS-20: the running model

How the transcribed circuit (`docs/circuit/`) runs in the plugin, what was decided on the way, and
the constants that are not part values. The numbers behind each block are on its circuit page.

## The board

`core/board.h` runs the netlists of `core/netlists/` on the shared solver (`core/circuit.h`, the
909's by way of the Minimoog) and wires them by the interface names of `docs/circuit/README.md`. A
block's input that is a patch-panel jack takes a cable's signal when one is in, else its normal;
a normal that goes through parts (the triggers' R80, the MVCA's R20, KLM-128's R1/R2 for EG 1 to
EXT FREQ) is in the netlists, switched by the `jack_<name>` controls. A cable patched backwards
(a later block's output into an earlier block) reads that block's last sample: one sample of cable.

Only the blocks that something needs run: SIGNAL OUT, the VCOs' charge currents and the noise
pull in what feeds them; the S&H, the MVCA and the ESP run once a cable takes one of their outputs.

| Group | Rate | Blocks |
|---|---|---|
| control | the host's | kbd and trig at 2x (the change detector's first pulse is under a sample at 48 kHz: `keyboard.md`), eg2, eg1, mg, mvca, noise, sh, esp, espenv, espfv at 4x on the sparse Newton (`esp.md`), vcoctl |
| audio | 4x the host's | the two VCO cores and shapers (C++), mixer, hpf and lpf (linear mode's tolerance 1 µV: `filters.md`; the solver's holds, below), vca |

The audio path is decimated to the host's rate by half-band FIRs (`core/oversampler.h`, two
stages). 4x: the KORG35's resonance at 2.5 kHz is 1.5 cents flat there against 5 at 2x and 15 at
1x, and its limiter's odd harmonics alias at 1x (`filters.md`, "Oversampling").

A sample whose Newton solve fails is redone in quarters (then 16ths, 64ths), and the next 32
samples too. The two KORG35 blocks also hold (`Circuit::setHold`): a second failure within 256
samples keeps them in quarters until a full step agrees with the quarters to 1 mV. At CUTOFF
9.5-10 with PEAK 10 the self-oscillation (near 37 kHz) is faster than full steps at 4x follow:
alternating them with quarters made it white noise (`improvements.md`, 2026-11-16). Every
block's solver counts what it did (`core/solver_stats.h`); `ms20corners` checks the extremes.

## The VCOs

The PUT latch that resets each core switches in nanoseconds; the solver can't step through it and
the reset needs MHz rates (`vco.md` §6). So `vcoctl.cir` works out the two charge currents at the
host's rate, and `core/vco.h` runs the cores at the audio rate:

- the ramp falls at I/C from the top (13.523 V) to the trip (7.507 V); after the dead time (VCO 1
  0.45 µs, VCO 2 0.27 µs, the ngspice runs with the parasitics, `vco.md` §6) it is back at the
  top, as a band-limited step (polyBLEP);
- the buffer (gain 0.906) and C1/C12's 1.6 Hz coupling give node 10/11;
- the shapers are the circuit's transfers: IC2B's rectifier, the 4558 comparators slewing at
  1 V/µs between ±14 V, the 4011's XOR at its threshold (its steps band-limited), each source's
  Thevenin equivalent into the switch common that VCO LEVEL loads (`vco.h` has every value and
  where it comes from). The commons go to `mixer.cir`.

The cores' ramps go back to `vcoctl.cir` (the cascodes' drain voltage), one sample late.

## Power, sleep, levels

- A plugin load (or a session load) is not a power-on: the board starts at its operating point.
  C6 (portamento) starts at 0 V, not at the operating point's leakage equilibrium (`keyboard.md`);
  the MG gets 10 µA for 1 ms into its summing node, as a real one starts from noise
  (`envelopes.md`).
- POWER (VOLUME's POWER OFF click) is a switch: on, every capacitor of the running blocks starts at
  0 V and the rails rise (every constant source and op-amp swing scaled with them,
  `Circuit::setConstantScale`), the trigger's Schmitt (IC13B) from its no-key side; off, the rails
  fall, the board stops below 4.5 V and SIGNAL OUT dies as C33 empties, then exact zeros. How the
  rails move is an estimate (`core/machine.cpp`, "POWER"; `improvements.md`, Behaviour and
  features). The blocks the plugin skips keep their state; the VCO cores (C++) start at rest, their
  shapers' outputs scaled with the rails.
- Sleep: no key down and SIGNAL OUT under -120 dB for 100 ms. After a note, the VCA's step comes
  through SIGNAL OUT's coupling as a few microvolts for about 1.5 s; the machine sleeps then.
  While asleep everything stops; it wakes on a key, a knob, a cable or sound at the input, and
  takes up where it stopped (the free-running VCOs and MG at another phase).
- SIGNAL OUT: 0 dBFS = 1 V peak, the specification's 2 Vpp maximum [chosen until a level
  reference exists]. The plugin's input is taken at the same scale.
- TR3's junction noise: white, 4.6 µV/√Hz into `noise.cir` with rz3 1k (since 2026-11-24): VCO 1's
  WHITE against its saw as one real unit's recording has them (`compare.md` §2); J25 gives 2.1 Vpp
  (6 sigma) against the specification's 5 Vpp, NOISE LEVEL (VR3) untrimmed in §7.

## Calibration

`tools/service` (ms20service) runs the adjustment procedure (§7) on the whole model: KBD CV high
and low (VR4, VR3), each VCO's high and low (VR1/VR2, VR6/VR5), the filters' FC ADJ (VR7, VR8).
`ms20service calibrate` sets them as the procedure does; the results are `Machine::Calibration`.
