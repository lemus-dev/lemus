#include "Scene.h"

namespace lemus::host {

double Automation::valueAt(double beat) const
{
	if (points.empty())
		return 0;
	if (beat <= points.front().first)
		return points.front().second;
	for (size_t i = 1; i < points.size(); ++i)
		if (beat < points[i].first) {
			const auto &[b0, v0] = points[i - 1];
			const auto &[b1, v1] = points[i];
			return v0 + (v1 - v0) * (beat - b0) / std::max(1e-9, b1 - b0);
		}
	return points.back().second;
}

int parseNote(const juce::var &v)
{
	if (v.isInt() || v.isDouble() || v.isInt64())
		return int(v);
	const juce::String s = v.toString().trim();
	static const char *names = "C D EF G A B";
	const juce::juce_wchar letter = juce::CharacterFunctions::toUpperCase(s[0]);
	int semi = -1;
	for (int i = 0; i < 12; ++i)
		if (names[i] == letter)
			semi = i;
	if (semi < 0)
		return -1;
	int pos = 1;
	if (s[pos] == '#')
		++semi, ++pos;
	else if (s[pos] == 'b')
		--semi, ++pos;
	else if (s[pos] == '-')
		++pos;
	return 12 * (s.substring(pos).getIntValue() + 1) + semi;
}

namespace {

std::map<juce::String, juce::var> objectToMap(const juce::var &v)
{
	std::map<juce::String, juce::var> out;
	if (auto *o = v.getDynamicObject())
		for (const auto &p : o->getProperties())
			out[p.name.toString()] = p.value;
	return out;
}

// Fields of `over` replace those of `base`; objects ("names", "params") merge key by key.
juce::var merged(const juce::var &base, const juce::var &over)
{
	auto *b = base.getDynamicObject();
	auto *o = over.getDynamicObject();
	if (b == nullptr)
		return over;
	juce::DynamicObject::Ptr out = new juce::DynamicObject(*b);
	if (o != nullptr)
		for (const auto &p : o->getProperties()) {
			const auto &prev = out->getProperty(p.name);
			out->setProperty(p.name, prev.getDynamicObject() != nullptr && p.value.getDynamicObject() != nullptr
			                             ? merged(prev, p.value)
			                             : p.value);
		}
	return juce::var(out.get());
}

} // namespace

juce::Array<juce::File> Templates::standardFiles()
{
	juce::Array<juce::File> files;
	for (const auto &dir : juce::File(LEMUS_MACHINES_DIR).findChildFiles(juce::File::findDirectories, false))
		if (const auto f = dir.getChildFile("host/sources.json"); f.existsAsFile())
			files.add(f);
	files.sort();
	return files;
}

Templates Templates::load(const juce::Array<juce::File> &files, juce::String &error)
{
	Templates t;
	for (const auto &f : files) {
		const auto json = juce::JSON::parse(f);
		auto *o = json.getDynamicObject();
		if (o == nullptr) {
			error = "can't read " + f.getFullPathName() + " as JSON";
			return t;
		}
		for (const auto &p : o->getProperties())
			t.entries[p.name.toString()] = {p.value, f.getParentDirectory()};
	}
	return t;
}

Scene Scene::load(const juce::File &file, const Templates &templates, juce::String &error)
{
	Scene s;
	const auto json = juce::JSON::parse(file);
	if (json.getDynamicObject() == nullptr) {
		error = "can't read " + file.getFullPathName() + " as JSON";
		return s;
	}
	auto num = [&](const char *k, double d) { return double(json.getProperty(k, d)); };
	s.name = json.getProperty("name", file.getFileNameWithoutExtension()).toString();
	s.rate = num("rate", s.rate);
	s.bpm = num("bpm", s.bpm);
	s.beats = json.hasProperty("bars") ? num("bars", 4) * 4 : num("beats", s.beats);
	s.tail = num("tail", s.tail);
	s.block = int(num("block", s.block));
	s.automationStep = int(num("automationStep", s.automationStep));
	s.onsFirst = bool(json.getProperty("onsFirst", false));
	s.playing = bool(json.getProperty("playing", true));
	if (auto *arr = json["notes"].getArray())
		for (const auto &n : *arr) {
			Note note;
			note.at = n.getProperty("at", 0.0);
			note.len = n.getProperty("len", 1.0);
			note.note = parseNote(n["note"]);
			note.velocity = n.hasProperty("vel") ? int(n["vel"]) : bool(n.getProperty("accent", false)) ? 127 : 100;
			if (note.note < 0) {
				error = "bad note " + n["note"].toString();
				return s;
			}
			s.notes.push_back(note);
		}
	if (json.hasProperty("midi")) {
		// A MIDI file's notes, its tempo map turned into beats at the scene's tempo.
		const auto path = file.getParentDirectory().getChildFile(json["midi"].toString());
		juce::FileInputStream in(path);
		juce::MidiFile mf;
		if (!in.openedOk() || !mf.readFrom(in)) {
			error = "can't read MIDI file " + path.getFullPathName();
			return s;
		}
		mf.convertTimestampTicksToSeconds();
		juce::MidiMessageSequence all;
		for (int t = 0; t < mf.getNumTracks(); ++t)
			all.addSequence(*mf.getTrack(t), 0);
		all.updateMatchedPairs();
		for (const auto *e : all)
			if (e->message.isNoteOn() && e->noteOffObject != nullptr) {
				const double toBeats = s.bpm / 60.0;
				s.notes.push_back({e->message.getTimeStamp() * toBeats,
				                   (e->noteOffObject->message.getTimeStamp() - e->message.getTimeStamp()) * toBeats,
				                   e->message.getNoteNumber(), e->message.getVelocity()});
			}
	}
	// "repeat": N plays the notes N times, "every" beats apart (default: their length rounded up
	// to whole bars).
	if (const int repeat = json.getProperty("repeat", 1); repeat > 1 && !s.notes.empty()) {
		double end = 0;
		for (const auto &n : s.notes)
			end = std::max(end, n.at + n.len);
		const double every = json.getProperty("every", std::max(4.0, std::ceil(end / 4.0 - 1e-9) * 4.0));
		const auto once = s.notes;
		for (int r = 1; r < repeat; ++r)
			for (auto n : once) {
				n.at += r * every;
				s.notes.push_back(n);
			}
	}
	s.params = objectToMap(json["params"]);
	if (auto *arr = json["automation"].getArray())
		for (const auto &a : *arr) {
			Automation au;
			au.param = a["param"].toString();
			if (auto *pts = a["points"].getArray())
				for (const auto &p : *pts)
					au.points.push_back({double(p[0]), double(p[1])});
			std::sort(au.points.begin(), au.points.end());
			s.automation.push_back(au);
		}
	if (json.hasProperty("input"))
		s.input = file.getParentDirectory().getChildFile(json["input"].toString());
	if (auto *arr = json["sources"].getArray())
		for (auto src : *arr) {
			// Paths the scene gives are next to the scene; a template's, next to its file.
			const auto own = src;
			juce::File tplDir = file.getParentDirectory();
			if (src.hasProperty("use")) {
				const auto it = templates.entries.find(src["use"].toString());
				if (it == templates.entries.end()) {
					error = "no source template \"" + src["use"].toString() + "\" (machines/*/host/sources.json)";
					return s;
				}
				src = merged(it->second.first, src);
				tplDir = it->second.second;
			}
			auto path = [&](const char *key) {
				return (own.hasProperty(key) ? file.getParentDirectory() : tplDir).getChildFile(src[key].toString());
			};
			Source out;
			out.label = src.getProperty("label", src.getProperty("use", "source")).toString().replaceCharacter('.', '-');
			out.plugin = src["plugin"].toString();
			out.machine = src["machine"].toString();
			if (src.hasProperty("state"))
				out.state = path("state");
			if (src.hasProperty("audio"))
				out.audio = path("audio");
			for (const auto &[k, v] : objectToMap(src["names"]))
				out.names[k] = v.toString();
			out.params = objectToMap(src["params"]);
			out.gainDb = src.getProperty("gainDb", 0.0);
			if (const auto clip = src["clip"]; clip.isObject()) {
				out.clipLevel = clip.getProperty("level", 0.0);
				out.clipBias = clip.getProperty("bias", 0.0);
				out.clipHard = clip.getProperty("hard", false);
			}
			s.sources.push_back(out);
		}
	if (s.sources.empty())
		error = "the scene has no sources";
	return s;
}

} // namespace lemus::host
