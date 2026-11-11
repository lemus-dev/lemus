"""Oracle replay against Gearmulator's Osirus: the same program and note into both, compared per program.

Both run the same DSP engine; what differs is the control processor. Here the firmware runs on an
emulated 80C515; Osirus replaces it with C++. So the words each sends the DSP are compared one by one
(the program's upload, the note's MIDI bytes), and then the sound of the note: its onset, level,
pitch and spectrum. Where they differ, one of the two firmware paths, or this board's timing, is wrong.

Each program plays on a freshly booted board, as Osirus starts each one fresh. A note's sound still
depends on time since boot on both (free-running LFOs, noise, a phaser left at rate 0, the
arpeggiator's clock), so the sound is judged against a control: Osirus with the note 0.34 s later.

Usage:
  replay.py run OUT --viruscrun PATH --osirus PATH --rom PATH [--banks CDEFGH] [--programs N] [--note 60]
  replay.py compare OUT [--banks CDEFGH] [--programs N]
  replay.py controllers OUT --viruscrun PATH --osirus PATH --rom PATH
      C126 "- Init -", then a note and every kind of channel message into both: the DSP words compared.

`--osirus` is Gearmulator's virusTestConsole with the local patches that read VIRUS_ROM, VIRUS_OUT,
VIRUS_SAMPLES, VIRUS_SCRIPT, VIRUS_NOTES and VIRUS_WORDLOG (private/virusc/reference). Its program
numbers count the flash banks from 0, which are banks C-H here; for banks A and B (`--banks AB`) it plays
C and D, which a fresh memory's A and B are copies of.
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np

RATE = 46875
BOOT = 13.0      # seconds: this board's firmware has initialised a fresh memory by about 11.2 s
STEP = 2.5       # seconds per program: change, note on 0.5 s later, off 1 s after that
# Each program on a freshly booted board, as Osirus starts each one fresh: in one long run, what the
# program before left (delay and reverb tails, free-running phases) would be compared too.
NOTE_AT, NOTE_LEN = 0.5, 1.0
BANKS = 'ABCDEFGH'
# A fresh memory's banks A and B (battery RAM) are copies of flash banks C and D, so Osirus plays those for them:
# the same sounds, reached through the RAM window here instead of the flash.
OSIRUS_BANK = {'A': 'C', 'B': 'D'}


def run_ours(args):
    out, viruscrun, rom, bank, program, note = args
    name = f'ours-{BANKS[bank]}{program:03d}'
    script = os.path.join(out, name + '.script')
    with open(script, 'w') as f:
        f.write(f'{BOOT:.3f} midi 176 32 {bank}\n{BOOT:.3f} midi 192 {program} -1\n'
                f'{BOOT + NOTE_AT:.3f} note {note} 100\n{BOOT + NOTE_AT + NOTE_LEN:.3f} note {note} 0\n')
    seconds = BOOT + STEP
    env = dict(os.environ, VC_SCRIPT=script, VC_WORDS=f'{BOOT - 0.1},{seconds}')
    r = subprocess.run([viruscrun, rom, str(seconds), os.path.join(out, name + '.wav')], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)
    with open(os.path.join(out, name + '.words'), 'w') as f:
        f.write('\n'.join(l.split()[6] for l in r.stdout.splitlines() if l.startswith('WORD')) + '\n')
    os.remove(script)
    return name


SHIFT = 0.3437   # seconds: the control run plays the note this much later, so LFOs and free phases differ


def run_osirus(args):
    out, osirus, rom, bank, program, note, control = args
    bank = BANKS.index(OSIRUS_BANK.get(BANKS[bank], BANKS[bank]))
    name = f'osirus{"2" if control else ""}-{BANKS[bank]}{program:03d}'
    script = os.path.join(out, 'osirus2.script' if control else 'osirus.script')
    env = dict(os.environ, VIRUS_ROM=rom, VIRUS_OUT=os.path.join(out, name + '.wav'), VIRUS_SAMPLES=str(int(STEP * RATE)),
               VIRUS_SCRIPT=script, VIRUS_NOTES='0', VIRUS_WORDLOG='1')
    r = subprocess.run([osirus, str((bank - 2) * 128 + program)], env=env, stdin=subprocess.DEVNULL,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    words = [l.split()[1] for l in r.stderr.splitlines()[-400:] if l.startswith('RXWORD')]
    with open(os.path.join(out, name + '.words'), 'w') as f:
        f.write('\n'.join(words) + '\n')
    return name


def run(a):
    os.makedirs(a.out, exist_ok=True)
    banks = [BANKS.index(b) for b in a.banks]
    with open(os.path.join(a.out, 'osirus.script'), 'w') as f:
        f.write(f'{NOTE_AT} note {a.note} 100\n{NOTE_AT + NOTE_LEN} note {a.note} 0\n')
    with open(os.path.join(a.out, 'osirus2.script'), 'w') as f:
        f.write(f'{NOTE_AT + SHIFT} note {a.note} 100\n{NOTE_AT + SHIFT + NOTE_LEN} note {a.note} 0\n')
    with ProcessPoolExecutor(a.jobs) as pool:
        jobs = [pool.submit(run_ours, (a.out, a.viruscrun, a.rom, b, p, a.note))
                for b in banks for p in range(a.programs)] if a.only != 'osirus' else []
        jobs += [pool.submit(run_osirus, (a.out, a.osirus, a.rom, b, p, a.note, c))
                 for b in banks for p in range(a.programs) for c in (False, True)] if a.only != 'ours' else []
        for n, j in enumerate(jobs):
            j.result()
            if n % 256 == 0:
                print(f'{n + 1}/{len(jobs)}', flush=True)


# ---------------------------------------------------------------------------------------- compare

def read_wav(path):
    b = open(path, 'rb').read()
    fmt, ch = int.from_bytes(b[20:22], 'little'), int.from_bytes(b[22:24], 'little')
    bits = int.from_bytes(b[34:36], 'little')
    i = b.find(b'data')
    n = int.from_bytes(b[i + 4:i + 8], 'little')
    raw = b[i + 8:i + 8 + n]
    if fmt == 3:
        d = np.frombuffer(raw, dtype=np.float32).astype(float)
    elif bits == 24:
        x = np.frombuffer(raw[:len(raw) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = x[:, 0] | (x[:, 1] << 8) | (x[:, 2] << 16)
        d = np.where(v >= 1 << 23, v - (1 << 24), v).astype(float) / (1 << 23)
    else:
        d = np.frombuffer(raw, dtype=np.int16).astype(float) / 32768
    return d.reshape(-1, ch)


def upload(words):
    """The last program upload: from its header to the end."""
    starts = [i for i, w in enumerate(words) if w == 'f47555']
    return words[starts[-1]:] if starts else []


def midi_bytes(words):
    """Words that carry one MIDI byte (the top byte), after the upload."""
    return [w[:2] for w in words]


ONSET = 1e-4
EDGES = 100 * 2 ** (np.arange(0, 22) / 3)    # third octaves, 100 Hz - 12.7 kHz


def bands(x):
    w = np.hanning(len(x))
    s = np.abs(np.fft.rfft(x * w)) ** 2
    f = np.fft.rfftfreq(len(x), 1 / RATE)
    return np.array([s[(f >= lo) & (f < hi)].sum() for lo, hi in zip(EDGES[:-1], EDGES[1:])]) + 1e-20


def f0(x):
    """Strongest spectral peak's frequency, refined by parabolic interpolation (Hz)."""
    n = len(x)
    s = np.abs(np.fft.rfft(x * np.hanning(n), 8 * n))
    f = np.fft.rfftfreq(8 * n, 1 / RATE)
    k = int(np.argmax(s[f > 30])) + int(np.sum(f <= 30))
    if 0 < k < len(s) - 1:
        a, b, c = np.log(s[k - 1:k + 2] + 1e-30)
        k = k + 0.5 * (a - c) / (a - 2 * b + c)
    return k * RATE / (8 * n)


def note_features(x):
    """x: mono, starting 50 ms before the note-on. Onset (ms after note-on), level and spectrum of the held note."""
    lead = int(0.05 * RATE)
    hit = np.nonzero(np.abs(x) > ONSET)[0]
    if len(hit) == 0:
        return None
    on = hit[0]
    hold = x[on + int(0.1 * RATE):on + int(0.9 * RATE)]
    if len(hold) < RATE // 4:
        return None
    rms = np.sqrt(np.mean(hold ** 2))
    return {'onset_ms': (on - lead) * 1000 / RATE, 'rms_db': 20 * np.log10(rms + 1e-12),
            'bands': 10 * np.log10(bands(hold)), 'f0': f0(hold)}


def compare_program(a):
    out, letter, p = a
    theirs_letter = OSIRUS_BANK.get(letter, letter)
    t0 = BOOT
    mine = open(os.path.join(out, f'ours-{letter}{p:03d}.words')).read().split()
    ours_wav = read_wav(os.path.join(out, f'ours-{letter}{p:03d}.wav'))
    theirs = open(os.path.join(out, f'osirus-{theirs_letter}{p:03d}.words')).read().split()
    um, ut = upload([w for w in mine]), upload(theirs)
    # the upload ends where the note's MIDI bytes begin: three words each for on and off
    um, nm = um[:-6], um[-6:]
    ut, nt = ut[:-6], ut[-6:]
    diffs = [(i, x, y) for i, (x, y) in enumerate(zip(um, ut)) if x != y]
    # page A bytes 1 and 2 (word 2, after the version): the firmware sends live values there, not the stored ones
    live = [d for d in diffs if d[0] == 2 and d[1][:2] == d[2][:2]]
    stale_tail = [d for d in diffs if d[0] == len(um) - 1 and d[1][:2] == d[2][:2]]
    other = [d for d in diffs if d not in live and d not in stale_tail]
    r = {'program': f'{letter}{p}', 'upload_len': (len(um), len(ut)), 'upload_other_diffs': other[:6],
         'n_other': len(other) + abs(len(um) - len(ut)), 'live_bytes': [d[1:] for d in live],
         'note_words_same': midi_bytes(nm) == midi_bytes(nt)}
    s = int((t0 + NOTE_AT - 0.05) * RATE)
    fo = note_features(ours_wav[s:s + int((STEP - NOTE_AT) * RATE)].mean(1))
    w = read_wav(os.path.join(out, f'osirus-{theirs_letter}{p:03d}.wav'))
    s = int((NOTE_AT - 0.05) * RATE)
    fg = note_features(w[s:s + int((STEP - NOTE_AT) * RATE)].mean(1))
    w = read_wav(os.path.join(out, f'osirus2-{theirs_letter}{p:03d}.wav'))
    s = int((NOTE_AT + SHIFT - 0.05) * RATE)
    fc = note_features(w[s:s + int((STEP - NOTE_AT - SHIFT) * RATE)].mean(1)) if len(w) > s + RATE else None

    def diff(x, y):
        lit = (x['bands'] > x['bands'].max() - 40) | (y['bands'] > y['bands'].max() - 40)
        return {'level_db': x['rms_db'] - y['rms_db'], 'band_max_db': float(np.max(np.abs(x['bands'] - y['bands'])[lit])),
                'band_mean_db': float(np.mean(np.abs(x['bands'] - y['bands'])[lit])), 'cents': 1200 * np.log2(x['f0'] / y['f0'])}
    if fo and fg:
        r.update(diff(fo, fg), onset_ms=(fo['onset_ms'], fg['onset_ms']), f0=(fo['f0'], fg['f0']))
        if fc:
            r['control'] = diff(fc, fg)
    else:
        r.update(silent=(fo is None, fg is None))
    return r


def compare(a):
    rows = []
    with ProcessPoolExecutor(a.jobs) as pool:
        rows = list(pool.map(compare_program, [(a.out, letter, p) for letter in a.banks for p in range(a.programs)]))
    import json
    with open(os.path.join(a.out, 'compare.json'), 'w') as f:
        json.dump(rows, f, indent=1, default=float)
    n = len(rows)
    same_upload = sum(r['n_other'] == 0 for r in rows)
    same_notes = sum(r['note_words_same'] for r in rows)
    print(f'{n} programs: upload identical (but page A bytes 1-2 and the last word\'s stale low bytes) {same_upload}; '
          f'note bytes identical {same_notes}')
    played = [r for r in rows if 'level_db' in r]
    if played:
        lv = np.array([r['level_db'] for r in played]); bm = np.array([r['band_mean_db'] for r in played])
        ct = np.array([r['cents'] for r in played])
        print(f'{len(played)} sounded in both: level ours-osirus median {np.median(lv):+.2f} dB, |level| > 1 dB: '
              f'{np.sum(np.abs(lv - np.median(lv)) > 1)}; band mean |diff| median {np.median(bm):.2f} dB, > 3 dB: {np.sum(bm > 3)}; '
              f'|cents| > 5: {np.sum(np.abs(ct) > 5)}')
        ctl = [r for r in played if 'control' in r]
        cl = np.array([r['control']['level_db'] for r in ctl]); cb = np.array([r['control']['band_mean_db'] for r in ctl])
        cc = np.array([r['control']['cents'] for r in ctl])
        print(f'  control, Osirus against itself with the note {SHIFT} s later: |level| > 1 dB: {np.sum(np.abs(cl) > 1)}; '
              f'band mean median {np.median(cb):.2f} dB, > 3 dB: {np.sum(cb > 3)}; |cents| > 5: {np.sum(np.abs(cc) > 5)}')
        # the largest beyond what moving the note in time does to Osirus itself: to listen to, not a verdict
        # (a program that depends on time since boot can differ by 20 dB and more)
        out = sorted(ctl, key=lambda r: -(r['band_mean_db'] - r['control']['band_mean_db']))[:10]
        print('  most beyond the control (band mean, ours / control):',
              ', '.join(f"{r['program']} {r['band_mean_db']:.1f}/{r['control']['band_mean_db']:.1f}" for r in out))
        # programs that moving the note in time hardly changes on Osirus: ours should match those closely
        st = [r for r in ctl if abs(r['control']['level_db']) < 0.1 and abs(r['control']['cents']) < 0.5
              and r['control']['band_mean_db'] < 0.3]
        if st:
            sl = np.array([r['level_db'] for r in st]); sc = np.array([r['cents'] for r in st])
            sb = np.array([r['band_mean_db'] for r in st])
            print(f'  {len(st)} steady programs: |level| max {np.max(np.abs(sl)):.2f} dB, |cents| max {np.max(np.abs(sc)):.2f}, '
                  f'band mean max {np.max(sb):.2f} dB')
        ons = np.array([r['onset_ms'][0] - r['onset_ms'][1] for r in played])
        print(f'  onset, ours later by: median {np.median(ons):.2f} ms, 5-95 % {np.percentile(ons, 5):.2f} to {np.percentile(ons, 95):.2f} ms')
    others = [r for r in rows if r['n_other']]
    print(f'{len(others)} with other upload differences:', ', '.join(f"{r['program']} {r['upload_other_diffs'][:2]}" for r in others[:10]))
    lb = {}
    for r in rows:
        for x, y in r['live_bytes']:
            lb[(x[2:], y[2:])] = lb.get((x[2:], y[2:]), 0) + 1
    print('page A bytes 1-2, ours / osirus:', sorted(lb.items(), key=lambda kv: -kv[1])[:8])
    silent = [r for r in rows if 'silent' in r]
    if silent:
        print(f'{len(silent)} silent in one or both:', ', '.join(f"{r['program']}{r['silent']}" for r in silent[:20]))


CONTROLLERS = ['note 60 100', 'midi 176 1 64', 'midi 176 7 100', 'midi 176 40 30', 'midi 176 74 20', 'midi 224 0 80',
               'midi 208 70 -1', 'midi 160 5 3', 'midi 176 64 127', 'note 60 0', 'midi 176 64 0', 'midi 176 10 20',
               'midi 176 11 90', 'midi 176 2 50', 'midi 176 120 0', 'midi 176 121 0', 'midi 176 123 0']


def single_dump(rom_path, bank, program):
    """A single dump of a flash program as the Virus sends it (MIDI DUMP TX): to the edit buffer."""
    rom = open(rom_path, 'rb').read()
    data = rom[0x50000 + bank * 0x8000 + program * 256:][:256]
    body = [0x10, 0x10, 0x00, 0x7f] + list(data)                # device 10 (omni), single dump, bank 0 (edit buffer), 7F
    check = sum(body) & 0x7f                                    # the manual's: device ID + 10 + bank + program + data
    return 'sysex ' + ' '.join(f'{b:02X}' for b in [0xf0, 0x00, 0x20, 0x33, 0x01] + body + [check, 0xf7])


def sysex_events(rom):
    page = lambda pg, part, prm, val: f'sysex F0 00 20 33 01 10 {pg:02X} {part:02X} {prm:02X} {val:02X} F7'
    return [page(0x70, 0x40, 40, 30),                      # page A, the single: FILTER 1 CUTOFF
            page(0x70, 0x40, 17, 90),                      # OSC 1 SHAPE
            page(0x71, 0x40, 1, 1),                        # page B: ARP MODE up
            page(0x71, 0x40, 1, 0),
            page(0x72, 0x00, 0x5c, 0x48),                  # page C: MASTER TUNE
            'note 60 100', 'note 60 0',
            single_dump(rom, 2, 9),                        # E9 into the edit buffer
            'note 62 100', 'note 62 0',
            page(0x72, 0x00, 0x7a, 1),                     # PLAY MODE: multi
            'midi 144 64 100', 'midi 145 67 100', 'midi 128 64 0', 'midi 129 67 0']


def controllers(a, events=None, name='controllers', step=0.1):
    events = events or CONTROLLERS
    os.makedirs(a.out, exist_ok=True)
    ours, theirs = os.path.join(a.out, f'{name}-ours.script'), os.path.join(a.out, f'{name}-osirus.script')
    with open(ours, 'w') as f:
        f.write(f'{BOOT} midi 176 32 2\n{BOOT} midi 192 126 -1\n' + ''.join(f'{BOOT + 0.5 + step * i:.2f} {e}\n' for i, e in enumerate(events)))
    with open(theirs, 'w') as f:
        f.write(''.join(f'{0.5 + step * i:.2f} {e}\n' for i, e in enumerate(events)))
    seconds = BOOT + 0.5 + step * len(events) + 0.5
    r = subprocess.run([a.viruscrun, a.rom, str(seconds)], env=dict(os.environ, VC_SCRIPT=ours, VC_WORDS=f'{BOOT + 0.4},{seconds}'),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)
    mine = [l.split()[6] for l in r.stdout.splitlines() if l.startswith('WORD')]
    r = subprocess.run([a.osirus, '126'], env=dict(os.environ, VIRUS_ROM=a.rom, VIRUS_OUT=os.path.join(a.out, f'{name}-osirus.wav'),
                       VIRUS_SAMPLES=str(int((seconds - BOOT) * RATE)), VIRUS_SCRIPT=theirs, VIRUS_NOTES='0', VIRUS_WORDLOG='1'),
                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    words = [l.split()[1] for l in r.stderr.splitlines() if l.startswith('RXWORD')]
    start = len(words) - 1 - words[::-1].index('f47555')
    theirs_w = words[start + len(upload(words)) - len(words[start:]) + 88:]   # after the 88-word upload
    if name != 'controllers':
        return mine, theirs_w
    top = lambda ws: [w[:2] for w in ws]   # one MIDI byte a word, in the top byte
    print(f'{len(CONTROLLERS)} messages: {len(mine)} words here, {len(theirs_w)} on Osirus; '
          f'{"identical" if top(mine) == top(theirs_w) else "DIFFERENT"}')
    if top(mine) != top(theirs_w):
        print('  here  :', ' '.join(top(mine)))
        print('  osirus:', ' '.join(top(theirs_w)))


def sysex(a):
    """Parameter changes by sysex on pages A, B and C, a single dump into the edit buffer, multi mode: the DSP's
    words from both, printed side by side (the firmware frames some of them differently, so they are read, not
    matched)."""
    mine, theirs = controllers(a, sysex_events(a.rom), 'sysex', step=0.3)
    with open(os.path.join(a.out, 'sysex-words.txt'), 'w') as f:
        f.write('here:\n' + ' '.join(mine) + '\n\nosirus:\n' + ' '.join(theirs) + '\n')
    print(f'{len(mine)} words here, {len(theirs)} on Osirus: {os.path.join(a.out, "sysex-words.txt")}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', choices=['run', 'compare', 'controllers', 'sysex'])
    ap.add_argument('out')
    ap.add_argument('--viruscrun')
    ap.add_argument('--osirus')
    ap.add_argument('--rom')
    ap.add_argument('--banks', default='CDEFGH')
    ap.add_argument('--programs', type=int, default=128)
    ap.add_argument('--note', type=int, default=60)
    ap.add_argument('--jobs', type=int, default=os.cpu_count())
    ap.add_argument('--only', choices=['ours', 'osirus'], help='run only one side (the other already in OUT)')
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)
    {'run': run, 'compare': compare, 'controllers': controllers, 'sysex': sysex}[a.mode](a)


if __name__ == '__main__':
    main()
