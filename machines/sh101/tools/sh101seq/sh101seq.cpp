// sh101seq: drives the SH-101 CPU board (the real firmware on the MCS-48 core) from a script
// and logs what the synth board would receive: the three sample-and-holds (KCV, RANGE,
// RANDOM), P2 (gate, PORTA OFF, CLK RST, LEDs) and RAM, against emulated milliseconds.
//
//   sh101seq [--rom ROM] SCRIPT|-  [--ram FILE] [--save-ram FILE] [--rate HZ]
//   sh101seq [--rom ROM] --selftest [--long]
//
// Script commands (one per line or separated by ';'; '#' at line start or after a space
// starts a comment):
//   power [blank|keep]      CPU reset (blank: zero the RAM first)
//   testmode                power on holding LOAD + KEY TRANSPOSE
//   key NOTE [down [up]]    press and release a key (ms, default 50/50); NOTE = F3..C6 or k0..k31
//   down NOTE.. / up NOTE.. hold / release keys (no wait)
//   btn NAME [down [up]]    press a button: LOAD PLAY DOWN UD UP HOLD KT
//   bdown NAME / bup NAME
//   range 16|8|4|2   trig gt|gate|lfo   tr L|M|H   pedal on|off
//   clock HZ [DUTY] | clock off | clk 0|1      the CLK input (square wave or a level)
//   wait MS
//   log lines|norandom|dac|p2|off              what to print (default lines)
//   ram [FROM [TO]]         hex dump (default 00-7F); mark / diff: RAM changes since mark
//   seq                     the decoded sequence; loop: the last loop period; leds; t: T flags
//   write STEP..            write a sequence into RAM: F3 C4~ rest .. (pitch at 8', ~ = legato)
//   stats MS                loop periods over the next MS
//   set ADDR BYTE..         poke RAM (hex)
//   say TEXT
#include "sim.h"

#include <cstring>
#include <iostream>
#include <map>
#include <sstream>

using namespace sh101test;
using Button = Machine::Button;

int runSelfTest(const std::vector<uint8_t> &rom, bool longRuns);

namespace {
const std::map<std::string, Button> kButtons = {
	{"LOAD", Button::Load}, {"PLAY", Button::Play}, {"DOWN", Button::ArpDown}, {"UD", Button::ArpUpDown},
	{"U&D", Button::ArpUpDown}, {"UP", Button::ArpUp}, {"HOLD", Button::Hold}, {"LEGATO", Button::Hold},
	{"KT", Button::KeyTranspose}, {"REST", Button::KeyTranspose}, {"KEYTR", Button::KeyTranspose}};
const char *kLedNames[] = {"LOAD", "PLAY", "DOWN", "U&D", "UP", "HOLD", "KT"};

struct Printer {
	enum Mode { Off, Lines, NoRandom, Dac, P2 } mode = Lines;
	size_t next = 0;
	sh101::CpuLines last;
	uint8_t lastP2 = 0;
	bool first = true;

	static std::string leds(uint8_t p2)
	{
		std::string s;
		const int code = p2 & 7;
		if (code >= 1 && code <= 5)
			s += kLedNames[code - 1];
		if (!(p2 & 0x20))
			s += s.empty() ? "HOLD" : "|HOLD";
		if (p2 & 0x40)
			s += s.empty() ? "KT" : "|KT";
		return s.empty() ? "-" : s;
	}
	void flush(Sim &sim)
	{
		for (; next < sim.events.size(); ++next) {
			const Event &e = sim.events[next];
			if (mode == Dac && e.kind == Event::Dac) {
				std::printf("%10.3f ms  DAC ch%d = %2d (%.3f V)\n", e.t * 1000, e.channel, e.value, e.value / 12.0);
				continue;
			}
			if (mode == P2 && e.kind == Event::P2) {
				std::printf("%10.3f ms  P2 = %02X  gate %d porta-off %d clk-rst %d  %s\n", e.t * 1000, e.value,
				            !(e.value & 0x80), (e.value & 0x08) != 0, !(e.value & 0x10), leds(uint8_t(e.value)).c_str());
				continue;
			}
			sh101::CpuLines l = last;
			uint8_t p2 = lastP2;
			if (e.kind == Event::Dac) {
				(e.channel == 0 ? l.kcv : e.channel == 1 ? l.range : l.random) = e.value;
			} else {
				p2 = uint8_t(e.value);
				l.gate = !(p2 & 0x80);
				l.portaOff = (p2 & 0x08) != 0;
				l.clkRst = !(p2 & 0x10);
			}
			const bool changed = first || l.kcv != last.kcv || l.range != last.range || l.gate != last.gate ||
			                     l.portaOff != last.portaOff || l.clkRst != last.clkRst || p2 != lastP2 ||
			                     (mode == Lines && l.random != last.random);
			if (changed && (mode == Lines || mode == NoRandom))
				std::printf("%10.3f ms  KCV %2d %-4s(%.3f V)  RANGE %2d  RND %2d  gate %d  porta-off %d  clk-rst %d  P2 %02X %s\n",
				            e.t * 1000, l.kcv, noteName(l.kcv).c_str(), l.kcv / 12.0, l.range, l.random, l.gate, l.portaOff,
				            l.clkRst, p2, leds(p2).c_str());
			last = l;
			lastP2 = p2;
			first = false;
		}
	}
};

void dumpRam(const Machine &m, int from, int to)
{
	const auto r = m.ram();
	for (int a = from & ~15; a <= to; a += 16) {
		std::printf("  %02X:", a);
		for (int i = 0; i < 16; ++i)
			if (a + i >= from && a + i <= to)
				std::printf(" %02X", r[size_t(a + i)]);
			else
				std::printf("   ");
		std::printf("\n");
	}
}

// Steps are named by the pitch they play at 8' with no key transpose: note n = MIDI 41 + n
// (KCV n + 5), so "F3" is F3 entered at TRANSPOSE M. "~" marks a legato step.
std::string stepText(const Machine::Step &s)
{
	if (s.rest)
		return "rest";
	const std::string r = noteName(s.note + 5);
	return s.legato ? r + "~" : r;
}

bool parseStep(std::string t, Machine::Step &s)
{
	s = {};
	if (t == "rest" || t == "r")
		return s.rest = true;
	if (!t.empty() && t.back() == '~') {
		s.legato = true;
		t.pop_back();
	}
	for (int n = 0; n <= 58; ++n)
		if (noteName(n + 5) == t) {
			s.note = n;
			return true;
		}
	return false;
}
} // namespace

int main(int argc, char **argv)
{
	std::string romPath = SH101_ROM_PATH, script, ramIn, ramOut;
	bool selftest = false, longRuns = false;
	double rate = 200000;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--rom" && i + 1 < argc)
			romPath = argv[++i];
		else if (a == "--selftest")
			selftest = true;
		else if (a == "--long")
			longRuns = true;
		else if (a == "--ram" && i + 1 < argc)
			ramIn = argv[++i];
		else if (a == "--save-ram" && i + 1 < argc)
			ramOut = argv[++i];
		else if (a == "--rate" && i + 1 < argc)
			rate = std::atof(argv[++i]);
		else
			script = a;
	}
	const auto rom = loadRom(romPath.c_str());
	if (rom.size() != 2048) {
		std::fprintf(stderr, "cannot read a 2048-byte ROM from %s\n", romPath.c_str());
		return 2;
	}
	if (selftest)
		return runSelfTest(rom, longRuns);
	if (script.empty()) {
		std::fprintf(stderr, "usage: sh101seq [--rom ROM] SCRIPT|- [--ram FILE] [--save-ram FILE]\n"
		                     "       sh101seq [--rom ROM] --selftest [--long]\n");
		return 2;
	}

	Sim sim(rom);
	sim.rate = rate;
	if (!ramIn.empty()) {
		const auto r = loadRom(ramIn.c_str());
		sim.m.setRam(r.data(), r.size());
	}
	Printer out;
	std::array<uint8_t, Machine::kRamBytes> mark = sim.m.ram();

	std::ifstream file;
	std::istream *in = &std::cin;
	if (script != "-") {
		file.open(script);
		if (!file) {
			std::fprintf(stderr, "cannot open %s\n", script.c_str());
			return 2;
		}
		in = &file;
	}
	std::string text((std::istreambuf_iterator<char>(*in)), {});
	for (auto &c : text)
		if (c == ';')
			c = '\n';
	std::istringstream lines(text);
	std::string line;
	bool powered = false;
	auto ensurePower = [&] {
		if (!powered) {
			sim.powerOn();
			powered = true;
		}
	};
	while (std::getline(lines, line)) {
		if (!line.empty() && line[0] == '#')
			line.clear();
		if (auto h = line.find(" #"); h != std::string::npos)
			line.resize(h);
		std::istringstream ss(line);
		std::string cmd;
		if (!(ss >> cmd))
			continue;
		std::vector<std::string> args;
		for (std::string a; ss >> a;)
			args.push_back(a);
		auto arg = [&](size_t i, double def) { return i < args.size() ? std::atof(args[i].c_str()) : def; };
		auto keyArg = [&](const std::string &s) {
			const int k = keyIndex(s);
			if (k < 0 || k >= Machine::kKeys) {
				std::fprintf(stderr, "unknown key %s\n", s.c_str());
				std::exit(1);
			}
			return k;
		};
		auto buttonArg = [&](const std::string &s) {
			auto it = kButtons.find(s);
			if (it == kButtons.end()) {
				std::fprintf(stderr, "unknown button %s\n", s.c_str());
				std::exit(1);
			}
			return it->second;
		};
		if (cmd == "power") {
			if (!args.empty() && args[0] == "blank") {
				std::array<uint8_t, Machine::kRamBytes> z{};
				sim.m.setRam(z.data(), z.size());
			}
			std::printf("%10.3f ms  -- power on\n", sim.now() * 1000);
			sim.powerOn();
			powered = true;
		} else if (cmd == "testmode") {
			sim.m.setButton(Button::Load, true);
			sim.m.setButton(Button::KeyTranspose, true);
			std::printf("%10.3f ms  -- power on holding LOAD + KEY TRANSPOSE\n", sim.now() * 1000);
			sim.powerOn();
			powered = true;
			sim.ms(50);
			sim.m.setButton(Button::Load, false);
			sim.m.setButton(Button::KeyTranspose, false);
		} else if (cmd == "key") {
			ensurePower();
			sim.press(keyArg(args.at(0)), arg(1, 50), arg(2, 50));
		} else if (cmd == "down" || cmd == "up") {
			ensurePower();
			for (auto &a : args)
				sim.key(keyArg(a), cmd == "down");
		} else if (cmd == "btn") {
			ensurePower();
			sim.button(buttonArg(args.at(0)), arg(1, 50), arg(2, 50));
		} else if (cmd == "bdown" || cmd == "bup") {
			ensurePower();
			for (auto &a : args)
				sim.m.setButton(buttonArg(a), cmd == "bdown");
		} else if (cmd == "range") {
			const int r = int(arg(0, 8));
			sim.m.setRange(r == 16 ? sh101::Range::Ft16 : r == 8 ? sh101::Range::Ft8 : r == 4 ? sh101::Range::Ft4 : sh101::Range::Ft2);
		} else if (cmd == "trig") {
			const std::string a = args.at(0);
			sim.m.setTrigger(a == "gt" ? sh101::Trigger::GateTrig : a == "lfo" ? sh101::Trigger::Lfo : sh101::Trigger::Gate);
		} else if (cmd == "tr") {
			const std::string a = args.at(0);
			sim.m.setTranspose(a == "L" ? sh101::Transpose::Low : a == "H" ? sh101::Transpose::High : sh101::Transpose::Mid);
		} else if (cmd == "pedal") {
			sim.m.setHoldPedal(args.at(0) == "on");
		} else if (cmd == "clock") {
			if (args.at(0) == "off")
				sim.setClockLevel(false);
			else
				sim.setClockHz(arg(0, 0), arg(1, 0.5));
		} else if (cmd == "clk") {
			sim.setClockLevel(arg(0, 0) != 0);
		} else if (cmd == "wait") {
			ensurePower();
			sim.ms(arg(0, 0));
		} else if (cmd == "log") {
			const std::string a = args.empty() ? "lines" : args[0];
			out.flush(sim);
			out.mode = a == "off" ? Printer::Off : a == "dac" ? Printer::Dac : a == "p2" ? Printer::P2 : a == "norandom" ? Printer::NoRandom : Printer::Lines;
		} else if (cmd == "ram") {
			out.flush(sim);
			std::printf("%10.3f ms  RAM\n", sim.now() * 1000);
			dumpRam(sim.m, int(std::strtol(args.size() > 0 ? args[0].c_str() : "0", nullptr, 16)),
			        int(std::strtol(args.size() > 1 ? args[1].c_str() : "7f", nullptr, 16)));
		} else if (cmd == "mark") {
			mark = sim.m.ram();
		} else if (cmd == "diff") {
			out.flush(sim);
			const auto r = sim.m.ram();
			std::printf("%10.3f ms  RAM diff:", sim.now() * 1000);
			for (int a = 0; a < Machine::kRamBytes; ++a)
				if (r[size_t(a)] != mark[size_t(a)])
					std::printf(" %02X:%02X>%02X", a, mark[size_t(a)], r[size_t(a)]);
			std::printf("\n");
			mark = r;
		} else if (cmd == "set") {
			auto r = sim.m.ram();
			int a = int(std::strtol(args.at(0).c_str(), nullptr, 16));
			for (size_t i = 1; i < args.size() && a < Machine::kRamBytes; ++i)
				r[size_t(a++)] = uint8_t(std::strtol(args[i].c_str(), nullptr, 16));
			sim.m.setRam(r.data(), r.size());
		} else if (cmd == "write") {
			std::vector<Machine::Step> steps;
			for (auto &a : args) {
				Machine::Step st;
				if (!parseStep(a, st)) {
					std::fprintf(stderr, "bad step %s\n", a.c_str());
					return 1;
				}
				steps.push_back(st);
			}
			sim.m.writeSequence(steps);
		} else if (cmd == "seq") {
			out.flush(sim);
			const auto s = sim.m.readSequence();
			std::printf("%10.3f ms  sequence (%zu steps):", sim.now() * 1000, s.size());
			for (const auto &st : s)
				std::printf(" %s", stepText(st).c_str());
			std::printf("\n");
		} else if (cmd == "loop") {
			out.flush(sim);
			std::printf("%10.3f ms  last loop %.3f ms\n", sim.now() * 1000, sim.m.lastLoopSeconds() * 1000);
		} else if (cmd == "t") {
			out.flush(sim);
			std::printf("%10.3f ms  T = %02X  P2 = %02X\n", sim.now() * 1000, sim.m.tFlags(), sim.m.p2());
		} else if (cmd == "leds") {
			out.flush(sim);
			std::printf("%10.3f ms  LEDs:", sim.now() * 1000);
			for (int l = 0; l < int(Machine::Led::Count); ++l)
				if (sim.m.led(Machine::Led(l)) > 0.05f)
					std::printf(" %s(%.2f)", kLedNames[l], sim.m.led(Machine::Led(l)));
			std::printf("\n");
		} else if (cmd == "stats") {
			// stats MS: loop period (P2 write interval) over the next MS
			out.flush(sim);
			const double t0 = sim.now();
			const size_t from = sim.events.size();
			sim.ms(arg(0, 100));
			double last = -1, mn = 1e9, mx = 0, sum = 0;
			int n = 0;
			for (size_t i = from; i < sim.events.size(); ++i) {
				const auto &e = sim.events[i];
				if (e.kind != Event::P2)
					continue;
				if (last >= 0) {
					const double d = e.t - last;
					mn = std::min(mn, d);
					mx = std::max(mx, d);
					sum += d;
					++n;
				}
				last = e.t;
			}
			std::printf("%10.3f ms  loop over %.0f ms: %d loops, min %.3f mean %.3f max %.3f ms\n", t0 * 1000, arg(0, 100), n,
			            mn * 1000, n ? sum / n * 1000 : 0, mx * 1000);
		} else if (cmd == "say") {
			out.flush(sim);
			std::printf("%10.3f ms  -- %s\n", sim.now() * 1000, line.substr(line.find("say") + 4).c_str());
		} else {
			std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
			return 1;
		}
		out.flush(sim);
	}
	out.flush(sim);
	if (!ramOut.empty()) {
		const auto r = sim.m.ram();
		std::ofstream f(ramOut, std::ios::binary);
		f.write(reinterpret_cast<const char *>(r.data()), std::streamsize(r.size()));
	}
	return 0;
}
