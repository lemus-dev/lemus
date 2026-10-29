// ROMs. Lemus ships no ROM data: each machine lists the images it needs, users keep their own
// dumps in a ROM folder, and the host finds each image by its SHA1, whatever the file is called.
// A loose file or a zip (MAME's sets work as they are) anywhere in the folder will do. Artwork
// packs live in the same folder but are found by name, and any pack is used.
#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <vector>

namespace juce {
class PropertiesFile;
}

namespace lemus {

// Lemus's settings, shared by every instance and both hosts: the ROM folder, the update check.
std::unique_ptr<juce::PropertiesFile> settings();

// Whether the prompt shown the first time a machine starts without an optional ROM has been
// answered (host/LemusEditor.cpp): one setting per machine, shared by every instance. It is cleared
// when the machine loads with every ROM, so the prompt shows again if one goes missing later. While a
// test ROM folder is set (romfolder::setOverride), it is kept in memory instead.
bool romPromptAnswered(const juce::String &machineId);
void setRomPromptAnswered(const juce::String &machineId, bool answered);

struct RomSpec {
	juce::String name;  // MAME's name for the image, e.g. "d650c-133.ic8"
	size_t size = 0;
	juce::String sha1;  // lower-case hex
	juce::String crc32; // lower-case hex, as MAME and ROM managers list it
	// An optional image: the machine also runs without it, doing less. `without` says what it
	// lacks then, e.g. the TB-303 without its CPU ROM is a voice played from MIDI.
	bool optional = false;
	juce::String without;
	// For the bar and the picker: what the image is ("CPU ROM") and, for an optional one, what
	// the machine is without it ("Voice mode").
	juce::String what = "ROM";
	juce::String mode;
	// For the prompt the first time the machine starts without this optional image: what the
	// image holds, and what the machine does and doesn't do in its mode without it.
	juce::String about;
	juce::StringArray modeHas, modeLacks;
};

// A machine's artwork pack (host/Artwork.h): found by name, and used whatever it holds.
struct ArtworkSpec {
	juce::String name; // e.g. "tb303.svg"
	// A blank pack: an SVG with the machine's mark slots commented out, so it draws nothing. The
	// host writes it into the ROM folder for the user to fill in (none: nullptr).
	const char *blank = nullptr;
};

struct ArtworkFile {
	juce::MemoryBlock data; // empty: none found
	juce::String where;     // its path within the folder (for a zip, "zip/entry")
};

// The images a machine was given, by name.
class RomSet {
public:
	const juce::MemoryBlock *find(const juce::String &name) const;
	void add(const juce::String &name, const juce::MemoryBlock &data) { m_images[name] = data; }
	bool has(const juce::String &name) const { return find(name) != nullptr; }

private:
	std::map<juce::String, juce::MemoryBlock> m_images;
};

namespace romfolder {

// Where the user's ROMs are: a setting shared by every Lemus instance and both hosts, by default
// ~/Library/Application Support/Lemus/ROMs.
juce::File location();
void setLocation(const juce::File &folder);
juce::File defaultLocation();
// Tests: use this folder instead, without touching the user's settings (empty to stop).
void setOverride(const juce::File &folder);

// Finds every image in `roms` in the folder. Images not found are added to `missing`.
RomSet load(const juce::File &folder, const std::vector<RomSpec> &roms, std::vector<RomSpec> *missing = nullptr);

// Finds a machine's artwork pack: a file with the pack's name (any case) anywhere in the folder,
// or an SVG in a zip named after it (tb303.zip for tb303.svg). A pack with no marks (a blank one
// not filled in yet) doesn't count. If there are several, the first by path wins.
ArtworkFile findArtwork(const juce::File &folder, const ArtworkSpec &spec);

// Writes the machine's blank pack into the folder, under the pack's name, if the folder has no
// file of that name (any case) anywhere in it. Creates the folder only if it is the default one.
// Returns whether it wrote one.
bool installBlankArtwork(const juce::File &folder, const ArtworkSpec &spec);

// Deletes the blank pack installBlankArtwork wrote into the folder, if it's still there untouched,
// so a ROM folder left behind doesn't keep a pack that does nothing. A pack with any change is kept.
// Returns whether it deleted one.
bool removeBlankArtwork(const juce::File &folder, const ArtworkSpec &spec);

} // namespace romfolder

juce::String sha1Hex(const void *data, size_t size);
juce::String crc32Hex(const void *data, size_t size);

} // namespace lemus
