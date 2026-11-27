#!/usr/bin/env python3
"""Measure centres in the master photo (photo px), docs/panel.md section 1.

  * knobs: the printed tick marks round each dial (on the panel face, so free of the knobs'
    parallax) are found as thin bright radial blobs in an annulus; the centre is the least-squares
    intersection of their axes, iterated. Each tick's angle and radii are kept.
  * knob tops: the knob's dark silhouette (fitted circle of its outer edge), for the parallax model.
  * jacks: the dark hole inside the nut (sub-pixel circle fit to its edge) and the nut's outline.

Usage: photo_centres.py OUT_JSON"""
import sys, os
import numpy as np
import cv2
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C


def load_gray():
    im = cv2.imread(C.MASTER, cv2.IMREAD_COLOR)
    return cv2.cvtColor(im, cv2.COLOR_BGR2GRAY).astype(np.float32)


def line_intersection(lines):
    """Least-squares point closest to lines given as (point, unit direction)."""
    A = np.zeros((2, 2)); b = np.zeros(2)
    for p, d in lines:
        n = np.array([-d[1], d[0]])
        A += np.outer(n, n); b += n * (n @ p)
    return np.linalg.solve(A, b)


def ticks_around(g, c, rin, rout, thr):
    """Radial tick blobs in the annulus rin..rout round c: list of (centroid, dir, len, angle, r0, r1)."""
    R = int(rout + 8)
    x0, y0 = int(c[0]) - R, int(c[1]) - R
    w = g[y0:y0 + 2 * R + 1, x0:x0 + 2 * R + 1]
    yy, xx = np.mgrid[0:w.shape[0], 0:w.shape[1]]
    rr = np.hypot(xx + x0 - c[0], yy + y0 - c[1])
    m = ((w > thr) & (rr > rin) & (rr < rout)).astype(np.uint8)
    n, lab, st, cen = cv2.connectedComponentsWithStats(m, 8)
    out = []
    for i in range(1, n):
        if st[i, 4] < 25:
            continue
        ys, xs = np.nonzero(lab == i)
        wt = w[ys, xs] - thr
        px = xs + x0; py = ys + y0
        mx, my = np.average(px, weights=wt), np.average(py, weights=wt)
        cov = np.cov(np.vstack([px - mx, py - my]), aweights=wt)
        ev, evec = np.linalg.eigh(cov)
        if ev[0] <= 0:
            continue
        el = np.sqrt(ev[1] / ev[0])
        d = evec[:, 1]
        rad = np.array([mx - c[0], my - c[1]]); rn = np.linalg.norm(rad)
        if rn == 0:
            continue
        cosang = abs(d @ rad) / rn
        L = 4 * np.sqrt(ev[1])
        if el < 3.0 or cosang < 0.97:
            continue
        pr = (px - c[0]) * rad[0] / rn + (py - c[1]) * rad[1] / rn
        out.append(dict(p=np.array([mx, my]), d=d, len=L, el=el, ang=C.angle_of(rad[0], rad[1]),
                        r0=float(pr.min()), r1=float(pr.max()), area=int(st[i, 4])))
    return out


def blobs_in(g, c, R, thr):
    """Bright elongated blobs (tick candidates) in a square window of half-size R round c."""
    x0, y0 = int(c[0]) - R, int(c[1]) - R
    w = g[y0:y0 + 2 * R + 1, x0:x0 + 2 * R + 1]
    m = (w > thr).astype(np.uint8)
    n, lab, st, cen = cv2.connectedComponentsWithStats(m, 8)
    out = []
    for i in range(1, n):
        if st[i, 4] < 25 or st[i, 4] > 1500:
            continue
        ys, xs = np.nonzero(lab == i)
        wt = w[ys, xs] - thr
        px = xs + x0; py = ys + y0
        mx, my = np.average(px, weights=wt), np.average(py, weights=wt)
        cov = np.cov(np.vstack([px - mx, py - my]), aweights=wt)
        ev, evec = np.linalg.eigh(cov)
        if ev[0] <= 0:
            continue
        el = np.sqrt(ev[1] / ev[0])
        if el < 3.0:
            continue
        out.append((np.array([mx, my]), evec[:, 1], 4 * np.sqrt(ev[1])))
    return out


def vote_centre(blobs, seed, rin, rout, span=90, step=3):
    """Centre that the most tick-like blobs point at (axis within 4 px, centroid at a tick radius)."""
    best = (-1, None)
    for dx in np.arange(-span, span + 1, step):
        for dy in np.arange(-span, span + 1, step):
            c = np.array([seed[0] + dx, seed[1] + dy])
            n = 0
            for p, d, L in blobs:
                v = p - c
                r = np.linalg.norm(v)
                if rin < r < rout and abs(-d[1] * v[0] + d[0] * v[1]) < 4:
                    n += 1
            if n > best[0]:
                best = (n, c)
    return best[1]


def knob_centre(g, seed, large, rout=None):
    # tick ring radii (px): small dials ticks end at r ~111, large ones at ~153
    rin, rout0 = (115, 160) if large else (75, 118)
    rout = rout or rout0
    thr = 150.0
    blobs = blobs_in(g, seed, 330 if large else 260, thr)
    c = vote_centre(blobs, seed, rin, rout)
    ticks = []
    for it in range(10):
        ticks = ticks_around(g, c, rin, rout, thr)
        if len(ticks) < 2:
            break
        cn = line_intersection([(t['p'], t['d']) for t in ticks])
        res = np.array([abs(np.array([-t['d'][1], t['d'][0]]) @ (t['p'] - cn)) for t in ticks])
        if len(ticks) > 3 and res.max() > 2.0:
            keep = [t for t, r in zip(ticks, res) if r <= max(2.0, 2.5 * np.median(res))]
            if len(keep) >= 2:
                ticks = keep
                cn = line_intersection([(t['p'], t['d']) for t in ticks])
        if np.linalg.norm(cn - c) < 0.02:
            c = cn
            break
        c = cn
    res = [abs(np.array([-t['d'][1], t['d'][0]]) @ (t['p'] - c)) for t in ticks]
    return c, ticks, res


def tick_ends(g, c, ticks):
    """Sub-pixel outer end of each tick: half-maximum crossing of the intensity along its axis,
    going outward from the tick's middle. Also the stroke width (FWHM across it)."""
    for t in ticks:
        u = np.array([np.sin(np.radians(t['ang'])), -np.cos(np.radians(t['ang']))])
        # the tick's own axis passes through the fitted centre within the residual
        rm = 0.5 * (t['r0'] + t['r1'])
        rs = np.arange(rm, t['r1'] + 8, 0.25)
        xs = (c[0] + u[0] * rs).astype(np.float32); ys = (c[1] + u[1] * rs).astype(np.float32)
        v = cv2.remap(g, xs[None, :], ys[None, :], cv2.INTER_LINEAR)[0]
        hi = np.median(v[:8]); lo = np.median(v[-8:])
        half = 0.5 * (hi + lo)
        i = np.nonzero(v < half)[0]
        if len(i) == 0 or hi - lo < 60:
            t['r_out'] = None
            continue
        i = i[0]
        r_out = rs[i - 1] + (v[i - 1] - half) / (v[i - 1] - v[i]) * 0.25
        t['r_out'] = float(r_out)
        t['end'] = [float(c[0] + u[0] * r_out), float(c[1] + u[1] * r_out)]
        # width across the tick at its middle
        n = np.array([-u[1], u[0]])
        offs = np.arange(-8, 8.01, 0.25)
        pm = c + u * (rm + 0.5 * (r_out - rm))
        xs = (pm[0] + n[0] * offs).astype(np.float32); ys = (pm[1] + n[1] * offs).astype(np.float32)
        w = cv2.remap(g, xs[None, :], ys[None, :], cv2.INTER_LINEAR)[0]
        bgv = 0.5 * (np.median(w[:6]) + np.median(w[-6:])); pk = w.max()
        t['fwhm'] = float((w > 0.5 * (pk + bgv)).sum() * 0.25)
    return ticks


def knob_silhouette(g, c, large):
    """Outer edge of the dark knob body: radial gradient maxima (dark->panel) fitted with a circle.
    The knob is black against the ~86 grey panel, its outline is the strongest dark/light edge."""
    R = 150 if large else 105
    pts = []
    for a in np.arange(0, 360, 3):
        u = C.unit(a)
        rs = np.arange(30, R, 0.5)
        xs = c[0] + u[0] * rs; ys = c[1] + u[1] * rs
        v = cv2.remap(g, xs.astype(np.float32)[None, :], ys.astype(np.float32)[None, :], cv2.INTER_LINEAR)[0]
        v = cv2.GaussianBlur(v.reshape(1, -1), (1, 5), 1.2).ravel()
        dv = np.diff(v)
        # last strong rise from dark (<45) to panel level before the ticks
        best = None
        for i in range(len(dv) - 1, 0, -1):
            if dv[i] > 6 and v[i] < 55 and v[min(i + 8, len(v) - 1)] > 65:
                best = i
                break
        if best is not None:
            r = rs[best] + 0.5
            pts.append((c[0] + u[0] * r, c[1] + u[1] * r))
    pts = np.array(pts)
    if len(pts) < 20:
        return None

    def f(p):
        return np.hypot(pts[:, 0] - p[0], pts[:, 1] - p[1]) - p[2]
    p0 = [pts[:, 0].mean(), pts[:, 1].mean(), np.hypot(*(pts - pts.mean(0)).T).mean()]
    r = least_squares(f, p0, loss='soft_l1', f_scale=2.0)
    return dict(c=r.x[:2].tolist(), r=float(r.x[2]), rms=float(np.sqrt(np.mean(f(r.x) ** 2))), n=len(pts))


def circle_fit(pts):
    def f(p):
        return np.hypot(pts[:, 0] - p[0], pts[:, 1] - p[1]) - p[2]
    p0 = [pts[:, 0].mean(), pts[:, 1].mean(), np.hypot(*(pts - pts.mean(0)).T).mean()]
    r = least_squares(f, p0, loss='soft_l1', f_scale=1.0)
    return r.x, float(np.sqrt(np.mean(f(r.x) ** 2)))


def radial_edges(g, c, r0, r1, rising, step=2.0):
    """Sub-pixel radial edge per angle: rising=True dark->bright going outward."""
    pts = []
    for a in np.arange(0, 360, step):
        u = C.unit(a)
        rs = np.arange(r0, r1, 0.25)
        xs = (c[0] + u[0] * rs).astype(np.float32); ys = (c[1] + u[1] * rs).astype(np.float32)
        v = cv2.remap(g, xs[None, :], ys[None, :], cv2.INTER_LINEAR)[0]
        v = np.convolve(v, np.ones(5) / 5, mode='same')
        dv = np.gradient(v)
        if not rising:
            dv = -dv
        i = int(np.argmax(dv[4:-4])) + 4
        if dv[i] < 3:
            continue
        a_, b_, c_ = dv[i - 1], dv[i], dv[i + 1]
        den = a_ - 2 * b_ + c_
        off = 0.5 * (a_ - c_) / den if den else 0
        r = rs[i] + off * 0.25
        pts.append((c[0] + u[0] * r, c[1] + u[1] * r))
    return np.array(pts)


def jack_centre(g, seed):
    c = np.array(seed, float)
    # refine the seed on the dark hole: centroid of dark pixels near the seed
    for it in range(3):
        R = 45
        w = g[int(c[1]) - R:int(c[1]) + R + 1, int(c[0]) - R:int(c[0]) + R + 1]
        m = w < 40
        ys, xs = np.nonzero(m)
        if len(xs) < 50:
            break
        c = np.array([xs.mean() + int(c[0]) - R, ys.mean() + int(c[1]) - R])
    # hole edge: dark -> bright (bushing rim) going outward
    hole_pts = radial_edges(g, c, 10, 40, rising=True)
    (hx, hy, hr), hrms = circle_fit(hole_pts)
    # keep only points close to the fit, refit
    d = np.abs(np.hypot(hole_pts[:, 0] - hx, hole_pts[:, 1] - hy) - hr)
    (hx, hy, hr), hrms = circle_fit(hole_pts[d < 2.5])
    # nut outline: bright nut -> panel grey going outward
    nut_pts = radial_edges(g, (hx, hy), hr + 25, hr + 75, rising=False, step=1.0)
    (nx, ny, nr), nrms = circle_fit(nut_pts)
    return dict(hole=[hx, hy], hole_r=hr, hole_rms=hrms,
                nut=[nx, ny], nut_r=nr, nut_rms=nrms, nut_pts=nut_pts.tolist())


def main():
    out_path = sys.argv[1]
    g = load_gray()
    knobs = {}
    for k, (sx, sy, board, part) in C.KNOBS.items():
        c, ticks, res = knob_centre(g, C.sp(sx, sy), k in C.LARGE, 155 if k == 'volume' else None)
        tick_ends(g, c, ticks)
        sil = knob_silhouette(g, c, k in C.LARGE)
        knobs[k] = dict(c=c.tolist(), n_ticks=len(ticks), tick_res_rms=float(np.sqrt(np.mean(np.square(res)))) if res else None,
                        ticks=[dict(ang=t['ang'], r0=t['r0'], r1=t['r1'], len=t['len'], el=t['el'], p=t['p'].tolist(), d=t['d'].tolist(), r_out=t.get('r_out'), end=t.get('end'), fwhm=t.get('fwhm')) for t in ticks],
                        silhouette=sil, board=board, part=part)
        print('%-14s c=(%.1f, %.1f) ticks=%2d res=%.2f px  sil=%s' % (
            k, c[0], c[1], len(ticks), knobs[k]['tick_res_rms'] or -1,
            None if sil is None else '(%.1f,%.1f) r=%.1f rms=%.2f' % (sil['c'][0], sil['c'][1], sil['r'], sil['rms'])))
    jacks = {}
    for k, (sx, sy, j) in C.JACKS.items():
        r = jack_centre(g, (sx, sy))
        r['j'] = j
        jacks[k] = r
        print('%-9s %-4s hole=(%.1f,%.1f) r=%.1f rms=%.2f  nut=(%.1f,%.1f) r=%.1f rms=%.2f' % (
            k, j, r['hole'][0], r['hole'][1], r['hole_r'], r['hole_rms'], r['nut'][0], r['nut'][1], r['nut_r'], r['nut_rms']))
    C.save_json(dict(knobs=knobs, jacks=jacks), out_path)


if __name__ == '__main__':
    main()
