#pragma once

#include "ucom43.h"

#include <array>
#include <cstdint>
#include <vector>

namespace tr808 {

// The TR-808's CPU board: the original uPD650C-085 firmware on the uCOM-43 core, the four
// uPD444 RAMs and their decoder, the switch matrix (step keys, CLEAR, the rotary and slide
// switches), the step LEDs and the four LED drivers, the status buffer and gate (START/STOP,
// FILL IN, RESET & START, TEMPO CLOCK), and the ports that carry the instrument data and the
// step pulse to the trigger circuit. See docs/firmware.md for the map.
class Machine : private ucom43::Bus {
public:
	// MODE, fully anticlockwise to fully clockwise.
	enum class Mode : uint8_t { PatternClear, FirstPart, SecondPart, ManualPlay, Play, Compose, Count };
	// BASIC VARIATION and AUTO FILL IN positions, left to right.
	enum class Variation : uint8_t { A, AB, B };
	enum class AutoFill : uint8_t { Manual, M16, M12, M8, M4, M2 };
	// The LEDs off the matrix: BASIC VARIATION A and B, 1st PART and 2nd PART.
	enum class Led : uint8_t { VariationA, VariationB, FirstPart, SecondPart, Count };

	static constexpr double kCpuHz = 500000.0;     // IFT-1 LC oscillator, 2 us (TP1)
	static constexpr double kCycleHz = kCpuHz / 4; // machine cycles per second
	static constexpr double kIrqSeconds = 0.0019;  // INT clock (TM-1, TP2), as trimmed
	static constexpr int kRamNibbles = 4 * 1024;

	Machine();
	bool setRom(const uint8_t *data, size_t size);
	bool romLoaded() const { return m_rom.size() == 2048; }
	void powerOn(); // CPU reset; the RAM is battery backed and kept
	// The INT clock (IC1, TM-1, TP2): its period and the low pulse that interrupts the CPU each
	// cycle (core/trimmers.h). intLevel: TP2 now (IC1 pin 6), low for the pulse after each interrupt.
	void setIntClock(double seconds, double lowSeconds)
	{
		m_irqSeconds = seconds;
		m_irqLow = lowSeconds;
	}
	double intClock() const { return m_irqSeconds; }
	double intLow() const { return m_irqLow; }
	bool intLevel() const { return m_irqAcc >= m_irqLow; }

	// Panel
	void setStepKey(int step, bool down) { if (step >= 0 && step < 16) m_step[size_t(step)] = down; } // 0-15 = 1-16
	bool stepKey(int step) const { return step >= 0 && step < 16 && m_step[size_t(step)]; }
	void setClearKey(bool down) { m_clear = down; }
	bool clearKey() const { return m_clear; }
	void setMode(Mode m) { m_mode = m; }
	Mode mode() const { return m_mode; }
	void setInstrument(int position) { m_instrument = position < 0 ? 0 : position > 11 ? 11 : position; } // 0-11 = 1 (AC) - 12 (CH)
	int instrument() const { return m_instrument; }
	void setAutoFill(AutoFill a) { m_autoFill = a; }
	void setVariation(Variation v) { m_variation = v; }
	void setPrescale(int position) { m_prescale = position < 0 ? 0 : position > 3 ? 3 : position; } // 0-3 = 1-4
	void setIfVariationB(bool b) { m_ifB = b; } // I/F VARIATION: false A, true B

	// Status inputs, as the status buffer reads them: the run level (START/STOP's flip-flop or DIN
	// pin 1), FILL IN (TAP, or DIN pin 5), RESET & START (DIN pin 4) and the 24 ppqn tempo clock
	// (the internal oscillator's T flip-flop, or DIN pin 3).
	void setRun(bool run) { m_run = run; }
	bool running() const { return m_run; }
	void setFillIn(bool level) { m_fillIn = level; }
	void setResetStart(bool level) { m_resetStart = level; }
	void setClock(bool level) { m_clock = level; }

	// Runs the board for one sample period of `rate`. Returns true if the trigger lines changed in
	// it (stepPulses() or instrumentData()).
	bool tick(double rate);

	// To the trigger circuit: PI2's step pulses (a count of rising edges; the pulse is 10 us, under
	// a sample) and the instrument data on PD/PE/PF, as VoiceBoard's kBit* bits.
	unsigned stepPulses() const { return m_pulses; }
	uint16_t instrumentData() const { return m_data; }

	// LED brightness 0-1: the 16 step LEDs (averaged over the multiplex) and the four others.
	float stepLed(int step) const { return step >= 0 && step < 16 ? m_stepLevel[size_t(step)] : 0.0f; }
	float led(Led l) const { return m_ledLevel[size_t(l)]; }
	uint16_t stepLedsNow() const { return m_ledNow; }

	// A rhythm pattern written straight into RAM in the firmware's own format (docs/firmware.md):
	// pattern 0-15 (BASIC RHYTHM 1-12, INTRO/FILL IN 1-4), variation 0/1 (A/B), the steps' voices
	// (kBit*, kBitAC for the accent; 1-32 of them, the first 16 the 1st part) and the pre-scale
	// (0-3 = 1-4). Lengths and pre-scale are shared by A and B, as on the panel.
	static int prescaleClocks(int prescale) { static const int c[] = {8, 4, 6, 3}; return c[prescale & 3]; }
	void writePattern(int pattern, int variation, const std::vector<uint16_t> &steps, int prescale = 2);
	// The same with the parts given apart: 1st 1-16 steps, 2nd 0-16.
	void writePattern(int pattern, int variation, const std::vector<uint16_t> &first, const std::vector<uint16_t> &second,
	                  int prescale = 2);
	// The factory rhythms (factory.cpp): every pattern as a new 808's memory held it, the tracks empty.
	void writeFactoryRhythms();

	// The battery-backed RAM: 4 chips x 1024 nibbles, chip = PE2-3, address = PE0-1, PD, PF (the
	// firmware's own address bits, docs/firmware.md).
	const std::array<uint8_t, kRamNibbles> &ram() const { return m_xram; }
	void setRam(const uint8_t *data, size_t n);
	std::vector<uint8_t> saveState() const;
	bool loadState(const std::vector<uint8_t> &data);

	// Tools: the CPU, the ports, the RAM traffic.
	const ucom43::Cpu &cpu() const { return m_cpu; }
	uint8_t port(int p) const { return m_cpu.portOut(p); }
	long ramWrites() const { return m_ramWrites; }

private:
	uint8_t read(int port) override;
	void write(int port, uint8_t data) override;
	uint8_t columnA(int column) const;
	uint8_t columnB(int column) const;
	bool writable() const;
	void updateLeds(double dt);
	void updateData();

	ucom43::Cpu m_cpu;
	std::vector<uint8_t> m_rom;
	std::array<uint8_t, kRamNibbles> m_xram{};

	std::array<bool, 16> m_step{};
	bool m_clear = false;
	Mode m_mode = Mode::Play;
	int m_instrument = 0, m_prescale = 2;
	AutoFill m_autoFill = AutoFill::Manual;
	Variation m_variation = Variation::A;
	bool m_ifB = false;
	bool m_run = false, m_fillIn = false, m_resetStart = false, m_clock = false;

	uint8_t m_portC = 0, m_portD = 0, m_portE = 0, m_portF = 0, m_portG = 0, m_portH = 0xf, m_portI = 1;
	double m_cycleAcc = 0, m_irqAcc = 0;
	double m_irqSeconds = kIrqSeconds, m_irqLow = 0.00016;
	unsigned m_pulses = 0;
	uint16_t m_data = 0;
	bool m_changed = false;
	long m_ramWrites = 0;

	uint16_t m_ledNow = 0;
	std::array<float, 16> m_stepOn{}, m_stepLevel{};
	float m_holdE0 = 0, m_holdE1 = 0; // the LED drivers' base capacitors (C4, C5), volts
	std::array<float, size_t(Led::Count)> m_ledLevel{};
};

// The internal tempo clock (main board p.8, Fig. 8): the anti-log pair Q13/Q14 charges C10 for
// the IC1 Schmitt oscillator, and IC2a halves it into the 24 ppqn clock at TP3. START holds the
// oscillator for 9 ms, so the first clock edge comes 9 ms after START and the clock runs in phase
// from there; it runs free otherwise.
class TempoClock {
public:
	static constexpr double kStartSeconds = 0.009;

	// TEMPO and FINE rotations (0-1) to beats per minute: Q14's collector current goes as
	// exp(Vb / VT), and its base sits on R41 (560) fed from the TEMPO wipers through R38 || R39
	// (50k), FINE's through R32 (560k), TM2 through R43 and -15 V through R40. TM2 sets 8.33 ms at
	// full TEMPO and FINE (300 BPM); full anticlockwise TEMPO is then 70 ms (the notes: 65 +-5).
	static double bpm(double tempo, double fine);
	// The same with TM2 at rotation tm2 (its wiper's travel from the -15 V end; 0.5 is where it sets
	// the 8.33 ms): its wiper's voltage and its resistance through R43 into the base node.
	static double bpm(double tempo, double fine, double tm2);

	// Advances by dt seconds at `bpm`, run is the START/STOP flip-flop. Returns the clock level.
	bool advance(double dt, double bpm, bool run);
	void reset() { m_phase = 0; m_hold = 0; m_run = false; }

private:
	double m_phase = 0; // in clock periods; high for the first half
	double m_hold = 0;  // seconds left of START's hold
	bool m_run = false;
};

} // namespace tr808
