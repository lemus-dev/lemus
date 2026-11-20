// The manager's window: the released plugins in a list (machine, plugin, the version installed,
// its status), the selected one's details with what's new and Update, Install or Remove, the
// plug-in folder and the update check in the footer. Everything runs from the catalogue the
// website publishes (Catalog.h) and what is on disk (Installed.h); the work is a Batch (Tasks.h).
#pragma once

#include "Catalog.h"
#include "Installed.h"
#include "Tasks.h"
#include "Widgets.h"

#include <map>
#include <memory>
#include <optional>
#include <set>

namespace lemus::manager {

class MainComponent final : public juce::Component, private juce::AsyncUpdater {
public:
	MainComponent();
	~MainComponent() override;

	// A batch is under way: closing waits for it.
	bool busy() const;
	void sayBusy();

	// For --snapshot: the list has been read (or failed to be), select a plugin, start the work a
	// button would ("update-all", or "update", "install", "remove" for the selected plugin).
	bool ready() const { return m_loading != Loading::Reading; }
	void show(const juce::String &plugin) { select(plugin); }
	void act(const juce::String &action);
	// The list didn't read, or a job of the last batch failed.
	bool failed() const;

	void paint(juce::Graphics &) override;
	void resized() override;
	bool keyPressed(const juce::KeyPress &) override;

private:
	class List;
	class Detail;
	friend class List;
	friend class Detail;

	enum class Loading { Reading, Failed, Ready };

	// One line of the list.
	struct Status {
		juce::String text;
		juce::Colour colour, onSelection;
	};

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

	Loading m_loading = Loading::Reading;
	juce::String m_error;
	std::optional<Catalog> m_catalog;
	std::map<juce::String, Install> m_installed;
	juce::String m_selected;
	juce::String m_note; // a passing message in the header (e.g. why closing waits)

	std::unique_ptr<Batch> m_batch;
	std::vector<Job> m_jobs;

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
