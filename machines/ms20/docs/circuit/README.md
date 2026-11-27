# MS-20 circuit: transcription and netlists

The target is Rev 1 as the service manual draws it (KLM-127 main board with two KORG35s, KLM-128,
KLM-129). Every block is transcribed part by part into a page here and a netlist in
`core/netlists/`, run by the shared solver (`core/circuit.h`, the 909's by way of the Minimoog) and
checked against ngspice before any fitting.

## Where to read the schematic

All in the private folder (`private/ms20/docs/`, listed in `docs/sources.md`):

| Source | What | Use |
|---|---|---|
| `manuals/service_archive_a/img/a-003.png`, `a-004.png` | Scan A, circuit diagrams (1) and (2), upright, 2217×1618 and 2098×1421 | First reading; retouched, so check thin lines against the others |
| `manuals/service_hyperreal_1994_tif/tif/korg_ms20_4.tif`, `_5.tif` | 1994 scan, 2485×3496, 1-bit, rotated | The sharpest copy of the Rev 1 drawing: confirm every value here |
| `manuals/service_archive_b/img/b-003.png`, `b-004.png` | Scan B, 2132×3000, rotated | Third copy |
| `schematic/hyperreal_late_klm307_klm127/KLM-127.GIF` | The **later** main board (with the KLM-307 daughterboard), 3937×3081, clean | Legibility only. It is Rev 2: a value read here counts only where a Rev 1 scan agrees |
| `schematic/korg35_korg_2000/KORG35.pdf` p. 3 | The KORG35's insides (Korg, 2000) | The module's parts; cite them to Stinchcombe's study (research.md §2.1) |
| `manuals/service_archive_a/img/a-008.png` | Block diagram with check-point levels | Levels to check against |
| `manuals/service_archive_a/img/a-009.png` | Adjustment procedure | The service checks (research.md §1.5) |
| `datasheets/` | Part data sheets | Device models (`semiconductors.md`) |

Crop and enlarge with ffmpeg (`ffmpeg -i in.png -vf "crop=w:h:x:y,scale=2*iw:-1" out.png`) or
Pillow from the repo's `.venv`.

## Conventions

**Files.** One netlist per block in `core/netlists/<block>.cir`, its transcription in
`docs/circuit/<block>.md`, its ngspice cases in `tools/spice/cases/<block>.json`. Semiconductor
models only in `core/netlists/models.cir` (`semiconductors.md`); a block's netlist uses them by part
type and is run as `models.cir,<block>.cir`.

**Header.** Each netlist starts with comments: the block, the board, the schematic it comes from,
its inputs, outputs, controls and probes, and what is not modelled. Element names keep the
schematic's designators (`R105`, `C26`, `Q19`, `IC12A`); a diode is `D<n>`, a part inside the
KORG35 is `<IC>_<part>` (`IC10_Q2`, `IC10_R3`).

**Supplies.** `VP15 p15 0 15`, `VN15 n15 0 -15` (7815/7915), `VP18 p18 0 18` (the unregulated
+18 V, which feeds the VCO buffer JFETs' drains; its ripple is not modelled). Ground is `0`.

**Inputs.** A jack or a signal from another block is `V<name> <node> 0 input <name>`, with the names
below. A jack's input is the voltage the cable brings (an op-amp output: a stiff source); an
unpatched jack is the normalled signal, which the board feeds into the same input.

**Outputs.** `.output <name> <node>` for each jack output and each signal another block takes.
Split blocks only where the signal is an op-amp output or a jack: never across a passive node.

**Controls.** The panel's knobs by their parameter ids (`plugin/PluginProcessor.cpp`:
`lpf_cutoff`, `vco1_pw`, `eg2_attack`...), each a pot (`P<VRn> a wiper b <ohms> <id> <taper>`)
whose control is the rotation 0 (fully anticlockwise) to 1. Japanese pot codes: **A** audio
(`p2.25`, the share rotation^2.25, 21 % at mid, since 2026-11-23: fitted to four readings in real
recordings, `improvements.md`; `a0.15`, two segments through 15 % at mid, before), **B** linear,
**C** reverse audio (`c0.15`). A rotary
switch is one `S` element per contact with controls `<id>_<position>` (1 = in that position). A
trim (semi-fixed resistor) is a pot whose control is `trim_<VRn>`, set by the service checks.

**Op-amps.** `X<IC><A|B> in+ in- out opamp <gbw> <gain> <vsat> <sr>` with the data sheet's typicals
at ±15 V (`semiconductors.md` has the numbers for the 4558, TL081/082 and CA3140).

**Interface names** (inputs and outputs between blocks; jack names follow `core/patch.h`):

| Name | What |
|---|---|
| `kbdcv` | KBD CV OUT (J33): the held, portamento'd keyboard CV, Hz/V, 1.048-8.387 V (C-1..C-4) |
| `kbdtrig` | IC13B's output, ±14 V (+14 idle, -14 with a key down), into the EGs through R80 (eg1.cir, eg2.cir) |
| `trigout` | KBD TRIG OUT (J32): +5.16 V idle, 0 V with a key down (S-trigger) |
| `trig`, `eg1trig` | a cable's voltage in EG 1+2 TRIG IN (J27) or EG 1 TRIG IN (J20); the normals are in the EG netlists |
| `vcocv`, `vco2cv` | VCO 1+2 CV IN (J26), VCO 2 CV IN (J24) |
| `totalext` | TOTAL EXT MODULATION (J4), normalled to `mg` |
| `extfreq` | VCO 1+2 EXT FREQ (J6), normalled to `eg1vco` |
| `ia1`, `ia2` | the VCO cores' charge currents, amps (vcoctl.cir), into the C++ cores (core/vco.h) |
| `ramp1`, `ramp2` | the cores' ramps, back from the C++ cores (the cascodes' drain voltage) |
| `vco1`, `vco2` | the VCO wave switches' commons, loaded by VCO LEVEL, into the mixer |
| `extsig` | EXT SIGNAL IN (J13) |
| `mix` | the mixer's output (check point 13) into the HPF |
| `hpf` | the HPF's output (check point 14) into the LPF |
| `hpfext`, `lpfext` | the EXT CUTOFF FREQ jacks (J14, J21), normalled to `eg2vcf` |
| `mg`, `mgsq` | MG triangle (J3), rectangle (J7) |
| `mgkick` | the MG's start-up current (a power-on stand-in for the noise a real one starts from) |
| `lpf` | the LPF's output (check point 16) into the VCA |
| `eg1`, `eg1rev`, `eg1out`, `eg1vco` | EG 1 (IC18b, -7.5 V idle to 0 V), REVERSE OUT (J11), EG-1 OUT (J12), and EG 1 as KLM-128's R1/R2 hand it to VCO 1+2 EXT FREQ's normal |
| `eg2`, `eg2vcf`, `eg2rev` | EG 2 to the VCA, EG 2's TO VCF (the filters' normal), REV OUT (J23) |
| `initgain` | INITIAL GAIN (J22) |
| `sigout` | SIGNAL OUT (J35); `phones` the phones amp's output |
| `noise` | TR3's junction noise into noise.cir (a source the machine makes) |
| `white`, `pink` | noise outputs (J25, J19) |
| `shin`, `shclk`, `shout` | S&H (J2, J5, J8) |
| `mvcain`, `mvcactl`, `mvcaout` | MVCA (J10, J15, J16) |
| `wheel`, `mom` | control wheel (J28), momentary switch (J31) |
| `espin`, `espamp`, `espfilt`, `espfv`, `espenv`, `esptrig` | the ESP's jacks; `esppre`, `espbp` between its netlists |
| `hostin` | the plugin's audio input, the cable from outside |

A jack's switch is a control the board sets: `jack_<name>` (and `<name>_patch` on KLM-128) is 1
with a cable in.

**Checks.** Each netlist runs through `circuitrun` and `tools/spice/compare_circuit.py` against
ngspice on the cases in its `tools/spice/cases/<block>.json` (a list of
`{"name": ..., "netlist": "models.cir,<block>.cir", "args": [circuitrun arguments...]}`), and its
page states the results (largest difference in % of each output's range).

## Pages

- `semiconductors.md`: the device models and the op-amps.
- `keyboard.md`: keyboard ladder, key hold, portamento, trigger detector, power supply.
- `vco.md`: VCO 1 and 2, their CV and modulation, wave shapers, ring modulator, mixer.
- `filters.md`: VC HPF and VC LPF (two KORG35s), their cutoff summers, the VCA, SIGNAL OUT.
- `envelopes.md`: EG 1, EG 2, MG.
- `klm128.md`, `esp.md`: noise, S&H, MVCA, wheel, switch, phones; the ESP.
