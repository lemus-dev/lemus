// Installing, updating and removing: a batch of jobs, one per plugin. Every installer is
// downloaded first and checked against the SHA-256 the catalogue gives; one that doesn't match is
// thrown away. Then one step with administrator rights does the batch, so the system asks once:
//   macOS    a shell script run through osascript "with administrator privileges": the release's
//            own .pkg through /usr/sbin/installer (receipts as a manual install leaves them), the
//            formats not wanted switched off with -applyChoiceChangesXML; removing deletes the
//            bundles and forgets their receipts.
//   Windows  the app itself, started again elevated (--apply <plan>), copies the release zip's
//            VST3 and app into place and makes a Start menu shortcut, or deletes them.
// Under a test root (setRoot) nothing asks: the macOS script expands the .pkg and copies its
// bundles, the Windows plan runs in this process.
#pragma once

#include "Catalog.h"
#include "Installed.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <vector>

namespace lemus::manager {

struct Job {
	enum class Kind { Install, Update, Remove };
	enum class State { Waiting, Downloading, Checking, Ready, Installing, Done, Failed, Stopped };

	Kind kind = Kind::Install;
	Entry entry;
	std::vector<Format> formats; // Install, Update: the formats to have
	Install before;              // what was there (Remove deletes its copies)

	State state = State::Waiting;
	double progress = 0; // of the download, 0 to 1
	juce::String error;  // Failed, Stopped: why
	juce::File file;     // the checked installer

	bool finished() const { return state == State::Done || state == State::Failed || state == State::Stopped; }
};

juce::String sha256Of(const juce::File &);

// macOS: the -applyChoiceChangesXML file that switches off the given choices.
juce::String choicesOff(const juce::StringArray &choiceIds);
// The choices of a release .pkg that hold formats not in `wanted` (choice ids end .au, .vst3, .app).
juce::StringArray unwantedChoices(const juce::StringArray &choiceIds, const std::vector<Format> &wanted);
// The choice ids in `installer -showChoiceChangesXML` output.
juce::StringArray choiceIdsIn(const juce::String &showChoiceChangesXml);

// Windows: the elevated side. Runs the plan file's operations and writes what happened beside it
// (<plan>.result: {"<plugin>": "" or why it failed}). Returns false if the plan didn't read.
bool applyPlan(const juce::File &plan);

// Downloads `d` into `target` and checks its size and SHA-256: empty when it is there and right,
// else why not (the file is then gone). A file:// URL only under a test root. `progress` gets 0 to
// 1 as it comes, `checking` is called when the check starts, and `stop` returning true gives up.
juce::String fetchChecked(const Download &d, const juce::File &target, const std::function<void(double)> &progress,
                          const std::function<void()> &checking, const std::function<bool()> &stop);

// Runs jobs in the background: downloads, checks, then the one step with administrator rights.
class Batch final : private juce::Thread {
public:
	// `changed` is called on this thread whenever a job's state or progress moves.
	Batch(std::vector<Job> jobs, std::function<void()> changed);
	~Batch() override;

	void start() { startThread(); }
	std::vector<Job> jobs() const;
	bool done() const { return m_done; }
	// Stop after the download under way: the rest are not downloaded; what is ready still installs.
	void stop() { m_stopping = true; }
	bool stopping() const { return m_stopping; }

private:
	void run() override;
	bool download(size_t index);
	void installReady();
	void set(size_t index, const std::function<void(Job &)> &change);

	mutable juce::CriticalSection m_lock;
	std::vector<Job> m_jobs;
	std::function<void()> m_changed;
	juce::File m_work;
	std::atomic<bool> m_stopping{false}, m_done{false};
};

// The manager updating itself to the catalogue's own entry, as the plugin managers do: its
// installer downloaded and checked like a plugin's, then installed:
//   macOS    the .pkg through the installer, as root, asking for the password once (the app goes
//            to /Applications, where the release's pkg puts it);
//   Windows  the new exe in place of this one (this one renamed <exe>.old, removed on the next
//            start), with no prompt; a folder it can't write fails, and the page offers the zip.
// Under a test root the new app goes there instead (<root>/Applications, <root>/<exe>).
class SelfUpdate final : private juce::Thread {
public:
	enum class State { Downloading, Checking, Installing, Done, Failed, Stopped };

	// `changed` is called on this thread whenever the state or the download's progress moves.
	SelfUpdate(Entry manager, std::function<void()> changed);
	~SelfUpdate() override;

	void start() { startThread(); }
	const Entry &entry() const { return m_entry; }
	State state() const { return m_state; }
	double progress() const { return m_progress; }
	juce::String error() const; // Failed, Stopped: why
	bool finished() const;

private:
	void run() override;
	juce::String install(const juce::File &download);
	void set(State, const juce::String &error = {});

	Entry m_entry;
	std::function<void()> m_changed;
	juce::File m_work;
	std::atomic<State> m_state{State::Downloading};
	std::atomic<double> m_progress{0};
	mutable juce::CriticalSection m_lock;
	juce::String m_error;
};

// After an update: starts the new manager, which waits for this one to quit (--after <pid>);
// the caller then quits. False under a test root, or if it didn't start.
bool relaunch();
// --after <pid>: waits (up to 15 s) for the manager the update came from to quit.
void waitForQuit(int pid);
// Windows: removes the exe an update left beside this one (<exe>.old). Nothing elsewhere.
void removeOldSelf();

} // namespace lemus::manager
