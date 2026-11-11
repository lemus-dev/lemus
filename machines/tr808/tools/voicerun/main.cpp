// voicerun: plays hits through the TR-808 voice board (no CPU) and writes the master output to a
// WAV file, for checking voices against the service notes and recordings.
//
//   voicerun OUT.wav [--rate HZ] [--seconds S] [--hit TIME:VOICE[+AC]]... [--set NAME=VALUE]... [--cal NAME=VALUE]... [--cold]
//
// VOICE is bd sd lt mt ht rs cp cb cy oh ch; +AC accents the step. --set takes the Controls
// fields (bdDecay=0.8, accent=1, lc=1 ...). Prints the CPU time per second of audio.
#include "tr808/core/circuit.h"
#include "tr808/core/voiceboard.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace tr808;

namespace {

void writeWav(const std::string &path, const std::vector<float> &x, int rate, int channels = 1)
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
	struct F { const char *n; double Controls::*p; };
	static const F fields[] = {
		{"accent", &Controls::accent}, {"bdLevel", &Controls::bdLevel}, {"bdTone", &Controls::bdTone},
		{"bdDecay", &Controls::bdDecay}, {"sdLevel", &Controls::sdLevel}, {"sdTone", &Controls::sdTone},
		{"sdSnappy", &Controls::sdSnappy}, {"ltLevel", &Controls::ltLevel}, {"ltTuning", &Controls::ltTuning},
		{"mtLevel", &Controls::mtLevel}, {"mtTuning", &Controls::mtTuning}, {"htLevel", &Controls::htLevel},
		{"htTuning", &Controls::htTuning}, {"rsLevel", &Controls::rsLevel}, {"cpLevel", &Controls::cpLevel},
		{"cbLevel", &Controls::cbLevel}, {"cyLevel", &Controls::cyLevel}, {"cyTone", &Controls::cyTone},
		{"cyDecay", &Controls::cyDecay}, {"ohLevel", &Controls::ohLevel}, {"ohDecay", &Controls::ohDecay},
		{"chLevel", &Controls::chLevel}, {"volume", &Controls::volume},
	};
	for (const auto &f : fields)
		if (key == f.n) {
			c.*f.p = v;
			return true;
		}
	struct B { const char *n; bool Controls::*p; };
	static const B bools[] = {{"lc", &Controls::lc}, {"mc", &Controls::mc}, {"hc", &Controls::hc}, {"cl", &Controls::cl}, {"ma", &Controls::ma}};
	for (const auto &b : bools)
		if (key == b.n) {
			c.*b.p = v > 0.5;
			return true;
		}
	return false;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: voicerun OUT.wav [--rate HZ] [--seconds S] [--hit TIME:VOICE[+AC]]... [--set NAME=VALUE]...\n");
		return 2;
	}
	const std::string outPath = argv[1];
	std::string jacksPath; // --jacks FILE.wav: every voice's jack, one channel each (volts)
	std::vector<std::string> probes; // --probe NAME: board signals printed every 10 ms
	double rate = 48000, seconds = 1;
	bool cold = false;
	Controls ctl;
	Calibration cal = unitCalibration();
	struct Hit { double t; uint16_t bits; };
	std::vector<Hit> hits;
	static const char *names[] = {"bd", "sd", "lt", "mt", "ht", "rs", "cp", "cb", "cy", "oh", "ch"};
	for (int a = 2; a < argc; ++a) {
		const std::string arg = argv[a];
		auto next = [&]() -> std::string { return a + 1 < argc ? argv[++a] : ""; };
		if (arg == "--rate") {
			rate = std::atof(next().c_str());
		} else if (arg == "--seconds") {
			seconds = std::atof(next().c_str());
		} else if (arg == "--set") {
			const std::string kv = next();
			const auto eq = kv.find('=');
			if (eq == std::string::npos || !setControl(ctl, kv.substr(0, eq), std::atof(kv.substr(eq + 1).c_str()))) {
				std::fprintf(stderr, "bad --set %s\n", kv.c_str());
				return 2;
			}
		} else if (arg == "--cal") {
			// a Calibration field: claveR313, cpOffset, noiseRms, monoMs, cbHz1, cbHz2, metalDuty,
			// metalHz0-3, lateSnare, lateClap; a part value, voice.param (bd.r170); or a knob law,
			// law=A,B,C (every knob's) or law.KNOB=A,B,C (one knob's: law.ltTuning), the rotation at
			// the 2.5, 5 and 7.5 ticks
			const std::string kv = next();
			const auto eq = kv.find('=');
			const std::string k = kv.substr(0, eq);
			if (k == "law" || k.rfind("law.", 0) == 0) {
				std::array<double, 3> law{};
				if (std::sscanf(kv.c_str() + eq + 1, "%lf,%lf,%lf", &law[0], &law[1], &law[2]) != 3) {
					std::fprintf(stderr, "bad --cal %s\n", kv.c_str());
					return 2;
				}
				if (k == "law")
					cal.knobLaw = law;
				else
					cal.knobLaws[k.substr(4)] = law;
				continue;
			}
			const double v = circuit::parseValue(kv.substr(eq + 1));
			if (k == "nominal") cal = Calibration{}; // the schematic's parts (before any other --cal)
			else if (k == "claveR313") cal.claveR313 = v;
			else if (k == "cpOffset") cal.cpOffset = v;
			else if (k == "noiseRms") cal.noiseRms = v;
			else if (k == "monoMs") cal.monoMs = v;
			else if (k == "cbHz1") cal.cbHz1 = v;
			else if (k == "cbHz2") cal.cbHz2 = v;
			else if (k == "metalDuty") cal.metalDuty = v;
			else if (k.rfind("metalHz", 0) == 0 && k.size() == 8 && k[7] >= '0' && k[7] <= '3') cal.metalHz[size_t(k[7] - '0')] = v;
			else if (k == "lateSnare") cal.lateSnare = v != 0;
			else if (k == "lateClap") cal.lateClap = v != 0;
			else if (k.find('.') != std::string::npos) // a part value: voice.param
				cal.parts[k] = v;
			else {
				std::fprintf(stderr, "bad --cal %s\n", kv.c_str());
				return 2;
			}
		} else if (arg == "--cold") { // POWER just switched on: discharged, the master muted 1.5 s
			cold = true;
		} else if (arg == "--probe") {
			probes.push_back(next());
		} else if (arg == "--jacks") {
			jacksPath = next();
		} else if (arg == "--hit") {
			const std::string h = next();
			const auto colon = h.find(':');
			Hit hit{std::atof(h.substr(0, colon).c_str()), 0};
			std::string rest = h.substr(colon + 1);
			size_t pos = 0;
			while (pos <= rest.size()) {
				const auto plus = rest.find('+', pos);
				const std::string v = rest.substr(pos, plus == std::string::npos ? std::string::npos : plus - pos);
				if (v == "ac" || v == "AC")
					hit.bits |= kBitAC;
				for (int k = 0; k < kVoices; ++k)
					if (v == names[k])
						hit.bits |= uint16_t(1u << k);
				if (plus == std::string::npos)
					break;
				pos = plus + 1;
			}
			hits.push_back(hit);
		} else {
			std::fprintf(stderr, "unknown argument %s\n", arg.c_str());
			return 2;
		}
	}
	VoiceBoard board;
	board.setControls(ctl);
	board.setCalibration(cal);
	board.prepare(rate);
	if (cold)
		board.coldStart();
	const long n = long(seconds * rate);
	std::vector<float> out(static_cast<size_t>(n));
	// A step: the data lines up, the step pulse (10 us), data held 1.2 ms, as the firmware does.
	const auto t0 = std::chrono::steady_clock::now();
	std::array<double, kVoices> peak{};
	std::vector<float> jacks;
	long i = 0;
	const int block = 64;
	while (i < n) {
		const int len = int(std::min<long>(block, n - i));
		for (int k = 0; k < len; ++k) {
			const double t = double(i + k) / rate;
			uint16_t data = 0;
			bool pulse = false;
			for (const auto &h : hits) {
				if (t >= h.t && t < h.t + 0.0012)
					data |= h.bits;
				if (t >= h.t && t < h.t + 1.0 / rate * 1.5)
					pulse = true;
			}
			board.setLines(pulse, data);
			board.render(out.data() + i + k, 1);
			if (!probes.empty() && (i + k) % long(rate / 100) == 0) {
				std::printf("%8.3f", t);
				for (const auto &p : probes)
					std::printf(" %s %.6g", p.c_str(), board.signal(p));
				std::printf("\n");
			}
			if (!jacksPath.empty())
				for (int v = 0; v < kVoices; ++v)
					jacks.push_back(float(board.voiceOut(Voice(v))));
			if (t > 0.5)
				for (int v = 0; v < kVoices; ++v)
					peak[size_t(v)] = std::max(peak[size_t(v)], std::abs(board.voiceOut(Voice(v))));
		}
		i += len;
	}
	const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	writeWav(outPath, out, int(rate));
	if (!jacksPath.empty())
		writeWav(jacksPath, jacks, int(rate), kVoices);
	const auto st = board.stats();
	std::fprintf(stderr, "%.3f s CPU for %.2f s audio (%.1f %% of real time); substepped %ld, failures %ld; voice steps:",
	             cpu, seconds, 100 * cpu / seconds, st.substepped, st.failures);
	for (int k = 0; k < kVoices; ++k)
		if (st.voiceSteps[size_t(k)])
			std::fprintf(stderr, "\n  %s: %ld steps, %d ports, %.2f iterations, %.2f factorisations per step, %ld block steps linear (%ld rebuilds), %.3f s (%.2f us/step)", names[k],
			             st.voiceSteps[size_t(k)], st.ports[size_t(k)], double(st.iterations[size_t(k)]) / double(st.voiceSteps[size_t(k)]),
			             double(st.factorizations[size_t(k)]) / double(st.voiceSteps[size_t(k)]), st.linearSteps[size_t(k)], st.linearizations[size_t(k)], st.seconds[size_t(k)],
			             1e6 * st.seconds[size_t(k)] / double(st.voiceSteps[size_t(k)]));
	if (std::getenv("TR808_BLOCKS"))
		std::fprintf(stderr, "\n%s", board.blockReport().c_str());
	std::fprintf(stderr, "\nvoice peaks after 0.5 s:");
	for (int k = 0; k < kVoices; ++k)
		std::fprintf(stderr, " %s %.2g", names[k], peak[size_t(k)]);
	std::fprintf(stderr, "\n");
	return 0;
}
