#!/usr/bin/env python3
"""Colour samples from the colour reference photos (docs/panel.md section 4).

Boxes are given on a 1000 px wide preview of each photo (read on a 10 px grid). Each sample is
the median RGB of the box, or a percentile for thin or shiny parts (print, chrome). The master is
monochrome: it gives only the grey levels, listed for comparison. The grey card in the SMEM photo
is the white-board edge behind the unit; the camera's white balance is taken as is.

Usage: colours.py OUT_JSON"""
import sys, os
import numpy as np
import cv2

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C

SMEM = os.path.join(C.PHOTOS, 'Korg_MS-20_Synthesizer_at_SMEM_Playroom.jpg')
ZINN = os.path.join(C.PHOTOS, 'Korg_MS-20.jpg')

# key -> list of (photo, box on the 1000 px preview, statistic)
SAMPLES = {
    'panel_paint': [(SMEM, (842, 440, 858, 452), 'median'), (SMEM, (858, 300, 872, 312), 'median'),
                    (SMEM, (500, 300, 525, 318), 'median'), (ZINN, (430, 175, 470, 190), 'median')],
    'print_white': [(SMEM, (800, 295, 832, 303), 'p99'), (SMEM, (196, 247, 252, 256), 'p99')],
    'logo_grey': [(SMEM, (800, 425, 835, 440), 'p90')],
    'knob_black': [(SMEM, (805, 265, 822, 272), 'p30'), (SMEM, (225, 437, 236, 447), 'p30'),
                   (ZINN, (815, 128, 835, 145), 'p30')],
    'jack_chrome': [(SMEM, (835, 280, 870, 300), 'p90'), (SMEM, (835, 280, 870, 300), 'p60')],
    'hole_black': [(SMEM, (846, 285, 856, 293), 'p10')],
    'key_white': [(SMEM, (290, 595, 320, 612), 'median'), (ZINN, (440, 490, 460, 515), 'median')],
    'key_black': [(SMEM, (288, 500, 296, 530), 'median'), (ZINN, (452, 400, 458, 440), 'median')],
    'side_panel': [(SMEM, (878, 300, 890, 400), 'median'), (ZINN, (905, 150, 915, 300), 'median')],
    'top_cover': [(SMEM, (400, 205, 600, 214), 'median'), (ZINN, (300, 60, 500, 75), 'median')],
    'cheek_black': [(SMEM, (205, 500, 215, 520), 'median')],
    'wheel_black': [(SMEM, (182, 515, 196, 560), 'median')],
    'led_off': [(SMEM, (786, 390, 790, 394), 'min')],
    'screw': [(SMEM, (857, 428, 867, 438), 'p80')],
}


def stat(a, how):
    if how == 'median':
        return np.median(a, 0)
    if how == 'min':
        return a[np.argmin(a.sum(1))]
    p = float(how[1:])
    s = a.sum(1)
    thr = np.percentile(s, p)
    sel = a[np.abs(s - thr) <= max(6, 0.03 * thr)]
    return np.median(sel, 0) if len(sel) else np.median(a, 0)


def main():
    cache = {}
    out = {}
    for key, lst in SAMPLES.items():
        vals = []
        for path, box, how in lst:
            if path not in cache:
                cache[path] = cv2.imread(path, cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32)
            im = cache[path]
            s = im.shape[1] / 1000.0
            x0, y0, x1, y1 = (int(round(v * s)) for v in box)
            a = im[y0:y1, x0:x1].reshape(-1, 3)
            v = stat(a, how)
            vals.append(dict(photo=os.path.basename(path), box=box, stat=how, rgb=[float(t) for t in v], hex=C.hexc(v)))
        out[key] = vals
        print('%-12s %s' % (key, '  '.join('%s %s(%s)' % (v['hex'], v['photo'][:9], v['stat']) for v in vals)))
    C.save_json(out, sys.argv[1])


if __name__ == '__main__':
    main()
