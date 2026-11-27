#pragma once

#include "HardwareView.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Hosts the hardware view, scaled to the window. The window keeps the view's aspect ratio; the
// scale is saved with the state.
class Ms20Editor : public juce::AudioProcessorEditor {
public:
	explicit Ms20Editor(Ms20Processor &);
	~Ms20Editor() override = default;

	void resized() override;

private:
	Ms20Processor &m_proc;
	ms20::HardwareView m_view;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ms20Editor)
};
