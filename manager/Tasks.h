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

} // namespace lemus::manager
