# Rim shot and hand clap (rs.cir, hc.cir)

The RIM SHOT and HAND CLAP of the voicing board 73133090, from their TRIG latch outputs and the
accent DAC they share to their outputs at the connector W5 to the multi-jack board, and the rim
shot's TRIG OUT. Sources as `docs/circuit.md` §0: **SN** p.11 (crops
`private/docs/schematic/crops/p11-rs-rim-shot.png`, `p11-hc-hand-clap.png`,
`p11-hc-adjustment-trimmer-tm.png`; the full page for the connections), p.7 (change information),
p.9 (MULTI OUT waveforms), p.10 (address map), p.12 (multi-jack and DIN boards); **NAVA**
(`Nava_v1.02_netlist.txt`, sheets RimShot and HandClap); **OLD**, the hand-written models
`private/reference/old-models/rs_hc.cpp`; **RES**, `docs/research.md` §3 A2.3/A2.4.
Every value below was read on the SN crops at 400 dpi and agrees with NAVA unless a note says
otherwise.

## 1. Shared: TRIG, the accent DAC IC7

- **trig.** RS and HC are TRIG latch 2 (IC10, 4174 on +5 V) bits 1 and 6 (`docs/research.md` C.4,
  from the firmware). Each netlist holds the latch's output resistance, `rlatch` 600 Ω (MC14174B at
  5 V: 0.88 mA typ, 0.51 min, at a 0.4 V drop; the value the other netlists use).
- **acc.** RS and HC share the accent DAC IC7 (RA6, RM0621; `interface.md` traces it, earlier notes had IC8), written as one byte at 4006. Its
  Thevenin source is `acc` (5 V · code / 63) through `rdac` 2.54 kΩ. On the board the DAC's node
  ("ACC", SN p.11 grid J-6 to J-11) goes only to IC48b pin 5 (RS) and IC26a pin 3 (HC): two op-amp
  (+) inputs, so the other voice's load on the node is nil, and each netlist holds the source alone
  (RES A2.2 put R496 on this node as a 2.5 % load: it is on IC26a's (−) input, to −15 V; §3).
- **Codes.** RS and HC have one key each and always use the accent base 50h: code **21** with the
  step unaccented or TOTAL ACCENT at 0, up to **47** on an accented step at full TOTAL ACCENT; 63 only
  from MIDI velocity 126-127 (firmware table 1F80, `docs/research.md` C.4). If both are triggered in
  one tick with different velocities, the last write sets both.

## 2. RS: rim shot (rs.cir; SN p.11 grid H-J 6-10)

Address map (SN p.10): IC48a J-7, IC48b J-6, IC49a I-7, IC49b H-7, IC50a J-9, IC50b J-10, Q63 I-6,
Q64 J-7, Q65 J-8, Q66/Q67 H-9, VR19 J-9.

**Trigger and excitation.** trig → **C111 0.018** → node na:

- **D89** (anode at na) → **R396 22K** → nb, **R395 22K** to ground. nb is the (+) input of all three
  resonators. D89 passes the rising edge only: nb gets one positive pulse, 2.11 V peak in the model
  (RES: 2.2 V, τ ≈ 0.26 ms); the falling edge leaves nb at 0.
- **R399 22K** → Q63's base, **R398 10K** to ground; Q63 (emitter grounded) conducts for the first
  ~0.25 ms of the edge.

**Accent buffer IC48b.** (+) from ACC; **R397 4.7K** (−) to ground, **R406 10K** (−) to output: ×3.13
(2.48 / 5.21 / 11.67 V at codes 10 / 21 / 47; code 63's 15.6 V clips).

**Envelope.** While Q63 conducts it pulls Q64's base down through **D90** (cathode at Q63's collector)
and **R400 47K**; **R402 1M** is Q64's base-emitter resistor. Q64 (PNP, emitter at IC48b's output)
charges **C119 0.047** through **R401 2.7K** (τ 0.13 ms): ENV reaches 4.97 V at code 21 (95 % of
IC48b's 5.21 V). C119 then runs down through **R403 1M** into Q65's base.

**Resonators.** Each a bridged T in a non-inverting op-amp's feedback: R1 from the output to (−),
C from (−) to the mid node and from the mid node to the output, R2 from the mid node to ground. The
op-amp's (+) is nb. H(s) = 1 + s R1 C / (s² R1 R2 C² + 2 s R2 C + 1): f₀ = 1 / (2π C √(R1 R2)),
Q = ½ √(R1/R2), peak band-pass gain R1 / (2 R2), decay τ = R1 C (RES A2.3).

| | Op-amp | R1 | C, C | R2 | f₀ | Q | τ | SN sketch | Model peak |
|---|---|---|---|---|---|---|---|---|---|
| F1 | IC48a | **R407 470K** | **C113, C112 0.01** | **R394 2.2K** | 495 Hz | 7.3 | 4.7 ms | T1 ≈ 2 ms, 30 Vp-p, 20 ms | ±13.5 V (clips) |
| F2 | IC49a | **R414 330K** | **C116, C115 0.027** | **R411 2.2K** | 219 Hz | 6.1 | 8.9 ms | T2 ≈ 4.5 ms, 15 Vp-p, 25 ms | ±7.7 V |
| F3 | IC49b | **R416 470K** | **C118, C117 0.0047** | **R404 2.2K** | 1053 Hz | 7.3 | 2.2 ms | T3 ≈ 1 ms, 30 Vp-p, 5 ms | ±13.5 V (clips) |

The sketches F1-F3 (SN p.11 grid K-6 to K-8) are what the model does: F1 and F3 rail-clip (30 Vp-p)
for their first cycles, F2 reaches 15.4 Vp-p. The clipping is inside each loop, so every op-amp
resonator's op-amp has the output limit and slew rate (`opsat`, `sr`).

**Sum and clipper.** F1 → **R408 12K**, F2 → **R415 12K**, F3 → **R417 3.3K** (12K struck through on
the drawing; SN p.7: 12k → 3.3k from SN 415300, "for giving the voice more realistic sound") into
node s; **D91/D92** (1SS133) anti-parallel to ground: s stays within ±0.66 V, near a square wave for
the first ~5 ms.

**VCA Q65.** s → **R423 12K** → Q65's collector, **R418 680** to ground (×0.054: ±35 mV at the
collector). Q65 (marked (S)) has its emitter at IC50a's virtual ground and its base fed from C119
through R403: a transistor attenuator in saturation. For a positive collector the current is
limited by the source (35 mV through 0.64 kΩ and the C-E resistance, about V_T / (β_R I_B)); for a
negative one it saturates at β_R I_B (reverse active), which flattens one half of the waveform at
the output. Q65's reverse β sets the voice's level (§6). The emitter takes part of the base current
into the virtual ground: −19 mV at IC50a's output 50 ms after a hit, which IC50b removes.

**Output.** IC50a (+ grounded): **VR19 100K(B)** LEVEL as a rheostat (wiper to the output end, NAVA
VR19.E/S), ∥ **C120 220P**: a low-pass at 14.5 kHz at centre, 7.2 kHz at full. Then IC50b:
**C121 0.01**, **C122 0.01** into (+), **R420 220K** (+) to ground, **R422 22K** from C121/C122 to (−),
**R419 4.7K** (−) to ground, **R421 4.7K** (−) to output. R422 returns to (−), which sits at V(+): a
unity-feedback Sallen-Key high-pass, f₀ 229 Hz, Q 1.58 (+4.4 dB at 256 Hz), times
1 + R421/R419, less R421/R422 times the C121/C122 node (out = 2.21 V(+) − 0.21 V(h)): a first-order
leak of 0.21 · s C121 R422 at low frequencies. NAVA confirms the topology and the 0.01s; RES
(Gearspace) "no highpass" is wrong for this board. IC50b → connector W5 pin 38.

The netlist ends with the multi-jack board's load on the connector, as the other voices' do
(`mix.cir` runs the jack board itself): **R515 1K** to the jack, **C506 0.01** there (SN 415300 up;
`c506`), and the jack's normal contact's **R516/R517 12K** to the mix buses (AC ground). IC50b's
output is stiff, so the load changes nothing at `out`.

**TRIG OUT** (SN p.11 grid H-9; SN p.1: "Rim Shot: +14 V, 20 ms pulse"). trig → **R392 1M** (22K
struck through) → Q66's base, **R393 1M** (10K struck through) to ground; Q66 (emitter grounded)
discharges **C114 0.1** (collector to ground) for the trigger's 2 ms. Q67 (PNP): emitter at +15 V
through **R495 1K** (SN415300-UP; a jumper before, the dashed brackets on the drawing), base at
**R410 47K** (to its emitter) / **R409 47K** (to Q66's collector). Q67 conducts until C114, charging
through R409 (τ 4.7 ms), no longer draws enough current to hold its base 0.6 V down: collector →
**R412 22K** to ground and
**R413 1K** → CN-4 pin 30 → the DIN jack board JK-2, where **C600 0.1** goes from pin 30 to ground at
the TRIG OUT (RIM SHOT) jack (SN p.7, SN415300-UP; p.12). The model: 14.27 V, above 7 V for 20.4 ms
after the trigger's edge, 10-90 % rise 0.53 ms (C600 through R413 and R495). `trigout` is the
jack's tip, unloaded.

**Production changes (SN p.7, SN 415300).** R417 12k → 3.3k; R392 22k → 1M, R393 10k → 1M and R495
1k for the jumper, with C600 0.1 µF "between terminal No. 30 and the ground (TRIG OUT jack)", "for
preventing RIM SHOT signal from being induced onto TAPE SYNC signal which otherwise may cause
glitches". The jack board's C500-C512 came in at the same serial ("Noise is high in OUTPUT").
The defaults are the late board; early units: `r417=12k r392=22k r393=10k r495=0.1 c600=1p
c506=1p` (the solver takes no 0 F capacitor). Early, the TRIG OUT's 32k loads the latch (trig sags
to 4.88 V) and its pulse is 16.6 ms above 7 V with a sharp edge.

**Params.** `rlatch` 600, `rdac` 2.54k, `r417` 3.3k, `r392` `r393` 1M, `r495` 1k, `c600` 0.1u,
`c121` `c122` 0.01u, `c506` 0.01u.

## 3. HC: hand clap (hc.cir; SN p.11 grid G-J 11-16)

Address map (SN p.10): IC26a J-11, IC26b G-13, IC28a H-16 (IC28b unused: NAVA joins its (−) and
output), IC29a I-12, b J-12, c J-12, d J-13, IC30 H-15, Q35 I-11, Q36 J-11, Q37 H-15, Q38 J-15,
Q86 I-14 ("Q40 Q-2, I-14 (Q86)": "On early PCBs two Q-40's exist one of which is denoted as Q-86 on
later PCBs"), VR20 H-16. Roland reused the TR-808's clap envelope almost part for part (RES A2.4;
`machines/tr808/docs/circuit.md` §10); the netlist reuses the 808's device models for the AN6912
and the BA662.

**Accent buffer IC26a.** (+) from ACC; **R206 4.7K** (−) to ground, **R212 10K** (−) to output, and
**R496 100K** from (−) to −15 V (the arrow; NAVA R496.1 on −VHC; OLD the same):
V = ACC · (1 + R212/R206 + R212/R496) + 15 V · R212/R496 = 3.23 · ACC + 1.5 V: 4.06 / 6.88 / 13.54 V
at codes 10 / 21 / 47, so the op-amp is at its limit (13.5 V) from code 47 up. "SN UP to 371499
SURFACE MOUNTED" marks R496 (dashed bracket) and C61/D53: fitted on the solder side on early units,
the same circuit.

**Trigger gate.** trig → **R215 22K** → Q35's base, **R216 10K** to ground; Q35 (emitter grounded)
pulls Q36's base down through **R210 22K**; **R211 10K** Q36's base to emitter. Q36 (PNP, emitter at
IC26a's output) puts IC26a's output on its collector for the trigger's 2 ms: into **R213 22K** /
**R218 22K** (to −15 V), whose tap n4 is comparator a's (−), and through **D52** into C59.

**Envelope generator, IC29 AN6912** (pin 3 to ground, pin 12 to −15 V; open-collector outputs that
sink to −15 V). Ladder **R219, R217, R214 4.7K** from ground to −15 V: m5 −5 V, m10 −10 V.

- **a** (4 −, 5 +, out 2): (−) n4, (+) m10. n4 sits at −15 V and rises to −4 V during the trigger, so
  a sinks for the trigger and discharges **C62 0.027** (**R229 1M** to ground, in the dashed box).
- **b** (8 −, 9 +, out 14): (+) C62, (−) m5: low while C62 is under −5 V, 2 ms + R229 C62 ln 3 =
  31.7 ms, the window. Its output sets the sawtooth's threshold p through **R227 2.7K** / **R228 5.6K**
  (−10.1 V in the window, 0 V after), drives comparator c's (−), and kicks Q86 at the window's end
  through **C64 0.001** / **R226 68K** (**D55** from −15 V clamps C64's far end).
- **c** (6 −, 7 +, out 1): (+) m5, (−) b's output: released in the window, so **R220 82K** (to ground)
  and **D54** (anode at c's output) pull ENV1 up; after the window c sinks and D54 is off.
- **d** (10 −, 11 +, out 13): (−) p, (+) ENV1 through **R225 1M**, with **C63 220P** from d's output
  to (+): when ENV1 rises past p, d lets go, **R222 10K** (to ground) lifts its output and, through
  **R221 18K**, Q86's base; C63 holds d released for ~0.2 ms (R225 C63 0.22 ms).
- **Q86** (emitter at −15 V) slams **C65 0.47/50** (+ to ground, ENV1 the − end) to −15 V through
  **R223 330** (τ 0.16 ms): the sawtooth's teeth. After the window, ENV1 runs up only through
  **R224 470K** + **R238 15K** (τ 0.23 s).

**The drawing error.** SN p.11 draws a junction dot where the R226 → Q86 line crosses the vertical
from d's output to R222, which would short R221. NAVA has {IC29 d out, C63, R221.1, R222} and
{Q86 base, R221.2, R226.2} as separate nets, as the TR-808's drawing (R358/R357 into the base): the
netlist follows NAVA (RES A2.4 the same). The board's copper settles it (SN p.10, read 2026-10-26 at
page px 2000-2230, 3555-3735): R222's pad joins C63 and R221's end toward IC29, R221's other end runs
on to Q86's base and R226; nothing joins the two. The dot is the schematic's slip.

**Q38** (2SA798 dual PNP, parts list "2SA798-G"). Common emitters from IC26a's output through
**R235 15K** (NAVA: R235.1 on the IC26a/Q36 net). Left base: Q86's collector through **R224 470K**,
**R238 15K** to ground (ENV1 · 15/485 when Q86 is off). Right base: the **TM 10K(B)** trimmer's
wiper (ends at ground and −15 V) through **R494 47K**, **R230 2.2K** to ground: 0 to −0.67 V. The
left collector is IC30's I_ABC (pin 1), directly (the 808 had R374 10k here); the right collector
goes to −15 V. So the OTA's current is R235's, split by ENV1 against TM: TM sets how far into the
exponential the teeth reach, the clap's spikiness and its level.

**Noise band-pass IC26b** (multiple feedback, + grounded). noise → **R208 47K** → node, **R209 10K**
to ground, **C42 0.0047** to (−), **C43 0.0047** to the output, **R207 150K** (−) to output:
f₀ 963 Hz, Q 2.13, −1.6 at f₀ (RES; Oden's 1140 Hz is not this circuit). The noise's 7.5 V mean
leaves no DC at the output. The noise generator's output resistance belongs to the noise netlist
(the line has several loads). The band-pass's partition runs at twice the voice's rate on the noise
generator's half-samples (`.part noise over=2`, as the snare's IC40a, `sd.md` §11): at 96 kHz the
trapezoidal rule takes its octave above 8 kHz 1-2 dB low against the same netlist at 384 kHz; at
twice the rate within 0.4-1.2 dB, the rest the voice's other partitions and the jack board.

**OTA path, IC30 BA662A.** IC26b → **C56 0.022** → **R202 39K** → pin 2 (−), **R201 1K** to ground
(×0.025, high-pass 181 Hz); pin 3 (+) **R236 1K** to ground; pins 4 and 7 to ground, pin 8 open
("4,7" at a ground symbol, "8P-NC"): the buffer is unused. Pin 6 → **R237 82K** to ground and
**C58 0.001** into IC28a's virtual ground (the current there high-passed at 1.94 kHz). Pin 9 +15 V,
pin 5 −15 V. The model is the 808's textbook OTA (`B... ota rs=450`, `tr808/docs/circuit.md` §16;
NAVA fits a BA6110). The solver's OTA has no output limit; with TM adjusted pin 6 peaks at 5.3 V
at code 21 and 9.6 V at codes 47 and 63 (IC26a limits), but a misadjusted TM (tm ≥ 0.4) drives it
past the rails.

**Reverb path, Q37.** IC26b → **C55 0.47/50** → **R204 1K** → **C57 0.47/50** to ground → **R205 3.3K**
→ Q37's collector, **R203 47** to ground (×0.014; ~80 Hz to ~440 Hz, RES). Q37 (marked (S)) has
its emitter at IC28a's virtual ground. Its base: D52 charges **C59 0.1** during the trigger; **R233
470K** → **C61 0.1** (**D53** anode at ground) → **R234 470K** → the base. Modes ~18 ms and ~123 ms (RES).

**Output.** IC28a (+ grounded): **VR20 50K(B)** LEVEL as a rheostat (NAVA VR20.E/S at the output),
nothing across it; → W5 pin 39. The jack board's load as RS: **R518 1K**, **C507 0.01** (`c507`),
**R519/R520 12K**.

**TM: the ADJUSTMENT.** SN p.11, by TM: "ADJUSTMENT LEVEL: Center, MULTI OUT 2Vp-p", with a
sketch of the four spikes. TM is the TR-909's only trimmer (parts list: H1051A013, 10K(B)
trimmer). The control `tm` is the wiper's place from the −15 V end (0) to the ground end (1). Its
default **0.255** comes from that procedure, `tools/compare/rshc.py --adjust`: LEVEL at centre,
code 21 (an unaccented step), the clap's peak-to-peak at the MULTI OUT (the output through the jack
board's R518 1k / C507 0.01 µF, 15.9 kHz) averaged over 8 runs of the noise generator, solved for
2.0 V (repeated runs of the procedure agree within ±0.005). The wiper is then at −11.2 V (1.90 kΩ),
the right base at −0.48 V. The p-p rises steeply:
it doubles from tm 0.22 to 0.26. OLD had the wiper 0.768 from the ground end (tm 0.232).

**Production notes.** SN p.7 lists no change to the clap but the jack board's C507 (SN 415300;
`c507=1p` before). The "SN UP to 371499 SURFACE MOUNTED" parts are on every board.

**Params.** `rlatch` 600, `rdac` 2.54k, `c65` 0.47u, `c507` 0.01u, `cmpvg` 0.5m (the comparators'
transition width, as the 808's).

## 4. Disagreements

| Point | SN | NAVA | OLD | RES | Netlist |
|---|---|---|---|---|---|
| R221 / Q86 base | dot shorts R221 | R221 in series | in series | in series | in series |
| R496 | (−) to −15 V | to −15 V | to −15 V | "to GND, loads the DAC 2.5 %" | to −15 V |
| C121/C122 | 0.01 | 0.01 | 0.047 (fitted) | 0.01 | 0.01 (`c121`, `c122`) |
| C65 | 0.47/50 | 0.47/50 | 0.36 (fitted) | 0.40 fits the timing | 0.47 (`c65`) |
| Q65, Q37 reverse β | — | — | 0.8, 3 (fitted) | — | models.cir's 2SC2603 (6) |
| Q86 β | — | — | 500 (the 4th tooth) | — | models.cir's (300) |
| IC30 | BA662A | BA6110 | OTA, 13 V swing | BA662A | OTA, no limit |
| TRIG OUT | Q66/Q67 | absent | absent | — | transcribed |

## 5. SPICE

`tools/spice/cases/rshc.py`; `suite.py --only rs,hc` at 96 kHz, the trigger's edges substepped as
the board does. Difference in % of the output's range:

| Case | What | Output | Range | Max % | RMS % |
|---|---|---|---|---|---|
| rs_21 | code 21 | out / trigout | 7.00 / 14.27 V | 0.84 / 0.07 | 0.024 / 0.003 |
| rs_47 | code 47 | out | 10.9 V | 1.12 | 0.024 |
| rs_10_level1 | code 10, LEVEL 1 | out | 7.65 V | 0.58 | 0.022 |
| rs_63_level1 | code 63, LEVEL 1 | out | 23.0 V | 0.97 | 0.023 |
| rs_21_level005 | code 21, LEVEL 0.05 | out | 0.70 V | 0.57 | 0.022 |
| hc_noise0 | code 21, no noise (Q37's base current only) | out | 4 mV | 0.01 | 0.002 |
| hc_21 | code 21 | out | 1.37 V | 55.7 | 0.48 |
| hc_47 | code 47 | out | 2.54 V | 29.7 | 0.35 |
| hc_63_level1 | code 63, LEVEL 1 | out | 4.87 V | 57.5 | 0.51 |
| hc_tm0 | TM at the −15 V end | out | 0.19 V | 0.32 | 0.013 |
| hc_tm1_level01 | TM at ground, LEVEL 0.1 | out | 1.42 V | 0.86 | 0.022 |

The rim shot agrees to 0.02-0.03 % RMS everywhere, the TRIG OUT to 0.002 %. The clap agrees to
0.01-0.02 % where its teeth don't move it (no noise, TM at the ends), and to 0.35-0.51 % RMS with
the noise: all of it in the 2nd to 4th teeth (20-45 ms), which come 7-37 µs early against ngspice
(0.3 % of the tooth period); the figure moves by ±0.15 % with any change to the circuit, as the
spikes it misplaces do. The comparators switch within a sample, which the solver can't place:
at 192 kHz the shift is 9-12 µs. ngspice itself moves the 2nd and 3rd teeth by 40 µs between its
1.3 µs and 0.33 µs steps (the 96 and 384 kHz decks), so both are within that of the converged
timing; outside the teeth the difference is under 0.01 %.

## 6. Against the real unit

The first comparison, through circuitrun and without AudioRealism's recording chain; §9 has the
board's numbers, through the chain, nominal and calibrated, which supersede these.
`tools/compare/rshc.py` (header: the measures). Code 21, LEVEL 0.5; the model's clap averaged over
8 runs of the LFSR noise (31 bits, taps 13/31, 455 kHz, RES A2.1). AudioRealism: 10 RS and 8 HC
shots at 96 kHz.

| RS | AudioRealism | Model | Model, C121/C122 0.047 µF |
|---|---|---|---|
| Lines (Hz, dB re the strongest) | 222 / 481 −8.0 / 1019 −18.7 | 219 / 493 −11.7 / 1052 −23.8 | 218 / 493 −8.7 / 1052 −19.5 |
| 220 / 480 Hz decay (ms) | 7.2 / 4.6 | 8.4 / 4.4 | 7.6 / 4.4 |
| −6 / −20 / −40 dB (ms) | 9.7 / 21.5 / 85 | 6.5 / 27.5 / 46 | 8.5 / 20.0 / 38 |
| Waveform correlation, 0-15 ms | | 0.65 | 0.88 |
| Slow lobe, 50-70 ms (% of peak) | +1.7 | 0.0 | 0.0 |
| Accent, code 47 against 21 | +4.3 dB | +4.1 dB | +4.1 dB |

| HC | AudioRealism | Model |
|---|---|---|
| Teeth (ms) | 0.2, 10.6, 21.9, 32.4 | 0.1, 12.9, 24.9, 32.3 (`c65=0.4u`: 0.1, 11.1, 22.6, 32.3) |
| Tail decay, 60-250 ms (ms) | 94 | 114 |
| Pulses over tail (dB) | 14.7 | 12.3 |
| 0-32 ms: peak, 2 / 5 / 8 kHz (dB) | 1155 Hz, −12 / −16 / −24 | 1340 Hz, −10 / −15 / −22 |
| 120-300 ms: peak, 2 / 5 / 8 kHz (dB) | 907 Hz, −18 / −35 / −46 | 970 Hz, −17 / −35 / −42 |
| Accent, code 47 against 21 | +5.1 dB | +6.0 dB |
| RS over HC (RMS of 200 ms) | +0.3 dB | +15.2 dB |

Accent sweep (AudioRealism accentNNN, dB re 0 %; codes as below):

| TOTAL ACCENT | 0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 | 100 % |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Code | 21 | 21 | 22 | 20 | 28 | 31 | 35 | 39 | 43 | 47 | 47 |
| RS recording | 0 | 0.0 | +0.8 | −0.3 | +1.7 | +2.3 | +2.7 | +3.3 | +3.9 | +4.3 | +4.3 |
| RS model | 0 | 0.0 | +0.3 | −0.3 | +1.7 | +2.2 | +2.8 | +3.3 | +3.7 | +4.1 | +4.1 |
| HC recording | 0 | +0.1 | +0.9 | −0.9 | +1.6 | +2.1 | +2.8 | +3.7 | +4.6 | +5.1 | +5.1 |
| HC model | 0 | 0.0 | +0.3 | −0.3 | +2.1 | +2.9 | +3.8 | +4.6 | +5.4 | +6.0 | +6.0 |

Other units (shape only; Rob Roy is normalised and trimmed at 40 ms): Rob Roy RIM127/63 220 / 459
−7.1 / 899 −22.3 Hz, 220 Hz decay 7.7 ms; archive.org Set2 RIMSHOT 225 / 477 −9.2 / 932 −23.2,
7.7 ms; Regebro RS 218 / 502 −8.7 / 1019 −18.5, 7.2 ms. Clap teeth: Rob Roy 0.3-0.4, 11.8,
24.3-25.8, 32.6-36.9 ms, tail 89-91 ms; Regebro 0.4, 10.6, 21.3, 30.8 ms, tail 92 ms.

**What matches.**

- **The accent.** The AudioRealism accent sweep, taken as the TOTAL ACCENT knob on an accented step
  (TA = ADC / 8, the ADC linear from 10 to 90 % of the rotation), through the firmware's table
  1F80 to the codes 21 22 20 28 31 35 39 43 47 at 10-90 %: the rim shot within 0.2 dB at every
  setting but 20 % (+0.28 against +0.81), and the "30 % outlier" (RES, `reference/real909/index.md`:
  1-3 dB low on BD, MT, RS and HC) is the table's entry 43 (28h, under entry 42's 30h): code 20,
  −0.30 dB in the model, −0.32 in the recording. Regebro's RS and CP take TOTAL ACCENT only
  (acc2 = acc4, +2.5 dB, acc3 = acc1), as the firmware's base 50h says.
- **The rim shot's lines and decay.** 219 / 493 / 1052 Hz against 222 / 481 / 1019 (F3 3 % high:
  C117/C118 3 % over 0.0047, or R416 · R404 7 % over, within the parts' tolerances; Regebro's unit
  218 / 502 / 1019);
  the 220 and 480 Hz lines' decay 8.4 / 4.4 ms against 7.2 / 4.6.
- **R417.** With r417=12k the model's third line falls to 925 Hz, −27 dB (3.3k: 1052 Hz, −24): the
  Rob Roy unit (899 Hz, −22 dB) and the archive.org set (932 Hz, −23 dB) are early boards, the
  AudioRealism and Regebro units (1019 Hz, −19 dB) late ones, as RES suspected.
- **The clap's spectrum.** IC26b's band: the late tail (120-300 ms) peaks at 970 Hz against 907,
  with −17 / −35 / −42 dB at 2 / 5 / 8 kHz against −18 / −35 / −46; the pulses' segment 1340 Hz
  against 1155 (with TM at 0.23 it is 1150-1180: the pulses outweigh the tail's 900 Hz more as TM
  rises), −10 / −15 / −22 dB against −12 / −16 / −24.
- **The clap's teeth** come where C65 puts them: 0.1, 12.9, 24.9, 32.3 ms with 0.47 µF; the
  AudioRealism unit 0.2, 10.6, 21.9, 32.4, Regebro's 0.4, 10.6, 21.3, 30.8, Rob Roy's 0.3, 11.8,
  24.3, 32.6. The fourth is the window's end in each (31.7 ms by R229 C62), the others C65's: with
  `c65=0.4u` the model gives 0.1, 11.1, 22.6, 32.3. C65 is an electrolytic (±20 %); the three units
  spread from about 0.40 to 0.45 µF.

**What doesn't, and candidates (not fitted).**

- **C121/C122 (the rim shot's shape).** With the printed 0.01 µF (229 Hz) the high-pass droops the
  clipped section's flat tops and overshoots at the end of the square part (a spike at 5.3 ms that
  no recording has); every recording (AudioRealism, Regebro, archive.org) has flat tops at half the
  negative peaks. With **0.047 µF** (49 Hz; OLD's value) the waveform's correlation with the
  AudioRealism shots over 0-15 ms rises from 0.65 to 0.88, the second and third lines come to
  −8.7 / −19.5 dB (recording −8.0 / −18.7; printed −11.7 / −23.8), the 220 Hz decay to 7.6 ms
  (7.2), and the envelope's −6 / −20 dB times to 8.5 / 20.0 ms (9.7 / 21.5; printed 6.5 / 27.5).
  SN and NAVA both print 0.01; a unit's C121/C122 would settle it.
- **The rim shot's level (Q65).** The AudioRealism set has every LEVEL at 50 % into one interface
  gain: RS is +0.26 dB over HC (RMS of 200 ms). The model, with TM adjusted, has RS 15.2 dB over HC
  (13.7 with C121/C122 0.047). Q65's level goes with its reverse β (§2): with a separate model
  for the two (S) transistors (Q65, Q37), β_R 2 / 1 / 0.5 takes 5 / 10 / 15 dB off RS − HC (the
  clap itself moves by less than 1 dB).
  The (S) mark isn't explained on the sheet; OLD fitted Q65's β_R to 0.8 for the same reason.
  models.cir's 2SC2603 has BR 6, a guess (the Micro Electronics sheet gives no reverse figures).
- **The clap's tail (Q37).** The tail decays with τ 114 ms against 94 (Rob Roy 89-91, Regebro 92),
  and the pulses stand 12.3 dB over the tail (RMS 0-35 ms over 60-120 ms) against 14.7. Q37's
  reverse β moves both: 2 / 1 / 0.5 give τ 95 / 80 / 66 ms and 18.9 / 21.5 / 22.9 dB; TM moves the
  second only. C59/C61 (0.1 µF) set the tail too.
- **The clap's accent.** +6.0 dB from code 21 to 47 against +5.1, and steeper in the middle
  (code 31: +2.9 against +2.1). The pulses go as IC26a's output (3.23 ACC + 1.5 V), the tail
  faster (Q37's base current is C61's voltage less V_BE). Candidates: IC26a's swing (an M5218 that
  limits at ±12.5 V, the data sheet's ±12 V minimum into 10k, gives +5.2 dB at 47; it doesn't
  explain the middle), Q37 (as above), R496.
- **The fourth tooth.** In every recording the fourth pulse is a full tooth with a long HF tail
  (the last sawtooth, decaying through R224/R238 only); the model's is a 0.2 ms spike: C64/R226's
  kick (68 µs) turns Q86 on too briefly to take C65 down to −14 V. Q86's β moves it (OLD fitted 500;
  2SC2603-F is 250-500 at 1 mA, models.cir has 300).
- **The rim shot's slow lobe.** All four units (AudioRealism, Regebro, archive.org, and Rob Roy's
  trimmed files as far as they go) have a positive lobe of 1.7 % of the peak (mean of 50-70 ms)
  that fades by 110-130 ms, after the ringing; it makes their −40 dB time 85 ms against the model's
  46. The netlist has nothing like it: Q65's base current gives IC50a a negative offset that IC50b
  removes (in any case of the wrong sign). It grows with the accent (AudioRealism: 1.8 % at 0 %,
  3.2 % at 100 %). Explained in §9.1: AudioRealism's chain (6.5 Hz) on a rim shot whose output
  has no high-pass.

## 7. CPU

Solver time per second of audio at 96 kHz (Apple Silicon, one core), a hit every 0.25 s with the
noise running, partitioned (`.part`) and whole:

| | RS | HC |
|---|---|---|
| Partitioned | 9.1 % of a core (91 ms/s) | 11.5 % (115 ms/s) |
| Whole | 17-27 % | 24-43 % |
| Partitioned, awake and idle | 7.6 % | 9.0 % |

Per partition, RS: trig 1.5 %, res 2.4 %, vca 2.4 % (Q65, 0.6 Newton iterations a sample, never
linear while C119 holds it on), out 0.7 %, tout 0.9 %; HC: noise 0.6 %, trig 1.4 %, env 2.5 %
(the comparators), ota 3.5 % (Q38 and IC30, 1.6 iterations a sample: the OTA's input port is never
linear with the noise on it, so it doesn't sleep while the voice is awake), out 2.0 % (Q37). The
resonators and the envelope run in linear mode 99 % of the time. Only the op-amps that clip have
the output limit and slew rate (IC48a/b, IC49a/b, IC50b; IC26a): IC50a's limit alone cost the
rim shot's vca partition half its time. The timings vary by 20 % with the machine's load.

Partitioned against whole (a hit every 0.5 s for 2 s): RS within 0.003 % RMS of its range
(envelope 0.01 dB), the TRIG OUT 0.03 %; the clap 0.36 % (envelope 0.2 dB), its chaotic teeth
again. Asleep (the board's sleep) they cost nothing.

## 8. Open questions

- The rim shot's output: IC50a's on the units recorded (§9.1), IC50b's high-pass on SN and NAVA.
  The board's copper (SN p.10, read 2026-10-26) wires IC50b as the schematic draws it: the M5218L's
  pins (p.11's inset: 1 out a, 2 -a, 3 +a, 4 -15 V, 5 +b, 6 -b, 7 out b, 8 +15 V) run, top to
  bottom, to Q65's emitter (pin 2), R419's and R420's grounded ends (3), R420 (5), R421 and R419
  (6), R421 again (7) and the +15 V strip (8). So the drawn board has IC50b; the recordings'
  units behave as if it weren't there, which the board can't explain (it doesn't show values: C121,
  C122 or R421 other than printed would).
  A photo or trace of a voicing board around IC50 would settle it.
- The clap's fourth tooth (§9.6), and the first interval of its teeth.
- The clap's late tail and its rest (§9.5): Q37's reverse β at 10-100 nA (ISC in `2SC2603S`).
- The BA662A's output limit (the solver's OTA has none).

## 9. Calibration (the AudioRealism unit)

**Method.** Hits through `tr909voicerun` (the board as the plugin plays it, 96 kHz, a 2 ms trigger,
LEVEL 0.5, the MULTI OUT jack), at AR's codes (`real909.ar_code`: 21 for the shots, 20-47 for the
accent takes), the clap over 8 stretches of the board's noise generator; the model's side through
AR's recording chain (`real909.chain`, 6.5 Hz first order). Levels against the other voices: AR's
interface gain is taken from the BD and the three toms at the grid's centre, model minus AR, RMS of
0-50 ms, +13.65 dB (sd 0.42 over the four): a voice at 0 dB below sits with them (their level is
their (S) VCAs', `s-transistors.md`). `tools/compare/rshc.py` prints nominal against unit;
`--fit rsout|rslines|tm|c65` renders the grids below. Fitted on AR's 10 RS and 8 HC shots; held
out: AR's 11 accent takes, Regebro's unit, the archive.org set, Rob Roy's.

**The entries for `unitCalibration()`:**

    c.parts = {
        {"rs.r421", 1e-3}, {"rs.c121", 100e-6}, {"rs.c122", 100e-6}, // the output is IC50a's: no IC50b high-pass, gain 1 (9.1)
        {"rs.c112", 0.01027e-6}, {"rs.c113", 0.01027e-6},             // F1 on the unit's 481 Hz, +2.7 % (9.2)
        {"rs.c115", 0.02646e-6}, {"rs.c116", 0.02646e-6},             // F2 on 222 Hz, -2.0 %
        {"rs.c117", 0.00485e-6}, {"rs.c118", 0.00485e-6},             // F3 on 1019 Hz, +3.2 %
        {"hc.tm", 0.30},                                              // TM: the pulses' level against the other voices (9.3)
        {"hc.c65", 0.39e-6},                                          // the teeth's spacing, -17 % (an electrolytic) (9.4)
        {"hc.c57", 0.38e-6},                                          // the reverb's level, -19 % (an electrolytic) (9.5)
    };

No knob laws: LEVEL is the voices' only knob and AR has it at 50 % only; TOTAL ACCENT's law is the
firmware's (`real909.AR_ACCENT_TA`).

### 9.1 The rim shot's output stage (outside the drawing; strong evidence)

The rim shot as drawn is 7.0 dB over the other voices, its waveform's positive excursions are 1.19
of its negative ones (AR 0.56), its −40 dB time is half AR's, and it has none of the slow positive
lobe all the units show. One change takes all four: the output taken from IC50a, without IC50b's
high-pass and gain of 2.21 (`--fit rsout`, code 21, TM 0.30):

| | ratio | lobe, % of peak at 30 / 50 / 80 / 100 ms | −40 dB | ncc 0-15 ms | RS against the others | 21 → 47 |
|---|---|---|---|---|---|---|
| AudioRealism | 0.56 | 2.83 / 1.94 / 0.69 / 0.17 | 85 ms | | 0 | +4.29 dB |
| as drawn | 1.23 | 0 / 0 / 0 / 0 | 45 | 0.64 | +7.0 dB | +3.78 |
| C121/C122 0.047 µF | 0.82 | 0.7 / 0.1 / 0.0 / 0.0 | 42 | 0.96 | +5.4 | +3.74 |
| R423 27k, C121/C122 0.047 µF | 0.83 | 0.7 / 0.1 / 0.0 / 0.0 | 42 | 0.96 | −1.2 | +3.87 |
| no high-pass, gain 2.21 | 0.60 | 2.26 / 1.72 / 0.86 / 0.50 | 92 | 0.995 | +5.4 | +3.82 |
| **IC50a's output** (no high-pass, gain 1) | **0.60** | **2.26 / 1.72 / 0.86 / 0.50** | **92** | **0.995** | **−0.6** | **+3.82** |
| gain 1, C121/C122 1 µF (2.3 Hz) | 0.60 | 3.55 / 2.58 / 1.07 / 0.34 | 94 | 0.995 | −0.6 | +3.84 |

- **The lobe** is AR's chain on a rim shot that keeps its low end: the clipped sum has a net
  negative area (−1.4 ms × the peak over the first 20 ms, AR −1.2 through its chain), and Q65's base
  current leaves a negative offset at IC50a that decays with C119; the 6.5 Hz high-pass turns both
  into a positive lobe. With IC50b's 229 Hz high-pass nothing of it reaches the jack.
- **Held out.** Regebro's unit (rev 4, MP3): ratio 0.54, lobe 3.22 / 2.23 / 0.75 / 0.10 %, −40 dB at
  87 ms; the archive.org set: ratio 0.56, lobe 2.82 / 1.28 % at 30 / 50 ms; scherer.de's `909.rar`
  (`Rim_Ind`, 36 hits, its chain unknown): ratio 0.47, lobe about 1 % from 30 to 100 ms. Every
  recording has the flat tops at half the negative peaks. AR's accent sweep keeps its law (the stage is linear).
- **Elsewhere.** maffez's notes on the RD-9 (`papers/clones/mix_rd9_maffez`): "Rim Shot output
  follows the actual Roland boards", where the other voices "follow the Roland schematics"; a
  Gearspace post (RES A2.3, a search snippet): "no highpass". SN p.11 and NAVA draw IC50b with
  C121/C122 0.01 µF.
- **In the netlist's terms** IC50b becomes a follower without its high-pass: R421 1 mΩ, C121/C122
  100 µF. The level match (−0.5 dB against the other voices) is not fitted: no part was tuned for
  it. Out of every tolerance, and flagged as such: an assembly or revision difference from the
  drawing, kept because four measures of three units agree and nothing in tolerance does it
  (R423 27k gets the level but not the shape; C121/C122 alone the shape but not the level).

### 9.2 The resonators' lines (in tolerance)

`--fit rslines`: each line moves with its own C pair (f ∝ 1/C) and barely with the others'. On AR's
222.1 / 480.8 / 1018.6 Hz: C112/C113 +2.7 %, C115/C116 −2.0 %, C117/C118 +3.2 %, giving 221.8 /
481.0 / 1018.4 Hz. Regebro's unit has 218 / 502 / 1019 (F1 4 % over AR's), KB6's 216 / 484 / 1042:
the units spread ±3 %, as 5 % capacitors would. The lines' levels (−8.0 / −18.0 dB, AR −8.0 / −18.7)
and decays (220 Hz 7.5 ms, AR 7.2; 480 Hz 4.8, AR 4.6) come with 9.1, not with these.

### 9.3 TM (a trim)

The pulses (the OTA's path, 1.5-6 kHz, 0-30 ms) go through no (S) transistor and nothing but TM
sets their level; against the other voices (C65 0.40 µF, 6 runs):

| tm | 0.255 | 0.28 | 0.30 | 0.31 | 0.32 |
|---|---|---|---|---|---|
| pulses against the others, dB | −6.1 | −2.3 | +0.3 | +1.5 | +2.5 |
| p-p at the jack (LEVEL 0.5, code 21), V | 2.1 | 3.1 | 4.0 | 4.4 | 4.9 |

**0.30.** The service notes' procedure (2 Vp-p at the jack, LEVEL at centre) done on the model gives
0.255: the model's p-p is its teeth's narrow spikes, and AR's clap has 2.4 dB more RMS than the
model's at equal p-p (`s-transistors.md` §3.2), so the procedure reads the model low; the unit at
0.30 has 4.1 Vp-p (`tools/service/check.py`, 8 runs: 4.07, 3.1-5.6 from hit to hit; 4.0 in the
table's 6). Held out: the accent range, codes 21 → 47, +5.47 dB (AR +5.11; nominal +5.87),
and the levels in 9.6. `s-transistors.md` reached 0.31 from the 0-50 ms level, which carries the
reverb (1.7 dB low before 9.5) and the weak fourth tooth (9.6); the pulses alone say 0.30.

### 9.4 C65 (the teeth)

`--fit c65`: the teeth's times scale with C65 (τ = (R220 ‖ 485k) C65) but the first interval
hardly does (10.9-11.0 ms from 0.38 to 0.41 µF). AR's third tooth, 21.69 ms after the first (the
sum of two intervals), wants **0.39 µF** (21.62 ms), −17 %, inside an electrolytic's −20 %. The
teeth then come at 0.23 / 11.21 / 21.85 / 31.57 ms against AR's 0.21 / 10.57 / 21.90 / 32.38 (the
accent takes, held out: 0.06 / 10.53 / 22.29 / 33.0). The other units differ by their own C65:
Regebro 0.41 / 10.63 / 21.29 / 30.75, Rob Roy 0.3-0.4 / 11.8 / 24.3-25.8 / 32.6-36.9 (about 0.45 µF).

### 9.5 The reverb (Q37's path)

At TM 0.30 the reverb (150-1500 Hz, 120-200 ms) is 1.7 dB under the other voices; **C57 0.38 µF**
(−19 %, an electrolytic; the path's low-pass R204/C57, 339 → 419 Hz, at the 960 Hz band) takes it
to −0.4 dB, and changes nothing else (its decay 128 ms either way). C59 moves the level with the
decay the wrong way (0.12 µF: −0.3 dB but 149 ms), C61 the decay but not the level.

**The late tail and the "leak".** AR's clap keeps falling at about 70 dB/s through IC26b's band:
−53 / −67 / −82 / −96 dB re its peak at 0.3 / 0.5 / 0.7 / 0.9 s, and −105 to −109 by 1.1 s. The
model falls as fast at first and then stalls (−52 / −64 / −72 dB, then 1.3 mV p-p at the jack at
1 s, 0.3 mV at 2 s with the voice kept awake; the board's sleep cuts it at about 0.9 s): C61 runs
down through Q37's base only, whose current falls as e^(V/V_T), so its gain falls ever more
slowly. That is the noise "at rest": Q37, not the OTA, which passes 2 µV p-p with Q38 off (I_ABC
under 1 pA; the BA662A's "leakage level" at no control current is −94 to −100 dBm from −30 dBm at
20 kHz, the V662A and AS662D sheets, so its off state is 64-70 dB down even there). A fall of
Q37's reverse β at low current does what AR does: with ISC 1e-11, NC 2 on `2SC2603S` (the limit
`s-transistors.md` §3.6 allows) the late tail runs −59 / −74 / −86 / −93 / −98 dB at 0.3-1.1 s
(AR −58 / −72 / −86 / −100 / −109), the reverb's decay 128 → 115 ms (AR 122), and the clap's
t-20 / t-40 / t-60 123 / 426 / 904 ms → 92 / 369 / 688 (AR 106 / 381 / 690). The model card is
`s-transistors.md`'s; this is evidence for it from the clap, the only voice whose VCA runs down to
tens of nA.

### 9.6 Nominal and calibrated against the units

AR, code 21, LEVEL 0.5 (`rshc.py`); "others" is the level against the BD and toms (0 dB = with them).

| | AudioRealism | nominal | unit |
|---|---|---|---|
| RS lines, Hz (dB) | 222 / 481 (−8.0) / 1019 (−18.7) | 219 / 493 (−11.7) / 1052 (−24.0) | 222 / 481 (−8.0) / 1018 (−18.0) |
| RS 220 / 480 Hz decay, ms | 7.2 / 4.6 | 8.4 / 4.4 | 7.5 / 4.8 |
| RS −6 / −20 / −40 dB, ms | 9.7 / 21.5 / 85 | 6.5 / 27.5 / 46 | 9.8 / 21.5 / 92 |
| RS ratio, ncc | 0.56 | 1.19, 0.64 | 0.60, 0.998 |
| RS level against the others | 0 | +7.0 dB | −0.5 dB |
| RS accent 21 → 47 | +4.29 dB | +3.77 | +3.81 |
| HC teeth, ms | 0.21 / 10.57 / 21.90 / 32.38 | 0.08 / 12.64 / 25.08 / 31.78 | 0.23 / 11.21 / 21.85 / 31.57 |
| HC fourth pulse re the 2nd and 3rd | −1.2 dB | −4.4 | −7.2 |
| HC pulses / reverb against the others | 0 / 0 | −7.2 / −1.7 dB | +0.2 / −0.4 dB |
| HC 0-50 ms against the others | 0 | −8.4 dB | −1.8 dB |
| HC tail 60-250 / 120-300 ms, ms | 94 / 122 | 120 / 128 | 120 / 128 |
| HC pulses over the tail | 14.7 dB | 11.4 | 17.1 |
| HC accent 21 → 47 | +5.11 dB | +5.87 | +5.47 |

The accent sweeps (AR's takes, held out): RS within 0.3 dB to code 35 and 0.5-0.65 dB low from 37
to 47 (nominal the same: the output stage is linear); HC within 0.6 dB at every take (nominal 0.8),
high at codes 20-35.

**Other units** (held out; normalised or MP3, so shape only). Regebro (late, rev 4): RS lines 218 /
502 / 1019 Hz, ratio 0.54, −40 dB at 87 ms; clap teeth 0.4 / 10.6 / 21.3 / 30.8 ms, fourth +0.6 dB,
tail 92 ms. Rob Roy (an early board, R417 12k): RS 221 / 459 / 899 Hz (the model's 12k: 925 Hz,
−27 dB); clap teeth 11.8 / 24.3-25.8, fourth −1.0 and +0.4 dB, tail 89-91 ms. archive.org Set2
(early): RS ratio 0.56, 932 Hz.

### 9.7 What remains

- **The clap's fourth pulse.** In AR's, Regebro's and Rob Roy's units it is as strong as the others
  (AR −1.2 dB, Regebro +0.6, Rob Roy −1.0 / +0.4) and sustains its noise for 10-15 ms; in a fourth
  (scherer.de's `909.rar`, `Clap_Ind`, 32 hits) the teeth come 7-14 ms apart, hit to hit, and the
  fourth is mostly 3-12 dB under the strongest (−0.1 to −15): not always full. The model's is a short spike, −4 to
  −7 dB, that gets weaker as C65 gets smaller (the end-of-window kick, C64/R226, finds ENV1 near P and
  takes it only to −13 V). C64 2.2 nF (+120 %) gives a sustained fourth pulse that rises over 3 ms
  instead of starting sharp, and C62 +4 % a full natural fourth tooth 2 ms late; Q86's β (250-500)
  changes nothing. Not fitted. It also holds up the clap's 60-250 ms tail (120 ms against 94) and
  the pulses-over-tail ratio (17 dB against 15), which the reverb fits leave alone.
- **The teeth's first interval**: shorter than the second on all three units (AR 10.4 / 11.3 ms),
  longer in the model (11.0 / 10.6), and hardly moved by C65: the first slam starts from 0 V at the
  trigger, the others from P; comparator d's hold (C63/R225) on that first slam is a candidate.
- **The clap's late tail**: Q37 at low current (9.5), for the transistor model.
- **The rim shot's accent law** is 0.5 dB short at the top (codes 37-47) with every output stage:
  the envelope (C119's charge, Q63's on-time) or Q65's source limit.
- **The clap's accent law** 0.4 dB high at code 47 and 0.6 in the middle.

### 9.8 Validation (for `docs/model.md`)

`real909.measure` on the jack (through AR's chain against AR), RS code 21, HC code 21 (8 runs);
LSD over 0-80 ms (RS) and 0-300 ms (HC), 1/6 octave. Roland's plugin at velocity 127
(`docs/reference.md`).

| | AR | Regebro | Roland | nominal | unit |
|---|---|---|---|---|---|
| RS lines, Hz | 224, 1013, 478 | 220, 500, 1022 | 213, 469, 985 | 222, 491, 1049 | 223, 1014, 479 |
| RS t-20 / t-40, ms | 25.5 / 89.7 | 26.3 / 90.4 | 28.9 / 46.3 | 31.3 / 49.5 | 25.6 / 99.0 |
| RS centroid 0-60 ms, Hz | 793 | 755 | 496 | 647 | 792 |
| RS LSD against AR, dB | | | | 2.44 | 0.72 |
| RS level against the others | 0 | | | +7.0 dB | −0.5 dB |
| RS accent 21 → 47 | +4.29 dB | +2.46 (TOTAL only) | | +3.77 | +3.81 |
| HC bursts, ms | 0.5, 10.8, 22.6, 32.4 | 0.5, 10.7, 21.4, 30.9 | 0.8, 11.6, 23.5, 37.4 | 0.5, 13.1, 25.5, 35.2 | 0.9, 11.6, 22.3, 32.2 |
| HC t-20 / t-40 / t-60, ms | 106 / 381 / 690 | 97 / 360 / 662 | 77 / 317 / 580 | 195 / 516 / 939 | 123 / 426 / 904 (ISC: 92 / 369 / 688) |
| HC centroid 0-60 ms, Hz | 2634 | 2749 | 2405 | 2718 | 2647 |
| HC LSD against AR, dB | | | | 4.42 | 2.63 |
| HC level against the others | 0 | | | −8.4 dB | −1.8 dB |
| HC accent 21 → 47 | +5.11 dB | +2.61 (TOTAL only) | | +5.87 | +5.47 |
