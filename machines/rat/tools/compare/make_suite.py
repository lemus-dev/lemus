#!/usr/bin/env python3
"""Generate the test suite: input signals (48 kHz, the NAM captures' rate) and
tools/compare/specs/suite.json, which pairs every NAM capture with knob positions and its
input level.

Knob positions (rotation 0..1 over the 300-degree travel) come from the capture names, or
from the pack's description where the names don't carry them (index.json "pack" says which):
  - clock positions (outmoded: "D-12_00 F-1_30 V-3_00"; stomptones: "DIST-0900_FILTER-1200";
    pablobarrasaa's description) map to rotation with 7:00 = 0, 12:00 = 0.5, 5:00 = 1 (0.1 per
    hour);
  - 0-10 numbers (pong, mmasonmusic, autoguitarrig, octaviofotografia, omarplastiko78): n/10. The
    RAT2 panel has no numbers, so 0 and 10 are taken as the ends of the travel;
  - tick indices 0-6 (imposteringtheband "D 4/6", ideofunk): the vintage panels' 7 printed ticks
    at -135 + 45 n degrees, i.e. 0.05 + 0.15 n;
  - percentages (rbrt "RAT 15 50 70", tone3000slm "RAT-D33-F50-V55", lorenzobona): n/100;
  - index grids (romanrecords "Distortion i Filter j", jakkwiat "D{i}_F{j}", i, j = 1..7)
    have unknown positions: the spec records the index and fit_nam.py solves for the pot
    resistance at each index.
VOLUME is only in the spec when it is stated (compare.py's default is 0.5).

Every spec also says what the pack's page says about the pedal (year, opamp, variant), whether
its level is calibrated ("calibrated"; level_dbu None means unknown, compare.py then uses its
default), how the knobs were read ("knobs": stated, index, described, unknown; "knob_scale"),
and "exclude": the reasons not to fit to it (empty = usable): its variant (turbo, large-box,
modded, fat-rat), led-diodes, index-knobs, unknown-knobs (DISTORTION and FILTER are then a
noon placeholder), approx-knobs. compare.py leaves out every capture with a variant.

The signals are deterministic. A signal file that already exists and differs from what this
generates is kept (the NAM renders were made from it) unless --rewrite-signals is given.

Usage: tools/compare/make_suite.py [--rewrite-signals]     (repo venv, ../../.venv)
"""
import json
import pathlib
import re
import sys

import numpy as np
import soundfile as sf

ROOT = pathlib.Path(__file__).resolve().parents[2]
SIG = ROOT / "build/suite/signals"
SPECS = ROOT / "tools/compare/specs"
RATE = 48000


def db(x):
    return 10 ** (x / 20)


def pluck(freqs, starts, dur, peak, seed=1):
    """Karplus-Strong plucked strings (a guitar stand-in): notes at freqs, starting at
    starts (s), mixed and normalised to peak (linear)."""
    rng = np.random.default_rng(seed)
    out = np.zeros(int(dur * RATE))
    for f, t0 in zip(freqs, starts):
        n = int(RATE / f)
        buf = rng.uniform(-1, 1, n)
        # Soften the excitation like a pick plus pickup: one-pole low-pass.
        for k in range(1, n):
            buf[k] = 0.6 * buf[k] + 0.4 * buf[k - 1]
        i0 = int(t0 * RATE)
        m = len(out) - i0
        y = np.zeros(m)
        b = buf.copy()
        idx = 0
        for k in range(m):
            nxt = (idx + 1) % n
            v = b[idx]
            b[idx] = 0.996 * 0.5 * (b[idx] + b[nxt])
            y[k] = v
            idx = nxt
        out[i0:] += y
    out *= peak / np.max(np.abs(out))
    return out


def fade(x, ms=5):
    n = int(ms * RATE / 1000)
    w = np.ones(len(x))
    w[:n] = np.linspace(0, 1, n)
    w[-n:] = np.linspace(1, 0, n)
    return x * w


def signals():
    t = lambda d: np.arange(int(d * RATE)) / RATE
    s = {}
    s["pluck-mf"] = (pluck([82.4, 123.5, 164.8, 110.0, 146.8, 196.0, 329.6, 246.9],
                           [0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 2.0, 2.4], 3.2, db(-12)),
                     "Plucked power chords and single notes, -12 dBFS peak")
    s["pluck-soft"] = (pluck([98.0, 146.8, 196.0, 220.0, 293.7], [0.0, 0.0, 0.0, 1.2, 1.6], 2.4, db(-30), seed=2),
                       "Softly plucked chord and notes, -30 dBFS peak")
    steps = [-42, -36, -30, -24, -18, -12, -6]
    s["sine-steps"] = (np.concatenate([fade(db(L) * np.sin(2 * np.pi * 220 * t(0.25))) for L in steps]),
                       "220 Hz sine at " + ", ".join(map(str, steps)) + " dBFS, 0.25 s each")
    d = 2.0
    f0, f1 = 30.0, 16000.0
    k = np.log(f1 / f0) / d
    s["sweep"] = (fade(db(-24) * np.sin(2 * np.pi * f0 * (np.exp(k * t(d)) - 1) / k)),
                  "Log sweep 30 Hz - 16 kHz at -24 dBFS")
    s["twotone"] = (fade(db(-26) * (np.sin(2 * np.pi * 330 * t(1.0)) + np.sin(2 * np.pi * 495 * t(1.0)))),
                    "330 + 495 Hz, -20 dBFS peak")
    rng = np.random.default_rng(3)
    nz = rng.standard_normal(RATE)
    nz = np.convolve(nz, np.ones(8) / 8, mode="same")
    env = (np.arange(RATE) % 12000) < 6000
    s["bursts"] = (fade(db(-24) * nz / np.max(np.abs(nz)) * env), "Low-passed noise bursts, -24 dBFS peak")
    tb = ROOT.parent / "tb303/private/reference/ab/famous03-ours.wav"
    if tb.exists():
        y, r = sf.read(tb)
        if y.ndim > 1:
            y = y.mean(axis=1)
        from scipy.signal import resample_poly
        y = resample_poly(y, 160, 147)[: 4 * RATE]
        s["tb303"] = (fade(db(-12) * y / np.max(np.abs(y))), "Our TB-303 playing an acid line, -12 dBFS peak")
    elif (SIG / "tb303.wav").exists():
        # The TB-303 source isn't here: keep the signal made from it before.
        y, _ = sf.read(SIG / "tb303.wav")
        s["tb303"] = (y, "Our TB-303 playing an acid line, -12 dBFS peak")
    return s


def rotation_from_clock(txt):
    txt = txt.upper()
    if txt in ("MAX",):
        return 1.0
    if txt in ("MIN",):
        return 0.0
    m = re.match(r"(\d{1,2})[_:]?(\d{2})$", txt)
    if not m:
        raise ValueError(txt)
    h, mi = int(m.group(1)), int(m.group(2))
    hours = (h % 12) + mi / 60.0
    # 7:00 = 0 ... 12:00 = 0.5 ... 5:00 = 1
    from_seven = (hours - 7) % 12
    return round(from_seven * 0.1, 4)


def ten(n):
    """A 0-10 number: 0 and 10 at the ends of the travel."""
    return round(float(n) / 10, 4)


def tick(n):
    """Tick n (0-6) of the 7 printed ticks, every 45 degrees from -135 (the travel is +-150)."""
    return round((float(n) * 45 - 135 + 150) / 300, 4)


def pct(n):
    return round(float(n) / 100, 4)


def triple(fn, d, f, v):
    return {"dist": fn(d), "filter": fn(f), "volume": fn(v)}


def spec_for(e, m):
    """The suite spec for model m of pack e (entries of private/reference/nam/index.json)."""
    slug, name = e["slug"], m["name"]
    spec = {"capture": slug, "model": m["file"], "name": name,
            "level_dbu": m["input_level_dbu"], "out_level_dbu": m["output_level_dbu"]}
    knobs, scale, exclude = "stated", "clock", []
    if slug == "outmoded-rat2-11dbu":
        g = re.search(r"D-(\S+) F-(\S+) V-(\S+)", name)
        spec.update(dist=rotation_from_clock(g.group(1)), filter=rotation_from_clock(g.group(2)),
                    volume=rotation_from_clock(g.group(3)), level_dbu=11.5, out_level_dbu=11.5)
    elif slug == "stomptones-rat2":
        g = re.search(r"DIST-(\w+)_FILTER-(\w+)", name)
        spec.update(dist=rotation_from_clock(g.group(1)), filter=rotation_from_clock(g.group(2)))
    elif slug == "romanrecords-rat-90s":
        g = re.search(r"Distortion (\d) Filter (\d)", name)
        spec.update(dist_index=int(g.group(1)), filter_index=int(g.group(2)))
        knobs, scale, exclude = "index", "index 1-7", ["index-knobs"]
    elif slug == "jakkwiat-rat2":
        g = re.search(r"D(\d)_F(\d)", name)
        spec.update(dist_index=int(g.group(1)), filter_index=int(g.group(2)))
        knobs, scale, exclude = "index", "index 1-7", ["index-knobs"]
    elif slug == "lorenzobona-largebox-reissue":
        g = re.search(r"(\d+)\s*[%_]", name)
        spec.update(dist=int(g.group(1)) / 100.0, filter=0.0, volume=0.5, variant="large-box")
        scale = "percent"
    elif slug == "returntozero-turbo-rat":
        spec.update(dist=1.0, filter=0.0, variant="turbo")
        scale = "min/max"
    # Packs added 2026-10.
    elif slug == "pong-rat2-1996-usa":
        # The settings are in the description, by the word in the model name.
        word = name.split()[-1]
        g = re.search(word + r" Setting : Distortion ([\d.]+) Filter ([\d.]+) Volume ([\d.]+)", e["description"])
        spec.update(triple(ten, *g.groups()))
        scale = "0-10"
    elif slug == "rbrt-rat-1990s":
        g = re.search(r"RAT (\d+) (\d+) (\d+)", name)
        spec.update(triple(pct, *g.groups()))
        scale = "percent"
    elif slug == "ideofunk-vintage-reissue":
        g = re.search(r"D([\d.]+)F([\d.]+)V([\d.]+)", name)
        scale = "ticks 0-6"
        if g:
            spec.update(triple(tick, *g.groups()))
        else:  # "Nuno Beato": Nuno Bettencourt's "cross" setting, not given
            knobs, scale = "unknown", "unknown"
            exclude.append("unknown-knobs")
        if "LED" in name.split():
            exclude.append("led-diodes")
    elif slug == "imposteringtheband-1984-whiteface":
        g = re.search(r"D(\d+)_F(\d+)_V(\d+)", name)
        spec.update(triple(tick, *g.groups()))
        scale = "ticks 0-6"
    elif slug == "pablobarrasaa-bigbox-1995":
        g = re.search(r"Gain at (\d+)' o clock , Filter (\d+)' o clock, Volume (\d+)' o clock", e["description"])
        spec.update(triple(lambda h: rotation_from_clock(f"{h}:00"), *g.groups()))
    elif slug == "mmasonmusic-rat2-lm308":
        g = re.search(r"D(\d+), F(\d+), V(\d+)", name)
        spec.update(triple(ten, *g.groups()))
        scale = "0-10"
    elif slug == "autoguitarrig-rat2":
        g = re.search(r"_D(\d+)_F(\d+)_L(\d+)", name)
        spec.update(triple(ten, *g.groups()))
        scale = "0-10"
    elif slug == "cripplingguitaraddiction-rat":
        # "distortion set almost 0 filter is at noon and volume is at max"; the photo has
        # DISTORTION near the 9:00 tick.
        spec.update(dist=0.15, filter=0.5, volume=1.0)
        knobs, scale = "described", "described"
        exclude.append("approx-knobs")
    elif slug == "octaviofotografia-rat":
        g = re.search(r"D(\d+)_F(\d+)_V(\d+)", name)
        spec.update(triple(ten, *g.groups()))
        scale = "0-10"
    elif slug == "hk-rat":
        knobs, scale = "unknown", "unknown"
        exclude.append("unknown-knobs")
    elif slug == "colossaldave-modded-rat":
        knobs, scale = "unknown", "unknown"
        exclude.append("unknown-knobs")
    elif slug == "jtn191-80s-rat":
        knobs, scale = "unknown", "unknown"
        exclude.append("unknown-knobs")
        if "LED" in name:
            exclude.append("led-diodes")
    elif slug == "omarplastiko78-fat-rat":
        g = re.search(r"V(\d+) F(\d+) D(\d+)", name)
        v, f, d = g.groups()
        spec.update(triple(ten, d, f, v))
        scale = "0-10"
    elif slug == "tone3000slm-turbo-rat":
        g = re.search(r"D(\d+)-F(\d+)-V(\d+)", name)
        spec.update(triple(pct, *g.groups()))
        scale = "percent"
    else:
        raise ValueError(f"no knob parsing for pack {slug}")
    if knobs == "unknown":
        # A placeholder so every spec renders (compare.py needs DISTORTION and FILTER); the
        # "unknown-knobs" exclusion says not to trust it.
        spec.setdefault("dist", 0.5)
        spec.setdefault("filter", 0.5)
    pack = e.get("pack") or {}
    if pack.get("variant") and "variant" not in spec:
        spec["variant"] = pack["variant"]
    if spec.get("variant"):
        exclude.insert(0, spec["variant"])
    spec.update(year=pack.get("year"), opamp=pack.get("opamp"), calibrated=spec["level_dbu"] is not None,
                knobs=knobs, knob_scale=scale, exclude=exclude)
    return spec


def captures():
    index = json.loads((ROOT / "private/reference/nam/index.json").read_text())
    return [spec_for(e, m) for e in index for m in e["models"]]


def main():
    SIG.mkdir(parents=True, exist_ok=True)
    SPECS.mkdir(parents=True, exist_ok=True)
    rewrite = "--rewrite-signals" in sys.argv
    sigs = []
    for name, (x, desc) in signals().items():
        path = SIG / f"{name}.wav"
        x = x.astype(np.float32)
        if path.exists() and not rewrite:
            old, r = sf.read(path, dtype="float32")
            if r != RATE or old.shape != x.shape or not np.array_equal(old, x):
                print(f"{name}: the existing file differs from what this generates; kept "
                      "(its renders were made from it; --rewrite-signals to replace it)")
                x = old
        else:
            sf.write(path, x, RATE, subtype="FLOAT")
        sigs.append({"name": name, "file": str(path.relative_to(ROOT)), "seconds": round(len(x) / RATE, 3),
                     "description": desc})
        print(f"{name:12s} {len(x) / RATE:5.2f} s  {desc}")
    caps = captures()
    suite = {"rate": RATE, "signals": sigs, "captures": caps}
    (SPECS / "suite.json").write_text(json.dumps(suite, indent=1))
    print(len(caps), "captures in tools/compare/specs/suite.json")


if __name__ == "__main__":
    main()
