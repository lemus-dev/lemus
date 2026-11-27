#!/usr/bin/env python3
"""The unit's outline and the panel's hardware in panel mm (docs/panel.md sections 2-3, 7).

  * edges (panel plate, side panel strips, top cover, lower lip): per profile, the sub-pixel
    position of the strongest step of the right sign inside a search window, on the rectified
    image; a straight line through each edge's samples. Raised edges are mapped with the camera at
    their height (the side panels' front edges: scale.H_SIDE).
  * screws, trimmer plugs, LEDs: a circle fitted to the radial edge of each (the outer edge where
    the part meets the panel), in the photo, mapped at the part's height.

Usage: measure_hw.py OUT_JSON"""
import sys, os
import numpy as np
import cv2
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C
import frame
import measure_lib as M
import panel_tables as PT
import scale as SC


def step_along(R, p0, p1, sign, smooth=0.3):
    """Sub-pixel position (mm along p0->p1) of the strongest brightness step of the given sign
    (+1 dark->bright going from p0 to p1)."""
    p0 = np.asarray(p0, float); p1 = np.asarray(p1, float)
    L = np.linalg.norm(p1 - p0); t = (p1 - p0) / L
    s = np.arange(0, L, 0.05)
    v = R.sample(R.g, p0[0] + t[0] * s, p0[1] + t[1] * s)
    k = max(1, int(smooth / 0.05))
    v = np.convolve(v, np.ones(k) / k, mode='same')
    d = np.gradient(v) * sign
    i = int(np.argmax(d[k:-k])) + k
    a, b, c = d[i - 1], d[i], d[i + 1]
    den = a - 2 * b + c
    off = 0.5 * (a - c) / den if den else 0.0
    return p0 + t * (s[i] + off * 0.05), float(d[i])


def edge_line(R, axis, at_values, lo, hi, sign):
    """Edge samples: axis 'x' = vertical edge (profiles along x at each y in at_values)."""
    pts = []
    for a in at_values:
        if axis == 'x':
            p, st = step_along(R, (lo, a), (hi, a), sign)
        else:
            p, st = step_along(R, (a, lo), (a, hi), sign)
        if st > 3:
            pts.append(p)
    pts = np.array(pts)
    c = 0 if axis == 'x' else 1
    med = np.median(pts[:, c])
    keep = np.abs(pts[:, c] - med) < 0.5
    pts = pts[keep]
    fit = np.polyfit(pts[:, 1 - c], pts[:, c], 1)
    res = pts[:, c] - np.polyval(fit, pts[:, 1 - c])
    return dict(value=float(np.median(pts[:, c])), slope=float(fit[0]), rms=float(res.std()), n=int(len(pts)))


def circle_part(Fr, g, c_mm, r_lo, r_hi, h, rising=False):
    """Circle on the strongest radial step round c_mm (photo), mapped at height h."""
    cpx = Fr.photo(np.array([c_mm]), h)[0]
    q = Fr.photo(np.array([c_mm, np.asarray(c_mm) + [1.0, 0]]), h)
    ppm = np.linalg.norm(q[1] - q[0])
    pts = []
    for a in np.arange(0, 360, 3):
        u = C.unit(a)
        rs = np.arange(r_lo * ppm, r_hi * ppm, 0.25)
        xs = (cpx[0] + u[0] * rs).astype(np.float32); ys = (cpx[1] + u[1] * rs).astype(np.float32)
        v = cv2.remap(g, xs[None, :], ys[None, :], cv2.INTER_LINEAR)[0]
        v = np.convolve(v, np.ones(5) / 5, mode='same')
        d = np.abs(np.gradient(v))
        i = int(np.argmax(d[4:-4])) + 4
        pts.append(cpx + u * rs[i])
    pts = np.array(pts)

    def f(p):
        return np.hypot(pts[:, 0] - p[0], pts[:, 1] - p[1]) - p[2]
    r = least_squares(f, [cpx[0], cpx[1], np.median(np.hypot(*(pts - cpx).T))], loss='soft_l1', f_scale=1.5)
    c = Fr.mm(r.x[:2], h)[0]
    return dict(c=c.tolist(), d=float(2 * r.x[2] / ppm), rms_px=float(np.sqrt(np.mean(f(r.x) ** 2))))


def main():
    Fr = frame.Frame()
    R = M.Rect()
    g = cv2.imread(C.MASTER, cv2.IMREAD_GRAYSCALE).astype(np.float32)
    sc = C.load_json(os.path.join(C.OUT, 'scale.json'))
    out = {}
    ys = np.arange(30, 195, 2.0); xs = np.arange(30, 540, 3.0)
    # panel plate edges (h = 0)
    out['plate_left'] = edge_line(R, 'x', ys, 10.5, 15.0, +1)
    out['plate_right'] = edge_line(R, 'x', ys, 553.5, 558.5, -1)
    out['plate_top'] = edge_line(R, 'y', xs, 17.0, 21.5, +1)
    out['plate_bottom'] = edge_line(R, 'y', xs, 196.0, 203.0, -1)
    # side panel strips: inner edges of their front faces (h = H_SIDE): measured on the rectified
    # image, then moved for the height (the parallax at x is (x - foot_x) * h / (D - h))
    D = Fr.cam.C[2]; fx = Fr.cam.C[0]
    li = edge_line(R, 'x', ys, 8.5, 11.5, -1)
    ri = edge_line(R, 'x', ys, 557.5, 561.0, +1)
    corr = lambda x: fx + (x - fx) * (D - SC.H_SIDE) / D
    out['side_left_inner'] = dict(li, value=float(corr(li['value'])))
    out['side_right_inner'] = dict(ri, value=float(corr(ri['value'])))
    # top silhouette (cover and side panels against the black background): per column, where the
    # smoothed grey first exceeds 18 (background ~7, cover ~30). The cover's far edge is y = 0 by
    # definition; its corners are chamfered; the side panels' tops sit lower.
    gs = cv2.GaussianBlur(R.g, (9, 9), 2.5)

    def top_y(x):
        yy = np.arange(-4, 22, 0.05)
        v = R.sample(gs, np.full_like(yy, x), yy)
        i = np.nonzero(v > 18)[0]
        return float(yy[i[0]]) if len(i) else None
    flat = [top_y(x) for x in np.arange(60, 510, 10.0)]
    y_flat = float(np.median([v for v in flat if v is not None]))
    chl = np.array([(x, top_y(x)) for x in np.arange(17, 30.1, 0.5)])
    chr_ = np.array([(x, top_y(x)) for x in np.arange(539, 553.1, 0.5)])
    side_l = float(np.median([top_y(x) for x in np.arange(0.5, 8, 0.5)]))
    side_r = float(np.median([top_y(x) for x in np.arange(561, 568.6, 0.5)]))
    pl = np.polyfit(chl[:, 0], chl[:, 1], 1); pr_ = np.polyfit(chr_[:, 0], chr_[:, 1], 1)
    out['top_silhouette'] = dict(y_flat=y_flat, side_top_left=side_l - y_flat, side_top_right=side_r - y_flat,
                                 chamfer_left=[float(pl[0]), float(pl[1] - y_flat)], chamfer_right=[float(pr_[0]), float(pr_[1] - y_flat)],
                                 chamfer_left_deg=float(np.degrees(np.arctan(abs(pl[0])))), chamfer_right_deg=float(np.degrees(np.arctan(abs(pr_[0])))))
    print('top silhouette: flat at %.2f; chamfers %.1f / %.1f deg; side panel tops %.2f / %.2f' % (
        y_flat, out['top_silhouette']['chamfer_left_deg'], out['top_silhouette']['chamfer_right_deg'], side_l - y_flat, side_r - y_flat))
    # screws, plugs, LEDs
    sc_out = {}
    for k, (x, y) in PT.SCREWS.items():
        sc_out[k] = circle_part(Fr, g, (x, y), 1.5, 4.8, 2.0)
    out['screws'] = sc_out
    pl = {}
    for k, (x, y, vr) in PT.PLUGS.items():
        pl[k] = circle_part(Fr, g, (x, y), 1.5, 4.5, 1.0)
        pl[k]['vr'] = vr
    out['plugs'] = pl
    le = {}
    for k, (x, y, what) in PT.LEDS.items():
        le[k] = circle_part(Fr, g, (x, y), 0.6, 2.6, 1.0)
        le[k]['what'] = what
    out['leds'] = le
    for k, v in out.items():
        if isinstance(v, dict) and 'value' in v:
            print('%-18s %.2f (slope %+.5f, rms %.3f, n %d)' % (k, v['value'], v['slope'], v['rms'], v['n']))
    for grp in ('screws', 'plugs', 'leds'):
        for k, v in out[grp].items():
            print('%-18s c=(%.2f, %.2f) d=%.2f rms %.1f px' % (k, *v['c'], v['d'], v['rms_px']))
    C.save_json(out, sys.argv[1])


if __name__ == '__main__':
    main()
