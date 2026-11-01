// ROMs. Lemus ships no ROM data: each machine lists the images it needs, users keep their own
// dumps in a ROM folder, and the host finds each image by its SHA1, whatever the file is called.
// A loose file or a zip (MAME's sets work as they are) anywhere in the folder will do. Artwork
// lives in the same folder: a file per place on the panel (artwork/<machine>/<place>.svg or .png),
// or an older single-SVG pack found by name.
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

// A place on a machine's panel that takes a picture of the user's own (host/Artwork.h): a brand
// mark Lemus doesn't include. Its file has a plain name (no brand in it, and the same across
// machines: maker, name, model, tagline...).
struct ArtworkPlace {
	juce::String id;    // the file's name without its extension: "maker" takes maker.svg or maker.png
	juce::String mark;  // the mark it is, as the panel (and an older pack) names it, e.g. "roland"
	juce::String label; // for the Artwork pop-up, e.g. "Roland logo"
	float width = 0, height = 0; // its box on the panel, mm
};

// A machine's artwork (host/Artwork.h). With places, as MAME does it: each place's file is in
// artwork/<folder>/ in the ROM folder (or artwork/<folder>.zip), where <folder> is the pack's name
// without ".svg", found by name when the machine loads; the Artwork pop-up on the bar lists the
// names. The older single-SVG pack, found by name and used whatever it holds, still fills the
// places that have no file.
struct ArtworkSpec {
	juce::String name; // e.g. "tb303.svg"
	// A blank pack: an SVG with the machine's mark slots commented out, so it draws nothing. Without
	// places, the host writes it into the ROM folder for the user to fill in; with places, it only
	// recognises untouched copies an earlier release wrote, and takes them out (none: nullptr).
	const char *blank = nullptr;
	std::vector<ArtworkPlace> places;
	// The most panel pixels per millimetre the machine's editor draws (largest window on a
	// high-density screen): a PNG this sharp stays sharp.
	float sharpPxPerMm = 0;

	juce::String folder() const { return name.upToLastOccurrenceOf(".", false, false); }
	// The RomSet name a place's file goes under (host/Artwork.h reads it from there).
	static juce::String key(const juce::String &place) { return "artwork/" + place; }
};

struct ArtworkFile {
	juce::MemoryBlock data; // empty: none found
	juce::String where;     // its path within the folder (for a zip, "zip/entry")
};

// A place's own file.
struct PlaceFile {
	juce::String place;
	juce::MemoryBlock data;
	juce::String where; // its path within the ROM folder (for a zip, "zip/entry")
	juce::File file;    // the loose file, or none for a zip entry
};

// The images a machine was given, by name.
class RomSet {
public:
	const juce::MemoryBlock *find(const juce::String &name) const;
	void add(const juce::String &name, const juce::MemoryBlock &data, const juce::File &where = {})
	{
		m_images[name] = data;
		m_where[name] = where;
	}
	bool has(const juce::String &name) const { return find(name) != nullptr; }
	// Where it was found: the file, or for a zip entry the zip's path with the entry's after it.
	juce::File where(const juce::String &name) const
	{
		const auto it = m_where.find(name);
		return it != m_where.end() ? it->second : juce::File();
	}

private:
	std::map<juce::String, juce::MemoryBlock> m_images;
	std::map<juce::String, juce::File> m_where;
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

// Finds a machine's single-SVG artwork pack: a file with the pack's name (any case) anywhere in the
// folder, or an SVG in a zip named after it (tb303.zip for tb303.svg), outside the places' own
// artwork/<folder>/ and artwork/<folder>.zip. A pack with no marks (a blank one not filled in yet)
// doesn't count. If there are several, the first by path wins.
ArtworkFile findArtwork(const juce::File &folder, const ArtworkSpec &spec);

// The places' own files: <place>.svg or <place>.png (any case; SVG first) in artwork/<folder>/,
// or else in artwork/<folder>.zip (anywhere in it), in the order of the places.
std::vector<PlaceFile> findPlaceFiles(const juce::File &folder, const ArtworkSpec &spec);
// Where the places' files go.
juce::File placeFolder(const juce::File &folder, const ArtworkSpec &spec);

// Everything of a machine's artwork the folder has: its pack and its places' own files.
struct ArtworkFound {
	ArtworkFile pack;
	std::vector<PlaceFile> places;
	bool empty() const { return pack.data.isEmpty() && places.empty(); }
};
ArtworkFound findAllArtwork(const juce::File &folder, const ArtworkSpec &spec);
// Adds it to `roms` for the machine's driver: the pack under its name, each place's file under
// ArtworkSpec::key.
void addArtwork(RomSet &roms, const ArtworkSpec &spec, const ArtworkFound &found);

// Writes the machine's blank pack into the folder, under the pack's name, if the folder has no
// file of that name (any case) anywhere in it. Creates the folder only if it is the default one.
// Never for a machine with places: those take a file per place from the Artwork pop-up. Returns
// whether it wrote one.
bool installBlankArtwork(const juce::File &folder, const ArtworkSpec &spec);

// Deletes the blank pack installBlankArtwork wrote into the folder, if it's still there untouched,
// so a ROM folder left behind doesn't keep a pack that does nothing (and, for a machine with
// places, whenever it loads: earlier releases wrote one). A pack with any change is kept. Returns
// whether it deleted one.
bool removeBlankArtwork(const juce::File &folder, const ArtworkSpec &spec);

} // namespace romfolder

juce::String sha1Hex(const void *data, size_t size);
juce::String crc32Hex(const void *data, size_t size);

} // namespace lemus
