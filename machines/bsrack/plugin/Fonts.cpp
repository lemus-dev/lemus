#include "Fonts.h"

#include "BSRackFonts.h"

namespace bsrack::fonts {

namespace {
juce::Typeface::Ptr load(const char *data, int size) { return juce::Typeface::createSystemTypefaceFor(data, size_t(size)); }
} // namespace

juce::Font mono(float size, int weight)
{
	static const auto regular = load(BSRackFonts::IBMPlexMonoRegular_ttf, BSRackFonts::IBMPlexMonoRegular_ttfSize);
	static const auto medium = load(BSRackFonts::IBMPlexMonoMedium_ttf, BSRackFonts::IBMPlexMonoMedium_ttfSize);
	static const auto semibold = load(BSRackFonts::IBMPlexMonoSemiBold_ttf, BSRackFonts::IBMPlexMonoSemiBold_ttfSize);
	return juce::Font(juce::FontOptions(weight == 2 ? semibold : weight == 1 ? medium : regular).withHeight(size));
}

juce::Typeface::Ptr legend()
{
	static const auto bold = load(BSRackFonts::LiberationSansBold_ttf, BSRackFonts::LiberationSansBold_ttfSize);
	return bold;
}

} // namespace bsrack::fonts
