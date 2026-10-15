#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <map>

// The TB-303's top panel, drawn from the measured layout (docs/panel.json -> PanelData.h) at
// 4 px/mm, with a slim plugin bar underneath. Knobs drag vertically (shift for fine, double
// click resets), the rotary switches click or drag between detents, keys press and release
// like the real ones, and the LEDs show the firmware's matrix. The computer keyboard plays
// the panel keys; '?' lists them.
//
// SERVICE on the bar takes the panel off (HardwareService.cpp): the boards as the service notes
// draw them, every part where the board has it; the six trimmers turn on the board or from their
// controls on the right, where the one picked lights up on the board and the scope, counter and
// meter hook onto its check point.
class HardwareView : public juce::Component, private juce::Timer {
public:
	static constexpr float kPxPerMm = 4.0f;
	static constexpr int kPanelHeight = 568;
	static constexpr int kBarHeight = 40;
	static constexpr int kWidth = 1200, kHeight = kPanelHeight + kBarHeight;

	explicit HardwareView(TB303Processor &);
	~HardwareView() override;

	void paint(juce::Graphics &) override;
	// The window is being resized (the editor scales the view). The panel and the boards are
	// cached as images at the screen's scale; while resizing goes on the old ones are stretched,
	// and they are redrawn once it has stopped: a redraw takes 35-160 ms, too long for every step.
	void resizing() { m_resizedAt = juce::Time::getMillisecondCounter(); }
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
	struct Knob {
		int element;
		const char *param;
	};
	struct Switch {
		int element;
		const char *param;
		std::vector<float> detentsDeg;
	};
	struct Key {
		int element;
		int key; // Machine::Key, or -1 for RUN/STOP
	};
	struct Led {
		int element;
		tb303::Machine::Led led;
	};
	enum class HitKind { None, Knob, Switch, Key, Waveform, Trimmer, TrimRow, TrimSlider, BarSync, BarTop, BarService, BarFactory, BarHelp };
	// The bar's sliders (CUT OFF MAX): their parameter, or null for other hits.
	static const char *barSlider(HitKind k);
	// The bar's items, left to right; FACTORY TRIM shows in service only.
	enum class BarItem { Sync, Top, Service, Factory, Help };
	juce::Rectangle<float> barRect(BarItem) const;
	juce::Rectangle<float> sliderTrack() const; // CUT OFF MAX's
	struct Hit {
		HitKind kind = HitKind::None;
		int index = -1;
	};

	void timerCallback() override;
	Hit hitTest(juce::Point<float> p) const;
	juce::Point<float> toMm(juce::Point<float> p) const { return p / kPxPerMm; }

	void renderStatic(juce::Graphics &);
	void drawElement(juce::Graphics &, int index);
	void drawKnob(juce::Graphics &, const Knob &, float value01, bool hover);
	void drawSwitch(juce::Graphics &, const Switch &, int position, bool hover);
	void drawKey(juce::Graphics &, const Key &, bool down);
	void drawLed(juce::Graphics &, const Led &, float level);
	void drawWaveformSwitch(juce::Graphics &, bool square);
	void drawBar(juce::Graphics &);
	void drawHelp(juce::Graphics &);
	void drawTooltip(juce::Graphics &);

	// Service (HardwareService.cpp). Positions there are view px, from board mm.
public:
	void setService(bool on); // SERVICE on the bar; kept in the state ("service")
	bool service() const { return m_service; }
	// Standalone self-test (--service-test): clicks and drags through the service view with
	// synthetic mouse events and reports each check ("ok ..." or "FAIL ...").
	juce::StringArray serviceSelfTest();
	void setServiceTrim(int trim); // the trimmer picked, 0-5 for TM1-TM6: lit on the board, the instruments on its check point
	int serviceTrim() const { return m_trim; }
	juce::String scopeReading() const { return m_scopeReading; }
private:
	void renderServiceStatic(juce::Graphics &);
	void paintService(juce::Graphics &);
	Hit hitTestService(juce::Point<float> p) const;
	juce::Point<float> servicePos(int element) const;    // a knob's pot, a switch, an LED
	juce::Point<float> trimmerPos(int i) const;          // TM1-TM6
	juce::Point<float> waveformPos() const;              // S1, the WAVEFORM switch
	juce::Rectangle<float> serviceKey(int keyIndex) const; // a key's switch on the switch board
	juce::Rectangle<float> trimRow(int i) const;         // a trimmer's control on the right
	juce::Rectangle<float> trimTrack(int i) const;       // its track (0 at the left, fully clockwise at the right)
	void drawServiceParts(juce::Graphics &);
	void drawTrimmer(juce::Graphics &, int index);
	void drawPicked(juce::Graphics &);
	void drawProbes(juce::Graphics &);
	void drawInstruments(juce::Graphics &);
	void drawScope(juce::Graphics &, juce::Rectangle<float> screen);
	void drawTrimmerList(juce::Graphics &);
	void updateScope();
	juce::String trimmerText(int index) const;
	void factoryTrim();
	void resetTrim(int index);
	void nudgeTrim(int index, float delta);

	float paramValue(const char *id) const;
	void setParamValue(const char *id, float v);
	juce::String paramText(const char *id) const;
	void pressKey(int keyIndex, bool down);
	void releaseAllKeys();

	TB303Processor &m_proc;
	std::vector<Knob> m_knobs;
	std::vector<Switch> m_switches;
	std::vector<Key> m_keys;
	std::vector<Led> m_leds;
	std::map<std::string, juce::Path> m_logos;

	juce::Image m_static;
	float m_staticScale = 0;
	uint32_t m_resizedAt = 0;
	// Whether a cached image drawn at scale `drawnAt` is to be redrawn for `phys` now.
	bool redrawCache(const juce::Image &image, float drawnAt, float phys) const;

	TB303Processor::Snapshot m_snap;
	Hit m_hover, m_drag;
	float m_dragStartValue = 0;
	juce::Point<float> m_dragStart;
	std::vector<bool> m_keyDown;
	std::vector<bool> m_latched; // held down by a right-click until clicked again
	std::map<int, int> m_computerKeys; // KeyPress code -> key index held
	bool m_showHelp = false;

	// Service
	bool m_service = false;
	int m_trim = 3;          // the trimmer picked (TM1-TM6 as 0-5)
	std::array<std::vector<std::pair<float, float>>, 6> m_specBands; // each trimmer's travel in spec, 0-1
	std::array<double, 6> m_specBandsFor{};                           // the trims they were worked out for
	juce::Image m_serviceStatic;
	float m_serviceStaticScale = 0;
	struct Trace {
		juce::Path path;
		int age;
	};
	std::vector<Trace> m_traces; // the scope's persistence, newest last
	juce::String m_scopeReading; // what the scope measures on the trace
	float m_tp6Div = 0.1f;       // TP6's volts per division (it ranges itself)
	std::vector<float> m_scopeBuf, m_scopeTrig;
	std::vector<std::pair<double, double>> m_heldNotes; // (CV volts, VCO Hz) of the last notes held

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareView)
};
