// tr909factory: makes the battery RAM of a new TR-909 (core/factory.h) from the user's v1.0
// firmware, and checks that v4.0 plays it as v1.0 does.
//
// Usage: tr909factory --v1 ROM [--out FILE] [--v4 ROM [--seconds S]]
//   --out FILE   write the 8 KB image (it is ROM-derived: keep it out of the repo)
//   --v4 ROM     power v4.0 on with the image, check it leaves the memory as it is, then play
//                track 1 at full TEMPO on both versions and compare every trigger (instrument,
//                accent DAC code, time); exits 1 on a mismatch
#include "tr909/core/factory.h"
#include "tr909/core/machine.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using tr909::Machine;

static std::vector<uint8_t> readFile(const char *path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Hit { double ms; int code; };

// Plays track 1 of `ram` and returns the trigger rising edges per instrument.
static std::map<std::string, std::vector<Hit>> playTrack1(const std::vector<uint8_t> &rom, const std::array<uint8_t, Machine::kRamSize> &ram, double seconds)
{
	static const struct { const char *name; int reg, bit, dac; } inst[] = {
		{"BD", 9, 1, 1}, {"SD", 9, 2, 2}, {"LT", 9, 3, 3}, {"MT", 9, 4, 4}, {"HT", 9, 5, 5},
		{"RS", 10, 1, 6}, {"HH", 10, 2, 7}, {"CR", 10, 4, 8}, {"RD", 10, 5, 8}, {"HC", 10, 6, 6},
	};
	auto m = std::make_unique<Machine>();
	m->setRom(rom.data(), rom.size());
	m->setRam(ram.data(), ram.size());
	m->powerOn();
	m->setTempoPot(1.0);
	std::map<std::string, std::vector<Hit>> hits;
	uint8_t latch[2] = {0, 0};
	const int step = int(Machine::kStateHz / 1000);
	bool startDown = false;
	for (int ms = 0; ms < int(seconds * 1000); ms++) {
		// TRACK 1 at 1.0 s, START at 1.3 s (0.1 s presses)
		m->setKey(Machine::Key::Track1, ms >= 1000 && ms < 1100);
		startDown = ms >= 1300 && ms < 1400;
		m->setKey(Machine::Key::Start, startDown);
		const double t0 = double(m->states()) / Machine::kStateHz * 1000.0;
		m->run(step);
		for (const auto &w : m->writes()) {
			if (w.reg < 9)
				continue;
			uint8_t &prev = latch[w.reg - 9];
			for (const auto &n : inst)
				if (n.reg == w.reg && !((prev >> n.bit) & 1) && ((w.value >> n.bit) & 1)) {
					std::string name = n.name;
					if (name == "HH")
						name = (w.value & 0x08) ? "CH" : "OH";
					hits[name].push_back({t0 + w.state / Machine::kStateHz * 1000.0, m->dac(n.dac) >> 1});
				}
			prev = w.value;
		}
		m->takeMidiOut();
	}
	return hits;
}

int main(int argc, char **argv)
{
	const char *v1 = nullptr, *v4 = nullptr, *out = nullptr;
	double seconds = 60;
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--v1") && i + 1 < argc) v1 = argv[++i];
		else if (!std::strcmp(argv[i], "--v4") && i + 1 < argc) v4 = argv[++i];
		else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
		else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
		else { std::fprintf(stderr, "usage: tr909factory --v1 ROM [--out FILE] [--v4 ROM [--seconds S]]\n"); return 2; }
	}
	if (!v1) { std::fprintf(stderr, "tr909factory: --v1 ROM is needed\n"); return 2; }
	const auto rom1 = readFile(v1);
	std::array<uint8_t, Machine::kRamSize> ram{};
	if (!tr909::factoryRam(rom1.data(), rom1.size(), ram)) {
		std::fprintf(stderr, "tr909factory: %s is not the v1.0 firmware (CRC32 %08x)\n", v1, tr909::romCrc32(rom1.data(), rom1.size()));
		return 1;
	}
	int used = 0;
	for (uint8_t b : ram) used += b != 0;
	std::printf("factory memory: %d non-zero bytes\n", used);
	if (out) {
		std::ofstream f(out, std::ios::binary);
		f.write(reinterpret_cast<const char *>(ram.data()), std::streamsize(ram.size()));
		std::printf("wrote %s\n", out);
	}
	if (!v4)
		return 0;

	const auto rom4 = readFile(v4);
	if (tr909::romCrc32(rom4.data(), rom4.size()) != tr909::kFirmwareV4Crc)
		std::printf("note: %s is not v4.0 (CRC32 %08x)\n", v4, tr909::romCrc32(rom4.data(), rom4.size()));
	int bad = 0;
	{
		// v4.0's power-on checks (the 2FEF flag, the track pointers) must leave the image alone.
		auto m = std::make_unique<Machine>();
		m->setRom(rom4.data(), rom4.size());
		m->setRam(ram.data(), ram.size());
		m->powerOn();
		for (int i = 0; i < 2000; i++)
			m->run(int(Machine::kStateHz / 1000));
		int changed = 0;
		for (size_t i = 0; i < ram.size(); i++)
			changed += m->ram()[i] != ram[i];
		std::printf("v4.0 power-on: %d bytes changed\n", changed);
		bad += changed != 0;
	}
	const auto a = playTrack1(rom1, ram, seconds), b = playTrack1(rom4, ram, seconds);
	int total = 0;
	for (const auto &e : a) {
		const auto &x = e.second;
		auto it = b.find(e.first);
		const std::vector<Hit> none;
		const auto &y = it == b.end() ? none : it->second;
		int levels = 0;
		double dmax = 0;
		const size_t n = std::min(x.size(), y.size());
		for (size_t i = 0; i < n; i++) {
			levels += x[i].code != y[i].code;
			dmax = std::max(dmax, std::fabs(y[i].ms - x[i].ms));
		}
		total += int(x.size());
		std::printf("%-2s  v1.0 %4zu hits  v4.0 %4zu  level mismatches %d  time difference up to %.2f ms\n",
			e.first.c_str(), x.size(), y.size(), levels, dmax);
		bad += x.size() != y.size() || levels != 0 || dmax > 8.0;
	}
	for (const auto &e : b)
		if (!a.count(e.first)) { std::printf("%s only on v4.0\n", e.first.c_str()); bad++; }
	std::printf("%d hits; %s\n", total, bad ? "MISMATCH" : "v4.0 plays the factory memory as v1.0 does");
	return bad ? 1 : 0;
}
