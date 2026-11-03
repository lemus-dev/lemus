#pragma once

#include "PluginProcessor.h"
#include "host/PanelFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <map>
#include <vector>

namespace tr909 {

// The TR-909's top panel, drawn from the measured layout (docs/panel.json, embedded as is) at
// 2.5 px/mm, with a slim plugin bar underneath. Knobs drag vertically (shift for fine, double
// click resets), keys press and release like the real ones (a right-click holds one down until
// it's clicked again), and the LEDs, the lit buttons and the MEAS/TEMPO display show the
// firmware's. The computer keyboard plays the panel keys; '?' lists them. REAR turns the 909
// round: its rear panel (panel.json's rear_panel), where a click plugs a cable into a MULTI OUT
// jack or pulls it out. SERVICE takes the top panel off (HardwareService.cpp): the boards with every
// part where the service notes' drawings put it, the clap's trimmer TM, the unit's circuit revision
// by serial number (its production changes) and a scope.
class HardwareView : public juce::Component, private juce::Timer {
public:
	// The key list (?) shown or not: the editor's --keys snapshot.
	void showKeys(bool on)
	{
		m_showHelp = on;
		repaint();
	}
	static constexpr float kPxPerMm = 2.5f;
	static constexpr float kPanelW = 486.0f, kPanelH = 279.49f;
	static constexpr int kPanelHeight = 699;
	static constexpr int kBarHeight = 40;
	static constexpr int kWidth = 1215, kHeight = kPanelHeight + kBarHeight;

	explicit HardwareView(TR909Processor &);
	~HardwareView() override;

	void paint(juce::Graphics &) override;
	// The window is being resized (the editor scales the view): the cached panel is stretched
	// meanwhile and redrawn at the new scale once resizing has stopped.
	void resizing() { m_resizedAt = juce::Time::getMillisecondCounter(); }
	void mouseMove(const juce::MouseEvent &) override;
	void mouseExit(const juce::MouseEvent &) override;
	void mouseDown(const juce::MouseEvent &) override;
	void mouseDrag(const juce::MouseEvent &) override;
	void mouseUp(const juce::MouseEvent &) override;
	void mouseDoubleClick(const juce::MouseEvent &) override;
	void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;
	void mouseMagnify(const juce::MouseEvent &, float scaleFactor) override;
	bool keyPressed(const juce::KeyPress &) override;
	bool keyStateChanged(bool isKeyDown) override;
	void modifierKeysChanged(const juce::ModifierKeys &) override;
	void focusLost(FocusChangeType) override;

	// One element of the panel (docs/panel.json): its type, id, the parameter or key it moves,
	// its box in panel millimetres and everything else as the JSON has it.
	struct Element {
		juce::String type, id, param, label;
		juce::Rectangle<float> box;
		juce::Point<float> centre;
		juce::var v;
	};
	static const std::vector<Element> &elements();
	void showRear(bool on) { m_rear = on; repaint(); } // tools: the rear panel, as REAR shows it
	void showService(bool on) { m_service = on; repaint(); } // tools: SERVICE
	void serviceView(float zoom, juce::Point<float> centreMm); // tools: SERVICE zoomed onto a point
	void serviceSetUpTm() { setUpTm(); }  // tools: the TM procedure's scope set-up (play the clap yourself)
	void serviceSweep() { updateScope(); } // tools: the scope takes its latest sweep now

private:
	enum class HitKind { None, Knob, Key, BarSync, BarHelp, BarRear, Jack, Power, BarService,
		                 Board, Drawer, Serial, Trim, TrimButton, Scope };
	struct Hit {
		HitKind kind = HitKind::None;
		int index = -1; // element index; for a Jack, the voice (kVoices: MASTER OUT R, + 1: L)
	};
	struct Key {
		int element;
		Machine::Key key;
		int led = -1; // the LED element in or behind the cap, or -1
	};

	void timerCallback() override;
	Hit hitAt(juce::Point<float> px) const;

	void renderStatic(juce::Graphics &);
	void drawStaticElement(juce::Graphics &, const Element &);
	void drawKnob(juce::Graphics &, const Element &, float value, bool hover);
	void drawKey(juce::Graphics &, const Key &, bool down, bool hover);
	void drawLed(juce::Graphics &, const Element &, float level);
	void drawDisplay(juce::Graphics &, const Element &);
	void drawBar(juce::Graphics &);
	void renderRear(juce::Graphics &); // in rear-panel millimetres
	void drawRear(juce::Graphics &);   // the cables, in pixels
	void drawHelp(juce::Graphics &);
	void drawTooltip(juce::Graphics &);

	// SERVICE (HardwareService.cpp)
	juce::AffineTransform underside(float zoom, juce::Point<float> pan) const; // panel mm -> view px
	void renderUnderside(juce::Graphics &, const juce::AffineTransform &mmToPx); // the still parts
	void paintService(juce::Graphics &);
	void drawDrawer(juce::Graphics &);
	void drawScope(juce::Graphics &);
	void updateScope();
	void scopeMenu(int button);
	void setUpTm();
	Hit hitService(juce::Point<float> px) const;
	void zoomService(float factor, juce::Point<float> aboutPx);
	void clampPan();
	juce::String serviceStatus() const;

	float paramValue(const juce::String &id) const;
	void setParamValue(const juce::String &id, float v);
	juce::String paramText(const juce::String &id) const;
	juce::String tempoText() const; // the tempo the firmware runs at (m_snap.tempo), as BPM
	int keyIndexOf(Machine::Key) const; // into m_keys, -1 if none
	void pressKey(int keyIndex, bool down);
	void releaseAllKeys(bool latchedToo);
	float ledLevel(const Element &) const;

	TR909Processor &m_proc;
	std::vector<Key> m_keys;
	std::vector<int> m_knobs, m_leds;
	int m_display = -1;
	std::map<juce::String, lemus::Mark> m_logos; // by mark: roland, tr909, rhythm_composer, rear_roland
	juce::Image m_static, m_rearStatic;
	float m_staticScale = 0, m_rearScale = 0;
	bool m_rear = false; // the rear panel shown
	bool m_service = false; // SERVICE: the underside, bottom cover off
	float m_zoom = 1.0f;     // SERVICE: 1 is the whole unit, as the panel
	juce::Point<float> m_pan; // SERVICE: the view's offset at that zoom, px
	juce::Image m_serviceBase, m_serviceDetail; // the whole underside at zoom 1; the visible part at the zoom
	float m_serviceBaseScale = 0;
	float m_detailZoom = 0, m_detailScale = 0;
	juce::Point<float> m_detailPan;
	uint32_t m_viewChangedAt = 0;
	bool m_panning = false;
	juce::Point<float> m_dragPan;
	int m_serialShown = 0;
	float m_tmShown = -1;
	// SERVICE's scope: settings, and the last triggered sweep as each pixel column's min and max
	struct Scope {
		int time = 6;                // index into the time bases (10 ms/div)
		std::array<int, 2> volts{{4, 4}}; // into the V/div steps (1 V/div)
		int trigger = int(Voice::HC); // the voice whose TRIG line triggers it; -1: free running
		std::array<std::vector<float>, 2> lo, hi; // per column
		std::array<float, 2> pp{};   // each channel's peak to peak over the sweep
		uint32_t lastTrigger = 0;
		bool test = false;           // the voice under test played every 0.6 s
		uint32_t lastTestMs = 0;
		std::array<juce::String, 2> label; // what each probe was put on ("R215 pin 1"), as picked
	} m_scope;
	std::array<bool, kVoices> m_cablesShown{}, m_outputsShown{};
	uint32_t m_resizedAt = 0;
	TR909Processor::Snapshot m_snap;
	std::vector<float> m_knobValues; // as last drawn, to repaint when the host moves one
	bool m_syncShown = false;
	Hit m_hover, m_drag;
	float m_dragStartValue = 0;
	juce::Point<float> m_dragStart;
	lemus::panel::Keys m_held; // the keys held by the mouse and the computer keyboard
	lemus::panel::Bar m_bar{kWidth, kPanelHeight, kBarHeight};
	int m_barSync = -1, m_barRear = -1, m_barService = -1;
	bool m_shiftHeld = false;
	bool m_showHelp = false;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareView)
};

} // namespace tr909
