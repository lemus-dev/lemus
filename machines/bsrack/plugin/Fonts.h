#pragma once

#include <juce_graphics/juce_graphics.h>

namespace bsrack::fonts {

// IBM Plex Mono for the bar and the key list: size in px, weight 0 regular, 1 medium, 2 semibold.
juce::Font mono(float size, int weight = 0);

// The panel's legends: screen-printed Helvetica-style bold capitals, stood in for by Liberation
// Sans Bold (fonts/, SIL OFL 1.1; Liberation Fonts 2.1.5, LiberationSans-Bold.ttf SHA1
// 4e5ee6e5de4d35337f7d14fa43222908e28e2d8d).
juce::Typeface::Ptr legend();

} // namespace bsrack::fonts
