# Noise generator (core/noise.h, core/noise.cpp)

The voicing board's one noise source, shared by the bass drum, snare, toms and hand clap. Not a
netlist: a bit-exact model of the shift register and its clock, whose output the board feeds to
the voices' `noise` inputs. Sources as `docs/circuit.md` §0: **SN** p.11 grid L-M 14-16 (crop
`private/docs/schematic/crops/p11-noise-tom-noise.png`), p.3 (HD14006BP pinout), p.6 (circuit
description), p.10 (address map); **NAVA** (`Nava_v1.02_netlist.txt`, sheet Noise); **DS** the data
sheets in `private/docs/datasheets/` (HD14006B, HD14070B, MC14006B, MC14070B); **RES**
`docs/research.md` A2.1.

## 1. The circuit

**Shift register.** IC32 and IC33 are HD14006BP 18-bit static shift registers on +15 V (pin 14),
VSS pin 7. SN p.3 gives the 4006's four sections: D1 (pin 1) → 4 stages → pin 13; D2 (4) → 4 →
pin 11 → 1 → pin 12; D3 (5) → 4 → pin 10; D4 (6) → 4 → pin 8 → 1 → pin 9. Each chip is wired
1 → 13 → 4 → 12 → 5 → 10 → 6 → 9 (4 + 5 + 4 + 5 = 18 stages) and IC32 pin 9 drives IC33 pin 1:
36 stages, stage 1 at IC32 pin 1.

| Point | Pin | Stage |
|---|---|---|
| IC32 D3+4 | 10 (and IC32 pin 6) | 13 |
| IC32 D4+5 | 9 → IC33 pin 1 | 18 |
| IC33 D3+4 | 10 (and IC33 pin 6) | 31 |
| IC33 D4+5, the output | 9 | 36 |

**Feedback.** IC31c (HD14070BP XOR, pins 8, 9 → 10) takes stage 13 (pin 9 from IC32 pin 10) and
stage 31 (pin 8 from IC33 pin 10); its output goes through **R187 47K** into IC32 pin 1. With
x[n] the bit clocked into stage 1, x[n] = x[n−13] ⊕ x[n−31]: polynomial x³¹ + x¹³ + 1. Stages
32-36 are a delay: the output is x[n−35].

**Clock.** IC31a (pins 1, 2 → 3; pin 1 tied to +15 V: an inverter) and IC31b (pins 5, 6 → 4; pin 5
grounded: a buffer) make a two-gate RC astable:

- IC31b's output (pin 4) drives IC31a's input (pin 2) and **C47 100P**; C47's other end is node X.
- IC31a's output (pin 3), the clock of both 4006s (pins 3), feeds X back through **R184 10K**.
- X reaches IC31b's input (pin 6) through **R185 33K**, which keeps the input diodes from clamping
  X when C47 kicks it beyond the rails.
- **C48 0.1µ** ("CERAMIC") decouples IC31-IC33's +15 V.

NAVA has the same nets (N$741 C47/R184/R185, N$742 C47/IC31, N$743 IC31 output to both CLK and R184).

**Start-up.** **C46 4.7µ/50** from +15 V to node S, **R182 10K** S to ground, **D49** (cathode at
S) to ground; **R183 10K** S → IC31d pin 13, pin 12 grounded (a buffer), **R186 100K** from its
output (pin 11) back to pin 13 (hysteresis); pin 11 → **D48** (anode) → IC32 pin 1. At power-on C46
is discharged, S sits at +15 V, IC31d's output is high and D48 holds stage 1 at 1 over R187's
feedback. S decays with R182·C46 = 47 ms; with R186's hysteresis pin 13 falls through the gate's
threshold (7.5 V typ, DS HD14070B: V_IL 6.75 V typ, V_IH 8.25 V typ at 15 V) when S reaches
(11 × 7.5 − 15)/10 = 6.75 V: after 47 ms · ln(15/6.75) = **37 ms**. 36 clocks (about 0.1 ms) fill the
register with ones, so the generator always starts from the all-ones state: the 909's noise
sequence is the same after every power-on (given the clock). D49 discharges C46 when the rail falls,
ready for the next power-on. SN p.6: "On power-up, a trigger is applied into pin 1 of IC32 via D48
for starting running".

**SN p.6 text** says "two shift registers … making up 32 stages": the drawing has 36 (RES A2.1).

## 2. Period and polynomial

x³¹ + x¹³ + 1 is irreducible over GF(2): its degree is prime, x^(2³¹) ≡ x mod p, and it has no root.
2³¹ − 1 is a (Mersenne) prime, so it is also primitive and stages 1-31 run through all 2³¹ − 1
nonzero states: the period is **2³¹ − 1 = 2 147 483 647 clocks**, 1.86 hours at 320 kHz.

`tools/noise/check.cpp` checks this on the model (build line in its header):

| Check | Result |
|---|---|
| x^(2³¹) mod x³¹ + x¹³ + 1 = x; no root | ok |
| Stages 1-31 return to all ones after exactly 2³¹ − 1 clocks (and move after 1) | ok |
| Stage 1 high on 2³⁰ of the 2³¹ − 1 clocks | ok (1 073 741 824) |
| Output = stage 1, 35 clocks late | ok |
| 20 s at 96 kHz: mean 7.503 V, range 0-15 V | ok |
| Power-on: output held high for the hold, then noise | ok |

(Stages 32-36 are not part of the period: the fill leaves them high where the sequence would have
other bits, so the full 36-bit state first repeats one period later.)

## 3. The clock rate

**From the parts.** The textbook period of this astable, 2.2 RC = 2.2 µs (455 kHz, RES), assumes
ideal gates. Simulated in the time domain (thresholds, delays, output resistance, input capacitance
and the input diodes: `tools/noise/astable.py`), the period is longer, mostly
because R185 33K and IC31b's input capacitance (DS MC14070B: C_in 5.0 pF typ, 7.5 max, plus 1-3 pF of
board) delay each switching by about R185·C_in:

| Case | f_clk |
|---|---|
| Ideal gates (2.2 RC) | 455 kHz |
| Gate delays only (40 ns each: DS MC14070B at 15 V, (0.26 ns/pF)·C_L + 37 ns) | 416 kHz |
| **Typical parts**: delays 40 ns, C_in 7 pF (5 + 2), 3 pF at X, outputs 170 Ω (8.8 mA typ at 1.5 V, 15 V) | **324 kHz** |
| C_in 6 / 8 / 10.5 pF | 335 / 314 / 293 kHz |
| Threshold 0.40 or 0.60 V_DD | 320 kHz (the period is flat around V_DD/2) |
| Delays 30 / 60 ns; outputs 100 / 500 Ω | 328 / 317; 326 / 316 kHz |
| R184 ±5 %; C47 ±10 % | 336-313; 349-303 kHz |
| 60 random units (R ±5 %, C47 ±10 %, C_in 6-10.5 pF, X 1-6 pF, delays 30-60 ns, outputs 150-500 Ω, thresholds 0.45-0.55 V_DD) | mean 309, 5-95 % 283-330, range 279-341 kHz |

**`NoiseGenerator::kClockHz` = 320 kHz**, a typical unit; a unit's own rate is likely within
280-340 kHz. Electric Druid (via RES) gives "roughly 300KHz … can vary significantly depending on
the brand of 4070 chip used, and the actual value of the 100pF capacitor", with no measurement
stated; ryandorward's plug-in uses 300 kHz.

**From the recordings: not measurable.** The bit stream's spectrum is sinc²(f/f_clk), so the clock
shows in the top octave only: at 48 kHz (the AudioRealism files' Nyquist) the droop is −0.17 dB for
455 kHz, −0.32 dB for 320 kHz and −0.45 dB for 280 kHz. Every voice filters the noise (and the
interface its anti-alias filter) far more than that, by amounts not known to 0.1 dB; an m-sequence
has no spectral lines; and the clock itself is far above the recordings' band. In the AudioRealism
files the snare's tail (TONE and SNAPPY 100, 50-300 ms) is 16 dB under its 5 kHz level at 15 kHz,
40 dB at 40 kHz and 45-49 dB at 46-48 kHz; the clap's tail 13, 25 and 27-30 dB: the voices' filters
and the jack's 1k / 0.01µ, against which a 0.1-0.3 dB difference between candidate clocks can't be
told apart.

## 4. The output and its loads

The output is IC33 pin 9 (stage 36), switching between the 4006's rails. Its loads (SN p.11, NAVA
net NOISE):

| Voice | Input | DC load | Load above ~10 kHz | Notes |
|---|---|---|---|---|
| BD | **R10 12K** → R9 4.7K to ground, C15 0.47µ → R46 4.7K | 16.7 kΩ | ≈ 14.4 kΩ | the attack noise (BD crop, grid M-8) |
| SD | **R286 10K** → R285 3.3K to ground, C85 0.015µ → R301 22K | 13.3 kΩ | ≈ 12.9 kΩ | the snappy (SD crop, grid Q-4) |
| Tom noise | **R190 22K** → C161 0.0047µ to ground, C51 0.0047µ → IC27b | none (caps) | ≈ 24 kΩ | the three toms' shared noise |
| HC | **R208 47K** → R209 10K to ground, C42 / C43 0.0047µ → IC26b | 57 kΩ | ≈ 49.5 kΩ | the clap's band-pass |
| All four | | **6.6 kΩ** | **≈ 4.4 kΩ** | |

**The 4006's output resistance matters at the high level.** DS HD14006B at 15 V, 25 °C: source
−3.5 mA typ (−1.2 min) at V_OH 13.5 V, about **430 Ω** typ (1.25 kΩ at the minimum); sink 8.8 mA
typ (3.0 min) at V_OL 1.5 V, about 170 Ω. The loads go to ground, so the low level stays at 0 V,
but the high level drops to 15 V × 6.6k / (6.6k + 0.43k) = **14.1 V** (DC) and 13.7 V above
10 kHz with typical parts, 12.6 V with a minimum-spec part: the noise every voice gets is about
0.6 dB (typical) under the open-circuit 15 V. Because the four inputs share the pin, a voice
netlist can't hold this as its own source resistance; `NoiseGenerator::setHigh` sets the high
level (default 15 V, the open-circuit value; **14.0 V** is the typical loaded pin). The output's
edges (430 Ω into the wiring's tens of pF) are tens of ns: no effect at the board rate.

The four voices take the same stream, so their noises are correlated as on the real machine
(the snare and clap phase against each other when hit together: RES A2.1).

## 5. The model (core/noise.*)

- `NoiseGenerator`: the 36-bit register as a `uint64_t` (stage k is bit k − 1); `clockOnce()`
  shifts in stage 13 ⊕ stage 31 (bits 12 and 30), or a 1 during the power-on hold; `output()` is
  stage 36.
- `next()` returns one board sample: the high level times the share of the sample the output is
  high, counted exactly from the clock edges that fall in it (a box filter of the zero-order-hold
  stream). A phase in clock periods carries between samples; at 96 kHz and 320 kHz that is 3.33
  clocks a sample.
- `setClock(hz)` (default `kClockHz`), `setRate(hz)` (the board's rate), `reset()` (running, all
  ones), `powerOn()` (the same with the 37 ms hold still to run: the output stays at the high level,
  then the noise starts), `state()` / `setState()`.
- Cost: a few shifts a sample.

**Spectrum of the output** (20 s at 96 kHz, 320 kHz clock, Welch, one-sided, mean removed):
−34.5 dB(V²/Hz) at low frequencies, flat within 0.4 dB to 20 kHz and 1.2 dB down by 40-48 kHz.
That is what the sampled box average of a random ±7.5 V bit stream gives in theory
(Σₖ 2·7.5² T_c sinc²((f + k f_s) T_c) sinc²((f + k f_s) T_s), with its aliases): the measured
spectrum is within 0.17 dB of it on average over 0.2-47 kHz. The averaged samples' RMS is 3.92 V
(the stream's 7.5 V, smoothed over 3.3 bits a sample).

**Board interface.** The netlists' `noise` input takes `next()` each board sample (`docs/circuit.md`:
0-15 V, averaged over each sample). Within a step the solver moves the input linearly between
samples.

## 6. Open questions

- The clock rate of a real unit: a scope on IC31 pin 3 (or the 4006 clock pins) would settle it.
  Nothing in the recordings can.
- The HD14070BP's input capacitance and thresholds are the largest uncertainty in the estimate; a
  different maker's 4070 (an early-production substitute) would move it by ±10 %.
