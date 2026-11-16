#pragma once

#include <juce_graphics/juce_graphics.h>

namespace qvgt::fonts {

// IBM Plex Mono for the panel's stand-in legends and the bar: size in px, weight 0 regular, 1
// medium, 2 semibold.
juce::Font mono(float size, int weight = 0);

} // namespace qvgt::fonts
