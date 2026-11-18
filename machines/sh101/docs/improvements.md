# SH-101: what we know is wrong, and the plan

What the model gets wrong or hasn't checked, as the transcriptions (`docs/circuit-osc.md`,
`docs/circuit-filter.md`), the SPICE checks and the comparisons with real units found it
(`docs/model.md`, `docs/model-osc.md`, `docs/model-filter.md`, `docs/spread.md`,
`docs/online-references.md`, `docs/roland-measurements.md`), and what is planned. "Real" is the
Friendly Noise unit (the one the Real unit calibration is fitted to, settings read at 1080p) unless
it says otherwise. "Roland" is Roland's SH-101 plug-in v1.2.0: a cross-check, not a real unit.

## Known inaccuracies

### Sound

- **ENV to cutoff.** As transcribed, ENV 10 can't sweep less than 7.7 octaves while KYBD tracks
  1 oct/oct: both reach the cutoff through IC18b, so R189, R193 and the ENV's 5.11 V swing fix
  their ratio (8.1 octaves and 5.42 V before the ENV GEN went onto the +14 V rail, 2026-11-13).
  The real unit sweeps 7.0-7.3 octaves (7.24 re-measured, `spread.md`) while tracking 1.024 ±
  0.021 oct/oct; loopop's unit (VCA GATE) needs a weight of 0.80-0.81 of the schematic's (0.805
  and 0.807 by `units.py`), and Failed Muso's ping matches 0.82 at ENV 5.0 (by ear ½); Roland
  sweeps 7.65. `envCvScale` (0.82 Real unit, 0.89 Roland; 1.0 is the schematic) stands in. The
  +14 V rail's value matters here: U-Labor's unit reads 14.3 V on TR3's emitter (the model's),
  a ModWiggler member's 14.81 V on R158's top, which would put the weight back near 0.78; that ENV peak, 5.26 V
  above the bus, is 0.541 of the way to that rail, as the divider says (ours 5.11 V on 14.3 V).
  Of 20 000 simulated units (before the rail), 90 % sweep 8.8-12.0 octaves and the lowest 7.4. Ruled out (`model.md`, "Calibrations"): the
  measurement (the release fit returns our own renders' sweeps), the tracking (the schematic needs
  0.92 or less; the real 1.024 reads 0.96-1.02 true), the chip's law (a Juno-106's IR3109 is
  exponential within 3 cents from 20 Hz to 10 kHz), parts within tolerance (two units agree), the
  VCA switch (one unit in GATE, one in ENV), the parts on the path (R189 100k, R192 33k, R193 150k
  by a photo's bands and two BOMs; 0.77 would need R189 at 130k or R193 at 115k), VR19 fed from
  S6's side of R197 (0.73, but the sweep would then follow the VCA switch by 1.7 octaves), and
  (2026-10-26) a part the drawing misses: the board diagrams on both sides, a board photo and the
  two bulletins show none. What is left is a cause common to every unit, in the ENV-to-cutoff path
  or the expo's scale. Waits on data.
- **The real unit's RES 0 take.** Its ENV plateau's spectrum fits four poles at 0.8-1.5 kHz,
  1.7 octaves under the 4.8 kHz the RES 10 take self-oscillates at (`online-references.md` §1.3):
  unexplained, not fitted. The same take's sweep, tracked spectrally, gives the 0.77 above
  (`model.md`).
- **FREQ's bottom: a slider reading, or unit spread.** The Friendly Noise unit's sub take (FREQ
  1.5 ± 0.3, KYBD 10, lowest note) puts the cutoff at 21.4 Hz at KCV 0; ours is 11.5 / 14.7 /
  18.7 Hz at FREQ 1.2 / 1.5 / 1.8 and matches at 1.97, 0.17 division past the reading (`units.py`;
  1.79 at 20.8 mV/oct, before 2026-11-16: the law is shallower now, with FREQ 0 kept in place). loopop's unit goes the other way: its rest sits 0.54 and 0.66 octave under ours at
  FREQ 4.7 and 3.9 (KYBD 0). Once Upon a Synth's unit sits above: read at FREQ 2.6 with ENV 5.5
  held (S 10, KYBD 0), its harmonics fit ours at FREQ 3.8 within 2 dB, about 1.2 octaves brighter
  (a held ENV can't tell FREQ from ENV depth; A/B scene 12, 2026-11-15). The service notes trim
  VCF WIDTH, not the absolute cutoff, so units
  differ there; the model's FREQ law (the linear pot loaded by R186, 1.1 to 1.7 octaves per
  division) is kept. Waits on data: a unit's cutoff at several legible FREQ positions.
- **FREQ's law: one unit 13-14 % steeper per division.** AutomaticGainsay's unit, self-oscillating
  while FREQ is swept by hand (13 readings from 1.5 to 8.5, read per frame at 720p; `units.py`
  `freqlaw`): 1.277 octaves per division over 1.5-8.5 against our 1.098 (1.206 at 20.8 mV/oct,
  before 2026-11-16), yet the same unit's top is 15.2 kHz: from 8.5 to 10 it rises only about one
  octave per division. So its law is steep in the middle and flattens at the top, which no single
  slope gives. The same unit's RES law reads stretched about as much, so part of it is the reading.
  A compression at the top that would follow it (a knee in the expo) was tried and dropped
  ('Tried and dropped', 2026-11-16). Waits on data.
- **The filter's top: two units, and the cause.** FREQ 10 at KYBD 0 is fitted (2026-11-16, Done)
  to two real units: Starsky Carr's self-oscillates at 13.93 kHz (FREQ 10, RES 10, ENV, MOD and
  KYBD 0, all legible at 1080p) and AutomaticGainsay's puts a RES 7.1 saw's peak at 15.2 kHz
  (three sweeps end there, the slider at 9.8-10.0; the same unit's open saw at FREQ 10 is
  −7.4 dB at 9-11 kHz). Ours: 14.7 and 14.2 kHz (`units.py` `freqlaw`, "top"). Roland's
  plug-in tops out at 15.6 kHz. What sets it is not identified: the expo's slope against the
  pin-9 divider stands in (22.8 mV/oct where one chip measured 20.8), because with VR8 trimming
  KYBD to 1 oct/oct that ratio alone sets FREQ's span. The Juno-106's chip law and the rails are
  kept. Perfect Circuit's bright saw at FREQ 10 had ENV 5.5 with S 10 (read 2026-11-16), and
  gstormelectro's take shows neither FREQ nor ENV, so neither says where FREQ 10 alone sits.
  Waits on data: more units self-oscillating at the top with the sliders legible.
- **LFO's top rate: unit spread.** RATE 10 is 36.4 Hz; Perfect Circuit's unit runs 38.2 Hz, 4 %
  faster, at the 65th percentile of the parts' spread with R31 and R35 at 100k (33.2-41.5 Hz,
  `spread.md`). R31 at 100k is confirmed by a real unit's sound (2026-11-13, `circuit-osc.md` 5.3).
  IC3 and R35 are on +15 V by both scans (re-read 2026-11-13); on +14 V RATE 10 would be 34.9 Hz.
- **Mixer levels: within the parts' spread.** Re the saw at 10, ours: square +1.8, sub 1 OCT and
  2 OCT −0.6, 25 % sub −1.7 dB (R92 and the 4013 on the +14 V rail, 14.3 V, since 2026-11-13; it
  was +2.0 / −0.25 / −0.3 / −1.4). Per note (`units.py`, three units, 24 events): the square is
  0.29-0.69 dB over Perfect Circuit's and gstormelectro's, the subs −0.29 to +0.48 dB against
  every unit's (one AutomaticGainsay note 0.87 under), inside the parts' spread (+1.2 to +2.7 /
  −1.0 to +0.7 dB).
- **Noise: the units spread 2.7 dB.** Not on the schematic. Level (`noiseRms` 0.98 V in the Real
  unit calibration, 1.05 service notes, 1.30 Roland): re the saw at 10, ours +0.44 dB, three units
  +0.68, +0.26 and −1.98 (`units.py`). Colour: C31's corner at 1150 Hz (900 before 2026-11-13).
  With each unit's own saw at FREQ 10 taken out of its noise clip, which removes its filter's top
  and its recording chain, the source's slope over 200 Hz-8 kHz is −6.32, −6.49 and
  −6.92 dB/octave and its centroid 1152, 1144 and 1044 Hz; ours −6.40 and 1157 Hz. Failed Muso's
  two clips have no saw, so their chain and top stay in: −7.9 dB/octave against our −6.7. The
  selected 2SC945's junction noise isn't modelled.
- **Resonance.** On AutomaticGainsay's unit (D3, RES 2.3 / 4.7 / 7.1 ± 0.2, the cutoff swept by
  hand) the peak over the fundamental, frame by frame at the same peak frequency (`units.py`), is
  +2.2 / +9.5 / +22.7 dB, ours +2.9 / +9.7 / +19.7: ours matches at RES 2.02 / 4.62 / 7.54. The
  top one is 0.44 division past the reading. That unit's FREQ law reads stretched by about as much
  ('FREQ's law'), so most of it is likely the same cause, not an earlier onset (the spread:
  0.715-0.855 of travel, ours 0.804). On loopop's unit the peak moves 530 → 860 Hz as RES rises
  (+2 → +18 dB), ours 530 → 851 Hz. loopop's two resonant decays (the A/B scenes, 2026-11-15)
  match ours, peak height through the sweep within 1 dB, at RES 7.6 / 7.5 against the readings
  6.5 / 6.9 (FREQ 4.0 / 3.6 against 4.7 / 3.9, D 3.4 / 5.0 against 3.2 / 4.8): that unit too is
  more resonant than ours at the read RES, by more than the reading's ±0.2. One unit each; no
  clip shows where self-oscillation starts (ours 0.804, the transistor-level chip in SPICE 0.801,
  Roland 0.82).
- **IR3109.** No maker's data: modelled from Open Music Labs' decode and the AS3109 data sheet,
  its law end to end from one Juno-106 voice (`model-filter.md` 2). The OTAs are matched with no
  offsets (real stages up to ±3 mV each); the P-MOS buffers' K and V_TO, and so their gain
  (0.89-0.96), have no maker's data; the compression above 10 kHz is taken entirely on the expo's
  current. From the decode's devices the model has the OTAs' output resistance, their outputs'
  soft stop near the rails, the mirrors' current gain (+2.2 % at 1 µA, falling 0.14 % per e-fold),
  the CAP pins' own 2.65 pF, and the buffers' channel-length modulation and stage 2/4 current
  sources (since 2026-11-14). Against the transistor-level chip, self-oscillation is +0.3 cents
  at 194 Hz, +5 at 970 Hz and +22 at 4.8 kHz, the AC response within 0.07 dB at 1 µA and 0.75 dB
  at 100 µA. What grows with frequency is not excess phase (the decode's OTAs add under 6 ns, its
  pins' junctions 0.5 cents at 4.8 kHz, the discrete resonance parts about 4 cents): about 14
  cents is the decode's input pair losing transconductance with current, by 1/(1 + I_ABC /
  6.95 mA), which a real chip's law doesn't show (with it, the Juno-106's fit from 20 Hz to 28 kHz
  goes from 3.0 to 7.0 cents RMS), so a real chip's expo must rise as much, as Open Music Labs
  measured its own doing. The model keeps the chain. The Real unit's expo current, 3.30 µA at
  22.8 mV/oct, is fitted so FREQ 0 sits where the real unit's does; FREQ 10 is then 14.7 kHz
  self-oscillating at KYBD 0, where two real units sit ('The filter's top'; 28 kHz at 20.8 mV/oct
  and 5.0 µA before 2026-11-16; the spec says 20).
- **Against the transcribed schematic** (`model.md`, "The whole voice", 43 cases): with the filter
  open the sources' level is within 0.11 dB of the joined circuit in ngspice, and their harmonics
  1-12 within 0.33 dB RMS (the full mix at 10 0.33 dB since the buffers' outputs at the rails
  match the decode's, 2026-11-14; each source alone within 0.33 dB). I_ABC within 4 cents,
  self-oscillation within 9 cents, the ENV to the VCA within 0.05 dB. With FREQ 3 and RES 0.95,
  self-oscillating at 23.6 Hz, its first three harmonics within 0.2 dB and the waveform within
  −21 dB (the harmonics column reads the note's own lines there, 120-150 dB below the
  oscillation).
- **VCA.** The BA662A is an ideal tanh with the TB-303's measured losses; real chips spread ±15 %
  in g_m (Roland sorted them by colour). Its fade is within 0.3 dB of the real release from 4.5 to
  9.5 s (`model.md`, "The VCA's turn-on").
- **Jack board A's clamp.** As drawn, TR7's base-collector junction conducts once the output
  swings below about −1 V: a 1 V peak sine loses 0.6 dB on its negative half, a 2 V one 2.4 dB,
  and a full saw at VOLUME 10 reaches −1.04 V. ngspice with standard 2SC1815 and 1S2473 models
  agrees within 0.05 dB; a real unit's diodes at microamps may start it later. Kept as drawn
  (decided 2026-11-15); waits on data.
- **Switching off and on.** Off: +9 V is taken to fall with τ 12 ms (an estimate from the 1 W
  rating and the rail's capacitors), and the thump follows the estimate: −30 dBFS for a 5 ms fall,
  −41 for 12, under −45 for 25. On: a note held about 30 dB down for 0.25 s, open after about
  1.4 s, from the circuit and ngspice. Neither is compared with a recording.
- **Transcription points not settled** (`circuit-osc.md` §8, `circuit-filter.md` §8,
  `model-filter.md` 8):
  - S3's (portamento) contact order, deduced from what each position must do;
  - which slider feeds which of connector pins 65-68 (Electric Druid's mapping);
  - whether the 4013's second flip-flop is clocked by Q1 or Q̄1 (inaudible; its supply is +14 V,
    settled 2026-11-13);
  - the grip vibrato's polarity against VCO MOD (IC2a's collectors);
  - the TRIG circuit's input (IC4b) and IC5a's feedback: its timing, about 0.54 ms per semitone of
    KCV change, follows the parts that were traced;
  - the LFO line's polarity at S7 deck 2 (the attack taken in the square's high half);
  - TUNE: ±64 cents against the spec's ±50 (every simulated unit ±59-68; R90 at 6.0M would give
    ±50), clockwise taken as sharp.
- **Not modelled:** the S/Hs' acquisition (3.3 µs) and droop, and the D/A's per-unit trims (an
  ideal 1/12 V per count); the CEM3340's triangle asymmetry, mid-ramp step and PWM-dependent pitch
  (later AS3340s show it, vintage CEM3340s reportedly don't); TR24's and the 4013's switching
  delays (µs); IC3b's slew within an LFO edge; temperature drift, part tempcos and TH1's tracking.

- **Self-oscillation above the audio band aliases.** With KYBD pushing the cutoff to its top
  (FREQ 10, RES 10, KYBD 10, C4, no sources) the filter oscillates at 36.1 kHz, ultrasonic as on
  the real units, which are silent there. At the 4x internal rate (176.4 / 192 kHz) its harmonics
  fold back into the audio band: 3449 Hz at -61.6 dBFS (and its harmonics) at 44.1 kHz, 11380 Hz
  at -59.2 dBFS at 48 kHz; the lines are already in a 192 kHz render, so they're made in the
  solve, not in the decimation (2026-11-17, Real unit calibration at 22.8 mV/oct). With KYBD 0,
  FREQ 10 oscillates at 14.7 kHz, in band, as the real units do. The VCF alone, self-oscillating
  at the top: at 4x the strongest in-band line is 42-46 dB under the oscillation; at 8x still
  40-46 dB (the line moves, it doesn't drop); at 16x 82-88 dB. The VCF's own cost there is about
  14 % of a core at 4x, 20 % at 8x, 26 % at 16x (fewer Newton iterations at the finer step), so
  16x near the top would add about 12 % of a core per voice in that corner.

### Behaviour and features

- **Not emulated:** CV/GATE in and out; the EXT CLK jack (SYNC stands in); PHONES; the HOLD jack's
  normalling and which pedal reads as held (CC 64 stands in; `firmware.md`, open question 1); the
  battery (the RAM always keeps the sequence); the MGS-1 grip's own controls beyond the lever's MOD
  push.
- **Voice mode** (no CPU ROM): no sequencer or arpeggio; the panel's keys and buttons are inert,
  as on the 303 (decided 2026-11-15); the MODULATOR's RANDOM wave holds
  still, since the firmware steps it.
- **CPU:** `sh101bench`'s `bright` costs about 6.1 % of a core at 48 kHz (Budget).

### Reference data

No real unit of our own and no paid sets. No public set of real-unit recordings at documented
settings exists for the SH-101 (`online-references.md`). The clips are
`private/sh101/reference/shootout/wav/roland_sh-101_real__<channel>_*.wav`: lossy YouTube audio,
sliders read off video.

| Unit | Where | What it has | Known settings |
|---|---|---|---|
| Friendly Noise (YouTube eStzYAZoOAI; the Real unit calibration's) | shootout `friendly-noise_*`; `private/sh101/reference/online/friendly-noise_eStzYAZoOAI` | the saw with the ENV sweeping at RES 10 and RES 0; the sub at RES 10 over 43 notes | 1080p: FREQ 0, RES 10, ENV 10, KYBD 10, A/D/S/R 10, VCA ENV, GATE+TRIG, RANGE 16', TRANSPOSE L, top key (KCV 3.0 V); the sub take FREQ 1.5 ± 0.3 |
| loopop (mint, unmodified; YouTube 3JBFoXgtHvE) | shootout `loopop_*` | ENV decays with resonance (two takes), RES raised on C2 | 1080p: ENV 10, KYBD 0, A 0, S 0, VCA GATE; FREQ 4.7 / RES 6.5 / D 3.2 and FREQ 3.9 / RES 6.9 / D 4.8 |
| AutomaticGainsay | shootout `automaticgainsay_*` | RES series on D3, sources alone, portamento at 3 and at 10, bender, arpeggio clock, sub modes, trigger modes | RES 2.3 / 4.7 / 7.1 ± 0.2 (720p); the rest approximate |
| Perfect Circuit (YouTube biuWnvJ0f7I) | shootout `perfect-circuit_*` | each source alone on F2; a RATE sweep (top 38.2 Hz) | 1080p |
| gstormelectro (YouTube SsHcY4YQOfw) | shootout `gstormelectro_*` | sources alone; PWM by the LFO, 25.7-48.2 % | stills; sliders taken as 10; PWM depth unknown |
| tonelab (YouTube zR3U7za05dI) | shootout `tonelab_*` | self-oscillation played at KYBD 10 (about 1.00 oct/oct over 2.2 octaves); GATE mode's legato | RES 10, KYBD 10 from frames |
| Failed Muso's loaner (YouTube 6NqaegXGbtE) | shootout `failed-muso_*` | noise at full and half; a ping near 700 Hz | by ear: FREQ ½, RES 10, ENV ½ |
| Once Upon a Synth (YouTube PFXO4ZRsVC4) | shootout `once-upon-a-synth_*` | portamento near maximum, τ 0.78 s both ways | PORTAMENTO ON near 10 |
| Italo Brutalo (YouTube Cr4Le00_MuA) | shootout `italo-brutalo_*` | a saw alone on D2: 1/n | saw 10, filter open |
| The rest: 310 clips of real units (281 confirmed from the video), 15 lossless | `private/sh101/reference/shootout/index.md` | performances, sweeps | unknown |
| A ModWiggler member's unit (2016) | `private/sh101/reference/online/modwiggler-166240-goob` | meter: ENV 10.29 V, bus 5.03 V, +15 V 14.8 V (peak 5.26 V above the bus) | gate on, S 10 |
| Sound On Sound's unit (2001; `research.md` S28) | text | maximum attack about 3 s | A 10 |
| Real boards: Din Sync's RE-101 BOM (a dismantled unit), Driessen's control board, five synth boards, the service notes' board diagram | `private/sh101/docs/papers/opensource/dinsync_re101/`, `private/sh101/docs/photos/` | part values (`online-references.md` §5) | - |
| A Juno-106 voice's IR3109 (KR-106's data; OML's bench on one chip) | `private/sh101/docs/papers/opensource/kr106_ultramaster` (data only; its GPL code isn't used) | self-oscillation at 93 DAC codes, 5.7 Hz-52 kHz | - |
| Roland's plug-in v1.2.0 (not a unit) | `private/sh101/reference/roland-vst/renders` (263), `measurements.json` | the 205-spec A/B suite and the probes | its own parameters (`roland-measurements.md`, "Rendering") |
| The transcribed schematic in ngspice | `tools/spice/{osc,filter,jack,voice}` | every block; the whole voice (43 cases) | service-notes values |

Twelve of the clips are A/B scenes against Lemus at the settings read off each video
(`private/sh101/reference/scenes/shootout-real`, `make_scenes.py`; Friendly Noise, loopop, Failed
Muso, Perfect Circuit, AutomaticGainsay, Once Upon a Synth).

## Budget

- **CPU:** `sh101bench`'s `bright` (pulse, saw and sub at 10, noise 0.3, FREQ 0.8, RES 0.7, ENV
  0.6, S 0.8, the MIDI line) under 6.7 % of a core at 48 kHz on an M5 Max (set 2026-11-15: about
  10 % over the CPU round's 6.1 %; 7.5 % had been proposed before it). Last measured 2026-11-14, after the CPU
  round: `bright` 6.27 % at 48 kHz (276 Mcyc/s, 1109 Mins/s, worst block 7.5 %) with other work
  loading the machine (load average 6-9), about 6.1 % quiet by its cycles; `midi` 4.18 %,
  `longrun-resmax` 8.11 %, `automation` 6.57 % (worst block 18.0 %), `idle` 1.20 %; every
  workload 12 384 M instructions/s. Before it, after the IR3109's buffers and mirrors: `bright`
  7.54 % loaded (346 Mcyc/s, 1292 Mins/s), about 7.44 % quiet, 14 713 M instructions/s. Before
  (M5 Max, the rendering thread's CPU time on the performance cores, best of 3): `bright` 7.26 %
  at 48 kHz (329 Mcyc/s, 1260 Mins/s, worst block 9.0 %), `midi` 4.99 %, `longrun-resmax` 9.34 %,
  `automation` 7.42 % (worst block 19.2 %: parameter changes), `idle` 1.38 % (73 % asleep);
  every workload at both rates 79.5 %, 14 354 M instructions/s. 2026-11-13: `bright` 6.72 %, 1106
  Mins/s, 12 624 M instructions/s; the 2026-11-14 accuracy round's IR3109 saturation costs about
  +12 %. The 2026-10-23 figures (11.2 % for `bright` at 44.1 kHz) were on an M1 Max by the wall
  clock.
- **Check set baseline:** 2026-11-16, the filter's-top round. Golden renders `build/sh101-golden-9`
  (the Real unit calibration at 22.8 mV/oct; golden-8 had 20.8); service checks 34/34 in each
  calibration (`tools/service/check.py`); the SPICE suite 28 of 28 within its limits
  (`tools/spice/suite.py`); `units.py` 107 rows (63 ok, 0 over, 7 known, 37 info); against
  Roland's renders 5.55 / 4.38 dB (Real unit; 6.1 / 5.4 before), 5.5 / 4.0 (Roland), 8.7 / 8.6
  (service); `sh101plugintest` 0 failures; the host tests (`lemustest_instrument`) pass;
  `auval -v aumu R101 Lmus` passes; `sh101corners` 0 of 299 cases failing at each rate.
- **Check set:** `sh101corners` (the controls' extremes: unconverged solves, noise, bounds), `sh101plugintest` (MIDI, sync, sequencer and arpeggio, pitch at every rate, state,
  120 s stability in every calibration, random-settings stress, sleep and wake, POWER on and off,
  voice mode; `--panel OUT.png [SCALE] [off|voice|keys|held]` draws the panel); `tests/host.cpp`
  (slots, bit-identical to the machine alone, sessions, artwork places and the older pack);
  `sh101bench golden|check|time|idle` (seven workloads at 44.1 and 48 kHz: CPU %, Mcyc/s, Mins/s,
  worst block, share asleep, the largest difference in dB); `tools/spice/suite.py` (every netlist
  against ngspice, 28 checks with limits; the scripts take the build from `SH101_BUILD` or
  `build/sh101`, no longer the stale per-block folders); `tools/service/check.py` (34 checks,
  `--calibration service|real|roland`); `sh101seq --selftest` (the firmware); `tools/compare/`
  against Roland's renders, `real.py` (the real clips' numbers) and `units.py` (per unit, per
  event, against the real clips); the A/B scenes (`host/scenes/sh101-sweep.json`;
  `private/sh101/reference/scenes/shootout-real`, 12); `auval`.

## Plan, in order

Nothing planned: what is left waits on data.

Waits on data:
- the ENV's weight: new service material, or a healthy unit's voltages at IC18b's inputs at ENV
  10 (the board diagrams, photos, BOMs and bulletins are searched, 2026-10-26);
- FREQ's law: a unit's cutoff at several legible FREQ positions, or the voltage at FREQ's wiper
  against its travel;
- the resonance near onset: a second unit's RES series at known readings;
- where self-oscillation starts: a clip that shows it;
- jack board A's clamp: a lossless full-volume saw from the OUTPUT jack;
- switching off and on: a recording of a unit switched off mid-note, and one from power-on;
- the transcription points above: photos of S3's wiring, the slider-to-pin wiring, IC2a's
  collectors, IC4b's input and S7 deck 2;
- TUNE's range on a real unit;
- the HOLD jack's normalling (TR12, R41-R44, J7);
- the RES 0 conflict: a second unit's plateau at RES 0 and 10;
- the attack's shape at a short ATTACK: a clip with a known short ATTACK on an open filter
  (Perfect Circuit's takes rise evenly over about 55 ms where ours rises fast, then slows; their
  sliders aren't shown);
- the SUB OSC switch moved under a held note: a second unit doing it (Perfect Circuit's makes a
  noise burst on one of three moves, and its thumps run about 150 ms at -30 dB re the note where
  ours settle in 50 ms).

## Tried and dropped

- 2026-11-16 (accuracy, the filter's top): a knee in the expo instead of a new slope. Order 3 at
  140-150 µA gives FREQ 10 13.3-14.1 kHz, but as a ceiling: Perfect Circuit's saw with ENV 5.5
  on top of FREQ 10 needs a pole near 20 kHz or more, which it can't reach. Order 1 at 260-300 µA
  reaches 14.3-15.4 kHz and lets ENV past it, but compresses from a few kHz up: about 50 cents
  flat by 1 kHz on a keyboard-tracked self-oscillation, against AutomaticGainsay's law, which
  stays exponential to 5.5 kHz, and Roland's, within 6 cents to 5.8 kHz. Neither has a part on
  the schematic or in the chip's decode behind it.
- 2026-11-14 (CPU): the OTAs' tanh as 1 − 2/(e^2x + 1) through exp instead of expm1: 2.5 % more
  cycles on `bright`, not fewer. A linear predictor for the jack board's Newton start: no fewer
  iterations (its steps are large for other reasons). Stopping the portamento's Newton at an exact
  fixed point: bit-exact but no measurable gain.
- 2026-11-14: the IR3109's excess phase as one pole per stage (the plan's first item after the
  rails round): not there. In ngspice the decode's OTAs add under 6 ns of delay at 1-300 µA; taken
  out of the netlist, the chip's capacitances move its self-oscillation by a constant 19 cents
  (its CAP pins' 2.65 pF, now in the model) and 0.5 cents more at 4.8 kHz, the discrete
  resonance parts' (TR26, TR27, D30/D31) by 3.7 cents at 4.8 kHz. A state per stage for that
  isn't worth it.
- 2026-11-14: the decode's input-pair droop (transconductance 1/(1 + I_ABC/6.95 mA)) in the
  OTAs: it is most of what is left of the self-oscillation gap against the decode (14 of 22 cents
  at 4.8 kHz), but the Juno-106's law then fits 7.0 cents RMS instead of 3.0 (any droop of more
  than about 1 Ω of emitter resistance makes it worse), unless the expo is made to rise by as
  much, which changes nothing audible. Left out.
- 2026-11-14: the decode's buffer offsets alone (P-MOS λ 0.01, the lateral-PNP source's
  368.7 µA with 123k): the full mix 0.76 → 0.33 dB, but the VCF's AC from 1 to 100 µA 0.82 →
  1.37 dB, at 10 µA 0.04 → 0.25 dB, and the whole voice's self-oscillation 12 → 32 cents. The
  model's buffer gains had been offsetting the decode's OTA tails, 2 % over I_ABC; both go in
  together or neither. Done together the same day, with the CAP pins' capacitance (Done).
- 2026-11-14: the decode's transistors' 40-60 Ω collector resistance in the OTA outputs' saturation:
  the full mix slightly worse; left out.
- 2026-11-13: the ENV slider below 10 from Jorb's excerpt (FREQ 2.1, RES 7.4, ENV 5.3, KYBD 9.4):
  that unit's cutoff offset can't be separated from the ENV's weight with one excerpt.
- 2026-11-13: "the resonance rises faster near onset" on AutomaticGainsay's unit: mostly the
  slider's stretch (its readings against ours match at 2.02 / 4.62 / 7.54), not the circuit.

- 2026-10-23: the VCA's turn-on moved 0.3 V by hand (`vcaKneeShiftVolts`, fitted 2026-10-08 to
  the real release within 0.5 dB RMS and to Roland's curves): R196 on S6's side of R197, as the
  board has it, does it with nothing fitted. The field stays, at 0, for tools.
- 2026-10-23: Open Music Labs' rising linearity error in the expo (+1 % at 12 µA, +10 % at
  0.5 mA): a Juno-106's IR3109 doesn't show it end to end; it had put the model 38 cents sharp at
  4 kHz and 66 at 10 kHz.
- 2026-10-23: the decode's tail-mirror helper on 100k (about 33 nA): it bent the cutoff 3-16 % low
  below 1 µA, which real chips don't; 10 MΩ.
- 2026-10-23: R31 and R35 at 47k (Open Music Labs): RATE 10 30.6 Hz against a real unit's 38.2.
- 2026-10-24: noise at Roland's level in the Real unit calibration (`noiseRms` 1.30, +2.9 dB re
  the saw): no real unit reaches it. The Roland calibration keeps it.
- 2026-10-09: R169 and R188 at the printed 47k: two real boards have 100k (VCO MOD at 10 was 25 %
  short of Roland's).
- 2026-10-09: not taken from Din Sync's BOM: C57 at 0.47 µF (drawn 0.047; Roland's 2-3 ms GATE
  edges fit 0.047) and jack board C12 at 4.7 nF (drawn 0.001; the output's low-pass would be at
  15 kHz).
- 2026-10-09: a steeper expo (22-25 mV/oct, I₀ 6-14 µA, the ENV's peak 10 % low) fits every
  real-unit observation but bottoms VR8 out on 31 % of units (`spread.md`); a photo read R193 at
  150k, so the KYBD path is as drawn.
- 2026-10-08: the IR3109 with ideal buffers: self-oscillation 145-165 cents sharp of the
  transistor-level chip, a saw into it 1.3-3 dB loud.
- 2026-10-08: the first reading of the real unit's ENV sweep (51 Hz → 4.86 kHz, 6.6 octaves): the
  pitch tracker's 30 Hz floor and the video's edit.

## Declined

- 2026-11-15: the panel's keys and buttons playing the voice in voice mode: they stay inert, as on
  the 303.
- 2026-11-15: a resonance trim in the Real unit calibration (about 8 % more loop gain,
  self-oscillation from about 7.5), fitted to the two units read hotter than ours ('Resonance'):
  the loop stays as drawn.
- The standing rules apply: no paid reference sets (2026-10-22), no real unit to measure.
- Parked rather than declined: the Windows build's push; the publish steps wait for the ship gate (2026-10-23: "all these things except the publish ones").

## Done

- 2026-11-17, session load keeps the unit as it was (found checking a 303 report): a
  session saved the CPU's RAM but booted the CPU on load, which cleared ARPEGGIO, HOLD and the
  held keys, so a held arpeggio reopened silent. The session now carries the whole board
  (`Machine::snapshot`: CPU, latches, sample-and-holds) and loads it as saved, as the 909, 303 and
  808 do; only POWER boots. 0.1.2 sessions (RAM only) still boot; 0.1.2 reads new ones as it
  always did (the sequence kept, the CPU booted). Saved and loaded under the processor's
  callback lock, which its processBlock now takes. Check: `sh101plugintest` State. A held
  arpeggio with SYNC off therefore plays as soon as a song opens, the DAW stopped, as a 101 left
  on would: kept on purpose (Chris, 2026-11-18). With SYNC on it waits for the host's clock.
- 2026-11-16, accuracy round (the filter's top: is FREQ 10 with RES 10 silent on a real unit?).
  No: two real units self-oscillate or peak at 13.93 and 15.2 kHz there (Starsky Carr
  q0yu1txLd8I 7:05, every VCF slider legible; AutomaticGainsay VQXq7OmZCtY 3:19-3:38), Roland's
  plug-in at 15.6 kHz; ours was at 28 kHz. 58 videos searched: the two above, and the bright saws
  that had argued for a high top don't say it (Perfect Circuit's had ENV 5.5 with S 10;
  gstormelectro's shows only stills). Real unit calibration: `vcfMvPerOct` 20.8 → 22.8,
  `vcfI0uA` 5.0 → 3.30 (FREQ 0 kept: Friendly Noise's rest 31.0 Hz against 31.1), `envCvScale`
  0.82 → 0.90 (its 7.27-octave sweep kept; loopop's two units now imply 0.885 and 0.901, Failed
  Muso's ping 0.901, closer to the schematic's 1.0). FREQ 10 at KYBD 0: 28.1 → 14.7 kHz
  self-oscillating, a RES 7.1 peak 27.5 → 14.2 kHz; AutomaticGainsay's open saw at FREQ 10 now
  within 0.5 dB (it was 5.3 dB brighter). Costs: FREQ's law per division is shallower
  (AutomaticGainsay's rows −12 to −14 %, were −4 to −6 %) and Friendly Noise's FREQ 1.5 reading
  now matches at 1.97 (was 1.79). `units.py`: each unit's saw against ours at that unit's
  settings (Perfect Circuit with ENV 5.5), the two tops as rows with a 0.15-octave limit (the
  units' spread), the resonance grid to FREQ 8.8. The expo's knee became a calibration value
  (`vcfKneeUA`, `vcfKneeOrder`; the Juno-106's 408 µA and 3 in every calibration, bit-exact).
  Checks: `units.py` 107 rows, 0 over (7 known: the two above, AutomaticGainsay's RES 7.1 and
  the LFO's top as before); against Roland's renders 6.1 / 5.4 → 5.55 / 4.38 dB; SPICE 28/28,
  service 34/34, plugintest and host tests pass, corners 0 failing (596 / 616 retried, were
  607 / 683), auval passes; golden-9. CPU: no workload costs more; `longrun-resmax` 10 % fewer
  instructions, `automation` 5 % (fewer samples in the hard solves above 15 kHz).
- 2026-11-16, fix (FREQ and RES high sounded "just like white noise"): the VCF's Newton stopped at
  12 iterations and took what it had. With I_ABC above about 380 uA (the pole near 40 kHz) and RES
  high, the ultrasonic self-oscillation needs 13-23 on some samples; the unconverged ones (up to
  6 %) made it chaotic, white noise in the audio band at -11 dB of the oscillation (FREQ 10, RES 10,
  C4, KYBD 10: -27 dBFS, flatness 0.87). Now up to 32 iterations, and a sample that still doesn't
  converge is done again from where it started in 2, 4, 8 and 16 steps (`solveSample`); the
  oscillation is a pure tone near 38 kHz under the decimator (-59 dBFS), as on the real unit.
  Retrying from 12 iterations instead was tried: it retries up to 15 % of the samples there, and
  steps of two lengths mixed leave a noise floor about 35 dB under the oscillation.
  Every implicit solve (VCF, jack board, VCA control current) now counts its unconverged samples
  (`SolverStats`, in the voice's probe), and `sh101corners` (tools/corners) renders 299 cases of
  the controls' extremes at 44.1 and 48 kHz and fails on any unconverged solve, broadband noise
  with no noise source, or output out of bounds (6 s). With the old solve it fails 187 cases;
  now 0, 607 and 683 samples retried in shorter steps out of 59 and 64 million. What is left at
  the top is aliasing ("Self-oscillation above the audio band aliases").
  None of the other checks could see it: the SPICE comparisons stop at 100 uA, the recordings
  never sit there, and the golden renders had it in them (`automation`'s first 1.4 s).
  `sh101bench` bit-exact against golden-7 except `automation` (the noise gone, then the
  self-oscillation's trajectory differs); new golden `build/sh101-golden-8`. Instructions +0.6 %
  (12412 → 12484 M/s; `bright` 1109 → 1114 Mins/s at 48 kHz). plugintest 0 failures, service
  checks 34/34, `units.py` 105 rows 0 over, auval passes.
- 2026-11-16, release: `ro-mono-101` 0.1.0 beta, with a standalone app; Windows built with
  clang-cl (both compilers passed `windows-machine.yml`). The docs name sources by their channel,
  site or publication only.
- 2026-11-16: CAL leaves the bar and the Calibration parameter goes: the plugin plays the Real
  unit calibration, the one the A/B scenes were heard with. Service notes and Roland stay for the
  tools (`sh101render --calibration`, the processor's calibration override): the service checks
  and the A/B against Roland's plug-in. CLOCK folds into SYNC: a click steps OFF, HOST 1/16, 1/8,
  1/4, 1/32, 1/16T, 1/8T (shift-click back), so the bar is SYNC and `?` as on the 303. The
  parameters (Sync, Sync Clock) stay. `sh101plugintest` 0 failures, the service checks 34/34,
  `sh101bench` bit-exact against `build/sh101-golden-7`, auval passes.
- 2026-11-15: a new instance starts warm, as on every Lemus machine: no switch-on mute (it had held
  the first notes after a load for about 1.4 s); only POWER runs it. `sh101plugintest`'s POWER check
  takes the switch-on from the switch; `sh101bench` bit-exact against `build/sh101-golden-7`; the
  A/B scenes' lead-in 1.6 s → none (their plugin sources preroll 0.3 s, past the coupling
  capacitors' settling).
- 2026-11-15, A/B listen of the twelve real-unit scenes: no model change. Every difference heard
  was the scene (settings or note timing read wrong, a switch moved during a take, the recording
  chain) or unit spread, and the scenes now carry what each take does. Checked on the way:
  **the release's shape** with known settings, Failed Muso's unit (R about 3 from S 5: the times
  to fall 1/3/6/10/15/20 dB within 25 ms of ours at R 2.5) and the Friendly Noise unit (R 10 from
  S 10: the level within about 1 dB, the slow start included); **the low end**: Perfect
  Circuit's takes (saw, square, the sub's three modes, 21.9-700 Hz) fit ours within 0.15 dB and
  1.5 degrees through one recording low-cut, second-order at 13.5 Hz, Q 0.58, so the unit's low
  end and ours agree to 22 Hz; **cutoff and resonance**: loopop's unit lower and more resonant
  than ours at the read FREQ and RES, Once Upon a Synth's 1.2 octaves brighter ('FREQ's bottom',
  'Resonance'). A resonance trim toward the hotter units was declined.
- 2026-11-14, CPU: the filter's and the jack board's Newton solvers, and three bit-exact caches.
  **Newton's early stop:** the IR3109's solver stops on a step under 0.1 mV that no limit cut
  (the step after one measures 1e-9 to 1e-13 V, inside the 0.1 µV it had to fall under before),
  jack board A's on one under 10 µV (the next about 1 nV); mean iterations at RES 0.7 2.0 → 1.0,
  self-oscillating at 50 µA 3.5 → 2.6, the jack board's in `bright` 3.9 → 2.7. **The filter's
  divisions:** the networks' conductances, 1/2V_T, 1/V_T and stage 1's node as multipliers.
  **Caches** (bit-exact: the same inputs from a fixed point): the CEM3340's frequency for a held
  current, the PWM network's pin 5, the BA662's control current. **Instructions:** every workload
  14 713 → 12 384 M/s (−15.8 %); `bright` 1292 → 1109 Mins/s, 346 → 276 Mcyc/s (−20 %, both on a
  loaded machine), about 7.44 → 6.1 % of a core at 48 kHz. **Sound:** not bit-exact. Against
  golden-6, the workloads that don't self-oscillate differ by −142 to −169 dBFS; `longrun-resmax`
  by −90 dBFS of phase (its envelope and spectrum identical); `automation`, which sweeps RES
  through self-oscillation over and over, by up to 1.3 dB of envelope at 44.1 kHz, as a 1e-15
  nudge to the old code does (1.0 dB): the oscillation builds from noise each time (in 50 ms
  windows a median of 0 dB, 95 % within 0.2 dB at 48 kHz and 0.4 dB at 44.1 kHz; its level the same
  to 0.002 dB and its third-octave spectrum to 0.1 dB; accepted 2026-11-15). The SPICE
  suite (28/28), the service checks (34/34 ×3), `units.py` (no row moved) and the comparison with
  Roland's renders read the same. Golden renders `build/sh101-golden-7`; plugintest 73/0; host
  tests, the idle check and auval pass.
- 2026-11-14 (second round), accuracy: the IR3109 against its transistor-level decode, the plan's
  two items. **Excess phase (item 1): not there.** In ngspice the decode's OTAs add under 6 ns of
  delay; taken out of the netlist one at a time, its chip capacitances move self-oscillation by a
  constant 19 cents and 0.5 cents more at 4.8 kHz, the discrete resonance parts' by 3.7 cents at
  4.8 kHz (Tried and dropped). What the +25 cents at 4.8 kHz was: the decode's OTA
  transconductance against I_ABC/2V_T, +2.2 % at 1 µA falling to −2.8 % at 300 µA (one OTA in
  ngspice), which three terms follow within 0.04 % from 0.3 to 500 µA: the mirrors' current gain
  (the tail mirror's and the Wilsons' Early effects and β: 1 + 2.2 %, falling 0.143 % per e-fold)
  and the input pair's droop, 1/(1 + I_ABC/6.95 mA). **In the model now** (`vcf_ir3109`): the
  mirrors' gain; the CAP pins' own 2.65 pF; the buffers' channel-length modulation (λ 0.01) and
  stage 2/4 current sources (368.7 µA, 123k: item 2, the buffers and tails together). The
  buffers' outputs at the rails moved onto the decode's (11-51 → 2-7 mV), their gains on stages 2
  and 4 0.967 → 0.955; the poles 97.3 Hz/µA at 1 µA and 97.0 at 10 µA. The droop stays out: the
  Juno-106's law then fits 7.0 cents RMS instead of 3.0 (Known inaccuracies, "IR3109").
  **Against the decode:** self-oscillation −4.9 / +4.2 / +24.8 → +0.3 / +5.5 / +22.2 cents at
  194 / 970 / 4800 Hz, its level −0.18 → −0.14 dB at most; AC at 1 µA 0.64 → 0.07 dB, at 10 µA
  0.04 → 0.09, 1-100 µA 0.82 → 0.75; the suite's limit back to 25 cents. **Whole voice:** the full
  mix's harmonics 0.76 → 0.33 dB (the round's audible target: saw, pulse and sub at 10, the sub's
  odd harmonics where each stage reaches its floor); self-oscillation −20…+5 → −9…+6 cents, the
  FREQ 3 case's waveform error +5.6 → −21 dB; I_ABC within 4 cents (the model's own I_ABC is now
  probed, `Voice::Probe::iAbc`, instead of being read back through a fixed buffer gain, which the
  new gains would have moved by 10 cents); limits 20 → 8 cents and 30 → 15. **Unchanged within a
  few cents and tenths of a dB:** `units.py` (105 rows, 60 ok, 0 over; the Real unit's ENV rest
  30.6 → 31.1 Hz, the take's 31.1), the service checks (34/34 ×3), against Roland's renders
  (6.1 / 5.4, 5.5 / 4.0, 8.7 / 8.6 dB); no calibration refitted. Golden renders
  `build/sh101-golden-6` (against golden-5: `bright` −40 dBFS, the self-oscillating workloads −4.8
  to −6.6 dBFS from the drift of a few cents); plugintest 73/0; host tests and auval pass.
  **CPU:** +2.5 % instructions (`bright` 1260 → 1292 Mins/s; the buffer's two extra terms per
  Newton step; the iterations are unchanged); 7.54 % measured with the machine loaded, about
  7.44 % expected.
- 2026-11-14, accuracy: everything free data allows, against the schematic, ngspice and the real
  clips. **Rails:** the +15 V (IC1b, 14.87 V) and +14 V (TR3's emitter, 14.3 V) symbols read off
  SN p.7 and applied part by part (`circuit-osc.md` and `circuit-filter.md` rails tables): FREQ's
  top, R187, the LFO's IC3 and R35, the bender's R10/R11 on +15 V; R92, R177/R178, the 4013, the
  ENV GEN, the BA662, R195, R196 and TR25 on +14 V. FREQ 10 19 → 28 kHz pole (the brightest unit's
  saw is a floor ours now clears, `units.py` `top`); the ENV's peak 10.42 → 10.11 V; PW 10 silent →
  3.0 % (law 50 / 38.3 / 26.5 / 14.8 / 3.0 % against Roland's 49.9 / 38.4 / 26.8 / 15.3 / 3.9);
  the square and subs 0.2-0.6 dB down, within the parts' spread of every unit. **Against
  ngspice:** the OTAs' output resistance and the BA662's input resistance modelled (the whole
  voice's level 0.52 → 0.11 dB); TNOM=25 on the discrete devices and every netlist at 25 °C (I_ABC
  17 → 5 cents, the cutoff CV 7.4 → 2.5); the ENV GEN's TR35 saturated and TR36's decay solved (ENV
  to the VCA 0.26 → 0.02 dB); the IR3109's outputs stop softly near the rails, the Wilson mirrors'
  saturation (the full mix's harmonics 1.90 → 0.76 dB RMS, its level 0.42 → 0.08 dB,
  self-oscillation's level at 4.8 kHz 0.62 → 0.18 dB, its pitch +22 → +25 cents); the oscillator
  section moved from 27 °C and I_S 20 fA to the filter's 25 °C part set (PW 7 0.16 % of duty off
  SPICE, its harmonics 1.39 → 0.33 dB). The IR3109's self-oscillation limit 22 → 30 cents and
  0.6 → 0.7 dB, with the reason in `suite.py` (the decode's excess phase, plan item 1);
  `compare_vcf.py` takes a time-weighted mean (it had added 41 mV to the 50 µA peak). **Noise:**
  C31's corner 900 → 1150 Hz, `noiseRms` 1.05 → 0.98 in the Real unit calibration; `units.py`
  measures the source's colour with each unit's own saw taken out (`noise`) and the filter's top
  against 1/n (`top`); the three OVERs it had were the units' tops and recording chains.
  **Calibrations:** Real unit `vcfI0uA` 6.0 → 5.0, `envCvScale` 0.77 → 0.82; Roland
  `vcfMvPerOct` 20.8 → 22.1 (keeps its 12.2-octave span with FREQ's top on +15 V), `vcfI0uA` 6.0 →
  3.9, `envCvScale` 0.80 → 0.89. **Checks:** SPICE suite 28 of 28; service checks 34/34 in each
  calibration; `units.py` 68 → 105 rows (60 ok, 4 known, 41 info); against Roland's renders 5.6 /
  4.1 → 5.5 / 4.0 dB (Roland), 5.6 / 4.0 → 6.1 / 5.4 (Real unit, which follows the real units' top
  where Roland's plug-in is darker); plugintest 73/0; golden renders `build/sh101-golden-5`
  (against golden-4 up to −4.1 dBFS, `automation`). **CPU:** `bright` 6.72 → 7.26 % of a core at
  48 kHz, every workload 12 624 → 14 354 M instructions/s (+13.7 %; the IR3109's saturation +12 %),
  inside the 7.5 % budget. **No change:** the LFO's rails confirmed (RATE 10 36.4 Hz stays unit
  spread); FREQ's law (one unit 5-8 % steeper) logged, cause not settled.
- 2026-11-13: the panel, the check set, and the cheap accuracy items. **Panel:** the bar, held
  keys, key list and tooltips from `host/PanelFrame.h`, as on the other machines (SYNC, CLOCK,
  CAL, `?`; held keys and buttons by name in orange; Space is PLAY in the standalone app); the
  brand marks as artwork places (`maker`, `model`; `ro-mono-101-maker.png` and the like, or
  `artwork/sh101/`), drawn as their files look, the older pack still read, the blank pack no
  longer written and untouched copies taken out; host tests for both. **Check set:**
  `tools/spice/suite.py` (28 checks with limits, 28 of 28); every SPICE script now takes the build
  from `SH101_BUILD` or `build/sh101` (the osc comparison had been reading an October binary in
  `build/sh101-osc`, which put the LFO 11-16 % off); `tools/compare/units.py` (68 rows per unit and
  per event against nine real units: 51 ok, 5 known, 12 info, 22 s); `sh101bench` with thread
  time, Mcyc/s, Mins/s and the worst block; `plugintest --panel ... keys|held`; auval on record.
  The baseline: bit-exact against `build/sh101-golden-4`, service checks 34/34, plugintest 73/0.
  **CPU:** `bright` 6.72 % of a core at 48 kHz (M5 Max); no CPU round needed. **Accuracy, no
  change to the sound:** R31 at 100k confirmed by a real unit's square-to-triangle swing
  (`circuit-osc.md` 5.3), and RATE 10's 36.4 Hz against a real 38.2 Hz is unit spread (the spread
  study rerun at 100k: 33.2-41.5 Hz); the portamento's top matches a second unit within 0.01 s and
  its mid-travel gap is the knob reading's; FREQ's bottom is 0.06 division past one unit's reading
  while a second unit sits the other way. `docs/sources.md` (every source with its URL; the
  files' SHA1s in `private/sh101/sources-sha1.tsv`, `tools/sources_manifest.py`). Docs: the
  lines earlier rounds left behind (the LFO's rates, the noise's level, `vcaKneeShiftVolts`, TR7's
  mute, the plugintest count), the SPICE table, the README's status in the ship's form.
- 2026-10-26: switching off (`521deb1`). `JackBoard::powerOff`: +9 V falls (τ 12 ms, an
  estimate), TR8 and C14 drive TR7 within a millisecond, IC1a's fall through C2 leaves a thump of
  about −41 dBFS, exact zeros after 0.5 s; `tools/spice/jack` `off` within 1 mV after the first
  0.6 ms; plugintest checks it. Golden renders `build/sh101-golden-4`. Twelve real-against-Lemus
  A/B scenes (private, `shootout-real`).
- 2026-10-24, accuracy (`d39367f`, committed 2026-10-26): the real clips per unit. The resonance
  against RES matches two units (no change); the square and subs are within part tolerance of
  three units, and the schematic's +4.5 / +2.8 dB is ruled out; `noiseRms` 1.30 → 1.05 V (+0.6 dB
  re the saw, as two units); loopop's unit (VCA GATE) needs an ENV weight of 0.72-0.79, so the
  0.77 is common to units and the VCA switch is ruled out.
- 2026-10-23, CPU (`7c11291`, committed 2026-10-24): `sh101bench` at 44.1 kHz, `midi` 14.2 →
  7.9 %, `bright` 18.5 → 11.2 %, `longrun-resmax` 21.6 → 14 %; every workload at both rates 245 →
  133 %. The VCF's Newton solved along its structure (down to 2×2), the trapezoid's slopes carried
  to first order at acceptance, tanh through expm1, the prewarp, ENV factors, expo and KYBD
  follower cached, the decimator's phases split, the keyboard driver idle. Renders identical but
  where self-oscillation starts from noise (golden `build/sh101-golden-3`).
- 2026-10-23, accuracy (`7c11291`): R196 on S6's side of R197 (the board diagram): the VCA's fade
  within 0.3 dB of the real release, no fitted shift. R31 and R35 100k: RATE 10 30.6 → 36.7 Hz
  (real 38.2). The IR3109's law from a Juno-106: exponential to 10 kHz, an order-3 knee at 408 µA.
  The SPICE decode's helper at 10 MΩ. VR8's trim solves TR42's gain (VCF WIDTH 0.9996). Against
  Roland 5.7 / 4.2 → 5.6 / 4.0 dB.
- 2026-10-23, the plugin (`7c11291`): ro-mono-101 (R101; AU, VST3, standalone), `kModelVersion` 1
  in the state, voice mode without the CPU ROM, POWER with jack board A's power-on mute and clamp
  (within 3 mV of ngspice), right-click latches, a new instance with no sequence, CLEAR MEMORY
  gone; `sh101bench`, `sh101plugintest --panel`.
- 2026-10-09: sleep (`8a5d170`): exact zeros once the gate is closed, the output under −120 dB for
  100 ms and the ENV, VCA and portamento settled: about 0.5 % of a core instead of 15 %. Tail 36 s
  (RELEASE at maximum).
- 2026-10-09: the artwork recipes and tracing tools out of the repo; the pack found by name
  (`a26934e`).
- 2026-10-09, accuracy (`2daaafe`, `ebf42ca`): R169 and R188 100k, from Din Sync's BOM and a board
  photo: VCO MOD at 10 from 25 % short of Roland's to 7 % over, RATE 10 32.5 → 30.6 Hz. The boards
  confirm R162, R158, R160, R189, R192 and R193; one unit's ENV peak is 5.26 V above the bus, 2 %
  under the drawing.
- 2026-10-08, accuracy (`993a07c`, `87ffa12`, `d61aa36`): the Real unit calibration, the default,
  fitted to the Friendly Noise unit (rest 31.8 Hz, plateau 4.81 kHz, 7.24 octaves, attack 2.77 s,
  release τ 4.23 s) with `envCvScale`; the IR3109's buffers (self-oscillation from about 150 cents
  sharp to within 3-22 cents of the transistor-level chip); the parts-spread study; the VCA's fade
  fitted (0.3 V); the whole voice as one circuit in ngspice (43 cases); one +15 V rail at 14.87 V
  (PW 7 had been 0.5 % narrow). Against Roland 5.8 → 5.6 dB.
- 2026-10-08: the build (`14db001`): the TMP80C49 firmware on an MCS-48 core (`sh101seq
  --selftest`), the synth board part by part, every block against ngspice, the service checks
  34/34, the host bit-identical to the machine alone; against Roland 5.8 dB over 205 specs.
