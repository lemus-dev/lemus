# SH-101: sources

What the SH-101 emulation was built and checked against. The files themselves are private
(`private/sh101/`, never in this repo). Each one is listed with its SHA1, size and source id in
`private/sh101/sources-sha1.tsv`, which `tools/sources_manifest.py` writes:

    .venv/bin/python -I machines/sh101/tools/sources_manifest.py

Most documents were fetched on 2026-10-06; later additions are dated below. Every download went
into its own empty directory and was checked before it was copied in. Nothing from a download was
run. Code was read as text only.

Per-file URLs, where they were recorded, are in:
- `docs/sources.md`: the service notes, owner's manuals, MC-202 documents, Roland's service
  bulletins, the 1982 catalogue, mod manuals and data sheets (URL and SHA1 for each);
- `docs/photos/sources.md`: panel and board photos (URL and licence for each);
- the header of each `.txt` under `docs/papers/`, and each folder's README;
- `reference/shootout/index.json` and each clip folder's `source.json`.

## The ROM

`tmp80c49p-6-7301.ic6`, the TMP80C49P-6-7301 CPU's 2048-byte mask ROM. SHA1
a84bb1758582dbb800001db3663eaf3fb9d0e25d, CRC32 4d9cb13c. It is Joe Britt's dump, `80C49_SH101.bin`
in https://github.com/joebritt/SH101_Disassembled (commit 708cde0, 2018), which has no licence.
MAME has no SH-101 set. The ROM is not in this repo; `docs/firmware.md` says how it was checked.

## Research sources (S1-S38)

The ids are `research.md` §0's. Paths are under `private/sh101/docs/papers/` unless they start
with `docs/`. "Files" counts every file with that id in the tsv, including the `.txt` beside each
PDF or page, transcriptions and crops. The SHA1 is given where one file is the source.

| # | Source | URL(s) | Local copy | Files | SHA1 |
|---|---|---|---|---|---|
| S1 | Roland, *SH-101 Service Notes*, 1st ed., 1982-11-01 | https://archive.org/download/synthmanual-roland-sh-101-service-notes/rolandsh-101servicenotes.pdf (and `_text.pdf`); a second scan at 400 dpi: http://privat.bahnhof.se/wb447909/dinsync/service_manuals/SH-101.pdf | `docs/sh101-service-notes*`, `docs/schematic/` (page images, crops), `sh101/sh101_service_notes_numbers_transcribed.txt` | 114 | main PDF 417ab2bcb24e30e13e95cbb52e860b8f3b5d7503 |
| S2 | Roland, *SH-101 Owner's Manual*, 1982 | https://archive.org/details/synthmanual-roland-sh-101-owners-manual; https://archive.org/details/roland-monophic-synthesizer-sh-101-owners-manual-1982 | `docs/sh101-owners-manual*`, `sh101/roland_sh101_owners_manual_1982_*` | 7 | main PDF 7352fbc5fb23c0395a1c1c00d33269bd4310ff5e |
| S3 | Curtis, *CEM3340/3345 VCO* data sheet, 1980 | https://www.curtiselectromusic.com/family-law.html; copy https://static.miraheze.org/sdiywikiwiki/1/1c/CEM3340-3345_long.pdf; short sheet https://static.wikitide.net/sdiywikiwiki/f/ff/CEM3340_datasheet.pdf | `docs/datasheets/cem3340-curtis-*`, `vco_cem3340/curtis_*` | 6 | long sheet 340b2f48a9c73f1e2af10b771615aac43edaef8d |
| S4 | Alfa AS3340 and AS3340HYB data sheets, Alfa's VCO/PWM tip, CoolAudio V3340, R. Hordijk *Tuning the AS3340* | https://web.archive.org/web/20240422233345id_/https://alfarzpp.lv/eng/sc/AS3340.pdf; https://www.alfatriode.lv/eng/sc/AS3340-HYB.pdf; https://web.archive.org/web/20200717231222id_/http://www.alfarzpp.lv/eng/sc/AS3340%20tip%20VCO%20_%20PWM%20.pdf; https://web.archive.org/web/2024id_/https://www.coolaudio.com/docs/COOLAUDIO_V3340_DATASHEETS.pdf; https://web.archive.org/web/20220925202454id_/http://www.alfarzpp.lv/eng/sc/Tuning%20the%20AS3340.pdf | `docs/datasheets/as3340-alfa.*`, `vco_cem3340/{alfa,coolaudio,hordijk}_*` | 12 | |
| S5 | T. Wiltshire (Electric Druid), *CEM3340 VCO designs* (2016), *A study of sub-oscillators* (2017) | https://electricdruid.net/cem3340-vco-voltage-controlled-oscillator-designs/; https://electricdruid.net/a-study-of-sub-oscillators/; https://electricdruid.net/wp-content/uploads/2017/06/SH101SubOsc.jpg | `vco_cem3340/wiltshire_*`, `sh101/electricdruid_{study,sh101_sub}*` | 11 | |
| S6 | CEM3340 web notes: SDIY wiki, R. Holmes, Kassutronics, T. Henry *VCO Maximus* | https://sdiy.info/wiki/CEM3340; https://richardsholmes.com/topics/synth/howto/3340/; https://kassu2000.blogspot.com/2018/06/vco-3340.html; http://www.birthofasynth.com/Thomas_Henry/Pages/VCO_Maximus-Detail.html | `vco_cem3340/{sdiy,holmes,kassutronics,henry}_*` | 8 | |
| S7 | CEM3340 forum extracts: Mod Wiggler (via the Wayback Machine), Electric Druid comments | Mod Wiggler t=200701, 224171, 224473, 228562, 230917, 231130, 231479, 233605 (each post's permalink is in the file); https://electricdruid.net/cem3340-vco-voltage-controlled-oscillator-designs/ | `forums/cem3340_*` | 2 | |
| S8 | IR3109 data sheet excerpt (Roland, 1982-05-10); Alfa *AS3109* data sheet | https://www.openmusiclabs.com/files/ir3109_datasheet.jpg; https://alfatriode.lv/eng/sc/AS3109.pdf | `docs/datasheets/{as3109-alfa,ir3109-roland}*`, `ota_ir3109/alfa_*`, `ota_ir3109/img/` | 7 | |
| S9 | Open Music Labs: IR3109 reverse engineering (Mod Wiggler thread), SH-101 schematic errors, BA662 reverse engineering, *Minimizing distortion in OTAs* | https://modwiggler.com/forum/viewtopic.php?t=126397 (read via https://web.archive.org/web/20240616202523/https://www.modwiggler.com/forum/viewtopic.php?t=126397); http://wiki.openmusiclabs.com/wiki/Sh101; https://www.openmusiclabs.com/wiki/BA662/; https://www.openmusiclabs.com/wiki/x0xb0x/; http://www.openmusiclabs.com/files/otadist.pdf | `forums/ir3109_modwiggler_reverse_engineering_thread.txt`, `ota_ir3109/openmusiclabs_*`, `vca_ba662/openmusiclabs_*`, `opensource/diy_openmusiclabs_sb01/` | 29 | |
| S10 | T. Wiltshire, *Roland filter designs: IR3109/AS3109* (2025) and the *AS3109 SH-101 filter* Rev. 2 (CC BY-NC-SA 4.0) | https://electricdruid.net/roland-filter-designs-with-the-ir3109-or-as3109/ | `ota_ir3109/electricdruid_*`, `opensource/diy_electricdruid/` | 11 | |
| S11 | A. Macaulay (AMSynths): IR3109, BA662 and IR3R01 articles; AM8101, AM8110, AM8112, AM8114 pages | https://amsynths.co.uk/2022/04/06/all-about-the-ir3109-chip/; https://amsynths.co.uk/home/products/filters/am8101-sh-101-filter/; https://amsynths.co.uk/2022/09/06/am8110-sn-101-vco/; https://amsynths.co.uk/home/products/amplifiers/am8112-sn-101-vca-adsr/; https://amsynths.co.uk/home/products/modulators/am8114-sh-101-modulator/; https://amsynths.co.uk/2018/01/07/all-about-the-ba662-chip/; https://amsynths.co.uk/2019/05/09/roland-ir3r01-clone/ | `*/amsynths_*`, `opensource/diy_amsynths_am8101/` | 22 | |
| S12 | Other IR3109 material: atosynth, minisystem *Jove*, Syntaur, Obsolete Technology, Analogue Renaissance, forum extracts | http://atosynth.blogspot.com/2017/10/juno-filter-analysis.html; http://atosynth.blogspot.com/2017/10/jupiter-6-filter-clone-initial.html; https://github.com/minisystem/Jove; http://www.syntaur.com/DIYdocs/DIY0021.html; https://obsoletetechnology.wordpress.com/projects/80017a-vcfvca-teardown/; http://www.analoguerenaissance.com/AR3109/; https://modwiggler.com/forum/viewtopic.php?t=248642; https://www.kvraudio.com/forum/viewtopic.php?t=370838 | `ota_ir3109/{atosynth,minisystem,syntaur,obsoletetechnology,analoguerenaissance}_*`, `forums/ir3109_{modwiggler_other,kvr}*` | 15 | |
| S13 | OTA papers: Bogason and Werner, DAFx-17; Köper, Holters, Esqueda and Parker, DAFx-22; Huovilainen, DAFx-05 | URL not recorded | `ota_ir3109/{bogason,koeper,huovilainen}_*` | 6 | |
| S14 | H. Gratz, *Operational Transconductance Amplifiers* (2008) | http://synth.stromeko.net/diy/OTA.pdf. The author asks that it not be mirrored: it is kept privately and must stay out of any public release. | not listed here | 3 | |
| S15 | BA662 family: Rohm BA6110, CoolAudio V662A, Alfa AS662D, TI LM13700, Intersil AN6668, Electric Druid OTA articles, atosynth pinout, OSH Park clone | http://www.firstpr.com.au/rwi/dfish/TT-303/click-fix/BA6110-datasheet-with-SIP-9.pdf; https://www.coolaudio.com/docs/Datasheet_CA_V662A_2020-10-12_Rev.0_Signed.pdf; https://www.alfatriode.lv/eng/sc/AS662D.pdf; https://www.ti.com/lit/ds/symlink/lm13700.pdf; https://electricdruid.net/wp-content/uploads/2025/09/AN6668-CA3080-Details.pdf; https://electricdruid.net/design-a-eurorack-vintage-vca-with-the-lm13700/; https://electricdruid.net/comparing-otas/; http://atosynth.blogspot.com/2017/07/ba662-pinout.html; https://oshpark.com/shared_projects/QCnNP4oK | `vca_ba662/*`, `docs/datasheets/{as662d-alfa,ba6110fs-rohm}.*` | 22 | |
| S16 | BA662 forum extracts (Mod Wiggler, Adafruit, VSE; a short quotation of R. Whittle, whose page asks not to be mirrored and was not saved) | https://modwiggler.com/forum/viewtopic.php?t=102698; the others by thread number in the file | `forums/ba662_forum_extracts.txt` | 1 | 269e7c09cc2313d5dae1c1d86c57e7a17022e3d2 |
| S17 | V. Zavalishin, *The Art of VA Filter Design* 2.1.2; Zavalishin 2008, topology-preserving transform | URL not recorded (copied from the TB-303 project) | `zavalishin/` | 4 | |
| S18 | W. Pirkle, *AN-4 Virtual Analog Filters*, *AN-6 VA Diode Ladder* | URL not recorded (copied from the TB-303 project) | `pirkle/` | 4 | |
| S19 | T. E. Stinchcombe, Moog ladder analysis (2008); Korg MS-10/MS-20 filters (2009) | 2009: https://www.timstinchcombe.co.uk/synth/MS20_study.pdf; 2008: URL not recorded (TB-303 project copy) | `ladder_models/{Moog_ladder_tf,stinchcombe_}*` | 4 | |
| S20 | A. Huovilainen, DAFx-04; MSc thesis, TKK 2010; Csound `moogladder` excerpt (LGPL) | http://lib.tkk.fi/Dipl/2010/urn100219.pdf; https://raw.githubusercontent.com/csound/csound/master/Opcodes/newfils.c; DAFx-04: URL not recorded (TB-303 project copy) | `ladder_models/{huovilainen,lazzarini}_*` | 5 | |
| S21 | Other ladder and nonlinear-filter papers | New ones: https://aaltodoc.aalto.fi/server/api/core/bitstreams/7ebb6719-8c32-4e23-97ce-b6ebac589990/content; https://aaltodoc.aalto.fi/bitstream/handle/123456789/14420/article6.pdf; https://www.apsipa.org/proceedings/2017/CONTENTS/papers2017/15DecFriday/FP-01/FP-01.4.pdf; https://www.acoustics.ed.ac.uk/wp-content/uploads/AMT_MSc_FinalProjects/2012__Daly__AMT_MSc_FinalProject_MoogVCF.pdf; https://quod.lib.umich.edu/cgi/p/pod/dod-idx/preserving-the-structure-of-the-moog-vcf-in-the-digital.pdf?c=icmc&format=pdf&idno=bbp2372.2007.062. TB-303 project copies: URL not recorded | `ladder_models/` | 20 | |
| S22 | mystran, "Cheap non-linear zero-delay filters", KVR | https://www.kvraudio.com/forum/viewtopic.php?t=349859 | `forums/ota_kvr_mystran_cheap_nonlinear_zdf.txt` | 1 | 340546cec83a5e6d5b6fa2ebde254103ca2dca04 |
| S23 | Antialiasing papers copied from the TB-303 project (ADAA, polyBLEP, BLAMP and others) | URL not recorded | `antialiasing/` | 16 | |
| S24 | New antialiasing papers (BLIT, minBLEP, DPW, PTR, EPTR, hard sync and others) | https://ccrma.stanford.edu/~stilti/papers/blit.pdf; http://www.cs.cmu.edu/~eli/papers/icmc01-hardsync.pdf; https://mac.kaist.ac.kr/pubs/ValimakiNamSmithAbel-taslp2010.pdf; https://aaltodoc.aalto.fi/server/api/core/bitstreams/0b7c5649-cd2b-476f-ba53-4e33a8103f06/content; https://home.mit.bme.hu/~bank/publist/smc13.pdf; https://quod.lib.umich.edu/cgi/p/pod/dod-idx/new-approaches-to-digital-subtractive-synthesis.pdf?c=icmc;idno=bbp2372.2005.042;format=pdf; https://aaltodoc.aalto.fi/bitstream/handle/123456789/30723/article5.pdf; https://www.ness.music.ed.ac.uk/wp-content/uploads/2016/12/ISMRA2016-48-1.pdf; https://dafx17.eca.ed.ac.uk/papers/DAFx17_paper_100.pdf; https://dafx12.york.ac.uk/papers/dafx12_submission_37.pdf; https://arxiv.org/pdf/2608.27648; http://www.radioeng.cz/fulltexts/2012/12_01_0056_0062.pdf; https://aaltodoc.aalto.fi/bitstreams/803386d5-d47a-47b2-8f09-e25dee6618d6/download | `antialiasing/` | 26 | |
| S25 | VA method papers (Werner, Holters and Zölzer, DAFx stability and neural VCO papers) | URL not recorded (copied from the TB-303 project) | `va_methods/` | 10 | |
| S26 | T. Wiltshire, *SH-101 replacement processor: a feasibility study* (2016) | https://electricdruid.net/sh101-replacement-processor-a-feasibility-study/ | `sh101/electricdruid_sh101_replacement_processor_2016.*` | 2 | page 2ba9b4ed882279d83e53740551fb9430ff02b2a9 |
| S27 | Tubbutec *SH-1oh1* user and installation manuals (2020) | https://tubbutec.de/files/SH-1oh1_User_Manual.pdf; https://tubbutec.de/files/SH-1oh1_Installation_Manual.pdf; https://tubbutec.de/sh-1oh1/; https://modwiggler.com/forum/viewtopic.php?t=267752 | `docs/sh101-mod-tubbutec-*`, `sh101/tubbutec_*`, `opensource/diy_tubbutec_sh1oh1/` | 15 | |
| S28 | Sound On Sound: Reid, *Synth Secrets* (2001) and *SH-01A* review (2018); Johnson, *Retrozone* (1994); Nagle, *SH-101 plug-out* (2014) | https://www.soundonsound.com/techniques/roland-sh101-arp-axxe-brass-synthesis; https://www.soundonsound.com/reviews/roland-sh-01a; https://www.soundonsound.com/reviews/roland-sh101-retrozone; Nagle: URL not recorded | `sh101/sos_*`, `context/review_sos_*` | 10 | |
| S29 | Period press via muzines.co.uk (1982-1985) | https://www.muzines.co.uk/articles/synthesiser-update/4444; https://www.muzines.co.uk/articles/roland-sh-101/4776; https://www.muzines.co.uk/articles/a-selective-survey-of-lead-line-synthesizers/3138; https://www.muzines.co.uk/articles/short-circuit/8994 | `sh101/muzines_*` | 8 | |
| S30 | Later Roland documents: PLUG-OUT manual, Roland Cloud SH-101 manual, German brochure, Roland AU *Icon Series* | https://www.rolandcloud.com/getmedia/2559c811-de8e-4a43-a50a-00d52d39fbb8/PlugoutSoftSynth-SH101-help-e03-W.pdf?ext=.pdf; https://static.roland.com/assets/media/pdf/PG-SH101_Manual_eng06_W.pdf; http://www.rolandmuseum.de/synth/html/SH101/Roland_SH101_ro.php?l=d; https://rolandcorp.com.au/blog/roland-icon-series-sh-101-synthesizer | `sh101/roland*`, `docs/roland-plugin/SH-101_*` | 12 | |
| S31 | SH-101 forum extracts: Mod Wiggler (10 threads), Vintage Synth Explorer, KVR | Mod Wiggler t=94957, 99094, 166240, 171228, 188458, 236634, 246305, 267752, 272059, 274655 (via the Wayback Machine); https://forum.vintagesynth.com/viewtopic.php?t=36010; https://forum.vintagesynth.com/viewtopic.php?t=57137; https://www.kvraudio.com/forum/viewtopic.php?f=102&t=465195; https://www.kvraudio.com/forum/viewtopic.php?t=543399 | `forums/sh101_*` | 2 | |
| S32 | Mods: Kenton filter-socket and MIDI kits, Synhouse MIDIJACK (2002), F. Anwander's SH-101 page (2017), Maffez MS-1 mods | https://kentonuk.com/wp-content/uploads/2019/06/SH-101_1_socket_kit-doc.pdf; https://kentonuk.com/wp-content/uploads/2019/06/S1015203.pdf; https://kentonuk.com/wp-content/uploads/2025/06/SH-101-2-socket-kit-user-20250620.pdf; http://www.synhouse.com/SH-101.html; https://www.florian-anwander.de/roland_sh101/; https://maffez.com/?page_id=674 | `docs/sh101-mod-kenton-*`, `sh101/{kenton,synhouse,anwander,maffez}_*` | 14 | |
| S33 | TAL, *TAL-BassLine-101* user manual (2020) | https://tal-software.com/downloads/docs/TAL-BassLine-101-UserManual.pdf | `sh101/tal_bassline101_user_manual_2020.*` | 2 | PDF e2caf49f9a77d76631e0dc9cb96dcc66c1bee1ae |
| S34 | Open-source code, read as text only (licences in `research.md` §10.1) | KR-106 https://github.com/kayrockscreenprinting/ultramaster_kr106 (bc15cae, GPL-3.0); MAME https://github.com/mamedev/mame (c6eac87, BSD-3-Clause files); LabSeven https://github.com/LabSevenDevVCVRack/LabSeven_VCVRack_modules (b74c73a, CC BY-NC-SA 4.0); Faust https://github.com/grame-cncm/faustlibraries (bf9f71a, MIT for `ve.lowpassLadder4`); https://github.com/surge-synthesizer/sst-filters (3431696, GPL-3.0+); TAL-NoiseMaker in https://github.com/DISTRHO/DISTRHO-Ports (d3b62da, GPL-2.0); https://github.com/pendragon-andyh/junox (0b5ed92, GPL-3.0); https://github.com/pendragon-andyh/Juno60 (beab865, MIT); https://github.com/Michi71/PicoVintageSynthCollection (4c49010, GPL-3.0+); https://github.com/mattWoolly/mwAudio101 (0be235f, GPL-3.0+) | `opensource/` (one folder per project, each with a README: URL, commit, licence) | 124 | |
| S35 | DIY clones and mods: OML/Superlative SB01, Electric Druid AS3109 filter, AMSynths AM8101, sibomots/RolandSH101 (BSD-3), Nova-mod, Circuitbenders, Kenton; Din Sync's RE-101 build guide and BOM (added 2026-10-09) | https://github.com/sibomots/RolandSH101 (9e9ce9c); https://manuals.playsuperlative.com/SB01/; https://dl.lojinx.com/analoghell/RolandSH101-NovaMod.pdf; https://www.circuitbenders.co.uk/synthmod/SH101.html; http://machines.hyperreal.org/manufacturers/Roland/SH-101/mods/; https://shop.re-303.com/files/files/RE101-BUILD-AND-BOM.zip; the reference-only projects in `opensource/diy_reference_only/README.txt` | `opensource/diy_*`, `opensource/dinsync_re101/`, `docs/sh101-mod-novamod-*`, `forums/opensource_diy_sh101_extracts.txt` | 38 | |
| S36 | Product context: Behringer MS-101/MS-1, Roland SH-01A, System-1 plug-out, Roland Cloud, ZEN-Core and FANTOM pages, reviews and news | Twelve in `context/README.txt`, e.g. https://maffez.com/wp-content/uploads/2020/05/BEHRINGER_MS_MODDING_GUIDE_V2.pdf, https://maffez.com/wp-content/uploads/2022/09/Behri_MS1_VCF.pdf, https://static.roland.com/assets/media/pdf/SH-01A_eng03_W.pdf, https://www.roland.com/us/products/rc_sh-101/; forum threads in each `forums/context_*.txt` header. News and review pages: URL not recorded | `context/`, `forums/context_*`, `docs/roland-plugin/EXM004_*` | 70 | |
| S37 | F. Anwander, *Roland filter versions* | URL not recorded (copied from the TB-303 project) | `forums/anwander_roland_filters.*` | 2 | page 37edfedfad3f32e77f0e6d08a4c6a60d52d36640 |
| S38 | Overviews: Wikipedia, Vintage Synth Explorer, Synthtopia's MS-101 vs SH-101 shoot-out (2019) | https://en.wikipedia.org/wiki/Roland_SH-101; https://www.vintagesynth.com/roland/sh-101; https://www.synthtopia.com/content/2019/02/06/101-shootout-behringer-ms-101-clone-vs-roland-sh-101/ | `sh101/{wikipedia,vintagesynth,synthtopia}_*` | 6 | |

One file has no source id: `forums/opensource_elektronauts_ir3109_emulation.txt`, an Elektronauts
thread on IR3109 emulation (https://www.elektronauts.com/t/roland-style-ir3109-filter-software-emulation/203008).

## Recordings of real units

Clips cut from public recordings, collected 2026-10-06 (`private/sh101/reference/shootout/`,
indexed in its `index.md` and `index.json`). They are used to compare behaviour: resonance,
self-oscillation, envelopes, VCA modes, the sources, portamento, LFO, arpeggio and sequencer
timing. Full-length downloads were deleted after cutting. The audio is lossy unless stated. All
rights stay with the uploaders; the clips are private and never published.

"Clips" counts every clip from the source; "SH-101" counts those of a real SH-101. YouTube links
are `https://www.youtube.com/watch?v=<id>`. Per-file SHA1s are in `private/sh101/sources-sha1.tsv`
(source `yt:<id>`, `soundcloud:`, `freesound:` or `rec:<slug>`).

| Channel | Source | Year | Clips | SH-101 | What it shows |
|---|---|---|---|---|---|
| AutomaticGainsay | `6QSJJfHEO_Q` | 2018 | 15 | 15 | Part 1: oscillator waves, pulse width, sub |
| AutomaticGainsay | `8jCX_EUNocI` | 2018 | 12 | 12 | Part 2: oscillator, range, noise |
| AutomaticGainsay | `VQXq7OmZCtY` | 2018 | 18 | 18 | Part 3: filter sweeps at several RES, self-oscillation played from the keys |
| AutomaticGainsay | `rA93zzTjk1A` | 2018 | 10 | 10 | Part 4: modulation |
| AutomaticGainsay | `GvF-Bddmj9k` | 2018 | 8 | 8 | Part 5: VCA GATE vs ENV, envelope |
| AutomaticGainsay | `5k-poyZyg2w` | 2018 | 7 | 7 | Part 6: sequencer, arpeggio |
| TONELAB | `zR3U7za05dI` | 2022 | 34 | 34 | guided tour: each slider, self-oscillation with KYBD |
| Reverb | `M2LyzzGdpmQ` | 2016 | 19 | 19 | dealer demo |
| V C O | `Kip0OV_uJxc` | 2017 | 18 | 18 | direct-sound demo, no talking |
| once upon a synth | `PFXO4ZRsVC4` | 2017 | 17 | 17 | tutorial: controls, portamento |
| Failed Muso | `6NqaegXGbtE` | 2019 | 26 | 14 | SH-101 vs MS-101 at the same settings |
| 3rdStoreyChemist | `dM2YUnsLgms` | 2018 | 24 | 13 | SH-101 vs SH-01A |
| Perfect Circuit | `biuWnvJ0f7I` | 2019 | 24 | 12 | SH-101 vs MS-1: each source alone, LFO rate sweep |
| Friendly Noise | `eStzYAZoOAI` | 2022 | 18 | 9 | SH-101 vs Model 82, with panel photos per chapter |
| MR TUNA Music | `6EIBdmXz15o` | 2020 | 15 | 8 | SH-101 vs SYSTEM-8 plug-out |
| gstormelectro | `SsHcY4YQOfw` | 2021 | 14 | 7 | SH-101 vs a 101-VCO Eurorack clone |
| loopop | `3JBFoXgtHvE` | 2019 | 12 | 6 | SH-101 vs MS-101, blind test |
| Meuchine | `RNldqRtdt0Q` | 2022 | 6 | 6 | sound exploration, no FX |
| MossPrinten | `BsvPfpOZCkA` | 2014 | 5 | 5 | line-recorded sound test |
| Hi Entropy | `9rVlcP5Bslg` | 2024 | 9 | 4 | SH-101 vs MS-1 MK II |
| Italo Brutalo | `Cr4Le00_MuA` | 2025 | 20 | 4 | SH-101, MC-202, SH-01A, S-1 and Roland Cloud |
| Bethany Griffin | `qTUrWmUgqq0` | 2018 | 4 | 4 | arpeggio and sequencer |
| zenobic | `y8wlIS8TCcA` | 2022 | 4 | 4 | info and tricks |
| Machiwoomiapoo | `HExs51wgeIE` | 2017 | 6 | 3 | SH-101 vs SH-01A |
| Starsky Carr | `czXml-N7eOI` | 2022 | 6 | 3 | SH-101 vs Model 82, self-oscillation sweep |
| Synths For Sale | `Xfjd7CU8UNM` | 2021 | 3 | 3 | sliders demo |
| Patch Project | `08I8QR6CONE` | 2022 | 2 | 2 | filter tuned as an oscillator |
| gattobus | `Er0mLjYBDa4` | 2018 | 4 | 2 | SH-101 vs SH-01A |
| Scott's Synth Stuff | `Ib774z-AU9A` | 2024 | 3 | 2 | SH-101 vs MS-1 |
| Bonedo Synthesizers | `G9wLe7m6sBw` | 2019 | 3 | 1 | MS-1, SH-101 and SH-01A, no talking |
| Tim | `uF2F6MhWIm4` | 2016 | 2 | 1 | envelope retrigger vs a Minilogue |
| TAL Software | https://soundcloud.com/tal-togu-audio-line/sh-101-vs-tal-bassline-101 | 2021 | 33 | 16 | SH-101 vs TAL-BassLine-101, patch by patch |
| MusicRadar SampleRadar | https://www.musicradar.com/news/tech/sampleradar-123-free-roland-sh-101-synth-samples-529084 | 2012 | 8 | 8 | free sample pack, no settings |
| archive.org `tr808all` | https://archive.org/download/tr808all/roland/sh-101/101-All-Samples.zip | 1997 | 7 | 7 | lossless 16-bit samples, incl. a cutoff sweep |
| himalaya (KVR) | https://www.kvraudio.com/forum/viewtopic.php?p=5922970 | 2014 | 5 | 0 | SH-101 vs FXpansion Strobe, order not stated |
| Freesound: andreas, druidbloke (3), freezegelman, zenobic (2), A43 | https://freesound.org/people/andreas/sounds/101200/ and sounds 167115-167117, 418381, 434349, 434390, 9687 | | 8 | 8 | noise, self-oscillation sweeps, arpeggio loop, drums, demos |

Two more videos were checked and gave no clips: Roland Cloud `da7MAoWuwJg` (voice-over
throughout) and Alex Ball `qeKxrva4tHw` (no voice heard alone).

Also under `reference/`:
- `online/` (2026-10-08): fuller downloads for reading settings off the picture: Friendly Noise
  `eStzYAZoOAI` (whole video), Starsky Carr `czXml-N7eOI` (655-690 s), Jorb `jl52FJCdrgk`
  (435-730 s), and Mod Wiggler thread
  https://www.modwiggler.com/forum/viewtopic.php?t=166240 (envelope voltages written on a
  schematic). Each folder's README gives URL, date and terms. `online-references.md` says what
  each gives.
- `scenes/shootout-real/`: A/B scenes made here from twelve of the clips above.
- `mame/` (2026-10-06): MAME's MCS-48 core and three 8049 drivers, from
  https://github.com/mamedev/mame at c6eac87 (BSD-3-Clause; README with each file's SHA1).
- `spread/`: made here (parts-spread runs).

## Roland's SH-101 plug-in

Roland's own SH-101 plug-in (Roland Cloud / PLUG-OUT, v1.2.0) is the main A/B reference.
`reference/roland-vst/renders/` holds 264 WAV renders of it, made here in 2026-10 at set parameter
values (tuning, each envelope stage, filter and LFO laws, bends and so on) through
`tools/compare/roland_render.py`. `base.otk` is the song each render starts from and
`measurements.json` what was measured from them (`roland-measurements.md`). Its manuals are under
S30; `docs/roland-plugin/` also holds our notes on it and the parameter names read from the
plug-in's own `Script.xml` (`roland-plugin.md`).

## Not obtained

From `research.md` §0:
- any Rohm BA662 data sheet (none survives);
- a Curtis CEM3340 Rev. G or OnChip data sheet, and Curtis's *Synthesource* newsletters;
- several papers (Välimäki and Huovilainen, CMJ 2006; Välimäki, IEEE SPL 2005; Hélie, TASLP 2010;
  D'Angelo and Välimäki, ICASSP 2013; Corbett 2024);
- Gearspace threads (Cloudflare, no Wayback copies) and Mod Wiggler t=195051, 285915 and 231189;
- the d16 LuSH-101 and Softube Model 82 manuals;
- any 1982-83 lab review with measurements, and any measured real-unit data for envelope times,
  LFO rates, portamento, cutoff, resonance or output level.

Not downloaded on purpose: `jn60c99`, a C port decompiled from Roland's Juno-60 plug-in (a
copyright and clean-room risk).

## Duplicates

Some files are kept twice under two names, for example a mod manual both in `docs/` and in
`docs/papers/`; the tsv shows them by their equal SHA1s. Two board photos in `docs/photos/pcb/`
(`matrixsynth_10_*` and `matrixsynth_13_*`) are the same file.
