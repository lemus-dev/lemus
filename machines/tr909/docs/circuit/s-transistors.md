# The "(S)" transistors (models.cir 2SC2603S)

Every single-transistor VCA on the voicing board carries a circled S on SN p.11: **Q6, Q12** (BD),
**Q50, Q51** (SD), **Q19-Q21, Q26-Q28, Q57-Q59** (LT, MT, HT), **Q33** (tom noise), **Q65** (RS),
**Q37** (HC), **Q85** (HH), **Q71, Q69** (crash, ride). This note collects what the mark means, how
the VCA's level and shape depend on the transistor's parameters, what the recordings say about
them, and the model fitted to all the voices at once. The netlists use `2SC2603S` at those 19
positions and the plain `2SC2603` everywhere else.

Sources as `docs/circuit.md` §0 (**SN**, **NAVA**, **RES**); the real units as
`docs/reference.md` (**AR** the AudioRealism unit, **RR** Rob Roy's, Regebro's); other Roland
service notes and the device data in `private/docs/papers/roland/`, `private/docs/papers/web/` and
`private/docs/datasheets/` (README and SOURCES files there list each).

**Result.** `2SC2603S` is the 2SC2603 with **BR 6.5** (it was 6). The (S) mark is Roland's
selection by the VCA's gain, which is the reverse β; the recordings put it at 6.5 against the
voices whose level doesn't go through an (S) transistor, provided the AudioRealism unit's clap
trimmer TM sits near 0.31 (the netlist's 0.255 is the service notes' 2 Vp-p procedure applied to
the model). A reverse β near 2, which the rim shot's and the clap's levels seemed to ask for, fits
only if TM is at 0.255 on that unit and every other gap is put elsewhere; §4 weighs the two. The
rim shot's own gap (about +7.5 dB) is not the transistor's (§3.3).

## 1. What the mark means

**SN.** p.11 marks the 19 NPNs above, and **C160 470P** (hi-hat clock) with a circled S and an
arrow to "Polystyrene"; a circled T marks the envelopes' capacitors (C8, C21, C35-C37, C98; the
schematic README also C9, C23, C100), probably tantalum. No legend explains either. The default
note reads "unless otherwise noted All NPN Transistors 2SC2603-F". The parts list (p.2) has one
small-signal NPN, **15129137 2SC2603-28-F** (h_FE rank F, 250-500 at 1 mA); the TR-707's list has
the same Roland number as "2SC2603-F", so "-28" is not a second part (a lead form or packing code,
unconfirmed). p.5 calls Q71 a VCA "configured as a voltage controlled potentiometer". No service
bulletin mentions these transistors (the only 909 bulletin is the PROM v4 one).

**Roland's other notes.** In about 40 other Roland service notes of 1981-86 (26 of them scanned for
circled letters) no circled S or legend turned up. Two of them select transistors for this very
circuit (the audio into a few tens of ohms at the collector, the emitter at an op-amp's virtual
ground, the control current into the base) and say how:

- JX-3P, July 1983 (`private/docs/papers/roland/jx3p_service_notes_1983/`): the VCA TR101 is
  marked "TR101 SEL"; the parts list: "2SC945-Q selected for VCA, RED / ORN / YEL / GRN, use one
  color in a unit" (four Roland numbers, one for each colour).
- MKS-30, main board dated Dec. 1984 (`.../mks30_service_notes_1984/`): TR3 and TR4 are marked
  "Gm SELECTED", with the note "Gm selected 2SC945Q for TR3 and TR4 are classified into four
  groups and dotted in one of Red, Orange, Yellow and Green. All TR3 and TR4 on a given PCB must be
  of the same color for uniform tone characteristics." The parts list gives the selected part the
  same number as the plain 2SC945-Q (15129107).

So a selected part needn't have a number of its own, and Roland's selection for this VCA, in the
909's year, was its gain (Gm), binned four ways, one bin to a board. The 909's (S) is most likely
the same: the 19 VCAs of a unit from one bin. That makes the transistor's gain one number per
unit, and the model one parameter set for all 19 positions. (Roland's other selected parts are
named in words: the TR-808's "2SC828 (R) selected noise", the SH-101's "BA662A (offset selected)
white dot", the JP-6's "µPC1252H2 VCA selected".)

**Elsewhere.** The RE-909 builders' advisory (2024, unofficial;
`private/docs/papers/web/sunshinejones_re909_advisory/`) reads (S) as "selected" and advises
forward h_FE 300-350, which misses the reverse β (§2). The clones don't select: the 9090 uses BC549s throughout, NAVA plain 2SC2603s, the
NeinOhNein kick BC549s, the RD-9 "high HFE transistors". Repairers replace failed (leaky, noisy)
2SC2603s with 2N3904, 2SC945P or BC547/549 (Whittle; Gearspace 1197307, 429167); nobody reports a
change of level or decay after a swap.

## 2. What the transistor's parameters do in the VCA

The collector sits on a low-impedance node (BD, toms, SD: 47 Ω under 2.2k or 1.5k; RS 680 Ω under
12k; HC 47 Ω under 3.3k; HH, crash, ride: 22 Ω fed from the D/A), the emitter at an op-amp's
virtual ground, so V_CE swings a few tens of mV either side of 0 (BD and toms ±12 mV, HH ±18 mV,
cymbals ±21 mV, RS ±35 mV). The base takes the envelope as a current (220k-1M from a capacitor, or
the anti-log pair's collector). Both junctions conduct: the transistor is saturated, and runs in
forward and in reverse (inverse) mode on alternate half-cycles. From the Gummel-Poon equations at
a fixed base current I_B:

- At V_CE = 0 the small-signal conductance is g = (β_F β_R / (β_F + β_R)) (1 + 1/(β_F + β_R))
  I_B / V_T ≈ β_R I_B / V_T for β_R ≪ β_F. The VCA's gain follows the base current and **the
  reverse β sets the level of every (S) voice**, in proportion while the source is stiff (BD, toms,
  hats, cymbals: g R_source 0.1 or less). This g is what the MKS-30 calls Gm: selecting by Gm is
  selecting by reverse β. Forward h_FE barely enters.
- For β_R ≪ β_F the current is about β_R I_B (e^(V_CE/V_T) − 1) − I_B: exponential, one half-cycle
  growing and the other limited to (β_R + 1) I_B. The asymmetry depends on the swing against V_T,
  not on β_R; only β_R near β_F (a tanh) makes it symmetric, and then the gain is β_F/2 (about
  +29 dB over β_R 6).
- At zero current V_CE = V_T ln(1 + 1/β_R) (4 mV at β_R 6), and a part of the base current,
  β_R/(β_F + β_R), leaves through the emitter: the small DC steps the voices' notes describe.
- The base voltage at a given I_B is V_T ln(I_B β_R / IS) (the base current flows mostly into the
  collector junction): IS and β_R set where a decaying envelope (C − V_BE)/R closes the VCA, and
  so the end of the tails.
- ISC/NC (collector-junction recombination) make β_R fall at low current: the gain then falls
  faster than I_B, which bends the tails and the cymbals' VCA law (§3.6). ISE/NE carry nA here and
  move nothing; NR other than 1 shifts the zero-current point by tens of mV (a thump), so NR 1;
  VAF changes the gain by under 0.5 %; the solver has no VAR (SPICE would take it; it would matter
  as little).

## 3. What the recordings say

Renders through `tr909voicerun` (the board as the plugin plays it, 96 kHz, a 2 ms trigger, every
LEVEL at 0.5), with variant netlists by `TR909_NETLISTS`, measured by `tools/compare/real909.py`;
the rim shot and clap by `tools/compare/rshc.py --netlists`, the hats and cymbals by
`tools/compare/hh.py` and `cymbals.py` (their tools given `--netlists` through a wrapper).

**The accent code of AR's grid.** AR's grid and its accent000 takes are at DAC code 21 for the
analog voices (an accented step at TOTAL ACCENT 0; CH 40, OH/CR/RD 32), not 10: the BD's 30 % take
is 0.5 dB under its 0 % take, MT's, RS's and HC's too, and only an accented step reaches the
accent table's one dip (code 20 after 21; `docs/reference.md`, Accent). Every number here is at
those codes. `bd.py`, `toms.py` and `sd.py` render the grid at code 10, which matters for their
shapes as well as levels: at code 21 the toms' decay is −0.04 octave at −20 dB and −0.17 at −40 dB,
the BD's −0.26 and −0.46; the same renders at code 10 give the toms −0.21 / −0.37 and the BD −0.42
/ −0.66, as toms.md (−0.15…−0.19 / −0.36…−0.45) and bd.md (−0.46 / −0.66) found.

### 3.1 Levels across the voices

AR recorded every voice at LEVEL 50 % through one interface gain, so its voices' levels against
each other are a measure of the VCAs' gains. Two levels don't go through an (S) transistor: the
clap's pulses (the OTA IC30, whose level TM sets) and the snare's snappy (the VCAs Q47/Q48,
2SD1469s, whose reverse β of about 50 comes from the sheet's "V_CE(sat) ≒ 6 mV at 1 mA/0.1 mA").
RMS over 0-50 ms from the onset, model minus AR, dB, after the clap's offset is taken out (the
snappy: the part above 2 kHz, 20-60 ms, SNAPPY 1):

| | BD | SD | LT | MT | HT | CH | OH | CR | RD | RS | snappy | HC tail τ ms | p/t dB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| AR | | | | | | | | | | | | 94 | 14.7 |
| BR 6, TM 0.255 (before) | +7.2 | +9.4 | +8.1 | +8.2 | +7.8 | +7.6 | +6.7 | +6.0 | +6.9 | +14.9 | +8.0 | 115 | 12.3 |
| BR 2.2, TM 0.255 | −0.3 | +3.1 | +0.8 | +0.8 | +0.4 | −0.3 | −1.3 | −2.0 | −1.1 | +9.7 | +8.5 | 100 | 18.5 |
| BR 6, TM 0.31 | −0.5 | +1.7 | +0.4 | +0.4 | +0.1 | −0.1 | −1.1 | −1.7 | −0.8 | +7.2 | +0.3 | 91 | 16.6 |
| **BR 6.5, TM 0.31** | **+0.1** | **+2.2** | **+1.0** | **+1.0** | **+0.6** | **+0.5** | **−0.4** | **−1.1** | **−0.1** | **+7.5** | **+0.3** | **93** | **16.3** |

- With the netlists as they were, every (S) voice is 6-9 dB too loud against the clap, the rim shot
  15 dB, and the snappy 8 dB: the clap is the odd one out, or every transistor VCA on the board
  (both types) is too hot.
- The (S) voices agree with each other within ±1 dB whatever BR is (BD, toms, hats and cymbals:
  standard deviation 0.7 dB at BR 6.5, 1.0 at BR 2.2): one gain for all, as one Gm bin would give.
  Their level moves as 20 log BR up to BR 12 (BR 1 / 2 / 3 / 4 / 6 / 12 put the nine voices' mean
  at −6.2 / −0.7 / +2.4 / +4.6 / +7.6 / +11.9 dB against the clap at TM 0.255).
- The snappy and the (S) drums agree with each other at the sheet's 2SD1469 and BR 6-6.5 (BR 6:
  the snare's drum part +9.5 dB and its snappy +8.0 at TM 0.255; +1.7 and +0.3 at TM 0.31).

### 3.2 The clap's tail (Q37)

The clap's pulses come through the OTA, its reverb tail through Q37. The tail's time constant
(60-250 ms) and the pulses' level against the tail (RMS 0-35 ms over 60-120 ms) depend on both
Q37's β_R and TM (rshc.py, 6 noise runs each):

| | TM 0.23 | 0.255 | 0.27 | 0.29 | 0.30 | 0.31 | 0.32 |
|---|---|---|---|---|---|---|---|
| BR 6: τ ms / p/t dB | 120 / 8.6 | 115 / 12.3 | 110 / 14.1 | 102 / 15.8 | 97 / 16.4 | 91 / 16.6 | 85 / 16.6 |
| BR 6.5 | | 116 / 11.7 | | | | 93 / 16.3 | |
| BR 2 | 109 / 16.1 | 98 / 19.0 | | | | | |
| BR 0.7 | | 75 / 22.8 | | | | | |
| clap p-p at the jack, V | 1.5 | 2.4 | 3.1 | 4.0 | 4.4 | 4.9 | |

AR's tail is 94 ms, RR's 89-91, Regebro's 92: three units within 5 ms. At BR 6-6.5 they need TM
0.30-0.31, which also brings the accent range (codes 21 to 47) to +5.7 dB (AR +5.1; +6.0 at TM
0.255). At TM 0.255 they need β_R about 2, and the pulses then stand 4 dB too high over the tail.
The netlist's TM (0.255) is the SN's adjustment, 2 Vp-p at the jack with LEVEL at centre, done on
the model (`rshc.py --adjust`): its p-p is the teeth's narrow spikes, and at equal p-p the model's
clap has 2.4 dB less RMS than AR's, so the procedure may land low on the model. That all three
units' tails want the same TM argues for that, rather than for three misadjusted units.

### 3.3 The rim shot (Q65)

The rim shot is the one (S) voice that doesn't line up: +7.5 dB over the others at BR 6.5 (TM
0.31), +9.7 at BR 2.2. Q65's β_R can't take it out without breaking the rest of the voice:

- **Accent law.** Codes 21 to 47: AR +4.29 dB; the model +4.05 at BR 6, +3.91 at 6.5, but +5.69 at
  BR 2 and +6.96 at 0.7. At β_R 6 the VCA is half source-limited (g × 644 Ω about 0.7), which
  flattens the law; with a small β_R it follows the envelope.
- **Waveform.** Over 0.5-8 ms AR's positive peaks are 0.56 of its negative ones (flat tops at half
  the negative peaks, every shot); the model's 1.19 at β_R 6.5 (0.78 with C121/C122 0.047 µF). A
  ratio of 0.56 means the reverse half clipping well under the forward half: a smaller β_R I_B, or
  a smaller swing at the collector than the model's ±35 mV.
- **Diagnostics** at BR 6.5, TM 0.31 (rim shot over the clap, rshc.py's 200 ms RMS, AR +0.3 dB):
  as drawn +8.1 dB; R423 27k (half the collector swing) +1.4 dB with the accent law kept (+3.91);
  R418 330 Ω +3.9 dB but the law +5.17; C121/C122 0.047 µF +6.6 dB with the waveform correlation 0.65 → 0.88; R418
  330 and C121/C122 0.047 together +2.4 dB. None is what SN and NAVA print. The rim shot's level
  is the rim shot's (its resonators' sum, the D91/D92 clip, the R423/R418 divider, Q65's envelope
  C119/R403, IC50's gains), not the transistor's.

### 3.4 Waveform asymmetry: hats, BD, Rob Roy

- **HH (Q85).** AR's hats are as lopsided as the model's exponential makes them (positive over
  negative peaks over the first 20 ms 1.23-1.65 against 1.23-1.64; the ROM through a linear chain
  1.0-1.25), at every accent and DECAY: AR's (S) transistor has β_R ≪ β_F. SN p.9's scope drawings
  of the hats, crash and ride are lopsided the same way (positive 1.8-1.9 × negative).
- **RR's symmetry is its processing.** Every RR file reaches both +1.0 and −1.0 (the BD, toms and
  hats alike): the set was normalised to both rails, which makes its waveforms "symmetric" (hh.md's
  1.00). It says nothing about its transistors.
- **BD (Q12).** AR's body is nearly symmetric (0.94 over 30-90 ms) where the model's is 0.66, and
  its second harmonic is lower (−27 dB re the fundamental, the model −22) while its third is higher
  (−26, the model −34). With Q85 on the same unit as exponential as the model's, this is most
  likely the collector swing, not Q12: a body half as large at Q12's collector (or the D10/D11 shaper less
  driven, which gives the higher third harmonic) would do both. The BD's level would then fall by
  about 5 dB with it; at BR 6.5 the BD is +0.1 dB as drawn.

### 3.5 The tails: IS and ISC

β_R moves the BD's and toms' decays by under 0.03 octave from 2 to 7 (the snare's t-20 by 0.1:
its first 10 ms against the rest). A larger IS (with β_R) lowers the base voltage at which an
envelope closes the VCA and lengthens the tails: at BR 2, IS 1.1e-14 / 1e-13 / 1e-12 put the toms'
−40 dB time at −0.16 / −0.10 / −0.04 octave and the BD's at −0.45 / −0.41 / −0.35, and leave the
clap's tail at 98-99 ms. But 1e-12 is V_BE 0.53 V at 1 mA, no small-signal NPN; within the
published range (below, up to about 7e-14) the toms gain 0.02 octave (IS 4e-14 at BR 6.5: −0.15
against −0.17). IS stays the 2SC2603's. A fall of β_R at low current (ISC, NC 2) shortens every
tail: with IS 4e-14, β_R 7.5, ISC 2e-11 (β_R 6.0 at 2 µA, 6.9 at 18 µA, 4.0 at 0.2 µA) the toms'
−20 / −40 dB go to −0.09 / −0.20 octave, the snare's to −0.48 / −0.32, the clap's tail to 86 ms,
and the (S) voices' spread to 1.1 dB. The tails argue against any.

### 3.6 The cymbals' VCA law

β_R moves the crash's and ride's level only: from BR 6 to 2 the per-block law (`cymbals.py
--match`) changes by 0.05 dB RMS. Their base currents run from 18.3 µA (crash; ride 10.6) at code 0
to 2 µA at code 56, so the law measures β_R's dependence on current. Against the model's flat β_R
the units' gain at code 56 (re codes 1-4) is −2.3 dB (AR crash), −1.0 (AR ride), +0.3 (RR crash),
−0.6 (RR ride): a mean of −0.9 dB, the units spread ±1 dB about it, and AR's ride, at lower
currents, bends less than its crash, which a low-current fall-off would do the other way round.
So β_R is flat within about 1 dB from 2 to 18 µA: at β_R 6.5, ISC ≤ 1e-11 at NC 2 (≤ 4e-13 at NC
1.5). Measured 2SC1815s (below) fall off faster than that.

### 3.7 Published parts

No sheet gives the 2SC2603's reverse h_FE (the Micro Electronics sheet: h_FE D/E/F/G, V_CE(sat)
≤ 0.3 V at 100/10 mA, f_T 200 MHz); no Mitsubishi sheet was found; the one 2SC2603 SPICE card
found (OrCAD jpwrbjt) is unusable (IS 1.5 nA). Reverse h_FE is printed only for muting types
(2SC2878: 150). For its replacements (`private/docs/datasheets/spice-models/`):

| Part, source | IS | BF | BR | ISC / NC | β_R at 10 / 30 / 100 µA |
|---|---|---|---|---|---|
| 2SC1815-O / Y / GR / BL, kaz-electro, measured (E and C swapped) | 9-30 f | 119-455 | 8.5 / 4.5 / 4.3 / 6.0 | 9-93 p / 1.5-1.8 | (Y) 0.8 / 1.4 / 2.2, (GR) 1.8 / 2.4 / 3.1, (BL) 3.7 / 4.4 / 4.9 |
| 2SC945, OrCAD | 9.6 f | 213 | 3.9 | 30 f / 3.9 | |
| KSC1815, Fairchild | 20 f | 128 | 4.6 | 132 f / 1.5 | |
| BC547B, Philips | 24 f | 294 | 7.95 | 63 f / 1.24 | 6.4 / 6.7 / 7.0 |
| 2SC1740S (Rohm format) | 70 f | 277 | 11.6 | 1.0 p / 1.33 | 5.6 / 6.5 / 7.4 |
| MMBT3904, NXP | 4.6 f | 160 | 5.9 | 3.3 p / 1.39 | 0.5 / 0.7 / 1.1 |
| 2N3904, National | 6.7 f | 416 | 0.74 | | |

Hobby readings of reverse h_FE: 2SC1815Y 2, 2N3904 3.7, 2N4401 1, 8050 27; EDN (Hogrefe) gives
the 2N3904's as 0.25. So β_R spans 0.3-10 between parts and grades, and at µA several fall off
strongly: a reason to select. β_R 6.5, flat, is in the range, near the better 2SC1815s and the
BC547B.

## 4. The fit, and the alternative

**What decides β_R** is what the (S) voices are compared with. Two references go through no (S)
transistor and disagree by about 8 dB:

- **(a) The clap at its service-notes TM (0.255).** Then β_R 2.2 puts the (S) voices on AR's levels
  (mean 0, ±1 dB), and the clap's tail at 100 ms. It leaves the snappy +8.5 dB, which would need the
  2SD1469s' reverse β near 8 rather than the 50 their sheet's 6 mV implies; the clap's pulses 3.8 dB
  over its tail; the rim shot +9.7 dB with its accent law +5.6 dB (AR 4.3).
- **(b) The snare's snappy (2SD1469 as its sheet).** Then the clap needs TM about 0.31 (its tail
  then 91-93 ms, as all three units), and β_R 6-6.5 puts the (S) voices on AR's levels (BR 6.5:
  mean +0.2, ±0.7 dB) and the snare's drum and snappy together. It leaves the pulses 1.6 dB over
  the tail, the rim shot +7.5 dB with its accent law right (+3.9), and asks that the model's TM
  adjustment lands low (§3.2).

(b) fits more of what was measured, with the data-sheet parts, and its one assumption is the
model's clap level, which the clap's own shape already questions. `2SC2603S` takes it:

    .model 2SC2603S NPN(IS=1.1e-14 BF=350 BR=6.5 NF=1 NR=1 VAF=110 ISE=1e-14 NE=1.5)

BR 6.5 is the joint fit at TM 0.31: the mean of the BD, toms, hats and cymbals' levels (zero at
6.3), the clap's tail (94 ms at 7), the snare's drum against its snappy (5), the rim shot's accent
law (5-6). ±1 in β_R is ±1.3 dB on every (S) voice, and this is one unit's bin (the MKS-30's four
bins span a range Roland doesn't give). IS, BF, VAF, ISE/NE are the 2SC2603's (§3.5: the
recordings don't determine IS within the plausible range); no ISC (§3.6). Under (a) the card would
be the same with BR 2.2.

## 5. Effect on each voice

BR 6 → 6.5 with the netlists' other values (TM 0.255), the voices' own measures at AR's settings
and codes:

| Voice | Before (BR 6) | After (BR 6.5) | AR |
|---|---|---|---|
| Level of each (S) voice | | +0.5…+0.7 dB | |
| BD t-20 / t-40 (octave, DECAY 0 / 0.5 / 1) | −0.26 / −0.46 | −0.26 / −0.46 | |
| BD click re body (ATTACK 0 / 0.5 / 1) | +5.3 dB | +5.3 dB | |
| BD body +/− peaks 30-90 ms | 0.66 | 0.66 | 0.94 |
| Toms t-20 / t-40 (octave, 9 settings) | −0.04 / −0.16 | −0.04 / −0.17 | |
| Toms attack noise (re 0-50 ms) | +0.2 dB | +0.4 dB | |
| SD t-20 / t-40 (SNAPPY 0) | −0.39 / −0.15 | −0.36 / −0.15 | |
| RS over HC | +15.3 dB | +15.6 dB | +0.3 |
| RS accent 21 → 47 | +4.05 dB | +3.91 dB | +4.29 |
| HC tail τ / p/t | 115 ms / 12.3 dB | 116 / 11.7 | 94 / 14.7 |
| HH asymmetry, decay, accent | | unchanged | |
| CR/RD VCA law | | unchanged (level +0.6 dB) | |

The change itself is small; what the fit shows is that β_R isn't the lever for the gaps it was a
candidate for (§6).

## 6. What remains, voice by voice

- **RS.** +7.5 dB over the other voices (at the fitted TM), and its waveform's +/− ratio (0.56 on
  AR, 1.19 in the model): its own circuit (§3.3: a collector swing about half the drawn one, R423 or
  the clip; C121/C122 0.047 for the shape). The slow positive lobe after the ringing is not Q65's
  either (its base current gives the opposite sign).
- **HC.** The TM default (§3.2): 0.31 fits AR's level against every other voice, all three units'
  tails and the accent range better than 0.255. Its pulses stand 1.6 dB over its tail even then
  (C59/C61, the reverb path). For the clap's owner to decide; `tm` is a control of hc.cir.
- **BD.** The click is 5.3 dB strong at every ATTACK, a constant factor, so a linear one in the
  click's path (C11/R44, C2/R7, the Q9 filter), not the VCA, whose nonlinearity would vary with
  ATTACK (Q6's collector swing grows severalfold across the knob). The body's symmetry and
  harmonics: the swing at Q12 (§3.4). The decay (−0.26 / −0.46 octave at code 21): C8, VR5, the hold (bd.md).
- **Toms.** −0.04 / −0.17 octave at code 21: within reach of the (T) capacitors' tolerance or
  DECAY's law; IS would need an implausible value (§3.5).
- **SD.** The drum part +2.2 dB over the other (S) voices, its t-20 short (its first 10 ms, sd.md).
- **HH, crash, ride.** Nothing (S)-related: the hats' asymmetry is right; the cymbals' law bends
  differently on each unit, by ±1 dB.
- **All analog voices**: the comparison scripts' grid code (10, where AR's is 21).

## 7. SPICE

`tools/spice/suite.py --only bd,lt,mt,ht,tomnoise,rs,hc,hh,cr,rd,sd` after the switch, 96 kHz (the
model exports to ngspice as any BJT), RMS difference in % of the output's range (max in brackets),
the outputs over each voice's cases:

| Voice | Cases | RMS % (max %) |
|---|---|---|
| BD | 7 | 0.015-0.080 (0.9-2.0) |
| LT, MT, HT | 5 each | 0.021-0.050 (0.12-0.89) |
| tom noise | 3 | 0.12-0.24 (4.5-7.2) |
| SD | 9 | 0.024-0.12 (0.12-1.25) |
| RS | 5 | 0.021-0.024 (0.58-1.10); TRIG OUT 0.001 |
| HC | 5 | 0.53-0.65 (47-50; the teeth's timing, rshc.md §5); TM ends 0.015-0.025 |
| HH | 12 | 1.4-2.2 fast cases (6-11), 0.05-0.11 slow cases: as hh.md at 96 kHz |
| crash, ride | 9 | 0.8-1.6, 2.3-2.8: as cymbals.md at 96 kHz (they need 192-384 kHz) |

The same cases on the old model (BR 6) give the clap 0.65 / 0.41 / 0.40 % (codes 21 / 47 / 63),
against 0.65 / 0.53 / 0.51 % now: its chaotic teeth, not the model; the rest as their notes.

## 8. Open questions

- A published photo of a voicing board's component side: colour dots on the 19 (S) parts, as the
  MKS-30's, would confirm the Gm bins.
- Any published reverse β of 2SC2603s at 2-20 µA of base current.
- The AudioRealism unit's TM setting (or its clap's p-p at the jack with LEVEL at centre), which
  would settle §4's (a) against (b).
- The rim shot (§3.3) and the BD's body (§3.4).

Scripts: the fits used scratch harnesses (variant netlists through `TR909_NETLISTS` and the
tools' `--netlists`, every level against AR's one interface gain); the numbers above can be
regenerated with `tr909voicerun` and `real909.py` as described in §3.
