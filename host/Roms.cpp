#include "Roms.h"

#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <array>
#include <memory>
#include <set>
#include <utility>

namespace lemus {

const juce::MemoryBlock *RomSet::find(const juce::String &name) const
{
	const auto it = m_images.find(name);
	return it != m_images.end() ? &it->second : nullptr;
}

namespace {

juce::String hex(const uint8_t *bytes, size_t n)
{
	static const char digits[] = "0123456789abcdef";
	juce::String s;
	s.preallocateBytes(n * 2);
	for (size_t i = 0; i < n; ++i) {
		s += digits[bytes[i] >> 4];
		s += digits[bytes[i] & 15];
	}
	return s;
}

// Another dump of an image that goes about under the same name, and the bytes that make it the one
// the machines name. The image a machine gets is then always the one its spec names.
struct OtherDump {
	const char *sha1;   // this dump's
	const char *wanted; // the spec's
	std::vector<std::pair<uint16_t, uint8_t>> bytes;
};

const std::vector<OtherDump> &knownDumps()
{
	static const std::vector<OtherDump> dumps{
	    // MAME's hd44780_a00.bin (the QuadraVerb GT's and DX7's LCD font) as drawn from the HD44780
	    // datasheet (CRC 01d108e2), still in many ROM sets: six characters differ from the corrected
	    // one (CRC e459877c), among them 7 and w.
	    {"bc0cdf0c9ba895f22e183c7bd35a3f655f2ca96f",
	     "65cf075a988cdcbb316b9afdd0529b374a1a65ec",
	     {{0x371, 0x01}, {0x372, 0x02}, {0x373, 0x04}, {0x374, 0x08}, {0x375, 0x08}, {0x376, 0x08},
	      {0x773, 0x11}, {0xaa2, 0x00}, {0xaa3, 0x1f}, {0xf55, 0x13}, {0xf56, 0x0d}, {0xf57, 0x00},
	      {0xfd1, 0x00}, {0xfd2, 0x04}, {0xfd3, 0x00}, {0xfd4, 0x1f}, {0xfd5, 0x00}, {0xfd6, 0x04}}},
	};
	return dumps;
}

} // namespace

juce::String sha1Hex(const void *data, size_t size)
{
	auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
	std::vector<uint8_t> m(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
	const uint64_t bits = uint64_t(size) * 8;
	m.push_back(0x80);
	while (m.size() % 64 != 56)
		m.push_back(0);
	for (int i = 7; i >= 0; --i)
		m.push_back(uint8_t(bits >> (i * 8)));

	uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
	for (size_t off = 0; off < m.size(); off += 64) {
		std::array<uint32_t, 80> w;
		for (size_t i = 0; i < 16; ++i)
			w[i] = uint32_t(m[off + 4 * i]) << 24 | uint32_t(m[off + 4 * i + 1]) << 16 | uint32_t(m[off + 4 * i + 2]) << 8 | m[off + 4 * i + 3];
		for (size_t i = 16; i < 80; ++i)
			w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
		for (size_t i = 0; i < 80; ++i) {
			uint32_t f, k;
			if (i < 20) {
				f = (b & c) | (~b & d);
				k = 0x5a827999;
			} else if (i < 40) {
				f = b ^ c ^ d;
				k = 0x6ed9eba1;
			} else if (i < 60) {
				f = (b & c) | (b & d) | (c & d);
				k = 0x8f1bbcdc;
			} else {
				f = b ^ c ^ d;
				k = 0xca62c1d6;
			}
			const uint32_t t = rol(a, 5) + f + e + k + w[i];
			e = d;
			d = c;
			c = rol(b, 30);
			b = a;
			a = t;
		}
		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
		h[4] += e;
	}
	uint8_t digest[20];
	for (size_t i = 0; i < 20; ++i)
		digest[i] = uint8_t(h[i / 4] >> (24 - 8 * (i % 4)));
	return hex(digest, 20);
}

juce::String crc32Hex(const void *data, size_t size)
{
	uint32_t crc = 0xffffffff;
	const auto *p = static_cast<const uint8_t *>(data);
	for (size_t i = 0; i < size; ++i) {
		crc ^= p[i];
		for (int b = 0; b < 8; ++b)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	crc = ~crc;
	const uint8_t bytes[] = {uint8_t(crc >> 24), uint8_t(crc >> 16), uint8_t(crc >> 8), uint8_t(crc)};
	return hex(bytes, 4);
}

juce::String alternativesText(const RomSpec &spec, bool hashes)
{
	juce::String s;
	for (const auto &alt : spec.alternatives) {
		s << juce::String::fromUTF8(" \xc2\xb7 or ") << alt.name << " (" << alt.label << ")";
		if (hashes)
			s << juce::String::fromUTF8(" \xc2\xb7 CRC ") << alt.crc32 << juce::String::fromUTF8(" \xc2\xb7 SHA1 ") << alt.sha1;
	}
	return s;
}

juce::StringArray otherDumps(const juce::String &sha1)
{
	juce::StringArray all;
	for (const auto &d : knownDumps())
		if (sha1 == d.wanted)
			all.add(d.sha1);
	return all;
}

juce::MemoryBlock fromOtherDump(const juce::MemoryBlock &data, const juce::String &sha1, juce::String dataSha1)
{
	for (const auto &d : knownDumps()) {
		if (sha1 != d.wanted)
			continue;
		if (dataSha1.isEmpty())
			dataSha1 = sha1Hex(data.getData(), data.getSize());
		if (dataSha1 != d.sha1)
			continue;
		juce::MemoryBlock fixed(data);
		for (const auto &[at, value] : d.bytes)
			if (at < fixed.getSize())
				static_cast<uint8_t *>(fixed.getData())[at] = value;
		return sha1Hex(fixed.getData(), fixed.getSize()) == sha1 ? fixed : juce::MemoryBlock();
	}
	return {};
}

namespace {

juce::File g_settingsFile; // tests

} // namespace

std::unique_ptr<juce::PropertiesFile> settings()
{
	juce::PropertiesFile::Options o;
	o.applicationName = "Lemus";
	o.filenameSuffix = ".settings";
	o.folderName = "Lemus";
	o.osxLibrarySubFolder = "Application Support";
	return g_settingsFile != juce::File() ? std::make_unique<juce::PropertiesFile>(g_settingsFile, o) : std::make_unique<juce::PropertiesFile>(o);
}

namespace romfolder {

namespace {

juce::File g_override;

juce::File lastChosen(const juce::PropertiesFile &s)
{
	const auto path = s.getValue("romFolder");
	return juce::File::isAbsolutePath(path) ? juce::File(path) : defaultLocation();
}

juce::String machineKey(const juce::String &machine)
{
	return "romFolder." + machine;
}

} // namespace

juce::File defaultLocation()
{
	return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if JUCE_MAC
	    .getChildFile("Application Support")
#endif
	    .getChildFile("Lemus")
	    .getChildFile("ROMs");
}

juce::File location()
{
	return g_override != juce::File() ? g_override : lastChosen(*settings());
}

juce::File location(const juce::String &machine)
{
	if (g_override != juce::File())
		return g_override;
	const auto s = settings();
	const auto own = s->getValue(machineKey(machine));
	return juce::File::isAbsolutePath(own) ? juce::File(own) : lastChosen(*s);
}

void setLocation(const juce::String &machine, const juce::File &folder)
{
	auto s = settings();
	s->setValue("romFolder", folder.getFullPathName());
	s->setValue(machineKey(machine), folder.getFullPathName());
	s->saveIfNeeded();
}

void keepLocation(const juce::String &machine, const juce::File &folder)
{
	if (g_override != juce::File())
		return;
	auto s = settings();
	if (s->containsKey(machineKey(machine)))
		return;
	s->setValue(machineKey(machine), folder.getFullPathName());
	s->saveIfNeeded();
}

void setSettingsFile(const juce::File &file)
{
	g_settingsFile = file;
}

void setOverride(const juce::File &folder)
{
	g_override = folder;
}

RomSet load(const juce::File &folder, const std::vector<RomSpec> &roms, std::vector<RomSpec> *missing)
{
	RomSet found;
	// What each image found so far is: 0 the spec's own, 1 its first alternative... A spec's own
	// image is what the search waits for; an alternative is kept until it turns up, or for good.
	std::map<juce::String, size_t> rank;
	auto settled = [&](const RomSpec &spec) {
		const auto it = rank.find(spec.name);
		return it != rank.end() && it->second == 0;
	};
	auto take = [&](const RomSpec &spec, const juce::MemoryBlock &data, const juce::File &where, size_t r) {
		if (const auto it = rank.find(spec.name); it != rank.end() && it->second <= r)
			return;
		found.add(spec.name, data, where);
		rank[spec.name] = r;
	};
	auto remaining = [&] {
		return size_t(std::count_if(roms.begin(), roms.end(), [&](const RomSpec &spec) { return !settled(spec); }));
	};
	// Only data of a wanted size is read and hashed, so a large folder (a whole MAME set) is cheap
	// to look through.
	auto offer = [&](const juce::MemoryBlock &data, const juce::File &where) {
		juce::String sha1;
		auto hash = [&]() -> const juce::String & {
			if (sha1.isEmpty())
				sha1 = sha1Hex(data.getData(), data.getSize());
			return sha1;
		};
		for (const auto &spec : roms) {
			if (settled(spec))
				continue;
			if (spec.size == data.getSize()) {
				if (hash() == spec.sha1) {
					take(spec, data, where, 0);
					continue;
				}
				if (const auto fixed = fromOtherDump(data, spec.sha1, hash()); !fixed.isEmpty()) {
					take(spec, fixed, where, 0);
					continue;
				}
			}
			for (size_t i = 0; i < spec.alternatives.size(); ++i)
				if (spec.alternatives[i].size == data.getSize() && hash() == spec.alternatives[i].sha1) {
					take(spec, data, where, i + 1);
					break;
				}
		}
	};
	auto wanted = [&](juce::int64 size) {
		for (const auto &spec : roms) {
			if (settled(spec))
				continue;
			if (juce::int64(spec.size) == size)
				return true;
			for (const auto &alt : spec.alternatives)
				if (juce::int64(alt.size) == size)
					return true;
		}
		return false;
	};

	if (remaining() > 0 && folder.isDirectory()) {
		std::vector<juce::File> zips;
		for (const auto &entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles)) {
			const auto file = entry.getFile();
			if (wanted(entry.getFileSize())) {
				juce::MemoryBlock data;
				if (file.loadFileAsData(data))
					offer(data, file);
			} else if (file.hasFileExtension("zip") && entry.getFileSize() < 256 * 1024 * 1024) {
				zips.push_back(file);
			}
			if (remaining() == 0)
				break;
		}
		for (size_t z = 0; z < zips.size() && remaining() > 0; ++z) {
			juce::ZipFile zip(zips[z]);
			for (int i = 0; i < zip.getNumEntries() && remaining() > 0; ++i)
				if (const auto *e = zip.getEntry(i); e != nullptr && wanted(e->uncompressedSize))
					if (std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(i)); in != nullptr) {
						juce::MemoryBlock data;
						in->readIntoMemoryBlock(data);
						offer(data, zips[z].getChildFile(e->filename));
					}
		}
	}

	if (missing != nullptr)
		for (const auto &spec : roms)
			if (!found.has(spec.name))
				missing->push_back(spec);
	return found;
}

namespace {

// Whether an SVG has a mark: an element with an id below the root (host/Artwork.h), other than an
// empty group. A blank pack has its slots commented out, so it has none.
bool hasMarks(const juce::XmlElement &e)
{
	for (const auto *child : e.getChildIterator())
		if ((child->getStringAttribute("id").isNotEmpty() && (!child->hasTagNameIgnoringNamespace("g") || child->getNumChildElements() > 0)) ||
		    hasMarks(*child))
			return true;
	return false;
}

bool hasMarks(const juce::MemoryBlock &svg)
{
	const auto xml = juce::XmlDocument::parse(svg.toString());
	return xml != nullptr && xml->hasTagNameIgnoringNamespace("svg") && hasMarks(*xml);
}

} // namespace

ArtworkFile findArtwork(const juce::File &folder, const ArtworkSpec &spec)
{
	if (spec.name.isEmpty() || !folder.isDirectory())
		return {};
	std::vector<ArtworkFile> found;
	const auto stem = spec.folder();
	const auto places = placeFolder(folder, spec);
	const auto placesZip = places.withFileExtension("zip");
	for (const auto &entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles)) {
		const auto file = entry.getFile();
		const auto where = file.getRelativePathFrom(folder);
		// The places' own files are never a pack (the TB-303 has a place called tb303).
		if (!spec.places.empty() && (file.isAChildOf(places) || file == placesZip))
			continue;
		if (file.getFileName().equalsIgnoreCase(spec.name)) {
			ArtworkFile a;
			if (file.loadFileAsData(a.data) && hasMarks(a.data)) {
				a.where = where;
				found.push_back(std::move(a));
			}
		} else if (file.hasFileExtension("zip") && file.getFileNameWithoutExtension().equalsIgnoreCase(stem)) {
			// An entry with the pack's name, or else the zip's only SVG.
			juce::ZipFile zip(file);
			int exact = -1, firstSvg = -1, svgs = 0;
			for (int i = 0; i < zip.getNumEntries() && exact < 0; ++i) {
				const auto entryName = zip.getEntry(i)->filename.fromLastOccurrenceOf("/", false, false);
				if (entryName.equalsIgnoreCase(spec.name))
					exact = i;
				else if (entryName.endsWithIgnoreCase(".svg") && svgs++ == 0)
					firstSvg = i;
			}
			const int pick = exact >= 0 ? exact : svgs == 1 ? firstSvg : -1;
			if (pick >= 0)
				if (std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(pick)); in != nullptr) {
					ArtworkFile a;
					in->readIntoMemoryBlock(a.data);
					a.where = where + "/" + zip.getEntry(pick)->filename;
					if (hasMarks(a.data))
						found.push_back(std::move(a));
				}
		}
	}
	if (found.empty())
		return {};
	std::sort(found.begin(), found.end(), [](const ArtworkFile &x, const ArtworkFile &y) { return x.where < y.where; });
	return found.front();
}

juce::File placeFolder(const juce::File &folder, const ArtworkSpec &spec)
{
	return folder.getChildFile("artwork").getChildFile(spec.folder());
}

juce::String placeFileName(const ArtworkSpec &spec, const juce::String &place)
{
	return spec.plugin.isNotEmpty() ? spec.plugin + "-" + place : place;
}

std::vector<PlaceFile> findPlaceFiles(const juce::File &folder, const ArtworkSpec &spec)
{
	std::vector<PlaceFile> found;
	if (spec.places.empty() || !folder.isDirectory())
		return found;
	const auto dir = placeFolder(folder, spec);
	const auto loose = dir.isDirectory() ? dir.findChildFiles(juce::File::findFiles, false) : juce::Array<juce::File>();
	std::unique_ptr<juce::ZipFile> zip;
	if (const auto z = dir.withFileExtension("zip"); z.existsAsFile())
		zip = std::make_unique<juce::ZipFile>(z);
	auto take = [&](const juce::Array<juce::File> &files, const juce::String &stem, PlaceFile &pf) {
		for (const char *ext : {".svg", ".png"})
			for (const auto &f : files)
				if (f.getFileName().equalsIgnoreCase(stem + ext) && f.loadFileAsData(pf.data)) {
					pf.where = f.getRelativePathFrom(folder);
					pf.file = f;
					return;
				}
	};
	// The files named after the plugin, looked for once, and only if a place still needs one.
	juce::Array<juce::File> named;
	bool lookedForNamed = false;
	auto namedFiles = [&]() -> const juce::Array<juce::File> & {
		if (!lookedForNamed) {
			lookedForNamed = true;
			for (const auto &entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles))
				if (entry.getFile().getFileName().startsWithIgnoreCase(spec.plugin + "-"))
					named.add(entry.getFile());
			named.sort();
		}
		return named;
	};
	for (const auto &place : spec.places) {
		PlaceFile pf;
		pf.place = place.id;
		take(loose, place.id, pf);
		if (pf.data.isEmpty() && zip != nullptr)
			for (const char *ext : {".svg", ".png"}) {
				for (int i = 0; i < zip->getNumEntries() && pf.data.isEmpty(); ++i) {
					const auto *e = zip->getEntry(i);
					if (!e->filename.fromLastOccurrenceOf("/", false, false).equalsIgnoreCase(place.id + ext))
						continue;
					if (std::unique_ptr<juce::InputStream> in(zip->createStreamForEntry(i)); in != nullptr) {
						in->readIntoMemoryBlock(pf.data);
						pf.where = dir.withFileExtension("zip").getRelativePathFrom(folder) + "/" + e->filename;
					}
				}
				if (!pf.data.isEmpty())
					break;
			}
		if (pf.data.isEmpty() && spec.plugin.isNotEmpty())
			take(namedFiles(), placeFileName(spec, place.id), pf);
		if (!pf.data.isEmpty())
			found.push_back(std::move(pf));
	}
	return found;
}

ArtworkFound findAllArtwork(const juce::File &folder, const ArtworkSpec &spec)
{
	return {findArtwork(folder, spec), findPlaceFiles(folder, spec)};
}

void addArtwork(RomSet &roms, const ArtworkSpec &spec, const ArtworkFound &found)
{
	if (!found.pack.data.isEmpty())
		roms.add(spec.name, found.pack.data);
	for (const auto &p : found.places)
		roms.add(ArtworkSpec::key(p.place), p.data);
}

bool installBlankArtwork(const juce::File &folder, const ArtworkSpec &spec)
{
	if (spec.name.isEmpty() || spec.blank == nullptr || !spec.places.empty())
		return false;
	// Never create a folder the user named: it may be on a drive that isn't mounted.
	if (!folder.isDirectory() && (folder != defaultLocation() || !folder.createDirectory()))
		return false;
	for (const auto &entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles))
		if (entry.getFile().getFileName().equalsIgnoreCase(spec.name))
			return false;
	return folder.getChildFile(spec.name).replaceWithText(spec.blank, false, false, "\n");
}

bool removeBlankArtwork(const juce::File &folder, const ArtworkSpec &spec)
{
	if (spec.name.isEmpty() || spec.blank == nullptr)
		return false;
	const auto file = folder.getChildFile(spec.name);
	if (!file.existsAsFile() || file.loadFileAsString() != juce::String(spec.blank).replace("\r\n", "\n"))
		return false;
	return file.deleteFile();
}

} // namespace romfolder

namespace {

std::set<juce::String> g_answered; // while a test ROM folder is set

} // namespace

bool romPromptAnswered(const juce::String &machineId)
{
	if (romfolder::g_override != juce::File())
		return g_answered.count(machineId) > 0;
	return settings()->getBoolValue("romPromptAnswered." + machineId, false);
}

void setRomPromptAnswered(const juce::String &machineId, bool answered)
{
	if (romfolder::g_override != juce::File()) {
		if (answered)
			g_answered.insert(machineId);
		else
			g_answered.erase(machineId);
		return;
	}
	auto s = settings();
	s->setValue("romPromptAnswered." + machineId, answered);
	s->saveIfNeeded();
}

} // namespace lemus
