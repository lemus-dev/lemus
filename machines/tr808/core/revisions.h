#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tr808 {

// The production changes, by serial number: the service notes' DESIGN CHANGES & IMPROVEMENTS (p.15)
// and the drawings' own lot notes (p.8, p.9, p.13: "(150k, .068 for 1~2 lot)"). A unit has every
// change whose serial it has reached; the netlists' defaults are the late board, so a change lists
// the values its parts had before it (docs/circuit.md §17): "<voice>.<param>" for a netlist's
// .param, "int.*" and "cbosc.*" for the INT clock's and the cowbell squares' parts (core/trimmers.h).
// The p.9 box "PORTION CHANGED" (the snare and clap) gives no serial: it is a choice of its own
// (Calibration::lateSnare, lateClap).
struct Change {
	int serial;          // the first unit with the change
	const char *board;   // "main" or "voicing"
	const char *parts;   // as the notes name them
	const char *change;  // what changed
	const char *why;     // Roland's reason
	std::vector<std::pair<const char *, double>> before; // the values before it; empty: not in the model
	std::vector<const char *> refs; // the parts' designators on the board
};

inline const std::vector<Change> &changes()
{
	static const std::vector<Change> c = {
		// Main 1: C203 fitted ("To down") or not ("To up") as the unit's HD14584 needs; the drawing's
		// lot note has R49 150k with C11 .068 before it
		{300, "main", "C11, C203, R49 (INT clock)", ".068 -> .047 + .039; R49 150k -> 100k",
		 "\"Variations in HD14584 hysteresis sometimes deviate Clock Rate out of specified frequency range\"",
		 {{"int.c11", 0.068e-6}, {"int.c203", 0.0}, {"int.r49", 150e3}}, {"C11", "C203", "R49"}},
		{300, "voicing", "C6, R44, R45 (cowbell)", ".01 -> .022, 390k -> 150k, 330k -> 100k",
		 "\"Difficulty in setting COW BELL sound frequency within the specified range\"",
		 {{"cbosc.c6", 0.01e-6}, {"cbosc.r44", 390e3}, {"cbosc.r45", 330e3}}, {"C6", "R44", "R45"}},
		{10600, "main", "R346, R332 (CP)", "1k -> 680, 22k -> 27k", "\"CP sound overmatches the rest in level\"",
		 {{"cp.r346", 1e3}, {"cp.r332", 22e3}}, {"R346", "R332"}},
		{10600, "main", "R91 (CPU)", "15k -> 1M", "the back-up battery's drain (not emulated)", {}, {"R91"}},
		{10600, "main", "R25 (DIN SYNC)", "220k -> 1.5M; 0.01 to the chassis", "false triggering from outside (not emulated)", {}, {"R25"}},
		// Main 5: the first noise generator, DC coupled (R127 4.7k to ground under TM4, R129 330k to
		// IC24a's pin 2, R131 300k, no C202); noise.cir has both (docs/circuit.md §3.1)
		{10600, "main", "R127, R129, R131, C200, C202 (noise generator)", "DC path -> C200 10u, R131 300k -> 100k, C202 22p added",
		 "\"Variations in UPC4558 bias current are transferred to 1/2 IC24 output as an offset\"",
		 {{"noise.r131", 300e3}, {"noise.c202a", 0}, {"noise.c202b", 0}, {"noise.n200", 127}, {"noise.n127", 127}},
		 {"R127", "R129", "R131", "C200", "C202"}},
		{10600, "main", "R106, R154, R182, R213, R242, R268, R298 (voice gates)", "22k -> 10k",
		 "\"Ensure sufficiency of gate drive signal voltage at lower COMMON TRIG amplitude\"",
		 {{"bd.r154", 22e3}, {"sd.r182", 22e3}, {"lt.r213", 22e3}, {"mt.r242", 22e3}, {"ht.r268", 22e3}, {"rs.r298", 22e3}, {"cp.r106", 22e3}},
		 {"R106", "R154", "R182", "R213", "R242", "R268", "R298"}},
		// Main 7: the common trig's monostable isn't a circuit in the model (an ideal 1 ms pulse)
		{10600, "main", "R101, C201 (common trig IC6)", "10k -> 100k, 22p added",
		 "\"High frequency from CP generator induces irregular oscillation on other generators\" (not modelled: IC6 is an ideal 1 ms pulse)",
		 {}, {"R101", "C201"}},
	};
	return c;
}

constexpr int kFirstSerial = 1;        // SN 000001
constexpr int kLateSerial = 10600;     // the late board: every change, the netlists' defaults
constexpr int kFittedSerial = 103852;  // Michael Fischer's unit, which Lemus is fitted to

// The serials where the board changes, the first unit included: the ranges a unit can be in.
inline std::vector<int> boardSerials()
{
	std::vector<int> s{kFirstSerial};
	for (const auto &c : changes())
		if (!c.before.empty() && c.serial != s.back())
			s.push_back(c.serial);
	return s;
}

// The values that differ from the late board's for a unit with this serial number.
inline std::map<std::string, double> revisionParts(int serial)
{
	std::map<std::string, double> p;
	for (const auto &c : changes())
		if (serial < c.serial)
			for (const auto &[k, v] : c.before)
				p[k] = v;
	return p;
}

} // namespace tr808
