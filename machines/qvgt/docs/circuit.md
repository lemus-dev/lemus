# The analog circuit

The GT's analog stages as the model has them: the preamp (service manual sheet 1, SKE1 rev B,
8-22-91) and the converters, dry path and outputs (sheet 2, GT_SKE2 rev B), with the data sheets
in `private/qvgt/docs/datasheets` (SSM2120 rev B1 and AN-382, CEM3381/PA381, PCM54, TL084, CD4051B,
CD4053B, LM339, 2N5460). Where the code is: `core/netlists/*.cir` (the circuit solver,
`core/circuit.*`, machines/tr909's), `core/preamp.*` (the netlists joined by the SSM2120,
`core/ssm2120.h`), `core/unit.*` (sheet 2 and the rates).

## Rates and levels

The host's samples are volts at the jacks, 0 dBFS = +4 dBu (1.736 V, a sine's peak), both ways.
The preamp runs at 4 times the DSP's rate (166,667 Hz; "The rate", below): host to that rate by a
windowed-sinc resampler, the preamp, then a 241-tap Kaiser low-pass decimating by 4 to the DSP's 41,666.67 Hz
(-6 dB at 20.8 kHz, 90 dB down from 22.9 kHz, where folding would reach 18.75 kHz). Sheet 2's
analog after the D/A runs at the DSP's rate, then up to the host's.

## Sheet 1: the preamp

| Block | Parts | Netlist |
|---|---|---|
| Inputs | J10, J11; R3, R1 470K; R2, R5 1K into U1D, U1A (followers); R4, R6 1K, C2, C1 10u | `input.cir` |
| INPUT pot | dual 10KB (P1A, P1B): U2D, U2A inverting, 0 to -10 (10K over 1K + the track) | `input.cir` |
| Mono sum | R7, R10 470 into U2C, gain 6 (R9 10K, R8 2K): U2 pin 8 feeds the compressor, the gate's detector and sheet 2's envelope | `input.cir` |
| Dry outputs | U2D, U2A's outputs, through R97, R98 10K and C37, C36 4.7u to U12 (sheet 2) | `input.cir`, `core/unit.cpp` |
| Compressor | U4 (SSM2120) VCA 1: R34 20K, C13 1u in; detector 1: R35 20K, C32 10u in, C21 4.7u LOG AV; control amp 1 through U14 (CD4051) | `core/preamp.cpp` |
| Overdrive | VCA 1's current into U1C; U3B (CD4053) picks R39 36K / R40 10K (off) or R38 4.7K, R37 1K, C11 4.7u, R36 47K and D6-D10 (on); C12 20p | `overdrive.cir` |
| Distortion | U1B multiple-feedback band-pass (R31 15K, C5, C6 .01u, R13 36K); R14 1.5K, R32 3.3K into C8 4.7u; U6A with D1-D5, Q1 (2N5460) and C7 2200p; eight feedback/attenuator rows (R17-R30) picked by U8 (CD4051); U9A picks it or the clean path (R33 10K, R12 6.8K) into U7A (R57 10K) | `shaper.cir` |
| Preamp tone | presence U7D (C24 .033u, R79, R80 56K, C22, C23 .01u, R77 9.1K, R78 1K, R76 47, R87 5.6K); bright U7C (R91 56K, C34, C35 .01u, R89 9.1K, R99 1K, R84 100, R88 7.5K, R85 1K); U9C, U9A pick; U7B sums (R59 10K) | `shaper.cir` |
| Cabinet | two unity Sallen-Key low-passes (U6C, U6D: 10K, 10K, 5600p, 2700p) tapped through 30K/20K; U5B picks one or both, U5C the cabinet or the plain signal (R53 30K, R50 20K) | `shaper.cir` |
| Bass boost | U6B multiple-feedback band-pass (R56 2.2K, R61 750, R55 39K, C9, C10 .01u, R54 300K) through U5A (R52 10K, R49 6.8K) | `shaper.cir` |
| U13A | the sum into the gate's VCA (R105 15K) | `shaper.cir` |
| Gate | U4 VCA 2: R103 30K, C27 1u in; detector 2: R63 20K, C28 .47u in from U2 pin 8, C29 10u LOG AV; control amp 2: R69, R70, R84 39K from the threshold DAC (U39 pin 1, sheet 2) | `core/preamp.cpp` |
| After the gate | U13D (R101 20K, C38 20p); send R96 910, R119 100 to J7; return J8 (R118 47K, R123 1K) into U13B, R120 1K, R121 680; U3 picks the return or the send (R99 10K, R95 6.8K); U13C (R102 10K) to sheet 2 | `gateout.cir` |

The CD4051/4053 switches are 125 ohms (the CD4053B's on resistance at 17 V). The op-amps are
TL084s: 3 MHz, 200,000 open loop, clipping softly at +-10.5 V, without their slew limit (13 V/us,
where audio needs 1.3 at most): with it the solver's Newton fails where a stage clips; leaving it
out moves a clipping stage's output 80-94 dB under the signal against ngspice with it.
1N4148: IS 2.52n, N 1.752, RS 0.568. 2N5460: VTO -2, BETA 0.75m, LAMBDA 0.01 (`models.cir`). The
diode strings (D6-D8, D9-D10 in the overdrive; D5-D4, D3-D1 in the distortion) are one diode each,
N and RS multiplied: the same current through identical junctions, so the same equations with
fewer of the Newton solve's ports (a third less CPU; `improvements.md`, cpu round 1).

`shaper.cir` runs as six partitions (its `.part` lines), joined only at op-amp outputs, which
the model drives as ideal sources: the same equations, solved in pieces, so the Newton solves
cover only the clipper. On the bench's plucks it comes out as close to ngspice as the netlist
solved whole does (below); the two differ from each other only in where the solver halves a step.

### Integration

Each op-amp is a single pole, integrated by backward Euler in the input, overdrive and gate
circuits and by the trapezoidal rule in the shaper (`Preamp`). `tools/spice/tapcheck.py` runs a
workload, taps every circuit's own inputs at each step and gives them to ngspice (its steps a 64th
of a DSP sample); on the plucks and strums of programs 0, 5 and 12 (36 ms from the first onset),
its outputs under 20 kHz against ngspice's, at the earlier 8x (333 kHz):

| Circuit | Backward Euler | Trapezoidal | Trapezoidal, the shaper at 2x |
|---|---|---|---|
| input (p8) | 52-60 dB under | 50 dB (program 0) | |
| overdrive (od8) | 49-55 dB | 47 dB (program 0) | |
| shaper (pre) | 31-41 dB | 31-44 dB | 43-58 dB |
| after the gate (out) | 65-72 dB | 65 dB (program 0) | |

The shaper's error is where U6A swings across its diodes at each zero crossing and the tone
stack's op-amps reach their rails, faster than a step resolves; at 8x backward Euler left its
output's top octave (12.8-25.6 kHz) 0.3-0.4 dB low and the octave below 0.12 dB, the trapezoidal
rule within 0.1 dB everywhere, 2x within 0.02 dB (for 32 % more CPU: not taken, `improvements.md`).
The other circuits come out closer with backward Euler, at 4x as at 8x (trapezoidal everywhere at
4x: the input 42 against 60 dB under, the gate 58 against 63, the overdrive 37 against 40). Oversampling only the band-pass and the
clipper helps program 0 but not program 5, whose error is in the tone stack. Comparing renders
sample by sample needs care: the whole unit is sensitive (an input 1e-7 larger moves its output by
-86 dB peak on the plucks, 1e-5 larger by -54 dB), so peaks mostly show an event a sample apart.

### The rate

Chosen 2026-11-12 by ear (blind A/B of 8x, 6x and 4x: `private/qvgt/reference/rate-ab`, no
difference heard) for a third less CPU. Renders with the hiss off against the same at 16x (aligned;
the error's RMS under the render's, and the largest octave level difference, 8-24 kHz):

| Rate | qvgtbench p30-sweep | Renders under 16x | Top octaves | tapcheck, program 0's pluck: overdrive, shaper, gate |
|---|---|---|---|---|
| 8x, 333 kHz | 5,395 Mins/s | 44-56 dB | -0.05 to -0.14 dB | 48.6, 39.5, 72.0 dB under |
| 6x, 250 kHz | 4,577 (-15 %) | 40-52 dB | -0.1 to -0.27 dB | 47.3, 33.4, 67.7 |
| 4x, 167 kHz | 3,615 (-33 %) | 35-45 dB | -0.2 to -0.6 dB | 39.8, 27.7, 62.6 |

Below 4 kHz every rate is within 0.02 dB of 16x, and the programs without distortion within 0.02
dB everywhere (bypass 61 dB under). The top octaves' loss is the distortion's highest harmonics,
not the linear stages (the trapezoidal rule everywhere leaves it: -0.16 to -0.43 dB); keeping the
clipper and the overdrive at 8x inside a 4x preamp (Chain::setOver) saved 25 % for no closer a
render. A unit's spread is larger: its 5-10 % capacitors move the tone stack's corners by more.
`QVGT_PREAMP_OVER` runs the tools at another rate (`tapcheck.py --over`, `suite.py --over`).

## Checks

`tools/spice/suite.py`: each netlist against ngspice 47 on the same deck at 166,667 Hz, 15 cases
(both pot settings, overdrive off and on, the shaper clean, clipping, distortion 1 and 8, Q1 on,
presence, bright, both cabinets, bass, the gate's outputs with and without the loop), the shaper's
with its trapezoidal poles. Worst RMS difference 0.31 % of the output's range (bright, 3 kHz),
worst peak 1.31 % (at 8x: 0.09 %, 0.48 %); all pass (2026-11-12). `tools/spice/tapcheck.py` (above) checks the circuits on
a workload's own signals.

`qvgtpreamprun` runs the preamp alone with the latches set by hand: levels, the VCAs' gains, the
control outputs, the solver's statistics and its speed.

## Cost

`qvgtbench` and `tools/bench/mins.sh`: the whole unit takes 2,200-3,600 million instructions per
second of audio (about 8-15 % of a core at 48 kHz), the preamp the largest part, its clipper about
half of that (`improvements.md`, Budget).

## Noise (HISS)

With HISS on (the bar; on by default): a source in series with each op-amp's + input (the netlists'
`noise_<part>` inputs), white at TL084's 18 nV/sqrt(Hz) (its 1/f corner left out); the SSM2120
VCAs' output noise, 19.6 pA/sqrt(Hz) at unity (the data sheet's -80 dBV in 20 kHz into its 36K) and
rising 0.475 dB a dB of gain (Figure 1b); after the D/A, U18's and U16's input noise, U15's summers'
at a noise gain of 2, the CEM3381's 10 pA/sqrt(Hz) (100 dB under 400 uA p-p in 20 kHz) rising with
the square root of its gain, and the output stages' at their + inputs. And the A/D's offset servo:
the DASP's LSTMSB (the last conversion's sign) through R140 51K into C64 4.7u, R137 51K into the
A/D's node against R136 510K from -12 V, so the input's DC sits at the conversions' sign change
and the A/D toggles about zero (`core/board.cpp`). Off: noiseless, the A/D reads exactly what it is
given.

## Not modelled

- The 20 kHz module's own response (the decimator's instead).
- The op-amps' 1/f noise and offsets; resistors' thermal noise (under the op-amps' everywhere it
  was looked at: the input's 1K is 4 nV/sqrt(Hz), the distortion's 220K at a gain of 1).
- The VCAs' distortion and control feedthrough.
- The input jacks' switches (J9/J10's normalling). J8's is modelled as empty: its tip at R118's
  0 V, and its switch grounding U3's select, which holds the loop out whatever U11 says (the owner's
  manual: the loop is bypassed with no jacks inserted).
