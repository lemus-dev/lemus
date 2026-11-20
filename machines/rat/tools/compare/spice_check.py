#!/usr/bin/env python3
"""Check the DK solver against ngspice on the same circuit.

`ratrender --netlist` prints the model as an ngspice netlist with its devices as behavioural
sources, so any difference here is the solver (time step, trapezoidal rule, Newton), not the
device models. Each case drives both with the same signal at 384 kHz and compares the output
jack voltage (and the op amp output) after the bias has settled.

Usage: tools/compare/spice_check.py [--quick]   (run with the repo venv: ../../.venv/bin/python)
Writes build/spice_check/<case>.png and prints a table.
"""
import concurrent.futures as cf
import os
import pathlib
import subprocess
import sys
import tempfile

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
RATRENDER = pathlib.Path(os.environ.get("RATRENDER", ROOT.parents[1] / "build/rat/ratrender"))  # RATRENDER: another build
OUT = ROOT / "build/spice_check"
RATE = 384000.0
SETTLE = 0.02  # seconds of silence before the signal (both start from their DC point)


def signal(kind, amp, dur):
    t = np.arange(int(dur * RATE)) / RATE
    if kind == "sine1k":
        x = amp * np.sin(2 * np.pi * 1000 * t)
    elif kind == "sine100":
        x = amp * np.sin(2 * np.pi * 100 * t)
    elif kind == "sweep":
        f0, f1 = 50.0, 12000.0
        k = np.log(f1 / f0) / dur
        x = amp * np.sin(2 * np.pi * f0 * (np.exp(k * t) - 1) / k)
    elif kind == "burst":
        # 2 kHz tone burst with a fast attack: exercises slewing and diode recovery.
        env = np.clip((t % 0.02) < 0.01, 0, 1) * 1.0
        x = amp * env * np.sin(2 * np.pi * 2000 * t)
    else:
        raise ValueError(kind)
    fade = np.minimum(1, t / 0.002)
    x = x * fade
    return np.concatenate([np.zeros(int(SETTLE * RATE)), x])


def run_case(case):
    name, kind, amp, dist, filt, vol, dur = case
    x = signal(kind, amp, dur)
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        # Our model, at 384 kHz without the oversampler.
        import soundfile as sf
        sf.write(td / "in.wav", x.astype(np.float32), int(RATE), subtype="FLOAT")
        knobs = ["--dist", str(dist), "--filter", str(filt), "--volume", str(vol)]
        subprocess.run([str(RATRENDER), str(td / "in.wav"), str(td / "ours.wav"), "--direct", *knobs],
                       check=True)
        subprocess.run([str(RATRENDER), str(td / "in.wav"), str(td / "ours_o.wav"), "--direct", "--probe",
                        "opout", *knobs], check=True)
        ours, _ = sf.read(td / "ours.wav")
        ours_o, _ = sf.read(td / "ours_o.wav")
        # The float WAV holds the input to 24-bit precision: SPICE gets the same samples.
        x32, _ = sf.read(td / "in.wav")
        tt = np.arange(len(x32)) / RATE
        pwl = td / "in.pwl"
        with open(pwl, "w") as f:
            for a, b in zip(tt, x32):
                f.write(f"{a:.9e} {b:.9e}\n")
        net = subprocess.run([str(RATRENDER), "--netlist", "DC 0", *knobs], check=True,
                             capture_output=True, text=True).stdout
        # Drive the input from the sample file through XSPICE's filesource.
        net = net.replace("Vsrc src 0 DC 0", f"""A_in %vd([src 0]) insrc
.model insrc filesource (file="{pwl}" amploffset=[0] amplscale=[1] timeoffset=0 timescale=1 timerelative=false amplstep=false)""")
        step = 1 / RATE
        net += f"""
.options reltol=1e-6 abstol=1e-12 vntol=1e-9 method=trap
.control
set filetype=ascii
tran {step:.9e} {len(x32) / RATE:.9e} 0 {step:.9e}
linearize v(w) v(o)
wrdata {td / 'spice.txt'} v(w) v(o)
.endc
.end
"""
        (td / "rat.cir").write_text(net)
        r = subprocess.run(["ngspice", "-b", str(td / "rat.cir")], capture_output=True, text=True)
        if r.returncode != 0 or not (td / "spice.txt").exists():
            return name, None, r.stdout[-2000:] + r.stderr[-2000:]
        d = np.loadtxt(td / "spice.txt")
    ts, sw, so = d[:, 0], d[:, 1], d[:, 3]
    n = min(len(ours), len(ts))
    t_ours = np.arange(len(ours)) / RATE
    spice_w = np.interp(t_ours[:n], ts, sw)
    spice_o = np.interp(t_ours[:n], ts, so)
    a = int(SETTLE * RATE)
    ew = ours[a:n] - spice_w[a:]
    eo = ours_o[a:n] - spice_o[a:]
    rms = lambda v: float(np.sqrt(np.mean(v * v)) + 1e-30)
    res = {
        "out_rms": rms(spice_w[a:]),
        "out_err_db": 20 * np.log10(rms(ew) / rms(spice_w[a:] - spice_w[a:].mean())),
        "out_max": float(np.max(np.abs(ew))),
        "op_err_db": 20 * np.log10(rms(eo) / rms(spice_o[a:] - spice_o[a:].mean())),
    }
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        OUT.mkdir(parents=True, exist_ok=True)
        seg = slice(a + int(0.01 * RATE), a + int(0.01 * RATE) + int(0.004 * RATE))
        fig, ax = plt.subplots(2, 1, figsize=(10, 6), sharex=True)
        tt = t_ours[seg] * 1000
        ax[0].plot(tt, spice_o[seg], label="ngspice")
        ax[0].plot(tt, ours_o[seg], "--", label="DK")
        ax[0].set_ylabel("op amp out (V)")
        ax[0].legend()
        ax[1].plot(tt, spice_w[seg], label="ngspice")
        ax[1].plot(tt, ours[seg], "--", label="DK")
        ax[1].set_ylabel("output jack (V)")
        ax[1].set_xlabel("ms")
        fig.suptitle(name)
        fig.savefig(OUT / f"{name}.png", dpi=90)
        plt.close(fig)
    except Exception as e:  # plotting is optional
        print("plot failed:", e)
    return name, res, None


CASES = [
    # name, signal, amplitude (V), dist, filter, volume, duration (s)
    ("sine1k-small-d0", "sine1k", 0.05, 0.0, 0.5, 0.5, 0.05),
    ("sine1k-small-d05", "sine1k", 0.05, 0.5, 0.5, 0.5, 0.05),
    ("sine1k-1V-d1-f0", "sine1k", 1.0, 1.0, 0.0, 0.5, 0.05),
    ("sine1k-1V-d1-f1", "sine1k", 1.0, 1.0, 1.0, 0.5, 0.05),
    ("sine100-03V-d05", "sine100", 0.3, 0.5, 0.3, 0.7, 0.08),
    ("sweep-03V-d07", "sweep", 0.3, 0.7, 0.2, 0.5, 0.15),
    ("burst-05V-d1", "burst", 0.5, 1.0, 0.0, 0.5, 0.08),
]


def main():
    cases = CASES[:3] if "--quick" in sys.argv else CASES
    with cf.ProcessPoolExecutor() as ex:
        results = list(ex.map(run_case, cases))
    print(f"{'case':22s} {'out rms V':>10s} {'out err dB':>11s} {'max err V':>10s} {'op err dB':>10s}")
    for name, res, err in results:
        if res is None:
            print(f"{name:22s} ngspice failed:\n{err}")
            continue
        print(f"{name:22s} {res['out_rms']:10.4f} {res['out_err_db']:11.1f} {res['out_max']:10.2e} {res['op_err_db']:10.1f}")


if __name__ == "__main__":
    main()
