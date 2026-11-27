#pragma once

#include "PluginProcessor.h"
#include "host/PanelFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <vector>

namespace ms20 {

// The MS-20 as docs/panel.json measures it (built in, read once): the front view, 569 x 297 mm,
// drawn flat from the top of the side panels down (the top cover the photo sees end-on, above
// them, left out, as the other machines draw no case top),
// with the printed panel, its 32 knobs and 4 rotary switches, the patch panel's 35 jacks and its
// flow diagram, the LEDs, the 37 keys, and the left cheek's control wheel and momentary switch,
// drawn at kPxPerMm (what doesn't move cached in an image). Cables plug from jack to jack as on the
// unit. Knobs drag (shift for fine, double-click for the default), rotary switches click round or
// drag, the keys and the momentary switch press like the real ones (a right-click holds them down
// until clicked again), the wheel drags and keeps its place. The KORG logotype is the user's
// artwork (none without it). The bar every machine shares sits under it (host/PanelFrame.h).
class HardwareView : public juce::Component, private juce::Timer {
public:
	static constexpr float kPxPerMm = 2.4f;
	static constexpr float kTopMm = 16.9f; // panel.json's y of the side panels' top: the view's top
	static constexpr float kWidthMm = 569.0f, kHeightMm = 297.4f - kTopMm;
	static constexpr int kWidth = 1366;       // 569 mm
	static constexpr int kPanelHeight = 673;  // 280.5 mm
	static constexpr int kBarHeight = 40;
	static constexpr int kHeight = kPanelHeight + kBarHeight;

	explicit HardwareView(Ms20Processor &);
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
	int controlCount() const { return int(m_controls.size()); }
	int unmappedControls() const;
	int jackCount() const { return int(m_jacks.size()); }
	// Where a part is (px): a control by its parameter ID, a jack by its panel id ("jack:espin"), a
	// key ("key:12"), "wheel", "switch" or "signal in" (the bar's pill); (-1, -1) if there's none.
	juce::Point<float> placeForTests(const juce::String &part) const;
	juce::String statusForTests() const { return status(); }

private:
	struct Control {
		juce::String id, label;
		bool rotary = false;
		juce::Point<float> c; // mm
		float d = 17.4f, topD = 15.5f;
		float minDeg = -150, maxDeg = 150;
		std::vector<float> detents; // rotary: degrees per position
		juce::RangedAudioParameter *param = nullptr;
		juce::var element;
	};
	struct Socket {
		juce::String id, label;
		int jack = -1; // core/patch.h index, -1 for a jack that isn't a patch point (PHONES)
		bool input = false;
		juce::Point<float> c; // mm
		float d = 7.0f, nut = 13.0f, nutRot = 0;
	};
	struct Key {
		int key;
		bool white;
		juce::Rectangle<float> box, front;
	};

	void timerCallback() override;
	// What doesn't move: the case, the print, holes, screws, the logo, the jacks, the knobs' bodies.
	void renderStatic(juce::Graphics &g);
	void drawKnobBody(juce::Graphics &g, const Control &c);
	void drawControl(juce::Graphics &g, const Control &c, bool hover); // its pointer
	void drawJackBody(juce::Graphics &g, const Socket &j);
	void drawLeds(juce::Graphics &g);
	void drawCables(juce::Graphics &g);
	void drawCable(juce::Graphics &g, juce::Point<float> a, juce::Point<float> b, juce::Colour colour, bool plugA, bool plugB);
	void drawKeys(juce::Graphics &g);
	void drawCheek(juce::Graphics &g);
	void drawBar(juce::Graphics &g);
	void drawHelp(juce::Graphics &g);
	void drawTooltip(juce::Graphics &g);
	juce::String status() const;
	juce::String controlTip(const Control &c) const;
	juce::String jackTip(const Socket &j) const;

	juce::Point<float> toMm(juce::Point<float> p) const { return p / kPxPerMm + juce::Point<float>(0, kTopMm); }
	static juce::Point<float> px(juce::Point<float> mm) { return (mm - juce::Point<float>(0, kTopMm)) * kPxPerMm; }
	static juce::Rectangle<float> px(juce::Rectangle<float> mm) { return (mm - juce::Point<float>(0, kTopMm)) * kPxPerMm; }
	static juce::AffineTransform mmToPx() { return juce::AffineTransform::translation(0, -kTopMm).scaled(kPxPerMm); }
	int controlAt(juce::Point<float> mm) const;
	int jackAt(juce::Point<float> mm) const;
	int keyAt(juce::Point<float> mm) const;
	bool overWheel(juce::Point<float> mm) const;
	bool overSwitch(juce::Point<float> mm) const;
	double value(const Control &c) const;      // its parameter, 0-1
	float angle(const Control &c) const;       // degrees clockwise from 12 o'clock
	void setValue(const Control &c, double v); // 0-1
	int detentOf(const Control &c) const;      // a rotary switch's position
	int detentAt(const Control &c, juce::Point<float> mm) const; // the position nearest the mouse
	int jackIndex(int patchJack) const;        // m_jacks index of a core/patch.h jack, or -1
	void connect(int from, int to);            // two m_jacks indices: an output and an input
	void cancelCable();                        // Escape: the cable being drawn dropped, a moved plug put back
	int signalIn() const;                      // where the cable from outside is: 0 none, 1 ESP, 2 EXT, 3 elsewhere
	void setSignalIn(int where);
	// SERVICE: a card over the bar with CIRCUIT, the unit's filter board (REV 1 or REV 2)
	juce::Rectangle<float> serviceCard() const;
	juce::Rectangle<float> serviceOption(int i) const; // 0 REV 1, 1 REV 2
	void drawService(juce::Graphics &g);
	void setWheel(double v);

	Ms20Processor &m_proc;
	std::vector<Control> m_controls;
	std::vector<Socket> m_jacks;
	std::vector<Key> m_keys; // white keys first, then black (drawn on top, hit first)
	std::map<juce::String, lemus::Mark> m_logos;
	int m_volume = -1; // m_controls index of VOLUME (its POWER OFF click)
	// The 37 keys and then the momentary switch, as the mouse and the computer keyboard hold them.
	static constexpr int kMomentary = Machine::kKeys;
	lemus::panel::Keys m_held;
	lemus::panel::Bar m_bar{kWidth, kPanelHeight, kBarHeight};
	int m_barService = -1; // the SERVICE pill: its card holds CIRCUIT
	bool m_service = false; // the card is open
	int m_hoverOption = -1; // the card's option under the mouse

	juce::Rectangle<float> m_wheel, m_switch; // mm
	juce::Path m_wheelSlot;                   // mm

	juce::Image m_static; // renderStatic() at the screen's pixels
	float m_staticScale = 0;

	int m_hoverControl = -1, m_hoverJack = -1, m_hoverBar = -1;
	bool m_hoverWheel = false, m_hoverSwitch = false;
	int m_dragControl = -1, m_mouseKey = -1;
	int m_dragDetent = 0;   // a rotary switch's position when the mouse went down on it
	bool m_keyDrag = false; // the mouse holds a key: sliding along the keys plays them
	bool m_mouseSwitch = false;
	float m_dragStart = 0, m_dragValue = 0;
	int m_cableFrom = -1;     // a cable being drawn: from this jack...
	juce::Point<float> m_cableTo; // ...to the mouse (mm)
	int m_pulledIn = -1, m_pulledFrom = -1; // the plug this drag pulled out of an input (core/patch.h jack, its output)
	bool m_dragWheel = false;
	double m_wheelValue = 0, m_wheelStart = 0;
	int m_octave = 0; // computer keyboard: -1 .. 1
	bool m_help = false;
	Ms20Processor::Display m_display;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareView)
};

} // namespace ms20
