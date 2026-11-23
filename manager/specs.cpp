// Prints what each shipping plugin needs from the user's ROM folder, as JSON for the website
// (website/src/data/roms.json, served as /roms.json for Lemus Plugin Manager): its ROM images
// (name, size, SHA1, CRC, whether optional and what the machine lacks without it, other images it
// also takes in one's place) and its artwork
// places. Taken from the machines' own specs (host/Machine.h), so the manager checks a folder the
// way the plugins read it (host/Roms.h). Built as lemus_specs_instrument and lemus_specs_fx, one
// per host (they don't link together); tools/release/roms-json.sh runs both and merges them. Run it
// again when a machine's ROMs or places change.
#include "host/Machine.h"

#include <cstdio>

using namespace lemus;

int main()
{
	auto plugins = std::make_unique<juce::DynamicObject>();
	for (const auto &m : machines()) {
		if (m.artwork.plugin.isEmpty())
			continue;
		juce::Array<juce::var> roms;
		for (const auto &r : m.roms) {
			auto o = std::make_unique<juce::DynamicObject>();
			o->setProperty("name", r.name);
			o->setProperty("size", juce::int64(r.size));
			o->setProperty("sha1", r.sha1);
			o->setProperty("crc32", r.crc32);
			o->setProperty("what", r.what);
			o->setProperty("optional", r.optional);
			if (r.optional)
				o->setProperty("without", r.without);
			if (!r.alternatives.empty()) {
				juce::Array<juce::var> alts;
				for (const auto &a : r.alternatives) {
					auto ao = std::make_unique<juce::DynamicObject>();
					ao->setProperty("name", a.name);
					ao->setProperty("size", juce::int64(a.size));
					ao->setProperty("sha1", a.sha1);
					ao->setProperty("crc32", a.crc32);
					ao->setProperty("label", a.label);
					alts.add(juce::var(ao.release()));
				}
				o->setProperty("alternatives", alts);
			}
			roms.add(juce::var(o.release()));
		}
		juce::Array<juce::var> places;
		for (const auto &p : m.artwork.places) {
			auto o = std::make_unique<juce::DynamicObject>();
			o->setProperty("id", p.id);
			o->setProperty("label", p.label);
			places.add(juce::var(o.release()));
		}
		auto e = std::make_unique<juce::DynamicObject>();
		e->setProperty("machine", m.id);
		e->setProperty("roms", roms);
		if (!places.isEmpty()) {
			e->setProperty("artwork", m.artwork.name);
			e->setProperty("places", places);
		}
		plugins->setProperty(m.artwork.plugin, juce::var(e.release()));
	}
	auto root = std::make_unique<juce::DynamicObject>();
	root->setProperty("plugins", juce::var(plugins.release()));
	const auto text = juce::JSON::toString(juce::var(root.release()), juce::JSON::FormatOptions{}.withSpacing(juce::JSON::Spacing::multiLine));
	std::fputs(text.toRawUTF8(), stdout);
	std::fputs("\n", stdout);
	return 0;
}
