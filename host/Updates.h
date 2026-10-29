// The update check. Once a day, while a Lemus window is open, the plugin reads one static file
// from the website, LEMUS_SITE/latest.json, which lists each released plugin's newest version,
// the page to get it from and what its recent versions changed:
//   {"plugins": {"ro-mono-303": {"version": "0.2.0", "url": "https://lemusdev.org/ro-mono-303",
//                                "changes": [{"version": "0.2.0", "notes": ["...", "..."]}]}}}
// The bar says when there's a newer release of this plugin (JucePlugin_Name), and a pop-up says
// what's new in it (popUpDue). A file-level
// "version" and "url" stand for any plugin the file doesn't list. The check sends nothing about the
// user, the host or the session: a plain GET with no query, no cookies and a fixed user agent. It
// can be switched off in the About box; the setting, and the file the last check fetched, are
// shared by every instance and every plugin. Only the plugins check (LEMUS_UPDATE_CHECK); the test
// programs never go online.
#pragma once

#include <juce_events/juce_events.h>

#include <vector>

namespace lemus::updates {

bool enabled();
void setEnabled(bool on);

// What a version changed, as its release notes list it.
struct Change {
	juce::String version;
	juce::StringArray notes;
};

struct Release {
	juce::String version;        // empty: nothing newer known
	juce::String url;            // its download page
	std::vector<Change> changes; // newest first
};
// The newest release of this plugin the last check found, if it's newer than this build, with the
// changes of every version after this build's.
Release newer();

// A plugin's release in latest.json's text: its entry, or the file's own if it has none. With
// `since`, only the changes of the versions after that one, up to the release, are kept.
Release releaseOf(const juce::String &latestJson, const juce::String &plugin, const juce::String &since = {});

// The what's-new pop-up for a newer release: due unless its version was skipped, or the pop-up
// showed for it less than a week ago. The answers are in the shared settings, so the pop-up shows
// in one window, not in every instance.
bool popUpDue(const Release &);
void poppedUp(const Release &); // it has shown: again in a week, if that version is still the newest
void skip(const Release &);     // Skip this version: not again for this one
// The same with the settings' values passed in: the version skipped, and "<version> <ms>", when
// the pop-up last showed and for which version.
bool popUpDue(const Release &, const juce::String &skipped, const juce::String &shown, juce::int64 now);

// True if version a is newer than b (dot-separated numbers: 0.10.0 is newer than 0.9.1).
bool isNewer(const juce::String &a, const juce::String &b);

// The check, shared by the open windows: listen to it to hear when a check has finished.
class Checker final : public juce::Thread, public juce::ChangeBroadcaster {
public:
	Checker();
	~Checker() override;

	// Starts a check in the background if checks are on and the last was a day or more ago.
	void checkIfDue();

private:
	void run() override;

	juce::CriticalSection m_lock;
	juce::WebInputStream *m_stream = nullptr; // the request in flight, so closing can cancel it
};

} // namespace lemus::updates
