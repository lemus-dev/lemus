// sh101voiceblocks: the whole voice (core/voice.h) on its own, for tools/spice/voice/compare_voice.py.
// It drives the CPU lines directly (no firmware), so a note starts exactly when it is told to.
//
//   sh101voiceblocks [--rate HZ] [--calibration service|real|roland] [--cal NAME=VALUE ...]
//                    PREROLL SECONDS [key=value ...] [@t:kcv,range,gate ...]
//
// key=value sets a Controls field (enums as their index: lfoWave, pwmSource, subMode, vcaMode,
// trigger, portaMode). @t:kcv,range,gate sets the S/H codes and the gate from time t (seconds; the
// first one applies from the reset). The voice is reset PREROLL seconds before t = 0 and runs to
// SECONDS. Writes one line per host sample from the reset: t, the OUTPUT jack (volts), the VCO
// (Hz), the ENV probe, the VCF pole (Hz). The decimator's delay (host samples) goes to stderr as
// "latency N".
#include "sh101/core/voice.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace sh101;

namespace {

bool setControl(Controls &c, const std::string &k, double v)
{
	if (k == "lfoRate") c.lfoRate = v;
	else if (k == "lfoWave") c.lfoWave = LfoWave(int(v));
	else if (k == "vcoMod") c.vcoMod = v;
	else if (k == "pulseWidth") c.pulseWidth = v;
	else if (k == "pwmSource") c.pwmSource = PwmSource(int(v));
	else if (k == "pulseLevel") c.pulseLevel = v;
	else if (k == "sawLevel") c.sawLevel = v;
	else if (k == "subLevel") c.subLevel = v;
	else if (k == "noiseLevel") c.noiseLevel = v;
	else if (k == "subMode") c.subMode = SubMode(int(v));
	else if (k == "vcfFreq") c.vcfFreq = v;
	else if (k == "vcfRes") c.vcfRes = v;
	else if (k == "vcfEnv") c.vcfEnv = v;
	else if (k == "vcfMod") c.vcfMod = v;
	else if (k == "vcfKybd") c.vcfKybd = v;
	else if (k == "vcaMode") c.vcaMode = VcaMode(int(v));
	else if (k == "attack") c.attack = v;
	else if (k == "decay") c.decay = v;
	else if (k == "sustain") c.sustain = v;
	else if (k == "release") c.release = v;
	else if (k == "trigger") c.trigger = Trigger(int(v));
	else if (k == "volume") c.volume = v;
	else if (k == "tune") c.tune = v;
	else if (k == "portamento") c.portamento = v;
	else if (k == "portaMode") c.portaMode = PortaMode(int(v));
	else if (k == "bendVco") c.bendVco = v;
	else if (k == "bendVcf") c.bendVcf = v;
	else if (k == "lfoModDepth") c.lfoModDepth = v;
	else return false;
	return true;
}

struct Event {
	double t;
	int kcv, range;
	bool gate;
};

} // namespace

int main(int argc, char **argv)
{
	double rate = 44100;
	Calibration cal = Calibration::serviceNotes();
	std::vector<std::string> pos;
	std::vector<std::pair<std::string, double>> calSets;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--rate" && i + 1 < argc)
			rate = std::atof(argv[++i]);
		else if (a == "--calibration" && i + 1 < argc) {
			const std::string c = argv[++i];
			cal = c == "service" ? Calibration::serviceNotes() : c == "roland" ? Calibration::roland() : Calibration::realUnit();
		} else if (a == "--cal" && i + 1 < argc) {
			const std::string kv = argv[++i];
			const auto eq = kv.find('=');
			calSets.push_back({kv.substr(0, eq), std::atof(kv.c_str() + eq + 1)});
		} else
			pos.push_back(a);
	}
	for (const auto &[k, v] : calSets)
		if (!cal.set(k, v)) {
			std::fprintf(stderr, "unknown calibration field %s\n", k.c_str());
			return 2;
		}
	if (pos.size() < 2) {
		std::fprintf(stderr, "usage: see the comment at the top of tools/spice/voice/blocks.cpp\n");
		return 2;
	}
	const double preroll = std::atof(pos[0].c_str()), seconds = std::atof(pos[1].c_str());
	Controls c;
	std::vector<Event> events;
	for (size_t i = 2; i < pos.size(); ++i) {
		const std::string &a = pos[i];
		if (a[0] == '@') {
			Event e{};
			int g = 0;
			if (std::sscanf(a.c_str() + 1, "%lf:%d,%d,%d", &e.t, &e.kcv, &e.range, &g) != 4) {
				std::fprintf(stderr, "bad event %s\n", a.c_str());
				return 2;
			}
			e.gate = g != 0;
			events.push_back(e);
		} else {
			const auto eq = a.find('=');
			if (eq == std::string::npos || !setControl(c, a.substr(0, eq), std::atof(a.c_str() + eq + 1))) {
				std::fprintf(stderr, "bad control %s\n", a.c_str());
				return 2;
			}
		}
	}

	Voice v;
	v.setCalibration(cal);
	v.setControls(c);
	v.prepare(rate);
	CpuLines lines;
	size_t next = 0;
	if (!events.empty()) {
		lines.kcv = events[0].kcv;
		lines.range = events[0].range;
		lines.gate = events[0].gate && events[0].t <= -preroll;
		if (events[0].t <= -preroll)
			next = 1;
	}
	v.setLines(lines);
	v.reset();
	v.setLines(lines);
	std::fprintf(stderr, "latency %.9g\n", v.latencySamples());
	ModFrame mod; // the MODULATOR at rest: VCO and VCF MOD are 0 in every case, PWM is MAN
	const int total = int((preroll + seconds) * rate + 0.5);
	for (int n = 0; n < total; ++n) {
		const double t = n / rate - preroll;
		while (next < events.size() && events[next].t <= t + 0.5 / rate) {
			lines.kcv = events[next].kcv;
			lines.range = events[next].range;
			lines.gate = events[next].gate;
			v.setLines(lines);
			++next;
		}
		float out = 0;
		v.render(&out, 1, &mod);
		const auto p = v.probe();
		std::printf("%.9g %.9g %.9g %.6g %.6g %.6g\n", t, out / Voice::kOutputScale, p.vcoHz, p.env, p.vcfHz, p.iAbc);
	}
	return 0;
}
