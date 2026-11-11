# TR-808: what we know is wrong, and the plan

The working list. **Known inaccuracies** are what the emulation gets wrong or leaves out, as far as
the evidence goes; **Plan** is what to do next, in order; **Done** records what was settled and
where. Numbers and evidence live in `model.md` ("Validation", "Open points"), `circuit.md` and
`research.md`; keep this file short and point there. When an item is fixed, move it to Done with its
commit.

## Known inaccuracies

### Sound

| # | What | How big, against what | Likely cause / lead | Plan |
|---|---|---|---|---|
| S2 | Snare's snappy: strong at the top, short at mid (the SNAPPY law refitted at the 2x board, 2026-11-04: the centroid asks a lower law, the decay a higher one) | At a matched early level the snappy's -20 dB is 0.09 octave short at Fischer's mid marks (0.15-0.32 at Reverb's), right at full; matching his 7.5 and 10 marks needs rotation 0.62-0.67 and 0.80-0.93 (the law: 0.80, 1) | A smaller dead zone (Q47's V_BE, D54, Q48's saturation) would do both; not R194 (lengthens every setting), D54's knee alone, the knob law, the envelope and swing-VCA parts within tolerance, or C51's dielectric absorption. The archive.org unit's long snappy is R194 at 4.7M's (unit spread) | - |
| S5 | Rim shot's R318 | R318 at 165k (printed 220k) for the 455 Hz / 1.7 kHz balance the units agree on. (Its pitch is right by its period; its first peak, 1.8 ms, at the late end of the units' 1.1-1.7) | The swing VCA and the 589 Hz high-pass | - |
| S6 | Cymbal R125 fit | 3.55k against the printed 2.2k (+61 %); printed with the bands refitted is worse (objective 8.6 / 4.7). Reverb's unit's cymbal rings 1.6x as long as Fischer's at the same marks (unit spread), so a second fit would differ | Stands in for the bands' balance in time | - |
| S7 | Claves 5 cents flat at 88.2/96 kHz | Their clipped 2.4 kHz ring's discretisation; the fitted C117/C119 already put them on Fischer's pitch (0 cents), so a higher rate would move the fit, not the sound | Discretisation | leave |
| S8 | Toms' germanium diodes | The 1S188FM has no data sheet; the model is a guess the accent behaviour bears out. The second unit's LT has a 2nd harmonic at -37 dB (mismatched diodes) the model doesn't | A data sheet or a measured part | - |
| S9 | Rails idealised | Rails ideal (+B1 dips 0.1-0.2 V on a cymbal hit through R165); output swing per load only for IC2a | Rails as circuits | P7 |
| S10 | Not modelled at all | Hum (-80 to -106 dBFS in three sets, its source unknown); temperature drift ("some 808s need to warm up") | - | - |
| S11 | Partition sleep | A hat hit that wakes its VCA while the last fades differs sample by sample (-25 dB) from a never-sleeping run; envelope within 0.06 dB, spectrum 0.05 dB | Frozen coupling capacitors | inaudible; leave |

### Behaviour and features

| # | What | Detail | Plan |
|---|---|---|---|
| B1 | POWER's rails | On is the circuit (P7); the rails rise and fall at once, and off is a 5 ms fade | - |
| B2 | Factory rhythm tracks | Tracks 1 (rock) and 2 (latin) aren't loaded: no document lists their bars. The factory patterns are | Find a source |
| B4 | Trigger outs, footswitches, SYNC OUT | Not emulated. Trigger outs as MIDI out: not wanted (2026-10-26) | - |
| B5 | Other units | SERVICE chooses the board by serial range and the snare's and clap's portion (P9, done), on Fischer's unit's parts; the other real units are only their trims away (TM3: 0.26 in Fischer's unit, about 0.32-0.38 in the other three, `model.md`, "Unit spread"; Reverb's unit has the early snare: SNARE + CLAP at EARLY). The first units' cowbell trims can't reach 1.85 and 1.25 ms with C5 as drawn (its early value isn't known); the INT clock's HD14584 thresholds are a choice (`model.md`, "SERVICE") | - |

### Reference data

| # | Gap | Effect |
|---|---|---|
| R1 | Fischer's set: one hit per knob setting, no retakes | Raum 45's five takes a voice give one unit's scatter (2026-10-23): 0.3 dB on the snare's snappy balance, 2-4 Hz on the rim shot; the toms' noise tails: model against itself 2.8-3.3 dB LSD |
| R2 | Knob positions only at the printed marks | The pot laws between them are fitted blind |
| R3 | The second unit (archive.org): settings unknown, knobs moved between takes | Only accent ratios are usable |
| R4 | No documented accent sweep | Accents checked at one setting per voice; Reverb's pack (2026-10-23) has accented pairs at MIN/MID/MAX of the snare's knobs and the toms' TUNING, at one unknown ACCENT (0.75 by its toms) |

## Plan, in order

| # | Step | Fixes | Cost |
|---|---|---|---|
| P1 | ~~Buy reference sets~~: Raum45 *AchtNullAcht* (€9.99) and Cluster Sound CS-808 (€29). **Skipped** (2026-10-16): R1-R4 stay open, S3 and S6 are fitted to Fischer's unit alone, B5 is Fischer's unit or the nominal one | - | - |
| P2 | ~~Pot laws for TUNING, SNAPPY, DECAY, TONE~~ Done: knob laws from Fischer's marks (`tools/compare/marks.py`) | S2, S3 | - |
| P3 | ~~Snare: D54's small-current model; op-amp slew rate in the solver~~ Done: slew in; D54 isn't the lever | S1, S2, S9 | - |
| P4 | ~~Clap: the noise source's spectrum, IC21b again~~ Done: not the noise (the snare's snappy shows no tilt); the 2SC945's low-current gain from its data sheet, IC21b's caps and C138 within tolerance | S4 | - |
| P5 | ~~Rim shot's 300 Hz~~ Done: the measure (a Hann window on a 12 ms hit); R318 refitted to 165k | S5 | - |
| P6 | ~~Metal voices at a higher internal rate~~ Done: the metal group and the mixer at 2x the board | S7 | - |
| P7 | ~~Power: the muting circuit, the board started cold at power-on~~ Done; rails as inputs that ramp left (under the mute) | B1 | - |
| P8 | ~~Individual outputs as plugin output buses~~ Done | B3 | - |
| P9 | ~~Decision (2026-11-04): before release, a SERVICE view like the TR-909's, with the choice of board: the early or the late snare and clap ("PORTION CHANGED", `lateSnare`/`lateClap`), and Fischer's trims or the nominal parts~~ Done (2026-11-05) | B5 | - |
| P10 | ~~MIDI: CC mapping for the knobs~~ (done, the TR-8's numbers); trigger outs as MIDI out **not wanted** (2026-10-26) | B4 | - |
| P11 | Check the Windows build | - | Small |
| P12 | ~~Artwork places: the panel's marks as `ArtworkSpec::places`, the processor an `ArtworkUser`, as machines/tb303 does~~ Done (2026-11-05) | - | - |
| P13 | ~~Op-amp internal poles by the trapezoidal rule~~ Done (2026-11-04): the rim shot and claves only; every other voice measures the same either way | - | - |
| P14 | ~~The snare, clap and maracas at twice the board's rate~~ Done (2026-11-04) | - | - |
| P15 | ~~The snare's SNAPPY law again at the 2x board~~ Done (2026-11-04) | S2 | - |

## Budget

- **CPU:** `tr808bench`'s `kit` (a kit from MIDI: BD, SD, CH, OH) under 22 % of a core at 48 kHz on
  one thread (`TR808_WORKERS=0`): the last measurement plus about 10 %. Goal (2026-11-04): 20 %,
  reached by the 2026-11-05 CPU round on instructions (3002 M a second against 3770, -20 %, the
  output unchanged to the bit): 20.1-20.4 % of a core on one thread (CPU % moves 3 % from run to
  run), 16.1 % on the audio thread with the voices on workers (`docs/model.md`, "CPU"); every voice
  48 / 24 %, knob automation 62 / 27 %. What is left to trim: the hats (the closed hat's VCA in
  Newton every sample at 192 kHz, about a quarter of a kit; the open hat's envelope, half the time
  in Newton, an eighth), the mixer, the BD's resonator and the snappy; quicker re-entry to linear
  mode would take 8-11 % more but moves the sound at tolerance level (Tried and dropped,
  2026-11-05). Measured again 2026-11-09 (the same 3005 M instructions a second): 12.4 % of a core
  on one thread, 10.1 % on the audio thread; the 20 % of 2026-11-05 was the same work under
  other conditions, so compare instructions between runs.
- **Check set:** there: `tr808plugintest` (DIN sync, MIDI and accents, sleep and wake, voice mode,
  state, POWER, SERVICE; `--panel`, `--service`), `lemustest_instrument` (`tests/host.cpp`), `tr808bench golden|check|time`
  (CPU %, Mins/s; peak, envelope and spectrum against golden renders), `tools/spice/suite.py`,
  `tools/service/check.py` (45 of 56 pass, 46 with the nominal parts; `docs/model.md`, "Service
  notes"), `tools/compare/` (`fischer.py`, `units.py`, `accent.py`, `marks.py`, `roland.py`, `levels.py`), the
  A/B scenes (`host/scenes/`; real loops in `private/tr808/reference/scenes/synthmania-real/`).
  Missing: the bench's worst block and time asleep. Golden renders: `build/tr808-golden-3` (2026-11-04, after P15; the 2026-11-05 CPU
  round renders them to the bit).

## Tried and dropped

- 2026-10-16: the snare's snappy level, CY R125 at the printed 2.2k, OH R132, the cowbell's
  attack, D54's knee (Done, 2026-10-16: the second round and P3).
- 2026-10-16 to 10-23: for S2, R194, D54's knee alone, the knob law, the envelope and swing-VCA
  parts within tolerance, C51's dielectric absorption (S2); for the snare's accent, R194 (0.6 dB
  over 2.2-10M), the noise level with SNAPPY refitted (1.4 dB), Q48's gain, Q47's breakdown
  (`docs/model.md`, "Open points").
- 2026-10-23: the rails' sag and the hum as circuits: about 0.15 dB and -100 dBFS, judged not worth
  modelling (S9, S10).
- 2026-10-24: Newton's chord and plain iterations, and tolerances three times looser: 1-4 % less
  CPU, not taken.
- 2026-11-05, CPU: the step's rows grouped by their zero pattern, columns packed (its matrices are
  50-60 % zeros): bit for bit but 4 % more instructions (the gather and scatter cost more than the
  zeros at 5-16 rows); Newton's device currents, factorisation and solve inlined into it: nothing;
  the transistors' exponentials together before their arithmetic: more instructions; `luSolveN`
  unrolled, and the chord solves' inactive rows as a compact block: not bit for bit (the compiler
  fuses `s -= a * b` or not by the loop's shape; -160 dBFS). Re-entering linear mode at once after
  a linear run of 64 samples (16) rather than 256 (the open hat's envelope leaves its window every
  ~100 samples as it decays, then waits up to 4096 in Newton): a kit 3002 -> 2761 (2659) M
  instructions, -8 (-11) %, but not rounding level: the kit -98 dBFS (envelope and spectrum within
  0.003 dB), the sequencer, every voice and automation -21 to -31 dBFS (the clap's multivibrator
  and the squares' phase move with it), envelope within 0.034 dB and spectrum 0.13 dB there, the
  long decays' cymbal as ever. The owner's call.

## Declined

- 2026-10-16: buying reference sets, Raum45 *AchtNullAcht* (€9.99) and Cluster Sound CS-808 (€29),
  for R1-R4 (P1). Free sources only. Don't offer again.
- 2026-10-26: the trigger outs as MIDI out (P10, B4). Don't offer again.
- 2026-11-08: a choice of fitted or nominal parts in SERVICE: removed, as the TB-303 and TR-909
  have none. The nominal unit stays for the tools (`--cal nominal=1`, `tools/service/check.py`).

## Done

| When | What | Commit |
|---|---|---|
| 2026-10-12 | The TR-808, ro-drums-808 | `1f9afc2` |
| 2026-10-12 | Factory rhythms on a new instance; MEMORY menu | `ee07461` |
| 2026-10-12 | Claves' mute (edge backward Euler); op-amp limiter convergence (the accented hats); junction-node ringing (cowbell); IC2a's limit (metal accents within 0.5 dB); the squares' duty (cowbell LSD 2.74 -> 0.49, C9/R82 as printed); noise trim (toms' "harmonics", R194 as printed); R324 (rim shot level); PART LEDs (schematic swaps D78/D79) | `10ee374` |
| 2026-10-12 | CPU: 682 -> 452 % over the bench (a kit 24 -> 16 % of a core) | `c6f2291`, `72fbaeb`, `1117330`, `7781c36` |
| 2026-10-12 | POWER button (behavioural, B1) | `8e0b431` |
| 2026-10-12 | Compared with Roland's TR-808 plugin: Lemus closer to the real unit on 98 of 99 recordings | `81b1381` |
| 2026-10-16 | Second round: the TUNING pots' values fitted with the toms (every tom and conga within 12 cents at every mark, laws measured again); `fischer.py` measures hits 20 dB down within 50 ms from the onset (flat start) and the rim shot's pitch on its low tone; R318 115k -> 165k (LSD 3.87 -> 1.87 on the new measure). Tried and left: the snare's snappy level, CY R125 at 2.2k, OH R132, the cowbell's attack | `cafda9e` |
| 2026-10-16 | P7: POWER on starts the board from discharged capacitors with Q13 (now in `mix.cir`) muting the master for the detector's 1.5 s  `cafda9e` |
| 2026-10-16 | P8: the MULTI OUT jacks as eleven mono plugin outputs (off until the host turns them on; the mix keeps every voice). P10: the knobs on the TR-8's control changes  `cafda9e` |
| 2026-10-16 | P2: knob laws (`Calibration::knobLaws`): the TUNING pots by pitch, CY TONE, and their shape for BD DECAY and SD SNAPPY. Congas' LSD 1.3-4.1 -> 0.3-0.7, BD 0.98 -> 0.64 (held out 0.64), the snare's SNAPPY 2.5 gate shut as Fischer's, its onset +0.18 -> +0.10 octave  `cafda9e` |
| 2026-10-16 | P4: the 2SC945's ISE 2e-14 -> 1e-14 (its data sheet's h_FE at 10 µA); the clap's C128, C129, C138 fitted within 20 %: its reverb tail within 1.1 dB RMS of Fischer's at 0.4-8 kHz (was 2.3), tail rate -0.36 -> -0.09 octave, LSD 3.42 -> 3.05. Other voices unchanged  `cafda9e` |
| 2026-10-16 | P6: the metal group (squares, metal source, CB, CY, hats) and the mixer at twice the board's rate: the metal voices within 0.5 dB of an 8x board to 20 kHz (were 1-3 dB low at 12-20 kHz), SPICE 3-10x closer, the mixer's 22 kHz poles no longer pulled to 19 kHz (every voice -1 dB at 18 kHz before); the open hat's C60 refitted (8.65 -> 7.36 µF). CPU +40 % (701 % summed, a little past before the 10-12 speed work)  `cafda9e` |
| 2026-10-16 | P3: op-amps slew (1 V/µs, every op-amp with an output limit); trigger edges a step at their sample's start, that sample in sixteenths (the snare's tone accent 10.4 -> 9.8 dB, as ngspice; BD and toms 0.1-0.2 dB nearer the second unit); `tools/compare/accent.py`. D54's knee tested and left (S2). CPU +2 %  `cafda9e` |
| 2026-10-23 | Unit spread (`tools/compare/units.py`: Fischer's, Raum 45's and the archive.org unit, the model over 8 hits): the cowbell's attack (S3) is the squares' phase at the hit, not a gap; the rim shot's pitch is right by its period (`fischer.py` now measures it so: +56 -> -10 cents); the clap's teeth fitted with C144 0.42 µF (all three units agreed; Fischer LSD 2.62 -> 2.25); the clap's brighter tail and smaller accent in two units are their TM3 (0.38); the snare's accent (S1) is unit spread: the archive.org unit's snappy accents 4 dB less, Reverb's (an early unit, ACCENT 0.75, 7 snare pairs) as the model; the early snappy (S2) confirmed on three units and narrowed to the snappy's decay at mid SNAPPY; free sets fetched (Reverb's pack via audio.com, Synthmania's serial 209265, a pot list that agrees with the netlists); `accent.py` takes the clap, maracas and closed hat, the snare at its takes' settings; `da=` and `leak=` for capacitors (from the 909's solver); step ticks and hum measured | |
| 2026-10-23 | Decided: the clap's TM3 at the other units' value, and the step ticks modelled. TM3 0.26 -> 0.37 (three units' tails within 0.6-1.3 dB, their -40 dB times, accents within 1.3 dB; the service check's clap now passes, 45 of 56) and C144 refitted at it (0.40 µF: both teeth inside all four units). Step ticks: the archive.org unit's averaged tick (1026 steps) fitted as a band-pass network from the common trig into the mixer's summing node (`mix.cir`, `mix.rtk3`/`ctk3`), -91 dBFS at the master out against the voices at their standard LEVEL; the HI and LO outputs only | |
| 2026-10-23 | The step ticks on a second unit: Synthmania's (serial 209265) gaps show the same spikes 1.0 ms apart, at the archive.org unit's level within a few dB (rim shot, cowbell); the model's tick set between the two (+1 dB: `mix.rtk3` 107 MΩ) | |
| 2026-10-23 | Reverb's knob voices as a second documented unit: the open hat at DECAY 0 (S12) decays longer there than the model, Fischer's shorter: unit spread, closed; the toms' decays within 0.16 octave, their tuning the unit's; its cymbal rings 1.6x as long as Fischer's (unit spread) | |
| 2026-10-23 | S2 at a matched snappy level (each real snare's early snappy/tone balance): small on the fitted unit (the mid marks' snappy 0.09 octave short, full SNAPPY ~1.3 dB strong early); R194 lengthens every setting, so not it; the archive.org unit's long snappy fits R194 4.7M (unit spread) | |
| 2026-10-24 | CPU: the TR-909 solver's speedups ported (column products, Newton's LU two columns at a time, ports grouped by limiting), 726 -> 606 % over the bench, instructions 21 % fewer; rounding-level change only (envelope and spectrum within 0.001 dB but the long decays' free-running cymbal, `docs/model.md`, "CPU"); `tr808bench` counts instructions and compares envelope and spectrum | |
| 2026-10-26 | CPU: the voices as jobs on worker threads (the TR-909's pool), the same output to the bit: the audio thread's share of a core with every voice 50 -> 26 % at 44.1 kHz, automation 63 -> 28 %, a kit 23 -> 18 %; `Impl::checkJobs` confirms no voice takes from another within a sample. A search for free reference data found nothing new (archive.org's second file is the knob run already on hand) | |
| 2026-11-04 | P13: the rim shot's and claves' op-amp poles by the trapezoidal rule (`Circuit::setPoleTrap`; `TR808_POLE_BE=1` and `circuitrun --pole-trap` for tools). Against ngspice: the unaccented claves were 5 dB low at their peak and 4-5 dB low at 8-20 kHz by backward Euler, now within 0.2 dB, with nothing extra above 22 kHz; every other voice, the metal group at 192 kHz included, within 0.1 dB of either rule. Fischer's claves: -20 dB at 24 ms (his 23.9, was 22), LSD 1.57 -> 1.13; the service check's claves decay 21 -> 24 ms (the notes' 25). Bench against `build/tr808-golden-2`: only the workloads with claves or rim shot move (envelope up to 0.41 dB, spectrum 0.47 dB); CPU unchanged (Mins/s 81890 -> 81892). Also: the CPU ROM's prompt words, so `lemustest_tr808` passes | |
| 2026-11-04 | P14: the snare, clap and maracas run with the metal group at twice the board's rate, their noise through a half-band. Against ngspice: their 16-20 kHz from 0.7-1.4 dB low to within 0.3 dB (the clap's unaccented hit +0.5 dB at 6-20 kHz, its teeth); the snare's and maracas' RMS error 2.5-4.5x smaller; with a noise-like input the snappy's excess at SNAPPY 5 from +0.9/+3.3 dB to +0.3/+0.8 dB (30-100/100-200 ms). Against the real units: clap and maracas inside all four units on every measure (the maracas' -20 dB time onto Fischer's; centroids up 3-6 %); the snare against Fischer: decays -0.04 octave, LSD 1.52 -> 1.73, its snappy 1-2 dB weaker at SNAPPY 2.5-5 (P15); service check: SD decay 74 -> 68 ms (the notes' 60). CPU: one thread, a kit 23.0 -> 24.2 %, every workload 528 -> 582 %; instructions +8 %. The voices' jobs stay bit-identical to interleaved | |
| 2026-11-04 | P15: the snare's SNAPPY gets its own law, fitted at the 2x board on LSD over Fischer's 25 snare recordings: 0.18 / 0.55 / 0.90 at the 2.5 / 5 / 7.5 ticks (was BD DECAY's 0.153 / 0.471 / 0.800). Median LSD 1.73 -> 1.48 (1.52 before P14), the -20 and -40 dB times on his (were -0.04 octave), centroid +0.08 octave as before; the 2.5 ticks 2.3-5.9 -> 1.3-2.3 | |
| 2026-11-05 | CPU round, the output unchanged to the bit (`tr808bench check` against `build/tr808-golden-3`, threads against interleaved, the SPICE suite's outputs): a kit on one thread at 48 kHz 3770 -> 3002 M instructions a second (-20 %; 24.9 -> 20.3 % of a core), 19.4 -> 16.1 % on the audio thread; the bench summed 613 -> 494 % on one thread (88.7 -> 70.2 G instructions), 336 -> 259 % threaded. Odd product rows paired with the padding row, three rotating state buffers read and written in place, the step and Newton's residual compiled per port count, whole-matrix factorisation when every port is active, the LU unrolled, a lean step for circuits without devices, jobs copying only what they read, half-bands idle on constant input (`docs/model.md`, "CPU") | |
| 2026-11-05 | P12: the panel's marks by place, a file each in `artwork/tr808/` (`maker`, `name`, `model`, `tagline`: the logo boxes of `docs/panel.json`), as the other machines take them; an older `tr808.svg` pack still loads, as outlines. The orange rule under the name only with the name's mark | |
| 2026-11-05 | P9: SERVICE (`plugin/HardwareService.cpp`, the TR-909's ported): the main and voicing boards on their pots, the switch board folded out 62 mm, the jack boards and the transformer where they show, every part of `docs/boards.json` with pads drawn (1182 of 1183), with the netlists' values (their 757 parts by circuit, every pin probeable), the copper from the pads. The six trimmers modelled from their parts (`core/trimmers.*`) as parameters saved with the session (the INT clock now at run time, TM2 in `TempoClock::bpm`, TM4 as IC24a's gain); each procedure's SET UP reads on the nominal unit TP2 1.900 ms, TP3 8.333 ms, TP4 130 mV rms, TP6 1.850 ms, TP7 1.250 ms, on the fitted one 1.900, 8.333, 157 mV, 1.791, 1.214. The unit by serial (`core/revisions.h`: 000001-000299, 000300-010599, 010600 up; `noise.cir` holds both noise generators), the portion and the parts, rebuilt at run time and saved; R332 22k and R346 1k give the clap's bursts +1.2 dB and its reverb +4.6 dB (the band-pass and divider: +1.0, +4.2), the 000300-010599 board's clap +2.5 dB at the master out with its 22k gate. The scope (two probes, the voices' gates, TP2/TP3 from the CPU board) keeps the jobs (threaded 1.0 s against 2.4 s on one thread for a busy 3 s, probed). Default unchanged to the bit (`tr808bench check`, one thread and threaded; a kit 3005 M instructions a second at 48 kHz) | |
| 2026-11-09 | Accuracy round, S4 (the clap's level), closed with no change to the sound (`tr808bench check` against `build/tr808-golden-3`: identical): Fischer's voices share one recording gain (interquartile range 1.0 dB over 13 voices, `tools/compare/levels.py`), his clap sits on it at his TM3 0.26 (-0.95 dB, first 30 ms); clap minus maracas, which cancels the knob and the chain, spans -3 to +11 dB over four units against the model's +3.3 at TM3 0.37: the trim, and unit spread. The SERVICE reading of 11.5 Vp-p that opened S4 is the absolute peak to peak; the check's scope measure reads 6.9 V (the notes' 6). The SPICE suite wasn't re-run (no netlist changed, ngspice not on this machine) | |
| 2026-11-09 | The MULTI OUT jacks as on the unit and the TR-909: P8's eleven outputs were never declared to the host (the driver's `outputs` list was empty, so the plugin had the main output only, and the check ran on the processor alone); the driver declares them, `tests/host.cpp` checks it. REAR on the bar shows the rear panel (`docs/panel.json`'s `rear_panel`, measured on the straight-on photo), a click plugs a cable into a jack, saved with the session. Each jack's normal contact is a switch in the netlists at the mix resistor's own value (`mix.cir`'s `SR1`-`SR11`, each voice's `SMIX`): a cable takes the voice out of the mix and loads it with 100k. With no cable the output is unchanged to the bit (`tr808bench check` against `build/tr808-golden-3`), 3006 M instructions a second on a kit against 3005. A cable moves the next voice's level by up to 0.25 dB: the mix resistors load C17, so taking one out moves the others' low end, as it would on the unit | |
