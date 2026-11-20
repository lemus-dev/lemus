// What the website has: LEMUS_SITE/latest.json, the file the plugins' update check reads, lists
// each released plugin with its newest version, its page, what its recent versions changed and
// its installers. The manager reads the same file:
//   {"plugins": {"ro-mono-303": {"version": "0.2.5", "url": "https://lemusdev.org/ro-mono-303",
//     "changes": [...], "emulates": "Roland TB-303 Bass Line", "year": 1981,
//     "type": "mono synth", "stage": "beta", "standalone": true, "withoutRom": "voice",
//     "downloads": {"mac": {"url": "...pkg", "sha256": "...", "size": 16292488},
//                   "windows": {"url": "...zip", "sha256": "...", "size": 7000732}}},
//   "lemus-plugin-manager": {"version": "0.1.0", "url": "...", "downloads": {...}}}}
// Every entry is a plugin but the manager's own, "lemus-plugin-manager".
#pragma once

#include "host/Updates.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace lemus::manager {

// One installer: where to get it and what it must hash to.
struct Download {
	juce::String url;
	juce::String sha256; // lower-case hex
	juce::int64 size = 0;
	bool valid() const { return url.startsWithIgnoreCase("https://") && sha256.length() == 64; }
};

struct Entry {
	juce::String plugin;   // its name, e.g. "ro-mono-303"
	juce::String emulates; // e.g. "Roland TB-303 Bass Line"
	juce::String type;     // e.g. "mono synth"
	int year = 0;
	juce::String version; // the newest
	juce::String page;    // its page on the website
	juce::String stage;   // "beta", or empty
	bool standalone = true;
	juce::String withoutRom; // "voice" (runs reduced), "none" (doesn't run), "not-needed" (has no ROM)
	Download mac, windows;

	// The installer for this system.
	const Download &here() const;
	// The machine's name for a list: the maker and model ("Roland TB-303"), without what follows the
	// model ("Bass Line"); a name with no number in it stays whole ("Access Virus C").
	juce::String shortName() const;
	// "Mono synth · 1981 · ro-mono-303"
	juce::String subtitle() const;
};

struct Catalog {
	juce::String text;           // the file as read, for each plugin's changes
	std::vector<Entry> plugins;  // by machine name
	std::optional<Entry> manager; // the manager's own release

	// Empty if the text isn't a catalogue.
	static std::optional<Catalog> parse(const juce::String &text);

	const Entry *find(const juce::String &plugin) const;
	// What changed after `since` up to the newest, newest first (all of it if `since` is empty).
	std::vector<updates::Change> changesSince(const Entry &, const juce::String &since) const;
};

// A plugin's name as the catalogue may give it: lower-case letters and digits in words joined by
// single hyphens ("ro-mono-303"). Names go into file paths and into the script that runs as root,
// so the catalogue skips any other.
bool isPluginName(const juce::String &);
// A version as the catalogue may give it: letters, digits, '.', '+' and '-' ("0.2.5", "1.0.0-rc1").
bool isVersion(const juce::String &);

// Where the catalogue comes from: LEMUS_SITE/latest.json, or a file or URL given instead
// (--catalog, for tests and before the website has the manager's fields).
void setCatalogSource(const juce::String &fileOrUrl);
juce::String catalogSource();

// Reads it (blocking). On failure, empty and `error` says why.
std::optional<Catalog> fetchCatalog(juce::String &error);

// The user agent of the manager's requests: not the plugins' "Lemus", so the website's count of
// installs (requests for latest.json with that agent) stays the plugins'.
inline const char *kUserAgent = "Lemus Plugin Manager";

} // namespace lemus::manager
