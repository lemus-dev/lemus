#include "Fonts.h"

#include <TB303Fonts.h>

namespace fonts {

namespace {
juce::Typeface::Ptr load(const char *data, int size)
{
	return juce::Typeface::createSystemTypefaceFor(data, size_t(size));
}
} // namespace

juce::Font mono(float size, Weight w)
{
	static const auto regular = load(TB303Fonts::IBMPlexMonoRegular_ttf, TB303Fonts::IBMPlexMonoRegular_ttfSize);
	static const auto medium = load(TB303Fonts::IBMPlexMonoMedium_ttf, TB303Fonts::IBMPlexMonoMedium_ttfSize);
	static const auto semi = load(TB303Fonts::IBMPlexMonoSemiBold_ttf, TB303Fonts::IBMPlexMonoSemiBold_ttfSize);
	const auto &face = w == Weight::SemiBold ? semi : w == Weight::Medium ? medium : regular;
	return juce::Font(juce::FontOptions(face).withHeight(size));
}

} // namespace fonts
