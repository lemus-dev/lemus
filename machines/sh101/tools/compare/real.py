#!/usr/bin/env python3
"""Behavioural numbers from recordings of real SH-101s (private/reference/shootout, index.md):
the clips whose settings are known well enough to pin a law. docs/roland-measurements.md, "Real
recordings", sets them against Roland's plug-in and our model.

Usage: real.py [--json OUT]   (run with the venv's python from anywhere)
The recordings are lossy (YouTube, SoundCloud) and the slider positions are read off video, so
these are checks of behaviour and of laws to within their accuracy, not sample comparisons.
"""
import json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import analyse as A

WAV = os.path.normpath(os.path.join(HERE, "../../private/reference/shootout/wav"))


def clip(name):
    x, sr = A.load(os.path.join(WAV, "roland_sh-101_real__" + name + ".wav"))
    return x, sr


def plateaus(t, f, min_len=0.12, tol_cents=15):
    """Stable-pitch stretches of a pitch track: (start, end, median Hz)."""
    out, i = [], 0
    ok = np.isfinite(f)
    while i < len(f):
        if not ok[i]:
            i += 1; continue
        j = i
        while j + 1 < len(f) and ok[j + 1] and abs(1200 * math.log2(f[j + 1] / np.median(f[i:j + 1]))) < tol_cents:
            j += 1
        if t[j] - t[i] >= min_len:
            out.append((float(t[i]), float(t[j]), float(np.median(f[i:j + 1]))))
        i = j + 1
    return out


def env_attack_release():
    """A/D/S/R all 10, saw, VCA ENV (friendly-noise): the attack and the release of the level."""
    x, sr = clip("friendly-noise_youtube_eStzYAZoOAI__saw_vcf_env_sweep_no_res_adsr_max_c4")
    t, e = A.envelope(x, sr, 0.02, 0.005)
    e = np.convolve(e, np.ones(5) / 5, "same")
    top = e.max()
    start = t[np.argmax(e > top * 0.01)]
    t10, t90, t99 = A.rise(t, e, start, (0.1, 0.9, 0.99), top)
    # release: from where the level leaves its plateau
    peak_i = int(np.argmax(e))
    after = np.where(e[peak_i:] < 0.9 * top)[0]
    rel0 = t[peak_i + after[0]] if len(after) else float("nan")
    tau, t20, t40 = A.decay_tau(t, e, rel0, t[-1], hi_db=-1, lo_db=-25)
    return {"attack_t10_90": round(t90 - t10, 3), "attack_t99": round(t99, 3),
            "release_tau": round(tau, 3), "release_t-20dB": round(t20, 3)}


def env_selfosc_sweep():
    """RES 10, FREQ 0, ENV 10, KYBD 10, A/D/S/R 10, RANGE 16', TRANSPOSE L, top key (friendly-noise):
    the envelope as the pitch of the self-oscillation. The tracker's 30 Hz floor and the video's edit
    cut the sweep's bottom off, so from_hz is not the rest point; online_release_fit.py fits the
    release instead (rest 26-32 Hz, 7.0-7.3 octaves; docs/online-references.md)."""
    x, sr = clip("friendly-noise_youtube_eStzYAZoOAI__selfosc_vcf_env_sweep_res_max_adsr_max")
    t, f = A.pitch_track(x, sr, 0.01, 0.06, 30, 5000)
    ok = np.isfinite(f)
    lo, hi = np.percentile(f[ok], [2, 98])
    return {"from_hz": round(float(lo), 1), "to_hz": round(float(hi), 1), "octaves": round(math.log2(hi / lo), 2)}


def smooth_track(x, sr, hop=0.01, fmin=30, fmax=3000):
    """YIN pitch track, median-filtered, in cents relative to its median."""
    from scipy.signal import medfilt
    t, f = A.pitch_track(x, sr, hop, 0.05, fmin, fmax)
    ok = np.isfinite(f)
    f = np.where(ok, f, np.nanmedian(f))
    f = medfilt(f, 7)
    return t, f, ok


def longest_glide(t, f, ok, min_cents=600):
    """The longest stretch moving one way (ignoring wobbles under 3 cents per step): indices."""
    c = 1200 * np.log2(f / np.median(f))
    d = np.diff(c)
    best, bi, bj = 0.0, 0, 0
    i = 0
    while i < len(d):
        sgn = np.sign(d[i]) if abs(d[i]) > 3 else 0
        if sgn == 0:
            i += 1; continue
        j = i
        while j + 1 < len(d) and (np.sign(d[j + 1]) == sgn or abs(d[j + 1]) <= 3) and ok[j + 1]:
            j += 1
        span = abs(c[j + 1] - c[i])
        if span > best:
            best, bi, bj = span, i, j + 1
        i = j + 1
    return bi, bj, c


def portamento():
    """PORTAMENTO ON: each long glide fitted with an exponential in cents (an RC on the CV)."""
    out = {}
    for name, label in (("once-upon-a-synth_youtube_PFXO4ZRsVC4__portamento_on_max_time_glide_up_then_down", "max"),
                        ("automaticgainsay_youtube_rA93zzTjk1A__portamento_on_glides_time_40_to_55pct", "3 then 10")):
        x, sr = clip(name)
        t, f, ok = smooth_track(x, sr)
        res = []
        mask = np.ones(len(f), bool)
        for _ in range(2):   # the two longest glides
            bi, bj, c = longest_glide(t, np.where(mask, f, np.median(f)), ok & mask)
            if bj - bi < 20:
                break
            tau, ci = A.exp_fit(t[bi:bj + 1], c[bi:bj + 1])
            res.append({"from_hz": round(float(f[bi]), 1), "to_hz": round(float(f[bj]), 1),
                        "seconds": round(float(t[bj] - t[bi]), 2), "tau": round(tau, 3),
                        "asymptote_hz": round(float(np.median(f) * 2 ** (ci / 1200)), 1)})
            mask[max(0, bi - 5):bj + 5] = False
        out[label] = res
    return out


def bender():
    """BENDER VCO at ~90 % and ~50 %: the furthest stable pitches either side of the held note."""
    out = {}
    for name, label in (("automaticgainsay_youtube_rA93zzTjk1A__bender_vco_depth90_bends_a2", "VCO 90%"),
                        ("automaticgainsay_youtube_rA93zzTjk1A__bender_vco_depth50_tritone_bends_a", "VCO 50%")):
        x, sr = clip(name)
        t, f = A.pitch_track(x, sr, 0.01, 0.05, 30, 3000)
        p = plateaus(t, f, 0.08, 20)
        hz = np.array([h for _, _, h in p])
        dur = np.array([e - s for s, e, _ in p])
        centre = hz[np.argmax(dur)] if len(hz) else float("nan")
        c = 1200 * np.log2(hz / centre)
        # drop octave errors: plateaus an octave or more from every other plateau
        keep = [k for k in range(len(c)) if sum(abs(c[k] - c[m]) < 300 for m in range(len(c)) if m != k) >= 1]
        c = c[keep]
        out[label] = {"centre_hz": round(float(centre), 1), "up_semitones": round(float(c.max()) / 100, 2),
                      "down_semitones": round(float(c.min()) / 100, 2)}
    return out


def noise():
    out = {}
    for name, label in (("failed-muso_youtube_6NqaegXGbtE__noise_full_level_open_filter", "full"),
                        ("failed-muso_youtube_6NqaegXGbtE__noise_half_level_open_filter", "half")):
        x, sr = clip(name)
        out[label] = A.noise_spectrum(x[int(0.2 * sr):int(-0.1 * sr)], sr)
    return out


def tracking():
    out = {}
    x, sr = clip("automaticgainsay_youtube_VQXq7OmZCtY__selfosc_sine_played_from_keyboard_kybd85")
    t, f = A.pitch_track(x, sr, 0.01, 0.04, 30, 4000)
    p = plateaus(t, f)
    hz = [h for _, _, h in p]
    if hz:
        span = 1200 * math.log2(max(hz) / min(hz))
        out["kybd~85%"] = {"notes": len(p), "span_cents": round(span, 0),
                           "oct_per_oct_if_E2_to_F4": round(span / 2500, 3)}
    x, sr = clip("tonelab_youtube_zR3U7za05dI__self_osc_sources_off_kybd_max_notes_played")
    t, f = A.pitch_track(x, sr, 0.01, 0.04, 30, 4000)
    p = plateaus(t, f)
    hz = sorted({round(h, 0) for _, _, h in p})
    if len(hz) >= 2:
        steps = [1200 * math.log2(h / hz[0]) for h in hz]
        out["kybd 10"] = {"pitches_hz": hz, "cents_from_lowest": [round(s) for s in steps],
                          "off_semitone_grid_cents": [round(s - 100 * round(s / 100)) for s in steps]}
    return out


def arpeggio():
    """ARPEGGIO UP on held chords, clocked by the LFO: the step period from the pitch steps."""
    x, sr = clip("automaticgainsay_youtube_5k-poyZyg2w__arp_up")
    t, f = A.pitch_track(x, sr, 0.005, 0.03, 60, 2000)
    p = plateaus(t, f, 0.05, 30)
    starts = np.array([s for s, _, _ in p])
    steps = np.diff(starts)
    steps = steps[(steps > 0.1) & (steps < 0.5)]
    return {"steps": int(len(steps)), "median_step_s": round(float(np.median(steps)), 4) if len(steps) else None}


def saw_partials():
    x, sr = clip("italo-brutalo_youtube_Cr4Le00_MuA__saw_single_note_d2_sh101")
    y = x[int(0.3 * sr):int(2.0 * sr)]
    f = A.f0(y, sr, 40, 400)
    return {"hz": round(f, 2), "h1-8_db": A.harmonic_profile(y, sr, f, 8)}


def pwm():
    x, sr = clip("gstormelectro_youtube_SsHcY4YQOfw__pwm_lfo_held_a3_rate_sweep")
    y = x[int(2.5 * sr):int(6.0 * sr)]
    t, d = A.duty_track(y, sr, 220.0, 0.01)
    return {"duty_pct_min_max": [round(float(np.percentile(d, 2)) * 100, 1), round(float(np.percentile(d, 98)) * 100, 1)]}


def main():
    res = {}
    for k, fn in (("env (A/D/S/R 10, VCA ENV)", env_attack_release), ("env as self-osc pitch (ENV 10)", env_selfosc_sweep),
                  ("portamento ON", portamento), ("bender", bender), ("noise, filter open", noise),
                  ("self-oscillation tracking", tracking), ("arpeggio UP", arpeggio), ("saw D2, filter open", saw_partials),
                  ("PWM by LFO, A3", pwm)):
        try:
            res[k] = fn()
        except Exception as e:
            res[k] = {"error": repr(e)}
        print(f"{k}: {json.dumps(res[k])}", flush=True)
    if "--json" in sys.argv:
        json.dump(res, open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=1)


if __name__ == "__main__":
    main()
