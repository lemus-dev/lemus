#!/usr/bin/env python3
"""The voice model's VCA and output stage against a SPICE simulation of the schematic
(tools/spice/out.cir, with the BA662 from tools/spice/ba662.lib).

Usage (from the machine folder, after building tb303blocks):
    ../../.venv/bin/python tools/spice/compare_out.py [--models datasheet,ds-low,ds-high]
           [--ba662 chip,ideal] [--spread] [--only response,volume,law,swing,clip,pin1]

The BA662 is either the chip (ba662.lib's transistor-level circuit, from the reverse-engineered
original and its clones' data sheets) or the ideal OTA the voice model's law is (I_ABC tanh(v/2V_T),
unlimited swing). With the ideal OTA the comparison checks the rest of the circuit; with the
chip it checks the model's BA662. --spread also runs the chip at both ends of its unknowns
(transistor gains, control mirror emitter resistors, buffer pull-up).

  response  small-signal gain from VCF OUT to the output at 30 Hz, 100 Hz and 1 kHz across
            RESONANCE and VOLUME (the input branches' bass roll-off and resonance compensation,
            the output load, VOLUME and the mixer)
  volume    the gain at 1 kHz against the VOLUME knob
  law       the small-signal gain at 1 kHz against I_ABC (the BA662's transconductance)
  swing     a loud 200 Hz tone at I_ABC 170 uA (accent): the BA662's output (pin 8) top and
            bottom about its bias, at three VOLUME settings (VOLUME loads pin 8)
  clip      the same at full VOLUME: the mixer's output top and bottom
  pin1      the voltage at the I_ABC input (pin 1) against I_ABC (the accent current into it
            depends on it: core/voice.cpp, docs/circuit.md 6.4)"""
import os, re, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(ROOT, "..", ".."))
BLOCKS = os.path.join(REPO, "build", "cmake", "tb303blocks")
MODELS = {"datasheet": os.path.join(HERE, "models-datasheet.lib"),
          "ds-low": os.path.join(HERE, "models-datasheet-low.lib"),
          "ds-high": os.path.join(HERE, "models-datasheet-high.lib")}
# The chip's unknowns at both ends: NPN and lateral PNP gain, the control mirror's emitter
# resistors (400 ohm Alfa, ~500 ohm measured by Open Music Labs) and the buffer pull-up (10k
# Alfa and OML, 11k BA6110), each corner taken to its worst together.
CORNERS = {"typ": {}, "low": {"ba_bfn": 80, "ba_bfp": 20, "ba_re": 550, "ba_rpu": "12k"},
           "high": {"ba_bfn": 400, "ba_bfp": 100, "ba_re": 350, "ba_rpu": "8k"}}
FS = 176400
B = 32.11
TESTS = ("response", "volume", "law", "swing", "clip", "pin1")


def taper_a(x):
    return (B ** x - 1) / (B - 1)


def spice(models, chip, body, analysis, outputs, tmp, params, corner=None):
    """Runs out.cir with the given BA662; returns the wrdata table and ngspice's stdout."""
    deck, out = os.path.join(tmp, "deck.cir"), os.path.join(tmp, "spice.txt")
    netlist = open(os.path.join(HERE, "out.cir")).read()
    if chip == "ideal":
        netlist = re.sub(r"^(XIC15 .*) BA662$", r"\1 BA662IDEAL", netlist, flags=re.M)
    p = " ".join(f"{k}={v}" for k, v in params.items())
    c = " ".join(f"{k}={v}" for k, v in (corner or {}).items())
    with open(deck, "w") as fh:
        fh.write(f"* compare_out\n.param {p}\n.include {models}\n.include {os.path.join(HERE, 'ba662.lib')}\n")
        if c:
            fh.write(f".param {c}\n")
        fh.write(f"{netlist}\n{body}\n")
        fh.write(f".control\nset wr_singlescale\noption numdgt=10\n{analysis}\nwrdata {out} {outputs}\nquit\n.endc\n.end\n")
    if os.path.exists(out):
        os.remove(out)
    r = subprocess.run(["ngspice", "-b", deck], capture_output=True, text=True)
    if not os.path.exists(out):
        raise RuntimeError(r.stdout[-2000:] + r.stderr[-2000:])
    return np.loadtxt(out), r.stdout


def ours(args, tmp):
    out = os.path.join(tmp, "ours.txt")
    subprocess.run([BLOCKS, "out"] + [str(a) for a in args] + [out], capture_output=True, check=True)
    return np.loadtxt(out)


def gains_ours(iabc, res, vol, tmp):
    d = ours(["impulse", 0, 1e-3, iabc, res, vol, 2.0, 0.5], tmp)
    H = np.fft.rfft(d[:, 2]) / 1e-3
    f = np.fft.rfftfreq(len(d), 1 / FS)
    return lambda fq: 20 * np.log10(np.abs(np.interp(fq, f, np.abs(H))))


def gain_spice(models, chip, iabc, res, vol, tmp, corner, analysis="ac lin 1 1000 1000"):
    s, _ = spice(models, chip, "Vin nvcf 0 DC 0 AC 1", analysis, "vdb(nout)", tmp,
                 {"iabc": iabc, "kres": res, "tvol": taper_a(vol)}, corner)
    return np.atleast_2d(s)


def tone_spice(models, chip, amp, iabc, vol, tmp, corner, node):
    """A 200 Hz tone; returns the node's swing about its quiescent voltage (from the op point)."""
    s, log = spice(models, chip, f"Vin nvcf 0 SIN(0 {amp} 200)",
                   f"op\necho QUIESCENT $&v({node})\ntran 2u 0.6 0.4 2u", f"v({node})", tmp,
                   {"iabc": iabc, "kres": 0.5, "tvol": taper_a(vol)}, corner)
    q = float(re.search(r"QUIESCENT (\S+)", log).group(1))
    return s[:, 1] - q


def main():
    args = sys.argv[1:]
    models = args[args.index("--models") + 1].split(",") if "--models" in args else ["datasheet"]
    chips = args[args.index("--ba662") + 1].split(",") if "--ba662" in args else ["chip"]
    only = args[args.index("--only") + 1].split(",") if "--only" in args else TESTS
    corners = list(CORNERS) if "--spread" in args else ["typ"]
    for m in models:
        for chip in chips:
            for cn in corners if chip == "chip" else ["typ"]:
                corner = CORNERS[cn]
                label = f"{os.path.basename(MODELS[m])}, BA662 {chip}" + (f" ({cn})" if chip == "chip" else "")
                with tempfile.TemporaryDirectory() as tmp:
                    run(m, chip, corner, label, only, tmp)


def run(m, chip, corner, label, only, tmp):
    lib = MODELS[m]
    if "response" in only:
        print(f"\nVCF OUT to the output, small signal, I_ABC 47.8 uA ({label}): gain dB, model / SPICE")
        print(f"{'res':>4} {'volume':>6} | {'30 Hz':>13} {'100 Hz':>13} {'1 kHz':>13}")
        for res in (0.0, 0.5, 1.0):
            for vol in (0.5, 1.0):
                s = gain_spice(lib, chip, 47.8e-6, res, vol, tmp, corner, "ac dec 40 10 20k")
                go = gains_ours(47.8e-6, res, vol, tmp)
                cells = " ".join(f"{go(fq):+6.1f}/{np.interp(fq, s[:, 0], s[:, 1]):+6.1f}" for fq in (30, 100, 1000))
                print(f"{res:4.1f} {vol:6.2f} | {cells}")
    if "volume" in only:
        print(f"\nVOLUME law at 1 kHz (RESONANCE 0.5), gain dB, model / SPICE ({label})")
        for vol in (0.1, 0.3, 0.5, 0.7, 0.9, 1.0):
            s = gain_spice(lib, chip, 47.8e-6, 0.5, vol, tmp, corner)
            go = gains_ours(47.8e-6, 0.5, vol, tmp)
            print(f"  VOLUME {vol:3.1f}: {go(1000):+6.1f} / {s[0, 1]:+6.1f}")
    if "law" in only:
        print(f"\nGain at 1 kHz against I_ABC (RESONANCE 0.5, VOLUME 0.5), dB ({label})")
        print(f"  {'I_ABC':>8} | {'model':>7} {'SPICE':>7} {'diff':>6}")
        for iabc in (5e-6, 20e-6, 47.8e-6, 100e-6, 170e-6, 300e-6):
            s = gain_spice(lib, chip, iabc, 0.5, 0.5, tmp, corner)
            go = gains_ours(iabc, 0.5, 0.5, tmp)(1000)
            print(f"  {iabc * 1e6:6.1f}uA | {go:+7.2f} {s[0, 1]:+7.2f} {go - s[0, 1]:+6.2f}")
    if "swing" in only:
        print(f"\nBA662 output (pin 8) about its bias, 200 Hz at I_ABC 170 uA, V top / bottom ({label})")
        for vol in (0.3, 0.6, 1.0):
            for amp in (2.0, 4.0, 8.0):
                d = ours(["sine", 200, amp, 170e-6, 0.5, vol, 0.6, 0.4], tmp)
                y = tone_spice(lib, chip, amp, 170e-6, vol, tmp, corner, "np8")
                print(f"  VOLUME {vol:3.1f}, VCF OUT {amp:3.1f} V: model {d[:, 3].max():+5.2f} / {d[:, 3].min():+5.2f}   "
                      f"SPICE {y.max():+5.2f} / {y.min():+5.2f}")
    if "clip" in only:
        print(f"\nMixer output, 200 Hz at I_ABC 170 uA, full VOLUME, V top / bottom about its mean ({label})")
        for amp in (0.5, 1.0, 2.0):
            d = ours(["sine", 200, amp, 170e-6, 0.5, 1.0, 0.6, 0.4], tmp)
            s, _ = spice(lib, chip, f"Vin nvcf 0 SIN(0 {amp} 200)", "tran 2u 0.6 0.4 2u", "v(nout)", tmp,
                         {"iabc": 170e-6, "kres": 0.5, "tvol": taper_a(1.0)}, corner)
            y = s[:, 1] - np.mean(s[:, 1])
            print(f"  VCF OUT {amp:3.1f} V: model {d[:, 2].max():+5.2f} / {d[:, 2].min():+5.2f}   "
                  f"SPICE {y.max():+5.2f} / {y.min():+5.2f}")
    if "pin1" in only:
        print(f"\nPin 1 (I_ABC input) voltage, V ({label}); the model: V_T ln(1 + I/5e-16) + 450 I")
        for iabc in (5e-6, 20e-6, 47.8e-6, 100e-6, 170e-6, 300e-6):
            s, log = spice(lib, chip, "Vin nvcf 0 DC 0", "op\necho PIN1 $&v(npin1)", "v(npin1)",
                           tmp, {"iabc": iabc, "kres": 0.5, "tvol": 0.5}, corner)
            v1 = float(re.search(r"PIN1 (\S+)", log).group(1))
            vm = 0.025865 * np.log1p(iabc / 5e-16) + 450 * iabc
            print(f"  {iabc * 1e6:6.1f}uA: model {vm:.3f}  SPICE {v1:.3f}")


if __name__ == "__main__":
    main()
