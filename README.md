# Lemus

Legacy Electronic MUsic Systems: a MAME-style emulator for studio gear, as AU/VST3 plugins.
One shared host runs many machines (drivers). Each machine ships as a plugin of its own, the host
with that machine alone, named maker, kind, model: the TB-303 is **ro-mono-303**. Downloads, user
guides and measurements against real units are on https://lemusdev.org.

Status: early. The first machine is the Roland TB-303 (ro-mono-303). Others are in development and
join this repository when they're released.

## Building

Needs CMake 3.22+ and JUCE, a submodule (`git submodule update --init`).

    cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build build/cmake -j --target plugin_tb303_All Lemus_All lemustest_instrument lemustest_tb303
    build/cmake/lemustest_instrument_artefacts/Release/lemustest_instrument
    build/cmake/lemustest_tb303_artefacts/Release/lemustest_tb303

`plugin_tb303_All` is ro-mono-303 (AU, VST3, standalone); `Lemus_All` is the development host,
which starts empty and loads a machine from its picker. The AU and VST3 are copied to
`~/Library/Audio/Plug-Ins/` (`-DLEMUS_COPY_PLUGINS=OFF` while a host has them loaded), and
`-DLEMUS_TOOLS=OFF` leaves out the test host and tests. Releases: `tools/release/README.md`.
`auval -v aumu R303 Lmus` checks ro-mono-303's AU.

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
the brand marks on them (logos, wordmarks) come from an optional artwork pack: an SVG named after
the machine (`tb303.svg`), anywhere in the ROM folder or in a zip of the same name. Without one the
panel has no marks. When the folder has no pack, Lemus writes a blank one there to fill in: the
machine's mark slots, commented out, so it draws nothing as it is.

Any SVG works as a pack: traced, redrawn by hand in an editor, or made by someone else. Each mark is
the element whose id is the mark's name (a path, shape, text or group, with any transforms and
curves), in the SVG's own units, which are the panel's millimetres; colours come from the panel. Each
machine's README lists its mark ids and their coordinate frames. If there are several packs, the
first by path is used, and the About box says which.

Packs contain trademarks. They never go in this repo, the project doesn't distribute them or say
where to get them, and it ships no tools or recipes for recreating them.

## Layout

| Path | What |
|---|---|
| `host/` | The host plugins: `LemusProcessor` runs the machine inside the plugin (audio, MIDI, transport, state and editor pass straight through); `SlotParameter` gives hosts 128 fixed parameters (`p001`-`p128`) that take the machine's names, so automation survives; `Roms` finds ROMs and artwork packs; `Artwork` reads packs; `LemusEditor` is the machine picker and the bar above a machine's editor, in `Style`; `Updates` is the update check |
| `machines/<id>/` | One machine each, all of it: `machine.cmake` (registers it), `<id>.cpp` (the driver), `core/` (the emulation, no JUCE), `plugin/` (its processor and editor), `tests/host.cpp` (its checks through the host), `host/` (its test-host sources and scenes), `tools/`, `docs/`, `README.md` |
| `tests/` | Headless checks of the host (`lemustest_instrument`): its own, and each machine's through it from `machines/<id>/tests/host.cpp`, including that audio through the host is bit-identical to the machine on its own |
| `third_party/` | JUCE (submodule), fonts (SIL OFL) |
| `tools/link-private.sh` | Links the private material into each machine folder |
| `tools/release/` | The release builds and installers, and the release checklist |
| `tools/host/` | The test host: `lemus-render` plays scenes through Lemus and other plugins offline (headless, parallel), Lemus AB plays them for A/B listening (`tools/host/README.md`) |
| `website/` | https://lemusdev.org (`website/README.md`) |

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
CLA. Questions, reports and recordings of real units: GitHub Discussions and Issues, or
hello@lemusdev.org.

## Licence and names

AGPL-3.0 (`LICENSE`). JUCE is used under the AGPLv3. The TB-303's CPU emulation is a port of
MAME's uCOM-4 core, BSD 3-Clause (see its files' headers and `tools/release/NOTICES.txt`).

Machine names identify the original equipment and are trademarks of their owners. Lemus is not
affiliated with or endorsed by them.
