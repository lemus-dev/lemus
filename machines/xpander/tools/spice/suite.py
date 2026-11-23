#!/usr/bin/env python3
"""The Xpander voice's model against ngspice, block by block.

    lemus/.venv/bin/python machines/xpander/tools/spice/suite.py [BUILD_DIR]

The decks are the voice board as research.md 3-4 draws it (voice sheet 3, SM p55), the CEM chips as
their data sheets' equations in behavioural sources, every external part a real part with its
schematic value: op-amps ideal, the CMOS switches their on-resistance (the model's values), the
pulse diodes a 1N4148's DC model. The model runs through xpandercircuitrun at 768 kHz.

1. CEM3372 alone: the input VCA's scaling, the four tanh cells into their capacitors (cell 4 at
   1/100 of the current into 330 pF, the x100 buffer), its small-signal response and cut-off law.
2. The 16 filter modes: the voice from the NOISE net to VOICE X (bus B, the CEM3372, the cell
   buffers as op-amp stages, UX15B through CX48, the 4051 and UX15A, the final VCA and UX16).
3. Resonance: VCF OUT -> CX48 -> RX64/RX63 -> pin 11 -> the resonance VCA's pair (tanh, 2 V_T,
   against the compensation node that carries the filter's input) -> 0-190 -> back into the input:
   (1 + k) u - k pin 11 small-signal; the onset of a kick's ringing and the oscillation, FD low and
   high.
4. CEM3374: the exponential current (less its drop across the converter's bulk resistance) into
   the 1000 pF capacitor in series with RX25 // CX26, the comparator between 0 and V_TH with its
   delay (a transmission line); frequency against VCIN.
5. The mix buses into pins 6 and 7: the switches (4016 R_on), the resistor networks, the pulse
   comparator through its diode, the coupling capacitors and 680R; each waveform's level at the pin.

The CEM chips have no transistor-level model, so this checks the model's solver and wiring against
its own circuit, not the chips. Exit status 1 if anything is out of tolerance.
"""
import math
import os
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(ROOT), "build", "xpander")
RUN = os.path.join(BUILD, "xpandercircuitrun")
failures = 0


def check(ok, what):
    global failures
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        failures += 1


def cents(a, b):
    return 1200 * math.log2(a / b)


# The data sheets (research.md 3.1, 3.3-3.4; model.md).
VT = 0.02585                         # kT/q
I_CELL0 = 2 * VT * 2 * math.pi * 1000 * 0.03e-6  # cell current: 1 kHz at 0 V with 0.03 uF
VOCT_3372 = 0.019                    # pin 15, V/octave
MIX = 0.01 * 35                      # input VCA at 5 V into the filter
TAPER = 4.394                        # the input VCAs' audio taper [model's choice]
RON_UX14 = 125                       # UX14b (CD4053B, R_on typical at 15 V) in series with cell 1's capacitor
RES_GAIN = 115                       # resonance VCA at 5 V on pin 10
OUT_SWING = 5.0                      # VCF OUT, 10 V p-p
VCA_MAX, VCA_OFF = 0.93, 0.085       # final VCA
VTH = 3.99                           # CEM3374 V_TH: +4REF
VOCT_3374 = 0.0179                   # CEM3374 VCIN, V/octave
R_BULK_3374 = 10                     # CEM3374 exponential converter bulk resistance [model's fit]
RON_4051, RON_4053, RON_4016 = 250, 125, 600  # the model's switch resistances at these supplies
VOH_TL082 = 10.5                     # comparator high on +-12 V

# The pole-mix network (research.md 3.3): per 4051 channel, the resistors from s1 (+101), s2 (-101),
# s3 (+101), s4 (-VCF OUT) into the channel, with their designators.
POLE_MIX = [
    [None, ("RX89", 100e3), ("RX90", 49.9e3), ("RX91", 100e3)],
    [("RX88", 100e3), ("RX87", 33.2e3), ("RX86", 33.2e3), ("RX85", 100e3)],
    [("RX81", 100e3), ("RX82", 33.2e3), ("RX83", 16.5e3), ("RX84", 24.9e3)],
    [("RX93", 100e3), ("RX92", 49.9e3), ("RX94", 49.9e3), None],
    [None, ("RX75", 62e3), None, None],
    [("RX73", 100e3), ("RX74", 49.9e3), ("RX72", 100e3), None],
    [None, None, None, ("RX76", 56e3)],
    [("RX95", 100e3), ("RX71", 100e3), None, None],
]
# research.md 3.3's labels: (FD low, FD high).
LABELS = [("4P BP", "2PHP+1PLP"), ("3PHP+1PLP", "3P HP"), ("3PAP+1PLP", "3P AP"), ("2PN+1PLP", "2P N"),
          ("2P LP", "1P LP"), ("2PHP+1PLP", "2P HP"), ("4P LP", "3P LP"), ("2P BP", "1P HP")]
STAGE_GAIN = [101, -101, 101, -100]


def taper(v):
    x = min(max(v / 5.0, 0.0), 1.0)
    return (math.exp(TAPER * x) - 1) / (math.exp(TAPER) - 1)


def vcff_for(pin15):
    """VCFF for a pin-15 voltage: RX53 38.3K from VCFF, RX52 196K from -5 V, RX54 1K to ground."""
    return (pin15 * (1 / 38.3e3 + 1 / 196e3 + 1 / 1e3) + 5 / 196e3) * 38.3e3


def cem3372(pin6, pin7, level_a, level_b, pin15, res, fd, r1=0):
    """The chip from its data sheet: input VCAs (2 V_T pairs, audio taper, x35, 1/100 into the
    filter), the resonance VCA (a 2 V_T pair between pin 11 and the compensation node, which carries
    the filter's input at pin 11's scale, x 0-RES_GAIN, back at the filter's scale: (1 + k) u - k pin 11
    small-signal), four tanh transconductors into the cell capacitors (cell 4 at 1/100 of the current
    into 330 pF), the x100 output buffer with its swing. pin15 is a node or a number."""
    g = RES_GAIN * min(max(res, 0), 5) / 5
    comp = 100 * 1.2 / 21.2  # the compensation node: the input at pin 11's scale (k = G x divider)
    f15 = pin15 if isinstance(pin15, str) else None
    lines = [
        f".param TA={taper(level_a):.12g} TB={taper(level_b):.12g} G={g:.12g} CMP={comp:.12g} VT={VT} I0={I_CELL0:.12g}",
        f"Bu u 0 V={MIX}*2*{{VT}}*({{TA}}*tanh(V({pin6})/(2*{{VT}}))+{{TB}}*tanh(V({pin7})/(2*{{VT}})))",
        "Bu1 u1 0 V=V(u)-{G}*2*{VT}*tanh((V(pin11)-{CMP}*V(u))/(2*{VT}))/100",
    ]
    ctl = f"exp(0.69314718056*V({f15})/{VOCT_3372})" if f15 else f"{math.exp(math.log(2) * pin15 / VOCT_3372):.12g}"
    caps = [100e-12 if fd else 0.033e-6, 0.033e-6, 0.033e-6, 330e-12]
    prev = "u1"
    for i, c in enumerate(caps):
        scale = "/100" if i == 3 else ""
        lines += [f"Bcell{i + 1} 0 c{i + 1} I={{I0}}{scale}*{ctl}*tanh((V({prev})-V(c{i + 1}))/(2*{{VT}}))"]
        # Cell 1's capacitor reaches pin 4 through UX14b (CD4053B, FD), its R_on in series.
        lines += [f"RUX14 c1 c1x {r1}", f"Ccell1 c1x 0 {c:.6g}"] if i == 0 and r1 else [f"Ccell{i + 1} c{i + 1} 0 {c:.6g}"]
        prev = f"c{i + 1}"
    lines += [
        f"Bout vcfout 0 V={OUT_SWING}*tanh(100*V(c4)/{OUT_SWING})",
        # VCF OUT -> CX48 -> RX68 into UX15B's virtual ground, and RX64/RX63 into pin 11.
        "CX48 vcfout f48 2.2u", "RX64 f48 pin11 20k", "RX63 pin11 0 1.2k",
        "RX68 f48 m15b 100k", "RX67 s4 m15b 100k", "EUX15B s4 0 0 m15b 1e8",
    ]
    return lines


def voice_deck(mode, vcff, res, source, analysis, mix=True, r1=None):
    """The voice from the NOISE net (its Thevenin source, 47 ohm) to VOICE X. Bus A is silent (VOLA
    0); bus B carries the noise path only, with the off paths' loads."""
    lines = ["* Xpander voice, behavioural CEM3372 and the board's parts", source,
             "R44 nsrc n44 47", "RX61 n44 n61 3.3k", f"RonUX14a n61 busb {RON_4053}",
             "RX35 busb n35 3.3k", "RX34 n35 0 47k",            # SAW2 off: RX34/RX35 still on the bus
             "Vcmp2 cmp2 0 -10.5", "DX04 cmp2 n37 D1N4148", "RX37 n37 busb 360k",  # PW2 low: diode off
             "CX33 busb pin7 2.2u", "RX40 pin7 0 680", "Vpin6 pin6 0 0",
             # Pin 15: RX53 from VCFF, RX52 from -5 V, RX54 to ground, RX55 + CX34 from the FM node.
             f"Vvcff vcff 0 {vcff:.12g}", "RX53 vcff pin15 38.3k", "Vm5 m5 0 -5", "RX52 pin15 m5 196k",
             "RX54 pin15 0 1k", "RX55 pin15 n55 39k", "CX34 n55 n41 0.1u", "RX41 n41 0 10k",
             ".model D1N4148 D(IS=4.352n N=1.906 RS=0.6458)"]
    lines += cem3372("pin6", "pin7", 0, 5, "pin15", res, mode & 8, RON_UX14 if r1 is None else r1)
    if mix:
        # The cell buffers: UX13B (+101), UX13D then UX13C (-101), UX13A (+101).
        lines += ["EUX13B s1 0 c1 m13b 1e8", "RX80 s1 m13b 100k", "RX79 m13b 0 1k",
                  "EUX13D s2a 0 c2 m13d 1e8", "RX60 s2a m13d 100k", "RX59 m13d 0 1k",
                  "RX77 s2a m13c 10k", "RX78 s2 m13c 10k", "EUX13C s2 0 0 m13c 1e8",
                  "EUX13A s3 0 c3 m13a 1e8", "RX57 s3 m13a 100k", "RX58 m13a 0 1k"]
        # The selected 4051 channel into UX15A (RX66 68K, RX69 10K to ground).
        for j, r in enumerate(POLE_MIX[mode & 7]):
            if r:
                lines.append(f"{r[0]} s{j + 1} ch {r[1]:.6g}")
        lines += [f"RonUX17 ch sum {RON_4051}", "RX66 mix sum 68k", "RX69 sum 0 10k", "EUX15A mix 0 0 sum 1e8"]
        # The final VCA: CX47 + RX62 into pin 12 (held at -1 V), current gain from pin 13 (RX51 18K,
        # CX40), out of pin 14 into UX16 (RX70 33K), CX49 into the pan network (about 10K).
        lines += ["CX47 mix n47 2.2u", "RX62 n47 pin12 33k", "Vpin12 pin12 0 -1",
                  "Vvca vcacv 0 5", "RX51 vcacv pin13 18k", "CX40 pin13 0 0.1u",
                  f"Bvca vo 0 V=max({VCA_MAX}*1e-6,{VCA_MAX}*(V(pin13)-{VCA_OFF})/(5-{VCA_OFF}))*33k*i(Vpin12)",
                  "CX49 vo vx 2.2u", "Rpan vx 0 10k"]
    lines += [".options reltol=1e-6 abstol=1e-15 vntol=1e-9", analysis, ".end"]
    return "\n".join(lines) + "\n"


def ngspice(deck, vectors, control="run"):
    with tempfile.TemporaryDirectory() as d:
        cir, dat = os.path.join(d, "x.cir"), os.path.join(d, "x.dat")
        ctrl = f".control\n{control}\nwrdata {dat} {' '.join(vectors)}\n.endc\n"
        open(cir, "w").write(deck.replace(".end\n", ctrl + ".end\n"))
        subprocess.run(["ngspice", "-b", cir], check=True, capture_output=True)
        return np.atleast_2d(np.loadtxt(dat))


def ac(deck, vectors):
    """Complex responses on ngspice's grid: (f, [H per vector])."""
    data = ngspice(deck, vectors)
    return data[:, 0], [data[:, 3 * k + 1] + 1j * data[:, 3 * k + 2] for k in range(len(vectors))]


def at(f, h, fr):
    """The complex response at fr, interpolated on log f."""
    x = np.log(f)
    return complex(np.interp(math.log(fr), x, h.real), np.interp(math.log(fr), x, h.imag))


def model(*args):
    return subprocess.run([RUN, *map(str, args)], check=True, capture_output=True, text=True).stdout.split()


def model_table(*args, cols):
    v = model(*args)
    return np.array(v, dtype=float).reshape(-1, cols)


def compare(fm, hm, fd, hd, floor_db=60):
    """Largest dB and phase differences at the model's frequencies, where the deck is within
    floor_db of its largest; and the largest complex difference relative to that largest."""
    hs = np.array([at(fd, hd, f) for f in fm])
    peak = np.max(np.abs(hs))
    sel = np.abs(hs) > peak * 10 ** (-floor_db / 20)
    ddb = np.abs(20 * np.log10(np.abs(hm[sel]) / np.abs(hs[sel])))
    dph = np.abs((np.angle(hm[sel] / hs[sel], deg=True) + 180) % 360 - 180)
    rel = np.max(np.abs(hm - hs)) / peak
    return ddb.max(), dph.max(), rel, hs


# ---------------------------------------------------------------------------------------------
print("1. CEM3372 alone: pin 6 to VCF OUT, no resonance (absolute dB, input VCA included)")
FREQS = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
for pin15, level in ((-0.06, 5), (0.0, 5), (0.06, 5), (0.0, 2.5)):
    t = model_table("vcf-ac", pin15, level, *FREQS, cols=3)
    hm = 10 ** (t[:, 1] / 20) * np.exp(1j * np.radians(t[:, 2]))
    deck = "\n".join(["* CEM3372 alone", "Vin pin6 0 DC 0 AC 1", "Vpin7 pin7 0 0"]
                     + cem3372("pin6", "pin7", level, 0, pin15, 0, 0)
                     + [".options reltol=1e-6", ".ac dec 1000 5 50k", ".end"]) + "\n"
    f, (hd,) = ac(deck, ["v(vcfout)"])
    ddb, dph, rel, hs = compare(t[:, 0], hm, f, hd)
    pb = 20 * math.log10(abs(hs[0]))
    # 0.1 dB: at 768 kHz the trapezoidal cells, prewarped at their pole, still put 20 kHz 0.2 % high
    # (0.056 dB through four poles at 8 kHz).
    check(ddb < 0.1 and dph < 1,
          f"pin 15 {pin15 * 1e3:+.0f} mV, pin 5 {level:.1f} V: passband {pb:.2f} dB; largest difference "
          f"{ddb:.3f} dB, {dph:.2f} deg (to -60 dB)")
expect = 20 * math.log10(MIX * 100 * taper(2.5))
print(f"      (passband: VCF OUT / pin 6 = 0.35 x taper x 100: {20 * math.log10(MIX * 100):.2f} dB at 5 V, "
      f"{expect:.2f} dB at 2.5 V)")

print("\n1. CEM3372 alone: cut-off law (cell pole where the 4 poles give -180 deg)")
law_fc = lambda v: 1000 * 0.03 / 0.033 * 2 ** (v / VOCT_3372)
deck_fc, model_fc = {}, {}
for pin15 in (-0.1, -0.05, 0.0, 0.05, 0.08, 0.1):
    fl = law_fc(pin15)
    fr, db, ph = map(float, model("vcf-ac", pin15, 5, round(fl)))
    ph -= 360 * round((ph + 180) / 360)  # the branch around -180
    model_fc[pin15] = fr / math.tan(math.radians(-ph / 4))
    deck = "\n".join(["* CEM3372 alone", "Vin pin6 0 DC 0 AC 1", "Vpin7 pin7 0 0"]
                     + cem3372("pin6", "pin7", 5, 0, pin15, 0, 0)
                     + [".options reltol=1e-6", f".ac dec 2000 {fl / 4:.6g} {fl * 4:.6g}", ".end"]) + "\n"
    f, (hd,) = ac(deck, ["v(vcfout)"])
    p = np.unwrap(np.angle(hd))
    deck_fc[pin15] = math.exp(np.interp(math.pi, -p, np.log(f)))
    check(abs(cents(model_fc[pin15], deck_fc[pin15])) < 1 and abs(cents(deck_fc[pin15], fl)) < 1,
          f"pin 15 {pin15 * 1e3:+4.0f} mV: model {model_fc[pin15]:9.2f} Hz, ngspice {deck_fc[pin15]:9.2f} Hz "
          f"({cents(model_fc[pin15], deck_fc[pin15]):+.2f} cents), law {fl:9.2f} Hz")
slope = 1000 * 0.2 / math.log2(model_fc[0.1] / model_fc[-0.1])
check(abs(slope - 19) < 0.02, f"scale over -100..+100 mV: {slope:.3f} mV/octave (data sheet 19); "
      f"0 V: {model_fc[0.0]:.2f} Hz (909.09 with 0.033 uF)")

# ---------------------------------------------------------------------------------------------
print("\n2. The 16 modes: NOISE net to VOICE X, small signal (deck within 60 dB of its largest)")
fd_phase = []
MODE_FREQS = [20, 50, 100, 200, 400, 700, 1000, 1300, 1575, 2000, 3000, 5000, 7000, 10000, 14000, 20000]
for pin15 in (0.0, 0.04):
    vcff = vcff_for(pin15)
    fc = law_fc(pin15)
    print(f"   VCFF {vcff:.4f} V (pin 15 {pin15 * 1e3:+.0f} mV, cells at {fc:.0f} Hz)")
    for mode in range(16):
        t = model_table("voice-ac", mode, f"{vcff:.12g}", *MODE_FREQS, cols=5)
        hm = 10 ** (t[:, 1] / 20) * np.exp(1j * np.radians(t[:, 2]))
        f, (hd, hmix) = ac(voice_deck(mode, vcff, 0, "Vn nsrc 0 DC 0 AC 1", ".ac dec 1000 5 50k"),
                           ["v(vx)", "v(mix)"])
        ddb, dph, rel, hs = compare(t[:, 0], hm, f, hd)
        i = int(np.argmax(np.abs(hs)))
        label = LABELS[mode & 7][mode >> 3]
        if mode & 8:
            fd_phase.append((dph, mode, fc))
        # The level and shape in dB; the phase only to 5 deg here (polarity, gross errors): FD high's
        # phase is held below.
        check(ddb < 0.1 and dph < 5,
              f"mode {mode:2d} {label:10s} peak {20 * math.log10(abs(hs[i])):+6.2f} dB at {t[i, 0]:5.0f} Hz: "
              f"largest difference {ddb:.3f} dB, {dph:.2f} deg; complex {100 * rel:.2f} % of peak")
# With FD high cell 1 has 100 pF, its pole 330 x the others' (300 kHz with cells at 909 Hz): above
# cem3372.cpp's warp() knee (x = 1.2) its trapezoidal gain grows 7.6 x faster than x, so for audio the
# pole acts several times higher than it is and its phase lag at 20 kHz is too small.
worst = max(fd_phase)
check(worst[0] < 1, f"FD high (cell 1 at 100 pF): largest phase difference {worst[0]:.2f} deg "
      f"(mode {worst[1]}, cells at {worst[2]:.0f} Hz, at 20 kHz)")

# The responses' shapes (research.md 3.3's table) on the deck: UX15A's output per unit input at cell 1
# (u1) against the table's polynomial in L = 1/(1 + s), from the resistor values, with what the table
# leaves out put back: CX48's high-pass on s4, cell 1's own pole (330 x higher with FD high, 100 pF)
# and the 4051's R_on. Then the gains at fc/30 and 30 fc (the table: 68.7, LP 110.8 and 121.4, less
# R_on); where the low end is not 0, it is s4's CX48 leaving the cancellation incomplete.
print("\n2. The deck's shapes against research.md 3.3 (UX15A per unit cell-1 input, cells at 909 Hz)")
fc = law_fc(0.0)
f48 = 1 / (2 * math.pi * 2.2e-6 / (1 / 21.2e3 + 1 / 100e3))
for mode in range(16):
    # Ideal cells (no UX14 R_on in cell 1), so the table alone is tested.
    deck = voice_deck(mode, vcff_for(0.0), 0, "Vn nsrc 0 DC 0 AC 1", ".ac dec 1000 0.5 500k", r1=0)
    f, (hmix, hu1) = ac(deck, ["v(mix)", "v(u1)"])
    fd = mode >> 3
    rs = POLE_MIX[mode & 7]
    ron = 1 + RON_4051 * sum(1 / r[1] for r in rs if r)
    coef = [0 if r is None else -68e3 * STAGE_GAIN[j] / r[1] / ron for j, r in enumerate(rs)]

    def poly(fr):
        s_ = 1j * fr / fc
        L, L1 = 1 / (1 + s_), 1 / (1 + s_ / (330 if fd else 1))
        hp = (1j * fr / f48) / (1 + 1j * fr / f48)
        return sum(c * tp for c, tp in zip(coef, [L1, L1 * L, L1 * L * L, L1 * L ** 3 * hp]))

    grid = [fc * 2 ** (k / 4) for k in range(-12, 13)]
    got = np.array([at(f, hmix, fr) / at(f, hu1, fr) for fr in grid])
    ideal = np.array([poly(fr) for fr in grid])
    worst = np.max(np.abs(got - ideal)) / np.max(np.abs(ideal))
    lo, hi = abs(at(f, hmix, fc / 30) / at(f, hu1, fc / 30)), abs(at(f, hmix, fc * 30) / at(f, hu1, fc * 30))
    check(worst < 0.002, f"mode {mode:2d} {LABELS[mode & 7][fd]:10s} = the table's polynomial within "
          f"{100 * worst:.3f} % (fc/8..8 fc); gain {lo:7.2f} at fc/30, {hi:6.2f} at 30 fc")

# ---------------------------------------------------------------------------------------------
print("\n3. Resonance: a kick (0.1 V for 50 us on NOISE) and VCF OUT")


def deck_ring(mode, vcff, res, t0, t1, step, loop_inside=False):
    deck = voice_deck(mode, vcff, res, "Vn nsrc 0 PULSE(0 0.1 0 1n 1n 50u 1)",
                      f".tran {step} {t1} {t0} {step}", mix=False)
    if loop_inside:  # the variant the model solves: CX48 driven from before the swing limit
        deck = deck.replace("CX48 vcfout f48 2.2u", "Bx100 x100 0 V=100*V(c4)\nCX48 x100 f48 2.2u")
    data = ngspice(deck, ["v(vcfout)"])
    return data[:, 0], data[:, 1]


def deck_onset(mode, vcff, lo, hi, seconds=0.4, iters=9):
    for _ in range(iters):
        mid = (lo + hi) / 2
        t, v = deck_ring(mode, vcff, mid, seconds * 3 / 4, seconds, "1u")
        if np.ptp(v) > 0.2:
            hi = mid
        else:
            lo = mid
    return (lo + hi) / 2


def osc_of(t, v):
    vv = v - np.mean(v)
    ups = np.where((vv[:-1] < 0) & (vv[1:] >= 0))[0]
    tc = t[ups] - vv[ups] * (t[ups + 1] - t[ups]) / (vv[ups + 1] - vv[ups])
    return (len(tc) - 1) / (tc[-1] - tc[0]), np.ptp(v)


for mode, pin15 in ((6, 0.0), (6, -0.04), (14, 0.0)):
    vcff = vcff_for(pin15)
    m = float(model("voice-onset", mode, f"{vcff:.12g}", 0.4)[0])
    s = deck_onset(mode, vcff, m * 0.9, m * 1.1)
    k = RES_GAIN * m / 5 * 1.2 / 21.2
    if mode == 14:
        # The 3-pole loop (cell 1 at 100 pF) needs k of about 8: at the board's divider the chip's
        # 0-5 V never gets there (owner's manual: RES 63 oscillates "in some mode settings").
        check(m > 4.999 and s > 5.0, f"onset, {LABELS[mode & 7][mode >> 3]}, cells at {law_fc(pin15):.0f} Hz: "
              f"none up to RES 5 V (model {m:.4f} V, ngspice {s:.4f} V); loop gain at 5 V k = {k:.3f}")
        continue
    check(abs(m / s - 1) < 0.01, f"onset, {LABELS[mode & 7][mode >> 3]}, cells at {law_fc(pin15):.0f} Hz: "
          f"RES {m:.4f} V (ngspice {s:.4f}, {100 * (m / s - 1):+.2f} %); loop gain k = {k:.3f}")

for mode, pin15, res in ((6, 0.0, 4.0), (6, 0.0, 5.0), (6, -0.04, 4.0), (6, 0.04, 4.0)):
    vcff = vcff_for(pin15)
    mhz, mpp = map(float, model("voice-osc", mode, f"{vcff:.12g}", res, 0.3))
    fc = law_fc(pin15)
    step = f"{min(0.5e-6, 1 / (fc * 4000)):.3g}"
    t, v = deck_ring(mode, vcff, res, 0.225, 0.3, step)
    shz, spp = osc_of(t, v)
    check(abs(cents(mhz, shz)) < 2 and abs(mpp / spp - 1) < 0.02,
          f"oscillation, {LABELS[mode & 7][mode >> 3]}, cells at {fc:.0f} Hz, RES {res:.1f} V: {mhz:.2f} Hz "
          f"(ngspice {shz:.2f}, {cents(mhz, shz):+.2f} cents), VCF OUT {mpp:.3f} V p-p (ngspice {spp:.3f}, "
          f"{100 * (mpp / spp - 1):+.2f} %)")
    # Where the difference comes from: the model's rate (trapezoidal cells at 192 kHz) and the
    # loop's source (the model feeds back 100 x cell 4, the board VCF OUT after its swing limit).
    own = float(model("--rate", 192000, "voice-osc", mode, f"{vcff:.12g}", res, 0.3)[0])
    fine = float(model("--rate", 6144000, "voice-osc", mode, f"{vcff:.12g}", res, 0.3)[0])
    ihz, ipp = osc_of(*deck_ring(mode, vcff, res, 0.225, 0.3, step, loop_inside=True))
    print(f"      model at 6.144 MHz {fine:.2f} Hz, at 192 kHz {own:.2f} Hz ({cents(own, fine):+.1f} cents); "
          f"ngspice with the loop from inside the buffer {ihz:.2f} Hz (6.144 MHz {cents(fine, ihz):+.2f} cents)")

# ---------------------------------------------------------------------------------------------
print("\n4. CEM3374 core: frequency against VCIN (pin 12)")


def vco_deck(vcin, periods, settle, vcof=None):
    iref = VTH / 806e3
    hz = iref * 2 ** (vcin / VOCT_3374) / (2 * 1000e-12 * VTH)
    T = 1 / hz
    step = T / 20000
    lines = ["* CEM3374 core, behavioural", f".param VTH={VTH}",
             # IREF: +4REF through RX29 806K into the pin's virtual ground (RX30 + CX30 to ground).
             f"Vref ref 0 {VTH}", "RX29 ref iref 806k", "Viref iref 0 0"]
    if vcof is None:
        lines.append(f"Vvcin vcin 0 {vcin:.12g}")
    else:  # VCOF1 through RX26 4.22K over RX27 100R with CX29
        lines += [f"Vvcof vcof 0 {vcof:.12g}", "RX26 vcof vcin 4.22k", "RX27 vcin 0 100", "CX29 vcin 0 0.1u"]
    lines += [
        # The comparator: towards V_TH while charging, towards 0 while discharging; its decision
        # reaches the current switch 0.5 us later (a matched line, so V(qd) = q/2 delayed).
        "Bq q 0 V=tanh(1e5*({VTH}/2*(1+2*V(qd))-V(pin13)))",
        "Rs q qa 50", "Tdelay qa 0 qd 0 Z0=50 TD=0.5u", "Rl qd 0 50",
        # The exponential current through the converter's bulk resistance: I = I0 2^(-I R / V_oct)
        # (V(ie) = I R), switched into or out of the capacitor.
        f"Be 0 ie I=i(Viref)*exp(0.69314718056*(V(vcin)-V(ie))/{VOCT_3374})", f"Rbulk ie 0 {R_BULK_3374}",
        f"Bi 0 pin13 I=V(ie)/{R_BULK_3374}*2*V(qd)",
        # CX28 1000 pF to the HF network RX25 1.2K // CX26 0.01 uF.
        "CX28 pin13 hf 1000p", "RX25 hf 0 1.2k", "CX26 hf 0 0.01u",
        ".options reltol=1e-7 abstol=1e-15 vntol=1e-9",
        f".tran {step:.4g} {(settle + periods) * T:.6g} {settle * T:.6g} {step:.4g} uic", ".end"]
    if vcof is not None:  # let CX29 charge first
        lines[-2] = f".tran {step:.4g} {(settle + periods) * T + 5e-3:.6g} {settle * T + 5e-3:.6g} {step:.4g} uic"
        lines.insert(-2, ".ic v(vcin)=" + f"{vcof * 100 / 4320:.12g}")
    data = ngspice("\n".join(lines) + "\n", ["v(pin13)"])
    t, v = data[:, 0], data[:, 1]
    th = VTH / 2
    i = np.where((v[:-1] < th) & (v[1:] >= th))[0]
    tc = t[i] + (th - v[i]) * (t[i + 1] - t[i]) / (v[i + 1] - v[i])
    return (len(tc) - 1) / (tc[-1] - tc[0]), hz


for vcin in (-0.1, -0.05, 0.0, 0.03, 0.06, 0.08, 0.1, 0.11):
    mhz = float(model("vco-hz", vcin)[0])
    shz, ideal = vco_deck(vcin, 12, 4)
    check(abs(cents(mhz, shz)) < 1, f"VCIN {vcin * 1e3:+5.0f} mV: {mhz:10.3f} Hz (ngspice {shz:10.3f}, "
          f"{cents(mhz, shz):+.2f} cents); against I/(2 C V_TH) {ideal:10.3f}: model {cents(mhz, ideal):+6.2f}, "
          f"ngspice {cents(shz, ideal):+6.2f} cents")
for vcof in (-2.0, 1.5):
    mhz = float(model("voice-vco", vcof)[0])
    shz, ideal = vco_deck(vcof * 100 / 4320, 12, 4, vcof=vcof)
    check(abs(cents(mhz, shz)) < 1, f"through the voice, VCOF1 {vcof:+.1f} V (RX26/RX27): {mhz:.3f} Hz "
          f"(ngspice {shz:.3f}, {cents(mhz, shz):+.2f} cents)")

# ---------------------------------------------------------------------------------------------
print("\n5. Mix buses: each waveform's level at pins 6 and 7 (VCOs near 200 Hz)")


def bus_deck(waves, f1, f2, pw1, pw2, t0, t1, step):
    lines = ["* the waveform switches and mix buses",
             ".model D1N4148 D(IS=4.352n N=1.906 RS=0.6458)"]
    # (bus, tri bit, saw bit, frequency, PW, tri R, saw divider, saw load, saw out, pulse R, diode, coupling, 680R)
    for b, tri_on, saw_on, hz, pw, rt, (rsi, rss), (rsl, rso), rp, dx, cx, rb in (
            ("a", waves & 4, waves & 32, f1, pw1, "RX42", ("RX45", "RX44"), ("RX46", "RX47"), "RX48", "DX05", "CX37", "RX56"),
            ("b", waves & 16, waves & 2, f2, pw2, "RX31", ("RX33", "RX32"), ("RX34", "RX35"), "RX37", "DX04", "CX33", "RX40")):
        T = 1 / hz
        pin = "pin6" if b == "a" else "pin7"
        e = T / 5000  # edges: about the model's band-limited ones
        lines += [f"Vtri{b} tri{b} 0 PULSE(0 {VTH} 0 {T / 2:.9g} {T / 2:.9g} 0 {T:.9g})",
                  f"Vsaw{b} saw{b} 0 PULSE(0 {2 * VTH} {-e / 2:.9g} {T - e:.9g} {e:.9g} 0 {T:.9g})"]
        duty = pw / VTH  # high while PW > saw/2
        if 0 < duty < 1:
            lines.append(f"Vcmp{b} cmp{b} 0 PULSE({-VOH_TL082} {VOH_TL082} {-e / 2:.9g} {e:.9g} {e:.9g} {duty * T - e:.9g} {T:.9g})")
        else:
            lines.append(f"Vcmp{b} cmp{b} 0 {-VOH_TL082 if duty <= 0 else VOH_TL082}")
        if tri_on:
            lines += [f"Rsw{rt} tri{b} n{rt} {RON_4016}", f"{rt} n{rt} bus{b} 47k"]
        lines += [f"{rsi} saw{b} n{rsi} 100k", f"{rss} n{rsi} 0 10k",
                  f"{rsl} n{rsl} 0 47k", f"{rso} n{rsl} bus{b} 3.3k"]
        if saw_on:
            lines.append(f"Rsw{rsi} n{rsi} n{rsl} {RON_4016}")
        lines += [f"{dx} cmp{b} n{rp} D1N4148", f"{rp} n{rp} bus{b} 360k",
                  f"{cx} bus{b} {pin} 2.2u", f"{rb} {pin} 0 680"]
    lines += [".options reltol=1e-6 abstol=1e-12 vntol=1e-9", f".tran {step:.4g} {t1:.9g} {t0:.9g} {step:.4g}", ".end"]
    return "\n".join(lines) + "\n"


def rms_pp(v):
    v = v[:-1]
    return math.sqrt(np.mean((v - np.mean(v)) ** 2)), np.ptp(v)


VCOF_200 = 0.0179 * math.log2(200 / (VTH / 806e3 / (2 * 1000e-12 * VTH))) * 4320 / 100
# Tolerance 1 %, the pulse paths 2 %: the model's comparator high is 10.5 V less 0.6 V, the deck's
# 1N4148 drops 0.43 V at 28 uA (+1.7 %). p-p only for one waveform at a time: where the saw's drop and
# the comparator's edge coincide, the peak depends on how the two edges overlap (the model's are the
# same band-limited step, the deck's two ramps 1/5000 of a period long).
for name, waves, pw1, pw2, tol in (("tri", 4 | 16, -0.2, -0.2, 0.01), ("saw", 32 | 2, -0.2, -0.2, 0.01),
                                   ("pulse 50 %", 0, VTH / 2, VTH / 2, 0.02), ("pulse 20 %", 0, 0.2 * VTH, 0.2 * VTH, 0.02),
                                   ("tri+saw+pulse", 4 | 16 | 32 | 2, VTH / 2, VTH / 4, 0.02)):
    f1, f2, ra, pa, rb, pb = map(float, model("bus", waves, pw1, pw2, f"{VCOF_200:.9g}", f"{VCOF_200 * 1.1:.9g}"))
    # One deck per bus so that each window holds whole periods of its own oscillator.
    res = []
    for hz, node in ((f1, "pin6"), (f2, "pin7")):
        T = 1 / hz
        t0 = math.ceil(1.5 / T) * T
        data = ngspice(bus_deck(waves, f1, f2, pw1, pw2, t0, t0 + 60 * T, T / 2000), [f"v({node})"],
                       control=f"run\nlinearize v({node})")
        res.append(rms_pp(data[:, 1]))
    (sra, spa), (srb, spb) = res
    # p-p only where one waveform has no pulse: the pulse's peak in the deck depends on where its
    # 1/2000-period steps fall against the coupling capacitor's droop (it moved 3 % for a 0.07-cent
    # change of frequency), so the pulse is held to its rms.
    combo = bool(waves & (4 | 16) and waves & (32 | 2)) or name.startswith("pulse")
    for pin, r, p, sr, sp in (("6", ra, pa, sra, spa), ("7", rb, pb, srb, spb)):
        if combo:
            check(abs(r / sr - 1) < tol, f"{name:13s} pin {pin}: {1e3 * r:7.3f} mV rms (ngspice {1e3 * sr:7.3f}; "
                  f"{100 * (r / sr - 1):+.2f} %)")
        else:
            check(abs(r / sr - 1) < tol and abs(p / sp - 1) < tol + 0.005,
                  f"{name:13s} pin {pin}: {1e3 * r:7.3f} mV rms, {1e3 * p:7.3f} mV p-p (ngspice {1e3 * sr:7.3f}, "
                  f"{1e3 * sp:7.3f}; {100 * (r / sr - 1):+.2f} %, {100 * (p / sp - 1):+.2f} %)")

amp, hz = 0.051, 1000
r, p = map(float, model("bus-noise", amp, hz))
deck = "\n".join(["* NOISE net to pin 7", "Vn nsrc 0 DC 0 AC 1", "R44 nsrc n44 47", "RX61 n44 n61 3.3k",
                  f"RonUX14a n61 busb {RON_4053}", "RX35 busb n35 3.3k", "RX34 n35 0 47k",
                  "Vcmp2 cmp2 0 -10.5", "DX04 cmp2 n37 D1N4148", "RX37 n37 busb 360k",
                  "CX33 busb pin7 2.2u", "RX40 pin7 0 680",
                  ".model D1N4148 D(IS=4.352n N=1.906 RS=0.6458)",
                  f".ac lin 1 {hz} {hz}", ".end"]) + "\n"
f, (hd,) = ac(deck, ["v(pin7)"])
sr = abs(hd[0]) * amp / math.sqrt(2)
check(abs(r / sr - 1) < 0.01, f"noise (51 mV peak at 1 kHz) pin 7: {1e3 * r:.3f} mV rms (ngspice {1e3 * sr:.3f}, "
      f"{100 * (r / sr - 1):+.2f} %)")

print("\n" + ("All checks passed." if failures == 0 else f"{failures} check(s) FAILED."))
sys.exit(1 if failures else 0)
