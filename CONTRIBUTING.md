# Contributing

Contributions are under the AGPL-3.0 (`LICENSE`), with a `Signed-off-by:` line on each commit
(`git commit -s`), which certifies the [Developer Certificate of Origin](https://developercertificate.org).
There is no CLA. Bugs: the forum, https://lemus.discourse.group, or GitHub Issues. A fix is a pull
request.

Most contributions are new machines. https://lemusdev.org/contribute says who does what, what it
costs and how to start. This file is the method, written to be followed by a person or a coding
agent.

## A machine, start to finish

1. **Build, once:** phases 0-5, ending at a first playable machine.
2. **Rounds, as many as it takes:** each is either accuracy or CPU, and each runs the same check
   set before and after.
3. **Ship gate:** a threshold, not "everything done".

The person behind the work signs off at phase 0, at phase 5, on each round's pick and result, and
on the pull request. The maintainers agree the target unit and the plugin name at phase 0, and
review and listen at the ship gate.

The machines in this repo are the examples: `machines/tr909` (firmware on an emulated CPU, every
analog circuit run from its netlist, sample ROMs) and `machines/tb303` (firmware plus one voice).
Copy from the closest one. Other kinds work the same way: firmware plus digital chips (an FM synth,
a sampler, a reverb), or circuit only (an analog synth, an effect).

## Rules

- **A machine is its folder.** Everything for machine `<id>` is in `machines/<id>/`, and it
  registers itself (`machine.cmake`). Adding a machine changes nothing outside its folder. A
  change to shared files (`CMakeLists.txt`, `host/`, `tests/`, `tools/`, `README.md`) is a pull
  request of its own, about that change.
- **Nothing copyrighted in the repo:** no ROMs, manuals, schematics, data sheets, photos,
  recordings, vendor renders, logos, or copper traced from photos. They live in the private folder
  (below). The repo has paths to them, constants with their sources, and text written here.
- **Licences.** MAME's CPU cores are BSD-3-Clause: port them with their notice. GPL projects are
  oracles by default: run as separate programs and compared against; their source is not read for
  this code. A machine may use AGPL-compatible GPL code instead where its README says so and names
  what it took. Its accuracy claims then cover only what can still be checked independently. Code
  under any other licence: ask on the claim issue first.
- **Names.** The plugin is named `<maker>-<kind>-<model>` (README, "Plugin names"). The original's
  name goes only in descriptions, with "Not affiliated with <maker>."
- **Nothing from memory.** Every value in the docs names its source: a page, a figure, a file and
  its SHA1, a measurement.
- **Free reference data.** Recordings people have published, the vendor's own demos, owners'
  recordings given for the purpose. Never bought sample sets or recordings with terms that bar
  this use. Say where each came from.
- **Realism first.** Model what the hardware does and state what it costs. The hardware panel is
  the machine's only view; settings the unit doesn't have go in the bar under it (README, "Machine
  panels"). No controls the unit lacks: no memory clear, no factory reload, no reset.
- **Measure per event** (each note, hit, accent), in absolute units (dB, cents, ms), against each
  real unit. Averages hide gaps. When the person hears a difference the numbers miss, the
  measurement is too coarse.
- **Never trade audible fidelity for CPU** (pitch, highs, timing) without the person's yes.

## The private folder

`../private/` beside the clone (or `-DLEMUS_PRIVATE_DIR=...`), linked into each
`machines/<id>/private` by `tools/link-private.sh` and ignored by git:

- `private/<id>/docs/`: service and owner's manuals (PDF and OCR text), schematic renders and
  crops, data sheets, photos, papers, forum threads as text.
- `private/<id>/reference/`: what the model is checked against: recordings of real units (a
  folder per unit with a README: whose, what, knob settings if known), captures, vendor renders,
  MAME's source for the CPU core.
- `private/roms/<id>/`: the ROM images.

Builds don't need it; without it, the ROM checks and comparisons are skipped.

## Build

### 0. Choose (gate)

A machine can be emulated to the standard of the ones in this repo when four inputs exist:

| Input | Where it comes from | If it's missing |
|---|---|---|
| Circuit | Service manual or schematic, data sheets, published analyses | Out of reach, unless a unit can be traced from photos |
| Firmware (if it has a CPU) | A ROM dump (MAME set, archive.org) and a CPU core (MAME, BSD-3) | A mask ROM nobody has read ends it |
| Custom chips | A data sheet, a community decode, or one of our own from the firmware | A chip known only from a decap caps it at behaviour |
| Reference | Free recordings of real units (several, to see the spread), captures, an open bit-exact emulator as an oracle, SPICE of the schematic | Checked against specs only |

Tiers: **1** everything public; **2** one input is our own work; **3** behaviour only; **4** out of
reach.

Decide, before any code:
- the tier;
- the **target unit**: the revision and serial range the model follows; other revisions can be
  switches later;
- the **plugin name**, `<maker>-<kind>-<model>`; the maintainers assign its four-character plugin
  code. Names and codes never change once shipped;
- the free reference recordings found.

Write it as an issue on lemus-dev/lemus titled `Machine: <Maker> <Model>`, after checking there
isn't one already. **Stop until the person and the maintainers OK the target and the name.**

For digital gear an open bit-exact emulator is the better reference. For analog gear the target is
inside the spread of real units, not a null against one.

### 1. Research

Every source is downloaded and kept in the private folder.

- `machines/<id>/docs/sources.md`: every file with its URL and SHA1.
- `machines/<id>/docs/research.md`: a section per block (facts, models found, disagreements,
  recommendation), a table of constants, the open questions.

Done when `research.md` has a recommendation for every block and the open questions are listed.

### 2. Skeleton

```
machines/<id>/
  <id>.cpp            driver: id, name, ROM specs (name, size, SHA1, CRC), artwork places
  machine.cmake       lemus_machine(<id> instrument|fx), lemus_plugin(<id> NAME ... CODE ...)
  README.md           ROMs, artwork places, use, keys, status with numbers, known differences
  core/               the emulation, no JUCE: CPU core, board, netlists, solver
  plugin/             processor, editor, hardware view, all in the machine's namespace
  tests/host.cpp      LEMUS_HOST_TEST(<id>): slots, audio bit-identical to the machine alone, sessions
  host/               sources.json and scenes/ for lemus-render and Lemus AB
  docs/               sources.md, research.md, circuit.md (or circuit/), firmware.md, model.md,
                      panel.md and panel.json, improvements.md
  tools/              <id>render, <id>seq, <id>bench, plugintest/, spice/, service/, compare/
  private ->          ../../../private/<id> (tools/link-private.sh)
```

Built in from the start:
- **Sleep:** exact zeros when settled, instant wake, a measured tail (`getTailLengthSeconds`).
  Instruments sleep when the output stays under -120 dB for 100 ms with the slow states settled;
  effects when the input is under -200 dBFS. Modelled hiss may run on while the circuit sleeps.
- **Model version:** `kModelVersion` saved in the state, so old sessions keep their sound.
- **Artwork places:** `ArtworkSpec::places`, one per printed mark, with plain file names; the
  processor an `ArtworkUser`; marks drawn as the user's file looks (`machines/tb303`). No brand
  marks in the repo, drawn, built in or traced.
- **The plugin:** `lemus_plugin(...)` with the agreed name and code; STANDALONE only for a machine
  with its own sequencer, MIDI_OUT where the unit has MIDI out.
- **ROMs:** supplied by the user, found by SHA1. Without its CPU ROM a machine loads in voice mode
  (plays from MIDI, panel keys inert), as the 303 does.

Build in a directory of your own: `cmake -S . -B build/<id> -DLEMUS_MACHINES=<id>`.

### 3. Firmware (machines with a CPU)

- Port MAME's CPU core (BSD-3, notice kept) as a standalone class; keep MAME's source in the
  private folder. `machines/tr909/tools/port_upd7810.py` is an example.
- The machine wraps the CPU with the board: RAM (battery-backed RAM is plugin state), key matrix,
  LEDs, sync inputs, output latches.
- Map the I/O by running it: a scripted driver (`<id>seq`) presses keys, feeds clocks and logs every
  port and LED change; check it against a disassembler. Don't trust MAME's driver blindly (on the
  303, PI0 was inverted and the DAC needed note = code + 13).
- Custom chips: decode from the firmware or a community decode; keep word widths and rounding as
  the chip has them; check against the oracle bit for bit where one exists.

Done when the firmware boots, the panel works and every output behaves as the owner's manual says.

### 4. Circuit

- Transcribe to `docs/circuit.md` (or `docs/circuit/<block>.md`) part by part: designators, values,
  page and crop references. Flag every drawing error with its evidence.
- Netlists (`core/netlists/*.cir`) on the solver in `machines/tr909/core/circuit.*` (MOSFETs,
  dielectric absorption, leakage). Real voltages.
- Nonlinear nodes inside a loop gain above 1 are solved implicitly (Newton); explicit updates of
  them blow up. Keep a NaN guard; log when the DC operating point fails to converge.
- Check each netlist against ngspice (the machine's `tools/spice/suite.py`, a deck exported from our own
  netlist) before any fitting. It is the cheapest test the solver has.
- Part models from data sheets. No fitting beyond data-sheet ranges and part tolerances; anything
  that needs more is flagged as out of tolerance, with its evidence.
- Run each block at the rate its circuit needs, checked against ngspice at a high rate.
  Oscillators band-limited (BLEP, BLAMP). Check the top octave per band, not only the overall level:
  a backward-Euler op-amp pole cost the 909 most of its 15-20 kHz against ngspice.
- **Calibrate by the service manual:** every check it gives (voltages, tuning, levels, times) is a
  case in the machine's `tools/service/check.py`; trims are set by a script beside it and kept in
  a `Calibration` struct.

Done when each block matches ngspice and every service-manual check passes.

### 5. First playable (gate)

- **Panel:** `docs/panel.json` measured from a straight-on photo (dimensions, palette, fonts, every
  legend and control), drawn in code by the hardware view, with the bar under it from
  `host/PanelFrame.h` (README, "Machine panels"). The rear panel where it has working jacks or
  switches.
- **Keys:** a computer-keyboard map that `?` shows; Space only in the standalone app.
- **Host:** MIDI plays it; host sync off by default; state saves the RAM and the parameters; CC
  numbers from the maker's own later gear where they exist; multi-outs where the unit has them.
- **Power:** POWER on and off as the circuit does them. Renders start at power-on, so tools and
  scenes pre-roll past it.
- **The check set** (below) exists and passes.
- **A first comparison** against every real unit there are recordings of, per event.
- Installed; `auval` passes.
- **`docs/improvements.md`**, in the shape of `machines/tr909/docs/improvements.md`: known gaps
  (sound, behaviour, not emulated), the reference units, the CPU budget (a named bench workload
  and a share of a core at 48 kHz), the plan in order.

Open a draft pull request. The person plays it and picks the first round.

## The check set

Every round runs all of it, before and after. Tool paths are the machine's (`machines/<id>/tools/`).

| Check | What it covers |
|---|---|
| `<id>plugintest` | MIDI, sync, random-settings stress, long runs, state round trip, sleep (exact zeros, wakes, sounds the same after), power, every calibration, the panel (`--panel`) |
| `tests/host.cpp` (`lemustest_instrument`, `lemustest_fx`) | slots, audio bit-identical to the machine alone, sessions |
| `<id>bench time` | per workload (idle, one note or hit, busy, everything, automation): CPU % at 44.1 and 48 kHz, worst block, time asleep |
| `<id>bench golden DIR`, `check DIR` | renders of every workload, and the largest difference in dB against them |
| `tools/spice/suite.py` | every netlist against ngspice |
| `tools/service/check.py` | the service manual's checks |
| `tools/compare/` | per unit, per event, against the real recordings: levels in dB, pitch in cents, times in ms |
| A/B scenes | `host/scenes/` (synthetic, public); scenes of the real recordings against the model, sample-aligned, in the private folder; played in Lemus AB, rendered by `lemus-render` (`tools/host/README.md`) |
| `auval -v aumu\|aumf <CODE> Lmus` | the AU as installed |

Digital machines replace the SPICE suite with an oracle replay (bit-exact cases). Effects use
captures with known knob positions and input levels.

## Rounds

One kind per round, accuracy or CPU. Each moves the other, so both are measured every time.

1. **Pick** items from improvements.md's plan. Skip anything under "Declined".
2. **Baseline:** the whole check set; keep the numbers and the golden renders.
3. **Change.**
4. **Recheck the whole set.** An accuracy round states its CPU cost. A CPU round is bit-exact
   against the golden renders, or shows the sound difference (dB, cents, Hz, per workload) and asks.
5. **Listen:** install it; if the sound moved, say what to listen for and in which A/B scenes.
6. **Log:** a dated Done entry in improvements.md with before and after numbers; what didn't work
   under "Tried and dropped", what the person turned down under "Declined".

improvements.md is the one tracker, and it is public: it names no one ("decided against,
2026-10-22").

**Accuracy rounds**
- Several units before calling a gap: a difference one unit shows and others don't is spread.
- Check the measurement before the model: a narrow analysis window can invent a pitch or a
  spectral deficit.
- A fit that pins a part at its tolerance bound is reporting missing physics: fit the constrained
  alternative too, compare on held-out data, write down which one shipped and why.
- Stop when what's left is spread or needs data nobody has; mark it "waits on data", with what
  would settle it.

**CPU rounds**
- Count instructions, not just cycles: instructions are steady (±0.2 %), cycles noisy (±3 %).
- Bit-exact first. A rewrite that moves rounding (fused multiply-adds, reordered sums) is accepted
  only after showing the difference.
- Threads: independent voices or partitions as jobs, bit-identical to one thread. More workers can
  cost more (the 909 went from six to two, 14 % less), because the heaviest job sets the block.
- Stop when fidelity-neutral trimming is used up; write down what's left and its size.

## Ship gate

- Every service-manual check passes.
- The CPU budget in improvements.md is met.
- No open item marked as a release blocker.
- The machine's README has its numbers, its known differences and what isn't emulated.
  "Compared with <maker>'s software" is a section apart from "validated against real units".
  It names who built it, by name or handle; the machine's page on lemusdev.org says the same.
- The pull request has only `machines/<id>/`, every commit signed off.

Then mark the pull request ready. The maintainers review it, listen, and do the release
(installers, the machine's page on lemusdev.org, the announcement). After release, a round that
changes the sound bumps `kModelVersion` and keeps the old behaviour for old sessions.

## Lessons

- Short renders miss slow instabilities: put stability workloads in fits and plugin tests.
- Align against captures in a narrow window (±12 samples); a wide cross-correlation locks onto the
  wrong period of a steady wave.
- The development hosts link every machine into one binary: namespace every class and helper.
- Defaults matter: sync on by default made the 303 play whenever the host did.
- `LemusProcessor` calls the machine's `processBlock` under its own lock, so a machine that
  rebuilds DSP state from the message thread must lock or check suspend in its own `processBlock`.
  Only a paced threaded test or ASan shows it.
- Never copy over a plugin binary a host has loaded. Install the machine's own bundles
  (`ditto`, then `killall -9 AudioComponentRegistrar` and `auval`).
- Swapping source files and building in the same second can leave stale objects: touch them.
- JUCE: `setSize` last in the editor's constructor.
- Python from `.venv` (README, "Building"); process pools, not thread pools, from a script with a
  `__main__` guard.
