"""SPICE cases for the hi-hat's analog half (core/netlists/hh.cir; docs/circuit/hh.md).

The digital half (core/hihat.*) gives the netlist IC68's six outputs d0..d5 and RUN/STOP; here they
are made in Python the same way, so that both solvers get the same piecewise-linear inputs: every
corner on a 96 kHz grid, which the solver at 96 kHz (or any multiple) follows exactly. Two kinds:

- rom: the ROM's data at the measured 31.57 kHz clock from the first latch 24 us after the trigger,
  each D/A step band-limited as HiHat::Smoothing::Sinc does (a Kaiser-windowed sinc step at 0.45 of
  the grid's rate, 8 samples a side). All of the hats' sound is at 5-20 kHz, where the trapezoidal
  rule's warping of the output filters (26.8 kHz, 15.9 kHz) is a phase error: these cases come out
  some percent off at 96 kHz, under 1 % at 192 kHz (docs/circuit/hh.md, "SPICE").
- slow: codes alternating 32 and 40 (d3) at 1 kHz with 8-sample ramps (83 us), for the envelope, the
  accent, the knobs' ends, the choke and the mute without the high band.

The firmware's HH accent codes (docs/research.md C.4): CH 25 (normal) and 40 (accent) at TOTAL
ACCENT 0, 40 and 55 at full; OH 32, up to 47. acc = 5 V * code / 63. The trigger is one 2 ms scan
tick; ohsel (CLOSED/OPEN) is set with it and held.
"""
import math
import os

import numpy as np

RATE = 96000
VDD = 5.0
CLOCK = 31570.0
FIRST = 23.76e-6  # the first latch after a trigger from rest: 1.5 astable periods
HERE = os.path.dirname(os.path.abspath(__file__))
ROM = os.path.join(os.path.realpath(os.path.join(HERE, '..', '..', '..', 'private')), '..', 'roms', 'tr909', 'hn61256p__c43.ic69')


def acc(code):
    return 5.0 * code / 63


def rom_codes():
    return np.frombuffer(open(os.path.normpath(ROM), 'rb').read(), np.uint8) >> 2


def _sinc_step(half=8, cutoff=0.45, beta=9.0, res=256):
    x = np.linspace(-half, half, 2 * half * res + 1)
    k = 2 * cutoff * np.sinc(2 * cutoff * x) * np.i0(beta * np.sqrt(np.maximum(0, 1 - (x / half) ** 2))) / np.i0(beta)
    s = np.concatenate([[0.0], np.cumsum(0.5 * (k[1:] + k[:-1]) / res)])
    return x, s / s[-1]


def lines(changes, t1, start=32, rate=RATE, half=8):
    """IC68's outputs as HiHat gives them: `changes` is [(time, code), ...]; each bit's steps
    band-limited and delayed by `half` samples. Returns {d0..d5: PWL points on the grid}."""
    n = int(round(t1 * rate)) + 1
    t = np.arange(n) / rate
    x, s = _sinc_step(half)
    out = {}
    for b in range(6):
        v = np.full(n, ((start >> b) & 1) * VDD)
        prev = (start >> b) & 1
        for tc, code in changes:
            bit = (code >> b) & 1
            if bit == prev:
                continue
            d = (bit - prev) * VDD
            prev = bit
            m = tc * rate + half  # the step's centre, delayed
            j0, j1 = max(0, int(math.floor(m - half))), min(n, int(math.ceil(m + half)) + 1)
            j = np.arange(j0, j1)
            v[j0:j1] += d * np.interp(j - m, x, s, left=0.0, right=1.0)
            v[j1:] += d
        out[f'd{b}'] = [(float(a), float(c)) for a, c in zip(t, v)]
    return out


def rom_changes(base, t_trig, t1, rom):
    """The latch's changes for a hit at t_trig reading the ROM from `base` (0 open, 0x6000 closed:
    from 6041 on, as HiHat reads it, kClosedLead)."""
    ch, prev = [], 32
    k = 0
    while True:
        tc = t_trig + FIRST + k / CLOCK
        if tc > t1 or k >= (0x6000 if base == 0 else 0x2000):
            break
        c = int(rom[base + k] if base == 0 else rom[0x6000 + ((k + 65) & 0x1FFF)])
        if c != prev:
            ch.append((tc, c))
            prev = c
        k += 1
    return ch


def slow_lines(t_on, t1, f=1000.0, rate=RATE):
    """Codes 32 and 40 alternating at f from t_on (d5 high, d3 toggling), each step an 8-sample
    ramp on the grid."""
    pts = [(0.0, 0.0)]
    k, level = 0, VDD
    while True:
        tg = round((t_on + k * 0.5 / f) * rate) / rate
        if tg > t1:
            break
        pts += [(tg, pts[-1][1]), (tg + 8.0 / rate, level)]
        level = VDD - level
        k += 1
    return {'d5': [(0.0, VDD)], 'd3': pts}


def cases(add, h):
    rom = rom_codes()
    T = 0.01  # the trigger

    def base_inputs(code, closed, t_trig=T):
        return {'trig': h.pulse(VDD, t=t_trig, w=2e-3), 'acc': [(0.0, 0.0), (t_trig, 0.0), (t_trig + h.E, acc(code))],
                'ohsel': [(0.0, VDD), (t_trig, VDD), (t_trig + h.E, VDD if closed else 0.0)], 'run': h.hold(VDD)}

    def rom_hit(name, code, closed, seconds=0.03, controls=None):
        inputs = base_inputs(code, closed)
        inputs.update(lines(rom_changes(0x6000 if closed else 0, T, seconds, rom), seconds))
        add(name, 'hh', seconds, inputs, controls, t0=T, ttrig=T)

    rom_hit('hh_oh_32', 32, False)
    rom_hit('hh_oh_47', 47, False)
    rom_hit('hh_ch_25', 25, True)
    rom_hit('hh_ch_55', 55, True)

    def slow_hit(name, code, closed, seconds=0.3, controls=None, extra=None):
        inputs = base_inputs(code, closed)
        inputs.update(slow_lines(T, seconds))
        if extra:
            inputs.update(extra)
        add(name, 'hh', seconds, inputs, controls, t0=T, ttrig=T)

    slow_hit('hh_slow_oh', 32, False)
    slow_hit('hh_slow_ch', 40, True, 0.15)
    slow_hit('hh_slow_ch_dec0_oh0', 25, True, 0.1, {'chdecay': 0, 'ohdecay': 0})
    slow_hit('hh_slow_ch_dec1_oh1', 55, True, 0.3, {'chdecay': 1, 'ohdecay': 1, 'level': 1})
    slow_hit('hh_slow_oh_dec0_level0', 32, False, 0.15, {'ohdecay': 0, 'level': 0})
    slow_hit('hh_slow_oh_dec1', 47, False, 0.6, {'ohdecay': 1})
    # a closed hat 50 ms into an open one: TRIG again, CLOSED high
    choke = {'trig': [(0, 0.0), (T, 0.0), (T + h.E, VDD), (T + 2e-3, VDD), (T + 2e-3 + h.E, 0.0),
                      (T + 0.05, 0.0), (T + 0.05 + h.E, VDD), (T + 0.052, VDD), (T + 0.052 + h.E, 0.0)],
             'ohsel': [(0.0, VDD), (T, VDD), (T + h.E, 0.0), (T + 0.05, 0.0), (T + 0.05 + h.E, VDD)]}
    slow_hit('hh_slow_choke', 32, False, 0.2, extra=choke)
    # RUN/STOP falling 60 ms into an open hat (the counter's stop): R488 mutes the VCA
    slow_hit('hh_slow_mute', 47, False, 0.12, extra={'run': [(0.0, VDD), (T + 0.06, VDD), (T + 0.06 + 8 / RATE, 0.0)]})
