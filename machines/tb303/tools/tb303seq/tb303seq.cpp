// tb303seq: drives the TB-303 CPU board (real firmware) from a script and logs what the
// voice board would receive. Used to check the I/O map and the sequencer against the
// owner's manual.
//
//   tb303seq ROM SCRIPT|- [--ram FILE] [--save-ram FILE]
//
// Script commands (one per line; '#' at line start or after a space starts a comment):
//   mode pw|pp|tw|tp        MODE switch (pattern write/play, track write/play)
//   track N                 TRACK / PATT.GROUP switch, 1-7
//   press KEY [ms]          press and release (default 50 ms down, 50 ms up)
//   hold KEY / release KEY
//   wait MS
//   bpm N                   internal tempo (24 ppqn clock)
//   run on|off              run level
//   leds                    print lit LEDs
//   quiet on|off            suppress line logging
// KEY: C C# D D# E F F# G G# A A# B C2 DOWN UP ACCENT SLIDE CLEAR FUNCTION PITCH TIME BACK TAP
#include "tb303/core/machine.h"
#include "tb303/core/patterns.h"
#include "tb303/core/tempoclock.h"

#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using tb303::Machine;

namespace {
const char *kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C2",
                        "DOWN", "UP", "ACCENT", "SLIDE", "CLEAR", "FUNCTION", "PITCH", "TIME", "BACK", "TAP"};
const char *kLedNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C2",
                           "DOWN", "UP", "ACCENT", "SLIDE", "PITCH", "TIME", "FUNCTION", "RUN"};
const char *kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct Sim {
	Machine m;
	double rate = 44100;
	double t = 0;
	double bpm = 120;
	tb303::TempoClock clock; // internal tempo: runs while stopped, restarts on RUN
	bool quiet = false;
	tb303::CpuLines last;

	void advance(double ms)
	{
		const int n = int(ms * rate / 1000 + 0.5);
		for (int i = 0; i < n; ++i) {
			m.setClock(clock.advance(1 / rate, bpm, m.running()));
			if (m.tick(rate) && !quiet) {
				const auto &l = m.lines();
				std::printf("%9.2f ms  note %2d %-2s%d  gate %d  acc %d  slide %d  strobes %u\n", t * 1000,
				            m.dacCode(), kNoteNames[l.note % 12], l.note / 12 - 1, l.gate, l.accent, l.slide, m.strobes());
			}
			t += 1 / rate;
		}
	}
};

int keyIndex(const std::string &s)
{
	for (int i = 0; i < int(Machine::Key::Count); ++i)
		if (s == kNames[i])
			return i;
	return -1;
}
} // namespace

int main(int argc, char **argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "usage: tb303seq ROM SCRIPT|- [--ram FILE] [--save-ram FILE]\n");
		return 2;
	}
	std::ifstream romf(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(romf)), {});
	Sim sim;
	if (!sim.m.setRom(rom.data(), rom.size())) {
		std::fprintf(stderr, "bad ROM\n");
		return 1;
	}
	std::string saveRam;
	for (int i = 3; i + 1 < argc; ++i) {
		if (!std::strcmp(argv[i], "--ram")) {
			std::ifstream f(argv[++i], std::ios::binary);
			std::vector<uint8_t> ram((std::istreambuf_iterator<char>(f)), {});
			sim.m.setPatternRam(ram.data(), ram.size());
		} else if (!std::strcmp(argv[i], "--save-ram")) {
			saveRam = argv[++i];
		}
	}
	sim.m.powerOn();
	sim.advance(200);

	std::ifstream file;
	std::istream *in = &std::cin;
	if (std::strcmp(argv[2], "-")) {
		file.open(argv[2]);
		in = &file;
	}
	std::string line;
	while (std::getline(*in, line)) {
		// Comments start with '#' at the start of a line or after a space (C# is a key).
		if (!line.empty() && line[0] == '#')
			line.clear();
		if (auto h = line.find(" #"); h != std::string::npos)
			line.resize(h);
		std::istringstream ss(line);
		std::string cmd, arg;
		if (!(ss >> cmd))
			continue;
		ss >> arg;
		if (cmd == "mode") {
			const std::map<std::string, Machine::Mode> modes = {{"tw", Machine::Mode::TrackWrite}, {"tp", Machine::Mode::TrackPlay},
			                                                     {"pp", Machine::Mode::PatternPlay}, {"pw", Machine::Mode::PatternWrite}};
			sim.m.setMode(modes.at(arg));
			sim.advance(20);
		} else if (cmd == "track") {
			sim.m.setTrack(std::atoi(arg.c_str()) - 1);
			sim.advance(20);
		} else if (cmd == "press" || cmd == "hold" || cmd == "release") {
			const int k = keyIndex(arg);
			if (k < 0) {
				std::fprintf(stderr, "unknown key %s\n", arg.c_str());
				return 1;
			}
			std::string ms;
			ss >> ms;
			const double down = ms.empty() ? 50 : std::atof(ms.c_str());
			if (cmd != "release") {
				sim.m.setKey(Machine::Key(k), true);
				sim.advance(cmd == "press" ? down : 20);
			}
			if (cmd != "hold") {
				sim.m.setKey(Machine::Key(k), false);
				sim.advance(cmd == "press" ? 50 : 20);
			}
		} else if (cmd == "raw") {
			// raw COL BIT: press a raw matrix position
			std::string b;
			ss >> b;
			sim.m.setRawKey(std::atoi(arg.c_str()), std::atoi(b.c_str()), true);
			sim.advance(50);
			sim.m.setRawKey(std::atoi(arg.c_str()), std::atoi(b.c_str()), false);
			sim.advance(50);
		} else if (cmd == "write") {
			// write GROUP SECTION PATTERN STEP... ; STEP = KEY[flags]:TIME, KEY C..C2, flags
			// any of u d a s, TIME N/T/R; e.g. "write 1 A 1 C:N D a:N D:T -:R"
			std::string sec, pat;
			ss >> sec >> pat;
			std::string rest;
			std::getline(ss, rest);
			const auto steps = tb303::parsePattern(rest);
			sim.m.writePattern(std::atoi(arg.c_str()) - 1, sec == "B" ? 1 : 0, std::atoi(pat.c_str()) - 1, steps);
		} else if (cmd == "say") {
			std::printf("%9.2f ms  -- %s\n", sim.t * 1000, line.substr(4).c_str());
		} else if (cmd == "wait") {
			sim.advance(std::atof(arg.c_str()));
		} else if (cmd == "bpm") {
			sim.bpm = std::atof(arg.c_str());
		} else if (cmd == "run") {
			sim.m.setRun(arg == "on");
			std::printf("%9.2f ms  run %s\n", sim.t * 1000, arg.c_str());
		} else if (cmd == "quiet") {
			sim.quiet = arg == "on";
		} else if (cmd == "leds") {
			std::printf("%9.2f ms  LEDs:", sim.t * 1000);
			for (int l = 0; l < int(Machine::Led::Count); ++l)
				if (sim.m.led(Machine::Led(l)) > 0.3f)
					std::printf(" %s(%.1f)", kLedNames[l], sim.m.led(Machine::Led(l)));
			std::printf("   matrix %04X\n", sim.m.ledMatrix());
		} else if (cmd == "matrix") {
			// Print averaged matrix bits over the next ARG ms.
			const double ms = std::atof(arg.c_str());
			std::vector<int> count(16, 0);
			const int steps = int(ms);
			for (int i = 0; i < steps; ++i) {
				sim.advance(1);
				for (int b = 0; b < 16; ++b)
					count[size_t(b)] += (sim.m.ledMatrix() >> b) & 1;
			}
			std::printf("%9.2f ms  PC hold %.2f %.2f %.2f %.2f  matrix duty:", sim.t * 1000, sim.m.pcHold(0), sim.m.pcHold(1), sim.m.pcHold(2), sim.m.pcHold(3));
			for (int b = 0; b < 16; ++b)
				if (count[size_t(b)])
					std::printf(" c%dr%d=%.2f", b / 4, b % 4, double(count[size_t(b)]) / steps);
			std::printf("\n");
		} else {
			std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
			return 1;
		}
	}
	if (!saveRam.empty()) {
		std::ofstream f(saveRam, std::ios::binary);
		f.write(reinterpret_cast<const char *>(sim.m.patternRam().data()), std::streamsize(sim.m.patternRam().size()));
	}
	return 0;
}
