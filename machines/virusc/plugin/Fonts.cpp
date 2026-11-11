#include "Fonts.h"

#include "VirusCFonts.h"

namespace virusc::fonts {

namespace {
juce::Typeface::Ptr load(const char *data, int size) { return juce::Typeface::createSystemTypefaceFor(data, size_t(size)); }

// The cap height of a face as a share of its font height.
float capRatio(const juce::Typeface::Ptr &face)
{
	juce::GlyphArrangement ga;
	ga.addLineOfText(juce::Font(juce::FontOptions(face).withHeight(100.0f)), "H", 0, 0);
	juce::Path ink;
	ga.createPath(ink);
	const float h = ink.getBounds().getHeight() / 100.0f;
	return h > 0 ? h : 0.7f;
}
} // namespace

juce::Font legend(float capHeight)
{
	static const auto face = load(VirusCFonts::MichromaRegular_ttf, VirusCFonts::MichromaRegular_ttfSize);
	static const float ratio = capRatio(face);
	return juce::Font(juce::FontOptions(face).withHeight(capHeight / ratio));
}

juce::Font mono(float size, int weight)
{
	static const auto regular = load(VirusCFonts::IBMPlexMonoRegular_ttf, VirusCFonts::IBMPlexMonoRegular_ttfSize);
	static const auto medium = load(VirusCFonts::IBMPlexMonoMedium_ttf, VirusCFonts::IBMPlexMonoMedium_ttfSize);
	static const auto semibold = load(VirusCFonts::IBMPlexMonoSemiBold_ttf, VirusCFonts::IBMPlexMonoSemiBold_ttfSize);
	return juce::Font(juce::FontOptions(weight == 2 ? semibold : weight == 1 ? medium : regular).withHeight(size));
}

} // namespace virusc::fonts
