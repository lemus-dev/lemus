// qvgtbench: the workloads every round measures, their cost and golden renders.
//
//   qvgtbench ROM time [N]      cost per workload: share of one core at its rate, the worst 512-sample
//                               block against its own duration, and time asleep; best of N runs
//   qvgtbench ROM golden DIR [CASE]  render every workload (or CASE) into DIR (stereo float 32
//                               WAV); QVGT_HISS=0 renders them without the modelled hiss, and
//                               QVGT_STATS prints each preamp circuit's linear-mode use, QVGT_TAP=FILE
//                               writes every preamp circuit's inputs and outputs each step
//                               (Preamp::Probe), QVGT_WHOLE runs the shaper as one circuit,
//                               QVGT_PREROLL=S puts S seconds of silence before the input
//   qvgtbench ROM check DIR [CASE]  render again and compare: the largest difference per workload in dB
//                               re full scale ("exact" when bit-identical)
//   qvgtbench ROM list
//   qvgtbench ROM run NAME      one workload only (none: the boot alone), for /usr/bin/time -l's
//                               instruction count: tools/bench/mins.sh gives millions per audio second
//
// Each workload is the unit as the plugin runs it (core/unit.h): booted with the factory programs,
// a program by MIDI, the INPUT and OUTPUT pots, then a deterministic input in 512-sample blocks.
#include "qvgt/core/unit.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kBlock = 512;

struct Lcg {
	uint32_t s = 12345;
	double next()
	{
		s = s * 1664525u + 1013904223u;
		return double(s >> 8) / double(1u << 24) * 2 - 1;
	}
};

// Plucked strings (Karplus-Strong), one note every `every` seconds, peak `peakDb` dBFS.
std::vector<float> plucks(double rate, double seconds, double every, double peakDb, const std::vector<double> &notes, int voices = 1)
{
	Lcg rng;
	std::vector<double> out(size_t(seconds * rate), 0.0);
	for (size_t k = 0; k * every < seconds - 0.2; ++k)
		for (int v = 0; v < voices; ++v) {
			const double f = notes[(k * size_t(voices) + size_t(v)) % notes.size()];
			const size_t n = size_t(rate / f);
			std::vector<double> buf(n);
			for (auto &b : buf)
				b = rng.next();
			const size_t start = size_t(double(k) * every * rate) + size_t(v) * size_t(0.012 * rate);
			for (size_t i = 0; start + i < out.size() && i < size_t(1.5 * rate); ++i) {
				const size_t j = i % n;
				out[start + i] += buf[j];
				buf[j] = 0.996 * 0.5 * (buf[j] + buf[(j + 1) % n]);
			}
		}
	double peak = 1e-9;
	for (const double x : out)
		peak = std::max(peak, std::abs(x));
	const double g = std::pow(10.0, peakDb / 20) / peak;
	std::vector<float> f(out.size());
	for (size_t i = 0; i < out.size(); ++i)
		f[i] = float(out[i] * g);
	return f;
}

std::vector<float> sweep(double rate, double seconds, double f0, double f1, double db)
{
	std::vector<float> out(size_t(seconds * rate));
	const double a = std::pow(10.0, db / 20), k = std::log(f1 / f0) / seconds;
	for (size_t i = 0; i < out.size(); ++i) {
		const double t = double(i) / rate;
		out[i] = float(a * std::sin(2 * kPi * f0 * (std::exp(k * t) - 1) / k));
	}
	return out;
}

struct Case {
	const char *name;
	double rate;
	int program;
	double input, output; // the pots, 0-10
	std::function<std::vector<float>(double)> signal;
	double tail;  // seconds of silence after it
	int pressKey; // a panel key pressed at the start (-1: none)
};

const std::vector<double> kRiff = {82.41, 110.0, 146.83, 196.0, 110.0, 82.41, 98.0, 123.47};

std::vector<Case> cases()
{
	return {
		{"p00-pluck-48k", 48000, 0, 6, 5, [](double r) { return plucks(r, 4, 0.5, -14, kRiff); }, 2, -1},
		{"p05-pluck-48k", 48000, 5, 6, 5, [](double r) { return plucks(r, 4, 0.5, -14, kRiff); }, 2, -1},
		{"p00-strum-hot-48k", 48000, 0, 8, 5, [](double r) { return plucks(r, 4, 1.0, -3, kRiff, 4); }, 1, -1},
		{"p12-strum-44k", 44100, 12, 6, 5, [](double r) { return plucks(r, 4, 1.0, -10, kRiff, 4); }, 2, -1},
		{"p30-sweep-48k", 48000, 30, 6, 5, [](double r) { return sweep(r, 4, 40, 16000, -24); }, 1, -1},
		{"bypass-pluck-48k", 48000, 0, 6, 5, [](double r) { return plucks(r, 3, 0.5, -14, kRiff); }, 1, 5},
		{"idle-48k", 48000, 0, 6, 5, [](double r) { return std::vector<float>(size_t(r * 1), 0.0f); }, 4, -1},
	};
}

std::vector<uint8_t> g_rom, g_ram;

struct Result {
	std::vector<float> l, r;
	double seconds = 0, took = 0, worstBlock = 0, worstAt = 0, asleep = 0;
};

Result run(const Case &c)
{
	qvgt::Unit u(g_rom);
	std::copy(g_ram.begin(), g_ram.end(), u.board().ram().begin());
	if (const char *h = std::getenv("QVGT_HISS"))
		u.setHiss(std::atoi(h) != 0);
	u.powerOn();
	u.board().midiIn(0xc0);
	u.board().midiIn(uint8_t(c.program));
	u.runSilent(0.5);
	if (c.pressKey >= 0) {
		u.board().setKey(c.pressKey, true);
		u.runSilent(0.3);
		u.board().setKey(c.pressKey, false);
		u.runSilent(0.5);
	}
	u.inputPot = c.input / 10;
	u.outputPot = c.output / 10;
	u.prepare(c.rate);
	// QVGT_TAP=FILE: the preamp's every step, its circuits' inputs and outputs (Preamp::Probe) as
	// doubles
	std::FILE *tap = nullptr;
	if (const char *t = std::getenv("QVGT_TAP")) {
		tap = std::fopen(t, "wb");
		u.preamp().setTap([tap](const qvgt::Preamp &p) { std::fwrite(&p.probe(), sizeof(qvgt::Preamp::Probe), 1, tap); });
	}
	auto in = c.signal(c.rate);
	if (const char *p = std::getenv("QVGT_PREROLL")) // seconds of silence first (tools)
		in.insert(in.begin(), size_t(std::atof(p) * c.rate), 0.0f);
	const size_t n = in.size() + size_t(c.tail * c.rate);
	Result res;
	res.l.resize(n);
	res.r.resize(n);
	std::vector<float> bi(kBlock);
	for (size_t at = 0; at < n; at += kBlock) {
		const int len = int(std::min<size_t>(kBlock, n - at));
		for (int i = 0; i < len; ++i)
			bi[size_t(i)] = at + size_t(i) < in.size() ? in[at + size_t(i)] : 0.0f;
		const auto t0 = std::chrono::steady_clock::now();
		u.process(bi.data(), bi.data(), &res.l[at], &res.r[at], len);
		const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		res.took += dt;
		if (dt / (len / c.rate) > res.worstBlock) {
			res.worstBlock = dt / (len / c.rate);
			res.worstAt = double(at) / c.rate;
		}
		if (u.asleep())
			res.asleep += len / c.rate;
	}
	res.seconds = double(n) / c.rate;
	if (tap) {
		u.preamp().setTap(nullptr);
		std::fclose(tap);
	}
	if (std::getenv("QVGT_STATS")) {
		std::fprintf(stderr, "latches: U10 %02x U11 %02x 0400 %02x; INPUT pot %g\n", u.board().latch(qvgt::Board::kLatchU10), u.board().latch(qvgt::Board::kLatchU11), u.board().latch(qvgt::Board::kLatch0400), u.preamp().inputPot());
		for (int ch = 0; ch < 4; ++ch) { // each circuit's controls as its first partition has them
			auto &st = u.preamp().chain(ch).stage(0);
			std::fprintf(stderr, "%s controls:", u.preamp().chain(ch).name().c_str());
			for (size_t c = 0; c < st.netlist().controls.size(); ++c)
				std::fprintf(stderr, " %s=%g", st.netlist().controls[c].c_str(), st.controlValue(int(c)));
			std::fprintf(stderr, "\n");
		}
		for (int ch = 0; ch < 4; ++ch)
			for (int k = 0; k < u.preamp().chain(ch).stages(); ++k) {
				const auto &st = u.preamp().chain(ch).stage(k).stats();
				std::fprintf(stderr, "chain %d stage %d: steps %ld linear %ld (%.1f%%) linearizations %ld iterations %ld substepped %ld failures %ld softened %ld worst residual %.3g\n", ch, k, st.steps, st.linearSteps, 100.0 * st.linearSteps / std::max(1L, st.steps), st.linearizations, st.iterations, st.substepped, st.failures, st.softened, st.worstResidual);
			}
	}
	return res;
}

void writeWav(const std::string &path, const Result &r, double rate)
{
	std::ofstream f(path, std::ios::binary);
	auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
	auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };
	const uint32_t bytes = uint32_t(r.l.size() * 8);
	f.write("RIFF", 4);
	w32(36 + bytes);
	f.write("WAVEfmt ", 8);
	w32(16);
	w16(3);
	w16(2);
	w32(uint32_t(rate));
	w32(uint32_t(rate) * 8);
	w16(8);
	w16(32);
	f.write("data", 4);
	w32(bytes);
	for (size_t i = 0; i < r.l.size(); ++i) {
		f.write(reinterpret_cast<const char *>(&r.l[i]), 4);
		f.write(reinterpret_cast<const char *>(&r.r[i]), 4);
	}
}

bool readWav(const std::string &path, std::vector<float> &l, std::vector<float> &r)
{
	std::ifstream f(path, std::ios::binary);
	std::vector<char> d((std::istreambuf_iterator<char>(f)), {});
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
	if (argc < 3) {
		std::fprintf(stderr, "usage: qvgtbench ROM time [N] | golden DIR [CASE] | check DIR [CASE] | list\n");
		return 2;
	}
	const std::string mode = argv[2];
	if (mode == "list") {
		for (const auto &c : cases())
			std::printf("%s\n", c.name);
		return 0;
	}
	std::ifstream rf(argv[1], std::ios::binary);
	g_rom.assign(std::istreambuf_iterator<char>(rf), {});
	if (!qvgt::Board::prepareRom(g_rom)) {
		std::fprintf(stderr, "%s: not the GT's EPROM\n", argv[1]);
		return 1;
	}
	{
		qvgt::Unit u(g_rom);
		u.powerOn();
		u.recallFactoryPrograms();
		g_ram.assign(u.board().ram().begin(), u.board().ram().end());
	}

	if (mode == "time") {
		const int runs = argc > 3 ? std::max(1, std::atoi(argv[3])) : 3;
		std::printf("%-20s %8s %10s %8s %8s\n", "workload", "CPU %", "worst blk", "at", "asleep");
		for (const auto &c : cases()) {
			Result best;
			for (int k = 0; k < runs; ++k) {
				auto r = run(c);
				if (k == 0 || r.took < best.took)
					best = std::move(r);
			}
			std::printf("%-20s %7.1f%% %9.1f%% %7.2fs %7.1fs\n", c.name, 100 * best.took / best.seconds, 100 * best.worstBlock, best.worstAt, best.asleep);
		}
		return 0;
	}
	if (mode == "run" && argc > 3) {
		for (const auto &c : cases())
			if (c.name == std::string(argv[3])) {
				const auto r = run(c);
				std::printf("%s %.3f s of audio\n", c.name, r.seconds);
			}
		return 0;
	}
	if ((mode == "golden" || mode == "check") && argc > 3) {
		const std::string dir = argv[3];
		int failed = 0;
		for (const auto &c : cases()) {
			if (argc > 4 && c.name != std::string(argv[4]))
				continue;
			const auto r = run(c);
			const std::string path = dir + "/" + c.name + ".wav";
			if (mode == "golden") {
				writeWav(path, r, c.rate);
				std::printf("%-20s %zu samples\n", c.name, r.l.size());
				continue;
			}
			std::vector<float> l, rr;
			if (!readWav(path, l, rr) || l.size() != r.l.size()) {
				std::printf("%-20s MISSING or a different length\n", c.name);
				++failed;
				continue;
			}
			double worst = 0;
			for (size_t i = 0; i < l.size(); ++i)
				worst = std::max({worst, double(std::abs(l[i] - r.l[i])), double(std::abs(rr[i] - r.r[i]))});
			if (worst == 0)
				std::printf("%-20s exact\n", c.name);
			else
				std::printf("%-20s %.1f dB\n", c.name, 20 * std::log10(worst));
		}
		return failed ? 1 : 0;
	}
	std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
	return 2;
}
