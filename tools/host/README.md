# Test host

Two programs that play the same **scene** through several plugins: Lemus with any machine, the
vendor's own plugin (Roland's TB-303 and SH-101), any VST3 or AU, or a recording.

- **`lemus-render`** renders scenes offline to WAV: no audio device, no window, faster than real
  time, nothing to hear. Each run is its own process, so any number of sessions can render at once
  (Onyx Tracker, by contrast, is one app that only one session can drive).
- **Lemus AB** (`Lemus AB.app`) plays a scene for listening: every source plays at once, in sync
  and looping, and you hear one at a time.

Build them from the repo root:

    cmake --build build/cmake -j --target lemus-render lemus-ab

## lemus-render

    build/cmake/lemus-render_artefacts/Release/lemus-render SCENE.json [...] [--out DIR] [--only LABEL,...]
    build/cmake/lemus-render_artefacts/Release/lemus-render --params PLUGIN|use:TEMPLATE [--machine ID]

It writes `DIR/<scene>-<label>.wav` per source (32-bit float, the plugin's outputs; the plugin's
latency is taken off the start). `--params` lists a plugin's parameters, names and current values
(and its own programs, if it has any),
e.g. `--params Lemus --machine tb303` or `--params use:tb303.roland`.

The same scene renders bit-identically every run. Checked against the earlier pipelines: Lemus's
TB-303 through it matches `tb303render --unit` exactly, and Roland's TB-303 through it matches
the Onyx Tracker renders of the comparison suite to 0.1 dB of log-spectral distance on average
(up to 1.2 dB on three pattern tests: Roland's plugin reacts slightly to how a host splits its
processing). `machines/tb303/tools/compare/roland_render.py` renders through it by default.

## Lemus AB

    open -n -a "build/cmake/lemus-ab_artefacts/Release/Lemus AB.app" --args "$PWD/machines/tb303/host/scenes/tb303-sweep.json"

(the path must be absolute: an app opened this way starts in `/`), or use Open, or drop a scene on
the window. There's one Lemus AB and one window, whichever session's build opens it. Each folder of
scenes is a test, listed down the left with the machines it tests (from the sources' templates,
`machine` or own plugin name, by their machine's README heading), and a scene opened while it runs
goes to its folder's test, or a new one. Click a test, or press [ and ], to show it. Only the test
shown plays, and each keeps its scene, sources and modes while another is shown. The list comes
back after a restart (`~/Library/Application Support/Lemus AB/tests.json`). Never quit it to open
a scene: the other sessions' tests are in it too.

    open -n -a ".../Lemus AB.app" --args --reload [/abs/PATH]    load the test again (the one shown without PATH)
    open -n -a ".../Lemus AB.app" --args --remove /abs/PATH      take the test off the list

PATH is a scene or its folder; right-clicking a test does the same. Our own plugins (a machine's,
or Lemus) load from a copy of the installed bundle, made again whenever it changes, so a plugin
rebuilt and installed is heard at the next scene load or `--reload`, with no restart (macOS keeps
playing a bundle it has loaded from a path, and replacing it under a running host can crash it).
A launch hands its command line to the running one through `inbox/` there and quits; when it is a
newer build than the one running (another folder's, or this one rebuilt), it takes over: the
running one saves its tests and starts the newer one, which carries on with them. Builds from
before this (2026-11-25) still open a window of their own.

Return plays and stops, Space goes to the next source, 1-9 pick one, B turns blind mode on and off. D turns dual mode on and off: two
sources at once, each in mono, the selected one in the left ear and another in the right (picking
the right one, or Space, swaps them). The other scenes in the same folder
are in the list next to Play, and Up/Down step through them (it keeps playing, and blind mode
reshuffles for each). Match levels (L) measures the sources again after you change one in its
editor (a volume, say); blind mode keeps its order.

- When a scene loads, one loop of each source is rendered offline and each is given the gain that
  brings it to the sources' average level, shown on its button, so A/B is level-matched
  (unless the scene says `"matchLevels": false`).
- Switching crossfades over 5 ms; all sources keep playing, so a switch is instant and in time.
- Each pass starts with new key-downs, as lemus-render's single pass does: every held note is
  released 20 ms before the loop's end (note-ons in those 20 ms are dropped). A note held into
  the next pass would make its first note legato, and a mono synth's envelopes wouldn't start
  again. A restart mid-loop gives the same 20 ms of key-up.
- Blind mode shuffles the sources behind the letters and hides their names until Reveal.
- Edit opens a source's own plugin window, to change it while it plays.
- The spectrum under the sources shows the reference (the scene's first recording, else its first
  source) filled, always, and the source you hear as a grey line over it, each after the level
  match; in blind mode it is hidden until Reveal. Peaks fall at 40 dB/s; stopped, it
  holds. Left/Right stop and step it through the loop a frame (1/30 s) at a time, Shift a 16th:
  each frame is the spectrum there, from the loop rendered for the level match. Hover for a
  frequency and both levels there.
- It makes sound only while playing. `--snapshot` and `--selftest` run in their own instance,
  beside a running one. `--snapshot OUT.png` saves the window after loading and
  quits; `--selftest OUT.wav` plays two loops with switches into a file, without the audio device
  (with `--snapshot` too, the snapshot shows the spectrum at its end).

## Scenes

JSON. Times are in beats at `bpm`; parameter values are the plugin's normalised 0-1 values, or
text the plugin parses ("Square", "440 Hz").

```json
{
  "name": "tb303-sweep",
  "bpm": 125, "bars": 4, "tail": 1, "rate": 48000,
  "params": {"cutoff": 0.1, "resonance": 0.85, "envmod": 0.55, "decay": 0.4, "accent": 0.8},
  "automation": [{"param": "cutoff", "points": [[0, 0.05], [16, 0.9]]}],
  "notes": [{"at": 0, "note": "C-2", "len": 0.12}, {"at": 0.5, "note": "C-3", "len": 0.12, "accent": true}],
  "repeat": 4,
  "sources": [{"use": "tb303.lemus"}, {"use": "tb303.roland"}]
}
```

| Field | |
|---|---|
| `bpm`, `beats` or `bars`, `tail` | tempo, length (4/4), seconds after the end (rendered, not looped) |
| `rate`, `block`, `automationStep` | sample rate (render; Lemus AB uses the device's), block size, how often automation is applied (samples) |
| `notes` | `at`, `len` in beats; `note` as "C-3"/"C#3" (Onyx's names, C-3 = MIDI 48) or a number; `vel`, or `accent: true` for 127 (default 100) |
| `midi` | a MIDI file's notes, its timing turned into beats at the scene's tempo |
| `cc` | MIDI controller changes: `at` in beats, `cc` (1: the mod wheel), `value` 0-127; at the same time, before the notes |
| `programs` | MIDI program changes: `at` in beats, `program` 0-127, `bank` 0-16383 (optional: bank select CC0/CC32 first); at the same time, before the controllers |
| `repeat`, `every` | play the notes N times, `every` beats apart (default: their length in whole bars) |
| `matchLevels` | `false`: Lemus AB plays the sources as rendered instead of level-matching them (for scenes where the level difference is the point); each button shows its level |
| `onsFirst` | at the same time, note-ons before note-offs, as Onyx sends them, so touching notes are legato; by default note-offs go first and a slide needs the notes to overlap |
| `playing` | the transport the plugins see: `false` reports it stopped (a plugin's own sequencer, such as Roland's TR-808's, then stays quiet and only the notes play); default `true` |
| `params`, `automation` | generic names (see below); automation is linear between `[beat, value]` points |
| `input` | an audio file into the plugin's inputs (effects), channel by channel; a mono file into every input |
| `sources` | what plays it (below) |

A **source** is a plugin or a recording:

| Field | |
|---|---|
| `use` | start from a template (below); the fields below override it |
| `label` | names the output file and the A/B button |
| `plugin` | `Lemus`, `Lemus FX`, `Roland/TB-303` (a VST3 in `~/Library/Audio/Plug-Ins` or `/Library/Audio/Plug-Ins`), `au:Name` for an AU, or a path |
| `machine` | for Lemus and Lemus FX: the machine to load (`tb303`, `sh101`, `rat`) |
| `state` | a saved plugin state, applied first: lemus-render/JUCE's host format, Onyx Tracker's (`OV3S`, as in a song's `plugin.state`), or a VST3 component's own |
| `names` | generic parameter name -> the plugin's own (so one scene's `cutoff` reaches "Cut Off Freq" and "VCF CUTOFF") |
| `params` | in the plugin's own names, after the scene's |
| `gainDb` | added to the output |
| `preroll` | seconds the plugin runs before the scene, the transport stopped and no notes, its output dropped: a machine switched on beforehand, past its power-on (the TR-909's muted master) |
| `clip` | `{"level": L, "bias": b}`: a soft clip on a plugin's output, `L * (tanh(x / L + b) - tanh(b))`, standing in for a recording chain that squashed the peaks of a recording it is compared with; with `"hard": true` a hard clip instead (the same with tanh replaced by a limit at ±1), for one that cut them flat |
| `audio` | instead of a plugin: a recording, played from the scene's start |

Templates come from every `machines/<id>/host/sources.json` (`tb303.lemus`, `tb303.roland`:
Roland's TB-303 in the Simple Saw patch, KEYBOARD on, drive and delay off; `sh101.lemus`,
`sh101.roland`); a template's own `state` and `audio` paths are next to its file, a scene's next
to the scene. `--sources FILE` adds a file.

Scenes live with their machine, in `machines/<id>/host/scenes/`; ones built on material that
can't be published (famous patterns, recordings) are in the private folder, e.g.
`private/tb303/reference/scenes/` (the acidvoice.com pattern MIDI files, each at fixed knobs and
with the cutoff rising over 4 bars).
