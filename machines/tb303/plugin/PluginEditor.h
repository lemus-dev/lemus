#pragma once

#include "HardwareView.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Hosts the hardware view, scaled to the window. The window keeps the view's aspect ratio;
// the scale is saved with the state.
class TB303Editor : public juce::AudioProcessorEditor {
public:
	explicit TB303Editor(TB303Processor &);
	~TB303Editor() override;

	void resized() override;

private:
	void runSnapshotIfAsked();

	TB303Processor &m_proc;
	HardwareView m_view;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TB303Editor)
};
