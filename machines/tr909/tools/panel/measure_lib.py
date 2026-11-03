"""Measurement helpers on the regularised master image (8 px/mm, panel mm coordinates).

All functions take and return millimetres in the panel frame (origin = rear-left corner of
the top view, x right, y towards the player)."""
import math
import numpy as np
from PIL import Image
import cv2

P = 8.0


class Master:
    def __init__(self, path):
        self.rgb = np.asarray(Image.open(path).convert('RGB')).astype(np.float32)
        self.gray = self.rgb.mean(axis=2)
        self.g = cv2.GaussianBlur(self.gray, (0, 0), 0.7)
        self.H, self.W = self.gray.shape

    # ---------------------------------------------------------------- basics
    def crop(self, x0, y0, x1, y1, img=None):
        img = self.g if img is None else img
        return img[int(round(y0 * P)):int(round(y1 * P)), int(round(x0 * P)):int(round(x1 * P))]

    def median_rgb(self, x0, y0, x1, y1, mask=None):
        c = self.rgb[int(y0 * P):int(y1 * P), int(x0 * P):int(x1 * P)].reshape(-1, 3)
        if mask is not None:
            c = c[mask.reshape(-1)]
        return np.median(c, axis=0)

    # ---------------------------------------------------------------- masks
    def kind_mask(self, kind, x0, y0, x1, y1):
        X0, Y0, X1, Y1 = int(x0 * P), int(y0 * P), int(x1 * P), int(y1 * P)
        g = self.g[Y0:Y1, X0:X1]
        rgb = self.rgb[Y0:Y1, X0:X1]
        r, gg, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
        orange = (r - b > 55) & (r > 120)
        if kind == 'dark':          # dark print on the light panel: 50 % level between paper and ink
            bg = np.percentile(g, 90); ink = min(np.percentile(g, 2), 90)
            return (g < 0.5 * (bg + ink)) & ~orange
        if kind == 'light':         # white print on a dark bar
            bg = np.percentile(g, 10); ink = max(np.percentile(g, 98), 170)
            return g > 0.5 * (bg + ink)
        if kind == 'orange':        # orange print (on dark bars or on the panel)
            return orange
        if kind == 'dark_on_orange':      # dark text on an orange box: clearly darker than the box itself
            return g < np.median(g) - 32
        if kind == 'orange_on_dark':      # orange text on a dark bar: red clearly above the bar's red
            lo, hi = np.percentile(r, 20), np.percentile(r, 99.5)
            return (r > 0.5 * (lo + hi)) & (r - b > 25)
        if kind == 'darkred':       # display window
            return (r < 110) & (r - b > 10) & (g < 90)
        raise ValueError(kind)

    def components_bbox(self, kind, box, margin=0.8, min_area=6, drop_border_lines=True, keep=None):
        """Union bbox (mm) of the ink components inside box (expanded by margin).  Components
        that touch the search border and are long (lines, neighbouring print) are dropped."""
        x0, y0, x1, y1 = box
        X0, Y0, X1, Y1 = x0 - margin, y0 - margin, x1 + margin, y1 + margin
        m = self.kind_mask(kind, X0, Y0, X1, Y1).astype(np.uint8)
        n, lab, st, cen = cv2.connectedComponentsWithStats(m, 8)
        H, W = m.shape
        sel = []
        for i in range(1, n):
            x, y, w, h, a = st[i]
            if a < min_area:
                continue
            touches = x == 0 or y == 0 or x + w >= W or y + h >= H
            if touches and drop_border_lines:
                continue
            if keep is not None and not keep(x / P + X0, y / P + Y0, w / P, h / P):
                continue
            sel.append((x, y, x + w, y + h))
        if not sel:
            return None
        a = np.array(sel)
        return [round(X0 + a[:, 0].min() / P, 2), round(Y0 + a[:, 1].min() / P, 2),
                round(X0 + a[:, 2].max() / P, 2), round(Y0 + a[:, 3].max() / P, 2)]

    # ---------------------------------------------------------------- edges / lines
    @staticmethod
    def subpix(v, i):
        if 0 < i < len(v) - 1:
            a, b, c = v[i - 1], v[i], v[i + 1]
            d = a - 2 * b + c
            if d:
                return i + 0.5 * (a - c) / d
        return float(i)

    def edge_y(self, x, y_est, sign, half=1.0, hw=0.3):
        """sub-pixel y of the strongest horizontal edge; sign -1 = light->dark going down."""
        a, b = int((y_est - half) * P), int((y_est + half) * P)
        col = self.g[a:b, int((x - hw) * P):int((x + hw) * P) + 1].mean(axis=1)
        d = np.diff(col) * sign
        i = int(np.argmax(d))
        if d[i] < 12:
            return None
        return (a + self.subpix(d, i) + 0.5) / P

    def edge_x(self, y, x_est, sign, half=1.0, hh=0.3):
        a, b = int((x_est - half) * P), int((x_est + half) * P)
        row = self.g[int((y - hh) * P):int((y + hh) * P) + 1, a:b].mean(axis=0)
        d = np.diff(row) * sign
        i = int(np.argmax(d))
        if d[i] < 12:
            return None
        return (a + self.subpix(d, i) + 0.5) / P

    def line_x(self, y, x_est, half=1.0, hh=0.3, dark=True):
        a, b = int((x_est - half) * P), int((x_est + half) * P)
        row = self.g[int((y - hh) * P):int((y + hh) * P) + 1, a:b].mean(axis=0)
        v = -row if dark else row
        i = int(np.argmax(v))
        if i <= 1 or i >= len(v) - 2 or (v[i] - np.median(v)) < 25:
            return None
        return (a + self.subpix(v, i) + 0.5) / P

    def line_y(self, x, y_est, half=1.0, hw=0.3, dark=True):
        a, b = int((y_est - half) * P), int((y_est + half) * P)
        col = self.g[a:b, int((x - hw) * P):int((x + hw) * P) + 1].mean(axis=1)
        v = -col if dark else col
        i = int(np.argmax(v))
        if i <= 1 or i >= len(v) - 2 or (v[i] - np.median(v)) < 25:
            return None
        return (a + self.subpix(v, i) + 0.5) / P

    def vline_extent(self, x, y_from, y_to, step=0.25, dark=True):
        """y range over which a thin vertical keyline at x is present."""
        ys = [y for y in np.arange(y_from, y_to, step) if self.line_x(y, x, half=0.7, hh=0.12, dark=dark) is not None]
        return (min(ys), max(ys)) if ys else None

    def hline_extent(self, y, x_from, x_to, step=0.25, dark=True):
        xs = [x for x in np.arange(x_from, x_to, step) if self.line_y(x, y, half=0.7, hw=0.12, dark=dark) is not None]
        return (min(xs), max(xs)) if xs else None

    def refine_rect(self, box, inside_dark=True, n=9, half=0.9):
        """Refine the four edges of a filled rectangle (dark fill on light ground by default)."""
        x0, y0, x1, y1 = box
        s = 1 if inside_dark else -1
        xs = np.linspace(x0 + 0.25 * (x1 - x0), x1 - 0.25 * (x1 - x0), n)
        ys = np.linspace(y0 + 0.25 * (y1 - y0), y1 - 0.25 * (y1 - y0), n)
        top = [self.edge_y(x, y0, -s, half) for x in xs]
        bot = [self.edge_y(x, y1, +s, half) for x in xs]
        lef = [self.edge_x(y, x0, -s, half) for y in ys]
        rig = [self.edge_x(y, x1, +s, half) for y in ys]
        med = lambda v, d: round(float(np.median([t for t in v if t is not None])), 2) if any(t is not None for t in v) else d
        return [med(lef, x0), med(top, y0), med(rig, x1), med(bot, y1)]

    def blob(self, box, cond):
        """centroid & bbox of pixels satisfying cond(rgb, gray) in box."""
        x0, y0, x1, y1 = box
        X0, Y0 = int(x0 * P), int(y0 * P)
        rgb = self.rgb[Y0:int(y1 * P), X0:int(x1 * P)]
        m = cond(rgb, rgb.mean(axis=2)).astype(np.uint8)
        n, lab, st, cen = cv2.connectedComponentsWithStats(m, 8)
        if n < 2:
            return None
        i = 1 + int(np.argmax(st[1:, 4]))
        ys, xs = np.where(lab == i)
        return dict(cx=(X0 + xs.mean() + 0.5) / P, cy=(Y0 + ys.mean() + 0.5) / P,
                    bbox=[(X0 + xs.min()) / P, (Y0 + ys.min()) / P, (X0 + xs.max() + 1) / P, (Y0 + ys.max() + 1) / P],
                    area=len(xs) / P / P)

    # ---------------------------------------------------------------- knob tick rings
    def tick_components(self, cx, cy, r_in, r_out, body_r=1.0, thr_off=55):
        R = int(r_out * P) + 4
        X0, Y0 = int(cx * P) - R, int(cy * P) - R
        sub = self.g[Y0:Y0 + 2 * R, X0:X0 + 2 * R]
        bgv = np.percentile(sub, 85)
        dark = (sub < bgv - thr_off).astype(np.uint8)
        k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (int(2 * body_r * P) + 1,) * 2)
        body = cv2.dilate(cv2.morphologyEx(dark, cv2.MORPH_OPEN, k), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)))
        ticks = dark & (1 - body)
        yy, xx = np.mgrid[0:2 * R, 0:2 * R]
        rr = np.hypot((xx + X0 + 0.5) / P - cx, (yy + Y0 + 0.5) / P - cy)
        t = (ticks * ((rr > r_in) & (rr < r_out))).astype(np.uint8)
        n, lab, st, cen = cv2.connectedComponentsWithStats(t, 8)
        out = []
        for i in range(1, n):
            ys, xs = np.where(lab == i)
            if len(xs) < 5:
                continue
            px, py = (xs + X0 + 0.5) / P, (ys + Y0 + 0.5) / P
            r = np.hypot(px - cx, py - cy)
            ang = math.degrees(math.atan2(px.mean() - cx, -(py.mean() - cy)))
            # radial straightness
            mx, my = px.mean(), py.mean()
            cov = np.cov(np.vstack([px - mx, py - my]))
            w, v = np.linalg.eigh(cov)
            if w[0] > 0.2 * w[1]:
                continue
            d = v[:, 1]
            rv = np.array([mx - cx, my - cy]); rn = np.linalg.norm(rv)
            if rn == 0 or abs(float(np.dot(rv / rn, d))) < 0.93:
                continue
            out.append(dict(ang=ang, r0=float(r.min()), r1=float(r.max()), n=len(xs)))
        return out


def nmed(v, default=None):
    """median of the non-None values"""
    v = [t for t in v if t is not None]
    return float(np.median(v)) if v else default
