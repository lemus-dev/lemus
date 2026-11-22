#include "RomCheck.h"

#include "Catalog.h"

#include <set>

namespace lemus::manager {

namespace {

// Files bigger than this aren't any plugin's ROM or artwork: skipped unread.
constexpr juce::int64 kMaxFile = 64 << 20;
// A dropped folder is searched this deep, for at most this many files.
constexpr int kMaxFiles = 5000;

juce::String needsSource()
{
	const auto source = catalogSource();
	const auto name = source.fromLastOccurrenceOf("/", false, false);
	return source.dropLastCharacters(name.length()) + "roms.json";
}

juce::String extension(const juce::String &name)
{
	return name.fromLastOccurrenceOf(".", false, false).toLowerCase();
}

// Where a dropped file's bytes go, and what to call them.
struct Wanted {
	juce::String plugin, name;
	juce::File folder;
};

// The plugins' ROMs by SHA1, and their places' file names (<plugin>-<place>, lower case).
struct Index {
	std::map<juce::String, std::vector<Wanted>> roms;
	std::map<juce::String, Wanted> art;
};

Index indexOf(const NeedsMap &needs)
{
	Index index;
	for (const auto &[plugin, n] : needs) {
		const auto folder = romfolder::location(n.machine);
		for (const auto &r : n.roms)
			index.roms[r.sha1].push_back({plugin, r.name, folder});
		for (const auto &p : n.artwork.places)
			index.art[romfolder::placeFileName(n.artwork, p.id).toLowerCase()] = {plugin, romfolder::placeFileName(n.artwork, p.id), folder};
	}
	return index;
}

// Writes `data` as `name` in `folder` unless a file of that name has the same bytes already.
bool put(const juce::File &folder, const juce::String &name, const void *data, size_t size, std::vector<Added> &added, const juce::String &plugin)
{
	const auto target = folder.getChildFile(name);
	if (target.existsAsFile() && target.getSize() == juce::int64(size)) {
		juce::MemoryBlock old;
		if (target.loadFileAsData(old) && old.getSize() == size && std::memcmp(old.getData(), data, size) == 0) {
			added.push_back({plugin, name, true});
			return true;
		}
	}
	if (!folder.createDirectory())
		return false;
	if (!target.replaceWithData(data, size))
		return false;
	added.push_back({plugin, name, false});
	return true;
}

// One file's bytes, whatever they came out of: a ROM by its SHA1, artwork by its name.
void offer(const juce::String &fileName, const juce::MemoryBlock &data, const Index &index, std::vector<Added> &added)
{
	if (const auto it = index.roms.find(sha1Hex(data.getData(), data.getSize())); it != index.roms.end()) {
		for (const auto &w : it->second)
			put(w.folder, w.name, data.getData(), data.getSize(), added, w.plugin);
		return;
	}
	const auto ext = extension(fileName);
	if (ext != "svg" && ext != "png")
		return;
	const auto stem = fileName.fromLastOccurrenceOf("/", false, false).upToLastOccurrenceOf(".", false, false).toLowerCase();
	if (const auto it = index.art.find(stem); it != index.art.end())
		put(it->second.folder, it->second.name + "." + ext, data.getData(), data.getSize(), added, it->second.plugin);
}

void offerZip(const juce::File &file, const Index &index, std::vector<Added> &added)
{
	juce::ZipFile zip(file);
	for (int i = 0; i < zip.getNumEntries(); ++i) {
		const auto *entry = zip.getEntry(i);
		if (entry == nullptr || entry->filename.endsWithChar('/') || entry->uncompressedSize > kMaxFile)
			continue;
		std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(i));
		if (in == nullptr)
			continue;
		juce::MemoryBlock data;
		in->readIntoMemoryBlock(data);
		offer(entry->filename, data, index, added);
	}
}

void offerFile(const juce::File &file, const Index &index, std::vector<Added> &added)
{
	if (file.getSize() > kMaxFile)
		return;
	if (extension(file.getFileName()) == "zip") {
		offerZip(file, index, added);
		return;
	}
	juce::MemoryBlock data;
	if (file.loadFileAsData(data))
		offer(file.getFileName(), data, index, added);
}

} // namespace

std::optional<NeedsMap> parseNeeds(const juce::String &text)
{
	const auto file = juce::JSON::parse(text);
	const auto *plugins = file["plugins"].getDynamicObject();
	if (plugins == nullptr)
		return std::nullopt;
	NeedsMap map;
	for (const auto &p : plugins->getProperties()) {
		const auto plugin = p.name.toString();
		const auto &v = p.value;
		if (!isPluginName(plugin) || !v.isObject())
			continue;
		Needs n;
		n.machine = v["machine"].toString();
		// The machine's id names its ROM-folder setting: plain only.
		if (n.machine.isEmpty() || !n.machine.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789_"))
			continue;
		if (const auto *roms = v["roms"].getArray())
			for (const auto &r : *roms) {
				RomSpec s;
				s.name = r["name"].toString();
				s.size = size_t(juce::int64(r["size"]));
				s.sha1 = r["sha1"].toString().toLowerCase();
				s.crc32 = r["crc32"].toString().toLowerCase();
				s.what = r["what"].toString();
				s.optional = bool(r["optional"]);
				s.without = r["without"].toString();
				// The name becomes a file in the ROM folder: no paths.
				if (s.name.isNotEmpty() && !s.name.containsAnyOf("/\\:") && s.name != ".." && s.sha1.length() == 40)
					n.roms.push_back(s);
			}
		n.artwork.name = v["artwork"].toString();
		n.artwork.plugin = plugin;
		if (const auto *places = v["places"].getArray())
			for (const auto &pl : *places) {
				ArtworkPlace place;
				place.id = pl["id"].toString();
				place.label = pl["label"].toString();
				if (place.id.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789_") && place.id.isNotEmpty())
					n.artwork.places.push_back(place);
			}
		map[plugin] = n;
	}
	return map;
}

std::optional<NeedsMap> fetchNeeds()
{
	const auto source = needsSource();
	juce::String text;
	if (!source.startsWithIgnoreCase("https://") && !source.startsWithIgnoreCase("http://")) {
		const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(source);
		if (!file.existsAsFile())
			return std::nullopt;
		text = file.loadFileAsString();
	} else {
		juce::WebInputStream stream(juce::URL(source), false);
		stream.withConnectionTimeout(10000).withExtraHeaders(juce::String("User-Agent: ") + kUserAgent + "\r\n");
		if (!stream.connect(nullptr) || stream.getStatusCode() != 200)
			return std::nullopt;
		juce::MemoryOutputStream body;
		body.writeFromInputStream(stream, 1 << 20);
		text = body.toString();
	}
	return parseNeeds(text);
}

RomCheck::Rom RomCheck::rom() const
{
	if (images.empty())
		return Rom::NotNeeded;
	bool reduced = false;
	for (const auto &i : images)
		if (!i.found) {
			if (!i.spec.optional)
				return Rom::Missing;
			reduced = true;
		}
	return reduced ? Rom::Reduced : Rom::Ready;
}

RomCheck::Art RomCheck::art() const
{
	if (places.empty())
		return Art::NotNeeded;
	int found = 0;
	for (const auto &p : places)
		found += p.where.isNotEmpty() ? 1 : 0;
	return found == 0 ? Art::None : found == int(places.size()) ? Art::Ready : Art::Partial;
}

RomCheck check(const Needs &n)
{
	RomCheck c;
	c.folder = romfolder::location(n.machine);
	const auto set = romfolder::load(c.folder, n.roms);
	for (const auto &r : n.roms)
		c.images.push_back({r, set.has(r.name), set.where(r.name)});
	std::map<juce::String, juce::String> found;
	if (!n.artwork.places.empty())
		for (const auto &f : romfolder::findPlaceFiles(c.folder, n.artwork))
			found[f.place] = f.where;
	for (const auto &p : n.artwork.places)
		c.places.push_back({p, found.count(p.id) > 0 ? found[p.id] : juce::String()});
	return c;
}

std::vector<Added> addDropped(const juce::StringArray &paths, const NeedsMap &needs)
{
	const auto index = indexOf(needs);
	std::vector<Added> added;
	int seen = 0;
	for (const auto &path : paths) {
		const juce::File f(path);
		if (f.isDirectory()) {
			for (const auto &entry : juce::RangedDirectoryIterator(f, true, "*", juce::File::findFiles)) {
				if (++seen > kMaxFiles)
					break;
				offerFile(entry.getFile(), index, added);
			}
		} else if (f.existsAsFile()) {
			offerFile(f, index, added);
		}
	}
	return added;
}

juce::String describe(const std::vector<Added> &added)
{
	if (added.empty())
		return "Nothing there is a Lemus plugin's ROM or artwork.";
	std::vector<Added> fresh;
	for (const auto &a : added)
		if (!a.already)
			fresh.push_back(a);
	if (fresh.empty())
		return added.size() == 1 ? added[0].plugin + " has " + added[0].file + " already." : "The plugins have those already.";
	std::set<juce::String> plugins;
	for (const auto &a : fresh)
		plugins.insert(a.plugin);
	if (fresh.size() == 1)
		return "Added " + fresh[0].file + " to " + fresh[0].plugin;
	if (plugins.size() == 1)
		return "Added " + juce::String(int(fresh.size())) + " files to " + *plugins.begin();
	return "Added " + juce::String(int(fresh.size())) + " files to " + juce::String(int(plugins.size())) + " plugins";
}

} // namespace lemus::manager
