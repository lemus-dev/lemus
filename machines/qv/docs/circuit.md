# The analog circuit

The QuadraVerb's analog stages as the model has them: the service manual's sheet 1 (rev F boards:
inputs, the A/D's front end, the dry VCA, the outputs, the signal display) with the data sheets in
`private/qv/docs/datasheets` (CEM3381, PCM54, TL084, LM311, CD4053B). Where the code is:
`core/analog.h` (each linear stage's design from its parts), `core/unit.*` (the stages joined, the
rates, the sample-and-holds, the dry VCA's control, the noise), `core/board.*` (the A/D's offset
servo, the level detector's and the key piezo's timing).

## Rates and levels

The host's samples are volts at the jacks, 0 dBFS = +4 dBu (1.736 V, a sine's peak), both ways. The
analog stages run at 4 times the DSP's rate (166,667 Hz): host to that rate by a windowed-sinc
resampler, the input stages, then a 241-tap Kaiser low-pass decimating by 4 to the DSP's 41,666.67
Hz for the A/D (-6 dB at 20.8 kHz, 90 dB down from 22.9 kHz); after the D/A, the sample-and-holds'
value held through each DSP sample at 4 times the rate (the hold's sinc droop comes with it), the
stages after it, and down to the host's rate. At 4 times, the stages whose corners sit near or above
the DSP's Nyquist frequency (the pre-emphasis's pole at 24 kHz, the reconstruction filters' 25 kHz,
the summers' 53 kHz) keep their response in the audio band: at the DSP's rate the reconstruction
filter came out 3.8 dB and 50 degrees off at 18 kHz.

The dry signal joins the summers delayed by the decimator's delay (120 steps, 30 DSP samples), so
dry and wet line up as the converters' own delay leaves them; the host is told the whole latency.

## The stages

| Block | Parts (sheet 1) | Model |
|---|---|---|
| Inputs | J13, J14 (R (MONO) on the rear: the left takes the right's signal with no plug in it), R2/R5 1K, R3/R6 1M, U1's followers | Unity; mono in feeds both sides (the plugin's layouts) |
| INPUT stages | C1/C2 4.7u, R1/R4 1K, the INPUT pot (10K B dual) between R1/R4 and U1's output, its wiper at U1's - input | -R_b s C / (1 + s (1K + R_a) C), R_a = 10K (1 - t) the track from the input end to the wiper (the gain rises clockwise): 0 to +20 dB; unity at t = 0.55; the coupling corner from 34 Hz (top) to 5.6 Hz (mid). Clipping at the rails (10.5 V) |
| Mono sum | R17, R18 470R into U4's + inputs | (L + R) / 2 |
| Pre-emphasis | U4 pins 1-3, R23 75K, R24 20K, C43 330p | (1 + s 95K C) / (1 + s 20K C): +13.5 dB above 24 kHz, a zero at 5.1 kHz |
| A/D's input | R25 5.6K, the 20 kHz module, R26 8.2K, R47 51K and R36 510K across it | The module's passband as a doubly terminated ladder's (the terminations' ratio, 0.556); its response the decimator's (no data) |
| A/D | U9 (4053) and C38 1000p, U3's buffer, U8 (LM311), the PCM54 and the DASP's SAR | 16 bits of +-3 V, two's complement; the offset servo from LSTMSB (R48, R47 51K, C13 4.7u, R36 510K to -12 V), as the GT's |
| Level detector | U4 pins 5-7, D2, R27 330K, C45 0.1u | The mono sum's positive peaks, decaying with 33 ms |
| Signal display | U7, R38 91K (the parts list; 92K on the drawing), R39 180K, R40 390K, R41 750K from U24's Q (5 V) | -18, -12, -6, CLIP at 0.32, 0.96, 2.34, 5 V of envelope; the DSP's overflow lights all four for 0.15 s (U24, R55 2.2M, C73 0.1u) |
| D/A, sample-and-holds | U10 (PCM54, +-3 V), R42 750, U9, C39/C40 2200p | Charged through R42 and the switch's 125 ohm for a 32-clock window, then held: a first-order lag a DSP sample (the de-emphasis, service manual 7.10), then the hold |
| Reconstruction | U3's buffers, U4's Sallen-Key: R30/R31, R29/R28 1.1K, C50/C46 .01u, C49/C48 3300p | 1 / (1 + s 2R C2 + s^2 R^2 C1 C2): 25 kHz, Q 0.87 |
| Dry VCA | C9/C10 4.7u, R32/R33 20K into the CEM3381 (U5), its current outputs into U2's summers | The current through the series R-C, times the CEM3381's gain: its data sheet's (PA381: 20 %/V from 0.1 V, -96 dB under it, typical unity at 5 V) times 0.86, the measured unit's (`improvements.md`, accuracy round 1) |
| VCA C.V. | latch 0600h Q1-Q4 through 1.5K, 3K, 6.2K, 12K, R75 10K; Q0 through R77 20K and D8 with 4.7K; C52 0.1u | Solved per code (Newton, the diode by its law), C52's time constant from the source resistance |
| Summers | U2 pins 1-3, 5-7: R34/R45 20K (wet), R7/R8 20K with C26/C27 150p | -(wet + 20K x the VCA's current) / (1 + s 20K 150p), clipping at the rails |
| OUTPUT, output stages | C4/C5 4.7u into the OUTPUT pot (10K A dual), U2's non-inverting stages: R10/R13 2K, R11/R12 2K with C8/C6 4.7u; R9/R14 470R | A 3.4 Hz high-pass, the pot's share (audio taper, 15 % at mid), 1 + 2K / (2K + 1/sC) (a gain of 2), clipping at the rails; J11, J12 not normalled: one plug is the left output |

## Checks

`tools/spice/ac.py`: each linear stage of the model (`qvanalog`, from `core/analog.h` at the unit's
rate) against the same stage as an ngspice 47 deck (ideal op-amps), AC 20 Hz to 20 kHz, 9 cases
(the INPUT stage at three rotations, the pre-emphasis, the reconstruction filter, the summer, the
output, the dry path at two gains). Worst to 10 kHz 0.035 dB and 0.50 degrees (the pre-emphasis's
magnitude, the reconstruction's phase); to 18 kHz 0.13 dB and 0.89 degrees. All pass (2026-11-27).

`qvresponse` (the whole unit at 48 kHz, the hiss off, stepped sines): on flat programs (the graphic
EQ's configuration with every band at 0 dB; the 5-band EQ's with every band at 0 dB) the DSP alone is
flat within 0.01 dB to 20 kHz, so the response is the analog's: -2.1 dB at 20 Hz (the INPUT stage's
coupling at unity gain), flat from 100 Hz to 2 kHz, -1.1 dB at 10 kHz, -2.0 at 16 kHz, -3.3 at 18
kHz, -7.0 at 20 kHz (the hold's droop, the decimator); the dry path -0.15 dB at 10 kHz, -1.1 at 18
kHz (the summers' 150p and the host's resampler; the dry signal skips the decimator, delayed
instead). Levels at 1 kHz with INPUT at unity and OUTPUT at 10: the wet +0.5 dB, the dry at DIRECT
99 +4.5 dB. At INPUT 10 the INPUT stage's coupling takes 7.6 dB off at 20 Hz.

The left output runs a DSP sample behind the right: the programs read it at instruction 0 and the
right at 95 (`dasp24.md`).

## Noise

Always on in the plugin (no setting), as the GT's: a source at each op-amp's + input, white at the
TL084's 18 nV/sqrt(Hz) (U1's followers and inverting stages, U4's pre-emphasis, U3's and U4's after
the D/A, U2's summers at a noise gain of 2 and its output stages), the CEM3381's 10 pA/sqrt(Hz)
rising with the square root of its gain, and the A/D's offset servo. Off only in the tools'
comparisons (`Unit::setHiss(false)`).

## Not modelled

- The 20 kHz module's own response (the decimator's instead): no values are published for it (the
  GT's open question too).
- The sample-and-holds' window: not on the sheet; 32 clocks (the GT's choice, where the held step
  undoes the pre-emphasis), so the hold's own droop is left as it is (-2.9 dB at 18 kHz).
- The op-amps' 1/f noise, offsets and slew rate; the VCA's distortion and control feedthrough.
- The D/A's images above the DSP's Nyquist frequency past what the reconstruction filter leaves of
  them: the host's resampler takes out what is above its own band.
