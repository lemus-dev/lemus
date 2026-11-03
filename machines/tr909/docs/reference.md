# Reference measurements: the target

What the real TR-909 measures at its knob settings, from the recordings in
`private/reference/real909/` (`index.md` there says what each set is), and what Roland's TR-909
plugin gives at the same settings (`private/reference/roland-vst/`). This is what the model is
validated against.

Every number here comes from one measuring library, `tools/compare/real909.py`, run over every
clip by `tools/compare/reference.py`; that script writes the full tables
(`private/reference/real909/_measurements_reference/tables.md`) and the per-clip numbers
(`measurements.json` there). The per-voice compare scripts and the final validation use the same
library on our renders, so both sides are measured the same way.

Knob values are pot rotations 0-1: AudioRealism's percentages / 100, Rob Roy's panel dial / 10.

## Sources

| set | unit | revision (from the sound) | format, processing | gives |
|---|---|---|---|---|
| **AR** AudioRealism Ultimate (2025) | one unit | late: RS F1 478 Hz (R417 3.3 k), HH low cut at about 700 Hz (C134 0.01 µ); so SN 415300 or later | 96 kHz/24-bit (one OH DECAY sweep only at 44.1 kHz/16-bit), individual outs, LEVEL 50 %, not normalised, trimmed to the onset; each clip ends up to 2.3 ms into the next hit (real909.py cuts 3 ms) | BD and SD 5x5x5, toms 11x11, CH DECAY x5, OH DECAY x5, CR/RD TUNE x11, 11 accent levels per voice, 10 RS and 8 HC shots |
| **RR** Rob Roy (1995) | a second unit (Manna Studios) | between SN 381500 and 415300: BD TUNE range as AR's (C9 0.33 µ), no 1 kHz line in the RS (R417 12 k), no HH low cut (C134 10 µ) | 44.1 kHz/16-bit through an E-mu ESI-32, normalised, starts and tails trimmed (most t-40 and some t-20 can't be seen), polarity inverted | BD, SD, toms at dial 0/3/7/10; CH, OH, CR, RD at 0-10 in 2s; RS at MIDI velocity 63 and 127 |
| **Regebro** (SoundCloud 2022) | Lennart Regebro's 909 | rev 4 (his statement): RS F1 500 Hz | 128 kbps MP3, individual and main outs, knobs not stated | every voice at four accent states |
| **scherer** ("living sounds", 2009) | a third unit's hats | - | 96 kHz/24-bit (recorded at 192 kHz), mix out then HH individual out, not normalised | CH and OH DECAY turned together from minimum to maximum, about 8 steps, in a 16-step pattern (accented and plain CH) |
| **scherer 909** ("living sounds", 2010; `909.rar`, fetched 2026-10-22) | the same poster's unit, presumably | - | 88.2 kHz/24-bit mono takes, output mostly not stated (clap, rim shot and one CH take individual out), knobs unknown, nothing labelled | BD, SD, toms, OH, CH, crash and ride with knobs turned during the takes; 32 claps, 36 rim shots; only knob-free measures (spectral shapes, VCO ratios, waveform ratios) taken |
| **KB6**, **ESW** | unknown | - | 44.1 kHz, no settings; ESW partly processed | spot checks of invariants (BD end pitch, VCO ratios, ROM clocks) |
| **Roland** | TR-909 plugin 1.1.3 | - | 48 kHz renders, single-knob sweeps around its default kit, MIDI velocity 127 | the same measurements at the same knobs |

Caveats that apply throughout:

- **One unit per knob grid.** Only AR covers the knobs finely; RR checks it at four marks.
  Unit-to-unit spread (below) is 1-5 % on most pitches, 13 % at the bottom of the SD's TUNE
  range, 7 % on the BD's end pitch and 12 % on one VCO ratio, so a model within those of AR is
  within the spread of real 909s.
- **Knob positions were set by hand.** AR's toms show ±1 Hz of scatter around a smooth law, and
  AR's accent series, recorded in their own sessions, sit off the grid at the same nominal knobs:
  LT +60 cents, SD +35, HT -22, RD -23, CR -6, MT +8; its CH accent takes decay longer (t-20 120
  against 93 ms, another CH or OH DECAY). Resetting a knob by hand lands within about ±0.05 of a
  turn; compare accent effects within a series, not against the grid.
- **AR's accent state** is inferred, not stated (Accent, below): its grid plays at about DAC code
  21 for the analog voices, RS and HC (40 for CH, 32 for OH, CR, RD), i.e. an accented step at
  TOTAL ACCENT 0. Render the grid at acc = 5 V x 21/63; the "accentNNN" takes run from code 21
  to 47 (`real909.ar_code(voice, accent)` gives each clip's code).
- **Recording chains are unknown** beyond what the sets say; levels are only comparable within a
  set (AR: one interface gain throughout, LEVEL 50 % on every voice).

## Measures (`tools/compare/real909.py`)

| name | definition |
|---|---|
| onset | first sample within 30 dB of the peak, moved back to the last sample 50 dB down (at most 3 ms) |
| t-20, t-40, t-60 | last time the 10 ms moving-RMS envelope is above its peak -N dB, from the onset; NaN when the level is within 6 dB of the clip's floor (its last 50 ms) |
| tail tau | exponential time constant of a line fitted to the envelope (dB) from -10 dB to -40 dB or the floor |
| zero-crossing track | positive-going crossings of the low-passed (BD, 1.5 kHz) or band-passed (toms, 0.7-1.4 f_main) signal; one frequency per period |
| sweep fit | f(t) = f_end + (f(0) - f_end) e^(-t/tau) over the track, t from the onset (BD 0-350 ms, toms 3-500 ms) |
| lines | zero-padded FFT peaks: SD f_low/f_high in 30-150 ms (f_high the strongest at 1.35-1.65 f_low), "early" the strongest in 2-20 ms; toms f_main/f_low in 50-250 ms (f_low the strongest at 0.55-0.8 f_main), early 3-25 ms; RS the three strongest in 0-30 ms |
| click | BD: energy above 1 kHz in the first 5 ms, dB re the whole hit |
| SD noise | the hit high-passed at 2 kHz: its t-20/t-40 and peak |
| rms 0-50 ms | the accent measure |
| ROM clock | the hats' and cymbals' sample clock: the ROM rendered as a zero-order hold at trial clocks, both band-passed 1.5-12 kHz, the highest normalised cross-correlation with 3-43 ms of the recording (NCC 0.9-0.98 on AR) |
| LSD | 1/6-octave band spectra, each set to its own total, RMS difference over the bands within 40 dB of the top and 10 dB above the floor, 30-330 ms (0-80 ms flat-start for a hit 20 dB down within 50 ms), as the 808's |

## Repeatability: the floor of each measure

The same real voice measured twice, at the same setting, by the measures above (LSD in dB;
decay as |log2| of the t-20 ratio, octaves):

| takes | LSD median (max) | t-20 |
|---|---|---|
| AR RS, 10 shots | 0.01 (0.04) | 0.001 |
| AR SD drum part, SNAPPY 0, the 5 TONE takes (TONE inert there) | 0.06-0.08 (0.13) | 0.001 |
| AR BD grid against its 0 % and 10 % accent takes (code 21 both) | 0.23 (0.25) | 0.000 |
| AR OH grid (44.1 kHz) against the 96 kHz accent 90/100 % takes | 0.15 (0.16) | 0.009 |
| AR CR grid against its 0/10 % accent takes (clock 0.4 % apart) | 0.45 (0.63) | 0.034 |
| AR HC, 8 shots | 2.53 (3.46) | 0.17 (0.50) |
| Regebro (MP3), the same accent state twice: RS / OH / RD / HC | 0.63 / 0.41 / 0.34 / 2.06 | 0.00-0.10 |

- The analog voices and the ROM voices repeat almost exactly; a model can be held to a few
  tenths of a dB of LSD on them where the knobs are known. The clap's noise makes each hit
  different: 2.5 dB of LSD and ±0.17 octave of t-20 is its floor, so compare medians of several
  hits. The snare's noise part (SNAPPY > 0) behaves likewise (grid against accent take: 2.4 dB).
- LSD on a tonal voice is mostly pitch: AR's LT grid against its own accent takes scores 7.9 dB,
  from their 60-cent pitch difference alone.

## BD

TUNE sets only the pitch sweep's time constant; the end pitch stays at 49.2-49.7 Hz (AR, ATTACK 0,
DECAY 1; the 25-cell columns span every ATTACK and DECAY at that TUNE):

| TUNE | first period Hz | f 10 ms | f 50 ms | f 100 ms | fit f(0) | f_end | tau ms | tau, 25 cells | RR dial | RR first | RR f_end | RR tau | Roland tau |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 146.7 | 125.4 | 50.0 | 49.4 | 294 | 49.22 | 7.60 | 6.8-7.7 | 0 | 108.9 | 49.29 | 8.21 | 7.24 |
| 0.25 | 173.4 | 141.1 | 51.2 | 49.6 | 289 | 49.33 | 9.66 | 9.4-9.7 | 3 | 145.8 | 49.34 | 10.84 | 8.87 |
| 0.5 | 190.7 | 154.5 | 53.8 | 49.7 | 286 | 49.39 | 11.79 | 10.8-11.9 | | | | | 13.81 |
| 0.75 | 204.6 | 169.1 | 57.8 | 49.8 | 284 | 49.45 | 14.50 | 13.9-14.7 | 7 | 188.3 | 49.46 | 18.03 | 20.85 |
| 1 | 238.7 | 211.7 | 91.4 | 56.8 | 279 | 49.54 | 29.14 | 27.4-29.4 | 10 | 214.8 | 49.46 | 29.29 | 29.36 |

- The fitted sweep starts at 279-294 Hz at the onset on every setting; the first measurable period
  (centred at 5-7 ms) is 147-239 Hz. RR's first periods are lower because its starts are trimmed;
  its fitted f(0), 264-279 Hz, agrees within 7 %.
- f_end on other units: RR 49.3-49.5 Hz, Regebro 52.7 Hz, KB6 52.3 Hz (median of 50 clips,
  47-61), Roland 52.6 Hz. Treat f_end as a per-unit constant, 49.2-52.7 Hz (±3.5 %).
- Over 200-400 ms the pitch is 0.1-0.15 Hz below the fitted end (TUNE 0-0.75); fork E's slow
  sag continues to 46-48 Hz just before the oscillation stops.
- TUNE law: with C9 0.33 µ, R57 22 k, R26 330 k, the fitted tau gives VR2 = 2.8 k / 10 k / 18 k /
  29 k / 99 k at 0 / 0.25 / 0.5 / 0.75 / 1: a strongly expanding (audio) taper on the 100 k(A)
  pot. RR's dial 3 (10.8 ms) is within 7 % of AR's curve; its dial 7 (18.0 ms) is above AR's
  0.75 (14.5 ms), so the taper's upper half differs between units (or RR's dial marks from AR's
  percentages). Roland's tau runs 17 % (0.5) and 44 % (0.75) longer than AR's in the middle.

DECAY lengthens a hold and then a decay (AR, TUNE 0.5, ATTACK 0; ms; Roland at ATTACK 0.5):

| DECAY | t-20 | t-40 | t-60 | tail tau | t-20 over TUNE x ATTACK | Roland t-20 | Roland t-40 | Roland t-60 |
|---|---|---|---|---|---|---|---|---|
| 0 | 105 | 154 | 375 | 29 | 99-107 | 91 | 118 | 147 |
| 0.25 | 152 | 187 | 426 | 29 | 134-153 | 124 | 176 | 241 |
| 0.5 | 173 | 218 | 453 | 36 | 165-183 | 189 | 299 | 430 |
| 0.75 | 213 | 327 | 491 | 52 | 185-215 | 246 | 404 | 593 |
| 1 | 288 | 487 | 569 | 90 | 279-290 | 286 | 480 | 708 |

- The real BD's t-60 is long at every DECAY: after the oscillation stops the output makes one
  slow excursion and holds a small offset about 41 dB down for 150-250 ms (fork E's `bd_tail.png`),
  which a model of the envelope and VCA bias has to reproduce. Roland's plugin has no such tail.
- RR (TUNE 7): tail tau 34 / 62 / 97 ms at DECAY 3 / 7 / 10, against AR's 29-36 / 52 / 90.

ATTACK adds a click and hardly moves the pitch (AR, TUNE 0.5, DECAY 1; the last two columns at
DECAY 0.5, as Roland's renders):

| ATTACK | peak dBFS | rms 0-50 ms | click dB | first period Hz | click, DECAY 0.5 | Roland click |
|---|---|---|---|---|---|---|
| 0 | -12.1 | -16.8 | -38.8 | 190.7 | -37.1 | -39.9 |
| 0.25 | -11.6 | -16.8 | -37.2 | 192.2 | -36.1 | -33.6 |
| 0.5 | -10.9 | -16.9 | -35.4 | 192.7 | -34.1 | -32.4 |
| 0.75 | -10.2 | -16.8 | -34.6 | 191.1 | -32.7 | -31.6 |
| 1 | -8.2 | -16.7 | -30.9 | 194.0 | -29.2 | -30.6 |

The real click grows evenly over the knob (8 dB, ATTACK 0 to 1); Roland's jumps 6 dB between 0
and 0.25 and then rises 3 dB.

## SD

Two lines at a ratio of 1.50 (AR, SNAPPY 0; TONE does nothing to them, the five TONE takes read
the same to 0.1 Hz). "early" is the strongest line in the first 20 ms (the pitch bend):

| TUNE | f_low Hz | f_high Hz | ratio | early Hz | t-20 ms | t-40 ms | RR dial | RR f_low | RR f_high | Roland f_low | Roland f_high |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 116.3 | 177.4 | 1.525 | 149.0 | 88 | 148 | 0 | 131.2 | 187.7 | 112.4 | 165.0 |
| 0.25 | 144.1 | 217.0 | 1.506 | 166.3 | 86 | 148 | 3 | 147.3 | 211.4 | 142.0 | 212.1 |
| 0.5 | 173.4 | 260.1 | 1.500 | 197.7 | 85 | 148 | | | | 172.6 | 253.8 |
| 0.75 | 200.9 | 301.2 | 1.499 | 229.4 | 85 | 148 | 7 | 206.3 | 298.1 | 197.9 | 294.7 |
| 1 | 230.7 | 346.1 | 1.500 | 258.7 | 87 | 147 | 10 | 234.3 | 338.7 | 225.4 | 328.3 |

- TUNE law: f_low rises 27.8 / 29.2 / 27.5 / 29.9 Hz per quarter turn: linear in rotation (VR6
  10 k(B) in a divider), span x1.98. RR's span is narrower (x1.79, the low end 13 % higher).
- The ratio is a capacitor ratio and varies between units: AR 1.50, RR 1.43-1.45, KB6 1.50,
  Regebro 1.39. Roland: 1.46-1.47 (1.47 = C69/C71 nominal).
- The early line is 12-15 % above f_low (28 % at TUNE 0): the service notes' 20 ms pitch bend,
  averaged over 2-20 ms.
- The drum part decays the same at every TUNE: t-20 85-88 ms, t-40 147-148 ms, tail tau 37-38 ms.

The snappy noise (above 2 kHz), TUNE 0.5: TONE sets its length, SNAPPY its level (t-20 / t-40 ms):

| TONE | SNAPPY 0.25 | 0.5 | 0.75 | 1 | Roland (SNAPPY 0.5) | RR (SNAPPY 10) |
|---|---|---|---|---|---|---|
| 0 | 83 / 99 | 85 / 100 | 83 / 100 | 86 / 101 | 74 / 87 | 107 / 119 (dial 0) |
| 0.25 | 147 / 179 | 147 / 181 | 147 / 181 | 150 / 182 | 165 / 210 | |
| 0.5 | 181 / 228 | 178 / 229 | 181 / 226 | 177 / 228 | 201 / 268 | |
| 0.75 | 204 / 262 | 195 / 261 | 207 / 266 | 200 / 262 | 222 / 300 | |
| 1 | 211 / 300 | 224 / 300 | 226 / 299 | 222 / 297 | 234 / 323 | |

| SNAPPY | 0 | 0.25 | 0.5 | 0.75 | 1 |
|---|---|---|---|---|---|
| noise peak re hit peak, AR dB | -30.9 | -12.0 | -11.5 | -8.3 | -6.0 |
| Roland | -11.0 | -10.1 | -7.0 | -1.4 | +1.0 |
| centroid 0-60 ms, AR Hz | 234 | 382 | 734 | 1354 | 2072 |
| Roland | 254 | 353 | 638 | 1559 | 2652 |

- SNAPPY does not change the noise's decay, only its level. TONE leaves the noise's peak within
  1 dB from 0.25 to 1 (at TONE 0 it is 3-4 dB hotter and much shorter).
- At SNAPPY 0 the real snare has almost no noise (31 dB down); Roland's keeps it 11 dB down, and
  its SNAPPY range is 7 dB hotter at the top.

## Toms

Each tom has a main line and a lower one at 1.47-1.48 x below it (AR, DECAY 1; f in 50-250 ms):

| TUNE | LT f_main | LT f_low | LT early | MT f_main | MT f_low | MT early | HT f_main | HT f_low | HT early |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 62.4 | 42.5 | 83.6 | 78.6 | 52.9 | 119.0 | 90.7 | 61.3 | 127.4 |
| 0.1 | 63.5 | 43.2 | 87.8 | 81.6 | 55.0 | 124.0 | 93.0 | 62.9 | 129.6 |
| 0.2 | 71.6 | 48.6 | 102.4 | 88.6 | 59.8 | 132.1 | 103.2 | 69.7 | 139.3 |
| 0.3 | 78.5 | 53.3 | 110.3 | 97.0 | 65.4 | 136.3 | 115.1 | 77.8 | 152.2 |
| 0.4 | 84.7 | 57.5 | 115.0 | 103.7 | 70.0 | 139.8 | 122.4 | 82.8 | 158.7 |
| 0.5 | 90.8 | 61.7 | 118.9 | 113.6 | 76.6 | 147.5 | 133.7 | 90.4 | 164.7 |
| 0.6 | 99.6 | 67.7 | 126.5 | 121.9 | 82.2 | 155.1 | 140.1 | 94.7 | 174.0 |
| 0.7 | 106.9 | 72.6 | 134.6 | 131.0 | 88.4 | 164.9 | 152.0 | 102.8 | 184.5 |
| 0.8 | 114.3 | 77.6 | 143.1 | 137.9 | 93.0 | 167.5 | 161.7 | 109.4 | 192.3 |
| 0.9 | 122.5 | 83.3 | 152.0 | 146.8 | 99.1 | 177.9 | 173.9 | 117.8 | 203.3 |
| 1 | 123.1 | 83.6 | 152.8 | 148.4 | 100.2 | 178.5 | 175.0 | 118.5 | 205.2 |

- DECAY doesn't move the pitch: f_main over the 11 DECAY settings stays within 0.3 Hz (LT), 0.6
  (MT), 0.9 (HT) at every TUNE.
- **TUNE law: linear in rotation between dead ends.** A line between two stops fits f_main with
  0.7 Hz rms (LT, MT) and 1.2 Hz (HT): the line runs from k = 0.086 / 0.077 / 0.072 to k = 0.921 /
  0.922 / 0.929 (LT / MT / HT) and the pitch is flat outside. An exponential in rotation fits
  worse (17-28 cents rms against 12-17 for the line). That is the 10 k(B) TUNE pot in the
  ladder (VR10, VR13, VR16), with about 8 % of dead travel at each end; the span is x1.97 / x1.89
  / x1.93 (nominal x2.0).
- The pitch envelope: the early line (3-25 ms) is 24-34 % above f_main on LT, 20-51 % on MT,
  17-41 % on HT (most at TUNE 0); over 250-500 ms it sags a further 2-5 % (f at 300 ms 0.95-0.99 f_main).
- The fitted sweep's tau is 46-118 ms but noisy (the track mixes the fast bend and the slow sag);
  use f_early and f_300ms rather than tau as targets.

DECAY (AR, TUNE 0.5; ms; the spread over the 11 TUNE settings in brackets):

| DECAY | LT t-20 | LT t-40 | LT t-60 | MT t-20 | MT t-40 | MT t-60 | HT t-20 | HT t-40 | HT t-60 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 173 (173-183) | 285 | 347 | 123 (121-130) | 192 | 314 | 125 (121-127) | 185 | 329 |
| 0.1 | 202 | 310 | 373 | 139 | 215 | 346 | 133 | 201 | 366 |
| 0.2 | 240 | 360 | 453 | 160 | 252 | 386 | 158 | 272 | 418 |
| 0.3 | 268 | 389 | 498 | 183 | 295 | 408 | 184 | 304 | 426 |
| 0.4 | 275 | 452 | 564 | 187 | 305 | 415 | 200 | 327 | 441 |
| 0.5 | 302 (280-302) | 486 | 631 | 192 (189-213) | 333 | 435 | 203 (199-221) | 350 | 451 |
| 0.6 | 310 | 522 | 674 | 212 | 352 | 452 | 226 | 374 | 492 |
| 0.7 | 336 | 556 | 769 | 215 | 379 | 499 | 228 | 397 | 538 |
| 0.8 | 344 | 591 | 784 | 215 | 403 | 526 | 244 | 419 | 565 |
| 0.9 | 345 | 599 | 820 | 239 | 411 | 550 | 246 | 442 | 588 |
| 1 | 345 (332-362) | 620 | 834 | 240 (225-246) | 414 | 553 | 246 (237-262) | 442 | 589 |

- t-40 roughly doubles over the knob (x2.2 / x2.2 / x2.4) and is flat above 0.8-0.9; the tail
  tau goes 58 -> 122 ms (LT), 38 -> 84 (MT), 36 -> 89 (HT).
- The tails carry the recording's 50 Hz hum harmonics about 60 dB down; t-60 is near that floor.

Other units:

| | LT TUNE 0 / 3 / 7 / 10 | MT | HT |
|---|---|---|---|
| RR f_main, Hz | 64.7 / 79.3 / 108.6 / 124.1 | 82.8 / 98.6 / 134.2 / 153.4 | 95.6 / 116.9 / 156.7 / 176.7 |
| RR re AR, cents | +62 / +17 / +27 / +14 | +90 / +29 / +41 / +58 | +92 / +26 / +53 / +16 |
| f_main / f_low: AR, RR, Regebro, KB6 | 1.47, 1.50, 1.52, 1.52 | 1.48, **1.66**, 1.50, 1.45 | 1.48, 1.45, 1.44, 1.45 |
| Roland f_main at 0 / 0.5 / 1, re AR | -112 / -79 / -120 cents | +2 / +12 / +30 | +210 / +150 / +173 |

- RR's toms sit 14-92 cents above AR's, most at TUNE 0 (the low end, as on the snare). RR's
  decays match AR's within 10 % at every mark (t-20 at DECAY 0/3/7/10: LT 197/274/334/362 against
  178/262/325/349 ms).
- RR's MT has its lower line at 1/1.66 instead of 1/1.48 at every TUNE: one capacitor ~12 % off in
  that unit. The VCO ratio is a per-unit constant: 1.44-1.53 across four units, apart from that
  MT. (Regebro's lower lines are 16-29 dB down, his toms being at a short DECAY, so his ratios are
  the least sure.)
- Roland's LT is 80-120 cents flat and its HT 150-210 cents sharp of AR's; its MT matches. Its
  decays (TUNE 0.5) run 0-20 % longer on t-20/t-40 than AR's and far longer on the LT's and MT's
  t-60 (LT 633-952 against 347-834 ms, MT 498-844 against 314-553).

## RS

| | peaks Hz (dB re strongest), first 30 ms | t-20 ms | t-40 ms |
|---|---|---|---|
| AR (10 shots, identical within 0.02 dB) | 224, 1013 (-2.9), 478 (-3.1) | 25.5 | 89.6-89.9 |
| AR accent 0 / 0.5 / 1 | the same lines | 25.6 / 25.5 / 25.5 | 90.9 / 104.4 / 119.4 |
| Regebro (4 accent states) | 219-220, 500 (-3.5), 1022 (-3.6) | 26.3-26.5 | 90.4-104.2 |
| KB6 (5 clips) | 216, 1042, 484 | | |
| RR (early, R417 12 k; vel 63 and 127) | 224, 452 (-2.1), 281 (-10.1) | trimmed | |
| Roland (vel 127) | 213, 469 (-0.6), 985 (-10.2) | 28.9 | 46.3 |

- The three resonators: about 220 Hz (F2, 4.5 ms period), 480-500 Hz (F1, 2 ms) and 1.01-1.04 kHz
  (F3, 1 ms), as the service notes' sketches give them. The R417 change (12 k -> 3.3 k at SN
  415300) acts on F3: SN p.9's 12 k trace has fewer 1 kHz cycles, and RR's early board has no
  1 kHz line at all (its F1 is also lower, 452 Hz against 478-500).
- Accent lengthens the tail (t-40 +30 ms at full TOTAL ACCENT) and not the first 25 ms.
- Roland's F3 is 7 dB weaker and its tail ends twice as early (t-40 46 ms against 90).

## HC

| | bursts, ms from the onset | t-20 ms | t-40 ms | t-60 ms | centroid 0-60 ms Hz |
|---|---|---|---|---|---|
| AR (8 shots) | 0.5-1.1, 10.8-12.1, 22.3-22.6, 32.3-34.1 | 87-123 | 359-412 | 674-707 | 2414-2815 |
| AR accent 0 / 0.5 / 1 | the same | 108 / 109 / 131 | 374 / 394 / 409 | | |
| Regebro (4 states) | 0.5-0.7, 10.5-11.5, 21.4-22.4, 30.9-33.6 | 89-97 | 352-371 | 493-677 | 2258-2749 |
| RR (2 clips) | 0.7, 12.2-12.3, 24.7-26.4, 32.9-33.1 | 133-137 | trimmed | | 2833-3254 |
| Roland (vel 127) | 0.8, 11.6, 23.5, 37.4 | 77 | 317 | 580 | 2405 |

- Four bursts about 10.5-11.5 ms apart (the 4th at 31-34 ms starts the reverb tail); shot to
  shot the burst times move by 1-2 ms and t-20 by ±20 %, so compare medians of several hits.
- Roland's 4th burst comes 3-5 ms late and its t-20 is 25 % short.
- AR's shots peak up to -2.6 dBFS at LEVEL 50 % and the accent-100 take reaches 0 dBFS: the
  clap's peaks may be clipped there; use rms and decay, not peak.

## Hi-hats

The sample clock is fixed (no tune knob): **31.57 kHz** on AR (31 560-31 585 Hz over 32 clips,
NCC 0.64-0.98) and RR (31 560-31 585 Hz), **31.19 kHz** on scherer's (31 183-31 202 Hz, 96 kHz
lossless), 31.40-31.42 kHz on KB6, **30.59 kHz** on Regebro's. The clock is a 4011UB RC astable
(R492, C160), so 3 % between units is plausible; it makes Regebro's figure believable, though a
speed error in his MP3 chain can't be ruled out. Roland plays the same ROM data at 32.85 kHz
(NCC 0.92; +68 cents against AR).

CH DECAY (ms; RR's CH clips are trimmed, only its tail tau shows; Roland at OH DECAY 0.5):

| set | DECAY | t-20 | t-40 | t-60 | tail tau |
|---|---|---|---|---|---|
| AR | 0 / 0.25 / 0.5 / 0.75 / 1 | 25 / 63 / 78 / 87 / 93 | 44 / 105 / 126 / 136 / 141 | - / 139 / 155 / 164 / 172 | 8.6 / 22.3 / 24.5 / 25.6 / 25.9 |
| RR | 0 / 2 / 4 / 6 / 8 / 10 | - / - / - / 91 / - / 99 | | | - / 16.7 / 35.8 / 34.5 / 39.4 / 36.4 |
| Roland | 0 / 0.2 / 0.5 / 0.7 / 1 | 27 / 62 / 89 / 95 / 98 | 49 / 106 / 136 / 139 / 143 | 70 / 139 / 184 / 202 / 204 | 9.5 / 22.0 / 25.2 / 25.9 / 25.7 |

OH DECAY (ms):

| set | DECAY | t-20 | t-40 | tail tau |
|---|---|---|---|---|
| AR (44.1 kHz) | 0 / 0.25 / 0.5 / 0.75 / 1 | 137 / 203 / 220 / 239 / 278 | 265 / 390 / 425 / 454 / 520 | 62 / 87 / 95 / 102 / 117 |
| RR | 0 / 2 / 4 / 6 / 8 / 10 | 151 / 199 / 222 / 241 / 282 / 299 | trimmed | 80 / 99 / 98 / 106 / 116 / 122 |
| Roland | 0 / 0.2 / 0.5 / 0.7 / 1 | 31 / 125 / 202 / 226 / 231 | 50 / 233 / 404 / 436 / 464 | 9 / 54 / 88 / 95 / 100 |

- AR's OH DECAY sweep is the 44.1 kHz edition's (cut at 0.59 s, 54 dB down); its DECAY 1 take
  matches the 96 kHz accent-100 take (t-20 278 / 280 ms, tail tau 117 ms both).
- The OH never gets short: at DECAY 0 its t-20 is still 137 ms (RR 151), where Roland's drops to
  31 ms. Roland's OH law is far wider than the real one's.
- CH at DECAY 0 is 25 ms to -20 dB on both AR and Roland; RR's CH clips are trimmed (only tail tau).
- The CH depends on the OH DECAY knob too (the C135 paths, research.md B.2.4); AR's OH DECAY
  during its CH takes isn't stated.
- scherer's unit, both DECAY knobs turned together from minimum to maximum (individual out; the
  sweep's 16 bars give 8 positions, each heard twice):

  | position | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
  |---|---|---|---|---|---|---|---|---|
  | OH t-20 / t-40 ms | 137 / 268 | 139 / 282 | 185 / 350 | 208 / 399 | 214 / 427 | 227 / 455 | 243 / 489 | 266 / 526 |
  | OH tail tau ms | 63 | 64 | 79 | 91 | 96 | 101 | 107 | 119 |
  | CH t-20 / t-40 ms | 25 / 44 | 27 / 49 | 47-51 / 88-92 | 67-69 / 113-118 | 87-88 / 134-137 | 96-97 / 144-145 | 107 / 152 | 122 / 160 |
  | CH tail tau ms | 8.8 | 9.8 | 19-20 | 23-24 | 26 | 26.5 | 26 | 24 |

  Its OH spans what AR's OH DECAY sweep does (t-20 137 -> 278, t-40 265 -> 520 ms), within 5 %
  at both ends. Its CH ends at t-20 122 ms with OH DECAY at maximum, where AR's CH grid (OH
  DECAY unknown) ends at 93 ms and AR's CH accent takes at 120 ms: the CH's length at full CH
  DECAY is set largely by the OH DECAY knob, so render AR's CH grid with OH DECAY as a fitted
  unknown. The accented CH (step 7) is about 7 dB above the plain one (step 11), as AR's accent
  range.

## Crash and ride

The sample clock against TUNE (Hz):

| TUNE | 0 | 0.1 | 0.2 | 0.3 | 0.4 | 0.5 | 0.6 | 0.7 | 0.8 | 0.9 | 1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| CR AR | 28 056 | 28 378 | 30 146 | 32 372 | 34 286 | 36 678 | 38 978 | 42 075 | 46 624 | 51 944 | 53 839 |
| RD AR | 28 066 | 28 583 | 30 086 | 32 398 | 35 000 | 36 825 | 39 814 | 43 828 | 48 188 | 54 435 | 56 105 |

| | dial 0 | 2 | 4 | 6 | 8 | 10 |
|---|---|---|---|---|---|---|
| CR RR | 27 616 | 29 561 | 33 660 | 41 299 | 48 479 | 55 769 |
| RD RR | 26 750 | 28 595 | 38 637 | 38 637 | 46 764 | 55 479 |

| | TUNE 0 | 0.25 | 0.5 | 0.75 | 1 |
|---|---|---|---|---|---|
| CR Roland | 26 661 | 30 376 | 35 267 | 41 949 | 51 882 |
| RD Roland | 26 661 | 30 376 | 35 274 | 42 977 | 55 169 |

- **TUNE law: the clock's period is linear in rotation between dead ends** (the 10 k(B) pot in
  the astable's timing resistor): T = 35.64 µs at k <= 0.073 falling linearly to 18.57 µs at
  k >= 0.949 (crash) and 35.63 -> 17.82 µs between 0.075 and 0.940 (ride), 14 cents rms. As
  frequency that is an accelerating curve: about 90-135 cents per 0.1 low in the turn, 165-210
  near the top, 20-60 in the dead ends. Roland's period is linear over the whole turn (37.5 ->
  18.1-19.3 µs, no dead ends).
- RR's RIDED4 and RIDED6 are the same file (one setting is missing). RR's clocks lock at NCC
  0.44-0.62 only (sampler-filtered, 44.1 kHz) but agree within 0.1 % with fork B's lag-tracked
  values on the upsampled files (research.md B.4, NCC 0.95-0.98).
- The decay follows the clock (the envelope steps with the ROM address): CR t-20 858 -> 457 ms and
  t-40 1093 -> 572 ms over TUNE 0 -> 1; RD t-20 707 -> 410, t-40 1098 -> 559 ms. At TUNE 0.5
  Roland's CR t-20/t-40 are 720/880 ms (AR 657/835), RD 616/893 (AR 571/838).
- Regebro's cymbals (TUNE unknown) run at 34.0 kHz (CR) and 34.5 kHz (RD).

## Accent

AR's "accentNNN" takes, rms over 0-50 ms relative to accent 100, dB; "grid" is the plain grid
take at the same knobs:

| voice | 0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 | 100 | grid |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| BD | -6.7 | -6.7 | -5.5 | **-7.2** | -4.1 | -3.2 | -2.1 | -1.6 | -0.8 | 0.0 | 0 | -6.5 |
| SD | -6.1 | -6.3 | -4.9 | -4.0 | -3.7 | -2.9 | -1.8 | -1.0 | -0.3 | 0.0 | 0 | -7.0 |
| LT | -7.1 | -6.7 | -5.5 | -4.4 | -4.2 | -3.1 | -2.4 | -1.5 | -0.8 | +0.2 | 0 | -6.7 |
| MT | -7.1 | -6.8 | -5.4 | **-7.3** | -4.2 | -3.1 | -2.1 | -1.6 | -0.6 | 0.0 | 0 | -6.7 |
| HT | -6.8 | -6.7 | -5.4 | -4.4 | -3.9 | -3.0 | -2.4 | -1.5 | -0.4 | +0.2 | 0 | -6.6 |
| RS | -4.3 | -4.3 | -3.4 | **-4.6** | -2.5 | -1.9 | -1.5 | -0.9 | -0.4 | 0.0 | 0 | |
| HC | -5.0 | -4.9 | -4.0 | **-6.0** | -3.5 | -2.9 | -2.4 | -1.4 | -0.6 | +0.1 | 0 | |
| CH | -7.1 | -7.1 | -6.1 | -5.6 | -4.6 | -3.6 | -2.9 | -1.9 | -1.0 | 0.0 | 0 | -8.2 |
| OH | -7.3 | -7.3 | -6.3 | -5.8 | -4.8 | -3.9 | -2.4 | -1.9 | -1.0 | 0.0 | 0 | -7.4 |
| CR | -7.8 | -7.8 | -6.8 | -6.2 | -5.2 | -4.1 | -3.1 | -2.1 | -1.0 | 0.0 | 0 | -6.6 |
| RD | -7.7 | -7.7 | -6.6 | -6.1 | -5.1 | -4.1 | -3.1 | -2.0 | -1.0 | 0.0 | 0 | -7.9 |

**What the takes are.** They are a TOTAL ACCENT sweep on an accented step, read through the
firmware's accent tables (research.md C.4: code = table[(base + TA) / 2] / 2, TA = the knob's ADC
value / 8):

- The 30 % take dips below the 0 % one on BD, MT, RS and HC, by 0.3-1 dB. Only an accented
  analog step (base 50h, table 1F80) reaches entry 43, the table's one non-monotonic entry (28h
  after 30h: code 20 below the 21 at TA 0), at TA 6-7; CH (table 1FC0) and OH/CR/RD (1FC0, base
  40h) never dip, and SD, LT, HT's 30 % takes read as TA 8-11 (the knob was set by hand for each
  voice). A normal step (base 32h) can't reach entry 43 at all.
- 0 % = 10 % and 90 % = 100 %: dead ends on the TOTAL ACCENT pot, as on the TUNE pots.
- Reading the BD's levels as amplitude proportional to the code (code 21 at 0 %) gives TA = 0-1,
  4-5, 6-7, 10-11, 14-15, 18-19, 20-21, 24-25, 28-31, 28-31 at 10 ... 100 %: TA rises about
  linearly, 0.33 per percent, between the dead ends.
- So the takes run from code 21 to 47 (analog voices, RS, HC), 40 to 55 (CH) and 32 to 47 (OH,
  CR, RD); the plain grid sits within 1.2 dB of the 0 % take, i.e. at code 21 (CH 40). The range is
  6-7 dB on the analog drums and 7-8 dB on the sample voices, 4-5 dB on RS and HC.
- Accent changes shape as well as level: the BD's t-40 528 against 485 ms (accent 1 / 0), the
  RS tail and the HC's t-20 longer, the SD's drum part rising more than its noise (centroid 2117 ->
  1136 Hz); CH and LT decays stay put.

Regebro's four takes, rms 0-50 ms relative to take 1 (individual outs; the inferred order is
take 2 TOTAL ACCENT only, 3 the instrument's accent only, 4 both):

| | BD | SD | LT | MT | HT | RS | HC | CH | OH | CR | RD |
|---|---|---|---|---|---|---|---|---|---|---|---|
| take 2 | +3.1 | +3.0 | +3.0 | +2.7 | +3.6 | +2.4 | +2.6 | +5.6 | +4.1 | -4.3 | +4.1 |
| take 3 | +7.7 | +7.3 | +7.9 | +7.3 | +7.6 | 0.0 | -0.3 | +9.1 | +0.1 | -0.4 | +0.2 |
| take 4 | +10.4 | +9.8 | +10.4 | +10.0 | +11.3 | +2.4 | +2.5 | +13.3 | +4.2 | -5.0 | +4.2 |

- The firmware explains this with TOTAL ACCENT near its middle (TA about 16): analog codes 10
  (none), 14 (TOTAL only), 21 (local only), 33 (both), i.e. +2.9, +6.4, +10.4 dB for a level
  proportional to the code, against BD's +3.1, +7.7, +10.4; OH/RD codes 32 -> 40 give +4.1 dB at
  research.md's 6.4 dB/V. RS, HC, OH, CR, RD have no instrument accent (take 3 = take 1). The
  order of the takes is consistent with this but still inferred.
- The CR's individual-out takes 2 and 4 are 4-5 dB *lower* (its main-out takes are +4.3/+4.4):
  an anomaly of that recording.

Roland's plugin ignores TOTAL ACCENT for MIDI notes and maps velocity continuously (vel 127 over
64: +12 dB BD, +16 dB SD, +10.7 dB on the sample voices; `measurements.md` there).

## Levels

AR, LEVEL 50 %, one interface gain (grid takes; dBFS):

| | BD | SD | LT | MT | HT | RS | HC | CH | OH | CR | RD |
|---|---|---|---|---|---|---|---|---|---|---|---|
| knobs | 0.5 each | 0.5 each | 0.5 | 0.5 | 0.5 | | | DECAY 0.5 | DECAY 0.5 (44.1 k) | TUNE 0.5 | TUNE 0.5 |
| peak | -10.8 | -7.9 | -11.7 | -10.5 | -10.3 | -10.6 | -2.6 | -9.9 | -14.8 | -16.9 | -21.7 |
| rms 0-50 ms | -16.9 | -19.4 | -18.4 | -17.9 | -17.9 | -22.3 | -22.4 | -25.1 | -28.4 | -30.0 | -34.4 |

The RS and HC "shot" takes have no accent value; research.md A2.2 finds their level equal to the
0 % accent take, so they too are at code 21.

## How the units differ

| quantity | AR | RR | Regebro | KB6 | Roland |
|---|---|---|---|---|---|
| BD end pitch, Hz | 49.2-49.5 | 49.3-49.5 | 52.7 | 52.3 (47-61) | 52.6 |
| BD tau at TUNE 0 / 1, ms | 7.6 / 29.1 | 8.2 / 29.3 | | | 7.2 / 29.4 |
| SD f_low at TUNE 0 / 1, Hz | 116.3 / 230.7 | 131.2 / 234.3 | | | 112.4 / 225.4 |
| SD VCO ratio | 1.50 | 1.43-1.45 | 1.39 | 1.50 | 1.46-1.47 |
| tom VCO ratio LT / MT / HT | 1.47 / 1.48 / 1.48 | 1.50 / 1.66 / 1.45 | 1.52 / 1.50 / 1.44 | 1.52 / 1.45 / 1.45 | 1.47 / 1.55 / 1.59 |
| tom f_main at TUNE 0 and 1, re AR | | +62..+92 / +14..+58 cents | | | LT -112/-120, HT +210/+173 |
| RS lines, Hz | 224, 478, 1013 | 224, 452 | 220, 500, 1022 | 216, 484, 1042 | 213, 469, 985 |
| hi-hat clock, kHz | 31.57 | 31.57 | 30.59 (scherer 31.19) | 31.40 | 32.85 |
| CR / RD clock at TUNE 0, kHz | 28.06 / 28.07 | 27.62 / 26.75 | | | 26.66 / 26.66 |
| CR / RD clock at TUNE 1, kHz | 53.84 / 56.11 | 55.77 / 55.48 | | | 51.88 / 55.17 |

- Pitches that come from one RC (BD end, VCO ratios, the hat clock) spread 3-12 % between units;
  the knob laws' shapes agree.
- AR and RR differ most at the bottom of each TUNE range (SD +13 %, toms +4-5 % on RR), which
  is where the TUNE ladders' fixed resistors set the pitch.
- Roland's numbers (the last column, ratios from its toms' lines) are the plugin's own unit, if it
  was modelled on one; its BD end pitch matches Regebro's and KB6's units, not AR's.

## Knob laws the data suggest

| knob | law | evidence |
|---|---|---|
| BD TUNE (VR2 100 k(A)) | expanding: VR2 = 2.8 / 10 / 18 / 29 / 99 k at 0 / .25 / .5 / .75 / 1 from tau = C9 ((R57 + VR2) ‖ R26) | AR, RR |
| SD TUNE (VR6 10 k(B)) | linear, x1.98 over the turn | AR 5 points |
| tom TUNE (VR10/13/16 10 k(B)) | linear between dead ends at about 0.08 and 0.92 | AR 11 points, 0.7-1.2 Hz rms |
| CR / RD TUNE | clock period linear between dead ends at about 0.074 and 0.94-0.95 | AR 11 points, 14 cents rms |
| TOTAL ACCENT (to the CPU's ADC) | TA linear between dead ends (flat over 0-0.1 and 0.9-1) | AR accent takes through the firmware tables |
| tom DECAY | t-40 x2.2-2.4 over the turn, flat above 0.8-0.9 | AR 11 points |
| BD DECAY | t-20 105 -> 288 ms, t-40 154 -> 487 ms | AR 5 points |
| CH / OH DECAY | CH t-20 25 -> 93 ms (122 with OH DECAY full), its tail tau 8.6 -> 26 ms and flat above 0.25; OH t-20 137 -> 278 ms | AR, RR, scherer |

The dead ends recur on every pot measured finely (toms, cymbals, accent): in this unit about 8 %
of each end of the turn does nothing. Whether that is the pots (carbon tracks shorter than the
rotation) or how AudioRealism set "0 %" and "100 %" can't be told from one unit, and RR's four
marks can't tell either.

## Open questions

- AR's accent state is inferred from the firmware's table quirk; confirm by rendering the grid at
  code 21 and the accent takes at codes 21-47 (if the model's levels then match AR's sweep within
  0.5 dB, the reading holds).
- The order of Regebro's takes is inferred (it fits the firmware at TA about 16).
- Regebro's hat clock (30.6 kHz) against AR's and RR's 31.57 and scherer's 31.19: unit spread or
  a speed error in his chain? Its BD end pitch (52.7 Hz) would then be 2-3 % off too.
- AR's OH DECAY during the CH takes, and CH DECAY during the OH takes, aren't stated.
- RR's level and accent: normalised, and the RS taken at MIDI velocity 63 and 127 suggests the
  set was played over MIDI (velocity 127 = code 63); its other clips' velocity is unknown.
- The BD's slow excursion and offset after the oscillation stops (t-60 at every DECAY) is
  measured, not explained.
- The dead ends on the pots: a property of the 909's pots, or of AR's knob setting?
- A second unit on a fine knob grid would settle the knob laws and their spread. None is free;
  the paid sets with documented settings (Edgar Maguyon's every-setting recordings, Samples From
  Mars, Sounds Outside the Lines, Zenhiser) are listed in `private/reference/real909/index.md`,
  "Added 2026-10-17".

## Recording chains

The model's output is the MULTI OUT jack, which is DC-coupled (the output op-amp, the jack board's
1k, the jack). AudioRealism's clips are high-passed at about 6.5 Hz, first order: the snare's drum
starts on a negative step (its VCAs' base current) that the model brings back to 0 smoothly by
100 ms, while AR's overshoots to +0.08 of the peak at 40-60 ms and decays by 150 ms; a first-order
high-pass on the model fits AR's waveform below 100 Hz best at 6-7 Hz over six settings (1.2 % of
the peak). It moves every decay measured on AR (the snare's t-20 66 -> 80 ms against AR's 85) and
the energy below 150 Hz. `real909.chain(x, sr, 'ar')` applies it to a model's render before the
comparison. Rob Roy's clips are normalised and trimmed by his sampler, and every one reaches both
+1.0 and -1.0, so their waveform's symmetry tells nothing; the other sources' chains aren't known.
