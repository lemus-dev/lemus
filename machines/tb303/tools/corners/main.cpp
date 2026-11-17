// tb303corners: the controls' extremes, checked for what the other checks can't see. Every other
// check compares against something (SPICE, real recordings, golden renders) only where that
// something exists; this one renders the corners nobody recorded (CUT OFF, RESONANCE, ENV MOD,
// ACCENT and DECAY at their ends, both waveforms, the lowest and highest MIDI notes, accents and
// slides, TM3 and FULL CUT OFF at their ends, knobs swept and jumped every block) through the
// plugin's processor at 44.1, 48 and 96 kHz (4x, 4x and 2x oversampling), and fails on:
//   - any iterative solve that didn't converge (tb303::SolverStats::failures, every solver of the
//     voice): an unconverged sample is silent in every other check, and on the SH-101 one such
//     solve turned a self-oscillation into white noise (docs/improvements.md, 2026-11-16);
//   - broadband noise where nothing should make it: the 303 has no noise source, so any case whose
//     median spectral flatness (40 Hz-16 kHz, 2048-sample windows above -70 dBFS) passes
//     kNoiseFlatness fails. Tones, chirps and thumps sit far below it (white noise is about 0.56);
//   - output that isn't finite or peaks past kMaxPeak, or a voice that restarted its circuit after
//     a non-finite sample (it silences those, so the output alone wouldn't show it).
// It also reports how many solves needed a retry in shorter steps, and the most iterations any
// one solve took, by solver.
//
//   tb303corners [--rate HZ] [--only TEXT] [--list] [--jobs N] [--wav DIR]
//
// --rate takes one rate instead (any the plugin takes); --wav writes each case's render to
// DIR/<rate>-<case>.wav, to listen to one.
//
// Exit status 0 when every case passes. About 20 s on an M5 Max with half its cores (the default
// jobs), under a minute while it is busy.
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kNoiseFlatness = 0.2;
constexpr float kMaxPeak = 4.0f; // past anything the voice can reach (the output stage clips at +2.54)

const juce::MemoryBlock &rom()
{
	static const juce::MemoryBlock r = [] {
		juce::MemoryBlock b;
		juce::File(TB303_ROM_PATH).loadFileAsData(b);
		return b;
	}();
	return r;
}

void set(TB303Processor &p, const char *id, float v)
{
	auto *param = p.params().getParameter(id);
	if (auto *c = dynamic_cast<juce::AudioParameterChoice *>(param))
		param->setValueNotifyingHost(c->convertTo0to1(v));
	else
		param->setValueNotifyingHost(v);
}

struct Case {
	std::string name;
	std::vector<std::pair<const char *, float>> params;
	double seconds = 1.2;
	// Per block, given the time and the block's index: automation.
	std::function<void(TB303Processor &, double, int)> automate;
	bool extremeNotes = false; // MIDI notes 0 and 127 too, and slides between them
};

// Notes at 0.1 s: both ends of the 303's three octaves and past them, every other one accented
// (velocity 127, else 90), every third one sliding into the next (overlapping), a rest now and then.
juce::MidiBuffer notes(double rate, double seconds, bool extreme)
{
	juce::MidiBuffer m;
	const int normal[] = {24, 36, 60, 72, 48, 30, 66, 84};
	const int wide[] = {0, 127, 12, 108, 0, 96, 127, 24};
	const double step = 0.1;
	int i = 0;
	for (double t = 0.02; t < seconds - 0.2; t += step, ++i) {
		const int k = extreme ? wide[i % 8] : normal[i % 8];
		if (i % 7 == 6)
			continue;
		const bool slide = i % 3 == 2;
		m.addEvent(juce::MidiMessage::noteOn(1, k, juce::uint8(i % 2 == 0 ? 127 : 90)), int(t * rate));
		m.addEvent(juce::MidiMessage::noteOff(1, k), int((t + (slide ? 1.3 : 0.55) * step) * rate));
	}
	return m;
}

std::vector<Case> cases()
{
	std::vector<Case> c;
	const float cutoffs[] = {0, 0.5f, 0.8f, 1};
	const float ress[] = {0, 0.6f, 1};
	for (float cut : cutoffs)
		for (float res : ress)
			for (float env : {0.0f, 1.0f})
				for (float acc : {0.0f, 1.0f})
					for (float dec : {0.0f, 1.0f})
						for (int sq = 0; sq < 2; ++sq) {
							Case k;
							char n[96];
							std::snprintf(n, sizeof n, "cut %.1f res %.1f env %.0f acc %.0f dec %.0f %s", cut, res, env, acc,
							              dec, sq ? "square" : "saw");
							k.name = n;
							k.params = {{"cutoff", cut}, {"resonance", res}, {"envmod", env}, {"accent", acc},
							            {"decay", dec},  {"waveform", float(sq)}, {"volume", 1}};
							c.push_back(k);
						}
	// Everything up, both waveforms: TM3 and FULL CUT OFF at their ends (the ladder's current
	// highest with TM3 at 0 and FULL CUT OFF at 3 kHz), and MIDI notes 0 and 127.
	auto hot = [](Case k, int sq) {
		k.params.insert(k.params.begin(), {{"cutoff", 1}, {"resonance", 1}, {"envmod", 1}, {"accent", 1},
		                                   {"decay", 0.5f}, {"waveform", float(sq)}, {"volume", 1}});
		k.name += sq ? ", square" : ", saw";
		return k;
	};
	for (int sq = 0; sq < 2; ++sq) {
		for (float tm3 : {0.0f, 1.0f})
			for (float top : {0.0f, 1.0f}) {
				Case k;
				k.name = std::string("everything up, TM3 ") + (tm3 > 0 ? "1" : "0") + ", FULL CUT OFF " + (top > 0 ? "3.0" : "1.2") + " kHz";
				k.params = {{"tm3", tm3}, {"fullcutoff", top}};
				c.push_back(hot(k, sq));
			}
		{
			Case k{"everything up, notes 0-127 and slides between them"};
			k.extremeNotes = true;
			c.push_back(hot(k, sq));
		}
		{
			Case k{"everything up, TM3 0, FULL CUT OFF 3.0 kHz, notes 0-127"};
			k.params = {{"tm3", 0}, {"fullcutoff", 1}};
			k.extremeNotes = true;
			c.push_back(hot(k, sq));
		}
	}
	// Sweeps and jumps, with the other knobs up.
	auto moving = [&](const char *name, std::function<void(TB303Processor &, double, int)> f) {
		for (int sq = 0; sq < 2; ++sq) {
			Case k{name};
			k.seconds = 2.0;
			k.automate = f;
			c.push_back(hot(k, sq));
		}
	};
	const double pi = 3.14159265358979;
	moving("CUT OFF swept 0-10-0 in 2 s", [pi](TB303Processor &p, double t, int) { set(p, "cutoff", float(0.5 - 0.5 * std::cos(t * pi))); });
	moving("RESONANCE swept 0-10-0 in 2 s", [pi](TB303Processor &p, double t, int) { set(p, "resonance", float(0.5 - 0.5 * std::cos(t * pi))); });
	moving("ENV MOD swept 0-10-0 in 2 s", [pi](TB303Processor &p, double t, int) { set(p, "envmod", float(0.5 - 0.5 * std::cos(t * pi))); });
	moving("CUT OFF jumps 0 / 10 each block", [](TB303Processor &p, double, int b) { set(p, "cutoff", b % 2 ? 1.f : 0.f); });
	moving("RESONANCE jumps 0 / 10 each block", [](TB303Processor &p, double, int b) { set(p, "resonance", b % 2 ? 1.f : 0.f); });
	moving("ENV MOD jumps 0 / 10 each block", [](TB303Processor &p, double, int b) { set(p, "envmod", b % 2 ? 1.f : 0.f); });
	moving("ACCENT jumps 0 / 10 each block", [](TB303Processor &p, double, int b) { set(p, "accent", b % 2 ? 1.f : 0.f); });
	moving("DECAY jumps 0 / 10 each block", [](TB303Processor &p, double, int b) { set(p, "decay", b % 2 ? 1.f : 0.f); });
	moving("TUNING jumps 0 / 10 each block", [](TB303Processor &p, double, int b) { set(p, "tuning", b % 2 ? 1.f : 0.f); });
	moving("WAVEFORM flips each block", [](TB303Processor &p, double, int b) { set(p, "waveform", float(b % 2)); });
	moving("every knob at random each block", [](TB303Processor &p, double, int b) {
		std::mt19937 r(unsigned(b) * 2654435761u);
		std::uniform_real_distribution<float> u(0, 1);
		for (const char *id : {"cutoff", "resonance", "envmod", "decay", "accent", "tuning"})
			set(p, id, u(r) < 0.5f ? (u(r) < 0.5f ? 0.f : 1.f) : u(r));
	});
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

struct Result {
	tb303::Voice::Probe probe;
	double flatness = 0;
	float peak = 0;
	bool finite = true;
};

Result run(const Case &k, double rate, const std::string &wavDir)
{
	TB303Processor p(&rom());
	for (const auto &q : k.params)
		set(p, q.first, q.second);
	const int block = 512;
	p.prepareToPlay(rate, block);
	const int total = int(k.seconds * rate);
	const juce::MidiBuffer all = notes(rate, k.seconds, k.extremeNotes);
	std::vector<float> out(static_cast<size_t>(total));
	juce::AudioBuffer<float> buf(2, block);
	Result r;
	for (int pos = 0, b = 0; pos < total; pos += block, ++b) {
		const int n = std::min(block, total - pos);
		buf.setSize(2, n, false, false, true);
		buf.clear();
		juce::MidiBuffer midi;
		for (const auto m : all)
			if (m.samplePosition >= pos && m.samplePosition < pos + n)
				midi.addEvent(m.getMessage(), m.samplePosition - pos);
		if (k.automate)
			k.automate(p, pos / rate, b);
		p.processBlock(buf, midi);
		const float *s = buf.getReadPointer(0);
		for (int i = 0; i < n; ++i) {
			r.finite = r.finite && std::isfinite(s[i]);
			r.peak = std::max(r.peak, std::abs(s[i]));
			out[size_t(pos + i)] = s[i];
		}
	}
	r.probe = p.snapshot().probe;
	if (!wavDir.empty()) {
		juce::String name = juce::String(int(rate)) + "-" + juce::String(k.name).replaceCharacters(" /,'.", "_____") + ".wav";
		const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile(wavDir).getChildFile(name);
		f.getParentDirectory().createDirectory();
		f.deleteFile();
		juce::AudioBuffer<float> b(1, total);
		b.copyFrom(0, 0, out.data(), total);
		juce::WavAudioFormat wav;
		std::unique_ptr<juce::OutputStream> st(f.createOutputStream());
		std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(st.get(), rate, 1, 32, {}, 0));
		st.release();
		w->writeFromAudioSampleBuffer(b, 0, total);
	}
	r.flatness = medianFlatness(out, rate);
	return r;
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI juce;
	std::vector<double> rates = {44100, 48000, 96000};
	std::string only, wavDir;
	bool list = false;
	int jobs = int(std::max(1u, std::thread::hardware_concurrency() / 2));
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
		else {
			std::fprintf(stderr, "usage: tb303corners [--rate HZ] [--only TEXT] [--list] [--jobs N] [--wav DIR]\n");
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
	if (all.empty()) {
		std::fprintf(stderr, "no case matches \"%s\"\n", only.c_str());
		return 2;
	}
	if (rom().getSize() == 0)
		std::fprintf(stderr, "note: no ROM at %s; voice mode\n", TB303_ROM_PATH);

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
		tb303::SolverStats total, by[tb303::kSolverCount];
		size_t worst = 0, loudest = 0;
		for (size_t i = 0; i < all.size(); ++i) {
			const Result &r = res[i];
			total += r.probe.solver;
			for (int s = 0; s < tb303::kSolverCount; ++s)
				by[s] += r.probe.solvers[s];
			if (r.flatness > res[worst].flatness)
				worst = i;
			if (r.peak > res[loudest].peak)
				loudest = i;
			std::string why;
			if (r.probe.solver.failures > 0) {
				why += " unconverged " + std::to_string(r.probe.solver.failures) + " (";
				for (int s = 0, first = 1; s < tb303::kSolverCount; ++s)
					if (r.probe.solvers[s].failures > 0) {
						why += std::string(first ? "" : ", ") + tb303::kSolverNames[s] + " " + std::to_string(r.probe.solvers[s].failures);
						first = 0;
					}
				why += ")";
			}
			if (r.flatness > kNoiseFlatness)
				why += " noise (flatness " + juce::String(r.flatness, 3).toStdString() + ")";
			if (!r.finite || r.peak > kMaxPeak)
				why += " output " + std::string(r.finite ? "peak " + juce::String(r.peak, 2).toStdString() : "not finite");
			if (r.probe.restarts > 0)
				why += " restarted after a non-finite sample " + std::to_string(r.probe.restarts) + "x";
			if (!why.empty()) {
				++bad;
				std::printf("  FAIL %6.0f  %-66s%s\n", rate, all[i].name.c_str(), why.c_str());
			}
		}
		std::printf("%6.0f Hz: %zu cases, %ld solves, %ld retried in shorter steps (%.5f %%), %ld unconverged; "
		            "noisiest: %s (median flatness %.3f, limit %.2f); loudest: %s (peak %.2f, limit %.1f)\n",
		            rate, all.size(), total.solves, total.retried,
		            total.solves ? 100.0 * double(total.retried) / double(total.solves) : 0.0, total.failures,
		            all[worst].name.c_str(), res[worst].flatness, kNoiseFlatness, all[loudest].name.c_str(),
		            double(res[loudest].peak), double(kMaxPeak));
		std::printf("          by solver (solves / retried / unconverged / most iterations):");
		for (int s = 0; s < tb303::kSolverCount; ++s)
			std::printf("%s %s %ld/%ld/%ld/%d", s ? "," : "", tb303::kSolverNames[s], by[s].solves, by[s].retried,
			            by[s].failures, by[s].maxIterations);
		std::printf("\n");
	}
	std::printf("%s: %d failing case(s)\n", bad ? "FAIL" : "PASS", bad);
	return bad ? 1 : 0;
}
