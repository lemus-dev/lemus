# TR-808 top panel: how `panel.json` was measured

`panel.json` is a to-scale description of the TR-808 top panel for drawing it in the plug-in, in the
same style as `machines/tb303/docs/panel.json`: 419 elements (case, rules, print, controls, keys,
LEDs, logo slots), a colour table, font notes and the rear-panel jack order. Coordinates are mm from the
rear-left corner of the top view, x to the right, y towards the player.

Brand marks (Roland logo, "Rhythm Composer", "TR-808", "Computer Controlled") are slots only
(`type: logo`, bbox and the mark's id). Nothing in this file traces them. The editor fills them from
the user's artwork, a file per place in `artwork/tr808/` in the ROM folder (`maker`, `name`, `model`,
`tagline`) or an older `tr808.svg` pack (ids `roland`, `rhythm_composer`, `tr808`,
`computer_controlled`, in panel mm), and draws none without them (README, "ROM"). The orange rule
under "Rhythm Composer" is a separate `line` with `with_logo: true`, drawn only with the
`rhythm_composer` mark, whose descenders cross it.

## Sources

All files are under `private/tr808/docs/` (reference only unless noted; see `photos/sources.md`).

| Role | File |
|---|---|
| Geometry master | `photos/soundgas/mintboxed_Roland-TR-808-Rhythm-Composer-SN228285-Mint-3.jpg` (2400 x 1484, top-down cut-out) |
| Cross-check 1, fine detail | `photos/commons_tr808_mim_pocius_cropped.jpg` (CC BY 2.0, Bryan Pocius, 3764 x 2240) |
| Cross-check 2, clean number bar | `photos/soundgas/rc20_Roland-TR-808-Rhythm-Composer-SN051874-2.jpg` (2400 x 1549) |
| Close-ups (3-D shapes) | `soundgas/mintboxed_...-Mint-2.jpg` (front), `-8`, `-9`; `sn310866_*_5.png` (side) |
| Rear panel | `soundgas/mintboxed_...-Mint-12.jpg`; legends from `vse/vse_roland_tr808_rear_lg.jpg` |
| Scale check | service notes `schematic/hires/sn-p-10-400dpi.png`, `sn-p-11-400dpi.png`, `sn-p-12-400dpi.png` |
| Legends, colours, layout | `photos/panel-notes.md` |

Pocius and rc20 were mapped onto the master with SIFT matches and a RANSAC homography (1255 and 1481
inliers, rms 1.0 and 0.9 master px). Every measurement was then made on three 8 px/mm resamplings of the
three photos in the same mm frame, so the photos could be compared feature by feature. The measuring
scripts lived in a scratch folder and are not part of the repo; the method is described below in
enough detail to redo it.

## Frame and scale

- **Frame.** master px = (95.5, 77) + 4.338 x (c + R(+0.0015 rad) x (p - c)), with c = (254, 152.5) mm.
  - Without the rotation this is the frame of `panel-notes.md`: x_mm = (x - 95.5)/4.338, y_mm = (y - 77)/4.338.
  - The master's outline is 2203.5 x 1322 px, which is 508 x 305 mm at 4.338 px/mm (the owner's-manual and service-notes size).
- **Rotation 0.0015 rad (0.086 deg).** The top and middle rules, the plate rows and the rear and front outline edges all slope by 0.0013-0.0016 in the master. The frame is turned to level them. This moves nothing by more than 0.4 mm.
- **The real panel is about 5 % smaller than 508 mm.** Two independent checks against the service notes say so:
  - **SN p. 11 main board** (foil side, mirrored). The 42-pin CPU footprint has a pad pitch of 37.52 px at 400 dpi. Taken as 2.54 mm, that makes the page a 0.938 reduction, and the pot terminal pitch then comes out at 4.98 mm, as it should.
    - The 15 pots under the AC-CP knobs fit the printed tick-ring centres in this file with a similarity scale of 1.0575 (rms 0.36 mm; with an affine fit, 0.22 mm).
  - **SN p. 10 switch board.** The FHI-12S-2.54DS connector has a 30.28 px pitch, and the 16 key switches are 213.86 px apart. That gives a key pitch of 17.94 mm, against 18.96 mm in this file.
  - Both give **true mm = 0.946 x panel.json mm**, a real width of about 481 mm. The Henry Ford museum measured the unit at 19 in (482.6 mm).
  - The 508 x 305 spec looks like a rounded 20 x 12 in figure.
  - The voicing-board artwork on SN p. 12 is not at 1:1 either. Fitting the CB-CH pots gives 0.985, and its hand-drawn pot pads scatter by +-4 %, so it cannot settle the scale.
  - The file stays in the requested 508 x 305 frame. `frame.true_mm_factor = 0.946` converts to physical mm; proportions are unaffected. Kept that way (2026-10-12): nothing on screen depends on it, and an older artwork pack measured from a real unit scales by the factor (a place's own file is fitted to its box).

## The rear panel

`rear_panel` (`layout`, and each item's `y`, `w`, `h`, `below`, `above`) is measured on the straight-on rear photo at 4.34 px/mm; y is down from the rear face's top edge. The SYNC switch was at the legend's x (68.4) and is now at the slot's (75.3).

## Corrections and what was *not* corrected

- **Parallax.** The Soundgas camera was close, about 0.5 m away.
  - Knob tops, key tops and key caps lean outwards from about (254, 153) mm: by up to 6 mm on the knobs (FINE, CH), and by up to 3.7 mm on the TAP and START/STOP caps.
  - No centre was taken from a raised part. Every centre comes from print:
    - knobs: the centre of the printed tick ring, or a circle fitted to the printed dots;
    - rotary switches: the printed dots, or the orange numbers;
    - TEMPO: the edges of the printed annulus;
    - keys: the printed step numbers, number-bar digits and the key LEDs;
    - START/STOP and TAP: the panel cut-out;
    - CLEAR button: the point where the three printed lines meet.
  - The text printed on the TAP cap was measured on the cap and moved with it onto the cut-out centre.
  - START, STOP and the rule between them are placed relative to each other (three photos) with the rule on the cap's centre: the cap's sloped sides show unevenly in every photo, so its print cannot be located from the cap's outline.
- **Lens bow: not corrected.** The long straight print does bow by up to +-0.4 mm.
  - The mid rule and the pre-scale rows bow like a smile; the top rule bows the other way.
  - The plate rows and the LEVEL row rise by about 0.9 mm from AC to HT.
  - All three photos show the same pattern to 0.1-0.3 mm after a projective map. Two different cameras could not produce identical distortion, and a homography cannot transfer one, so the irregularity is most likely in the print itself.
  - The column rules lean by -0.1 to +1.0 mm over their 105 mm length, in a non-monotonic pattern. Lens distortion would be symmetric, so this is in the print, and the rules are given with their measured lean.
  - So the elements keep their individually measured positions; the 909's rubber-sheet regularisation was not applied.
  - The knob centres agree with the drilled pot grid of SN p. 11 to 0.22 mm rms, so whatever is left is within the accuracy goal.
- **Watermarks.** The master's watermark covers the number bar, so the number bar, its digits and the tags were measured on rc20 and Pocius. The rc20 watermark sits on the front rim and was avoided.

## Method by element group

- **Case.**
  - The outline polygon was traced from the white background (approxPolyDP, 0.35 mm).
  - The rear jacks peeking over the rear edge are excluded.
  - Side panels: top face 0-19.2 / 488.2-508 mm, with a rounded highlight edge and a dark gap of about 1.5 mm to the plate. The plate runs x 21.7-485.6.
  - The polygons include the rear tabs and front feet of the chamfered side panels as seen from above.
- **Rules.** Each rule's centre and width come from the light-dark-light edge pair in the master (sub-pixel), fitted as a line, with its extent from a brightness scan.
  - The top rule is 1.62 mm wide, the middle rule 1.75 mm, the column rules about 0.45 mm.
  - The grey mode-block frame is about 0.9 mm.
- **Mode block lines.** The cream, orange and grey keylines were found as straight runs in colour masks, then refined with edge pairs. The stroke is about 0.45 / 0.40 / 0.35 mm.
  - The outline boxes give the outer edge of the keyline as bbox.
- **Text.** Each legend is the union bbox of the ink components in a search box, with components that touch the box border dropped (lines, neighbours).
  - The threshold is half-way between the local ground and the ink.
  - cap_h is the bbox height for all-caps legends. The bboxes carry about 0.1 mm of JPEG blur on each side.
  - Legends hidden by a knob in the master ("1st PART") were taken from Pocius.
- **Voice knobs.** The tick-ring centre is the mean of two estimates: the least-squares point nearest the 11 radial tick axes, and a circle fitted to the tick outer ends.
  - Results from the three photos were weighted 0.5 / 0.3 / 0.2 (master / Pocius / rc20). Master and Pocius agree to about 0.2 mm.
  - Ticks run r 5.8 to 7.8 mm and are about 0.45 mm wide.
  - The LEVEL "standard" dot sits at 75 deg, r 7.3 mm, about 1.2 mm across.
  - Inserts are about 5.2 mm across; the knob body is about 11 mm.
- **Plates.** Edges are the median gradient peaks along each side, averaged over the three photos (spread 0.1-0.47 mm).
  - Large capitals are 4.0 mm, small capitals 1.95 mm, from the component heights.
  - The spans list the full and small capitals as printed.
- **Rotary switches, FINE, MASTER VOLUME, AUTO FILL IN.** Circles were fitted to the printed dots with the angles fixed at 30-deg steps (rms 0.13-0.24 mm).
  - INSTRUMENT-SELECT has no dots. Its centre comes from the 12 orange numbers (r 13.5 mm, rms 0.3 mm), and the symmetry of the 12 boxes agrees with it to 0.1 mm.
- **TEMPO dial.** Ellipses were fitted to the ring's outer and inner edges in three photos: centre spread 0.3 mm, axis ratio 0.994.
  - Ring diameters are 58.5 / 48.3 mm.
  - The digits sit at r 26.7 mm, upright.
  - The 41 dots are at r 23.0 mm, every 7.5 deg from -150 deg.
- **Pre-scale bars, glyphs and arcs.**
  - Bars are the grey runs between gaps of 3.8-4.1 mm (splits shorter than 3 mm, caused by glyphs, were merged), with edges by gradient; master and Pocius averaged.
  - Note glyphs are placed by their head centroid. The bboxes come from a template: ♪ 3.4 x 5.15 mm, ♩ 2.05 x 4.95 mm.
  - Arcs were sampled every 0.4 mm as the darkest row under the bar top, fitted with a quadratic and written as 9-point polylines.
- **Keys.**
  - The x of each step key is its LED centre, which agrees with the printed step number above and the digit below to about 0.3 mm. The step grid averages 18.96 mm.
  - The top face is 13.3 x 23.9 mm (the key-top blobs measured 13.6 x 24.3 in all three photos, trimmed for blur and side faces).
  - The LED is 3.5 mm behind the rear end of the face, 3 mm across.
  - START/STOP: the cap is 36 x 16.4 mm at the panel, with a 32.4 x 13 mm flat top, in a 39.9 x 20.3 mm cut-out. TAP: 18.6 mm at the panel, with a 13.5 x 13.2 mm flat top, in a 20.4 x 20.5 mm cut-out. CLEAR: 6.6 mm cap in an 8.6 mm ring.
- **Slide and lever switches.**
  - Slots come from dark-rectangle edges: voice 5.8 x 12.1, PRE-SCALE 6.5 x 19.1, BASIC-VARIATION 20.8 x 7.55, I/F-VARIATION 13.6 x 7.6 mm.
  - Lever positions (dx) are the A/AB/B legend centres.
  - Slider offsets come from the slider in the photos: the voice switches are shown down in Pocius, PRE-SCALE at 4 in the master.
- **Screws.** 16 screws, dark-blob bbox centres, head about 6.2 mm across.
- **Colours.** Medians over flat areas of the master (neutral studio light); thin print uses the 80th-85th percentile. Values follow `panel-notes.md` section 8 where it already had them; the notes in the table give the ranges.

## Accuracy

| What | Estimate | Evidence |
|---|---|---|
| Printed features, relative | +-0.3 mm | Three photos agree to 0.1-0.3 mm (plates, bars, rings). |
| Knob and switch centres | +-0.2-0.3 mm | Pot grid of SN p. 11: 0.22 mm rms (affine). Dot fits: rms 0.13-0.24 mm. |
| Absolute, in the 508 frame | +-0.5 mm | Rotation and frame residuals. |
| Physical mm | about 5 % | See `true_mm_factor`. |
| 3-D sizes (knob bodies and heights, key faces, cap faces, slider travel) | +-1 mm | Heights are estimates (the 303 file style). |
| Rear panel | +-2 mm | Perspective of the rear photo. |

## Uncertain or open

1. **Scale.** The frame is the 508-mm spec; the artwork says the unit is 0.946 of that. See above.
2. **y versus x.** Affine fits to both artwork pages give y about 1.2-1.4 % larger than x in this frame relative to the artwork. The printed TEMPO ring in the master is round to 0.6 %. The anisotropy is probably in the scanned service-notes pages, and the frame is left isotropic. If it were in the photo, the 305-mm depth would be up to 4 mm short.
3. **Voice slide-switch travel** (+-1.2 mm) and the **PRE-SCALE** steps (4.03 mm) come from one slider position each, +-0.5 mm.
4. **Hidden print.** The MODE PATTERN CLEAR dot and the MASTER VOLUME 8/9 dots are under the knobs in every photo and are computed. The MASTER VOLUME "8" legend is partly covered.
5. **`key_window`** (the darker window round the step keys) is approximate, +-1 mm. It barely differs from the lower ground.
6. **Front and rear bends.** `front_lip` and `rear_bend` are seen at a grazing angle; their widths are +-0.5 mm.
7. **Glyph and arc shapes** are templates. Positions are measured; the stroke shapes are described, not traced.
8. **Number-bar digits 10-12** were measured on rc20 and Pocius only (watermark in the master).

## Conventions and new element types

- These are as in the TB-303 file: `text` bbox = ink box, `outline_box` bbox = outer keyline edge, `line` points = stroke centre, plus `nx` and `ny` normalised to 508 / 305.
- New types:
  - `dial` (TEMPO ring, digits, 41 dots, knob sub-object);
  - `key` (`shape`: wedge / rect_large / square / round; `led`; `cutout_bbox`);
  - `slide_switch` (slot, slider, positions with `offset`);
  - `lever_switch` (slot, lever head, positions with `dx`);
  - `logo` (slot only: bbox, `logo` = the mark's id);
  - plates are `label_box` with `spans`, `cap_h_large`, `cap_h_small`, `text_bbox` and `baseline_y`.
- Interactive elements carry a `param` hint:
  - voice controls: `bd_level`, `sd_snappy`, `lt_tuning`, ...;
  - voice slide switches: `lc_switch`, `mc_switch`, `hc_switch`, `cl_switch`, `ma_switch`;
  - `mode`, `instrument`, `autofill`, `tempo`, `fine`, `volume`, `variation`, `ifvariation`, `prescale`, `step1`...`step16`, `start`, `tap`, `clear`, `power`.
- `tools/panel/gen_panel.py` of the TB-303 does not know the new types yet. A TR-808 generator needs them added.
