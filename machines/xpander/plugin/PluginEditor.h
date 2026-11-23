#pragma once

#include "HardwareView.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Hosts the hardware view, scaled to the window; the window keeps the view's aspect ratio and the
// scale is saved with the state.
class XpanderEditor : public juce::AudioProcessorEditor {
public:
	explicit XpanderEditor(XpanderProcessor &);
	~XpanderEditor() override = default;

	void resized() override;

private:
	XpanderProcessor &m_proc;
	xpander::HardwareView m_view;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XpanderEditor)
};
