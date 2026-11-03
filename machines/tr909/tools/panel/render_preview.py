#!/usr/bin/env python3
"""Render docs/panel.json with Pillow and compose it with the regularised master photo for checking.

Usage: render_preview.py PANEL_JSON MASTER_PNG OUT_PNG [--ppmm 4] [--overlay PHOTO_PNG OUT_JPG] ...

Panels, top to bottom: (1) the panel drawn from the JSON alone, (2) the photo, (3) the photo with
the JSON drawn over it as thin outlines (texts, keylines, controls) - everything should sit on the
print. The renderer is deliberately simple (flat colours, no lighting); it only has to show that
every element is where it should be and has the right size. Brand marks are drawn as their slots'
boxes: the marks themselves are not in the repo.

--overlay draws the outlines over another picture in the same frame (8 px/mm, e.g. a photo that
crosscheck.py resampled onto the master), at full resolution, into OUT_JPG. The pictures this
writes show the photos: keep them out of the repo."""
import collections, json, math, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HELV = '/System/Library/Fonts/Helvetica.ttc'
HELVN = '/System/Library/Fonts/HelveticaNeue.ttc'
TIMES = '/System/Library/Fonts/Times.ttc'
SS = 3   # supersampling


def hexrgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def font_for(name, px):
    n = (name or '').lower()
    try:
        if 'times' in n:
            return ImageFont.truetype(TIMES, px, index=0)
        if 'bold' in n:
            return ImageFont.truetype(HELV, px, index=1)
        return ImageFont.truetype(HELVN, px, index=10)     # Helvetica Neue Medium
    except OSError:
        return ImageFont.load_default()


class R:
    def __init__(self, d, ppmm):
        self.d = d
        self.s = ppmm * SS
        self.W, self.H = int(round(d['panel']['w'] * self.s)), int(round(d['panel']['h'] * self.s))
        self.im = Image.new('RGB', (self.W, self.H), (255, 255, 255))
        self.dr = ImageDraw.Draw(self.im)
        # a colour the table lacks shows in magenta
        self.col = collections.defaultdict(lambda: (255, 0, 255), {k: hexrgb(v['hex']) for k, v in d['colours'].items()})

    def p(self, x, y):
        return (x * self.s, y * self.s)

    def box(self, b):
        return [b[0] * self.s, b[1] * self.s, b[2] * self.s, b[3] * self.s]

    def rrect(self, b, colour, r=0.0, outline=None, width=0):
        self.dr.rounded_rectangle(self.box(b), radius=r * self.s, fill=colour, outline=outline, width=int(width))

    def text(self, e, colour):
        b = e['bbox']
        cap = e.get('cap_h') or (b[3] - b[1])
        label = e['label']
        f = font_for(e.get('font'), 200)
        # measure cap height of this font at 200 px
        cb = f.getbbox('H')
        cap_px200 = cb[3] - cb[1]
        size = max(4, int(round(200 * cap * self.s / cap_px200)))
        f = font_for(e.get('font'), size)
        tb = f.getbbox(label)
        tw, th = tb[2] - tb[0], tb[3] - tb[1]
        if tw <= 0 or th <= 0:
            return
        tmp = Image.new('L', (tw + 4, th + 4), 0)
        ImageDraw.Draw(tmp).text((2 - tb[0], 2 - tb[1]), label, font=f, fill=255)
        bw, bh = (b[2] - b[0]) * self.s, (b[3] - b[1]) * self.s
        tmp = tmp.resize((max(1, int(round(bw))), max(1, int(round(bh)))), Image.LANCZOS)
        self.im.paste(Image.new('RGB', tmp.size, colour), (int(round(b[0] * self.s)), int(round(b[1] * self.s))), tmp)

    def poly_evenodd(self, paths, colour):
        acc = None
        x0 = min(min(x for x, y in p) for p in paths); y0 = min(min(y for x, y in p) for p in paths)
        x1 = max(max(x for x, y in p) for p in paths); y1 = max(max(y for x, y in p) for p in paths)
        X0, Y0 = int(x0 * self.s) - 2, int(y0 * self.s) - 2
        w, h = int(x1 * self.s) - X0 + 4, int(y1 * self.s) - Y0 + 4
        acc = np.zeros((h, w), np.uint8)
        for p in paths:
            m = Image.new('L', (w, h), 0)
            ImageDraw.Draw(m).polygon([(x * self.s - X0, y * self.s - Y0) for x, y in p], fill=1)
            acc ^= np.asarray(m, np.uint8)
        mask = Image.fromarray(acc * 255)
        self.im.paste(Image.new('RGB', (w, h), colour), (X0, Y0), mask)

    # ---------------------------------------------------------------- element types
    def knob(self, e):
        c = self.col
        x, y = e['x'], e['y']
        sc = e['scale']
        for a in sc['angles_deg']:
            ro = sc['r_outer_long'] if a in sc['long_at_deg'] else sc['r_outer']
            sa, ca = math.sin(math.radians(a)), -math.cos(math.radians(a))
            self.dr.line([self.p(x + sa * sc['r_inner'], y + ca * sc['r_inner']), self.p(x + sa * ro, y + ca * ro)],
                         fill=c['print_dark'], width=max(1, int(round(sc['tick_w'] * self.s))))
        rb = e.get('base_d', e['knob_d']) / 2
        rt = e['knob_d'] / 2
        self.dr.ellipse([*self.p(x - rb, y - rb), *self.p(x + rb, y + rb)], fill=c['knob_skirt_shade'])
        self.dr.ellipse([*self.p(x - rt, y - rt), *self.p(x + rt, y + rt)], fill=c['knob'])
        # orange pointer line at 12 o'clock
        L = 0.32 * rt if e['knob_style'] == 'small' else 0.55 * rt
        self.dr.line([self.p(x, y - rt + 0.5), self.p(x, y - rt + 0.5 + L)], fill=c['orange_print'],
                     width=max(1, int(round(0.55 * self.s))))

    def button(self, e):
        c = self.col
        b = e['bbox']
        st = e.get('style')
        if st == 'small':
            self.rrect(b, c['small_btn'], 0.5)
            m = 1.6
            self.rrect([b[0] + m, b[1] + m, b[2] - m, b[3] - m], c['small_btn_centre'], 0.4)
            return
        self.rrect(b, c['key_hole'], 0.3)
        m = 0.35
        self.rrect([b[0] + m, b[1] + m, b[2] - m, b[3] - m - 0.6], c['ivory_cap' if st == 'ivory' else 'key_cap_side'], 0.6)
        m2 = 2.2
        self.rrect([b[0] + m2, b[1] + m2 + 0.6, b[2] - m2, b[3] - m2], c['ivory_cap' if st == 'ivory' else 'key_cap_top'], 1.2)

    def draw(self):
        c = self.col
        els = self.d['elements']
        order = ['case', 'region', 'fill', 'label_box', 'outline', 'outline_box', 'line', 'display', 'symbol', 'text', 'logo',
                 'button', 'led', 'knob']
        for t in order:
            for e in els:
                if e['type'] != t:
                    continue
                if t == 'case':
                    self.rrect(e['bbox'], c[e['colour']], e.get('corner_r', 0))
                elif t == 'region':
                    self.rrect(e['bbox'], c[e['colour']], e.get('corner_r', 0))
                elif t in ('fill', 'label_box'):
                    self.rrect(e['bbox'], c[e['colour']], e.get('corner_r', 0))
                elif t == 'outline_box':
                    pts = [self.p(x, y) for x, y in e['points']]
                    self.dr.line(pts + [pts[0]], fill=c[e['colour']], width=max(1, int(round(e['stroke'] * self.s))), joint='curve')
                elif t == 'line':
                    pts = [self.p(x, y) for x, y in e['points']]
                    self.dr.line(pts, fill=c[e['colour']], width=max(1, int(round(e['stroke'] * self.s))), joint='curve')
                elif t == 'display':
                    self.rrect(e['bbox'], c['display'], 0.3)
                elif t == 'symbol':
                    b = e['bbox']; col = c[e['colour']]
                    hw, hh = 0.62 * (b[2] - b[0]) if e['label'] == 'eighth_note' else (b[2] - b[0]), 1.15
                    hx0 = b[0]
                    self.dr.ellipse([*self.p(hx0, b[3] - hh), *self.p(hx0 + min(hw, 1.45), b[3])], fill=col)
                    sx = hx0 + min(hw, 1.45) - 0.18
                    self.dr.line([self.p(sx, b[3] - 0.5), self.p(sx, b[1])], fill=col, width=max(1, int(0.3 * self.s)))
                    if e['label'] == 'eighth_note':
                        self.dr.line([self.p(sx, b[1]), self.p(b[2], b[1] + 1.6)], fill=col, width=max(1, int(0.45 * self.s)))
                elif t == 'text':
                    self.text(e, c[e['colour']])
                elif t == 'logo':
                    self.dr.rectangle(self.box(e['bbox']), outline=c[e['colour']], width=max(1, int(0.3 * self.s)))
                elif t == 'button':
                    self.button(e)
                elif t == 'led':
                    b = e['bbox']
                    if e.get('shape') == 'round':
                        self.dr.ellipse(self.box(b), fill=c['led_unlit'])
                    else:
                        self.rrect(b, c['led_window'], 0.2)
                elif t == 'knob':
                    self.knob(e)
        return self.im.resize((self.W // SS, self.H // SS), Image.LANCZOS)


def outline_overlay(d, photo, ppmm):
    im = photo.copy()
    dr = ImageDraw.Draw(im, 'RGBA')
    s = ppmm
    cyan = (0, 230, 255, 255)
    for e in d['elements']:
        t = e['type']
        if t in ('case', 'region'):
            continue
        if 'points' in e:
            pts = [(x * s, y * s) for x, y in e['points']]
            if e.get('closed'):
                pts.append(pts[0])
            dr.line(pts, fill=(255, 40, 200, 255), width=1)
            continue
        b = [v * s for v in e['bbox']]
        if t == 'knob':
            r = e['knob_d'] / 2 * s
            dr.ellipse([e['x'] * s - r, e['y'] * s - r, e['x'] * s + r, e['y'] * s + r], outline=(0, 255, 0, 255))
            sc = e['scale']
            for a in sc.get('angles_deg', []):
                ro = sc['r_outer_long'] if a in sc['long_at_deg'] else sc['r_outer']
                sa, ca = math.sin(math.radians(a)), -math.cos(math.radians(a))
                dr.line([(e['x'] + sa * sc['r_inner']) * s, (e['y'] + ca * sc['r_inner']) * s, (e['x'] + sa * ro) * s,
                         (e['y'] + ca * ro) * s], fill=(0, 255, 0, 200))
        elif t == 'led' and e.get('shape') == 'round':
            dr.ellipse(b, outline=(255, 0, 255, 255))
        else:
            dr.rectangle(b, outline=cyan if t in ('text', 'symbol') else (255, 220, 0, 255))
    return im


def main():
    args = sys.argv[1:]
    overlays = []
    while '--overlay' in args:
        i = args.index('--overlay')
        overlays.append((args[i + 1], args[i + 2]))
        del args[i:i + 3]
    ppmm = 4.0
    if '--ppmm' in args:
        i = args.index('--ppmm')
        ppmm = float(args[i + 1])
        del args[i:i + 2]
    pj, master, out = args[:3]
    d = json.load(open(pj))
    for photo, dest in overlays:
        im = Image.open(photo).convert('RGB')
        outline_overlay(d, im, 8.0).save(dest, quality=88)
        print('overlay ->', dest)
    rend = R(d, ppmm).draw()
    W, H = rend.size
    photo = Image.open(master).convert('RGB').resize((W, H), Image.LANCZOS)
    over = outline_overlay(d, photo, ppmm)
    gap = 16
    sheet = Image.new('RGB', (W, 3 * H + 2 * gap + 3 * 22), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    f = ImageFont.truetype(HELV, 16)
    y = 0
    for img, cap in ((rend, 'panel.json rendered (flat colours)'), (photo, 'master photo, rectified and regularised (Soundgas #282297)'),
                     (over, 'photo with panel.json outlines: yellow = controls/boxes, cyan = texts/notes, green = knobs+ticks, magenta = keylines')):
        dr.text((6, y + 3), cap, fill=(230, 230, 230), font=f)
        y += 22
        sheet.paste(img, (0, y))
        y += H + gap
    sheet.save(out, optimize=True)
    print('preview ->', out, sheet.size)

if __name__ == '__main__':
    main()
