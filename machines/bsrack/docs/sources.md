# Bass Station Rack: sources

Every file was downloaded on 2026-11-24. Each row gives its URL and SHA1. Paths are relative to
`private/bsrack/`. All downloads are treated as untrusted data. No file here came from a purchase,
an account, or a request to anyone.

## ROM

| File | What | SHA1 | CRC32 |
|---|---|---|---|
| `private/roms/bsrack/v1.5.bin` | Program EPROM, firmware v1.5, ST M27C256B, 32 KB. MAME set `bassstr`, file `v1.5.bin`. | 2c2dc3bee0fabf1e77d02cdf3e53885f2c5b913e | a8bc9721 |

## Manuals

| File | URL | SHA1 | Notes |
|---|---|---|---|
| `docs/manuals/novation-rack-user-guide-en.pdf` | https://fael-downloads-prod.focusrite.com/customer/prod/downloads/BassStation%20Rack%20User%20Guide%20-%20EN.pdf | 506745e474c2622e859d975b1d6c01a9725c6299 | Novation's own copy of the owner's manual. 23 pp., distilled 2000-02-28. Text extracted to `.txt` (layout) and `rack-guide-flow.txt` (reading order). The MIDI implementation chart (p. 21) is an image. |
| `docs/manuals/polynominal-rack-manual.pdf` | https://www.polynominal.com/novation-bass-station-rack/novation-bass-station-rack-manual.pdf | b0e8ff07f23499b7e7707b73a37dc583ad46a548 | The same text as Novation's copy, different file |
| `docs/manuals/polynominal-keyboard-manual.pdf` | https://www.polynominal.com/novation-bass-station-keyboard/novation-bass-station-manual.pdf | febbe3bccf9f1d4f6d844462bb2effb386b0db76 | Bass Station keyboard owner's manual |
| `docs/manuals/novation-super-bass-station-user-guide.pdf` | https://fael-downloads-prod.focusrite.com/customer/prod/downloads/Super%20Bass%20Station%20User%20Guide.pdf | 3b43f34cd050874c3dfa36cd4ba6de10417b11b6 | The 1997 successor. Kept for comparison only. |

## Circuit

| File | URL | SHA1 | Notes |
|---|---|---|---|
| `docs/schematic/zeninstruments-bass-station-schematic.png` | https://blogger.googleusercontent.com/img/b/R29vZ2xl/AVvXsEgnXVCrb-vEtC4IYFCDd9OCGJ0_DlwHYM_5s7IIrsKuoZSakldJsXACQ8-6PEN_QNNsS85Z_S47HMKUf_Tfb1RiUgyUx4JEvR3kyGy44fJ-6sH0cLQosjlqKfkUo_l59i0F8LZlPgHGLXpAaAYoQOPLYLVISEgHsf71oCy3YUBr8Au-wCcDxa8YeeGU/s4206/Novation%20Bass%20Station%20schematic.png | 964c807b733b987e4e6157a65d53f75c2825131d | Hand-traced analog section of a keyboard board marked "Novation Bass Station SM, Issue 4, 1995". 4206×1405. It gives designators and part types, **no values** apart from the 2.2 µF coupling caps and one "272" mark. The CV multiplexer, the buffers and the digital section are not drawn. |
| `docs/web/zeninstruments-schematic.html` | https://zeninstruments.blogspot.com/2017/09/novation-bass-station-schematic.html | 01e52d741606a7fd30920d15ab318fea76187cea | The post and its comment thread. Text extracted to `.txt`. |
| `docs/schematic/blocks/*.png`, `*.jpg` | the post's own images, `https://blogger.googleusercontent.com/img/b/…/s1600/<name>` (URLs in `docs/web/zeninstruments-schematic.html`) | see below | Crops of the trace (OSCA, OSC_MIX, OSC_MUX, VCF, VCA, Header: no values) and two other images |
| `docs/schematic/blocks/oscar4.jpg` | (as above) | 0a860367cd7721ebc02f7e9267453b6a6cc1c3eb | **The OSCar's "Filters and VCA" service sheet, hand-drawn, with values.** The same designer and filter topology (the trace: "closely related to the OSCar filter"). LM13600 OTAs with their buffers in place of the Bass Station's LM3080 + op-amp integrators. |
| `docs/schematic/blocks/VCF.png` | (as above) | 2ecba98312301a7c5defeb7c9c1b3bc0941536fb | Crop of the trace, 1600×758 |
| `docs/schematic/blocks/OSC_MUX.png` | (as above) | f1651d25ba6d7c38d1fe8ec6a5d5e9bfa88351c4 | The 4053, as in the full drawing |
| `docs/web/sdiy-pinky-bstn.html` | http://www.sdiy.org/pinky/BStn/BStn.html | 30a25055d9650afb0f8f424d76eb3164b11a9ddc | Keyboard mods: pin functions of the unused 24-pin header |
| `docs/photos/sdiy-pinky-bs1.jpg` … `bs4.jpg` | http://www.sdiy.org/pinky/BStn/bs1.jpg … bs4.jpg | 25e3d76a…, 6d91e341…, 3ada9b9c…, 73658064… | Keyboard board photos, 567 px wide. Too small to read part marks. |
| `docs/photos/sdiy-pinky-bs5.jpg` | http://www.sdiy.org/pinky/BStn/bs5.jpg | b8a734ba707df2b274e4a5f1f992c4d75ed7f95d | The mod's own buffer circuit, not Novation's |

## Data sheets

| File | URL | SHA1 |
|---|---|---|
| `docs/datasheets/CA3080_intersil.pdf` | http://web.archive.org/web/2016id_/http://www.intersil.com/content/dam/Intersil/documents/ca30/ca3080-a.pdf | d5232f2bf059d4145564a801e77fa80ca831fb48 |
| `docs/datasheets/TLC272_ti.pdf` | https://www.ti.com/lit/ds/symlink/tlc272.pdf | 98b3af8b1200d69cc3dcef8050a288d97996ebbf |
| `docs/datasheets/TLC274_ti.pdf` | https://www.ti.com/lit/ds/symlink/tlc274.pdf | 134e9b0fb66806be5de06e32553c09943f8fc66a |
| `docs/datasheets/LM324_ti.pdf` | https://www.ti.com/lit/ds/symlink/lm324.pdf | a487b3702a6c3676976b16df704e85a52416fadb |
| `docs/datasheets/CD4053B_ti.pdf` | https://www.ti.com/lit/ds/symlink/cd4053b.pdf | 13a14f133590863b0bd2b6d8adb8bee3f94ac5c4 |
| `docs/datasheets/CD4070B_ti.pdf` | https://www.ti.com/lit/ds/symlink/cd4070b.pdf | 2f656c942476ce13e80f07477bd136f0fd1c73f2 |
| `docs/datasheets/FMA1_rohm.pdf` | https://fscdn.rohm.com/en/products/databook/datasheet/discrete/transistor/digital/fma1at148-e.pdf | fe7f7f691a1333b7cbb6ab9dcf002f964c570dce |
| `docs/datasheets/BC846_series_onsemi.pdf` | https://www.onsemi.com/pdf/datasheet/bc846alt1-d.pdf | 3e21325883f77702db0590bfd6c5ef2323b67207 |

## Reviews and history

| File | URL | SHA1 |
|---|---|---|
| `docs/reviews/sos-bassstation-1994.html` | https://www.soundonsound.com/reviews/novation-bassstation | 76f4f2aecc83e840192a091ecda5c72635a2785d |
| `docs/reviews/sos-bassstation-rack-1995.html` | https://www.soundonsound.com/reviews/novation-bassstation-0 | ccb38da8c3c3311df80b7ba1a437a8165a542892 |
| `docs/reviews/sos-super-bass-station.html` | https://www.soundonsound.com/reviews/novation-super-bass-station | 172169d4a02bd4f5ef66f01485a522e583798318 |
| `docs/web/novation-bass-station-story.html` | https://novationmusic.com/articles/the-bass-station-story | d994e8cc7d491441b1ecc125915ce262b147f3d5 |
| `docs/web/polynominal-rack.html` | https://www.polynominal.com/novation-bass-station-rack/index.html | 3c33fe5495f1e55be66e4fca800f40f475d2d2a0 |
| `docs/web/polynominal-keyboard.html` | https://www.polynominal.com/novation-bass-station-keyboard/index.html | e860fb5c3614239c06a398002dbb35d431187fe0 |
| `docs/web/polynominal-keyboard-patch.zip` | https://www.polynominal.com/novation-bass-station-keyboard/patch.zip | 78d1bcfa54b8989bc2e4b73e2dea1a6678808f52 |

## Panel photos

| File | URL | SHA1 | Notes |
|---|---|---|---|
| `docs/web/polynominal-rack-front.jpg` | https://www.polynominal.com/novation-bass-station-rack/novation-bass-station-rack.jpg | e539fb19dd789cf12a2dcefa067822f279b3d560 | Front panel, straight on, 4061×1244, cut out on white. A site watermark covers the lower right corner. |
| `docs/web/polynominal-rack-rear.jpg` | https://www.polynominal.com/novation-bass-station-rack/rear.jpg | df3089582ef130a50539af45e67b71bdd2b649f2 | Rear panel, 1032×104 |

## MAME

Fetched from github.com/mamedev/mame at the commit in `reference/mame/COMMIT`. All files are BSD-3-Clause (AJR).

| File | SHA1 |
|---|---|
| `reference/mame/basssta.cpp` | 36fc58c224b7b5e3063c12e38090b892165989fa |
| `reference/mame/drumsta.cpp` | a03553c80dadd96deb9fd0a445aa6c764dbb5d4f |
| `reference/mame/mn1880.cpp` | 75705a7de830bf6b747df82d253482b8fe8cf0b8 |
| `reference/mame/mn1880.h` | e0caff456945737e5bd3b2a75b54b3eb8d7e066a |
| `reference/mame/mn1880d.cpp` | 08f1fc0e938a1119866f352867ac742e1c0215b7 |
| `reference/mame/mn1880d.h` | 33095e438adc698ad0a61f17f38e37de7ebea4c4 |

## Recordings of real units

The polynominal.com pages host these clips of their own units. Rack clips:
`https://www.polynominal.com/novation-bass-station-rack/<name>.mp3`. Keyboard clips:
`https://www.polynominal.com/novation-bass-station-keyboard/<name>.mp3`. No knob settings are
published with them.

| File | SHA1 | Notes |
|---|---|---|
| `reference/polynominal-rack/factory-demo.mp3` | 4f584a4d4eefc4b0ec8b9d93c2c79857e8453ee0 | The Rack's built-in demo sequence, 44.8 s, mono, 44.1 kHz. The firmware plays the same notes. |
| `reference/polynominal-rack/filter-2poles.mp3` | 984aec87161dd3ae5169cb7107ebde03684ba0e0 | 12 dB slope |
| `reference/polynominal-rack/fiolter-4poles.mp3` | c4295f36a5a67da8ce89e82e933b1b0c4c702f10 | 24 dB slope (the site's spelling) |
| `reference/polynominal-rack/adsr.mp3` | 5ea4cfffc9a805171d86a8a0c4f917dcd1556aea | |
| `reference/polynominal-rack/lfo.mp3` | f7d868c89825c555d7e0053e8d34ef4d3a94083e | |
| `reference/polynominal-rack/wavefo.mp3` | 429352f3bd1fafe2a3f831e6177185870ec48b30 | Waveforms |
| `reference/polynominal-rack/pulsewisth.mp3` | eec1e731a3701104f246177d0a09d15149406a78 | Pulse width |
| `reference/polynominal-rack/detune-range.mp3` | 42d3e325994802dd4ab4fc5746e56b7ed46191ac | |
| `reference/polynominal-rack/portamento.mp3` | d1bdb17a81e60e947e7c71a1f316ee1143a83979 | |
| `reference/polynominal-rack/xmod.mp3` | ef985ce76cf1aafd09eef8372cc802819cdb95b4 | |
| `reference/polynominal-rack/onturn.mp3` | 096c805630e115da4bc586c3079f8ddbe99ad34f | |
| `reference/polynominal-rack/bass-analog.mp3` | d68d51de1dc1482f0dfde8e4f7804ce1e4e8a613 | Same size as synth-legato.mp3, different content |
| `reference/polynominal-rack/bass-minimoog.mp3` | c8505b724472f1def8a9d770b2be269f9f64b5ef | |
| `reference/polynominal-rack/bass-moog.mp3` | b2f3d15ab761a536c3bc634134b1c7a3f74cbfd8 | |
| `reference/polynominal-rack/bass-mooga.mp3` | ff1e06db6cffbd6d57189998af3d3b4f224be00d | |
| `reference/polynominal-rack/bass-soft.mp3` | 8ce7cb376199c41d85681dcdd17a1247986af130 | |
| `reference/polynominal-rack/bass-solid.mp3` | 20c80177010c8f500da7bb583f8648f6aae76762 | |
| `reference/polynominal-rack/fx-rain.mp3` | bb54d3eac12b7a4469d287126eef170ee93045f6 | |
| `reference/polynominal-rack/harpsichord.mp3` | 5c31579bbbef58873a6de9b6668697876c611a1a | |
| `reference/polynominal-rack/lead-zanzara.mp3` | 3d0baa41d29f7576c8a6e0bdd3736c4c482f210a | |
| `reference/polynominal-rack/organ.mp3` | 593ef894718f02b036be3f274687920560b6a252 | |
| `reference/polynominal-rack/perc.mp3` | cb3caab6e9c25fdf520b6aa317dcaaebaa606979 | |
| `reference/polynominal-rack/synth-detuned.mp3` | 3546008692f9abd85782437da65c04ab09414af8 | |
| `reference/polynominal-rack/synth-filter.mp3` | 734d12438096c0378c8669dbc350b9c337568797 | |
| `reference/polynominal-rack/synth-leasd4.mp3` | db7dc8487be9e87563bf84f3967d4dff48e1dc2d | |
| `reference/polynominal-rack/synth-legato.mp3` | c9b570f1d3e7049132663524b9ec0e3874030d1d | |
| `reference/polynominal-rack/synth-lgfo.mp3` | 1c60daeeb918ebd6b213deaa2d14911990c1c169 | |
| `reference/polynominal-rack/synth-sick303.mp3` | f14db0cfebf555fc494ef03ba7b5d66be2933ed6 | |
| `reference/polynominal-rack/synth-tb303.mp3` | 59d0474c434b8ce3d4e6fdc2a4fba002bcb5c109 | |
| `reference/polynominal-rack/synth-wet.mp3` | a68db24261452e84a4778a247395048be60a167b | |
| `reference/polynominal-keyboard/demo1.mp3` | 008fc129ac3320e221978e304fca66be4b25f11e | Keyboard |
| `reference/polynominal-keyboard/demo2.mp3` | f30c506604a9670739981845ad3ffdc627171044 | Keyboard |
| `reference/polynominal-keyboard/demo3.mp3` | ca16e666b4359afaab1b93a3a4ffa9d60e99a935 | Keyboard |

## YouTube

zibbybone's "Novation Bass Station Rack - Preset Patches and External Filter Input Demo"
(https://www.youtube.com/watch?v=cyiyopNwVAA, 2014-06-20), downloaded on 2026-11-25. A Rack steps through programs 00-39 with + under one MIDI pattern, 4-5 s
each, then plays through FILTER INPUT. The pattern and the program changes, read off the picture, are
in `reference/youtube/zibbybone.json`.

| File | SHA1 | Notes |
|---|---|---|
| `reference/youtube/zibbybone-cyiyopNwVAA.m4a` | 6139378d4cd2fd5e39ffeaab668c70d07d45cb47 | Its sound, AAC |
| `reference/youtube/zibbybone-cyiyopNwVAA-720p.mp4` | a68f8bbb9ca7350d3affbf70b51bbb57cdf940a6 | The picture, for the display |

## Not downloaded

- Three Gearspace threads (a modified Rack, two keyboard repairs) refuse automated fetches. Read in a browser on 2026-11-24, they had no usable board photo (one has 480×640 shots of a Rack rear socket and CPU corner):
  - https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/1441421-novation-bassstation-rack-modified-what.html
  - https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/652455-novation-bass-station-repair.html
  - https://gearspace.com/board/geekzone/846606-novation-bass-station-original-no-sound-help.html
- A circuitbenders.co.uk forum attachment (a keyboard board photo near the EEPROM,
  topic 926, attachment 777) returned a text page, not the image.
- A 2010 YouTube demo of a Rack recorded direct (matrixsynth.com, 2010-05-01). Not fetched yet.
