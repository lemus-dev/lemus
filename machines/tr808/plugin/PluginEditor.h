#pragma once

#include "HardwareView.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Hosts the hardware view, scaled to the window. The window keeps the view's aspect ratio; the
// scale is saved with the state.
class TR808Editor : public juce::AudioProcessorEditor {
public:
	explicit TR808Editor(TR808Processor &);
	~TR808Editor() override;

	void resized() override;

private:
	void runSnapshotIfAsked();

	TR808Processor &m_proc;
	tr808::HardwareView m_view;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TR808Editor)
};
