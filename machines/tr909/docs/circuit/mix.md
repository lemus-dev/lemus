# Mixer, MULTI OUT and master outputs (mix.cir)

The multi-jack board JK-1 73133100 and the master section of the voicing board: each voice's
MULTI OUT jack and its two mix resistors, the two summers with MASTER VOLUME, the output networks
to the Left and Right/MONO jacks, and the power-on mute. Sources as `docs/circuit.md` §0: **SN**
p.12 (crop `private/docs/schematic/crops/p12-multi-jack-board-schematic.png`), p.11 grid A-B
22-26 (crop `p11-master-volume-mix-out.png`; `p11-right-edge-power-jack-connectors.png` for the
connector names), p.7 (change information), p.2 (parts list); **NAVA** (`Nava_v1.02_netlist.txt`,
sheet 12, the master section; NAVA has no jack board); **RES** `docs/research.md` A2.5. Every value
was read on the crops at 400 dpi.

## 1. JK-1: MULTI OUT jacks and mix resistors

The voicing board's outputs reach JK-1 over W5 (CN-5 pins 33-42). On JK-1 each goes through a 1k
to its jack's tip; with nothing plugged in, the tip rests on the jack's switching contact, which
feeds the voice's two mix resistors to the two mix buses. A plug lifts the tip off the contact:
the voice leaves the mix and drives the cable alone. The sleeves are grounded.

| Voice | Pin | Series R | Jack C (from SN 415300) | To the left bus (pin 45) | To the right bus (pin 43) | L − R |
|---|---|---|---|---|---|---|
| BD | 33 | R500 1K | C500 .01 | R501 12K | R502 12K | 0 |
| SD | 34 | R503 1K | C502 .01 | R504 12K | R505 12K | 0 |
| LT | 35 | R506 1K | C503 .01 | **R507 8.2K** | R508 15K | **+5.2 dB** |
| MT | 36 | R509 1K | C504 .01 | R510 12K | R511 12K | 0 |
| HT | 37 | R512 1K | C505 .01 | R513 15K | **R514 8.2K** | **−5.2 dB** |
| RS | 38 | R515 1K | C506 .01 | R516 12K | R517 12K | 0 |
| HC | 39 | R518 1K | C507 .01 | R519 12K | R520 12K | 0 |
| HH | 40 | R521 1K | C508 .01 | R522 15K | **R523 8.2K** | **−5.2 dB** |
| Crash | 41 | R524 1K | C509 .01 | R525 12K | **R526 10K** | −1.6 dB |
| Ride | 42 | R527 1K | C510 .01 | **R528 8.2K** | R529 15K | **+5.2 dB** |

The jack capacitors have no C501: C501 0.1 (same change, the arrow under "SN 415300-UP C500-512 and")
joins JK-1's ground to the chassis, out of the signal path.

**Which bus is left.** JK-1 prints the upper resistor of each pair (R501, R504, …) going to pin 45
"RMIX" and the lower to pin 43 "LMIX". The voicing board prints pin 43 "MIX IN (R)" and pin 45
"MIX IN (L)", and its wiring agrees with its own names: pin 45 → C153 → IC66b → R474, C155 → pin 47
"MIX OUT (L)" → JK-1 pin 47 "LOUT" → R530 → the Left jack. So the upper resistors are the left
ones and JK-1's two MIX labels are swapped (RES A2.5 reads it the same way). Recordings agree:

- **Charterhouse, "TR-909 vs TR-8S vs MC-707"** (`reference/real909/videos/charterhouse_tr909_vs_tr8s_vs_mc707/`,
  one chapter per instrument, three machines a chapter), L − R energy per 0.5 s, first block of each
  chapter (the TR-909, by the title's order): BD, SD, MT, RS, HC 0.0 dB; LT +4.5 to +4.7; HT −4.5;
  CH and OH −4.5; crash −1.5; ride +5.9 to +6.0. Every sign and the crash's −1.5 dB match; the mix's
  own L − R (§4) is +4.8 to +5.0 dB at the toms' frequencies and ±5.25 dB for hats and ride, so the
  others are within 0.75 dB (resistor tolerance: 5 % parts give ±0.9 dB). The other two blocks put
  the toms, hats and ride on the same sides (the TR-8S and MC-707 copy the layout), with the crash
  on the other side in the second.
- Hedonia (TR-909 and TR-1000, stereo outs, compressed): the 6-20 kHz band leans right by 2-4 dB
  once the hats come in. Weak (two machines alternate), but the same side.
- RES: Gearspace posters recall "hi-hat right, toms going left to right".

**Netlist.** Per voice: `R5xx` voice → `m<v>`, `C5xx` (`{cjack}`), `SJ<n>` (the switching contact,
closed while `plug_<v>` < 0.5), the two mix resistors, and `SX<n>`/`RX<n>`: while plugged, the
cable's input `rmulti` (100k, not on the schematic) on the jack.

## 2. The master section (voicing board)

- **C153 1µ/50** (left) and **C154 1µ/50** (right) take the buses into the (−) inputs of **IC66b**
  and **IC66a** (M5218L), (+) grounded: inverting summers. No series resistor: the bus sits at the
  summer's virtual ground through 1 µF, so with the mix resistors (and the 1k / 0.01µ before them)
  the buses are high-passed, first order, at about **105 Hz (L) and 115 Hz (R)** (the netlist; RES
  estimated 130 Hz). Each voice's branch also sees the bus voltage below that corner, so a voice's
  level at 100 Hz depends a little on the others' resistors: LT's L − R is +4.8 dB at 100 Hz,
  +5.25 dB from 1 kHz.
- **VR28 MASTER VOLUME, 50K(B) dual** (parts list p.2: EWK77A027B54, linear), one half in each
  summer's feedback: one end at the (−) input, the other end and the wiper together at the output
  (the dashed line ganging a and b). So the feedback is the track between the (−) end and the
  wiper, 0 to 50 kΩ, and the gain is proportional to the rotation: −6 dB at mid, no output at 0.
  Netlist `P28B o66b o66b n66b 50k volume`.
- **R474 / R473 1K** and **C155 / C152 10µ/16** to pins 47 / 48.
- The summers invert: the master outputs are in antiphase with the MULTI OUTs (Regebro measured
  it, RES A2.5).
- The summers clip at ±13.5 V (`opsat`). With the volume up and several voices hitting together the
  sum easily gets there (at full volume a centred voice has +8.25 dB, ×2.6: one peaking at 5 V asks
  for 13 V), so this matters. They are given no slew limit: a full 20 kHz swing needs 1.6 V/µs, under the
  M5218's 2.2, and the slewing model keeps the solver out of its linear mode (§8).

**Gains** (the netlist at 1 kHz, dB re the voice's output; `rload` and `rmulti` 100k):

| Voice | L, volume 1 | R, volume 1 | L, volume 0.5 | MULTI OUT, unplugged | MULTI OUT, plugged |
|---|---|---|---|---|---|
| BD, SD, MT, RS, HC | +8.25 | +8.25 | +2.23 | −1.35 | −0.10 |
| LT, ride | +11.40 | +6.16 | +5.38 | −1.51 | −0.10 |
| HT, HH | +6.16 | +11.39 | +0.14 | −1.51 | −0.10 |
| Crash | +8.13 | +9.71 | +2.11 | −1.47 | −0.10 |

A MULTI OUT is 1.3-1.5 dB louder with a cable in it than its voltage while it feeds the mix.

## 3. Master outputs (JK-1) and MONO

- Pin 47 → **R530 2.2K** → the Left jack, with **C511 .01** and **R531 10K** to ground; pin 48 →
  **R532 2.2K** → Right/MONO, **C512 .01**, **R533 10K**.
- Seen from a jack: (1k + 2.2k) ∥ 10k = **2.42 kΩ**, 0.76 of the summer's output (−2.4 dB) unloaded;
  with C511 a first-order low-pass at **6.6 kHz** (6.7 kHz into 100k, 8.2 kHz into 10k). The spec's
  "6 Vp-p, 1 kΩ" (SN p.1) is nominal.
- **MONO.** The Left jack's switching contact is wired to the right output: with nothing in the Left
  jack the two outputs are joined (through the 2.2k's, an average) and Right/MONO carries the mono
  mix. A plug in Left separates them. The Right/MONO jack's own contact is tied to its tip (no
  switching). Netlist: `SJL l r !plugl`, control `plugl` (default 1: stereo).
- `RLL` / `RLR`: the inputs the master outputs drive, `rload` 100k (not on the schematic).

## 4. The loads the jack board puts on the voices

Each voice netlist ends with the jack board's load on its output (its `out` is the voltage at
CN-5): the 1k, the 0.01µ (late boards) and the two mix resistors to the buses, which are virtual
grounds above about 110 Hz. As resistors to ground:

| Voice | Load (R50x + R_L ∥ R_R) | Network to put in the voice netlist |
|---|---|---|
| BD, SD, MT, RS, HC | **7.0 kΩ** | 1k to the jack node; .01µ, 12k, 12k from it to ground |
| LT, HT, HH, ride | **6.30 kΩ** | 1k; .01µ, 8.2k, 15k |
| Crash | **6.45 kΩ** | 1k; .01µ, 12k, 10k |
| Any, with a cable in its jack | 1k + rmulti | 1k; .01µ, the cable's input |

Above the jack filter's corner the load falls towards the 1k: 1.36 kΩ at 20 kHz (late boards). The
voices' outputs are M5218 outputs (±50 mA), so the load only matters for what the voice netlists
read at `out`; below 110 Hz the buses aren't quite grounds and the true load is somewhat higher.
The voice netlists that are in already use these networks (bd, sd, lt, mt, ht, rs, hc, rd; R50x and
C50x with their two mix resistors to ground).

## 5. Power-on mute (voicing board, p.11 grid A-B 23-24)

- **C143 47µ/35** from +15 V to node M1; **R472 470K** M1 to −15 V; **D195** (cathode at M1) and
  **R471 4.7K** to −15 V. Running, C143 holds 30 V and M1 sits at −15 V.
- **D194** (anode at M1) → M2 → **R469 22K** → the base of **Q82**, **R470 22K** → the base of **Q83**
  (2SC2878, "Q82, Q83: 2SC2878"); emitters grounded, collectors at pins 48 and 47 (after C152 and
  C155). NAVA: N$274, N$770, N$780-N$784.
- **Q79** (PNP, emitter on **C142 10µ/16**, charged from +15 V through **D193**; base to +15 V through
  **R468 10K**; collector at M2): when the +15 V rail falls at power-off, C142 holds Q79's emitter
  up, Q79 conducts and mutes the outputs while the rails go.
- **Power-on.** C143 is discharged, so M1 starts at +15 V: D194 and the 22k's saturate Q82 and Q83,
  which short pins 47 and 48 to ground (while C152 and C155 charge, so no thump). M1 falls as the
  bases' current and R472 charge C143. In the netlist (a steady tone at volume 1, peak-to-peak
  against its level a minute later): **−57 dB at 0.5 s, −49 dB at 0.9 s, −37 dB at 1.4 s, −22 dB at
  1.7 s, −9 dB at 1.8 s, −2 dB at 2.0 s**, within 0.5 dB from 2.5 s after the rails come up.
- **After the mute** the bases still draw current through D194 whenever an output swings below
  M1 − 1.2 V: the 2SC2878 conducts collector to emitter in reverse (reverse h_FE 150: the reason
  the part is used, so that it mutes both half-waves). With C152 / C155 in series this is a DC
  restorer: an output's negative peaks are held at about M1 − 1.2 V and the waveform rides up on
  the coupling capacitor (a ±1.7 V tone sits between −0.3 and +2.9 V at 2.2 s), and a hit's first
  negative swing is clipped there until C152 recharges (τ about 0.13 s through R530 and R531).
  M1 falls with C143's charge through R472 (τ = 22 s): it passes 0 V at about 3.3 s, −4 V at 10 s,
  −10.5 V at 30 s. A ±1.7 V output is clean from about 5 s, a ±10 V one only after half a minute.
  Only the master outputs are affected (the MULTI OUTs and the voices are not).
- **Netlist.** Input `pon` is C143's positive end: hold it constant (+15 V) while running. For a
  power-on, solve the DC point with `pon` = −15 V (C143 then holds 0 V, as when discharged) and step it
  to +15 V. **RLK 100Meg** M2-M1 stands for the leakage of D194 and Q79 (not on the schematic): the
  bases' only DC path while the mute is off, holding them near −15 V (the solver needs one; with the
  2SC2878's 25 V V_EBO the real bases can sit there too). Q79, C142, D193, R468 aren't in the netlist
  (the rails don't fall in it).

## 6. Early units

SN p.7: from SN 415300 "C500–C512 0.01µ added" ("Noise is high in OUTPUT"). Earlier jack boards
have none of them: no 18 kHz pole at the MULTI OUTs, no 6.6 kHz pole at the master outputs.
`.param cjack=1p` for those (a stray instead of 0.01µ: the solver takes no 0 F capacitor); the
voice netlists have their own `c50x` parameters for their copies of the jack load. Default: the
late board.

## 7. Check against the real 909

**Master against MULTI OUT** (the hi-hat, ROM playback, so the two recordings of one unit compare
directly): AudioRealism's master-out and individual-out open hats, as RES A2.5 measured them,
against the netlist (master R / MULTI OUT with a cable in it, both into 100k; re 1 kHz):

| | 63 Hz | 100 Hz | 160 Hz | 4 kHz | 6.3 kHz | 10 kHz | 16 kHz | 20 kHz |
|---|---|---|---|---|---|---|---|---|
| AudioRealism | −5.0 | −3.9 | −1.5 | −1.1 | −2.5 | −4.7 | −7.4 | −8.9 |
| mix.cir (R; L) | −6.8; −6.0 | −3.9; −3.3 | −1.9; −1.5 | −1.1 | −2.4 | −4.6 | −7.6 | −9.3 |
| mix.cir, MULTI OUT into 10k | | | | −1.1 | −2.5 | −4.8 | −8.0 | −9.8 |

Within 0.4 dB from 100 Hz up; the 63 Hz point is 1-1.8 dB deeper in the netlist (a bus corner
slightly higher than the unit's, or the recording's own low end). Regebro's rev-4 unit (MP3):
−2.6 to −4.0 dB at 100 Hz, −2.2 at 6.3 kHz, −4.5 at 10 kHz, −7 at 16 kHz: the same within his
codec's spread. The MULTI OUT alone: −0.6 dB at 6.3 kHz, −1.4 at 10 kHz, −3.1 at 16 kHz, −4.2 at
20 kHz (the 1k / 0.01µ into 100k; RES: Regebro's "15 kHz 6 dB/oct").

**Pans:** §1, against Charterhouse's video. **Polarity:** inverted at the master outputs, as Regebro
measured.

## 8. SPICE and CPU

`tools/spice/cases/mix.py`, 96 kHz (difference in % of each output's range; outputs with signal):

| Case | What | Output: max %, rms % |
|---|---|---|
| mix | BD 55 Hz, LT 130 Hz, HH 7 + 11 kHz, crash 3.1 kHz, ride 4.7 kHz; volume 0.5 | l 0.21, 0.077; r 0.34, 0.130; mbd 0.01, 0.001; mlt 0.02, 0.001; mhh 1.88, **0.643**; mcr 0.30, 0.127; mrd 0.61, 0.280; busl 0.19, 0.046; busr 0.35, 0.091 |
| mix_vol0 | the same, volume 0 | mhh 1.87, 0.640; mrd 0.60, 0.279; the rest ≤ 0.30, 0.13 |
| mix_vol1_clip | BD 7 V, SD 5 + 2 V, HT 4 V; volume 1, both summers clipping | l 0.31, 0.027; r 0.30, 0.026; all others ≤ 0.23, 0.021 |
| mix_plugged_mono | BD and HH plugged, Left empty (mono); volume 0.8 | l, r 0.11, 0.054; mhh 1.74, 0.634; mrd 0.62, 0.287; the rest ≤ 0.30, 0.131 |
| mix_poweron | C143 discharged at 0.05 s (2 s); volume 1 | l 3.05, 0.009; r 2.51, 0.009 (the peak at the power-on step); mhh 0.39, 0.090; the rest ≤ 0.32, 0.049 |

The hi-hat jack's 0.64 % RMS is the trapezoidal rule's frequency warping on the 1k / 0.01µ pole
(18.6 kHz) with 7 and 11 kHz in it at 96 kHz: at 192 kHz it is 0.144 % (and ride 0.067 %, l 0.020 %).
Everything else is within 0.3 % RMS.

**CPU** (Apple M-series, one core, 96 kHz, the solver's own timing): 3.9-4.1 % of real time in
steady state (linear mode: no Newton iterations), 5.7 % at full volume with the summers clipping
part of the time. With the slew limit on IC66 it was 12.6 %, and with the mute's bases left floating 7 %
(both kept the solver in Newton every sample).

## 9. Interface

- **Inputs:** bd sd lt mt ht rs hc hh cr rd (each voice's `out`), pon (§5).
- **Controls:** `volume` (VR28, 0.5); `plug_bd` … `plug_rd` (0); `plugl` (1).
- **Outputs:** `l`, `r` (the jacks), `mbd` … `mrd` (each MULTI OUT jack), `busl`, `busr` (probes).
- **Params:** `cjack` (0.01u; 1p early), `rmulti` (100k), `rload` (100k).
- **Rate:** the board rate; linear apart from clipping and the mute, so it could run at the host
  rate where the summers don't clip.

## 10. Open questions

- The interfaces' inputs (`rmulti`, `rload`): 100k matches AudioRealism's tilt best; a 10k input
  moves the master's corner to 8.2 kHz and takes 1.7 dB off.
- The negative-swing clamp after the mute (§5) follows from the drawing and the 2SC2878's reverse
  h_FE; nobody seems to have reported it. A recording of a hot master output in the first ten seconds
  after power-on would show it.
