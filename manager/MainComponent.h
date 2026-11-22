// The manager's window: the released plugins in a list (machine, plugin, the version installed,
// its status), the selected one's details with what's new and Update, Install or Remove, the
// plug-in folder and the update check in the footer. Everything runs from the catalogue the
// website publishes (Catalog.h) and what is on disk (Installed.h); the work is a Batch (Tasks.h).
#pragma once

#include "Catalog.h"
#include "Installed.h"
#include "Report.h"
#include "RomCheck.h"
#include "Tasks.h"
#include "Widgets.h"

#include <map>
#include <memory>
#include <optional>
#include <set>

namespace lemus::manager {

class MainComponent final : public juce::Component, public juce::FileDragAndDropTarget, private juce::AsyncUpdater {
public:
	MainComponent();
	~MainComponent() override;

	// A batch, or the manager's own update, is under way: closing waits for it.
	bool busy() const;
	void sayBusy();

	// For --snapshot: the list has been read (or failed to be), select a plugin, start the work a
	// button would ("update-all", or "update", "install", "remove" for the selected plugin;
	// "self-update" and "later" for the prompt that offers a newer manager; "drag" shows the drop
	// overlay, "drop:<path>|<path>" drops those files).
	bool ready() const { return m_loading != Loading::Reading; }
	void show(const juce::String &plugin) { select(plugin); }
	void act(const juce::String &action);
	// The list didn't read, or a job of the last batch failed.
	bool failed() const;

	// Selects the plugin once the list has read (--select, the plugins' "Open Plugin Manager").
	void selectWhenLoaded(const juce::String &plugin);

	void paint(juce::Graphics &) override;
	void paintOverChildren(juce::Graphics &) override;
	void resized() override;
	bool keyPressed(const juce::KeyPress &) override;

	// ROMs and artwork dropped anywhere on the window (RomCheck.h addDropped).
	bool isInterestedInFileDrag(const juce::StringArray &) override;
	void fileDragEnter(const juce::StringArray &, int, int) override;
	void fileDragExit(const juce::StringArray &) override;
	void filesDropped(const juce::StringArray &, int, int) override;

private:
	class List;
	class Detail;
	class Prompt;
	friend class List;
	friend class Detail;
	friend class Prompt;

	enum class Loading { Reading, Failed, Ready };

	// One line of the list.
	struct Status {
		juce::String text;
		juce::Colour colour, onSelection;
	};

	// A batch of plugins is under way (busy() also counts the manager's own update).
	bool batchBusy() const;
	void load();
	void rescan();
	void refresh(); // the list, the details, the header and footer after any change

	std::vector<const Entry *> visible() const;
	const Entry *selected() const;
	void select(const juce::String &plugin);
	void moveSelection(int by);

	const Install &installOf(const Entry &) const;
	const Job *jobOf(const Entry &) const;
	bool hasUpdate(const Entry &) const;
	int updateCount() const;
	Status statusOf(const Entry &) const;
	juce::String summary() const;

	void startJobs(std::vector<Job>);
	void update(const Entry &);
	void install(const Entry &, const std::vector<Format> &);
	void remove(const Entry &);
	void updateAll();
	void handleAsyncUpdate() override;

	// What a plugin needs from its ROM folder, and what the folder has (null: not known, or not yet).
	const Needs *needsOf(const Entry &) const;
	const RomCheck *romCheckOf(const Entry &) const;
	void checkRoms(); // in the background, then refresh
	void say(const juce::String &note, bool ok); // a passing message in the header

	// The manager's own update: the catalogue's entry when it is newer than this app and has an
	// installer for this system (else the footer's pill opens its page).
	const Entry *newerManager() const;
	void showPrompt();
	void hidePrompt();
	void startSelfUpdate();
	// The last run crashed (crashFile()): a card says so first. Closing it deletes the report.
	void closeCrashNote();

	Loading m_loading = Loading::Reading;
	juce::String m_error;
	std::optional<Catalog> m_catalog;
	std::map<juce::String, Install> m_installed;
	juce::String m_selected;
	juce::String m_note; // a passing message in the header (e.g. why closing waits)
	bool m_noteOk = false;
	std::optional<NeedsMap> m_needs;
	std::map<juce::String, RomCheck> m_romChecks;
	bool m_dragging = false;
	juce::String m_selectWhenLoaded;

	std::unique_ptr<Batch> m_batch;
	std::vector<Job> m_jobs;

	std::unique_ptr<SelfUpdate> m_self;
	std::unique_ptr<Prompt> m_prompt;
	bool m_promptOffered = false; // shown once a launch; the pill shows it again
	bool m_relaunchTried = false, m_relaunched = false;
	juce::String m_crash; // the last run's crash report, until its card is closed

	SearchField m_search;
	Pill m_updateAll{"Update all", Pill::Kind::Outline};
	Pill m_retry{"Try again"};
	Pill m_show{"Show"};
	Pill m_managerUpdate{{}, Pill::Kind::Outline};
	Check m_daily{"Check for updates once a day"};
	std::unique_ptr<List> m_list;
	std::unique_ptr<juce::Viewport> m_listView;
	std::unique_ptr<Detail> m_detail;

	JUCE_DECLARE_WEAK_REFERENCEABLE(MainComponent)
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace lemus::manager
