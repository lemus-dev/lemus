#!/usr/bin/env python3
"""Cross-check the TR-909 master photo and its measurements against other photos of the panel and
against the service notes' 1:1 board artwork (docs/panel.md, "Frame" and "Accuracy").

Usage: crosscheck.py MASTER_RECT_PNG MASTER_REG_PNG PANEL_JSON OUT_DIR [--pots SN_P10_PNG] PHOTO:NAME ...

MASTER_RECT_PNG is the master rectified by its outline (rectify.py, 8 px/mm, panel mm), MASTER_REG_PNG
the same after regularise.py, PANEL_JSON what measure_panel.py measured on the latter. --pots gives the
voicing-board foil view (service notes p. 10, 400 dpi) for the pot check.

Registration of a photo onto a master frame:
  1. SIFT key points on the photo and on the master (grey, the master at 0.6 x, about the photos'
     scale), Lowe's ratio test (0.75), a RANSAC homography (0.5 mm).
  2. Dense refinement, three times: the photo resampled into the master frame; on a 3 mm grid of
     8 x 8 mm patches over the print, the shift between master and photo by phase correlation
     (sub-pixel, Hann window). Patches within reach of a knob, key or button are left out (their
     raised tops lean with each camera's parallax), and so are patches without detail in both
     directions (an edge alone fixes no shift along it). The mapping is refitted to the shifted
     points: a homography (RANSAC 0.4, then 0.2 mm, then least squares), or, with the lens term, a
     two-term radial distortion about the photo's centre (k1 r^2 + k2 r^4, r over the half
     diagonal) followed by a homography, fitted by robust least squares.
  The residual of a patch is where the photo has the print minus where the master has it, with
  everything the mapping can explain taken out.

Tests (printed; OUT_DIR/crosscheck_raw.json):
  - the rubber sheet: each photo mapped onto the rectified master, then mappings fitted to the
    master position plus k times regularise.py's shift, k = -0.5 .. 1.5; the k with the smallest
    median residual is the share of the sheet the photo supports (0: the master's bow is in the
    print, 1: it is in the master photo); with the homography alone and with the lens term;
  - the lines (regularise.py's finder) in each photo against a 23.0 mm grid fitted to its own lines,
    the instrument section widths, and the sagitta of the long bars (centre minus ends, tilt out);
  - the pots (--pots): the mounting slots of the 27 small pots found on the foil view, each pot's
    centre the mid-point of its two slots (or one slot and the mean half spacing), and a mirrored
    similarity from the printed tick-ring centres to them, for the master as rectified, the master
    regularised and every photo.
Feature check (OUT_DIR/crosscheck_features.json): every photo mapped onto the regularised master with
the lens term, and every element of PANEL_JSON measured on each, the same way on all of them (tick-
ring centres, ink boxes, bar and box edges, line positions, blobs); the differences from the
master, by element and by group. OUT_DIR/reg_<name>.png: each photo in the regularised frame.
What the first measurement left out is measured too (extras(): the cheeks' edges and seams, the plate's
bends, the lit buttons' windows and LEDs, the display's digits, MAX, the key tops, the LED names).
OUT_DIR/crosscheck.json is the report lemus_panel.py merges: the photos, the frame and why, the
accuracy table, the residuals, element changes ("elements"), new elements ("add") and colours."""
import json, os, sys
import numpy as np
from PIL import Image
import cv2
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regularise as RG
from measure_lib import Master, nmed

P = 8.0
PATCH = 64      # px (8 mm)
STEP = 24       # px (3 mm)
WIN = cv2.createHanningWindow((PATCH, PATCH), cv2.CV_32F)


def gray(rgb):
    return cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY).astype(np.float32)


# ------------------------------------------------------------------------------- registration
def sift_h(photo_g, master_g, scale=0.6):
    sift = cv2.SIFT_create(nfeatures=40000)
    small = cv2.resize(master_g, None, fx=scale, fy=scale, interpolation=cv2.INTER_AREA)
    kq, dq = sift.detectAndCompute(photo_g.astype(np.uint8), None)
    km, dm = sift.detectAndCompute(small.astype(np.uint8), None)
    flann = cv2.FlannBasedMatcher(dict(algorithm=1, trees=5), dict(checks=128))
    pairs = flann.knnMatch(dq, dm, k=2)
    good = [a for a, b in pairs if a.distance < 0.75 * b.distance]
    pq = np.float64([kq[g.queryIdx].pt for g in good])
    pm = np.float64([km[g.trainIdx].pt for g in good]) / scale
    H, inl = cv2.findHomography(pq, pm, cv2.RANSAC, 0.5 * P, maxIters=20000, confidence=0.9999)
    return H, len(good), int(inl.sum())


def raised_mask(panel, shape):
    """1 where the print may be hidden or displaced by a raised part (knobs, keys, buttons)."""
    m = np.zeros(shape, np.uint8)
    for e in panel['elements']:
        if e['type'] == 'knob':
            r = 10.5 if e.get('knob_d', 11) < 15 else 17.5
            cv2.circle(m, (int(e['x'] * P), int(e['y'] * P)), int(r * P), 1, -1)
        elif e['type'] in ('button', 'key'):
            x0, y0, x1, y1 = e['bbox']
            pad = 3.0
            cv2.rectangle(m, (int((x0 - pad) * P), int((y0 - pad) * P)), (int((x1 + pad) * P), int((y1 + pad) * P)), 1, -1)
    return m


def patch_grid(master_g, mask):
    H, W = master_g.shape
    gx = cv2.Sobel(master_g, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(master_g, cv2.CV_32F, 0, 1, ksize=3)
    pts = []
    for y in range(PATCH // 2 + 8, H - PATCH // 2 - 8, STEP):
        for x in range(PATCH // 2 + 8, W - PATCH // 2 - 8, STEP):
            sl = np.s_[y - PATCH // 2:y + PATCH // 2, x - PATCH // 2:x + PATCH // 2]
            if mask[sl].any():
                continue
            a, b, c = (gx[sl] ** 2).mean(), (gy[sl] ** 2).mean(), (gx[sl] * gy[sl]).mean()
            lmin = 0.5 * ((a + b) - np.sqrt((a - b) ** 2 + 4 * c * c))  # weaker direction of the structure tensor
            if lmin < 60.0:
                continue
            pts.append((x, y))
    return np.array(pts, np.float64)


def shifts(ref_g, img_g, pts):
    """Shift of img against ref at each patch centre: the feature at c in ref is at c + s in img."""
    out = []
    for x, y in pts:
        x, y = int(x), int(y)
        a = ref_g[y - PATCH // 2:y + PATCH // 2, x - PATCH // 2:x + PATCH // 2]
        b = img_g[y - PATCH // 2:y + PATCH // 2, x - PATCH // 2:x + PATCH // 2]
        if b.std() < 4:
            out.append((np.nan, np.nan, 0.0))
            continue
        (dx, dy), resp = cv2.phaseCorrelate(a - a.mean(), b - b.mean(), WIN)
        out.append((dx, dy, resp))
    return np.array(out)


class Mapping:
    """Photo px -> master px: optional radial lens term about the photo's centre, then a homography."""

    def __init__(self, H, size, k=(0.0, 0.0)):
        self.H = np.asarray(H, np.float64)
        self.c = np.array([size[0] / 2, size[1] / 2])
        self.R = float(np.hypot(*size) / 2)
        self.k = np.asarray(k, np.float64)

    def undistort(self, q):
        v = (q - self.c) / self.R
        r2 = np.sum(v * v, axis=1, keepdims=True)
        return self.c + (q - self.c) * (1 + self.k[0] * r2 + self.k[1] * r2 * r2)

    def distort(self, u):
        q = u.copy()
        for _ in range(6):     # fixed point: q = c + (u - c) / (1 + k1 r(q)^2 + k2 r(q)^4)
            v = (q - self.c) / self.R
            r2 = np.sum(v * v, axis=1, keepdims=True)
            q = self.c + (u - self.c) / (1 + self.k[0] * r2 + self.k[1] * r2 * r2)
        return q

    def forward(self, q):
        return cv2.perspectiveTransform(self.undistort(q).reshape(-1, 1, 2), self.H).reshape(-1, 2)

    def inverse(self, m):
        u = cv2.perspectiveTransform(m.reshape(-1, 1, 2), np.linalg.inv(self.H)).reshape(-1, 2)
        return self.distort(u)

    def warp(self, img, shape):
        Hh, Ww = shape
        X, Y = np.meshgrid(np.arange(Ww, dtype=np.float64), np.arange(Hh, dtype=np.float64))
        src = self.inverse(np.stack([X.ravel(), Y.ravel()], axis=1)).astype(np.float32)
        mx, my = src[:, 0].reshape(Hh, Ww), src[:, 1].reshape(Hh, Ww)
        return cv2.remap(img, mx, my, cv2.INTER_CUBIC, borderMode=cv2.BORDER_REPLICATE)

    def to_json(self):
        return dict(H=self.H.tolist(), k=self.k.tolist(), centre=self.c.tolist(), R=self.R)


def fit_mapping(q, m, size, lens, thr_px):
    """Mapping from photo points q to master points m (px)."""
    H, inl = cv2.findHomography(q, m, cv2.RANSAC, thr_px, maxIters=20000, confidence=0.9999)
    inl = inl.ravel().astype(bool)
    H, _ = cv2.findHomography(q[inl], m[inl], 0)
    M = Mapping(H, size)
    if lens:
        near = np.hypot(*(cv2.perspectiveTransform(q.reshape(-1, 1, 2), H).reshape(-1, 2) - m).T) < 1.0 * P

        def f(k):
            M.k = k
            u = M.undistort(q[near])
            Hk, _ = cv2.findHomography(u, m[near], 0)
            M.H = Hk
            return (cv2.perspectiveTransform(u.reshape(-1, 1, 2), Hk).reshape(-1, 2) - m[near]).ravel()
        sol = least_squares(f, [0.0, 0.0], loss='soft_l1', f_scale=0.2 * P)
        f(sol.x)
    res = M.forward(q) - m
    return M, inl, res


def register(rgb, ref_g, pts, lens):
    pg = gray(rgb)
    size = rgb.shape[1::-1]
    H, nm, ni = sift_h(pg, ref_g)
    M = Mapping(H, size)
    for it in range(3):
        warped = M.warp(pg, ref_g.shape)
        s = shifts(ref_g, warped, pts)
        ok = (s[:, 2] > 0.25) & np.isfinite(s[:, 0]) & (np.hypot(s[:, 0], s[:, 1]) < 2.5 * P)
        q = M.inverse(pts[ok] + s[ok, :2])
        M, inl, res = fit_mapping(q, pts[ok], size, lens and it == 2, 0.2 * P if it else 0.4 * P)
    good = np.hypot(res[:, 0], res[:, 1]) < 1.0 * P
    e = np.hypot(res[good, 0], res[good, 1]) / P
    st = dict(sift_matches=nm, sift_inliers=ni, patches=int(ok.sum()), within_0_2mm=int((e < 0.2).sum()),
              residual_mm=dict(rms=float(np.sqrt(np.mean(e ** 2))), median=float(np.median(e)), p90=float(np.percentile(e, 90))))
    return M, st, np.where(ok)[0], q, res / P, good


# ------------------------------------------------------------------------------- tests
def sheet_scan(q, base, D, size, lens):
    """Median residual of mappings fitted to base + k D, for k = -0.5 .. 1.5 (q photo px, base px, D mm)."""
    out = []
    for k in np.arange(-0.5, 1.51, 0.25):
        tgt = base + k * D * P
        M, inl, res = fit_mapping(q, tgt, size, lens, 0.3 * P)
        e = np.hypot(res[:, 0], res[:, 1]) / P
        e = e[e < 1.0]
        out.append(dict(k=round(float(k), 2), median=round(float(np.median(e)), 4), rms=round(float(np.sqrt(np.mean(e ** 2))), 4)))
    return out


def line_report(img_rgb):
    """Divider x (mean of each fitted line) and bar sagitta, as regularise.py finds them."""
    V, H = RG.collect(RG.Measurer(img_rgb.astype(np.float32).mean(axis=2)))
    vx, sag = {}, {}
    for name, d in V.items():
        a = np.array(d['pts'])
        if len(a) >= 8:
            c, _, _ = RG.robust_polyfit(a[:, 1], a[:, 0], 1)
            vx[name] = float(np.mean(np.polyval(c, a[:, 1])))
    for name, d in H.items():
        a = np.array(d['pts'])
        if len(a) >= 30:
            c, _, _ = RG.robust_polyfit(a[:, 0], a[:, 1], 3)
            xs = np.linspace(a[:, 0].min(), a[:, 0].max(), 51)
            ys = np.polyval(c, xs)
            s = ys - np.polyval(np.polyfit(xs, ys, 1), xs)
            sag[name] = round(float(s[25] - 0.5 * (s[0] + s[-1])), 3)
    grid = {'inst_div_%d' % k: 2 * (k - 1) for k in range(2, 9)}
    grid.update({'step_div_%d' % k: 2 * (k + 1) for k in range(0, 7)})
    K1 = float(np.mean([vx[l] - (j - 0.5) * RG.PITCH for l, j in grid.items() if l in vx]))
    dev = {l: round(vx[l] - (K1 + (j - 0.5) * RG.PITCH), 3) for l, j in grid.items() if l in vx}
    widths = [round(vx['inst_div_%d' % (k + 1)] - vx['inst_div_%d' % k], 3) if 'inst_div_%d' % k in vx and 'inst_div_%d' % (k + 1) in vx
              else None for k in range(1, 9)]
    return dict(K1=K1, grid_dev=dev, section_widths=widths, sagitta=sag)


def find_pots(p10_png, knobs):
    """The 27 small pots on the voicing-board foil view (mirrored): their centres in page px."""
    Image.MAX_IMAGE_PIXELS = None
    im = np.asarray(Image.open(p10_png).convert('L')).astype(np.float32) / 255
    b = cv2.GaussianBlur(im, (0, 0), 2.5)
    n, lab, st, cen = cv2.connectedComponentsWithStats((b > 0.93).astype(np.uint8), 4)
    slots = []
    for i in range(1, n):
        x, y, w, h, a = st[i]
        if 18 <= w <= 45 and 28 <= h <= 60 and a > 0.75 * w * h and 2700 < y < 4200:
            ring = b[y - 8:y + h + 8, x - 8:x + w + 8].copy()
            ring[8:-8, 8:-8] = np.nan
            if 0.2 < np.nanmean(ring) < 0.8:       # a white slot in a hatched pad
                slots.append(cen[i])
    S = np.array(slots)
    # pairs 150-167 px apart on a row give the half spacing; a first mirrored similarity from them
    half = []
    for s in S:
        o = S[(np.abs(S[:, 1] - s[1]) < 8) & (S[:, 0] - s[0] > 150) & (S[:, 0] - s[0] < 167)]
        if len(o):
            half.append(0.5 * (o[0][0] - s[0]))
    HALF = float(np.median(half))
    # page u = a - s x, v = b + s y (mirrored), first roughly (from BD TUNE and BD LEVEL on the page),
    # then refitted to the pots found, twice, with a tighter search each time
    T = np.array([[-15.72, 0.0, 7155.0], [0.0, 15.72, 1418.0]])
    kx = {e['id']: (e['x'], e['y']) for e in knobs}
    pots = {}
    for tol in (40, 15, 10):
        pots = {}
        for e in knobs:
            u, v = T @ [e['x'], e['y'], 1.0]
            L = [s for s in S if abs(s[0] - (u - HALF)) < tol and abs(s[1] - v) < tol]
            R = [s for s in S if abs(s[0] - (u + HALF)) < tol and abs(s[1] - v) < tol]
            if L and R:
                pots[e['id']] = (0.5 * (L[0] + R[0])).tolist()
            elif L:
                pots[e['id']] = (L[0] + [HALF, 0]).tolist()
            elif R:
                pots[e['id']] = (R[0] - [HALF, 0]).tolist()
        ids = list(pots)
        A = np.array([[kx[i][0], kx[i][1], 1.0] for i in ids])
        B = np.array([pots[i] for i in ids])
        T = np.linalg.lstsq(A, B, rcond=None)[0].T
    return pots, HALF


def ring_centre(M, x, y, large=False):
    """Tick-ring centre: x from the 0-degree tick, y from the 9 and 3 o'clock ticks (each knob alone)."""
    r0, r1 = (13.5, 15.5) if large else (6.7, 7.7)
    xs = [M.line_x(t, x, half=1.0, hh=0.12) for t in np.arange(y - (r1 + 1.1), y - (r0 + 0.2), 0.2)]
    xs = [t for t in xs if t is not None and abs(t - x) < 1.0]
    ys = []
    for side in (-1, 1):
        v = [M.line_y(x + side * rr, y, half=1.0, hw=0.15) for rr in np.arange(r0, r1, 0.2)]
        v = [t for t in v if t is not None and abs(t - y) < 0.8]
        if len(v) >= 3:
            ys.append(float(np.median(v)))
    return (float(np.median(xs)) if xs else None, float(np.mean(ys)) if ys else None, len(ys))


def pot_fit(rings, pots):
    ids = [i for i in pots if i in rings and rings[i][0] is not None and rings[i][1] is not None]
    A = np.array([rings[i][:2] for i in ids]) * [-1, 1]      # the foil view is mirrored
    B = np.array([pots[i] for i in ids])
    a, b = A - A.mean(0), B - B.mean(0)
    za, zb = a[:, 0] + 1j * a[:, 1], b[:, 0] + 1j * b[:, 1]
    z = np.sum(zb * np.conj(za)) / np.sum(np.abs(za) ** 2)
    r = (zb - z * za) / abs(z)
    return dict(n=len(ids), px_per_mm=round(float(abs(z)), 3), rot_deg=round(float(np.degrees(np.angle(z))), 3),
                rms_x=round(float(np.sqrt(np.mean(r.real ** 2))), 3), rms_y=round(float(np.sqrt(np.mean(r.imag ** 2))), 3),
                max=round(float(np.max(np.abs(r))), 3), res={i: [round(float(v.real), 3), round(float(v.imag), 3)] for i, v in zip(ids, r)})


# ------------------------------------------------------------------------------- feature check
def kind_of(colour):
    return {'print_dark': 'dark', 'orange_print': 'orange_on_dark', 'print_white': 'light', 'logo_orange': 'orange'}.get(colour, 'dark')


def light_bbox(M, box, margin=0.4):
    x0, y0, x1, y1 = box
    m = M.kind_mask('light', x0 - margin, y0 - margin, x1 + margin, y1 + margin).astype(np.uint8)
    n, lab, st, cen = cv2.connectedComponentsWithStats(m, 8)
    sel = [st[i] for i in range(1, n) if st[i][4] >= 4 and not (st[i][0] == 0 or st[i][1] == 0 or st[i][0] + st[i][2] >= m.shape[1]
                                                                 or st[i][1] + st[i][3] >= m.shape[0])]
    if not sel:
        return None
    a = np.array(sel)
    X0, Y0 = x0 - margin, y0 - margin
    return [X0 + a[:, 0].min() / P, Y0 + a[:, 1].min() / P, X0 + (a[:, 0] + a[:, 2]).max() / P, Y0 + (a[:, 1] + a[:, 3]).max() / P]


def measure_element(M, e):
    """(x, y, w, h) of an element measured on image M the same way on every photo, or None."""
    t = e['type']
    try:
        if t == 'knob':
            x, y, n = ring_centre(M, e['x'], e['y'], e.get('knob_style') == 'large')
            return None if x is None or y is None else (x, y, None, None)
        if t == 'text':
            k = kind_of(e['colour'])
            bb = light_bbox(M, e['bbox']) if k == 'light' else M.components_bbox(k, e['bbox'], margin=0.4, drop_border_lines=k != 'orange_on_dark')
        elif t == 'logo':
            bb = M.components_bbox('orange' if 'orange' in e['colour'] else 'dark', e['bbox'], margin=1.0, min_area=20)
        elif t in ('label_box', 'fill', 'display'):
            if t == 'label_box' and e['colour'] == 'orange_box':
                bb = M.refine_rect(e['bbox'], inside_dark=True, half=0.8)
            else:
                bb = M.refine_rect(e['bbox'], inside_dark=True, half=0.9)
        elif t == 'line' and len(e['points']) == 2:
            (xa, ya), (xb, yb) = e['points']
            if abs(xa - xb) < 0.01:
                x = nmed([M.line_x(y, xa, half=0.8, hh=0.2, dark=e['colour'] != 'print_white') for y in np.linspace(min(ya, yb) + 1, max(ya, yb) - 1, 9)])
                return None if x is None else (x, 0.5 * (ya + yb), None, None)
            if abs(ya - yb) < 0.01:
                y = nmed([M.line_y(x, ya, half=0.8, hw=0.2, dark=e['colour'] != 'print_white') for x in np.linspace(min(xa, xb) + 1, max(xa, xb) - 1, 9)])
                return None if y is None else (0.5 * (xa + xb), y, None, None)
            return None
        elif t == 'button':
            if e.get('style') == 'small':
                b = M.blob((e['x'] - 6, e['y'] - 6, e['x'] + 6, e['y'] + 6), lambda rgb, g: g < 165)
                return None if b is None else (b['cx'], b['cy'], None, None)
            x0, y0, x1, y1 = e['bbox']
            bb = [nmed([M.edge_x(y, x0, -1, half=1.2) for y in np.linspace(y0 + 3, y1 - 3, 9)]),
                  nmed([M.edge_y(x, y0, -1, half=1.2) for x in np.linspace(x0 + 3, x1 - 3, 9)]),
                  nmed([M.edge_x(y, x1, +1, half=1.2) for y in np.linspace(y0 + 3, y1 - 3, 9)]),
                  nmed([M.edge_y(x, y1, +1, half=1.2) for x in np.linspace(x0 + 3, x1 - 3, 9)])]
            if any(v is None for v in bb):
                return None
        elif t == 'led' and e.get('shape') == 'round':
            b = M.blob((e['x'] - 2.2, e['y'] - 2.2, e['x'] + 2.2, e['y'] + 2.2), lambda rgb, g: (rgb[..., 0] - rgb[..., 2] > 6) & (g < 75))
            return None if b is None else (b['cx'], b['cy'], None, None)
        elif t == 'symbol':
            bb = light_bbox(M, e['bbox'], margin=0.3)
        else:
            return None
    except (ValueError, IndexError, TypeError):
        return None
    if bb is None:
        return None
    return (0.5 * (bb[0] + bb[2]), 0.5 * (bb[1] + bb[3]), bb[2] - bb[0], bb[3] - bb[1])


GROUPS = [('knob tick-ring centres', lambda e: e['type'] == 'knob'),
          ('text ink boxes', lambda e: e['type'] == 'text'),
          ('header bars, orange boxes', lambda e: e['type'] == 'label_box'),
          ('scale and number bars', lambda e: e['type'] == 'fill'),
          ('keylines and dividers', lambda e: e['type'] == 'line'),
          ('note glyphs', lambda e: e['type'] == 'symbol'),
          ('key cut-outs (large keys)', lambda e: e['type'] == 'button' and e.get('style') != 'small'),
          ('small buttons (cap blobs)', lambda e: e['type'] == 'button' and e.get('style') == 'small'),
          ('scale LEDs', lambda e: e['type'] == 'led' and e.get('shape') == 'round'),
          ('display window', lambda e: e['type'] == 'display'),
          ('logo boxes', lambda e: e['type'] == 'logo')]


# ------------------------------------------------------------------------------- what the first measurement left out
def r2(v):
    return round(float(v), 2)


def run_where(mask):
    """First and last index of the longest run of True."""
    best, cur = (0, -1), None
    for i, m in enumerate(mask):
        if m and cur is None:
            cur = i
        if (not m or i == len(mask) - 1) and cur is not None:
            j = i if m else i - 1
            if j - cur > best[1] - best[0]:
                best = (cur, j)
            cur = None
    return best


def hexc(v):
    return '#%02X%02X%02X' % tuple(int(round(c)) for c in v)


def extras(M, photos, panel):
    """Elements the first measurement left out, measured on the regularised master (M) and the
    photos resampled onto it (photos: name -> Master): the side cheeks' edges and the seams to the top
    plate, the plate's rear and front bends, the lit buttons' windows and LEDs, the display's digits,
    the MAX legend, the key tops. Returns (add, overrides, colours, notes)."""
    W_MM, H_MM = panel['panel']['w'], panel['panel']['h']
    add, over, cols, notes = [], {}, {}, {}
    rgb = M.rgb
    # ---- side cheeks: column profiles over y 30-250
    band = rgb[int(30 * P):int(250 * P)].mean(axis=0)            # (W, 3)
    lum = band.mean(axis=1)
    xs = (np.arange(len(lum)) + 0.5) / P

    def span(x0, x1, cond):
        i0, i1 = int(x0 * P), int(x1 * P)
        a, b = run_where(cond(lum[i0:i1]))
        return xs[i0 + a] - 0.5 / P, xs[i0 + b] + 0.5 / P

    for side, sgn in (('left', 1), ('right', -1)):
        if sgn > 0:
            cheek = np.median(lum[int(1.5 * P):int(6.5 * P)])
            plate = np.median(lum[int(12 * P):int(25 * P)])
            gmin = lum[int(7.2 * P):int(8.8 * P)].min()
            g0, g1 = span(7.2, 8.8, lambda v: v < cheek - 0.5 * (cheek - gmin))
            hpk = lum[int(8.2 * P):int(9.6 * P)].max()
            h0, h1 = span(8.2, 9.6, lambda v: v > plate + 0.5 * (hpk - plate))
            omin = lum[int(0.15 * P):int(1.5 * P)].min()
            o0, o1 = span(0.15, 1.5, lambda v: v < cheek - 0.5 * (cheek - omin))
            o0 = 0.0
        else:
            cheek = np.median(lum[int(479.5 * P):int(484.5 * P)])
            plate = np.median(lum[int(461 * P):int(474 * P)])
            gmin = lum[int(477.9 * P):int(479.2 * P)].min()
            g0, g1 = span(477.9, 479.2, lambda v: v < plate - 0.5 * (plate - cheek) - 0.25 * (cheek - gmin))
            hpk = lum[int(476.6 * P):int(478.1 * P)].max()
            h0, h1 = span(476.6, 478.1, lambda v: v > plate + 0.5 * (hpk - plate))
            omin = lum[int(484.6 * P):int(485.9 * P)].min()
            o0, o1 = span(484.6, 485.9, lambda v: v < cheek - 0.5 * (cheek - omin))
            o1 = W_MM
        gcol = band[int(g0 * P):int(g1 * P) + 1].mean(axis=0)
        ocol = band[int(o0 * P) + 2:int(o1 * P) + 1].mean(axis=0) if sgn > 0 else band[int(o0 * P):int(o1 * P) - 2].mean(axis=0)
        hcol = band[int(h0 * P):int(h1 * P) + 1].max(axis=0)
        cols.setdefault('cheek_gap', (gcol, 'dark seam between a side cheek and the top plate, in shade (master %s at its darkest, left)'))
        cols.setdefault('side_panel_edge', (ocol, 'rounded outer edge of the side cheeks, turned away from the light (master %s, left)'))
        cols.setdefault('plate_edge', (hcol, 'rounded edge of the top plate along the cheeks, catching the light (master %s, left)'))
        add.append(dict(id='cheek_%s_edge' % side, type='fill', bbox=[r2(o0), 0.0, r2(o1), r2(H_MM)], colour='side_panel_edge',
                        note='rounded outer edge of the %s cheek, darker than its top face' % side))
        add.append(dict(id='seam_%s' % side, type='fill', bbox=[r2(g0), 0.0, r2(g1), r2(H_MM)], colour='cheek_gap',
                        note='dark seam between the %s cheek and the top plate (darkest at mid depth, lighter towards the rear)' % side))
        add.append(dict(id='plate_edge_%s' % side, type='line', points=[[r2((h0 + h1) / 2), 0.0], [r2((h0 + h1) / 2), r2(H_MM)]],
                        stroke=r2(h1 - h0), colour='plate_edge', note='highlight on the rounded %s edge of the top plate' % side))
    # ---- rear and front bends: row profiles over x 60-420
    rows = rgb[:, int(60 * P):int(420 * P)].mean(axis=1)
    lr = rows.mean(axis=1)
    ys = (np.arange(len(lr)) + 0.5) / P
    plate_r = np.median(lr[int(2 * P):int(5 * P)])
    rmin = lr[int(0.15 * P):int(1.5 * P)].min()
    a, b = run_where(lr[int(0.15 * P):int(1.5 * P)] < plate_r - 0.5 * (plate_r - rmin))
    r0, r1 = 0.0, ys[int(0.15 * P) + b] + 0.5 / P
    cols['rear_bend'] = (rows[int(0.15 * P) + a:int(0.15 * P) + b + 1].mean(axis=0), "the plate's bend down at the rear, seen edge-on (master %s)")
    add.append(dict(id='rear_bend', type='fill', bbox=[8.24, 0.0, 478.03, r2(r1)], colour='rear_bend',
                    note='the top plate turning down at the rear: a faint darker band (lighting; master and Commons 909bd, about 2 mm there)'))
    plate_f = np.median(lr[int(270 * P):int(276 * P)])
    tail = lr[int(276 * P):]
    i_start = int(276 * P) + int(np.argmax(tail < plate_f - 0.05 * (plate_f - tail.min())))
    f0 = ys[i_start] - 0.5 / P
    fmid = (f0 + H_MM) / 2
    c1 = rows[i_start:int(fmid * P)].mean(axis=0)
    c2 = rows[int(fmid * P):int((H_MM - 0.15) * P)].mean(axis=0)
    cols['front_bend'] = (c1, 'the top plate turning down at the front, upper half of the band (master %s)')
    cols['front_bend_dark'] = (c2, 'the same, lower half, turned furthest from the light (master %s)')
    add.append(dict(id='front_bend', type='fill', bbox=[8.24, r2(f0), 478.03, r2(fmid)], colour='front_bend',
                    note='the top plate turning down at the front (lighting: the bands darken towards the edge)'))
    add.append(dict(id='front_bend_edge', type='fill', bbox=[8.24, r2(fmid), 478.03, r2(H_MM)], colour='front_bend_dark',
                    note='the front edge of the bend'))
    # ---- the lit buttons: frosted window and centre, and their LEDs
    leds = {'btn_track_play_1': 'Track1', 'btn_track_play_2': 'Track2', 'btn_track_play_3': 'Track3', 'btn_track_play_4': 'Track4',
            'btn_pattern_play_1': 'Pattern1', 'btn_pattern_play_2': 'Pattern2', 'btn_pattern_play_3': 'Pattern3', 'btn_ext_inst': 'ExtInst',
            'btn_tempo_step': 'Tempo', 'btn_back_tap': 'Back', 'btn_fwd_bank1': 'Fwd', 'btn_avail_meas_bank2': 'AvailableMeas',
            'btn_cycle_guide': 'Cycle', 'btn_tape_sync': 'TapeSync'}
    small = [e for e in panel['elements'] if e['type'] == 'button' and e.get('style') == 'small']
    profs, cen_lit, cen_unlit = [], [], []
    for e in small:
        x, y = e['x'], e['y']
        sub = M.gray[int((y - 6) * P):int((y + 6) * P), int((x - 6) * P):int((x + 6) * P)]
        yy, xx = np.where(sub < 140)
        cx, cy = x - 6 + (xx.mean() + 0.5) / P, y - 6 + (yy.mean() + 0.5) / P      # the cap as seen (it leans)
        h = M.gray[int((cy - 0.6) * P):int((cy + 0.6) * P), int((cx - 6) * P):int((cx + 6) * P)].mean(axis=0)
        v = M.gray[int((cy - 6) * P):int((cy + 6) * P), int((cx - 0.6) * P):int((cx + 0.6) * P)].mean(axis=1)
        n = min(len(h), len(v))
        profs.append((h[:n] + v[:n]) / 2)
        c = np.median(rgb[int((cy - 1) * P):int((cy + 1) * P), int((cx - 1) * P):int((cx + 1) * P)].reshape(-1, 3), axis=0)
        (cen_lit if e['id'] in leds else cen_unlit).append(c)
    pr = np.median(np.array([p[:min(map(len, profs))] for p in profs]), axis=0)
    t = (np.arange(len(pr)) + 0.5) / P - 6
    mid = len(pr) // 2
    # the bright rim of the window, left and right of the centre (inside the cap's dark face, |t| < 3.2 mm)
    lo_, hi_ = int((6 - 3.2) * P), int((6 + 3.2) * P)
    il = lo_ + int(np.argmax(pr[lo_:mid - 6]))
    ir = mid + 6 + int(np.argmax(pr[mid + 6:hi_]))
    lvl_c = np.median(pr[mid - 4:mid + 4])
    def cross(i, step, level):
        while 0 < i < len(pr) - 1 and pr[i] > level:
            i += step
        return t[i]
    hl, hr = 0.5 * (pr[il] + lvl_c), 0.5 * (pr[ir] + lvl_c)
    win = cross(ir, +1, hr) - cross(il, -1, hl)
    ctr = cross(ir, -1, hr) - cross(il, +1, hl)
    win, ctr = r2(win), r2(ctr)
    cols['small_btn_centre_red'] = (np.median(cen_lit, axis=0), 'centre of the 14 lit-row caps with their LED off: tinted by the red LED '
                                    'behind it (master %%s; the six left-box caps, with no LED, %s)' % hexc(np.median(cen_unlit, axis=0)))
    for e in small:
        if e['id'] in leds:
            add.append(dict(id='led_' + e['id'][4:], type='led', shape='backlight', x=e['x'], y=e['y'], w=ctr, h=ctr, window=win,
                            led=leds[e['id']], **{'in': e['id']}, colour='small_btn_centre_red', colour_lit='led_lit',
                            note='red LED (D618-D631) behind the frosted centre of the cap: the centre square glows; window = the '
                                 'frosted square with its bright rim, as seen on the cap'))
    notes['small_buttons'] = 'cap 8.6 mm with its sloped sides (unchanged); frosted window %.1f mm square with a bright rim, its centre %.1f mm' % (win, ctr)
    # ---- the step-key and ENTER tops (key 8, key 9 and ENTER are nearest the camera's axis)
    tops = []
    for kid in ('key_step_8', 'key_step_9', 'key_cartridge_enter'):
        e = [q for q in panel['elements'] if q['id'] == kid][0]
        h = M.gray[int((e['y'] + 2) * P):int((e['y'] + 3) * P), int((e['x'] - 11) * P):int((e['x'] + 11) * P)].mean(axis=0)
        tt = (np.arange(len(h)) + 0.5) / P - 11
        face = np.median(h[int(9 * P):int(13 * P)]) if kid != 'key_cartridge_enter' else np.median(h[int(12 * P):int(16 * P)])
        a, b = run_where(np.abs(h - face) < 0.12 * face)
        tops.append(tt[b] - tt[a] + 1 / P)
    top = r2(np.median(tops))
    for e in panel['elements']:
        if e['type'] == 'button' and e.get('style') == 'step':
            over.setdefault(e['id'], {})['cap_top'] = [top, top]
    notes['key_tops'] = 'grey key tops (steps, ENTER) %.1f mm square from profiles across keys 8, 9 and ENTER (%s)' % (top, ', '.join('%.2f' % v for v in tops))
    # ---- LED names (tr909::Machine::Led)
    for e in panel['elements']:
        if e['type'] == 'led':
            if e['id'].startswith('led_step_'):
                over.setdefault(e['id'], {})['led'] = 'Step%d' % int(e['id'][9:])
            elif e['id'].startswith('led_scale_'):
                # PB4/PA0 is the bottom LED (docs/firmware.md, the LED matrix; core/machine.h Scale1 = D635, bottom)
                over.setdefault(e['id'], {})['led'] = 'Scale%d' % (5 - int(e['id'][10:]))
            elif e['id'] == 'led_cartridge_enter':
                over.setdefault(e['id'], {})['led'] = 'Enter'
    # ---- MAX: the VOLUME knob hides the M in the master; from the photos, through the MIN legend beside it
    mn = [q for q in panel['elements'] if q['id'] == 'txt_min'][0]['bbox']
    mx = []
    for n, Mp in photos.items():
        b = Mp.components_bbox('dark', [434.6, 191.2, 440.9, 193.8], margin=0.5)
        bm = Mp.components_bbox('dark', mn, margin=0.4)
        if b is None or bm is None or not 4.5 < b[2] - b[0] < 6.0 or abs((bm[2] - bm[0]) - (mn[2] - mn[0])) > 0.4:
            continue
        dx = 0.5 * (bm[0] + bm[2]) - 0.5 * (mn[0] + mn[2])
        mx.append((b[0] - dx, b[2] - dx, n))
    if mx:
        x0, x1 = float(np.median([m[0] for m in mx])), float(np.median([m[1] for m in mx]))
        over['txt_max'] = dict(bbox=[r2(x0), mn[1], r2(x1), mn[3]], cap_h=r2(mn[3] - mn[1]), x=r2((x0 + x1) / 2), y=r2((mn[1] + mn[3]) / 2),
                               w=r2(x1 - x0), h=r2(mn[3] - mn[1]),
                               note='x from %s (the VOLUME knob hides the M in the master), offset by MIN beside it; y as MIN' % ', '.join(m[2] for m in mx))
    # ---- the display's digits, from a photo where they show (the bdaniel photo: the unlit segments faintly, a lit 1)
    disp = [q for q in panel['elements'] if q['type'] == 'display'][0]
    for n, Mp in photos.items():
        if n != 'bdaniel':
            continue
        x0, y0, x1, y1 = disp['bbox'][0] + 0.55, disp['bbox'][1] + 0.5, disp['bbox'][2] - 0.5, disp['bbox'][3] - 0.5
        c = Mp.rgb[int(y0 * P):int(y1 * P), int(x0 * P):int(x1 * P), 0]
        hp = cv2.GaussianBlur(c - cv2.GaussianBlur(c, (0, 0), 12), (0, 0), 1.0)
        m = (hp > np.percentile(hp, 99.0)).astype(np.uint8)
        k, lab, st, cen = cv2.connectedComponentsWithStats(m, 8)
        segs = sorted([(x0 + (cen[i][0] + 0.5) / P, y0 + (cen[i][1] + 0.5) / P, st[i][2] / P, st[i][3] / P) for i in range(1, k) if st[i][4] > 15],
                      key=lambda q: q[1])
        row = cv2.GaussianBlur((-hp).mean(axis=1).reshape(1, -1).astype(np.float32), (0, 0), 2).ravel()
        yy = y0 + (np.arange(len(row)) + 0.5) / P
        pk = [yy[i] for i in range(2, len(row) - 2) if row[i] == max(row[i - 2:i + 3]) and row[i] > np.percentile(row, 70)]
        col = (-hp).mean(axis=0)
        col = col - col.mean()
        ac = np.correlate(col, col, 'full')[len(col) - 1:]
        pitch = (np.argmax(ac[int(9 * P):int(16 * P)]) + int(9 * P)) / P
        if len(segs) >= 2 and len(pk) >= 3:
            (bx, by, bw, bh), (cx_, cy_, cw, ch) = segs[0], segs[-1]
            slant = float(np.degrees(np.arctan2(bx - cx_, cy_ - by)))
            hgt = pk[-1] - pk[0]
            seg_w = 0.5 * (bw + cw) * 0.8
            right_x = 0.5 * (bx + cx_)
            over['display'] = dict(digits_geometry=dict(
                digits=3, digit_h=r2(hgt + seg_w), segment_rows_y=[r2(v) for v in (pk[0], pk[len(pk) // 2], pk[-1])],
                pitch_measured=r2(pitch), pitch=12.7, slant_deg=round(slant, 1), segment_w=r2(seg_w),
                digit_w=r2(6.0 + seg_w), centre_y=r2(0.5 * (pk[0] + pk[-1])),
                centres_x=[r2(right_x - 3.0 - 2 * 12.7), r2(right_x - 3.0 - 12.7), r2(right_x - 3.0)],
                note='from the Commons bdaniel photo, where the unlit segments show faintly and the right digit shows a lit 1 '
                     '(segments b and c, centres (%.2f, %.2f) and (%.2f, %.2f)): segment rows a, g, d at the y given, slant from b and c, '
                     'pitch %.2f by autocorrelation (the 0.5 in pitch of a 3-digit 0.56 in module is 12.7 mm, taken), each digit\'s centre '
                     '3.0 mm left of its b-c line at mid height. +-0.5 mm.' % (bx, by, cx_, cy_, pitch)))
    return add, over, cols, notes


def main():
    args = sys.argv[1:]
    pots_png = None
    if '--pots' in args:
        i = args.index('--pots')
        pots_png = args[i + 1]
        del args[i:i + 2]
    rect_png, reg_png, panel_json, out_dir = args[:4]
    photos = [a.rsplit(':', 1) for a in args[4:]]
    os.makedirs(out_dir, exist_ok=True)
    panel = json.load(open(panel_json))
    rect, reg = np.asarray(Image.open(rect_png).convert('RGB')), np.asarray(Image.open(reg_png).convert('RGB'))
    rg, gg = gray(rect), gray(reg)
    shape = rg.shape
    mask = raised_mask(panel, shape)
    pts = patch_grid(rg, mask)
    sheet = shifts(rg, gg, pts)[:, :2] / P
    print('%d print patches; the rubber sheet moves them by %.3f mm rms (max %.3f)' % (
        len(pts), np.sqrt(np.nanmean(np.sum(sheet ** 2, axis=1))), np.nanmax(np.hypot(sheet[:, 0], sheet[:, 1]))))
    raw = dict(patches=(pts / P).round(3).tolist(), sheet=sheet.round(4).tolist(), photos={},
               lines=dict(master_rect=line_report(rect), master_reg=line_report(reg)))
    knobs_small = [e for e in panel['elements'] if e['type'] == 'knob' and e.get('knob_style') == 'small']
    pots = None
    if pots_png:
        pots, half = find_pots(pots_png, knobs_small)
        raw['pots'] = dict(source=pots_png, found=len(pots), half_spacing_px=half, centres_px=pots)
        rings = {}
        for fname, img in (('master_rect', rect_png), ('master_reg', reg_png)):
            Mi = Master(img)
            rings[fname] = {e['id']: ring_centre(Mi, e['x'], e['y']) for e in knobs_small}
            raw['pots'][fname] = pot_fit(rings[fname], pots)
            f = raw['pots'][fname]
            print('pots: %-12s %d pots, %.3f px/mm, rot %.2f deg: rms x %.3f y %.3f mm, max %.3f' % (
                fname, f['n'], f['px_per_mm'], f['rot_deg'], f['rms_x'], f['rms_y'], f['max']))
    for path, name in photos:
        rgb = np.asarray(Image.open(path).convert('RGB'))
        size = rgb.shape[1::-1]
        rec = dict(path=path, size=list(size))
        # onto the rectified master, homography only: the sheet test, lines, pots
        Mr, st, used, q, res, good = register(rgb, rg, pts, lens=False)
        rec['onto_rect'] = dict(mapping=Mr.to_json(), **st)
        D = sheet[used]
        ok = good & np.isfinite(D[:, 0])
        rec['sheet_scan'] = sheet_scan(q[ok], pts[used][ok], D[ok], size, lens=False)
        rec['sheet_scan_lens'] = sheet_scan(q[ok], pts[used][ok], D[ok], size, lens=True)
        warped_rect = Mr.warp(rgb, shape)
        rec['lines'] = line_report(warped_rect)
        best = min(rec['sheet_scan'], key=lambda s: s['median'])
        bestl = min(rec['sheet_scan_lens'], key=lambda s: s['median'])
        print('%-8s onto rect: SIFT %d/%d, %d patches, residual median %.3f mm; sheet: best k %.2f (homography), %.2f (with lens term)' % (
            name, st['sift_matches'], st['sift_inliers'], st['patches'], st['residual_mm']['median'], best['k'], bestl['k']))
        if pots:
            Mi = Master.__new__(Master)
            Mi.rgb = warped_rect.astype(np.float32)
            Mi.gray = Mi.rgb.mean(axis=2)
            Mi.g = cv2.GaussianBlur(Mi.gray, (0, 0), 0.7)
            Mi.H, Mi.W = Mi.gray.shape
            rr = {e['id']: ring_centre(Mi, e['x'], e['y']) for e in knobs_small}
            if sum(1 for v in rr.values() if v[0] is not None and v[1] is not None) >= 8:
                rec['pots'] = pot_fit(rr, pots)
                f = rec['pots']
                print('         pots: %d, rms x %.3f y %.3f mm, max %.3f' % (f['n'], f['rms_x'], f['rms_y'], f['max']))
        # onto the regularised master, with the lens term: the feature check
        Mg, st2, used2, q2, res2, good2 = register(rgb, gg, pts, lens=True)
        rec['onto_reg'] = dict(mapping=Mg.to_json(), **st2)
        warped = Mg.warp(rgb, shape)
        Image.fromarray(warped).save(os.path.join(out_dir, 'reg_%s.png' % name))
        print('         onto reg with the lens term (k1 %+.4f, k2 %+.4f): %d patches, residual median %.3f mm, rms %.3f, 90 %% %.3f' % (
            Mg.k[0], Mg.k[1], st2['patches'], st2['residual_mm']['median'], st2['residual_mm']['rms'], st2['residual_mm']['p90']))
        raw['photos'][name] = rec
    # the line report, side by side
    reps = {'master_rect': raw['lines']['master_rect'], 'master_reg': raw['lines']['master_reg'],
            **{n: raw['photos'][n]['lines'] for _, n in photos}}
    print('dividers minus a 23.0 mm grid fitted to each picture (mm):')
    for n, r in reps.items():
        print('  %-11s' % n, ' '.join('%s %+.2f' % (k.replace('inst_div_', 'i').replace('step_div_', 's'), v) for k, v in r['grid_dev'].items()))
    print('instrument sections (mm):')
    for n, r in reps.items():
        print('  %-11s' % n, ' '.join('%.2f' % w if w else '  -  ' for w in r['section_widths']))
    print('bar sagitta (mm):')
    for l in reps['master_rect']['sagitta']:
        print('  %-14s' % l, ' '.join('%s %+.2f' % (n[:9], r['sagitta'][l]) if l in r['sagitta'] else '%s   -  ' % n[:9] for n, r in reps.items()))
    json.dump(raw, open(os.path.join(out_dir, 'crosscheck_raw.json'), 'w'))

    # ------------------------------------------------------------------ feature check
    imgs = {'master': Master(reg_png)}
    for _, n in photos:
        imgs[n] = Master(os.path.join(out_dir, 'reg_%s.png' % n))
    feats = {}
    for e in panel['elements']:
        if e['type'] in ('case', 'region'):
            continue
        m = {n: measure_element(M, e) for n, M in imgs.items()}
        if m['master'] is None:
            continue
        feats[e['id']] = {n: None if v is None else [round(float(t), 3) if t is not None else None for t in v] for n, v in m.items()}
    # A measurement is compared only where it found the same thing on both pictures: its size within
    # 0.6 mm of the master's (a smaller or larger ink box is a legend half found, or a neighbour taken
    # in) and its centre within 1.5 mm. Then, per photo, the smooth part of the differences (a cubic
    # in x and y fitted to the print's points: the photo's own lens distortion, which the radial term
    # leaves) is taken out too, which leaves how well the elements agree locally.
    def same(m, v):
        if v is None:
            return False
        if m[2] is not None and v[2] is not None and (abs(v[2] - m[2]) > 0.6 or abs(v[3] - m[3]) > 0.6):
            return False
        return np.hypot(v[0] - m[0], v[1] - m[1]) < 1.5
    raised = {e['id'] for e in panel['elements'] if e['type'] in ('button', 'led')}
    smooth = {}
    for _, n in photos:
        ids = [i for i in feats if i not in raised and same(feats[i]['master'], feats[i][n])]
        A = np.array([feats[i]['master'][:2] for i in ids])
        D = np.array([[feats[i][n][0] - feats[i]['master'][0], feats[i][n][1] - feats[i]['master'][1]] for i in ids])
        u, v = (A[:, 0] - 243) / 243, (A[:, 1] - 140) / 140
        B = np.stack([np.ones_like(u), u, v, u * u, u * v, v * v, u ** 3, u * u * v, u * v * v, v ** 3], axis=1)
        cx = np.linalg.lstsq(B, D[:, 0], rcond=None)[0]
        cy = np.linalg.lstsq(B, D[:, 1], rcond=None)[0]
        smooth[n] = (cx, cy)
    def smooth_at(n, x, y):
        u, v = (x - 243) / 243, (y - 140) / 140
        b = np.array([1, u, v, u * u, u * v, v * v, u ** 3, u * u * v, u * v * v, v ** 3])
        return float(b @ smooth[n][0]), float(b @ smooth[n][1])
    groups = {}
    for gname, sel in GROUPS:
        ids = [e['id'] for e in panel['elements'] if sel(e) and e['id'] in feats]
        g = dict(n=len(ids))
        for _, n in photos:
            rows = [(feats[i][n][0] - feats[i]['master'][0], feats[i][n][1] - feats[i]['master'][1], *smooth_at(n, *feats[i]['master'][:2]))
                    for i in ids if same(feats[i]['master'], feats[i][n])]
            if not rows:
                continue
            d = np.array(rows)
            loc = d[:, :2] - d[:, 2:]
            g[n] = dict(n=len(d), rms_x=round(float(np.sqrt(np.mean(d[:, 0] ** 2))), 3), rms_y=round(float(np.sqrt(np.mean(d[:, 1] ** 2))), 3),
                        local_rms_x=round(float(np.sqrt(np.mean(loc[:, 0] ** 2))), 3), local_rms_y=round(float(np.sqrt(np.mean(loc[:, 1] ** 2))), 3),
                        max=round(float(np.hypot(d[:, 0], d[:, 1]).max()), 3))
        groups[gname] = g
    print('feature check, photo minus master in the regularised frame: rms x / y; the same with the smooth part out (mm):')
    for gname, g in groups.items():
        print('  %-28s n %3d ' % (gname, g['n']) + ' | '.join('%s %3d: %.2f/%.2f, local %.2f/%.2f' % (
            n, g[n]['n'], g[n]['rms_x'], g[n]['rms_y'], g[n]['local_rms_x'], g[n]['local_rms_y']) for _, n in photos if n in g))
    json.dump(dict(features=feats, groups=groups), open(os.path.join(out_dir, 'crosscheck_features.json'), 'w'), indent=0)

    # ------------------------------------------------------------------ the report for the converter
    add, over, cols, notes = extras(imgs['master'], {n: imgs[n] for _, n in photos}, panel)
    INFO = {'commons_tr909_front_909bd.jpg': ('serial not known (Commons "RolandTR-909-front.jpg", 2006)', 'CC BY-SA 3.0, User:909 bd'),
            'dm_Roland-TR-909-Rhythm-Composer-Drum-Machine-3.jpg': ('#403957 (Soundgas listing, 2016)', 'reference only, (c) Soundgas'),
            'commons_tr909_large_bdaniel.jpg': ('serial not known (Brandon Daniel, 2009; a perspective-corrected derivative of an oblique shot)',
                                                'CC BY-SA 2.0, Brandon Daniel')}
    plist = []
    for path, n in photos:
        r = raw['photos'][n]
        base = os.path.basename(path)
        rel = path[path.index('private/'):] if 'private/' in path else path
        plist.append(dict(name=n, path=rel, unit=INFO.get(base, ('', ''))[0], licence=INFO.get(base, ('', ''))[1], size=r['size'],
                          sift_matches=r['onto_rect']['sift_matches'], sift_inliers=r['onto_rect']['sift_inliers'],
                          patches=r['onto_reg']['patches'], lens_k=[round(v, 5) for v in r['onto_reg']['mapping']['k']],
                          rms_mm=round(r['onto_reg']['residual_mm']['rms'], 3), median_mm=round(r['onto_reg']['residual_mm']['median'], 3),
                          p90_mm=round(r['onto_reg']['residual_mm']['p90'], 3), rms_px=round(r['onto_reg']['residual_mm']['rms'] * P, 2),
                          sheet_k=min(r['sheet_scan'], key=lambda q: q['median'])['k'],
                          sheet_k_lens=min(r['sheet_scan_lens'], key=lambda q: q['median'])['k'],
                          pots=r.get('pots', {}).get('rms_x')))
    pr = raw.get('pots', {})
    def grp(g, n, k):
        return groups.get(g, {}).get(n, {}).get(k)
    cc = [n for _, n in photos if n != 'bdaniel']
    def rng(g, k):
        v = [grp(g, n, k) for n in cc if grp(g, n, k) is not None]
        if not v:
            return '-'
        return '%.2f' % v[0] if abs(max(v) - min(v)) < 0.005 else '%.2f-%.2f' % (min(v), max(v))
    accuracy = []
    if pr:
        accuracy.append(dict(what='Knob centres (printed tick rings), x', estimate='+-0.1 mm',
                             evidence='%d small pots of the 1:1 voicing-board artwork (SN p. 10): %.3f mm rms, max %.2f (similarity, %.3f px/mm); '
                                      'the master as rectified, without the rubber sheet: %.3f mm' % (
                                          pr['master_reg']['n'], pr['master_reg']['rms_x'], pr['master_reg']['max'], pr['master_reg']['px_per_mm'],
                                          pr['master_rect']['rms_x'])))
        accuracy.append(dict(what='Knob centres, y', estimate='+-0.15 mm',
                             evidence='the same pots: %.3f mm rms (similarity); the page is 1.2 %% taller than wide (an affine fit takes it out), '
                                      'the knob rows are 19.86 mm apart on the panel and 20.11 on the page at its x scale' % pr['master_reg']['rms_y']))
    accuracy += [
        dict(what='Knob centres against the other photos', estimate='0.1 mm', evidence='rms x/y %s / %s mm (locally %s / %s)' % (
            rng('knob tick-ring centres', 'rms_x'), rng('knob tick-ring centres', 'rms_y'), rng('knob tick-ring centres', 'local_rms_x'),
            rng('knob tick-ring centres', 'local_rms_y'))),
        dict(what='Printed lines, bars and boxes, relative', estimate='+-0.15 mm', evidence='dividers and keylines rms x %s, y %s; bars %s / %s; '
             'header bars and orange boxes %s / %s mm, against two other photos' % (
                 rng('keylines and dividers', 'rms_x'), rng('keylines and dividers', 'rms_y'), rng('scale and number bars', 'rms_x'),
                 rng('scale and number bars', 'rms_y'), rng('header bars, orange boxes', 'rms_x'), rng('header bars, orange boxes', 'rms_y'))),
        dict(what='Legends (ink boxes), relative', estimate='+-0.15 mm (sizes +-0.2)', evidence='%s / %s mm rms, %s / %s locally (the legends '
             'both photos found alike)' % (rng('text ink boxes', 'rms_x'), rng('text ink boxes', 'rms_y'), rng('text ink boxes', 'local_rms_x'),
                                         rng('text ink boxes', 'local_rms_y'))),
        dict(what='Absolute, in the 486 mm frame', estimate='+-0.3 mm (+-0.5 within 40 mm of the case ends)',
             evidence='the photos still differ from the master by %s mm (90th percentile) after a homography and a radial lens term, '
                      'most at the panel ends; the outline sets the frame' % '/'.join('%.2f' % q['p90_mm'] for q in plist if q['name'] in cc)),
        dict(what='Raised parts (key cut-outs as seen, small caps, LED domes)', estimate='+-0.5 mm', evidence='key cut-outs %s / %s, small caps '
             '%s / %s, scale LEDs %s / %s mm rms between photos: each camera leans them differently' % (
                 rng('key cut-outs (large keys)', 'rms_x'), rng('key cut-outs (large keys)', 'rms_y'), rng('small buttons (cap blobs)', 'rms_x'),
                 rng('small buttons (cap blobs)', 'rms_y'), rng('scale LEDs', 'rms_x'), rng('scale LEDs', 'rms_y'))),
        dict(what='Logo boxes', estimate='+-0.3 mm', evidence='%s / %s mm rms against the other photos' % (rng('logo boxes', 'rms_x'), rng('logo boxes', 'rms_y'))),
        dict(what='Display digits', estimate='+-0.5 mm', evidence='one photo (bdaniel), faint segments; the part type for the pitch'),
        dict(what='3-D sizes (heights, key tops, knob bodies)', estimate='+-1 mm (key tops +-0.3)', evidence='estimates; key tops from profiles'),
    ]
    frame = dict(
        decision='regularised (regularise.py, as the first measurement): the master rectified by its outline, then the rubber sheet that puts '
                 'the dividers on the 23.0 mm key-pair grid and straightens the long bars',
        why=['the printed tick-ring centres fit the small pots of the 1:1 voicing-board artwork to %s mm rms in x on the regularised master, '
             '%s mm on the master as rectified' % (pr.get('master_reg', {}).get('rms_x'), pr.get('master_rect', {}).get('rms_x')),
             'the master\'s bow is the pattern of a pincushion lens about the picture\'s middle (lines above it bend up at the ends, lines below '
             'it down; sections near the middle narrow, outer ones wide)',
             'the two other straight-on photos (909bd, dm3) show the same pattern with about 60 %% of the amplitude (their bars\' sagitta '
             '-0.5 mm against the master\'s -0.9), so a homography from them to the master cannot see it: alone it favours k = %s; with a radial '
             'lens term of their own, k = %s; their own rings miss the pots by %s mm rms' % (
                 ', '.join(str(q['sheet_k']) for q in plist if q['name'] in cc), ', '.join(str(q['sheet_k_lens']) for q in plist if q['name'] in cc),
                 ', '.join(str(q['pots']) for q in plist if q['name'] in cc)),
             'the bdaniel photo, perspective-corrected from an oblique view (its distortion lies elsewhere), has the sections regular to 0.1 mm'],
        size_mm=[486.0, 279.49])
    out = dict(photos=plist, frame=frame, accuracy=accuracy, residuals=dict(groups=groups, pots=pr and {k: {q: v for q, v in pr[k].items() if q != 'res'}
                                                                                                        for k in ('master_rect', 'master_reg')},
                                                                          lines={k: v for k, v in reps.items()}),
               elements=over, add=add, colours={k: dict(hex=hexc(v[0]), note=v[1] % hexc(v[0])) for k, v in cols.items()}, notes=notes)
    for e in out['add']:
        if 'bbox' in e and 'x' not in e:
            x0, y0, x1, y1 = e['bbox']
            e.update(x=r2((x0 + x1) / 2), y=r2((y0 + y1) / 2), w=r2(x1 - x0), h=r2(y1 - y0))
        elif 'points' in e and 'x' not in e:
            xs_, ys_ = [p_[0] for p_ in e['points']], [p_[1] for p_ in e['points']]
            e.update(x=r2((min(xs_) + max(xs_)) / 2), y=r2((min(ys_) + max(ys_)) / 2))
            e['bbox'] = [r2(min(xs_) - e['stroke'] / 2), r2(min(ys_)), r2(max(xs_) + e['stroke'] / 2), r2(max(ys_))]
        elif 'w' in e and 'bbox' not in e:
            e['bbox'] = [r2(e['x'] - e['w'] / 2), r2(e['y'] - e['h'] / 2), r2(e['x'] + e['w'] / 2), r2(e['y'] + e['h'] / 2)]
        e['nx'] = round(e['x'] / 486.0, 4)
        e['ny'] = round(e['y'] / 279.49, 4)
    json.dump(out, open(os.path.join(out_dir, 'crosscheck.json'), 'w'), indent=1)
    print('crosscheck.json: %d elements to add, %d to change' % (len(add), len(over)))


if __name__ == '__main__':
    main()
