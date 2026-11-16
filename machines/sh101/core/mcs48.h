// license:BSD-3-Clause
// copyright-holders:Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud
//
// Intel MCS-48 CPU core (8048/8049 family), ported from MAME's src/devices/cpu/mcs48
// (mcs48.cpp/mcs48.h at commit c6eac87a, reference/mame/) for the SH-101 emulator.
//
//     Intel MCS-48/UPI-41 Portable Emulator
//     Copyright Mirko Buffoni
//     Based on the original work Copyright Dan Boris, an 8048 emulator
//
// The MAME device framework is replaced by a small port interface (Bus) and the opcode
// handler table by a switch. Only the MCS-48 opcode set is kept (no UPI-41, 8021 or 8022
// variants). Instruction behaviour and cycle counts are MAME's, including its known
// timer/IRQ quirks (see the TODO in the MAME source). Like MAME there is no CMOS IDL/HALT
// opcode: 01 is illegal. The default configuration is the I8049: 2 KB internal ROM, 128
// bytes of RAM, EA low. One machine cycle is 15 oscillator clocks (÷3 then ÷5).
#pragma once

#include <cstdint>

namespace mcs48 {

// Everything the core reads from or drives onto its pins. Only I/O instructions call these,
// so a virtual call per access costs nothing measurable.
class Bus {
public:
	virtual ~Bus() = default;
	virtual uint8_t busIn() { return 0xff; }       // INS A,BUS (and ANL/ORL BUS read-back, as MAME)
	virtual void busOut(uint8_t) {}                // OUTL BUS,A / ANL BUS / ORL BUS
	virtual uint8_t portIn(int /*port 1|2*/) { return 0xff; }
	virtual void portOut(int /*port 1|2*/, uint8_t) {}
	virtual int testIn(int /*0|1*/) { return 0; }  // T0, T1
	virtual uint8_t extIn(uint8_t) { return 0xff; } // MOVX A,@Rr
	virtual void extOut(uint8_t, uint8_t) {}       // MOVX @Rr,A
	virtual void prog(int) {}                      // PROG strobe to an 8243
};

class Cpu {
public:
	static constexpr int kClockDivider = 15; // oscillator clocks per machine cycle

	struct Config {
		int romSize = 2048; // 0 (external only), 1024, 2048 or 4096
		int ramSize = 128;  // 64, 128 or 256
	};

	explicit Cpu(Bus &bus) : Cpu(bus, Config{}) {}
	Cpu(Bus &bus, const Config &config);

	// Program memory: internal ROM (EA low) of config.romSize bytes; the caller keeps it alive.
	void setRom(const uint8_t *rom, int size) { m_rom = rom; m_romSize = size; }
	void reset(); // RESET pin: internal RAM is kept

	// Runs whole instructions until at least `cycles` machine cycles have been used (at least
	// one instruction) and returns the number used: the overshoot is at most one instruction
	// plus an interrupt entry, and the caller carries it.
	int run(int cycles);

	void setIrq(bool asserted) { m_irqState = asserted; } // /INT low = asserted
	void setEa(bool high) { m_ea = high; }

	// Debug / test access.
	uint16_t pc() const { return m_pc; }
	uint8_t a() const { return m_a; }
	uint8_t psw() const { return m_psw; }
	uint8_t timer() const { return m_timer; } // T register
	bool f1() const { return m_f1; }
	uint8_t p1() const { return m_p1; }
	uint8_t p2() const { return m_p2; }
	uint8_t *ram() { return m_ram; }
	const uint8_t *ram() const { return m_ram; }
	int ramSize() const { return m_ramMask + 1; }
	uint64_t totalCycles() const { return m_totalCycles; }

	// Serialisable state (everything but the ROM and the configuration).
	struct State {
		uint64_t totalCycles;
		uint16_t prevPc, pc, a11;
		uint8_t a, psw, p1, p2, timer, prescaler, t1History, dbbo;
		uint8_t f1, ea, irqState, irqPolled, irqInProgress, timerOverflow, timerFlag;
		uint8_t tirqEnabled, xirqEnabled, timecountEnabled;
		int32_t icount;
		uint8_t ram[256];
	};
	void save(State &s) const;
	void load(const State &s);

private:
	uint8_t programRead(uint16_t a) const { return a < m_romSize && !m_ea ? m_rom[a] : 0; }
	uint8_t fetch()
	{
		const uint16_t a = m_pc;
		m_pc = uint16_t(((m_pc + 1) & 0x7ff) | (m_pc & 0x800));
		return programRead(a);
	}
	uint8_t ramR(uint8_t a) const { return m_ram[a & m_ramMask]; }
	void ramW(uint8_t a, uint8_t v) { m_ram[a & m_ramMask] = v; }
	uint8_t &reg(int r) { return m_ram[m_regBase + r]; }
	void updateRegptr() { m_regBase = (m_psw & 0x10) ? 24 : 0; }

	void pushPcPsw();
	void pullPcPsw();
	void pullPc();
	void add(uint8_t d);
	void addc(uint8_t d);
	void jmp(uint16_t address) { m_pc = uint16_t(address | (m_irqInProgress ? 0 : m_a11)); }
	void call(uint16_t address) { pushPcPsw(); jmp(address); }
	void jcc(bool result)
	{
		const uint16_t pch = m_pc & 0xf00;
		const uint8_t offset = fetch();
		if (result)
			m_pc = uint16_t(pch | offset);
	}
	void expander(int operation, int port);
	void checkIrqs();
	void burn(int count);
	void execute(uint8_t op);

	Bus &m_bus;
	const uint8_t *m_rom = nullptr;
	int m_romSize = 0;
	int m_ramMask = 127;

	uint64_t m_totalCycles = 0;
	uint16_t m_prevPc = 0, m_pc = 0, m_a11 = 0;
	uint8_t m_a = 0, m_psw = 0, m_p1 = 0, m_p2 = 0, m_timer = 0, m_prescaler = 0, m_t1History = 0, m_dbbo = 0;
	bool m_f1 = false, m_ea = false;
	bool m_irqState = false, m_irqPolled = false, m_irqInProgress = false;
	bool m_timerOverflow = false, m_timerFlag = false, m_tirqEnabled = false, m_xirqEnabled = false;
	uint8_t m_timecountEnabled = 0;
	int m_regBase = 0;
	int m_icount = 0;
	uint8_t m_ram[256] = {};
};

} // namespace mcs48
