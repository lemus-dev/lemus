#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tr909 {

// The production changes, by serial number: the service notes' change table (p.7) and the drawing's
// own notes (p.11, "SN UP to 371499 SURFACE MOUNTED"; R442/R440 annotated). A unit has every change
// whose serial it has reached; the netlists' defaults are the late board, so a change lists the
// .param values its parts had before it (docs/research.md, "Hardware revisions"; each voice's
// docs/circuit/<voice>.md). The firmware's versions are the ROM's (Machine), not the board's.
struct Change {
	int serial;          // the first unit with the change
	const char *parts;   // as the notes name them
	const char *change;  // what changed
	const char *why;     // Roland's reason, or what it does
	std::vector<std::pair<const char *, double>> before; // "voice.param" values before it; empty: not in the model
	std::vector<const char *> refs; // the parts' designators on the boards (the BD's R495 isn't drawn)
};

inline const std::vector<Change> &changes()
{
	static const std::vector<Change> c = {
		{371500, "R495 (BD)", "150k from Q6's base to ground removed", "\"SN UP to 371499 SURFACE MOUNTED\" (p.11)",
		 {{"bd.r495", 150e3}}, {}},
		{381500, "C9 (BD)", "0.22u -> 0.33u", "\"for expanding the TUNE range\"", {{"bd.c9", 0.22e-6}}, {"C9"}},
		{381500, "C87, R306, R312 (tape)", "C87 removed, R306 4.7k -> 47k, R312 470k -> 4.7M", "tape LOAD (not emulated)", {},
		 {"C87", "R306", "R312"}},
		{392600, "R442, R440 (crash, ride)", "33k -> 10k", "the sample clocks' TUNE range (annotated, p.11)",
		 {{"cr.r442", 33e3}, {"rd.r440", 33e3}}, {"R442", "R440"}},
		{393000, "D701 (power supply)", "RD6.8JB2 -> RD5.6JB2", "the reset at a brown-out (the supply's ramp is not modelled)", {},
		 {"D701"}},
		{415300, "R417 (RS)", "12k -> 3.3k", "\"for giving the voice more realistic sound\"", {{"rs.r417", 12e3}}, {"R417"}},
		{415300, "C134 (HH)", "10u -> 0.01u", "\"for rolling off unnecessary lower frequencies\"", {{"hh.c134", 10e-6}}, {"C134"}},
		{415300, "R392, R393, R495, C600 (TRIG OUT)", "22k, 10k -> 1M; jumper -> R495 1k; C600 0.1u added",
		 "the rim shot's TRIG OUT pulse kept out of TAPE SYNC",
		 {{"rs.r392", 22e3}, {"rs.r393", 10e3}, {"rs.r495", 0.1}, {"rs.c600", 1e-12}},
		 {"R392", "R393", "R495", "C600"}},
		{415300, "C500-C512 (multi-jack board)", "0.01u to ground at each jack added", "\"Noise is high in OUTPUT\"",
		 {{"mix.cjack", 1e-12}, {"bd.c500", 1e-12}, {"sd.c502", 1e-12}, {"rs.c506", 1e-12}, {"hc.c507", 1e-12}},
		 {"C500", "C501", "C502", "C503", "C504", "C505", "C506", "C507", "C508", "C509", "C510", "C511", "C512"}},
		{426700, "C54, R194 (tom noise)", "0.0022u -> 0.0047u, 47k -> 100k", "\"to emphasize attack of TOM TOMs\"",
		 {{"tomnoise.c54", 0.0022e-6}, {"tomnoise.r194", 47e3}}, {"C54", "R194"}},
	};
	return c;
}

constexpr int kFirstSerial = 360100; // the earliest units (ROM group A, SN p.7)
constexpr int kLateSerial = 426700;  // the late board: every change, the netlists' defaults

// The serials where the board changes, the first unit included: the ranges a unit can be in.
inline std::vector<int> boardSerials()
{
	std::vector<int> s{kFirstSerial};
	for (const auto &c : changes())
		if (!c.before.empty() && c.serial != s.back())
			s.push_back(c.serial);
	return s;
}

// The .param overrides for a unit with this serial number.
inline std::map<std::string, double> revisionParts(int serial)
{
	std::map<std::string, double> p;
	for (const auto &c : changes())
		if (serial < c.serial)
			for (const auto &[k, v] : c.before)
				p[k] = v;
	return p;
}

} // namespace tr909
