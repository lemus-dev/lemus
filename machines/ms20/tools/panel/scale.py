#!/usr/bin/env python3
"""Absolute scale and origin of the panel frame (docs/panel.md section 1, "Scale").

Primary: the service manual's overall width, 569 mm (section 1, item 15). The widest parts are the
two side panels, whose outer faces show in the master as the silhouette edges against the black
background. Their front edges stand a little proud of the panel face (h_side below, from the dark
band of the inner faces): the edges are mapped onto the panel plane through the camera model
(camera.py) at that height, so the camera's 0.65 m distance does not inflate the width.

The frame is then: x = 0 on the left side panel's outer face; y = 0 on the top of the front
outline, the far (top) edge of the top cover as the master sees it straight-on; mm; the panel
face seen straight-on.

Usage: scale.py RECTIFY_FIT OUT_JSON"""
import sys, os
import numpy as np
import cv2

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C
import camera

H_SIDE = 2.5          # mm the side panels' front edges stand proud of the panel face (+-2.5)


def edge_points(g, x_lo, x_hi, y_lo, y_hi, rising, step=8, level=None):
    """Silhouette edge per row between black background (~6) and the side panel (~50-120):
    the crossing of 35 (half the panel edge's typical grey) with sub-pixel interpolation."""
    pts = []
    sm = cv2.blur(g, (1, 7))
    for y in range(y_lo, y_hi, step):
        row = sm[y, x_lo:x_hi]
        lv = level or 35.0
        idx = np.nonzero(row > lv)[0] if rising else np.nonzero(row > lv)[0]
        if len(idx) == 0:
            continue
        i = idx[0] if rising else idx[-1]
        if rising:
            if i == 0:
                continue
            x = x_lo + i - 1 + (lv - row[i - 1]) / (row[i] - row[i - 1])
        else:
            if i == len(row) - 1:
                continue
            x = x_lo + i + (row[i] - lv) / (row[i] - row[i + 1])
        pts.append((x, y))
    return np.array(pts)


def top_points(g, x_lo, x_hi, y_lo, y_hi, step=8, lv=35.0):
    """Top silhouette of the side panels (rows from the top, first crossing of lv)."""
    pts = []
    sm = cv2.blur(g, (7, 1))
    for x in range(x_lo, x_hi, step):
        col = sm[y_lo:y_hi, x]
        idx = np.nonzero(col > lv)[0]
        if len(idx) == 0 or idx[0] == 0:
            continue
        i = idx[0]
        pts.append((x, y_lo + i - 1 + (lv - col[i - 1]) / (col[i] - col[i - 1])))
    return np.array(pts)


def to_mm(cam, u, h):
    """Photo px of a point h mm proud of the panel -> panel mm (frame axes), parallax removed."""
    u = np.atleast_2d(u)
    return cam.photo_to_mm(u) + (cam.photo_to_mm_h(u, h) - cam.photo_to_mm_h(u, 0.0))


def main():
    fit = C.load_json(sys.argv[1])
    g = cv2.imread(C.MASTER, cv2.IMREAD_GRAYSCALE).astype(np.float32)
    # side panel outer edges, over the panel's height (photo rows 1060-2740)
    left = edge_points(g, 940, 1100, 1060, 2740, rising=True)
    right = edge_points(g, 6540, 6700, 1060, 2740, rising=False)
    s = 10.0
    for it in range(8):
        cam = camera.Camera(fit, s)
        xl = to_mm(cam, left, H_SIDE)[:, 0]
        xr = to_mm(cam, right, H_SIDE)[:, 0]
        w = np.median(xr) - np.median(xl)
        s = s * w / C.SPEC_W
    cam = camera.Camera(fit, s)
    xl = to_mm(cam, left, H_SIDE)[:, 0]; xr = to_mm(cam, right, H_SIDE)[:, 0]
    w_flat = np.median(to_mm(cam, right, 0.0)[:, 0]) - np.median(to_mm(cam, left, 0.0)[:, 0])
    w_5 = np.median(to_mm(cam, right, 5.0)[:, 0]) - np.median(to_mm(cam, left, 5.0)[:, 0])
    print('scale s = %.4f frame px/mm; camera at %s mm (x, y in frame mm, z = distance)' % (s, np.round(cam.C, 1)))
    print('side panel edges: left x %.2f (sd %.2f), right x %.2f (sd %.2f); width %.2f' % (
        np.median(xl), xl.std(), np.median(xr), xr.std(), np.median(xr) - np.median(xl)))
    print('width if the edges were flush: %.2f mm; if 5 mm proud: %.2f mm  (-> scale %+.2f %% / %+.2f %%)' % (
        w_flat, w_5, (w_flat / C.SPEC_W - 1) * 100, (w_5 / C.SPEC_W - 1) * 100))
    # the side panels' tops, and the top cover's far edge (top of the front outline, h = 0: as seen)
    tl = top_points(g, 1030, 1110, 700, 1100)
    tr = top_points(g, 6520, 6595, 700, 1100)
    side_top = np.median(np.r_[to_mm(cam, tl, H_SIDE)[:, 1], to_mm(cam, tr, H_SIDE)[:, 1]])
    tc = top_points(g, 1400, 6200, 600, 1000, step=20, lv=18.0)
    cover_top = to_mm(cam, tc, 0.0)[:, 1]
    ytop = float(np.median(cover_top))
    x0 = np.median(xl)
    origin_frame_px = (np.array([x0, ytop]) * s).tolist()
    print('top cover edge y %.2f (sd %.2f), side panel tops y %.2f (frame mm before the shift)' % (ytop, cover_top.std(), side_top))
    print('origin: frame px %s (left outer face, top cover edge)' % np.round(origin_frame_px, 1))
    C.save_json(dict(s=s, origin_frame_px=origin_frame_px, h_side=H_SIDE, camera_C_mm=cam.C.tolist(),
                     width_flush=w_flat, width_5mm=w_5,
                     left_edge_sd=float(xl.std()), right_edge_sd=float(xr.std()),
                     side_top_y=float(side_top - ytop), cover_top_sd=float(cover_top.std())),
                 sys.argv[2])


if __name__ == '__main__':
    main()
