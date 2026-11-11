// The TR-808's blank artwork pack (host/Roms.h, ArtworkSpec::blank): tr808.svg with the panel's
// four mark slots commented out, so it draws nothing. Earlier versions wrote it into the ROM folder
// when the folder had no tr808.svg; now that the 808 has places, the host only recognises untouched
// copies and takes them out, so the text below stays as it was. The slots are the panel's logo
// boxes (docs/panel.json); the tests check that they match.
#pragma once

namespace tr808 {

inline constexpr const char *kBlankArtwork = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<!--
  tr808.svg: artwork for the panel of the TR-808 in Lemus.

  Lemus doesn't include the panel's brand marks. This file is blank: it has a slot for each mark,
  commented out, so it draws nothing. To fill a slot, take out the comment markers around it and
  replace the box in it with your artwork. Uncommented as it is, a slot draws a solid box.

  Units are the panel's millimetres (508 x 305), from its top-left corner, and artwork is drawn
  where it is, so keep it inside its slot. A mark is the element with the slot's id and everything
  in it: paths, shapes, text or groups, with any transforms. Shapes are filled in the panel's
  print colour; colours and strokes in this file are ignored.

  Lemus writes this file into the ROM folder when the folder has no tr808.svg, and doesn't change
  it after that. If you delete it, it comes back blank. A tr808.svg anywhere in the ROM folder, or
  an SVG in tr808.zip, is used.
-->
<svg xmlns="http://www.w3.org/2000/svg" width="508mm" height="305mm" viewBox="0 0 508 305">

<!-- Slot "roland": x 330.80 to 411.20, y 31.50 to 43.70 (80.40 x 12.20)
<g id="roland">
  <rect x="330.80" y="31.50" width="80.40" height="12.20"/>
</g>
-->

<!-- Slot "rhythm_composer": x 262.40 to 367.30, y 165.60 to 179.00 (104.90 x 13.40)
<g id="rhythm_composer">
  <rect x="262.40" y="165.60" width="104.90" height="13.40"/>
</g>
-->

<!-- Slot "tr808": x 381.50 to 412.20, y 168.80 to 175.80 (30.70 x 7.00)
<g id="tr808">
  <rect x="381.50" y="168.80" width="30.70" height="7.00"/>
</g>
-->

<!-- Slot "computer_controlled": x 350.60 to 412.10, y 179.90 to 187.35 (61.50 x 7.45)
<g id="computer_controlled">
  <rect x="350.60" y="179.90" width="61.50" height="7.45"/>
</g>
-->

</svg>
)svg";

} // namespace tr808
