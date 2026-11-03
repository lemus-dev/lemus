#!/usr/bin/env python3
"""A/B scenes of a real TR-909 (AudioRealism's pack: one late unit) against Lemus's, at the pack's
known settings, for Lemus AB and lemus-render (tools/host/README.md):

  ar-beat-<kit>     a two-bar beat built from the pack's single hits (individual outs, LEVEL 50 %),
                    every hit at one of its grid settings
  ar-rs-shuffle<k>  the pack's rim shot at SHUFFLE k (1-7): 32 sixteenths at 113.8 BPM
  ar-oh-cut         the open hat (OH DECAY 100 %) cut by a closed hat a shuffled sixteenth later,
                    shuffle 1-7, CH DECAY 0 % then 100 % (master out)
  ar-flam-<voice>   BD, SD, LT, MT, HT: flams 1-8 (FLAM keys 9-16)
  ar-snare-clap     snare and clap together, 8 shots (master out: their shared noise); the pack
                    doesn't give the snare's knobs: TUNE 50 %, TONE 100 %, SNAPPY 100 %, accented,
                    as its other snare extras, fit the shots best of the 5 x 5 x 5 grid (2.4 dB
                    rms in sixth octaves over their first 250 ms; 50/50/50: 4.1 dB)

Both sides are recordings, lined up hit for hit: the real one from the pack, Lemus's rendered by
`tr909plugintest --render` (the plugin's processor, firmware and voicing board, switched on and
past its power-on) at the same knobs, each hit at the accent DAC code the pack played it at
(real909.ar_code, as a MIDI velocity the firmware's table turns into that code), from the same
output (the voice's MULTI OUT with a cable in, or the master pair, mono), through AR's recording
chain (real909.chain). A take's notes are at the real hits' times; MIDI lands on the firmware's
scan, a millisecond or two off, so each note is moved once by what the first render measured.

A beat from single hits is the real voices played one at a time and added: the noise that snare,
clap and toms share doesn't interact as it does in one unit (ar-snare-clap has that), and a hit is
cut where the next on its voice starts (the hats share one). It plays twice and the second time is
kept, so the loop carries the tails round.

The pack can't be published: the scenes and their WAVs go to the private folder.

  ar_scenes.py [--plugintest PATH] [--out DIR] [--work DIR] [--only beat,rs,oh,flam,clap]

--work keeps the renders (build/tr909/ar_scenes): a run after a change to the plugin renders again.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
REPO = os.path.normpath(os.path.join(MACHINE, '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..', 'compare'))
sys.dont_write_bytecode = True
import real909 as R  # noqa: E402
import hh  # noqa: E402
from sequencer_timing import flam_interval, onsets  # noqa: E402

PACK = os.path.join(R.REAL909, 'packs', 'audiorealism_tr909_ultimate', 'wav', 'TR-909-96kHz-24bit')
OUT = os.path.join(MACHINE, 'private', 'reference', 'scenes', 'ar')
SR = 96000
LEAD = 3.0  # s: past the 909's boot and its master's power-on mute (README, Power-on)
PAD = 0.05  # s before a take's first hit in its slot
NOTE = {'BD': 36, 'RS': 37, 'SD': 38, 'HC': 39, 'LT': 43, 'CH': 42, 'MT': 47, 'OH': 46, 'HT': 50, 'CR': 49, 'RD': 51}
# the MULTI OUT jacks after the main pair (PluginProcessor's kJackNames)
JACK = {'BD': 2, 'SD': 3, 'LT': 4, 'MT': 5, 'HT': 6, 'RS': 7, 'HC': 8, 'CH': 9, 'OH': 9, 'CR': 10, 'RD': 11}
# AR: "the part volume set to 50%"
LEVELS = {k: 0.5 for k in ('bd_level', 'sd_level', 'lt_level', 'mt_level', 'ht_level', 'rs_level', 'hc_level',
                            'hh_level', 'cr_level', 'rd_level')}
FLAM_KEYS = {'BD': 'BassDrum/BassDrum909-Flam{n}-Tune100-Attack100-Decay100.wav',
             'SD': 'SnareDrum/Snare909-flam{n}-Tune050-Tone100-Decay100.wav',
             'LT': 'LowTom/LowTom-flam{n}-Tune050-Decay100.wav',
             'MT': 'MidTom/MidTom-flam{n}-Tune050-Decay100.wav',
             'HT': 'HiTom/HiTom-flam{n}-Tune050-Decay100.wav'}
FLAM_KNOBS = {'BD': {'bd_tune': 1.0, 'bd_attack': 1.0, 'bd_decay': 1.0},
              'SD': {'sd_tune': 0.5, 'sd_tone': 1.0, 'sd_snappy': 1.0},  # the files' "Decay": SNAPPY
              'LT': {'lt_tune': 0.5, 'lt_decay': 1.0}, 'MT': {'mt_tune': 0.5, 'mt_decay': 1.0},
              'HT': {'ht_tune': 0.5, 'ht_decay': 1.0}}


def velocity(voice, code):
    """The lowest MIDI velocity the firmware turns into `code` (real909.midi_code)."""
    for v in range(1, 128):
        if R.midi_code(voice, v) == code:
            return v
    raise ValueError(f'no velocity plays {voice} at code {code}')


def pct(v):
    return f'{round(v * 100):03d}'


class Lemus:
    """Renders through tr909plugintest --render, cached by job and binary in `work`."""

    def __init__(self, exe, work):
        self.exe, self.work = exe, work
        os.makedirs(self.work, exist_ok=True)
        self.stamp = str(os.stat(exe).st_mtime_ns)

    def render(self, notes, knobs, seconds, jacks):
        """notes [(seconds, voice, code)] -> (frames, 12): the main pair, then the ten MULTI OUTs."""
        job = {'rate': SR, 'seconds': round(seconds, 4), 'jacks': jacks, 'params': {**LEVELS, **knobs},
               'notes': [[round(t, 6), NOTE[v], velocity(v, c)] for t, v, c in sorted(notes)]}
        key = hashlib.md5((json.dumps(job, sort_keys=True) + self.stamp).encode()).hexdigest()[:20]
        wav = os.path.join(self.work, key + '.wav')
        if not os.path.exists(wav):
            js = os.path.join(self.work, key + '.json')
            with open(js, 'w') as f:
                json.dump(job, f)
            subprocess.run([self.exe, '--render', js, wav], check=True, capture_output=True)
            os.remove(js)
        x, _ = sf.read(wav, dtype='float64', always_2d=True)
        return x

    def output(self, notes, knobs, seconds, jacks):
        """The output the pack recorded: the voices' MULTI OUTs added, or the master pair's mean;
        through AR's chain."""
        x = self.render(notes, knobs, seconds, jacks)
        y = x[:, sorted({JACK[v] for _, v, _ in notes})].sum(axis=1) if jacks else x[:, :2].mean(axis=1)
        return R.chain(y, SR, 'ar')


def load(rel):
    """A pack clip, mono, without its last 3 ms (it runs into the next hit: real909.END_GUARD_MS)."""
    x, sr = sf.read(os.path.join(PACK, rel), dtype='float64', always_2d=True)
    assert sr == SR, rel
    return x.mean(axis=1)[:len(x) - int(0.003 * SR)]


def rise(y, i0, win=0.03, floor=0.0):
    """The first sample from i0 within `win` s whose magnitude is 30 dB under the window's peak and
    three times what came in the 5 ms before i0 (the last hit's tail): a hit's onset."""
    i0 = max(0, i0)
    seg = np.abs(y[i0:i0 + int(win * SR)])
    pre = np.abs(y[max(0, i0 - int(0.005 * SR)):i0]).max(initial=0.0) if floor else 0.0
    th = max(3 * pre, seg.max() * 10 ** (-30 / 20))
    return i0 + int(np.argmax(seg > th))


def write(out, name, real, lemus, bpm, beats, about):
    os.makedirs(os.path.join(out, 'wav'), exist_ok=True)
    srcs = []
    for label, y in (('real', real), ('lemus', lemus)):
        rel = f'wav/{name}-{label}.wav'
        sf.write(os.path.join(out, rel), y.astype(np.float32), SR, subtype='FLOAT')
        srcs.append({'label': label, 'audio': rel})
    scene = {'name': f'tr909-{name}', 'bpm': bpm, 'beats': beats, 'tail': 0, 'rate': SR, 'sources': srcs}
    with open(os.path.join(out, f'{name}.json'), 'w') as f:
        json.dump(scene, f, indent=1)
    print(f'{name}: {about}')


# ---------------------------------------------------------------------------------------------
# A beat from single hits

# Each kit: the knobs (rotation) and the pack's clip for each voice at them. Hits are at the grid's
# code (real909.ar_code: analog, RS, HC 21; CH 40; OH, CR, RD 32). OH is the pack's only 96 kHz
# OH (DECAY 100 %, its 0 % accent take: code 32); RS and HC cycle through the pack's shots.
KITS = {
    'mid': {'bd': (0.5, 0.5, 0.5), 'sd': (0.5, 0.5, 0.5), 'toms': (0.5, 0.5), 'ch': 0.5},
    'deep': {'bd': (0.0, 1.0, 1.0), 'sd': (0.25, 0.75, 0.75), 'toms': (0.3, 0.8), 'ch': 0.25},
}
# two bars of sixteenths
PATTERN = {
    'BD': [0, 4, 8, 12, 16, 20, 24, 28],
    'SD': [4, 20, 28],
    'HC': [12],
    'RS': [10, 26],
    'CH': [s for s in range(29) if s % 4 != 2],
    'OH': [2, 6, 10, 14, 18, 22, 26],
    'HT': [29], 'MT': [30], 'LT': [31],
    'CR': [0], 'RD': [16],
}
BEAT_BPM = 126


def kit_clips(k):
    bd, sd, (tt, td), ch = k['bd'], k['sd'], k['toms'], k['ch']
    clips = {
        'BD': [f'BassDrum/BassDrum909-tune{pct(bd[0])}-attack{pct(bd[1])}-decay{pct(bd[2])}.wav'],
        'SD': [f'SnareDrum/SnareDrum909-tune{pct(sd[0])}-tone{pct(sd[1])}-snappy{pct(sd[2])}.wav'],
        'LT': [f'LowTom/LowTom909-tune{pct(tt)}-decay{pct(td)}.wav'],
        'MT': [f'MidTom/MidTom909-tune{pct(tt)}-decay{pct(td)}.wav'],
        'HT': [f'HiTom/HiTom909-tune{pct(tt)}-decay{pct(td)}.wav'],
        'CH': [f'ClosedHat/ClosedHat909-decay{pct(ch)}.wav'],
        'OH': ['OpenHat/OpenHat-accent000-decay100.wav'],
        'RS': [f'RimShot/RimShot909-shot{n}.wav' for n in range(1, 11)],
        'HC': [f'HandClap/HandClap909-shot{n}.wav' for n in range(1, 9)],
        'CR': ['Crash/Crash909-tune050.wav'],
        'RD': ['Ride/Ride909-tune050.wav'],
    }
    knobs = {'bd_tune': bd[0], 'bd_attack': bd[1], 'bd_decay': bd[2], 'sd_tune': sd[0], 'sd_tone': sd[1],
             'sd_snappy': sd[2], 'ch_decay': ch, 'oh_decay': 1.0, 'cr_tune': 0.5, 'rd_tune': 0.5}
    for t in ('lt', 'mt', 'ht'):
        knobs[t + '_tune'], knobs[t + '_decay'] = tt, td
    return clips, knobs


def beat(lemus, out):
    step = 60 / BEAT_BPM / 4
    period = 32 * step
    for name, k in KITS.items():
        clips, knobs = kit_clips(k)
        hits = sorted((LEAD + rep * period + s * step, v) for v, steps in PATTERN.items() for s in steps for rep in (0, 1))
        notes = [(t, v, R.ar_code(v)) for t, v in hits]
        seconds = LEAD + 2 * period + 0.1
        x = lemus.render(notes, knobs, seconds, True)
        lem = R.chain(x[:, sorted(set(JACK.values()))].sum(axis=1), SR, 'ar')
        # each real hit placed on Lemus's onset of it, on its voice's jack; cut where the next hit
        # on the same jack starts
        placed = []
        count = {}
        for t, v in hits:
            i = rise(x[:, JACK[v]], int(round(t * SR)), floor=1.0)
            c = clips[v][count.get(v, 0) % len(clips[v])]
            count[v] = count.get(v, 0) + 1
            y = load(c)
            placed.append((i - rise(y, 0), JACK[v], y))
        real = np.zeros(len(x))
        for n, (at, jack, y) in enumerate(placed):
            nxt = [a for a, j, _ in placed[n + 1:] if j == jack and a > at]
            end = min([len(real), at + len(y)] + nxt[:1])
            seg = y[:end - at].copy()
            if nxt and end == nxt[0]:
                f = min(len(seg), int(0.001 * SR))
                seg[len(seg) - f:] *= np.linspace(1, 0, f)
            real[at:end] += seg
        a, b = int(round((LEAD + period) * SR)), int(round((LEAD + 2 * period) * SR))
        # the loop starts a little before its first step, as the onsets are a little after it
        write(out, f'ar-beat-{name}', real[a:b], lem[a:b], BEAT_BPM, 8,
              f'BD {k["bd"]}, SD {k["sd"]}, toms {k["toms"]}, CH {k["ch"]} (tune, attack/tone, decay/snappy)')


# ---------------------------------------------------------------------------------------------
# The pack's sequenced takes

def takes(lemus, out, name, items, knobs, jacks, about, measure=None):
    """items: [(clip, [(seconds from the clip's start, voice, code)])], one slot each. Lemus plays
    the notes, lined up on the real take's first hit; `measure(y, i0, k, hits)` gives the times (s
    from i0, the first hit's onset) at which slot k's hits sound, the real take's and Lemus's
    measured alike, and each note is moved once by the difference."""
    measure = measure or onset_measure
    slots, starts, at = [], [], LEAD
    for clip, hits in items:
        y = load(clip)
        slots.append((y, hits))
        starts.append(at)
        at += PAD + len(y) / SR + 0.05
    total = at - LEAD

    def notes_for(shift):
        return [(s + PAD + t + d, v, c) for s, (y, hits), sh in zip(starts, slots, shift) for (t, v, c), d in zip(hits, sh)]

    shift = [[0.0] * len(hits) for _, hits in slots]
    best = None
    for attempt in range(2):
        lem = lemus.output(notes_for(shift), knobs, at + 0.2, jacks)
        worst = 0.0
        tried = [list(sh) for sh in shift]
        for k, ((y, hits), s) in enumerate(zip(slots, starts)):
            r0 = rise(y, 0)
            real_t = measure(y, r0, k, hits)
            i0 = int(round((s + PAD + hits[0][0] + shift[k][0]) * SR)) - int(0.002 * SR)
            l0 = rise(lem, i0, floor=1.0)
            lem_t = measure(lem, l0, k, hits)
            err = [lt - rt for lt, rt in zip(lem_t, real_t)]
            worst = max([worst] + [abs(e) for e in err])
            if attempt == 0:  # a scan's worth; a group tick or more is the measure's choice of tick
                shift[k] = [sh - e if abs(e) < 2.5e-3 else sh for sh, e in zip(shift[k], err)]
        if best is None or worst < best[0]:
            best = (worst, lem, tried)
    # the closer of the two (moving a note can land it on the scan on the other side)
    worst, lem, shift = best
    print(f'  {name}: timing against the real hits, worst {worst * 1e3:.2f} ms')
    real = np.zeros(int(round(total * SR)))
    mine = np.zeros_like(real)
    for (y, hits), s, sh in zip(slots, starts, shift):
        o = int(round((s - LEAD + PAD) * SR))
        r0 = rise(y, 0)
        real[o:o + len(y)] += y[:len(real) - o]
        l0 = rise(lem, int(round((s + PAD + hits[0][0] + sh[0]) * SR)) - int(0.002 * SR), floor=1.0)
        a = l0 - r0
        n = min(len(y), len(real) - o)
        mine[o:o + n] = lem[a:a + n]
    write(out, name, real, mine, 60, round(total, 4), about)


def onset_measure(y, i0, k, hits):
    """Each hit's onset (rise), looked for from 2 ms before its time after the first."""
    return [(rise(y, i0 + int(round((t - hits[0][0]) * SR)) - int(0.002 * SR), floor=1.0) - i0) / SR for t, _, _ in hits]


def rs(lemus, out):
    for k in range(1, 8):
        clip = f'RimShot/RimShot909-shuffle{k}-112BPM.wav'
        # 32 sixteenths; the file ends in the next one's click (real909.END_GUARD_MS)
        t = [ms / 1000 for ms in onsets(os.path.join(PACK, clip))][:32]
        takes(lemus, out, f'ar-rs-shuffle{k}', [(clip, [(s, 'RS', R.ar_code('RS')) for s in t])],
              {}, True, f'rim shot, SHUFFLE {k}, {len(t)} hits')


def oh_cut(lemus, out):
    items = []
    for cd in ('000', '100'):
        for sh in range(1, 8):
            clip = f'OpenHat/OpenHat-Dec100-cut-ClosedHat-Dec{cd}-Shuffle{sh}.wav'
            x = load(clip)
            rate = hh.clock(x[:int(0.12 * SR)], SR, 'oh', t1=0.1)
            x1 = hh.trim_trig(x, SR, 'oh', rate)  # from 1 ms before the OH's trigger
            rx = hh.cut_times(x1, SR, rate)
            t_oh = (len(x) - len(x1)) / SR + 1e-3
            items.append((clip, [(t_oh, 'OH', OH_CUT_CODES[0]), (t_oh + rx[1] - 24e-6, 'CH', OH_CUT_CODES[1])], cd))
    for cd in ('000', '100'):
        sub = [(c, h) for c, h, d in items if d == cd]
        takes(lemus, out, f'ar-oh-cut-ch{cd}', sub, {'oh_decay': 1.0, 'ch_decay': int(cd) / 100}, False,
              f'OH DECAY 100 % cut by CH (DECAY {int(cd)} %) a sixteenth later, SHUFFLE 1-7', measure=cut_measure)


# The codes of the open hat cut by the closed (master out: no single take to compare levels with).
OH_CUT_CODES = (32, 40)


def cut_measure(y, i0, k, hits):
    """The OH's trigger (from its ROM data) and the CH's data start (hh.cut_times), s from i0."""
    lo = max(0, i0 - int(0.01 * SR))
    seg = y[lo:lo + int(1.0 * SR)]
    rate = hh.clock(seg[:int(0.12 * SR)], SR, 'oh', t1=0.1)
    x1 = hh.trim_trig(seg, SR, 'oh', rate)  # from 1 ms before the OH's trigger
    rx = hh.cut_times(x1, SR, rate)
    trig = (lo + len(seg) - len(x1) - i0) / SR + 1e-3
    return [trig, trig + rx[1]]


def flam(lemus, out):
    for v, pat in FLAM_KEYS.items():
        items = []
        for n in range(1, 9):
            clip = pat.format(n=n)
            y = load(clip)
            r0 = rise(y, 0)
            main = flam_interval(y[r0:], SR, v, n + 8) / 1000
            # the grace note at the normal code, the main at the accent's (firmware.md, Flam)
            items.append((clip, [(r0 / SR, v, R.dac_code(v)), (r0 / SR + main, v, R.dac_code(v, 0, True))]))
        takes(lemus, out, f'ar-flam-{v.lower()}', items, FLAM_KNOBS[v], True, f'{v} flams 1-8',
              measure=lambda y, i0, k, hits, v=v: [0.0, flam_interval(y[i0:], SR, v, k + 9) / 1000])


def snare_clap(lemus, out):
    items = []
    for n in range(1, 9):
        clip = f'SnareDrum/Snare909-and-Clap909-shot{n}.wav'
        t = rise(load(clip), 0) / SR
        items.append((clip, [(t, 'SD', R.ar_code('SD')), (t, 'HC', R.ar_code('HC'))]))
    takes(lemus, out, 'ar-snare-clap', items, {'sd_tune': 0.5, 'sd_tone': 1.0, 'sd_snappy': 1.0}, False,
          'snare (TUNE 50 %, TONE 100 %, SNAPPY 100 %) and clap on one step, 8 shots')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--plugintest', default=os.path.join(REPO, 'build', 'tr909', 'tr909plugintest_artefacts', 'Release', 'tr909plugintest'))
    ap.add_argument('--out', default=OUT)
    ap.add_argument('--work', default=os.path.join(REPO, 'build', 'tr909', 'ar_scenes'))
    ap.add_argument('--only', default='beat,rs,oh,flam,clap')
    a = ap.parse_args()
    lemus = Lemus(a.plugintest, a.work)
    only = a.only.split(',')
    if 'beat' in only:
        beat(lemus, a.out)
    if 'rs' in only:
        rs(lemus, a.out)
    if 'oh' in only:
        oh_cut(lemus, a.out)
    if 'flam' in only:
        flam(lemus, a.out)
    if 'clap' in only:
        snare_clap(lemus, a.out)


if __name__ == '__main__':
    sys.exit(main())
