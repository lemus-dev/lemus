# TB-303: what we know is wrong, and the plan

What the model gets wrong or hasn't checked, as the transcription, the SPICE runs and the
comparisons with the real units found it (`docs/circuit.md`, `docs/model.md`), and what is planned.
"Real" is Din Sync's unit (400 single notes, 2010; `docs/model.md`, "Real TB-303 single notes"),
the one the plugin's unit is fitted to, unless it says otherwise; "acidvoice" is acidvoice.com's
unit (its shootout clips); "the RE-303" is acidvoice.com's RE-303, a replica of the TB-303's boards
(`tools/compare/re303.py`). The figures against Din Sync's unit are with CUT OFF MAX at the fitted
unit's 1.35 kHz, not the 2.3 kHz the plugin ships at.

ro-mono-303 has shipped (0.1.5, 2026-11-01). A change to the sound bumps `kModelVersion` (now 1)
and keeps the old behaviour for sessions saved before it (`plugin/PluginProcessor.h`); such a change is
agreed before it ships.

## Known inaccuracies

### Sound

- **Brightness in the first 100 ms.** Against Din Sync's notes, mean over saw / square: 375 / 392
  cents from the real unit's (the unit against its own retakes: 41 / 60), about 25 %; later in the
  note (0.6-1.2 s) 184 / 174 cents (33 / 30). The whole spectrum: log-spectral distance 3.0 / 2.9 dB
  (2.5 / 2.2); level 0.8 dB (0.1). Not located. Next: which sets carry it, note by note
  (`tools/compare/dinsync.py`).
- **Resonant peak.** 145 / 135 cents from Din Sync's on average (retakes 95 / 43); the sets with the
  other knobs at 25 % about 250 cents high.
- **Accented low notes: a notch the model doesn't have.** acidvoice's accented A#1s at CUT OFF 0
  (eight notes, all alike): the 5th harmonic (291 Hz) 16 dB under ours, the 10th-11th 7-8 dB under,
  the 3rd, 6th and 12th within 2-6 dB. Din Sync's accented C2 at CUT OFF 0 dips at the 4th-5th
  harmonic where ours dips at the 6th. Plain notes match, and so do accented A#2s. Something about
  3 ms long in every cycle of an accent, or a once-a-cycle wobble of the VCA's gain; nothing in the
  circuit as transcribed is that strong (`docs/model.md`, "Open points"). Ruled out: Tried and
  dropped, 2026-10-12. Waits on data.
- **The VCA's swing.** Fitted, not explained: the BA662's output saturating at a fifth of the
  reverse-engineered chip's swing (~±1 V about its bias) gives Din Sync's loud accents' peak against
  RMS 14.4 dB (real 14.1) and their spikes +10.4 dB (real +10.9); acidvoice's accents 5.9 / 12.5 dB at
  CUT OFF 0 / 60 % against 6.2 / 9.6. In the circuit as transcribed nothing nears a limit there (the
  chip's output peaks at ~1.3 V on a full accent). Waits on data.
- **The accent sweep.** VR4-B fitted at 37.5k (25 % under its 50k): acidvoice's accents top out
  within −70 to +30 cents of the real ones (mean error over each accent's first 60 ms 68 cents, its
  plain notes 86); Din Sync's accented notes' resonant peak over their first 150 ms 180 cents. The
  climb gets halfway 2-3 ms sooner than acidvoice's (10 / 13 ms against 13 / 15 ms on A#2 / A#1),
  inside the measurement's resolution of one period.
- **The attack.** Fitted: BA662 pin 1 80 mV lower against Q31's V_EB, both within their spreads.
  RMS error over the first 5 ms 1.7 dB against Din Sync's plain notes with a still filter. The sound
  starts ~6 ms after the gate and reaches half level 1.7 ms later (Whittle: "a typical 4 ms delay
  and then a 3 ms attack").
- **CUT OFF's upper half: units differ.** The fitted unit's law reaches 1.35 kHz of ring at full
  CUT OFF, and Din Sync's cutoff sweep with everything else at 0 matches it within 50 cents at every
  position. The plugin ships CUT OFF MAX at 2.3 kHz, a typical unit's (Whittle: ~2.5 kHz). One
  two-segment law sets the top and the whole upper half: at 2.5 kHz, CUT OFF 60 % (RESONANCE full,
  no env mod) rings ~1040 Hz where acidvoice's unit rings ~700 Hz at 60 % (the fitted law: 622 Hz;
  ours at 54 % matches it). Waits on data.
- **Against SPICE of the schematic** (`docs/model.md`, "SPICE"): the square's 4-8 kHz content
  +2.8 dB at C5 (junction capacitances soften the real edges); full CUT OFF with full RESONANCE
  ~3.5 dB of spectral distance (the self-oscillation threshold); with Q21 at the top of its F rank
  (gain 435) SPICE's ring at the top of the range decays ~5 dB per 100 ms, ours 26; the resonant
  peak's frequency within ~6 %; the whole voice's harmonics within 0.2-1.4 dB RMS.
- **Against the RE-303** (32 clips, one pattern at 16 knob settings, saw and square;
  `tools/compare/re303.py`, third-octave bands in three slices a step, at the clip's own pitch).
  RMS distance 4.79 dB as the plugin ships; 4.54 with its trims fitted (FULL CUT OFF 2.0 kHz, TM3
  where it ships); 4.14 with its CUT OFF / ENV MOD pot law fitted as well (more of the track by 3
  o'clock than the two-segment law); 4.07 with its DECAY law and MEG peak (within 3 % of the
  shipped unit's). Its pot law makes Din Sync's score worse (4.89 → 5.11-5.25), so it is that
  unit's (the replica's pots, or the knob positions read off a drawing); its DECAY and MEG values
  leave Din Sync's unchanged (4.886), so the envelope agrees with both units. Left over, not
  explained by any trim or part value: ENV MOD 8 with DECAY 3 (setting 10) ~7 semitones darker
  than the RE-303 from the start of each note; all knobs at 5 (setting 07) and DECAY 5 with ENV
  MOD 5 (setting 15) 3-4 semitones darker; full ACCENT sweeps ~2 semitones higher than the RE-303's
  (acidvoice's TB-303 sat 1.3-2.3 semitones above ours before VR4-B was fitted: spread).
- **The BA662's unknowns.** No Rohm data sheet: modelled from Open Music Labs' reverse engineering
  of an original chip and its clones' data sheets (`docs/circuit.md` 6.1). Pin 1's drop (±0.04 V)
  moves the accent ±0.5 dB. Real chips have 30-250 µV of input offset, either sign (two originals
  measured), which steps the output −2 to −8 mV at a note's start in SPICE's chip; the model's has
  none (unit-specific). Q32's β (the 2SC945's rank isn't known) sets how long the VEG holds.
- **Pitch.** No gap: A 110.03 Hz, low C to high C 2.0006 : 1 (`tools/service/check.py`). Din Sync's
  unit plays C2 + 5 cents (TUNING not stated); acidvoice's octave is ~2.03 : 1 (its TM5 out of trim,
  which SERVICE's TM5 can set).

Against Roland's TB-303 software (not a real unit; `docs/model.md`, "Roland TB-303 software"): mean
log-spectral distance 2.8 dB over 56 tests. Where they differ the model follows the schematic: half
ACCENT +4.1 dB against ~+11.5, VOLUME at full ~4.5 dB louder, TUNING ±700 cents against ±1 octave,
the peak at CUT OFF 0 and full RESONANCE +7.4 against +11.0 dB.

### Behaviour and features

- **Not emulated:** MIX IN, CV and GATE OUT, the headphone amp, the battery (its LED, and the
  random memory a flat one leaves: a new instance has every pattern as PATTERN CLEAR leaves it), the
  DIN socket's unofficial sync out with a half-inserted plug (`docs/sequencer.md`).
- **Firmware:** status input PA2 (Q7, 1.5 MΩ base resistor) isn't identified (`docs/firmware.md`).
- **SERVICE:** which trimmers TM1 and TM2 are (47k or 220k), the ranges of TM1, TM2 and TM6, and
  where 2 ms falls in TM3's travel are assumptions; TM6 moves only the pitch side, not the VCF and
  VCA biases that also hang off the 5.333 V reference (`docs/model.md`, "Service trimmers").
- **Slid accents** are weak (+3-4 dB, no sweep: the MEG isn't retriggered), as on the real unit;
  the firmware's and MIDI's renders of accented slides agree to 44 dB (2026-10-31). Not a gap.

### Reference data

| Unit | Where | What it has | Known settings |
|---|---|---|---|
| Din Sync's TB-303 (2010; serial, revision and trims unknown) | `private/tb303/reference/dinsync/` | 400 single low Cs (C2), plain and accented, saw and square: 25 sets, each sweeping one of CUT OFF, RESONANCE, ENV MOD, DECAY, ACCENT; 16-bit, 44.1 kHz | the five knobs at 0/25/50/75/100 % (approximate); TUNING, VOLUME, tempo unknown |
| acidvoice.com's TB-303 | `private/tb303/reference/shootout/roland_tb-303_real/` (mp3) | three accent tests (`303_60`, `303_0_0`, `303_100`: A#1 and A#2, 136.04 BPM); pattern clips, saw and square, dry and through a distortion pedal | accent tests: saw, ACCENT 100 %, CUT OFF / RESONANCE 60/100, 0/0, 100/100 %, ENV MOD 0 (from the clips); the rest unknown |
| acidvoice.com's RE-303 (a replica of the TB-303's boards; sent 2026-11-10) | `private/tb303/reference/acidvoice-re303/`; A/B scenes in `private/tb303/reference/scenes/acidvoice-re303/` | one 16-step pattern (A1-E3, three accents, four ties, no slides) at 130 BPM, at 16 knob settings, saw and square: 32 clips, 24-bit, 44.1 kHz | the five knobs from a drawing per setting (clock positions, good to about half a tick); TUNING read from the clips (10-32 cents flat, varying by clip); VOLUME unknown |
| ADSR Sounds' TB-303 (YouTube, 2016) | `private/tb303/reference/shootout/adsr-303/` | 12 two-bar takes, cut and transcribed step by step; AAC 128 kb/s | knobs "closed", "half", "open" only; not compared yet |
| Whittle's unit(s) (Devil Fish docs) | `private/tb303/docs/papers/forums/` (S10 in `docs/research.md`) | published figures: release ~16 ms, typical full CUT OFF ~2.5 kHz, 4 ms delay and 3 ms attack | as published |
| Owners' recordings | none yet; lemusdev.org/ro-mono-303/record lists the takes | | |
| Roland's TB-303 software (not a real unit) | `private/tb303/reference/roland-vst/` | 61 test renders (Simple Saw), the plugin state | same MIDI and pot positions as the model |
| SPICE of the transcribed schematic | `tools/spice/` (community models in `private/tb303/reference/spice/`) | every block and the whole voice, typical parts and both ends of their spread | |
| BA662 sources | `private/tb303/reference/ba662/` | Open Music Labs' reverse engineering, Alfa AS662 and Rohm BA6110 data | |

No paid sets, and no real unit here to measure.

## Budget

- **CPU:** `tb303bench`'s `midi` (the acid line from MIDI: three octaves, accents and slides) under
  13 % of a core at 48 kHz, and no other playing workload over it (proposed, to be confirmed): the last
  measured top plus about 10 %, under the ~15 % the README's Idle paragraph still states.
  Last measured: 10.5-11.7 % of a core at 48 kHz playing (2026-10-09, 0837561; 17-19 % before it; the
  commit doesn't split it by workload), asleep about 0.1 % (443f49d). Not measured since the fits
  of 2026-10-12 and the trims of 2026-10-13.
- **Check set baseline:** none kept: no `build/tb303-golden-*` on disk. Service checks 16 of 16
  (`docs/model.md`, "Service notes"); SPICE per block as in `docs/model.md`'s table (2026-10-07).
- **Check set:** there: `tb303plugintest` (ROM, sync with the host's transport, MIDI and accents,
  sleep, wake and the same sound after, internal clock, power off and on, random-settings stress,
  TM3's ring, FULL CUT OFF, long-run stability at both ends of TM3, each trimmer, state with the
  model version); `lemustest_tb303` (`tests/host.cpp`: ROMs, the ROM folder, artwork pack and
  places, editor, MIDI); `tb303bench time|golden|check` (CPU % per workload at 44.1 and 48 kHz, max
  difference against golden renders); the SPICE checks (`tools/spice/compare_slide.py`,
  `compare_vco.py`, `compare_vcf.py`, `compare_env.py`, `compare_out.py`, `compare_voice.py`, through
  `tb303blocks`); `tools/service/check.py`; `tools/compare/` (`dinsync.py` note by note,
  `acidvoice_check.py`, `compare.py` against Roland's software); the A/B scenes
  (`host/scenes/tb303-sweep.json`; `private/tb303/reference/scenes/acidvoice-real/`, the three
  accent tests against Lemus; `voice-ab/`, 21 against Roland's software); `auval -v aumu R303 Lmus`.
  Missing: the bench's worst block, time asleep, Mins/s and instruction counts; one SPICE run with
  pass or fail (`tools/spice/suite.py`); `tb303plugintest --panel`; golden renders on disk. The ship
  gate needs none of them.

## Plan, in order

1. **Baseline** (tooling): the whole check set on today's code, golden renders kept as
   `build/tb303-golden-0`, the bench re-run against the budget. Every round starts from it.
2. **Decision:** confirm the CPU budget above (cpu).
3. **The check set's missing pieces** (tooling): `tb303bench` with the worst block, time asleep,
   Mins/s and instruction counts, as `tr909bench` has; `tools/spice/suite.py` running the six
   comparisons with pass or fail bounds from `docs/model.md`'s table; `tb303plugintest --panel`.
4. **README status** (tooling), for the next release: the Idle paragraph's "about 15 %" predates
   2026-10-09's CPU pass; Roland's software as a section of its own, apart from the real units
   (it is one of the Status list's four checks now).
5. **Decision:** take acidvoice.com's full-CUT-OFF figure out of the README's "Units differ" (and out
   of `docs/model.md` and the FULL CUT OFF comment in `plugin/PluginProcessor.cpp`), as the site did
   on 2026-10-14? (tooling)
6. **Brightness in the first 100 ms** (accuracy): 375 / 392 cents against Din Sync's; which sets
   carry it, note by note, and a mechanism rather than a part pinned at its tolerance.
7. **The resonant peak with the other knobs at 25 %** (accuracy): ~250 cents high against Din
   Sync's.
8. **Near self-oscillation against SPICE** (accuracy): full CUT OFF with full RESONANCE 3.5 dB; the
   ring's decay with Q21 at the top of its rank 26 against 5 dB per 100 ms.
9. **A third unit** (accuracy): ADSR Sounds' takes against Lemus, note by note, to tell unit spread
   from model error (lossy, knobs only roughly known).
10. **The square's top** (accuracy): 4-8 kHz +2.8 dB at C5 against SPICE (Q8's junction
    capacitances).
11. **SERVICE's TM6** (accuracy): its range from the IC16b network (`docs/circuit.md` §1), and the
    VCF and VCA biases moving with the reference.
12. **PA2** (accuracy): what drives Q7, from the schematic, and what the firmware does with it.
13. **ENV MOD with DECAY between 3 and 5 against the RE-303** (accuracy): ~7 semitones darker at
    ENV MOD 8 / DECAY 3 from the note's start, 3-4 at DECAY 5; DECAY 0 and 10 match. No trim or
    part value closes it. Compare Din Sync's ENV MOD and DECAY sets at 25-75 % note by note; if they
    show it too, it's the model.
14. **sources.md** (tooling): every source file with its URL and SHA1; `docs/research.md` §0 lists
    the papers with URLs, without hashes, and the recordings' indexes are in the private folder.

No release blockers: sleep, the model version, artwork places and the plugin name are in, and the
ship gate's checks (`tools/service/check.py`, `tb303bench time`) exist. Any item above that changes
the sound bumps `kModelVersion` first.

Waits on data:
- **CUT OFF's top and upper half across units:** Din Sync's unit ~1.35 kHz, Whittle's typical
  ~2.5 kHz; the plugin ships 2.3 kHz. acidvoice.com's full-CUT-OFF clip isn't used as a data point:
  those recordings may be distorted (2026-10-14). Would settle it: owners' takes at known
  settings from several units (the record page's filter range, L3-L7: CUT OFF on five ticks,
  RESONANCE full).
- **The notch on accented low notes:** single accented low notes, CUT OFF, RESONANCE and ENV MOD at
  0, ACCENT 25/50/75/100 %, at two or three pitches, from any unit (the record page's A1-A5).
- **How typical Din Sync's unit is:** a second unit at known settings (the record page's E1 and E2
  compare with Din Sync's all-100 % and all-50 % notes).
- **The VCA's swing and pin 1:** an original BA662's output stage and pin 1 measured, or Rohm's
  data.
- **TM1 and TM2's values, TM3's travel:** a clear photo of a board's trimmers.

## Tried and dropped

- Before the port (2026-10-07): a fit with Q21 at β 1000 and a 0.44 mA tail, outside the parts'
  ranges: at high resonance a ~6 Hz oscillation built up over several loops ("bubbling"), which the
  short tests didn't show. Fits now keep to the parts' ranges and reject an unstable calibration
  (`specs/stability.json`; plugintest at both ends of TM3).
- 2026-10-08: the switch between two calibrations (service notes: 2.0 ms ring, β 300; Roland's
  software: 2.47 ms, β 435), replaced by the unit fitted to Din Sync's notes (score 9.76 / 7.11 /
  4.89 fitted).
- 2026-10-12: a larger C62 for the accent sweep's shortfall: closes it but slows every short decay
  (score 4.97-5.50 at 1.15-1.45 µF). VR4-B 37.5k shipped instead.
- 2026-10-12, for the notch on accented low notes: the filter's cutoff and resonance (matched), a
  hotter VCO into the ladder, BA662 input overdrive (squashes plain notes 1-3.6 dB), the BA662's
  swing (fits the accents' peaks, not this), IC12-A's on-resistance (2 kΩ takes the accent's
  brightness at full ACCENT from +98 to +20 cents, not the notch, and the score gets worse).
- 2026-10-12: a soft clip, then a hard clip, on the plugins, standing in for acidvoice's recording
  chain: Din Sync's clean recordings flatten their accents the same way, so it is the unit's VCA
  (The VCA's swing, above).
- 2026-10-12: ACCENT drive on the bar (x0.5 to x3 the MEG fed to the ACCENT pot), an experiment,
  removed the same day (320a425).
- 2026-10-31: artwork packs with white shapes left out and stray marks fitted into their slots
  (652c630), reverted the same day (c6d78c5): leaving out white shapes is a guess that breaks
  white-on-black marks. A file per place replaced it (2026-11-01).

## Declined

- 2026-10-08: a second, Onyx-style view beside the hardware panel. The hardware panel is the only
  view. Don't offer again.
- 2026-10-09: a replacement ROM bundled as the default, and a ROM-less panel keyboard: without the
  user's ROM the 303 loads in voice mode. Don't offer again.
- 2026-11-01: for artwork, file choosers, JPG (keying out a background is a guess), a "shape only"
  kind, and artwork saved in sessions or State files: a file per place in the ROM folder, drawn as
  it looks, per computer. Don't offer again.

## Done

- 2026-11-11, accuracy round: a third unit, the RE-303 (32 clips at 16 knob settings). No change
  to the sound: its trims, pot law and envelope fitted within the parts' ranges explain all but
  ENV MOD at mid DECAY (plan item 13); the pot law is the unit's (worse on Din Sync's notes), the
  envelope agrees with Din Sync's fit. The shipped FULL CUT OFF (2.3 kHz) sits beside its 2.0 kHz;
  Din Sync's 1.35 kHz scores far worse on it (6.90 dB against 4.79). Tools: `tools/compare/re303.py`
  (compare, fit, pitch), `tb303render --top HZ` and `--fullcutoff`, `dinsync.py` taking
  `TB303RENDER` from the environment. Check set before and after the same (no sound change):
  `tb303plugintest` and `lemustest_tb303` pass, golden renders in `build/tb303-golden-0`, bench
  6.5-7.4 % of a core per playing workload at 48 kHz (`tb303bench`, this Mac), asleep 0.8 %.
- 2026-11-01: 0.1.5 (1b3e733). Artwork: a file per place (`maker`, `name`, `model`, `tagline`) in
  `artwork/tb303/` in the ROM folder, drawn as it looks; one ROMs pop-up; no blank pack written,
  untouched ones taken out (c200671).
- 2026-10-31: MIDI: an overlapping note of the same pitch slides (ties) rather than going silent; the
  sustain pedal (CC 64) makes every note slide; the firmware's and MIDI's renders of accented slides
  agree to 44 dB (26aa5a2).
- 2026-10-29: 0.1.4 (eb1f4fe): keys held by a right-click named in the status bar; what's new on
  update (8c5f7cf).
- 2026-10-15: 0.1.2 and 0.1.3: a right-click latches a panel key (c34e047); no CLEAR MEMORY in the bar
  (ecf8135); AGPL notices, the publish script, the clean-room note on `docs/firmware.md` and
  `tools/dis650.py` (52b65e7).
- 2026-10-14: 0.1.0 and 0.1.1 (4f9d54b): POWER on VOLUME, as the unit's ganged switch; CUT OFF MAX
  ships at 2.3 kHz rather than the fitted unit's 1.35; SERVICE's boards part by part from the service
  notes' drawing (3bc1fcb); the first-start ROM prompt (0fbffd2).
- 2026-10-13: SERVICE: the boards, the six trimmers (`core/trims.h`), a scope, a counter on the VCO
  and a meter on CV OUT at the notes' check points; TM3 replaces VCF TRIM; trims saved (ec7cecc).
- 2026-10-12, accuracy round: the accent sweep (VR4-B 37.5k): acidvoice's accents had topped out
  1.3-2.3 semitones short, now within −70 to +30 cents (mean error over the first 60 ms 160 → 68
  cents, plain notes 108 → 86); Din Sync's accented notes' first 150 ms 200 → 180 cents; score
  4.885 → 4.890 (419a9d6).
- 2026-10-12, accuracy round: the attack (pin 1 −80 mV): half level 1.7 ms after the sound starts,
  was 7.5 (RMS error over the first 5 ms 4.1 → 1.7 dB), release, accent level and score unchanged;
  the VCA's swing (a fifth of the chip's): Din Sync's accents' peak against RMS 16.5 → 14.4 dB (real
  14.1), spikes 15.1 → 10.4 dB (real 10.9); CUT OFF MAX a slider, 1.2-3 kHz (b7566a9).
- 2026-10-11: ships as its own plugin, ro-mono-303, code R303 (7e229bf).
- 2026-10-10: no factory patterns, as the original; Space goes to the DAW in a plugin (959e651);
  release builds, the model version in sessions, the update check (074551f).
- 2026-10-09, CPU round: the voice 38 % less, the same sound: 17-19 % → 10.5-11.7 % of a core at
  48 kHz playing; worst difference −138.5 dBFS against the previous renders, not growing over 20 s at
  full resonance; the service checks pass (0837561). Sleep when idle: about 0.1 % of a core asleep,
  against 15 % (443f49d).
- 2026-10-09: voice mode without the CPU ROM (d7e1e0d); the tempo clock runs while stopped and RUN
  restarts it in phase (about 1 start in 10 had lost its first step) (9d9a146); `tools/probe`, a
  black-box harness (c3a6b2a).
- 2026-10-08, accuracy round: the unit fitted to Din Sync's 400 notes (TM3 2.5 ms, Q21 β 385, MEG
  9.3 V, DECAY 860k, two-segment pot laws): score 9.76 → 4.89; log-spectral distance 3.0 dB (Roland's
  software 3.3, the unit's retakes 2.4), resonant peak within ~140 cents (360), level within 0.8 dB
  (1.7). The hardware panel the only view (8fc14d8).
- 2026-10-07, accuracy round: every block against ngspice of the transcribed schematic and the BA662
  at transistor level (b7b58f0): accent at half ACCENT +4.1 dB (SPICE +4.1), note start 6.7 ms
  (7.0), the release ~6 → 13.8 ms (SPICE 14.1); gain ranks from the data sheets (7f46628); Q21's stage
  2.5 % less gain (f3a8015); the whole voice within 0.1-0.3 dB in level, 0.2-1.4 dB in harmonics
  (3f6f8a9). Service checks 16 of 16.
- 2026-10-07: the port into Lemus's shared host (6a5b11d).
