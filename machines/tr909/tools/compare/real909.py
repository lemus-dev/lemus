#!/usr/bin/env python3
"""The TR-909 reference recordings and the measurements every compare script shares.

The recordings live in the private folder (private/reference/real909/, index.md there says what
each set is); this module finds a clip by voice and knob settings, loads it, trims it to its
onset, and measures it. The per-voice scripts (bd.py, sd.py, ...) and the final validation use
it on the recordings and on our renders alike, so both are measured the same way.

Knob values are pot rotations 0-1 everywhere: AudioRealism's percentages / 100, Rob Roy's panel
dial / 10, Roland's plugin parameters as rendered. Voices are BD SD LT MT HT RS HC CH OH CR RD.

Sources (`source=`):

  ar        AudioRealism TR-909 Ultimate (2025): one late unit, 96 kHz/24-bit, individual outs,
            LEVEL 50 %. BD and SD on a 5x5x5 grid, toms 11x11, CH DECAY, CR/RD TUNE, 11 "accent"
            levels per voice (accent=0..1), flams, shots; OH DECAY only in the 44.1 kHz edition.
  rr        Rob Roy Recordings (1995): a second unit, dial 0/3/7/10 (hats and cymbals 0-10 in 2s),
            44.1 kHz/16-bit through an E-mu ESI-32, normalised, tails trimmed.
  regebro   Lennart Regebro's rev-4 909 (and his RD-9), every voice at 4 accent states (accent=1..4:
            none, TOTAL only, local only, both; the order is inferred), individual and main outs
            (config=tr909_individual_out, ...), 128 kbps MP3, knobs not stated.
  kb6, esw  archive.org KB6 Set2/Set3 and Echo Sound Works: no settings; spot checks only.
  scherer   "living sounds" (2009): a third unit's hats, 96 kHz, mix out and individual out
            (out=mix_out|individual_out), CH and OH DECAY swept together min to max (bar=1-17
            orders the positions; dial values unknown), CH accent inferred (accent_inferred='on'|'off').
  cfclone   Colin Fraser's home-built 808/909 voice clone (2000), NOT a 909: knobs as L/M/H.
  roland    Roland's TR-909 plugin renders (private/reference/roland-vst/renders): single-knob sweeps
            around its default kit at vel 127, 48 kHz, the hit 50 ms in.
  (slug)    any other pack listed in real909/index.json, by its folder name.

  python3 real909.py list [--source ar] [--voice BD]
  python3 real909.py ref BD tune=0.5 attack=1 decay=1 [--source ar] [--accent A]
  python3 real909.py measure FILE|ref:SOURCE:VOICE:k=v,k=v [--voice BD] [--json]
  python3 real909.py compare A B [--voice BD] [--window 0.03,0.33]

FILE in measure/compare is a WAV, or a reference written as ref:ar:BD:tune=0.5,attack=1,decay=1
(ref:regebro:SD:accent=4,config=tr909_individual_out). The voice is taken from the reference, or
given with --voice for a plain WAV (or guessed from the file name).

The AR grid plays at accent DAC code 21 (CH 40, OH/CR/RD 32): an accented step at TOTAL ACCENT
0; its accentNNN takes are a TOTAL ACCENT sweep on an accented step, codes 21-47. ar_code(),
dac_code() and midi_code() give the code to render a comparison at (acc = acc_volts(code)).
"""
import argparse, glob, json, math, os, re, sys
from dataclasses import dataclass, field

import numpy as np
import soundfile as sf
from scipy import signal
from scipy.optimize import curve_fit

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
PRIVATE = os.path.join(MACHINE, 'private')
REAL909 = os.path.join(PRIVATE, 'reference', 'real909')
ROLAND_RENDERS = os.path.join(PRIVATE, 'reference', 'roland-vst', 'renders')
ROM_DIR = os.path.normpath(os.path.join(os.path.realpath(PRIVATE), '..', 'roms', 'tr909'))

VOICES = ['BD', 'SD', 'LT', 'MT', 'HT', 'RS', 'HC', 'CH', 'OH', 'CR', 'RD']
SOURCES = {'ar': 'audiorealism_tr909_ultimate', 'rr': 'zoeblade_robroy_1995',
           'kb6': 'archiveorg_drum_machines_collection', 'esw': 'echosoundworks_tr909day_2018',
           'scherer': 'scherer_909hats_2009', 'cfclone': 'colinfraser_clone_808909'}
TOMS = ('LT', 'MT', 'HT')
SAMPLED = ('CH', 'OH', 'CR', 'RD')


# ---------------------------------------------------------------------------------------------
# The catalogue

@dataclass
class Clip:
    """One recording: where it is and what was set. `knobs` are rotations 0-1 by knob name
    (tune, attack, decay, tone, snappy, ch_decay, oh_decay, level); `accent` is the source's own
    accent value (ar: the pack's accent000-100 as 0-1; regebro: 1-4; roland: velocity), None for
    a plain grid hit; `extra` holds the rest (flam, shot, shuffle, config, ...)."""
    path: str
    source: str
    voice: str
    knobs: dict = field(default_factory=dict)
    accent: object = None
    extra: dict = field(default_factory=dict)
    lossy: bool = False
    notes: str = ''

    def __str__(self):
        k = ' '.join(f'{n}={v:g}' for n, v in sorted(self.knobs.items()))
        a = f' accent={self.accent}' if self.accent is not None else ''
        e = ''.join(f' {n}={v}' for n, v in sorted(self.extra.items()))
        return f'{self.source}:{self.voice} {k}{a}{e}  {os.path.relpath(self.path, REAL909) if self.path.startswith(REAL909) else self.path}'


_catalogue = None


def _knobs_from_index(ps):
    """index.json's parsed_settings -> (knobs, accent, extra)."""
    knobs, extra, accent = {}, {}, None
    for k, v in ps.items():
        k = k.rstrip('?')
        if k == 'accent_pct':
            accent = v / 100
        elif k.endswith('_pct'):
            knobs[k[:-4]] = v / 100
        elif k.endswith('_dial'):
            knobs[k[:-5]] = v / 10
        elif k in ('flam_mode', 'shuffle_mode'):
            extra[k[:-5]] = v
        else:
            extra[k] = v
    return knobs, accent, extra


def catalogue():
    """Every clip we know: the packs in real909/index.json, Regebro's cut hits and Roland's
    renders. Built once, from the index and the file names (no audio is read)."""
    global _catalogue
    if _catalogue is not None:
        return _catalogue
    out = []
    idx = os.path.join(REAL909, 'index.json')
    if os.path.exists(idx):
        d = json.load(open(idx))
        rev = {v: k for k, v in SOURCES.items()}
        for c in d['packs']['clips']:
            voice = c['instrument']
            if voice not in VOICES:
                continue  # combinations (SD+HC, OH+CH) and unspecified toms
            knobs, accent, extra = _knobs_from_index(c.get('parsed_settings') or {})
            out.append(Clip(os.path.join(REAL909, c['file']), rev.get(c['source'], c['source']), voice, knobs, accent,
                            extra, bool(c.get('lossy')), '; '.join(c.get('caveats') or [])))
    hits = os.path.join(REAL909, 'videos', 'regebro_soundcloud_rd9_tr909_all_sounds_accent_levels', 'wav', 'hits')
    if os.path.exists(os.path.join(hits, 'hits.json')):
        for h in json.load(open(os.path.join(hits, 'hits.json'))):
            v = {'CP': 'HC'}.get(h['instrument'], h['instrument'])
            out.append(Clip(os.path.join(hits, h['file']), 'regebro', v, {}, h['accent_level'], {'config': h['config']}, True,
                            'acc1 none, acc2 TOTAL only, acc3 local only, acc4 both (order inferred); 128 kbps MP3'))
    if os.path.isdir(ROLAND_RENDERS):
        for p in sorted(glob.glob(os.path.join(ROLAND_RENDERS, '*.wav'))):
            n = os.path.basename(p)[:-4]
            m = re.fullmatch(r'(bd|sd|lt|mt|ht|rs|hc|ch|oh|cr|rd)-(tune|decay|attack|level|tone|snappy|hh-level)-(\d{3})', n)
            if m:
                k = 'level' if m.group(2) == 'hh-level' else m.group(2)
                out.append(Clip(p, 'roland', m.group(1).upper(), {k: int(m.group(3)) / 100}, 127, {}))
                continue
            m = re.fullmatch(r'(bd|sd|lt|mt|ht|rs|hc|ch|oh|cr|rd)-default', n)
            if m:
                out.append(Clip(p, 'roland', m.group(1).upper(), {}, 127, {'default': True}))
                continue
            m = re.fullmatch(r'vel-(\w\w)-(\d+)', n)
            if m:
                out.append(Clip(p, 'roland', m.group(1).upper(), {}, int(m.group(2)), {}))
                continue
            m = re.fullmatch(r'acc-(\w\w)-ta(\d{3})-(acc|norm)', n)
            if m:
                out.append(Clip(p, 'roland', m.group(1).upper(), {'total_accent': int(m.group(2)) / 100},
                                127 if m.group(3) == 'acc' else 64, {}))
    _catalogue = out
    return out


ROLAND_DEFAULT = {'tune': 0.5, 'decay': 0.5, 'attack': 0.5, 'tone': 0.5, 'snappy': 0.5}


def find(voice, source='ar', accent=None, any_accent=False, **want):
    """The clips of `voice` from `source` whose knobs match `want` (rotations, within 0.005).
    Extra fields (flam=, shot=, config=, ...) match `extra`. By default only plain hits are
    returned (no accent value, no flam/shot/shuffle): pass accent= to get an accent take, or
    any_accent=True for all. Roland's renders vary one knob from its default kit: a knob not in
    a render's name is taken at its default 0.5."""
    voice = voice.upper()
    res = []
    for c in catalogue():
        if c.voice != voice or c.source != source:
            continue
        if not any_accent and source != 'roland':
            if accent is None and (c.accent is not None or any(k in c.extra for k in ('flam', 'shot', 'shuffle'))):
                if not any(k in want for k in ('flam', 'shot', 'shuffle')):
                    continue
            if accent is not None and c.accent != accent:
                continue
        knobs = dict(ROLAND_DEFAULT, **c.knobs) if source == 'roland' else c.knobs
        ok = True
        for k, v in want.items():
            if k in knobs:
                ok = abs(knobs[k] - float(v)) < 0.005
            elif k in c.extra:
                try:
                    ok = float(c.extra[k]) == float(v)
                except (TypeError, ValueError):
                    ok = str(c.extra[k]) == str(v)
            else:
                ok = False
            if not ok:
                break
        if ok:
            res.append(c)
    return res


def get(voice, source='ar', accent=None, **want):
    """The one clip that matches (see find); raises LookupError for none or several."""
    r = find(voice, source, accent, **want)
    if source == 'roland' and len(r) > 1:
        r = [c for c in r if c.accent == (accent or 127)] or r
        plain = [c for c in r if not c.extra.get('default') and 'total_accent' not in c.knobs] or r
        r = plain if len(plain) == 1 else [c for c in r if c.extra.get('default')] or r
    if len(r) != 1:
        raise LookupError(f'{len(r)} clips for {source}:{voice} {want} accent={accent}: ' + '; '.join(map(str, r[:5])))
    return r[0]


def grid(voice, source='ar', accent=None):
    """The knob names and values a source's plain clips of a voice span: {knob: [values]}."""
    vals = {}
    for c in find(voice, source, accent):
        for k, v in c.knobs.items():
            vals.setdefault(k, set()).add(v)
    return {k: sorted(v) for k, v in vals.items()}


# ---------------------------------------------------------------------------------------------
# Accent: the DAC code a clip was played at

ANALOG = ('BD', 'SD', 'LT', 'MT', 'HT')
_tables = None


def accent_tables():
    """The firmware's two 64-byte accent tables, read from the v4 ROM (private/roms/tr909):
    1F80 for the analog voices, RS and HC, 1FC0 for the hats and cymbals (research.md C.4)."""
    global _tables
    if _tables is None:
        b = open(os.path.join(ROM_DIR, 'tr909_v4.ic609'), 'rb').read()
        _tables = (b[0x1F80:0x1FC0], b[0x1FC0:0x2000])
    return _tables


def dac_code(voice, ta=0, accented=False):
    """The accent DAC code (0-63; acc = 5 V x code / 63) the firmware writes for a sequencer step:
    table[(base + TA) / 2] / 2, TA the TOTAL ACCENT knob's ADC value / 8 (0-31) on a step with
    total accent, else 0; base 50h for an accented analog voice or CH, 32h unaccented, 50h always
    for RS and HC, 40h always for OH, CR, RD. dac_code('BD') = 10, dac_code('BD', 0, True) = 21."""
    voice = voice.upper()
    if voice in ('RS', 'HC'):
        base = 0x50
    elif voice in ('OH', 'CR', 'RD'):
        base = 0x40
    else:
        base = 0x50 if accented else 0x32
    tab = accent_tables()[1 if voice in SAMPLED else 0]
    return tab[(base + int(ta)) >> 1] >> 1


def midi_code(voice, vel):
    """The accent DAC code for a received MIDI note: table[vel / 2] / 2 (v50 -> 10, v80 -> 21,
    v127 -> 63 on the analog voices)."""
    tab = accent_tables()[1 if voice.upper() in SAMPLED else 0]
    return tab[int(vel) >> 1] >> 1


# AudioRealism's accentNNN takes are a TOTAL ACCENT sweep on an accented step (docs/reference.md,
# Accent); TA by take, read from the BD's levels (the knob was set by hand per voice: the SD, LT
# and HT 30 % takes read as TA 8-11).
AR_ACCENT_TA = {0.0: 0, 0.1: 0, 0.2: 4, 0.3: 6, 0.4: 10, 0.5: 14, 0.6: 18, 0.7: 20, 0.8: 24, 0.9: 30, 1.0: 31}


def ar_code(voice, accent=None):
    """The DAC code an AudioRealism clip was played at: a grid or shot clip (accent None) as an
    accented step at TOTAL ACCENT 0 (21 analog, RS, HC; 40 CH; 32 OH, CR, RD; within about 1 dB
    of the 0 % take), an accentNNN take through AR_ACCENT_TA."""
    ta = 0 if accent is None else AR_ACCENT_TA[round(float(accent), 1)]
    return dac_code(voice, ta, True)


def acc_volts(code):
    """The accent input voltage for a DAC code (the 6-bit DAC spans 0-5 V)."""
    return 5.0 * code / 63


# ---------------------------------------------------------------------------------------------
# Loading and trimming

# AudioRealism's clips end up to 2.3 ms into the next hit (a click at the cut): dropped.
END_GUARD_MS = {'ar': 3.0}


def load(path, mono=True):
    """Read a WAV (or a Clip) as float64; mono=True averages the channels. For a Clip, the
    source's END_GUARD_MS is cut off the end. Returns (x, sr)."""
    guard = 0.0
    if isinstance(path, Clip):
        guard = END_GUARD_MS.get(path.source, 0.0)
        path = path.path
    x, sr = sf.read(path, always_2d=True, dtype='float64')
    if guard:
        x = x[:len(x) - int(guard * sr / 1000)]
    return (x.mean(axis=1) if mono else x), sr


def onset(x, sr, thresh_db=-30.0, back_db=-50.0, back_ms=3.0):
    """Index where the hit starts: the first sample whose magnitude reaches `thresh_db` below the
    clip's peak, moved back to the last sample before it that is below `back_db` (at most
    `back_ms` back), so a slow first rise (the BD at ATTACK 0) is not cut."""
    a = np.abs(x)
    pk = a.max()
    if pk <= 0:
        return 0
    i = int(np.argmax(a > pk * 10 ** (thresh_db / 20)))
    lo = max(0, i - int(back_ms * sr / 1000))
    quiet = np.nonzero(a[lo:i] < pk * 10 ** (back_db / 20))[0]
    return lo + int(quiet[-1]) + 1 if len(quiet) else lo


def freeze(x, sr):
    """(index from which the signal no longer changes, its constant value), or (len(x), 0) if
    it keeps changing. Roland's plugin parks every voice at a small DC value once quiet."""
    d = np.nonzero(np.diff(x) != 0)[0]
    if len(d) == 0:
        return 0, float(x[-1]) if len(x) else 0.0
    last = int(d[-1]) + 1
    if len(x) - last > int(0.005 * sr):
        return last, float(x[-1])
    return len(x), 0.0


def trim(x, sr, pre_ms=0.0, **kw):
    """The clip from its onset (minus `pre_ms`), with a constant tail value (freeze) removed.
    Returns (y, onset index in x)."""
    fi, dc = freeze(x, sr)
    if fi < len(x):
        x = x - dc
    i = max(0, onset(x, sr, **kw) - int(pre_ms * sr / 1000))
    return x[i:], i


def load_ref(voice, source='ar', accent=None, pre_ms=0.0, **want):
    """get() + load() + trim(): (x from its onset, sr, Clip)."""
    c = get(voice, source, accent, **want)
    x, sr = load(c)
    y, _ = trim(x, sr, pre_ms)
    return y, sr, c


# ---------------------------------------------------------------------------------------------
# Filters, envelopes, levels

# The recording chains, as far as the recordings show them, to apply to a model's render before it
# is compared with a source's clip (the model's output is the MULTI OUT jack, which is DC-coupled:
# IC40b -> R503 -> the jack). AudioRealism's clips are high-passed at about 6.5 Hz, first order: the
# snare's drum starts on a negative step (its VCAs' base current) that the model returns to 0
# smoothly by 100 ms and AR overshoots to +0.08 of the peak at 40-60 ms, decaying by 150 ms; a
# first-order high-pass on the model fits AR's waveform below 100 Hz best at 6-7 Hz over six
# settings (1.2 % of the peak; second order fits worse) (docs/circuit/sd.md). The others: not known.
CHAIN_HP_HZ = {'ar': 6.5}


def chain(x, sr, source='ar'):
    """A model's render as the source's recording chain would have taken it: AudioRealism's 6.5 Hz
    first-order high-pass (CHAIN_HP_HZ); other sources unchanged. Apply it to the model's side
    before comparing decays and low-frequency energy with that source's clips."""
    f = CHAIN_HP_HZ.get(source)
    if not f:
        return x
    b, a = signal.butter(1, f / (sr / 2), 'highpass')
    return signal.lfilter(b, a, x)


def lowpass(x, sr, f, order=4):
    """Zero-phase Butterworth low-pass (sosfiltfilt: no delay, order doubled)."""
    return signal.sosfiltfilt(signal.butter(order, min(f, 0.45 * sr), 'lowpass', fs=sr, output='sos'), x)


def highpass(x, sr, f, order=4):
    """Zero-phase Butterworth high-pass."""
    return signal.sosfiltfilt(signal.butter(order, f, 'highpass', fs=sr, output='sos'), x)


def bandpass(x, sr, lo, hi, order=4):
    """Zero-phase Butterworth band-pass."""
    return signal.sosfiltfilt(signal.butter(order, [lo, min(hi, 0.45 * sr)], 'bandpass', fs=sr, output='sos'), x)


def envelope(x, sr, win_ms=10.0):
    """Moving RMS over a centred window of `win_ms`, one value per sample (a Hilbert envelope
    is non-local: its 1/t tails would hold a decayed sound up)."""
    n = max(1, int(win_ms * sr / 1000))
    c = np.concatenate([[0.0], np.cumsum(x * x)])
    i = np.arange(len(x))
    a, b = np.clip(i - n // 2, 0, len(x)), np.clip(i - n // 2 + n, 0, len(x))
    return np.sqrt(np.maximum((c[b] - c[a]) / n, 0))


def db(a, floor=-300.0):
    """20 log10 |a| (arrays too), `floor` for zero."""
    a = np.abs(np.asarray(a, dtype=float))
    return np.where(a > 0, 20 * np.log10(np.maximum(a, 1e-300)), floor) if a.ndim else (20 * math.log10(a) if a > 0 else floor)


def peak_db(x):
    """Sample peak, dBFS."""
    return float(db(np.abs(x).max())) if len(x) else -300.0


def energy_db(x, sr, t0=0.0, t1=None):
    """Integrated level 10 log10(sum x^2 / sr) over t0..t1 s (dB re 1 FS^2 s): the measure for
    level differences between hits of different lengths."""
    s = x[int(t0 * sr):None if t1 is None else int(t1 * sr)]
    e = float(np.sum(s * s)) / sr
    return 10 * math.log10(e) if e > 0 else -300.0


def rms_db(x, sr, t0=0.0, t1=0.05):
    """RMS level over t0..t1 s, dBFS (the default, the first 50 ms, is the accent measure)."""
    s = x[int(t0 * sr):int(t1 * sr)]
    return float(db(np.sqrt(np.mean(s * s)))) if len(s) else -300.0


def noise_floor_db(x, sr, win_ms=10.0, tail_ms=50.0):
    """The clip's floor relative to its envelope peak, dB: the median envelope over its last
    `tail_ms`. On a trimmed recording this is where it was cut, not true silence; decay_times()
    uses it to refuse levels it cannot see."""
    e = envelope(x, sr, win_ms)
    t = e[-max(1, int(tail_ms * sr / 1000)):]
    return float(db(np.median(t) / e.max())) if e.max() > 0 else 0.0


def decay_times(x, sr, levels=(20, 40, 60), win_ms=10.0, floor_db=None, margin_db=6.0, mode='last'):
    """Decay times in ms from the start of x (trim it first): for each N in `levels`, the last
    time the `win_ms` RMS envelope is above its peak -N dB (mode='last', robust to a re-rise
    of a noisy tail, the definition of Roland's measurements.md), or the first time it falls
    below that after the peak (mode='first'). A level within `margin_db` of the clip's floor
    (noise_floor_db unless `floor_db` is given, dB re peak) gives NaN: it can't be seen.
    Returns {'t20': ms, 't40': ms, ...}."""
    e = envelope(x, sr, win_ms)
    pk = e.max()
    if pk <= 0:
        return {f't{n}': float('nan') for n in levels}
    fl = noise_floor_db(x, sr, win_ms) if floor_db is None else floor_db
    p = int(np.argmax(e))
    out = {}
    for n in levels:
        if -n < fl + margin_db:
            out[f't{n}'] = float('nan')
            continue
        thr = pk * 10 ** (-n / 20)
        if mode == 'last':
            above = np.nonzero(e > thr)[0]
            out[f't{n}'] = above[-1] / sr * 1000 if len(above) else float('nan')
        else:
            below = np.nonzero(e[p:] < thr)[0]
            out[f't{n}'] = (p + below[0]) / sr * 1000 if len(below) else float('nan')
    return out


def decay_rate(x, sr, start_db=-10.0, stop_db=-40.0, win_ms=10.0, floor_db=None, margin_db=10.0):
    """The tail's slope in dB/s: a line fitted to the envelope (dB re peak) from where it first
    falls below `start_db` after the peak to where it first reaches `stop_db` (or the floor +
    `margin_db`). Also returns the equivalent exponential time constant. {'db_per_s', 'tau_ms'}."""
    e = envelope(x, sr, win_ms)
    if e.max() <= 0:
        return {'db_per_s': float('nan'), 'tau_ms': float('nan')}
    ed = db(e / e.max())
    fl = noise_floor_db(x, sr, win_ms) if floor_db is None else floor_db
    stop = max(stop_db, fl + margin_db)
    p = int(np.argmax(e))
    a = np.nonzero(ed[p:] < start_db)[0]
    if not len(a):
        return {'db_per_s': float('nan'), 'tau_ms': float('nan')}
    i0 = p + int(a[0])
    b = np.nonzero(ed[i0:] < stop)[0]
    i1 = i0 + int(b[0]) if len(b) else len(ed)
    if i1 - i0 < int(0.005 * sr):
        return {'db_per_s': float('nan'), 'tau_ms': float('nan')}
    t = np.arange(i0, i1) / sr
    s = float(np.polyfit(t, ed[i0:i1], 1)[0])
    return {'db_per_s': s, 'tau_ms': -20 / math.log(10) / s * 1000 if s < 0 else float('nan')}


# ---------------------------------------------------------------------------------------------
# Pitch

def zc_track(x, sr, lp=None, band=None, t_max=None, floor_db=-30.0, gate=0.1):
    """Instantaneous frequency from the positive-going zero crossings of a filtered signal:
    low-passed at `lp` Hz, or band-passed `band`=(lo, hi), zero-phase. Crossings are linearly
    interpolated; each period gives one frequency at its middle. Tracking stops where the
    filtered envelope falls `floor_db` below its peak (or at `t_max` s), and starts once the
    filtered signal first reaches `gate` x its peak (a small lead-in excursion would add a false
    first period). Returns (t s from the start of x, f Hz), arrays."""
    y = x
    if band is not None:
        y = bandpass(x, sr, *band)
    elif lp is not None:
        y = lowpass(x, sr, lp)
    if len(y) < 4 or np.abs(y).max() <= 0:
        return np.array([]), np.array([])
    e = envelope(y, sr, 5.0)
    above = np.nonzero(e > e.max() * 10 ** (floor_db / 20))[0]
    end = int(above[-1]) + 1 if len(above) else len(y)
    if t_max is not None:
        end = min(end, int(t_max * sr))
    start = int(np.argmax(np.abs(y) > gate * np.abs(y[:end]).max()))
    s = np.signbit(y[:end])
    idx = np.nonzero(s[:-1] & ~s[1:])[0]
    idx = idx[idx >= start]
    if len(idx) < 2:
        return np.array([]), np.array([])
    frac = -y[idx] / (y[idx + 1] - y[idx])
    tz = (idx + frac) / sr
    return (tz[:-1] + tz[1:]) / 2, 1 / np.diff(tz)


def acf_track(x, sr, fmin=30.0, fmax=1000.0, win_ms=None, hop_ms=2.0, lp=None, band=None, t_max=None,
              floor_db=-30.0, min_strength=0.5):
    """Pitch by short-time autocorrelation: Hann frames of `win_ms` (default 3.5 periods of
    fmin, at least 10 ms) every `hop_ms`, of x low-passed at `lp` or band-passed `band`; the
    frame's ACF normalised by its energy and by the window's own ACF (Boersma 1993), lags
    1/fmax..1/fmin (kept below a third of the frame), the highest peak, parabolic interpolation;
    a peak at half the lag with >= 0.85 of its strength is preferred (octave guard). Frames
    whose RMS is `floor_db` below the loudest, or whose peak is weaker than `min_strength`, are
    dropped. A sum of tones (the toms' two VCOs, 3:2) has the period of their common
    fundamental: band-pass around the one you want. Returns (t s of the frame centres, f Hz,
    strength 0-1)."""
    y = bandpass(x, sr, *band) if band else lowpass(x, sr, lp) if lp else x
    if t_max is not None:
        y = y[:int(t_max * sr)]
    w_ms = win_ms or max(10.0, 3500.0 / fmin)
    n = int(w_ms * sr / 1000)
    hop = max(1, int(hop_ms * sr / 1000))
    if len(y) < n:
        return np.array([]), np.array([]), np.array([])
    win = np.hanning(n)
    nfft = 1 << int(math.ceil(math.log2(2 * n)))
    wac = np.fft.irfft(np.abs(np.fft.rfft(win, nfft)) ** 2)[:n]
    wac = wac / wac[0]
    lo, hi = max(2, int(sr / fmax)), min(n // 3, int(sr / fmin) + 1)
    frames = np.lib.stride_tricks.sliding_window_view(y, n)[::hop]
    en = np.sqrt(np.mean(frames ** 2, axis=1))
    keep = en > en.max() * 10 ** (floor_db / 20)
    T, F, S = [], [], []
    for k in np.nonzero(keep)[0]:
        fr = (frames[k] - frames[k].mean()) * win
        r = np.fft.irfft(np.abs(np.fft.rfft(fr, nfft)) ** 2)[:n]
        if r[0] <= 0 or hi - lo < 3:
            continue
        r = r / r[0] / np.maximum(wac, 1e-3)
        seg = r[lo:hi + 1]
        j = int(np.argmax(seg)) + lo
        h = int(round(j / 2))
        if h - 2 > lo:
            jj = h - 2 + int(np.argmax(r[h - 2:h + 3]))
            if r[jj] >= 0.85 * r[j]:
                j = jj
        if r[j] < min_strength or j >= hi:
            continue
        a, b, c = r[j - 1], r[j], r[j + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        d = min(0.5, max(-0.5, d))
        T.append((k * hop + n / 2) / sr)
        F.append(sr / (j + d))
        S.append(min(1.0, b))
    return np.array(T), np.array(F), np.array(S)


def track_at(t, f, times):
    """A pitch track's value at each time in `times` (s), interpolated; NaN outside it."""
    return [float(np.interp(s, t, f)) if len(t) and t[0] <= s <= t[-1] else float('nan') for s in times]


def _sweep(t, f_end, f_start, tau):
    return f_end + (f_start - f_end) * np.exp(-t / tau)


def fit_sweep(t, f, t_min=0.0, t_max=None, f_end=None):
    """Fit f(t) = f_end + (f_start - f_end) e^(-t/tau) to a pitch track over t_min..t_max s
    (t from the onset, so f_start is the sweep extrapolated to the onset). `f_end` fixes the end
    pitch. Returns {'f_start', 'f_end', 'tau_ms', 'rms_hz', 'n'} or {} if it fails."""
    t, f = np.asarray(t), np.asarray(f)
    m = (t >= t_min) & ((t <= t_max) if t_max is not None else True)
    t, f = t[m], f[m]
    if len(t) < 4:
        return {}
    try:
        if f_end is None:
            p, _ = curve_fit(_sweep, t, f, p0=(f[-1], f[0] * 1.2, 0.02), maxfev=20000,
                             bounds=([1, 1, 1e-4], [5000, 20000, 5]))
        else:
            q, _ = curve_fit(lambda tt, a, b: _sweep(tt, f_end, a, b), t, f, p0=(f[0] * 1.2, 0.02), maxfev=20000,
                             bounds=([1, 1e-4], [20000, 5]))
            p = (f_end, q[0], q[1])
    except Exception:
        return {}
    r = f - _sweep(t, *p)
    return {'f_start': float(p[1]), 'f_end': float(p[0]), 'tau_ms': float(p[2] * 1000),
            'rms_hz': float(np.sqrt(np.mean(r ** 2))), 'n': int(len(t))}


def spectral_peaks(x, sr, fmin=30.0, fmax=2000.0, n=3, min_sep=0.08, window='hann'):
    """The `n` strongest local maxima of the zero-padded spectrum of x between fmin and fmax,
    at least `min_sep` (relative) apart, parabolic in dB. window='hann' or 'flat' (flat start,
    Hann fall over the last quarter: for a window from the onset of a fast hit). Returns
    [(Hz, dB re the strongest)]."""
    if len(x) < 64:
        return []
    nfft = max(1 << 17, 1 << int(math.ceil(math.log2(len(x)))))
    X = np.abs(np.fft.rfft(x * taper(len(x), window), nfft))
    fr = np.fft.rfftfreq(nfft, 1 / sr)
    L = 20 * np.log10(X + 1e-20)
    band = np.nonzero((fr >= fmin) & (fr <= fmax))[0]
    i = band[1:-1]
    loc = i[(L[i] > L[i - 1]) & (L[i] >= L[i + 1])]
    loc = loc[np.argsort(-L[loc])]
    out = []
    for k in loc:
        a, b, c = L[k - 1], L[k], L[k + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        fk = (k + d) * (fr[1] - fr[0])
        if all(abs(fk - o[0]) > min_sep * o[0] for o in out):
            out.append((fk, b - 0.25 * (a - c) * d))
        if len(out) == n:
            break
    top = out[0][1] if out else 0.0
    return [(float(fk), float(lv - top)) for fk, lv in out]


# ---------------------------------------------------------------------------------------------
# Spectra

def taper(n, kind='hann'):
    """A window: 'hann', or 'flat' (flat, with a Hann fall over the last quarter): a full Hann
    over a window that starts at a fast hit's onset would weigh its tail, not the hit."""
    if kind == 'hann':
        return np.hanning(n)
    w = np.ones(n)
    k = n // 4
    if k:
        w[n - k:] = 0.5 * (1 + np.cos(np.pi * np.arange(k) / k))
    return w


def band_edges(fmin=40.0, fmax=18000.0, per_oct=6):
    """Band edges fmin * 2^(k / per_oct) up to fmax."""
    n = int(math.floor(per_oct * math.log2(fmax / fmin)))
    return fmin * 2 ** (np.arange(n + 1) / per_oct)


def band_levels(x, sr, window=(0.0, None), per_oct=6, fmin=40.0, fmax=18000.0, kind='hann'):
    """Energy per band (dB, absolute) of x[window] (seconds), windowed by taper(kind); bands
    above 0.45 sr are dropped. Returns (band centres Hz, dB)."""
    s = x[int(window[0] * sr):None if window[1] is None else int(window[1] * sr)]
    nfft = max(1 << 15, 1 << int(math.ceil(math.log2(max(2, len(s))))))
    X = np.abs(np.fft.rfft(s * taper(len(s), kind), nfft)) ** 2
    fr = np.fft.rfftfreq(nfft, 1 / sr)
    ed = band_edges(fmin, min(fmax, 0.45 * sr), per_oct)
    cs = np.concatenate([[0.0], np.cumsum(X)])
    ix = np.searchsorted(fr, ed)
    b = (cs[ix[1:]] - cs[ix[:-1]]) / nfft * 2 / sr + 1e-30
    return np.sqrt(ed[:-1] * ed[1:]), 10 * np.log10(b)


def short_hit(x, sr):
    """True for a hit 20 dB down within 50 ms of its start (the rim shot, the closed hat, short
    BD/SD settings): its later part is mostly floor, so spectra are taken over 0-80 ms."""
    t = decay_times(x, sr, (20,), floor_db=-200)['t20']
    return not (t >= 50)


def lsd(a, sr_a, b, sr_b, window=None, per_oct=6, top_db=40.0, floor_margin_db=10.0, fmax=18000.0, detail=False):
    """Log-spectral distance between two hits (both trimmed to their onsets), dB: the RMS over
    bands of the difference of their band spectra, each set to its own total (so it compares
    shape, not level). Over `window` (s), by default 30-330 ms, or 0-80 ms with a flat start for
    a short_hit() `a`. Only bands within `top_db` of a's strongest and `floor_margin_db` above
    a's floor (its last 50 ms, scaled to the window's length) count, and only below `fmax` and
    0.45 x the lower sample rate (pass fmax=16000 against an MP3). On a nearly pure tone (the BD's tail) it mostly measures pitch: a line
    100 cents off moves across a band edge. detail=True also returns (centres, a dB, b dB, mask)."""
    if window is None:
        window = (0.0, 0.08) if short_hit(a, sr_a) else (0.03, 0.33)
    kind = 'flat' if window[0] == 0 else 'hann'
    fmax = min(fmax, 0.45 * min(sr_a, sr_b))
    fa, A = band_levels(a, sr_a, window, per_oct, fmax=fmax, kind=kind)
    fb, B = band_levels(b, sr_b, window, per_oct, fmax=fmax, kind=kind)
    nb = min(len(A), len(B))
    fa, A, B = fa[:nb], A[:nb], B[:nb]
    tail = a[-int(0.05 * sr_a):]
    wl = (window[1] - window[0]) if window[1] is not None else len(a) / sr_a - window[0]
    _, Fl = band_levels(tail, sr_a, (0, None), per_oct, fmax=fmax, kind='hann')
    Fl = Fl[:nb] + 10 * math.log10(max(wl, 1e-3) / 0.05)
    keep = (A > A.max() - top_db) & (A > Fl + floor_margin_db)
    An = A - 10 * np.log10(np.sum(10 ** (A[keep] / 10))) if keep.any() else A
    Bn = B - 10 * np.log10(np.sum(10 ** (B[keep] / 10))) if keep.any() else B
    d = float(np.sqrt(np.mean((An - Bn)[keep] ** 2))) if keep.any() else float('nan')
    return (d, (fa, An, Bn, keep)) if detail else d


def centroid(x, sr, window=(0.0, 0.06), kind=None, fmin=20.0):
    """Power spectral centroid (Hz) of x[window] (s) above fmin; the taper is flat from a
    window starting at 0, else Hann."""
    s = x[int(window[0] * sr):None if window[1] is None else int(window[1] * sr)]
    if len(s) < 16:
        return float('nan')
    X = np.abs(np.fft.rfft(s * taper(len(s), kind or ('flat' if window[0] == 0 else 'hann')))) ** 2
    fr = np.fft.rfftfreq(len(s), 1 / sr)
    m = fr >= fmin
    return float((X[m] * fr[m]).sum() / (X[m].sum() + 1e-30))


def band_envelopes(x, sr, edges=None, win_ms=5.0, hop_ms=1.0):
    """Envelopes per band: x through zero-phase 4th-order Butterworth band-passes at `edges`
    (default octaves 44-11300 Hz, 8 bands centred 63 Hz-8 kHz), then a `win_ms` moving RMS,
    sampled every `hop_ms`. Returns (t s, centres Hz, E dBFS [band, t])."""
    if edges is None:
        edges = 44.2 * 2 ** np.arange(9)
    edges = np.asarray([e for e in edges if e < 0.45 * sr])
    hop = max(1, int(hop_ms * sr / 1000))
    E = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        y = bandpass(x, sr, lo, hi)
        E.append(db(envelope(y, sr, win_ms)[::hop]))
    t = np.arange(len(E[0])) * hop / sr if E else np.array([])
    return t, np.sqrt(edges[:-1] * edges[1:]), np.array(E)


def compare_band_envelopes(a, sr_a, b, sr_b, edges=None, t_max=None, range_db=40.0, level_match=True):
    """Per-band envelope comparison of two hits (trimmed to onset). Both are optionally matched
    on total energy first (b scaled to a). Per band: the level difference (b - a, dB of the band
    energy), each one's -20 dB time (ms), and the RMS of the dB difference of the envelopes over
    the time a's band envelope is within `range_db` of its peak. Returns a list of dicts, one
    per band (f, d_level_db, t20_a, t20_b, env_rms_db)."""
    if level_match:
        g = energy_db(a, sr_a) - energy_db(b, sr_b)
        b = b * 10 ** (g / 20)
    ta, fa, Ea = band_envelopes(a, sr_a, edges)
    tb, fb, Eb = band_envelopes(b, sr_b, edges)
    nb = min(len(fa), len(fb))
    n = min(Ea.shape[1], Eb.shape[1])
    if t_max is not None:
        n = min(n, int(t_max / (ta[1] - ta[0])) if len(ta) > 1 else n)
    out = []
    for k in range(nb):
        ea, eb = Ea[k, :n], Eb[k, :n]
        la, lb = 10 * np.log10(np.sum(10 ** (ea / 10)) + 1e-30), 10 * np.log10(np.sum(10 ** (eb / 10)) + 1e-30)
        m = ea > ea.max() - range_db

        def t20(e):
            i = np.nonzero(e > e.max() - 20)[0]
            return float(ta[i[-1]] * 1000) if len(i) else float('nan')

        out.append({'f': float(fa[k]), 'd_level_db': float(lb - la), 't20_a': t20(ea), 't20_b': t20(eb),
                    'env_rms_db': float(np.sqrt(np.mean((eb[m] - ea[m]) ** 2))) if m.any() else float('nan')})
    return out


# ---------------------------------------------------------------------------------------------
# The sample voices: the ROM and its clock

def rom(which):
    """A sample ROM as the 909 plays it: 6-bit codes (byte >> 2) minus 32, float. which = 'oh'
    (HH ROM 0x0000-0x5FFF), 'ch' (HH ROM 0x6000-0x7FFF), 'crash', 'ride' (the files in
    private/roms/tr909 are in playback order, reference/rom_decoded/files.json)."""
    f = {'oh': 'hn61256p__c43.ic69', 'ch': 'hn61256p__c43.ic69', 'crash': 'hn61256p__c42.ic62',
         'ride': 'hn61256p__c44.ic54'}[which]
    b = np.frombuffer(open(os.path.join(ROM_DIR, f), 'rb').read(), dtype=np.uint8)
    v = (b >> 2).astype(float) - 32.0
    return v[:0x6000] if which == 'oh' else v[0x6000:] if which == 'ch' else v


def _ncc_best(hay, needle):
    n = len(needle)
    needle = needle - needle.mean()
    c = signal.fftconvolve(hay, needle[::-1], mode='valid')
    s1 = np.convolve(hay, np.ones(n), mode='valid')
    s2 = np.convolve(hay * hay, np.ones(n), mode='valid')
    e = np.sqrt(np.maximum(s2 - s1 * s1 / n, 0) + 1e-12)
    r = c / (e * np.linalg.norm(needle) + 1e-12)
    i = int(np.argmax(r))
    return float(r[i]), i


def rom_rate(x, sr, which, lo=15000.0, hi=80000.0, needle=(0.003, 0.043), rom_span=4000):
    """The sample clock (Hz) a recording of a hat or cymbal plays its ROM at: the ROM's first
    `rom_span` samples are rendered as a zero-order hold at candidate clocks, both band-passed
    1.5-12 kHz (clear of the VCA's envelope and the output filters), and the clock whose
    normalised cross-correlation with the recording's `needle` window (s from the onset) is
    highest wins: a log grid, then two finer passes. x is trimmed to the onset. Returns
    {'rate_hz', 'ncc', 'rom_offset'} (offset: ROM samples at the needle's start)."""
    data = rom(which)[:rom_span]
    hp = bandpass(x, sr, 1500, 12000, 2)
    nd = hp[int(needle[0] * sr):int(needle[1] * sr)]

    def score(r):
        k = np.floor(np.arange(int(len(data) / r * sr)) * r / sr).astype(int)
        hay = bandpass(data[np.clip(k, 0, len(data) - 1)], sr, 1500, 12000, 2)
        if len(hay) <= len(nd):
            return -1.0, 0
        return _ncc_best(hay, nd)

    rates = np.exp(np.linspace(math.log(lo), math.log(hi), 240))
    best = max(((score(r), r) for r in rates), key=lambda z: z[0][0])
    for span, n in ((0.02, 41), (0.003, 31)):
        rates = best[1] * np.linspace(1 - span, 1 + span, n)
        best = max(((score(r), r) for r in rates), key=lambda z: z[0][0])
    (c, i), r = best
    return {'rate_hz': float(r), 'ncc': c, 'rom_offset': float(i * r / sr)}


# ---------------------------------------------------------------------------------------------
# Voice-specific pieces

def hc_bursts(x, sr):
    """The clap's bursts: times (ms from the onset) of 1 ms RMS maxima in the first 60 ms that
    stand >= 10 dB above the dips around them (the reverb tail only ripples by 5-8 dB)."""
    pre = int(0.005 * sr)  # silence before the onset, so the first burst has a rise
    e = envelope(np.concatenate([np.zeros(pre), x[:int(0.06 * sr)]]), sr, 1.0)
    if e.max() <= 0:
        return []
    edb = db(e / e.max())
    pk, _ = signal.find_peaks(edb, height=-20, distance=int(0.004 * sr), prominence=10)
    return [float((p - pre) / sr * 1000) for p in pk]


def click_db(x, sr, t1=0.005, f=1000.0):
    """Energy above `f` in the first `t1` s, dB re the hit's whole energy: the BD's ATTACK."""
    h = highpass(x, sr, f)
    return energy_db(h, sr, 0, t1) - energy_db(x, sr)


def tom_lines(x, sr, voice, window=(0.05, 0.25)):
    """A tom's two lowest strong lines (VCO-1 and VCO-2, about 1.48 apart) in `window`:
    spectral peaks 30-400 Hz; the main line is the strongest, the lower one the strongest peak
    between 0.55 and 0.8 of it. Returns (main Hz, lower Hz or NaN, lower dB re main)."""
    s = x[int(window[0] * sr):int(window[1] * sr)]
    pk = spectral_peaks(s, sr, 30, 400, 8, 0.05)
    if not pk:
        return float('nan'), float('nan'), float('nan')
    f0 = pk[0][0]
    low = [(f, l) for f, l in pk if 0.55 < f / f0 < 0.8]
    return f0, (low[0][0] if low else float('nan')), (low[0][1] if low else float('nan'))


def measure(x, sr, voice=None, rate=False):
    """The standard measurements of one hit (x trimmed to its onset), as a dict:

      all     peak_db, energy_db, rms50_db (first 50 ms), floor_db (re peak), t20/t40/t60 (ms,
              last time above), tail_db_per_s / tail_tau_ms (decay_rate -10..-40 dB), centroid_hz
              (0-60 ms), centroid_full_hz (to -40 dB)
      BD      zc track low-passed at 1.5 kHz: f_first, f_10ms, f_50ms, f_100ms, f_late (median
              200-400 ms), sweep fit over 0-350 ms (sweep_f_start, sweep_f_end, sweep_tau_ms,
              sweep_rms_hz), click_db
      SD      peaks 2-20 ms and 30-150 ms (peaks_early, peaks_late: [(Hz, dB)]), f_low (strongest
              line 90-300 Hz, 30-150 ms), f_high (strongest 1.35-1.65 x f_low), noise t20/t40 above
              2 kHz (noise_t20, noise_t40)
      toms    f_main / f_low / low_db (tom_lines 50-250 ms), f_early (strongest line 3-25 ms),
              zc track band-passed 0.7-1.4 f_main, sweep fit over 3-500 ms
      RS      peaks_hz (first 30 ms, 80 Hz-4 kHz)
      HC      bursts_ms
      CH OH CR RD   rom clock (rate=True: slow): rom_rate_hz, rom_ncc
    """
    m = {'sr': sr, 'length_s': len(x) / sr, 'peak_db': peak_db(x), 'energy_db': energy_db(x, sr),
         'rms50_db': rms_db(x, sr, 0, 0.05), 'floor_db': noise_floor_db(x, sr)}
    m.update(decay_times(x, sr))
    r = decay_rate(x, sr)
    m['tail_db_per_s'], m['tail_tau_ms'] = r['db_per_s'], r['tau_ms']
    m['centroid_hz'] = centroid(x, sr)
    t40 = decay_times(x, sr, (40,), floor_db=-200)['t40']
    m['centroid_full_hz'] = centroid(x, sr, (0, max(0.02, (t40 if t40 == t40 else 300) / 1000)), 'flat')
    voice = (voice or '').upper()
    if voice == 'BD':
        t, f = zc_track(x, sr, lp=1500, floor_db=-40)
        if len(f):
            m['f_first'] = float(f[0])
            m['f_10ms'], m['f_50ms'], m['f_100ms'] = track_at(t, f, (0.01, 0.05, 0.1))
            late = f[(t >= 0.2) & (t <= 0.4)]
            m['f_late'] = float(np.median(late)) if len(late) else float('nan')
            fit = fit_sweep(t, f, 0, 0.35)
            for k, v in fit.items():
                m['sweep_' + k] = v
        m['click_db'] = click_db(x, sr)
    elif voice == 'SD':
        m['peaks_early'] = spectral_peaks(x[int(0.002 * sr):int(0.02 * sr)], sr, 80, 1000, 3)
        late = x[int(0.03 * sr):int(0.15 * sr)]
        m['peaks_late'] = spectral_peaks(late, sr, 80, 1000, 3)
        pk = spectral_peaks(late, sr, 90, 300, 1)
        if pk:
            m['f_low'] = pk[0][0]
            hi = spectral_peaks(late, sr, 1.35 * pk[0][0], 1.65 * pk[0][0], 1)
            m['f_high'] = hi[0][0] if hi else float('nan')
        n = highpass(x, sr, 2000)
        d = decay_times(n, sr, (20, 40))
        m['noise_t20'], m['noise_t40'] = d['t20'], d['t40']
        m['noise_peak_db'] = peak_db(n)
    elif voice in TOMS:
        f0, fl, ll = tom_lines(x, sr, voice)
        m['f_main'], m['f_low'], m['low_db'] = f0, fl, ll
        e = spectral_peaks(x[int(0.003 * sr):int(0.025 * sr)], sr, 40, 500, 1)
        m['f_early'] = e[0][0] if e else float('nan')
        if f0 == f0:
            t, f = zc_track(x, sr, band=(0.7 * f0, 1.4 * f0), floor_db=-40)
            if len(f):
                f = signal.medfilt(f, 3) if len(f) >= 3 else f
                fit = fit_sweep(t, f, 0.003, 0.5)
                for k, v in fit.items():
                    m['sweep_' + k] = v
                m['f_10ms'], m['f_50ms'], m['f_100ms'], m['f_300ms'] = track_at(t, f, (0.01, 0.05, 0.1, 0.3))
    elif voice == 'RS':
        m['peaks_hz'] = spectral_peaks(x[:int(0.03 * sr)], sr, 80, 4000, 3, window='flat')
    elif voice == 'HC':
        m['bursts_ms'] = hc_bursts(x, sr)
    elif voice in SAMPLED and rate:
        rr = rom_rate(x, sr, {'CH': 'ch', 'OH': 'oh', 'CR': 'crash', 'RD': 'ride'}[voice])
        m['rom_rate_hz'], m['rom_ncc'] = rr['rate_hz'], rr['ncc']
    return m


# ---------------------------------------------------------------------------------------------
# Command line

def _parse_ref(s):
    """ref:SOURCE:VOICE:k=v,k=v -> Clip."""
    _, src, voice, *rest = s.split(':')
    want, accent = {}, None
    for kv in (rest[0].split(',') if rest and rest[0] else []):
        k, v = kv.split('=')
        if k == 'accent':
            accent = float(v) if '.' in v else int(v)
        else:
            want[k] = float(v) if re.fullmatch(r'[\d.]+', v) and k not in ('config',) else v
    return get(voice, src, accent, **want)


def _open(s, voice=None):
    if s.startswith('ref:'):
        c = _parse_ref(s)
        x, sr = load(c)
        y, _ = trim(x, sr)
        return y, sr, c.voice, str(c)
    x, sr = load(s)
    y, _ = trim(x, sr)
    if voice is None:
        b = os.path.basename(s).upper()
        for k, v in (('BASSDRUM', 'BD'), ('SNARE', 'SD'), ('LOWTOM', 'LT'), ('MIDTOM', 'MT'), ('HITOM', 'HT'),
                     ('RIMSHOT', 'RS'), ('HANDCLAP', 'HC'), ('CLOSEDHAT', 'CH'), ('OPEN', 'OH'), ('CRASH', 'CR'),
                     ('RIDE', 'RD')):
            if k in b:
                voice = v
                break
        else:
            m = re.match(r'(BD|SD|LT|MT|HT|RS|HC|CH|OH|CR|RD)[-_]', b)
            voice = m.group(1) if m else None
    return y, sr, voice, s


def _fmt(v):
    if isinstance(v, float):
        return 'nan' if v != v else f'{v:.4g}' if abs(v) < 1e5 else f'{v:.0f}'
    if isinstance(v, list):
        return ', '.join(f'{f:.1f} ({l:+.0f})' if isinstance(f, float) and isinstance(l, float) else str((f, l)) for f, l in v) \
            if v and isinstance(v[0], tuple) else ', '.join(_fmt(z) for z in v)
    return str(v)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('list')
    p.add_argument('--source')
    p.add_argument('--voice')
    p = sub.add_parser('ref')
    p.add_argument('voice')
    p.add_argument('knobs', nargs='*')
    p.add_argument('--source', default='ar')
    p.add_argument('--accent')
    p = sub.add_parser('measure')
    p.add_argument('file')
    p.add_argument('--voice')
    p.add_argument('--json', action='store_true')
    p.add_argument('--rate', action='store_true', help='also the ROM clock (hats, cymbals; slow)')
    p = sub.add_parser('compare')
    p.add_argument('a')
    p.add_argument('b')
    p.add_argument('--voice')
    p.add_argument('--window', help='LSD window t0,t1 in s')
    p.add_argument('--rate', action='store_true')
    a = ap.parse_args()

    if a.cmd == 'list':
        for c in catalogue():
            if (a.source is None or c.source == a.source) and (a.voice is None or c.voice == a.voice.upper()):
                print(c)
    elif a.cmd == 'ref':
        want = {k: (float(v) if re.fullmatch(r'[\d.]+', v) else v) for k, v in (kv.split('=') for kv in a.knobs)}
        acc = None if a.accent is None else (float(a.accent) if '.' in a.accent else int(a.accent))
        for c in find(a.voice, a.source, acc, **want):
            print(c)
    elif a.cmd == 'measure':
        x, sr, voice, name = _open(a.file, a.voice)
        m = measure(x, sr, a.voice or voice, a.rate)
        if a.json:
            print(json.dumps({'file': name, 'voice': voice, **m}, indent=1, default=float))
        else:
            print(f'{name}  ({voice or "voice unknown"})')
            for k, v in m.items():
                print(f'  {k:18s} {_fmt(v)}')
    elif a.cmd == 'compare':
        xa, sra, va, na = _open(a.a, a.voice)
        xb, srb, vb, nb = _open(a.b, a.voice or va)
        voice = a.voice or va or vb
        ma, mb = measure(xa, sra, voice, a.rate), measure(xb, srb, voice, a.rate)
        print(f'A {na}\nB {nb}\nvoice {voice}\n')
        print(f'  {"":18s} {"A":>14s} {"B":>14s}   B vs A')
        for k in ma:
            va_, vb_ = ma.get(k), mb.get(k)
            rel = ''
            if isinstance(va_, float) and isinstance(vb_, float) and va_ == va_ and vb_ == vb_ and min(va_, vb_) > -299:
                if k.endswith('_db') or k.endswith('db_per_s'):
                    rel = f'{vb_ - va_:+.2f} dB' if k.endswith('_db') else f'{vb_ - va_:+.1f}'
                elif k.startswith('f_') or k.startswith('sweep_f') or k.endswith('_hz'):
                    rel = f'{1200 * math.log2(vb_ / va_):+.0f} cents' if va_ > 0 and vb_ > 0 else ''
                elif k.startswith('t') or k.endswith('_ms'):
                    rel = f'{math.log2(vb_ / va_):+.2f} oct' if va_ > 0 and vb_ > 0 else ''
            print(f'  {k:18s} {_fmt(va_):>14s} {_fmt(vb_):>14s}   {rel}')
        w = tuple(float(v) for v in a.window.split(',')) if a.window else None
        if w is None:
            w = (0.0, 0.08) if short_hit(xa, sra) else (0.03, 0.33)
        d, (fc, A, B, keep) = lsd(xa, sra, xb, srb, w, detail=True)
        print(f'\n  LSD {d:.2f} dB over {int(keep.sum())} 1/6-octave bands, {w[0] * 1000:.0f}-{w[1] * 1000:.0f} ms')
        print('\n  band envelopes (B level-matched to A on total energy):')
        print(f'  {"Hz":>6s} {"dLevel dB":>10s} {"t20 A ms":>9s} {"t20 B ms":>9s} {"env rms dB":>11s}')
        for r in compare_band_envelopes(xa, sra, xb, srb):
            print(f'  {r["f"]:6.0f} {r["d_level_db"]:+10.2f} {r["t20_a"]:9.0f} {r["t20_b"]:9.0f} {r["env_rms_db"]:11.2f}')


if __name__ == '__main__':
    main()
