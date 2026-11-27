// qvresponse: the whole unit's frequency response (core/unit.h at the host's rate, the hiss off),
// wet and dry, by stepped sines: what the converters and the analog stages leave of a flat program.
//
//   qvresponse ROM [--rate HZ] [--input 0-10] [--output 0-10] [--level dBFS]
//
// Wet: configuration 2 (GRAPHIC EQ -> DELAY) with every band at 0 dB, the delay's mix at 0 and the
// EQ's at 99 (it has no direct signal). Dry: configuration 0 with the direct signal at 99 and the
// effects at 0 (MAST FX 0). Edits by SysEx (service manual 11.0). Prints each frequency's gain in
// dB, from the input's level to the output's, and the level at 1 kHz.
#include "qv/core/unit.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct Qv {
	qv::Unit u;
	explicit Qv(const std::vector<uint8_t> &rom) : u(rom)
	{
		u.powerOn();
		u.recallFactoryPrograms();
	}
	void press(int k)
	{
		u.board().setKey(k, true);
		u.runSilent(0.25);
		u.board().setKey(k, false);
		u.runSilent(0.3);
	}
	void midi(std::initializer_list<int> bytes)
	{
		for (int x : bytes)
			u.board().midiIn(uint8_t(x));
		u.runSilent(0.05);
	}
	bool sysexOn()
	{
		press(4);
		for (int i = 0; i < 8 && u.board().lcd().text(0).find("SYSEX") == std::string::npos; ++i)
			press(7);
		press(3);
		const bool on = u.board().lcd().text(1).find("ON") != std::string::npos;
		press(11);
		return on;
	}
	void edit(int func, int page, int value)
	{
		const int a = value & 0xff, hi = (value >> 8) & 0xff;
		midi({0xf0, 0x00, 0x00, 0x0e, 0x02, 0x01, func, page, a >> 1, ((a & 1) << 6) | (hi >> 2), (hi & 3) << 5, 0xf7});
		u.runSilent(0.1);
	}
};

// gain (dB) of a sine at f through the unit, from its input to its output (both channels' average)
double gainAt(qv::Unit &u, double rate, double f, double amp)
{
	const int n = int(rate * 0.4), skip = int(rate * 0.25);
	std::vector<float> x(static_cast<size_t>(n)), l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i)
		x[size_t(i)] = float(amp * std::sin(2 * M_PI * f * i / rate));
	u.process(x.data(), x.data(), l.data(), r.data(), n);
	// each output's level at f (a Hann-windowed DFT over the steady part), averaged: the left
	// output runs a DSP sample behind the right (the program reads it at instruction 0, the right at
	// 95), so their sum would comb
	double reL = 0, imL = 0, reR = 0, imR = 0, wsum = 0;
	for (int i = skip; i < n; ++i) {
		const double w = 0.5 - 0.5 * std::cos(2 * M_PI * (i - skip) / (n - skip));
		const double c = std::cos(2 * M_PI * f * i / rate), s = std::sin(2 * M_PI * f * i / rate);
		reL += w * l[size_t(i)] * c;
		imL -= w * l[size_t(i)] * s;
		reR += w * r[size_t(i)] * c;
		imR -= w * r[size_t(i)] * s;
		wsum += w;
	}
	return 20 * std::log10((std::hypot(reL, imL) + std::hypot(reR, imR)) / wsum / amp);
}

// the same through the board alone (the DSP at its rate, the analog stages left out)
double dspGainAt(qv::Unit &u, double f, double amp)
{
	const double rate = qv::kSampleRate;
	const int n = int(rate * 0.3), skip = int(rate * 0.15);
	double reL = 0, imL = 0, reR = 0, imR = 0, wsum = 0;
	for (int i = 0; i < n; ++i) {
		double l, r;
		u.board().runSample(amp * std::sin(2 * M_PI * f * i / rate), 0, l, r);
		if (i < skip)
			continue;
		const double w = 0.5 - 0.5 * std::cos(2 * M_PI * (i - skip) / (n - skip));
		const double c = std::cos(2 * M_PI * f * i / rate), s = std::sin(2 * M_PI * f * i / rate);
		reL += w * l * c;
		imL -= w * l * s;
		reR += w * r * c;
		imR -= w * r * s;
		wsum += w;
	}
	return 20 * std::log10((std::hypot(reL, imL) + std::hypot(reR, imR)) / wsum / amp);
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: qvresponse ROM [--rate HZ] [--input 0-10] [--output 0-10] [--level dBFS]\n");
		return 2;
	}
	double rate = 48000, input = qv::Unit::kInputUnity * 10, output = 10, level = -12;
	for (int i = 2; i + 1 < argc; i += 2) {
		const std::string a = argv[i];
		const double v = std::atof(argv[i + 1]);
		if (a == "--rate")
			rate = v;
		else if (a == "--input")
			input = v;
		else if (a == "--output")
			output = v;
		else if (a == "--level")
			level = v;
	}
	std::ifstream f(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), {});
	if (!qv::Board::prepareRom(rom)) {
		std::fprintf(stderr, "%s: not a QuadraVerb EPROM (v2.03 or v1.07)\n", argv[1]);
		return 1;
	}
	const double freqs[] = {20, 30, 50, 100, 200, 500, 1000, 2000, 5000, 8000, 10000, 12000, 14000, 16000, 17000, 18000, 19000, 20000};
	const double amp = std::pow(10.0, level / 20);
	for (const char *path : {"wet", "eq5", "dry"}) {
		Qv q(rom);
		q.sysexOn();
		q.midi({0xc0, 0});
		q.u.runSilent(0.4);
		if (std::strcmp(path, "wet") == 0) {
			q.edit(7, 0, 2); // GRAPHIC EQ -> DELAY
			q.u.runSilent(0.5);
			for (int b = 0; b < 11; ++b) {
				q.edit(4, b, 14); // every band at 0 dB (0-28)
			}
			q.edit(8, 0, 99); // MAST FX
			q.edit(8, 1, 99); // EQ
			q.edit(8, 2, 0);  // DELAY
		} else if (std::strcmp(path, "eq5") == 0) {
			q.edit(7, 0, 3); // 5 BAND EQ -> PITCH -> DELAY
			q.u.runSilent(0.5);
			for (int b : {1, 4, 7, 10, 12})
				q.edit(4, b, 280); // every band's boost/cut at 0 dB (0-560)
			q.edit(8, 0, 1);  // POST-EQ
			q.edit(8, 1, 99); // MAST FX
			q.edit(8, 2, 99); // EQ
			q.edit(8, 3, 0);  // PITCH
			q.edit(8, 4, 0);  // DELAY
		} else {
			q.edit(8, 0, 0); // PRE-EQ mix
			q.edit(8, 1, 99); // DIRECT
			q.edit(8, 2, 0);  // MAST FX
		}
		q.u.setHiss(false);
		q.u.setIdleSleep(false);
		q.u.inputPot = input / 10;
		q.u.outputPot = output / 10;
		q.u.prepare(rate);
		std::printf("%s (%s |%s|):", path, q.u.board().lcd().text(0).c_str(), q.u.board().lcd().text(1).c_str());
		double ref = 0;
		std::vector<double> g;
		for (const double fr : freqs) {
			g.push_back(gainAt(q.u, rate, fr, amp));
			if (fr == 1000)
				ref = g.back();
		}
		std::printf(" %.2f dB at 1 kHz\n", ref);
		// the board alone, for the wet path: what the DSP's program does by itself
		std::vector<double> d;
		double dref = 0;
		if (std::strcmp(path, "dry") != 0)
			for (const double fr : freqs) {
				d.push_back(fr < qv::kSampleRate / 2 ? dspGainAt(q.u, fr, 0.25) : 0.0);
				if (fr == 1000)
					dref = d.back();
			}
		for (size_t k = 0; k < g.size(); ++k) {
			std::printf("  %6.0f Hz %+7.2f dB", freqs[k], g[k] - ref);
			if (!d.empty())
				std::printf("   (the DSP alone %+7.2f dB, the analog %+7.2f dB)", d[k] - dref, g[k] - ref - (d[k] - dref));
			std::printf("\n");
		}
	}
	return 0;
}
