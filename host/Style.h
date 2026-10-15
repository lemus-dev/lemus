// The look of Lemus's own screens (the machine picker, the bar above a machine, the About box):
// Tokyo Night colours, as Onyx Todo uses them, and JetBrains Mono in Regular and Bold. The
// machines' panels keep the look of the hardware.
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace lemus::style {

namespace colours {
const juce::Colour ground{0xff1a1b26};    // the page
const juce::Colour panel{0xff16161e};     // side panes, footers, pop-ups
const juce::Colour well{0xff292e42};      // cards, fields, buttons
const juce::Colour selection{0xff33467c}; // the selected row
const juce::Colour ink{0xffc0caf5};       // titles and text
const juce::Colour inkSoft{0xffa9b1d6};   // secondary text
const juce::Colour muted{0xff737aa2};     // labels and details the text already carries
const juce::Colour rule{0x73565f89};      // 1px lines
const juce::Colour accent{0xff7aa2f7};    // the one colour: the Load button, links
const juce::Colour title{0xffa9c1ff};     // the product's name
const juce::Colour onAccent{0xff16161e};  // text on an accent fill
const juce::Colour warn{0xffe0af68};      // running without an optional ROM
const juce::Colour ok{0xff9ece6a};        // ready, found
} // namespace colours

// JetBrains Mono at a CSS-style size (point height).
juce::Font mono(float size, bool bold = false);

// A label in small bold capitals, tracked out, so it never reads as a heading.
void drawLabel(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> area, juce::Colour colour = colours::muted);

} // namespace lemus::style
