// tr808seq: drives the TR-808 CPU board (the real firmware) from a script and logs what the
// trigger circuit would receive: each step pulse with the instrument data held on the ports.
// Used to check the I/O map and the sequencer against the owner's manual (docs/firmware.md).
//
//   tr808seq ROM SCRIPT|- [--ram FILE] [--save-ram FILE] [--rate HZ]
//
// Script commands (one per line; '#' starts a comment):
//   mode clear|1st|2nd|manual|play|compose   MODE
//   inst N                  INSTRUMENT-SELECT / RHYTHM TRACK, 1-12 (or AC BD SD ... CH)
//   fill manual|16|12|8|4|2 AUTO FILL IN
//   var a|ab|b              BASIC VARIATION
//   ifvar a|b               I/F VARIATION
//   prescale N              PRE-SCALE, 1-4
//   press KEY [ms]          press and release (default 150 ms down, 150 ms up); KEY = 1-16, CLEAR,
//                           TAP, START (the START/STOP key: toggles the run flip-flop)
//   hold KEY / release KEY
//   wait MS
//   bpm N                   internal tempo (24 ppqn)
//   din on [PHASE] | off    DIN sync from now: run high and a 24 ppqn clock starting at PHASE
//                           of a period (0: rising now), high for each period's first half
//   pattern N A|B PS STEPS  write BASIC RHYTHM / INTRO N (1-16), variation, pre-scale 1-4, steps
//                           (voices joined by +: bd+ch+ac, or - for none) straight into RAM
//   factory                 the factory rhythms into RAM (as a new instance), then power on
//   leds                    print the lit LEDs
//   ram                     print the RAM's non-zero nibbles
//   cpuram                  print the CPU's own RAM
//   watch HEX [history]     log changes of a CPU RAM nibble, with the PC (and the instructions before)
//   ramdiff                 print the RAM nibbles changed since the last ramdiff
//   quiet on|off            suppress the trigger log
//   status run|fill|reset|clock on|off   force a status input (tools: find the status map)
#include "tr808/core/machine.h"
#include "tr808/core/voiceboard.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using tr808::Machine;

namespace {
const char *kVoiceNames[] = {"BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH", "AC"};
const char *kInstNames[] = {"AC", "BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH"};

std::string dataText(uint16_t d)
{
	std::string s;
	for (int b = 0; b < 12; ++b)
		if (d & (1u << b)) {
			if (!s.empty())
				s += ' ';
			s += kVoiceNames[b];
		}
	return s.empty() ? "-" : s;
}

struct Sim {
	Machine m;
	double rate = 48000;
	double t = 0;
	double bpm = 120;
	tr808::TempoClock clock;
	bool runFF = false, forcedRun = false, forceRun = false;
	bool din = false;      // DIN sync: run and a 24 ppqn clock from t0, high for the first half
	double dinStart = 0, dinPhase = 0;
	bool quiet = false;
	unsigned lastPulses = 0;
	double monoUntil = -1;
	uint16_t heldData = 0;
	double lastStep = -1;
	int watch = -1; // CPU RAM nibble to watch
	bool watchHistory = false;

	void advance(double ms)
	{
		const int n = int(ms * rate / 1000 + 0.5);
		for (int i = 0; i < n; ++i) {
			const bool run = din || (forceRun ? forcedRun : runFF);
			m.setRun(run);
			bool level = clock.advance(1 / rate, bpm, run && !din);
			if (din) {
				const double ph = (t - dinStart) * bpm / 60.0 * 24.0 + dinPhase;
				level = ph - std::floor(ph) < 0.5;
			}
			m.setClock(level);
			const int before = watch >= 0 ? m.cpu().ram()[watch] & 0xf : 0;
			m.tick(rate);
			if (watch >= 0 && (m.cpu().ram()[watch] & 0xf) != before) {
				std::printf("%9.2f ms  ram %02x: %x -> %x  (pc %03x)\n", t * 1000, watch, before, m.cpu().ram()[watch] & 0xf, m.cpu().pc());
				if (watchHistory) {
					uint16_t pcs[64];
					uint8_t accs[64];
					m.cpu().history(pcs, accs);
					std::printf("    ");
					for (int k = 0; k < 64; ++k)
						std::printf(" %03x/%x", pcs[k], accs[k]);
					std::printf("\n");
				}
			}
			if (m.stepPulses() != lastPulses) {
				lastPulses = m.stepPulses();
				monoUntil = t + 0.001;
				heldData = 0;
			}
			// What the 1 ms common trig lets through: the data lines high at any time during it.
			if (t < monoUntil) {
				heldData |= m.instrumentData();
				if (t + 1 / rate >= monoUntil && !quiet) {
					std::printf("%9.2f ms  step  %-30s", (monoUntil - 0.001) * 1000, dataText(heldData).c_str());
					if (lastStep >= 0)
						std::printf("  +%.2f ms", (monoUntil - 0.001 - lastStep) * 1000);
					std::printf("\n");
					lastStep = monoUntil - 0.001;
				}
			}
			t += 1 / rate;
		}
	}

	void leds() const
	{
		std::printf("%9.2f ms  leds  steps", t * 1000);
		for (int s = 0; s < 16; ++s)
			if (m.stepLed(s) > 0.3f)
				std::printf(" %d", s + 1);
		static const char *names[] = {"A", "B", "1st", "2nd"};
		for (int l = 0; l < 4; ++l)
			if (m.led(Machine::Led(l)) > 0.5f)
				std::printf("  %s", names[l]);
		std::printf("  (run %d)\n", m.running());
	}
};

bool setKey(Sim &s, const std::string &key, bool down)
{
	if (key == "CLEAR")
		s.m.setClearKey(down);
	else if (key == "TAP")
		s.m.setFillIn(down);
	else if (key == "START") {
		if (down)
			s.runFF = !s.runFF;
	} else if (!key.empty() && std::isdigit(static_cast<unsigned char>(key[0]))) {
		const int k = std::atoi(key.c_str());
		if (k < 1 || k > 16)
			return false;
		s.m.setStepKey(k - 1, down);
	} else
		return false;
	return true;
}
} // namespace

int main(int argc, char **argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "usage: tr808seq ROM SCRIPT|- [--ram FILE] [--save-ram FILE] [--rate HZ]\n");
		return 2;
	}
	Sim s;
	std::string ramIn, ramOut;
	for (int a = 3; a < argc; ++a) {
		const std::string arg = argv[a];
		if (arg == "--ram" && a + 1 < argc)
			ramIn = argv[++a];
		else if (arg == "--save-ram" && a + 1 < argc)
			ramOut = argv[++a];
		else if (arg == "--rate" && a + 1 < argc)
			s.rate = std::atof(argv[++a]);
	}
	{
		std::ifstream f(argv[1], std::ios::binary);
		std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		if (!s.m.setRom(rom.data(), rom.size())) {
			std::fprintf(stderr, "can't load ROM %s\n", argv[1]);
			return 1;
		}
	}
	if (!ramIn.empty()) {
		std::ifstream f(ramIn, std::ios::binary);
		std::vector<uint8_t> ram((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		s.m.setRam(ram.data(), ram.size());
	}
	s.m.powerOn();

	std::istream *in = &std::cin;
	std::ifstream file;
	if (std::strcmp(argv[2], "-") != 0) {
		file.open(argv[2]);
		if (!file) {
			std::fprintf(stderr, "can't read %s\n", argv[2]);
			return 1;
		}
		in = &file;
	}
	std::string line;
	int lineNo = 0;
	while (std::getline(*in, line)) {
		++lineNo;
		if (const auto hash = line.find('#'); hash != std::string::npos)
			line.resize(hash);
		std::istringstream ls(line);
		std::string cmd;
		if (!(ls >> cmd))
			continue;
		std::string a, b;
		ls >> a >> b;
		for (auto &c : a)
			c = char(std::toupper(static_cast<unsigned char>(c)));
		auto fail = [&]() {
			std::fprintf(stderr, "line %d: can't do '%s'\n", lineNo, line.c_str());
			std::exit(1);
		};
		if (cmd == "mode") {
			static const char *modes[] = {"CLEAR", "1ST", "2ND", "MANUAL", "PLAY", "COMPOSE"};
			int m = -1;
			for (int k = 0; k < 6; ++k)
				if (a == modes[k])
					m = k;
			if (m < 0)
				fail();
			s.m.setMode(Machine::Mode(m));
		} else if (cmd == "inst") {
			int k = std::atoi(a.c_str()) - 1;
			for (int i = 0; i < 12; ++i)
				if (a == kInstNames[i])
					k = i;
			if (k < 0 || k > 11)
				fail();
			s.m.setInstrument(k);
		} else if (cmd == "fill") {
			static const char *fills[] = {"MANUAL", "16", "12", "8", "4", "2"};
			int m = -1;
			for (int k = 0; k < 6; ++k)
				if (a == fills[k])
					m = k;
			if (m < 0)
				fail();
			s.m.setAutoFill(Machine::AutoFill(m));
		} else if (cmd == "var") {
			s.m.setVariation(a == "A" ? Machine::Variation::A : a == "AB" ? Machine::Variation::AB : Machine::Variation::B);
		} else if (cmd == "ifvar") {
			s.m.setIfVariationB(a == "B");
		} else if (cmd == "prescale") {
			s.m.setPrescale(std::atoi(a.c_str()) - 1);
		} else if (cmd == "press") {
			const double ms = b.empty() ? 150 : std::atof(b.c_str());
			if (!setKey(s, a, true))
				fail();
			s.advance(ms);
			setKey(s, a, false);
			s.advance(ms);
		} else if (cmd == "hold" || cmd == "release") {
			if (!setKey(s, a, cmd == "hold"))
				fail();
		} else if (cmd == "wait") {
			s.advance(std::atof(a.c_str()));
		} else if (cmd == "din") {
			// din on [PHASE]: DIN sync starts now (run high, the clock at PHASE of a period); din off
			s.din = a == "ON";
			s.dinStart = s.t;
			s.dinPhase = b.empty() ? 0 : std::atof(b.c_str());
		} else if (cmd == "bpm") {
			s.bpm = std::atof(a.c_str());
		} else if (cmd == "leds") {
			s.leds();
		} else if (cmd == "ram") {
			const auto &r = s.m.ram();
			for (int chip = 0; chip < 4; ++chip)
				for (int row = 0; row < 64; ++row) {
					bool any = false;
					for (int k = 0; k < 16; ++k)
						any = any || r[size_t(chip * 1024 + row * 16 + k)] != 0;
					if (!any)
						continue;
					std::printf("  %d.%03x:", chip, row * 16);
					for (int k = 0; k < 16; ++k)
						std::printf(" %x", r[size_t(chip * 1024 + row * 16 + k)]);
					std::printf("\n");
				}
		} else if (cmd == "pattern") {
			// pattern N A|B PRESCALE STEP STEP ...   (STEP: voices joined by +, e.g. bd+ch+ac, or -)
			std::istringstream rest(line);
			std::string w, var, ps;
			int num = 0;
			rest >> w >> num >> var >> ps;
			std::vector<uint16_t> steps;
			while (rest >> w) {
				uint16_t bits = 0;
				std::stringstream vs(w);
				std::string v;
				while (std::getline(vs, v, '+')) {
					for (auto &c : v)
						c = char(std::toupper(static_cast<unsigned char>(c)));
					for (int b = 0; b < 12; ++b)
						if (v == kVoiceNames[b])
							bits |= uint16_t(1u << b);
				}
				steps.push_back(bits);
			}
			if (num < 1 || num > 16 || steps.empty())
				fail();
			s.m.writePattern(num - 1, var == "b" || var == "B" ? 1 : 0, steps, std::atoi(ps.c_str()) - 1);
		} else if (cmd == "factory") {
			s.m.writeFactoryRhythms();
			s.m.powerOn();
		} else if (cmd == "watch") {
			s.watch = int(std::strtol(a.c_str(), nullptr, 16));
			s.watchHistory = b == "history" || b == "HISTORY";
		} else if (cmd == "ramdiff") {
			static std::vector<uint8_t> last(s.m.ram().begin(), s.m.ram().end());
			const auto &r = s.m.ram();
			for (size_t i = 0; i < r.size(); ++i)
				if (r[i] != last[i])
					std::printf("  %zu.%03zx: %x -> %x\n", i / 1024, i % 1024, last[i], r[i]);
			last.assign(r.begin(), r.end());
		} else if (cmd == "cpuram") {
			const uint8_t *r = s.m.cpu().ram();
			for (int row = 0; row < 8; ++row) {
				std::printf("  %02x:", row * 16);
				for (int k = 0; k < 16; ++k)
					std::printf(" %x", r[row * 16 + k] & 0xf);
				std::printf("\n");
			}
		} else if (cmd == "quiet") {
			s.quiet = a == "ON";
		} else if (cmd == "status") {
			for (auto &c : b)
				c = char(std::tolower(static_cast<unsigned char>(c)));
			const bool on = b == "on";
			if (a == "RUN") {
				s.forceRun = true;
				s.forcedRun = on;
			} else if (a == "FILL")
				s.m.setFillIn(on);
			else if (a == "RESET")
				s.m.setResetStart(on);
			else
				fail();
		} else {
			fail();
		}
	}
	if (!ramOut.empty()) {
		std::ofstream f(ramOut, std::ios::binary);
		f.write(reinterpret_cast<const char *>(s.m.ram().data()), std::streamsize(s.m.ram().size()));
	}
	std::printf("%ld RAM writes\n", s.m.ramWrites());
	return 0;
}
