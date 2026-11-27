"""Helpers for measuring on the rectified master (10 px/mm, panel mm; docs/panel.md section 1).

The panel is grey and the print is white: "ink" is a pixel brighter than the local panel level
(median over 5 mm) by more than INK. Every function takes and returns panel mm."""
import os
import numpy as np
import cv2
from skimage.morphology import skeletonize

import ms20_common as C

INK = 55.0


class Rect:
    def __init__(self, path=os.path.join(C.OUT, 'master_rect.png')):
        meta = C.load_json(os.path.splitext(path)[0] + '.json')
        self.x0, self.y0, self.P = meta['x0'], meta['y0'], meta['ppm']
        self.bgr = cv2.imread(path, cv2.IMREAD_COLOR)
        self.g = cv2.cvtColor(self.bgr, cv2.COLOR_BGR2GRAY).astype(np.float32)
        self.bg = cv2.medianBlur(self.g.astype(np.uint8), 51).astype(np.float32)
        self.ink = self.g - self.bg
        self.mask = (self.ink > INK).astype(np.uint8)
        n, lab, st, cen = cv2.connectedComponentsWithStats(self.mask, 8)
        self.lab, self.st, self.cen = lab, st, cen

    # pixel centre (i, j) = (x0 + (j + 0.5)/P, y0 + (i + 0.5)/P)
    def px(self, x, y):
        return (x - self.x0) * self.P - 0.5, (y - self.y0) * self.P - 0.5

    def mm(self, px, py):
        return self.x0 + (px + 0.5) / self.P, self.y0 + (py + 0.5) / self.P

    def sample(self, img, xs, ys):
        px, py = self.px(np.asarray(xs, float), np.asarray(ys, float))
        return cv2.remap(img, np.float32(px).reshape(1, -1), np.float32(py).reshape(1, -1), cv2.INTER_LINEAR)[0]

    def comps_in(self, box, max_h=None, max_w=None, min_area=6):
        """Print components whose centroid lies in box (mm) and that fit max_h/max_w (mm)."""
        x0, y0 = self.px(box[0], box[1]); x1, y1 = self.px(box[2], box[3])
        out = []
        sel = ((self.cen[:, 0] >= x0) & (self.cen[:, 0] <= x1) & (self.cen[:, 1] >= y0) & (self.cen[:, 1] <= y1))
        for i in np.nonzero(sel)[0]:
            if i == 0:
                continue
            l, t, w, h, a = self.st[i]
            if a < min_area:
                continue
            if max_h is not None and h > max_h * self.P:
                continue
            if max_w is not None and w > max_w * self.P:
                continue
            bx0, by0 = self.mm(l - 0.5, t - 0.5); bx1, by1 = self.mm(l + w - 0.5, t + h - 0.5)
            out.append(dict(i=int(i), bbox=[bx0, by0, bx1, by1], area=int(a),
                            c=list(self.mm(*self.cen[i]))))
        return out

    def ink_bbox(self, comps):
        """Sub-pixel ink box of a set of components: half-maximum edges of the ink profiles."""
        ids = [c['i'] for c in comps]
        x0 = min(c['bbox'][0] for c in comps) - 0.6; x1 = max(c['bbox'][2] for c in comps) + 0.6
        y0 = min(c['bbox'][1] for c in comps) - 0.6; y1 = max(c['bbox'][3] for c in comps) + 0.6
        px0, py0 = self.px(x0, y0); px1, py1 = self.px(x1, y1)
        px0, py0, px1, py1 = int(px0), int(py0), int(np.ceil(px1)), int(np.ceil(py1))
        sub = self.ink[py0:py1, px0:px1].copy()
        lab = self.lab[py0:py1, px0:px1]
        keep = np.isin(lab, ids)
        # grow the component mask by 1 px so the anti-aliased rim counts
        keep = cv2.dilate(keep.astype(np.uint8), np.ones((3, 3), np.uint8)) > 0
        sub[~keep] = 0
        sub = np.clip(sub, 0, None)
        pk = np.percentile(sub[keep], 95) if keep.any() else 1
        colp = sub.max(0); rowp = sub.max(1)

        def edges(p, lo_off):
            h = 0.5 * pk
            idx = np.nonzero(p >= h)[0]
            if len(idx) == 0:
                return None
            a, b = idx[0], idx[-1]
            ea = a - (p[a] - h) / max(p[a] - p[a - 1], 1e-6) if a > 0 else a
            eb = b + (p[b] - h) / max(p[b] - p[b + 1], 1e-6) if b < len(p) - 1 else b
            return lo_off + ea, lo_off + eb
        ex = edges(colp, px0); ey = edges(rowp, py0)
        if ex is None or ey is None:
            return None
        (ax, ay) = self.mm(ex[0], ey[0]); (bx, by) = self.mm(ex[1], ey[1])
        return [ax, ay, bx, by], float(pk)

    def stroke_width(self, comps):
        """Median stroke width (mm) of components: 2 x distance transform on the skeleton."""
        ids = [c['i'] for c in comps]
        x0 = min(c['bbox'][0] for c in comps) - 0.5; x1 = max(c['bbox'][2] for c in comps) + 0.5
        y0 = min(c['bbox'][1] for c in comps) - 0.5; y1 = max(c['bbox'][3] for c in comps) + 0.5
        px0, py0 = self.px(x0, y0); px1, py1 = self.px(x1, y1)
        m = np.isin(self.lab[int(py0):int(py1) + 1, int(px0):int(px1) + 1], ids).astype(np.uint8)
        if m.sum() == 0:
            return None
        dt = cv2.distanceTransform(m, cv2.DIST_L2, 5)
        sk = skeletonize(m > 0)
        v = dt[sk]
        if len(v) == 0:
            return None
        return float(2 * np.median(v) / self.P)

    def colour(self, box):
        x0, y0 = self.px(box[0], box[1]); x1, y1 = self.px(box[2], box[3])
        a = self.bgr[int(y0):int(y1), int(x0):int(x1)].reshape(-1, 3)
        return np.median(a, 0)[::-1]


def fit_line_profile(R, p0, p1, half=1.2, step=0.4, min_ink=INK):
    """Stroke centre line along a roughly straight printed line from p0 to p1 (mm): per sample, the
    ink-weighted centre of a perpendicular profile. Returns points (N, 2) and widths (FWHM, mm)."""
    p0 = np.asarray(p0, float); p1 = np.asarray(p1, float)
    L = np.linalg.norm(p1 - p0)
    t = (p1 - p0) / L; n = np.array([-t[1], t[0]])
    offs = np.arange(-half, half + 1e-9, 0.05)
    pts, wid = [], []
    for s in np.arange(0, L + 1e-9, step):
        c = p0 + t * s
        xs = c[0] + n[0] * offs; ys = c[1] + n[1] * offs
        v = R.sample(R.ink, xs, ys)
        pk = v.max()
        if pk < min_ink:
            continue
        on = v > 0.5 * pk
        i = int(np.argmax(v))
        a = i
        while a > 0 and on[a - 1]:
            a -= 1
        b = i
        while b < len(v) - 1 and on[b + 1]:
            b += 1
        if a == 0 or b == len(v) - 1:
            continue
        w = np.clip(v[a - 1:b + 2], 0, None)
        o = (w * offs[a - 1:b + 2]).sum() / w.sum()
        # FWHM with linear interpolation at both edges
        h = 0.5 * pk
        ea = offs[a] - (v[a] - h) / max(v[a] - v[a - 1], 1e-6) * 0.05
        eb = offs[b] + (v[b] - h) / max(v[b] - v[b + 1], 1e-6) * 0.05
        pts.append(c + n * o); wid.append(eb - ea)
    return np.array(pts), np.array(wid)
