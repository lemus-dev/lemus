// What each plugin needs from the user's ROM folder, and what the folder has: the ROM and artwork
// in each plugin's details, the list's ROM and ART columns, and files dropped on the window.
//
// The needs come from LEMUS_SITE/roms.json (beside latest.json; manager/specs.cpp writes the data
// from the machines' own specs). The folder is checked with the plugins' own code (host/Roms.h):
// a ROM is found by its SHA1 whatever it is called, loose or in a zip, and an artwork place by its
// file's name, so the manager and the plugin always agree. Lemus ships no ROMs: nothing here
// downloads one or says where to get one.
#pragma once

#include "host/Roms.h"

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <vector>

namespace lemus::manager {

// What one plugin needs.
struct Needs {
	juce::String machine;       // the machine's id, which keys the plugin's ROM folder
	std::vector<RomSpec> roms;  // none: a circuit with no ROM
	ArtworkSpec artwork;        // its places (none: no artwork); its plugin name is set
};
using NeedsMap = std::map<juce::String, Needs>; // by plugin name

// Empty if the text isn't a roms.json.
std::optional<NeedsMap> parseNeeds(const juce::String &text);
// roms.json beside the catalogue (catalogSource()); empty if it can't be read.
std::optional<NeedsMap> fetchNeeds();

// What a plugin's ROM folder has for it.
struct RomCheck {
	enum class Rom { NotNeeded, Ready, Reduced, Missing }; // Reduced: an optional one is missing
	enum class Art { NotNeeded, None, Partial, Ready };

	juce::File folder;
	struct Image {
		RomSpec spec;
		bool found = false;
		juce::File where; // the file it was found in (a zip for an entry)
	};
	std::vector<Image> images;
	struct Place {
		ArtworkPlace place;
		juce::String where; // its path in the folder; empty: none
	};
	std::vector<Place> places;

	Rom rom() const;
	Art art() const;
};
RomCheck check(const Needs &);

// Files dropped on the window: ROMs matched by SHA1 (loose files, in zips, in folders) and artwork
// by its name (<plugin>-<place>.svg or .png), each copied into its plugin's ROM folder under the
// name the plugin knows it by. What isn't a Lemus ROM or artwork is left alone.
struct Added {
	juce::String plugin, file; // file: the name it was copied under
	bool already = false;      // the folder had it already
};
std::vector<Added> addDropped(const juce::StringArray &paths, const NeedsMap &needs);
// "Added d650c-085.ic4 to ro-drums-808", "Added 3 files to 2 plugins", or why nothing was.
juce::String describe(const std::vector<Added> &);

} // namespace lemus::manager
