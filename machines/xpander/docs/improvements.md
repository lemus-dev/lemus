# Oberheim Xpander: what we know is wrong, and the plan

What the model gets wrong or hasn't checked, as research.md, model.md and the comparisons with the
real units found it, and what is planned. "Real" is an Xpander (main 1.2, voice 1.4, voice board
Rev D) unless it says otherwise; Matrix-12 recordings are named as such (the same voice boards).

## Known inaccuracies

### Sound

- **Pitch per note** (after the droop fix, below): within about 0.2 cents of the real units'
  medians per octave over 33-1047 Hz; note-to-note scatter 0.6 cents (real 0.5-1.0). Above C8 (no
  real data) the curve sits about +1.9 cents between the tune points.
- **Self-oscillation pitch against cut-off.** With the cells' tanh (R. McClellan's notes: the
  diode-loaded cell is a tanh of the input difference), the oscillation at the firmware's RES 63
  (4.67 V) sits about 265 cents below the cells' small-signal frequency (463 before round 5's
  resonance gain, 241 before round 15's VCF OUT gain). The VCF tune corrects it at RES 63, but the
  firmware maps RES 0-62 below the break point, so RES 62 to 63 drops the pitch by about 2.6
  semitones in the model, and the cut-off at RES 0-62
  sits that far above the tune's targets. The data sheet and the firmware don't settle whether the real chip
  drops that much; Yxxwb4d8GHA's RES 62/63 takes (12:45-13:20) play low notes under an envelope
  sweep, so its pitch can't be read there. Waits on data: one held note at RES 62 and at 63, same
  cut-off.
- **The RES scale in 4P LP** (A/B, NuBRcBlNAH8 4P LP at max resonance): the clip fits ours at RES
  52 (2026-11-19, round 11). If that pass was at RES 63, ours resonates too strongly there; but the
  same video's 4P BP pass, on the same 4-pole loop, self-oscillates, so the clip's RES is unknown.
  Waits on data: an Xpander holding one low note in 4P LP at known RES values, from a voice out.
- **VCF OUT's gain** (x88 against the data sheet's x100) is fitted on one Matrix-12's phaser clips
  (round 15). The phaser modes' low end hangs on it: at x100 their fundamental would sit 6-12 dB
  lower, at 0.94 3-7 dB, and another unit could sit anywhere around it. Its other effects (3P
  and 4P LP 1.1 dB quieter against the other modes; the cells swinging more in self-oscillation)
  have no recording to check them. The factory demo's unit (NJrIT7_8N0Q, round 16) agrees on
  PHASEPAD (3 PHASE+1 LOW): its bands from 63 Hz to 1 kHz within 0.9 dB rms of ours at x88, 2.4 at
  x100; SAMPLETY 6.6 against 7.9, BAMBOO no different. Waits on data: a second unit's phaser mode at
  a known FREQ and RES, one note.
- **VCF FM's high sidebands** (round 16, a watch item): on the factory demo HAMNCHEZ (3P LP, RES 63,
  VCF FM 40 from VCO2 at 658 Hz) is 20-40 dB brighter above 4 kHz in ours, VCF-FMOD and PIPES (4P LP,
  FM 117 and 128) 10-20 dB above 5 kHz. In ours it is the FM: at amount 0 the top falls 30 dB, while
  RES 10 steps down moves it 3 dB. A triangle sweeping the cut-off exponentially does make sidebands
  that high, and STEELDRM (3P LP, RES 63, FM 231) and the heavy-FM patches in the other modes match
  the demo at the top, so it may be those patches on that unit. A Matrix-12 at a known amount
  (round 19: FM 63 into 1P HP at RES 63, two saws) matches ours within 2.0 dB rms in octave bands
  from 63 Hz to 8 kHz (3.8 at FM 32, 5.5 at FM 0), so the depth holds.
- **PHASEPAD's top end** (round 17, a watch item): ours 7-13 dB brighter at 4-8 kHz on both units
  that play it (the dry qfg9Jni8nN8 and the factory demo); the Matrix-12's phaser-mode clips are, if
  anything, darker in ours at their top harmonics, so it is that patch (its cut-off and its
  modulation) rather than the mode. Waits on data: the patch's settings on a unit.
- **The input VCAs' taper** below 2.7 V is the fitted family extrapolated (round 12 measured it at
  2.69 V against 4.67 V; the patch-to-patch levels of the factory demo agree down to 1.4 V, but no
  single measurement covers that range). The absolute gain is held at VOL 63 [C]. Waits on data: a
  dry, steady patch with one VCO at a non-harmonic interval and its VOL under 2.5 V.
- **The resonance VCA's CV law** is taken as linear [C].
- **CEM3374 comparator delay** fitted to 0.5 us by the firmware's PW tune [F]; the data sheet's
  typical 3 % untrimmed HF error at 10 kHz would be 0.75 us of delay alone (4 delays a period), and
  it also holds the exponential converter's bulk resistance (now 10 ohm [F]). Waits on data: a single
  VCO near 50 % pulse width, and real pitch above 4 kHz.
- **Output level**: 0 dBFS = 10 V at the outputs [C]; one voice of the factory patch peaks at about
  -29 dBFS. To check against the recordings' levels once comparisons are in.
- **Sample-rate lock**: a narrow plateau remains at exactly rate/18 (10666.67 Hz at 192 kHz) in
  hard self-oscillation, about 8 cents wide; clear of the tune's E9 target.
- **The DAC's held voltages** are near enough, not exact [C] (firmware.md, model.md):
  - the fine sample (C805) and the reference (C806) are instant;
  - FAST drives C2 for the whole S&H window, where the schematic gates it with A0 (about half).

  At the CD4051B/4053B's typical R_on the firmware's windows settle them to under 0.4 cent. At the
  maximum R_on, the fine sample would carry up to about 8 cents of pitch over from the CV written
  before it. Waits on data: a unit's pitch against the cut-off of the voice before.

### Behaviour and features

- **Not emulated:** the cassette interface; CV1-6, GATE1-6, TRIGGER, CHAIN ADVANCE and the pedals as
  plugin inputs (they read as unplugged: CV at 0 V, the pedals at full scale, as after ECO 601. A
  patch written with a pedal can sound very different without one: SYNFLUTE in a free user bank
  has PEDAL 1 +53 on its VCA envelope's release, so every note rings for seconds here, where the
  `itLhB9Yn1DE` Matrix-12 plays it as if its pedal read 0 V: either a pedal sat at its minimum, or
  that model's firmware ignores an unplugged pedal, since its pedal jacks tell it whether a plug is
  in (MAME's matrix12 driver) and the Xpander's don't. The unit's own answer works here: MASTER >
  MIDI > CTRLS takes PEDAL 1 or 2 from any MIDI controller instead of the rear jack, with MIDI >
  ENABLES > CONTROL on; checked 2026-11-23, SYNFLUTE with PEDAL 1 on CC 4: CC 4 at 0 releases in
  about 0.2 s, at 64 or more it rings on); temperature drift; component tolerances other than the
  voices' levels (the VCOs and filters are alike, so the factory tune is the same for all six).
- **First start**: a new computer's first instance runs RESET MEMORY and TUNE ALL itself (about 20 s
  of CPU, in the background, cached per model version). With the factory patches in the ROM folder
  (FACTORY1.SYX, optional) it then receives them through MIDI IN and starts on patch 00; without
  them single patches 00-99 are blank. There are no factory multi patches: none was found.

### Reference data

| Unit | Where | What it has | Known settings |
|---|---|---|---|
| Xpander (synthlegends) | `private/reference/youtube/qfg9Jni8nN8` | 18 min, no effects, panned voices; clips in places; patches 02-55 in order, 26 under the factory names | the patch on the display |
| Xpander (LESINDES) | `VS7MldWcmYM`, `6n_ONjfJzZ0` | sequenced, no FX | none |
| Xpander (tomm noise) | `R5QXJDEJlyk` | presets, dual mono | none |
| Xpander (Collector Current) | `11MKJkpqzwQ` | VCO FM, mono | none |
| Xpanders (AustinSynthLabs, HanzVolt) | `h_BU2jK6czw`, `wOHqSobYrhw` | filter modes, by hand | the mode names in captions; FREQ, RES and MODE on the display (`wOHqSobYrhw`, two notes at RES 53-63) |
| 4 Xpander patches (Modular Samples, CC0) | `private/reference/freesound/` | single notes MIDI 36-96, vel 127 | unknown patches |
| Matrix-12 (gstormelectro) | `NuBRcBlNAH8`, `Iz_dFadQSuI` | all 15 modes on a saw, then at full resonance; FM | on screen only |
| Matrix-12 (Vulture Culture) | `Yxxwb4d8GHA` | level-matched A/B with a plugin; resonance values, filter FM, PW, noise | in the captions |
| Xpander (Andy Whitmore) | `NJrIT7_8N0Q` | single patches 00-99 in order, live; 89 of the names match FACTORY1.SYX | the patch on the display |
| Matrix-12 (Really Nice Audio) | `itLhB9Yn1DE`, `5ZsQmJHbN0A` | two free user banks (Urbanek, 100 patches; Springer, 100 effects) in order, near mono; not for tone: 5-7 dB brighter than ours above 4 kHz in every patch whatever its filter does, where the Xpander recordings and `Yxxwb4d8GHA` agree with ours; its noise patch (Springer 95, noise through a notch at FREQ 0) is 3-5 dB brighter than white noise at 4-12.5 kHz in every frame, which no noise source is, so the lift comes after the voice: that unit's output or the recording chain, which nothing in the video can tell apart. Either way one unit against three sources that agree with ours. `itLhB9Yn1DE` also carries a reverb: after a stop, a tail 23-25 dB under the sound at 0.1 s, falling about 30 dB/s, its L and R uncorrelated where the sound is near mono (0.96). So notes, pitch, envelopes and patch-against-patch differences only | the patch on the display, the banks' SysEx in hand: every setting |
| Matrix-12 (Vulture Culture) | `Etz45W_z7ns` (live stream), `RmVB7CshXz4` | factory singles by name (LIMUIDZ through every filter mode, BIG-SYN2 at RES 63); a filter-FM sound | named, or matched in a plugin on screen |

No recording has full stated settings or comes from a voice out.

The `NuBRcBlNAH8` takes carry coding noise ("dirt"): a floor between the harmonics 30-50 dB over
the model's in the high-pass, notch and 1P LP modes. It follows each mode's spectrum and is nearly as
strong in L-R as in L+R, where a mono voice's own noise would sit 25 dB lower, like its harmonics.
It is uncorrelated from harmonic to harmonic: the VCO is steady to 0.15 cents, so there is no jitter
to model. The model's render through MP3 128k then Opus 208k (an assumed 2016 upload, then
YouTube's rate) lands within 0-10 dB of it. The private A/B scenes have that as a source
(2026-11-13). Of the `Yxxwb4d8GHA` takes only the 8.4 kHz triangle needs one (Opus alone, at the
download's 138 kbps, 2026-11-19); the others' floors match without.

## Budget

- **CPU:** `xpanderbench`'s `chord6-48k` (six voices held) under 29 % of a core at 48 kHz. Last
  measured: 23.6 % (2026-11-23, round 23); `note-48k` 6.6 %, `seq-48k` 9.1 %, `idle-48k` 3.4 % (all
  voices asleep: the two CPUs).
- **Check set baseline:** 2026-11-23 (round 22), golden renders `build/xpander-golden-22` (bit-exact since); service
  checks 9/9 (`tools/service/check.py`); SPICE suite 88 ok (`tools/spice/suite.py`); the controls'
  extremes (`xpandercorners`: 1,238 cases at 44.1 and 48 kHz, no unconverged solve, no noise where
  nothing makes it, 10-30 s); plugin test, host tests, panel test pass; auval (round 10); pitch
  per note against the freesound notes (`tools/compare/notes.py`). The factory tune is cached as
  `factory-r13.ram`, with the factory patches `factory-r13-patches.ram` (`core/warm.h`).

## Plan, in order

1. Watch items: the saw level against the triangle (round 3), RES 63 in FD-high modes on other
   units (round 5; a second Matrix-12 agrees by ear, round 19). (accuracy, waits on data)
2. Output level: no recording gives an absolute level (patch-to-patch levels agree within 1.6 dB rms
   across the factory demo since round 12). (accuracy, waits on data)
3. CPU: with six voices the filter (its solve and tanh, about half) and the VCOs (about 28 %, the
   HF network's exp in their event solve) are the cost; the tanh calls the solve needs are left
   (round 22), the CPUs' plumbing is done (round 23), and the 6809 core's instruction loop (about
   8 % with six voices, a third of idle) would need a rewrite. What's left moves bits: fewer Newton
   iterations, a reciprocal in the S&H updates; each needs a decision. (cpu) The filter's Newton takes 2.4 iterations on average, 7 at most over the extremes;
   `xpandercorners` checks any such change (capped at 4, 1 % of its samples stay unconverged).
4. **Drift** (accuracy): the oscillators' slow wander with temperature, always on with no setting
   (no machine has a drift or noise-off switch, decided 2026-11-24), as the Minimoog's
   (`machines/minimoog/docs/model.md`, "Oscillators' noise and drift"): cents per kelvin from this
   circuit's parts (the CEM3374s' own drift, the temperature sensors U805 reads and what the
   firmware's tuning takes out of it; six voices, so their beats move), a few kelvin over a session
   as slow random walks pulled back to tuned, checked against a long held note of a warmed-up unit
   if a recording has one. Added 2026-11-24; not scheduled yet.

Waits on data: the RES 62/63 pitch, the input taper, the comparator delay, the RES scale in
4P LP, VCF OUT's gain on a second unit (above).

## Tried and dropped

- 2026-11-22 (round 19): the cut-off against a Matrix-12's displayed FREQ while its owner sweeps a
  strings patch at known FREQ and RES (`nZ_tj1aQsnU`, values read off the display twice a second).
  His patch is an edited one (not the factory STRANGS1: FREQ 50 against 91, 2 POLE LOW against
  4), so its keyboard tracking and filter envelope are unknown, and ours oscillates at 6.5 kHz
  where his peaks sit at 2.0-2.5 kHz. LIMUIDZ (`Etz45W_z7ns`, 23:02-26:00) for the phaser gain: at
  its stored cut-off the phaser's low end barely depends on it (x1.00 and x0.88 within 0.6 dB on
  every partial), and its modulated FREQ moves the notch reference. The Springer bank's video is
  sound effects: no notes to transcribe.
- 2026-11-22 (round 16): reading envelope faults from the factory demo's raw transcription. The
  flags it raised were the transcription's: notes an octave off (45 of 82 patches), bell notes cut
  to 0.17 s while the player held them (ORCHBELL: -19 dB median from ours entering its release; held
  to the next strike, -2.5), a stretch's start on speech or silence, and drum hits missed (ROCK TOM,
  WEIRDLFO, JUNGLE, MELOCOMP). SAMPLETY's apparent sustain was the earlier strikes still ringing in
  other voices: over the whole stretch, struck as he struck it, ours follows his envelope dips and
  all. Controllers reach the firmware (mod wheel changes 6 factory patches, pressure 7, bend 81).
- 2026-11-22 (round 15): a second unit for the phaser modes' low end. `wOHqSobYrhw` shows FREQ, RES
  and MODE on the display (3 PHASE+1 LOW at FREQ 33 RES 56; 3 POLE PHASE at FREQ 5 and 21, RES 55,
  next to 2 POLE NOTCH at the same FREQ 5 and 24), but it plays two notes about a third apart (or
  two VCOs) at RES 53-63, so no window holds one note to set a phaser mode against the notch;
  `h_BU2jK6czw` captions its modes but shows no settings; `Yxxwb4d8GHA`'s phaser passes have two
  detuned VCOs and the filter envelope moving. Checking the VCF OUT gain by level instead: the
  factory-patch clips scatter 8 dB rms against ours (the factory demo's 3P/4P LP patches 1.2 dB
  louder than the rest, the dry video's 2.8 dB quieter, on 3), NuBRcBlNAH8's mode clips 2-3 dB.
- 2026-11-22 (round 15): VCO and filter FM against `Yxxwb4d8GHA` (19:30-26:10). The amounts can't
  be read (the Matrix-12's display blooms at 1080p, and the plugin beside it shows percentages):
  only "half" and "full" are spoken, with the cut-off unknown. The FM path is the schematic's [R]
  under the real firmware.
- 2026-11-22 (round 13): modelling NJrIT7_8N0Q's recording chain to explain its slower decays. The
  chain is stated for the same player's other Xpander video (an Avalon 737 preamp, which has an
  optical compressor, and a Waves L2 limiter on the master) and the audio shows it: 7.9 % of 100 ms
  blocks peak within 0.5 dB of full scale (qfg9Jni8nN8: 0.6 %). Fitted over the ten preset scenes
  (ours with the fitted reverb, one drive for all): a lookahead limiter barely engages at the drive
  the levels call for (shape rms 2.85 to 2.82 dB); a compressor helps a little at 1.5:1 (2.56 dB),
  steeper ratios fit worse. Neither explains E. BANJO's decay (half our dB rate over 200 ms) or
  TACPIANO's there; the dry unit's TACPIANO follows ours.
- 2026-11-20 (round 12, first pass): fitting the input-VCA taper to the attacks of patches that
  move VOL during a note (FAKE-DBL's crossfade, E PIANO3's tine, PLUKFLUT's pluck, SF-FLUTE) by
  their level envelopes and moving VCO's partials. Every measure but one ranked the old curve worst
  or near it, but they disagreed on how much flatter, and none fitted: chords struck slightly apart,
  legato lines (the last note still sounding), reverb and unknown velocity swamp a 3-10 dB question.
  Steady patches settled it instead (Done, round 12). Also unusable as steady tests, found while
  scanning the factory set: OBOEHEIM and TINGO (VCO2 frequency-modulates VCO1, so every partial is
  on one series), WOBBLES (sync), MYSTERY (its pitch moves); NASTYSAW and DISTORTD (4.06 V against
  4.67 V) separate the curves by only 3.5 dB, inside their scatter, and disagree with each other.
- 2026-11-13: raising the CEM3374 comparator delay alone to cure the low-end droop (1.25-1.3 us gives
  the same pitch). TUNE ALL's PW verdict turns erratic: PASS at 0.5-1.3 and 1.5 us, FAIL at 1.4,
  1.75, 2.0 and 2.4 us (the measured duty jumps about 10 counts between adjacent corrections across
  the firmware's 4-count window). The bulk resistance passes at every value from 8 to 11.5 ohm.
- 2026-11-13: FAST for the VOICEN* write cycle only (research.md 4.1 used to read it so). DAC MONO
  passes that way too, but its first count lags the sweep's jump back by 22 counts. The schematic
  gates FAST with the live A0, not the write, so the model keeps it on for the window.

- 2026-11-19: model changes for the throb in the 4P LP max-resonance scene (round 11). None of
  these moves harmonics 1-6, where most of the difference was: twice or half the input level into
  the filter, twice or half the resonance pair's range, the compensation node at 0, 0.77 (the data
  sheet's 22K/1K) or 1.3 times the board's divider, the resonance gain from 0.6 to 1.06 times with
  the cut-off retuned to hold the resonance at the clip's pitch, and the feedback's coupling corner
  from 4 to 100 Hz. The throb topped out near 12.7 dB against the clip's 23.5.

## Declined

- 2026-11-13: renaming the plugin ob-poly-x after the XPANDER trademark finding (research.md 11);
  decided to keep ob-poly-xp.

## Done

- 2026-11-23, CPU round 23: **the CPUs' plumbing and the voices' control tick**, bit-exact (golden
  renders `build/xpander-golden-22` exact on all five workloads; `xpandercorners` the same solve
  counts as round 22). The main board's FIRQ timer counts all the elapsed 8-cycle steps at once
  (was a loop pass a step); the 6809's dummy bus cycles ($FFFF, ROM on both boards) only count
  their cycles; RAM reads go straight to the boards' arrays (writes keep MEMORY PROTECT); a voice
  skips its control tick when its last one changed nothing and no S&H was charged since. The old
  and new benches alternated four times: idle-48k 3.75 -> 3.40 %, note-48k 6.89 -> 6.55 %,
  seq-48k 9.27 -> 9.11 %, chord6-48k 23.7 -> 23.6 % (within its spread). With six voices held the
  two CPUs are about 8 % of the time (round 22's 37 % came from a profile of all the workloads
  together); idle, the 6809 core's own instruction loop is 36 % and the voices' S&H updates 21 %
  (exact divisions, which a reciprocal would change). The firmware has no dominant idle loop to
  skip: the busiest, on the voice CPU (A624-A631), is 18 % of its instructions.

- 2026-11-23, CPU round 22: **the filter's last tanh**, bit-exact on the golden renders. Once the
  Newton solve has converged (its last step under 1e-12 V), the cells' state and pin 17 take their
  tanh from the last iteration's value plus slope x step (the next term under 1e-21, below a
  double's resolution) instead of five more tanh calls a sample; an unconverged solve computes
  them as before. chord6-48k 25.0 -> 23.4-23.8 %, note-48k 7.2 -> 6.8-6.9 %, seq-48k 9.7 -> 9.2-9.4 %,
  idle unchanged. Golden renders `build/xpander-golden-22`: all five workloads exact. At the
  controls' extremes (`xpandercorners`) 169 of 89.8 million filter solves fewer: rounding-level
  differences moving a voice's sleep by a sample; 0 unconverged, as before. Checks as round 21.
  Tried and dropped the same day: a polynomial tanh (e^-2|x| by a degree-12 polynomial, within
  3e-16 of std::tanh, 4x its throughput alone, differences -235 to -283 dB): slower in the solve
  (chord6 34 %, on the four cells alone 27 %), where each tanh waits on the last and its longer
  chain loses to the library's. The shared noise generator costs 0.25 % of a core: not worth a
  new random sequence.

- 2026-11-23, accuracy round 21: **the Matrix-12 bank's outliers, a dry listening set, unit
  character**; one model change (the VCO core's mismatch).
  - The `itLhB9Yn1DE` patches that stood apart from the rest of that video: SYNFLUTE's long notes
    are its PEDAL 1 release modulation and the unplugged-pedal reading (Behaviour above); BIG BASS's
    filter opens at once on the Matrix-12, where ours waits for its envelope's DELAY 3 (47 ms). The
    Xpander does as ours does: factory APOLLO has the same filter envelope, and in `NJrIT7_8N0Q`
    (788-801 s) its pluck peaks about 60 ms after each strike, as ours does, at about the same
    brightness. Ours times DELAY 63 at 2.75 s (the manual: 2.5 s maximum). So it is that model's
    firmware (its main ROM is its own), not the voice. STR1N1.3, JOY-1 and CLASSIC (LFOs, ramps and
    VCO FM on the cut-off and levels) found no cause on our side; that recording also carries a
    reverb (Reference data).
  - A dry listening set: only recordings with nothing after the synth, against the plugin as it is
    (the private scenes; `qfg9Jni8nN8`, `Yxxwb4d8GHA`). In it, S.GENVIV was 11 dB darker in
    harmonics 5-15: the player switched to it from patch 05 a moment before the strike, and the
    voices' envelopes carry over a patch change (the firmware's, ours included); played with that
    lead-in, ours is within 2 dB.
  - **Model change, unit character:** the VCO core's up and down currents differ by 0.3 %
    (`Cem3374::Params::asym`; model.md): the triangle's even harmonics then match the Matrix-12's
    takes (h2/h4/h6 -53/-60/-64 dB against -53/-59/-64 at C3; before, under -105). Chosen by ear
    over a symmetric core (the same five scenes at 0 and 0.3 %). The pulse's width shift with pitch was
    already in (`pwVbe`). Checks: golden renders `build/xpander-golden-21` (before): idle -170 dB,
    note-48k -53, note-44k -51, chord6-48k -20, seq-48k -26 (the new tune's VCO phases in the
    last two); TUNE ALL 5/5, service 9/9, SPICE 88 ok, `xpandercorners` 0 unconverged, plugin, host
    and panel tests pass, pitch per note unchanged, auval passes; CPU chord6-48k 24.7 -> 25.0 %
    (within the run-to-run spread; the mismatch is a choice between two currents per reversal).
    Factory cache `factory-r13`; the scene states retuned.
  - The render tool takes `cv:N,V` (CV and pedal inputs) and `vcoasym:X`; MASTER > MIDI > CTRLS
    drives the pedals from MIDI controllers (Behaviour above).

- 2026-11-22, accuracy round 20: **a free user bank on a Matrix-12**, no model change. Peter
  Urbanek's 100 single patches (free; the SysEx in hand, so every setting is known) played in order
  on a Matrix-12 with the display in shot (`itLhB9Yn1DE`, near mono); the Matrix-12's singles load
  into ours as they are (`private/reference/tools/load_bank.py`). 42 patches read off the display,
  105 windows swept as round 17: on the 90 clean ones the envelopes' median difference is 0.3 dB,
  the spectra correlate 0.81 (median), and the median band offsets sit within 2.4 dB from 63 Hz to
  8 kHz (ours +2.3 dB at 500 Hz-1 kHz, -1.4 and -1.9 dB at 4 and 8 kHz). The best-specified check
  of the patch-level sound so far; no patch stands out but MYSTERY, whose pitch moves by design.
- 2026-11-22, accuracy round 19: **Matrix-12 recordings with known settings**, no model change. A
  search for recordings that settle the open gaps found none for the RES 62/63 step, a VCO well
  down against the other, or a fixed 50 % pulse. From what it did find:
  - VCF FM: a Matrix-12 at FM 63 into 1P HP at RES 63 (`RmVB7CshXz4`; its values matched in a
    plugin on screen) against ours: octave bands within 2.0 dB rms (3.8 at FM 32, 5.5 at FM 0), so
    the depth holds and HAMNCHEZ's top is that patch on that unit.
  - Self-oscillation: on a second Matrix-12 (`Etz45W_z7ns`, 96:08) the owner hears 1P and 3P not
    going into full self-oscillation at full resonance and 2P and 4P going, as ours (round 5); BIG-
    SYN2 (4P LP) at RES 63 oscillates on both.
  - The Matrix-12's factory singles (FACTORY2.SYX) load into ours as they are.
  The phaser gain and the swept cut-off were tried and dropped (above).
- 2026-11-22, accuracy round 18: **voice-to-voice tuning**, no model change. `3DFsES1SgkM` plays
  six-note chords on a unit whose TUNE ALL passes: the notes of one chord agree within +-1 cent (a
  clean patch, the whole chord 17-19 cents sharp: the patch's detune) and +-2 cents (a fuzzier
  one), the per-note scatter the model already has (0.6 cents; real units 0.5-1.0). Which voice
  plays which note can't be told (two pan positions, octave doublings share partials), so nothing
  per voice is measurable; the voices stay alike but for their levels.
- 2026-11-22, accuracy round 17: **the factory patches against the dry unit**, no model change.
  qfg9Jni8nN8 (dry; the display read every second: 26 of the patches he plays carry factory names,
  some likely edited) gave 73 windows of up to 6 s, swept as round 16 (octave and held notes fixed,
  no reverb). Envelopes agree, the median difference within 1 dB on all but a transcription miss.
  Tone shows no tilt: the median band offset over 42 clean windows within 0.2 dB from 63 Hz to 4
  kHz. Above 2 kHz single patches scatter +-10-20 dB both ways, and the two units often disagree on
  the same patch (GEOFFUNK 20 dB darker in ours here, level on the demo; E. PIANO the other way),
  which is patch edits and unit spread. The same way on both units: PHASEPAD's top (Known
  inaccuracies, a watch item) and MARIMBA's 8 kHz band, which is 80 dB under its peak (up to 3 kHz
  it matches closely).
- 2026-11-22, accuracy round 16: **the factory patches against the factory demo**, no model change.
  82 of the 100 patches had a stretch without speech in NJrIT7_8N0Q (2-8 s, its transcribed notes,
  velocity 100, ours with the video's fitted reverb). With each patch's octave and held notes fixed
  (Tried and dropped), the spectra correlate 0.85-0.96 on most and the envelopes' median difference
  sits within 2 dB; the median band offsets over 62 clean patches are within 1 dB from 63 Hz to 8
  kHz, so the demo's chain has no tilt to speak of. The demo's phaser patches agree with round 15's
  VCF OUT gain (PHASEPAD's low bands 2.4 to 0.9 dB rms). Left: VCF FM's high sidebands on HAMNCHEZ
  and VCF-FMOD (Known inaccuracies, a watch item). No code change.
- 2026-11-22, accuracy round 15: **VCF OUT's gain x88, for the phaser modes' low end.** First the
  NuBRcBlNAH8 mode scenes were refitted with that video's 82 Hz low cut allowed for (RES -6 to +6,
  FREQ 0-127): most fits improved (4P BP max 7.3 to 4.3 dB rms, 2 HIGH+1 LOW 4.6 to 1.5, 2P LP 5.0 to
  1.9), but the two phaser modes stayed off the same way at every RES: the clip's fundamental 6-7
  dB over ours (12 dB through the low cut), its 2nd and 3rd harmonics 2-3 dB and 1 dB, the notch where
  ours is. The phaser modes cancel at DC, with VCF OUT weighted about 4 against the board's x101 cell
  buffers, so their low end hangs on VCF OUT's own gain on the cell-4 voltage: the data sheet's x100
  has no tolerance. Swept in an experiment build: 0.88 fits all four phaser scenes (the shape error
  summed with 4P BP's 12.9 to 10.7 dB rms) and 3P HP and 3 HIGH+1 LOW don't move; above 1 the low
  end comes back too, but 4P BP then fits worse. Now `Cem3372::Params::outGain` 88 (pin 17, the
  resonance input, the compensation node, the noise scale) with the resonance gain 115 x 100/88 so
  the fitted loop gain holds; factory cache r12.
  - Phaser scenes, through the low cut: 3P PHASE 3.8 to 3.0 dB rms, its fundamental -11.5 to +0.6 dB
    against the clip; 3 PHASE+1 LOW 3.3 to 2.4, -12.5 to -0.7 dB; the other modes within 0.1 dB, or
    better. The factory patches in these modes (PHASEPAD, SAMPLETY, BAMBOO, AIR REZZ) gain 5-13 dB
    below 400 Hz; the private `phaser/` A/B scenes have both gains.
  - Resonance, each model on its own factory tune: the RES 63 oscillation at FREQ 24, 44 and 64
    lands on the same pitch (155.2, 491.8, 1574.0 Hz); scene 22's ripple 0.207 to 0.209, its
    harmonics 1-7 within 0.8 dB. On the round-12 tune the RES 63 oscillation sat 24 cents low: the
    cells swing more for the same pin 17 level, and the VCF tune moves the cut-off up as much (about
    a fifth of a FREQ step at RES 0-62), so a notch on the fundamental (2 NOTCH+1 LOW) needed
    refitting. The private scene states now carry the r12 tune.
  - Level: 3P and 4P LP 1.1 dB quieter against the other modes (patch 01 peaks at -29.0 dBFS, was
    -28.0). Nothing tells: the factory demo's patch levels scatter 8 dB rms (Tried and dropped).
  - Check set: TUNE ALL 5/5, service 9/9, SPICE suite ok, `xpandercorners` 1,238 cases with no
    unconverged solve (VCF at most 7 iterations, mean 2.41), plugin, panel and host tests pass, auval
    ok, pitch per note unchanged; chord6-48k 25.0 % (26.2 %); golden renders differ (LP level, the
    retune), `build/xpander-golden-15` written.
  FM and a second unit's phaser were tried and dropped (above).
- 2026-11-22, accuracy round 14: **tone on the dry factory-patch scenes**, no model change. COLONS'
  resonant cluster at 2.6 kHz that ours lacked was a note the transcription missed (with 53 added
  no peak 25 dB or less under the loudest is unmatched either way); STRINGS1's one extra partial
  (957 Hz, 15 dB down) stays under every note set (a beating null on the unit's strike); OH WOW
  matches within a few dB per harmonic. Velocity reaches the firmware (3 of the 100 factory patches
  use it: 21, 31, 51), so it isn't what differs on the others. Pitch: the factory demo's unit sits 3
  cents under ours, synthlegends' 10 cents over (unit tuning). Check set unchanged (no code change).
- 2026-11-22, accuracy round 13: **early decays on fading patches**, no model change. On scenes cut
  like the clips (a rendered "lemus" source: looping the plugin live let one loop's release blunt the
  next strike, 3-17 dB, the main cause of "more envelope peak on the unit"), ours falls a median
  1-2 dB faster over the first 100-250 ms; the largest gaps (E. BANJO 8 dB, TACPIANO) are on
  NJrIT7_8N0Q, whose chain compresses and limits ("Tried and dropped"). The dry unit's TACPIANO
  follows the firmware's VCA CV through the linear VCA within 2 dB for 400 ms, as ours does; its
  other differences are within the beating of detuned VCOs from strike to strike. Check set
  unchanged (no code change).
- 2026-11-20, accuracy round 12: **the input VCAs' taper** (VOL A/B), from "the env sounds different
  in almost all of these clips, more peak in the real ones" on the new dry factory-patch scenes
  (qfg9Jni8nN8). Measured per scene first: the firmware's envelopes, the VCA S&H and the final VCA
  follow the unit (TACPIANO's VCA CV through the linear VCA gives the unit's first 400 ms within
  2 dB); the patches that differed most move VOL during a note, where the guessed taper (gain 0.1
  at 2.5 V) all but shut a VCO at 0.3-1.5 V.
  - The measurement: VIBES, VCO1 a triangle at 4.67 V and VCO2 a triangle two octaves up at 2.69 V,
    no FM on the playing voice, no sync. A triangle has no 4th harmonic, so that partial is VCO2's
    alone against VCO1's 3rd and 5th. On the dry video (one note, two 0.4 s halves) and the other
    video (two chords, another unit) the old curve puts VCO2 8-11 dB too low; (e^(k x) - 1) /
    (e^k - 1) with k = 0.7 (gain 0.41 at 2.5 V) fits within 0.5 dB dry and 1.5 dB on the chords
    (k = 1: +0.9 to +1.6; k = 0.5: -0.1 to -1.9).
  - The gain at 5 V is now 28.6 (data sheet 28-42, typical 35), which holds the gain at VOL 63
    where it was: patches at full VCO volume render as before (the 31 A/B scenes of the Matrix-12
    takes and filter modes: 36-55 dB under the signal; the same stored tune on BLANK: -82 dB).
  - Check, not fitted: across the ten factory-demo scenes (NJrIT7_8N0Q, one unit, one take) the
    spread of clip-minus-ours levels falls from 4.7 dB rms (range 15.7) to 1.6 dB (5.6): E. BANJO
    (VOLs 2.09 and 1.40 V) from 12.7 dB off the median to 0.3, E PIANO3 7.6 to 1.1, SF-FLUTE 3.0 to
    0.1, HARPSI 3.4 to 3.4 the other way. On the dry video's twelve 6.0 to 4.9 dB (FAKE-DBL 10 dB
    nearer).
  - Factory cache r11 (TUNE ALL stores corrections 1-2 counts apart, about 1 cent on the VCOs). The
    golden renders differ by that re-tune (-20 to -31 dB peak, the VCOs' beating moving); with the
    stored tune unchanged a full-volume patch is exact to -82 dB. Golden renders: build/xpander-
    golden-11 (before), -12 (after); round 11's -10 was never written (the check script didn't make
    the folder). Check set otherwise unchanged: corners 0 unconverged, SPICE 4/4, service 9/9,
    plugintest and host tests pass; chord6 26.2 % (25.4 % before, run-to-run noise: the taper is
    computed only when a level changes).
- 2026-11-19, accuracy round 11: **the resonance's pitch and throb in the A/B scenes** (by ear,
  the 4P LP max-resonance scene: "the resonance pitch is different", then "more amplitude movement in
  the original"). No model change: the scenes and the recording explained both.
  - Pitch: the scenes' FREQ, fitted on the whole harmonic shape, put our resonance 93-295 cents
    under the clips' in the four scenes where it stands clear (2P BP, 2P NOTCH max, 4P LP, 4P LP
    max). Refit by the harmonics around it, those come within 13-58 cents. Not the factory tune: on
    the current calibration the old FREQ resonates at the same pitch.
  - The low cut: every clip of that video carries a low cut, in every mode, low resonance included:
    the fundamental sits about 5 dB low and leads ours by about 100 degrees. Fitted on the 4P LP
    max-resonance clip's harmonics 1-8, levels and phases: a second-order Butterworth at 82 Hz (Q
    0.71), a mixer's 80 Hz low-cut switch; the low-resonance clips (4P LP, 2P BP, 2P HP) agree within
    a few degrees. A first-order cut leaves the fundamental 23-31 degrees short. It is the recording
    chain: the Xpander's own couplings are at 2-4 Hz. This was the "fundamental under high
    resonance" gap (logged 2026-11-13, then put down to the voice because the low-resonance clips'
    levels hid it; their phases show it).
  - The throb rides on harmonics 7 and 8, either side of the resonance, and on the fundamental's
    phase. Refit with the low cut taken into account (FREQ 44, RES 52), ours as recorded has
    harmonics 1-10 within 0.7 dB and 3 degrees of the clip, the resonance at 498 Hz against 494,
    and a throb of 25.2 dB against 23.5 (ours as the plugin plays it: 11.3). The changes tried in the
    model are under "Tried and dropped".
  - The private A/B scene has an "as recorded" source: the plugin through that low cut, then the
    upload's codecs.
  - The 8.4 kHz triangle (by ear: "more distortion in the real one"): Opus's noise around a high pure
    tone, about -42 dB; ours through Opus at the download's rate lands at -44 dB and sounds the same
    (A/B, by ear). The clip also has a 2nd harmonic at -54 dB (16.8 kHz) and a 0.2 dB level wobble;
    ours has neither.
  - Factory patch A/B scenes (private): an Xpander playing patches 00-99 live (NJrIT7_8N0Q), the
    patch read off its display. Its passages transcribed (basic-pitch) weren't accurate enough by
    ear; what's kept is one isolated chord per patch whose notes check out against the clip's
    spectrum: 10 patches.
  - Check set unchanged (no code change; the golden renders named `build/xpander-golden-10` were
    never written), service 9/9,
    SPICE suite ok, `xpandercorners` 0 failing, plugin and host tests pass; `chord6-48k` 26.1 %.
- 2026-11-16, checks: **every iterative solve counted, retried, and checked at the controls'
  extremes** (`xpandercorners`). A solve that runs out of iterations and goes on with what it has
  is silent in every other check: SPICE stops short of the extremes, the recordings never sit
  there, golden renders keep whatever it did. On another machine one turned a filter's
  self-oscillation into white noise.
  - The solves, all in the voices, are now counted per voice (`core/solver_stats.h`, read through
    the processor):
    - the CEM3372's Newton, one per sample: 16 iterations, then taken as it stood. Over the
      extremes it needs at most 7 (mean 2.4), so none fell short. Now a sample that doesn't
      converge in 16 is done again from its start in 2, then 4, 8, 16 shorter steps with the inputs
      held, the last split run to its end whatever it gets. Capped at 6 iterations, 32 of the
      corners' cases fail at 48 kHz (FD modes at the top of FREQ, RES high, an ultrasonic input).
      Capped at 4 with the samples taken as they stood, as before, 1 % of the samples at 48 kHz
      stay unconverged, in 150 cases, and nothing else shows it: the noisiest case goes from
      flatness 0.07 to 0.15, under the limit. As a separate call the step cost 0.8 % of chord6-48k's
      instructions, so it is inlined;
    - the CEM3374's event times (the triangle reaching V_TH or 0, the saw crossing the pulse
      width), 8 iterations of Newton inside a bracket: the real failure, below;
    - the CEM3374's bulk-resistance solve, 4 Newton steps at each new control current: the
      currents reach x = 0.37, where the last step moves by 1e-12. Now checked;
    - the CEM3374's event loop, 24 events a sample, the rest of the sample dropped beyond: at most
      5. Now counted.
  - **The failure: event times.** The solve took a Newton point only strictly inside its bracket.
    Between events at audio pitches the HF network has settled and the pin rises in a straight
    line, so the first guess is the root. The next point lands on the bracket's end, or a rounding
    error past it, and is refused; the loop then halves towards the root 8 times and stops short.
    - 45 % of note-48k's events came out so, 60 % of chord6-48k's, 53 % of seq-48k's: up to
      0.14 us early or late, mean 16 ns (0.3 % of a sample at 192 kHz).
    - Pitch moved -0.04 to +0.07 cents from 100 Hz to 4 kHz, +0.3 cents at 22-43 kHz (the VCO
      through its S&H). Between the harmonics the jitter sat about 120 dB under the note (the
      chord's floor is 7-9 dB lower now).
    - With both VCOs ultrasonic (one voice at VCOF +4 and +5 V) it made broadband noise: 54 of the
      corners' voice cases at 48 kHz, median flatness 0.20-0.32. None of the cases set up through
      the firmware was noisy.
    - Fix: from the bracket's upper end, Newton steps clamped into the bracket, halving only when a
      step leaves it, until a step or the bracket is under 1e-13 s. That takes one or two more
      iterations (16 at most over the corners).
  - `xpandercorners` (`tools/corners`): 1,238 cases at 44.1 and 48 kHz, 10-30 s.
    - 134 through the plugin's processor, set up on the panel so the firmware writes the CVs: the
      15 filter modes at FREQ 127 and RES 63; FREQ 0; RES 0; FREQ and RES swept by hand; the VCF FM
      at 63 with VCO 2 at both ends; the linear FM at 63; both VCOs at FREQ 63 with every wave and
      sync; pulses at PW 0 and 63; noise; MIDI 0-127; a six-note chord; the 100 factory patches at
      MIDI 36 and 96.
    - 1,104 of one voice with its S&Hs written directly over the DAC's range: every mode with FD
      low and high, VCFF -5 to +5 V, RES to 4.86 V, the VCOs at -5 and +5 V, the MDAC at 255 into
      the VCF and into VCO 1, sync, the pulse widths' ends, VCFF jumping -5/+5 V with FAST writes.
    - It fails on any unconverged solve; on median spectral flatness over 0.2 where no noise
      source plays (exempt: the six factory patches that switch the noise on, and a voice whose saw
      or pulse sits under 50 Hz, whose clicks are flat in any window); on output not finite or past
      its bound. Now: no unconverged solve, the noisiest case 0.07.
    - Proof: with the old event-time solve every case fails (618,028 unconverged at 48 kHz) and
      54 are noisy; with the CEM3372 capped, as above.
  - The tune: with the new event times TUNE ALL (5/5 PASS) stores slightly different corrections.
    74 bytes of the battery RAM change: the tune tables move by one to three steps in 51 places
    (three voices' entries in one table by a larger step), the rest is working state. The factory
    tune is now `factory-r10` (`core/warm.h`).
  - Against golden-8: idle -166 dB, note-48k -46 dB, note-44k -45 dB, chord6-48k -25 dB, seq-48k
    -25 dB.
    - With the old tune, the code change alone keeps every level and octave band within 0.16 dB
      (note-48k's pitch -0.015 cents).
    - The new tune moves how each voice's two saws beat: chord6-48k +0.5 dB (its 4-8 kHz band
      -3.5 dB), seq-48k's 4-8 kHz band -3.7 dB, note-48k within 0.1 dB.
    - Golden renders `build/xpander-golden-9`. The CEM3372's change alone is exact on all five
      workloads against golden-8.
  - Pitch per note (`tools/compare/notes.py`, its own TUNE VCOS from power-on): the notes move
    0.4 cents rms (at most 1.9), the tune's steps. The model's four sets stay alike: residual rms
    0.3-0.7 cents (before 0.3-0.5), scatter 0.1-0.5 (0.2-0.4), stretch +0.14 to +0.23 cents an
    octave (+0.12 to +0.19); the units' scatter is 0.5-1.0.
  - Checks: service 9/9, TUNE ALL 5/5; SPICE 88 ok (the VCO within 0.09 cents of ngspice, as
    before); plugin, panel and host tests pass; corners pass.
  - CPU, instructions retired over whole runs (boot included): chord6-48k +0.38 %, note-48k
    +0.27 %, seq-48k +0.85 %, idle +0.13 % (the counters, the event solves' extra iterations).

- 2026-11-13, accuracy: **the low-end pitch droop**.
  - Cause: not a low-frequency fault. The lone VCO followed the exponential law at the bottom (+0.16
    cents at 32.7 Hz), but its high-frequency error was over-compensated (the 0.5 us delay costs 2 us
    a period, the board's 1.2K // 0.01 uF network takes 4.8 us off). The firmware corrects below C4
    along the parabola through its C4, E5 and G#6 corrections (not a straight line: research.md
    5.1), so an error proportional to frequency comes out at C1 about -5.9 times the C4 error.
  - Fix: the exponential converter's bulk resistance, I = I0 2^(-I R_bulk / V_oct), R_bulk 10 ohm
    [D: the CEM3340/3345 data sheet names it and the delay as the two causes of the HF error; F: to
    the freesound notes, any value from 9 to 11.5 ohm fits].
  - Pitch from each set's median, by octave of VCO frequency (33-62 / 65-123 / 131-247 / 262-494 /
    523-1047 Hz): model before -5.71 / -2.07 / +0.01 / +0.96 / +0.98 cents, after -0.16 / -0.18 /
    -0.12 / -0.08 / -0.03; the four real patches -1.21 to +1.03 / -0.24 to +0.03 / -0.24 to +0.49 /
    +0.07 to +0.35 / -1.64 to +0.17. Stretch +1.61 -> +0.03 cents/octave; worst note -8.1 -> -2.7.
  - Absolute after the tune: C1 -1.6 / -2.1 cents (VCO1 / VCO2), C2 -1.0 / -1.3, C3 -0.4 / -0.7, C4
    -0.2.
  - TUNE ALL 5/5 at 44.1, 48 and 96 kHz; the SPICE suite's VCO block within 0.09 cents (its deck
    has the bulk resistor too); service checks 9/9; plugin, panel and host tests pass.
  - CPU: chord6-48k +0.2 points (27.4 % with the S&H change); golden renders remade
    (`build/xpander-golden-2`; against golden-1: note -24 dB, chord6 -16 dB, the onset shift of the
    S&H change below and the new tune).

- 2026-11-13, accuracy round 10: **how the filter's oscillation bubbles** (by ear, the 4P BP
  max-resonance scene: "the original bubbles more"). That clip self-oscillates, the hand sweep taking
  the oscillation from 80 to 178 Hz across the saw's harmonics; its level ripples 0.16 (rms of the
  envelope) where ours rippled 0.08, and its 2nd and 3rd harmonics stand 8-10 dB nearer the
  oscillation. A gentler oscillation (loop gain at RES 63 nearer 4) leaves the driven harmonics
  stronger beside it and beats more: resonance gain 140 -> 115 (loop gain at RES 63 7.4 -> 6.1;
  120 gives ripple 0.13, 105 gives 0.24). After: ripple 0.15, harmonics against the oscillation
  -24 -22 -8 -13 dB (hardware -23 -19 -12 -11). The 4-pole-loop modes still oscillate at RES 63, the
  3-pole ones don't. Through the firmware's VCF tune the cut-off at a given FREQ moves down a little
  again: one saw at the default FREQ, -0.6 dB at 1 kHz and -2 dB at 8 kHz on C3, less higher up;
  FREQ 127's cells 23.4 kHz (24.2). The data sheet's onset range doesn't quite allow it (model.md).
  SPICE: the oscillation cases at RES 3 V (now under the onset, 3.17 V) moved to 4 V; 88 ok.
  Checks: service 9/9, TUNE ALL 5/5 (`factory-r9`); plugin and host tests pass; golden renders
  `build/xpander-golden-8`; chord6-48k 25.3 %.

- 2026-11-13, accuracy round 9 (2): **the noise's low end** (by ear: "ours is much higher"). The
  Matrix-12's noise take has a hump around 110-160 Hz (+13 dB against 1 kHz), a rise to it from
  1 kHz and a steep cut under 63 Hz; ours was flat there. Clipped band-limited noise alone doesn't
  make it (the clipping flattens it); the clipped broadband part plus a band of noise around 128 Hz,
  both high-passed at 73 Hz, fits within 1.5 dB rms from 25 Hz to 13 kHz (model.md). The same
  video's triangle at C1 has no low cut, so the cut is the noise path's, not the recording's.
  Through the voice with the filter open: within 1.5 dB from 31 Hz to 6 kHz (was up to 13 dB off
  below 200 Hz), 1-2 dB darker above 8 kHz. Bit-exact on the five workloads (none uses noise); the
  factory tune byte-identical; idle-48k 3.6 -> 3.85 % (a Gaussian sample per voice-board sample).

- 2026-11-13, accuracy round 9: **the pulse in the bass** (found by ear in the A/B scenes).
  - A Matrix-12's pulse run (Yxxwb4d8GHA 6:00, C4-C3-C2-C1) has steady duty cycles of 50.00, 50.27,
    50.55, 50.78 % (cycle to cycle within 0.03 %): even harmonics -55, -39, -37, -32 dB under the
    fundamental, where ours stayed at -52 to -64 (square at every note). Odd harmonics and pitch
    already matched within 1 dB and the sample.
  - Ruled out: an offset in the CEM3372's input pairs (any offset that touches the pulse makes the
    triangle lopsided first; the hardware's triangle is symmetric at C1, -55 dB), pitch jitter (both
    steady to the sample), a leak in the triangle core (the same triangle).
  - Linear in octaves, 0.26 % each, about one V_T ln 2 on the saw's 8 V: the comparator sees the saw
    shifted by a base-emitter voltage of the core's current. Modelled so (model.md), 1.15 V_T a
    natural log of the current [F]; the firmware's PW tune takes the constant part.
  - After: even harmonics -48, -39, -34, -31 dB at C4-C1 (hardware -55, -39, -37, -32). The PW
    tune passes; the PW corrections in the factory tune move (`factory-r8`).
  - Golden renders `build/xpander-golden-7`. With the r7 tune the change is bit-exact on all five
    workloads (their pulses are off); the r8 tune moves some VCO trims one step, which changes how
    the default patch's two saws beat (note-48k up to +7 dB in the 2 kHz band over its 1.5 s).
  - Also from the A/B scenes, not changed: the filter's resonant peak against the 3rd harmonic
    matches (4P LP +12 against +11 dB); gstormelectro's unit has 6 dB less fundamental in one 4P LP
    clip but not in its max-resonance one, so that waits on data.
  - Checks: service 9/9, TUNE ALL 5/5; SPICE 88 ok; plugin and host tests pass; chord6-48k 25.7 %.

- 2026-11-13, cpu round 8 (bit-exact against golden-6; warm-up RAM byte-identical): the 6809s read
  plain ROM directly (main board $A000 up, voice board $C000 up; their read() returns it unchanged
  and has no side effects there), instruction fetches and data alike, the cycle still counted:
  chord6-48k 25.2 -> 24.7 %, note-48k 7.1 -> 6.7 %, idle 3.7 -> 3.5 %. That was most of the bus's
  share; the rest of the 6809s' time is the interpreter itself.

- 2026-11-13, cpu round 7 (bit-exact against `build/xpander-golden-6` on all five workloads; the
  factory warm-up's RAM, with and without the patches, byte-identical):
  - The voice's per-sample coefficients of fixed time constants (S&H sharing, the control pins, five
    coupling capacitors) computed once per rate: chord6-48k 28.9 -> 27.7 %.
  - The CEM3372: the cells' prewarped gains and exp2 kept while FREQ and the capacitors don't change
    (dropped on a rate change), the input VCAs' taper while the level doesn't, the resonance path's
    two tanh shared between each Newton step's residual and Jacobian, the secant gains from the last
    tanh: 25.9 %.
  - The CEM3374: its exponential current kept while VCIN and the FM current don't change, the HF
    network's decay while the step doesn't: 25.4 %.
  - The 8253: counters 1 and 2 fast-forwarded over stretches where they only count down (no load,
    no trigger, no change of OUT; an exact rule per mode): 24.9 %, idle 3.9 %.
  - The S&Hs: one whose C1 and C2 have equalised to the last bit is skipped until written: 24.5 %,
    note-48k 6.8 %, idle 3.6 % (repeat runs 24.5-25.4 %).
  - Left, roughly: tanh in the filter's Newton solve about a quarter of the time (fewer iterations or
    a cheaper tanh would change bits); the two 6809s about 15 % plus 5 % in the bus (a templated
    core without the virtual bus might save 3-5 %); the CEM3374's event solve about 9 %.

- 2026-11-13, accuracy round 6: **UX14's on-resistance in series with cell 1's capacitor**
  (model.md). From the schematic: the CD4053B that selects cell 1's capacitor (FD) sits between
  pin 4 and the capacitor, 125 ohm typical. ECO 603 ties UX14's brand to the filter's oscillation,
  which is what the resistor does: it opens cell 1 near the top of the range and leads its phase.
  - The ngspice decks have it too (voice decks only; the chip-alone and mix-table checks keep ideal
    cells): SPICE 88 ok, oscillation within 0.02 cents.
  - Through the firmware's VCF tune (it tunes the RES 63 oscillation, which now runs above the cells
    near the top): FREQ 127's cells at 24 kHz, 34 kHz before. Default patch renders: -0.6 dB at
    2 kHz, -1.1 at 4 kHz, -1.5 to -2 at 8 kHz, levels unchanged. The RES 62/63 jump and its effect on
    the cut-off at the top wait on the same data as before.
  - The model's other 4053 path (the noise switch, UX14a) uses the same 125 ohm, was 200 [C].
  - Checks: service 9/9, TUNE ALL 5/5 (factory tune `factory-r7`); plugin and host tests pass;
    golden renders `build/xpander-golden-6`; chord6-48k 28.9 % (27.8 before; the solve iterates
    more).

- 2026-11-13, accuracy round 5: **which modes oscillate at RES 63**.
  - Found by comparing all 15 modes with NuBRcBlNAH8 (a Matrix-12 sweeping each mode by hand on one
    held saw; fitted frame by frame against a library of 135 model renders): the modes' shapes fit
    within 0.6-1.2 dB rms, their levels within +-1.5 dB of each other (13 of 15); no change there.
  - The max-resonance pass oscillates in the 4-pole-loop modes only; the 3-pole ones (FD high: 1P LP,
    3P LP, 1P/2P/3P HP, 2P notch, 3P phase) stay harmonic. The owner's manual: RES 63 oscillates
    "in some mode settings". The model, at the data sheet's typical resonance gain (190, loop gain
    10 at RES 63), oscillated in every mode.
  - The gain is now 140 (loop gain 7.4 at RES 63; model.md): FD-low modes oscillate (4P LP -25,
    2P LP -21, 4P BP -31 dBFS with no input), FD-high modes don't (-96 to -108 dBFS of hiss; 1P LP's
    peak 31 dB at RES 63, 17 dB at RES 62). RES 0-62 are unchanged: the RES tune scales them to the
    break point, which moves with the gain (now 2.2 V).
  - Side effect, from the firmware's own VCF tune (it tunes the RES 63 oscillation): the oscillation
    clips less, so the cells sit 241 cents above it instead of 463, and the cut-off at RES 0-62 is
    about 2.2 semitones lower than before for the same FREQ. Default patch renders: -1.2 dB at
    2 kHz, -2.5 at 4 kHz, -3.8 at 8 kHz, levels within 0.3 dB. No recording gives an absolute
    cut-off to check this against.
  - The video's owner (Yxxwb4d8GHA) calls 63 "fully self-oscillating" while on 1P LP: his unit may
    sit higher in the spread (the data sheet's onset range is 2.2-3.4 V). A watch item.
  - SPICE suite: its 3P LP cases now check that neither model nor ngspice oscillates up to RES 5 V
    (ngspice onset 5.5 V); the old 3P LP +3 cents case is gone with them: 88 ok, none failing.
  - Checks: service 9/9, TUNE ALL 5/5 (factory tune `factory-r6`); plugin and host tests pass;
    golden renders `build/xpander-golden-5`; chord6-48k 27.8 %.

- 2026-11-13, accuracy round 4: **the voices' levels differ, as on a unit**.
  - A real unit's voices: freesound's Melodirt set (one Xpander, one note per voice in turn) has
    per-voice levels -0.30, +0.42, +0.13, -0.12, +0.31, -0.41 dB (0.30 dB rms), each voice steady
    within 0.04 dB; the six voices explain 98 % of its note-to-note level change (random: 8 %). The
    other three sets are stacked or unison patches (no per-voice pattern).
  - Nothing trims voice levels: voice 1.4's TUNE VCA (F176) writes the DAC to 0 and toggles every
    voice latch between 0 and $FF until stopped; it is a service loop, not a calibration.
  - The model's voices were identical; each now has its final VCA gain set to that unit's offset
    (model.md). A 12-note rotation reproduces the pattern.
  - The same unit's pitch differs 0.26 cents rms by voice and its brightness 6.5 cents of centroid:
    left out (inaudible, and they come from spreads the firmware's tune mostly removes).
  - Checks: service 9/9, TUNE ALL 5/5 (factory tune `factory-r4`); SPICE 88 ok plus the known 3P
    LP; plugin and host tests pass (the plugin test's wake-up loudness check now allows the voices'
    spread); golden renders `build/xpander-golden-4`.

- 2026-11-13, accuracy round 3: **the oscillator waveforms against a Matrix-12** (Yxxwb4d8GHA
  2:15-6:30, hardware takes told from the plugin's by the 44.1 kHz aliases only the plugin has).
  Triangle with the filter open: odd harmonics within 1 dB of the hardware to the 19th; pulse at
  PW 31: within 1 dB; triangle against pulse level +5.0 dB (hardware +3.8 to +6.7 by note); the
  triangle at 8.4 kHz: pitch and top end within the takes' spread. No change. The hardware's even
  harmonics on the triangle sit at -50 to -58 dB (the model's are under -110): an up/down current
  mismatch of about 0.3 %, inaudible, not modelled. The saw takes are octave dyads (weak data):
  they would put the model's saw about 2.5 dB low against the triangle, where the schematic
  arithmetic (research.md 3) supports the model; a watch item.

- 2026-11-13, accuracy round 2: **the filter's own noise, the noise generator, the recordings**.
  - **Self-oscillation from silence** (a bug): a patch whose VCOs are off and RES 63 (the manual's
    "use the filter as a sound generator") stayed silent once its voice had slept, since the model's
    filter state was exactly zero with nothing to start it. The CEM3372 now has its own noise (50 uV
    rms at VCF OUT wide open, data sheet 200 uV maximum; model.md), each voice its own; it starts
    the oscillation within milliseconds, as on the chip.
  - **RES tune and the break point**: the firmware's RES tune finds where the filter starts to
    oscillate and spreads RES 0-62 below it, 63 above (SM p21). Without the noise it found
    oscillation only where a CV step kicked the filter: 1.94 V; now 1.88 V, inside the data sheet's
    onset range scaled to the board's divider (2.2-3.4 V x 0.768: 1.69-2.61 V, typical 2.15 V). With no input: RES 61 -98 dBFS of resonant hiss, RES 62 -46 dBFS
    (just short), RES 63 -21.6 dBFS (full oscillation). Yxxwb4d8GHA says the same of a real unit:
    "63 is fully self-oscillating, but 62 is just shy".
  - **Noise generator** (model.md): a 6 kHz random telegraph at +-51 mV, fitted to the Matrix-12's
    noise takes with the filter open (the plugin's takes beside them are white). Before: within
    1 dB to 4 kHz, 5 dB too bright at 10 kHz and 9 dB at 16 kHz; now within 1 dB from 600 Hz to
    13 kHz. Its level rose 8 dB in the audio band (the old placeholder spread its power to 96 kHz):
    noise alone now sits 7 dB under two pulse waves, the take about 3.5 dB (patch levels unknown),
    before 15.5 dB.
  - **Recordings and the factory patches**: the 100 factory patches were rendered at MIDI 36-84
    against the four freesound sets; none matches (best 8.7 dB rms of 1/6-octave shape against a
    median of 18-22, no stand-out), and their slot numbers don't match the factory names: they are
    user patches. The render tool can start from a saved RAM and type a patch (`--ram`,
    `patch:NN`).
  - Checks: service 9/9, TUNE ALL 5/5; SPICE 88 ok plus the known 3P LP case; plugin and host tests
    pass; auval passes. Against golden-2 levels are within 0.3 dB; with the same tune the code change
    is -90 dB on note-48k (the chip noise). The new tune (`factory-r3`) moves the break point and a
    few VCO/PW corrections by one DAC step; the rest of the 61 changed RAM bytes are the firmware's
    working state at the end of the warm-up, which shifts the phase of seq-48k's short high notes
    (its 8 kHz band -6 dB on three notes, 40 dB under the total).
  - CPU: chord6-48k 27.4 -> 27.8 %.

- 2026-11-13, feature: **the factory patches**. A new unit loads Oberheim's 100 factory single
  patches (FACTORY1.SYX, an optional image, sources.md) after its tune: MASTER, MIDI, ENABLES,
  SYSTEMX on; the dumps through MIDI IN at 31.25 kbaud (12.8 s of the firmware's time, under a
  second of CPU); SYSTEMX off; patch 00. All 100 names read back as the bank lists them. The state
  is cached apart (`factory-r2-patches.ram`), so the tune and the renders without the bank are
  unchanged. The firmware marks the edit buffer edited at every start (the dot after the patch
  number), as it did with the OBERHEIM default.

- 2026-11-13, accuracy: **DAC MONO no longer beeps** (it was a release blocker).
  - What the test does (voice 1.4 F19E, firmware.md): it steps VCO 1's S&H through codes
    10245-10260 and counts 64 OSC periods of each while the S&H is still selected. It beeps when the
    first count is below the last, or when a step goes backwards or jumps.
  - Why it beeped: the model charged an S&H only when the next CLEAR* let it go, so each count
    measured the previous step's code. Count 1 read the last sweep's 10260 and count 16 read 10259,
    so first - last was -5 counts and every sweep beeped: 30 beeps in 10 s. Sampling the mute every
    50 ms had caught every other one.
  - The fix: the selected S&H charges from its VOICEN* cycle on, exact over each stretch of bus
    cycles (model.md).
  - After: no beep in 33 sweeps (10 s), from blank memory and from a tuned unit. Steps measure 2-6
    counts (4 nominal, plus the 8253's one count at each end) against a threshold of 10.
  - What a real unit does: it beeps only when something moves a 4-count step by 5-6 counts, 0.6 cent
    over a 7.4 ms count. Mains hum, VCO jitter or drift can do that; so can a DAC that isn't
    monotonic, which is what the test is for. The model has none of these in this path.
  - Side effects: every S&H voltage now arrives up to one window earlier (the VCA's window is
    611 us), so notes start 0.3-0.8 ms earlier. On seq-48k notes moved -1.2 to +0.4 ms and peaks
    -0.1 to +0.2 dB. Aligned, note-48k matches the old render to -70 dB.
  - TUNE ALL 5/5. Its corrections move by one or two LSBs: pitch per note within 0.5 cent of before.
  - The rest of the check set: service checks 9/9 (`check.py` now counts beeps); plugin test,
    panel and host tests pass; SPICE suite as before.
  - `xpanderbench check build/xpander-golden-1`: idle -147 dB, note -24 dB, chord6 -16 dB, seq
    -25 dB. That is the onset shift.
  - CPU unchanged: chord6-48k 25.4 % (an A/B of the old and new core in the same build: within
    0.1 %).
