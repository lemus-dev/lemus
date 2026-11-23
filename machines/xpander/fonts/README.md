# Fonts

The panel's print is Helvetica Condensed (docs/panel.md section 5). The view stands in **Nimbus Sans
Narrow** (URW++, the Helvetica Narrow design: spurred G, straight-legged R, flagged 1), Regular for the
legends and Bold for the keypad caps, each legend fitted to its measured ink box. The bar and the key
list use the host's IBM Plex Mono (`third_party/fonts`).

Why not Liberation Sans Narrow, which panel.md suggests: it is not under the SIL OFL. The OFL
Liberation fonts (2.x) have no Narrow; Liberation Sans Narrow exists only as 1.07.x
(github.com/liberationfonts/liberation-sans-narrow), under GPL v2 with a font exception, which does
not mix with Lemus's AGPL-3.0 in one binary. The URW base 35 fonts are AGPL-3.0, Lemus's own licence
(with an exception for embedding in PostScript and PDF documents).

| File | Source | SHA1 |
|---|---|---|
| `NimbusSansNarrow-Regular.ttf` | https://raw.githubusercontent.com/ArtifexSoftware/urw-base35-fonts/20200910/fonts/NimbusSansNarrow-Regular.ttf | 98313582dc79c140d4c5685853738f05f7906ced |
| `NimbusSansNarrow-Bold.ttf` | https://raw.githubusercontent.com/ArtifexSoftware/urw-base35-fonts/20200910/fonts/NimbusSansNarrow-Bold.ttf | 6980b298ab8f22b09d8dbc4edb641b8da71d0c38 |
| `COPYING-urw-base35.txt` | https://raw.githubusercontent.com/ArtifexSoftware/urw-base35-fonts/20200910/COPYING (AGPL-3.0 text) | 78e50e186b04c8fe1defaa098f1c192181b3d837 |
| `LICENSE-urw-base35.txt` | https://raw.githubusercontent.com/ArtifexSoftware/urw-base35-fonts/20200910/LICENSE (the licence and its exception) | bfb6d621b392371ea69ff0ea26ec09e12581555b |

Tag `20200910` of github.com/ArtifexSoftware/urw-base35-fonts (commit c15105598aa7), fetched
2026-11-12. Copyright 2014 (URW)++ Design & Development. Unmodified.
