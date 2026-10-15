#include "PluginEditor.h"

#include <cstdio>

TB303Editor::TB303Editor(TB303Processor &p) : AudioProcessorEditor(&p), m_proc(p), m_view(p)
{
	// Read first: setting the limits resizes the window, which saves its scale.
	const double scale = double(m_proc.params().state.getProperty("scale", 1.0));
	constexpr int w = HardwareView::kWidth, h = HardwareView::kHeight;
	addAndMakeVisible(m_view);
	setResizable(true, true);
	setResizeLimits(w / 2, h / 2, w * 2, h * 2);
	getConstrainer()->setFixedAspectRatio(double(w) / h);
	setSize(juce::roundToInt(w * scale), juce::roundToInt(h * scale));
	m_view.grabKeyboardFocus();
	if (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
		runSnapshotIfAsked();
}

TB303Editor::~TB303Editor() = default;

// Standalone self-test: --snapshot out.png [--scale x] [--run] [--service [--step TM-1]] [--service-test]
// Shows the view (with --service, the boards), waits for the LEDs to settle, writes a 2x PNG
// and quits.
void TB303Editor::runSnapshotIfAsked()
{
	const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
	const int at = args.indexOf("--snapshot");
	if (at < 0 || at + 1 >= args.size())
		return;
	const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(args[at + 1]);
	const int s = args.indexOf("--scale");
	const double scale = s >= 0 && s + 1 < args.size() ? args[s + 1].getDoubleValue() : 1.0;
	setSize(juce::roundToInt(HardwareView::kWidth * scale), juce::roundToInt(HardwareView::kHeight * scale));
	if (args.contains("--run"))
		m_proc.toggleRun();
	if (args.contains("--service-test")) {
		m_view.setService(false);
		for (const auto &line : m_view.serviceSelfTest())
			std::printf("%s\n", line.toRawUTF8());
		std::fflush(stdout);
	}
	if (args.contains("--service"))
		m_view.setService(true);
	const int step = args.indexOf("--step");
	if (step >= 0 && step + 1 < args.size())
		m_view.setServiceTrim(args[step + 1].getIntValue());
	const bool ringTest = args.contains("--service-test");
	if (ringTest) {
		// TM3's step as the notes set it up: the scope should time the unit's 2.5 ms ring on TP6.
		m_view.setService(true);
		m_view.setServiceTrim(2);
		for (const auto &[id, v] : {std::pair<const char *, float>{"cutoff", 0.5f}, {"resonance", 1.0f}, {"envmod", 0.0f}, {"decay", 0.0f}, {"accent", 0.0f}})
			m_proc.params().getParameter(id)->setValueNotifyingHost(v);
	}
	juce::Timer::callAfterDelay(2500, [safe = juce::Component::SafePointer<TB303Editor>(this), out, ringTest] {
		if (safe == nullptr)
			return;
		if (ringTest) {
			const auto r = safe->m_view.scopeReading();
			const double ms = r.fromFirstOccurrenceOf("ring ", false, false).getDoubleValue();
			std::printf("%s  the scope times TP6's ring at %s (the trims say %.2f ms)\n", std::abs(ms - 2.5) < 0.15 ? "ok   " : "FAIL ",
			            r.toRawUTF8(), tb303::trims::ringMs(safe->m_proc.trims()));
			std::fflush(stdout);
		}
		const auto image = safe->createComponentSnapshot(safe->getLocalBounds(), true, 2.0f);
		out.deleteFile();
		juce::FileOutputStream stream(out);
		juce::PNGImageFormat().writeImageToStream(image, stream);
		stream.flush();
		juce::JUCEApplicationBase::quit();
	});
}

void TB303Editor::resized()
{
	if (getWidth() <= 0)
		return;
	const float scale = float(getWidth()) / float(HardwareView::kWidth);
	m_view.resizing();
	m_view.setBounds(0, 0, HardwareView::kWidth, HardwareView::kHeight);
	m_view.setTransform(juce::AffineTransform::scale(scale));
	m_proc.params().state.setProperty("scale", scale, nullptr);
}
