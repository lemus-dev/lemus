#pragma once

#include "PluginProcessor.h"
#include "host/PanelFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <map>
#include <vector>

namespace tr808 {

// The TR-808's top panel, drawn from the measured layout (docs/panel.json, embedded as is) at
// 2.4 px/mm, with a slim plugin bar underneath. Knobs and TEMPO drag vertically (shift for fine,
// double click resets), rotary switches click or drag between detents, slide and lever switches
// click to their nearest position, keys press and release like the real ones, and the LEDs show
// the firmware's. The computer keyboard plays the panel keys; '?' lists them. SERVICE takes the top
// panel off (HardwareService.cpp): the boards with every part where the service notes' drawings put
// it, the six trimmers, the unit's circuit by serial number and a scope. REAR turns the 808 round:
// its rear panel (panel.json's rear_panel), where a click plugs a cable into a MULTI OUT jack.
class HardwareView : public juce::Component, private juce::Timer {
public:
	static constexpr float kPxPerMm = 2.4f;
	static constexpr float kPanelW = 508.0f, kPanelH = 305.0f;
	static constexpr int kPanelHeight = 732;
	static constexpr int kBarHeight = 40;
	static constexpr int kWidth = 1220, kHeight = kPanelHeight + kBarHeight;

	explicit HardwareView(TR808Processor &);
	~HardwareView() override;

	void paint(juce::Graphics &) override;
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
	void focusLost(FocusChangeType) override;

	// One element of the panel (docs/panel.json): its type, id, the parameter it moves, its box in
	// panel millimetres and everything else as the JSON has it.
	struct Element {
		juce::String type, id, param, label;
		juce::Rectangle<float> box;
		juce::Point<float> centre;
		juce::var v;
	};
	static const std::vector<Element> &elements();

	// Tools (tr808plugintest): SERVICE, zoomed onto a point; trimmer i's procedure set up (SN p.14:
	// the probe on its test point, the time base, volts and trigger); the scope's latest sweep and
	// what the procedure reads (ms, mV rms or Vp-p; 0 if none).
	void showService(bool on);
	void showRear(bool on) { m_rear = on; repaint(); } // the rear panel, as REAR shows it
	bool rearShown() const { return m_rear; }
	void serviceView(float zoom, juce::Point<float> centreMm);
	void serviceSetUp(int trimmer) { setUpTrim(trimmer); }
	void serviceSweep() { updateScope(); }
	double serviceReading() const { return m_scope.reading; }

private:
	enum class HitKind { None, Knob, Switch, Slide, Lever, Key, Power, BarSync, BarHelp, BarRear, Jack, BarService,
		                 Board, Drawer, Serial, Portion, Trim, Trimmer, TrimButton, Scope };
	struct Hit {
		HitKind kind = HitKind::None;
		int index = -1; // element index
	};

	void timerCallback() override;
	Hit hitAt(juce::Point<float> px) const;

	void renderStatic(juce::Graphics &);
	void drawStaticElement(juce::Graphics &, const Element &);
	void drawKnob(juce::Graphics &, const Element &, float value, bool hover);
	void drawRotary(juce::Graphics &, const Element &, int position, bool hover);
	void drawDial(juce::Graphics &, const Element &, float value, bool hover);
	void drawSlide(juce::Graphics &, const Element &, int position, bool hover);
	void drawLever(juce::Graphics &, const Element &, int position, bool hover);
	void drawKey(juce::Graphics &, const Element &, bool down, float led, bool hover);
	void drawLed(juce::Graphics &, const Element &, float level);
	void drawPower(juce::Graphics &, const Element &, bool on, bool hover);
	void drawBar(juce::Graphics &);
	void drawHelp(juce::Graphics &);
	void drawTooltip(juce::Graphics &);
	void renderRear(juce::Graphics &); // in rear-panel millimetres
	void drawRear(juce::Graphics &);   // the cables, in pixels

	// SERVICE (HardwareService.cpp)
	juce::AffineTransform underside(float zoom, juce::Point<float> pan) const; // panel mm -> view px
	juce::Point<float> panelMm(juce::Point<float> px) const; // a point of the panel's or the boards', mm
	void renderUnderside(juce::Graphics &, const juce::AffineTransform &mmToPx); // the still parts
	void paintService(juce::Graphics &);
	void drawDrawer(juce::Graphics &);
	void drawScope(juce::Graphics &);
	void updateScope();
	void scopeMenu(int button);
	void setUpTrim(int trimmer);
	Hit hitService(juce::Point<float> px) const;
	void zoomService(float factor, juce::Point<float> aboutPx);
	void clampPan();
	juce::String serviceStatus() const;
	void setTrim(int trimmer, float rotation);

	float paramValue(const juce::String &id) const;
	void setParamValue(const juce::String &id, float v);
	int choiceIndex(const juce::String &id) const;
	int choiceCount(const juce::String &id) const;
	void setChoice(const juce::String &id, int index);
	juce::String paramText(const juce::String &id) const;
	int keyOf(const Element &) const; // TR808Processor::Key, -1 if none
	void pressKey(int element, bool down);
	void releaseAllKeys();
	float ledLevel(const Element &) const;

	TR808Processor &m_proc;
	std::map<juce::String, lemus::Mark> m_logos; // by mark: roland, rhythm_composer, tr808, computer_controlled
	juce::Image m_static;
	float m_staticScale = 0;
	TR808Processor::Snapshot m_snap;
	Hit m_hover, m_drag;
	float m_dragStartValue = 0;
	juce::Point<float> m_dragStart;
	lemus::panel::Keys m_held; // by element: the keys held by the mouse and the computer keyboard
	lemus::panel::Bar m_bar{kWidth, kPanelHeight, kBarHeight};
	int m_barSync = -1, m_barRear = -1, m_barService = -1;
	bool m_showHelp = false;
	bool m_rear = false; // the rear panel shown
	juce::Image m_rearStatic;
	float m_rearScale = 0;

	bool m_service = false;   // SERVICE: the top panel off
	float m_zoom = 1.0f;      // SERVICE: 1 is the whole unit
	juce::Point<float> m_pan; // SERVICE: the view's offset at that zoom, px
	juce::Image m_serviceBase, m_serviceDetail; // the boards at zoom 1; the visible part at the zoom
	float m_serviceBaseScale = 0, m_detailZoom = 0, m_detailScale = 0;
	juce::Point<float> m_detailPan;
	uint32_t m_viewChangedAt = 0, m_resizedAt = 0;
	bool m_panning = false;
	juce::Point<float> m_dragPan;
	int m_serialShown = 0; // the serial the boards were drawn for (their parts' values)
	// SERVICE's scope: settings, and the last sweep as each pixel column's min and max
	struct Scope {
		int time = 3;            // index into the time bases (1 ms/div)
		int volts = 4;           // into the V/div steps (1 V/div)
		int trigger = -2;        // a voice's gate (Voice order); -1 free running; -2 auto: the probe's own rise
		std::vector<float> lo, hi; // per column
		float pp = 0;            // the sweep's peak to peak
		double reading = 0;      // what the procedure reads (ms, mV rms, Vp-p)
		uint32_t lastTrigger = 0;
		int procedure = -1;      // the trimmer whose procedure is set up, -1 none
		bool test = false;       // the clap played every 0.6 s (TM3's procedure)
		uint32_t lastTestMs = 0;
		juce::String label;      // what the probe was put on ("R375 1"), as picked
	} m_scope;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareView)
};

} // namespace tr808
