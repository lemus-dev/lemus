#include "Catalog.h"

#include <algorithm>

namespace lemus::manager {

namespace {

juce::String g_source;

Download downloadOf(const juce::var &v)
{
	Download d;
	d.url = v["url"].toString();
	d.sha256 = v["sha256"].toString().toLowerCase().fromLastOccurrenceOf(":", false, false);
	d.size = juce::int64(v["size"]);
	return d;
}

Entry entryOf(const juce::String &name, const juce::var &v)
{
	Entry e;
	e.plugin = name;
	e.emulates = v["emulates"].toString();
	e.type = v["type"].toString();
	e.year = int(v["year"]);
	e.version = v["version"].toString();
	e.page = v["url"].toString();
	e.stage = v["stage"].toString();
	e.standalone = v.hasProperty("standalone") ? bool(v["standalone"]) : true;
	e.withoutRom = v["withoutRom"].toString();
	e.mac = downloadOf(v["downloads"]["mac"]);
	e.windows = downloadOf(v["downloads"]["windows"]);
	if (e.emulates.isEmpty())
		e.emulates = name;
	return e;
}

} // namespace

const Download &Entry::here() const
{
#if JUCE_WINDOWS
	return windows;
#else
	return mac;
#endif
}

juce::String Entry::shortName() const
{
	const auto words = juce::StringArray::fromTokens(emulates, " ", "");
	for (int i = 0; i < words.size(); ++i)
		if (words[i].containsAnyOf("0123456789")) {
			juce::StringArray kept;
			for (int k = 0; k <= i; ++k)
				kept.add(words[k]);
			return kept.joinIntoString(" ");
		}
	return emulates;
}

juce::String Entry::subtitle() const
{
	juce::StringArray parts;
	if (type.isNotEmpty())
		parts.add(type.substring(0, 1).toUpperCase() + type.substring(1));
	if (year > 0)
		parts.add(juce::String(year));
	parts.add(plugin);
	return parts.joinIntoString(juce::String::fromUTF8(" \xc2\xb7 "));
}

std::optional<Catalog> Catalog::parse(const juce::String &text)
{
	const auto file = juce::JSON::parse(text);
	const auto *plugins = file["plugins"].getDynamicObject();
	if (plugins == nullptr)
		return std::nullopt;
	Catalog c;
	c.text = text;
	for (const auto &p : plugins->getProperties()) {
		if (!p.value.isObject())
			continue;
		auto e = entryOf(p.name.toString(), p.value);
		if (e.version.isEmpty())
			continue;
		if (e.plugin == "lemus-plugin-manager")
			c.manager = e;
		else
			c.plugins.push_back(e);
	}
	std::sort(c.plugins.begin(), c.plugins.end(), [](const Entry &a, const Entry &b) {
		return a.emulates.compareNatural(b.emulates) < 0;
	});
	return c;
}

const Entry *Catalog::find(const juce::String &plugin) const
{
	for (const auto &e : plugins)
		if (e.plugin == plugin)
			return &e;
	return nullptr;
}

std::vector<updates::Change> Catalog::changesSince(const Entry &e, const juce::String &since) const
{
	return updates::releaseOf(text, e.plugin, since).changes;
}

void setCatalogSource(const juce::String &fileOrUrl)
{
	g_source = fileOrUrl;
}

juce::String catalogSource()
{
	return g_source.isNotEmpty() ? g_source : juce::String(LEMUS_SITE) + "/latest.json";
}

std::optional<Catalog> fetchCatalog(juce::String &error)
{
	const auto source = catalogSource();
	juce::String text;
	if (!source.startsWithIgnoreCase("https://") && !source.startsWithIgnoreCase("http://")) {
		const juce::File file(juce::File::getCurrentWorkingDirectory().getChildFile(source));
		if (!file.existsAsFile()) {
			error = "No file " + file.getFullPathName() + ".";
			return std::nullopt;
		}
		text = file.loadFileAsString();
	} else {
		juce::WebInputStream stream(juce::URL(source), false);
		stream.withConnectionTimeout(10000).withExtraHeaders(juce::String("User-Agent: ") + kUserAgent + "\r\n");
		if (!stream.connect(nullptr) || stream.getStatusCode() != 200) {
			error = "Couldn't reach " + juce::URL(source).getDomain() + ".";
			return std::nullopt;
		}
		juce::MemoryOutputStream body;
		body.writeFromInputStream(stream, 1 << 20);
		text = body.toString();
	}
	auto c = Catalog::parse(text);
	if (!c)
		error = "The list of plugins didn't read.";
	return c;
}

} // namespace lemus::manager
