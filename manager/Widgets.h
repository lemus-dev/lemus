// The manager's controls, in the look of Lemus's own screens (host/Style.h): the same pill and
// check box the plugins' bar and pop-ups draw (host/LemusEditor.cpp), and text helpers.
#pragma once

#include "host/Style.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lemus::manager {

namespace col = lemus::style::colours;
using lemus::style::mono;

const juce::Colour kError{0xfff7768e}; // a failure

// Every button: plain on well, primary filled with the accent, outline tinted.
class Pill final : public juce::Button {
public:
	enum class Kind { Plain, Primary, Outline };

	explicit Pill(const juce::String &text = {}, Kind kind = Kind::Plain, juce::Colour tint = col::accent, float size = 12.0f)
		: Button(text), m_kind(kind), m_tint(tint), m_size(size)
	{
		setMouseCursor(juce::MouseCursor::PointingHandCursor);
	}

	int idealWidth() const { return juce::GlyphArrangement::getStringWidthInt(font(), getButtonText()) + int(m_size * 2.4f); }
	void setStyle(Kind kind, juce::Colour tint = col::accent)
	{
		m_kind = kind;
		m_tint = tint;
		repaint();
	}

	void paintButton(juce::Graphics &g, bool over, bool down) override
	{
		const auto r = getLocalBounds().toFloat().reduced(0.5f);
		const float radius = r.getHeight() / 2;
		const bool on = isEnabled();
		if (m_kind == Kind::Outline) {
			g.setColour(m_tint.withAlpha(down ? 0.28f : over ? 0.18f : 0.1f));
			g.fillRoundedRectangle(r, radius);
			g.setColour(m_tint.withAlpha(on ? 0.6f : 0.3f));
			g.drawRoundedRectangle(r, radius, 1.0f);
			g.setColour(on ? m_tint : col::muted);
		} else {
			auto fill = m_kind == Kind::Primary && on ? m_tint : col::well;
			if (on && down)
				fill = fill.darker(0.2f);
			else if (on && over)
				fill = fill.brighter(0.12f);
			g.setColour(fill);
			g.fillRoundedRectangle(r, radius);
			g.setColour(!on ? col::muted : m_kind == Kind::Primary ? col::onAccent : col::ink);
		}
		g.setFont(font());
		g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
	}

private:
	juce::Font font() const { return mono(m_size, m_kind == Kind::Primary); }

	Kind m_kind;
	juce::Colour m_tint;
	float m_size;
};

// A check box with its label. An optional second label sits after the first in ink-soft.
class Check final : public juce::Button {
public:
	explicit Check(const juce::String &text = {}, const juce::String &detail = {}) : Button(text), m_detail(detail)
	{
		setClickingTogglesState(true);
		setMouseCursor(juce::MouseCursor::PointingHandCursor);
	}

	void setLabelWidth(int w) { m_labelWidth = w; }

	void paintButton(juce::Graphics &g, bool over, bool) override
	{
		const auto box = getLocalBounds().removeFromLeft(getHeight()).toFloat().withSizeKeepingCentre(16, 16);
		g.setColour(getToggleState() ? col::accent : over ? col::well.brighter(0.25f) : col::well.brighter(0.12f));
		g.fillRoundedRectangle(box, 4.0f);
		if (getToggleState()) {
			juce::Path tick;
			tick.startNewSubPath(box.getX() + 4, box.getCentreY());
			tick.lineTo(box.getX() + 7, box.getBottom() - 4.5f);
			tick.lineTo(box.getRight() - 4, box.getY() + 4.5f);
			g.setColour(col::onAccent);
			g.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
		}
		auto text = getLocalBounds().withTrimmedLeft(getHeight() + 4);
		if (m_detail.isEmpty()) {
			g.setColour(col::inkSoft);
			g.setFont(mono(12.5f));
			g.drawText(getButtonText(), text, juce::Justification::centredLeft, true);
			return;
		}
		g.setColour(col::ink);
		g.setFont(mono(13.0f));
		g.drawText(getButtonText(), text.removeFromLeft(m_labelWidth), juce::Justification::centredLeft, true);
		g.setColour(col::inkSoft);
		g.setFont(mono(12.0f));
		g.drawText(m_detail, text, juce::Justification::centredLeft, true);
	}

private:
	juce::String m_detail;
	int m_labelWidth = 44;
};

// Text wrapped to a width, in one font and colour: its height, and drawing it.
inline juce::TextLayout layoutOf(const juce::String &text, const juce::Font &font, juce::Colour colour, float width)
{
	juce::AttributedString s;
	s.append(text, font, colour);
	s.setWordWrap(juce::AttributedString::byWord);
	juce::TextLayout layout;
	layout.createLayout(s, width);
	return layout;
}

// Draws text wrapped to a width at (x, y); returns its height.
inline float drawWrapped(juce::Graphics &g, const juce::String &text, const juce::Font &font, juce::Colour colour, float x, float y,
                         float width)
{
	const auto layout = layoutOf(text, font, colour, width);
	layout.draw(g, {x, y, width, layout.getHeight()});
	return layout.getHeight();
}

// The search field: a pill-shaped well with a lens.
class SearchField final : public juce::Component {
public:
	SearchField()
	{
		m_editor.setFont(mono(14.0f));
		m_editor.setTextToShowWhenEmpty("Search plugins", col::muted);
		m_editor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
		m_editor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
		m_editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
		m_editor.setColour(juce::TextEditor::textColourId, col::ink);
		m_editor.setColour(juce::CaretComponent::caretColourId, col::accent);
		m_editor.setColour(juce::TextEditor::highlightColourId, col::selection);
		m_editor.setIndents(0, 7);
		m_editor.setTitle("Search plugins");
		addAndMakeVisible(m_editor);
	}

	juce::TextEditor &editor() { return m_editor; }
	const juce::TextEditor &editor() const { return m_editor; }

	void paint(juce::Graphics &g) override
	{
		const auto r = getLocalBounds().toFloat();
		g.setColour(col::well);
		g.fillRoundedRectangle(r, r.getHeight() / 2);
		if (m_editor.hasKeyboardFocus(false)) {
			g.setColour(col::accent);
			g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() / 2, 1.0f);
		}
		const auto lens = juce::Rectangle<float>(12, r.getCentreY() - 7, 14, 14);
		juce::Path p;
		p.addEllipse(lens.getX() + 1.5f, lens.getY() + 1.5f, 9, 9);
		p.startNewSubPath(lens.getX() + 9.5f, lens.getY() + 9.5f);
		p.lineTo(lens.getRight() - 1, lens.getBottom() - 1);
		g.setColour(col::muted);
		g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
	}

	void resized() override { m_editor.setBounds(getLocalBounds().withTrimmedLeft(32).withTrimmedRight(12)); }

private:
	juce::TextEditor m_editor;
};

} // namespace lemus::manager
