// The Bass Station Rack: the CPU board running firmware v1.5 (core/board.h, MAME's MN1880 core in
// core/mn1880/) and the analog voice it drives (core/voice.h). No JUCE.
//
// Per host sample the CPU runs 8 MHz / host rate oscillator clocks (a 32.32 fixed-point position, so
// the same input gives the same output at any block size), then the voice gets that sample's
// VoiceFrame (core/voice_io.h): the DCO sync edges at their times within the sample, the S&H codes,
// the mixer levels and the selects.
#pragma once

#include "controls.h"
#include "voice_io.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace bsrack {

class Board;
class Voice;
struct VoiceParts;

// What the front panel's display shows (the latches, as last multiplexed).
struct Display {
	// Each digit's segments: a, b, c, d, e, f, g in bits 0-6, the decimal point in bit 7.
	std::array<uint8_t, 2> digits{};  // [0] the tens digit, [1] the units digit
	bool program = false, midiRx = false, midiTx = false, utility = false, save = false;  // the mode LEDs
	bool edit = false;   // the tens digit's decimal point (v1.5 at 3BC5)
	bool write = false;  // flashes while the Write switch is at ENABLE (3BA7)
	bool lfo = false;    // the LFO SPEED LED
};

class Machine {
public:
	// The program EPROM (32 KB, v1.5); empty: nothing to run. A new machine has a new unit's EEPROM
	// (Board::defaultEeprom) and is powered on.
	explicit Machine(std::vector<uint8_t> rom = {});
	~Machine();
	Machine(const Machine &) = delete;
	Machine &operator=(const Machine &) = delete;

	void prepare(double hostRate);

	// The unit has no power switch: plugging the adapter in is the power-on (a reset of the CPU and
	// the board; the EEPROM keeps its contents). A plugin load is not one.
	void powerOn();
	void powerOff();
	bool powered() const { return m_powered; }

	void setControls(const Controls &c);
	const Controls &controls() const { return m_controls; }
	void setButton(int button, bool down);

	// A byte arriving at MIDI IN: the UART receives it at 31.25 kbaud from now (after the bytes
	// still on the line).
	void midiIn(uint8_t byte);

	// n host-rate samples of OUTPUT; filterIn: FILTER INPUT's samples (nullptr: nothing plugged in).
	void render(float *out, int n, const float *filterIn);

	// The voice has settled (Voice::settled()): render keeps running the CPU but skips the voice,
	// giving exact zeros, until the VCA CV moves or wake() is called.
	bool asleep() const { return m_asleep; }
	void wake() { m_asleep = false; }

	// The 2816 EEPROM (2 KB): the 60 user programs and the global settings. Loading one boots the
	// firmware again from it (the program and the MIDI channels are read at boot).
	std::vector<uint8_t> saveEeprom() const;
	bool loadEeprom(const std::vector<uint8_t> &data);

	// The whole unit for a session: a plugin load isn't a power-on, so a session reopens the unit as
	// it was, the sound being edited and the display included (the CPU, its RAM and timers, the
	// board, the EEPROM, the clock). Call between render()s. snapshot: empty before anything has run
	// (no ROM, or not rendered yet). restore: false, and the unit as it was, if the data isn't a
	// snapshot of a unit that ran; the voice starts again from its CVs.
	std::vector<uint8_t> snapshot() const;
	bool restore(const std::vector<uint8_t> &data);

	// The last sample's VoiceFrame.
	const VoiceFrame &frame() const { return m_frame; }
	// The voice's circuit values (for the fitting tools; core/voice.h).
	const VoiceParts &voiceParts() const;
	void setVoiceParts(const VoiceParts &p);
	// The voice's delay, host samples.
	double latencySamples() const;
	// The filter solver's unconverged samples since the last call (tools/corners).
	int takeUnconverged();
	// DCO edges that didn't fit a sample's VoiceFrame (more than kMaxEdges), since the machine was made.
	uint64_t droppedEdges() const { return m_droppedTotal; }
	// The display, safe to read from another thread.
	Display display() const;
	// MIDI OUT: the bytes the UART has sent, oldest first (the queue holds 4096; more are dropped).
	// at (if given): the sample each was sent in, on samples()' count.
	int takeMidiOut(uint8_t *dest, int max, uint64_t *at = nullptr);
	// Host samples rendered since the machine was made.
	uint64_t samples() const { return m_samples; }

	bool romLoaded() const { return m_romLoaded; }
	double rate() const { return m_rate; }

	static constexpr size_t kRomSize = 32768;
	static constexpr size_t kEepromSize = 2048;

private:
	void runSample();

	std::unique_ptr<Board> m_board;
	std::unique_ptr<Voice> m_voice;
	Controls m_controls;
	bool m_romLoaded = false;
	double m_rate = 48000;
	bool m_powered = true;
	bool m_asleep = false;
	uint16_t m_sleepVca = 0;

	// The CPU's target position in oscillator clocks: whole clocks and a 32-bit fraction; one host
	// sample's length likewise.
	uint64_t m_clock = 0, m_stepClocks = 0;
	uint32_t m_clockFrac = 0, m_stepFrac = 0;
	double m_step = 0;
	uint64_t m_sampleStart = 0;
	uint32_t m_sampleStartFrac = 0;
	VoiceFrame m_frame;

	std::array<uint8_t, 4096> m_midiOut{};
	std::array<uint64_t, 4096> m_midiOutAt{};
	size_t m_midiOutHead = 0, m_midiOutCount = 0;
	uint64_t m_samples = 0, m_droppedTotal = 0;
	std::atomic<uint32_t> m_display{0};
};

} // namespace bsrack
