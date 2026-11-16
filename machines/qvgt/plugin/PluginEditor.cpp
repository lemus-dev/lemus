#include "PluginEditor.h"

QVGTEditor::QVGTEditor(QVGTProcessor &p) : AudioProcessorEditor(&p), m_proc(p), m_view(p)
{
	// Read first: setting the limits resizes the window, which saves its scale.
	const double scale = double(m_proc.params().state.getProperty("scale", 1.0));
	constexpr int w = qvgt::HardwareView::kWidth, h = qvgt::HardwareView::kHeight;
	addAndMakeVisible(m_view);
	setResizable(true, true);
	setResizeLimits(w / 2, h / 2, w * 2, h * 2);
	getConstrainer()->setFixedAspectRatio(double(w) / h);
	setSize(juce::roundToInt(w * scale), juce::roundToInt(h * scale));
	m_view.grabKeyboardFocus();
}

void QVGTEditor::resized()
{
	if (getWidth() <= 0)
		return;
	const float scale = float(getWidth()) / float(qvgt::HardwareView::kWidth);
	m_view.setBounds(0, 0, qvgt::HardwareView::kWidth, qvgt::HardwareView::kHeight);
	m_view.setTransform(juce::AffineTransform::scale(scale));
	m_proc.params().state.setProperty("scale", scale, nullptr);
}
