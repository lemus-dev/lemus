# Power supply and power-on

The power supply board, its reset circuit, and everything that happens at power-on and power-off,
for the board model's cold start. No netlist (the rails are constants in every netlist; the mute
is in mix.cir). Sources as `docs/circuit.md` §0: **SN** p.12 (crop
`private/docs/schematic/crops/p12-power-supply-board-schematic.png`), p.6 ("RESET"), p.7 (change
information), p.11 (the mute, the noise generator's start-up, the latches' reset), p.8 (the switch
board's reset and A/D); **DS** `private/docs/datasheets/` (NEC RD-JB zeners); **RES**
`docs/research.md` A2.6, C.4.

## 1. Rails

Power supply board PS 73133071-76 (one per mains version, PCB 2291084703), transformer 245-364U:

| Rail | From | Reservoir | Regulator | Out | Feeds |
|---|---|---|---|---|---|
| +15 V | secondary S1 "DC20V×2 0.2A", bridge D703 1B4B41 | C702 2200µ/25 | IC701 7815 (µA7815) | C705 10µ/16 | voicing board (CN to the PS: "+15V, GND, −15V") |
| −15 V | the same winding's other half | C703 2200µ/25 | IC702 7915 (µA7915) | C706 10µ/16 | voicing board |
| +5 V | secondary S2 "DC9.5V 0.5A", bridge D702 1B4B41 | C704 2200µ/16 | IC703 7805 (µA7805UC) | C707 10µ/16 | switch board (CPU, RAM, logic); the voicing board's latches and sample counters through CN1 pin 8 |

The voicing board decouples the rails locally (C109/C110, C140/C141 … 10µ/16 near the op-amps);
the netlists take them as ideal ±15 V and +5 V. The 4006 / 4070 noise generator runs on +15 V; the
AN6912 between ground and −15 V; the 4174 latches, 4040/4520 counters and the ROMs on +5 V (RES A2.6).

## 2. RESET (Q701, Q702, D701)

- **D701** (zener, **RD5.6JB2** from SN 393000, RD6.8JB2 before; parts list p.3 has both) from IC703's
  input (the unregulated ~9.5 V) to the node with **R701 10K** to ground and **R702 10K** to Q701's
  base. **Q701**: emitter grounded, collector to +5 V through **R704 10K** and to Q702's base through
  **R703 10K**. **Q702**: emitter grounded, collector the **RESET** line, with **R705 1K** to +5 V
  and **C701 4.7µ/25** to ground. RESET goes to the switch board (F-connector "RESET to SWITCH
  BOARD").
- While IC703's input is low, D701 doesn't conduct, Q701 is off, R704 turns Q702 on and RESET is held
  low. Once the input passes the zener voltage plus Q701's V_BE, Q701 conducts, Q702 lets go, and
  RESET rises through R705 into C701: τ = 4.7 ms, so the CPU leaves reset about 5-8 ms after the
  threshold. SN p.6: "When the voltage on input terminal of IC703 (Power Supply Board) reaches 7.0V,
  Q701 conducts and cuts off Q702. The circuit also provides power down reset when the IC703 input
  voltage goes sufficiently below 7.0V on power down or power fail."
- Threshold from the parts: RD5.6JB 5.3-6.0 V at 5 mA (DS NEC 1981; no sheet lists the JB2 sub-rank;
  Zzk ≤ 200 Ω at 0.5 mA, so at the ~70 µA it carries here it sits a few hundred mV lower) plus
  0.6 V: about 6-6.5 V, the SN's 7.0 V in round figures. With the RD6.8JB (6.4-7.2 V) it was about
  7.5 V, close to the ~9.5 V the 7805 sees at a low mains: the reason for the change ("When the unit
  is operated from a poor AC line and is forced to stop or reset … replace D701 … with RD5.6JB2",
  SN p.6; p.7 table).
- **What RESET does** (SN p.6): it resets IC604 (the µPD7811) and IC608 (RAM), the cartridge board,
  and "TRIG and ACCENT latches (IC1—IC10) on VOICING Board via Switch Board" (CN1 pin 7 to each 4174's
  clear): every trigger line low and every accent DAC at code 0 (0 V) until the firmware writes them.
- The firmware's first writes after reset (RES C.4): 4007 = 0, 4008 = 0, 400A = 3Ch, which raises the
  HH (closed), crash and ride trigger lines for one write, probably to run the sample counters to
  their stop state.

## 3. What power-on does, in order

| Time after the switch | What | Where |
|---|---|---|
| 0-20 ms | the reservoirs charge, the regulators come into regulation | PS |
| until IC703's input passes ~7 V, + 5-8 ms | CPU, RAM, latches in reset: triggers low, DACs 0 V | §2 |
| 0-37 ms | the noise generator's output held high (IC31d, D48), then the register runs from all ones | `noise.md` |
| from 0 | every voice's capacitors charge from 0 V to their operating points (bias networks, coupling capacitors): the voices' own settling, at the MULTI OUTs and into the mix | the voice netlists |
| 0-1.8 s | **master outputs muted** by Q82/Q83 (C143 discharged); −9 dB at 1.8 s, −2 dB at 2.0 s, within 0.5 dB from 2.5 s | `mix.md` §5 |
| 2-30 s | the master outputs' negative half-waves held above C143's negative end − 1.2 V (a DC restorer with C152/C155): from about 5 s for ±2 V, after ~30 s for ±10 V | `mix.md` §5 |

The MULTI OUTs are not muted: whatever the voices do while they settle comes straight out of them.

## 4. Power-off

- The +15 V rail falls: C142 (charged through D193) holds Q79's emitter up while R468 pulls its base
  down with the rail, so Q79 conducts into R469/R470 and saturates Q82/Q83: the master outputs are
  muted while the rails go (`mix.md` §5).
- IC703's input falls below ~7 V: RESET low, the latches clear.
- D195 / R471 discharge C143 and D49 discharges C46 as the rails go, so the mute and the noise
  generator's start-up work again at the next power-on.

## 5. For the board model

Done (2026-10-22, `VoiceBoard::powerOn`; since 2026-11-02 only when the rear's POWER switch goes on.
A new instance, a session load and MEMORY are a unit that has been on a while: the board at its
operating points (`VoiceBoard::reset`), the CPU from RESET run past its boot at once with its latch
writes dropped, `TR909Processor::warmStart`):
the latches cleared, the noise generator's `powerOn()`, mix.cir solved with `pon` at −15 V and
stepped to +15 V. A BD hit comes out −49 / −24 / −3.0 / −0.8 / 0.0 dB at 1.0 / 1.5 / 1.8 / 2.0 /
2.5 s (the table above: −9 dB at 1.8 s, −2 dB at 2.0 s; C143's network in the netlist lets go about
0.15 s early).

**The voices from 0 V** (`tr909circuitrun --cold`: every capacitor at 0 V, the rails up at once; whole
netlists at 96 kHz, inputs at rest, the noise at its 7.5 V mean). The analog voices' outputs move by
microvolts (BD 0.2 µV, SD 1 µV; the clap one 80 mV sample) and the jack board's MULTI OUTs by under a
nanovolt; a hit at 1 / 3 / 10 / 30 s against one from the operating point: BD, SD, RS the same; the
clap 0.44 / 0.26 / 0.03 / 0.03 dB; the toms 0.04-2.2 dB at every time (their VCOs' phase, not
settling). The BD's and SD's capacitors still move at 60 s, which would keep them awake. The
hi-hat's, crash's and ride's analog stages do thump: 1.7 V at the hi-hat's jack for 2 ms, 1.8 V at
the crash's and ride's, settling over 1.6 s. So `powerOn()` starts those three from 0 V (their
MULTI OUTs: about −12 dBFS, under −70 dBFS by 2.5 s; the muted master −72 dBFS) and the others at
their operating points. The real rails ramp over ~20 ms, which would soften the thumps a little.

The original plan:

A cold start: every capacitor at 0 V would be the literal state, but the voices' settling has to be
run through their netlists (tens of seconds of large time constants in some). A practical sequence:
DC-solve every voice (its settled state), start the noise generator with `powerOn()`, solve mix.cir's
DC point with `pon` at −15 V and step it to +15 V (the mute's timing then comes out of the netlist),
and release the CPU's reset after ~10-30 ms with the latches cleared. The voices' own power-on
transients (the coupling capacitors charging) would need the voices started from 0 V instead of
their DC points; they are only heard at the MULTI OUTs, the master outputs being muted for the first
1.8 s.
