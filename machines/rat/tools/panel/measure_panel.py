#!/usr/bin/env python3
"""Measure the 1984 ProCo RAT "White Face" from photos and write docs/panel.json.

Usage (from the machine folder):  ../../.venv/bin/python -I tools/panel/measure_panel.py [--overlay OUT.png] [--rect OUT.png]

Photos (private/docs/photos/whiteface/, sellers' photos, reference only; sources.md there):
- top_orig1984_rv103889534.jpg: a 1984 original (serial RT-024885, pots dated 1984 week 31), shot
  nearly straight down with an iPhone 16 (2.22 mm lens, 26 mm equivalent: f = 3028 px). The camera
  is about 16 cm above the top, so heights matter: a part 3 mm above the print looks 2 % larger.
- gut_orig1984_rv102019119.jpg: another 1984 unit's board from below, the three pots' backs square-on.
- front_orig1985_rv87589066.jpg, side_orig_rv99143823.jpg: the front face and a side cheek square-on.

Method:
1. The print is one plane. Its pose is fitted with the camera's focal length from EXIF: the
   rectangles' edges (legend box, RAT box, the white block, the dividers) must be straight and
   square, and the three scale arcs circles. Plane units are then about photo px at the print.
2. The scale (plane units per mm) has two sources that disagree by 2.4 %:
   - the pot pitch: the knobs sit on the pots' shafts, at the arcs' centres; in the gut photo the
     pots' centre spacing is 1.143 pot-body diameters, and CTS 450 bodies are 15/16 in (23.8 mm),
     so the pitch is 27.2 mm;
   - the leaflet that came with the small box ("2 3/8"H x 4 1/16* x 3 1/8"D") read as a 4 1/16 in
     (103.2 mm) cheek length, the depth ProCo still gives for the RAT2. Its 3 1/8 in can't be the
     width (the photo's plan aspect is 1.12, not 1.30), so the line is not taken at its word.
   The scale is their mean, +-1.2 %.
3. Everything on the print is measured on the rectified plane. Parts above it (cheek tops, knob
   tops, the footswitch) are moved back towards the point under the camera by their height.

Frame (all output in mm): origin = rear-left corner of the plan outline (outer face of the left
cheek, rear end of the cheeks); x to the right, y towards the player (down on screen). Angles in
degrees from 12 o'clock, clockwise.
"""
import argparse, json, math, os, sys
import numpy as np
from PIL import Image, ImageOps, ImageDraw
from scipy import ndimage, optimize
from scipy.spatial.transform import Rotation

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PHOTOS = os.path.join(ROOT, "private/docs/photos/whiteface")
TOP = "top_orig1984_rv103889534.jpg"
GUT = "gut_orig1984_rv102019119.jpg"
FRONT = "front_orig1985_rv87589066.jpg"
SIDE = "side_orig_rv99143823.jpg"
OUT = os.path.join(ROOT, "docs/panel.json")

F35 = 26.0                      # EXIF FocalLengthIn35mmFormat of TOP
POT_BODY_MM = 23.81             # CTS 450 series: 15/16 in
LEAFLET_LENGTH_MM = 103.19      # 4 1/16 in
CHEEK_ABOVE_PRINT_MM = 1.5      # the cheeks' top edges over the print (+-1.5; flush to the 3 mm of the RAT2)


def load(name, rotate=0):
    im = ImageOps.exif_transpose(Image.open(os.path.join(PHOTOS, name)))
    return im.rotate(rotate, expand=True) if rotate else im


# ====================================================================== print points (photo px)

class Photo:
    def __init__(self, im):
        self.rgb = im
        self.g = np.asarray(im.convert("L")).astype(float)
        self.H, self.W = self.g.shape

    def sample(self, x, y):
        return ndimage.map_coordinates(self.g, [y, x], order=1)


def centroid(profile, lo):
    b = np.percentile(profile, 15)
    w = np.clip(profile - b, 0, None)
    if w.max() < 40:
        return None
    w[w < 0.35 * w.max()] = 0
    return lo + (w * np.arange(len(profile))).sum() / w.sum()


def half_max(profile, lo):
    lo_v, hi_v = profile.min(), profile.max()
    h = (lo_v + hi_v) / 2
    i = np.where(np.diff(np.sign(profile - h)) != 0)[0]
    if len(i) != 1:
        return None
    i = i[0]
    return lo + i + (h - profile[i]) / (profile[i + 1] - profile[i])


def print_points(P):
    """Sub-pixel points on the printed rectangles' strokes and edges. The windows were read off the
    photo (upright: knobs at the top), each a few px around one stroke."""
    g = P.g
    hs = lambda x, y0, y1: centroid(g[y0:y1, x - 1:x + 2].mean(axis=1), y0)   # noqa: E731  horizontal stroke
    vs = lambda y, x0, x1: centroid(g[y - 1:y + 2, x0:x1].mean(axis=0), x0)   # noqa: E731  vertical stroke
    he = lambda x, y0, y1: half_max(g[y0:y1, x - 1:x + 2].mean(axis=1), y0)   # noqa: E731  filled edge
    ve = lambda y, x0, x1: half_max(g[y - 1:y + 2, x0:x1].mean(axis=0), x0)   # noqa: E731
    pts = {}

    def add(name, lst):
        pts[name] = [(float(x), float(y)) for x, y in lst if x is not None and y is not None]

    add("leg_top", [(x, hs(x, 1222, 1262)) for x in range(760, 2200, 8)])
    add("leg_bot", [(x, hs(x, 1412, 1452)) for x in range(760, 2200, 8)])
    add("leg_left", [(vs(y, 705, 750), y) for y in range(1265, 1410, 4)])
    add("leg_right", [(vs(y, 2205, 2252), y) for y in range(1265, 1410, 4)])
    inner = g[1270:1400, 760:2200]
    cand = np.where((inner > 150).mean(axis=0) > 0.9)[0] + 760
    groups = np.split(cand, np.where(np.diff(cand) > 3)[0] + 1)
    divs = sorted(int(gr.mean()) for gr in groups if len(gr))
    assert len(divs) == 2, divs
    for k, xd in enumerate(divs):
        add(f"leg_div{k + 1}", [(vs(y, xd - 20, xd + 20), y) for y in range(1265, 1410, 4)])
    add("rat_top", [(x, hs(x, 2074, 2112)) for x in range(900, 2090, 8)])
    add("rat_bot", [(x, hs(x, 2400, 2442)) for x in range(900, 2090, 8)])
    add("rat_left", [(vs(y, 845, 880), y) for y in range(2130, 2380, 4)])
    add("rat_right", [(vs(y, 2115, 2152), y) for y in range(2130, 2380, 4)])
    # The white block's edges, away from the logo (it touches the top-left).
    add("blk_top", [(x, he(x, 2108, 2136)) for x in range(1250, 2080, 8)])
    add("blk_bot", [(x, he(x, 2376, 2402)) for x in range(1250, 2080, 8)])
    add("blk_left", [(ve(y, 880, 910), y) for y in range(2140, 2370, 4)])
    add("blk_right", [(ve(y, 2088, 2118), y) for y in range(2140, 2370, 4)])
    return pts


def arc_points(P, guesses, r0=239.7):
    """Points on the three scale arcs: on rays from each centre, a single narrow white stroke within
    14 px of the radius, on the black plate (rays crossing a knob or a tick are dropped)."""
    out = {}
    for name, (cx, cy) in guesses.items():
        for _ in range(4):
            pts, degs = [], []
            for deg in np.arange(-180, 180, 0.5):
                t = math.radians(deg)
                r = np.arange(r0 - 14, r0 + 14, 0.25)
                p = P.sample(cx + r * math.sin(t), cy - r * math.cos(t))
                ro = np.concatenate([np.arange(r0 - 30, r0 - 18, 1), np.arange(r0 + 18, r0 + 30, 1)])
                b = np.median(P.sample(cx + ro * math.sin(t), cy - ro * math.cos(t)))
                if b > 90:
                    continue
                w = np.clip(p - b, 0, None)
                if w.max() < 70:
                    continue
                on = w > 0.4 * w.max()
                lab, n = ndimage.label(on)
                if n != 1 or on.sum() * 0.25 > 16:
                    continue
                wi = w * on
                rr = (wi * r).sum() / wi.sum()
                pts.append((cx + rr * math.sin(t), cy - rr * math.cos(t)))
                degs.append(deg)
            A = np.array(pts)
            f = lambda c: np.hypot(A[:, 0] - c[0], A[:, 1] - c[1]) - c[2]   # noqa: E731
            s = optimize.least_squares(f, [cx, cy, r0], loss="soft_l1", f_scale=1.5)
            keep = np.abs(f(s.x)) < 3
            cx, cy, _ = s.x
            A, degs = A[keep], np.array(degs)[keep]
        out[name] = A.tolist()
    return out


# ====================================================================== the print plane's pose

class Pose:
    HL = ["leg_top", "leg_bot", "rat_top", "rat_bot", "blk_top", "blk_bot"]
    VL = ["leg_left", "leg_right", "leg_div1", "leg_div2", "rat_left", "rat_right", "blk_left", "blk_right"]
    ARCS = ["dist", "filt", "vol"]

    def __init__(self, P, pts, arcs):
        self.W, self.H = P.W, P.H
        self.cx, self.cy = (P.W - 1) / 2, (P.H - 1) / 2
        self.f = F35 * math.hypot(P.W, P.H) / 43.27
        self.tz = self.f      # gauge: the plane's units are about px at the print
        self.pts, self.arcs = pts, arcs
        p0 = [0, 0, 0, 0, 0]
        p0 += [np.mean([y for _, y in pts[k]]) - self.cy for k in self.HL]
        p0 += [np.mean([x for x, _ in pts[k]]) - self.cx for k in self.VL]
        for a in self.ARCS:
            A = np.array(arcs[a])
            p0 += [A[:, 0].mean() - self.cx, A[:, 1].mean() - self.cy] + ([] if a == "filt" else [240.0])
        s = optimize.least_squares(self.residuals, np.array(p0, float), loss="soft_l1", f_scale=1.0)
        self.unpack(s.x)
        r = self.residuals(s.x)
        self.rms, self.worst = float(np.sqrt((r ** 2).mean())), float(np.abs(r).max())
        self.C = -self.R.T @ self.T                    # the camera in plane coordinates
        self.Z = abs(self.C[2])                        # its height over the print, units
        self.nadir = self.C[:2]

    def unpack(self, p):
        i = 0
        self.R = Rotation.from_rotvec(p[0:3]).as_matrix()
        self.T = np.array([p[3], p[4], self.tz])
        i = 5
        self.h = dict(zip(self.HL, p[i:i + len(self.HL)]))
        i += len(self.HL)
        self.v = dict(zip(self.VL, p[i:i + len(self.VL)]))
        i += len(self.VL)
        self.c = {}
        for a in self.ARCS:
            if a == "filt":
                # gauge: the FILTER arc's radius is 240 units
                self.c[a] = np.array([p[i], p[i + 1], 240.0])
                i += 2
            else:
                self.c[a] = np.array(p[i:i + 3])
                i += 3

    def to_plane(self, xy, w=0.0):
        """Photo px -> plane (u, v) on the plane at height w units (towards the camera)."""
        xy = np.atleast_2d(np.asarray(xy, float))
        d = np.stack([(xy[:, 0] - self.cx) / self.f, (xy[:, 1] - self.cy) / self.f, np.ones(len(xy))], 1) @ self.R
        C = -self.R.T @ self.T
        sgn = np.sign(C[2])
        lam = (sgn * w - C[2]) / d[:, 2]
        return C[:2] + lam[:, None] * d[:, :2]

    def to_photo(self, u, v, w=0.0):
        C = -self.R.T @ self.T
        X = np.stack([u, v, np.full_like(u, np.sign(C[2]) * w)], -1) @ self.R.T + self.T
        return self.cx + self.f * X[..., 0] / X[..., 2], self.cy + self.f * X[..., 1] / X[..., 2]

    def residuals(self, p):
        self.unpack(p)
        out = []
        for k in self.HL:
            out.append(self.to_plane(self.pts[k])[:, 1] - self.h[k])
        for k in self.VL:
            out.append(self.to_plane(self.pts[k])[:, 0] - self.v[k])
        for a in self.ARCS:
            q = self.to_plane(self.arcs[a])
            c = self.c[a]
            out.append(np.hypot(q[:, 0] - c[0], q[:, 1] - c[1]) - c[2])
        return np.concatenate(out)

    def lift(self, uv, w):
        """A point measured on the print plane that is really at height w units: where it is in plan."""
        uv = np.asarray(uv, float)
        return self.nadir + (uv - self.nadir) * (self.Z - w) / self.Z

    def rectify(self, P, u0, u1, v0, v1):
        uu, vv = np.meshgrid(np.arange(u0, u1, dtype=float), np.arange(v0, v1, dtype=float))
        x, y = self.to_photo(uu, vv)
        rgb = np.asarray(P.rgb.convert("RGB")).astype(float)
        out = np.stack([ndimage.map_coordinates(rgb[..., c], [y, x], order=1, cval=255) for c in range(3)], -1)
        return out.clip(0, 255).astype(np.uint8)


# ====================================================================== scale

def pot_pitch_mm():
    """The pots' centre spacing, in body diameters, from the gut photo (pots' backs square-on)."""
    im = load(GUT)
    g = ndimage.gaussian_filter(np.asarray(im.convert("L")).astype(float), 2)
    s = im.size[0] / 975
    fits = {}
    for name, (cx, cy, r) in {"left": (175, 405, 125), "right": (712, 400, 128)}.items():
        cx, cy, r = cx * s, cy * s, r * s
        pts = []
        for deg in range(0, 360, 2):
            t = math.radians(deg)
            rr = np.arange(0.75 * r, 1.2 * r, 1.0)
            xs, ys = cx + rr * math.cos(t), cy + rr * math.sin(t)
            p = ndimage.map_coordinates(g, [ys, xs], order=1)
            dp = np.diff(p)
            i = int(np.argmin(dp))
            if dp[i] < -6:
                pts.append((xs[i], ys[i]))
        A = np.array(pts)
        for _ in range(3):
            f = lambda c: np.hypot(A[:, 0] - c[0], A[:, 1] - c[1]) - c[2]   # noqa: E731
            sol = optimize.least_squares(f, [cx, cy, r], loss="soft_l1", f_scale=3)
            res = f(sol.x)
            A = A[np.abs(res) < 3 * np.median(np.abs(res)) + 1]
        fits[name] = sol.x
    pitch = math.hypot(*(fits["right"][:2] - fits["left"][:2])) / 2     # three pots, two pitches
    d = fits["left"][2] + fits["right"][2]                              # mean diameter
    return pitch / d * POT_BODY_MM, {k: [round(float(x), 1) for x in v] for k, v in fits.items()}


# ====================================================================== measuring on the plane

def stroke_width(prof):
    """Full width at half maximum of a white stroke profile, in samples."""
    b = np.percentile(prof, 10)
    w = prof - b
    m = w.max()
    above = np.where(w > m / 2)[0]
    if len(above) < 2:
        return None
    i0, i1 = above[0], above[-1]
    if i0 == 0 or i1 == len(w) - 1:
        return None
    l = i0 - 1 + (m / 2 - w[i0 - 1]) / (w[i0] - w[i0 - 1])
    r = i1 + (w[i1] - m / 2) / (w[i1] - w[i1 + 1])
    return r - l


def front_face(width_mm):
    """The front face, square-on in FRONT (the camera about 4 deg above it). The scale is the cheeks'
    outer faces, as far apart as in the top view; z runs down the face from the top plate's front bend."""
    im = load(FRONT)
    g = ndimage.gaussian_filter(np.asarray(im.convert("L")).astype(float), 2)
    yy = 1965                                       # a row through the logo
    prof = g[yy - 40:yy + 40, :].mean(axis=0)
    bg = float(np.median(prof[60:130]))             # the shelf beside the pedal
    level = (bg + 3) / 2

    def first(i0, i1, step):
        for i in range(i0, i1, step):
            if (prof[i] - level) * (prof[i + step] - level) <= 0:
                return i + (level - prof[i]) / (prof[i + step] - prof[i]) * step
    L, R = first(150, 400, 1), first(2800, 2400, -1)
    s = (R - L) / width_mm
    sub = g[1620:2350, 900:2020] > 150              # the white logo
    ys, xs = np.where(sub)
    lx0, lx1, ly0, ly1 = xs.min() + 900, xs.max() + 901, ys.min() + 1620, ys.max() + 1621
    # the face's top: the bend's highlight; its bottom: the base's edge (columns clear of the logo
    # and of the knobs' and footswitch's reflections)
    tops, bots = [], []
    for x in (1900, 2000, 2100, 2200, 2300):
        col = g[1250:1450, x - 20:x + 20].mean(axis=1)
        tops.append(1250 + int(np.argmax(col)))
        col = g[2400:2560, x - 20:x + 20].mean(axis=1)
        bots.append(2400 + int(np.argmax(col)))
    top, bot = float(np.median(tops)), float(np.median(bots))
    mm = lambda px: round(float(px / s), 2)   # noqa: E731
    return {"source": FRONT, "px_per_mm": round(float(s), 3), "height": mm(bot - top),
            "place_front": {"x0": mm(lx0 - L), "x1": mm(lx1 - L), "z0": mm(ly0 - top), "z1": mm(ly1 - top)},
            "note": "the face spans the top plate's width; z from the top plate's front bend down to the base"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--overlay")
    ap.add_argument("--rect")
    a = ap.parse_args()

    P = Photo(load(TOP, rotate=90))
    pts = print_points(P)
    arcs = arc_points(P, {"dist": (985, 1702), "filt": (1488.8, 1701.8), "vol": (1993, 1702)})
    pose = Pose(P, pts, arcs)
    tilt = math.degrees(math.acos(abs(pose.R[2, 2])))
    print(f"pose: f {pose.f:.1f} px, tilt {tilt:.2f} deg, camera {pose.Z:.0f} units over the print; rms {pose.rms:.2f} units, worst {pose.worst:.2f}")
    print("arc radii", [round(float(pose.c[k][2]), 2) for k in pose.ARCS])

    # The rectified plane, 1 unit a pixel.
    # (the plane's origin is arbitrary: the window is set from the print, with room for the cheeks)
    u0, u1 = int(pose.v["leg_left"] - 240), int(pose.v["leg_right"] + 180)
    v0, v1 = int(pose.h["leg_top"] - 200), int(pose.h["rat_bot"] + 720)
    R = pose.rectify(P, u0, u1, v0, v1)
    if a.rect:
        Image.fromarray(R).save(a.rect)
    G = np.asarray(Image.fromarray(R).convert("L")).astype(float)
    at = lambda u, v: ndimage.map_coordinates(G, [np.atleast_1d(v) - v0, np.atleast_1d(u) - u0], order=1)  # noqa: E731

    # ---- scale
    pitch_mm, pot_fits = pot_pitch_mm()
    pitch_u = ((pose.c["filt"][0] - pose.c["dist"][0]) + (pose.c["vol"][0] - pose.c["filt"][0])) / 2
    s_pot = pitch_u / pitch_mm
    dark = G < 100
    Ls, Rs = [], []
    for y in range(200, 1900, 10):
        xs = np.where(dark[y])[0]
        if len(xs) > 100:
            Ls.append(xs.min())
            Rs.append(xs.max())
    out_left, out_right = float(np.median(Ls)) + u0 + 0.5, float(np.median(Rs)) + u0 - 0.5
    tops, bots = [], []
    for x in list(range(int(out_left - u0) + 5, int(out_left - u0) + 40, 5)) + list(range(int(out_right - u0) - 40, int(out_right - u0) - 5, 5)):
        ys = np.where(dark[:, x])[0]
        tops.append(ys.min())
        bots.append(ys.max())
    out_rear, out_front = float(min(tops)) + v0 + 0.5, float(max(bots)) + v0 - 0.5
    # The cheek tops are above the print: their plan length, back on the print's scale.
    h_units = lambda mm, s: mm * s   # noqa: E731
    s_leaf = (out_front - out_rear) / LEAFLET_LENGTH_MM
    for _ in range(5):
        s_leaf = (out_front - out_rear) * (pose.Z - h_units(CHEEK_ABOVE_PRINT_MM, s_leaf)) / pose.Z / LEAFLET_LENGTH_MM
    S = (s_pot + s_leaf) / 2
    print(f"scale: pot pitch {pitch_mm:.2f} mm -> {s_pot:.3f} u/mm; leaflet length -> {s_leaf:.3f} u/mm; "
          f"using {S:.3f} (+-{abs(s_pot - s_leaf) / 2 / S * 100:.1f} %); camera {pose.Z / S:.0f} mm over the print")

    hc = CHEEK_ABOVE_PRINT_MM * S
    (ol, orr), (_, _) = pose.lift([out_left, out_rear], hc), (0, 0)
    corner_rl = pose.lift([out_left, out_rear], hc)
    corner_fr = pose.lift([out_right, out_front], hc)
    U0, V0 = corner_rl
    mmx = lambda u: round(float((u - U0) / S), 2)   # noqa: E731
    mmy = lambda v: round(float((v - V0) / S), 2)   # noqa: E731
    mml = lambda d: round(float(d / S), 3)          # noqa: E731
    width, depth = mml(corner_fr[0] - U0), mml(corner_fr[1] - V0)
    print(f"outline {width} x {depth} mm (aspect {depth / width:.4f})")

    panel = {"units": "mm", "w": width, "h": depth, "pxPerUnit": None}
    els = {}
    # ---- the print's rectangles (stroke centre lines from the pose)
    def stroke_mm(u_or_v, axis, at_other):
        """FWHM of a stroke crossing at plane coordinate u_or_v along axis 0 (vertical stroke: sample along u) or 1."""
        ws = []
        for o in at_other:
            t = np.arange(u_or_v - 20, u_or_v + 20, 0.25)
            prof = at(t, np.full_like(t, o)) if axis == 0 else at(np.full_like(t, o), t)
            w = stroke_width(prof)
            if w:
                ws.append(w * 0.25)
        return mml(np.median(ws))

    h, v = pose.h, pose.v
    lb = {"x0": mmx(v["leg_left"]), "x1": mmx(v["leg_right"]), "y0": mmy(h["leg_top"]), "y1": mmy(h["leg_bot"]),
          "divs": [mmx(v["leg_div1"]), mmx(v["leg_div2"])],
          "stroke": stroke_mm(h["leg_top"], 1, np.linspace(v["leg_left"] + 40, v["leg_right"] - 40, 30))}
    els["legend_box"] = lb
    rb = {"x0": mmx(v["rat_left"]), "x1": mmx(v["rat_right"]), "y0": mmy(h["rat_top"]), "y1": mmy(h["rat_bot"]),
          "stroke": stroke_mm(h["rat_top"], 1, np.linspace(v["rat_left"] + 40, v["rat_right"] - 40, 30))}
    els["rat_box"] = rb
    els["white_block"] = {"x0": mmx(v["blk_left"]), "x1": mmx(v["blk_right"]), "y0": mmy(h["blk_top"]), "y1": mmy(h["blk_bot"])}

    # ---- legends: the white letters in each cell
    labels = []
    cells = [(v["leg_left"], v["leg_div1"]), (v["leg_div1"], v["leg_div2"]), (v["leg_div2"], v["leg_right"])]
    for text, (c0, c1) in zip(["DISTORTION", "FILTER", "VOLUME"], cells):
        su0, su1 = int(c0 - u0 + 14), int(c1 - u0 - 14)
        sv0, sv1 = int(h["leg_top"] - v0 + 14), int(h["leg_bot"] - v0 - 14)
        sub = G[sv0:sv1, su0:su1] > 140
        ys, xs = np.where(sub)
        bx0, bx1 = xs.min() + su0 + u0, xs.max() + 1 + su0 + u0
        by0, by1 = ys.min() + sv0 + v0, ys.max() + 1 + sv0 + v0
        labels.append({"text": text, "x0": mmx(bx0), "x1": mmx(bx1), "y0": mmy(by0), "y1": mmy(by1),
                       "cap_h": mml(by1 - by0), "font": "Helvetica Bold (screen print): drawn with Nimbus Sans Narrow Bold stretched to the box"})
    els["labels"] = labels

    # ---- scales: centre and radius from the pose; stroke; where the arc and its ticks are
    scales = []
    for k, name in zip(pose.ARCS, ["distortion", "filter", "volume"]):
        cu, cv, r = pose.c[k]
        ws = []
        for deg in range(-125, 126, 5):
            t = math.radians(deg)
            rr = np.arange(r - 15, r + 15, 0.25)
            prof = at(cu + rr * math.sin(t), cv - rr * math.cos(t))
            if prof.min() > 80:
                continue
            w = stroke_width(prof)
            if w and w * 0.25 < 25:
                ws.append(w * 0.25)
        # ticks: white inside the arc between 0.62 r and 0.92 r, angle by angle
        tick = []
        for deg in np.arange(-150, 150.01, 0.25):
            t = math.radians(deg)
            rr = np.arange(0.70 * r, 0.93 * r, 1.0)
            tick.append(np.mean(at(cu + rr * math.sin(t), cv - rr * math.cos(t)) > 140))
        tick = np.array(tick)
        scales.append({"name": name, "cx": mmx(cu), "cy": mmy(cv), "r": mml(r), "stroke": mml(np.median(ws)) if ws else None,
                       "tick_profile": [round(float(x), 2) for x in tick[::4]]})
    els["scales"] = scales

    # ---- ticks: the short ones inside the arc (0, +-45, +-90) and the arc's ends (+-135); each tick's
    # angle is the centroid of the white across a narrow ring just inside the arc stroke
    for sc, k in zip(scales, pose.ARCS):
        cu, cv, r = pose.c[k]
        ring = np.arange(0.86 * r, 0.95 * r, 1.0)
        ticks = []
        for nominal in (-135, -90, -45, 0, 45, 90, 135):
            degs = np.arange(nominal - 6, nominal + 6.01, 0.1)
            prof = np.array([np.mean(at(cu + ring * math.sin(math.radians(d)), cv - ring * math.cos(math.radians(d)))) for d in degs])
            b = np.percentile(prof, 10)
            w = np.clip(prof - b, 0, None)
            if w.max() < 60:
                continue                          # under the knob, as it leans in this photo
            w[w < 0.4 * w.max()] = 0
            deg = float((w * degs).sum() / w.sum())
            tw = stroke_width(prof)
            # the visible inner end: walk in along the tick until it goes dark (or under the knob)
            rr = np.arange(0.95 * r, 0.40 * r, -1.0)
            on = at(cu + rr * math.sin(math.radians(deg)), cv - rr * math.cos(math.radians(deg))) > 120
            inner = rr[np.argmin(on)] if not on.all() else rr[-1]
            ticks.append({"nominal": nominal, "deg": round(deg, 2),
                          "w": mml(tw * 0.1 * math.radians(1) * 0.905 * r) if tw else None,
                          "visible_r_in": mml(inner)})
        sc["ticks"] = ticks
        sc.pop("tick_profile", None)

    # ---- knobs: each one's flat top is a disc at the knob's height; it leans away from the point under
    # the camera by height / (camera height - height). The shaft is the scale's centre.
    Gs = ndimage.gaussian_filter(G, 1.5)
    ats = lambda u, v: ndimage.map_coordinates(Gs, [np.atleast_1d(v) - v0, np.atleast_1d(u) - u0], order=1)  # noqa: E731

    def disc_edge(cu, cv, r_lo, r_hi, skip=()):
        """Circle through the first rise out of a dark disc on rays from (cu, cv)."""
        pts = []
        for deg in np.arange(0, 360, 1.5):
            if any(abs((deg - s_ + 180) % 360 - 180) < 9 for s_ in skip):
                continue
            t_ = math.radians(deg)
            rr = np.arange(r_lo, r_hi, 0.5)
            p = ats(cu + rr * math.sin(t_), cv - rr * math.cos(t_))
            base = np.median(ats(cu + 0.3 * r_lo * math.sin(t_), cv - 0.3 * r_lo * math.cos(t_)))
            hit = np.where(p > base + 28)[0]
            if len(hit):
                rh = rr[hit[0]]
                pts.append((cu + rh * math.sin(t_), cv - rh * math.cos(t_)))
        A = np.array(pts)
        c = [cu, cv, (r_lo + r_hi) / 2]
        for _ in range(4):
            f = lambda c_: np.hypot(A[:, 0] - c_[0], A[:, 1] - c_[1]) - c_[2]   # noqa: E731
            c = optimize.least_squares(f, c, loss="soft_l1", f_scale=2).x
            res = f(c)
            A = A[np.abs(res) < max(3.0, 2.5 * np.median(np.abs(res)))]
        return c, len(A), float(np.sqrt((f(c) ** 2).mean()))

    def pointer_angle(cu, cv, r):
        best = None
        for deg in np.arange(-180, 180, 0.5):
            t_ = math.radians(deg)
            rr = np.arange(0.25 * r, 0.7 * r, 1.0)
            m = np.mean(at(cu + rr * math.sin(t_), cv - rr * math.cos(t_)))
            if best is None or m > best[0]:
                best = (m, deg)
        return best[1]

    knobs = []
    hk_guess = 16.0 * S
    for k, sc in zip(pose.ARCS, scales):
        A = np.array(pose.c[k][:2])
        guess = pose.nadir + (A - pose.nadir) * pose.Z / (pose.Z - hk_guess)
        pa = pointer_angle(guess[0], guess[1], 8.5 * S)
        (tu, tv, tr), n, rms = disc_edge(guess[0], guess[1], 6.0 * S, 11.0 * S, skip=(pa,))
        # height from the lean (along the line from the nadir through the shaft)
        dA = A - pose.nadir
        dT = np.array([tu, tv]) - pose.nadir
        ratio = float(dT @ dA / (dA @ dA))                 # = Z / (Z - h)
        hgt = pose.Z * (1 - 1 / ratio) if ratio > 1.0005 else None
        knobs.append({"name": sc["name"], "top_fit_n": n, "top_rms_units": round(rms, 2), "pointer_deg": round(pa, 1),
                      "lean_mm": [mml(tu - A[0]), mml(tv - A[1])], "lean_ratio": round(ratio, 4),
                      "top_h_mm": mml(hgt) if hgt else None, "top_r_units_on_print": round(float(tr), 2)})
    # The skirt: on the side facing the point under the camera, its bottom edge is what shows: the
    # outermost bright knurl ridge on rays within 35 degrees of that direction.
    for k, kb in zip(pose.ARCS, knobs):
        A = np.array(pose.c[k][:2])
        toward = math.degrees(math.atan2(pose.nadir[0] - A[0], -(pose.nadir[1] - A[1])))
        edges = []
        for deg in np.arange(toward - 35, toward + 35.01, 1.0):
            t_ = math.radians(deg)
            rr = np.arange(6.5 * S, 12.0 * S, 0.5)
            p_ = at(A[0] + rr * math.sin(t_), A[1] - rr * math.cos(t_))
            plate = np.median(at(A[0] + 11.3 * S * math.sin(t_), A[1] - 11.3 * S * math.cos(t_)))
            hit = np.where(p_ > plate + 30)[0]
            hit = hit[rr[hit] < pose.c[k][2] - 0.8 * S]      # not the scale's arc or ticks
            if len(hit):
                edges.append(rr[hit[-1]])
        kb["skirt_d_bottom"] = mml(2 * np.percentile(edges, 80)) if edges else None
    hs = [kb["top_h_mm"] for kb in knobs if kb["top_h_mm"] and abs(kb["lean_ratio"] - 1) > 0.03]
    h_top = float(np.mean(hs))
    for kb in knobs:
        kb["top_d"] = mml(2 * kb["top_r_units_on_print"] * (pose.Z - h_top * S) / pose.Z)
    # The pointer: the white line on each top, from the top's own (leaning) centre.
    for k, kb in zip(pose.ARCS, knobs):
        A = np.array(pose.c[k][:2])
        tc = pose.nadir + (A - pose.nadir) * kb["lean_ratio"]
        rt = kb["top_r_units_on_print"]
        best = None
        for deg in np.arange(-60, 60, 0.25):
            t_ = math.radians(deg)
            rr = np.arange(0.2 * rt, 0.85 * rt, 1.0)
            m = np.mean(at(tc[0] + rr * math.sin(t_), tc[1] - rr * math.cos(t_)))
            if best is None or m > best[0]:
                best = (m, deg)
        deg = best[1]
        t_ = math.radians(deg)
        rr = np.arange(0, rt, 0.5)
        on = at(tc[0] + rr * math.sin(t_), tc[1] - rr * math.cos(t_)) > 120
        idx = np.where(on)[0]
        r0, r1 = rr[idx[0]], rr[idx[-1]]
        mid = (r0 + r1) / 2
        nrm = np.arange(-20, 20, 0.25)
        prof = at(tc[0] + mid * math.sin(t_) + nrm * math.cos(t_), tc[1] - mid * math.cos(t_) + nrm * math.sin(t_))
        pw = stroke_width(prof)
        kb["pointer"] = {"deg_in_photo": round(float(deg), 1), "r0_frac": round(float(r0 / rt), 3), "r1_frac": round(float(r1 / rt), 3),
                         "w_units": round(float(pw * 0.25), 2) if pw else None}
    els["knobs"] = knobs
    print("knob top height (from the outer knobs' lean):", [round(x, 2) for x in hs], "->", round(h_top, 2), "mm")
    for kb in knobs:
        print("  ", kb)

    # ---- cheeks, the gaps beside them and the shell's top plate: transitions on profiles across the
    # left and right edges (background -> cheek top -> black gap -> plate) and along the plate's
    # rear and front edges (plate -> background or the cheeks' overhang)
    def transitions(prof, lo):
        """Indices where a smoothed profile crosses half-way between neighbouring plateaus."""
        return prof
    rows = np.linspace(V0 + 25 * S, V0 + 80 * S, 23)
    def edge_profile(u_from, u_to, v):
        uu = np.arange(u_from, u_to, 0.5)
        return uu, ats(uu, np.full_like(uu, v))
    gaps = {}
    for side, (ua, ub) in {"left": (U0 - 1 * S, U0 + 9 * S), "right": (corner_fr[0] + 1 * S, corner_fr[0] - 9 * S)}.items():
        g0, g1 = [], []
        for v_ in rows:
            uu = np.arange(min(ua, ub), max(ua, ub), 0.5)
            p_ = ats(uu, np.full_like(uu, v_))
            if side == "right":
                uu, p_ = uu[::-1], p_[::-1]
            # the gap: the darkest run (under 0.6 of the plate's level) after the cheek
            plate = np.median(p_[-int(2 * S):])
            low = p_ < 0.6 * plate
            idx = np.where(low)[0]
            if len(idx) == 0:
                continue
            run = np.split(idx, np.where(np.diff(idx) > 2)[0] + 1)
            run = max(run, key=len)
            g0.append(uu[run[0]])
            g1.append(uu[run[-1]])
        gaps[side] = (float(np.median(g0)), float(np.median(g1)))
    gl0, gl1 = sorted(gaps["left"])
    gr0, gr1 = sorted(gaps["right"])
    cols = np.linspace(U0 + 25 * S, U0 + 70 * S, 19)
    re, fe = [], []
    for u_ in cols:
        vv_ = np.arange(V0 - 1 * S, V0 + 12 * S, 0.5)
        p_ = ats(np.full_like(vv_, u_), vv_)
        i = np.where(p_ < 100)[0]
        re.append(vv_[i[0]])
        # the front edge: scanning forwards from inside the plate, the first light pixel (the floor
        # beyond the plate's front bend)
        vv_ = np.arange(corner_fr[1] - 14 * S, corner_fr[1] + 2 * S, 0.5)
        p_ = ats(np.full_like(vv_, u_), vv_)
        i = np.where(p_ > 100)[0]
        fe.append(vv_[i[0]])
    shell_rear, shell_front = float(np.median(re)), float(np.median(fe))
    els["cheeks"] = {"left": {"x0": 0.0, "x1": mmx(gl0)}, "right": {"x0": mmx(gr1), "x1": width},
                     "y0": 0.0, "y1": depth,
                     "note": "the cheeks' top edges in plan, 1.5 mm over the print; the ends round over about 3 mm"}
    els["gaps"] = {"left": [mmx(gl0), mmx(gl1)], "right": [mmx(gr0), mmx(gr1)]}
    els["shell_top"] = {"x0": mmx(gl1), "x1": mmx(gr0), "y0": mmy(shell_rear), "y1": mmy(shell_front),
                        "note": "the top plate; its rear and front edges bend down into the rear and front faces"}
    print("cheeks/gaps/shell:", els["gaps"], els["shell_top"])

    # ---- the footswitch: the chrome bezel (washer and nut) on the plate, the plunger's top above it
    def bright_disc(cu, cv, r_lo, r_hi, thr):
        pts = []
        for deg in np.arange(0, 360, 2):
            t_ = math.radians(deg)
            rr = np.arange(r_lo, r_hi, 0.5)
            p_ = ats(cu + rr * math.sin(t_), cv - rr * math.cos(t_))
            hit = np.where(p_ < thr)[0]
            if len(hit):
                rh = rr[hit[0]]
                pts.append((cu + rh * math.sin(t_), cv - rh * math.cos(t_)))
        A_ = np.array(pts)
        c = [cu, cv, (r_lo + r_hi) / 2]
        for _ in range(4):
            f = lambda c_: np.hypot(A_[:, 0] - c_[0], A_[:, 1] - c_[1]) - c_[2]   # noqa: E731
            c = optimize.least_squares(f, c, loss="soft_l1", f_scale=2).x
            res = f(c)
            A_ = A_[np.abs(res) < max(3.0, 2.5 * np.median(np.abs(res)))]
        return c, float(np.sqrt((f(c) ** 2).mean()))
    # the bezel: bright chrome against the black plate; search from its centre outwards for the plate
    sub = (G[int(V0 + 70 * S - v0):int(V0 + 100 * S - v0), int(U0 + 30 * S - u0):int(U0 + 64 * S - u0)] > 150)
    ys_, xs_ = np.where(sub)
    bu, bv = xs_.mean() + U0 + 30 * S, ys_.min() + V0 + 70 * S + 7.5 * S
    (bu, bv, br), brms = bright_disc(bu, bv, 5 * S, 11 * S, 45)
    # the plunger's top: matte, ringed by black; it leans towards the front
    pu, pv = bu + 0.2 * S, bv + 3.5 * S
    (pu, pv, pr), prms = bright_disc(pu, pv, 2.5 * S, 7.5 * S, 70)
    h_bezel = 2.0 * S       # the bezel's top over the plate (washer + nut), from the front photo's side view
    B = pose.lift([bu, bv], h_bezel)
    dB = B - pose.nadir
    ratio = float((np.array([pu, pv]) - pose.nadir) @ dB / (dB @ dB))
    hp = pose.Z * (1 - 1 / ratio)
    els["footswitch"] = {"x": mmx(B[0]), "y": mmy(B[1]), "bezel_d": mml(2 * br * (pose.Z - h_bezel) / pose.Z),
                         "plunger_d": mml(2 * pr * (pose.Z - hp) / pose.Z), "plunger_top_h": mml(hp),
                         "fit_rms_units": [round(brms, 2), round(prms, 2)]}
    print("footswitch:", els["footswitch"])

    # ---- the white block's marks: the black inside it. The RAT letters are the three large parts on the
    # right; the ProCo logo (with SOUND INC) is everything left of them.
    bx0, bx1, by0, by1 = pose.v["blk_left"], pose.v["blk_right"], pose.h["blk_top"], pose.h["blk_bot"]
    blk = G[int(by0 - v0) + 4:int(by1 - v0) - 4, int(bx0 - u0) + 4:int(bx1 - u0) - 4] < 110
    lab, n = ndimage.label(blk)
    objs = ndimage.find_objects(lab)
    sizes = ndimage.sum(blk, lab, range(1, n + 1))
    comps = [(int(sizes[i]), objs[i]) for i in range(n) if sizes[i] > 30]
    big = sorted(comps, key=lambda c: -c[0])[:3]
    rat_x0 = min(c[1][1].start for c in big)
    def bbox_of(cs):
        x0 = min(c[1][1].start for c in cs); x1 = max(c[1][1].stop for c in cs)
        y0 = min(c[1][0].start for c in cs); y1 = max(c[1][0].stop for c in cs)
        ox_, oy_ = bx0 + 4, by0 + 4
        return {"x0": mmx(x0 + ox_), "x1": mmx(x1 + ox_), "y0": mmy(y0 + oy_), "y1": mmy(y1 + oy_)}
    logo_left = [c for c in comps if c[1][1].stop <= rat_x0 - 2]
    els["mark_rat"] = bbox_of(big)
    els["mark_proco"] = bbox_of(logo_left)
    print("marks:", els["mark_rat"], els["mark_proco"])

    if a.overlay:
        im = Image.fromarray(R).convert("RGB")
        d = ImageDraw.Draw(im)
        P_ = lambda x, y: (U0 + x * S - u0, V0 + y * S - v0)   # noqa: E731
        def rect(e, col, w=2):
            d.rectangle([P_(e["x0"], e["y0"]), P_(e["x1"], e["y1"])], outline=col, width=w)
        d.rectangle([P_(0, 0), P_(width, depth)], outline=(255, 0, 255), width=2)
        rect(els["shell_top"], (0, 200, 255))
        rect(els["legend_box"], (255, 0, 0))
        for x in els["legend_box"]["divs"]:
            d.line([P_(x, els["legend_box"]["y0"]), P_(x, els["legend_box"]["y1"])], fill=(255, 0, 0), width=2)
        for l in els["labels"]:
            rect(l, (0, 255, 0), 1)
        rect(els["rat_box"], (255, 0, 0))
        rect(els["white_block"], (255, 128, 0))
        rect(els["mark_rat"], (0, 255, 0))
        rect(els["mark_proco"], (0, 255, 0))
        for sc, kb in zip(els["scales"], els["knobs"]):
            cx, cy = P_(sc["cx"], sc["cy"])
            r = sc["r"] * S
            d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 0, 0), width=2)
            for tk in sc["ticks"]:
                t_ = math.radians(tk["deg"])
                d.line([(cx + 0.7 * r * math.sin(t_), cy - 0.7 * r * math.cos(t_)), (cx + r * math.sin(t_), cy - r * math.cos(t_))], fill=(0, 255, 255), width=2)
            rk = kb["top_d"] / 2 * S
            d.ellipse([cx - rk, cy - rk, cx + rk, cy + rk], outline=(255, 255, 0), width=2)
        fs = els["footswitch"]
        cx, cy = P_(fs["x"], fs["y"])
        for dd, col in ((fs["bezel_d"], (255, 255, 0)), (fs["plunger_d"], (0, 255, 255))):
            r = dd / 2 * S
            d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=col, width=2)
        im.save(a.overlay)

    fr = front_face(width)
    print("front face:", fr)

    # ---- docs/panel.json
    kn = [kb for kb in knobs]
    lean_ok = [kb for kb in kn if abs(kb["lean_ratio"] - 1) > 0.03]
    mean = lambda xs: round(float(np.mean(xs)), 2)   # noqa: E731
    skirt_d = mean([kb["skirt_d_bottom"] for kb in kn])
    top_d = mean([kb["top_d"] for kb in kn])
    ptr = [kb["pointer"] for kb in kn if kb["pointer"]["r0_frac"] > 0.1]
    pw = mean([p_["w_units"] for p_ in ptr]) * (pose.Z - h_top * S) / pose.Z / S
    tick_w = mean([tk["w"] for sc in scales for tk in sc["ticks"] if tk["w"]])
    tick_in = min(tk["visible_r_in"] for sc in scales for tk in sc["ticks"])
    out = {
        "source_image": "private/docs/photos/whiteface/" + TOP,
        "source_note": "a 1984 original's top (serial RT-024885) from a Reverb listing, reference only; see private/docs/photos/whiteface/sources.md",
        "units": "mm",
        "view": "plan (orthographic top view): the print plane rectified with the fitted camera pose; parts above it moved back by their height",
        "origin": "rear-left corner of the plan outline (outer face of the left cheek, rear end of the cheeks); x right, y towards the player",
        "angles": "degrees from 12 o'clock, clockwise",
        "pose": {"f_px": round(pose.f, 1), "tilt_deg": round(tilt, 2), "camera_mm_over_print": round(pose.Z / S, 1),
                 "rms_units": round(pose.rms, 2), "arc_radii_units": [round(float(pose.c[k][2]), 2) for k in pose.ARCS]},
        "scale": {"units_per_mm": round(S, 3), "pot_pitch": {"units_per_mm": round(s_pot, 3), "pitch_mm": round(pitch_mm, 2), "gut_fits_px": pot_fits},
                  "leaflet_length": {"units_per_mm": round(s_leaf, 3), "length_mm": LEAFLET_LENGTH_MM, "cheek_above_print_mm": CHEEK_ABOVE_PRINT_MM},
                  "uncertainty_pct": round(abs(s_pot - s_leaf) / 2 / S * 100, 1)},
        "accuracy": "print positions +-0.05 mm relative (pose rms 0.85 units = 0.045 mm); overall size +-1.2 % (the scale); "
                    "knob and footswitch sizes and heights +-0.5..2 mm (fitted on edges of 3-D parts)",
        "outline": {"w": width, "h": depth},
        "cheeks": els["cheeks"], "gaps": els["gaps"], "shell_top": els["shell_top"],
        "legend_box": els["legend_box"], "labels": els["labels"],
        "scales": [{"name": sc["name"], "cx": sc["cx"], "cy": sc["cy"], "r": sc["r"], "stroke": sc["stroke"],
                    "arc_deg": [-135, 135], "tick_deg": [-135, -90, -45, 0, 45, 90, 135],
                    "ticks_measured": sc["ticks"]} for sc in scales],
        "tick_w": tick_w, "tick_r_in": tick_in,
        "tick_note": "short ticks at 0, +-45, +-90 and the arc's ends at +-135 run in under the knob; tick_r_in is the "
                     "innermost visible point (the knob hides the rest)",
        "knobs": {"skirt_d": skirt_d, "top_d": top_d, "height": round(h_top, 1), "pointer_r0": round(float(np.mean([p_["r0_frac"] for p_ in ptr])) * top_d / 2, 2),
                  "pointer_r1": round(float(np.mean([p_["r1_frac"] for p_ in ptr])) * top_d / 2, 2), "pointer_w": round(pw, 2),
                  "travel_deg": [-150, 150], "travel_note": "CTS 450 pots turn 300 degrees",
                  "measured": kn},
        "rat_box": els["rat_box"], "white_block": els["white_block"],
        "places": {"rat": els["mark_rat"], "proco": els["mark_proco"]},
        "footswitch": els["footswitch"],
        "front_face": fr,
    }
    # Colours: medians of patches on the rectified photo (soft studio light; the print's white is
    # the block's, the plate's black between the knobs and the RAT box).
    RGB = R.astype(float)
    def patch(x0, y0, x1, y1):
        a_ = RGB[int(V0 + y0 * S - v0):int(V0 + y1 * S - v0), int(U0 + x0 * S - u0):int(U0 + x1 * S - u0)].reshape(-1, 3)
        return "#%02x%02x%02x" % tuple(int(round(c)) for c in np.median(a_, axis=0))
    wb, fs, sc0 = els["white_block"], els["footswitch"], scales[1]
    out["colours"] = {
        "plate": patch(30, 42, 60, 48),
        "block_white": patch(wb["x1"] - 3, wb["y0"] + 2, wb["x1"] - 1, wb["y1"] - 2),
        "print_white": patch(wb["x1"] - 3, wb["y0"] + 2, wb["x1"] - 1, wb["y1"] - 2),
        "cheek": patch(0.4, 30, 1.6, 70),
        "gap": patch(els["gaps"]["left"][0] + 0.3, 30, els["gaps"]["left"][1] - 0.3, 70),
        "knob_top": patch(sc0["cx"] - 3 + 0.9, sc0["cy"] - 2.2 + 1, sc0["cx"] - 1 + 0.9, sc0["cy"] - 2.2 + 3),
        "plunger": patch(fs["x"] - 2, fs["y"] + 3, fs["x"] + 2, fs["y"] + 6),
        "chrome": patch(fs["x"] - 7.6, fs["y"] - 1, fs["x"] - 6.8, fs["y"] + 1),
        "note": "medians of photo patches; the light is soft and from above, so these are the surfaces' look, not paint swatches",
    }
    print("colours:", out["colours"])
    json.dump(out, open(OUT, "w"), indent=1)
    print("wrote", OUT)
    print(json.dumps(els["legend_box"]), json.dumps(els["rat_box"]), json.dumps(els["white_block"]))
    for l in labels:
        print(l["text"], l["x0"], l["x1"], l["y0"], l["y1"], l["cap_h"])
    for sc in scales:
        print(sc["name"], sc["cx"], sc["cy"], sc["r"], sc["stroke"])
        for tk in sc["ticks"]:
            print("   ", tk)


if __name__ == "__main__":
    main()
