#include "Fonts.h"

#include "QVFonts.h"

namespace qv::fonts {

namespace {
juce::Typeface::Ptr load(const char *data, int size) { return juce::Typeface::createSystemTypefaceFor(data, size_t(size)); }
} // namespace

juce::Font mono(float size, int weight)
{
	static const auto regular = load(QVFonts::IBMPlexMonoRegular_ttf, QVFonts::IBMPlexMonoRegular_ttfSize);
	static const auto medium = load(QVFonts::IBMPlexMonoMedium_ttf, QVFonts::IBMPlexMonoMedium_ttfSize);
	static const auto semibold = load(QVFonts::IBMPlexMonoSemiBold_ttf, QVFonts::IBMPlexMonoSemiBold_ttfSize);
	return juce::Font(juce::FontOptions(weight == 2 ? semibold : weight == 1 ? medium : regular).withHeight(size));
}

} // namespace qv::fonts
