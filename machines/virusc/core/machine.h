// The Virus C's board: the 80C515 running the firmware, the DSP56362 it boots over the host port,
// the LCD, the key and LED matrix, the knobs' A/D multiplexer, the banked flash and RAM, MIDI.
// One call to runFrame() is one audio frame, 1/46875 s, with both processors in lockstep, so a
// render is the same every time. docs/firmware.md has the wiring and its sources.
#pragma once

#include "c515.h"
#include "hd44780.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace virusc {

constexpr int kRomSize = 524288;
constexpr double kSampleRate = 46875.0;  // 12 MHz / 256
constexpr double kMcuClock = 12000000.0; // the 80C515's crystal (docs/firmware.md)
constexpr int kKnobs = 32;

class Machine : private C515::Bus {
public:
	Machine();
	~Machine() override;

	bool setRom(const uint8_t *rom, size_t size);
	bool romLoaded() const { return !m_rom.empty(); }

	// Both processors from reset; the battery RAM as it is.
	void powerOn();
	// One audio frame: the 80C515's share, then the DSP's up to the next frame it puts out.
	void runFrame();
	// One audio frame with the DSP held: the 80C515, the panel and the LCD run on, the output is
	// silent. For sleep: whoever calls it wakes the DSP as soon as hostAccesses() changes.
	void runFrameDspHeld();
	// Every access of the 80C515 to the host port, counted.
	uint64_t hostAccesses() const { return m_hostAccesses; }
	// During boot, unheard: a chord played and released, so that the recompiler has met the code a
	// first note runs (docs/firmware.md). Returns once the tail has gone; a program in the unit's
	// state is not changed.
	void warmUp();

	// The frame runFrame() produced (silence until the firmware starts the ESAI): the six output jacks,
	// OUT 1 L, OUT 1 R, OUT 2 L, OUT 2 R, OUT 3 L, OUT 3 R. The DSP's ESAI sends OUT n on its
	// transmitter n-1, left in slot 0 and right in slot 1 (docs/firmware.md, "Audio in and out").
	static constexpr int kOutputs = 6;
	float out(int jack) const { return m_out[jack]; }
	float left() const { return m_out[0]; }
	float right() const { return m_out[1]; }
	// The external inputs, IN L and IN R, for the frames from the next one on: the ESAI's receiver 0,
	// left in slot 0. The DSP reads its input kInputAhead frames ahead of the frame it sends.
	void setInput(float left, float right)
	{
		m_inL = left;
		m_inR = right;
	}
	static constexpr int kInputAhead = 32;
	uint64_t frames() const { return m_frames; }
	// The most frame periods the DSP may run to hand over one frame (runFrame).
	static constexpr uint64_t kDspBudgetFrames = 16;
	// For tools: frames the DSP made, frames it didn't make within that budget once it had started (the last
	// sample repeats: should stay 0), frames made but not taken yet.
	uint64_t framesMade() const { return m_framesMade; }
	uint64_t framesMissed() const { return m_framesMissed; }
	size_t framesQueued() const;
	uint64_t dspInstructions() const;

	void midiIn(uint8_t byte) { m_midiIn.push_back(byte); }
	std::vector<uint8_t> takeMidiOut();

	// A panel key, row 0-7 and bit 0-7, as MAME's matrix has them.
	void setKey(int row, int bit, bool down);
	// A knob, 0-127, by MAME's index (ADC channel * 4 + mux).
	void setKnob(int knob, int value) { m_knobs[size_t(knob)] = uint8_t(value); }
	int knob(int knob) const { return m_knobs[size_t(knob)]; }

	const Hd44780 &lcd() const { return m_lcd; }

	// The LEDs: seven columns (P5 bits 0-2), each lit from two 7-bit latches loaded from P4, one open
	// while P4 bit 7 is high, one while P5 bit 3 is high (docs/firmware.md, "LEDs"). Bits 0-4 of each
	// latch are LEDs; bits 5 and 6 are steady lines. ledDuty() gives, for column c and line l (0-6
	// latch A, 7-13 latch B), the share of the time that column was selected with the line high.
	// Each column's share is taken over at least kLedWindow cycles of that column (two passes of the
	// scan, about 30 ms in all); until a column has that much, it keeps its last value. A shorter
	// window, such as one audio block, catches most columns not selected at all and reads them dark.
	static constexpr int kLedColumns = 7, kLedLines = 14;
	static constexpr uint64_t kLedWindow = 4000; // 80C515 machine cycles (1 MHz); one column's turn is ~2300
	void ledDuty(float out[kLedColumns][kLedLines]);
	// The two LFO RATE LEDs are not in that scan: the DSP drives them with PWM from its timers, timer 2
	// for LFO 1 and timer 1 for LFO 2/3 (docs/firmware.md, "LEDs"). lfoLed() gives each one's brightness
	// as the timers stand now (0 LFO 1, 1 LFO 2/3), on ledDuty()'s scale: a scanned LED is lit one
	// seventh of the time, so with the same current a PWM duty of 1/7 matches a fully lit scanned LED.
	// **Assumption:** the same current; the board isn't known.
	float lfoLed(int which) const;
	// A DSP timer's control/status, load and compare registers, for tools.
	void dspTimer(int index, uint32_t &tcsr, uint32_t &tlr, uint32_t &tcpr) const;
	// The latches as they are now, for tools: (A << 7) | B for the selected column, and that column.
	int ledColumn() const { return m_ledColumn; }
	uint16_t ledLatches() const { return uint16_t((m_latchA << 7) | m_latchB); }

	// The battery-backed RAM banks.
	std::vector<uint8_t> &ram() { return m_ram; }

	// Tools.
	C515 &mcu() { return m_mcu; }
	bool dspStarted() const;
	uint32_t dspPc() const;
	// For tools: `count` instructions of the DSP's program memory from `from`, one per line.
	std::string dspDisassemble(uint32_t from, int count) const;
	uint32_t dspPctl() const;
	size_t hostWordsSent() const { return m_hostWords; }
	// The flash's 64 KB sectors the firmware has written (the SYSTEM menu's STORE TO FLASH), each as its
	// number then its bytes, for the session; and putting them back, after setRom.
	std::vector<uint8_t> flashChanges() const;
	void setFlashChanges(const std::vector<uint8_t> &changes);
	// Skipping the DSP's idle loop (idleStep), on unless turned off to compare; DSP instructions skipped so far.
	void setIdleSkip(bool on) { m_idleSkip = on; }
	uint64_t idleSkipped() const { return m_idleSkipped; }
	// For tools: times the DSP's program went back to its bootstrap ROM (dspToBootRom).
	int dspReboots() const { return m_dspReboots; }
	std::array<uint64_t, 4> hostBySelect() const { return m_hostBySelect; }
	// For tools: words the 80C515 sent that the DSP hasn't read yet, and words the DSP sent the other way.
	size_t dspRxPending() const;
	uint32_t dspX(uint32_t address) const; // tools: DSP X memory
	uint64_t dspIdleLoopSignature() const;  // tools: a hash of the program words of the DSP's idle loop
	size_t dspTxPending() const;
	// Tools: called with every write on the 80C515's data bus (pc, address, data, P5).
	std::function<void(uint16_t, uint16_t, uint8_t, uint8_t)> onXWrite;
	std::function<void(uint8_t)> onP5;
	std::function<void(int, uint8_t)> onPortOut; // port, value: every write to P0-P5
	std::function<void(int)> onPortIn;           // port: every read of P0-P5
	std::function<void(uint32_t, uint64_t)> onDspBlock; // tools: each DSP step's first PC and the instructions it ran
	std::function<void(uint16_t, uint16_t, uint8_t)> onXRead; // pc, address, P5: reads of the window at $8000
	// Every access to the host port registers, while logging is on.
	struct HostAccess { uint64_t cycle; uint16_t pc; uint8_t write, offset, data; };
	void setHostLog(bool on) { m_hostLogOn = on; m_hostLog.clear(); }
	const std::vector<HostAccess> &hostLog() const { return m_hostLog; }

private:
	struct Dsp;

	// C515::Bus
	uint8_t xread(uint16_t addr) override;
	void xwrite(uint16_t addr, uint8_t data) override;
	uint8_t portIn(int port) override;
	void portOut(int port, uint8_t data) override;
	uint8_t analogIn(int channel) override;
	bool rxd() override;
	bool rxIdle() override { return !m_rxBusy && m_midiIn.empty(); }
	void txByte(uint8_t data) override;

	uint8_t hostRead(int offset);
	void hostWrite(int offset, uint8_t data);
	uint8_t hostReadRegister(int offset);
	void hostWriteRegister(int offset, uint8_t data);
	void hostPins();
	void hostService();
	void dspToBootRom();
	void hostReset();
	void bootWord(uint32_t word);
	void serverWord(uint32_t word, uint8_t last);
	void idleStep(uint64_t limit);
	uint32_t flashAddress(uint16_t a) const;
	uint8_t flashRead(uint32_t address);
	void flashWrite(uint32_t address, uint8_t d);
	void selectBanks(uint8_t p5);
	void runMcuShare();

	std::vector<uint8_t> m_rom;
	std::vector<uint8_t> m_ram;          // 32 KB at $0000 and the four 32 KB battery banks at $8000
	C515 m_mcu;
	Hd44780 m_lcd;
	std::unique_ptr<Dsp> m_dsp;

	int m_romBank = 0, m_ramBank = 0, m_scan = 0, m_anSelect = 0;
	uint8_t m_p4Latch = 0xff;
	uint8_t m_latchA = 0, m_latchB = 0;
	int m_ledColumn = 7;
	uint64_t m_ledSince = 0;
	uint64_t m_ledOn[8][kLedLines]{}, m_colTime[8]{};
	float m_ledOut[kLedColumns][kLedLines]{};
	void ledAccount();
	std::array<uint8_t, 8> m_rows{};
	std::array<uint8_t, kKnobs> m_knobs{};

	uint8_t m_lcdLatch = 0;
	bool m_lcdE = false;
	uint8_t m_lcdRead = 0xff;

	// the host port, as the 80C515 sees it (DSP56362UM ch. 6)
	uint8_t m_icr = 0, m_cvr = 0x32, m_ivr = 0x0f;
	std::array<uint8_t, 3> m_tx{};
	uint32_t m_rxWord = 0;
	bool m_rxValid = false;
	int m_dspReboots = 0;
	uint8_t m_p3Latch = 0xff, m_p1Latch = 0xff;
	// P3.3 low selects the host port at $0400-$0407; high, those are RAM like the rest (part 1's edit buffer
	// starts there). The firmware drives it low around every access (docs/firmware.md, "Host port boot").
	bool hostSelected() const { return !(m_p3Latch & 0x08); }
	std::array<uint64_t, 4> m_hostBySelect{}; // tools: reads with P3.3 low, high; writes low, high
	bool m_idleSkip = true;              // idleStep; off for comparing
	uint64_t m_idleSkipped = 0;
	bool m_swapped = false;              // P1.0 low: code below $8000 from the RAM, data to and from the flash
	int m_flashCycle = 0;                // the flash's unlock cycles seen
	bool m_flashProgram = false, m_flashEraseSetup = false, m_flashId = false, m_flashBusy = false;
	uint8_t m_flashLast = 0, m_flashToggle = 0;
	std::array<bool, 8> m_sectorWritten{};
	bool m_server = false;               // the DSP runs the program that holds the flash data (serverWord)
	std::vector<uint8_t> m_serverData;
	size_t m_serverRead = 0;
	size_t m_hostWords = 0;
	bool m_hreq = false; // the host port's HREQ is asserted (low on P3.2)
	uint64_t m_hostAccesses = 0;

	// MIDI IN as a line: a byte's ten bits at 31250 baud.
	std::deque<uint8_t> m_midiIn;
	uint64_t m_rxStart = 0;
	bool m_rxBusy = false;
	uint8_t m_rxByte = 0;
	std::vector<uint8_t> m_midiOut;

	uint8_t m_p5Latch = 0xff;
	bool m_hostLogOn = false;
	std::vector<HostAccess> m_hostLog;

	double m_mcuDebt = 0.0;
	uint64_t m_frames = 0, m_framesMade = 0, m_framesMissed = 0;
	float m_out[kOutputs]{};
	float m_inL = 0, m_inR = 0;
};

} // namespace virusc
