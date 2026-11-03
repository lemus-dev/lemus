"""SPICE cases for the bass drum (core/netlists/bd.cir; docs/circuit/bd.md).

The analog voices play DAC code 10 (normal) or 21 (accented) at TOTAL ACCENT 0, up to 21 and 47
at full TOTAL ACCENT; 63 is MIDI velocity 127 (docs/research.md C.4). acc = 5 V * code / 63. The
trigger is one 2 ms scan tick.

The VCO free-runs and has no DC operating point (the integrator's capacitor is open at DC), so
neither solver starts settled: each case triggers after 0.5 s (1.5 s at full DECAY, where C8
drains slowly) and is compared from 1 ms after the trigger. The trigger resets the VCO, but the
body VCA opens while Q11 is still emptying C14 (about 0.4 ms), so the first half millisecond
shows wherever the free-running triangle was: a blip that differs from hit to hit on a real 909
too, and between the two solvers.

The noise is the 909's LFSR (x^31 + x^13 + 1 from all ones; docs/circuit/noise.md) at the noise
generator's default clock, 320 kHz, 0/15 V, averaged over each 96 kHz sample, from 10 ms before
the trigger to 100 ms after it (ENV2 has closed the attack VCA by then); held at its 7.5 V mean
outside that.
"""

RATE = 96000
CLOCK = 320e3


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


def cases(add, h):
    def hit(name, code, seconds=0.6, controls=None, ttrig=0.5, noise=True):
        inputs = {"trig": h.pulse(5.0, t=ttrig, w=2e-3), "acc": h.hold(acc(code)),
                  "noise": lfsr(ttrig - 0.01, ttrig + 0.1) if noise else h.hold(7.5)}
        add(name, "bd", ttrig + seconds, inputs, controls, t0=ttrig + 0.001, ttrig=ttrig)

    hit("bd_10", 10)
    hit("bd_21", 21)
    hit("bd_47", 47)
    hit("bd_63_level1", 63, controls={"level": 1})
    hit("bd_21_tune0_attack1_decay0", 21, 0.4, {"tune": 0, "attack": 1, "decay": 0})
    hit("bd_10_tune1_attack0_decay1", 10, 1.4, {"tune": 1, "attack": 0, "decay": 1}, ttrig=1.5)
    hit("bd_21_attack1_noise0", 21, 0.3, {"attack": 1}, noise=False)
