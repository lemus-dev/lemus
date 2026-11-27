// The QuadraVerb's board headless: boots the firmware and logs the LCD and the latches as they
// change; optionally presses keys and renders a WAV (mono in, stereo out at the DSP's rate).
//
//   qvrun ROM [-s seconds] [-f volts] [-k time:key[:hold]]... [-m time:hexbytes]... [-p program]
//           [-l] [-i in.wav -o out.wav]
//
// ROM: the 27C512 image as read or descrambled. -k presses key k (the firmware's key numbers,
// core/board.h) at `time` seconds for `hold` seconds (0.1 by default); -f sets the key force (the
// piezo's volts, core/board.h). -p selects a program by MIDI
// program change once booted. -l loops MIDI out back to MIDI in (the service manual's
// self-diagnostics: power up holding BYPASS and CONFIG, -k 0:5:3 -k 0:14:3). -i/-o render a 16-bit
// mono WAV through the board after boot.
#include "qv/core/board.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace qv;

namespace {

struct KeyPress {
	double at, hold;
	int key;
};

struct MidiAt {
	double at;
	std::vector<uint8_t> bytes;
};

std::vector<float> readWav(const char *path, int &rate)
{
	std::ifstream f(path, std::ios::binary);
	std::vector<char> d((std::istreambuf_iterator<char>(f)), {});
	std::vector<float> out;
	if (d.size() < 44)
		return out;
	size_t p = 12;
	int channels = 1, bits = 16;
	while (p + 8 <= d.size()) {
		const std::string id(&d[p], 4);
		const uint32_t len = uint8_t(d[p + 4]) | uint8_t(d[p + 5]) << 8 | uint8_t(d[p + 6]) << 16 | uint32_t(uint8_t(d[p + 7])) << 24;
		if (id == "fmt ") {
			channels = uint8_t(d[p + 10]);
			rate = int(uint8_t(d[p + 12]) | uint8_t(d[p + 13]) << 8 | uint8_t(d[p + 14]) << 16);
			bits = uint8_t(d[p + 22]);
		} else if (id == "data" && bits == 16) {
			for (size_t i = p + 8; i + 2 * size_t(channels) <= p + 8 + len && i + 1 < d.size(); i += 2 * size_t(channels))
				out.push_back(float(int16_t(uint8_t(d[i]) | uint8_t(d[i + 1]) << 8)) / 32768.0f);
		}
		p += 8 + len + (len & 1);
	}
	return out;
}

void writeWav(const char *path, const std::vector<float> &l, const std::vector<float> &r, int rate)
{
	std::ofstream f(path, std::ios::binary);
	const uint32_t n = uint32_t(l.size()), data = n * 4;
	auto u32 = [&](uint32_t v) { f.put(char(v)); f.put(char(v >> 8)); f.put(char(v >> 16)); f.put(char(v >> 24)); };
	auto u16 = [&](uint16_t v) { f.put(char(v)); f.put(char(v >> 8)); };
	f.write("RIFF", 4); u32(36 + data); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32(uint32_t(rate)); u32(uint32_t(rate) * 4); u16(4); u16(16);
	f.write("data", 4); u32(data);
	for (uint32_t i = 0; i < n; ++i)
		for (float x : {l[i], r[i]})
			u16(uint16_t(int16_t(std::lround(std::fmax(-1.0, std::fmin(32767.0 / 32768, double(x))) * 32768))));
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: qvrun ROM [-s seconds] [-k time:key[:hold]]... [-p program] [-i in.wav -o out.wav]\n");
		return 1;
	}
	double seconds = 4, force = -1;
	int program = -1;
	bool loopback = false;
	const char *inWav = nullptr, *outWav = nullptr;
	std::vector<KeyPress> keys;
	std::vector<MidiAt> midi;
	for (int i = 2; i < argc; ++i) {
		if (!std::strcmp(argv[i], "-s") && i + 1 < argc)
			seconds = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "-m") && i + 1 < argc) {
			// -m time:hexbytes, e.g. 6.0:F000000E070364F7
			const char *arg = argv[++i];
			MidiAt m{std::atof(arg), {}};
			const char *hex = std::strchr(arg, ':');
			for (const char *h = hex ? hex + 1 : ""; h[0] && h[1]; h += 2) {
				const char b[3] = {h[0], h[1], 0};
				m.bytes.push_back(uint8_t(std::strtoul(b, nullptr, 16)));
			}
			midi.push_back(m);
		} else if (!std::strcmp(argv[i], "-f") && i + 1 < argc)
			force = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "-p") && i + 1 < argc)
			program = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "-l"))
			loopback = true;
		else if (!std::strcmp(argv[i], "-i") && i + 1 < argc)
			inWav = argv[++i];
		else if (!std::strcmp(argv[i], "-o") && i + 1 < argc)
			outWav = argv[++i];
		else if (!std::strcmp(argv[i], "-k") && i + 1 < argc) {
			KeyPress k{0, 0.1, 0};
			std::sscanf(argv[++i], "%lf:%d:%lf", &k.at, &k.key, &k.hold);
			keys.push_back(k);
		}
	}
	std::ifstream f(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), {});
	if (!Board::prepareRom(rom)) {
		std::fprintf(stderr, "%s: not a QuadraVerb firmware image\n", argv[1]);
		return 1;
	}
	Board board(std::move(rom));
	if (force >= 0)
		board.setKeyForce(force);

	std::string lines[2];
	uint8_t latches[8];
	for (int y = 0; y < 8; ++y)
		latches[y] = board.latch(y);
	uint64_t writes = 0;
	const int samples = int(seconds * kSampleRate);
	int lastOverflow = -samples;
	for (int s = 0; s < samples; ++s) {
		const double t = s / kSampleRate;
		for (const auto &k : keys) {
			if (std::abs(t - k.at) < 0.5 / kSampleRate)
				board.setKey(k.key, true);
			if (std::abs(t - (k.at + k.hold)) < 0.5 / kSampleRate)
				board.setKey(k.key, false);
		}
		for (const auto &m : midi)
			if (std::abs(t - m.at) < 0.5 / kSampleRate)
				for (const uint8_t b : m.bytes)
					board.midiIn(b);
		double l, r;
		board.runSample(0, 0, l, r);
		// the DSP's OVFLO (the clip LED), logged when it starts after 0.2 s without
		if (board.dsp().overflow) {
			board.dsp().overflow = false;
			if (s - lastOverflow > int(0.2 * kSampleRate))
				std::printf("[%9.3f ms] DSP overflow\n", t * 1e3);
			lastOverflow = s;
		}
		for (const uint8_t b : board.takeMidiOut()) {
			std::printf("[%9.3f ms] MIDI out %02X\n", t * 1e3, b);
			if (loopback)
				board.midiIn(b);
		}
		for (int n = 0; n < 2; ++n)
			if (board.lcd().text(n) != lines[n]) {
				lines[n] = board.lcd().text(n);
				std::printf("[%9.3f ms] LCD |%s|%s| pc=%04X\n", t * 1e3, board.lcd().text(0).c_str(), board.lcd().text(1).c_str(), board.cpu().pc());
				break;
			}
		for (int y = 2; y < 8; ++y)
			if (board.latch(y) != latches[y]) {
				latches[y] = board.latch(y);
				std::printf("[%9.3f ms] latch %04X = %02X\n", t * 1e3, y << 9, latches[y]);
			}
		writes = board.daspWrites();
	}
	std::printf("DSP control-store writes: %llu\n", (unsigned long long)writes);

	if (program >= 0) {
		board.midiIn(0xc0);
		board.midiIn(uint8_t(program));
		for (int s = 0; s < int(2 * kSampleRate); ++s) {
			double l, r;
			board.runSample(0, 0, l, r);
		}
		std::printf("after program change %d: |%s|%s|\n", program, board.lcd().text(0).c_str(), board.lcd().text(1).c_str());
	}
	if (inWav != nullptr && outWav != nullptr) {
		int rate = 0;
		const auto in = readWav(inWav, rate);
		if (in.empty() || std::abs(rate - kSampleRate) > 1) {
			std::fprintf(stderr, "%s: needs a 16-bit WAV at %.2f Hz (got %d Hz)\n", inWav, kSampleRate, rate);
			return 1;
		}
		std::vector<float> l, r;
		// the level detector's envelope as if full scale at the A/D were 5.4 V at U2 pin 8 (the
		// preamp flat, core/unit.cpp has the real chain)
		const double release = std::exp(-1 / (kSampleRate * 0.033));
		double env = 0;
		for (size_t i = 0; i < in.size() + size_t(3 * kSampleRate); ++i) {
			const double x = i < in.size() ? in[i] : 0.0;
			env = std::max(x * 5.4, env * release);
			double a, b;
			board.runSample(x, env, a, b);
			l.push_back(float(a));
			r.push_back(float(b));
		}
		writeWav(outWav, l, r, int(std::lround(kSampleRate)));
		std::printf("rendered %zu samples to %s\n", l.size(), outWav);
	}
	return 0;
}
