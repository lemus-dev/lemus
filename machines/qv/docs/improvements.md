# QuadraVerb: what we know is wrong, and the plan

What the model gets wrong or hasn't checked, as the transcription and the comparisons with the real
units found it (`docs/circuit.md`, `docs/firmware.md`, `docs/research.md`), and what is planned.
"Real" is a QuadraVerb or QuadraVerb Plus on the recordings below unless it says otherwise.

## Known inaccuracies

### Sound

- **One real unit compared so far** (accuracy round 1): a line recording with its dry source
  (`tools/compare/stevie177.sh`), six programs (plates, a small room, a gated reverb), every one
  writing the same dry VCA code (01h). Per octave band, 250 Hz-8 kHz, the output taken apart into
  the model's dry and wet paths (energy fits 11-18 dB under the real envelope): after the round the
  wet against the dry sits within -1.5 and +2.2 dB of the real (it was 1.6 dB low); the dry path's
  spectrum against the real within +-0.6 dB from 250 Hz to 8 kHz. Programs 11, 12 and 16 (long halls,
  rooms) don't separate the two paths in this material. Untested: other dry VCA codes.
- **The 20 kHz module's response.** No values are published (the GT's open question too); the
  decimator stands in (flat to 18.75 kHz, -6 dB at 20.8 kHz). A recording of a flat program with
  its dry source would settle the top octave.
- **The sample-and-holds' window.** Not on the sheet; 32 clocks, the GT's choice (the held step
  undoes the pre-emphasis), so the hold's droop stays: the wet path is -1.1 dB at 10 kHz and -3.3 at
  18 kHz (`qvresponse`). Same as above to settle.
- **The DSP's arithmetic** (accumulator width, rounding, saturation, the 20-bit word): the decode's
  hypothesis, shared with the GT.
- **The dry VCA's gain** is fitted to the one measured unit (`kCemGain` 0.86, -1.3 dB against the
  data sheet's typical unity; core/unit.cpp): placed in the VCA because it is the one part of
  either path without limits on its data sheet. Whether the unit's wet path is louder instead
  (the A/D's divider, the DSP's own gain) the recordings can't tell: the balance is the same.
- **The hiss** is from the parts' data sheets, not measured on a unit.
- **The plates' top octaves decay faster on the recording** (program 19's free tail: 4 kHz -32
  dB/s against the model's -25, 8 kHz -39 against -25; 250 Hz-2 kHz within 0.2-3.3 dB/s). The
  recording is a second lossy encode (Opus, then AAC), which strips quiet high frequencies, so
  this isn't taken as the unit's yet. Settles with a lossless recording or the Freesound IRs.

### Behaviour and features

- **The left output runs a DSP sample behind the right** (the programs read it at instruction 0 and
  the right at 95): as the decode has it; unchecked.
- **The ring modulator** shifts by 0.71 of the displayed SPECTRUM SHIFT, as on the GT (the
  firmware's own timing on this CPU); a recording at a known shift would settle it. The CPU's
  timing does hold elsewhere: the Leslie's rotor rates on programs 38 and 67 match a Plus's within
  0.3 % and 1.3 % (accuracy round 2).
- **The tremolo (program 87) runs at 4.19 Hz; Temecula's Plus at 4.48 Hz** (6.9 % faster; their own
  plugin at 3.67 Hz). Their unit's firmware version isn't known, and 2.01 changed the factory
  programs (the software history): open until a v2.03 unit's recording or the program's rate page
  on theirs settles it.
- **Not emulated:** the keys' force as a continuous amount (a press is light, or hard with Shift);
  the D/A's images above the host's band.

### UX

- **The panel** is the GT's geometry checked against a straight-on photo of a QuadraVerb Plus
  (`docs/panel.json`: keys within 0.2-2.5 mm, knobs and LEDs within 0.7 mm); the print, logos and
  LCD bezel measured on it. The LCD's colour is a judgement from an unpowered photo (the original
  unlit reflective LCD). The rear is +-5 mm.
- **The rear's other jacks** (power, MIDI, the audio jacks) have no tooltip: only the footswitch
  jacks do something on a click, as on the GT.

### Reference data

| Unit | Where | What it has | Known settings |
|---|---|---|---|
| A QuadraVerb Plus (Temecula's reference unit) | `private/qv/reference/recordings/temecula` | 15 factory programs each on guitar and on drums (28 programs in all), 24-bit 44.1 kHz, the vendor's plugin beside each | The programs (00, 02, 06, 12, 14, 21, 22, 30, 33, 35-38, 45, 50, 57, 60, 64, 66, 67, 69-71, 73, 81, 82, 87, 89); no dry source; levels unknown |
| A QuadraVerb, line recorded (YouTube py39bK-rA74) | `private/qv/reference/recordings/youtube` | A drum machine dry, then programs 10, 11, 12, 16, 17, 18, 19, 27 | The programs (captions); dry passes before each; levels unknown |
| Juno-60 through a unit (YouTube 8TIwuxSbMXI) | same | Programs 05, 06, 22, 26, 38, 63, 64 and a dry section | The programs (chapters) |
| An SH-101 through a Plus (YouTube JuYB2NdGcZY) | same | Dry, programs 12, 19, 52, 15 | The programs (chapters) |
| Others | same, README | Walkthroughs, the sampler, resonators, ring modulator, all 90 programs in turn (KeY-N7c1ERw) | Partly |
| Freesound user mansardian's impulse responses | listed in `private/qv/reference/recordings/freesound/README.md` (download needs a login) | 32 IRs: room, plate, chamber, hall at decay 00-80 | Type and decay in the names |

## Budget

- **CPU:** `qvbench`'s busiest workload (`bypass-pluck-48k`, 1,045 Mins/s; the playing workloads
  837-971) under 1,150 Mins/s and 3 % of a core at 48 kHz on an M5 Max. Last measured: 2.7 %
  (`p38-strum-44k`), 1,045 Mins/s (2026-11-27, after CPU round 1). Confirmed 2026-11-27.
  What's left (CPU round 1's profile): the DSP's 96 instructions a sample about a third, the
  80C31's per-cycle timers and serial port a quarter (a bulk advance between events would take
  most of that, bit-exact but a rework of MAME's peripheral code), the input resampler a tenth,
  the analog stages at 4x the rest.
- **Check set baseline:** 2026-11-27, golden renders `build/qv-golden-2`; service checks 9/9 on
  both firmwares; `tools/spice/ac.py` 9/9 (worst 0.035 dB / 0.5 deg to 10 kHz); `qvdspcheck` all
  pass; `qvcorners` 792/792 (v2.03), 396/396 at 48 kHz (v1.07).

## Plan, in order

1. Release blockers: the Windows build (decided 2026-11-27: not yet; at release)

Waits on data: the 20 kHz module and the sample-and-hold window (a flat program with its dry
source); the DSP's number formats (a unit's output at known settings near clipping and at low
level); the dry VCA at other codes (a recording with its dry source on a program at another DIRECT
level); the plates' top-octave decay (a lossless recording, or the Freesound IRs, which need a
login to download).

## Tried and dropped

- 2026-11-27: the analog stages after the D/A at the DSP's rate (as the GT has them), with the
  bilinear transform: the reconstruction filter 3.8 dB and 50 deg off at 18 kHz, its 25 kHz corner
  above the DSP's Nyquist frequency. Fits at that rate (FIR, matched-z, IIR with fitted numerators)
  left 0.2-5 dB in the band or unstable poles. Replaced by running the analog stages at 4x.

## Declined

## Done

- 2026-11-27, play test in Simple DAW (the owner's): passed.
- 2026-11-27, A/B listen: the 7 scenes in `private/qv/reference/scenes/stevie177` (programs 10,
  12, 16, 17, 18, 19, 27, the recording's dry pass through al-fx-quad against the unit): approved
  ("sounds great").
- 2026-11-27, UX round 1: `CONTRIBUTING.md`'s list walked against the GT (the reference this
  panel was built from) with snapshots (the panel, the rear, the key list, the knobs' and keys'
  tooltips). The frame, bar, status line, key list, latches, computer keys and tooltips are
  `host/PanelFrame.h`'s; the plugin load is warm, POWER boots, a session reopens on its edit page
  (`qvplugintest`); the ROM prompt names both firmwares with their sizes, SHA1s and CRCs
  (`lemustest_qv`); the art is measured. Finding fixed: the NAME and MOD keys' tooltips and
  held-key status still said the GT's PRE-AMP and NAME/MOD (`docs/panel.json`'s names). Golden
  renders exact.
- 2026-11-27, CPU round 1, bit-exact against `build/qv-golden-2`: the DSP's program decoded once
  per commit instead of every sample, its loop without the per-instruction I/O tests; the output's
  queue a vector instead of a deque; the 80C31's per-cycle peripherals inlined, and the receive
  line's idle state read once per run (nothing enqueues during one). 974 -> 866 Mins/s
  (p00-pluck), 1,185 -> 1,045 (bypass-pluck), -11 % and -12 %.
- 2026-11-27, accuracy round 2: what the Temecula hardware takes show without a dry source. The
  Leslie's rotor: program 38 7.14 Hz on the Plus, 7.17 Hz in the model; program 67 0.76 and 0.77 Hz
  (the left-right envelope difference's spectrum; the model on noise and on a sine). The ping-pong
  delay (program 06): 755.9 ms between the sides on the Plus, 756.8 ms in the model. The tremolo
  (program 87): 4.48 Hz on the Plus, 4.19 in the model, left open (Known inaccuracies). No model
  change. The A/B scenes made (`private/qv/reference/scenes/stevie177/make_scenes.py`), lined up
  within 0.1 ms by cross-correlation; the plugin now waits for the firmware's start when a host
  renders offline (lemus-render rendered a just-loaded instance in silence), as the SP-12's.
- 2026-11-27, accuracy round 1: the line-recorded unit (YouTube py39bK-rA74, a drum machine's dry
  passes and the unit on factory programs) against the model fed those passes: `qvcompare` (the
  model's output as a whole, its dry path alone and its wet path alone), `tools/compare/compare.py`
  (lined up by cross-correlation, then per octave band real = a^2 dry + w^2 wet in 10 ms energy
  frames), `tools/compare/stevie177.sh`. Before: the wet 1.6 dB low against the dry (median of 29
  band fits over programs 10, 17, 18, 19, 27; quartiles 1.0 and 2.1 dB). The CEM3381's law now its
  data sheet's (20 %/V from 0.1 V, -0.27 dB at code 01h) and its gain the measured unit's (0.86):
  after, within -1.5 and +2.2 dB, median +0.1 dB. The signal display's R38 the parts list's 91K
  (92K on the drawing). Golden renders `build/qv-golden-2` (programs with the dry path on move:
  p15/p88 -32 dB difference, bypass -36 dB; the rest under -100 dB). CPU unchanged.
- 2026-11-27, phases 1-4: research, skeleton (core copied from machines/qvgt), firmware (v2.03 and
  v1.07: boot, factory programs, keys, LEDs, self-diagnostics, the Plus's configurations), the
  analog stages from sheet 1 at 4x against ngspice (5b7babe, d0c22be).
