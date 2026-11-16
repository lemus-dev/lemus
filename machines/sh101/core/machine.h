#pragma once

#include "lines.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sh101 {

// The SH-101's CPU section: the original TMP80C49P-6-7301 firmware on the MCS-48 core, the
// 4 x 8 keyboard matrix and the switch columns, the 6-bit DAC with its 4052 demultiplexer and
// three sample-and-holds, and the P2 latch (gate, PORTA OFF, CLK RST, LEDs). T0 is the HOLD
// pedal, T1 the inverted clock. See docs/firmware.md for the map and docs/sequencer.md for
// what the firmware does with it.
class Machine {
public:
	static constexpr double kResonatorHz = 6e6;        // CSA 6 MHz ceramic resonator
	static constexpr double kCycleHz = kResonatorHz / 15; // 400k machine cycles per second
	static constexpr int kKeys = 32;                   // F3 (0) .. C6 (31)
	static constexpr int kRamBytes = 128;
	static constexpr int kSequenceStart = 0x1c;        // first step byte
	static constexpr int kMaxSteps = 100;              // 0x1C-0x7F

	// The seven function buttons on the DB5 column.
	enum class Button : uint8_t { Load, Play, ArpDown, ArpUpDown, ArpUp, Hold, KeyTranspose, Count };
	// Panel LEDs: the 4556-decoded mode LEDs, HOLD and KEY TRANSPOSE.
	enum class Led : uint8_t { Load, Play, ArpDown, ArpUpDown, ArpUp, Hold, KeyTranspose, Count };

	Machine();
	~Machine();

	bool setRom(const uint8_t *data, size_t size);
	bool romLoaded() const;
	// CPU reset. Internal RAM is battery backed and kept, as the firmware's INITIAL SET expects.
	void powerOn();

	// Panel inputs. Keys, buttons and the three switches are latched at the next loop boundary
	// (the P2 write that ends each main loop) and read by the loop after it; the pedal and the
	// clock are read directly (T0, T1).
	void setKey(int key, bool down); // 0 = F3 .. 31 = C6
	bool key(int key) const;
	void setButton(Button b, bool down);
	bool button(Button b) const;
	void setRange(Range r);
	void setTrigger(Trigger t);
	void setTranspose(Transpose t);
	void setHoldPedal(bool held); // T0 = !held
	void setClock(bool level);    // CLK: T1 = !level

	// Runs the CPU for one sample period of `rate` (whole instructions; the remainder carries
	// to the next call). Returns true if lines() changed.
	bool tick(double rate);
	// Runs for `seconds` at a nominal 1 MHz tick, for tests and tools.
	void run(double seconds);
	const CpuLines &lines() const;
	float led(Led l) const; // 0..1, smoothed with a 20 ms time constant
	bool ledOn(Led l) const; // the latch as it is now

	// The 128 bytes of internal RAM: registers, keyboard state and the 100-step sequence
	// (0x1C-0x7F). It is the battery-backed state the plugin saves.
	std::array<uint8_t, kRamBytes> ram() const;
	void setRam(const uint8_t *data, size_t n);
	// Battery-backed state for the plugin: a small header and the 128 RAM bytes. Stable across
	// loadState() + powerOn(). loadState() also accepts a bare 128-byte RAM image.
	std::vector<uint8_t> saveState() const;
	bool loadState(const std::vector<uint8_t> &data);
	// Full snapshot (CPU registers, latches, timing) for tests that need an exact resume.
	std::vector<uint8_t> snapshot() const;
	bool restore(const std::vector<uint8_t> &data);

	// Firmware timing, for tests and the MIDI driver: the length of the last main loop
	// (time between two P2 writes), and the number of loops run (each one latches the panel).
	double lastLoopSeconds() const;
	uint64_t loops() const;

	// One sequencer step as the panel enters it (docs/sequencer.md). `note` is the key number
	// plus 12 x the TRANSPOSE switch (L 0, M 12, H 24), so 0..55 = F3 at L .. C6 at H, which is
	// also KCV - 5 (the DAC allows up to 58). A rest has no note and always gates off.
	// `legato` = the step was entered with LEGATO (HOLD) held: PLAY slurs into it from the step
	// before (no gate gap at its start), a tie when the note is the same.
	//
	// In RAM (0x1C-0x7F, one byte per step): a note is 0x80 | 0x40 (legato) | note; a rest is
	// 0x40 | the last key entered; 0x00 ends the sequence (or the 100th step does).
	struct Step {
		bool rest = false;
		int note = 0;
		bool legato = false;
		bool operator==(const Step &o) const
		{
			return rest == o.rest && (rest || (note == o.note && legato == o.legato));
		}
	};
	static constexpr int kPointer = 0x1b; // the step pointer: 0x1B = before the first step
	// Writes a sequence straight into RAM in the firmware's format (at most 100 steps; the rest
	// of the memory is cleared, as LOAD does when it starts from the beginning) and sets the
	// pointer to the start. readSequence() reads up to the end marker.
	void writeSequence(const std::vector<Step> &steps);
	std::vector<Step> readSequence() const;
	static uint8_t encodeStep(const Step &s, int lastNote = 0);
	static Step decodeStep(uint8_t b);
	// A 16-step sequence for tests and tools (docs/sequencer.md, "No sequence at first").
	static std::vector<Step> demoSequence();

	// Test and tool access.
	struct Observer {
		virtual ~Observer() = default;
		virtual void dacWrite(int /*channel*/, int /*code*/, uint64_t /*cycle*/) {}
		virtual void p2Write(uint8_t /*value*/, uint64_t /*cycle*/) {}
	};
	void setObserver(Observer *o);
	uint64_t cycles() const;   // machine cycles since construction
	uint8_t p2() const;        // P2 latch
	uint8_t tFlags() const;    // the T register, which the firmware uses as a flag byte
	uint16_t pc() const;

	struct Impl;

private:
	std::unique_ptr<Impl> m;
};

} // namespace sh101
