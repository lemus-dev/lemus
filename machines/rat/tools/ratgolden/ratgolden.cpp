// Golden renders and a benchmark, for making the model cheaper without changing its sound.
//
//   ratgolden --write DIR      render every case into DIR (64-bit float WAV, mono)
//   ratgolden --check DIR      render again and compare with DIR: the largest difference per case,
//                              in dB re full scale ("exact" when bit-identical)
//   ratgolden --pairs          render the cases two at a time as a stereo pair (Channel::processPair)
//                              and compare each side with the same case rendered alone: must be exact
//   ratgolden --bench [N]      cost per second of audio for the benchmark workloads, best of N runs
//   ratgolden --list           the cases
//
// The cases drive rat::Channel the way the plugin does: calibration, op amp, knobs set at
// once, then the knobs, LEVEL and the footswitch updated once per 512-sample block. They cover
// guitar- and synth-like input, a sine sweep, loud and quiet input, every knob at its ends and
// middle, both calibrations and op amps, 44.1 to 192 kHz, knob automation, bypass and LEVEL changes.
#include "rat/core/unit.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace rat;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Deterministic noise (no library generator, so the input is the same everywhere).
struct Lcg {
	uint32_t s = 12345;
	double next()
	{
		s = s * 1664525u + 1013904223u;
		return double(s >> 8) / double(1u << 24) * 2 - 1;
	}
};

// A palm-muted riff on the low strings: eighth notes at 120 BPM with decaying harmonics.
std::vector<double> guitar(double rate, double seconds, double gain)
{
	static const double notes[] = {82.41, 82.41, 98.0, 82.41, 110.0, 82.41, 123.47, 98.0, 146.83, 196.0};
	std::vector<double> x(size_t(rate * seconds));
	Lcg n;
	for (size_t i = 0; i < x.size(); ++i) {
		const double t = double(i) / rate, step = t / 0.25;
		const int k = int(step) % 10;
		const double tn = (step - std::floor(step)) * 0.25;
		double v = 0;
		for (int h = 1; h <= 8; ++h)
			v += std::sin(2 * kPi * notes[k] * h * tn + 0.3 * h) / (h * h) * std::exp(-tn * (6 + h));
		x[i] = gain * (0.35 * v + 0.001 * n.next());
	}
	return x;
}

// A detuned saw chord with a slow swell and release.
std::vector<double> synth(double rate, double seconds, double gain)
{
	static const double f[] = {110.0, 110.7, 164.8, 220.9, 277.2};
	std::vector<double> x(size_t(rate * seconds));
	double ph[5] = {0, 0.21, 0.47, 0.63, 0.89};
	for (size_t i = 0; i < x.size(); ++i) {
		const double t = double(i) / rate;
		double v = 0;
		for (int k = 0; k < 5; ++k) {
			ph[k] += f[k] / rate;
			ph[k] -= std::floor(ph[k]);
			v += 2 * ph[k] - 1;
		}
		const double env = std::min(1.0, t / 0.2) * std::min(1.0, std::max(0.0, (seconds - t) / 0.3));
		x[i] = gain * 0.15 * v * env;
	}
	return x;
}

// Logarithmic sine sweep, 20 Hz to 20 kHz (or Nyquist).
std::vector<double> sweep(double rate, double seconds, double gain)
{
	std::vector<double> x(size_t(rate * seconds));
	const double f0 = 20, f1 = std::min(20000.0, 0.45 * rate), k = std::log(f1 / f0) / seconds;
	for (size_t i = 0; i < x.size(); ++i) {
		const double t = double(i) / rate;
		x[i] = gain * std::sin(2 * kPi * f0 * (std::exp(k * t) - 1) / k);
	}
	return x;
}

struct Case {
	std::string name;
	std::string signal = "guitar"; // guitar, synth, sweep, burst
	double rate = 48000, seconds = 1.5, gain = 1.0, level = kDefaultLevelDbu;
	bool captured = false, op07 = false, china = false;
	Controls k;
	// Per-block changes: automation 1 = knob sweeps, 2 = footswitch toggles, 3 = LEVEL steps.
	int automation = 0;
	bool probes = false; // the node probes on, as the plugin had them
};

std::vector<double> input(const Case &c)
{
	if (c.signal == "synth")
		return synth(c.rate, c.seconds, c.gain);
	if (c.signal == "sweep")
		return sweep(c.rate, c.seconds, c.gain);
	auto x = guitar(c.rate, c.seconds, c.gain);
	if (c.signal == "burst") // silence, then the riff
		for (size_t i = 0; i < x.size() / 3; ++i)
			x[i] = 0;
	return x;
}

struct Rig {
	Channel ch;

	explicit Rig(const Case &c)
	{
		ch.pedal().enableProbes(c.probes);
		ch.pedal().setCalibration(c.captured ? capturedCalibration() : datasheetCalibration());
		ch.pedal().setOpAmp(c.op07 ? OpAmp::OP07 : OpAmp::LM308);
		if (c.china)
			ch.pedal().setParts(chinaParts());
		ch.pedal().setControls(c.k, true);
		ch.setLevelDbu(c.level);
		ch.prepare(c.rate);
	}

	// The once-per-block updates the plugin makes.
	void block(const Case &c, size_t pos)
	{
		const double t = double(pos) / c.rate, u = t / c.seconds;
		if (c.automation == 1) {
			Controls k;
			k.distortion = u;
			k.filter = 1 - u;
			k.volume = 0.5 + 0.45 * std::sin(2 * kPi * 1.3 * t);
			ch.pedal().setControls(k);
		} else if (c.automation == 2) {
			ch.setEngaged(int(t / 0.4) % 2 == 0);
		} else if (c.automation == 3) {
			ch.setLevelDbu(int(t / 0.5) % 2 ? 14.0 : -6.0);
		}
	}
};

constexpr int kBlock = 512;

// Newton statistics over everything rendered.
long g_failures = 0, g_iterations = 0, g_samples = 0;

std::vector<double> render(const Case &c, const std::vector<double> &x)
{
	Rig rig(c);
	std::vector<double> y(x.size());
	for (size_t pos = 0; pos < x.size(); pos += kBlock) {
		rig.block(c, pos);
		const size_t end = std::min(x.size(), pos + kBlock);
		for (size_t i = pos; i < end; ++i)
			y[i] = rig.ch.process(x[i]);
	}
	g_failures += rig.ch.pedal().failures();
	g_iterations += rig.ch.pedal().iterations();
	g_samples += rig.ch.pedal().samples();
	return y;
}

// Two cases at the same rate as a stereo pair, for as long as the shorter one.
void renderPair(const Case &a, const Case &b, const std::vector<double> &xa, const std::vector<double> &xb,
	std::vector<double> &ya, std::vector<double> &yb)
{
	Rig ra(a), rb(b);
	const size_t n = std::min(xa.size(), xb.size());
	ya.assign(n, 0);
	yb.assign(n, 0);
	for (size_t pos = 0; pos < n; pos += kBlock) {
		ra.block(a, pos);
		rb.block(b, pos);
		const size_t end = std::min(n, pos + kBlock);
		for (size_t i = pos; i < end; ++i)
			Channel::processPair(ra.ch, rb.ch, xa[i], xb[i], ya[i], yb[i]);
	}
}

std::vector<Case> cases()
{
	std::vector<Case> out;
	const Controls knobs[] = {{0, 0, 0}, {0.5, 0.5, 0.5}, {1, 1, 1}, {1, 0, 0.7}, {0, 1, 0.3}, {0.8, 0.3, 1}, {1, 0.5, 0}};
	auto tag = [](const Case &c) {
		char b[160];
		std::snprintf(b, sizeof b, "%s_%s_%s_%gk_d%g_f%g_v%g", c.signal.c_str(), c.captured ? "cap" : "ds",
			c.op07 ? "op07" : "lm308", c.rate / 1000, c.k.distortion, c.k.filter, c.k.volume);
		return std::string(b);
	};
	for (int cal = 0; cal < 2; ++cal)
		for (int op = 0; op < 2; ++op) {
			for (const auto &k : knobs)
				for (const char *s : {"guitar", "synth"}) {
					Case c;
					c.signal = s;
					c.captured = cal;
					c.op07 = op;
					c.k = k;
					c.name = tag(c);
					out.push_back(c);
				}
			Case c;
			c.signal = "sweep";
			c.seconds = 2.0;
			c.gain = 0.5;
			c.captured = cal;
			c.op07 = op;
			c.name = tag(c);
			out.push_back(c);
		}
	for (double rate : {44100.0, 96000.0, 192000.0})
		for (int combo = 0; combo < 2; ++combo)
			for (const char *s : {"guitar", "sweep"}) {
				Case c;
				c.signal = s;
				c.rate = rate;
				c.captured = combo == 0;
				c.op07 = combo == 1;
				c.k = {0.7, 0.4, 0.6};
				c.gain = std::string(s) == "sweep" ? 0.5 : 1.0;
				c.name = tag(c);
				out.push_back(c);
			}
	for (int combo = 0; combo < 2; ++combo) {
		Case loud;
		loud.captured = combo == 0;
		loud.op07 = combo == 1;
		loud.gain = 3.0;
		loud.level = 12;
		loud.k = {0.6, 0.2, 0.8};
		loud.name = "loud_" + tag(loud);
		out.push_back(loud);
		Case quiet = loud;
		quiet.gain = 0.01;
		quiet.level = kDefaultLevelDbu;
		quiet.name = "quiet_" + tag(quiet);
		out.push_back(quiet);
		Case burst = loud;
		burst.signal = "burst";
		burst.gain = 1.0;
		burst.level = kDefaultLevelDbu;
		burst.name = tag(burst);
		out.push_back(burst);
		for (double rate : {48000.0, 96000.0}) {
			Case a;
			a.captured = combo == 0;
			a.op07 = combo == 1;
			a.rate = rate;
			a.seconds = 3.0;
			a.automation = 1;
			a.name = "automation_" + tag(a);
			out.push_back(a);
		}
	}
	Case bypass;
	bypass.captured = true;
	bypass.automation = 2;
	bypass.seconds = 2.0;
	bypass.name = "bypass_" + tag(bypass);
	out.push_back(bypass);
	Case level = bypass;
	level.automation = 3;
	level.signal = "synth";
	level.name = "levelsteps_" + tag(level);
	out.push_back(level);
	Case china;
	china.china = true;
	china.captured = true;
	china.op07 = true;
	china.name = "china_" + tag(china);
	out.push_back(china);
	return out;
}

// 64-bit float WAV, mono.
bool writeWav(const std::string &path, const std::vector<double> &y, double rate)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	const uint32_t bytes = uint32_t(y.size() * 8);
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + bytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(3);
	u16(1);
	u32(uint32_t(rate));
	u32(uint32_t(rate) * 8);
	u16(8);
	u16(64);
	std::fwrite("data", 1, 4, f);
	u32(bytes);
	std::fwrite(y.data(), 8, y.size(), f);
	std::fclose(f);
	return true;
}

bool readWav(const std::string &path, std::vector<double> &y)
{
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 44, SEEK_SET);
	y.resize(size_t(std::max(0L, size - 44) / 8));
	const size_t n = std::fread(y.data(), 8, y.size(), f);
	std::fclose(f);
	return n == y.size();
}

double seconds(const std::function<void()> &f)
{
	const auto t0 = std::chrono::steady_clock::now();
	f();
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

int main(int argc, char **argv)
{
	const std::string mode = argc > 1 ? argv[1] : "";
	const auto all = cases();
	if (mode == "--list") {
		for (const auto &c : all)
			std::printf("%s\n", c.name.c_str());
		return 0;
	}
	if ((mode == "--write" || mode == "--check") && argc > 2) {
		const std::string dir = argv[2];
		double worst = 0;
		int exact = 0;
		std::string worstName;
		for (const auto &c : all) {
			const auto y = render(c, input(c));
			const std::string path = dir + "/" + c.name + ".wav";
			if (mode == "--write") {
				if (!writeWav(path, y, c.rate)) {
					std::fprintf(stderr, "cannot write %s\n", path.c_str());
					return 1;
				}
				continue;
			}
			std::vector<double> g;
			if (!readWav(path, g) || g.size() != y.size()) {
				std::fprintf(stderr, "cannot read %s\n", path.c_str());
				return 1;
			}
			double d = 0;
			for (size_t i = 0; i < y.size(); ++i)
				d = std::max(d, std::fabs(y[i] - g[i]));
			if (d == 0)
				++exact;
			else
				std::printf("  %-60s %.1f dB\n", c.name.c_str(), 20 * std::log10(d));
			if (d >= worst) {
				worst = d;
				worstName = c.name;
			}
		}
		if (mode == "--check") {
			std::printf("Newton: %.4f iterations per sample, %ld failures in %ld samples\n",
				double(g_iterations) / double(std::max(1L, g_samples)), g_failures, g_samples);
			std::printf("%d of %zu cases bit-identical; worst %s (%s)\n", exact, all.size(),
				worst == 0 ? "exact" : (std::to_string(20 * std::log10(worst)) + " dB").c_str(), worstName.c_str());
			return worst < 1e-6 ? 0 : 1; // -120 dB
		}
		std::printf("wrote %zu cases to %s\n", all.size(), dir.c_str());
		return 0;
	}
	if (mode == "--pairs") {
		int pairs = 0, bad = 0;
		for (size_t i = 0; i < all.size(); ++i) {
			// Pair each case with the next one at the same rate.
			size_t j = (i + 1) % all.size();
			while (all[j].rate != all[i].rate)
				j = (j + 1) % all.size();
			std::vector<double> ya, yb;
			const auto xa = input(all[i]), xb = input(all[j]);
			renderPair(all[i], all[j], xa, xb, ya, yb);
			const auto ga = render(all[i], xa), gb = render(all[j], xb);
			double d = 0;
			for (size_t k = 0; k < ya.size(); ++k)
				d = std::max({d, std::fabs(ya[k] - ga[k]), std::fabs(yb[k] - gb[k])});
			++pairs;
			if (d != 0) {
				++bad;
				std::printf("  %s + %s: %.1f dB\n", all[i].name.c_str(), all[j].name.c_str(), 20 * std::log10(d));
			}
		}
		std::printf("%d of %d pairs identical to the cases rendered alone\n", pairs - bad, pairs);
		return bad ? 1 : 0;
	}
	if (mode == "--bench") {
		const int runs = argc > 2 ? std::atoi(argv[2]) : 5;
		struct Work {
			const char *name;
			Case c;
		};
		std::vector<Work> work;
		for (double rate : {44100.0, 48000.0, 96000.0}) {
			Case g; // the plugin's defaults: Captured, LM308, knobs 5 / 4 / 5
			g.captured = true;
			g.k = {0.5, 0.4, 0.5};
			g.rate = rate;
			g.seconds = 2.0;
			work.push_back({"guitar, defaults", g});
			Case gp = g;
			gp.probes = true;
			work.push_back({"guitar, defaults, probes", gp});
			Case s = g;
			s.signal = "synth";
			s.k = {1, 0.2, 0.6};
			work.push_back({"synth, full distortion", s});
			Case o = g;
			o.op07 = true;
			o.k = {0.3, 0.7, 0.7};
			work.push_back({"guitar, OP07, light", o});
			Case a = g;
			a.automation = 1;
			work.push_back({"guitar, knob automation", a});
		}
		std::printf("%-30s %8s %12s\n", "workload (per channel)", "rate", "% of a core");
		for (const auto &w : work) {
			const auto x = input(w.c);
			double best = 1e9;
			for (int r = 0; r < runs; ++r)
				best = std::min(best, seconds([&] { render(w.c, x); }));
			std::printf("%-30s %7.1fk %11.2f%%\n", w.name, w.c.rate / 1000, 100 * best / w.c.seconds);
		}
		// A stereo instance with different left and right (guitar and synth): both channels.
		for (double rate : {44100.0, 48000.0, 96000.0}) {
			Case g;
			g.captured = true;
			g.k = {0.5, 0.4, 0.5};
			g.rate = rate;
			g.seconds = 2.0;
			Case s = g;
			s.signal = "synth";
			const auto xg = input(g), xs = input(s);
			double one = 1e9, pair = 1e9;
			for (int r = 0; r < runs; ++r) {
				one = std::min(one, seconds([&] {
					render(g, xg);
					render(s, xs);
				}));
				std::vector<double> ya, yb;
				pair = std::min(pair, seconds([&] { renderPair(g, s, xg, xs, ya, yb); }));
			}
			std::printf("%-30s %7.1fk %11.2f%%  (one at a time %.2f%%)\n", "stereo L/R differ, per channel", rate / 1000,
				50 * pair / g.seconds, 50 * one / g.seconds);
		}
		return 0;
	}
	std::fprintf(stderr, "usage: ratgolden --write DIR | --check DIR | --pairs | --bench [N] | --list\n");
	return 2;
}
