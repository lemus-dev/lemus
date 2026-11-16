#!/usr/bin/env python3
"""Measures the SH-101's control laws and block behaviour from a set of renders of the specs
(docs/roland-measurements.md, "What needs measuring"), for Roland's plug-in or ours.

Usage: measure.py DIR [DIR2] [--json OUT] [--only GROUP,...]
  DIR    renders (<spec name>.wav), e.g. private/reference/roland-vst/renders
  DIR2   a second set: printed side by side (DIR first)
Groups: range sub lfoprobe pwmprobe pitch freq onset res kybd envdepth env vca trig porta lfo pw
        mix volume bend cal
Run with the analysis venv's python. Missing renders are skipped.
"""
import argparse, json, math, os, sys
from concurrent.futures import ProcessPoolExecutor
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import analyse as A

SPECS = A.specs()


def wav(d, name):
    p = os.path.join(d, name + ".wav")
    return A.load(p) if os.path.exists(p) else (None, None)


def held_f0(d, name, fmin=20, fmax=8000, t0=0.25, t1=None):
    x, sr = wav(d, name)
    if x is None:
        return None
    s, e = A.note_span(SPECS[name])
    t1 = t1 if t1 is not None else e - 0.05
    y = A.seg(x, sr, s + t0, t1)
    return A.f0(y, sr, fmin, fmax)


def level_db(d, name, t0=0.3, t1=None):
    x, sr = wav(d, name)
    if x is None:
        return None
    s, e = A.note_span(SPECS[name])
    return round(float(A.db(A.rms(A.seg(x, sr, s + t0, (t1 or e) - 0.05)))), 2)


def r(v, n=2):
    return None if v is None or (isinstance(v, float) and not math.isfinite(v)) else round(float(v), n)


# --- groups ------------------------------------------------------------------------------------

def g_range(d):
    out = {}
    for raw in range(6):
        f = held_f0(d, f"probe-range-raw{raw}")
        if f:
            out[f"raw{raw}"] = {"hz": r(f), "a4_octaves": r(math.log2(f / 440), 3)}
    for ft, k in (("16", -1), ("8", 0), ("4", 1), ("2", 2)):
        f = held_f0(d, f"osc-saw-{ft}-A4")
        if f:
            out[f"{ft}'"] = {"hz": r(f), "cents_vs_442": r(A.cents(f, 442 * 2 ** k), 1)}
    return out


def g_sub(d):
    out = {}
    names = [f"probe-sub-raw{i}" for i in range(3)] + [f"osc-sub-{m}" for m in ("1oct", "2oct", "2oct-pulse")]
    for n in names:
        x, sr = wav(d, n)
        if x is None:
            continue
        s, e = A.note_span(SPECS[n])
        y = A.seg(x, sr, s + 0.25, e - 0.05)
        f = A.f0(y, sr, 20, 2000)
        note = A.midi_hz(A.parse_note(SPECS[n]["notes"][0]["note"]))
        out[n] = {"hz": r(f), "octaves_down": r(math.log2(note / f), 2), "h1-6_db": A.harmonic_profile(y, sr, f, 6),
                  "duty": r(A.duty_from_wave(y), 3)}
    return out


def mod_of_note(d, name, what="pitch", t0=0.5):
    x, sr = wav(d, name)
    if x is None:
        return None
    s, e = A.note_span(SPECS[name])
    y = A.seg(x, sr, s + t0, e - 0.05)
    if what == "pitch":
        tc, fc = A.pitch_track(y[: int(4 * sr)], sr, 0.01, 0.03, 20, 5000)
        fc = fc[np.isfinite(fc)]
        lo, hi = (np.percentile(fc, [1, 99]) if len(fc) else (20, 8000))
        t, f = A.period_track(y, sr, max(10.0, 0.6 * lo), min(12000.0, 1.25 * hi))
        if len(t) < 20:
            return {}
        v = 1200 * np.log2(f / np.median(f))
        m = A.modulation(t, v)
        m["unit"] = "cents"
        return m
    t, dd = A.duty_track(y, sr, A.midi_hz(A.parse_note(SPECS[name]["notes"][0]["note"])))
    m = A.modulation(t, dd * 100)
    m["unit"] = "% duty"
    m["duty_min_max"] = [r(float(dd.min()) * 100, 1), r(float(dd.max()) * 100, 1)] if len(dd) else None
    return m


def g_lfoprobe(d):
    return {f"raw{i}": mod_of_note(d, f"probe-lfo-raw{i}") for i in range(6) if wav(d, f"probe-lfo-raw{i}")[0] is not None}


def g_pwmprobe(d):
    return {f"raw{i}": mod_of_note(d, f"probe-pwm-raw{i}", "duty", 0.05) for i in range(6)
            if wav(d, f"probe-pwm-raw{i}")[0] is not None}


def g_pitch(d):
    out = {}
    for raw in (0, 10, 20):
        f = held_f0(d, f"probe-tune-raw{raw:02d}")
        if f:
            out[f"tune-raw{raw:02d}"] = r(f)
    a4 = held_f0(d, "osc-saw-8-A4")
    if a4:
        out["A4_hz"] = r(a4)
        for n in ("F-3", "C-4", "C-5", "C-6"):
            name = f"osc-saw-8-{n.replace('-', '')}"
            f = held_f0(d, name)
            if f:
                out[name] = {"hz": r(f), "cents_vs_ET": r(A.cents(f, a4 * 2 ** ((A.parse_note(n) - 69) / 12)), 1)}
        for name, n, oct_ in (("osc-saw-8-L-F3", "F-3", -1), ("osc-saw-8-H-C6", "C-6", 1)):
            f = held_f0(d, name)
            if f:
                out[name] = {"hz": r(f), "cents_vs_ET": r(A.cents(f, a4 * 2 ** ((A.parse_note(n) - 69) / 12 + oct_)), 1)}
        for t in ("000", "100"):
            f = held_f0(d, f"osc-tune-{t}")
            if f:
                out[f"tune-{t}_cents"] = r(A.cents(f, a4), 1)
    return out


def selfosc(d, name, t0=0.25):
    x, sr = wav(d, name)
    if x is None:
        return None
    s, e = A.note_span(SPECS[name])
    y = A.seg(x, sr, s + t0, e - 0.05)
    lev = float(A.db(A.rms(y)))
    if lev < -70:
        return {"hz": None, "dbfs": r(lev, 1)}
    f = A.f0_sine(y, sr, 2.0)
    out = {"hz": r(f), "dbfs": r(lev, 1)}
    if f and np.isfinite(f) and f < 5000:
        prof = A.harmonic_profile(y, sr, f, 3)
        out["h2_h3_db"] = prof[1:]
    return out


def g_freq(d):
    out = {}
    for i in range(11):
        v = selfosc(d, f"selfosc-f{i * 10:03d}", t0=1.0 if i < 2 else 0.25)
        if v:
            out[f"slider{i}"] = v
    for raw in list(range(0, 256, 16)) + [255]:
        v = selfosc(d, f"probe-freq-raw{raw:03d}", t0=1.0 if raw < 40 else 0.25)
        if v:
            out[f"raw{raw:03d}"] = v
    return out


def g_onset(d):
    names = [f"selfosc-onset-r{v}" for v in ("075", "080", "082", "084", "086", "088", "090", "095")] + ["selfosc-f050"]
    return {n: selfosc(d, n, t0=0.6) for n in names if wav(d, n)[0] is not None}


def g_res(d):
    out = {}
    for f in ("040", "050", "060", "070", "080", "090", "100"):
        for rr in ("000", "050", "075", "100"):
            n = f"vcf-saw-f{f}-r{rr}"
            x, sr = wav(d, n)
            if x is None:
                continue
            s, e = A.note_span(SPECS[n])
            fund = A.f0(A.seg(x, sr, s + 0.25, e - 0.05), sr, 40, 400, guess=110.5)
            out[n] = A.resonant_peak(A.seg(x, sr, s + 0.25, e - 0.05), sr, fund)
    base = level_db(d, "vcf-bass-r000")
    for rr in ("000", "050", "080", "100"):
        lv = level_db(d, f"vcf-bass-r{rr}")
        if lv is not None and base is not None:
            out[f"bass-r{rr}_db"] = r(lv - base)
    return out


def g_kybd(d):
    out = {}
    notes = ("F-3", "F-4", "F-5", "C-6")
    for k in ("000", "050", "100"):
        pts = []
        for n in notes:
            v = selfosc(d, f"selfosc-kybd{k}-{n.replace('-', '')}")
            if v and v.get("hz"):
                pts.append((A.parse_note(n), v["hz"]))
        if len(pts) >= 2:
            xs = np.array([p[0] / 12 for p in pts]); ys = np.log2([p[1] for p in pts])
            slope = np.polyfit(xs, ys, 1)[0]
            out[f"kybd{k}"] = {"oct_per_oct": r(slope, 3), "hz": {n: r(h) for n, h in zip(notes, [p[1] for p in pts])}}
    for n in ("F3", "F5"):
        v = selfosc(d, f"probe-kybd-neg-{n}")
        if v:
            out[f"neg-{n}"] = v
    return out


def g_envdepth(d):
    out = {}
    base = selfosc(d, "selfosc-env000", t0=0.5)
    if base:
        out["base"] = base.get("hz")
    for e in ("012", "025", "038", "050", "062", "075", "088", "100"):
        n = f"selfosc-env{e}"
        v = selfosc(d, n, t0=0.5)
        if v and v.get("hz") and base and base.get("hz"):
            out[f"env{e}"] = {"hz": v["hz"], "octaves": r(math.log2(v["hz"] / base["hz"]), 2)}
    v = selfosc(d, "probe-env-neg", t0=0.5)
    if v:
        out["env-neg (Roland raw 0)"] = v
    return out


def env_of(d, name):
    x, sr = wav(d, name)
    if x is None:
        return None, None, None
    t, e = A.envelope(x, sr, 0.004, 0.0005)
    return t, e, sr


def g_env(d):
    out = {}
    EIGHTHS = ("000", "012", "025", "038", "050", "062", "075", "088", "100")
    for a in EIGHTHS:
        n = f"env-attack-a{a}"
        t, e, _ = env_of(d, n)
        if t is None:
            continue
        s, end = A.note_span(SPECS[n])
        top = np.max(e[(t > s) & (t < end)])
        t10, t90, t99 = A.rise(t, e, s, (0.1, 0.9, 0.99), top)
        out[n] = {"t10": r(t10, 4), "t90": r(t90, 4), "t99": r(t99, 4), "t10_90": r(t90 - t10, 4)}
    for dd in EIGHTHS:
        n = f"env-decay-d{dd}"
        t, e, _ = env_of(d, n)
        if t is None:
            continue
        s, end = A.note_span(SPECS[n])
        tp = t[(t > s) & (t < s + 0.1)][np.argmax(e[(t > s) & (t < s + 0.1)])]
        tau, t20, t40 = A.decay_tau(t, e, tp, end)
        out[n] = {"tau": r(tau, 4), "t-20dB": r(t20, 4), "t-40dB": r(t40, 4)}
    full = None
    for v in ("000", "025", "050", "075"):
        n = f"env-sustain-s{v}"
        t, e, _ = env_of(d, n)
        if t is None:
            continue
        s, end = A.note_span(SPECS[n])
        peak = np.max(e[(t > s) & (t < s + 0.1)])
        sus = np.median(e[(t > end - 0.4) & (t < end - 0.05)])
        out[n] = {"sustain_rel": r(sus / peak, 4), "sustain_db": r(float(A.db(sus / peak)), 2)}
    for rr in EIGHTHS:
        n = f"env-release-r{rr}"
        t, e, _ = env_of(d, n)
        if t is None:
            continue
        s, end = A.note_span(SPECS[n])
        tau, t20, t40 = A.decay_tau(t, e, end, end + 12)
        out[n] = {"tau": r(tau, 4), "t-20dB": r(t20, 4), "t-40dB": r(t40, 4)}
    return out


def g_vca(d):
    out = {}
    for m in ("env", "gate"):
        n = f"vca-{m}-phrase"
        t, e, _ = env_of(d, n)
        if t is None:
            continue
        rises, falls = [], []
        for i in range(len(SPECS[n]["notes"])):
            s, end = A.note_span(SPECS[n], i)
            mm = (t > s) & (t < end + 0.1)
            top = np.max(e[mm])
            t10, t90 = A.rise(t[mm], e[mm], s, (0.1, 0.9), top)
            rises.append(r((t90 - t10) * 1000, 2))
            after = (t > end) & (t < end + 0.2)
            lev = e[after] / max(top, 1e-12)
            k = np.where(lev < 0.1)[0]
            falls.append(r(float(t[after][k[0]] - end) * 1000, 1) if len(k) else None)
        out[n] = {"rise_10_90_ms": rises, "fall_to_10pct_ms": falls}
    return out


def g_trig(d):
    out = {}
    for n in ("trig-gatetrig-legato", "trig-gate-legato", "trig-lfo-held"):
        t, e, _ = env_of(d, n)
        if t is None:
            continue
        sp = SPECS[n]
        s = A.note_span(sp, 0)[0]
        end = max(A.note_span(sp, i)[1] for i in range(len(sp["notes"])))
        out[n] = {"attacks": A.count_attacks(t, e, s - 0.01, end)}
    return out


def g_porta(d):
    out = {}
    names = [f"porta-on-p{p}" for p in ("012", "025", "038", "050", "062", "075", "088", "100")]
    for n in names + ["porta-auto-legato", "porta-auto-detached"]:
        x, sr = wav(d, n)
        if x is None:
            continue
        sp = SPECS[n]
        s2, e2 = A.note_span(sp, 1)
        tt, ff = A.period_track(A.seg(x, sr, 0, e2), sr, 50, 3000)
        # the target: where the second note settles (its last half second)
        f_to = A.f0(A.seg(x, sr, e2 - 0.6, e2 - 0.05), sr, 50, 3000)
        out[n] = A.glide(tt, ff, s2, f_to)
    return out


def g_lfo(d):
    out = {}
    for rr in ("000", "010", "025", "040", "050", "060", "075", "090", "100"):
        n = f"lfo-rate-r{rr}"
        if wav(d, n)[0] is not None:
            out[n] = mod_of_note(d, n)
    for w in ("tri", "sqr", "rnd", "noise"):
        n = f"lfo-vco-{w}"
        if wav(d, n)[0] is not None:
            out[n] = mod_of_note(d, n)
    for m in ("010", "025", "038", "050", "075", "100"):
        n = f"lfo-depth-m{m}"
        if wav(d, n)[0] is not None:
            out[n] = mod_of_note(d, n)
    for raw in list(range(128, 256, 16)) + [255]:
        n = f"probe-vcodepth-raw{raw:03d}"
        if wav(d, n)[0] is not None:
            out[n] = mod_of_note(d, n)
    for n in ("lfo-vcf-tri-m025", "lfo-vcf-tri-m050", "lfo-vcf-tri-m075", "lfo-vcf-tri-m100", "lfo-vcf-sqr-m050"):
        if wav(d, n)[0] is not None:
            out[n] = mod_of_note(d, n)
    return out


def g_pw(d):
    out = {}
    for w in ("000", "025", "050", "075", "090", "100"):
        n = f"osc-pulse-pw{w}"
        x, sr = wav(d, n)
        if x is None:
            continue
        s, e = A.note_span(SPECS[n])
        y = A.seg(x, sr, s + 0.25, e - 0.05)
        f = A.f0(y, sr, 50, 2000, guess=A.midi_hz(57))
        dw = A.duty_from_wave(y)
        out[n] = {"duty_wave": r(min(dw, 1 - dw) * 100, 1), "duty_harm": r(A.duty_from_harmonics(y, sr, f) * 100, 1),
                  "dbfs": r(float(A.db(A.rms(y))), 1)}
    for n in ("pwm-lfo", "pwm-env"):
        if wav(d, n)[0] is not None:
            out[n] = mod_of_note(d, n, "duty", 0.05)
    return out


def g_mix(d):
    out = {}
    ref = level_db(d, "mix-saw-100")
    if ref is None:
        return out
    for n in ("mix-saw-010", "mix-saw-025", "mix-saw-038", "mix-saw-050", "mix-saw-062", "mix-saw-075", "mix-saw-090",
              "mix-pulse-050", "mix-sub-050", "mix-all",
              "osc-pulse-pw000", "osc-sub-1oct", "osc-sub-2oct", "osc-sub-2oct-pulse", "osc-noise-100", "osc-noise-050",
              "osc-saw-sub-2oct"):
        lv = level_db(d, n)
        if lv is not None:
            out[n + "_db_vs_saw"] = r(lv - ref)
    for n in ("osc-noise-100", "osc-noise-050"):
        x, sr = wav(d, n)
        if x is not None:
            s, e = A.note_span(SPECS[n])
            out[n] = A.noise_spectrum(A.seg(x, sr, s + 0.2, e - 0.05), sr)
    return out


def g_volume(d):
    out = {}
    ref = level_db(d, "volume-100")
    for v in ("010", "025", "038", "050", "062", "075", "090", "100"):
        lv = level_db(d, f"volume-{v}")
        if lv is not None:
            out[f"volume-{v}"] = {"dbfs": lv, "db_vs_10": r(lv - ref) if ref is not None else None}
    a, b = level_db(d, "vel-030"), level_db(d, "vel-127")
    if a is not None and b is not None:
        out["vel30_vs_127_db"] = r(a - b)
    return out


def g_bend(d):
    out = {}
    for n in ("probe-bend-raw01", "probe-bend-raw11", "probe-bend-raw23", "bend-vco"):
        x, sr = wav(d, n)
        if x is None:
            continue
        sp = SPECS[n]
        ls = A.line_seconds(sp)
        ev = sp["bend"]
        base = A.f0(A.seg(x, sr, 0.2, ev[0]["line"] * ls - 0.02), sr, 50, 4000)
        vals = {}
        for i, e in enumerate(ev[:-1]):
            if e["value"] == 0:
                continue
            t0 = e["line"] * ls + 0.15
            t1 = ev[i + 1]["line"] * ls - 0.02
            f = A.f0(A.seg(x, sr, t0, t1), sr, 30, 8000)
            vals[f"{e['value']:+.0f}"] = r(A.cents(f, base) / 100, 2)
        out[n] = {"semitones": vals}
    return out


def g_cal(d):
    out = {}
    f3, a4, f5 = (held_f0(d, f"cal-vco-{n}") for n in ("F3", "A4", "F5"))
    if a4:
        out["vco_A4_hz"] = r(a4)
    if f3 and f5:
        out["vco_F5/F3_cents_vs_2oct"] = r(A.cents(f5 / f3, 4), 1)
    x, sr = wav(d, "cal-pw")
    if x is not None:
        dw = A.duty_from_wave(A.seg(x, sr, 0.3, 0.95))
        out["pw0_duty"] = r(min(dw, 1 - dw) * 100, 1)
    v = {n: selfosc(d, f"cal-vcf-{n}") for n in ("F4", "A4", "F5")}
    if all(v.values()) and all(x.get("hz") for x in v.values()):
        out["vcf_A4_hz"] = v["A4"]["hz"]
        out["vcf_F5/F4_cents_vs_oct"] = r(A.cents(v["F5"]["hz"] / v["F4"]["hz"], 2), 1)
    return out


GROUPS = {"range": g_range, "sub": g_sub, "lfoprobe": g_lfoprobe, "pwmprobe": g_pwmprobe, "pitch": g_pitch,
          "freq": g_freq, "onset": g_onset, "res": g_res, "kybd": g_kybd, "envdepth": g_envdepth, "env": g_env,
          "vca": g_vca, "trig": g_trig, "porta": g_porta, "lfo": g_lfo, "pw": g_pw, "mix": g_mix,
          "volume": g_volume, "bend": g_bend, "cal": g_cal}


def run(args):
    g, d = args
    try:
        return g, GROUPS[g](d)
    except Exception as e:  # keep the other groups going
        return g, {"error": repr(e)}


def measure(d, groups):
    with ProcessPoolExecutor() as ex:
        return dict(ex.map(run, [(g, d) for g in groups]))


def flat(prefix, v, out):
    if isinstance(v, dict):
        for k, x in v.items():
            flat(f"{prefix}.{k}" if prefix else k, x, out)
    else:
        out[prefix] = v
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--json")
    ap.add_argument("--only")
    a = ap.parse_args()
    groups = a.only.split(",") if a.only else list(GROUPS)
    results = [measure(os.path.abspath(d), groups) for d in a.dirs]
    if a.json:
        json.dump(results[0] if len(results) == 1 else results, open(a.json, "w"), indent=1)
    for g in groups:
        print(f"== {g}")
        rows = [flat("", res.get(g, {}), {}) for res in results]
        keys = list(dict.fromkeys(k for row in rows for k in row))
        for k in keys:
            print(f"  {k:48s} " + "  |  ".join(str(row.get(k)) for row in rows))


if __name__ == "__main__":
    main()
