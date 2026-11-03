#!/usr/bin/env python3
"""The TR-909 service notes (Roland, First Edition, JUN. 15, 1984; private/docs/tr909-service-notes.pdf,
page images in private/docs/schematic/) against the model: every figure they give that the model can
be run against, as a case printing the notes' value, the model's and PASS or FAIL. Exit status 1 if
any case fails.

    check.py [--build DIR] [--only NAME,...] [--cal nominal|unit|both] [--seeds N] [--jobs N]

  --build   where tr909voicerun, tr909circuitrun, tr909hhrun and tr909seq are (default: $TR909_BUILD,
            else the first of build/tr909, build/tr909-svc that has them)
  --only    sections: spec, cpu, hihat, snare, multiout, clap, rimshot, power
  --cal     the voices' cases on the notes' parts (nominal: `tr909voicerun --cal nominal=1`, the
            netlists' own values), on the unit Lemus plays (unit: unitCalibration() in
            core/voiceboard.cpp, the AudioRealism unit), or both (default)

The notes have one adjustment (TM, the hand clap's trimmer, p.11) and no other procedure; the rest
are figures in the specifications (p.1), the circuit descriptions (p.5, p.6), the MULTI OUT
waveforms (p.9) and the sketches on the schematic (p.11). The voices run through the same
programs as the plugin: the whole voicing board (tr909voicerun, its MULTI OUT jacks, unplugged: the
jack as it stands, as SERVICE's scope probes it), one netlist with extra outputs on its inner nodes
(tr909circuitrun; the extra .output lines go in a temporary file, the netlists are untouched), the
hi-hat's logic (tr909hhrun) and the firmware on the CPU board (tr909seq).

Margins. The notes state no tolerance anywhere. Each case's margin is the spread of the parts that
set it (resistors +-5 %, film capacitors +-10 %, the regulators' +-4 %, the M5218L's output swing,
+-12 V minimum on +-15 V), written next to it; a sketch's length adds where a hand sketch stops
drawing a ring (the three rim-shot sketches end 2.3 to 4.3 of their drawn parts' time constants
after the start, +-30 %). Lengths are read to 1/10 of the first swing, as machines/tr808's
service check reads its notes' decays.

p.9 is hand-traced, without a graticule: the traces' own time scales differ by +-20 % (the BD's
last cycles, the snare's and the clap's teeth read against each other), so no level or time is read
off it, only what one trace says against its pair or itself: the accent's p-p ratio, the first
swing's sign, the clap's four teeth, the hats' and cymbals' lopsidedness, R417's effect. The traces
were read at their pen stroke's centre on the 400 dpi page image (P9 below).

Not run against the model (printed as SKIP): RESET's 7.0 V (p.6; the rails don't ramp in the
model), the tape interface's analog side and the SAVE data's timing (p.6; not emulated), the noise
generator's "32 stages" (p.6 text; the drawing has 36 and the model follows the drawing), the
hi-hat's "1/tenth" (p.5, a statement of R451/VR21 against R452/VR23), the power consumption, and
the MIDI pages (p.14-16) and the firmware's procedures, which tools/tr909seq/tests.py checks.
"""
import argparse
import concurrent.futures as cf
import os
import re
import statistics
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf
from scipy.signal import find_peaks

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
REPO = os.path.normpath(os.path.join(MACHINE, '..', '..'))
NETLISTS = os.path.join(MACHINE, 'core', 'netlists')
sys.path.insert(0, os.path.join(MACHINE, 'tools', 'tr909seq'))
import seqrun  # noqa: E402

RATE = 96000  # voicerun's host rate (the board runs at it from 88.2 kHz up)
CRATE = 192000  # circuitrun's
JACK = dict(bd=0, sd=1, lt=2, mt=3, ht=4, rs=5, hc=6, ch=7, oh=7, cr=8, rd=9)  # voicerun --jacks channels
# p.9: "Observed at MULTI OUT jack with all knobs set to center"
CENTRE = ['bdTune', 'bdLevel', 'bdAttack', 'bdDecay', 'sdTune', 'sdLevel', 'sdTone', 'sdSnappy', 'ltTune', 'ltLevel',
          'ltDecay', 'mtTune', 'mtLevel', 'mtDecay', 'htTune', 'htLevel', 'htDecay', 'rsLevel', 'hcLevel', 'hhLevel',
          'chDecay', 'ohDecay', 'crLevel', 'crTune', 'rdLevel', 'rdTune']
# The firmware's DAC codes for a step without TOTAL ACCENT (docs/firmware.md, "Accent codes"): BD-HT
# 10 normal, 21 accent; CH 25 / 40; RS and HC 21; OH, crash and ride 32.
NORMAL, ACCENT = dict(bd=10, sd=10, lt=10, ch=25), dict(bd=21, sd=21, lt=21, ch=40)

# p.9 read on the 400 dpi page image (private/docs/schematic/p09-multi-out-waveforms-switch-board-pcb.png):
# each trace's extent above and below its axis line, px, less the pen's 5 px stroke (its centre).
# Boxes (x0, y0, x1, y1): BD 1150,1290-2420,1630 and 1150,1650-2420,1800; SD 1160,1990-2420,2200 and
# 1160,2270-2420,2390; LT 3060,1320-4320,1530 and 3060,1600-4320,1760 (the "noise" arrow's columns
# 80-94 and 84-98 left out); CH 5060,1780-5360,1930 and 5060,2060-5370,2170; OH 5060,2270-5400,2390;
# crash 6200,1290-6740,1510; ride 6200,1690-6740,1900.
P9_PP = dict(bd=(279, 106), sd=(190, 91), lt=(153, 78), ch=(116, 68))  # (with accent, w/o accent)
P9_LOPSIDED = {'ch': (76.5, 39.5), 'ch25': (37.5, 30.5), 'oh': (59.5, 29.5), 'cr': (119.5, 66.5), 'rd': (104.5, 57.5)}

RESULTS = []
SKIPS = []


def case(src, name, expected, measured, ok, why):
    RESULTS.append(bool(ok))
    print(f"  {'PASS' if ok else 'FAIL'}  {src:<5s} {name:<62s} {expected:<22s} {measured:<36s} {why}")


def skip(src, name, why):
    SKIPS.append(name)
    print(f"  SKIP  {src:<5s} {name:<62s} {why}")


def within(x, ref, rel):
    return abs(x / ref - 1) <= rel


# ---------------------------------------------------------------------------------------------
# the programs

class Tools:
    def __init__(self, build, jobs):
        self.voicerun = os.path.join(build, 'tr909voicerun')
        self.circuitrun = os.path.join(build, 'tr909circuitrun')
        self.hhrun = os.path.join(build, 'tr909hhrun')
        seqrun.SEQ = os.path.join(build, 'tr909seq')
        self.pool = cf.ThreadPoolExecutor(jobs)
        self.tmp = tempfile.mkdtemp(prefix='tr909svc')
        self.n = 0

    def path(self, name):
        self.n += 1
        return os.path.join(self.tmp, f'{self.n}_{name}')

    def voice(self, voice, code, seconds, cal, extra=(), t0=0.05):
        """One hit through the voicing board, knobs at centre: its MULTI OUT jack from the hit, volts."""
        wav, jacks = self.path('m.wav'), self.path('j.wav')
        args = [self.voicerun, wav, '--rate', str(RATE), '--seconds', f'{t0 + seconds:.4f}', '--hit',
                f'{t0:.4f}:{voice}:{code}', '--jacks', jacks]
        for k in CENTRE:
            args += ['--set', f'{k}=0.5']
        for c in cal.voicerun + list(extra):
            args += ['--cal', c]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(' '.join(args) + '\n' + r.stderr)
        x, _ = sf.read(jacks)
        os.remove(wav)
        os.remove(jacks)
        return x[int(round(t0 * RATE)):, JACK[voice]]

    def submit_voice(self, *a, **k):
        return self.pool.submit(self.voice, *a, **k)

    def circuit(self, netlist, seconds, inputs, outputs=(), params=None, controls=None, rate=CRATE):
        """One netlist (with models.cir) through the solver, extra .output lines on inner nodes: {name: array}."""
        extra = self.path('outputs.cir')
        with open(extra, 'w') as f:
            for o in outputs:
                f.write(f'.output {o} {o}\n')
        csv = self.path('out.csv')
        args = [self.circuitrun, ','.join([os.path.join(NETLISTS, 'models.cir'), os.path.join(NETLISTS, netlist), extra]),
                '--rate', str(rate), '--seconds', str(seconds), '--out', csv]
        for k, v in (params or {}).items():
            args += ['--param', f'{k}={v}']
        for k, v in (controls or {}).items():
            args += ['--control', f'{k}={v}']
        for k, v in inputs.items():
            args += ['--input', f'{k}={v}']
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(' '.join(args) + '\n' + r.stderr)
        d = np.genfromtxt(csv, delimiter=',', names=True)
        os.remove(csv)
        os.remove(extra)
        return {k: np.asarray(d[k]) for k in d.dtype.names}

    def hihat(self, kind, seconds, clock=None):
        """A hi-hat hit at 10 ms through tr909hhrun: IC68's and RUN/STOP's changes, (time, code, run) rows."""
        log, wav = self.path('hh.csv'), self.path('hh.wav')
        args = [self.hhrun, wav, '--rate', str(RATE), '--seconds', str(seconds), '--hit', f'0.01:{kind}', '--log', log]
        if clock:
            args += ['--clock', str(clock)]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(' '.join(args) + '\n' + r.stderr)
        rows = np.genfromtxt(log, delimiter=',', skip_header=1)
        os.remove(log)
        os.remove(wav)
        return rows


class Cal:
    """A calibration: voicerun's --cal entries, the netlists' .params by voice, the hi-hat's clock."""

    def __init__(self, name, voicerun, parts, hh_clock):
        self.name, self.voicerun, self.parts, self.hh_clock = name, voicerun, parts, hh_clock

    def params(self, voice):
        return {k.split('.', 1)[1]: v for k, v in self.parts.items() if k.startswith(voice + '.')}


def unit_calibration():
    """unitCalibration()'s part values and hi-hat clock, read from core/voiceboard.cpp."""
    src = open(os.path.join(MACHINE, 'core', 'voiceboard.cpp')).read()
    body = src[src.index('Calibration unitCalibration()'):]
    body = body[:body.index('return c;')]
    parts = {k: float(v) for k, v in re.findall(r'\{"(\w+\.\w+)",\s*([-+0-9.eE]+)\}', body)}
    m = re.search(r'c\.hhClockHz\s*=\s*([0-9.eE+]+)', body)
    return Cal('unit', [], parts, float(m.group(1)) if m else None)


def pulse(t0, width=2e-3, high=5.0):
    """A PWL input: low, high from t0 for width (a trigger line), low again."""
    e = 1e-7
    return f'0,0,{t0},0,{t0 + e},{high},{t0 + width},{high},{t0 + width + e},0'


def first_swing_pp(x, period_samples):
    a = np.abs(x)
    start = int(np.argmax(a > 0.05 * a.max()))
    return start, float(np.ptp(x[start:start + period_samples]))


def length_to_tenth(x, fs, period_samples):
    """From the onset to where the swing (p-p over one period) has fallen to 1/10 of the first."""
    start, ref = first_swing_pp(x, period_samples)
    hop = max(1, period_samples // 8)
    for i in range(start, len(x) - period_samples, hop):
        if np.ptp(x[i:i + period_samples]) < ref / 10:
            return (i - start + period_samples) / fs
    return float('nan')


def period(x, fs, start, span):
    """The median period between rising zero crossings over [start, start + span) seconds."""
    s = x[int(start * fs):int((start + span) * fs)]
    up = np.where((s[:-1] < 0) & (s[1:] >= 0))[0]
    frac = up + s[up] / (s[up] - s[up + 1])  # linear interpolation to the crossing
    return float(np.median(np.diff(frac))) / fs


# ---------------------------------------------------------------------------------------------
# p.1 specifications

def spec(T, cals):
    print('\nSpecifications (SN p.1)')
    # Tempo: the firmware on the CPU board, TEMPO at its two ends, a BD on every 16th
    all16 = ' '.join(str(i) for i in range(1, 17))
    widths = []
    for pot, secs, want in ((0, 10, 37), (1, 3, 290)):
        r = seqrun.Run(f'tempo {pot};run 1;SHIFT+PATT1;CLEAR+1;press START;INST+2;p {all16};run 0.5;log hits;echo a;'
                       f'run {secs};echo b')
        t = [h[0] for h in r.hits('BD', 'a', 'b')]
        bpm = 60000.0 / (4 * (t[-1] - t[0]) / (len(t) - 1))
        widths += r.widths('a', 'b')
        case('p.1', f"Tempo, TEMPO fully {'anti' if pot == 0 else ''}clockwise", f'{want} BPM +-1.14', f'{bpm:.2f} BPM',
             abs(bpm - want) <= 1.14, "one of the firmware's tempo steps (1.14 BPM: B = 32 + 225 A/D / 256); "
             'the figures are its display (A/D + 36)')
    T.trig_widths = widths

    # TRIGGER OUT (RIM SHOT): rs.cir's trigout, the DIN jack board's tip (C600 on it), unloaded
    d = T.circuit('rs.cir', 0.06, dict(trig=pulse(0.005), acc=f'0,{5 * 21 / 63}'), outputs=('t', 'b15', 'nb15'))
    T.rs_nominal = d
    v = d['trigout']
    peak = float(v.max())
    above = np.where(v > peak / 2)[0]
    width = (above[-1] - above[0] + 1) / CRATE
    case('p.1', 'Trigger Out (Rim Shot), its level', '+14 V +-5 %', f'{peak:+.2f} V', within(peak, 14, 0.05),
         "the 7815's +-4 % and Q67's saturation; R412/R413 22k/1k (+-5 %) move it 0.2 %")
    case('p.1', 'Trigger Out (Rim Shot), its pulse (at half height)', '20 ms +-15 %', f'{width * 1e3:.1f} ms',
         within(width, 0.020, 0.15), 'C114 0.1u (+-10 %) recharging through R409 47k (+-5 %)')

    # Master Out [6 Vp-p, 1 kOhm]: read as the largest swing into 1 kOhm. mix.cir, MASTER VOLUME full,
    # a +-10 V triangle into the BD's input: IC66 at its rails, through R474 1k, R530 2.2k, R531 10k
    tri = ['0', '0']
    for k in range(int(0.3 * 1000) + 1):
        tri += [f'{k / 1000:.6f}', '10' if k % 2 else '-10']
    d = T.circuit('mix.cir', 0.3, dict(pon='0,15', bd=','.join(tri)), params=dict(rload='1k'), controls=dict(volume=1),
                  rate=RATE)
    s = slice(int(0.2 * RATE), None)
    pl, pr = float(np.ptp(d['l'][s])), float(np.ptp(d['r'][s]))
    case('p.1', 'Master Out, the largest swing into 1 kOhm (L, R)', '6 Vp-p +-12 %', f'{pl:.2f}, {pr:.2f} Vp-p',
         within(pl, 6, 0.12) and within(pr, 6, 0.12),
         "IC66's swing (M5218L: +-12 V min, +-14 typ on +-15 V) and R474/R530/R531 (+-5 %); "
         'the 1 kOhm read as the load (the drawn parts give 2.42 kOhm out)')


# ---------------------------------------------------------------------------------------------
# p.5, p.6: the CPU board (firmware)

def cpu(T, cals):
    print('\nCPU board and firmware (SN p.5, p.6)')
    w = getattr(T, 'trig_widths', None)
    if not w:
        all16 = ' '.join(str(i) for i in range(1, 17))
        r = seqrun.Run(f'analog 0 4c;run 1;SHIFT+PATT1;CLEAR+1;press START;INST+2;p {all16};run 0.5;log hits;run 4')
        w = r.widths()
    case('p.5', 'TRIG: a pulse "for 2ms" (one scan), its length', '2 ms +-0.5', f'{min(w):.2f}-{max(w):.2f} ms (mean '
         f'{statistics.mean(w):.2f}, {len(w)})', all(1.5 <= x <= 2.5 for x in w),
         'the figure to its millisecond (the scan is the firmware\'s, on the 12 MHz crystal)')
    d = getattr(T, 'rs_nominal', None) or T.circuit('rs.cir', 0.06, dict(trig=pulse(0.005), acc=f'0,{5 * 21 / 63}'),
                                                     outputs=('t', 'b15', 'nb15'))
    hi = float(d['t'][int(0.006 * CRATE)])
    case('p.5', 'TRIG: "5V positive going" (IC10 into the rim shot)', '5 V +-5 %', f'{hi:.3f} V', within(hi, 5, 0.05),
         "the 7805's +-4 % and the 4174's output under its load")

    # TAPE SYNC on PC4 (p.6): 1200 Hz while stopped; 1200 and 2400 Hz alternately while playing,
    # changing every half period of T, 1/24 of a quarter note
    r = seqrun.Run('analog 0 4c;run 1;tape 0.5;SHIFT+PATT1;CLEAR+1;press START;run 0.3;tape 1')
    h = [dict((int(a), int(b)) for a, b in re.findall(r'(\d+) us x(\d+)', s)) for t, k, s in r.lines
         if k == 'TEXT' and s.startswith('TAPE SYNC')]

    def near(hist, us):
        return {k: v for k, v in hist.items() if abs(k / us - 1) <= 0.1}

    stop = near(h[0], 416)
    f_stop = 1e6 / (2 * sum(k * v for k, v in stop.items()) / sum(stop.values()))
    lo, hi = near(h[1], 416), near(h[1], 208)
    f_lo = 1e6 / (2 * sum(k * v for k, v in lo.items()) / sum(lo.values()))
    f_hi = 1e6 / (2 * sum(k * v for k, v in hi.items()) / sum(hi.values()))
    t_lo, t_hi = sum(k * v for k, v in lo.items()), sum(k * v for k, v in hi.items())
    share = t_hi / (t_lo + t_hi)
    case('p.6', 'TAPE SYNC stopped: a continual 1200 Hz', '1200 Hz +-50', f'{f_stop:.0f} Hz '
         f'({sum(stop.values())} of {sum(h[0].values())} half periods)', abs(f_stop - 1200) <= 50 and
         sum(stop.values()) >= 0.95 * sum(h[0].values()), 'the figure to its 100 Hz (a timer on the crystal)')
    case('p.6', 'TAPE SYNC playing: 1200 and 2400 Hz', '1200, 2400 Hz +-50', f'{f_lo:.0f}, {f_hi:.0f} Hz',
         abs(f_lo - 1200) <= 50 and abs(f_hi - 2400) <= 50, 'as above')
    case('p.6', 'TAPE SYNC playing: alternately, half the time each', '2400 Hz 50 % +-5', f'{100 * share:.1f} %',
         abs(share - 0.5) <= 0.05, 'a half period at each change, about 0.4 of 11 ms (T/2 at 112.6 BPM)')
    skip('p.6', 'RESET at 7.0 V on IC703\'s input', 'the model\'s rails are constants (no ramp, no brown-out)')
    skip('p.6', 'Tape: LOAD\'s 300 us, SAVE\'s 3.2 kbaud, 500/125 us, 170/350 mV', 'the tape interface isn\'t emulated')
    skip('p.6', 'NOISE: "two shift registers ... making up 32 stages"', 'the drawing has 36 (x^31 + x^13 + 1) and the model '
         'follows it (docs/circuit/noise.md)')


# ---------------------------------------------------------------------------------------------
# p.5: the hi-hat's clock and address counters

def hihat(T, cals):
    print('\nHi-hat (SN p.5: "about 60kHz, which is divided by two"; ADDRESS TABLE)')
    for cal in cals:
        o = T.pool.submit(T.hihat, 'oh', 1.0, cal.hh_clock)
        c = T.pool.submit(T.hihat, 'ch', 0.4, cal.hh_clock)
        out = {}
        for kind, f in (('oh', o), ('ch', c)):
            rows = f.result()
            t = rows[:, 0]
            d = np.diff(t)
            one = d[(d > 0) & (d < 1.5 * d[d > 0].min())]
            clock = 1 / float(np.median(one))
            stop = t[np.argmax(rows[:, 2] == 0)] if (rows[:, 2] == 0).any() else float('nan')
            out[kind] = (clock, (stop - 0.01) * clock)
        clock = out['oh'][0]
        case('p.5', f'[{cal.name}] the clock oscillator (IC72c, d): twice the sample clock', '60 kHz +-15 %',
             f'{2 * clock / 1e3:.2f} kHz (samples {clock / 1e3:.3f} kHz)', within(2 * clock, 60e3, 0.15),
             '"about": R492 12k (+-5 %), C160 470p (+-5 %) and the 4011UB\'s thresholds')
        n_open, n_closed = out['oh'][1], out['ch'][1]
        case('p.5', f'[{cal.name}] OPEN: the counter stops at 110 0000 0000 0000', '24576 samples +-1',
             f'{n_open:.1f}', abs(n_open - 0x6000) <= 1, 'logic: to a sample clock')
        case('p.5', f'[{cal.name}] CLOSED: the counter stops at 010 0000 0000 0000', '8192 samples +-1',
             f'{n_closed:.1f}', abs(n_closed - 0x2000) <= 1, 'logic: to a sample clock')
    skip('p.5', 'CLOSED: R451 + VR21 "1/tenth" of R452 + VR23', 'a statement of the parts (110k, 1.1M), not a figure '
         'the circuit makes')


# ---------------------------------------------------------------------------------------------
# p.6, p.11: the snare's VCO supplies and shapers

def snare(T, cals):
    print('\nSnare drum (SN p.6: IC36\'s V_DD and V_SS; p.11: VCO-1, VCO-2 "0.6V"), TUNE at centre')
    hit = 0.1
    jobs = {cal.name: T.pool.submit(T.circuit, 'sd.cir', 0.2, dict(trig=pulse(hit), acc=f'0,{5 * 10 / 63}',
                                                                      noise='0,7.5'),
                                    outputs=('vss', 'sh1', 'sh2'), params=cal.params('sd')) for cal in cals}
    for cal in cals:
        d = jobs[cal.name].result()
        vdd, vss = d['vdd'], d['vss']
        i0 = int((hit - 0.001) * CRATE)
        rest = float(vdd[i0])
        ip = int(np.argmax(vdd))
        peak = float(vdd[ip])
        back = ip + int(np.argmax(np.abs(vdd[ip:] - rest) < (peak - rest) / 10))
        ret = (back - ip) / CRATE
        case('p.6', f'[{cal.name}] V_DD at rest', '2 V +-10 %', f'{rest:.3f} V', within(rest, 2, 0.10),
             'TUNE\'s divider R287/VR6/R284 (+-5 %, the pot +-20 %), through R249 (the figure gives no TUNE)')
        case('p.6', f'[{cal.name}] V_DD at the trigger\'s end (2 ms)', '5 V +-10 %', f'{peak:.3f} V at '
             f'{(ip / CRATE - hit) * 1e3:.2f} ms', within(peak, 5, 0.10),
             'ENV1 (the PULSE line) through R248 470k against R249 100k (+-5 %)')
        case('p.6', f'[{cal.name}] V_DD back at rest "20ms" after (to 1/10)', '20 ms +-45 %', f'{ret * 1e3:.1f} ms',
             within(ret, 0.020, 0.45), 'a sketch\'s length (+-30 %) and C66 (R248 + R249) (+-15 %)')
        mr, mp = float(vss[i0] / vdd[i0]), float(vss[ip] / vdd[ip])
        case('p.6', f'[{cal.name}] V_SS mirrors V_DD (-2 V, -5 V)', 'V_SS / V_DD -1 +-10 %', f'{mr:.3f} at rest, '
             f'{mp:.3f} at the peak', within(mr, -1, 0.10) and within(mp, -1, 0.10), 'IC35b\'s R251/R252 100k (+-5 %)')
        s = slice(int(hit * CRATE), None)
        a1, a2 = float(np.abs(d['sh1'][s]).max()), float(np.abs(d['sh2'][s]).max())
        case('p.11', f'[{cal.name}] VCO-1, VCO-2 after their shapers (D65/D66, D69/D70)', '0.6 V +-0.1',
             f'{a1:.3f}, {a2:.3f} V peak', abs(a1 - 0.6) <= 0.1 and abs(a2 - 0.6) <= 0.1,
             'the 1SS133\'s forward voltage at 0.1-1 mA')


# ---------------------------------------------------------------------------------------------
# p.9: the MULTI OUT waveforms, all knobs at centre

def multiout(T, cals):
    print('\nMULTI OUT waveforms (SN p.9, all knobs at centre; hand traces read against their pairs, see the top)')
    for cal in cals:
        jobs = {}
        for v, sec in (('bd', 0.6), ('sd', 0.4), ('lt', 0.6), ('ch', 0.4)):
            for code in (NORMAL[v], ACCENT[v]):
                jobs[(v, code)] = T.submit_voice(v, code, sec, cal)
        for v, sec in (('oh', 1.0), ('cr', 2.0), ('rd', 2.0)):
            jobs[(v, 32)] = T.submit_voice(v, 32, sec, cal)
        jobs[('rs', 3.3)] = T.submit_voice('rs', 21, 0.1, cal)
        jobs[('rs', 12)] = T.submit_voice('rs', 21, 0.1, cal, extra=['rs.r417=12k'])
        x = {k: f.result() for k, f in jobs.items()}
        for v in ('bd', 'sd', 'lt', 'ch'):
            a, b = float(np.ptp(x[(v, ACCENT[v])])), float(np.ptp(x[(v, NORMAL[v])]))
            ref = P9_PP[v][0] / P9_PP[v][1]
            case('p.9', f'[{cal.name}] {v.upper()}: with accent against w/o (p-p, codes {ACCENT[v]}/{NORMAL[v]})',
                 f'{ref:.2f} +-20 %', f'{a / b:.2f} ({a:.2f} / {b:.2f} Vp-p)', within(a / b, ref, 0.20),
                 'the tracing (+-5 %), the accent DAC\'s and the VCA\'s parts, units\' spread')
        for v, want, what in (('bd', -1, 'negative'), ('sd', -1, 'negative'), ('lt', 1, 'positive (the "noise")')):
            signs, said = [], []
            for code in (NORMAL[v], ACCENT[v]):
                s = x[(v, code)]
                i = int(np.argmax(np.abs(s) > 0.5 * np.abs(s).max()))
                signs.append(int(np.sign(s[i])))
                said.append(f'{s[i]:+.2f} V at {i / RATE * 1e3:.1f} ms')
            case('p.9', f'[{cal.name}] {v.upper()}: the first swing over half the largest', f'{what}, both',
                 ', '.join(said) + ' (w/o, with)', all(s == want for s in signs),
                 'the sign (p.9\'s traces read the same way)')
        for k, key, name in (('ch', ('ch', 40), 'CH with accent'), ('ch25', ('ch', 25), 'CH w/o accent'),
                             ('oh', ('oh', 32), 'OH'), ('cr', ('cr', 32), 'crash'), ('rd', ('rd', 32), 'ride')):
            s = x[key]
            ref = P9_LOPSIDED[k][0] / P9_LOPSIDED[k][1]
            lop = float(s.max() / -s.min())
            case('p.9', f'[{cal.name}] {name}: positive peak against negative', f'{ref:.2f} +-20 %', f'{lop:.2f}',
                 within(lop, ref, 0.20), 'the tracing, the (S) VCA transistor\'s law, units\' spread')

        def f3_share(s):
            s = s[:int(0.010 * RATE)]
            spec = np.abs(np.fft.rfft(s * np.hanning(len(s)), 1 << 16)) ** 2
            f = np.fft.rfftfreq(1 << 16, 1 / RATE)
            return float(spec[(f > 700) & (f < 1500)].sum() / spec[f > 100].sum())
        a, b = f3_share(x[('rs', 3.3)]), f3_share(x[('rs', 12)])
        case('p.9', f'[{cal.name}] RS: R417 12K against 3.3K, F3\'s 1 kHz in the first 10 ms', 'less with 12K',
             f'{100 * b:.0f} % against {100 * a:.0f} %', b < a, 'the direction (fewer 1 kHz cycles drawn)')


# ---------------------------------------------------------------------------------------------
# p.11: the hand clap's ADJUSTMENT (TM), and p.9's clap

def clap(T, cals, seeds):
    print('\nHand clap (SN p.11 ADJUSTMENT: "LEVEL: Center, MULTI OUT 2Vp-p"; p.9: four teeth)')
    for cal in cals:
        # code 21: the clap's level on a step without TOTAL ACCENT; another stretch of the noise each run
        runs = [T.submit_voice('hc', 21, 0.15, cal, t0=0.02 + 0.0173 * s) for s in range(seeds)]
        x = [f.result() for f in runs]
        pp = [float(np.ptp(s)) for s in x]
        drawn = float(re.search(r'\btm=([0-9.]+)', open(os.path.join(NETLISTS, 'hc.cir')).read()).group(1))
        tm = f"TM {drawn:g}, hc.cir's" if cal.name == 'nominal' else f"TM {cal.parts.get('hc.tm', drawn):.2f}, the unit's"
        case('p.11', f'[{cal.name}] MULTI OUT p-p, LEVEL at centre ({tm})', '2.0 Vp-p +-0.2',
             f'{statistics.mean(pp):.2f} Vp-p ({min(pp):.2f}-{max(pp):.2f}, {seeds} runs)',
             abs(statistics.mean(pp) - 2) <= 0.2, 'a scope at 500 mV/div read to a minor division (0.1 V) on teeth '
             'that vary with the noise, and its +-3 %')
        # the teeth come at fixed times and the noise's peaks don't: |x|'s peak in each 0.5 ms, over
        # the runs (a scope's persistence), its peaks over 40 % of the largest, 5 ms apart, in 45 ms
        w = int(0.0005 * RATE)
        n = int(0.045 * RATE) // w * w
        env = np.mean([np.abs(s[:n]).reshape(-1, w).max(axis=1) / np.abs(s[:n]).max() for s in x], axis=0)
        pk, _ = find_peaks(np.concatenate(([0.0], env)), height=0.4 * env.max(), distance=int(0.005 * RATE / w))
        case('p.9', f'[{cal.name}] the teeth before the tail', '4', f'{len(pk)}, at ' + ', '.join(
             f'{(p - 0.5) * w / RATE * 1e3:.1f}' for p in pk) + ' ms', len(pk) == 4,
             f'a count: |x|\'s peaks over {seeds} runs, over 40 % of the largest')


# ---------------------------------------------------------------------------------------------
# p.11: the rim shot's resonators F1-F3

def rimshot(T, cals):
    print('\nRim shot resonators (SN p.11 sketches F1-F3, at IC48a, IC49a, IC49b)')
    sketch = (('F1', 'o48a', 2e-3, 30, 20e-3), ('F2', 'o49a', 4.5e-3, 15, 25e-3), ('F3', 'o49b', 1e-3, 30, 5e-3))
    t0 = 0.005
    jobs = {cal.name: T.pool.submit(T.circuit, 'rs.cir', 0.08, dict(trig=pulse(t0), acc=f'0,{5 * 21 / 63}'),
                                    outputs=[s[1] for s in sketch], params=cal.params('rs')) for cal in cals}
    for cal in cals:
        d = jobs[cal.name].result()
        for name, node, T_, vpp, length in sketch:
            x = d[node][int(t0 * CRATE):]
            per = period(x, CRATE, 0.004, 0.02 if name != 'F3' else 0.005)
            pp = float(np.ptp(x))
            ln = length_to_tenth(x, CRATE, int(round(per * CRATE)))
            case('p.11', f'[{cal.name}] {name}: T{name[1]}, its period', f'{T_ * 1e3:g} ms +-15 %', f'{per * 1e3:.3f} ms',
                 within(per, T_, 0.15), 'f0 = 1/(2 pi C sqrt(R1 R2)): C +-10 %, R +-5 %')
            if vpp == 30:
                case('p.11', f'[{cal.name}] {name}: its swing (rail to rail)', '24-30 Vp-p', f'{pp:.2f} Vp-p',
                     24 <= pp <= 30, 'the +-15 V rails; the M5218L swings +-12 V at least')
            else:
                case('p.11', f'[{cal.name}] {name}: its swing', f'{vpp} Vp-p +-20 %', f'{pp:.2f} Vp-p',
                     within(pp, vpp, 0.20), 'the kick (C111 +-10 %, R395/R396) and the gain R1/2R2 (+-10 %)')
            case('p.11', f'[{cal.name}] {name}: its length (to 1/10)', f'{length * 1e3:g} ms +-45 %', f'{ln * 1e3:.1f} ms',
                 within(ln, length, 0.45), 'a sketch\'s length (+-30 %) and tau = R1 C (+-15 %)')


# ---------------------------------------------------------------------------------------------
# p.12: the power supply

def power(T, cals):
    print('\nPower supply (SN p.12: IC701 7815, IC702 7915, IC703 7805)')
    d = getattr(T, 'rs_nominal', None) or T.circuit('rs.cir', 0.06, dict(trig=pulse(0.005), acc=f'0,{5 * 21 / 63}'),
                                                     outputs=('t', 'b15', 'nb15'))
    h = T.circuit('hh.cir', 0.001, {}, outputs=('v5',))
    p, n, f = float(d['b15'].mean()), float(d['nb15'].mean()), float(h['v5'].mean())
    case('p.12', 'the rails +15 V, -15 V, +5 V', '+15, -15, +5 V +-4 %', f'{p:+.3f}, {n:+.3f}, {f:+.3f} V',
         within(p, 15, 0.04) and within(n, -15, 0.04) and within(f, 5, 0.04), 'the regulators\' +-4 % (the model\'s '
         'rails are ideal)')
    skip('p.1', 'Power consumption 14 W', 'not modelled')


SECTIONS = [('spec', spec), ('cpu', cpu), ('hihat', hihat), ('snare', snare), ('multiout', multiout),
            ('clap', None), ('rimshot', rimshot), ('power', power)]


def find_build():
    env = os.environ.get('TR909_BUILD')
    if env:
        return env
    for d in ('tr909', 'tr909-svc'):
        p = os.path.join(REPO, 'build', d)
        if all(os.path.exists(os.path.join(p, t)) for t in ('tr909voicerun', 'tr909circuitrun', 'tr909hhrun', 'tr909seq')):
            return p
    return os.path.join(REPO, 'build', 'tr909')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--build', default=None)
    ap.add_argument('--only', default='')
    ap.add_argument('--cal', choices=('nominal', 'unit', 'both'), default='both')
    ap.add_argument('--seeds', type=int, default=8)
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()
    build = os.path.abspath(a.build or find_build())
    T = Tools(build, a.jobs)
    nominal = Cal('nominal', ['nominal=1'], {}, None)
    cals = [c for c in (nominal, unit_calibration()) if a.cal in ('both', c.name)]
    only = set(a.only.split(',')) if a.only else None
    print(f'TR-909 service notes (First Edition, JUN. 15, 1984) against the model ({build})')
    print('Calibrations: ' + ', '.join({'nominal': 'nominal = the notes\' parts', 'unit': 'unit = the AudioRealism unit '
                                        'Lemus plays'}[c.name] for c in cals))
    print(f"  {'':4s}  {'SN':<5s} {'case':<62s} {'the notes':<22s} {'the model':<36s} margin: its reason")
    for name, fn in SECTIONS:
        if only and name not in only:
            continue
        if name == 'clap':
            clap(T, cals, a.seeds)
        else:
            fn(T, cals)
    print('\nNot here: the MIDI pages (SN p.14-16) and the firmware\'s procedures: tools/tr909seq/tests.py')
    fails = RESULTS.count(False)
    print(f'\n{RESULTS.count(True)} of {len(RESULTS)} pass, {fails} fail; {len(SKIPS)} not checkable on the model')
    try:
        os.rmdir(T.tmp)
    except OSError:
        pass
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
