#!/usr/bin/env python3
"""Printed graphics in panel mm (docs/panel.md section 8): legends, the patch panel's boxes and
amplifier triangles, every printed line (section rules, frames, flow lines) with its arrowheads,
and the symbols (waveforms, ground marks, the wheel and button marks).

Legends: measure_text.py, seeded by panel_tables.LEGENDS. Boxes and triangles: each side's stroke
centre line is fitted (measure_lib.fit_line_profile) between seeded corners. Lines: what is left
of the print once legends, boxes, triangles, symbols, jacks and dials are masked is skeletonised
and traced into polylines (Douglas-Peucker, 0.12 mm); wide blobs at line ends are arrowheads,
kept as triangles (tip, two base corners). Symbols: the skeleton of the ink inside the seeded box,
traced the same way.

Usage: measure_print.py OUT_JSON"""
import sys, os
import numpy as np
import cv2
from skimage.morphology import skeletonize

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C
import measure_lib as M
import measure_text as T
import panel_tables as PT

LEGEND_OPTS = {
    't_vco1_2': dict(max_gap=6.0), 't_vco2_2': dict(max_gap=6.0),
    't_esp': dict(font='title'),
    'l_esp_highcut': dict(xmin=396.9),
    # legends in the glare at the top right, and the large word under the KORG logo: the glyphs
    # inside a box read on a 1 mm grid
    'l_power_off': dict(box=(503.0, 64.4, 522.0, 68.8)),
    'j_sigout': dict(box=(527.5, 24.4, 546.5, 28.6)),
    'l_volume': dict(box=(498.0, 24.4, 511.0, 28.6)),
    'l_synthesizer': dict(box=(488.5, 181.4, 541.0, 191.0)),
}
LOGO_BOX = (488.5, 165.0, 534.0, 181.4)      # search box of the KORG logotype (ink box measured)


def legends(R, exclude):
    out = {}
    for lid, text, cx, cy, font, align in PT.LEGENDS:
        o = LEGEND_OPTS.get(lid, {})
        f = o.get('font', font)
        if 'box' in o:
            r = T.measure_box(R, o['box'], f)
        else:
            r = T.measure_legend(R, text, cx, cy, f, max_gap=o.get('max_gap'), exclude=exclude, xmin=o.get('xmin'))
        if r is None:
            print('legend FAIL', lid, text)
            continue
        r.update(text=text, font=font, align=align)
        out[lid] = r
    return out


def side_line(R, p0, p1, half=0.8):
    pts, wid = M.fit_line_profile(R, p0, p1, half=half, step=0.2)
    if len(pts) < 5:
        return None
    return pts, wid


def fit_box(R, box):
    """Stroke-centre rectangle of a printed box: each side fitted between its corners (inset 0.8 mm)."""
    x0, y0, x1, y1 = box
    sides = {}
    ins = 0.9
    segs = {'top': ((x0 + ins, y0), (x1 - ins, y0)), 'bottom': ((x0 + ins, y1), (x1 - ins, y1)),
            'left': ((x0, y0 + ins), (x0, y1 - ins)), 'right': ((x1, y0 + ins), (x1, y1 - ins))}
    widths = []
    for name, (a, b) in segs.items():
        r = side_line(R, a, b)
        if r is None:
            return None
        pts, wid = r
        coord = 1 if name in ('top', 'bottom') else 0
        v = pts[:, coord]
        med = np.median(v)
        keep = np.abs(v - med) < 0.2
        sides[name] = float(np.mean(v[keep]))
        widths.extend(wid[keep].tolist())
    return dict(bbox=[sides['left'], sides['top'], sides['right'], sides['bottom']], stroke=float(np.median(widths)))


def fit_triangle(R, tri):
    """Amplifier triangle pointing right: vertical left side, two slanted sides meeting at the tip."""
    x0, y0, x1, y1 = tri
    ym = 0.5 * (y0 + y1)
    left = side_line(R, (x0, y0 + 1.2), (x0, y1 - 1.2))
    up = side_line(R, (x0 + 1.0, y0 + 0.4 + (1.0 / (x1 - x0)) * (ym - y0)), (x1 - 1.0, ym - (1.0 / (x1 - x0)) * (ym - y0)), half=0.9)
    dn = side_line(R, (x0 + 1.0, y1 - 0.4 - (1.0 / (x1 - x0)) * (y1 - ym)), (x1 - 1.0, ym + (1.0 / (x1 - x0)) * (y1 - ym)), half=0.9)
    if left is None or up is None or dn is None:
        return None
    xl = float(np.median(left[0][:, 0]))
    pu = np.polyfit(up[0][:, 0], up[0][:, 1], 1)
    pd = np.polyfit(dn[0][:, 0], dn[0][:, 1], 1)
    xt = (pd[1] - pu[1]) / (pu[0] - pd[0]); yt = np.polyval(pu, xt)
    a = [xl, float(np.polyval(pu, xl))]; b = [xl, float(np.polyval(pd, xl))]
    return dict(points=[a, [float(xt), float(yt)], b], stroke=float(np.median(np.r_[left[1], up[1], dn[1]])))


def trace_skeleton(mask, R, eps=0.12):
    """Polylines (mm) of a binary mask's skeleton: walk from ends and junctions."""
    sk = skeletonize(mask > 0).astype(np.uint8)
    H, W = sk.shape
    nb = cv2.filter2D(sk, -1, np.ones((3, 3), np.float32), borderType=cv2.BORDER_CONSTANT) - sk
    nb = nb * sk
    nodes = set(zip(*np.nonzero((nb == 1) | (nb >= 3))))
    visited = set()
    polys = []

    def neigh(p):
        y, x = p
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if (dy or dx) and 0 <= y + dy < H and 0 <= x + dx < W and sk[y + dy, x + dx]:
                    yield (y + dy, x + dx)

    def walk(start, nxt):
        path = [start, nxt]
        visited.add(frozenset((start, nxt)))
        prev, cur = start, nxt
        while cur not in nodes:
            cand = [q for q in neigh(cur) if q != prev and frozenset((cur, q)) not in visited]
            if not cand:
                break
            # prefer 4-neighbours
            cand.sort(key=lambda q: abs(q[0] - cur[0]) + abs(q[1] - cur[1]))
            q = cand[0]
            visited.add(frozenset((cur, q)))
            path.append(q)
            prev, cur = cur, q
        return path

    for n0 in list(nodes):
        for q in neigh(n0):
            if frozenset((n0, q)) not in visited:
                polys.append(walk(n0, q))
    # closed loops without nodes
    rest = sk.copy()
    for pl in polys:
        for y, x in pl:
            rest[y, x] = 0
    ys, xs = np.nonzero(rest)
    for y, x in zip(ys, xs):
        if rest[y, x]:
            p0 = (y, x)
            q = next(iter(neigh(p0)), None)
            if q is None:
                continue
            pl = walk(p0, q)
            for yy, xx in pl:
                rest[yy, xx] = 0
            polys.append(pl)
    out = []
    for pl in polys:
        if len(pl) < 3:
            continue
        a = np.array([[x, y] for y, x in pl], np.float32).reshape(-1, 1, 2)
        ap = cv2.approxPolyDP(a, eps * R.P, False)[:, 0, :]
        out.append(ap)
    return out


def region_mask(R, box):
    x0, y0 = R.px(box[0], box[1]); x1, y1 = R.px(box[2], box[3])
    return int(x0), int(y0), int(np.ceil(x1)), int(np.ceil(y1))


def main():
    R = M.Rect()
    kn = C.load_json(os.path.join(C.OUT, 'knobs.json'))
    jk = C.load_json(os.path.join(C.OUT, 'jacks.json'))['jacks']
    # areas the line tracer and the legends must ignore: jack nuts (as the photo shows them at h=0)
    excl = np.zeros(R.mask.shape, np.uint8)
    for k, j in jk.items():
        px, py = R.px(*j['nut_c'])
        rr = (min(j['nut_af'], 13.4) / np.sqrt(3) + 0.15) * R.P
        cv2.circle(excl, (int(px), int(py)), int(rr), 1, -1)
    excl_sym = np.zeros(R.mask.shape, np.uint8)
    for k, j in jk.items():
        px, py = R.px(*j['c'])
        cv2.circle(excl_sym, (int(px), int(py)), int((min(j['nut_af'], 13.4) / 2 + 0.3) * R.P), 1, -1)
    leg = legends(R, excl)
    out = dict(legends=leg)
    lg = T.measure_box(R, LOGO_BOX, 'name', max_h=12.0, max_w=12.0)
    out['logo_korg'] = lg
    print('KORG logotype ink box [%.2f %.2f %.2f %.2f]' % tuple(lg['bbox']))
    # boxes and triangles
    boxes = {}
    for k, b in PT.BOXES.items():
        r = fit_box(R, b)
        if r is None:
            print('box FAIL', k)
            continue
        boxes[k] = r
        print('%-10s [%.2f %.2f %.2f %.2f] stroke %.2f' % (k, *r['bbox'], r['stroke']))
    tris = {}
    for k, t in PT.TRIANGLES.items():
        r = fit_triangle(R, t)
        if r is None:
            print('tri FAIL', k)
            continue
        tris[k] = r
        print('%-16s %s stroke %.2f' % (k, np.round(r['points'], 2).tolist(), r['stroke']))
    out['boxes'] = boxes; out['triangles'] = tris
    # symbols: skeleton polylines inside their boxes
    syms = {}
    for k, (b, desc) in PT.SYMBOLS.items():
        x0, y0, x1, y1 = region_mask(R, b)
        sub = R.mask[y0:y1, x0:x1].copy()
        sub[excl_sym[y0:y1, x0:x1] > 0] = 0
        # only components fully inside the box
        n, lab, st, _ = cv2.connectedComponentsWithStats(sub, 8)
        keep = np.zeros_like(sub)
        for i in range(1, n):
            l, t, w, h, a = st[i]
            if l > 0 and t > 0 and l + w < sub.shape[1] and t + h < sub.shape[0] and a >= 4:
                keep[lab == i] = 1
        if keep.sum() == 0:
            print('symbol FAIL', k)
            continue
        ys, xs = np.nonzero(keep)
        bb0 = R.mm(xs.min() + x0 - 0.5, ys.min() + y0 - 0.5); bb1 = R.mm(xs.max() + x0 + 0.5, ys.max() + y0 + 0.5)
        dt = cv2.distanceTransform(keep, cv2.DIST_L2, 5)
        sk = skeletonize(keep > 0)
        sw = float(2 * np.median(dt[sk]) / R.P) if sk.any() else None
        polys = trace_skeleton(keep, R, eps=0.08)
        pl = [[[round(float(R.mm(x + x0, y + y0)[0]), 2), round(float(R.mm(x + x0, y + y0)[1]), 2)] for x, y in p] for p in polys]
        syms[k] = dict(bbox=[bb0[0], bb0[1], bb1[0], bb1[1]], stroke=sw, polylines=pl, label=desc)
        print('%-16s bbox [%.2f %.2f %.2f %.2f] stroke %.2f, %d polylines' % (k, *syms[k]['bbox'], sw or -1, len(pl)))
    out['symbols'] = syms
    # small two-line voltage marks
    small = {}
    for sid, text, x0, y0, x1, y1 in PT.SMALL:
        # the print pixels inside the box (glyphs of the two lines can touch: no component logic)
        px0, py0 = R.px(x0, y0); px1, py1 = R.px(x1, y1)
        px0, py0, px1, py1 = int(px0), int(py0), int(np.ceil(px1)), int(np.ceil(py1))
        sub = R.mask[py0:py1, px0:px1].copy()
        sub[excl_sym[py0:py1, px0:px1] > 0] = 0
        ys, xs = np.nonzero(sub)
        if len(xs) < 8:
            print('small FAIL', sid)
            continue
        bx0, by0 = R.mm(xs.min() + px0 - 0.5, ys.min() + py0 - 0.5)
        bx1, by1 = R.mm(xs.max() + px0 + 0.5, ys.max() + py0 + 0.5)
        bb = [float(bx0), float(by0), float(bx1), float(by1)]
        small[sid] = dict(text=text, bbox=bb, cap_h=float(by1 - by0), x=float((bx0 + bx1) / 2), y=float((by0 + by1) / 2),
                          stroke=None)
        print('%-12s %-6s [%.2f %.2f %.2f %.2f] cap %.2f' % (sid, text, *bb, by1 - by0))
    out['small'] = small
    # ---- lines: everything else ----
    m = R.mask.copy()
    m[excl > 0] = 0
    for d in (leg, small):
        for r in d.values():
            x0, y0, x1, y1 = region_mask(R, [r['bbox'][0] - 0.25, r['bbox'][1] - 0.25, r['bbox'][2] + 0.25, r['bbox'][3] + 0.25])
            m[y0:y1, x0:x1] = 0
    for r in syms.values():
        x0, y0, x1, y1 = region_mask(R, [r['bbox'][0] - 0.2, r['bbox'][1] - 0.2, r['bbox'][2] + 0.2, r['bbox'][3] + 0.2])
        m[y0:y1, x0:x1] = 0
    for r in boxes.values():
        b = r['bbox']
        for (a0, a1) in (((b[0], b[1]), (b[2], b[1])), ((b[0], b[3]), (b[2], b[3])), ((b[0], b[1]), (b[0], b[3])), ((b[2], b[1]), (b[2], b[3]))):
            p0 = R.px(*a0); p1 = R.px(*a1)
            cv2.line(m, (int(round(p0[0])), int(round(p0[1]))), (int(round(p1[0])), int(round(p1[1]))), 0, int(0.9 * R.P))
    for r in tris.values():
        p = np.array([R.px(*q) for q in r['points']], np.int32)
        cv2.polylines(m, [p], True, 0, int(0.9 * R.P))
    # dials: ticks and numerals
    for k, v in kn.items():
        px, py = R.px(*v['c'])
        cv2.circle(m, (int(px), int(py)), int((v['r_tick_out'] + 4.6) * R.P), 0, -1)
    # outside the printed panel face
    m[:, :int(R.px(12.0, 0)[0])] = 0
    m[:, int(R.px(557.0, 0)[0]):] = 0
    m[:int(R.px(0, 21.5)[1]), :] = 0
    m[int(R.px(0, 198.6)[1]):, :] = 0
    n, lab, st, _ = cv2.connectedComponentsWithStats(m, 8)
    lines = []
    arrows = []
    for i in range(1, n):
        l, t, w, h, a = st[i]
        if max(w, h) < 2.5 * R.P or a < 20:
            continue
        comp = (lab[t:t + h, l:l + w] == i).astype(np.uint8)
        comp = np.pad(comp, 2)
        dt = cv2.distanceTransform(comp, cv2.DIST_L2, 5)
        sk = skeletonize(comp > 0)
        half = np.median(dt[sk])
        # arrowheads: blobs much wider than the line
        wide = (dt > max(2.2 * half, 0.22 * R.P)).astype(np.uint8)
        if wide.any():
            nw, lw, sw_, cw = cv2.connectedComponentsWithStats(cv2.dilate(wide, np.ones((3, 3), np.uint8)), 8)
            for j in range(1, nw):
                cyx = cw[j]
                # grow back to the full head: comp pixels within 1.6 mm of the wide core
                core = (lw == j).astype(np.uint8)
                reach = cv2.dilate(core, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (int(1.6 * R.P),) * 2))
                head = (reach & comp & (dt > half * 1.25)).astype(np.uint8)
                cnts, _ = cv2.findContours(head, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
                if not cnts:
                    continue
                cnt = max(cnts, key=cv2.contourArea)
                if cv2.contourArea(cnt) < 0.25 * R.P ** 2:
                    continue
                ok, tri = cv2.minEnclosingTriangle(cnt.astype(np.float32))
                tri = np.asarray(tri).reshape(-1, 2)
                pts = [[float(R.mm(x + l - 2, y + t - 2)[0]), float(R.mm(x + l - 2, y + t - 2)[1])] for x, y in tri]
                arrows.append(dict(points=pts, area=float(cv2.contourArea(cnt) / R.P ** 2)))
        polys = trace_skeleton(comp, R)
        for p in polys:
            q = [[round(float(R.mm(x + l - 2, y + t - 2)[0]), 2), round(float(R.mm(x + l - 2, y + t - 2)[1]), 2)] for x, y in p]
            L = sum(np.hypot(q[k + 1][0] - q[k][0], q[k + 1][1] - q[k][1]) for k in range(len(q) - 1))
            if L < 1.0:
                continue
            # stroke width along it
            ws = []
            for k in range(len(q) - 1):
                if np.hypot(q[k + 1][0] - q[k][0], q[k + 1][1] - q[k][1]) > 2.0:
                    a0 = np.array(q[k]); a1 = np.array(q[k + 1]); dd = (a1 - a0) / np.linalg.norm(a1 - a0)
                    _, wd = M.fit_line_profile(R, a0 + dd * 0.8, a1 - dd * 0.8, half=0.9, step=0.5)
                    ws.extend(wd.tolist())
            lines.append(dict(points=q, length=float(L), stroke=float(np.median(ws)) if ws else float(2 * half / R.P)))
    out['lines'] = lines
    out['arrows'] = arrows
    print('%d lines, %d arrowheads' % (len(lines), len(arrows)))
    C.save_json(out, sys.argv[1])


if __name__ == '__main__':
    main()
