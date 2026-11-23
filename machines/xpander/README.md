# Oberheim Xpander

Emulation of the Oberheim Xpander, the six-voice analog synthesizer module (1984): main software
1.2, voice software 1.4, voice board revision D. Plugin `ob-poly-xp`, an instrument. Not affiliated
with Oberheim.

The real firmware runs on both of the unit's 6809 boards: the main processor (panel, displays,
patches, MIDI) and the voice processor (the 54 sample-and-holds, the 8253 counter, the tune).
The six analog voices are circuit models from the service manual's schematic and the chips' data
sheets:
- two CEM3374 oscillators;
- a CEM3372 filter with the Xpander's 15 pole-mixing modes;
- the VCA and the pan section.

The voices run at four times the host's rate (`docs/model.md`). The firmware sets every voice through
its sample-and-holds as on the unit, and its own TUNE measures and corrects the voices
(`docs/firmware.md`).

The first time it runs on a computer, the unit does what a new Xpander did. It resets its memory
(RESET MEMORY) and tunes itself (TUNE ALL), which takes about 20 s of CPU in the background; the bar
says TUNING meanwhile. Then, with the factory patches in the ROM folder, it receives them through
MIDI IN and starts on single patch 00. The result is cached in Lemus's application folder, and the
battery RAM is saved with the session.

The known gaps and the plan are in `docs/improvements.md`.

## ROMs

The six EPROMs are needed; the factory patches are optional. None of them is included.

| File | Size | SHA1 | What |
|---|---|---|---|
| `exp1.2-0.u8` | 8 KB | `4064edc2fa0bea62684c28e8e004feb8229604fe` | main ROM 0 (software 1.2) |
| `exp1.2-1.u7` | 8 KB | `233efc3da3a96777b10a4d6fe7e80864e9231f04` | main ROM 1 |
| `exp1.2-2.u6` | 8 KB | `798c1b28fb59819baadbf0142ecd1348e759fa70` | main ROM 2 |
| `exp1.2-3.u5` | 8 KB | `5897ad0cd66cf37b8e9b277654aab4eb83d1c8ab` | main ROM 3 |
| `ca1.4-0.u912` | 8 KB | `5ab13ccc3b75df313a447520ca54a492102b7e3b` | voice ROM 0 (software 1.4) |
| `ca1.4-1.u914` | 8 KB | `951bbc197a0140a93bb7621a7aedbe0f4037990c` | voice ROM 1 |
| `FACTORY1.SYX` | 39,900 B | `b8abde65cff74424438fd371eb2261c2e70ccaa8` | optional: Oberheim's 100 factory single patches as a SysEx dump. Without it the single patches start blank. |

The six EPROMs are MAME's `xpander` set, with MAME's names. The firmware is Oberheim's copyrighted
work. It is everything the unit does: the panel, the patches, the modulation, MIDI and the tune.

## Artwork

The panel draws no brand marks of its own. Two places take the user's files (SVG or PNG), either in
`artwork/xpander/` in the ROM folder or named `ob-poly-xp-<place>` anywhere in it:
- `maker`: the Oberheim logo, 59.2 x 10.5 mm;
- `name`: the Xpander logo, 78.8 x 20.3 mm.

Both sit at the lower left of the PROGRAMMER section, and the rear panel repeats them from the same
files. The rear's model label is drawn without the lines that name the product and the maker.

## Code from other projects

- `core/m6809/`: MAME's MC6809 core (BSD-3-Clause, Nathan Woods), with its notice. The opcode
  switch is generated from MAME's `m6809.lst` by `tools/port_m6809.py`; `m6809.h` lists the changes.
- `core/blep.*` and the decimator follow `machines/proone`, which is this project's own code.
- The panel's print is Nimbus Sans Narrow (URW++, AGPL-3.0, `fonts/README.md`).

## Status

Checked five ways; the numbers are in `docs/improvements.md`.

- **The service manual's checks** (`tools/service/check.py`): 9 of 9 pass.
  - VERSION shows main 1.2 and voice 1.4.
  - MEM and VMEM pass.
  - LEDS and DISP: every LED lights, and all 1,920 display segments.
  - The 4 V reference is 3.990 V (3.920-4.080).
  - The DAC offset is within 1 mV.
  - DAC MONO gives no beep in 10 s.
  - TUNE ALL passes for VCOS, PW, RES, VCF and VCA.
- **The circuit against ngspice** (`tools/spice/suite.py`): 88 cases pass.
  - The CEM3372 alone, and the CEM3374 alone.
  - All 16 pole-mixing settings, within 0.07 dB.
  - The filter's oscillation, within 0.02 cents.
- **The controls' extremes** (`xpandercorners`): 1,238 cases at 44.1 and 48 kHz pass.
  - Through the firmware: all 15 filter modes at FREQ 127 and RES 63, the sweeps, both FMs at 63,
    the VCOs at the top with sync, MIDI 0-127, a chord, the 100 factory patches.
  - One voice with its CVs over the DAC's whole range.
  - No solve fails to converge, and no noise appears where nothing makes it.
- **Real units:**
  - *Xpander notes, pitch:* four sets of single Xpander notes (Modular Samples on freesound, CC0,
    MIDI 36-96). The model is within about 0.2 cents of the units' median pitch in each octave from
    33 to 1047 Hz. Its note-to-note scatter is 0.6 cents; the units' is 0.5-1.0.
  - *Xpander notes, levels:* the voices' levels differ as on one of these units (-0.41 to +0.42 dB
    by voice).
  - *Xpander recordings* (from YouTube): all 100 factory patches against the factory demo, the
    patch-to-patch levels within 1.6 dB rms; factory patches on a dry unit, their envelopes' median
    difference within 1 dB; a filter envelope's delay lands where the unit's does (APOLLO's pluck
    about 60 ms after the strike in both).
  - *Matrix-12 recordings* (the same voice boards, from YouTube):
    - the triangle and the pulse are within 1 dB to the 19th harmonic, and the triangle's even
      harmonics with them (-53/-60/-64 dB at C3 against -53/-59/-64: the core's charge and discharge
      currents differ by 0.3 %);
    - the pulse's duty cycle moves with pitch as on the unit (even harmonics at C1-C4 -31 to -48 dB,
      the unit's -32 to -55);
    - the noise generator is within 1.5 dB from 25 Hz to 13 kHz;
    - the 15 filter modes' shapes on a hand-swept saw are within 0.6-1.2 dB rms, and 13 of the 15
      levels within 1.5 dB;
    - the same modes self-oscillate at RES 63 (the 4-pole-loop ones only), and the oscillation's
      level ripples as on the unit (0.15 against 0.16);
    - the phaser modes' low end within 1 dB of the unit's (VCF OUT's gain fitted to them).
  - *By ear:* side by side with 31 clips of these recordings, played level-matched, an A/B passed.
- **CPU** (`xpanderbench`, at 48 kHz on an M5 Max; budget 29 %):
  - six voices held: 23.6 % of a core;
  - one note: 6.6 %;
  - a sequence: 9.1 %;
  - idle: 3.4 % (every voice asleep, both CPUs running).

What works:
- The firmware boots and runs as on the unit:
  - the panel, the three displays and every LED;
  - single and multi patches;
  - the modulation matrix, with its LFOs, envelopes, ramps, tracking generators and lags;
  - FM;
  - the 15 filter modes;
  - MIDI in and out, SysEx included;
  - the service pages (VERSION, MEM, VMEM, LEDS, DISP, the DAC checks).
- TUNE, both its pages and TUNE ALL, against the model: the corrections are stored in the battery
  RAM as on the unit.
- MEMORY PROTECT on the rear: with it on, RAM 0 and 1 (U2, U3) can't be written, as on the unit.
- POWER on the rear: off, it is silent with the RAM kept; on, it boots from that RAM.
- The six VOICE outputs as extra plugin outputs.
- Sleep: a voice whose VCA is shut and whose output has stayed under -120 dB (re 10 V) for 100 ms
  stops its oscillators and filter. With every voice asleep the outputs are exact zeros. A rising
  VCA wakes the voice.

### Known differences

- **No recording of an Xpander or a Matrix-12 has full stated settings or comes from a voice out.**
  The filter-mode clips also carry the coding noise of their upload, 30-50 dB above the voice's own
  floor in the high-pass modes.
- **RES 62 to 63:** with the cells' tanh, the filter's oscillation at RES 63 sits about 265 cents
  below the cells' small-signal frequency. The firmware tunes the filter on that oscillation, so
  from RES 62 to 63 the pitch drops by about 2.6 semitones. Whether the real chip drops that much
  isn't known.
- **Fitted on one unit:** VCF OUT's gain (x88 against the data sheet's x100, from one Matrix-12's
  phaser-mode clips; a factory-demo patch agrees); the input VCAs' taper above 2.7 V (two units),
  extrapolated below.
- **Chosen values, with no source to pin them:**
  - the resonance VCA's CV law (linear);
  - the output level (0 dBFS = 10 V at the outputs; one voice of the default patch peaks near
    -29 dBFS);
  - VOLUME's taper.
- **Fitted to pitch:** the CEM3374's comparator delay (0.5 us) and its converter's bulk resistance
  (10 ohm). Above C8, where there is no data, the pitch sits about +1.9 cents between the tune's
  points.
- **Sample-rate lock:** in hard self-oscillation there is a narrow lock at exactly a 1/18th of the
  rate (about 8 cents wide), clear of the tune's points.
- **The six voices are alike** but for their levels. Their oscillators and filters have no spread
  (all share the same 0.3 % triangle asymmetry), so one factory tune serves all six.
- **The sample-and-holds:** the DAC's fine sample and the reference settle at once, and FAST drives
  its capacitor for the whole window, where the schematic gates it for about half.
- **The panel** is measured from photos (`docs/panel.md`, about 0.3 mm on the top). It is printed in
  Nimbus Sans Narrow, standing in for Helvetica Condensed.

### Not emulated

- The cassette interface.
- CV 1-6, GATE 1-6, TRIGGER, CHAIN ADVANCE and the two pedals: their jacks read as unplugged (an
  unplugged pedal reads full, as on the unit). The unit's own MIDI CONTROLS page (MASTER > MIDI >
  CTRLS) takes PEDAL 1 and 2 from MIDI controllers instead, with MIDI > ENABLES > CONTROL on.
- Temperature and drift.
- Voice boards before revision D.
- Factory multi patches: none has been found, so the multi patches are as RESET MEMORY leaves them.

## Tools

| Tool | What it does |
|---|---|
| `xpanderfw ROMDIR [seconds] [script...]` | Both CPUs headless from power-on, without the voices: the displays, the LEDs, the voice board's activity |
| `xpanderrender ROMDIR OUT.wav [--rate R] [--ram FILE] STEP...` | A scripted performance from power-on (panel presses, encoders, MIDI, `patch:NN`, `saveram:FILE`) to a WAV |
| `xpanderplugintest [--panel DIR \| --state RAM OUT]` | The processor: the first start, MIDI in tune, the panel, the session; PNGs of the panel and the rear |
| `xpanderbench time [N] \| golden DIR \| check DIR \| list \| run NAME` | The workloads: cost per workload, golden renders, the difference from them |
| `xpandercorners [--rate HZ] [--only TEXT] [--list] [--wav DIR] [--show]` | The controls' extremes: fails on an unconverged solve, on noise where nothing makes it, on output out of bounds |
| `xpandercircuitrun ...` | The model's blocks alone, for the SPICE suite |
| `tools/spice/suite.py` | Every block against ngspice |
| `tools/service/check.py [BUILD]` | The service manual's checks (SM p20-36) |
| `tools/compare/notes.py` | Pitch per note against the freesound Xpander notes |
| `tools/port_m6809.py` | Regenerates the 6809's opcode switch from MAME |

Build: `cmake -S . -B build/xpander -DLEMUS_MACHINES=xpander && cmake --build build/xpander --target xpanderfw xpanderrender xpanderbench xpanderplugintest xpandercorners plugin_xpander_All`.

## Keys

The panel's keys work by mouse, or from the computer:

| Key | Does |
|---|---|
| 0-9, +, - | the keypad (= is + too; the number pad works) |
| Return | STORE |
| Backspace, Delete | CLEAR |
| S, M | SINGLE PATCH, MULTI PATCH |
| T, Y | TUNE PAGE, MASTER PAGE |
| P | PAGE 2 |
| V, B, F, K | the pages VCO1, VCO2, FM/LAG, TRACK X |
| C, E, L, R, I | the pages VCF/VCA, ENV X, LFO X, RAMP X, MISC. |

The six encoders turn in detents: drag them up or down, or use the wheel (with Shift, slower).
VOLUME is a knob. `?` lists the keys. REAR shows the rear panel, where POWER and MEMORY PROTECT are
switched by clicking them.
