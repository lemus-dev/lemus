#!/usr/bin/env python3
"""Patch panel jacks in panel mm (docs/panel.md sections 1 and 7).

Each jack (1/4 in phone jack, hex nut on the panel) is measured in the photo:
  * the opening: the dark hole's edge, fitted with a circle on the arc that faces the camera only
    (on the far arc the bushing's inner wall shows and eats into the dark area);
  * the nut: a regular hexagon fitted to the nut's outline (centre, across-flats, rotation).
The opening stands proud of the panel; its foot on the panel is found with the camera model at the
effective height H_HOLE, the one at which the jack grid has the same scale as the legends centred
on the jacks (check in docs/panel.md section 1). The printed legends sit about 0.6 mm left of the
jack holes: a print-to-hole registration offset of this unit, kept as measured.

Usage: measure_jacks.py OUT_JSON"""
import sys, os
import numpy as np
import cv2
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C
import frame
import photo_centres as PCm

H_HOLE = 1.8          # effective height of the opening's centre (mm), see the docstring
H_NUT = 1.2           # mid-height of the nut outline (mm), half a 2.4 mm nut


def hex_fit(pts, c0, r0):
    pts = np.asarray(pts)

    def f(p):
        d = pts - p[:2]
        rr = np.hypot(d[:, 0], d[:, 1])
        ang = np.arctan2(d[:, 1], d[:, 0]) - p[3]
        phi = (ang + np.pi / 6) % (np.pi / 3) - np.pi / 6
        return rr - p[2] / np.cos(phi)
    best = None
    for th in np.radians(np.arange(0, 60, 7.5)):
        r = least_squares(f, [c0[0], c0[1], r0 * 0.93, th], loss='soft_l1', f_scale=2.0)
        if best is None or r.cost < best.cost:
            best = r
    res = f(best.x)
    return best.x, float(np.sqrt(np.mean(res ** 2)))


def main():
    Fr = frame.Frame()
    g = PCm.load_gray()
    pc = C.load_json(os.path.join(C.OUT, 'photo_centres.json'))
    foot = Fr.photo(Fr.cam.C[:2][None, :])[0]
    out = {}
    for k, v in pc['jacks'].items():
        c = np.array(v['hole'])
        pts = PCm.radial_edges(g, c, 10, 40, rising=True, step=1.0)
        d = np.hypot(*(pts - c).T)
        u = (foot - c) / np.linalg.norm(foot - c)
        ang = np.degrees(np.arccos(np.clip(((pts - c) @ u) / np.maximum(d, 1e-9), -1, 1)))
        sel = (ang <= 100) & (np.abs(d - np.median(d)) < 4)
        (hx, hy, hr), hrms = PCm.circle_fit(pts[sel])
        hole_mm = Fr.mm([hx, hy], H_HOLE)[0]
        # local scale at the jack: px per mm in the photo
        q = Fr.photo(np.array([hole_mm, hole_mm + [1.0, 0]]), H_HOLE)
        ppm = np.linalg.norm(q[1] - q[0])
        # nut hexagon on the outline points (photo px), then into mm at the nut's height
        npts = np.array(v['nut_pts'])
        (nx, ny, nri, nth), nrms = hex_fit(npts, v['nut'], v['nut_r'])
        nut_mm = Fr.mm([nx, ny], H_NUT)[0]
        q2 = Fr.photo(np.array([nut_mm, nut_mm + [1.0, 0]]), H_NUT)
        ppm2 = np.linalg.norm(q2[1] - q2[0])
        out[k] = dict(j=v['j'], c=hole_mm.tolist(), hole_d=float(2 * hr / ppm), hole_rms_px=hrms,
                      nut_c=nut_mm.tolist(), nut_af=float(2 * nri / ppm2), nut_rot_deg=float(np.degrees(nth) % 60),
                      nut_rms_px=nrms)
        print('%-9s %-4s c=(%7.2f,%7.2f) hole %.2f  nut c=(%7.2f,%7.2f) A/F %.2f rot %4.1f rms %.1f' % (
            k, v['j'], *hole_mm, out[k]['hole_d'], *nut_mm, out[k]['nut_af'], out[k]['nut_rot_deg'], nrms))
    af = [o['nut_af'] for k, o in out.items() if k != 'sigout']
    print('nut A/F median %.2f (sd %.2f), hole median %.2f' % (np.median(af), np.std(af), np.median([o['hole_d'] for o in out.values()])))
    C.save_json(dict(h_hole=H_HOLE, h_nut=H_NUT, jacks=out), sys.argv[1])


if __name__ == '__main__':
    main()
