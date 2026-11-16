#pragma once

#include "PluginProcessor.h"
#include "host/PanelFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace sh101 {

// The SH-101's top panel, drawn from the measured layout (docs/panel.json -> PanelData.h) at
// 2.2 px/mm, with a slim plugin bar underneath. Sliders and knobs drag (shift for fine, double
// click resets), switches click or drag between detents, the buttons and keys press like the real
// ones, the bender lever bends left and right and pushes forward for MOD, and the LEDs show the
// firmware's latch. A right-click holds a key or button down until it is clicked again. POWER
// switches the unit off and on. The computer keyboard plays the keys; '?' lists them.
class HardwareView : public juce::Component, private juce::Timer {
public:
	static constexpr float kPxPerMm = 2.2f;
	static constexpr int kPanelHeight = 682; // 310 mm
	static constexpr int kBarHeight = 40;
	static constexpr int kWidth = 1254, kHeight = kPanelHeight + kBarHeight;

	explicit HardwareView(SH101Processor &);
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

private:
	enum class HitKind { None, Slider, Switch, Knob, Button, Key, Bender, Power, BarSync, BarHelp };
	struct Hit {
		HitKind kind = HitKind::None;
		int index = -1;
	};
	// A parameter control: a slider, a knob, or a switch (slide or rotary) with its detents.
	struct Control {
		int element;
		const char *param;
		std::vector<int> order; // switch: parameter item per detent (detents in panel order)
	};

	void timerCallback() override;
	Hit hitAt(juce::Point<float> p) const;
	juce::Point<float> toMm(juce::Point<float> p) const { return p / kPxPerMm; }

	void renderStatic(juce::Graphics &);
	void drawPrint(juce::Graphics &, int element);
	void drawSlider(juce::Graphics &, const Control &, float value, bool hot);
	void drawSwitch(juce::Graphics &, const Control &, int detent, bool hot);
	void drawKnob(juce::Graphics &, const Control &, float value, bool hot);
	void drawButton(juce::Graphics &, int element, bool down);
	void drawLed(juce::Graphics &, int element, float level);
	void drawKey(juce::Graphics &, int element, bool down);
	void drawBender(juce::Graphics &, float bend, float push);
	void drawBar(juce::Graphics &);
	void drawHelp(juce::Graphics &);
	void drawTooltip(juce::Graphics &);

	float paramValue(const char *id) const;
	void setParamValue(const char *id, float v);
	juce::String paramText(const char *id) const;
	int detentOf(const Control &) const;
	void setDetent(const Control &, int detent);
	float sliderValueAt(const Control &, float yMm) const;
	int buttonKey(int button) const { return int(m_keys.size()) + button; } // m_held's index for a button
	void releaseAll();
	void setBender(float bend, float push);

	SH101Processor &m_proc;
	std::vector<Control> m_sliders, m_switches, m_knobs;
	std::vector<int> m_buttons;          // elements, in Machine::Button order
	std::vector<int> m_leds;             // elements, in Machine::Led order
	int m_ledRate = -1, m_ledPower = -1, m_bender = -1, m_power = -1;
	std::vector<int> m_keys;             // elements, by key number
	std::map<std::string, lemus::Mark> m_logos; // by the panel's logo id

	juce::Image m_static;
	float m_staticScale = 0;

	SH101Processor::Snapshot m_snap;
	Hit m_hover, m_drag;
	float m_dragStartValue = 0;
	juce::Point<float> m_dragStart;
	int m_mouseKey = -1;                 // key held by the mouse
	// The keys (0-31) and then the buttons (Machine::Button order), as the mouse and the computer
	// keyboard hold them.
	lemus::panel::Keys m_held;
	std::map<int, int> m_leverKeys;      // KeyPress code -> the arrow it is (kLeftCode ...)
	lemus::panel::Bar m_bar{kWidth, kPanelHeight, kBarHeight};
	int m_barSync = -1;
	int m_octave = 0;                    // computer keyboard: -1 .. 1
	float m_bend = 0, m_push = 0;        // the lever, from the mouse or the arrow keys
	bool m_showHelp = false;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareView)
};

} // namespace sh101
