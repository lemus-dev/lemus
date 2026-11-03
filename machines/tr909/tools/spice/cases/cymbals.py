"""SPICE cases for the crash and ride (core/netlists/cr.cir, rd.cir; their clocks crclk.cir,
rdclk.cir; docs/circuit/cymbals.md).

The analog halves take Cymbal's lines (core/cymbals.*): the latch's six outputs d0..d5, the
counter's bits 9-14 e0..e5 and RUN. Here they come from the ROM (private/roms/tr909, or a
pseudo-random stand-in without it) at a steady sample clock of 35 kHz, each line's steps placed at
their own times and averaged over each 96 kHz step, as Cymbal's Box smoothing gives them; the
points fall on the 96 kHz grid, so the solver and ngspice see the same piecewise-linear input. The
accent DAC plays code 10 (normal) or 21 (accented) at TOTAL ACCENT 0, 47 at full (docs/research.md
C.4); acc = 5 V * code / 63. Cases run 20 ms from the trigger, past the envelope D/A's first step
(sample 512); cr_end plays the counter's last 1024 samples, the envelope's last steps (codes 62,
63, where IC52b's current falls to nothing) and the stop (RUN falling, R427's mute, the envelope
D/A back to 0).

Run as a script, it makes the two measurements the suite can't (docs/circuit/cymbals.md, "Rates"):
    cymbals.py clock   the clock netlists' period against ngspice's at fine steps (TUNE 0, 0.5, 1;
                       R442 10k and 33k), with Cymbal's own timing (tr909cyrun --timing)
    cymbals.py steps [--rate HZ] [--smoothing sinc|box] [--voice cr|rd] [--seconds S]
                       the solver on cr.cir (whole) with Cymbal's lines against ngspice on the
                       same netlist with every D/A step at its exact time (10 ns edges), from
                       tr909cyrun's event log; the difference by band
CIRCUITRUN and CYRUN name the tools (default build/tr909/tr909circuitrun, tr909cyrun).
"""
import os
import random
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "../../../../.."))
NET = os.path.join(REPO, "machines/tr909/core/netlists")
ROMS = os.path.normpath(os.path.join(REPO, "../private/roms/tr909"))
RATE = 96000
FS = 35000.0  # the cases' sample clock
T = 0.01      # trigger time
ROMFILE = {"cr": "hn61256p__c42.ic62", "rd": "hn61256p__c44.ic54"}
OTHER = {"cr": "rd.run", "rd": "cr.run"}


def acc(code):
    return 5.0 * code / 63


def rom(voice):
    try:
        with open(os.path.join(ROMS, ROMFILE[voice]), "rb") as f:
            data = f.read()
        if len(data) == 0x8000:
            return data
    except OSError:
        pass
    rng = random.Random(1 if voice == "cr" else 2)  # a stand-in: noise round code 32
    return bytes(max(0, min(63, int(rng.gauss(32, 8)))) << 2 for _ in range(0x8000))


def events(voice, t0, first, last, fs=FS):
    """The lines' changes as Cymbal makes them at a steady clock: latch k (counter value first + k)
    at t0 + (k + 1.5) / fs, with the counter stepping to first + k + 1 at the same instant; RUN
    rises at t0 (first = 0: the trigger) and falls when the counter reaches 8000."""
    data = rom(voice)
    ev = []  # (t, line, level)
    code = data[0x7FFF] >> 2
    count = first
    if first == 0:
        ev.append((t0, "run", 5.0))
    for k in range(last - first):
        t = t0 + (k + 1.5) / fs
        new = data[count & 0x7FFF] >> 2
        for b in range(6):
            if ((new ^ code) >> b) & 1:
                ev.append((t, f"d{b}", 5.0 * ((new >> b) & 1)))
        code = new
        old, count = count, count + 1
        for b in range(6):
            if ((old ^ count) >> (9 + b)) & 1:
                ev.append((t, f"e{b}", 5.0 * ((count >> (9 + b)) & 1)))
        if count & 0x8000:
            ev.append((t, "run", 0.0))
            break
    return ev, code


def boxed(ev, init, t_end, rate=RATE):
    """Each line as PWL points on the rate's grid: its average over each step (Cymbal's Box)."""
    h = 1.0 / rate
    lines = {k: [(0.0, v)] for k, v in init.items()}
    level = dict(init)
    for t, name, v in ev:
        n = int(t / h) + 1  # the step [n-1, n] holds the edge
        a = level[name]
        frac = n - t / h    # its share after the edge
        pts = lines[name]
        while pts and pts[-1][0] > (n - 1) * h + 1e-12:
            pts.pop()  # an edge in the same or the next step: start from what was there
        if pts[-1][0] < (n - 1) * h - 1e-12:
            pts.append(((n - 1) * h, a))
        pts.append((n * h, a + (v - a) * frac))
        pts.append(((n + 1) * h, v))
        level[name] = v
    for k in lines:
        lines[k].append((t_end, level[k]))
    return lines


def case_inputs(voice, code_acc, t0=T, seconds=0.02, first=0, other_run=False):
    data = rom(voice)
    init = {"run": 0.0 if first == 0 else 5.0}
    latch = data[(first - 1) & 0x7FFF] >> 2 if first else data[0x7FFF] >> 2
    for b in range(6):
        init[f"d{b}"] = 5.0 * ((latch >> b) & 1)
        init[f"e{b}"] = 5.0 * ((first >> (9 + b)) & 1)
    if first == 0:  # the trigger's reset clears the counter: the envelope D/A to 0
        pass
    last = min(0x8000, first + int(seconds * FS) + 2)
    ev, _ = events(voice, t0, first, last)
    inputs = boxed(ev, init, t0 + seconds + 0.01)
    inputs["acc"] = [(0.0, acc(code_acc))]
    inputs[OTHER[voice]] = [(0.0, 5.0 if other_run else 0.0)]
    return inputs


def cases(add, h):
    for v in ("cr", "rd"):
        add(f"{v}_10", v, T + 0.02, case_inputs(v, 10), t0=T)
        add(f"{v}_21", v, T + 0.02, case_inputs(v, 21), t0=T)
        add(f"{v}_47_level1", v, T + 0.02, case_inputs(v, 47), {"level": 1}, t0=T)
        add(f"{v}_10_other", v, T + 0.02, case_inputs(v, 10, other_run=True), {"level": 0.2}, t0=T)
    # the counter's last 1024 samples and the stop, at full accent and LEVEL
    add("cr_end", "cr", T + 0.04, case_inputs("cr", 47, seconds=0.04, first=0x7C00), {"level": 1}, t0=T)


# --- the measurements ----------------------------------------------------------------------------

def tool(env, name):
    return os.environ.get(env, os.path.join(REPO, "build/tr909", name))


def spice_rising(t, v, th=2.5):
    import numpy as np
    i = np.where((v[:-1] < th) & (v[1:] >= th))[0]
    return t[i] + (th - v[i]) / (v[i + 1] - v[i]) * (t[i + 1] - t[i])


def run_spice(deck, out_dir, tstep, tstop=None, extra="", method="trap", options=None):
    """Runs a circuitrun deck with ngspice at a fine step (and without the export's node shunts:
    1 pF at each node moves the clock's period 1.2 %): its data as an array. method=gear for the hits with
    exact steps, where ngspice's trapezoidal rule gives up on a step 37 ms in ("timestep too
    small" at the first filter)."""
    import numpy as np
    d = open(deck).read()
    tr = re.search(r"\.tran .*", d).group(0)
    tstop = tstop or tr.split()[2]
    d = d.replace(tr, f".tran {tstep} {tstop} 0 {tstep}")
    d = re.sub(r" (cshunt|rshunt)=\S+", "", d).replace("method=trap", "method=" + method)
    if options:
        d = re.sub(r"^\.options .*$", ".options " + options, d, flags=re.M)
    data = os.path.join(out_dir, "spice.txt")
    d = re.sub(r"wrdata \S+", "wrdata " + data, d)
    if extra:
        d = d.replace(".control", extra + "\n.control")
    path = os.path.join(out_dir, "deck_fine.cir")
    open(path, "w").write(d)
    subprocess.run(["ngspice", "-b", path], capture_output=True, text=True, cwd=out_dir)
    return np.loadtxt(data, skiprows=1)


def clock(out):
    import numpy as np
    cr = tool("CIRCUITRUN", "tr909circuitrun")
    cy = tool("CYRUN", "tr909cyrun")
    print("clock period, us: ngspice (2 ns steps, no node shunts) / the solver at 100 MHz (Cymbal's timing)")
    for voice, rs in (("cr", "r442"), ("rd", "r440")):
        for r in ("10k", "33k"):
            res = subprocess.run([cy, "--voice", voice, "--timing", "--seconds", "0.001", "--param", f"{rs}={r}"],
                                 capture_output=True, text=True).stdout.split("\n")
            table = {}
            for line in res[1:]:
                f = line.split()
                if len(f) > 3:
                    table[round(float(f[0]), 2)] = float(f[2]) + float(f[3])
            for tune in (0.0, 0.5, 1.0):
                work = os.path.join(out, f"clock_{voice}_{r}_{tune}")
                os.makedirs(work, exist_ok=True)
                args = [cr, f"models.cir,{voice}clk.cir", "--rate", "50e6", "--seconds", "1.5e-4",
                        "--control", f"tune={tune}", "--param", f"{rs}={r}",
                        "--input", "trig=0,0,1e-6,0,1.0001e-6,5", "--input", "run=0,0,1e-6,0,1.0001e-6,5",
                        "--out", os.path.join(work, "ours.csv"), "--spice", os.path.join(work, "deck.cir"),
                        os.path.join(work, "spice.txt")]
                subprocess.run(args, cwd=NET, capture_output=True)
                s = run_spice(os.path.join(work, "deck.cir"), work, "2n")
                ta = spice_rising(s[:, 0], s[:, 1])
                ps = float(np.mean(np.diff(ta[-5:])))
                po = table.get(tune, float("nan")) * 1e-6
                print(f"  {voice} {rs} {r:4s} TUNE {tune:.1f}: ngspice {ps * 1e6:8.4f}  solver {po * 1e6:8.4f}"
                      f"  ({100 * (po / ps - 1):+.3f} %)  sample clock {0.5 / ps:8.1f} Hz")


def steps(out, rate, smoothing, voice, seconds):
    import numpy as np
    cr = tool("CIRCUITRUN", "tr909circuitrun")
    cy = tool("CYRUN", "tr909cyrun")
    work = os.path.join(out, f"steps_{voice}_{int(rate)}_{smoothing}")
    os.makedirs(work, exist_ok=True)
    ev_path, ours_path = os.path.join(work, "events.csv"), os.path.join(work, "ours.csv")
    r = subprocess.run([cy, "--voice", voice, "--whole", "--rate", str(rate), "--smoothing", smoothing, "--acc", "47",
                        "--level", "1", "--seconds", str(seconds), "--events", ev_path, "--csv", ours_path],
                       capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    lat = 8.0 if smoothing == "sinc" else 0.5
    # ngspice: the same netlist, each line a PWL with its steps at their exact times (10 ns edges)
    ev = np.genfromtxt(ev_path, delimiter=",", names=True)
    data = rom(voice)
    t_end = seconds
    lines = {f"d{b}": [] for b in range(6)}
    lines.update({f"e{b}": [] for b in range(6)})
    lines["run"] = []
    code0 = data[0x7FFF] >> 2
    level = {f"d{b}": 5.0 * ((code0 >> b) & 1) for b in range(6)}
    level.update({f"e{b}": 0.0 for b in range(6)})
    level["run"] = 0.0
    for k in level:
        lines[k].append((0.0, level[k]))
    for e in ev:
        t = float(e["time"])
        new = {f"d{b}": 5.0 * ((int(e["code"]) >> b) & 1) for b in range(6)}
        new.update({f"e{b}": 5.0 * ((int(e["envelope"]) >> b) & 1) for b in range(6)})
        new["run"] = 5.0 * int(e["run"])
        for k, v in new.items():
            if v != level[k]:
                lines[k] += [(t, level[k]), (t + 1e-8, v)]
                level[k] = v
    deck = os.path.join(work, "deck.cir")
    args = [cr, f"models.cir,{voice}.cir", "--rate", str(rate), "--seconds", str(t_end), "--control", "level=1",
            "--input", f"acc=0,{acc(47)}", "--input", f"{OTHER[voice]}=0,0", "--out", os.path.join(work, "dummy.csv"),
            "--spice", deck, os.path.join(work, "spice.txt")]
    subprocess.run(args, cwd=NET, capture_output=True)
    d = open(deck).read()
    for k, pts in lines.items():
        pwl = " ".join(f"{t:.12g} {v:.6g}" for t, v in pts)
        d = re.sub(rf"^(v{k} \S+ 0) .*$", lambda m: m.group(1) + " PWL( " + pwl + " )", d, flags=re.M)
    open(deck, "w").write(d)
    s = run_spice(deck, work, "0.25u", method="gear",
                  options="reltol=1e-5 abstol=1e-12 vntol=1e-7 gmin=1e-12 method=gear")
    if s[-1, 0] < t_end * 0.99:
        print(f"ngspice stopped at {s[-1, 0] * 1e3:.1f} ms")
        t_end = s[-1, 0]
    names = open(deck).read().split("wrdata")[1].split("\n")[0].split()[1:]
    o = np.genfromtxt(ours_path, delimiter=",", names=True)
    ts, js = s[:, 0], s[:, 1 + names.index("v(n_jack)")]
    t = o["time"] - lat / rate
    y = o["jack"]
    m = (t > 1.2e-3) & (t < t_end - 1e-3)
    ref = np.interp(t[m], ts, js)
    d = y[m] - ref
    rms = np.sqrt(np.mean(d * d)) / np.sqrt(np.mean(ref * ref))
    print(f"{voice} at {rate:.0f} Hz, {smoothing}: jack, solver against ngspice with exact steps: "
          f"rms difference {100 * rms:.2f} % of the signal's rms")
    # by band: the difference's spectrum against the signal's (Welch)
    from scipy.signal import welch
    seg = min(4096, int(m.sum()))
    f, pr = welch(ref, fs=rate, nperseg=seg)
    _, pd = welch(d, fs=rate, nperseg=seg)
    _, po = welch(y[m], fs=rate, nperseg=seg)
    for a, b in ((20, 2000), (2000, 5000), (5000, 10000), (10000, 15000), (15000, 20000), (20000, 30000)):
        k = (f >= a) & (f < b)
        if k.any():
            print(f"  {a / 1000:4.0f}-{b / 1000:2.0f} kHz: level {10 * np.log10(po[k].sum() / pr[k].sum()):+6.2f} dB,"
                  f" difference {10 * np.log10(pd[k].sum() / pr[k].sum()):6.1f} dB under the signal there"
                  f" (signal {10 * np.log10(pr[k].sum() / pr.sum()):6.1f} dB of the total)")


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["clock", "steps"])
    ap.add_argument("--out", default=os.path.join(REPO, "build", "tr909", "spice", "cymbals"))
    ap.add_argument("--rate", type=float, default=192000)
    ap.add_argument("--smoothing", default="sinc")
    ap.add_argument("--voice", default="cr")
    ap.add_argument("--seconds", type=float, default=0.03)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    if a.what == "clock":
        clock(a.out)
    else:
        steps(a.out, a.rate, a.smoothing, a.voice, a.seconds)


if __name__ == "__main__":
    main()
