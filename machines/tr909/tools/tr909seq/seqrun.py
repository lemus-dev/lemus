"""Runs tr909seq with a script and parses its log (used by tests.py and tools/compare/).

Script macros (tr909seq's own commands are passed through; ';' separates commands):
    A+B       hold A, 50 ms later press B for 0.1 s, release both, run 0.1 s
    A+B+C     hold A and B, press C
    p K K ... press keys one after another (0.1 s each, 0.1 s apart)
    pair A B  (with a modifier held) press A, 30 ms later B, release both: the two keys of a voice;
              two keys of steps 9-16 pressed inside one 2 ms scan register as one key
"""
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../../../.."))
ROMS = os.path.normpath(os.path.join(ROOT, "../private/roms/tr909"))
V1 = os.path.join(ROMS, "tr909_v1.ic609")
V4 = os.path.join(ROMS, "tr909_v4.ic609")
# tr909seq: $TR909SEQ, else the first build directory that has one
SEQ = os.environ.get("TR909SEQ") or next(
    (p for p in (os.path.join(ROOT, "build", d, "tr909seq") for d in ("tr909", "tr909-fw")) if os.path.exists(p)),
    os.path.join(ROOT, "build/tr909/tr909seq"))
VERBOSE = False


def expand(script):
    out = []
    for c in [c.strip() for c in script.split(";")]:
        if not c:
            continue
        w = c.split()
        if len(w) == 1 and "+" in w[0] and not w[0].startswith("+"):
            ks = w[0].split("+")
            out += ["down " + " ".join(ks[:-1]), "run 0.05", "press " + ks[-1], "up " + " ".join(ks[:-1]), "run 0.1"]
        elif w[0] == "p":
            out += ["press " + k for k in w[1:]]
        elif w[0] == "pair":
            out += [f"down {w[1]}", "run 0.03", f"down {w[2]}", "run 0.1", f"up {w[1]} {w[2]}", "run 0.05"]
        else:
            out.append(c)
    return out


class Run:
    """A tr909seq run, parsed. Lines are (ms, kind, rest) in order."""

    def __init__(self, script, rom=None, ram=None, save_ram=None):
        args = [SEQ] + (["--rom", rom] if rom else []) + (["--ram", ram] if ram else []) + \
            (["--save-ram", save_ram] if save_ram else []) + expand(script)
        p = subprocess.run(args, capture_output=True, text=True)
        self.text = p.stdout
        if VERBOSE:
            print(self.text)
        self.lines = []
        for l in self.text.splitlines():
            m = re.match(r"\s*([\d.]+) ms  (\S+)\s*(.*)", l)
            if m:
                self.lines.append((float(m.group(1)), m.group(2), m.group(3)))
            else:
                self.lines.append((None, "TEXT", l))

    def section(self, start=None, end=None):
        """Lines between ECHO markers (exclusive)."""
        r, on = [], start is None
        for t, k, s in self.lines:
            if k == "ECHO":
                if s.strip() == start:
                    on = True
                    continue
                if s.strip() == end:
                    on = False
            if on:
                r.append((t, k, s))
        return r

    def hits(self, inst=None, start=None, end=None):
        out = []
        for t, k, s in self.section(start, end):
            if k == "HIT":
                n, c = s.split()
                if inst is None or n == inst or (isinstance(inst, (set, tuple, list)) and n in inst):
                    out.append((t, n, int(c)))
        return out

    def widths(self, start=None, end=None):
        return [float(s.split()[1]) for t, k, s in self.section(start, end) if k == "END"]

    def midi(self, start=None, end=None):
        return [(t, int(s.split()[1], 16)) for t, k, s in self.section(start, end) if k == "MIDI"]

    def duty(self):
        """'ledduty' reports: list of (step on-times 0-9 as a string, {led: 0-9})."""
        out = []
        for t, k, s in self.lines:
            if k == "DUTY":
                w = s.split()
                out.append((w[1], {x.split(":")[0]: int(x.split(":")[1]) for x in w[3:]}))
        return out

    def leds(self, start=None, end=None):
        out = []
        for t, k, s in self.section(start, end):
            if k == "LEDS":
                m = re.search(r"display \[(...)\]  steps (\S+)  lit ?(.*)", s)
                out.append({"t": t, "display": m.group(1), "steps": m.group(2), "lit": set(m.group(3).split())})
        return out

    def ram(self, start=None, end=None):
        """RAM changes from 'diff' lines: list of (addr, old, new)."""
        out = []
        for t, k, s in self.section(start, end):
            if k == "TEXT":
                m = re.match(r"RAM ([0-9A-F]{4}): ([0-9A-F]{2}) -> ([0-9A-F]{2})", s)
                if m:
                    out.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16)))
        return out

    def dump(self):
        """Memory from 'ram' dumps: {addr: byte}."""
        mem = {}
        for t, k, s in self.lines:
            if k == "TEXT":
                m = re.match(r"([0-9A-F]{4}):((?: [0-9A-F]{2})+)$", s)
                if m:
                    a = int(m.group(1), 16)
                    for i, b in enumerate(m.group(2).split()):
                        mem[a + i] = int(b, 16)
        return mem


def intervals(ts):
    return [b - a for a, b in zip(ts, ts[1:])]


