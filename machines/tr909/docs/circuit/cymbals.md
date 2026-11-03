# Crash and ride (cr.cir, rd.cir, crclk.cir, rdclk.cir; core/cymbals.*)

The CRASH and RIDE of the voicing board 73133090: each a clock oscillator with TUNE, a counter that
reads its sample ROM, a latch, a 6-bit D/A, a second D/A on the counter's top bits for the
envelope, an anti-log pair and a transistor VCA, two Sallen-Key low-passes and the LEVEL stage, to
the connector W5 to the multi-jack board. Sources as `docs/circuit.md` §0: **SN** p.11 (crops
`private/docs/schematic/crops/p11-cy-crash.png`, `p11-cy-ride.png`; the full page for the analog
chains, grid B-F 11-27, and the supply legend at grid C-D 24-25), p.5 ("DIGITAL VOICE
GENERATORS"), p.3 (parts list: TC4011UBP, TC4013BP, HD14040BP, TC4520BP, HD14174BP, HN61256P-PC42
and -PC44, RM-0621), p.12 (multi-jack board); **NAVA** (`Nava_v1.02_netlist.txt`,
`Nava_v1.02_parts.csv`); **OLD**, `private/reference/old-models/cymbals.cpp`; **RES**,
`docs/research.md` §4 (B.1-B.10); **DS** `private/docs/datasheets/` (TC4011UBP, MC14011UB,
CD4011UB, M5218, 2SA798, 2SC2603). Every value below was read on the SN crops at 400 dpi and agrees
with NAVA's parts list (NAVA calls the ride's trigger capacitor C140; SN's is C40, NAVA's C40 is a
10 µF elsewhere). The crash and ride are the same circuit part for part except R432 270K / R439
470K and the jack board's mix resistors; the crash's designators come first below, the ride's in
brackets.

On SN p.11 the supply symbols are: a bar with a dot +5 V, an arrow up +15 V, an arrow down −15 V,
hatching ground (the legend at C123/C124/C129). Every bar-dot below is +5 V (NAVA's "+5V" net has
IC52.+IN, R428 and R429).

Signal order: TRIG → C50/R189 → the reset of the timing flip-flops and counters. The astable IC59c
(inverter) / IC59b (gated by NOT bit 15) gives two complementary clocks A and B; IC60b toggles on A,
IC60a on B; IC60a clocks the counter and the ROM's CE, the NAND of both Qs (IC59a) the latch IC63.
IC63 → RA10 → C127 → the 22 Ω R431, Q71 (S) to IC51b's virtual ground (I-V, R425 47K). Counter
bits 9-14 → RA11 → R432 → IC52b, which runs the anti-log pair Q70 (accent and mute on its base
network) whose output collector is Q71's base current. IC51b → two Sallen-Keys (Q75, Q76) → C132 →
IC65a with VR24 → W5.

## 1. The netlists' interface

| Netlist | Inputs | Outputs | Controls |
|---|---|---|---|
| `cr.cir`, `rd.cir` | `d0`..`d5` the latch's outputs (IC63 / IC53), `e0`..`e5` counter bits 9-14, `run` IC59d's (IC57a's) output, `rd.run` (`cr.run`) the other cymbal's, `acc` IC9's open-circuit voltage | `out` IC65a (IC64a) at W5, `jack` the MULTI OUT contact behind R524/C509 (R527/C510) | `level` VR24 (VR26) |
| `crclk.cir`, `rdclk.cir` | `trig` the TRIG line, `run` as above | `osca`, `oscb` the astable's two outputs, `rst` the reset line | `tune` VR25 (VR27) |

- **d0..d5**: d5 into RA10's R (5k, the MSB) … d0 into 32R (160k), i.e. the ROM byte's bits 7..2.
  The public images are in playback order and in D/A order (RES B.2.2: Colin Fraser's in-circuit
  capture; the pins as drawn give D2 32, D3 16, D1 8, D4 4, D0 2, D5 1 on the chip's data pins), so
  `byte >> 2` is the code. **e0..e5**: e5 = bit 14 into RA11's R … e0 = bit 9 into 32R. All are 0/5 V
  CMOS outputs, each through `rlatch` 450 Ω (HD14174B / MC14174B at 5 V: 0.88 mA typ at a 0.4 V
  drop, `interface.md` §1) into its resistor: the D/As carry the outputs' resistance, which leaves
  code 32 2.4 % under code 31 (`interface.md` §2; OLD fitted 380 Ω to the same non-monotonic step).
- **run** = NOT counter bit 15: 5 V while the counter runs. `rd.run` / `cr.run` are board signals
  (a name with a dot, `voiceboard.cpp`'s `signalName`): the ride's base network loads the shared
  accent DAC by 4.9 kΩ to 5 V while the ride runs and to 4.55 V (R433 220 to +5 V, R437 2.2K to
  0 V) while it is stopped (§5).
- **acc**: the board's 5 V · code / 63 for IC9 (RA8, address 4008), behind `rdac` 2.54 kΩ, the
  convention of `docs/circuit.md` (`interface.md` §2 recommends 2.69 kΩ with the latch's resistance;
  the change is under 0.5 % at the node).
- The netlists hold no trigger input: the trigger reaches the analog half only through the lines
  Cymbal makes (§3). `crclk.cir` holds C50/R189 and the astable; the board doesn't run it (§9).

## 2. The clock: IC59c, IC59b, TUNE (crclk.cir; SN grid E-F 11-13; ride IC57b, IC57c, grid E-F 18-20)

- **IC59c** (TC4011UBP, pins 8 and 9 tied): its input g comes from x through **R442 10K** (33K
  before SN 392600, struck through and annotated "SN392600 UP 10K" on p.11; NAVA has 10K); its output
  is **A** (pin 10, `osca`), which also feeds IC59b pin 6 and clocks IC60b (pin 11).
- **IC59b**: pin 5 is IC59d's output (the run line), pin 6 A; its output is **B** (pin 4, `oscb`),
  which clocks IC60a (pin 3).
- Timing: **VR25 10K(B)** as a rheostat (wiper tied to its A end; NAVA: VR25.A and .S on A's net)
  in series with **R443 6.8K** from A to x; **C131 470P** from x to B. The ride: IC57b (pins 5, 6
  tied, from **R440**), IC57c (pin 9 run, pin 8 A), **VR27 10K(B)**, **R441 6.8K**, **C130 470P**.
- With run low IC59b's output is held high; IC59c then settles, self-biased through R442/R443/VR25,
  at its threshold (A ≈ 2.6 V in the model). When run rises (the trigger's reset clears bit 15),
  B falls and A rises at once (0.02-0.03 µs in the model), then the clock runs at its steady period
  from the first cycle. When the counter reaches 8000 (B having just risen) run falls, B stays high
  and A drifts back to the self-bias point: no more B edges, so the counter stops there.
- **The gate model** (`models.cir`, 4011UB_N / 4011UB_P: SPICE level-1 MOSFETs, the solver's `M`
  element, `docs/circuit/solver-mosfet.md`; each gate two of each, the P in parallel, the N in
  series; DCMOS its input diodes). The TC4011UBP's sheet (Toshiba 1985) gives only
  minimum drive (−0.16 mA at 4.6 V, 0.44 mA at 0.4 V, the same as Motorola's MC14011UB), V_IL / V_IH
  2.0 / 3.0 V typ, C_IN 5 pF. The typicals of the MC14011UB (Motorola 1988: −0.36 mA at 4.6 V, −1.7 mA
  at 2.5 V, 0.88 mA at 0.4 V) and the CD4011UB (Harris/TI: −1 mA, −3.2 mA, 1 mA) differ threefold.
  A level-1 fit to Motorola's (with the threshold at 2.5 V for both inputs and 2.35 V for one)
  stops the cymbals' clock from TUNE 0.9 up (IC59b's threshold lands 0.5 V under IC59c's
  self-bias point and no longer regenerates) and runs it 26 % slow at the centre: not a TC4011UBP
  that works in a 909. The default is the same fit to the CD4011UB's typicals (VTO 2.06 / 2.04 V,
  β = KP/2·W/L 0.90 / 0.187 mA/V², LAMBDA 0.05; source −0.84 mA at 4.6 V and −3.6 mA at 2.5 V, sink 1.0 mA
  at 0.4 V), checked on the hi-hat's astable, which has fixed parts (R492 12K, R493 33K, C160 470P
  polystyrene, the same TC4011UBP): the model gives **31.80 kHz** for its sample clock against the
  **31.57 kHz** measured on two units (RES B.4): +0.7 %.
- The pins' capacitance is in the netlist: **C59G 5p** at g (C_IN typ), **C59A/C59B 15p** at A and B
  (a gate output with the next gate's input and a 4013 clock input). They make the edges finite (the
  solver and ngspice both need it) and set them: 1 pF more at every node moves the period by 1.2 %.
  The input protection diodes (DCMOS) clamp x's swing past the rails through R442.
- **Measured against the real units** (sample clock = half the astable's frequency; Hz):

| TUNE | model | AR crash | AR ride | RR crash (dial) | RR ride (dial) |
|---|---|---|---|---|---|
| 0 | 24 832 | 28 056 (−11.5 %) | 28 065 (−11.5 %) | 27 616 (−10.1 %) | 26 750 (−7.2 %) |
| 0.2 | 27 380 | 30 146 (−9.2 %) | 30 090 (−9.0 %) | 29 561 (−7.4 %) | 28 595 (−4.3 %) |
| 0.4 | 30 492 | 34 286 (−11.1 %) | 35 000 (−12.9 %) | 33 660 (−9.4 %) | 38 637 (−21 %, RR's duplicate file) |
| 0.5 | 32 320 | 36 678 (−11.9 %) | 36 830 (−12.2 %) | | |
| 0.6 | 34 368 | 38 978 (−11.8 %) | 39 820 (−13.7 %) | 41 299 (−16.8 %) | 38 637 (−11.0 %) |
| 0.8 | 39 150 | 46 624 (−16.0 %) | 48 185 (−18.7 %) | 48 479 (−19.2 %) | 46 764 (−16.3 %) |
| 1 | 43 933 | 53 839 (−18.4 %) | 56 110 (−21.7 %) | 55 769 (−21.2 %) | 55 479 (−20.8 %) |

  The model's clock is **7-22 % slow** against both units, more at the top. The period is nearly
  linear in VR25's resistance in both: the model's T = 0.876 µs/kΩ · R + 5.42 µs (R = R443 + VR25's
  part), the AR crash's T = 0.853 µs/kΩ · R + 3.49 µs taking the pot as 10k and linear: the slopes
  agree within 3 %, the fixed part is 1.9 µs a period shorter in the unit. The units' TUNE law also
  has a dead zone of about 0.07-0.08 of the rotation at each end (AR crash 0 → 0.1: +1.1 %, 0.9 →
  1: +3.6 %; the middle steps 1 µs each 0.1), a knob law the netlist leaves to the board
  (`Calibration::parts`, or a law as the 808's `knobLaws`).
- What closes the gap: the calibration (§15) fits the TC4011UBPs (their drive and a gate-drain
  capacitance, jointly with the hi-hat's astable), the pots and C130, and the knob law; the
  candidates weighed there are the gate, C131/C130 and the pots alone, R443/R441.
- Parameters for the fit (plumbing, printed values as defaults): `r443`/`r441`, `cg`, `ca`, `cb`
  (the pins' capacitance), `cgd` (C59M/C59N, C57M/C57N: the gates' gate-drain capacitance, none by
  default) in the clock netlists; `w4011n`/`w4011p` (the 4011UB's channel widths) in models.cir.
- **Production change.** R442 (R440) 33K → 10K from SN 392600: the model's clock 0.4-1.1 % faster
  with 10K (sample clock at TUNE 0 / 0.5 / 1: 24 553 / 32 185 / 43 716 Hz with 33K). Early units:
  `r442=33k` / `r440=33k` (crclk/rdclk `.param`). Not in p.7's change table; only annotated.

## 3. Reset, flip-flops, counter, ROM, latch (core/cymbals.*: Cymbal)

- **Reset**: TRIG (IC10, `rtrig` 450 Ω) → **C50 100P** → rst, **R189 100K** to ground (ride C40,
  R188). rst resets IC60a/b (pins 4, 10), the 4040 (pin 11) and the 4520 (pin 7 / 15) while it is
  above the inputs' threshold (taken as V_DD / 2): **6.92 µs** from the trigger's edge (the clock
  netlist, solver and ngspice alike). DRST stands for those inputs' protection diodes, which clamp the
  falling edge.
- **Timing flip-flops** (TC4013BP, D tied to /Q, S grounded): IC60b toggles on A rising, IC60a on B
  rising. IC60a's Q clocks the 4040 (pin 10, falling edge) and IC59a pin 1; its /Q is IC62's CE
  (pin 22; the cymbal ROMs' CE is active low, RES B.2.1, Fraser): the ROM takes its address as Q
  rises. IC60b's Q is IC59a pin 2. **IC59a** (NAND) clocks the latch IC63 (pin 9) as it rises, i.e.
  when one Q falls while both were high. Which flip-flop leads depends on which edge comes first
  after the reset lets go: from a stopped clock B's first edge (6.6 µs at TUNE 1) falls inside the
  reset, so A leads and the latch takes the ROM's output on A's edges, half a clock period before
  the count steps on B's; on a retrigger of a running clock the order is whatever the phase gives,
  as on the unit (a quarter of a sample period, hit to hit). Data is valid well within the HN61256P's
  3.5 µs access (RES B.2.1).
- **Counter**: crash IC61 (HD14040BP, bits 0-11) then IC58a (TC4520BP clocked on its EN, pin 2, by
  IC61 Q12; bits 12-15). The ride's is the other way round: **IC58b first** (EN pin 10 from IC56a Q,
  bits 0-3) then **IC55** (4040 clocked by IC58b Q4, pin 14; bits 4-15). Both put bits 9-14 on the
  envelope D/A (crash: IC61 Q10-Q12 pins 14, 15, 1 and IC58a Q1-Q3 pins 3-5; ride: IC55 Q6-Q11 pins
  2, 4, 13, 12, 14, 15; RA11/RA13's top resistor on bit 14) and bit 15 on ROM CS (pin 26) and
  IC59d (IC57a). The ripple through the counters (under a microsecond) is left out.
- **Address lines**: the counter's bits go to different ROM pins in the two voices (crash bits 0-14
  → pins 10 9 20 21 23 24 7 6 8 5 4 25 3 27 2; ride → 8 20 9 10 21 7 6 23 25 2 24 4 5 3 27). RES
  B.2.2 says the same pin sequence is drawn for both; it isn't. It doesn't matter for the model: the
  public images are in playback order for both (RES's whiteness test), so the counter's value is the
  file offset.
- **Latch** IC63 (IC53; HD14174BP): ROM D0..D5 (pins 11-13, 15-17) into its D inputs; D6/D7 unwired.
  Its /R (pin 1) is tied to +5 V: a trigger doesn't clear it; it holds the last code (ROM[7FFF] =
  0x80, code 32, at rest). A reset that drops IC60a's Q while both Qs are high makes IC59a's edge, and
  the latch takes the ROM's output once more (as HiHat).
- **The run**: 32 768 latches, then bit 15 stops it. Data ends at 7EA2 (crash) / 7FBD (ride);
  the envelope code is 63 (no current) over the last 512. At 8000 the envelope D/A returns to code 0
  (full current) while R427 mutes the VCA (§5). Played length 32768 × 2 periods: 1.32 s at TUNE 0,
  0.75 s at TUNE 1 in the model (AR: 1.17 / 0.61 s).
- **Lines out**: Cymbal places each line's step at its exact time and gives the netlist each line
  band-limited per board step: a Kaiser-windowed sinc step 16 steps long (latency 8 steps, the
  default) or the step's average (Box, latency 0.5); §9.

## 4. The D/A and the VCA (cr.cir; SN grid E-F 15-17; ride grid F 21-23)

- **RA10 (RA12), RM0621**: 5k … 160k from the six latch outputs to the node da; **C127 10µ/16**
  (+ at da) to vn, **R431 22** to ground (ride C128, R438). The D/A's AC current, ±1 mA at full scale
  behind 2.54 kΩ (+ rlatch), makes ±21 mV across R431.
- **Q71 (S)** (2SC2603, "selected"; Q69): collector on vn, emitter at IC51b's (−) (virtual ground;
  (+) grounded), base current from Q70B. Saturated, with V_CE the D/A's ±21 mV, it is a conductance
  ≈ BR · I_B / V_T; **R425 47K** (R424) is the I-V. The arrow (emitter) is on IC51b's side (crop
  zoom at grid F-16).
- The gain follows I_B linearly: the model's per-block law is ((63 − code)/63)^1.0 (§10: AR's crash
  ^1.1, its ride ^1.05, RR's crash ^1.0). BR sets the level (BR 1.5 instead of 6: −11.7 dB) but not
  the law; ISE/NE (base recombination) moves neither at these currents. The M5218's input bias current
  (500 nA max, DS) at IC52b's node would bend the law's end by under 1 dB at code 60.

## 5. The envelope D/A, the anti-log pair, accent and mute (cr.cir; SN grid C-E 15-18; ride C-E 22-25)

- **RA11 (RA13)**: bits 9-14 through `rlatch` into 5k … 160k to ea; **R432 270K** (R439 470K) to
  IC52b's (−) (IC52a). IC52b's (+) is at +5 V (bar-dot; NAVA). So the current out of the node is
  I_env = (5 V − V_ea) / (R432 + 2.5k) = **18.3 µA · (63 − code)/63** (ride **10.6 µA**): it falls in
  64 steps of 512 samples to nothing, whatever the clock (RES B.2.5).
- **Q70 (2SA798, matched PNP pair; Q68)**: emitters joined, fed from IC52b's output through **R430
  2.2K** (R436); Q70A's collector at IC52b's (−), so IC52b sets the emitters where Q70A carries
  I_env; **C126 1000P** (C125) across IC52b. Q70B's base on **R429 100** to +5 V, its collector
  into Q71's base: I_B = I_env · exp((V_B1 − V_B2) / V_T).
- **Q70A's base** (q70b1): **R428 220** to +5 V, **R426 4.7K** from the accent DAC's node, **R427
  2.2K** from IC59d's output (run). Running: V_B1 = 5 V − (5 V − V_ac) · 200/4900. Stopped: R427
  pulls it 0.55 V down, −184 dB: the mute (the envelope D/A's code 0 at 8000 can't be heard).
- **The shared accent DAC**: IC9's node ac carries the crash's R426 and the ride's R435 (each 4.9 kΩ
  to their base networks): V_ac moves 0.49 V per volt of V_oc, so the gain moves **6.7 dB per volt**
  of V_oc (0.535 dB a code; RES's 9.0 dB/V counted one cymbal's load). Model, crash, LEVEL 50 %,
  energy re code 32: code 10 −11.85 dB, 21 −5.91, 47 +7.94. **The other cymbal's state moves it:**
  with the ride running (its run line high) the crash is **+1.35 dB** (its network pulls ac up by
  0.12 V); `rd.run` / `cr.run` carry it.
- The base currents are 1-60 nA; the other voice's (tens of nA through 200 Ω) is left out.

## 6. Filter, LEVEL, the jack board (cr.cir; SN grid E-F 16-19; ride F 23-27)

- **Sallen-Key 1**: **R456 5.6K**, **R457 5.6K**, **C136 1200P** from their junction to Q75's
  emitter (bootstrap), **C137 1000P** from Q75's base to ground; **Q75** emitter follower (collector
  +15 V, **R458 10K** to −15 V). f0 25.9 kHz, Q 0.55.
- **Sallen-Key 2**: **R459 5.6K**, **R461 5.6K**, **C138 2700P**, **C139 390P**, **Q76**, **R460
  10K**. f0 27.7 kHz, Q 1.32; together about a 4th-order Butterworth at 26.8 kHz (RES B.2.3), which
  passes the first image band (sample clock 25-56 kHz).
- **C132 10µ/16** (+ toward R445: Q76's emitter sits at −1.2 V, IC65a's (−) at 0) → **R445 22K** →
  **IC65a** (−), (+) grounded; **VR24 50K(B)** in its feedback as a rheostat, wiper on the output
  end: gain 0 … 2.27, `P24 out out n65a 50k level`. Ride: C144/C145/C146/C147, Q77/Q78, R462-R467,
  C133, R446, IC64a, VR26.
- **W5 → JK-1** (SN p.12): CN-5 pin 41 (crash) / 42 (ride); **R524 1K** and **C509 .01** (R527,
  C510; "SN 415300-UP C500-512"), the jack's normal contact, **R525 12K** and **R526 10K** into the
  mix amplifiers' virtual grounds (ride **R528 8.2K**, **R529 15K**). The netlists hold that load
  (`mix.cir` runs the jack board itself); `jack` is the contact with nothing plugged in.

## 7. Production changes and early units

- R442 / R440 33K → 10K from SN 392600 (§2): `r442` / `r440` in the clock netlists.
- C509/C510 from SN 415300 (the multi-jack board's 0.01 µF; `mix.md`); the netlists have them.
- No other cymbal part changed (SN p.7). The defaults are the late board.

## 8. Disagreements and readings

- **RES B.2.2**: "the same pin sequence is drawn for the crash and the ride": the ride's counter
  starts with the 4520 and its address pins differ (§3). Immaterial to the playback.
- **RES B.2.3** "accent 9.0 dB/V": with the other cymbal's load on IC9, 6.7 dB/V (§5); the measured
  range agrees with 6.7 (§10).
- **SN p.5** ("anti-log tapered by IC52b and Q70") and RES B.8: the exponential acts on the accent,
  the envelope is linear in the address D/A's current, as RES says; confirmed by the model's law.
- **OLD**: an Ebers-Moll VCA with a fitted β_R 1.7 and a recombination current (55 nA) for the 1.1
  law; a fitted clock (k 1.84, 3.45 µs a period of gate delay, a TUNE dead zone): this netlist
  takes the parts as drawn and lists the same misfits as candidates. OLD assumed the other cymbal
  stopped (2.2K at 0 V): here `rd.run` / `cr.run` carry its state.
- The bar-dot symbol is +5 V (SN's legend; NAVA's nets), not ground: IC52b's (+), R428 and R429.

## 9. Rates and the D/A's timing

**The clock isn't run by the board.** In the solver at board rates the astable is meaningless
(TUNE 0.5, 2 ms, against ngspice's 15.470 µs period): at 192 kHz its period gets three steps and
comes out 11 % short with 2.4 µs of jitter (27 of 130 edges, 209 Newton iterations a step); at
768 kHz −1.9 % with 0.5 µs of jitter; at 3 MHz −1.4 %, 0.13 µs (12 Newton iterations a step, 30 s
of CPU a second); 12 MHz −0.86 %; 49 MHz −0.035 %; 100 MHz −0.007 % (175 s of CPU a second). Its
edges take tens of nanoseconds and the period hangs on them (1 pF more at each node: 1.2 %).
Instead Cymbal::setClock runs the clock netlist (the solver, 100 MHz) at
41 TUNE positions (every 0.025) from the stopped state with TRIG and RUN rising: the reset's end,
the first four edges, then the two steady half periods (two periods: twelve edges move them by
under 10⁻⁵); setTune interpolates with a cubic, the end segments on the parabola of their three
positions (the half periods bend near TUNE 1, A's with a minimum at 0.97: a straight line between
positions would be up to 10⁻³ off there). Against the clock netlist run at the TUNE itself: within
4 · 10⁻⁵ of the period halfway between positions, 2 · 10⁻⁴ over the last 0.025. The board's steps
then get the edges at their exact times. About 0.2 s (on four threads; 0.4 s of CPU) a voice, once
per process (cached by netlist and parameters). Placed against the clock netlist run along at
100 MHz for 10 ms (`tr909cyrun --live-clock`): within 22 ns (TUNE 0.55, between positions), 21 ns
(TUNE 1), 1.8 µs (0.9875), i.e. 2 · 10⁻⁶ to 2 · 10⁻⁴ of the time.

**The analog half** gets the D/A lines band-limited at their exact times (§3); measured against
ngspice on cr.cir with every line stepping at its exact time (10 ns edges, gear, 0.25 µs steps), the
first 60 ms of a crash at code 47, LEVEL 1, from Cymbal's event log (`tools/spice/cases/cymbals.py
steps`): the jack's level per band, solver minus ngspice (dB), and the difference's level under the
signal there:

| Board rate | lines | 0-2 kHz | 2-5 | 5-10 | 10-15 | 15-20 | 20-30 | rms difference |
|---|---|---|---|---|---|---|---|---|
| 96 kHz | sinc | 0.00 (−46) | −0.06 (−40) | −0.17 (−30) | −0.50 (−21) | −1.69 (−9) | −4.5 (−3) | 9.3 % |
| 96 kHz | box | 0.00 (−42) | −0.08 (−37) | −0.26 (−28) | −0.69 (−19) | −2.25 (−8) | −5.2 (−3) | 9.8 % |
| 192 kHz | sinc | 0.00 (−50) | −0.03 (−47) | −0.09 (−38) | −0.22 (−30) | −0.66 (−20) | −1.3 (−14) | 2.9 % |
| 192 kHz | box | 0.00 (−49) | −0.03 (−45) | −0.11 (−36) | −0.27 (−29) | −0.76 (−19) | −1.4 (−14) | 3.2 % |
| 384 kHz | sinc | 0.00 (−54) | −0.01 (−52) | −0.04 (−44) | −0.10 (−38) | −0.29 (−28) | −0.5 (−24) | 1.1 % |
| 384 kHz | box | 0.00 (−53) | −0.01 (−51) | −0.05 (−43) | −0.12 (−37) | −0.32 (−27) | −0.6 (−23) | 1.2 % |
| 768 kHz | sinc | 0.00 (−58) | −0.01 (−57) | −0.02 (−50) | −0.05 (−44) | −0.14 (−35) | −0.2 (−32) | 0.5 % |

Ride at 192 / 384 kHz (sinc): 15-20 kHz −0.45 / −0.19 dB, rms difference 4.7 / 1.7 %.

- The D/A's placement is not what limits it: sinc and box differ by 0.1 dB at 15-20 kHz from 192 kHz
  up. The solver's own integration is (the trapezoidal rule pulls the 27 kHz filter and the jack's
  15.9 kHz RC down; the same at any input): the error falls 8-12 dB an octave of rate.
- **Needs**: the images up to 20 kHz within 0.3 dB take **384 kHz** (four times the 96 kHz board);
  **192 kHz**, the 808's metal-group rate, gives 0.7 dB at 15-20 kHz (crash), 0.45 dB (ride), 0.1-0.2
  dB below 10 kHz. 96 kHz is 1.7 dB low at 15-20 kHz. The D/A smoothing: sinc (latency 8 steps) at
  any rate; box (half a step) costs 0.1-0.5 dB more at the top.

## 10. Against the real 909

`tools/compare/cymbals.py` (AudioRealism CR/RD TUNE 0-100 % and the accent takes at TUNE 50 %; Rob
Roy dial 0-10): renders through tr909cyrun at 192 kHz, the jack resampled, both measured alike:
the clock (ROM cross-correlation), the per-block VCA law against the ROM, levels within a series,
LSD and octave bands over 0-300 ms. `--match` renders the model at the TUNE where its clock is the
recording's, to compare the analog half alone.

- **Clock**: 7-22 % slow (§2), the one large misfit.
- **VCA law** (gain in dB at envelope codes 16 / 32 / 48 / 56, re codes 1-4; clock matched):
  AR crash −2.5 / −6.2 / −14.2 / −21.6, model −2.4 / −6.0 / −12.3 / −19.3 (RMS over the blocks
  within 35 dB: 1.0-1.3 dB); AR ride −2.6 / −6.0 / −13.5 / −20.4, model −2.8 / −6.6 / −12.7 / −19.4
  (0.6 dB); RR crash −1.8 / −5.3 / −11.8 / −19.1 (0.4-0.6 dB against the model); RR ride −2.7 /
  −6.0 / −12.4 / −19.8 (0.3-0.4 dB, dial 0-8; its dial-10 file is trimmed).
  The two units bracket the model: AR's crash bends (about ^1.1, RES's fit), RR's doesn't.
- **Spectrum** at matched clock: LSD 0.7-1.3 dB (crash), 0.8-1.0 dB (ride); octave bands 0.5-8 kHz
  within 0.6 dB of the recordings against 16 kHz. Unmatched the LSD is 3-5 dB: the clock's error.
- **Levels**: within the TUNE series ±0.15 dB (matched), ±0.8 dB (unmatched); the ride 6.17 dB under
  the crash against AR's 6.65 dB (R439 470K against R432 270K; one interface gain, LEVEL 50 % on
  both). **Accent** (codes 32 → 47, AR accent000 → 100): crash +7.95 dB against +7.84, ride +8.02
  against +7.69; each take within 0.6 dB (`real909.AR_ACCENT_TA`'s TA per take).
- Candidates for what's left: the clock (§2); the VCA's bend on AR's crash (Q71's selection, the
  (S); its BR moves only the level); per-unit pot values.

## 11. SPICE (tools/spice/cases/cymbals.py)

Suite cases (`suite.py --only cr,rd`): normal (10), accented (21), full accent at LEVEL 1 (47), the
other cymbal running at LEVEL 0.2, for both; `cr_end`, the counter's last 1024 samples and the stop.
The lines are the ROM's at a steady 35 kHz clock, each step averaged over its 96 kHz step (Box), so
both solvers see the same piecewise-linear input. RMS difference in % of the output's range,
`out` / `jack` (max in brackets):

| Case | 96 kHz | 192 kHz | 384 kHz |
|---|---|---|---|
| cr_10 | 1.57 / 1.24 (8.4) | 0.38 / 0.31 (1.9) | 0.074 / 0.060 (0.36) |
| cr_21 | 1.55 / 1.23 (8.3) | 0.37 / 0.30 (2.0) | 0.074 / 0.061 (0.36) |
| cr_47_level1 | 1.36 / 1.09 (6.5) | 0.34 / 0.29 (1.7) | 0.113 / 0.095 (0.84) |
| cr_10_other | 1.57 / 1.24 (8.4) | 0.38 / 0.31 (2.0) | 0.074 / 0.060 (0.36) |
| cr_end | 1.01 / 0.80 (6.8) | 0.27 / 0.23 (2.3) | 0.055 / 0.048 (0.41) |
| rd_10 | 2.82 / 2.45 (13.6) | 0.67 / 0.60 (3.4) | 0.133 / 0.117 (0.64) |
| rd_21 | 2.80 / 2.44 (13.7) | 0.67 / 0.60 (3.5) | 0.133 / 0.118 (0.63) |
| rd_47_level1 | 2.56 / 2.28 (12.4) | 0.62 / 0.57 (3.1) | 0.161 / 0.149 (1.08) |
| rd_10_other | 2.82 / 2.45 (13.6) | 0.67 / 0.60 (3.4) | 0.133 / 0.117 (0.63) |

(192 kHz with the solver's current deck options, 0.1 pF node shunts; 96 and 384 kHz before that
change, which moved these cases by under 0.07 % of their range.) The 0.5 % target is met from
192 kHz on the crash and 384 kHz on the ride, not at 96 kHz: the hits'
content at 10-30 kHz (the image band) is what the trapezoidal rule gets wrong, more on the brighter
ride. Newton: 1.7-2.5 iterations a step whole, 0 substeps, 0 failures.

**The clock** (`cymbals.py clock`): the period, solver at 100 MHz against ngspice at 2 ns steps
(without the export's node shunts: 1 pF at each node moves it by 1.2 %, the export's 0.1 pF by
0.1 %): crash and ride alike (the same circuit), R442 10K: TUNE 0 20.140 µs (−0.006 %), 0.5
15.470 µs (−0.007 %), 1 11.380 µs (−0.005 %); 33K: 20.364 (−0.003 %), 15.535 (−0.004 %),
11.438 µs (−0.003 %). The reset: 6.918 µs. The JFET stand-ins the gates had before the MOSFET
element gave the same table to 4 · 10⁻⁶.

## 12. CPU

`tr909cyrun`, a 1.2 s hit at code 21 (Apple M1 Max, one core, partitioned, nothing asleep):
**0.11 s of CPU a second at 96 kHz, 0.23 at 192 kHz, 0.44-0.46 at 384 kHz**, crash and ride alike;
whole (no partitions) 0.36 s at 192 kHz. Box smoothing saves 0.01-0.02. The cost is the vca
partition (Q70B, Q71: 4 ports, about 1.1 Newton iterations a step, 11 % linear: Q71 moves with the
signal); env and out run 99-100 % in linear mode. vca and out sleep once the VCA is shut. Cymbal
itself is under 1 % of it. setClock: 0.2 s once a voice (0.4 s of CPU on four threads).

## 13. For the board (core/cymbals.h)

```cpp
class Cymbal {
	enum class Smoothing { Sinc, Box };
	struct Lines { double d[6], e[6], run; };          // cr.cir's d0..d5, e0..e5, run (volts)
	struct Timing { double reset, first[4]; bool firstIsA; double ab, ba; };
	void setRom(const uint8_t *data, size_t size);     // hn61256p__c42.ic62 / __c44.ic54
	bool setClock(const std::string &netlist,          // models + crclk / rdclk text
	              const std::map<std::string, double> &params = {}); // the voice's .param overrides
	void setTune(double rotation);                     // VR25 / VR27 (Controls::crTune, rdTune)
	void setRate(double hz);                           // the internal rate step() runs at; then reset()
	void setSmoothing(Smoothing s);                    // Sinc (default) or Box
	double latency() const;                            // the lines' delay, steps (8 or 0.5)
	void reset();                                      // stopped at 8000, the latch at ROM[7FFF]
	void start(double delay = 0);                      // TRIG's rising edge, delay s into the next step
	const Lines &step();                               // one internal step: the lines for it
	bool running() const;                              // NOT bit 15
	bool idle() const;                                 // stopped and the lines settled
	unsigned count() const; int code() const; double time() const;
	void setLog(std::vector<Event> *); void setEdgeLog(std::vector<Edge> *); // tools
};
```

- Each internal sample: on TRIG2 bit 4 (5) rising, `start(0)`; then `step()` and its lines into
  cr.cir's `d0..d5`, `e0..e5`, `run` (`cr.d0` … as board signals); `cr.run` also feeds rd.cir.
  The trigger needs no netlist step of its own (no `trig` input in cr.cir): the clock netlist is
  only Cymbal's. `idle()` lets the board stop stepping Cymbal; the netlist's vca/out partitions
  sleep on their own.
- `setClock` takes the clock netlist text (`netlists::find("crclk")` after `models`) and the same
  `cr.` parameters the board gives cr.cir (`r442`, `vr25`, `c131` live in crclk.cir).
- Rates: §9. Partitions: env, vca (in=q70e, sleep), out (in=o51b, sleep); Q70's emitters are the
  cut (IC52b holds them where Q70A carries the envelope current, whatever Q70B draws).
- The API mirrors HiHat's (`core/hihat.h`); the two could share the line smoothing.

**machine.cmake** (for the lead):

```cmake
# add core/cymbals.cpp to lemus_tr909core's sources, then:
# The crash and ride alone: a hit to WAV through Cymbal and cr.cir / rd.cir, the clock's timing
# (crclk.cir, rdclk.cir), the D/A events for the SPICE comparison, the CPU.
add_executable(tr909cyrun ${TR909_DIR}/tools/cyrun/main.cpp)
target_link_libraries(tr909cyrun PRIVATE lemus_tr909core)
target_compile_definitions(tr909cyrun PRIVATE TR909_ROM_DIR="${TR909_ROM_DIR}")
```

## 14. Open questions

- The TC4011UBP's own drive and gate-drain capacitance (§15 fits them at 1.72× the CD4011UB's
  typical drive and 2.15 pF): a measured part would test the fit.
- AR's crash VCA bends more than the model's and RR's (^1.1 against ^1.0).
- Whether the board can afford 384 kHz for the cymbals (0.44 s of CPU a second each), or takes
  192 kHz and 0.7 dB at 15-20 kHz.
- The latch-to-count phase on a retrigger is the unit's (a quarter sample, hit to hit); not checked
  on recordings of repeated hits.

## 15. Calibration

Fitted to the AudioRealism unit (AR), checked on Rob Roy's (RR) and against the hi-hat's astable on
AR's unit. The analog halves need nothing: at a matched clock their spectrum is within 1.1-1.3 dB
LSD, their levels within a TUNE series within 0.15 dB, the accent range within 0.4 dB (§10). The
misfit is the clock, 7-22 % slow on both units and both voices, and its knob.

**Measure.** The sample clock (ROM cross-correlation, `real909.rom_rate`) on AR's TUNE grid, 0-1 in
0.1; fitted on TUNE 0, 0.2 … 1 (both voices together), held out 0.1, 0.3 … 0.9; checked on RR's dial
0-10 (rotation = dial / 10, AR's laws and parts) and on the hi-hat's astable (R492 12K, R493 33K,
C160 470P polystyrene, the same TC4011UBP: 31.57 kHz on AR and RR, `docs/reference.md`). The clock
netlists timed directly (a harness on `Cymbal::timeClock`, Nelder-Mead on the cents), then the final
numbers through the board (`tr909voicerun`, `tools/compare/cymbals.py --cal ...`, AR's recording
chain applied).

**What the parts can do** (cents RMS against AR's 11 settings, each the best fit of its parts):

| Parts free | Crash | Ride | Hi-hat's astable | Why not |
|---|---|---|---|---|
| none (nominal) | 250 | 290 | +0.7 % | |
| C131 / C130, VR25 / VR27, the knob law | 38 | 92 | (+0.7 %) | C −14-16 %; the model's period bends near the top of TUNE (its gates against 6.8k), the units' doesn't |
| R443 / R441 | | | | the top needs about 4.4k (−35 %) |
| the pins' capacitance (5 / 15 / 15 pF) | | | | halving them all: +3 % at the top, against +20 % needed |
| **the gates (drive, gate-drain C, shared), VR25, VR27, C130, the knob laws** | **8.0 / 20** | **12.9 / 18** | **−0.4 c** | |

(fitted / held out.) Only the gates explain both ends of both voices and the hi-hat at once: the
cymbals (R442 10k) run 13 % faster than the hi-hat's astable (33k) at the same 12k and 470p on the
unit, where the data-sheet gates give 1.4 %. A gate-drain capacitance slows an astable with a large
input resistor (the hi-hat's 33k) more than one with a small one (10k), and a stronger output
speeds both: the two fix the hi-hat and the cymbals' top together; the pots then set the bottom.

**Values** (for `unitCalibration()`):

```cpp
// The TC4011UBPs (IC59 crash, IC57 ride; one fit for both): drive x1.72 the CD4011UB's typical
// (the channels' width) and a 2.15 pF gate-drain capacitance, with the hi-hat's astable (31.57 kHz)
c.parts["cr.w4011n"] = 931e-6;    // N, 540u nominal
c.parts["cr.w4011p"] = 483.6e-6;  // P, 280.5u
c.parts["cr.cgd"] = 2.15e-12;     // C59M/C59N, none nominal
c.parts["rd.w4011n"] = 931e-6;
c.parts["rd.w4011p"] = 483.6e-6;
c.parts["rd.cgd"] = 2.15e-12;     // C57M/C57N
c.parts["cr.vr25"] = 8355;        // VR25 10k(B) -16 %: the crash's clock at TUNE's low end
c.parts["rd.vr27"] = 9031;        // VR27 10k(B) -10 %: the ride's
c.parts["rd.c130"] = 450.6e-12;   // C130 470p -4 %: the ride's top 4 % over the crash's
c.knobLaws["crTune"] = {{0.073, 0}, {0.956, 1}}; // dead ends: flat under 0.073 and over 0.956
c.knobLaws["rdTune"] = {{0.0745, 0}, {0.9456, 1}};
```

- **The gates**: 1.72× the CD4011UB's typical drive is 1.7 mA at a 0.4 V drop, above every typical
  published (MC14011UB 0.88, CD4011UB 1.0; the TC4011UBP's sheet has minimums only, 0.44): flagged,
  a strong part or the level-1 model's way of making the gate's transitions faster. The 2.15 pF is
  within the data sheets' C_IN (5 pF typ). Both are the unit's IC59/IC57 as one; they move no other
  voice (the hi-hat's clock is HiHat's own constant).
- **VR25 −16 %, VR27 −10 %**: within a carbon pot's 20 %. **C130 −4 %**: within 10 %.
- **Knob laws**: TUNE does nothing over the first 7.3-7.5 % and the last 4.4-5.4 % of the turn,
  straight between: `docs/reference.md`'s 0.073 / 0.949 and 0.075 / 0.940 from the period alone.

**Before / after** (through the board; model minus recording):

| | Crash nominal | Crash calibrated | Ride nominal | Ride calibrated |
|---|---|---|---|---|
| AR clock, cents RMS (fitted / held out) | 250 / 248 | 8.0 / 20.0 | 289 / 291 | 12.9 / 17.8 |
| AR clock, worst setting | −352 (TUNE 1) | −32 (0.9) | −423 (1) | −36 (0.9) |
| RR clock, cents RMS (dial 0-10) | 286 | 56 | 286 | 69 (without RR's duplicate dial-4 file) |
| hi-hat's astable | +0.7 % | −0.4 c | | |
| AR t-20 / t-40 ms, TUNE 0 (rec 858 / 1092; 706 / 1097) | 1021 / 1242 | 903 / 1099 | 826 / 1241 | 659 / 1096 |
| TUNE 0.5 (rec 657 / 834; 555 / 834) | 783 / 955 | 690 / 845 | 567 / 949 | 555 / 833 |
| TUNE 1 (rec 457 / 572; 376 / 553) | 575 / 703 | 469 / 573 | 466 / 704 | 380 / 558 |
| AR level within the TUNE series, dB RMS | 0.38 | 0.10 | 0.62 | 0.14 |
| AR LSD 0-300 ms, dB (mean of 11) | 3.70 | 1.11 | 4.95 | 1.14 |
| RR LSD, dB | 4.51 | 2.51 | 5.48 | 4.00 |

- The decays follow the clock (the envelope steps with the address): t-40 now within 1-2 % at every
  TUNE. The crash's t-20 stays 2-5 % long (its VCA bends less than AR's: −19.3 dB at envelope code
  56 against −21.6); the ride's 6-10 % short at TUNE 0-0.3 (its t-40 right).
- The held-out settings are worse than the fitted (20 against 8 cents): the residual is a ripple
  along the turn (−20 c at 0.3, +21 c at 0.7, −32 / −36 c at 0.9 on both voices), a carbon track's
  unevenness or the knob's marks; a law with more points would take it and is not fitted.
- RR's unit: its crash +23 to +33 c at dial 0-4 and −58 to −85 c at 6-10, its ride +85 / +98 c at
  dial 0 / 2 and +18 to +68 c above: another unit's pots and dial marks, within a semitone.

**Validation** (for `docs/model.md`): crash / ride, model calibrated (nominal) against AR:

| Measure | Crash | Ride |
|---|---|---|
| Clock over TUNE 0-1, cents RMS | 15 (250) | 15 (290) |
| Clock against RR, cents RMS | 56 (286) | 69 (286) |
| t-20 / t-40 at TUNE 0.5, ms (AR) | 690 / 845 (783 / 955; AR 657 / 834) | 555 / 833 (567 / 949; AR 555 / 834) |
| Level: within the TUNE series, dB RMS | 0.10 (0.38) | 0.14 (0.62) |
| Level against the other voices (s-transistors.md §3.1, BR 6.5) | −1.1 dB | −0.1 dB |
| Ride under the crash (AR 6.65 dB) | 6.17 dB | |
| Spectrum, LSD 0-300 ms, dB | 1.11 (3.70) | 1.14 (4.95) |
| Accent, codes 32 → 47 against AR's takes | +0.19 dB over AR's range; takes 0.27 dB RMS | +0.39 dB; 0.35 dB RMS |
| VCA law, gain at envelope code 56 re codes 1-4 | −19.3 dB (AR −21.6, RR −19.0) | −19.3 (AR −20.4, RR −19.7) |

Against Roland's TR-909 plugin (`docs/reference.md`): its clock runs 29-118 cents under AR's
(26 661 Hz at TUNE 0, 35 267 at 0.5, 51 882 at 1 for the crash; no dead ends), the calibrated
model within ±21 c of AR's; at TUNE 0.5 its crash t-20 / t-40 are 720 / 880 ms (model 690 / 845,
AR 657 / 834), its ride 616 / 893 (model 555 / 833, AR 555 / 834).

**Unexplained.** The gates' fitted drive above every published typical (the level-1 MOSFET may stand
for the transitions' speed rather than the DC drive; the input clamp is not it: DCMOS from IS 1e-14
to 1e-8 and RS 10 to 0.1 Ω moves the clocks by under 1.5 % and the cymbal-to-hi-hat ratio not at
all, 2026-10-22); the clock's ripple along the turn (±20-35 c);
AR's crash VCA bending 2.3 dB more than the model at the envelope's end (RR's doesn't; Q71's
selection, s-transistors.md §3.6); RR's own pots; AR's ride accent takes recorded at a clock 1.3 %
under its TUNE-50 % grid take (the knob was reset by hand between sessions; 37 c in the model).

Reproduce: `VOICERUN=build/tr909/tr909voicerun tools/compare/cymbals.py --nominal --only ar,rr`
(before) and the same with the `--cal` values above as `--cal cr.w4011n=931u ... --cal
law.crTune=0.073,0,0.956,1 ...` (after); `--only acc` for the accent takes.
