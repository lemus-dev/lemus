// What is installed, and where things go. Plugins are installed for every user, where the
// installers put them and where every DAW looks:
//   macOS    AU  /Library/Audio/Plug-Ins/Components/<plugin>.component
//            VST3 /Library/Audio/Plug-Ins/VST3/<plugin>.vst3
//            App /Applications/<plugin>.app
//   Windows  VST3 C:\Program Files\Common Files\VST3\<plugin>.vst3
//            App C:\Program Files\Lemus\<plugin>\<plugin>.exe (with the licence and notices)
// A copy in the user's own ~/Library/Audio/Plug-Ins (a development build, or one copied there by
// hand) isn't the manager's, but it's reported: a DAW may load it instead.
#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace lemus::manager {

enum class Format { AU, VST3, App };
juce::String nameOf(Format);
// The formats there are on this system, the app only if the plugin has one.
std::vector<Format> formatsHere(bool standalone);

struct Places {
	juce::File au, vst3, apps; // for every user (no AU on Windows)
	juce::File userAu, userVst3; // the user's own (macOS only)
	juce::File startMenu;       // Windows: the Start menu's Lemus folder
	juce::File pluginFolder() const; // the folder the footer shows
};
// Under `root` instead of the system's own folders, as for tests and --root (empty to stop).
void setRoot(const juce::File &root);
juce::File root();
Places places();

// Where a format's copy of a plugin goes.
juce::File bundleOf(const Places &, Format, const juce::String &plugin);

struct Copy {
	Format format;
	juce::File file;
	juce::String version; // as the copy says, or empty
};

struct Install {
	std::vector<Copy> copies;     // for every user: the manager's
	std::vector<Copy> userCopies; // in the user's own folders
	bool installed() const { return !copies.empty(); }
	bool has(Format) const;
	std::vector<Format> formats() const;
	// The installed version: the oldest of the copies (so a half-finished update still shows).
	juce::String version() const;
};
Install scan(const juce::String &plugin);

// A copy's version: CFBundleShortVersionString in a macOS bundle's Info.plist; a Windows VST3's
// Contents/Resources/moduleinfo.json; a Windows exe's version info.
juce::String versionOf(const juce::File &copy, Format);
// The "Version" in a moduleinfo.json's text (JSON5: trailing commas, so not juce::JSON).
juce::String moduleInfoVersion(const juce::String &text);
// CFBundleShortVersionString in an Info.plist's text (XML).
juce::String plistVersion(const juce::String &text);

} // namespace lemus::manager
