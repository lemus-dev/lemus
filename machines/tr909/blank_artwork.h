// The TR-909's blank artwork pack (host/Roms.h, ArtworkSpec::blank): tr909.svg with the panel's
// three mark slots commented out, so it draws nothing. The host writes it into the ROM folder when the
// folder has no tr909.svg. The slots are the panel's logo boxes (docs/panel.json).
#pragma once

namespace tr909 {

inline constexpr const char *kBlankArtwork = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<!--
  tr909.svg: artwork for the panel of the TR-909 in Lemus.

  Lemus doesn't include the panel's brand marks. This file is blank: it has a slot for each mark,
  commented out, so it draws nothing. To fill a slot, take out the comment markers around it and
  replace the box in it with your artwork. Uncommented as it is, a slot draws a solid box.

  Units are the panel's millimetres (486 x 279.49), from its top-left corner, and artwork is drawn
  where it is, so keep it inside its slot. A mark is the element with the slot's id and everything
  in it: paths, shapes, text or groups, with any transforms. Shapes are filled in the panel's
  print colour; colours and strokes in this file are ignored.

  Lemus writes this file into the ROM folder when the folder has no tr909.svg, and doesn't change
  it after that. If you delete it, it comes back blank. A tr909.svg anywhere in the ROM folder, or
  an SVG in tr909.zip, is used.
-->
<svg xmlns="http://www.w3.org/2000/svg" width="486mm" height="279.49mm" viewBox="0 0 486 279.49">

<!-- Slot "roland": x 34.72 to 115.22, y 14.22 to 26.85 (80.50 x 12.63)
<g id="roland">
  <rect x="34.72" y="14.22" width="80.50" height="12.63"/>
</g>
-->

<!-- Slot "tr909": x 34.90 to 142.78, y 74.90 to 90.52 (107.88 x 15.62)
<g id="tr909">
  <rect x="34.90" y="74.90" width="107.88" height="15.62"/>
</g>
-->

<!-- Slot "rhythm_composer": x 305.90 to 441.40, y 83.50 to 90.38 (135.50 x 6.88)
<g id="rhythm_composer">
  <rect x="305.90" y="83.50" width="135.50" height="6.88"/>
</g>
-->

</svg>
)svg";

} // namespace tr909
