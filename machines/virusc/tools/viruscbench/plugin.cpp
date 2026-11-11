// The check set's bench, through the plugin's processor as a host runs it (sleep and the resampler
// included), at 44.1 and 48 kHz in 512-sample blocks:
//   viruscbench time [RUNS [WORKLOAD]]   CPU % of a core, the worst block, the share of time asleep
//   viruscbench golden DIR               renders every workload into DIR
//   viruscbench check DIR                renders again and gives the largest difference in dB
//   viruscbench render SCRIPT OUT.wav    plays a MIDI script through the plugin at 48 kHz (the website's
//                                        clips): lines `SECONDS HEXBYTES...`, and `SECONDS end`
// Each render boots its own instance, so they don't depend on each other.
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#if defined(__APPLE__)
#include <libproc.h>
#include <pthread.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr int kBlock = 512;

struct Event {
	double t;
	std::vector<uint8_t> midi; // empty: a knob move
	int knob = -1, value = 0;
};

struct Workload {
	const char *name;
	double seconds;
	std::vector<Event> events;
};

std::vector<uint8_t> programChange(int bank, int program) { return {0xb0, 0x20, uint8_t(bank), 0xc0, uint8_t(program)}; }

std::vector<Workload> workloads()
{
	std::vector<Workload> w;
	w.push_back({"idle", 4.0, {}});
	w.push_back({"one note", 5.0, {{0.5, {0x90, 60, 100}}, {2.5, {0x80, 60, 0}}}});
	{
		// eight notes on F69 "Moon PadJS"
		Workload c{"chord", 6.0, {{0.0, programChange(5, 69)}}};
		for (int n : {48, 52, 55, 59, 60, 64, 67, 71}) {
			c.events.push_back({0.5, {0x90, uint8_t(n), 100}});
			c.events.push_back({3.5, {0x80, uint8_t(n), 0}});
		}
		w.push_back(c);
	}
	{
		// 32 notes, the most it plays, on the same pad
		Workload b{"32 notes", 6.0, {{0.0, programChange(5, 69)}}};
		for (int k = 0; k < 32; ++k) {
			b.events.push_back({0.5 + 0.01 * k, {0x90, uint8_t(36 + 2 * k), 100}});
			b.events.push_back({3.5, {0x80, uint8_t(36 + 2 * k), 0}});
		}
		w.push_back(b);
	}
	{
		// CUTOFF (knob 21) down and up over 4 s under a held note on A0
		Workload a{"cutoff sweep", 5.5, {{0.5, {0x90, 48, 100}}, {4.7, {0x80, 48, 0}}}};
		for (int k = 0; k <= 80; ++k) {
			const double x = k / 80.0;
			a.events.push_back({0.6 + 4.0 * x, {}, 21, int(std::lround(127 * std::abs(1 - 2 * x)))});
		}
		w.push_back(a);
	}
	{
		// A0-A7, a note on each: the patch uploads
		Workload p{"program changes", 5.6, {}};
		for (int k = 0; k < 8; ++k) {
			p.events.push_back({0.7 * k, programChange(0, k)});
			p.events.push_back({0.7 * k + 0.3, {0x90, 60, 100}});
			p.events.push_back({0.7 * k + 0.6, {0x80, 60, 0}});
		}
		w.push_back(p);
	}
	return w;
}

struct Result {
	juce::AudioBuffer<float> audio;
	double seconds = 0, worstMs = 0, asleep = 0;
	double minstr = -1; // millions of instructions retired while rendering (Apple: proc_pid_rusage), -1 if unknown
};

// Instructions this process has retired: steadier than time under load (CONTRIBUTING.md, CPU rounds).
double instructionsRetired()
{
#if defined(__APPLE__)
	rusage_info_v4 ri{};
	if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t *>(&ri)) == 0)
		return double(ri.ri_instructions);
#endif
	return -1;
}

Result render(const juce::MemoryBlock &rom, const Workload &w, double rate)
{
	Result r;
	VirusCProcessor p(&rom);
	// VC_BENCH_JACKS: every bus on, OUT 2, OUT 3 and the side-chain (fed silence), for the cost of the
	// jacks' resampling. The renders stay OUT 1's.
	if (std::getenv("VC_BENCH_JACKS") != nullptr) {
		auto layout = p.getBusesLayout();
		layout.outputBuses.getReference(1) = layout.outputBuses.getReference(2) = juce::AudioChannelSet::stereo();
		layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
		p.setBusesLayout(layout);
	}
	p.prepareToPlay(rate, kBlock);
	p.waitForBoot();
	const int total = int(w.seconds * rate), channels = p.getTotalNumOutputChannels();
	r.audio.setSize(2, total);
	juce::AudioBuffer<float> buffer(channels, kBlock);
	size_t next = 0;
	int blocks = 0, asleep = 0;
	const double instr0 = instructionsRetired();
	for (int at = 0; at < total; at += kBlock) {
		const int n = std::min(kBlock, total - at);
		buffer.setSize(channels, n, false, false, true);
		buffer.clear();
		juce::MidiBuffer midi;
		for (; next < w.events.size() && w.events[next].t * rate < at + n; ++next) {
			const auto &e = w.events[next];
			if (e.midi.empty()) {
				auto *param = p.params().getParameter("knob" + juce::String(e.knob));
				param->setValueNotifyingHost(param->convertTo0to1(float(e.value)));
			} else
				midi.addEvent(e.midi.data(), int(e.midi.size()), juce::jlimit(0, n - 1, int(e.t * rate) - at));
		}
		const auto t0 = std::chrono::steady_clock::now();
		p.processBlock(buffer, midi);
		const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		r.seconds += s;
		r.worstMs = std::max(r.worstMs, s * 1000);
		++blocks;
		asleep += p.asleep();
		for (int c = 0; c < 2; ++c)
			r.audio.copyFrom(c, at, buffer, c, 0, n);
	}
	r.asleep = blocks ? 100.0 * asleep / blocks : 0;
	if (const double instr1 = instructionsRetired(); instr0 >= 0 && instr1 >= 0)
		r.minstr = (instr1 - instr0) / 1e6;
	return r;
}

bool writeWav(const juce::File &f, const juce::AudioBuffer<float> &b, double rate)
{
	f.deleteFile();
	std::unique_ptr<juce::OutputStream> out = f.createOutputStream();
	if (!out)
		return false;
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(out.release(), rate, 2, 32, {}, 0));
	return w && w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

bool readWav(const juce::File &f, juce::AudioBuffer<float> &b)
{
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::AudioFormatReader> r(wav.createReaderFor(f.createInputStream().release(), true));
	if (!r)
		return false;
	b.setSize(int(r->numChannels), int(r->lengthInSamples));
	r->read(&b, 0, int(r->lengthInSamples), 0, true, true);
	return true;
}

// A script of timed MIDI messages: `0.5 90 3c 64` (hex bytes), `# comments`, `12.0 end`.
bool readScript(const juce::File &f, Workload &w)
{
	w.name = "script";
	w.seconds = 0;
	for (auto line : juce::StringArray::fromLines(f.loadFileAsString())) {
		line = line.upToFirstOccurrenceOf("#", false, false).trim();
		if (line.isEmpty())
			continue;
		auto tokens = juce::StringArray::fromTokens(line, " \t", "");
		tokens.removeEmptyStrings();
		const double t = tokens[0].getDoubleValue();
		if (tokens[1] == "end") {
			w.seconds = t;
			continue;
		}
		Event e{t, {}};
		for (int i = 1; i < tokens.size(); ++i)
			e.midi.push_back(uint8_t(tokens[i].getHexValue32()));
		w.events.push_back(e);
	}
	std::stable_sort(w.events.begin(), w.events.end(), [](const Event &a, const Event &b) { return a.t < b.t; });
	return w.seconds > 0;
}

} // namespace

int pluginBench(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI init;
#if defined(__APPLE__)
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	const juce::String mode = argv[1];
	if (mode == "render") {
		Workload w;
		juce::MemoryBlock rom;
		if (argc < 4 || !readScript(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]), w) ||
		    !juce::File(VIRUSC_ROM).loadFileAsData(rom)) {
			std::printf("usage: viruscbench render SCRIPT OUT.wav (a script with an end line; the ROM at %s)\n", VIRUSC_ROM);
			return 2;
		}
		const auto r = render(rom, w, 48000.0);
		return writeWav(juce::File::getCurrentWorkingDirectory().getChildFile(argv[3]), r.audio, 48000.0) ? 0 : 1;
	}
	const bool hasDir = mode == "golden" || mode == "check";
	if (hasDir && argc < 3) {
		std::printf("usage: viruscbench time [RUNS [WORKLOAD]] | golden DIR | check DIR\n");
		return 2;
	}
	const juce::File dir = hasDir ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File();
	const int runs = !hasDir && argc > 2 ? juce::String(argv[2]).getIntValue() : 1;
	const juce::String only = !hasDir && argc > 3 ? argv[3] : "";
	if (mode == "golden")
		dir.createDirectory();
	juce::MemoryBlock rom;
	if (!juce::File(VIRUSC_ROM).loadFileAsData(rom)) {
		std::printf("No ROM at %s\n", VIRUSC_ROM);
		return 1;
	}

	const bool check = mode == "check";
	int missing = 0;
	double worstDb = -400;
	std::printf("%-16s %6s %8s %10s %9s %9s %s\n", "workload", "rate", "CPU %", "worst ms", "asleep %", "Minstr/s", check ? "max diff dB" : "");
	for (const auto &w : workloads())
		for (double rate : {44100.0, 48000.0}) {
			if (only.isNotEmpty() && only != w.name)
				continue;
			Result best;
			for (int k = 0; k < std::max(1, runs); ++k) {
				auto r = render(rom, w, rate);
				if (k == 0 || r.seconds < best.seconds)
					best = std::move(r);
			}
			const double cpu = 100 * best.seconds / w.seconds;
			const auto file = dir.getChildFile(juce::String(w.name).replaceCharacter(' ', '-') + "-" + juce::String(int(rate)) + ".wav");
			juce::String extra;
			if (mode == "golden")
				writeWav(file, best.audio, rate);
			else if (check) {
				juce::AudioBuffer<float> ref;
				if (!readWav(file, ref) || ref.getNumSamples() != best.audio.getNumSamples() || ref.getNumChannels() != 2) {
					extra = "no golden";
					++missing;
				} else {
					double d = 0;
					for (int c = 0; c < 2; ++c)
						for (int i = 0; i < ref.getNumSamples(); ++i)
							d = std::max(d, double(std::abs(best.audio.getSample(c, i) - ref.getSample(c, i))));
					const double db = d > 0 ? 20 * std::log10(d) : -400;
					worstDb = std::max(worstDb, db);
					extra = d > 0 ? juce::String(db, 1) : juce::String("identical");
				}
			}
			std::printf("%-16s %6d %8.2f %10.2f %9.1f %9.1f %s\n", w.name, int(rate), cpu, best.worstMs, best.asleep,
			            best.minstr >= 0 ? best.minstr / w.seconds : -1.0, extra.toRawUTF8());
			std::fflush(stdout);
		}
	if (check && missing)
		std::printf("%d without a golden render\n", missing);
	else if (check && worstDb <= -400)
		std::printf("all identical to the golden renders\n");
	else if (check)
		std::printf("largest difference %.1f dB\n", worstDb);
	return check && (missing || worstDb > -400) ? 1 : 0;
}
