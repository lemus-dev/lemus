// xpanderbench: the workloads every round measures, their cost and golden renders.
//
//   xpanderbench time [N]          cost per workload: share of one core at its rate, the worst
//                                  64-sample block against its own duration; best of N
//   xpanderbench golden DIR [CASE] render every workload (or CASE) into DIR (stereo float 32 WAV)
//   xpanderbench check DIR [CASE]  render again and compare: the largest difference per workload in
//                                  dB re full scale ("exact" when bit-identical)
//   xpanderbench list | run NAME
//
// Each workload starts from the factory state (the cached RAM of a reset, tuned unit; made first if
// missing, which takes a while), booted, then MIDI on a script in 64-sample blocks.
#include "xpander/core/warm.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace xpander;
constexpr int kBlock = 64;

std::vector<uint8_t> g_main, g_voice, g_factory;

std::vector<uint8_t> load(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

struct Note {
	int note, vel;
	double on, off;
};
struct Case {
	const char *name;
	double rate, seconds;
	std::vector<Note> notes;
};

std::vector<Case> cases()
{
	std::vector<Case> c;
	c.push_back({"idle-48k", 48000, 3, {}});
	c.push_back({"note-48k", 48000, 3, {{60, 100, 0.1, 2.5}}});
	c.push_back({"note-44k", 44100, 3, {{60, 100, 0.1, 2.5}}});
	c.push_back({"chord6-48k", 48000, 4, {{48, 100, 0.1, 3.5}, {55, 100, 0.1, 3.5}, {60, 100, 0.1, 3.5},
	                                      {64, 100, 0.1, 3.5}, {67, 100, 0.1, 3.5}, {72, 100, 0.1, 3.5}}});
	std::vector<Note> seq;
	for (int i = 0; i < 16; ++i)
		seq.push_back({36 + (i * 7) % 36, 60 + (i * 13) % 60, 0.1 + i * 0.25, 0.1 + i * 0.25 + 0.2});
	c.push_back({"seq-48k", 48000, 4.5, seq});
	return c;
}

struct Result {
	std::vector<float> l, r;
	double took = 0, seconds = 0, worstBlock = 0;
};

Result run(const Case &c)
{
	Machine m(g_main.data(), g_voice.data());
	m.prepare(c.rate);
	std::copy(g_factory.begin(), g_factory.end(), m.main().ram().begin());
	boot(m);
	Result r;
	r.seconds = c.seconds;
	const int total = int(c.seconds * c.rate);
	r.l.resize(size_t(total));
	r.r.resize(size_t(total));
	const double blockSeconds = kBlock / c.rate;
	for (int pos = 0; pos < total; pos += kBlock) {
		const int n = std::min(kBlock, total - pos);
		const double t0 = pos / c.rate, t1 = (pos + n) / c.rate;
		for (const auto &e : c.notes) {
			if (e.on >= t0 && e.on < t1) {
				m.main().midiIn(0x90);
				m.main().midiIn(uint8_t(e.note));
				m.main().midiIn(uint8_t(e.vel));
			}
			if (e.off >= t0 && e.off < t1) {
				m.main().midiIn(0x80);
				m.main().midiIn(uint8_t(e.note));
				m.main().midiIn(64);
			}
		}
		const auto a = std::chrono::steady_clock::now();
		m.process(r.l.data() + pos, r.r.data() + pos, n);
		const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
		r.took += dt;
		r.worstBlock = std::max(r.worstBlock, dt / blockSeconds);
	}
	return r;
}

void writeWav(const std::string &path, const Result &r, int rate)
{
	std::ofstream f(path, std::ios::binary);
	auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
	auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };
	const uint32_t frames = uint32_t(r.l.size());
	f.write("RIFF", 4);
	u32(36 + frames * 8);
	f.write("WAVEfmt ", 8);
	u32(16);
	u16(3);
	u16(2);
	u32(uint32_t(rate));
	u32(uint32_t(rate) * 8);
	u16(8);
	u16(32);
	f.write("data", 4);
	u32(frames * 8);
	for (uint32_t i = 0; i < frames; ++i) {
		f.write(reinterpret_cast<const char *>(&r.l[i]), 4);
		f.write(reinterpret_cast<const char *>(&r.r[i]), 4);
	}
}

bool readWav(const std::string &path, std::vector<float> &l, std::vector<float> &r)
{
	const auto d = load(path);
	if (d.size() < 44)
		return false;
	const size_t n = (d.size() - 44) / 8;
	l.resize(n);
	r.resize(n);
	for (size_t i = 0; i < n; ++i) {
		std::memcpy(&l[i], &d[44 + 8 * i], 4);
		std::memcpy(&r[i], &d[44 + 8 * i + 4], 4);
	}
	return true;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "xpanderbench time [N] | golden DIR [CASE] | check DIR [CASE] | list | run NAME\n");
		return 1;
	}
	const std::string dir = XPANDER_ROM_DIR;
	for (const char *n : {"exp1.2-0.u8", "exp1.2-1.u7", "exp1.2-2.u6", "exp1.2-3.u5"}) {
		auto r = load(dir + "/" + n);
		g_main.insert(g_main.end(), r.begin(), r.end());
	}
	for (const char *n : {"ca1.4-0.u912", "ca1.4-1.u914"}) {
		auto r = load(dir + "/" + n);
		g_voice.insert(g_voice.end(), r.begin(), r.end());
	}
	if (g_main.size() != 0x8000 || g_voice.size() != 0x4000) {
		std::fprintf(stderr, "ROMs not found in %s\n", dir.c_str());
		return 1;
	}
	// The factory state: the plugin's cache if it is there, else made here.
	const char *home = std::getenv("HOME");
	const std::string cache = std::string(home ? home : ".") + "/Library/Application Support/Lemus/xpander/" + kFactoryFile;
	g_factory = load(cache);
	if (g_factory.size() != size_t(MainBoard::kRamSize)) {
		std::fprintf(stderr, "making the factory state (reset and TUNE ALL)...\n");
		Machine m(g_main.data(), g_voice.data());
		m.prepare(48000);
		const auto w = warmUp(m, g_main.data());
		std::fprintf(stderr, "%s\n", w.message.c_str());
		g_factory.assign(m.main().ram().begin(), m.main().ram().end());
	}

	const std::string mode = argv[1];
	const auto all = cases();
	if (mode == "list") {
		for (const auto &c : all)
			std::printf("%s\n", c.name);
		return 0;
	}
	if (mode == "run" && argc > 2) {
		for (const auto &c : all)
			if (c.name == std::string(argv[2]))
				run(c);
		return 0;
	}
	if (mode == "time") {
		const int n = argc > 2 ? std::atoi(argv[2]) : 3;
		std::printf("%-14s %8s %10s\n", "workload", "core %", "worst blk");
		for (const auto &c : all) {
			Result best;
			for (int i = 0; i < n; ++i) {
				auto r = run(c);
				if (i == 0 || r.took < best.took)
					best = std::move(r);
			}
			std::printf("%-14s %7.2f%% %9.1f%%\n", c.name, 100 * best.took / best.seconds, 100 * best.worstBlock);
		}
		return 0;
	}
	if ((mode == "golden" || mode == "check") && argc > 2) {
		const std::string out = argv[2];
		const std::string only = argc > 3 ? argv[3] : "";
		int bad = 0;
		for (const auto &c : all) {
			if (!only.empty() && only != c.name)
				continue;
			const auto r = run(c);
			const std::string path = out + "/" + c.name + ".wav";
			if (mode == "golden") {
				writeWav(path, r, int(c.rate));
				std::printf("%-14s written\n", c.name);
				continue;
			}
			std::vector<float> l, rr;
			if (!readWav(path, l, rr) || l.size() != r.l.size()) {
				std::printf("%-14s no golden render\n", c.name);
				++bad;
				continue;
			}
			double worst = 0;
			for (size_t i = 0; i < l.size(); ++i)
				worst = std::max({worst, double(std::fabs(l[i] - r.l[i])), double(std::fabs(rr[i] - r.r[i]))});
			if (worst == 0)
				std::printf("%-14s exact\n", c.name);
			else
				std::printf("%-14s %.1f dB\n", c.name, 20 * std::log10(worst));
		}
		return bad;
	}
	return 1;
}
