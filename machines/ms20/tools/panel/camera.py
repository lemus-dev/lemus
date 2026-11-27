"""The master photo's camera model (docs/panel.md section 1).

rectify_fit.py gives photo px -> "frame px" on the panel plane (lens distortion k1 about the image
centre, then a homography). With the focal length from the photo's EXIF (28.5 mm on a 4.345 um
pixel pitch, f = 6559 px) and the panel scale s (frame px per mm, scale.py), the homography splits
into the camera's pose relative to the panel. Then any point raised h mm off the panel (a knob
top, a jack's bushing, the side panels' front edges) can be mapped between photo and panel
exactly, which removes its parallax.

Panel coordinates here are mm in the panel plane with the frame's axes (x right, y down); z is
the height towards the camera."""
import numpy as np

import ms20_common as C
import rectify_fit as R


class Camera:
    def __init__(self, fit, s, origin=(0.0, 0.0)):
        self.p = np.array(fit['params'], float)
        self.k = self.p[:2]
        self.H = R.hom(self.p[2:10])
        self.Hi = np.linalg.inv(self.H)
        self.s = float(s)                       # frame px per mm
        self.o = np.array(origin, float)        # frame px of the panel mm origin
        f = C.F_PX
        self.K = np.array([[f, 0, R.CX], [0, f, R.CY], [0, 0, 1.0]])
        # panel mm (x, y, 1) -> frame px -> ideal photo px
        S = np.array([[self.s, 0, self.o[0]], [0, self.s, self.o[1]], [0, 0, 1.0]])
        M = np.linalg.inv(self.K) @ self.Hi @ S
        lam = 2.0 / (np.linalg.norm(M[:, 0]) + np.linalg.norm(M[:, 1]))
        r1, r2, t = lam * M[:, 0], lam * M[:, 1], lam * M[:, 2]
        if t[2] < 0:
            r1, r2, t = -r1, -r2, -t
        self.ortho = dict(n1=float(np.linalg.norm(lam * M[:, 0])), n2=float(np.linalg.norm(lam * M[:, 1])),
                          dot=float(r1 @ r2 / np.linalg.norm(r1) / np.linalg.norm(r2)))
        # orthonormalise
        U, _, Vt = np.linalg.svd(np.c_[r1, r2, np.cross(r1, r2)])
        Rm = U @ Vt
        self.R = Rm
        self.t = t
        # z axis of the panel frame must point towards the camera
        Cpos = -Rm.T @ t
        if Cpos[2] < 0:
            Rm = Rm @ np.diag([1, 1, -1.0])
            self.R = Rm
            Cpos = -Rm.T @ t
        self.C = Cpos                            # camera centre in panel mm (z = distance)

    # photo <-> panel plane (h = 0) through the fitted model
    def photo_to_mm(self, u):
        q = R.model(self.p, np.atleast_2d(u))
        return (q - self.o) / self.s

    def ideal(self, u):
        return R.undistort(np.atleast_2d(u), self.k)

    def mm_to_photo(self, X, h=0.0):
        """Panel point (x, y) raised h mm towards the camera -> photo px (with distortion)."""
        X = np.atleast_2d(np.asarray(X, float))
        P = np.c_[X, np.full(len(X), h) if np.ndim(h) == 0 else np.asarray(h)]
        cam = (self.R @ P.T).T + self.t
        u = (self.K @ cam.T).T
        u = u[:, :2] / u[:, 2:]
        return self.distort(u)

    def distort(self, u):
        """Inverse of R.undistort by fixed-point iteration."""
        u = np.atleast_2d(u)
        d = u.copy()
        for _ in range(20):
            d = d - (R.undistort(d, self.k) - u)
        return d

    def photo_to_mm_h(self, u, h):
        """Photo px of a point raised h mm -> its foot on the panel (x, y) in mm."""
        ui = self.ideal(u)
        rays = (np.linalg.inv(self.K) @ np.c_[ui, np.ones(len(ui))].T).T    # camera frame directions
        d = (self.R.T @ rays.T).T                                             # in panel frame
        Cp = self.C
        hh = np.full(len(ui), h) if np.ndim(h) == 0 else np.asarray(h, float)
        lam = (hh - Cp[2]) / d[:, 2]
        P = Cp + d * lam[:, None]
        return P[:, :2]
