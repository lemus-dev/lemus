// The pedal as it is: the 1984 RAT "White Face" small box seen from above, drawn in code from
// PanelData.h (docs/panel.json, measured from photos; tools/panel/), with its front face below it.
// The brand marks (the RAT letters, the Pro Co logos) are artwork places, filled from the user's
// own files; without them the white block is plain. Knobs and the footswitch work as on the pedal.
// The bar underneath (host/PanelFrame.h) holds what the pedal does not have: INPUT (a gain in
// front of it), whether it is on (it has no LED) and the key list.
#pragma once

#include "PluginProcessor.h"

#include "host/PanelFrame.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

class HardwareView : public juce::Component, private juce::Timer {
public:
	static constexpr float kPxPerMm = 6.0f;
	static constexpr int kWidth = 630;
	static constexpr int kPanelHeight = 850;
	static constexpr int kBarHeight = 40;
	static constexpr int kHeight = kPanelHeight + kBarHeight;

	explicit HardwareView(RatProcessor &);
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

private:
	void timerCallback() override;
	void drawPedal(juce::Graphics &g);
	void drawKnob(juce::Graphics &g, int k, float value, bool hot);
	void drawFootswitch(juce::Graphics &g);
	void drawBar(juce::Graphics &g);
	juce::String status() const;
	int knobAt(juce::Point<float> mm) const;
	bool onFootswitch(juce::Point<float> mm) const;
	juce::Point<float> toMm(juce::Point<float> px) const;
	float paramValue(const char *id) const;
	void setParamValue(const char *id, float v);
	void toggleFootswitch();

	RatProcessor &m_proc;
	lemus::panel::Bar m_bar{kWidth, kPanelHeight, kBarHeight};
	int m_barInput = -1;
	int m_hoverBar = -1, m_hoverKnob = -1, m_dragKnob = -1;
	bool m_dragInput = false, m_hoverFoot = false, m_help = false;
	float m_dragStartY = 0, m_dragStartX = 0, m_dragStartValue = 0;
	std::array<float, 5> m_shown{};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareView)
};
