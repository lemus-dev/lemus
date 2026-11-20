#include "PluginEditor.h"

RatEditor::RatEditor(RatProcessor &p) : AudioProcessorEditor(&p), m_proc(p), m_view(p)
{
	// Read first: setting the limits resizes the window, which saves its scale.
	const double scale = double(m_proc.params().state.getProperty("scale", 1.0));
	constexpr int w = HardwareView::kWidth, h = HardwareView::kHeight;
	addAndMakeVisible(m_view);
	setResizable(true, true);
	setResizeLimits(w / 2, h / 2, w * 2, h * 2);
	getConstrainer()->setFixedAspectRatio(double(w) / h);
	setSize(juce::roundToInt(w * scale), juce::roundToInt(h * scale));
	if (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
		runSnapshotIfAsked();
}

// Standalone self-test: --snapshot out.png [--scale x] [--signal]
// Shows the view (with a test signal if asked), waits for the LED to settle, writes a 2x PNG and
// quits.
void RatEditor::runSnapshotIfAsked()
{
	const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
	const int at = args.indexOf("--snapshot");
	if (at < 0 || at + 1 >= args.size())
		return;
	const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(args[at + 1]);
	const int s = args.indexOf("--scale");
	const double scale = s >= 0 && s + 1 < args.size() ? args[s + 1].getDoubleValue() : 1.0;
	setSize(juce::roundToInt(HardwareView::kWidth * scale), juce::roundToInt(HardwareView::kHeight * scale));
	m_proc.setTestSignal(args.contains("--signal"));
	juce::Timer::callAfterDelay(3000, [safe = juce::Component::SafePointer<RatEditor>(this), out] {
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

RatEditor::~RatEditor() = default;

void RatEditor::resized()
{
	if (getWidth() <= 0)
		return;
	const float scale = float(getWidth()) / float(HardwareView::kWidth);
	m_view.setBounds(0, 0, HardwareView::kWidth, HardwareView::kHeight);
	m_view.setTransform(juce::AffineTransform::scale(scale));
	m_proc.params().state.setProperty("scale", scale, nullptr);
}
