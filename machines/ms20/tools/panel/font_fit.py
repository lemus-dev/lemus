#!/usr/bin/env python3
"""Which typeface is the panel print closest to (docs/panel.md section 5)?

For every one-word legend measured on the master (ink width w, cap height h), each candidate font
predicts the ink width of that word at cap height h: the sum of the glyph advances minus the
first glyph's left and the last glyph's right side bearing, plus a tracking t (mm per letter gap).
Per font, a horizontal scale s and t are fitted by least squares over all words; the rms of the
remaining width error ranks the fonts. A natural match fits with s close to 1.

Usage: font_fit.py PRINT_JSON [FONT_FILE ...]"""
import sys, os
import numpy as np
from fontTools.ttLib import TTFont, TTCollection

CANDIDATES = [
    '/System/Library/Fonts/Supplemental/Arial Narrow.ttf',
    '/System/Library/Fonts/Supplemental/Arial.ttf',
    ('/System/Library/Fonts/Helvetica.ttc', 0),
    ('/System/Library/Fonts/HelveticaNeue.ttc', 0),
    ('/System/Library/Fonts/Supplemental/Futura.ttc', 0),
    ('/System/Library/Fonts/Supplemental/GillSans.ttc', 0),
    '/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf',
    '/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf',
]


def load(spec):
    if isinstance(spec, tuple):
        tc = TTCollection(spec[0])
        f = tc.fonts[spec[1]]
        name = '%s #%d' % (os.path.basename(spec[0]), spec[1])
    else:
        f = TTFont(spec)
        name = os.path.basename(spec)
    full = f['name'].getDebugName(4) or name
    return f, full


def word_width(f, word, cap_mm):
    cmap = f.getBestCmap()
    hmtx = f['hmtx']
    glyf = f['glyf'] if 'glyf' in f else None
    capH = getattr(f['OS/2'], 'sCapHeight', 0) or 0.7 * f['head'].unitsPerEm
    k = cap_mm / capH
    adv = 0.0
    names = [cmap.get(ord(ch)) for ch in word]
    if any(n is None for n in names):
        return None
    gs = f.getGlyphSet()
    from fontTools.pens.boundsPen import BoundsPen
    bounds = []
    for n in names:
        bp = BoundsPen(gs)
        gs[n].draw(bp)
        bounds.append(bp.bounds)
    adv = sum(hmtx[n][0] for n in names)
    lsb = bounds[0][0] if bounds[0] else 0
    rsb = hmtx[names[-1]][0] - bounds[-1][2] if bounds[-1] else 0
    return (adv - lsb - rsb) * k


def main():
    import json
    d = json.load(open(sys.argv[1]))
    words = []
    for k, v in d['legends'].items():
        t = v['text']
        if ' ' in t or not t.isalpha() or len(t) < 3 or v.get('font') == 'name':
            continue
        words.append((t, v['bbox'][2] - v['bbox'][0], v['cap_h'], len(t) - 1))
    print('%d one-word legends' % len(words))
    fonts = sys.argv[2:] or CANDIDATES
    rows = []
    for spec in fonts:
        try:
            f, name = load(spec)
        except Exception as e:
            print('skip', spec, e)
            continue
        A, b = [], []
        for t, w, h, gaps in words:
            p = word_width(f, t, h)
            if p is None:
                continue
            A.append([p, gaps]); b.append(w)
        A = np.array(A); b = np.array(b)
        sol, *_ = np.linalg.lstsq(A, b, rcond=None)
        res = A @ sol - b
        sol0 = np.linalg.lstsq(A[:, :1], b, rcond=None)[0]
        res0 = A[:, :1] @ sol0 - b
        rows.append((np.sqrt(np.mean(res ** 2)), name, sol[0], sol[1], np.sqrt(np.mean(res0 ** 2)), sol0[0]))
    rows.sort()
    print('%-40s %8s %8s %10s   %s' % ('font', 'rms mm', 'x-scale', 'track mm', '(no tracking: rms, x-scale)'))
    for r, name, s, t, r0, s0 in rows:
        print('%-40s %8.3f %8.3f %10.3f   (%.3f, %.3f)' % (name, r, s, t, r0, s0))


if __name__ == '__main__':
    main()
