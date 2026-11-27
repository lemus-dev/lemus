#!/usr/bin/env python3
"""Fit the master photo's mapping onto the panel plane (docs/panel.md section 1).

Model: radial lens distortion about the principal point (k1; the 28.5 mm lens' barrel), then
a homography. It is fitted to the panel's own print only:
  * every printed straight line (photo_lines.py) must come out straight and exactly horizontal or
    vertical;
  * every dial's tick ends (photo_centres.py) must lie on a circle round the dial centre.
That fixes rotation, shear, keystone, aspect and distortion; the gauge (position and scale) is
pinned at the image centre and set to mm later (scale.py). The service manual's board drawings
are not used here: they are the independent check (check_pcb.py).

Usage: rectify_fit.py PHOTO_CENTRES PHOTO_LINES OUT_JSON"""
import sys, os
import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C

W, H = 7875, 5296
CX, CY = W / 2.0, H / 2.0
NORM = C.F_PX                      # distortion radius unit: the focal length in px


def undistort(p, k, c=(CX, CY)):
    p = np.asarray(p, float)
    d = (p - c) / NORM
    r2 = (d ** 2).sum(-1, keepdims=True)
    return c + d * NORM * (1 + k[0] * r2 + k[1] * r2 * r2)


def hom(h):
    return np.append(h, 1.0).reshape(3, 3)


def apply_h(Hm, p):
    p = np.asarray(p, float)
    q = np.c_[p, np.ones(len(p))] @ Hm.T
    return q[:, :2] / q[:, 2:]


def model(params, p):
    k = params[:2]
    Hm = hom(params[2:10])
    return apply_h(Hm, undistort(p, k))


def build(centres, lines):
    L = []
    for l in lines['lines']:
        if l['kind'] == 'h' and l['p0'][1] < 1040:
            continue                      # the panel's top edge highlight, not print
        L.append((0 if l['kind'] == 'v' else 1, np.array(l['pts'])))
    T = []
    for k, v in centres['knobs'].items():
        ends = [t['end'] for t in v['ticks'] if t.get('end') is not None]
        if len(ends) >= 3:
            T.append((np.array(v['c']), np.array(ends)))
    return L, T


def residuals(params, L, T, sig_line=0.15, sig_tick=0.5):
    out = []
    for a, pts in L:
        q = model(params, pts)[:, a]
        out.append((q - q.mean()) / sig_line)
    for c, ends in T:
        qc = model(params, c[None, :])[0]
        q = model(params, ends)
        r = np.hypot(*(q - qc).T)
        out.append((r - r.mean()) / sig_tick)
    # gauge: the image centre stays put and keeps its local scale
    p0 = np.array([[CX, CY], [CX + 100, CY], [CX, CY + 100]])
    q0 = model(params, p0)
    out.append((q0[0] - p0[0]) * 100)
    J = np.c_[q0[1] - q0[0], q0[2] - q0[0]] / 100.0
    out.append([(np.linalg.det(J) - 1) * 1e4])
    return np.concatenate([np.ravel(o) for o in out])


def main():
    centres = C.load_json(sys.argv[1]); lines = C.load_json(sys.argv[2])
    L, T = build(centres, lines)
    x0 = np.r_[0.0, 0.0, 1, 0, 0, 0, 1, 0, 0, 0]
    report = {}
    for name, free in (('homography', slice(2, 10)), ('homography+k1', slice(0, 10)), ('homography+k1+k2', slice(0, 10))):
        def f(xf, name=name):
            x = x0.copy()
            if name == 'homography':
                x[2:] = xf
            elif name == 'homography+k1':
                x[0] = xf[0]; x[2:] = xf[1:]
            else:
                x[:] = xf
            return residuals(x, L, T)
        if name == 'homography':
            init = x0[2:]
        elif name == 'homography+k1':
            init = np.r_[x0[0], x0[2:]]
        else:
            init = x0
        r = least_squares(f, init, x_scale='jac', method='trf', loss='soft_l1', f_scale=3.0, max_nfev=20000)
        if name == 'homography':
            x = x0.copy(); x[2:] = r.x
        elif name == 'homography+k1':
            x = x0.copy(); x[0] = r.x[0]; x[2:] = r.x[1:]
        else:
            x = r.x
        res = residuals(x, L, T)
        nl = sum(len(p) for _, p in L)
        rl = res[:nl] * 0.15
        rt = res[nl:nl + sum(len(e) for _, e in T)] * 0.5
        print('%-18s line rms %.3f px  tick rms %.3f px  k=(%.4f, %.4f)' % (name, np.sqrt((rl ** 2).mean()), np.sqrt((rt ** 2).mean()), x[0], x[1]))
        report[name] = dict(params=x.tolist(), line_rms_px=float(np.sqrt((rl ** 2).mean())), tick_rms_px=float(np.sqrt((rt ** 2).mean())))
        x0 = x if name != 'homography' else x0
        if name == 'homography':
            x0 = x.copy()
    best = report['homography+k1']      # k2 is not determined by the data (it trades with k1)
    # per-line straightness after the fit
    x = np.array(best['params'])
    per = []
    for (a, pts), l in zip(L, [l for l in lines['lines'] if not (l['kind'] == 'h' and l['p0'][1] < 1040)]):
        q = model(x, pts)
        per.append(dict(kind=l['kind'], at=float(q[:, a].mean()), span=[float(q[:, 1 - a].min()), float(q[:, 1 - a].max())],
                        rms=float(q[:, a].std()), width_px=l['width_px']))
    C.save_json(dict(model='radial k1 about the image centre (r in units of f = %.1f px), then homography' % NORM,
                     centre=[CX, CY], norm=NORM, fits=report, params=best['params'], lines_after=per), sys.argv[3])


if __name__ == '__main__':
    main()
