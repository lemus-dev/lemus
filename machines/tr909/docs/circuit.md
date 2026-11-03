# Roland TR-909 voicing board: circuit transcription

Every analog circuit of the TR-909's voicing board (73133090) and multi-jack board (JK-1), as the
netlists in `core/netlists/` run it, transcribed part by part from the service notes, and the
digital parts of the noise generator and the sample voices as `core/` runs them. This file has the
conventions every netlist follows; each block's transcription (every stage, every value and where
it was read, what was hard to read, where the sources disagree, the production changes) is in
`docs/circuit/`:

| Block | Netlists, code | Notes |
|---|---|---|
| Semiconductors | `models.cir` | [semiconductors.md](circuit/semiconductors.md) |
| The (S) VCA transistors | `models.cir` (2SC2603S) | [s-transistors.md](circuit/s-transistors.md) |
| Trigger and accent latches, DACs | (the board: `VoiceBoard::write`) | [interface.md](circuit/interface.md) |
| Noise generator | `core/noise.*` | [noise.md](circuit/noise.md) |
| Bass drum | `bd.cir` | [bd.md](circuit/bd.md) |
| Snare drum | `sd.cir` | [sd.md](circuit/sd.md) |
| Low, mid and hi tom, tom noise | `lt.cir`, `mt.cir`, `ht.cir`, `tomnoise.cir` | [toms.md](circuit/toms.md) |
| Rim shot, hand clap, TRIGGER OUT | `rs.cir`, `hc.cir` | [rshc.md](circuit/rshc.md) |
| Hi-hat | `hh.cir`, `core/hihat.*` | [hh.md](circuit/hh.md) |
| Crash and ride | `cr.cir`, `rd.cir`, `crclk.cir`, `rdclk.cir`, `core/cymbals.*` | [cymbals.md](circuit/cymbals.md) |
| Multi-jack board, master outputs, power-on mute | `mix.cir` | [mix.md](circuit/mix.md) |
| Power supply and reset | (what power-on does) | [power.md](circuit/power.md) |
| The solver's MOSFET element | `core/circuit.*` | [solver-mosfet.md](circuit/solver-mosfet.md) |

The research digest (`docs/research.md`) came before the transcription; its "Corrections found
since" lists where the transcription found it wrong.

## 0. Sources, conventions

**Sources.** All under `private/docs/` (not in the repo):

| Key | What | Where |
|---|---|---|
| **SN** | Roland *TR-909 Service Notes*, 1st ed., 15 Jun 1984, 16 pp. Page images at 400 dpi, crops of each block | `tr909-service-notes.pdf`, `schematic/pNN-*.png`, `schematic/crops/`, OCR index `schematic/ocr/` |
| **OM** | Owner's manual | `tr909-owners-manual.pdf`, `.txt` |
| **NAVA** | e-licktronic Nava v1.02 netlist: a one-to-one copy of a late voicing board with Roland's designators | `papers/emulations/code/e-licktronic_Nava-v1.0/` |
| **DS** | Data sheets | `datasheets/` |

Grid references are SN p.11's: rows A-R, columns 1-27.

**Netlists.** One file per block, in the solver's syntax (`core/circuit.h`). `models.cir` holds
every semiconductor model and the op-amp parameters, and is read before each block. Every
designator is Roland's (R239, C66, Q41, IC34b, VR6); a part that isn't on the schematic says what it
stands for.

**The board's signals, as the netlists take them** (`VoiceBoard`, `core/voiceboard.*`):

| Input | What | Level |
|---|---|---|
| `trig` | The voice's TRIG latch output (IC1 or IC10, 4174 on +5 V) | 0 / 5 V, high for one panel scan (about 2 ms) per hit. The netlist holds the latch's output resistance |
| `acc` | The voice's accent DAC (IC2-IC9 4174 into an RM0621): its open-circuit voltage | 0-5 V, held between writes. With ideal latches it is 5 V · code / 63 behind 2.54 kΩ (5 kΩ · 32/63); the board counts the 4174s' 455 Ω outputs in series with the array's resistors, which makes it 5 V · Σg_on / Σg behind 2.69 kΩ (`interface.md`), and sets each netlist's `rdac` to that. Where two voices share a DAC (RS and HC on IC7, crash and ride on IC9), each netlist holds the other's load as it is (none for RS/HC: op-amp inputs; the other cymbal's base network, by its run line) |
| `noise` | The noise generator's output (IC33 pin 9, 4006 on +15 V), averaged over each sample | 0-15 V |
| `ohsel` | HH only: the CLOSED/OPEN line (TRIG latch 2, bit 3) | 0 / 5 V |

The sample voices (HH, crash, ride) take their latches' outputs (`d0`-`d5`), the cymbals' envelope
counter bits (`e0`-`e5`) and the run lines (`run`, and the other cymbal's `rd.run` / `cr.run`) as
further inputs, from their digital halves (`core/hihat.*`, `core/cymbals.*`), each line's steps
band-limited and placed at their own times within the board's steps. A name with a dot in it is a
board signal as it stands; other names get the voice's prefix (`hh.d0`).

Rails are constants in each netlist: `b15` +15 V, `nb15` -15 V, `v5` +5 V.

**Controls** are the panel's pots by their panel names (`tune`, `level`, `attack`, `decay`, `tone`,
`snappy`, `chdecay`, `ohdecay`), rotation 0 (fully anticlockwise) to 1, with the pot's VR number,
value and taper from the parts list.

**Output.** `.output out` is the voice's output at the voicing board's connector to the multi-jack
board, loaded by what the jack board puts on it (`mix.cir` runs the jack board itself).

**Rates.** The board runs at twice the host rate (88.2 or 96 kHz); every netlist has to be right
from 88.2 kHz up. The sample voices run at twice the board's rate (176.4 or 192 kHz): the images of
their D/As pass the filters after them (`hh.md`, `cymbals.md`, "Rates"). The cymbals' clocks
(`crclk.cir`, `rdclk.cir`) aren't run by the board at all: `Cymbal` times them once at 100 MHz at
eleven TUNE positions and places their edges from that.
