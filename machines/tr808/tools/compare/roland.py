#!/usr/bin/env python3
"""Roland's TR-808 plugin (Roland Cloud), Lemus and Michael Fischer's real TR-808 at the same knobs:
every Fischer recording (fischer.py) is also rendered through Roland's plugin, and both plugins are
measured against the real unit the way fischer.py measures Lemus (decays, tail rate, centroid,
pitch, log-spectral distance), and against each other.

  roland.py [--voices BD,SD,...] [--out DIR] [--json OUT] [--lemus-render PATH] [--voicerun PATH] [--held-out | --fit-set]

Roland's plugin is played through lemus-render (tools/host), transport stopped (its own sequencer
quiet), with the tr808.roland source template
(host/sources.json): its main outputs, every pan centred, LEVEL at max, AC LEVEL 0, the standard
drum notes (36 BD, 38 SD, 43/47/50 toms, 64/63/62 congas, 37 RS, 75 claves, 39 CP, 70 maracas,
56 CB, 49 CY, 46 OH, 42 CH) at velocity 64, the knobs Fischer set at their marks (rotation 0-1),
its own extra knobs (BD/CB TUNE, GAIN) at their defaults. Its hits vary a little one to the next
(the first after a start come out low), so each setting plays three hits to settle and three to
measure, and the metrics are the medians of the three.
"""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fischer as F  # noqa: E402

MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
REPO = os.path.normpath(os.path.join(MACHINE, '..', '..'))
LEMUS_RENDER = os.path.join(REPO, 'build', 'tr808', 'lemus-render_artefacts', 'Release', 'lemus-render')
SOURCES = os.path.join(MACHINE, 'host', 'sources.json')

NOTE = {'BD': 36, 'SD': 38, 'LT': 43, 'MT': 47, 'HT': 50, 'LC': 64, 'MC': 63, 'HC': 62, 'RS': 37, 'CL': 75,
        'CP': 39, 'MA': 70, 'CB': 56, 'CY': 49, 'OH': 46, 'CH': 42}
# Fischer's knob names per voice -> the scene's generic parameter names (sources.json)
KNOB = {'BD': {'TONE': 'bd_tone', 'DECAY': 'bd_decay', 'LEVEL': 'bd_level'},
        'SD': {'TONE': 'sd_tone', 'SNAPPY': 'sd_snappy', 'LEVEL': 'sd_level'},
        'LT': {'TUNING': 'lt_tuning', 'LEVEL': 'lt_level'}, 'LC': {'TUNING': 'lt_tuning', 'LEVEL': 'lt_level'},
        'MT': {'TUNING': 'mt_tuning', 'LEVEL': 'mt_level'}, 'MC': {'TUNING': 'mt_tuning', 'LEVEL': 'mt_level'},
        'HT': {'TUNING': 'ht_tuning', 'LEVEL': 'ht_level'}, 'HC': {'TUNING': 'ht_tuning', 'LEVEL': 'ht_level'},
        'RS': {'LEVEL': 'rs_level'}, 'CL': {'LEVEL': 'rs_level'}, 'CP': {'LEVEL': 'cp_level'}, 'MA': {'LEVEL': 'cp_level'},
        'CB': {'LEVEL': 'cb_level'}, 'CY': {'TONE': 'cy_tone', 'DECAY': 'cy_decay', 'LEVEL': 'cy_level'},
        'OH': {'DECAY': 'oh_decay', 'LEVEL': 'oh_level'}, 'CH': {'LEVEL': 'ch_level'}}
WARM, MEASURE, BPM, RATE = 3, 3, 120, 44100


def scene(rec, seconds):
    v = rec['instrument']
    params = {'ac_level': 0.0, 'volume': 0.8}
    for k, x in rec['knobs'].items():
        if k in KNOB[v]:
            params[KNOB[v][k]] = 1.0 if k == 'LEVEL' else x['normalised']
    gap = int(np.ceil((seconds + 0.5) * BPM / 60))  # beats between hits
    notes = [{'at': i * gap, 'note': NOTE[v], 'len': 0.25, 'vel': 64} for i in range(WARM + MEASURE)]
    name = os.path.splitext(os.path.basename(rec['path']))[0].replace(' ', '_')
    return {'name': 'r_' + v + '_' + name, 'bpm': BPM, 'beats': gap * (WARM + MEASURE), 'tail': 0.5, 'rate': RATE, 'playing': False,
            'params': params, 'notes': notes, 'sources': [{'use': 'tr808.roland'}]}, gap * 60.0 / BPM


def metrics(ref, mod, sr, band=(30, 5000)):
    m = {'decay': F.decay_times(mod, sr), 'centroid': F.centroid(mod, sr), 'rate': F.decay_rate(mod, sr),
         'lsd': F.lsd(ref, mod, sr)}
    m['pitch'] = F.pitch(mod, sr, band)
    return m


def align(ref, mod):
    mod = mod[max(0, F.onset(mod) - 20):][:len(ref)]
    return ref[:len(mod)], mod


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--voices', default='')
    ap.add_argument('--out')
    ap.add_argument('--json')
    ap.add_argument('--lemus-render', default=LEMUS_RENDER)
    ap.add_argument('--voicerun', default=F.VOICERUN)
    ap.add_argument('--held-out', action='store_true')
    ap.add_argument('--fit-set', action='store_true')
    a = ap.parse_args()
    want = set(a.voices.upper().split(',')) if a.voices else set(NOTE)
    out = a.out or tempfile.mkdtemp(prefix='tr808roland')
    os.makedirs(out, exist_ok=True)

    recs = []
    for rec in F.records():
        v = rec['instrument']
        if v not in want or v not in NOTE:
            continue
        marks = [F.mark(k) for name, k in rec['knobs'].items() if name != 'LEVEL']
        held = any(m in (2.5, 7.5) for m in marks)
        if (a.held_out and not held) or (a.fit_set and held):
            continue
        ref, sr = sf.read(os.path.join(F.REF, rec['path']))
        ref = ref[:, 0] if ref.ndim > 1 else ref
        ref = ref[max(0, F.onset(ref) - 20):]
        seconds = min(len(ref) / sr, 3.0) + 0.1
        sc, period = scene(rec, seconds)
        path = os.path.join(out, sc['name'] + '.json')
        json.dump(sc, open(path, 'w'))
        recs.append((rec, ref, sr, seconds, sc, period, path))

    # every scene in one lemus-render run (Roland's plugin loads once a scene, the process once)
    todo = [r for r in recs if not os.path.exists(os.path.join(out, r[4]['name'] + '-roland.wav'))]
    if todo:
        cmd = [a.lemus_render] + [r[6] for r in todo] + ['--sources', SOURCES, '--out', out]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode:
            sys.exit('lemus-render failed: ' + res.stderr[-2000:])

    rows = []
    for rec, ref, sr, seconds, sc, period, _ in recs:
        v = rec['instrument']
        x, xsr = sf.read(os.path.join(out, sc['name'] + '-roland.wav'))
        x = x[:, :2].mean(axis=1) if x.ndim > 1 else x
        hits = []
        for i in range(WARM, WARM + MEASURE):
            seg = x[int(i * period * xsr):int((i * period + seconds) * xsr)]
            r, m = align(ref, seg)
            hits.append(metrics(r, m, sr, F.PITCH_BAND.get(v, (30, 5000))))
        roland = {k: (list(np.median([h[k] for h in hits], axis=0)) if k == 'decay' else float(np.median([h[k] for h in hits])))
                  for k in hits[0]}
        mod, _ = F.render(rec, False, [], [], a.voicerun, seconds)
        r, mod = align(ref, mod)
        lemus = metrics(r, mod, sr, F.PITCH_BAND.get(v, (30, 5000)))
        # Lemus against Roland directly (the median hit's waveform as the reference)
        seg = x[int((WARM + 1) * period * xsr):int(((WARM + 1) * period + seconds) * xsr)]
        seg = seg[max(0, F.onset(seg) - 20):]
        n = min(len(seg), len(mod))
        row = {'file': os.path.basename(rec['path']), 'voice': v,
               'knobs': {k: F.mark(xk) for k, xk in rec['knobs'].items() if k != 'LEVEL'},
               'real': {'decay': F.decay_times(ref, sr), 'centroid': F.centroid(ref, sr), 'rate': F.decay_rate(ref, sr),
                        'pitch': F.pitch(ref, sr, F.PITCH_BAND.get(v, (30, 5000)))},
               'roland': roland, 'lemus': lemus, 'lsd_lemus_roland': F.lsd(seg[:n], mod[:n], sr)}
        rows.append(row)
        knobs = ' '.join('%s%g' % (k[0], xk) for k, xk in row['knobs'].items())
        p = ''
        if v in F.TUNED:
            p = '  pitch %6.1f real / %6.1f roland / %6.1f lemus' % (row['real']['pitch'], roland['pitch'], lemus['pitch'])
        print('%-12s %-10s -20dB %5.0f/%5.0f/%5.0f ms  lsd roland %5.2f lemus %5.2f  (lemus-roland %5.2f)%s' % (
            row['file'], knobs, row['real']['decay'][1], roland['decay'][1], lemus['decay'][1], roland['lsd'], lemus['lsd'],
            row['lsd_lemus_roland'], p), flush=True)

    print('\nmedians per voice, against the real unit (model/real: octaves; cents; dB)')
    print('voice  n | -20 dB roland lemus | -40 dB roland lemus | centroid roland lemus | pitch roland lemus | lsd roland lemus | lemus-roland')
    summary = {}
    for v in [v for v in NOTE if any(r['voice'] == v for r in rows)]:
        rs = [r for r in rows if r['voice'] == v]

        def med(f):
            vals = [f(r) for r in rs]
            vals = [x for x in vals if np.isfinite(x)]
            return float(np.median(vals)) if vals else float('nan')
        s = {}
        for src in ('roland', 'lemus'):
            s[src] = {'decay20': med(lambda r: np.log2(r[src]['decay'][1] / r['real']['decay'][1])),
                      'decay40': med(lambda r: np.log2(r[src]['decay'][2] / r['real']['decay'][2])),
                      'centroid': med(lambda r: np.log2(r[src]['centroid'] / r['real']['centroid'])),
                      'cents': med(lambda r: 1200 * np.log2(r[src]['pitch'] / r['real']['pitch'])) if v in F.TUNED else float('nan'),
                      'lsd': med(lambda r: r[src]['lsd'])}
        s['lsd_lemus_roland'] = med(lambda r: r['lsd_lemus_roland'])
        summary[v] = s
        R, L = s['roland'], s['lemus']
        print('%-4s %3d | %+6.2f %+6.2f | %+6.2f %+6.2f | %+6.2f %+6.2f | %+6.0f %+6.0f | %5.2f %5.2f | %5.2f' % (
            v, len(rs), R['decay20'], L['decay20'], R['decay40'], L['decay40'], R['centroid'], L['centroid'],
            R['cents'], L['cents'], R['lsd'], L['lsd'], s['lsd_lemus_roland']))
    if a.json:
        json.dump({'rows': rows, 'summary': summary}, open(a.json, 'w'), indent=1, default=float)


if __name__ == '__main__':
    main()
