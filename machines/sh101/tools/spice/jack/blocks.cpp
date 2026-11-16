// sh101jackblocks: jack board A's mute (core/jack_board.*) on its own, for compare.py. A sine of
// AMP volts peak from IC1a at HZ (through C2 and the jack board with TR7 off, as Ba662Vca gives
// it); prints, for each time given, the OUTPUT jack's highest and lowest voltage over the period
// that starts there.
//
//   sh101jackblocks [--fs HZ] [--load OHMS] on AMP HZ T1 [T2 ...]       switched on at t = 0
//   sh101jackblocks [--fs HZ] [--load OHMS] settled AMP HZ T1 [T2 ...]  switched on long before
//   sh101jackblocks [--fs HZ] [--load OHMS] off AMP HZ TOFF TSTOP       switched off at TOFF: prints
//       "t volts" every 20 us from TOFF - 10 ms, IC1a's output under the falling supply as
//       Ba662Vca has it (C2's high-pass included)
//   sh101jackblocks [--fs HZ] bench SECONDS                             ns per sample, switched on at 0
#include "sh101/core/jack_board.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace sh101;

int main(int argc, char **argv)
{
	constexpr double kPi = 3.14159265358979323846;
	double fs = 176400, load = 47e3;
	int a = 1;
	for (; a + 1 < argc && std::strncmp(argv[a], "--", 2) == 0; a += 2) {
		if (std::strcmp(argv[a], "--fs") == 0)
			fs = std::atof(argv[a + 1]);
		else if (std::strcmp(argv[a], "--load") == 0)
			load = std::atof(argv[a + 1]);
	}
	if (argc - a < 2) {
		std::fprintf(stderr, "usage: sh101jackblocks [--fs HZ] [--load OHMS] on|settled AMP HZ T... | bench SECONDS\n");
		return 1;
	}
	const std::string cmd = argv[a];
	JackBoard jb;
	jb.setLoad(load);
	jb.prepare(fs);
	// The jack's voltage with TR7 off for IC1a's sine: R29, R28 into the load (C2 passes it).
	const double kj = load / (2.2e3 + 1e3 + load);
	if (cmd == "bench") {
		const double seconds = std::atof(argv[a + 1]);
		jb.powerOn();
		const long n = long(seconds * fs);
		double sum = 0;
		const auto t0 = std::chrono::steady_clock::now();
		for (long i = 0; i < n; ++i)
			sum += jb.tick(kj * std::sin(2 * kPi * 110 * double(i) / fs));
		const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
		std::printf("%.1f ns/sample (%g)\n", ns / double(n), sum);
		return 0;
	}
	if (cmd == "off" && argc - a >= 5) {
		const double amp = std::atof(argv[a + 1]), hz = std::atof(argv[a + 2]);
		const double toff = std::atof(argv[a + 3]), tstop = std::atof(argv[a + 4]);
		jb.settle();
		// Ba662Vca's output stage: IC1a against the bus, C2's high-pass, the divider.
		const double rHp = 100e3 * (2.2e3 + 1e3 + load) / (100e3 + 2.2e3 + 1e3 + load);
		const double wHp = std::tan(1 / (2 * 2.2e-6 * rHp * fs));
		const double hpA = (1 - wHp) / (1 + wHp);
		double hpX = 0, hpY = 0, nextT = toff - 0.01;
		const long n = long(tstop * fs);
		for (long i = 0; i < n; ++i) {
			const double t = double(i) / fs;
			if (t >= toff && !jb.poweringDown() && t < toff + 1 / fs)
				jb.powerOff();
			const double v9 = jb.poweringDown() ? jb.railVolts() : 9.0;
			double w = amp * std::sin(2 * kPi * hz * t);
			if (v9 < 9)
				w = std::max(std::min(v9 > 6.9 ? w : 0.0, v9 - 6.2), -5.0);
			hpY = hpA * hpY + (1 + hpA) / 2 * (w - hpX);
			hpX = w;
			const double j = jb.tick(hpY * kj);
			if (t >= nextT - 1e-12) {
				std::printf("%.6f %.7f\n", t, j);
				nextT += 20e-6;
			}
		}
		return 0;
	}
	if (argc - a < 4)
		return 1;
	const double amp = std::atof(argv[a + 1]), hz = std::atof(argv[a + 2]);
	std::vector<double> times;
	for (int i = a + 3; i < argc; ++i)
		times.push_back(std::atof(argv[i]));
	if (cmd == "on")
		jb.powerOn();
	else
		jb.settle();
	const double end = times.empty() ? 0 : times.back() + 1 / hz;
	const long n = long(end * fs) + 1;
	std::vector<double> hi(times.size(), -1e9), lo(times.size(), 1e9);
	for (long i = 0; i < n; ++i) {
		const double t = double(i) / fs;
		const double j = jb.tick(kj * amp * std::sin(2 * kPi * hz * t));
		for (size_t k = 0; k < times.size(); ++k)
			if (t >= times[k] && t < times[k] + 1 / hz) {
				hi[k] = std::max(hi[k], j);
				lo[k] = std::min(lo[k], j);
			}
	}
	for (size_t k = 0; k < times.size(); ++k)
		std::printf("%g %.6f %.6f\n", times[k], hi[k], lo[k]);
	return 0;
}
