// The Xpander's top panel, drawn from docs/panel.json (built in, read once) at 1.8 px/mm: the
// printed panel with its legends, title bars, rules and page diagrams, the 59 keys (held while the
// mouse or a computer key is down, and as the firmware reads them), the 16 LEDs from the two
// latches, the three VFDs as the firmware drives their anodes, the six encoders and VOLUME, the
// wooden end-bells. REAR shows the back: the jacks, MIDI, and the MEMORY PROTECT switch, which
// slides. The brand marks are the user's artwork (none without it). The bar every machine shares
// sits under it (host/PanelFrame.h).
//
// The panel is drawn once into an image at the screen's scale; each repaint draws only what moves
// over it (the VFDs' lit segments, the lit LEDs, the pressed keys, the knobs), and the 30 Hz timer
// repaints only what changed.
#pragma once

#include "PluginProcessor.h"
#include "host/PanelFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

namespace xpander {

class HardwareView : public juce::Component, private juce::Timer {
public:
	static constexpr float kPxPerMm = 1.8f;
	static constexpr float kUnitW = 841.2f, kUnitH = 312.5f; // the top view, mm
	static constexpr int kWidth = 1514;                      // 841.2 mm
	static constexpr int kPanelHeight = 563;                 // 312.5 mm
	static constexpr int kBarHeight = 40;
	static constexpr int kHeight = kPanelHeight + kBarHeight;

	explicit HardwareView(XpanderProcessor &);
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
	void showRear(bool on);
	void showHelp(bool on);
	int buttonCount() const { return int(m_buttons.size()); }
	// Presses (or lets go of) a panel key as the mouse would, by panel.json id; false if none.
	bool pressForTests(const juce::String &id, bool down);
	// Turns encoder 0-5 as a drag would; drainForTests() sends the next detents (the pacer's tick).
	void turnForTests(int encoder, int detents);
	bool drainForTests() { return drainDetents(); }

private:
	struct Button {
		juce::String id, name;
		juce::Point<float> c;      // mm
		juce::Rectangle<float> hole; // the square hole (or its cell of a shared opening), mm
		int column = 0, row = 0;   // the switch matrix
		bool red = false;
		juce::String print;        // keypad caps: the numeral
		juce::Rectangle<float> printBox;
		juce::String computerKey;  // for the tooltip
	};
	struct Led {
		juce::Point<float> c;
		float d = 3.8f;
		int latch = 1, bit = 0;
	};
	struct Knob {
		juce::Point<float> c;
		int encoder = -1; // -1: VOLUME
	};
	struct Vfd {
		juce::Rectangle<float> window;
		float firstX = 0, pitch = 4.6f, top = 0;
	};

	void timerCallback() override;
	void renderPanel(juce::Graphics &);
	void renderRear(juce::Graphics &);
	void drawCap(juce::Graphics &, const Button &, bool down, bool hover) const;
	void drawKnob(juce::Graphics &, const Knob &, bool hover) const;
	void drawLitLed(juce::Graphics &, const Led &) const;
	void drawVfd(juce::Graphics &, int d) const;
	void drawProtect(juce::Graphics &) const;
	void drawMark(juce::Graphics &, const juce::String &place, juce::Rectangle<float> boxMm, bool fitOutline = false) const;
	void drawBar(juce::Graphics &);
	void drawTip(juce::Graphics &);
	juce::String status() const;

	juce::Point<float> toMm(juce::Point<float> p) const { return p / kPxPerMm; }
	void repaintMm(juce::Rectangle<float> mm);
	int buttonAt(juce::Point<float> mm) const;
	int knobAt(juce::Point<float> mm) const;
	bool overProtect(juce::Point<float> mm) const;
	bool overPower(juce::Point<float> mm) const;
	bool held(const Button &, const XpanderProcessor::Display &) const;
	bool lit(const Led &, const XpanderProcessor::Display &) const;
	float knobDegrees(const Knob &) const;
	void turn(int knob, int detents);
	bool drainDetents();
	void setVolume(float normalised);

	XpanderProcessor &m_proc;
	juce::RangedAudioParameter *m_volume = nullptr;
	std::vector<Button> m_buttons;
	std::vector<Led> m_leds;
	std::vector<Knob> m_knobs;
	std::array<Vfd, 3> m_vfds;
	juce::Rectangle<float> m_protectMm; // the rear's MEMORY PROTECT switch, in the rear view's mm
	juce::Rectangle<float> m_powerMm;   // the rear's POWER rocker

	juce::Image m_panelImage, m_rearImage;
	float m_panelScale = 0, m_rearScale = 0;

	lemus::panel::Keys m_held;
	lemus::panel::Bar m_bar{kWidth, kPanelHeight, kBarHeight};
	int m_barRear = -1;

	XpanderProcessor::Display m_display;
	std::array<int, 6> m_encoderDetents{}; // the knobs' turn, for drawing (30 detents a turn)
	// Detents still to send. The board latches one "moved" per encoder until the firmware reads it
	// (every several ms: detents 5 ms apart get lost, 11 ms apart all count), and the processor
	// applies the panel at the start of each block, so detents sent together count as one: they go
	// one at a time, at up to 50 a second.
	std::array<int, 6> m_pendingDetents{};
	struct Pacer : juce::Timer {
		std::function<bool()> tick;
		void timerCallback() override
		{
			if (!tick())
				stopTimer();
		}
	} m_pacer;
	float m_shownVolume = -1;
	bool m_shownProtect = false;
	bool m_shownPower = true;
	juce::String m_shownStatus;

	int m_hoverButton = -1, m_hoverKnob = -1, m_hoverBar = -1;
	bool m_hoverProtect = false, m_hoverPower = false;
	int m_mouseButton = -1, m_dragKnob = -1;
	float m_dragLast = 0, m_dragAccum = 0, m_wheelAccum = 0;
	bool m_help = false, m_rear = false;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareView)
};

} // namespace xpander
