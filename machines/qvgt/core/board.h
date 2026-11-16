// The QuadraVerb GT's main board, its digital side: the 80C31 with its EPROM and battery-backed
// SRAM, the HD44780 LCD, the output latches (two of them the preamp's), the key matrix, MIDI, and
// the DASP 24 with its control store and delay memory. No JUCE.
//
// XDATA (QuadraVerb schematic and code; the GT's U35 decodes the same, docs/research.md):
//   8000h-FFFFh  SRAM (32K x 8), selected by A15
//   writes 0000h-7FFFh go to the 74HC138 on A9-A11:
//     Y0 0000h  DASP 24 control store, A0-A8 its address
//     Y1 0200h  LCD, A0 = RS
//     Y2 0400h, Y3 0600h, Y4 0800h  latches (LEDs, the dry VCA level, the level detector's reset)
//     Y5 0A00h  U11, preamp (overdrive level, COMP 1-3, TONE ON/OFF)
//     Y6 0C00h  U10, preamp (DIST ON/1-3, TONE PRESENCE/BRIGHT, BASS BOOST, CABINET ON, 1/2)
#pragma once

#include "dasp24.h"
#include "i8031.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace qvgt {

constexpr double kClock = 24e6;              // Z1
constexpr double kSampleRate = kClock / 576; // 41,666.67 Hz
constexpr int kCyclesPerSample = 24;         // 80C31 machine cycles (12 MHz / 12) per DSP sample
constexpr size_t kRomSize = 0x10000;
constexpr size_t kRamSize = 0x8000;

// The LCD's controller, as far as the firmware uses it.
struct Hd44780 {
	uint8_t ddram[128]{};
	uint8_t cgram[64]{};
	int addr = 0;
	bool cgMode = false, incr = true, displayOn = false;
	void write(bool rs, uint8_t v);
	// One of the two 16-character lines as character codes.
	void line(int n, uint8_t out[16]) const;
	std::string text(int n) const; // printable, for logs: custom characters as '#'
};

class Board : private I8031::Bus {
public:
	// The latches as the 74HC138 numbers them (Y2-Y6).
	enum Latch { kLatch0400 = 2, kLatch0600 = 3, kLatch0800 = 4, kLatchU11 = 5, kLatchU10 = 6 };

	// The 27C512's image as read from the chip (A0-A7 reversed on the board) or descrambled; false
	// if it isn't a QuadraVerb GT's firmware. `rom` comes back descrambled.
	static bool prepareRom(std::vector<uint8_t> &rom);

	explicit Board(std::vector<uint8_t> descrambledRom);
	Board(const Board &) = delete;
	Board &operator=(const Board &) = delete;

	// The POWER switch on: the 80C31 from reset, the SRAM as the battery kept it.
	void powerOn();
	// The 80C31 alone (boot, no audio).
	void runCycles(uint64_t machineCycles);
	// One DSP sample: 24 machine cycles of the 80C31, then the DSP. adc: the converter's input as a
	// fraction of full scale; envelope: the level detector's volts (sheet 2: U16 and D15, a
	// precision peak detector on U2 pin 8, R145 330K and C52 0.1u; core/unit.cpp).
	// outL/outR: the D/A's codes as fractions of full scale.
	void runSample(double adc, double envelope, double &outL, double &outR);
	// The A/D's offset servo (sheet 2): the DASP's LSTMSB (the last conversion's sign) through R140
	// 51K into C64 4.7u, R137 51K from it into the A/D's input node, against R136 510K from -12 V:
	// it holds the input's DC at the conversions' sign change, so the A/D toggles about zero at idle
	// and never reads a steady 0. Off: the A/D reads exactly what it is given (a noiseless model).
	void setAdcServo(bool on) { m_adcServo = on; }
	// One sample with the DSP held (asleep): the 80C31 runs on, the delay memory keeps its contents.
	void runSampleDspHeld();

	std::array<uint8_t, kRamSize> &ram() { return m_sram; }
	const Hd44780 &lcd() const { return m_lcd; }
	uint8_t latch(int y) const { return m_latch[size_t(y)]; }
	I8031 &cpu() { return m_cpu; }
	Dasp24 &dsp() { return m_dsp; }
	const uint8_t *controlStore() const { return m_daspActive.data(); }
	// Control-store writes from the 80C31 since the last call (wakes the DSP).
	uint64_t daspWrites() const { return m_daspWrites; }

	// Front panel: the 4 x 4 key matrix (P1.4-P1.7 drive columns low, P1.0-P1.3 read rows). Key k
	// as the firmware's key bitmaps number it: k 0-3 column P1.6, rows 0-3; 4-7 P1.7; 8-11 P1.4;
	// 12-15 P1.5. The GT's keys (docs/firmware.md): 0 EQ, 1 PRE-AMP, 2 VALUE down, 3 VALUE up,
	// 4 MIDI, 5 BYPASS, 6 PAGE down, 7 PAGE up, 8 PITCH, 9 NAME/MOD, 10 STORE, 11 PROG, 12 DELAY,
	// 13 MIX, 14 CONFIG, 15 REVERB.
	void setKey(int k, bool down) { m_keys[size_t(k & 15)] = down; }
	// P3.4 the bypass footswitch, P3.5 the program-advance footswitch (low: pressed).
	void setFootswitch(int which, bool down);
	// The keys' force sensor: a piezo (Z2) under the keypad, buffered (U39) and compared with the
	// level detector's ramp on P3.3; latch 0600h bit 5 (Q4) shorts it. `volts` is what a press
	// leaves on it while a key is down (sheet 2; the firmware turns it into scroll speed).
	void setKeyForce(double volts) { m_pressVolts = volts; }

	// MIDI in, as bytes on the serial line at 31,250 baud; out, as the UART sends them.
	void midiIn(uint8_t byte) { m_midiIn.push_back(byte); }
	std::vector<uint8_t> takeMidiOut();

	// The input level detector (gates, triggers, sheet 2): C108 charges through R209 510K from +12 V
	// and is discharged by latch 0600h bit 6 (Q5); U39 drives P3.2 high while it is above the
	// envelope (runSample's).

private:
	uint8_t xread(uint16_t addr) override;
	void xwrite(uint16_t addr, uint8_t data) override;
	uint8_t portIn(int port) override;
	void portOut(int, uint8_t) override {}
	bool rxd() override;
	bool rxIdle() override;
	void txByte(uint8_t data) override { m_midiOut.push_back(data); }
	void updateLevelDetector(double envelope);
	void commit();
	void runCpu();

	std::vector<uint8_t> m_rom;
	I8031 m_cpu;
	Dasp24 m_dsp;
	std::array<uint8_t, kRamSize> m_sram{};
	Hd44780 m_lcd;
	std::array<uint8_t, 8> m_latch{};
	std::array<uint8_t, 0x200> m_dasp{}, m_daspActive{}; // as the 80C31 writes it; as the DSP runs it
	bool m_daspCommit = false;
	uint64_t m_daspWrites = 0;
	std::array<bool, 16> m_keys{};
	uint8_t m_p3In = 0xff;

	// the serial line: the bytes waiting, and the one on the wire (start, 8 data, stop: 32 cycles a bit)
	std::deque<uint8_t> m_midiIn;
	int m_rxBits = 0;    // bit times left of the byte on the wire
	uint64_t m_rxStart = 0;
	uint8_t m_rxByte = 0;
	std::vector<uint8_t> m_midiOut;

	double m_envelope = 0, m_capVolts = 0;
	bool m_adcServo = true, m_lstMsb = false;
	double m_c64 = 1.2; // C64's volts: where the servo settles with no offset of its own
	double m_pressVolts = 0.1, m_piezo = 0;
	uint64_t m_cycleTarget = 0;
};

} // namespace qvgt
