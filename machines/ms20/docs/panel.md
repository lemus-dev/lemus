# Korg MS-20: front panel layout spec

This spec is for drawing the MS-20's front in the plug-in's hardware view. The machine-readable
version is `docs/panel.json` (457 elements in mm). This document says how it was measured and adds
what the JSON cannot say well: materials, 3-D shapes, fonts and what is still uncertain.

Target: the original MS-20 (1978), Rev 1 with two KORG35 filters (KLM-127, KLM-128C, KLM-129), as
the service manual draws it. The photographed unit has the same print as the owner's manual
drawing and the other photos.

## 1. Sources and method

Photos are reference only and live in `private/ms20/docs/photos/` (list, authors, licences and
SHA1 in its `SOURCES.md`).

- **Master photo:** `Korg_Synthesizer_MS-20.jpg` (Wilfredor, CC0, 7875 x 5296). A monochrome
  studio shot square to the panel: Nikon Z 7II, 28.5 mm, f/8, about 9.8 px/mm on the panel. The
  camera stood 669 mm from the panel face, opposite (296, 167) mm in the panel frame.
- **Rectification** (`rectify_fit.py`). One model maps the photo onto the panel plane: radial lens
  distortion about the image centre (k1 = 0.035, in units of the focal length), then a homography.
  It is fitted to the panel's own print only:
  - 24 long printed rules and flow lines must come out straight and exactly horizontal or vertical
    (3,336 sub-pixel samples);
  - the outer ends of 236 dial ticks (36 dials) must lie on circles round their dial centres.
  - Result: lines straight to 0.03 mm rms, tick ends on their circles to 0.15 mm rms. A second
    distortion term changes nothing (it trades with k1).
- **Scale** (`scale.py`). The service manual's overall width, **569 mm** (section 1, item 15:
  "569(W) x 308(D) x 249(H) mm"), is the distance between the side panels' outer faces, which the
  master shows as clean silhouette edges (0.1-0.2 mm sd along their length). The side panels'
  front edges stand a little proud of the panel face; the camera model (EXIF focal length and
  pixel pitch, the fitted homography) maps them at 2.5 mm height. Flush edges would give
  571.1 mm, 5 mm proud 566.9 mm: the height uncertainty is +-0.4 % of scale.
- **Scale cross-checks.** All agree with the 569 mm scale within 0.3 %:
  - **KLM-128C front view** (1994 TIFF, 300 dpi). 34 of the 35 jacks are drawn rings on this
    board. Fitted to the photo's jacks by a similarity: 0.22 mm rms, 0.42 mm max. The drawing comes
    out at 11.84 px/mm against the legends centred on the jacks, and 11.81-11.89 px/mm against the
    openings for 0-4 mm of opening height. 11.811 px/mm would be a 1:1 drawing at 300 dpi, which
    puts the width at 569-571 mm.
  - **KLM-127 front view.** The 30 pots and rotary switches are fitted by a similarity: 0.39 mm
    rms, 0.68 mm max. The drawing is at 77.7 % (9.18 px/mm), so it says nothing about absolute size.
    It also places the eight trimmers under the panel's plugs (0.4-3 mm) and the two LEDs (2 mm).
  - **Design grid.** Knob columns are 37.91 mm apart (VCO MIXER to EG 2, 0.09 mm rms), the ESP
    knobs 29.91 mm and the EG rows 30.04 mm. A metric 38/30 mm grid would mean the scale is 0.1-0.3 %
    low.
  - **Keyboard.** The 22 white keys at a standard 23.571 mm pitch (6.5 in octave) fill the
    opening between the keyboard cheeks with 1-5 mm to spare. This is a weak check, about 1 %.
- **Measuring.** The photo was resampled to 10 px/mm in the panel frame (`rectify_image.py`) and
  measured with Python, OpenCV, SciPy and scikit-image:
  - print mask (ink 55 grey levels above the local panel level) and connected components for
    legends, glyphs, boxes and lines;
  - sub-pixel half-maximum ink boxes for legends;
  - FWHM profiles for stroke widths;
  - per-side line fits for boxes;
  - skeleton tracing (Douglas-Peucker 0.12 mm) for flow lines, rules and symbols;
  - angular ink profiles for the dial ticks;
  - circle and hexagon fits for jacks, screws, plugs and LEDs.
- **Raised parts.** Parts above the panel face are mapped at their height through the camera
  model: jack openings 1.8 mm, nuts 1.2 mm, screws 2 mm. Without that, the jacks at the right of
  the panel would sit 1 mm too far right. The jack-opening height is set by the legends centred on
  the jacks. Knob centres are the printed tick rings' centres (print, no parallax), not the knob
  bodies.
- **Overlay check** (`overlay.py`). `photos/tools/panel_overlay_check.jpg` draws panel.json back
  on the rectified photo:
  - print, boxes, flow lines, ticks, numerals, jacks and hardware sit on the photo;
  - the drawn ticks (design angles) miss the photo's by 0.08 mm rms, both tangential and at the
    outer end (358 ticks);
  - knob legends are centred on their knobs to 0.23 mm rms;
  - the keyboard, lifted back onto the key plane and projected through the camera, sits on the
    keys; key gaps are within 0.41 mm rms of a regular grid.
- **Regenerating.** `tools/panel/build_panel.sh` runs the whole chain from the photos to
  panel.json and the overlay (16 s). Its intermediate measurements go to the private
  `photos/tools/`.

## 2. Dimensions and frame

The frame is a **front view**: the panel face seen straight-on, with the keyboard below it in
orthographic projection onto the panel plane (section 9).

| | Value |
|---|---|
| **Outline** | **569 x 297.4 mm** (width: service manual; height: top cover edge to the bottom rail's front edge in this view) |
| Origin | Top-left: x = 0 on the left side panel's outer face, y = 0 on the top cover's far edge as seen straight-on. **x** right, **y** down the panel face. mm. |
| Normalised | `nx = x/569`, `ny = y/297.4` |
| Unit | 569 W x 308 D x 249 H mm, 7.7 kg (service manual) |
| Panel lean | The key plane is at 60.9 deg to the panel face, so the panel leans back about 61 deg from the keys. |

**Design grid** (knob centres, +-0.3 mm):

| | Value |
|---|---|
| Knob rows | y 52.9 (VCO wave, large) / 53.5 (top row), 88.5, 92.4, 96.6, 118.6, 131.4 (SCALE, large), 148.5, 178.7 |
| Knob columns | VCO 1 37.5, VCO 2 79.8, MIXER/FM 119.9, HPF 158.0, LPF 196.0, MG 233.7, EG 1 271.6, EG 2 309.6 (pitch 37.91) |
| ESP knobs | x 348.0, 377.8, 407.8, 437.6, 467.7 (pitch 29.91), y 178.5 |
| VOLUME | 504.3, 48.6 |
| Jack rows | y 57.4 (six inputs), 83.6, 103.4, 123.4, 151.2 (ESP); SIGNAL OUT 37.1; VCO 2 CV IN 93.4 |

## 3. Physical structure (top to bottom, in the front view)

| Part | Where (mm) | Notes |
|---|---|---|
| Top cover | y 0-20.3, between x 14.0 and 556.1 | Black steel cover seen end-on above the panel. Its top corners are chamfered at about 42 deg, from (14.0, 16.9) to (32.6, 0) and from (537.3, 0) to (556.1, 17.0). |
| Side panels | x 0-11.1 and 558.1-569, from y 16.9 | Black, finely textured. Their front faces stand about 2.5 mm proud of the panel face (estimate). They continue down round the keyboard as its cheeks. |
| Front panel | x 11.95-556.63, y 20.26-200.50 | Painted metal plate with white screen print. Its edges are straight to 0.06-0.18 mm. Five chrome screws hold it (section 8). |
| Lower lip | y 200.5-211.0 | The case's dark lip over the keys. The keys go in under its lower edge at y 211.0. |
| Keyboard | x 42.2-559.8, y 211.0-286.9 | 37 keys, C to C, orthographic view of the key plane (section 9). |
| Left cheek | x 0-41.2, y 211-297.4 | Holds the CONTROL WHEEL and the MOMENTARY SWITCH (section 9). |
| Right cheek | x 560.8-569 | |
| Bottom rail | y 286.9-297.4 | In front of the keys. Its front edge (y 297.4) is projected from the key plane, +-3 mm. |

The top cover's upper surface carries four black rubber pads (visible in `Korg_MS-20.jpg`). They
are not seen in the front view.

## 4. Colour palette

The master is monochrome, so the colours come from the colour photos (`colours.py`). Render
values are white-balanced suggestions; the photo values are medians or percentiles.

- The SMEM photo is in daylight.
- Zinnmann's `Korg_MS-20.jpg` is in warm light.
- The master is lit from above: the panel's grey falls from 96 at the top to 46 at the bottom.
  Draw the panel flat.

| Key | Render hex | Photo | Use |
|---|---|---|---|
| `panel_paint` | `#30333B` | SMEM `#282A30` / `#3E4453` / `#363A46` | Panel ground: dark blue-grey, satin |
| `print_white` | `#EEF0F2` | SMEM highlights `#DAE2F1`-`#FCFCFF` | All panel print (slightly cool white) |
| `logo_grey` | `#B8BBC2` | SMEM `#B4B8C1` | The KORG logotype is light grey, not white. Only for a blank logo place. |
| `top_cover` / `side_panel` / `lip_black` | `#1E1F23` / `#26272C` / `#202126` | side panel SMEM `#28282F` | Case parts |
| `knob_black` / `pointer_white` | `#1A1A1F` / `#E8E8E6` | SMEM `#26262D`, `#151616` | Knobs and their pointer lines |
| `nut_chrome` / `hole_black` | `#B9BCC2` / `#050505` | highlights `#E0DFE0`, mid `#42495A` | Jack nuts and bushings, openings |
| `plug_black` / `screw_chrome` | `#18181A` / `#8A8A8E` | screw SMEM `#48484A` (shaded) | Trimmer plugs, panel screws |
| `led_off` / `led_on` | `#2A0D0E` / `#FF2A1E` | near black in both colour photos | Red LEDs (recessed); lit: add a glow |
| `key_white` / `key_black` | `#F0ECE2` / `#0F1011` | SMEM `#F2ECE0` / `#0E0F10` | Keys (white slightly warm; black glossy) |
| `cheek_black` / `wheel_black` / `button_black` / `slot_black` | `#121212` / `#2C2B2A` / `#151515` / `#080808` | wheel SMEM `#2E2C2B` | Keyboard cheeks, wheel, switch, wheel opening |
| `case_black` | `#141416` | | Silhouette, bottom rail |

## 5. Typography

**Typeface.** All panel print is one sans-serif grotesque in white caps, Helvetica-like and
condensed:
- G without a spur;
- straight-legged R;
- 1 with a flag;
- generous letter-spacing.

`font_fit.py` predicts each one-word legend's ink width from candidate fonts at the measured cap
height, fitting a horizontal scale and a tracking (69 words):

| Font | rms | x-scale | tracking |
|---|---|---|---|
| Helvetica / Arial | 0.37 mm | 0.65 | 0.37 mm |
| **Arial Narrow** | 0.37 mm | **0.78** | 0.36 mm |
| Arial Narrow, no tracking | 0.46 mm | 0.95 | none |
| DIN Alternate | 0.39 mm | 0.76 | 0.37 mm |
| Futura, Gill Sans | 0.61-0.63 mm | | |

So the print is a Helvetica Condensed-like face, a little narrower still, with about 0.36 mm of
tracking.

| Font key | Used for | Cap height | Stroke (FWHM) |
|---|---|---|---|
| `panel_title` | Section titles (VOLTAGE CONTROLLED / OSCILLATOR 1 ..., EXTERNAL SIGNAL PROCESSOR) | 3.0 mm | 0.42 mm (14 %) |
| `panel_legend` | Control labels, sub-titles (FREQUENCY MODULATION, PORTAMENTO, MASTER TUNE), jack names, voltage ranges | 2.2 mm | 0.30 mm |
| dial numerals | Numerals round the dials (in each knob's `scale`) | 1.8 mm (small dials), 2.2 mm (large) | 0.30 mm |
| `panel_box` | Text in the patch panel's boxes, left-aligned 1.6-2.2 mm inside the box line | 2.15 mm | 0.30 mm |
| `panel_small` | Two-line level marks (+5V / 0V, +2.5V / 2.5V) and GND | 1.9 mm | 0.28 mm |
| `panel_name` | SYNTHESIZER under the logo | 6.3 mm | 0.78 mm |

**Substitute.**
- Liberation Sans Narrow Regular (SIL OFL 1.1, metric-compatible with Arial Narrow), or Roboto
  Condensed (Apache 2.0).
- Neither is in the repo. `third_party/fonts` has only IBM Plex Mono and JetBrains Mono, and
  `machines/virusc/fonts` has Michroma. None of these is close.
- Fit every legend to its measured bbox width.

**Layout.**
- Section titles are left-aligned in their section: 1-2 mm right of the rule in the VCO and filter
  sections, about 8 mm in MG, EG 1 and EG 2. VCO MIXER and VOLTAGE CONTROLLED AMPLIFIER are
  centred in their sections.
- Control labels are centred under their knob (0.23 mm rms).
- Two-line jack legends (VCO 1+2 / CV IN, KBD / CV OUT, CONTROL / IN PUT, EG 1 / TRIG IN, F∝V /
  CV OUT) are left-aligned. "IN PUT" is printed with a space.

## 6. Knobs, dials and rotary switches

**Knobs.** 36 controls in two sizes. Sizes are estimates from the photos, +-2 mm.

| Kind | Used on | Base (skirt) | Top | Height |
|---|---|---|---|---|
| Small | 31 pots | 17.4 mm | 15.5 mm | 15 mm |
| Large | VCO 1/2 WAVE FORM and SCALE, both CUTOFF FREQUENCY, VOLUME | 23.6 mm | 18.5 mm | 18 mm |

- Both are black plastic, with a fluted top part on a wider flared skirt.
- A white pointer line runs from near the centre to the edge of the top.
- They are tall. A straight-on render should show the side a little, as the photo does: the
  camera's offset moves the knob tops by up to 8 mm in the master.

**Dial scales** (measured on all 36; angles clockwise from 12 o'clock):

| Scale | Ticks | Tick radius in-out | Numerals |
|---|---|---|---|
| 0-10, small dial (25 dials) | 11 at -150 ... +150, every 30 deg | 9.45-11.29 mm | 0 ... 10 at r 12.5-13.0, cap 1.8 |
| 0-10, large dial (HPF/LPF CUTOFF) | the same | 13.47-15.57 | at r 17.1-17.7, cap 2.2 |
| -5 ... +5 (VCO 2 PITCH, MASTER TUNE) | 11 | 9.45-11.37 | -5 ... 0 ... +5, 0 at 12 o'clock, r 12.8-14.2 |
| VOLUME | 1 at -150 ... 10 at +120, plus a tick at 180 for POWER OFF | 13.47-15.52 | 1 ... 10 at r 16.9-17.7, cap 2.1; "POWER OFF" under the 180 deg tick |
| PW (VCO 1) | 11 ticks, no numerals | 9.45-11.42 | symbols: pulse at 0, wide pulse bottom-left, flat line bottom-right |
| MG WAVE FORM | 11 ticks, no numerals | 9.45-11.23 | symbols: square over triangle at 0; pulse-and-ramp at bottom-left and bottom-right |
| Rotary switches | 4 ticks at -45, -15, +15, +45 | 13.47-15.6 | the detent legends below |

- Ticks are 0.26 mm wide.
- The measured tick angles sit on the design angles to 0.39 deg rms.
- `numeral_layouts` in the JSON gives the mean polar position of every numeral per scale. Per-dial
  measured positions are in each knob's `scale.numerals`.
- Two numerals touch a rule or were missed (PORTAMENTO). They take the layout position, marked
  `source: layout`.

**Rotary switches.** 4 positions each, detents at the four ticks (-45, -15, +15, +45 deg), listed in
the plugin's choice order. The switches are KLM-127 SW1-SW4.

| Switch | Detent legends |
|---|---|
| `vco1_wave` | triangle, sawtooth, pulse "⊓ ↔ narrow pulse" (PW), white noise |
| `vco2_wave` | sawtooth, square, narrow pulse, RING |
| `vco1_scale` | 32' 16' 8' 4' |
| `vco2_scale` | 16' 8' 4' 2' |

**Knob table:**
- the ids are the plugin's parameter ids;
- centres are in the JSON, and section 2 lists the grid;
- each knob names its legend element (`legend`);
- `source` gives its KLM-127 part and residual.

## 7. Patch panel: the 35 jacks

All 35 are 1/4 in phone jacks with chrome hex nuts:
- opening Ø 7.0 mm;
- nut 13.0 mm across flats (median of 34; sd 0.4);
- nut rotation varies jack to jack and is kept per jack (`nut_rot_deg`).

Ids follow `docs/circuit/README.md`'s interface names; J numbers are the service manual's.

| Jack | J | Panel name | In/out | x, y | Print |
|---|---|---|---|---|---|
| `totalext` | J4 | TOTAL (EXT MODULATION) | in | 343.45, 57.55 | TOTAL / -5V~+5V |
| `extfreq` | J6 | VCO 1+2 EXT FREQ (FREQ) | in | 372.31, 57.55 | FREQ / -5V~+5V |
| `extsig` | J13 | EXT SIGNAL IN | in | 395.39, 57.44 | EXT SIGNAL IN / 3VPP MAX |
| `hpfext` | J14 | HPF CUTOFF FREQ | in | 418.34, 57.30 | CUTOFF FREQ / -5V~+5V |
| `lpfext` | J21 | LPF CUTOFF FREQ | in | 443.35, 57.47 | CUTOFF FREQ / -5V~+5V |
| `initgain` | J22 | VCA INITIAL GAIN | in | 469.59, 57.21 | INITIAL GAIN / 0V~+5V |
| `sigout` | J35 | SIGNAL OUT | out | 537.12, 37.10 | SIGNAL OUT (and a → arrow from VOLUME) |
| `phones` | J34 | PHONES | out | 537.08, 57.09 | PHONES |
| `mg` | J3 | MG OUT (triangle) | out | 343.58, 83.88 | OUT; triangle waves and +2.5V / 2.5V level mark below |
| `mgsq` | J7 | MG OUT (rectangle) | out | 383.55, 83.71 | OUT; rectangle waves and +5V / 0V below |
| `eg1out` | J12 | EG 1 OUT | out | 403.36, 83.51 | OUT; +5V / 0V and an envelope shape beside it |
| `eg2rev` | J23 | EG 2 REV OUT | out | 480.52, 83.56 | REV OUT; 0V and an inverted envelope below |
| `vco2cv` | J24 | VCO 2 CV IN | in | 497.69, 93.37 | VCO 2 / CV IN; "VCO 2 ←" |
| `vcocv` | J26 | VCO 1+2 CV IN | in | 516.89, 83.03 | VCO 1+2 / CV IN; "VCO 1 ←" |
| `kbdcv` | J33 | KBD CV OUT | out | 536.91, 83.02 | KBD / CV OUT |
| `shclk` | J5 | S&H CLOCK | in | 363.30, 103.66 | CLOCK; ground mark, GND |
| `eg1rev` | J11 | EG 1 REV OUT | out | 403.23, 103.62 | REV OUT; +5V / 0V and an inverted envelope |
| `mvcactl` | J15 | MVCA CONTROL IN PUT | in | 423.43, 103.50 | CONTROL / IN PUT; 0V~+5V |
| `eg1trig` | J20 | EG 1 TRIG IN | in | 454.61, 103.47 | EG 1 / TRIG IN; ground mark, GND |
| `trig` | J27 | EG 1+2 TRIG IN | in | 516.83, 103.13 | TRIG IN (beside the KBD box); ground mark, GND |
| `trigout` | J32 | KBD TRIG OUT | out | 536.73, 102.94 | TRIG OUT; ground mark, GND |
| `shin` | J2 | S&H IN | in | 343.55, 123.47 | IN |
| `shout` | J8 | S&H OUT | out | 383.46, 123.68 | OUT |
| `mvcain` | J10 | MVCA IN | in | 403.12, 123.58 | IN |
| `mvcaout` | J16 | MVCA OUT | out | 434.36, 123.56 | OUT |
| `pink` | J19 | PINK NOISE | out | 454.51, 123.56 | PINK and a squiggle |
| `white` | J25 | WHITE NOISE | out | 494.58, 123.14 | WHITE and a squiggle |
| `wheel` | J28 | CONTROL WHEEL | out | 516.41, 123.28 | wheel symbol (an arc); -5V~+5V |
| `mom` | J31 | MOMENTARY SWITCH | out | 536.75, 123.17 | push-button symbol; ground mark, GND |
| `espin` | J1 | ESP SIGNAL IN | in | 343.22, 151.42 | SIGNAL IN |
| `espamp` | J9 | ESP AMP OUT | out | 383.27, 151.17 | OUT |
| `espfilt` | J17 | ESP BAND PASS OUT | out | 423.35, 151.36 | OUT |
| `espfv` | J18 | ESP F-V CV OUT | out | 463.57, 151.19 | F∝V / CV OUT |
| `espenv` | J29 | ESP ENV OUT | out | 516.60, 151.13 | ENV OUT |
| `esptrig` | J30 | ESP TRIG OUT | out | 536.61, 150.67 | TRIG OUT; ground mark, GND |

The -5V~+5V printed between WHITE and the wheel jack belongs to the CONTROL WHEEL jack (the spec
gives the wheel output as -5 V ... 0 ... +5 V; white noise is 5 Vp-p).

**Against `core/patch.h`:**
- patch.h has 33 jacks. The panel has two more:
  - **J12 EG 1 OUT** (`eg1out`): a real patch output, already in the circuit (`eg1out`,
    README's interface table) but missing from patch.h;
  - **J34 PHONES**: the headphone amp, not a patch point.
- patch.h has nothing the panel lacks.
- Each jack carries `in_patch_h`.

KLM-128C draws 34 jack rings. SIGNAL OUT (J35) sits in a notch at the board's edge and has no
drawn ring.

## 8. Printed graphics

**Section rules and frames** (20 `line` elements, `kind: rule`):
- Vertical rules between the sections: 0.57-0.71 mm.
- A heavier frame, 1.05-1.12 mm, runs round the VCA/MG/EG 1 block. It goes from the top at
  x 290.5 down to y 82.9, left to x 214.8, down to y 118.1, left to x 101.0, then down to the bottom.
  The heavy rule x 328.2 separates the patch panel, and the heavy rule y 133.5 (x 330.8-552.8)
  separates the ESP.
- Rules are straight to 0.1 mm.
- Where knobs, legends or LEDs interrupt them, the print is broken. The KBD trigger LED sits in a
  gap of the EG 1 / EG 2 rule at x 290.4.

**Patch panel flow diagram:**
- 15 square-cornered boxes (`outline_box`, stroke 0.47 mm):
  - VCO 1, VCO 2, VCO MIXER, HP FILTER, LP FILTER, AMPLIFIER;
  - MODULATION GENERATOR, ENVELOPE GENERATOR 1 and 2, KBD, SAMPLE & HOLD, NOISE GENERATOR;
  - BAND PASS FILTER, F-V CONVERTER, ENVELOPE FOLLOWER.
- Two amplifier triangles (VCA and AMP, stroke 0.46). The VCA section's large triangle is
  0.31 mm.
- 61 flow lines (`kind: flow`, 0.32 mm). Corners are rounded in the print; the polylines keep them.
- 26 arrowheads (filled triangles, about 1.9 mm).

**Symbols** (28 `symbol` elements, stroke centre polylines, 0.4 mm):
- VCO wave shapes and the PW and MG WAVE FORM scale symbols;
- the MG output wave trains with their level marks;
- the envelope shapes beside EG 1 OUT, EG 1 REV OUT and EG 2 REV OUT;
- the PINK and WHITE squiggles;
- the wheel arc and the push button over J28 and J31;
- six ground marks.

**Legends.** All 167 are in the JSON with:
- ink bbox, cap height, stroke and alignment;
- the text as printed (double spaces mark the print's wide word gaps).

The VCA section's own legend is "EG 2 & EXT".

## 9. Keyboard, wheel and switch

**Key plane** (`measure_keys.py`).
- The 21 gaps between the 22 white keys are traced on 38 photo rows. As 3-D lines they meet at a
  vanishing point that gives the keys' direction, so the white key tops are a plane at
  **60.9 deg** to the panel face.
- The plane's distance is set by the standard full-size pitch, **165/7 = 23.571 mm** (6.5 in
  octave). The photo cannot size the keys by itself; the fit to the keyboard opening (section 1)
  agrees to about 1 %.
- In the view the keys are an **orthographic projection** onto the panel plane. Lengths along the
  keys shrink by 0.487.

| | True (key plane) | Front view |
|---|---|---|
| White key pitch | 23.571 mm (gaps 0.41 mm rms off a regular grid) | x 42.2 + 23.571 n; 1 mm gaps |
| White key visible top | 141.8 mm from under the lip to the front edge | y 211.0-282.6 |
| White key front face | 4.9 mm seen above the rail | y 282.6-286.9 |
| Black keys | 12.7 mm wide at the base (median; single keys 9.6-15.3: the measuring row crosses their sloped front face), visible length 94 mm | y 211.0-255.7 (front base); their top front edge about 9.6 mm higher in the view (black_rise 11 mm, estimate) |
| Black key centres | measured per type over the gap they sit over | in the JSON |

- 37 keys: key 0 = C2 ... key 36 = C5, the plugin's MIDI 36-72.
- Black keys are drawn over the white keys.

**Left cheek** (x 0-41.2). It holds:
- **CONTROL WHEEL** (`control_wheel`): a black ribbed wheel in an opening running front to back,
  about 11-12 mm wide. Centre click. It drives J28, and pitch bend in the plugin.
- **MOMENTARY SWITCH** (`momentary_switch`): a black push button in a chrome ring, Ø 10 mm, in front
  of the wheel. It drives J31.

The cheek's top is not in the key plane and its exact tilt could not be fitted from the master.
Hand-picked photo points mapped onto the key plane give their places to about +-5 mm. The SMEM
photo shows them best.

## 10. LEDs, plugs and screws

| Part | Where | Notes |
|---|---|---|
| MG rate LED | 233.98, 118.71 | KLM-127 LED-1; pulses at the MG rate. No label. |
| KBD trigger LED | 290.58, 98.85 | KLM-127 LED 2, in a gap of the EG 1 / EG 2 rule. Lit while a key is down. No label. |
| ESP PEAK LED | 489.74, 138.53 | Label "PEAK" under it. |
| ESP TRIG LED | 535.88, 138.18 | Above TRIG OUT. |
| Trimmer plugs (8) | VCO 1 PW 52.5, 107.8; VCO 2 PITCH 94.6, 107.7; VCO 1 SCALE 52.3, 148.9; VCO 2 SCALE 94.7, 148.4; PORTAMENTO 52.7, 189.4; MASTER TUNE 95.0, 190.1; HPF 145.2, 79.9; LPF 182.8, 80.6 | Black plastic plugs, Ø 5.1, over KLM-127 VR1-VR8 |
| Screws (5) | 17.6, 41.0; 18.1, 181.4; 252.8, 33.4 (VCA section); 551.1, 41.2; 550.5, 181.4 | Chrome pan-head, Ø 6 |

- The LEDs are red, about 3 mm, sunk in panel holes. They look dark when off.

## 11. Artwork places (brand marks are not drawn)

| Place | Element | Box (mm) | What |
|---|---|---|---|
| `maker` | `logo_maker` | [491.62, 168.02, 530.61, 180.33] (39.0 x 12.3) | The KORG logotype, printed light grey at the bottom right of the panel. The box is its ink box. `lockup_bbox` [491.62, 168.02, 538.35, 189.31] also covers the word SYNTHESIZER under it. |
| `name` | none | | The front shows no MS-20 model name. Check the rear panel, which no photo here shows. |

SYNTHESIZER (`l_synthesizer`, cap 6.3 mm) is a generic word in the panel's grotesque, not a mark.
It is drawn as text; artwork that includes it replaces it.

## 12. Accuracy

| What | Accuracy |
|---|---|
| Knob centres | +-0.3 mm (0.39 mm rms vs KLM-127 by a similarity, max 0.68) |
| Jack centres | +-0.25 mm (0.22 mm rms vs KLM-128C, max 0.42) |
| Printed legends, boxes, lines, ticks, numerals | +-0.2 mm relative, +-0.4 mm absolute |
| Scale | +-0.4 % (569 mm spec; side-panel height). The cross-checks lean +0.1 to +0.3 %. |
| Plate, lip, side-panel edges | +-0.3 mm |
| Screws, plugs, LEDs | +-0.5 mm |
| Keyboard x | +-1.5 mm |
| Key lengths and fronts | +-3 mm (standard pitch assumed) |
| Left cheek, wheel, switch | +-5 mm |
| 3-D sizes (knob heights and tops, black-key rise, side-panel proudness) | estimates, +-2 mm |
| Colours | lighting-dependent starting points |

## 13. Open questions

1. **Scale, 0.3 %.**
   - The 569 mm spec gives 37.91 mm knob columns.
   - Treating KLM-128C as a 1:1 drawing and the knob grid as metric (38/30 mm) both point to a
     scale about 0.25 % larger (570.4 mm). That would be the width if the side panels' front faces
     stood flush.
   - A tape measure on a real unit's width over the side panels, or between two knob shafts,
     would settle it.
   - It moves nothing by more than 0.7 mm.
2. **EG 1 OUT (J12)** is on the panel but not in `core/patch.h`. Adding it is a code change
   outside this spec.
3. **Model name place.** There is no MS-20 print on the front. Is there one on the rear, or on the
   keyboard's front rail?
4. **Left cheek geometry** (wheel opening, switch): the cheek's top surface is neither the key
   plane nor the panel plane, so the places are +-5 mm. A straight photo of the cheek would fix it.
5. **Knob dimensions** (skirt, top, height) are photo estimates. The knobs on the KLM-127 drawing
   are 16.2 mm pot outlines, and the rotary switches 23.1 mm, which fits the knob bases.
6. **Typeface.** The family (Helvetica Condensed-like, tracked) is clear. The exact cut is not, and
   neither substitute font is in the repo yet.
7. **Black keys.** Their rise above the white keys (11 mm) and the front face slant are standard
   values, not measured.
8. **LED colour** when lit: assumed red, as on photos of working units; not in the photos here.
