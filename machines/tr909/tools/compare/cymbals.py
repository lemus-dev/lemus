#!/usr/bin/env python3
"""The crash and ride (core/cymbals.*, core/netlists/cr.cir, rd.cir and their clocks crclk.cir,
rdclk.cir) against real TR-909s: renders each recording's settings through the board
(tr909voicerun, as the plugin plays it; or tr909cyrun, the voice alone) and measures both the same
way (docs/circuit/cymbals.md, "Against the real 909", "Calibration").

    cymbals.py [--voicerun PATH | --cyrun PATH] [--cal SPEC ...] [--nominal] [--out DIR] [--jobs 2]
               [--only ar,acc,rr] [--voices cr,rd] [--match] [--json FILE]

--cal passes tr909voicerun's --cal (cr.vr25=8.4k, law.crTune=0.07,0,0.95,1); --nominal starts from
the service notes' unit (--cal nominal=1). With --cyrun, --cal takes the voice's parameters only
(vr25=8.4k) and knob laws don't apply.

The recordings (real909.py; private/reference/real909/index.md):
  ar    AudioRealism, one late unit, 96 kHz/24-bit, individual outs, LEVEL 50 %: CR and RD TUNE
        0-100 % in 10 % steps, played as an accented step at TOTAL ACCENT 0 (DAC code 32,
        real909.ar_code)
  acc   the same unit's 11 accent takes at TUNE 50 %: a TOTAL ACCENT sweep on an accented step,
        codes 32-47 (real909.AR_ACCENT_TA)
  rr    Rob Roy 1995, a second unit, TUNE dial 0-10 in 2s, LEVEL full, normalised, 44.1 kHz
The model plays each at that code (the other cymbal stopped), LEVEL as the recording's, TUNE the
recording's knob rotation (--match: the rotation at which the model's clock is the recording's,
from tr909cyrun --timing, where it reaches it: the analog half compared at the same clock); its
output is the MULTI OUT jack (behind R524/C509, R527/C510) resampled to the recording's rate.

Measures, on both:
  rate    the sample clock: the ROM rendered as a zero-order hold at candidate clocks against the
          recording, band-passed 1.5-12 kHz (real909.rom_rate)
  env     the VCA's law: each 512-sample block's RMS (1.5-12 kHz) against the ROM's own block
          there (rendered at the measured clock), in dB re blocks 1-4, against the envelope D/A's
          code (the block number): the RMS of the model's minus the recording's over the blocks
          within 35 dB, and the gain at codes 16, 32, 48, 56
  level   within a series, the take's energy against the series' reference take (TUNE 50 %, or
          accent 100), model minus recording
  lsd     log-spectral distance over 0-300 ms (real909.lsd, sixth octaves, energy matched), and
          the level difference per octave band 500 Hz-16 kHz there, model minus recording
  decay   t-20 / t-40 (real909.decay_times, last time above the peak -20 / -40 dB, 20 Hz high-passed)
A summary per series gives the clock's error in cents (rms; AR's grid split into the even, fitted,
and the odd, held-out, settings).
"""
import argparse
import concurrent.futures as cf
import json
import os
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import real909 as R  # noqa: E402

REPO = os.path.normpath(os.path.join(HERE, '../../../..'))
OCT = [500, 1000, 2000, 4000, 8000, 16000]
WHICH = {'cr': 'crash', 'rd': 'ride'}


def render(a, voice, tune, code, level, seconds=1.6):
    """One hit: through the board (tr909voicerun, the voice's MULTI OUT jack at 96 kHz) or the voice
    alone (tr909cyrun, its jack at 192 kHz). Cached by its settings."""
    tag = '_'.join(c.replace('=', '').replace(',', '-').replace('.', 'p') for c in a.cal) or 'default'
    name = f"{'vr' if a.voicerun else 'cy'}_{voice}_t{tune:.4f}_c{code}_l{level:.2f}_{'nom_' if a.nominal else ''}{tag}"
    path = os.path.join(a.out, name[:200] + '.wav')
    if os.path.exists(path):
        return path
    if a.voicerun:
        jacks = path + '.jacks.wav'
        args = [a.voicerun, path + '.master.wav', '--rate', '96000', '--seconds', str(seconds),
                '--hit', f'0.01:{voice}:{code}', '--set', f'{voice}Tune={tune}', '--set', f'{voice}Level={level}',
                '--jacks', jacks]
        if a.nominal:
            args += ['--cal', 'nominal=1']
        for c in a.cal:
            args += ['--cal', c]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(r.stderr)
        x, sr = sf.read(jacks, always_2d=True)
        sf.write(path, x[:, 8 if voice == 'cr' else 9], sr, subtype='FLOAT')
        for f in (jacks, path + '.master.wav'):
            os.remove(f)
    else:
        args = [a.cyrun, '--voice', voice, '--tune', str(tune), '--acc', str(code), '--level', str(level),
                '--rate', '192000', '--seconds', str(seconds), '--out', path]
        for c in a.cal:
            args += ['--param', c]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(r.stderr)
    return path


def model_at(path, sr, source):
    """The render at the recording's rate, through the source's recording chain (real909.chain:
    AudioRealism's 6.5 Hz high-pass), trimmed to its onset as the recordings are."""
    x, fs = R.load(path)
    if int(fs) != int(sr):
        g = np.gcd(int(fs), int(sr))
        x = signal.resample_poly(x, int(sr) // g, int(fs) // g)
    y, _ = R.trim(R.chain(x, sr, source), sr)
    return y


def envelope_law(x, sr, which, rr):
    """Per-block gain (dB re blocks 1-4) against the ROM, from rom_rate's clock and offset."""
    rate, off = rr['rate_hz'], rr['rom_offset']
    data = R.rom(which)
    hp = R.bandpass(x, sr, 1500, 12000, 2)
    n = np.arange(len(x))
    rom_n = off + (n / sr - 0.003) * rate  # the ROM sample playing at each output sample
    k = np.floor(np.clip(rom_n, 0, len(data) - 1)).astype(int)
    zoh = R.bandpass(data[k], sr, 1500, 12000, 2)
    g = []
    for b in range(64):
        m = (rom_n >= 512 * b) & (rom_n < 512 * (b + 1))
        if m.sum() < 16:
            g.append(np.nan)
            continue
        a, r = np.sqrt(np.mean(hp[m] ** 2)), np.sqrt(np.mean(zoh[m] ** 2))
        g.append(20 * np.log10(a / r) if a > 0 and r > 0 else np.nan)
    g = np.array(g)
    return g - np.nanmean(g[1:5])


def measure(x, sr, which):
    rr = R.rom_rate(x, sr, which)
    env = envelope_law(x, sr, which, rr)
    _, lv = R.band_levels(x, sr, (0.0, 0.3), per_oct=1, fmin=354.0, fmax=22600.0, kind='flat')
    dt = R.decay_times(R.highpass(x, sr, 20.0), sr, (20, 40))
    return {'rate': rr['rate_hz'], 'ncc': rr['ncc'], 'env': env, 'energy': R.energy_db(x, sr), 'oct': lv,
            't20': dt['t20'], 't40': dt['t40']}


def series(voice, unit):
    """(label, clip, tune, code, level) for one voice and unit."""
    V = voice.upper()
    out = []
    if unit == 'ar':
        for t in R.grid(V, 'ar')['tune']:
            out.append((f'tune {t:.1f}', R.get(V, 'ar', tune=t), t, R.ar_code(V), 0.5))
    elif unit == 'acc':
        for a in range(11):
            c = R.find(V, 'ar', accent=a / 10, tune=0.5)
            if c:
                out.append((f'acc {a / 10:.1f}', c[0], 0.5, R.ar_code(V, a / 10), 0.5))
    elif unit == 'rr':
        for t in R.grid(V, 'rr')['tune']:
            c = R.find(V, 'rr', tune=t)
            if c:
                out.append((f'dial {10 * t:.0f}', c[0], t, R.ar_code(V), 1.0))
    return out


def timing(a, voice):
    """The model's sample clock against the TUNE knob (tr909cyrun --timing; the knob law, if any,
    applied): (tunes, rates)."""
    cyrun = a.cyrun or os.path.join(os.path.dirname(a.voicerun), 'tr909cyrun')
    args = [cyrun, '--voice', voice, '--timing', '--seconds', '0.001']
    law = None
    for c in a.cal:
        k, v = c.split('=', 1)
        if k == f'law.{voice}Tune':
            p = [float(q) for q in v.split(',')]
            law = (np.array(p[0::2]), np.array(p[1::2]))
        elif k.startswith(voice + '.'):
            args += ['--param', f'{k[len(voice) + 1:]}={v}']
        elif '.' not in k:
            args += ['--param', c]
    out = subprocess.run(args, capture_output=True, text=True).stdout.split('\n')[1:]
    rows = [list(map(float, l.split()[:2])) for l in out if len(l.split()) > 3]
    pos, rates = np.array([r[0] for r in rows]), np.array([r[1] for r in rows])
    if law is None:
        return pos, rates
    knob = np.linspace(0, 1, 401)
    return knob, np.interp(np.interp(knob, *law), pos, rates)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--voicerun', default=os.environ.get('VOICERUN', os.path.join(REPO, 'build/tr909/tr909voicerun')))
    ap.add_argument('--cyrun', default=None)
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'tr909', 'compare', 'cymbals'))
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--only', default='ar,acc,rr')
    ap.add_argument('--voices', default='cr,rd')
    ap.add_argument('--cal', action='append', default=[])
    ap.add_argument('--nominal', action='store_true')
    ap.add_argument('--match', action='store_true')
    ap.add_argument('--json')
    a = ap.parse_args()
    if a.cyrun:
        a.voicerun = None
    os.makedirs(a.out, exist_ok=True)
    results = {}
    for voice in a.voices.split(','):
        which = WHICH[voice]
        for unit in a.only.split(','):
            rows = series(voice, unit)
            if not rows:
                continue
            if a.match:
                tunes, rates = timing(a, voice)
                fixed = []
                for label, clip, tune, code, level in rows:
                    x, sr, _ = R.load_ref(voice.upper(), clip.source, clip.accent, **clip.knobs)
                    want = R.rom_rate(x, sr, which)['rate_hz']
                    if rates.min() <= want <= rates.max():
                        fixed.append((label, clip, float(np.interp(want, rates, tunes)), code, level))
                rows = fixed
            with cf.ThreadPoolExecutor(a.jobs) as ex:
                paths = list(ex.map(lambda r: render(a, voice, r[2], r[3], r[4]), rows))

            def work(i):
                label, clip, tune, code, level = rows[i]
                x, sr, _ = R.load_ref(voice.upper(), clip.source, clip.accent, **clip.knobs)
                y = model_at(paths[i], sr, clip.source)
                return label, code, measure(x, sr, which), measure(y, sr, which), R.lsd(x, sr, y, sr, window=(0.0, 0.3))

            with cf.ThreadPoolExecutor(a.jobs) as ex:
                res = list(ex.map(work, range(len(rows))))
            ref = {'ar': 'tune 0.5', 'acc': 'acc 1.0', 'rr': 'dial 6'}[unit]
            e0 = next((r for r in res if r[0] == ref), res[len(res) // 2])
            print(f'\n{voice} {unit}: model against the recording')
            print(f"{'take':9s} {'code':>4s} {'rate rec':>9s} {'model':>8s} {'cents':>6s}  {'t20 rec/mod':>11s} {'t40 rec/mod':>11s}"
                  f"  {'env':>5s}  {'g48 rec/mod':>11s} {'g56':>11s}  {'level d':>7s} {'lsd':>5s}  oct 0.5-16k (model - rec, dB)")
            cents = []
            for label, code, mr, mm, d in res:
                k = np.isfinite(mr['env']) & np.isfinite(mm['env']) & (mr['env'] > -35)
                erms = float(np.sqrt(np.mean((mm['env'][k] - mr['env'][k]) ** 2))) if k.any() else float('nan')
                lvl = (mm['energy'] - e0[3]['energy']) - (mr['energy'] - e0[2]['energy'])
                octd = (mm['oct'] - mm['oct'].max()) - (mr['oct'] - mr['oct'].max())
                c = 1200 * np.log2(mm['rate'] / mr['rate'])
                cents.append((label, c))
                gs = ' '.join(f"{mr['env'][q]:5.1f}/{mm['env'][q]:5.1f}" for q in (48, 56))
                print(f"{label:9s} {code:4d} {mr['rate']:9.0f} {mm['rate']:8.0f} {c:+6.0f}  {mr['t20']:5.0f}/{mm['t20']:5.0f}"
                      f" {mr['t40']:5.0f}/{mm['t40']:5.0f}  {erms:5.2f}  {gs}  {lvl:+7.2f} {d:5.2f}  "
                      + ' '.join(f'{v:+5.1f}' for v in octd[:len(OCT)]))
                results[f'{voice}.{unit}.{label}'] = dict(code=code, rate_rec=mr['rate'], rate_model=mm['rate'], cents=c,
                                                         ncc_rec=mr['ncc'], ncc_model=mm['ncc'], env_rms=erms,
                                                         t20=[mr['t20'], mm['t20']], t40=[mr['t40'], mm['t40']],
                                                         env_rec=[None if not np.isfinite(v) else float(v) for v in mr['env']],
                                                         env_model=[None if not np.isfinite(v) else float(v) for v in mm['env']],
                                                         level_diff=lvl, lsd=d, oct_diff=[float(v) for v in octd])
            cs = np.array([c for _, c in cents])
            if unit == 'ar' and not a.match and len(cs) == 11:
                print(f'clock: rms {np.sqrt(np.mean(cs ** 2)):.1f} cents; fitted (TUNE 0, 0.2 ... 1) '
                      f'{np.sqrt(np.mean(cs[0::2] ** 2)):.1f}, held out (0.1 ... 0.9) {np.sqrt(np.mean(cs[1::2] ** 2)):.1f}')
            else:
                print(f'clock: rms {np.sqrt(np.mean(cs ** 2)):.1f} cents')
            lv = np.array([results[f'{voice}.{unit}.{l}']['level_diff'] for l, _ in cents])
            print(f'level within the series (model - recording, re {ref}): rms {np.sqrt(np.mean(lv ** 2)):.2f} dB; '
                  f'lsd mean {np.mean([r[4] for r in res]):.2f} dB')
    if a.json:
        with open(a.json, 'w') as f:
            json.dump(results, f, indent=1)


if __name__ == '__main__':
    main()
