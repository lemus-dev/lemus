// sh101render: renders test specs (tools/compare/specs/*.json) through the plugin's processor,
// headless, so the result is exactly what the plugin plays: MIDI into the firmware's keyboard,
// the CPU's lines into the synth board.
//
//   sh101render SPEC.json [SPEC.json ...] [--out DIR] [--rate 44100] [--only NAME,NAME]
//               [--cal NAME=VALUE ...] [--calibration service|real|roland]
//
// Spec format (shared with tools/compare/roland_render.py):
//   {"name": "saw-sweep", "bpm": 120, "lpb": 4, "lines": 32, "tail": 2.0,
//    "params": {"sawLevel": 1, "vcfFreq": 0.4, "lfoWave": 1, ...},
//    "notes": [{"line": 0, "note": "C-3", "len": 2, "vel": 100}, ...],
//    "bend": [{"line": 4, "value": 0.5}], "mod": [{"line": 8, "value": 1}],
//    "buttons": [{"line": 0, "button": "arpUp", "len": 0.5}],
//    "hold": [{"line": 0, "len": 16}]}
// Specs marked "roland_only" (raw Roland values, specs/probes.json) are skipped.
// Slider params are travel 0-1; choice params are the item index. Notes are Onyx Tracker names
// (C-3 = MIDI 48). Lines are rows at "lpb" rows per beat.
#include "PluginProcessor.h"

namespace {
// The CPU's mask ROM, from the private folder (SH101_ROM_PATH, set by machine.cmake).
const juce::MemoryBlock &sh101Rom()
{
	static const juce::MemoryBlock rom = [] {
		juce::MemoryBlock b;
		juce::File(SH101_ROM_PATH).loadFileAsData(b);
		return b;
	}();
	return rom;
}
} // namespace

#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

namespace {

int parseNote(const juce::String &s)
{
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

struct Event {
	double time;
	int order; // at equal times: offs before ons, so a repeated note is a new key press
	enum Kind { NoteOn, NoteOff, Bend, Mod, ButtonDown, ButtonUp, HoldOn, HoldOff } kind;
	int a = 0;
	double v = 0;
};

const std::pair<const char *, sh101::Machine::Button> kButtons[] = {
	{"load", sh101::Machine::Button::Load},       {"play", sh101::Machine::Button::Play},
	{"arpDown", sh101::Machine::Button::ArpDown}, {"arpUpDown", sh101::Machine::Button::ArpUpDown},
	{"arpUp", sh101::Machine::Button::ArpUp},     {"hold", sh101::Machine::Button::Hold},
	{"keyTranspose", sh101::Machine::Button::KeyTranspose},
};

sh101::Calibration g_cal;
bool g_hasCal = false;
int g_calibration = -1;

bool render(const juce::var &spec, const juce::File &outDir, double rate)
{
	const juce::String name = spec["name"].toString();
	const double bpm = spec.getProperty("bpm", 120.0);
	const double lpb = spec.getProperty("lpb", 4.0);
	const double lineSec = 60.0 / bpm / lpb;

	SH101Processor proc(&sh101Rom());
	proc.settlePower(); // the unit switched on long before, as the recordings it is compared with
	auto &params = proc.params();
	if (auto *obj = spec["params"].getDynamicObject()) {
		for (const auto &kv : obj->getProperties()) {
			auto *p = params.getParameter(kv.name.toString());
			if (p == nullptr) {
				std::fprintf(stderr, "%s: unknown parameter %s\n", name.toRawUTF8(), kv.name.toString().toRawUTF8());
				return false;
			}
			const double v = kv.value;
			if (auto *c = dynamic_cast<juce::AudioParameterChoice *>(p))
				p->setValueNotifyingHost(c->convertTo0to1(float(v)));
			else if (auto *b = dynamic_cast<juce::AudioParameterBool *>(p))
				p->setValueNotifyingHost(v > 0.5 ? 1.0f : 0.0f);
			else
				p->setValueNotifyingHost(p->convertTo0to1(float(v)));
		}
	}
	if (g_hasCal)
		proc.setCalibrationOverride(g_cal);
	else if (g_calibration >= 0)
		proc.setCalibrationOverride(sh101::Calibration::byIndex(g_calibration));

	std::vector<Event> events;
	double lastLine = spec.getProperty("lines", 0.0);
	if (auto *arr = spec["notes"].getArray()) {
		for (const auto &n : *arr) {
			const double start = n["line"], end = start + double(n.getProperty("len", 1.0));
			const int note = parseNote(n["note"].toString());
			const int vel = n.getProperty("vel", 100);
			events.push_back({start * lineSec, 1, Event::NoteOn, note, double(vel)});
			events.push_back({end * lineSec, 0, Event::NoteOff, note});
			lastLine = std::max(lastLine, end);
		}
	}
	auto addValues = [&](const char *key, Event::Kind kind) {
		if (auto *arr = spec[key].getArray())
			for (const auto &e : *arr)
				events.push_back({double(e["line"]) * lineSec, 2, kind, 0, double(e["value"])});
	};
	addValues("bend", Event::Bend);
	addValues("mod", Event::Mod);
	if (auto *arr = spec["buttons"].getArray()) {
		for (const auto &b : *arr) {
			int which = -1;
			for (const auto &[n, btn] : kButtons)
				if (b["button"].toString() == n)
					which = int(btn);
			if (which < 0) {
				std::fprintf(stderr, "%s: unknown button %s\n", name.toRawUTF8(), b["button"].toString().toRawUTF8());
				return false;
			}
			const double start = b["line"], len = b.getProperty("len", 0.5);
			events.push_back({start * lineSec, 3, Event::ButtonDown, which});
			events.push_back({(start + len) * lineSec, 3, Event::ButtonUp, which});
		}
	}
	if (auto *arr = spec["hold"].getArray()) {
		for (const auto &h : *arr) {
			const double start = h["line"], len = h.getProperty("len", 1.0);
			events.push_back({start * lineSec, 3, Event::HoldOn});
			events.push_back({(start + len) * lineSec, 3, Event::HoldOff});
			lastLine = std::max(lastLine, start + len);
		}
	}
	std::stable_sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
		return a.time != b.time ? a.time < b.time : a.order < b.order;
	});

	const int block = 256;
	proc.setRateAndBufferSizeDetails(rate, block);
	proc.prepareToPlay(rate, block);
	// Let the firmware boot (INITIAL SET) before the first event, as a powered-on unit would be.
	const double preroll = spec.getProperty("preroll", 0.25);
	const double tail = spec.getProperty("tail", 2.5);
	const int pre = int(preroll * rate);
	const int total = pre + int((lastLine * lineSec + tail) * rate);
	juce::AudioBuffer<float> outBuf(1, total - pre);
	juce::AudioBuffer<float> buf(2, block);
	size_t next = 0;
	for (int pos = 0; pos < total; pos += block) {
		const int n = std::min(block, total - pos);
		buf.setSize(2, n, false, false, true);
		buf.clear();
		juce::MidiBuffer midi;
		while (next < events.size() && pre + int(events[next].time * rate) < pos + n) {
			const auto &e = events[next++];
			const int at = std::max(0, pre + int(e.time * rate) - pos);
			switch (e.kind) {
			case Event::NoteOn: midi.addEvent(juce::MidiMessage::noteOn(1, e.a, juce::uint8(e.v)), at); break;
			case Event::NoteOff: midi.addEvent(juce::MidiMessage::noteOff(1, e.a), at); break;
			case Event::Bend: midi.addEvent(juce::MidiMessage::pitchWheel(1, juce::jlimit(0, 16383, int(8192 + e.v * 8191))), at); break;
			case Event::Mod: midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, juce::jlimit(0, 127, int(e.v * 127))), at); break;
			case Event::HoldOn: midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), at); break;
			case Event::HoldOff: midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), at); break;
			// Panel buttons apply at the start of the block they fall in (<= 5.8 ms at 44.1 kHz).
			case Event::ButtonDown: proc.setButton(sh101::Machine::Button(e.a), true); break;
			case Event::ButtonUp: proc.setButton(sh101::Machine::Button(e.a), false); break;
			}
		}
		proc.processBlock(buf, midi);
		for (int i = 0; i < n; ++i)
			if (pos + i >= pre)
				outBuf.setSample(0, pos + i - pre, buf.getSample(0, i));
	}

	const juce::File file = outDir.getChildFile(name + ".wav");
	file.deleteFile();
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
	if (stream == nullptr)
		return false;
	auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
	if (writer == nullptr)
		return false;
	writer->writeFromAudioSampleBuffer(outBuf, 0, outBuf.getNumSamples());
	std::printf("%s\n", file.getFullPathName().toRawUTF8());
	return true;
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI juceInit;
	juce::File outDir = juce::File::getCurrentWorkingDirectory();
	double rate = 44100;
	juce::StringArray only, specs;
	for (int i = 1; i < argc; ++i) {
		const juce::String a(argv[i]);
		if (a == "--out" && i + 1 < argc)
			outDir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
		else if (a == "--rate" && i + 1 < argc)
			rate = juce::String(argv[++i]).getDoubleValue();
		else if (a == "--only" && i + 1 < argc)
			only.addTokens(argv[++i], ",", "");
		else if (a == "--calibration" && i + 1 < argc) {
			const juce::String c(argv[++i]);
			g_calibration = c == "service" ? 0 : c == "roland" ? 2 : 1;
		}
		else if (a == "--cal" && i + 1 < argc) {
			const juce::String kv(argv[++i]);
			if (!g_hasCal) {
				g_cal = sh101::Calibration::byIndex(g_calibration < 0 ? 1 : g_calibration);
				g_hasCal = true;
			}
			const auto key = kv.upToFirstOccurrenceOf("=", false, false);
			if (!g_cal.set(key.toStdString(), kv.fromFirstOccurrenceOf("=", false, false).getDoubleValue())) {
				std::fprintf(stderr, "unknown calibration %s\n", key.toRawUTF8());
				return 2;
			}
		} else
			specs.add(a);
	}
	if (specs.isEmpty()) {
		std::fprintf(stderr, "usage: sh101render SPEC.json [...] [--out DIR] [--rate HZ] [--only NAME,...] "
		                     "[--calibration service|real|roland] [--cal NAME=VALUE ...]\n");
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
			if (bool(spec.getProperty("roland_only", false)))
				continue; // raw Roland values with no counterpart on our panel (specs/probes.json)
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
