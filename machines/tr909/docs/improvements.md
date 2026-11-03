# TR-909: what we know is wrong, and the plan

What the model gets wrong or hasn't checked, as the transcription and the comparisons with the real
units found it (`docs/circuit/*.md`, `docs/reference.md`, `docs/model.md`), and what is planned.
"Real" is the AudioRealism unit (`private/reference/real909/packs/audiorealism_tr909_ultimate`,
a late board, played at DAC code 21) unless it says otherwise; "RR" is Rob Roy's 1995 unit (an
early board).

## Known inaccuracies

### Sound

The calibration (`docs/model.md`, "Calibration") fitted the AudioRealism unit; what it leaves:

- **BD.** The body's 2nd harmonic (-21.5 dB against AR's -28, RR's -29 and a third unit's -24.5 to
  -27: perhaps 3-6 dB high; the 3rd and the half-cycles' ratio are spread between units, the model
  inside: AR -26 / 0.95, scherer.de's unit -31 to -33 / 0.77, the model -33 / 0.84); DECAY 0 (t-20 93 against 106 ms, tail τ 21 against 29: Q3/
  Q4's release or the VCA's law near its threshold); the click's real cause (R44 36k stands for it);
  the accent 0.7 dB low around code 35 (`circuit/bd.md`, "Calibration").
- **SD.** The snappy's burst at the trigger's end 2.5 dB weak at SNAPPY 0-0.5 against the AudioRealism
  unit (right at 0.75-1); the pitch bend's first milliseconds 3-10 % high (C66 -25 % would fit it, outside its
  tolerance and worse elsewhere); the drum 1-1.5 dB hot against the snappy; the drum's t-20 8-12 %
  short and t-40 5-10 % long (`circuit/sd.md`, "Calibration"). The noise's top octave was the board's
  rate (fixed 2026-10-22, below).
- **Toms.** The far tail 0.04-0.14 octave short at -40 dB (0.07-0.16 before the tantalum capacitors'
  dielectric absorption, 2 %): the unit's tail has another shape, its VCAs closing as the envelope
  nears V_BE; the accent's top 0.4-0.7 dB short; the tom noise's
  attack up to 1.3 dB strong; HT's held-out TUNE settings 25 cents low (`circuit/toms.md`).
- **RS.** The output taken from IC50a rather than the drawn IC50b (the evidence is strong: five
  units' flat tops at half the negative peaks); the board's copper (SN p.10) wires IC50b as drawn,
  so the units' difference would be values (C121, C122, R421), not wiring; the accent's top 0.5 dB
  short.
- **HC.** The fourth tooth a spike where AR's, Regebro's and RR's units have a full tooth (C64, C62,
  Q86 don't do it; scherer.de's unit has it mostly 3-12 dB down, so units differ), and with it the
  tail at 60-250 ms (120 ms against 94); the first tooth's interval longer than the
  second, the units' shorter (comparator d's hold on the first slam); the late tail: ISC 1e-11 on
  the (S) part fits it within 2 dB but the shared card is left without it.
- **HH.** The OH's t-40 at full DECAY 544-555 ms against 520; RR's 5-8 % longer decays and its
  OH law; the early board's low end (RR has 6-14 dB less below 150 Hz than the model's rectified
  pedestal, though RR's waveforms are its sampler's).
- **Crash, ride.** The 4011UB gates' drive x1.72 is above every published typical (the level-1
  MOSFET may stand for the gates' transition speed rather than their drive); a ripple of ±20-35
  cents along TUNE; the crash's VCA bends 2.3 dB more at its envelope's end on AR (not on RR).
- **The (S) transistors.** A selected 2SC2603, fitted as `2SC2603S` (BR 6.5) on the snare's
  snappy; if the AudioRealism unit's clap TM is at the service notes' spec, BR 2.2 instead
  (`circuit/s-transistors.md`).
- **One unit.** The calibration is the AudioRealism unit's; RR's differs (its toms 14-92 cents
  higher, its BD's decay shorter, its VCOs unlocked).
- **Rates.** The sample voices' 15-20 kHz band 0.5-0.7 dB low against ngspice at 192 kHz. The
  analog voices at 88.2-96 kHz: the noisy ones (SD, HC) 0.4-1.2 dB low at their jacks over 10-16
  kHz and about 2.5 dB at the master out over 16-24 kHz, against the board at 384 kHz (`docs/model.md`,
  "Rates"); a board at four times the rate takes most of it, inaudibly, for 1.5 times the CPU
  (tried, out).

### Behaviour and features

- **CPU.** The busy kit (`tr909bench`'s `kit`) costs about 2.4 G cycles a second at 48 kHz,
  counting every thread (2.3 G at 44.1 kHz; 2.28 G on one thread, `TR909_WORKERS=0`): over
  two thirds of a core, against the half (1.6 G) planned; every voice at once 3.05 G. Of the solver's time on the busy kit (`tr909replay`): the
  comparator VCOs (the snare's two, the low tom's three, the BD's) about 40 %, two thirds of it
  their flips, each redone in sixteenths over three or four samples; relinearisation about 17 %;
  the hi-hat's and crash's VCAs and output stages, at 192 kHz, about 20 %.
- **Not emulated:** the tape interface (SAVE, LOAD, VERIFY, TAPE SYNC's input), the cartridge
  (detected as absent), TRIGGER OUT (modelled in `rs.cir`, not an output of the plugin), the MIDI
  out's clock, start/stop and song select (its channel messages go out), the analog voices' own
  power-on transients (they start at their operating points: from 0 V they measure the same,
  `circuit/power.md` §5), the supply rails' ramp (so the serial number's D701 change, a brown-out
  reset, does nothing).
- **SERVICE:** the power supply's and transformer's places are assumed (`boards.md`); the boards'
  colours and where they print the designators are a guess (no photograph found); R114 and R115
  aren't placed; a diode's band and a transistor's flat face aren't from the drawing; the switch
  board, the DIN board and the power supply aren't probeable (the model has no netlist for them).
- **Accent DAC labels.** p.11 reads as RA1 (IC2) into the snare's IC34b and RA2 into the BD,
  against `interface.md`'s reading and RES's table; which IC decodes 4001 and 4002 is untraced.
  The board follows the firmware's addresses (4001 with the BD's trigger, 4002 with the snare's), so
  only the labels are in question.
- **Production changes** are `.param`s of the netlists (`circuit/*.md`), set from the unit's serial
  number in SERVICE (`core/revisions.h`); the default is the late board.

### Reference data

- One unit on a fine knob grid (AudioRealism), a second (RR) on four dial marks, a third
  (scherer.de: his hats, and since 2026-10-22 his `909.rar`, every voice with the knobs turned
  during the takes, knobs and outputs unknown: only knob-free measures). No paid sets.
- No measurements of a real unit's parts or nodes (no real hardware here).

## Plan, in order

1. **CPU**: the busy kit under half a core, with a golden-render bench showing the sound unchanged.
   2026-10-22 took it 17 % lower (Done); what is left needs a decision (the two below) or
   another treatment of the flips:
   - `TR909_AHEADZONE=0` (a comparator's flip is found by the sample's trial step rather than
     predicted a sample ahead): 5 % less, the snare's VCOs 25-60 ppm further from ngspice (-120 to
     -145 ppm; 0.04 cent), the CMOS test VCO closer (+71 to -22 ppm), the others unchanged; the
     busy kit's envelopes within 0.1 dB of now, its spectra 0.01 dB.
   - The sample voices at 96 kHz rather than 192 (`TR909_GROUP=1`): 11-12 % less on the busy kit,
     the hats' and cymbals' highs lower: -0.3 dB at 10 kHz, -0.8 at 16, -1.5 at 18, -2 to -2.5 at 20.
   - A flip's substeps in quarters rather than sixteenths (tried 2026-10-23): 3.3 % less on the
     busy kit, 5 % on every voice, the VCOs 150-250 ppm further from ngspice; out. Coarse only
     after the flip keeps the pitch and saves nothing.
   - 2026-11-01 research (`build/tr909-research/report-flips.md`, `report-rates.md`; prototypes
     in `flips.patch`, `rates.patch` there, env-gated): nothing found keeps today's accuracy for
     less. What is left, each a decision:
     - The Newton island (not built): in a flip's sixteenths Newton only on the 1-3 devices
       (2-6 ports) that leave their linearisation, the rest of the partition linear. Flips are
       about 25 % of the busy kit (the VCO partitions 37.6 %); estimated 55-65 % off a flip, about
       10 % of the kit (6-14 %); 3-5 days. The devices outside the island go to linear mode's
       100 µV tolerance (Newton's 10 µV), so it changes how a flip is computed.
     - The sample voices at the board rate with their op-amps' poles by the trapezoidal rule
       (`TR909_GROUP=1 TR909_GROUP_TRAP=1`): 7.0 % off the busy kit (ch/oh/cr/rd 25-27 %).
       Against ngspice: better than today below about 15 kHz on the hat and crash. Above that,
       the crash's 18-20 kHz is -1.36 dB (today -0.77), the phase-counting difference is 10-13 dB
       worse and the incoherent part 2-4.5 dB worse; the ride's incoherent part is 2-6 dB worse in
       every band. That's the VCA partition at the board rate, the limit. A trade: by ear (hat,
       crash, ride loops) or not at all.
     - The same with a fitted 24-tap FIR (the solver at 96k against itself at 768k, not against a
       recording): 6.5-6.9 % off. The total difference beats today's in every band but the
       cymbals' 0-2 kHz. A fit, so a decision first.
2. **The remaining gaps above**, one by one, where a mechanism can be found (not by fitting parts
   to measures they don't drive). Found 2026-11-01 (`report-rates.md` §2): an op-amp's internal
   pole is integrated by backward Euler (first order; the comparators' by the trapezoidal rule).
   That is most of the sample voices' top-band loss. With trapezoidal poles at today's 2x
   (`TR909_GROUP_TRAP=1`), the hats, crash and ride come 1-10 dB closer to ngspice in every band,
   for +0.3 % CPU. On the hat, no ringing near Nyquist at 96 or 192 kHz. Backward Euler was kept
   because the trapezoidal rule maps a MHz closed loop near z = -1 and rings
   (`circuit/solver-mosfet.md`), so other voices (and the mix board) need checking voice by voice
   before it goes further. It changes the sound (towards the circuit): a decision.
3. **More units** from free recordings (scherer.de's `909.rar` came 2026-10-22; others as they are
   found) to tell unit differences from model errors.

## Budget

- **CPU:** `tr909bench`'s `kit` under half a core at 48 kHz (1.6 G cycles a second over all
  threads) is plan item 1's goal. Last measured (2026-11-01): 2.41 G over all threads, 2.28 G on
  one (`TR909_WORKERS=0`), about 75 % of a core taking 1.6 G as half; its worst block about 55 %,
  every voice at once about 97 % of a block. Nothing measured so far reaches the goal without a
  declined trade (plan item 1, Tried and dropped). For the ship gate (decided 2026-11-02: ship as it is), the last
  measurement plus about 10 %: `kit` under 2.65 G cycles a second over all threads at 48 kHz, its
  worst block under 60 %. Half a core stays plan item 1's goal.
- **Check set:** there: `tr909plugintest` (MIDI and voice mode, accents, sync, sleep and wake,
  state, power-on; `--panel`; `--service`: the serial number's revisions, TM, the scope's probes),
  `lemustest_instrument` (`tests/host.cpp`), `tr909bench golden|check|time` (CPU %, cycles over all
  threads, worst block, Mins/s; envelope and spectrum against golden renders), `tr909replay`,
  `tools/spice/suite.py`, `tools/tr909seq/tests.py` (54 firmware checks), `tools/compare/` (per
  voice against the units, `sequencer_timing.py`), the A/B scenes (`host/scenes/`;
  `private/tr909/reference/scenes/`: ar, factory-patterns, video). Service checks:
  `tools/service/check.py` (2026-11-02, about 7 s), every service-notes figure the model can run
  (p.1 specs, p.5-6 firmware, hi-hat and snare figures, p.9's traces against their pairs, p.11's TM
  and rim-shot sketches, p.12 rails), on the notes' parts and the unit's: 62 of 75 pass. The 13
  fails: the unit's TM (4.1 Vp-p, not 2), the snare's V_DD peak (4.1 V, not 5), and from p.9 the
  LT and CH accent ratios, the snare's spike as its trigger ends, the CH-accent and OH
  lopsidedness; each on both calibrations but TM. Missing: a random-settings stress and a long-run
  case in plugintest; golden renders of the current code on disk (`build/tr909-golden-k3` is gone; the
  flips research kept one in `build/tr909-research/flips-work/golden`).

## Tried and dropped

- 2026-10-17: located-event flips (a comparator's flip located inside the step): slower and less
  accurate.
- 2026-10-22, CPU: relinearised devices as corrections to fixed matrices (12 % slower), a predictor
  and fresh factorisations in a flip's substeps, linear mode on a comparator's approach, a
  column-major LU for Newton (3.6 % slower), the solved devices (1.2 %, the sound moved): Done,
  2026-10-22's CPU pass. Also a refactorisation on a change of the Jacobian.
- 2026-10-22, accuracy: dielectric absorption for the BD at DECAY 0, the 4011UB's input clamp for
  the cymbals' clocks (Done, 2026-10-22).
- 2026-11-01, the flips research (`build/tr909-research/report-flips.md`); nothing saves at today's
  accuracy:
  - quarters with sixteenths only where a corner falls: slower (a quarter costs its four
    sixteenths' iterations);
  - the comparator linear over 10 ranges rather than 2: -1.8 % on the busy kit, one VCO 13 ppm
    further from ngspice (over the 10 ppm bar);
  - Newton started from the last flip's trajectory or a straight line: lands on the comparator's
    second solution;
  - a fresh LU at every sixteenth: 6-9 % slower;
  - re-using linearisations (plan item 1's untried idea): at most 1.7 % of the kit.
- 2026-11-01, the sample voices' rate research (`build/tr909-research/report-rates.md`):
  - an exact (matrix-exponential, ZOH) discretisation: not viable, since the stage the D/A steps
    enter is nonlinear with fast dynamics of its own, and the partitions relinearise 60-70k times
    a second;
  - the bilinear rule prewarped (every capacitor, or the out stage): the top band right, the
    0-10 kHz total difference 10-20 dB worse (the coupling capacitors' corners move);
  - the VCA at the board rate, the out stage at 2x: about 3 % off, the same incoherent limit, 83 µs
    later.
- 2026-11-01: more than two worker threads: more cycles, no shorter block (Done, 2026-11-01).

## Declined

- 2026-10-22: `TR909_AHEADZONE=0`: 5 % less on the busy kit, the snare's VCOs 25-60 ppm further
  from ngspice (plan item 1). Don't offer again.
- 2026-10-22: the sample voices at 96 kHz rather than 192 (`TR909_GROUP=1`): 11-12 % less, the
  hats' and cymbals' highs -0.8 dB at 16 kHz, -2 to -2.5 at 20 (plan item 1). Don't offer again.
- 2026-10-22: HQ, the board at four times the rate, offered twice: 1.5 times the CPU for most of
  the noisy voices' 10-24 kHz shortfall (Rates, above); not heard in the plugin or in Lemus AB
  loops, removed. Don't offer it again, nor as HQ for bounces.
- 2026-10-22: TRIG OUT as a plugin output, the cartridge, the tape interface (Not emulated, above).
- 2026-10-22: a MULTI OUT plugged by the host turning its bus on (Onyx and Logic turn every bus on):
  a cable clicked into the rear panel's jack instead.
- 2026-10-23: a flip's substeps in quarters: 3.3 % less on the busy kit, the VCOs 150-250 ppm
  further from ngspice (they move pitch; plan item 1). Don't offer again.
- 2026-11-02: widening the crash's TUNE range for a unit whose crash goes lower. The unit in a
  factory-song video plays its crash's sample clock at 26.35 kHz for a minute, 109 cents under
  the model's TUNE 0 (28.06 kHz, the AudioRealism unit's; Rob Roy's 27.6); its hi-hat clock is
  within 6 cents of the model's, so it isn't the video's speed. VR25 near its printed 10k (the
  fit has 8.36k) would reach it (TUNE 0 at 25.7 kHz, the top unchanged), but it would move the
  lower half of TUNE away from the calibrated unit. Unit spread: kept as the calibrated unit.

## Done

- 2026-11-03: the TEMPO readout (the knob's tooltip, the bar, the parameter's text) is what the
  firmware runs at. The firmware follows the knob with a dead band (13FC: 128 at power-on, then a
  count short of the knob whenever it is two counts away), which the readout left out: the
  default showed 120.6 BPM but ran at 121.7 (122 on the display). The parameter's text is now the
  BPM after a load with the knob there; the bar and the tooltip read the firmware's own value
  (VV:30), so a knob turned later, a step either side, and a recalled tempo memory show right; in
  MIDI and DIN sync the bar shows no tempo. A warm start now boots with the pots where the knobs
  are, so a session load runs at the same tempo whatever the knob was before. The default now runs
  at 120.6 BPM, 121 on the display (`tr909plugintest`, "tempo"). Also: song position into a track
  counts each measure at its own length, and pattern play ignores it (`tests.py`, "song position:
  measure lengths, pattern play"; `docs/firmware.md` corrected).
- 2026-11-02: the bar's MEMORY pill is gone (decided 2026-11-02: no memory clear or factory reload on
  any machine unless the unit has a control for it). A new instance holds the factory patterns (with
  the v1.0 ROM), and the host's State: Reset, on every machine, makes the 909 a new instance.
  The bar: SYNC, REAR, SERVICE, `?`.

- 2026-11-02: SERVICE's FACTORY TRIM pill is gone: it put TM back to its default, as a double-click on
  the trimmer does, and its name read as the notes' 2 Vp-p adjustment, which it isn't. TM's default
  stays the calibrated unit's 0.30 (the clap +0.3 dB against the other voices, against AR's; the
  notes' 0.255 leaves it 6.1 dB under, `docs/circuit/rshc.md` 9.3). The voice-mode prompt now also
  says what the other missing ROMs take away (the host lists every missing image's losses), and the
  program ROM's list adds sync, MIDI out and the factory patterns.

- 2026-11-02: Windows. The plugin builds with MSVC and with clang-cl (`.github/workflows/windows-machine.yml`,
  run by hand), the host test passes (the ROM checks are skipped on the runner), and the zip is
  made as a release's. Two fixes on the way: the netlists' names reach the header step with commas
  (the Visual Studio generator kept the backslash of `\;`), and `NOMINMAX` before `windows.h`;
  tr909bench measures on Windows too. On the runner (two virtual cores, so no worker threads; voice
  mode) the busy kit costs 2.72 G cycles a second at 48 kHz with clang-cl and 3.21 G with MSVC
  (2.31-2.41 G on the Mac), and doesn't keep up there; a real PC is still to be tried. Built for
  x86_64 and run under Rosetta, the sound matches the ARM build (envelopes within 0.29 dB, spectra
  0.19 dB over the bench's workloads); one plugintest check fails there: a SERVICE probe on the
  clap's `hc.env1` moves its envelope 1.63 dB in a 5 ms frame (0.08 on ARM, 0.5 allowed).
- 2026-11-02: the service checks, `tools/service/check.py`: 62 of 75 pass (README, "Status").
  The snare's "spike" (p.9's first swing positive, not negative) is the snappy's high-passed noise
  bursting as Q43 lets go of Q47 at the trigger's end; with SNAPPY at 0 it's gone. The AudioRealism
  unit has the same burst: over its 144 snare clips, model against clip at the same knobs and code
  (the burst's 4 kHz high-pass RMS in the 0.6 ms before the main swing, re the hit's peak), -0.3 dB
  median at SNAPPY 0.75-1 (IQR -2.3 to +2.1), -2.5 to -2.7 dB at SNAPPY 0-0.5. p.9's traces leave it
  out; the model is left as it is. The low-SNAPPY shortfall is a new gap (below).

- 2026-11-02: artwork places, as the 303's: a file per place in `artwork/tr909/` (`maker`, `model`,
  `name`, and `rear_maker` for the rear panel's Roland logo, its own place), drawn as it looks; the
  older `tr909.svg` pack fills the places without a file (the rear takes its `roland`, as before).
  The blank pack is no longer written, and an untouched copy is taken out. The host test checks the
  places against the panel's logo boxes.

- 2026-11-02: SERVICE's copper generated, as the 303's: a land at every pad of `docs/boards.json`
  and tracks from each pad to its nearest neighbours of other parts (a spanning forest, 0/45/90
  degrees), drawn faintly through the boards (`makeCopper`; `docs/boards.md`). The copper traced from
  the service notes' foil views is out of the artwork pack, which now holds the three panel marks
  alone (43.5 kB rather than 1.77 MB). plugintest and the host test pass.

- 2026-11-02: a new instance, a session load and MEMORY start warm, as a 909 that has been on a
  while (decided 2026-11-02): the board at its operating points, the CPU from RESET run through its 0.6 s boot at
  once with its latch writes dropped (`TR909Processor::warmStart`, 1 s of the firmware in about
  7 ms; the boot's one pulse on the HH, crash and ride trigger lines unheard), so a hit 20 ms after
  loading comes out at its full level (+0.3 dB re one 40 s after a power-on, the BD VCO's phase) and
  the board sleeps 4 s after loading rather than about a minute. Only the rear's POWER switch going
  on runs the power-on (`VoiceBoard::powerOn`: the boot, the 2 s master mute, the thumps at the
  MULTI OUTs). A session loaded while switched off loads on and warm. plugintest: the warm start,
  and the power-on's checks now after POWER off and on.

- 2026-11-01: two worker threads rather than up to six (bit for bit the same): the toms' and the
  snare's jobs decide the block and the rest fit beside them, so the other threads only cost
  cycles (waking, the caller waiting on them, colder or slower cores). `tr909bench` at 48 kHz, six
  against two: the busy kit 2.80 against 2.41 G cycles a second (-14 %), every voice 3.29 / 3.07,
  automation 2.93 / 2.59; the worst block the same (54-55 %, every voice 96-98 %).
  `TR909_WORKERS` sets the count for the tools.

- 2026-10-26: the netlists' doubts checked on the board's copper (SN p.10, now read part by part):
  the clap's R221 and R222 wired as NAVA has them (the schematic's junction dot is its slip), the
  BD's R8 to the supply strip, the rim shot's IC50b wired as drawn (`circuit/rshc.md`,
  `circuit/bd.md`). No netlist changed.

- 2026-10-24: SERVICE: the top panel off, as the 303's: the voicing and switch boards' component
  sides with every part where the service notes' drawings put them (p.9, p.10, p.12 read off into
  `docs/boards.json`, placed on the panel's knobs and the rear's jacks, 1227 parts checked against
  the netlists; `docs/boards.md`), the knobs and keys on them working, the copper faint through from
  the artwork pack. The serial number puts SN p.7's production changes
  on the board (`core/revisions.h`; each heard on its voice and no other, plugintest), TM is a
  parameter with the notes' procedure, and a two-channel scope probes any pin of any part the
  netlists draw (a probe reloads its partition with the node as an output: the snare unchanged, the
  clap's envelope within 0.16 dB, its comparators a fraction of a sample apart once).

- 2026-10-23: a second CPU pass, small: the uPD7810's timers step straight to their next match
  (bit for bit; the emulator 22 % less, about 0.3 % of the busy kit), the noise generator's
  sub-samples in one pass (within -100 dB). Measured directly rather than from the profiler, which
  over-counts the emulator (10.8 %, really 1.5 %) and the output decimators (3.8 %, really under
  1 %): what is left is the solver itself. The worker jobs already go heaviest first. `tr909bench`
  starts past the power-on and counts instructions.

- 2026-10-22: dielectric absorption and leakage in the netlist format (`da=`, `leak=`; Dow's model as
  R and C elements, matching ngspice as before). The tantalum capacitors (toms, BD) at 2 %: the toms'
  far tails 0.01-0.03 octave longer, the BD's t-40 and tail a little closer, t-20 unchanged, about
  1 % more CPU on the toms. Tried and not it: absorption for the BD at DECAY 0 (no change at 2-10 %),
  the 4011UB's input clamp for the cymbals' clocks (under 1.5 %, the ratio to the hi-hat's clock
  unchanged). The BD's harmonics stay level-free through the decay in both, which doesn't locate them.

- 2026-10-22: the rear's POWER switch works: off, no sound or MIDI, the panel dark, the CPU and the
  board stopped (no CPU), the memory kept; on, a power-on as a new instance's. Not saved: a session
  loads switched on.

- 2026-10-22: power-on. A new instance, a session load and MEMORY are the 909 switched on
  (`VoiceBoard::powerOn`): the latches cleared, the noise generator's start, and the master outputs
  muted while C143 charges, from mix.cir's own network (its `pon` stepped from -15 to +15 V). A BD hit
  at 1.0 / 1.5 / 1.8 / 2.0 / 2.5 s comes out -49 / -24 / -3.2 / -0.9 / -0.1 dB (SN: -9 dB at 1.8 s, -2 dB
  at 2.0 s: the netlist's mute lets go a little early); the DC restorer then holds the outputs'
  negative half-waves for up to half a minute, and the board sleeps about a minute after power-on.
  The firmware's power-up trigger (HH, crash, ride) is heard only at the MULTI OUTs, as on the unit,
  and the hi-hat's, crash's and ride's analog stages start from 0 V: their jacks thump (about
  −12 dBFS, settled by 2.5 s). The other voices measure the same from 0 V (`circuit/power.md` §5).

- 2026-10-22: MULTI OUT. REAR (the bar) shows the rear panel (panel.json's rear_panel); a click
  plugs a cable into a MULTI OUT jack: its voice leaves the mix (mix.cir's switching contacts,
  `VoiceBoard::setPlugged`) for that output, the jack loaded by the cable. No cable: in the mix,
  the output silent, whatever the host turns on. The jacks reach hosts at last: the plugin's shell
  (host/LemusProcessor) declared one output; it now declares the machine's (`MachineInfo::outputs`),
  mono or stereo. Tested: a cable in BD, the mix has no BD left (−41 dB, its floor), the SD the
  same; the cables saved with the session.

- 2026-10-22: MIDI out. The plugin sends the firmware's MIDI OUT (the EXT INST notes and the merge
  of what comes in), each message at its own sample (`lemus_plugin(... MIDI_OUT)`, a new option of
  the shared CMakeLists). Tested: notes in on channel 3 come out 2.1 ms later; the factory
  pattern sends nothing (the internal voices never go out).

- 2026-10-22: accuracy. scherer.de's `909.rar` fetched (324 MB, every voice of a third unit,
  catalogued in `private/.../scherer_909_2009/README.md`). The snare's noise was 1.4-3.6 dB dark
  over 10-16 kHz from the board's rate, not the circuit: its filter (IC40a) and the clap's (IC26b)
  run at twice the rate (`.part ... over=2`, on the noise generator's half-samples), within 0.6 dB
  of four times; three units agree with the faster render (`circuit/sd.md` §11, §15). Every voice's
  partitions checked against its whole netlist (`docs/model.md`, "Rates"): within 0.3 dB of
  envelope but for the toms' and ride's, which differ by their VCOs' phase drift and frame levels
  only. The third unit: BD harmonics and the clap's fourth tooth are spread between units, the rim
  shot's output stage IC50a's as on the other four.

- 2026-10-22: the CPU pass, 17 % off the busy kit (`tr909bench` at 48 kHz 3451 to 2852 M cycles a
  second, 44.1 kHz 3232 to 2657; every voice 3776 to 3483; golden renders `build/tr909-golden-k2`).
  Bit for bit the same: the linear steps' products with K and M in one pass down shared columns,
  the states and inputs read where they are kept, the windows checked two ports at a time, a fast
  path for a linear step; relinearisation compiled for each device's port count, its update of K,
  M and Zf (now by columns) as T first and then multiply-adds down the columns; Newton's LU rows
  two at a time and its ports grouped by how they are limited; a block that doesn't sleep on its
  own skips the quiet checks. Not bit for bit: the rule sending a small circuit that relinearises
  most steps to Newton is gone (1.6 %; linear mode's 100 uV against Newton's 10: the clap's teeth
  move, one frame of `everything` 0.17 dB, about the partitions' own difference from the whole
  netlist), and the switch to the stale rows comes at twice the rate (1.1 %, measured). The SPICE
  suite is unchanged (largest change 0.004 % rms). Dropped: the solved devices (`TR909_SOLVED`,
  1.2 %, the sound moved). Tried and dropped: relinearised devices as corrections to fixed
  matrices (12 % slower at these sizes), a predictor and fresh factorisations in a flip's
  substeps, linear mode on a comparator's approach to its flip, a column-major LU for Newton.
  `tr909replay` (with `TR909_RECORD`) measures one partition on recorded inputs.

- 2026-10-17: the port into Lemus: the firmware on the uPD7811 (with fixes to the CPU core: serial
  transmit, interrupt acknowledge, short slices; the cartridge sense, the DIN inputs), every voice a
  netlist checked against ngspice, the sample voices' logic, the jack board, the panel and editor,
  the factory patterns from the v1.0 ROM, voice mode, sleep, the A/B scenes, the solver's MOSFET
  element, its node-shunt fix and its comparator VCOs (pitch within 0.01 % of ngspice); the (S)
  transistors; the calibration to the AudioRealism unit, every voice checked on held-out settings
  and on RR (`docs/model.md`).
