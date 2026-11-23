# Oberheim Xpander: the analog model

What `core/` models per voice and in the output section, where each constant comes from, and what
was chosen or fitted. The circuit is research.md section 3-5. Marks: **[R]** read from the
schematic or parts list, **[D]** data sheet, **[C]** chosen (no source pins it), **[F]** fitted
(with what to).

The voices run at 4x the host rate and are decimated with half-band filters (`core/decimator.*`,
from the Pro-One). The CPUs run between analog samples; their S&H writes are applied at the start
of the next analog sample with their exact connection times.

## Sample-and-holds (`core/voice.cpp`)

- Each CV: C1 0.033 uF written through the 4051 (R_on 250 ohm [C: data sheet range at 17 V]),
  1M to C2 0.0068 uF; FAST also writes C2 directly; RES has no fast path [R].
- The selected S&H charges from the bus cycle of its VOICEN* write until the next CLEAR* lets it go
  [R: U806 holds the selection until CLEAR* clears it]. The charge is exact over each stretch of bus
  cycles, up to every event and to the end of each sample. The firmware may keep one selected for
  milliseconds and measure meanwhile: DAC MONO counts VCO 1 for 7.4 ms while its S&H stays
  connected (firmware.md).
- FAST is taken as connecting C2 for the whole window [C]. The schematic gates it with A0 (U807:
  FAST* = NAND(A0, U810's latched D7)), so C2 is connected only on the window's bus cycles with A0
  high: odd addresses and the $FFFF dummy cycles. The CD4051's inhibit delays (200 ns on, 130 ns
  off typical, VDD 5 V) shorten each of those cycles further. In all, about half the window.
- Pins: VCOF1/2 through 4.22K over 100R with 0.1 uF (0.02315 of the CV, 9.8 us) [R]; VCA through
  18K and 0.1 uF (1.8 ms) [R]; VCF FREQ = (VCFF/38.3K - 5 V/196K) x (38.3K // 196K // 1K) [R].
- Revision D's order (S&H 0 = PW1, 1 = VCA), as the firmware detects it.

## Oscillators (`core/cem3374.*`)

- A triangle core: the 1000 pF timing capacitor charged and discharged between 0 and V_TH = +4REF
  (3.99 V) by I = (IREF + linear FM) 2^((VCIN - I R_bulk) / 17.9 mV), IREF = 3.99 V / 806K [R, D];
  R_bulk 10 ohm, the exponential transistor's bulk emitter resistance [D: named by the CEM3340/3345
  data sheet as a cause of high-end flatness; F: to the freesound notes' pitch per octave]; limited
  to 600 uA [D: 350-550 uA maximum].
- The HF network in series with the capacitor (1.2K // 0.01 uF) solved exactly; the comparator's
  delay 0.5 us a reversal [F: voice ROM 1.4's PW tune converges only with a jump under about 2
  counts at 50 %, where the triangle overshoots V_TH for the delay; the data sheet's typical 3 %
  untrimmed HF error at 10 kHz would be 0.75 us alone, and it lumps the bulk resistance in].
- The saw from the triangle (rising: tri; falling: 2 V_TH - tri), 0 to 8 V [D, R].
- Pulse width against pitch: the comparator sees the saw shifted by a base-emitter voltage of the
  core's current, 1.15 V_T ln(I/IREF): per octave down the pulse widens by 0.26 %, so a pulse tuned
  square at the PW tune's note grows even harmonics in the bass [F: a Matrix-12's pulse run,
  Yxxwb4d8GHA 6:00: 50.00/50.27/50.55/50.78 % at C4/C3/C2/C1, its triangle symmetric at C1].
- The core's up and down currents differ by 0.3 % (charging the faster): the triangle leans, its
  even harmonics about pi/4 x 0.3 % (-52 dB) under the fundamental, falling 1/n^2 [F: a Matrix-12's
  triangle takes, Yxxwb4d8GHA: h2/h4/h6 -53/-59/-64 dB at C3, the model -53/-60/-64; chosen by ear
  over a symmetric core, round 21]. The same on every VCO; the tune doesn't see it (the period
  moves by the square of the mismatch).
- Pulse: TL082 comparators high while PW > saw/2, through the diode and 360K into the bus, its
  conductance on the bus while high [R] (a 0.6 V diode drop [C]; ngspice with a 1N4148 gives 1.75 %
  more current).
- VCO2 (the chip's A side) hard-reset by VCO1's saw drop through CX21 when SYNC lifts DX03 [R, D].
- Band-limited at the event times (cubic B-spline BLEP/BLAMP, `core/blep.*`). Each event time is
  solved to 1e-13 s (Newton inside a bracket); a solve that falls short is counted
  (`xpandercorners`, docs/improvements.md 2026-11-16).

## Mix buses and FM

- Waveform switches (4016, R_on 600 ohm [C]) and the resistor networks into 680R through 2.2 uF at
  pins 6 and 7, solved as Norton sums with the coupling capacitor [R].
- Noise: U29 clips the junction noise at its rails, so the NOISE net carries near-binary +-51 mV
  [R: "100 mV pTop"]: a random telegraph signal, flat to 6 kHz and -6 dB/octave above. Beside it a
  band of noise around 128 Hz (Q 3.3, 8.2 dB under it) and a second-order high-pass at 73 Hz on
  both: C27 and the junction's source impedance at U29's input, the uncompensated LM301's own gain
  at the top. [F: their shape fitted to a Matrix-12's noise with the filter open (Yxxwb4d8GHA
  10:58, 11:10): within 1.5 dB from 31 Hz to 6 kHz, the hump +12.4 dB at 126 Hz against +13.0.]
- FM: VCO2's triangle AC-coupled into the AD7523 (x D/256, inverted), to VCO1's IREF through 0.1 uF
  + 422K (linear) or to pin 15 through 0.1 uF + 39K (exponential) [R].

## CEM3372 (`core/cem3372.*`)

- Input VCAs: differential pairs, 2 V_T tanh, audio-taper gain into the filter at 1/100 [D]. The
  taper (e^(0.7 x) - 1) / (e^0.7 - 1), x = V / 5: 0.41 at 2.5 V [F: the factory patch VIBES on two
  units, VCO2 at 2.69 V 6 dB under VCO1 at 4.67 V; improvements.md, round 12]; below 2.7 V the same
  family, not measured. Gain 28.6 at 5 V (data sheet 28-42), so the gain at VOL 63 (4.67 V) is
  where the earlier guess (0.1 at 2.5 V, 35 at 5 V) had it [C].
- Four cells, each a one-pole lag with a tanh on its input difference; cut-off 1 kHz at 0 V with
  0.03 uF, 19 mV/octave [D]; cell 1 switched to 100 pF by FD [R]. UX14b, the CD4053B that switches
  cell 1's capacitor, is in series with it: 125 ohm [R; D: typical at 15 V]. It adds R x the cell's
  current to pin 4: a zero at 1/(R C), 39 kHz with 0.033 uF, that opens cell 1 at high cut-offs and
  leads its phase, so the filter oscillates above its cells near the top (cells at 3.9 kHz: 4.1 kHz).
  ECO 603 raised the resonance gain for "any brand of 4053 (UX14)": this is why.
- Resonance: VCF OUT (pin 17) AC-coupled into pin 11 through 20K (ECO 603) over 1.2K [R]; the
  resonance VCA is a differential pair like the input VCAs (the same symbol in the block diagram),
  comparing pin 11 with the filter's input scaled for constant passband gain, gain 0-131 for 0-5 V on
  pin 10 [C: linear in the CV; C: the compensation node scaled to the board's divider and to VCF
  OUT's gain] (115 when VCF OUT was taken at the data sheet's x100, before round 15: the loop gain,
  divider x gain x VCF OUT's gain / 100, is what was fitted, and is kept). The gain is
  set by how the filter oscillates: the owner's manual says RES 63 makes it oscillate "in some mode
  settings", and a Matrix-12 (NuBRcBlNAH8) oscillates at full resonance in the 4-pole-loop modes
  (FD low) but not in the 3-pole ones (FD high), so the loop gain at RES 63 (4.67 V) lies between 4
  and 8; where it oscillates, its swept oscillation beats against the saw's harmonics ("bubbles")
  as much as the model's does at 6.1 [F: envelope ripple 0.15 against 0.16, the 2nd and 3rd harmonics
  against the oscillation within 1-4 dB]. In the data sheet's own circuit (22K/1K) that puts the
  onset at 4.0 V, above its 2.2-3.4 V: the board's pin 11 may load the 1.2K leg; the data sheet's
  typical gain, 190, would give 10 and every mode oscillating. Without the pair, the clipped oscillation locked to the sample clock and the
  firmware's VCF tune failed at E9.
- VCF OUT (pin 17) is the cell-4 voltage x88 [F] (the data sheet's x100 [D], no tolerance given):
  the phaser modes are the dry signal minus a 3-pole all-pass (research.md 3.3), with VCF OUT
  weighted about 4 against the board's x101 cell buffers, so their low end hangs on this gain. A
  Matrix-12's four phaser clips (NuBRcBlNAH8) fit at x88: their fundamentals within -1 to +1 dB, 3-13
  dB under at x100; its 4P BP fits better too, and no other mode's shape moves. One unit (round 15).
  It makes 3P and 4P LP (VCF OUT alone) 1.1 dB quieter against the other modes, and with the loop
  gain kept the cells swing more for the same pin 17 level: the oscillation at RES 63 sits 24 cents
  lower before the firmware's VCF tune, which corrects it there (the cut-off at RES 0-62 moves up as
  much).
- VCF OUT swing limited to 10 V p-p [D].
- The chip's own noise, white at the cells' input: 50 uV rms at VCF OUT in 20 kHz with the filter
  wide open [D: 200 uV maximum; C: a quarter of it], each voice its own. It is what starts
  self-oscillation when nothing feeds the filter, and what the firmware's RES tune finds the break
  point from (RES 0-62 below it, 63 above).
- Each voice's final VCA gain differs from the typical 0.93 as on a unit: -0.30, +0.42, +0.13,
  -0.12, +0.31, -0.41 dB for voices 1-6 [F: one unit's voices, freesound 17457; nothing in the
  firmware trims them: voice 1.4's TUNE VCA, F176, only toggles the latches].
- Final VCA: linear, 0.93 at 5 V, shut off at +85 mV, -120 dB feed-through, soft limit 0.8 mA [D];
  its output current flows against its input [F: the firmware's PW tune].
- Solved per sample by Newton on the four cells, the loop and pin 17's swing limit together
  (trapezoidal, prewarped where the pole is in the band, plain bilinear above it), from a first guess
  with the last sample's secant gains. Against ngspice (tools/spice/suite.py): responses within
  0.06 dB, the cut-off law exact, self-oscillation within 0.04 cents and 0.00 % in 4P LP; 3P LP (the
  100 pF cell past Nyquist) +3.1 cents.
- Newton takes 2.4 iterations a sample on average and at most 7 over the controls' extremes
  (`xpandercorners`), of 16 allowed. A sample that doesn't converge in 16 is done again from its
  start in 2, 4, 8, then 16 shorter steps with the inputs held, and counted.

## Pole mixing and output

- The 4051's eight resistor sets from the cell buffers (+101, -101, +101, -VCF OUT after CX48)
  into UX15A (68K), through UX17's on-resistance (250 ohm, as the S&H muxes) [R, research.md 3.3];
  op-amps clip at +-11 V [C]. All 16 modes within 0.07 dB of ngspice.
- VCA input through 2.2 uF + 33K, output through 33K (ECO 602) and 2.2 uF into the pan network [R].
- Pan muxes: the eight resistor pairs into the U101 summers; channel 7 to the voice's own jack;
  MUTE from AUTO* [R]. VOLUME: 50K dual audio-taper pot [C: 10 % at mid]. 0 dBFS = 10 V at the
  outputs [C].
- U108: the six voices through 10K with 4.7K to ground (ECO 602) against 47K; 100 nA bias and 2 mV
  offset [D: typical], no hysteresis [R].

## Sleep

A voice sleeps once its VCA is shut (its S&H under the cut-off) and VOICE X has stayed under
-120 dB re 10 V for 100 ms: its oscillators and filter stop, its S&Hs and control pins run on. It
wakes when the VCA CV rises, its oscillators and filter first running 5 ms with the controls as they
are. With every voice asleep the outputs are exact zeros; both CPUs keep running.

## Not modelled

- Temperature: the CEM3374's sensor reads 2.5 V and the chips' scales are fixed.
- Component tolerances and unit spread beyond the voices' levels and the VCO core's mismatch (the
  same 0.3 % on every VCO; VCO, filter and resonance spreads, which the firmware's tune mostly
  removes); power-supply ripple, hum.
- The CEM chips' internals beyond their data sheets.
