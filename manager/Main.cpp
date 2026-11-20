// Lemus Plugin Manager.
//   Lemus Plugin Manager [--catalog <file or URL>] [--root <folder>]
//     --catalog  read the list of plugins from there instead of LEMUS_SITE/latest.json
//     --root     install under this folder instead of the system's, asking for nothing (tests)
//   Lemus Plugin Manager --snapshot <png> [--select <plugin>] [--action <what>] [--wait <ms>] [--until-done]
//     render the window to a PNG once the list is read (and the action has run for --wait ms, or
//     with --until-done, has finished); the exit code is 1 if a job failed
//   Lemus Plugin Manager --apply <plan.json>
//     Windows: run an install plan with the rights this process was started with (the app starts
//     itself this way, elevated, so Windows asks once per batch); writes <plan>.result.
#include "MainComponent.h"

namespace lemus::manager {

class Window final : public juce::DocumentWindow {
public:
	Window() : DocumentWindow(JUCE_APPLICATION_NAME_STRING, col::ground, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
	{
		setUsingNativeTitleBar(true);
		setContentOwned(new MainComponent(), true);
		setResizable(false, false);
		centreWithSize(getWidth(), getHeight());
		setVisible(true);
	}

	void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

	MainComponent *main() const { return dynamic_cast<MainComponent *>(getContentComponent()); }
};

// --snapshot: the window's content drawn off screen, saved as a PNG.
class Snapshot final : private juce::Timer {
public:
	Snapshot(juce::File png, juce::String select, juce::String action, int waitMs, bool untilDone)
		: m_png(std::move(png)), m_select(std::move(select)), m_action(std::move(action)), m_wait(waitMs), m_untilDone(untilDone)
	{
		m_main.setVisible(true);
		startTimer(50);
	}

private:
	void timerCallback() override
	{
		if (!m_main.ready())
			return;
		if (m_readyAt == 0) {
			m_readyAt = juce::Time::getMillisecondCounter();
			if (m_select.isNotEmpty())
				m_main.show(m_select);
			if (m_action.isNotEmpty())
				m_main.act(m_action);
		}
		if (juce::Time::getMillisecondCounter() - m_readyAt < juce::uint32(m_wait) || (m_untilDone && m_main.busy()))
			return;
		stopTimer();
		juce::JUCEApplication::getInstance()->setApplicationReturnValue(m_main.failed() ? 1 : 0);
		const auto image = m_main.createComponentSnapshot(m_main.getLocalBounds(), true, 2.0f);
		m_png.deleteFile();
		juce::FileOutputStream out(m_png);
		juce::PNGImageFormat().writeImageToStream(image, out);
		out.flush();
		juce::JUCEApplication::getInstance()->systemRequestedQuit();
	}

	MainComponent m_main;
	juce::File m_png;
	juce::String m_select, m_action;
	int m_wait;
	bool m_untilDone;
	juce::uint32 m_readyAt = 0;
};

class App final : public juce::JUCEApplication {
public:
	const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
	const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
	// The elevated copy (--apply) runs beside the window that started it.
	bool moreThanOneInstanceAllowed() override { return getCommandLineParameterArray().contains("--apply"); }

	void initialise(const juce::String &) override
	{
		const auto args = getCommandLineParameterArray();
		const auto value = [&args](const char *flag) {
			const int i = args.indexOf(flag);
			return i >= 0 && i + 1 < args.size() ? args[i + 1].unquoted() : juce::String();
		};
		if (args.contains("--apply")) {
			const juce::File plan(value("--apply"));
			setApplicationReturnValue(plan.existsAsFile() && applyPlan(plan) ? 0 : 1);
			quit();
			return;
		}
		if (const auto catalog = value("--catalog"); catalog.isNotEmpty())
			setCatalogSource(catalog);
		if (const auto r = value("--root"); r.isNotEmpty())
			setRoot(juce::File::getCurrentWorkingDirectory().getChildFile(r));
		if (const auto png = value("--snapshot"); png.isNotEmpty()) {
			const auto wait = value("--wait");
			m_snapshot = std::make_unique<Snapshot>(juce::File::getCurrentWorkingDirectory().getChildFile(png), value("--select"), value("--action"),
			                                        wait.isNotEmpty() ? wait.getIntValue() : 300, args.contains("--until-done"));
			return;
		}
		m_window = std::make_unique<Window>();
	}

	void shutdown() override
	{
		m_window.reset();
		m_snapshot.reset();
	}

	void systemRequestedQuit() override
	{
		if (m_snapshot != nullptr) {
			quit();
			return;
		}
		if (m_window != nullptr && m_window->main() != nullptr && m_window->main()->busy()) {
			m_window->main()->sayBusy();
			return;
		}
		quit();
	}

	void anotherInstanceStarted(const juce::String &) override
	{
		if (m_window != nullptr)
			m_window->toFront(true);
	}

private:
	std::unique_ptr<Window> m_window;
	std::unique_ptr<Snapshot> m_snapshot;
};

} // namespace lemus::manager

START_JUCE_APPLICATION(lemus::manager::App)
