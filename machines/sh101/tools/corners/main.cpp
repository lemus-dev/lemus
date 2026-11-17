// sh101corners: the controls' extremes, checked for what the other checks can't see. Every other
// check compares against something (SPICE, real recordings, golden renders) only where that
// something exists; this one renders the corners nobody recorded (FREQ and RES at their tops, both
// ends of the keyboard, every source, fast envelopes, hard modulation) through the plugin's
// processor at 44.1 and 48 kHz, and fails on:
//   - any implicit solve that didn't converge (sh101::SolverStats::failures, every solver of the
//     voice): an unconverged sample is silent in every other check, and near the top of the VCF's
//     range it once turned the self-oscillation into white noise (docs/improvements.md, 2026-11-16);
//   - broadband noise where nothing should make it: with no noise source and no noise or random
//     modulation, a case whose median spectral flatness (40 Hz-16 kHz, 2048-sample windows above
//     -70 dBFS) passes kNoiseFlatness. Tones, chirps and thumps sit far below it; the old bug
//     measured 0.39-0.50;
//   - output that isn't finite, or peaks past kMaxPeak.
// It also reports how many solves needed a retry in shorter steps.
//
//   sh101corners [--rate 44100|48000] [--only TEXT] [--list] [--jobs N] [--wav DIR]
//
// --wav writes each case's render to DIR/<rate>-<case>.wav, to listen to one.
//
// Exit status 0 when every case passes. About a minute on an M1 Max.
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace {

using Id = SH101Processor::Id;

constexpr double kNoiseFlatness = 0.2;
constexpr float kMaxPeak = 4.0f; // well past anything the voice reaches (about 1)

const juce::MemoryBlock &rom()
{
	static const juce::MemoryBlock r = [] {
		juce::MemoryBlock b;
		juce::File(SH101_ROM_PATH).loadFileAsData(b);
		return b;
	}();
	return r;
}

void set(SH101Processor &p, const char *id, float v)
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
	bool noisy = false; // a noise source or noise/random modulation: no flatness check
	double seconds = 1.0;
	// Per block, given the time: automation.
	std::function<void(SH101Processor &, double)> automate;
	bool bend = false; // pitch bend swept full down and up
};

// Notes: both ends of the keyboard and the middle, short and legato, at 0.12 s each.
juce::MidiBuffer notes(double rate, double seconds, bool bend)
{
	juce::MidiBuffer m;
	const int keys[] = {24, 36, 60, 84, 96, 48, 72, 108};
	const double step = 0.12;
	int i = 0;
	for (double t = 0.02; t < seconds - 0.25; t += step, ++i) {
		const int k = keys[i % 8];
		const bool legato = i % 3 == 2;
		m.addEvent(juce::MidiMessage::noteOn(1, k, juce::uint8(100)), int(t * rate));
		m.addEvent(juce::MidiMessage::noteOff(1, k), int((t + (legato ? 1.3 : 0.6) * step) * rate));
	}
	if (bend)
		for (double t = 0; t < seconds; t += 0.01) {
			const double v = std::sin(t * 2 * 3.14159265358979 * 3);
			m.addEvent(juce::MidiMessage::pitchWheel(1, std::clamp(int(8192 + 8191 * v), 0, 16383)), int(t * rate));
		}
	return m;
}

std::vector<Case> cases()
{
	std::vector<Case> c;
	const float freqs[] = {0, 0.3f, 0.6f, 0.8f, 0.9f, 1};
	const float ress[] = {0, 0.6f, 0.85f, 1};
	const char *srcName[] = {"saw", "all", "none"};
	for (float f : freqs)
		for (float r : ress)
			for (int env = 0; env < 2; ++env)
				for (int src = 0; src < 3; ++src)
					for (int range : {0, 3}) {
						Case k;
						char n[96];
						std::snprintf(n, sizeof n, "freq %.2f res %.2f env %d %s %s", f, r, env, srcName[src],
						              range == 0 ? "16'" : "2'");
						k.name = n;
						k.params = {{Id::vcfFreq, f},       {Id::vcfRes, r},         {Id::vcfEnv, float(env)},
						            {Id::vcfKybd, 1},       {Id::range, float(range)}, {Id::vcaMode, 1},
						            {Id::attack, 0},        {Id::decay, 0.3f},        {Id::sustain, 0.5f},
						            {Id::release, 0.2f},    {Id::noiseLevel, 0},      {Id::portaMode, 2},
						            {Id::sawLevel, src == 2 ? 0.f : 1.f},
						            {Id::pulseLevel, src == 1 ? 1.f : 0.f},
						            {Id::pulseWidth, 1},     {Id::pwmSource, 1},
						            {Id::subLevel, src == 1 ? 1.f : 0.f}};
						c.push_back(k);
					}
	// Sweeps and hard modulation, at RES 10.
	auto base = [](Case k) {
		k.params.insert(k.params.begin(), {{Id::vcfRes, 1}, {Id::vcfKybd, 1}, {Id::vcaMode, 1}, {Id::sawLevel, 1},
		                                   {Id::noiseLevel, 0}, {Id::portaMode, 2}});
		k.seconds = 2.0;
		return k;
	};
	{
		Case k{"FREQ swept 0-10-0 in 2 s"};
		k.automate = [](SH101Processor &p, double t) { set(p, Id::vcfFreq, float(0.5 - 0.5 * std::cos(t * 3.14159265))); };
		c.push_back(base(k));
	}
	{
		Case k{"RES swept 0-10-0 at FREQ 10"};
		k.params = {{Id::vcfFreq, 1}};
		k.automate = [](SH101Processor &p, double t) { set(p, Id::vcfRes, float(0.5 - 0.5 * std::cos(t * 3.14159265))); };
		Case b = base(k);
		b.params.erase(b.params.begin()); // RES automated
		c.push_back(b);
	}
	{
		Case k{"FREQ jumps 0 / 10 each block"};
		k.automate = [](SH101Processor &p, double t) { set(p, Id::vcfFreq, int(t * 100) % 2 ? 1.f : 0.f); };
		c.push_back(base(k));
	}
	{
		Case k{"VCF MOD 10, LFO square at top rate, FREQ 8"};
		k.params = {{Id::vcfFreq, 0.8f}, {Id::vcfMod, 1}, {Id::lfoWave, 1}, {Id::lfoRate, 1}};
		c.push_back(base(k));
	}
	{
		Case k{"VCF MOD 10, LFO random, FREQ 7"};
		k.params = {{Id::vcfFreq, 0.7f}, {Id::vcfMod, 1}, {Id::lfoWave, 2}, {Id::lfoRate, 1}};
		k.noisy = true;
		c.push_back(base(k));
	}
	{
		Case k{"VCF MOD 10, LFO noise, FREQ 7"};
		k.params = {{Id::vcfFreq, 0.7f}, {Id::vcfMod, 1}, {Id::lfoWave, 3}, {Id::lfoRate, 1}};
		k.noisy = true;
		c.push_back(base(k));
	}
	{
		Case k{"ENV 10, A/D/S/R 0, FREQ 6"};
		k.params = {{Id::vcfFreq, 0.6f}, {Id::vcfEnv, 1}, {Id::attack, 0}, {Id::decay, 0}, {Id::sustain, 0}, {Id::release, 0}};
		c.push_back(base(k));
	}
	{
		Case k{"noise 10, FREQ 9"};
		k.params = {{Id::vcfFreq, 0.9f}, {Id::noiseLevel, 1}};
		k.noisy = true;
		Case b = base(k);
		b.params.erase(std::remove_if(b.params.begin(), b.params.end(),
		                              [](auto &q) { return std::string(q.first) == Id::noiseLevel && q.second == 0; }),
		               b.params.end());
		c.push_back(b);
	}
	{
		Case k{"bend VCF 10, wheel swept, FREQ 9"};
		k.params = {{Id::vcfFreq, 0.9f}, {Id::bendVcf, 1}, {Id::bendVco, 1}};
		k.bend = true;
		c.push_back(base(k));
	}
	{
		Case k{"portamento 10, FREQ 9"};
		k.params = {{Id::vcfFreq, 0.9f}, {Id::portamento, 1}};
		Case b = base(k);
		for (auto &q : b.params)
			if (std::string(q.first) == Id::portaMode)
				q.second = 0;
		c.push_back(b);
	}
	{
		Case k{"VCO MOD 10, LFO square, PWM LFO, everything up, FREQ 10"};
		k.params = {{Id::vcfFreq, 1}, {Id::vcoMod, 1}, {Id::lfoWave, 1}, {Id::lfoRate, 1}, {Id::pwmSource, 0},
		            {Id::pulseLevel, 1}, {Id::subLevel, 1}, {Id::volume, 1}, {Id::vcfEnv, 1}};
		c.push_back(base(k));
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

struct Result {
	sh101::SolverStats solver;
	double flatness = 0;
	float peak = 0;
	bool finite = true;
};

Result run(const Case &k, double rate, const std::string &wavDir)
{
	SH101Processor p(&rom());
	p.settlePower();
	for (const auto &q : k.params)
		set(p, q.first, q.second);
	const int block = 512;
	p.prepareToPlay(rate, block);
	const int total = int(k.seconds * rate);
	const juce::MidiBuffer all = notes(rate, k.seconds, k.bend);
	std::vector<float> out(static_cast<size_t>(total));
	juce::AudioBuffer<float> buf(2, block);
	Result r;
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
		const float *s = buf.getReadPointer(0);
		for (int i = 0; i < n; ++i) {
			r.finite = r.finite && std::isfinite(s[i]);
			r.peak = std::max(r.peak, std::abs(s[i]));
			out[size_t(pos + i)] = s[i];
		}
	}
	r.solver = p.snapshot().probe.solver;
	if (!wavDir.empty()) {
		juce::String name = juce::String(int(rate)) + "-" + juce::String(k.name).replaceCharacters(" /,'", "____") + ".wav";
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
	r.flatness = k.noisy ? 0 : medianFlatness(out, rate);
	return r;
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI juce;
	std::vector<double> rates = {44100, 48000};
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
			std::fprintf(stderr, "usage: sh101corners [--rate HZ] [--only TEXT] [--list] [--jobs N] [--wav DIR]\n");
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
		std::fprintf(stderr, "note: no ROM at %s; voice mode\n", SH101_ROM_PATH);

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
		sh101::SolverStats total;
		size_t worst = 0;
		for (size_t i = 0; i < all.size(); ++i) {
			const Result &r = res[i];
			total += r.solver;
			if (r.flatness > res[worst].flatness)
				worst = i;
			std::string why;
			if (r.solver.failures > 0)
				why += " unconverged " + std::to_string(r.solver.failures);
			if (r.flatness > kNoiseFlatness)
				why += " noise (flatness " + juce::String(r.flatness, 3).toStdString() + ")";
			if (!r.finite || r.peak > kMaxPeak)
				why += " output " + std::string(r.finite ? "peak " + juce::String(r.peak, 2).toStdString() : "not finite");
			if (!why.empty()) {
				++bad;
				std::printf("  FAIL %5.0f  %-52s%s\n", rate, all[i].name.c_str(), why.c_str());
			}
		}
		std::printf("%5.0f Hz: %zu cases, %ld solves, %ld retried in shorter steps (%.4f %%), %ld unconverged; "
		            "noisiest: %s (median flatness %.3f, limit %.2f)\n",
		            rate, all.size(), total.solves, total.retried,
		            total.solves ? 100.0 * double(total.retried) / double(total.solves) : 0.0, total.failures,
		            all[worst].name.c_str(), res[worst].flatness, kNoiseFlatness);
	}
	std::printf("%s: %d failing case(s)\n", bad ? "FAIL" : "PASS", bad);
	return bad ? 1 : 0;
}
