#include "Fonts.h"

#include <SH101Fonts.h>

namespace sh101::fonts {

namespace {
juce::Typeface::Ptr load(const char *data, int size)
{
	return juce::Typeface::createSystemTypefaceFor(data, size_t(size));
}
} // namespace

juce::Font mono(float size, Weight w)
{
	static const auto regular = load(SH101Fonts::IBMPlexMonoRegular_ttf, SH101Fonts::IBMPlexMonoRegular_ttfSize);
	static const auto medium = load(SH101Fonts::IBMPlexMonoMedium_ttf, SH101Fonts::IBMPlexMonoMedium_ttfSize);
	static const auto semi = load(SH101Fonts::IBMPlexMonoSemiBold_ttf, SH101Fonts::IBMPlexMonoSemiBold_ttfSize);
	const auto &face = w == Weight::SemiBold ? semi : w == Weight::Medium ? medium : regular;
	return juce::Font(juce::FontOptions(face).withHeight(size));
}

} // namespace sh101::fonts
