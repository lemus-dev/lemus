// Firmware self-test: every behaviour docs/firmware.md and docs/sequencer.md describe, run
// against the real ROM. `sh101seq --selftest [--long]`; exits non-zero on a failure.
#include "sim.h"

#include <algorithm>
#include <cstdarg>
#include <map>

using namespace sh101test;
using Button = Machine::Button;
using Led = Machine::Led;
using Step = Machine::Step;

namespace {

int g_failures = 0;

void check(bool ok, const std::string &what, const char *fmt = nullptr, ...)
{
	char detail[256] = "";
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(detail, sizeof detail, fmt, ap);
		va_end(ap);
	}
	std::printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", what.c_str(), *detail ? ": " : "", detail);
	if (!ok)
		++g_failures;
}

std::string join(const std::vector<int> &v)
{
	std::string s;
	for (int x : v)
		s += (s.empty() ? "" : " ") + std::to_string(x);
	return s;
}

// A powered-on machine with blank RAM: RANGE 8', TRANSPOSE M, GATE+TRIG, settled.
struct Unit : Sim {
	explicit Unit(const std::vector<uint8_t> &rom, sh101::Trigger trig = sh101::Trigger::GateTrig) : Sim(rom)
	{
		std::array<uint8_t, Machine::kRamBytes> z{};
		m.setRam(z.data(), z.size());
		m.setRange(sh101::Range::Ft8);
		m.setTranspose(sh101::Transpose::Mid);
		m.setTrigger(trig);
		powerOn();
		ms(20);
	}
	// Runs until n more main loops have ended (P2 writes).
	void loops(int n)
	{
		const uint64_t target = m.loops() + uint64_t(n);
		while (m.loops() < target)
			advance(1 / rate);
	}
	const sh101::CpuLines &lines() const { return m.lines(); }
	int k(const char *name) const { return keyIndex(name); }
	void down(const char *name) { key(k(name), true); }
	void up(const char *name) { key(k(name), false); }
	void hold(Button b, bool down) { m.setButton(b, down); }
};

// The KCV values written while the gate rises, from the event log: one per note start.
std::vector<int> noteStarts(const Sim &s, double from)
{
	std::vector<int> r;
	int kcv = -1;
	bool gate = false;
	for (const auto &e : s.events) {
		if (e.kind == Event::Dac && e.channel == 0)
			kcv = e.value;
		if (e.kind == Event::P2) {
			const bool g = !(e.value & 0x80);
			if (g && !gate && e.t >= from)
				r.push_back(kcv);
			gate = g;
		}
	}
	return r;
}

// Every KCV change after `from`.
std::vector<int> kcvChanges(const Sim &s, double from)
{
	std::vector<int> r;
	for (const auto &e : dacEdges(s.events, 0, from))
		r.push_back(e.value);
	return r;
}

int gateRises(const Sim &s, double from, double to = 1e30)
{
	int n = 0;
	for (const auto &e : gateEdges(s.events, from, to))
		n += e.value;
	return n;
}

// --- Power-on, test mode, switches -----------------------------------------------------------

void testBoot(const std::vector<uint8_t> &rom)
{
	std::printf("Test mode (service notes p.4 table)\n");
	Sim s(rom);
	s.m.setButton(Button::Load, true);
	s.m.setButton(Button::KeyTranspose, true);
	s.powerOn();
	s.ms(20);
	check(s.m.p2() == 0xf9 && s.m.lines().kcv == 0 && s.m.lines().range == 0, "power on holding LOAD + KEY TRANSPOSE",
	      "P2 %02X KCV %d RANGE %d", s.m.p2(), s.m.lines().kcv, s.m.lines().range);
	s.m.setButton(Button::Load, false);
	s.m.setButton(Button::KeyTranspose, false);
	s.ms(10);
	struct Row {
		Button b;
		const char *name;
		int kcv, range, p2;
	};
	const Row rows[] = {{Button::Play, "PLAY", 33, 0, 0xfa}, {Button::ArpDown, "DOWN", 30, 0, 0xfb},
	                    {Button::ArpUpDown, "U&D", 57, 0, 0x7c}, {Button::ArpUp, "UP", 0, 57, 0x7d},
	                    {Button::Load, "LOAD", 0, 0, 0xf9}};
	for (const auto &r : rows) {
		s.button(r.b, 20, 20);
		const auto &l = s.m.lines();
		check(l.kcv == r.kcv && l.range == r.range && s.m.p2() == r.p2, std::string("test mode ") + r.name,
		      "KCV %d (%.3f V) RANGE %d P2 %02X", l.kcv, l.kcv / 12.0, l.range, s.m.p2());
	}
	s.button(Button::Hold, 20, 20);
	check(s.m.p2() == 0x97, "HOLD leaves test mode through INITIAL SET (and, still down, turns HOLD on)", "P2 %02X",
	      s.m.p2());

	std::printf("Switches: KCV and RANGE (service notes p.3, p.6)\n");
	Unit u(rom);
	const std::pair<sh101::Range, int> ranges[] = {
		{sh101::Range::Ft16, 12}, {sh101::Range::Ft8, 24}, {sh101::Range::Ft4, 36}, {sh101::Range::Ft2, 48}};
	std::string got;
	bool ok = true;
	for (auto [r, code] : ranges) {
		u.m.setRange(r);
		u.loops(3);
		got += std::to_string(u.lines().range) + " ";
		ok &= u.lines().range == code;
	}
	check(ok, "RANGE 16' 8' 4' 2' = 1, 2, 3, 4 V", "codes %s", got.c_str());
	u.m.setRange(sh101::Range::Ft8);
	const std::pair<sh101::Transpose, std::pair<int, int>> trs[] = {
		{sh101::Transpose::Low, {5, 36}}, {sh101::Transpose::Mid, {17, 48}}, {sh101::Transpose::High, {29, 60}}};
	for (auto [t, codes] : trs) {
		u.m.setTranspose(t);
		u.down("F3");
		u.loops(4);
		const int f3 = u.lines().kcv;
		u.up("F3");
		u.loops(4);
		u.down("C6");
		u.loops(4);
		const int c6 = u.lines().kcv;
		u.up("C6");
		u.loops(4);
		check(f3 == codes.first && c6 == codes.second,
		      std::string("TRANSPOSE ") + "LMH"[int(t)] + ": F3 and C6",
		      "%.3f V, %.3f V", f3 / 12.0, c6 / 12.0);
	}
}

// --- Keyboard --------------------------------------------------------------------------------

void testKeyboard(const std::vector<uint8_t> &rom)
{
	std::printf("Key priority (owner's manual p.29)\n");
	auto run = [&](sh101::Trigger t) {
		Unit u(rom, t);
		const double t0 = u.now();
		u.down("A4");
		u.ms(20);
		u.down("F3");
		u.ms(20);
		u.down("C6");
		u.ms(20);
		u.up("C6");
		u.ms(20);
		u.up("F3");
		u.ms(20);
		u.up("A4");
		u.ms(20);
		return std::make_pair(kcvChanges(u, t0), gateRises(u, t0));
	};
	const auto gt = run(sh101::Trigger::GateTrig);
	check(gt.first == std::vector<int>{33, 17, 48, 17, 33} && gt.second == 1,
	      "GATE+TRIG: last note; a release falls back to the lowest key held; one gate",
	      "KCV %s, %d gate", join(gt.first).c_str(), gt.second);
	const auto gate = run(sh101::Trigger::Gate);
	check(gate.first == std::vector<int>{33, 17, 33} && gate.second == 1, "GATE: lowest note; one gate",
	      "KCV %s, %d gate", join(gate.first).c_str(), gate.second);
	const auto lfo = run(sh101::Trigger::Lfo);
	check(lfo.first == std::vector<int>{33, 17, 33} && lfo.second == 1, "LFO: lowest note; one gate",
	      "KCV %s, %d gate", join(lfo.first).c_str(), lfo.second);
	{
		Unit u(rom);
		const double t0 = u.now();
		u.down("C4");
		u.down("E4");
		u.down("G4");
		u.ms(20);
		check(kcvChanges(u, t0) == std::vector<int>{24}, "GATE+TRIG, keys in the same scan: the lowest");
	}

	std::printf("Release and re-press (docs/sequencer.md, Keyboard)\n");
	for (int gap : {1, 2, 3}) {
		Unit u(rom);
		u.down("C4");
		u.ms(20);
		const double t0 = u.now();
		// Release just after a loop boundary, re-press after `gap` more boundaries.
		u.loops(1);
		u.up("C4");
		u.loops(gap);
		u.down("C4");
		u.ms(20);
		const int rises = gateRises(u, t0);
		check(rises == (gap >= 2 ? 1 : 0),
		      "released for " + std::to_string(gap) + " loop boundar" + (gap == 1 ? "y" : "ies"),
		      gap >= 2 ? "gate drops for a loop (%d new gate)" : "not seen (%d new gates)", rises);
	}
	{
		Unit u(rom);
		u.down("C4");
		u.ms(20);
		u.loops(1);
		u.up("C4");
		const double t0 = u.now();
		u.ms(30);
		const auto e = gateEdges(u.events, t0);
		const double delay = e.empty() ? 0 : e[0].t - t0;
		check(e.size() == 1 && delay > 2 * 1.7e-3 && delay < 3 * 2.5e-3, "gate off two loops after the release",
		      "%.2f ms", delay * 1000);
	}

	std::printf("KEY TRANSPOSE: KT held + a key (owner's manual p.36)\n");
	{
		Unit u(rom);
		bool ok = true, led = true;
		std::string bad;
		u.hold(Button::KeyTranspose, true);
		for (int key = 0; key < Machine::kKeys; ++key) {
			u.key(key, true);
			u.ms(10);
			u.key(key, false);
			u.ms(10);
			const int want = key < 20 ? key - 7 : key - 19;
			if (u.lines().range != 24 + want) {
				ok = false;
				bad += keyName(key) + "=" + std::to_string(u.lines().range - 24) + " ";
			}
			led &= u.m.ledOn(Led::KeyTranspose) == (want != 0);
		}
		u.hold(Button::KeyTranspose, false);
		u.ms(10);
		check(ok, "F3..B3 = -7..-1, C4 = 0, C#4..C5 = +1..+12, C#5..C6 = +1..+12, on RANGE", "%s", bad.c_str());
		check(led, "KEY TRANSPOSE LED lit while transposed");
		u.down("C4");
		u.ms(20);
		check(u.lines().kcv == 24 && u.lines().range == 36, "it moves RANGE, not KCV (so not the VCF's key follow)",
		      "KCV %d RANGE %d", u.lines().kcv, u.lines().range);
		check(gateRises(u, 0) == 1, "keys pressed with KT held play no note");
	}

	std::printf("HOLD button and pedal\n");
	{
		Unit u(rom);
		u.button(Button::Hold, 20, 20);
		u.down("C4");
		u.ms(20);
		u.up("C4");
		u.ms(30);
		const bool held = u.lines().gate && u.m.ledOn(Led::Hold);
		u.down("E4");
		u.ms(20);
		u.up("E4");
		u.ms(30);
		check(held && u.lines().gate && u.lines().kcv == 28 && gateRises(u, 0) == 1,
		      "HOLD: the gate stays on after release; new keys change the pitch under it");
		u.button(Button::Hold, 20, 20);
		check(!u.lines().gate && !u.m.ledOn(Led::Hold), "HOLD off: gate off");
		u.m.setHoldPedal(true);
		u.ms(10);
		u.down("C4");
		u.ms(20);
		u.up("C4");
		u.ms(30);
		const bool pedal = u.lines().gate && u.m.ledOn(Led::Hold);
		u.m.setHoldPedal(false);
		u.ms(20);
		check(pedal && !u.lines().gate, "pedal (T0 low): holds like the button, lights HOLD, releases with it");
	}
}

// --- Sequencer -------------------------------------------------------------------------------

std::vector<Step> enter(Unit &u, const std::vector<std::pair<std::string, char>> &input)
{
	// input: (key name, kind) with kind 'n' note, 'l' legato note, 'r' rest; the TRANSPOSE
	// switch follows a trailing ':L', ':M' or ':H' on the key name.
	u.button(Button::Load, 20, 20);
	std::vector<Step> want;
	int lastKey = 0;
	for (auto [name, kind] : input) {
		sh101::Transpose t = sh101::Transpose::Mid;
		if (auto c = name.find(':'); c != std::string::npos) {
			t = name[c + 1] == 'L' ? sh101::Transpose::Low : name[c + 1] == 'H' ? sh101::Transpose::High : sh101::Transpose::Mid;
			name.resize(c);
		}
		u.m.setTranspose(t);
		u.ms(5);
		if (kind == 'r') {
			u.button(Button::KeyTranspose, 20, 20);
			want.push_back({true, 0, false});
			continue;
		}
		if (kind == 'l')
			u.hold(Button::Hold, true);
		u.press(keyIndex(name), 20, 20);
		if (kind == 'l')
			u.hold(Button::Hold, false);
		lastKey = keyIndex(name);
		want.push_back({false, lastKey + 12 * int(t), kind == 'l'});
	}
	u.m.setTranspose(sh101::Transpose::Mid);
	u.button(Button::Load, 20, 20);
	return want;
}

std::string text(const std::vector<Step> &v)
{
	std::string s;
	for (const auto &st : v)
		s += (s.empty() ? "" : " ") + (st.rest ? std::string("rest") : noteName(st.note + 5) + (st.legato ? "~" : ""));
	return s;
}

void testSequencer(const std::vector<uint8_t> &rom)
{
	std::printf("LOAD: the step format (owner's manual pp.33-34)\n");
	{
		Unit u(rom);
		const auto want = enter(u, {{"F3", 'n'}, {"A3", 'n'}, {"C4", 'l'}, {"", 'r'}, {"C6", 'n'}, {"F3:H", 'n'}, {"F3:L", 'l'}});
		const auto got = u.m.readSequence();
		check(got == want, "notes (key + 12 x TRANSPOSE), legato, rest read back", "%s", text(got).c_str());
		const auto r = u.m.ram();
		check(r[0x1c] == 0x8c && r[0x1e] == 0xd3 && r[0x1f] == 0x53 && r[0x23] == 0x00,
		      "bytes: note 0x80|n, legato 0xC0|n, rest 0x40|last key, end 0x00", "%02X %02X %02X .. %02X", r[0x1c], r[0x1e],
		      r[0x1f], r[0x23]);
	}
	{
		Unit u(rom, sh101::Trigger::Lfo);
		enter(u, {{"C4", 'n'}, {"E4", 'n'}});
		const auto got = u.m.readSequence();
		check(got.size() == 2 && got[0].legato && got[1].legato, "entered with ENV TRIG on LFO, every note is legato",
		      "%s", text(got).c_str());
	}
	{
		Unit u(rom);
		enter(u, {{"C4", 'n'}, {"E4", 'n'}});
		u.button(Button::Load, 20, 20);
		u.button(Button::Load, 20, 20);
		check(u.m.readSequence().size() == 2, "LOAD in and out with nothing entered keeps the sequence");
		u.button(Button::Load, 20, 20);
		u.button(Button::KeyTranspose, 20, 20);
		u.button(Button::Load, 20, 20);
		const auto got = u.m.readSequence();
		check(got.size() == 1 && got[0].rest, "LOAD from the start: the first key or REST clears the old sequence",
		      "%s", text(got).c_str());
	}
	{
		Unit u(rom);
		u.button(Button::Load, 20, 20);
		for (int i = 0; i < 100; ++i)
			u.press(i % 12, 10, 10);
		const bool stillLoad = u.m.ledOn(Led::Load);
		u.press(keyIndex("C6"), 20, 20);
		const auto got = u.m.readSequence();
		check(stillLoad && !u.m.ledOn(Led::Load) && got.size() == 100 && u.m.ram()[0x7f] == 0x80 + 99 % 12 + 12,
		      "100 steps fill 0x1C-0x7F; the 101st key leaves LOAD and plays", "%zu steps, LOAD %d", got.size(),
		      u.m.ledOn(Led::Load));
	}

	std::printf("PLAY (owner's manual p.35)\n");
	{
		Unit u(rom);
		const std::vector<Step> seq = {{false, 19, false}, {false, 23, false}, {false, 26, true}, {true, 0, false},
		                               {false, 31, false}, {false, 31, true}, {false, 14, false}};
		u.m.writeSequence(seq);
		check(u.m.readSequence() == seq, "writeSequence / readSequence round trip");
		u.setClockHz(20);
		const double t0 = u.now();
		u.button(Button::Play, 20, 0);
		u.ms(2 * 7 * 50 + 30);
		u.button(Button::Play, 20, 20);
		// Expected per step: KCV = note + 5; a rest gates off and keeps the pitch; a legato step
		// has no gate gap at its start.
		const auto starts = noteStarts(u, t0);
		const std::vector<int> wantStarts = {24, 28, 36, 19, 24, 28, 36, 19};
		check(starts == wantStarts, "a gate starts at each step but the legato ones and the rest; it loops",
		      "note starts (KCV) %s", join(starts).c_str());
		const auto changes = kcvChanges(u, t0);
		const std::vector<int> wantChanges = {24, 28, 31, 36, 19, 24, 28, 31, 36, 19};
		check(std::search(changes.begin(), changes.end(), wantChanges.begin(), wantChanges.end()) != changes.end(),
		      "KCV per step: note + 5; the rest keeps the last pitch; the tie doesn't move", "KCV %s",
		      join(changes).c_str());
		// The gap: gate off for one loop at the old pitch, then the new pitch and gate together.
		double gapMax = 0, gapMin = 1;
		const auto e = gateEdges(u.events, t0);
		for (size_t i = 1; i + 1 < e.size(); ++i)
			if (e[i].value == 0 && e[i + 1].value == 1 && e[i + 1].t - e[i].t < 0.01) {
				gapMax = std::max(gapMax, e[i + 1].t - e[i].t);
				gapMin = std::min(gapMin, e[i + 1].t - e[i].t);
			}
		check(gapMin > 0.0015 && gapMax < 0.0035, "the retrigger gap is one loop", "%.2f-%.2f ms", gapMin * 1000,
		      gapMax * 1000);
		check(!u.lines().gate && !u.m.ledOn(Led::Play), "PLAY again stops, gate off");
	}
	{
		Unit u(rom);
		u.m.writeSequence({{false, 19, false}, {false, 23, false}});
		u.setClockHz(20);
		u.button(Button::Play, 20, 20);
		u.ms(60);
		u.m.setTranspose(sh101::Transpose::High);
		u.ms(120);
		int kcvHigh = 0;
		for (const auto &e : u.events)
			if (e.kind == Event::Dac && e.channel == 0)
				kcvHigh = std::max(kcvHigh, e.value);
		check(kcvHigh == 28, "the TRANSPOSE switch does nothing during PLAY", "highest KCV %d", kcvHigh);
		u.m.setTranspose(sh101::Transpose::Mid);
		u.hold(Button::KeyTranspose, true);
		u.press(keyIndex("G4"), 20, 20);
		u.hold(Button::KeyTranspose, false);
		u.ms(60);
		check(u.lines().range == 31 && u.m.ledOn(Led::Play), "KT + a key transposes the playing sequence (RANGE)",
		      "RANGE %d", u.lines().range);
		u.press(keyIndex("C6"), 20, 20);
		check(u.lines().range == 31 && u.lines().kcv != 53, "a key alone does nothing during PLAY");
		u.m.setHoldPedal(true);
		u.ms(150);
		const bool held = u.lines().gate;
		u.m.setHoldPedal(false);
		u.ms(20);
		check(held && !u.m.ledOn(Led::Play) && !u.lines().gate, "pedal down holds the gate; releasing it stops PLAY");
	}
	{
		Unit u(rom);
		u.m.writeSequence({{false, 19, false}, {false, 23, false}, {false, 26, false}, {false, 31, false}});
		u.setClockLevel(false);
		u.button(Button::Play, 20, 20);
		// Two clock pulses (steps 1 and 2), then LOAD: new steps follow the step playing.
		for (int i = 0; i < 2; ++i) {
			u.setClockLevel(true);
			u.ms(20);
			u.setClockLevel(false);
			u.ms(20);
		}
		u.button(Button::Load, 20, 20);
		const bool load = u.m.ledOn(Led::Load);
		u.press(keyIndex("D5"), 20, 20);
		u.press(keyIndex("E5"), 20, 20);
		u.button(Button::Load, 20, 20);
		const auto got = u.m.readSequence();
		const std::vector<Step> want = {{false, 19, false}, {false, 23, false}, {false, 33, false}, {false, 35, false}};
		check(load && got == want, "LOAD during PLAY continues after the step playing (editing)", "%s",
		      text(got).c_str());
	}
	{
		Unit u(rom);
		u.setClockHz(20);
		u.button(Button::Play, 20, 20);
		const bool on = u.m.ledOn(Led::Play);
		u.ms(80);
		check(on && !u.m.ledOn(Led::Play) && gateRises(u, 0) == 0, "PLAY with no sequence lights, then gives up at the first clock");
	}
	{
		Unit u(rom);
		u.m.writeSequence({{false, 19, false}});
		u.setClockHz(10);
		const double t0 = u.now();
		u.button(Button::Play, 20, 20);
		const auto rst = p2BitEdges(u.events, 0x10, true, t0);
		check(rst.size() >= 2 && rst[0].value == 1 && rst[1].t - rst[0].t < 0.004, "PLAY start pulses CLK RST for one loop");
	}
}

// --- Arpeggio --------------------------------------------------------------------------------

// Presses the first key, then the others a few loops later, as fingers do (and as the MIDI
// driver does): the pattern starts from the first key.
void chord(Unit &u, const std::vector<const char *> &keys)
{
	u.down(keys[0]);
	u.loops(3);
	for (size_t i = 1; i < keys.size(); ++i)
		u.down(keys[i]);
}

void testArpeggio(const std::vector<uint8_t> &rom)
{
	std::printf("Arpeggio (owner's manual p.32)\n");
	struct Case {
		Button mode;
		const char *name;
		std::vector<const char *> keys;
		std::vector<int> want;
	};
	const Case cases[] = {
		{Button::ArpUp, "UP", {"C4", "E4", "G4"}, {24, 28, 31, 24, 28, 31, 24}},
		{Button::ArpDown, "DOWN", {"G4", "C4", "E4"}, {31, 28, 24, 31, 28, 24, 31}},
		{Button::ArpUpDown, "U&D", {"C4", "E4", "G4", "C5"}, {24, 28, 31, 36, 31, 28, 24, 28, 31, 36}},
	};
	for (const auto &c : cases) {
		Unit u(rom);
		u.setClockHz(20);
		u.button(c.mode, 20, 20);
		const double t0 = u.now();
		chord(u, c.keys);
		u.ms(50.0 * double(c.want.size()) - 30);
		const auto got = kcvChanges(u, t0);
		const bool gateOn = u.lines().gate && gateRises(u, t0) == 1;
		const bool porta = u.lines().portaOff;
		for (auto k : c.keys)
			u.up(k);
		u.ms(20);
		check(std::equal(c.want.begin(), c.want.end(), got.begin(), got.end()) || (got.size() >= c.want.size() && std::equal(c.want.begin(), c.want.end(), got.begin())),
		      std::string(c.name) + ": order (first key first, ends not repeated in U&D)", "KCV %s", join(got).c_str());
		check(gateOn, std::string(c.name) + ": one gate for the whole run (GATE+TRIG retriggers come from the KCV change)");
		check(porta && !u.lines().portaOff, std::string(c.name) + ": PORTA OFF (P23) high while it runs, low after");
	}
	{
		Unit u(rom);
		u.setClockHz(50);
		u.button(Button::ArpUp, 20, 20);
		const double t0 = u.now();
		u.key(0, true);
		u.loops(3);
		for (int key = 1; key < Machine::kKeys; ++key)
			u.key(key, true);
		u.ms(20 * 33);
		auto got = kcvChanges(u, t0);
		bool ok = got.size() >= 33;
		for (size_t i = 0; ok && i < 33; ++i)
			ok = got[i] == int(i % 32) + 17;
		check(ok, "UP with all 32 keys: F3 to C6, one key per clock", "%zu steps", got.size());
	}
	{
		Unit u(rom);
		u.setClockHz(20);
		u.button(Button::ArpUp, 20, 20);
		const double t0 = u.now();
		u.down("A4");
		u.ms(200);
		check(kcvChanges(u, t0) == std::vector<int>{33} && u.lines().gate && !u.lines().portaOff,
		      "a single key in an arpeggio mode plays normally");
		const auto rst = p2BitEdges(u.events, 0x10, true, t0);
		check(!rst.empty() && rst[0].value == 1, "key-down in an arpeggio mode pulses CLK RST");
	}
	{
		Unit u(rom);
		u.setClockHz(20);
		u.button(Button::ArpUp, 20, 20);
		u.button(Button::Hold, 20, 20);
		chord(u, {"C4", "E4"});
		u.ms(100);
		u.up("C4");
		u.up("E4");
		const double t0 = u.now();
		u.ms(200);
		check(kcvChanges(u, t0).size() >= 3 && u.lines().gate, "HOLD latches the arpeggio after the keys are released");
		const double t1 = u.now();
		chord(u, {"F4", "A4"});
		u.ms(200);
		u.up("F4");
		u.up("A4");
		u.ms(100);
		auto got = kcvChanges(u, t1);
		auto firstNew = std::find_if(got.begin(), got.end(), [](int k) { return k == 29 || k == 33; });
		const bool onlyNew = firstNew != got.end() && firstNew - got.begin() <= 1 &&
		                     std::all_of(firstNew, got.end(), [](int k) { return k == 29 || k == 33; });
		check(onlyNew && got.size() >= 3, "a new chord replaces the latched one", "KCV %s", join(got).c_str());
		u.button(Button::Hold, 20, 20);
		u.ms(100);
		check(!u.lines().gate, "HOLD off stops it");
	}
	{
		Unit u(rom);
		u.setClockHz(10);
		u.button(Button::ArpUp, 20, 20);
		chord(u, {"C4", "E4"});
		u.ms(30);
		const int before = u.lines().kcv;
		u.m.setTranspose(sh101::Transpose::High);
		u.loops(3);
		const int after = u.lines().kcv;
		u.m.setRange(sh101::Range::Ft4);
		u.loops(3);
		check(after == before + 12 && u.lines().range == 36, "TRANSPOSE and RANGE act at once mid-arpeggio",
		      "KCV %d -> %d, RANGE %d", before, after, u.lines().range);
	}
	{
		Unit u(rom, sh101::Trigger::Lfo);
		const double t0 = u.now();
		u.down("C4");
		u.ms(20);
		const auto rst = p2BitEdges(u.events, 0x10, true, t0);
		Unit g(rom, sh101::Trigger::GateTrig);
		const double t1 = g.now();
		g.down("C4");
		g.ms(20);
		check(!rst.empty() && rst[0].value == 1 && p2BitEdges(g.events, 0x10, true, t1).size() <= 1,
		      "CLK RST on key-down with ENV TRIG on LFO, not with GATE+TRIG");
	}
}

// --- Clock, random, timing, power ------------------------------------------------------------

void testClockAndTiming(const std::vector<uint8_t> &rom, bool longRuns)
{
	std::printf("RANDOM (DAC channel 2): one step per clock edge\n");
	{
		Unit u(rom);
		u.setClockLevel(false);
		u.ms(10);
		std::vector<int> r;
		for (int i = 0; i < 1100; ++i) {
			u.setClockLevel(true);
			u.ms(5);
			r.push_back(u.lines().random);
			u.setClockLevel(false);
			u.ms(5);
		}
		const std::vector<int> first = {54, 25, 28, 40, 12, 57, 49, 4, 50, 36};
		check(std::equal(first.begin(), first.end(), r.begin()), "after power-on: 54 25 28 40 12 ...", "%s",
		      join(std::vector<int>(r.begin(), r.begin() + 10)).c_str());
		auto period = [&](size_t p) {
			for (size_t i = 0; i + p < r.size(); ++i)
				if (r[i] != r[i + p])
					return false;
			return true;
		};
		std::array<int, 64> hist{};
		for (size_t i = 0; i < 500; ++i)
			++hist[size_t(r[i])];
		const auto [lo, hi] = std::minmax_element(hist.begin(), hist.end());
		check(period(500) && !period(250) && !period(100) && !period(125),
		      "period 500 edges: page 1 of the ROM (250 non-zero bytes) twice, as the increments sum to 32");
		check(*lo >= 1, "every value 0-63 occurs, unevenly", "%d-%d times per period", *lo, *hi);
		(void)longRuns;
	}

	std::printf("Main-loop period by mode and last key (service notes p.4: 1.5-3.5 ms)\n");
	auto stats = [](Unit &u, double ms) {
		const size_t from = u.events.size();
		u.ms(ms);
		double last = -1, mn = 1, mx = 0;
		for (size_t i = from; i < u.events.size(); ++i)
			if (u.events[i].kind == Event::P2) {
				if (last >= 0) {
					mn = std::min(mn, u.events[i].t - last);
					mx = std::max(mx, u.events[i].t - last);
				}
				last = u.events[i].t;
			}
		return std::make_pair(mn * 1000, mx * 1000);
	};
	struct Mode {
		const char *name;
		std::function<void(Unit &)> setup;
		double lo, hi;
	};
	const Mode modes[] = {
		{"GATE+TRIG, idle", [](Unit &) {}, 1.7, 1.8},
		{"GATE, idle", [](Unit &u) { u.m.setTrigger(sh101::Trigger::Gate); }, 1.6, 1.8},
		{"GATE+TRIG, one key", [](Unit &u) { u.down("C4"); }, 2.4, 2.5},
		{"GATE, all keys", [](Unit &u) { u.m.setTrigger(sh101::Trigger::Gate); for (int k = 0; k < 32; ++k) u.key(k, true); }, 1.5, 1.7},
		{"LOAD, key held", [](Unit &u) { u.button(Button::Load, 20, 20); u.down("C4"); }, 2.5, 2.7},
		{"GATE+TRIG, idle after C6", [](Unit &u) { u.press(31, 20, 20); }, 3.1, 3.2},
		{"GATE, F3 held", [](Unit &u) { u.m.setTrigger(sh101::Trigger::Gate); u.down("F3"); }, 1.1, 1.2},
		{"PLAY", [](Unit &u) { u.m.writeSequence({{false, 19, false}, {false, 23, false}}); u.setClockHz(50); u.button(Button::Play, 20, 20); }, 1.9, 2.1},
		{"PLAY after C6", [](Unit &u) { u.press(31, 20, 20); u.m.writeSequence({{false, 19, false}, {false, 23, false}}); u.setClockHz(50); u.button(Button::Play, 20, 20); }, 3.3, 3.5},
		{"ARP U&D, all keys", [](Unit &u) { u.setClockHz(50); u.button(Button::ArpUpDown, 20, 20); for (int k = 0; k < 32; ++k) u.key(k, true); }, 3.1, 3.8},
	};
	double worst = 0;
	for (const auto &md : modes) {
		Unit u(rom);
		md.setup(u);
		u.ms(30);
		const auto [mn, mx] = stats(u, 500);
		worst = std::max(worst, mx);
		check(mn >= md.lo && mx <= md.hi, md.name, "%.3f-%.3f ms", mn, mx);
	}

	std::printf("Clock limits: T1 is polled once a loop\n");
	auto steps = [&](double hz, double duty) {
		Unit u(rom);
		u.m.writeSequence({{false, 19, false}, {false, 23, false}, {false, 26, false}, {false, 31, false}});
		u.setClockHz(hz, duty);
		const double t0 = u.now();
		u.button(Button::Play, 20, 0);
		u.ms(1000);
		return int(kcvChanges(u, t0).size());
	};
	const int n150 = steps(150, 0.5), n50 = steps(50, 0.05);
	check(n150 >= 148, "150 Hz, 50 % duty (3.3 ms phases): every step", "%d steps in 1 s", n150);
	check(n50 < 40, "50 Hz with 1 ms pulses: steps are lost", "%d of 50", n50);
	(void)worst;
}

void testPower(const std::vector<uint8_t> &rom)
{
	std::printf("Power and battery-backed RAM\n");
	{
		Sim s(rom);
		std::array<uint8_t, Machine::kRamBytes> junk;
		for (int i = 0; i < Machine::kRamBytes; ++i)
			junk[size_t(i)] = uint8_t(0x5a ^ (i * 37));
		s.m.setRam(junk.data(), junk.size());
		s.powerOn();
		s.ms(10);
		const auto r = s.m.ram();
		bool kept = true;
		for (int a = 0x1c; a < 0x80; ++a)
			kept &= r[size_t(a)] == junk[size_t(a)];
		check(r[0x1b] == 0x1b && r[0x16] == 0 && r[0x14] < 3 && kept,
		      "INITIAL SET clears 0x02-0x1B (key transpose, random, pointer...), keeps 0x1C-0x7F (the sequence)");
	}
	{
		Unit a(rom);
		const std::vector<Step> seq = {{false, 19, false}, {true, 0, false}, {false, 26, true}};
		a.m.writeSequence(seq);
		a.ms(10);
		const auto state = a.m.saveState();
		Sim b(rom);
		b.m.loadState(state);
		b.powerOn();
		b.ms(20);
		check(b.m.readSequence() == seq && state.size() == 5 + Machine::kRamBytes,
		      "saved state (header + 128 bytes) brings the sequence back through a cold boot");
	}
}

} // namespace

int runSelfTest(const std::vector<uint8_t> &rom, bool longRuns)
{
	// --long is accepted for compatibility; every test is quick.
	testBoot(rom);
	testKeyboard(rom);
	testSequencer(rom);
	testArpeggio(rom);
	testClockAndTiming(rom, longRuns);
	testPower(rom);
	std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
	return g_failures ? 1 : 0;
}
