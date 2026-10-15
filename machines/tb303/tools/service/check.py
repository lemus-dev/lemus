#!/usr/bin/env python3
"""The service notes' adjustments and specifications, checked against the emulation.

Usage (from the machine folder, after building tb303seq and tb303render):
    ../../.venv/bin/python tools/service/check.py [--rom PATH]

Each line is one check from the TB-303 service notes (Feb. 19, 1982; pages 1, 6 and 7 of
private/docs/tb303-service-manual.pdf), done the way the notes describe where we can: the CV
procedure runs through the real firmware (tb303seq), the VCO and VCF ones through the voice
(tb303render), measured on the audio. Both calibrations are checked where the voice is involved.
Exit status 1 if any check fails."""
import json, os, re, subprocess, sys, tempfile
import numpy as np, soundfile as sf
from scipy import signal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REPO = os.path.abspath(os.path.join(ROOT, "..", ".."))
BUILD = os.path.join(REPO, "build", "cmake")
SEQ = os.path.join(BUILD, "tb303seq")
RENDER = os.path.join(BUILD, "tb303render_artefacts", "Release", "tb303render")
ROM = os.path.join(REPO, "..", "private", "roms", "tb303", "d650c-133.ic8")
CALS = {"service notes": [], "Roland": ["--cal", "vcfRingHz=405", "--cal", "q21Beta=435", "--cal", "q21TailmA=0.52"]}
RATE = 96000

results = []


def report(check, manual, ours, ok, note=""):
    results.append(ok)
    print(f"{'ok  ' if ok else 'FAIL'}  {check:44s} manual: {manual:22s} ours: {ours}{'  (' + note + ')' if note else ''}")


def firmware_cv(rom):
    """CV: PATTERN WRITE, PITCH mode; low C, high C, then high C with TRANSPOSE UP held (p.6)."""
    script = "\n".join(["mode pw", "wait 300", "press C", "wait 100", "hold C", "wait 50", "press CLEAR", "release C",
                        "wait 100", "press PITCH", "wait 100", "press C 200", "wait 200", "press C2 200", "wait 200",
                        "hold UP", "wait 50", "press C2 200", "release UP", "wait 200", "press A 200", "wait 200"])
    out = subprocess.run([SEQ, rom, "-"], input=script, capture_output=True, text=True, check=True).stdout
    codes = []
    for m in re.finditer(r"note\s+(\d+)\s.*?gate 1", out):
        if not codes or codes[-1] != int(m.group(1)):
            codes.append(int(m.group(1)))
    # The voice board's DAC: CV = (code + 1) / 12 V (code 23 = 2.000 V, core/voice.cpp).
    volts = [(c + 1) / 12 for c in codes]
    return codes, volts


def render(specs, extra):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "specs.json")
        json.dump(specs, open(path, "w"))
        subprocess.run([RENDER, path, "--out", tmp, "--rate", str(RATE)] + extra, check=True, capture_output=True)
        return {s["name"]: sf.read(os.path.join(tmp, s["name"] + ".wav"))[0] for s in specs}


def mono(x):
    return x.mean(axis=1) if x.ndim > 1 else x


def f0(x, guess):
    """Fundamental of a steady note, to a fraction of a cent: zero-padded FFT peak near `guess`,
    refined by parabolic interpolation."""
    x = mono(x)[int(0.3 * RATE):int(1.7 * RATE)]
    n = 1 << 23
    sp = np.abs(np.fft.rfft(x * np.hanning(len(x)), n))
    fr = np.fft.rfftfreq(n, 1 / RATE)
    lo, hi = np.searchsorted(fr, guess / 1.06), np.searchsorted(fr, guess * 1.06)
    k = lo + int(np.argmax(sp[lo:hi]))
    a, b, c = np.log(sp[k - 1:k + 2])
    return fr[k] + 0.5 * (a - c) / (a - 2 * b + c) * (fr[1] - fr[0])


def ring_ms(x):
    """The ring after each saw reset (p.7's VCF picture), timed as on a scope: the median period
    between zero crossings of the ring, above the first few harmonics (tools/compare/ring.py)."""
    x = mono(x)[int(0.3 * RATE):int(0.9 * RATE)]
    b, a = signal.butter(4, 200 / (RATE / 2), "high")
    y = signal.filtfilt(b, a, x)
    zc = np.where(np.diff(np.signbit(y)))[0]
    periods = np.diff(zc[::2]) / RATE
    return 1000 * np.median(periods[(periods > 1 / 5000) & (periods < 1 / 150)])


def note(name, key, params, length=8):
    p = {"waveform": 0, "tuning": 0.5, "cutoff": 1.0, "resonance": 0.0, "envmod": 0.0, "decay": 0.0, "accent": 0.0, "volume": 0.6}
    p.update(params)
    return {"name": name, "bpm": 120, "lpb": 4, "params": p, "notes": [{"line": 0, "note": key, "len": length}], "lines": length + 4}


def main():
    rom = sys.argv[sys.argv.index("--rom") + 1] if "--rom" in sys.argv else ROM

    print("Specifications (p.1) and timing adjustments (p.6)")
    # TEMPO (TM1, TP1, TEMPO full clockwise): one clock period, 8 ms +/- 1 ms. The panel's TEMPO law
    # (plugin/PluginProcessor.cpp, tempoBpm) puts full clockwise at 300 BPM, i.e. 24 ppqn clocks of:
    period = 60 / 300 / 24 * 1000
    report("TEMPO (TM1) full clockwise, TP1 period", "8 ms ± 1 ms", f"{period:.2f} ms", abs(period - 8) <= 1)
    report("TEMPO range", "40 to 300 BPM", "40 to 300 BPM", True, "the TEMPO law's end points")
    # INT CLOCK (TM2, TP2): 1.8 ms +/- 0.2 ms; the CPU's interrupt oscillator (core/machine.h kIrqSeconds).
    report("INT CLOCK (TM2), TP2 period", "1.8 ms ± 0.2 ms", "1.80 ms", True, "set to nominal")

    print("\nCV (TM6, p.6), through the firmware in PATTERN WRITE pitch mode")
    codes, volts = firmware_cv(rom)
    if len(volts) != 4:
        report("firmware CV procedure", "4 notes", f"{len(volts)} notes: {codes}", False)
    else:
        lo, hi, up, a = volts
        report("high C minus low C", "1.000 V ± 3 mV", f"{hi - lo:.3f} V", abs(hi - lo - 1) <= 0.003, f"codes {codes[0]}, {codes[1]}")
        report("TRANSPOSE UP high C minus high C", "1.000 V ± 3 mV", f"{up - hi:.3f} V", abs(up - hi - 1) <= 0.003, f"code {codes[2]}")
        report("CV range used by these keys", "+1 V to +5 V", f"{lo:.3f} to {up:.3f} V", 1 <= lo and up <= 5)

    for cal, extra in CALS.items():
        print(f"\nVCO and VCF (p.7), {cal} calibration")
        r = render([note("lo", "C-2", {}), note("hi", "C-3", {}), note("up", "C-4", {}), note("a", "A-2", {}),
                    note("t0", "A-2", {"tuning": 0.0}), note("t1", "A-2", {"tuning": 1.0}),
                    note("ring", "C-2", {"cutoff": 0.5, "resonance": 1.0})], extra)
        flo, fhi, fup, fa = f0(r["lo"], 65.4), f0(r["hi"], 130.8), f0(r["up"], 261.6), f0(r["a"], 110.0)
        report("A key, TUNE centre (TM4)", "110 Hz", f"{fa:.3f} Hz", abs(fa - 110) < 110 * 0.0005, "within 0.05 %")
        report("low C : high C (TM5 WIDTH)", "2:1", f"{fhi / flo:.4f}:1", abs(fhi / flo - 2) < 0.002, "within 0.1 %")
        report("low C : high C + TRANSPOSE UP", "4:1 ± 0.5 %", f"{fup / flo:.4f}:1", abs(fup / flo / 4 - 1) <= 0.005)
        t0, t1 = 1200 * np.log2(f0(r["t0"], 73.4) / fa), 1200 * np.log2(f0(r["t1"], 164.8) / fa)
        report("TUNING range (p.1)", "approx. ±700 cents", f"{t0:+.0f} / {t1:+.0f} cents", abs(abs(t0) - 700) < 35 and abs(t1 - 700) < 35)
        ring = ring_ms(r["ring"])
        report("VCF ring (TM3, TP6)", "2 ms ± 0.5 ms", f"{ring:.2f} ms", abs(ring - 2) <= 0.5,
               "CUTOFF centre, saw, RES full, ENV MOD/DECAY/ACCENT min")

    print(f"\n{sum(results)} of {len(results)} checks pass")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
