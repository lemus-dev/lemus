#pragma once

#include "HardwareView.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Hosts the hardware view, scaled to the window. The window keeps the view's aspect ratio; the
// scale is saved with the state.
class VirusCEditor : public juce::AudioProcessorEditor {
public:
	explicit VirusCEditor(VirusCProcessor &);
	~VirusCEditor() override = default;

	void resized() override;

private:
	VirusCProcessor &m_proc;
	virusc::HardwareView m_view;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VirusCEditor)
};
