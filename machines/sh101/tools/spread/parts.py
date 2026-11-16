"""Component tolerances for the SH-101 spread study (docs/spread.md), and random units drawn from them.

Every value is the transcription's (docs/circuit-osc.md, docs/circuit-filter.md); every tolerance
says where it comes from. A unit is a dict of numpy arrays, one entry per part, so a whole
population is computed at once.

Distributions: resistors, capacitors and pot tracks are uniform within their tolerance band. That
is conservative, since real parts cluster nearer nominal; the 5-95 % ranges of sums of several
parts come out near-normal anyway. Trims (VR1-VR8) are not drawn: measures.py redoes them per unit
by the service procedure.
"""
import numpy as np

# Tolerance classes, with their source.
TOL = {
    "R": (0.05, "carbon-film resistors, +-5 % (the SN parts list's 'carbon resistor', Japanese J grade)"),
    "Rmf": (0.01, "metal film, +-1 % (the dotted resistors around the CEM3340)"),
    "pot": (0.20, "carbon slide and rotary pots, track +-20 % (Alps/Matsushita EVH catalogue grade)"),
    "Cel": (0.20, "aluminium electrolytics, +-20 % (M grade; some 1980s parts were -10/+50 %)"),
    "Cfilm": (0.10, "polyester film, +-10 % (K grade)"),
    "Ccer": (0.05, "polystyrene/ceramic NP0 for the 240 pF and 1 nF timing parts, +-5 % (J grade)"),
}


class _Rng:
    """numpy's generator, or (nominal=True) one that returns every part's nominal value."""

    def __init__(self, seed, nominal):
        self.g = np.random.default_rng(seed)
        self.nominal = nominal

    def uniform(self, lo, hi, n, nom=None):
        if self.nominal:
            return np.full(n, (lo + hi) / 2 if nom is None else nom, dtype=float)
        return self.g.uniform(lo, hi, n)

    def normal(self, mu, sd, n):
        return np.full(n, mu, dtype=float) if self.nominal else self.g.normal(mu, sd, n)

    def choice(self, a, n):
        return np.full(n, a[-1], dtype=float) if self.nominal else self.g.choice(a, n)


def _u(rng, n, nom, tol):
    return nom * (1 + rng.uniform(-tol, tol, n))


def draw(n, seed=101, nominal=False):
    """n random units, or (nominal=True) n copies of the nominal unit. Returns a dict name ->
    array (SI units; temperatures in Celsius)."""
    rng = _Rng(seed, nominal)
    R, P, E, F, K = TOL["R"][0], TOL["pot"][0], TOL["Cel"][0], TOL["Cfilm"][0], TOL["Ccer"][0]
    u = {}

    def r(name, nom, tol=R):
        u[name] = _u(rng, n, nom, tol)

    # Rails and references.
    u["V15"] = _u(rng, n, 14.87, 0.02)    # TR3's regulated +15 V: zener-referenced, +-2 %
    u["V14"] = _u(rng, n, 14.0, 0.03)     # the bender board's +14 V, +-3 %
    u["Vbus"] = rng.uniform(4.93, 5.00, n, 5.0)  # +5 V bus after the D/A WIDTH trim (research.md §2: 4.93-5.0)
    u["T"] = rng.uniform(15, 35, n, 25.0)       # room temperature when played; trims done at 25 C
    u["hfeN"] = rng.uniform(200, 400, n, 300.0)  # 2SC1815GR
    u["hfeP"] = rng.uniform(200, 400, n, 300.0)  # 2SA1015GR
    u["Vbe"] = rng.normal(0.62, 0.01, n)  # small-signal V_BE at ~0.2-1 mA (data-sheet scatter)
    u["Vf"] = rng.normal(0.60, 0.015, n)  # 1S2473 at the clamp's current

    # Cutoff CV summer and pin-9 divider (circuit-filter.md 4).
    for name, nom in [("R186", 200e3), ("R187", 680e3), ("R189", 100e3), ("R191", 100e3), ("R192", 33e3),
                      ("R193", 150e3), ("R195", 33e3), ("R128", 4.7e3), ("R127", 5.6e3), ("R126", 1.8e3),
                      ("R125", 820)]:
        r(name, nom)
    for name in ("VR17", "VR19", "VR20", "VR21"):
        r(name, 100e3, P)
    r("VR8max", 47e3, P)
    # TH1 "SDT1000": 1 k at 25 C (assumed); a tempco part, taken as +3500 ppm/K, the usual expo
    # compensation resistor. Its tolerance +-5 %. (Its sign hardly matters here: see spread.md.)
    u["TH1_25"] = _u(rng, n, 1000.0, 0.05)
    # IR3109's expo: slope at 25 C between the AS3109 data sheet's 17.5 and OML's measured 20.8
    # mV/oct; I0 (OTA current with pin 9 at the bus) unknown for the original chip: assumed
    # log-uniform over x/2 about the AS3109's 2.4 uA.
    u["slope25"] = rng.uniform(17.5e-3, 20.8e-3, n, 19.0e-3)
    u["I0"] = 2.4e-6 * 2 ** rng.uniform(-1, 1, n, 0.0)
    # The four stages: 68k / 68k / 560 networks and 240 pF, and the OTAs' gm matched to +-5 % (OML).
    for i in range(4):
        r(f"R68a{i}", 68e3)
        r(f"R68b{i}", 68e3)
        r(f"R560_{i}", 560)
        u[f"C240_{i}"] = _u(rng, n, 240e-12, K)
        u[f"gm{i}"] = _u(rng, n, 1.0, 0.05)
    # Resonance loop (circuit-filter.md 3).
    for name, nom in [("R131", 10e3), ("R132", 150e3), ("R136", 15e3), ("R137", 33e3), ("R129", 5.6e3),
                      ("R113", 15e3), ("R114", 68e3), ("R130", 150e3), ("R139", 1e3)]:
        r(name, nom)
    r("VR18", 100e3, P)

    # ENV GEN (circuit-filter.md 5).
    for name, nom in [("R158", 82e3), ("R141", 270), ("R164", 150), ("R148", 220), ("R163", 47)]:
        r(name, nom)
    r("VR24", 100e3, P)  # SUSTAIN: its track sets the threshold, i.e. the ENV's peak
    for name in ("VR22", "VR23", "VR25"):
        r(name, 1e6, P)
    u["potEnd"] = rng.uniform(50, 500, n, 200.0)  # the 1M A pots' residual at 0
    u["C59"] = _u(rng, n, 4.7e-6, E)

    # VCA and output (circuit-filter.md 6-7).
    for name, nom in [("R197", 3.3e3), ("R133", 12e3), ("R140", 47e3), ("R29j", 2.2e3), ("R28j", 1e3),
                      ("R30j", 100e3)]:
        r(name, nom)

    # Oscillator section (circuit-osc.md).
    r("VR2", 2e6, P)                     # PORTAMENTO 2M A
    u["C10"] = _u(rng, n, 0.33e-6, F)     # handwritten ".33": film assumed
    r("VR10", 100e3, P)                  # RATE 100k A
    u["taperMid"] = rng.uniform(0.10, 0.20, n, 0.15)  # A taper at mid travel: 15 % nominal, 10-20 % in practice
    u["rateEnd"] = rng.uniform(0.0, 0.004, n, 0.0)   # RATE's residual at 0, as a share of the track
    # R169 and R188 are 100k on real boards (the service notes print 47k), R31 and R35 too (Din
    # Sync's BOM; the service notes leave them blank; circuit-osc.md 5.3).
    for name, nom in [("R168", 220), ("R15", 120e3), ("R35", 100e3), ("R31", 100e3), ("R32", 220e3),
                      ("R169", 100e3), ("R170", 47e3)]:
        r(name, nom)
    u["C9"] = _u(rng, n, 0.1e-6, F)
    u["Vsq"] = rng.normal(8.0, 0.15, n)   # IR9022's swing about the bus (2 V inside +15/-5 V)
    for name, nom in [("R89", 100e3), ("R90", 4.7e6), ("R3", 100e3), ("R1", 150e3), ("R178", 120e3),
                      ("R92", 68e3), ("R110", 680e3), ("R111", 470e3), ("R112", 820e3), ("R109", 150e3),
                      ("R47sub", 47e3), ("R188", 100e3), ("R173", 100e3)]:
        r(name, nom)
    for name in ("VR12", "VR13", "VR14", "VR15"):
        r(name, 100e3, P)
    u["sawPP"] = _u(rng, n, 10.0, 0.03)   # CEM3340 ramp, 0-10 V typ (data sheet; +-3 % assumed)
    u["pwFull"] = rng.uniform(4.6, 5.4, n, 5.0)  # CEM3340 pin 5 for 100 % (data sheet)
    u["vcaOs"] = rng.uniform(0.03e-3, 0.25e-3, n, 0.1e-3) * rng.choice([-1, 1], n)  # BA662A, offset selected
    return u
