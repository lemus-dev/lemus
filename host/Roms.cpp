#include "Roms.h"

#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <array>
#include <memory>
#include <set>

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

std::unique_ptr<juce::PropertiesFile> settings()
{
	juce::PropertiesFile::Options o;
	o.applicationName = "Lemus";
	o.filenameSuffix = ".settings";
	o.folderName = "Lemus";
	o.osxLibrarySubFolder = "Application Support";
	return std::make_unique<juce::PropertiesFile>(o);
}

namespace romfolder {

namespace {

juce::File g_override;

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
	if (g_override != juce::File())
		return g_override;
	const auto path = settings()->getValue("romFolder");
	return path.isNotEmpty() && juce::File::isAbsolutePath(path) ? juce::File(path) : defaultLocation();
}

void setLocation(const juce::File &folder)
{
	auto s = settings();
	s->setValue("romFolder", folder.getFullPathName());
	s->saveIfNeeded();
}

void setOverride(const juce::File &folder)
{
	g_override = folder;
}

RomSet load(const juce::File &folder, const std::vector<RomSpec> &roms, std::vector<RomSpec> *missing)
{
	RomSet found;
	size_t remaining = roms.size();
	// Only data of a wanted size is read and hashed, so a large folder (a whole MAME set) is cheap
	// to look through.
	auto offer = [&](const juce::MemoryBlock &data) {
		juce::String sha1;
		for (const auto &spec : roms) {
			if (spec.size != data.getSize() || found.has(spec.name))
				continue;
			if (sha1.isEmpty())
				sha1 = sha1Hex(data.getData(), data.getSize());
			if (sha1 == spec.sha1) {
				found.add(spec.name, data);
				--remaining;
			}
		}
	};
	auto wanted = [&](juce::int64 size) {
		for (const auto &spec : roms)
			if (juce::int64(spec.size) == size && !found.has(spec.name))
				return true;
		return false;
	};

	if (remaining > 0 && folder.isDirectory()) {
		std::vector<juce::File> zips;
		for (const auto &entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles)) {
			const auto file = entry.getFile();
			if (wanted(entry.getFileSize())) {
				juce::MemoryBlock data;
				if (file.loadFileAsData(data))
					offer(data);
			} else if (file.hasFileExtension("zip") && entry.getFileSize() < 256 * 1024 * 1024) {
				zips.push_back(file);
			}
			if (remaining == 0)
				break;
		}
		for (size_t z = 0; z < zips.size() && remaining > 0; ++z) {
			juce::ZipFile zip(zips[z]);
			for (int i = 0; i < zip.getNumEntries() && remaining > 0; ++i)
				if (const auto *e = zip.getEntry(i); e != nullptr && wanted(e->uncompressedSize))
					if (std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(i)); in != nullptr) {
						juce::MemoryBlock data;
						in->readIntoMemoryBlock(data);
						offer(data);
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
	const auto stem = spec.name.upToLastOccurrenceOf(".", false, false);
	for (const auto &entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles)) {
		const auto file = entry.getFile();
		const auto where = file.getRelativePathFrom(folder);
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

bool installBlankArtwork(const juce::File &folder, const ArtworkSpec &spec)
{
	if (spec.name.isEmpty() || spec.blank == nullptr)
		return false;
	// Never create a folder the user named: it may be on a drive that isn't mounted.
	if (!folder.isDirectory() && (folder != defaultLocation() || !folder.createDirectory()))
		return false;
	for (const auto &entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles))
		if (entry.getFile().getFileName().equalsIgnoreCase(spec.name))
			return false;
	return folder.getChildFile(spec.name).replaceWithText(spec.blank, false, false, "\n");
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
