"""What a unit does, in closed form from its parts (parts.py), with the trims redone by the service
procedure. The formulas are the transcriptions' (docs/circuit-filter.md, docs/circuit-osc.md);
spice_check.py runs the same draws through the SPICE netlists to show they agree.

Each function takes a unit dict of arrays and returns arrays. Voltages are absolute unless the
name says "re bus". Frequencies are the VCF's pole frequency (the geometric mean of the four
stages) unless the name says "osc" (self-oscillation, where the four poles' phase reaches 180 deg).
"""
import numpy as np

K_B_Q = 8.617333e-5  # k/q, V/K
CENTS = 1200 / np.log(2)


def vt(u):
    return K_B_Q * (u["T"] + 273.15)


def taper_a(x, mid):
    """Japanese A taper, the exponential law through `mid` at half travel (as the model uses)."""
    b = (1 / mid - 1) ** 2
    return (b ** x - 1) / (b - 1)


# ----------------------------------------------------------------------------- the cutoff CV

def th1(u, t=None):
    t = u["T"] if t is None else t
    return u["TH1_25"] * (1 + 3.5e-3 * (t - 25))


def divider(u, t=None):
    """Pin 9 / IC18b's output (both re bus): R128, R127 || (R126 + TH1), R125."""
    rpar = 1 / (1 / u["R127"] + 1 / (u["R126"] + th1(u, t)))
    return u["R125"] / (u["R128"] + rpar + u["R125"])


def slope(u, t=None):
    """The expo's volts per octave at pin 9: proportional to absolute temperature."""
    t = u["T"] if t is None else t
    return u["slope25"] * (t + 273.15) / 298.15


def summer_current(u, freq=0.0, env_v=0.0, kybd=0.0, kcv=0.0, r8=None):
    """Current into IC18b's summing node (the bus is its virtual ground), amps.
    freq, kybd: slider travel 0-1 (B laws). env_v: the ENV line's volts above the bus, with the ENV
    slider at 10 (its wiper on the line). kcv: KCV volts. r8: VR21's share at KYBD 10 after VR8."""
    vb = u["Vbus"]
    # FREQ: VR17 from +14 V to ground, its wiper (Thevenin) through R186.
    x = np.clip(freq, 0, 1)
    i = (u["V14"] * x - vb) / (u["R186"] + u["VR17"] * x * (1 - x))
    i = i + (u["V14"] - vb) / u["R187"]
    i = i + env_v / u["R189"]
    # KYBD: TR42 follows VR21's wiper (KCV through VR8 into VR21 to ground) up by its V_EB.
    base = kcv * (r8 if r8 is not None else 0.0) * kybd
    i = i + (KYBD_FOLLOW * base + u["Vbe"] - vb) / u["R193"]
    return i


def pin9(u, t=None, **kw):
    """Pin 9 re bus for the given controls (see summer_current), with IC18b's output range."""
    v18 = np.clip(-u["R192"] * summer_current(u, **kw), 2.0 - u["Vbus"], 13.0 - u["Vbus"])
    return v18 * divider(u, t)


def stage_w(u, i_abc, t=None):
    """The four stages' pole frequencies (rad/s) for an OTA current: gm_i I Rp/(2 Vt R68 C)."""
    t = u["T"] if t is None else t
    v = K_B_Q * (t + 273.15)
    ws = []
    for i in range(4):
        rp = 1 / (1 / u[f"R68a{i}"] + 1 / u[f"R68b{i}"] + 1 / u[f"R560_{i}"])
        ws.append(u[f"gm{i}"] * i_abc * rp / (2 * v * u[f"R68a{i}"] * u[f"C240_{i}"]))
    return np.array(ws)


def w180(ws):
    """The frequency where four one-pole lags add to 180 deg, and the loop gain it needs there."""
    lo, hi = ws.min(0) * 0.5, ws.max(0) * 2.0
    for _ in range(60):
        mid = np.sqrt(lo * hi)
        ph = np.arctan(mid / ws).sum(0)
        lo = np.where(ph < np.pi, mid, lo)
        hi = np.where(ph < np.pi, hi, mid)
    w = np.sqrt(lo * hi)
    return w, np.prod(np.sqrt(1 + (w / ws) ** 2), axis=0)


def cutoff_hz(u, v9, t=None):
    """Pole frequency (geometric mean of the stages) and self-oscillation frequency, Hz."""
    i_abc = u["I0"] * 2 ** (-v9 / slope(u, t))
    ws = stage_w(u, i_abc, t)
    w, _ = w180(ws)
    return np.exp(np.log(ws).mean(0)) / (2 * np.pi), w / (2 * np.pi)


# TR42's follower and base current against R193: SPICE (spice_check.py) puts the KYBD path's gain
# 0.56 % under the ideal 1, so the closed forms use 0.9944.
KYBD_FOLLOW = 0.9944
# TR37's base current loads the threshold divider: SPICE puts the ENV's peak 42 mV under it.
PEAK_LOAD = 0.042


def kybd_trim(u):
    """VR8 trimmed at 25 C by the service procedure (KYBD 10: F5 an octave above F4).
    One volt of KCV moves pin 9 by R192/R193 x r8 x div; it must equal the expo's octave.
    Returns r8 (VR21's share), VR8 ohms, and whether the trim reached 1 oct/oct."""
    need = slope(u, 25.0) / (u["R192"] / u["R193"] * KYBD_FOLLOW * divider(u, 25.0))
    vr8 = u["VR21"] * (1 / need - 1)
    ok = (vr8 >= 0) & (vr8 <= u["VR8max"])
    vr8 = np.clip(vr8, 0, u["VR8max"])
    return u["VR21"] / (u["VR21"] + vr8), vr8, ok


def env_swing(u):
    """The ENV's peak above the bus: the threshold, R158 from +15 V into SUSTAIN's track (VR24)
    to the bus, less TR37's base current (about 20 mV)."""
    s = (u["V15"] - u["Vbus"]) * u["VR24"] / (u["R158"] + u["VR24"]) - PEAK_LOAD
    return s * u["swingScale"] if "swingScale" in u else s


# ----------------------------------------------------------------------------- the measures

def measures(u):
    m = {}
    vb = u["Vbus"]
    r8, vr8, trim_ok = kybd_trim(u)
    m["VR8 trim (kohm)"] = vr8 / 1e3
    m["KYBD trim reaches 1 oct/oct"] = trim_ok.astype(float)
    sl = slope(u)
    # Tracking when played at T (trimmed at 25 C).
    m["KYBD tracking at T (oct/oct)"] = u["R192"] / u["R193"] * KYBD_FOLLOW * r8 * divider(u) / sl

    v0, v10 = pin9(u, freq=0.0), pin9(u, freq=1.0)
    m["FREQ span 0-10 (oct)"] = (v0 - v10) / sl
    # The loaded B pot: octaves in the top division against the middle one.
    d_top = (pin9(u, freq=0.9) - v10) / sl
    d_mid = (pin9(u, freq=0.45) - pin9(u, freq=0.55)) / sl
    m["FREQ top/mid division"] = d_top / d_mid
    p0, o0 = cutoff_hz(u, v0)
    p10, o10 = cutoff_hz(u, v10)
    m["FREQ 0, KYBD 0: pole (Hz)"] = p0
    m["FREQ 0, KYBD 0: self-osc (Hz)"] = o0
    m["FREQ 10, KYBD 0: pole (kHz)"] = p10 / 1e3
    m["FREQ 10, KYBD 0: self-osc (kHz)"] = o10 / 1e3

    swing = env_swing(u)
    m["ENV peak re bus (V)"] = swing
    env_d = -u["R192"] * swing / u["R189"] * divider(u)
    m["ENV depth at ENV 10 (oct)"] = -env_d / sl
    # The clip's settings: FREQ 0, KYBD 10, C4 at TRANSPOSE M (KCV 2.0 V), ENV 10, RES 10.
    vs = pin9(u, freq=0.0, kybd=1.0, kcv=2.0, r8=r8)
    _, s0 = cutoff_hz(u, vs)
    _, s1 = cutoff_hz(u, vs + env_d)
    m["Clip settings: start, self-osc (Hz)"] = s0
    m["Clip settings: end, self-osc (kHz)"] = s1 / 1e3
    # The clip re-measured (clip_sweep.py): rest 31.9 Hz, ENV depth 7.24 oct. The I0 that puts the
    # rest there, and the expo slope (at 25 C) the measured depth needs with this unit's other parts.
    m["I0 for the clip's 31.9 Hz rest (uA)"] = u["I0"] * 31.9 / s0 * 1e6
    m["Slope for a 7.24-oct depth (mV/oct)"] = -env_d / 7.24 * 298.15 / (u["T"] + 273.15) * 1e3
    # The most KYBD can track, with VR8 at 0 (trimmed units need this >= 1).
    m["KYBD tracking with VR8 at 0 (oct/oct)"] = u["R192"] / u["R193"] * KYBD_FOLLOW * divider(u, 25.0) / slope(u, 25.0)

    # ENV times (circuit-filter.md 5.5): attack charges C59 toward +15 V and ends at the
    # threshold; decay and release are time constants.
    vtop = u["V15"] - 0.006
    rest = vb + 0.015
    lnA = np.log((vtop - rest) / (vtop - (vb + swing)))
    m["Attack at 10 (s)"] = (u["R141"] + u["VR22"]) * u["C59"] * lnA
    m["Attack at 0 (ms)"] = (u["R141"] + u["potEnd"]) * u["C59"] * lnA * 1e3
    m["Decay tau at 10 (s)"] = (u["R164"] + u["VR23"]) * u["C59"]
    m["Decay tau at 0 (ms)"] = (u["R164"] + u["potEnd"]) * u["C59"] * 1e3
    m["Release tau at 10 (s)"] = (u["R148"] + u["R163"] + u["VR25"]) * u["C59"]
    m["Release tau at 0 (ms)"] = (u["R148"] + u["R163"] + u["potEnd"]) * u["C59"] * 1e3

    # Self-oscillation onset (circuit-filter.md 3.2): the loop gain at full RES against what the
    # four (mismatched) stages need at their 180 deg point.
    af = u["hfeN"] / (u["hfeN"] + 1)
    g26 = u["R136"] * af * (1 / u["R131"] + 1 / u["R132"]) * 0.994 * 0.996
    rin27 = (u["hfeN"] + 1) * u["R137"]
    f27 = 0.990 * rin27 / (rin27 + 1e3)
    loop = g26 * f27 * u["R114"] / (u["R129"] + u["R113"]) * 0.994
    ws = stage_w(u, 10e-6)
    _, need = w180(ws)
    m["RES loop gain at 10"] = loop
    m["Self-osc onset (RES travel)"] = need / loop
    m["Self-osc at RES 10"] = (need / loop <= 1.0).astype(float)
    # Level re the saw (both through the same VCF output, VCA and output): the clamp's diode drop
    # into R113 against the saw's swing into R111.
    lvl = u["Vf"] * u["R111"] / (u["R113"] * u["sawPP"])
    m["Self-osc level re saw (dB, re nominal)"] = 20 * np.log10(lvl / (0.60 * 470e3 / (15e3 * 10.0)))

    # Portamento (circuit-osc.md 3).
    m["Portamento tau at 10 (s)"] = u["VR2"] * u["C10"]

    # MODULATOR (circuit-osc.md 5.1): f = k Vsq / (R15 + Rs) / (2 C9 dV). The upper threshold is
    # the TR5/TR6 node with both off: R35 from +15 V, R32 into TR8's base, R31 + R169 to the bus.
    g = 1 / u["R35"] + 1 / u["R32"] + 1 / (u["R31"] + u["R169"])
    vup = (u["V15"] / u["R35"] + 0.62 / u["R32"] + vb / (u["R31"] + u["R169"])) / g
    dv = vup - 0.02
    for name, x in (("0", 0.0), ("5", 0.5), ("10", 1.0)):
        a = np.clip(taper_a(x, u["taperMid"]) + (u["rateEnd"] if x == 0 else 0), 0, 1)
        rb = a * u["VR10"] + u["R168"]
        rt = (1 - a) * u["VR10"]
        k = rb / (rb + rt)
        rs = rb * rt / (rb + rt)
        m[f"LFO rate at {name} (Hz)"] = k * u["Vsq"] / (u["R15"] + rs) / (2 * u["C9"] * dv)

    # TUNE (circuit-osc.md 4.3): R89/R90 x the bus across VR9, at 1 V/oct after the VCO trims.
    m["TUNE range (+- cents)"] = 1200 * (u["R89"] / u["R90"]) * vb / 2
    # MOD depths at 10: the triangle through R170 against R188 at IC18a, then R3/R1 at 1 V/oct to
    # the VCO, or R192/R191 and the divider to the VCF.
    k188 = u["R188"] / (u["R170"] + u["R188"])
    m["VCO MOD at 10 (+- cents)"] = 1200 * dv * k188 / 2 * u["R3"] / u["R1"]
    tri_pk = (vup - vb) * k188  # IC18a's swing above the bus at the triangle's top
    m["VCF MOD at 10 (+- oct)"] = tri_pk * u["R192"] / u["R191"] * divider(u) / sl

    # PULSE WIDTH (circuit-osc.md 4.4): VR4 trims slider 0 to a square, i.e. pin 5 = pwFull/2.
    # At 10 (MAN), pin 5 scales by TR25's emitter at the MAN contact against at the bus.
    v_man = vb + (u["V15"] - vb) * u["VR12"] / (u["R178"] + u["VR12"])
    ratio = (v_man - 0.58) / (vb - 0.58)
    m["PW at 10, MAN (% high)"] = np.clip(1 - ratio / 2, 0, 1) * 100

    # Mixer levels re the saw at 10 (circuit-osc.md 4.7), as offsets from the nominal +2.0 / -0.1 dB.
    pp_pulse = (vb - 0.01) + (u["V15"] - vb) * u["VR13"] / (u["R92"] + u["VR13"])
    pp_pulse_n = (5.0 - 0.01) + (14.87 - 5.0) * 100e3 / 168e3
    pp_sub = u["V14"] * u["VR15"] / (u["R47sub"] + u["VR15"])
    pp_sub_n = 14.0 * 100e3 / 147e3
    saw = u["sawPP"] / u["R111"]
    saw_n = 10.0 / 470e3
    m["Pulse re saw (dB)"] = 2.0 + 20 * np.log10((pp_pulse / u["R110"]) / saw / ((pp_pulse_n / 680e3) / saw_n))
    m["Sub re saw (dB)"] = -0.1 + 20 * np.log10((pp_sub / u["R112"]) / saw / ((pp_sub_n / 820e3) / saw_n))

    # Output at VOLUME 10, full saw, VCF open, VCA in ENV mode at the peak, re nominal (-1.06 dBu).
    stages = np.prod([u[f"R68b{i}"] / u[f"R68a{i}"] for i in range(4)], axis=0)
    out_sum = u["R139"] / (u["R130"] + 2 * u["R139"])
    i_abc = (swing - 0.62) / (u["R197"] + u["R133"])
    gain = u["sawPP"] * u["R114"] / u["R111"] * stages * out_sum * i_abc * 0.985 * u["R140"] / (2 * vt(u))
    rl = 47e3
    jack = (u["R28j"] + rl) / (u["R29j"] + u["R28j"] + rl) * rl / (u["R28j"] + rl)
    gain_n = 10.0 * 68 / 470 * 1.0 * (1e3 / 152e3) * ((5.42 - 0.62) / 15.3e3) * 0.985 * 47e3 / (2 * K_B_Q * 298.15)
    jack_n = (1e3 + rl) / (3.2e3 + rl) * rl / (1e3 + rl)
    m["Output at VOLUME 10 (dB re nominal)"] = 20 * np.log10(gain * jack / (gain_n * jack_n))
    return m
