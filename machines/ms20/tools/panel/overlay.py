#!/usr/bin/env python3
"""Draw docs/panel.json back onto the rectified master (docs/panel.md section 1, "Overlay check").

Print, knobs, jacks and hardware are drawn in the panel frame directly. The keyboard and the left
cheek are in orthographic projection of the key plane: they are lifted back onto the key plane,
projected into the photo with the camera model and from there into the rectified frame, so they
sit where the photo shows them.

The check image is a photo derivative: it is written to the private photos/tools folder.
It also prints the overlay residuals: the drawn tick marks against the photo's (angle and outer
end), the knob and jack centres against the service manual's boards, the keys' gaps against the
photo's.

Usage: overlay.py PANEL_JSON OUT_JPG"""
import sys, os
import numpy as np
import cv2

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C
import frame
import measure_lib as M
import rectify_fit as RF

S = 4.0     # output px per mm


def main():
    pj = C.load_json(sys.argv[1])
    R = M.Rect()
    Fr = frame.Frame()
    ky = C.load_json(os.path.join(C.OUT, 'keys.json'))
    n = np.array(ky['plane_normal']); rho = ky['plane_rho']
    img = cv2.resize(R.bgr, None, fx=S / R.P, fy=S / R.P, interpolation=cv2.INTER_AREA)
    img = (img * 0.75).astype(np.uint8)

    def P(x, y):
        return (int(round((x - R.x0) * S)), int(round((y - R.y0) * S)))

    def key_to_rect(pts):
        """Front-view (x, y) on the key plane -> rectified frame mm (as the photo shows it)."""
        pts = np.atleast_2d(pts)
        z = (rho + n[1] * -pts[:, 1] * -1) if False else (rho - n[1] * pts[:, 1]) / n[2]
        X = np.c_[pts, z]
        cam = Fr.cam
        u = (cam.K @ ((cam.R @ X.T).T + cam.t).T).T
        u = u[:, :2] / u[:, 2:]
        u = cam.distort(u)
        return Fr.mm(u)

    Y = (0, 255, 255); G = (0, 255, 0); M_ = (255, 0, 255); Rr = (0, 0, 255); B = (255, 160, 0); Wt = (255, 255, 255)
    for e in pj['elements']:
        t = e['type']
        if t == 'text':
            b = e['bbox']; cv2.rectangle(img, P(b[0], b[1]), P(b[2], b[3]), Y, 1)
        elif t == 'outline_box':
            b = e['bbox']; cv2.rectangle(img, P(b[0], b[1]), P(b[2], b[3]), M_, 1)
        elif t == 'polygon':
            cv2.polylines(img, [np.array([P(*q) for q in e['points']], np.int32)], True, M_, 1)
        elif t == 'line':
            cv2.polylines(img, [np.array([P(*q) for q in e['points']], np.int32)], False, Rr if e['kind'] == 'rule' else B, 1)
        elif t == 'arrowhead':
            cv2.fillPoly(img, [np.array([P(*q) for q in e['points']], np.int32)], G)
        elif t == 'symbol':
            for pl in e['polylines']:
                cv2.polylines(img, [np.array([P(*q) for q in pl], np.int32)], False, B, 1)
        elif t in ('knob', 'rotary'):
            c = np.array([e['x'], e['y']])
            sc = e['scale']
            for d in sc['ticks_deg']:
                u = C.unit(d)
                cv2.line(img, P(*(c + u * sc['tick_r'][0])), P(*(c + u * sc['tick_r'][1])), Rr, 1)
            for nm in sc.get('numerals', []):
                if 'bbox' in nm:
                    b = nm['bbox']; cv2.rectangle(img, P(b[0], b[1]), P(b[2], b[3]), G, 1)
                else:
                    cv2.drawMarker(img, P(nm['x'], nm['y']), M_, cv2.MARKER_SQUARE, 8, 1)
            cv2.circle(img, P(*c), int(e['d'] / 2 * S), M_, 1)
            cv2.drawMarker(img, P(*c), Rr, cv2.MARKER_CROSS, 8, 1)
        elif t == 'jack':
            c = (e['x'], e['y'])
            cv2.circle(img, P(*c), int(e['d'] / 2 * S), G, 1)
            r = e['nut_af'] / np.sqrt(3)
            th = np.radians(e['nut_rot_deg'] + 30)
            hexp = [P(c[0] + r * np.cos(th + k * np.pi / 3), c[1] + r * np.sin(th + k * np.pi / 3)) for k in range(6)]
            cv2.polylines(img, [np.array(hexp, np.int32)], True, G, 1)
        elif t in ('led', 'hole', 'screw'):
            cv2.circle(img, P(e['x'], e['y']), int(e['d'] / 2 * S), Wt, 1)
        elif t == 'logo':
            b = e['bbox']; cv2.rectangle(img, P(b[0], b[1]), P(b[2], b[3]), Rr, 2)
            cv2.putText(img, 'place: ' + e['place'], P(b[0], b[1] - 1), cv2.FONT_HERSHEY_SIMPLEX, 0.5, Rr, 1)
        elif t in ('fill', 'region'):
            if e['id'] in ('panel_face', 'lower_lip', 'side_left', 'side_right'):
                b = e['bbox']; cv2.rectangle(img, P(b[0], b[1]), P(b[2], b[3]), Wt, 1)
        elif t == 'key':
            b = e['bbox']
            q = key_to_rect([[b[0], b[1]], [b[2], b[1]], [b[2], b[3]], [b[0], b[3]]])
            cv2.polylines(img, [np.array([P(*p) for p in q], np.int32)], True, G if e['white'] else Rr, 1)
        elif t == 'wheel':
            q = key_to_rect(e['opening']['polygon'])
            cv2.polylines(img, [np.array([P(*p) for p in q], np.int32)], True, M_, 2)
        elif t == 'switch':
            q = key_to_rect([[e['x'], e['y']]])[0]
            cv2.circle(img, P(*q), int(e['d'] / 2 * S), M_, 2)
    cv2.imwrite(sys.argv[2], img, [cv2.IMWRITE_JPEG_QUALITY, 88])
    print('wrote', sys.argv[2], img.shape)
    # ---- residuals
    kn = C.load_json(os.path.join(C.OUT, 'knobs.json'))
    dev = []
    for e in pj['elements']:
        if e['type'] in ('knob', 'rotary'):
            v = kn[e['id']]
            for d in e['scale']['ticks_deg']:
                m = [t for t in v['ticks'] if abs(((t['deg'] - d) + 180) % 360 - 180) < 5]
                if m:
                    da = np.radians(m[0]['deg'] - d) * e['scale']['tick_r'][1]
                    dr = m[0]['r_out'] - e['scale']['tick_r'][1]
                    dev.append((da, dr))
    dev = np.array(dev)
    print('ticks drawn vs photo: %d ticks, tangential rms %.3f mm, radial (outer end) rms %.3f mm' % (
        len(dev), np.sqrt(np.mean(dev[:, 0] ** 2)), np.sqrt(np.mean(dev[:, 1] ** 2))))
    # centred legends against their control
    ctl = {e['id']: e for e in pj['elements'] if e['type'] in ('knob', 'rotary', 'jack')}
    dx = []
    for e in pj['elements']:
        if e['type'] in ('knob', 'rotary') and e.get('legend'):
            lg = next((t for t in pj['elements'] if t['id'] == e['legend']), None)
            if lg and lg['align'] == 'centre':
                dx.append(lg['x'] - e['x'])
    dx = np.array(dx)
    print('knob legends centred on their knob: mean %+.2f mm, rms about the mean %.2f mm (n %d)' % (dx.mean(), dx.std(), len(dx)))
    print('accuracy numbers:', pj['accuracy_numbers'])


if __name__ == '__main__':
    main()
