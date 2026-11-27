# Bass Station Rack: what we know is wrong, and the plan

This lists what the model gets wrong or hasn't checked, as found by the transcription
(`docs/circuit.md`), the firmware notes (`docs/firmware.md`) and the comparisons with the real
unit, and what is planned. "Real" means the unit in polynominal.com's recordings
(`private/bsrack/reference/polynominal-rack/`) unless it says otherwise.

## Known inaccuracies

### Sound

- **The voice's part values are mostly fitted.**
  - The only schematic is a hand trace of a 1995 keyboard board. It has the topology and almost no
    values (`docs/circuit.md`).
  - The filter sections take the OSCar's values where its sheet has the matching part [O], except
    R51/R63 and C33/C37, fitted for self-oscillation (round 2).
  - Everything else is fitted [F]: the rails, the DAC's full scale, the converters' dividers and
    references, the DCO capacitors, U18's gain, the mixer, the VCA and the output level.
  - Waits on data: a readable photo of a Rack (or keyboard) board, at least about 3 times the
    resolution of the one set online (`docs/sources.md`, Not downloaded). It would give the
    resistors only: the SMD capacitors (C33/C37, C30 and the rest) carry no marking.
- **The demo against its recording, after accuracy round 3** (`build/bsrack-demo-lemus.wav`,
  `tools/compare/demo.py`):
  - frame loudness correlation 0.97, median difference 0.7 dB;
  - centroid ratio 0.99;
  - 1/3-octave bands within ±2.9 dB from 40 Hz to 10 kHz, -5.0 dB at 12.9 kHz, -2.5 dB at 16 kHz.
  - In the dark passages (4-16 s) the recording carries a floor at 5-16 kHz 55-65 dB under the
    300 Hz-3 kHz band that we don't have: the MP3's noise or the unit's hiss, not told apart.
- **The 40 factory programs against zibbybone's video** (`tools/compare/fitpresets.py`, A/B set
  `private/bsrack/reference/scenes/ab-presets/`):
  - 1/3-octave band shares within 4.3 dB RMS on average (each band floored at -45 dB: the video's
    AAC drops what is further down); 20 Hz-2 kHz within 2-3 dB RMS across the programs.
  - Fitting the voice to them barely moves the old values (4.43 → 4.32): full key tracking confirmed,
    the top at 13-14 kHz, C33 indifferent (the demo wants 43p). The converter's floor (round 4) took
    them to 4.32 too, and the demo to 2.19.
  - The outliers don't answer to any voice value: SPIT's filter envelope there falls from 4 kHz to a
    1 kHz plateau over 150-300 ms where v1.5 sweeps to the bottom in 20 ms; SQUARE PORTA (12 dB,
    full RESONANCE) is 20-27 dB brighter at 1-6 kHz. v1.5 changed the 12 dB resonance law, so that
    unit likely runs other firmware.
  - RAINMAN (program 19): its LFO slams the cutoff open and shut at 41.7 Hz (6 firmware ticks) on
    the real unit too. Ours swung harder: the cutoff fell to about 13 Hz when shut. With a floor
    under the converter's current (`vcfFloor`, 6 uA: the cutoff never under about 45 Hz, round 4)
    its band error went 4.36 → 3.57 dB; it still has 3-8 dB more at 8-16 kHz. A bias left at full
    RESONANCE (`resFloor`, 30 uA) would take more off but costs TRANCE 2 and the demo (Declined).
  - Checked for glitches there: no DCO edge dropped, no failed solve left, the same at 192 kHz. The
    firmware's VCF sum and both clamps (6229-62D8, `bsrackseq` with BSRACK_PROBE) give exactly
    v1.5's values: at full depth the LFO pins the cutoff shut, then open, every 24 ms.
  - That unit swings it about half as far (brightness 25 dB a cycle against our 59), and its SPIT
    envelope stops halfway too: as if its firmware applied half the modulation depth. v1.5 changed
    this code (the resonance step), so that unit likely runs earlier firmware.
  - Waits on data: that unit's firmware version, or the factory programs from a v1.5 unit.
- **The VCF converter is anchored, not free** (round 2):
  - The top: code 000 rings at 13.0 kHz, from the resonant peak in fiolter-4poles (8.75-9.25 s).
    A free fit to the demo found 13.1 kHz on its own.
  - The tracking: the firmware's 24 dB key tracking (32 codes a semitone) taken as full tracking.
  - If the real tracking isn't full, the divider is off. Waits on data: a recording of
    self-oscillation played up the keyboard.
- **The VCA's leak.**
  - Closed, the real unit leaks the last note at about -76 dB under it (fiolter-4poles, filter-2poles
    and wavefo, start and end). The spectrum is flat over the first harmonics, like the square
    through a capacitance.
  - Our VCA closes to exact silence, so the voice can sleep. Not modelled (Declined, below).
  - fiolter-4poles also holds a steady tone at -53 dBFS at 3.2-3.6 s. It is a state we can't place
    (a held note at a low sustain?), not the closed leak.
- **Self-oscillation (rounds 2, 3).**
  - At full RESONANCE in 24 dB the filter self-oscillates up to about 250 Hz and builds up at about
    20 dB/s; above that it rings and dies away (Q ~100 at 1 kHz, ~15 at 10 kHz). In 12 dB the
    firmware keeps the resonance CV off zero, so it doesn't.
  - As traced, the loop with U28 off is damped above 200 Hz by the limiter diodes' leakage. R51
    (3.3k, C30's lag) makes the low end oscillate; C33 (43p, its lead) sets how the damping rises
    with frequency, fitted to the demo with the rest.
  - Seen in the recordings: fiolter-4poles at 70-170 Hz, fx-rain's tone at 43-172 Hz with pings at
    0.5-1.2 kHz that die away. The level is set by the diodes and isn't checked.
  - Waits on data: a clean recording of self-oscillation, played up the keyboard and swept to the
    top; and one of the factory program RAINMAN (19), to check its top.
- **The pulse width (round 2).**
  - From the clips: 59 % at MAN's left end (wavefo), about 55 % at code 800 and 20 % at E00, the
    same at notes 48, 60 and 72 (pulsewisth).
  - R32 (16.9k) is fitted to that. With the old 12k, the code-E00 pulse was 2-4 % and vanished
    above note 72.
- **The oscillators' levels** (saw against pulse, osc 1 against osc 2) aren't checked. The clips'
  mix settings are unknown. Waits on data: a recording with known settings.
- **The recording runs fast.** The reference demo runs 0.60 % fast in both tempo and pitch (+10.4
  cents) against the firmware at 8 MHz. Either that unit's crystal or the recording. Waits on data:
  a second recording of the demo.
- **The DCO reset** is taken as instantaneous. On the unit, the 4070's pulse (R29/C9, value unknown)
  holds the ramp empty for microseconds, which matters at the top of the range.
- **No hiss.**
  - The clips' floor with the VCA closed (about -135 dBFS/Hz) is the recording chain's.
  - Between harmonics during a note the MP3's own noise is 40-50 dB above it, so the unit's hiss
    can't be read from these recordings.
  - From the parts (the OTAs' and op-amps' noise) it should sit 70-100 dB under a full note.
  - Waits on data: a lossless recording.

### Behaviour and features

- **Not emulated:**
  - the rear CV/GATE IN and OUT (the firmware reads and writes them; the plugin has no jacks for
    them);
  - MIDI THRU (the jack copies MIDI IN). The plugin's MIDI output is the MIDI OUT jack: what the
    firmware sends, as a knob's controller (UX round 1);
  - the headphone amplifier.
- **The 0.6 % speed** of the reference recording is not modelled. The model runs at 8.000 MHz
  (`docs/firmware.md`).
- **The keyboard's firmware** isn't available, so there is no keyboard model.
- **v1.5's pot-scan lock-up, faithful (decided 2026-11-25).**
  - The idle-pot search at 6A10 has no exit when all 17 pots are "active" at once. That happens when
    every knob is turned within a few seconds, or when a host moves every parameter at once (a preset
    or automation).
  - The firmware then spins at 6A17 and ignores notes, keys and AUDITION. The unit stays so until
    power-off; the plugin stays so until the State pop-up's Reset. A session saved while it hangs
    reopens hanging, as the unit was.
  - Lemus's patch for it stays in the board for the tools (`bsrackseq --potstorm --patched`). The
    machine doesn't use it (`docs/firmware.md`, "A lock-up in v1.5").

### UX

- **The rear** (REAR in the bar) is measured from a 1032×104 image (2.339 px/mm, about ±0.5 mm). Its
  FILTER EXTERNAL AUDIO INPUT is the plugin's side-chain, as the Minimoog's INPUT EXT. SIGNAL: a cable
  shows in it while the host routes audio there. CV/GATE IN and OUT and MIDI THRU are drawn, not
  emulated (their tooltips say so).
- **The 7-segment digit shapes are a guess.** The digits can't be seen behind the red filter in the
  photo.
- **Soft takeover is the unit's own behaviour.** A pot acts only once moved: "when you turn a rotary
  control more than a few degrees ... expect the sound to 'ping' to the new position" (owner's
  manual, p. 4).
- **The brand marks** (maker, name) are drawn from the front photo with macOS's fonts standing in
  for the unit's (private artwork, UX round 1).

### Reference data

| Unit | Where | What it has | Known settings |
|---|---|---|---|
| Rack (polynominal) | `private/bsrack/reference/polynominal-rack/` | The built-in demo (44.8 s); filter 2/4-pole, ADSR, LFO, waveform, pulse-width, detune, portamento, xmod clips; 16 patch clips | The demo: everything (ROM). The rest: unknown. |
| Keyboard (polynominal) | `private/bsrack/reference/polynominal-keyboard/` | 3 demos | Unknown; a different unit and firmware |
| Rack (zibbybone, YouTube cyiyopNwVAA) | `private/bsrack/reference/youtube/` | Factory programs 00-39 stepped with + under one MIDI pattern (Roland Fantom X6), 4-5 s each; then FILTER INPUT | The programs; the pattern transcribed (`zibbybone.json`); its firmware unknown |

## Budget

- **CPU:** `bsrackbench`'s `bass-48k` under 7.5 % of a core at 48 kHz. Last measured: 4.09 %, worst
  block 11.6 % (2026-11-25, after accuracy round 4).
  - Demo (44.1 kHz) 3.8-4.0 %.
  - The same build measures 5.0-5.1 % for bass-48k when the machine is busier. Compare runs back to
    back.
  - **The demo's worst block** is 29-31 % of a 64-sample block since accuracy round 3 (85-90 %
    before): filter section 2 no longer limits hard at the top of its range.
  - The firmware is about 1 % of a core; the voice runs the two filter sections on the solver at
    twice the host rate.
- **Check set baseline:** 2026-11-25, golden renders `build/bsrack-golden-5` (after accuracy round 4;
  `golden-4` after round 3).
  - SPICE suite 4/4 (`vcf1_limiter_ringing` 1.46 % at its peak, 0.44 % RMS).
  - corners 24/24, no unconverged solve.
  - host tests pass; plugintest passes; auval passes.
  - No service manual, so no service checks.

## Plan, in order

Accuracy is used up with the data we have (round 2), the A/B is done (2026-11-25), CPU round 1
found no bit-exact trim left, UX round 1 holds. Left:

Nothing planned: what's left waits on data or a release.

Waits on data:
- a readable board photo: every [F] value;
- a second recording of the demo: the 0.6 % speed;
- a lossless recording: the hiss, the leak's level;
- self-oscillation played up the keyboard: its level and the VCF tracking;
- clips with known mix settings: the oscillators' levels.

## Tried and dropped

- 2026-11-25: R50 = 10k, R51 = 330 in the filter sections, read as the OSCar's input attenuator.
  It gave a 120 Hz highpass: below 1/(2π·330 Ω·2.2 µF) the input and the feedback reach node A
  differently. Swapped to R50 = 330, R51 = 10k, which is the OSCar's R47 10K at the resonance OTA's
  output node. That gives a flat passband (0.91 at 20 Hz, 1.0 at 100 Hz).
- 2026-11-25: R52 = 270k (the OSCar's R52). It belongs to the OSCar's inverting stage, which the
  Bass Station dropped. With it the resonance path is far too weak to damp the loop. Fitted 10k.

- 2026-11-25: the VCF divider and reference fitted freely to the demo, bands to 16 kHz. The
  search stopped at score 3.43 (top 13.1 kHz, tracking 116 %); with both anchors it reached 2.94.
- 2026-11-25: self-oscillation from R51 alone (4.7k, 3.3k): it oscillates only below about 600 Hz.
  From C33 alone (open): only above about 2 kHz. Together (3.3k, 2.2p): everywhere.

- 2026-11-25 (CPU round 1): a fixed-size Newton and LU for 17 ports (the filter sections). About 1 %
  faster, and not bit-exact (-156 dB).
- 2026-11-25 (CPU round 1): shorter stretches of quarters after a failed full step (8, 2 samples
  instead of 32). More steps fail, so no faster. The self-oscillation's phase moves (-31 dB).

- 2026-11-25 (accuracy round 3): C33 2.2p (round 2) and 10p: the top self-oscillates or rings with
  a Q of hundreds; RAINMAN screamed at 12 kHz. 100p: clean, but the demo's 10-13 kHz bands 6-11 dB
  low. A small U28 bias left at full RESONANCE instead: it damps every frequency alike, so the low
  end stops oscillating before the top calms down.

## Declined

- 2026-11-25: a resonance bias left at full RESONANCE (`resFloor` 30 uA) to bring RAINMAN's top
  closer to zibbybone's unit. Decided against: that unit likely runs other firmware, and the v1.5
  demo prefers none.

- 2026-11-25: modelling the closed VCA's leak (-76 dB under the last note). Decided against: it is
  below hearing, and it would keep the voice running whenever the plugin runs (about 1.5 % of a
  core while idle).

## Done

- 2026-11-27, 0.1.0 beta released (tag no-mono-bsr-v0.1.0):
  - A standalone app, like the other instruments.
  - Windows (clang-cl, the public tag build): the netlist names now go to netlists.cmake with
    commas (Visual Studio's custom build step split the escaped semicolons), and no M_PI (MSVC
    lacks it). Bit-exact against golden-5.
  - Checks: plugintest, host tests (bsrack, instrument), auval arm64 and x86_64 on the release
    build, the host test on Windows.

- 2026-11-25, accuracy round 4 (RAINMAN still didn't sound right):
  - New reference: zibbybone's video of the 40 factory programs (the pattern transcribed, the
    program changes read off the display; `tools/compare/fitpresets.py`, A/B set ab-presets).
  - Checked RAINMAN for glitches: none (edges, solver, rate). Its 41.7 Hz LFO matches the real
    unit's to the tick.
  - Change: `vcfFloor` 6 uA under the VCF converter's current: the cutoff never under about 45 Hz.
  - Factory programs: band error 4.43 → 4.32 dB; RAINMAN 4.36 → 3.57. Demo: 2.25 → 2.19 (loudness
    correlation 0.97, centroid 1.01).
  - CPU unchanged (4.09 %); corners 24/24, plugintest, host tests, auval pass.

- 2026-11-25, accuracy round 3 (program 19 RAINMAN sounded broken):
  - Found: at full RESONANCE in 24 dB the filter self-oscillated up to the top; RAINMAN's LFO pins
    the cutoff there, and it screamed at 12.4 kHz (half the patch's energy). Not the solver: the
    same at 192 kHz.
  - Change: C33/C37 2.2p → 43p, a voice part (`c33`) fitted with the rest to the demo; refit:
    beta 1020 → 952, R34 216 → 207, R35 27k → 44.1k, R74 697 → 2.69k, VCA reference 6.1M → 7.15M,
    R79 3.3M → 1.47M, CV settle 0.96 → 0.40 ms.
  - Demo: loudness correlation 0.89 → 0.97, median 1.0 → 0.7 dB, bands 40 Hz-16 kHz 2.0 → 1.3 dB
    RMS, brightness error 0.17 → 0.12 octaves.
  - RAINMAN: the 8-16 kHz share -5 → about -9 dB of its energy.
  - CPU: bass-48k 4.70 → 4.07 %; the demo's worst block 85-90 → 30 %.
  - SPICE 4/4, corners 24/24, plugintest, host tests, auval pass. `bsrackrender`'s trace takes an
    interval (BSRACK_RENDER_TRACE=<ms>).

- 2026-11-25, UX round 1 (the CONTRIBUTING.md list walked against the shipped machines):
  - A session reopens the whole unit as it was (`Machine::snapshot`: the CPU with its RAM and
    timers, the board, the EEPROM). It used to boot from the EEPROM, losing a knob's edit not
    written to a program and the mode.
  - MIDI OUT: the firmware's bytes leave the plugin as messages at the sample they were sent in (a
    knob: its controller, FREQUENCY 105).
  - The window: 2.6 px/mm as the QuadraVerb GT, 1255×154 at scale 1 (was 1931×216).
  - The brand marks drawn and installed (they were missing).
  - FILTER INPUT is the side-chain itself, as on the Minimoog: the bar's FILTER IN switch is gone.
  - The rear view (REAR): the plate measured from polynominal's rear image, the jacks with tooltips,
    the rear logos as artwork places, a cable in EXTERNAL AUDIO INPUT while the side-chain is routed.
  - A session saved without the ROM boots once the ROM is there (it restored a unit that never ran).
  - The audio thread no longer copies the EEPROM every block (an allocation). `processBlock`
    takes its own lock and honours suspend, as the shell needs.
  - Held: controls (drag, Shift, double-click, wheel, tooltip), the bar and keys from
    `host/PanelFrame.h`, Space to the host, keys by place, parameter names and values, the ROM
    prompt.
  - Bit-exact against `golden-3`; corners 24/24; plugintest (new: warm session, MIDI OUT), host
    tests, auval pass.

- 2026-11-25, CPU round 1:
  - Found the demo's worst block (above). Nothing faster at bit-exact.
  - `bsrackbench`: says when the worst block falls; `golden` makes its folder. It wrote nothing
    before when the folder was missing, so round 2's renders were made again from its commit
    (`build/bsrack-golden-3`).
  - All five workloads exact against `golden-3`.

- 2026-11-25, accuracy round 2:
  - Changes:
    - the VCF converter anchored (13.0 kHz top, full key tracking): divider 3.9k → 2.97k,
      reference 1.2M → 588k;
    - self-oscillation in 24 dB: R51/R63 10k → 3.3k, C33/C37 10p → 2.2p;
    - the pulse width: R32 12k → 16.9k;
    - refitted to the demo with bands to 16 kHz (`tools/compare/fitdemo.py --anchor full`): beta
      280 → 1020, R34 180 → 216, R74 1.3k → 697, VCA reference 18M → 6.1M, CV settle 1.3 → 0.96 ms.
  - Demo against the recording:
    - loudness correlation 0.87 → 0.89, median difference 1.2 → 1.0 dB;
    - bands 40 Hz-16 kHz 5.1 → 2.0 dB RMS (12.9 kHz -22.8 → -1.6 dB, 8 kHz +9.0 → +5.2 dB);
    - per-window brightness error 0.11 → 0.17 octaves.
  - SPICE `vcf1_limiter_ringing` 1.9 → 1.46 %.
  - CPU: bass-48k 4.53 → 4.70 %.
  - Unchanged: corners 24/24, host tests, plugintest, auval.
  - Found, not changed: hiss, the leak, the oscillators' levels (waits on data, above).

- 2026-11-25, accuracy round 1:
  - Change: the voice's fitted values, fitted together to the built-in demo's recording
    (`tools/compare/fitdemo.py`):
    - VCF reference 600k → 1.2M;
    - the FMA1's internal beta 120 → 280;
    - mixer input R34 330 → 180, bias R35 22k → 27k;
    - VCA input R74 330 → 1.3k, converter R41 3.3k → 2.7k, reference 10M → 18M, bleed R79 1M → 3.3M;
    - the CV settle 1.0 → 1.3 ms.
  - Demo against the recording: loudness correlation 0.57 → 0.87, median difference 2.2 → 1.2 dB,
    per-window brightness error 0.31 → 0.12 octaves RMS, band RMS 3.0 → 1.6 dB.
  - CPU: bass-48k 6.66 → 4.53 %.
  - Unchanged: corners 24/24, SPICE 4/4.

- 2026-11-25: build phases 1-5 to first playable. Commits: 1502ad4 research, 2f86f1e skeleton,
  4210442 firmware, then the voice and panel.
