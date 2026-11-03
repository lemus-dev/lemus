#!/usr/bin/env python3
"""Rectify the straight-on master photo of the TR-909 top into a millimetre grid.

Usage: rectify.py PHOTO OUT_PNG OUT_JSON [--ppm 8]

Finds the four outer edges of the top view (outer edges of the two olive side panels,
rear edge and front edge) with sub-pixel gradient fits along every row/column, fits a
straight line to each (robust, 2 passes), intersects them for the corners and warps the
quadrilateral onto a 486.0 mm wide canvas (486 mm = official width, owner's manual p. 39 /
service notes p. 1). The depth in mm is taken from the photo at the same scale (the photo
is square-on to the sloping top, so x and y share one scale; this was cross-checked against
switch positions on the 1:1 switch-board artwork, see docs/panel.md). Also reports the inner
edges of the side panels (side panel / top plate boundary)."""
import json, sys
import numpy as np
from PIL import Image
import cv2

W_MM = 486.0


def subpix_peak(v, i):
    if 0 < i < len(v) - 1:
        a, b, c = v[i - 1], v[i], v[i + 1]
        d = a - 2 * b + c
        if d != 0:
            return i + 0.5 * (a - c) / d
    return float(i)


def edge_positions(gray, axis, lo, hi, sign, span):
    """For each line (row if axis==1, column if axis==0) in span, find the strongest
    gradient of the given sign between lo..hi. Returns (pos_along, edge_pos)."""
    out = []
    g = cv2.GaussianBlur(gray, (0, 0), 1.0)
    for t in span:
        prof = g[t, lo:hi] if axis == 1 else g[lo:hi, t]
        d = np.diff(prof) * sign
        i = int(np.argmax(d))
        if d[i] < 8:
            continue
        out.append((t, lo + subpix_peak(d, i) + 0.5))
    return np.array(out)


def fit_line(pts):
    # fit pos = a*t + b, two-pass with outlier rejection
    t, p = pts[:, 0], pts[:, 1]
    for _ in range(3):
        A = np.vstack([t, np.ones_like(t)]).T
        (a, b), *_ = np.linalg.lstsq(A, p, rcond=None)
        r = p - (a * t + b)
        keep = np.abs(r) < max(0.6, 2.5 * np.std(r))
        t, p = t[keep], p[keep]
    return a, b, float(np.std(r[keep])) if keep.any() else 0.0, len(t)


def main():
    photo, out_png, out_json = sys.argv[1:4]
    ppm = float(sys.argv[sys.argv.index("--ppm") + 1]) if "--ppm" in sys.argv else 8.0
    rgb = np.asarray(Image.open(photo).convert("RGB")).astype(np.float32)
    gray = rgb.mean(axis=2)
    H, W = gray.shape
    rows = range(200, H - 200, 2)
    cols = range(300, W - 300, 2)
    # outer edges (white background <-> unit)
    L = edge_positions(gray, 1, 60, 160, -1, rows)          # white -> olive (darker)
    R = edge_positions(gray, 1, W - 160, W - 60, +1, rows)  # olive -> white
    T = edge_positions(gray, 0, 60, 160, -1, cols)          # white -> panel
    B = edge_positions(gray, 0, H - 135, H - 70, +1, cols)  # panel -> white (front edge ~y 1365)
    # inner edges (olive side panel <-> light top plate)
    Li = edge_positions(gray, 1, 125, 175, +1, rows)
    Ri = edge_positions(gray, 1, W - 175, W - 125, -1, rows)
    lines = {}
    for k, pts in dict(left=L, right=R, top=T, bottom=B, left_inner=Li, right_inner=Ri).items():
        a, b, sd, n = fit_line(pts)
        lines[k] = dict(a=float(a), b=float(b), sd=sd, n=n)
        print(f"{k:12s} pos = {a:+.5f}*t + {b:8.2f}   sd {sd:.2f} px  n={n}")

    def corner(v, h):  # v: x = a*y + b ; h: y = a*x + b
        av, bv = lines[v]["a"], lines[v]["b"]
        ah, bh = lines[h]["a"], lines[h]["b"]
        # x = av*y + bv, y = ah*x + bh
        x = (av * bh + bv) / (1 - av * ah)
        y = ah * x + bh
        return [x, y]

    tl, tr = corner("left", "top"), corner("right", "top")
    bl, br = corner("left", "bottom"), corner("right", "bottom")
    wpx = 0.5 * (np.hypot(tr[0] - tl[0], tr[1] - tl[1]) + np.hypot(br[0] - bl[0], br[1] - bl[1]))
    hpx = 0.5 * (np.hypot(bl[0] - tl[0], bl[1] - tl[1]) + np.hypot(br[0] - tr[0], br[1] - tr[1]))
    s = wpx / W_MM
    H_MM = round(hpx / s, 2)
    print("corners", tl, tr, bl, br)
    print(f"width {wpx:.2f} px -> {s:.4f} px/mm ; depth {hpx:.2f} px -> {H_MM} mm")
    src = np.float32([tl, tr, br, bl])
    dst = np.float32([[0, 0], [W_MM * ppm, 0], [W_MM * ppm, H_MM * ppm], [0, H_MM * ppm]])
    M = cv2.getPerspectiveTransform(src, dst)
    out = cv2.warpPerspective(np.asarray(Image.open(photo).convert("RGB")), M,
                              (int(round(W_MM * ppm)), int(round(H_MM * ppm))),
                              flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_REPLICATE)
    Image.fromarray(out).save(out_png)
    # inner side-panel edges in mm (mid-height)
    ymid = 0.5 * (tl[1] + bl[1])
    def to_mm(x, y):
        p = M @ np.array([x, y, 1.0]); return p[:2] / p[2] / ppm
    li = to_mm(lines["left_inner"]["a"] * ymid + lines["left_inner"]["b"], ymid)
    ri = to_mm(lines["right_inner"]["a"] * ymid + lines["right_inner"]["b"], ymid)
    res = dict(photo=photo, px_per_mm_photo=round(float(s), 4), ppm_rectified=ppm,
               width_mm=W_MM, depth_mm=H_MM, corners_px=dict(tl=tl, tr=tr, br=br, bl=bl),
               homography_photo_to_rectified_px=M.tolist(), edge_lines=lines,
               side_panel_inner_x_mm=[round(float(li[0]), 2), round(float(ri[0]), 2)])
    json.dump(res, open(out_json, "w"), indent=1)
    print("side panel inner edges (mm):", res["side_panel_inner_x_mm"])


if __name__ == "__main__":
    main()
