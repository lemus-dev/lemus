#!/usr/bin/env python3
"""The toms against real TR-909s: each recorded setting played through tr909voicerun (the voicing
board as the plugin runs it, tom noise and all) at the same knobs and accent code, and both
measured by real909.py's measures (docs/reference.md).

  AudioRealism (ar): one late unit, 96 kHz, individual outs, LEVEL 50 %; TUNE x DECAY 0-1 in
    0.1 steps per tom (11 x 11), played as accented steps at TOTAL ACCENT 0 (DAC code 21,
    real909.ar_code); its 11 "accent" takes (TUNE 0.5, DECAY 1) a TOTAL ACCENT sweep, codes 21-47.
  Rob Roy (rr): a second unit, dial 0/3/7/10 (rotation 0, 0.3, 0.7, 1) on TUNE and DECAY, LEVEL
    at max, normalised (no level), 44.1 kHz; played at code 21 like AR's (its accent isn't stated).

Measures (real909.measure; model against unit): f_main and f_low (the two lowest lines, VCO-1 and
VCO-2, 50-250 ms), f_early (3-25 ms: the pitch envelope's start), the ratio f_main / f_low, t-20
and t-40 (10 ms RMS, last time above), the level (RMS 0-50 ms; one offset for the recording chain
over all of AR's files), the LSD (30-330 ms, 1/6 octave) as rendered and with the model's
frequencies scaled onto the unit's f_main, and the attack's noise (above 1.5 kHz in 0-10 ms
against 0-50 ms: the tom noise's burst through VCO3's VCA). The model's render goes through the
recording chain first (real909.chain: AR's 6.5 Hz first-order high-pass).

Calibration: --cal passes tr909voicerun's --cal (lt.c18=21e-9, law.ltTune=0.08,0,0.92,1, ...);
--nominal plays the service notes' unit (--cal nominal=1 first), else the board's
unitCalibration() plus the --cal values. --grid picks the AR settings: full (121 a tom), fit
(TUNE and DECAY both in 0, 0.2 ... 1: 36), hold (both in 0.1, 0.3 ... 0.9: 25), cross (the TUNE
sweep at DECAY 0.5, the DECAY sweep at TUNE 0.5, the corners: 25), coarse (0, 0.5, 1: 9).

  toms.py [--toms lt,mt,ht] [--set ar,accent,rr] [--grid full|fit|hold|cross|coarse] [--nominal]
          [--cal NAME=VALUE]... [--jobs 2] [--voicerun PATH] [--cache DIR] [--json OUT]
"""
import argparse, concurrent.futures as cf, hashlib, json, math, os, subprocess, sys, tempfile

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
REPO = os.path.normpath(os.path.join(MACHINE, '..', '..'))
NET = os.path.join(MACHINE, 'core', 'netlists')
VOICERUN = os.environ.get('VOICERUN', os.path.join(REPO, 'build', 'tr909', 'tr909voicerun'))
sys.path.insert(0, HERE)
import real909 as R  # noqa: E402

RATE = 96000
T_HIT = 0.05
JACK = {'lt': 2, 'mt': 3, 'ht': 4}  # tr909voicerun --jacks: BD SD LT MT HT RS HC HH CR RD
TOMS = ('lt', 'mt', 'ht')


def netlist_text(f):
    """A netlist without its comments: a render is redone only when the circuit changes."""
    return '\n'.join(l for l in open(os.path.join(NET, f)).read().splitlines() if l.strip() and not l.lstrip().startswith('*'))


def render(tom, tune, decay, level, code, seconds, cals, nominal, voicerun, cache):
    """The tom's jack (volts, 96 kHz) as tr909voicerun plays it, the hit at T_HIT."""
    args = ['--rate', str(RATE), '--seconds', '%.3f' % (T_HIT + seconds), '--hit', '%g:%s:%d' % (T_HIT, tom, code),
            '--set', '%sTune=%g' % (tom, tune), '--set', '%sDecay=%g' % (tom, decay), '--set', '%sLevel=%g' % (tom, level)]
    cal = (['--cal', 'nominal=1'] if nominal else []) + [a for c in cals for a in ('--cal', c)]
    key = hashlib.sha1(json.dumps([args, cal, [netlist_text(f) for f in ('models.cir', tom + '.cir', 'tomnoise.cir')],
                                   os.path.getmtime(voicerun)]).encode()).hexdigest()[:20]
    path = os.path.join(cache, '%s-%s-full.npy' % (tom, key))
    if os.path.exists(path):
        return np.load(path)
    with tempfile.TemporaryDirectory() as tmp:
        jacks = os.path.join(tmp, 'j.wav')
        env = dict(os.environ, TR909_NETLISTS=NET)  # the netlists as they are on disk, not as built
        r = subprocess.run([voicerun, os.path.join(tmp, 'm.wav')] + args + cal + ['--jacks', jacks], capture_output=True, text=True,
                           env=env)
        if r.returncode:
            raise RuntimeError('tr909voicerun failed: ' + r.stderr[-2000:])
        x, sr = sf.read(jacks, always_2d=True)
    y = x[:, JACK[tom]]
    os.makedirs(cache, exist_ok=True)
    np.save(path, y)
    return y


def attack_hf(x, sr):
    """The energy above 1.5 kHz in the first 10 ms against the first 50 ms, dB."""
    h = R.highpass(x, sr, 1500)
    return R.energy_db(h, sr, 0, 0.01) - R.energy_db(x, sr, 0, 0.05)


def lsd_aligned(ref, sr_r, mod, sr_m, scale):
    """LSD with the model's frequencies scaled by `scale` (resampled: its lines onto the unit's)."""
    if not (scale == scale) or abs(scale - 1) < 1e-4:
        return R.lsd(ref, sr_r, mod, sr_m)
    p = int(round(scale * 2000))
    return R.lsd(ref, sr_r, resample_poly(mod, 2000, p), sr_m)


def measure(x, sr, voice):
    m = R.measure(x, sr, voice)
    m['ratio'] = m['f_main'] / m['f_low'] if m.get('f_low') == m.get('f_low') else float('nan')
    m['attack_hf'] = attack_hf(x, sr)
    return m


def items_for(tom, sets, grid):
    v = tom.upper()
    out = []
    if 'ar' in sets:
        for c in R.find(v, 'ar'):
            t, d = round(c.knobs['tune'], 2), round(c.knobs['decay'], 2)
            ti, di = int(round(t * 10)), int(round(d * 10))
            if grid == 'fit' and (ti % 2 or di % 2):
                continue
            if grid == 'hold' and not (ti % 2 and di % 2):
                continue
            if grid == 'cross' and not (di == 5 or ti == 5 or (ti % 10 == 0 and di % 10 == 0)):
                continue
            if grid == 'coarse' and (ti % 5 or di % 5):
                continue
            out.append(dict(set='ar', tom=tom, clip=c, tune=t, decay=d, level=0.5, code=R.ar_code(v)))
    if 'accent' in sets:
        for c in R.find(v, 'ar', any_accent=True):
            if c.accent is None or c.extra:
                continue
            out.append(dict(set='accent', tom=tom, clip=c, tune=0.5, decay=1.0, level=0.5, code=R.ar_code(v, c.accent),
                            accent=c.accent))
    if 'rr' in sets:
        for c in R.find(v, 'rr'):
            out.append(dict(set='rr', tom=tom, clip=c, tune=round(c.knobs['tune'], 2), decay=round(c.knobs['decay'], 2),
                            level=1.0, code=R.ar_code(v)))
    return out


def work(it, a):
    x, sr = R.load(it['clip'])
    ref, _ = R.trim(x, sr)
    seconds = min(len(ref) / sr, 1.6) + 0.05
    y = render(it['tom'], it['tune'], it['decay'], it['level'], it['code'], seconds, a.cal, a.nominal, a.voicerun, a.cache)
    # the recording chain (AR's 6.5 Hz high-pass: the MULTI OUT is DC-coupled) on the model's side,
    # from its resting level before the hit
    src = 'rr' if it['set'] == 'rr' else 'ar'
    y = R.chain(y - np.mean(y[int(0.01 * RATE):int((T_HIT - 0.005) * RATE)]), RATE, src)[int((T_HIT - 0.002) * RATE):]
    if sr != RATE:
        g = math.gcd(int(sr), RATE)
        y = resample_poly(y, int(sr) // g, RATE // g)
    mod, _ = R.trim(y, sr)
    n = min(len(ref), len(mod))
    ref, mod = ref[:n], mod[:n]
    v = it['tom'].upper()
    mr, mm = measure(ref, sr, v), measure(mod, sr, v)
    scale = mr['f_main'] / mm['f_main'] if mr['f_main'] == mr['f_main'] and mm['f_main'] == mm['f_main'] else float('nan')
    keys = ('f_main', 'f_low', 'f_early', 'f_300ms', 'ratio', 't20', 't40', 'tail_tau_ms', 'rms50_db', 'attack_hf')
    out = {k: it[k] for k in ('set', 'tom', 'tune', 'decay', 'level', 'code')}
    out['accent'] = it.get('accent')
    out['file'] = os.path.basename(it['clip'].path)
    out['ref'] = {k: float(mr.get(k, float('nan'))) for k in keys}
    out['mod'] = {k: float(mm.get(k, float('nan'))) for k in keys}
    out['lsd'] = R.lsd(ref, sr, mod, sr)
    out['lsd_aligned'] = lsd_aligned(ref, sr, mod, sr, scale)
    return out


def cents(a, b):
    return 1200 * math.log2(b / a) if a > 0 and b > 0 else float('nan')


def octaves(a, b):
    return math.log2(b / a) if a > 0 and b > 0 else float('nan')


def med(v):
    v = [x for x in v if x == x]
    return float(np.median(v)) if v else float('nan')


def summarise(rows, offset=None):
    """Medians per set and tom; the level offset is the median over AR's grid (or `offset`)."""
    ar = [r for r in rows if r['set'] == 'ar']
    if offset is None:
        offset = med([r['mod']['rms50_db'] - r['ref']['rms50_db'] for r in ar]) if ar else 0.0
    res = {'offset': offset}
    for st in ('ar', 'rr'):
        rs = [r for r in rows if r['set'] == st]
        if not rs:
            continue
        print('\n%s: model against the unit, medians (pitch cents, decays octaves, level dB after the chain offset %.1f dB)'
              % (st, offset))
        print('tom   n  f_main  f_low  f_early  ratio(u/m)    t-20   t-40  level (sd)    lsd (aligned)  attack')
        for tom in TOMS:
            t = [r for r in rs if r['tom'] == tom]
            if not t:
                continue
            d = dict(n=len(t),
                     f_main=med([cents(r['ref']['f_main'], r['mod']['f_main']) for r in t]),
                     f_low=med([cents(r['ref']['f_low'], r['mod']['f_low']) for r in t]),
                     f_early=med([cents(r['ref']['f_early'], r['mod']['f_early']) for r in t]),
                     ratio_ref=med([r['ref']['ratio'] for r in t]), ratio_mod=med([r['mod']['ratio'] for r in t]),
                     t20=med([octaves(r['ref']['t20'], r['mod']['t20']) for r in t]),
                     t40=med([octaves(r['ref']['t40'], r['mod']['t40']) for r in t]),
                     lsd=med([r['lsd'] for r in t]), lsd_aligned=med([r['lsd_aligned'] for r in t]),
                     attack=med([r['mod']['attack_hf'] - r['ref']['attack_hf'] for r in t]),
                     pitch_rms=float(np.sqrt(np.nanmean([cents(r['ref']['f_main'], r['mod']['f_main']) ** 2 for r in t]))))
            if st == 'ar':
                lv = [r['mod']['rms50_db'] - r['ref']['rms50_db'] - offset for r in t]
                d['level'], d['level_sd'] = med(lv), float(np.nanstd(lv))
            else:
                d['level'] = d['level_sd'] = float('nan')
            res[(st, tom)] = d
            print('%-4s %3d  %+5.0f  %+5.0f   %+5.0f  %.3f/%.3f  %+5.2f  %+5.2f  %+5.1f (%.1f)  %5.2f (%5.2f)  %+5.1f   (f_main rms %.0f c)' % (
                tom, d['n'], d['f_main'], d['f_low'], d['f_early'], d['ratio_ref'], d['ratio_mod'], d['t20'], d['t40'],
                d['level'], d['level_sd'], d['lsd'], d['lsd_aligned'], d['attack'], d['pitch_rms']))
    t = [r for r in rows if r['set'] == 'ar' and abs(r['decay'] - 0.5) < 1e-6]
    if t:
        print('\nTUNE (AR, DECAY 0.5): f_main, unit / model Hz')
        for tom in TOMS:
            s = sorted([r for r in t if r['tom'] == tom], key=lambda r: r['tune'])
            if s:
                print('%-4s ' % tom + '  '.join('%.1f %5.1f/%5.1f' % (r['tune'], r['ref']['f_main'], r['mod']['f_main']) for r in s))
    t = [r for r in rows if r['set'] == 'ar' and abs(r['tune'] - 0.5) < 1e-6]
    if t:
        print('\nDECAY (AR, TUNE 0.5): t-20 and t-40, unit / model ms')
        for tom in TOMS:
            s = sorted([r for r in t if r['tom'] == tom], key=lambda r: r['decay'])
            if s:
                print('%-4s ' % tom + '  '.join('%.1f %3.0f/%3.0f %3.0f/%3.0f' % (r['decay'], r['ref']['t20'], r['mod']['t20'],
                                                                          r['ref']['t40'], r['mod']['t40']) for r in s))
    t = [r for r in rows if r['set'] == 'accent']
    if t:
        print('\nAccent (AR takes, TUNE 0.5 DECAY 1): level over the 0 % take, unit / model dB')
        for tom in TOMS:
            s = sorted([r for r in t if r['tom'] == tom], key=lambda r: r['accent'])
            if s:
                b = s[0]
                print('%-4s ' % tom + '  '.join('%3.0f%% %+4.1f/%+4.1f' % (100 * r['accent'], r['ref']['rms50_db'] - b['ref']['rms50_db'],
                                                                        r['mod']['rms50_db'] - b['mod']['rms50_db']) for r in s))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--toms', default='lt,mt,ht')
    ap.add_argument('--set', default='ar,accent,rr')
    ap.add_argument('--grid', default='full', choices=['full', 'fit', 'hold', 'cross', 'coarse'])
    ap.add_argument('--nominal', action='store_true')
    ap.add_argument('--cal', action='append', default=[])
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--voicerun', default=VOICERUN)
    ap.add_argument('--cache', default=os.path.join(REPO, 'build', 'tr909', 'toms-renders'))
    ap.add_argument('--json')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    items = [it for tom in a.toms.split(',') for it in items_for(tom, a.set.split(','), a.grid)]
    rows = []
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        for row in ex.map(lambda it: work(it, a), items):
            rows.append(row)
            if not a.quiet:
                f, m = row['ref'], row['mod']
                print('%-6s %s T%.1f D%.1f c%2d  f_main %6.1f/%6.1f  f_low %6.1f/%6.1f  early %6.1f/%6.1f  t-20 %4.0f/%4.0f  '
                      't-40 %4.0f/%4.0f  rms %6.1f/%6.1f  lsd %4.2f (%4.2f)' % (
                          row['set'], row['tom'], row['tune'], row['decay'], row['code'], f['f_main'], m['f_main'], f['f_low'],
                          m['f_low'], f['f_early'], m['f_early'], f['t20'], m['t20'], f['t40'], m['t40'], f['rms50_db'],
                          m['rms50_db'], row['lsd'], row['lsd_aligned']), flush=True)
    summarise(rows)
    if a.json:
        json.dump(rows, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
