#!/usr/bin/env python3
"""Remove the residual non-linear distortion from the rectified master photo.

Usage: regularise.py RECT_PNG RECT_JSON OUT_PNG OUT_JSON

Why: after the 4-corner homography (rectify.py) the Soundgas master still bends printed lines: the
long bars bow by up to 0.9 mm (their sagitta) and the 46 mm instrument sections measure 45.35 to
46.48 mm. The pattern is the one a pincushion lens gives about the middle of the picture: lines
above the middle bend up at their ends, lines below it bend down, sections near the middle come out
narrow and outer ones wide (crosscheck.py: a radial model about the photo's centre takes out about
half of it on its own). It is in the photo, not in the print:
  * the small pots on the 1:1 voicing-board artwork (service notes p. 10; 25 of the 27 found) fit
    the printed tick-ring centres of the regularised master to 0.09 mm rms in x, against 0.38 mm on
    the master as rectified (crosscheck.py --pots): the rings are on the pot grid, as the knobs need;
  * the other straight-on photos (Commons "RolandTR-909-front", Soundgas #403957) show the same
    pattern with about 60 % of the master's amplitude, so a homography from them to the master
    can't see it (it is common to them), but their own rings miss the pots by 0.23-0.24 mm rms;
    once each is given its own radial lens distortion as well as a homography, they support 50-75 %
    of the correction below (the rest is what two radial terms can't model);
  * Commons "Roland TR-909 (large)", a perspective-corrected view from an oblique shot (so with its
    distortion elsewhere), has the sections regular to 0.1 mm.

Method ("rubber sheeting" on the panel's own print):
  * vertical keylines: the 7 instrument dividers and the 7 step-section dividers that sit on
    key-pair boundaries are put on the design grid x = K1 + (j - 0.5) * 23.0 mm (key pitch
    23.0 mm, confirmed by the 1:1 switch-board artwork in the service notes and by the
    486 mm case width); other vertical lines are only straightened (kept at their mean x);
  * horizontal edges (header bars, the 4 scale bars, the number bar, legend boxes, keylines)
    are straightened and kept at their mean y;
  * the corrections are interpolated linearly between neighbouring lines and tapered to 0 at
    the outer edges of the case (which stay at 0..486 x 0..depth mm).
The result is resampled onto the same 8 px/mm grid. Every later measurement is made on it."""
import json, sys
import numpy as np
from PIL import Image
import cv2

P = 8.0       # px/mm of the rectified master
PITCH = 23.0  # step-key pitch, mm


def subpix(v, i):
    if 0 < i < len(v) - 1:
        a, b, c = v[i - 1], v[i], v[i + 1]
        d = a - 2 * b + c
        if d:
            return i + 0.5 * (a - c) / d
    return float(i)


class Measurer:
    def __init__(self, gray):
        self.g = cv2.GaussianBlur(gray, (0, 0), 0.8)

    def vline_x(self, x_est, y, half=1.6, hh=0.4):
        a, b = int((x_est - half) * P), int((x_est + half) * P)
        row = self.g[int((y - hh) * P):int((y + hh) * P) + 1, a:b].mean(axis=0)
        i = int(np.argmin(row))
        if i <= 1 or i >= len(row) - 2 or np.median(row) - row[i] < 35:
            return None
        return float((a + subpix(-row, i) + 0.5) / P)

    def hedge_y(self, x, y_est, sign, half=1.2, hw=0.4):
        a, b = int((y_est - half) * P), int((y_est + half) * P)
        col = self.g[a:b, int((x - hw) * P):int((x + hw) * P) + 1].mean(axis=1)
        d = np.diff(col) * sign
        i = int(np.argmax(d))
        if i <= 0 or i >= len(d) - 1 or d[i] < 15:
            return None
        return float((a + subpix(d, i) + 0.5) / P)

    def hline_y(self, x, y_est, half=1.0, hw=0.4):   # thin dark keyline
        a, b = int((y_est - half) * P), int((y_est + half) * P)
        col = self.g[a:b, int((x - hw) * P):int((x + hw) * P) + 1].mean(axis=1)
        i = int(np.argmin(col))
        if i <= 1 or i >= len(col) - 2 or np.median(col) - col[i] < 30:
            return None
        return float((a + subpix(-col, i) + 0.5) / P)


# ---- line definitions (approximate positions in the rectified master, mm) ----------------
# vertical: name, band, x_est, y-range, grid index j (None = free line)
INST_DIV = [35.26, 70.70, 117.09, 163.46, 209.26, 254.61, 300.09, 346.11, 392.59, 440.77]
STEP_DIV = [116.86, 163.00, 208.98, 254.74, 300.59, 346.68, 392.84, 440.70]
VLINES = []
for k, x in enumerate(INST_DIV):
    VLINES.append(('inst_div_%d' % k, 'inst', x, (104.0, 151.0), 2 * (k - 1) if 2 <= k <= 8 else None))
for k, x in enumerate(STEP_DIV):
    VLINES.append(('step_div_%d' % k, 'step', x, (228.0, 253.5), 2 * (k + 1) if k <= 6 else None))
VLINES.append(('leftbox_left', 'step', 35.4, (197.0, 267.0), None))
VLINES.append(('leftbox_right', 'step', 68.5, (227.0, 267.0), None))

# horizontal: name, kind ('edge+'/'edge-' gradient sign, or 'line' thin keyline), y_est, x-ranges
GAPS_INST = [(d - 2.2, d + 2.2) for d in INST_DIV]


def ranges_minus(lo, hi, gaps):
    out, a = [], lo
    for g0, g1 in sorted(gaps):
        if g1 <= a or g0 >= hi:
            continue
        if g0 > a:
            out.append((a, g0))
        a = max(a, g1)
    if a < hi:
        out.append((a, hi))
    return out


ROWGAPS = {1: [209.0, 346.5], 2: [140.0, 209.0, 277.5, 346.5], 3: [254.75], 4: [163.2, 254.75, 346.5]}
ROWTOP = {1: 195.45, 2: 203.0, 3: 210.5, 4: 218.0}
HLINES = [('hdr_top', 'edge-', 97.3, ranges_minus(37, 440, GAPS_INST)),
          ('hdr_bot', 'edge+', 102.8, ranges_minus(37, 440, GAPS_INST))]
for r in (1, 2, 3, 4):
    rg = ranges_minus(79, 438, [(gx - 2.0, gx + 2.0) for gx in ROWGAPS[r]])
    HLINES.append(('row%d_top' % r, 'edge-', ROWTOP[r], rg))
    HLINES.append(('row%d_bot' % r, 'edge+', ROWTOP[r] + 6.52, rg))
# rows of knob legends (TUNE/LEVEL..., ATTACK/DECAY...): each word gives one sample of the
# common cap line (top) and baseline (bottom); spans are approximate word extents (mm)
LBL_UP = [(80.0, 87.0), (100.2, 108.6), (126.4, 133.4), (146.5, 154.8), (172.6, 179.6), (192.6, 200.9),
          (218.4, 225.2), (238.2, 246.4), (263.8, 270.8), (283.7, 291.8), (308.5, 316.7), (329.4, 337.6),
          (375.9, 384.1), (401.1, 409.4), (422.4, 430.6)]
LBL_LO = [(78.1, 88.2), (100.2, 108.7), (126.1, 133.2), (145.8, 156.1), (171.9, 180.2), (217.5, 225.9),
          (262.9, 271.2), (351.6, 365.5), (373.1, 386.6), (395.8, 405.4), (407.3, 414.2), (419.0, 425.1),
          (426.8, 433.8)]
TEXTROWS = [('lbl_up', 109.4, LBL_UP), ('lbl_lo', 129.3, LBL_LO)]
HLINES += [('numbar_top', 'edge-', 255.9, [(76, 438)]),
           ('numbar_bot', 'edge+', 261.4, [(76, 438)]),
           ('leftbox_top', 'line', 195.6, [(37, 66)]),
           ('leftbox_bottom', 'line', 268.45, [(37, 67)])]


def collect(meas):
    V, H = {}, {}
    for name, band, x0, (ya, yb), j in VLINES:
        pts = []
        for y in np.arange(ya, yb, 0.75):
            x = meas.vline_x(x0, y, half=1.3)
            if x is not None and abs(x - x0) < 0.9:
                pts.append((x, float(y)))
        V[name] = dict(band=band, j=j, pts=pts)
    for name, kind, y0, rgs in HLINES:
        pts = []
        for a, b in rgs:
            for x in np.arange(a, b, 1.0):
                if kind == 'line':
                    y = meas.hline_y(x, y0)
                else:
                    y = meas.hedge_y(x, y0, -1 if kind == 'edge-' else +1)
                if y is not None and abs(y - y0) < 1.0:
                    pts.append((float(x), y))
        H[name] = dict(pts=pts)
    for name, y0, spans in TEXTROWS:
        tops, bots = [], []
        for x0, x1 in spans:
            a, b = int((y0 - 2.4) * P), int((y0 + 2.4) * P)
            prof = 255 - meas.g[a:b, int((x0 + 0.3) * P):int((x1 - 0.3) * P)].mean(axis=1)
            d = np.diff(prof)
            i, k = int(np.argmax(d)), int(np.argmin(d))
            xm = 0.5 * (x0 + x1)
            tops.append((xm, float((a + subpix(d, i) + 0.5) / P)))
            bots.append((xm, float((a + subpix(-d, k) + 0.5) / P)))
        H[name + '_top'] = dict(pts=tops)
        H[name + '_bot'] = dict(pts=bots)
    return V, H


def robust_polyfit(t, v, deg):
    t, v = np.asarray(t), np.asarray(v)
    keep = np.ones(len(t), bool)
    for _ in range(3):
        c = np.polyfit(t[keep], v[keep], deg)
        r = v - np.polyval(c, t)
        s = max(0.04, 2.5 * np.std(r[keep]))
        keep = np.abs(r) < s
    return c, float(np.std(r[keep])), int(keep.sum())


def main():
    rect_png, rect_json, out_png, out_json = sys.argv[1:5]
    R = json.load(open(rect_json))
    W_MM, H_MM = R['width_mm'], R['depth_mm']
    rgb = np.asarray(Image.open(rect_png).convert('RGB'))
    meas = Measurer(rgb.astype(np.float32).mean(axis=2))
    V, H = collect(meas)
    # fit straight lines x(y) for vertical lines, quadratic y(x) for horizontal ones
    for name, d in V.items():
        a = np.array(d['pts'])
        d['fit'], d['sd'], d['n'] = robust_polyfit(a[:, 1], a[:, 0], 1)
        d['mean'] = float(np.mean(a[:, 0]))
        d['yr'] = (float(a[:, 1].min()), float(a[:, 1].max()))
    for name, d in H.items():
        a = np.array(d['pts'])
        d['fit'], d['sd'], d['n'] = robust_polyfit(a[:, 0], a[:, 1], 2 if len(a) < 40 else (6 if len(a) > 200 else 3))
        d['mean'] = float(np.mean(np.polyval(d['fit'], np.linspace(a[:, 0].min(), a[:, 0].max(), 200))))
        d['xr'] = (float(a[:, 0].min()), float(a[:, 0].max()))
    # design grid: K1 (centre of key 1) from all grid lines
    ks = [d['mean'] - (d['j'] - 0.5) * PITCH for d in V.values() if d['j'] is not None]
    K1 = float(np.mean(ks))
    for d in V.values():
        d['design'] = K1 + (d['j'] - 0.5) * PITCH if d['j'] is not None else d['mean']
    # tie the right-hand end line of both bands together
    tie = 0.5 * (V['inst_div_9']['mean'] + V['step_div_7']['mean'])
    V['inst_div_9']['design'] = V['step_div_7']['design'] = tie
    for d in H.values():
        d['design'] = d['mean']

    # ---- correction fields on a 1 mm grid ------------------------------------------------
    gx = np.arange(0, W_MM + 1.0, 1.0)
    gy = np.arange(0, H_MM + 1.0, 1.0)

    def band_dx(band, y):
        lines = [(np.polyval(d['fit'], np.clip(y, *d['yr'])), d['design']) for d in V.values() if d['band'] == band]
        lines.sort()
        xs = [0.0] + [l[0] for l in lines] + [W_MM]
        dx = [0.0] + [l[1] - l[0] for l in lines] + [0.0]
        return np.interp(gx, xs, dx)

    DX = np.zeros((len(gy), len(gx)))
    inst_lo, step_hi = 151.0, 228.0
    Fi = {y: band_dx('inst', y) for y in gy}
    Fs = {y: band_dx('step', y) for y in gy}
    for iy, y in enumerate(gy):
        if y <= inst_lo:
            DX[iy] = Fi[y]
        elif y >= step_hi:
            DX[iy] = Fs[y]
        else:
            t = (y - inst_lo) / (step_hi - inst_lo)
            DX[iy] = (1 - t) * band_dx('inst', inst_lo) + t * band_dx('step', step_hi)
    DY = np.full_like(DX, np.nan)
    have = np.zeros(len(gx), bool)
    for ix, x in enumerate(gx):
        pts = []
        for d in H.values():
            x0, x1 = d['xr']
            if not (x0 - 3.0 <= x <= x1 + 3.0):
                continue          # a line only constrains the columns it actually spans
            yl = float(np.polyval(d['fit'], np.clip(x, x0, x1)))
            pts.append((yl, d['design'] - yl))
        if not pts:
            continue
        pts.sort()
        ys = [0.0] + [p[0] for p in pts] + [H_MM]
        dys = [0.0] + [p[1] for p in pts] + [0.0]
        DY[:, ix] = np.interp(gy, ys, dys)
        have[ix] = True
    # columns outside every line (case margins): taper the nearest column to 0 at the case edge
    idx = np.where(have)[0]
    for ix in range(len(gx)):
        if have[ix]:
            continue
        if ix < idx[0]:
            DY[:, ix] = DY[:, idx[0]] * gx[ix] / gx[idx[0]]
        elif ix > idx[-1]:
            DY[:, ix] = DY[:, idx[-1]] * (W_MM - gx[ix]) / (W_MM - gx[idx[-1]])
        else:
            j = idx[np.searchsorted(idx, ix)]; i = idx[np.searchsorted(idx, ix) - 1]
            t = (gx[ix] - gx[i]) / (gx[j] - gx[i]); DY[:, ix] = (1 - t) * DY[:, i] + t * DY[:, j]

    # ---- resample: design pixel -> source pixel (fixed-point inversion) -------------------
    Hp, Wp = rgb.shape[:2]
    # field value at pixel (i, j) = bilinear sample of the 1 mm grid at ((j+0.5)/P, (i+0.5)/P) mm
    mx_, my_ = np.meshgrid((np.arange(Wp, dtype=np.float32) + 0.5) / P, (np.arange(Hp, dtype=np.float32) + 0.5) / P)
    big_dx = cv2.remap(DX.astype(np.float32), mx_, my_, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    big_dy = cv2.remap(DY.astype(np.float32), mx_, my_, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    X, Y = np.meshgrid(np.arange(Wp, dtype=np.float32), np.arange(Hp, dtype=np.float32))
    sx, sy = X.copy(), Y.copy()
    for _ in range(3):
        ddx = cv2.remap(big_dx, sx, sy, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
        ddy = cv2.remap(big_dy, sx, sy, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
        sx, sy = X - ddx * P, Y - ddy * P
    out = cv2.remap(rgb, sx, sy, cv2.INTER_CUBIC, borderMode=cv2.BORDER_REPLICATE)
    Image.fromarray(out).save(out_png)
    rep = dict(K1=K1, pitch=PITCH, width_mm=W_MM, depth_mm=H_MM,
               max_abs_dx=float(np.abs(DX).max()), max_abs_dy=float(np.abs(DY).max()),
               vlines={k: dict(mean=d['mean'], design=d['design'], sd=d['sd'], n=d['n'], j=d['j']) for k, d in V.items()},
               hlines={k: dict(design=d['design'], sd=d['sd'], n=d['n'], xr=d['xr'],
                               bow=float(np.ptp(np.polyval(d['fit'], np.linspace(*d['xr'], 100))))) for k, d in H.items()},
               field_mm=dict(x=gx[::10].tolist(), y=gy[::10].tolist(),
                             dx=np.round(DX[::10, ::10], 3).tolist(), dy=np.round(DY[::10, ::10], 3).tolist()))
    json.dump(rep, open(out_json, 'w'), indent=1)
    print('K1 = %.3f mm; max |dx| %.2f, max |dy| %.2f mm' % (K1, rep['max_abs_dx'], rep['max_abs_dy']))
    for k, d in V.items():
        print('  %-14s mean %7.2f -> %7.2f  (sd %.3f, n %d)' % (k, d['mean'], d['design'], d['sd'], d['n']))
    for k, d in H.items():
        print('  %-14s y %7.2f  bow/tilt range %.2f  (sd %.3f, n %d)' % (k, d['design'], rep['hlines'][k]['bow'], d['sd'], d['n']))


if __name__ == '__main__':
    main()
