"""SPICE cases for the low, mid and hi tom and their shared tom noise (core/netlists/lt.cir, mt.cir,
ht.cir, tomnoise.cir; docs/circuit/toms.md).

The analog voices play DAC code 10 (normal) or 21 (accented) at TOTAL ACCENT 0, up to 21 and 47
at full TOTAL ACCENT; 63 is MIDI velocity 127 (docs/research.md C.4). acc = 5 V * code / 63. The
trigger is one 2 ms scan tick.

The three VCOs of each tom free-run and have no DC operating point, so neither solver starts
settled. Line A resets them for the trigger, but a reset from a low triangle (the reset
transistor then runs inverted, at beta_R) takes up to 3.5 ms and is not over when a 2 ms trigger
ends: how a hit starts depends on where the VCOs were (on the unit too). So each case first holds
the trigger for 10 ms at DAC code 0 (G then reaches only 0.6 V, so the envelopes stay under
0.1 V), which leaves both solvers' VCOs reset alike; the hit follows 5 ms later, and the cases are
compared from its trigger's end. A tom takes the tom-noise bus as its input tomnoise.tn: here a
1 kHz sine of its size, so VCO3's VCA path is checked without the noise's randomness
(tomnoise.cir has its own cases).

The noise is the 909's LFSR (x^31 + x^13 + 1 from all ones, docs/research.md A2.1) clocked at
455 kHz, 0/15 V, averaged over each 96 kHz sample, as tools/spice/cases/bd.py has it.
"""

RATE = 96000
CLOCK = 455e3
T = 0.02  # trigger time
PRE = (0.005, 0.015)  # the reset pulse before it


def acc(code):
    return 5.0 * code / 63


def lfsr(t0, t1, rate=RATE, clock=CLOCK):
    """The noise bus averaged over each sample from t0 to t1, as PWL points (held at 7.5 V
    before and after)."""
    reg = (1 << 36) - 1
    phase = 0.0
    n = int(round((t1 - t0) * rate))
    per = clock / rate  # clock periods a sample
    pts = [(0.0, 7.5), (t0, 7.5)]
    for k in range(1, n + 1):
        left, s = per, 0.0
        while left > 1e-12:
            take = min(1 - phase, left)
            s += take * ((reg >> 35) & 1)
            phase += take
            left -= take
            if phase >= 1 - 1e-12:
                phase = 0.0
                reg = ((reg << 1) | (((reg >> 12) ^ (reg >> 30)) & 1)) & ((1 << 36) - 1)
        pts.append((t0 + k / rate, 15.0 * s / per))
    pts.append((t1 + 1e-6, 7.5))
    return pts


def tn_sine(t1, f=1000.0, v=1.0, step=1.0 / 48000):
    """A stand-in for the tom-noise bus: a sine to t1, then 0 V."""
    import math
    n = int(t1 / step)
    return [(k * step, v * math.sin(2 * math.pi * f * k * step)) for k in range(n + 1)] + [(t1 + step, 0.0)]


def cases(add, h):
    def hit(tom, name, code, seconds=0.3, controls=None):
        e = h.E
        trig = [(0, 0.0), (PRE[0], 0.0), (PRE[0] + e, 5.0), (PRE[1], 5.0), (PRE[1] + e, 0.0),
                (T, 0.0), (T + e, 5.0), (T + 2e-3, 5.0), (T + 2e-3 + e, 0.0)]
        dac = [(0, 0.0), (PRE[1] + 1e-3, 0.0), (PRE[1] + 1e-3 + e, acc(code))]
        inputs = {"trig": trig, "acc": dac,
                  "tomnoise.tn": tn_sine(T + 0.12)}
        add(f"{tom}_{name}", tom, T + seconds, inputs, controls, t0=T + 0.002, ttrig=T)

    for tom in ("lt", "mt", "ht"):
        hit(tom, "10", 10)
        hit(tom, "21", 21)
        hit(tom, "47_level1", 47, controls={"level": 1})
        hit(tom, "21_tune0_decay1", 21, 0.5, {"tune": 0, "decay": 1})
        hit(tom, "21_tune1_decay0", 21, 0.2, {"tune": 1, "decay": 0})

    # the tom noise: a hit of each tom, normal and accented (their gated accent lines as the toms
    # drive them: IC23b's output less Q29's drop for the 2 ms), and two toms at once
    def g(code, t=T):
        v = 3.173 * acc(code) + 0.682 - 0.05
        return h.pulse(v, t=t, w=2e-3)

    def noise_hit(name, which, seconds=0.06):
        inputs = {"noise": lfsr(0.0, T + seconds)}
        for tom in ("lt", "mt", "ht"):
            inputs[f"trig.{tom}"] = h.hold(0.0)
            inputs[f"{tom}.g"] = h.hold(0.0)
        for tom, code, t in which:
            inputs[f"trig.{tom}"] = h.pulse(5.0, t=t, w=2e-3)
            inputs[f"{tom}.g"] = g(code, t)
        add(name, "tomnoise", T + seconds, inputs, None, t0=0.0, ttrig=T)

    noise_hit("tomnoise_lt10", [("lt", 10, T)])
    noise_hit("tomnoise_ht47", [("ht", 47, T)])
    noise_hit("tomnoise_mt21_lt10", [("mt", 21, T), ("lt", 10, T + 0.001)])
