// voicerun: plays hits through the TR-909 voicing board (no CPU) and writes the master outputs to a
// WAV file, for checking voices against the service notes and recordings.
//
//   tr909voicerun OUT.wav [--rate HZ] [--seconds S] [--hit TIME:VOICE[:CODE]]... [--set NAME=VALUE]...
//                 [--cal VOICE.PARAM=VALUE]... [--jacks FILE.wav] [--probe SIGNAL]... [--roms DIR] [--trig MS]
//
// VOICE is bd sd lt mt ht rs hc ch oh cr rd; CODE is the accent DAC's code, 0-63 (default 10, the
// firmware's normal level at TOTAL ACCENT 0; 21 is its accent there, 47 at full TOTAL ACCENT). Each
// hit writes the voice's DAC, then holds its trigger high for --trig ms (2, about one panel scan).
// --set takes the Controls fields (bdTune=0.8, sdSnappy=1, volume=0.7 ...). --jacks writes every
// MULTI OUT jack as a channel. Prints the CPU time per second of audio.
#include "tr909/core/circuit.h"
#include "tr909/core/voiceboard.h"
#include "tr909/core/parallel.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <algorithm>
#include <string>
#include <vector>

using namespace tr909;

namespace {

void writeWav(const std::string &path, const std::vector<float> &x, int rate, int channels)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	const uint32_t bytes = uint32_t(x.size() * 4);
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + bytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(3);
	u16(uint16_t(channels));
	u32(uint32_t(rate));
	u32(uint32_t(rate * 4 * channels));
	u16(uint16_t(4 * channels));
	u16(32);
	std::fwrite("data", 1, 4, f);
	u32(bytes);
	std::fwrite(x.data(), 4, x.size(), f);
	std::fclose(f);
}

bool setControl(Controls &c, const std::string &key, double v)
{
	struct F {
		const char *n;
		double Controls::*p;
	};
	static const F fields[] = {
		{"bdTune", &Controls::bdTune},   {"bdLevel", &Controls::bdLevel}, {"bdAttack", &Controls::bdAttack},
		{"bdDecay", &Controls::bdDecay}, {"sdTune", &Controls::sdTune},   {"sdLevel", &Controls::sdLevel},
		{"sdTone", &Controls::sdTone},   {"sdSnappy", &Controls::sdSnappy}, {"ltTune", &Controls::ltTune},
		{"ltLevel", &Controls::ltLevel}, {"ltDecay", &Controls::ltDecay}, {"mtTune", &Controls::mtTune},
		{"mtLevel", &Controls::mtLevel}, {"mtDecay", &Controls::mtDecay}, {"htTune", &Controls::htTune},
		{"htLevel", &Controls::htLevel}, {"htDecay", &Controls::htDecay}, {"rsLevel", &Controls::rsLevel},
		{"hcLevel", &Controls::hcLevel}, {"hhLevel", &Controls::hhLevel}, {"chDecay", &Controls::chDecay},
		{"ohDecay", &Controls::ohDecay}, {"crLevel", &Controls::crLevel}, {"crTune", &Controls::crTune},
		{"rdLevel", &Controls::rdLevel}, {"rdTune", &Controls::rdTune},   {"volume", &Controls::volume},
	};
	for (const auto &f : fields)
		if (key == f.n) {
			c.*f.p = v;
			return true;
		}
	return false;
}

std::vector<uint8_t> readFile(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: tr909voicerun OUT.wav [--rate HZ] [--seconds S] [--hit TIME:VOICE[:CODE]]... [--set NAME=VALUE]...\n");
		return 2;
	}
	const std::string outPath = argv[1];
	std::string jacksPath, romDir = TR909_ROM_DIR;
	std::vector<std::string> probes;
	double rate = 48000, seconds = 1, trigMs = 2.0;
	int threads = 1, block = 1; // --threads N: the voices' jobs on N threads; --block N: host samples a render call
	Controls ctl;
	Calibration cal = unitCalibration();
	struct Hit {
		double t;
		int voice; // Voice, with HH split: kVoices = the open hat
		int code;
	};
	std::vector<Hit> hits;
	static const char *names[] = {"bd", "sd", "lt", "mt", "ht", "rs", "hc", "ch", "cr", "rd", "oh"};
	for (int a = 2; a < argc; ++a) {
		const std::string arg = argv[a];
		auto next = [&]() -> std::string { return a + 1 < argc ? argv[++a] : ""; };
		if (arg == "--rate") {
			rate = std::atof(next().c_str());
		} else if (arg == "--seconds") {
			seconds = std::atof(next().c_str());
		} else if (arg == "--threads") {
			threads = std::max(1, std::atoi(next().c_str()));
		} else if (arg == "--block") {
			block = std::max(1, std::atoi(next().c_str()));
		} else if (arg == "--trig") {
			trigMs = std::atof(next().c_str());
		} else if (arg == "--set") {
			const std::string kv = next();
			const auto eq = kv.find('=');
			if (eq == std::string::npos || !setControl(ctl, kv.substr(0, eq), std::atof(kv.substr(eq + 1).c_str()))) {
				std::fprintf(stderr, "bad --set %s\n", kv.c_str());
				return 2;
			}
		} else if (arg == "--cal") {
			// a part value, voice.param (bd.c9); noiseHz; nominal=1 (the service notes' parts and
			// straight knobs, before any other --cal); or a knob law, law.KNOB=R,P,R,P,... (law.ltTune:
			// rotation, track position pairs; law.ltTune= alone makes it straight)
			const std::string kv = next();
			const auto eq = kv.find('=');
			const std::string k = kv.substr(0, eq);
			if (k.rfind("law.", 0) == 0) {
				std::vector<std::pair<double, double>> pts;
				std::vector<double> v;
				std::string rest = kv.substr(eq + 1);
				for (size_t pos = 0; pos < rest.size();) {
					const auto comma = rest.find(',', pos);
					v.push_back(std::atof(rest.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos).c_str()));
					if (comma == std::string::npos)
						break;
					pos = comma + 1;
				}
				for (size_t i = 0; i + 1 < v.size(); i += 2)
					pts.push_back({v[i], v[i + 1]});
				cal.knobLaws[k.substr(4)] = pts;
				continue;
			}
			if (k == "nominal") {
				cal = Calibration{};
				continue;
			}
			const double v = circuit::parseValue(kv.substr(eq + 1));
			if (k == "noiseHz")
				cal.noiseHz = v;
			else if (k == "hhClockHz")
				cal.hhClockHz = v;
			else if (k.find('.') != std::string::npos)
				cal.parts[k] = v;
			else {
				std::fprintf(stderr, "bad --cal %s\n", kv.c_str());
				return 2;
			}
		} else if (arg == "--probe") {
			probes.push_back(next());
		} else if (arg == "--jacks") {
			jacksPath = next();
		} else if (arg == "--roms") {
			romDir = next();
		} else if (arg == "--hit") {
			const std::string h = next();
			const auto c1 = h.find(':');
			const auto c2 = h.find(':', c1 + 1);
			const std::string v = h.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
			Hit hit{std::atof(h.substr(0, c1).c_str()), -1, c2 == std::string::npos ? 10 : std::atoi(h.c_str() + c2 + 1)};
			for (int k = 0; k <= kVoices; ++k)
				if (v == names[k])
					hit.voice = k;
			if (hit.voice < 0) {
				std::fprintf(stderr, "bad --hit %s\n", h.c_str());
				return 2;
			}
			hits.push_back(hit);
		} else {
			std::fprintf(stderr, "unknown argument %s\n", arg.c_str());
			return 2;
		}
	}
	const auto hh = readFile(romDir + "/hn61256p__c43.ic69"), cr = readFile(romDir + "/hn61256p__c42.ic62"),
	           rd = readFile(romDir + "/hn61256p__c44.ic54");
	VoiceBoard board;
	board.setSampleRoms({hh.size() == 32768 ? hh.data() : nullptr, cr.size() == 32768 ? cr.data() : nullptr,
	                     rd.size() == 32768 ? rd.data() : nullptr});
	board.setControls(ctl);
	board.setCalibration(cal); // prepares at the default rate
	board.prepare(rate);
	std::unique_ptr<Parallel> pool;
	if (threads > 1) {
		pool = std::make_unique<Parallel>(threads - 1);
		board.setParallel(pool.get());
	}
	const long n = long(seconds * rate);
	std::vector<float> out(size_t(2 * n));
	std::vector<float> jacks;
	static constexpr int kDac[kVoices + 1] = {1, 2, 3, 4, 5, 6, 6, 7, 8, 8, 7};
	static constexpr int kReg[kVoices + 1] = {9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10};
	static constexpr int kBit[kVoices + 1] = {1, 2, 3, 4, 5, 1, 6, 2, 4, 5, 2};
	const auto t0 = std::chrono::steady_clock::now();
	std::vector<bool> up(hits.size(), false), done(hits.size(), false);
	// the first sample at or after time t
	auto at = [rate](double t) {
		long e = long(std::ceil(t * rate));
		while (e > 0 && double(e - 1) / rate >= t)
			--e;
		while (double(e) / rate < t)
			++e;
		return e;
	};
	std::vector<float> lb(static_cast<size_t>(block)), rb(static_cast<size_t>(block)), jb(static_cast<size_t>(kVoices * block));
	for (long i = 0; i < n;) {
		const double t = double(i) / rate;
		long next = n; // the next sample a write falls on
		for (size_t h = 0; h < hits.size(); ++h) {
			const auto &hit = hits[h];
			if (!up[h] && t >= hit.t) {
				board.write(kDac[hit.voice], uint8_t((hit.code & 63) << 1));
				if (hit.voice == int(Voice::HH) || hit.voice == kVoices) // CLOSED/OPEN: high for the closed hat
					board.write(10, uint8_t((board.latch(10) & ~0x08) | (hit.voice == kVoices ? 0 : 0x08)));
				board.write(kReg[hit.voice], uint8_t(board.latch(kReg[hit.voice]) | (1u << kBit[hit.voice])));
				up[h] = true;
			}
			if (up[h] && !done[h] && t >= hit.t + trigMs * 1e-3) {
				board.write(kReg[hit.voice], uint8_t(board.latch(kReg[hit.voice]) & ~(1u << kBit[hit.voice])));
				done[h] = true;
			}
			if (!up[h])
				next = std::min(next, at(hit.t));
			else if (!done[h])
				next = std::min(next, at(hit.t + trigMs * 1e-3));
		}
		const int len = int(std::max(1L, std::min({long(block), next - i, n - i, probes.empty() ? long(block) : 1L})));
		float *jp[kVoices];
		for (int v = 0; v < kVoices; ++v)
			jp[v] = &jb[size_t(v * block)];
		board.render(lb.data(), rb.data(), len, jacksPath.empty() ? nullptr : jp);
		for (int k = 0; k < len; ++k) {
			out[size_t(2 * (i + k))] = lb[size_t(k)];
			out[size_t(2 * (i + k) + 1)] = rb[size_t(k)];
			if (!jacksPath.empty())
				for (int v = 0; v < kVoices; ++v)
					jacks.push_back(float(jb[size_t(v * block + k)] * VoiceBoard::kVoltsFullScale));
		}
		if (!probes.empty() && i % long(rate / 1000) == 0) {
			std::printf("%8.4f", t);
			for (const auto &p : probes)
				std::printf(" %s %.6g", p.c_str(), board.signal(p));
			std::printf("\n");
		}
		i += len;
	}
	const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	writeWav(outPath, out, int(rate), 2);
	if (!jacksPath.empty())
		writeWav(jacksPath, jacks, int(rate), kVoices);
	std::fprintf(stderr, "%.3f s CPU for %.2f s audio (%.1f %% of real time)\n", cpu, seconds, 100 * cpu / seconds);
	std::fprintf(stderr, "at the end: %s; awake:", board.asleep() ? "asleep" : "awake");
	for (int v = 0; v < kVoices; ++v)
		if (board.voiceAwake(Voice(v)))
			std::fprintf(stderr, " %s", names[v]);
	std::fprintf(stderr, "\n");
	if (std::getenv("TR909_BLOCKS"))
		std::fprintf(stderr, "%s", board.blockReport().c_str());
	return 0;
}
