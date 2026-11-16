// qvgtdspcheck: the GT's own DSP configurations against what they must compute, through the
// firmware (keys and SysEx edits) and the board at the DSP's rate, the analog stages left out.
//
//   qvgtdspcheck ROM
//
// - Ring modulator (configuration 6, program 51): a frequency shifter. A 1 kHz sine moves up by
//   the shift, the other sideband and the carrier well under it; the shift is 0.71 of the display
//   (the CPU oscillator's tick rate, docs/firmware.md "Timer 0").
// - Resonators (configuration 7, program 80): five combs; an impulse rings at pitches in the
//   semitone ratios they are tuned to (within the integer delays' rounding).
// - Sampling (configuration 8): a recording loops back at its pitch; MIDI notes transpose it.
// - The audio trigger fires at the level detector's -18 dB (re the clip LED's 5 V).
#include "qvgt/core/unit.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
void check(bool ok, const std::string &what)
{
	std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok)
		++g_failures;
}

constexpr double kFs = qvgt::kSampleRate;

struct Gt {
	std::unique_ptr<qvgt::Unit> u;
	explicit Gt(const std::vector<uint8_t> &rom)
	{
		u = std::make_unique<qvgt::Unit>(rom);
		u->powerOn();
		u->recallFactoryPrograms();
		b().setAdcServo(false);
	}
	qvgt::Board &b() { return u->board(); }
	void press(int k, double hold = 0.25)
	{
		b().setKey(k, true);
		u->runSilent(hold);
		b().setKey(k, false);
		u->runSilent(0.3);
	}
	void midi(std::initializer_list<int> bytes)
	{
		for (int x : bytes)
			b().midiIn(uint8_t(x));
		u->runSilent(0.05);
	}
	void program(int p)
	{
		midi({0xc0, p});
		u->runSilent(0.4);
	}
	bool sysexOn()
	{
		press(4);
		for (int i = 0; i < 8 && b().lcd().text(0).find("SYSEX") == std::string::npos; ++i)
			press(7);
		press(3);
		const bool on = b().lcd().text(1).find("ON") != std::string::npos;
		press(11);
		return on;
	}
	// MIDI Editing (service manual, GT MIDI implementation): function, page, value
	void edit(int func, int page, int value)
	{
		const int a = value & 0xff, hi = (value >> 8) & 0xff;
		midi({0xf0, 0x00, 0x00, 0x0e, 0x07, 0x01, func, page, a >> 1, ((a & 1) << 6) | (hi >> 2), (hi & 3) << 5, 0xf7});
		u->runSilent(0.1);
	}
	std::string line(int n) { return b().lcd().text(n); }
	void run(const std::vector<double> &x, std::vector<double> &l, double envelope = 0)
	{
		l.resize(x.size());
		for (size_t i = 0; i < x.size(); ++i) {
			double r;
			b().runSample(x[i], envelope, l[i], r);
		}
	}
};

// a sine's level (dBFS) at f, Hann-windowed, from `from`
double toneDb(const std::vector<double> &x, double f, size_t from = 0)
{
	std::complex<double> acc = 0;
	const size_t n = x.size() - from;
	for (size_t i = 0; i < n; ++i) {
		const double w = 0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n));
		acc += w * x[from + i] * std::polar(1.0, -2 * M_PI * f * double(i) / kFs);
	}
	return 20 * std::log10(std::abs(acc) / (double(n) / 4) + 1e-30);
}

// the strongest frequency between lo and hi (Hz), by a fine scan
double peakHz(const std::vector<double> &x, double lo, double hi, size_t from = 0)
{
	double best = lo, bestDb = -1e9;
	for (double f = lo; f <= hi; f += 0.5) {
		const double d = toneDb(x, f, from);
		if (d > bestDb) {
			bestDb = d;
			best = f;
		}
	}
	return best;
}

// the zero-crossing frequency of x over [from, from + len)
double zcHz(const std::vector<double> &x, size_t from, size_t len)
{
	int zc = 0;
	for (size_t i = from + 1; i < from + len && i < x.size(); ++i)
		if ((x[i - 1] < 0) != (x[i] < 0))
			++zc;
	return zc / 2.0 / (double(len) / kFs);
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: qvgtdspcheck ROM\n");
		return 2;
	}
	std::ifstream f(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), {});
	if (!qvgt::Board::prepareRom(rom)) {
		std::fprintf(stderr, "%s: not the GT's EPROM\n", argv[1]);
		return 1;
	}

	std::printf("Ring modulator (configuration 6)\n");
	{
		Gt g(rom);
		check(g.sysexOn(), "SYSEX ENABLE on");
		g.program(51);
		g.edit(3, 0, 100); // SPECTRUM SHIFT 100 Hz
		g.edit(3, 1, 198); // the up-shifted signal only
		g.edit(8, 0, 0);
		g.edit(8, 1, 99);
		g.edit(8, 2, 0);
		g.edit(8, 3, 99);
		g.edit(8, 4, 0);
		g.edit(8, 5, 0);
		std::vector<double> x(size_t(2 * kFs)), l;
		for (size_t i = 0; i < x.size(); ++i)
			x[i] = 0.25 * std::sin(2 * M_PI * 1000.0 * double(i) / kFs);
		g.run(x, l);
		const size_t from = size_t(kFs);
		const double up = peakHz(l, 1040, 1160, from);
		const double upDb = toneDb(l, up, from), downDb = toneDb(l, 2000 - up, from), carrierDb = toneDb(l, 1000, from);
		std::printf("    1 kHz in: %.1f Hz at %.1f dBFS; the other side %.1f, the carrier %.1f\n", up, upDb, downDb, carrierDb);
		check(std::abs(up - 1071) < 3, "the shift is 71 Hz for the display's 100 (0.71: the tick rate)");
		check(upDb - downDb > 25 && upDb - carrierDb > 25, "a single sideband: the other side and the carrier 25 dB under it");
	}

	std::printf("Resonators (configuration 7)\n");
	{
		Gt g(rom);
		g.sysexOn();
		g.program(80);
		const int semis[5] = {0, 12, 7, -12, 24};
		for (int i = 0; i < 5; ++i)
			g.edit(4, 2 + i, semis[i] + 24);
		g.edit(4, 1, 99);
		g.edit(8, 0, 0);
		g.edit(8, 1, 99);
		g.edit(8, 2, 0);
		g.edit(8, 3, 99);
		g.edit(8, 4, 0);
		g.edit(8, 5, 0);
		// one resonator at a time: the others an octave apart would mask the ratios
		double base = 0;
		bool inTune = true;
		for (int i = 0; i < 5; ++i) {
			for (int j = 0; j < 5; ++j)
				g.edit(4, 2 + j, j == i ? semis[i] + 24 : 60);
			std::vector<double> x(size_t(0.6 * kFs), 0.0), l;
			x[100] = 0.9;
			g.run(x, l);
			const double expect = 523.25 * std::pow(2.0, semis[i] / 12.0);
			const double got = peakHz(l, expect * 0.9, expect * 1.1, 150);
			if (i == 0)
				base = got;
			const double cents = 1200 * std::log2(got / (base * std::pow(2.0, semis[i] / 12.0)));
			std::printf("    resonator %d at %+d semitones: %.1f Hz (%+.0f cents from the semitone ratio)\n", i + 1, semis[i], got, cents);
			inTune = inTune && std::abs(cents) < 45;
		}
		check(inTune, "each rings at its semitone ratio within the integer delays' rounding (45 cents)");
	}

	std::printf("Sampling (configuration 8)\n");
	{
		Gt g(rom);
		g.sysexOn();
		g.program(0);
		g.edit(7, 0, 7);
		g.u->runSilent(0.5);
		g.edit(8, 0, 0);
		g.edit(8, 1, 0);
		g.edit(8, 2, 99);
		g.edit(2, 3, 0); // audio trigger off: BYPASS records
		g.edit(2, 0, 0); // looping
		g.press(12);
		g.press(5, 0.1);
		check(g.line(0).find("SAMPLING") != std::string::npos, "BYPASS starts the recording");
		std::vector<double> x(size_t(2.0 * kFs), 0.0), l;
		for (size_t i = 0; i < x.size(); ++i) {
			const double t = double(i) / kFs;
			x[i] = t < 0.4 ? 0.3 * std::sin(2 * M_PI * 440 * t) : t < 0.8 ? 0.3 * std::sin(2 * M_PI * 880 * t) : 0.0;
		}
		g.run(x, l);
		std::vector<double> q(size_t(3.2 * kFs), 0.0), p;
		g.run(q, p);
		// the loop's period from the 440 Hz part's onsets, and its pitches
		std::vector<double> onsets;
		bool on = false;
		for (size_t s = 0; s + 400 < p.size(); s += 200) {
			double e = 0;
			for (size_t i = s; i < s + 400; ++i)
				e += p[i] * p[i];
			const bool loud = e / 400 > 1e-3;
			if (loud && !on && zcHz(p, s, 2000) < 600)
				onsets.push_back(double(s) / kFs);
			on = loud;
		}
		const double period = onsets.size() >= 2 ? onsets[1] - onsets[0] : 0;
		double lowHz = 0, highHz = 0;
		if (!onsets.empty()) {
			const size_t at = size_t(onsets[0] * kFs);
			lowHz = zcHz(p, at + size_t(0.1 * kFs), size_t(0.2 * kFs));
			highHz = zcHz(p, at + size_t(0.5 * kFs), size_t(0.2 * kFs));
		}
		std::printf("    loop every %.3f s; %.0f Hz then %.0f Hz\n", period, lowHz, highHz);
		check(std::abs(period - 65536 / kFs) < 0.02, "it loops over the whole memory (1.573 s)");
		check(std::abs(lowHz - 440) < 10 && std::abs(highHz - 880) < 15, "at the recorded pitches");
		// MIDI: base note 60, an octave and a fifth up
		g.b().setKey(0, true);
		g.u->runSilent(0.05);
		g.b().setKey(0, false);
		g.u->runSilent(0.3);
		g.edit(2, 0, 1); // one shot
		g.edit(2, 4, 1);
		g.edit(2, 6, 60);
		g.edit(2, 5, 0);
		g.edit(2, 7, 127);
		bool transposes = true;
		for (const auto &[note, ratio] : {std::pair<int, double>{72, 2.0}, {67, 1.4983}}) {
			g.midi({0x90, note, 100});
			std::vector<double> z(size_t(0.6 * kFs), 0.0), pl;
			g.run(z, pl);
			g.midi({0x80, note, 0});
			// from where it starts (the recording begins with a little silence), inside the 440 Hz part
			size_t start = 0;
			while (start < pl.size() && std::abs(pl[start]) < 1e-3)
				++start;
			const double hz = zcHz(pl, start + size_t(0.02 * kFs), size_t(0.1 * kFs));
			std::printf("    MIDI note %d: %.0f Hz\n", note, hz);
			transposes = transposes && std::abs(hz / 440 - ratio) < 0.04;
			g.u->runSilent(0.8);
		}
		check(transposes, "MIDI notes transpose it (an octave, a fifth)");
	}

	std::printf("The audio trigger\n");
	{
		Gt g(rom);
		g.sysexOn();
		g.program(0);
		g.edit(7, 0, 7);
		g.u->runSilent(0.5);
		g.edit(2, 3, 1);
		g.press(12);
		g.press(5, 0.1);
		double fired = 0;
		for (double v = 0.3; v < 1.5 && fired == 0; v += 0.05) {
			std::vector<double> x(size_t(0.3 * kFs), 0.0), l;
			g.run(x, l, v);
			if (g.line(0).find("WAITING") == std::string::npos)
				fired = v;
		}
		std::printf("    fires at %.2f V of envelope (%.1f dB under 5 V)\n", fired, 20 * std::log10(5 / std::max(fired, 1e-9)));
		check(fired > 0.45 && fired < 0.8, "at the panel's -18 dB (0.63 V under the clip LED's 5 V)");
	}

	std::printf(g_failures ? "\n%d FAILED\n" : "\nAll passed.\n", g_failures);
	return g_failures ? 1 : 0;
}
