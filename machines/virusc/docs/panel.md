# Virus C top panel: how `panel.json` was measured

`panel.json` is a to-scale description of the desktop Virus C's top panel for drawing it in the
plug-in, in the style of `machines/tr808/docs/panel.json`. It has 367 elements:

- case, end caps, plate and section boxes;
- 32 knobs, 35 buttons and 69 LEDs;
- the display, legends, rules, tags and symbols;
- three logo slots.

It also has a colour table, font notes and the rear jack order. Coordinates are mm from the rear-left
corner of the top view (end caps included), x to the right, y towards the player.

The brand marks (the maker's emblem and wordmark, the model lettering, and "ADVANCED SIMULATED ANALOG
SYNTHESIZER") are slots only (`type: logo`, ids `maker`, `name`, `tagline`). Nothing in this file
traces them.

## Sources

All files are under `private/virusc/docs/photos/` (reference only).

| Role | File |
|---|---|
| Geometry master | `Access_Virus_C.jpg` (2930 x 1240, Commons "File:Access Virus C .jpg", CC BY-SA 2.0): near straight-on, knobs fitted, LEDs and display lit |
| Cross-check, holes and print | `Access_Virus_C_Desktop_-6875867395-.jpg` (2048 x 739, Commons / Flickr, CC BY 2.0): bare faceplate, upside down at an angle |
| Scale | `Access_Virus_C_Desktop_-6875865237-.jpg` (panel PCB, solder side); `Access_Virus_C_Desktop.jpg` (display close-up) |
| Rear panel | `Access_Virus_C_Desktop_-6875895299-.jpg` (straight-on rear view) |
| Control names, MAME indices | `private/virusc/reference/mame/acvirus.cpp`, `virusc` ports |

The bare plate was rotated 180 degrees and mapped onto the rectified master with SIFT and a RANSAC
homography (49 inliers). A cubic warp fitted to 654 local template matches on print followed (median
residual 0.6 px). Every measurement was then made on 8 px/mm resamplings of both photos in the same mm
frame. The scripts lived in a scratch folder and are not part of the repo; the method is described below
in enough detail to redo it.

## Frame and scale

- **Rectification.** The master was not taken square:
  - its y is stretched by about 3.4 %;
  - it has slight barrel distortion, keystone and shear.
- **Fitted model.** One model was fitted to 39 traced straight print lines (section bars and rules, box
  edges, display frame rails, the case lip), each required to be straight and level or plumb.
  - Model terms: a radial term, two projective terms, a shear, and a y factor of 0.9661.
  - Line rms 0.35 px (1.15 px before).
  - The y factor makes the 32 printed dot rings round (rms 0.23 px).
  - The display's line pitch / character pitch comes out at 1.678 against the module's 1.676, an
    independent check of the aspect.
  - `frame.mapping` gives the numbers.
- **Scale: 6.02 rectified px/mm, +-0.8 %.** Three checks:
  - **Display.** The module is a 2 x 16 character LCD, a TRULY MCC162B6-2 according to a PCB photo. Its
    datasheet was not found.
    - HD44780 5 x 7 glyphs were fitted to "1 VOCODER / Mode Off" in the master: character pitch 21.25 px
      and line pitch 35.66 px.
    - The standard 16 x 2 values (3.55 x 5.95 mm) give 5.99 px/mm.
    - In the close-up, the dot lattice gives character/dot pitch 5.94 (standard 5.92) and line/dot pitch
      8.52 (standard 8.50).
  - **PCB.** Pot lug pairs on the solder side give the 32 pot centres. A homography onto the knob centres
    fits to 1.75 px (0.29 mm).
    - Through it, the pin pitch of 10 rows of DIP footprints (2.54 mm) gives 6.07 px/mm (sd 0.05).
    - The 2 x 20 header gives 5.95.
  - **Adopted value: 6.02,** with all three inside +-1 %.
- **Size.** Top view **470.9 x 181.1 mm** (+-3.5 / +-1.5 mm), end caps included, rear sockets excluded.
  - zZounds lists 470 x 185 x 75 mm. The 185 mm depth plausibly includes the rear sockets.
  - Spectrasonics' 380 x 250 x 73 mm does not fit: its aspect is 1.52, the photos give 2.60.
  - Plate: x 13.7-455.0, y 3.8-169.8. Front lip to y 177.6. The cap feet reach y 181.1.
- **Parallax.** Knob tops lean outwards from (248.9, 92.5) mm by 2.82 % of their distance from it.
  - With a knob height of 16.5-17.5 mm, the camera was about 600-640 mm away.
  - The case outline was moved 1.2 mm inwards at the caps (cap top taken as ~3 mm above the plate).
  - Button caps were moved for a 3.5 mm cap height.
  - No centre comes from a raised part alone.

## Method by element group

- **Case.**
  - The outline is traced against the white background (approxPolyDP 0.35 mm).
  - Caps, gaps, plate edges, the rear and front bends and the lip come from intensity profiles across them.
  - The left cap's rounded inner edge is lit, the right one's is in shade, so the right gap reads wider.
- **Section boxes.** Each box has three parts:
  - a near-black fill; its right and bottom edges are plain fill edges;
  - a 2.6 mm blue-grey bar down the left side;
  - a 0.45 mm rule along the top, at tab height over the title, stepping down ~3 mm at about 30 degrees to
    run to the right edge.

  How they were measured:
  - Bars: edge traces.
  - Rules: ridge traces.
  - The slants: fitted to blue-grey pixels.
  - Fill edges: median profiles.

  EFFECTS and DELAY / REVERB share one box. FILTERS and AMPLIFIER share the bar.
- **Knobs.** The centre is the circle through the printed dots, the mean of master and bare plate (they
  agree to 0.08 mm median, 0.27 mm max).
  - The scale has 13 marks 21.67 degrees apart (sd 0.07) at r 10.55 mm. Dots sit on the inner marks.
  - Numbers or glyphs take marks 0, 6 and 12. Number offsets are very regular: (0.1, -10.5), (-8.0, +6.85)
    and (+8.2, +6.85) mm.
  - OSC VOL carries SATURATION along the arc instead of its right-hand dots.
  - SOFT KNOB 1/2 have - / + at the ends and a dot at the top.
  - `mame_knob` follows MAME's `virusc_knobs` names. All 32 match a printed label.
- **Buttons.** All 35 are 11.7 mm square caps in cut-outs of 12.65 mm per button, shared by pairs and
  triples.
  - Cut-out edges come from the bare plate; caps come from the master, parallax removed.
  - x is the cut-out's even split; y is the mean of cut-out and cap.
  - Measured cut-outs that read wide (shadow) or narrow (FILT EDIT / FILT 1) were replaced by the standard
    width, centred between cut-out and caps.
- **LEDs.** Each centre is the bare-plate hole: top edge + D/2 + 0.35 mm.
  - Through-holes lose their lower edge to the plate thickness.
  - On the 32 knob holes the top edge reads 0.35 mm high. The same holds for the LED holes against the
    lit glows (median offset 0.04 / -0.21 mm, sd 0.25 / 0.40).
  - Six LEDs have no clean hole and take their column and row from their neighbours: LFO 1 select,
    2/3, LFO 2 destinations 3 and 6, LFO 3 destination 1, MULTI.
- **Legends.** Each legend is the ink union bbox in a search box, with components touching the border
  dropped.
  - Ink masks: on the bare plate, light and low-saturation pixels above the local ground; on the master, a
    top-hat filter that excludes red pixels.
  - Both photos are measured. If the two agree within 0.6 mm in width they are averaged (115 legends);
    otherwise the box with the right height and the larger width is kept (27 bare, 9 master).
  - Read by hand from 8 px/mm crops: BPM, PANIC, EXT IN R (it merges with its bar), SATURATION (arc).
- **Display.** `char_origin` comes from the glyph fit. The pitches are the standard values; the window
  and bezel are traced rails.
- **Colours.** The plate, case, knobs and buttons come from four other photos with neutral surroundings:
  a studio shot on white and three listing photos (daylight, room light). Each is white-balanced so the
  legend print is neutral, and every value is taken as a share of the print's luminance. The master is
  lamp-lit, and its values came out brown, so only the LED and LCD colours are still from it.
  - The plate is a near-black slate inside the section boxes and a dark slate outside them, both faintly
    blue. Knobs and buttons are slate grey. How light they look depends on the light: buttons range from
    near the plate (daylight, top down) to light slate (studio).
- **Rear panel.** Jack centres come from Hough circles on the rear photo, mapped to top-panel x by a linear
  fit to the jack names printed along the top panel's rear edge (rms 0.52 mm). The order runs from MIDI
  THRU at the left to ON/OFF at the right, as seen from the front.

## Accuracy

| What | Estimate | Evidence |
|---|---|---|
| Knob centres | +-0.15 mm | Master and bare plate rings agree, median 0.08 mm |
| Printed legends, rules, bars | +-0.2 mm | Bbox includes ~0.1 mm of blur per side; the two photos agree to <=0.2 mm on most |
| LEDs | +-0.3 mm | Hole bias corrected; glow scatter 0.25 / 0.40 mm |
| Buttons, cut-outs | +-0.4 mm | Shadows on cut-out edges |
| Hand-read items, link lines | +-0.4 mm | From crops |
| Case, caps, gaps, lip | +-1 mm | Lighting, cap height |
| Absolute scale | +-0.8 % | Display 5.99 vs PCB 6.07 / 5.95 |
| 3-D sizes | +-1.5 mm | Knob 13.0 / 12.3 mm, height 16.5 (rear photo); cap height 3.5 (parallax); knob hole 9.8 (bare plate) |
| Rear panel | +-2 mm in x, +-3 mm in y | Rear photo perspective |

## Uncertain or open

1. **Scale.** The datasheet of the display module was not found, so the 3.55 mm pitch is the standard
   value, not the module's own.
2. **Buttons not matched to MAME.** The centre EDIT, SYSTEM / MULTI EDIT and the lower LFOS / MOD SELECT
   have no `mame_key`.
   - By elimination they are probably MAME's "Edit" (row 1 bit 2), "Global" (row 1 bit 3) and
     "LFOs/Mod: Amount" (row 0 bit 6). The manual describes the lower SELECT under AMOUNT.
   - TRANSPOSE left = down and right = up are assumed from the LED order.
   - The firmware will settle all of them.
3. **LED matrix.** MAME drives a 16 x 8 matrix. Which bit lights which of the 69 LEDs is not known yet.
4. **End caps.** Their height above the plate (~3 mm) and the right cap's inner edge are estimates.
5. **Bare-plate registration** is weakest along the rear edge. The rules near y 19 read 0.1-0.4 mm lower
   there, so positions there are from the master.
6. **Front lip.** It is seen at a grazing angle; its 7.8 mm is not a true dimension.
7. **Pot travel.** The rotation range is the scale's end marks (+-130 degrees); the pots' mechanical stops
   were not measured.
8. **Unlit LEDs and dim legends** (AUDITION, PANIC, DEMO, `print_dim_blue`) have estimated colours.

## Conventions and new element types

- These are as in the TR-808 file:
  - `text` bbox = ink box;
  - `outline_box` bbox = outer stroke edge;
  - `line` points = stroke centre;
  - `nx` and `ny` normalised to 470.86 / 181.06.
- New types:
  - `knob` with `scale.marks`, `dot_marks`, `labels` and an optional `zero_bar`;
  - `lcd` (`char_origin`, pitches, cell and dot sizes);
  - `text_arc` (SATURATION);
  - `label_box` tags (1, 2, 3, MOD, VALUE: blue-grey fill, light text).
- `button.cutout_bbox` is the shared panel opening; `cutout_shared_by` says how many caps sit in it.
- Interactive elements carry `param`. Knobs carry `mame_knob`; buttons carry `mame_key [row, bit]`
  where the match is clear.
