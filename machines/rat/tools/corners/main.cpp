// ratcorners: the extremes of the controls and of the input, checked for what the other checks
// can't see. The golden renders, the SPICE suite and the captures stop where real playing does;
// this renders the corners nobody recorded (every knob at its ends, both calibrations and op amps,
// input at full scale and 12 dB over it with the input level at its ends, sweeps, squares, DC steps,
// impulses, noise, silence, knobs jumping, the footswitch and LEVEL switching) through
// rat::Channel as the plugin drives it, at 44.1, 48, 96 and 192 kHz (the pedal at 8x, 8x, 4x and
// 2x), and fails on:
//   - any per-sample Newton solve still unconverged after its retry, or an operating point that
//     didn't converge (rat::Pedal::SolverStats): the pedal then goes on from its last iterate,
//     silently in every other check;
//   - output that isn't finite, or past kMaxVolts at the output jack (bypassed, past the input's
//     own peak by that much).
// It also reports how many solves needed the retry.
//
//   ratcorners [--rate HZ] [--only TEXT] [--list] [--jobs N] [--wav DIR]
//
// --wav writes each case's render to DIR/<rate>-<case>.wav (64-bit float, mono, dBFS), to listen
// to one or to compare two builds sample by sample.
//
// Exit status 0 when every case passes.
#include "rat/core/unit.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace rat;

namespace {

constexpr double kPi = 3.14159265358979323846;
// At the output jack: D2/D3 hold the clip node within about 0.8 V, the JFET follows it and C13 can
// take a swing about as far the other way, so 3 V is past anything the circuit can make (the most
// the corners reach is 1.1 V).
constexpr double kMaxVolts = 3.0;
constexpr int kBlock = 512;

struct Lcg {
	uint32_t s = 0x5eed;
	double next()
	{
		s = s * 1664525u + 1013904223u;
		return double(s >> 8) / double(1u << 24) * 2 - 1;
	}
};

// The input, in dBFS units (1 = full scale), amplitude a.
std::vector<double> signal(const std::string &kind, double rate, double seconds, double a)
{
	std::vector<double> x(size_t(rate * seconds), 0.0);
	Lcg n;
	const double f0 = 20, f1 = std::min(20000.0, 0.45 * rate), k = std::log(f1 / f0) / seconds;
	for (size_t i = 0; i < x.size(); ++i) {
		const double t = double(i) / rate;
		double v = 0;
		if (kind == "sweep") // logarithmic, 20 Hz to 20 kHz
			v = std::sin(2 * kPi * f0 * (std::exp(k * t) - 1) / k);
		else if (kind == "square 60 Hz")
			v = std::fmod(t * 60, 1.0) < 0.5 ? 1 : -1;
		else if (kind == "square 3 kHz")
			v = std::fmod(t * 3000, 1.0) < 0.5 ? 1 : -1;
		else if (kind == "DC steps") { // 0, +, 0, -, + , - ... every 0.1 s
			static const double steps[] = {0, 1, 0, -1, 1, -1, 0, 1, -1, 0};
			v = steps[size_t(t / 0.1) % 10];
		} else if (kind == "impulses") // one sample each 0.05 s, alternating
			v = i % size_t(0.05 * rate) == 0 ? ((i / size_t(0.05 * rate)) % 2 ? -1 : 1) : 0;
		else if (kind == "noise")
			v = n.next();
		x[i] = a * v;
	}
	return x;
}

struct Case {
	std::string name;
	std::string signal = "silence";
	double amp = 0, levelDbu = kDefaultLevelDbu, seconds = 0.5;
	bool captured = true, op07 = false, china = false;
	Controls k;
	// Per block: 1 = the knobs jump between their ends, 2 = the knobs swept, 3 = the footswitch
	// toggled, 4 = LEVEL between its ends.
	int automation = 0;
};

std::vector<Case> cases()
{
	std::vector<Case> c;
	struct Level {
		const char *name;
		double dbu, amp;
	};
	const Level levels[] = {{"0 dBFS at -10 dBu", -10, 1}, {"0 dBFS at +20 dBu", 20, 1}, {"+12 dBFS at +20 dBu", 20, 4}};
	const char *kinds[] = {"sweep", "square 60 Hz", "square 3 kHz", "DC steps", "impulses", "noise"};
	for (int cal = 0; cal < 2; ++cal)
		for (int op = 0; op < 2; ++op) {
			const std::string dev = std::string(cal ? "captured" : "datasheet") + (op ? " OP07" : " LM308");
			for (int knobs = 0; knobs < 8; ++knobs) {
				Controls k;
				k.distortion = knobs & 1;
				k.filter = (knobs >> 1) & 1;
				k.volume = (knobs >> 2) & 1;
				char kn[64];
				std::snprintf(kn, sizeof kn, "dist %g filter %g vol %g", k.distortion * 10, k.filter * 10, k.volume * 10);
				auto add = [&](const std::string &kind, const Level *l) {
					Case x;
					x.signal = kind;
					x.captured = cal;
					x.op07 = op;
					x.k = k;
					if (l) {
						x.amp = l->amp;
						x.levelDbu = l->dbu;
					}
					x.seconds = kind == "sweep" ? 1.0 : 0.5;
					x.name = dev + ", " + kn + ", " + kind + (l ? std::string(", ") + l->name : "");
					c.push_back(x);
				};
				add("silence", nullptr);
				for (const char *kind : kinds)
					for (const auto &l : levels)
						add(kind, &l);
			}
			// Moving controls, at the hottest input.
			auto moving = [&](const char *what, int automation, const char *kind) {
				Case x;
				x.signal = kind;
				x.amp = 4;
				x.levelDbu = 20;
				x.captured = cal;
				x.op07 = op;
				x.k = {1, 1, 1};
				x.automation = automation;
				x.seconds = 1.0;
				x.name = dev + ", " + what + ", " + kind + ", +12 dBFS at +20 dBu";
				c.push_back(x);
			};
			moving("knobs jump 0 / 10 each block", 1, "noise");
			moving("knobs swept", 2, "sweep");
			moving("footswitch every 50 ms", 3, "sweep");
			moving("LEVEL -10 / +20 dBu each 0.1 s", 4, "noise");
		}
	// The China-made parts (the tools' --parts china), at the hottest input.
	for (int knobs : {0, 7})
		for (const char *kind : kinds) {
			Case x;
			x.signal = kind;
			x.amp = 4;
			x.levelDbu = 20;
			x.china = true;
			x.op07 = true;
			x.k = {double(knobs & 1), double((knobs >> 1) & 1), double((knobs >> 2) & 1)};
			x.seconds = std::string(kind) == "sweep" ? 1.0 : 0.5;
			x.name = std::string("china parts OP07, knobs ") + (knobs ? "10" : "0") + ", " + kind + ", +12 dBFS at +20 dBu";
			c.push_back(x);
		}
	return c;
}

struct Result {
	Pedal::SolverStats solver;
	double peakVolts = 0; // over the limit's allowance (the input's own peak when bypassed)
	bool finite = true;
};

// 64-bit float WAV, mono.
void writeWav(const std::string &path, const std::vector<double> &y, double rate)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f) {
		std::fprintf(stderr, "cannot write %s\n", path.c_str());
		return;
	}
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
}

Result run(const Case &c, double rate, const std::string &wavDir)
{
	Channel ch;
	ch.pedal().setCalibration(c.captured ? capturedCalibration() : datasheetCalibration());
	ch.pedal().setOpAmp(c.op07 ? OpAmp::OP07 : OpAmp::LM308);
	if (c.china)
		ch.pedal().setParts(chinaParts());
	ch.pedal().setControls(c.k, true);
	ch.setLevelDbu(c.levelDbu);
	ch.prepare(rate);
	const auto x = signal(c.signal, rate, c.seconds, c.amp);
	std::vector<double> y(x.size());
	Result r;
	for (size_t pos = 0; pos < x.size(); pos += kBlock) {
		const double t = double(pos) / rate, u = t / c.seconds;
		if (c.automation == 1) {
			const double e = (pos / kBlock) % 2 ? 1.0 : 0.0;
			ch.pedal().setControls({e, 1 - e, e});
		} else if (c.automation == 2) {
			ch.pedal().setControls({u, 1 - u, 0.5 + 0.5 * std::sin(2 * kPi * 3 * t)});
		} else if (c.automation == 3) {
			ch.setEngaged(int(t / 0.05) % 2 == 0);
		} else if (c.automation == 4) {
			ch.setLevelDbu(int(t / 0.1) % 2 ? -10.0 : 20.0);
		}
		const size_t end = std::min(x.size(), pos + kBlock);
		for (size_t i = pos; i < end; ++i) {
			y[i] = ch.process(x[i]);
			const double vfs = ch.levelVolts();
			const double allowance = ch.engaged() && c.automation != 3 ? 0.0 : std::fabs(x[i]) * vfs;
			r.finite = r.finite && std::isfinite(y[i]);
			r.peakVolts = std::max(r.peakVolts, std::fabs(y[i]) * vfs - allowance);
		}
	}
	r.solver = ch.pedal().solverStats();
	if (!wavDir.empty()) {
		std::string name = std::to_string(int(rate)) + "-" + c.name + ".wav";
		for (char &q : name)
			if (q == ' ' || q == '/' || q == ',')
				q = '_';
		writeWav(wavDir + "/" + name, y, rate);
	}
	return r;
}

} // namespace

int main(int argc, char **argv)
{
	std::vector<double> rates = {44100, 48000, 96000, 192000};
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
			std::fprintf(stderr, "usage: ratcorners [--rate HZ] [--only TEXT] [--list] [--jobs N] [--wav DIR]\n");
			return 2;
		}
	}
	std::vector<Case> all;
	for (auto &c : cases())
		if (only.empty() || c.name.find(only) != std::string::npos)
			all.push_back(c);
	if (list) {
		for (auto &c : all)
			std::printf("%s\n", c.name.c_str());
		return 0;
	}

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
		Pedal::SolverStats total;
		double peak = 0;
		size_t mostRetried = 0;
		for (size_t i = 0; i < all.size(); ++i) {
			const Result &r = res[i];
			total.solves += r.solver.solves;
			total.retried += r.solver.retried;
			total.failures += r.solver.failures;
			total.dcFailures += r.solver.dcFailures;
			peak = std::max(peak, r.peakVolts);
			if (r.solver.retried > res[mostRetried].solver.retried)
				mostRetried = i;
			std::string why;
			if (r.solver.failures > 0)
				why += " unconverged " + std::to_string(r.solver.failures);
			if (r.solver.dcFailures > 0)
				why += " operating point unconverged";
			if (!r.finite)
				why += " output not finite";
			else if (r.peakVolts > kMaxVolts)
				why += " output " + std::to_string(r.peakVolts) + " V";
			if (!why.empty()) {
				++bad;
				std::printf("  FAIL %6.0f  %-90s%s\n", rate, all[i].name.c_str(), why.c_str());
			}
		}
		std::printf("%6.0f Hz (%dx): %zu cases, %ld solves, %ld retried (%.4f %%), %ld unconverged, %ld operating points "
		            "unconverged; output peak %.2f V (limit %.0f); most retried: %s (%ld)\n",
		            rate, oversamplingFor(rate), all.size(), total.solves, total.retried,
		            total.solves ? 100.0 * double(total.retried) / double(total.solves) : 0.0, total.failures,
		            total.dcFailures, peak, kMaxVolts, all.empty() ? "-" : all[mostRetried].name.c_str(),
		            all.empty() ? 0L : res[mostRetried].solver.retried);
	}
	std::printf("%s: %d failing case(s)\n", bad ? "FAIL" : "PASS", bad);
	return bad ? 1 : 0;
}
