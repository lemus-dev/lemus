// bsrackbench: the workloads every round measures, their cost and golden renders.
//
//   bsrackbench time [N]          cost per workload: share of one core at its rate, the worst
//                                 64-sample block against its own duration, time asleep; best of N
//   bsrackbench golden DIR [CASE] render every workload (or CASE) into DIR (mono float 32 WAV)
//   bsrackbench check DIR [CASE]  render again and compare: the largest difference per workload in
//                                 dB re full scale ("exact" when bit-identical)
//   bsrackbench list
//   bsrackbench run NAME          one workload only, for /usr/bin/time -l's instruction count
//
// Each workload is the machine as the plugin runs it (core/machine.h): firmware v1.5 from the private
// folder (BSRACK_ROM), a new unit's EEPROM, its pots and switches, MIDI and panel buttons on a
// script, in 64-sample blocks.
#include "bsrack/core/machine.h"
#include "bsrack/tools/hands.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#ifndef BSRACK_ROM
#define BSRACK_ROM ""
#endif

namespace {

using namespace bsrack;
constexpr int kBlock = 64;

struct Ev {
	double at;
	std::vector<uint8_t> midi; // or empty: a button
	int button = -1;
	bool down = false;
};
struct Case {
	const char *name;
	double rate, seconds;
	std::function<void(Controls &)> set;
	std::vector<Ev> events;
	std::function<void(Controls &, double)> automate; // per block, from time t (s)
};

std::vector<Ev> notes(double from, double step, int n, const std::vector<int> &ns, double len = 0.8)
{
	std::vector<Ev> e;
	for (int i = 0; i < n; ++i) {
		const uint8_t k = uint8_t(ns[size_t(i) % ns.size()]);
		e.push_back({from + i * step, {0x90, k, 100}});
		e.push_back({from + i * step + step * len, {0x80, k, 64}});
	}
	return e;
}

std::vector<Ev> press(double at, int button)
{
	return {{at, {}, button, true}, {at + 0.06, {}, button, false}};
}

std::vector<Case> cases()
{
	const std::vector<int> riff = {36, 36, 48, 43, 39, 36, 46, 41};
	std::vector<Ev> demo;
	for (double t : {0.3, 0.6, 0.9})
		for (auto &e : press(t, kMode))
			demo.push_back(e);
	for (auto &e : press(1.2, kAudition))
		demo.push_back(e);
	return {
		{"idle-48k", 48000, 4, [](Controls &) {}, {}, nullptr},
		{"note-48k", 48000, 3, [](Controls &) {}, {{0.1, {0x90, 48, 100}}, {2.0, {0x80, 48, 64}}}, nullptr},
		{"bass-48k", 48000, 4,
		 [](Controls &c) {
			 c.pots[kResonance] = 0.8;
			 c.pots[kModDepth] = 0.85;
			 c.switches[kOsc1Wave] = 1;
		 },
		 notes(0.1, 0.25, 15, riff), nullptr},
		{"sync-sweep-48k", 48000, 4,
		 [](Controls &c) {
			 c.switches[kSync] = 1;
			 c.pots[kMix] = 1.0;
			 c.pots[kResonance] = 0.6;
		 },
		 {{0.1, {0x90, 40, 100}}, {3.6, {0x80, 40, 64}}},
		 [](Controls &c, double t) {
			 c.pots[kFrequency] = 0.5 + 0.5 * std::sin(t * 2.0);
			 c.pots[kSemitone] = 0.5 + 0.5 * std::sin(t * 3.1);
		 }},
		{"demo-44k", 44100, 30, [](Controls &) {}, demo, nullptr},
	};
}

std::vector<uint8_t> loadRom()
{
	std::ifstream f(BSRACK_ROM, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Result {
	std::vector<float> out;
	double seconds = 0, worstBlock = 0, worstAt = 0, asleep = 0;
};

Result render(const Case &c, const std::vector<uint8_t> &rom)
{
	Machine m(rom);
	m.prepare(c.rate);
	Controls ctl;
	const double defaults[kPots] = {0.7, 0, 0.5, 0, 0.5, 0.5, 0, 0.5, 0.5, 0.5, 0.6, 0, 0.5, 0, 0.4, 1, 0.3, 0};
	for (int i = 0; i < kPots; ++i)
		ctl.pots[size_t(i)] = defaults[i];
	ctl.switches = {0, 1, 0, 1, 0, 0, 0, 1, 1, 1, 0, 2};
	m.setControls(ctl);
	{
		// boot, then the case's settings as hands make them (tools/hands.h); the script starts after
		std::vector<float> boot(size_t(c.rate * 0.3));
		m.render(boot.data(), int(boot.size()), nullptr);
		Controls target = ctl;
		c.set(target);
		tools::moveTo(m, ctl, target, c.rate);
	}
	auto ev = c.events;
	std::sort(ev.begin(), ev.end(), [](const Ev &a, const Ev &b) { return a.at < b.at; });
	const long total = long(c.seconds * c.rate);
	Result r;
	r.out.assign(size_t(total), 0.0f);
	size_t next = 0;
	long asleep = 0;
	const auto start = std::chrono::steady_clock::now();
	for (long pos = 0; pos < total; pos += kBlock) {
		const int n = int(std::min<long>(kBlock, total - pos));
		const double t = double(pos) / c.rate;
		while (next < ev.size() && ev[next].at <= t) {
			if (ev[next].button >= 0)
				m.setButton(ev[next].button, ev[next].down);
			else
				for (uint8_t b : ev[next].midi)
					m.midiIn(b);
			++next;
		}
		if (c.automate) {
			c.automate(ctl, t);
			m.setControls(ctl);
		}
		const auto b0 = std::chrono::steady_clock::now();
		m.render(r.out.data() + pos, n, nullptr);
		const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - b0).count();
		if (dt / (n / c.rate) > r.worstBlock) {
			r.worstBlock = dt / (n / c.rate);
			r.worstAt = t;
		}
		asleep += m.asleep() ? n : 0;
	}
	r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	r.asleep = double(asleep) / double(total);
	return r;
}

void writeWav(const std::string &path, const std::vector<float> &x, int rate)
{
	std::ofstream f(path, std::ios::binary);
	auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
	auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };
	f.write("RIFF", 4); u32(uint32_t(36 + x.size() * 4)); f.write("WAVE", 4);
	f.write("fmt ", 4); u32(16); u16(3); u16(1); u32(uint32_t(rate)); u32(uint32_t(rate * 4)); u16(4); u16(32);
	f.write("data", 4); u32(uint32_t(x.size() * 4));
	f.write(reinterpret_cast<const char *>(x.data()), std::streamsize(x.size() * 4));
}

std::vector<float> readWav(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (b.size() < 44)
		return {};
	std::vector<float> x((b.size() - 44) / 4);
	std::memcpy(x.data(), b.data() + 44, x.size() * 4);
	return x;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "bsrackbench time [N] | golden DIR [CASE] | check DIR [CASE] | list | run NAME\n");
		return 2;
	}
	const std::string mode = argv[1];
	const auto rom = loadRom();
	if (rom.size() != Machine::kRomSize) {
		std::fprintf(stderr, "no v1.5 ROM at %s\n", BSRACK_ROM);
		return 1;
	}
	const auto all = cases();
	if (mode == "list") {
		for (const auto &c : all)
			std::printf("%s\n", c.name);
		return 0;
	}
	if (mode == "run" && argc > 2) {
		for (const auto &c : all)
			if (c.name == std::string(argv[2]))
				render(c, rom);
		return 0;
	}
	if (mode == "time") {
		const int n = argc > 2 ? std::atoi(argv[2]) : 3;
		for (const auto &c : all) {
			Result best;
			best.seconds = 1e9;
			for (int i = 0; i < n; ++i) {
				auto r = render(c, rom);
				if (r.seconds < best.seconds)
					best = std::move(r);
			}
			std::printf("%-16s %6.2f %% of a core, worst block %6.1f %% (at %.2f s), asleep %5.1f %%\n", c.name,
			            100.0 * best.seconds / c.seconds, 100.0 * best.worstBlock, best.worstAt, 100.0 * best.asleep);
		}
		return 0;
	}
	if ((mode == "golden" || mode == "check") && argc > 2) {
		const std::string dir = argv[2];
		int bad = 0;
		for (const auto &c : all) {
			if (argc > 3 && c.name != std::string(argv[3]))
				continue;
			const auto r = render(c, rom);
			const std::string path = dir + "/" + c.name + ".wav";
			if (mode == "golden") {
				std::filesystem::create_directories(dir);
				std::ofstream probe(path, std::ios::binary | std::ios::app);
				if (!probe) {
					std::fprintf(stderr, "can't write %s\n", path.c_str());
					return 1;
				}
				probe.close();
				writeWav(path, r.out, int(c.rate));
				std::printf("%s\n", path.c_str());
				continue;
			}
			const auto g = readWav(path);
			if (g.size() != r.out.size()) {
				std::printf("%-16s no golden render (%s)\n", c.name, path.c_str());
				++bad;
				continue;
			}
			double worst = 0;
			for (size_t i = 0; i < g.size(); ++i)
				worst = std::max(worst, double(std::abs(g[i] - r.out[i])));
			if (worst == 0)
				std::printf("%-16s exact\n", c.name);
			else
				std::printf("%-16s largest difference %.1f dB\n", c.name, 20 * std::log10(worst));
		}
		return bad != 0;
	}
	std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
	return 2;
}
