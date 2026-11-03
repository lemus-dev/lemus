#!/usr/bin/env python3
"""A/B scenes of a real TR-909 playing its factory song against Lemus's, for Lemus AB and
lemus-render (tools/host/README.md): factory-song, the whole of track 1 (65 measures, about 2'17"),
and factory-pattern-N, one measure of each pattern in it, looped.

The real one is zibbybone's video "Roland TR-909 Factory Song" (2016; the reference index's
videos/zibbybone_tr909_factory_song): a unit loaded with the factory patterns (TRACK 1 and
PATTERN 1 held at power-on, which only firmware v1-v3 do) playing track 1, recorded from its
output (a line: about -76 dB between takes), Opus at about 100 kbps. Lemus's is the same memory
(core/factory.h, made by the v1.0 ROM's loader) played by the plugin's processor from START,
rendered by `tr909plugintest --render` with "factory", its master pair in mono. The notes,
accents, flams and shuffle come from the firmware on both sides; what the video doesn't give was
fitted to it (2026-10-26):

- The tempo. The real one plays at the TEMPO A/D's B = 101 (114.9 BPM) for 32.5 s from START,
  then at B = 100 (113.7 BPM): the knob nudged, or its reading crossing the firmware's one-count
  dead band (docs/firmware.md, Sync). Lemus's knob does the same. The video's hits then land on
  ours through the song with a spread of about 2 ms (the scans' jitter, both sides), its clock
  0.03 % slow against ours: it is resampled onto our timeline (half a cent).
- The board: a unit of serial 392600-415299, before the 415300 changes to the rim shot and the
  hi-hat (core/revisions.h), as its firmware says (Ver. 1 came in at 403900).
- The knobs, TOTAL ACCENT and the voices' balance (LEVELs): fitted on the song's first 70 s,
  sixth-octave band powers in 10 ms frames, log error, all voices at once with one smooth EQ for
  the video's unknown chain (Lemus's side isn't EQ'd: what the chain did can't be told from
  what Lemus's master out does differently). Each voice's grid (BD and SD 5 x 5 x 5, toms and the
  hats' two decays 11 x 11, cymbals 11) against the video, the others at their best, twice. The
  LEVELs keep the fitted balance with the loudest voice at full.

The pattern loops are cut from the two takes at the same place: a measure of each pattern after
the tempo settled, the one whose end sounds most like what led into it (both takes), so the loop
sounds like the pattern played again; it starts 10 ms before the measure, is a measure of B = 100
long and crossfades into its start over its last 5 ms. The measures' starts are their first hits
(every pattern has one on step 1) from tr909seq, run as the render is: its hits land on the
render's, and the video's, within the scan's jitter. The track's patterns are read from the memory
tr909factory makes (a chained pattern plays with the next in one measure: 7-8).

A pattern scene's sources: real, and lemus, the plugin live (Edit opens it): the measure's hits
as MIDI notes, each at a velocity the firmware turns into the hit's accent DAC code, 2 ms early for
the MIDI's own lag; the fitted knobs, the board in its state; TOTAL ACCENT plays no part, as for
any MIDI note. It plays within about 0.7 dB of Lemus's take of the measure, 1-2 dB in sixth
octaves (MIDI plays a step's notes one after another, the sequencer at once).

The video can't be published: the scenes and their WAVs go to the private folder.

  factory_song.py [--plugintest PATH] [--seq PATH] [--factory PATH] [--out DIR] [--patterns DIR] [--work DIR]
"""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.normpath(os.path.join(HERE, '..', '..'))
REPO = os.path.normpath(os.path.join(MACHINE, '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..', 'compare'))
sys.dont_write_bytecode = True
import real909 as R  # noqa: E402

VIDEO = os.path.join(R.REAL909, 'videos', 'zibbybone_tr909_factory_song', 'wav', 'zibbybone_tr909_factory_song.wav')
OUT = os.path.join(MACHINE, 'private', 'reference', 'scenes', 'video')
PATTERNS = os.path.join(MACHINE, 'private', 'reference', 'scenes', 'factory-patterns')
BUILD = os.path.join(REPO, 'build', 'tr909')
SR = 48000  # the video's
LEAD = 3.0  # s: START pressed, past the 909's boot and its master's power-on mute (README, Power-on)
SONG = 137.0  # s from START to the song's last hit
TAIL = 2.5
# the TEMPO knob: rotation 78/255 reads B = 101 (the dead band, from above), 77/255 then B = 100
TEMPO = [(0.0, 78 / 255), (32.475, 77 / 255)]  # (s after START, rotation)
# the video's time at our START, and its clock against ours
VIDEO_START, VIDEO_CLOCK = 11.5656, 0.9997
BAR = 192 * (1099138 // 100) / 1e6  # s: a measure at B = 100, 192 ticks of floor(1099138 / B) us
PRE, FADE = 0.010, 0.005  # s: a loop's start before its measure, its crossfade
MIDI_LAG = 0.002  # s: a MIDI note's hit after the note (the bytes at 31250 baud, then the scan), measured
NOTE = {'BD': 36, 'RS': 37, 'SD': 38, 'HC': 39, 'LT': 43, 'CH': 42, 'MT': 47, 'OH': 46, 'HT': 50, 'CR': 49, 'RD': 51}
STATE = 'tr909-sn403900.state'
SERIAL = 403900
# the fit (rotation 0-1, to the grid's step: BD and SD 25 %, the others 10 %)
KNOBS = {'bd_tune': 0.75, 'bd_attack': 0.75, 'bd_decay': 0.75, 'sd_tune': 0.5, 'sd_tone': 0.5, 'sd_snappy': 0.5,
         'lt_tune': 0.5, 'lt_decay': 0.4, 'mt_tune': 0.5, 'mt_decay': 0.1, 'ht_tune': 0.5, 'ht_decay': 0.3,
         'ch_decay': 0.2, 'oh_decay': 0.8, 'cr_tune': 0.3, 'rd_tune': 0.3, 'accent': 0.4}
LEVELS = {'bd_level': 0.49, 'sd_level': 0.59, 'lt_level': 0.39, 'mt_level': 0.47, 'ht_level': 0.40, 'rs_level': 0.45,
          'hc_level': 0.46, 'hh_level': 1.0, 'cr_level': 0.79, 'rd_level': 0.35}


def lemus(exe, work):
    """Lemus's take: the factory song from START, the master pair in mono (cached in `work`)."""
    job = {'rate': SR, 'seconds': LEAD + SONG + TAIL, 'factory': True, 'start': LEAD, 'serial': SERIAL,
           'params': {**KNOBS, **LEVELS, 'tempo': TEMPO[0][1]},
           'changes': [[LEAD + t, 'tempo', v] for t, v in TEMPO[1:]]}
    os.makedirs(work, exist_ok=True)
    key = hashlib.md5((json.dumps(job, sort_keys=True) + str(os.stat(exe).st_mtime_ns)).encode()).hexdigest()[:20]
    wav = os.path.join(work, key + '.wav')
    if not os.path.exists(wav):
        js = os.path.join(work, key + '.json')
        with open(js, 'w') as f:
            json.dump(job, f)
        subprocess.run([exe, '--render', js, wav], check=True, capture_output=True)
        os.remove(js)
    x, sr = sf.read(wav, dtype='float64', always_2d=True)
    assert sr == SR
    return x[:, :2].mean(axis=1)


def video(n):
    """The video's audio in mono on our timeline (n samples): ours at t is the video's at
    VIDEO_START + VIDEO_CLOCK (t - LEAD), interpolated at 4x."""
    x, sr = sf.read(VIDEO, dtype='float64', always_2d=True)
    assert sr == SR
    up = 4
    xu = resample_poly(x.mean(axis=1), up, 1)
    pos = (VIDEO_START + VIDEO_CLOCK * (np.arange(n) / SR - LEAD)) * SR * up
    i = np.clip(pos.astype(np.int64), 0, len(xu) - 2)
    f = pos - i
    y = xu[i] * (1 - f) + xu[i + 1] * f
    y[(pos < 0) | (pos >= len(xu) - 1)] = 0
    return y


def track(exe, work):
    """Track 1 of the factory memory: each measure's pattern name ('7-8' for 7 chained to 8)."""
    ram = os.path.join(work, 'factory.bin')
    subprocess.run([exe, '--v1', os.path.join(R.ROM_DIR, 'tr909_v1.ic609'), '--out', ram], check=True, capture_output=True)
    with open(ram, 'rb') as f:
        d = f.read()
    end = d[0xF86] | d[0xF87] << 8  # the bank offset of its last measure (docs/firmware.md, RAM layout)
    names = []
    for a in range(0xC00, end + 1):
        p = d[a] >> 4  # group 1 only
        name = str(p + 1)
        while p < 15 and d[0xF80 + 8 * p] & 0x80:
            p += 1
            name += f'-{p + 1}'
        names.append(name)
    return ram, names


def measures(exe, ram, n):
    """The song's hits [(s, voice, accent DAC code)] on our timeline, from tr909seq run as the
    render is (TEMPO, TOTAL ACCENT, START at LEAD, the changes), and the n measures' starts: the
    first hit of each."""
    script = [f'tempo {TEMPO[0][1]:.6f}', f'accent {KNOBS["accent"]:.6f}', 'log hits', f'run {LEAD}', 'down START', 'run 0.1', 'up START']
    now = LEAD + 0.1
    for t, v in TEMPO[1:]:
        script += [f'run {LEAD + t - now:.6f}', f'tempo {v:.6f}']
        now = LEAD + t
    script.append(f'run {LEAD + SONG + TAIL - now:.6f}')
    out = subprocess.run([exe, '--ram', ram, '; '.join(script)], check=True, capture_output=True, text=True).stdout
    hits = sorted((float(m.group(1)) / 1000, m.group(2), int(m.group(3))) for m in re.finditer(r'([\d.]+) ms  HIT (\S+) (\d+)', out))
    hits = [h for h in hits if h[0] > LEAD]
    t = np.array([h[0] for h in hits])
    starts = [t[0]]
    while len(starts) < n:
        c = t[np.argmin(abs(t - starts[-1] - BAR))]  # a measure on: the tempo moves it by ms, a step is 130 ms
        starts.append(t[(t > c - 0.004) & (t <= c)].min())  # the step's first hit (a flam's comes later)
    return hits, np.array(starts)


def velocity(voice, code):
    """The lowest MIDI velocity the firmware turns into `code` (real909.midi_code), else the
    nearest code's."""
    return min(range(1, 128), key=lambda v: (abs(R.midi_code(voice, v) - code), v))


def state(path, serial):
    """The live source's plugin state, as the plugin saves it: the board of `serial`, the rest as
    a new instance (the scene sets the knobs). The Lemus shell's ValueTree (host/LemusProcessor:
    LEMUS, version, machine, state) around the 909's XML (copyXmlToBinary), in JUCE's binary
    ValueTree format; the host hands it to the VST3 as its component's state."""
    def cint(v):  # OutputStream::writeCompressedInt, v >= 0
        b = v.to_bytes(4, 'little').rstrip(b'\0')
        return bytes([len(b)]) + b

    def text(s):
        return s.encode() + b'\0'

    xml = f'<?xml version="1.0" encoding="UTF-8"?>\n<TR909 serial="{serial}"/>'.encode()
    machine = struct.pack('<II', 0x21324356, len(xml)) + xml + b'\0'
    props = [('version', cint(5) + b'\x01' + struct.pack('<i', 1)),  # var: int
             ('machine', cint(len('tr909') + 2) + b'\x05' + text('tr909')),  # string
             ('state', cint(1 + len(machine)) + b'\x08' + machine)]  # binary
    with open(path, 'wb') as f:
        f.write(text('LEMUS') + cint(len(props)) + b''.join(text(k) + v for k, v in props) + cint(0))


def loop(x, t):
    """A measure of x from t (s) as a seamless loop: from PRE before it, BAR long, its last FADE
    crossfaded into what came before its start."""
    a, n, f = int(round((t - PRE) * SR)), int(round(BAR * SR)), int(FADE * SR)
    y = x[a:a + n].copy()
    w = 0.5 - 0.5 * np.cos(np.pi * np.arange(f) / f)
    y[n - f:] = y[n - f:] * (1 - w) + x[a - f:a] * w
    return y


def seam(x, t, win=0.05):
    """How unlike a measure's end is to what led into its start: the rms of their difference
    over win, against the measure's own rms."""
    a, b, w = int(round((t - PRE) * SR)), int(round((t - PRE + BAR) * SR)), int(win * SR)
    return np.sqrt(np.mean((x[b - w:b] - x[a - w:a]) ** 2) / np.mean(x[a:b] ** 2))


def patterns(names, starts, hits, takes, out):
    """factory-pattern-N: per pattern, the measure (after the tempo settled, not the last) that
    loops best in both takes; the takes cut from it, and Lemus live playing its hits as MIDI."""
    settled = LEAD + TEMPO[-1][0] + BAR
    bpm = 240 * SR / round(BAR * SR)  # 4 beats a measure: the loop is exactly its WAVs' length
    os.makedirs(os.path.join(out, 'wav'), exist_ok=True)
    state(os.path.join(out, STATE), SERIAL)
    params = {('total_accent' if k == 'accent' else k): v for k, v in {**KNOBS, **LEVELS}.items()}
    for name in sorted(set(names), key=lambda s: int(s.split('-')[0])):
        ks = [k for k in range(len(names) - 1) if names[k] == name and starts[k] > settled]
        k = min(ks, key=lambda k: sum(seam(y, starts[k]) for y in takes.values()))
        t0 = starts[k] - PRE
        notes = [{'at': round((t - t0 - MIDI_LAG) * bpm / 60, 6), 'note': NOTE[v], 'len': 0.05, 'vel': velocity(v, c)}
                 for t, v, c in hits if t0 <= t < t0 + BAR]
        rel = f'wav/factory-pattern-{name}-real.wav'
        sf.write(os.path.join(out, rel), loop(takes['real'], starts[k]).astype(np.float32), SR, subtype='FLOAT')
        sources = [{'label': 'real', 'audio': rel}, {'use': 'tr909.lemus', 'state': STATE}]
        scene = {'name': f'tr909-factory-pattern-{name}', 'bpm': bpm, 'beats': 4, 'tail': 0, 'rate': SR, 'playing': False,
                 'params': params, 'notes': notes, 'sources': sources}
        with open(os.path.join(out, f'factory-pattern-{name}.json'), 'w') as f:
            json.dump(scene, f, indent=1)
        print(f'factory-pattern-{name}: measure {k + 1} of {len(names)}, {len(notes)} hits')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--plugintest', default=os.path.join(BUILD, 'tr909plugintest_artefacts', 'Release', 'tr909plugintest'))
    ap.add_argument('--seq', default=os.path.join(BUILD, 'tr909seq'))
    ap.add_argument('--factory', default=os.path.join(BUILD, 'tr909factory'))
    ap.add_argument('--out', default=OUT)
    ap.add_argument('--patterns', default=PATTERNS)
    ap.add_argument('--work', default=os.path.join(BUILD, 'factory_song'))
    a = ap.parse_args()
    ours = lemus(a.plugintest, a.work)
    real = video(len(ours))
    i0 = int((LEAD - 0.05) * SR)
    os.makedirs(os.path.join(a.out, 'wav'), exist_ok=True)
    srcs = []
    for label, y in (('real', real), ('lemus', ours)):
        rel = f'wav/factory-song-{label}.wav'
        sf.write(os.path.join(a.out, rel), y[i0:].astype(np.float32), SR, subtype='FLOAT')
        srcs.append({'label': label, 'audio': rel})
    seconds = (len(ours) - i0) / SR
    scene = {'name': 'tr909-factory-song', 'bpm': 60, 'beats': round(seconds, 4), 'tail': 0, 'rate': SR, 'sources': srcs}
    with open(os.path.join(a.out, 'factory-song.json'), 'w') as f:
        json.dump(scene, f, indent=1)
    print(f'factory-song: {seconds:.1f} s, {a.out}')
    ram, names = track(a.factory, a.work)
    hits, starts = measures(a.seq, ram, len(names))
    patterns(names, starts, hits, {'real': real, 'lemus': ours}, a.patterns)


if __name__ == '__main__':
    main()
