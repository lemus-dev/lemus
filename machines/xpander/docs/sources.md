# Oberheim Xpander: sources

Every file downloaded on 2026-11-12, with its URL and SHA1. Paths are relative to
`private/xpander/` (linked as `machines/xpander/private` by `tools/link-private.sh`), except ROMs,
which are in `private/roms/xpander/`. All downloads are untrusted data. No file came from a
purchase, an account or a request to anyone. Recordings are listed in
`private/reference/youtube/SOURCES.md` (summary in `research.md` section 4).

## ROMs (`private/roms/xpander/`)

The images are kept under MAME's names. Every SHA1 matches MAME master
c2caa8969adbf3d599a655a162a6eac163fa63bc (`xpander.cpp`, `xpander_vb.cpp`). Where they came from
isn't given here (the project doesn't say where to get ROMs).

| File | SHA1 | MAME name |
|---|---|---|
| xpander/exp1.2-0.u8 | 4064edc2fa0bea62684c28e8e004feb8229604fe | exp1.2-0.u8 (main 1.2) |
| xpander/exp1.2-1.u7 | 233efc3da3a96777b10a4d6fe7e80864e9231f04 | exp1.2-1.u7 |
| xpander/exp1.2-2.u6 | 798c1b28fb59819baadbf0142ecd1348e759fa70 | exp1.2-2.u6 |
| xpander/exp1.2-3.u5 | 5897ad0cd66cf37b8e9b277654aab4eb83d1c8ab | exp1.2-3.u5 |
| xpander/ca1.4-0.u912 | 5ab13ccc3b75df313a447520ca54a492102b7e3b | ca1.4-0.u912 (voice 1.4) |
| xpander/ca1.4-1.u914 | 951bbc197a0140a93bb7621a7aedbe0f4037990c | ca1.4-1.u914 (also 1.6's second ROM) |
| xpander/ca1.6-0.u912 | a7babb3d5211b5ef0d03f50540401ae4ae7170f3 | ca1.6-0.u912 (voice 1.6) |
| matrix12/mat1.1-0.u9 … mat1.1-4.u5 | as MAME `matrix12` | Matrix-12 main 1.1 (later machine) |

Not found: Xpander main 1.0.

### Factory patches (optional image)

Oberheim's factory single patches, as a SysEx dump; copied to `private/roms/xpander/`. Where they
came from isn't given here, as for the ROMs.

| File | SHA1 | What |
|---|---|---|
| FACTORY1.SYX | b8abde65cff74424438fd371eb2261c2e70ccaa8 | "Factory patches 1 (XP)": the set that came with the Xpander. 100 single patch dumps, F0 10 02 01 00 nn (196 bytes, each as its low 7 bits then its top bit) F7, 399 bytes each, patches 00-99 |
| FACTORY2.SYX | (in the private folder) | "Factory patches 2 (M12)": the set that came with the Matrix-12, the same format |

The dump format and the enable commands are Oberheim's Xpander MIDI specification (part 950038).

The bank's notes say the file is the patch data "exactly as it came from the synthesizer". Its 100
names match its list, and voice 1.4 / main 1.2 store all 100 (model checked 2026-11-13).

## Service manuals (`docs/manuals/`)

| File | URL | SHA1 | Note |
|---|---|---|---|
| service_a/XPANDER_SERVICE_MANUAL.pdf | https://archive.org/download/oberheim_XPANDER_SERVICE_MANUAL/XPANDER_SERVICE_MANUAL.pdf | 52257a7633670119911768d3ced607cc1c027d3f | First edition July 1984 (p/n 950037), 79 pp colour, schematics up to 2048 px, ECOs 601-615. The scan to read |
| service_a/XPANDER_SERVICE_MANUAL_djvu.txt | …/XPANDER_SERVICE_MANUAL_djvu.txt | b0849e1a1f303df854e242e602a4193969b98a50 | archive.org OCR |
| service_b/Oberheim_Xpander_Service_Manual.pdf | https://archive.org/download/oberheim_Oberheim_Xpander_Service_Manual/Oberheim_Xpander_Service_Manual.pdf | 2b791dbe852e837573a2cf81e1255757fac4fa66 | Same edition, 77 pp greyscale 200 dpi, foldouts in halves, ECO 601 only. Second opinion |
| service_b/Oberheim_Xpander_Service_Manual_djvu.txt | …/Oberheim_Xpander_Service_Manual_djvu.txt | 5b20db0a94da716db1ff5130983f1382f6300869 | archive.org OCR |

## Derived files (ours, private)

- `docs/survey.md`: the circuit and firmware survey (its content is research.md sections 1-3).
- `docs/schematic/crops/`: crops of service_a/service_b cited in research.md.
- `docs/disasm/voice14.dis`, `main12.dis`: disassemblies of the ROMs (capstone M680X), made by
  `docs/tools/dis.py`; `docs/tools/polemix.py` computes the pole-mix coefficients.
- `reference/tools/`: the recordings' stereo and correlation checks.


Paths are relative to `private/xpander/`. SHA1 is `shasum` of the file as saved. All downloads were
free and anonymous; nothing was executed. Files already in hand before this pass (service manuals
`docs/manuals/service_a`, `service_b`, MAME `xpander*.cpp/.h`, ROMs) are not repeated unless compared.
Schematic crops made during the survey are in `docs/schematic/crops/` (our own crops of the service manual).

## 1. Data sheets

| File | URL | SHA1 | Note |
|---|---|---|---|
| docs/datasheets/CEM3372_ElectricDruid.pdf | https://electricdruid.net/datasheets/CEM3372SignalProcessor.pdf | 2d3608732a75b55819ad10d515938fd501d1cd2a | Curtis CEM3372 "uP controllable signal processor", 2 pp, 300 dpi 1-bit scan |
| docs/datasheets/CEM3372_sdiy.pdf | https://static.wikitide.net/sdiywikiwiki/9/93/CEM3372_datasheet.pdf | 777c1ed5062e47d6d6516f72f3038524d0ea02aa | Same sheet, different scan (sdiy.info wiki), 2 pp, 300 dpi |
| docs/datasheets/CEM3374_sdiy.pdf | https://static.wikitide.net/sdiywikiwiki/0/02/CEM3374_datasheet.pdf | 830fe4084ce2e97af7801689432c4b28036a31b8 | Curtis CEM3374 dual VCO, 2 pp, 300 dpi; block diagram, tempco sensor (+2.5 V, +3300 ppm), sync types. Same scan also at maxim4u.com/datasheet.hk mirror (not kept) |
| docs/datasheets/Alfa_AS3372E.pdf | https://alfatriode.lv/eng/sc/AS3372E.pdf | bc14842a2c169e469bc296fd8950136a2fbe3a3a | Alfa AS3372E (modern CEM3372 re-design, DIP-24, not pin compatible), 6 pp, vector |
| docs/datasheets/related_cem/CEM3340-3345_long.pdf | https://static.wikitide.net/sdiywikiwiki/1/1c/CEM3340-3345_long.pdf | 340b2f48a9c73f1e2af10b771615aac43edaef8d | CEM3340/3345 long sheet, 6 pp (sister VCO; service manual compares the 3374 tracking to it) |
| docs/datasheets/related_cem/CEM3320_datasheet_long.pdf | https://static.wikitide.net/sdiywikiwiki/d/d1/CEM3320_datasheet_long.pdf | 424971da2334c9a78a7948183234c8c834dfc044 | CEM3320 long sheet, 6 pp (earlier filter, for comparison) |
| docs/datasheets/related_cem/CEM3328_datasheet.pdf | https://static.wikitide.net/sdiywikiwiki/8/84/CEM3328_datasheet.pdf | 93c68869fdd7ce0f4b54c7d745547e3e3600bf5f | CEM3328 4-pole VCF, 2 pp (comparison) |
| docs/datasheets/Intersil_AD7523_AD7533_2001.pdf | https://www.komponenten.es.aau.dk/fileadmin/komponenten/Data_Sheet/ADC_DAC/AD7523.pdf | cfbb165b638e0672346fdb39863213bf4e1b3319 | AD7523/AD7533 8-bit multiplying DAC (FM depth MDAC), Intersil FN3105 Jan 2001, 9 pp. Not the original Analog Devices sheet |
| docs/datasheets/TI_cd4051b_4052b_4053b.pdf | https://www.ti.com/lit/ds/symlink/cd4051b.pdf | a7a77208c331ec23ade744616bbcb7e790825ac9 | CD4051B/52B/53B analog mux, 39 pp (pole-mix mux, S&H addressing) |
| docs/datasheets/TI_cd4016b.pdf | https://www.ti.com/lit/ds/symlink/cd4016b.pdf | e55d03db027fd2f33ae21390661280d95f5928ae | CD4016B quad bilateral switch, 30 pp (parts list: 6 x MC14016) |
| docs/datasheets/TI_cd40103b.pdf | https://www.ti.com/lit/ds/symlink/cd40103b.pdf | 554948da8164a8625f7a5999749300de783f1d49 | CD40103B 8-stage down counter, 21 pp (in parts list) |
| docs/datasheets/TI_adc0804.pdf | https://www.ti.com/lit/ds/symlink/adc0804-n.pdf | 51ede35cc579ed0da366b11b8c4a44e59a02194f | ADC0801-0805, 58 pp (pot board U20) |
| docs/datasheets/TI_tl08x.pdf | https://www.ti.com/lit/ds/symlink/tl081.pdf | 8efa9381a0aaeaa7b63522ff1944cea5ef02bba5 | TL081/082/084, 78 pp (voice board op-amps; current TI sheet, not 1984) |
| docs/datasheets/TI_tl07x.pdf | https://www.ti.com/lit/ds/symlink/tl074.pdf | feac3c5d52d44def8c96207bb0c1d45468237f41 | TL071/072/074, 89 pp (parts list: 6 x TL074, 1 x TL072) |
| docs/datasheets/TI_lm311.pdf | https://www.ti.com/lit/ds/symlink/lm311.pdf | 49f932a4fec89e1a8e9bccebceb806a6e9f8a989 | LM311 comparator, 42 pp (voice board oscillator comparator) |
| docs/datasheets/TI_lm101a-n_lm301a.pdf | https://www.ti.com/lit/ds/symlink/lm101a-n.pdf | a64d30d0e3dbae330d7c3415062bdde99e27fe3e | LM101A/201A/301A op-amp, 25 pp (parts list: LM301AN) |
| docs/datasheets/Motorola_MC6809_bitsavers.pdf | http://www.bitsavers.org/components/motorola/_dataSheets/6809.pdf | 52cf7c9406e340631edb8b4d76a3366cd220c661 | MC6809 data sheet, 36 pp |
| docs/datasheets/Motorola_M6809PM_rev0_May83.pdf | http://www.bitsavers.org/components/motorola/6809/M6809PM.rev0_May83.pdf | e7254258e34cba20da8083d3c9ee4da11a4c52c9 | MC6809 programming manual rev 0, May 1983, 220 pp |
| docs/datasheets/Motorola_MC6850.pdf | https://docs.ampnuts.ru/eevblog.docs/Tektronix/TLA_Ensoniq/Datasheets/MC6850.pdf | 0fc96b40cb4675b2c3a3c51a4ffef81da115bde4 | MC6850 ACIA (MIDI, U26), Motorola, 12 pp |
| docs/datasheets/Intel_8253_1979_Peripheral_Design_Handbook_p46-56.pdf | http://www.bitsavers.org/components/intel/_dataBooks/1979_Intel_Peripheral_Design_Handbook.pdf (pages 46-56 extracted with pdfseparate/pdfunite) | 86c80369426bfcba20e2c2176d5c7655463b1274 | Intel 8253/8253-5 PIT data sheet, 11 pp (voice board PIT) |

### Published analysis of CEM3372 / CEM3374

| File | URL | SHA1 | Note |
|---|---|---|---|
| docs/datasheets/analysis/russellmcc_2013-06-14_CEM3372.html | https://www.russellmcc.com/posts/2013-06-14-CEM3372.html | 1487bc7260bfbb7f4c2b2c84b7d445b45c2594d2 | Russell McClellan, "Notes on the CEM3372 VCF" (2013): how the 3372 filter core works, vs CEM3320 |
| docs/datasheets/analysis/russellmcc_images/cem3372.pdf | https://www.russellmcc.com/images/cem3372.pdf | e3b8466f15be3790b248f2286f1cf3c248d6ce1a | Same article as PDF, 7 pp |
| docs/datasheets/analysis/russellmcc_images/cem3372.png | https://www.russellmcc.com/images/cem3372.png | d8ed3ca80e6e2a4c17702ad5ceb1807fe87e446e | Article figure |
| docs/datasheets/analysis/russellmcc_images/rc_divider.png | https://www.russellmcc.com/images/rc_divider.png | d088eefa0f1ab6bc0b1ba71d7e8bcfa042ed50ad | Article figure |
| docs/datasheets/analysis/russellmcc_images/rc_parallel.png | https://www.russellmcc.com/images/rc_parallel.png | 459c6dc5a77fb9610d3b0f4a8a75c54a11135892 | Article figure |
| docs/datasheets/analysis/electricdruid_multimode-filters-part-2-pole-mixing.html | https://electricdruid.net/multimode-filters-part-2-pole-mixing-filters/ | 9d77941557f99a92f316ca6c2d5a8b99a061334a | Electric Druid on the Xpander/Matrix pole-mixing filter: buffered cap taps, alternating inverting buffers, SW1 removing stage 1, 15 responses (HTML only, images not saved) |
| docs/datasheets/analysis/muzines_7985_EMM_Sep1984_dual_VC_LFO_CEM3374.html | https://www.muzines.co.uk/articles/dual-voltage-controlled-lfo/7985 | b3ba03ed2715a4b759fd65f0d8de633552494d9e | E&MM Sep 1984 DIY project built on a CEM3374 (application detail: sync, tempco ref) |

## 2. Manuals, owner's manual, ECOs, reviews

| File | URL | SHA1 | Note |
|---|---|---|---|
| docs/manuals/owners/JL11375/Oberheim_Xpander_Owners_Manual_synthfool.pdf | https://www.synthfool.com/docs/Oberheim/Oberheim_Xpander/Oberheim_Xpander_Owners_Manual.pdf | 2dc04f0b3176b0934adc5813dfbcd3623b171a4d | Owner's manual (June 1984), 66 pp, 2305x3000 1-bit. Byte-identical to archive.org JL11375 "Oberheim Xpander Owner's Manual (1984-06).pdf" |
| docs/manuals/owners/JL11375/Oberheim Xpander Owner's Manual (1984-06)_djvu.txt | https://archive.org/download/JL11375/Oberheim%20Xpander%20Owner%27s%20Manual%20%281984-06%29_djvu.txt | 886cd5f86ebaa2887c65b8a99d97d8be3c099f76 | archive.org OCR of the above |
| docs/manuals/owners/synthmanual/oberheimxpanderownersmanual.pdf | https://archive.org/download/synthmanual-oberheim-xpander-owners-manual/oberheimxpanderownersmanual.pdf | ef58d0b7b46671e54a112d3c1fae01fd228adbf9 | Owner's manual, second scan (synthmanual.com), 69 pp, 2305x3000. Same file as manuals.fdiskc.com/flat/Oberheim Xpander Owners Manual.pdf (same size) |
| docs/manuals/owners/synthmanual/oberheimxpanderownersmanual_djvu.txt | https://archive.org/download/synthmanual-oberheim-xpander-owners-manual/oberheimxpanderownersmanual_djvu.txt | 2d38eb032dc5ee628db36856277514a22232cc5b | OCR of the above |
| docs/manuals/service_synthfool/Oberheim_Xpander_Service_Manual_synthfool.pdf | https://www.synthfool.com/docs/Oberheim/Oberheim_Xpander/Oberheim_Xpander_Service_Manual.pdf | 1f730f785c5c08d63575d20e187ce1f0211424f4 | Service manual text part only, 39 pp, 845x1100 (200 dpi). Identical to archive.org JL11376 |
| docs/manuals/service_synthfool/Oberheim Xpander Service Manual_djvu.txt | https://archive.org/download/JL11376/Oberheim%20Xpander%20Service%20Manual_djvu.txt | 0d98c54b79d11218d1651d6f1b1c2c96fa937b78 | OCR of the above |
| docs/manuals/service_notes/oberheimxpanderservicenotes.pdf | https://archive.org/download/synthmanual-oberheim-xpander-service-notes/oberheimxpanderservicenotes.pdf | 23155567a550da35dffc9d75668f6056cadf7351 | Titled "Service Notes" but is another full scan of the Service Manual, First Edition (p/n 950037), 79 pp, 845x1100. No separate bulletins inside |
| docs/manuals/service_notes/oberheimxpanderservicenotes_djvu.txt | https://archive.org/download/synthmanual-oberheim-xpander-service-notes/oberheimxpanderservicenotes_djvu.txt | db265df99381c7df18c346e461cac64c4e811e83 | OCR of the above |
| docs/manuals/ecos/xpander_eco601.jpg | https://www.synthfool.com/docs/Oberheim/ECOS/Xpander/xpander_eco601.jpg | 3a28e4a8d1dad7557a2fc0928743a188302d98fa | ECO 601 (6/6/84): pot board, remove R75/R76 47k so unplugged pedals read max. Service manual p. 69. 1168x1590 |
| docs/manuals/ecos/xpander_eco602.jpg | https://www.synthfool.com/docs/Oberheim/ECOS/Xpander/xpander_eco602.jpg | b965af789fa642835975e36ef8d2bb33a1796c99 | ECO 602, 1109x1597 |
| docs/manuals/ecos/xpander_eco603.jpg | https://www.synthfool.com/docs/Oberheim/ECOS/Xpander/xpander_eco603.jpg | a1b43ab33f79d472707f1fed6ddff56d96ca1d01 | ECO 603, 1179x1614 |
| docs/manuals/ecos/archive_JL11385/Oberheim Xpander ECOS.pdf | https://archive.org/download/JL11385/Oberheim%20Xpander%20ECOS.pdf | 22ea38033a8ee28858bec293c171a331a08dc5ae | Same 3 ECOs as PDF, 3 pp |
| docs/manuals/ecos/archive_JL11385/Oberheim Xpander ECOS_djvu.txt | https://archive.org/download/JL11385/Oberheim%20Xpander%20ECOS_djvu.txt | 043e53f3432e2e9632050676c9f405334cea7fa4 | OCR |
| docs/manuals/reviews/muzines_7969_oberheim-xpander.html | https://www.muzines.co.uk/articles/oberheim-xpander/7969 | cdeadabab19d3f55d44ff60fa8f9667dc6cd7e7d | E&MM Sep 1984 review (text) |
| docs/manuals/reviews/sos_retrozone_oberheim-xpander.html | https://www.soundonsound.com/reviews/oberheim-xpander | 9c994e626dec6dc6268b1fb977aae6eca1496262 | Sound On Sound Retrozone review (text) |

Voice-software / board-revision notes found only as text in existing files: service manual ECO text
"install voice software version 1.1 (or above) by changing U912 ... U914" (service_a OCR ~line 5116); the
"FILTER RESONANCE CALIBRATION for voice software version 1.1" section; and "SOFTWARE VERSION NUMBER"
(Master page, VERSION). A forum quote (synth-diy.org Xpantastic archive, via web search only) says voice
boards rev D and up must run voice 1.4, and boards with Curtis rev C filters need voice 1.4 with main 1.1.

## 3. Schematics

| File | URL | SHA1 | Note |
|---|---|---|---|
| docs/schematic/synthfool/xpander_schematics.zip | https://www.synthfool.com/docs/Oberheim/Oberheim_Xpander/xpander_schematics.zip | 2b7b8f3d51330543706d63ec71f090095454bdc1 | 75 JPEGs (xpsvc01-68, xpsvca1-8), all 845x1100, from 2003; pages 37-38 missing (notes.txt). Extracted to docs/schematic/synthfool/xpander_schematics/ |
| docs/schematic/archive_JL11386/Oberheim Xpander Schematics.pdf | https://archive.org/download/JL11386/Oberheim%20Xpander%20Schematics.pdf | 6df7ab127f2c9882f1abe37616a3f8979735906e | Same synthfool images as PDF, 74 pp, 845x1100 |
| docs/schematic/archive_JL11386/Oberheim Xpander Schematics_djvu.txt | https://archive.org/download/JL11386/Oberheim%20Xpander%20Schematics_djvu.txt | a11ac09df41491fd26d64ecc98c060021b8d348c | OCR |

Verdict: none of these is cleaner than what we had. The best schematic scan remains
`docs/manuals/service_a/XPANDER_SERVICE_MANUAL.pdf` (schematic pages up to 2048 px, e.g. p. 11 at
2474x2360); service_b and all synthfool/archive copies are 845x1100.

## 4. Panel photos

Licence: only the Wikimedia Commons files are free-licensed. Reverb, vintagesynth, matrixsynth and
muzines images are copyright of their photographers/sellers: private measuring reference only, never ship.

| File | URL | SHA1 | Note |
|---|---|---|---|
| **docs/photos/reverb/103212593/103212593_0.jpg** | https://rvb-img.reverb.com/i/eed4835f-a8b4-4e87-ae32-8e7c1d91bc75.jpg (listing https://reverb.com/item/103212593, Collingswood Music, 2026-10) | 7b0efb4863490f6e12696317ba01cc1d087ab206 | **Best straight-down full front panel**, 5712x4284, panel fills ~5300 px across the cheeks. Raw pixels are upright landscape; EXIF says Orientation 6, so ignore EXIF (viewers that honour it show it rotated). Little perspective. All rights reserved |
| docs/photos/reverb/102838549/102838549_0.jpg | https://rvb-img.reverb.com/i/49efd557-c840-4763-a292-b3b0468d35e6.jpeg (https://reverb.com/item/102838549, Lords of Synthesis) | de681539dd71c94e7fc3dda660ffc8437d922ba8 | Straight-on full panel, 5712x4284, panel ~3700 px wide, slight tilt. All rights reserved |
| docs/photos/reverb/102838549/102838549_1.jpg | https://rvb-img.reverb.com/i/ad1d8d4d-14e8-424f-9ef6-12e856311488.jpeg | 198aa61d6cf38f8a6c4ad20e5b6872408a9e5e56 | Left third close, 5712x4284 |
| docs/photos/reverb/102838549/102838549_2.jpg | https://rvb-img.reverb.com/i/c649ef1d-66f3-4260-b5ef-691db592522a.jpeg | a4edd4b203fc19a07e261e5315e593180054f2b4 | Middle third close, 5712x4284 |
| docs/photos/reverb/102838549/102838549_3.jpg | https://rvb-img.reverb.com/i/1224c2fb-f6c1-408c-8ee3-7ee0c625d38c.jpeg | 1f241a9d11fe347522aa198d1f4d1e2a1ddd5092 | Right third close, 5712x4284 |
| docs/photos/reverb/102838549/102838549_7.jpg | https://rvb-img.reverb.com/i/7465326e-2085-4488-8157-4190f005c3e0.jpeg | 774f58ae3df340471edb347d6247eaab762abad3 | Rear, left (power, MIDI, cassette), 5712x4284 |
| docs/photos/reverb/102838549/102838549_8.jpg | https://rvb-img.reverb.com/i/03616e9d-79ff-4a23-a2f0-e4e1ad33548a.jpeg | 8d8eea8482634ce4bbb165ab4bcda08a3e975eff | Rear, middle (CV/gate inputs), 5712x4284 |
| docs/photos/reverb/102838549/102838549_9.jpg | https://rvb-img.reverb.com/i/75b1995c-368d-4300-8ce2-20918567e5c2.jpeg | ae122c71293a0db154423f2f03aa0cf2a0035fcf | Rear, right (audio outs), 5712x4284 |
| docs/photos/reverb/100339652/100339652_3.jpg | https://rvb-img.reverb.com/i/14753e94-cd98-4589-b815-a5dfe210ecb9.jpeg (https://reverb.com/item/100339652, Music Gear Vienna) | 9bacb756d4104969d6bacfd002f8fa420cba6a4c | Full panel, white background, displays lit, slight high angle, 3821x1811 |
| docs/photos/reverb/100339652/100339652_1.jpg | https://rvb-img.reverb.com/i/73b8f25b-2416-4da7-844f-455d36523dcb.jpeg | a3f4dfbe89b84de840534e017a3fa18fc443c980 | Left half close, straight-on, 3892x2905 |
| docs/photos/reverb/100339652/100339652_2.jpg | https://rvb-img.reverb.com/i/dd880ea6-5bd4-4465-a72f-5f1feabf5f2a.jpeg | 74cc5a17097949149c4f99daadb55e368d0fd6f9 | Middle close, 4032x2833 |
| docs/photos/reverb/100339652/100339652_4.jpg | https://rvb-img.reverb.com/i/9eb1ba41-2cd5-4636-b8ba-c75355e5f676.jpeg | c4918b09dc835f1d29b5db08407fd1aac0185e43 | Right half close, 3959x2884 |
| docs/photos/reverb/98053552/98053552_7.jpg | https://rvb-img.reverb.com/i/28fe40f9-d995-4c70-aa06-8af8ab0a9afd.jpg (https://reverb.com/item/98053552, Ishibashi Music) | 3d650c869cb0082a817f8bb57d1e65a6bb2a6bf6 | Left half straight-on, 2000x1333 |
| docs/photos/reverb/98053552/98053552_8.jpg | https://rvb-img.reverb.com/i/d784e3ca-dde1-4d9a-9d13-ed9f179f2dd4.jpg | 6c71f8f8a4267bc91e9c75d94e7e07d93bfe9298 | Right half straight-on, 2000x1333 |
| docs/photos/reverb/98053552/98053552_2.jpg | https://rvb-img.reverb.com/i/ca14f4eb-4bd5-42c0-86e2-2b137b911ea9.jpg | fe2a1b6dd8d5bcda6f39e7cde0000ca6863b897b | Rear left straight-on, 2000x1333 |
| docs/photos/reverb/98053552/98053552_3.jpg | https://rvb-img.reverb.com/i/da2dc537-b6e9-44a6-8e59-018ba7e0032c.jpg | 18c910dc52befc94f7b32bc3705331d28e6f0110 | Rear middle, 2000x1333 |
| docs/photos/reverb/98053552/98053552_4.jpg | https://rvb-img.reverb.com/i/d71f8c0a-85d7-477f-91a1-32c3cb5d3559.jpg | 541219c8e0fbe8d5fd0a71bc0dc3c7899842efd9 | Rear right, 2000x1333 |
| docs/photos/reverb/99263892/99263892_1.jpg | https://rvb-img.reverb.com/i/bde4f59a-8078-49dd-9e0e-78584794788e.jpg (https://reverb.com/item/99263892, Sir Analog Audio) | 1a9ad38af19fa206de929f0c15b08e58d4c66b2b | Left third straight-on, white bg, 2458x1843 |
| docs/photos/reverb/99263892/99263892_2.jpg | https://rvb-img.reverb.com/i/bf15e616-813a-49d8-9d84-05914688f704.jpg | 9de0f9f132536a076c9e3c615597099056403e25 | Middle third, 2458x1843 |
| docs/photos/reverb/99263892/99263892_3.jpg | https://rvb-img.reverb.com/i/a8a358b4-3ad2-45fc-b4df-f6b01c96ab24.jpg | ff4144d2ec6800b2fb106cd9174a44ba5a10c906 | Right third, 2458x1843 |
| docs/photos/commons/Oberheim_Xpander.jpg | https://upload.wikimedia.org/wikipedia/commons/7/72/Oberheim_Xpander.jpg | e6600d026febd198686769c7d73dc54b1e4d0cc4 | Commons "Oberheim Xpander.jpg", Bill.lay, 2010-01-30, **CC BY-SA 3.0**; straight-on but only 640x233 |
| docs/photos/commons/Oberheim_Xpander_rear.jpg | https://upload.wikimedia.org/wikipedia/commons/c/cc/Oberheim_Xpander_%28rear%29.jpg | 1c81e375adf0f7ab20d7cc1ceb34a6f47634f449 | Commons rear, 1816x656, derived from Brandon Daniel's Flickr photo, **CC BY-SA 2.0**; angled, upscaled, soft |
| docs/photos/vintagesynth/oberheim_xpander.jpg | https://www.vintagesynth.com/sites/default/files/2017-05/oberheim_xpander.jpg | 3855333fd139e2b46255c939337efe529e4dd540 | 600x392, angled. All rights reserved |
| docs/photos/vintagesynth/oberheim_xpander_rear_lg.jpg | https://www.vintagesynth.com/sites/default/files/2017-05/oberheim_xpander_rear_lg.jpg | 72edec12f92ade8dc233a2e2029526b7f71705b7 | Rear straight-on, 1200x326. All rights reserved |
| docs/photos/web/matrixsynth_2019_glossy/1.jpg | https://blogger.googleusercontent.com/img/b/R29vZ2xl/AVvXsEjFP51HhEoF5aqAwjXqclI7hozoa7JSu1eNrpwnjWJFEnQgkAF-RLbPXrQlYtEn3G5b80NaW8IsSfu0cw5Mp9AEYILS87Znf91NwUj_a_Z3-IqVtm9MxVqjPbMW3T5U05sKt8tK/s0/1.jpg (post https://www.matrixsynth.com/2019/09/oberheim-xpander-w-glossy-front-panel.html) | 85906831146048bc68aed7914018f31f706f384a | Glossy-panel unit, low angle, 1600x1179. All rights reserved |
| docs/photos/web/matrixsynth_2019_glossy/2.jpg | same post, .../s0/2.jpg | d478aacd917545caa14ad128e0beb5ad66971da5 | Close-up of mod-source buttons and legends, 1600x1200 (useful for key-cap shape) |
| docs/photos/web/matrixsynth_2019_glossy/3.jpg | same post, .../s0/3.jpg | 255422988c50fdce6ac64d22000cd1bb0b561a37 | Side cheek profile, 1600x1141 |
| docs/photos/web/matrixsynth_2019_glossy/4.jpg | same post, .../s0/4.jpg | d4d906ee34728c5da759ac984630730fc36ed220 | Rear, angled, 1600x652 |
| docs/photos/muzines/EMM_84_09_oberheim_xpa_large.jpg | https://www.muzines.co.uk/images_mag/articles/emm/EMM_84_09_oberheim_xpa_large.jpg | 301b5251feb56cb898a73dbd433c1474ff4e6cc4 | 1984 press photo, 636x312 |
| docs/photos/muzines/ES_84_10_xpand_your_h_large.jpg | https://www.muzines.co.uk/images_mag/articles/es/ES_84_10_xpand_your_h_large.jpg | 67261429e5f04c776a8c49ae665597565439d921 | 1984 press photo, 636x342 |

Also usable for layout: MAME `docs/mame/layout/oberheim_xpander.lay` (CC0, m1macrophage) has element
positions for the whole panel, and the owner's manual has panel drawings.

## 5. MAME sources (BSD-3-Clause; layouts CC0)

All from https://github.com/mamedev/mame at master commit **c2caa8969adbf3d599a655a162a6eac163fa63bc**
(2026-11-12T18:10:55Z), raw URLs `https://raw.githubusercontent.com/mamedev/mame/<commit>/src/<path>`.
The existing docs/mame/xpander.cpp, xpander_vb.cpp, xpander_vb.h are byte-identical to that commit.
Every .cpp/.h below carries `license:BSD-3-Clause`. The M6809 core is generated: `m6809make.py` turns
`base6x09.lst` + `m6809.lst` (and hd6309.lst / konami.lst for the variants) into `m6809.hxx` at build
time; all inputs are here, no generated file is in the repo. There are no CEM3374 or va_vco "CEM" files:
MAME models the 3374 inside xpander_vb.cpp (va_vco.* is the generic VCO, used in xpander.cpp for noise).

| File | Source path | SHA1 |
|---|---|---|
| docs/mame/cpu/m6809/6x09dasm.cpp | src/devices/cpu/m6809/6x09dasm.cpp | 4b0dafffc89f03609d8babe6d5b9638fc96ee3ef |
| docs/mame/cpu/m6809/6x09dasm.h | src/devices/cpu/m6809/6x09dasm.h | ac1a3d98d2b9efb38a875fd11f74a9f50dcc160d |
| docs/mame/cpu/m6809/base6x09.lst | src/devices/cpu/m6809/base6x09.lst | 863515a7d8e701a7f72f5db75d53df57e9566120 |
| docs/mame/cpu/m6809/hd6309.cpp | src/devices/cpu/m6809/hd6309.cpp | e2d63b5432f26bd44572f00c8c1e9d743d2b6013 |
| docs/mame/cpu/m6809/hd6309.h | src/devices/cpu/m6809/hd6309.h | 0b5a7d4d136293b3f87da83f412fbf0308a36c3c |
| docs/mame/cpu/m6809/hd6309.lst | src/devices/cpu/m6809/hd6309.lst | aabb741e0ed41c01bd99c657344321f67bd9bbfc |
| docs/mame/cpu/m6809/konami.cpp | src/devices/cpu/m6809/konami.cpp | 82cc203a43fbd1ef1c9527628a2d18fff289be79 |
| docs/mame/cpu/m6809/konami.h | src/devices/cpu/m6809/konami.h | d3eab8c49b68e8671414bb35b9e05985d387d18d |
| docs/mame/cpu/m6809/konami.lst | src/devices/cpu/m6809/konami.lst | fb2afd50847269dbe0f9319c20c1c32fa6c77054 |
| docs/mame/cpu/m6809/m6809.cpp | src/devices/cpu/m6809/m6809.cpp | bf40f52d503b239202de0ad3fdd5c9ca097cc2d4 |
| docs/mame/cpu/m6809/m6809.h | src/devices/cpu/m6809/m6809.h | 729a5e8e5ed26207d37be1b97b1a707e651bbe08 |
| docs/mame/cpu/m6809/m6809.lst | src/devices/cpu/m6809/m6809.lst | 258f372a3196a180bc2aa7566060c72221442334 |
| docs/mame/cpu/m6809/m6809inl.h | src/devices/cpu/m6809/m6809inl.h | e67e979129ace84c58d094c3448cafa753dc9e66 |
| docs/mame/cpu/m6809/m6809make.py | src/devices/cpu/m6809/m6809make.py | 6927da798e7cc1397d2f97bfafe73e983f169692 |
| docs/mame/sound/cem3372.cpp | src/devices/sound/cem3372.cpp | 6748d7e9107db0ef8b4bb2e0878b5442a4ffbe5a |
| docs/mame/sound/cem3372.h | src/devices/sound/cem3372.h | 4cf8f2a8bfaf9aaea60342149c6d1719ebf8ad8f |
| docs/mame/sound/va_eg.cpp | src/devices/sound/va_eg.cpp | 7b1748ee18311a291b582400a3691eae5e9d55d7 |
| docs/mame/sound/va_eg.h | src/devices/sound/va_eg.h | a6cfa530fb339840d5f2822c2315f40497372e8d |
| docs/mame/sound/va_vca.cpp | src/devices/sound/va_vca.cpp | f65946de0920125f216ee803b988ff6e2020dcbd |
| docs/mame/sound/va_vca.h | src/devices/sound/va_vca.h | a72d411dee25cc02265960c12589faffa00078b8 |
| docs/mame/sound/va_vcf.cpp | src/devices/sound/va_vcf.cpp | 3bb34e20b3c64e1ff738b0b399768077f2e55535 |
| docs/mame/sound/va_vcf.h | src/devices/sound/va_vcf.h | 19a6599e9910da656fc06b23e718c96eacdb3ec0 |
| docs/mame/sound/va_vco.cpp | src/devices/sound/va_vco.cpp | 3b5560afeb53a8d4a74b7d6bd62c27c896a00514 |
| docs/mame/sound/va_vco.h | src/devices/sound/va_vco.h | 6ada345594a8fdd826f2b9cf4745b04f80b09aab |
| docs/mame/sound/va_ops.cpp | src/devices/sound/va_ops.cpp | 909fb67b42aca2d8d7459f397c3292a878b00a84 |
| docs/mame/sound/va_ops.h | src/devices/sound/va_ops.h | 8e190df7e682d0ce6721d0e84a1b42f863cdec6c |
| docs/mame/sound/flt_rc.cpp | src/devices/sound/flt_rc.cpp | 4cec265c40151f479567862f04ee9f1b5fcc2846 |
| docs/mame/sound/flt_rc.h | src/devices/sound/flt_rc.h | 864c693dc676f4b5cf24c1b2850d94aa1a61aca8 |
| docs/mame/sound/mixer.cpp | src/devices/sound/mixer.cpp | 823af601281948c192121267ac2630406956470f |
| docs/mame/sound/mixer.h | src/devices/sound/mixer.h | c2417e74be74ebd3c7da5bf30000e9e5c8f630ce |
| docs/mame/sound/cem3310.cpp | src/devices/sound/cem3310.cpp | 26ff014ca7a0f0b445d8e560be92dba468fdf6c7 |
| docs/mame/sound/cem3310.h | src/devices/sound/cem3310.h | e823c99f006411b2fa0d5169f363b0058a942a1b |
| docs/mame/sound/cem3320.cpp | src/devices/sound/cem3320.cpp | 6c4bf7a035b97e0f1bc13bf96fa04172594de7fe |
| docs/mame/sound/cem3320.h | src/devices/sound/cem3320.h | eb5674548ec579854d79fc6438e66a0e6cf6d058 |
| docs/mame/sound/cem3340.cpp | src/devices/sound/cem3340.cpp | 0141347eaf779b48cf5a8055d5270f4635ad356b |
| docs/mame/sound/cem3340.h | src/devices/sound/cem3340.h | 03d320f34fb0dc2999477c9017a49991548f269c |
| docs/mame/sound/cem3360.cpp | src/devices/sound/cem3360.cpp | 507e392cdb52d1bb45f19b63359bac8da1f3702f |
| docs/mame/sound/cem3360.h | src/devices/sound/cem3360.h | 29250922a16732796addafac6de0913f7d009c1a |
| docs/mame/sound/cem3394.cpp | src/devices/sound/cem3394.cpp | ed2635939d5a49b4f1529395ce21181bf7df094e |
| docs/mame/sound/cem3394.h | src/devices/sound/cem3394.h | 86e2ad4a60b18672fc64362a9870d326b0d94161 |
| docs/mame/machine/pit8253.cpp | src/devices/machine/pit8253.cpp | aeb60f5eca625ab6468168b6130f76b7ef837b29 |
| docs/mame/machine/pit8253.h | src/devices/machine/pit8253.h | f7ed763f098a441b4f452d07a7c4d7040e598833 |
| docs/mame/machine/6850acia.cpp | src/devices/machine/6850acia.cpp | 90c8a0f553a95b96d29f0d82bed788c44a76d470 |
| docs/mame/machine/6850acia.h | src/devices/machine/6850acia.h | d49139c9f9c7c6017e8cd9950276003c0ef4c478 |
| docs/mame/machine/adc0804.cpp | src/devices/machine/adc0804.cpp | 3e4c5c9ef0deb3d7397b9ab6d964f9dddd23423e |
| docs/mame/machine/adc0804.h | src/devices/machine/adc0804.h | a2c306a96a208b4e8dbbe60b5eccf69b7d302580 |
| docs/mame/machine/7474.cpp | src/devices/machine/7474.cpp | 89c473a9861b6f90f18374e80293894acddc33bb |
| docs/mame/machine/7474.h | src/devices/machine/7474.h | 6a4232f3ab0988446f384df23736466315a23588 |
| docs/mame/machine/quadmouse.cpp | src/devices/machine/quadmouse.cpp | f63a8f7f05b93eb3432ae100b685224501805ae8 |
| docs/mame/machine/quadmouse.h | src/devices/machine/quadmouse.h | 882ffa37b4de89b39899ec157a673cec34738a0c |
| docs/mame/machine/output_latch.cpp | src/devices/machine/output_latch.cpp | e4462150f3d9aa000e6918ddb7aa9d6055d21206 |
| docs/mame/machine/output_latch.h | src/devices/machine/output_latch.h | 7aaf8466510d30b16fec9e96189b99c32384b279 |
| docs/mame/machine/clock.cpp | src/devices/machine/clock.cpp | de9e9cf42fd28fe16471721cd5d766d2f2c5cfc2 |
| docs/mame/machine/clock.h | src/devices/machine/clock.h | da9434c6afd188dce3435f5ef4ba0f7fb11a3b68 |
| docs/mame/machine/nvram.cpp | src/devices/machine/nvram.cpp | 72048ad49b93ced7f5c8afe9b0a44483d6116ca7 |
| docs/mame/machine/nvram.h | src/devices/machine/nvram.h | b55dd877612f860277bcafdd32281fefbb134252 |
| docs/mame/machine/rescap.h | src/devices/machine/rescap.h | e53cf369dbc705d7a7f4bd1d678dd23350ef3be5 |
| docs/mame/video/pwm.cpp | src/devices/video/pwm.cpp | edbe0b0185542581bbcfe606a1a0c65a7f7343cc |
| docs/mame/video/pwm.h | src/devices/video/pwm.h | 39055348d051d73fddb39eb35e5867bdb50b5d91 |
| docs/mame/matrixsynth_kbd.cpp | src/mame/oberheim/matrixsynth_kbd.cpp | db1fa0e5d7e4fd4d5017c327fb1f92d8d863daa8 |
| docs/mame/matrixsynth_kbd.h | src/mame/oberheim/matrixsynth_kbd.h | 0510ddab02500c8a449328551ba0e7e553e70025 |
| docs/mame/layout/oberheim_xpander.lay | src/mame/layout/oberheim_xpander.lay | fb8084d149dc75a7f9c66ccde532c30439ad81e5 |
| docs/mame/layout/oberheim_matrix12.lay | src/mame/layout/oberheim_matrix12.lay | 38ba15693ac87745c2b9e562f664f320d96aaf09 |

## 6. Trademark

| File | URL | SHA1 | Note |
|---|---|---|---|
| docs/trademark/notes.md | USPTO tmsearch + TSDR, EUIPO eSearch, TMview (URLs inside) | 7bd0fdb2d4228525604972c916380ea98b541a7d | XPANDER live for Marion Systems, LLC: US Reg. 7475329 (class 9 music software, reg. 2024-08-13), US Serial 98434046 (class 15, allowed, pending SOU), EUTM 019074503 (class 15, reg. 2025-01-09), UK00004095101 (class 15). Oberheim's US Reg. 1371954 (1985) cancelled 1992 |

## Looked for, not found

- **CEM3372 / CEM3374 die shots or reverse-engineered internals**: none published. Stromeko
  (synth.stromeko.net/DIY.html#CEMpics) has die photos of CEM3310/3320/3330/3340 only, and notes CEM chips
  are Interdesign bipolar arrays matching the 1980 "201 Analog IC Designs" brochure (not found online as a
  file). No Curtis patent naming the 3372/3374 found via web search (Google Patents by inventor not tried in depth).
  sdiy.info has no CEM3372/CEM3374 article pages (only the datasheet files). Mod Wiggler thread
  "CEM3320 and CEM3372 differences" (t=212545) could not be fetched (blocked). Tindie sells a "CEM 3374 VCO
  replacement board" (alanbog), no schematic published.
- **Curtis originals on curtiselectromusic.com**: Wayback has only 3310/3320/3330/3340/3365+ long sheets,
  no 3372/3374. synthfool Other_Misc/CEM_chips.zip has 3310/3320/3340 only (not kept).
- **Alfa AS3374**: does not exist; only AS3372E.
- **Analog Devices AD7523 original**: analog.com timed out; Wayback had no capture. Intersil second-source sheet kept instead.
- **MP7614 / HS3140 (14-bit DAC hybrid, parts list "HS3140C-4 / MP7614KN")**, **UDN6118** (VFD driver),
  **LM329** (6.9 V reference): only on alldatasheet / datasheet.hk-style mirrors behind Cloudflare or
  download walls, or TI pages gone; not fetched.
- **8253 standalone Intel sheet**: only on Cloudflare mirrors; used the 1979 handbook section instead.
- **Service bulletins / software release notes (voice 1.4, main 1.2, voice board revs A-D)**: no Oberheim
  document found on archive.org, synthfool, fdiskc or soundprogramming. The Xpantastic Yahoo-group archive
  (synth-diy.org/yg-archives2/group/xpantastic, files/4_Documents incl. "16_Xpander.MIDI-spec.txt") likely
  has user notes, but the site sits behind an Anubis anti-bot wall that asks AI agents to stay out, so it
  was not read. A person can browse it by hand.
- **soundprogramming.net**: its Xpander page links /manuals/Oberheim_Xpander_Manual.pdf and
  /manuals/Oberheim_Xpander_ServiceManual.pdf, but both return an HTML page now (no files).
- **manuals.fdiskc.com/flat/**: has Xpander owner's and service manuals, but they are byte/size-identical
  to copies above (service = service_b sha1 2b791dbe...; owner's = synthmanual copy, 12068884 bytes).
- **Cleaner schematics**: none better than service_a (see section 3). Pages 37-38 of the synthfool set are missing.
- **High-res free-licensed panel photo**: Commons has only the 640x233 front and a soft 1816x656 rear; no
  Flickr CC search done (needs API key). Reverb photos are the usable high-res references (all rights reserved).
