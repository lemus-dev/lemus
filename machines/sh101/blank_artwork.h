// The SH-101's blank artwork pack (host/Roms.h, ArtworkSpec::blank): sh101.svg with the panel's
// two mark slots commented out, so it draws nothing. Earlier builds wrote it into the ROM folder;
// now the marks are places (sh101.cpp) and the host takes untouched copies out. The slots are the
// panel's logo boxes (docs/panel.json); the tests check that they match.
#pragma once

namespace sh101 {

inline constexpr const char *kBlankArtwork = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<!--
  sh101.svg: artwork for the panel of ro-mono-101 (Lemus).

  Lemus doesn't include the panel's brand marks. This file is blank: it has a slot for each mark,
  commented out, so it draws nothing. To fill a slot, take out the comment markers around it and
  replace the box in it with your artwork. Uncommented as it is, a slot draws a solid box.

  Units are the panel's millimetres (570 x 310), from its rear-left corner, and artwork is drawn
  where it is, so keep it inside its slot. A mark is the element with the slot's id and everything
  in it: paths, shapes, text or groups, with any transforms. Shapes are filled in the panel's
  print colour; colours and strokes in this file are ignored.

  Lemus writes this file into the ROM folder when the folder has no sh101.svg, and doesn't change
  it after that. If you delete it, it comes back blank. A sh101.svg anywhere in the ROM folder, or
  an SVG in sh101.zip, is used.
-->
<svg xmlns="http://www.w3.org/2000/svg" width="570mm" height="310mm" viewBox="0 0 570 310">

<!-- Slot "roland": x 392.80 to 456.30, y 146.00 to 155.70 (63.50 x 9.70)
<g id="roland">
  <rect x="392.80" y="146.00" width="63.50" height="9.70"/>
</g>
-->

<!-- Slot "sh101": x 461.40 to 548.40, y 134.30 to 155.70 (87.00 x 21.40)
<g id="sh101">
  <rect x="461.40" y="134.30" width="87.00" height="21.40"/>
</g>
-->

</svg>
)svg";

} // namespace sh101
