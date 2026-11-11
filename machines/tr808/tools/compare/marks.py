#!/usr/bin/env python3
"""The rotation each of Fischer's knob marks really sets on his unit: for every recording of the
given voices, the knob rotation (0-1) at which the model meets the real hit on one measure, found by
bisection with the other knobs at their marks. TUNING by pitch, DECAY by the -20 dB time (its
rotation where the measure is monotonic). Prints a row per recording and, per knob and mark, the
median rotation: the points of Calibration::knobLaws (docs/model.md, "Calibration"). voicerun
applies the unit's knob laws, so to measure a knob afresh give it a straight one:

  marks.py LT,LC TUNING --cal law.ltTuning=0.25,0.5,0.75

  marks.py VOICES KNOB [--measure pitch|decay20|centroid] [--cal NAME=VALUE ...]
"""
import argparse, os, sys
import numpy as np
import soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fischer as F

ap = argparse.ArgumentParser()
ap.add_argument('voices')
ap.add_argument('knob')
ap.add_argument('--measure', default='pitch')
ap.add_argument('--cal', action='append', default=[])
ap.add_argument('--iters', type=int, default=11)
a = ap.parse_args()


def measure(x, sr):
    if a.measure == 'pitch':
        return np.log2(F.pitch(x, sr))
    if a.measure == 'decay20':
        return np.log2(F.decay_times(x, sr)[1])
    if a.measure == 'centroid':
        return np.log2(F.centroid(x, sr))
    raise SystemExit('unknown measure')


rows = {}
for rec in F.records():
    v = rec['instrument']
    if v not in a.voices.upper().split(',') or a.knob not in rec['knobs']:
        continue
    ref, sr = sf.read(os.path.join(F.REF, rec['path']))
    ref = ref[max(0, F.onset(ref) - 20):]
    target = measure(ref, sr)
    seconds = min(len(ref) / sr, 3.0) + 0.1

    def at(r):
        trial = dict(rec, knobs=dict(rec['knobs'], **{a.knob: dict(rec['knobs'][a.knob], normalised=r)}))
        mod, _ = F.render(trial, False, [], a.cal, F.VOICERUN, seconds)
        mod = mod[max(0, F.onset(mod) - 20):][:len(ref)]
        return measure(mod, sr)

    lo, hi = 0.0, 1.0
    mlo, mhi = at(lo), at(hi)
    sign = 1 if mhi > mlo else -1
    if not (min(mlo, mhi) <= target <= max(mlo, mhi)):
        r = lo if abs(target - mlo) < abs(target - mhi) else hi
    else:
        for _ in range(a.iters):
            mid = 0.5 * (lo + hi)
            if (at(mid) - target) * sign < 0:
                lo = mid
            else:
                hi = mid
        r = 0.5 * (lo + hi)
    mark = F.mark(rec['knobs'][a.knob])
    rows.setdefault(mark, []).append(r)
    print('%-12s %s %4.1f -> rotation %.3f' % (os.path.basename(rec['path']), a.knob, mark, r), flush=True)
print('\nmark   median   spread (min..max)')
for m in sorted(rows):
    v = rows[m]
    print('%4.1f   %.3f    %.3f..%.3f  (%d)' % (m, np.median(v), min(v), max(v), len(v)))
