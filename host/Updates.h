// The update check. Once a day, while a Lemus window is open, the plugin reads one static file
// from the website, LEMUS_SITE/latest.json, which lists each released plugin's newest version
// and the page to get it from:
//   {"plugins": {"ro-mono-303": {"version": "0.2.0", "url": "https://lemusdev.org/ro-mono-303"}}}
// The bar says when there's a newer release of this plugin (JucePlugin_Name). A file-level
// "version" and "url" stand for any plugin the file doesn't list. The check sends nothing about the
// user, the host or the session: a plain GET with no query, no cookies and a fixed user agent. It
// can be switched off in the About box; the setting, and the file the last check fetched, are
// shared by every instance and every plugin. Only the plugins check (LEMUS_UPDATE_CHECK); the test
// programs never go online.
#pragma once

#include <juce_events/juce_events.h>

namespace lemus::updates {

bool enabled();
void setEnabled(bool on);

struct Release {
	juce::String version; // empty: nothing newer known
	juce::String url;     // its download page
};
// The newest release of this plugin the last check found, if it's newer than this build.
Release newer();

// A plugin's release in latest.json's text: its entry, or the file's own if it has none.
Release releaseOf(const juce::String &latestJson, const juce::String &plugin);

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
