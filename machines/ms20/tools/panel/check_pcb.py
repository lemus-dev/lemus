#!/usr/bin/env python3
"""Independent check of the rectified frame against the service manual's board drawings, and the
scale cross-checks (docs/panel.md section 1).

  * KLM-127: the 30 knob centres (tick-ring centres, print, no parallax) against the drawn shaft
    centres. Similarity and affine fits: residuals, the drawing's scale (drawing px per mm), its
    shear, and what the drawn pot outline measures in mm (16 mm pots would draw 16 mm).
  * KLM-128C: the 34 board jacks. The photo sees each jack's hole at the top of its bushing,
    which stands proud of the panel: the height is fitted here (one value for all jacks) as the one
    that makes the jack grid match the drawing best by a similarity.

Usage: check_pcb.py PHOTO_CENTRES PCB_REFS OUT_JSON"""
import sys, os
import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C
import frame


def sim_fit(src, dst):
    """dst ~ s R src + t (least squares). Returns params, residuals."""
    def f(p):
        c, s_ = np.cos(p[1]), np.sin(p[1])
        Rm = p[0] * np.array([[c, -s_], [s_, c]])
        return ((src @ Rm.T + p[2:4]) - dst).ravel()
    sc = np.sqrt(((dst - dst.mean(0)) ** 2).sum() / ((src - src.mean(0)) ** 2).sum())
    r = least_squares(f, [sc, 0, *(dst.mean(0) - sc * src.mean(0))])
    return r.x, f(r.x).reshape(-1, 2)


def aff_fit(src, dst):
    A = np.c_[src, np.ones(len(src))]
    M, *_ = np.linalg.lstsq(A, dst, rcond=None)
    return M, A @ M - dst


def rms(r):
    return float(np.sqrt((r ** 2).sum(1).mean()))


def main():
    pc = C.load_json(sys.argv[1]); pr = C.load_json(sys.argv[2])
    F = frame.Frame()
    out = {}
    # ---- KLM-127 ----
    names, P, D = [], [], []
    for k, v in pc['knobs'].items():
        if v['board'] == '127' and pr['klm127'].get(v['part']):
            names.append(k); P.append(F.mm(v['c'])[0]); D.append(pr['klm127'][v['part']]['c'])
    P = np.array(P); D = np.array(D)
    ps, rs = sim_fit(D, P)
    M, ra = aff_fit(D, P)
    ax = np.linalg.norm(M[0]); ay = np.linalg.norm(M[1])
    shear = np.degrees(np.arccos(M[0] @ M[1] / ax / ay)) - 90
    potd = np.mean([pr['klm127'][p]['r'] for p in pr['klm127'] if pr['klm127'][p] and pr['kinds'][p] == 'pot']) * 2
    swd = np.mean([pr['klm127'][p]['r'] for p in pr['klm127'] if pr['klm127'][p] and pr['kinds'][p] == 'switch']) * 2
    print('KLM-127 (%d knobs): similarity rms %.3f mm (max %.2f), affine rms %.3f mm; drawing %.3f px/mm (x %.3f, y %.3f), shear %.2f deg, rotation %.2f deg' % (
        len(P), rms(rs), np.abs(rs).max(), rms(ra), 1 / ps[0], 1 / ax, 1 / ay, shear, np.degrees(ps[1])))
    print('  drawn pot outline %.2f mm, rotary switch outline %.2f mm (at the similarity scale)' % (potd * ps[0], swd * ps[0]))
    out['klm127'] = dict(n=len(P), sim_rms=rms(rs), sim_max=float(np.abs(rs).max()), aff_rms=rms(ra),
                         px_per_mm=1 / ps[0], px_per_mm_x=1 / ax, px_per_mm_y=1 / ay, shear_deg=shear,
                         pot_outline_mm=potd * ps[0], switch_outline_mm=swd * ps[0],
                         residuals={n: [float(a), float(b)] for n, (a, b) in zip(names, rs)},
                         sim=ps.tolist())
    for n, r in zip(names, rs):
        print('   %-12s %+.2f %+.2f' % (n, *r))
    # trimmers: drawn VR1-VR8 mapped through the similarity (where the panel's plugged holes are)
    c, s_ = np.cos(ps[1]), np.sin(ps[1])
    Rm = ps[0] * np.array([[c, -s_], [s_, c]])
    out['klm127']['mapped'] = {p: (np.array(v['c']) @ Rm.T + ps[2:4]).tolist() for p, v in pr['klm127'].items() if v}
    # ---- KLM-128C ----
    jn, U, D2 = [], [], []
    for k, v in pc['jacks'].items():
        if pr['klm128'].get(v['j']):
            jn.append(k); U.append(v['hole']); D2.append(pr['klm128'][v['j']]['c'])
    U = np.array(U); D2 = np.array(D2)
    best = None
    for h in np.arange(0, 12.01, 0.25):
        Pj = F.mm(U, h)
        pj, rj = sim_fit(D2, Pj)
        if best is None or rms(rj) < best[1]:
            best = (h, rms(rj), pj, rj, Pj)
    h, r0, pj, rj, Pj = best
    M2, ra2 = aff_fit(D2, Pj)
    ax2 = np.linalg.norm(M2[0]); ay2 = np.linalg.norm(M2[1])
    shear2 = np.degrees(np.arccos(M2[0] @ M2[1] / ax2 / ay2)) - 90
    print('KLM-128C (%d jacks): best hole height %.2f mm; similarity rms %.3f mm (max %.2f), affine rms %.3f; drawing %.3f px/mm, shear %.2f deg, rotation %.2f deg' % (
        len(U), h, r0, np.abs(rj).max(), rms(ra2), 1 / pj[0], shear2, np.degrees(pj[1])))
    for hh in (0, 3, 6, 9):
        print('   at h=%d: rms %.3f' % (hh, rms(sim_fit(D2, F.mm(U, hh))[1])))
    ringd = np.mean([v['r'] for v in pr['klm128'].values() if v]) * 2
    print('  drawn jack ring %.2f mm' % (ringd * pj[0]))
    for n, r in zip(jn, rj):
        print('   %-9s %+.2f %+.2f' % (n, *r))
    out['klm128'] = dict(n=len(U), hole_height_mm=float(h), sim_rms=r0, sim_max=float(np.abs(rj).max()), aff_rms=rms(ra2),
                         px_per_mm=1 / pj[0], shear_deg=shear2, ring_mm=ringd * pj[0],
                         residuals={n: [float(a), float(b)] for n, (a, b) in zip(jn, rj)}, sim=pj.tolist())
    C.save_json(out, sys.argv[3])


if __name__ == '__main__':
    main()
