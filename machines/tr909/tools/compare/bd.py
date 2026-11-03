#!/usr/bin/env python3
"""The bass drum (core/netlists/bd.cir) against real TR-909s (private/reference/real909;
docs/circuit/bd.md, "Against the real unit" and "Calibration"). Hits rendered through
tr909voicerun, the board as the plugin plays it (96 kHz, a 2 ms trigger, the noise generator
running), taken at the jack (R500 1k into C500 0.01u and a high-impedance input: a 15.9 kHz pole)
and measured with real909.measure, the same way as the recordings, both from the trigger (the
recordings' found from their click, below); for AudioRealism the render goes through its recording
chain first (real909.chain: a 6.5 Hz first-order high-pass):

  pitch     zero-crossing track low-passed at 1.5 kHz: the first period (f_first), the pitch at 10,
            50 and 100 ms, and f(t) = f_end + (f_start - f_end) e^(-t/tau) fitted over 0-350 ms
  decay     the 20 ms RMS envelope: hold (first fall 3 dB under its peak), t20 / t40 (last time
            above -20 / -40 dB), the tail's time constant (-10 to -40 dB)
  attack    click_db (energy above 1 kHz in the first 5 ms re the whole hit), and the click's
            own level: the 1-8 kHz band's RMS over 1.5-4.5 ms re the body's RMS over 10-40 ms
  level     energy of the hit (dB); AudioRealism has one interface gain and LEVEL at 50 % on every
            file, so levels between cells compare after one common offset (the median)
  body      the 400 Hz low-passed body's positive over negative peak (30-90 ms) and its 2nd and 3rd
            harmonics re the fundamental (40-140 ms, TUNE 0 and 0.25, where the pitch has settled)
  spectrum  log-spectral distance 30-330 ms (1/6 octave; on the BD's tail mostly a pitch measure)
  accent    AudioRealism's accent000-100 takes: RMS over 0-50 ms and energy re accent000

Units: AudioRealism (2025, 96 kHz/24-bit, individual out, LEVEL 50 %): the TUNE x ATTACK x DECAY
grid at 0/25/50/75/100 % (125 files) and the 11 accent files (TUNE, ATTACK, DECAY 100 %); Rob Roy
(1995, 44.1 kHz, normalised, trimmed and inverted: shape only), dial 0/3/7/10, ATTACK 0 or 10.
The codes are real909.ar_code's: the grid at DAC code 21 (accented steps at TOTAL ACCENT 0), the
accent takes 21 to 47 (docs/reference.md, Accent); Rob Roy's at 21 too (unknown; its levels are
normalised away).

  bd.py [--out DIR] [--voicerun PATH] [--jobs N] [--unit] [--cal K=V ...] [--netlists DIR]
        [--cells grid|quick] [--units ar,acc,rr] [--plots] [--json FILE]

Without --unit the board runs the service notes' parts and straight knobs (--cal nominal=1);
--unit takes unitCalibration(). --cal passes voicerun's --cal (bd.c8=0.47u, law.bdTune=0,0.09,...)
after that. --netlists reads the netlists from DIR (TR909_NETLISTS; default core/netlists, so an
edited netlist needs no rebuild). --cells quick does the TUNE, DECAY and ATTACK rows through the
grid's centre only. Renders are cached in DIR/cache by their settings and the netlists' text.
"""
import argparse
import concurrent.futures as cf
import hashlib
import json
import math
import os
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import real909 as R  # noqa: E402

MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
REPO = os.path.normpath(os.path.join(MACHINE, '..', '..'))
NET = os.path.join(MACHINE, 'core', 'netlists')
RATE = 96000
T_HIT = 0.3  # the hit's time in the renders (after the board's own settling)
KNOBS = (0.0, 0.25, 0.5, 0.75, 1.0)
NAMES = {'tune': 'bdTune', 'attack': 'bdAttack', 'decay': 'bdDecay', 'level': 'bdLevel'}


class Renderer:
    def __init__(self, voicerun, cache, cal, unit, net=NET):
        self.vr = os.path.abspath(voicerun)
        self.cache = cache
        self.cal = list(cal)
        self.unit = unit
        self.net = os.path.abspath(net)
        os.makedirs(cache, exist_ok=True)

    def key(self, knobs, c, seconds):
        s = json.dumps([knobs, c, seconds, self.cal, self.unit, os.path.getmtime(self.vr)], sort_keys=True)
        for n in ('bd.cir', 'models.cir'):
            p = os.path.join(self.net, n)
            s += open(p).read() if os.path.exists(p) else ''
        return os.path.join(self.cache, hashlib.sha1(s.encode()).hexdigest()[:16] + '.npy')

    def render(self, knobs, c, seconds=1.3):
        """The hit at the jack from 1 ms before the trigger: knobs {tune, attack, decay, level}
        (rotations), DAC code c."""
        path = self.key(knobs, c, seconds)
        if os.path.exists(path):
            return np.load(path)
        tag = path[:-4] + f'.{os.getpid()}.{id(knobs)}'
        cmd = [self.vr, tag + '-m.wav', '--rate', str(RATE), '--seconds', f'{T_HIT + seconds:.4f}',
               '--hit', f'{T_HIT}:bd:{c}', '--jacks', tag + '-j.wav']
        for k, v in knobs.items():
            cmd += ['--set', f'{NAMES[k]}={v}']
        if not self.unit:
            cmd += ['--cal', 'nominal=1']
        for v in self.cal:
            cmd += ['--cal', v]
        env = dict(os.environ, TR909_NETLISTS=self.net)
        r = subprocess.run(cmd, capture_output=True, text=True, env=env)
        if r.returncode:
            raise RuntimeError(r.stderr)
        x, sr = sf.read(tag + '-j.wav', always_2d=True, dtype='float64')
        os.remove(tag + '-j.wav')
        os.remove(tag + '-m.wav')
        y = x[int((T_HIT - 0.001) * sr):, 0]  # the BD's MULTI OUT
        # the jack with a plug in it: R500 1k into C500 0.01u (SN 415300 up), a high-impedance input
        b, a = signal.bilinear([1], [1e3 * 0.01e-6, 1], RATE)
        y = signal.lfilter(b, a, y)
        np.save(path, y)
        return y


# ---------------------------------------------------------------------------------------------
# measurements

def harmonics(x, sr, a=0.04, b=0.14):
    """The 2nd and 3rd harmonics re the fundamental, dB, over a..b s (Hann, zero-padded)."""
    s = x[int(a * sr):int(b * sr)]
    if len(s) < 64:
        return float('nan'), float('nan')
    nfft = 1 << 18
    X = np.abs(np.fft.rfft(s * np.hanning(len(s)), nfft))
    fr = np.fft.rfftfreq(nfft, 1 / sr)
    band = (fr > 35) & (fr < 80)
    f0 = fr[band][np.argmax(X[band])]
    a1 = X[band].max()

    def at(k):
        m = (fr > k * f0 * 0.95) & (fr < k * f0 * 1.05)
        return 20 * math.log10(X[m].max() / a1)
    return at(2), at(3)


def bd_measure(x, sr):
    """real909.measure for the BD, plus: hold (first time 3 dB under the 20 ms envelope's peak,
    after it), the polarity of the first big swing, the click's level, the body's symmetry and
    harmonics."""
    m = R.measure(x, sr, 'BD')
    e = R.envelope(x, sr, 20.0)
    p = int(np.argmax(e))
    below = np.nonzero(e[p:] < e[p] * 10 ** (-3 / 20))[0]
    m['hold_ms'] = (p + below[0]) / sr * 1000 if len(below) else float('nan')
    below6 = np.nonzero(e[p:] < e[p] * 10 ** (-6 / 20))[0]
    m['t6_ms'] = (p + below6[0]) / sr * 1000 if len(below6) else float('nan')
    lp = R.lowpass(x, sr, 400)
    k = int(np.argmax(np.abs(lp[:int(0.02 * sr)]) > 0.3 * np.abs(lp[:int(0.05 * sr)]).max()))
    m['polarity'] = float(np.sign(lp[k]))
    m['peak_re_energy_db'] = m['peak_db'] - m['energy_db']
    seg = lp[int(0.03 * sr):int(0.09 * sr)]
    m['sym'] = float(seg.max() / -seg.min()) if seg.min() < 0 else float('nan')
    m['h2_db'], m['h3_db'] = harmonics(x, sr)
    hp = R.bandpass(x, sr, 1000, 8000)
    m['click_re_body_db'] = R.rms_db(hp, sr, 0.0015, 0.0045) - R.rms_db(x, sr, 0.010, 0.040)
    return m


# Both are measured from the trigger: the model's own, and the recording's as its click (the edge
# of Q8's pulse as the trigger ends) less the model's click delay. The onset a level threshold finds
# is no anchor for this voice: the first millisecond is the free-running VCO being reset (a blip of
# random size, which the recordings' own trimming has mostly cut) and a slow drift.
CLICK_MS = 2.35


def click_time(x, sr):
    """The click's edge (s): where the 2 kHz high-passed signal first reaches half its largest
    swing in the first 8 ms."""
    h = np.abs(R.highpass(x[:int(0.012 * sr)], sr, 2000))
    n = int(0.008 * sr)
    return int(np.argmax(h[:n] > 0.5 * h[:n].max())) / sr


def from_trigger(x, sr):
    """x from its estimated trigger: CLICK_MS before its click (zeros padded in front)."""
    i = int(round((click_time(x, sr) - CLICK_MS / 1000) * sr))
    return np.concatenate([np.zeros(-i), x]) if i < 0 else x[i:]


def ref(source, accent=None, **k):
    """The recording from its estimated trigger."""
    c = R.get('BD', source, accent, **k)
    x, sr = R.load(c)
    fi, dc = R.freeze(x, sr)
    if fi < len(x):
        x = x - dc
    y, _ = R.trim(x, sr, pre_ms=3.0)
    return from_trigger(y, sr), sr


KEYS = ('f_first', 'f_10ms', 'f_50ms', 'f_100ms', 'sweep_f_start', 'sweep_f_end', 'sweep_tau_ms', 'hold_ms',
        't6_ms', 't20', 't40', 'tail_tau_ms', 'click_db', 'click_re_body_db', 'energy_db', 'rms50_db',
        'peak_re_energy_db', 'centroid_hz', 'polarity', 'sym', 'h2_db', 'h3_db')


def run(cells, rd, jobs):
    """cells: [(label, knobs, code, (source, accent, refknobs))] -> list of dicts (real, model, lsd)."""
    def one(cell):
        label, knobs, c, (src, a, rk) = cell
        x, sr = ref(src, a, **rk)
        y = rd.render(dict(knobs, level=0.5), c, max(1.3, len(x) / sr + 0.05))
        y = R.chain(y, RATE, src)[int(0.001 * RATE):]  # the recording chain (AR: its 6.5 Hz high-pass)
        y = y[:int(len(x) / sr * RATE)]
        mr, mm = bd_measure(x, sr), bd_measure(y, RATE)
        win = (0.03, 0.33) if len(x) / sr > 0.35 else None
        return dict(label=label, knobs=knobs, code=c, source=src, real={k: mr.get(k) for k in KEYS},
                    model={k: mm.get(k) for k in KEYS}, lsd=R.lsd(x, sr, y, RATE, window=win))
    with cf.ThreadPoolExecutor(jobs) as ex:
        return list(ex.map(one, cells))


def f(v, nd=1):
    return 'nan' if v is None or v != v else f'{v:.{nd}f}'


def table(rows, cols, title, label='label'):
    print(f'\n{title}')
    print(f'{label:24s} ' + ' '.join(f'{c:>15s}' for c, _, _ in cols))
    for r in rows:
        print(f"{r['label']:24s} " + ' '.join(f"{f(r['real'][k], nd):>7s}/{f(r['model'][k], nd):<7s}" for _, k, nd in cols))


SUMMARY = (('sweep_f_end', 'Hz', False), ('sweep_tau_ms', 'oct', True), ('sweep_f_start', 'oct', True),
           ('f_first', 'oct', True), ('f_50ms', 'oct', True), ('hold_ms', 'oct', True), ('t20', 'oct', True),
           ('t40', 'oct', True), ('tail_tau_ms', 'oct', True), ('click_db', 'dB', False),
           ('click_re_body_db', 'dB', False), ('sym', '', False), ('h2_db', 'dB', False), ('h3_db', 'dB', False))


def diffs(rows, k, logk):
    d = []
    for r in rows:
        u, v = r['real'].get(k), r['model'].get(k)
        if u is None or v is None or u != u or v != v or (logk and (u <= 0 or v <= 0)):
            continue
        d.append(math.log2(u / v) if logk else u - v)
    return np.array(d)


def summary(rows, title):
    print(f'\n{title} ({len(rows)} cells): median real - model, and the spread (rms of the differences)')
    for k, unit, logk in SUMMARY:
        d = diffs(rows, k, logk)
        if len(d):
            print(f'  {k:16s} median {np.median(d):+7.3f} {unit:3s}  rms {np.sqrt(np.mean(d * d)):6.3f}  n {len(d)}')
    l = np.array([r['lsd'] for r in rows if r['lsd'] == r['lsd']])
    if len(l):
        print(f'  lsd 30-330 ms     median {np.median(l):.2f} dB, worst {l.max():.2f}')
    pol = [r['real']['polarity'] * r['model']['polarity'] for r in rows]
    print(f'  polarity agrees on {sum(1 for p in pol if p > 0)} of {len(pol)}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'tr909', 'compare-bd'))
    ap.add_argument('--voicerun', default=os.environ.get('VOICERUN', os.path.join(REPO, 'build', 'tr909', 'tr909voicerun')))
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--unit', action='store_true')
    ap.add_argument('--cal', action='append', default=[])
    ap.add_argument('--netlists', default=NET)
    ap.add_argument('--cells', default='grid', choices=('grid', 'quick'))
    ap.add_argument('--units', default='ar,acc,rr')
    ap.add_argument('--plots', action='store_true')
    ap.add_argument('--json')
    a = ap.parse_args()
    rd = Renderer(a.voicerun, os.path.join(a.out, 'cache'), a.cal, a.unit, a.netlists)
    units = a.units.split(',')
    grid21 = R.ar_code('BD')

    cells = []
    if a.cells == 'quick':
        grid = [(t, 0.0, 1.0) for t in KNOBS] + [(0.5, 0.0, d) for d in KNOBS] + [(0.5, at, 1.0) for at in KNOBS[1:]]
    else:
        grid = [(t, at, d) for t in KNOBS for at in KNOBS for d in KNOBS]
    for t, at, d in dict.fromkeys(grid) if 'ar' in units else ():
        k = dict(tune=t, attack=at, decay=d)
        cells.append((f'ar t{t:.2f} a{at:.2f} d{d:.2f}', k, grid21, ('ar', None, k)))
    for ak in [i / 10 for i in range(11)] if 'acc' in units else ():
        k = dict(tune=1.0, attack=1.0, decay=1.0)
        c = R.ar_code('BD', ak)
        cells.append((f'ar accent {ak:.1f} (code {c})', k, c, ('ar', ak, k)))
    for t in (0.0, 0.3, 0.7, 1.0) if 'rr' in units else ():
        for at in (0.0, 1.0):
            for d in (0.0, 0.3, 0.7, 1.0):
                k = dict(tune=t, attack=at, decay=d)
                try:
                    R.get('BD', 'rr', None, **k)
                except LookupError:
                    continue
                cells.append((f'rr t{t:.1f} a{at:.0f} d{d:.1f}', k, grid21, ('rr', None, k)))
    res = run(cells, rd, a.jobs)
    ar = [r for r in res if r['source'] == 'ar' and 'accent' not in r['label']]
    acc_rows = [r for r in res if 'accent' in r['label']]
    rr = [r for r in res if r['source'] == 'rr']

    def pick(rows, **want):
        return [r for r in rows if all(abs(r['knobs'][k] - v) < 1e-6 for k, v in want.items())]

    pcols = [('f_first', 'f_first', 0), ('f_10ms', 'f_10ms', 0), ('f_50ms', 'f_50ms', 1), ('f_100ms', 'f_100ms', 1),
             ('f_start', 'sweep_f_start', 0), ('f_end', 'sweep_f_end', 2), ('tau ms', 'sweep_tau_ms', 1)]
    dcols = [('hold ms', 'hold_ms', 0), ('-6 dB ms', 't6_ms', 0), ('t20 ms', 't20', 0), ('t40 ms', 't40', 0),
             ('tail tau ms', 'tail_tau_ms', 0)]
    acols = [('click dB', 'click_db', 1), ('click/body', 'click_re_body_db', 1), ('peak-E dB', 'peak_re_energy_db', 1),
             ('+/- 30-90', 'sym', 2)]
    hcols = [('+/- 30-90', 'sym', 2), ('h2 dB', 'h2_db', 1), ('h3 dB', 'h3_db', 1)]

    if ar:
        table(pick(ar, attack=0.0, decay=1.0), pcols, 'Pitch against TUNE (AudioRealism, ATTACK 0, DECAY 100 %): real / model')
        table(pick(ar, tune=0.5, attack=0.0), dcols, 'Decay against DECAY (AudioRealism, TUNE 50 %, ATTACK 0): real / model')
        table(pick(ar, tune=0.5, decay=1.0), acols, 'Attack against ATTACK (AudioRealism, TUNE 50 %, DECAY 100 %): real / model')
        table([r for r in ar if r['knobs']['tune'] <= 0.25 and r['knobs']['attack'] == 0.0], hcols,
              'The body (AudioRealism, ATTACK 0, TUNE 0 and 25 %): real / model')
        off = np.median([r['real']['energy_db'] - r['model']['energy_db'] for r in ar])
        print(f'\nLevel (energy, dB): real - model, median {off:.2f} dB (the interface gain); per cell after it:')
        dl = np.array([r['real']['energy_db'] - r['model']['energy_db'] - off for r in ar])
        print(f'  spread over {len(ar)} cells: rms {np.sqrt(np.mean(dl * dl)):.2f} dB, worst {dl[np.argmax(np.abs(dl))]:+.2f} dB '
              f"({ar[int(np.argmax(np.abs(dl)))]['label']})")
        for name in ('attack', 'decay', 'tune'):
            by = {}
            for r, d in zip(ar, dl):
                by.setdefault(r['knobs'][name], []).append(d)
            print(f'  by {name.upper():6s}: ' + '  '.join(f'{k:.2f}: {np.mean(v):+.2f}' for k, v in sorted(by.items())))
        summary(ar, 'AudioRealism grid')
    if acc_rows:
        e0r, e0m = acc_rows[0]['real']['energy_db'], acc_rows[0]['model']['energy_db']
        r0r, r0m = acc_rows[0]['real']['rms50_db'], acc_rows[0]['model']['rms50_db']
        print('\nAccent (AudioRealism accent000-100, TUNE/ATTACK/DECAY 100 %): dB re accent000, real / model')
        for r in acc_rows:
            print(f"  {r['label']:28s} rms 0-50 ms {r['real']['rms50_db'] - r0r:+5.2f} / {r['model']['rms50_db'] - r0m:+5.2f}"
                  f"   energy {r['real']['energy_db'] - e0r:+5.2f} / {r['model']['energy_db'] - e0m:+5.2f}"
                  f"   t40 {f(r['real']['t40'], 0)} / {f(r['model']['t40'], 0)}")
    if rr:
        table(pick(rr, attack=0.0, decay=1.0), pcols, 'Pitch against TUNE (Rob Roy, ATTACK 0, DECAY 10): real / model')
        table(pick(rr, tune=0.7, attack=0.0), dcols, 'Decay against DECAY (Rob Roy, TUNE 7, ATTACK 0): real / model')
        summary(rr, 'Rob Roy')
    if a.json:
        with open(a.json, 'w') as fo:
            json.dump(res, fo, indent=1, default=float)
    if a.plots and ar:
        plots(a.out, rd, ar, grid21)


def plots(out, rd, ar, code):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(5, 2, figsize=(14, 16))
    g = 10 ** (np.median([r['real']['energy_db'] - r['model']['energy_db'] for r in ar]) / 20)
    for i, t in enumerate(KNOBS):
        k = dict(tune=t, attack=0.0, decay=1.0)
        x, sr = ref('ar', None, **k)
        y = R.chain(rd.render(dict(k, level=0.5), code, max(1.3, len(x) / sr + 0.05)), RATE, 'ar')[int(0.001 * RATE):]
        n = int(0.15 * sr)
        ax[i, 0].plot(np.arange(n) / sr * 1e3, x[:n], lw=0.8, label='AudioRealism')
        ax[i, 0].plot(np.arange(n) / RATE * 1e3, g * y[:n], lw=0.8, label='model')
        ax[i, 0].set_title(f'TUNE {t:.2f}, ATTACK 0, DECAY 1'); ax[i, 0].grid(alpha=.3)
        tx, fx = R.zc_track(x, sr, lp=1500, floor_db=-40)
        ty, fy = R.zc_track(y, RATE, lp=1500, floor_db=-40)
        ax[i, 1].plot(tx * 1e3, fx, 'o-', ms=3, label='AudioRealism')
        ax[i, 1].plot(ty * 1e3, fy, 's--', ms=3, label='model')
        ax[i, 1].set_xlim(0, 300); ax[i, 1].set_ylim(40, 300); ax[i, 1].grid(alpha=.3); ax[i, 1].set_ylabel('Hz')
    ax[0, 0].legend(); ax[0, 1].legend()
    plt.tight_layout()
    p = os.path.join(out, 'bd_tune.png')
    plt.savefig(p, dpi=70)
    print('plot', p)


if __name__ == '__main__':
    main()
