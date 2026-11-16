// qvgtrender: a WAV through the whole unit (core/unit.h) at the file's rate, as the plugin runs it.
//
//   qvgtrender ROM IN.wav OUT.wav [--input 0-10] [--output 0-10] [--program N] [--gain dB]
//              [--key t:KEY[:hold]]... [--tail S]
//
// The unit boots, gets its factory programs, takes program N by MIDI, and renders IN (its first
// channel to both inputs, scaled by --gain) with S seconds of tail. Prints the input and output
// levels, the headroom LEDs (time with each count lit), overflows, the preamp's latches, and the
// time it took against real time, and the preamp's peaks. OUT is stereo float 32.
#include "qvgt/core/unit.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool readWav(const std::string &path, std::vector<double> &out, double &rate)
{
	std::ifstream f(path, std::ios::binary);
	std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), {});
	if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4))
		return false;
	auto u16 = [&](size_t p) { return uint16_t(d[p] | d[p + 1] << 8); };
	auto u32 = [&](size_t p) { return uint32_t(d[p] | d[p + 1] << 8 | d[p + 2] << 16 | uint32_t(d[p + 3]) << 24); };
	size_t p = 12;
	int fmt = 0, ch = 0, bits = 0;
	while (p + 8 <= d.size()) {
		const uint32_t len = u32(p + 4);
		if (!std::memcmp(d.data() + p, "fmt ", 4)) {
			fmt = u16(p + 8);
			ch = u16(p + 10);
			rate = u32(p + 12);
			bits = u16(p + 22);
			if (fmt == 0xFFFE && len >= 26)
				fmt = u16(p + 32);
		} else if (!std::memcmp(d.data() + p, "data", 4)) {
			const size_t bytes = size_t(bits / 8), frame = bytes * size_t(ch);
			const size_t frames = std::min<size_t>(len, d.size() - p - 8) / frame;
			out.resize(frames);
			const unsigned char *s = d.data() + p + 8;
			for (size_t i = 0; i < frames; ++i, s += frame) {
				double v = 0;
				if (fmt == 3 && bits == 32) {
					float x;
					std::memcpy(&x, s, 4);
					v = x;
				} else if (bits == 16)
					v = int16_t(s[0] | s[1] << 8) / 32768.0;
				else if (bits == 24) {
					int32_t x = s[0] | s[1] << 8 | s[2] << 16;
					if (x & 0x800000)
						x -= 0x1000000;
					v = x / 8388608.0;
				}
				out[i] = v;
			}
			return true;
		}
		p += 8 + len + (len & 1);
	}
	return false;
}

void writeWav(const std::string &path, const std::vector<float> &l, const std::vector<float> &r, double rate)
{
	std::ofstream f(path, std::ios::binary);
	auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
	auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };
	const uint32_t bytes = uint32_t(l.size() * 8);
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
	for (size_t i = 0; i < l.size(); ++i) {
		f.write(reinterpret_cast<const char *>(&l[i]), 4);
		f.write(reinterpret_cast<const char *>(&r[i]), 4);
	}
}

double db(double x) { return 20 * std::log10(x + 1e-30); }

} // namespace

int main(int argc, char **argv)
{
	if (argc < 4) {
		std::fprintf(stderr, "usage: qvgtrender ROM IN.wav OUT.wav [--input 0-10] [--output 0-10] [--program N] [--gain dB] [--tail S]\n");
		return 2;
	}
	double inputPot = 5, outputPot = 7, gainDb = 0, tail = 2;
	int program = 0;
	std::vector<std::pair<double, int>> keys; // time, key (held 0.3 s)
	for (int i = 4; i < argc; ++i) {
		const std::string a = argv[i];
		auto next = [&]() -> const char * {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "%s needs a value\n", a.c_str());
				std::exit(2);
			}
			return argv[++i];
		};
		if (a == "--input")
			inputPot = std::atof(next());
		else if (a == "--output")
			outputPot = std::atof(next());
		else if (a == "--program")
			program = std::atoi(next());
		else if (a == "--gain")
			gainDb = std::atof(next());
		else if (a == "--tail")
			tail = std::atof(next());
		else if (a == "--key") {
			const std::string k = next();
			const auto c = k.find(':');
			keys.push_back({std::atof(k.substr(0, c).c_str()), std::atoi(k.substr(c + 1).c_str())});
		} else {
			std::fprintf(stderr, "unknown option %s\n", a.c_str());
			return 2;
		}
	}

	std::ifstream rf(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rf)), {});
	if (!qvgt::Board::prepareRom(rom)) {
		std::fprintf(stderr, "%s: not the GT's EPROM\n", argv[1]);
		return 1;
	}
	std::vector<double> in;
	double rate = 0;
	if (!readWav(argv[2], in, rate) || in.empty()) {
		std::fprintf(stderr, "can't read %s\n", argv[2]);
		return 1;
	}

	qvgt::Unit u(rom);
	u.powerOn();
	u.recallFactoryPrograms();
	u.board().midiIn(0xc0);
	u.board().midiIn(uint8_t(program & 0x7f));
	u.runSilent(0.5);
	u.inputPot = inputPot / 10;
	u.outputPot = outputPot / 10;
	u.prepare(rate);
	std::printf("program %02d |%s|  U10 %02X  U11 %02X  0400h %02X  0600h %02X  latency %d\n", program,
	            u.board().lcd().text(1).c_str(), u.board().latch(qvgt::Board::kLatchU10),
	            u.board().latch(qvgt::Board::kLatchU11), u.board().latch(qvgt::Board::kLatch0400),
	            u.board().latch(qvgt::Board::kLatch0600), u.latency());

	// the preamp's peaks: U2 pin 8, U3 pin 8 (the overdrive), the shaper's output, U13 pin 8 (to the A/D)
	double pk[4] = {};
	u.preamp().setTap([&pk](const qvgt::Preamp &p) {
		const auto &q = p.probe();
		const double v[4] = {q.p8, q.od8, q.pre, q.out};
		for (int k = 0; k < 4; ++k)
			pk[k] = std::max(pk[k], std::abs(v[k]));
	});
	const double g = std::pow(10.0, gainDb / 20);
	const size_t n = in.size() + size_t(tail * rate);
	std::vector<float> l(n), r(n);
	double inPeak = 0, inSum = 0, outPeak = 0, outSum = 0;
	long lit[5] = {}, clips = 0;
	bool wasClip = false;
	size_t nextKey = 0;
	const int block = 256;
	std::vector<float> bi(block), bl(block), br(block);
	const auto t0 = std::chrono::steady_clock::now();
	for (size_t at = 0; at < n; at += block) {
		const int len = int(std::min<size_t>(block, n - at));
		while (nextKey < keys.size() && keys[nextKey].first * rate <= double(at)) {
			// keys run on the board's own time: press, hold 0.3 s of audio, release (approximate)
			u.board().setKeyForce(qvgt::Unit::kLightPress);
			u.board().setKey(keys[nextKey].second, true);
			++nextKey;
		}
		for (int i = 0; i < len; ++i) {
			const double x = at + size_t(i) < in.size() ? in[at + size_t(i)] * g : 0.0;
			bi[size_t(i)] = float(x);
			inPeak = std::max(inPeak, std::abs(x));
			inSum += x * x;
		}
		u.process(bi.data(), bi.data(), bl.data(), br.data(), len);
		for (int i = 0; i < len; ++i) {
			l[at + size_t(i)] = bl[size_t(i)];
			r[at + size_t(i)] = br[size_t(i)];
			const double y = std::max(std::abs(bl[size_t(i)]), std::abs(br[size_t(i)]));
			outPeak = std::max(outPeak, y);
			outSum += 0.5 * (double(bl[size_t(i)]) * bl[size_t(i)] + double(br[size_t(i)]) * br[size_t(i)]);
		}
		++lit[u.headroom()];
		if (u.clip() && !wasClip)
			++clips;
		wasClip = u.clip();
		for (const auto &k : keys)
			if (k.first * rate + 0.3 * rate <= double(at) && k.first * rate + 0.3 * rate > double(at) - block)
				u.board().setKey(k.second, false);
	}
	const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	writeWav(argv[3], l, r, rate);
	const long blocks = lit[0] + lit[1] + lit[2] + lit[3] + lit[4];
	std::printf("in  peak %6.1f dBFS  rms %6.1f dBFS\n", db(inPeak), 10 * std::log10(inSum / double(in.size()) + 1e-30));
	std::printf("out peak %6.1f dBFS  rms %6.1f dBFS\n", db(outPeak), 10 * std::log10(outSum / double(n) + 1e-30));
	std::printf("preamp peaks: U2 pin 8 %.2f V, U3 pin 8 %.2f V, the shaper %.2f V, U13 pin 8 %.2f V\n", pk[0], pk[1], pk[2], pk[3]);
	std::printf("headroom LEDs lit (share of blocks): 0 %.0f%%  1 %.0f%%  2 %.0f%%  3 %.0f%%  4 %.0f%%; overflows %ld\n",
	            100.0 * lit[0] / blocks, 100.0 * lit[1] / blocks, 100.0 * lit[2] / blocks, 100.0 * lit[3] / blocks,
	            100.0 * lit[4] / blocks, clips);
	std::printf("%.1f s in %.2f s: %.1f x real time\n", double(n) / rate, took, double(n) / rate / took);
	return 0;
}
