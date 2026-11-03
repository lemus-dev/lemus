#!/usr/bin/env python3
"""The hi-hat's D/A steps against ngspice: how the solver at a given rate, fed HiHat's lines
(core/hihat.*: each D/A step band-limited, averaged over the step, or the latch's level at the
step's end), reproduces the same circuit run by ngspice with every D/A step at its exact time
(docs/circuit/hh.md, "Rates and the D/A's timing").

    hhtiming.py [--hhrun PATH] [--out DIR] [--rates 96000,192000] [--smoothing none,box,sinc]
                [--hit oh:47] [--seconds 0.06] [--jobs N] [--control NAME=V ...] [--extra "ARGS"]

One ngspice deck (tr909hhrun --spice, 0.1 us steps, the D/A lines stepping in 10 ns at the logic's
times) is the reference for every case: its output is put on a grid at 8x the case's rate, low-
passed at 20 kHz (a Kaiser FIR flat to 20 kHz, -120 dB from 23 kHz) and read at the solver's
samples less the smoothing's latency. The solver's output gets the same low-pass. Per case: the
RMS difference against the reference's RMS from 2 ms after the hit, and per band (1-5, 5-10,
10-15, 15-18, 18-20 kHz) the level against the reference (dB) and the part of the difference
that no linear response explains (the coherence residual: aliases and jitter, dB re the band).
--extra passes more tr909hhrun arguments to the solver runs (--whole: no partitions).
"""
import argparse, concurrent.futures as cf, os, subprocess, sys

import numpy as np
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '../../../..'))
NGSPICE = os.environ.get('NGSPICE', '/opt/homebrew/bin/ngspice')
BANDS = [(1e3, 5e3), (5e3, 10e3), (10e3, 15e3), (15e3, 18e3), (18e3, 20e3)]
LATENCY = {'sinc': 8.0, 'box': 0.5, 'none': 0.0}


def lowpass(rate):
    n = int(120 / (2.285 * 2 * np.pi * 3000 / rate)) | 1
    return signal.firwin(n, 21500, fs=rate, window=('kaiser', 12))


def load_csv(path):
    d = np.genfromtxt(path, delimiter=',', names=True)
    return d['time'], d['out']


def reference(data, rate, over=8):
    raw = np.loadtxt(data, skiprows=1)
    t, y = raw[:, 0], raw[:, 1]
    fine = over * rate
    tg = np.arange(int(t[-1] * fine)) / fine
    yg = np.interp(tg, t, y)
    return tg, signal.filtfilt(lowpass(fine), [1.0], yg)


def compare(t, y, tref, yref, rate, lat, t0):
    y = signal.filtfilt(lowpass(rate), [1.0], y)
    r = np.interp(t - lat / rate, tref, yref)
    w = (t >= t0) & (t <= tref[-1] - 1e-3)
    e = y[w] - r[w]
    rms = float(np.sqrt(np.mean(e ** 2)) / np.sqrt(np.mean(r[w] ** 2)))
    nper = int(rate / 100)
    f, prr = signal.welch(r[w], rate, nperseg=nper)
    _, pyy = signal.welch(y[w], rate, nperseg=nper)
    _, pry = signal.csd(r[w], y[w], rate, nperseg=nper)
    inc = np.maximum(pyy - np.abs(pry) ** 2 / np.maximum(prr, 1e-300), 0)
    bands = []
    for a, b in BANDS:
        m = (f >= a) & (f < b)
        bands.append((10 * np.log10(pyy[m].sum() / prr[m].sum()), 10 * np.log10(max(inc[m].sum(), 1e-30) / prr[m].sum())))
    return rms, bands


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--hhrun', default=os.environ.get('HHRUN', os.path.join(REPO, 'build/tr909/tr909hhrun')))
    ap.add_argument('--out', default=os.path.join(REPO, 'build/tr909/hhtiming'))
    ap.add_argument('--rates', default='96000,192000')
    ap.add_argument('--smoothing', default='none,box,sinc')
    ap.add_argument('--hit', default='oh:47')
    ap.add_argument('--seconds', type=float, default=0.06)
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--control', action='append', default=[])
    ap.add_argument('--extra', default='', help='more tr909hhrun arguments for the solver runs (e.g. --whole)')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    t_hit = 0.005
    hit = ['--hit', f'{t_hit}:{a.hit}']
    ctl = sum((['--control', c] for c in a.control), [])
    common = ['--seconds', str(a.seconds)] + hit + ctl
    rates = [float(r) for r in a.rates.split(',')]
    smooth = a.smoothing.split(',')
    # the reference: one deck (its D/A times don't depend on the rate or the smoothing)
    deck, data = os.path.join(a.out, 'ref.cir'), os.path.join(a.out, 'ref.txt')
    subprocess.run([a.hhrun, os.path.join(a.out, 'ref.wav'), '--rate', '192000', '--smoothing', 'none', '--spice', deck, data,
                    '--spice-step', '1e-7'] + common, check=True, capture_output=True)
    jobs = {}
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        ref_job = ex.submit(subprocess.run, [NGSPICE, '-b', deck], capture_output=True, cwd=a.out)
        for r in rates:
            for s in smooth:
                csv = os.path.join(a.out, f'{int(r)}_{s}.csv')
                jobs[(r, s)] = (csv, ex.submit(subprocess.run, [a.hhrun, os.path.join(a.out, f'{int(r)}_{s}.wav'), '--rate', str(r),
                                                                '--smoothing', s, '--csv', csv] + common + a.extra.split(),
                                                                capture_output=True, text=True))
        ref_job.result()
    print(f'hit {a.hit} at {t_hit * 1e3:.0f} ms, {a.seconds * 1e3:.0f} ms; reference ngspice 0.1 us, exact D/A times')
    print(f'{"rate":>6s} {"lines":5s} {"rms %":>6s}  ' + '  '.join(f'{int(lo / 1e3)}-{int(hi / 1e3)}k level/alias' for lo, hi in BANDS))
    refs = {}
    for (r, s), (csv, job) in sorted(jobs.items()):
        job.result()
        if r not in refs:
            refs[r] = reference(data, r)
        t, y = load_csv(csv)
        rms, bands = compare(t, y, *refs[r], r, LATENCY[s], t_hit + 2e-3)
        print(f'{r / 1e3:5.0f}k {s:5s} {100 * rms:6.2f}  ' + '  '.join(f'{lv:+6.2f} {al:5.0f}  ' for lv, al in bands), flush=True)


if __name__ == '__main__':
    sys.exit(main())
