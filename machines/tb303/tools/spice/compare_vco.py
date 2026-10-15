#!/usr/bin/env python3
"""The voice model's VCO against a SPICE simulation of the schematic (tools/spice/vco.cir).

Usage (from the machine folder, after building tb303blocks):
    ../../.venv/bin/python tools/spice/compare_vco.py [--notes C1,C3,C5] [--models datasheet,community]
           [--plots DIR]

Two comparisons at each note, with each device-model set (the data-sheet set at typical values and
at both ends of the parts' spread, and the community models):
  shaper  the Q8 square shaper alone, both sides driven by the same ideal saw (the model's)
  vco     the whole VCO: SPICE's saw core, reset and Q28 buffer against the model's ideal saw
For each it prints the saw's and the square's levels, the square's duty cycle, and the square's
level against the simulation: overall (AC RMS), at the fundamental, and over harmonics 8-16, the
part of the spectrum where the model's square is short of Roland's at high notes."""
import os, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(ROOT, "..", ".."))
BLOCKS = os.path.join(REPO, "build", "cmake", "tb303blocks")
MODELS = {"datasheet": os.path.join(HERE, "models-datasheet.lib"),
          "ds-low": os.path.join(HERE, "models-datasheet-low.lib"),
          "ds-high": os.path.join(HERE, "models-datasheet-high.lib"),
          "community": os.path.join(ROOT, "private", "reference", "spice", "models-community.lib")}
NOTES = {"C1": 32.703, "C2": 65.406, "C3": 130.81, "C4": 261.63, "C5": 523.25}
TSTOP, TFROM = 0.6, 0.4   # seconds simulated; analysis window starts here
RATE = 352800             # the model's VCO rate; SPICE output is resampled to it
TRESET = 1e-6             # the model's reset time (core/voice.cpp kResetSec)


def run_ours(f, tmp):
    out = os.path.join(tmp, "ours.txt")
    r = subprocess.run([BLOCKS, "vco", str(f), str(TSTOP), str(TFROM), out], capture_output=True, text=True, check=True)
    c17 = float(r.stdout.split()[1])
    d = np.loadtxt(out)
    return d[:, 1], d[:, 2], c17


def run_spice(models, f, ideal, c17, tmp, ichg=None):
    ichg = ichg if ichg is not None else f * 0.01e-6 * 5.3
    deck = os.path.join(tmp, "deck.cir")
    out = os.path.join(tmp, "spice.txt")
    with open(deck, "w") as fh:
        fh.write(f"* compare_vco\n.param ichg={ichg:.6e} ideal={int(ideal)} f={f} treset={TRESET} c17ic={c17:.4f}\n")
        fh.write(f".include {models}\n.include {os.path.join(HERE, 'vco.cir')}\n")
        fh.write(".control\nset wr_singlescale\noption numdgt=10\n")
        fh.write(f"tran 0.5u {TSTOP} {TFROM} 0.5u uic\nwrdata {out} v(nsrc) v(nsq)\nquit\n.endc\n.end\n")
    subprocess.run(["ngspice", "-b", deck], capture_output=True, check=True)
    d = np.loadtxt(out)
    t = d[:, 0]
    grid = TFROM + np.arange(int((TSTOP - TFROM) * RATE) - 1) / RATE
    return np.interp(grid, t, d[:, 1]), np.interp(grid, t, d[:, 2])


def period_of(saw):
    """Frequency from the saw's resets (big upward steps)."""
    jumps = np.where(np.diff(saw) > 1.5)[0]
    jumps = jumps[np.insert(np.diff(jumps) > 2, 0, True)]   # a reset split over two samples counts once
    return np.median(np.diff(jumps)) / RATE, jumps


def analyse(saw, sq, f):
    per, jumps = period_of(saw)
    n = int(round(per * RATE))
    k = max(1, (len(saw) - jumps[0] - 1) // n)       # whole periods from the first reset
    a, b = jumps[0] + 1, jumps[0] + 1 + k * n
    s, q = saw[a:b], sq[a:b]
    spec = np.abs(np.fft.rfft(q - q.mean())) / (len(q) / 2)
    h = spec[k * np.arange(1, 17)]                    # harmonics 1..16
    hi = np.percentile(q, 95)
    lo = np.percentile(q, 5)
    return {"freq": 1 / per, "saw_top": s.max(), "saw_bot": s.min(), "sq_hi": hi, "sq_lo": lo,
            "duty": np.mean(q > (hi + lo) / 2), "rms": q.std(), "h": h}


def db(x, y):
    return 20 * np.log10(x / y)


def main():
    args = sys.argv[1:]
    notes = args[args.index("--notes") + 1].split(",") if "--notes" in args else list(NOTES)
    models = args[args.index("--models") + 1].split(",") if "--models" in args else list(MODELS)
    models = [m for m in models if os.path.exists(MODELS[m])]   # the community set is private
    print(f"{'note':4} {'test':6} {'models':9} | {'saw top/bottom (V)':22} | {'square high/low (V)':22} | {'duty':11} | "
          f"square, ours - SPICE: {'RMS':>6} {'h1':>6} {'h8-16':>6}")
    for note in notes:
        f = NOTES[note]
        with tempfile.TemporaryDirectory() as tmp:
            saw_o, sq_o, c17 = run_ours(f, tmp)
            ours = analyse(saw_o, sq_o, f)
            for test in ("shaper", "vco"):
                for m in models:
                    if test == "shaper":
                        saw_s, sq_s = run_spice(MODELS[m], f, True, c17, tmp)
                    else:
                        # Set the expo current so SPICE's core runs at the note (two tries).
                        ichg = f * 0.01e-6 * 5.3
                        for _ in range(2):
                            saw_s, sq_s = run_spice(MODELS[m], f, False, c17, tmp, ichg)
                            if len(period_of(saw_s)[1]) < 3:
                                break
                            ichg *= f / analyse(saw_s, sq_s, f)["freq"]
                        if len(period_of(saw_s)[1]) < 3:
                            print(f"{note:4} {test:6} {m:9} | the saw core doesn't reset with these models "
                                  f"(saw {saw_s.min():.2f}..{saw_s.max():.2f} V): no comparison")
                            continue
                    sp = analyse(saw_s, sq_s, f)
                    hf = np.sqrt(np.sum(ours["h"][7:] ** 2)) / np.sqrt(np.sum(sp["h"][7:] ** 2))
                    print(f"{note:4} {test:6} {m:9} | ours {ours['saw_top']:5.2f}/{ours['saw_bot']:5.2f} "
                          f"sp {sp['saw_top']:5.2f}/{sp['saw_bot']:5.2f} | ours {ours['sq_hi']:5.2f}/{ours['sq_lo']:5.2f} "
                          f"sp {sp['sq_hi']:5.2f}/{sp['sq_lo']:5.2f} | {ours['duty']:4.2f}/{sp['duty']:4.2f} | "
                          f"{'':22}{db(ours['rms'], sp['rms']):+6.2f} {db(ours['h'][0], sp['h'][0]):+6.2f} {20 * np.log10(hf):+6.2f}")


if __name__ == "__main__":
    main()
