// The board headless: boots the firmware from reset, prints the LCD as it changes and what the
// firmware set up, optionally holds notes, writes a WAV at 46875 Hz.
//   viruscrun <rom> <seconds> [out.wav] [note-on-at-seconds] [notes]
#include "virusc/core/machine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "viruscrun <rom> <seconds> [out.wav] [note-on-at] [notes]\n");
		return 2;
	}
	std::setvbuf(stdout, nullptr, _IOLBF, 0); // line by line, so a crash doesn't swallow the last lines
	std::ifstream f(argv[1], std::ios::binary);
	std::vector<char> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	static virusc::Machine m;
	if (!m.setRom(reinterpret_cast<const uint8_t *>(rom.data()), rom.size())) {
		std::fprintf(stderr, "bad ROM (%zu bytes)\n", rom.size());
		return 1;
	}
	const double seconds = std::atof(argv[2]);
	const char *out = argc > 3 ? argv[3] : nullptr;
	const double noteAt = argc > 4 ? std::atof(argv[4]) : -1;
	const int notes = argc > 5 ? std::atoi(argv[5]) : 1;

	if (const char *v = std::getenv("VC_VOLUME"))
		m.setKnob(2, std::atoi(v));
	// VC_KEYS="seconds:row:bit:down,...": panel keys. VC_TRACE=from-seconds: data writes outside RAM and P5 changes.
	struct KeyAt { double t; int row, bit, down; };
	std::vector<KeyAt> keys;
	if (const char *k = std::getenv("VC_KEYS"))
		for (const char *p = k; *p;) {
			KeyAt e{};
			if (std::sscanf(p, "%lf:%d:%d:%d", &e.t, &e.row, &e.bit, &e.down) == 4)
				keys.push_back(e);
			p = std::strchr(p, ',');
			if (!p)
				break;
			++p;
		}
	// VC_SCRIPT=file: lines "seconds key row bit 0|1", "seconds knob index value", "seconds show" (LCD and LEDs),
	// "seconds midi b1 b2 b3", "seconds note n velocity", "seconds sysex F0 .. F7" (hex), "seconds dump n" (RAM to dump-n.bin).
	struct Step { double t; std::string what; int a, b, c; std::vector<uint8_t> bytes; };
	std::vector<Step> script;
	if (const char *sf = std::getenv("VC_SCRIPT")) {
		std::ifstream in(sf);
		std::string line;
		while (std::getline(in, line)) {
			Step st{};
			char what[16] = {0};
			if (std::sscanf(line.c_str(), "%lf %15s %d %d %d", &st.t, what, &st.a, &st.b, &st.c) >= 2) {
				st.what = what;
				if (st.what == "sysex") { // "seconds sysex F0 00 20 ... F7", hex
					std::istringstream hex(line.substr(line.find("sysex") + 5));
					std::string byte;
					while (hex >> byte)
						st.bytes.push_back(uint8_t(std::stoul(byte, nullptr, 16)));
				}
				script.push_back(st);
			}
		}
	}
	size_t nextStep = 0;
	const double traceFrom = std::getenv("VC_TRACE") ? std::atof(std::getenv("VC_TRACE")) : 1e9;
	int traced = 0;
	// VC_WORDS=from,to: every word the 80C515 sends the DSP in that span (seconds), with HF0/HF1; with
	// VC_HOSTRAW, every write to the host port registers too.
	double wordsFrom = 1e9, wordsTo = 0;
	if (const char *w = std::getenv("VC_WORDS"))
		std::sscanf(w, "%lf,%lf", &wordsFrom, &wordsTo);
	uint8_t icr = 0, tx[3] = {};
	// VC_WATCH=hex address or from-to: every write there, with the code that made it and the return addresses.
	long watch = -1, watchTo = -1;
	if (const char *w = std::getenv("VC_WATCH")) {
		char *end = nullptr;
		watch = watchTo = std::strtol(w, &end, 16);
		if (end && *end == '-')
			watchTo = std::strtol(end + 1, nullptr, 16);
	}
	m.onXWrite = [&](uint16_t pc, uint16_t a, uint8_t d, uint8_t p5) {
		if (a >= watch && a <= watchTo)
		{
			std::printf("WATCH %9.4f s pc %04x write %04x = %02x P5 %02x returns:", double(m.frames()) / virusc::kSampleRate, pc, a, d, p5);
			for (int k = m.mcu().sp(); k > 8 && k > m.mcu().sp() - 12; k -= 2) // return addresses, high byte on top
				std::printf(" %02x%02x", m.mcu().iram(uint8_t(k)), m.mcu().iram(uint8_t(k - 1)));
			std::printf("\n");
		}
		if (a >= 0x0400 && a <= 0x0407) {
			const int r = a & 7;
			if (std::getenv("VC_HOSTRAW") && double(m.frames()) / virusc::kSampleRate >= wordsFrom && double(m.frames()) / virusc::kSampleRate < wordsTo)
				std::printf("HOSTW %9.5f s pc %04x reg %d = %02x\n", double(m.frames()) / virusc::kSampleRate, pc, r, d);
			if (r == 0)
				icr = d;
			else if (r >= 5) {
				tx[r - 5] = d;
				const double t = double(m.frames()) / virusc::kSampleRate;
				if (r == 7 && t >= wordsFrom && t < wordsTo) // ICR bit 5 (HLEND): register 5 is the low byte
					std::printf("WORD %9.4f s pc %04x HF%d%d %06x\n", t, pc, (icr >> 4) & 1, (icr >> 3) & 1,
					            (icr & 0x20) ? (tx[2] << 16) | (tx[1] << 8) | tx[0] : (tx[0] << 16) | (tx[1] << 8) | tx[2]);
			}
		}
		if (double(m.frames()) / virusc::kSampleRate < traceFrom || traced > 4000)
			return;
		const bool flashWindow = a >= 0x8000 && !(p5 & 8), unlock = a == 0x5555 || a == 0x2aaa;
		if (!flashWindow && !unlock)
			return;
		++traced;
		std::printf("%9.4f s  pc %04x  write %04x = %02x  P5 %02x\n", double(m.frames()) / virusc::kSampleRate, pc, a, d, p5);
	};
	std::map<int, int> readsByP5;
	m.onXRead = [&](uint16_t, uint16_t, uint8_t p5) {
		if (double(m.frames()) / virusc::kSampleRate >= traceFrom)
			++readsByP5[p5];
	};
	m.onP5 = [&](uint8_t p5) {
		if (double(m.frames()) / virusc::kSampleRate >= traceFrom && traced <= 400 && (p5 & 0xf0) != 0x00 && (p5 & 0x0f) == 0x08) {}
	};
	// VC_PORTS=from-seconds: 4 ms of P1/P3/P4/P5 writes and P4 reads, with the 80C515's cycle count.
	const double portsFrom = std::getenv("VC_PORTS") ? std::atof(std::getenv("VC_PORTS")) : 1e9;
	const auto portsActive = [&] {
		const double t = double(m.frames()) / virusc::kSampleRate;
		return t >= portsFrom && t < portsFrom + 0.004;
	};
	// VC_P4FIRST=n: the first n reads of P4 (the key rows), with the row P5 selects and what was read.
	const int p4First = std::getenv("VC_P4FIRST") ? std::atoi(std::getenv("VC_P4FIRST")) : 0;
	int p4Reads = 0;
	uint8_t lastP5 = 0;
	// VC_DSPPROF=from,to: DSP instructions by the PC their step started at, in that span, the busiest first.
	std::map<uint32_t, uint64_t> dspProf;
	double profFrom = 1e9, profTo = 0;
	if (const char *dp = std::getenv("VC_DSPPROF")) {
		std::sscanf(dp, "%lf,%lf", &profFrom, &profTo);
		m.onDspBlock = [&](uint32_t pc, uint64_t n) {
			const double t = double(m.frames()) / virusc::kSampleRate;
			if (t >= profFrom && t < profTo) {
				dspProf[pc] += n;
				// the idle loop at $2C087-$2C0A7: a step that starts and ends in it
				const uint32_t end = m.dspPc();
				if (pc >= 0x2c087 && pc <= 0x2c0a7 && end >= 0x2c087 && end <= 0x2c0a7)
					dspProf[0xffffff] += n;
			}
		};
	}
	// VC_PINS=from,to: every change of P1 and P3 in that span (the pins that aren't the scan and banks).
	double pinsFrom = 1e9, pinsTo = 0;
	if (const char *pp = std::getenv("VC_PINS"))
		std::sscanf(pp, "%lf,%lf", &pinsFrom, &pinsTo);
	uint8_t lastPin[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	m.onPortOut = [&](int port, uint8_t d) {
		const double tp = double(m.frames()) / virusc::kSampleRate;
		if ((port == 1 || port == 3) && tp >= pinsFrom && tp < pinsTo && d != lastPin[port])
			std::printf("PIN %9.5f s pc %04x P%d %02x -> %02x\n", tp, m.mcu().pc(), port, lastPin[port], d);
		if (port < 8)
			lastPin[port] = d;
		if (port == 5)
			lastP5 = d;
		if (portsActive() && (port == 4 || port == 5))
			std::printf("PORT %10llu pc %04x P%d <- %02x\n", (unsigned long long)m.mcu().cycles(), m.mcu().pc(), port, d);
	};
	m.onPortIn = [&](int port) {
		if (port == 4 && p4Reads < p4First) {
			++p4Reads;
			std::printf("P4READ %9.5f s pc %04x P5 %02x\n", double(m.frames()) / virusc::kSampleRate, m.mcu().pc(), lastP5);
		}
		if (portsActive() && port == 4)
			std::printf("PORT %10llu pc %04x P4 read\n", (unsigned long long)m.mcu().cycles(), m.mcu().pc());
		if (port == 4 && std::getenv("VC_SCANLOG")) {
			const double t = double(m.frames()) / virusc::kSampleRate;
			static double last = 0;
			if (t >= std::atof(std::getenv("VC_SCANLOG")) && t < std::atof(std::getenv("VC_SCANLOG")) + 0.3) {
				if (t - last > 0.02)
					std::printf("SCAN gap %.1f ms before %.4f s (pc %04x)\n", (t - last) * 1000, t, m.mcu().pc());
			}
			last = t;
		}
	};
	m.powerOn();
	const uint64_t total = uint64_t(seconds * virusc::kSampleRate);
	std::vector<float> audio;
	auto t0 = std::chrono::steady_clock::now();
	audio.reserve(size_t(total) * 2);
	char last[2][17] = {{0}, {0}};
	bool startedSeen = false, serialSeen = false, sent = false;
	uint64_t sampled = 0;
	// VC_LEDWIN=from,frames: from `from` s, read the LEDs every `frames` frames as the plugin does once per
	// block, and print each LED's lowest, highest and mean brightness at the end.
	double ledWinFrom = 0;
	int ledWinBlock = 0;
	if (const char *lw = std::getenv("VC_LEDWIN"))
		std::sscanf(lw, "%lf,%d", &ledWinFrom, &ledWinBlock);
	struct LedWin {
		float lo = 1, hi = 0;
		double sum = 0;
	} ledWin[virusc::Machine::kLedColumns][virusc::Machine::kLedLines];
	int ledWinCount = 0;
	bool ledWinSkip = false;
	for (uint64_t i = 0; i < total; ++i) {
		m.runFrame();
		audio.push_back(m.left());
		audio.push_back(m.right());
		if (m.dspStarted() && !startedSeen) {
			startedSeen = true;
			std::printf("%8.3f s  DSP started after %zu host words\n", double(i) / virusc::kSampleRate, m.hostWordsSent());
		}
		if (!serialSeen && (m.mcu().scon() & 0x10)) {
			serialSeen = true;
			std::printf("%8.3f s  UART on: SCON %02x TMOD %02x TH1 %02x PCON %02x ADCON %02x\n", double(i) / virusc::kSampleRate,
			            m.mcu().scon(), m.mcu().tmod(), m.mcu().th1(), m.mcu().pcon(), m.mcu().adcon());
		}
		if (const char *bp = std::getenv("VC_PROGRAM"); bp && noteAt >= 0 && i == uint64_t((noteAt - 2.0) * virusc::kSampleRate)) { // bank,program, 2 s before the notes
			int bank = 0, prog = 0;
			std::sscanf(bp, "%d,%d", &bank, &prog);
			for (int b : {0xb0, 0x20, bank, 0xc0, prog})
				m.midiIn(uint8_t(b));
		}
		if (std::getenv("VC_MISSED") && (i + 1) % uint64_t(virusc::kSampleRate) == 0) { // once a second: frames the DSP missed
			static uint64_t last = 0;
			std::printf("MISSED %4.0f s: %llu of %d frames, %zu queued\n", double(i + 1) / virusc::kSampleRate,
			            (unsigned long long)(m.framesMissed() - last), int(virusc::kSampleRate), m.framesQueued());
			last = m.framesMissed();
		}
		if (ledWinBlock > 0 && i >= uint64_t(ledWinFrom * virusc::kSampleRate) && (i + 1) % uint64_t(ledWinBlock) == 0) {
			float duty[virusc::Machine::kLedColumns][virusc::Machine::kLedLines];
			m.ledDuty(duty);
			for (int c = 0; c < virusc::Machine::kLedColumns && ledWinSkip; ++c)
				for (int l = 0; l < virusc::Machine::kLedLines; ++l) {
					auto &s = ledWin[c][l];
					s.lo = std::min(s.lo, duty[c][l]);
					s.hi = std::max(s.hi, duty[c][l]);
					s.sum += duty[c][l];
				}
			ledWinCount += ledWinSkip;
			// The first reads hold the average since power-on until each column has its next window.
			ledWinSkip = i >= uint64_t((ledWinFrom + 0.1) * virusc::kSampleRate);
		}
		// VC_TIMERS=from: from `from` s, every 20 ms, DSP timers 1 and 2 (the LFO RATE LEDs) and lfoLed().
		if (const char *tf = std::getenv("VC_TIMERS"); tf && i >= uint64_t(std::atof(tf) * virusc::kSampleRate) && i % 937 == 0) {
			std::printf("TIMERS %.3f", double(i) / virusc::kSampleRate);
			for (int k : {1, 2}) {
				uint32_t tcsr, tlr, tcpr;
				m.dspTimer(k, tcsr, tlr, tcpr);
				std::printf("  t%d csr %06x lr %06x cpr %06x", k, tcsr, tlr, tcpr);
			}
			std::printf("  led1 %.2f led23 %.2f\n", m.lfoLed(0), m.lfoLed(1));
		}
		if (const char *dp = std::getenv("VC_DSPPC")) { // VC_DSPPC=from,to: the DSP's PC every frame in that span
			double from = 1e9, to = 0;
			std::sscanf(dp, "%lf,%lf", &from, &to);
			const double t = double(i) / virusc::kSampleRate;
			if (t >= from && t < to) {
				std::printf("DSPPC %.5f %06x %llu\n", t, m.dspPc(), (unsigned long long)m.dspInstructions());
				std::fflush(stdout);
			}
		}
		if (std::getenv("VC_MIDIOUT")) { // VC_MIDIOUT=1: every byte the 80C515 sends out of MIDI OUT, in hex
			const auto out = m.takeMidiOut();
			if (!out.empty()) {
				std::printf("MIDIOUT %9.4f s", double(i) / virusc::kSampleRate);
				for (uint8_t b : out)
					std::printf(" %02x", b);
				std::printf("\n");
			}
		}
		while (nextStep < script.size() && i >= uint64_t(script[nextStep].t * virusc::kSampleRate)) {
			const auto &st = script[nextStep++];
			if (st.what == "key")
				m.setKey(st.a, st.b, st.c != 0);
			else if (st.what == "knob")
				m.setKnob(st.a, st.b);
			else if (st.what == "duty") { // brightness 0-9 of the given lines, "c,l" packed as c*14+l in a, b, c
				float duty[virusc::Machine::kLedColumns][virusc::Machine::kLedLines];
				m.ledDuty(duty);
				std::printf("DUTY %.3f", st.t);
				for (int k : {st.a, st.b, st.c})
					if (k >= 0)
						std::printf(" %d", std::min(9, int(duty[k / 14][k % 14] * 10)));
				std::printf("\n");
			}
			else if (st.what == "midi") // up to three raw bytes, decimal; -1 for none
				for (int d : {st.a, st.b, st.c}) {
					if (d >= 0)
						m.midiIn(uint8_t(d));
				}
			else if (st.what == "sysex")
				for (uint8_t d : st.bytes)
					m.midiIn(d);
			else if (st.what == "note") // note number a, velocity b (0 = off)
				for (int d : {st.b ? 0x90 : 0x80, st.a, st.b})
					m.midiIn(uint8_t(d));
			else if (st.what == "dasm") { // dasm-<a>.txt: b instructions of the DSP's program memory from c (decimal)
				std::ofstream d("dasm-" + std::to_string(st.a) + ".txt");
				d << m.dspDisassemble(uint32_t(st.c), st.b);
			}
			else if (st.what == "dump") { // dump-<a>.bin: the 32 KB at $0000, the battery banks, then the 256 bytes of internal RAM
				std::ofstream d("dump-" + std::to_string(st.a) + ".bin", std::ios::binary);
				d.write(reinterpret_cast<const char *>(m.ram().data()), std::streamsize(m.ram().size()));
				for (int k = 0; k < 256; ++k) {
					const char v = char(m.mcu().iram(uint8_t(k)));
					d.write(&v, 1);
				}
			}
			else if (st.what == "show") {
				char t0[17] = {0}, t1[17] = {0};
				m.lcd().text(0, t0);
				m.lcd().text(1, t1);
				for (int c = 0; c < 16; ++c) {
					if (t0[c] < 32 || t0[c] > 126) t0[c] = '#';
					if (t1[c] < 32 || t1[c] > 126) t1[c] = '#';
				}
				std::printf("SHOW %.3f %d %d %d |%.16s|%.16s| leds", st.t, st.a, st.b, st.c, t0, t1);
				float duty[virusc::Machine::kLedColumns][virusc::Machine::kLedLines];
				m.ledDuty(duty);
				for (int c = 0; c < virusc::Machine::kLedColumns; ++c) {
					std::printf(" ");
					for (int l = 0; l < virusc::Machine::kLedLines; ++l)
						std::printf("%c", duty[c][l] > 0.75f ? '#' : duty[c][l] > 0.25f ? '+' : duty[c][l] > 0.02f ? '.' : '-');
				}
				std::printf("\n");
			}
		}
		for (const auto &k : keys)
			if (i == uint64_t(k.t * virusc::kSampleRate)) {
				m.setKey(k.row, k.bit, k.down != 0);
				std::printf("%8.3f s  key row %d bit %d %s\n", k.t, k.row, k.bit, k.down ? "down" : "up");
			}
		if (noteAt >= 0 && !sent && double(i) >= noteAt * virusc::kSampleRate) {
			sent = true;
			for (int n = 0; n < notes; ++n) {
				m.midiIn(0x90);
				m.midiIn(uint8_t(48 + n));
				m.midiIn(95);
			}
			std::printf("%8.3f s  %d note(s) on\n", double(i) / virusc::kSampleRate, notes);
		}
		if ((i & 0x3ff) == 0) {
			for (int l = 0; l < 2; ++l) {
				char t[17] = {0};
				m.lcd().text(l, t);
				if (std::memcmp(t, last[l], 16) != 0) {
					std::memcpy(last[l], t, 17);
					for (int c = 0; c < 16; ++c) if (t[c] < 32 || t[c] > 126) t[c] = 35;
					std::printf("%8.3f s  LCD %d |%.16s|\n", double(i) / virusc::kSampleRate, l, t);
				}
			}
		}
		if (noteAt >= 0 && std::getenv("VC_PROGRAM") && std::getenv("VC_T0") && i + 1 == uint64_t(std::atof(std::getenv("VC_T0")) * virusc::kSampleRate))
			t0 = std::chrono::steady_clock::now();
		if (const char *pcs = std::getenv("VC_PCLOG")) {
			const double t = double(i) / virusc::kSampleRate, from = std::atof(pcs);
			if (t >= from && t < from + 0.4 && (i % 47) == 0)
				std::printf("PC %.4f %04x bank %d sp %02x dsp %06x instr %llu\n", t, m.mcu().pc(), 0, m.mcu().sp(), m.dspPc(), (unsigned long long)m.dspInstructions());
		}
		if (std::getenv("VC_HOSTLOG") && i == uint64_t(std::atof(std::getenv("VC_HOSTLOG")) * virusc::kSampleRate))
			m.setHostLog(true);
		if (i >= sampled) {
			sampled = i + uint64_t(virusc::kSampleRate);
			std::printf("%8.3f s  mcu pc %04x, host words %zu, dsp pc %06x, dsp instr %llu, pctl %06x, dsp reboots %d\n", double(i) / virusc::kSampleRate, m.mcu().pc(),
			            m.hostWordsSent(), m.dspPc(), (unsigned long long)m.dspInstructions(), m.dspPctl(), m.dspReboots());
			printf("           sp %02x stack:", m.mcu().sp());
			for (int k = m.mcu().sp(); k > 7 && k > m.mcu().sp() - 12; --k)
				printf(" %02x", m.mcu().iram(uint8_t(k)));
			printf("\n");
		}
	}
	{
		const auto h = m.hostBySelect();
		std::printf("host port accesses: reads with P3.3 low %llu, high %llu; writes low %llu, high %llu\n", (unsigned long long)h[0],
		            (unsigned long long)h[1], (unsigned long long)h[2], (unsigned long long)h[3]);
	}
	std::printf("idle loop signature %016llx, DSP instructions %llu, skipped in the idle loop %llu\n", (unsigned long long)m.dspIdleLoopSignature(),
	            (unsigned long long)m.dspInstructions(), (unsigned long long)m.idleSkipped());
	if (!dspProf.empty()) {
		std::vector<std::pair<uint64_t, uint32_t>> v;
		uint64_t total = 0;
		for (const auto &[pc, n] : dspProf) {
			v.push_back({n, pc});
			total += n;
		}
		std::sort(v.rbegin(), v.rend());
		std::printf("DSPPROF %llu instructions\n", (unsigned long long)total);
		for (size_t k = 0; k < v.size() && k < 40; ++k)
			std::printf("DSPPROF %06x %10llu %5.1f %%\n", v[k].second, (unsigned long long)v[k].first, 100.0 * double(v[k].first) / double(total));
	}
	for (const auto &[p5, n] : readsByP5)
		std::printf("window reads with P5 %02x: %d\n", p5, n);
	if (ledWinCount > 0) {
		int flicker = 0;
		for (int c = 0; c < virusc::Machine::kLedColumns; ++c)
			for (int l = 0; l < virusc::Machine::kLedLines; ++l) {
				const auto &s = ledWin[c][l];
				if (s.hi < 0.02f)
					continue;
				const bool varies = s.hi - s.lo > 0.1f;
				flicker += varies;
				std::printf("LED %d%c%d  lo %.2f hi %.2f mean %.2f%s\n", c, l < 7 ? 'A' : 'B', l % 7, s.lo, s.hi,
				            s.sum / ledWinCount, varies ? "  varies" : "");
			}
		std::printf("%d reads of %d frames; %d LEDs vary\n", ledWinCount, ledWinBlock, flicker);
	}
	if (const char *t = std::getenv("VC_T0")) {
		const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), span = seconds - std::atof(t);
		std::printf("%.2f s of machine in %.2f s wall: %.1f %%\n", span, wall, 100.0 * wall / span);
	}
	if (std::getenv("VC_HOSTLOG")) {
		const auto &h = m.hostLog();
		int counts[2][8] = {};
		for (const auto &e : h)
			++counts[e.write][e.offset & 7];
		std::printf("host port, reads by register:");
		for (int r = 0; r < 8; ++r)
			std::printf(" %d", counts[0][r]);
		std::printf("; writes:");
		for (int r = 0; r < 8; ++r)
			std::printf(" %d", counts[1][r]);
		std::printf("\n");
		std::printf("host log: %zu accesses\n", h.size());
		for (size_t i = 0; i < h.size() && i < 60; ++i)
			std::printf("  %10llu pc %04x %s %d = %02x\n", (unsigned long long)h[i].cycle, h[i].pc, h[i].write ? "W" : "R", h[i].offset, h[i].data);
	}
	if (out) {
		std::ofstream w(out, std::ios::binary);
		const uint32_t data = uint32_t(audio.size() * 4), rate = 46875, bytes = rate * 8, riff = 36 + data;
		const uint16_t fmt = 3, ch = 2, align = 8, bits = 32;
		const uint32_t sixteen = 16;
		w.write("RIFF", 4); w.write((char *)&riff, 4); w.write("WAVEfmt ", 8); w.write((char *)&sixteen, 4); w.write((char *)&fmt, 2); w.write((char *)&ch, 2);
		w.write((char *)&rate, 4); w.write((char *)&bytes, 4); w.write((char *)&align, 2); w.write((char *)&bits, 2); w.write("data", 4); w.write((char *)&data, 4);
		w.write(reinterpret_cast<const char *>(audio.data()), std::streamsize(audio.size() * 4));
	}
	return 0;
}
