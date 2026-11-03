#pragma once

#include "HardwareView.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Hosts the hardware view, scaled to the window. The window keeps the view's aspect ratio; the
// scale is saved with the state.
class TR909Editor : public juce::AudioProcessorEditor {
public:
	explicit TR909Editor(TR909Processor &);
	~TR909Editor() override;

	void resized() override;

private:
	void runSnapshotIfAsked();

	TR909Processor &m_proc;
	tr909::HardwareView m_view;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TR909Editor)
};
