#!/usr/bin/env python3
"""Writes the A/B scenes in host/scenes/ (Lemus against Roland's TR-909 plugin, for Lemus AB and
lemus-render): every voice in turn, a beat, a long kick, the hats choking, the velocity curves,
and knob sweeps.

  scenes.py [--out DIR]

Both plugins are played by MIDI notes with the transport reported stopped ("playing": false), so
neither one's own sequencer runs. The real 909 turns a received velocity into its accent DAC
code through the firmware's table at 1F80 (docs/research.md, C.4: code = table[vel / 2] / 2), so
a plain hit is velocity 50 (code 10) and an accented one 80 (code 21): the levels the 909's own
sequencer plays at TOTAL ACCENT 0 (a step with the instrument's accent; TOTAL ACCENT itself
doesn't act on MIDI notes, in the firmware or in Roland's plugin). Roland's plugin maps velocity
continuously, so it hears the same 50 and 80; tr909-velocity steps through the whole range for
comparing the two curves. Velocity 127 is code 63, past anything the 909's sequencer reaches
(47), and isn't used for accents here.

The 909 has one hi-hat LEVEL and Roland's plugin one per sound: a scene sets `hh_level` and
`oh_level` alike (both reach Lemus's HH Level).
"""
import argparse, json, os

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
# The firmware's receive map (research.md C.5) and Roland's (docs/roland-plugin/note-map.txt) agree.
NOTE = {'BD': 36, 'RS': 37, 'SD': 38, 'HC': 39, 'LT': 43, 'CH': 42, 'MT': 47, 'OH': 46, 'HT': 50, 'CR': 49, 'RD': 51}
PLAIN, ACCENT = 50, 80
KNOBS = {'total_accent': 0.0, 'volume': 0.85,
         'bd_tune': 0.5, 'bd_attack': 0.5, 'bd_decay': 0.5, 'sd_tune': 0.5, 'sd_tone': 0.5, 'sd_snappy': 0.5,
         'lt_tune': 0.5, 'lt_decay': 0.5, 'mt_tune': 0.5, 'mt_decay': 0.5, 'ht_tune': 0.5, 'ht_decay': 0.5,
         'ch_decay': 0.5, 'oh_decay': 0.5, 'cr_tune': 0.5, 'rd_tune': 0.5}
LEVELS = {k: 0.75 for k in ('bd_level', 'sd_level', 'lt_level', 'mt_level', 'ht_level', 'rs_level', 'hc_level',
                             'hh_level', 'oh_level', 'cr_level', 'rd_level')}
SOURCES = [{'use': 'tr909.lemus'}, {'use': 'tr909.roland'}]


def hit(at, voice, accent=False, vel=None):
    return {'at': round(at, 4), 'note': NOTE[voice], 'len': 0.1, 'vel': vel or (ACCENT if accent else PLAIN)}


def scene(name, bpm, beats, notes, params=None, tail=1.5, repeat=None, automation=None):
    s = {'name': name, 'bpm': bpm, 'beats': beats, 'tail': tail, 'playing': False,
         'params': {**KNOBS, **LEVELS, **(params or {})}, 'notes': sorted(notes, key=lambda n: (n['at'], n['note']))}
    if automation:
        s['automation'] = automation
    if repeat:
        s['repeat'] = repeat
    s['sources'] = SOURCES
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(MACHINE, 'host', 'scenes'))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    scenes = []
    order = ['BD', 'SD', 'LT', 'MT', 'HT', 'RS', 'HC', 'CH', 'OH', 'CR', 'RD']

    # every voice alone, plain then accented, a second apart (two for the cymbals)
    notes, at = [], 0
    for v in order:
        step = 4 if v in ('CR', 'RD') else 2
        notes += [hit(at, v), hit(at + step, v, True)]
        at += 2 * step
    scenes.append(scene('tr909-voices', 120, at, notes, tail=2.5))

    # a beat with every voice: one bar, played four times; accents on the BD downbeat, the
    # second snare and the open hats
    beat = [(0, 'BD', True), (0, 'CR'), (0.5, 'OH', True), (1, 'BD'), (1, 'SD'), (1, 'HC'), (1.5, 'OH', True),
            (1.75, 'RS'), (2, 'BD'), (2.5, 'OH', True), (2.75, 'LT'), (3, 'BD'), (3, 'SD', True), (3, 'HC'),
            (3.25, 'MT'), (3.5, 'OH', True), (3.75, 'HT')]
    beat += [(i / 4, 'CH') for i in range(16) if i % 2 == 1 and i not in (1, 5, 9, 13)]
    beat += [(i / 4, 'RD') for i in (0, 4, 8, 12)]
    scenes.append(scene('tr909-beat', 126, 4, [hit(*b) for b in beat], repeat=4))

    # the long kick: BD DECAY up, ATTACK full, TUNE at three quarters, under a sparse beat
    boom = [(0, 'BD', True), (0.75, 'BD'), (2.5, 'BD'), (1, 'SD'), (3, 'SD', True)] + [(i / 2 + 0.5, 'CH') for i in range(0, 8, 2)]
    scenes.append(scene('tr909-boom', 120, 4, [hit(*b) for b in boom], params={'bd_decay': 1.0, 'bd_tune': 0.75, 'bd_attack': 1.0},
                        tail=3, repeat=4))

    # the hats: 16th CH with accents, then OH (DECAY full) choked by CH after 1/16, 1/8, 1/4 and
    # 1/2 beat, then OH left to ring
    hats = [(i / 4, 'CH', i % 4 == 0) for i in range(16)]
    t = 4
    for gap in (0.25, 0.5, 1.0, 2.0):
        hats += [(t, 'OH', True), (t + gap, 'CH')]
        t += 4
    hats += [(t, 'OH', True)]
    scenes.append(scene('tr909-hats', 120, t + 4, [hit(*h) for h in hats], params={'oh_decay': 1.0, 'ch_decay': 0.5}, tail=1.5))

    # velocity: BD, SD, LT, RS, HC, CH, CR at 20 .. 127, half a beat apart
    vels = [20, 36, 50, 64, 80, 96, 110, 127]
    notes, at = [], 0
    for v in ('BD', 'SD', 'LT', 'RS', 'HC', 'CH', 'CR'):
        for vel in vels:
            notes.append(hit(at, v, vel=vel))
            at += 2 if v in ('BD', 'LT', 'CR') else 1
        at += 2
    scenes.append(scene('tr909-velocity', 120, at, notes, tail=2.5))

    # knob sweeps: one voice on every beat while a knob goes 0 -> 1 over 8 bars
    sweeps = [('tr909-sweep-bd-tune', 'BD', 'bd_tune', {'bd_decay': 0.75}),
              ('tr909-sweep-bd-decay', 'BD', 'bd_decay', {}),
              ('tr909-sweep-sd-tune', 'SD', 'sd_tune', {'sd_snappy': 0.3}),
              ('tr909-sweep-sd-tone', 'SD', 'sd_tone', {'sd_snappy': 1.0}),
              ('tr909-sweep-lt-tune', 'LT', 'lt_tune', {}),
              ('tr909-sweep-ht-decay', 'HT', 'ht_decay', {}),
              ('tr909-sweep-oh-decay', 'OH', 'oh_decay', {}),
              ('tr909-sweep-cr-tune', 'CR', 'cr_tune', {}),
              ('tr909-sweep-rd-tune', 'RD', 'rd_tune', {})]
    for name, v, knob, extra in sweeps:
        every = 2 if v in ('CR', 'RD') else 1
        notes = [hit(b, v, accent=False) for b in range(0, 32, every)]
        scenes.append(scene(name, 120, 32, notes, params={**extra, knob: 0.0}, tail=2.0,
                            automation=[{'param': knob, 'points': [[0, 0.0], [31, 1.0]]}]))

    for s in scenes:
        with open(os.path.join(a.out, s['name'] + '.json'), 'w') as f:
            json.dump(s, f, indent=1)
            f.write('\n')
        print(s['name'], len(s['notes']), 'notes')


if __name__ == '__main__':
    main()
