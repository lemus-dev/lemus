# Oberheim Xpander: panel layout spec

This spec is for drawing the Xpander's top panel and rear in the plug-in's hardware view. The
machine-readable version is `docs/panel.json`: 574 top-view elements in mm, plus the rear panel. This
document says how it was measured and adds what the JSON cannot say well: materials, 3-D shapes, fonts,
the switch matrix and what is still uncertain.

Target: a US production unit (model XP-1, 1984-88). Every unit photographed has the same top-panel print.
The rear jack strip comes in two print versions (section 12).

## 1. Sources and method

Photos are reference only and live in `private/xpander/docs/photos/`. Their list, owners and SHA1s are
in `docs/sources.md` section 4.

- **Master photo:** `reverb/103212593/103212593_0.jpg`.
  - A near straight-down view of the whole unit: iPhone 16 main camera (26 mm equivalent), 5712 x 4284,
    about 6.3 px/mm on the panel.
  - The raw pixels are upright; its EXIF orientation tag is wrong and was ignored.
  - The camera was only about 0.67 m above the panel, so raised parts show parallax (below).
- **Scale reference: the pot board artwork.** The service manual's PCB foldout (scan A, image 51) carries
  every pot-board switch, the six encoders, the VOLUME pot, eleven LEDs and all rear jacks.
  - Scale: its 0.1 in IC pin pitch. The foldout's two halves were reproduced at slightly different
    scales (9.580 and 9.657 px per 0.1 in), so each half is scaled by its own pins.
  - The paper is compressed near the fold (scan x 950-1150), so points there were left out of the fit.
  - The scan is also about 1 % short in y on both halves (encoder outlines are ellipses, ry/rx 0.983-0.993).
    It is therefore used only for x scale and for knob centres, not for aspect.
- **Photo correction:**
  1. Thirty long straight print features were fitted: blue bars, bottom rules, rear-legend rules,
     diagram box edges, long diagram verticals and the panel edges. They bow 3-5 px towards the image
     centre.
  2. A radial term (k1 = +0.023 on the normalised radius, about 0.7 % at the panel ends) plus a small
     perspective term make them straight and parallel. Line rms drops from 1.13 to 0.65 px.
  3. A sag model (panel ends about 5 mm nearer the camera) fits equally well. Either way the correction
     is the same to a fraction of a pixel.
- **Scale and its check:**
  - 59 button caps were fitted in the photo: square top face, fixed-width two-edge gradient match, about
    ±1 px.
  - The x scale comes from per-half fits of the caps against the PCB, through the pin pitch. The two
    halves agree to 0.38 %.
  - Independent check: the adjacent-key pitch (keypad, the 14 mod-source keys, PATCH EDIT) comes out at
    16.48-16.55 mm, i.e. 0.650 in to 0.2 %.
- **Cap parallax:**
  - Cap tops stand about 3 mm proud, so in the photo they sit outward from their holes. Measured from
    the hole gaps: κ = h/(D-h) = 0.0044 ± 0.001.
  - Buttons are given at their plan (switch) positions; the print is mapped with the panel-plane scale.
  - Check: on the side facing the camera the hole edge is visible. The hole centres derived from it
    agree with the plan positions to 0.2 mm (SINGLE, TUNE, VCF/VCA, MISC.).
- **Measuring:** the photo was resampled to 10 px/mm in the panel frame and measured with Python and
  OpenCV:
  - print mask (background-subtracted) → connected components → legend groups, every string read by eye;
  - morphological line extraction → snapped, merged segments;
  - diagram boxes found from their labels;
  - Hough circles plus radial profiles for the knob symbols;
  - FWHM profiles for stroke widths;
  - redness centroids for the LEDs.
- **Overlay check:** panel.json was drawn back onto the rectified photo, and also rendered on its own.
  Legends, boxes, lines, knob symbols, LEDs and VFD windows sit on the print to about 0.3 mm.
- **Inventory and names:** MAME `oberheim_xpander.lay` (CC0) and `xpander.cpp` (input ports, LED
  latches). The owner's and service manuals give no dimensions.
- **Rear:**
  - Jack x positions from the pot-board artwork (the threaded bushings drawn at the board edge).
  - MIDI, CASSETTE and MEMORY PROTECT x from the processor-board artwork (scan A image 50), registered on
    its five page switches (0.30 mm rms).
  - Heights and the lower panel from the straight-on Perfect Circuit rear photo
    (`vintagesynth/oberheim_xpander_rear_lg.jpg`).
- **Regenerating:** `private/xpander/docs/photos/tools/panel/` holds the scripts (`xmap.py` is the
  photo → mm mapping, `build_panel.py` writes this JSON, `overlay.py` draws the check), the
  intermediate measurements (`data/`) and the overlay renders. The scripts still point at the scratch
  folder they ran in, so adjust the paths before re-running.

## 2. Dimensions and frame

| | Value |
|---|---|
| **Outline (top view)** | **841.2 x 312.5 mm**. Width from the photo, with the end-bell tops corrected for parallax. Dealer listings give 33 x 12 x 6 in (838 x 305 x 152 mm) and 27-28 lb. Oberheim gives no dimensions. |
| Origin | Rear-left corner of the top view, at the rear end of the left end-bell. **x** runs right, **y** runs towards the player. mm. |
| Normalised | `nx = x/841.2`, `ny = y/312.5` |
| Slope | The whole top slopes up towards the rear (wedge end-bells). The photo is square to the top surface (camera tilt about 1°), so y and all print are measured **along the surface**. |
| Printed panel | x 14.07-827.72, y 20.55-292.55 (813.7 x 272.0 mm) |

**Why 841.2 mm.**
- The end-bell tops stand about 5 mm above the panel. Their inner faces show 2.5-3.5 mm wide at about
  410 mm from the camera nadir, which gives that height.
- In the photo they are therefore spread outward by about 3 mm per side: the apparent width is 847.5 mm.
- Corrected, the outer edges are 841.2 mm apart, which matches the 33 in listings.
- The printed panel itself is in the panel plane and needs no such correction.

## 3. Physical structure (rear to front)

| Part | Where (mm) | Notes |
|---|---|---|
| Wooden end-bells | x 0-12.9 and 828.3-841.2, y 0-312.5 | Solid wood, oiled red-brown, about ½ in thick. Wedge-shaped in side view: low front, high rear. Their top stands about 5 mm proud of the panel and shows a narrow inner face. Two black screws on each outer face. They reach about 6 mm behind the rear rail and about 5.5 mm in front of the front rail. |
| Rear rail | y 6.2-20.6 | Black-painted extrusion; its top face is seen from above. Its rear face is the top of the rear jack strip. |
| Frame strips | x 12.9-14.1 and 827.7-828.3 | Thin black edges between the end-bells and the print |
| Printed panel | x 14.07-827.72, y 20.55-292.55 | Flat, satin dark grey, screen printed. The whole panel assembly hinges back for service (4 screws on the front-rail face, 1 on each end-bell). |
| Front rail | y 292.6-307.0 | Black extrusion; its vertical front face carries the 4 screws |

The top has no power switch. POWER and MEMORY PROTECT are on the rear.

## 4. Colour palette

Render values are white-balanced suggestions. In the master photo the left is lit warm and the right by
daylight, so the photo values spread.

| Key | Render hex | Photo | Use |
|---|---|---|---|
| `panel_grey` | `#45464A` | `#464445` (left) - `#4A4A4B` - `#646368` (right) | Panel ground, neutral dark grey |
| `print_white` | `#E8E6DF` | core `#DCD2C2` (warm) - `#EEEFEA` | Legends, titles, page names, box labels |
| `print_grey` | `#A3A4A5` | core `#A8A9A9` | Secondary legends: MULTI in the title, MULTI PATCH, MULTI PATCH PAGE, alternate page names |
| `print_green` | `#B4BB8C` | core `#ACB18D`; lit unit `#C6D2A6` | Yellow-green modulation legends and leaders |
| `line_cyan` | `#C6D6DC` | core `#CAD8DF` | Diagram lines, boxes, knob symbols, leaders |
| `print_blue` | `#526291` | `#505E83` (thin rules read `#4D576F`) | Title bars and rules, periwinkle |
| `vfd_window` | `#2A6E55` | `#2E745A`-`#337A5D` | Green filter, unlit |
| `vfd_unlit` / `vfd_lit` | `#3A8669` / `#3CFFD0` | `#388366` / `#0AFDDA` (overexposed) | Unlit segments show faintly; lit segments cyan-green, draw with a glow |
| `led_off` / `led_on` | `#5A3434` / `#FF3A2E` | `#5C4442`-`#6F6166` / none lit in any photo | Red diffused LEDs behind round holes |
| `cap_black` / `cap_edge` | `#2E2E30` / `#141416` | `#3B3833`-`#3E3D3D` lit | Key caps, satin, with a concave dish; dark gap round each cap |
| `cap_red` | `#E8505A` | `#EC585F` | STORE cap |
| `cap_print` | `#F2F2EE` | | Keypad numerals |
| `hole_black` | `#0C0C0D` | | Holes and the shared key openings |
| `knob_black` / `pointer_white` | `#1C1C1E` / `#E8E8E4` | | Knobs; white triangle on the skirt |
| `rail_black` / `case_black` / `hinge_black` | `#1F1F21` / `#18181A` / `#151517` | `#3B3A3C`-`#413E3E` lit | Extrusions, rear faces, silhouette, hinge |
| `wood` / `wood_shade` | `#6E402D` / `#3F2418` | `#6D402D` warm, `#895F4F` daylight | End-bells |
| `power_red` | `#B0242C` | | Rear power rocker |

## 5. Typography

**Typeface.**
- All panel print is one condensed grotesque, **Helvetica Condensed**: spurred G, straight-legged R,
  flagged 1, narrow round O (VCO1 reads almost like "VC01").
- Medium weight for most legends; a lighter weight for the large titles.
- Every legend is in the JSON as its ink bbox, cap height, colour class and alignment, word for word.

| Font key | Used for | Cap height |
|---|---|---|
| `title` | MASTER, PROGRAMMER, MODULATION SOURCE/X SELECT, PAGE MODIFIER, SINGLE/MULTI PATCH PAGE SELECT (MULTI grey). Each title sits in a gap of its blue bar. | 7.0 mm |
| `legend` | VOLUME, TUNE PAGE, MASTER PAGE, SINGLE/PATCH, MULTI/PATCH (grey), PATCH EDIT, STORE, SWITCH MODE, PAGE 2 | 4.8 mm |
| `legend_m` | MODULATION SOURCE SELECT, X SELECT; the numerals 1-9 0 + − under the mod keys and 1-6 under the encoders | 4.8 mm; numerals 3.3-4.2 mm |
| `page` | Page names beside the page buttons (VCO1 TRANS, VCO2 PAN, FM/LAG CVMIDI, TRACK X ZONE, VCF/VCA VOLUME, ENV X VIB, LFO X, RAMP X, MISC. MISC.). The first word is white, the second grey. | 4.7 mm |
| `box` / `box_s` | Labels inside the diagram boxes / PWM | 4.2-4.3 / 3.3 mm |
| `legend_s` | VFD1 header (PATCH, PATCH NAME, VOICE 1-6, CURRENT PAGE), mod-source names, QUANTIZE, CLEAR | 2.5-2.7 mm |
| `legend_xs` | MODULATION PAGE (green), MULTI PATCH PAGE (grey), MODULATION/PAGE SELECT (green), ON/OFF, VALUE X | 2.4-2.5 mm |
| `diagram` | Diagram parameter labels and the two-line green modulation labels | 2.2-2.4 mm |
| `strip` / `strip_head` | Rear-jack legends printed along the rear of the top panel / their headings with rules (AUDIO OUT, CV AND GATE INPUTS, MIDI) | 3.0-3.2 / 4.8-5.2 mm |
| `keycap` | Numerals on the keypad caps: a bold, DIN-like geometric sans, white | 6.4 mm |

**Substitute.**
- **Nimbus Sans Narrow** (URW's Helvetica Narrow design, AGPL-3.0 like Lemus), Regular and Bold, in
  `fonts/` (`fonts/README.md` has the source and SHA1s). Liberation Sans Narrow was the first
  choice, but its only Narrow (1.07) is GPL v2 with a font exception, not OFL.
- Roboto Condensed (OFL) would be the alternative.
- Michroma, already in the repo, is a wide extended face and does not match. IBM Plex Mono is only for
  the plug-in chrome.
- Fit each legend to its measured bbox width.

The VFDs are not a font: draw 14-segment glyphs (section 9).

## 6. Buttons (59)

**Cap and hole.**
- All caps are the same part: a square cap 15.0 mm, corner radius about 1.2 mm, with a concave round dish
  of about 11.3 mm, satin black. STORE is the same cap in salmon red.
- Caps stand about 3 mm above the panel.
- Single caps sit in **16.2 mm square holes**.
- The keypad (12), the 14 mod-source keys and the 6 PATCH EDIT keys each sit in **one shared black
  opening**, with no panel between caps (`open_*`, 8.1 mm beyond the outer cap centres).
- Pitch is **0.65 in (16.51 mm)** between adjacent keys. The six soft-key columns are 31.42 mm apart
  (1.237 in, not an inch-grid value; 40/6 VFD characters is 30.7 mm, so not that either).
- Keypad caps carry white numerals and + / −; their size and offset are in `cap.print`.

**Positions.** `x,y` are the switch centres, which are also the hole centres. The switch matrix is
MAME's: input port `switches_N`, bit, active low. In the hardware it is the pot board's 8 x 8 matrix (74LS42
U16, 74HC244 U24); the five right-hand page keys are SW1-SW5 on the processor board.

| Button | Label | x, y | Port, bit |
|---|---|---|---|
| tune / master | TUNE PAGE / MASTER PAGE | 151.32, 80.16 / 213.44, 79.96 | switches_1 bit 6 / bit 7 |
| single / multi | SINGLE PATCH / MULTI PATCH | 64.46, 174.84 / 93.75, 174.82 | switches_2 bit 0 / bit 1 |
| voice_1 … voice_6 | PATCH EDIT 1-6 | x 118.45, 134.65, 150.96, 167.44, 184.06, 200.93; y 174.6-174.8 | switches_2 bits 2-7 |
| store | STORE (red) | 63.71, 213.38 | switches_1 bit 4 |
| keypad 7 8 9 | | x 150.95, 167.34, 184.05; y 213.5 | switches_0 bit 7; switches_1 bits 0, 1 |
| keypad 4 5 6 | | y 230.1 | switches_0 bits 4, 5, 6 |
| keypad 1 2 3 | | y 246.8 | switches_0 bits 1, 2, 3 |
| keypad − 0 + | | y 263.3 | switches_1 bit 3; switches_0 bit 0; switches_1 bit 2 |
| mod row, 14 keys | LEVER 1, LEVER 2, PEDAL 1, PEDAL 2, VIB, KEYBOARD, LAG, VELOCITY / RELEASE VELOCITY, PRESSURE, ENV X, LFO X, TRACK X, RAMP X | x 286.42 … 500.67 (pitch 16.48), y 77.43 | switches_5 bits 0-7 / switches_6 bits 0-5 (MAME names RELEASE VELOCITY, PRESSURE, ENV, LFO, TRACK, RAMP) |
| (the same keys, second function) | X SELECT: 1 2 3 4 5 6 7 8 9 0 + − QUANTIZE CLEAR, printed under each key | | |
| p1_1 … p1_6 | soft keys under the upper PAGE MODIFIER display ("P1 1-6") | x 323.60, 355.46, 386.92, 418.33, 449.36, 480.92; y 163.9 | switches_3 bits 0-5 |
| p2_1 … p2_6 | soft keys under the lower display ("P2 1-6") | x 323.92, 355.39, 386.88, 418.17, 449.50, 481.14; y 228.6 | switches_4 bits 0-5 |
| page2 | PAGE 2 | 285.77, 262.59 | switches_1 bit 5 |
| vco1 / vco2 | VCO1 / VCO2 | 538.85, 101.85 / 538.58, 159.08 | switches_3 bit 6 / bit 7 |
| fm_lag / track_x | FM/LAG / TRACK X | 538.49, 217.75 / 538.57, 275.49 | switches_4 bit 6 / bit 7 |
| vcf_vca, env_x, lfo_x, ramp_x | VCF/VCA, ENV X, LFO X, RAMP X | x 676.0; y 101.30, 158.61, 217.48, 275.40 | switches_7 bits 0-3 (processor SW5, SW4, SW3, SW1) |
| misc | MISC. | 742.96, 275.05 | switches_7 bit 4 (processor SW2) |

Unused matrix bits: switches_6 bits 6-7 and switches_7 bits 5-7 (pulled up, no switch).

The mod row was levelled to its mean y. The cap fits showed a 0.6 mm tilt along the row, but the artwork
shows none.

## 7. Encoders and VOLUME

**Knob.** All seven knobs are the same skirted black knob:
- flat skirt Ø 27 mm;
- fluted (knurled) body Ø 16 mm, about 24 flutes;
- smooth domed cap Ø 10.5 mm;
- about 15 mm tall;
- a white triangle on the skirt (base at r 7.8, tip at r 11.8, 3.7 mm wide) that turns with the knob.

On the encoders the triangle means nothing; sellers usually leave it at 12 o'clock.

| Control | Centre x, y | Notes |
|---|---|---|
| Encoders 0-5 (`encoder_knob_1..6`) | x 323.76, 355.41, 386.83, 418.28, 449.67, 481.29; y 262.05 | LA226 step encoders, 30 detents per turn, endless. On the soft-key columns. Numbers 1-6 printed below (cap 4.2 mm). |
| VOLUME (`volume_knob`) | 81.99, 80.11 | 50K dual-gang pot. No printed scale. Travel taken as ±150°; the photo shows −125°. |

Centres come from the pot-board artwork; the knob tops are too tall for the photo. Encoder 1's centre is
in the scan's fold, so it takes its soft-key column's x.

## 8. LEDs (16)

**LED type.**
- Red diffused LEDs (the artwork outline is 4.9 mm, T-1¾) behind round holes about 3.8 mm across.
- They sit about flush and look dark maroon when off.
- None is lit in any photo, so `led_on` is a standard red.

**Latches.**
- `LED1` = pot board U14 (74LS374); `LED2` = processor board U24 (74LS374).
- All active low; MAME output names.

| LED | Panel meaning | x, y | Latch, bit |
|---|---|---|---|
| led_mod_sorc | MODULATION SOURCE SELECT | 273.76, 59.04 | LED1 bit 1 |
| led_x_sel | X SELECT | 274.48, 94.95 | LED1 bit 0 |
| led_mod | SWITCH MODE: MODULATION / PAGE SELECT | 274.83, 159.29 | LED1 bit 2 |
| led_on_off_a | SWITCH MODE: ON/OFF (upper) | 275.07, 169.41 | LED1 bit 3 |
| led_value | SWITCH MODE: VALUE X | 274.85, 224.41 | LED1 bit 7 |
| led_on_off_b | SWITCH MODE: ON/OFF (lower) | 274.50, 234.59 | LED1 bit 6 |
| led_page_2 | PAGE 2 | 274.63, 247.51 | LED1 bit 5 |
| led_vco1 / led_vco2 / led_fm_lag | page LEDs (pot board) | 554.72, 103.82 / 554.83, 160.97 / 554.56, 220.07 | LED2 bits 5 / 6 / 7 |
| led_track_x | page LED | 555.35, 277.76 | LED1 bit 4 |
| led_vcf_vca / led_env_x / led_lfo_x / led_ramp_x | page LEDs (processor board) | x 692.1; y 103.63, 161.07, 219.69, 277.73 | LED2 bits 4 / 3 / 2 / 1 |
| led_misc | page LED | 759.59, 277.26 | LED2 bit 0 |

Each page LED sits between its button and the page name.

## 9. Vacuum fluorescent displays (3)

Each display is an Itron FG405A2: 40 characters of 14-segment + decimal point + underline. The main CPU
writes raw segment patterns; there is no character ROM. MAME outputs are `vfd_<n>_char_<i>` and
`vfd_<n>_line_<i>`. vfd 1, 2 and 3 are the service manual's DISPLAY 1, 2 and 3.

| Display | Green window (bbox) |
|---|---|
| vfd_1, PROGRAMMER | 56.15, 125.05 - 250.25, 145.05 |
| vfd_2, PAGE MODIFIER upper | 304.15, 124.75 - 496.85, 144.95 |
| vfd_3, PAGE MODIFIER lower | 305.05, 190.05 - 496.95, 209.95 |

**Window.** About 193 x 20 mm, a green filter in a rectangular cut-out, flush with the panel.

**Character grid.**
- Pitch **4.60 mm**, measured from the unlit cells on two displays (4.59-4.60).
- The 40-character field is centred in the window: first character centre about 7.3 mm in from the
  window's left edge.

**Glyphs.** Measured on a lit unit and corrected for its oblique view:
- characters about 3.0 mm wide and 6.5 mm tall, slanted about 10°, stroke about 0.45 mm;
- the decimal point sits right of the baseline; the underline segment about 1 mm below the glyph;
- the top of the glyphs is about 6 mm below the window top (±0.5-1 mm).

The segments sit a few mm under the panel, so photos show them about 1 % smaller than the print.

Draw the unlit segments faintly in `vfd_unlit`; the unit shows the grid when idle.

## 10. Printed graphics

**Section titles and bars.**
- Each section title sits in a gap of a periwinkle bar about 7.0 mm tall: a short block left of the
  title, then a long bar to the section's right edge.
- Bars at y 45.0-52.2 (top row) and 103.9-110.8 (PROGRAMMER, PAGE MODIFIER).
- The sections end on 2 mm blue bottom rules (y about 282-284).
- In the page-select section a 2.2 mm rule runs under each row of page names. It starts under the page
  button and runs to the end of that page's diagram.

**Rear-jack strip.**
- Along the rear of the panel (y 23-35) the names of the rear jacks are printed directly over the jacks
  below, mirrored as seen from the front.
- Left to right: LEFT, MONO, RIGHT, VOICE 1-6 / AUDIO OUT; GATE 1, CV1 … GATE 6, CV6 / CV AND GATE
  INPUTS; TRIGGER IN, 1 2 / PEDAL INPUT, ADVANCE CHAIN, TO FROM / CASSETTE, IN OUT THRU / MIDI, ON OFF /
  MEMORY PROTECT.
- The three headings break thin white rules.

**Leaders.** All are 0.6 mm light-cyan lines unless noted:
- SINGLE/MULTI bracket from the VFD1 window down to SINGLE and MULTI.
- Six PATCH EDIT leaders: straight down from 1.5 mm under the window, a 45° jog of 1.4, 3.7 or 6.4 mm
  outward from the middle, then straight down to the caps.
- MODULATION PAGE leader (green) and MULTI PATCH PAGE leader (grey) right of VFD1.
- Six soft-key column lines, four pieces each: between the displays, keys and encoders, broken where
  the keys and displays are.
- White L-brackets from the MOD SOURCE and X SELECT LEDs to the first key's legends.

**Page diagrams.** Eight block diagrams, one per page; MISC. has none. Drawn in light cyan:
- **20 boxes**, standard height 11.4 mm, stroke 0.6 mm, square corners. The VCO2 MIX box is taller,
  14.7 mm. PWM boxes are 5.1 mm.
- Connection lines are axis-aligned segments, including the long cross-page wires. The JSON has 163
  `line` elements in all, counting the leaders and strip rules. The cross-page wires are:
  - VCO outputs → VCF;
  - VCO2 triangle → FM VCA;
  - the FM destination switch → VCO1 and VCF frequency.
- The **FM destination switch** is a small box with a pivot and two throw contacts (`sym_fm_destination`).
- **31 knob symbols**:
  - circle r 3.11 mm to the stroke centre, stroke 0.45 mm;
  - pointer from r 1.65 to the ring at 217° (lower left);
  - a numeral inside (cap about 2.1 mm) that is **the soft encoder (1-6) editing that parameter on that
    page**.
- Each modulatable parameter has a **yellow-green L-leader** from its knob stem to a two-line green label
  ("FREQUENCY / MODULATION" …).
- Parameter labels sit centred under the knobs.

| Page | Boxes (left to right) | Knob numbers |
|---|---|---|
| VCO1 | VCO1 (TRIANGLE, SAWTOOTH, PULSE outputs), PWM, MIX, VCA | 1 FREQUENCY, 2 DETUNE, 4 PULSE WIDTH, 6 VOLUME |
| VCO2 | VCO2, PWM, MIX (+NOISE), VCA; SYNC | 1, 2, 4, 6 |
| FM/LAG | FM VCA, destination switch, LAG | 1 FM AMPLITUDE, 5 RATE |
| TRACK X | TRACKING GEN. (IN, OUT) | 2-6 POINT 1-5 |
| VCF/VCA | MULTIMODE VCF, VCA1, VCA2 (VOICE OUT) | 1 FREQUENCY, 2 RESONANCE, 3 MODE (15), 5 VOLUME, 6 VOLUME |
| ENV X | ENVELOPE GENERATOR (TRIGGER IN), VCA (OUT) | 1 DELAY, 2 ATTACK, 3 DECAY, 4 SUSTAIN, 5 RELEASE, 6 AMPLITUDE |
| LFO X | LFO, VCA (OUT) | 1 SPEED, 2 WAVESHAPE, 4 RETRIG, 6 AMPLITUDE |
| RAMP X | RAMP (TRIGGER, OUT) | 2 RATE |

**Line widths.** Stroke-centre polylines in the JSON:
- 0.6 mm for lines, leaders and box edges (FWHM 0.6-0.7 mm, including camera blur);
- 0.45 mm for the knob-symbol rings.

## 11. Artwork places (brand marks are not drawn)

| Place | Element | Box (mm) | What |
|---|---|---|---|
| `maker` | `logo_maker` | 54.65, 242.05 - 113.85, 252.55 | Cream "Oberheim" fat-face wordmark with an (R) dot, lower left of the PROGRAMMER section |
| `name` | `logo_name` | 54.55, 254.55 - 133.35, 274.85 | Outlined "Xpander" wordmark under it (thin cream outline) |

The rear repeats the marks; they are places too (section 12). Nothing else on the top panel is a brand
mark.

## 12. Rear panel

**Faces.** The rear has two faces joined by a black piano hinge:
- The **upper jack strip**, about 47.5 mm tall. It belongs to the hinged top assembly; the pot board's
  jacks come through it.
- The **lower rear panel**, about 62.5 mm tall, part of the base.
- They are separated by the 6 mm hinge.

`rear_panel` gives every item's top-view `x` and its `x_seen_from_rear` (841.2 − x). z is measured down
from the top of each face. As seen from behind, the power end is at the left.

**Upper strip, seen from behind left to right:**
- 11 cooling slots. Some units have an FCC sticker there instead.
- MEMORY PROTECT: a 2-position slide switch, OFF left / ON right seen from behind. It is processor-board
  SW6, MAME port `memory_protect`.
- MIDI THRU, OUT, IN: 5-pin DIN.
- CASSETTE FROM, TO: 3.5 mm jacks.
- The CAUTION / ATTENTION text block.
- ADVANCE CHAIN, PEDAL INPUT 2, 1, TRIGGER IN.
- CV AND GATE INPUTS: CV6, GATE 6 … CV1, GATE 1, at 16.4 mm pitch.
- AUDIO OUT: VOICE 6 … VOICE 1, at 20.4 mm pitch.
- RIGHT, MONO, LEFT.

All jacks are ¼ in, except cassette. Jack centres are at z 21.3 and the DIN centres at z 22.0.

**Lower panel, seen from behind left to right:**
- The red POWER rocker in a black bezel.
- The IEC mains inlet on a two-screw flange.
- Above it, two silver-white foil stickers with black print: XPANDER, MODEL XP-1, 95-130 VAC OR
  190-260 VAC, 50-60 Hz 39 WATTS MAX.; then OBERHEIM ELECTRONICS, INC., LOS ANGELES, CALIF. 90064, Nº
  and the serial. Each sticker's box is measured on the straight-on photo (48.6 x 14.2 and
  48.6 x 15.7 mm, 3.7 mm apart, the lower one 2.9 mm further right), and the print within each on
  Reverb 102838549 image 7 (5712 px, at about 17 px/mm). That photo is taken from above, so its heights
  are foreshortened (its top sticker is 3.88 times as wide as tall, the straight-on one 3.37). Its
  print is therefore used as fractions of each sticker, which carry over: each line's share of its
  sticker's width agrees within 1-3 % between the two units. The lines naming the product and the
  maker aren't drawn, like the brand marks (place `serial`, `drawn: false` in panel.json).
- A small 115/230 V slide selector.
- The printed emblem, Oberheim and Xpander wordmarks and the address line. These are places `emblem`,
  `maker`, `name` and `maker_address`.

**Print variants.**
- On US units (102838549 and the Perfect Circuit unit) the legends are **above** the jacks.
- A Japanese (Nihon Hammond) unit, 98053552, prints the CV/GATE, AUDIO OUT and control-input legends
  **below** the jacks. It also carries a NIHON HAMMOND plate instead of the Oberheim label.
- The JSON follows the US print.

**Cross-check.** The jack x positions fall under the printed strip legends on the top panel within
1-2 mm. MIDI, CASSETTE and MEMORY PROTECT fall within 0.5 mm.

## 13. Accuracy

| What | Accuracy |
|---|---|
| Overall scale | ±0.3 % (0.65 in key pitch measures 16.48-16.55 mm) |
| Button centres | ±0.3 mm relative, ±0.5 mm absolute (parallax model checked on hole edges to 0.2 mm) |
| Encoder and VOLUME centres | ±0.5 mm (artwork); encoder 1 ±0.8 mm |
| Legends, lines, boxes, knob symbols, bars | ±0.3 mm relative |
| LEDs | ±0.4 mm |
| VFD windows | ±0.3 mm |
| VFD character grid | Pitch ±0.03 mm; vertical placement and glyph size ±0.5-1 mm |
| End-bells, rails, outline | ±1.5 mm (parallax corrected with an estimated 5 mm height) |
| 3-D sizes (cap height 3 mm, knob height 15 mm, skirt/body/cap diameters) | Estimates, ±0.5-4 mm |
| Rear | Jack x ±1 mm; other items ±2 mm; heights ±1.5 mm |
| Colours | Lighting-dependent starting points |

## 14. Open points

1. **Overall size.** The dimensions are from the photo plus dealer listings (33 x 12 x 6 in). A tape
   measure on a real unit would settle width and depth.
2. **Lens or sag.** The 0.7 % edge correction fits either lens pincushion or a slightly bowed panel. The
   drawn result is the same, but whether the real panel is flat is not known.
3. **VFD glyphs.** Glyph height, slant and vertical position come from an oblique photo of another unit.
   The FG405A2 datasheet was not found.
4. **Cap and knob heights** are estimates (cap about 3 mm from parallax; knobs about 15 mm).
5. **LED lit colour.** No photo shows a lit LED.
6. **VOLUME travel** is assumed ±150°; there is no printed scale.
7. **Rear upper strip.** Vent slots or FCC sticker varies by unit. Vent size and positions are ±3 mm, from
   an oblique photo.
8. **Lettering.** The family is clear (Helvetica Condensed); the exact weight of the titles is not.

## 15. Against MAME

**What MAME names.**
- MAME's layout names every button, LED, encoder, the VOLUME knob and the three VFDs.
- The panel matches it, and MAME's port names are kept in the JSON.

**What the photos add, which MAME doesn't name or draw:**
- The white pointer triangles on the encoder and VOLUME skirts.
- The Oberheim/Xpander wordmarks.
- The keypad caps' printed numerals as cap print.
- The DESTINATION switch as a real switch symbol.
- The exact geometry of every diagram.

**On the rear.**
- POWER, the mains inlet, the voltage selector, vents, labels and the hinge are not in MAME.
- MEMORY PROTECT is a rear slide switch: MAME prints its legend in the top strip, which is correct
  (the panel names the rear switch there).

**Naming.**
- The four mod sources MAME calls ENV, LFO, TRACK and RAMP are printed **ENV X, LFO X, TRACK X,
  RAMP X** on the panel.
