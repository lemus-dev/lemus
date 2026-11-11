#pragma once

#include "PluginProcessor.h"
#include "host/PanelFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <map>
#include <vector>

namespace virusc {

// The Virus C's top panel, drawn from its measured layout (docs/panel.json, embedded as is) at
// 2.6 px/mm, with the bar every machine shares under it (host/PanelFrame.h). Knobs drag vertically
// (shift for fine, double click returns them to where the plugin starts), keys press and release as
// on the unit and go through the firmware's own scan, the LEDs show what the firmware lights, at
// the brightness it drives them, and the LCD shows the controller's dots. '?' lists the computer keys.
class HardwareView : public juce::Component, private juce::Timer {
public:
	static constexpr float kPxPerMm = 2.6f;
	static constexpr float kPanelW = 470.86f, kPanelH = 181.06f;
	static constexpr int kPanelHeight = 471; // kPanelH * kPxPerMm
	static constexpr int kBarHeight = 40;
	static constexpr int kWidth = 1224, kHeight = kPanelHeight + kBarHeight;

	explicit HardwareView(VirusCProcessor &);
	~HardwareView() override;

	void paint(juce::Graphics &) override;
	void mouseMove(const juce::MouseEvent &) override;
	void mouseExit(const juce::MouseEvent &) override;
	void mouseDown(const juce::MouseEvent &) override;
	void mouseDrag(const juce::MouseEvent &) override;
	void mouseUp(const juce::MouseEvent &) override;
	void mouseDoubleClick(const juce::MouseEvent &) override;
	void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;
	bool keyPressed(const juce::KeyPress &) override;
	bool keyStateChanged(bool isKeyDown) override;
	void focusLost(FocusChangeType) override;

	// One element of the panel as docs/panel.json has it, its box in panel millimetres.
	struct Element {
		juce::String type, id, label;
		juce::Rectangle<float> box;
		juce::Point<float> centre;
		juce::var v;
	};
	static const std::vector<Element> &elements();

	// Tools and tests.
	void timerCallbackForTests() { timerCallback(); }
	void showRear(bool on) { m_rear = on; repaint(); }
	int keyCount() const { return int(m_keys.size()); }
	int knobCount() const { return int(m_knobs.size()); }
	int ledCount() const { return int(m_leds.size()); }

private:
	struct KeyRef {
		int element;
		int row, bit;
		juce::String name;
	};
	struct KnobRef {
		int element;
		juce::RangedAudioParameter *param;
	};
	struct LedRef {
		int element;
		int column, line; // ledState() indices; column -1: an LFO RATE LED, line 0 or 1
	};

	void timerCallback() override;
	void renderStatic(juce::Graphics &g);
	void drawStaticElement(juce::Graphics &g, const Element &e);
	void drawKnob(juce::Graphics &g, const Element &e, float value, bool hover);
	void drawKey(juce::Graphics &g, const Element &e, bool down, bool hover);
	void drawLed(juce::Graphics &g, const Element &e, float level);
	void drawLcd(juce::Graphics &g, const Element &e);
	void drawBar(juce::Graphics &g);
	void drawRear(juce::Graphics &g);
	juce::Rectangle<float> rearSwitch() const; // the ON/OFF rocker in the rear view, mm
	juce::Rectangle<float> toView(juce::Rectangle<float> mm) const { return mm * kPxPerMm; }
	juce::Point<float> toMm(juce::Point<float> p) const { return p / kPxPerMm; }
	int knobAt(juce::Point<float> mm) const;
	int keyAt(juce::Point<float> mm) const;
	juce::String status() const;
	const std::array<uint8_t, 8> &glyph(uint8_t code);

	VirusCProcessor &m_proc;
	std::vector<KeyRef> m_keys;
	std::vector<KnobRef> m_knobs;
	std::vector<LedRef> m_leds;
	int m_lcdElement = -1;
	lemus::panel::Keys m_held;
	lemus::panel::Bar m_bar{kWidth, kPanelHeight, kBarHeight};

	juce::Image m_static;
	float m_staticScale = 0;

	int m_hoverKnob = -1, m_hoverKey = -1, m_hoverBar = -1;
	int m_dragKnob = -1, m_mouseKey = -1;
	float m_dragStart = 0, m_dragValue = 0;
	bool m_help = false;
	bool m_rear = false;    // the rear panel instead of the top (the bar's REAR)
	int m_barRear = -1;
	bool m_hoverSwitch = false;

	float m_ledLevels[Machine::kLedColumns][Machine::kLedLines]{};
	float m_lfoLevels[2]{};
	VirusCProcessor::Lcd m_lcd;
	std::map<uint8_t, std::array<uint8_t, 8>> m_glyphs; // character code -> 8 rows of 5 dots, bit 4 leftmost

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareView)
};

} // namespace virusc
