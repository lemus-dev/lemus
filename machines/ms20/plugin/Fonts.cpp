#include "Fonts.h"

#include "Ms20Fonts.h"

namespace ms20::fonts {

namespace {
juce::Typeface::Ptr load(const char *data, int size) { return juce::Typeface::createSystemTypefaceFor(data, size_t(size)); }
} // namespace

juce::Font mono(float size, int weight)
{
	static const auto regular = load(Ms20Fonts::IBMPlexMonoRegular_ttf, Ms20Fonts::IBMPlexMonoRegular_ttfSize);
	static const auto medium = load(Ms20Fonts::IBMPlexMonoMedium_ttf, Ms20Fonts::IBMPlexMonoMedium_ttfSize);
	static const auto semibold = load(Ms20Fonts::IBMPlexMonoSemiBold_ttf, Ms20Fonts::IBMPlexMonoSemiBold_ttfSize);
	return juce::Font(juce::FontOptions(weight == 2 ? semibold : weight == 1 ? medium : regular).withHeight(size));
}

juce::Font panel(float size)
{
	static const auto narrow = load(Ms20Fonts::NimbusSansNarrowRegular_ttf, Ms20Fonts::NimbusSansNarrowRegular_ttfSize);
	return juce::Font(juce::FontOptions(narrow).withHeight(size));
}

} // namespace ms20::fonts
