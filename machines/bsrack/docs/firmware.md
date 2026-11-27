# Bass Station Rack: firmware v1.5 and the board it drives

This is a reference for the firmware and the digital board around the CPU, as Lemus runs them
(`core/board.h`, `core/mn1880/`). All of it is read from the v1.5 program ROM and from running it.
Addresses are ROM addresses (program space) unless they are written `(x'NN')` (a data-space
direct address: an on-chip register below 0x60, RAM above). The tools:

- `bsrackdis <rom> -t 352F:17` lists the program, following code from the vectors and the pot
  handlers.
- `bsrackseq <rom> [seconds] [options]` runs it headless with scripted inputs and logs what it does.
- `tools/port_mn1880.py` builds `core/mn1880/` from MAME's MN1880 core.

Marks:
- **[inferred]**: shown only by how v1.5 uses the hardware.
- **[open]**: not settled; see the last section.
- Everything else was measured by running the firmware, or read directly from the code.

## CPU and clock

- **Part:** a Panasonic MN1880-family microcontroller with a custom marking. MAME's
  cycle-by-cycle MN1880 core runs it.
  - The family has two CPUs sharing one execution core. v1.5 stops the second CPU with its first
    instruction (`(x'16')` = 02 at 3580); the second CPU's reset vector is a branch to itself.
- **Peripherals:** the on-chip register layout matches MAME's MN1880023 (the Drum Station's CPU)
  wherever v1.5 uses the same registers.
- **Timing:** one CPU cycle is 4 oscillator clocks, as MAME's model has it.
- **Clock: 8 MHz.** MAME guesses the same. Four facts agree:
  - The UART runs in mode `(x'19')` = 62, MAME's 8N1 at oscillator/256. That is MIDI's
    31.25 kbaud only at 8 MHz.
  - Timer 3 reloads FF06: 250 counts at oscillator/16. At 8 MHz that is an exact 2.000 kHz tick,
    and v1.5 divides it by 8 into a 250 Hz main loop.
  - The DCO period table at 0200 starts with the period, in 500 kHz counts, of MIDI note 0
    (8.176 Hz). 500 kHz is 8 MHz / 16.
  - With that, MIDI note 60 plays 261.64 Hz on oscillator 2: middle C.
  - Still open: a check against the factory demo recording's pitch.

## Memory map

| Space | Range | What |
|---|---|---|
| program | 0000-7FFF | the 27C256 |
| program | 0000-003F | vectors, big-endian words (below) |
| program | 0040-005A | interrupt stubs (`reti`), CPU 1's park loop at 0041 |
| program | 0200 | DCO period table: one octave in 384 steps of 2^(-1/384) |
| program | 0500, 0600, 0700 | the mix pot's osc 1 and osc 2 level curves; the resonance level compensation |
| program | 0800 | bipolar pot curve (dead band around the centre) |
| program | 0C00-114F | the 40 factory programs, 34 bytes each |
| program | 1300, 13B4 | 7-segment tables for the display (tens, units) |
| program | 146C | keypad code → key table |
| program | 1500 | CV IN → note table (CV/Gate converter) |
| program | 352F | pot handler jump table (17 big-endian words) |
| data | 0000-005F | on-chip registers |
| data | 0060-03CF | RAM; v1.5 clears it all at boot (35C1). The stack starts at 0300 |
| data | 8000-87FF | 2816 EEPROM, 2 KB |
| data | 8800 | panel switches (read) |
| data | A000 | latch: pot mux address (bits 0-3), CV S&H channel (4-6), S&H enable (7) |
| data | A800 | latch: switch rows (bits 0-3), keypad rows (4-7), active low |
| data | B000 | latch: LEDs, active low |
| data | B800 | latch: 7-segment digit |

- **RAM size [inferred]:** MAME's MN1880023 has RAM to 031F. v1.5 clears and uses RAM up to 03CF,
  as MAME's own Bass Station driver maps it, so Lemus extends the RAM there.
- **Address decoding:** the latches decode on 2 KB blocks. Nothing answers at 9800-9FFF.

Some RAM v1.5 uses:

| Where | What |
|---|---|
| 00C0-00E1 | the program being played (the 34-byte format) |
| 00E0, 00E1 | switch states (in the program) |
| 00A0, 00A2 | the timer 0 and timer 1 reload values the tick copies into 0x20/0x22 |
| 0100-011F | MIDI receive ring, indexes (x'A4')/(x'A5') |
| 0190-019F | MIDI transmit ring, indexes (x'A6')/(x'A7') |
| 0187-018B | program number, MIDI RX/TX channels, CV/Gate channel and type |
| 0220-022F | the eight CV words, one per S&H channel |
| 0280-0290 | the pots' last readings (pickup) |

## Vectors and interrupts

The vectors are big-endian words: reset at 0000, a software interrupt (PI) at 0002, then IRQ n at
0004 + 2n. CPU 1's set starts at 0020.

| Vector | Handler | Source | v1.5's use |
|---|---|---|---|
| reset | 3580 | | boot |
| IRQ3 | 77D4 | timer 0 overflow | osc 1: toggles OSCA_SYNC, does the osc 2 sync |
| IRQ4 | 77EF | timer 1 overflow [inferred] | reloads timer 1 after a sync restart |
| IRQ6 | 77F3 | timer 3 (2 kHz) | the tick: one CV slot per tick, the main loop flag every 8th |
| IRQ9 | 5654 | UART receive | MIDI in ring |
| IRQ5, IRQ8 | stubs | ? | enabled, but they only return |
| others | stubs | | |

- **Enables (77C0):**
  - `(x'12')` = 08 enables IRQ3 and clears the interrupt mask.
  - `(x'15')` gets the bits for IRQ5, 6, 8 and 9.
  - IRQ4 is enabled only by the IRQ3 handler, when osc sync is on.
- **Re-enabling:** in MAME's model a taken interrupt clears its own enable bit. The handlers set it
  again before `reti` (77E7, 7812).
- **Main loop:** it waits at 5277 for `(x'F8')` bit 4, which the tick sets every 8th time: a
  250 Hz loop.
  - It scans the pots, switches and keypad, runs the envelopes, LFO and portamento, and computes
  the CVs.
  - The tick handler (7815) works through 8 slots in `(x'B8')`. Each slot sends one CV word to the
  DAC.
  - Slot 3 also reloads timer 1 and slot 4 timer 0, from their RAM copies.

## Timers and the DCOs

**Setup:** v1.5 sets `(x'31')` = F2 (run bits), `(x'33')` = 14 (control), `(x'35')` = 10 (timer 3
periodic). Timer 0 is at 0x20/0x21, timer 1 at 0x22/0x23, timer 2 at 0x24/0x25, timer 3 at
0x26/0x27.

**Timers 0 and 1 in control mode 14 [inferred]:**
- **Counting:**
  - They count up at oscillator/16 and overflow at 0x10000. The period is 0 − (table entry >>
    octave) (5C6B, 5D0E).
  - At each overflow they reload from their register. A write takes effect at the next overflow
    without restarting the timer: v1.5 rewrites both every 4 ms while the oscillators run.
- **Run bits:** timer 0's is `(x'31')` bit 4 (MAME's); timer 1's is bit 5. Setting the run bit
  starts the timer from its register.
- **Interrupts:** timer 0's overflow is IRQ3. Timer 1's is IRQ4.
- **Pins:**
  - **OSCA_SYNC** is port 3 bit 3, toggled in software by the IRQ3 handler (77E2-77EE).
  - **OSCB_SYNC** is timer 1's own output pin. Nothing in software follows timer 1 when sync is off
    (IRQ4 isn't even enabled), yet osc 2 runs.
- **Pitch:**
  - Each line's every edge resets its DCO's ramp: the keyboard schematic's 4070 XOR makes a pulse
    on both edges. So the DCO frequency equals the edge rate.
  - Note 60 on a centred program measures 261.64 Hz on osc 2 and 262.16 Hz on osc 1. Osc 1's
    pitch index sits one table step (1/32 semitone) above osc 2's, and drops back to it for one
    4 ms tick every 68 ms; its CV does the same (7FE/7FF at note 60). **[open: why]**
- **Jitter:** OSCA's edges carry the interrupt latency, about ±50 oscillator clocks (±6 µs) per
  edge at note 60. OSCB's edges are exact.
- **Sync** (switch on, `E1` bit 6):
  - The IRQ3 handler loads FFFE into timer 1, clears and sets its run bit, and enables IRQ4
    (77D5-77E0). The IRQ4 handler writes the real period back (77EF).
  - With the reload semantics above, OSCB gets three edges 24, 56 and 88 clocks after each OSCA
    edge. They are 2 counts apart, until the reload.
  - [inferred: the burst follows from the reload semantics; real silicon could reload once.]
- **The oscillator CVs** (S&H channels 4 and 5): computed alongside the periods from the same
  pitch index and octave (5C1D-5C34, 5CB6-5CCD). They set the ramps' charging current, so the ramp
  height stays level across the range.

## The serial DAC and the S&H channels

**Protocol** (7886-78F5):
1. S&H off: A000 bit 7 low.
2. A pulse on port 1 bit 1.
3. The channel goes into A000 bits 4-6.
4. 12 bits are shifted in, MSB first: data on port 1 bit 0, clocked by port 1 bit 2 rising. The
   bits are the top 12 of the RAM word.
5. S&H on: A000 bit 7 high.

**The pipeline [inferred]:**
- The pulse loads the previously shifted word into the DAC's output. The slot for channel c
  therefore shifts word 0220 + 2(c + 1), and channel c holds word 0220 + 2c.
- v1.5's own indexing is offset by one slot to match, and that is the evidence. Each channel is
  refreshed once per 4 ms (250 Hz).

| Ch | RAM word | Moved by | Net (keyboard schematic) |
|---|---|---|---|
| 0 | 0220 | the last MIDI note: 816 codes per octave | the rear CV OUT [inferred] |
| 1 | 0222 | filter frequency, mod depth, env 2 (code falls as the cutoff rises) | VCF_CV_LIN |
| 2 | 0224 | pulse width with Osc 2 selected | OSCB_PWM_CV |
| 3 | 0226 | pulse width with Osc 1 selected (follows the PWM LFO) | OSCA_PWM_CV |
| 4 | 0228 | note, semitone, detune, osc 2 modulation (code falls as the pitch rises) | OSCB_CV |
| 5 | 022A | note, osc 1 modulation | OSCA_CV |
| 6 | 022C | resonance; the 12/24 switch (code falls as the resonance rises) | RES_CV |
| 7 | 022E | envelope 1 (code falls as the level rises; FFF closed) | VCA_CV_LIN |

- **Envelope shape:** envelopes are linear in code and step at 250 Hz. For example, attack at
  0x90 runs FFF→0FF in about 200 ms.
- **Latency:** a MIDI note-on reaches channel 7 about 7.7 ms after its first byte.
- **The 12/24 switch:**
  - It sets port 3 bit 1 (12 dB = 1).
  - It also moves the RES CV: FC0 → 870 at minimum resonance (6D5A).
  - It also changes the filter-mod code (6DA3, 6E42). That fits the manual: "in the 12dB position
    ... only positive MOD".

## A_VOL and B_VOL: on-chip registers

- **The registers:** the oscillators' levels aren't DAC channels. v1.5 writes them into on-chip
  registers every 4 ms (54A6): `(x'34')` for osc 1 and `(x'32')` for osc 2.
- **Where the values come from** (6B99):
  - The mix pot looks up the level curves at 0500 (osc 1) and 0600 (osc 2).
  - Each is scaled by the curve at 0700, indexed by the inverted resonance, so the levels fall
    as resonance rises (to about a third at full resonance; see "CV transfer").
- **Curve shape:**
  - The osc 1 curve peaks a third of the way round and falls to the right.
  - The osc 2 curve starts there and rises to full.
- [open: what the registers drive: an on-chip PWM or DAC to A_VOL_CV/B_VOL_CV. MAME doesn't model
  them.]
- [open: the external input's level. The manual's MIX legend fades the external input against
  the oscillators, but v1.5 outputs no external level, so it must come from the pot's analog
  side.]

## CV transfer

How v1.5 turns the panel, the note and the modulation into DAC codes, measured with `bsrackseq` and
explained from the code. All codes are 12-bit.

**How it was measured:**
- Everything starts from a neutral program, booted as program 40: the default program, with the
  LFO stopped. Every modulation depth sits at its zero, sustain is full, the envelopes are
  instant, and there is no portamento.
- Note 60 is held, in 24 dB unless stated otherwise.
- Each table below names the `bsrackseq` command that reproduces it.

**How a pot becomes a program value:**
- **Soft takeover:** a pot acts only once its reading moves more than 5 codes away from the last
  one that acted (6987-6999). The sweeps therefore come to each value from far away. A setting
  that "doesn't take" usually didn't move enough.
- **Scaling:** the reading is offset by −5 (clamped at 0), then multiplied by 1 + 6/256 (6A64). So
  ADC 00-FF lands on stored values 00-FF.

### Filter frequency (S&H 1)

**Computation** (61A0-62A8):

value = clamp(F + M, 00, FF); in 24 dB, value = clamp(value + K, 00, FF);
code = (FF − value) × 16.

- Only the high byte of word 0222 is written (0223), so the filter CV has 8-bit resolution: the
  low nibble is always 0.
- **F**, the FREQUENCY value:
  - in 24 dB, the stored pot value;
  - in 12 dB, the handler stores 0x40 + pot × 0x88/256 instead: 40-C7 (6D5E).
- **K**, key tracking (628A-62A0): 24 dB only. 2 × (note − 36), i.e. 32 codes per semitone above
  C2 (note 36); 0 below.
- **M**, the modulation: a signed 16-bit sum centred on 8000, divided by 0x20 (62AB-62D5). Its
  terms:
  - **Env 2** (term `(x'72')`, 60D5-6113): ±depth × env2 / 32.
    - In 24 dB, depth = |curve(byte 25) − 80| / 4, 0-0x20 (6E42). It is zero across the 0800
      curve's dead band (77-89), and its sign is the side of 80.
    - In 12 dB, depth = byte 25 × 12/256, 0-0x0B, positive only (6E56): "only positive MOD".
      v1.5 also adds the resonance step `(x'1CD')` (0-7, 6DE0, 60F9). So at resonance 0 a full
      ENV 2 still adds about 0x10.
    - A full depth with a full envelope moves the value by up to ±255: the whole range.
  - **LFO** (term `(x'79')`, 63A4-64EC): ±E2 × LFO / 32, with E2 = |curve(byte 11) − 80| / 2,
    0-0x40 (6E00). At full depth that is up to ±510: it saturates both ends.
  - **Mod wheel and aftertouch** (`01DD`, `01DF`, 6229-624D): the utility nibbles Fd and AF
    (below). At rest they add nothing.

FREQUENCY sweep (`--cvmap=11:20`; 12 dB: `--switch=slope:12dB --patch=25:00`):

| Pot | 24 dB stored | 24 dB code | 12 dB stored | 12 dB code |
|---|---|---|---|---|
| 00 | 00 | CF0 | 40 | B00 |
| 20 | 20 | AF0 | 51 | 9F0 |
| 40 | 41 | 8E0 | 62 | 8E0 |
| 60 | 62 | 6D0 | 74 | 7C0 |
| 80 | 7D | 520 | 82 | 6E0 |
| A0 | 9E | 310 | 93 | 5D0 |
| C0 | BF | 100 | A5 | 4B0 |
| E0 | E0 | 000 | B7 | 310 |
| FF | FF | 000 | C7 | 210 |

**What this explains:**
- With note 60, 24 dB saturates at 000 (wide open) from F = CF, because K = 0x30. The default
  program 40 stores F = FC, so the filter reads 000 until the pot actually takes. That matches
  80 → 000, C0 → 100, FF → 000 when 80 didn't take.
- Key tracking on its own (FREQUENCY 00, 24 dB): note 36 → FF0, 60 → CF0, 84 → 9F0.

### Resonance (S&H 6) and the osc levels

**Computation** (6D86-6DE5):
- The stored value is clamped: 03 and below → 00, FB and above → FF.
- **24 dB:** RES code = (FF − value) × 16. Linear.
- **12 dB:** v1.5 writes FF − (0x40 + value/2 + ((F − 0x40) × 0x97/256)/2), × 16 (6DA3). So in
  12 dB the resonance CV starts higher and also follows the frequency. That is the FC0 → 870
  shift seen at resonance 03 with F = FC.

**Osc levels:** both are scaled by the curve at 0700, indexed by FF − resonance (6B99). They fall
as resonance rises. Sweep: `--cvmap=13:20`, 12 dB `--switch=slope:12dB --patch=25:00`.

| Pot | Stored | 24 dB code | 12 dB code (F = FC) | (x'34') at MIX 50 |
|---|---|---|---|---|
| 00 | 00 | FF0 | 880 | FE |
| 20 | 20 | DF0 | | D5 |
| 40 | 41 | BE0 | 680 | A9 |
| 60 | 62 | 9D0 | | 7E |
| 80 | 7D | 820 | 4A0 | 77 |
| A0 | 9E | 610 | | 6E |
| C0 | BF | 400 | 290 | 66 |
| E0 | E0 | 1F0 | | 5D |
| FF | FF | 000 | 090 | 55 |

### Pulse width (S&H 3 for osc 1, S&H 2 for osc 2)

**Clamping:** v1.5 writes only the high byte, clamped to 08-E0, so codes run 080-E00 (797E-79EB).
The source switch picks the byte:

- **MAN.** (6AE1): 0x70 + ((stored − 2) mod 256) × 0x70/256.
  - It rises from 700 at stored 02-03 to DE0 at FF.
  - Stored 00 and 01 wrap to DF0/DE0, a quirk at the very left end.
- **ENV 2** (6078-6096): 0x80 + (stored − 0x80) × env2/256. It rests at 800 when env 2 is 0.
- **LFO**: the internal fixed LFO, not the panel's (7A76-7A75).
  - It is a triangle of period 1.024 s: a 16-bit counter stepping 0x0200 every 4 ms, up then
    down.
  - The code goes from 0x80 towards 0x80 ± |stored − 0x80| and back, on one side only (79F9-7A74).
    For example, stored FF gives 800-E00, clamped at the top.
  - It rests at 800.
- **The pot's zero:** with ENV 2 and LFO the pot is a depth around 0x80, and it is zero only at
  exactly 80 (6ABC).
- **[open] Which code is "50%":** the firmware's "square" is 700 in MAN at the left end, and 800
  at rest with ENV 2 or LFO. The manual's "far left = 50%" therefore puts the analog 50% at about
  700-800.

Sweep (`--cvmap=15:20 --switch=pwm:man|env2|lfo`; ENV 2 with its sustain full):

| Pot | Stored | MAN. | ENV 2 |
|---|---|---|---|
| 00 | 00 | DF0 | 080 |
| 20 | 20 | 7D0 | 230 |
| 40 | 41 | 8B0 | 430 |
| 60 | 62 | 9A0 | 640 |
| 80 | 7D | A50 | 7F0 |
| A0 | 9E | B40 | 9D0 |
| C0 | BF | C20 | BE0 |
| E0 | E0 | D10 | DE0 |
| FF | FF | DE0 | E00 |

### Mix: (x'34') and (x'32')

level = curve(mix) × curve0700(FF − resonance) / 256: osc 1 from the curve at 0500, osc 2 from
0600 (6B99).

- **Writing:** v1.5 writes both registers every 4 ms from the main loop (54A6, called at 5488).
- **Nothing reads them back.** No other register is set up for them.
- The only one-time set-ups v1.5 leaves unexplained are timer 2 = FFF8, `(x'3F')` = 00,
  `(x'40')`/`(x'41')` = FFFF and `(x'44')`/`(x'45')` = FFFC (3691-3698, 35D4). Those could be an
  8-bit PWM's time base. **[open]**

Sweep at resonance 03 (`--cvmap=1:10`):

| Mix | (x'34') osc 1 | (x'32') osc 2 | Mix | (x'34') | (x'32') |
|---|---|---|---|---|---|
| 00 | 00 | 00 | 80 | EA | 5A |
| 10 | 49 | 00 | 90 | D9 | 7D |
| 20 | 8C | 00 | A0 | C4 | 9C |
| 30 | C5 | 00 | B0 | A8 | B9 |
| 40 | EA | 00 | C0 | 8C | D0 |
| 50 | FB | 00 | D0 | 6B | E3 |
| 60 | FA | 1D | E0 | 48 | F1 |
| 70 | F3 | 42 | F0 | 22 | F9 |
| | | | FF | 00 | FC |

### Envelope 1: the VCA (S&H 7)

**Code range:** closed is FFF; the attack peak is 01F. Sustain: code = 01F + (FF − S) × 15.93,
where S is the stored sustain. The SUSTAIN pot goes through the curve at 0900 first (6F4B):

| Pot | 00 | 20 | 40 | 60 | 80 | A0 | C0 | E0 | FF |
|---|---|---|---|---|---|---|---|---|---|
| stored | 00 | 68 | 95 | AD | BD | CF | DF | F0 | FF |
| code | FFF | 986 | 6B9 | 53A | 43B | 31C | 21D | 10E | 01F |

**Time law:** attack, decay and release are linear ramps in code, one step per 4 ms tick. The
table gives the time of a full sweep: attack FFF → 01F, decay 01F → FD0 (sustain 03), release
04F → FFF. The first ticks' step sizes give the rate. Commands: `--envmap=14:20`,
`--envmap=10:20 --patch=14:03`, `--envmap=8:20`.

| Pot (stored) | Attack ms | Decay ms | Release ms | Decay step / tick |
|---|---|---|---|---|
| 00 | 4 | 4 | ≈4 | whole |
| 20 | 12 | 12 | 20 | 1499 |
| 40 (41) | 24 | 32 | 40 | 526 |
| 60 (62) | 60 | 88 | 96 | 191 |
| 80 (7D) | 128 | 196 | 204 | 80 |
| A0 (9E) | 344 | 552 | 560 | 32 |
| C0 (BF) | 892 | 1564 | 1568 | 16 |
| E0 | 2416 | 4452 | 4440 | <16 |
| FF | 6068 | 12288 | 12248 | <16 |

- The release column includes about 8 ms of note-off handling. Decay and release follow the same
  law.

**Velocity** (`--envmap=16:40`):
- With the VELOCITY pot at V, the peak = 01F + 0x7D9 × (V/255) × (127 − velocity)/126. The sustain
  code moves by the same amount, so velocity offsets the whole envelope in code (in dB after the
  VCA's exponential converter).
- At V = 00 velocity does nothing. At V = FF, velocity 40 peaks at 40B and velocity 01 at 7F8:
  about half the code range.

**EG bias** (utility byte 29):

VCA code = env + (FF0 − env) × (bias/15) × (1 − pressure/127).

- The high nibble acts with channel aftertouch, the low nibble with breath (CC 2); both measured.
- At bias 15, zero pressure closes the VCA (FF0), and full pressure gives the envelope's own
  level.

### The oscillator CVs (S&H 4, 5) and the pitch

**Computation** (5BE2-5C34 for osc 1, 5C7B-5CCD for osc 2):

word = FFFF − (oct − 1) × 2000 − offset × 10.66.

- `oct` is the octave, `(x'163')`/`(x'164')`. `offset` is the pitch index in table bytes:
  0-2FF, 2 per 1/32 semitone, in `01C8`/`0139`.
- In 12-bit codes: 512 per octave, 42.67 per semitone, 1.33 per table step.
- The DCO period is the period table >> oct, the same index.
- **Note range** (`--keymap=1`):
  - Notes 0-11 are ignored.
  - 12-107 are octaves 1-8 (OSC CV FFF down to 029).
  - 120-127 all play note 120.
- **The wrap:**
  - (oct − 1) × 2000 is computed in 16 bits, so the CV only covers octaves 1-8.
  - Octave 9 (notes 108-119) wraps to FEF-E1A; octave 10 (120) to DEF.
  - There is no range bit: the period keeps shifting, while the ramp-slope CV jumps back to its
    low end. Above note 107 the ramp therefore gets far too little current for its period.
- **Timer resolution:** at high notes the period is only a few hundred counts or fewer. Note 96 is
  238 counts (+6.5 cents); note 120 is 59 counts (+21 cents).
- **Semitone** (6BF6, `--cvmap=2:10`): ⌊stored/0x15⌋ semitones, 0-12, added to osc 2's index at
  42.67 codes each. FF is 12, exactly one octave.
- **Detune** (6C15, the 0800 curve, `--cvmap=6:8`): zero across 77-89, and much wider than the
  manual's ±50 cents (osc 2's CV relative to the centre):

| Pot | 00 | 40 | 70 | 78-88 | 90 | A0 | C0 | FF |
|---|---|---|---|---|---|---|---|---|
| OSCB CV | +AB | +4B | +03 | 0 | −02 | −17 | −4A | −A9 |
| semitones | −4.0 | −1.8 | −0.07 | 0 | +0.05 | +0.54 | +1.7 | +3.96 |

### LFO

**Rate** (`--lfomap`):
- Speed (stored) 03 and below stops it (655A); up to about 10 it is slower than 1/20 Hz.
- The rate then roughly doubles every 0x20. The LFO SPEED LED toggles once per cycle.
- The rate is quantised by the 4 ms main loop.

| Speed | 20 | 30 | 40 | 50 | 60 | 70 | 80 (7D) | 90 | A0 | B0 | C0 | D0 | E0 | F0 | FF |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Hz | 0.18 | 0.38 | 0.63 | 0.94 | 1.41 | 2.07 | 2.69 | 4.03 | 5.95 | 8.93 | 13.2 | 19.2 | 27.8 | 41.7 | 50 |

**Shapes** (E0 bits 0-1), measured on the VCF code:
- **0 R'DOM:** a new random level each cycle, held.
- **1 TRI:** a triangle.
- **2 SAW:** falls, then jumps back up. The VCF code ramps up, then drops.

**Depths:**
- **Into the filter:** ±E2 × LFO / 32 value units (above). Measured with a triangle at speed C0
  around FREQUENCY 80 (`--cvmap=9:10 --switch=modsrc:lfo --patch=7:C0 --patch=9:80`):
  - mod depth 70 swings ±3 value units (±0x30 codes);
  - 60: ±0x22 units;
  - 50: ±0x45 units;
  - 40 and below, or B0 and above: the whole range.
- **Into the oscillators** (osc LFO depth, pot 0, `--cvmap=0:10 --patch=7:C0`): up to ±0xA5 codes
  at FF (±3.9 semitones). Stored 00 is a special case, near zero.
- **Into the pulse width:** the PWM source LFO is the internal triangle (above), not the panel
  LFO.

### The utility nibbles (program bytes 26-29)

| Byte | High nibble | Low nibble |
|---|---|---|
| 26 | Fd: mod wheel → filter | Pd: mod wheel → pitch |
| 27 | AP: aftertouch → pitch, signed, 2 semitones per unit (7: 13.9 up, 1: 2 up, F = −1: 2 down, 9 = −7: 13.9 down) | bP: breath → pitch [presumed] |
| 28 | AF: aftertouch → filter, signed | bF: breath → filter [presumed] |
| 29 | AE: aftertouch EG bias, 0-15 | bE: breath EG bias, 0-15 |

- With the LFO stopped, the mod wheel gave a fixed offset (pitch −0x4E at Pd 15, filter to BD0 at
  Fd 15). It probably scales the LFO's held value. **[inferred]**

## ADC and the pot mux

- **Channel select [inferred]:** the channel is `(x'1E')` bits 0-2. v1.5 writes 70/71/73 there,
  never MAME's `(x'1D')` (34AD, 34C4, 34DB).
- **One conversion:**
  1. `(x'1E')` = 70 + channel starts the conversion.
  2. `(x'5D')` = 40 | channel << 3 | channel, with bit 6 pulsed low then high. [open: what
     `(x'5D')` is]
  3. A fixed delay loop.
  4. Read `(x'1C')`, then clear `(x'14')` bit 6.
- **Channels:**
  - channel 0: 16 pots through a mux addressed by A000 bits 0-3;
  - channel 3: the 17th pot;
  - channel 1: the CV/Gate converter's CV IN.
- **Rates:** v1.5 reads two pots per 4 ms tick in mux order, then channel 3, and channel 1 every
  tick. That gives 29.4 readings per second per pot, and 250 per second for CV IN.
- **Pickup:** a pot acts only once it moves more than 5 codes from its last reading (0280-0290). A
  handler from the table at 352F then stores it and sends its MIDI controller.

| Mux | Pot | Program byte | Controller | Handler |
|---|---|---|---|---|
| 0 | Osc mod LFO depth | 5 (osc 1) / 23 (osc 2) | | 6C64 |
| 1 | Mix | 3 | | 6B96 |
| 2 | Semitone (13 steps) | 2 | | 6BF6 |
| 3 | LFO speed (off at the bottom) | 7 | | 706E |
| 4 | Osc mod envelope depth | 4 / 22 | | 6CE1 |
| 5 | LFO delay | 8 | | 7078 |
| 6 | Detune | 1 | | 6C15 |
| 7 | Portamento (off at the bottom) | 0 | | 6A87 |
| 8 | Env release | 15 (env 1) / 20 (env 2) | 111 / 117 | 700A |
| 9 | Filter mod depth | 11 (LFO) / 25 (env 2) | 107 | 6DE9 |
| 10 | Env decay | 13 / 18 | 109 / 115 | 6FA7 |
| 11 | Filter frequency | 9 | 105 | 6D4E |
| 12 | Env sustain | 14 / 19 | 110 / 116 | 6F4B |
| 13 | Resonance | 10 | 106 | 6D86 |
| 14 | Env attack | 12 / 17 | 108 / 114 | 6ED8 |
| 15 | Pulse width | 6 / 24 | | 6AA4 |
| 16 (ch 3) | Env velocity | 16 / 21 | 112 / 118 | 6E96 |

- **Which byte a dual pot writes:** for pots 0, 4 and 15 the Osc 1/2 switch decides; for the
  envelope pots the Envelope 1/2/1&2 switch. Pot 9's byte follows the filter mod source.
- **Volume** isn't read by the firmware: it is analog, after the VCA.

## Switch matrix (0x8800)

- **Reading:** a row is selected with a low bit in A800 bits 0-3; row r is bit 3 − r. Bits 0-5 of
  0x8800 are read; 0 = contact closed. The read and its bit remap are at 33BF.
- **Change detection:** each scan is compared with the previous one (0140-0147); only a change
  is decoded.
- **Decoding** is at 65A1-6797. Rows 0, 2 and 3 are checked against lists of valid patterns: each
  multi-way switch must have exactly one contact closed, or the row is ignored.
- **Pickup:** like the pots, a switch's position applies only once it moves.

| Switch | Row | Contacts (8800 bits) per position | Stored in |
|---|---|---|---|
| Osc 2 range 16', 8', 4', 2' | 0 | bit 0, 1, 2, 3 | E0 bits 5-6 = 0, 1, 2, 3 |
| Envelope 1, 2, 1&2 | 0 | bit 4, none, bit 5 | (x'FC') bits 2/1 |
| LFO shape R'DOM, TRI, SAW | 3 | bit 3, 4, 5 | E0 bits 0-1 = 0, 1, 2 |
| Osc 1-2 sync off, on | 3 | bit 0 closed = off | E1 bit 6 |
| PWM source ENV 2, MAN., LFO | 2 | bit 0, 1, 2 | E1 bits 2-3 (osc 1) / 0-1 (osc 2) = 2, 1, 0 |
| Triggering AUTOGLIDE, SINGLE, MULTI [inferred: names by panel order] | 2 | bit 3, 4, 5 | E1 bits 4-5 = 2, 1, 0 |
| Write PROTECT, ENABLE | 1 | bit 0 closed = protect | (x'B6') bit 6 |
| Osc 1 waveform saw, pulse | 1 | bit 1 closed = saw | E0 bit 7 = 1 → port 1 bit 7 = 0 |
| Osc 2 waveform saw, pulse | 1 | bit 2 closed = saw | E0 bit 4 = 1 → port 1 bit 6 = 0 |
| Osc mod oscillator 1, 2 | 1 | bit 3 closed = osc 1 | (x'FC') bit 6 |
| Filter mod source LFO, ENV 2 | 1 | bit 4 closed = LFO | E0 bit 3 |
| Cut-off 12dB, 24dB | 1 | bit 5 closed = 12 dB | E0 bit 2 → port 3 bit 1 |

How the positions were confirmed:
- **Measured by running the firmware:** the range (oscillator 2 at 1/2/4 times oscillator 1 for 8', 4', 2'), sync, PWM source, filter mod source, waveforms (the factory "TB303 SQUARE"
  and "EOW" programs agree), 12/24 and the selects.
- **Triggering:** code 2 doesn't retrigger the envelopes on a legato note, and the factory
  "AUTOGLIDE" program has it. Codes 1 and 0 both retriggered in that test **[open]**.

## Keypad

- **Reading:** rows are selected by A800 bits 4-7 (active low); the columns are read on port 9
  bits 2-5 (3405). Key code = row × 4 + column, with port 9 bits 4, 5, 3, 2 as columns 0-3.
- **The code table at 146C:**

| Code | Key | Code | Key | Code | Key | Code | Key |
|---|---|---|---|---|---|---|---|
| 0 | 1 | 4 | 2 | 8 | 3 | 12 | 4 |
| 1 | 5 | 5 | 6 | 9 | 7 | 13 | 8 |
| 2 | 9 | 6 | − | 10 | (0) | 14 | + |
| 3 | 0 | 7 | (0) | 11 | (0) | 15 | MODE |

- Codes 7, 10 and 11 also read as 0. Presumably no keys sit there.
- **AUDITION/DEMO** isn't on the keypad: it is port 2 bit 4, active low (37CD). It is the same pin
  as on MAME's Drum Station.

## Ports

| Port | Bits | Use |
|---|---|---|
| 1 (out) | 0, 1, 2 | DAC data, load, clock |
| 1 (out) | 6, 7 | osc 2 / osc 1 waveform select (1 = pulse) to the 4053 |
| 2 (in) | 4 | AUDITION, active low |
| 2 (in) | 6 | GATE IN (CV/Gate converter; read at boot too, 3611) |
| 3 (out) | 1 | slope, 1 = 12 dB, to the 4053 |
| 3 (out) | 3 | OSCA_SYNC |
| 3 (out) | 5 | digit select: 1 = tens |
| 3 (out) | 7 | low while a note is held (342A) [open: GATE OUT?] |
| 9 (in) | 2-5 | keypad columns |
| 4, 5, 6, 9 | | written once at boot (0F, 0F, FF, 00) |

- Port inputs need an input mask in MAME's model: port 2 is 0x50, port 9 is 0x3C.
- `(x'0F')` is set and cleared around each port's first write at boot (358A-35BC), and `(x'1F')`
  bit 1 around port 9's. They look like a port-direction write select. [open; MAME has no port
  directions]

## Display and LEDs

- **B800:** one 7-segment digit with bits d, dp, e, c, b, a, f, g (active high). v1.5 writes a
  digit, then flips port 3 bit 5 to show it, every 4 ms (3498, 34A4).
  - The tens digit's point is the **Edit** LED (3BC5): it flashes when the sound is edited, faster
    in Compare.
  - The units digit's point flashes while `(x'1B3')` counts down. [open]
- **B000 (active low):**
  - bit 0: LFO SPEED;
  - bits 1-5: the mode LEDs Program, MIDI RX, MIDI TX, Utility, Save. MIDI RX's bit flashes on
    incoming MIDI;
  - bit 6: **Write**, flashing while the Write switch is at Enable (3BA7);
  - bit 7: unused.
- **What a blank EEPROM shows:** the program number FF indexes past the digit tables (180
  entries), so the display reads "58.".

## EEPROM and the program format

**Layout of the 2816:**
- User program p (40-99) is at (p − 40) × 34: 60 programs, 0000-07F7.
- The global settings are at 07F8-07FF:

| Address | Setting | RAM | Saved by |
|---|---|---|---|
| 07F8 bit 0 | poly mode | (x'F8') bit 6 | 72C9 |
| 07F9 | CV/Gate type | 018B | 72B6 |
| 07FA | CV/Gate MIDI channel | 018A | 72A9 |
| 07FB | MIDI TX channel (1-16) | 0189 | 729C |
| 07FC | MIDI RX channel | 0188 | 728F |
| 07FD | program number | 0187 | 7282 |
| 07FE | CV IN scale trim (signed) | (x'8C') | read at 7322 |
| 07FF | CV IN offset trim (signed) | (x'9D') | read at 732C |

- The two trims apply to the CV IN reading (78F6-7971). They look like factory calibration.
- Master tuning isn't stored. That fits the manual's "tunes itself" at power-on.
- v1.5 reads all of these at boot (72D5).

**Writing:** the EEPROM write routine (3551) writes a byte, then reads it back until it matches.
That is data polling; Lemus's write is immediate.

**No init path.** v1.5 never formats a blank EEPROM.
- At boot (3580-36C1) it reads no key or switch except GATE IN, and checks no checksum.
- The only EEPROM writers are:
  - the WRITE key (47CD → 7250);
  - the setters for the global settings (7282-72C9);
  - the sysex receive states that write from offset 0 (74F1, 7522).
- So with a blank EEPROM it loads program FF, reading 34 bytes from the empty 9800 block. A new
  unit's EEPROM must have been loaded at the factory.

**The program format** (34 bytes; RAM 00C0-00E1; factory programs at 0C00 + 34p):

| Byte | Holds | Byte | Holds |
|---|---|---|---|
| 0 | portamento | 17-21 | env 2 attack, decay, sustain, release, velocity |
| 1 | detune | 22 | osc 2 envelope depth |
| 2 | semitone | 23 | osc 2 LFO depth |
| 3 | mix | 24 | osc 2 pulse width |
| 4 | osc 1 envelope depth | 25 | filter mod depth (env 2) |
| 5 | osc 1 LFO depth | 26-29 | utility settings, two nibbles each (see "CV transfer") |
| 6 | osc 1 pulse width | 30 | pitch bend range, 0-12 |
| 7 | LFO speed | 31 | MIDI-to-LFO clock rate |
| 8 | LFO delay | 32 | E0: bit 7 osc 1 saw, 5-6 range, 4 osc 2 saw, 3 mod source, 2 12 dB, 0-1 LFO shape |
| 9 | filter frequency | 33 | E1: bit 6 sync, 4-5 triggering, 2-3 osc 1 PWM source, 0-1 osc 2 PWM source |
| 10 | resonance | | |
| 11 | filter mod depth (LFO) | | |
| 12-16 | env 1 attack, decay, sustain, release, velocity | | |

- Pot bytes run from 03 to FC in the factory programs. The bipolar pots are centred at 80.

## The default EEPROM

The owner's manual says a new unit has:
- programs 40-99 "initially stored with a basic square wave tone" (the "Plain Vanilla" tones);
- MIDI channel 1;
- program 00 selected.

v1.5 has no template or routine for this, so `Board::defaultEeprom()` builds it.

**Global settings:** poly off, CV/Gate type 00 and channel 1, MIDI TX and RX channel 1, program
00, no CV IN trims.

**The program**, in all 60 user slots:

| Byte(s) | Value | Why |
|---|---|---|
| 0 portamento | 03 | off |
| 1 detune | 80 | centre |
| 2 semitone | 03 | 0 |
| 3 mix | 50 | osc 1 only: the curves give it full level and osc 2 none |
| 4, 5 osc 1 env/LFO depth | 80 | centre: no modulation |
| 6 osc 1 pulse width | 03 | square |
| 7 LFO speed | 9C | what most factory programs have |
| 8 LFO delay | 03 | none |
| 9 frequency | FC | open |
| 10 resonance | 03 | none |
| 11, 25 mod depths | 80 | centre: no modulation |
| 12-16, 17-21 envelopes | 03 03 FC 03 03 | instant attack, full sustain, instant release, no velocity: a plain gate |
| 22, 23 osc 2 depths | 80 | centre |
| 24 osc 2 pulse width | 03 | square |
| 26-31 utility settings | | as nearly all factory programs: pitch bend 2, MIDI-to-LFO clock off |
| 32 E0 | | both oscillators pulse, osc 2 at 8', mod source env 2, 24 dB, LFO shape code 1 (each as most factory programs, where the tone doesn't decide) |
| 33 E1 | | sync off, triggering code 0, PWM source MAN. for both oscillators |

## Sysex

- **Parsing:** bytes after F0 are taken in pairs, as nibbles (77BA). The state machine is at
  74C6, state in `(x'95')`.
- **Header:** 00 20 29 (Novation's manufacturer ID), then 01, then a type pair:
  - 02/22 and 03/22: load the 34-byte edit buffer (00C0).
  - Other 02/xx and 03/xx: write the EEPROM from offset 0 (74A1, 74BE).
- **After a 02/xx load:** at the end (7535), v1.5 converts programs 40-46 (7629).
  [open: what each type is exactly: single vs all, Rack vs keyboard format]
- **Conflict with the manual:** the manual says an "All" dump writes the EEPROM even with the
  Write switch at Protect. v1.5 checks the switch (`(x'B6')` bit 6) before both EEPROM-writing
  states (749E, 74BB) and rejects the dump at Protect (74DE).

## MIDI

- **In:**
  - The UART is in mode 62, 8N1 at 31.25 kbaud; a byte is taken 9.5 bit times after its start
    bit.
  - The IRQ9 handler (5654) puts bytes into a 32-byte ring at 0100.
  - Clock and start/continue/stop (F8, FA, FB, FC) are handled for the LFO sync (5814).
  - Program change n selects program n.
- **Out:**
  - A 16-byte ring at 0190 is sent by polling the UART's busy bit (`(x'18')` bit 3, 7777).
  - v1.5 sends a controller for each pot that moves. It also sends the CV/Gate converter's notes
    from GATE IN edges, on the CV/Gate channel with the note from CV IN (56EC-573A); the poly
    mode's notes; the demo's drum track; and sysex dumps.
  - At boot it sends a stray 00: the receive-ring set-up at 35FB-35FD also leaves 01 in the
    transmit ring's tail index.

## What MAME lacks, and what Lemus infers

MAME's MN1880023 model covers what the Drum Station, CR-504 and PSR-500 firmware use. For v1.5
Lemus adds the following, in `tools/port_mn1880.py`'s `V15` list. Each is marked `LEMUS: inferred
from v1.5 at <address>` in `core/mn1880/mn1880.cpp`.

- **Timer 0 in control mode 14:** counts up at oscillator/16, reloads at each overflow, a write
  doesn't restart it, IRQ3 (35D8, 785B, 77D4). MAME runs timer 0 only in mode 8A.
- **Timer 1** at 0x22/0x23 with run bit `(x'31')` bit 5: as timer 0, IRQ4, with a toggling output
  pin, OSCB_SYNC (77D4-77EF, 784E). MAME has no timer 1, and uses IRQ4 for its PWM.
- **oscillator/16 for timers 0 and 1:** from the period table and note 60.
- **The ADC channel** in `(x'1E')` bits 0-2 (34AD).

In the board, not the CPU:
- RAM to 03CF;
- the DAC protocol and its pipeline;
- the S&H channels;
- the pot mux;
- the switch matrix at 0x8800 (MAME's Bass Station driver calls it the button input);
- the keypad on port 9;
- the inputs on port 2;
- the display multiplexing.

Not modelled, because v1.5 only writes them and nothing it does depends on them:
- `(x'32')`/`(x'34')` (read by the voice as levels);
- `(x'5D')`, `(x'0E')`, `(x'0F')`, `(x'1F')`, `(x'3F')`, `(x'40')`-`(x'45')`;
- timer 2's FFF8;
- the EEPROM's write time.

## A lock-up in v1.5, and Lemus's patch

v1.5's pot scan can lock the unit up.

**How the scan works:**
- Each 4 ms tick, the main loop reads two pots.
- One is the next **active** pot: one that has moved. Its flag at 0280 + n is set to FF when it
  moves (699C) and counts down by one each time it is scanned (697A).
- The other is the next **idle** pot, which sub_6A10 finds by looking for a flag of zero. It is
  called every tick from 5475.

**The bug:** 6A10's search (6A16-6A26) has no exit. With all 17 flags non-zero at once it spins for
good.
- The main loop stops, so MIDI notes, the keypad and AUDITION do nothing until power-off.
- The interrupts still run, so the CV refresh and the MIDI input ring keep going.

**How easily it happens:**
- One active pot's flag lasts about 1 s. With n pots active each counts down only every n-th tick,
  so 17 last about 17 s.
- Turning all 17 knobs about 0.3 s apart reaches it, and so does anything that moves all the pots
  at once (`bsrackseq --potstorm=60`).
- Nothing in our CPU or UART model is involved: the loop reads RAM only. A real v1.5 unit should
  lock up the same way. [inferred: not checked on hardware]

**The machine is faithful to it** (decided 2026-11-25). The plugin locks up as the unit would, until
the State pop-up's Reset; a session load with its EEPROM is safe, since the reboot takes the pots as
they are.

**The patch, for the tools only:** `Board::setRom(rom, true)` patches the ROM image, and only when
the bytes are v1.5's.
- The call at 5475 goes to a check in unused ROM at 356B-3578 (all FF in v1.5) instead.
- The check calls 6A10 only if some flag is zero, and otherwise returns: the idle pointer waits a
  tick.
- Outside the locking state the firmware runs unchanged, apart from the check's few cycles per
  tick.

**Regression case:** `bsrackseq <rom> --potstorm[=GAP]` moves all 17 pots to FF and back, GAP ms
apart. It then checks that a note and AUDITION open the VCA, and exits 0 on PASS.
- At GAP 60 it reaches 17 active pots: as v1.5 it locks (FAIL, v1.5's own behaviour); with
  `--patched` it passes.

## Open questions

- **The crystal:** 8 MHz fits everything; a check against the factory demo's pitch is still to do.
- **Timer 1's reload on a sync restart:** the three-edge burst could be one on the real part.
- **The pot-scan lock-up:** whether a real v1.5 unit locks up as the code says.
- **Osc 1's index offset:** one table step above osc 2, back to it one tick in 17. Deliberate
  (against phase-locked unison) or a rounding in the pitch path?
- **Triggering names:** codes 10 and 00 are named SINGLE and MULTI by panel order only; both
  retriggered in a legato test.
- **The breath nibbles:** the low nibbles of bytes 27 and 28 are presumably breath to pitch and to
  filter, as byte 29's is breath EG bias (measured).
- **Detune's range:** about ±4 semitones at the ends, against the manual's ±50 cents.
- **`(x'32')`/`(x'34')`:** what they drive; and where the external input's level comes from.
- **Port 3 bit 7:** GATE OUT, and its polarity? Also the units digit's point.
- **The sysex types:** exactly what each is; and the Write-switch check against the manual.
- **The DAC part, the S&H hold and the CV scaling:** for the voice.
