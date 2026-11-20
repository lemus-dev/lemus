#include "Fonts.h"

#include <BinaryData.h>

namespace fonts {

namespace {
juce::Typeface::Ptr load(const char *data, int size)
{
	return juce::Typeface::createSystemTypefaceFor(data, size_t(size));
}
} // namespace

juce::Font mono(float size, Weight w)
{
	static const auto regular = load(BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
	static const auto medium = load(BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);
	static const auto semi = load(BinaryData::IBMPlexMonoSemiBold_ttf, BinaryData::IBMPlexMonoSemiBold_ttfSize);
	const auto &face = w == Weight::SemiBold ? semi : w == Weight::Medium ? medium : regular;
	return juce::Font(juce::FontOptions(face).withHeight(size));
}

juce::Font ui(float size, bool bold)
{
	return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}

} // namespace fonts
