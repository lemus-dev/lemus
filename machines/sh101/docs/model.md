# SH-101: the model as built

The emulation has three parts, each with its own document:

- **The CPU board**: the original TMP80C49P-6-7301 firmware on an MCS-48 core, with the key matrix,
  the switch columns, the 6-bit DAC with its three sample-and-holds, and the P2 latch
  (`core/machine.*`, `core/mcs48.*`). See `firmware.md` and `sequencer.md`.
- **The oscillator section**, from the S/H outputs to the source mixer (`core/osc*`, `core/lfo*`):
  portamento, the pitch summer, the bender board, the CEM3340 VCO, the PWM network, the 4013
  sub-oscillator, the noise source, the mixer sliders and the MODULATOR. See `circuit-osc.md` (the
  transcription) and `model-osc.md` (the model).
- **The filter section**, from the source mixer to the OUTPUT jack (`core/filter*`, `core/vcf_*`,
  `core/env_*`, `core/vca_*`): the IR3109 VCF with its resonance loop and cutoff CV summer, the
  discrete ENV generator with its trigger circuit, the BA662A VCA and the output stage. See
  `circuit-filter.md` and `model-filter.md`.

`core/voice.*` joins the two sections. It runs both at 4× the host rate below 50 kHz (2× up to
100 kHz, then 1×, so 176.4–192 kHz inside) and decimates with half-band filters. The processor
(`plugin/PluginProcessor.*`) runs the CPU board at the host rate, sample by sample. Every DAC
write and P2 change becomes a sample-stamped event for the voice, so the firmware's 1.1–3.7 ms
loop latency is the hardware's. The MODULATOR runs at the host rate as well, because its square is
also the CPU's clock.

## Conventions

- Voltages are the circuit's: absolute in the oscillator section (the rails are +15, +14, +9,
  −5 V and the trimmed +5 V bus), relative to the +5 V bus inside the VCF and VCA. Each file says
  which.
- Sliders and knobs are travel 0–1 (the panel's 0–10). Each pot has its marked law: B is linear,
  and A is an exponential law that passes 15 % at mid-travel.
- **Output level:** OUTPUT jack volts × `Voice::kOutputScale`; 0 dBFS = 2.2 V peak, which puts the
  unit at the level of Roland's plug-in at VOLUME 10. A full saw at VOLUME 10 peaks near −5 dBFS.
  At lower VOLUME settings ours is quieter than Roland's (−12 dB at VOLUME 5), because VOLUME is
  a 100k A pot and Roland's level control is nearly linear.

## Calibrations

Three named calibrations. The plugin plays Real unit; the other two are the tools' (`sh101render
--calibration`): the service checks use Service notes, the A/B against Roland's plug-in Roland.

| Field | Service notes | Real unit | Roland | |
|---|---|---|---|---|
| `vcoTuneHz` | 442 | 440 | 440 | VR7: A4 at 8' M (the service notes tune to 442 Hz) |
| `portaTauSec` | 0.66 | 0.78 | 0.66 | portamento τ at full: 2 MΩ × 0.33 µF; a real unit measures 0.78 s |
| `noiseRms` | 1.05 | 1.05 | 1.30 | IC12a's noise, V RMS: not on the schematic; real units' +0.6 dB re the saw, Roland's +2.9 |
| `vcfMvPerOct` | 19.0 | 22.8 | 22.1 | the IR3109's expo slope at pin 9 (AS3109 data sheet 17.5–20.5; measured on an original chip, 20.8). With VR8 trimming KYBD it sets FREQ's span: the Real unit's puts FREQ 10 where two real units' tops are, Roland's gives its plug-in's span |
| `vcfI0uA` | 2.4 | 3.30 | 3.9 | its expo current with pin 9 at the bus (AS3109 2.4; the decode 2.96) |
| `envCvScale` | 1.0 | 0.90 | 0.89 | the ENV's weight at the cutoff summer against the schematic (below) |
| `envPotScale` | 1.0 | 0.9 | 0.8 | the ENV's 1M pots against nominal (±20 %) |
| VR8 | 27.9 k | 16.9 k | 10.1 k | trimmed by the service procedure for the values above, in each, making up TR42's own gain (about 0.995) as a technician trimming by ear would (16.9 k is near mid-travel of its 47 k, where a designer would put it) |

Everything else is the same in all three: trims done as the service notes say (D/A, VCO WIDTH,
RANGE WIDTH, PW, VCF WIDTH), with the parts' nominal values. The full lists are in `model-osc.md`
and `model-filter.md` ("Calibration").

- **Service notes** is the unit as the service notes trim it, with nominal parts and the data
  sheets' values where the schematic leaves something open. The IR3109's expo has no data sheet of
  its own, so the AS3109's values stand in (19 mV/oct, 250 Hz per stage at 0 V); the transistor-level
  decode of the chip agrees (`circuit-filter.md` 2.1).
- **Real unit** is fitted to what recordings of a real SH-101 measure (`online-references.md`; the
  Friendly Noise unit, settings read from 1080p frames):
  - FREQ 0, KYBD 10, KCV 3.0 V, RES 10 rests at 26–32 Hz, and the ENV-10 plateau sits at 4.8 kHz
    (`units.py`: real 31.1 Hz and 4800 Hz, ours 31.1 Hz and 4817 Hz), so FREQ 0 at KYBD 0 sits
    near 3.5–4 Hz (ours 3.75 Hz).
  - FREQ 10 at KYBD 0: Starsky Carr's unit self-oscillates at 13.93 kHz (RES 10, every VCF slider
    legible) and AutomaticGainsay's peaks at 15.2 kHz (RES 7.1); ours 14.7 and 14.2 kHz. With VR8
    trimming KYBD to 1 oct/oct, the expo's slope against the pin-9 divider alone sets FREQ's span,
    so it takes 22.8 mV/oct (one chip measured 20.8, which put FREQ 10 at 28 kHz) and the expo
    current that keeps FREQ 0 in place, 3.30 µA. The cause isn't identified; the slope stands in
    (`improvements.md`, "The filter's top"; before 2026-11-16: 20.8 mV/oct, 5.0 µA).
  - ENV 10 sweeps 7.0–7.3 octaves: `units.py`'s release fit gives the real take 7.27 octaves and
    τ 4.24 s, ours 7.28 and 4.27 s.
  - Attack at 10 reaches 3.8 kHz from 53 Hz in 2.95 s (ours 2.77); release τ at 10 is 4.1–4.4 s
    (ours 4.23): the ENV pots run about 0.9 × nominal.
  - Portamento τ at full: 0.78 s (another unit).
  - The same unit tracks KYBD at 1.024 ± 0.021 oct/oct; ours is trimmed to 1.000.
  - A second unit (loopop's) needs the same ENV weight (below).
- **Roland** is fitted to Roland's SH-101 plug-in (`fit_roland.py`). Its FREQ law spans 12.2
  octaves, where the circuit with the measured rails spans 13.1 at 20.8 mV/oct: the expo takes
  22.1 mV/oct and 3.9 µA, which lays our law on Roland's as closely as before the rails were
  corrected (261 cents RMS over FREQ 1–10, 257 before; `fit_roland.py`'s score 13.94 against
  13.97). The ENV's weight keeps ENV 10 at 7.4 octaves in its renders, as before (Roland 7.65; its depth at
  lower ENV settings follows a different law), the pots Roland's decay and release curves (0.8 is
  best on average; Roland's longest times follow a steeper law than an A pot, τ 4.94 s at 10
  against our 3.8). It is for A/B against that plug-in.
- All three pass the stability runs (`specs/stability.json`, and 120 s each in `sh101plugintest`): a
  resonant bass line, LFO and ENV sweeping a self-oscillating filter, noise into maximum resonance,
  and everything at maximum. None grows or bubbles.

**The ENV's weight (`envCvScale`).** As transcribed, ENV 10 can't sweep less than 7.7 octaves while
KYBD tracks 1 oct/oct: both reach the cutoff through IC18b, so their ratio is fixed by R189, R193 and
the ENV's 5.11 V swing (ENV octaves = 1.5 × 5.11 × KYBD tracking ÷ VR8's share, which is at most 1).
Until 2026-11-13 the ENV GEN was read as being on +15 V (14.87 V): its swing was 5.42 V, the floor
8.1 octaves and the weight the real units need 0.73–0.79. On +14 V (14.3 V, SN p.7's filled arrow)
the same clips need 0.80–0.82: the rails close about a fifth of the gap to 1.0. With the expo at 22.8 mV/oct (2026-11-16) VR8's trim moves and the same clips need 0.885–0.90.
The real unit sweeps 7.0–7.3 octaves while tracking 1.024 oct/oct on the same day, and Roland's
plug-in 7.65 at 0.995 oct/oct. Every part involved was re-read on the 400 dpi scan (R158 82k on the
+14 V rail, R189 100k, R193 150k, R192 33k, VR19 100kB, IC2b a follower), and the parts-spread study
puts the floor at 8.8–12 octaves for 90 % of units (`spread.md`, with the ENV on +15 V; 6 % less
on +14 V). So something on real units weakens the ENV's path by about 0.80–0.89 (Roland's plug-in
the most), or strengthens the KYBD's, and it isn't on the schematic. Real boards rule out the parts themselves (below, "Real boards"). `envCvScale` stands
in for it; 1.0 keeps the schematic.

What has been ruled out since:
- **The measurement.** The release fit (`tools/compare/online_release_fit.py`) run on our own renders
  of the patch returns each render's own sweep: rest 31.9 Hz, depth 7.13 against a known 0.77, and
  6.3 Hz, 9.31 octaves against a render built to sweep 9.3. The saw entraining the bottom of the
  release doesn't bias it. The 1080p frame reads ENV, RES and KYBD at the top and FREQ at 0. Model
  against real through the release, aligned at note-off, 0–1.4 s: 4820/4750, 2687/2681,
  1624/1631, 1222/1243 Hz, so one uniform law fits it, sudden drop at note-off included.
- **The tracking.** The ringing-over-sub method used on the clip reads our renders 0.05 high
  (1.024 for a true 0.976), so the real unit's 1.024 is about 0.96–1.02 true. The schematic needs
  0.92 or less to sweep 7.24 octaves.
- **The chip's law.** A Juno-106's IR3109 is exponential within 3 cents from 20 Hz to 10 kHz
  (`model-filter.md` 2), and the sweep runs from 28 Hz to 4.8 kHz.
- **Parts within tolerance:** 24.4 mV/oct with VR8 at 0 gives rest 32.6 Hz and depth 7.90; 20.8
  mV/oct, I₀ 1.2 µA and the pots × 1.19 give 6.3 Hz and 9.31. Neither fits.
- **A part or value the drawing misses (2026-10-26).** The service notes' board diagrams (both
  sides, around VR19, R188–R193, IC18, C64 and TR42) show no part the circuit diagram lacks;
  VR19's top is on the ENV line's copper, not behind R190 18k (which would give 0.735, and make
  the sweep follow the PWM switch). A photo of a control board reads R189 100k, R192 33k and R193
  150k by their bands, with nothing added, as Din Sync's and sibomots' BOMs have them; 0.77 would
  need R189 at 130k or R193 at 115k. No Roland service information beyond the two bulletins held
  (the bender unit, the selected CEM3340), and no repair or mod thread, touches this path.
- **VR19 fed from S6's side of R197** (as R196 is, below) would weaken the ENV by 0.73 in VCA ENV
  mode, but it would make the sweep depend on the VCA switch (1.7 octaves brighter in GATE mode),
  which Roland's plug-in doesn't, and the board's trace puts VR19 on pin 85's side.

**A second unit (2026-10-24).** loopop's unit (mint and unmodified; sliders read from
1080p frames: ENV 10 at its stop, KYBD 0, A 0, S 0, VCA **GATE**) in two takes, FREQ 4.7 / RES 6.5
/ D 3.2 and FREQ 3.9 / RES 6.9 / D 4.8, with its rest pinned near 130 Hz by the settled spectrum:
fitting the resonant peak's track over the decay gives a weight of 0.72–0.79 (best 0.08–0.12 octave
RMS; 1.0 misses by 0.97–1.1 for any offset or decay). Both takes agree on the weight and on the
unit's offset. Failed Muso's ping (FREQ ½, RES 10, ENV ½, by ear) gave 0.86 (0.78–0.96 for ENV
4.5–5.5) on the first reading; `tools/compare/units.py` (2026-11-13) has ours with 0.77 matching it
at ENV 5.0. The saw take of the Friendly Noise unit (RES 0), tracked spectrally, confirms that unit's
0.77 independently of the self-oscillation. So:
- the weight is not one unit's: every clip that pins it lands at 0.72–0.79, none near 1.0 (with
  the ENV on +14 V, 2026-11-13: Friendly Noise 0.82, loopop 0.805 and 0.81, Failed Muso 0.82 at
  ENV 5.0);
- the VCA switch doesn't cause it: loopop's unit was in GATE mode, the Friendly Noise unit in ENV;
- part tolerances are ruled out twice over, since two units agree. What is left is a cause common
  to every unit, in the ENV-to-cutoff path or the expo's scale, that the transcription misses.

**The VCA's turn-on.** TR28 is a PNP in common base, its base on the bus (re-read on the scan), so
as the circuit diagram draws it the VCA opens only once the ENV is about 0.55 V above the bus: the
last 11 % of every decay and release falls off a cliff. The real unit doesn't. The cause is on the
board: R196 120k from +14 V sits on S6's ENV contact, the far side of R197 from the ENV line, as
the service notes' own board diagram shows (`circuit-filter.md` 5.7). That holds S6's contact
0.25 V above the ENV, so TR28 opens that much lower, with no value fitted. The real unit's release
(VCA in ENV mode, dB against the plateau, at 4.5 / 5.5 / 6.5 / 7.5 / 8.5 / 9.0 / 9.5 s):

| | 4.5 s | 5.5 s | 6.5 s | 7.5 s | 8.5 s | 9.0 s | 9.5 s |
|---|---|---|---|---|---|---|---|
| Real | −10.5 | −12.7 | −14.9 | −17.9 | −20.8 | −22.2 | −23.7 |
| R196 on S6 (the model, ENV on +15 V) | −10.5 | −12.6 | −14.8 | −17.8 | −20.5 | −22.1 | −23.9 |
| R196 as drawn | −11.6 | −14.3 | −17.3 | −21.6 | −26.6 | −30.1 | −34.6 |

With the ENV GEN and R196 on +14 V (2026-11-13) the ENV's swing is 5.11 V instead of 5.42 V, and
TR28's current alone puts the model's fade 0.1 dB lower at 4.5 s, growing to 0.7 dB at 9.5 s
(about −24.6 dB, 0.9 dB under the real unit's −23.7); not re-measured on the recording.

Before the board was read, the Real unit and Roland calibrations moved the turn-on 0.3 V lower
by hand (`vcaKneeShiftVolts`, now 0 everywhere and kept for tools); that was within 0.2–0.8 dB of
the same release.

## Validation

### The schematic, in SPICE

Every block matches ngspice of the transcribed circuit (`tools/spice/osc`, `tools/spice/filter`,
`tools/spice/jack`, `tools/spice/voice`). `tools/spice/suite.py` runs them all, reads each one's
headline numbers and holds each to a limit just above the figure here (28 checks, about four
minutes; 2026-11-13, after the rails were read right: 25 of 28, below):

| Block | Model against SPICE |
|---|---|
| LFO frequency and thresholds | within 0.1–1.1 % (R31 and R35 100k) |
| Portamento τ | within 0.01 %; AUTO's residual glide after a jump 74 mV (SPICE 81 mV) |
| Grip LFO (bender board) | ±1.164 V (SPICE ±1.172 V) |
| PWM at CEM3340 pin 5 | within 3.4 mV |
| Pulse and sub levels into the mixer | within 5 nA |
| Pitch summer (DC) | within 2.4 mV |
| VCF with the IR3109 at transistor level (from the decode; `tools/spice/filter/ir3109`) | self-oscillation pitch +0.3 to +22 cents (+0.3 at 194 Hz, +5 at 970 Hz, +22 at 4.8 kHz; limit 25 cents) and −0.04 to −0.14 dB (limit 0.7 dB), onset RES 0.804 (0.801); AC within 0.09 dB at 10 µA, 0.07 dB at 1 µA, 0.75 dB at 100 µA; a saw through it 1.3 % waveform error, harmonics 1–8 within 0.14 dB. Since 2026-11-14 the model has the decode's mirror gain, its CAP pins' 2.65 pF and its buffers' λ and current sources (self-oscillation was −4.9 to +24.8 cents, AC at 1 µA 0.64 dB). What grows with frequency is the decode's input pair losing transconductance with current, which a real chip's law doesn't show, so the model leaves it out (`model-filter.md` 2), and about 4 cents of the discrete resonance parts' capacitance at 4.8 kHz; not excess phase |
| VCF with the behavioural IR3109 (`compare_vcf.py`) | not a reference since the chip's buffers were modelled (2026-10-08): the model self-oscillates 120–130 cents under it, as the transistor-level chip does |
| Cutoff CV summer | within 2.5 cents (7 before the discrete devices' TNOM was set to 25 °C, below) |
| ENV, 7 scenarios | 1–18 mV RMS; peak and release times within 0.25 % |
| VCA and output stage | control current within 0.08 dB, gain from pin 3's open-circuit voltage within 0.06 dB, output within 0.02 dB (2 Hz–20 kHz) |
| Jack board A | power-on mute within 3.3 mV, the clamp within 0.05 dB, switching off within 0.9 mV after the first 2 ms |

**The whole voice** (`tools/spice/voice`, 43 cases). The block netlists are joined into one circuit
from the VCO's outputs to the OUTPUT jack, so the blocks load and drive each other as on the board:
- the sliders and mixing resistors into the mixer node, C45;
- the transistor-level IR3109 with its expo converter driving the OTAs' tails, from IC18b's summer
  and the pin-9 divider;
- the resonance loop and the output sum into C53 and the BA662;
- the ENV GEN at transistor level on the same gate, feeding VR19, the VCA (ENV or GATE) and PWM;
- VOLUME, IC1a, C2 and jack board A.

Only the VCO core is a source, following the model's own frequency and phase. Both sides use the
service-notes values (SPICE's VR4 and VR8 trimmed by the service procedure in its own circuit).
Model against SPICE:

| Cases | Level | Waveform error | Harmonics 1–12 | Other |
|---|---|---|---|---|
| Saw, pulse (PW 0 and 7), sub (each mode), mixes; filter open, 49–788 Hz (13) | within 0.11 dB | −33.2 to −39.8 dB | 0.08–0.33 dB; the full mix (saw, pulse and sub at 10) 0.33 dB (1.9 before the IR3109's outputs stopped softly near the rails and 0.76 before its buffers' outputs at the rails matched the decode's, both 2026-11-14; PW 7 1.39 before the oscillator section's 25 °C part set) | |
| Noise, filter open (1) | 0.0–1.4 dB | | | third-octave bands 2–3 dB RMS (a random signal on both sides; SPICE's is drawn afresh each run) |
| VCF at FREQ 7, RES 0–7.5, saw and pulse (6) | within 0.05 dB; the saw at RES 7.5 0.30 dB (its 4th harmonic on the resonance's flank, where 5 cents of cutoff count) | −28.0 to −40.7 dB | 0.08–0.17 dB (the square at RES 5: 2.5 dB on its near-absent even harmonics) | I_ABC within 1 cent |
| VCF at FREQ 5 (8) | within 0.05 dB | −39.2 to −42.0 dB | 0.01–0.02 dB | I_ABC 2 cents under |
| VCF at FREQ 3 (deep stopband, 50–60 dB down) (6) | within 0.02–3.0 dB | −7.9 to −28.5 dB | 0.27–0.31 dB | I_ABC 4 cents under |
| VCF at FREQ 3 with KYBD 10 on the top key (1) | within 0.2 dB | −28.9 dB | 0.16 dB | I_ABC within 1 cent |
| Self-oscillation at FREQ 7 / 5 / 3, RES 9.5 (6) | within 0.18 dB; the oscillation's own level within 1.0 dB | FREQ 3: −21 dB | FREQ 7 and 5: 0.10–0.57 dB; FREQ 3: its own first three harmonics within 0.2 dB (the column reads the note's lines there, 120–150 dB below the oscillation) | pitch +6 / −3 / −9 cents (+4 / −8 / −20 before 2026-11-14's second round) |
| ENV on the VCF, VCA in ENV and GATE mode (2) | within 0.02 dB | −32.3 to −32.4 dB | 0.12 dB | ENV within 0.1 % of its swing; attack to 90 % 143/143 ms, release to 10 % 496/495 ms; I_ABC 4 cents under |

- **What the joined circuit adds over the block checks:**
  - **A constant 0.24–0.52 dB on the level**, until 2026-11-13. About half was the transistor-level
    OTAs' finite output resistance (0.4 % of passband gain per stage), half the BA662's input
    resistance loading pin 3. Both are in the model now (`model-filter.md` 2 and 5), and the
    sources agree within 0.1 dB.
  - **Where the VCA opens.** Both circuits have R196 on S6's contact (`circuit-filter.md` 5.7):
    I_ABC is 40 dB under its peak at ENV = bus + 0.28 V (bus + 0.52 V with R196 where the
    circuit diagram draws it), and the control current agrees within 0.08 dB above 1 µA
    (`tools/spice/filter/compare_vca.py`).
  - **I_ABC about 15 cents under SPICE's** at every cutoff tried, until 2026-11-13: not the model.
    `models.lib` gives the 2SC1815's and 2SA1015's I_S for 0.62 V at 1 mA at 25 °C, as the model
    has them, but ngspice reads model parameters at TNOM 27 °C, so at the whole voice's 25 °C its
    TR42 sat 8 mV above the model's V_EB (13 cents of cutoff through R193; 4 mV and 7 cents in the
    27 °C block test). With TNOM=25 on the discrete devices and every filter netlist at 25 °C, as
    `jack/` already was, the two agree within 5 cents (the KYBD case within 1 cent, with SPICE's
    VR8 now the model's trim, 27.86 kΩ, rather than the ideal 28.54 kΩ).
- **What it found in the model:** the oscillator section used +15.0 V for the regulated rail while
  the filter section used the measured 14.87 V. It's one rail on the board. Pulse width at PW 7
  was 0.5 % narrow because of it, through R178; it's 14.87 V throughout now (and since
  2026-11-13 R177/R178 are on +14 V, the solid arrow, which leaves PW 10 at 3.0 %).
- **Sub-µA cutoffs (FREQ 3 and 5 with KYBD 0):** with the decode's first guess for its tail
  mirror's beta helper (Rbh 100k, about 33 nA of base current out of the master current), the
  transistor-level IR3109 ran 3 to 16 % lower in frequency there. Real chips don't (OML: linear to
  about 2 nA; a Juno-106's IR3109 exponential down to 5.7 Hz, `model-filter.md` 2), so the decode
  now biases it lightly (10 MΩ), and the low cases agree as in the table.
- **The rest of the FREQ 3 differences** are slow start-up ringing near the 16 Hz cutoff, or SPICE's
  pre-charge of C45 for the pulse's mean (the pulse cases' DC step at note-on differs by about
  9 mV; saw and sub agree within 4 mV). The FREQ 3 cases settle for 3 s before the note when
  RES is 7.5 or more; settled, they agree within 0.7 dB.

### The service notes

`tools/service/check.py` runs the service notes' adjustments and specifications against the
emulation. All **34 of 34** pass:
- the D/A in test mode, through the firmware;
- VCO TUNE, WIDTH and RANGE WIDTH, through the whole machine;
- VCF WIDTH (0.9996 oct/oct by the procedure);
- the cutoff, ENV, LFO, portamento, PWM, TUNE and output ranges;
- the sub-oscillator waveforms;
- saw aliasing below −106 dB.

### Roland's SH-101 plug-in

Mean log-spectral distance against Roland's renders (`tools/compare/compare.py`, 205 specs; `lsdA`
is the same on time-averaged spectra, which ignores modulation phase):

| Group (specs) | Real unit: lsd / lsdA | Roland: lsd / lsdA | Service notes: lsd / lsdA |
|---|---|---|---|
| VCF (34) | 4.0 / 5.0 dB | 2.7 / 2.4 dB | 4.4 / 5.4 dB |
| VCA (2) | 3.8 / 2.3 dB | 3.5 / 2.5 dB | 4.3 / 3.6 dB |
| Mixer (11) | 4.2 / 3.2 dB | 4.0 / 2.5 dB | 9.0 / 11.0 dB |
| ENV (35) | 4.0 / 3.1 dB | 3.5 / 2.2 dB | 11.4 / 12.1 dB |
| Self-oscillation (40) | 5.5 / 7.4 dB | 5.1 / 6.6 dB | 5.7 / 7.5 dB |
| Oscillator (24) | 5.6 / 5.5 dB | 5.2 / 4.7 dB | 9.3 / 10.0 dB |
| LFO (24) | 12.1 / 8.2 dB | 10.9 / 5.4 dB | 11.0 / 7.2 dB |
| Portamento (10) | 10.7 / 3.6 dB | 10.3 / 2.5 dB | 14.6 / 6.0 dB |
| **All (205)** | **6.1 / 5.4 dB** | **5.5 / 4.0 dB** | **8.7 / 8.6 dB** |

Since 2026-11-14 (the rails, the loadings, the ENV GEN's TR35, the noise, the IR3109's output
resistance and saturation; each calibration refitted) the Roland calibration went from 5.6 / 4.1 to
5.5 / 4.0 dB, and the Real unit's from 5.6 / 4.0 to 6.1 / 5.4. The Real unit's calibration follows
the real units where they and Roland's plug-in disagree: most of its rise was FREQ 8-10 (FREQ 10
at 28 kHz then; 14.7 kHz since 2026-11-16, `improvements.md` "The filter's top"), and the LFO
specs, which sit there. Before (2026-10-23 to 2026-11-13): Real unit 5.6 / 4.0, Roland 5.6 / 4.1,
service notes 9.2 / 10.4.

The oscillator group's distance is mostly its pulse-width specs, and it fell (6.3 → 4.7 dB lsdA
in Roland's calibration) when R177/R178 went onto +14 V: at PW 5 ours is now a 26.5 % pulse
against Roland's 26.8 %. Until 2026-11-13 it was 25.0 %, whose every fourth harmonic cancels
exactly, and a bin-by-bin distance punishes such nulls far beyond what the duty is worth
(`spread.md`: both are ordinary units).

The LFO group rose from 4.5 dB (lsdA) when R169 and R188 went to the 100k of real boards (below,
"Real boards"), and came back to 5.5 with R31 and R35 at 100k, whose rates are closer to Roland's at
every RATE. Most of the group's specs sit at VCO MOD 2.5–3, where Roland's law is steeper than
VR11's audio track and ours is the deeper. Since 2026-11-14 it is 8.2 dB in the Real unit
calibration and 5.4 in Roland's: the two calibrations now set the cutoff law differently, and
these specs go through the filter.

On the 85 specs of the same kind as the TB-303's suite (the VCF, saw and sub, the ENV, the VCA) it
was (2026-10-23) 3.05 dB with the Real unit calibration and 3.02 with Roland's; the TB-303's is 2.8 dB. The saws are within 0.5–1.3 cents and 2.5–3.4 dB. Most of the service notes' distance is
their 442 Hz tuning: against Roland's 440 Hz, the high harmonics land in other frequency bins.

Where the two differ, `spread.md` says whether a real unit could do what Roland's plug-in does
(5–95 % of 20 000 simulated units, every trim redone per unit):

| | Roland's plug-in | Ours | Real units |
|---|---|---|---|
| Portamento τ at full | 1.55 s | 0.66 s (0.78 Real unit) | 0.53–0.80 s; one unit 0.78 s. **Roland departs** |
| VCO MOD depth at 10 | ±26 semitones | ±28 | ±27–32 (formula; ±26–31 measured as the renders are). Roland sits at the low edge. Before R188 was corrected to 100k, ours was ±21 and Roland outside |
| FREQ law | a straight exponential, 1.23 oct per division | bends upward, 1.1–1.7 oct per division | every unit bends 1.31–1.47×: the FREQ pot is a linear 100k loaded by R186 200k. **Roland simplifies** |
| KYBD | pivots near MIDI 12 | adds the key CV (0 V at C2, TRANSPOSE M) | the real unit's rest at KCV 3.0 V fits the key-CV pivot with FREQ 0 at 3.5 Hz. **Roland departs** |
| ENV depth curve | roughly quadratic in the slider | linear (VR19 is 100kB) | no real-unit data below ENV 10 |
| LFO rate at 0 / 5 / 10 | 0.09 / 5.9 / 39 Hz | 0.08 / 5.0 / 36.4 Hz | one unit 38.2 Hz at 10; the parts spread (run with R31 and R35 at 47k) 0.07–0.19 / 3.0–5.6 / 28–35 Hz (the spec: 0.1–30). **Roland departs** at 5 |
| Pulse width at 10 | 3.9 % | 0 % | a third of units leave a thin pulse up to 7 %: both are ordinary units |
| TUNE range | ±39 cents (MASTER TUNE) | ±64 | ±59–68 (the spec says ±50) |

### Real units

From recordings of real SH-101s (`online-references.md` and `roland-measurements.md`, "Real
recordings"; lossy, with slider positions read off video frames):
- **The Friendly Noise unit** (settings verified at 1080p): rest, ENV depth, attack and release as
  under "Calibrations", all matched by the Real unit calibration. KYBD tracks 1.024 ± 0.021 oct/oct;
  self-oscillation at the plateau is +0.9 dB over the saw's fundamental. FREQ 1.5 (±0.3) at the
  lowest key CV self-oscillates near 21 Hz; the Real unit calibration gives 11–18 Hz across that
  reading.
- **The ENV's weight:** a second unit (loopop's, GATE mode) needs 0.80–0.81, as the Friendly
  Noise unit's 0.82 (above, "Calibrations"; 0.72–0.79 and 0.77 with the ENV on +15 V).
- **Resonance** (AutomaticGainsay's unit, D3, RES read at 2.3 / 4.7 / 7.1 ± 0.2 from 720p frames):
  the peak over the fundamental, with the saw's 1/n undone, is +2.2 / +8.9 / +23.2 dB and the
  fundamental's loss 4.2 / 6.9 / 8.4 dB; ours at the same readings +3.0 / +10.1 / +22.0 and
  5.0 / 7.3 / 8.65. Ours reaches the real peaks at 0.198 / 0.435 / 0.72 of travel, within 0.035
  of the readings and of both signs. At RES 7.1 the peak against cutoff is within 0.7 dB of the
  real one in every octave band but 0.8–1.6 kHz (2.2 dB). loopop's unit, RES raised at a fixed
  cutoff on C2: at equal peak height the loss differs from ours by 0.04 dB (sd 0.08) from +1 to
  +19 dB, and the peak moves 456 → 869 Hz against our 450 → 851. Roland's plug-in at RES 7.5
  behaves like a real unit at about 7.2. No clip shows where self-oscillation starts.
- **Levels at 10** (three units, sliders read from frames; `model-osc.md`, "Validation"): square
  +1.2 to +1.5 dB re the saw, sub −0.55 to −1.5, 25 % sub −1.7 to −2.5: ours (+2.0, −0.25 to
  −0.3, −1.4) is within part tolerance, and the schematic's estimate (+4.5, +2.8) is ruled out.
  Two of the units' subs are 1 dB louder than Roland's. Noise +0.6, +0.7 and −2.5 dB re the saw:
  the noise level now follows the first two (it was Roland's +2.9).
- **Saw:** ideal 1/n spectrum, as ours.
- **Noise colour:** −6.8 to −7.9 dB/octave with a 0.95–1.1 kHz centroid. Ours is −6.84 dB/octave,
  1.02 kHz.
- **Tracking at KYBD 10** (another unit): about 1.00 oct/oct.
- **Bender:** +14.7 semitones at VCO 9. That number calibrates the lever.
- **Portamento:** τ 0.78 s at full (inside the parts' tolerance; the Real unit calibration uses it).
- **Maximum attack:** Sound On Sound (2001) measured about 3 s on a unit; the Friendly Noise unit
  takes about 3.2–3.3 s to its plateau. Ours: 3.7 s nominal, 3.3 s with the Real unit's pots.

No public set of real-unit recordings at documented settings exists for the SH-101, as Din Sync's
does for the TB-303; `online-references.md` lists what was searched.

### Real boards

Part values read off real boards (`online-references.md` §5):
- **R188 and R169 are 100k**, not the 47k printed. Din Sync's RE-101 BOM comes from a dismantled
  SH-101, and a repair photo of another control board reads R188 the same. The model uses 100k:
  VCO MOD at 10 went from 25 % short of Roland's to 7 % over, and the LFO's top from 32.5 Hz to
  30.6 Hz (the spec: 30).
- **R31 and R35** are 47k per Open Music Labs and 100k per Din Sync. The model takes 100k
  (2026-10-23): RATE 10 runs 36.4 Hz, against 38.2 Hz on a real unit and 30.6 Hz at 47k
  (`circuit-osc.md` 5.3).
- **Confirmed as transcribed:** R162 (the service notes' own board diagram), R189 100k, R192 33k,
  R193 150k, R186 200k, R187 680k (the photo), and R158, R160, R133, R196, R197 (the BOM).
- **The ENV's peak**, measured on one unit with a meter: 5.26 V above the bus, with R158's top
  at 14.81 V. The drawing with +14 V at U-Labor's 14.3 V gives 5.11 V (3 % under); that unit's
  own rail gives 5.37 V, and its threshold node sits at the divider's ratio of it.

So the parts that set the ENV's weight against KYBD, and the ENV's own swing, are as drawn. The
0.82 that the Real unit calibration needs (0.77 with the ENV on +15 V) was thought most likely to come from the filter chip
itself, a law that isn't exponential at the ends of its range. A Juno-106's IR3109 rules that out
for the sweep's range (`model-filter.md` 2); what is left is under "Calibrations".

## Known differences and approximations

- The IR3109 is modelled from Open Music Labs' decode and the AS3109 data sheet. There are no
  maker's data for the original chip. Its OTAs' mismatch and offsets aren't modelled. Its law
  end to end comes from one Juno-106 voice (`model-filter.md` 2), and the compression above 10 kHz
  is taken entirely on the expo's current. The decode's own compression is its input pair losing
  transconductance with current, not phase (its OTAs add under 6 ns); a real chip's law shows none
  of that below 10 kHz, so the model leaves it out (`model-filter.md` 2).
- **The ENV's weight at the cutoff:** the Real unit and Roland calibrations scale it by 0.82 and
  0.89 to match what they measure, because the schematic as transcribed can't sweep less than
  7.7 octaves at ENV 10 while KYBD tracks (above, "Calibrations"; 8.1 and 0.77 / 0.80 while the
  ENV was read as being on +15 V). Two real units need it. The
  measurement, the tracking, the chip's law, parts in tolerance and the VCA switch are ruled out;
  the cause is not identified.
- **R31 and R35** are 100k (Din Sync's BOM, a real unit's top LFO rate), not OML's 47k. That leaves
  the square's swing at IC18a 0.74 × the triangle's, where Roland's plug-in has them equal; R35
  alone at 100k would do both, but no board source has that mix (`circuit-osc.md` 5.3).
- **Switching off:** +9 V falls with an estimated 12 ms time constant and the rest of the voice is
  taken to stop when the +15 V regulator drops out (`model-filter.md` 5); the thump's level
  follows the estimate (−30 to under −45 dBFS for 5–25 ms).
- **Jack board A's clamp:** switched on, TR7's base-collector path limits the output's negative
  peaks below about −1 V (`model-filter.md` 5). It follows from the board and the standard
  2SC1815 and 1S2473 models; a real unit's diodes at microamps may start it a little later.
- **R162's position:** it is missing on the schematic, and OML lists it without a place. We put it
  where the ENV works (`circuit-filter.md` 5.4); the service notes' own board diagram agrees.
- **Assumptions in the oscillator section:**
  - The portamento switch's contact order is deduced from the behaviour each position must have.
  - Which slider feeds which mixer resistor follows Electric Druid's mapping.
  - The grip vibrato's polarity relative to VCO MOD is uncertain.
- **Set from outside the schematic:** the noise level (two real units' +0.6 dB re the saw) and the noise
  corner (four real clips). The schematic gives neither.
- **The TRIG circuit:** its input (IC4b) and IC5a's exact feedback weren't fully traced. Its
  timing, about 0.54 ms per semitone of KCV change, follows the parts that were.
- **Self-oscillation start:** it starts from the circuit's 20 µV noise floor and takes up to a few
  seconds at low cutoffs, as on the hardware.
- **Not emulated:**
  - the CV/GATE inputs and outputs and the EXT CLK jack (SYNC stands in for a clock cable);
  - PHONES;
  - the battery (the RAM always keeps the sequence);
  - the MGS-1 grip's own controls, beyond the lever's MOD push.
