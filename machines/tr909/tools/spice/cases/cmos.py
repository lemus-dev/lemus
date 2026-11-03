"""SPICE cases for the solver's MOSFET (core/circuit.*; docs/circuit/solver-mosfet.md): the test
circuits core/netlists/test_inverter.cir, test_ringosc.cir, test_vco.cir and test_nand.cir, not
parts of the TR-909.

Inputs step only on the 96 kHz grid and ramp over whole samples (T96 below), so the solver and
ngspice see the same piecewise-linear input. The oscillators' frequencies against ngspice's are
in the doc (the suite compares waveforms only).
"""

T96 = 1 / 96000


def square(f, seconds, v, edge=8):
    """0/v square of frequency f, its edges on the 96 kHz grid, `edge` samples long (a gate's
    switching inside one sample is a sub-sample event either solver resolves its own way)"""
    pts = [(0, 0.0)]
    k = 1
    while True:
        n = round(k / (2 * f) * 96000)
        t = n * T96
        if t + edge * T96 >= seconds:
            break
        lo, hi = (0.0, v) if k % 2 else (v, 0.0)
        pts += [(t, lo), (t + edge * T96, hi)]
        k += 1
    return pts


def kick(amps, samples=10, dc=False):
    """a current for `samples` samples from the first (or from the DC point, dc): the oscillators'
    way off their unstable DC point"""
    if dc:
        return [(0, amps), (samples * T96, amps), ((samples + 1) * T96, 0.0)]
    return [(0, 0.0), (T96, 0.0), (2 * T96, amps), ((samples + 1) * T96, amps), ((samples + 2) * T96, 0.0)]


def cases(add, h):
    # an inverter's transfer curve (a slow triangle on vin) and its transient into 100 nF || 10k
    for vdd in (5, 10, 15):
        add(f"cmos_inv_dc{vdd}", "test_inverter", 0.2, {"vdd": h.hold(vdd), "vin": [(0, 0), (0.1, vdd), (0.2, 0)]}, t0=0.0)
    for vdd in (5, 15):
        add(f"cmos_inv_sq{vdd}", "test_inverter", 0.02, {"vdd": h.hold(vdd), "vin": square(500, 0.02, vdd)}, t0=0.0)
    # a three-stage ring oscillator (about 300 Hz at 5 V)
    for vdd in (5, 10):
        add(f"cmos_ring{vdd}", "test_ringosc", 0.05, {"vdd": h.hold(vdd), "kick": kick(2e-4)}, t0=0.0)
    # the snare's VCO, at rest and with its CV moving (2 -> 4 V and back)
    add("cmos_vco", "test_vco", 0.05, {"cv": h.hold(2.5), "kick": kick(1e-4, 1, dc=True)}, t0=0.0)
    add("cmos_vco_sweep", "test_vco", 0.05, {"cv": [(0, 2.0), (0.01, 2.0), (0.02, 4.0), (0.04, 2.0)], "kick": kick(1e-4, 1, dc=True)}, t0=0.0)
    # a 4011UB NAND: a's transfer curve with b high, then b's with a high; both switching
    add("cmos_nand_dc", "test_nand", 0.2, {"a": [(0, 0), (0.05, 5), (0.1, 5)], "b": [(0, 5), (0.1, 5), (0.15, 0), (0.2, 5)]}, t0=0.0)
    add("cmos_nand_sq", "test_nand", 0.02, {"a": square(700, 0.02, 5), "b": square(300, 0.02, 5)}, t0=0.0)
