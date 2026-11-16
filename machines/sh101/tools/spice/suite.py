#!/usr/bin/env python3
"""Every SH-101 netlist against ngspice, in one run: the oscillator section (osc/compare.py), the
filter section's blocks (filter/compare_cv, compare_env, compare_vca), the IR3109 at transistor
level (filter/ir3109/compare_ir3109.py), jack board A (jack/compare.py) and the whole voice
(voice/compare_voice.py, 43 cases). Each script's output is kept, its headline numbers read, and
each set against a limit: the figure docs/model.md ("Validation") gives, with a little room, so a
change that moves the model away from the schematic shows up.

    ../../.venv/bin/python tools/spice/suite.py [--out DIR] [--only osc,cv,env,vca,ir3109,jack,voice]

Run from the machine folder after building the block runners into build/sh101 (or $SH101_BUILD):
    cmake --build build/sh101 --target sh101oscblocks sh101filterblocks sh101voiceblocks sh101jackblocks
It takes about four minutes on ten cores (the whole voice uses them all). Outputs go to DIR
(default build/sh101/spice), one <set>.txt each, and summary.txt.

filter/compare_vcf.py isn't in the suite: it runs the model against the behavioural IR3109 of
models.lib, the chip as decoded before the buffers were modelled, and now reads 120-130 cents
under it by design (docs/model-filter.md 2). The transistor-level chip is the reference.
"""
import argparse
import concurrent.futures as cf
import math
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(MACHINE, "..", ".."))
PY = sys.executable

SETS = {
    "osc": "tools/spice/osc/compare.py",
    "cv": "tools/spice/filter/compare_cv.py",
    "env": "tools/spice/filter/compare_env.py",
    "vca": "tools/spice/filter/compare_vca.py",
    "ir3109": "tools/spice/filter/ir3109/compare_ir3109.py",
    "jack": "tools/spice/jack/compare.py",
    "voice": "tools/spice/voice/compare_voice.py",
}

NUM = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?"


def floats(s):
    return [float(x) for x in re.findall(NUM, s)]


def section(text, start, stop=None):
    """The lines from the one starting with `start` up to the next one starting with `stop` (or a
    line without leading space, by default)."""
    lines = text.splitlines()
    out, on = [], False
    for ln in lines:
        if not on:
            if ln.strip().startswith(start):
                on = True
            continue
        if (stop is not None and ln.strip().startswith(stop)) or (stop is None and ln and not ln[0].isspace()):
            break
        out.append(ln)
    return out


# Each parser returns [(what, value, limit, unit)]: a check passes when |value| <= limit.

def parse_osc(t):
    r = []
    rows = [ln for ln in section(t, "MODULATOR", "CLK RST") if "|" in ln and re.match(r"\s*\d", ln)]
    r.append(("LFO frequency, RATE 0-10", max(abs(float(ln.split("|")[-1])) for ln in rows), 1.5, "%"))
    rows = [ln for ln in section(t, "Portamento:", "AUTO") if "|" in ln and re.match(r"\s*\d", ln)]
    r.append(("portamento time constant", max(abs(floats(ln.split("|")[-1])[-1]) for ln in rows), 0.05, "%"))
    m = re.search(r"model: amplitude .*?: (" + NUM + r")", t)
    s = re.search(r"SPICE: amplitude .*?: (" + NUM + r")", t)
    r.append(("grip LFO amplitude", 100 * (float(m.group(1)) / float(s.group(1)) - 1), 1.5, "%"))
    pw = [float(x) for x in re.findall(r"max \|diff\| (" + NUM + r") mV", t)]
    r.append(("pulse width at pin 5", max(pw), 4, "mV"))
    lv = []
    for ln in section(t, "Pulse and sub levels", "Noise amplifier"):
        if "pulse low" in ln or "pulse high" in ln:
            a, b = floats(ln.split(":")[1])[:2]
            lv.append(abs(a - b))
        elif ln.strip().startswith("sub"):
            for a, b in re.findall(r"(" + NUM + r")/\s*(" + NUM + r")", ln):
                lv.append(abs(float(a) - float(b)))
    r.append(("pulse and sub currents into the mixer", max(lv), 0.01, "uA"))
    dc = []
    for ln in section(t, "IC3a / IC12b"):
        parts = ln.split("|")
        if len(parts) == 3 and re.match(r"\s*\d", parts[0]):
            a, b = floats(parts[1]), floats(parts[2])
            dc += [1000 * abs(a[0] - b[0]), 1000 * abs(a[1] - b[1])]
    r.append(("pitch summer, DC (IC3a, IC12b)", max(dc), 3, "mV"))
    return r


def parse_cv(t):
    # Cases whose pin 9 sits beyond the expo's 2 mA ceiling (more than 200 mV under the bus) don't
    # set a cutoff; the rest should keep the constant offset of a few cents.
    within, beyond = [], []
    for ln in t.splitlines():
        m = re.search(r"model\s+(" + NUM + r")\s+SPICE\s+(" + NUM + r")\s+(" + NUM + r") cents", ln)
        if m:
            (within if abs(float(m.group(2))) < 200 else beyond).append(abs(float(m.group(3))))
    return [("cutoff CV at pin 9 (in range)", max(within), 8, "cents")]


def parse_env(t):
    r = []
    rms, times = [], []
    for ln in t.splitlines():
        m = re.match(r"\s+(\w+)\s+ENV err rms\s+(" + NUM + r") mV", ln)
        if not m:
            continue
        name = m.group(1)
        rms.append(float(m.group(2)))
        pk = re.search(r"peak at\s+(" + NUM + r") /\s+(" + NUM + r") s", ln)
        # trig and lfo retrigger: their peaks are several nearly equal maxima, so the time of the
        # largest isn't a measure.
        if pk and name not in ("trig", "lfo"):
            a, b = float(pk.group(1)), float(pk.group(2))
            times.append(100 * abs(a / b - 1))
        rl = re.search(r"release to 10%\s+(" + NUM + r") /\s+(" + NUM + r") s", ln)
        if rl:
            times.append(100 * abs(float(rl.group(1)) / float(rl.group(2)) - 1))
    r.append(("ENV, 7 scenarios: error RMS", max(rms), 20, "mV"))
    r.append(("ENV peak and release times", max(times), 0.3, "%"))
    m = re.search(r"steady (" + NUM + r") / (" + NUM + r") uA", t)
    r.append(("GATE-mode VCA current", 20 * math.log10(float(m.group(2)) / float(m.group(1))), 0.05, "dB"))
    return r


def parse_vca(t):
    r = []
    m = re.search(r"worst above 1 uA: (" + NUM + r") dB", t)
    r.append(("VCA control current (above 1 uA)", float(m.group(1)), 0.12, "dB"))
    g = [abs(float(x)) for x in re.findall(r"model\s+" + NUM + r"\s+(" + NUM + r") dB", "\n".join(section(t, "Small-signal gain")))]
    r.append(("VCA small-signal gain", max(g), 0.12, "dB"))
    o = [abs(float(x)) for x in re.findall(r"model\s+" + NUM + r"\s+(" + NUM + r") dB", "\n".join(section(t, "Pin 8 -> OUTPUT")))]
    r.append(("pin 8 to the OUTPUT jack, 2 Hz-20 kHz", max(o), 0.03, "dB"))
    return r


def parse_ir3109(t):
    r = []
    ac = section(t, "AC:", "worst")
    at10 = [float(re.search(r"max \|err\| (" + NUM + r")", ln).group(1)) for ln in ac if "10 uA" in ln and "max |err|" in ln]
    r.append(("VCF AC, I_ABC 10 uA", max(at10), 0.2, "dB"))
    allac = float(re.search(r"worst (" + NUM + r") dB", t).group(1))
    r.append(("VCF AC, 1-100 uA", allac, 0.9, "dB"))
    # Self-oscillation: +0.3 cents at 194 Hz, +5 at 970 Hz, +22 at 4.8 kHz (2026-11-14, with the
    # decode's mirror gain, CAP pin capacitance and buffers in the model). What grows with frequency
    # is mostly the decode's input pair losing transconductance with current, which the model
    # leaves out because a real chip's law (a Juno-106's) doesn't show it, and about 4 cents of the
    # discrete resonance parts' capacitance (improvements.md, "IR3109"); the limit holds that gap.
    osc = [ln for ln in section(t, "Self-oscillation:", "Self-oscillation onset") if "cents" in ln]
    r.append(("self-oscillation pitch", max(abs(float(re.search(r"(" + NUM + r") cents", ln).group(1))) for ln in osc), 25, "cents"))
    r.append(("self-oscillation level", max(abs(float(re.search(r"cents (" + NUM + r") dB", ln).group(1))) for ln in osc), 0.7, "dB"))
    s = float(re.search(r"SPICE onset at (" + NUM + r")", t).group(1))
    m = float(re.search(r"model onset at (" + NUM + r")", t).group(1))
    r.append(("self-oscillation onset (VR18 travel)", m - s, 0.01, ""))
    we = [float(x) for x in re.findall(r"waveform error (" + NUM + r") %", t)]
    r.append(("saw through the filter: waveform error", max(we), 2.0, "%"))
    h = []
    for ln in section(t, "Saw 0-10 V at 110 Hz through 470k: OUT4 harmonics"):
        if "diff" in ln:
            h += [abs(x) for x in floats(ln.split("diff")[1])]
    r.append(("saw harmonics 1-8", max(h), 0.34, "dB"))
    return r


def parse_jack(t):
    r = []
    on = section(t, "Switched on with", "Switched on long")
    r.append(("power-on mute (TR7, C13)", float(re.search(r"largest difference (" + NUM + r") mV", "\n".join(on)).group(1)), 4, "mV"))
    cl = section(t, "Switched on long before", "Switched off")
    r.append(("clamp (TR7's junction)", float(re.search(r"largest difference (" + NUM + r") dB", "\n".join(cl)).group(1)), 0.05, "dB"))
    off = []
    for ln in section(t, "Switched off", None):
        m = re.match(r"\s+(" + NUM + r")-\s*(" + NUM + r") ms.*largest difference\s+(" + NUM + r") mV", ln)
        if m and float(m.group(1)) >= 2:
            off.append(float(m.group(3)))
    r.append(("power-off mute after 2 ms", max(off), 1.0, "mV"))
    return r


def parse_voice(t):
    r = []
    m = re.search(r"osc\s+\d+ cases: level within (" + NUM + r") dB.*?harmonics " + NUM + r" dB mean / (" + NUM + r") max", t)
    r.append(("whole voice, sources (filter open): level", float(m.group(1)), 0.6, "dB"))
    r.append(("whole voice, sources: harmonics 1-12", float(m.group(2)), 1.3, "dB"))
    m = re.search(r"vcf\s+\d+ cases: .*?I_ABC within (" + NUM + r") cents", t)
    # 4 and 9 cents since 2026-11-14 (the model's I_ABC read from its own probe, the decode's mirror
    # gain, CAP pins and buffers in the model).
    r.append(("whole voice, VCF: I_ABC", float(m.group(1)), 8, "cents"))
    m = re.search(r"vcf\s+\d+ self-oscillating: pitch (" + NUM + r") to (" + NUM + r") cents", t)
    r.append(("whole voice, self-oscillation pitch", max(abs(float(m.group(1))), abs(float(m.group(2)))), 15, "cents"))
    m = re.search(r"env\s+\d+ cases: level within (" + NUM + r") dB", t)
    r.append(("whole voice, ENV to the VCA: level", float(m.group(1)), 0.3, "dB"))
    return r


PARSERS = dict(osc=parse_osc, cv=parse_cv, env=parse_env, vca=parse_vca, ir3109=parse_ir3109, jack=parse_jack, voice=parse_voice)


def run(name, out, env):
    t0 = time.time()
    p = subprocess.run([PY, SETS[name]], cwd=MACHINE, capture_output=True, text=True, env=env)
    text = p.stdout + p.stderr
    with open(os.path.join(out, name + ".txt"), "w") as f:
        f.write(text)
    try:
        rows = PARSERS[name](text) if p.returncode == 0 else None
    except Exception as e:  # noqa: BLE001 - a script whose output changed shape fails the suite
        rows, text = None, text + "\nsuite: couldn't read the output: %r" % e
    return name, rows, time.time() - t0, p.returncode, text[-1500:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None)
    ap.add_argument("--only", default=",".join(SETS))
    a = ap.parse_args()
    build = os.environ.get("SH101_BUILD", "build/sh101")
    out = os.path.abspath(a.out or os.path.join(build if os.path.isabs(build) else os.path.join(REPO, build), "spice"))
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, SH101_BUILD=build)
    names = [n for n in a.only.split(",") if n]
    for n in names:
        if n not in SETS:
            sys.exit("unknown set %s (%s)" % (n, ", ".join(SETS)))
    # The whole voice runs its 43 cases on every core; the small sets share two more workers.
    results = {}
    with cf.ThreadPoolExecutor(3) as ex:
        futs = [ex.submit(run, n, out, env) for n in sorted(names, key=lambda n: n != "voice")]
        for f in cf.as_completed(futs):
            n, rows, secs, code, tail = f.result()
            results[n] = (rows, secs, code, tail)
            print("  %-7s %5.0f s  %s" % (n, secs, "read" if rows is not None else "FAILED"), flush=True)
    lines, fails = [], 0
    for n in names:
        rows, secs, code, tail = results[n]
        if rows is None:
            fails += 1
            lines.append("%-7s FAILED (exit %d); see %s.txt\n%s" % (n, code, n, tail))
            continue
        for what, v, lim, unit in rows:
            ok = abs(v) <= lim
            fails += not ok
            lines.append("%-7s %-46s %9.3f %-5s limit %7.3f  %s" % (n, what, v, unit, lim, "ok" if ok else "OVER"))
    lines.append("")
    lines.append("SPICE suite: %d of %d checks within their limits" % (sum(len(results[n][0] or [None]) for n in names) - fails,
                                                                       sum(len(results[n][0] or [None]) for n in names)))
    text = "\n".join(lines)
    print("\n" + text)
    with open(os.path.join(out, "summary.txt"), "w") as f:
        f.write(text + "\n")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
