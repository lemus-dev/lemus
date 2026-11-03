#pragma once

#include "tr909/core/upd7810/upd7810.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace tr909 {

// The TR-909's CPU board: the original firmware (v4.0, 8 KB EPROM IC609) on an emulated
// uPD7811 (external memory, internal ROM unused), the 8 KB battery-backed RAM, the switch
// matrix, the LED matrix, the tempo and TOTAL ACCENT pots on the A/D converter, MIDI on the
// serial port, and the latches the voice board reads: eight 6-bit DACs (accent/level CVs)
// and two trigger latches. See docs/firmware.md for the map.
class Machine : private upd7810_device::Bus {
public:
	// Panel keys, in matrix order: row r (PA bit r) and return bit b give index r*8 + b.
	enum class Key : uint8_t {
		Step1, Step2, Step3, Step4, Step5, Step6, Step7, Step8,
		Step9, Step10, Step11, Step12, Step13, Step14, Step15, Step16,
		Shift, Scale, Row2Bit2, InstSelect, ShuffleFlam, LastStep, StopCont, Start,
		Clear, Track1, Track2, Track3, Track4, Pattern1, Pattern2, Pattern3,
		EnterAccent, TapeSync, Cycle, AvailableMeas, Fwd, Back, Tempo, ExtInst,
		Count
	};

	// Panel LEDs: column (PB strobe 0-4) * 8 + row (PA bit 0-7), from the switch board
	// schematic (IC602 M54516 column sources, IC603 M54562 row sinks).
	enum class Led : uint8_t {
		Step1 = 0, Step2, Step3, Step4, Step5, Step6, Step7, Step8,
		Step9 = 8, Step10, Step11, Step12, Step13, Step14, Step15, Step16,
		Track1 = 17, Track2, Track3, Track4, Pattern1, Pattern2, Pattern3,
		Enter = 24, ExtInst, Tempo, Back, Fwd, Cycle, AvailableMeas, TapeSync,
		Scale1 = 32, Scale2, Scale3, Scale4, // D635 (bottom) .. D632 (top)
		Count = 40
	};

	static constexpr double kXtalHz = 12.0e6;
	static constexpr double kStateHz = kXtalHz / 3; // CPU states per second
	static constexpr int kRamSize = 0x2000;
	static constexpr int kStatesPerMidiBit = 128;    // 12 MHz / 24 / 16 = 31250 baud
	// The receiver samples each bit in its middle (x16 clock) and loads RXB, raising INTSR, at
	// the stop bit's sample: 9.5 bit times after the start bit's edge.
	static constexpr int kStatesToRxByte = 19 * kStatesPerMidiBit / 2;

	// A write to the voice board: reg 1-8 = DAC1-8 (bits 1-6 used), 9 = trigger latch 1,
	// 10 = trigger latch 2. `state` counts CPU states from the start of the current tick.
	struct VoiceWrite {
		uint32_t state;
		uint8_t reg, value;
	};

	Machine();
	bool setRom(const uint8_t *data, size_t size);
	bool romLoaded() const { return m_rom.size() == 0x2000; }
	void powerOn(); // CPU reset; RAM is battery backed and kept

	// Panel
	void setKey(Key k, bool down);
	bool key(Key k) const { return m_keys[size_t(k)]; }
	void setKeyPolarityInverted(bool inv) { m_keyInvert = inv; } // test hook
	// Pots read by the A/D converter, 0-1 of their travel (AN0 TEMPO, AN3 TOTAL ACCENT).
	// setAnalog(1, code) is the cartridge sense: 255 (the default) none, 0 an M-64C inserted
	// (the slot itself is not emulated: it reads FFh).
	void setTempoPot(double v) { m_an[0] = toCode(v); }
	void setTotalAccentPot(double v) { m_an[3] = toCode(v); }
	void setAnalog(int channel, uint8_t code) { m_an[channel & 7] = code; }

	// Rear panel (SN p.8, p.12). DIN SYNC IN: pin 1 START/STOP (high = run), pin 3 TEMPO
	// clock (24 ppqn, the CPU counts rising edges on INT1), pin 5 CONTINUE; with no cable all
	// three are low. Foot switch (DP-2) jack: PC2, read by the firmware as a START/STOP key; the
	// jack grounds PC2 with no plug in, and `pressed` is PC2 high. Call between run()s.
	void setDinSync(bool run, bool clock, bool cont);
	void setFootSwitch(bool pressed) { m_foot = pressed; }
	// Tape sync out: the CPU's TO pin (PC4, timer 1), 1202 Hz stopped, 1202/2404 Hz alternating
	// on every 48 ppqn tick while it plays. (The SAVE data are PB6-7, portB() >> 6.) Both reach
	// the SAVE/OUT jack through the voicing board's tape circuit.
	int tapeSyncOut() const { return m_cpu.to_level() & 1; }

	// MIDI in (bytes are sent on the serial line one after another at 31250 baud) and out, each
	// byte out with the state its stop bit ended at (statesNow()).
	void midiIn(const uint8_t *data, size_t n);
	bool midiInPending() const { return !m_rxQueue.empty(); }
	struct MidiOutByte {
		uint64_t state;
		uint8_t data;
	};
	std::vector<uint8_t> takeMidiOut();
	std::vector<MidiOutByte> takeMidiOutTimed();

	// Runs the board for `states` CPU states. Voice-board writes made during it are in
	// writes() with their time from the start of the call.
	void run(int states);
	// Runs for one sample period at `rate` (keeps the fraction between calls).
	void tick(double rate);
	const std::vector<VoiceWrite> &writes() const { return m_writes; }
	uint64_t states() const { return m_states; }
	// CPU states since power-up, counting the current instruction's slice inside run() (for
	// trace callbacks and tools).
	uint64_t statesNow() const { return m_states + now(); }

	// Voice board latches as last written.
	uint8_t dac(int n) const { return m_dac[(n - 1) & 7]; } // n = 1-8
	uint8_t trig1() const { return m_trig1; }
	uint8_t trig2() const { return m_trig2; }

	// Raw port state for the panel model and tools.
	uint8_t portA() const { return m_cpu.pa_out(); }
	uint8_t portB() const { return m_cpu.pb_out(); }
	uint8_t portC() const { return m_cpu.pc_out(); }
	uint16_t pc() const { return m_cpu.pc(); }
	void setTrace(std::function<void(uint16_t)> f) { m_cpu.trace() = std::move(f); } // tools only
	// LED matrix: the PA value last seen while each column strobe (PB0-4) was high, and a
	// brightness per LED averaged over the multiplex (0-1, 1 = lit on every pass).
	const std::array<uint8_t, 8> &ledLatch() const { return m_ledLatch; }
	bool led(Led l) const { return (m_ledLatch[size_t(l) >> 3] >> (size_t(l) & 7)) & 1; }
	float ledLevel(Led l) const { return m_ledLevel[size_t(l)]; }
	// The 3-digit display (D636): digit 0 = left (hundreds) .. 2 = right, as the BCD code
	// the 4511 held while the digit's column was strobed (10-15 = blank), and as segments
	// (bit 0 = a .. bit 6 = g).
	int digit(int i) const { return m_digit[size_t(i < 0 ? 0 : i > 2 ? 2 : i)]; }
	static uint8_t segments(int bcd);

	// Battery-backed RAM and the whole machine state.
	const std::array<uint8_t, kRamSize> &ram() const { return m_ram; }
	void setRam(const uint8_t *data, size_t n);
	uint8_t peek(uint16_t addr) { return read(addr); }
	void poke(uint16_t addr, uint8_t v) { write(addr, v); }
	uint8_t iram(int i) const { return m_cpu.iram(i); }
	std::vector<uint8_t> saveState() const;
	bool loadState(const std::vector<uint8_t> &data);

private:
	uint8_t read(uint16_t addr) override;
	void write(uint16_t addr, uint8_t data) override;
	uint32_t now() const; // state within the current run()
	int rxd();
	void txd(int level);
	uint8_t portCIn() const;
	uint8_t switches() const;
	void updateLeds(int states);
	void portsChanged();
	static uint8_t toCode(double v) { return uint8_t(v <= 0 ? 0 : v >= 1 ? 255 : v * 255.0 + 0.5); }

	upd7810_device m_cpu;
	std::vector<uint8_t> m_rom;
	std::array<uint8_t, kRamSize> m_ram{};
	std::array<bool, size_t(Key::Count)> m_keys{};
	bool m_keyInvert = false;
	std::array<uint8_t, 8> m_an{};
	bool m_foot = false;
	bool m_dinRun = false, m_dinClock = false, m_dinCont = false;

	// Voice board latches
	std::array<uint8_t, 8> m_dac{};
	uint8_t m_trig1 = 0, m_trig2 = 0;
	std::vector<VoiceWrite> m_writes;

	// Timing
	uint64_t m_states = 0;      // states run before the current run() call
	int m_runIcount0 = 0;       // the current slice's length: its elapsed states are this - icount
	uint32_t m_sliceBase = 0;   // states from the start of run() to the current slice
	double m_stateAcc = 0;

	// MIDI
	struct RxByte { uint64_t start; uint8_t data; };
	std::deque<RxByte> m_rxQueue;
	uint64_t m_rxFree = 0; // first state the line is free for the next byte
	int m_txBit = -1;      // -1 idle, else bits received so far in the frame
	uint16_t m_txShift = 0;
	std::vector<MidiOutByte> m_midiOut;

	// LEDs and display
	std::array<uint8_t, 8> m_ledLatch{};
	std::array<float, 40> m_ledLevel{};
	std::array<int, 3> m_digit{{15, 15, 15}};
	uint8_t m_bcd = 15; // IC601 4511 latch
};

} // namespace tr909
