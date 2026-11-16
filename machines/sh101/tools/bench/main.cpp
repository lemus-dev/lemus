// CPU benchmark and golden-reference check for the SH-101 (SH101Processor: CPU board, synth board,
// sleep). Renders a fixed set of workloads at 44.1 and 48 kHz, times each (best of N runs), and
// writes them as golden WAVs or compares them with golden WAVs written earlier, so speed work
// can show it leaves the sound alone. Every workload starts switched on long before (no power-on
// mute). CPU % is the rendering thread's CPU time against the audio's length (on Apple Silicon,
// asked for on the performance cores); Mcyc/s and Mins/s the CPU cycles and instructions a second
// of audio took (Apple Silicon): instructions hold within about 0.2 % from run to run, cycles and
// time within a few %, so instructions show a small change. Worst % is the slowest 512-sample
// block's time against its length: what a host's meter shows.
//   sh101bench golden DIR [RUNS]   render, time, and write DIR/<workload>-<rate>.wav
//   sh101bench check DIR [RUNS]    render, time, and compare with DIR: max |difference| in dBFS
//   sh101bench time [RUNS [NAME]]  render and time only (one workload by name, for profiling)
//   sh101bench idle                 a few patches after a note: their resting level, whether the
//                                   voice sleeps and when
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>

#if defined(__APPLE__)
#include <libproc.h>
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
using Id = SH101Processor::Id;

struct PlayHead : juce::AudioPlayHead {
	juce::Optional<PositionInfo> getPosition() const override
	{
		PositionInfo p;
		p.setIsPlaying(false);
		p.setBpm(120);
		return p;
	}
};

void set(SH101Processor &p, const char *id, float v)
{
	auto *param = p.params().getParameter(id);
	if (auto *c = dynamic_cast<juce::AudioParameterChoice *>(param))
		param->setValueNotifyingHost(c->convertTo0to1(v));
	else
		param->setValueNotifyingHost(v);
}

// 16ths at 120 BPM over two octaves, some legato (overlapping) and a few rests.
juce::MidiBuffer line(double rate, double seconds)
{
	juce::MidiBuffer m;
	const int notes[] = {48, 48, 60, 51, -1, 53, 48, 55, 58, 48, 72, 46, -1, 50, 62, 43};
	const bool legato[] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0};
	const double step = 60.0 / 120 / 4;
	for (int i = 0; i * step < seconds - step; ++i) {
		const int k = i % 16;
		if (notes[k] < 0)
			continue;
		const int on = int((0.05 + i * step) * rate);
		const int off = int((0.05 + i * step + (legato[k] ? 1.2 : 0.5) * step) * rate);
		m.addEvent(juce::MidiMessage::noteOn(1, notes[k], juce::uint8(100)), on);
		m.addEvent(juce::MidiMessage::noteOff(1, notes[k]), off);
	}
	return m;
}

struct Workload {
	const char *name;
	double seconds;
	std::function<void(SH101Processor &)> setup; // sliders and switches, before prepare
	bool midi = true;                            // play the line
	bool play = false;                           // PLAY: the demo sequence on the MODULATOR clock
	std::function<void(SH101Processor &, double)> automate; // per block, given the time
};

std::vector<Workload> workloads()
{
	std::vector<Workload> w;
	w.push_back({"sequencer", 8, [](SH101Processor &) {}, false, true, {}});
	w.push_back({"midi", 8, [](SH101Processor &) {}, true, false, {}});
	w.push_back({"bright", 6, [](SH101Processor &p) {
		             set(p, Id::pulseLevel, 1);
		             set(p, Id::sawLevel, 1);
		             set(p, Id::subLevel, 1);
		             set(p, Id::noiseLevel, 0.3f);
		             set(p, Id::vcfFreq, 0.8f);
		             set(p, Id::vcfRes, 0.7f);
		             set(p, Id::vcfEnv, 0.6f);
		             set(p, Id::sustain, 0.8f);
		             set(p, Id::volume, 1);
	             }, true, false, {}});
	w.push_back({"pwm-lfo-porta", 6, [](SH101Processor &p) {
		             set(p, Id::pulseLevel, 1);
		             set(p, Id::sawLevel, 0);
		             set(p, Id::pwmSource, 0);
		             set(p, Id::pulseWidth, 0.6f);
		             set(p, Id::vcoMod, 0.3f);
		             set(p, Id::lfoRate, 0.6f);
		             set(p, Id::portaMode, 0);
		             set(p, Id::portamento, 0.5f);
		             set(p, Id::trigger, 2);
	             }, true, false, {}});
	// The self-oscillating filter held for a long run, where numerical differences would grow.
	w.push_back({"longrun-resmax", 16, [](SH101Processor &p) {
		             set(p, Id::vcfRes, 1);
		             set(p, Id::vcfFreq, 0.6f);
		             set(p, Id::vcfEnv, 0.4f);
		             set(p, Id::vcfKybd, 1);
		             set(p, Id::sustain, 1);
	             }, true, false, {}});
	w.push_back({"automation", 8, [](SH101Processor &) {}, true, false, [](SH101Processor &p, double t) {
		             set(p, Id::vcfFreq, float(0.5 + 0.4 * std::sin(t * 1.3)));
		             set(p, Id::vcfRes, float(0.5 + 0.5 * std::sin(t * 0.7 + 1)));
		             set(p, Id::vcfEnv, float(0.5 + 0.5 * std::sin(t * 0.9 + 2)));
		             set(p, Id::decay, float(0.5 + 0.5 * std::sin(t * 0.5 + 3)));
		             set(p, Id::pulseWidth, float(0.5 + 0.5 * std::sin(t * 1.1 + 4)));
		             set(p, Id::tune, float(0.5 + 0.1 * std::sin(t * 2.3)));
		             set(p, Id::volume, float(0.6 + 0.4 * std::sin(t * 0.3)));
	             }});
	// One note, then silence: mostly asleep.
	w.push_back({"idle", 8, [](SH101Processor &) {}, false, false, {}});
	return w;
}

// This thread's CPU time (the processor runs on it alone): steadier than the wall clock on a busy
// machine.
double threadSeconds()
{
	timespec t{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return double(t.tv_sec) + 1e-9 * double(t.tv_nsec);
}

// The process's CPU cycles and instructions so far (Apple Silicon; 0 elsewhere): unlike the time,
// the same on a loaded machine, whichever cores the thread gets.
double cycles(double *instructions)
{
#if defined(__APPLE__)
	rusage_info_v4 r{};
	if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t *>(&r)) == 0) {
		*instructions = double(r.ri_instructions);
		return double(r.ri_cycles);
	}
#endif
	*instructions = 0;
	return 0;
}

struct Timing {
	double seconds = 0, cycles = 0, instructions = 0, worst = 0; // worst: the slowest block's time / its length
	double asleepShare = 0;
};

juce::AudioBuffer<float> render(const juce::MemoryBlock &rom, const Workload &w, double rate, Timing &t)
{
	SH101Processor p(&rom);
	p.settlePower();
	PlayHead ph;
	p.setPlayHead(&ph);
	w.setup(p);
	if (w.play)
		p.writeSequence(sh101::Machine::demoSequence());
	const int block = 512;
	p.prepareToPlay(rate, block);
	const int total = int(w.seconds * rate);
	juce::MidiBuffer all = w.midi ? line(rate, w.seconds) : juce::MidiBuffer();
	if (std::string(w.name) == "idle") {
		all.addEvent(juce::MidiMessage::noteOn(1, 48, juce::uint8(100)), int(0.1 * rate));
		all.addEvent(juce::MidiMessage::noteOff(1, 48), int(0.3 * rate));
	}
	juce::AudioBuffer<float> out(1, total), buf(2, block);
	int asleepBlocks = 0, blocks = 0;
	double i0 = 0;
	const double t0 = threadSeconds(), c0 = cycles(&i0);
	t.worst = 0;
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
		if (w.play)
			p.setButton(sh101::Machine::Button::Play, pos < int(0.05 * rate));
		const auto b0 = std::chrono::steady_clock::now();
		p.processBlock(buf, midi);
		t.worst = std::max(t.worst, std::chrono::duration<double>(std::chrono::steady_clock::now() - b0).count() * rate / n);
		out.copyFrom(0, pos, buf, 0, 0, n);
		asleepBlocks += p.voiceAsleep() ? 1 : 0;
		++blocks;
	}
	t.seconds = threadSeconds() - t0;
	double i1 = 0;
	t.cycles = cycles(&i1) - c0;
	t.instructions = i1 - i0;
	t.asleepShare = blocks > 0 ? double(asleepBlocks) / blocks : 0;
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
// After a note: the resting level (the closed VCA's leak, docs/model.md) and whether it sleeps.
int idleLevels(const juce::MemoryBlock &rom)
{
	struct Patch {
		const char *name;
		std::vector<std::pair<const char *, float>> set;
	};
	const std::vector<Patch> patches = {{"new instance", {}},
	                                    {"FREQ 0", {{Id::vcfFreq, 0.0f}}},
	                                    {"FREQ 5", {{Id::vcfFreq, 0.5f}}},
	                                    {"FREQ 10", {{Id::vcfFreq, 1.0f}}},
	                                    {"SUB 0", {{Id::subLevel, 0.0f}}},
	                                    {"RES 0", {{Id::vcfRes, 0.0f}}},
	                                    {"VOLUME 3", {{Id::volume, 0.3f}}},
	                                    {"all sources, FREQ 10", {{Id::vcfFreq, 1.0f}, {Id::pulseLevel, 1.0f}, {Id::subLevel, 1.0f}}}};
	for (const auto &patch : patches) {
		SH101Processor p(&rom);
		p.settlePower();
		for (const auto &[id, v] : patch.set)
			set(p, id, v);
		const double rate = 48000;
		const int block = 512;
		p.prepareToPlay(rate, block);
		juce::AudioBuffer<float> buf(2, block);
		float pk = 0;
		double asleepAt = -1;
		const int offBlock = 30;
		for (int b = 0; b < int(12 * rate / block); ++b) {
			juce::MidiBuffer m;
			if (b == 2)
				m.addEvent(juce::MidiMessage::noteOn(1, 48, juce::uint8(100)), 0);
			if (b == offBlock)
				m.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
			p.processBlock(buf, m);
			if (b * block > 10 * rate)
				pk = std::max(pk, buf.getMagnitude(0, 0, block));
			if (asleepAt < 0 && p.voiceAsleep())
				asleepAt = (b - offBlock) * block / rate;
		}
		std::printf("%-24s rest %7.1f dBFS  %s", patch.name, 20 * std::log10(pk + 1e-30), p.voiceAsleep() ? "asleep" : "awake");
		if (asleepAt >= 0)
			std::printf(" (%.2f s after the note-off)", asleepAt);
		std::printf("\n");
	}
	return 0;
}
} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI init;
#if defined(__APPLE__)
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); // the performance cores, as a host's audio thread
#endif
	const juce::String mode = argc > 1 ? argv[1] : "time";
	if (mode == "idle") {
		juce::MemoryBlock rom;
		juce::File(SH101_ROM_PATH).loadFileAsData(rom);
		return idleLevels(rom);
	}
	const bool hasDir = mode == "golden" || mode == "check";
	const juce::File dir = hasDir && argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File();
	const int runs = argc > (hasDir ? 3 : 2) ? juce::String(argv[hasDir ? 3 : 2]).getIntValue() : 3;
	if (hasDir && dir == juce::File()) {
		std::printf("usage: sh101bench golden|check DIR [RUNS] | time [RUNS [NAME]]\n");
		return 2;
	}
	if (mode == "golden")
		dir.createDirectory();
	juce::MemoryBlock rom;
	juce::File(SH101_ROM_PATH).loadFileAsData(rom);

	double worstDb = -400, totalCpu = 0, totalIns = 0;
	int missing = 0;
	std::printf("%-22s %6s %8s %8s %8s %8s %7s %12s\n", "workload", "rate", "CPU %", "Mcyc/s", "Mins/s", "worst %", "asleep",
	            mode == "check" ? "max diff dB" : "");
	const juce::String only = !hasDir && argc > 3 ? argv[3] : "";
	for (const auto &w : workloads())
		for (double rate : {44100.0, 48000.0}) {
			if (only.isNotEmpty() && only != w.name)
				continue;
			juce::AudioBuffer<float> out;
			Timing best;
			best.seconds = best.cycles = best.instructions = best.worst = 1e30;
			for (int r = 0; r < std::max(1, runs); ++r) {
				Timing t;
				out = render(rom, w, rate, t);
				best.seconds = std::min(best.seconds, t.seconds);
				best.cycles = std::min(best.cycles, t.cycles);
				best.instructions = std::min(best.instructions, t.instructions);
				best.worst = std::min(best.worst, t.worst);
				best.asleepShare = t.asleepShare;
			}
			const double cpu = 100 * best.seconds / w.seconds, asleep = best.asleepShare;
			totalCpu += cpu;
			totalIns += best.instructions / w.seconds;
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
			std::printf("%-22s %6d %8.2f %8.0f %8.0f %8.1f %6.0f%% %12s\n", w.name, int(rate), cpu, 1e-6 * best.cycles / w.seconds,
			            1e-6 * best.instructions / w.seconds, 100 * best.worst, 100 * asleep, extra.toRawUTF8());
		}
	std::printf("total CPU %% (sum of workloads): %.2f; instructions %.0f M/s\n", totalCpu, 1e-6 * totalIns);
	if (mode == "check")
		std::printf("worst difference: %s\n", worstDb <= -400 ? "identical (bit-exact)" : (juce::String(worstDb, 1) + " dBFS").toRawUTF8());
	return missing > 0 ? 1 : 0;
}
