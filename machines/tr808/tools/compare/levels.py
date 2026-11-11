#!/usr/bin/env python3
"""Levels against the real units, where the recording's gain or the LEVEL knob would otherwise hide
them. Two measures:

  1. Fischer's set (individual outputs, every LEVEL at max, one chain): recording minus model per
     voice, in dB, the median over the voice's files. A common offset is the chain's gain; a voice
     apart from the rest has its own level wrong. Files peaking above -3 dBFS are left out (the
     chain limits at about -2.45), so the clap, maracas and rim shot aren't in this table; the clap's
     windows are printed against the others' common gain, at its TM3 as fitted and at Fischer's.

  2. The clap against the maracas, in dB, on every real unit (RMS from the onset): the two share one
     LEVEL knob and one jack, so neither the knob nor the chain's gain counts. The model at several
     TM3 settings shows what the difference says about each unit's trim.

  levels.py [--voicerun PATH]
"""
import argparse, collections, os, sys
import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fischer as fz
import units as un

TM3S = (0.26, 0.30, 0.33, 0.37, 0.42)
WINDOWS = ((0, 0.03), (0, 0.06))


def rms_db(x, sr, t0, t1):
    s = x[int(t0 * sr):int(t1 * sr)]
    return 20 * np.log10(np.sqrt(np.mean((s - np.mean(s)) ** 2)) + 1e-12)


def from_fischer(rec, voicerun, cals, seconds=1.0):
    ref, sr = sf.read(os.path.join(fz.REF, rec['path']))
    ref = ref if ref.ndim == 1 else ref[:, 0]
    ref = ref[max(0, fz.onset(ref) - 20):]
    mod, _ = fz.render(rec, False, [], cals, voicerun, min(len(ref) / sr, seconds) + 0.1)
    mod = mod[max(0, fz.onset(mod) - 20):][:len(ref)]
    return ref[:len(mod)], mod, sr


def voices_against_fischer(voicerun):
    diffs = collections.defaultdict(list)
    for rec in fz.records():
        v = rec['instrument']
        if v in fz.VOICES and rec['audio']['peak_dbfs'] <= -3:
            ref, mod, sr = from_fischer(rec, voicerun, [])
            diffs[v].append(rms_db(ref, sr, 0, 0.03) - rms_db(mod, sr, 0, 0.03))
    med = {v: float(np.median(d)) for v, d in diffs.items()}
    gain = float(np.median(list(med.values())))
    spread = float(np.subtract(*np.percentile(list(med.values()), [75, 25])))
    print('Recording minus model, first 30 ms, dB (Fischer; LEVEL at max)')
    for v, d in med.items():
        print('  %-3s %2d files  %+6.2f   (%+5.2f from the common gain)' % (v, len(diffs[v]), d, d - gain))
    print('  the common gain %+.2f, interquartile range %.2f' % (gain, spread))
    rec = next(r for r in fz.records() if r['instrument'] == 'CP')
    print('\nThe clap against that gain, by window (0-30, 30-60, 60-120, 120-250 ms), dB')
    for name, cals in (('as fitted', []), ("Fischer's TM3 0.26", ['cpOffset=0.26'])):
        ref, mod, sr = from_fischer(rec, voicerun, cals)
        cells = [rms_db(ref, sr, a, b) - rms_db(mod, sr, a, b) - gain for a, b in ((0, 0.03), (0.03, 0.06), (0.06, 0.12), (0.12, 0.25))]
        print('  %-20s' % name, ' '.join('%+6.2f' % c for c in cells))


def clap_minus_maracas(cp, ma):
    out = []
    for a, b in WINDOWS:
        c = 20 * np.log10(np.mean([10 ** (rms_db(x, sr, a, b) / 20) for x, sr in cp]))
        m = 20 * np.log10(np.mean([10 ** (rms_db(x, sr, a, b) / 20) for x, sr in ma]))
        out.append(c - m)
    return out


def clap_against_maracas(voicerun):
    print('\nClap minus maracas, dB:           0-30 ms  0-60 ms')
    takes = {"Fischer's": ([un.fischer_take('CP')], [un.fischer_take('MA')]),
             "Raum 45's": (un.raum_takes('CP'), un.raum_takes('MA')),
             "Reverb's": (un.reverb_takes('CP', False), un.reverb_takes('MA', False)),
             "archive.org's": (un.archive_takes('CP', False), un.archive_takes('MA', False))}
    for name, (cp, ma) in takes.items():
        print('  %-30s' % (name + ' unit'), ' '.join('%+8.2f' % v for v in clap_minus_maracas(cp, ma)))
    for tm3 in TM3S:
        cal = ['cpOffset=%g' % tm3]
        cp = un.model_takes('CP', False, voicerun, [], cal)
        ma = un.model_takes('MA', False, voicerun, [], cal)
        print('  %-30s' % ('the model, TM3 %.2f' % tm3), ' '.join('%+8.2f' % v for v in clap_minus_maracas(cp, ma)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--voicerun', default=fz.VOICERUN)
    a = ap.parse_args()
    voices_against_fischer(a.voicerun)
    clap_against_maracas(a.voicerun)


if __name__ == '__main__':
    main()
