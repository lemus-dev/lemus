// The TB-303's blank artwork pack (host/Roms.h, ArtworkSpec::blank): tb303.svg with the panel's
// four mark slots commented out, so it draws nothing. The host writes it into the ROM folder when
// the folder has no tb303.svg. The slots are the panel's logo boxes (docs/panel.json); the tests
// check that they match.
#pragma once

namespace tb303 {

inline constexpr const char *kBlankArtwork = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<!--
  tb303.svg: artwork for the panel of ro-mono-303 (Lemus).

  Lemus doesn't include the panel's brand marks. This file is blank: it has a slot for each mark,
  commented out, so it draws nothing. To fill a slot, take out the comment markers around it and
  replace the box in it with your artwork. Uncommented as it is, a slot draws a solid box.

  Units are the panel's millimetres (300 x 142), from its top-left corner, and artwork is drawn
  where it is, so keep it inside its slot. A mark is the element with the slot's id and everything
  in it: paths, shapes, text or groups, with any transforms. Shapes are filled in the panel's
  print colour; colours and strokes in this file are ignored.

  Lemus writes this file into the ROM folder when the folder has no tb303.svg, and doesn't change
  it after that. If you delete it, it comes back blank. A tb303.svg anywhere in the ROM folder, or
  an SVG in tb303.zip, is used.
-->
<svg xmlns="http://www.w3.org/2000/svg" width="300mm" height="142mm" viewBox="0 0 300 142">

<!-- Slot "roland": x 13.34 to 65.09, y 14.09 to 21.45 (51.75 x 7.36)
<g id="roland">
  <rect x="13.34" y="14.09" width="51.75" height="7.36"/>
</g>
-->

<!-- Slot "bass_line": x 226.80 to 283.52, y 14.27 to 21.45 (56.72 x 7.18)
<g id="bass_line">
  <rect x="226.80" y="14.27" width="56.72" height="7.18"/>
</g>
-->

<!-- Slot "tb303": x 207.63 to 236.37, y 44.48 to 50.56 (28.74 x 6.08)
<g id="tb303">
  <rect x="207.63" y="44.48" width="28.74" height="6.08"/>
</g>
-->

<!-- Slot "computer_controlled": x 179.83 to 236.37, y 52.58 to 59.02 (56.54 x 6.44)
<g id="computer_controlled">
  <rect x="179.83" y="52.58" width="56.54" height="6.44"/>
</g>
-->

</svg>
)svg";

} // namespace tb303
