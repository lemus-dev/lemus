#pragma once

#include "HardwareView.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Hosts the hardware view, scaled to the window. The window keeps the view's aspect ratio; the
// scale is saved with the state.
class QVGTEditor : public juce::AudioProcessorEditor {
public:
	explicit QVGTEditor(QVGTProcessor &);
	~QVGTEditor() override = default;

	void resized() override;

private:
	QVGTProcessor &m_proc;
	qvgt::HardwareView m_view;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QVGTEditor)
};
