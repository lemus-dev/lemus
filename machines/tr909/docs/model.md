# Voicing board model and validation

The 909's sound is its voicing board and its multi-jack board: five analog voices (BD, SD, three
toms) built on VCOs, a rim shot of three resonators, a clap on the 808's sawtooth generator, three
sample voices (the hi-hat, crash and ride) whose 6-bit D/As feed analog VCAs and filters, a shared
noise generator, and the jack board's mix to the master outputs. Lemus runs them the way the
schematic draws them: each analog block is a netlist (`core/netlists/*.cir`, transcribed part by
part: `docs/circuit.md` and `docs/circuit/`) solved sample by sample by a small circuit simulator
(`core/circuit.*`, the 808's, `machines/tr808/docs/model.md` "The solver"); the noise generator and
the sample voices' counters, ROMs and latches are their logic, edge by edge. No voice is a
hand-made DSP approximation.

## Blocks

`VoiceBoard` (`core/voiceboard.*`) holds the board's signals by name and runs, each sample:

| Block | Netlist, code | What it is |
|---|---|---|
| Latches and DACs | `VoiceBoard::write` | The CPU's writes to IC1/IC10 (the trigger lines, 0/5 V) and IC2-IC9 (the accent DACs, each 5 V · Σg_on / Σg behind 2.69 kΩ with the 4174s' 455 Ω outputs) |
| Noise | `core/noise.*` | The 31-stage register (x^31 + x^13 + 1, 36 stages) at 320 kHz, its 0/14 V output averaged over each sample |
| Voices | `bd sd lt mt ht rs hc hh cr rd` | Each from its trigger and accent inputs to its output at the connector, with the jack board's load |
| Tom noise | `tomnoise` | Keyed by the three toms' gates, into all three |
| Sample voices' logic | `core/hihat.*`, `core/cymbals.*` | The counters, ROMs, latches and D/A lines, each step band-limited and placed at its own time; the cymbals' clocks timed once from `crclk.cir`, `rdclk.cir` |
| Jack board | `mix.cir` | R500-R533, C500-C512, the MULTI OUT jacks, the pan resistors into IC66a/b, MASTER VOLUME, the output networks, the power-on mute |

The master outputs are volts / 10 (`kVoltsFullScale`): 0 dBFS is 10 V at the jack.

## Rates

The board runs at twice the host rate below 80 kHz (88.2 or 96 kHz) and at the host rate above,
then decimates with half-band FIRs (`core/oversampler.*`). The sample voices run at twice the
board's rate (176.4 or 192 kHz): their D/As' images pass the 27 kHz filters after them, and at the
board's rate their 15-20 kHz band was 1.5-2 dB low against ngspice (0.5-0.7 dB at twice it; 384 kHz
would be within 0.3). Their jacks come back to the board's rate through half-bands for the jack
board. Trigger edges run in quarter steps for 8 samples, as the 808's.

The noise filters of the snare (C85/R302, IC40a) and the clap (IC26b) run at twice the board's rate
(`.part ... over=2`: two steps a sample, on the noise generator's half-samples): at the board's rate
the trapezoidal rule took the octave above 8 kHz of their noise 1-3.6 dB low against the same
netlist at 384 kHz, and three real snares agree with the faster one (`circuit/sd.md` §11, §15).
What the board's rate still costs the noisy voices: at their jacks 0.4-1.2 dB at 10-16 kHz, at the
master out about 2.5 dB over 16-24 kHz (each partition's inputs moving linearly across its step, the
jack and mix boards' filters). The board at four times the host rate (2026-10-22, an HQ switch, tried
twice and taken out) lifted every voice's 16 / 20 kHz band by 0.9-2.7 / 1.6-3.4 dB and changed
nothing below 8 kHz by over 1 dB, for 1.5 times the CPU (the busy kit 48 % of a core at 48 kHz
against 30 %): not heard, A/B in the plugin and on level-matched hat, ride, crash, clap and beat
loops.

**Partitions against whole netlists** (2026-10-22, each voice at codes 10 / 21 / 47, its own jack,
5 ms envelopes within 40 dB of the peak, 1/3 octaves): BD, SD, RS, hats and crash within 0.01-0.31
dB of envelope; the ride's envelope 0.35-0.7 dB with its spectrum the same; the clap's spectrum the
same but for bands 30 dB down; the toms 0.2-1.6 dB of envelope and up to 5 dB in bands 30-40 dB
down, with their VCOs' frequencies the same to 0.1 Hz: their three VCOs drift apart in phase after
the first cycles (`circuit/toms.md` §8), which moves the beating, not the voice.

## The solver's additions for the 909

- **A MOSFET element** (`M`, SPICE level 1 without body effect or charge storage) for the CMOS
  gates in analog paths: the snare's and toms' VCOs are built on 4069UB inverters whose supplies
  move with the pitch envelope; the cymbals' clocks are 4011UB astables. Models fitted to the
  Hitachi, Motorola and TI data sheets (`docs/circuit/solver-mosfet.md`).
- **Op-amp comparators** (positive or no feedback: the BD's, the snare's and the toms' VCOs) have
  their internal pole integrated by the trapezoidal rule, their switching instant located within
  the sample, and a comparator in a rail stays in linear mode: VCO pitch within 0.01 % of ngspice at
  88.2-96 kHz.
- **The node shunt** only on nodes with no DC path at all (the snare's accent gates sat at the 1 GΩ
  shunt's mercy); ports the linear network leaves floating get a conductance the device takes back.
- **Linear mode** keeps each device's linearisation error under 100 µV where it lands, at every
  port. A device that leaves its window is linearised again in place (a rank update of the step's
  matrices), so the VCAs, envelopes and shapers stay out of Newton (`docs/circuit/solver-mosfet.md`
  §8).

## Sleep and power-on

- **A voice sleeps** once its jack has stayed within a floor for 0.1 s and none of its timing
  capacitors (0.1 µF and up) has moved more than 5 mV across that window. The BD's, snare's and
  toms' VCOs run at rest, their VCAs shut, and each trigger resets them: those voices judge by their
  jacks alone. Floors: 10 µV; the snare 1 mV (its VCOs' tones leak through Q50/Q51 as the envelope
  creeps, 0.13 mV at 1.8 s); the clap 2 mV (Q37 closes ever more slowly after a hit: 1.3 mV p-p of
  noise at 1 s). The tom noise runs while a tom does.
- **The board sleeps** when nothing runs: the processor puts out exact zeros. The CPU board keeps
  running, and its writes wake the board only when they change a latch (the firmware rewrites them
  every panel scan).
- **A new instance** is a 909 that has been on a while: every circuit at its DC operating point, and
  the voices with free-running VCOs run 1 s from their power-on state. The firmware's power-up
  trigger (the hi-hat, crash and ride at DAC code 0) plays at its 0.6 s, faintly (-60 dBFS), as on
  the unit.

## Calibration

The tantalum capacitors (the toms' ENV1 and ENV2, the BD's C8 and C9) have 2 % dielectric absorption
(`da=` in the netlists, circuit.h; Dow's four R-C branches, 0.01-10 s): modern MnO2 tantalum is about
0.5 % (Vishay), none is published for the 909's parts. Their charge given back late lengthens the
toms' far tails (t-40 +0.01-0.03 octave) without moving t-20, and the BD's t-40 and tail a little; it
matches ngspice as the plain netlists do, for about 1 % more CPU on the toms.

`unitCalibration()` (`core/voiceboard.cpp`) is the AudioRealism unit (a late board; its per-knob
recordings at 96 kHz, `docs/reference.md`): its parts' values where they differ from the drawn ones,
and its knobs' laws, each fitted on some knob settings and checked on the rest and on Rob Roy's
1995 unit (an early board). `tr909voicerun --cal nominal=1` plays the service notes' unit instead.
The comparisons apply the AudioRealism recording chain's 6.5 Hz high-pass to the model
(`real909.chain`; `docs/reference.md`, "Recording chains"), and play its grid at DAC code 21 (an
accented step at TOTAL ACCENT 0, which its 30 % accent take shows).

| Voice | Values | Outside tolerance |
|---|---|---|
| BD | C14 +3.6 %, R27 +6 %, C8 0.40 µF (+21 %), R44 36k; TUNE and DECAY laws (VR2 at 0.6/8.8/17/28/100 % of its track at the knob's quarters, VR5 at 0/13/20/33/100 %) | R44 +64 %: the click is 4.5 dB strong against the body at every setting on both units, an unidentified factor in its path |
| SD | C69 +3.5 % (the VCOs then lock at 3:2 as the unit's), VR7 600k (+20 %) | — |
| Toms | LT's TUNE pot +6 %, MT's -2.5 %; one VCO capacitor each for the VCO ratio (LT 1.471, MT 1.481), HT's two +3-4 %; the three DECAY pots +20 %; TUNE's dead ends (0.08, 0.935) | — (the DECAY pots at the edge, all three alike) |
| RS | Its output taken from IC50a, not IC50b's high-pass; the resonators' capacitors -2.0 to +3.2 % | the output stage: against the drawing (`circuit/rshc.md`) |
| HC | TM 0.30 (the service notes' 2 Vp-p gives 0.255), C65 -17 %, C57 -19 % (electrolytics) | — |
| HH | OH DECAY's law (VR23's track 15 % at a third of the turn, 34 % at three quarters); the clock 31.577 kHz | — |
| Crash, ride | The 4011UB gates' drive x1.72 and 2.15 pF gate-drain, VR25 -16 %, VR27 -10 %, C130 -4 %; TUNE's dead ends | the gates' drive: above every published typical |
| (S) transistors | `2SC2603S`, BR 6.5 (`circuit/s-transistors.md`) | — |

## Validation

### SPICE

Every netlist against ngspice on the same circuit (`tools/spice/suite.py`, 96 kHz, RMS of each
output's range): BD 0.015-0.08 %, SD 0.02-0.12 %, toms 0.02-0.05 %, tom noise 0.12-0.24 %, RS
0.02 %, HC 0.5-0.65 % (its teeth land a few µs apart: ngspice moves them 40 µs between its own step
sizes), the jack board within 0.35 % (the hi-hat's jack 0.64 % at 96 kHz, 0.14 % at 192); the hi-hat
0.45 % and the crash 0.3 % at their 192 kHz (the ride 0.6 %, within 0.2 % at 384 kHz). The VCOs'
pitch within 0.01 %; the cymbals' clocks within 0.007 % at 100 MHz.

### The AudioRealism unit (calibrated, nominal in brackets)

| Voice | Pitch | Decay | Spectrum (LSD) | Accent |
|---|---|---|---|---|
| BD | end 0.00 Hz (+5.2), sweep τ +0.003 oct | t-20 +0.01 oct (-0.30), t-40 -0.04 (-0.23) | 4.2 dB (11.7), 0-330 ms | codes 21→47 +6.34 dB (unit +6.70); click re body +0.2 dB (+4.5) |
| SD | VCO-1 +0.5…+1.0 % (+3.4…+4.0), VCO-2 within 1.4 %, ratio 1.500 (unit 1.499-1.525) | snappy t-20 at TONE 0/0.5/1: 86/182/225 ms (unit 85/178/224) | 2.3-2.9 dB (3.0-4.4) | Regebro's codes 14/21/33 within 0.6 dB |
| LT, MT, HT | -4/-6/-10 cents median, 14/14/20 RMS over 121 settings (74/46/80) | t-20 +0.01-0.02 oct, t-40 -0.04/-0.09/-0.14 (-0.14/-0.17/-0.25) | 2.9/2.7/3.1 dB (6.8/4.7/8.3) | 21→47 +6.4-6.5 dB (unit 6.8-7.1) |
| RS | lines 223/479/1014 Hz (unit 224/478/1013) | t-20/t-40 25.6/99 ms (25.5/89.7) | 0.72 dB (2.44) | 21→47 +3.81 dB (4.29); level re others -0.5 dB (+7.0) |
| HC | bursts 0.9/11.6/22.3/32.2 ms (0.5/10.8/22.6/32.4) | t-20/t-40/t-60 123/426/904 ms (106/381/690) | 2.63 dB (4.42) | 21→47 +5.47 dB (5.11) |
| HH | clock 31.577 kHz | CH t-20 at 0/50/100 %: 25/78/93 ms (unit the same); OH τ within 2 % rms over the sweep (9 %) | 0.31-0.99 dB, octave bands within 0.4 dB | CH +7.16 (7.08), OH +7.51 (7.29) |
| Crash, ride | clock within 15 cents rms over TUNE (250/290) | t-20 at TUNE 0.5: 690/555 ms (unit 657/555) | 1.11/1.14 dB (3.70/4.95) | 32→47 within 0.2-0.4 dB |

Held out of each fit, the same measures stay within these figures (the BD over 112 of its 125 grid
settings; the toms' odd TUNE and DECAY settings: -7/-13/-25 cents median; the snare's four other
TUNE settings; the hi-hat's 50 % from its 25 and 75 %). Each voice's full tables:
`docs/circuit/<voice>.md`, "Calibration".

### Other units

- **Rob Roy's (an early board, a 1995 sampler, normalised):** the BD's end pitch within 0.2 Hz, its
  sweep at three of four dial marks; its decays are its own (the BD's t-20 at full DECAY 224 ms
  against AudioRealism's 289); its toms sit 14-92 cents above AudioRealism's and its MT's VCO ratio
  is 1.66 (a capacitor off); its cymbal clocks within 56-69 cents with AudioRealism's parts; its rim
  shot (R417 12k, the early value) at 925 Hz against its 899.
- **scherer.de's hats (a third unit):** CH τ at its eight DECAY states 6 % rms (16 % nominal).
- **Regebro's (a rev. 4 unit):** the rim shot's waveform and slow lobe and the snare's accent steps.

### Roland's TR-909 software

The same settings against the AudioRealism unit (Roland's renders, `docs/reference.md`): its toms
LT 79-120 cents flat and HT 150-210 cents sharp (Lemus within ±28 cents); its BD sweep at TUNE 75 %
20.9 ms (unit 14.4, Lemus 14.5); its open hat at DECAY 0 down 20 dB in 31 ms (unit and Lemus
136-137); its clap's fourth burst 3-5 ms late, its rim shot's tail half as long; its cymbal clocks
29-118 cents under the unit's with no dead ends (Lemus within ±21 cents).

### The firmware

The original v4.0 firmware runs the sequencer; every owner's-manual procedure behaves as the manual
says, the firmware's own quirks aside (`docs/sequencer.md`, 54 regression checks in
`tools/tr909seq/tests.py`). Against the AudioRealism recordings: shuffle within 0.84 ms at all
seven settings, all 40 flam files within 0.7 ms of the emulation (37 within 0.25 ms).

## CPU

`tr909bench` (tools/bench) renders fixed workloads at 44.1 and 48 kHz through the processor, with
the firmware and the voices' parallel jobs. It reports the rendering thread's CPU, the cycles a
second over all threads, wall (the blocks' time against the audio's), worst (the slowest
512-sample block against its length, what a host's meter shows) and the instructions retired a
second (`Mins/s`: steady within 0.2 % from run to run, where cycles move 3 %, so the measure for
small changes). Each render starts past the power-on (the firmware's delay, the master mute, C143's
charging), once the board sleeps. `golden`/`check` keep renders and compare them.

The busiest job decides the worst block: the toms (one job for all three and their noise, which
couple sample by sample) and the snare. What it costs, wall % / worst %, Apple M1 Max, before and
after the solver's speed work of 2026-10-17, its two passes (`docs/circuit/solver-mosfet.md` §8):

| workload | wall before | wall after | worst before | worst after |
|---|---|---|---|---|
| busy kit | 62 / 65 | 34 / 37 | 126 / 141 | 57 / 62 |
| every voice | 120 / 126 | 62 / 66 | 224 / 234 | 104 / 112 |
| every voice once, decays up | 89 / 93 | 43 / 45 | 296 / 313 | 114 / 119 |
| LT | 62 / 65 | 27 / 29 | 107 / 126 | 47 / 50 |
| MT | 65 / 69 | 29 / 31 | 112 / 128 | 48 / 52 |
| HT | 69 / 73 | 31 / 33 | 125 / 133 | 53 / 56 |
| SD | 48 / 50 | 29 / 31 | 77 / 86 | 43 / 48 |
| idle | 8 / 6 | 6 / 5 | 37 / 49 | 32 / 35 |

Wall and worst are each "44.1 / 48 kHz". The toms' job also takes the worst block when all three
are hit at once (every voice: 104-112 %).

## Open points

`docs/improvements.md` lists what is known to be wrong, and the plan.
