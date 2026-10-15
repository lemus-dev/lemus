// tb303render: renders test specs (tools/compare/specs/*.json) through the voice model.
//
//   tb303render SPEC.json [SPEC.json ...] [--out DIR] [--rate 44100] [--only NAME,NAME] [--unit]
//              [--cal vcfRingHz=400 --cal q21Beta=600 --cal q21TailmA=0.45 ...]
//
// The voice starts from the schematic's nominal values (tb303::Calibration's defaults); --unit
// starts from the plugin's unit instead (tb303::unitCalibration()). --cal then overrides.
//
// The spec format is the one tools/compare/roland_render.py plays through Roland's plugin:
// lines at "bpm" and "lpb", notes with "line", "note" ("C-3", "D#2"), "len" in lines,
// "accent", "slide", optional "vel"; "params" are pot positions 0-1. Notes go through the
// same MidiDriver as the plugin (velocity >= 100 accents, overlapping notes slide), with the
// slid note held until the next one starts, exactly as the Roland harness sends them.
#include "tb303/core/midi.h"
#include "tb303/core/voice.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <map>

namespace {

int parseNote(const juce::String &s)
{
	// "C-3" / "C#3" / "Db3": Onyx Tracker's names, where C-3 is MIDI 48.
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
	const int octave = s.substring(pos).getIntValue();
	return 12 * (octave + 1) + semi;
}

struct Event {
	double time;
	int order; // at equal times: note-ons of later notes before note-offs (as Onyx sends them)
	bool on;
	int note, velocity;
};

tb303::Calibration g_cal; // --cal NAME=VALUE overrides

bool render(const juce::var &spec, const juce::File &outDir, double rate)
{
	const juce::String name = spec["name"].toString();
	const double bpm = spec.getProperty("bpm", 120.0);
	const double lpb = spec.getProperty("lpb", 4.0);
	const double lineSec = 60.0 / bpm / lpb;

	tb303::Controls c;
	const auto params = spec["params"];
	auto knob = [&](const char *id, double def) { return double(params.getProperty(id, def)); };
	c.tuning = knob("tuning", 0.5);
	c.cutoff = knob("cutoff", 0.5);
	c.resonance = knob("resonance", 0.5);
	c.envMod = knob("envmod", 0.5);
	c.decay = knob("decay", 0.5);
	c.accent = knob("accent", 0.5);
	c.volume = knob("volume", 0.6);
	c.square = knob("waveform", 0) > 0.5;

	// Notes in time order; a slid note is held to the start of the next one.
	std::vector<juce::var> notes;
	if (auto *arr = spec["notes"].getArray())
		for (auto &n : *arr)
			notes.push_back(n);
	std::stable_sort(notes.begin(), notes.end(), [](const juce::var &a, const juce::var &b) {
		return double(a["line"]) < double(b["line"]);
	});
	std::vector<Event> events;
	double lastLine = spec.getProperty("lines", 0.0);
	for (size_t i = 0; i < notes.size(); ++i) {
		const auto &n = notes[i];
		const double start = n["line"];
		double end = start + double(n.getProperty("len", 1.0));
		if (bool(n.getProperty("slide", false)) && i + 1 < notes.size())
			end = std::max(end, double(notes[i + 1]["line"]));
		const int vel = n.hasProperty("vel") ? int(n["vel"]) : bool(n.getProperty("accent", false)) ? 127 : 64;
		const int note = parseNote(n["note"].toString());
		events.push_back({start * lineSec, -int(i), true, note, vel});
		events.push_back({end * lineSec, 1 << 20, false, note, 0});
		lastLine = std::max(lastLine, end);
	}
	std::stable_sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
		return a.time != b.time ? a.time < b.time : a.order < b.order;
	});

	tb303::Voice voice;
	voice.setCalibration(g_cal);
	voice.prepare(rate);
	voice.setControls(c);
	tb303::MidiDriver midi;

	const double tail = 2.5;
	const int total = int((lastLine * lineSec + tail) * rate);
	juce::AudioBuffer<float> buffer(1, total);
	float *out = buffer.getWritePointer(0);
	size_t next = 0;
	int pos = 0;
	while (pos < total) {
		while (next < events.size() && int(events[next].time * rate) <= pos) {
			const auto &e = events[next++];
			if (e.on)
				midi.noteOn(e.note, e.velocity);
			else
				midi.noteOff(e.note);
			voice.setLines(midi.lines(), midi.triggers());
		}
		int end = next < events.size() ? std::min(total, int(events[next].time * rate)) : total;
		if (end <= pos)
			end = pos + 1;
		voice.render(out + pos, end - pos);
		pos = end;
	}

	const juce::File file = outDir.getChildFile(name + ".wav");
	file.deleteFile();
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
	if (stream == nullptr)
		return false;
	std::unique_ptr<juce::AudioFormatWriter> writer(
		wav.createWriterFor(stream.get(), rate, 1, 32, {}, 0));
	if (writer == nullptr)
		return false;
	stream.release();
	writer->writeFromAudioSampleBuffer(buffer, 0, total);
	std::printf("%s\n", file.getFullPathName().toRawUTF8());
	return true;
}

} // namespace

int main(int argc, char **argv)
{
	juce::File outDir = juce::File::getCurrentWorkingDirectory();
	double rate = 44100;
	juce::StringArray only, specs, overrides;
	for (int i = 1; i < argc; ++i) {
		const juce::String a(argv[i]);
		if (a == "--out" && i + 1 < argc)
			outDir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
		else if (a == "--rate" && i + 1 < argc)
			rate = juce::String(argv[++i]).getDoubleValue();
		else if (a == "--only" && i + 1 < argc)
			only.addTokens(argv[++i], ",", "");
		else if (a == "--unit")
			g_cal = tb303::unitCalibration();
		else if (a == "--cal" && i + 1 < argc)
			overrides.add(argv[++i]); // e.g. --cal q21Beta=600 --cal vcfRingHz=400
		else
			specs.add(a);
	}
	for (const auto &kv : overrides) {
		const auto key = kv.upToFirstOccurrenceOf("=", false, false);
		const double v = kv.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
		if (key == "vcfRingHz") g_cal.vcfRingHz = v;
		else if (key == "q21Beta") g_cal.q21Beta = v;
		else if (key == "q21TailmA") g_cal.q21TailmA = v;
		else if (key == "tuningRangeCents") g_cal.tuningRangeCents = v;
		else if (key == "megPeakV") g_cal.megPeakV = v;
		else if (key == "decayPotOhms") g_cal.decayPotOhms = v;
		else if (key == "megCapUF") g_cal.megCapUF = v;
		else if (key == "taperAMid") g_cal.taperAMid = v;
		else if (key == "taperAShape") g_cal.taperAShape = v;
		else if (key == "decayTaperMid") g_cal.decayTaperMid = v;
		else if (key == "cutoffTaperMid") g_cal.cutoffTaperMid = v;
		else if (key == "decayTaperShape") g_cal.decayTaperShape = v;
		else if (key == "pin1ShiftV") g_cal.pin1ShiftV = v;
		else if (key == "vcaSwing") g_cal.vcaSwing = v;
		else if (key == "c13UF") g_cal.c13UF = v;
		else if (key == "vr4bOhms") g_cal.vr4bOhms = v;
		else {
			std::fprintf(stderr, "unknown calibration %s\n", key.toRawUTF8());
			return 2;
		}
	}
	if (specs.isEmpty()) {
		std::fprintf(stderr, "usage: tb303render SPEC.json [...] [--out DIR] [--rate HZ] [--only NAME,...] [--unit] [--cal NAME=VALUE]\n");
		return 2;
	}
	outDir.createDirectory();
	for (const auto &path : specs) {
		const auto json = juce::JSON::parse(juce::File::getCurrentWorkingDirectory().getChildFile(path));
		std::vector<juce::var> list;
		if (auto *arr = json.getArray())
			list.assign(arr->begin(), arr->end());
		else
			list.push_back(json);
		for (const auto &spec : list) {
			if (!only.isEmpty()) {
				bool match = false;
				for (auto &o : only)
					match |= spec["name"].toString().contains(o);
				if (!match)
					continue;
			}
			if (!render(spec, outDir, rate))
				return 1;
		}
	}
	return 0;
}
