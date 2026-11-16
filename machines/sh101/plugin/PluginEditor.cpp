#include "PluginEditor.h"

SH101Editor::SH101Editor(SH101Processor &p) : AudioProcessorEditor(&p), m_proc(p), m_view(p)
{
	// Read first: setting the limits resizes the window, which saves its scale.
	const double scale = double(m_proc.params().state.getProperty("scale", 1.0));
	constexpr int w = sh101::HardwareView::kWidth, h = sh101::HardwareView::kHeight;
	addAndMakeVisible(m_view);
	setResizable(true, true);
	setResizeLimits(w / 2, h / 2, w * 2, h * 2);
	getConstrainer()->setFixedAspectRatio(double(w) / h);
	setSize(juce::roundToInt(w * scale), juce::roundToInt(h * scale));
	m_view.grabKeyboardFocus();
	if (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
		runSnapshotIfAsked();
}

SH101Editor::~SH101Editor() = default;

// Standalone self-test: --snapshot out.png [--scale x]
// Shows the view, waits for the LEDs to settle, writes a 2x PNG and quits.
void SH101Editor::runSnapshotIfAsked()
{
	const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
	const int at = args.indexOf("--snapshot");
	if (at < 0 || at + 1 >= args.size())
		return;
	const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(args[at + 1]);
	const int s = args.indexOf("--scale");
	const double scale = s >= 0 && s + 1 < args.size() ? args[s + 1].getDoubleValue() : 1.0;
	setSize(juce::roundToInt(sh101::HardwareView::kWidth * scale), juce::roundToInt(sh101::HardwareView::kHeight * scale));
	juce::Timer::callAfterDelay(2500, [safe = juce::Component::SafePointer<SH101Editor>(this), out] {
		if (safe == nullptr)
			return;
		const auto image = safe->createComponentSnapshot(safe->getLocalBounds(), true, 2.0f);
		out.deleteFile();
		juce::FileOutputStream stream(out);
		juce::PNGImageFormat().writeImageToStream(image, stream);
		stream.flush();
		juce::JUCEApplicationBase::quit();
	});
}

void SH101Editor::resized()
{
	if (getWidth() <= 0)
		return;
	const float scale = float(getWidth()) / float(sh101::HardwareView::kWidth);
	m_view.setBounds(0, 0, sh101::HardwareView::kWidth, sh101::HardwareView::kHeight);
	m_view.setTransform(juce::AffineTransform::scale(scale));
	m_proc.params().state.setProperty("scale", scale, nullptr);
}
