# Sources

What the QuadraVerb GT emulation is built and checked against. The files are private
(`private/`, linked as `machines/qvgt/private`); paths below are relative to it, or to the
private folder's `roms/qvgt/` for the firmware. Retrieved 2026-11-11.

## Firmware

| File | From | SHA1 |
|---|---|---|
| `roms/qvgt/qvgtv103.bin` | archive.org "vintage-synth-firmware-collection", `Alesis_QuadraVerb_GT_Firmware_v1.03.zip` (zip SHA1 257d4caf0386c188e91fac6ed5748f53aa7a7cdb). 27C512 image as read, address lines A0-A7 reversed | 0ac063e74b6c6ecf9edda10824df9a79c621ccd9 |
| `roms/qvgt/qvgtv100.bin` | same collection, `Alesis_QuadraVerb_GT_Firmware_v1.00.zip` (zip SHA1 450b4a1c3dbca956888011dac1d826d827497e66) | f09ee9d50425a407fa33b6ac8eb79eca660e491f |
| `reference/firmware/qv+v203.bin` | same collection, `Alesis_QuadraVerb+_Firmware_v2.03.zip` (zip SHA1 61a342b5423df4d1de5f7e2dd7b1ac1cb6d7570a): the QuadraVerb Plus, same DSP programs family | 5f97c5cb6b209643bed6ec4046662c87c317a9ee |
| `reference/firmware/quadraverb-fw-v107-raw.bin` | same collection, `Alesis_QuadraVerb_Firmware_v1.07.zip`: the original QuadraVerb, the firmware the DSP decode was first made on | 224b6a308552921e0fc6a916f900f57f6d5fa623 |

Descrambled (A0-A7 reversed back), the GT images start `02 00 E0` (LJMP 00E0h); v1.03 descrambled
SHA1 59c05f8db694f151c6cfad1cece2ce61759f3e82, v1.00 3d9b5b4a300f8a3ff8d24bb21cf81c4bd0d8b5ee.

## Documents

From quadraverb.nl (<http://quadraverb.nl/downloads.htm>) unless noted. `docs/manual/` also holds
a text extraction of each (`pdftotext -layout`).

| File (`docs/manual/`) | What | URL | SHA1 |
|---|---|---|---|
| `QuadraVerbGT Service-Manual.pdf` | GT service manual rev 1.00, 10/17/95, 33 pp.: two schematic sheets (pp. 7-8, vector), software history, MIDI implementation and program layout, parts list with every value | <http://quadraverb.nl/manuals/QuadraVerbGT%20Service-Manual.pdf> | 7eecc8cd0acffb5c02fdfe018f1591b4d4e41a21 |
| `gt-ref.pdf` | GT reference manual, 129 pp. | <http://quadraverb.nl/images/gt-ref.pdf> | f69957082bb1e44635db1ddee3b30ab09123354e |
| `gt-ProgramChart.pdf` | Factory program chart | <http://quadraverb.nl/images/gt-ProgramChart.pdf> | 8d9dc3d414c11134b85dbf691c64e9f48b2e6d13 |
| `gt-notice.pdf` | Notice sheet | <http://quadraverb.nl/images/gt-notice.pdf> | b560d0eff2295432d7e213db8fe8f53de550f820 |
| `gt-_QuickSetup.pdf` | Quick setup | <http://quadraverb.nl/images/gt-_QuickSetup.pdf> | c9ff700efa5b322f0725c1cc3be91a0ff169f41e |
| `plus-manual-add.pdf` | QuadraVerb Plus manual addendum (ring modulator, resonators, sampling) | <http://quadraverb.nl/images/plus-manual-add.pdf> | f00ab0d8c5279db7c48e33707c2a0a96872bb311 |
| `quadraverb-service-manual.pdf` | QuadraVerb service manual: circuit description of the DASP 24, SAR ADC, DAC | <http://quadraverb.nl/manuals/Quadraverb%20service%20manual.pdf> | 48998a6ca426d369187b0cf618335005ab350fa7 |
| `quadraverb-ref-manual-v1.0.pdf` | QuadraVerb reference manual v1.0 | <http://quadraverb.nl/manuals/QuadraVerb_Ref_Man_v1.0.pdf> | 89b7bdc04262a64ea139c41da57915f37a7db105 |
| `al3201.pdf` | Alesis AL3201 data sheet: a later Alesis reverb DSP, for comparison only | (kept from the earlier QuadraVerb work; source URL to re-find) | ccf692235a12614b2c6a6a16bc81a3ef0f097bb9 |

Schematic renders (`docs/schematic/`, `pdftoppm -r 400` of service manual pp. 7-8):

| File | SHA1 |
|---|---|
| `SKE1_sheet1_preamp_400dpi.png` (sheet 1 of 2, drawing 9-50-1050 rev B, 8-22-91) | 1913ddfcf9bb0638fc24c39e7648c86436aa77dc |
| `SKE1_sheet2_dsp_io_400dpi.png` (sheet 2 of 2) | 0e722d443f0d46597f4dd8035f0314cfc25a5b92 |

## Data sheets (`docs/datasheets/`)

| File | Part | URL | SHA1 |
|---|---|---|---|
| `SSM2120.pdf` | SSM2120 dynamic range processor (U4: compressor, gate, overdrive VCA) | <https://docs.ampnuts.ru/analog.com.datasheet/ssm2120/105738070SSM2120.pdf> | 23352694ebd86d18821b4e1ab453907647ac3f6e |
| `SSM2120_revC.pdf` | same, rev C | <https://static.datasheets.com/doc/16649311-analog-devices-ssm2120p-ds.pdf> | d3cf3b55191bac3154eb3c587a661913d140778c |
| `CEM3381_sdiy.pdf` | CEM3381 dual low-rise VCA (U12, dry level) | <https://static.wikitide.net/sdiywikiwiki/8/8e/CEM3381_datasheet.pdf> | d989847bf306d3747a727c01e3750ce14b584b34 |
| `CEM3381_3382_curtis.pdf` | CEM3381/3382, Curtis's own link (scanned) | <https://www.curtiselectromusic.com/family-law.html> ("Data Sheet PDF") | 36ac86863d6e3fd429c72cd81e7286789ffbf36a |
| `PCM54.pdf` | PCM54 16-bit DAC (U20) | <https://docs.ampnuts.ru/eevblog.docs/Jasco/870_875_UV_Spectrophotometer/Datasheets/pcm54.pdf> | 20e01116a11a16e36a82bdd68578ab3be051a04e |
| `tl084.pdf` | TL084 quad op amp (9 used) | <https://www.ti.com/lit/ds/symlink/tl084.pdf> | 55d0e0f1c5b209b79cf28888bdd9b3fdb56509b4 |
| `lm311.pdf` | LM311 comparator (U21, the SAR ADC's comparator) | <https://www.ti.com/lit/ds/symlink/lm311.pdf> | 49f932a4fec89e1a8e9bccebceb806a6e9f8a989 |
| `lm339.pdf` | LM339 quad comparator (U17) | <https://www.ti.com/lit/ds/symlink/lm339.pdf> | 0dab47dcbeeec896bf56382016174b3983b9c8aa |
| `cd4051b.pdf`, `cd4053b.pdf` | CD4051B/4053B analog switches (one TI sheet covers both) | <https://www.ti.com/lit/ds/symlink/cd4051b.pdf> | a7a77208c331ec23ade744616bbcb7e790825ac9 |
| `2n5460-d.pdf` | 2N5460 P-JFET (Q1, distortion) | <https://www.onsemi.com/download/data-sheet/pdf/2n5460-d.pdf> | eba39c18460b465455a55629837657b41704d58a |

Not fetched yet: 1N4148 (onsemi returned an HTML page), 2N4401, 6N138, the 20 kHz low-pass filter
module (part 7-20-0001, no data).

## Recordings (`reference/recordings/youtube/`)

Real units, audio downloaded with yt-dlp; settings mostly unknown. The folder's README lists what
each shows.

| Video | Channel | Length | SHA1 of the m4a |
|---|---|---|---|
| <https://www.youtube.com/watch?v=Q5WEcTGqxgE> | Don Solaris | 18:51 | 8b5168078b9221aeebdf426449b23e76016ec5ed |
| <https://www.youtube.com/watch?v=wS5u7lwVCHA> | Tone Works | 4:24 | fc6169f889f1ea3bfdcd22fafa9c86827ec9daa3 |
| <https://www.youtube.com/watch?v=2Cezu3iBHpk> | Daniel Combet | 18:59 | 92329040f8ba03efa4bbc9e9820f12a6220a7816 |
| <https://www.youtube.com/watch?v=V_1n1hXO5jM> | Phase of the Day | 0:54 | 0f0a4ec663c356e6f3752fcc56dc8414ac5b33c8 |
| <https://www.youtube.com/watch?v=HV_v6yfAfSs> | elijah | 0:58 | 8be3e9a9a6eec12f262d325a0bf7ad06572201f1 |
| <https://www.youtube.com/watch?v=Sxw9Mnevz64> | elijah | 0:59 | de7295ec4aeb4575ca54a9a6bf4d73f95f5c5196 |
| <https://www.youtube.com/watch?v=3cfsQ6CD15s> | elijah | 1:38 | 8794468d5aa3d0a175dbd60c996bec9fb4bd5cd8 |
| <https://www.youtube.com/watch?v=1cFntYwsPes> | elijah | 2:00 | d742f592cb187576a1f57f9ebe3b01457eda1441 |
| <https://www.youtube.com/watch?v=6t48dFOvLQA> | Adilson Oliveira | 11:57 | 2765f79dc3e462ed6f1e0e9634cf82396f611a33 |
| <https://www.youtube.com/watch?v=iRG9aGK1Vqg> | Daniel Krämer | 18:22 | 258485b6d90345b6e979c83021cce34fef4b6cc1 |

## Other emulations (not used as oracles)

- CPU Quad (CPU Records), original QuadraVerb v1.07, closed: <https://cpurecords.net/software/cpu-quad/>
- QDV1 (Temecula DSP), QuadraVerb Plus, behavioural, with A/B renders against a real unit on its
  page: <https://www.temeculadsp.com/qdv1>
- MIDIVerb decode (the same designer's earlier DSP): MAME `src/mame/alesis/midiverb.cpp`,
  <https://github.com/emeb/MIDIVerb_RE>

## Earlier work

The 80C31 core, the board model and the DASP-24 interpreter come from a QuadraVerb (v1.07)
emulator written for this project before the machine was started (2026-10-04, outside the repo).
Its notes on the DSP instruction set are the starting point for `docs/dasp24.md`.
