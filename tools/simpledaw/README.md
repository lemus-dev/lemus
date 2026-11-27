# Simple DAW

An internal test host for Lemus plugins: one Audio Unit at 90 % size with a compact looped sequence above it at the same width,
in Tokyo Night. Native macOS (Swift + AppKit, an Objective-C audio core on AudioToolbox), not
JUCE. It is not part of the public repo.

    ./build.sh            # builds build/Simple DAW.app
    ./build.sh --open     # builds and (re)starts it

## Using it

- **Plugin**: the pill next to the title lists every AU in `~/Library/Audio/Plug-Ins/Components`
  and `/Library/Audio/Plug-Ins/Components` (new ones appear on their own). With nothing loaded,
  the list sits where the plugin goes. Plugin → Open Plugin File… loads a `.component` from
  anywhere (a build folder, say).
- **Effects**: "+ FX" over the plugin box adds an effect after the plugin (up to 8, in order:
  plugin → FX 1 → FX 2 → out). Each has a tab: click it to show its panel, the dot bypasses it,
  × removes it. The box keeps the widest panel's width; its height follows the tab shown.
- **Hot reload**: when the loaded plugin's binary changes (a rebuild dittoed into Components),
  Simple DAW loads the new build within about a second, keeps the plugin's state and carries on
  playing. Effects reload the same way. ⌘R reloads by hand.
- **Sequence**: click to add a note, drag to make it longer; drag a note to move it, its right
  edge to resize it; double-click or right-click deletes. Shift-click adds to the selection,
  Option-drag selects a box, ⌘A all. A toggles accent (velocity 127), arrows move (Shift: an
  octave / a bar), Delete removes, ⌘Z undoes. The keys on the left play the plugin. Notes that
  overlap play legato (a 303 slide); notes that touch don't.
- **Drum mode** (the Keys/Drums pill, ⌘D): one lane per instrument, one cell per grid step. Click
  a cell to set or clear it (drag paints), Option-click or right-click toggles its accent; click
  a lane's name to hear it. It is on by itself for plugins named `*-drums-*`. The Lemus drum
  machines get their own lanes in panel order and the velocities their sequencers use (909: 50
  normal, 80 accent; 808: 90 / 127, accent from 100); other plugins get General MIDI drum notes.
  Both modes edit the same notes.
- **Transport**: Space plays and stops (always from the top, looping). BPM: type it or scroll on
  it. The bar length and grid are the two pills after it.
- Tempo and transport go to the plugin (host sync), MIDI from every connected input goes to it,
  and the meter shows its render time against real time (DSP %, average and peak).
- With an effect loaded, a small built-in saw synth plays the notes into it.

## Files

- The song (default `~/Library/Application Support/Simple DAW/song.json`; File → Open/Save As
  for others) is saved on every edit, and reloaded whenever something else changes it.
- The plugin states sit next to it in `<song>.states.plist`, one per plugin, saved every
  20 seconds, on switching and on quitting.
- `~/Library/Application Support/Simple DAW/status.json`: what is open now (song, plugin,
  version, build time, playing, reloads).
- Each load copies the bundle to `~/Library/Caches/Simple DAW/loads/` (cleared on the next start).

## Song format

The Lemus scene format's fields (a scene's notes open here too). Note names are Onyx's: C-3 =
MIDI 48. `vel` 127 is an accent. `plugin` is a bundle name from Components, or a path to a
`.component`. `effects` lists the effects in order, `[{"plugin": "pr-dist-r", "bypass": false}]`. `mode` is `"drums"` or `"keys"` (left out: drums for `*-drums-*` plugins).

```json
{
  "plugin": "ro-mono-303",
  "bpm": 125,
  "bars": 2,
  "grid": 0.25,
  "notes": [
    {"at": 0, "len": 0.25, "note": "C-2", "vel": 127},
    {"at": 0.5, "len": 0.25, "note": "C-3", "vel": 100}
  ]
}
```

To give Simple DAW a sequence from outside (Claude, a script): write the song file that
`status.json` names and it reloads; or write a new one and `open -a "Simple DAW" file.json`.
Flags when started from a shell: `--play` starts playing, `--background` doesn't take focus.

## How the reload works

macOS can't unload a plugin's code, so each load copies the bundle to a new path, `dlopen`s the
copy, and registers its factory with `AudioComponentRegister` in this process only, under the
installed type/subtype/manufacturer (so saved states still match) and a version above the last
copy's. The new instance gets the old one's `ClassInfo` state, the audio thread swaps to it, and
the old one is disposed half a second later. Old copies stay mapped until quit (a few MB each).

One limit: an exported weak symbol (a function-local static in inline code built with default
visibility) is shared between copies by dyld, so such a static keeps the first copy's value.
