# Korg MS-20: what we know is wrong, and the plan

What the model gets wrong or hasn't checked, as the transcription (`docs/circuit/`) and the checks
found it, and what is planned. "Real" is a Rev 1 unit (two KORG35s) unless it says otherwise.

## Known inaccuracies

### Sound

- **The LPF's self-oscillation over the oscillators** (2026-11-25). In one unit's hand sweep
  (g6OfcNTvJow, scene 21, PEAK 8.6, both VCOs and vibrato) the resonance is a line of its own that
  runs free between the harmonics, 18 dB over them; ours is pulled onto a harmonic unless it stands
  30-36 dB over them, and with the vibrato it then wobbles about 3 dB rms at the vibrato's rate
  (the recording 1 dB): it sounds bubbly. ngspice gives ours to the dB (the LPF netlist on the
  scene's input), and none of these frees it: the KORG35's and the JFET's junction capacitances,
  a capacitor across R117, the 4558's bandwidth, slew and gain, the 2SC1623's BR (at a matched
  pitch), NR, ISC, BF, the JFET's beta, R111/R113/C28/C29/C30/R109/R5, four limiter diode models,
  PEAK up to 10, the Rev 2 filters, noise in the loop up to 30 times shot noise, the hand's wander
  on the cutoff. Another unit (QjZgZodB614, scene 31, a square at PEAK 9.9) is pulled about as
  often as ours. Waits on data: another Rev 1 unit's LPF self-oscillating over the oscillators with
  vibrato, to tell this unit from the circuit.
- **Device noise in the filters.** The KORG35s have none here. Self-oscillating alone
  (t8t17yEHHUQ, scene 46) the real line has a noise skirt 35-50 dB over ours: -50 dB at +-2 %,
  -61 to -67 dB at +-5 %, -73 to -84 dB at +-20 %. Noise of 1.4 pA/rtHz into C28's node (or
  35 nV/rtHz at IC11a's input) gives that skirt (circuitrun, 2026-11-25); it moves neither the pull
  above nor any level. Plan item 2.
- **Units' filter trims.** At mid settings our HPF rings 0.3-0.45 of the knob high on three units
  (g6OfcNTvJow, 2d8UXDNJvkI, t8t17yEHHUQ) and agrees near 5 (the 7-4 trim); the LPF sits 0.1-0.25
  low on two; 2d8UXDNJvkI's HPF rings about 9 dB harder over pink noise than ours at its PEAK
  (8.2, read clearly) and its self-oscillation relative to the VCOs is 5.7 dB over ours on
  t8t17yEHHUQ. The scenes carry each unit's trims. R101 at 3.9k (its value lost in the scans)
  would fit those two units but puts scenes 20 and 22 (g6OfcNTvJow) 4-5 dB rms out: unit spread,
  not the drawing (Tried and dropped).
- **VCO 2's sawtooth level** (settled 2026-11-24). The circuit diagrams take node 11 straight to
  SW-4, three times VCO 1's saw (through R4 100K); real boards have **R62 100K** there, which the
  diagrams leave out but the manual's board layout places beside R63 (board photos, Rev 1 and Rev 2:
  `circuit/vco.md` §1). Three units' VCO 2 saw stands 3.6-4.6 dB under its square (t8t17yEHHUQ,
  BwBwJosbQkg's Rev 1 and Rev 2 units), two Rev 1 units' level with VCO 1's saw (-0.35 to -0.1 dB);
  ours with R62: -3.8 dB (H1, mixer) and +1.1 dB (VCO 2's swing is 6.80 V, VCO 1's 6.02:
  `compare.md` §1.4). Before, +4.7 and +9.5 dB, and the saw overdrove the filters' limiters.
- **VCO 1's tracking bow.** Trimmed exactly at C-4 and C-1 (§7-2 read strictly), C-2 / C-3 come
  out +2.02 / +2.22 cents: the 2SC1583's Early effect through the cascode (Q5's source, Q6B's
  collector, falls 0.12 V from C-1's current to C-4's; with VAF 1e6 the bow is -0.24 / +0.22, the
  keyboard ladder's own). Passing strictly needs VAF 34 V or more; the sheet's curves read 26-31.
  The calibration does what a technician does when the check fails: the high trim moved until the
  errors split (-1.1, +1.0, +1.1, -1.1 cents at C-1..C-4; decided 2026-11-22). A real unit's
  tracking would say whether the bow is this size.
- **VCO reset dead time.** 0.45 µs (VCO 1), 0.27 µs (VCO 2) from the netlist with assumed
  parasitics; K. Tanaka measured 0.3 µs on a built core. VCO 1's 10 kHz moves 4.4 cents between them.
- **KORG35 reverse-mode transistor data.** The 2SC1623's BR/NR fit NEC's saturation curves from
  BR 1.72 / NR 1.02 to BR 4.35 / NR 0.98; since 2026-11-23 the high end, which three real units'
  resonance asks for (Done below; Stinchcombe's library has 4.21). The pin-4 offset and the
  resonance's asymmetry go with it, unchecked (`circuit/semiconductors.md` §3).
- **R101** (the HPF's gain resistor) is illegible in every scan: 4.7 kΩ, its LPF twin R116's
  value (`circuit/filters.md`); one Rev 1 board photo reads yellow-violet-red (4.7k, likely;
  2026-11-24).
- **CUTOFF's range.** As drawn the knob moves the KORG35's control by 11.5 V, about 11.5 octaves
  (natural frequency 9.7 Hz to 24.6 kHz), against the specification's 50 Hz to 5 kHz (HPF) and
  15 kHz (LPF). Every resistor in the path reads the same in two scans. The recordings' knob
  readings don't contradict the drawing: a line through atoooy's six HPF readings (two videos)
  moves 1.11 octaves per knob unit, ours 1.15; its two LPF readings 1.01, ours 1.33 near the top
  (two points; Kick.S's 6.3 -> 1673 Hz against the service trim's 5 -> 500 Hz gives 1.34).
- **SIGNAL OUT level.** Check point 19 reaches 0.95 Vpp with a 10 Vpp mixer output and both filters
  open, against the block diagram's 2 Vpp: Q19's rank (orange/yellow chosen), its reverse beta
  (2.665; 5.6 would give 2 Vpp, outside the data), or the drawing's figure.
- **Self-oscillation's pitch** against ngspice (the LPF at PEAK 10, measured 2026-11-24 at the
  rates the machine picks: 4x below about 6.5 kHz, 8x above): +0.2 cents at 500 Hz, -0.3 at 2.6 kHz,
  -0.8 at 6.5 kHz (-2.4 at 4x, at the switch), -0.5 at 10.7 kHz, +5.9 at 18.7 kHz, +21 at 32 kHz
  (above the band); our solver at 3.07 MHz within 0.8 cents throughout. Before the 8x (2026-11-22,
  4x): -15 to +6 cents from 2.5 to 23.6 kHz; before the trapezoidal gain stages, +88 at 14.5 kHz.
- **Aliasing at the top of the KORG35s.** At CUTOFF 10 and PEAK 10 a KORG35 self-oscillates near
  37 kHz; since 2026-11-23 the KORG35s run at 8x (below). Before that, at 4x, held in quarters
  (16x), its outputs filtered from those quarters
  (`Circuit::setDecimation`, 2026-11-22): the LPF alone leaves its aliased 2.39 kHz tone at
  -85.5 dB (was -44.5), a 9.6 kHz product at -70 dB (the VCA's, after the filters, not
  decimated). `ms20corners` has no aliasing case left; two KNOWN remain, both "EG 2 10 into both,
  EG 2 all 0, PEAKs 10, CUTOFFs 5": EG 2 sweeping both self-oscillating filters many octaves at each
  note fills 1-16 kHz, the same band by band at 8x (within 1 dB): the circuit's own sound.
- **Grit in self-oscillating sweeps near the top** (heard in the A/B, 2026-11-23; gone since the
  KORG35s run at 8x, Done below). With PEAK 10
  and the oscillation between about 15 and 25 kHz (CUTOFF 9-9.6), its high harmonics fold at the
  KORG35s' 192 kHz rate into audible lines: held at CUTOFF 9.2 (17.5 kHz) a 738 Hz line at -51 dB
  under the tone, at 9.4 (21.4 kHz) a 744 Hz line at -36 dB (192 kHz - 9 x 21.4 kHz); at a 96 kHz
  host rate (384 kHz inside) the same lines fall to -75 and -67 dB. A sweep through that range
  drags them across the band: the grit. Not the cutoff's stepping (the same at 32- and 256-sample
  blocks), and there since the first build (this morning's build has it too). Below 15 kHz the
  oscillation is clean (55-70 dB between its harmonics).
- **EG 2's SUSTAIN** uses R169 = 4.3 kΩ (as the MS-10's same network, and the specification's
  "0~5V": 4.96 V) where every scan prints 43K (which would cap SUSTAIN at 13 % of the peak). One
  real unit's plateaus against its peak (BwBwJosbQkg 1056-1064 s, SUSTAIN read 5 / 7.5 / 9):
  -10.0 / -5.9 / -3.4 dB, ours -8.2 / -4.2 / -2.5 (R169 5.6k would give -10.2 / -6.0 / -4.2, but
  4.13 V at SUSTAIN 10); a higher peak (the latch's trip, 5.18-5.37 V here) would do the same, and
  a level against the peak can't tell them apart: kept. EG 1 idles at -7.5 V where the specification says
  -5 V. EG 2's gate opens 7.1 ms after every trigger from rest, as drawn (to confirm in a recording
  of a sharp attack). The shortest times depend on the 4007s' drive (typical used).
- **MG** swings 5.0 Vpp as the specification says (since 2026-11-24, IC15a's ±12.5 V; FM depth above);
  its rectangle 0 / 4.59 V against the specification's +5 V.
- **Noise.** TR3's junction noise is a white source behind rz3 1k (since 2026-11-24; before 10k),
  4.6 µV/√Hz: VCO 1's WHITE against its saw in one real unit's take, level within 0.1 dB, top within
  0.6-1.3 dB at 6-13 kHz (before 2.5-4.9 dB short; `compare.md` §2). J25 then gives 2.1 Vpp (6 sigma)
  against the specification's 5 Vpp: VR3 (NOISE LEVEL) has no procedure in §7. 1/f and burst noise
  are not modelled. PINK's slope agrees with a real unit (-5.89 dB/oct against the model's -5.97:
  one pole at 32.9 Hz, not a -3 dB/oct pink); PINK stands 1.75 dB over WHITE (the specification
  gives both 5 Vpp ±20 %; with 10k it was 6.25).
- **The self-oscillation's harmonics.** The LPF alone self-oscillating at a held knob in a vintage
  unit (QjZgZodB614 8.0-8.3 s, 1673 Hz, PEAK read 8.5-10): H2..H5 -26.3 -18.8 -35.6 -32.7 dB; ours
  since BR 4.35 at PEAK 9 -26.8 -22.8 -39.0 -41.1, at PEAK 10 -24.4 -16.6 -31.7 -28.2: the recording
  lies between (before, ours at PEAK 10, its most, gave -31.5 -22.1 -41.3 -38.0). (Corrected 2026-11-23: the two YH3g01qU7O8 "self-oscillation"
  tones `compare.md` §3 used are VCO 1 notes through the ESP's band-pass; that recording is taken
  from ESP BAND-PASS OUT.) The real chain has more second harmonic (triangle +7 dB H2): the VCA's
  operating point, its drive unknown in the clips.
- **Resonance under a loud input** (settled by BR 4.35, Done below). A C2 square at VCO 1 LEVEL 9.5
  through the LPF at a held 2.17 kHz (QjZgZodB614 52.8-53.4 s, PEAK read 9-10): the real resonant
  harmonic is -13.4 dB under H1; ours -23.2 at PEAK 9.5, -13.2 at 10 (before: no peak up to 9.8).
  g6OfcNTvJow's both saws at PEAK 8.6 now whistle through the hand sweep as the recording does
  (A/B scene 21). Scene 31's dip near the top (8.8-9.6 s) was the scene's: the codec hides the
  resonance above 16 kHz and the gap sent ours to CUTOFF 10, where a full-level square loses 9 dB
  (the LPF self-oscillating at about 37 kHz under it: H1 58.5 dB at CUTOFF 8.8, 49.6 at 10). Short
  gaps now hold the last tracked frequency. Whether the real LPF squashes so at CUTOFF 10 is not in
  any recording.
- **EG 2's reach into the LPF** (g6OfcNTvJow 0.45-10.6 s, every knob read off a tripod shot: LPF
  5.5, PEAK 8.6, EG 2/EXT 7.5, EG 2 decay 7.7, sustain 4.6): the resonance falls 2.27 -> 1.04 kHz
  (0.5-9.5 s) in the recording; ours since the power-law pots (2026-11-23) about 3.6 -> 0.9 kHz
  (before, 4.30 -> 0.91: 2.2 octaves against 1.1), half an octave high at EG 2's peak and right at
  the sustain, within a knob reading (CUTOFF's slope above, ours steeper near the top). (Ours meets
  the service manual's 500 Hz at CUTOFF 5, PEAK 10.)
- **MG rate against FREQUENCY** (settled 2026-11-24). Three units run slower than the drawing's
  straight law below the top: atoooy's at 13 settings (1.39 -> 1.06 Hz ... 10 -> 19.57 Hz), Kick.S's
  at four (2.0 -> 1.75, 2.7-2.8 -> 3.53-3.73, 4.5 -> 7.56 Hz), g6OfcNTvJow's at one (4.0 -> 7.21 Hz).
  VR25's track starts 0.91 of a knob unit from its stop (`j0.091/0.5/0.45`): atoooy's within 6 % RMS
  (straight: 31 %), g6's 5 % under, Kick.S's 3 % at 4.5 and 14-40 % fast at 2.0-2.8 (its dead zone
  nearer 1.2: spread). The top (19.5 Hz) and FREQ 0 (0.09 Hz; SPEC 0.1 Hz) are unchanged
  (`circuit/envelopes.md`, the MG).
- **F-V converter** reads +0.6 to +1.1 % against ngspice (C2 by backward Euler since 2026-11-22;
  before, 9-14 %). Not the sample grid: our solver at 3.07 MHz reads +1.4 %, its reset 2.9 us
  later than ngspice's and IC2C tripping 0.1 V higher (`circuit/esp.md` §4.3). Runs only when a
  cable takes F-V CV OUT.
- **ESP HIGH CUT:** C5 reads 0.15 µF in all three scans (the section's others .015); as drawn the
  top is 1.94 kHz against the specification's 5 kHz. Kept as drawn (`.param c5`).
- **MVCA** photocoupler law: Hamamatsu's P873 resistance curve with time constants partly from a
  sibling part (`circuit/klm128.md`). Running since 2026-11-23 (below); before, the cell stayed dark.
  Its speed at low light is assumed (the 380's drive dependence carried over): a real CdS cell
  brightens slower at small LED currents, which would start a delayed vibrato later. The delayed
  vibrato's start (zijbJb7Z84Y 32.7-45.7 s, EG 1 DELAY read 2.2 there and 2.6 at 45.7 s): ours 0.15-0.2
  s early at 2.2, on time at 2.6 (2026-11-24). The "0.5 s early" of round 7 was the A/B scene's: its
  30 ms key-ups left EG 1's C39 charged (22 ms time constant), so ours skipped the DELAY on every
  note after the first; the player's key-ups are 80-270 ms (scenes 52, 62 now take them from the
  recording's level dips, `fix_gaps.py`). A recording of an MVCA patch with EG 1's DELAY at 0 would
  separate the cell.
- **Pot tapers.** No sheet. Every A pot follows a power law, share = rotation^2.25 (21 % at mid),
  since 2026-11-23, fitted to four readings that fix the exponent independently: FM MG/T.EXT 2.0's
  depth (2.4), EG 2 DECAY 2's fall in a pluck (2.2), EG 2 DECAY 7.7's time (2.05-2.2), PORTAMENTO
  4.4's glides (real 545 / 525 / 505 ms, ours 490 / 515 / 475; two segments would be 17 % shorter
  still). The one reading against, EG 1's DELAY 2.6 / ATTACK 2.0 under the MVCA (ours' vibrato
  starts about 0.5 s early), was the A/B scene's key-ups (MVCA, above). Near the knobs' bottom the
  real EG times run long (2026-11-24, one unit, p41aRcIXPwA 140-155 s, 13 notes): EG 2 ATTACK 1.4
  (read 1.24-1.59, 1.1-1.2 by eye) rises 10-90 % in 0.167 s, ours 0.095; RELEASE 0.3 (0.2-0.6) falls
  20 dB in 0.064 s, ours 0.020. With DECAY 2's pluck (13 % long) that is 4-18 kΩ more at the bottom
  of the time pots (an end resistance of 0.4-0.9 %), or knob readings 0.4-0.6 low. Not changed: the
  same end resistance in the FM pots would leak the MG into the pitch at FM 0 (±16 cents), which no
  recording shows; one unit can't tell the pots from the readings. A second unit (BwBwJosbQkg
  1029-1074 s, EG 2 alone, knobs read ±0.5; 2026-11-24): ATTACK 2 rises in 0.39 / 0.38 s (ours
  0.18), 4.5 in 0.75 / 1.16 s (ours 1.07); RELEASE 0.5 falls -6 to -40 dB in 16-21 ms (ours 22);
  RELEASE 2 and 4 run about twice ours (gates unknown); DECAY 3.5 and 5 match within 1-3 dB over
  3 s, DECAY 2 runs 1.7 times ours, DECAY 1-1.5 faster. Long at 0.15-0.2 of the travel, right at
  0.05 and 0.35-0.5: no taper does that (an end resistance lengthens RELEASE 0.5 too; a linear
  first segment lengthens DECAY 5), so knob readings near the bottom (±0.5 is ±50 % of the
  resistance there) stay the likelier cause. Kept.
- **The HPF below self-oscillation: two units disagree.** atoooy's unit's HPF rings on pink noise
  11-12 dB harder than ours (CUTOFF 3.7-3.8, PEAK 7.5-8.2: +30.5 / +32.7 dB above the spectrum's
  trend against +19.5 / +20.4; scenes 48-50) and its self-oscillation at PEAK 8 carries more
  harmonics (H3 -22 against -36 dB, scene 47); R101 at 3.9k-4.3k (onset PEAK 4.4-6.0) would match
  it. g6OfcNTvJow's unit at HPF PEAK 7.4 has the tamer HPF that R101 4.7k gives (scene 20: with 4.3k
  ours' HPF line outshines the LPF's, the reverse of that recording). Unit spread, or R101 differs
  between units; R101 stays 4.7k, its LPF twin R116's value. (Scene 42's missing HPF ridge on white
  noise was the scene's: it swept the LPF. Since fixed, ours' ridge stands +9.2 / +9.3 / +4.5 dB
  against the recording's +9.9 / +8.1 / +4.3.)
  (Settled 2026-11-23 by BR 4.35: PEAK ran 0.6-1 unit behind; both filters now self-oscillate from
  PEAK 7.4-7.6, as the unit's do at PEAK 8, scenes 46-47.)
- **FM depth** (settled 2026-11-24, decided: follow the specification). The specification gives VCO
  1+2 EXT FREQ as "(OCT/V)" and the MG triangle as "5VPP"; the drawing gives 1.18 octaves a volt
  (R39 110K, R43 / R44 100K, R40 5.11K, R38 100, read in three scans and the parts list) and 5.6 Vpp:
  2.42 dB deeper. One unit's recordings agree with the specification: FM MG/T.EXT 7.4 with the MG's
  ramp, 2400 cents peak to peak (p41aRcIXPwA 38.9-55.0 s), ours 3081 before, 2422 now; 2.0 through
  the MVCA (zijbJb7Z84Y), ±55-57 cents, ours ±74-75 before, ±57 now (the taper's exponent from the
  two: 2.33). A second unit (g6OfcNTvJow) at FM 1.4: ±11 cents, ours ±34.6 (before ±45.5): a knob
  reading near an A pot's bottom (0.4 units would explain it). No part is known to differ: R43 and
  R44 stand at 117.8k (`rfm`, vcoctl.cir), which leaves MASTER TUNE's range alone, and IC15a swings
  ±12.5 V (`vsat15a`, mg.cir; the 4558's minimum is ±12 V), which leaves the MG's rate alone and puts
  the rectangle at 0 / 4.59 V (SPEC "+5V").
- **The LPF nearly closed** (white noise, PEAK 0, t8t17yEHHUQ 210.8-212.4 s; the knob is hidden,
  the cutoff under the edge measurement's 84 Hz). Ours at CUTOFF 2.3-2.4 matches the recording below
  400 Hz (-11.6 / -19.7 dB re open at 50-100 / 100-200 Hz, ours -11.5 / -20); above, the recording
  falls about 6 dB per octave to a flat -46 dB from 3 kHz, ours 12 dB per octave to -85 dB at 6-12 kHz.
  The recording's codec floor (Opus) or a leak; the 2SC1623's junction capacitances (5 pF) and
  base-collector leakage (`isc1623`) don't make it (they move the bands by under 12 dB). (Until
  2026-11-23 A/B scene 41 sent that floor to CUTOFF 0, which made ours look 30-70 dB too closed.)
- **VCO shapers.** Every step and corner is band-limited by two-point polynomials (the reset and
  the ring modulator's steps polyBLEP, the comparators' slewed edges polyBLAMP since 2026-11-22):
  against the audio path at 16x their harmonics droop 0.1-0.2 dB at 13-20 kHz.
- **Drift** (since 2026-11-24, always on, no setting): the VCOs' slow wander with temperature.
  The converters are log-antilog pairs (a 2SC1583 dual each), so the keyboard's linear CV cancels
  in temperature; what moves is linear in the cores (C2/C11 polystyrene, about -150 ppm/K; the
  scale ladders' carbon resistors, a few hundred ppm/K: +0.6 cents a kelvin together, an estimate:
  no parts list gives the resistors' kind) and the exponential part's scale, V_T's: a modulation of m
  octaves at the antilog base (MASTER TUNE, VCO 2's PITCH, the trims, FM) moves m T/T0, 4 cents a
  kelvin an octave (`Machine::driftFactor`, from the probes b6, b7 against vblog). One temperature
  walk for the board (1 K rms, 600 s) and one per oscillator (0.3 cents rms, 600 s), Ornstein-
  Uhlenbeck, pulled back to tuned, from tuned at each start and power-on, through sleep in closed
  form: after 30 s 0.31 K rms (0.2 cents at MASTER TUNE's centre), after 4 minutes 0.74 K, in the
  end 1 K; VCO 2 detuned a fifth from VCO 1 moves 2.3 cents a kelvin against it, so the beat
  wanders. Not checked against a recording (no long held note of a warmed-up unit in hand); not
  modelled: warm-up after power-on. Each instance is its own unit (since 2026-11-24): the hiss,
  the noise source and the drift run from a seed of its own, a new one at each load and power-on
  (`Machine::setSeed`, `Ms20Processor::reseed`), so two instances don't hiss or wander alike; the
  tools keep the fixed streams (`Machine` alone, `Ms20Processor::fixedSeeds`, `MS20_SEED=0`), so
  their renders repeat.
- **Asleep, the VCOs and the MG run on** (since 2026-11-24; before, they stopped and took up where
  they were). On waking, each VCO core moves on by the time asleep at its charge current
  (`Vco::advance`; the MG's FM while asleep left out). The MG runs alone for one cycle after the
  machine falls asleep, keeping its state at both ends of both halves; from then on its phase
  follows the clock (the turns between samples, from where the halves' lines meet), and waking puts
  it there (`Machine::wakeUp`); its LED blinks on while asleep. Against a machine kept awake
  (`ms20bench sleep`, 24 MG cases, gaps of 2 and 7.3 s): within 0.01 % of a cycle a cycle slept
  (0.15 samples a cycle at most), VCO 1 0.002 %. The S&H, the ESP and anything else stay stopped.

### Behaviour and features

- **Not emulated:** the supplies' ripple (§7-1: measured and left out, "Tried and dropped");
  temperature; component spread between units.
- **The Rev 2 filter board** (since 2026-11-24): SERVICE's CIRCUIT (the bar) picks REV 1 (two KORG35
  modules, as the service manual draws) or REV 2 (the later KLM-307 daughterboard: LM13600 OTAs, the
  VCA with them), as Korg's own MS-20 Kit has a switch; the session keeps it (`circuit/klm307.md`).
  Fitted to the one Rev 2 recorded screaming (BwBwJosbQkg 716.2-719.8 s, the same patch and phrase
  as its Rev 1's; accuracy round 14): **D8-D13** (no type printed) clamp about 0.55 V later than
  three 1S1555s (`LIM307X3`); **VR2** (KLM-307's LPF resonance preset, no procedure) at 0.55, just
  under self-oscillation at PEAK 9.5, where the whistle steps from harmonic to harmonic; **R38**
  4.7k (printed 3.3K; 33k from 2026-11-24 to 2026-11-27, decided again 2026-11-27): how hard the VCA
  pair is driven sets the whole signal's squash and brightness. **VR1** (the HPF's) at 0.3, from
  the same video's Rev 2 HPF sweeps (807-827 s, HPF PEAK 8 and 9.5): the HPF oscillates at the
  top of its sweep from PEAK 8 (about 18.5 kHz, as loud at 9.5); D1-D6 stay 1S1555 (LIM307X3 there
  puts that top 7 % low). **R9**, printed 47K where 4.7K
  would be expected: kept; 4.7k changes nothing measurable in that recording. The VCA's treble shelf
  (C9 + R42, -6 dB above about 340 Hz) is as drawn and matches the recording (octave by octave
  within 0.1 dB of the real units' Rev 2 against Rev 1). The LM13600 reduced to a behavioural OTA
  and a one-transistor buffer (half the solver's work; within 2-10 cents and 0.2-0.44 dB of the
  sheet's transistors: klm307.md §2). Left: the whistle steps a little less than the recording's
  (energy between the harmonics early in the sweep -3.6 dB against -7.8).
- **Not a plugin output:** PHONES (the plugin's output is SIGNAL OUT).
- **POWER** (VOLUME's click) boots the unit from cold and lets it run down (since 2026-11-24;
  `Machine::switchOn`, `core/machine.cpp` "POWER"); a plugin or session load stays warm, settled
  at its settings. Switched on, every capacitor starts at 0 V and the ±15 V rails rise as C23
  charges (an estimate: no winding resistance or rail load is published), at ±15 V after 13 ms;
  the constant sources and the op-amps' output swing (the rails less each one's headroom) follow
  them. The board charges to its operating point: SIGNAL OUT within 1 mV of a warm start's after
  0.26 s, its capacitors within 10 mV after 1.2 s, the slowest signal (IC11b's output, C35 through
  R120) within 1 mV after 3.5 s; the machine stays awake 4 s. EG 2 fires at the switch (its HOLD
  timer C42 starts empty, as a trigger leaves it) and opens the VCA on the filters' settling: a
  power-on thump of 1.14 V peak at VOLUME 7 (+1.2 dBFS; 0.16 at VOLUME 0.5, just out of the click,
  where the switch leaves it), back to rest by 1.4 s. IC13B, the trigger's Schmitt trigger, starts
  high (an assumption: the solver would decide its tie at 0 V either way). Switched off, the rails
  hold 13 ms, then fall (C23 into about 0.1 A); a held note dies with them (-16 dB at 30 ms, -37 dB
  at 100 ms); below 4.5 V the board stops and SIGNAL OUT dies as C33 empties: exact zeros 0.21 s
  after the switch. Not modelled: the negative rail falling first (C24 is half C23), the op-amps'
  minimum supply, the VCO cores' trip and top following the rails, the mains' phase. Cost: the 13
  ms the rails rise take 1.7x real time a 64-sample block at worst (10 of the first 15 blocks over
  at 48 kHz, 8 of 13 at 44.1); the run-down 1.1x at worst (1 block over); 0 unconverged solves.

### Reference data

| Unit | Where | What it has | Known settings |
|---|---|---|---|
| 6 vintage Rev 1 units (uploaders' words) | `private/ms20/reference/youtube/` (SOURCES.md) | demos, sweeps, one 15 min dry exploration | none stated |
| 3 vintage Rev 2 units | 〃 | demos, a modded unit | none |
| Rev 1 against Rev 2 on one patch | 〃 (1MXo9aPZETA, I6kLYRX8F2U) | the two filters side by side | none |
| 12 vintage, revision unknown | 〃 | waveforms, PW sweep, noise, envelopes; two with every knob stated | EyB1ZHhjA9w, oIil0WVC-Ko |
| Korg's reissues (mini, FS, MS-20M) | 〃 | the KORG35 noise leak, REV1/REV2 | some PEAK settings |

## Budget

- **CPU:** `ms20bench`'s `everything-44k` under 60 % of a core at 48 kHz (raised 2026-11-23 for the
  KORG35s at 8x, which take the grit and ticks out of their self-oscillation near the top; 52 %
  since 2026-11-22 for the trapezoidal gain stages). Last measured (2026-11-24, CPU round 4: the
  control side on a second thread; best of 6 rounds beside the round-3 build, load average 4-8):
  43.2 % of a core on the caller's (audio) thread, 50.8 % over both threads (one thread: 49.2 %);
  the other workloads 14-19 % on the audio thread, 19-26 % over both. Worst 64-sample block:
  0.74-0.79x, everything-44k 0.94x (the first block of the key change at 3.10 s: the keyboard block,
  0.8 ms on the control thread; before, 1.13-1.19x at the note event at 0.51 s, EG 2 and EG 1 with
  the LPF's Newton at 8x).
- **CPU, Rev 2** (opt-in, the same ceiling): since CPU round 7 (2026-11-24, two runs on a loaded
  machine), over both threads scream-rev2 40-41 % (Rev 1 26 %), everything-rev2-44k 46-47 % (Rev 1
  53-54 %: its LPF self-oscillates at 8x), selfosc-rev2 28-29 % (20 %); worst 64-sample blocks
  everything-rev2-44k 0.94x, everything-44k 0.85-0.88x, the rest 0.77-0.84x.
- **Check set baseline:** 2026-11-25 (accuracy round 13), golden renders `build/ms20-golden-28`;
  SPICE 78/78; corners 0 failing, 0 KNOWN, 0 unconverged (`--rev2` 2 KNOWN); service, plugintest
  (+ `--panel`), the host test, `threads`, `sleep`, `boot` and auval pass. Before: 2026-11-24 (accuracy round 12, drift), golden renders
  `build/ms20-golden-24`; the rest as round 11's (corners 0 failing, 3 KNOWN at the flatness
  limit, 0 unconverged; sleep checked with drift off). Before: 2026-11-24 (accuracy round 11, Rev 2), golden renders
  `build/ms20-golden-23` (+ scream-rev2, everything-rev2, selfosc-rev2); service checks 46/46 (§7-4
  on Rev 2 too); SPICE suite 78/78 (+ 20 KLM-307 cases); `ms20corners` 0 failing, 1 KNOWN, 0
  unconverged, and the same with `--rev2`; `ms20bench threads` and `sleep` pass; plugintest (+ the
  filter board), the host test and auval pass. Before: 2026-11-24 (accuracy round 10), golden renders `build/ms20-golden-22`;
  service checks 44/44; SPICE suite 58/58; `ms20corners` 0 failing,
  1 KNOWN, 0 unconverged; `ms20bench threads` and `sleep` pass; plugintest, the host test and auval
  pass. Before: 2026-11-24, golden renders `build/ms20-golden-20`; service checks 44/44;
  SPICE suite 58/58; `ms20corners` 0 failing, 8 KNOWN, 0 unconverged; plugintest, the host test and
  auval pass. Before: 2026-11-23, golden renders `build/ms20-golden-19`; service checks 44/44;
  SPICE suite 58/58; `ms20corners` 0 failing, 5 KNOWN, 0 unconverged; plugintest, the host test and
  auval pass. Before: 2026-11-22, golden renders `build/ms20-golden-10`; service checks 44/44;
  SPICE suite 58/58 (it takes the newer circuitrun of `build/ms20` in the clone or beside it,
  since 2026-11-22); `ms20corners` 0 failing, 2 KNOWN, 0 unconverged; plugintest, the host test and auval pass. Before: 2026-11-15, golden renders
  `build/ms20-golden-2`; service checks 42/44
  (VCO 1's bow above); SPICE suite 58/58 (4 known: the MG's phase drift, the F-V converter);
  plugintest and the host test pass; auval passes. 2026-11-16: golden renders
  `build/ms20-golden-pre` (bit-identical with the solver's counts and holds); `ms20corners` 0 of
  300 failing, 10 KNOWN (the aliasing above), 0 unconverged.
- **Check set:** `ms20bench check` (the golden renders; `tools/bench/compare.py OLD NEW` for the
  per-note pitch and level of two sets), `ms20service` (§7's checks),
  `tools/spice/suite.py` (every netlist against ngspice, 20 s), `ms20plugintest` (MIDI, random
  settings, 20 s note, session, sleep, POWER's boot and run-down; `--panel`), `ms20bench boot`
  (POWER from cold beside a warm start: the boot's length, transient, blocks; the run-down),
  `ms20bench threads` (two threads against one, bit for bit: the workloads, patches, POWER, sleep,
  odd block sizes; `MS20_THREADS=1` runs any tool on one thread), `ms20bench sleep` (the MG and VCOs
  through a sleep against a machine kept awake), `ms20corners --rev2` and `ms20render --rev2` (the
  Rev 2 board), `lemustest_ms20` (through the host),
  `ms20corners` (the controls' extremes: unconverged solves and operating points, noise where
  nothing makes it, bounds; about a minute), auval.
- **A/B scenes:** `host/scenes/` (ms20-bass, ms20-scream); the real recordings' in
  `private/ms20/reference/scenes/` (`make_scenes.py`: VCO 1 saw, triangle, square; VCO 2 saw,
  square, narrow pulse; white noise; t8t17yEHHUQ at the settings read off the video).

## Plan, in order

0. ~~Decision: R161 33k~~ (printed 1K; round 13): kept, decided 2026-11-27 (a known departure
   from the drawing; a board photo of R161 would still settle it). D20/D21's 1S1555LIM knee: kept
   (decided 2026-11-25).
1. Accuracy, waits on data: EG 2's reach into the LPF in g6OfcNTvJow (one reading); the EG time
   pots' bottom (one unit; Pot tapers); the resonance's asymmetry, SIGNAL OUT's level and second
   harmonic (`docs/compare.md` lists what would settle each). The scenes and the readings they come
   from: `private/ms20/reference/scenes/ab/` and `events/`.
1b. Accuracy: the KORG35s' device noise (above): noise sources into C15/C28's nodes at the measured
   skirt's level, both filters; a few percent of CPU.
2. Accuracy: noise's 1/f and burst components (no recording isolates them).
3. CPU: the worst block (everything-44k 0.94x since round 4: the first block of a key change, at
   3.10 and 4.10 s, where the keyboard block, kbd.cir at 2x, takes 0.8 ms on the control thread,
   longer than the audio path's 0.6 ms beside it; the note events' EG 2 + EG 1 + LPF blocks a few
   blocks later now overlap and take about half what they did). (a) ~~The control blocks on a second thread~~ (done
   2026-11-24, CPU round 4: worst blocks -9 to -20 %, everything-44k -16 %; not the -30 % hoped, as
   the worst block is now the keyboard's alone; over both threads +2 to +6 % CPU, bass +15 %).
   What's left, by size (fidelity-neutral trimming used up, 2026-11-24, after rounds 5-7): every
   remaining lever trades something. The keyboard's spike at a key change is now the comparator
   flips' sixteenths (their edges to 0.7 us): eighths take its iterations down 20-30 % and the
   average 1 %, but move notes 0.2-0.34 cents and scream's level 0.2 dB; a contact closing over a
   few samples would need the switch element continuous (every netlist's switches);
   (b) the LPF's Newton while it self-oscillates above the audio band (2.26 s of everything-44k's
   5 s; 2.9 iterations, 1.55 factorisations a step at 8x): a three-point predictor (not bit-exact,
   within the solver's tolerance), or 4x there (a fidelity trade: its H5 folds into the band);
   (c) EG 2's linear-mode re-entry, 120-250 us a time after a note (rebuilding its state-space
   matrices around new operating points; not cacheable).
4. UX: ~~POWER as a cold start~~ (done 2026-11-24, UX round 2); a MIDI CC map (the unit has no
   MIDI; the SH-101 maps every slider).
5. ~~**Drift**~~ (done 2026-11-24, accuracy round 12, above; each instance its own seed since the
   same day). Left: a check against a long held note of a warmed-up unit (none found yet).

Waits on data: R169 (an absolute sustain level, or a sharper board photo: BwBwJosbQkg gives levels
against the peak only); the resonance's asymmetry (the LPF ringing alone at a stated PEAK); the EG
time pots' bottom (two units, both long at 0.15-0.2 of the travel; a third with a close view of the
knobs); the MVCA cell at low light (an MVCA patch with EG 1's DELAY at 0); ESP C5 (a KLM-129 photo
with legible markings). Searched 2026-11-24: about 65 YouTube queries in nine languages, Flickr,
Reverb listings, repair blogs and threads (`private/ms20/reference/youtube/SOURCES.md`,
`private/ms20/docs/photos/boards/SOURCES.md`).

## Tried and dropped

- 2026-11-27 (CPU, Rev 2): the VCA's linear-mode tolerance at 3e-4 or 1e-3 V (the default 1e-4):
  its renders within -84 dB of round 14's, but only 5 % off the VCA block (its windows at the
  floor).
- 2026-11-27 (accuracy round 14, Rev 2): VR2 fully over (gain 5.55) with 1S1555s: the scream's level
  within 1 dB but 5-15 dB too bright above 5 kHz mid-sweep (the loop clipping hard); the later
  diodes with VR2 at 0.4: right level and brightness, but self-oscillating freely, so the whistle
  glides where the recording's steps; R38 3.3k as printed: 6-8 dB too bright and squashed (crest
  4.0-4.8 dB against 5.8) at any VCO levels; R38 10k: as clean as 33k; R9 4.7k: no change.
- 2026-11-27: a second reference for the screaming LPF, a stock Rev 1 (KORG35) unit
  (BwBwJosbQkg 653.6-657.2 s, a hand sweep up over two saws an octave apart). Its settings off the
  video (LPF PEAK 9.5, HPF CUTOFF 2.5, VCO 2 0.5 Hz sharp of the octave) put every band within
  1.5 dB of ours with LPF PEAK at 9.9: ours gives a little less resonance near the top of PEAK
  than this unit at its marking (within knob-reading error and pot spread; left). Not the
  recording (the same video's open-filter scenes match within 0.4 dB). What is left: the first
  0.6 s of the sweep (cutoff about 450 Hz) 2-4 dB brighter than the real above 2 kHz and its
  resonance 2 dB weaker. Tried: the KORG35's junction capacitances (C_ob 3 pF, C_be 8 pF across
  Q1-Q3): nothing moves. D20/D21's knee: IS 1.34e-8 N 1.6 (softer, same clamp): 6-14 dB darker,
  resonance 8-12 dB weak; IS 1.0e-9 N 1.3 (later): the stock unit at its read PEAK 9.5 within
  1 dB everywhere but scene 21 (the modded unit) 6-8 dB bright, resonance 7.5 dB loud; IS 5e-9
  N 1.6 (softer and later): the stock unit at 9.9 within 1.1 dB, scene 21 as now: not heard as
  different. The units disagree on the knee: unit spread.
- 2026-11-25 (accuracy round 13): the LPF limiter's knee later (IS 5.5e-9, N 1.949): the resonance
  2-5 dB over the recordings', 3-4 dB more above 2 kHz; IS 8.0e-9, N 1.949: levels right but the
  resonance rang 25 dB from edge to trough against the recording's 17 (dirty); N 1.0-1.3 with a
  later clip: free lines in scene 21 but the resonance 4-9 dB loud. PEAK 9.0 in scene 21 against
  the pull (wobble 3.0 to 2.1 dB, 3.5 dB more highs): not better by ear. R101 at 2.2k, 3.3k, 3.9k
  (re-trimmed by 7-4 each time): the pink-noise rings and the HPF's self-oscillation level fit at
  3.9k, scenes 20 and 22 go 4-5 dB rms out. The 2SC1623's BR 2-20 (at a matched pitch) for the
  HPF's knob law: the shape barely moves.
- 2026-11-24 (CPU): the keyboard block on the sparse solver: its offset-trim VCVS (EOS5) leaves a row
  with nothing on the diagonal, so the analysis falls back (as Rev 2's followers did). The flips in
  eighths: above (Plan 3). Rev 2's expo pair and servo in a block of their own at the host rate
  (5 of the filters' 23 ports): not tried, as it would sample an audio-rate cutoff (a VCO into EXT
  CUTOFF) at the host rate, where Rev 1 takes it at 4x-8x.
- 2026-11-24 (CPU, the keyboard's spike), before round 5's form: (a) every junction above its knee
  coming down taken to its linearised current's voltage: 1 % fewer iterations; (b) with those the
  network wants off sent to half the knee: the step at 3.10 s from 360 to 165 us, but note-48k slept
  2 % of its time instead of 8 % (a junction conducting at the step's start, sent off, can settle a
  latch or Schmitt trigger on its other solution: round 5 leaves those to Newton); (c) half steps on
  a residual's two-cycle: +-7 %; (d) substeps ahead of a key change: worse; (e) a longer trial
  (12-24 iterations), everywhere or after a control only: bass notes 0.3-0.37 cents, no faster once
  (a)-(b) were scoped; (f) the keyboard at 1x: worse (0.90 to 1.00x); (g) any of it in every step,
  not only after a control: +3 % on average. The KORG35s' linear tolerance 3e-6: 3.5-15 % cheaper,
  1.3 cents on self-oscillation: declined below.

- 2026-11-24: the supplies' ripple (§7-1: at most 2 mVpp on each rail, the 7815's own ripple
  rejection puts its +15 V nearer 0.3 mVpp). Measured by rippling every rail together at the check's
  limit, 100 Hz: a 100 Hz line at -84 dBFS at SIGNAL OUT and sidebands at f0 +-100 Hz 77 dB under a
  held note's fundamental; idle, under -120 dBFS (it still sleeps). Under the unit's own hiss (-62
  dBFS in a recording's gaps) and below hearing at any sane level; done properly the rails would be
  every netlist's inputs. Not modelled.
- 2026-11-24: the solver against ngspice beyond the self-oscillation's pitch: the F-V converter's
  +0.6-1.1 % (our comparator and op-amp models trip it 0.1 V later; neither is the real unit, and it
  runs only with a cable from F-V CV OUT) and the VCO shapers' 0.1-0.2 dB at 13-20 kHz (two-point
  polyBLEP/BLAMP; four points would take it to about 0.02 dB). Both left: under any recording's
  reach.

- 2026-11-15: the VCO cores in the netlist: the solver can't step through the PUT latch (every
  sample fails at the trip, at 1.5 and 6 MHz), and its 0.3-0.5 µs reset needs MHz rates. C++ cores
  driven by the netlist's charge currents instead (`circuit/vco.md` §9).
- 2026-11-15: the cores' live ramp fed back into vcoctl.cir: a 6 V step into the converters' loops
  at every reset; Newton failed every sample (10x real time). Held at the ramp's middle (the
  current moves under 0.005 % over the ramp).

- 2026-11-15: substeps ahead of a fast input on vcoctl.cir (a CV step over 20 mV a sample): the
  worst block got worse (note-48k 124 -> 150 %, scream-48k 763 -> 880 %) and the output moved
  (-11 to -44 dB against the golden renders). Reverted.

- 2026-11-15: the VCO converters' op-amps (IC3, IC7) at the TL081's 3 MHz: after a keyboard CV step
  the log converters' loops fail Newton on about half the samples at 48 kHz, and a 64-sample block
  took up to 10x its duration at a note-on. At 300 kHz (`gbwconv` in vcoctl.cir): no failures,
  worst blocks 1.2-1.8x; the charge currents agree with ngspice's 3 MHz run to 0.005 cents from
  1 ms after each step; the golden renders move (-10 to -36 dB at the worst sample: the free-running
  VCOs' phase, as any change in a step's first millisecond moves it). Decided 2026-11-22: 300 kHz
  ships; the 3 MHz part (`.param gbwconv=3e6`) waits on a solver that converges through the step.

- 2026-11-22: giving up a Newton solve when plain iterates come back bit for bit (an exact cycle,
  bit-exact by construction): vcoctl's stalled iterates wandered in their last bits and never
  repeated. Rows of Newton's matrix equilibrated by powers of two (M's condition near 1e14 in
  vcoctl at 44.1 kHz): the solve 1000 times more accurate on random right sides, but the stall
  stayed (the cause was the residual's cutoff) and every block's rounding moved. The sparse Newton
  on mg.cir (15 ports, 50 % slower), eg1.cir (no gain) and kbd.cir (its network doesn't fit).

- 2026-11-22: the 339's flips timed within the sample (as the op-amp comparators', sixteenths):
  the F-V's means went +1.06 / +0.55 -> +2.1 / +1.3 % against ngspice (the rest is the model, not
  the grid). The gain stages' poles at theta 0.6 (between): within 5 cents to CUTOFF 9 but locked
  to 24 kHz at 9.5 (+28 cents), for 50 % more instructions; 0.7 the same lock. Re-entering linear
  mode sooner after short runs (kLinPays 1-16): no gain.

- 2026-11-24: a three-point (parabolic) Newton predictor in place of the straight-line one, for
  the LPF's self-oscillation at 8x: instructions everything-44k +0.25 %, selfosc -0.5 %, scream 0 %
  (the straight line already lands within Newton's first step there). Reverted.

## Declined

- 2026-11-24: the KORG35s' linear-mode tolerance from 1e-6 to 3e-6 V (3.5-15 % cheaper; the
  self-oscillation's pitch moves 1.3 cents).
## Done

- 2026-11-27, accuracy (round 15): **the Rev 2 VCA's level.** After round 14 (R38 4.7k) Rev 2 stood
  9-11 dB over Rev 1 on the bench's matching workloads (scream +10.0, everything +11.8, selfosc
  +8.8), over the specification's 2 Vpp; the recording has them level (scream -0.2 dB, arpeggios
  -2.1). The VCA pair's base resistor 1.2M (drawn ranks 240k/330k; fitted, decided 2026-11-27):
  Rev 2 against Rev 1 -0.1 dB on the scene, the squash (crest 5.4 against the recording's 5.8),
  whistle and bands as round 14's within 2 dB (the top octave 1.7 dB brighter). 330k: +9.3 dB;
  560k: +5.6; 820k: +2.9; 1.5M: -1.6.
- 2026-11-27, UX (round 3): **SERVICE** on the bar opens a card with CIRCUIT, REV 1 or REV 2 (was the
  FILTER pill), as the other machines keep a unit's circuit in SERVICE; **the host's audio on the
  panel's jacks** as the Minimoog's INPUT EXT. SIGNAL: a click on an empty ESP SIGNAL IN or EXT
  SIGNAL IN plugs the cable from outside in there (out of the other one), a click on it pulls it;
  the status line says where it is (was the SIGNAL IN pill). The key list and jack tips say so.
  plugintest checks both.
- 2026-11-27, accuracy (round 14, the Rev 2 HPF): **VR1 0.3** (was 0.5, mid travel), FCA ADJ
  re-trimmed by 7-4 (vr7b 0.5209 -> 0.5347). BwBwJosbQkg's Rev 2 HPF sweeps (captions "MS-20 REV2";
  HPF PEAK 8 at 806-809 s and 9.5 at 820-824 s, LPF open, PEAK 2): at the top of the sweep the real
  Rev 2 keeps oscillating, 10 / 9.5 dB under the body before the sweep at 18.7 / 18.1 kHz (its Rev 1
  goes 40 dB down, as ours); ours with VR1 0.5 23.5 dB under at PEAK 8 (not oscillating) and 4.9 at
  9.5; with 0.3, 6.6 and 6.0 dB under at 19.1 / 18.4 kHz (the rest hangs on the uploader's VCO
  levels). Through the sweep at PEAK 8, the whistle's share of the harmonics' energy (0.8-6.4 kHz)
  within 0.8 dB of the real's (was 3-5 dB under); at PEAK 9.5 as before (within 1 dB to 6.4 kHz).
  The Rev 2 LPF scene (363) refitted: within 0.5 dB a band, the whistle 1 dB lower against the body.
  Rev 1 bit-exact against golden-30; golden renders `build/ms20-golden-31`; SPICE 78/78; corners
  0 failing (`--rev2` 2 KNOWN); service checks, plugintest, `--panel`, the host test, `threads`,
  `sleep` pass.
- 2026-11-27, CPU (after accuracy round 14, Rev 2): measured, nothing changed. On a quiet machine
  (load average 3, best of 5 per block, the round-13 and round-14 builds back to back, twice):
  everything-rev2-44k 45.0-45.2 % -> 48.4-48.6 % over both threads, worst block 0.89x -> 0.93x
  (under 1x: the 1.23x of the loaded run was the load); scream-rev2 39.6-39.8 -> 43.9-44.4 %;
  selfosc-rev2 27.7-28.4 -> 33.1-33.2 %; Rev 1 the same. Where: the VCA pair, driven harder through
  R38 4.7k, now leaves its linear-mode windows every sample (relinearised 1.56 times a sample, 118 ->
  356 ns a sample; its windows sit at the 0.1 mV floor, so the tolerance doesn't move them), and
  the LPF, its oscillation larger before LIM307X3 clamps, runs Newton more (selfosc 2.09 -> 2.97
  iterations a step, linear 32 -> 9 %). Both are the new sound's own work.
- 2026-11-27, accuracy (round 14): **the Rev 2 filter against a recording** (above): D8-D13 as
  `LIM307X3`, VR2 0.55, R38 4.7k, FCA ADJ re-trimmed by 7-4 (vr8b 0.5212 -> 0.5488). Against the
  recording (VCO levels 7 and 3, PEAK 9.5): the whistle against the body -0.3 dB (before -4.7, the
  recording +0.1), every band 50 Hz-15 kHz within 0.4 dB (before up to 9.8), crest 5.5 dB (7.6;
  5.8), peak symmetry 0.93 (0.79; 0.95). Rev 1 bit-exact against golden-28; golden renders
  `build/ms20-golden-30`; SPICE 78/78; corners 0 failing (`--rev2` 2 KNOWN, as before); service
  checks pass; plugintest, `--panel`, the host test, `threads`, `sleep` pass. CPU (load average 21,
  back to back against the round-13 build): everything-rev2-44k 47-51 % -> 55-59 % over both
  threads, its worst block 0.96-0.97x -> 1.23x (a key change at 3.6 s); selfosc-rev2 28-32 -> 38 %;
  scream-rev2 the same. Scene 363 (`private/ms20/reference/scenes/rev2-scream`).
- 2026-11-25, accuracy (round 13): one unit's EG 2 sweep and resonance (g6OfcNTvJow), and the
  scenes. **R161 33k** (printed 1K) with the EG 2/EXT pots' 50k load in `eg2.cir`: TO VCF at 0.6 of
  the drawn level, which three sweeps on two filters at two SUSTAIN settings want. **D20/D21 on
  1S1555LIM** (IS 2.9e-9, N 1.3): the resonance's level along scene 20's sweep within 1.6 dB and
  how steadily it rings between the oscillators' edges (scene 20: 13.6 dB from edge to trough
  against the recording's 11.5, was 18.2; scene 21: 18.5 against 17.4, was 24.6), 1.0-1.6 dB more
  above 2 kHz than the recordings (the earlier fit's 3-4). **VR8 0.3370** by 7-4 after it (was
  0.3316). **Asleep MG:** a turn more than 2 samples from its flip (a fast MG's short half, near a
  ramp: 2.09) dropped the cycle and left the MG stopped through the sleep (15-48 % of a cycle out
  on waking); the limit is now kMgEarly (4), and the cycle's length is refined over 4 cycles
  (`ms20bench sleep` within 0.004 % a cycle at five sleep starts; before, 0.011-0.015 % at MG 10,
  WAVE 0.5 at some). Scenes (private): VCO 2 PITCH from each recording's detune (two scenes beat
  24 cents where the recordings sit within 3-4), per-unit HPF/LPF trims, scene 21's vibrato from
  its audio and ours there pre-rendered from inside a held note. Golden renders `build/ms20-golden-28`
  against golden-27: VCO pitch unchanged; levels note 0.28 dB, bass 0.88, scream 0.15, everything
  0.85, selfosc 0.96 dB; Rev 2 under 0.02 dB. CPU (`ms20bench time`, load average 4.5):
  everything-44k 41.8 % on the audio thread, 49.4 % over both. SPICE 78/78; corners 0 failing, 0
  KNOWN, 0 unconverged (`--rev2`: 0 failing, 2 KNOWN); service checks pass; plugintest, `--panel`,
  the host test, `threads`, `sleep`, `boot` pass; auval passes.

- 2026-11-24, CPU (round 7): the LM13600s' output clamps inside the OTA element (`vop`, `von`,
  `iscl`: the same diodes to 13.6 / -13.8 V, no ports of their own; Newton limits the output against
  each clamp as a junction, a port class of its own; written out for ngspice as the diodes were).
  Rev 2's blocks 27 to 23 ports: instructions scream-rev2 -12.2 %, everything-rev2 -11.9 %,
  selfosc-rev2 -9.5 %; against golden-26 under -38 dB, pitch within 0.03 cents (the same equations,
  Newton's path); Rev 1 bit-exact. SPICE 78/78, corners `--rev2` 0 unconverged (without the limiting
  the clamps left 27 overload cases unconverged).

- 2026-11-24, CPU (round 6): Rev 2's filters at a linear-mode tolerance of 3e-5 V (was 1e-5;
  decided over 1e-4, which saves 14-23 % but moves the self-oscillation 2-3 cents). Instructions
  scream-rev2 -8.9 %, everything-rev2 -6.5 %, selfosc-rev2 -1.9 %; against golden-25 the
  self-oscillation's pitch 0.18 cents, levels under 0.003 dB; Rev 1 bit-exact. Corners `--rev2` 0
  unconverged.

- 2026-11-24, CPU (round 5): the step after a control moves (a key's contact, a switch;
  `Circuit::m_afterControl`, cleared for the operating point). A junction that was off when the
  step began and that Newton drove past most of its knee comes back down at once: to the voltage
  that carries the step's linearised current, or, where the network wants it off, where it began
  (Newton's own step there is vt an iteration; `limitJunction`). A limiter port's step that undoes
  its last, both over a knee, is halved (Newton's two-cycle between the clamp at 4 knees, where the
  tanh is flat, and the other side). Junctions conducting at the step's start are left alone (a
  latch keeps its state; note-48k sleeps as before). Key changes in everything-44k: 725 to 487 and
  695 to 631 iterations; worst 64-sample blocks everything-44k 0.91 to 0.85-0.89x,
  everything-rev2-44k 1.12 to 0.96-0.98x (Rev 2 under real time); instructions -0.1 to +0.6 %. Not
  bit-exact (decided: commit): bass notes up to 0.3 cents, old and new each within 0.15 cents of the
  keyboard at 8x (the solver's own error at 2x); the rest under -92 dB. SPICE 78/78, corners 0
  unconverged.

- 2026-11-24, UX: each instance its own hiss, noise and drift (`Machine::setSeed`, a new seed at
  each load and power-on, as the Minimoog's); plugintest checks two instances apart and fixed
  seeds alike. Golden renders exact (the tools' fixed streams).

- 2026-11-24, accuracy (round 12): **drift** (above): the cores' charge currents wander with a
  board temperature walk and each oscillator's own, linear and through each converter's exponential
  part. Golden renders against golden-23: within 0.1 cents on note and bass, under 1 cent on scream,
  everything-44k's clean notes +0.1 to +0.8 cents (its heavy modulation: the exponential part);
  selfosc exact (no VCO). The converter's probe vb renamed vblog (the VCA's probe vb shared its
  board signal). CPU: two exp2 a sample, nothing measurable. `Machine::setDrift(false)` for tools
  that compare two machines across a sleep.

- 2026-11-24, accuracy (round 11): **the Rev 2 filter board** (above; `circuit/klm307.md`). KLM-307
  transcribed from its clean drawing (every value legible; ten doubts listed), the main board's
  changes from the later KLM-127 drawing; `hpf307.cir`, `lpf307.cir`, `vca307.cir` as drop-ins with
  the same names; 20 SPICE cases within 0.5 %. The LM13600 reduced for the solver (37 ports to 27,
  half the time; a VCVS follower kept the sparse solver out), linear mode at 1e-5 V on these blocks:
  Rev 2 workloads from 2.5-4x Rev 1's CPU to 1.2-2.3x (above). `Machine::setFilterRev`, the bar's
  FILTER pill, the session's `filterRev`; §7-4 for Rev 2 in `ms20service` (calibrated, 46/46);
  `--rev2` in `ms20render` and `ms20corners`; Rev 2 workloads in `ms20bench`. Six A/B scenes (67-72)
  from BwBwJosbQkg: VCO 1's saw, VCO 2's saw and square on its Rev 1 and Rev 2 units, notes from the
  recording. Rev 1 bit-exact against golden-22. R38 33k (decided). Not settled: R9, VR1/VR2.
  Also measured and left (Tried and dropped): the supplies' ripple, the F-V converter, the VCO
  shapers' top; the self-oscillation's pitch was already within a cent to 10.7 kHz (above).

- 2026-11-24, accuracy (round 10): the data search and what it settled. Two new sources: board
  photos (a Rev 1 KLM-127 at 1024x768, KLM-127C, F and G boards; `private/ms20/docs/photos/boards/`)
  and BwBwJosbQkg (a Rev 1 and a Rev 2 unit side by side on one tripod shot, every demo captioned).
  **VCO 2's saw through R62 100K** (`core/vco.cpp`): fitted on real boards, missing from the circuit
  diagrams; VCO 2's saw against its square -3.8 dB (H1) where three units give -3.6 to -4.6 (was
  +4.7). **The VCOs and MG run on while asleep** (`Machine::wakeUp`, above). R101 4.7k confirmed by
  a photo (likely). R169 kept at 4.3k against one unit's sustain (above); the EG time pots kept
  against a second unit's times (Pot tapers). Golden renders against golden-21: scream and
  everything-44k exact (no VCO 2 saw, no sleep in them); note, bass, selfosc within 0.04 cents and
  0.0003 dB (the VCOs' phase after a sleep). CPU: none awake; asleep, the MG alone for its first
  cycle (0.16 us a sample; 0.11 s at FREQ 5, 11 s at FREQ 0), then nothing.
  Not settled: R169, ESP C5 (no legible KLM-129 photo), Q19's rank (two Rev 1 units carry a red to
  red-orange dot: orange is the table's nearest; the model has orange/yellow), the MVCA at low light,
  the filters ringing alone at a stated PEAK.

- 2026-11-24, CPU (round 4): the control side on a second thread (Plan 3 (a), decided "build it").
  For a stretch of a render call (up to 256 samples; its keys, controls and input known before it
  starts, so no added latency) a thread of the machine's own runs the control group (keyboard,
  trigger, EGs, MG, MVCA, S&H, ESP, the VCOs' converters) through it, publishing each sample's
  signals that the audio path reads into a ring (unpatched: EG 2, EG 2 TO VCF, the MG, the VCOs'
  charge currents, WHITE: six doubles); the caller runs the audio path (VCO cores, mixer, KORG35s,
  VCA, decimator) for each sample as soon as it is published, on its own copy of the board's
  signals (`Board::beginSide` / `stepSide` / `endSide`). Each side does what one thread does, in the
  same order. The caller runs the control side itself until it sees the thread has woken and
  claimed the job (1-2 samples), then hands it over, so a late wake costs nothing
  (`Machine::renderSplit`). One thread (the old loop) when a running control block reads a signal
  the audio group makes (a cable from SIGNAL OUT into the control side: `Board::splittable`, worked
  out at each wiring), while the rails move (POWER's first 13 ms, the run-down), when sleep could
  start within the stretch (no key down and the 100 ms of quiet within reach), for stretches under
  16 samples (MIDI events cut blocks), with `MS20_THREADS=1` or `MS20_HIST`, on one core. The thread
  (`core/worker.h`): made at `prepare`, ended with the machine, asleep on a semaphore between jobs
  (posting a job and handing it over take no lock and allocate nothing), the caller's floating-point mode with each job (the
  host's flush-to-zero: the two threads round alike), at the audio QoS; in the plugin it joins the
  host's audio workgroup (`audioWorkgroupContextChanged`, as the TR-909's workers). Bit-identical:
  golden renders exact against golden-21 with two threads and with one; `ms20bench threads` (new)
  renders 16 cases both ways (the workloads; five patches it splits, two it doesn't; cables in and
  out while playing; POWER's boot, run-down and boot; sleep and wake; blocks of 1-1024 samples) and
  compares output, capacitors and solver counts: all exact, also under ThreadSanitizer (no reports;
  plugintest passes under it too); `ms20corners` the same both ways; `ms20bench boot` the same as
  the round-3 build. Best of 6 interleaved rounds against the round-3 build (load average 4-8):
  worst 64-sample block everything-44k 1.125 -> 0.944x (-16 %), scream 0.975 -> 0.776 (-20 %),
  selfosc 0.926 -> 0.791 (-15 %), note 0.891 -> 0.794 (-11 %), bass 0.814 -> 0.743 (-9 %); share
  of a core on the audio thread everything-44k 49.2 -> 43.2 %, note 21.1 -> 17.3, bass 22.5 -> 16.1,
  scream 24.0 -> 18.8, selfosc 18.1 -> 14.4; over both threads (getrusage) 50.8 % (+3 %), note 22.3
  (+5 %), bass 25.8 (+15 %: its control side is nearly as heavy as its audio path, and the caller
  waits on it 18 % of the run), scream 24.5 (+2 %), selfosc 19.2 (+6 %). Instructions
  (everything-44k, `/usr/bin/time -l`) 57.75 -> 58.8-59.1 G (+2 %: the waits and handovers), wall
  2.51 -> 2.19 s; one thread 57.75 G as before. Per block (everything-44k, fastest of 7, under load):
  the note events' blocks where EG 2, EG 1 and the LPF spike together take 45-55 % less (1.65 ->
  0.68x, 1.47 -> 0.84x); the first block of each key change, where the keyboard block's 0.8 ms
  makes the control side the longer of the two, 11-18 % less, and is now the worst: hence -16 %, not
  the plan's -30 % (Plan 3). Service checks 44/44, `ms20corners` 0 failing, plugintest and `--panel`, the host test
  pass.
  Not compiled yet on Windows or Linux (the worker's semaphores and floating-point mode): the ship
  gate's Windows build is its first; auval passes.

- 2026-11-24, UX (round 2): POWER boots from cold and runs down (Behaviour and features), as the
  SH-101, TR-808 and TR-909 do; a plugin or session load stays warm. The solver scales its constant
  sources and op-amp swings with the rails (`Circuit::setConstantScale`, `discharge`), the board
  passes them on (`Board::setSupply`), `Machine::switchOn` / `switchOff` step the rails per sample
  rising and every 32 samples falling (so linear mode runs between); `powerOn` / `powerOff` stay
  the warm path the tools use. The trigger's Schmitt starts high; below 4.5 V the run-down stops
  the board. Boot: rails at ±15 V in 13 ms, SIGNAL OUT within 1 mV of a warm start's in 0.26 s, the
  capacitors within 10 mV in 1.2 s, every steady signal within 1 mV in 3.5 s; thump 1.14 V peak at
  VOLUME 7 (EG 2's HOLD timer fires it), 0.16 at VOLUME 0.5; worst block 1.66x real time at 48 kHz,
  1.70x at 44.1 (the first, while the rails rise; 10 / 8 blocks over, 0.33x after 20 ms), with
  `everything-44k` at its usual 49.8 %. Run-down: exact zeros after 0.21 s, worst block 1.09x.
  0 unconverged solves (and none in 48 run-downs and boots: 8 settings, 44.1-96 kHz, a key held
  or not). `ms20bench boot` measures it; plugintest's POWER checks: off runs a note down to exact
  zeros, on boots (not a warm start, within 1 mV of one by 0.5 s, a note 2 s on within 0.5 dB,
  asleep with exact zeros once settled), a session load neither boots nor runs down. Golden
  renders exact against golden-21; plugintest and `--panel`, the host test, service checks 44/44,
  `ms20corners` 0 failing pass.

- 2026-11-24, accuracy (round 9): FM depth to the specification (decided): R43 / R44 at 117.8k for 1.00
  octave a volt, IC15a's swing ±12.5 V for a 5 Vpp MG (FM depth, above). The MG ramp at FM 7.4: 3081 ->
  2422 cents (recording 2400); at 2.0 through the MVCA ±74-75 -> ±57 (±55-57). Golden renders against
  golden-20: note, bass, selfosc within 0.03 cents and 0.0003 dB; scream 0.17 cents (its cutoff
  wobble 1 dB smaller); everything-44k's FM vibrato and EG 1 sweep 2.4 dB shallower. Service checks
  44/44, SPICE suite 58/58, `ms20corners` 0 failing, plugintest, the host test and auval pass. Golden
  renders `build/ms20-golden-21`.

- 2026-11-24, UX (round 1): the panel against the shipped machines (an audit of 14 items, the
  SH-101 the reference). The held status in the accent colour; the control tooltip is the host's
  parameter name and value text, under the control; VCO 2 PITCH and MASTER TUNE print -5..+5 as on
  the panel, VOLUME "POWER OFF" in its click; the control wheel's position saved with the session;
  rotary switches step clockwise and wrap, shift- or right-click back, a drag picks; sliding along
  the keys plays them; the MOMENTARY SWITCH a panel key (key 1, latchable, Escape lets go);
  tooltips on the wheel, the switch, VOLUME's click and every jack (what's plugged, what a click
  does); Escape cancels a cable being drawn; the octave in the status, ", asleep" gone; the key list
  in the shared layout; "SIGNAL IN: OFF / ESP / EXT" with shift-click back and a warning when the
  host's input is off; an older artwork pack's outline drawn; sharp art at 9.6 px/mm; Shift-fine 1/5
  and the wheel's step 0.08 as elsewhere; the static panel cached in an image. A session load
  settles the machine at the session's settings, as a plugin load does (before, the board stepped
  from the defaults: a 0.71-peak thump with the VCA held open), and `start()` sets the wheel's and
  MOMENTARY's jacks before it settles. Golden renders exact; plugintest (new session, value-text
  and panel-mouse checks), `--panel`, the host test, `ms20corners` (0 failing) and auval pass.
  Open: POWER (Behaviour and features), a MIDI CC map.

- 2026-11-24, CPU (round 3): bit-exact against golden-20 on every workload. The sample loop's seven
  board inputs set by index instead of by name (a map lookup each, 2 % of everything-44k's samples);
  the noise board idle unless VCO 1 takes WHITE or a cable takes WHITE or PINK (it ran every sample;
  `settle()` still solves its operating point, so it starts settled when switched in). Instructions
  (`/usr/bin/time -l`, the machine under other sessions' load): note 17.60 -> 16.28 G (-7.5 %), bass
  23.87 -> 21.94 (-8.1 %), scream 25.57 -> 23.73 (-7.2 %), everything-44k 60.11 -> 57.94 (-3.6 %),
  selfosc 15.32 -> 13.88 (-9.4 %). What's left: Plan 3. Service checks 44/44, SPICE suite 58/58,
  `ms20corners` 0 failing / 8 KNOWN, plugintest, the host test and auval pass.

- 2026-11-24, accuracy (round 8): more readings off the videos (atoooy parts 1, 2, 4-6; Kick.S's MG,
  EG and filter-envelope videos, fetched): MG rate at 18 FREQUENCY settings on three units, FM
  depth, EG 2 times, the delayed vibrato, VCO 2's saw. Changed: (1) the MG's VR25 starts its track
  0.91 of a knob unit from the stop: atoooy's 13 readings within 6 % RMS (before 31 %; 1.39 -> 1.13
  Hz against 1.06, before 2.79), the top and FREQ 0 unchanged; (2) the noise source behind rz3 1k at
  4.6 µV/√Hz: WHITE against VCO 1's saw within 0.1 dB (before 1.6 dB under), its top within 0.6-1.3 dB
  at 6-13 kHz (before 2.5-4.9 dB short), PINK 1.75 dB over WHITE (before 6.25; SPEC: equal). Found
  not to be the model: round 7's "vibrato 0.5 s early" (the scene's 30 ms key-ups; MVCA, above).
  Open: FM depth (a decision), the EG time pots' bottom (Pot tapers). Golden renders against
  golden-19: note, bass, selfosc within 0.005 cents and 0.0001 dB; scream 2.1 cents and
  everything-44k 450 cents per note (the MG's rate at FREQUENCY 3 and 7: vibrato and cutoff wobble at
  another phase). CPU: no change to any block's work (bench under a load average of 35, not
  comparable; next CPU round). `ms20corners`: "HPF cutoff 10.0 peak 10 no input" at 44.1 kHz joins
  KNOWN (aliasing: 0.222 with the adaptive rates, 0.125 at 8x throughout, 0.000 at 16x;
  `MS20_FILTER_OVER` forces the KORG35s' rate for such checks); 0 failing. Service checks 44/44, SPICE
  suite 58/58, plugintest, the host test and auval pass. New A/B scenes 60, 62-66 (FM ramp, delayed
  vibrato, EG 2 times, VCO 2 saw alone, the two saws in unison, S&H noise into FM); 01-03 tuned
  (ours was 11 cents sharp); 20-22 at the read MG FREQUENCY 4.0. Golden renders
  `build/ms20-golden-20`.

- 2026-11-23, CPU (round 2): the KORG35s at 8x only where they need it (chosen: "Adaptive 4x/8x
  filters"). Each block's rate follows its pin-1 CV (8x above 2.45 V, about 6.5 kHz; 4x below
  2.15 V; 50 ms dwell); its delay is 3 samples at either rate (the board publishes the first of
  the two steps a sample late; `Circuit::retime` resamples the decimation's history at a switch),
  so the plugin's latency is fixed. Against the filters at 8x throughout (golden-17): per note
  within 0.15 cents and 0.01 dB (selfosc at CUTOFF 5 0.67 cents: 4x's self-oscillation pitch);
  no clicks at the switches (A/B sweeps 21, 30, 31, 46, 47: the worst 5 ms onset flux above 2 kHz
  as at 8x, 6.7 / 8.8 dB against 6.7 / 8.5; before `retime` 21 dB). CPU: note 25.3 -> 23.3 %, bass
  30.0 -> 24.8, scream 37.4 -> 27.2, selfosc 27.0 -> 20.4, everything-44k 52.4 -> 51.2 (its LPF stays
  at 8x). Service checks 44/44, SPICE suite 58/58, `ms20corners` 0 failing / 5 KNOWN, plugintest, the
  host test and auval pass. Golden renders `build/ms20-golden-19`.

- 2026-11-23, accuracy (round 7): every A pot (the EG times, PORTAMENTO, the ESP's CV ADJUST too) on
  the power law, at exponent 2.25 (`p2.25`; round 6 had the 100 kΩ pots at 2.5 and the rheostats
  on two segments), fitted to four readings (Pot tapers, above): a pluck's EG 2 fall -2..-12 ->
  -43 dB/s (recording -38), EG 2 DECAY 7.7's time 3.6 -> 2.6 s (2.7), PORTAMENTO 4.4's glides within
  10 %, FM depth 8.6 -> 2.4 dB over. Three A/B scenes were wrong, not the model: scene 42 steered
  the LPF instead of the HPF (`make_real_scenes.py` now takes the filter from the event), and
  retriggered notes had 5 ms of key-up, which the keyboard's trigger doesn't see (now 30 ms):
  scene 51 plucked once. CUTOFF's slope near the top: the readings don't contradict it (above).
  Golden renders against golden-16: 0.07-0.3 dB (the EG times in every workload). CPU: everything-44k
  52.4 %. SPICE: eg1_jack and eg2_hold_retrigger's logic edges land 30 / 120 us from ngspice's
  (0.015-0.02 % of their timings, the envelopes within 0.1-2.6 %): known. `ms20corners`: "LPF cutoff
  10.0 peak 10 no input" at 44.1 kHz joins KNOWN (aliasing, clean at 16x); 0 failing. Service checks
  44/44, plugintest, the host test and auval pass. Golden renders `build/ms20-golden-17`.

- 2026-11-23, accuracy (the round-5 leftovers): the panel's 100 kΩ A pots (VCO 1 LEVEL, FM MG/T.EXT,
  FM EG 1/EXT, both filters' MG/T.EXT and EG 2/EXT) on a power law, share = rotation^2.5 (the
  solver's new `p<gamma>` taper; two segments through 15 % at mid before), fitted twice
  independently (FM depth: 2.4; EG 2 DECAY's time: 2.6); the 1-2 MΩ rheostats keep two segments (EG
  1's delay and attack fit them). FM MG/T.EXT 2.0's delayed vibrato ±9 / ±120 / ±153 -> ±3 / ±38 /
  ±49 cents (recording ±9 / ±47 / ±57); EG 2's sweep of the LPF in g6OfcNTvJow 4.3 -> 0.9 kHz ->
  3.6 -> 0.9 kHz (recording 2.3-2.6 -> 1.0). R101 tried at 4.3k and 3.9k: one unit's HPF asks for
  it, another's against it; kept 4.7k (above). The closed LPF (scene 41) and scene 31's dip turned
  out to be the scenes' mappings; both fixed in `make_real_scenes.py`, what is left logged above.
  Golden renders against golden-15: note and selfosc identical, bass 0.11 dB, scream 0.02 dB,
  everything-44k 0.48 dB (their FM, MG and EG 2 amounts). CPU unchanged (everything-44k 54.5 %).
  Service checks 44/44, SPICE suite 58/58, `ms20corners` 0 failing / 5 KNOWN, plugintest, the host
  test and auval pass. Golden renders `build/ms20-golden-16`.

- 2026-11-23, accuracy (the KORG35s' resonance, from the real-recording A/B): the 2SC1623's reverse
  beta to the high end of what NEC's curves allow, BR 4.35 with NR 0.98 (was 2.665 / 1;
  Stinchcombe's library 4.21), the §7-4 trims redone (VR7 0.3305, VR8 0.3316 for 500 Hz at CUTOFF
  5, PEAK 10). Scored on the LPF netlist against three units: self-oscillation from PEAK 7.4-7.6
  (was 8.6; the units oscillate at PEAK 8, HPF and LPF); a full-level square's resonant harmonic at
  PEAK 10 -13.2 dB (recording -13.4 at 9-10; was -19.4, none below 9.9); the self-oscillation's
  H2-H5 bracketing the recording's between PEAK 9 and 10 (were 3-6 dB short at 10). BR 3.39 and
  1.72, the diodes' IS / 10 and / 100, C28 -10 % and R116 at 4.3k/3.9k were scored too: only BR's
  high end fits all three without a part the scans contradict. Its cost: the LPF self-oscillating
  above the audio band in `everything-44k` (CUTOFF 10, EG 2, PEAK 8) took Newton every step, 46 ->
  82 % of a core; the sparse Newton on the LPF (2.7x faster, the same iterations; renders within
  -148 dB but for the chaotic ultrasonic oscillation's phase) brought it to 43.7 %. The stronger
  oscillation near the top made the 4x rate's grit worse and added ticks (CUTOFF 10, PEAK 9.5:
  bursts to -41 dBFS), so both KORG35s now run at 8x (`Machine::kFilterOver`): -45 -> -70 dBFS of
  folded lines at PEAK 9, ticks -> a steady -78 dBFS at 9.5; 8x moves the audible workloads under
  0.02 cents / 0.01 dB. Against golden-10: note 0.22 dB, bass 0.34 dB, scream 1.73 dB, selfosc
  1.21 dB (the resonance). CPU (`ms20bench time`): everything-44k 46.2 -> 54.2 %, note 21.4 -> 28.5,
  bass 23.4 -> 30.1, scream 25.6 -> 38.0, selfosc 18.8 -> 27.5; worst block 1.08 -> 1.13x.
  `ms20corners`: "HPF 9 / LPF 10, both PEAK 10, VCOs" joins KNOWN (flatness 0.22-0.24 at 8x,
  0.25-0.31 at 16x: the circuit's own); 0 failing. Service checks 44/44, SPICE suite 58/58,
  plugintest, the host test and auval pass. A/B scenes 21, 30-31, 43-47 now follow the recordings
  (`private/ms20/reference/scenes/ab/`).

- 2026-11-23, accuracy (the MVCA, found in the real-recording A/B): klm128.md §3.3's CdS law was
  written but never run, so PPC1 sat at its dark 10 Mohm and MVCA OUT was about -62 dB: every patch
  through the MVCA did nothing (zijbJb7Z84Y's "violin" patch, MG -> MVCA -> TOTAL, gave no vibrato).
  `Machine::stepCds` now runs the law per host sample while MVCA OUT is patched (PPC1 set when it
  moves 0.1 %). That patch now fades its vibrato in over the note like the recording (depth: FM
  above). Golden renders `build/ms20-golden-10` exact (none patches the MVCA); service checks
  44/44, plugintest and auval pass.
- 2026-11-23, reference: 28 A/B scenes of real recordings against ours at the settings read off
  the videos (`private/ms20/reference/scenes/ab/`, built by `make_real_scenes.py` from
  `events/*.json`; sweeps whose knob is hidden follow the recording's tracked pitch through
  `cutoff_cal.py`). New: two vintage units' tutorials (QjZgZodB614, MFR9hY3CQhs), atoooy parts
  4-6. Findings logged above (resonance under a loud input, PEAK, EG 2's reach, FM depth, the
  self-oscillation's harmonics; YH3g01qU7O8's "self-oscillation" corrected).

- 2026-11-22, accuracy (the VCO shapers): the comparators' corners (slewing at 1 V/us between the
  rails: where they start, reach a rail or turn) get polyBLAMP corrections (`SlewComparator`'s
  corners, `blamp`). VCO 1's pulse at 8' / 4' C-4 (1 / 2 kHz): non-harmonic power -61.7 -> -75.3
  dB and -59.2 -> -68.1 dB re H1, the largest stray line -72.7 -> -88.6 dB; against the audio path
  at 16x the harmonics are within 0.06 dB before and droop 0.1-0.2 dB at 13-20 kHz after (the
  two-point kernel, as the saw's polyBLEP). Golden renders `-9` -> `-10`: only scream (VCO 2's
  square) and everything (VCO 1's pulse) move, level within 0.003 dB; no CPU cost. Service checks,
  corners, plugintest, host test, auval pass.
- 2026-11-22, accuracy (self-oscillation pitch, F-V converter):
  - The KORG35s' gain stages (IC11a, IC8b: gain 58, a 51 kHz loop inside the resonance) have their
    op-amp poles integrated by the trapezoidal rule (`trap` on the X line; the solver has a theta
    method per capacitor now, `theta=`, every other capacitor bit-exact). By backward Euler the
    pole lagged too little and self-oscillation ran sharp, converging first order. Against
    ngspice at 192 kHz (LPF / HPF, PEAK 10): CUTOFF 7 +5.9 -> +1.3 c, 8 +8.1 -> -3.7, 9 +88 ->
    -14.5 (HPF +92 -> -15.6), 9.5 +160 -> +5.2; the level at 9 0.362 -> 0.397 Vpp (ngspice 0.392).
    No ringing at Nyquist (the gain stage's alternating part about 1e-4 V). SPICE suite's filter
    cases closer (lpf_square_ringing 0.235 -> 0.043 %, hpf_sine_drive 0.297 -> 0.176 %).
    `ms20corners` KNOWN 6 -> 2 (both filters at the top clean now). VR8 0.4628 -> 0.4629. Golden
    renders `-8` -> `-9`: per note within 0.02 cents and 0.005 dB except everything-44k (level
    0.22 dB, its PEAK 8 resonance) and selfosc (0.3 cents). CPU: everything-44k 28.6 -> 46.5 % of
    a core (the LPF in linear mode 69 -> 18 % of its samples), note-48k +9 % instructions: budget
    raised to 52 % (decided 2026-11-22).
  - The F-V converter's C2 by backward Euler (`be`, a new capacitor option): against ngspice +9.1 /
    +14.3 % -> +1.06 / +0.55 %. esp.md's "rounded to whole samples" for the rest was wrong (3.07 MHz
    reads +1.4 %) and is corrected.
  - The SPICE suite had been running a stale circuitrun (`build/ms20` inside the clone, from
    2026-11-16) all day: rerun against the current one, 58/58 with every change of the day; it now
    takes the newer of the two places.
- 2026-11-22, accuracy (aliasing at the KORG35s' top): the HPF's and LPF's outputs are filtered
  from the quarters the solver holds them in (a 25-tap Kaiser sinc at 16x, flat within 0.011 dB to
  24 kHz, 66 dB down from 168 kHz; `Circuit::setDecimation`), and every sample comes three 4x
  samples late, filtered or not, so held and full-step samples agree (a sample whose window has
  none of the solver's quarters is the sample itself, delayed). The LPF alone at CUTOFF 10, PEAK
  10: the aliased 2.39 kHz tone -44.5 -> -85.5 dB. `ms20corners` KNOWN 11 -> 6 (the HPF alone at
  the top, MG into both EXT, EG 1 into TOTAL EXT clean at both rates; off the known list). The
  output is 1.5 samples later (`Machine::latencySamples`); against the old renders delayed by 1.5
  samples, the workloads differ by -55 to -61 dB at most (-72 to -87 dB RMS), self-oscillation
  -26 dB (its phase); per note pitch within 0.03 cents, level within 0.007 dB. CPU: instructions
  +2.1 % (note-48k), +1.5 % (everything-44k). A plain average of the held quarters (tried first)
  got the tone to -69 dB but changed the delay only while held. Service checks 44/44, plugintest,
  host test, auval pass. Also checked: jmvzCeaDUIQ (MS-20 FS, MK1 filter swept at PEAK 100 %)
  can't place the limiter's H3: a 66 Hz VCO drone under the sweep puts its harmonics in every
  band.
- 2026-11-22, accuracy (VCO 1's §7-2 bow, the release blocker): the cause measured (Early effect
  off: C-2 / C-3 +2.02 / +2.22 -> -0.24 / +0.22 cents; the dead time 0 or 0.3 us moves them 0.1-0.3
  cents; the core's netlist trip rise at low current would fix C-2 only and push 32' past ±10
  cents); no sheet value passes (VAF 31, the O rank's low-I_DSS JFET: C-3 +2.07). `ms20service
  calibrate` now does the technician's step after the procedure: a VCO whose C-1..C-4 miss ±2
  cents gets its high trim moved until the errors split. VR1 0.1152 -> 0.1128, VR2 0.7057 ->
  0.7054: VCO 1 -1.11, +0.95, +1.11, -1.08 cents; 32' +6.6 to +5.2, 4' -1.9 to -5.8 (all ±10).
  Service checks 42/44 -> 44/44. Golden renders `-6` -> `-7`: VCO 1 about 1 cent flat at C-1 and
  C-4, sharp mid-range by the same; no CPU cost. Corners, plugintest, host test, auval pass.
- 2026-11-22, CPU (the worst blocks): every workload's worst 64-sample block under its duration
  (before 1.17-1.88x, after 0.88-0.98x) and less CPU in every one (share of a core: note-48k
  22.5 -> 20.1 %, bass-48k 27.4 -> 22.7, scream-48k 27.5 -> 24.6, everything-44k 35.1 -> 28.6,
  selfosc-48k 20.1 -> 18.3; best of 5 under the same load). Three changes:
  - Newton's residual (np > 16) sums the currents that move a port by over 0.1 nV *as Newton
    judges it*, weighted (`Level::fColMaxW`). Unweighted, the cutoff ignored that an op-amp's
    limiter port counts its open-loop gain (2e5) times: a current right at the cutoff (IC3's
    input stage, 4.9e-10 A through 0.214 ohm) came and went between iterations, a residual of
    1.06e-5 against the tolerance of 1e-5 that Newton never got under. vcoctl's full steps failed
    at every try under FM at 44.1 kHz (8.6 % of `everything-44k`'s samples in quarters, 0.20 s);
    now none (0.04 s). The VCOs' charge currents come nearer a tight reference (tolerance 1e-8,
    every current): `everything-44k` median error 0.021/0.035 cents (IA1/IA2) -> 0.00005, worst
    0.26/0.46 -> 0.22; `scream-48k` unchanged. kbd.cir gains too (0.13 -> 0.08 s).
  - EG 2 on the sparse Newton (52 ports): its gates crossing their thresholds at a note event took
    a 64-sample block about 1 ms; now 40-70 % less. Rounding only (-156 dB; -114 dB self-oscillating).
  - Comparators: a sample goes to the sixteenths ahead of a flip when the input is about to cross,
    or comes within a linear range while the output can still move; an output wound up past its
    rail flips only once the input crosses. And the samples after it go straight to the sixteenths
    only if it did flip. The MG's IC15A spent about 25 samples in sixteenths before each flip at a
    few hertz (400-500 us in a block); MG 0.064 -> 0.030 s in `bass-48k`. The MG's period against
    the same netlist at 768 kHz: -0.021 -> -0.017 cents at 10 Hz, +0.007 at 20 Hz both ways.
  Golden renders `build/ms20-golden-4` -> `-6`, per note (`tools/bench/compare.py`): pitch within
  0.004 cents, level within 0.0006 dB; largest sample difference -66 dB (note, bass), -48
  (scream), -31 (self-oscillation), -22 (everything: the free-running VCOs' and MG's phase). Service
  checks, SPICE suite, plugintest, host test, auval as before; `ms20corners` 0 failing, one more
  KNOWN (MG into both EXT at PEAK 10, 48 kHz: flatness 0.240 against the 0.20 limit, the same case
  already KNOWN at 44.1 kHz, the aliasing at the top). `ms20bench`: the worst block is each
  block's fastest of the runs (another process's preemption doesn't count), `MS20_PROFILE=1` prints
  each slow block's blocks and the solver's counts, `probe NAME SIGNALS` prints signals a block,
  and `MS20_SLOW=<block>` its slow samples.
- 2026-11-16, solver (the class of bug that made the SH-101's top white noise): every circuit
  block now counts its samples taken in shorter steps, the samples it held, the shortest steps
  that needed the comparators softened, the samples that didn't converge even in 64ths (taken as
  they stood, partly advanced) and the operating points that didn't converge (`Board::settle`
  used to ignore them); `core/solver_stats.h`, in the processor's `display().solver`. The solves:
  the netlist solver's Newton (40 iterations, 8 for a trial in a block with comparators), retried
  in quarters, 16ths, 64ths, then with the comparators softened; its DC solve (Newton, gmin, gain
  and source stepping); linear mode's windows (three scalar root-finds capped at 12, 6 and 12:
  monotone, converged within 5, 6 and 7 over every input, 5e-6 relative at worst: left as they
  are). The VCO cores' event loop is exact. Over the controls' extremes nothing went unconverged.
  What did go wrong (LPF or HPF CUTOFF 9.5-10, PEAK 10): the KORG35 self-oscillates near 37 kHz,
  where full steps at 4x that converge lock onto a quarter of the rate (48 kHz), and the failing
  ones' 32 samples of quarters alternated with them (65 % of the samples), scrambling the
  oscillation into white noise in the audio band: -36.4 dBFS RMS at 48 kHz, -37.9 at 44.1, median
  spectral flatness 0.45-0.48, with no input. Now the KORG35 blocks hold (`Circuit::setHold`): a
  second failed sample within 256 keeps them in quarters until a full step, tried every 32 to 512
  samples, agrees with them to 1 mV (a trial that fails to converge ends a hold that hasn't seen
  one converge elsewhere, so stiff events step as before); what is left at the top is the
  aliasing logged above. A little less CPU there (instructions -5 % at 48 kHz, -7 % at 44.1 for
  a second of it). `ms20corners` (tools/corners): 150 cases of the controls' extremes, the patch
  panel's routings and the input, at 44.1 and 48 kHz, about a minute; it fails on any unconverged
  sample or operating point, noise with no noise source, or output out of bounds. With the old solver
  (MS20_NOHOLD=1) 10 cases fail and 13 trip the known list; now 0 and 10. With Newton crippled to
  3 iterations it fails 150 of 150 at 48 kHz, with no retries below quarters 288 of 300.
  `ms20bench` bit-identical to `build/ms20-golden-pre` in every workload; instructions -0.01 to
  +0.04 % (idle +0.16 %: start-up). Service checks 42/44 (as before), SPICE suite 58/58,
  plugintest and the host test pass.
- 2026-11-15, accuracy (first comparison, `docs/compare.md`): VCO 2's core swing 6.02 -> 6.80 V
  from a real unit's narrow pulse (duty 6.25 %: node 11 6.1-6.2 Vpp); VCO 2's §7-2 trims come
  inside their range (VR6 0.478, VR5 0.694) and every VCO 2 check passes (service 37/44 -> 42/44).
