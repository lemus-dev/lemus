#!/usr/bin/env python3
"""Write docs/panel.json in the Lemus conventions from what measure_panel.py measured.

Usage: lemus_panel.py MEASURED_JSON OUT_JSON [--crosscheck CROSSCHECK_JSON]

MEASURED_JSON is measure_panel.py's output (the elements as measured on the master photo).
CROSSCHECK_JSON (crosscheck.py) adds the cross-check photos, the frame, the accuracy table,
overrides for measured values ("elements": {id: {field: value}}) and new elements ("add").

What this step does, besides copying the measurements:
- Brand marks are slots only: type logo, bbox and the pack id ("logo": roland, tr909,
  rhythm_composer). Nothing here traces them; the editor fills them from the artwork pack.
- Every interactive element gets a param hint: a knob its parameter id (bd_tune ... tempo), a key
  its tr909::Machine::Key enumerator (Step1 ... EnterAccent), as the processor's setKey takes it.
- Keys are type key (shape large or small, the LED they carry, the cut-out); every LED names its
  tr909::Machine::Led enumerator; the 14 lit small buttons get the LED behind their cap.
- The display carries its digit geometry; the paths name the private folder (private/...)."""
import json, sys

W_MM, H_MM = 486.0, 279.49

PRIVATE_PHOTOS = "private/docs/photos/"

# knob element -> parameter id (TR909Processor::knobs(), plus tempo)
KNOB_PARAMS = {
    "knob_accent": "accent", "knob_hh_ch_decay": "ch_decay", "knob_hh_oh_decay": "oh_decay",
    "knob_crash_level": "cr_level", "knob_crash_tune": "cr_tune", "knob_ride_level": "rd_level",
    "knob_ride_tune": "rd_tune", "knob_tempo": "tempo", "knob_volume": "volume",
}

# key element -> tr909::Machine::Key, and the tr909::Machine::Led it carries or that lights it
KEYS = {
    "key_start": ("Start", None), "key_stop_cont": ("StopCont", None),
    "btn_track_play_1": ("Track1", "Track1"), "btn_track_play_2": ("Track2", "Track2"),
    "btn_track_play_3": ("Track3", "Track3"), "btn_track_play_4": ("Track4", "Track4"),
    "btn_pattern_play_1": ("Pattern1", "Pattern1"), "btn_pattern_play_2": ("Pattern2", "Pattern2"),
    "btn_pattern_play_3": ("Pattern3", "Pattern3"), "btn_ext_inst": ("ExtInst", "ExtInst"),
    "btn_tempo_step": ("Tempo", "Tempo"), "btn_back_tap": ("Back", "Back"), "btn_fwd_bank1": ("Fwd", "Fwd"),
    "btn_avail_meas_bank2": ("AvailableMeas", "AvailableMeas"), "btn_cycle_guide": ("Cycle", "Cycle"),
    "btn_tape_sync": ("TapeSync", "TapeSync"), "key_cartridge_enter": ("EnterAccent", "Enter"),
    "btn_last_step": ("LastStep", None), "btn_scale": ("Scale", None), "btn_shuffle_flam": ("ShuffleFlam", None),
    "btn_clear": ("Clear", None), "btn_instrument_select": ("InstSelect", None), "btn_shift": ("Shift", None),
}
for k in range(1, 17):
    KEYS["key_step_%d" % k] = ("Step%d" % k, "Step%d" % k)

# The scale LEDs, top to bottom: PB4 PA0 is the bottom one (D635) and PA3 the top one (D632)
# (switch board, SN p. 8; docs/firmware.md), so Machine::Led::Scale1 is the bottom row.
SCALE_LEDS = {"led_scale_1": ("Scale4", "D632"), "led_scale_2": ("Scale3", "D633"),
              "led_scale_3": ("Scale2", "D634"), "led_scale_4": ("Scale1", "D635")}

# The lit small buttons' LED element ids
BACKLIGHT_IDS = {
    "Track1": "led_track_play_1", "Track2": "led_track_play_2", "Track3": "led_track_play_3", "Track4": "led_track_play_4",
    "Pattern1": "led_pattern_play_1", "Pattern2": "led_pattern_play_2", "Pattern3": "led_pattern_play_3",
    "ExtInst": "led_ext_inst", "Tempo": "led_tempo_step", "Back": "led_back_tap", "Fwd": "led_fwd_bank1",
    "AvailableMeas": "led_avail_meas_bank2", "Cycle": "led_cycle_guide", "TapeSync": "led_tape_sync",
}

LOGO_IDS = {"logo_roland": "roland", "logo_tr909": "tr909", "logo_rhythm_composer": "rhythm_composer"}
LOGO_NOTES = {
    "roland": "brand mark slot, not drawn: the Roland logo (R emblem of three parallel strokes + wordmark) in orange. "
              "Filled from the artwork pack (id roland).",
    "tr909": "brand mark slot, not drawn: the TR-909 logotype (heavy, extended, rounded-square letters) in the dark print. "
             "Filled from the artwork pack (id tr909).",
    "rhythm_composer": "brand mark slot, not drawn: the RHYTHM COMPOSER logotype (wide capitals with cut M, E, S, R) in the "
                       "dark print. Filled from the artwork pack (id rhythm_composer).",
}

CONVENTIONS = {
    "coordinates": "mm from the rear-left corner of the top view (the outer edge of the left side cheek), x to the right, "
                   "y towards the player; nx = x / panel.w, ny = y / panel.h",
    "text": "bbox = ink bounding box (cap height to baseline for caps-only legends); x, y = bbox centre; cap_h = capital "
            "height. Legends printed on a label_box or inside an outline_box are their own text elements.",
    "label_box": "a filled box (header bar, orange box); bbox = its edges; its legend is the text elements inside it",
    "outline_box": "bbox = outer edge of the keyline; points = the keyline's centre line where it isn't a rectangle (the tab "
                   "over the left key); stroke = keyline width; corner_r = corner radius",
    "line": "points = stroke centre line; stroke = width",
    "fill": "a printed area: bbox, corner_r",
    "symbol": "note glyphs in the scale bars: bbox = the glyph's ink box (head at the lower left, stem up from its right "
              "side, one flag on the eighth notes)",
    "knob": "x, y = centre of the printed tick ring (= the pot shaft); knob_d = knob top seen from above; base_d = the skirt; "
            "scale = the printed ticks (angles from 12 o'clock, clockwise positive)",
    "key": "x, y = centre of the panel cut-out (photo parallax removed); bbox = the cut-out for the large keys, the visible "
           "cap for the small buttons; cap_top = the flat top face; led = the id of the LED element in or behind the cap",
    "led": "led = the tr909::Machine::Led it shows (the processor's snapshot leds[]); shape rect (lens in a key top), round "
           "(3 mm LED in a scale bar) or backlight (the centre of a lit small button's frosted window: bbox = that "
           "square, window = the window's side)",
    "display": "bbox = the red filter window; digit = the 7-segment module's geometry; the digits are the processor's "
               "snapshot digits[] (BCD, 10-15 blank), left to right",
    "logo": "a brand-mark slot: bbox only, never traced shapes; logo = the artwork pack's element id",
    "param": "every interactive element carries param: a knob its parameter id (TR909Processor::knobs(), tempo), a key its "
             "tr909::Machine::Key enumerator (Step1 ... EnterAccent), as TR909Processor::setKey takes it",
}


ACCURACY = ("knob centres +-0.1 mm in x, +-0.15 in y (25 pots of the 1:1 voicing-board artwork, SN p. 10: 0.087 / 0.17 mm "
            "rms); printed lines, bars, boxes and legends +-0.15 mm relative (three other photos mapped onto the master), "
            "+-0.3 mm absolute in the 486 mm frame (+-0.5 within 40 mm of the case ends); raised parts as the photos see them "
            "+-0.5 mm (parallax: the file has panel-plane positions); display digits +-0.5 mm; 3-D sizes +-1 mm. "
            "See accuracy_table and docs/panel.md.")


def r2(v):
    return round(float(v), 2)


def finish(e):
    """x, y, w, h, nx, ny from the bbox (or the other way round), rounded."""
    if "bbox" in e and ("x" not in e or "y" not in e):
        x0, y0, x1, y1 = e["bbox"]
        e.setdefault("x", (x0 + x1) / 2)
        e.setdefault("y", (y0 + y1) / 2)
    if "bbox" not in e:
        if "w" in e and "h" in e:
            e["bbox"] = [e["x"] - e["w"] / 2, e["y"] - e["h"] / 2, e["x"] + e["w"] / 2, e["y"] + e["h"] / 2]
        elif "d" in e:
            e["bbox"] = [e["x"] - e["d"] / 2, e["y"] - e["d"] / 2, e["x"] + e["d"] / 2, e["y"] + e["d"] / 2]
    if "bbox" in e:
        e["bbox"] = [r2(v) for v in e["bbox"]]
        if e["type"] not in ("line",):
            e.setdefault("w", e["bbox"][2] - e["bbox"][0])
            e.setdefault("h", e["bbox"][3] - e["bbox"][1])
    for k in ("x", "y", "w", "h", "d"):
        if k in e and e[k] is not None:
            e[k] = r2(e[k])
    if "x" in e and "y" in e:
        e["nx"] = round(e["x"] / W_MM, 4)
        e["ny"] = round(e["y"] / H_MM, 4)
    return e


def photo_path(p):
    for prefix in ("docs/photos/", "private/docs/photos/"):
        if p.startswith(prefix):
            return PRIVATE_PHOTOS + p[len(prefix):]
    return p


def convert_element(e):
    e = dict(e)
    t, i = e["type"], e["id"]
    if t == "logo":
        e["logo"] = LOGO_IDS[i]
        e["note"] = LOGO_NOTES[e["logo"]]
    elif t == "knob":
        e["param"] = KNOB_PARAMS.get(i, i[len("knob_"):])
    elif t in ("button", "key"):
        key, led = KEYS[i]
        style = e.pop("style", None) or e.get("key_style")
        large = style in ("step", "ivory") or e.get("shape") == "large"
        e["type"] = "key"
        e["shape"] = "large" if large else "small"
        e["key_style"] = {"step": "grey", "ivory": "ivory", "small": "smoky"}.get(style, style)
        e["param"] = key
        if large:
            e["cutout_bbox"] = list(e["bbox"])
        if e.pop("illuminated", False) or (led and not large):
            e["led"] = BACKLIGHT_IDS[led]
            e["lit"] = True
        elif led:
            e["led"] = "led_cartridge_enter" if key == "EnterAccent" else "led_step_%s" % key[4:]
    elif t == "led":
        if i in SCALE_LEDS:
            e["led"], d = SCALE_LEDS[i]
            e["note"] = ("3 mm red LED %s set into the left end of the scale bar, lit when its scale is selected "
                         "(Machine::Led::%s, scale %s: the owner's manual, p. 24, numbers the scales from the bottom row "
                         "up, 16th notes, 32nd notes, 8th-note triplets, 16th-note sextuplets)") % (d, e["led"], e["led"][-1])
        elif i.startswith("led_step_"):
            e["led"] = "Step" + i[len("led_step_"):]
            e["in"] = "key_step_" + i[len("led_step_"):]
        elif i == "led_cartridge_enter":
            e["led"] = "Enter"
            e["in"] = "key_cartridge_enter"
    elif t == "display":
        e["note"] = ("red filter window (2222030700) over the 3-digit 7-segment LED D636 (LB-603VK); it shows the tempo "
                     "(37-290) or the measure number")
    return e


def backlights(elements):
    """The LED behind each lit small button (D618-D631), where the cross-check hasn't measured it: the
    centre of the frosted window on its cap (crosscheck.py: window 4.9 mm, centre 3.0 mm)."""
    out = []
    for e in elements:
        if e["type"] == "key" and e.get("lit"):
            key = e["param"]
            out.append(finish({
                "id": BACKLIGHT_IDS[key], "type": "led", "shape": "backlight", "x": e["x"], "y": e["y"], "w": 3.0, "h": 3.0,
                "window": 4.88, "led": key, "in": e["id"], "colour": "small_btn_centre_red", "colour_lit": "led_lit",
                "note": "red LED (D618-D631) behind the frosted centre of the cap: the centre square glows; window = the frosted "
                        "square with its bright rim, as seen on the cap",
            }))
    return out


def main():
    args = sys.argv[1:]
    measured, out = args[0], args[1]
    cross = json.load(open(args[args.index("--crosscheck") + 1])) if "--crosscheck" in args else {}
    d = json.load(open(measured))

    overrides = cross.get("elements", {})
    elements = []
    for e in d["elements"]:
        e = dict(e)
        e.update(overrides.get(e["id"], {}))
        elements.append(finish(convert_element(e)))
    known = {e["id"] for e in elements}
    for e in cross.get("add", []):
        e = finish(convert_element(dict(e)))
        if e["id"] in known:
            elements = [e if x["id"] == e["id"] else x for x in elements]
        else:
            elements.append(e)
            known.add(e["id"])
    for e in backlights(elements):
        if e["id"] not in known:
            elements.append(e)
            known.add(e["id"])

    # the display's digit geometry, from ui_suggestions (or the cross-check)
    ui = [u for u in d.get("ui_suggestions", []) if u["id"] != "ui_display_digits"]
    digits = next((u for u in d.get("ui_suggestions", []) if u["id"] == "ui_display_digits"), None)
    for e in elements:
        g = e.pop("digits_geometry", None)
        if e["type"] == "display" and g:
            e["digit"] = {"digit_h": g["digit_h"], "digit_w": g["digit_w"], "pitch": g["pitch"], "segment_w": g["segment_w"],
                          "slant_deg": g["slant_deg"], "x": g["centres_x"], "y": g["centre_y"], "note": g["note"]}
            if "segment_rows_y" in g:
                e["digit"]["segment_rows_y"] = g["segment_rows_y"]
        if e["type"] == "display" and "digit" not in e and digits:
            e["digit"] = {k: digits[k] for k in ("digit_h", "digit_w", "pitch", "segment_w", "slant_deg", "decimal_points")
                          if k in digits}
            e["digit"]["note"] = digits["note"]

    colours = dict(d["colours"])
    colours.setdefault("small_btn_centre_red", {"hex": "#7E6A62", "note": "centre of the 14 lit-row caps with their LED off: "
                                                                        "tinted by the red LED behind it (master #7E6A62)"})
    # the smoky caps read light grey in daylight (close-up rim #989694) and near black under the studio
    # light (master #3E3C3B); the Commons photos lie between (bdaniel #7A706E)
    colours["small_btn"] = {"hex": "#77726F", "note": "small tact-switch caps (2247029300): smoky translucent grey, "
                            "a frosted lighter square on top. Master #3E3C3B (studio light), close-up rim #989694 "
                            "(daylight), bdaniel #7A706E"}
    colours.setdefault("small_btn_centre_plain", {"hex": "#ADA292", "note": "the frosted centre of the six left-box caps, "
                                                                          "which have no LED behind them (master #ADA292)"})
    colours.update(cross.get("colours", {}))

    frame = {
        "px_per_mm_in_source": d.get("px_per_mm_in_source"),
        "source_corners_px": d.get("source_corners_px"),
        "homography_source_to_master_px": d.get("homography_source_to_master_px"),
        "master_image": "build/tr909-panel/panel/master_reg.png (8 px/mm; tools/panel/build_panel.sh)",
        "regularisation": d.get("regularisation"),
    }
    frame.update(cross.get("frame", {}))

    rear = dict(d.get("rear_panel", {}))
    if "source" in rear:
        rear["source"] = rear["source"].replace("docs/photos/", PRIVATE_PHOTOS)

    sources = [{"path": photo_path(d["source_image"]), "role": "geometry master", "licence": "reference only (c) Soundgas"}]
    sources += cross.get("photos", [])

    fonts = dict(d["fonts"])
    fonts["logos"] = ("Brand marks (roland, tr909, rhythm_composer) are slots only and are never drawn from the photos; "
                      "fill them from the artwork pack (tr909.svg).")

    ui.append({"id": "ui_brand_marks", "type": "note",
               "note": "The logo elements are empty slots: the editor draws the marks from the user's artwork pack "
                       "(tr909.svg in the ROM folder, ids roland, tr909, rhythm_composer) and nothing without one."})

    res = {
        "source_image": photo_path(d["source_image"]),
        "source_note": d["source_note"] + (" Cross-checked on %s, each mapped onto the master by SIFT + RANSAC and dense "
                                           "refinement (tools/panel/crosscheck.py)." % ", ".join(p["path"] for p in cross["photos"])
                                           if cross.get("photos") else ""),
        "sources": sources,
        "units": "mm",
        "origin": d["origin"],
        "frame": frame,
        "panel": d["panel"],
        "design_grid": d["design_grid"],
        "accuracy": cross.get("accuracy_summary", ACCURACY if cross.get("accuracy") else d["accuracy"]),
        "accuracy_table": cross.get("accuracy", []),
        "small_knob_tick_stats": d.get("small_knob_tick_stats"),
        "colours": colours,
        "fonts": fonts,
        "conventions": CONVENTIONS,
        "elements": elements,
        "rear_panel": rear,
        "ui_suggestions": ui,
    }
    with open(out, "w") as f:
        json.dump(res, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print(out, len(elements), "elements")


if __name__ == "__main__":
    main()
