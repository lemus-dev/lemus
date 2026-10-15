#pragma once

#include "midi.h"
#include "ucom43.h"

#include <array>
#include <cstdint>
#include <vector>

namespace tb303 {

// The TB-303's CPU board: the original uPD650C-133 firmware on the uCOM-43 core, the three
// uPD444 pattern RAMs, the switch board's key matrix and LEDs, the status buffer (tempo
// clock, run level, TAP) and the latches that turn port writes into the voice board's CV,
// accent, slide and gate. See docs/firmware.md for the map.
class Machine : private ucom43::Bus {
public:
	// Matrix keys. RUN/STOP is not in the matrix: it toggles a flip-flop in the clock
	// circuit (see setRun). TAP is on the status port.
	enum class Key : uint8_t {
		C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B, C2,
		Down, Up, Accent, Slide,
		PatternClear, Function, PitchMode, TimeMode, Back, Tap,
		Count
	};
	// MODE switch positions, left to right on the panel.
	enum class Mode : uint8_t { TrackWrite, TrackPlay, PatternPlay, PatternWrite };

	// LEDs on the panel. Matrix LEDs are found by row/column; see ledFor().
	enum class Led : uint8_t {
		C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B, C2,
		Down, Up, Accent, Slide,
		PitchMode, TimeMode, Function, Run,
		Count
	};

	static constexpr double kCpuHz = 454545.0;      // LC oscillator, 2.2 us
	static constexpr double kCycleHz = kCpuHz / 4;   // machine cycles per second
	static constexpr double kIrqSeconds = 0.0018;    // INT oscillator (TM2)
	static constexpr int kRamNibbles = 3 * 1024;

	Machine();
	bool setRom(const uint8_t *data, size_t size);
	bool romLoaded() const { return m_rom.size() == 2048; }
	void powerOn(); // CPU reset; pattern RAM is battery backed and kept

	// Panel
	void setKey(Key k, bool down);
	// Test hook: force a raw matrix bit (column 0-3, return bit 0-7).
	void setRawKey(int column, int bit, bool down)
	{
		const uint8_t m = uint8_t(1 << (bit & 7));
		m_raw[column & 3] = uint8_t(down ? m_raw[column & 3] | m : m_raw[column & 3] & ~m);
	}
	bool key(Key k) const { return m_keys[size_t(k)]; }
	void setMode(Mode m) { m_mode = m; }
	Mode mode() const { return m_mode; }
	void setTrack(int position) { m_track = position < 0 ? 0 : position > 6 ? 6 : position; } // 0-6 = track 1-7
	int track() const { return m_track; }

	// Status inputs: run level (the RUN/STOP flip-flop, or DIN pin 1) and the 24 ppqn tempo
	// clock (the internal oscillator's divider, or DIN pin 3).
	void setRun(bool run) { m_run = run; }
	bool running() const { return m_run; }
	void setClock(bool level) { m_clock = level; }

	// Runs the board for one sample period of `rate`. Returns true if the voice lines changed
	// during it (read them with lines()).
	bool tick(double rate);

	// The INT oscillator's period (TM2; kIrqSeconds trimmed), and its output at TP2: negative
	// pulses, low for the first 0.15 ms of each period (as the service notes draw them).
	void setIrqSeconds(double s) { m_irqSeconds = s > 1e-4 ? s : 1e-4; }
	double irqSeconds() const { return m_irqSeconds; }
	bool tp2() const { return m_irqAcc >= 0.15e-3; }

	// Voice board inputs as the latches hold them: note = DAC code + 13 as a MIDI number
	// (code 23 = 2.000 V = C2), gate = PI2, accent = IC13 latched from PE0, slide = PI1 level.
	const CpuLines &lines() const { return m_lines; }
	int dacCode() const { return m_dac; }
	unsigned strobes() const { return m_strobes; }

	// LED brightness 0-1 (averaged over the multiplex) and the raw 4x4 matrix.
	float led(Led l) const { return m_ledLevel[size_t(l)]; }
	uint16_t ledMatrix() const { return m_ledNow; }
	float pcHold(int bit) const { return m_pcHold[size_t(bit & 3)]; }

	// One pattern step as the panel enters it: key 0-12 (C..C'), octave DOWN/UP, ACCENT,
	// SLIDE, and the time value.
	struct Step {
		enum Time : uint8_t { Note, Tie, Rest };
		uint8_t key = 0;
		bool down = false, up = false, accent = false, slide = false;
		Time time = Note;
	};
	// Writes a pattern straight into pattern memory in the firmware's own format (found by
	// programming patterns through the panel and reading the RAM back; docs/firmware.md):
	// group 0-3 (I-IV), section 0/1 (A/B), pattern 0-7. Per step one pitch nibble (0-12,
	// F = rest, E = end marker after the last step, D in step 15 = triplet mode) and one flag
	// nibble 0x80 later (1 DOWN, 2 UP, 4 ACCENT, 8 SLIDE); a tie repeats the pitch with SLIDE
	// set on the step before.
	static int patternAddress(int group, int section, int pattern) { return 0x400 + group * 0x200 + section * 0x100 + pattern * 0x10; }
	void writePattern(int group, int section, int pattern, const std::vector<Step> &steps, bool triplet = false);
	// Every pattern as PATTERN CLEAR leaves it: the 16 pitch nibbles F (16 steps, all rest).
	// Like the firmware's clear, it leaves the flag nibbles alone.
	void clearPatterns();

	// Battery-backed pattern RAM (3072 nibbles) and the full machine state.
	const std::array<uint8_t, kRamNibbles> &patternRam() const { return m_xram; }
	void setPatternRam(const uint8_t *data, size_t n);
	std::vector<uint8_t> saveState() const;
	bool loadState(const std::vector<uint8_t> &data);

private:
	uint8_t read(int port) override;
	void write(int port, uint8_t data) override;
	void refreshRam();
	uint8_t matrixColumn(int column) const;
	void updateLeds(double dt);

	ucom43::Cpu m_cpu;
	std::vector<uint8_t> m_rom;
	std::array<uint8_t, kRamNibbles> m_xram{};
	std::array<bool, size_t(Key::Count)> m_keys{};
	uint8_t m_raw[4] = {};
	Mode m_mode = Mode::PatternPlay;
	int m_track = 0;
	bool m_run = false, m_clock = false;

	// RAM glue
	uint8_t m_addr[3] = {};
	uint16_t m_ramAddress = 0;
	uint8_t m_ramData = 0;
	bool m_ramCe = false, m_ramWe = false;

	// Port state
	uint8_t m_portG = 0, m_portH = 0xf, m_portI = 0, m_portE = 0, m_portC = 0;

	// Timing
	double m_cycleAcc = 0;
	double m_irqAcc = 0;
	double m_irqSeconds = kIrqSeconds;

	// Outputs
	CpuLines m_lines;
	int m_dac = 24;
	unsigned m_strobes = 0;
	bool m_linesChanged = false;
	uint16_t m_ledNow = 0;

	std::array<float, 16> m_ledOn{};
	std::array<float, 4> m_pcHold{}; // switch board 47k/1uF LED drivers on PC0-3
	std::array<float, size_t(Led::Count)> m_ledLevel{};
};

} // namespace tb303
