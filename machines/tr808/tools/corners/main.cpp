// tr808corners: the controls' extremes, checked for what the other checks can't see. Every other
// check compares against something (SPICE, recordings, golden renders) only where that something
// exists; this one renders the corners nobody recorded through the plugin's processor: each voice
// with every one of its knobs at both ends (LEVEL, TONE, DECAY, SNAPPY, TUNING, ACCENT), its
// switch both ways (the congas, claves, maracas) and its trimmers at their ends (the clap's TM3
// C.P. OFFSET, the cowbell's TM1 and TM2), hit unaccented and accented (velocity 1 and 127), slow,
// in a roll at 30 ms and in one at 4 ms (the TR-808 has no flams); the whole kit at once with every
// knob and trimmer at either end, the knobs swept and jumping while it plays, and the first board
// (SN 000001, its early noise generator and the snare's and clap's drawn values). At 44.1, 48 and
// 88.2 kHz (from 88.2 kHz the board runs at the host's rate, not twice it). Fails on:
//   - any circuit step that didn't converge (Circuit::Stats::failures, every partition of every
//     voice, the maracas, the accent, common trig, noise and metal circuits and the mixer): the
//     solver retries a step in quarters, sixteenths and 64ths and then with its comparators
//     softened, and a step that fails even then is accepted as it stands, silent in every other
//     check (on the SH-101 such samples turned a resonance into white noise;
//     machines/sh101/docs/improvements.md, 2026-11-16);
//   - broadband noise where nothing should make it: a case of the BD, a tom or conga, the rim shot
//     or claves, or the cowbell whose median spectral flatness (40 Hz-16 kHz, 2048-sample windows
//     above -70 dBFS) passes kNoiseFlatness. None of them takes the noise but the toms, whose pink
//     noise is a breath on the attack that a median over 2048-sample windows doesn't see (the old
//     SH-101 bug measured 0.39-0.50). The snare, clap, maracas, cymbal and hats are noise or the
//     metal source: not judged, their flatness is reported (--each);
//   - output that isn't finite, or peaks past kMaxPeak.
// It also reports the steps, the substepped ones and those that needed the comparators softened.
//
//   tr808corners [--rate HZ] [--only TEXT] [--list] [--jobs N] [--wav DIR] [--each]
//
// --wav writes each case's render to DIR/<rate>-<case>.wav, to listen to one; --each prints every
// case's numbers, passing or not. The ROM comes from TR808_ROM_PATH (the voices play from MIDI
// either way).
//
// Exit status 0 when every case passes. About half a minute (the cases on all but two cores).
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace {

using Stats = tr808::circuit::Circuit::Stats;

constexpr double kNoiseFlatness = 0.2;
constexpr float kMaxPeak = 4.0f; // well past anything the board reaches (1.68 at the loudest corner)

const juce::MemoryBlock &rom()
{
	static const juce::MemoryBlock r = [] {
		juce::MemoryBlock b;
		juce::File(TR808_ROM_PATH).loadFileAsData(b);
		return b;
	}();
	return r;
}

void set(TR808Processor &p, const char *id, float v) { p.params().getParameter(id)->setValueNotifyingHost(v); }

struct Hit {
	double t;
	int note, velocity;
};

struct Case {
	std::string name;
	std::vector<std::pair<std::string, float>> knobs;
	std::vector<Hit> hits;
	double seconds = 2.6;
	bool judgeNoise = false; // no noise past the attack: the flatness check
	int serial = 0;          // 0: the fitted unit's
	std::function<void(TR808Processor &, double)> automate; // per block, given the time
};

// One voice's pattern: accented then not, a roll at 30 ms and one at 4 ms (every other hit
// accented), then a last accented hit to ring out.
std::vector<Hit> voiceHits(int note, int alt = -1)
{
	std::vector<Hit> h;
	const int other = alt < 0 ? note : alt;
	h.push_back({0.05, note, 127});
	h.push_back({0.9, other, 1});
	for (int k = 0; k < 8; ++k)
		h.push_back({1.5 + 0.030 * k, k % 2 ? other : note, k % 2 ? 1 : 127});
	for (int k = 0; k < 8; ++k)
		h.push_back({1.9 + 0.004 * k, k % 2 ? note : other, k % 2 ? 127 : 1});
	h.push_back({2.1, note, 127});
	return h;
}

// Every voice, 16ths at 150 BPM for `seconds`, accents on every other step.
std::vector<Hit> kit(double seconds)
{
	static const int notes[] = {36, 38, 43, 47, 50, 37, 39, 56, 49, 46, 42};
	std::vector<Hit> h;
	int s = 0;
	for (double t = 0.05; t < seconds - 0.5; t += 0.1, ++s)
		for (int n : notes)
			if ((s + n) % 3 != 0 || n == 36)
				h.push_back({t, n, s % 2 ? 127 : 40});
	return h;
}

// A knob in a case's name: "decay" for the voice's own ("bd_decay" in a BD case), else the
// parameter's id as words ("oh decay" in an HH case, "tm cb1"; "accent" for ACCENT).
std::string label(const std::string &voice, const std::string &id)
{
	if (id == "ac_level" || id == "accent")
		return "accent";
	std::string v = voice;
	for (auto &ch : v)
		ch = char(std::tolower(static_cast<unsigned char>(ch)));
	if (id.rfind(v + "_", 0) == 0)
		return id.substr(v.size() + 1);
	std::string r = id;
	std::replace(r.begin(), r.end(), '_', ' ');
	return r;
}

std::vector<Case> cases()
{
	std::vector<Case> c;
	struct V {
		const char *name;
		int note, alt;
		std::vector<std::string> knobs; // ac_level is every voice's
		bool clean;                     // no noise past the attack: judged for noise
	};
	const std::vector<V> voices = {
		{"BD", 36, -1, {"bd_level", "bd_tone", "bd_decay"}, true},
		{"SD", 38, -1, {"sd_level", "sd_tone", "sd_snappy"}, false},
		{"LT", 43, -1, {"lt_level", "lt_tuning", "lc"}, true},
		{"MT", 47, -1, {"mt_level", "mt_tuning", "mc"}, true},
		{"HT", 50, -1, {"ht_level", "ht_tuning", "hc"}, true},
		{"RS", 37, -1, {"rs_level", "cl"}, true},
		{"CP", 39, -1, {"cp_level", "ma", "tm_cp"}, false},
		{"CB", 56, -1, {"cb_level", "tm_cb1", "tm_cb2"}, true},
		{"CY", 49, -1, {"cy_level", "cy_tone", "cy_decay"}, false},
		{"HH", 42, 46, {"ch_level", "oh_level", "oh_decay"}, false}, // closed and open, each choking the other
	};
	for (const auto &v : voices) {
		auto knobs = v.knobs;
		knobs.push_back("ac_level");
		const int n = int(knobs.size());
		for (int m = 0; m < 1 << n; ++m) {
			Case k;
			k.name = v.name;
			for (int i = 0; i < n; ++i) {
				const float x = float(m >> i & 1);
				const std::string &id = knobs[size_t(i)];
				k.knobs.push_back({id, x});
				if (id.size() == 2) // a switch
					k.name += " " + id + (x > 0 ? " on" : " off");
				else
					k.name += " " + label(v.name, id) + (x > 0 ? " 10" : " 0");
			}
			k.hits = voiceHits(v.note, v.alt);
			k.judgeNoise = v.clean;
			c.push_back(k);
		}
	}
	std::vector<std::string> all;
	for (const char *id : {"ac_level", "bd_level", "bd_tone", "bd_decay", "sd_level", "sd_tone", "sd_snappy", "lt_level", "lt_tuning",
	                       "mt_level", "mt_tuning", "ht_level", "ht_tuning", "rs_level", "cp_level", "cb_level", "cy_level", "cy_tone",
	                       "cy_decay", "oh_level", "oh_decay", "ch_level", "volume"})
		all.push_back(id);
	std::vector<std::string> trims;
	for (const char *id : TR808Processor::kTrimIds)
		if (std::string(id) != "tm_int" && std::string(id) != "tm_tempo") // the CPU board's clocks: not the voices'
			trims.push_back(id);
	auto every = [&](float x) {
		std::vector<std::pair<std::string, float>> r;
		for (const auto &id : all)
			r.push_back({id, x});
		for (const auto &id : trims)
			r.push_back({id, x});
		return r;
	};
	for (float x : {0.f, 1.f})
		for (bool sw : {false, true}) {
			Case k{std::string("kit, every knob and trimmer ") + (x > 0 ? "10" : "0 but the LEVELs and MASTER VOLUME") +
			       (sw ? ", congas, claves, maracas" : "")};
			k.knobs = every(x);
			if (x == 0)
				for (const auto &id : all)
					if (id.find("level") != std::string::npos || id == "volume")
						k.knobs.push_back({id, 1});
			for (const char *s : {"lc", "mc", "hc", "cl", "ma"})
				k.knobs.push_back({s, sw ? 1.f : 0.f});
			k.hits = kit(3);
			k.seconds = 3;
			c.push_back(k);
		}
	{
		Case k{"kit, every knob and trimmer 10, first board"};
		k.knobs = every(1);
		k.hits = kit(3);
		k.seconds = 3;
		k.serial = tr808::kFirstSerial;
		c.push_back(k);
	}
	{
		// every knob swept end to end and back at its own rate while the kit plays, the switches
		// flipping (each move rebuilds its voice's matrices mid-ring)
		Case k{"kit, every knob swept"};
		k.knobs = {{"volume", 1}};
		k.hits = kit(4);
		k.seconds = 4;
		k.automate = [all, trims](TR808Processor &p, double t) {
			for (size_t i = 0; i < all.size(); ++i)
				if (all[i] != "volume")
					set(p, all[i].c_str(), float(0.5 - 0.5 * std::cos(t * 3.14159265 * (1 + 0.37 * double(i)))));
			for (size_t i = 0; i < trims.size(); ++i)
				set(p, trims[i].c_str(), float(0.5 - 0.5 * std::cos(t * 3.14159265 * (2.1 + 0.61 * double(i)))));
			const char *sw[] = {"lc", "mc", "hc", "cl", "ma"};
			for (int i = 0; i < 5; ++i)
				set(p, sw[i], int(t * (3 + i)) % 2 ? 1.f : 0.f);
		};
		c.push_back(k);
	}
	{
		// every knob jumping between its ends every block
		Case k{"kit, every knob jumping 0 / 10"};
		k.knobs = {{"volume", 1}};
		k.hits = kit(3);
		k.seconds = 3;
		k.automate = [all](TR808Processor &p, double t) {
			const float x = int(t * 100) % 2 ? 1.f : 0.f;
			for (const auto &id : all)
				if (id != "volume")
					set(p, id.c_str(), x);
		};
		c.push_back(k);
	}
	return c;
}

// Median spectral flatness over 40 Hz-16 kHz, 2048-sample Hann windows above -70 dBFS.
double medianFlatness(const std::vector<float> &x, double rate)
{
	const int n = 2048;
	std::vector<double> flat;
	std::vector<std::complex<double>> a(static_cast<size_t>(n));
	for (size_t start = 0; start + size_t(n) <= x.size(); start += size_t(n)) {
		double e = 0;
		for (int i = 0; i < n; ++i)
			e += double(x[start + size_t(i)]) * x[start + size_t(i)];
		if (std::sqrt(e / n) < 3.2e-4)
			continue;
		for (int i = 0; i < n; ++i)
			a[size_t(i)] = x[start + size_t(i)] * (0.5 - 0.5 * std::cos(2 * 3.14159265358979 * i / n));
		// Radix-2 FFT.
		for (int i = 1, j = 0; i < n; ++i) {
			int bit = n >> 1;
			for (; j & bit; bit >>= 1)
				j ^= bit;
			j ^= bit;
			if (i < j)
				std::swap(a[size_t(i)], a[size_t(j)]);
		}
		for (int len = 2; len <= n; len <<= 1) {
			const std::complex<double> w = std::polar(1.0, -2 * 3.14159265358979 / len);
			for (int i = 0; i < n; i += len) {
				std::complex<double> wk = 1;
				for (int k = 0; k < len / 2; ++k, wk *= w) {
					const auto u = a[size_t(i + k)], v = a[size_t(i + k + len / 2)] * wk;
					a[size_t(i + k)] = u + v;
					a[size_t(i + k + len / 2)] = u - v;
				}
			}
		}
		const int lo = int(std::ceil(40.0 * n / rate)), hi = int(16000.0 * n / rate);
		double lg = 0, ar = 0;
		for (int k = lo; k <= hi; ++k) {
			const double p = std::norm(a[size_t(k)]) + 1e-30;
			lg += std::log(p);
			ar += p;
		}
		const int m = hi - lo + 1;
		flat.push_back(std::exp(lg / m) / (ar / m));
	}
	if (flat.empty())
		return 0;
	std::nth_element(flat.begin(), flat.begin() + long(flat.size() / 2), flat.end());
	return flat[flat.size() / 2];
}

void add(Stats &a, const Stats &b)
{
	a.steps += b.steps;
	a.iterations += b.iterations;
	a.substepped += b.substepped;
	a.failures += b.failures;
	a.softened += b.softened;
	a.linearSteps += b.linearSteps;
}

struct Result {
	Stats solver;
	std::string failedIn; // the partitions with failures, and how many
	double flatness = 0;
	float peak = 0;
	bool finite = true;
};

Result run(const Case &k, double rate, const std::string &wavDir)
{
	TR808Processor p(rom().getSize() ? &rom() : nullptr);
	if (k.serial > 0)
		p.setSerial(k.serial);
	for (const auto &[id, v] : k.knobs)
		set(p, id.c_str(), v);
	const int block = 512;
	p.prepareToPlay(rate, block);
	const int total = int(k.seconds * rate);
	juce::MidiBuffer all;
	for (const auto &h : k.hits)
		all.addEvent(juce::MidiMessage::noteOn(10, h.note, juce::uint8(h.velocity)), int(h.t * rate));
	std::vector<float> out(static_cast<size_t>(total));
	juce::AudioBuffer<float> buf(2, block);
	Result res;
	for (int pos = 0; pos < total; pos += block) {
		const int n = std::min(block, total - pos);
		buf.setSize(2, n, false, false, true);
		buf.clear();
		juce::MidiBuffer midi;
		for (const auto m : all)
			if (m.samplePosition >= pos && m.samplePosition < pos + n)
				midi.addEvent(m.getMessage(), m.samplePosition - pos);
		if (k.automate)
			k.automate(p, pos / rate);
		p.processBlock(buf, midi);
		for (int ch = 0; ch < 2; ++ch) {
			const float *s = buf.getReadPointer(ch);
			for (int i = 0; i < n; ++i) {
				res.finite = res.finite && std::isfinite(s[i]);
				res.peak = std::max(res.peak, std::abs(s[i]));
			}
		}
		std::copy(buf.getReadPointer(0), buf.getReadPointer(0) + n, out.begin() + pos);
	}
	for (const auto &s : p.solverStats()) {
		add(res.solver, s.stats);
		if (s.stats.failures > 0)
			res.failedIn += " " + s.name + ":" + std::to_string(s.stats.failures);
	}
	if (!wavDir.empty()) {
		juce::String name = juce::String(int(rate)) + "-" + juce::String(k.name).replaceCharacters(" /,'", "____") + ".wav";
		const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile(wavDir).getChildFile(name);
		f.getParentDirectory().createDirectory();
		f.deleteFile();
		juce::AudioBuffer<float> b(1, total);
		b.copyFrom(0, 0, out.data(), total);
		juce::WavAudioFormat wav;
		std::unique_ptr<juce::OutputStream> st(f.createOutputStream());
		const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(32).withSampleFormat(
		    juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
		if (auto w = wav.createWriterFor(st, opts))
			w->writeFromAudioSampleBuffer(b, 0, total);
	}
	res.flatness = medianFlatness(out, rate);
	return res;
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI juce;
	// each case on one thread: the cases run side by side, not the voices (the result is the same)
#ifdef _WIN32
	_putenv_s("TR808_WORKERS", "0");
#else
	setenv("TR808_WORKERS", "0", 1);
#endif
	std::vector<double> rates = {44100, 48000, 88200};
	std::string only, wavDir;
	bool list = false, each = false;
	int jobs = int(std::max(1u, std::thread::hardware_concurrency() - 2));
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--rate" && i + 1 < argc)
			rates = {std::atof(argv[++i])};
		else if (a == "--only" && i + 1 < argc)
			only = argv[++i];
		else if (a == "--jobs" && i + 1 < argc)
			jobs = std::max(1, std::atoi(argv[++i]));
		else if (a == "--wav" && i + 1 < argc)
			wavDir = argv[++i];
		else if (a == "--list")
			list = true;
		else if (a == "--each")
			each = true;
		else {
			std::fprintf(stderr, "usage: tr808corners [--rate HZ] [--only TEXT] [--list] [--jobs N] [--wav DIR] [--each]\n");
			return 2;
		}
	}
	std::vector<Case> all;
	for (auto &k : cases())
		if (only.empty() || k.name.find(only) != std::string::npos)
			all.push_back(k);
	if (list) {
		for (auto &k : all)
			std::printf("%s\n", k.name.c_str());
		return 0;
	}
	if (rom().getSize() == 0)
		std::fprintf(stderr, "note: no ROM at %s\n", TR808_ROM_PATH);

	int bad = 0;
	for (double rate : rates) {
		std::vector<Result> res(all.size());
		std::atomic<size_t> next{0};
		std::vector<std::thread> pool;
		for (int j = 0; j < jobs; ++j)
			pool.emplace_back([&] {
				for (size_t i; (i = next++) < all.size();)
					res[i] = run(all[i], rate, wavDir);
			});
		for (auto &t : pool)
			t.join();
		Stats total;
		size_t worstClean = all.size(), worstAny = 0;
		for (size_t i = 0; i < all.size(); ++i) {
			const Result &r = res[i];
			add(total, r.solver);
			if (r.flatness > res[worstAny].flatness)
				worstAny = i;
			if (all[i].judgeNoise && (worstClean == all.size() || r.flatness > res[worstClean].flatness))
				worstClean = i;
			std::string why;
			if (r.solver.failures > 0)
				why += " unconverged " + std::to_string(r.solver.failures) + " (" + r.failedIn.substr(1) + ")";
			if (all[i].judgeNoise && r.flatness > kNoiseFlatness)
				why += " noise (flatness " + juce::String(r.flatness, 3).toStdString() + ")";
			if (!r.finite || r.peak > kMaxPeak)
				why += " output " + std::string(r.finite ? "peak " + juce::String(r.peak, 2).toStdString() : "not finite");
			if (!why.empty()) {
				++bad;
				std::printf("  FAIL %5.0f  %-60s%s\n", rate, all[i].name.c_str(), why.c_str());
			} else if (each) {
				std::printf("       %5.0f  %-60s %9ld steps %6ld substepped %4ld softened  flatness %.3f%s  peak %.3f\n", rate,
				            all[i].name.c_str(), r.solver.steps, r.solver.substepped, r.solver.softened, r.flatness,
				            all[i].judgeNoise ? " (judged)" : "", r.peak);
			}
		}
		std::printf("%5.0f Hz: %zu cases, %ld steps, %ld substepped (%.3f %%), %ld softened, %ld unconverged; ", rate,
		            all.size(), total.steps, total.substepped,
		            total.steps ? 100.0 * double(total.substepped) / double(total.steps) : 0.0, total.softened, total.failures);
		if (worstClean < all.size())
			std::printf("noisiest judged: %s (median flatness %.3f, limit %.2f); ", all[worstClean].name.c_str(),
			            res[worstClean].flatness, kNoiseFlatness);
		std::printf("noisiest: %s (%.3f)\n", all[worstAny].name.c_str(), res[worstAny].flatness);
	}
	std::printf("%s: %d failing case(s)\n", bad ? "FAIL" : "PASS", bad);
	return bad ? 1 : 0;
}
