// The Bass Station Rack's digital board around the MN1880 (core/mn1880/), as firmware v1.5 drives it.
// No JUCE. Everything here is read from the firmware (addresses in the comments; docs/firmware.md
// has the evidence), not from a schematic of the Rack: the hand-traced keyboard schematic leaves
// this part out. [inferred] marks what only the firmware's use shows.
//
// Data space:
//   8000-87FF  2816 EEPROM (2 KB): user programs 40-99 at (p - 40) * 34, global settings 07F8-07FF
//   8800       panel switches (read): bits 0-5 of the rows A800 bits 0-3 select, 0 = contact closed
//   A000       latch: bits 0-3 pot mux address, 4-6 CV S&H channel, 7 S&H enable (34F2-352E)
//   A800       latch: bits 0-3 switch rows, 4-7 keypad rows (active low)
//   B000       latch: LEDs, active low: 0 LFO speed, 1-5 mode (Program, MIDI RX, MIDI TX, Utility,
//              Save), 6 Write (342A-348B, 3BA7)
//   B800       latch: the 7-segment digit (bits d dp e c b a f g, active high); port 3 bit 5 = 1
//              selects the tens digit. The tens digit's point is the Edit LED (3BC5).
// Ports:
//   port 1     bit 0 DAC data, bit 2 DAC clock (rising), bit 1 DAC load pulse (7886-78F5);
//              bit 7 osc 1 and bit 6 osc 2 waveform, 1 = pulse (the 4053; 5C36, 5CCF)
//   port 2     inputs: bit 6 GATE IN (3611, 56EC), bit 4 the Audition button, active low (37CD)
//   port 3     bit 3 OSCA_SYNC (toggled by the IRQ3 handler, 77E2), bit 5 digit select, bit 1 the
//              slope (1 = 12 dB, 6220), bit 7 low while a note is held (342A) [gate out?]
//   port 9     inputs bits 2-5: the keypad columns for the rows A800 bits 4-7 select (3405)
// Timer 1's output pin is OSCB_SYNC [inferred]. ADC (channel = (x'1E') bits 0-2): channel 0 the pot
// the mux selects (mux 0-15), channel 3 mux 16, channel 1 the CV/Gate converter's CV IN.
// CV DAC (12-bit, serial; S&H channel c holds RAM word 0220 + 2c): 0 CV OUT, 1 VCF, 2 OSCB_PWM,
// 3 OSCA_PWM, 4 OSCB_CV, 5 OSCA_CV, 6 RES, 7 VCA (core/voice_io.h). Oscillator levels: the on-chip
// registers (x'34') (osc 1) and (x'32') (osc 2) (6B99, 54A6).
#pragma once

#include "controls.h"
#include "mn1880/mn1880.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace bsrack {

// The pots the firmware reads, by mux address (0-15 on ADC channel 0, 16 on channel 3), from the
// pot handlers (jump table 352F), the MIDI controllers they send and their measured effects.
extern const char *const kMuxPotNames[17];
// controls.h's pots to mux addresses; -1: VOLUME, which the firmware doesn't read (analog, after
// the VCA).
extern const std::array<int, kPots> kPotMux;

// Each panel switch position (controls.h's order) as the switch-matrix contacts it closes:
// row (0-3, the firmware's numbering, A800 bit 3 - row) and a mask of 8800 bits.
struct SwitchContacts {
	int row;
	uint8_t mask;                     // the switch's contacts in the row
	std::array<uint8_t, 4> closed;    // per position: the contacts it closes (0: none)
};
extern const std::array<SwitchContacts, kSwitches> kSwitchContacts;

// controls.h's buttons to keypad codes (row * 4 + column, columns port 9 bits 4, 5, 3, 2; the
// firmware's code table at ROM 146C); -1: AUDITION, on port 2 bit 4.
extern const std::array<int, kButtons> kKeyCode;

class Board : public mn1880_device::Bus {
public:
	static constexpr double kClock = 8e6;  // the oscillator (docs/firmware.md: why 8 MHz)
	static constexpr size_t kRomSize = 32768, kEepromSize = 2048;

	mn1880023_device cpu;
	std::vector<uint8_t> rom = std::vector<uint8_t>(kRomSize, 0xff);
	std::array<uint8_t, kEepromSize> eeprom{};

	// inputs
	std::array<uint8_t, 17> pots{};  // ADC codes by mux address
	uint8_t cvIn = 0;                // ADC channel 1: CV IN
	std::array<uint8_t, 4> switchRows{};  // 8800 bits 0-5 per row, 0 = closed
	std::array<uint8_t, 4> keyRows{};     // port 9 bits 2-5 per keypad row, 0 = pressed
	bool gateIn = false;   // GATE IN: port 2 bit 6 (the pin's level)
	bool audition = false; // the Audition button (port 2 bit 4 low while held)

	// outputs
	uint8_t a000 = 0, a800 = 0xff, b000 = 0xff, b800 = 0;
	std::array<uint8_t, 2> digits{};  // the latch value each digit shows: [0] units, [1] tens
	std::array<uint16_t, 8> cv{};     // the S&Hs' held DAC codes
	uint16_t dacOut = 0;
	std::function<void(int channel, uint16_t code)> onCv;   // a S&H took a new value
	std::function<void(int osc, uint64_t clock)> onSync;   // an edge on OSCA_SYNC (0) / OSCB_SYNC (1)
	std::function<void(uint8_t)> onMidiOut;
	std::function<void(uint16_t addr, uint8_t data)> onLatch;
	std::function<void(uint16_t addr, int data)> onUnmapped;  // data < 0: a read
	std::function<void(int channel, int mux)> onAdc;          // a conversion

	Board();
	Board(const Board &) = delete;
	Board &operator=(const Board &) = delete;

	// fixPotScanLock: apply the patch for v1.5's pot-scan lock-up (board.cpp, docs/firmware.md "A
	// lock-up in v1.5"); only a v1.5 image with the expected bytes is patched. Off: the machine is
	// faithful to the lock-up (decided 2026-11-25); the patch stays for the tools (bsrackseq --patched).
	bool setRom(const std::vector<uint8_t> &data, bool fixPotScanLock = false);
	bool potScanLockFixed() const { return m_potScanFix; }
	void reset() { cpu.reset(); }
	void run(int cycles) { cpu.run(cycles); }

	// controls.h's switch `sw` to position `pos`
	void setSwitch(int sw, int pos);
	// the keypad key `code` (0-15)
	void setKey(int code, bool down);

	uint8_t port1() const { return m_port1; }
	uint8_t port3() const { return m_port3; }

	// The whole board for a session (Machine::snapshot): the CPU, the EEPROM, the inputs as the CPU
	// last saw them, the latches, the DAC and its S&Hs. loadState: false if the data doesn't fit.
	void saveState(std::vector<uint8_t> &out) const;
	bool loadState(const uint8_t *&p, const uint8_t *end);

	// A new unit's EEPROM as the owner's manual describes it (docs/firmware.md): programs 40-99
	// "initially stored with a basic square wave tone", MIDI channel 1, program 00. v1.5 has no
	// template or init routine for it, so defaultProgram() builds the tone.
	static std::array<uint8_t, kEepromSize> defaultEeprom();
	static std::array<uint8_t, 34> defaultProgram();

	// The firmware's segment bits (d dp e c b a f g) to a, b, c, d, e, f, g, dp in bits 0-7.
	static uint8_t segments(uint8_t latch);

	// mn1880_device::Bus
	uint8_t read_program(uint16_t addr) override;
	uint8_t read_data(uint16_t addr) override;
	void write_data(uint16_t addr, uint8_t data) override;

private:
	void port1Write(uint8_t d);
	void port3Write(uint8_t d);
	void sample();
	void adc(int channel);

	bool m_potScanFix = false;
	uint8_t m_port1 = 0, m_port3 = 0;
	uint16_t m_dacShift = 0;
	std::array<bool, 8> m_sampled{};
};

} // namespace bsrack
