#include "PluginEditor.h"

TR909Editor::TR909Editor(TR909Processor &p) : AudioProcessorEditor(&p), m_proc(p), m_view(p)
{
	// Read first: setting the limits resizes the window, which saves its scale.
	const double scale = double(m_proc.params().state.getProperty("scale", 1.0));
	constexpr int w = tr909::HardwareView::kWidth, h = tr909::HardwareView::kHeight;
	addAndMakeVisible(m_view);
	setResizable(true, true);
	setResizeLimits(w / 2, h / 2, w * 2, h * 2);
	getConstrainer()->setFixedAspectRatio(double(w) / h);
	setSize(juce::roundToInt(w * scale), juce::roundToInt(h * scale));
	m_view.grabKeyboardFocus();
	if (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
		runSnapshotIfAsked();
}

TR909Editor::~TR909Editor() = default;

// Standalone self-test: --snapshot out.png [--scale x] [--run] [--keys]
// Shows the view, optionally starts the sequencer, waits for the LEDs to settle, writes a 2x PNG
// and quits.
void TR909Editor::runSnapshotIfAsked()
{
	const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
	const int at = args.indexOf("--snapshot");
	if (at < 0 || at + 1 >= args.size())
		return;
	const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(args[at + 1]);
	const int s = args.indexOf("--scale");
	const double scale = s >= 0 && s + 1 < args.size() ? args[s + 1].getDoubleValue() : 1.0;
	setSize(juce::roundToInt(tr909::HardwareView::kWidth * scale), juce::roundToInt(tr909::HardwareView::kHeight * scale));
	if (args.contains("--keys"))
		m_view.showKeys(true);
	if (args.contains("--run")) {
		m_proc.setKey(TR909Processor::Key::Start, true);
		juce::Timer::callAfterDelay(100, [safe = juce::Component::SafePointer<TR909Editor>(this)] {
			if (safe != nullptr)
				safe->m_proc.setKey(TR909Processor::Key::Start, false);
		});
	}
	juce::Timer::callAfterDelay(2500, [safe = juce::Component::SafePointer<TR909Editor>(this), out] {
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

void TR909Editor::resized()
{
	if (getWidth() <= 0)
		return;
	const float scale = float(getWidth()) / float(tr909::HardwareView::kWidth);
	m_view.resizing();
	m_view.setBounds(0, 0, tr909::HardwareView::kWidth, tr909::HardwareView::kHeight);
	m_view.setTransform(juce::AffineTransform::scale(scale));
	m_proc.params().state.setProperty("scale", scale, nullptr);
}
