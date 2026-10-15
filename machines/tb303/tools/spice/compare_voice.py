#!/usr/bin/env python3
"""The whole voice against a SPICE simulation of the whole schematic: the block netlists (vco.cir,
vcf.cir, env.cir, out.cir, with ba662.lib) joined into one circuit, so the blocks load and drive
each other as on the board, against the model through Voice's public interface (tb303blocks voice).

Usage (from the machine folder, after building tb303blocks):
    ../../.venv/bin/python tools/spice/compare_voice.py [--scenarios a,b] [--models datasheet,...]
           [--keep DIR]

Only the expo converter that sets the pitch is not simulated: the saw core's current is a fixed
source, tuned so SPICE's VCO runs at the model's frequency for the note; the two VCOs keep their
own phase. For each note it prints, model / SPICE: the level (peak of a 30 ms RMS envelope, two
periods at the lowest note), the envelope's mean difference while within 30 dB of the peak, the
waveform error (the difference after lining up the VCO phase, against the signal, over the note),
the harmonics' levels (RMS difference over those within 50 dB of the strongest, up to 10 kHz:
SPICE's variable time step leaves a numerical floor between them), and the DC step (the mean over
whole VCO periods) at the output where the note starts and ends: the VCA's and the filter's DC
shifts through the coupling capacitors.
--keep writes both outputs as WAV files there."""
import os, re, subprocess, sys, tempfile
import numpy as np
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(ROOT, "..", ".."))
BLOCKS = os.path.join(REPO, "build", "cmake", "tb303blocks")
MODELS = {"datasheet": os.path.join(HERE, "models-datasheet.lib"),
          "ds-low": os.path.join(HERE, "models-datasheet-low.lib"),
          "ds-high": os.path.join(HERE, "models-datasheet-high.lib")}
FS = 44100
SETTLE = 0.3   # seconds before the first note, both sides

# name: knobs (cutoff, resonance, envmod, decay, accent, volume), wave, DAC note, notes (on, off,
# accent; seconds after SETTLE), seconds
SCENARIOS = {
    "plain": ((0.5, 0.5, 0.5, 0.5, 0.5, 0.7), "saw", 36, [(0.05, 0.25, 0), (0.35, 0.45, 0), (0.55, 0.57, 0)], 0.8),
    "accent": ((0.5, 0.8, 0.6, 0.5, 1.0, 0.7), "saw", 36, [(0.05, 0.20, 0), (0.30, 0.45, 1), (0.55, 0.70, 1)], 0.9),
    "square-res": ((0.75, 1.0, 0.3, 0.3, 0.5, 0.5), "square", 48, [(0.05, 0.30, 0), (0.40, 0.55, 1)], 0.75),
    "long": ((0.3, 0.3, 0.7, 1.0, 0.0, 0.7), "saw", 24, [(0.05, 1.05, 0)], 1.3),
}

# Joining the blocks. Each block's nodes get its prefix except the rails; the stand-ins each
# block's own test used for its neighbours are dropped, and the joins name the shared nodes.
DROP = {
    "vco": {"V12", "VREF", "Bsel", "Videal", "R62", "C17", "R70"},
    "vcf": {"V12", "VREF", "Iff", "R121", "C21", "R122", "C21b", "R124"},
    "env": {"V12", "VREF", "R69", "Vlad", "Vq12e", "XIC15", "RIC6"},
    "out": {"V12", "VREF", "RVRa", "RVRb", "C15", "R67", "Rq18", "IABC"},
}
SHARED = {"0", "n12", "nref"}


def joins(wave):
    return {("vco", "nsrc"): "vco_nsrcc",                                   # the shaper sees the buffered saw
            ("vcf", "nin"): "vco_nsq" if wave == "square" else "vco_nsrcc",  # WAVEFORM into R62
            ("env", "nq11c"): "nq11m",                                      # Q11's collector, via Vlad
            ("vcf", "nq11c"): "nq11c",
            ("out", "nvcf"): "vcf_nout",                                    # VCF OUT into the VCA branches
            ("out", "nwip"): "vcf_nwip",                                    # the resonance wiper
            ("env", "nic1"): "out_npin1"}                                   # I_ABC into IC15 pin 1


def block_lines(name, wave):
    text = open(os.path.join(HERE, f"{name}.cir")).read().splitlines()
    jn = joins(wave)
    out = []
    for line in text:
        s = line.strip()
        if not s or s.startswith("*"):
            continue
        tok = s.split()
        if tok[0] in DROP[name]:
            continue
        if tok[0].startswith("."):
            out.append(s)
            continue
        s = re.sub(r"\s+IC=\S+", "", s)            # initial conditions come from .ic below
        # rename node tokens outside braces
        parts = re.split(r"(\{[^}]*\})", s)
        for i in range(0, len(parts), 2):
            parts[i] = re.sub(r"\b(n[A-Za-z0-9_]+)\b",
                              lambda m: m.group(1) if m.group(1) in SHARED else jn.get((name, m.group(1)), f"{name}_{m.group(1)}"),
                              parts[i])
        out.append("".join(parts))
    return out


def netlist(wave):
    lines = ["V12 n12 0 11.85", "VREF nref 0 5.333", "Vlad nq11c nq11m 0"]
    for b in ("vco", "vcf", "env", "out"):
        lines.append(f"* --- {b}.cir")
        lines += block_lines(b, wave)
    lines.append(".ic v(vco_nsaw)=11")
    return "\n".join(lines) + "\n"


def pwl(points):
    return " ".join(f"{t:.6f} {v}" for t, v in points)


def sources(notes, sec):
    edge = 10e-6
    g, a = [(0, 0)], [(0, 0)]
    for i, (on, off, acc) in enumerate(notes):
        on, off = on + SETTLE, off + SETTLE
        g += [(on, 0), (on + edge, 5.5), (off, 5.5), (off + edge, 0)]
        nxt = notes[i + 1][0] + SETTLE if i + 1 < len(notes) else sec
        lvl = 11.85 if acc else 0
        a += [(on - 0.001, a[-1][1]), (on - 0.001 + edge, lvl), (nxt - 0.001, lvl)]
    a = sorted(set(a))
    return f"Vgate env_ngate 0 PWL({pwl(g)})\nVacc env_nacc 0 PWL({pwl(a)})\n"


def ours(scn, tmp):
    knobs, wave, note, notes, sec = SCENARIOS[scn]
    out = os.path.join(tmp, "ours.txt")
    shifted = ";".join(f"{a},{b},{c}" for a, b, c in notes)
    r = subprocess.run([BLOCKS, "voice", "service", ",".join(map(str, knobs)), wave, str(note), shifted, str(sec), out],
                       capture_output=True, text=True, check=True)
    params = dict(kv.split("=") for kv in r.stdout.split())
    d = np.loadtxt(out)
    lat = float(params["latency"])
    y = np.interp(np.arange(len(d)) + lat, np.arange(len(d)), d[:, 1])   # undo the decimator's delay
    return y, params


def tune_ichg(models, hz, tmp):
    """The saw core's current for the model's frequency (vco.cir alone, two tries)."""
    ichg = hz * 0.01e-6 * 5.3
    for _ in range(3):
        deck, out = os.path.join(tmp, "tune.cir"), os.path.join(tmp, "tune.txt")
        with open(deck, "w") as fh:
            fh.write(f"* tune\n.param ichg={ichg:.6e} ideal=0 f={hz} treset=1e-6 c17ic=0\n.include {models}\n"
                     f".include {os.path.join(HERE, 'vco.cir')}\n.control\nset wr_singlescale\noption numdgt=10\n"
                     f"tran 0.5u 0.4 0.2 0.5u uic\nwrdata {out} v(nsrcc)\nquit\n.endc\n.end\n")
        subprocess.run(["ngspice", "-b", deck], capture_output=True, check=True)
        d = np.loadtxt(out)
        t, v = d[:, 0], d[:, 1]
        mid = 0.5 * (v.min() + v.max())
        k = np.where((v[:-1] < mid) & (v[1:] >= mid))[0]    # the saw only rises through mid at its reset
        tc = t[k] + (mid - v[k]) / (v[k + 1] - v[k]) * (t[k + 1] - t[k])
        f = 1 / np.median(np.diff(tc))
        ichg *= hz / f
        if abs(f / hz - 1) < 2e-4:
            break
    return ichg


def spice(models, scn, params, ichg, tmp):
    knobs, wave, note, notes, sec = SCENARIOS[scn]
    total = sec + SETTLE
    deck, out = os.path.join(tmp, "voice.cir"), os.path.join(tmp, "voice.txt")
    p = {"ichg": f"{ichg:.6e}", "ideal": 0, "f": 1, "treset": 1e-6, "c17ic": 0, "iff": 1e-5, "iabc": 0,
         "kres": params["wres"], "tvol": params["tvol"]}
    p.update({k: params[k] for k in ("tcut", "tenv", "tdec", "xacc", "wres", "tm3")})
    with open(deck, "w") as fh:
        fh.write("* compare_voice\n.param " + " ".join(f"{k}={v}" for k, v in p.items()) + "\n")
        fh.write(f".include {models}\n.include {os.path.join(HERE, 'ba662.lib')}\n")
        fh.write(netlist(wave))
        fh.write(sources(notes, total))
        fh.write(f".options method=gear\n.control\nset wr_singlescale\noption numdgt=10\n"
                 f"tran 10u {total} {SETTLE} 1u\nwrdata {out} v(out_njack) i(vabc) i(vlad)\nquit\n.endc\n.end\n")
    r = subprocess.run(["ngspice", "-b", deck], capture_output=True, text=True)
    if not os.path.exists(out):
        raise RuntimeError(r.stdout[-3000:] + r.stderr[-2000:])
    d = np.loadtxt(out)
    t = d[:, 0] - SETTLE
    b, a = signal.butter(6, 18000 / (1 / np.median(np.diff(t)) / 2))
    y = signal.filtfilt(b, a, d[:, 1])
    grid = np.arange(int(sec * FS)) / FS
    return np.interp(grid, t, y), np.interp(grid, t, d[:, 2]), np.interp(grid, t, d[:, 3])


def env_db(y, win=0.03):
    n = int(win * FS)
    p = np.convolve(y * y, np.ones(n) / n, mode="same")
    return 10 * np.log10(p + 1e-20)


def lsd(a, b):
    n = min(4096, len(a))
    f, pa = signal.welch(a, FS, nperseg=n)
    _, pb = signal.welch(b, FS, nperseg=n)
    k = (f > 30) & (f < 12000)
    da, db = 10 * np.log10(pa[k] + 1e-20), 10 * np.log10(pb[k] + 1e-20)
    da, db = da - np.mean(da), db - np.mean(db)
    return float(np.sqrt(np.mean((da - db) ** 2)))


def harmonics_db(o, s, hz):
    """RMS difference of the harmonic levels (dB) over a whole number of periods."""
    n = int(len(o) * hz / FS) * FS / hz
    k = int(n)
    if k < int(2 * FS / hz):
        return float("nan")
    w = np.hanning(k)
    fo, fs_ = np.abs(np.fft.rfft(o[:k] * w)), np.abs(np.fft.rfft(s[:k] * w))
    f = np.fft.rfftfreq(k, 1 / FS)
    harm = np.arange(1, int(10000 / hz) + 1) * hz
    idx = [int(round(h * k / FS)) for h in harm]
    ao = np.array([fo[max(i - 1, 0):i + 2].max() for i in idx])
    as_ = np.array([fs_[max(i - 1, 0):i + 2].max() for i in idx])
    keep = as_ > as_.max() * 10 ** (-50 / 20)
    return float(np.sqrt(np.mean((20 * np.log10(ao[keep] / as_[keep])) ** 2)))


def wave_error(o, s, hz):
    """The waveform difference after shifting SPICE by up to half a VCO period to line up the
    phase, against the signal (dB: -20 is a 10 % difference)."""
    span = int(0.5 * FS / hz)
    best = None
    for lag in range(-span, span + 1):
        a = o[span:len(o) - span]
        b = s[span + lag:len(s) - span + lag]
        e = np.sum((a - b) ** 2)
        if best is None or e < best[0]:
            best = (e, np.sum(a * a))
    return 10 * np.log10(best[0] / best[1])


def dc_step(y, t0, hz, after=0.005):
    """The DC step at the output at t0, mV: the mean over two whole VCO periods from `after`
    seconds past t0, less the mean over the two periods before t0 (whole periods cancel the note)."""
    n = int(round(2 * FS / hz))
    i = int(t0 * FS)
    j = int((t0 + after) * FS)
    return 1e3 * (np.mean(y[j:j + n]) - np.mean(y[max(i - n, 0):i]))


def main():
    args = sys.argv[1:]
    models = args[args.index("--models") + 1].split(",") if "--models" in args else ["datasheet"]
    scns = args[args.index("--scenarios") + 1].split(",") if "--scenarios" in args else list(SCENARIOS)
    keep = args[args.index("--keep") + 1] if "--keep" in args else None
    for m in models:
        for scn in scns:
            knobs, wave, note, notes, sec = SCENARIOS[scn]
            with tempfile.TemporaryDirectory() as tmp:
                o, params = ours(scn, tmp)
                ichg = tune_ichg(MODELS[m], float(params["hz"]), tmp)
                s, iabc, ilad = spice(MODELS[m], scn, params, ichg, tmp)
            o = o[:len(s)]
            if keep:
                import soundfile as sf
                os.makedirs(keep, exist_ok=True)
                sf.write(os.path.join(keep, f"{scn}-{m}-model.wav"), o / 4, FS, subtype="FLOAT")
                sf.write(os.path.join(keep, f"{scn}-{m}-spice.wav"), s / 4, FS, subtype="FLOAT")
            eo, es = env_db(o), env_db(s)
            print(f"\n{scn} ({os.path.basename(MODELS[m])}): knobs {knobs}, {wave}, note {note} ({float(params['hz']):.2f} Hz)"
                  f"   [model / SPICE]")
            print(f"  {'note':12} {'level dB':>15} {'envelope diff':>14} {'wave error':>11} {'harmonics':>10} {'DC step at start mV':>20} {'at end mV':>12}")
            for i, (on, off, acc) in enumerate(notes):
                nxt = notes[i + 1][0] if i + 1 < len(notes) else sec
                w = slice(int(on * FS), int(min(nxt, sec) * FS))
                po, ps = eo[w].max(), es[w].max()
                live = es[w] > ps - 30
                ediff = float(np.mean(np.abs((eo[w] - po) - (es[w] - ps))[live]))
                seg = slice(int(on * FS), int((off + 0.01) * FS))
                steady = slice(int((on + 0.02) * FS), int(off * FS))     # past the attack
                hz = float(params["hz"])
                print(f"  {('accent' if acc else 'plain') + f' {on:.2f}':12} {po:+6.1f}/{ps:+6.1f}   {ediff:10.2f} dB"
                      f"  {wave_error(o[seg], s[seg], hz):7.1f} dB {harmonics_db(o[steady], s[steady], hz):6.2f} dB"
                      f"   {dc_step(o, on, hz):+7.1f}/{dc_step(s, on, hz):+7.1f}"
                      f"   {dc_step(o, off, hz, 0.02):+6.1f}/{dc_step(s, off, hz, 0.02):+6.1f}")


if __name__ == "__main__":
    main()
