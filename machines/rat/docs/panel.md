# ProCo RAT (1984 "White Face"): the panel

How the Hardware view's pedal was measured. The numbers are in `docs/panel.json` (written by
`tools/panel/measure_panel.py`); `plugin/PanelData.h` is generated from it by
`tools/panel/gen_panel.py`. Everything is in mm, in plan view: origin at the rear-left corner of the
outline (the left cheek's outer face, the cheeks' rear end), x to the right, y towards the player;
angles in degrees from 12 o'clock, clockwise.

## 1. The unit and the photos

The 1984-86 small box ("Version 3-A White Face" in JHS's history): a flat-topped steel U-shell
between two side cheeks, no LED, the RAT logo as a white block with the letters and the Pro Co logo
knocked out of it, DISTORTION / FILTER / VOLUME in a bracketed legend box, a scale arc with seven
ticks round each knob, and the large Pro Co Sound Inc logo on the front face. The 1986 Black Face
and No Brackets units differ only in print.

The photos are sellers' and are reference only (`private/docs/photos/whiteface/`, its
`sources.md`):
- **Top:** `top_orig1984_rv103889534.jpg`, a 1984 original (serial RT-024885; pots dated 1984
  week 31; LM308N), shot almost straight down with an iPhone 16 at 26 mm equivalent
  (f = 3028 px). The camera was about 16 cm over the top, so a part's height shows: a knob's top,
  16 mm up, looks 11 % larger and leans away from the point under the camera.
- **Pots:** `gut_orig1984_rv102019119.jpg`, another 1984 board from below, the three pots' backs
  square-on.
- **Front:** `front_orig1985_rv87589066.jpg`, square-on, the camera about 4 degrees above the face.

## 2. Method

1. **The print's plane.** Sub-pixel points along the strokes of the legend box, its dividers, the
   RAT box and the white block's edges, and along the three scale arcs (rays from each centre, one
   narrow stroke near the radius on the black plate). The camera's pose is fitted with its focal
   length from EXIF so that, on the plane, the rectangles' edges are straight and square and the
   arcs are circles: 2.46 degrees of tilt, residual 0.85 plane units (0.045 mm); the three arcs'
   radii agree to 0.15 %.
2. **Scale.** Two sources, 2.4 % apart; the scale is their mean, so the panel's overall size is
   good to about 1.2 %:
   - the **pot pitch**: the knobs sit on the pots' shafts, at the arcs' centres. On the gut photo
     the outer pots' centres are 2 x 1.143 body diameters apart, and CTS 450 bodies are 15/16 in
     (23.81 mm): a pitch of 27.22 mm;
   - the **leaflet** that came with the small box: "2 3/8"H x 4 1/16* x 3 1/8"D", read as a
     4 1/16 in (103.2 mm) cheek length, the depth ProCo still gives for the RAT2, with the cheek
     tops taken as 1.5 mm (+-1.5) over the print. Its 3 1/8 in can't be the width (it would make
     the plan 1.30 long for 1 wide; the photo's is 1.12), so the line isn't taken at its word.
3. **On the plane.** The photo is resampled onto the print's plane, one plane unit (0.053 mm) a
   pixel, and measured there: strokes as full widths at half maximum, the legends' ink boxes, the
   ticks' angles (centroids across a ring just inside the arc), the logo boxes (the black parts in
   the white block), the cheeks, gaps and plate edges (profiles across them).
4. **Parts above the print** are moved back towards the point under the camera by their height:
   - **knobs:** the flat tops' lean against their shafts gives their height (16.4 mm; the three
     knobs give 18.7, 14.4 and 16.0); the skirt is read on its side facing the camera, where its
     bottom edge shows;
   - **footswitch:** the chrome bezel is taken as 2 mm high; the plunger's top leans 22.5 mm up.
5. **Front face** (front photo): its scale is the cheeks' outer faces, 92.98 mm apart as in the top
   view; its top is the plate's front bend, its bottom the base's edge.

`--overlay OUT.png` draws every measured element over the rectified photo.

## 3. What was measured

| | |
|---|---|
| Outline (the cheeks' top edges) | 92.98 x 104.40 mm (aspect 1.123) |
| Top plate | x 3.62-89.11, y 2.33-102.13; gaps 1.6 mm wide beside it |
| Legend box | 6.73-86.52 x 6.17-16.10 (stroke centres), stroke 0.48, dividers at 33.19 and 59.98 |
| Legends | Helvetica Bold capitals, 3.2 mm cap height, each fitted to its ink box |
| Scales | centres (19.99, 30.48), (46.95, 30.54), (73.81, 30.50); r 12.69, stroke 0.48; arc -135..135; ticks at 0, +-45, +-90, +-135 (within 1.5 degrees), 0.53 wide, running in under the knob |
| Knobs | skirt 18.05, flat top 17.44, 16.4 high; pointer 3.2 to 7.4 mm from the centre, 0.65 wide; travel +-150 (CTS 450: 300 degrees) |
| RAT box | 14.05-80.33 x 51.34-68.13, stroke 0.58; white block 15.61-78.85 x 52.88-66.47 |
| Marks (artwork places) | RAT letters 33.21-77.87 x 54.15-65.25; Pro Co logo 16.82-30.20 x 53.57-65.09; front logo x 31.50-61.61, 13.19-37.53 mm down the face |
| Footswitch | centre (45.70, 83.72); bezel 16.9; plunger 10.7 |
| Front face | 47.3 mm from the plate's bend to the base |

Colours are the photo's (soft light from above): plate #0e0d0c, block #c5c6c0, knob top #252525.

## 4. Accuracy and open points

- Print positions relative to each other: about 0.05 mm. The panel's overall size: +-1.2 % (the
  scale's two sources).
- Knob, footswitch and front-face sizes: +-0.5 to 2 mm (edges of 3-D parts, assumed heights).
- One unit: the print's placement varies from unit to unit (screen printing; the White Face's
  white ink smudged, which is why the 1986 logo changed).
- The cheeks' height over the print and the front face's height are from photos at an angle, not
  measured on a unit.

## 5. Regenerating

    ../../.venv/bin/python -I tools/panel/measure_panel.py [--overlay OUT.png] [--rect OUT.png]   # -> docs/panel.json
    ../../.venv/bin/python -I tools/panel/gen_panel.py                                            # -> plugin/PanelData.h

The brand marks are not in the repo. A user's own files fill the places (`artwork/rat/model`,
`maker` and `front`, SVG or PNG); a private set cut from the same photos is made by
`private/tools/panel/make_places.py`.
