# The boards (SERVICE)

The facts a SERVICE view needs, as the TR-909's (`machines/tr909/docs/boards.md`): with the top
panel off, the boards' component sides, every part where the service notes' board drawings put it,
the knobs on their pots and the keys on their switches. The sources are the service notes' board
pages: p.10 (the switch board; the main board's component side, which draws its edge), p.11 (the
main board's foil side; jack board A), p.12 (the power supply, jack board B, the voicing board's
component and foil sides) and p.1 (a photograph of the opened unit). SERVICE draws it
(`plugin/HardwareService.cpp`, the TR-909's ported): every part with pads, the copper generated from
the pads; the power supply, out of sight under the main board, isn't drawn.

## What is where

- `docs/boards.json`: the facts, in panel millimetres (the top view's frame of `docs/panel.json`,
  508 x 305 mm; every board as it lies in the unit seen from above, through the panel). Its keys are
  the 909's: each board's outline, its stacking level, the page-to-panel transform it was placed
  with (from the 400-dpi scan of its page), its pads (`pads`, by board: x, y, kind) and the parts
  placed on it (`parts`: designator, board, pads with pin 1 first where `pin1` says so, how it was
  placed; `printed` where the drawing names a part otherwise).
- The boards, and their levels (0 is nearest the panel; seen from below, a higher level hides a
  lower one):

| Board | Id | Level | Where (panel mm) |
|---|---|---|---|
| SWITCH BOARD OP3116-090 | `switch` | 0 | under the 16 step keys, x 117-438, y 226-283 |
| MAIN BOARD OP3116-130 | `main` | 1 | hung on its 15 pots, x 37-358, y 26-276: everything from the MODE switch to CP, with a corner cut away at the rear left for the POWER switch |
| VOICING BOARD VG3116-140 | `voicing` | 1 | beside it on its 8 pots, x 355-484, y 26-275: CB to CH, MASTER VOLUME, TAP, I/F VARIATION |
| JACK BOARD A OP3116-100 | `jacka` | 2 | at the rear panel, x 117-212: TRIGGER OUT CB, CP, AC and the two PEDAL SW jacks |
| JACK BOARD B OP3116-110 | `jackb` | 2 | at the rear panel, x 213-458: the eleven MULTI OUTs and MASTER OUT HI, LO |
| POWER SUPPLY PS3116-051/-054 | `power` | 3 | on the chassis floor, about x 145-287, y 63-181 (assumed) |

The main and voicing boards hang side by side at one depth (both on the same kind of pot) and meet
at x = 356 mm (the two fits overlap them by up to 2 mm, within the drawings' accuracy). The switch
board's front strip covers both. The jack boards hang from the rear panel below them, the power
supply and the transformer stand on the chassis floor.

The copper is to be generated from the pads, as the 909's is (`makeCopper`): nothing here traces
Roland's artwork.

## Placement

The pages are not 1:1, and not all at one scale: the main board's drawing is at 0.94 of full size
(the 42-pin CPU's pitch, `panel.md`), the switch board's and the small boards' smaller still, the
voicing board's a little larger. Each board is placed by fitting features of its drawing to the
panel; the scale comes out of the fit (in `panel.json`'s 508-mm frame, 5 % larger than the unit).

| Board | Placed by | Fit |
|---|---|---|
| Main | its 15 small pots (the midpoints of their two mounting lugs, p.11, the page mirrored) on the knob centres of `panel.json`, an affine fit | 0.22 mm rms (max 0.39) |
| Voicing | its 8 pots (VR2-VR8 the centres of their drawn rings, VR1 its shaft, p.12) on the knob centres, an affine fit | 0.20 mm rms (max 0.34) |
| Switch | its 16 key switches' LED windows (p.10) on the step LEDs, a similarity fit (one scale, turned 90 degrees) | 0.35 mm rms (max 0.54) |
| Jack A | x: its five jacks on the rear panel's (`panel.json` rear_panel, +-2 mm there), one scale for both axes; the jack mouths at y = 2 mm (assumed) | 0.49 mm rms |
| Jack B | x: its first eleven jacks on the rear panel's; as jack A | 0.66 mm rms |
| Power supply, transformer | **assumed**: nothing of the unit to fit them on. Where p.1's photograph shows them on the chassis floor (the power board left of centre, the transformer at the left), the power board at jack board A's drawing scale | - |

Checks on the main board's fit, on parts it wasn't fitted to: the rotary switches' pin rings land
0.70-0.85 mm from their panel centres (INSTRUMENT-SELECT, AUTO FILL IN, MODE), the FINE pot's lugs
0.8 mm from its knob, and the five voice switches SW8-SW12 under their slide switches. The main
board's edge is drawn only on p.10; p.10 was registered on p.11 by 750 pads found on both (2.8 px
rms).

Jack board B's two MASTER OUT jacks: by p.13, J12 is HI and J13 LO, and J12 sits next to CH's J11;
the rear panel (photographs of real units) has LO next to CH and HI at the end, as `panel.json` has it.
Placed by place: the jack at x 429 is LO, whatever p.13 numbers it.

## Pads

Found on the scans:

- main board (p.11, foil side): the pads are black discs with a white drill hole; a pad is a local
  maximum of the distance transform of the solid ink (the halftone ground removed) with a small
  round hole in it; the IC pads, small ovals, by matching an oval and keeping those in rows at
  0.1". Letters' counters ("0", "8", "R") are told from drill holes by their size and shape.
- voicing board (p.12, component side): the pads are drawn as open circles, found by a Hough
  transform once the halftone is removed; the jumper wires' ends are filled dots.
- switch board (p.10, component side): the copper under each key switch, averaged over the 16 keys
  aligned on their LED windows, gives the switch's two contact pins and its LED's two leads.

Some real pads are missed and a few false ones remain; every part's own pads join its board's pads.

## Parts

A part is its designator and its pads. From geometry: the main board's pots VR3-VR17 (three pins
176 px below the midpoint of their lugs, pin 1 the one the foil side marks "1"), the voicing board's
pots VR1-VR8 and its op-amps IC1-IC7 (their pads at the corners of the drawn bodies, at 0.1", every
notch at the top), the switch board's sixteen key switches and LEDs. The rest were read off the
drawings tile by tile (the pages cut into tiles with the found pads numbered, each designator read
with its pads; the main board on the foil side's printed legend, with the component side beside it
to show which pads a part's body joins; the voicing board on its component side, with its foil side
for the printed designators), then checked against the circuit: a part whose circuit neighbours (the
parts on its nodes, rails aside, in `core/netlists`) all lie far from it was looked at again, and a
missing part was looked for where its neighbours are.

Names: the switch board's keys are printed S1-S16 (`printed`); they are SW1-SW16 here, and their
LEDs LED1-LED16 (the schematic numbers both 1-16). The switch board's 12-way connector carries no
designator (CN1 here), nor do jack board B's two terminal strips (CN1, CN2 there, `printed` with
their part numbers). The test points TP1-TP3 and TP5 (main board) and TP6, TP7 (voicing board) are
rings drawn round another part's pad, and share it. On the voicing board many parts are drawn as
bodies that hide their pads (standing resistors, the flat transistors Q1-Q8, the electrolytics):
their pads are estimated from the body (`how` says "pads estimated").

What a part is in the circuit comes from the netlists, not the drawing. The main and voicing boards
number their parts independently (both have an R1), so a part is its board and its designator.

1183 parts (2026-11-05):

| Board | Parts | Of the netlists' parts on it | Within 20 mm of a circuit neighbour |
|---|---|---|---|
| Main | 767 | 448 of 448 | 446 of 455 checked (median 4.1 mm) |
| Voicing | 356 (145 with pads estimated) | 265 of 265 | 254 of 259 checked (median 4.1 mm) |
| Switch | 43 (32 from geometry) | - | - |
| Jack B | 17 | 15 of 15 (R1-R14, C1 of the mixer's netlist) | - |
| Jack A, power supply | none: outline and placement only | - | - |

The parts farther than 20 mm from every circuit neighbour were each looked at again and are as
drawn: the pots, whose pins run far across the board (the AC LEVEL pot VR3 is 225 mm from R114 by
the accent circuit at the front), and output resistors and capacitors at the ends of long tracks.
The netlists' R390 (a 0.1 ohm tie the model adds) isn't a board part. The main board's parts the
netlists don't draw (the logic, the switches, the supply decoupling) are placed as well; a few
designators on it weren't found: TP4 (on one of R123's pads), C16, D32, D72-D75.

The drawings' own slips are named after the schematic, as the review settled them: on the main
board the component side letters C153 as "C159", and C125's middle digit is hidden; on the voicing
board one resistor is lettered "R23" by hand and printed R122 on the foil side (it is R122, the
circuit says).
