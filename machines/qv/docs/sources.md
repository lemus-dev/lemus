# Sources

What the QuadraVerb emulation is built and checked against. The files are private (`private/`,
linked as `machines/qv/private`); paths below are relative to it, or to the private folder's
`roms/qv/` for the ROMs. Retrieved 2026-11-27 unless noted (most were kept from the QuadraVerb GT's
research, `machines/qvgt/docs/sources.md`, and the earlier QuadraVerb work of 2026-10-04).

## ROMs

| File | Whose dump | SHA1 |
|---|---|---|
| `roms/qv/qvv203.bin` | QuadraVerb Plus firmware v2.03, the archive.org "vintage-synth-firmware-collection" (`Alesis_QuadraVerb+_Firmware_v2.03.zip`, zip SHA1 61a342b5423df4d1de5f7e2dd7b1ac1cb6d7570a). 27C512 image as read, A0-A7 reversed | 5f97c5cb6b209643bed6ec4046662c87c317a9ee |
| `roms/qv/qvv107.bin` | QuadraVerb firmware v1.07, the same collection (`Alesis_QuadraVerb_Firmware_v1.07.zip`) | 224b6a308552921e0fc6a916f900f57f6d5fa623 |
| `roms/qv/hd44780_a00.bin` | HD44780 A00 character ROM, MAME's | 65cf075a988cdcbb316b9afdd0529b374a1a65ec |

Descrambled, v1.07 starts `02 00 AB` and v2.03 `02 00 E0`. The same collection holds QuadraVerb
v1.03 and Plus v2.00-2.02 (not used: v1.03 predates the rev F board, `research.md`).

## Documents

From quadraverb.nl (<http://quadraverb.nl/downloads.htm>) unless noted. `docs/manual/` also holds a
text extraction of each (`pdftotext -layout`).

| File (`docs/manual/`) | What | URL | SHA1 |
|---|---|---|---|
| `quadraverb-service-manual.pdf` | Service manual 1.00 (6/28/94), 46 pp.: circuit description, board revisions and updates (§7), self-diagnostics (§5), two schematic sheets (pp. 14-15, vector, every value), software history 1.00-2.03, Plus MIDI implementation, parts list (PCB QV MAIN REV F) | <http://quadraverb.nl/manuals/Quadraverb%20service%20manual.pdf> | 48998a6ca426d369187b0cf618335005ab350fa7 |
| `quadraverb-ref-manual-v1.0.pdf` | Reference manual v1.0 | <http://quadraverb.nl/manuals/QuadraVerb_Ref_Man_v1.0.pdf> | 89b7bdc04262a64ea139c41da57915f37a7db105 |
| `plus-manual-add.pdf` | QuadraVerb Plus manual addendum | <http://quadraverb.nl/images/plus-manual-add.pdf> | f00ab0d8c5279db7c48e33707c2a0a96872bb311 |
| `al3201.pdf` | Alesis AL3201 data sheet, a later Alesis reverb DSP, for comparison only | (source URL to re-find) | ccf692235a12614b2c6a6a16bc81a3ef0f097bb9 |

Schematic renders (`docs/schematic/`, `pdftoppm -r 400` of service manual pp. 14-15; `crops/` cut
from them):

| File | SHA1 |
|---|---|
| `sheet1_analog_400dpi.png` (inputs, A/D front end, dry VCA, outputs, level display, MIDI, power) | f248229e2ead1f7eb984c4aa54c1307077d98143 |
| `sheet2_digital_400dpi.png` (80C31, EPROM, SRAM, DASP 24, PCM54, DRAM, latches, LCD) | 214ec9e2374d1012e99ac9939f740b375aa6e04a |

Sound On Sound, February 1991, p. 84, "Win An Alesis Quadraverb Plus!" (a new Plus pictured):
<https://www.muzines.co.uk/articles/win-an-alesis-quadraverb-plus/7378> (viewed, not kept).

## Data sheets (`docs/datasheets/`)

Copied from the GT's research (URLs and SHA1s in `machines/qvgt/docs/sources.md`):
`CEM3381_sdiy.pdf` and `CEM3381_3382_curtis.pdf` (U5), `PCM54.pdf` (U10), `tl084.pdf` (U1-U4,
U7, U22), `lm311.pdf` (U8), `cd4053b.pdf` (U9).

## Reference recordings

`reference/` (a README in each folder with every file, its source and SHA1; collected 2026-11-27):

| Folder | What | From |
|---|---|---|
| `reference/recordings/temecula/` | 60 WAVs: a QuadraVerb Plus and Temecula's QDV1 plugin on the same factory programs, guitar and drums, 24-bit 44.1 kHz | Temecula DSP's QDV1 page (<https://www.temeculadsp.com/qdv1>), offered for download; no terms stated, kept private |
| `reference/recordings/youtube/` | 21 videos' audio (m4a): owners' units with named programs, dry passes, walkthroughs, repairs | YouTube, by channel in the README |
| `reference/recordings/freesound/` | A list only (download needs a login): 32 impulse responses of a QuadraVerb, CC BY 4.0 | Freesound user mansardian |
| `reference/sysex/` | Owners' program banks (not factory dumps) | Listed in the README |
| `docs/photos/` | 33 photos: straight-on front and rear, close-ups, boards, labels, serials | Listing photos (seller's copyright, private) and YouTube frames; README |
| `docs/manual/` (more) | Factory program charts, SysEx notes, four magazine reviews | README there |
