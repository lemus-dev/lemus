#include "Fonts.h"

#include "QVGTFonts.h"

namespace qvgt::fonts {

namespace {
juce::Typeface::Ptr load(const char *data, int size) { return juce::Typeface::createSystemTypefaceFor(data, size_t(size)); }
} // namespace

juce::Font mono(float size, int weight)
{
	static const auto regular = load(QVGTFonts::IBMPlexMonoRegular_ttf, QVGTFonts::IBMPlexMonoRegular_ttfSize);
	static const auto medium = load(QVGTFonts::IBMPlexMonoMedium_ttf, QVGTFonts::IBMPlexMonoMedium_ttfSize);
	static const auto semibold = load(QVGTFonts::IBMPlexMonoSemiBold_ttf, QVGTFonts::IBMPlexMonoSemiBold_ttfSize);
	return juce::Font(juce::FontOptions(weight == 2 ? semibold : weight == 1 ? medium : regular).withHeight(size));
}

} // namespace qvgt::fonts
