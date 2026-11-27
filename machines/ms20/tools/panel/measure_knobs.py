#!/usr/bin/env python3
"""Knobs and rotary switches in panel mm (docs/panel.md sections 1 and 6).

Per dial:
  * centre: the printed tick ring's centre (photo_centres.py), mapped as print (no parallax);
  * ticks: angles from an angular ink profile just inside the ticks' outer ends on the rectified
    image (sub-degree centroids), outer radius from the photo's sub-pixel tick ends, inner radius
    where a tick's inner end is visible (not under the knob), width (FWHM);
  * numerals: glyph components outside the ticks, grouped by angle and matched to the scale's
    numerals; centre, polar position, cap height;
  * knob body: the dark silhouette in the photo. Its near edge (towards the camera) is the skirt at
    the panel; the far edge and the knob top's shift give the height (camera.py).

Usage: measure_knobs.py OUT_JSON"""
import sys, os
import numpy as np
import cv2

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C
import frame
import measure_lib as M
import panel_tables as PT


def angular_ticks(R, c, r, half_w=12):
    """Peaks of the ink round a circle of radius r (mm) about c: list of (angle, peak)."""
    angs = np.arange(-180, 180, 0.1)
    u = np.c_[np.sin(np.radians(angs)), -np.cos(np.radians(angs))]
    v = R.sample(R.ink, c[0] + u[:, 0] * r, c[1] + u[:, 1] * r)
    on = v > M.INK
    out = []
    i = 0
    n = len(v)
    while i < n:
        if on[i]:
            j = i
            while j < n and on[j]:
                j += 1
            w = np.clip(v[i:j] - M.INK * 0.5, 0, None)
            a = (w * angs[i:j]).sum() / w.sum()
            span = (j - i) * 0.1
            out.append((float(a), float(v[i:j].max()), span))
            i = j
        else:
            i += 1
    return out


def tick_profile(R, c, ang, r_lo, r_hi, r_ref):
    """Along the ray at ang: the ink run through r_ref (just inside the outer end); returns
    (r_in, r_out, inner_visible, fwhm). r_out is the half-maximum crossing, sub-sample."""
    u = np.array([np.sin(np.radians(ang)), -np.cos(np.radians(ang))])
    rs = np.arange(r_lo, r_hi, 0.02)
    v = R.sample(R.ink, c[0] + u[0] * rs, c[1] + u[1] * rs)
    gv = R.sample(R.g, c[0] + u[0] * rs, c[1] + u[1] * rs)
    i0 = int(np.argmin(np.abs(rs - r_ref)))
    pk = v[max(0, i0 - 25):i0 + 25].max()
    if pk < M.INK:
        return None
    h = 0.5 * pk
    on = v > h
    if not on[i0]:
        i0 = max(range(max(0, i0 - 25), min(len(v), i0 + 25)), key=lambda i: v[i])
    a = i0
    while a > 0 and on[a - 1]:
        a -= 1
    b = i0
    while b < len(v) - 1 and on[b + 1]:
        b += 1
    if b >= len(v) - 1:
        return None
    r_out = rs[b] + (v[b] - h) / max(v[b] - v[b + 1], 1e-6) * 0.02
    r_in = rs[a] - ((v[a] - h) / max(v[a] - v[a - 1], 1e-6) * 0.02 if a > 0 else 0)
    # inner end visible if the panel (not the black knob) lies just inside it
    inner_vis = a > 30 and np.median(gv[max(0, a - 30):a - 8]) > 40
    rm = 0.5 * (r_in + r_out)
    n = np.array([-u[1], u[0]])
    offs = np.arange(-1.0, 1.0001, 0.02)
    w = R.sample(R.ink, c[0] + u[0] * rm + n[0] * offs, c[1] + u[1] * rm + n[1] * offs)
    hh = 0.5 * w.max()
    on2 = np.nonzero(w > hh)[0]
    fw = float((on2[-1] - on2[0] + 1) * 0.02) if len(on2) else None
    return float(r_in), float(r_out), bool(inner_vis), fw


def numerals(R, c, r_tick, scale, knob_r):
    """Glyphs round the dial outside the ticks, matched to the scale's numerals by angle."""
    exp = PT.SCALES[scale]
    if not exp:
        return []
    comps = R.comps_in((c[0] - r_tick - 7, c[1] - r_tick - 7, c[0] + r_tick + 7, c[1] + r_tick + 7),
                       max_h=3.2, max_w=4.0, min_area=5)
    pts = []
    for cp in comps:
        bx = cp['bbox']
        cx, cy = (bx[0] + bx[2]) / 2, (bx[1] + bx[3]) / 2
        rr = np.hypot(cx - c[0], cy - c[1])
        if rr < r_tick + 0.4 or rr > r_tick + 3.2:
            continue
        pts.append((C.angle_of(cx - c[0], cy - c[1]), rr, cp))
    out = []
    used = set()
    for text, ang in exp:
        # glyph components of this numeral: within 14 deg of the expected angle, clustered
        cand = [(abs(((a - ang + 180) % 360) - 180), a, rr, cp) for a, rr, cp in pts if id(cp) not in used]
        cand = [t for t in cand if t[0] < 14]
        if not cand:
            out.append(dict(text=text, deg_design=ang, found=False))
            continue
        cand.sort(key=lambda t: t[0])
        first = cand[0]
        group = [first]
        for t in cand[1:]:
            b0 = first[3]['bbox']; b1 = t[3]['bbox']
            if abs((b0[1] + b0[3]) / 2 - (b1[1] + b1[3]) / 2) < 0.6 and abs((b0[3] - b0[1]) - (b1[3] - b1[1])) < 0.6 and \
               min(abs(b1[0] - b0[2]), abs(b0[0] - b1[2]), abs((b0[0] + b0[2]) / 2 - (b1[0] + b1[2]) / 2)) < 2.2:
                group.append(t)
        sel = [t[3] for t in group]
        for t in group:
            used.add(id(t[3]))
        r = R.ink_bbox(sel)
        if r is None:
            continue
        bb, pk = r
        cx, cy = (bb[0] + bb[2]) / 2, (bb[1] + bb[3]) / 2
        out.append(dict(text=text, deg_design=ang, found=True, bbox=bb, x=cx, y=cy,
                        deg=C.angle_of(cx - c[0], cy - c[1]), r=float(np.hypot(cx - c[0], cy - c[1])),
                        cap_h=float(bb[3] - bb[1]), n=len(sel)))
    return out


def silhouette(Fr, g, c_mm, large, r_tick=11.3, frac=0.5):
    """Dark knob silhouette in the photo: pixels darker than thr, closed (the white pointer and the
    ribs' highlights would split it) and hole-filled; the component under the dial centre. Returns
    its outline points per 2 deg, mapped onto the panel plane as seen (h = 0), in mm."""
    cpx = Fr.photo(np.array([c_mm]))[0]
    R = 230 if large else 170
    x0, y0 = int(cpx[0]) - R, int(cpx[1]) - R
    w = g[y0:y0 + 2 * R, x0:x0 + 2 * R]
    # the panel's own level round the dial (it falls from ~100 at the top to ~45 at the bottom):
    # the median of non-print pixels in a ring just outside the numerals
    a = np.radians(np.arange(0, 360, 1.0))
    ring = []
    for rr in np.arange(r_tick + 6.0, r_tick + 8.5, 0.5):
        pts = Fr.photo(np.c_[c_mm[0] + np.sin(a) * rr, c_mm[1] - np.cos(a) * rr])
        v = cv2.remap(g, pts[:, 0].astype(np.float32)[None, :], pts[:, 1].astype(np.float32)[None, :], cv2.INTER_LINEAR)[0]
        ring.append(v)
    ring = np.concatenate(ring)
    level = float(np.median(ring[ring < 140]))
    thr = frac * level
    m = (cv2.GaussianBlur(w, (5, 5), 1.5) < thr).astype(np.uint8)
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (25, 25)))
    n, lab = cv2.connectedComponents(m, 8)
    cc = lab[R - 15:R + 15, R - 15:R + 15].ravel()
    cc = cc[cc > 0]
    if len(cc) == 0:
        return [], level, None, (x0, y0)
    k = np.bincount(cc).argmax()
    blob = (lab == k).astype(np.uint8)
    cnts, _ = cv2.findContours(blob, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    cnt = max(cnts, key=cv2.contourArea)[:, 0, :].astype(float)
    cnt[:, 0] += x0; cnt[:, 1] += y0
    q = Fr.mm(cnt)
    ang = np.array([C.angle_of(*(p - c_mm)) for p in q])
    rad = np.hypot(*(q - c_mm).T)
    out = []
    for a in np.arange(-180, 180, 2):
        sel = np.abs(((ang - a + 180) % 360) - 180) < 1.0
        if sel.any():
            i = np.argmax(rad[sel])
            out.append((float(a), float(rad[sel][i]), q[sel][i].tolist()))
    return out, level, blob, (x0, y0)


def fit_circle(pts):
    from scipy.optimize import least_squares
    pts = np.asarray(pts)
    def f(p):
        return np.hypot(pts[:, 0] - p[0], pts[:, 1] - p[1]) - p[2]
    p0 = [pts[:, 0].mean(), pts[:, 1].mean(), np.hypot(*(pts - pts.mean(0)).T).mean()]
    r = least_squares(f, p0, loss='soft_l1', f_scale=0.2)
    return r.x, float(np.sqrt(np.mean(f(r.x) ** 2)))


def main():
    Fr = frame.Frame()
    R = M.Rect()
    pc = C.load_json(os.path.join(C.OUT, 'photo_centres.json'))
    g = cv2.imread(C.MASTER, cv2.IMREAD_GRAYSCALE).astype(np.float32)
    foot = Fr.cam.C[:2]
    out = {}
    for k, v in pc['knobs'].items():
        large = k in C.LARGE
        c = Fr.mm(v['c'])[0]
        ends = [Fr.mm(t['end'])[0] for t in v['ticks'] if t.get('end')]
        r_out = float(np.median([np.hypot(*(e - c)) for e in ends])) if ends else (15.6 if large else 11.4)
        peaks = angular_ticks(R, c, r_out - 0.6)
        ticks = []
        for a, pk, span in peaks:
            if span > 6:          # a numeral or a line crossing, not a tick
                continue
            tp = tick_profile(R, c, a, r_out - 6.0, r_out + 0.45, r_out - 0.6)
            if tp is None:
                continue
            r_in, r_o, vis, fw = tp
            if abs(r_o - r_out) > 0.6:
                continue
            ticks.append(dict(deg=a, r_in=r_in, r_out=r_o, inner_visible=vis, w=fw))
        scale = PT.KNOB_SCALE.get(k, 'n10')
        nums = numerals(R, c, r_out, scale, None)
        sil, level, blob, off = silhouette(Fr, g, c, large, r_out)
        a_foot = C.angle_of(*(foot - c))
        def sel(lo, hi):
            return [(a, r, q) for a, r, q in sil if lo <= abs(((a - a_foot + 180) % 360) - 180) <= hi]
        near = [r for a, r, q in sel(0, 40)]
        side = [r for a, r, q in sel(75, 105)]
        farp = [q for a, r, q in sel(115, 180)]
        top = None
        if len(farp) > 10:
            (tx, ty, tr), trms = fit_circle(farp)
            top = dict(c=[float(tx), float(ty)], r=float(tr), rms=trms)
        out[k] = dict(c=c.tolist(), r_tick_out=r_out, ticks=ticks, numerals=nums, scale=scale,
                      near_r=float(np.median(near)) if near else None,
                      side_r=float(np.median(side)) if side else None, top=top,
                      foot_dir=a_foot, dist_to_foot=float(np.hypot(*(foot - c))),
                      board=v['board'], part=v['part'], large=large)
        tp = out[k]['top']
        print('%-14s c=(%7.2f,%7.2f) r_out=%.2f ticks=%2d nums %d/%d  near %.2f side %.2f  top r %.2f shift %.2f (dist %.0f)' % (
            k, c[0], c[1], r_out, len(ticks), sum(1 for n in nums if n.get('found')), len(nums),
            out[k]['near_r'] or -1, out[k]['side_r'] or -1, tp['r'] if tp else -1,
            np.hypot(*(np.array(tp['c']) - c)) if tp else -1, out[k]['dist_to_foot']))
    C.save_json(out, sys.argv[1])


if __name__ == '__main__':
    main()
