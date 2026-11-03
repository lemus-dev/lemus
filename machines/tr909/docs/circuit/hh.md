# Hi-hat (hh.cir, hihat.*)

The HI-HAT of the voicing board 73133090, closed and open: the sample clock, the address counter,
the CLOSED/OPEN logic, the ROM and its latch, which `core/hihat.*` models in C++, and the D/A, the
VCA with its envelope and accent, the filter and LEVEL to the connector W5, which
`core/netlists/hh.cir` holds. Sources as `docs/circuit.md` §0: **SN** p.11 (crop
`private/docs/schematic/crops/p11-hh-hi-hat.png`, the full page for the connections), p.5
("DIGITAL VOICE GENERATORS", the hi-hat's ADDRESS TABLE and block drawing), p.7 (change
information), p.10 (address map), p.12 (multi-jack board); **NAVA** (`Nava_v1.02_netlist.txt`);
**OLD**, `private/reference/old-models/cymbals.cpp`; **RES**, `docs/research.md` §4
(B.1-B.10). Every value was read on the SN crop at 400 dpi and agrees with NAVA unless a note says
otherwise.

Address map (SN p.10): IC65b C-12, IC67a B-8, IC67b C-9, IC68 D-7, IC69 E-6, IC70 F-8, IC71 F-6,
IC72a E-8, IC72b D-6, IC72c/d D-5, IC73a D-5, IC73b C-5, Q72 B-5 (printed "Q-5"), Q73 B-6, Q74 B-7,
Q80 D-10, Q81 D-11, Q84 C-8, Q85 D-8, VR21 B-6, VR22 D-12, VR23 B-7.

## 1. The split

The hardware splits at IC68's outputs. Everything before them is CMOS logic on +5 V, clocked by an
RC oscillator whose frequency is fixed by R492 and C160; everything after is analog. `HiHat`
(core/hihat.*) runs the logic event by event, each oscillator edge at its own time within the
board's steps, and gives the netlist six D/A lines and RUN/STOP; `hh.cir` takes them with the
board's `trig`, `ohsel` and `acc`.

| hh.cir input | What | From |
|---|---|---|
| `d0`..`d5` | IC68's outputs into RA9: `d5` into R (5k, the ROM byte's bit 7) .. `d0` into 32R (160k, bit 2) | `HiHat::Lines::d` |
| `run` | IC72a pin 3, RUN/STOP (5 V while the counter runs) | `HiHat::Lines::run` |
| `trig` | TRIG latch IC10 Q3 (pin 7), the HH trigger: 5 V for one scan tick | the board |
| `ohsel` | IC10 Q1 (pin 2), CLOSED/OPEN: 5 V for a closed hat, held between ticks | the board |
| `acc` | accent DAC IC8 / RA7 (address 4007, both hats), open-circuit, 5 V · code / 63 | the board |

Outputs: `out`, IC65b's output at W5 to the multi-jack board (CN-5 pin 40 by the jack board's
order); `jack`, the MULTI OUT
jack's contact behind R521 / C508 with nothing plugged in. Controls: `level` (VR22 50k B),
`chdecay` (VR21 100k B), `ohdecay` (VR23 1M A, a0.15). Params: `c134` (0.01u, late; 10u before SN
415300), `c135` (1u), `rtrig`, `rlatch` (450 Ω, HD14174B), `rdac` (2.54k).

## 2. The digital half (core/hihat.*)

**Oscillator, SN p.11 grid D-5.** IC72c (pins 8, 9 tied) and IC72d (pins 12, 13), TC4011UBP: the
two-gate CMOS astable. **R492 12K** from IC72c's output (pin 10) to the timing node, **C160 470P
(S) polystyrene** from the node to IC72d's output (pin 11), **R493 33K** from the node to IC72c's
inputs. IC72d's other input (pin 13) is RUN/STOP: while it is low, IC72d's output is held high and
the oscillator stops. Its frequency isn't the textbook 1/(2.2 RC) (40.8 kHz sample clock): the
measured sample clock is 31.57 kHz on two units (RES B.4: AudioRealism 31.57/31.58, Rob Roy
31.56-31.58 kHz; the AudioRealism takes here read 31.560-31.585 kHz), so the astable's period is
taken as `kAstable` · R492 · C160 with kAstable = 2.808 (63.14 kHz, the SN's "about 60kHz"), and
`setClock()` trims it. Duty taken as 50 %.

**Restart.** Stopped, IC72c sits self-biased at its threshold through R492 + R493 (IC72d's output
high, no current in C160). When RUN rises, IC72d's output falls, C160 takes the node 5 V down, and
IC72c's output (A) rises at once: the oscillator starts as from an A edge, B (IC72d pin 11) rising
half a period (7.92 µs) later.

**Timing generator, SN p.5 "IC73 ... TIMING GEN".** IC73a (TC4013BP, D pin 5 tied to /Q pin 2,
clock pin 3 from A) and IC73b (D pin 9 to /Q pin 12, clock pin 11 from B) toggle on opposite edges:
two quadrature squares at half the oscillator's frequency. S pins 6 and 8 grounded, R pins 4 and 10
from the reset. **IC73b Q (pin 13)** is IC71's clock (pin 10, counting on its fall) and IC69's
clocked CE (pin 22: the HN61256P takes its address as CE rises; the hi-hat ROM's CE is active high
by mask, RES B.2.1); **IC72b** (pins 5 and 6 from the two Qs) clocks IC68 (pin 9) as it rises.

**Reset.** TRIG → **C159 100P** → **R491 100K** to ground → IC71 pin 11, IC70 pin 15, IC73 pins 4
and 10. The edge's 5 V decays with 10 µs; the resets let go at the CMOS threshold, VDD / 2, after
R491 C159 ln 2 = **6.93 µs** (`kResetTime`). From rest, B's first edge (7.92 µs) is the first one
after the reset: IC73b toggles first, and the first sample (ROM[0]) is latched **1.5 periods,
23.76 µs**, after the trigger; then one every 31.67 µs. A reset threshold under about 2.2 V would
hold the flip-flops past that edge and move every sample 7.9 µs later. On a retrigger the
oscillator keeps running: the first latch comes one period after the first edge past the reset,
22.8-30.7 µs after the trigger depending on the phase. A reset taking IC73b's Q down while both Qs are
high is an IC72b edge too: IC68 takes the ROM's output once more at the trigger (the next sample of
the sound being cut); modelled.

**Counter and stop, SN p.11 grid E-F 6-9.** IC71 (HD14040BP) counts bits 0-11; its Q12 (pin 1)
clocks IC70b (TC4520BP: CK pin 9 grounded, EN pin 10, counting on EN's fall) for bits 12-14 (pins
11, 12, 13). The ROM's lines A14 (pin 27) and A13 (pin 2) are **D199** from bit 13 and **D198** from
bit 14, each ORed with CLOSED through **D196 / D197** (1SS133), pulled down by **R487 / R486 10K**.
**IC72a** (pins 2 and 1: bit 13 and the A13 line) gives RUN/STOP: low when bit 13 ∧ (bit 14 ∨
CLOSED), so the counter stops at **6000h open** and **2000h closed**, as SN p.5's table says ("110
0000 0000 0000 .... in OPEN mode, 010 0000 0000 0000 .... in CLOSED mode"). RUN/STOP also drives
IC69's CS (pin 26) and, through R488, mutes the VCA (§4). `HiHat` follows the logic as drawn,
whatever the order of TRIG and CLOSED: a CLOSED line rising during an open hat past 2000h stops it,
before 2000h it reads the closed region from there on; CLOSED falling while stopped at 2000h
restarts the counter (the firmware changes it only with a trigger, RES C.4).

**ROM and latch.** IC69 HN61256P C43; the public image (`hn61256p__c43.ic69`) is in playback order
with the 6-bit code in bits 7..2 (RES B.2.2, B.3). Only D0-D5 (pins 11, 12, 13, 15, 16, 17) reach
**IC68** (HD14174BP; D inputs 14, 13, 11, 6, 4, 3; R̄ pin 1 to +5 V); its outputs (2, 15, 5, 12, 7,
10) drive RA9. The order of the six lines on the chip doesn't matter here: the image is already
descrambled, bit 7 the MSB into R. Before the first latch the latch holds the last sample of the
last region played, 80h (code 32). At power-on (`reset()`) the counter is as the firmware's
power-up write (400A = 3Ch, a closed hat) leaves it: stopped at 2000h, CLOSED high.

**The closed hat's first 65 samples.** In the image the closed region (6000h-7FFFh) starts with
65 samples of 80h (6000h-6040h, 2.06 ms), and RES B.3 took them as played: the closed hat's sound
2 ms after its trigger. Two units say it isn't:

- AudioRealism's 14 takes of an open hat cut by a closed one (`OpenHat-Dec100-cut-ClosedHat-*`,
  master out): the open hat's ROM data stays coherent with the ROM (NCC 0.94-0.99 in 0.5 ms
  windows) up to the moment the closed hat's sound (image 6041h) starts; a counter reset 2.06 ms
  earlier would have cut it there. Median gap between the two: +0.16 ms (`tools/compare/hh.py`,
  "cut"). Rob Roy's OPCL2: the same, −0.6 ms.
- Every single closed hat (AudioRealism CH DECAY 0-100 %) holds its full gain for 2-3 ms after its
  sound's onset before the decay: the trigger's hold (Q74, §5) starts with the sound, not 2 ms
  before it.

So `HiHat::address()` reads the closed region from 6041h, the 65 samples last (`kClosedLead`):
the image is taken to be that much off the counter there, which costs nothing at the region's end
(silent from 7C7Ah). The naokiiwakami hobby player's 6040h start (RES B.3) agrees with this.

**Interface** (core/hihat.h). The board calls it once a step of the hh netlist:

```cpp
class HiHat {
public:
	enum class Smoothing { Sinc, Box };
	struct Lines { double d[6]; double run; };            // volts, the netlist's d0..d5 and run
	void setRom(const uint8_t *data, size_t size);       // hn61256p__c43.ic69, 32 KiB
	void setRate(double hz);                              // the hh netlist's step rate
	void setSmoothing(Smoothing s);                       // default Sinc (§8)
	double latency() const;                               // the lines' delay in steps: 8 (Sinc), 0.5 (Box)
	void setClock(double hz);                             // sample clock trim; default 31.571 kHz
	void reset();                                         // power-on: stopped at 2000h, CLOSED high
	void start(bool closed, double delay = 0);            // TRIG's rising edge, `delay` s into the next step,
	                                                      // with the CLOSED/OPEN line latched with it
	void setClosed(bool closed, double delay = 0);        // the CLOSED/OPEN line on its own
	const Lines &step();                                  // one step: the lines for the netlist
	bool running() const;                                 // RUN high
	bool idle() const;                                    // stopped, nothing pending, lines settled
	unsigned count() const; bool closed() const; int code() const; double time() const;
	void setLog(std::vector<Event> *log);                 // every latch / RUN change at its exact time
};
```

## 3. The D/A (RA9)

**RA9, RM0621**: R, 2R, 4R, 8R, 16R, 32R with R = 5k (the SN p.11 inset), from IC68's outputs to
one node, into **C157 10µ/16** and **R482 22Ω** to ground. A plain binary-weighted array: linear,
no anti-log. The brief's "anti-log D/A" is SN p.5's block drawing, whose "ANTI-LOG" label sits under
IC67a and Q84, the envelope and accent's exponential converter (§4). The netlist holds the six
resistors with IC68's output resistance in series (`rlatch` 450 Ω: MC14174B, the HD14174B's grade,
0.88 mA typ at a 0.4 V drop at 5 V; `docs/circuit/interface.md` §1-2): R_th 2.69 kΩ, and code 32
(2.469 V) slightly under code 31 (2.531 V), the MSB short of its share. A unit's RM0621 tolerance
(no data sheet) is unknown. The lines are averages over each step, so the netlist sees them as
voltage sources moving linearly within a step (§8).

## 4. VCA, anti-log pair, accent, mute

- **VCA Q85 (S)** (2SC2603, the board's default NPN; NAVA's "Q52"): collector on the 22 Ω node
  (`vn`), emitter at **IC67b**'s virtual ground (M5218L, **R483 47K** the I-V resistor), base fed
  by **R485 10K** from Q84's second collector. It works saturated: ±18 mV across it at full scale,
  its current I_E ≈ β_R I_B (e^(V_CE/V_T) − 1) (Ebers-Moll): a conductance set by its base current,
  and an exponential. The model's positive peaks come out 1.2-1.6 × the negative ones (the ROM
  through a linear chain: 1.0-1.25), as the AudioRealism unit's (§10).
- **Anti-log pair Q84** (2SA798, one package): the reference transistor's collector is IC67a's (−)
  input, held at **+5 V** by IC67a (its (+) on +5 V, **C156 1000P** across it, output through
  **R484 2.2K** to the common emitters): its current is the envelope's, (5 V − C135) / **R453
  220K**. Its base: **R489 150Ω** to +5 V, **R481 4.7K** from the accent DAC, **R488 2.2K** from
  RUN/STOP; the other base **R490 100Ω** to +5 V. The output current is the reference's ×
  exp((V_B,ref − V_B,out) / V_T): exponential in the accent, about 6.4 dB/V (RES B.2.3), and with
  RUN/STOP low the reference base falls 0.36 V: −120 dB, the VCA shut while the counter is stopped.
  The p.11 symbol at R489's and R490's ends is the one on IC67a's (+) and IC68's R̄: +5 V (NAVA:
  +5V).

## 5. Envelope (SN p.11 grid B 5-8; SN p.5 "CLOSED ..... OPEN")

- **Q74** (R454 22K from TRIG, R455 10K) empties **C135 1µ/50** while TRIG is high: the VCA held
  at full gain for the trigger's 2 ms.
- **R452 100K + VR23 1M(A) OH DECAY** (a rheostat: wiper to the C135 end) from +5 V charge C135
  always; **R451 10K + VR21 100K(B) CH DECAY** from Q73's collector while **Q73** (2SA1115, R450 10K
  base to +5 V, R449 22K) is on, which **Q72** (R447 22K from CLOSED/OPEN, R448 10K) does for a
  closed hat. R453 to IC67a's +5 V node loads C135 as well:
  τ_OH = C135 ((R452 + VR23) ∥ R453) = 69-183 ms, τ_CH = C135 ((R451 + VR21) ∥ (R452 + VR23) ∥ R453)
  = 8.7-69 ms (RES B.2.4). The envelope current falls as a pure exponential to Q73's saturation,
  a few millivolts short of 5 V.
- A closed hat during an open one: its TRIG empties C135 again, CLOSED switches the CH path in,
  and the counter starts the closed region: the open hat is cut where the closed one starts (§2).

## 6. Filter, LEVEL, the jack board

- Two unity-gain Sallen-Key low-passes with emitter followers on ±15 V: **R480, R475 5.6K, C148
  1200P** (to Q80's emitter), **C149 1000P**, **Q80**, **R476 10K** to −15 V; **R477, R479 5.6K,
  C150 2700P** (to Q81's emitter), **C151 390P**, **Q81**, **R478 10K**. 25.9 kHz Q 0.55 and 27.7 kHz
  Q 1.32: about a 4th-order Butterworth at 26.8 kHz, which passes the first image band (16-47 kHz)
  up to its corner (RES B.2.3).
- **C134** (0.01 µ from SN 415300; 10µ/16 before, struck through on p.11 with "SN 415300-UP 0.01")
  → **R444 22K** → **IC65b** with **VR22 50K(B) LEVEL** as its feedback rheostat: −(0-2.27) ×, a
  723 Hz high-pass (late) or 0.72 Hz (early).
- W5 to the multi-jack board (SN p.12): **R521 1K**, **C508 0.01** (from SN 415300), the jack's
  normal contact to the mix resistors **R522 15K** and **R523 8.2K** into IC66's virtual grounds.
  hh.cir holds them as IC65b's load (mix.cir runs the jack board itself); `jack` is their node.
  With a plug in, the mix resistors are off: R521 / C508 then roll off at 15.9 kHz.

## 7. Production changes and early boards

SN p.7: **C134 10 µF → 0.01 µF from SN 415300** ("For rolling off unnecessary lower frequencies");
C500-C512 on the jack board from SN 415300 (C508 is the hi-hat's). Nothing else on the hi-hat.
Defaults are the late board; an early one is `--param c134=10u` (and mix.cir without C508). The
AudioRealism unit is late (−10 dB at 200 Hz), Rob Roy's early (flat to 200 Hz) (RES B.2.3).

## 8. Rates and the D/A's timing

The latch moves once a sample clock (31.57 kHz) at instants that don't fall on the board's steps,
and the filters pass the first image band. The solver's inputs move linearly within a step, so a
D/A step given as the latch's level at each step's end ramps across the step it falls in (a timing
error up to ±half a step, which beats with the clock); `Circuit::jump` would put it at the step's
start (up to a whole step). `HiHat` instead gives each line band-limited: **Sinc**, every step a
Kaiser-windowed sinc step (cutoff 0.45 of the step rate, β 9, 8 steps a side; latency 8 steps,
42 µs at 192 kHz), or **Box**, each line's average over each step (latency half a step).

`tools/spice/hhtiming.py`: one ngspice deck with the D/A lines stepping in 10 ns at the logic's
exact times (0.1 µs steps; `tr909hhrun --spice`) against the solver at each rate, both low-passed
at 20 kHz, the solver read back by its latency. Accented open hat (code 47, 60 ms), partitioned:

| Rate | Lines | RMS % | 1-5 kHz | 5-10 kHz | 10-15 kHz | 15-18 kHz | 18-20 kHz |
|---|---|---|---|---|---|---|---|
| 96k | point | 20.2 | −0.05 / −19 | −0.11 / −18 | −0.43 / −14 | −0.29 / −10 | −0.84 / −10 |
| 96k | box | 12.1 | −0.11 / −31 | −0.31 / −35 | −0.72 / −31 | −1.38 / −27 | −2.07 / −25 |
| 96k | sinc | 10.7 | −0.09 / −33 | −0.22 / −38 | −0.51 / −35 | −0.97 / −32 | −1.53 / −34 |
| 192k | point | 9.5 | −0.07 / −23 | −0.03 / −24 | −0.33 / −21 | −0.21 / −16 | −0.45 / −16 |
| 192k | box | 3.9 | −0.06 / −37 | −0.14 / −41 | −0.31 / −38 | −0.59 / −34 | −0.79 / −34 |
| **192k** | **sinc** | **3.4** | **−0.05 / −38** | **−0.12 / −43** | **−0.27 / −40** | **−0.48 / −37** | **−0.66 / −38** |
| 384k | box | 1.7 | −0.03 / −42 | −0.06 / −47 | −0.15 / −44 | −0.26 / −40 | −0.35 / −41 |
| 384k | sinc | 1.6 | −0.03 / −43 | −0.06 / −47 | −0.13 / −45 | −0.24 / −41 | −0.31 / −43 |

Each band: the level against ngspice (dB) / the part of the difference no linear response explains
(aliases, jitter; dB re the band). The closed hat (code 40, 70 ms) comes out within 0.1 dB of
these: 192k sinc 2.7 %, −0.04 / −0.08 / −0.16 / −0.29 / −0.42 dB, residual −33 to −43 dB.

Where it comes from:

- **The trapezoidal rule's warping** of the 4-pole filter and the jack RC: with Q85 replaced by a
  resistor, Sinc lines at 192 kHz meet ngspice to −0.00 / −0.03 / −0.07 / −0.15 / −0.24 dB with
  the residual at −94 to −61 dB (a Python model of the linear chain gives the same). The RMS figure
  is mostly this warping's phase: the hats' sound is all at 5-20 kHz.
- **The solver's own integration of the VCA's exponential**: ngspice given the solver's own lines
  (`--spice-as-solver`): −0.01 to −0.21 dB, residual −55 to −42 dB.
- **The smoothing ahead of the exponential**: the circuit's Q85 sees a staircase, the solver's a
  band-limited one; the rest (−0.4 dB at 18-20 kHz, residual to −37 dB). It halves with each
  doubling of the rate. Placing the steps after the VCA instead (the vca → out cut carries a
  staircase of the same timing) would remove it, but needs `jump()` at every latch edge and the
  board to place the cut's steps; not done.
- **A cut at Q85's base** (R485 across partitions) passed its ±17 mV a sample late: −30 to −36 dB
  residual; so Q84 and Q85 share a partition.

**Needs.** The hh netlist at **twice the board's rate (176.4 / 192 kHz)**, as the 808's metal
group, with Sinc lines: within 0.3 dB to 15 kHz and 0.7 dB to 20 kHz of the exact circuit, residual
under −37 dB. At the board's rate (88.2 / 96 kHz) the top octave loses 1-1.5 dB. Box is a fall-back
(causal, latency 2.6 µs at 192 kHz, 0.1-0.15 dB more loss, residual −34 dB at the top). The lines lag
the board's `trig`, `ohsel` and `acc` by `latency()` steps: delay those three for the hh netlist by
as much to line them up (42 µs; against the envelope's 2 ms hold it moves nothing audible).

## 9. SPICE

`tools/spice/cases/hh.py`; `suite.py --only hh`, the trigger's edges substepped as the board does.
The lines come from the same smoothing on a 96 kHz grid, so both solvers get the same piecewise-
linear inputs. *rom*: the ROM at the clock; *slow*: codes 32 / 40 at 1 kHz with 83 µs ramps.
Difference in % of the output's range (`out`):

| Case | What | Range | 96k max / RMS % | 192k max / RMS % |
|---|---|---|---|---|
| hh_oh_32 | OH, code 32 | 1.59 V | 9.5 / 2.15 | 2.3 / 0.46 |
| hh_oh_47 | OH, code 47 | 3.69 V | 8.1 / 1.81 | 2.4 / 0.46 |
| hh_ch_25 | CH, code 25 | 1.36 V | 11.0 / 2.02 | 2.7 / 0.45 |
| hh_ch_55 | CH, code 55 | 7.0 V | 11.4 / 1.53 | 3.2 / 0.46 |
| hh_slow_oh | OH, slow | 0.43 V | 2.3 / 0.090 | 0.67 / 0.021 |
| hh_slow_ch | CH code 40, slow | 0.69 V | 2.3 / 0.077 | 0.67 / 0.019 |
| hh_slow_ch_dec0_oh0 | CH, both DECAYs 0 | 0.29 V | 2.3 / 0.063 | 0.67 / 0.016 |
| hh_slow_ch_dec1_oh1 | CH code 55, DECAYs 1, LEVEL 1 | 3.2 V | 2.1 / 0.057 | 0.63 / 0.021 |
| hh_slow_oh_dec1 | OH code 47, OH DECAY 1 | 1.03 V | 2.2 / 0.068 | 0.67 / 0.018 |
| hh_slow_choke | OH, CH 50 ms later | 0.43 V | 2.3 / 0.108 | 0.67 / 0.026 |
| hh_slow_mute | RUN/STOP falls at 60 ms | 1.03 V | 2.2 / 0.100 | 0.67 / 0.029 |

The envelope, accent, knobs, choke and mute agree to 0.02-0.03 % RMS at 192 kHz (0.06-0.11 % at
96 kHz); the ROM cases to 0.46 % at 192 kHz, the warping of §8 at 96 kHz. Partitioned against
whole (`tr909hhrun --whole`): 0.0035 % RMS of the range, envelope within 0.002 dB.

An ideal op-amp (`opamp` without gbw) as IC67a, in the loop through Q84, stops the solver's DC
solve converging (the output is wrong by volts); with the single-pole M5218L ({gbw}) it is fine.

## 10. Against the real 909

`tools/compare/hh.py` (its header has the measures): each recording's settings rendered through
the voicing board (`tr909voicerun`, 96 kHz), the HH jack taken as a plugged one (R521 / C508 at
15.9 kHz into a 100k input; the board's jack has the mix resistors on it) and through AR's
recording chain (`real909.chain`), measured as the recording. τ is the VCA's decay against the
ROM's own data (the stored sample's decay cancels), each recording's clock refined to 0.5 Hz over
its first 300 ms (a clock 10 Hz off loses the ROM by 150 ms: the first version of the tool, with
real909.rom_rate's 43 ms estimate, gave the 44.1 kHz OH DECAY take 118 ms where it is 168);
t-20 / t-40 / tail as `docs/reference.md`'s tables; level within each series. The service notes'
unit (`--nominal`), real / model:

| AR (late unit, LEVEL 50 %) | τ ms | t-20 ms | t-40 ms | level dB | LSD dB |
|---|---|---|---|---|---|
| CH DECAY 0 / 25 / 50 / 75 / 100 % (OH DECAY 0) | 8.7 / 25.1 / 32.0 / 37.5 / 41.9 vs 8.9 / 23.5 / 32.0 / 37.6 / 41.2 | 25 / 63 / 78 / 87 / 93 vs 25 / 59 / 78 / 87 / 93 | 44 / 105 / 126 / 136 / 141 vs 46 / 102 / 128 / 137 / 141 | −5.37 / −1.97 / −1.09 / −0.46 vs −5.24 / −2.15 / −1.01 / −0.39 | 0.41-0.99 |
| OH DECAY 0 / 25 / 50 / 75 / 100 % (44.1 kHz) | 68.6 / 108.0 / 122.3 / 134.4 / 168.1 vs 67.7 / 95.0 / 112.6 / 153.2 / 166.7 | 137 / 203 / 220 / 239 / 278 vs 136 / 188 / 207 / 259 / 279 | 265 / 390 / 425 / 454 / 520 vs 265 / 356 / 396 / 508 / 544 | −2.83 / −1.36 / −0.98 / −0.63 vs −2.87 / −1.79 / −1.24 / −0.26 | 0.40 |
| CH accent 0 → 1 (codes 40 → 55; CH and OH DECAY 100 %) | 61.5-62.0 vs 61.8-63.3 | 120-121 vs 120-122 | 158 vs 157-158 | −7.08 vs −7.16 (accent 0) | 0.31-0.42 |
| OH accent 0 → 1 (codes 32 → 47; OH DECAY 100 %) | 164.9-173.1 vs 166.9-168.4 | 278-280 vs 279-281 | 519-520 vs 544-555 | −7.29 vs −7.51 (accent 0) | 0.33-0.50 |

- **Clock.** 31.577 kHz on AR (median of 32 takes, 31.570-31.580 by session), 31.569 on RR, 31.187
  on scherer's unit; the model 31.571 (kAstable, fitted to AR and RR).
- **Every AR DECAY-100 open hat is one setting**: the 96 kHz accent takes and the 44.1 kHz sweep's
  100 % take have τ 165-173 ms, t-20 278-280, tail 116.7-117.0 ms. My first report's "the sessions
  disagree" was the tool's clock.
- **AR's OH DECAY during its takes.** Its CH grid was played with OH DECAY at 0 (at 25 % its CH
  100 % would last 48 ms, not 42); its CH accent takes with OH DECAY at 100 % (61.5-62 ms is the
  model's CH 100 % at OH 100 %, 61.8 ms): not at the centre, as the first report guessed, so they
  say nothing about VR23's taper.
- **C135** at its 1 µF fits all of AR's ends within 2 % (CH 0, CH 100 % at OH 0 and at OH 100 %,
  OH 0 and 100 %).
- **OH DECAY's law** is what doesn't fit: the model is 12 % short at 25 %, 8 % short at 50 % and 14 %
  long at 75 % (§14).
- **Accent**: within 0.1 dB over each series but codes 50 and 52 (0.5 dB: their TOTAL ACCENT
  readings, `real909.AR_ACCENT_TA`, come from the BD).
- **Spectrum**: octave bands 0.5-16 kHz within ±0.4 dB, LSD 0.31-0.99 dB: the 723 Hz high-pass, the
  filters and the jack's RC as the late board.
- **Asymmetry** (positive over negative peaks, first 20 ms): AR 1.23-1.65, the model 1.29-1.62, the
  ROM through a linear chain 1.0-1.25: AR's Q85 is as exponential as the model's.
- **Choke** (OH cut by CH, AR master out, 14 takes): the closed hat's data starts where the open
  hat's stops, median gap −0.09 ms real, +0.14 ms model (2.06 ms without `kClosedLead`, §2).
- **The D/A's step at code 31 → 32 shows.** The VCA's current decays with C135's RC (183 ms at OH
  DECAY 100 %), but the gain measured against the ROM falls faster, 167 ms, because IC68's output
  resistance (`rlatch` 450 Ω) leaves the MSB short and the small codes of the stored sample's tail
  lose gain at the 31/32 crossing; with `rlatch` near 0 the model gives 184 ms. AR's 168 ms fits
  the first with C135 at 1 µF, and AR's CH (where the step weighs little) fits C135 at 1 µF too;
  an ideal D/A would need C135 at 0.92 µF for the OH and 1.03 µF for the CH.
- **OH t-40 at DECAY 100 %**: 520 ms real, 544-555 ms model (τ and the tail agree): the last
  1-2 dB of the open hat's tail, about 10 dB above its floor.

Rob Roy (early unit, `hh.c134=10u`, LEVEL full, normalised, trimmed, 44.1 kHz), nominal:

| | τ real / model, dial 0 / 2 / 4 / 6 / 8 / 10 (ms) |
|---|---|
| CH (OH DECAY 0) | 9.6 / 19.0 / 29.1 / 37.9 / 42.6 / 43.5 vs 8.9 / 21.2 / 29.2 / 34.6 / 38.4 / 41.2 |
| OH | 73.3 / 98.6 / 118.9 / 137.3 / 161.8 / 173.8 vs 67.7 / 90.4 / 106.4 / 135.6 / 157.1 / 167.0 |

- RR's unit runs 5-8 % longer at both ends of both knobs (its C135 or R453, within tolerance).
- Its octave bands are within −1.2 / +0.3 dB; the LSD (2.6-6.6 dB) is the low end: below 150 Hz the
  model has 6-14 dB more, the pedestal Q85's exponential rectifies out of the envelope, which the
  early C134 (10 µF) passes. RR's waveforms read symmetric (1.00) because the set is normalised to
  both rails (docs/circuit/s-transistors.md §3.4): nothing about its Q85.

## 11. CPU

Solver time per second of audio (Apple Silicon, one core, best of three; a hit at 10 ms), with
`HiHat` 5 ms/s (Sinc; 2.9 ms/s Box) on top:

| | 96 kHz | 192 kHz |
|---|---|---|
| OH code 32, sounding 1 s, partitioned | 39 ms/s | 69 ms/s |
| OH code 47 | 47 | 80 |
| CH code 40 (0.26 s of sound in the second) | 31 | 54 |
| No hit (awake, the board's sleep off) | 17 | 39 |
| Whole netlist, OH code 47 | 154 | 289 |

Per partition at 192 kHz: env 1 state, 6 ports, 100 % linear (0.01 s/s); vca (Q84, Q85, IC67a/b,
the D/A) 5 states, 6 ports, 0.9 Newton iterations a step, 23 % linear (0.05 s/s); out (Q80, Q81,
IC65b) 7 states, 6 ports, 89 % linear (0.02 s/s). The vca and out partitions sleep on their own
when the VCA is shut, and the voice sleeps when `HiHat::idle()` and its jack has settled.

## 12. Wiring it into the board

- Add `${TR909_DIR}/core/hihat.cpp` to `lemus_tr909core`; the netlist header picks up `hh.cir` by
  itself. The tool, for machine.cmake:

```cmake
# The hi-hat on its own: hits through core/hihat.* and hh.cir to a WAV file, and ngspice decks with
# the D/A's exact times (docs/circuit/hh.md, tools/spice/hhtiming.py, tools/compare/hh.py).
add_executable(tr909hhrun ${TR909_DIR}/tools/hhrun/main.cpp)
target_link_libraries(tr909hhrun PRIVATE lemus_tr909core)
target_compile_definitions(tr909hhrun PRIVATE TR909_ROM_PATH="${TR909_ROM_DIR}/hn61256p__c43.ic69"
  TR909_NETLIST_DIR="${TR909_DIR}/core/netlists")
```

- Each hh step (twice the board's): on TRIG2 bit 2 rising, `start(bit 3, delay)`; `step()`; the
  lines into `d0`..`d5` and `run`; `trig`, `ohsel`, `acc` as the latches hold them, delayed by
  `latency()` steps. `trig` and `ohsel` edges substepped as any trigger (8 substeps, `edge(8)`,
  `jump()`); the D/A lines never.

## 13. Open questions

- The closed region's 65 samples: an offset of the public image (as taken here) or something the
  hardware does; a raw chip dump or a scope on IC68 would settle it.
- Q85 "(S)": its reverse β sets the level (docs/circuit/s-transistors.md); AR's unit is as
  lopsided as Ebers-Moll.
- VR23's law on other units: AR's and scherer's agree (§14), Rob Roy's dial marks sit nearer the
  straight two-segment law.
- RM0621's tolerances (the D/A's 31 → 32 step, which AR's open-hat decay supports at `rlatch`
  450 Ω).
- The astable's duty and the reset threshold (the first sample at 23.8 or 31.7 µs).

## 14. Calibration

Fitted to the AudioRealism unit (AR) through the board (`tr909voicerun`), held out on AR's own
middle setting, scherer's unit (CH and OH DECAY turned together) and Rob Roy's (RR); measures and
tables from `tools/compare/hh.py` (`--nominal` against `--nominal --cal law.ohDecay=...`).

**Entries for `unitCalibration()`:**

```cpp
// OH DECAY (VR23 1M(A)): its track reaches 15 % at a third of the turn and 34 % at three
// quarters, where the netlist's a0.15 taper has 15 % at half and 57.5 % at three quarters
// (AR's OH DECAY sweep; scherer's coupled sweep agrees)
c.knobLaws["ohDecay"] = {{0, 0}, {0.335, 0.5}, {0.76, 0.612}, {1, 1}};
// the hi-hat's sample clock, AR's (32 takes, 31 570-31 580 Hz by session)
c.hhClockHz = 31577;
```

`hhClockHz` is a new field: `double hhClockHz = 0;` in `Calibration` (0: `HiHat`'s own, 31.571 kHz
from R492, C160 and `kAstable`), `if (m_cal.hhClockHz > 0) im.hihat.setClock(m_cal.hhClockHz);`
after `setSmoothing` in `VoiceBoard::prepare`, and `hhClockHz` in `tr909voicerun --cal` as
`noiseHz` is. It moves AR's clock by 0.02 % (0.3 cent); other units: RR 31 569, scherer 31 187,
Regebro 30 590 Hz (`docs/reference.md`).

**What was left alone, and why.**

- **C135** (1 µF electrolytic): AR's ends fit within 2 % (CH DECAY 0: τ 8.7 / 8.9 ms; CH 100 % with
  OH 0: 41.9 / 41.2; with OH 100 %: 61.5-62.0 / 61.8-63.3; OH DECAY 0: 68.6 / 67.7; 100 %:
  165-173 / 166.9-168.4). RR's ends run 5-8 % longer: its unit's.
- **CH DECAY** (VR21 100k(B)): straight. AR's 25 % is 1.5 ms (6 %) long, its 50 / 75 / 100 % within
  0.7 ms; RR's dial marks deviate the other way (short at 2, long at 6-8).
- **R451, R452, R453, VR21's and VR23's values**: the ends fit; nothing asks for them.
- **The level and the accent** are the anti-log pair's and Q85's (s-transistors.md): the accent
  series fit within 0.1 dB.

**The OH DECAY law.** AR's sweep, τ real / nominal (ms): 68.6 / 67.7, 108.0 / 95.0, 122.3 / 112.6,
134.4 / 153.2, 168.1 / 166.7 at 0 / 25 / 50 / 75 / 100 %: the real track stays low through the
middle of the turn and climbs in the last quarter, where the two-segment a0.15 bends at the
centre. Fitted as a two-segment track (0, 0) - (k, s) - (1, 1) in VR23's share, through the
netlist's taper:

- on 25 % and 75 % alone: k 0.83, s 0.38, which puts 50 % (held out) at 125.7 ms against AR's
  122.3 (nominal 112.6);
- on all three: **k 0.76, s 0.34**, the law above.

Through the board, real / nominal / calibrated:

| OH DECAY | τ ms | t-20 ms | t-40 ms | tail ms |
|---|---|---|---|---|
| 0 % | 68.6 / 67.7 / 67.7 | 137 / 136 / 136 | 265 / 265 / 265 | 61.6 / 61.2 / 61.2 |
| 25 % | 108.0 / 95.0 / 104.6 | 203 / 188 / 201 | 390 / 356 / 377 | 87.3 / 79.3 / 85.3 |
| 50 % | 122.3 / 112.6 / 124.6 | 220 / 207 / 221 | 425 / 396 / 430 | 94.5 / 89.0 / 95.5 |
| 75 % | 134.4 / 153.2 / 137.2 | 239 / 259 / 239 | 454 / 508 / 455 | 102.0 / 108.9 / 102.1 |
| 100 % | 168.1 / 166.7 / 166.9 | 278 / 279 / 279 | 520 / 544 / 544 | 116.9 / 116.1 / 116.1 |

AR's CH grid (OH DECAY 0) and accent takes (both DECAYs 100 %) don't move (the law holds the ends).

**Held out.**

- **scherer's unit** (third unit; both knobs turned together, 8 states): each state put at the
  rotation whose OH decay the model gives at the recording's, its CH τ checked there. CH τ
  real / nominal / calibrated (ms): 8.7 / 9.7 / 9.5, 9.7 / 11.0 / 10.3, 19.8 / 25.5 / 20.8,
  28.8 / 39.2 / 31.9, 37.3 / 41.6 / 39.0, 45.0 / 44.0 / 44.6, 51.4 / 47.5 / 51.5, 62.4 / 61.8 / 61.8:
  **16 % rms nominal, 6 % calibrated**. The OH law reaches this unit as well as AR's: with the
  nominal one the two knobs, turned together, can't be at one rotation.
- **RR** (OH dials 0-10): τ real / nominal / calibrated (ms) 73.3 / 67.7 / 67.7, 98.6 / 90.4 / 98.9,
  118.9 / 106.4 / 117.8, 137.3 / 135.6 / 130.5, 161.8 / 157.1 / 146.1, 173.8 / 167.0 / 167.0: 6.6 % rms
  nominal, 5.7 % calibrated. The law helps its dials 2 and 4 and costs its 8 (−10 %): RR's track
  climbs earlier than AR's, and its unit runs 5-8 % long throughout (C135 / R453).

**Against Roland's TR-909 plugin** (its renders, `docs/reference.md`: CH at OH DECAY 0.5; t-20 ms):

| | 0 | 0.2 | 0.5 | 0.7 | 1 |
|---|---|---|---|---|---|
| CH (OH 0.5): Roland / model | 27 / 26 | 62 / 62 | 89 / 92 | 95 / 100 | 98 / 108 |
| OH: Roland / model / AR (0, .25, .5, .75, 1) | 31 / 136 / 137 | 125 / 192 / 203 | 202 / 221 / 220 | 226 / 236 / 239 | 231 / 279 / 278 |

The plugin's open hat at OH DECAY 0 dies in 31 ms where both real sweeps (AR, scherer) and the
model keep 136-137 ms: R453 in parallel with C135 and the 100k end resistor R452 set a floor the
plugin doesn't have. Its clock is 32.85 kHz (+68 cents).

**Validation** (for docs/model.md):

| HH | AR | model nominal | model calibrated | other units |
|---|---|---|---|---|
| clock | 31.577 kHz | 31.571 | 31.577 | RR 31.569, scherer 31.187, Regebro 30.59, Roland 32.85 |
| CH t-20 at CH DECAY 0 / 50 / 100 % (OH 0) | 25 / 78 / 93 ms | 25 / 78 / 93 | 25 / 78 / 93 | scherer (OH 0) 25 |
| OH t-20 at OH DECAY 0 / 25 / 50 / 75 / 100 % | 137 / 203 / 220 / 239 / 278 ms | 136 / 188 / 207 / 259 / 279 | 136 / 201 / 221 / 239 / 279 | scherer 137 → 266; Roland 31 → 231 |
| OH τ rms over the sweep | | 9.0 % | 2.0 % | RR 6.6 → 5.7 % |
| scherer's CH at its 8 states | | 16 % rms | 6 % rms | |
| CH t-20 with both DECAYs 100 % | 120-121 ms | 120-122 | 120-122 | scherer 121-122 |
| accent, codes 40 → 55 (CH) / 32 → 47 (OH) | +7.08 / +7.29 dB | +7.16 / +7.51 | +7.16 / +7.51 | |
| LSD, first 80 ms | | 0.31-0.99 dB | 0.31-0.99 | RR 2.6-6.6 (its low end) |
| octave bands 0.5-16 kHz | | within ±0.4 dB | within ±0.4 | RR −1.2 / +0.3 |
| +/− peaks, first 20 ms | 1.23-1.65 | 1.29-1.62 | 1.29-1.62 | |
| choke gap (OH cut by CH) | −0.09 ms | +0.14 | +0.14 | |

**Unexplained.**

- The open hat's last decibels: t-40 at DECAY 100 % is 544-555 ms against 520 (τ and tail agree).
- RR's OH law (its 8 mark sits near full) and its 5-8 % longer decays: a second unit's parts.
- scherer's state 4 (+11 %): one of the hand-turned states between the knobs' positions.
- The early board's low end (RR: 6-14 dB less below 150 Hz than the model's rectified pedestal).
- AR's 25 % CH DECAY, 6 % long.
