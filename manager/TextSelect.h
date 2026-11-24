// Selecting text the window draws itself (the details pane), so names, ROM files and checksums can
// be copied: where a wrapped text's characters sit, and the name around a character.
#pragma once

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace lemus::manager {

// Text wrapped to a width, in one font and colour (what the details pane draws).
inline juce::TextLayout layoutOf(const juce::String &text, const juce::Font &font, juce::Colour colour, float width)
{
	juce::AttributedString s;
	s.append(text, font, colour);
	s.setWordWrap(juce::AttributedString::byWord);
	juce::TextLayout layout;
	layout.createLayout(s, width);
	return layout;
}

// Where the characters of a text laid out by layoutOf sit, relative to its top left: each line's
// boundaries between characters, measured from the line's start so a glyph from a fallback font
// (✓) keeps its own width.
class TextSpots {
public:
	TextSpots(const juce::String &text, const juce::Font &font, float width)
	{
		const auto layout = layoutOf(text, font, juce::Colours::white, width);
		for (int n = 0; n < layout.getNumLines(); ++n) {
			const auto &line = layout.getLine(n);
			Line l;
			l.from = juce::jlimit(0, text.length(), line.stringRange.getStart());
			l.to = juce::jlimit(l.from, text.length(), line.stringRange.getEnd());
			while (l.to > l.from && (text[l.to - 1] == '\n' || text[l.to - 1] == '\r'))
				--l.to;
			l.top = line.lineOrigin.y - line.ascent;
			l.bottom = line.lineOrigin.y + line.descent;
			for (int i = l.from; i <= l.to; ++i)
				l.edges.push_back(line.lineOrigin.x + (i == l.from ? 0.0f : juce::GlyphArrangement::getStringWidth(font, text.substring(l.from, i))));
			m_lines.push_back(std::move(l));
		}
	}

	// The boundary between characters nearest a point: on the line under it (above the first, the
	// first; below the last, the last).
	int indexAt(juce::Point<float> p) const
	{
		if (m_lines.empty())
			return 0;
		const Line *line = &m_lines.back();
		for (const auto &l : m_lines)
			if (p.y < l.bottom) {
				line = &l;
				break;
			}
		size_t i = 0;
		while (i + 1 < line->edges.size() && p.x > (line->edges[i] + line->edges[i + 1]) / 2)
			++i;
		return line->from + int(i);
	}

	// What covers characters [from, to): a box on each line they reach.
	std::vector<juce::Rectangle<float>> boxes(int from, int to) const
	{
		std::vector<juce::Rectangle<float>> r;
		for (const auto &l : m_lines) {
			const int a = juce::jmax(from, l.from), b = juce::jmin(to, l.to);
			if (a < b)
				r.push_back({l.edges[size_t(a - l.from)], l.top, l.edges[size_t(b - l.from)] - l.edges[size_t(a - l.from)], l.bottom - l.top});
		}
		return r;
	}

private:
	struct Line {
		int from = 0, to = 0;
		float top = 0, bottom = 0;
		std::vector<float> edges; // to - from + 1 of them
	};
	std::vector<Line> m_lines;
};

// What a double click takes at a boundary: the name the character after it (else before it) is in,
// letters, digits and - _ . + (Roland, TB-303, ro-mono-303, hd44780_a00.bin), less the full stop
// that ends a sentence; any other character alone.
inline juce::Range<int> nameAround(const juce::String &text, int at)
{
	const auto named = [](juce::juce_wchar c) { return juce::CharacterFunctions::isLetterOrDigit(c) || c == '-' || c == '_' || c == '.' || c == '+'; };
	const int length = text.length();
	if (length == 0)
		return {};
	at = juce::jlimit(0, length, at);
	if (at == length || (!named(text[at]) && at > 0 && named(text[at - 1])))
		--at;
	if (!named(text[at]))
		return {at, at + 1};
	int from = at, to = at + 1;
	while (from > 0 && named(text[from - 1]))
		--from;
	while (to < length && named(text[to]))
		++to;
	while (to > from + 1 && text[to - 1] == '.')
		--to;
	while (from < to - 1 && text[from] == '.')
		++from;
	return {from, to};
}

} // namespace lemus::manager
