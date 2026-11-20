# ProCo RAT

Component-level emulation of the original ProCo RAT distortion pedal, as the 1984 "White Face"
small box (LM308): machine `rat` in Lemus FX, the effects host. Its circuit is the RAT's from the
1981 big box to the 1988 RAT2, which added the LED; the RAT2's part changes (R12 2M2, R4 2M2, C13
10 µF, R15 10k) are in the tools (`--parts rat2`) for the comparisons with RAT2 recordings. It is built the way the TB-303 was (`../tb303`): research saved, a part-by-part
model of the schematic, and verification against a circuit simulator and against recordings of
real pedals. The RAT has no firmware, so the flow's digital half does not apply.

The whole circuit is one nonlinear system, solved per sample at 8x oversampling with the nodal
DK method and Newton. It covers:
- the input network;
- the LM308 gain stage, through a macromodel whose slew rate and gain-bandwidth both come from
  its 30 pF compensation;
- the DISTORTION pot and the two RC legs;
- the 1N4148 clippers;
- FILTER;
- the 2N5458 source follower;
- VOLUME into the next device's input.

## Artwork

The pedal needs no ROM. Its brand marks are artwork places, filled from your own files when your
ROM folder has them (the host's Artwork pop-up lists them): `artwork/rat/model` (the RAT letters),
`maker` (the Pro Co logo beside them in the white block) and `front` (the Pro Co Sound Inc logo on
the front face), each an SVG or a PNG drawn as it looks, fitted to its box. Without them the white
block is plain. Lemus ships none.

## Building and tools

The machine builds into Lemus FX from the repo root (see the top-level README). Its own tools:

    cmake -S . -B build/rat -DCMAKE_BUILD_TYPE=Release -DLEMUS_COPY_PLUGINS=OFF -DLEMUS_MACHINES=rat
    cmake --build build/rat -j --target ratplugintest ratrender ratgolden ratcorners

The analysis tools use the repo's Python environment (`../../.venv`; set it up from the repo root
with `python3.14 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt`) and ngspice
(`brew install ngspice`). The NAM reference renderer is NeuralAmpModelerCore (MIT), kept with the private material and built there:

    cmake -S private/third_party/NeuralAmpModelerCore -B private/third_party/NeuralAmpModelerCore/build/cmake \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build private/third_party/NeuralAmpModelerCore/build/cmake --target render

## Using it

The controls are the pedal's: DISTORTION, FILTER (clockwise is darker), VOLUME and the footswitch,
which is true bypass. The bar under the pedal has what it doesn't:
- **INPUT** (−12 to +24 dB, default 0): a gain in front of the pedal, to hit it harder or softer
  without reaching for clip gain (in most DAWs the fader comes after the plugin). The output isn't
  made up for it, and the footswitch's bypass skips it. At 0 dB, 0 dBFS is +4 dBu at the pedal's
  input (1.74 V peak), about a hot humbucker or a line-level synth.
- **The parts** are the unit trims fitted to real RATs: the LM308's to a 1990 LM308 RAT2's
  published scope shots and two LM308-era captures, the trims the op amps share to captures of
  RAT2s; DISTORTION's and VOLUME's laws a measured 1979 RAT pot's (10.1 % at half turn), FILTER's
  fitted to a capture of a 1984 White Face (6 %). The tools also have the datasheet's typical
  values (`--cal datasheet`) and any input level (`--level`), for the comparisons.

The 1984 RAT has the LM308 only. The OP07 that RAT2s have used since the mid-1990s, and the
China-made RAT2's parts, stay in the tools (`--opamp op07`, `--parts china`) for the comparisons.

The editor is the 1984 White Face's top, drawn in code from measurements of photos of 1984 units
(`docs/panel.md`): every legend, scale and part placed to about 0.05 mm on the print, the whole to
about 1 %. The pedal has no LED; the bar under it says whether it is on. Drag a knob up
or down (shift for fine), scroll, or double-click to reset. Click the footswitch (or press Space in the standalone app; in a DAW, Space is the DAW's transport).
`?` shows the key list. The bar under the pedal is the one every Lemus machine has (`host/PanelFrame.h`). The window size is saved with the session.

**Idle.** A channel whose input is silent (under −200 dBFS: at full DISTORTION the pedal has
about 70 dB of gain) sleeps once its output has stayed under −120 dB for 100 ms: exact zeros out,
no circuit model (0.7 ms of work for 10 s instead of 6.7 % of a core per channel). Input or a
knob wakes it. Its tail is 1 s: from full-scale input it falls under −90 dB within 0.84 s at any
setting. Some of the circuit's DC settles over minutes, so after a long silence the first ~50 ms
of the next note are as after a 2 s one (−20 dB difference at the attack, gone after a second);
a host that suspends plugins by their tail does the same.

## Layout

| Path | What |
|---|---|
| `rat.cpp` | The driver: ID, name, artwork places, and how Lemus creates the machine |
| `core/dk.*` | Nodal DK solver: netlist, trapezoidal discretisation, compiled matrices, Newton |
| `core/devices.h` | Device equations: LM308 macromodel, 1N4148 pair, 2N5458 (Shichman-Hodges) |
| `core/pedal.*` | The RAT2 netlist, calibrations, knob smoothing, DC operating point, small-signal response, SPICE export |
| `core/unit.*`, `oversampler.*` | One channel: level convention, 8x oversampling, bypass |
| `plugin/` | JUCE processor, editor, `HardwareView` (the pedal and the shared bar), generated `PanelData.h` (the marks' places, no outlines) |
| `tools/ratrender` | Renders WAVs through the model; DC point, response, benchmark, `--netlist` |
| `tools/ratgolden` | Golden renders, the stereo-pair check and a benchmark, for making the model cheaper without changing its sound |
| `tools/corners` | `ratcorners`: the knobs' and the input's extremes, both calibrations and op amps, 44.1 to 192 kHz; fails on any unconverged solve or output out of bounds |
| `tools/compare/` | NAM download (`fetch_nam.py`), suite (`make_suite.py`), NAM renders (`nam_render.py`), comparisons (captures, Aalto, EGFxSet, published scope shots), fits, stability, SPICE checks, A/B |
| `tools/panel/` | Panel measurement from photos (`measure_panel.py`, camera pose and scale) and `gen_panel.py` (panel.json to PanelData.h) |
| `tools/plugintest` | Headless test of the machine's processor |
| `docs/` | `circuit.md` (part-by-part transcription), `research.md`, `model.md`, `panel.md`/`panel.json`, `sources.md` |
| `fonts/` | Nimbus Sans Bold for the legends (URW base 35, AGPL-3.0; `fonts/README.md`) |
| `private/` | Not in the repo; see below |

## Private material

The docs and tools refer to material this repo can't contain: schematics, datasheets, papers, web
pages, photos, the SPICE netlist with vendor models, NAM captures and the Aalto and EGFxSet
recordings (which belong to their authors), measurements digitised from published scope shots, A/B
renders, and the traced outlines of the brand marks. It lives in the Lemus private
folder (`~/src/lemus/private/rat`) at the paths the docs give, and `tools/link-private.sh` links
it in here as `private/`.

## Verification

How each part was checked is in `docs/model.md`. The tools run from this folder (they use
`../../build/rat/ratrender`; set `RATRENDER` to run them on another build):

    ../../.venv/bin/python tools/compare/spice_check.py     # solver vs ngspice on the same equations
    ../../.venv/bin/python tools/compare/spice_vendor.py    # device models vs the vendors' SPICE models
    ../../.venv/bin/python tools/compare/fetch_nam.py       # download the captures (once)
    ../../.venv/bin/python tools/compare/make_suite.py      # signals and specs
    ../../.venv/bin/python tools/compare/nam_render.py      # reference renders through the captures
    ../../.venv/bin/python tools/compare/compare.py --cal captured --opamp op07   # ours vs the captures
    ../../.venv/bin/python tools/compare/fit_nam.py --opamp op07   # fit the shared and OP07 trims
    ../../.venv/bin/python tools/compare/fit_lm308.py --base build/fit/result.json   # then the LM308's
    ../../.venv/bin/python tools/compare/compare_scope.py --cal captured   # a 1990 RAT2's scope shots
    ../../.venv/bin/python tools/compare/fetch_aalto.py     # Aalto recordings of a real RAT (once, 47 MB)
    ../../.venv/bin/python tools/compare/compare_aalto.py --scan --cal captured --opamp op07
    ../../.venv/bin/python tools/compare/fetch_egfx.py      # EGFxSet's RAT2 recordings (once, 0.9 GB)
    ../../.venv/bin/python tools/compare/compare_egfx.py --scan --every 5 --cal captured --opamp op07
    ../../.venv/bin/python tools/compare/stability.py --cal captured --opamp op07   # long runs at the extremes
    ../../.venv/bin/python tools/compare/ab.py              # listening A/B WAVs
    ../../build/rat/ratplugintest_artefacts/Release/ratplugintest build/plugintest

Before changing the solver for speed, write golden renders, then check against them (worst
difference per case; the bar is −120 dB) and that a stereo pair matches each side alone:

    ../../build/rat/ratgolden --write build/golden      # 85 cases: knobs, calibrations, op amps, rates, automation
    ../../build/rat/ratgolden --check build/golden
    ../../build/rat/ratgolden --pairs
    ../../build/rat/ratgolden --bench                   # cost per second of audio
    ../../build/rat/ratcorners                          # the extremes: every solve converged, output bounded

## Status

The 1984 White Face, its circuit checked against ngspice and the vendors' SPICE models and its
sound against recordings of real RATs. The full numbers are in `docs/model.md` and
`docs/improvements.md`.

- **Solver:** matches ngspice on the same circuit to −46 … −86 dB error, and the small-signal
  response to 0.01 dB. No unconverged solve at any extreme of the knobs, input and sample rate
  (`ratcorners`, 636 cases).
- **Bias:** the op amp sits at 4.49 V, as in the vendor-model netlist, and the JFET's source at
  2.68 V, as real 2N5458s in this circuit (2.6–2.9 V).
- **Why it clips asymmetrically:** the op amp's swing about the bias, through the DISTORTION
  network's DC feedback, sets the duty of the clipped wave. An LM308 clips its positive side
  slightly sooner (negative peaks 4.5 % larger on a 1990 RAT2).
- **Real pedals:**
  - The one capture of a real 1984 White Face (one setting, its level not stated; 0 dBu fits
    best): log-spectral distance **6.3 dB**, ESR **−13.2 dB**; level-matched, its band levels within
    0.9 dB above 2 kHz on a guitar DI and within 0.2 dB on a TB-303 line.
  - A 1990 LM308 RAT2's published scope shots: peak ratio within 0.8 %, harmonics within 4.4 dB.
  - Aalto University's recordings of a real RAT (97 clips): 7.1 dB.
  - Captures of six later RAT2s and RATs, with the RAT2's part values: 8.6 dB. Their newer pots
    keep FILTER darker at the same knob position than the 1984 pedal's.
- **Cost:** about 3.7 % of a core per channel at 48 kHz (guitar, defaults; Apple M5 Max); a
  stereo instance with different sides about 3.4 % a channel. It sleeps on silent input. The plugin and host tests pass;
  auval passes.

Known differences and approximations:
- The pots' laws: DISTORTION's and VOLUME's are the one measured vintage RAT pot's (1979, 10.1 % at
  half turn); FILTER's is fitted to the White Face capture (6 %). No 1984 pot has been measured.
- The top end depends on DISTORTION more than on the real RAT2 it was compared with: too dark at
  low DISTORTION, too bright at noon (a few dB above 4 kHz).
- The LM308's slew rate and swing ended at the bounds of their bench ranges; the LM308 captures
  that set them are uncalibrated.
- One 1984 reference: a second one would tell its FILTER pot from its C11 and its knob reading.
- Not emulated: the footswitch click, supply sag from the op amp's current, and pickup loading
  (the source is 1 kΩ).
