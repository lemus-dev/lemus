# Access Virus C: what we know is wrong, and the plan

What the emulation gets wrong or hasn't checked, and what is planned. The firmware runs as it is
(OS 6.6) on an emulated 80C515 and on the dsp56300 engine for the DSP56362 (`docs/firmware.md`,
`docs/research.md`). There is no real unit to measure: recordings other people made are the
reference, and none with isolated notes has been found yet.

## Known inaccuracies

### Sound

- **Not compared with a real unit.** The DSP engine is the one Gearmulator's Osirus uses, so Osirus
  can't check the sound; only recordings of real units can. What there is (Reference data, below):
  phrases on known factory programs, lossy, notes unknown. First comparison (E73 PowerStrnJ, three
  sustained chords, the notes read from the spectrum): 3.5-5.3 dB mean difference per third octave,
  100 Hz-8 kHz, with no direction the chords agree on; below 600 Hz the guessed voicing dominates.
  Against Osirus on the same notes, 0.5-1.0 dB. Isolated notes on known programs would settle it.
- **Against Osirus, the firmware paths agree** (2026-11-11, see Done): the same DSP program, the same
  upload for all 768 flash programs but two bytes that change nothing, the same words for notes and
  every kind of channel message; sysex parameter changes on pages A, B and C, a single dump into the
  edit buffer, multi mode (15 of 16 parts: Osirus loads part 10's program wrong); banks A and B
  (a fresh memory's, copies of C and D) all 256 the same through the RAM window. Osirus has no panel, so the knobs and menus are checked against the manual instead.
- **The output stage, half of it.** The DACs (three CS4390s) are modelled from their datasheet:
  filter, noise floor, auto-mute (see Done; `docs/firmware.md`, "Audio in and out"). Not modelled:
  their distortion (THD+N -98 dB), and the op-amp stage between them and the jacks (gain, filter, the
  coupling capacitors' low cut), which the photos don't show. Waits on data: sharper photos of that
  corner of the board. **Assumption:** the DACs' auto-mute is wired on.
- **Sample rate 46,875 Hz (12 MHz / 256)**, from Gearmulator's notes, not checked against the
  firmware. The resampler to the host's rate isn't measured for quality; its peaks pass full scale
  by up to 0.2 dB (chord workload, 1.022).
- **Input converter not modelled.** The side-chain reaches the DSP as it is: no input circuit or ADC,
  and the DSP reads it 0.68 ms ahead of what it sends; the real converter isn't known (not in the
  photos). Waits on data, as the op-amp stage.
- **The inputs need a host that feeds an instrument's side-chain.** Onyx DAW does (`set_sidechain`,
  tried, see Done). Logic, Live, Cubase, Reaper, Bitwig and FL Studio (VST3) do, from their
  documentation and forums, not tried here; GarageBand doesn't seem to. An effect build of the plugin would work in all of them but breaks one
  plugin per machine.

### Behaviour and features

- **Firmware against the manual: checked** for every key and LED, STORE, LED MODE, category search,
  every menu's pages, MIDI DUMP TX, MIDI out and the power-on keys (`docs/firmware.md`). Open:
  - **STORE TO FLASH: the DSP's part is stood in for.** The program the DSP runs at P:$1000xx to hold
    the sector while the flash is written isn't in what this board has; the board does what the
    firmware asks of it (`docs/firmware.md`, "Store to flash"). Erases finish at once, where the chip
    takes about a second a sector.
  - After a MIDI program change, VALUE steps from the program last chosen on the panel until the
    program view is entered again. Taken as the firmware's own; not confirmed on a unit.
- **Fresh memory.** A new instance starts with banks A and B (battery RAM) as copies of flash banks
  9-11. **Assumption:** what Access put in RAM at the factory isn't known.
- **LEDs.** 12 are placed by wiring order only (LFO 2's and LFO 3's destinations, BPM): they never
  changed in the tests. Four lines that are always on (bits 5-6 of each latch) aren't identified,
  and scan positions 6B2-6B4 light nothing. The two LFO RATE LEDs are drawn as bright as the scanned
  ones, assuming the same current.
- **Panel.** How light the knobs and buttons are is a judgement between photos: they range from near
  the plate (daylight, top down) to light slate (studio light). The rear panel's MIDI THRU isn't
  emulated: the plugin's MIDI output is MIDI OUT.
- **A note sent with a program change** was once seen lost while the patch uploads
  (`docs/firmware.md`, "Sleep"); not seen again since the HREQ fix, not proven gone.
- **The recompiler's first use.** Code no earlier program used costs up to 8.4 ms in one block the
  first time it runs: 3 blocks over 2.67 ms with the plugin's warm-up, 16 programs a bank, none the
  second time (`viruscbench <rom> spikes 128 16 1`). Warming up more at boot would change how the unit
  starts, since the firmware keeps what it plays (`docs/firmware.md`, "The recompiler at run time").
- **Windows: built, not played.** The VST3 and the standalone app build and package with MSVC and
  clang-cl (`windows-machine.yml`), and the host test passes there, but without the ROM, which the
  runner doesn't have: the Virus's own checks haven't run on Windows.

### Reference data

| Unit | Where | What it has | Known settings |
|---|---|---|---|
| Access's own Virus C (web demos, 2002) | `private/virusc/reference/access-demos` | 41 preset phrases, 13 filter demos, 3 arpeggiator demos; MP3 at 22.05 kHz | programs unknown; the filter demos' patch names aren't in the ROM |
| A Virus C (YouTube, 2008) | `private/virusc/reference/youtube/wjplLpYF7go` | played phrases on E73, C40, D19, F10 | factory programs, notes unknown; AAC 128 kbps |
| A Virus KC (YouTube, 2017) | `private/virusc/reference/youtube/DZKK5s1scdw` | bank A 0-50 played in turn | factory programs, notes unknown; AAC 128 kbps |
| Other Virus C demos (YouTube) | `private/virusc/reference/youtube` | demos, the factory demo songs | `SOURCES.md` there |

None has isolated notes. The factory demo songs can't be replayed: this ROM has none ("NO DEMOSONG
but 1024 Sounds!"). The panel's layout and colours come from photos (`docs/panel.md`).

## Budget

- **CPU:** `viruscbench time`'s busiest workload (`program changes`, `cutoff sweep`) under 8 % of a
  core at 48 kHz (M5 Max), OUT 1 only. Last measured (2026-11-11, after the idle skip): `cutoff sweep`
  5.4-5.6 %, 1,306 million instructions a second of audio; every workload 1.2-5.6 %; with every jack on
  5.6 %, 1,394 million. The bench now counts instructions too (`Minstr/s`), steady to about 1 % where
  the time moved 3 % under load: compare those.
- **Check set baseline:** 2026-11-11. Golden renders `build/virusc-golden-4` (six workloads at 44.1
  and 48 kHz, bit-identical when rendered again; new with the DACs, see Done; `VIRUSC_NO_DAC=1`
  renders golden-3);
  `viruscplugintest` 27 checks; host test passes; `auval` passes; `viruscbench <rom> spikes 128 16 1`
  3 blocks over 2.67 ms on the first pass, worst 8.4 ms, none on the second; all 768 flash programs
  the same with and without the idle skip.
- **Oracle replay** (`tools/compare/replay.py`, against Gearmulator's Osirus console with local
  patches; private/virusc/reference): uploads identical for 768 of 768 flash programs but page A
  bytes 1-2 and the last word's stale low bytes; note bytes 768 of 768; `controllers` 50 of 50 words.
  Sound, each program on a fresh board: level median +0.00 dB, band shape median 1.31 dB, against
  1.45 dB for Osirus against itself with the note 0.34 s later; the 72 programs that move least
  in time within 0.49 dB and 0.67 dB.

## Plan, in order

1. More of the real recordings: C40, D19 and F10 from the same video, the KC's bank A 0-50; better
   still, isolated notes on known programs (accuracy, waits on data).
2. Oracle replay: the sound with the note at the same time since boot on both, which the fresh boots
   don't give (accuracy).
3. CPU: what is left of the audio path (cutoff sweep): recompiled DSP code 49 %, the 80C515 22 % (its
   timers, ADC and UART every machine cycle), the DSP's peripherals 12 %, the idle skip's checks 8 %,
   the board 7 % (cpu).

Waits on data: the op-amp stage and the input converter (sharper board photos), the factory RAM (a dump of a unit never
stored to), the sound against a real unit (recordings).

## Tried and dropped

- 2026-11-11: the oracle replay as one long run through each bank. What the program before left
  (delay and reverb tails, free-running phases) moved notes by up to 24 dB of band shape against
  Osirus, which starts each program fresh. Replaced by one fresh boot per program.
- 2026-11-09: compiling DSP code ahead from a list of addresses, to avoid the recompiler's
  first-use blocks. What a block compiles to depends on the DSP's state when it first runs, so a
  chord played unheard at the end of boot does it instead (`Machine::warmUp`).
- 2026-11-10: panel colours from the straight-on master photo: lamp-lit, they came out brown.
  Replaced by values from four photos with neutral surroundings.

## Declined

None yet.

## Done

- 2026-11-11, accuracy round: the DACs. Three Cirrus Logic CS4390s, read off a repair shop's board
  photos; modelled from the datasheet (`core/dac.h`, `docs/firmware.md`, "Audio in and out"): the
  filter as the resampler's kernel from 48 kHz up (fitted to the datasheet's plots within 0.16 dB rms,
  flat to 21.26 kHz, 25 samples of delay), the noise floor 103 dB under a full-scale sine, the
  auto-mute after 8,192 still frames. Against golden-3, at 48 kHz: level 0.000 dB on every workload,
  under 18 kHz within 0.03 dB; the top as the DAC has it, where the old resampler was -0.4 dB at
  20 kHz, -2.8 dB at 21 kHz and -9.9 dB at 22 kHz (the DAC: flat, flat, -0.27 dB). At 44.1 kHz only
  the noise changes. Idle: the noise until the DACs mute (0.27 s), then exact zeros. With
  `VIRUSC_NO_DAC=1` golden-3 again (one sample at -228 dB: a filter coefficient now computed at run
  time). CPU at 48 kHz, run alternately: cutoff sweep 5.05-5.12 % without, 5.09-5.16 % with; idle
  1.11-1.12 % and 1.11-1.16 %. New golden renders `build/virusc-golden-4`; `viruscplugintest` 27
  checks, host test, `auval` pass.

- 2026-11-11, accuracy and CPU: the rest of the plan.
  - **The host port's select.** The sysex replay found part 1 going to the DSP with its first bytes
    wrong in multi mode: its edit buffer is in the RAM at $0400-$0407, where the board had the host port
    answer whatever the 80C515 did. P3.3 low selects the port (49,120 reads and 150,366 writes there
    with it low, 30 and 27 with it high, in an 18 s sysex run); high, it's RAM. The flash programmer's
    copy goes there with P3.3 high too, so the write to the RAM under the port is gone (it was an
    assumption). New golden renders `build/virusc-golden-3`: level within 0.01 dB of golden-2, the
    residual -39 to -45 dB, peaks up to -6.5 dB apart.
  - **RESET resets the port's host side** (ICR, CVR, IVR as at power-on). Without it, after STORE TO
    FLASH the firmware booted the DSP with the byte order its other routines left swapped, and the
    boot words arrived reversed: the plugin test crashed there once the select was in.
  - **Oracle replay, the rest** (`replay.py sysex`, `--banks AB`): sysex parameter changes on pages A,
    B and C and the note after them, the same words; a single dump into the edit buffer, the same;
    multi mode, 15 of 16 parts the same, Osirus loading part 10's program wrong (13 asked, 10 loaded).
    Banks A and B against Osirus's C and D, which they copy: 256 of 256 uploads and notes the same, the
    sound level median +0.02 dB, band shape 1.32 dB against 1.44 for the control. The 768 flash
    programs again after the select: unchanged.
  - **The menus against the manual:** every EDIT-type key's pages (oscillators, filters, LFOs, effects,
    delay, arpeggiator with ARP on, EDIT, SYSTEM), the names as the manual's.
  - **The rear panel:** REAR in the bar shows it, from `panel.json`: MIDI IN/OUT/THRU, OUT 1-3, the
    inputs, DC, the ON/OFF switch. Cables hang from MIDI IN, DC, OUT 1, and OUT 2, OUT 3 and the inputs
    while the host has them on. The switch works: off, silent and dark, the RAM and the flash kept for
    the session; on, a boot from them. `viruscplugintest`: "switched off it is silent (0); switched on
    it boots with bank A as stored".
  - **First-use blocks**, measured with the warm-up as the plugin runs: 3 blocks over 2.67 ms on the
    first pass (worst 8.4 ms), none on the second. Kept (`docs/firmware.md`).
  - **Several instances:** two on two threads render what one renders alone, sample for sample
    (`viruscplugintest`).
  - **Windows:** the VST3 and the standalone app (now declared, as the other machines have it) build
    and package with MSVC and clang-cl. MSVC needed `/permissive-` for the engine, as upstream builds
    it; the resampler's `M_PI` became a constant.
  - Checks: golden renders identical to golden-3; `viruscplugintest` 27 checks; host test, `auval`
    pass; all 768 programs the same with and without the idle skip.

- 2026-11-11, CPU round: the DSP's idle loop. Between blocks of frames the program goes round
  $2C087-$2C0A7 waiting for the next block: 89 % of its instructions with one note on "- Init -", 64 %
  with an eight-note pad. Once a pass found what it reads as the pass before and no interrupt waits,
  the board counts the passes up to the next time the peripherals run as run, landing where the run
  would have them run (`docs/firmware.md`, "Running the DSP"). Also: the 80C515's UART no longer samples
  a line that stays high while it waits for a start bit. Bit-identical: golden renders identical at
  44.1 and 48 kHz, all 768 flash programs rendered with and without (`VIRUSC_NO_IDLE_SKIP=1`) the
  same, `viruscplugintest` 25 checks pass. Instructions a second of audio at 48 kHz, before and after:
  idle 341 to 287 million, one note 1,853 to 992, chord 1,819 to 1,054, 32 notes 1,842 to 1,079,
  cutoff sweep 2,132 to 1,306, program changes 2,091 to 1,114 (39-46 % fewer); CPU, cutoff sweep 7.9 %
  to 5.4-5.6 %, every jack on 8.1-8.5 % to 5.6 %. `viruscbench spikes`, the same with and without the
  skip under the same load (4-5 blocks over 2.67 ms on the first pass, worst 9.4-9.8 ms; mean block
  0.146 ms against 0.226). The bench gained an instruction count per workload.

- 2026-11-11, accuracy round: STORE TO FLASH writes the flash. Traced in the firmware: it parks the
  64 KB sector in the DSP (which it sends to its bootstrap ROM and on to a program at P:$1000xx), copies
  its flash programmer from bank 7 into RAM, and runs it with P1.0 low, which swaps code and data below
  $8000: code from the RAM, data to the flash. Modelled: P1.0's swap; the Am29F040B's commands
  (unlock, program, sector and chip erase, IDs, reset), a program answering one read with its status;
  the written sectors in the session, over the ROM file. Stood in for: the DSP program, from the
  firmware's side of its protocol. Bank A over E: "programming OK! 00 00 00" (with programs done at
  once it read 00 00 02: the firmware polls the bank's last byte), E0 then "AutoBendBC", F as it was,
  and so after reloading the session (state 299 KB with the sector). `viruscplugintest` checks all of
  it, 25 checks. Golden renders identical; CPU at 48 kHz against the build before, run alternately
  under the same load: cutoff sweep 7.9-8.0 % before, 7.9-8.2 % after, program changes 7.7-7.8 % and
  7.7-8.1 % (both above this morning's 7.1-7.2 %, run with the machine quieter); host test and `auval`
  pass. The flash programmer's place in bank 7 came from the dsp563c-tools notes (README).

- 2026-11-11, accuracy round: the firmware against the manual (`docs/firmware.md`, "Panel keys",
  "MIDI out", "Store to flash"; `viruscrun` gained `dump`, `dasm`, `VC_WATCH`, `VC_P4FIRST`,
  `VC_MIDIOUT`, `VC_DSPPC` and `VC_PINS` for it).
  - **STORE TO FLASH crashed the host.** Confirmed from the SYSTEM menu, the firmware sends the DSP
    to its bootstrap ROM, which the engine doesn't have: the recompiler compiled on through empty
    memory, each block inside the one before, until the stack overflowed (`viruscrun` exit 139, the
    stack 8 MB deep). Now the recompiler stops past the end of P memory (one guard in the engine,
    README) and the board stands in for the ROM. The unit shows "wait...", initialises its global
    memory, boots the DSP again 11 s after the confirm and plays (C126 at -22.5 dB, a new
    `viruscplugintest` check). The flash isn't written (Known inaccuracies).
  - **PART - and PART + in SINGLE mode are the category search**, not a fault: with Bass chosen
    (hold SINGLE, step PARAMETER) PART + went A0, A2, A5, A9, exactly the Bass programs; with Off,
    the programs without a category (C126, C127).
  - **The "program 127 to 1" wrap was a MIDI program change.** VALUE steps from the panel's copy of
    the program number ($441B), which a MIDI program change doesn't refresh until the program view
    is entered again. With the panel alone, VALUE stops at 0 and 127.
  - **Power-on keys:** this OS reads them once, 30 ms in: ARP EDIT, DELAY EDIT and EFFECTS SELECT
    clear the battery RAM, and the firmware refills banks A and B from the flash. MAME's test modes
    are the Virus A and B's.
  - **MIDI out** matches the controller map (knobs as CCs, page B keys as sysex, at 31,250 baud);
    MIDI DUMP TX sends a valid single dump, the flash's bytes but for page A bytes 1-2.
  - Program changes over MIDI every 0.2 s, by CC 0 or CC 32: all taken.
  - Check set: golden renders identical; CPU at 48 kHz, busiest workload 7.1-7.2 % (the PC check
    after each DSP step costs nothing measurable); `viruscplugintest` passes, 24 checks with the
    new one; host test passes; `auval` passes.

- 2026-11-11, accuracy round: the oracle replay against Osirus (Gearmulator's console with local
  patches, which replaces the 80C515 with C++ and runs the same DSP engine). `tools/compare/replay.py`
  plays each of the 768 flash programs (banks C-H) on a freshly booted board, C4 at velocity 100 for
  1 s, and the same on Osirus, then compares the words each sends the DSP and the sound of the note.
  - **The DSP program:** the 47,233 words of the command stream are the same; here they come after
    the 83-word bootloader the firmware sends, which Osirus loads directly.
  - **Program uploads:** identical word for word for all 768, but for two things. Page A bytes 1-2
    (named Modulation Wheel and Breath Controller): the firmware sends 0 and the bank it was
    told (C = 2), Osirus the stored bytes, which run 0-126 in steps of 2 and 0-1 like storage
    metadata. Osirus with a ROM whose bytes 1-2 were set to what the firmware sends rendered all 768
    programs bit-identically: they change nothing. And the last word's two low bytes are whatever
    the host port's registers held (07 00 here), as on the real HI08; Osirus sends 00 00.
  - **Notes and controllers:** the firmware passes channel messages to the DSP one byte a word, as
    Osirus does; identical for the note on and off of all 768 programs and for 17 messages of
    every kind (`replay.py controllers`: mod wheel, volume, CC 40 and 74, pitch bend, channel and
    poly pressure, sustain, pan, expression, breath, sound off, reset, notes off).
  - **Globals:** the firmware sends 23 global settings at boot, Osirus 7; the one both send that
    differs is the device ID (0 here, 16 on Osirus). Osirus given all 23 rendered the seven most
    different programs bit-identically: no audible global differs.
  - **Sound:** level ours minus Osirus median +0.00 dB; band shape (third octaves 100 Hz-12.7 kHz,
    mean |difference| where either is within 40 dB of its peak) median 1.31 dB, against 1.45 dB for
    Osirus against itself with the note 0.34 s later. The 72 programs that hardly move in time
    agree within 0.49 dB in level and 0.67 dB in shape. The ones that differ most (F52, C63, F81,
    D107, F90, F26, F51, C79, F77, F12, H102, H114, F33) depend on time since boot on Osirus too:
    with the note 13.5 s after its start instead of 0.5 s, Osirus differs from itself by up to
    21.8 dB (C63), and here the note comes 13.5 s after power-on. What they have: free-running
    LFOs, noise, a phaser at rate 0 left where its LFO stood (D107), the arpeggiator on its
    free-running clock (F90).
  - **Onset:** the note sounds 1.73 ms later here (median): the MIDI bytes take about 1 ms on the
    UART at 31,250 baud and the firmware passes them on; Osirus hands them to the DSP at once.
  - No change to the machine; CPU as before. In a single run through a bank, what the program before
    left (tails, phases) moved each note by up to 24 dB of band shape, so each program now plays on
    a fresh board.

- 2026-11-11: the inputs in a host. Onyx DAW gained side-chains for instruments (`set_sidechain track
  source`: the source track after its effects and EQ, before its mute and fader, into the
  instrument's audio input; the instrument reloads with its input on, keeping its state). In Onyx,
  on a state from `viruscplugintest`'s `VC_SHELL_IN` tool with the new `VC_ROUTE=input` (C126
  "- Init -", INPUT Static on IN L+R): a -12 dB 1 kHz sine from an audio track came out of OUT 1 at
  -14.2 dB peak both sides (the plugin test: -17.2 dB RMS, the same level); with the source muted and
  its fader down, unchanged; with the side-chain off, silent; on again, back to -14.2 dB.

- 2026-11-10, accuracy round: the rear panel's other jacks. OUT 2 and OUT 3 are stereo outputs (off
  until the host turns them on), the two inputs a stereo side-chain, read only while the host has it
  on (with nothing routed, Live leaves an instrument's own last output there). The ESAI's
  transmitters 0-2 are OUT 1-3 and receiver 0 the inputs, as Gearmulator maps them; the firmware's
  routing agrees (`docs/firmware.md`, "Audio in and out"). New `viruscplugintest` checks, 24/24:
  SECOND OUTPUT to OUT 2 and MULTI part 0 to OUT 3 put a note on that pair only; INPUT Static plays a
  side-chain sine on OUT 1 (100 % at its frequency); with the side-chain off, a sine left in the
  buffer isn't heard. In Onyx DAW (routing set by a state from `viruscplugintest`'s `VC_SHELL_IN` tool,
  Onyx having no sysex): MULTI part 0 to OUT 3 sounds on OUT 3's channel only, SECOND OUTPUT on OUT 1
  and OUT 2; Onyx feeds no instrument an input, so the side-chain wasn't tried there. The host's
  `MachineInfo` gained `stereoOutputs` and `sideChain`, opt-in (the
  808's host tests unchanged). Golden renders identical. CPU at 48 kHz, against the code before,
  run alternately: OUT 1 only, busiest workload 7.3-7.6 % before and 7.5-8.0 % after, within the
  runs' spread; every jack on (`VC_BENCH_JACKS=1`), 8.1-8.5 %, over the budget.

- 2026-11-10, accuracy round: everything played flat, a semitone and more under load. The DSP had one
  frame period to hand over each frame; when it ran late it fell behind real time and the frames it
  hadn't made repeated the last sample (13 % of frames under a six-note chord). Now it may run 16
  periods (`docs/firmware.md`, "Running the DSP"). A4 on C126 "- Init -": 419.6 Hz before, 440.65 Hz
  after, Osirus 440.53 Hz. E73 PowerStrnJ against Osirus, same notes, third octaves 100 Hz-8 kHz: mean
  difference 1.8-5.1 dB before (ours darker), 0.5-1.0 dB after. Against the real recording, after:
  3.5-5.3 dB mean, voicing guessed. CPU: busiest workload 7.0 % before, 7.1 % after; worst block 1.8
  to 2.6 ms. New golden renders `build/virusc-golden-2`. Found by listening to the A/B against the
  real recording and Osirus (`private/virusc/reference/scenes/e73-chords.json`).

- 2026-11-10, accuracy round: the first comparison with a real unit, E73 PowerStrnJ from a 2008
  recording (`private/virusc/reference/youtube/wjplLpYF7go`), three sustained chords against the
  same notes rendered by `viruscrun`, spectral shape in third octaves (`tools/compare/bands.py`).
  Ours minus real, 1-5 kHz: chord 2 -3 to 0 dB, chord 3 -3 to +2, chord 4 -5 to 0; 6-10 kHz -1 to
  +6. No consistent tilt; put down to the guessed voicing and velocity, the recording chain and the
  AAC encoding. (Ours was a semitone flat then, see the entry above: the comparison missed it.)

- 2026-11-09 to 2026-11-10, build (phases 1-5): research and sources (dd4c688, 90a31ac); the
  firmware on an 80C515 and the DSP56362 engine (5fe1e84); block timing and the warm-up (85330eb);
  battery RAM, STORE, sleep holding only the DSP (33020e5); the panel (07c796d) and its colours
  (a8349ae); steady LEDs and the LFO RATE LEDs from the DSP's timers (3aa3ed8); HREQ on P3.2, which
  ended a hang after about 20 program changes from the panel (cd27ac7); this file and the check
  set's bench.
