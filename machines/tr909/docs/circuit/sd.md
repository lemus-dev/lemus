# Snare drum (sd.cir)

The SNARE DRUM of the voicing board 73133090, from its TRIG latch output, its accent DAC and the
noise bus to its output at the connector W5 to the multi-jack board. Sources as `docs/circuit.md`
§0: **SN** p.11 (crop `private/docs/schematic/crops/p11-sd-snare-drum.png`, the full page for the
connections), p.6 (circuit description, the V_DD/V_SS figure), p.7 (change information), p.9 (MULTI
OUT waveforms), p.10 (address map), p.12 (multi-jack board); **NAVA** (`Nava_v1.02_netlist.txt`,
the SnareDrum nets); **OLD**, the hand-written model `private/reference/old-models/sd.cpp`;
**RES**, `docs/research.md` §2 "Snare drum". Every value below was read on the SN crop at 400 dpi
and agrees with NAVA unless a note says otherwise.

Address map (SN p.10): IC34a Q-2, IC34b R-2, IC35a/b P-3, IC36 P-5 and P-9, IC37a P-4, IC37b P-6,
IC38a P-8, IC38b P-10, IC39a Q-6, IC39b R-7, IC40a Q-5, IC40b R-12, Q39 P-2, Q40 Q-2, Q41 R-2,
Q42 R-3, Q43 Q-7, Q44 P-5, Q45 P-10, Q46 Q-9, Q47 R-7, Q48 R-5, Q49 Q-12, Q50 R-9, Q51 R-12, VR6
P-1, VR7 R-3, VR8 R-12, VR9 S-7. ("Q40 Q-2, I-14 (Q86)": the second Q40 of early boards is the
clap's, `rshc.md` §3; the snare's Q40 keeps its name.)

## 1. Inputs

- **trig.** SD is TRIG latch 1 (IC1, 4174 on +5 V) bit 2 (`docs/research.md` C.4). The netlist holds
  the latch's output resistance, `rlatch` 600 Ω, as the other voices' do.
- **acc.** SN p.11 draws IC34b pin 5 from **RA1**, IC2's RM0621 (the line runs down the left edge of
  the drawing to RA1 pin 15; RA2, IC3's, goes to the bass drum's IC11b). RES's table, from the p.4
  block diagram, gives IC2 to BD and IC3 to SD; the firmware writes SD's code to DAC2 at 4002
  (`docs/firmware.md`). The decoder (IC612/613) that picks the latch for 4002 is untraced, so which
  of IC2/IC3 is DAC2 is open; the netlist only sees `acc` (5 V · code / 63) through `rdac` 2.54 kΩ.
  The node goes to IC34b's (+) input alone: no other load.
- **noise.** R286's far end is the noise bus, straight from IC33 pin 9 (traced on the full page; it
  also feeds the toms' R190, the BD and the clap): `noise` 0-15 V, the bit stream averaged over a
  sample. The "NOISE" flag on the drawing (grid R-8) only names the snappy line C78-R294.
- **Codes.** 10 normal and 21 accented at TOTAL ACCENT 0; 21 and 47 at full TOTAL ACCENT (C.4).

## 2. Trigger: Q39, Q40, the PULSE line (SN grid P-Q 1-2)

trig → **R239 22K** → Q39's base, **R242 10K** to ground; Q39 (emitter grounded) pulls Q40's base
down through **R246 22K** (**R244 10K** base to +15 V). Q40 (PNP) has its emitter at +15 V through
**R245 100**; its collector is the **PULSE** line: +14.86 V for the trigger in the model. Q39's
collector (q39c) also gates the accent (§3). The PULSE line goes to, and only to (NAVA N$119):

- **D56** → R247 → C66: ENV1, the pitch bend (§4);
- **D62** → R275 → IC37a (+), **D63** → R274 → IC38a (+): forces both comparators high;
- **R258 1M** → Q44, **R267 1M** → Q45: short the integrators' capacitors through R263/R268;
- **R281 100K** / **R282 22K** → Q46, **R292 100K** / **R296 22K** → Q49: hold ENV3 and ENV2 at
  ground (mute the drum VCAs);
- **R266 100K** / **R265 22K** → Q43: holds Q47's base (the high-pass noise VCA) at ground.

With Q40 off the line has no pull-up: the three 122k base dividers hold it near ground, and while a
comparator's output is low its D62/D63 conducts from the line into the comparator's (+) node, which
pulls the line to −2.5…−3.6 V and lifts that comparator's lower threshold (§5). This is the coupling
between the two VCOs that OLD found ("they lock at 3:2").

R258's value isn't printed (SN shows only "R258"); NAVA gives 1M, as R267's printed value; OLD
assumed the same. The netlist uses 1M.

## 3. Accent: IC34, Q41, Q42, ENV4 (SN grid Q-R 1-4)

**IC34b** (+) acc; **R240 4.7K** (−) to ground, **R241 10K** (−) to output: ×3.13, `ac`
(2.48 / 5.21 / 11.67 V at codes 10 / 21 / 47; code 63's 15.6 V clips). **IC34a** inverts it ×0.1
(**R255 100K** in, **R243 10K** feedback, (+) grounded): `ac10` = −0.1 · ac, ENV4's discharge target.

**Q41** (PNP, emitter at ac) is the accent gate: its base goes through **R257 100K** and **D58** to
q39c, so it conducts while Q39 does, putting ac on its collector, the gated-accent line (NAVA
N$136), which charges ENV2, ENV3 and ENV5 through D67, D68 and D64 (§6, §7). SN p.6 calls this
"ACCENT coming through Q04 currently gated by the TRIG" (Q41).

**Q42** (PNP, emitter at ac) charges ENV4 through **D60**. Its base is held down after the trigger:
**R256 1M** to node c68n, **C68 0.0068** from ac to c68n, **D61** from c68n to q39c. During the
trigger D61 holds c68n at about 0.6 V; after it, C68 holds c68n while Q42's base current runs it up
through R256, so Q42 goes on conducting for about 24 ms (the model: ENV4 within 0.1 V of its peak
from 4 to 23 ms after the trigger at TONE 0, code 10), the "flat" top RES quotes from Whittle.
Neither base has a resistor to its emitter: at rest Q41 and Q42 are off only because D58's and
D61's reverse leakage (from q39c at 15 V) lifts their bases (§11, the solver's node shunts).

**ENV4** is **C67 0.47/50** (bipolar, "BP"), with **D59** (anode at ground) clamping it at about
−0.3…−0.6 V, discharged through **VR7 500K(B)** TONE (a rheostat: wiper to the R254 end, NAVA
VR7.E/S) and **R254 100K** towards IC34a's output, and loaded by **R297 220K** into Q48's base. The
target below ground is why C67 is bipolar and why the decay "runs faster than exponential"
(RES, Whittle): the VCA cuts off at about 0.5 V on a curve heading for −0.25 V (code 10). τ is
C67 · (R254 + VR7) in parallel with R297's load: about 32 ms at TONE 0 and 76 ms at full (RES).
TONE acts on ENV4 only, so it does nothing with SNAPPY at 0 (RES, measured on AR).

## 4. ENV1 and the 4069 rails: IC35 (SN grid P-Q 1-3)

The PULSE line charges **C66 0.01** through **D56** and **R247 47K** (τ 0.47 ms). **TUNE VR6 10K(B)**
sits between **R287 100K** (from +15 V) and **R284 10K** (to ground): its wiper spans 1.25-2.50 V.
IC35a's (+) sums the wiper through **R249 100K** and ENV1 through **R248 470K** (82 % / 18 %); IC35a
is a follower whose (−) senses V_DD after **R250 1K**, so V_DD equals its (+) whatever IC36 draws;
IC35b inverts V_DD (**R251**, **R252 100K**) and senses V_SS after **R253 1K**: V_SS = −V_DD. V_DD
rests at the wiper (1.87 V at TUNE 0.5) and rises to 4.1 V during the 2 ms trigger (ENV1 13.4 V),
falling back with C66 · (R248 + R249) ≈ 5.7 ms: SN p.6's figure, "2V" to "5V" in 2 ms and back over
"20ms", drawn with V_SS mirrored. **D57 RD18JB2** (18 V zener across V_DD-V_SS) is left out of the
netlist: V_DD − V_SS stays under 11 V (as mt.cir's D34).

C72 and C74 (10/16, by IC38b on the drawing) decouple the block's ±15 V and are left out: the
rails are ideal.

## 5. VCO-1 and VCO-2: IC36-IC38 (SN grid P 3-11)

**VCO-1.** Comparator **IC37a**: (+) node with **R260 100K** from its output and **R262 100K** to
ground, and **R275 10K** from D62; (−) the triangle through **R269 100K**. Lightly loaded (R260 +
R262, and R259 into clamped inputs), it swings ±14 V (`opsatlight`, the data book's typical into
10k, as the toms' comparators), so with D62 off its thresholds are ±7.0 V. Its output drives three
of IC36's inverters in parallel (inputs 9, 11, 13, outputs 8, 10, 12; pin 14 V_DD, pin 7 V_SS)
through **R259 100K**; their protection diodes clamp the inputs at 0.6 V beyond the rails, the
0.11 mA through R259 going into V_DD or V_SS, which IC35's feedback holds. The inverters' output,
V_SS or V_DD through about 200 Ω of channel (three gates at V_GS ≈ 4 V), drives the integrator
**IC37b** through **R261 47K** into **C69 0.01**: a triangle sloping at V_DD / (R261 C69), 2.13 V/ms
per volt of V_DD. **Q44** (emitter at IC37b's (−), collector through **R263 10K** to its output,
base from the PULSE line through R258) empties C69 for the trigger, leaving the triangle at +0.8 V,
from where it starts up as the trigger ends.

**VCO-2** is the same with **IC38**, inverters 1-2, 3-4, 5-6 (SN p.11 prints the inputs as "1.3.6",
a slip for 1, 3, 5), **R270**, **R271**, **R272**, **R273**, **R274**, **R264 47K**, **C71 0.0068**,
**Q45**, **R268**: 1.47 times VCO-1's frequency with the same thresholds.

**The inverters** are the solver's level-1 MOSFETs, `models.cir` 4069UB_N/_P (fitted by the solver's
owner to the HD14069UB typical table and TI's CD4069UB curves, for 3-10 V supplies), three in
parallel as one N and one P of three times the width (4069UB_N3/_P3), and the input diodes as one
DCMOS pair of three times the area (DCMOS3). The channels (about 600 Ω a gate at V_GS 4 V, against
47k) take 0.4 % off the integrating current at TUNE 0.5, 0.6 % at TUNE 0.

**The coupling** (§2). With both comparators high the PULSE line rests at 0 V and the thresholds
are +7.03 / −7.0 V; while one comparator is low its D62/D63 conducts from the line, which falls to
−2.6 V and lifts that comparator's lower threshold to −3.80 V; with both low the line is at −3.8 V
and each lower threshold −4.72 V. So the triangles run from +7.03 V down to −3.80 or −4.72 V
depending on the other VCO, about 30 % faster than ±7 V would give, and each VCO's flips move the
other's turning point: the pair pulls towards 3:2. In the model at nominal values the VCOs run at
1.453-1.455 times each other at every TUNE (unlocked); with C69 3.5 % over nominal they lock at
1.500-1.502 (§13). While the line is negative, Q44's and Q45's bases (1M from the line) sit at
about −2.6 V, so at the bottom of each triangle (below about −3.1 V) their base-collector junctions
conduct a few µA into the integrators: part of the circuit, and of its SPICE deck.

**V_DD** (TUNE 0.5): 1.875 V at rest, 4.10 V at the trigger's end, 3.20 V 3 ms later, 2.44 V at 8 ms,
1.98 V at 18 ms: the pitch bend.

SN p.6 says the pulse through D62 "turns [IC37a's] output to low"; the wiring drives its (+) input
high, so the output goes high, the inverters' outputs to V_SS, and after the trigger the integrator
ramps up. The netlist follows the wiring.

## 6. Shapers and drum VCAs (SN grid Q-R 7-12)

VCO-1's triangle → **R283 10K** → **D65/D66** (anti-parallel to ground) → **R288 1.5K** → the 47 Ω
node (**R289 47**) → **Q50**'s collector; VCO-2's → **R305 10K** → **D69/D70** → **R304 2.2K** →
**R303 47** → **Q51**'s collector. Q50 and Q51 (marked (S): `2SC2603S`, the 2SC2603 with a reverse β
of 6.5, `docs/circuit/s-transistors.md`) have their emitters at IC40b's
virtual ground (the symbols' arrows, and NAVA N$99): saturated transistors whose collector-emitter
conductance follows the base current.

- **ENV3** (Q50): the gated accent through **D68** onto **C83 0.1**, through **R291 220K** onto
  **C70 0.0047**, through **R290 22K** into the base. **Q46** (base from the PULSE line, R281/R282)
  holds C70 at ground for the trigger: "c) the base of Q46 which cuts off VCA Q50, muting unwanted
  noises in the VCO-1 path" (SN p.6).
- **ENV2** (Q51): **D67** onto **C82 0.047**, **R295 220K** onto **C79 0.01**, **R293 220K** into the
  base; **Q49** (R292/R296) holds C79 for the trigger. OLD calls both Q46 and Q49 mutes; SN p.6
  only mentions Q46.

## 7. Snappy (SN grid Q-R 3-8)

**Noise filter.** noise → **R286 10K** / **R285 3.3K** (÷4.03) → **C85 0.015** (**R302 220K** to
ground: high-pass 48 Hz) → **IC40a**, a unity-gain Sallen-Key low-pass: **R301**, **R300 22K**,
**C81 0.0033** to the output, **C84 220P** at (+): f₀ 8.5 kHz, Q 1.94.

**Path A (ENV4).** IC40a → **C80 0.022** → **R299 4.7K** → the 47 Ω node (**R298 47**; high-pass
1.5 kHz) → **Q48** (2SD1469) → IC39b's virtual ground; base from ENV4 through **R297 220K**.

**Path B (ENV5).** IC40a → **IC39a**, a unity-gain Sallen-Key high-pass: **C76**, **C75 0.0022**
("0.0022u x2"), **R276 6.8K** to the output, **R277 47K** (+) to ground: f₀ 4.0 kHz, Q 1.31 → **R278
2.2K** → **R279 47** → **Q47** (2SD1469) → IC39b's virtual ground. ENV5: the gated accent through
**D64** onto **C73 470P**, through **R280 220K** into Q47's base, which **Q43** holds at ground while
the PULSE line is up. ENV5 therefore opens Q47 only as the trigger ends: C73's charge runs into the
base in about C73 · R280 = 0.1 ms, a short burst of the high band (SN p.6: "ENV 5 determines the
amount of high frequency noise components in the SNAPPY").

**IC39b** sums Q47 and Q48: **VR9 10K(B)** SNAPPY as a rheostat in its feedback (wiper to the output
end, NAVA VR9.E/S), with **C77 10P**; then **C78 0.47/50** and **R294 47K** into IC40b.

## 8. Output (SN grid R 11-12)

**IC40b** sums the drum VCAs' and the snappy's currents; **VR8 50K(B)** LEVEL is its feedback (a
rheostat, wiper to the output, NAVA VR8.E/S), nothing across it. Its output is the voice's output,
straight to W5/CN-5 pin 34 (NAVA SD_OUT). The netlist ends with the multi-jack board's load (p.12),
as the other voices': **R503 1K** to the jack, **C502 0.01** there (SN 415300 up; `c502`), and the
jack's normal contact's **R504**, **R505 12K** to the mix buses' virtual grounds. IC40b's output is
stiff: the load changes nothing at `out`.

## 9. Production changes

SN p.7 lists none for the snare. The jack board's C502 came in at SN 415300 with C500-C512 ("Noise
is high in OUTPUT"); early units: `c502=1p` (the solver takes no 0 F capacitor).

## 10. Against SN p.6, NAVA, OLD and RES

- Every part value and connection agrees with NAVA (late board), including R258 1M.
- SN p.6: the D62 sentence (§5) and "Q04" (Q41) are slips of the text; "1.3.6" (§5) of the drawing.
- RES's accent-DAC table (§1) disagrees with the p.11 wiring (RA1, IC2).
- RES (from Whittle) "ENV5 is the same irrespective of the accent level ... VCA Q41": D64 hangs on
  Q41's collector, so ENV5 follows the accent as ENV2-ENV4 do; its VCA is Q47 (RES itself sides with
  Roland).
- OLD takes C71 4 % under nominal so that the PULSE-line coupling locks VCO-2 to 3:2 (AR's 1.500);
  the netlist keeps the nominal 0.0068 µF, and the calibration takes C69 instead (§15).

## 11. The netlist

**Op-amps.** All M5218L (`models.cir`). IC34b (the accent can clip it), the comparators IC37a/IC38a
(±14 V, `opsatlight`), IC39b and IC40b (SNAPPY and LEVEL at full can drive them to the rails) have
the output limit and the slew rate; IC34a, IC35a/b, the integrators IC37b/IC38b and the filters IC39a/IC40a are plain single
poles (their outputs stay within ±11 V and slew under 0.1 V/µs).

**Partitions** (`.part`, `tr808/docs/model.md` "Partitions"), in order: `trig` (Q39, Q40 and every
load of the PULSE line and q39c as a cut element), `rails` (ENV1, IC35), `acc` (IC34, Q41, Q42, ENV4
and the gated accent line's diodes and capacitors), `vco1`, `vco2`, `shape1`, `shape2` (the diode
pairs, which switch with the free-running triangles), `noise` (C85/R302 and IC40a, at twice the
voice's rate, below), `snappy` (IC39a, Q47, Q48, IC39b; it sleeps), `out` (the drum VCAs and their
envelopes, IC40b, the jack load). A resistor or diode across a cut is listed on both sides.

**The noise filter at twice the rate.** At the voice's 88.2-96 kHz the trapezoidal rule moves
IC40a's 8.5 kHz peak down and compresses the octave above it toward Nyquist: the snappy's tail
(60-200 ms, re its 2-4 kHz part) came out 1.4 / 2.3 / 3.6 dB under the same netlist at 384 kHz at
10 / 12.7 / 16 kHz, darker than three units (§13). `.part noise over=2` steps C85/R302 and IC40a
twice a sample, on the noise generator's two halves of each sample (`NoiseGenerator::next(n, sub)`),
IC40a's output the cut; `snappy` takes it once a sample. Over 4 gains under 0.6 dB more. What the
board's rate still costs: at the jack 0.4 / 0.7 / 1.1 dB at 10 / 12.8 / 16 kHz (each 96 kHz
partition's input moving linearly across its step, and the jack's R503/C502), at the master out
about 2.5 dB over 16-24 kHz (the mix board's too).

**The node shunts.** The solver used to put 1 GΩ from every node to ground (`circuit.cpp`,
kGshunt, mirrored in the deck by `rshunt`). On the bases of Q41 and Q42, which have no resistor to
their emitters and rest on islands that only D58's and D61's leakage lifts, those shunts drew a few
nA more than the leakage, which the PNPs amplified: the gated-accent line rested at 0.7 V (code 10),
ENV4 above its target, and from code 21 up Q47, Q48 and Q50 passed noise and tone at rest (a hit's
tail settled 30 dB under its peak). The solver now shunts only nodes with no DC path at all
(`docs/circuit/solver-mosfet.md` §2): at rest both PNPs are off, their bases at 15.0 V, ENV4 at
−0.190 / −0.258 / −0.312 V at codes 10 / 21 / 47 (ngspice the same), and a hit's tail falls to
−77 dB under its peak within 1.2 s.

The 1SS133's real reverse current is larger than the model's (IS 1.45 nA): about 11 nA at 13 V,
25 °C (Rohm sheet, Fig. 2), more in a warm unit. As current sources on D58 and D61 it shortens
ENV4's hold (the snappy's t-20 by 2-12 ms), away from the recordings (§13). The netlist leaves it
out.

**The comparators.** At 96 kHz the VCO comparators (100k/100k positive feedback, 2.2 V/µs, 23 V of
slew a sample) used to flip by themselves: the step's equations had a second, self-consistent
solution with the output already swinging. The solver now integrates a comparator's pole by the
trapezoidal rule and redoes a flip's sample in sixteenths (`solver-mosfet.md` §3): no spurious
flips, and one VCO alone runs within 0.01 % of ngspice's period.

**Rest.** The VCOs run all the time (the trigger resets them). After a hit the drum VCAs' bases
creep down through their envelopes' last nanoamps (C83 and C82 can only discharge through the
bases once these are below their thresholds), so the VCO tones leak at the jack: 0.46 mV RMS at
0.7 s, 0.25 mV at 1.1 s, 0.13 mV at 1.8 s after a code-10 hit (τ about 0.9 s), with the noise
through Q47/Q48 under it. A voice-level sleep floor under 0.5 mV keeps the snare awake for seconds
after each hit.

## 12. SPICE

`tools/spice/cases/sd.py` through `tools/spice/suite.py` (96 kHz, the trigger's edges substepped as
the board does; ngspice with reltol 1e-6, its step at most a sample, started from the solver's
state): the whole netlist against ngspice on the same deck. The
noise is a sum of six sines (1.1-11.3 kHz) around 7.5 V; acc = 5 V · code / 63; a 2 ms trigger at
10 ms; the difference in % of each output's range, RMS from the trigger on.

| Case | out RMS % | out max % |
|---|---|---|
| sd_noise0 (code 10, noise held at 7.5 V) | 0.023 | 0.12 |
| sd_10 / sd_21 / sd_47 (codes) | 0.100 / 0.080 / 0.064 | 0.68 / 0.57 / 0.59 |
| sd_tune0 (code 10) / sd_tune1 (code 21) | 0.101 / 0.077 | 0.70 / 0.55 |
| sd_tone0_snappy1 (code 10) | 0.124 | 1.21 |
| sd_tone1_snappy1 (code 21, 0.5 s) | 0.115 | 1.31 |
| sd_snappy0_level1 (code 47) | 0.024 | 0.13 |

The probes: ENV4 0.001-0.002 %, V_DD 0.004-0.13 %, IC39b's output (the snappy) 0.22-0.34 %, the
PULSE line 0.8-1.8 % (a 15 V edge a few µs apart), the triangles 0.46-1.1 % (1.8 and 3.8 % over the
0.5 s case: the free-running VCOs' phases drift by a few µs a flip, which the decaying output
hardly carries). Partitioned against whole (`TR909_FULL`, the board's noise, codes 10 / 21 / 47
over the knobs): 5 ms envelopes within 0.03-0.21 dB RMS (max 0.90 dB), 1/3-octave spectra within
0.04-0.50 dB.

## 13. Against the real 909

`tools/compare/sd.py`: hits through `tr909voicerun` at 96 kHz (the board's noise generator, a 2 ms
trigger, LEVEL 0.5 as AudioRealism's, the nominal parts: `--cal nominal=1`), at AR's grid code, **21**
(an accented step at TOTAL ACCENT 0: `real909.ar_code`), the output through the jack board's R503
1k into C502 0.01 µF as at a plugged MULTI OUT, and through AR's recording chain (`real909.chain`: a
first-order 6.5 Hz high-pass, §15), measured by `real909.py` as the recordings are. AudioRealism's
5 × 5 × 5 grid (AR, one late unit), its eleven TOTAL ACCENT takes, Rob Roy's dial settings (RR, a
second unit, normalised, its chain unknown and left out). Single takes on both sides: the snappy's
numbers scatter by a few ms. (The first version of this section rendered the grid at code 10; the
numbers below replace it.)

**Pitch** (SNAPPY 0, TONE 0.5; the strongest lines 30-150 ms; "early" the strongest line 2-20 ms):

| TUNE | VCO-1 model / AR | VCO-2 model / AR | ratio model / AR | VCO-2 re VCO-1 | early model / AR |
|---|---|---|---|---|---|
| 0 | 120.2 / 116.3 Hz | 175.0 / 177.4 | 1.456 / 1.525 | −11.0 / −10.2 dB | 169 / 149 |
| 0.25 | 149.7 / 144.1 | 217.4 / 217.0 | 1.453 / 1.506 | −10.5 / −9.8 | 202 / 166 |
| 0.5 | 179.5 / 173.4 | 260.9 / 260.1 | 1.453 / 1.500 | −11.0 / −9.8 | 213 / 198 |
| 0.75 | 208.9 / 200.9 | 303.5 / 301.2 | 1.453 / 1.499 | −10.5 / −9.8 | 239 / 229 |
| 1 | 238.1 / 230.7 | 346.3 / 346.1 | 1.454 / 1.500 | −10.6 / −9.9 | 270 / 259 |

VCO-1 runs 2.6-4.0 % sharp, VCO-2 within −1.4…+0.8 %. The model's VCOs run free at 1.453-1.456
times each other, as RR's do (1.430-1.446); AR's are locked at 3:2. TUNE's law is straight on both
(AR's VCO-1 rises 27.8 / 29.2 / 27.5 / 29.9 Hz a quarter turn, the model's 29.5 / 29.8 / 29.4 /
29.2), the span ×1.98 on both; RR's ×1.79 (its dials aren't rotations). RR's VCO-1 at dial 0 / 3 /
7 / 10: 131.2 / 147.3 / 206.3 / 234.3 Hz, the model's at rotation 0 / 0.3 / 0.7 / 1: 120.2 / 155.6
/ 203.0 / 238.1.

**Drum decay** (SNAPPY 0): t-20 74-83 ms against AR's 84-88, t-40 154-159 against 147-148; the first
10 ms over 10-50 ms (RMS) 6.2-8.1 dB against 6.8-8.7. AR's t-40 sits 3 dB above its own floor
(mains hum, −43 dB re the peak by 160 ms), so it is the floor's more than the drum's.

**Snappy** (TUNE 0.5; above 2 kHz; t-20 / t-40 ms, model : AR):

| TONE | SNAPPY 0.25 | 0.5 | 0.75 | 1 |
|---|---|---|---|---|
| 0 | 85/97 : 83/99 | 86/97 : 85/100 | 86/97 : 83/100 | 86/98 : 86/101 |
| 0.25 | 138/157 : 147/179 | 138/158 : 147/181 | 138/158 : 147/181 | 138/158 : 150/182 |
| 0.5 | 169/202 : 181/228 | 169/202 : 178/229 | 169/202 : 181/226 | 169/203 : 177/228 |
| 0.75 | 196/236 : 204/262 | 197/236 : 195/261 | 197/236 : 207/266 | 197/236 : 200/262 |
| 1 | 216/263 : 211/300 | 218/264 : 224/300 | 218/264 : 226/299 | 219/264 : 222/297 |

SNAPPY doesn't move the decay, on either side; TONE 0 matches; from TONE 0.25 up the model's t-40 is
10-13 % short, growing with VR7's share (§15). ENV4's hold is as Whittle measured (24 ms). RR (TUNE
dial 7, TONE 0, SNAPPY 10): 107/119 ms against the model's 87/98 and AR's 86/101.

The snappy against the drum: its part above 2 kHz (RMS 20-60 ms) re the hit's RMS 0-50 ms: −19.4 /
−13.6 / −10.4 / −8.3 dB at SNAPPY 0.25 / 0.5 / 0.75 / 1, AR −18.2 / −12.8 / −9.0 / −6.8: the model's
noise 1-1.5 dB low against its drum part. Over 0-60 ms at SNAPPY 1, re the drum's 150-210 Hz band:
2-5 kHz as AR's, 5-8 kHz 1-2.4 dB low, 8-16 kHz 3-4.5 dB low; so the centroid (0-60 ms) is lower,
1396 against 2072 Hz at SNAPPY 1.

**Level.** The model's RMS 0-50 ms in dBV against AR's in dBFS over the 125 grid points: one offset,
−14.3 dB, within −14.6…−14.0 (5-95 %); no knob moves it by more than 0.2 dB: LEVEL, TUNE, TONE and
SNAPPY's laws hold (AR's interface gain is unknown, so the offset itself says nothing).

**Accent** (TUNE 0.5, TONE 1, SNAPPY 1; RMS 0-50 ms re the 100 % take): the model at the takes'
codes (21, 21, 24, 20, 28, 31, 35, 37, 41, 47, 47) against AR: 0 % −6.0 / −6.1 dB, 20 % −4.9 /
−4.9, 40 % −3.6 / −3.7, 50 % −2.8 / −2.9, 60-80 % 0.5-0.8 dB under AR (the TOTAL ACCENT knob's
reading, not the snare), 30 % −6.4 / −4.0 (`docs/reference.md`: SD's 30 % take reads as TA 8-11, not
the table's dip). The centroid falls with the code as AR's does (1412 → 744 Hz against 2117 → 1136:
the drum part rising more than the noise). Regebro's four takes (codes 10 / 14 / 21 / 33 at TOTAL
ACCENT about half): +3.7 / +7.2 / +10.4 dB against +3.0 / +7.3 / +9.8.

**Spectrum.** Log-spectral distance (1/6 octave, 0-200 ms) to AR: 3.3-4.4 dB at SNAPPY 0, 3.0-3.9 at
0.5 and 1, mostly VCO-1's pitch (a line 3 % off moves across band edges).

## 14. CPU

`tr909voicerun --seconds 2`, `TR909_PROFILE=1 TR909_BLOCKS=1`, the snare's partitions summed. These
were taken on a loaded machine (load average 9-15 on 10 cores), so the times are high; the
iteration counts are not affected.

| Workload (2 s at 96 kHz) | trig | rails | acc | vco1 | vco2 | shape1+2 | snappy | out | total |
|---|---|---|---|---|---|---|---|---|---|
| a hit every 0.25 s, codes 10/21 | 0.06 s | 0.01 | 0.13 | 0.46 | 0.54 | 0.08 | 0.33 | 0.27 | 1.86 s (93 % of a core) |
| one hit, then rest (4 s run, per 2 s) | 0.06 | 0.01 | 0.06 | 0.43 | 0.54 | 0.08 | 0.25 | 0.12 | 1.55 s (77 %) |

Newton iterations a sample: vco1 2.0-2.5 and vco2 2.4-2.7 (42-70 % of samples linear: the flips,
the rails' sweep after a hit, Q44/Q45 conducting at the triangles' bottoms, and the PULSE line
moving at the other VCO's flips), snappy 2.0-2.6 (the noise keeps Q47/Q48's junctions moving),
out 0.4-2.7, trig 0.3-0.4, acc 0.02-0.3, rails 0. The VCOs never stop and the jack leaks their
tones for seconds after a hit (§11 "Rest"), so the voice stays awake.

## 15. Calibration

The unit Lemus plays is AudioRealism's (AR); `unitCalibration()` takes these (`core/voiceboard.cpp`,
merged by the lead):

    c.parts = {{"sd.c69", 0.01035e-6},  // VCO-1 3.4 % sharp; with it the VCOs lock at 3:2 as AR's
               {"sd.vr7", 600e3}};      // TONE's pot +20 %: the snappy's tail at TONE 0.25-1

No knob laws: TUNE is straight on AR (§13), TONE's error is uniform over its turn (a value, not a
taper), LEVEL and SNAPPY move AR's level offset by under 0.3 dB. The netlist's parts as printed stay
the defaults; `sd.cir` has the candidates as `.param`s (c66 c67 c69 c71 vr7 r248 r249).

**The recordings' chain.** AR's drum part starts with a negative DC step (the drum VCAs' base
current, −0.38 of the peak), which AR's recording overshoots to +0.08 at 40-60 ms and brings back by
150 ms; the model's, DC-coupled to the jack as the 909 is, returns smoothly. A first-order high-pass
on the model's output matches AR's waveform below 100 Hz best at 6-7 Hz over six settings
(residual 1.2 % of the peak; second order worse), and with it the low band's energy over 60-150 ms
(9 dB short without). It is AR's interface, not the 909: `real909.chain` (6.5 Hz) applies it to the
model's side of every AR comparison. It moved the drum's t-20 from 64-66 to 74-83 ms (AR 84-88).

**C69 +3.5 % (0.01035 µF).** Fitted to VCO-1 at TUNE 0.5 (179.5 → 173.9 Hz; AR 173.4); the other
TUNEs held out. R261 +3.5 % does the same (VCO-1 hangs on R261·C69); a capacitor's 10 % is the likelier.
The lock is a prediction, not fitted: with VCO-1 3.5 % lower the free ratio comes near 1.5 and the
PULSE-line coupling (§5) pulls VCO-2 onto 3:2 at every TUNE.

| TUNE | VCO-1 nominal / calibrated / AR | ratio nominal / calibrated / AR | LSD (SNAPPY 0) nominal / calibrated |
|---|---|---|---|
| 0 (held out) | 120.2 / 116.9 / 116.3 Hz | 1.456 / 1.502 / 1.525 | 3.9 / 2.3 dB |
| 0.25 (held out) | 149.7 / 145.5 / 144.1 | 1.453 / 1.501 / 1.506 | 4.4 / 2.5 |
| 0.5 (fitted) | 179.5 / 173.9 / 173.4 | 1.453 / 1.501 / 1.500 | 3.3 / 2.6 |
| 0.75 (held out) | 208.9 / 202.2 / 200.9 | 1.453 / 1.501 / 1.499 | 4.1 / 2.6 |
| 1 (held out) | 238.1 / 230.4 / 230.7 | 1.454 / 1.501 / 1.500 | 4.1 / 2.9 |

VCO-2 moves by under 0.3 % (it was right). Against RR, an unlocked unit (1.430-1.446), the lock takes
the model away: its ratio 1.453-1.456 becomes 1.500-1.501; RR's VCO-1 at its dials (131.2 / 147.3 /
206.3 / 234.3 Hz) against the model at 0 / 0.3 / 0.7 / 1 (nominal 120.2 / 155.6 / 203.0 / 238.1,
calibrated 117.0 / 151.2 / 196.5 / 230.4) says little, its dials not being rotations.

**VR7 +20 % (600k).** The snappy's tail is right at TONE 0, where VR7 is out of ENV4's path, and its
t-40 10-13 % short from TONE 0.25 up: VR7's value, at the pot's tolerance. C67, ENV4's capacitor
(+10 %: 0.52 µF), would lengthen TONE 0 too (91/105 against AR's 86/101) and fits worse; 650k fits
t-40 better and t-20 worse. Fitted on SNAPPY 1; SNAPPY 0.25-0.75 held out (the decay doesn't depend
on SNAPPY, so they check little but the noise); TONE 0 and RR (TONE 0) are untouched by it.

| TONE (SNAPPY 0.5, held out) | nominal t-20/t-40 | calibrated | AR |
|---|---|---|---|
| 0 | 86/97 | 86/97 | 85/100 |
| 0.25 | 138/158 | 144/168 | 147/181 |
| 0.5 | 169/202 | 182/217 | 178/229 |
| 0.75 | 197/236 | 205/254 | 195/261 |
| 1 | 218/264 | 225/283 | 224/300 |
| mean error | 3.2 % / 9.8 % | 2.2 % / 4.8 % | |

**Not fitted.**

- *The pitch bend* (C66 −25 % before). With C69 the early line is 3-10 % above AR's (164 / 181 / 206
  / 237 / 267 against 149 / 166 / 198 / 229 / 259 Hz), VCO-1's first period after the trigger
  1.44-1.47 times its settled pitch against AR's 1.36-1.37. C66 0.0075 µF brings the early line to
  −3…+5 % but raises the spectral distance from 2.3-2.9 to 2.9-3.2 dB and is outside a 10 % part;
  R248 larger lengthens the bend (its area goes as R249·C66); R249 −25 % would do as C66, a 5 % part.
  The evidence is one measure, a strongest-line pick over 2-20 ms that mixes the two VCOs: left.
- *C67* is no longer called for: §13's first version rendered the grid at code 10, where ENV4
  starts lower; at code 21 TONE 0 matches.
- *The drum's decay.* t-20 77-78 ms against 84-88 (−8…−12 %), the first 10 ms over 10-50 ms 7.0-8.7
  dB against 6.8-8.7 (right), the envelopes within 1-2 dB to −40 dB. C83 and C82 +10 % bring t-20 to
  79-85 ms but the first 10 ms 0.5 dB low and the spectral distance unchanged: left.
- *The noise against the drum*: 1-1.5 dB low (−13.3 against −12.8 dB at SNAPPY 0.5, −8.0 against
  −6.8 at 1; centroid 0-60 ms 1476 against 2072 Hz at SNAPPY 1). `s-transistors.md` finds the drum
  part 2.2 dB over the other (S) voices with the snappy on AR: the drum is hot rather than the noise
  quiet. Its level goes as (ENV3 − V_BE)/(R291 + R290), R289's 47 Ω and the shaper's diodes: no part
  within tolerance gives 2 dB.
- *The noise's top octave* (2026-10-22) was the board's rate, not the circuit: its tail's shape
  (60-200 ms, 1/3 octaves re 2-4 kHz) at 10 / 12.7 / 16 kHz is −4.5 / −11.1 / −17.2 dB at 96 kHz,
  −3.1 / −8.8 / −13.6 with the board at 384 kHz, and the units −2.3 / −8.2 / −13.5 (AR), −0.4 /
  −5.2 / −9.5 (Rob Roy), −1.2 / −6.1 / −10.9 (scherer.de's `909.rar`). With the filter at twice the
  rate (§11): −3.5 / −9.4 / −14.5; the log-spectral distance to AR 0.1-0.2 dB lower at SNAPPY 0.5
  and 1. IC40a's parts are as printed.

**Validation** (for `docs/model.md`; AR's grid at code 21 through its chain; RR at its dials):

| Measure | nominal | calibrated | AR | RR | Roland's plugin |
|---|---|---|---|---|---|
| VCO-1 at TUNE 0 / 0.5 / 1, Hz | 120.2 / 179.5 / 238.1 | 116.9 / 173.9 / 230.4 | 116.3 / 173.4 / 230.7 | 131.2 / – / 234.3 (dial 0, 10) | 112.4 / 172.6 / 225.4 |
| VCO-2 / VCO-1 | 1.453-1.456 | 1.500-1.502 | 1.499-1.525 | 1.430-1.446 | 1.46-1.47 |
| early line (2-20 ms) re AR | +4…+22 % | +3…+10 % | | | |
| drum t-20, ms | 74-83 | 77-78 | 84-88 | 71-79 (no chain; model 64-66) | |
| snappy t-20 / t-40, TONE 0 / 0.5 / 1 (SNAPPY 0.5), ms | 86/97, 169/202, 218/264 | 86/97, 182/217, 225/283 | 85/100, 178/229, 224/300 | 107/119 (TONE 0, SNAPPY 10) | 74/87, 201/268, 234/323 |
| level offset over 125 grid points | −14.3 dB, 5-95 % ±0.3 | −14.3, ±0.3 | | | |
| noise above 2 kHz re hit, SNAPPY 0.5 / 1 | −13.6 / −8.3 dB | −13.5 / −8.2 | −12.8 / −6.8 | | |
| accent: 0 % take re 100 % (codes 21, 47) | −6.0 dB | −6.0 | −6.1 | | |
| Regebro's codes 14 / 21 / 33 re 10 | +3.7 / +7.2 / +10.4 | +3.6 / +7.1 / +10.4 | (Regebro) +3.0 / +7.3 / +9.8 | | |
| LSD 0-200 ms, 15 settings | 3.0-4.4 dB | 2.3-2.9 | | | |

**What remains.** The pitch bend's first milliseconds (3-10 % high); the drum part 1-2 dB hot
against the noise and the other (S) voices, and the noise's top octave 3-4 dB dark; the drum's t-20
8-12 % short; RR's unit is unlocked and its snappy at TONE 0 20 % longer (a unit difference).

