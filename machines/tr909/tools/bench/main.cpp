// CPU benchmark and golden-reference check for the TR-909 (TR909Processor: the firmware on its CPU
// board, the voicing board, sleep). Renders a fixed set of workloads at 44.1 and 48 kHz, times each
// (best of N runs), and writes them as golden WAVs or compares them with golden WAVs written
// earlier, so speed work can show it leaves the sound alone. CPU % is the rendering thread's CPU
// time against the audio's length (on Apple Silicon, asked for on the performance cores); Mcyc/s
// the CPU cycles it took a second of audio (Apple Silicon), which a loaded machine doesn't change;
// both count the voices' worker threads' work (the cycles, the process's) or not (the CPU %, the
// rendering thread's). Wall % is the time the blocks took against the audio's length, and worst %
// the slowest 512-sample block's against its length: what a host's meter shows.
//   tr909bench golden DIR [RUNS [NAME...]]  render, time, and write DIR/<workload>-<rate>.wav
//   tr909bench check DIR [RUNS [NAME...]]   render, time, and compare with DIR: the largest
//                                           difference against the workload's own peak, and the
//                                           difference's RMS against the workload's own RMS, in
//                                           dB; and, as a free-running VCO's phase moves the
//                                           waveform with any change to the solver's rounding,
//                                           the largest difference of the envelope (50 ms
//                                           frames) and of the spectrum (1/6-octave bands)
//   tr909bench time [RUNS [NAME...]]        render and time only
// (NAME...: only the workloads named.)
// MIDI goes into the firmware's MIDI IN on channel 10, after its power-on delay (each render first
// runs the processor for a second, untimed and not kept). The ROMs come from TR909_ROM_DIR.
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <ctime>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <libproc.h>
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
struct PlayHead : juce::AudioPlayHead {
	juce::Optional<PositionInfo> getPosition() const override
	{
		PositionInfo p;
		p.setIsPlaying(false);
		p.setBpm(120);
		return p;
	}
};

void set(TR909Processor &p, const char *id, float v)
{
	p.params().getParameter(id)->setValueNotifyingHost(v);
}

// MIDI notes (TR909Processor::noteFor, the v4.0 key assignment); OH is the open hat's 46.
enum Note { BD = 36, SD = 38, LT = 43, MT = 47, HT = 50, RS = 37, HC = 39, CH = 42, OH = 46, CR = 49, RD = 51 };

struct Hit {
	double t;
	int note, velocity;
};

// The busy kit (the lead's tr909voicerun kit): an 8th-note grid at 0.125 s, BD and CH on every
// step, SD and HC on 2 and 4, crash, low tom and rim shot on 1. Velocity 80 is the analog voices'
// accent code (21), 50 the hats' code 25, 64 the crash's code 32.
std::vector<Hit> busyKit(double seconds)
{
	std::vector<Hit> h;
	for (int i = 0; 0.05 + i * 0.125 < seconds - 0.1; ++i) {
		const double t = 0.05 + i * 0.125;
		const int s = i % 8;
		h.push_back({t, BD, 80});
		h.push_back({t, CH, 50});
		if (s == 2 || s == 6) {
			h.push_back({t, SD, 80});
			h.push_back({t, HC, 80});
		}
		if (s == 0) {
			h.push_back({t, CR, 64});
			h.push_back({t, LT, 80});
			h.push_back({t, RS, 80});
		}
	}
	return h;
}

// Every voice: a 16th-note grid at 120 BPM, kick on the beats, snare on 2 and 4, closed hats on
// the 8ths, the open hat on the last 16th, the toms, rim shot, clap, crash and ride here and there,
// accents on the downbeats.
std::vector<Hit> everything(double seconds)
{
	std::vector<Hit> h;
	const double step = 60.0 / 120 / 4;
	for (int i = 0; 0.05 + i * step < seconds - step; ++i) {
		const double t = 0.05 + i * step;
		const int s = i % 16, bar = i / 16;
		const int v = s == 0 ? 127 : 70;
		std::vector<int> n;
		if (s % 2 == 0 && s != 14)
			n.push_back(CH);
		if (s % 4 == 0)
			n.push_back(BD);
		if (s == 4 || s == 12)
			n.push_back(SD);
		if (s == 14)
			n.push_back(OH);
		if (s == 12)
			n.push_back(HC);
		if (s == 7)
			n.push_back(LT);
		if (s == 10)
			n.push_back(MT);
		if (s == 13)
			n.push_back(HT);
		if (s == 3)
			n.push_back(RS);
		if (s == 0 && bar % 2 == 0)
			n.push_back(CR);
		if (s == 8 || (s == 0 && bar % 2 == 1))
			n.push_back(RD);
		for (int k : n)
			h.push_back({t, k, v});
	}
	return h;
}

// One voice alone: hits every 0.5 s at normal, accent and full velocity.
std::vector<Hit> alone(int note, double seconds)
{
	static const int vel[] = {50, 80, 127, 50};
	std::vector<Hit> h;
	for (int i = 0; 0.05 + i * 0.5 < seconds - 0.1; ++i)
		h.push_back({0.05 + i * 0.5, note, vel[i % 4]});
	return h;
}

struct Workload {
	std::string name;
	double seconds;
	std::map<std::string, float> knobs; // set before prepare
	std::vector<Hit> hits;
	std::function<void(TR909Processor &, double)> automate; // per block, given the time
};

std::vector<Workload> workloads()
{
	std::vector<Workload> w;
	w.push_back({"kit", 4, {}, busyKit(4), {}});
	w.push_back({"everything", 4, {}, everything(4), {}});
	// Each voice alone at its knobs' defaults, all down and all up.
	struct V {
		const char *name;
		int note;
		std::vector<const char *> knobs;
	};
	const std::vector<V> voices = {
		{"bd", BD, {"bd_tune", "bd_attack", "bd_decay"}}, {"sd", SD, {"sd_tune", "sd_tone", "sd_snappy"}},
		{"lt", LT, {"lt_tune", "lt_decay"}},             {"mt", MT, {"mt_tune", "mt_decay"}},
		{"ht", HT, {"ht_tune", "ht_decay"}},             {"rs", RS, {}},
		{"hc", HC, {}},                                   {"ch", CH, {"ch_decay"}},
		{"oh", OH, {"oh_decay"}},                         {"cr", CR, {"cr_tune"}},
		{"rd", RD, {"rd_tune"}},
	};
	for (const auto &v : voices) {
		w.push_back({v.name, 2, {}, alone(v.note, 2), {}});
		if (v.knobs.empty())
			continue;
		for (float x : {0.0f, 1.0f}) {
			std::map<std::string, float> k;
			for (const char *id : v.knobs)
				k[id] = x;
			w.push_back({std::string(v.name) + (x > 0 ? "-up" : "-down"), 2, k, alone(v.note, 2), {}});
		}
	}
	// Every decay up, every voice once (the toms and cymbals ring on), then the tails into sleep.
	{
		std::map<std::string, float> k;
		for (const char *id : {"bd_decay", "lt_decay", "mt_decay", "ht_decay", "ch_decay", "oh_decay"})
			k[id] = 1;
		std::vector<Hit> h;
		int i = 0;
		for (int n : {BD, SD, LT, MT, HT, RS, HC, OH, CR, RD})
			h.push_back({0.05 + 0.02 * i++, n, 100});
		w.push_back({"long-decays", 6, k, h, {}});
	}
	// The busy kit with knobs turning (every block).
	w.push_back({"automation", 4, {}, busyKit(4), [](TR909Processor &p, double t) {
		             set(p, "bd_tune", float(0.5 + 0.5 * std::sin(t * 1.3)));
		             set(p, "bd_decay", float(0.5 + 0.5 * std::sin(t * 0.7 + 1)));
		             set(p, "sd_snappy", float(0.5 + 0.5 * std::sin(t * 0.9 + 2)));
		             set(p, "sd_tone", float(0.5 + 0.5 * std::sin(t * 1.7 + 2.5)));
		             set(p, "lt_tune", float(0.5 + 0.5 * std::sin(t * 0.5 + 3)));
		             set(p, "cr_tune", float(0.5 + 0.5 * std::sin(t * 1.1 + 4)));
		             set(p, "ch_decay", float(0.5 + 0.5 * std::sin(t * 2.3)));
		             set(p, "volume", float(0.6 + 0.4 * std::sin(t * 0.3)));
	             }});
	// A kick, then silence: mostly asleep (its last second must be exact zeros).
	w.push_back({"idle", 6, {}, {{0.1, BD, 100}}, {}});
	return w;
}

struct Roms {
	static juce::MemoryBlock load(const char *name)
	{
		juce::MemoryBlock b;
		juce::File(juce::String(TR909_ROM_DIR) + "/" + name).loadFileAsData(b);
		return b;
	}
	juce::MemoryBlock fw = load("tr909_v4.ic609"), hh = load("hn61256p__c43.ic69"), cr = load("hn61256p__c42.ic62"),
	                  rd = load("hn61256p__c44.ic54");
};

// This thread's CPU time (the processor runs on it alone): steadier than the wall clock on a busy
// machine.
double threadSeconds()
{
#if defined(_WIN32)
	FILETIME created, exited, kernel, user;
	if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
		return 0;
	auto ticks = [](const FILETIME &f) { return double((uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime); };
	return 1e-7 * (ticks(kernel) + ticks(user)); // 100 ns units
#else
	timespec t{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return double(t.tv_sec) + 1e-9 * double(t.tv_nsec);
#endif
}

// The process's CPU cycles so far (Apple Silicon, Windows; 0 elsewhere; instructions on Apple Silicon only): unlike the time, the same on a
// loaded machine, whichever cores the thread gets.
double cycles(double *instructions = nullptr)
{
#if defined(__APPLE__)
	rusage_info_v4 r{};
	if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t *>(&r)) == 0) {
		if (instructions)
			*instructions = double(r.ri_instructions);
		return double(r.ri_cycles);
	}
#elif defined(_WIN32)
	ULONG64 c = 0;
	if (QueryProcessCycleTime(GetCurrentProcess(), &c))
		return double(c);
#endif
	return 0;
}

juce::AudioBuffer<float> render(const Roms &roms, const Workload &w, double rate, double &seconds, double &cyc, bool &asleep,
                                double &wall, double &worst, double &ins)
{
	TR909Processor p(roms.fw.getSize() ? &roms.fw : nullptr, &roms.hh, &roms.cr, &roms.rd);
	PlayHead ph;
	p.setPlayHead(&ph);
	for (const auto &[id, v] : w.knobs)
		set(p, id.c_str(), v);
	const int block = 512;
	p.prepareToPlay(rate, block);
	juce::AudioBuffer<float> buf(2, block);
	juce::MidiBuffer none;
	// Past the power-on: the firmware's delay, the master mute, C143's charging (a minute, until the
	// board sleeps); the workloads measure a 909 that has been on a while.
	for (int pos = 0; pos < int(rate) || (pos < int(120 * rate) && !p.voiceAsleep()); pos += block) {
		buf.clear();
		p.processBlock(buf, none);
	}
	const int total = int(w.seconds * rate);
	juce::MidiBuffer all;
	for (const auto &h : w.hits)
		all.addEvent(juce::MidiMessage::noteOn(10, h.note, juce::uint8(h.velocity)), int(h.t * rate));
	juce::AudioBuffer<float> out(2, total);
	double i0 = 0;
	const double t0 = threadSeconds(), c0 = cycles(&i0);
	wall = worst = 0;
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
		const auto b0 = std::chrono::steady_clock::now();
		p.processBlock(buf, midi);
		const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - b0).count();
		wall += took;
		worst = std::max(worst, took * rate / n);
		for (int ch = 0; ch < 2; ++ch)
			out.copyFrom(ch, pos, buf, ch, 0, n);
	}
	seconds = threadSeconds() - t0;
	double i1 = 0;
	cyc = cycles(&i1) - c0;
	ins = i1 - i0;
	asleep = p.voiceAsleep();
	return out;
}

void writeWav(const juce::File &f, const juce::AudioBuffer<float> &b, double rate)
{
	f.deleteFile();
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> s(f.createOutputStream());
	const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(b.getNumChannels()).withBitsPerSample(32).withSampleFormat(
	    juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
	auto wr = wav.createWriterFor(s, opts);
	wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

bool readWav(const juce::File &f, juce::AudioBuffer<float> &b)
{
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::AudioFormatReader> r(wav.createReaderFor(f.createInputStream().release(), true));
	if (r == nullptr)
		return false;
	b.setSize(int(r->numChannels), int(r->lengthInSamples));
	r->read(&b, 0, int(r->lengthInSamples), 0, true, r->numChannels > 1);
	return true;
}

// The level in 50 ms frames (both channels), in dB. (A tom's three VCOs beat: its level over 10 ms
// moves by a decibel with their phases.)
std::vector<double> envelope(const juce::AudioBuffer<float> &b, double rate)
{
	const int n = int(0.05 * rate);
	std::vector<double> e;
	for (int at = 0; at + n <= b.getNumSamples(); at += n) {
		double s = 0;
		for (int ch = 0; ch < b.getNumChannels(); ++ch)
			for (int i = at; i < at + n; ++i)
				s += double(b.getSample(ch, i)) * b.getSample(ch, i);
		e.push_back(10 * std::log10(s / n + 1e-30));
	}
	return e;
}

// In-place radix-2 FFT of n (a power of two) complex values.
void fft(std::vector<double> &re, std::vector<double> &im)
{
	const size_t n = re.size();
	for (size_t i = 1, j = 0; i < n; ++i) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			std::swap(re[i], re[j]);
			std::swap(im[i], im[j]);
		}
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		const double a = -2 * juce::MathConstants<double>::pi / double(len);
		for (size_t i = 0; i < n; i += len)
			for (size_t k = 0; k < len / 2; ++k) {
				const double c = std::cos(a * double(k)), sn = std::sin(a * double(k));
				const double xr = re[i + k + len / 2] * c - im[i + k + len / 2] * sn;
				const double xi = re[i + k + len / 2] * sn + im[i + k + len / 2] * c;
				re[i + k + len / 2] = re[i + k] - xr;
				im[i + k + len / 2] = im[i + k] - xi;
				re[i + k] += xr;
				im[i + k] += xi;
			}
	}
}

// The long-term spectrum (4096-point Hann frames, half overlapped, both channels) in 1/6-octave
// bands from 25 Hz up to 20 kHz, in dB.
std::vector<double> bands(const juce::AudioBuffer<float> &b, double rate)
{
	constexpr int n = 4096;
	std::vector<double> power(n / 2 + 1, 0.0), re(n), im(n);
	for (int ch = 0; ch < b.getNumChannels(); ++ch)
		for (int at = 0; at + n <= b.getNumSamples(); at += n / 2) {
			for (int i = 0; i < n; ++i) {
				re[size_t(i)] = b.getSample(ch, at + i) * (0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / n));
				im[size_t(i)] = 0;
			}
			fft(re, im);
			for (int k = 0; k <= n / 2; ++k)
				power[size_t(k)] += re[size_t(k)] * re[size_t(k)] + im[size_t(k)] * im[size_t(k)];
		}
	std::vector<double> out;
	for (double lo = 25; lo < 20000; lo *= std::pow(2.0, 1.0 / 6)) {
		const double hi = lo * std::pow(2.0, 1.0 / 6);
		double s = 0;
		for (int k = 0; k <= n / 2; ++k) {
			const double f = k * rate / n;
			if (f >= lo && f < hi)
				s += power[size_t(k)];
		}
		out.push_back(10 * std::log10(s + 1e-30));
	}
	return out;
}

// The largest difference (dB) between two level curves where the reference is within `range` dB
// of its top.
double worstDb(const std::vector<double> &ref, const std::vector<double> &out, double range)
{
	double top = -1e30, worst = 0;
	for (double r : ref)
		top = std::max(top, r);
	for (size_t k = 0; k < ref.size() && k < out.size(); ++k)
		if (ref[k] > top - range)
			worst = std::max(worst, std::abs(out[k] - ref[k]));
	return worst;
}
} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI init;
#if defined(__APPLE__)
	// on the performance cores where it can be (a loaded machine puts a default thread on the
	// efficiency cores, at a third of the speed)
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	const juce::String mode = argc > 1 ? argv[1] : "time";
	const bool hasDir = mode == "golden" || mode == "check";
	const juce::File dir = hasDir && argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File();
	const int runs = argc > (hasDir ? 3 : 2) ? juce::String(argv[hasDir ? 3 : 2]).getIntValue() : 2;
	if ((hasDir && dir == juce::File()) || (!hasDir && mode != "time")) {
		std::printf("usage: tr909bench golden|check DIR [RUNS [NAME...]] | time [RUNS [NAME...]]\n");
		return 2;
	}
	if (mode == "golden")
		dir.createDirectory();
	std::vector<std::string> only;
	for (int a = hasDir ? 4 : 3; a < argc; ++a)
		only.push_back(argv[a]);
	const Roms roms;
	if (roms.fw.getSize() == 0)
		std::printf("no firmware ROM in %s: voice mode\n", TR909_ROM_DIR);

	double worstPeak = -400, worstRms = -400, worstEnv = 0, worstSpec = 0, totalCpu = 0, totalCycles = 0;
	int missing = 0, failed = 0;
	if (mode == "check")
		std::printf("%-14s %6s %8s %8s %7s %8s %9s %9s %8s %8s\n", "workload", "rate", "CPU %", "Mcyc/s", "wall %", "worst %", "peak dB", "rms dB", "env dB", "spec dB");
	else
		std::printf("%-14s %6s %8s %8s %7s %8s %8s\n", "workload", "rate", "CPU %", "Mcyc/s", "wall %", "worst %", "Mins/s");
	for (const auto &w : workloads())
		for (double rate : {44100.0, 48000.0}) {
			if (!only.empty() && std::find(only.begin(), only.end(), w.name) == only.end())
				continue;
			juce::AudioBuffer<float> out;
			double best = 1e9, bestCycles = 1e30, bestWall = 1e9, bestWorst = 1e9, bestIns = 1e30;
			bool asleep = false;
			for (int r = 0; r < std::max(1, runs); ++r) {
				double s = 0, c = 0, wl = 0, wr = 0, in = 0;
				out = render(roms, w, rate, s, c, asleep, wl, wr, in);
				bestIns = std::min(bestIns, in);
				best = std::min(best, s);
				bestCycles = std::min(bestCycles, c);
				bestWall = std::min(bestWall, wl);
				bestWorst = std::min(bestWorst, wr);
			}
			const double wallPct = 100 * bestWall / w.seconds, worstPct = 100 * bestWorst;
			const double cpu = 100 * best / w.seconds, mcps = 1e-6 * bestCycles / w.seconds;
			totalCpu += cpu;
			totalCycles += mcps;
			const auto file = dir.getChildFile(juce::String(w.name) + "-" + juce::String(int(rate)) + ".wav");
			juce::String peakCol, rmsCol, envCol, specCol, note;
			if (w.name == "idle") { // asleep, and its last second exact zeros
				bool zeros = true;
				for (int ch = 0; ch < 2; ++ch)
					for (int i = out.getNumSamples() - int(rate); i < out.getNumSamples(); ++i)
						zeros = zeros && out.getSample(ch, i) == 0.0f;
				note = juce::String(asleep ? "asleep" : "AWAKE") + (zeros ? ", exact zeros" : ", NOT ZEROS");
				failed += asleep && zeros ? 0 : 1;
			}
			if (mode == "golden") {
				writeWav(file, out, rate);
			} else if (mode == "check") {
				juce::AudioBuffer<float> ref;
				if (!readWav(file, ref) || ref.getNumSamples() != out.getNumSamples() || ref.getNumChannels() != out.getNumChannels()) {
					peakCol = "no golden";
					++missing;
				} else {
					double d = 0, e = 0, peak = 0, sig = 0;
					for (int ch = 0; ch < 2; ++ch)
						for (int i = 0; i < out.getNumSamples(); ++i) {
							const double r = ref.getSample(ch, i), x = out.getSample(ch, i) - r;
							d = std::max(d, std::abs(x));
							e += x * x;
							peak = std::max(peak, std::abs(r));
							sig += r * r;
						}
					if (d == 0) {
						peakCol = "identical";
					} else {
						const double p = 20 * std::log10(d / std::max(peak, 1e-30)), q = 10 * std::log10(e / std::max(sig, 1e-30));
						worstPeak = std::max(worstPeak, p);
						worstRms = std::max(worstRms, q);
						peakCol = juce::String(p, 1);
						rmsCol = juce::String(q, 1);
						// what doesn't depend on a free-running oscillator's phase: the level in 50 ms
						// frames within 40 dB of the top, the 1/6-octave bands within 50 dB
						const double env = worstDb(envelope(ref, rate), envelope(out, rate), 40);
						const double spec = worstDb(bands(ref, rate), bands(out, rate), 50);
						worstEnv = std::max(worstEnv, env);
						worstSpec = std::max(worstSpec, spec);
						envCol = juce::String(env, 2);
						specCol = juce::String(spec, 2);
					}
				}
			}
			if (mode == "check")
				std::printf("%-14s %6d %8.2f %8.0f %7.1f %8.1f %9s %9s %8s %8s  %s\n", w.name.c_str(), int(rate), cpu, mcps, wallPct, worstPct, peakCol.toRawUTF8(),
				            rmsCol.toRawUTF8(), envCol.toRawUTF8(), specCol.toRawUTF8(), note.toRawUTF8());
			else
				std::printf("%-14s %6d %8.2f %8.0f %7.1f %8.1f %8.0f  %s\n", w.name.c_str(), int(rate), cpu, mcps, wallPct, worstPct,
				            1e-6 * bestIns / w.seconds, note.toRawUTF8());
			std::fflush(stdout);
		}
	std::printf("total CPU %% (sum of workloads): %.2f; Mcycles a second: %.0f\n", totalCpu, totalCycles);
	if (mode == "check") {
		if (worstPeak <= -400)
			std::printf("worst difference: identical (bit-exact)\n");
		else
			std::printf("worst difference: %.1f dB re peak, %.1f dB re RMS; envelope %.2f dB, spectrum %.2f dB\n", worstPeak,
			            worstRms, worstEnv, worstSpec);
	}
	return missing > 0 || failed > 0 ? 1 : 0;
}
