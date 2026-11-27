#pragma once

#include "HardwareView.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Hosts the hardware view, scaled to the window. The window keeps the view's aspect ratio; the
// scale is saved with the state.
class BassStationEditor : public juce::AudioProcessorEditor {
public:
	explicit BassStationEditor(BassStationProcessor &);
	~BassStationEditor() override = default;

	void resized() override;

private:
	BassStationProcessor &m_proc;
	bsrack::HardwareView m_view;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassStationEditor)
};
