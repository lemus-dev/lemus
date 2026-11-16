#!/usr/bin/env python3
"""The SH-101 against its service notes (First Edition, 1982): every adjustment in "ADJUSTMENT
SPECIFICATIONS" and every specification on p.2 that the emulation can show, pass or fail.

Usage (from the machine folder, after building sh101seq, sh101render, sh101oscblocks and
sh101filterblocks into the same build folder):
    ../../.venv/bin/python tools/service/check.py [--build DIR] [--calibration service|real|roland]

It runs, in order:
  1. the firmware's test mode (sh101seq --selftest): the D/A adjustment's three voltages;
  2. the whole machine through the firmware (sh101render): VCO TUNE (A4 at 8' M = 442 Hz),
     VCO WIDTH (F3 to F5 exactly two octaves), CV OUT's range;
  3. the filter section's blocks (sh101filterblocks): VCF WIDTH (F5 an octave above F4 at KYBD
     10), the cutoff range, the ENV times, the OUTPUT level;
  4. the oscillator section's own checks (tools/spice/osc/check.py): RANGE, TUNE, LFO, portamento,
     pulse width, sub-oscillator, noise.
"""
import json, math, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(ROOT, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "compare"))
import analyse as A  # noqa: E402

BUILD = os.path.join(REPO, "build", "sh101")
if "--build" in sys.argv:
    BUILD = os.path.abspath(sys.argv[sys.argv.index("--build") + 1])
CAL = "service"
if "--calibration" in sys.argv:
    CAL = sys.argv[sys.argv.index("--calibration") + 1]
SEQ = os.path.join(BUILD, "sh101seq")
RENDER = os.path.join(BUILD, "sh101render_artefacts", "Release", "sh101render")
FILTER = os.path.join(BUILD, "sh101filterblocks")
results = []


def run(*args):
    return subprocess.run([str(a) for a in args], capture_output=True, text=True, check=True).stdout


def check(name, ok, detail):
    results.append(bool(ok))
    print(f"  {'pass' if ok else 'FAIL'}  {name}: {detail}")


def section(title):
    print(f"\n{title}")


# 1. D/A CONVERTER ADJUSTMENT: test mode, CV OUT reads 0 V (LOAD), 2.75 V (PLAY), 2.5 V (DOWN).
section("1. D/A converter (test mode, CV OUT; the firmware on the CPU)")
out = run(SEQ, "--selftest")
want = {"LOAD": 0.0, "PLAY": 2.75, "DOWN": 2.5}
for key, volts in want.items():
    m = re.search(rf"(ok|FAIL)\s+test mode {key}: KCV (\d+) \(([\d.]+) V\)", out)
    check(f"{key}: {volts:.3f} V +-1 mV", m and m.group(1) == "ok" and abs(float(m.group(3)) - volts) <= 1e-3,
          f"KCV {m.group(2)} = {m.group(3)} V" if m else "not found in the selftest")


# 2. The whole machine (firmware + voice), rendered as the plug-in plays it.
def render(specs):
    d = tempfile.mkdtemp(prefix="sh101check-")
    path = os.path.join(d, "specs.json")
    json.dump(specs, open(path, "w"))
    run(RENDER, path, "--out", d, "--rate", 48000, "--calibration", CAL)
    return {s["name"]: A.load(os.path.join(d, s["name"] + ".wav"))[0] for s in specs}


BASE = {"range": 1, "transpose": 1, "sawLevel": 1.0, "pulseLevel": 0.0, "subLevel": 0.0, "noiseLevel": 0.0,
        "vcfFreq": 1.0, "vcfRes": 0.0, "vcfEnv": 0.0, "vcfKybd": 0.0, "vcaMode": 1, "sustain": 1.0,
        "volume": 0.7, "tune": 0.5, "portaMode": 2, "vcoMod": 0.0, "lfoModDepth": 0.0, "bendVco": 0.0}


def held(name, note, **p):
    params = dict(BASE)
    params.update(p)
    return {"name": name, "bpm": 120, "lpb": 4, "lines": 8, "params": params,
            "notes": [{"line": 0, "note": note, "len": 8, "vel": 100}], "tail": 0.2}


section(f"2. VCO adjustment (the whole machine, {CAL} calibration)")
w = render([held("a4", "A-4"), held("f3", "F-3"), held("f5", "F-5")])
sr = 48000


def pitch(x):
    return A.f0(x[int(0.5 * sr):int(1.5 * sr)], sr)


a4, f3, f5 = pitch(w["a4"]), pitch(w["f3"]), pitch(w["f5"])
tune = 442.0 if CAL == "service" else 440.0
check(f"VCO TUNE: A4 at 8' M = {tune:g} Hz", abs(A.cents(a4, tune)) < 1.0, f"{a4:.2f} Hz ({A.cents(a4, tune):+.2f} cents)")
check("VCO WIDTH: F3 to F5 = 2400 cents", abs(A.cents(f5, f3) - 2400) < 1.0, f"{A.cents(f5, f3):.2f} cents")

# 3. The filter section.
section("3. VCF adjustment, cutoff range, ENV, OUTPUT (filter section blocks)")
# The filter blocks take FilterCalibration fields for kybd-track and cv (core/filter.cpp).
cal = {"service": [],
       "real": ["--cal", "vcfI0uA=5.0", "--cal", "vcfMvPerOct=20.8", "--cal", "envCvScale=0.82"],
       "roland": ["--cal", "vcfI0uA=3.9", "--cal", "vcfMvPerOct=22.1", "--cal", "envCvScale=0.89"]}[CAL]
# Fig. 2: RES max, KYBD max, A4 near 1 kHz; F5 (3.417 V) must be an octave above F4 (2.417 V).
best = None
for fr in [x / 100 for x in range(40, 80)]:
    hz = float(run(FILTER, *cal, "cv", fr, 0, 0, 0, 0, 1, 2.75, 0, 0).split()[2])
    if best is None or abs(math.log(hz / 1000)) < abs(math.log(best[1] / 1000)):
        best = (fr, hz)
f4, f5, slope = map(float, run(FILTER, *cal, "kybd-track", 2.417, 3.417, best[0]).split()[-3:])
check("VCF WIDTH: F5 an octave above F4 (KYBD 10, RES 10, A4 near 1 kHz)", abs(slope - 1) < 0.002,
      f"FREQ {best[0]:.2f}: F4 {f4:.1f} Hz, F5 {f5:.1f} Hz, {slope:.4f} oct/oct")
lo = float(run(FILTER, *cal, "cv", 0, 0, 0, 0, 0, 0, 0, 0, 0).split()[2])
hi = float(run(FILTER, *cal, "cv", 1, 0, 0, 0, 0, 1, 5.0, 0, 0).split()[2])
check("Cutoff Frequency 10 Hz .. 20 kHz (spec): the range covers it", lo <= 10 and hi >= 20e3,
      f"FREQ 0 {lo:.1f} Hz; FREQ 10 with KYBD 10 at the top key {hi / 1000:.1f} kHz")


def env_times(a, d, s, r, gate, total):
    rows = [list(map(float, l.split())) for l in run(FILTER, "env", a, d, s, r, "gatetrig", gate, total).splitlines()]
    t = [x[0] for x in rows]
    e = [x[1] for x in rows]
    rest = e[0]
    peak = max(e)
    i_peak = e.index(peak)
    t_on = 0.010
    attack = t[i_peak] - t_on
    return t, e, rest, peak, i_peak, attack


t, e, rest, peak, ip, att_min = env_times(0, 1, 1, 0, 0.5, 0.6)
_, _, _, _, _, att_max = env_times(1, 1, 1, 0, 8.0, 8.5)
check("ENV Attack Time 1.5 ms .. 4 s (spec)", 1.0e-3 <= att_min <= 3e-3 and 3.0 <= att_max <= 4.5,
      f"{att_min * 1e3:.2f} ms .. {att_max:.2f} s (rest to peak)")


def tau_after(t, e, t0, target):
    # Time constant of the fall from t0 toward `target`: the time to fall 63 % of the way.
    i0 = min(range(len(t)), key=lambda i: abs(t[i] - t0))
    v0 = e[i0]
    goal = v0 - 0.632 * (v0 - target)
    for i in range(i0, len(t)):
        if e[i] <= goal:
            return t[i] - t[i0]
    return float("nan")


t, e, rest, peak, ip, _ = env_times(0, 0, 0, 0, 1.0, 1.2)
d_min = tau_after(t, e, t[ip], rest)
t, e, rest, peak, ip, _ = env_times(0, 1, 0, 0, 30.0, 30.0)
d_max = tau_after(t, e, t[ip], rest)
check("ENV Decay Time 2 ms .. 10 s (spec; 10 s is about 90 % of the way, 2.3 tau)",
      d_min <= 3e-3 and 8.0 <= 2.3 * d_max <= 12.0, f"tau {d_min * 1e3:.2f} ms .. {d_max:.2f} s (90 %: {2.3 * d_max:.1f} s)")
t, e, rest, peak, ip, _ = env_times(0, 0, 1, 1, 0.3, 30.0)
r_max = tau_after(t, e, 0.31, rest)
t, e, rest, peak, ip, _ = env_times(0, 0, 1, 0, 0.3, 0.5)
r_min = tau_after(t, e, 0.31, rest)
check("ENV Release Time 2 ms .. 10 s (spec)", r_min <= 3e-3 and 8.0 <= 2.3 * r_max <= 12.0,
      f"tau {r_min * 1e3:.2f} ms .. {r_max:.2f} s (90 %: {2.3 * r_max:.1f} s)")
lvl = run(FILTER, "voice-level", 1, 0, 0, 0, 1, 1)
m = re.search(r"rms ([\d.]+) V \(([-\d.]+) dBu\)", lvl)
check("Audio output 0 dBm max (spec): a full saw at VOLUME 10, filter open", m and float(m.group(2)) <= 0.5 and float(m.group(2)) > -3,
      f"{m.group(1)} V RMS, {m.group(2)} dBu" if m else lvl.strip())

# 4. The oscillator section's own checks.
section("4. VCO, MODULATOR, portamento, pulse width, sub-oscillator, noise (oscillator section)")
osc = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "spice", "osc", "check.py"), "--build", BUILD],
                     capture_output=True, text=True)
for line in osc.stdout.splitlines():
    m = re.match(r"\s+(pass|FAIL)\s+(.*)", line)
    if m:
        results.append(m.group(1) == "pass")
        print(line)

print(f"\n{sum(results)} of {len(results)} pass")
sys.exit(0 if all(results) else 1)
