#pragma once

#include <juce_graphics/juce_graphics.h>

namespace ms20::fonts {

// IBM Plex Mono for the bar, the key list and the stand-in legends: size in px, weight 0 regular,
// 1 medium, 2 semibold.
juce::Font mono(float size, int weight = 0);

// Nimbus Sans Narrow, the panel print's stand-in (fonts/README.md).
juce::Font panel(float size);

} // namespace ms20::fonts
