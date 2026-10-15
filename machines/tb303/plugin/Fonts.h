// Fonts for the plugin bar, help and tooltips. IBM Plex Mono is bundled (SIL OFL).
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace fonts {

enum class Weight { Regular, Medium, SemiBold };

juce::Font mono(float size, Weight w = Weight::Regular);

} // namespace fonts
