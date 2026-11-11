# Lemus

Legacy Electronic MUsic Systems: a MAME-style emulator for studio gear, as AU/VST3 plugins.
One shared host runs many machines (drivers). Each machine ships as a plugin of its own, the host
with that machine alone, named maker, kind, model: the TB-303 is **ro-mono-303**, the TR-909
**ro-drums-909**, the TR-808 **ro-drums-808**, the Virus C **ac-digital-vc**. Downloads, user
guides and measurements against real units are on https://lemusdev.org.

Status: early. The machines released so far are the Roland TB-303 (ro-mono-303), the Roland
TR-909 (ro-drums-909), the Roland TR-808 (ro-drums-808) and the Access Virus C (ac-digital-vc). Others are in development and join this repository when they're released.

## Building

Needs CMake 3.22+ and JUCE, a submodule (`git submodule update --init`).

    cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build build/cmake -j --target plugin_tb303_All plugin_tr909_All plugin_tr808_All plugin_virusc_All Lemus_All lemustest_instrument lemustest_tb303 lemustest_tr909 lemustest_tr808 lemustest_virusc
    build/cmake/lemustest_instrument_artefacts/Release/lemustest_instrument
    build/cmake/lemustest_tb303_artefacts/Release/lemustest_tb303
    build/cmake/lemustest_tr909_artefacts/Release/lemustest_tr909
    build/cmake/lemustest_tr808_artefacts/Release/lemustest_tr808
    build/cmake/lemustest_virusc_artefacts/Release/lemustest_virusc

`plugin_tb303_All` is ro-mono-303, `plugin_tr909_All` ro-drums-909, `plugin_tr808_All` ro-drums-808 and `plugin_virusc_All` ac-digital-vc (AU, VST3, standalone); `Lemus_All` is the development host,
which starts empty and loads a machine from its picker. The AU and VST3 are copied to
`~/Library/Audio/Plug-Ins/` (`-DLEMUS_COPY_PLUGINS=OFF` while a host has them loaded), and
`-DLEMUS_TOOLS=OFF` leaves out the test host and tests. Releases: `tools/release/README.md`.
`auval -v aumu R303 Lmus` checks ro-mono-303's AU, `auval -v aumu R909 Lmus` ro-drums-909's,
`auval -v aumu R808 Lmus` ro-drums-808's, `auval -v aumu Avrc Lmus` ac-digital-vc's.

The machines' Python tools run from a virtual environment with pinned libraries (Python 3.14), set
up once from the repo root: `python3.14 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt`.

## ROMs

Lemus ships no ROM data. Machines that need ROMs list them (name, size, SHA1, CRC); you keep your
own dumps in a ROM folder, set in the machine picker or the bar's Add ROM pop-up, by default
`~/Library/Application Support/Lemus/ROMs` (on Windows `%APPDATA%\Lemus\ROMs`). Files are matched
by their SHA1 whatever they're called, loose or in zips, so MAME sets work as they are. A machine
whose ROMs are missing can't be loaded, and a session that uses one keeps its settings until the
ROMs are there. A ROM can be optional: the TB-303 without its CPU ROM is its voice, played from
MIDI, with no sequencer ("voice mode"). The bar above the machine then says so and offers to add
it: put the ROM in the folder, press Rescan, and the machine reloads with it, keeping the session.

## Artwork

Like MAME, Lemus ships no copyrighted artwork. The panels are drawn from our own measurements, and
the brand marks on them (logos, wordmarks) come from the user's own files in the ROM folder.
Without them the panel has no marks.

As MAME keeps artwork, each machine has a folder of it, `artwork/<machine>/` in the ROM folder (or
a zip, `artwork/<machine>.zip`), with an SVG or PNG for each mark's place on the panel, named
plainly (`maker.png`, `model.svg`...). It's drawn as it looks, scaled to fit the place with its
proportions kept and centred; only what it draws counts, and transparent parts show the panel.
Artwork on the bar lists each place's file name and size, opens the folder and reads it again.
Artwork stays in the ROM folder: sessions and state files don't carry it.

The older kind, an artwork pack, still works: one SVG named after the machine (`tb303.svg`),
anywhere in the ROM folder or in a zip of the same name, where each mark is the element whose id is
the mark's name, in the panel's millimetres, filled in the panel's colours. It fills the places that
have no file of their own. Each machine's README lists its places.

Artwork contains trademarks. It never goes in this repo, the project doesn't distribute it or say
where to get it, and it ships no tools or recipes for recreating it.

## Layout

| Path | What |
|---|---|
| `host/` | The host plugins: `LemusProcessor` runs the machine inside the plugin (audio, MIDI, transport, state and editor pass straight through); `SlotParameter` gives hosts 128 fixed parameters (`p001`-`p128`) that take the machine's names, so automation survives; `Roms` finds ROMs and artwork; `Artwork` reads it; `LemusEditor` is the machine picker and the bar above a machine's editor, in `Style`; `PanelFrame.h` is what every machine's panel shares (below); `Updates` is the update check |
| `machines/<id>/` | One machine each, all of it: `machine.cmake` (registers it), `<id>.cpp` (the driver), `core/` (the emulation, no JUCE), `plugin/` (its processor and editor), `tests/host.cpp` (its checks through the host), `host/` (its test-host sources and scenes), `tools/`, `docs/`, `README.md` |
| `tests/` | Headless checks of the host (`lemustest_instrument`): its own, and each machine's through it from `machines/<id>/tests/host.cpp`, including that audio through the host is bit-identical to the machine on its own |
| `third_party/` | JUCE (submodule), fonts (SIL OFL) |
| `tools/link-private.sh` | Links the private material into each machine folder |
| `tools/release/` | The release builds and installers, and the release checklist |
| `tools/host/` | The test host: `lemus-render` plays scenes through Lemus and other plugins offline (headless, parallel), Lemus AB plays them for A/B listening (`tools/host/README.md`) |
| `website/` | https://lemusdev.org (`website/README.md`) |

## Machine panels

A machine's editor is its hardware panel, nothing else, with a bar under it. Everything around the
hardware works the same on every machine, from `host/PanelFrame.h`:

- **The bar**: what the unit doesn't have, as pills from the left in the order SYNC, the
  machine's own settings, REAR, SERVICE (each where the machine has it), then the status line, and
  `?` at the right end. Every pill shows a tooltip. Never a button for something the unit can't
  do itself (no memory clear, no factory trim, no factory-pattern reload): the State pop-up's Reset
  makes the machine a new instance.
- **The status line**: the machine's own state, or `VOICE MODE (no <ROM>): MIDI plays the
  voice(s)` without the program ROM, or the keys held by a right-click
  (`SHIFT held: click it or Esc to release`, in orange).
- **Keys**: a click holds a panel key while the button is down; a right-click (ctrl-click) holds
  it until it's clicked again (a key that acts on the press alone, like RUN/STOP, isn't latched).
  A computer key holds its panel key while it's down; a latched key stays down when its computer
  key comes up. Escape lets go of every key and closes the key list.
- **Computer keys**: `?` shows the key list, Escape, Backspace and Delete are the panel's clear
  key, Space is the run key in the standalone app only (in a DAW it's the DAW's transport).
- **The key list** (`?`): "<MODEL> KEYS", the machine's lines, then the shared lines on the mouse,
  latching and Space. Any click, `?` or Escape closes it.

## Private material

Manuals, schematics, papers, photos, vendor renders, recordings of real units, ROMs and artwork
packs are what the machines are built and checked against, but this repo can't contain them.
They live in a private folder next to the repo (`../private`, or `-DLEMUS_PRIVATE_DIR=...`),
which `tools/link-private.sh` links into each machine folder as `private/` (ignored by git).
Builds don't need it; without it the ROM and pack checks are skipped.

## Plugin names

A machine's plugin is named `<maker>-<kind>-<model>`, all lower case, so the original's own name
and model letters never appear in it; they go in the plugin's description, with "Not affiliated
with <maker>."

- **Maker**: two letters, the first two of the maker's name: ro (Roland), mo (Moog), ko (Korg). When
  two makers would share them, the later one takes the two letters people would guess (mr for
  Marshall, lx for Lexicon).
- **Kind**: a synth by how it makes its sound, mono or poly (analog oscillators) or digital
  (digital oscillators, whatever the filters); another instrument by what it is, drums or sampler;
  an effect by what it does, dist, delay, reverb, comp, eq, mixer.
- **Model**: the model's digits without its letter prefix (TB-303: 303, D-50: 50); a model named
  in words gives its initials (Minimoog Model D: mmd).

A name and its plugin code never change once the plugin ships: sessions find the plugin by them.

## Contributing

Contributions are under the AGPL-3.0, with a `Signed-off-by:` line on each commit; there is no
CLA. New machines: `CONTRIBUTING.md`, and https://lemusdev.org/contribute for who does what and
what it costs. Questions, reports and recordings of real units: the forum,
https://lemus.discourse.group. Bugs can also go to GitHub Issues.

## Licence and names

AGPL-3.0 (`LICENSE`). JUCE is used under the AGPLv3. The TB-303's, TR-808's and TR-909's CPU emulations
are ports of MAME's uCOM-4 and uPD7810 cores, BSD 3-Clause (see their files' headers and
`tools/release/NOTICES.txt`). The Virus C's 80C515 and LCD controller are ports of MAME's mcs51
and hd44780, BSD 3-Clause; its DSP56362 is the dsp56300 emulator, GPL-3.0, with asmjit (Zlib),
combined with the AGPL code under section 13 of both licences (`machines/virusc/README.md`).

Machine names identify the original equipment and are trademarks of their owners. Lemus is not
affiliated with or endorsed by them.
