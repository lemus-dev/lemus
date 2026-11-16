// Shared harness for sh101seq and the firmware tests: a Machine with a clock generator, a
// hold pedal, timed panel presses and a log of every DAC and P2 write against emulated time.
#pragma once

#include "sh101/core/machine.h"
#include "sh101/core/midi.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace sh101test {

using sh101::Machine;

inline std::vector<uint8_t> loadRom(const char *path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

// Key names: F3 .. C6 = keys 0 .. 31 (MIDI 53 .. 84).
inline std::string keyName(int k)
{
	static const char *n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	const int midi = 53 + k;
	return std::string(n[midi % 12]) + std::to_string(midi / 12 - 1);
}
inline int keyIndex(const std::string &s)
{
	if (s.size() > 1 && (s[0] == 'k' || s[0] == 'K') && isdigit((unsigned char)s[1]))
		return std::atoi(s.c_str() + 1);
	for (int k = 0; k < Machine::kKeys; ++k)
		if (s == keyName(k))
			return k;
	return -1;
}
// KCV code -> the note it plays at 8' (RANGE 24): code 24 = 2 V = C4 (MIDI 60).
inline std::string noteName(int code)
{
	static const char *n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	const int midi = code + 36;
	return std::string(n[midi % 12]) + std::to_string(midi / 12 - 1);
}

struct Event {
	enum Kind : uint8_t { Dac, P2 } kind;
	int channel; // DAC channel, or -1
	int value;
	double t;    // seconds of emulated time (from the cycle counter)
};

struct Sim : Machine::Observer {
	Machine m;
	double rate = 200000; // tick rate: 5 us resolution
	double t = 0;
	// Clock generator: square wave at clockHz with `duty` high; or a fixed level.
	double clockHz = 0, duty = 0.5, phase = 0;
	bool clockLevel = false;
	std::vector<Event> events;
	bool record = true;
	std::function<void(Sim &)> perTick; // e.g. a KeyboardDriver
	uint64_t cycle0 = 0;

	explicit Sim(const std::vector<uint8_t> &rom)
	{
		m.setRom(rom.data(), rom.size());
		m.setObserver(this);
	}
	double now() const { return double(m.cycles() - cycle0) / Machine::kCycleHz; }
	void dacWrite(int ch, int code, uint64_t c) override
	{
		if (record)
			events.push_back({Event::Dac, ch, code, double(c - cycle0) / Machine::kCycleHz});
	}
	void p2Write(uint8_t v, uint64_t c) override
	{
		if (record)
			events.push_back({Event::P2, -1, v, double(c - cycle0) / Machine::kCycleHz});
	}

	void powerOn()
	{
		m.powerOn();
	}
	void setClockHz(double hz, double d = 0.5)
	{
		clockHz = hz;
		duty = d;
		if (hz <= 0)
			m.setClock(clockLevel);
	}
	void setClockLevel(bool level)
	{
		clockHz = 0;
		clockLevel = level;
		m.setClock(level);
	}
	void advance(double seconds)
	{
		const long n = long(seconds * rate + 0.5);
		const double dt = 1 / rate;
		for (long i = 0; i < n; ++i) {
			if (clockHz > 0) {
				phase += clockHz * dt;
				phase -= std::floor(phase);
				m.setClock(phase < duty);
			}
			if (perTick)
				perTick(*this);
			m.tick(rate);
			t += dt;
		}
	}
	void ms(double v) { advance(v / 1000); }
	void key(int k, bool down) { m.setKey(k, down); }
	void press(int k, double downMs = 50, double upMs = 50)
	{
		m.setKey(k, true);
		ms(downMs);
		m.setKey(k, false);
		ms(upMs);
	}
	void button(Machine::Button b, double downMs = 50, double upMs = 50)
	{
		m.setButton(b, true);
		ms(downMs);
		m.setButton(b, false);
		ms(upMs);
	}
	// Events since `from` (seconds).
	std::vector<Event> since(double from) const
	{
		std::vector<Event> r;
		for (const auto &e : events)
			if (e.t >= from)
				r.push_back(e);
		return r;
	}
};

// Edge lists of one output, reconstructed from the event log: the changes in (from, to], with
// the state just before `from` as the starting point (so a state that holds is not an edge).
struct Edge {
	double t;
	int value;
};
template <typename Pick>
std::vector<Edge> edges(const std::vector<Event> &ev, double from, double to, Pick pick)
{
	std::vector<Edge> r;
	int last = -1;
	for (const auto &e : ev) {
		if (e.t > to)
			break;
		int v;
		if (!pick(e, v))
			continue;
		if (e.t >= from && last >= 0 && v != last)
			r.push_back({e.t, v});
		if (e.t >= from && last < 0)
			r.push_back({e.t, v}); // nothing before `from`: the first value counts as a change
		last = v;
	}
	return r;
}
inline std::vector<Edge> gateEdges(const std::vector<Event> &ev, double from = 0, double to = 1e30)
{
	return edges(ev, from, to, [](const Event &e, int &v) {
		v = (e.value & 0x80) ? 0 : 1;
		return e.kind == Event::P2;
	});
}
inline std::vector<Edge> p2BitEdges(const std::vector<Event> &ev, int mask, bool activeLow, double from = 0, double to = 1e30)
{
	return edges(ev, from, to, [mask, activeLow](const Event &e, int &v) {
		v = ((e.value & mask) != 0) != activeLow ? 1 : 0;
		return e.kind == Event::P2;
	});
}
inline std::vector<Edge> dacEdges(const std::vector<Event> &ev, int ch, double from = 0, double to = 1e30)
{
	return edges(ev, from, to, [ch](const Event &e, int &v) {
		v = e.value;
		return e.kind == Event::Dac && e.channel == ch;
	});
}

} // namespace sh101test
