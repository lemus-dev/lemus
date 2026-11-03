#include "Fonts.h"

#include <TR909Fonts.h>

namespace tr909::fonts {

namespace {
juce::Typeface::Ptr load(const char *data, int size)
{
	return juce::Typeface::createSystemTypefaceFor(data, size_t(size));
}
} // namespace

juce::Font mono(float size, Weight w)
{
	static const auto regular = load(TR909Fonts::IBMPlexMonoRegular_ttf, TR909Fonts::IBMPlexMonoRegular_ttfSize);
	static const auto medium = load(TR909Fonts::IBMPlexMonoMedium_ttf, TR909Fonts::IBMPlexMonoMedium_ttfSize);
	static const auto semi = load(TR909Fonts::IBMPlexMonoSemiBold_ttf, TR909Fonts::IBMPlexMonoSemiBold_ttfSize);
	const auto &face = w == Weight::SemiBold ? semi : w == Weight::Medium ? medium : regular;
	return juce::Font(juce::FontOptions(face).withHeight(size));
}

} // namespace tr909::fonts
