#include "Style.h"

#include "LemusFonts.h"

namespace lemus::style {

juce::Font mono(float size, bool bold)
{
	static const auto regular =
		juce::Typeface::createSystemTypefaceFor(LemusFonts::JetBrainsMonoRegular_ttf, size_t(LemusFonts::JetBrainsMonoRegular_ttfSize));
	static const auto heavy =
		juce::Typeface::createSystemTypefaceFor(LemusFonts::JetBrainsMonoBold_ttf, size_t(LemusFonts::JetBrainsMonoBold_ttfSize));
	return juce::Font(juce::FontOptions(bold ? heavy : regular).withPointHeight(size));
}

void drawLabel(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> area, juce::Colour colour)
{
	g.setColour(colour);
	g.setFont(mono(12.0f, true).withExtraKerningFactor(0.06f));
	g.drawText(text.toUpperCase(), area, juce::Justification::centredLeft, false);
}

} // namespace lemus::style
