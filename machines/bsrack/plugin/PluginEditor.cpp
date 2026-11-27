#include "PluginEditor.h"

BassStationEditor::BassStationEditor(BassStationProcessor &p) : AudioProcessorEditor(&p), m_proc(p), m_view(p)
{
	// Read first: setting the limits resizes the window, which saves its scale.
	const double scale = double(m_proc.params().state.getProperty("scale", 1.0));
	constexpr int w = bsrack::HardwareView::kWidth, h = bsrack::HardwareView::kHeight;
	addAndMakeVisible(m_view);
	setResizable(true, true);
	setResizeLimits(w / 2, h / 2, w * 2, h * 2);
	getConstrainer()->setFixedAspectRatio(double(w) / h);
	// JUCE: setSize last in the editor's constructor.
	setSize(juce::roundToInt(w * scale), juce::roundToInt(h * scale));
}

void BassStationEditor::resized()
{
	if (getWidth() <= 0)
		return;
	const float scale = float(getWidth()) / float(bsrack::HardwareView::kWidth);
	m_view.setBounds(0, 0, bsrack::HardwareView::kWidth, bsrack::HardwareView::kHeight);
	m_view.setTransform(juce::AffineTransform::scale(scale));
	m_proc.params().state.setProperty("scale", scale, nullptr);
}
