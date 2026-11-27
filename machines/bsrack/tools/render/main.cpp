// Renders a scripted performance of the Bass Station Rack headless to a WAV file.
//
//   bsrackrender <rom> <out.wav> [seconds=5] [--rate=48000] [--pot=NAME:V@MS] [--switch=NAME:POS@MS]
//                [--button=NAME:0|1@MS] [--press=NAME@MS] [--note=MS:NOTE:VEL:LEN_MS] [--midi=MS:HH,HH,..]
//
// NAME: a pot, switch or button as core/controls.h lists them in lower case (volume, frequency, ...;
// cut_off, ...; key0..key9, key_minus, key_plus, audition, mode). --press is down then up 60 ms later.
// The machine starts as a plugin load does: a new unit's EEPROM, powered on, program 40 selected.
#include "bsrack/core/machine.h"
#include "bsrack/core/voice.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace bsrack;

namespace {

const char *kPotNames[kPots] = {"volume", "portamento", "lfo_speed", "lfo_delay", "mix", "detune", "semitone", "mod_env", "mod_lfo",
                                "pulse_width", "frequency", "resonance", "mod_depth", "attack", "decay", "sustain", "release", "velocity"};
const char *kSwitchNames[kSwitches] = {"write", "lfo_shape", "osc1_wave", "osc2_range", "osc2_wave", "sync",
                                       "oscillator", "pwm_source", "cut_off", "mod_source", "envelope", "triggering"};
const char *kButtonNames[kButtons] = {"key1", "key2", "key3", "key4", "key5", "key6", "key7", "key8", "key9", "key_minus", "key0", "key_plus", "audition", "mode"};

int find(const char *const *names, int n, const std::string &s)
{
	for (int i = 0; i < n; ++i)
		if (s == names[i])
			return i;
	std::fprintf(stderr, "unknown name %s\n", s.c_str());
	std::exit(2);
}

struct Event {
	enum Kind { Pot, Switch, Button, Midi } kind;
	int index = 0;
	double value = 0;
	std::vector<uint8_t> bytes;
};

void writeWav(const std::string &path, const std::vector<float> &x, int rate)
{
	std::ofstream f(path, std::ios::binary);
	auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
	auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };
	f.write("RIFF", 4); u32(uint32_t(36 + x.size() * 4)); f.write("WAVE", 4);
	f.write("fmt ", 4); u32(16); u16(3); u16(1); u32(uint32_t(rate)); u32(uint32_t(rate * 4)); u16(4); u16(32);
	f.write("data", 4); u32(uint32_t(x.size() * 4));
	f.write(reinterpret_cast<const char *>(x.data()), std::streamsize(x.size() * 4));
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "usage: bsrackrender <rom> <out.wav> [seconds] [--rate=] [--pot=] [--switch=] [--button=] [--press=] [--note=] [--midi=]\n");
		return 2;
	}
	std::ifstream rf(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
	double seconds = 5;
	int rate = 48000;
	std::multimap<long, Event> events; // by sample
	Controls c;
	const double defaults[kPots] = {0.7, 0, 0.5, 0, 0.5, 0.5, 0, 0.5, 0.5, 0.5, 0.6, 0, 0.5, 0, 0.4, 1, 0.3, 0};
	for (int i = 0; i < kPots; ++i)
		c.pots[size_t(i)] = defaults[i];
	c.switches = {0, 1, 0, 1, 0, 0, 0, 1, 1, 1, 0, 2};
	std::vector<std::string> args(argv + 3, argv + argc);
	std::vector<std::pair<std::string, double>> parts;
	for (const auto &a : args)
		if (a.rfind("--rate=", 0) == 0)
			rate = std::atoi(a.c_str() + 7);
	auto at = [&](const std::string &s) {
		const auto p = s.find('@');
		return p == std::string::npos ? 0L : long(std::atof(s.c_str() + p + 1) * 1e-3 * rate);
	};
	for (const auto &a : args) {
		if (a.rfind("--rate=", 0) == 0)
			continue;
		if (a[0] != '-') {
			seconds = std::atof(a.c_str());
			continue;
		}
		const auto eq = a.find('=');
		const std::string key = a.substr(2, eq - 2), val = a.substr(eq + 1);
		const std::string head = val.substr(0, val.find_first_of(":@"));
		if (key == "pot" || key == "switch" || key == "button") {
			const double v = std::atof(val.c_str() + val.find(':') + 1);
			Event e{key == "pot" ? Event::Pot : key == "switch" ? Event::Switch : Event::Button};
			e.index = key == "pot" ? find(kPotNames, kPots, head) : key == "switch" ? find(kSwitchNames, kSwitches, head) : find(kButtonNames, kButtons, head);
			e.value = v;
			events.insert({at(val), e});
		} else if (key == "press") {
			Event e{Event::Button};
			e.index = find(kButtonNames, kButtons, head);
			e.value = 1;
			events.insert({at(val), e});
			e.value = 0;
			events.insert({at(val) + long(0.06 * rate), e});
		} else if (key == "note") { // MS:NOTE:VEL:LEN
			double ms, len;
			int note, vel;
			std::sscanf(val.c_str(), "%lf:%d:%d:%lf", &ms, &note, &vel, &len);
			Event on{Event::Midi}, off{Event::Midi};
			on.bytes = {0x90, uint8_t(note), uint8_t(vel)};
			off.bytes = {0x80, uint8_t(note), 0x40};
			events.insert({long(ms * 1e-3 * rate), on});
			events.insert({long((ms + len) * 1e-3 * rate), off});
		} else if (key == "part") { // NAME=VALUE: a VoiceParts field
			const auto e2 = val.find('=');
			parts.push_back({val.substr(0, e2), std::atof(val.c_str() + e2 + 1)});
		} else if (key == "midi") { // MS:HH,HH
			Event e{Event::Midi};
			const auto colon = val.find(':');
			for (const char *p = val.c_str() + colon + 1; *p;) {
				e.bytes.push_back(uint8_t(std::strtoul(p, const_cast<char **>(&p), 16)));
				if (*p == ',')
					++p;
			}
			events.insert({long(std::atof(val.c_str()) * 1e-3 * rate), e});
		} else {
			std::fprintf(stderr, "unknown option %s\n", a.c_str());
			return 2;
		}
	}

	Machine m(rom);
	if (!m.romLoaded()) {
		std::fprintf(stderr, "not a 32 KB ROM: %s\n", argv[1]);
		return 1;
	}
	m.prepare(rate);
	if (!parts.empty()) {
		VoiceParts vp = m.voiceParts();
		const std::map<std::string, double *> fields = {
			{"dacFull", &vp.dacFull}, {"cvTau", &vp.cvTau}, {"volTau", &vp.volTau}, {"fmaBeta", &vp.fmaBeta},
			{"dcoRin", &vp.dcoRin}, {"dcoRgnd", &vp.dcoRgnd}, {"dcoRref", &vp.dcoRref}, {"dcoCap", &vp.dcoCap},
			{"r31", &vp.r31}, {"r32", &vp.r32}, {"r33", &vp.r33}, {"r34", &vp.r34}, {"r35", &vp.r35},
			{"vcfRin", &vp.vcfRin}, {"vcfRgnd", &vp.vcfRgnd}, {"vcfRref", &vp.vcfRref}, {"r58", &vp.r58},
			{"vcaRin", &vp.vcaRin}, {"vcaRgnd", &vp.vcaRgnd}, {"vcaRref", &vp.vcaRref}, {"r79", &vp.r79},
			{"vcaTau", &vp.vcaTau}, {"r73", &vp.r73}, {"r74", &vp.r74}, {"r80", &vp.r80}, {"outVolts", &vp.outVolts},
			{"c33", &vp.c33}, {"abcDrop", &vp.abcDrop}, {"resFloor", &vp.resFloor}, {"vcfFloor", &vp.vcfFloor}};
		for (const auto &[name, v] : parts) {
			const auto f = fields.find(name);
			if (f == fields.end()) {
				std::fprintf(stderr, "unknown part %s\n", name.c_str());
				return 2;
			}
			*f->second = v;
		}
		m.setVoiceParts(vp);
	}
	m.setControls(c);
	const long total = long(seconds * rate);
	std::vector<float> out(size_t(total), 0.0f);
	long pos = 0;
	auto it = events.begin();
	while (pos < total) {
		while (it != events.end() && it->first <= pos) {
			const auto &e = it->second;
			if (e.kind == Event::Pot)
				c.pots[size_t(e.index)] = e.value;
			else if (e.kind == Event::Switch)
				c.switches[size_t(e.index)] = int(e.value);
			else if (e.kind == Event::Button)
				m.setButton(e.index, e.value >= 0.5);
			else
				for (uint8_t b : e.bytes)
					m.midiIn(b);
			m.setControls(c);
			++it;
		}
		long end = it != events.end() ? std::min(total, std::max(pos + 1, it->first)) : total;
		// BSRACK_RENDER_TRACE=<ms>: a line every that many milliseconds too (1: at the events only)
		if (const char *tr = std::getenv("BSRACK_RENDER_TRACE"); tr != nullptr && std::atof(tr) > 1)
			end = std::min(end, pos + std::max(1L, long(rate * std::atof(tr) / 1000)));
		m.render(out.data() + pos, int(end - pos), nullptr);
		pos = end;
		if (std::getenv("BSRACK_RENDER_TRACE")) {
			const auto &fr = m.frame();
			std::printf("%7.3f s: vca %03X vcf %03X res %03X volA %02X volB %02X gate %d asleep %d osc %03X/%03X dropped %d\n", double(pos) / rate,
			            fr.dac[kVcaCv], fr.dac[kVcfCv], fr.dac[kResCv], fr.volA, fr.volB, int(fr.gateOut), int(m.asleep()), fr.dac[kOscACv],
			            fr.dac[kOscBCv], int(m.droppedEdges()));
		}
	}
	writeWav(argv[2], out, rate);
	float peak = 0;
	for (float v : out)
		peak = std::max(peak, std::abs(v));
	const auto d = m.display();
	std::printf("%s: %.2f s at %d Hz, peak %.4f, display %02x %02x\n", argv[2], seconds, rate, peak, d.digits[0], d.digits[1]);
	return 0;
}
