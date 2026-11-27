#!/usr/bin/env python3
"""The keyboard in 3-D from the master photo (docs/panel.md section 9).

The white keys' top surfaces form one plane (the key plane). In the photo:
  * the 21 gaps between the 22 white keys are found on every row of the keys' front part (below
    the black keys) and fitted with straight lines (after removing the lens distortion); their
    vanishing point gives the keys' direction in 3-D (the camera's intrinsics from the EXIF);
  * the key plane holds that direction and the panel's x axis; its distance from the camera is set
    by the white-key pitch: the standard full-size 6.5-inch octave (165/7 = 23.571 mm per white
    key; Korg calls the original's keyboard standard-sized). The photo cannot size the keys by
    itself; the scale check is where that plane meets the panel (panel.md section 9);
  * the white keys' front top edge, the line where they go under the panel's lower lip, and the
    black keys' front base line are found per key and intersected with the plane.
Everything is then given in the panel frame (x, y on the panel face, z towards the camera) and
projected straight onto the panel plane (drop z) for the front view.

Usage: measure_keys.py OUT_JSON"""
import sys, os
import numpy as np
import cv2

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C
import frame
import rectify_fit as RF

PITCH = 165.0 / 7.0
# Hand-picked photo points (px) on the left keyboard cheek, read on a 50 px grid (+-10 px): the
# control wheel's opening (left and right edges at its back and front) and the momentary switch
# (button centre and the rim of its ring). They are mapped onto the key plane (+-2 mm).
CHEEK = {
    'wheel_back_left': (1040, 3030), 'wheel_back_right': (1160, 3030),
    'wheel_front_left': (985, 3420), 'wheel_front_right': (1100, 3420),
    'mom_centre': (950, 3545), 'mom_ring_left': (895, 3545), 'mom_ring_right': (1005, 3545),
    'cheek_front_left': (845, 3775), 'cheek_front_right': (985, 3778),
}
N_WHITE = 22
WHITE_OF = [0, 2, 4, 5, 7, 9, 11]           # semitones of the white keys in an octave


def subpix_min(v, i):
    a, b, c = v[i - 1], v[i], v[i + 1]
    d = a - 2 * b + c
    return i + (0.5 * (a - c) / d if d else 0.0)


def find_gaps(g, rows, x_lo=1150, x_hi=6800):
    """Per row: the 21 white-key gaps (photo px, sub-pixel), as dark minima ~260 px apart."""
    out = {}
    sm = cv2.blur(g, (1, 7))
    for y in rows:
        row = np.convolve(sm[y], np.ones(5) / 5, 'same')
        mins = []
        for x in range(x_lo, x_hi):
            if row[x] < row[x - 1] and row[x] <= row[x + 1]:
                loc = row[x - 60:x + 60]
                if row[x] < 0.75 * np.percentile(loc, 90):
                    mins.append((subpix_min(row, x), row[x]))
        # merge minima closer than 40 px (keep the darkest)
        merged = []
        for m in mins:
            if merged and m[0] - merged[-1][0] < 40:
                if m[1] < merged[-1][1]:
                    merged[-1] = m
            else:
                merged.append(m)
        xs = [m[0] for m in merged]
        if len(xs) == 21:
            out[y] = xs
    return out


def main():
    Fr = frame.Frame()
    cam = Fr.cam
    g = cv2.imread(C.MASTER, cv2.IMREAD_GRAYSCALE).astype(np.float32)
    k = np.array(Fr.fit['params'][:2])
    rows = list(range(3420, 3751, 6))
    gaps = find_gaps(g, rows)
    ys = sorted(gaps)
    print('rows with 21 gaps: %d of %d' % (len(ys), len(rows)))
    G = np.array([gaps[y] for y in ys])          # (rows, 21)
    # undistort and fit a line per gap: x = a + b y
    lines = []
    for i in range(21):
        pts = RF.undistort(np.c_[G[:, i], ys], k)
        b, a = np.polyfit(pts[:, 1], pts[:, 0], 1)
        res = pts[:, 0] - (a + b * pts[:, 1])
        lines.append((a, b, res.std()))
    # vanishing point: least squares intersection of x - b y = a
    A = np.array([[1.0, -b] for a, b, s in lines]); bb = np.array([a for a, b, s in lines])
    vp, *_ = np.linalg.lstsq(A, bb, rcond=None)
    vx = vp[0]; vy = vp[1]
    # in this parametrisation the solution is (x, y) with x = a + b y
    Kinv = np.linalg.inv(cam.K)
    d_cam = Kinv @ np.array([vx, vy, 1.0]); d_cam /= np.linalg.norm(d_cam)
    # the keys run away from the player: towards the panel (panel frame -y)
    Rm, t = cam.R, cam.t
    d_pan = Rm.T @ d_cam
    if d_pan[1] > 0:
        d_pan = -d_pan
    x_pan = np.array([1.0, 0, 0])
    n_pan = np.cross(x_pan, d_pan); n_pan /= np.linalg.norm(n_pan)
    if n_pan[2] < 0:
        n_pan = -n_pan
    # key plane: n . P = rho (panel frame). Rays from the camera centre Cp through photo points.
    Cp = cam.C

    def ray(u):
        ui = RF.undistort(np.atleast_2d(u), k)
        r = (Kinv @ np.c_[ui, np.ones(len(ui))].T).T
        return (Rm.T @ r.T).T

    def hit(u, rho):
        r = ray(u)
        lam = (rho - n_pan @ Cp) / (r @ n_pan)
        return Cp + r * lam[:, None]
    # pitch at the front rows sets rho
    yf = ys[-1]
    uf = np.c_[gaps[yf], np.full(21, yf)]
    rho0 = n_pan @ Cp - 100.0
    for it in range(30):
        P = hit(uf, rho0)
        sp = np.diff(P[:, 0]).mean()
        # scale the distance from the camera to the plane
        dist = n_pan @ Cp - rho0
        rho0 = n_pan @ Cp - dist * PITCH / sp
    P = hit(uf, rho0)
    sp = np.diff(P[:, 0])
    print('vanishing point (%.0f, %.0f); key direction (panel) %s; plane normal %s' % (vx, vy, np.round(d_pan, 4), np.round(n_pan, 4)))
    tilt = np.degrees(np.arccos(abs(n_pan[2])))
    print('angle between key plane and panel plane: %.1f deg' % tilt)
    print('pitch along the front row: mean %.3f, sd %.3f mm (set to %.3f)' % (sp.mean(), sp.std(), PITCH))
    # key boundaries x along the keyboard: gaps at the front row in 3-D
    gx = P[:, 0]
    x0 = gx[0] - PITCH; x1 = gx[-1] + PITCH
    # front top edge of the white keys and their back line (where they go under the lip):
    # per white key, along its centre column, the strongest step bright->dark going down (front)
    # and dark->bright going down (back)
    centres_front = [(gaps[yf][i - 1] if i > 0 else gaps[yf][0] - (gaps[yf][1] - gaps[yf][0])) for i in range(22)]
    front_pts, back_pts, face_pts = [], [], []
    for i in range(N_WHITE):
        if i == 0:
            xa = gaps[yf][0] - (gaps[yf][1] - gaps[yf][0]); xb = gaps[yf][0]
        elif i == N_WHITE - 1:
            xa = gaps[yf][20]; xb = gaps[yf][20] + (gaps[yf][20] - gaps[yf][19])
        else:
            xa, xb = gaps[yf][i - 1], gaps[yf][i]
        xc = int(0.5 * (xa + xb))
        col = cv2.blur(g, (15, 1))[:, xc]
        col = np.convolve(col, np.ones(5) / 5, 'same')
        dcol = np.gradient(col)
        # front top edge: the strongest fall from the bright key top into the shaded front face
        # (rows 3740-3800); face bottom: the fall from the face into the dark rail (3800-3845)
        j = 3740 + int(np.argmin(dcol[3740:3800]))
        front_pts.append((xc, j + 0.5))
        jb = 3800 + int(np.argmin(dcol[3800:3845]))
        face_pts.append((xc, jb + 0.5))
        # back: the first rise from the dark lip into the bright key top, 2820-2960
        j2 = 2820 + int(np.argmax(dcol[2820:2960]))
        back_pts.append((xc, j2))
    Pf = hit(np.array(front_pts, float), rho0)
    Pb = hit(np.array(back_pts, float), rho0)
    # the white keys' front face: the plane through the front edge, normal to the key direction
    face_h = []
    for pf, u in zip(Pf, face_pts):
        r = ray(np.array([u], float))[0]
        lam = ((pf - Cp) @ d_pan) / (r @ d_pan)
        q = Cp + r * lam
        face_h.append(float((pf - q) @ n_pan))
    print('white key front face: %.1f mm high (sd %.1f), as the camera sees it down to the rail' % (np.median(face_h), np.std(face_h)))
    # length of the visible key top along the key direction
    Lw = [(pf - pb) @ (-d_pan) for pf, pb in zip(Pf, Pb)]
    print('white key front edge (panel frame) y %.2f..%.2f z %.2f; back line y %.2f z %.2f; visible length %.1f (sd %.1f)' % (
        Pf[:, 1].min(), Pf[:, 1].max(), Pf[:, 2].mean(), Pb[:, 1].mean(), Pb[:, 2].mean(), np.mean(Lw), np.std(Lw)))
    # black keys: their front base line (glossy black face onto the white key top), per black key
    blacks = []
    octave_whites = [0, 1, 2, 3, 4, 5, 6]
    # black keys sit over the gaps C|D, D|E, F|G, G|A, A|B in each octave: gap indices
    bgap = []
    for o in range(3):
        for gi in (0, 1, 3, 4, 5):
            bgap.append(o * 7 + gi)
    for gi in bgap:
        ys_b = np.arange(3300, 3560)
        # column through the black key: the gap line at these rows
        a, b, s = lines[gi]
        # (undistorted coordinates; close enough to the photo here for picking the column)
        xc = a + b * 3450
        col = cv2.blur(g, (9, 1))[:, int(xc)]
        col = np.convolve(col, np.ones(5) / 5, 'same')
        dcol = np.gradient(col)
        j = 3300 + int(np.argmax(dcol[3300:3560]))     # black face -> white key top (dark->bright)
        # width at the base: the dark run across the row just above j
        row = cv2.blur(g, (1, 5))[j - 8]
        xs = np.arange(int(xc) - 120, int(xc) + 120)
        dark = row[xs] < 0.5 * np.percentile(row[int(xc) - 200:int(xc) + 200], 90)
        idx = np.nonzero(dark)[0]
        if len(idx) == 0:
            continue
        # the run containing the centre
        c0 = 120
        lo = c0
        while lo > 0 and dark[lo - 1]:
            lo -= 1
        hi = c0
        while hi < len(dark) - 1 and dark[hi + 1]:
            hi += 1
        pl = hit(np.array([[xs[lo], j - 8], [xs[hi], j - 8], [xc, j]], float), rho0)
        blacks.append(dict(gap=gi, front=pl[2].tolist(), left=pl[0].tolist(), right=pl[1].tolist(),
                           width=float(pl[1][0] - pl[0][0])))
    bw = np.median([b_['width'] for b_ in blacks])
    bl = [((np.array(b_['front']) - Pb[0]) @ (-d_pan)) for b_ in blacks]
    print('black keys: %d; base width median %.2f mm; front base at y %.2f (z %.2f); length from the back line %.1f' % (
        len(blacks), bw, np.mean([b_['front'][1] for b_ in blacks]), np.mean([b_['front'][2] for b_ in blacks]), np.median(bl)))
    ch = {k_: hit(np.array([v_], float), rho0)[0].tolist() for k_, v_ in CHEEK.items()}
    for k_, v_ in ch.items():
        print('  %-18s %s' % (k_, np.round(v_, 1)))
    out = dict(cheek=ch, pitch=PITCH, key_dir=d_pan.tolist(), plane_normal=n_pan.tolist(), plane_rho=float(rho0),
               tilt_deg=float(tilt), vanishing_point=[float(vx), float(vy)], gap_line_rms_px=float(np.mean([s for a, b, s in lines])),
               gap_x=gx.tolist(), gap_pitch_sd=float(sp.std()), x_left=float(x0), x_right=float(x1),
               white_front=Pf.tolist(), white_back=Pb.tolist(), white_len=float(np.mean(Lw)), white_face_h=float(np.median(face_h)),
               blacks=blacks, black_width=float(bw), black_len=float(np.median(bl)))
    C.save_json(out, sys.argv[1])


if __name__ == '__main__':
    main()
