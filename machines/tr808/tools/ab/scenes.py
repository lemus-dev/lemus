#!/usr/bin/env python3
"""Writes the A/B scenes in host/scenes/ (Lemus against Roland's TR-808 plugin, for Lemus AB and
lemus-render): factory rhythms from core/factory.cpp, a beat, every voice in turn, and a long kick.

  scenes.py [--out DIR]

Both plugins are played by MIDI notes with the transport reported stopped ("playing": false), or
Roland's own sequencer plays along. A plain hit is velocity 64 and an accented one 127: Roland's
notes set the trigger level continuously (64 is its plain level, as in tools/compare/roland.py;
its AC LEVEL acts only on its own sequencer), and Lemus accents from 100 up, by AC LEVEL, which
is at max here. Only the voices both play the same are used: Roland's conga, claves and maracas
notes play the toms, rim shot and clap (its voice switches aren't host parameters), so the latin
rhythms, which want them, are left out.
"""
import argparse, json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
NOTE = {'BD': 36, 'SD': 38, 'LT': 43, 'MT': 47, 'HT': 50, 'RS': 37, 'CP': 39, 'CB': 56, 'CY': 49, 'OH': 46, 'CH': 42}
PLAIN, ACCENT = 64, 127
KNOBS = {'ac_level': 1.0, 'volume': 0.7,
         'bd_tone': 0.5, 'bd_decay': 0.5, 'sd_tone': 0.5, 'sd_snappy': 0.5, 'lt_tuning': 0.5, 'mt_tuning': 0.5,
         'ht_tuning': 0.5, 'cy_tone': 0.5, 'cy_decay': 0.5, 'oh_decay': 0.5}
LEVELS = {k: 0.75 for k in ('bd_level', 'sd_level', 'lt_level', 'mt_level', 'ht_level', 'rs_level', 'cp_level',
                             'cb_level', 'cy_level', 'oh_level', 'ch_level')}
SOURCES = [{'use': 'tr808.lemus'}, {'use': 'tr808.roland'}]
# factory rhythms: (scene name, BASIC RHYTHM key 1-12, tempo)
RHYTHMS = [('tr808-rock', 3, 120), ('tr808-track8', 8, 116), ('tr808-track9', 9, 104), ('tr808-track10', 10, 112)]


def hit(at, voice, accent=False):
    return {'at': round(at, 4), 'note': NOTE[voice], 'len': 0.1, 'vel': ACCENT if accent else PLAIN}


def scene(name, bpm, beats, notes, params=None, tail=1.5, repeat=None):
    s = {'name': name, 'bpm': bpm, 'beats': beats, 'tail': tail, 'playing': False,
         'params': {**KNOBS, **LEVELS, **(params or {})}, 'notes': sorted(notes, key=lambda n: (n['at'], n['note']))}
    if repeat:
        s['repeat'] = repeat
    s['sources'] = SOURCES
    return s


def charts():
    """core/factory.cpp's kCharts: {key: {variation: {row: pattern}}}, 16-step 4/4 charts only."""
    src = open(os.path.join(MACHINE, 'core', 'factory.cpp')).read()
    out = {}
    for key, prescale, var, rows in re.findall(r'\{(\d+), (\d+), (\d+), "([^"]*)"\}', src):
        f = rows.split()
        rows = dict(zip(f[0::2], f[1::2]))
        if int(prescale) == 2 and all(len(p) == 16 for p in rows.values()):
            out.setdefault(int(key) + 1, {})[int(var)] = rows
    return out


def rhythm(name, key, bpm, chart):
    """Variation A then B, a bar each (a sixteenth a step), the AC row accenting its steps."""
    notes = []
    for bar, var in enumerate(sorted(chart)):
        rows = chart[var]
        acc = rows.get('AC', '.' * 16)
        for row, pattern in rows.items():
            if row == 'AC':
                continue
            notes += [hit(bar * 4 + i / 4, row, acc[i] == 'a') for i, c in enumerate(pattern) if c != '.']
    return scene(name, bpm, 4 * len(chart), notes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(MACHINE, 'host', 'scenes'))
    a = ap.parse_args()
    scenes = []
    # a beat with every voice: one bar, played four times
    beat = [(0, 'CH'), (0, 'BD', True), (0, 'CY'), (0.5, 'CH'), (0.75, 'RS'), (1, 'CH'), (1, 'BD'), (1, 'SD'),
            (1.5, 'CH'), (1.75, 'LT'), (2, 'CH'), (2, 'BD'), (2.25, 'CB'), (2.5, 'CH'), (2.5, 'MT'), (3, 'CH'),
            (3, 'BD'), (3, 'SD'), (3, 'CP'), (3.25, 'HT'), (3.5, 'CH'), (3.5, 'OH')]
    scenes.append(scene('tr808-beat', 120, 4, [hit(*b) for b in beat], repeat=4))
    # every voice alone, plain then accented, a second apart
    order = ['BD', 'SD', 'LT', 'MT', 'HT', 'RS', 'CP', 'CB', 'CH', 'OH', 'CY']
    scenes.append(scene('tr808-voices', 120, 4 * len(order),
                        [hit(4 * i + 2 * k, v, k == 1) for i, v in enumerate(order) for k in (0, 1)], tail=2.5))
    # the long kick: BD decay up, tone down, under a sparse beat
    boom = [(0, 'BD', True), (0.75, 'BD'), (2.5, 'BD'), (1, 'SD'), (3, 'SD', True)] + [(i / 2, 'CH') for i in range(8)]
    scenes.append(scene('tr808-boom', 90, 4, [hit(*b) for b in boom], params={'bd_decay': 1.0, 'bd_tone': 0.2}, tail=3,
                        repeat=4))
    ch = charts()
    for name, key, bpm in RHYTHMS:
        scenes.append(rhythm(name, key, bpm, ch[key]))
    for s in scenes:
        with open(os.path.join(a.out, s['name'] + '.json'), 'w') as f:
            json.dump(s, f, indent=1)
            f.write('\n')
        print(s['name'], len(s['notes']), 'notes')


if __name__ == '__main__':
    main()
