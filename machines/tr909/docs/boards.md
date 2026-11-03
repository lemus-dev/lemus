# The boards (SERVICE)

SERVICE on the plugin bar takes the top panel off, as the 303's does: the voicing board where it
hangs under the panel on its pots, the switch board folded out in front of it on its harness, their
component sides up, every part where the service notes' board drawings put it (p.9 switch board
73133080, p.10 voicing board 73133090; p.12 has the multi-jack, DIN jack and power supply boards,
which lie under the voicing board and aren't shown). The parts are drawn as what they are: the
resistors banded by their netlist values, the capacitors as film, ceramic, tantalum or electrolytic
by theirs, glass diodes, TO-92 transistors, the ICs with their parts-list markings, the clap's
trimmer TM turning with its parameter. The knobs stay on their pots and the keys on their
switches, as they do when the panel comes off, and work; the LEDs and the display run. The boards
are single-sided, so their copper (on the other side) shows faintly through, generated; the
transformer stands in the voicing board's cut-out.

## What is where

- `docs/boards.json`: the facts, in panel millimetres (the top view's frame of `docs/panel.json`; the
  view mirrors x, as the unit lies face down): each board's outline, its stacking level (from the
  panel down: switch 0, voicing 1, multi-jack 2, DIN 3, power 4: a higher level hides a lower one
  from below), the page-to-panel transform it was placed with, its pads (`pads`, by board: x, y,
  kind), and the parts placed on it (`parts`: designator, board, pads). Embedded in the plugin
  (`TR909Boards`).
- The copper is generated, not traced: the real tracks are Roland's artwork. `makeCopper`
  (`plugin/HardwareService.cpp`) puts a land on every pad of `pads` (round, or oval as its kind
  says) and joins each pad to its nearest neighbours of other parts, at most four within 16 mm, as
  a spanning forest, shortest first, routed at 0/45/90 degrees in three widths (0.5, 0.65, 0.8 mm),
  as the 303's SERVICE draws its copper. The nets are not the board's; it looks like a board's
  underside, nothing more. Until 2026-11-02 the copper was traced from the foil views (p.9, p.10,
  p.12) into the private artwork pack; that pack no longer has it.

## Placement

The pages are 1:1 at 400 dpi (15.725 px/mm across, about 1.2 % taller than wide, as `panel.md`
found).

| Board | Placed by | Fit |
|---|---|---|
| Voicing | its 27 small pots (each the midpoint of its two mounting slots) on the knob centres of `panel.json`, an affine fit | 0.15 mm rms |
| Switch | mirrored at 1:1; the step keys' 23.0 mm pitch, SW4's outline on step key 4 | by construction |
| Multi-jack | x: its twelve jacks on the rear panel's (`panel.json` rear_panel); its rear edge at the rear panel | 0.34 mm rms |
| DIN jack | x: its four DIN sockets and four jacks on the rear's; its rear edge at the rear panel | 0.44 mm rms |
| Power supply, transformer | **assumed**: no feature of the unit to fit them on. The transformer stands in the voicing board's cut-out at the rear left (which is there to clear something tall), the power board in front of it, by the POWER switch and AC inlet | - |

The heights are read from the rear panel: the MULTI OUT row is above the DIN row. The view shows
the voicing and switch boards; the others stay in `boards.json` with their parts.

## Pads

Pads are found on the scan (scratch tools, not kept): every round pad has a small solid crescent,
its drill mark, inside a white disc in its dotted ring; the IC pads are small ovals; the jumper
wires end on pads drawn dark. Three detectors (the crescents, holes enclosed by copper, templates of
a round, an oval and a dark pad) are merged; a pad two of them agree on, or the template alone
(which is reliable), is kept: 3149 on the voicing board. Some real pads are missed (their copper
shows without a joint) and a few false ones remain; the parts' own pads add to them.

## Parts

A part is its designator and its pads. The 27 small pots come from geometry: each pot's slots give
its centre, its knob (the registration) its VR number (the netlists' pots by voice and control),
and its three pins are 158 px (10 mm) below the slots, 57 px (3.6 mm) apart. The rest were read off
the drawing, designator by designator (2026-10-23): the pages cut into tiles of 600 x 450 px with
the found pads numbered, each tile read for its designators and their pads, then checked against the
circuit: a part whose circuit neighbours (the parts on its nodes, rails aside) all lie far from it
was looked at again, and each missing part was looked for where its neighbours are. 1227 parts:
voicing board 947 (827 of the 829 parts its netlists draw; R114 and R115 share one blob of copper
whose pads can't be told apart), switch board 185, multi-jack 61, DIN jack 14, power supply 20.
The drawings' own slips are kept as drawn and named after the schematic: the board's legend prints
C86 for the schematic's C85, C49 for its C40 (p.11 uses C40 twice), and R603 for the DIN board's
R608; C600 (TRIG OUT, SN 415300 up) isn't drawn on any board. The M5218L op-amps are single-row
parts with a spare hole above pin 1. The parts' pads that the detectors missed join the boards'
pads (3679 on the voicing board).

What a part is in the circuit comes from the netlists, not the drawing: `VoiceBoard::parts()` lists
every part they draw by its designator, each pin with its node, so the scope probes a pin by the
netlist (`VoiceBoard::setProbe`), and a part that isn't placed on the board yet can still be probed
from the scope's menu.

## Colours

No photograph of a real 909's boards was found: they are drawn green, as the 303's, with a white
legend (the boards' names, and the designators beside the parts close up: where the real board
prints them isn't known). Parts the drawings place but the netlists don't value (the decoupling
capacitors, the logic) get plausible bodies: a diode's cathode band and a transistor's flat face
one way, as the drawing doesn't say.
