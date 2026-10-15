// license:BSD-3-Clause
// copyright-holders:hap (MAME ucom4 core), standalone port for the TB-303 emulator
//
// NEC uCOM-43 4-bit MCU (uPD650 and friends): 2000x8 ROM, 96x4 RAM, 3-level stack.
// Ported from MAME's src/devices/cpu/ucom4 with the device framework replaced by a small
// port interface. Behaviour, including MAME's open questions, is unchanged.
#pragma once

#include <cstdint>

namespace ucom43 {

enum Port { A = 0, B, C, D, E, F, G, H, I };

class Bus {
public:
	virtual ~Bus() = default;
	// A and B are inputs; C and D read back what was written ORed with the pins.
	virtual uint8_t read(int port) = 0;
	virtual void write(int port, uint8_t data) = 0;
};

class Cpu {
public:
	explicit Cpu(Bus &bus) : m_bus(bus) {}

	void setRom(const uint8_t *rom, int size) { m_rom = rom; m_romSize = size; }
	void reset();

	// Runs at least `cycles` machine cycles (one machine cycle = 4 oscillator clocks).
	// Returns the cycles actually used (instructions are not split).
	int run(int cycles);

	// /INT pin (edge triggered on the rising edge of `state`, as in MAME).
	void setInt(bool state);

	uint16_t pc() const { return m_pc; }
	const uint8_t *ram() const { return m_ram; }
	uint8_t *ram() { return m_ram; }
	uint8_t portOut(int port) const { return m_portOut[port & 0xf]; }

	// Serialisable state.
	struct State {
		uint16_t stack[3];
		uint8_t portOut[16];
		uint8_t ram[128];
		uint8_t op, prevOp, arg, skip;
		uint16_t pc;
		uint8_t acc, dpl, dph, carry, carrySave, timerF, intF, inteF, intLine;
		int32_t timerLeft;
	};
	void save(State &s) const;
	void load(const State &s);

private:
	uint8_t romByte(uint16_t a) const { return a < m_romSize ? m_rom[a] : 0; }
	uint8_t input(int port);
	void output(int port, uint8_t data);
	void incrementPc() { m_pc = uint16_t((m_pc & ~0xff) | ((m_pc + 1) & 0xff)); }
	void fetchArg();
	void doInterrupt();
	void execute();

	// 96 nibbles: 0x00-0x4f and 0x70-0x7f; 0x50-0x6f is unmapped (reads 0).
	static bool mapped(int a) { return a < 0x50 || a >= 0x70; }
	uint8_t ramR() const { const int a = ((m_dph << 4) | m_dpl) & kDataMask; return mapped(a) ? m_ram[a] & 0xf : 0; }
	void ramW(uint8_t d) { const int a = ((m_dph << 4) | m_dpl) & kDataMask; if (mapped(a)) m_ram[a] = d & 0xf; }
	uint8_t regR(int i) const { return m_ram[kDataMask - i] & 0xf; }
	void regW(int i, uint8_t d) { m_ram[kDataMask - i] = d & 0xf; }
	void popStack();
	void pushStack();
	void tick(int n) { m_icount -= n; if (m_timerLeft > 0 && (m_timerLeft -= n) <= 0) m_timerF = 1; }

	static constexpr int kPrgMask = 0x7ff;
	static constexpr int kDataMask = 0x7f;

	Bus &m_bus;
	const uint8_t *m_rom = nullptr;
	int m_romSize = 0;
	int m_icount = 0;

	uint16_t m_stack[3] = {};
	uint8_t m_portOut[16] = {};
	uint8_t m_ram[128] = {};
	uint8_t m_op = 0, m_prevOp = 0, m_arg = 0, m_bitmask = 1;
	bool m_skip = false;
	uint16_t m_pc = 0, m_prevPc = 0;
	uint8_t m_acc = 0, m_dpl = 0, m_dph = 0;
	uint8_t m_carry = 0, m_carrySave = 0, m_timerF = 0, m_intF = 0, m_inteF = 0;
	bool m_intLine = false;
	int m_timerLeft = 0; // machine cycles until the timer F/F sets (0 = stopped)
};

} // namespace ucom43
