#!/usr/bin/env python3
"""Shaft and jack centres from the service manual's board front views (docs/panel.md section 1).

KLM-127 (1994 TIFF korg_ms20_6.tif, turned to landscape) carries every knob of the left two thirds
of the panel: the four rotary switches SW1-SW4 and the pots VR9-VR34; also the trimmers VR1-VR8
under the panel's plugged holes. KLM-128C (korg_ms20_7.tif, portrait: the board is drawn turned
90 deg) carries 34 of the 35 jacks; SIGNAL OUT (J35) sits in a notch at the board's edge.

Each part outline is a drawn circle. Its centre comes from a robust circle fit to the centre line
of the drawn stroke (dark-ink centroid along radial profiles), starting from a Hough circle near
the seed. Coordinates are drawing pixels (300 dpi scan of the printed page).

Usage: pcb_refs.py OUT_JSON"""
import sys, os
import numpy as np
import cv2
from PIL import Image
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C

Image.MAX_IMAGE_PIXELS = None
K = 3496 / 1800.0      # seeds below were read on 1800 px previews of the drawings

# KLM-127 parts (preview px of the landscape drawing): part -> (x, y, kind)
P127 = {
    'SW1': (262, 465, 'switch'), 'VR9': (258, 655, 'pot'), 'SW2': (255, 835, 'switch'),
    'SW4': (455, 460, 'switch'), 'VR12': (460, 645, 'pot'), 'SW3': (455, 838, 'switch'),
    'VR10': (258, 1055, 'pot'), 'VR11': (458, 1050, 'pot'),
    'VR13': (650, 455, 'pot'), 'VR14': (648, 665, 'pot'), 'VR15': (650, 910, 'pot'), 'VR16': (650, 1050, 'pot'),
    'VR20': (818, 455, 'pot'), 'VR19': (830, 655, 'pot'), 'VR18': (830, 905, 'pot'), 'VR17': (833, 1053, 'pot'),
    'VR21': (1003, 455, 'pot'), 'VR22': (1008, 655, 'pot'), 'VR23': (1012, 905, 'pot'), 'VR24': (1013, 1050, 'pot'),
    'VR26': (1186, 900, 'pot'), 'VR25': (1189, 1044, 'pot'),
    'VR27': (1365, 760, 'pot'), 'VR28': (1366, 903, 'pot'), 'VR29': (1366, 1045, 'pot'),
    'VR34': (1543, 444, 'pot'), 'VR33': (1544, 614, 'pot'), 'VR32': (1545, 758, 'pot'),
    'VR31': (1546, 899, 'pot'), 'VR30': (1546, 1042, 'pot'),
    # trimmers (small circles); the panel has a plugged access hole over each
    'VR1': (325, 712, 'trim'), 'VR6': (527, 712, 'trim'), 'VR2': (322, 905, 'trim'), 'VR5': (527, 905, 'trim'),
    'VR3': (340, 1110, 'trim'), 'VR4': (527, 1110, 'trim'), 'VR7': (760, 575, 'trim'), 'VR8': (940, 575, 'trim'),
    'LED-1': (1183, 757, 'led'), 'LED2': (1455, 657, 'led'),
}
RAD = {'switch': 107, 'pot': 74.5, 'trim': 46, 'led': 17}

# KLM-128C jacks (preview px of the portrait drawing, 1279 x 1800)
P128 = {
    'J34': (358, 364), 'J33': (517, 364), 'J32': (639, 364), 'J31': (761, 363), 'J30': (931, 364),
    'J26': (518, 487), 'J27': (639, 486), 'J28': (763, 486), 'J29': (933, 487),
    'J24': (579, 603), 'J25': (761, 622), 'J23': (518, 706), 'J22': (358, 773),
    'J20': (640, 865), 'J19': (762, 867), 'J18': (933, 811), 'J21': (359, 933),
    'J16': (760, 989), 'J15': (639, 1050), 'J17': (932, 1055), 'J14': (357, 1085),
    'J12': (518, 1177), 'J11': (640, 1177), 'J10': (763, 1178), 'J13': (359, 1226),
    'J7': (519, 1298), 'J8': (763, 1300), 'J9': (934, 1299), 'J6': (359, 1365), 'J5': (640, 1422),
    'J4': (358, 1543), 'J3': (518, 1542), 'J2': (762, 1542), 'J1': (932, 1543),
}
K128 = 3496 / 1800.0


def load(path, rotate):
    im = Image.open(path).convert('L')
    if rotate:
        im = im.rotate(-90, expand=True)
    return np.array(im).astype(np.float32)


def stroke_points(g, c, r, half=12, step=2.0):
    """Centre line of a drawn circle of radius ~r: per angle, the ink-weighted mean radius."""
    pts = []
    for a in np.arange(0, 360, step):
        u = C.unit(a)
        rs = np.arange(r - half, r + half, 0.5)
        xs = (c[0] + u[0] * rs).astype(np.float32); ys = (c[1] + u[1] * rs).astype(np.float32)
        v = cv2.remap(g, xs[None, :], ys[None, :], cv2.INTER_LINEAR)[0]
        ink = np.clip(255 - v, 0, None)
        # the stroke: the dark run nearest r
        if ink.sum() < 255 * 3:
            continue
        lab = ink > 128
        runs = []
        i = 0
        while i < len(lab):
            if lab[i]:
                j = i
                while j < len(lab) and lab[j]:
                    j += 1
                runs.append((i, j))
                i = j
            else:
                i += 1
        if not runs:
            continue
        mid = len(rs) / 2
        i0, i1 = min(runs, key=lambda t: abs((t[0] + t[1]) / 2 - mid))
        if i1 - i0 > 16:            # merged with a label or a trace: skip
            continue
        w = ink[max(0, i0 - 1):i1 + 1]
        rr = rs[max(0, i0 - 1):i1 + 1]
        rm = (w * rr).sum() / w.sum()
        pts.append((c[0] + u[0] * rm, c[1] + u[1] * rm))
    return np.array(pts)


def fit(pts, p0):
    def f(p):
        return np.hypot(pts[:, 0] - p[0], pts[:, 1] - p[1]) - p[2]
    r = least_squares(f, p0, loss='soft_l1', f_scale=1.0)
    res = f(r.x)
    keep = np.abs(res) < 3
    r = least_squares(lambda p: np.hypot(pts[keep, 0] - p[0], pts[keep, 1] - p[1]) - p[2], r.x)
    res = np.hypot(pts[keep, 0] - r.x[0], pts[keep, 1] - r.x[1]) - r.x[2]
    return r.x, float(np.sqrt(np.mean(res ** 2))), int(keep.sum())


def hough_near(g, seed, rad):
    R = int(rad * 1.6)
    x0, y0 = int(seed[0]) - R, int(seed[1]) - R
    w = g[y0:y0 + 2 * R, x0:x0 + 2 * R].astype(np.uint8)
    b = cv2.GaussianBlur(w, (5, 5), 1.5)
    cs = cv2.HoughCircles(b, cv2.HOUGH_GRADIENT, dp=1, minDist=10, param1=100, param2=20,
                          minRadius=int(rad * 0.75), maxRadius=int(rad * 1.25))
    if cs is None:
        return np.array(seed, float), rad
    cs = cs[0]
    d = np.hypot(cs[:, 0] + x0 - seed[0], cs[:, 1] + y0 - seed[1])
    i = int(np.argmin(d))
    return np.array([cs[i, 0] + x0, cs[i, 1] + y0]), float(cs[i, 2])


def measure(g, seeds, scale, radfn):
    """Circle per part. The drawn outlines of one kind share a radius (pots 74.5 px, switches
    ~107 px, trimmers 46 px, LEDs 17 px, jack rings 50.4 px): the stroke is searched within
    +-12 % of that radius, so labels and traces touching the outline do not pull the fit."""
    out = {}
    for k, v in seeds.items():
        seed = np.array(v[:2], float) * scale
        rn = radfn(v)
        c, r = hough_near(g, seed, rn)
        if abs(r - rn) > 0.08 * rn or np.linalg.norm(c - seed) > 0.5 * rn:
            c, r = seed, rn
        best = None
        for it in range(4):
            pts = stroke_points(g, c, rn, half=max(4, rn * 0.12))
            if len(pts) < 20:
                break
            p, rms, n = fit(pts, [c[0], c[1], rn])
            c = p[:2]
            best = dict(c=[float(c[0]), float(c[1])], r=float(p[2]), rms=rms, n=n)
        out[k] = best
        print('%-6s %s' % (k, 'FAIL' if best is None else '(%.1f, %.1f) r=%.1f rms=%.2f n=%d' % (
            best['c'][0], best['c'][1], best['r'], best['rms'], best['n'])))
    return out


def main():
    g127 = load(C.KLM127, True)
    g128 = load(C.KLM128, False)
    print('KLM-127', g127.shape)
    r127 = measure(g127, P127, K, lambda v: RAD[v[2]])
    print('KLM-128C', g128.shape)
    r128 = measure(g128, P128, K128, lambda v: 50.4)
    C.save_json(dict(klm127=r127, klm128=r128, kinds={k: v[2] for k, v in P127.items()}), sys.argv[1])


if __name__ == '__main__':
    main()
