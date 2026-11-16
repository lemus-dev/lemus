#!/usr/bin/env python3
"""docs/panel.json -> plugin/PanelData.h: the measured SH-101 top panel as a C++ table.

Usage: gen_panel.py docs/panel.json plugin/PanelData.h

Each element keeps its type, id, label or function, parameter id, bounding box (mm from the rear-left
corner of the case), colour, a polyline (lines, grooves; a switch's detent positions) and eight
type-specific numbers v[0..7]:
  Text      v0 cap height, v1 1 = left-aligned
  Scale     v0 first tick y, v1 pitch, v2 count, v3 tick width
  Slider    v0 centre x, v1 slot top, v2 slot bottom, v3 value-0 y, v4 value-1 y, v5 cap width, v6 cap height,
            v7 stripe (0 green, 1 orange, 2 yellow)
  VSwitch   v0 centre x, v1 slot top, v2 slot bottom, v5 cap width, v6 cap height, v7 stripe; points: detent y
  HSwitch   v0 lever diameter; points: detent x (left to right); label: parameter item index per position
  Knob      v0 centre x, v1 centre y, v2 diameter, v3 min angle, v4 max angle, v5 tick count, v6 tick r0, v7 tick r1
  Rotary    v0 centre x, v1 centre y, v2 diameter, v6 tick r0, v7 tick r1; points: detent angles (deg, 12 o'clock = 0)
  Arc       v0 centre x, v1 centre y, v2 radius, v3 start angle, v4 end angle, v5 stroke
  Jack      v0 centre x, v1 centre y, v2 diameter, v3 kind (0 phone, 1 mini, 2 mini stereo, 3 DC)
  Ring/Dot  v0 centre x, v1 centre y, v2 radius, v3 stroke
  Arrow     v0 tip x, v1 tip y, v2 direction (deg, 0 = up), v3 size
  Button    v0..v3 cap box
  Key       v0 key number (0 = F3), v1 1 = white
  Bender    v0..v3 lever box
  Region    v0 style (0 shelf, 1 raised, 2 lower, 3 keybed, 4 lid, 5 inset)
  Line, Groove: v0 stroke"""
import json, sys

TYPES = {"case": "Case", "region": "Region", "groove": "Groove", "line": "Line", "text": "Text", "scale": "Scale",
         "jack": "Jack", "ring": "Ring", "dot": "Dot", "arrowhead": "Arrow", "arc": "Arc", "logo": "Logo",
         "slider": "Slider", "vswitch": "VSwitch", "hswitch": "HSwitch", "knob": "Knob", "rotary": "Rotary",
         "button": "Button", "power": "Power", "led": "Led", "key": "Key", "bender": "Bender"}
REGION_STYLES = {"shelf": 0, "raised": 1, "lower": 2, "keybed": 3, "lid": 4, "inset": 5}
STRIPES = {"green": 0, "orange": 1, "yellow": 2}
JACKS = {"phone": 0, "mini": 1, "mini_stereo": 2, "dc": 3}


def argb(colours, key):
    if key is None:
        return "0x00000000"
    return "0xff" + colours[key]["hex"].lstrip("#")[:6].lower()


def cstr(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    src, out = sys.argv[1:3]
    d = json.load(open(src))
    colours = d["colours"]
    rows, points = [], []
    for e in d["elements"]:
        t = TYPES[e["type"]]
        v = [0.0] * 8
        pts = []
        label = e.get("label", e.get("function", e.get("led", "")))
        if t in ("Line", "Groove"):
            pts = [c for p in e["points"] for c in p]
            v[0] = e["stroke"]
            xs, ys = pts[0::2], pts[1::2]
            box = [min(xs), min(ys), max(xs), max(ys)]
        elif t in ("Ring", "Dot"):
            v[:4] = [e["x"], e["y"], e["r"], e.get("stroke", 0)]
            box = [e["x"] - e["r"], e["y"] - e["r"], e["x"] + e["r"], e["y"] + e["r"]]
        elif t == "Arrow":
            v[:4] = [e["x"], e["y"], e["dir_deg"], e["size"]]
            box = [e["x"] - e["size"], e["y"] - e["size"], e["x"] + e["size"], e["y"] + e["size"]]
        elif t == "Arc":
            v[:6] = [e["x"], e["y"], e["r"], e["start_deg"], e["end_deg"], e["stroke"]]
            box = [e["x"] - e["r"], e["y"] - e["r"], e["x"] + e["r"], e["y"] + e["r"]]
        else:
            box = e["bbox"]
        if t == "Text":
            v[0] = e["cap_h"]
            v[1] = 1.0 if e.get("align") == "left" else 0.0
        elif t == "Scale":
            v[:4] = [e["y0"], e["pitch"], e["n"], e["tick_w"]]
        elif t == "Slider":
            v = [e["x"], e["slot"][1], e["slot"][3], e["travel"][0], e["travel"][1], e["cap_w"], e["cap_h"], STRIPES[e["stripe"]]]
        elif t == "VSwitch":
            v = [e["x"], e["slot"][1], e["slot"][3], 0, 0, e["cap_w"], e["cap_h"], STRIPES[e["stripe"]]]
            pts = [c for y in e["positions"] for c in (e["x"], y)]
        elif t == "HSwitch":
            v[0] = e["lever_d"]
            pts = [c for x in e["positions"] for c in (x, 0.5 * (box[1] + box[3]))]
            label = ",".join(str(i) for i in e["order"])
        elif t == "Knob":
            v = [e["x"], e["y"], e["d"], e["min_deg"], e["max_deg"], e.get("ticks", 0), *(e.get("tick_r") or [0, 0])]
        elif t == "Rotary":
            v = [e["x"], e["y"], e["d"], 0, 0, 0, *e["tick_r"]]
            pts = [c for a in e["detents"] for c in (a, 0)]
        elif t == "Jack":
            v[:4] = [e["x"], e["y"], e["d"], JACKS[e["kind"]]]
        elif t == "Button":
            v[:4] = e["cap"]
        elif t == "Key":
            v[:2] = [e["key"], 1.0 if e["white"] else 0.0]
        elif t == "Bender":
            v[:4] = e["lever"]
        elif t == "Region":
            v[0] = REGION_STYLES[e["style"]]
        p0, pn = len(points) // 2, len(pts) // 2
        points += pts
        rows.append("\t{PType::%s, %s, %s, %s, {%s}, %s, {%s}, %d, %d}," % (
            t, cstr(e["id"]), cstr(label), cstr(e.get("param", "")),
            ", ".join("%.3ff" % c for c in box), argb(colours, e.get("colour")),
            ", ".join("%.3ff" % c for c in v), p0, pn))
    with open(out, "w") as f:
        f.write("// Generated by tools/panel/gen_panel.py from docs/panel.json: the SH-101 top panel, measured\n")
        f.write("// from the Commons photo (CC BY-SA 3.0, 909 bd). Coordinates in mm from the rear-left corner.\n")
        f.write("#pragma once\n\n#include <cstdint>\n\nnamespace sh101::panel {\n\n")
        f.write("enum class PType { %s };\n\n" % ", ".join(dict.fromkeys(TYPES.values())))
        f.write("struct Element {\n\tPType type;\n\tconst char *id, *label, *param;\n\tfloat box[4]; // x0, y0, x1, y1\n"
                "\tuint32_t colour;\n\tfloat v[8]; // per type, see tools/panel/gen_panel.py\n\tint pointStart, pointCount;\n};\n\n")
        f.write("constexpr float kWidth = %.1ff, kHeight = %.1ff;\n\n" % (d["panel"]["w"], d["panel"]["h"]))
        f.write("inline const float points[] = {\n\t" + ", ".join("%.3ff" % p for p in points) + "\n};\n\n")
        f.write("inline const Element elements[] = {\n" + "\n".join(rows) + "\n};\n\n")
        f.write("constexpr int kElementCount = %d;\n\n" % len(rows))
        for k in colours:
            f.write("constexpr uint32_t %s = %s;\n" % ("c_" + k, argb(colours, k)))
        f.write("\n} // namespace sh101::panel\n")
    print(len(rows), "elements")


if __name__ == "__main__":
    main()
