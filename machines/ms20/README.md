# Korg MS-20 (ko-mono-20)

An emulation of the Korg MS-20 (1978). By default it's the Rev 1, with two KORG35 filter modules and
the boards KLM-127, KLM-128 and KLM-129 as the service manual draws them. SERVICE on the bar
switches it to the Rev 2, the later KLM-307 filter board with LM13600 chips and its own VCA. Not
affiliated with Korg.

## Status

We think it's really close. It passes all 46 of the service manual's checks, and on what we could
measure from recordings of about twenty real units it sits with them. The gaps we know about are
under "Known differences" below, and `docs/improvements.md` has the details.

- **The service manual's checks** run in `ms20service` and all 46 pass, the Rev 2's filter trim
  included. VCO 1's tracking is split the way a technician would trim it, -1.1 to +1.1 cents from
  C-1 to C-4, because the strict procedure leaves C-3 0.2 cents past its limit.
- **Every netlist** matches ngspice running the same circuit within 1 % (78 cases).
- **Against real units** we only have free recordings, since there's no unit here. Ours matches
  them on the oscillators' waveforms and levels, the filters' sweeps and resonance, the FM depth,
  the MG's rates, the noise and the envelopes, and there are 42 A/B scenes for listening. A stock
  Rev 1 screaming its filter up sounds very much the same as ours on the same patch.
- **The Rev 2** is fitted to the one recording we have of a Rev 2 on both filters, played next to a
  Rev 1. Its limiter diodes, two resonance trims and VCA drive come from that recording (they're
  not printed or have no procedure), and with them its scream matches within 2 dB band by band.
- **CPU.** We measure at 48 kHz on an M-series Mac (`ms20bench`). A held note takes about 26 % of a
  core over both threads, and the heaviest workload, everything at once with both filters
  self-oscillating, about 50 %. The Rev 2 is about the same at 48 %. The worst 64-sample block
  finishes inside its own time (0.89-0.94 of it), and the control side runs on a second thread.

### Known differences

- **The LPF's self-oscillation over the oscillators** gets pulled onto a harmonic and wobbles with
  vibrato, where one unit's runs free between the harmonics. Another unit gets pulled as ours does,
  so it may be that unit.
- **No device noise in the filters.** A self-oscillating filter's tone comes out cleaner than the
  real one's, which has a noise skirt around it.
- **A screaming LPF sweep** starts 2-4 dB brighter than a real one above 2 kHz while the cutoff is
  low, and ours needs PEAK 9.9 to match one unit at 9.5.
- **Values we took against the drawings.** R161 is 33k where it's printed 1K, because one unit's
  envelope sweeps all want that. R169 is 4.3k where it's printed 43K, which would cap SUSTAIN at
  13 %. R62 100K is on real boards but missing from the diagrams. On the Rev 2, R38 is 4.7k (printed
  3.3K) and the VCA runs on about a fifth of the drawn base current, so its level matches the Rev 1.
- **CUTOFF's range** follows the drawing, about 11.5 octaves, where the specification says 50 Hz to
  5 or 15 kHz. The knob readings in recordings agree with the drawing.
- **SIGNAL OUT** reaches 0.95 Vpp with both filters open, where the block diagram says 2 Vpp.
- **Noise** at the jack is 2.1 Vpp against the specification's 5, and there's no 1/f or burst noise.
- **EG 1** rests at -7.5 V where the specification says -5 V.

### Not emulated

- The power supply's ripple and the unit warming up.
- Part tolerances, so every MS-20 here is the same unit (each one drifts and hisses on its own, though).
- PHONES isn't a plugin output, so the plugin plays SIGNAL OUT.

## ROMs

None: the MS-20 has no CPU.

## Artwork places

The panel's brand mark is the user's own file in the ROM folder's `artwork/ms20/` (or anywhere in
it as `ko-mono-20-maker`):

| Place | What |
|---|---|
| `maker` | the KORG logotype on the panel, bottom right (38.99 x 12.31 mm) |

There is no model name on the front. Without the file the place stays blank.

## Use

MIDI notes C3-C6 play the 37 keys, so 8' sounds at the MIDI pitch; notes outside fold in by
octaves. The keyboard gives the highest key held, as its resistor ladder does; legato never
retriggers the envelopes. Pitch bend moves the control wheel, which stays where it is left and is
saved with the session. The patch panel takes cables as the unit does: drag from a jack to another,
an output into an input; a plug pulled from an input takes its cable with it; right-click a jack to
pull its cables. The plugin's side-chain input is the cable from outside. Click ESP SIGNAL IN or EXT
SIGNAL IN to plug it in there, and click it again to pull it out. SERVICE on the bar holds CIRCUIT,
the filter board, REV 1 or REV 2, which the session keeps (switching restarts the circuit settled). VOLUME's counter-clockwise click is POWER OFF, as on
the unit: turned out of it, the MS-20 boots from cold (its capacitors charging, EG 2 firing once:
a thump, quiet at the low VOLUME the switch leaves); into it, the sound dies with the rails. A
plugin or session load is not a power-on: settled at its controls and cables.

## Keys

`?` shows them. A W S E D F T G Y H U J K O L P ; ' play from the keyboard's middle C, Z and X move
the octave (-1 to +1), 1 is the momentary switch. Knobs drag up and down (shift for fine,
double-click for the default, or the mouse wheel); a click on a rotary switch steps it one position
clockwise and wraps round, shift-click or right-click steps it back, a drag round picks a position.
Sliding along the keys plays them in turn. The control wheel drags up and down and has its centre
click (double-click centres it); the momentary switch is held with the mouse. A right-click holds a
key or the momentary switch down until it is clicked again; Escape lets go of them all and drops a
cable being drawn.

## Docs

- `docs/research.md`: sources, revisions, the KORG35, references, open questions.
- `docs/sources.md`: every file used, with URL and SHA1 (the files are in the private folder).
- `docs/circuit/`: every block transcribed part by part, its netlist's checks.
- `docs/model.md`: how the netlists run in the plugin, and what isn't a part value.
- `docs/compare.md`: the model against recordings of real units, per event.
- `docs/panel.json`, `docs/panel.md`: the panel as measured.
- `docs/improvements.md`: what is known to be wrong, and the plan.
