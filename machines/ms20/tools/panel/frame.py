"""Photo px -> panel mm in the final frame (docs/panel.md section 2), with parallax removal.

Loads rectify_fit.json and scale.json from the private tools folder."""
import os
import numpy as np

import ms20_common as C
import camera


class Frame:
    def __init__(self, out=C.OUT):
        self.fit = C.load_json(os.path.join(out, 'rectify_fit.json'))
        sc = C.load_json(os.path.join(out, 'scale.json'))
        self.s = sc['s']
        self.origin = np.array(sc['origin_frame_px'])
        self.cam = camera.Camera(self.fit, self.s, origin=self.origin)

    def mm(self, u, h=0.0):
        """Photo px -> frame mm of the point's foot on the panel face; h = its height (mm) above
        the face towards the camera (0 for print)."""
        u = np.atleast_2d(np.asarray(u, float))
        p = self.cam.photo_to_mm(u)
        if np.any(np.asarray(h) != 0):
            p = p + (self.cam.photo_to_mm_h(u, h) - self.cam.photo_to_mm_h(u, 0.0))
        return p

    def photo(self, X, h=0.0):
        """Frame mm (raised h) -> photo px, consistent with mm() for h = 0."""
        X = np.atleast_2d(np.asarray(X, float))
        q = X * self.s + self.origin                 # frame px
        import rectify_fit as R
        ui = R.apply_h(np.linalg.inv(self.cam.H), q)   # ideal photo px
        u = self.cam.distort(ui)
        if np.any(np.asarray(h) != 0):
            d = self.cam.mm_to_photo(X, h) - self.cam.mm_to_photo(X, 0.0)
            u = u + d
        return u
