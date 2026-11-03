#!/usr/bin/env python3
"""The snare drum against real TR-909s: renders SD hits through tr909voicerun (the voicing board
running core/netlists/sd.cir, 96 kHz, a 2 ms trigger; its output through the jack board's R503 and
C502, as the recordings were taken at the MULTI OUT jacks) at the AudioRealism grid's knob settings
and at Rob Roy's dial settings, measures them the way real909.py measures the recordings, and prints
the comparison as Markdown tables (docs/circuit/sd.md, "Against the real 909").

  sd.py [--voicerun PATH] [--netlists DIR] [--jobs 4] [--out DIR] [--unit] [--cal sd.c71=6.8n ...]
        [--no-chain] [--quick]

The model's output goes through each source's recording chain (real909.chain: AR's 6.5 Hz high-pass)
unless --no-chain. --netlists reads DIR/<name>.cir in place of the built-in netlists
(TR909_NETLISTS); the board runs
the service notes' parts and straight knobs (voicerun --cal nominal=1) unless --unit; --cal passes
part values on top (voicerun --cal); --quick renders only TUNE 0.5 and the accents.
The renders are kept in DIR (default build/tr909/compare-sd).

What is compared (single takes on both sides: the snappy is noise, so its numbers scatter):
  pitch     VCO-1 and VCO-2 (f_low, f_high: the strongest lines 30-150 ms), their ratio, the
            early line (2-20 ms, the pitch bend), SNAPPY 0, against AR and Rob Roy
  decay     the hit's t-20 / t-40 (10 ms RMS envelope, last time above), SNAPPY 0
  snappy    the noise above 2 kHz: its t-20 / t-40 by TONE and SNAPPY, its RMS 20-60 ms re the
            hit's RMS 0-50 ms by SNAPPY, the spectral centroid 0-60 ms
  level     each render's RMS 0-50 ms in dBV against the AR take's dBFS: the offset is the
            interface's gain, which should be one number over the grid; its spread is the level
            law's error
  accent    AR's eleven TOTAL ACCENT takes (codes 21-47 through the firmware's table) and
            Regebro's four (codes 10 / 14 / 21 / 33): RMS 0-50 ms and centroid
  spectrum  the 1/6-octave log-spectral distance (real909.lsd, 0-200 ms) model against AR
"""
import argparse, json, math, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import soundfile as sf
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import real909 as r  # noqa: E402

REPO = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..'))
VALUES = (0.0, 0.25, 0.5, 0.75, 1.0)
RATE = 96000
T_HIT = 0.05
SD = 1  # the SD jack's channel in voicerun's --jacks file


GRID = r.ar_code('SD')  # AR's grid: an accented step at TOTAL ACCENT 0 (21)


def render(a, knobs, code=GRID, seconds=1.3, source='ar'):
    """One hit: knobs {tune, tone, snappy, level}; returns (x from its onset, sr)."""
    tag = '-'.join(f'{k}{knobs[k]:.2f}' for k in sorted(knobs)) + f'-c{code}'
    path = os.path.join(a.out, f'sd-{tag}.wav')
    if not os.path.exists(path) or a.fresh:
        cmd = [a.voicerun, os.path.join(a.out, 'master-' + tag + '.wav'), '--rate', str(RATE), '--seconds', str(seconds),
               '--hit', f'{T_HIT}:sd:{code}', '--jacks', path]
        names = {'tune': 'sdTune', 'tone': 'sdTone', 'snappy': 'sdSnappy', 'level': 'sdLevel'}
        for k, v in knobs.items():
            cmd += ['--set', f'{names[k]}={v}']
        if not a.unit:
            cmd += ['--cal', 'nominal=1']  # the service notes' parts and straight knobs
        for c in a.cal:
            cmd += ['--cal', c]
        env = dict(os.environ)
        if a.netlists:
            env['TR909_NETLISTS'] = a.netlists
        subprocess.run(cmd, check=True, capture_output=True, env=env)
        os.remove(os.path.join(a.out, 'master-' + tag + '.wav'))
    x, sr = sf.read(path, always_2d=True, dtype='float64')
    # the MULTI OUT jack with a plug in it (the recordings): the jack board's R503 1k into C502
    # 0.01 uF (SN 415300 up), the plug opening the normal contact to R504/R505
    b, a_ = signal.bilinear([1.0], [1e3 * 10e-9, 1.0], sr)
    y = signal.lfilter(b, a_, x[:, SD])
    if not a.no_chain:
        # the source's recording chain (real909.chain: AR's 6.5 Hz high-pass, which turns the drum
        # part's DC step into AR's overshoot at 40-60 ms; the 909's MULTI OUT is DC-coupled)
        y = r.chain(y, sr, source)
    y, _ = r.trim(y, sr)
    return y, sr


def measure(x, sr):
    m = r.measure(x, sr, 'SD')
    m['rms50_dbv'] = r.rms_db(x, sr, 0, 0.05)
    # the first 10 ms against what follows: RMS 0-10 ms re RMS 10-50 ms
    m['attack_db'] = r.rms_db(x, sr, 0, 0.01) - r.rms_db(x, sr, 0.01, 0.05)
    # the snappy's level: its part above 2 kHz, RMS 20-60 ms, re the hit's RMS 0-50 ms
    m['noise_re_hit_db'] = r.rms_db(r.highpass(x, sr, 2000), sr, 0.02, 0.06) - m['rms50_dbv']
    m['centroid60'] = r.centroid(x, sr, (0.0, 0.06))
    # VCO-2's line re VCO-1's, 30-150 ms
    late = x[int(0.03 * sr):int(0.15 * sr)]
    pk = r.spectral_peaks(late, sr, 80, 700, 6)
    m['high_re_low_db'] = float('nan')
    if pk and m.get('f_low') and m.get('f_high') == m.get('f_high'):
        lo = min(pk, key=lambda p: abs(p[0] - m['f_low']))
        hi = min(pk, key=lambda p: abs(p[0] - m['f_high']))
        if abs(lo[0] / m['f_low'] - 1) < 0.02 and abs(hi[0] / m['f_high'] - 1) < 0.02:
            m['high_re_low_db'] = hi[1] - lo[1]
    return m


def ref(source, accent=None, **want):
    try:
        x, sr, c = r.load_ref('SD', source, accent, **want)
    except LookupError:
        return None
    return measure(x, sr), x, sr


def f1(v, d=1):
    return '-' if v is None or (isinstance(v, float) and not math.isfinite(v)) else f'{v:.{d}f}'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--voicerun', default=os.environ.get('VOICERUN', os.path.join(REPO, 'build/tr909/tr909voicerun')))
    ap.add_argument('--netlists')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--out', default=os.path.join(REPO, 'build/tr909/compare-sd'))
    ap.add_argument('--cal', action='append', default=[])
    ap.add_argument('--quick', action='store_true')
    ap.add_argument('--no-chain', action='store_true', help="leave out the recordings' chain (real909.chain)")
    ap.add_argument('--unit', action='store_true', help="the board's unit calibration rather than the nominal parts")
    ap.add_argument('--fresh', action='store_true')
    ap.add_argument('--json')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    tunes = (0.5,) if a.quick else VALUES
    jobs = {}
    for tu in tunes:
        for to in VALUES:
            for sn in VALUES:
                if a.quick and not (to == 0.5 or sn in (0.0, 1.0)):
                    continue
                jobs[('ar', tu, to, sn, GRID)] = dict(tune=tu, tone=to, snappy=sn, level=0.5)
    # AR's accent takes (TOTAL ACCENT on an accented step, codes 21-47) and Regebro's codes (10 none,
    # 14 TOTAL only, 21 local only, 33 both, at TOTAL ACCENT about half: docs/reference.md, Accent)
    acc_codes = sorted({r.ar_code('SD', k / 10) for k in range(11)} | {10, 14, 21, 33})
    for code in acc_codes:
        jobs[('acc', 0.5, 1.0, 1.0, code)] = dict(tune=0.5, tone=1.0, snappy=1.0, level=0.5)
    # Rob Roy's unit (how it was played is unknown: taken as AR's grid, code 21)
    for d in (0.0, 0.3, 0.7, 1.0):
        jobs[('rr', d, 0.5, 0.0, GRID)] = dict(tune=d, tone=0.5, snappy=0.0, level=1.0)
    jobs[('rr', 0.7, 0.0, 1.0, GRID)] = dict(tune=0.7, tone=0.0, snappy=1.0, level=1.0)

    def run(key):
        x, sr = render(a, jobs[key], key[4], source='rr' if key[0] == 'rr' else 'ar')
        return key, measure(x, sr), x, sr
    with ThreadPoolExecutor(a.jobs) as ex:
        got = {k: (m, x, sr) for k, m, x, sr in ex.map(run, list(jobs))}
    out = {'model': {}, 'ref': {}}

    print('## SD against AudioRealism (AR) and Rob Roy (RR)\n')
    print(f'Model: tr909voicerun at 96 kHz, LEVEL 0.5 as AR, a 2 ms trigger, code {GRID} (AR grid: an accented step at TOTAL ACCENT 0), through the jack board R503/C502 as at a plugged MULTI OUT, and {"no recording chain" if a.no_chain else "real909.chain (AR: a 6.5 Hz high-pass)"}.\n')

    # pitch and decay, drum part
    print('### Drum part (SNAPPY 0, TONE 0.5): VCO lines 30-150 ms, early line 2-20 ms, decay\n')
    print('| TUNE | f_low model / AR | f_high model / AR | ratio model / AR | VCO-2 re VCO-1 dB | early model / AR | t-20 model / AR | t-40 model / AR | 0-10 re 10-50 ms dB |')
    print('|---|---|---|---|---|---|---|---|---|')
    for tu in tunes:
        m = got[('ar', tu, 0.5, 0.0, GRID)][0]
        rf = ref('ar', tune=tu, tone=0.5, snappy=0.0)
        g = rf[0] if rf else {}
        e = lambda d: d['peaks_early'][0][0] if d.get('peaks_early') else float('nan')
        ratio = lambda d: d.get('f_high', float('nan')) / d.get('f_low', float('nan')) if d.get('f_low') else float('nan')
        print(f"| {tu:.2f} | {f1(m.get('f_low'))} / {f1(g.get('f_low'))} | {f1(m.get('f_high'))} / {f1(g.get('f_high'))} | "
              f"{f1(ratio(m), 3)} / {f1(ratio(g), 3)} | {f1(m['high_re_low_db'])} / {f1(g.get('high_re_low_db'))} | {f1(e(m))} / {f1(e(g))} | {f1(m['t20'], 0)} / {f1(g.get('t20'), 0)} | "
              f"{f1(m['t40'], 0)} / {f1(g.get('t40'), 0)} | {f1(m['attack_db'])} / {f1(g.get('attack_db'))} |")
        out['model'][f'drum_tune{tu}'] = m
        out['ref'][f'drum_tune{tu}'] = g
    print()
    print('Rob Roy (LEVEL at full, normalised; dial d as rotation d/10):\n')
    print('| TUNE dial | f_low model / RR | f_high model / RR | ratio model / RR | t-20 model / RR |')
    print('|---|---|---|---|---|')
    for d in (0.0, 0.3, 0.7, 1.0):
        m = got[('rr', d, 0.5, 0.0, GRID)][0]
        cands = r.find('SD', 'rr', tune=d, snappy=0.0)
        g = {}
        if cands:
            x, sr = r.load(cands[0])
            g = measure(r.trim(x, sr)[0], sr)
        ratio = lambda q: q.get('f_high', float('nan')) / q['f_low'] if q.get('f_low') else float('nan')
        print(f"| {int(round(d * 10))} | {f1(m.get('f_low'))} / {f1(g.get('f_low'))} | {f1(m.get('f_high'))} / "
              f"{f1(g.get('f_high'))} | {f1(ratio(m), 3)} / {f1(ratio(g), 3)} | {f1(m['t20'], 0)} / {f1(g.get('t20'), 0)} |")
    m = got[('rr', 0.7, 0.0, 1.0, GRID)][0]
    cands = r.find('SD', 'rr', tune=0.7, tone=0.0, snappy=1.0)
    g = {}
    if cands:
        x, sr = r.load(cands[0])
        g = measure(r.trim(x, sr)[0], sr)
    print(f"\nRob Roy noise (TUNE 7, TONE 0, SNAPPY 10): t-20 / t-40 above 2 kHz {f1(m['noise_t20'], 0)}/{f1(m['noise_t40'], 0)} "
          f"model, {f1(g.get('noise_t20'), 0)}/{f1(g.get('noise_t40'), 0)} RR; centroid 0-60 ms {f1(m['centroid60'], 0)} / "
          f"{f1(g.get('centroid60'), 0)} Hz")
    print()

    # snappy
    print('### Snappy (TUNE 0.5): noise above 2 kHz, t-20 / t-40 ms, model / AR\n')
    print('| TONE | ' + ' | '.join(f'SNAPPY {s:g}' for s in VALUES[1:]) + ' |')
    print('|---|' + '---|' * 4)
    for to in VALUES:
        cells = []
        for sn in VALUES[1:]:
            k = ('ar', 0.5, to, sn, GRID)
            if k not in got:
                cells.append('-')
                continue
            m = got[k][0]
            rf = ref('ar', tune=0.5, tone=to, snappy=sn)
            g = rf[0] if rf else {}
            cells.append(f"{f1(m['noise_t20'], 0)}/{f1(m['noise_t40'], 0)} : {f1(g.get('noise_t20'), 0)}/{f1(g.get('noise_t40'), 0)}")
        print(f'| {to:.2f} | ' + ' | '.join(cells) + ' |')
    print()
    print('SNAPPY (TUNE 0.5, TONE 0.5): the >2 kHz part\'s RMS 20-60 ms re the hit\'s RMS 0-50 ms (dB); centroid 0-60 ms (Hz); RMS 0-50 ms re SNAPPY 0 (dB).\n')
    print('| SNAPPY | noise re hit model / AR | centroid model / AR | rms50 re S0 model / AR |')
    print('|---|---|---|---|')
    m0 = got[('ar', 0.5, 0.5, 0.0, GRID)][0]
    g0 = (ref('ar', tune=0.5, tone=0.5, snappy=0.0) or [{}])[0]
    for sn in VALUES:
        m = got[('ar', 0.5, 0.5, sn, GRID)][0]
        g = (ref('ar', tune=0.5, tone=0.5, snappy=sn) or [{}])[0]
        print(f"| {sn:.2f} | {f1(m['noise_re_hit_db'])} / {f1(g.get('noise_re_hit_db'))} | "
              f"{f1(m['centroid60'], 0)} / {f1(g.get('centroid60'), 0)} | {f1(m['rms50_dbv'] - m0['rms50_dbv'])} / "
              f"{f1(g.get('rms50_dbv', float('nan')) - g0.get('rms50_dbv', float('nan')))} |")
    print()

    # level offset over the grid
    offs = []
    for k, (m, x, sr) in got.items():
        if k[0] != 'ar':
            continue
        rf = ref('ar', tune=k[1], tone=k[2], snappy=k[3])
        if rf:
            offs.append((k, rf[0]['rms50_dbv'] - m['rms50_dbv']))
    if offs:
        v = np.array([o for _, o in offs])
        print(f'### Level\n\nAR dBFS minus model dBV (RMS 0-50 ms) over {len(v)} grid points: median {np.median(v):.1f} dB, '
              f'spread (5-95 %) {np.percentile(v, 5):.1f} to {np.percentile(v, 95):.1f} dB. By knob (median offset re the overall median):\n')
        med = np.median(v)
        for i, name in ((1, 'TUNE'), (2, 'TONE'), (3, 'SNAPPY')):
            row = []
            for val in VALUES:
                s = [o for kk, o in offs if abs(kk[i] - val) < 1e-6]
                row.append(f1(np.median(s) - med) if s else '-')
            print(f'- {name} ' + ' / '.join(f'{val:g}: {c}' for val, c in zip(VALUES, row)))
        print()
        out['level_offsets'] = {f'{k[1]}-{k[2]}-{k[3]}': o for k, o in offs}

    # accent
    print('### Accent (TUNE 0.5, TONE 1, SNAPPY 1): AR\'s TOTAL ACCENT takes, RMS 0-50 ms re accent100 (dB), centroid 0-60 ms (Hz)\n')
    print('| take | code | rms model / AR | centroid model / AR |')
    print('|---|---|---|---|')
    top = r.ar_code('SD', 1.0)
    mt = got[('acc', 0.5, 1.0, 1.0, top)][0]
    a100 = ref('ar', accent=1.0, tune=0.5, tone=1.0, snappy=1.0)
    for k in range(11):
        code = r.ar_code('SD', k / 10)
        m = got[('acc', 0.5, 1.0, 1.0, code)][0]
        g = ref('ar', accent=k / 10, tune=0.5, tone=1.0, snappy=1.0)
        ga = f1(g[0]['rms50_dbv'] - a100[0]['rms50_dbv']) if g and a100 else '-'
        gc = f1(g[0]['centroid60'], 0) if g else '-'
        print(f"| {10 * k} % | {code} | {f1(m['rms50_dbv'] - mt['rms50_dbv'])} / {ga} | {f1(m['centroid60'], 0)} / {gc} |")
    gp = ref('ar', tune=0.5, tone=1.0, snappy=1.0)
    if gp and a100:
        print(f"\nAR's grid take (code {GRID}): {gp[0]['rms50_dbv'] - a100[0]['rms50_dbv']:+.1f} dB re accent100.")
    m10 = got[('acc', 0.5, 1.0, 1.0, 10)][0]['rms50_dbv']
    print('Regebro (rev 4, knobs unknown; TOTAL ACCENT about half: codes 10 / 14 / 21 / 33), re take 1: '
          f"model {got[('acc', 0.5, 1.0, 1.0, 14)][0]['rms50_dbv'] - m10:+.1f} / {got[('acc', 0.5, 1.0, 1.0, 21)][0]['rms50_dbv'] - m10:+.1f} / "
          f"{got[('acc', 0.5, 1.0, 1.0, 33)][0]['rms50_dbv'] - m10:+.1f}, Regebro +3.0 / +7.3 / +9.8")
    print()

    # spectrum
    print('### Spectrum: log-spectral distance model against AR, 0-200 ms, 1/6 octave (dB)\n')
    print('| TUNE | SNAPPY 0 | SNAPPY 0.5 | SNAPPY 1 |')
    print('|---|---|---|---|')
    for tu in tunes:
        cells = []
        for sn in (0.0, 0.5, 1.0):
            _, x, sr = got[('ar', tu, 0.5, sn, GRID)]
            rf = ref('ar', tune=tu, tone=0.5, snappy=sn)
            cells.append(f1(r.lsd(x, sr, rf[1], rf[2], window=(0.0, 0.2))) if rf else '-')
        print(f'| {tu:.2f} | ' + ' | '.join(cells) + ' |')
    print()
    if a.json:
        json.dump(out, open(a.json, 'w'), indent=1, default=float)


if __name__ == '__main__':
    main()
