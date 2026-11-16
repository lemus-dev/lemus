# Roland SH-101: top-panel layout

The panel the plug-in draws. The machine-readable version is `docs/panel.json` (345 elements, mm
coordinates, colour keys), which `tools/panel/gen_panel.py` turns into `plugin/PanelData.h`. This file
says where the numbers come from and what JSON doesn't say well.

## 1. Sources and method

- **Master image:** `private/docs/photos/commons_sh101_wikipedia_909bd.jpg`, a near top-down photo of
  a grey unit (CC BY-SA 3.0, English Wikipedia user "909 bd", 2006; 2398 × 1312 px, about 4.1 px/mm).
  It is the only free-licence whole-panel photo, so the artwork recipe traces the brand marks from it.
  Its left case edge is cut off and its lens bends the long grooves by up to 1.9 mm.
- **Scale and squareness** come from two dealer photos with the whole case outline visible (reference
  only, © Soundgas): `soundgas/sn596598_…-3.jpg` (keystone 0.4 %) and `soundgas/sn279013_…-1.png`. The
  case is 570 mm wide (service notes: 570 W × 311 D × 80 H mm, 4.1 kg).
- **The map.** 33 print features (section titles, legends, buttons, the logos) were located in all
  three photos by OpenCV template matching. Their positions in the dealer photos, measured from the
  case edges, fix each feature in panel mm; a least-squares affine maps the Commons photo onto them
  (residual 0.41 mm in x, 0.20 mm in y). A smooth correction then levels the three long grooves
  (y 31.5, 45.1, 123.6), which are straight on the real panel; after it they hold within ±0.15 mm.
  The lower left panel (VOLUME to BENDER) sits about 3 mm below the main panel, so it has its own
  similarity map (residual 0.1 mm). Both maps are in panel.json (`source_map`).
- **Measurements** on the Commons photo rectified to 10 px/mm:
  - text boxes: the white print components inside a region read off a gridded zoom, plus 0.18 mm for
    the anti-aliased edge;
  - scale ticks: detected per column (24 main, 4 bender) and fitted to a uniform grid: 11 ticks,
    3.12 mm pitch from y 71.45 (bender: 2.02 mm from y 232.87);
  - slider slots, channels and caps: dark-run detection; buttons and LEDs: blob detection; knobs:
    circle fits; jacks: Hough circles;
  - everything else (glyphs, connectors, brackets, the bender) from 8–30 px/mm gridded zooms.
- **Keyboard:** white-key gaps and black-key centres from the rectified Soundgas photo (the keys are a
  different plane): 19 white keys at 23.43 mm from x 91.6; black keys 14 mm wide, ending at y 256.9.
- **Check:** `private/docs/photos/tools/` has the scripts (`build_panel_json.py` writes panel.json).
  Snapshots of the view were compared side by side with both photos at the same scale; legends,
  controls and keys line up to about 0.5 mm.

Accuracy: ±0.3 mm within a section; ±1 mm absolute in x (the two dealer photos disagree by up to
2 mm across the width), ±0.5 mm in y; the lower left panel and the keys ±1 mm.

## 2. Coordinates and structure

Origin at the rear-left corner of the case seen from above; x to the right, y towards the player, mm.
The view draws 570 × 310 mm at 2.2 px/mm (1254 × 682 px) with the plugin bar below.

| Zone | y (mm) | Notes |
|---|---|---|
| Rear shelf | 0.8 – 19.6 | The jack face, sloping away: MODULATION GRIP (x 22–48 inset), DC IN, EXT CLK IN, HOLD, CV/GATE IN and OUT (in a plate), PHONES, OUTPUT. The battery lid (x 194.5–361.3) and the jack block are separate mouldings |
| Legend strip | 19.6 – 31.5 | The jack legends, white |
| Section titles | 31.5 – 45.1 | Raised strip divided by grooves: MODULATOR, VCO, SOURCE MIXER, VCF, VCA, ENV |
| Controls | 45.1 – 123.6 | Vertical grooves at x 6.4, 55.7, 117.2, 223.9, 328.8, 424.8, 452.7, 558.6 |
| Buttons and logos | 123.6 – 162.6 | POWER, the seven buttons and their LEDs, Roland and SH-101 |
| Step | 162.6 – 166 | The raised panel's lit front face over the left panel and the right cheek |
| Left panel | 164.4 – 306.4, x 3.5 – 88.6 | VOLUME, PORTAMENTO, the AUTO/OFF/ON and L/M/H switches, the three bender sliders, the bender |
| Keys | 164.2 – 306.8, x 91.6 – 536.8 | 32 keys F to C |
| Right cheek | x 537.9 – 566.5 | Plain |

## 3. Body colours

The SH-101 came in grey, red and blue (and white in Japan). The panel print is white on all of them.

| Body | Measured | Notes |
|---|---|---|
| Grey | `#777778` (Soundgas 596598, neutral light), `#6A6653` (Soundgas 279013, an early olive-grey), `#6E6A62` (Commons, warm cast) | **Default.** The master photo is grey, and it is the common colour |
| Red | `#C0373C` (Soundgas 310587, studio light) | |
| Blue | `#2E4473` (Soundgas 372483) | |

The view draws grey `#6E6C67`, between the neutral and the warm photos. Other palette entries
(`colours` in panel.json): slider and switch caps black with a green (MODULATOR RATE, SOURCE MIXER),
orange (VCO, VCF, ENV, bender) or yellow (slide switches) line; knobs black with an orange pointer;
button caps light grey; LEDs rectangular red (GL-9PR2); keys `#F4F3F0` / `#1C1B1D`.

## 4. Typography

- All legends are white Helvetica Bold caps. Cap heights, print edge included: section titles about
  6 mm, control names 3.8 mm (VOLUME, TUNE, FREQ …), jack legends 3.3 mm, switch positions and button
  names 2.8 mm, scale numerals 2.4 mm.
- The view sizes each legend by its cap height (measured on the face's "H") and fits it to the
  measured width.
- Logotypes are traced, not typeset (§6).

## 5. Controls

| Kind | Parts | Geometry |
|---|---|---|
| Sliders (16 + 3) | 30 mm travel on the main panel, 20 mm on the bender | Value 0 at the bottom tick, 1 at the top; caps 8.7 × 19.5 mm (bender 7.8 × 17), the coloured line marks the value. Channels 8.3 mm wide, slots 3.2 mm |
| Slide switches | PWM (LFO / MAN / ENV), SUB OSC (1 OCT / 2 OCT / 2 OCT pulse), VCA (ENV / GATE), ENV trigger (GATE+TRIG / GATE / LFO) | Detents 4 mm apart (3.5 mm for VCA), top to bottom, where the printed connectors meet the switch |
| Rotary switches | WAVE FORM (triangle, square, RANDOM, NOISE), RANGE (16′ 8′ 4′ 2′) | Detents at −45, −15, +15, +45° with printed ticks |
| Knobs | TUNE (arc with b and #), VOLUME, PORTAMENTO (11 ticks) | −150 to +150°; Ø 19.6 mm |
| Lever switches | PORTAMENTO (AUTO, OFF, ON), TRANSPOSE (L, M, H) | Horizontal, three positions |
| Buttons | LOAD, PLAY, DOWN, U&D, UP, HOLD, KEY TRANSPOSE | Caps 10.8 × 5.4 mm, each with its LED above; POWER on the left with its LED |
| LEDs | the seven buttons, POWER, the LFO/CLK RATE LED | 5.2 × 2.5 mm |
| Bender | the lever in its slot (x 23–66.4, y 286.4–296.8) | A paddle that pivots sideways and tips forward for MOD |
| Keys | 32, F3 to C6 | Key numbers 0–31 as the firmware's matrix numbers them |

The plug-in bar under the panel holds what the hardware doesn't have: SYNC (OFF, or HOST with its
step length) and `?`.

## 6. Brand marks (the artwork pack)

Marks are never in the repo, and Lemus ships no pack. The panel looks for these ids in the user's
`sh101.svg`:

| id | Box (mm) | What |
|---|---|---|
| `roland` | x 392.8–456.3, y 146.0–155.7 | The emblem and wordmark |
| `sh101` | x 461.4–548.4, y 134.3–155.7 | The dot-matrix logotype |

Both are in panel mm (the `top` frame), white. Any SVG with these ids works as a pack.
