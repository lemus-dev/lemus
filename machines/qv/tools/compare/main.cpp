// qvcompare: a recording's dry source through the unit at a program, as three renders: the whole
// output, its dry path alone and its wet path alone (Unit::muteWet, muteDry), for
// tools/compare/compare.py to hold against the unit's recorded output.
//
//   qvcompare ROM DRY.wav PROGRAM OUT_PREFIX [--input 0-10] [--hiss 0|1]
//
// DRY.wav: 16-bit PCM, mono or stereo, at any rate (the renders at the same rate). Writes
// OUT_PREFIX-full.wav, -dry.wav, -wet.wav (stereo float 32), each with 3 s of tail.
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

bool readWav(const char *path, std::vector<float> &l, std::vector<float> &r, double &rate)
{
	std::ifstream f(path, std::ios::binary);
	std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), {});
	if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4))
		return false;
	auto u16 = [&](size_t p) { return uint16_t(d[p] | d[p + 1] << 8); };
	auto u32 = [&](size_t p) { return uint32_t(d[p] | d[p + 1] << 8 | d[p + 2] << 16 | uint32_t(d[p + 3]) << 24); };
	int channels = 0, bits = 0;
	for (size_t p = 12; p + 8 <= d.size();) {
		const uint32_t len = u32(p + 4);
		if (!std::memcmp(&d[p], "fmt ", 4)) {
			channels = u16(p + 10);
			rate = u32(p + 12);
			bits = u16(p + 22);
		} else if (!std::memcmp(&d[p], "data", 4) && bits == 16 && channels > 0) {
			const size_t frames = std::min<size_t>(len, d.size() - p - 8) / size_t(2 * channels);
			for (size_t i = 0; i < frames; ++i) {
				const unsigned char *s = &d[p + 8 + i * size_t(2 * channels)];
				const float a = float(int16_t(s[0] | s[1] << 8)) / 32768.0f;
				const float b = channels > 1 ? float(int16_t(s[2] | s[3] << 8)) / 32768.0f : a;
				l.push_back(a);
				r.push_back(b);
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

} // namespace

int main(int argc, char **argv)
{
	if (argc < 5) {
		std::fprintf(stderr, "usage: qvcompare ROM DRY.wav PROGRAM OUT_PREFIX [--input 0-10] [--hiss 0|1]\n");
		return 2;
	}
	double input = qv::Unit::kInputUnity * 10;
	bool hiss = false;
	for (int i = 5; i + 1 < argc; i += 2) {
		if (!std::strcmp(argv[i], "--input"))
			input = std::atof(argv[i + 1]);
		else if (!std::strcmp(argv[i], "--hiss"))
			hiss = std::atoi(argv[i + 1]) != 0;
	}
	std::ifstream rf(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rf)), {});
	if (!qv::Board::prepareRom(rom)) {
		std::fprintf(stderr, "%s: not a QuadraVerb EPROM (v2.03 or v1.07)\n", argv[1]);
		return 1;
	}
	std::vector<float> inL, inR;
	double rate = 0;
	if (!readWav(argv[2], inL, inR, rate)) {
		std::fprintf(stderr, "%s: not a 16-bit PCM WAV\n", argv[2]);
		return 1;
	}
	const int program = std::atoi(argv[3]);
	for (const char *which : {"full", "dry", "wet"}) {
		qv::Unit u(rom);
		u.powerOn();
		u.recallFactoryPrograms();
		u.board().midiIn(0xc0);
		u.board().midiIn(uint8_t(program & 0x7f));
		u.runSilent(0.5);
		u.setHiss(hiss);
		u.setIdleSleep(false);
		u.muteWet = !std::strcmp(which, "dry");
		u.muteDry = !std::strcmp(which, "wet");
		u.inputPot = input / 10;
		u.outputPot = 1.0;
		u.prepare(rate);
		const size_t n = inL.size() + size_t(3 * rate);
		std::vector<float> l(n), r(n), a(n, 0.0f), b(n, 0.0f);
		std::copy(inL.begin(), inL.end(), a.begin());
		std::copy(inR.begin(), inR.end(), b.begin());
		for (size_t at = 0; at < n; at += 512) {
			const int len = int(std::min<size_t>(512, n - at));
			u.process(&a[at], &b[at], &l[at], &r[at], len);
		}
		// the latency taken out, so the renders line up with the input
		const size_t lat = size_t(u.latency());
		l.erase(l.begin(), l.begin() + long(lat));
		r.erase(r.begin(), r.begin() + long(lat));
		writeWav(std::string(argv[4]) + "-" + which + ".wav", l, r, rate);
		if (!std::strcmp(which, "full"))
			std::printf("program %02d |%s|, latency %zu samples\n", program, u.board().lcd().text(1).c_str(), lat);
	}
	return 0;
}
