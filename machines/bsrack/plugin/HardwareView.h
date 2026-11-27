#pragma once

#include "PluginProcessor.h"
#include "host/PanelFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace bsrack {

// The Bass Station Rack's front panel as docs/panel.json measures it (built in, read once), at
// 4 px/mm: the 18 knobs, the 12 slide switches, the data-entry keypad with AUDITION/DEMO and MODE
// SELECT, the 2-digit display and its LEDs as the firmware lights them, the LFO SPEED and WRITE
// LEDs, PHONES, every printed legend; the brand marks from the user's artwork (none without it).
// The bar every machine shares sits under it (host/PanelFrame.h). REAR turns the unit round: the
// rear plate as panel.json's "rear" measures it, its jacks with tooltips. FILTER's EXTERNAL AUDIO
// INPUT is the plugin's side-chain: a cable in it while the host routes audio there.
class HardwareView : public juce::Component, private juce::Timer {
public:
	// 2.6 px/mm, as the QuadraVerb GT's 19" panel: 1255 px wide at scale 1 (4 px/mm made 1931)
	static constexpr float kPxPerMm = 2.6f;
	static constexpr int kWidth = 1255;       // the 482.6 mm plate
	static constexpr int kPanelHeight = 114;  // 44.0 mm
	static constexpr int kBarHeight = 40;
	static constexpr int kHeight = kPanelHeight + kBarHeight;

	explicit HardwareView(BassStationProcessor &);
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
	// The pointer over a control: a pot's or switch's parameter id, or a key's panel.json id
	// ("key1", "audition"); its tooltip shown. Empty: nothing.
	void hoverForTests(const juce::String &id);
	void showHelpForTests(bool on);
	void showRear(bool on)
	{
		m_rear = on;
		m_hoverJack = -1;
		repaint();
	}
	bool rearShown() const { return m_rear; }
	// The controls the view knows, and how many found no parameter.
	int controlCount() const { return int(m_knobs.size() + m_switches.size() + m_keys.size()); }
	int unmappedControls() const;
	// A computer key, as a test types it: true if the panel took it.
	bool typeForTests(const juce::KeyPress &k) { return keyPressed(k); }
	bool keyDown(int button) const { return m_held.down(button); }

private:
	struct Text {
		juce::String text;
		float x = 0, y = 0, cap = 1.5f; // mm: anchor, cap-height centre, cap height
		int align = 0;                  // -1 left, 0 centre, 1 right
		juce::Colour colour;
		float w = 0;                    // > 0: fitted to this width
	};
	struct Knob {
		juce::RangedAudioParameter *param = nullptr;
		juce::Point<float> c; // mm
		float d = 12.6f, capD = 8.9f;
		float from = -141, to = 141; // the pointer's travel: the printed scale's ends
	};
	struct Switch {
		juce::RangedAudioParameter *param = nullptr;
		juce::Rectangle<float> body; // mm
		std::vector<float> leverX;   // the lever's centre at each position
	};
	struct Key {
		juce::String id, name, computer;
		juce::Rectangle<float> box; // mm
		Text legend;                // the numeral or SELECT printed on it (empty: none)
		juce::Rectangle<float> note; // AUDITION's note symbol (empty: none)
	};
	// The printed panel: paths by colour, in drawing order, made once.
	struct Layer {
		juce::Colour colour;
		juce::Path path;
	};

	void timerCallback() override;
	void buildStatic();
	void addFill(juce::Colour c, const juce::Path &p);
	void addStroke(juce::Colour c, const juce::Path &p, float widthMm);
	void addText(const Text &t);
	void paintPanel(juce::Graphics &g);
	void drawKnob(juce::Graphics &g, const Knob &k, bool hover) const;
	void drawSwitch(juce::Graphics &g, const Switch &s, bool hover) const;
	void drawKey(juce::Graphics &g, const Key &k, bool down, bool hover) const;
	void drawDisplay(juce::Graphics &g) const;
	void drawLed(juce::Graphics &g, juce::Point<float> c, float d, juce::Colour off, bool lit) const;
	void drawMarks(juce::Graphics &g, bool rear) const;
	void paintRear(juce::Graphics &g);
	void drawCable(juce::Graphics &g, juce::Point<float> jackMm) const;
	int jackAt(juce::Point<float> viewMm) const;
	juce::String jackTip(int jack) const;
	void drawBar(juce::Graphics &g);
	void drawTip(juce::Graphics &g);
	juce::String status() const;
	void setHover(juce::Point<float> at);
	juce::Point<float> toMm(juce::Point<float> p) const { return p / kPxPerMm; }
	int knobAt(juce::Point<float> mm) const;
	int switchAt(juce::Point<float> mm) const;
	int keyAt(juce::Point<float> mm) const;
	int position(const Switch &s) const;
	void setPosition(const Switch &s, int pos);
	int nearestPosition(const Switch &s, float xMm) const;
	uint64_t signature() const;

	BassStationProcessor &m_proc;
	std::vector<Layer> m_layers;
	std::vector<Knob> m_knobs;
	std::vector<Switch> m_switches;
	std::vector<Key> m_keys; // in bsrack::Button order
	std::vector<juce::Rectangle<int>> m_displayAreas; // what a change on the display repaints
	lemus::panel::Keys m_held;
	lemus::panel::Bar m_bar{kWidth, kPanelHeight, kBarHeight};

	int m_hoverKnob = -1, m_hoverSwitch = -1, m_hoverKey = -1, m_hoverBar = -1, m_hoverJack = -1;
	int m_barRear = -1;
	bool m_rear = false;
	int m_dragKnob = -1, m_dragSwitch = -1, m_mouseKey = -1;
	float m_dragStart = 0, m_dragValue = 0, m_switchFrom = 0;
	bool m_switchMoved = false;
	bool m_help = false, m_focused = false;
	BassStationProcessor::Display m_display;
	uint64_t m_signature = 0;
	juce::Image m_static; // the printed panel at m_staticScale
	float m_staticScale = 0;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareView)
};

} // namespace bsrack
