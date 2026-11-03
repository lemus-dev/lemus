# TR-909 top panel: how `panel.json` was measured

`panel.json` is a to-scale description of the TR-909 top panel, which the editor draws as it is
(`plugin/HardwareView.cpp` reads it at run time): 354 elements (case and cheeks, print, legends,
keylines, knobs and their scales, keys, LEDs, the display, brand-mark slots), a colour table, font
notes and the rear panel. Coordinates are mm from the rear-left corner of the top view, x to the
right, y towards the player. It follows the TB-303's and TR-808's files; "Conventions" below.

Brand marks (the Roland logo, TR-909, RHYTHM COMPOSER) are slots only (`type: logo`, bbox and the
mark's id). Nothing in this file traces them.

## Sources

All files are under `private/docs/` (reference only unless noted; `photos/SOURCES.md`).

| Role | File | Unit, licence |
|---|---|---|
| Geometry master | `photos/soundgas/sn282297_Roland-TR-909-Rhythm-Composer-SN282297-Ed-Obrien-2.jpg` (2400 x 1473, straight-on studio cut-out, about 4.5 px/mm) | #282297, (c) Soundgas |
| Cross-check 1 | `photos/commons_tr909_front_909bd.jpg` (2272 x 1333, straight-on, rear edge cropped) | serial not known, CC BY-SA 3.0, User:909 bd |
| Cross-check 2 | `photos/soundgas/dm_Roland-TR-909-Rhythm-Composer-Drum-Machine-3.jpg` (2048 x 1231, straight-on, watermark over the keys) | #403957, (c) Soundgas |
| Cross-check 3 | `photos/commons_tr909_large_bdaniel.jpg` (2284 x 1349), perspective-corrected by its author from an oblique shot; the only photo with the LEDs and display on | serial not known, CC BY-SA 2.0, Brandon Daniel |
| Close-ups, colours | `photos/soundgas/sn393354_...-Temp-2.jpg` (daylight, right half), `sn282297_...-3` ... `-6`, `-9`, `-10` (three-quarter views: knob and key shapes) | (c) Soundgas |
| Scale and grid | service notes p. 9 (switch board foil view, 1:1 at 400 dpi: key pitch, small-button pitch), p. 10 (`schematic/p10-voicing-board-pcb-address-map.png`, voicing board foil view: the 27 small pots) | Roland |
| Rear panel | `photos/soundgas/sn282297_...-7.jpg` (straight-on rear), `photos/commons_tr909_rear_909bd.jpg`, SN p. 12 | |
| Display, scales | owner's manual (`tr909-owners-manual.pdf`) p. 24 (the scale rows), SN p. 1 (parts) | Roland |

## Method

`tools/panel/build_panel.sh` runs the pipeline with the repo's `.venv` (numpy, scipy, OpenCV,
Pillow); the pictures it makes show the photos, so they stay in `build/tr909-panel/panel/`.

1. **`rectify.py`** finds the four outer edges of the top view (the outer edges of the two side
   cheeks, the rear and front edges) by sub-pixel gradient fits along every row and column (sd
   0.1-0.7 px per edge), intersects them and maps the quadrilateral onto a mm grid at 8 px/mm, the
   width the official 486 mm. The camera was square-on (keystone 0.19 %, a tilt of about 0.6 deg),
   so the depth takes the same scale: 279.49 mm.
2. **`regularise.py`**, the rubber sheet: the 14 vertical dividers on key-pair boundaries are put on
   the design grid x = K1 + (j - 0.5) 23.0 mm, 22 horizontal edges and legend rows are
   straightened, the correction interpolated between the lines and tapered to 0 at the case edges
   (at most 0.71 mm in x, 0.67 in y; the lines straight to +-0.02 mm afterwards). "Frame" says why.
3. **`measure_panel.py`** measures every element on the regularised master, from approximate
   positions read off gridded zooms, refined automatically: ink boxes at the 50 % level for text,
   gradient edges for bars, boxes and cut-outs, blob centroids for LEDs, buttons and the display,
   the printed tick rings for the knobs (x from the 0 deg tick, the row's y from the 9 and 3 o'clock
   ticks); then snapped to the design rows and grids (one y per legend row, the 23.0 mm key grid,
   the 12.0 mm small-button pitch).
4. **`crosscheck.py`** registers the other photos onto the master and measures every element on
   each of them the same way ("Cross-check"), checks the knob centres against the pots of SN p. 10,
   and measures what the first pass left out (the cheeks' edges, the plate's bends, the lit buttons'
   windows, the display's digits).
5. **`lemus_panel.py`** writes `docs/panel.json` in the Lemus conventions: the brand marks as slots,
   the parameter and key hints, the keys and LEDs as the editor wants them, the cross-check's
   additions.
6. **`render_preview.py`** draws the file and overlays it on each photo (`overlay_*.jpg`) for a look
   element by element; `panel_tables.py` prints the appendix tables.

The brand marks are traced, for the artwork pack only, by a script in the private folder
(`private/tools/panel/trace_logos.py`, from the owner's manual cover and SN p. 1); neither it nor
the marks are in the repo.

## Frame and scale

- **Width**: the official 486 mm over the cheeks' outer faces, depth 279.49 mm at the same scale
  (step 1 above). The step-key pitch is 23.0 mm three ways: the photo at that width (23.02 +-
  0.02), the switch-board artwork of SN p. 9 at 1:1 (360.6 px = 22.98 +- 0.06 mm, calibrated on
  the DIP pin pitch) and the number-bar legends (+-0.1 mm); the instrument sections are 2 x 23.
- **The rubber sheet stays.** On the master as rectified the long bars bow by up to 0.9 mm
  (sagitta) and the 46 mm sections measure 45.35-46.48 mm. The TR-808's panel turned out the other
  way round (its three photos agreed after a homography, so the irregularity was in the print), so
  the sheet was tested again (2026-10-17, `crosscheck.py`):
  - The other photos, by homography alone, side with the master. Fitting mappings to the master
    position plus k times the sheet's shift, the smallest residual is at k = 0.25 (909bd) and 0.5
    (dm3). Measured on their own, their dividers miss a 23.0 mm grid in the master's pattern at about
    60 % of its amplitude, their sections are 45.7-46.4 mm, their scale bars' sagitta -0.5 mm
    against the master's -0.9.
  - That pattern is a lens's: lines above the middle of the picture bend up at their ends, lines
    below it down, sections near the middle come out narrow and the outer ones wide (pincushion about
    the photo's centre). A two-term radial model about the master's centre takes out about half of
    the sheet on its own (0.30 to 0.18 mm rms after a homography). All three are straight-on shots
    centred on the panel, so they share the pattern, and a homography between them cannot see it.
  - The board says so. On the voicing-board foil view (SN p. 10, 400 dpi) 25 of the 27 small pots
    are found by their two mounting slots (two are crossed by traces), each pot's centre the
    mid-point of its slots. A mirrored similarity from the printed tick-ring centres to the pots
    leaves **0.087 mm rms in x** on the regularised master (max 0.43; 15.725 px/mm, 1:1 at 400 dpi
    to 0.2 %), against **0.375 mm** on the master as rectified and 0.23-0.24 mm on the two other
    photos. In y it leaves 0.17 mm: the page is 1.2 % taller than wide (an affine fit gives 0.12 mm),
    as the TR-808's service-notes pages are, which is also why the knob rows are 19.86 mm apart on
    the panel and 20.11 mm on the page at its x scale.
  - Given a radial lens term of its own, each photo supports more of the sheet: k = 0.5 (909bd),
    0.75 (dm3), 0.75 (bdaniel). bdaniel, corrected from an oblique view (its lens distortion lies
    elsewhere in the picture), has the sections regular to 0.1 mm (45.91-46.04).
- So the frame is the first measurement's (2026-10-06): the master rectified by its outline, then
  regularised; `measure_panel.py` on it gives the same numbers as before, element for element.

## Cross-check

Each photo is mapped onto the regularised master: SIFT key points (the master at 0.6 x, about the
photos' scale), Lowe's ratio test (0.75), a RANSAC homography (0.5 mm), 961-1267 matches and
336-695 inliers; then three rounds of dense refinement, the photo resampled into the master's frame
and the shift found by phase correlation on a 3 mm grid of 8 x 8 mm patches over the print (patches
near a knob, key or button left out, as their raised tops lean with each camera's parallax, and
those without detail in both directions), the mapping refitted to them (RANSAC 0.4, then 0.2 mm,
then least squares), with a two-term radial lens distortion about the photo's centre fitted with
the homography. Residuals: 909bd 2426 patches, 0.23 mm rms (median 0.17); dm3 2675, 0.21 (0.12);
bdaniel 2496, 0.28 (0.20).

Then every element is measured on each photo the same way as on the master (tick-ring centres, ink
boxes, bar and box edges, line positions, blobs) and compared; only measurements that found the
same thing count (size within 0.6 mm, centre within 1.5 mm). "Locally" is after taking out, per
photo, a cubic in x and y fitted to the differences (what two radial terms leave of its lens).

| Group | 909bd rms x / y (mm) | dm3 rms x / y | locally (909bd; dm3) |
|---|---|---|---|
| Knob tick-ring centres (29) | 0.10 / 0.12 | 0.07 / 0.12 | 0.06 / 0.08; 0.04 / 0.08 |
| Legends, ink boxes (113 / 102 of 133) | 0.14 / 0.15 | 0.12 / 0.10 | 0.11 / 0.08; 0.07 / 0.06 |
| Header bars, orange boxes (17) | 0.10 / 0.07 | 0.23 / 0.08 | 0.06 / 0.10; 0.10 / 0.09 |
| Scale and number bars (15) | 0.08 / 0.16 | 0.08 / 0.09 | 0.09 / 0.06; 0.07 / 0.04 |
| Keylines and dividers (21) | 0.10 / 0.03 | 0.13 / 0.01 | 0.08 / 0.09; 0.04 / 0.06 |
| Note glyphs (22 / 21 of 30) | 0.08 / 0.15 | 0.04 / 0.09 | 0.05 / 0.09; 0.07 / 0.05 |
| Logo boxes (3) | 0.29 / 0.08 | 0.12 / 0.08 | 0.09 / 0.08; 0.07 / 0.08 |
| Display window | 0.24 / 0.47 | 0.38 / 0.01 | |
| Key cut-outs as seen (19) | 0.27 / 0.30 | 0.35 / 0.37 | |
| Small caps (20) | 0.33 / 0.29 | 0.34 / 0.39 | |
| Scale LEDs (4) | 0.34 / 0.48 | 0.40 / 0.55 | |

The raised parts differ because each camera leans them differently (the six left-box caps by
0.8-1.1 mm between the master and the other two); their centres in the file are panel-plane
positions from the print and the cut-outs. The scale LEDs' domes measure x 72.4-73.3 mm in the four
photos and 0.2-0.4 mm below their bars' centre lines, the lean of a raised dome; the file keeps them
at x 73.1 on the bar centre lines.

### What the cross-check added (2026-10-17)

- **The cheeks and the plate's edges**, from column profiles over y 30-250 of the master: the
  cheeks' rounded outer edges (x 0-0.62 and 485.38-486.0, darker), the dark seams to the top plate
  (x 7.88-8.12 at their darkest; on the right a softer shade over 478.12-479.12), the highlight on
  the plate's rounded edges (lines at x 8.69 and 477.69, 0.38 mm).
- **The plate's bends**: a faint darker band along the rear (y 0-0.62) and the front bend in two
  bands (278.12-278.81, 278.81-279.49). Lighting, but in every photo.
- **The LEDs behind the 14 lit buttons** (D618-D631), `led` elements of shape `backlight` in their
  caps: the frosted window shows as a 4.9 mm square with a bright rim, its centre 3.0 mm, tinted by
  the LED when it is off (the six left-box caps, which have none, are not).
- **The display's digits** (`display.digit`), from the bdaniel photo, where the unlit segments
  show faintly and the right digit a lit 1 (segments b and c): segment rows a, g, d at y 173.07,
  179.19, 185.94 (digits 14.3 mm tall, segments 1.45 mm), slant 12 deg, digit width 7.45 mm,
  centres x 101.32, 114.02, 126.72 at mid height. The pitch measures 12.25 mm by autocorrelation;
  the 12.7 mm (0.5 in) of a 0.56 in 3-digit module is taken. +-0.5 mm.
- **MAX**: the VOLUME knob hides its M in the master; x 435.13-440.37 from 909bd and bdaniel,
  offset by MIN beside it, y as MIN.
- **The grey key tops** (steps, ENTER): 12.6 mm square from profiles across keys 8, 9 and ENTER
  (12.62, 13.12, 12.50), where the first pass estimated 13.0.
- No screws show on the top panel in any photo.

## Dimensions

| | Value |
|---|---|
| Official size | 486 (W) x 105 (H) x 300 (D) mm, 4.5 kg (owner's manual p. 39, service notes p. 1) |
| Top view (what the editor draws) | 486.0 x 279.49 mm, aspect 1.739: the width over the outer faces of the two side cheeks, the depth the length of the sloping top seen square-on. The 300 mm depth is the footprint, with the base and its front lip below and in front of the slope. |
| Side cheeks | x 0-8.24 and 478.03-486, olive grey (parts 2112017600 / 2112017700); only their top edges show from above |
| Top plate | x 8.24-478.03, the full depth; painted steel (2221039300) |
| Design grid | step keys at x = 82.28 + 23.0 (k - 1); the instrument sections and the step dividers on the key-pair boundaries x = 82.28 + 23 (j - 0.5) (116.78, 162.78 ... 392.78); small buttons on a 12.0 mm pitch; knob rows y 119.93 and 139.82 |
| Scale | 4.4962 px/mm in the master photo, 8 px/mm in the resampled master; the editor draws 2.5 px/mm (1215 x 699 px) |

## Physical structure

- **The case** is a wedge: the sloping top runs down from 105 mm at the rear towards the front,
  between the two olive cheeks (about 8 mm thick from above). The plate bends down at the rear and
  at the front (a fine darker band along both edges); corners are rounded, r about 1 mm. All the
  controls stand on the one plane: there is no sub-panel.
- **Knobs**: plain charcoal cylinders, very slightly tapered, flat top, an orange pointer line on the
  top running in from the rim (about 2 mm long on the small knobs, 4.5 on the large). Small knobs
  (2247030300, 27 of them on 16 mm pots): top 10.8 mm (measured), base about 11.6, about 15 mm
  tall. TEMPO and VOLUME (2247029000): top 20 mm, base about 21, about 17 mm tall.
- **Large keys**: the step keys and CARTRIDGE/ENTER (grey key top 2247029100 on KFL11904 /
  KFL10903 switches with their LED) and START, STOP/CONT (ivory 2247029200 on KHJ10902). Square
  cut-out about 18.85 x 19.1 mm; the cap fills it but for a dark gap, widest at the front, with
  sloped sides up to a dished top face, 12.6 x 12.6 mm on the grey keys (from profiles across keys
  8, 9 and ENTER; 15 x 15 estimated on the ivory caps), about 9 mm proud. The grey caps have a dark
  LED lens, 5.4 x 2.3 mm, near the rear of the top (y 239.16 on the step keys, 174.19 on
  CARTRIDGE/ENTER).
- **Small buttons** (tact switch KHJ11901, key top 2247029300): the visible cap is 8.6 x 8.6 mm with
  its sloped sides, the top about 6.6 mm; about 5 mm proud. Smoky translucent grey, with a frosted
  window on top (4.9 mm square, a bright rim round a 3.0 mm centre); the 14 in the middle row have a
  red LED behind them (D618-D631), which tints the centre and lights it red when on. The six in the
  left box have none.
- **Display**: a red filter window (2222030700), 39.9 x 19.7 mm, flush with the panel, over the
  3-digit 7-segment LED D636 (LB-603VK): digits 14.3 mm tall, 7.45 wide, slanted 12 deg, 1.45 mm
  segments, centres x 101.32, 114.02, 126.72 at y 179.5 ("What the cross-check added").
- **Scale LEDs**: four 3 mm red LEDs (D632-D635) at x 73.1 in the left ends of the four scale bars.

## Colours

Render values, white-balanced; measured values depend on the light (master: Soundgas studio,
contrasty; close-up: Soundgas #393354 in daylight; 909bd, bdaniel: the Commons photos). `colours`
in the file has every source value.

| Key | Render | Use |
|---|---|---|
| `panel` | `#E6E3D9` | Top-plate paint: warm off-white, satin |
| `side_panel` | `#6C6650` | Olive side cheeks |
| `print_dark`, `header_bar` | `#4B505B` | All dark print: bars, keylines, legends. Slate grey with a blue cast (the master's tone curve crushes it to `#323230`) |
| `print_white` | `#F2F0EA` | Notes, brackets and SHIFT legends in the dark bars (the paint left open) |
| `orange_print` | `#EE7F32` | Header-bar titles and the knob pointers |
| `orange_box` | `#F0883A` | The orange label boxes |
| `logo_orange` | `#EC7A30` | The Roland mark (from the user's artwork) |
| `display` | `#4A141B` | The display filter, unlit |
| `led_lit`, `led_unlit`, `led_window` | `#FF2A1E`, `#4A0A10`, `#3A3A3C` | LEDs and segments, the scale LEDs unlit, the lenses in the key tops unlit |
| `knob`, `knob_skirt_shade` | `#46443F`, `#2A2826` | Knob top, shaded side |
| `key_cap`, `key_cap_top`, `key_cap_side` | `#CDC9BC`, `#DCD8CC`, `#A6A194` | Grey key tops |
| `ivory_cap` | `#F1E6C6` | START and STOP/CONT |
| `small_btn`, `small_btn_centre` | `#77726F`, `#C9C2C0` | Small caps: the smoky cap (near black under the master's studio light, `#3E3C3B`; light grey in daylight, `#989694`), its frosted window |
| `small_btn_centre_red`, `small_btn_centre_plain` | `#7E6A62`, `#ADA292` | The window's centre over an unlit LED (tinted by it), and on the six left-box caps, which have none |
| `key_hole` | `#141414` | The gaps in the cut-outs |
| `side_panel_edge`, `cheek_gap`, `plate_edge` | `#605847`, `#524942`, `#F3EDE8` | The cheeks' rounded outer edges, the seams to the plate, the plate's rounded edges |
| `rear_bend`, `front_bend`, `front_bend_dark` | `#DCDCD3`, `#BBB4AA`, `#9B968E` | The plate bending down at the rear and the front |

## Typography

- All legends are Helvetica capitals. Helvetica Medium: the knob legends (cap 2.1 mm), the
  middle-row legends (START, STOP/CONT, MEAS/TEMPO, CARTRIDGE, VOLUME at 2.5; TRACK PLAY ... at
  1.9-2.1), the orange-box texts (1.9-2.1), the left-box legends (2.0-2.1), RIM SHOT, HAND CLAP,
  CLOSED, OPEN, CRASH, RIDE in the legend boxes (2.25). Helvetica Bold: the header-bar titles
  (2.6), the step numbers (2.9), the white SHIFT legends 1 ... 0, COPY ... LOAD (2.9), the instrument
  names in the legend boxes (2.8). The BANK numerals I and II are a bold Times.
- Double spaces: "RIM SHOT  HAND CLAP", "TRACK  WRITE", "TOTAL  ACCENT", "CRASH  TUNE", "RIDE  TUNE".
  "/ GUIDE" has a space after the slash, "/ENTER" and "/FLAM" none.
- Text boxes are the ink box at the 50 % level; cap heights are +-0.2 mm (the JPEG blur makes
  thin text read 0.1-0.2 mm taller).
- **Brand marks** (the Roland logo, TR-909, RHYTHM COMPOSER) are slots only (`type: logo`, bbox,
  `logo` = the mark's id). The editor fills them from the user's artwork, a file per place in
  `artwork/tr909/` in the ROM folder (`maker`, `model`, `name`; `rear_maker` for the rear's Roland
  logo) or an older `tr909.svg` pack (ids `roland`, `tr909`, `rhythm_composer`, in panel mm), and
  draws none without them (README, "ROMs").

| Slot | Box (mm) | Colour |
|---|---|---|
| `roland` | [34.72, 14.22, 115.22, 26.85] | `logo_orange` |
| `tr909` | [34.90, 74.90, 142.78, 90.52] | `print_dark` |
| `rhythm_composer` | [305.90, 83.50, 441.40, 90.38] | `print_dark` |

## Knobs, keys, LEDs, print

- **Small knob scale**: 11 ticks every 30 deg from -150 deg, 0.42 mm wide, r 5.8 to 7.8, the 0 and
  +-150 deg ticks to 8.7. Legends centred above, 9.3-11.4 mm above the centre.
- **TEMPO and VOLUME**: centres (160.30, 178.66) and (425.46, 178.66); 51 ticks every 6 deg, r 10.8
  to 14.8, every fifth (multiples of 30 deg) to 15.9. TEMPO with SLOW / FAST under it, VOLUME with
  MIN / MAX.
- **Keys**: START (50.52, 178.90), STOP/CONT (75.60, 178.94), CARTRIDGE/ENTER (386.15, 178.63);
  the step keys on the 23.0 mm grid at y about 242.8. Middle row (y 173.61): TRACK PLAY 1-4 at
  193.58 + 12 k, PATTERN PLAY 1-3 at 247.41 + 12 k and EXT INST 283.41, TEMPO/STEP 301.33,
  BACK/TAP 313.33, FWD/I 325.33, AVAILABLE MEAS/II 337.33, CYCLE/GUIDE 351.24, TAPE SYNC 363.24.
  Left box: columns x 45.65 / 60.69, rows y 206.89 / 224.94 / 243.00 (LAST STEP, SCALE /
  SHUFFLE/FLAM, CLEAR / INSTRUMENT SELECT, SHIFT).
- **LEDs**: the step-key and CARTRIDGE/ENTER lenses (`led_step_k`, `led_cartridge_enter`, drawn on
  the key's centre line; the photo shows them leaning with the key tops); the four scale LEDs; the
  14 backlights (`led_track_play_1` ... `led_tape_sync`, the 3.0 mm centre of the frosted window on
  the cap, `window` 4.9 mm). The scale LEDs count from the bottom: the bottom row is scale 1 (16th notes)
  in the owner's manual (p. 24) and Machine::Led::Scale1 (D635, PB4 PA0) on the switch board.
- **Instrument area**: a 0.5 mm frame line at x 35.26 and 440.73 from the header top (y 97.38) to
  152.87; dividers at 70.70 and on the 46 mm grid from 103.13. Nine header bars at y 97.38-102.90,
  corner r about 0.3, 1.1 mm gaps on the divider lines, each with its orange title.
- **Middle row**: the orange boxes at y 184.60-191.07 (TRACK  WRITE, PATTERN WRITE, EXT / INST,
  PATTERN WRITE / MODE, BANK, LAST / MEAS, TEMPO / MODE) and SHIFT in the left box.
- **Step section**: the left-box keyline (0.5 mm, x 35.49-68.57, y 195.71-268.48, open at the top
  right where the scale bars start); four scale bars (x 68.2-439.2, y 195.31-201.80, 202.86-209.38,
  210.39-216.92, 217.88-224.42) split by 1.0-1.2 mm gaps on key boundaries (row 1 after steps 6
  and 12, row 2 after 3, 6, 9, 12, row 3 after 8, row 4 after 4, 8, 12); white eighth notes (rows 1
  and 2) and quarter notes (rows 3 and 4) on the steps that start a beat, white 0.35 mm brackets
  over the beat groups of rows 1 and 2, a white upright between steps 15 and 16 in row 2; the
  keyline over the step numbers (y 226.51, interrupted over step 8); the step dividers (y
  227.63-255.12); the number bar [72.59, 255.87, 437.11, 261.42] with the white SHIFT legends; eight
  legend boxes (0.45 mm keyline, r about 0.9, bottom y 268.5), five with a tab over the left key
  (the top at 262.73 over the first key, a 2 mm slant at about key x + 10.5 down to 263.68).

## Rear panel

`rear_panel` in the file, measured on the straight-on rear photo
`private/docs/photos/soundgas/sn282297_...-7.jpg` to about +-1 mm, cross-checked with the Commons
rear view and the jack-board artwork (SN p. 12). x is in top-panel coordinates (the rear is seen
mirrored), y down from the top edge of the rear face (about 97 mm tall): the cartridge slot, MASTER
OUT R/L, the ten MULTI OUT jacks, SYNC IN, EXT CONTROL, TAPE MEMORY/SYNC, MIDI OUT x2 and IN, the
vent, the POWER rocker and the AC inlet, with their legends.

## Accuracy

| What | Estimate | Evidence |
|---|---|---|
| Knob centres, x | +-0.1 mm | 25 pots of the 1:1 voicing-board artwork (SN p. 10): 0.087 mm rms, max 0.43 (0.375 without the rubber sheet) |
| Knob centres, y | +-0.15 mm | the same pots: 0.17 mm rms, 0.12 with the page's 1.2 % y stretch fitted |
| Knob centres against the other photos | 0.1 mm | rms 0.07-0.10 (x) / 0.12 (y), locally 0.04-0.06 / 0.08 |
| Printed lines, bars, boxes, relative | +-0.15 mm | dividers and keylines 0.10-0.13 / 0.01-0.03, bars 0.08 / 0.09-0.16, header bars and orange boxes 0.10-0.23 / 0.07-0.08 (two photos) |
| Legends, relative | +-0.15 mm (sizes +-0.2) | 0.12-0.14 / 0.10-0.15 rms, 0.07-0.11 / 0.06-0.08 locally |
| Step-key pitch | 23.0 +-0.02 mm | photo, SN p. 9 artwork at 1:1, number-bar legends |
| Absolute, in the 486 mm frame | +-0.3 mm (+-0.5 within 40 mm of the case ends) | the photos differ from the master by 0.34 mm (90th percentile) after homography and lens term, most at the panel's ends; the outline sets the frame |
| Raised parts as seen (key cut-outs, small caps, LED domes) | +-0.5 mm | 0.27-0.55 mm rms between photos (parallax); the file has panel-plane positions |
| Logo slots | +-0.3 mm | 0.12-0.29 / 0.08 rms |
| Display digits | +-0.5 mm | one photo, faint segments; the module's pitch |
| 3-D sizes (heights, knob bodies, ivory cap tops) | +-1 mm (grey key tops +-0.3) | estimates; the grey key tops from profiles |
| Rear panel | +-1 mm | one straight-on photo |

## Uncertain or open

1. **Near the case ends** (x < 40 or > 445) there are few straight lines to check against, and the
   photos differ by up to 0.5 mm there; the logo area (y < 95) is outside the rubber sheet's line
   network and extrapolated (+-0.4 mm).
2. **The print itself** may be off its design grid by up to about 0.2 mm: silk-screen registration
   and stretch can't be told from photo error at this level.
3. **3-D sizes are estimates**: knob and key heights, the knobs' bases, the ivory cap tops. Only the
   footprints seen from above are measured.
4. **Perspective**: the raised parts lean outwards in every photo (in the master from about (255,
   140) mm: key tops by about 1 % of their distance from that point, knob tops by about 2 %). All
   positions in the file are panel-plane positions; the LED lenses are on their keys' centre lines.
5. **Colours** depend on the light. The master's tone curve crushes the slate print to near black
   and the smoky caps read anywhere from near black to light grey; the render values are a judged
   middle.
6. **The display** was measured on one photo with faint segments; the pitch measures 12.25 mm there
   and 12.7 mm (the module's 0.5 in) is used.
7. **The scale LEDs' order**: the bottom row is scale 1 in the owner's manual (p. 24) and
   Machine::Led::Scale1 (PB4 PA0, D635) on the switch board, and the firmware lights it after a
   cleared memory. The only photo with the LEDs on (bdaniel) shows the top one lit beside TRACK 1,
   PATTERN 1 and BANK I, the power-on state; that unit's pattern may simply have been at scale 4. A
   lit photo of a unit at a known scale would settle it.
8. **Watermarks**: the master's lies across the number bar and the legend boxes, dm3's across the
   keys; those legends were measured through them and checked on the other photos.

## Conventions

`panel.json` follows the TB-303's and TR-808's files (`machines/tb303/docs/panel.json`,
`machines/tr808/docs/panel.json`); `conventions` in the file says it in short.

- Coordinates are mm from the rear-left corner of the top view (the outer edge of the left side
  cheek), x to the right, y towards the player; `nx`, `ny` are normalised to 486 / 279.49. Every
  element has `x`, `y` (its centre) and a `bbox` [x0, y0, x1, y1].
- `text`: bbox = the ink box (cap height to baseline for the caps-only legends), `cap_h`, `font`.
  The legends on a `label_box` (header bars, orange boxes) and in an `outline_box` (the legend boxes
  under the number bar) are text elements of their own.
- `outline_box`: bbox = the keyline's outer edge, `points` its outline where it isn't a rectangle (the
  tab over the left key), `stroke`, `corner_r`. `line`: `points` = the stroke's centre line.
- `knob`: the centre of the printed tick ring (= the pot shaft), `knob_d` the top seen from above,
  `base_d` the skirt, `scale` the ticks (angles from 12 o'clock, clockwise positive).
- `key`: the 39 keys and buttons. `shape` large (the 19 mm keys: steps, CARTRIDGE/ENTER, START,
  STOP/CONT) or small (the 8.6 mm tact caps), `key_style` grey, ivory or smoky; bbox = the panel
  cut-out (large) or the visible cap (small), `cutout_bbox`, `cap_top` the flat top face, `led` the
  LED element in or behind the cap, `lit` for the 14 buttons with an LED behind them.
- `led`: `led` = the `tr909::Machine::Led` it shows, `in` = the key it sits in; `shape` rect (the
  lens in a key top), round (the 3 mm scale LEDs) or backlight (the centre of a lit small button's
  frosted window, `window` = that window's side). The scale LEDs count from the bottom: Machine::Led::Scale1 is the bottom row (D635, PB4
  PA0 on the switch board), scale 1 (16th notes) in the owner's manual (p. 24).
- `display`: bbox = the red filter window; `digit` = the 7-segment module's geometry.
- `logo`: a brand-mark slot, bbox only, `logo` = the mark's id (`roland`, `tr909`,
  `rhythm_composer`). Nothing in the file traces the marks: they are trademarks. The editor fills
  the slots from the user's artwork pack (`tr909.svg` in the ROM folder) and draws none without one.
- `param`: every interactive element names what it moves. A knob its parameter id (`bd_tune` ...
  `rd_tune`, `accent` for TOTAL ACCENT, `volume`, `tempo`); a key its `tr909::Machine::Key`
  enumerator (`Step1` ... `Step16`, `Start`, `StopCont`, `Track1` ... `Track4`, `Pattern1` ...
  `Pattern3`, `ExtInst`, `Tempo`, `Back`, `Fwd`, `AvailableMeas`, `Cycle`, `TapeSync`,
  `EnterAccent`, `LastStep`, `Scale`, `ShuffleFlam`, `Clear`, `InstSelect`, `Shift`), as
  `TR909Processor::setKey` takes it.
- Paths to photos and scans are under `private/` (`machines/tr909/private` = the Lemus private
  folder's `tr909/`).

`tools/panel/lemus_panel.py` writes these from what `measure_panel.py` measured, so the measuring
scripts keep their own element names.

## Appendix: element tables (tools/panel/panel_tables.py)

### Knobs

| id | param | function | centre x, y | top Ø | type | printed scale |
|---|---|---|---|---|---|---|
| knob_accent | `accent` | TOTAL ACCENT | 53.07, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_bd_tune | `bd_tune` | BD TUNE | 83.23, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_bd_level | `bd_level` | BD LEVEL | 104.27, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_bd_attack | `bd_attack` | BD ATTACK | 83.22, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_bd_decay | `bd_decay` | BD DECAY | 104.27, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_sd_tune | `sd_tune` | SD TUNE | 129.31, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_sd_level | `sd_level` | SD LEVEL | 150.30, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_sd_tone | `sd_tone` | SD TONE | 129.30, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_sd_snappy | `sd_snappy` | SD SNAPPY | 150.31, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_lt_tune | `lt_tune` | LT TUNE | 175.34, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_lt_level | `lt_level` | LT LEVEL | 196.35, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_lt_decay | `lt_decay` | LT DECAY | 175.36, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_mt_tune | `mt_tune` | MT TUNE | 221.31, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_mt_level | `mt_level` | MT LEVEL | 242.32, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_mt_decay | `mt_decay` | MT DECAY | 221.33, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_ht_tune | `ht_tune` | HT TUNE | 267.28, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_ht_level | `ht_level` | HT LEVEL | 288.28, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_ht_decay | `ht_decay` | HT DECAY | 267.26, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_rs_level | `rs_level` | RIM SHOT LEVEL | 313.23, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_hc_level | `hc_level` | HAND CLAP LEVEL | 334.25, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_hh_level | `hh_level` | HI HAT LEVEL | 380.24, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_hh_ch_decay | `ch_decay` | CLOSED HAT DECAY | 359.28, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_hh_oh_decay | `oh_decay` | OPEN HAT DECAY | 380.25, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_crash_level | `cr_level` | CRASH LEVEL | 405.35, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_ride_level | `rd_level` | RIDE LEVEL | 426.46, 119.93 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_crash_tune | `cr_tune` | CRASH TUNE | 405.34, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_ride_tune | `rd_tune` | RIDE TUNE | 426.47, 139.82 | 10.80 | small | 11 ticks, r 5.8–7.8 (long 8.7 at 0, ±150°) |
| knob_tempo | `tempo` | TEMPO | 160.30, 178.66 | 20.00 | large | 51 ticks, r 10.8–14.8 (long 15.9 at every 30°) |
| knob_volume | `volume` | VOLUME | 425.46, 178.66 | 20.00 | large | 51 ticks, r 10.8–14.8 (long 15.9 at every 30°) |

### Keys and buttons

Size = panel cut-out for the large keys, visible cap for the small buttons; param = the tr909::Machine::Key.

| id | param | function | centre x, y | w × h | shape, style | LED |
|---|---|---|---|---|---|---|
| key_start | `Start` | START | 50.52, 178.90 | 18.99 × 18.97 | large, ivory |  |
| key_stop_cont | `StopCont` | STOP/CONT | 75.60, 178.94 | 18.84 × 18.97 | large, ivory |  |
| btn_track_play_1 | `Track1` | TRACK PLAY 1 | 193.58, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_track_play_1 |
| btn_track_play_2 | `Track2` | TRACK PLAY 2 | 205.58, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_track_play_2 |
| btn_track_play_3 | `Track3` | TRACK PLAY 3 | 217.58, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_track_play_3 |
| btn_track_play_4 | `Track4` | TRACK PLAY 4 | 229.58, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_track_play_4 |
| btn_pattern_play_1 | `Pattern1` | PATTERN PLAY 1 | 247.41, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_pattern_play_1 |
| btn_pattern_play_2 | `Pattern2` | PATTERN PLAY 2 | 259.41, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_pattern_play_2 |
| btn_pattern_play_3 | `Pattern3` | PATTERN PLAY 3 | 271.41, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_pattern_play_3 |
| btn_ext_inst | `ExtInst` | EXT INST | 283.41, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_ext_inst |
| btn_tempo_step | `Tempo` | TEMPO / STEP | 301.33, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_tempo_step |
| btn_back_tap | `Back` | BACK / TAP | 313.33, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_back_tap |
| btn_fwd_bank1 | `Fwd` | FWD / BANK I | 325.33, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_fwd_bank1 |
| btn_avail_meas_bank2 | `AvailableMeas` | AVAILABLE MEAS / BANK II | 337.33, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_avail_meas_bank2 |
| btn_cycle_guide | `Cycle` | CYCLE/GUIDE / LAST MEAS | 351.24, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_cycle_guide |
| btn_tape_sync | `TapeSync` | TAPE SYNC / TEMPO MODE | 363.24, 173.61 | 8.60 × 8.60 | small, smoky (lit) | led_tape_sync |
| key_cartridge_enter | `EnterAccent` | CARTRIDGE/ENTER | 386.15, 178.63 | 18.89 × 18.83 | large, grey | led_cartridge_enter |
| btn_last_step | `LastStep` | LAST STEP | 45.65, 206.89 | 8.60 × 8.60 | small, smoky |  |
| btn_scale | `Scale` | SCALE | 60.69, 206.89 | 8.60 × 8.60 | small, smoky |  |
| btn_shuffle_flam | `ShuffleFlam` | SHUFFLE/FLAM | 45.65, 224.94 | 8.60 × 8.60 | small, smoky |  |
| btn_clear | `Clear` | CLEAR | 60.69, 224.94 | 8.60 × 8.60 | small, smoky |  |
| btn_instrument_select | `InstSelect` | INSTRUMENT SELECT | 45.65, 243.00 | 8.60 × 8.60 | small, smoky |  |
| btn_shift | `Shift` | SHIFT | 60.69, 243.00 | 8.60 × 8.60 | small, smoky |  |
| key_step_1 | `Step1` | 1 | 82.28, 242.91 | 18.84 × 19.06 | large, grey | led_step_1 |
| key_step_2 | `Step2` | 2 | 105.28, 242.87 | 18.84 × 19.10 | large, grey | led_step_2 |
| key_step_3 | `Step3` | 3 | 128.28, 242.85 | 18.85 × 19.12 | large, grey | led_step_3 |
| key_step_4 | `Step4` | 4 | 151.28, 242.83 | 18.86 × 19.08 | large, grey | led_step_4 |
| key_step_5 | `Step5` | 5 | 174.28, 242.78 | 18.86 × 19.18 | large, grey | led_step_5 |
| key_step_6 | `Step6` | 6 | 197.28, 242.81 | 18.86 × 19.06 | large, grey | led_step_6 |
| key_step_7 | `Step7` | 7 | 220.28, 242.81 | 18.86 × 19.06 | large, grey | led_step_7 |
| key_step_8 | `Step8` | 8 | 243.28, 242.80 | 18.86 × 19.08 | large, grey | led_step_8 |
| key_step_9 | `Step9` | 9 | 266.28, 242.82 | 18.85 × 19.07 | large, grey | led_step_9 |
| key_step_10 | `Step10` | 10 | 289.28, 242.79 | 18.85 × 19.02 | large, grey | led_step_10 |
| key_step_11 | `Step11` | 11 | 312.28, 242.77 | 18.85 × 19.05 | large, grey | led_step_11 |
| key_step_12 | `Step12` | 12 | 335.28, 242.80 | 18.85 × 19.01 | large, grey | led_step_12 |
| key_step_13 | `Step13` | 13 | 358.28, 242.76 | 18.85 × 19.10 | large, grey | led_step_13 |
| key_step_14 | `Step14` | 14 | 381.28, 242.76 | 18.85 × 19.11 | large, grey | led_step_14 |
| key_step_15 | `Step15` | 15 | 404.28, 242.78 | 18.85 × 19.04 | large, grey | led_step_15 |
| key_step_16 | `Step16` | 16 | 427.28, 242.72 | 18.85 × 19.08 | large, grey | led_step_16 |

### LEDs and display

led = the tr909::Machine::Led.

| id | led | centre x, y | w × h | shape | in |
|---|---|---|---|---|---|
| display |  | 115.69, 178.75 | 39.87 × 19.74 | window |  |
| led_cartridge_enter | `Enter` | 386.15, 174.19 | 5.40 × 2.30 | rect | key_cartridge_enter |
| led_scale_1 | `Scale4` | 73.10, 198.55 | 3.00 × 3.00 | round |  |
| led_scale_2 | `Scale3` | 73.10, 206.12 | 3.00 × 3.00 | round |  |
| led_scale_3 | `Scale2` | 73.10, 213.65 | 3.00 × 3.00 | round |  |
| led_scale_4 | `Scale1` | 73.10, 221.15 | 3.00 × 3.00 | round |  |
| led_step_1 | `Step1` | 82.28, 239.16 | 5.40 × 2.30 | rect | key_step_1 |
| led_step_2 | `Step2` | 105.28, 239.16 | 5.40 × 2.30 | rect | key_step_2 |
| led_step_3 | `Step3` | 128.28, 239.16 | 5.40 × 2.30 | rect | key_step_3 |
| led_step_4 | `Step4` | 151.28, 239.16 | 5.40 × 2.30 | rect | key_step_4 |
| led_step_5 | `Step5` | 174.28, 239.16 | 5.40 × 2.30 | rect | key_step_5 |
| led_step_6 | `Step6` | 197.28, 239.16 | 5.40 × 2.30 | rect | key_step_6 |
| led_step_7 | `Step7` | 220.28, 239.16 | 5.40 × 2.30 | rect | key_step_7 |
| led_step_8 | `Step8` | 243.28, 239.16 | 5.40 × 2.30 | rect | key_step_8 |
| led_step_9 | `Step9` | 266.28, 239.16 | 5.40 × 2.30 | rect | key_step_9 |
| led_step_10 | `Step10` | 289.28, 239.16 | 5.40 × 2.30 | rect | key_step_10 |
| led_step_11 | `Step11` | 312.28, 239.16 | 5.40 × 2.30 | rect | key_step_11 |
| led_step_12 | `Step12` | 335.28, 239.16 | 5.40 × 2.30 | rect | key_step_12 |
| led_step_13 | `Step13` | 358.28, 239.16 | 5.40 × 2.30 | rect | key_step_13 |
| led_step_14 | `Step14` | 381.28, 239.16 | 5.40 × 2.30 | rect | key_step_14 |
| led_step_15 | `Step15` | 404.28, 239.16 | 5.40 × 2.30 | rect | key_step_15 |
| led_step_16 | `Step16` | 427.28, 239.16 | 5.40 × 2.30 | rect | key_step_16 |
| led_track_play_1 | `Track1` | 193.58, 173.61 | 3.00 × 3.00 | backlight | btn_track_play_1 |
| led_track_play_2 | `Track2` | 205.58, 173.61 | 3.00 × 3.00 | backlight | btn_track_play_2 |
| led_track_play_3 | `Track3` | 217.58, 173.61 | 3.00 × 3.00 | backlight | btn_track_play_3 |
| led_track_play_4 | `Track4` | 229.58, 173.61 | 3.00 × 3.00 | backlight | btn_track_play_4 |
| led_pattern_play_1 | `Pattern1` | 247.41, 173.61 | 3.00 × 3.00 | backlight | btn_pattern_play_1 |
| led_pattern_play_2 | `Pattern2` | 259.41, 173.61 | 3.00 × 3.00 | backlight | btn_pattern_play_2 |
| led_pattern_play_3 | `Pattern3` | 271.41, 173.61 | 3.00 × 3.00 | backlight | btn_pattern_play_3 |
| led_ext_inst | `ExtInst` | 283.41, 173.61 | 3.00 × 3.00 | backlight | btn_ext_inst |
| led_tempo_step | `Tempo` | 301.33, 173.61 | 3.00 × 3.00 | backlight | btn_tempo_step |
| led_back_tap | `Back` | 313.33, 173.61 | 3.00 × 3.00 | backlight | btn_back_tap |
| led_fwd_bank1 | `Fwd` | 325.33, 173.61 | 3.00 × 3.00 | backlight | btn_fwd_bank1 |
| led_avail_meas_bank2 | `AvailableMeas` | 337.33, 173.61 | 3.00 × 3.00 | backlight | btn_avail_meas_bank2 |
| led_cycle_guide | `Cycle` | 351.24, 173.61 | 3.00 × 3.00 | backlight | btn_cycle_guide |
| led_tape_sync | `TapeSync` | 363.24, 173.61 | 3.00 × 3.00 | backlight | btn_tape_sync |

### Bars, boxes and fills

Rectangles as [x0, y0, x1, y1].

| id | label | rect | colour | shape |
|---|---|---|---|---|
| side_panel_left |  | [0.00, 0.00, 8.24, 279.49] | side_panel |  |
| side_panel_right |  | [478.03, 0.00, 486.00, 279.49] | side_panel |  |
| top_plate |  | [8.24, 0.00, 478.03, 279.49] | panel |  |
| hdr_total_accent | TOTAL ACCENT | [36.46, 97.38, 70.17, 102.90] | header_bar |  |
| hdr_bass_drum | BASS DRUM | [71.16, 97.38, 116.17, 102.90] | header_bar |  |
| hdr_snare_drum | SNARE DRUM | [117.27, 97.38, 162.14, 102.90] | header_bar |  |
| hdr_low_tom | LOW TOM | [163.15, 97.38, 208.18, 102.90] | header_bar |  |
| hdr_mid_tom | MID TOM | [209.24, 97.38, 254.21, 102.90] | header_bar |  |
| hdr_hi_tom | HI TOM | [255.32, 97.38, 300.18, 102.90] | header_bar |  |
| hdr_rimshot_handclap | RIM SHOT  HAND CLAP | [301.33, 97.38, 346.25, 102.90] | header_bar |  |
| hdr_hi_hat | HI HAT | [347.37, 97.38, 392.08, 102.90] | header_bar |  |
| hdr_cymbal | CYMBAL | [393.33, 97.38, 439.32, 102.90] | header_bar |  |
| obox_track_write | TRACK WRITE | [188.59, 184.60, 234.37, 191.07] | orange_box |  |
| obox_pattern_write | PATTERN WRITE | [242.46, 184.60, 276.30, 191.07] | orange_box |  |
| obox_ext_inst | EXT INST | [278.29, 184.60, 288.37, 191.07] | orange_box |  |
| obox_pattern_write_mode | PATTERN WRITE MODE | [296.36, 184.60, 318.23, 191.07] | orange_box |  |
| obox_bank | BANK | [320.30, 184.60, 342.24, 191.07] | orange_box |  |
| obox_last_meas | LAST MEAS | [346.27, 184.60, 356.26, 191.07] | orange_box |  |
| obox_tempo_mode | TEMPO MODE | [358.21, 184.60, 368.29, 191.07] | orange_box |  |
| obox_shift | SHIFT | [55.84, 249.01, 65.27, 253.42] | orange_box |  |
| bar_scale1_1 |  | [68.22, 195.31, 208.26, 201.80] | print_dark |  |
| bar_scale1_2 |  | [209.22, 195.31, 346.29, 201.80] | print_dark |  |
| bar_scale1_3 |  | [347.48, 195.31, 439.16, 201.80] | print_dark |  |
| bar_scale2_1 |  | [68.23, 202.86, 139.31, 209.38] | print_dark |  |
| bar_scale2_2 |  | [140.30, 202.86, 208.33, 209.38] | print_dark |  |
| bar_scale2_3 |  | [209.22, 202.86, 277.14, 209.38] | print_dark |  |
| bar_scale2_4 |  | [278.24, 202.86, 346.23, 209.38] | print_dark |  |
| bar_scale2_5 |  | [347.58, 202.86, 439.17, 209.38] | print_dark |  |
| bar_scale3_1 |  | [68.22, 210.39, 254.24, 216.92] | print_dark |  |
| bar_scale3_2 |  | [255.38, 210.39, 439.25, 216.92] | print_dark |  |
| bar_scale4_1 |  | [68.32, 217.88, 162.28, 224.42] | print_dark |  |
| bar_scale4_2 |  | [163.23, 217.88, 254.26, 224.42] | print_dark |  |
| bar_scale4_3 |  | [255.35, 217.88, 346.07, 224.42] | print_dark |  |
| bar_scale4_4 |  | [347.29, 217.88, 439.23, 224.42] | print_dark |  |
| bar_numbers |  | [72.59, 255.87, 437.11, 261.42] | print_dark |  |
| box_bass_drum | BASS DRUM | [72.85, 262.73, 114.72, 268.47] | print_dark | polygon (72.85,262.73) (92.75,262.73) (94.75,263.68) (114.72,263.68) (114.72,268.47) (72.85,268.47) |
| box_snare_drum | SNARE DRUM | [118.79, 262.75, 160.66, 268.50] | print_dark | polygon (118.79,262.75) (138.69,262.75) (140.69,263.69) (160.66,263.69) (160.66,268.50) (118.79,268.50) |
| box_low_tom | LOW TOM | [164.74, 262.73, 206.68, 268.48] | print_dark | polygon (164.74,262.73) (184.64,262.73) (186.64,263.66) (206.68,263.66) (206.68,268.48) (164.74,268.48) |
| box_mid_tom | MID TOM | [210.79, 262.75, 252.71, 268.48] | print_dark | polygon (210.79,262.75) (230.69,262.75) (232.69,263.68) (252.71,263.68) (252.71,268.48) (210.79,268.48) |
| box_hi_tom | HI TOM | [256.83, 262.75, 298.75, 268.52] | print_dark | polygon (256.83,262.75) (276.73,262.75) (278.73,263.69) (298.75,263.69) (298.75,268.52) (256.83,268.52) |
| box_rim_hand | RIM SHOT / HAND CLAP | [302.86, 262.75, 344.69, 268.57] | print_dark | polygon (302.86,262.75) (344.69,262.75) (344.69,268.57) (302.86,268.57) |
| box_hihat | CLOSED / OPEN / CLOSED | [348.78, 262.75, 390.66, 268.61] | print_dark | polygon (348.78,262.75) (368.68,262.75) (370.68,263.69) (390.66,263.69) (390.66,268.61) (348.78,268.61) |
| box_cymbal | CRASH / RIDE | [394.84, 262.69, 436.92, 268.57] | print_dark | polygon (394.84,262.69) (436.92,262.69) (436.92,268.57) (394.84,268.57) |
| cheek_left_edge |  | [0.00, 0.00, 0.62, 279.49] | side_panel_edge |  |
| seam_left |  | [7.88, 0.00, 8.12, 279.49] | cheek_gap |  |
| cheek_right_edge |  | [485.38, 0.00, 486.00, 279.49] | side_panel_edge |  |
| seam_right |  | [478.12, 0.00, 479.12, 279.49] | cheek_gap |  |
| rear_bend |  | [8.24, 0.00, 478.03, 0.62] | rear_bend |  |
| front_bend |  | [8.24, 278.12, 478.03, 278.81] | front_bend |  |
| front_bend_edge |  | [8.24, 278.81, 478.03, 279.49] | front_bend_dark |  |

### Keylines and white lines

| id | points | width | colour |
|---|---|---|---|
| line_inst_div_0 | (35.26, 97.38) → (35.26, 152.87) | 0.50 | print_dark |
| line_inst_div_1 | (70.70, 103.13) → (70.70, 153.12) | 0.50 | print_dark |
| line_inst_div_2 | (116.78, 103.13) → (116.78, 152.87) | 0.50 | print_dark |
| line_inst_div_3 | (162.78, 103.13) → (162.78, 152.87) | 0.50 | print_dark |
| line_inst_div_4 | (208.78, 103.38) → (208.78, 152.87) | 0.50 | print_dark |
| line_inst_div_5 | (254.78, 103.13) → (254.78, 152.87) | 0.50 | print_dark |
| line_inst_div_6 | (300.78, 103.38) → (300.78, 152.87) | 0.50 | print_dark |
| line_inst_div_7 | (346.78, 103.38) → (346.78, 152.87) | 0.50 | print_dark |
| line_inst_div_8 | (392.78, 103.38) → (392.78, 152.87) | 0.50 | print_dark |
| line_inst_div_9 | (440.73, 97.38) → (440.73, 152.87) | 0.50 | print_dark |
| line_leftbox | (67.59, 195.71) → (35.49, 195.71) → (35.49, 268.48) → (68.57, 268.48) → (68.57, 225.40) | 0.50 | print_dark |
| bracket_scale1_1_6 | (75.58, 197.67) → (78.78, 196.07) → (200.78, 196.07) → (203.98, 197.67) | 0.35 | print_white |
| bracket_scale1_7_12 | (214.08, 197.68) → (217.28, 196.08) → (339.03, 196.08) → (342.23, 197.68) | 0.35 | print_white |
| bracket_scale1_13_16 | (352.08, 198.29) → (355.28, 196.69) → (437.03, 196.69) | 0.35 | print_white |
| bracket_scale2_1_3 | (76.08, 205.27) → (79.28, 203.67) → (131.53, 203.67) → (134.73, 205.27) | 0.35 | print_white |
| bracket_scale2_4_6 | (144.83, 205.28) → (148.03, 203.68) → (200.78, 203.68) → (203.98, 205.28) | 0.35 | print_white |
| bracket_scale2_7_9 | (214.58, 205.27) → (217.78, 203.67) → (269.53, 203.67) → (272.73, 205.27) | 0.35 | print_white |
| bracket_scale2_10_12 | (282.83, 205.29) → (286.03, 203.69) → (338.53, 203.69) → (341.73, 205.29) | 0.35 | print_white |
| bracket_scale2_13_15 | (352.08, 205.93) → (355.28, 204.33) → (408.03, 204.33) → (411.23, 205.93) | 0.35 | print_white |
| bracket_scale2_16_16 | (420.33, 205.91) → (423.53, 204.31) → (436.78, 204.31) | 0.35 | print_white |
| line_scale2_div_15_16 | (415.81, 203.41) → (415.81, 208.66) | 0.35 | print_white |
| line_steps_75_240 | (80.25, 226.51) → (222.25, 226.51) | 0.40 | print_dark |
| line_steps_250_436 | (264.25, 226.51) → (429.25, 226.51) | 0.40 | print_dark |
| line_step_div_2_3 | (116.78, 227.63) → (116.78, 255.12) | 0.50 | print_dark |
| line_step_div_4_5 | (162.78, 227.63) → (162.78, 255.12) | 0.50 | print_dark |
| line_step_div_6_7 | (208.78, 227.63) → (208.78, 255.12) | 0.50 | print_dark |
| line_step_div_8_9 | (254.78, 227.63) → (254.78, 255.12) | 0.50 | print_dark |
| line_step_div_10_11 | (300.78, 227.63) → (300.78, 255.12) | 0.50 | print_dark |
| line_step_div_12_13 | (346.78, 227.63) → (346.78, 255.12) | 0.50 | print_dark |
| line_step_div_14_15 | (392.78, 227.63) → (392.78, 255.12) | 0.50 | print_dark |
| line_step_right | (440.73, 195.23) → (440.73, 268.97) | 0.50 | print_dark |
| plate_edge_left | (8.69, 0.00) → (8.69, 279.49) | 0.38 | plate_edge |
| plate_edge_right | (477.69, 0.00) → (477.69, 279.49) | 0.38 | plate_edge |

### Every printed legend

Centre and width of the ink box, cap height (mm).

| text | id | centre x, y | width | cap | font | colour |
|---|---|---|---|---|---|---|
| `TOTAL ACCENT` | txt_hdr_total_accent | 53.44, 100.19 | 26.00 | 2.62 | Helvetica Bold | orange_print |
| `BASS DRUM` | txt_hdr_bass_drum | 93.76, 100.19 | 20.24 | 2.62 | Helvetica Bold | orange_print |
| `SNARE DRUM` | txt_hdr_snare_drum | 139.93, 100.19 | 22.38 | 2.62 | Helvetica Bold | orange_print |
| `LOW TOM` | txt_hdr_low_tom | 185.87, 100.19 | 16.50 | 2.62 | Helvetica Bold | orange_print |
| `MID TOM` | txt_hdr_mid_tom | 231.91, 100.19 | 14.63 | 2.62 | Helvetica Bold | orange_print |
| `HI TOM` | txt_hdr_hi_tom | 277.80, 100.19 | 11.75 | 2.62 | Helvetica Bold | orange_print |
| `RIM SHOT` | txt_hdr_rimshot_handclap_1 | 312.61, 100.19 | 15.75 | 2.62 | Helvetica Bold | orange_print |
| `HAND CLAP` | txt_hdr_rimshot_handclap_2 | 333.25, 100.19 | 19.38 | 2.62 | Helvetica Bold | orange_print |
| `HI HAT` | txt_hdr_hi_hat | 369.91, 100.19 | 10.62 | 2.62 | Helvetica Bold | orange_print |
| `CYMBAL` | txt_hdr_cymbal | 415.81, 100.19 | 13.75 | 2.62 | Helvetica Bold | orange_print |
| `TUNE` | txt_bd_tune | 83.52, 109.54 | 6.88 | 2.12 | Helvetica Medium | print_dark |
| `LEVEL` | txt_bd_level | 104.30, 109.54 | 8.25 | 2.12 | Helvetica Medium | print_dark |
| `TUNE` | txt_sd_tune | 129.66, 109.54 | 7.00 | 2.12 | Helvetica Medium | print_dark |
| `LEVEL` | txt_sd_level | 150.26, 109.54 | 8.13 | 2.12 | Helvetica Medium | print_dark |
| `TUNE` | txt_lt_tune | 175.69, 109.54 | 7.00 | 2.12 | Helvetica Medium | print_dark |
| `LEVEL` | txt_lt_level | 196.43, 109.54 | 8.13 | 2.12 | Helvetica Medium | print_dark |
| `TUNE` | txt_mt_tune | 221.60, 109.54 | 6.88 | 2.12 | Helvetica Medium | print_dark |
| `LEVEL` | txt_mt_level | 242.41, 109.54 | 8.13 | 2.12 | Helvetica Medium | print_dark |
| `TUNE` | txt_ht_tune | 267.56, 109.54 | 6.87 | 2.12 | Helvetica Medium | print_dark |
| `LEVEL` | txt_ht_level | 288.30, 109.54 | 8.00 | 2.12 | Helvetica Medium | print_dark |
| `LEVEL` | txt_rs_level | 313.19, 109.54 | 8.13 | 2.12 | Helvetica Medium | print_dark |
| `LEVEL` | txt_hc_level | 334.29, 109.54 | 8.13 | 2.12 | Helvetica Medium | print_dark |
| `LEVEL` | txt_hh_level | 380.21, 109.54 | 8.13 | 2.12 | Helvetica Medium | print_dark |
| `LEVEL` | txt_crash_level | 405.31, 109.54 | 8.13 | 2.12 | Helvetica Medium | print_dark |
| `LEVEL` | txt_ride_level | 426.60, 109.54 | 8.24 | 2.12 | Helvetica Medium | print_dark |
| `ATTACK` | txt_bd_attack | 83.17, 129.31 | 10.00 | 2.15 | Helvetica Medium | print_dark |
| `DECAY` | txt_bd_decay | 104.36, 129.31 | 8.37 | 2.15 | Helvetica Medium | print_dark |
| `TONE` | txt_sd_tone | 129.22, 129.31 | 7.13 | 2.15 | Helvetica Medium | print_dark |
| `SNAPPY` | txt_sd_snappy | 150.40, 129.31 | 10.25 | 2.15 | Helvetica Medium | print_dark |
| `DECAY` | txt_lt_decay | 175.39, 129.31 | 8.50 | 2.15 | Helvetica Medium | print_dark |
| `DECAY` | txt_mt_decay | 221.48, 129.31 | 8.50 | 2.15 | Helvetica Medium | print_dark |
| `DECAY` | txt_ht_decay | 267.35, 129.31 | 8.37 | 2.15 | Helvetica Medium | print_dark |
| `CH DECAY` | txt_hh_ch_decay | 359.20, 129.31 | 13.76 | 2.15 | Helvetica Medium | print_dark |
| `OH DECAY` | txt_hh_oh_decay | 380.19, 129.31 | 13.13 | 2.15 | Helvetica Medium | print_dark |
| `CRASH TUNE` | txt_crash_tune | 405.21, 129.31 | 18.38 | 2.15 | Helvetica Medium | print_dark |
| `RIDE TUNE` | txt_ride_tune | 426.53, 129.31 | 14.62 | 2.15 | Helvetica Medium | print_dark |
| `TRACK  WRITE` | txt_track_write | 211.47, 187.78 | 18.75 | 2.13 | Helvetica Medium | print_dark |
| `PATTERN WRITE` | txt_pattern_write | 259.15, 187.78 | 20.38 | 2.13 | Helvetica Medium | print_dark |
| `EXT` | txt_ext_inst_1 | 283.25, 186.51 | 5.13 | 2.13 | Helvetica Medium | print_dark |
| `INST` | txt_ext_inst_2 | 283.00, 189.32 | 6.12 | 2.12 | Helvetica Medium | print_dark |
| `PATTERN WRITE` | txt_pattern_write_mode_1 | 307.01, 186.58 | 20.50 | 2.00 | Helvetica Medium | print_dark |
| `MODE` | txt_pattern_write_mode_2 | 307.07, 189.07 | 7.62 | 2.13 | Helvetica Medium | print_dark |
| `BANK` | txt_bank | 331.12, 187.91 | 7.13 | 2.13 | Helvetica Medium | print_dark |
| `LAST` | txt_last_meas_1 | 351.23, 186.70 | 6.62 | 2.00 | Helvetica Medium | print_dark |
| `MEAS` | txt_last_meas_2 | 351.30, 189.20 | 7.25 | 1.88 | Helvetica Medium | print_dark |
| `TEMPO` | txt_tempo_mode_1 | 363.42, 186.64 | 8.88 | 1.88 | Helvetica Medium | print_dark |
| `MODE` | txt_tempo_mode_2 | 363.23, 189.14 | 7.50 | 2.00 | Helvetica Medium | print_dark |
| `START` | txt_start | 50.28, 163.73 | 9.87 | 2.50 | Helvetica Medium | print_dark |
| `STOP/CONT` | txt_stop_cont | 75.21, 163.59 | 18.62 | 2.50 | Helvetica Medium | print_dark |
| `MEAS/TEMPO` | txt_meas_tempo | 115.35, 163.31 | 22.26 | 2.50 | Helvetica Medium | print_dark |
| `TEMPO` | txt_tempo | 160.35, 160.51 | 11.00 | 2.38 | Helvetica Medium | print_dark |
| `SLOW` | txt_slow | 147.52, 192.76 | 7.50 | 2.12 | Helvetica Medium | print_dark |
| `FAST` | txt_fast | 172.82, 192.76 | 6.25 | 2.12 | Helvetica Medium | print_dark |
| `TRACK PLAY` | txt_track_play | 211.40, 163.45 | 16.00 | 2.00 | Helvetica Medium | print_dark |
| `PATTERN PLAY` | txt_pattern_play | 259.25, 163.46 | 18.50 | 1.88 | Helvetica Medium | print_dark |
| `TEMPO` | txt_tempo_btn | 301.45, 163.45 | 9.00 | 2.00 | Helvetica Medium | print_dark |
| `BACK` | txt_back | 313.32, 163.38 | 7.00 | 2.13 | Helvetica Medium | print_dark |
| `FWD` | txt_fwd | 325.32, 163.40 | 6.00 | 2.00 | Helvetica Medium | print_dark |
| `AVAILABLE` | txt_available | 336.63, 162.22 | 13.63 | 2.00 | Helvetica Medium | print_dark |
| `MEAS` | txt_meas | 336.59, 164.82 | 7.38 | 2.00 | Helvetica Medium | print_dark |
| `CYCLE` | txt_cycle | 351.51, 162.22 | 8.38 | 2.00 | Helvetica Medium | print_dark |
| `/ GUIDE` | txt_guide | 350.94, 164.78 | 9.88 | 2.00 | Helvetica Medium | print_dark |
| `TAPE` | txt_tape | 363.38, 162.22 | 6.13 | 2.00 | Helvetica Medium | print_dark |
| `SYNC` | txt_sync | 363.35, 164.76 | 7.00 | 2.12 | Helvetica Medium | print_dark |
| `CARTRIDGE` | txt_cartridge | 386.19, 161.97 | 18.38 | 2.50 | Helvetica Medium | print_dark |
| `/ENTER` | txt_enter | 386.41, 164.94 | 12.38 | 2.50 | Helvetica Medium | print_dark |
| `TOTAL  ACCENT` | txt_total_accent | 386.38, 192.22 | 20.87 | 2.25 | Helvetica Medium | print_dark |
| `VOLUME` | txt_volume | 425.40, 160.50 | 13.00 | 2.50 | Helvetica Medium | print_dark |
| `MIN` | txt_min | 413.77, 192.59 | 4.75 | 1.87 | Helvetica Medium | print_dark |
| `STEP` | txt_step | 301.50, 181.62 | 6.76 | 2.25 | Helvetica Medium | print_dark |
| `TAP` | txt_tap | 313.39, 181.50 | 4.75 | 2.00 | Helvetica Medium | print_dark |
| `I` | txt_bank_1 | 325.29, 181.56 | 0.62 | 2.12 | Times Roman (serif numerals) | print_dark |
| `II` | txt_bank_2 | 337.26, 181.69 | 1.63 | 2.13 | Times Roman (serif numerals) | print_dark |
| `MAX` | txt_max | 437.75, 192.59 | 5.24 | 1.87 | Helvetica Medium | print_dark |
| `1` | txt_track_play_1 | 193.38, 181.78 | 0.75 | 1.87 | Helvetica Medium | print_dark |
| `2` | txt_track_play_2 | 205.31, 181.66 | 1.38 | 2.12 | Helvetica Medium | print_dark |
| `3` | txt_track_play_3 | 217.37, 181.66 | 1.50 | 2.12 | Helvetica Medium | print_dark |
| `4` | txt_track_play_4 | 229.31, 181.66 | 1.38 | 1.88 | Helvetica Medium | print_dark |
| `1` | txt_pattern_play_1 | 247.41, 181.66 | 0.87 | 1.88 | Helvetica Medium | print_dark |
| `2` | txt_pattern_play_2 | 259.42, 181.72 | 1.37 | 2.00 | Helvetica Medium | print_dark |
| `3` | txt_pattern_play_3 | 271.35, 181.66 | 1.50 | 2.12 | Helvetica Medium | print_dark |
| `LAST STEP` | txt_last_step | 45.34, 213.82 | 14.37 | 2.00 | Helvetica Medium | print_dark |
| `SCALE` | txt_scale | 60.30, 213.82 | 8.50 | 2.00 | Helvetica Medium | print_dark |
| `SHUFFLE` | txt_shuffle | 45.25, 232.36 | 11.74 | 2.12 | Helvetica Medium | print_dark |
| `/FLAM` | txt_flam | 44.55, 234.86 | 8.75 | 2.12 | Helvetica Medium | print_dark |
| `CLEAR` | txt_clear | 60.37, 232.36 | 8.63 | 2.12 | Helvetica Medium | print_dark |
| `INSTRUMENT` | txt_instrument | 45.17, 249.90 | 17.25 | 2.00 | Helvetica Medium | print_dark |
| `SELECT` | txt_select | 45.23, 252.44 | 10.13 | 2.12 | Helvetica Medium | print_dark |
| `SHIFT` | txt_shift | 60.30, 251.22 | 7.37 | 2.12 | Helvetica Medium | print_dark |
| `1` | txt_step_1 | 82.33, 229.41 | 1.25 | 2.87 | Helvetica Bold | print_dark |
| `2` | txt_step_2 | 105.45, 229.41 | 2.00 | 2.87 | Helvetica Bold | print_dark |
| `3` | txt_step_3 | 128.45, 229.41 | 2.00 | 2.87 | Helvetica Bold | print_dark |
| `4` | txt_step_4 | 151.39, 229.41 | 2.12 | 2.87 | Helvetica Bold | print_dark |
| `5` | txt_step_5 | 174.33, 229.41 | 2.00 | 2.87 | Helvetica Bold | print_dark |
| `6` | txt_step_6 | 197.39, 229.41 | 2.12 | 2.87 | Helvetica Bold | print_dark |
| `7` | txt_step_7 | 220.39, 229.41 | 1.88 | 2.87 | Helvetica Bold | print_dark |
| `8` | txt_step_8 | 243.39, 229.41 | 2.12 | 2.87 | Helvetica Bold | print_dark |
| `9` | txt_step_9 | 266.39, 229.41 | 2.12 | 2.87 | Helvetica Bold | print_dark |
| `10` | txt_step_10 | 289.32, 229.41 | 3.75 | 2.87 | Helvetica Bold | print_dark |
| `11` | txt_step_11 | 312.39, 229.41 | 3.12 | 2.87 | Helvetica Bold | print_dark |
| `12` | txt_step_12 | 335.26, 229.41 | 3.37 | 2.87 | Helvetica Bold | print_dark |
| `13` | txt_step_13 | 358.39, 229.41 | 3.62 | 2.87 | Helvetica Bold | print_dark |
| `14` | txt_step_14 | 381.32, 229.41 | 3.75 | 2.87 | Helvetica Bold | print_dark |
| `15` | txt_step_15 | 404.39, 229.41 | 3.62 | 2.87 | Helvetica Bold | print_dark |
| `16` | txt_step_16 | 427.45, 229.41 | 3.75 | 2.87 | Helvetica Bold | print_dark |
| `1` | txt_shift_1 | 82.42, 258.79 | 1.25 | 2.87 | Helvetica Bold | print_white |
| `2` | txt_shift_2 | 105.30, 258.79 | 2.00 | 2.87 | Helvetica Bold | print_white |
| `3` | txt_shift_3 | 128.37, 258.79 | 2.13 | 2.87 | Helvetica Bold | print_white |
| `4` | txt_shift_4 | 151.24, 258.79 | 2.12 | 2.87 | Helvetica Bold | print_white |
| `5` | txt_shift_5 | 174.31, 258.79 | 2.25 | 2.87 | Helvetica Bold | print_white |
| `6` | txt_shift_6 | 197.31, 258.79 | 2.25 | 2.87 | Helvetica Bold | print_white |
| `7` | txt_shift_7 | 220.31, 258.79 | 1.75 | 2.87 | Helvetica Bold | print_white |
| `8` | txt_shift_8 | 243.43, 258.79 | 2.25 | 2.87 | Helvetica Bold | print_white |
| `9` | txt_shift_9 | 266.43, 258.79 | 2.25 | 2.87 | Helvetica Bold | print_white |
| `0` | txt_shift_10 | 289.37, 258.79 | 2.37 | 2.87 | Helvetica Bold | print_white |
| `COPY` | txt_shift_11 | 312.36, 258.79 | 9.25 | 2.87 | Helvetica Bold | print_white |
| `INS` | txt_shift_12 | 335.21, 258.79 | 5.63 | 2.87 | Helvetica Bold | print_white |
| `DEL` | txt_shift_13 | 358.15, 258.79 | 6.50 | 2.87 | Helvetica Bold | print_white |
| `SAVE` | txt_shift_14 | 381.10, 258.79 | 8.50 | 2.87 | Helvetica Bold | print_white |
| `VERIFY` | txt_shift_15 | 404.56, 258.79 | 11.87 | 2.87 | Helvetica Bold | print_white |
| `LOAD` | txt_shift_16 | 427.61, 258.79 | 9.25 | 2.87 | Helvetica Bold | print_white |
| `BASS DRUM` | txt_bass_drum | 93.83, 265.83 | 20.25 | 2.87 | Helvetica Bold | print_dark |
| `SNARE DRUM` | txt_snare_drum | 139.76, 265.93 | 22.50 | 2.75 | Helvetica Bold | print_dark |
| `LOW TOM` | txt_low_tom | 185.15, 265.85 | 16.62 | 2.87 | Helvetica Bold | print_dark |
| `MID TOM` | txt_mid_tom | 231.83, 265.90 | 14.62 | 2.75 | Helvetica Bold | print_dark |
| `HI TOM` | txt_hi_tom | 277.87, 265.88 | 11.62 | 2.88 | Helvetica Bold | print_dark |
| `RIM SHOT` | txt_rim_hand_rim_shot | 313.30, 265.89 | 12.88 | 2.25 | Helvetica Medium | print_dark |
| `HAND CLAP` | txt_rim_hand_hand_clap | 333.93, 265.89 | 15.63 | 2.25 | Helvetica Medium | print_dark |
| `CLOSED` | txt_hihat_closed | 356.85, 266.00 | 11.37 | 2.37 | Helvetica Medium | print_dark |
| `OPEN` | txt_hihat_open | 369.85, 265.94 | 7.63 | 2.25 | Helvetica Medium | print_dark |
| `CLOSED` | txt_hihat_closed2 | 382.71, 266.06 | 11.37 | 2.24 | Helvetica Medium | print_dark |
| `CRASH` | txt_cymbal_crash | 404.53, 266.02 | 9.62 | 2.24 | Helvetica Medium | print_dark |
| `RIDE` | txt_cymbal_ride | 427.46, 265.89 | 6.25 | 2.25 | Helvetica Medium | print_dark |

### Note glyphs in the scale bars

| id | glyph | bbox |
|---|---|---|
| note_scale1_1 | eighth note | [81.21, 197.06, 83.58, 201.50] |
| note_scale1_3 | eighth note | [127.21, 197.06, 129.58, 201.50] |
| note_scale1_5 | eighth note | [173.21, 197.06, 175.58, 201.50] |
| note_scale1_7 | eighth note | [219.21, 197.06, 221.58, 201.50] |
| note_scale1_9 | eighth note | [265.21, 197.06, 267.58, 201.50] |
| note_scale1_11 | eighth note | [311.21, 197.06, 313.58, 201.50] |
| note_scale1_13 | eighth note | [357.21, 197.06, 359.58, 201.50] |
| note_scale1_15 | eighth note | [403.21, 197.06, 405.58, 201.50] |
| note_scale2_1 | eighth note | [81.21, 204.61, 83.58, 209.05] |
| note_scale2_2 | eighth note | [104.21, 204.61, 106.58, 209.05] |
| note_scale2_3 | eighth note | [127.21, 204.61, 129.58, 209.05] |
| note_scale2_4 | eighth note | [150.21, 204.61, 152.58, 209.05] |
| note_scale2_5 | eighth note | [173.21, 204.61, 175.58, 209.05] |
| note_scale2_6 | eighth note | [196.21, 204.61, 198.58, 209.05] |
| note_scale2_7 | eighth note | [219.21, 204.61, 221.58, 209.05] |
| note_scale2_8 | eighth note | [242.21, 204.61, 244.58, 209.05] |
| note_scale2_9 | eighth note | [265.21, 204.61, 267.58, 209.05] |
| note_scale2_10 | eighth note | [288.21, 204.61, 290.58, 209.05] |
| note_scale2_11 | eighth note | [311.21, 204.61, 313.58, 209.05] |
| note_scale2_12 | eighth note | [334.21, 204.61, 336.58, 209.05] |
| note_scale2_13 | eighth note | [357.21, 204.61, 359.58, 209.05] |
| note_scale2_14 | eighth note | [380.21, 204.61, 382.58, 209.05] |
| note_scale2_15 | eighth note | [403.21, 204.61, 405.58, 209.05] |
| note_scale2_16 | eighth note | [426.21, 204.61, 428.58, 209.05] |
| note_scale3_1 | quarter note | [81.33, 211.89, 82.71, 216.01] |
| note_scale3_9 | quarter note | [265.33, 211.89, 266.71, 216.01] |
| note_scale4_1 | quarter note | [81.33, 219.38, 82.71, 223.50] |
| note_scale4_5 | quarter note | [173.33, 219.38, 174.71, 223.50] |
| note_scale4_9 | quarter note | [265.33, 219.38, 266.71, 223.50] |
| note_scale4_13 | quarter note | [357.33, 219.38, 358.71, 223.50] |

### Brand-mark slots

The artwork pack's ids; slots only.

| pack id | mark | bbox | colour |
|---|---|---|---|
| `roland` | Roland | [34.72, 14.22, 115.22, 26.85] | logo_orange |
| `tr909` | TR-909 | [34.90, 74.90, 142.78, 90.52] | print_dark |
| `rhythm_composer` | RHYTHM COMPOSER | [305.90, 83.50, 441.40, 90.38] | print_dark |
