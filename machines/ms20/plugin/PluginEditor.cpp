#include "PluginEditor.h"

Ms20Editor::Ms20Editor(Ms20Processor &p) : AudioProcessorEditor(&p), m_proc(p), m_view(p)
{
	// Read first: setting the limits resizes the window, which saves its scale.
	const double scale = double(m_proc.params().state.getProperty("scale", 1.0));
	constexpr int w = ms20::HardwareView::kWidth, h = ms20::HardwareView::kHeight;
	addAndMakeVisible(m_view);
	setResizable(true, true);
	setResizeLimits(w / 2, h / 2, w * 2, h * 2);
	getConstrainer()->setFixedAspectRatio(double(w) / h);
	setSize(juce::roundToInt(w * scale), juce::roundToInt(h * scale));
	m_view.grabKeyboardFocus();
}

void Ms20Editor::resized()
{
	if (getWidth() <= 0)
		return;
	const float scale = float(getWidth()) / float(ms20::HardwareView::kWidth);
	m_view.setBounds(0, 0, ms20::HardwareView::kWidth, ms20::HardwareView::kHeight);
	m_view.setTransform(juce::AffineTransform::scale(scale));
	m_proc.params().state.setProperty("scale", scale, nullptr);
}
