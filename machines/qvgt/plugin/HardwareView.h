#pragma once

#include "PluginProcessor.h"
#include "host/PanelFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <vector>

namespace qvgt {

// The QuadraVerb GT's front panel as docs/panel.json measures it (built in, read once): the
// INPUT and OUTPUT knobs, the 4-LED signal display, the LCD as the firmware writes it, the sixteen
// keys of the key matrix with their LEDs, POWER, the brand marks from the user's artwork (none
// without it), at 2.6 px/mm; the bar every machine shares under it (host/PanelFrame.h). REAR shows
// the rear, where the BYPASS and ADVANCE footswitch jacks take a click as a footswitch press.
class HardwareView : public juce::Component, private juce::Timer {
public:
	static constexpr float kPxPerMm = 2.6f;
	static constexpr int kPanelHeight = 114; // the 43.66 mm plate
	static constexpr int kBarHeight = 40;
	static constexpr int kWidth = 1255, kHeight = kPanelHeight + kBarHeight;

	explicit HardwareView(QVGTProcessor &);
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

	// Tools and tests.
	void timerCallbackForTests() { timerCallback(); }
	void showRear(bool on) { m_rear = on; repaint(); }
	void hoverForTests(juce::Point<float> at) { setHover(at); }

private:
	struct KeyRef {
		int key;                    // the firmware's key number (core/board.h)
		juce::String name;
		juce::Rectangle<float> box; // mm
		int ledLatch = -1, ledBit = 0; // the LED in the key: latch (2, 3, 4) and bit, active low; -1 none
	};
	struct Knob {
		juce::String name;
		juce::Point<float> centre; // mm
		float d = 15.8f, labelY = 0;
		juce::RangedAudioParameter *param = nullptr;
	};
	struct Jack {
		juce::String name;
		float x, d;       // rear mm: centre, outer diameter
		int footswitch;   // 0 BYPASS, 1 ADVANCE, -1 none
		bool din;
		juce::StringArray label;
	};

	void timerCallback() override;
	void paintFront(juce::Graphics &g);
	void paintRear(juce::Graphics &g);
	void drawKnob(juce::Graphics &g, const Knob &k, bool hover);
	void drawKey(juce::Graphics &g, const KeyRef &k, bool down, bool hover);
	void drawLcd(juce::Graphics &g);
	void drawBar(juce::Graphics &g);
	void drawTip(juce::Graphics &g);
	void setHover(juce::Point<float> at);
	juce::Rectangle<float> toView(juce::Rectangle<float> mm) const { return mm * kPxPerMm; }
	juce::Point<float> toMm(juce::Point<float> p) const { return p / kPxPerMm; }
	int knobAt(juce::Point<float> mm) const;
	int keyAt(juce::Point<float> mm) const;
	int jackAt(juce::Point<float> mm) const;
	juce::String status() const;

	QVGTProcessor &m_proc;
	std::vector<KeyRef> m_keys;
	std::vector<Knob> m_knobs;
	std::vector<Jack> m_jacks;
	juce::Rectangle<float> m_power;
	std::map<juce::String, lemus::Mark> m_logos;
	lemus::panel::Keys m_held;
	lemus::panel::Bar m_bar{kWidth, kPanelHeight, kBarHeight};
	int m_barHiss = -1, m_barRear = -1;

	int m_hoverKnob = -1, m_hoverKey = -1, m_hoverBar = -1, m_hoverJack = -1;
	int m_dragKnob = -1, m_mouseKey = -1, m_footswitch = -1;
	float m_dragStart = 0, m_dragValue = 0;
	bool m_hoverPower = false;
	bool m_help = false, m_rear = false;
	bool m_hardPress = false; // shift held with the last key press: a hard press
	QVGTProcessor::Display m_display;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareView)
};

} // namespace qvgt
