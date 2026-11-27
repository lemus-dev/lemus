# Front panel measurements

`panel.json` is the Bass Station Rack's front panel in mm, measured on 2026-11-24 from one photo. Every
position and size in it comes from pixels through the scale and lens model below. Nothing was placed by
eye. Values that are guesses rather than measurements say so in a `note`.

## Source

`private/bsrack/docs/web/polynominal-rack-front.jpg`: polynominal.com's straight-on cut-out of the
whole front on a white background, 4061 × 1244 px, SHA1 `e539fb19dd789cf12a2dcefa067822f279b3d560`.
The site's orange watermark covers the lower right corner, over the "Rack" script, part of the
ANALOGUE SYNTHESIZER MODULE strip and the lower right rack slot.

## Scale

The plate spans x 178..3905 px and y 441..780 px. That plate is taken as 482.6 × 44.0 mm, giving 7.72 px/mm
on average (3727 px / 482.6 mm). The origin is the plate's top left corner and the units are mm.

That average does not hold everywhere in the photo. The lens has barrel distortion, and the image is
also very slightly keystoned. Measured inside the frame, the local scale runs from 7.04 px/mm across and
7.61 px/mm down at the left end, to 8.04/8.11 px/mm near x = 2400 px. The rest of this section shows
how we know that:

- The printed frame lines bow. Their top runs from y 466.6 px at the left to 456.6 px near x = 2500
  and back to 458.1 px at the right. Their bottom runs 754.9 → 763.0 → 761.0 px. This is one smooth
  curve through all eight frames, so it is the lens, not the print.
- The knob tick circles shrink towards the ends: their outer radius is 64.5 px mid-panel and 61.9 px
  at the LFO SPEED knob. They also shrink more across than down.
- The cut-out is a drawn rectangle. Its top and bottom edges are straight (441 and 780 px) while the
  real plate edge curves. At the left end the gap between them is filled flat dark (rows 441-448 and
  775-778). In the middle the mask clips about 1 mm off the plate.

The correction is one model fitted with `scipy.optimize.least_squares`. It is a radial undistortion
(centre 2043, 617 px; q1 = 0.0144, q2 = −0.0012 per (1000 px)²) followed by a homography to mm. The model
was fitted so that:

- the 555 top-line and 547 bottom-line samples lie on two horizontal lines;
- 16 vertical frame lines (about 54 samples each) are vertical;
- the outer tick ends of 15 knobs (about 160 points) lie on circles of one common radius;
- the plate's left and right mask edges at mid-height (178.5 and 3904.5 px at y 610) are 0 and 482.6 mm.

The residuals are 0.02 mm RMS on the lines and 0.03 mm on the ticks. The knob positions were left free
in the fit, so they check it independently:

- all 18 knobs come out on y = 14.0 ± 0.1 mm;
- within each section the knobs come out on a 20.00 mm pitch (149.96, 170.01, 190.03, 210.04, 230.07,
  250.45, 270.45, 290.45, …, 450.91);
- the frame gaps fall near round numbers (139.8, 179.85, 199.87, 240.15, 300.3, 360.5);
- the four rack slots come out 31.6-31.7 mm apart vertically (EIA spacing is 31.75 mm).

If the plain 7.72 px/mm were used instead, x would be off by up to 8.0 mm (at x ≈ 160 mm) and the
frame tops would step by 1.3 mm between sections.

**Vertical anchor.** The distortion model fixes vertical scale but not where y = 0 is. At the left end
the plate's real top and bottom edges can be seen against the mask's dark fill, 42.87 mm apart. Their
mid-line and the mid-line of the four slot centres agree at 21.40 mm in model units. The 44.0 mm plate
is centred on that line. That puts y = 0 at the plate top, the frames at 3.00..40.77 mm and the slots at
6.0-6.3 and 37.7-37.9 mm. The flat face seen in the photo is therefore 42.9 mm tall, with 0.55 mm at
each edge lost to the dark edge band.

Converter: `mm(px)`, built from the fitted parameters. The scripts are scratch files and are not kept
in the repo. The full parameters are `[cx, cy, q1, q2] = [2043, 617, 0.0144, −0.0012]` and
`h = [128.358, −1.387, 242.407, 0.333, 123.678, 21.303, 0.0135, −0.0040]` on `u = (x' − 2000)/1000`,
`v = (y' − 610)/1000`, plus 0.60 mm added to y.

**Uncertainty.** Printed parts are good to about ±0.2 mm, and to about ±0.1 mm between neighbours. The
rack slots are ±0.4 mm, because the site masked them by hand. The display bezel is ±0.3 mm, because its
edges are faint.

## Conventions (as `machines/qvgt/docs/panel.json`)

- Boxes are `[x, y, w, h]` and points are `[x, y]`.
- Text entries are `{"text", "x", "y", "cap", "align"}`, plus a `colour` key (a name from `colours`).
  `x` is the left edge, centre or right edge according to `align`. `y` is the middle of the capitals,
  which is the `midY` that `drawLegend` takes. `cap` is the capital height.
- Knob `d` is the skirt diameter, `cap_d` the blue cap's, and `label_y` the middle of the strip label.
- Angles are clockwise from 12 o'clock, as in the QuadraVerb `drawKnob`.
- `texts` holds every printed word, grouped by frame. The same entries are also attached to their
  knobs, switches, keys and display.

## How each part was measured

**Frames and strips.** Line centres are sub-pixel peak centroids, converted through the model. Lines
are 0.3 mm wide (FWHM). The strips are 22.38..24.99 mm on y everywhere. Their x ends are half-level
crossings.

**Knobs.** The centre and tick geometry come from the white tick marks. Each knob has 11 ticks, outer
radius 7.98 mm, inner radius 6.45 mm (median), 0.3 mm wide. They are 28.2° apart and run from −141° to
+141°.

VOLUME and PORTAMENTO show only their right-hand ticks, because their caps hide the rest. Their centres
were fitted with the radius held at 7.98.

MIX is drawn differently. It has a ring (r 6.25-7.0) round the skirt from −143° to +141°, yellow from
−143° to −58° (the EXT. end) and grey after, with ticks standing out from the ring.

Cap and skirt are circle fits in mm, using the blue half-level for the cap and the dark-to-face
half-level for the skirt:

- The cap is 8.9 mm. That is the cap top as seen, about 2 % large because it is nearer the camera.
- The skirt is 12.6 mm, taken from the knobs at 230-310 mm. Knobs further out read 12.8-13.3 mm,
  because their sides show.
- The caps sit up to 4 mm off their shafts at the panel ends (parallax). That is why centres come from
  the printed ticks and not the caps.
- The pointer is about 1.0 mm wide and runs from the centre to the rim (r 0 to 4.4). It was measured on
  the ENVELOPE (mod) knob.

**Switches.** The slot's x edges are the strongest plate-to-dark steps. Its top is a half-level crossing
at 28.25-28.38 mm. Its bottom is a soft shadow at 32.9-33.5 mm, so the common 32.95 is taken from the
five cleanest. Lever edges are crossings in the lever's top highlight row. The lever is 5.5 × 3.5 mm
(mean of ten). `lever_x` holds the lever centre for each position, with the end positions 0.8 mm in from
the slot ends; 0.8 mm is the gap measured at levers sitting at an end.

Where each lever sits in the photo (`photo_at`):

| Switch | Lever at |
|---|---|
| WRITE | PROTECT |
| SHAPE | SAW |
| OSC 1 WAVEFORM | pulse |
| RANGE | 8' |
| OSC 2 WAVEFORM | pulse |
| SYNC | ON |
| OSCILLATOR | 2 |
| PWM SOURCE | LFO |
| CUT-OFF | 24dB |
| MOD SOURCE | LFO |
| ENVELOPE | 1&2 |
| TRIGGERING | AUTOGLIDE |

Every measured lever centre is within 0.5 mm of its computed position.

**Keys.** The outline is taken at 25 % of the way from the face colour to the key. It includes the
darker right and lower side faces that the camera sees; the lit face alone is about 9.45 × 4.45 mm. The
numerals sit about 0.9 mm left of the outline centre, because the key tops are raised and seen from the
right.

**Display.**

- The bezel is a faint dark rounded rectangle; its edges were taken from median profiles.
- The red filter is the R−G half-level.
- The five LEDs are R−B half-chroma blobs.
- The EDIT and WRITE corner marks are pixel runs. They mark the two decimal points, 10.0 mm apart.
- The 7-segment digits cannot be seen: they are unlit behind the filter, and contrast stretching shows
  nothing. The two digit boxes are therefore **guessed** from a 0.4 in digit and the decimal-point
  marks; `digits_note` says so.

**LEDs, PHONES jack, logos.**

- The panel LEDs and the jack are circle and blob fits in mm. The jack nut is 8.44 mm across and its
  hole 6.34 mm.
- The Novation logo box is the white-letter extent, including the small square at its left.
- "BassStation" is measured whole on a blue mask.
- "Rack" uses a yellow mask that excludes the orange watermark (G/R > 0.72). Its box is the extent of
  the visible strokes; the strokes under the watermark are assumed to lie inside it (see `note`).
- The ANALOGUE SYNTHESIZER MODULE strip's box is measured. Its text is measured from the left edge;
  its right end meets the watermark, so the text width is ±0.3 mm.

**Text.** Words are connected components of a colour mask, merged along a line. Each class has its own
mask:

| Text | Mask |
|---|---|
| white legends | g > 95, chroma < 45 |
| strip labels | dark on the strip |
| titles | blue, B − R > 35 |
| second-function labels | yellow, R − B > 45 |
| SAVE TYPE and SAVE | dim grey |
| numerals | g > 212 on the keys |

Bounding boxes go through the model at their pixel edges.

## Artwork boxes

| Place | Mark | Box (mm) |
|---|---|---|
| `maker` | `novation` | [104.60, 6.72, 29.03, 2.80] |
| `name` | `bass_station_rack` | [421.50, 29.46, 42.82, 5.78]: "Rack" partly under the watermark |

## Colours

Each colour is the median of real pixels in the area given (px, x0..x1 × y0..y1). Where a mask was
used, the percentile band is given. The photo is warm and lit from the upper right: the face is #403b35
in the middle of the top margin and #2d2c26 at the far left.

| Name | Hex | Pixels sampled |
|---|---|---|
| face | #3d3a33 | 300..3700 × 445..453 (strip above the frames), 10-90 % luminance |
| frame | #d0cdc6 | 2000..2400 × 456..459, line pixels g > 90, upper half |
| strip | #a9a79a | 2100..2600 × 615..634, g > 120, 20-80 % |
| strip_text | #27281f | 2100..2600 × 619..632, g < 100, darkest 30 % |
| blue | #6693ba | 2066..2387 × 637..660 (OSC 1-2 MOD/SYNC.), blue mask, top 25 % |
| legend | #e6e4de | 2166..2283 × 730..745 (OSCILLATOR), g > 110, top 15 % |
| yellow | #c9b26c | 620..900 × 520..700 (keypad labels), yellow mask, top 10 % |
| grey_label | #948270 | 632..697 × 719..729 (SAVE TYPE), g > 80, top 15 % |
| label_box / label_box_text | #c6c0b3 / #3e3e32 | 630..700 × 733..746 (COMPARE box) |
| cap | #8fb6d1 | 2040..2058 × 520..545 (ENVELOPE mod knob cap) |
| skirt | #1b1c1b | 2015..2030 × 520..580, g < 60, darkest 30 % |
| tick | #eeedec | 2050..2070 × 480..495, g > 110, top 40 % |
| pointer | #f8f8f8 | 2061..2068 × 522..548, g > 230 |
| key / key_text | #a39e92 / #fdfdfd | 640..660 × 500..512 (key 1 face) / 647..655 × 496..510 ("1") |
| switch_slot / switch_lever | #090a08 / #24241f | 1550..1565 × 664..690 darkest 40 % / 2540..2575 × 676..692 |
| display / display_filter | #272620 / #412e20 | 915..930 × 600..615 / 950..1070 × 530..600 |
| led_display_off | #6a5345 | 1159..1169 × 629..638 (SAVE LED) |
| led_red_off | #5a2a18 | 1232..1243 × 674..684 (LFO SPEED LED) |
| novation_logo | #d7d2cb | 960..1140 × 493..510, g > 150 |
| logo_blue / logo_yellow | #9bb3d0 / #dec97d | 3428..3656 × 678..710 blue mask / 3665..3762 × 671..717 yellow mask |
| synth_strip / synth_strip_text | #c8c2b6 / #1f1f15 | 3432..3530 × 720..724 / 3431..3531 × 725..736, g < 100 |
| phones_nut / phones_hole | #cdc3af / #010101 | 370..430 × 650..715, g > 120 / 390..410 × 672..692 |

No LED is lit in the photo, so no "on" colours were measured. The display LEDs read pale pink because
of a reflection on the window.

## Not measured

- **7-segment digit boxes.** The digits are invisible in the photo; the boxes are guessed (see above).
- **LED "on" colours.** Nothing is lit in the photo.
- **"Rack" and part of the MODULE strip text.** These are under the watermark: the extents are of the
  visible parts. SYNTHESIZER is spelled with a Z, as on the same strip on the rear photo; the
  front's visible Z agrees.
- **The lower right rack slot.** It is under the watermark: its centre is measured, and its size is
  taken from the other three.
- **Knob travel.** One photo does not show it; the ticks span ±141°.

## How the view draws it

`plugin/HardwareView.cpp` draws everything above at 4 px/mm. Where the drawing needs more than this
file has:

- **Font.** The legends are a Helvetica-style bold. They are drawn in Liberation Sans Bold
  (`fonts/`, SIL OFL 1.1) at each entry's measured cap height. Measured against that face, the
  printed words are narrower: 0.86 ± 0.03 of its width over 49 words. The blue titles are a
  wider cut, at 1.07. The view uses those two factors. The ANALOGUE SYNTHESIZER MODULE line is
  fitted to its measured width.
- **Lit colours.** Nothing is lit in the photo. A lit segment or LED is drawn as a plain red LED,
  `#ff3a22`; that colour is not measured.
- **Digits.** The guessed digit boxes are drawn as upright 7-segment digits, with each decimal point
  where its EDIT or WRITE corner mark points.
- **Knobs.** The pointer turns over the printed scale: −141° to +141° (MIX −143° to +141°).

## The rear

Measured 2026-11-25 from polynominal.com's straight-on image of the whole rear
(`private/bsrack/docs/web/polynominal-rack-rear.jpg`, 1032×104). panel.json's `rear` has it, in the
rear plate's millimetres; the view centres the plate in the front's 482.6 × 44.0 mm.

- **Scale:** the jacks sit on a 20.0 mm pitch (CV IN to GATE OUT, the three MIDI sockets alike), 46.7
  px apart: 2.339 px/mm. The plate's 1029 px are then 440.0 mm, the standard 1U body between the
  ears, which supports the pitch. 43.2 mm of its height shows.
- **Print:** the four frames (CV / GATE with its divider, FILTER, OUTPUT, MIDI) and the power
  socket's, from the image's dark lines; every legend's cap centre and width from its ink. The FCC
  lines run together at this resolution: their pitch and the block's width are measured, the
  lines' own widths read off it.
- **Sockets:** the jacks' black rings 9.4 mm with 5.2 mm holes; the DIN shells 14.0 mm with 7.2 mm
  insulators (radial profiles); POWER IN 5.6 mm.
- **Logos:** the black "novation" with its square, "BassStation Rack" and the ANALOGUE SYNTHESIZER
  MODULE box, the first two as the artwork places `rear_maker` and `rear_name`.
