#pragma once

#include <juce_graphics/juce_graphics.h>

namespace virusc::fonts {

// The panel's legends: Michroma (SIL OFL 1.1, fonts/), the nearest free face to the panel's extended
// geometric capitals. `capHeight` in the caller's units.
juce::Font legend(float capHeight);
// IBM Plex Mono for the bar: size in px, weight 0 regular, 1 medium, 2 semibold.
juce::Font mono(float size, int weight = 0);

} // namespace virusc::fonts
