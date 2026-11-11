#!/usr/bin/env python3
"""Accent against the second real unit (archive.org "TR 808 Samples", private/reference/real808/
archiveorg_tr-808-samples/808 accent levels.flac): each voice hit accented and then plain at the mix
out, knobs undocumented. Only the ratio compares: accented over plain in dB, peak to peak and RMS
over the first 30 ms of the hit (RMS is the measure for the noisy and metal voices, whose peak to
peak varies hit to hit). The model is rendered through voicerun from the voice's jack, ACCENT fully
clockwise, at the knob settings the real takes' spectra and decays match (docs/model.md, "A second
real unit"). The clap's accent follows its TM3: this unit's is about 0.38, the fitted unit's the
other units' 0.37, Fischer's 0.26 ("Unit spread"). The snare's takes are dark: TONE 0, SNAPPY 0.5.

  accent.py [--voices SD,BD,...] [--cal NAME=VALUE ...] [--set NAME=VALUE ...] [--voicerun PATH]
"""
import argparse, os, subprocess, sys, tempfile
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
FILE = os.path.join(MACHINE, 'private', 'reference', 'real808', 'archiveorg_tr-808-samples', '808 accent levels.flac')
VOICERUN = os.path.normpath(os.path.join(MACHINE, '..', '..', 'build', 'tr808', 'voicerun'))
JACK = ['bd', 'sd', 'lt', 'mt', 'ht', 'rs', 'cp', 'cb', 'cy', 'oh', 'ch']

# The real takes: (accented onset, plain onset) in seconds, from index.json's detected hits,
# identified by ear and spectrum; the model's voice and the settings that match them.
TAKES = {
    'BD': ([(0.9939, 2.3259)], 'bd', []),
    'SD': ([(35.654, 36.987), (38.319, 39.653), (40.985, 42.317)], 'sd', ['sdTone=0', 'sdSnappy=0.5']),
    'LT': ([(56.980, 58.312), (59.644, 60.978), (62.310, 63.642)], 'lt', []),
    'MT': ([(70.307, 71.641), (72.973, 74.305)], 'mt', []),
    'HT': ([(88.969, 90.300), (91.634, 92.967)], 'ht', []),
    'RS': ([(102.299, 103.631), (104.965, 106.297)], 'rs', []),
    'CL': ([(107.629, 108.963), (110.297, 111.629)], 'rs', ['cl=1']),
    'CB': ([(142.292, 143.625), (144.957, 146.291)], 'cb', []),
    'CP': ([(120.962, 122.294), (123.628, 124.960)], 'cp', []),
    'MA': ([(152.956, 154.292), (155.624, 156.956)], 'cp', ['ma=1']),
    'CH': ([(198.057, 200.169), (202.278, 204.388)], 'ch', []),
}


def rms30(x, sr):
    return float(np.sqrt(np.mean(x[:int(0.03 * sr)] ** 2)))


def pp30(x, sr):
    s = x[:int(0.03 * sr)]
    return float(s.max() - s.min())


def real(takes):
    x, sr = sf.read(FILE)
    out = []
    for a, p in takes:
        ha = x[int(a * sr) - int(0.001 * sr):][:int(0.05 * sr)]
        hp = x[int(p * sr) - int(0.001 * sr):][:int(0.05 * sr)]
        out.append((20 * np.log10(pp30(ha, sr) / pp30(hp, sr)), 20 * np.log10(rms30(ha, sr) / rms30(hp, sr))))
    return np.mean(out, axis=0)


def model(voice, sets, cals, voicerun):
    res = []
    for ac in (True, False):
        with tempfile.TemporaryDirectory() as tmp:
            jacks = os.path.join(tmp, 'j.wav')
            args = [voicerun, os.path.join(tmp, 'm.wav'), '--rate', '48000', '--seconds', '0.3',
                    '--hit', '0.05:' + voice + ('+ac' if ac else ''), '--jacks', jacks, '--set', 'accent=1']
            for s in sets:
                args += ['--set', s]
            for c in cals:
                args += ['--cal', c]
            r = subprocess.run(args, capture_output=True, text=True)
            if r.returncode:
                sys.exit('voicerun failed: ' + r.stderr)
            x, sr = sf.read(jacks)
        y = x[:, JACK.index(voice)]
        y = y[int(0.05 * sr) - int(0.001 * sr):] - y[int(0.04 * sr)]
        res.append((pp30(y, sr), rms30(y, sr)))
    return 20 * np.log10(res[0][0] / res[1][0]), 20 * np.log10(res[0][1] / res[1][1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--voices', default=','.join(TAKES))
    ap.add_argument('--cal', action='append', default=[])
    ap.add_argument('--set', action='append', default=[])
    ap.add_argument('--voicerun', default=VOICERUN)
    a = ap.parse_args()
    print('%-4s %16s %16s %14s' % ('', 'real pp / rms', 'model pp / rms', 'diff rms'))
    for v in a.voices.split(','):
        takes, voice, sets = TAKES[v]
        rp, rr = real(takes)
        mp, mr = model(voice, sets + a.set, a.cal, a.voicerun)
        print('%-4s %7.1f / %5.1f  %7.1f / %5.1f  %+8.1f' % (v, rp, rr, mp, mr, mr - rr))


if __name__ == '__main__':
    main()
