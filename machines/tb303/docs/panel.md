# Roland TB-303 Bass Line: front-panel layout spec

This spec is for drawing a photoreal TB-303 top panel in the plug-in UI. The machine-readable version is `docs/panel.json`, which has 263 elements with mm coordinates, normalised coordinates and colour keys. This document explains it and adds what JSON cannot say well: materials, 3-D shapes, fonts and uncertainties.

## 1. Sources and method

- **Master image:** `private/docs/photos/commons_tb303_panel_topdown_sims.jpg`. It is a straight-on top view (CC0, Steve Sims, 1656×814 px). There is no measurable keystone: the left and right edges hold at x = 10.5 px and 1639.5 px from top to bottom.
- **Cross-checks:**
  - `vse_tb303big.jpg`, a flatbed-style scan. Proportions match within about 1.5 %.
  - `soundgas/sn163500_Roland_TB-303_163500_4.png`, a studio straight-on shot.
  - The owner's-manual drawings (`owners-manual-panel/om_page-08.png` and others).
- **How the numbers were taken:** with Python and Pillow:
  - line detection for keylines and boxes;
  - blob detection for LEDs, scale ticks, position dots and knob-marker squares;
  - circle fits for bezels and tick rings;
  - word bounding boxes for text;
  - manual reading of 2.4x–6x gridded zooms for everything else.
- **Overlay check:** `private/docs/photos/panel_overlay_check.jpg` draws every JSON element back onto the photo. Everything lines up to about 1 px (0.2 mm).
- **Regenerating:** the raw pixel data and the conversion live in `private/docs/photos/tools/build_panel_json.py`.

## 2. Dimensions and coordinate system

| | Value |
|---|---|
| **Official dimensions** (owner's manual p. 90, Service Notes p. 1) | **300 (W) × 146 (D) × 55 (H) mm, 1.0 kg**. The figure of 361×237×66 mm quoted in the brief is wrong. |
| **Top-view outline** (what the UI draws) | **300.0 × 142.0 mm**, aspect 2.113 : 1 |
| Scale in master photo | 5.43 px/mm; photo px (10.5, 18.5) = panel (0, 0) |
| Origin | Top-left corner of the outline (rear-left of the unit). **x** runs right, **y** runs toward the player (down the screen). All values are in mm. |
| Normalised coords | `nx = x/300`, `ny = y/142` (also stored in the JSON) |
| Accuracy | ±0.3 mm in x. ±0.5 mm in y, with a possible y-scale error of up to 1.5 % from camera tilt. 3-D sizes (knob heights, cap sizes) are estimates, ±1 mm. |

The spec says 146 mm deep, but the top view measures 142 mm. The missing ~4 mm is most likely the bottom plate or feet, the rear jacks, or rounding. The 55 mm height includes the knobs; the case body is about 40 mm tall.

A UI at 4 px/mm is 1200 × 568 px. At 6 px/mm it is 1800 × 852 px.

### Design grid observed

- **Tone knobs:** centres sit on y = 15.32. The pitch is **22.47 mm** (122 px), starting at x = 89.23 for TUNING.
- **Second row:** TEMPO, PATT.GROUP, MODE and VOLUME all sit on y = 51.73.
  - **PATT.GROUP sits exactly under TUNING** (x 89.23).
  - **MODE sits exactly under RESONANCE** (x 134.16).
- **Keyboard:** each white key is **16.41 mm** wide (89.1 px). The black keys are centred on the C|D, D|E, F|G, G|A and A|B boundaries.
- **Lower key row:** the keys and LEDs share x with the white-key centres. The four right-hand keys (DOWN, UP, ACCENT, SLIDE) keep the same 16.4 mm pitch after a 17.4 mm jump from high C.

## 3. Physical structure (levels, from the rear edge to the front)

| Zone | y range (mm) | Notes |
|---|---|---|
| Tone strip (upper plate) | 0 – 27.1 | Flat metallic-silver paint. Holds the jack legends, the 6 tone knobs and the roland and bass_line mark slots. Two vertical printed rules: x = 75.8 (y 2.6–24.0) and x = 214.5 (y 2.5–24.2), each 0.45 mm wide. |
| Seam | 27.1 | Fine step or groove between the tone strip and the middle section. Draw it as a 0.8 mm dark line (`seam_dark`) with a 0.3 mm highlight (`seam_hilite`) under it. It runs x 1.4 → 298.8 and stops short of the case sides. In close-ups the tone strip's front edge looks a hair higher (≤0.5 mm). |
| Middle section | 27.1 – 70.0 | Same plane and paint as the tone strip. Holds TEMPO, PATT.GROUP, MODE, VOLUME and the tb303 mark slot. |
| Step down | 70.0 – 71.5 | Vertical face (in shade) dropping roughly 2–3 mm to the recessed keyboard sub-panel, with a thin highlight on the sub-panel's top edge at y ≈ 71.5. |
| Keyboard sub-panel | 71.5 – 135.4, x 2.4 – 298.3 | Separate plate (Roland "sub panel N-277"), slightly lighter and cooler silver. All key, LED and box graphics sit on it. |
| Case walls | x 0 – 2.4 and 298.3 – 300 beside the sub-panel | Top of the case wall (≈1.0 mm) plus the inner step face in shade. |
| Front step + rim | 135.4 – 137.0 shade, 137.0 – 142.0 rim | Front lip of the case in front of the sub-panel, seen from above. Draw it a little darker (`case_front_rim`), with a 0.2 mm bright edge at y = 142. |

- **Corners:** the outline corners are rounded, r ≈ 1.3 mm.
- **Bevel:** the top surface has a very small bright bevel (≈0.3 mm) along the rear and side edges.
- **Case seams:** in the side and rear photos the case shows two horizontal moulded seams on the side and rear faces.

## 4. Colour palette

Render values are white-balanced suggestions. "Measured" values are medians from the photos; lighting differs between them, so read them as a range.

| Key (JSON) | Render hex | Measured | Use |
|---|---|---|---|
| `case_silver` | `#C3C3BF` | Sims `#BCBCB8`, VSE `#ADB4AD`–`#B8BEBA`, Soundgas `#EFEEE8` (over-exposed) | Tone strip and middle section. Metallic silver paint with a fine grain; add 1–2 % noise or speckle. Do not use a brushed texture. |
| `subpanel_silver` | `#CDCDCA` | Sims `#CBCBCA` | Keyboard sub-panel, a touch lighter and cooler. |
| `key_white` | `#DCDFDE` | Sims `#D5D8D9`, VSE `#EAEEE8` | Printed "white key" areas. Off-white, not pure white. |
| `print_black` | `#1E1E20` | Sims `#28282A`, Soundgas `#0B0D0F` | All black print: bands, boxes, legends, ticks, keylines. |
| `print_white` | `#ECECEA` | Sims `#E1E2E4` | Reverse legends inside black areas. |
| `print_orange` | `#C4502A` | Sims `#A14A2A` (brightest `#C96F54`), Soundgas `#982F10` | Orange-red print: PATTERN, the pattern numbers 1–8, A, B, PATT. SECTION. Medians are darkened by anti-aliasing, so the real ink is a fairly saturated vermilion-orange. |
| `led_lit` | `#FF1E32` | Close-up: core `#F30037`, body `#E2002D` | Lit LED. Add a soft red glow, radius ≈ 2× the LED. |
| `led_unlit` | `#5A0C12` | Sims `#6E151F`, Soundgas `#550004` | Unlit LED: dark-red dome with a small specular highlight at the upper left. |
| `keycap_face` | `#B9B9B6` | Soundgas `#B4B5B5` | Satin silver-grey key caps. The Sims photo's `#A48E73` is warm reflection from the room. |
| `keycap_hilite` / `keycap_shadow` | `#EDEDEB` / `#5E5E5A` | – | Cap bevels: light on the top edge, dark on the right and bottom. |
| `key_hole` | `#121212` | – | Panel cut-out gap around each cap, ≈0.4 mm visible all round. |
| `knob_cap` | `#C8C8C6` | Sims `#B0ACA9` | Flat satin top disc of the knobs. |
| `knob_knurl_light` / `knob_knurl_dark` | `#E4E4E2` / `#55514B` | Sims skirt `#595246` | Knurled skirt: strongly reflective metallised plastic, high contrast. |
| `bezel_ring` / `bezel_gap` | `#D9D9D7` / `#151515` | – | Chrome-bright rim of the knob well, and the dark gap inside it. |
| `case_front_rim` | `#A9A9A5` | Sims `#9C8465` (warm) | Case rim and step faces in shade. |
| `seam_dark` / `seam_hilite` | `#7C7C78` / `#E8E8E4` | Sims `#948471` | Tone-strip seam. |

## 5. Typography

- **Panel legends** are Helvetica (Neue Haas / Helvetica Medium or Bold), all caps.
  - Small legends (TUNING, MIX IN, PATTERN CLEAR …) are Helvetica Medium. Cap height is 1.8–1.9 mm.
  - Section headings (TEMPO, MODE, VOLUME, PATT.GROUP) are Helvetica Medium, cap height ≈2.5 mm.
  - FUNCTION and SELECTOR are a larger, lighter Helvetica (regular weight), cap height 2.5 and 3.1 mm.
  - PITCH MODE (reverse) is Helvetica Bold, 3.1 mm.
  - Reverse legends (note names, TRANSPOSE, DOWN, UP, ACCENT, SLIDE, DEL, INS, BAR, TRACK, the number boxes) are Helvetica Bold.
- **"#"** in the note names is a raised sharp sign: C♯, D♯, F♯, G♯, A♯. It sits higher than the letter, about 65 % of the cap height.
- **Serif:** "D.C.", "D.S." and the segno sign (𝄋) use Times Bold Italic. The roman numerals I, II, III and IV use Times Bold (serifed).
- **Brand marks:** not drawn by Lemus. Their slots are left blank and filled from the user's artwork
  pack; the blank pack (`blank_artwork.h`) holds them. Slots (ids as in the pack): `roland` x 13.34–65.09,
  y 14.09–21.45; `bass_line` x 226.80–283.52, y 14.27–21.45; `tb303` x 207.63–236.37, y 44.48–50.56;
  `computer_controlled` x 179.83–236.37, y 52.58–59.02.
- **Rule:** a 0.45 mm printed rule at y = 51.86 runs x 179.6–236.6, between the tb303 and computer_controlled slots. It goes with those marks, so Lemus draws it only when the artwork pack has one of them.

## 6. Knobs and rotary switches

All rotation angles are measured from 12 o'clock, clockwise positive.

| id | Label | Centre x, y (mm) | Bezel Ø | Knob | Scale | Range |
|---|---|---|---|---|---|---|
| knob_tuning | TUNING | 89.23, 15.32 | 15.84 | small knurled | 11 ticks every 30°, square marker at 0° | −150…+150° |
| knob_cutoff | CUT OFF FREQ | 111.69, 15.32 | 15.84 | small knurled | same | −150…+150° |
| knob_resonance | RESONANCE | 134.16, 15.32 | 15.84 | small knurled | same | −150…+150° |
| knob_envmod | ENV MOD | 156.63, 15.32 | 15.84 | small knurled | same | −150…+150° |
| knob_decay | DECAY | 179.10, 15.32 | 15.84 | small knurled | same | −150…+150° |
| knob_accent | ACCENT | 201.57, 15.32 | 15.84 | small knurled | same | −150…+150° |
| knob_tempo | TEMPO | 44.01, 51.73 | 23.39 | large knurled | 21 ticks every 15°: long at multiples of 30°, short between; ±150° marks are slashes by SLOW / FAST | −150…+150° |
| switch_patt_group | TRACK / PATT.GROUP | 89.23, 51.73 | 23.39 | bar knob | 7 dots | detents −105, −75, −45, −15, +15, +45, +75° |
| switch_mode | MODE | 134.16, 51.73 | 23.39 | bar knob | 4 dots | detents +45, +75, +105, +135° |
| knob_volume | VOLUME (POWER SW) | 262.30, 51.73 | 23.39 | large knurled | as TEMPO; slashes by OFF / MAX | −150…+150°; the power switch clicks just after OFF |

### Small tone knobs (Roland part N-195, 16 mm pots)

- **Well:** each knob sits in a round well. The top view shows, from the outside in:
  - a bright chrome rim, OD 15.84 mm and about 1 mm wide;
  - a dark gap of about 0.7 mm;
  - the knob.
- **Knob body:** a knurled cylinder, very slightly tapered.
  - Ø about 12.4 mm at the skirt;
  - height about 12 mm above the panel;
  - fine straight knurl, roughly 60–70 ridges (pitch about 0.6 mm; this count is uncertain).
- **Top:** a flat satin-aluminium disc, Ø about 8.8 mm. A **small rectangular notch** (≈1 mm wide, ≈0.7 mm deep) is cut in the disc's rim as the pointer. Close-ups (`soundgas/sn163700_Roland__TB-303_Bass_Line_4.jpg`) show no line across the cap.
- **Perspective:** the knobs are tall, so a top-down render should push the cap a little away from the panel centre and show some skirt on the far side, as the photo does.
- **Scale:**
  - ticks run from r = 8.66 to 10.4 mm, about 0.4 mm wide, at −150, −120 … +150°;
  - at 0° there is a **1.3 mm filled square** (centre r = 9.4 mm) instead of a tick;
  - the 3 o'clock tick of one knob and the 9 o'clock tick of the next almost touch, so the row reads as a dashed line.
- **Legend:** centred above each knob at y = 3.5 mm, cap height 1.9 mm.

### Large knobs (TEMPO, VOLUME; part N-194)

- **Well:** chrome rim OD 23.4 mm.
- **Knob:** knurled skirt Ø about 19.5 mm, flat cap Ø about 15 mm, height about 14 mm, with a notch pointer at the cap edge as on the small knobs. VOLUME on the master unit also shows an engraved radial line.
- **Scale:** ticks start at r = 12.8 mm. Long ticks reach r = 14.8 mm and short ticks reach r = 13.7 mm. Width is about 0.45 mm.

### Rotary switches (PATT.GROUP, MODE; knob N-198)

- **Well and base:** same 23.4 mm chrome well; round base Ø about 19 mm.
- **Fin:** a tall bar runs across the full diameter, about 7 mm wide and about 13 mm high. It has a flat top and sloped ends; one end is the pointer, marked by a small step or notch on its top edge.
- **Position dots:** Ø 0.9 mm, at r ≈ 13.6 mm (PATT.GROUP) and ≈ 13.4 mm (MODE). Dot centres are in the JSON (`dot_pattgroup_n`, `dot_mode_n`).
- **PATT.GROUP legends:** black boxes about 3.0 × 2.3 mm with white figures 1–7, placed beside the dots, plus Times roman numerals and bracket keylines.

  | Group | Positions |
  |---|---|
  | **I** | 1 and 2 (bracket on the left, "I" at x 69.2, y 52.2) |
  | **II** | 3 and 4 ("II", then a line to 4) |
  | **III** | 5 and 6 ("5", then a line to "III") |
  | **IV** | 7 ("IV" below a bracket hook, x 108.9, y 51.7) |

  - Above the switch: "TRACK" (reverse box, x 71.4–83.3, y 30.1–33.3) followed by "PATT.GROUP".
  - Box centres: 1 (73.3, 54.9), 2 (73.3, 48.2), 3 (76.3, 42.1), 4 (82.4, 37.1), 5 (95.9, 37.1), 6 (101.6, 42.1), 7 (105.2, 48.2).
- **MODE legends:**

  | Position | Legend | Style | Centre (mm) |
  |---|---|---|---|
  | +45° | WRITE | reverse box | 149.9, 42.2 |
  | +75° | PLAY | reverse box | 152.7, 48.1 |
  | +105° | PLAY | plain black text | 152.7, 55.3 |
  | +135° | WRITE | plain black text | 149.7, 61.0 |

  - A square bracket joins the two reverse boxes to a reverse "TRACK" box (165.1, 45.1).
  - A second bracket joins the two plain legends to plain "PATTERN" (166.3, 58.6).

## 7. Keys (push buttons)

**Construction:**
- Every key is the same rectangular cap (Roland N-191, switch KHC-10901). It has a flat top and a chamfered bevel all round, and stands about 3–4 mm proud of the panel in a rectangular cut-out.
- **Normal key cut-out:** 6.35 × 12.34 mm. The visible cap face is about 5.5 × 11.5 mm.
- **Wide keys** (RUN/STOP and WRITE/NEXT-TAP, cap N-192): cut-out 18.6 × 12.34 mm.
- **Horizontal keys** (PITCH MODE, TIME MODE, PATTERN CLEAR, BACK) are the normal cap turned 90°: 12.34 × 6.35 mm.
- The master unit's RUN/STOP cap shows a faint vertical moulding line at about 75 % of its width.

| id | Label / function | Centre x, y | Cut-out w × h |
|---|---|---|---|
| key_C, D, E, F, G, A, B, C2 | Notes C … C′; pattern 1–8; selector 1–8 | x = 74.31, 90.66, 107.09, 123.48, 139.87, 156.26, 172.65, 189.04; y = 111.14 | 6.35 × 12.34 |
| key_Cs, Ds, Fs, Gs, As | C♯ (DEL), D♯ (INS), F♯, G♯, A♯ | x = 82.54, 98.90, 131.77, 148.16, 164.55; y = 91.62 | 6.35 × 12.34 |
| key_down | TRANSPOSE DOWN / STEP / 9 / note | 206.45, 111.14 | 6.35 × 12.34 |
| key_up | TRANSPOSE UP / triplet / 0 / tie | 222.74, 111.14 | 6.35 × 12.34 |
| key_accent | ACCENT / A / 100 / rest | 239.13, 111.14 | 6.35 × 12.34 |
| key_slide | SLIDE / B / 200 | 255.52, 111.14 | 6.35 × 12.34 |
| key_function | FUNCTION (BAR) | 44.62, 111.14 | 6.35 × 12.34 |
| key_pitch_mode | PITCH MODE | 50.59, 90.94 | 12.34 × 6.35 |
| key_time_mode | TIME MODE | 254.60, 86.98 | 12.34 × 6.35 |
| key_pattern_clear | PATTERN CLEAR (D.C. / BAR RESET) | 21.09, 90.88 | 12.34 × 6.35 |
| key_back | BACK (segno) | 279.10, 91.07 | 12.34 × 6.35 |
| key_run_stop | RUN/STOP | 21.07, 111.14 | 18.6 × 12.34 |
| key_tap | WRITE/NEXT, TAP (D.S.) | 279.19, 111.23 | 18.6 × 12.34 |

## 8. LEDs

- **Part and size:** all 21 LEDs are 3 mm red (Sharp GL-3AR-2), domed, sitting just proud of the panel. Draw them Ø 3.0 mm.
- **Row y-values:**
  - upper row: y ≈ 82.0 (black keys and TIME MODE), with PITCH MODE at 82.85;
  - lower row: y ≈ 101.3.
- **Lit state:** use `led_lit` with a glow. **Unlit state:** use `led_unlit` with a highlight.

| LED | x, y | LED | x, y |
|---|---|---|---|
| led_pitch_mode (in black PITCH MODE block) | 51.01, 82.85 | led_run (between RUN and BATTERY) | 21.05, 101.34 |
| led_Cs | 82.76, 81.95 | led_function (right of FUNCTION) | 60.11, 101.42 |
| led_Ds | 99.12, 82.04 | led_C | 74.31, 101.38 |
| led_Fs | 131.84, 82.06 | led_D | 90.66, 101.16 |
| led_Gs | 148.25, 81.99 | led_E | 107.29, 101.36 |
| led_As | 164.55, 81.95 | led_F | 123.57, 101.27 |
| led_time_mode (under "TIME MODE") | 231.05, 82.17 | led_G | 140.09, 101.31 |
| led_A | 156.35, 101.34 | led_B | 172.76, 101.34 |
| led_C2 | 189.21, 101.33 | led_down | 206.56, 101.23 |
| led_up | 222.85, 101.36 | led_accent | 239.36, 101.33 |
| led_slide | 255.82, 101.49 | | |

## 9. Printed graphics on the keyboard sub-panel

All keylines are black and 0.25–0.3 mm wide. Rectangles below are given as [x0, y0, x1, y1] in mm.

- **Outer frame keyline:** [4.97, 73.66, 295.21, 133.20], drawn round everything.
- **Section boxes** (keylines):

  | Box | Rectangle | Contents |
  |---|---|---|
  | PATTERN CLEAR | [6.46, 75.27, 35.14, 97.15] | D.C. box, BAR RESET box, PATTERN CLEAR legend and key |
  | RUN/STOP | [6.46, 97.99, 35.14, 131.95] | |
  | FUNCTION | [36.11, 97.99, 64.88, 131.95] | |
  | Main keyboard | [36.19, 75.27, 263.63, 131.95] | Keyboard plus TIME MODE / TRANSPOSE / ACCENT / SLIDE |
  | BACK | [264.64, 75.27, 293.28, 97.05] | |
  | TAP | [264.64, 97.94, 293.28, 131.94] | |

- **Black fills:**
  - PITCH MODE block [36.28, 75.27, 64.88, 97.15] (white "PITCH MODE", the LED and the key sit on it).
  - Note-name band [36.28, 75.27, 200.84, 79.37].
  - Black strip right of the keyboard [197.89, 75.27, 200.84, 97.15].
  - TRANSPOSE / ACCENT / SLIDE band [197.88, 91.07, 263.62, 97.15].
  - **PATTERN band:** an L-shape. The horizontal part is [53.37, 119.43, 198.07, 125.41]; the riser under NORMAL MODE is [53.37, 111.23, 64.83, 119.43]. Bottom corners are rounded (r ≈ 1 mm). A thin keyline runs about 0.75 mm outside it, from y 111.2 down the left side, along y ≈ 126.2 and up the right end. The DEL and INS tabs hang from this keyline.
  - **PATT. SECTION band:** [231.12, 119.52, 263.63, 125.45], bottom-left corner rounded. Inside it, a white rule at y 122.3 and a white vertical at x 247.2 (y 119.6–121.8) separate A from B.
- **Printed white keys:** 8 off-white rounded rectangles, y 79.56–119.34, with bottom corners r ≈ 0.75 mm. A 0.2 mm black line separates each pair. x ranges:

  | Key | x range |
  |---|---|
  | C | 65.9–82.1 |
  | D | 82.3–98.5 |
  | E | 98.7–114.9 |
  | F | 115.1–131.5 |
  | G | 131.7–147.7 |
  | A | 147.9–164.3 |
  | B | 164.5–180.6 |
  | C′ | 180.9–197.0 |

  The E|F and B|C′ separators run the full height up to the note band.
- **Printed black keys:** 9.2 mm wide, y 79.83–103.05, bottom corners r ≈ 1.1 mm. Each carries its LED, its upper key and (C♯, D♯) a white DEL / INS label near the bottom (y ≈ 101.0). x ranges:

  | Key | x range |
  |---|---|
  | C♯ | 77.72–86.92 |
  | D♯ | 94.11–103.31 |
  | F♯ | 126.98–136.28 |
  | G♯ | 143.37–152.49 |
  | A♯ | 159.67–168.88 |

- **TIME MODE area:** the silver area x 200.8–263.6, y 75.3–91.1.
  - Three thin vertical dividers at x 214.5, 230.9 and 247.2 (y ≈ 83.5–91.1); the middle one hangs from the TIME MODE LED.
  - Symbols, left to right:
    - "● ♬" (filled dot + sixteenth note, meaning *note*), centre (206.4, 87.3);
    - "○ ♬‿" (hollow dot + tied sixteenth, meaning *tie*), centre (221.6, 87.6);
    - "– 𝄿" (dash + sixteenth rest, meaning *rest*), centre (238.6, 86.9).
  - Fourth cell: the TIME MODE key.
- **TRANSPOSE band contents** (all white):
  - "TRANSPOSE" at (214.6, 92.5) with a white rule under it (x 200.9–228.9, y 94.1);
  - "DOWN" at (206.4, 95.6) and "UP" at (222.8, 95.6), split by a short white vertical;
  - white verticals at x 230.9 and 247.2 run the band's full height;
  - "ACCENT" at (239.2, 94.1) and "SLIDE" at (254.6, 94.1).
- **Lower dividers:** below the band, black verticals at x 214.5 (to y 126.4), 230.9 and 247.2 (to y 119.4) separate the DOWN, UP, ACCENT and SLIDE columns.

## 10. All printed legends

Positions are text-box centres in mm; "cap" is the glyph height in mm. Exact wording and capitalisation are as shown. The JSON has bounding boxes for every item.

**Tone strip** (Helvetica Medium; jack legends under filled ▲ triangles 2.0–2.4 × 2.0 mm at y ≈ 3.3–3.9):

| Legend | Centre | Cap | Note |
|---|---|---|---|
| MIX IN | 14.6, 6.3 | 1.8 | ▲ at 14.7, 3.3 |
| WAVEFORM | 35.3, 6.3 | 1.8 | Above it, from left: sawtooth glyph (2 teeth, x 28.0–33.4), ▲ (35.2, 3.3), square-wave glyph (2 cycles, x 36.7–43.0) |
| SYNC IN | 59.8, 6.3 | 1.8 | ▲ at 59.5 |
| TUNING / CUT OFF FREQ / RESONANCE / ENV MOD / DECAY / ACCENT | above each knob, y 3.5 | 1.9 | A 1.3 mm filled square sits between each legend and its knob, centre y 5.89 |
| CV | 219.6, 6.3 | 1.8 | ▲ at 219.7 |
| GATE | 232.9, 6.5 | 1.8 | ▲ at 233.0 |
| HEADPHONE | 248.3, 6.5 | 1.8 | ▲ at 248.2 |
| OUTPUT | 265.8, 6.5 | 1.8 | ▲ at 265.6 |
| DC 9V | 283.0, 6.6 | 1.7 | ▲ at 282.9 |
| roland slot | 39.2, 17.8 | 7.4 (box h) | |
| bass_line slot | 255.2, 17.9 | 7.2 | |

**Middle section:**

| Legend | Centre | Cap |
|---|---|---|
| TEMPO | 44.4, 31.7 | 2.5 |
| SLOW / FAST (with slashes) | 32.3, 63.8 / 55.6, 63.7 | 1.7 |
| [TRACK] (reverse) + PATT.GROUP | 77.3, 31.7 + 96.3, 31.7 | 2.5 |
| MODE | 133.9, 31.5 | 2.5 |
| TB-303 | 222.0, 47.5 | 6.1 |
| Computer Controlled | 208.1, 55.8 | ≈5.0 |
| VOLUME | 262.2, 31.9 | 2.5 |
| POWER SW · OFF · MAX | 240.6, 63.7 · 251.2, 63.9 (smaller, 1.6) · 273.4, 63.8 (1.4) | 1.9 |

**Keyboard sub-panel:**

| Legend | Centre | Cap / style |
|---|---|---|
| D.C. (outline box 7.5 × 4.1) | 11.2, 78.7 | Times Bold Italic |
| BAR RESET (outline box 18.4 × 4.1) | 24.9, 78.7 | Helvetica, 1.9 |
| PATTERN CLEAR | 20.8, 84.5 | 1.9 |
| RUN · BATTERY | 16.0, 101.0 · 28.5, 101.0 | 1.8 |
| RUN/STOP | 20.7, 122.3 | 2.2 |
| PITCH MODE (white) | 50.6, 77.9 | Bold, 3.1 |
| FUNCTION | 47.2, 101.1 | regular, 2.5 |
| NORMAL / MODE (two lines) | 58.2, 106.3 / 58.1, 108.8 | 1.8 |
| [BAR] (reverse box 8.2 × 4.1) | 44.4, 122.4 | Bold |
| PATTERN (orange) | 61.2, 122.2 | Bold, 2.2 |
| 1 2 3 4 5 6 7 8 (orange, in PATTERN band) | x = 74.2, 90.6, 107.2, 123.3, 139.7, 155.9, 172.6, 188.8; y 122.3 | Bold, 2.5 |
| SELECTOR | 60.1, 129.1 | regular, 3.1 |
| Selector boxes 1–8, 9, 0, 100, 200 (reverse, 5.7–6.3 × 3.4; 7.0–7.2 wide for 100/200) | x = 73.9, 90.3, 106.7, 123.2, 139.6, 156.0, 172.6, 189.0, 206.4, 222.7, 239.1, 255.4; y 129.2 | Bold |
| DEL / INS tabs (reverse, 7.5 × 3.9, hanging) | 82.1, 128.2 / 98.6, 128.2 | Bold |
| Note names C C♯ D D♯ E F F♯ G G♯ A A♯ B C (white, in band) | x = 74.0, 82.6, 90.7, 98.7, 106.9, 123.3, 131.9, 139.6, 147.9, 156.2, 164.4, 172.5, 188.9; y 77.5 | Bold, 2.3 |
| DEL / INS on black keys (white) | 82.3, 101.0 / 98.7, 101.0 | Bold, 2.1 |
| TIME MODE | 230.9, 78.1 | 2.4 |
| STEP | 206.3, 122.6 | 1.9 |
| Triplet glyph (3 beamed eighths over "⌊3⌋") | 222.7, 122.8 | 6.8 × 5.7 box |
| A · B (orange) | 238.9, 120.8 · 255.4, 120.8 | 1.7 |
| PATT. SECTION (orange) | 247.0, 123.8 | Bold, 2.1 |
| Segno 𝄋 (outline box 7.0 × 3.9) | 279.0, 78.8 | serif |
| BACK | 279.1, 84.5 | 1.9 |
| D.S. (outline box 7.3 × 4.1) | 279.0, 101.2 | Times Bold Italic |
| WRITE/NEXT (outline box 20.5 × 4.4) | 279.1, 122.5 | 1.9 |
| TAP | 279.2, 129.0 | 1.8 |

## 11. WAVEFORM switch and rear panel

- **The WAVEFORM switch is not on the top panel.** It is a small black 2-position slide switch on the **rear face**, directly behind the "WAVEFORM" legend. The legend's ▲ points at it, with the sawtooth to the left and the square to the right (as seen from the front).
- **UI stand-in:** Roland's own TB-303 plug-in draws a tiny slide switch at the top-left edge above the legend. `panel.json → ui_suggestions.ui_waveform_switch` suggests a 7 × 1.9 mm slider centred at (35.2, 1.15), squeezed between the rear edge and the waveform glyphs (which start at y 2.3).
- **Rear jacks:** each sits directly behind its ▲ on the top panel. In top-panel x (mm):

  | Jack | x (mm) | Type |
  |---|---|---|
  | MIX IN | 14.7 | 6.3 mm jack |
  | WAVEFORM | 35.2 | slide switch |
  | SYNC IN | 59.5 | 5-pin DIN, black |
  | CV | 219.7 | 3.5 mm |
  | GATE | 233.0 | 3.5 mm |
  | HEADPHONE | 248.2 | 6.3 mm stereo |
  | OUTPUT | 265.6 | 6.3 mm |
  | DC 9V | 282.9 | barrel in a square black housing |

- **Rear face:** about 40 mm tall, same silver. A horizontal moulded seam runs at about jack-centre height and a second seam sits near the bottom plate. See `soundgas/sn163500_Roland_TB-303_5534.jpg` and `_5535.jpg`, and manual p. 74.

## 12. Uncertainties and notes

1. **Depth.** The top view is 142 mm; the spec says 146 mm (see §2). The y-scale also carries a possible 1–1.5 % camera-tilt error. The flatbed VSE scan and the master photo differ by about ±1.3 % in y at different feature pairs.
2. **3-D sizes are estimates:** knob heights and diameters, cap sizes, key height and bar-knob fin size. Only the top-view footprints (bezels, cut-outs, ticks) are measured.
3. **Knurl ridge count** is uncertain: about 60–70 on the small knobs and about 80–100 on the large ones.
4. **Rotary detents are snapped to 30°:**
   - PATT.GROUP was measured at −104, −74, −45, −14, +14, +44, +74°, giving nominal −105 … +75.
   - MODE was measured at 43, 74, 105, 135°, giving nominal 45 … 135.
   - Which end of the bar is the pointer was not confirmed on a labelled photo; assume the end with the notch or step.
5. **Colours** depend on lighting. Treat the render values as a starting point and tune them against `soundgas/sn163500_Roland_TB-303_163500_4.png` (neutral, bright) and the master photo (daylight, warmer). Orange print is the least certain value.
6. **Print weight.** On the master unit the MIX IN, WAVEFORM and SYNC IN legends look slightly bolder than CV … DC 9V. The other units photographed show the same weight, so this is probably print variation; use Helvetica Medium for all of them.
7. **Snapping.** PATT.GROUP and MODE centres were snapped to the TUNING and RESONANCE x positions; raw measurements were within 0.2–0.4 mm. The tone-knob pitch of 122 px = 22.47 mm is very likely 22.5 mm by design. The white-key pitch of 16.41 mm may be 16.5 mm by design.
8. **NORMAL MODE** has no box around it; it is printed on the silver above the black riser of the PATTERN band.
9. **Lit LEDs.** None of the straight-on photos show LEDs lit. The lit colour comes from the angled close-up `commons_tb303_closeup.jpg`.
