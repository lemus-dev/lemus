#!/usr/bin/env python3
"""Printed straight lines in the master photo (photo px), for the rectification (docs/panel.md 1).

The panel's section rules, frames and the patch panel's flow lines are long, thin, white, and
either horizontal or vertical in the artwork. They are found with a probabilistic Hough transform
on the print mask, then sampled every 6 px across their length: each sample is the ink-weighted
centre of a perpendicular profile, kept only where the profile shows one clean stroke (so
crossings, legends and arrow heads drop out).

Usage: photo_lines.py OUT_JSON"""
import sys, os
import numpy as np
import cv2

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C


def print_mask(g):
    bg = cv2.medianBlur(g.astype(np.uint8), 31).astype(np.float32)
    return ((g - bg) > 70).astype(np.uint8), bg


def sample_line(g, bg, p0, p1, half=9, step=6):
    p0 = np.array(p0, float); p1 = np.array(p1, float)
    L = np.linalg.norm(p1 - p0)
    t = (p1 - p0) / L
    n = np.array([-t[1], t[0]])
    pts = []
    for s in np.arange(4, L - 4, step):
        c = p0 + t * s
        offs = np.arange(-half, half + 0.01, 0.5)
        xs = (c[0] + n[0] * offs).astype(np.float32); ys = (c[1] + n[1] * offs).astype(np.float32)
        v = cv2.remap(g, xs[None, :], ys[None, :], cv2.INTER_LINEAR)[0]
        b = cv2.remap(bg, xs[None, :], ys[None, :], cv2.INTER_LINEAR)[0]
        ink = v - b
        pk = ink.max()
        if pk < 70:
            continue
        on = ink > 0.5 * pk
        # one run only, not touching the window edge
        edges = np.diff(on.astype(int))
        if (edges == 1).sum() + on[0] != 1 or on[0] or on[-1]:
            continue
        i0 = int(np.argmax(on)); i1 = len(on) - int(np.argmax(on[::-1]))
        width = (i1 - i0) * 0.5
        if width < 2 or width > 14:
            continue
        w = np.clip(ink[i0 - 1:i1 + 1], 0, None)
        o = (w * offs[i0 - 1:i1 + 1]).sum() / w.sum()
        pts.append((c[0] + n[0] * o, c[1] + n[1] * o, width))
    return np.array(pts)


def main():
    im = cv2.imread(C.MASTER, cv2.IMREAD_GRAYSCALE).astype(np.float32)
    m, bg = print_mask(im)
    # thin the search to the panel area (photo px)
    m[:950, :] = 0; m[2760:, :] = 0; m[:, :1150] = 0; m[:, 6650:] = 0
    segs = cv2.HoughLinesP(m * 255, 1, np.pi / 720, threshold=200, minLineLength=220, maxLineGap=6).reshape(-1, 4)
    found = []
    for x0, y0, x1, y1 in segs:
        dx, dy = x1 - x0, y1 - y0
        ang = np.degrees(np.arctan2(dy, dx)) % 180
        if min(ang, 180 - ang) < 3:
            kind = 'h'
            if x1 < x0:
                x0, y0, x1, y1 = x1, y1, x0, y0
        elif abs(ang - 90) < 3:
            kind = 'v'
            if y1 < y0:
                x0, y0, x1, y1 = x1, y1, x0, y0
        else:
            continue
        found.append([kind, float(x0), float(y0), float(x1), float(y1)])
    # merge collinear overlapping Hough pieces
    merged = []
    for kind in 'hv':
        segs_k = [f for f in found if f[0] == kind]
        key = (lambda f: (f[2] + f[4]) / 2) if kind == 'h' else (lambda f: (f[1] + f[3]) / 2)
        segs_k.sort(key=key)
        groups = []
        for f in segs_k:
            for gp in groups:
                if abs(key(gp[-1]) - key(f)) < 6:
                    a0, a1 = (f[1], f[3]) if kind == 'h' else (f[2], f[4])
                    lo = min(min(q[1], q[3]) if kind == 'h' else min(q[2], q[4]) for q in gp)
                    hi = max(max(q[1], q[3]) if kind == 'h' else max(q[2], q[4]) for q in gp)
                    if a0 <= hi + 25 and a1 >= lo - 25:
                        gp.append(f)
                        break
            else:
                groups.append([f])
        for gp in groups:
            if kind == 'h':
                xs = [q[1] for q in gp] + [q[3] for q in gp]
                ys = [q[2] for q in gp] + [q[4] for q in gp]
                i0, i1 = int(np.argmin(xs)), int(np.argmax(xs))
                merged.append(['h', xs[i0], ys[i0], xs[i1], ys[i1]])
            else:
                ys = [q[2] for q in gp] + [q[4] for q in gp]
                xs = [q[1] for q in gp] + [q[3] for q in gp]
                i0, i1 = int(np.argmin(ys)), int(np.argmax(ys))
                merged.append(['v', xs[i0], ys[i0], xs[i1], ys[i1]])
    lines = []
    for kind, x0, y0, x1, y1 in merged:
        pts = sample_line(im, bg, (x0, y0), (x1, y1))
        if len(pts) < 25:
            continue
        # robust straight fit; drop samples off the stroke
        a = 0 if kind == 'v' else 1          # coordinate that should be constant
        for it in range(3):
            p = np.polyfit(pts[:, 1 - a], pts[:, a], 1)
            r = pts[:, a] - np.polyval(p, pts[:, 1 - a])
            keep = np.abs(r) < max(0.8, 3 * np.median(np.abs(r)) * 1.4826)
            pts = pts[keep]
            if len(pts) < 25:
                break
        if len(pts) < 25:
            continue
        p = np.polyfit(pts[:, 1 - a], pts[:, a], 1)
        r = pts[:, a] - np.polyval(p, pts[:, 1 - a])
        lines.append(dict(kind=kind, p0=[x0, y0], p1=[x1, y1], pts=pts[:, :2].tolist(),
                          width_px=float(np.median(pts[:, 2])), slope=float(p[0]), rms=float(r.std()),
                          length=float(np.ptp(pts[:, 1 - a]))))
    lines.sort(key=lambda l: (l['kind'], l['p0'][1] if l['kind'] == 'h' else l['p0'][0]))
    for l in lines:
        print('%s (%6.0f,%6.0f)-(%6.0f,%6.0f) n=%3d len=%5.0f slope=%+.5f rms=%.2f w=%.1f' % (
            l['kind'], *l['p0'], *l['p1'], len(l['pts']), l['length'], l['slope'], l['rms'], l['width_px']))
    C.save_json(dict(lines=lines), sys.argv[1])


if __name__ == '__main__':
    main()
