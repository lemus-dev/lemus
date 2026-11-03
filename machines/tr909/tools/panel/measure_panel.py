#!/usr/bin/env python3
"""Measure every element of the TR-909 top panel on the regularised master and write
docs/panel.json (plus an overlay check image).

Usage: measure_panel.py MASTER_REG_PNG RECT_JSON REG_JSON OUT_JSON [OVERLAY_JPG]

Pipeline (see docs/panel.md):
  rectify.py     photo -> 486 mm wide mm-grid (homography from the case outline)
  regularise.py  remove the photo's residual bending (lens pincushion) using the panel's own
                 straight keylines and the 23.0 mm key grid
  crosscheck.py  check that against other photos and the pots of the 1:1 board artwork
  measure_panel  (this file) all elements: approximate positions from visual reading of
                 gridded zooms, refined here by edge / blob / ink-bbox detection, then snapped
                 to the obvious design rows and grids where the panel has them.
All coordinates are mm, origin = rear-left corner of the top view, x right, y to the player."""
import json, math, sys
import numpy as np
sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from measure_lib import Master, P, nmed

W_MM = 486.0
K1 = None          # x of step key 1 (from regularise.py)
PITCH = 23.0       # step-key pitch (mm)
SMALL_PITCH = 12.0 # pitch of the small illuminated buttons (switch-board artwork: 188 px @ 15.69 px/mm)
elements = []


def r2(v):
    return round(float(v), 2)


def add(**e):
    if 'bbox' in e and 'x' not in e:
        x0, y0, x1, y1 = e['bbox']
        e['x'], e['y'], e['w'], e['h'] = r2((x0 + x1) / 2), r2((y0 + y1) / 2), r2(x1 - x0), r2(y1 - y0)
    elif 'w' in e and 'h' in e and 'bbox' not in e:
        e['bbox'] = [r2(e['x'] - e['w'] / 2), r2(e['y'] - e['h'] / 2), r2(e['x'] + e['w'] / 2), r2(e['y'] + e['h'] / 2)]
    elif 'd' in e and 'bbox' not in e:
        e['bbox'] = [r2(e['x'] - e['d'] / 2), r2(e['y'] - e['d'] / 2), r2(e['x'] + e['d'] / 2), r2(e['y'] + e['d'] / 2)]
    for k in ('x', 'y', 'w', 'h', 'd'):
        if k in e:
            e[k] = r2(e[k])
    if 'bbox' in e:
        e['bbox'] = [r2(v) for v in e['bbox']]
    if 'x' in e:
        e['nx'] = round(e['x'] / W_MM, 4)
        e['ny'] = round(e['y'] / H_MM, 4)
    elements.append(e)
    return e


# ------------------------------------------------------------------------------- colours
COLOURS = {}   # filled in main() from samples


def sample_colours(M):
    def hexc(v):
        return '#%02X%02X%02X' % tuple(int(round(c)) for c in v)
    s = {}
    s['panel'] = M.median_rgb(150, 30, 280, 70)
    s['panel_lower'] = M.median_rgb(150, 270.5, 280, 277)
    s['side_panel'] = M.median_rgb(1.5, 40, 6.5, 240)
    s['print_dark'] = M.median_rgb(150, 211.5, 200, 216)
    s['number_bar'] = M.median_rgb(90, 257, 100, 260.5)
    s['header_bar'] = M.median_rgb(74, 98.2, 82, 99.0)
    o = M.kind_mask('orange', 200, 184.8, 232, 190.6)
    s['orange_box'] = M.median_rgb(200, 184.8, 232, 190.6, o)
    o = M.kind_mask('orange', 34, 14, 116, 27)
    s['logo_orange'] = M.median_rgb(34, 14, 116, 27, o)
    s['display'] = M.median_rgb(100, 172, 131, 185)
    s['knob'] = M.median_rgb(98, 117, 102, 123)
    s['key_cap'] = M.median_rgb(76, 243, 84, 249)
    s['ivory_cap'] = M.median_rgb(46, 175, 52, 183)
    s['small_btn'] = M.median_rgb(192, 171, 195, 176)
    return {k: hexc(v) for k, v in s.items()}

# ------------------------------------------------------------------------------- step section
def light_components(M, box, min_area=4):
    """components of white print inside a dark bar; returns list of mm bboxes"""
    import cv2
    x0, y0, x1, y1 = box
    m = M.kind_mask('light', x0, y0, x1, y1).astype(np.uint8)
    n, lab, st, cen = cv2.connectedComponentsWithStats(m, 8)
    out = []
    for i in range(1, n):
        x, y, w, h, a = st[i]
        if a < min_area:
            continue
        out.append([x0 + x / P, y0 + y / P, x0 + (x + w) / P, y0 + (y + h) / P, a / P / P])
    return out


def measure_step(M, G, keyx, bound):
    H = G['hlines']; V = G['vlines']
    # ---------------- left box (keyline frame, open at the top right where the scale bars start)
    xl, xr = V['leftbox_left']['design'], V['leftbox_right']['design']
    yt, yb = H['leftbox_top']['design'], H['leftbox_bottom']['design']
    top_ext = M.hline_extent(yt, xl - 1, xr - 0.6)
    right_ext = M.vline_extent(xr, 225.0, yb + 1)
    add(id='line_leftbox', type='line', points=[[r2(top_ext[1] + 0.1), r2(yt)], [r2(xl), r2(yt)], [r2(xl), r2(yb)],
                                                 [r2(xr), r2(yb)], [r2(xr), r2(right_ext[0] - 0.1)]],
        stroke=0.5, colour='print_dark', x=r2((xl + xr) / 2), y=r2((yt + yb) / 2),
        note='keyline frame round LAST STEP ... SHIFT; open at the top right, where the four scale bars begin')
    # left-box buttons: two columns x three rows
    LB = [('btn_last_step', 'LAST STEP', 0, 0), ('btn_scale', 'SCALE', 1, 0), ('btn_shuffle_flam', 'SHUFFLE/FLAM', 0, 1),
          ('btn_clear', 'CLEAR', 1, 1), ('btn_instrument_select', 'INSTRUMENT SELECT', 0, 2), ('btn_shift', 'SHIFT', 1, 2)]
    approx = {(0, 0): (45.66, 206.6), (1, 0): (60.72, 206.6), (0, 1): (45.66, 224.7), (1, 1): (60.72, 224.7),
              (0, 2): (45.66, 242.8), (1, 2): (60.72, 242.8)}
    bl = {}
    for bid, lab, c, r in LB:
        x, y = approx[(c, r)]
        bl[(c, r)] = M.blob((x - 6, y - 6, x + 6, y + 6), lambda rgb, g: g < 165)
    colx = [np.median([bl[(c, r)]['cx'] for r in range(3)]) for c in range(2)]
    rowy = [np.median([bl[(c, r)]['cy'] for c in range(2)]) for r in range(3)]
    for bid, lab, c, r in LB:
        add(id=bid, type='button', label=lab, x=r2(colx[c]), y=r2(rowy[r]), w=8.6, h=8.6, style='small',
            illuminated=False, colour='small_btn', cap_top=[6.6, 6.6], height=5.0,
            note='same translucent cap as the middle-row buttons, no LED behind it (SW17-SW24 have none on the switch board)')
    LT = [('txt_last_step', 'LAST STEP', [38.1, 212.4, 52.5, 214.6]), ('txt_scale', 'SCALE', [56.0, 212.4, 64.6, 214.5]),
          ('txt_shuffle', 'SHUFFLE', [39.2, 231.0, 51.3, 233.3]), ('txt_flam', '/FLAM', [40.0, 233.5, 48.9, 236.0]),
          ('txt_clear', 'CLEAR', [56.0, 231.0, 64.8, 233.1]), ('txt_instrument', 'INSTRUMENT', [36.5, 248.6, 53.9, 250.9]),
          ('txt_select', 'SELECT', [40.0, 251.2, 50.3, 253.5])]
    for tid, lab, box in LT:
        bb = M.components_bbox('dark', box, margin=0.45)
        add(id=tid, type='text', bbox=bb, label=lab, cap_h=r2(bb[3] - bb[1]), font='Helvetica Medium, caps', colour='print_dark')
    sb = M.refine_rect([56.0, 248.9, 65.2, 253.3], inside_dark=True, half=0.7)
    add(id='obox_shift', type='label_box', bbox=sb, label='SHIFT', colour='orange_box', text_colour='print_dark',
        font='Helvetica Medium, caps', corner_r=0.2)
    t = M.components_bbox('dark_on_orange', [sb[0] + 0.4, sb[1] + 0.4, sb[2] - 0.4, sb[3] - 0.4], margin=0, drop_border_lines=False)
    add(id='txt_shift', type='text', bbox=t, label='SHIFT', cap_h=r2(t[3] - t[1]), font='Helvetica Medium, caps', colour='print_dark')

    # ---------------- the four scale bars
    gaps = {1: [6, 12], 2: [3, 6, 9, 12], 3: [8], 4: [4, 8, 12]}       # key boundaries j|j+1 where a bar is split
    rows = {}
    for r in (1, 2, 3, 4):
        y0, y1 = H['row%d_top' % r]['design'], H['row%d_bot' % r]['design']
        rows[r] = (y0, y1)
        xs_left = r2(nmed([M.edge_x(y, 68.6, -1, half=1.0) for y in np.linspace(y0 + 1, y1 - 1, 7)]))
        xs_right = r2(nmed([M.edge_x(y, 439.2, +1, half=1.0) for y in np.linspace(y0 + 1, y1 - 1, 7)]))
        cuts = []
        for j in gaps[r]:
            b = bound(j)
            a_ = nmed([M.edge_x(y, b - 0.6, +1, half=0.9) for y in np.linspace(y0 + 1, y1 - 1, 7)])
            b_ = nmed([M.edge_x(y, b + 0.6, -1, half=0.9) for y in np.linspace(y0 + 1, y1 - 1, 7)])
            cuts.append((r2(a_), r2(b_)))
        segs = []
        x = xs_left
        for a_, b_ in cuts:
            segs.append((x, a_)); x = b_
        segs.append((x, xs_right))
        for i, (a_, b_) in enumerate(segs):
            add(id='bar_scale%d_%d' % (r, i + 1), type='fill', bbox=[a_, r2(y0), b_, r2(y1)], colour='print_dark', corner_r=0.15,
                note='scale indicator bar, row %d' % r)
        # LED at the left end of each bar
        cy = (y0 + y1) / 2
        led = M.blob((70.5, cy - 2.2, 76.0, cy + 2.2), lambda rgb, g: (rgb[..., 0] - rgb[..., 2] > 6) & (g < 75))
        add(id='led_scale_%d' % r, type='led', shape='round', x=73.1, y=r2(cy), d=3.0,
            colour='led_unlit', colour_lit='led_lit',
            note='3 mm red LED (D632-D635) set into the bar, lit when this scale is selected')
    # bracket lines (white) in rows 1 and 2: horizontal run ~0.85 mm below the bar top, slanted ends
    groups = {1: [(1, 6), (7, 12), (13, 16)], 2: [(1, 3), (4, 6), (7, 9), (10, 12), (13, 15), (16, 16)]}
    for r in (1, 2):
        y0, y1 = rows[r]
        for k0, k1 in groups[r]:
            # height of the horizontal run for this group: brightest row in the upper band of the bar
            xm = np.arange(keyx(k0) + 3.0, keyx(k1) + (2.0 if k1 < 16 else 8.0), 1.0)
            if r == 1 and k1 < 16:
                xm = np.arange(keyx(k0) + 3.0, keyx(k1) - 3.0, 1.0)
            ys = [M.line_y(x, y0 + 1.1, half=0.75, hw=0.2, dark=False) for x in xm]
            ys = [v for v in ys if v is not None]
            yrun = r2(np.median(ys))
            xa = keyx(k0) - 8.0
            to_end = (r, k0) in ((1, 13), (2, 16))       # these run on to the right end of the bar
            xgrid = np.arange(xa, 438.5 if to_end else min(keyx(k1) + 11.0, 438.5), 0.25)
            band = [M.line_y(x, yrun, half=0.45, hw=0.12, dark=False) for x in xgrid]
            xs = [x for x, v in zip(xgrid, band) if v is not None and abs(v - yrun) < 0.3]
            runs, cur = [], [xs[0]] if xs else []
            for x in xs[1:]:
                if x - cur[-1] <= 3.0:
                    cur.append(x)
                else:
                    runs.append(cur); cur = [x]
            if cur:
                runs.append(cur)
            xs = max(runs, key=len)
            run0, run1 = r2(min(xs)), r2(max(xs))
            pts = [[r2(run0 - 3.2), r2(yrun + 1.6)], [run0, yrun], [run1, yrun], [r2(run1 + 3.2), r2(yrun + 1.6)]]
            if to_end:       # runs to the bar end without a down-slant
                pts = pts[:3]
            add(id='bracket_scale%d_%d_%d' % (r, k0, k1), type='line', points=pts, stroke=0.35, colour='print_white',
                x=r2((run0 + run1) / 2), y=yrun, note='white beam/bracket line over the notes of one beat group')
    # thin white upright splitting row 2 between steps 15 and 16 (the bar itself is not cut there)
    y0, y1 = rows[2]
    xw = nmed([M.line_x(y, bound(15), half=1.0, hh=0.2, dark=False) for y in np.linspace(y0 + 1.5, y1 - 1.0, 6)], bound(15))
    ext = M.vline_extent(xw, y0 + 0.3, y1 - 0.2, dark=False)
    add(id='line_scale2_div_15_16', type='line', points=[[r2(xw), r2(ext[0] if ext else y0 + 1.4)], [r2(xw), r2(ext[1] if ext else y1 - 0.8)]],
        stroke=0.35, colour='print_white', x=r2(xw), y=r2((y0 + y1) / 2), note='white divider inside scale bar 2')
    # note glyphs: eighth notes (rows 1, 2), quarter notes (rows 3, 4). Measured on the clean (left) part of
    # the bars, then every glyph is placed with the same size and offset from its key centre / bar top.
    notes = {1: [1, 3, 5, 7, 9, 11, 13, 15], 2: list(range(1, 17)), 3: [1, 9], 4: [1, 5, 9, 13]}
    meas = {1: [], 2: []}
    for r in (1, 2, 3, 4):
        y0, y1 = rows[r]
        for k in notes[r]:
            if keyx(k) > 345:          # glare on the right-hand bar segments spoils the threshold
                continue
            xk = keyx(k)
            comps = light_components(M, [xk - 3.2, y0 + 1.5, xk + 3.2, y1 - 0.2])
            comps = [c for c in comps if c[4] > 0.08 and (c[3] - c[1]) > 0.45]
            a = np.array(comps)
            bb = [a[:, 0].min() - xk, a[:, 1].min() - y0, a[:, 2].max() - xk, a[:, 3].max() - y0]
            meas[1 if r <= 2 else 2].append(bb)
    glyph = {t: [r2(np.median([b[i] for b in meas[t]])) for i in range(4)] for t in (1, 2)}
    for r in (1, 2, 3, 4):
        y0, y1 = rows[r]
        g = glyph[1 if r <= 2 else 2]
        for k in notes[r]:
            xk = keyx(k)
            add(id='note_scale%d_%d' % (r, k), type='symbol', label='eighth_note' if r <= 2 else 'quarter_note',
                bbox=[r2(xk + g[0]), r2(y0 + g[1]), r2(xk + g[2]), r2(y0 + g[3])], colour='print_white', step=k,
                note='white note glyph: filled oval head (lower left, tilted), stem up from its right side%s' % (
                    ', one flag curling down to the right' if r <= 2 else ''))
    # thin keyline above the step numbers
    yk = r2(np.median([v for v in [M.line_y(x, 226.5, half=0.8, hw=0.2) for x in np.arange(85, 425, 5)] if v]))
    for a, b in ((75.0, 240.0), (250.0, 436.0)):
        ext = M.hline_extent(yk, a, b)
        if ext:
            add(id='line_steps_%d_%d' % (int(a), int(b)), type='line', points=[[r2(ext[0]), yk], [r2(ext[1]), yk]], stroke=0.4,
                colour='print_dark', x=r2((ext[0] + ext[1]) / 2), y=yk)
    # step dividers between key pairs and the right edge line
    for j in (2, 4, 6, 8, 10, 12, 14):
        x = bound(j)
        ext = M.vline_extent(x, 225.0, 257.0)
        add(id='line_step_div_%d_%d' % (j, j + 1), type='line', points=[[r2(x), r2(ext[0] - 0.12)], [r2(x), r2(ext[1] + 0.12)]],
            stroke=0.5, colour='print_dark', x=r2(x), y=r2((ext[0] + ext[1]) / 2))
    xr = V['step_div_7']['design']
    ext = M.vline_extent(xr, 194.6, 272.0)
    add(id='line_step_right', type='line', points=[[r2(xr), r2(ext[0] - 0.12)], [r2(xr), r2(ext[1] + 0.12)]], stroke=0.5,
        colour='print_dark', x=r2(xr), y=r2((ext[0] + ext[1]) / 2), note='right edge line of the step section')
    # step numbers above the keys
    nb = []
    for k in range(1, 17):
        xk = keyx(k)
        nb.append(M.components_bbox('dark', [xk - 2.4, 227.4, xk + 2.4, 231.0], margin=0.3))
    ny0 = r2(np.median([b[1] for b in nb])); ny1 = r2(np.median([b[3] for b in nb]))
    for k, b in zip(range(1, 17), nb):
        add(id='txt_step_%d' % k, type='text', bbox=[b[0], ny0, b[2], ny1], label=str(k), cap_h=r2(ny1 - ny0),
            font='Helvetica Bold', colour='print_dark')
    # step keys: hole = panel cut-out; LED window in the key top
    for k in range(1, 17):
        xk = keyx(k)
        top = np.median([v for v in [M.edge_y(x, 233.3, -1, half=1.2) for x in np.linspace(xk - 6, xk + 6, 9)] if v])
        bot = np.median([v for v in [M.edge_y(x, 252.4, +1, half=1.2) for x in np.linspace(xk - 6, xk + 6, 9)] if v])
        add(id='key_step_%d' % k, type='button', label=str(k), x=r2(xk), y=r2((top + bot) / 2), w=18.85, h=r2(bot - top),
            style='step', colour='key_cap', cap_top=[13.0, 13.0], height=9.0, step=k,
            functions=dict(step=k, instrument_select=['BD', 'BD', 'SD', 'SD', 'LT', 'LT', 'MT', 'MT', 'HT', 'HT', 'RS', 'HC',
                                                      'CH', 'OH', 'CRASH', 'RIDE'][k - 1],
                           shift=['1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'COPY', 'INS', 'DEL', 'SAVE', 'VERIFY', 'LOAD'][k - 1]),
            note='grey key top (2247029100) on a KFL11904 switch with built-in LED; bbox = panel cut-out; x on the 23.0 mm grid')
        win = M.blob((xk - 6, 236, xk + 4, 241.5), lambda rgb, g: g < 90)
        add(id='led_step_%d' % k, type='led', shape='rect', x=r2(xk), y=r2(win['cy']), w=5.4, h=2.3, colour='led_window',
            colour_lit='led_lit', note='LED window in the key top, centred on the key (photo shows it shifted by '
            'perspective lean, %.2f mm here)' % (win['cx'] - xk))
    # LED windows share one row (the per-key scatter is perspective lean of the raised key tops)
    wins = [e for e in elements if e['id'].startswith('led_step_')]
    wy = r2(np.median([e['y'] for e in wins]))
    for e in wins:
        e['y'] = wy; e['bbox'] = [r2(e['x'] - e['w'] / 2), r2(wy - e['h'] / 2), r2(e['x'] + e['w'] / 2), r2(wy + e['h'] / 2)]
        e['ny'] = round(wy / H_MM, 4)
    # number bar + white legends
    nbar = [r2(nmed([M.edge_x(y, 72.3, -1, half=1.0) for y in np.linspace(256.5, 260.7, 5)])), H['numbar_top']['design'],
            r2(nmed([M.edge_x(y, 437.0, +1, half=1.0) for y in np.linspace(256.5, 260.7, 5)])), H['numbar_bot']['design']]
    add(id='bar_numbers', type='fill', bbox=[r2(v) for v in nbar], colour='print_dark', corner_r=0.15,
        note='bar under the step keys carrying the SHIFT functions in white')
    legs = ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'COPY', 'INS', 'DEL', 'SAVE', 'VERIFY', 'LOAD']
    lb = []
    for k, lab in zip(range(1, 17), legs):
        xk = keyx(k); hw = 1.6 if len(lab) == 1 else 1.55 * len(lab) + 0.6
        comps = light_components(M, [xk - hw, nbar[1] + 0.6, xk + hw, nbar[3] - 0.6])
        a = np.array([c for c in comps if c[4] > 0.05])
        lb.append([a[:, 0].min(), a[:, 1].min(), a[:, 2].max(), a[:, 3].max()])
    ly0 = r2(np.median([b[1] for b in lb])); ly1 = r2(np.median([b[3] for b in lb]))
    for k, lab, b in zip(range(1, 17), legs, lb):
        add(id='txt_shift_%d' % k, type='text', bbox=[r2(b[0]), ly0, r2(b[2]), ly1], label=lab, cap_h=r2(ly1 - ly0),
            font='Helvetica Bold, caps', colour='print_white')
    # legend boxes (instrument select) with their tabs
    BOX = [('box_bass_drum', [('BASS DRUM', None)], 1, 2, 'left'), ('box_snare_drum', [('SNARE DRUM', None)], 3, 4, 'left'),
           ('box_low_tom', [('LOW TOM', None)], 5, 6, 'left'), ('box_mid_tom', [('MID TOM', None)], 7, 8, 'left'),
           ('box_hi_tom', [('HI TOM', None)], 9, 10, 'left'), ('box_rim_hand', [('RIM SHOT', 11), ('HAND CLAP', 12)], 11, 12, None),
           ('box_hihat', [('CLOSED', 13), ('OPEN', 'open'), ('CLOSED', 'closed2')], 13, 14, 'left'),
           ('box_cymbal', [('CRASH', 15), ('RIDE', 16)], 15, 16, None)]
    ytop_hi = r2(np.median([v for v in [M.line_y(x, 262.6, half=0.6, hw=0.2) for x in (76, 80, 84, 122, 126, 168, 172)] if v]))
    ybot = H['leftbox_bottom']['design']
    for bid, words, k0, k1, tab in BOX:
        xa, xb = keyx(k0) - 9.5, keyx(k1) + 9.6
        xa = r2(nmed([M.line_x(y, xa, half=1.0, hh=0.15) for y in (264.5, 265.5, 266.5)]))
        xb = r2(nmed([M.line_x(y, xb, half=1.0, hh=0.15) for y in (264.5, 265.5, 266.5)]))
        yb = r2(np.median([v for v in [M.line_y(x, ybot, half=0.7, hw=0.2) for x in np.linspace(xa + 3, xb - 3, 7)] if v]))
        # top edge profile: find the two levels and the slant
        xs = np.arange(xa + 1.5, xb - 1.5, 0.25)
        ys = np.array([M.line_y(x, 263.1, half=1.0, hw=0.1) or np.nan for x in xs])
        lo = np.nanmedian(ys[ys > 263.15]) if np.any(ys > 263.15) else np.nan
        hi = np.nanmedian(ys[ys < 263.0]) if np.any(ys < 263.0) else np.nan
        if tab and not (np.isnan(lo) or np.isnan(hi)):
            mid = (lo + hi) / 2
            if tab == 'left':
                xs_hi = xs[ys < mid]; t0 = xs_hi.max()        # last x on the high (tab) level
                pts = [[xa, r2(hi)], [r2(t0 - 0.6), r2(hi)], [r2(t0 + 1.4), r2(lo)], [xb, r2(lo)], [xb, yb], [xa, yb]]
            else:
                xs_hi = xs[ys < mid]; t0 = xs_hi.min()
                pts = [[xa, r2(lo)], [r2(t0 - 1.4), r2(lo)], [r2(t0 + 0.6), r2(hi)], [xb, r2(hi)], [xb, yb], [xa, yb]]
        else:
            yt = r2(np.nanmedian(ys))
            pts = [[xa, yt], [xb, yt], [xb, yb], [xa, yb]]
        add(id=bid, type='outline_box', points=pts, closed=True, stroke=0.45, corner_r=0.9, colour='print_dark',
            bbox=[xa, r2(min(p[1] for p in pts)), xb, yb], label=' / '.join(w for w, _ in words),
            note={'left': 'rounded keyline box; the top edge has a raised tab over the left key',
                  'right': 'rounded keyline box; the top edge has a raised tab over the right part'}.get(tab, 'rounded keyline box'))
        # texts
        inner = [xa + 0.6, yb - 4.2, xb - 0.6, yb - 0.6]
        if len(words) == 1:
            t = M.components_bbox('dark', inner, margin=0, drop_border_lines=False, min_area=8)
            add(id='txt_' + bid[4:], type='text', bbox=t, label=words[0][0], cap_h=r2(t[3] - t[1]), font='Helvetica Bold, caps',
                colour='print_dark')
        else:
            # split the inner area into word slots by the gaps in the ink
            import cv2
            m = M.kind_mask('dark', *inner)
            colsum = m.sum(axis=0) > 0
            runs = []; i = 0
            while i < len(colsum):
                if colsum[i]:
                    j = i
                    while j < len(colsum) and colsum[j]:
                        j += 1
                    runs.append([i, j]); i = j
                else:
                    i += 1
            merged = []
            for a_, b_ in runs:
                if merged and a_ - merged[-1][1] < 1.6 * P:
                    merged[-1][1] = b_
                else:
                    merged.append([a_, b_])
            merged = [mm for mm in merged if (mm[1] - mm[0]) > 2 * P]
            for (wd, tag), (a_, b_) in zip(words, merged):
                t = M.components_bbox('dark', [inner[0] + a_ / P, inner[1], inner[0] + b_ / P, inner[3]], margin=0.1,
                                      drop_border_lines=False, min_area=8)
                add(id='txt_%s_%s' % (bid[4:], wd.lower().replace(' ', '_') if tag in (None, 11, 12, 13, 15, 16, 'open') else 'closed2'),
                    type='text', bbox=t, label=wd, cap_h=r2(t[3] - t[1]), font='Helvetica Medium, caps', colour='print_dark')

# ------------------------------------------------------------------------------- output
def colour_table(measured):
    # name: (render hex, note incl. measured values). "master" = Soundgas studio shot (contrasty, neutral-warm);
    # "close-up" = Soundgas #393354 daylight close-up; "909bd" / "bdaniel" = the two Commons photos.
    m = measured
    return {
        'panel': ('#E6E3D9', 'top panel paint, warm off-white. master %s, close-up #E4E5EA, 909bd #E4DCD2, bdaniel #E5E5DB' % m['panel']),
        'side_panel': ('#6C6650', 'olive / khaki grey side cheeks. master %s, 909bd #585239' % m['side_panel']),
        'print_dark': ('#4B505B', 'dark print (header bars, scale bars, number bar, keylines, legends, TR-909 and RHYTHM COMPOSER): '
                       'slate grey with a blue cast. master %s (studio curve crushes it), close-up #4E525B..#5B5F6A, 909bd #5E6374, '
                       'bdaniel #56617A' % m['print_dark']),
        'header_bar': ('#4B505B', 'header bars: same ink as print_dark. master %s' % m['header_bar']),
        'print_white': ('#F2F0EA', 'white in the dark bars (notes, brackets, SHIFT legends): the panel paint left open'),
        'orange_print': ('#EE7F32', 'orange text in the header bars and the pointer line on every knob; thin strokes read darker in photos'),
        'orange_box': ('#F0883A', 'orange label boxes (TRACK WRITE ... TEMPO MODE, SHIFT). master %s, close-up #CA8A54 (in shade), '
                       '909bd #DC955E, bdaniel #FF7D05' % m['orange_box']),
        'logo_orange': ('#EC7A30', 'Roland logo. master %s' % m['logo_orange']),
        'display': ('#4A141B', 'red display filter, unlit (dark maroon). master %s, 909bd #854A50 (reflection)' % m['display']),
        'led_lit': ('#FF2A1E', 'lit red LED / display segment; add a soft glow'),
        'led_unlit': ('#4A0A10', 'unlit 3 mm red LED in the scale bars (dark red dome, small highlight)'),
        'led_window': ('#3A3A3C', 'unlit rectangular LED lens in a key top (dark grey; red when lit)'),
        'knob': ('#46443F', 'knob plastic: charcoal, very slightly warm, satin. master %s, close-up #53524E, 909bd #4B4441' % m['knob']),
        'knob_skirt_shade': ('#2A2826', 'shaded side of the knob body'),
        'key_cap': ('#CDC9BC', 'step and CARTRIDGE/ENTER key tops (2247029100): light warm grey. master %s, close-up #C4C3C1, '
                    '909bd #B9B7A8, bdaniel #C9C3AD' % m['key_cap']),
        'key_cap_top': ('#DCD8CC', 'dished top face of the grey keys (lighter than the sloped sides)'),
        'key_cap_side': ('#A6A194', 'sloped sides of the grey keys'),
        'ivory_cap': ('#F1E6C6', 'START and STOP/CONT key tops (2247029200), ivory. master %s (over-exposed), 909bd #EADECA' % m['ivory_cap']),
        'small_btn': ('#8F8D8B', 'small tact-switch caps (2247029300): smoky translucent grey, frosted lighter square centre. master %s, '
                      'close-up rim #989694' % m['small_btn']),
        'small_btn_centre': ('#C9C2C0', 'frosted centre of the small caps, unlit (close-up #9E8A8A in shade); lit: #F0606A..#FF3040'),
        'key_hole': ('#141414', 'dark gap / shadow in the panel cut-outs round the keys'),
    }


def write_outputs(M, R, G, measured, big, SMALL_TICKS, out_json, overlay):
    cols = colour_table(measured)
    for e in elements:
        for k in ('colour', 'text_colour', 'pointer_colour', 'colour_lit'):
            if k in e and e[k] not in cols:
                raise SystemExit('unknown colour %s in %s' % (e[k], e['id']))
        if e.get('note') is None:
            e.pop('note', None)
    d = dict(
        source_image='private/docs/photos/soundgas/sn282297_Roland-TR-909-Rhythm-Composer-SN282297-Ed-Obrien-2.jpg',
        source_note='Soundgas studio photo of TR-909 #282297 (reference only, (c) Soundgas). Straight-on, 2400x1473 px, 4.50 px/mm.',
        master_image='build/tr909-panel/panel/master_reg.png (8 px/mm; regenerate with tools/panel/build_panel.sh)',
        units='mm',
        origin='top-left (rear-left) corner of the top view (outer edge of the left side cheek); x right, y towards the player',
        px_per_mm_in_source=R['px_per_mm_photo'],
        source_corners_px=R['corners_px'],
        homography_source_to_master_px=R['homography_photo_to_rectified_px'],
        regularisation=dict(method='rubber-sheeting on straight keylines + 23.0 mm key grid (tools/panel/regularise.py)',
                            max_abs_dx_mm=round(G['max_abs_dx'], 2), max_abs_dy_mm=round(G['max_abs_dy'], 2)),
        panel=dict(w=W_MM, h=H_MM, official_dimensions_mm=dict(w=486, d=300, h=105, weight_kg=4.5),
                   aspect=round(W_MM / H_MM, 4), top_plate_x=[e for e in elements if e['id'] == 'top_plate'][0]['bbox'][0::2],
                   note='official: 486 (W) x 300 (D) x 105 (H) mm, 4.5 kg (owner\'s manual p. 39, service notes p. 1)'),
        design_grid=dict(key_pitch=PITCH, key1_x=round(K1, 2), section_pitch=2 * PITCH, small_button_pitch=SMALL_PITCH,
                         knob_rows_y=sorted(set(e['y'] for e in elements if e['type'] == 'knob' and e.get('knob_style') == 'small')),
                         note='step keys at x = key1_x + 23.0*(k-1); instrument sections and step dividers on the key-pair '
                              'boundaries; small buttons on a 12.0 mm pitch (switch-board artwork)'),
        accuracy='printed graphics and control centres: +-0.15 mm (x), +-0.25 mm (y) relative to each other, +-0.4 mm absolute; '
                 'knob centres agree with the 1:1 voicing-board artwork to 0.09 mm rms (x) / 0.13 mm (y); 3-D sizes '
                 '(knob and key heights, cap tops) are estimates +-1.5 mm',
        small_knob_tick_stats=SMALL_TICKS,
        colours={k: dict(hex=v[0], note=v[1]) for k, v in cols.items()},
        fonts=dict(panel_legends='Helvetica Medium caps (Neue Haas / Helvetica). Header bars, step numbers, SHIFT legends and the '
                                 'instrument names in the legend boxes are Helvetica Bold.',
                   serif='"I" and "II" (BANK) are serif roman numerals (Times Roman).',
                   logos='Roland, TR-909 and RHYTHM COMPOSER are brand marks: slots only, filled from the artwork pack.'),
        elements=elements,
    )
    d['rear_panel'] = REAR_PANEL
    d['ui_suggestions'] = UI_SUGGESTIONS
    json.dump(d, open(out_json, 'w'), indent=1)
    print(len(elements), 'elements ->', out_json)
    if overlay:
        draw_overlay(M, overlay)


def draw_overlay(M, path):
    from PIL import Image, ImageDraw
    im = Image.fromarray(M.rgb.astype(np.uint8)).convert('RGB')
    dr = ImageDraw.Draw(im, 'RGBA')
    S = P
    C = dict(knob=(0, 200, 0), button=(0, 90, 255), led=(255, 0, 255), text=(255, 120, 0), label_box=(255, 210, 0),
             logo=(255, 0, 0), line=(140, 0, 200), fill=(0, 200, 200), outline_box=(0, 160, 160), symbol=(255, 255, 0),
             display=(255, 60, 60), region=(120, 120, 120), case=(60, 60, 60))
    for e in elements:
        c = C.get(e['type'], (255, 255, 255))
        if e['type'] == 'line' or (e['type'] == 'outline_box' and 'points' in e):
            pts = [(x * S, y * S) for x, y in e['points']]
            if e.get('closed'):
                pts.append(pts[0])
            dr.line(pts, fill=c + (230,), width=2)
            continue
        if e['type'] in ('region', 'case'):
            continue
        x0, y0, x1, y1 = [v * S for v in e['bbox']]
        if e['type'] == 'knob':
            r = e['d'] / 2 * S
            dr.ellipse([e['x'] * S - r, e['y'] * S - r, e['x'] * S + r, e['y'] * S + r], outline=c + (255,), width=2)
            sc = e['scale']
            for a in sc['angles_deg']:
                ro = sc['r_outer_long'] if a in sc['long_at_deg'] else sc['r_outer']
                ca, sa = math.sin(math.radians(a)), -math.cos(math.radians(a))
                dr.line([(e['x'] + ca * sc['r_inner']) * S, (e['y'] + sa * sc['r_inner']) * S,
                         (e['x'] + ca * ro) * S, (e['y'] + sa * ro) * S], fill=c + (200,), width=1)
            dr.line([e['x'] * S - 6, e['y'] * S, e['x'] * S + 6, e['y'] * S], fill=c + (255,), width=1)
            dr.line([e['x'] * S, e['y'] * S - 6, e['x'] * S, e['y'] * S + 6], fill=c + (255,), width=1)
        elif e['type'] == 'led' and e.get('shape') == 'round':
            dr.ellipse([x0, y0, x1, y1], outline=c + (255,), width=2)
        else:
            dr.rectangle([x0, y0, x1, y1], outline=c + (230,), width=1)
    im.save(path, quality=88)
    print('overlay ->', path)


REAR_PANEL = {
 "source": "private/docs/photos/soundgas/sn282297_Roland-TR-909-Rhythm-Composer-SN282297-Ed-Obrien-7.jpg (straight-on rear view); cross-checked with Commons RolandTR-909-rear.jpg and the jack-board artwork (service notes p. 12)",
 "note": "Seen from behind, so left and right are swapped: x is given in TOP-PANEL coordinates (mm from the left side as seen from the front), y in mm down from the top edge of the rear face. Scale 4.47 px/mm from the 486 mm width. Accuracy about +-1 mm. The rear face is about 97 mm tall (case height 105 mm incl. feet). Legends are dark slate print like the top; MASTER OUT / MULTI OUT have keylines under their titles, EXT CONTROL / TAPE MEMORY/SYNC / MIDI have keylines above their titles, and a bracket joins the two MIDI OUT sockets.",
 "items": [
  {
   "id": "rear_cartridge_slot",
   "label": "CARTRIDGE (M-64C)",
   "kind": "memory cartridge slot, black, 25.7 x 56 mm opening, long side vertical",
   "x": 457.3,
   "y": 35.7,
   "w": 25.7,
   "h": 56.4
  },
  {
   "id": "rear_master_out_r",
   "label": "MASTER OUT R(MONO)",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 399.4,
   "y": 29.9,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_master_out_l",
   "label": "MASTER OUT L",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 381.5,
   "y": 30.0,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_out_ride",
   "label": "MULTI OUT RIDE",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 359.5,
   "y": 30.1,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_out_crash",
   "label": "MULTI OUT CRASH",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 341.5,
   "y": 30.2,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_out_hihat",
   "label": "MULTI OUT HI HAT",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 322.5,
   "y": 30.3,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_out_clap",
   "label": "MULTI OUT CLAP",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 304.5,
   "y": 30.4,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_out_rimshot",
   "label": "MULTI OUT RIM SHOT",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 285.6,
   "y": 30.5,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_out_hitom",
   "label": "MULTI OUT HI TOM",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 267.8,
   "y": 30.6,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_out_midtom",
   "label": "MULTI OUT MID TOM",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 249.0,
   "y": 30.6,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_out_lowtom",
   "label": "MULTI OUT LOW TOM",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 231.1,
   "y": 30.6,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_out_snare",
   "label": "MULTI OUT SNARE",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 212.4,
   "y": 30.6,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_out_bassdrum",
   "label": "MULTI OUT BASS DRUM",
   "kind": "6.3 mm (1/4\") mono jack, black bezel",
   "x": 194.3,
   "y": 30.5,
   "w": 10.7,
   "h": 10.5
  },
  {
   "id": "rear_sync_in",
   "label": "SYNC IN",
   "kind": "5-pin DIN socket (DIN sync in), black, with 2 flange screws",
   "x": 391.9,
   "y": 54.1,
   "w": 21.7,
   "h": 15.0
  },
  {
   "id": "rear_start_stop",
   "label": "EXT CONTROL START/STOP",
   "kind": "6.3 mm jack (foot switch DP-2)",
   "x": 362.1,
   "y": 57.8,
   "w": 11.0,
   "h": 10.7
  },
  {
   "id": "rear_trig_out",
   "label": "EXT CONTROL TRIG OUT (RIM SHOT)",
   "kind": "6.3 mm jack (+14 V, 20 ms trigger)",
   "x": 343.8,
   "y": 58.0,
   "w": 11.0,
   "h": 10.5
  },
  {
   "id": "rear_save_out",
   "label": "TAPE MEMORY/SYNC SAVE/OUT",
   "kind": "6.3 mm jack",
   "x": 317.3,
   "y": 58.0,
   "w": 11.0,
   "h": 10.7
  },
  {
   "id": "rear_load_in",
   "label": "TAPE MEMORY/SYNC LOAD/IN",
   "kind": "6.3 mm jack",
   "x": 299.1,
   "y": 58.1,
   "w": 11.0,
   "h": 10.5
  },
  {
   "id": "rear_midi_out_1",
   "label": "MIDI OUT",
   "kind": "5-pin DIN socket",
   "x": 269.0,
   "y": 54.6,
   "w": 21.7,
   "h": 15.0
  },
  {
   "id": "rear_midi_out_2",
   "label": "MIDI OUT",
   "kind": "5-pin DIN socket (second OUT)",
   "x": 236.9,
   "y": 54.8,
   "w": 21.5,
   "h": 14.8
  },
  {
   "id": "rear_midi_in",
   "label": "MIDI IN",
   "kind": "5-pin DIN socket",
   "x": 204.9,
   "y": 54.8,
   "w": 21.5,
   "h": 15.0
  },
  {
   "id": "rear_vent",
   "label": "(ventilation)",
   "kind": "5 vertical slots, ~1.4 x 18 mm, pitch 5.0 mm",
   "x": 165.4,
   "y": 15.9,
   "w": 21.7,
   "h": 18.3
  },
  {
   "id": "rear_power_switch",
   "label": "POWER  ON / OFF",
   "kind": "black rocker switch in a square bezel",
   "x": 43.3,
   "y": 24.8,
   "w": 22.1,
   "h": 14.5
  },
  {
   "id": "rear_ac_inlet",
   "label": "AC",
   "kind": "IEC C14 inlet (3-pin) with screw flanges (export) / 2-pin on some units",
   "x": 40.5,
   "y": 54.5,
   "w": 52.1,
   "h": 23.5
  },
  {
   "id": "rear_logo_roland",
   "label": "Roland",
   "kind": "large grey Roland wordmark, ~110 x 22 mm",
   "x": 128.8,
   "y": 54.4
  }
 ],
 "legends": [
  "CARTRIDGE",
  "M-64C",
  "MASTER OUT",
  "R(MONO)",
  "L",
  "MULTI OUT",
  "RIDE",
  "CRASH",
  "HI HAT",
  "CLAP",
  "RIM SHOT",
  "HI TOM",
  "MID TOM",
  "LOW TOM",
  "SNARE",
  "BASS DRUM",
  "SYNC  IN",
  "START/STOP",
  "TRIG OUT",
  "(RIM SHOT)",
  "SAVE/OUT",
  "LOAD/IN",
  "OUT",
  "IN",
  "EXT CONTROL",
  "TAPE MEMORY/SYNC",
  "MIDI",
  "POWER",
  "ON",
  "OFF",
  "AC",
  "Roland (wordmark, grey)"
 ]
}
UI_SUGGESTIONS = [
 {
  "id": "ui_display_digits",
  "type": "display_digits",
  "parent": "display",
  "digits": 3,
  "digit_h": 14.2,
  "digit_w": 8.0,
  "pitch": 12.7,
  "segment_w": 1.8,
  "slant_deg": 10,
  "decimal_points": True,
  "note": "3-digit 0.56 inch red 7-segment module (LB-603VK) behind the filter: digits about 14.2 mm tall on a 12.7 mm pitch, centred in the window. From the part type and a faint \"888\" in the Commons bdaniel photo; not measured precisely."
 },
 {
  "id": "ui_small_button_led",
  "type": "note",
  "note": "The 14 middle-row buttons are lit from behind (D618-D631): draw the frosted centre square red/pink when on. The six left-box buttons have no LED."
 },
 {
  "id": "ui_perspective",
  "type": "note",
  "note": "Positions are panel-plane (base) positions. In the master photo raised parts lean outwards from about (255, 140) mm: key tops by ~1 % of their distance from that point, knob tops by ~2 %. A top-down UI should draw knob caps and key tops centred on these positions (or with a small, consistent fake perspective)."
 }
]


def main():
    global K1, H_MM
    master, rect_json, reg_json, out_json = sys.argv[1:5]
    overlay = sys.argv[5] if len(sys.argv) > 5 else None
    R = json.load(open(rect_json)); G = json.load(open(reg_json))
    H_MM = R['depth_mm']
    K1 = G['K1']
    M = Master(master)
    measured = sample_colours(M)
    keyx = lambda k: K1 + PITCH * (k - 1)        # centre of step key k (1..16)
    bound = lambda j: K1 + PITCH * (j - 0.5)      # boundary between keys j and j+1

    # ============================================================== case / structure
    xl = nmed([M.edge_x(y, 8.3, +1, half=1.5) for y in np.arange(20, 260, 10)])
    xr = nmed([M.edge_x(y, 478.0, -1, half=1.5) for y in np.arange(20, 260, 10)])
    add(id='case_outline', type='case', x=W_MM / 2, y=H_MM / 2, w=W_MM, h=H_MM, corner_r=1.0, colour='side_panel',
        note='top view of the whole unit: 486.0 mm (official width) x %.1f mm. The depth is the length of the sloping top '
             'measured square-on; the official 300 mm depth is the footprint (the base and its front lip reach further '
             'forward than the sloping top). Corners rounded r ~1 mm.' % H_MM)
    add(id='side_panel_left', type='region', bbox=[0, 0, r2(xl), H_MM], colour='side_panel', corner_r=1.0,
        note='top edge of the left side cheek (part 2112017600), olive grey; its outer face is not visible from above')
    add(id='side_panel_right', type='region', bbox=[r2(xr), 0, W_MM, H_MM], colour='side_panel', corner_r=1.0,
        note='right side cheek (2112017700)')
    add(id='top_plate', type='region', bbox=[r2(xl), 0, r2(xr), H_MM], colour='panel', corner_r=0.8,
        note='painted steel top panel (2221039300), warm off-white; all print sits on it. Very slight darker shading '
             '(~0.6 mm) along the rear and front bends.')

    # ============================================================== logos
    def logo(id_, kind, approx, label, colour, note):
        bb = M.components_bbox(kind, approx, margin=1.0, min_area=20)
        add(id=id_, type='logo', bbox=bb, label=label, colour=colour, note=note)
    logo('logo_roland', 'orange', [34.6, 14.1, 115.2, 26.8], 'Roland', 'logo_orange',
         'brand mark slot: the 1980s Roland logo ("R" emblem of 3 parallel outline strokes and the wordmark), orange')
    logo('logo_tr909', 'dark', [34.9, 74.9, 142.6, 90.4], 'TR-909', 'print_dark',
         'brand mark slot: the TR-909 logotype, dark slate')
    logo('logo_rhythm_composer', 'dark', [305.9, 83.5, 441.4, 90.5], 'RHYTHM COMPOSER', 'print_dark',
         'brand mark slot: the RHYTHM COMPOSER logotype, dark slate')

    # ============================================================== instrument section
    inst_div = [G['vlines']['inst_div_%d' % k]['design'] for k in range(10)]
    # outer frame lines start at the top of the header bars; inner dividers just below them
    for k, x in enumerate(inst_div):
        ext = M.vline_extent(x, 96.0, 156.0)
        add(id='line_inst_div_%d' % k, type='line', points=[[r2(x), r2(ext[0] - 0.12)], [r2(x), r2(ext[1] + 0.12)]],
            stroke=0.5, colour='print_dark', x=r2(x), y=r2((ext[0] + ext[1]) / 2),
            note={0: 'left edge of the instrument section', 9: 'right edge of the instrument section'}.get(k, 'section divider'))
    # header bars
    names = ['TOTAL ACCENT', 'BASS DRUM', 'SNARE DRUM', 'LOW TOM', 'MID TOM', 'HI TOM', 'RIM SHOT  HAND CLAP', 'HI HAT', 'CYMBAL']
    ids = ['total_accent', 'bass_drum', 'snare_drum', 'low_tom', 'mid_tom', 'hi_tom', 'rimshot_handclap', 'hi_hat', 'cymbal']
    bar_x = [(36.44, 70.19), (71.19, 116.19), (117.31, 162.06), (163.19, 208.06), (209.31, 254.19),
             (255.44, 300.06), (301.44, 346.19), (347.56, 391.94), (393.56, 439.19)]
    hdr = []
    for (a, b), nm, i in zip(bar_x, names, ids):
        bb = M.refine_rect([a, 97.4, b, 102.9])
        hdr.append(bb)
    y0 = r2(np.median([b[1] for b in hdr])); y1 = r2(np.median([b[3] for b in hdr]))
    for bb, nm, i in zip(hdr, names, ids):
        words = nm.split('  ')
        add(id='hdr_' + i, type='label_box', bbox=[bb[0], y0, bb[2], y1], label=nm, colour='header_bar',
            text_colour='orange_print', font='Helvetica Bold, caps', corner_r=0.3)
    # header texts (orange on dark)
    tb = []
    for bb, nm, i in zip(hdr, names, ids):
        t = M.components_bbox('orange_on_dark', [bb[0] + 0.6, y0 + 0.6, bb[2] - 0.6, y1 - 0.6], margin=0.0, min_area=4,
                              drop_border_lines=False)
        tb.append(t)
    # cap line / baseline of the header texts: half-maximum of the red-channel profile (the thin light
    # strokes bleed in the JPEG, so a fixed threshold overstates the height)
    ext = []
    for t in tb:
        ya = y0 + 0.6
        rr = M.rgb[int(ya * P):int((y1 - 0.6) * P), int(t[0] * P):int(t[2] * P), 0]
        prof = np.percentile(rr, 90, axis=1)
        half = 0.5 * (np.median(np.r_[prof[:2], prof[-2:]]) + prof.max())
        idx = np.where(prof > half)[0]
        ext.append((int(ya * P) / P + idx[0] / P, int(ya * P) / P + (idx[-1] + 1) / P))
    ty0 = r2(np.median([e_[0] for e_ in ext])); ty1 = r2(np.median([e_[1] for e_ in ext]))
    for t, nm, i in zip(tb, names, ids):
        if '  ' in nm:   # two words spaced apart: measure separately
            a, b = nm.split('  ')
            t1 = M.components_bbox('orange_on_dark', [t[0], ty0, t[0] + 15.6, ty1], margin=0.2, min_area=4, drop_border_lines=False)
            t2 = M.components_bbox('orange_on_dark', [t[2] - 19.3, ty0, t[2], ty1], margin=0.2, min_area=4, drop_border_lines=False)
            add(id='txt_hdr_%s_1' % i, type='text', bbox=[t1[0], ty0, t1[2], ty1], label=a, cap_h=r2(ty1 - ty0),
                font='Helvetica Bold, caps', colour='orange_print')
            add(id='txt_hdr_%s_2' % i, type='text', bbox=[t2[0], ty0, t2[2], ty1], label=b, cap_h=r2(ty1 - ty0),
                font='Helvetica Bold, caps', colour='orange_print')
        else:
            add(id='txt_hdr_' + i, type='text', bbox=[t[0], ty0, t[2], ty1], label=nm, cap_h=r2(ty1 - ty0),
                font='Helvetica Bold, caps', colour='orange_print')

    # knobs: x from the 0-degree tick, y = row centre from the +-90 degree ticks
    KN = [  # id, label (as printed above), approx x, row, section
        ('knob_accent', '', 53.07, 1, 'total_accent', 'TOTAL ACCENT'),
        ('knob_bd_tune', 'TUNE', 83.23, 0, 'bass_drum', 'BD TUNE'), ('knob_bd_level', 'LEVEL', 104.27, 0, 'bass_drum', 'BD LEVEL'),
        ('knob_bd_attack', 'ATTACK', 83.22, 1, 'bass_drum', 'BD ATTACK'), ('knob_bd_decay', 'DECAY', 104.27, 1, 'bass_drum', 'BD DECAY'),
        ('knob_sd_tune', 'TUNE', 129.31, 0, 'snare_drum', 'SD TUNE'), ('knob_sd_level', 'LEVEL', 150.30, 0, 'snare_drum', 'SD LEVEL'),
        ('knob_sd_tone', 'TONE', 129.30, 1, 'snare_drum', 'SD TONE'), ('knob_sd_snappy', 'SNAPPY', 150.32, 1, 'snare_drum', 'SD SNAPPY'),
        ('knob_lt_tune', 'TUNE', 175.34, 0, 'low_tom', 'LT TUNE'), ('knob_lt_level', 'LEVEL', 196.34, 0, 'low_tom', 'LT LEVEL'),
        ('knob_lt_decay', 'DECAY', 175.36, 1, 'low_tom', 'LT DECAY'),
        ('knob_mt_tune', 'TUNE', 221.31, 0, 'mid_tom', 'MT TUNE'), ('knob_mt_level', 'LEVEL', 242.32, 0, 'mid_tom', 'MT LEVEL'),
        ('knob_mt_decay', 'DECAY', 221.33, 1, 'mid_tom', 'MT DECAY'),
        ('knob_ht_tune', 'TUNE', 267.28, 0, 'hi_tom', 'HT TUNE'), ('knob_ht_level', 'LEVEL', 288.28, 0, 'hi_tom', 'HT LEVEL'),
        ('knob_ht_decay', 'DECAY', 267.26, 1, 'hi_tom', 'HT DECAY'),
        ('knob_rs_level', 'LEVEL', 313.23, 0, 'rimshot_handclap', 'RIM SHOT LEVEL'),
        ('knob_hc_level', 'LEVEL', 334.2, 0, 'rimshot_handclap', 'HAND CLAP LEVEL'),
        ('knob_hh_level', 'LEVEL', 380.25, 0, 'hi_hat', 'HI HAT LEVEL'),
        ('knob_hh_ch_decay', 'CH DECAY', 359.2, 1, 'hi_hat', 'CLOSED HAT DECAY'),
        ('knob_hh_oh_decay', 'OH DECAY', 380.25, 1, 'hi_hat', 'OPEN HAT DECAY'),
        ('knob_crash_level', 'LEVEL', 405.35, 0, 'cymbal', 'CRASH LEVEL'), ('knob_ride_level', 'LEVEL', 426.45, 0, 'cymbal', 'RIDE LEVEL'),
        ('knob_crash_tune', 'CRASH  TUNE', 405.4, 1, 'cymbal', 'CRASH TUNE'), ('knob_ride_tune', 'RIDE  TUNE', 426.47, 1, 'cymbal', 'RIDE TUNE'),
    ]
    rows_y = {}
    for r, yr in ((0, 119.93), (1, 139.83)):
        ys = []
        for kid, lab, x, row, sec, fn in KN:
            if row != r:
                continue
            for side in (-1, 1):
                v = [M.line_y(x + side * rr, yr, half=1.0, hw=0.15) for rr in np.arange(6.7, 7.7, 0.2)]
                v = [t for t in v if t is not None and abs(t - yr) < 0.8]
                if len(v) >= 3:
                    ys.append(float(np.median(v)))
        rows_y[r] = r2(np.median(ys))
    tick_stats = []
    for kid, lab, x, row, sec, fn in KN:
        yr = rows_y[row]
        xs = [M.line_x(y, x, half=1.0, hh=0.12) for y in np.arange(yr - 8.8, yr - 6.9, 0.2)]
        xs = [t for t in xs if t is not None and abs(t - x) < 1.0]
        xk = float(np.median(xs)) if xs else x
        for t in M.tick_components(xk, yr, 5.6, 9.8):
            tick_stats.append(t)
        add(id=kid, type='knob', label=fn, x=r2(xk), y=yr, d=10.8, knob_d=10.8, base_d=11.6, height=15.0,
            knob_style='small', colour='knob', pointer_colour='orange_print', section=sec,
            rotation=dict(min_deg=-150, max_deg=150, zero='top', clockwise_positive=True),
            scale=dict(type='ticks', angles_deg=list(range(-150, 151, 30)), r_inner=5.8, r_outer=7.8,
                       r_outer_long=8.7, long_at_deg=[-150, 0, 150], tick_w=0.42),
            note='knob part 2247030300 on a 16 mm pot; centre = centre of the printed tick ring (x from the 0-degree tick, '
                 'row y from the 9 and 3 o\'clock ticks); agrees with the pot positions on the 1:1 voicing-board '
                 'artwork to 0.09 mm rms in x')
    # tick statistics (for the record)
    regular = [t for t in tick_stats if min(abs(t['ang'] - a) for a in (-150, 150, 0)) > 8]
    long_ = [t for t in tick_stats if min(abs(t['ang'] - a) for a in (-150, 150, 0)) <= 8]
    SMALL_TICKS = dict(n=len(tick_stats), r_outer_regular=r2(np.median([t['r1'] for t in regular])),
                       r_outer_long=r2(np.median([t['r1'] for t in long_])),
                       r_inner_min=r2(np.percentile([t['r0'] for t in tick_stats], 10)))
    # knob legends above knobs (same row for every section)
    lab_rows = {0: (108.4, 110.4), 1: (128.3, 130.3)}
    leg = []
    for kid, lab, x, row, sec, fn in KN:
        if not lab:
            continue
        a, b = lab_rows[row]
        w = 4.3 * len(lab.replace(' ', '')) * 0.5 + 2.5
        if '  ' in lab:     # CRASH  TUNE / RIDE  TUNE: two words, centred on the knob as a pair
            bb = M.components_bbox('dark', [x - 10.5, a - 0.2, x + 10.5, b + 0.2], margin=0.5)
        else:
            bb = M.components_bbox('dark', [x - w / 2, a - 0.2, x + w / 2, b + 0.2], margin=0.6)
        leg.append((kid, lab, row, bb))
    for row in (0, 1):
        r = [l for l in leg if l[2] == row]
        yy0 = r2(np.median([l[3][1] for l in r])); yy1 = r2(np.median([l[3][3] for l in r]))
        for kid, lab, _, bb in r:
            add(id='txt_' + kid[5:], type='text', bbox=[bb[0], yy0, bb[2], yy1], label=lab.replace('  ', ' '),
                cap_h=r2(yy1 - yy0), font='Helvetica Medium, caps', colour='print_dark',
                note='two words with a wide space' if '  ' in lab else None)

    # ============================================================== middle row
    # START / STOP-CONT (ivory keys)
    for kid, lab, ax in (('key_start', 'START', 50.6), ('key_stop_cont', 'STOP/CONT', 75.6)):
        hole = [nmed([M.edge_x(y, ax - 9.4, -1, half=1.2) for y in np.arange(172, 186, 1.5)]),
                nmed([M.edge_y(x, 169.2, -1, half=1.2) for x in np.arange(ax - 6, ax + 6, 1.5)]),
                nmed([M.edge_x(y, ax + 9.4, +1, half=1.2) for y in np.arange(172, 186, 1.5)]),
                nmed([M.edge_y(x, 188.5, +1, half=1.2) for x in np.arange(ax - 6, ax + 6, 1.5)])]
        add(id=kid, type='button', label=lab, bbox=[r2(v) for v in hole], style='ivory', colour='ivory_cap',
            cap_top=[15.0, 15.0], height=8.0,
            note='ivory key top (2247029200) on a KHJ10902 switch; bbox = panel cut-out as seen from above')
    # display window
    disp = M.blob((93, 165, 138, 192), lambda rgb, g: (rgb[..., 0] - rgb[..., 2] > 12) & (g < 95))
    add(id='display', type='display', bbox=[r2(v) for v in disp['bbox']], label='MEAS/TEMPO', colour='display',
        digits=3, segment_colour='led_lit', note='red filter window (2222030700) over a 3-digit 7-segment LED LB-603VK; '
        'digits read tempo (37-290) or measure number')
    # TEMPO & VOLUME (large knobs)
    big = {}
    for kid, lab, ax, ay in (('knob_tempo', 'TEMPO', 160.3, 178.6), ('knob_volume', 'VOLUME', 426.3, 178.7)):
        # centre from the long 9 / 3 o'clock ticks and the 0-degree tick
        yl = [M.line_y(ax - r, ay, half=1.2, hw=0.15) for r in np.arange(13.5, 15.5, 0.25)]
        yr_ = [M.line_y(ax + r, ay, half=1.2, hw=0.15) for r in np.arange(13.5, 15.5, 0.25)]
        ys = [v for v in yl + yr_ if v is not None and abs(v - ay) < 1.0]
        cy = float(np.median(ys))
        xs = [M.line_x(y, ax, half=1.2, hh=0.12) for y in np.arange(cy - 15.5, cy - 13.0, 0.2)]
        xs = [v for v in xs if v is not None and abs(v - ax) < 1.0]
        cx = float(np.median(xs))
        ticks = M.tick_components(cx, cy, 11.5, 16.6, body_r=1.0)
        big[kid] = (cx, cy, ticks)
        add(id=kid, type='knob', label=lab, x=r2(cx), y=r2(cy), d=20.0, knob_d=20.0, base_d=21.0, height=17.0,
            knob_style='large', colour='knob', pointer_colour='orange_print',
            rotation=dict(min_deg=-150, max_deg=150, zero='top', clockwise_positive=True),
            scale=dict(type='ticks', angles_deg=[a for a in range(-150, 151, 6)], r_inner=10.8, r_outer=14.8,
                       r_outer_long=15.9, long_at_deg=list(range(-150, 151, 30)), tick_w=0.4),
            note='knob part 2247029000 (tempo, volume); 51 ticks every 6 degrees, every 5th (multiples of 30 degrees) longer'
                 if lab == 'TEMPO' else 'knob part 2247029000 on the master volume pot; same 51-tick scale as TEMPO')

    # small illuminated buttons (14 in the middle row)
    SB = [('btn_track_play_1', 'TRACK PLAY 1', 193.6), ('btn_track_play_2', 'TRACK PLAY 2', 205.6),
          ('btn_track_play_3', 'TRACK PLAY 3', 217.6), ('btn_track_play_4', 'TRACK PLAY 4', 229.6),
          ('btn_pattern_play_1', 'PATTERN PLAY 1', 247.45), ('btn_pattern_play_2', 'PATTERN PLAY 2', 259.45),
          ('btn_pattern_play_3', 'PATTERN PLAY 3', 271.45), ('btn_ext_inst', 'EXT INST', 283.45),
          ('btn_tempo_step', 'TEMPO / STEP', 301.35), ('btn_back_tap', 'BACK / TAP', 313.35),
          ('btn_fwd_bank1', 'FWD / BANK I', 325.35), ('btn_avail_meas_bank2', 'AVAILABLE MEAS / BANK II', 337.35),
          ('btn_cycle_guide', 'CYCLE/GUIDE / LAST MEAS', 351.25), ('btn_tape_sync', 'TAPE SYNC / TEMPO MODE', 363.25)]
    sbx = []
    for bid, lab, ax in SB:
        b = M.blob((ax - 6, 166, ax + 6, 181), lambda rgb, g: g < 165)
        sbx.append((bid, lab, b))
    sb_y = r2(np.median([b['cy'] for _, _, b in sbx]))
    # snap groups onto the 12.0 mm switch pitch (switch-board artwork)
    groups = [[0, 1, 2, 3], [4, 5, 6, 7], [8, 9, 10, 11], [12, 13]]
    for gidx in groups:
        xs = np.array([sbx[i][2]['cx'] for i in gidx]); k = np.arange(len(gidx))
        x0 = float(np.mean(xs - SMALL_PITCH * k))
        for j, i in enumerate(gidx):
            bid, lab, b = sbx[i]
            add(id=bid, type='button', label=lab, x=r2(x0 + SMALL_PITCH * j), y=sb_y, w=8.6, h=8.6, style='small',
                illuminated=True, colour='small_btn', cap_top=[6.6, 6.6], height=5.0,
                note='clear/grey translucent cap (2247029300) over a tact switch KHJ11901 with a red LED behind it '
                     '(D618-D631); bbox = visible cap incl. its sloped sides')
    # CARTRIDGE / ENTER key (grey key with LED window, same family as the step keys)
    ax, ay = 386.05, 179.1
    hole = [nmed([M.edge_x(y, ax - 9.5, -1, half=1.2) for y in np.arange(172, 186, 1.5)]),
            nmed([M.edge_y(x, 169.6, -1, half=1.2) for x in np.arange(ax - 6, ax + 6, 1.5)]),
            nmed([M.edge_x(y, ax + 9.5, +1, half=1.2) for y in np.arange(172, 186, 1.5)]),
            nmed([M.edge_y(x, 188.6, +1, half=1.2) for x in np.arange(ax - 6, ax + 6, 1.5)])]
    add(id='key_cartridge_enter', type='button', label='CARTRIDGE/ENTER', bbox=[r2(v) for v in hole], style='step',
        colour='key_cap', cap_top=[13.0, 13.0], height=9.0,
        note='grey key top with LED window (KFL10903 switch with LED D617); also TOTAL ACCENT in step write')
    cx = (hole[0] + hole[2]) / 2
    led = M.blob((cx - 5, 172, cx + 5, 177), lambda rgb, g: g < 95)
    add(id='led_cartridge_enter', type='led', shape='rect', x=r2(cx), y=r2(led['cy']), w=5.4, h=2.3,
        colour='led_window', colour_lit='led_lit', note='rectangular LED lens in the key top (seen with perspective lean; '
        'drawn at the key centre line)')

    # orange boxes (label boxes, dark text on orange)
    OB = [('obox_track_write', 'TRACK WRITE', [188.8, 184.6, 234.4, 191.0]),
          ('obox_pattern_write', 'PATTERN WRITE', [242.6, 184.6, 276.4, 191.0]),
          ('obox_ext_inst', 'EXT INST', [278.4, 184.6, 288.4, 191.0]),
          ('obox_pattern_write_mode', 'PATTERN WRITE MODE', [296.5, 184.6, 318.3, 191.0]),
          ('obox_bank', 'BANK', [320.5, 184.7, 342.3, 191.1]),
          ('obox_last_meas', 'LAST MEAS', [346.4, 184.7, 356.4, 191.1]),
          ('obox_tempo_mode', 'TEMPO MODE', [358.4, 184.9, 368.3, 191.1])]
    ob = []
    for oid, lab, box in OB:
        bb = M.refine_rect(box, inside_dark=True, half=0.8)
        ob.append((oid, lab, bb))
    oy0 = r2(np.median([b[1] for _, _, b in ob])); oy1 = r2(np.median([b[3] for _, _, b in ob]))
    for oid, lab, bb in ob:
        two = lab in ('EXT INST', 'PATTERN WRITE MODE', 'LAST MEAS', 'TEMPO MODE')
        lines = {'EXT INST': ['EXT', 'INST'], 'PATTERN WRITE MODE': ['PATTERN WRITE', 'MODE'], 'LAST MEAS': ['LAST', 'MEAS'],
                 'TEMPO MODE': ['TEMPO', 'MODE']}.get(lab, [lab])
        add(id=oid, type='label_box', bbox=[bb[0], oy0, bb[2], oy1], label=lab, colour='orange_box',
            text_colour='print_dark', font='Helvetica Medium, caps', corner_r=0.2, lines=lines)
        # texts inside
        if len(lines) == 1:
            t = M.components_bbox('dark_on_orange', [bb[0] + 0.5, oy0 + 0.5, bb[2] - 0.5, oy1 - 0.5], margin=0, min_area=4,
                                  drop_border_lines=False)
            add(id='txt_' + oid[5:], type='text', bbox=t, label=lab.replace('TRACK WRITE', 'TRACK  WRITE'), cap_h=r2(t[3] - t[1]),
                font='Helvetica Medium, caps', colour='print_dark')
        else:
            mid = (oy0 + oy1) / 2
            t1 = M.components_bbox('dark_on_orange', [bb[0] + 0.4, oy0 + 0.35, bb[2] - 0.4, mid - 0.05], margin=0, min_area=4,
                                   drop_border_lines=False)
            t2 = M.components_bbox('dark_on_orange', [bb[0] + 0.4, mid + 0.05, bb[2] - 0.4, oy1 - 0.35], margin=0, min_area=4,
                                   drop_border_lines=False)
            add(id='txt_' + oid[5:] + '_1', type='text', bbox=t1, label=lines[0], cap_h=r2(t1[3] - t1[1]),
                font='Helvetica Medium, caps', colour='print_dark')
            add(id='txt_' + oid[5:] + '_2', type='text', bbox=t2, label=lines[1], cap_h=r2(t2[3] - t2[1]),
                font='Helvetica Medium, caps', colour='print_dark')

    # middle-row legends (dark on panel)
    MT = [('txt_start', 'START', [45.1, 162.1, 55.1, 164.8]),
          ('txt_stop_cont', 'STOP/CONT', [65.9, 161.9, 84.5, 164.9]),
          ('txt_meas_tempo', 'MEAS/TEMPO', [104.1, 162.0, 126.5, 164.9]),
          ('txt_tempo', 'TEMPO', [154.6, 159.2, 165.8, 161.8]),
          ('txt_slow', 'SLOW', [143.2, 191.5, 151.3, 194.0]),
          ('txt_fast', 'FAST', [169.7, 191.7, 175.9, 193.8]),
          ('txt_track_play', 'TRACK PLAY', [203.4, 162.2, 219.4, 164.4]),
          ('txt_pattern_play', 'PATTERN PLAY', [250.0, 162.4, 268.5, 164.4]),
          ('txt_tempo_btn', 'TEMPO', [296.7, 162.2, 305.9, 164.4]),
          ('txt_back', 'BACK', [309.7, 162.2, 316.8, 164.4]),
          ('txt_fwd', 'FWD', [322.2, 162.4, 328.3, 164.4]),
          ('txt_available', 'AVAILABLE', [329.7, 161.1, 343.5, 163.3]),
          ('txt_meas', 'MEAS', [332.9, 163.7, 340.3, 165.8]),
          ('txt_cycle', 'CYCLE', [347.2, 161.1, 355.6, 163.3]),
          ('txt_guide', '/ GUIDE', [346.0, 163.6, 355.9, 165.9]),
          ('txt_tape', 'TAPE', [360.2, 161.1, 366.4, 163.3]),
          ('txt_sync', 'SYNC', [359.6, 163.7, 366.9, 165.8]),
          ('txt_cartridge', 'CARTRIDGE', [377.0, 160.6, 395.5, 163.4]),
          ('txt_enter', '/ENTER', [380.1, 163.5, 392.5, 166.4]),
          ('txt_total_accent', 'TOTAL  ACCENT', [375.7, 191.1, 396.9, 193.5]),
          ('txt_volume', 'VOLUME', [418.9, 159.5, 432.0, 162.0]),
          ('txt_min', 'MIN', [410.9, 191.9, 416.2, 194.2]),

          ('txt_step', 'STEP', [298.0, 180.5, 304.9, 182.8]),
          ('txt_tap', 'TAP', [310.9, 180.5, 315.9, 182.7]),
          ('txt_bank_1', 'I', [324.6, 180.5, 325.8, 182.8]),
          ('txt_bank_2', 'II', [336.2, 180.5, 338.3, 182.8])]
    CAP = {'txt_stop_cont': 2.5, 'txt_meas_tempo': 2.5, 'txt_enter': 2.5, 'txt_guide': 2.0}   # '/' overshoots the caps
    for tid, lab, box in MT:
        bb = M.components_bbox('dark', box, margin=0.3 if tid == 'txt_slow' else 0.5)
        font = 'Times Roman (serif numerals)' if tid.startswith('txt_bank_') else 'Helvetica Medium, caps'
        add(id=tid, type='text', bbox=bb, label=lab, cap_h=CAP.get(tid, r2(bb[3] - bb[1])), font=font, colour='print_dark',
            note='bbox includes the slash, which overshoots the cap height' if tid in CAP else None)
    add(id='txt_max', type='text', bbox=[434.95, 191.5, 440.5, 193.45], label='MAX', cap_h=1.95, font='Helvetica Medium, caps',
        colour='print_dark', note='read by eye: the left of the "M" is hidden by the leaning VOLUME knob in the master photo')
    # numerals under TRACK PLAY / PATTERN PLAY buttons
    for i, x in enumerate([193.6, 205.6, 217.6, 229.6]):
        bb = M.components_bbox('dark', [x - 1.2, 180.5, x + 1.2, 182.7], margin=0.4)
        add(id='txt_track_play_%d' % (i + 1), type='text', bbox=bb, label=str(i + 1), cap_h=r2(bb[3] - bb[1]),
            font='Helvetica Medium', colour='print_dark')
    for i, x in enumerate([247.45, 259.45, 271.45]):
        bb = M.components_bbox('dark', [x - 1.2, 180.5, x + 1.2, 182.7], margin=0.4)
        add(id='txt_pattern_play_%d' % (i + 1), type='text', bbox=bb, label=str(i + 1), cap_h=r2(bb[3] - bb[1]),
            font='Helvetica Medium', colour='print_dark')
    measure_step(M, G, keyx, bound)
    write_outputs(M, R, G, measured, big, SMALL_TICKS, out_json, overlay)


if __name__ == '__main__':
    main()
