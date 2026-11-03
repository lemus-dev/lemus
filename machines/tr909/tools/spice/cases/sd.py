"""SPICE cases for the snare drum (core/netlists/sd.cir; docs/circuit/sd.md).

acc = 5 V * code / 63: 10 is the firmware's normal level and 21 its accent at TOTAL ACCENT 0, 47
the accent at full TOTAL ACCENT (docs/research.md C.4). The trigger is one 2 ms scan tick.
The noise is a sum of sines around the 4006's 7.5 V mean, in the snappy's band (a deterministic
stand-in for the LFSR, whose chaos would make every run differ).
The VCOs free-run (a free-running VCO has no operating point; the deck starts ngspice from the
solver's state) until the trigger resets them; the comparison starts at the trigger.
"""

NOISE = [(1123, 0.8, 0.0), (2711, 0.8, 1.0), (4507, 0.8, 2.0), (6203, 0.8, 3.0), (8101, 0.6, 4.0),
         (11317, 0.4, 5.0)]


def acc(code):
    return 5.0 * code / 63


def cases(add, h):
    sec = 0.3
    tr = h.pulse(5.0, w=2e-3)
    nz = [(t, 7.5 + v) for t, v in h.sines(NOISE, 0.5, 3 / 96000)]
    t0 = h.T
    add("sd_noise0", "sd", sec, {"trig": tr, "acc": h.hold(acc(10)), "noise": h.hold(7.5)}, t0=t0)
    for code in (10, 21, 47):
        add(f"sd_{code}", "sd", sec, {"trig": tr, "acc": h.hold(acc(code)), "noise": nz}, t0=t0)
    add("sd_tune0", "sd", sec, {"trig": tr, "acc": h.hold(acc(10)), "noise": nz}, {"tune": 0}, t0=t0)
    add("sd_tune1", "sd", sec, {"trig": tr, "acc": h.hold(acc(21)), "noise": nz}, {"tune": 1}, t0=t0)
    add("sd_tone0_snappy1", "sd", sec, {"trig": tr, "acc": h.hold(acc(10)), "noise": nz},
        {"tone": 0, "snappy": 1}, t0=t0)
    add("sd_tone1_snappy1", "sd", 0.5, {"trig": tr, "acc": h.hold(acc(21)), "noise": nz},
        {"tone": 1, "snappy": 1}, t0=t0)
    add("sd_snappy0_level1", "sd", sec, {"trig": tr, "acc": h.hold(acc(47)), "noise": nz},
        {"snappy": 0, "level": 1}, t0=t0)
