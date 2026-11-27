#!/usr/bin/env python3
"""Resample the master photo into the panel frame at 10 px/mm (docs/panel.md section 1).

The map is the fitted camera model (rectify_fit.py, scale.py) for the panel face (h = 0),
computed on a 1 mm grid and interpolated. Things off the panel face (knob tops, jack bushings,
the keyboard) keep their parallax in this image; measure_*.py corrects those with the camera.

Usage: rectify_image.py OUT_PNG [X0 Y0 X1 Y1 mm]"""
import sys, os
import numpy as np
import cv2

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C
import frame


def main():
    out = sys.argv[1]
    x0, y0, x1, y1 = (float(v) for v in sys.argv[2:6]) if len(sys.argv) > 5 else (-10.0, -25.0, 580.0, 430.0)
    F = frame.Frame()
    P = C.PPM
    gx = np.arange(x0, x1 + 1.0001, 1.0); gy = np.arange(y0, y1 + 1.0001, 1.0)
    X, Y = np.meshgrid(gx, gy)
    u = F.photo(np.c_[X.ravel(), Y.ravel()]).reshape(len(gy), len(gx), 2).astype(np.float32)
    W = int(round((x1 - x0) * P)); H = int(round((y1 - y0) * P))
    # output pixel (r, c) centre = (x0 + (c + 0.5)/P, y0 + (r + 0.5)/P) mm = grid index ((c + 0.5)/P, (r + 0.5)/P)
    jj = ((np.arange(W) + 0.5) / P)
    ii = ((np.arange(H) + 0.5) / P)
    J, I = np.meshgrid(jj, ii)
    mapx = cv2.remap(u[..., 0], J.astype(np.float32), I.astype(np.float32), cv2.INTER_LINEAR)
    mapy = cv2.remap(u[..., 1], J.astype(np.float32), I.astype(np.float32), cv2.INTER_LINEAR)
    im = cv2.imread(C.MASTER, cv2.IMREAD_COLOR)
    rect = cv2.remap(im, mapx, mapy, cv2.INTER_CUBIC, borderMode=cv2.BORDER_CONSTANT, borderValue=(0, 0, 0))
    cv2.imwrite(out, rect)
    C.save_json(dict(x0=x0, y0=y0, x1=x1, y1=y1, ppm=P,
                     note='pixel (i, j) centre = (x0 + (j + 0.5)/ppm, y0 + (i + 0.5)/ppm) mm'), os.path.splitext(out)[0] + '.json')
    print('wrote', out, rect.shape)


if __name__ == '__main__':
    main()
