"""SPICE cases for the rim shot and hand clap (core/netlists/rs.cir, hc.cir; docs/circuit/rshc.md).

RS and HC are always played at the accent base 50h: DAC code 21 with TOTAL ACCENT at 0 or the
step unaccented, up to 47 on an accented step at full TOTAL ACCENT (docs/research.md C.4); 63 is
MIDI velocity 127. acc = 5 V * code / 63. The trigger is one 2 ms scan tick.
The clap's noise is a sum of sines around the 4006's 7.5 V mean (a deterministic stand-in for the
LFSR, whose chaos would make every run differ), about the level the shift register's bits have in
IC26b's band.
"""

NOISE = [(613, 0.4, 0.0), (967, 0.4, 1.0), (1301, 0.4, 2.0), (2203, 0.4, 3.0), (4111, 0.3, 4.0)]


def acc(code):
    return 5.0 * code / 63


def cases(add, h):
    tr = h.pulse(5.0, w=2e-3)
    # rim shot
    for code in (21, 47):
        add(f"rs_{code}", "rs", 0.15, {"trig": tr, "acc": h.hold(acc(code))})
    add("rs_10_level1", "rs", 0.15, {"trig": tr, "acc": h.hold(acc(10))}, {"level": 1})
    add("rs_63_level1", "rs", 0.15, {"trig": tr, "acc": h.hold(acc(63))}, {"level": 1})
    add("rs_21_level005", "rs", 0.15, {"trig": tr, "acc": h.hold(acc(21))}, {"level": 0.05})
    # hand clap
    sec = 0.4
    nz = [(t, 7.5 + v) for t, v in h.sines(NOISE, sec, 3 / 96000)]
    add("hc_noise0", "hc", sec, {"trig": tr, "acc": h.hold(acc(21)), "noise": h.hold(7.5)})
    for code in (21, 47):
        add(f"hc_{code}", "hc", sec, {"trig": tr, "acc": h.hold(acc(code)), "noise": nz})
    add("hc_63_level1", "hc", sec, {"trig": tr, "acc": h.hold(acc(63)), "noise": nz}, {"level": 1})
    add("hc_tm0", "hc", sec, {"trig": tr, "acc": h.hold(acc(21)), "noise": nz}, {"tm": 0})
    add("hc_tm1_level01", "hc", sec, {"trig": tr, "acc": h.hold(acc(21)), "noise": nz}, {"tm": 1, "level": 0.1})
