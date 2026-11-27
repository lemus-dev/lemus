"""Legend measurement on the rectified master (docs/panel.md section 1, "Print").

A legend is the run of glyph components on one text line that starts at the glyph nearest its
seed and grows left and right while the gap to the next glyph stays under a word gap. Its box is
the sub-pixel ink box (half-maximum edges); its cap height is the median height of its capitals
(glyphs without descenders or tails); its stroke is 2 x the distance transform on the skeleton."""
import numpy as np

CAP = {'title': 3.1, 'legend': 2.5, 'box': 2.3, 'small': 1.9, 'name': 5.0}


def glyph_count(text):
    return sum(1 for ch in text if not ch.isspace())


def stem_width(R, sel, bb):
    """Stroke width (mm): FWHM of the ink runs on rows through the middle third of the cap height,
    median over all runs of the legend's glyphs (vertical stems dominate)."""
    ids = [c['i'] for c in sel]
    ws = []
    y0, y1 = bb[1] + (bb[3] - bb[1]) / 3, bb[3] - (bb[3] - bb[1]) / 3
    for y in np.arange(y0, y1, 0.1):
        xs = np.arange(bb[0] - 0.3, bb[2] + 0.3, 0.02)
        v = R.sample(R.ink, xs, np.full_like(xs, y))
        lab = R.lab[int(round(R.px(0, y)[1])), np.clip(np.round(R.px(xs, y)[0]).astype(int), 0, R.lab.shape[1] - 1)]
        v = np.where(np.isin(lab, ids) | (v > 0), v, 0)
        pk = np.percentile(v, 99)
        if pk < M_INK:
            continue
        on = v > 0.5 * pk
        d = np.diff(on.astype(int))
        starts = np.nonzero(d == 1)[0]; ends = np.nonzero(d == -1)[0]
        for s in starts:
            e = ends[ends > s]
            if len(e):
                w = (e[0] - s) * 0.02
                if 0.12 < w < 0.9:
                    ws.append(w)
    return float(np.median(ws)) if ws else None


M_INK = 55.0


def measure_box(R, box, font, max_h=None, max_w=None):
    """All glyph components inside box (mm): ink box, cap height, stroke."""
    cap = CAP[font]
    comps = [c for c in R.comps_in(box, max_h=max_h or cap * 1.7, max_w=max_w or max(cap * 3.0, 2.0), min_area=4)
             if box[0] <= c['bbox'][0] and c['bbox'][2] <= box[2] and box[1] <= c['bbox'][1] and c['bbox'][3] <= box[3]]
    if not comps:
        return None
    bb, pk = R.ink_bbox(comps)
    tops = np.array([c['bbox'][1] for c in comps]); bots = np.array([c['bbox'][3] for c in comps])
    hts = bots - tops
    big = hts > 0.6 * np.median(hts)
    cap_h = float(np.median(bots[big]) - np.median(tops[big]))
    return dict(bbox=[float(v) for v in bb], cap_h=cap_h, stroke=stem_width(R, comps, bb), n_glyphs=len(comps),
                expect=None, x=float((bb[0] + bb[2]) / 2), y=float((bb[1] + bb[3]) / 2), peak=pk)


def measure_legend(R, text, cx, cy, font, max_gap=None, exclude=None, xmin=None):
    cap = CAP[font]
    n = max(1, glyph_count(text))
    w_est = n * 0.62 * cap + text.count(' ') * 0.3 * cap
    box = (cx - w_est / 2 - 3.0, cy - cap * 0.75, cx + w_est / 2 + 3.0, cy + cap * 0.75)
    comps = R.comps_in(box, max_h=cap * 1.7, max_w=max(cap * 3.0, 2.0))
    if exclude is not None:
        comps = [c for c in comps if not exclude[int(R.px(*c['c'])[1]), int(R.px(*c['c'])[0])]]
    if xmin is not None:
        comps = [c for c in comps if c['bbox'][0] >= xmin]
    if not comps:
        return None
    # one text line: keep glyphs whose vertical centre is near the median
    yc = np.array([(c['bbox'][1] + c['bbox'][3]) / 2 for c in comps])
    med = np.median(yc)
    comps = [c for c, y in zip(comps, yc) if abs(y - med) < cap * 0.45]
    if not comps:
        return None
    comps.sort(key=lambda c: c['bbox'][0])
    # start at the glyph nearest the seed, grow while gaps stay below a word gap
    gap = max_gap if max_gap is not None else max(1.9, 0.85 * cap)
    k = int(np.argmin([abs((c['bbox'][0] + c['bbox'][2]) / 2 - cx) for c in comps]))
    lo = hi = k
    while lo > 0 and comps[lo]['bbox'][0] - comps[lo - 1]['bbox'][2] < gap:
        lo -= 1
    while hi < len(comps) - 1 and comps[hi + 1]['bbox'][0] - comps[hi]['bbox'][2] < gap:
        hi += 1
    # wide word gaps inside one legend (e.g. "OSCILLATOR   1"): extend while still inside the
    # expected width
    while hi < len(comps) - 1 and comps[hi + 1]['bbox'][2] - comps[lo]['bbox'][0] < w_est + 1.0 and \
            comps[hi + 1]['bbox'][0] - comps[hi]['bbox'][2] < 3 * gap:
        hi += 1
    sel = comps[lo:hi + 1]
    r = R.ink_bbox(sel)
    if r is None:
        return None
    bb, pk = r
    tops = np.array([c['bbox'][1] for c in sel]); bots = np.array([c['bbox'][3] for c in sel])
    hts = bots - tops
    big = hts > 0.6 * np.median(hts)
    cap_h = float(np.median(bots[big]) - np.median(tops[big])) if big.any() else float(bb[3] - bb[1])
    sw = stem_width(R, sel, bb)
    return dict(bbox=[float(v) for v in bb], cap_h=cap_h, stroke=sw, n_glyphs=len(sel), expect=n,
                x=float((bb[0] + bb[2]) / 2), y=float((bb[1] + bb[3]) / 2), peak=pk)
