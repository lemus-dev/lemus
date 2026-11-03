// tr909seq: runs the TR-909 firmware headless and scripts the panel, MIDI and sync inputs,
// logging what the CPU board sends to the voice board (triggers and DAC levels), the LEDs, the
// display and MIDI out. tools/tr909seq/tests.py runs the regression checks with it.
//
// Usage: tr909seq [--rom FILE] [--ram FILE] [--save-ram FILE] [COMMAND ...]   (or commands on stdin;
//        commands may also be joined with ';')
//   run S                 run S seconds
//   press KEY [S]         hold KEY for S seconds (default 0.1), then release and run 0.1 s
//   down KEY... / up KEY...   press or release keys (several at once)
//   keys                  list key names
//   midi HH HH ...        send MIDI bytes (hex)
//   midiclock BPM S [start|cont]   send F8 at 24 ppqn for S seconds (first FA or FB if given)
//   din run|stop|cont|off DIN SYNC IN levels: run = START/STOP high, cont = CONTINUE high too
//   dinclock BPM S        run S seconds with a 24 ppqn clock (50 % duty) on DIN pin 3
//   foot down|up          foot switch
//   tempo V / accent V    TEMPO / TOTAL ACCENT pot, 0-1;  analog CH CODE  any A/D input
//   slice US              run granularity in microseconds (default 100)
//   log trig|hits|all|off log trigger changes (default), instrument hits (HIT name code at each
//                         rising trigger, END name width at its fall), every voice write, or nothing
//   midiout on|off|all    MIDI out logging: all but F8 (default), nothing, everything
//   leds                  LED latches, the display, the step LEDs (x bright, . dim, - dark over
//                         the last 20 ms), the lit LEDs by name;  ledlevels: all 40 levels
//   ledduty S             run S seconds and print each LED's on-time (0-9; 9 always on)
//   display               the 3 digits
//   ram ADDR LEN          dump memory (hex); snap / diff: RAM changes since the last snap
//   iram                  dump the CPU's internal RAM (FF00-FFFF)
//   pc                    print the CPU's PC
//   scan S                run S seconds and report the panel/voice scan: period, its spread, the
//                         trigger latch write's place in it
//   tape S                run S seconds and report the TAPE SYNC output's (TO) half periods
//                         and the tape data output's (PB6-7) changes
//   echo TEXT             print TEXT
//   cov on|mark|new, flow S [LO HI]   firmware exploration (see below)
//   invert                read the key returns inverted (MAME's polarity: the firmware then
//                         sees every key pressed)
#include "tr909/core/machine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using tr909::Machine;

static const char *kKeyNames[] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16",
	"SHIFT", "SCALE", "R2B2", "INST", "SHUFFLE", "LAST", "STOP", "START",
	"CLEAR", "TRACK1", "TRACK2", "TRACK3", "TRACK4", "PATT1", "PATT2", "PATT3",
	"ENTER", "TAPESYNC", "CYCLE", "AVAIL", "FWD", "BACK", "TEMPO", "EXTINST",
};

static const char *kLedNames[] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16",
	"-", "TRACK1", "TRACK2", "TRACK3", "TRACK4", "PATT1", "PATT2", "PATT3",
	"ENTER", "EXTINST", "TEMPO", "BACK", "FWD", "CYCLE", "AVAIL", "TAPESYNC",
	"SCALE1", "SCALE2", "SCALE3", "SCALE4", "-", "-", "-", "-",
};

static int keyIndex(const std::string &s)
{
	for (int i = 0; i < int(Machine::Key::Count); i++)
		if (s == kKeyNames[i])
			return i;
	return -1;
}

static std::vector<uint8_t> readFile(const char *path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Instruments on the trigger latches: latch, bit, DAC.
struct Inst { const char *name; int reg, bit, dac; };
static const Inst kInst[] = {
	{"BD", 9, 1, 1}, {"SD", 9, 2, 2}, {"LT", 9, 3, 3}, {"MT", 9, 4, 4}, {"HT", 9, 5, 5},
	{"RS", 10, 1, 6}, {"HH", 10, 2, 7}, {"CR", 10, 4, 8}, {"RD", 10, 5, 8}, {"HC", 10, 6, 6},
};

struct Runner {
	Machine m;
	enum { LogOff, LogTrig, LogHits, LogAll } log = LogTrig;
	int midiLog = 1; // 0 off, 1 all but F8, 2 all
	uint8_t t1 = 0, t2 = 0;
	double sliceUs = 100;
	double rise[10] = {};
	std::vector<uint8_t> cov = std::vector<uint8_t>(0x10000), mark = std::vector<uint8_t>(0x10000);
	bool covOn = false;
	std::vector<uint8_t> snapRam;
	// optional per-slice hook (clock generators)
	std::function<void(double)> perSlice;

	double seconds() const { return double(m.states()) / Machine::kStateHz; }

	void voiceWrite(double t, const Machine::VoiceWrite &w)
	{
		if (log == LogAll)
			std::printf("%10.3f ms  %s%-2d = %02X\n", t, w.reg <= 8 ? "DAC" : "TRIG", w.reg <= 8 ? w.reg : w.reg - 8, w.value);
		if (w.reg < 9)
			return;
		uint8_t &prev = w.reg == 9 ? t1 : t2;
		if (log == LogTrig && w.value != prev) {
			std::printf("%10.3f ms  TRIG%d %02X -> %02X   DAC", t, w.reg - 8, prev, w.value);
			for (int d = 1; d <= 8; d++)
				std::printf(" %02X", m.dac(d));
			std::printf("\n");
		}
		if (log == LogHits)
			for (int i = 0; i < 10; i++) {
				const Inst &n = kInst[i];
				if (n.reg != w.reg)
					continue;
				const bool was = (prev >> n.bit) & 1, is = (w.value >> n.bit) & 1;
				if (!was && is) {
					const char *name = n.name;
					if (i == 6)
						name = (w.value & 0x08) ? "CH" : "OH";
					std::printf("%10.3f ms  HIT %s %d\n", t, name, m.dac(n.dac) >> 1);
					rise[i] = t;
				}
				else if (was && !is)
					std::printf("%10.3f ms  END %s %.3f\n", t, i == 6 ? "HH" : n.name, t - rise[i]);
			}
		prev = w.value;
	}

	void run(double s)
	{
		const int slice = std::max(1, int(sliceUs * 1e-6 * Machine::kStateHz + 0.5));
		long n = long(s * Machine::kStateHz / slice + 0.5);
		for (long i = 0; i < n; i++) {
			const double t0 = seconds();
			if (perSlice)
				perSlice(t0);
			m.run(slice);
			for (const auto &w : m.writes())
				voiceWrite((t0 + w.state / Machine::kStateHz) * 1000.0, w);
			for (uint8_t b : m.takeMidiOut())
				if (midiLog == 2 || (midiLog == 1 && b != 0xf8))
					std::printf("%10.3f ms  MIDI OUT %02X\n", seconds() * 1000.0, b);
		}
	}

	void printLeds()
	{
		std::printf("%10.3f ms  LEDS", seconds() * 1000.0);
		for (int s = 0; s < 5; s++) std::printf(" PB%d:%02X", s, m.ledLatch()[size_t(s)]);
		std::printf("  display [");
		for (int d = 0; d < 3; d++) { int v = m.digit(d); std::printf("%c", v < 10 ? '0' + v : ' '); }
		// step LEDs by brightness over the last ~20 ms: x bright, . dim, - dark
		std::printf("]  steps ");
		for (int i = 0; i < 16; i++) {
			const float l = m.ledLevel(Machine::Led(i));
			std::printf("%c", l >= 0.6f ? 'x' : l > 0.08f ? '.' : '-');
		}
		std::printf("  lit");
		for (int i = 16; i < 40; i++)
			if (kLedNames[i][0] != '-' && m.led(Machine::Led(i)))
				std::printf(" %s", kLedNames[i]);
		std::printf("\n");
	}

	// The panel/voice scan: entries (PC = 012B) and the trigger latch writes.
	void scanStats(double sec)
	{
		std::vector<double> entry, trigw;
		m.setTrace([&](uint16_t pc) {
			if (covOn) cov[pc] = 1;
			if (pc == 0x012b) entry.push_back(double(m.statesNow()));
		});
		const auto oldLog = log;
		log = LogOff;
		const int slice = std::max(1, int(sliceUs * 1e-6 * Machine::kStateHz + 0.5));
		long n = long(sec * Machine::kStateHz / slice + 0.5);
		for (long i = 0; i < n; i++) {
			const double s0 = double(m.states());
			m.run(slice);
			for (const auto &w : m.writes())
				if (w.reg == 9) trigw.push_back(s0 + w.state);
			m.takeMidiOut();
		}
		log = oldLog;
		if (covOn) m.setTrace([this](uint16_t pc) { cov[pc] = 1; }); else m.setTrace(nullptr);
		auto report = [](const char *what, const std::vector<double> &t) {
			if (t.size() < 3) { std::printf("%s: too few\n", what); return; }
			std::map<long, int> hist;
			double sum = 0, mn = 1e9, mx = 0;
			for (size_t i = 1; i < t.size(); i++) {
				const double d = (t[i] - t[i - 1]) / Machine::kStateHz * 1e6;
				sum += d; mn = std::min(mn, d); mx = std::max(mx, d);
				hist[long(std::floor(d / 8.0)) * 8]++; // 8 us bins
			}
			std::printf("%s: n=%zu mean %.1f us min %.1f max %.1f\n", what, t.size() - 1, sum / double(t.size() - 1), mn, mx);
			for (auto &h : hist) std::printf("  %5ld-%5ld us  %d\n", h.first, h.first + 8, h.second);
		};
		report("scan entry period", entry);
		report("trigger latch write period", trigw);
		// write time after the scan's entry
		std::vector<double> lag;
		size_t j = 0;
		for (double w : trigw) {
			while (j + 1 < entry.size() && entry[j + 1] <= w) j++;
			if (j < entry.size() && entry[j] <= w) lag.push_back((w - entry[j]) / Machine::kStateHz * 1e6);
		}
		if (!lag.empty()) {
			double mn = *std::min_element(lag.begin(), lag.end()), mx = *std::max_element(lag.begin(), lag.end()), s = 0;
			for (double v : lag) s += v;
			std::printf("trigger write after scan entry: mean %.1f us min %.1f max %.1f\n", s / double(lag.size()), mn, mx);
		}
	}

	void tapeStats(double sec)
	{
		const int slice = 4; // 1 us
		long n = long(sec * Machine::kStateHz / slice + 0.5);
		int prev = m.tapeSyncOut();
		double last = -1;
		int dataPrev = m.portB() >> 6, dataChanges = 0, dataSeen = 1 << dataPrev;
		std::map<long, int> hist;
		const auto oldLog = log;
		log = LogOff;
		for (long i = 0; i < n; i++) {
			m.run(slice);
			m.takeMidiOut();
			const int dv = m.portB() >> 6;
			if (dv != dataPrev) { dataChanges++; dataSeen |= 1 << dv; dataPrev = dv; }
			const int v = m.tapeSyncOut();
			if (v != prev) {
				const double t = double(m.states()) / Machine::kStateHz * 1e6;
				if (last >= 0) hist[long(std::lround((t - last) / 4.0)) * 4]++;
				last = t;
				prev = v;
			}
		}
		log = oldLog;
		std::printf("TAPE SYNC half periods:");
		for (auto &h : hist) std::printf("  %ld us x%d", h.first, h.second);
		std::printf("\n");
		std::printf("TAPE DATA (PB6-7) changes: %d, levels seen", dataChanges);
		for (int i = 0; i < 4; i++) if (dataSeen & (1 << i)) std::printf(" %d", i);
		std::printf("\n");
	}

	bool command(std::istringstream &in, const std::string &cmd)
	{
		if (cmd == "run") { double s = 1; in >> s; run(s); }
		else if (cmd == "press") {
			std::string k; in >> k;
			int i = keyIndex(k);
			if (i < 0) { std::fprintf(stderr, "unknown key %s\n", k.c_str()); return false; }
			double s = 0.1; in >> s;
			m.setKey(Machine::Key(i), true);
			run(s);
			m.setKey(Machine::Key(i), false);
			run(0.1);
		}
		else if (cmd == "down" || cmd == "up") {
			std::string k;
			while (in >> k) {
				int i = keyIndex(k);
				if (i < 0) { std::fprintf(stderr, "unknown key %s\n", k.c_str()); return false; }
				m.setKey(Machine::Key(i), cmd == "down");
			}
		}
		else if (cmd == "midi") {
			std::vector<uint8_t> b; std::string h;
			while (in >> h) b.push_back(uint8_t(std::strtoul(h.c_str(), nullptr, 16)));
			m.midiIn(b.data(), b.size());
		}
		else if (cmd == "midiclock") {
			double bpm = 120, sec = 1; std::string how; in >> bpm >> sec >> how;
			if (how == "start") { uint8_t b = 0xfa; m.midiIn(&b, 1); }
			else if (how == "cont") { uint8_t b = 0xfb; m.midiIn(&b, 1); }
			const double period = 60.0 / bpm / 24.0;
			double next = seconds();
			perSlice = [&](double t) { if (t >= next) { uint8_t b = 0xf8; m.midiIn(&b, 1); next += period; } };
			run(sec);
			perSlice = nullptr;
		}
		else if (cmd == "din") {
			std::string s; in >> s;
			dinRun = s == "run" || s == "cont";
			dinCont = s == "cont";
			m.setDinSync(dinRun, false, dinCont);
		}
		else if (cmd == "dinclock") {
			double bpm = 120, sec = 1; in >> bpm >> sec;
			const double period = 60.0 / bpm / 24.0;
			const double t0 = seconds();
			perSlice = [&](double t) { m.setDinSync(dinRun, std::fmod(t - t0, period) < period / 2, dinCont); };
			run(sec);
			perSlice = nullptr;
			m.setDinSync(dinRun, false, dinCont);
		}
		else if (cmd == "foot") { std::string s; in >> s; m.setFootSwitch(s == "down"); }
		else if (cmd == "tempo") { double v; in >> v; m.setTempoPot(v); }
		else if (cmd == "accent") { double v; in >> v; m.setTotalAccentPot(v); }
		else if (cmd == "analog") { int c; unsigned v; in >> c >> std::hex >> v; m.setAnalog(c, uint8_t(v)); }
		else if (cmd == "slice") { in >> sliceUs; }
		else if (cmd == "log") {
			std::string s; in >> s;
			log = s == "all" ? LogAll : s == "off" ? LogOff : s == "hits" ? LogHits : LogTrig;
		}
		else if (cmd == "midiout") { std::string s; in >> s; midiLog = s == "off" ? 0 : s == "all" ? 2 : 1; }
		else if (cmd == "leds") printLeds();
		else if (cmd == "ledduty") {
			// ledduty S: run S seconds, then print each LED's on-time fraction (latch sampled
			// every 0.5 ms): steps as 16 digits 0-9 (9 = always on), then the lit others
			double sec = 0.4; in >> sec;
			std::vector<double> on(40, 0);
			long n = long(sec * 2000);
			const auto oldLog = log;
			log = LogOff;
			for (long i = 0; i < n; i++) {
				run(0.0005);
				for (int l = 0; l < 40; l++) on[size_t(l)] += m.led(Machine::Led(l)) ? 1 : 0;
			}
			log = oldLog;
			std::printf("%10.3f ms  DUTY steps ", seconds() * 1000.0);
			for (int l = 0; l < 16; l++) std::printf("%d", int(std::lround(on[size_t(l)] / double(n) * 9)));
			std::printf("  lit");
			for (int l = 16; l < 40; l++)
				if (kLedNames[l][0] != '-' && on[size_t(l)] > 0)
					std::printf(" %s:%d", kLedNames[l], int(std::lround(on[size_t(l)] / double(n) * 9)));
			std::printf("\n");
		}
		else if (cmd == "ledlevels") {
			std::printf("%10.3f ms  LEVELS", seconds() * 1000.0);
			for (int i = 0; i < 40; i++) std::printf(" %.2f", m.ledLevel(Machine::Led(i)));
			std::printf("\n");
		}
		else if (cmd == "display") {
			std::printf("%10.3f ms  DISPLAY [", seconds() * 1000.0);
			for (int d = 0; d < 3; d++) { int v = m.digit(d); std::printf("%c", v < 10 ? '0' + v : ' '); }
			std::printf("]\n");
		}
		else if (cmd == "ram") {
			std::string a, l; in >> a >> l;
			unsigned addr = std::strtoul(a.c_str(), nullptr, 16), len = std::strtoul(l.c_str(), nullptr, 16);
			for (unsigned i = 0; i < len; i++) {
				if (i % 16 == 0) std::printf("%s%04X:", i ? "\n" : "", addr + i);
				std::printf(" %02X", m.peek(uint16_t(addr + i)));
			}
			std::printf("\n");
		}
		else if (cmd == "snap") snapRam.assign(m.ram().begin(), m.ram().end());
		else if (cmd == "diff") {
			if (snapRam.empty()) snapRam.assign(m.ram().size(), 0);
			int shown = 0;
			for (size_t i = 0; i < m.ram().size(); i++)
				if (m.ram()[i] != snapRam[i] && shown++ < 400)
					std::printf("RAM %04zX: %02X -> %02X\n", 0x2000 + i, snapRam[i], m.ram()[i]);
			if (shown > 400) std::printf("RAM ... %d changes in all\n", shown);
			snapRam.assign(m.ram().begin(), m.ram().end());
		}
		else if (cmd == "iram") {
			for (int i = 0; i < 256; i++) {
				if (i % 16 == 0) std::printf("%sFF%02X:", i ? "\n" : "", i);
				std::printf(" %02X", m.iram(i));
			}
			std::printf("\n");
		}
		else if (cmd == "scan") { double s = 1; in >> s; scanStats(s); }
		else if (cmd == "tape") { double s = 1; in >> s; tapeStats(s); }
		else if (cmd == "echo") { std::string rest; std::getline(in, rest); std::printf("%10.3f ms  ECHO%s\n", seconds() * 1000.0, rest.c_str()); }
		else if (cmd == "cov") {
			// cov on | mark | new : coverage of executed addresses; "new" lists ranges run
			// since "mark" that never ran before it
			std::string a; in >> a;
			if (a == "on" && !covOn) { covOn = true; m.setTrace([this](uint16_t pc) { cov[pc] = 1; }); }
			else if (a == "mark") mark = cov;
			else if (a == "new") {
				int start = -1, last = -100;
				for (int i = 0; i <= 0x10000; i++) {
					const bool hit = i < 0x10000 && cov[size_t(i)] && !mark[size_t(i)];
					if (hit && start < 0) start = i;
					if (hit) last = i;
					if (start >= 0 && (i == 0x10000 || (!hit && i - last > 4))) {
						std::printf("new %04X-%04X\n", start, last);
						start = -1;
					}
				}
			}
		}
		else if (cmd == "flow") {
			// flow S [LO HI]: run S seconds and list the jumps (non-sequential PC changes) taken
			// with counts, in order of first use; LO-HI (hex) limits it to jumps from that range
			double sec = 0.1; std::string lo = "0", hi = "FFFF"; in >> sec >> lo >> hi;
			const unsigned l = std::strtoul(lo.c_str(), nullptr, 16), h = std::strtoul(hi.c_str(), nullptr, 16);
			std::vector<std::pair<uint32_t, uint32_t>> order; // (from<<16|to, count)
			std::unordered_map<uint32_t, size_t> pos;
			uint16_t prev = m.pc();
			m.setTrace([&](uint16_t pc) {
				if (covOn) cov[pc] = 1;
				if ((pc < prev || pc > prev + 4) && prev >= l && prev <= h) {
					uint32_t key = uint32_t(prev) << 16 | pc;
					auto it = pos.find(key);
					if (it == pos.end()) { pos[key] = order.size(); order.push_back({key, 1}); }
					else order[it->second].second++;
				}
				prev = pc;
			});
			run(sec);
			for (auto &e : order) std::printf("%04X -> %04X  x%u\n", e.first >> 16, e.first & 0xffff, e.second);
			if (covOn) m.setTrace([this](uint16_t pc) { cov[pc] = 1; }); else m.setTrace(nullptr);
		}
		else if (cmd == "invert") m.setKeyPolarityInverted(true);
		else if (cmd == "pc") std::printf("PC=%04X\n", m.pc());
		else if (cmd == "keys") { for (auto *k : kKeyNames) std::printf("%s ", k); std::printf("\n"); }
		else { std::fprintf(stderr, "unknown command %s\n", cmd.c_str()); return false; }
		return true;
	}

	bool dinRun = false, dinCont = false;
};

int main(int argc, char **argv)
{
	std::string rom = TR909_ROM_PATH;
	const char *ramIn = nullptr, *ramOut = nullptr;
	std::vector<std::string> cmds;
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--rom") && i + 1 < argc) rom = argv[++i];
		else if (!std::strcmp(argv[i], "--ram") && i + 1 < argc) ramIn = argv[++i];
		else if (!std::strcmp(argv[i], "--save-ram") && i + 1 < argc) ramOut = argv[++i];
		else {
			// commands separated by ';' or as separate args
			std::string s = argv[i];
			size_t p;
			while ((p = s.find(';')) != std::string::npos) { cmds.push_back(s.substr(0, p)); s = s.substr(p + 1); }
			if (!s.empty()) cmds.push_back(s);
		}
	}
	Runner r;
	auto romData = readFile(rom.c_str());
	if (!r.m.setRom(romData.data(), romData.size())) { std::fprintf(stderr, "bad ROM %s\n", rom.c_str()); return 1; }
	if (ramIn) { auto d = readFile(ramIn); r.m.setRam(d.data(), d.size()); }
	r.m.powerOn();
	auto exec = [&](const std::string &line) {
		std::istringstream in(line);
		std::string cmd;
		if (in >> cmd) r.command(in, cmd);
	};
	if (cmds.empty()) { std::string line; while (std::getline(std::cin, line)) exec(line); }
	else for (auto &c : cmds) exec(c);
	std::fflush(stdout);
	if (ramOut) { std::ofstream f(ramOut, std::ios::binary); f.write((const char *)r.m.ram().data(), r.m.ram().size()); }
}
