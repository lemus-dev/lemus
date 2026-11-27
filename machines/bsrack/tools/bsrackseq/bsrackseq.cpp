// bsrackseq: runs the Bass Station Rack firmware (v1.5) on the MN1880 port (core/mn1880/) with the
// board in core/board.h, headless, and logs what it does: latch writes, the first write to each on-chip register,
// port / UART outputs, MAME's log messages and unmapped accesses; ADC conversions by channel; CV
// DAC channels; the oscillator sync edges; then where the CPU spends the last part of the run.
// Inputs can be scripted: pots, panel switches, MIDI bytes, GATE IN and Audition.
//
// Build: the bsrackseq target (machine.cmake), no JUCE.
// Run: bsrackseq <v1.5.bin> [seconds=2] [options]
//   --eeprom=blank|FILE     EEPROM image (default: a new unit's, Board::defaultEeprom)
//   --save-eeprom=FILE      write the EEPROM image at the end
//   --pot=N:V[@MS]          the pot at mux address N (0-16, core/board.cpp) to ADC code V (hex)
//   --switch=NAME:POS[@MS]  write|lfo|wave1|range|wave2|sync|select|pwm|slope|modsrc|env|trigger,
//                           POS a position name (see switch_names) or number, in panel order
//   --button=NAME:0|1[@MS]  1-9, 0, minus, plus, audition, mode
//   --midi=MS:HH,HH,...     MIDI bytes into the UART at MS ms (hex)
//   --gate=MS:0|1           GATE IN's level
//   --cv[=MASK]             log CV channel changes (MASK hex, default FF)
//   --sync                  log the oscillator sync edge rates every 100 ms
//   --trace=N --latches=N   instructions to trace / latch changes to log (default 0 / 24)
//   --dis=START-END         disassemble a range and exit
// CV transfer maps (docs/firmware.md, "CV transfer"), each from a program with every modulation at
// its zero (neutral_program), booted as program 40:
//   --cvmap=POT[:STEP]      sweep a pot (mux address) 00..FF, note held: every S&H code, the levels
//   --envmap=POT[:STEP]     an envelope pot's time law on the VCA code
//   --lfomap                the LFO's rate and swing per shape
//   --keymap[=STEP]         notes 0..127: the S&H codes, DCO periods, octaves, indexes, edge rates
//   --note=N --velocity=HH --nonote --patch=BYTE:HH (program byte) --switch=NAME:POS
//   --show=ADDR,..          RAM words to print with each --cvmap step
// Regression: --potstorm[=GAP] (v1.5's pot-scan lock-up, PASS/FAIL); --patched loads the ROM
// with Lemus's patch for it (the machine runs v1.5 as it is: faithful to the lock-up).
#include "bsrack/core/board.h"
#include "bsrack/core/mn1880/mn1880d.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using bsrack::Board;

double g_clock = Board::kClock;
Board *g_board = nullptr;
u16 g_last_pc = 0;  // the instruction that started last: the one doing an access

double ms(u64 cycles) { return double(cycles) * mn1880_device::clocks_per_cycle / g_clock * 1000.0; }
u64 now() { return g_board->cpu.total_cycles(); }
u64 cycles_at(double msec) { return u64(msec * g_clock / 1000.0 / mn1880_device::clocks_per_cycle); }

std::string disasm(u16 pc, int *length = nullptr)
{
	mn1880_disassembler dis;
	util::disasm_interface &d = dis;
	util::disasm_interface::data_buffer buf([] (offs_t a) -> u8 { return a < 0x8000 ? g_board->rom[a] : 0; });
	std::ostringstream s;
	const offs_t r = d.disassemble(s, pc, buf, buf);
	if (length)
		*length = int(r & util::disasm_interface::LENGTHMASK);
	return s.str();
}

struct Latch
{
	Latch(const char *n, u16 a) : name(n), addr(a) {}
	const char *name;
	u16 addr;
	u64 writes = 0;
	int changes = 0;
	int shown = 0;
	bool written = false;
	u8 value = 0;
	std::map<u8, u64> values;
};

struct Sfr
{
	u64 writes = 0, reads = 0;
	u8 last = 0;
	std::map<u8, u64> values;
	std::set<u16> write_pcs, read_pcs;
};

struct Event
{
	u64 at;
	std::string what;
	std::function<void()> act;
};

// controls.h's switches, in its order, with their positions in panel order
const struct { const char *name, *positions; } switch_names[bsrack::kSwitches] = {
	{"write", "protect enable"}, {"lfo", "rdom tri saw"}, {"wave1", "saw pulse"}, {"range", "16 8 4 2"},
	{"wave2", "saw pulse"}, {"sync", "off on"}, {"select", "1 2"}, {"pwm", "env2 man lfo"}, {"slope", "12dB 24dB"},
	{"modsrc", "lfo env2"}, {"env", "1 2 1&2"}, {"trigger", "autoglide single multi"},
};
const char *const button_names[bsrack::kButtons] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "minus", "0", "plus", "audition", "mode"};

int position_of(int sw, const std::string &pos)
{
	std::istringstream names(switch_names[sw].positions);
	std::string n;
	for (int i = 0; names >> n; i++)
		if (n == pos)
			return i;
	return std::atoi(pos.c_str());
}

// ------------------------------------------------------------ CV transfer maps

// A program with every modulation at its zero (docs/firmware.md, "CV transfer"): osc 1 only, both
// oscillators pulse at 8', filter open, no resonance, the bipolar depths at 80 (the dead band of the
// curve at 0800), envelopes instant with full sustain, no velocity, no portamento, sync off, 24 dB,
// PWM source MAN., filter mod source ENV 2 (its depth's zero is 80 in 24 dB, 00 in 12 dB).
std::array<u8, 34> neutral_program()
{
	std::array<u8, 34> p = Board::defaultProgram();
	p[7] = 0x03;  // LFO speed: stopped
	return p;
}

struct MapSetup
{
	int note = 60, velocity = 0x64;
	std::vector<std::pair<int, int>> patch;     // program byte, value
	std::vector<std::pair<int, int>> switches;  // controls.h switch, position
	std::vector<u16> show;                      // RAM words to print
	int step = 8;
	bool no_note = false;
};

void run_ms(Board &b, double msec)
{
	const u64 end = b.cpu.total_cycles() + cycles_at(msec);
	while (b.cpu.total_cycles() < end)
		b.run(int(std::min<u64>(1000, end - b.cpu.total_cycles())));
}

void note_on(Board &b, int note, int velocity)
{
	b.cpu.uart_rx_byte(0x90);
	b.cpu.uart_rx_byte(u8(note));
	b.cpu.uart_rx_byte(u8(velocity));
}

void note_off(Board &b, int note)
{
	b.cpu.uart_rx_byte(0x80);
	b.cpu.uart_rx_byte(u8(note));
	b.cpu.uart_rx_byte(0);
}

// Boots into program 40 = the neutral program (with the patches), the switches set, every pot at the
// program's value (so none moves), and the note held.
void boot_neutral(Board &b, const MapSetup &m)
{
	b.eeprom = Board::defaultEeprom();
	std::array<u8, 34> p = neutral_program();
	for (auto &pv : m.patch)
		p[size_t(pv.first)] = u8(pv.second);
	std::copy(p.begin(), p.end(), b.eeprom.begin());
	b.eeprom[0x7fd] = 40;
	b.reset();
	run_ms(b, 600);
	// a switch acts only when it moves: from another position to the one asked for
	for (auto &sw : m.switches)
	{
		b.setSwitch(sw.first, sw.second == 0 ? 1 : 0);
		run_ms(b, 100);
		b.setSwitch(sw.first, sw.second);
		run_ms(b, 100);
	}
	if (!m.no_note)
		note_on(b, m.note, m.velocity);
	run_ms(b, 300);
}

// Moves a pot to v the way a hand would, so the firmware takes it (it acts on changes of more than
// 5 codes): from far away first.
void move_pot(Board &b, int pot, int v, double settle_ms)
{
	b.pots[size_t(pot)] = u8(v < 0x80 ? 0xff : 0x00);
	run_ms(b, 80);
	b.pots[size_t(pot)] = u8(v);
	run_ms(b, settle_ms);
}

// The program byte a pot writes, with the selects as they are (docs/firmware.md's pot table).
int prog_byte(Board &b, int pot)
{
	const bool osc2 = BIT(b.cpu.ram(0xfc), 6), env2 = BIT(b.cpu.ram(0xfc), 1) && !BIT(b.cpu.ram(0xfc), 2);
	const bool modenv = BIT(b.cpu.ram(0xe0), 3);
	switch (pot)
	{
	case 0: return osc2 ? 23 : 5;
	case 1: return 3;
	case 2: return 2;
	case 3: return 7;
	case 4: return osc2 ? 22 : 4;
	case 5: return 8;
	case 6: return 1;
	case 7: return 0;
	case 8: return env2 ? 20 : 15;
	case 9: return modenv ? 25 : 11;
	case 10: return env2 ? 18 : 13;
	case 11: return 9;
	case 12: return env2 ? 19 : 14;
	case 13: return 10;
	case 14: return env2 ? 17 : 12;
	case 15: return osc2 ? 24 : 6;
	default: return env2 ? 21 : 16;
	}
}

void print_show(Board &b, const MapSetup &m)
{
	for (u16 a : m.show)
		std::printf(" %04X", b.cpu.ram(a) | b.cpu.ram(u16(a + 1)) << 8);
}

// --cvmap=POT: sweeps a pot (mux address 0-16) 00..FF and prints every S&H channel and the osc
// levels in steady state (min-max over 100 ms where they move).
int cvmap(Board &b, int pot, const MapSetup &m)
{
	boot_neutral(b, m);
	std::printf("cvmap: pot %d (%s), note %d%s; S&H codes (min-max over 100 ms if moving)\n", pot, bsrack::kMuxPotNames[pot],
			m.note, m.no_note ? " not held" : " held");
	std::printf("  pot  stored  CVout  VCF   OSCB_PWM OSCA_PWM OSCB_CV OSCA_CV RES   VCA    (x'34') (x'32')");
	for (u16 a : m.show)
		std::printf(" @%04X", a);
	std::printf("\n");
	for (int v = 0; v <= 0x100; v += m.step)
	{
		const int pv = std::min(v, 0xff);
		move_pot(b, pot, pv, 200);
		std::array<u16, 8> lo, hi;
		lo.fill(0xfff);
		hi.fill(0);
		u8 l34 = 0xff, h34 = 0, l32 = 0xff, h32 = 0;
		for (int i = 0; i < 25; i++)
		{
			run_ms(b, 4);
			for (size_t c = 0; c < 8; c++)
			{
				lo[c] = std::min(lo[c], b.cv[c]);
				hi[c] = std::max(hi[c], b.cv[c]);
			}
			l34 = std::min(l34, b.cpu.reg(0x34));
			h34 = std::max(h34, b.cpu.reg(0x34));
			l32 = std::min(l32, b.cpu.reg(0x32));
			h32 = std::max(h32, b.cpu.reg(0x32));
		}
		std::printf("  %02X   %02X     ", pv, b.cpu.ram(u16(0xc0 + prog_byte(b, pot))));
		for (size_t c = 0; c < 8; c++)
		{
			char cell[16];
			if (lo[c] == hi[c])
				std::snprintf(cell, sizeof(cell), "%03X", lo[c]);
			else
				std::snprintf(cell, sizeof(cell), "%03X-%03X", lo[c], hi[c]);
			std::printf(" %-8s", cell);
		}
		std::printf(l34 == h34 ? " %02X     " : " %02X-%02X  ", l34, h34);
		std::printf(l32 == h32 ? " %02X     " : " %02X-%02X  ", l32, h32);
		print_show(b, m);
		std::printf("\n");
	}
	return 0;
}

// Samples S&H channel ch every 4 ms (its refresh) until it hasn't moved for `still` ms or `limit` ms
// pass; returns the codes.
std::vector<u16> trace_cv(Board &b, int ch, double still, double limit)
{
	std::vector<u16> v;
	int same = 0;
	for (double t = 0; t < limit; t += 4)
	{
		run_ms(b, 4);
		const u16 c = b.cv[size_t(ch)];
		same = !v.empty() && c == v.back() ? same + 4 : 0;
		v.push_back(c);
		if (same >= still)
			break;
	}
	return v;
}

// --envmap=POT: the VCA (envelope 1, S&H 7) over time for each value of an envelope pot (8 release,
// 10 decay, 12 sustain, 14 attack, 16 velocity): attack to the peak, decay to the sustain level,
// release to closed, the times and the first steps per 4 ms tick. For pot 16, the peak and sustain
// for note velocities 7F, 40, 10, 01.
int envmap(Board &b, int pot, const MapSetup &m0)
{
	MapSetup m = m0;
	m.no_note = true;
	boot_neutral(b, m);
	std::printf("envmap: pot %d (%s); VCA code (S&H 7) sampled at its 4 ms refresh\n", pot, bsrack::kMuxPotNames[pot]);
	if (pot == 16)
		std::printf("  pot  stored  peak/sustain at velocity 7F, 40, 10, 01\n");
	else
		std::printf("  pot  stored  attack ms  peak  first steps    decay ms  sustain  first steps    release ms  first steps\n");
	for (int v = 0; v <= 0x100; v += m.step)
	{
		const int pv = std::min(v, 0xff);
		move_pot(b, pot, pv, 200);
		const int byte = prog_byte(b, pot);
		if (pot == 16)
		{
			std::printf("  %02X   %02X     ", pv, b.cpu.ram(u16(0xc0 + byte)));
			for (int vel : {0x7f, 0x40, 0x10, 0x01})
			{
				note_on(b, m.note, vel);
				std::vector<u16> a = trace_cv(b, 7, 300, 15000);
				const u16 peak = *std::min_element(a.begin(), a.end());
				std::printf("  %03X/%03X", peak, a.back());
				note_off(b, m.note);
				trace_cv(b, 7, 300, 15000);
			}
			std::printf("\n");
			continue;
		}
		note_on(b, m.note, m.velocity);
		// attack: to the minimum code; decay: from there until it stops
		std::vector<u16> a = trace_cv(b, 7, 300, 30000);
		size_t start = 0;
		while (start < a.size() && a[start] == 0xfff)
			start++;
		const size_t peak_at = size_t(std::min_element(a.begin(), a.end()) - a.begin());
		size_t settle = a.size() - 1;
		while (settle > peak_at && a[settle - 1] == a.back())
			settle--;
		auto steps = [] (const std::vector<u16> &x, size_t from, size_t to) {
			std::string out;
			for (size_t i = from; i < to && i < from + 3 && i + 1 < x.size(); i++)
			{
				char cell[8];
				std::snprintf(cell, sizeof(cell), " %+d", int(x[i + 1]) - int(x[i]));
				out += cell;
			}
			return out;
		};
		std::printf("  %02X   %02X     %8.0f  %03X %-14s %8.0f  %03X    %-14s", pv, b.cpu.ram(u16(0xc0 + byte)),
				double(peak_at + 1 - std::min(start, peak_at)) * 4, a[peak_at], steps(a, start > 0 ? start - 1 : 0, peak_at).c_str(),
				double(settle - peak_at) * 4, a.back(), steps(a, peak_at, settle).c_str());
		note_off(b, m.note);
		std::vector<u16> r = trace_cv(b, 7, 300, 30000);
		size_t end = r.size() - 1;
		while (end > 0 && r[end - 1] == r.back())
			end--;
		std::vector<u16> rr{a.back()};
		rr.insert(rr.end(), r.begin(), r.end());
		std::printf("  %8.0f    %s\n", double(end + 1) * 4, steps(rr, 0, rr.size()).c_str());
	}
	return 0;
}

// --lfomap: the LFO's period (from the LFO SPEED LED) and its swing on the VCF code (mod source LFO,
// mod depth full) for LFO speed 00..FF, per shape; then one period of each shape's code pattern.
int lfomap(Board &b, const MapSetup &m0)
{
	std::printf("lfomap: LFO speed (pot 3) vs the LFO SPEED LED's period and the VCF code's swing\n"
	            "        (filter mod source LFO, mod depth FF, frequency 80, note %d held)\n", m0.note);
	const char *shapes[3] = {"R'DOM", "TRI", "SAW"};
	for (int shape = 0; shape < 3; shape++)
	{
		MapSetup m = m0;
		m.switches.push_back({bsrack::kModSource, 0});
		m.switches.push_back({bsrack::kLfoShape, shape});
		boot_neutral(b, m);
		move_pot(b, 11, 0x80, 100);
		move_pot(b, 9, 0xff, 100);
		std::printf("  shape position %d (%s):\n    speed  stored  LED period ms  Hz        VCF code range\n", shape, shapes[shape]);
		for (int v = 0; v <= 0x100; v += std::max(m.step, 0x10))
		{
			const int pv = std::min(v, 0xff);
			move_pot(b, 3, pv, 300);
			// the LED toggles: time between rising edges of B000 bit 0 (active low: lit)
			std::vector<double> edges;
			u16 lo = 0xfff, hi = 0;
			int last = BIT(b.b000, 0);
			for (double t = 0; t < 20000 && edges.size() < 5; t += 1)
			{
				run_ms(b, 1);
				const int led = BIT(b.b000, 0);
				if (led && !last)
					edges.push_back(t);
				last = led;
				if (edges.size() >= 1)
				{
					lo = std::min(lo, b.cv[1]);
					hi = std::max(hi, b.cv[1]);
				}
			}
			const double period = edges.size() >= 2 ? (edges.back() - edges.front()) / double(edges.size() - 1) : 0;
			std::printf("    %02X     %02X      %10.1f  %8.3f  %03X-%03X\n", pv, b.cpu.ram(0xc7), period, period > 0 ? 1000.0 / period : 0.0, lo, hi);
		}
		move_pot(b, 3, 0xc0, 300);
		std::printf("    pattern at speed C0, VCF code every 10 ms:");
		for (int i = 0; i < 60; i++)
		{
			run_ms(b, 10);
			std::printf(" %03X", b.cv[1]);
		}
		std::printf("\n");
	}
	return 0;
}

// --keymap: notes 0..127: the S&H codes, the DCO reload values, octaves and
// pitch indexes, and the sync edge rates.
int keymap(Board &b, const MapSetup &m0, int step)
{
	MapSetup m = m0;
	m.no_note = true;
	boot_neutral(b, m);
	std::array<std::vector<u64>, 2> edges;
	b.onSync = [&] (int osc, u64 clock) { edges[size_t(osc)].push_back(clock); };
	std::printf("keymap: notes, neutral program\n  note  CVout VCF  OSCB_CV OSCA_CV  A0    A2    oct A/B  index A/B  OSCA Hz    OSCB Hz\n");
	for (int n = 0; n < 128; n += step)
	{
		note_on(b, n, m.velocity);
		run_ms(b, 200);
		edges[0].clear();
		edges[1].clear();
		run_ms(b, 300);
		auto hz = [&] (int o) {
			const auto &e = edges[size_t(o)];
			return e.size() < 2 ? 0.0 : double(e.size() - 1) * g_clock / double(e.back() - e.front());
		};
		std::printf("  %3d   %03X   %03X  %03X     %03X      %04X  %04X  %d/%d      %3d/%3d    %9.3f  %9.3f\n", n, b.cv[0], b.cv[1], b.cv[4], b.cv[5],
				b.cpu.ram(0xa0) | b.cpu.ram(0xa1) << 8, b.cpu.ram(0xa2) | b.cpu.ram(0xa3) << 8, b.cpu.ram(0x163), b.cpu.ram(0x164),
				(b.cpu.ram(0x1c8) | b.cpu.ram(0x1c9) << 8) / 2, (b.cpu.ram(0x139) | b.cpu.ram(0x13a) << 8) / 2, hz(0), hz(1));
		note_off(b, n);
		run_ms(b, 50);
	}
	return 0;
}

// --potstorm[=GAP]: the regression case for v1.5's pot-scan lock-up (core/board.cpp). After the
// boot, all 17 pots go to FF and back to 80, GAP ms apart (default 60); then a note and AUDITION
// must open the VCA. Reports whether all 17 pots were active at once (the state that locks the
// unpatched firmware) and exits 0 on PASS. Without --patched it shows the lock-up (FAIL is v1.5's own behaviour).
int potstorm(Board &b, int gap)
{
	b.eeprom = Board::defaultEeprom();
	b.reset();
	run_ms(b, 1000);
	static const int order[17] = {7, 3, 5, 1, 6, 2, 4, 0, 15, 11, 13, 9, 14, 10, 12, 8, 16};
	int most_active = 0;
	auto count_active = [&] {
		int n = 0;
		for (int i = 0; i < 17; i++)
			n += b.cpu.ram(u16(0x280 + i)) != 0;
		most_active = std::max(most_active, n);
	};
	for (int p : order)
		for (int v : {0xff, 0x80})
		{
			b.pots[size_t(p)] = u8(v);
			for (int t = 0; t < gap; t += 4)
			{
				run_ms(b, 4);
				count_active();
			}
		}
	for (int t = 0; t < 400; t += 4)
	{
		run_ms(b, 4);
		count_active();
	}
	auto opens = [&] (double within) {
		for (double t = 0; t < within; t += 4)
		{
			run_ms(b, 4);
			if (b.cv[7] < 0xff0)
				return true;
		}
		return false;
	};
	note_on(b, 60, 0x7f);
	const bool note = opens(300);
	note_off(b, 60);
	run_ms(b, 600);
	b.audition = true;
	const bool audition = opens(300);
	b.audition = false;
	run_ms(b, 600);
	const bool pass = note && audition;
	std::printf("potstorm: 17 pots to FF and back, %d ms apart; most pots active at once %d of 17; ROM patch %s\n"
	            "  note 60 opens the VCA: %s; AUDITION opens it: %s; main loop pc %04X\n%s\n",
			gap, most_active, b.potScanLockFixed() ? "on" : "off", note ? "yes" : "no", audition ? "yes" : "no", b.cpu.pc(0),
			pass ? "PASS" : "FAIL");
	return pass ? 0 : 1;
}

const char *usage =
	"usage: bsrackseq <rom> [seconds=2] [--eeprom=blank|FILE] [--save-eeprom=FILE] [--pot=N:V[@MS]]\n"
	"       [--switch=NAME:POS[@MS]] [--button=NAME:0|1[@MS]] [--midi=MS:HH,..] [--gate=MS:0|1] [--cv[=MASK]]\n"
	"       [--sync] [--clock=Hz] [--trace=N] [--latches=N] | --dis=START-END\n"
	"   or: bsrackseq <rom> --cvmap=POT[:STEP] | --envmap=POT[:STEP] | --lfomap | --keymap[=STEP]\n"
	"       [--note=N] [--velocity=HH] [--nonote] [--patch=BYTE:HH] [--switch=NAME:POS] [--show=ADDR,..]\n"
	"   or: bsrackseq <rom> --potstorm[=GAP] [--patched]\n";

} // namespace

int main(int argc, char **argv)
{
	Board board;
	g_board = &board;
	mn1880023_device &cpu = board.cpu;

	const char *rom_path = nullptr, *save_eeprom = nullptr;
	std::string eeprom_arg;
	double seconds = 2.0;
	int trace_left = 0, latch_lines = 24;
	long dis_start = -1, dis_end = -1;
	int cv_mask = 0;
	bool sync_log = false;
	std::vector<Event> events;
	std::vector<std::string> pending;  // parsed after the ROM loads
	std::string map_mode;
	int map_pot = 0;
	bool patched = false;
	MapSetup map;
	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
		if (!std::strncmp(a, "--dis=", 6))
		{
			char *dash = nullptr;
			dis_start = std::strtol(a + 6, &dash, 16);
			dis_end = dash && *dash == '-' ? std::strtol(dash + 1, nullptr, 16) : dis_start + 0x40;
		}
		else if (!std::strncmp(a, "--clock=", 8))
			g_clock = std::atof(a + 8);
		else if (!std::strncmp(a, "--trace=", 8))
			trace_left = std::atoi(a + 8);
		else if (!std::strncmp(a, "--latches=", 10))
			latch_lines = std::atoi(a + 10);
		else if (!std::strncmp(a, "--eeprom=", 9))
			eeprom_arg = a + 9;
		else if (!std::strncmp(a, "--save-eeprom=", 14))
			save_eeprom = a + 14;
		else if (!std::strcmp(a, "--cv"))
			cv_mask = 0xff;
		else if (!std::strncmp(a, "--cv=", 5))
			cv_mask = int(std::strtol(a + 5, nullptr, 16));
		else if (!std::strcmp(a, "--sync"))
			sync_log = true;
		else if (!std::strncmp(a, "--cvmap=", 8) || !std::strncmp(a, "--envmap=", 9) || !std::strcmp(a, "--lfomap") || !std::strncmp(a, "--keymap", 8))
		{
			const char *v = std::strchr(a, '=');
			map_mode = std::string(a + 2, v ? size_t(v - a - 2) : std::strlen(a + 2));
			if (v)
			{
				char *colon = nullptr;
				map_pot = int(std::strtol(v + 1, &colon, 10));
				if (colon && *colon == ':')
					map.step = int(std::strtol(colon + 1, nullptr, 16));
			}
		}
		else if (!std::strncmp(a, "--potstorm", 10))
		{
			map_mode = "potstorm";
			map_pot = a[10] == '=' ? std::atoi(a + 11) : 60;
		}
		else if (!std::strcmp(a, "--patched"))
			patched = true;
		else if (!std::strncmp(a, "--note=", 7))
			map.note = std::atoi(a + 7);
		else if (!std::strncmp(a, "--velocity=", 11))
			map.velocity = int(std::strtol(a + 11, nullptr, 16));
		else if (!std::strcmp(a, "--nonote"))
			map.no_note = true;
		else if (!std::strncmp(a, "--patch=", 8))
		{
			char *colon = nullptr;
			const int byte = int(std::strtol(a + 8, &colon, 10));
			map.patch.push_back({byte, colon && *colon == ':' ? int(std::strtol(colon + 1, nullptr, 16)) : 0});
		}
		else if (!std::strncmp(a, "--show=", 7))
		{
			std::istringstream in(a + 7);
			std::string h;
			while (std::getline(in, h, ','))
				map.show.push_back(u16(std::strtol(h.c_str(), nullptr, 16)));
		}
		else if (!std::strncmp(a, "--", 2))
			pending.push_back(a);
		else if (!rom_path)
			rom_path = a;
		else
			seconds = std::atof(a);
	}
	if (!rom_path)
	{
		std::fputs(usage, stderr);
		return 2;
	}
	{
		std::vector<uint8_t> rom(Board::kRomSize);
		FILE *f = std::fopen(rom_path, "rb");
		const bool ok = f != nullptr && std::fread(rom.data(), 1, rom.size(), f) == rom.size();
		if (f)
			std::fclose(f);
		if (!ok || !board.setRom(rom, patched))
		{
			std::fprintf(stderr, "%s: can't read 32768 bytes\n", rom_path);
			return 1;
		}
	}

	if (dis_start >= 0)
	{
		for (long pc = dis_start; pc <= dis_end && pc < 0x8000;)
		{
			int length = 1;
			const std::string text = disasm(u16(pc), &length);
			std::printf("%04lX ", pc);
			for (int i = 0; i < 6; i++)
				std::printf(i < length ? " %02X" : "   ", board.rom[size_t(pc + i) & 0x7fff]);
			std::printf("  %s\n", text.c_str());
			pc += std::max(length, 1);
		}
		return 0;
	}

	if (eeprom_arg == "blank")
		board.eeprom.fill(0xff);
	else if (eeprom_arg.empty() || eeprom_arg == "default")
		board.eeprom = Board::defaultEeprom();
	else if (!eeprom_arg.empty())
	{
		FILE *f = std::fopen(eeprom_arg.c_str(), "rb");
		if (!f || std::fread(board.eeprom.data(), 1, board.eeprom.size(), f) != board.eeprom.size())
		{
			std::fprintf(stderr, "%s: can't read a 2048-byte EEPROM image\n", eeprom_arg.c_str());
			return 1;
		}
		std::fclose(f);
	}

	// scripted inputs
	for (const std::string &opt : pending)
	{
		const size_t eq = opt.find('=');
		const std::string key = opt.substr(0, eq), val = eq == std::string::npos ? "" : opt.substr(eq + 1);
		double at = 0;
		std::string body = val;
		if (key == "--pot" || key == "--switch" || key == "--button")
		{
			const size_t atp = val.find('@');
			if (atp != std::string::npos)
			{
				at = std::atof(val.c_str() + atp + 1);
				body = val.substr(0, atp);
			}
		}
		else
		{
			const size_t colon = val.find(':');
			at = std::atof(val.c_str());
			body = colon == std::string::npos ? "" : val.substr(colon + 1);
		}
		if (key == "--pot")
		{
			const int n = std::atoi(body.c_str());
			const size_t c = body.find(':');
			const u8 v = u8(std::strtol(body.c_str() + (c == std::string::npos ? 0 : c + 1), nullptr, 16));
			if (n < 0 || n > 16 || c == std::string::npos)
			{
				std::fprintf(stderr, "bad %s\n", opt.c_str());
				return 2;
			}
			events.push_back({cycles_at(at), "pot " + std::to_string(n) + " (" + bsrack::kMuxPotNames[n] + ") = " + body.substr(c + 1),
					[&board, n, v] { board.pots[size_t(n)] = v; }});
		}
		else if (key == "--switch")
		{
			const size_t c = body.find(':');
			const std::string name = body.substr(0, c), pos = c == std::string::npos ? "0" : body.substr(c + 1);
			bool found = false;
			for (int sw = 0; sw < bsrack::kSwitches; sw++)
				if (name == switch_names[sw].name)
				{
					const int p = position_of(sw, pos);
					events.push_back({cycles_at(at), "switch " + name + " " + pos, [&board, sw, p] { board.setSwitch(sw, p); }});
					found = true;
				}
			if (!found)
			{
				std::fprintf(stderr, "bad %s\n", opt.c_str());
				return 2;
			}
		}
		else if (key == "--midi")
		{
			std::vector<u8> bytes;
			std::istringstream in(body);
			std::string h;
			while (std::getline(in, h, ','))
				bytes.push_back(u8(std::strtol(h.c_str(), nullptr, 16)));
			events.push_back({cycles_at(at), "MIDI in " + body, [&cpu, bytes] { for (u8 b : bytes) cpu.uart_rx_byte(b); }});
		}
		else if (key == "--button")
		{
			const size_t c = body.find(':');
			const std::string name = body.substr(0, c);
			const bool down = c != std::string::npos && std::atoi(body.c_str() + c + 1) != 0;
			int b = 0;
			while (b < bsrack::kButtons && name != button_names[b])
				b++;
			if (b == bsrack::kButtons)
			{
				std::fprintf(stderr, "bad %s\n", opt.c_str());
				return 2;
			}
			const int code = bsrack::kKeyCode[size_t(b)];
			events.push_back({cycles_at(at), "button " + body, [&board, code, down] {
				if (code < 0)
					board.audition = down;
				else
					board.setKey(code, down);
			}});
		}
		else if (key == "--gate")
		{
			const bool level = std::atoi(body.c_str()) != 0;
			events.push_back({cycles_at(at), "gate " + body, [&board, level] { board.gateIn = level; }});
		}
		else
		{
			std::fputs(usage, stderr);
			return 2;
		}
	}
	std::stable_sort(events.begin(), events.end(), [] (const Event &a, const Event &b) { return a.at < b.at; });

	if (!map_mode.empty())
	{
		// the switch options set the panel before the boot
		for (const std::string &opt : pending)
			if (!opt.compare(0, 9, "--switch="))
			{
				const std::string body = opt.substr(9);
				const size_t c = body.find(':');
				for (int sw = 0; sw < bsrack::kSwitches; sw++)
					if (body.substr(0, c) == switch_names[sw].name)
						map.switches.push_back({sw, position_of(sw, c == std::string::npos ? "0" : body.substr(c + 1))});
			}
		if (map_mode == "potstorm")
			return potstorm(board, map_pot);
		if (map_mode != "keymap" && (map_pot < 0 || map_pot > 16))
		{
			std::fputs(usage, stderr);
			return 2;
		}
		if (map_mode == "cvmap")
			return cvmap(board, map_pot, map);
		if (map_mode == "envmap")
			return envmap(board, map_pot, map);
		if (map_mode == "lfomap")
			return lfomap(board, map);
		return keymap(board, map, map_pot > 0 ? map_pot : 8);
	}

	// vector targets (big-endian words at 0000-003F), to count interrupt entries
	std::map<u16, std::string> vectors;
	for (int v = 0; v < 0x20; v++)
	{
		const u16 target = u16(board.rom[size_t(2 * v)] << 8 | board.rom[size_t(2 * v + 1)]);
		char name[32];
		if ((v & 0x0f) == 0)
			std::snprintf(name, sizeof(name), "CPU%d reset", v >> 4);
		else if ((v & 0x0f) == 1)
			std::snprintf(name, sizeof(name), "CPU%d PI", v >> 4);
		else
			std::snprintf(name, sizeof(name), "CPU%d IRQ%d", v >> 4, (v & 0x0f) - 2);
		std::string &s = vectors[target];
		s += s.empty() ? name : std::string(", ") + name;
	}
	std::map<u16, u64> vector_hits, vector_first;

	const u64 total = cycles_at(seconds * 1000.0);
	const u64 window_start = total - total / 8;
	std::map<u16, u64> hot;
	u64 instructions = 0;
	bool in_window = false;

	// BSRACK_PROBE=PC,PC,..: at each, the working bytes the VCF routine (61A0-62D8) uses
	std::set<u16> probe;
	if (const char *pr = std::getenv("BSRACK_PROBE"))
		for (const char *q = pr; *q;)
		{
			char *end = nullptr;
			probe.insert(u16(std::strtol(q, &end, 16)));
			q = *end ? end + 1 : end;
		}
	cpu.trace() = [&] (u16 pc) {
		if (!probe.empty() && probe.count(pc))
			std::printf("%9.3f ms  %04X  AA %02X AB %02X  F0 %02X F1 %02X  C9 %02X 7C %02X  79 %02X%02X 72 %02X%02X  fs %02X\n", ms(now()), pc,
			            cpu.ram(0xAA), cpu.ram(0xAB), cpu.ram(0xF0), cpu.ram(0xF1), cpu.ram(0xC9), cpu.ram(0x7C), cpu.ram(0x7A), cpu.ram(0x79),
			            cpu.ram(0x73), cpu.ram(0x72), cpu.fs(cpu.active_cpu()));
		g_last_pc = pc;
		instructions++;
		if (in_window)
			hot[pc]++;
		if (vectors.count(pc) && pc >= 0x40 && vector_hits[pc]++ == 0)
			vector_first[pc] = now();
		if (trace_left > 0)
		{
			trace_left--;
			std::printf("%9.3f ms  cpu%d %04X  %s\n", ms(now()), cpu.active_cpu(), pc, disasm(pc).c_str());
		}
	};

	std::map<u16, Sfr> sfrs;
	std::map<std::pair<u16, u16>, u64> window_sfr_reads;
	cpu.sfr_hook() = [&] (u16 addr, u8 data, bool write) {
		Sfr &s = sfrs[addr];
		if (write)
		{
			if (s.writes == 0)
				std::printf("%9.3f ms  SFR %02X <- %02X  pc %04X  (first write)\n", ms(now()), addr, data, g_last_pc);
			s.writes++;
			s.last = data;
			s.values[data]++;
			s.write_pcs.insert(g_last_pc);
		}
		else
		{
			s.reads++;
			s.read_pcs.insert(g_last_pc);
			if (in_window)
				window_sfr_reads[{addr, g_last_pc}]++;
		}
	};

	std::vector<std::string> log_lines;
	cpu.log() = [&] (const char *msg) {
		if (log_lines.size() < 40)
			std::printf("%9.3f ms  LOG %s", ms(now()), msg);
		log_lines.push_back(msg);
	};

	std::vector<Latch> latches{{"A000 control", 0xa000}, {"A800 row select", 0xa800}, {"B000 mode LEDs", 0xb000}, {"B800 digits", 0xb800}};
	board.onLatch = [&] (u16 addr, u8 data) {
		for (Latch &l : latches)
			if (addr == l.addr)
			{
				l.writes++;
				l.values[data]++;
				if (!l.written || data != l.value)
				{
					l.changes++;
					if (l.shown++ < latch_lines)
						std::printf("%9.3f ms  %-16s <- %02X  pc %04X\n", ms(now()), l.name, data, g_last_pc);
				}
				l.written = true;
				l.value = data;
			}
	};
	std::map<std::string, u64> unmapped;
	board.onUnmapped = [&] (u16 addr, int data) {
		char key[64];
		std::snprintf(key, sizeof(key), "%s %04X", data == -2 ? "program read" : data < 0 ? "read" : "write", addr);
		if (unmapped[key]++ == 0 && unmapped.size() <= 40)
			std::printf("%9.3f ms  UNMAPPED %s  pc %04X\n", ms(now()), key, g_last_pc);
	};

	// ADC: conversions by channel and mux address, and the order of the first ones after 1 s
	std::map<std::pair<int, int>, u64> adc_counts;
	std::vector<std::pair<int, int>> adc_order;
	board.onAdc = [&] (int ch, int mux) {
		adc_counts[{ch, ch == 0 ? mux : -1}]++;
		if (now() >= cycles_at(1000) && adc_order.size() < 48)
			adc_order.push_back({ch, ch == 0 ? mux : -1});
	};

	// CV channels
	std::array<u64, 8> cv_updates{};
	board.onCv = [&] (int ch, u16 code) {
		cv_updates[size_t(ch)]++;
		if (BIT(cv_mask, ch))
			std::printf("%9.3f ms  CV %d = %03X\n", ms(now()), ch, code);
	};

	// sync edges
	std::array<std::vector<u64>, 2> edges;
	board.onSync = [&] (int osc, u64 clock) { edges[size_t(osc)].push_back(clock); };
	auto rate = [&] (int osc, u64 from_clock, u64 to_clock) -> double {
		const auto &e = edges[size_t(osc)];
		auto lo = std::lower_bound(e.begin(), e.end(), from_clock), hi = std::lower_bound(e.begin(), e.end(), to_clock);
		if (hi - lo < 2)
			return 0;
		return double(hi - lo - 1) * g_clock / double(*(hi - 1) - *lo);
	};

	std::array<u64, 10> port_writes{};
	std::array<int, 10> port_shown{};
	std::array<int, 10> port_last;
	port_last.fill(-1);
	// the board owns ports 1 and 3; log the others
	auto port_out = [&] (int port, u8 data) {
		port_writes[size_t(port)]++;
		if (data != port_last[size_t(port)] && port_shown[size_t(port)]++ < 12)
			std::printf("%9.3f ms  port %d out %02X  pc %04X\n", ms(now()), port, data, g_last_pc);
		port_last[size_t(port)] = data;
	};
	cpu.port_out_cb<0>() = [&] (u8 d) { port_out(0, d); };
	cpu.port_out_cb<2>() = [&] (u8 d) { port_out(2, d); };
	cpu.port_out_cb<4>() = [&] (u8 d) { port_out(4, d); };
	cpu.port_out_cb<5>() = [&] (u8 d) { port_out(5, d); };
	cpu.port_out_cb<6>() = [&] (u8 d) { port_out(6, d); };
	cpu.port_out_cb<7>() = [&] (u8 d) { port_out(7, d); };
	cpu.port_out_cb<8>() = [&] (u8 d) { port_out(8, d); };
	cpu.port_out_cb<9>() = [&] (u8 d) { port_out(9, d); };

	u64 tx_bytes = 0;
	board.onMidiOut = [&] (u8 d) {
		if (tx_bytes++ < 64)
			std::printf("%9.3f ms  MIDI out %02X\n", ms(now()), d);
	};

	std::printf("bsrackseq: %s, %.0f Hz oscillator (%.0f CPU cycles/s), %.3f s = %llu cycles, EEPROM %s\n\n",
			rom_path, g_clock, g_clock / mn1880_device::clocks_per_cycle, seconds, (unsigned long long)total,
			eeprom_arg.empty() ? "default" : eeprom_arg.c_str());

	board.reset();
	size_t next_event = 0;
	u64 next_sync_log = cycles_at(100);
	while (now() < total)
	{
		while (next_event < events.size() && events[next_event].at <= now())
		{
			std::printf("%9.3f ms  >> %s\n", ms(now()), events[next_event].what.c_str());
			events[next_event++].act();
		}
		in_window = now() >= window_start;
		u64 stop = std::min<u64>(now() + 1000, in_window ? total : window_start);
		if (next_event < events.size())
			stop = std::min(stop, std::max(events[next_event].at, now() + 1));
		board.run(int(stop - now()));
		if (sync_log && now() >= next_sync_log)
		{
			const u64 c = now() * mn1880_device::clocks_per_cycle, w = u64(g_clock / 10);
			std::printf("%9.3f ms  sync edges/s over 100 ms: OSCA %.3f  OSCB %.3f\n", ms(now()), rate(0, c - w, c), rate(1, c - w, c));
			next_sync_log += cycles_at(100);
		}
	}

	// ------------------------------------------------------------ summary
	std::printf("\n== end at %.3f ms: %llu instruction starts\n", ms(now()), (unsigned long long)instructions);
	for (int i = 0; i < 2; i++)
		std::printf("CPU%d: pc %04X  sp %04X  xp %04X  yp %04X  fs %02X  ie %03X  iemask %d\n", i, cpu.pc(i), cpu.sp(i),
				cpu.xp(i), cpu.yp(i), cpu.fs(i), cpu.ie(i), cpu.iemask(i));
	std::printf("CPUM %02X  pending IRQ requests %03X\n", cpu.cpum(), cpu.irq_requests());

	std::printf("\n== entries at vector targets (first, count)\n");
	for (auto &v : vectors)
	{
		if (v.first < 0x40 || !vector_hits[v.first])
			continue;
		std::printf("  %04X  %-30s %9.3f ms  %llu (%.1f/s)\n", v.first, v.second.c_str(), ms(vector_first[v.first]),
				(unsigned long long)vector_hits[v.first], double(vector_hits[v.first]) / seconds);
	}

	std::printf("\n== where the CPU spends the last %.0f ms (instruction starts)\n", ms(total - window_start));
	std::vector<std::pair<u64, u16>> top;
	u64 window_total = 0;
	for (auto &h : hot)
	{
		top.push_back({h.second, h.first});
		window_total += h.second;
	}
	std::sort(top.rbegin(), top.rend());
	for (size_t i = 0; i < top.size() && i < 12; i++)
		std::printf("  %04X  %6.2f %%  %s\n", top[i].second, 100.0 * double(top[i].first) / double(window_total), disasm(top[i].second).c_str());

	std::printf("\n== on-chip registers written (count, last, values)\n");
	for (auto &s : sfrs)
	{
		if (s.second.writes == 0)
			continue;
		std::printf("  (x'%02X') %8llu  last %02X  values", s.first, (unsigned long long)s.second.writes, s.second.last);
		int n = 0;
		for (auto &v : s.second.values)
			if (n++ < 10)
				std::printf(" %02X", v.first);
		if (s.second.values.size() > 10)
			std::printf(" ... (%zu)", s.second.values.size());
		std::printf("\n");
	}
	std::printf("== on-chip registers only read (count, pcs)\n");
	for (auto &s : sfrs)
		if (s.second.writes == 0)
		{
			std::printf("  (x'%02X') %8llu  pcs", s.first, (unsigned long long)s.second.reads);
			int n = 0;
			for (u16 pc : s.second.read_pcs)
				if (n++ < 8)
					std::printf(" %04X", pc);
			std::printf("\n");
		}

	std::printf("\n== ADC conversions per second (channel/mux address: pot)\n");
	for (auto &c : adc_counts)
	{
		if (c.first.first == 0)
			std::printf("  ch0/%-2d %-18s %8.1f/s\n", c.first.second, bsrack::kMuxPotNames[c.first.second], double(c.second) / seconds);
		else
			std::printf("  ch%d    %-18s %8.1f/s\n", c.first.first, c.first.first == 3 ? bsrack::kMuxPotNames[16] : c.first.first == 1 ? "CV IN" : "?",
					double(c.second) / seconds);
	}
	std::printf("  order after 1 s:");
	for (auto &o : adc_order)
		if (o.first == 0)
			std::printf(" %d", o.second);
		else
			std::printf(" ch%d", o.first);
	std::printf("\n");

	std::printf("\n== CV DAC channels (S&H value changes per second, last code)\n");
	const char *cv_names[8] = {"CV OUT", "VCF", "OSCB_PWM", "OSCA_PWM", "OSCB_CV", "OSCA_CV", "RES", "VCA (env 1)"};
	for (int c = 0; c < 8; c++)
		std::printf("  %d %-12s %8.1f/s  %03X\n", c, cv_names[c], double(cv_updates[size_t(c)]) / seconds, board.cv[size_t(c)]);
	std::printf("  osc levels: (x'34') osc 1 %02X, (x'32') osc 2 %02X; port 1 %02X, port 3 %02X\n",
			cpu.reg(0x34), cpu.reg(0x32), board.port1(), board.port3());
	const u64 last = now() * mn1880_device::clocks_per_cycle;
	std::printf("  sync edges/s over the last 500 ms: OSCA %.3f  OSCB %.3f (%zu / %zu edges in all)\n",
			rate(0, last - u64(g_clock / 2), last), rate(1, last - u64(g_clock / 2), last), edges[0].size(), edges[1].size());

	std::printf("\n== latches\n");
	for (Latch &l : latches)
	{
		std::printf("  %-16s %8llu writes, %d changes, last %02X; values", l.name, (unsigned long long)l.writes, l.changes, l.value);
		int n = 0;
		for (auto &v : l.values)
			if (n++ < 12)
				std::printf(" %02X:%llu", v.first, (unsigned long long)v.second);
		if (l.values.size() > 12)
			std::printf(" ... (%zu)", l.values.size());
		std::printf("\n");
	}
	std::printf("  display: tens %02X units %02X (latch bits d dp e c b a f g)\n", board.digits[1], board.digits[0]);
	std::printf("  RAM: program %d, MIDI RX ch %d; E0 %02X E1 %02X\n", cpu.ram(0x187), cpu.ram(0x188), cpu.ram(0xe0), cpu.ram(0xe1));

	std::printf("\n== peripheral outputs\n");
	for (int p = 0; p < 10; p++)
		if (port_writes[size_t(p)])
			std::printf("  port %d: %llu writes, last %02X\n", p, (unsigned long long)port_writes[size_t(p)], port_last[size_t(p)]);
	std::printf("  MIDI out bytes %llu\n", (unsigned long long)tx_bytes);
	std::printf("  timers: 0x20 %02X%02X  0x22 %02X%02X  0x24 %02X%02X  0x26 %02X%02X  run(0x31) %02X  ctl(0x33) %02X  t3ctl(0x35) %02X\n",
			cpu.reg(0x21), cpu.reg(0x20), cpu.reg(0x23), cpu.reg(0x22), cpu.reg(0x25), cpu.reg(0x24), cpu.reg(0x27), cpu.reg(0x26),
			cpu.reg(0x31), cpu.reg(0x33), cpu.reg(0x35));

	std::printf("\n== MAME log messages: %zu\n", log_lines.size());
	std::printf("== unmapped accesses: %zu addresses\n", unmapped.size());
	for (auto &u : unmapped)
		std::printf("  %s: %llu\n", u.first.c_str(), (unsigned long long)u.second);

	if (save_eeprom)
	{
		FILE *f = std::fopen(save_eeprom, "wb");
		if (!f || std::fwrite(board.eeprom.data(), 1, board.eeprom.size(), f) != board.eeprom.size())
		{
			std::fprintf(stderr, "%s: can't write\n", save_eeprom);
			return 1;
		}
		std::fclose(f);
		std::printf("\nEEPROM image written to %s\n", save_eeprom);
	}
	return 0;
}
