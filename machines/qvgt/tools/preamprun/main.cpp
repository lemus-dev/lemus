// preamprun: the GT's analog preamp (core/preamp.h) on its own, with the latches set by hand.
//
//   preamprun [--u10 HEX] [--u11 HEX] [--gate HEX] [--pot 0-1] [--freq HZ] [--seconds S]
//             [--levels dBV,dBV,...] [--csv FILE]
//
// For each level (a sine's RMS at both inputs, in dBV), runs the preamp for --seconds and prints the
// last half second's RMS at U2 pin 8 and at the A/D's input (U13 pin 8), the VCAs' gains and the
// control outputs at the end, and the time it took against real time. --csv writes every step of the
// last level (t, in, p8, out, conout1, conout2).
#include "qvgt/core/preamp.h"
#include "qvgt/core/board.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
	unsigned u10 = 0x02, u11 = 0x01, gate = 0x1f; // flat, distortion off, compression off, gate off
	double pot = 1, freq = 220, seconds = 1;
	std::vector<double> levels{-40, -20, -10, 0};
	const char *csv = nullptr;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		auto next = [&]() -> const char * {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "%s needs a value\n", a.c_str());
				std::exit(2);
			}
			return argv[++i];
		};
		if (a == "--u10")
			u10 = unsigned(std::strtoul(next(), nullptr, 16));
		else if (a == "--u11")
			u11 = unsigned(std::strtoul(next(), nullptr, 16));
		else if (a == "--gate")
			gate = unsigned(std::strtoul(next(), nullptr, 16));
		else if (a == "--pot")
			pot = std::atof(next());
		else if (a == "--freq")
			freq = std::atof(next());
		else if (a == "--seconds")
			seconds = std::atof(next());
		else if (a == "--levels") {
			levels.clear();
			std::stringstream s(next());
			std::string v;
			while (std::getline(s, v, ','))
				levels.push_back(std::atof(v.c_str()));
		} else if (a == "--csv")
			csv = next();
		else {
			std::fprintf(stderr, "unknown option %s\n", a.c_str());
			return 2;
		}
	}

	const double rate = qvgt::kSampleRate * qvgt::kPreampOver;
	qvgt::Preamp p;
	p.setControls(uint8_t(u10), uint8_t(u11), uint8_t(gate), pot);
	p.prepare(rate);
	std::printf("U10 %02X  U11 %02X  0400h %02X  pot %.2f  %.0f Hz at %.0f Hz\n", u10, u11, gate, pot, freq, rate);
	std::printf("%8s %9s %9s %8s %8s %8s %8s %7s\n", "in dBV", "p8 dBV", "out dBV", "VCA1 dB", "VCA2 dB", "conout1", "conout2", "x real");
	FILE *f = nullptr;
	for (size_t li = 0; li < levels.size(); ++li) {
		const double amp = std::pow(10.0, levels[li] / 20) * std::sqrt(2.0);
		const int n = int(seconds * rate), from = std::max(0, n - int(0.5 * rate));
		if (csv && li + 1 == levels.size()) {
			f = std::fopen(csv, "w");
			if (f)
				std::fprintf(f, "t,in,p8,out,conout1,conout2\n");
		}
		double s8 = 0, so = 0;
		const auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < n; ++i) {
			const double x = amp * std::sin(2 * M_PI * freq * i / rate);
			const auto o = p.step(x, x);
			const double p8v = p.p8();
			if (i >= from) {
				s8 += p8v * p8v;
				so += o.toAdc * o.toAdc;
			}
			if (f)
				std::fprintf(f, "%.7f,%.6f,%.6f,%.6f,%.5f,%.5f\n", i / rate, x, p8v, o.toAdc, p.conout1(), p.conout2());
		}
		const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		const int m = n - from;
		std::printf("%8.1f %9.2f %9.2f %8.2f %8.2f %8.3f %8.3f %7.2f\n", levels[li], 10 * std::log10(s8 / m + 1e-30),
		            10 * std::log10(so / m + 1e-30), p.vca1Db(), p.vca2Db(), p.conout1(), p.conout2(), seconds / took);
		for (int c = 0; c < 4; ++c)
			for (int s = 0; s < p.chain(c).stages(); ++s) {
				auto &k = p.chain(c).stage(s);
				const auto &st = k.stats();
				std::printf("    %-18s %2d states %2d ports: %.2f it/step, %ld substepped, %ld failed, %ld factorizations, %ld linear\n",
				            k.name().c_str(), k.stateCount(), k.portCount(), st.steps ? double(st.iterations) / st.steps : 0.0,
				            st.substepped, st.failures, st.factorizations, st.linearSteps);
				k.resetStats();
			}
		std::fflush(stdout);
	}
	if (f)
		std::fclose(f);
	return 0;
}
