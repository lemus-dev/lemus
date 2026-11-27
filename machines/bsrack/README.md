# Novation Bass Station Rack (no-mono-bsr)

An emulation of the Novation Bass Station Rack (1995), the 1U module version of the Bass Station
analogue bass synthesizer. Not affiliated with Novation.

**Status: 0.1.0 beta.** The real firmware (v1.5) runs on MAME's MN1880
core, and we model the analog voice from a traced circuit. The trace has almost no part values, so we
fitted most of them to recordings of real units (`docs/improvements.md` has the numbers and the plan).

## How close it is

We think it's really close on most sounds, though a few of the wilder factory patches still differ
from the one real unit we could hear play them.

- **The demo** - the built-in demo plays note for note like a real unit's recording, and its level
  follows the recording within 0.7 dB (median) with the tone within about 3 dB from 40 Hz to 10 kHz.
- **The factory programs** - we lined up all 40 against a video of a real unit playing them, and the
  tone comes out within 4.3 dB on average. That unit probably runs older firmware, because a few
  patches (SPIT, SQUARE PORTA, RAINMAN) move the filter there in ways v1.5's code doesn't.
- **The filter at full resonance** - in 24 dB it self-oscillates up to about 250 Hz and rings above
  that.
- **No hiss** - we don't model any yet, because none of our recordings is clean enough to hear it.
- **CPU** - about 4 % of one core at 48 kHz playing a bassline.

## What it doesn't do

- **CV and gate** - the rear's CV/GATE IN and OUT are drawn, but the plugin has no jacks for them.
- **MIDI THRU** - the plugin's MIDI output is the MIDI OUT jack (a knob sends its controller).
- **The headphone amp** - the output is the line out.

## Things the real unit does too

- **Soft takeover** - a knob only acts once you move it, so a program keeps its own settings until
  you touch the panel.
- **The v1.5 lock-up** - if every knob gets turned within a few seconds (or a host moves every
  parameter at once), v1.5 hangs and ignores notes. The real unit stays like that until you switch it
  off, and the plugin until you use Reset in the State menu.
- **FILTER INPUT** - route a track to the plugin's side-chain and it goes through the filter, as the
  rear's EXTERNAL AUDIO INPUT does. REAR in the bar turns the unit round.

## ROMs

| Name | Size | SHA1 | CRC32 | What |
|---|---|---|---|---|
| `v1.5.bin` | 32768 | 2c2dc3bee0fabf1e77d02cdf3e53885f2c5b913e | a8bc9721 | Program EPROM, firmware v1.5 (MAME set `bassstr`) |

Required. The firmware does the oscillators' clocks, the envelopes, the LFO and the panel. Without
the firmware there is nothing to play.

## Artwork places

These are optional. Put each file in the ROM folder as `artwork/bsrack/<place>.svg` (or `.png`),
or as `no-mono-bsr-<place>.svg`.

| Place | Mark | Box (mm) |
|---|---|---|
| `maker` | Novation logo, in the display window | 29.03 × 2.80 |
| `name` | BassStation Rack logo, lower right | 42.82 × 5.78 |
| `rear_maker` | Novation logo on the rear | 88.5 × 8.6 |
| `rear_name` | BassStation Rack logo on the rear | 87.7 × 11.9 |

## Docs

- `docs/research.md`: the target unit, the firmware, the circuit, the panel, the reference
  recordings, and the open questions.
- `docs/sources.md`: every source, with its SHA1.
