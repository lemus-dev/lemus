# Sources

Every file is kept under `private/` (never in this repo). Paths are relative to the private folder.

## ROM

| File | Source | Size | SHA1 |
|---|---|---|---|
| `roms/virusc/virus_c_660x368.bin` | <https://archive.org/details/access-virus-b-c-roms>, `Access Virus C (am29f040b_6v6).zip`. Matches MAME `virusc`, BIOS `v6.6` | 524288 | `4c34546889d90522a8d85300c471f18339f88f72` |

MAME's DSP56362 bootstrap ROM `boot-362.bin` (CRC `fa69796b`, named in `dsp56362.cpp`) is not
needed: the engine stands in for it (`firmware.md`).

## MAME source (BSD-3-Clause)

MAME master `d077ea24f0a73118bee9558bc939b99ae662bfc5`, in `virusc/reference/mame/`.

| File | SHA1 |
|---|---|
| `acvirus.cpp` (src/mame/access) | `04d588f12436176a7cecf2821952372d41e45e1e` |
| `virusc.lay` (src/mame/layout) | `2bac4ccfac12f81ef6bcc956b762e1d5316a688b` |
| `dsp563xx/dsp563xx.cpp` | `daa12471d0f5b7e5cecdae1876098ec46c1802e9` |
| `dsp563xx/dsp563xx-interp.cpp` | `80c286fdaa4fb7acd916884531b07567f742c471` |
| `dsp563xx/dsp56362.cpp` | `cef57490266adbd104cf5ab5099a1c42c81e7edc` |
| `dsp563xx/hi08.cpp` | `5655e6d5ea3e0ffb93cc1bfd68061ed3e0ff8a20` |
| `mcs51/i8051.cpp` | `790d96d8a126ffe3ccdd53d4554ee79834cb75b5` |
| `mcs51/mcs51ops.cpp` | `bc7a0217d6eb3e3c9bf15fd384fe7a392f0c2a92` |
| `mcs51/sab80c535.cpp` | `03851ae9186d8c0b6c4cb4e9243553b73c6dca50` |

The rest of `src/devices/cpu/dsp563xx/` and `mcs51/` at that commit is in the same folders.

## Documents

| File | Source | Pages | SHA1 |
|---|---|---|---|
| `virusc/docs/DSP56300FM.pdf` | <https://www.nxp.com.cn/docs/en/reference-manual/DSP56300FM.pdf> | 512 | `75865c0590d927f4122956635732e5c05a7c949c` |
| `virusc/docs/DSP56362.pdf` | <https://www.nxp.com/docs/en/data-sheet/DSP56362.pdf> | 152 | `fd4d0bb49ef40e8f75cae5ab09c5e33f8beeb12e` |
| `virusc/docs/DSP56362UM.pdf` | <https://www.nxp.com/docs/en/user-guide/DSP56362UM.pdf> | 336 | `fc2febc2b7f33b049942fdad369565752309c542` |
| `virusc/docs/AN1848.pdf`, Enhanced Serial Audio Interface | <https://www.nxp.com/docs/en/application-note/AN1848.pdf> | 56 | `a71a1c51d0cd6064a25395e649de53ff701e7d8f` |
| `virusc/docs/c515_um.pdf` | <https://www.keil.com/dd/docs/datashts/infineon/c515_um.pdf> | 160 | `524c6ac0fa4a3e1e2ce719dce2da914609e0bd44` |
| `virusc/docs/virus_c_manual.pdf`, Virus C Series User Manual OS5 | <https://soundprogramming.net/manuals/Access_Virus_C_Manual_v5.pdf> | 198 | `90b50814704598194bc762520062a72f12395a1f` |

## Reference

| File | Source | SHA1 |
|---|---|---|
| `virusc/reference/vse/virusc.mp3` (129.8 s, 128 kbit/s) | <https://vintagesynth.com/access/virus-c> | `d6fab8761c7eaac804982cf2ecf826bc13f213ab` |
| `virusc/reference/vse/indigo_0.mp3` | same page | `75aa5f732a154a79965bf6463b6a13183d545b1e` |

Gearmulator (GPL-3.0), <https://github.com/dsp56300/gearmulator>, commit
`83480f3cf64390524cbbfad40bd7575c2bb017b4`, with its `dsp56300` engine at `8b73196`, in
`virusc/reference/gearmulator`. The test console there has local edits for headless renders.

Not yet downloaded: front-panel photos, Wikimedia Commons, "Access Virus C Desktop" (15 files,
for example `File:Access Virus C Desktop (6875892803).jpg`).

## Facts taken from web pages

- <https://theusualsuspects.io/technical/osirus-c>: the emulator's published technical notes.
- <https://en.wikipedia.org/wiki/Access_Virus>, <https://www.vintagesynth.com/access/virus-c>,
  <https://www.soundonsound.com/techniques/access-virus-masterclass>: model history and
  voice counts.
