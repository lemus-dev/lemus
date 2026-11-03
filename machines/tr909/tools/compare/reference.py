#!/usr/bin/env python3
"""The target tables of docs/reference.md: real909.py's measurements over the AudioRealism grid,
the second units (Rob Roy, Regebro, the unlabelled KB6 and Echo Sound Works sets) and Roland's
plugin renders, written as Markdown tables and JSON.

  reference.py [--jobs 4] [--out DIR] [--fresh]

Every clip is measured once (real909.measure, the ROM clock for hats and cymbals) and cached in
DIR/cache.json by path and modification time; DIR defaults to
private/reference/real909/_measurements_reference (measurements of private recordings stay
private; docs/reference.md quotes them). The tables go to DIR/tables.md, the per-clip numbers to
DIR/measurements.json.
"""
import argparse, json, math, os, sys
from multiprocessing import Pool

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import real909 as r  # noqa: E402

OUT = os.path.join(r.REAL909, '_measurements_reference')
SECOND = ('rr', 'kb6', 'esw', 'regebro', 'roland', 'scherer')


def wanted():
    """The clips the tables use: every AR clip but the flams and shuffles, the second units,
    Roland's knob sweeps, defaults and accent pairs."""
    out = []
    for c in r.catalogue():
        if c.source == 'ar' and not any(k in c.extra for k in ('flam', 'shuffle')):
            out.append(c)
        elif c.source in SECOND:
            out.append(c)
    return out


def _job(c):
    try:
        x, sr = r.load(c)
        y, i = r.trim(x, sr)
        m = r.measure(y, sr, c.voice, rate=c.voice in r.SAMPLED)
        m['onset_ms'] = i / sr * 1000
        # per-band envelope -20 dB times, octave bands (for the hats and cymbals' brightness decay)
        if c.voice in r.SAMPLED or c.voice in ('SD', 'HC', 'RS'):
            t, f, E = r.band_envelopes(y, sr, edges=[1000, 2000, 4000, 8000, 16000], win_ms=10, hop_ms=2)
            m['band_t20_ms'] = {f'{fc:.0f}': float(t[np.nonzero(e > e.max() - 20)[0][-1]] * 1000) for fc, e in zip(f, E)}
        return c.path, json.loads(json.dumps(m, default=float))
    except Exception as e:  # report and go on
        return c.path, {'error': repr(e)}


def measure_all(jobs, fresh):
    os.makedirs(OUT, exist_ok=True)
    cp = os.path.join(OUT, 'cache.json')
    cache = {} if fresh or not os.path.exists(cp) else json.load(open(cp))
    todo = [c for c in wanted() if cache.get(c.path, {}).get('_mtime') != os.path.getmtime(c.path)]
    print(f'{len(todo)} clips to measure ({len(cache)} cached)', file=sys.stderr)
    with Pool(jobs) as p:
        for k, (path, m) in enumerate(p.imap_unordered(_job, todo, chunksize=4)):
            m['_mtime'] = os.path.getmtime(path)
            cache[path] = m
            if k % 100 == 99:
                print(f'  {k + 1}', file=sys.stderr)
                json.dump(cache, open(cp, 'w'))
    json.dump(cache, open(cp, 'w'))
    return cache


# ---------------------------------------------------------------------------------------------

def fm(v, nd=1):
    if v is None or (isinstance(v, float) and (math.isnan(v) or math.isinf(v))):
        return '-'
    if isinstance(v, (list, tuple)):
        return ', '.join(fm(z, nd) for z in v)
    return f'{v:.{nd}f}'


def table(head, rows):
    out = ['| ' + ' | '.join(head) + ' |', '|' + '|'.join(['---'] * len(head)) + '|']
    out += ['| ' + ' | '.join(str(c) for c in row) + ' |' for row in rows]
    return '\n'.join(out) + '\n'


class M:
    """Lookup of cached measurements by catalogue query."""

    def __init__(self, cache):
        self.c = cache

    def get(self, voice, source='ar', accent=None, **k):
        try:
            clip = r.get(voice, source, accent, **k)
        except LookupError:
            return None
        return self.c.get(clip.path)

    def all(self, voice, source='ar', accent=None, any_accent=False, **k):
        return [(cl, self.c[cl.path]) for cl in r.find(voice, source, accent, any_accent, **k) if cl.path in self.c]


def stat(vals):
    v = np.array([x for x in vals if x is not None and x == x], dtype=float)
    if not len(v):
        return float('nan'), float('nan'), float('nan')
    return float(np.median(v)), float(v.min()), float(v.max())


def spread(vals, nd=1):
    m, lo, hi = stat(vals)
    return f'{fm(m, nd)} ({fm(lo, nd)}-{fm(hi, nd)})' if m == m else '-'


def g(m, k):
    return None if m is None else m.get(k)


def cents(a, b):
    return 1200 * math.log2(a / b) if a and b and a > 0 and b > 0 else float('nan')


def linfit(x, y):
    """Least-squares line y = a + b x; returns (a, b, rms residual, max |residual|)."""
    x, y = np.asarray(x, float), np.asarray(y, float)
    ok = np.isfinite(y)
    x, y = x[ok], y[ok]
    b, a = np.polyfit(x, y, 1)
    res = y - (a + b * x)
    return float(a), float(b), float(np.sqrt(np.mean(res ** 2))), float(np.max(np.abs(res)))


def tables(cache):
    D = M(cache)
    md = []
    J = {}
    Q = [0, 0.25, 0.5, 0.75, 1.0]
    T11 = [round(0.1 * i, 1) for i in range(11)]

    # ---------------------------------------------------------------- BD
    md.append('## BD\n')
    rows, jt = [], {}
    for t in Q:
        cells = [D.get('BD', tune=t, attack=a, decay=d) for a in Q for d in Q]
        base = D.get('BD', tune=t, attack=0, decay=1)
        rows.append([fm(t, 2), fm(g(base, 'f_first')), fm(g(base, 'f_10ms')), fm(g(base, 'f_50ms')), fm(g(base, 'f_100ms')),
                     fm(g(base, 'sweep_f_start')), fm(g(base, 'sweep_f_end'), 2), fm(g(base, 'sweep_tau_ms'), 2),
                     spread([g(c, 'sweep_tau_ms') for c in cells], 2), spread([g(c, 'sweep_f_end') for c in cells], 2),
                     fm(g(base, 'f_late'), 2)])
        jt[t] = {k: g(base, k) for k in ('f_first', 'f_10ms', 'f_50ms', 'f_100ms', 'sweep_f_start', 'sweep_f_end', 'sweep_tau_ms', 'f_late')}
        jt[t]['tau_over_grid'] = stat([g(c, 'sweep_tau_ms') for c in cells])
    J['bd_tune'] = jt
    md.append('BD TUNE (AR; ATTACK 0, DECAY 1; the last two columns over all 25 ATTACK x DECAY cells: median (min-max)).\n')
    md.append(table(['TUNE', 'f first', 'f 10 ms', 'f 50 ms', 'f 100 ms', 'fit f(0)', 'fit f_end', 'tau ms',
                     'tau ms, 25 cells', 'f_end, 25 cells', 'f 200-400 ms'], rows))
    rows = []
    for d in Q:
        cells = [D.get('BD', tune=t, attack=a, decay=d) for t in Q for a in Q]
        base = D.get('BD', tune=0.5, attack=0, decay=d)
        rows.append([fm(d, 2), fm(g(base, 't20')), fm(g(base, 't40')), fm(g(base, 't60')), fm(g(base, 'tail_tau_ms')),
                     spread([g(c, 't20') for c in cells], 0), spread([g(c, 't40') for c in cells], 0), fm(g(base, 'energy_db'), 2)])
    md.append('\nBD DECAY (AR; TUNE 0.5, ATTACK 0; spread over TUNE x ATTACK). Times from the onset, ms.\n')
    md.append(table(['DECAY', 't-20', 't-40', 't-60', 'tail tau', 't-20, 25 cells', 't-40, 25 cells', 'energy dB'], rows))
    rows = []
    for a in Q:
        base = D.get('BD', tune=0.5, attack=a, decay=1)
        rows.append([fm(a, 2), fm(g(base, 'peak_db'), 2), fm(g(base, 'rms50_db'), 2), fm(g(base, 'click_db'), 2),
                     fm(g(base, 'centroid_hz'), 0), fm(g(base, 'f_first'))])
    md.append('\nBD ATTACK (AR; TUNE 0.5, DECAY 1). click = energy above 1 kHz in the first 5 ms re the whole hit.\n')
    md.append(table(['ATTACK', 'peak dBFS', 'rms 0-50 ms', 'click dB', 'centroid 0-60 ms', 'f first'], rows))
    # Rob Roy
    rows = []
    for t in (0, 0.3, 0.7, 1.0):
        base = D.get('BD', 'rr', tune=t, attack=0, decay=1)
        rows.append([fm(t * 10, 0), fm(g(base, 'f_first')), fm(g(base, 'f_50ms')), fm(g(base, 'sweep_f_start')),
                     fm(g(base, 'sweep_f_end'), 2), fm(g(base, 'sweep_tau_ms'), 2), fm(g(base, 't20')), fm(g(base, 't40'))])
    md.append('\nBD, Rob Roy unit (ATTACK 0, DECAY 10; normalised, tails trimmed).\n')
    md.append(table(['TUNE dial', 'f first', 'f 50 ms', 'fit f(0)', 'fit f_end', 'tau ms', 't-20', 't-40'], rows))
    rows = []
    for d in (0, 0.3, 0.7, 1.0):
        base = D.get('BD', 'rr', tune=0.7, attack=0, decay=d)
        rows.append([fm(d * 10, 0), fm(g(base, 't20')), fm(g(base, 't40')), fm(g(base, 'tail_tau_ms'))])
    md.append('\nBD DECAY, Rob Roy (TUNE 7, ATTACK 0).\n')
    md.append(table(['DECAY dial', 't-20', 't-40', 'tail tau'], rows))
    # Roland
    rows = []
    for t in Q:
        ro, ar = D.get('BD', 'roland', tune=t), D.get('BD', tune=t, attack=0.5, decay=0.5)
        rows.append([fm(t, 2), fm(g(ar, 'f_first')), fm(g(ro, 'f_first')), fm(g(ar, 'sweep_f_end'), 2), fm(g(ro, 'sweep_f_end'), 2),
                     fm(g(ar, 'sweep_tau_ms'), 2), fm(g(ro, 'sweep_tau_ms'), 2), fm(g(ar, 'f_50ms')), fm(g(ro, 'f_50ms'))])
    md.append('\nBD TUNE, AR against Roland\'s plugin at the same knobs (ATTACK 0.5, DECAY 0.5; Roland at vel 127).\n')
    md.append(table(['TUNE', 'f first AR', 'Roland', 'f_end AR', 'Roland', 'tau AR', 'Roland', 'f 50 ms AR', 'Roland'], rows))
    rows = []
    for d in Q:
        ro, ar = D.get('BD', 'roland', decay=d), D.get('BD', tune=0.5, attack=0.5, decay=d)
        rows.append([fm(d, 2), fm(g(ar, 't20')), fm(g(ro, 't20')), fm(g(ar, 't40')), fm(g(ro, 't40')), fm(g(ar, 't60')), fm(g(ro, 't60'))])
    md.append('\nBD DECAY, AR against Roland (TUNE 0.5, ATTACK 0.5).\n')
    md.append(table(['DECAY', 't-20 AR', 'Roland', 't-40 AR', 'Roland', 't-60 AR', 'Roland'], rows))
    rows = []
    for a in Q:
        ro, ar = D.get('BD', 'roland', attack=a), D.get('BD', tune=0.5, attack=a, decay=0.5)
        rows.append([fm(a, 2), fm(g(ar, 'click_db'), 1), fm(g(ro, 'click_db'), 1), fm(g(ar, 'peak_db') - g(ar, 'rms50_db') if ar else None, 1),
                     fm(g(ro, 'peak_db') - g(ro, 'rms50_db') if ro else None, 1)])
    md.append('\nBD ATTACK, AR against Roland (click dB as above; crest = peak - rms 0-50 ms).\n')
    md.append(table(['ATTACK', 'click AR', 'Roland', 'crest AR', 'Roland'], rows))
    # unlabelled sets: f_end only
    rows = []
    for src in ('kb6', 'esw', 'regebro'):
        vals = [m.get('sweep_f_end') for cl, m in D.all('BD', src, any_accent=True) if cl.extra.get('config', 'tr909').startswith('tr909')]
        taus = [m.get('sweep_tau_ms') for cl, m in D.all('BD', src, any_accent=True) if cl.extra.get('config', 'tr909').startswith('tr909')]
        rows.append([src, len(vals), spread(vals, 2), spread(taus, 1)])
    md.append('\nBD end pitch in the sets without settings (median (min-max) over their BD clips).\n')
    md.append(table(['set', 'clips', 'f_end Hz', 'tau ms'], rows))

    # ---------------------------------------------------------------- SD
    md.append('\n## SD\n')
    rows, jt = [], {}
    for t in Q:
        cells = [D.get('SD', tune=t, tone=o, snappy=0) for o in Q]
        b = D.get('SD', tune=t, tone=0.5, snappy=0)
        early = g(b, 'peaks_early') or [[float('nan')]]
        rows.append([fm(t, 2), spread([g(c, 'f_low') for c in cells]), spread([g(c, 'f_high') for c in cells]),
                     fm(stat([g(c, 'f_high') for c in cells])[0] / stat([g(c, 'f_low') for c in cells])[0], 3),
                     fm(early[0][0]), fm(g(b, 't20')), fm(g(b, 't40')), fm(g(b, 'tail_tau_ms'))])
        jt[t] = {'f_low': stat([g(c, 'f_low') for c in cells])[0], 'f_high': stat([g(c, 'f_high') for c in cells])[0]}
    J['sd_tune'] = jt
    md.append('SD TUNE, drum part (AR; SNAPPY 0, over the 5 TONE settings: median (min-max)); lines from 30-150 ms, '
              '"early" the strongest line in 2-20 ms; decay of the TONE 0.5 take.\n')
    md.append(table(['TUNE', 'f_low Hz', 'f_high Hz', 'ratio', 'early Hz', 't-20', 't-40', 'tail tau'], rows))
    rows = []
    for o in Q:
        row = [fm(o, 2)]
        for s in Q[1:]:
            b = D.get('SD', tune=0.5, tone=o, snappy=s)
            row.append(f'{fm(g(b, "noise_t20"), 0)} / {fm(g(b, "noise_t40"), 0)}')
        b = D.get('SD', tune=0.5, tone=o, snappy=1)
        row += [fm(g(b, 'centroid_hz'), 0), fm(g(b, 't20'), 0)]
        rows.append(row)
    md.append('\nSD noise (above 2 kHz) t-20 / t-40 ms by TONE (rows) and SNAPPY (columns), TUNE 0.5 (AR); '
              'centroid 0-60 ms and the whole hit\'s t-20 at SNAPPY 1.\n')
    md.append(table(['TONE', 'SNAPPY 0.25', '0.5', '0.75', '1', 'centroid S1', 't-20 S1'], rows))
    rows = []
    for s in Q:
        b = D.get('SD', tune=0.5, tone=0.5, snappy=s)
        rows.append([fm(s, 2), fm(g(b, 'peak_db'), 2), fm(g(b, 'rms50_db'), 2), fm(g(b, 'noise_peak_db'), 2), fm(g(b, 'centroid_hz'), 0)])
    md.append('\nSD SNAPPY level (AR; TUNE 0.5, TONE 0.5): noise peak = the >2 kHz part\'s peak.\n')
    md.append(table(['SNAPPY', 'peak dBFS', 'rms 0-50 ms', 'noise peak dBFS', 'centroid'], rows))
    rows = []
    for t in (0, 0.3, 0.7, 1.0):
        b = D.get('SD', 'rr', tune=t, tone=0, snappy=0)
        rows.append([fm(t * 10, 0), fm(g(b, 'f_low')), fm(g(b, 'f_high')),
                     fm(g(b, 'f_high') / g(b, 'f_low') if b and g(b, 'f_low') else None, 3), fm(g(b, 't20')), fm(g(b, 't40'))])
    md.append('\nSD, Rob Roy unit (SNAPPY 0).\n')
    md.append(table(['TUNE dial', 'f_low', 'f_high', 'ratio', 't-20', 't-40'], rows))
    rows = []
    for o in (0, 0.3, 0.7, 1.0):
        b = D.get('SD', 'rr', tune=0.7, tone=o, snappy=1)
        rows.append([fm(o * 10, 0), fm(g(b, 'noise_t20'), 0), fm(g(b, 'noise_t40'), 0), fm(g(b, 'centroid_hz'), 0)])
    md.append('\nSD noise, Rob Roy (TUNE 7, SNAPPY 10).\n')
    md.append(table(['TONE dial', 'noise t-20', 'noise t-40', 'centroid'], rows))
    rows = []
    for t in Q:
        ro, ar = D.get('SD', 'roland', tune=t), D.get('SD', tune=t, tone=0.5, snappy=0.5)
        rows.append([fm(t, 2), fm(g(ar, 'f_low')), fm(g(ro, 'f_low')), fm(g(ar, 'f_high')), fm(g(ro, 'f_high')), fm(g(ar, 't20')), fm(g(ro, 't20'))])
    md.append('\nSD TUNE, AR against Roland (TONE 0.5, SNAPPY 0.5).\n')
    md.append(table(['TUNE', 'f_low AR', 'Roland', 'f_high AR', 'Roland', 't-20 AR', 'Roland'], rows))
    rows = []
    for o in Q:
        ro, ar = D.get('SD', 'roland', tone=o), D.get('SD', tune=0.5, tone=o, snappy=0.5)
        rows.append([fm(o, 2), fm(g(ar, 'noise_t20'), 0), fm(g(ro, 'noise_t20'), 0), fm(g(ar, 'noise_t40'), 0), fm(g(ro, 'noise_t40'), 0)])
    md.append('\nSD TONE, noise decay, AR against Roland (TUNE 0.5, SNAPPY 0.5).\n')
    md.append(table(['TONE', 'noise t-20 AR', 'Roland', 'noise t-40 AR', 'Roland'], rows))
    rows = []
    for s in Q:
        ro, ar = D.get('SD', 'roland', snappy=s), D.get('SD', tune=0.5, tone=0.5, snappy=s)
        lv = lambda m: (g(m, 'noise_peak_db') - g(m, 'peak_db')) if m else None
        rows.append([fm(s, 2), fm(lv(ar), 1), fm(lv(ro), 1), fm(g(ar, 'centroid_hz'), 0), fm(g(ro, 'centroid_hz'), 0)])
    md.append('\nSD SNAPPY, AR against Roland: noise peak re the hit\'s peak, dB; centroid 0-60 ms.\n')
    md.append(table(['SNAPPY', 'noise AR', 'Roland', 'centroid AR', 'Roland'], rows))

    # ---------------------------------------------------------------- toms
    md.append('\n## Toms\n')
    J['toms'] = {}
    for v in r.TOMS:
        rows, jt = [], {}
        for t in T11:
            cells = [D.get(v, tune=t, decay=d) for d in T11]
            b = D.get(v, tune=t, decay=1)
            rows.append([fm(t, 1), fm(g(b, 'f_main')), spread([g(c, 'f_main') for c in cells]), fm(g(b, 'f_low')),
                         fm(g(b, 'f_main') / g(b, 'f_low') if b and g(b, 'f_low') else None, 3), fm(g(b, 'f_early')),
                         fm(g(b, 'f_10ms')), fm(g(b, 'f_300ms')), fm(g(b, 'sweep_tau_ms'), 0)])
            jt[t] = {k: g(b, k) for k in ('f_main', 'f_low', 'f_early', 'f_300ms', 'sweep_f_start', 'sweep_f_end', 'sweep_tau_ms')}
            jt[t]['f_main_over_decay'] = stat([g(c, 'f_main') for c in cells])
        J['toms'][v] = {'tune': jt}
        md.append(f'\n{v} TUNE (AR; DECAY 1; f_main over the 11 DECAY settings: median (min-max)). f_main/f_low from 50-250 ms, '
                  f'early 3-25 ms, f 10 ms / 300 ms from the zero-crossing track around f_main; tau of its exponential fit.\n')
        md.append(table(['TUNE', 'f_main', 'f_main, 11 DECAY', 'f_low', 'ratio', 'early', 'f 10 ms', 'f 300 ms', 'tau ms'], rows))
        rows, jd = [], {}
        for d in T11:
            cells = [D.get(v, tune=t, decay=d) for t in T11]
            b = D.get(v, tune=0.5, decay=d)
            rows.append([fm(d, 1), fm(g(b, 't20'), 0), fm(g(b, 't40'), 0), fm(g(b, 't60'), 0), fm(g(b, 'tail_tau_ms'), 0),
                         spread([g(c, 't20') for c in cells], 0), spread([g(c, 't40') for c in cells], 0), fm(g(b, 'energy_db'), 2)])
            jd[d] = {k: g(b, k) for k in ('t20', 't40', 't60', 'tail_tau_ms', 'energy_db')}
        J['toms'][v]['decay'] = jd
        md.append(f'\n{v} DECAY (AR; TUNE 0.5; spread over the 11 TUNE settings).\n')
        md.append(table(['DECAY', 't-20', 't-40', 't-60', 'tail tau', 't-20, 11 TUNE', 't-40, 11 TUNE', 'energy dB'], rows))
        # knob law
        ts = [t for t in T11 if jt[t]['f_main']]
        fs = [jt[t]['f_main'] for t in ts]
        a, b, rms, mx = linfit(ts[:9], fs[:9])
        ae, be, rmse, mxe = linfit(ts[:9], np.log(fs[:9]))
        J['toms'][v]['law'] = {'lin_0_80': [a, b, rms, mx], 'exp_0_80': [math.exp(ae), be, rmse, mxe]}
        md.append(f'\n{v} f_main against TUNE 0-0.8: linear {a:.1f} + {b:.1f} k Hz (rms {rms:.2f}, max {mx:.2f} Hz); '
                  f'exponential {math.exp(ae):.1f} e^({be:.3f} k) (rms {rmse * 1200 / math.log(2):.0f} cents). '
                  f'0.9 -> 1.0: {fs[-2]:.1f} -> {fs[-1]:.1f} Hz.\n')
    rows = []
    for v in r.TOMS:
        for t in (0, 0.3, 0.7, 1.0):
            b = D.get(v, 'rr', tune=t, decay=1)
            ar = D.get(v, tune=t, decay=1)
            rows.append([v, fm(t * 10, 0), fm(g(b, 'f_main')), fm(g(ar, 'f_main')), fm(cents(g(b, 'f_main'), g(ar, 'f_main')), 0),
                         fm(g(b, 'f_low')), fm(g(ar, 'f_low')), fm(g(b, 'f_main') / g(b, 'f_low') if b and g(b, 'f_low') else None, 3)])
    md.append('\nToms, Rob Roy unit against AR (DECAY 10 / 1.0; dial n against AR TUNE n/10).\n')
    md.append(table(['tom', 'TUNE', 'f_main RR', 'AR', 'RR re AR cents', 'f_low RR', 'AR', 'ratio RR'], rows))
    rows = []
    for v in r.TOMS:
        for d in (0, 0.3, 0.7, 1.0):
            b = D.get(v, 'rr', tune=0.7, decay=d)
            ar = D.get(v, tune=0.7, decay=d)
            rows.append([v, fm(d * 10, 0), fm(g(b, 't20'), 0), fm(g(ar, 't20'), 0), fm(g(b, 't40'), 0), fm(g(ar, 't40'), 0)])
    md.append('\nTom DECAY, Rob Roy against AR (TUNE 7 / 0.7).\n')
    md.append(table(['tom', 'DECAY', 't-20 RR', 'AR', 't-40 RR', 'AR'], rows))
    rows = []
    for v in r.TOMS:
        for t in Q:
            ro, ar = D.get(v, 'roland', tune=t), D.get(v, tune=t, decay=0.5)
            rows.append([v, fm(t, 2), fm(g(ar, 'f_main')), fm(g(ro, 'f_main')), fm(cents(g(ro, 'f_main'), g(ar, 'f_main')), 0),
                         fm(g(ar, 'f_low')), fm(g(ro, 'f_low')), fm(g(ar, 'f_early')), fm(g(ro, 'f_early'))])
    md.append('\nTom TUNE, AR against Roland (DECAY 0.5).\n')
    md.append(table(['tom', 'TUNE', 'f_main AR', 'Roland', 'Roland re AR cents', 'f_low AR', 'Roland', 'early AR', 'Roland'], rows))
    rows = []
    for v in r.TOMS:
        for d in Q:
            ro, ar = D.get(v, 'roland', decay=d), D.get(v, tune=0.5, decay=d)
            rows.append([v, fm(d, 2), fm(g(ar, 't20'), 0), fm(g(ro, 't20'), 0), fm(g(ar, 't40'), 0), fm(g(ro, 't40'), 0), fm(g(ar, 't60'), 0), fm(g(ro, 't60'), 0)])
    md.append('\nTom DECAY, AR against Roland (TUNE 0.5).\n')
    md.append(table(['tom', 'DECAY', 't-20 AR', 'Roland', 't-40 AR', 'Roland', 't-60 AR', 'Roland'], rows))
    rows = []
    for v in r.TOMS:
        for src in ('kb6', 'regebro'):
            ms = [m for cl, m in D.all(v, src, any_accent=True) if cl.extra.get('config', 'tr909').startswith('tr909')]
            rat = [m['f_main'] / m['f_low'] for m in ms if m.get('f_low') and m.get('f_low') == m.get('f_low')]
            rows.append([v, src, len(ms), spread([m.get('f_main') for m in ms]), spread(rat, 3)])
    md.append('\nToms in the sets without settings (f_main and the VCO-1/VCO-2 ratio).\n')
    md.append(table(['tom', 'set', 'clips', 'f_main', 'ratio'], rows))

    # ---------------------------------------------------------------- RS, HC
    md.append('\n## RS and HC\n')
    rows = []
    shots = D.all('RS', shot=None) if False else [(cl, m) for cl, m in D.all('RS', any_accent=True) if 'shot' in cl.extra and cl.accent is None]
    for cl, m in sorted(shots, key=lambda z: z[0].extra['shot']):
        p = m.get('peaks_hz') or []
        rows.append([cl.extra['shot'], fm(m['peak_db'], 2), fm(m['rms50_db'], 2), fm(m['t20'], 1), fm(m['t40'], 1),
                     ', '.join(f'{f:.0f} ({l:+.1f})' for f, l in p)])
    md.append('RS, AR\'s ten repeated shots (no accent value; peaks in the first 30 ms).\n')
    md.append(table(['shot', 'peak dBFS', 'rms 0-50', 't-20', 't-40', 'peaks Hz (dB)'], rows))
    rows = []
    for src, sel in (('rr', {}), ('regebro', {'config': 'tr909_individual_out'}), ('roland', {})):
        for cl, m in D.all('RS', src, any_accent=True, **sel):
            if src == 'roland' and not (cl.extra.get('default')):
                continue
            p = m.get('peaks_hz') or []
            rows.append([src, cl.accent if cl.accent is not None else cl.extra, fm(m['t20'], 1), fm(m['t40'], 1),
                         ', '.join(f'{f:.0f} ({l:+.1f})' for f, l in p)])
    md.append('\nRS, other units (Rob Roy at MIDI velocity 63/127; Regebro\'s 4 accent states; Roland default at vel 127).\n')
    md.append(table(['set', 'accent', 't-20', 't-40', 'peaks Hz (dB)'], rows))
    rows = []
    shots = [(cl, m) for cl, m in D.all('HC', any_accent=True) if 'shot' in cl.extra and cl.accent is None]
    for cl, m in sorted(shots, key=lambda z: z[0].extra['shot']):
        rows.append([cl.extra['shot'], fm(m['peak_db'], 2), fm(m['rms50_db'], 2), fm(m.get('bursts_ms')), fm(m['t20'], 0),
                     fm(m['t40'], 0), fm(m['t60'], 0), fm(m['centroid_hz'], 0)])
    for src, sel in (('rr', {}), ('regebro', {'config': 'tr909_individual_out'}), ('roland', {})):
        for cl, m in D.all('HC', src, any_accent=True, **sel):
            if src == 'roland' and not cl.extra.get('default'):
                continue
            rows.append([f'{src} {cl.accent if cl.accent is not None else ""}', fm(m['peak_db'], 2), fm(m['rms50_db'], 2),
                         fm(m.get('bursts_ms')), fm(m['t20'], 0), fm(m['t40'], 0), fm(m['t60'], 0), fm(m['centroid_hz'], 0)])
    md.append('\nHC: AR\'s eight shots, then the other units. bursts = 1 ms envelope maxima standing 10 dB clear, ms from the onset.\n')
    md.append(table(['shot', 'peak dBFS', 'rms 0-50', 'bursts ms', 't-20', 't-40', 't-60', 'centroid'], rows))

    # ---------------------------------------------------------------- hats and cymbals
    md.append('\n## Hi-hats and cymbals\n')
    rows = []
    for d in Q:
        b = D.get('CH', decay=d)
        rows.append(['AR', fm(d, 2), fm(g(b, 't20'), 0), fm(g(b, 't40'), 0), fm(g(b, 't60'), 0), fm(g(b, 'tail_tau_ms'), 1),
                     fm(g(b, 'rom_rate_hz'), 0), fm(g(b, 'rom_ncc'), 2)])
    for d in (0, 0.2, 0.4, 0.6, 0.8, 1.0):
        b = D.get('CH', 'rr', decay=d)
        rows.append(['RR', fm(d * 10, 0), fm(g(b, 't20'), 0), fm(g(b, 't40'), 0), fm(g(b, 't60'), 0), fm(g(b, 'tail_tau_ms'), 1),
                     fm(g(b, 'rom_rate_hz'), 0), fm(g(b, 'rom_ncc'), 2)])
    for d in T11:
        b = D.get('CH', 'roland', decay=d)
        rows.append(['Roland', fm(d, 1), fm(g(b, 't20'), 0), fm(g(b, 't40'), 0), fm(g(b, 't60'), 0), fm(g(b, 'tail_tau_ms'), 1),
                     fm(g(b, 'rom_rate_hz'), 0), fm(g(b, 'rom_ncc'), 2)])
    md.append('CH DECAY (AR with OH DECAY unknown; RR dial; Roland at OH DECAY 0.5). ROM clock from the 0x6000 region.\n')
    md.append(table(['set', 'DECAY', 't-20', 't-40', 't-60', 'tail tau', 'clock Hz', 'NCC'], rows))
    rows = []
    for d in Q:
        b = D.get('OH', decay=d)
        rows.append(['AR (44.1k)', fm(d, 2), fm(g(b, 't20'), 0), fm(g(b, 't40'), 0), fm(g(b, 't60'), 0), fm(g(b, 'tail_tau_ms'), 1),
                     fm(g(b, 'rom_rate_hz'), 0), fm(g(b, 'rom_ncc'), 2)])
    b = D.get('OH', accent=1.0, decay=1)
    rows.append(['AR acc 1.0 (96k)', '1.00', fm(g(b, 't20'), 0), fm(g(b, 't40'), 0), fm(g(b, 't60'), 0), fm(g(b, 'tail_tau_ms'), 1),
                 fm(g(b, 'rom_rate_hz'), 0), fm(g(b, 'rom_ncc'), 2)])
    for d in (0, 0.2, 0.4, 0.6, 0.8, 1.0):
        b = D.get('OH', 'rr', decay=d)
        rows.append(['RR', fm(d * 10, 0), fm(g(b, 't20'), 0), fm(g(b, 't40'), 0), fm(g(b, 't60'), 0), fm(g(b, 'tail_tau_ms'), 1),
                     fm(g(b, 'rom_rate_hz'), 0), fm(g(b, 'rom_ncc'), 2)])
    for d in T11:
        b = D.get('OH', 'roland', decay=d)
        rows.append(['Roland', fm(d, 1), fm(g(b, 't20'), 0), fm(g(b, 't40'), 0), fm(g(b, 't60'), 0), fm(g(b, 'tail_tau_ms'), 1),
                     fm(g(b, 'rom_rate_hz'), 0), fm(g(b, 'rom_ncc'), 2)])
    md.append('\nOH DECAY. ROM clock from the 0x0000 region.\n')
    md.append(table(['set', 'DECAY', 't-20', 't-40', 't-60', 'tail tau', 'clock Hz', 'NCC'], rows))
    rows = []
    for out in ('individual_out', 'mix_out'):
        for bar in range(0, 18):
            oh = [m for cl, m in D.all('OH', 'scherer', any_accent=True, out=out, bar=bar)]
            ch = [m for cl, m in D.all('CH', 'scherer', any_accent=True, out=out, bar=bar, accent_inferred='on')]
            if not oh and not ch:
                continue
            o, c = (oh or [None])[0], (ch or [None])[0]
            rows.append([out.replace('_out', ''), bar, fm(g(o, 't20'), 0), fm(g(o, 't40'), 0), fm(g(o, 'tail_tau_ms'), 1),
                         fm(g(c, 't20'), 0), fm(g(c, 't40'), 0), fm(g(c, 'tail_tau_ms'), 1), fm(g(o, 'rom_rate_hz'), 0), fm(g(c, 'rom_rate_hz'), 0)])
    md.append('\nHats, scherer unit: CH and OH DECAY swept together, bar 1 = minimum (CH = the accented step-7 hit).\n')
    md.append(table(['out', 'bar', 'OH t-20', 'OH t-40', 'OH tail tau', 'CH t-20', 'CH t-40', 'CH tail tau', 'OH clock', 'CH clock'], rows))
    for v in ('CR', 'RD'):
        rows, jt = [], {}
        for t in T11:
            b = D.get(v, tune=t)
            rows.append(['AR', fm(t, 1), fm(g(b, 'rom_rate_hz'), 0), fm(g(b, 'rom_ncc'), 2), fm(g(b, 't20'), 0), fm(g(b, 't40'), 0),
                         fm(g(b, 'centroid_full_hz'), 0)])
            jt[t] = {k: g(b, k) for k in ('rom_rate_hz', 'rom_ncc', 't20', 't40')}
        J[v.lower() + '_tune'] = jt
        for t in (0, 0.2, 0.4, 0.6, 0.8, 1.0):
            b = D.get(v, 'rr', tune=t)
            rows.append(['RR', fm(t * 10, 0), fm(g(b, 'rom_rate_hz'), 0), fm(g(b, 'rom_ncc'), 2), fm(g(b, 't20'), 0), fm(g(b, 't40'), 0),
                         fm(g(b, 'centroid_full_hz'), 0)])
        for t in Q:
            b = D.get(v, 'roland', tune=t)
            rows.append(['Roland', fm(t, 2), fm(g(b, 'rom_rate_hz'), 0), fm(g(b, 'rom_ncc'), 2), fm(g(b, 't20'), 0), fm(g(b, 't40'), 0),
                         fm(g(b, 'centroid_full_hz'), 0)])
        md.append(f'\n{v} TUNE: ROM clock, decay, centroid (onset to -40 dB).\n')
        md.append(table(['set', 'TUNE', 'clock Hz', 'NCC', 't-20', 't-40', 'centroid'], rows))

    # ---------------------------------------------------------------- accent
    md.append('\n## Accent\n')
    rows, ja = [], {}
    for v in r.VOICES:
        acc = sorted([(cl, m) for cl, m in D.all(v, any_accent=True) if isinstance(cl.accent, float)], key=lambda z: z[0].accent)
        if not acc:
            continue
        top = acc[-1][1]['rms50_db']
        lv = [m['rms50_db'] - top for _, m in acc]
        knobs = acc[0][0].knobs
        grid = [m for cl, m in D.all(v, **knobs) if cl.accent is None and not cl.extra]
        gl = grid[0]['rms50_db'] - top if grid else None
        rows.append([v] + [fm(z, 1) for z in lv] + [fm(gl, 1)])
        ja[v] = {'levels': lv, 'grid_re_acc100': gl}
    J['accent_ar'] = ja
    md.append('AR "accentNNN" takes, rms 0-50 ms re accent100, dB; last column the plain grid take at the same knobs.\n')
    md.append(table(['voice'] + [f'{a}' for a in range(0, 101, 10)] + ['grid'], rows))
    rows, jr = [], {}
    for cfg in ('tr909_individual_out', 'tr909_main_out'):
        for v in r.VOICES:
            ms = {cl.accent: m for cl, m in D.all(v, 'regebro', any_accent=True, config=cfg)}
            if 1 not in ms:
                continue
            base = ms[1]['rms50_db']
            rows.append([cfg.replace('tr909_', ''), v] + [fm(ms[k]['rms50_db'] - base, 1) if k in ms else '-' for k in (2, 3, 4)]
                        + [fm(ms[1]['peak_db'], 1)])
            jr.setdefault(cfg, {})[v] = [ms[k]['rms50_db'] - base if k in ms else None for k in (2, 3, 4)]
    J['accent_regebro'] = jr
    md.append('\nRegebro\'s four takes, rms 0-50 ms re take 1, dB (inferred order: 2 TOTAL only, 3 local only, 4 both).\n')
    md.append(table(['out', 'voice', 'take 2', 'take 3', 'take 4', 'take 1 peak dBFS'], rows))

    # ---------------------------------------------------------------- levels
    md.append('\n## Levels at LEVEL 50 % (AR)\n')
    rows = []
    for v, k in (('BD', dict(tune=0.5, attack=0.5, decay=0.5)), ('SD', dict(tune=0.5, tone=0.5, snappy=0.5)),
                 ('LT', dict(tune=0.5, decay=0.5)), ('MT', dict(tune=0.5, decay=0.5)), ('HT', dict(tune=0.5, decay=0.5)),
                 ('RS', dict(shot=1)), ('HC', dict(shot=1)), ('CH', dict(decay=0.5)), ('OH', dict(decay=0.5)),
                 ('CR', dict(tune=0.5)), ('RD', dict(tune=0.5))):
        b = D.get(v, **k)
        rows.append([v, ' '.join(f'{a}={c:g}' for a, c in k.items()), fm(g(b, 'peak_db'), 1), fm(g(b, 'rms50_db'), 1), fm(g(b, 'energy_db'), 1)])
    md.append(table(['voice', 'knobs', 'peak dBFS', 'rms 0-50 ms', 'energy dB'], rows))
    return '\n'.join(md), J


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--fresh', action='store_true')
    a = ap.parse_args()
    cache = measure_all(a.jobs, a.fresh)
    errs = {k: v['error'] for k, v in cache.items() if 'error' in v}
    for k, v in list(errs.items())[:20]:
        print('error', k, v, file=sys.stderr)
    md, J = tables(cache)
    open(os.path.join(OUT, 'tables.md'), 'w').write('# TR-909 reference measurements (generated by tools/compare/reference.py)\n\n' + md)
    json.dump({'tables': J, 'clips': {os.path.relpath(k, r.REAL909) if k.startswith(r.REAL909) else k: v for k, v in cache.items()}},
              open(os.path.join(OUT, 'measurements.json'), 'w'), indent=1, default=float)
    print(os.path.join(OUT, 'tables.md'))


if __name__ == '__main__':
    main()
