#pragma once

#include "HardwareView.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Hosts the hardware view, scaled to the window. The window keeps the view's aspect ratio;
// the scale is saved with the state.
class SH101Editor : public juce::AudioProcessorEditor {
public:
	explicit SH101Editor(SH101Processor &);
	~SH101Editor() override;

	void resized() override;

private:
	void runSnapshotIfAsked();

	SH101Processor &m_proc;
	sh101::HardwareView m_view;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SH101Editor)
};
