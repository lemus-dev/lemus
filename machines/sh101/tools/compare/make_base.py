#!/usr/bin/env python3
"""Builds private/reference/roland-vst/base.otk: an Onyx Tracker song with Roland's SH-101 plug-in on
instrument 02 in the hardware-equivalent baseline that roland_render.py opens before every render.

Usage: make_base.py [--out FILE]   (Onyx Tracker must be running; this replaces its open song)

Baseline (raw values; roland_render.BASELINE):
  EFFECTS: CRUSHER, REVERB and DELAY levels 0 (DELAY TIME 0); VCA TONE 128 (centre = 0)
  TEMPO SYNC, ARPEGGIO, KEY HOLD off; SCATTER TYPE 1 (raw 0), DEPTH 0; OCTAVE 0
  MASTER TUNE raw 10 = 440 Hz; PATCH LEVEL (= Roland's VOLUME knob) 128
  Both ADSRs set alike from our single envelope; the rest is our plugin's defaults, mapped.
CONDITION and CIRCUIT MOD AMOUNT are not VST3 parameters and the plug-in leaves them out of its
saved state, so they cannot be set from here: every instance gets them from the patch it starts
with ("LD the real 101", raw 0 for both in the factory bank). See docs/roland-measurements.md.

Song: 120 BPM, 4 lines/beat, 6 ticks/line, 16 tracks, master 1.0, desk as the user's preference
(the CR1604 is transparent at its defaults: unity trim/fader, flat EQ, centre pan = 0 dB).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import roland_render as R


def main():
    out = R.BASE
    if "--out" in sys.argv:
        out = os.path.abspath(sys.argv[sys.argv.index("--out") + 1])
    R.otk("new", "--force")
    R.otk("load", R.INST, "SH-101")
    try:
        R.otk("unload", "01")
    except RuntimeError:
        pass
    for k, v in (("tempo", 120), ("lpb", 4), ("tpl", 6), ("tracks", R.TRACKS), ("volume", 1)):
        R.otk("set", k, str(v))
    R.otk("instrument", R.INST, "velocity=100", "transpose=0", "level=0")
    raw, *_ = R.roland_params({"volume": 0.5})
    args = []
    for name, v in raw.items():
        r, val = R.vst_value(name, v)
        args += [name, f"{val:.6f}"]
    R.otk("param", R.INST, *args)
    R.otk("block", "lines", "00", "16")
    R.otk("clear", "00", "--tracks", f"1-{R.TRACKS}")
    R.otk("save", out)
    # read back
    got = {}
    for line in R.otk("params", R.INST).splitlines():   # "  13  VCF FREQ   = 89  (0.3490)"
        if "=" in line and "(" in line:
            name = line.split("=")[0].split(None, 1)[1].strip()
            got[name] = float(line.rsplit("(", 1)[1].rstrip(") "))
    bad = [(n, R.vst_value(n, v)[1], got.get(n)) for n, v in raw.items()
           if abs(R.vst_value(n, v)[1] - got.get(n, -1)) > 0.003]
    print(out)
    print("mismatches:", bad or "none")


if __name__ == "__main__":
    main()
