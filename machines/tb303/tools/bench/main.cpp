// CPU benchmark and golden-reference check for the TB-303 (TB303Processor: CPU board, voice,
// sleep). Renders a fixed set of workloads at 44.1 and 48 kHz, times each (best of N runs), and
// writes them as golden WAVs or compares them with golden WAVs written earlier, so speed work
// can show it leaves the sound alone.
//   tb303bench golden DIR [RUNS]   render, time, and write DIR/<workload>-<rate>.wav
//   tb303bench check DIR [RUNS]    render, time, and compare with DIR: max |difference| in dBFS
//   tb303bench time [RUNS [NAME]]  render and time only (one workload by name, for profiling)
#include "PluginProcessor.h"
#include "tb303/core/patterns.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {
struct PlayHead : juce::AudioPlayHead {
	juce::Optional<PositionInfo> getPosition() const override
	{
		PositionInfo p;
		p.setIsPlaying(false);
		p.setBpm(125);
		return p;
	}
};

void set(TB303Processor &p, const char *id, float v)
{
	p.params().getParameter(id)->setValueNotifyingHost(v);
}

// A 16th-note acid line at 125 BPM over three octaves: accents (velocity 127) and slides
// (overlapping notes), with a few rests.
juce::MidiBuffer acidLine(double rate, double seconds)
{
	juce::MidiBuffer m;
	const int notes[] = {36, 36, 48, 39, -1, 41, 36, 43, 46, 36, 60, 34, -1, 38, 50, 31};
	const bool acc[] = {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0};
	const bool slide[] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0};
	const double step = 60.0 / 125 / 4;
	for (int i = 0; i * step < seconds - step; ++i) {
		const int k = i % 16;
		if (notes[k] < 0)
			continue;
		const int on = int((0.05 + i * step) * rate);
		const int off = int((0.05 + i * step + (slide[k] ? 1.2 : 0.5) * step) * rate);
		m.addEvent(juce::MidiMessage::noteOn(1, notes[k], juce::uint8(acc[k] ? 127 : 90)), on);
		m.addEvent(juce::MidiMessage::noteOff(1, notes[k]), off);
	}
	return m;
}

struct Workload {
	const char *name;
	double seconds;
	std::function<void(TB303Processor &)> setup;      // knobs, before prepare
	bool midi = true;                                  // play the acid line
	bool run = false;                                  // RUN: the test pattern on the internal clock
	std::function<void(TB303Processor &, double)> automate; // per block, given the time
};

std::vector<Workload> workloads()
{
	std::vector<Workload> w;
	w.push_back({"sequencer", 8, [](TB303Processor &) {}, false, true, {}});
	w.push_back({"midi", 8, [](TB303Processor &) {}, true, false, {}});
	w.push_back({"resmax-cutmax", 6, [](TB303Processor &p) {
		             set(p, "resonance", 1);
		             set(p, "cutoff", 1);
		             set(p, "envmod", 1);
		             set(p, "decay", 0);
		             set(p, "accent", 1);
		             set(p, "volume", 1);
	             }, true, false, {}});
	w.push_back({"resmax-cutmin-square", 6, [](TB303Processor &p) {
		             set(p, "resonance", 1);
		             set(p, "cutoff", 0);
		             set(p, "envmod", 0.3f);
		             set(p, "decay", 1);
		             set(p, "accent", 1);
		             set(p, "waveform", 1);
	             }, true, false, {}});
	w.push_back({"square-trims", 6, [](TB303Processor &p) {
		             set(p, "waveform", 1);
		             set(p, "tm3", float(tb303::trims::tm3For(1000 / 3.0))); // a 3.0 ms ring
		             set(p, "fullcutoff", 1);
		             set(p, "accent", 0);
		             set(p, "decay", 0.2f);
	             }, true, false, {}});
	w.push_back({"automation", 8, [](TB303Processor &) {}, true, false, [](TB303Processor &p, double t) {
		             set(p, "cutoff", float(0.5 + 0.5 * std::sin(t * 1.3)));
		             set(p, "resonance", float(0.5 + 0.5 * std::sin(t * 0.7 + 1)));
		             set(p, "envmod", float(0.5 + 0.5 * std::sin(t * 0.9 + 2)));
		             set(p, "decay", float(0.5 + 0.5 * std::sin(t * 0.5 + 3)));
		             set(p, "accent", float(0.5 + 0.5 * std::sin(t * 1.1 + 4)));
		             set(p, "tuning", float(0.5 + 0.1 * std::sin(t * 2.3)));
		             set(p, "volume", float(0.6 + 0.4 * std::sin(t * 0.3)));
	             }});
	// A long run at full cutoff and resonance: the self-oscillating filter, where any numerical
	// difference would show by growing.
	w.push_back({"longrun-resmax", 20, [](TB303Processor &p) {
		             set(p, "cutoff", 1);
		             set(p, "resonance", 1);
		             set(p, "envmod", 0.6f);
		             set(p, "decay", 0.4f);
		             set(p, "accent", 0.8f);
		             set(p, "tm3", 0.5f); // a 2.0 ms ring
	             }, true, false, {}});
	// The waveform switch flipped mid-note: the square shaper runs in saw mode too, and its state
	// at the switch must not change.
	w.push_back({"waveform-switch", 6, [](TB303Processor &) {}, true, false, [](TB303Processor &p, double t) {
		             set(p, "waveform", int(t / 0.37) % 2 == 0 ? 0.0f : 1.0f);
	             }});
	// One note, then silence: mostly asleep.
	w.push_back({"idle", 8, [](TB303Processor &) {}, false, false, {}});
	return w;
}

juce::AudioBuffer<float> render(const juce::MemoryBlock &rom, const Workload &w, double rate, double &seconds)
{
	TB303Processor p(&rom);
	PlayHead ph;
	p.setPlayHead(&ph);
	w.setup(p);
	if (w.run)
		p.writePattern(0, 0, 0, tb303::parsePattern(tb303::kTestPattern));
	p.prepareToPlay(rate, 512);
	const int block = 512;
	const int total = int(w.seconds * rate);
	juce::MidiBuffer all = w.midi ? acidLine(rate, w.seconds) : juce::MidiBuffer();
	if (std::string(w.name) == "idle") {
		all.addEvent(juce::MidiMessage::noteOn(1, 36, juce::uint8(127)), int(0.1 * rate));
		all.addEvent(juce::MidiMessage::noteOff(1, 36), int(0.3 * rate));
	}
	juce::AudioBuffer<float> out(1, total), buf(2, block);
	if (w.run)
		p.toggleRun();
	const auto t0 = std::chrono::steady_clock::now();
	for (int pos = 0; pos < total; pos += block) {
		const int n = std::min(block, total - pos);
		buf.setSize(2, n, false, false, true);
		buf.clear();
		juce::MidiBuffer midi;
		for (const auto m : all)
			if (m.samplePosition >= pos && m.samplePosition < pos + n)
				midi.addEvent(m.getMessage(), m.samplePosition - pos);
		if (w.automate)
			w.automate(p, pos / rate);
		p.processBlock(buf, midi);
		out.copyFrom(0, pos, buf, 0, 0, n);
	}
	seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	return out;
}

void writeWav(const juce::File &f, const juce::AudioBuffer<float> &b, double rate)
{
	f.deleteFile();
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> s(f.createOutputStream());
	std::unique_ptr<juce::AudioFormatWriter> wr(wav.createWriterFor(s.get(), rate, 1, 32, {}, 0));
	s.release();
	wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

bool readWav(const juce::File &f, juce::AudioBuffer<float> &b)
{
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::AudioFormatReader> r(wav.createReaderFor(f.createInputStream().release(), true));
	if (r == nullptr)
		return false;
	b.setSize(1, int(r->lengthInSamples));
	r->read(&b, 0, int(r->lengthInSamples), 0, true, false);
	return true;
}
} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI init;
	const juce::String mode = argc > 1 ? argv[1] : "time";
	const bool hasDir = mode == "golden" || mode == "check";
	const juce::File dir = hasDir && argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File();
	const int runs = argc > (hasDir ? 3 : 2) ? juce::String(argv[hasDir ? 3 : 2]).getIntValue() : 3;
	if (hasDir && dir == juce::File()) {
		std::printf("usage: tb303bench golden|check DIR [RUNS] | time [RUNS]\n");
		return 2;
	}
	if (mode == "golden")
		dir.createDirectory();
	juce::MemoryBlock rom;
	juce::File(TB303_ROM_PATH).loadFileAsData(rom);

	double worstDb = -400, totalCpu = 0;
	int missing = 0;
	std::printf("%-22s %6s %9s %12s\n", "workload", "rate", "CPU %", mode == "check" ? "max diff dB" : "");
	const juce::String only = !hasDir && argc > 3 ? argv[3] : "";
	for (const auto &w : workloads())
		for (double rate : {44100.0, 48000.0}) {
			if (only.isNotEmpty() && only != w.name)
				continue;
			juce::AudioBuffer<float> out;
			double best = 1e9;
			for (int r = 0; r < std::max(1, runs); ++r) {
				double s = 0;
				out = render(rom, w, rate, s);
				best = std::min(best, s);
			}
			const double cpu = 100 * best / w.seconds;
			totalCpu += cpu;
			const auto file = dir.getChildFile(juce::String(w.name) + "-" + juce::String(int(rate)) + ".wav");
			juce::String extra;
			if (mode == "golden") {
				writeWav(file, out, rate);
			} else if (mode == "check") {
				juce::AudioBuffer<float> ref;
				if (!readWav(file, ref) || ref.getNumSamples() != out.getNumSamples()) {
					extra = "no golden";
					++missing;
				} else {
					double d = 0;
					for (int i = 0; i < out.getNumSamples(); ++i)
						d = std::max(d, double(std::abs(out.getSample(0, i) - ref.getSample(0, i))));
					const double db = d > 0 ? 20 * std::log10(d) : -400;
					worstDb = std::max(worstDb, db);
					extra = d > 0 ? juce::String(db, 1) : juce::String("identical");
				}
			}
			std::printf("%-22s %6d %9.2f %12s\n", w.name, int(rate), cpu, extra.toRawUTF8());
		}
	std::printf("total CPU %% (sum of workloads): %.2f\n", totalCpu);
	if (mode == "check")
		std::printf("worst difference: %s\n", worstDb <= -400 ? "identical (bit-exact)" : (juce::String(worstDb, 1) + " dBFS").toRawUTF8());
	return missing > 0 ? 1 : 0;
}
