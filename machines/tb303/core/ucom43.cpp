// license:BSD-3-Clause
// copyright-holders:hap (MAME ucom4 core), standalone port for the TB-303 emulator
#include "ucom43.h"

#include <cstring>

namespace ucom43 {

namespace {
// uCOM-43 extra registers live at the top of RAM.
enum { REG_X = 0, REG_Y, REG_R, REG_S, REG_W, REG_Z, REG_F };
} // namespace

void Cpu::reset()
{
	m_pc = 0;
	m_op = 0;
	m_skip = false;
	m_timerLeft = 0;
	m_intLine = false;
	m_intF = 0;
	m_inteF = 0; // uCOM-43
	for (int i = C; i <= I; ++i)
		output(i, 0);
}

void Cpu::setInt(bool state)
{
	if (!m_intLine && state)
		m_intF = 1;
	m_intLine = state;
}

uint8_t Cpu::input(int port)
{
	port &= 0xf;
	uint8_t v = 0;
	switch (port) {
	case A:
	case B:
		v = m_bus.read(port);
		break;
	case C:
	case D:
		v = m_bus.read(port) | m_portOut[port];
		break;
	default:
		break;
	}
	return v & 0xf;
}

void Cpu::output(int port, uint8_t data)
{
	port &= 0xf;
	data &= 0xf;
	if (port >= C && port <= I)
		m_bus.write(port, port == I ? data & 7 : data);
	m_portOut[port] = data;
}

void Cpu::popStack()
{
	m_pc = m_stack[0] & kPrgMask;
	m_stack[0] = m_stack[1];
	m_stack[1] = m_stack[2];
}

void Cpu::pushStack()
{
	m_stack[2] = m_stack[1];
	m_stack[1] = m_stack[0];
	m_stack[0] = m_pc;
}

void Cpu::fetchArg()
{
	// 2-byte opcodes: STM/LDI/CLI/CI, JMP/CAL, OCD
	if ((m_op & 0xfc) == 0x14 || (m_op & 0xf0) == 0xa0 || m_op == 0x1e) {
		tick(1);
		m_arg = romByte(m_pc);
		incrementPc();
	}
}

void Cpu::doInterrupt()
{
	tick(1);
	pushStack();
	m_pc = 0xf << 2;
	m_intF = 0;
	m_inteF = 0;
}

int Cpu::run(int cycles)
{
	m_icount = cycles;
	while (m_icount > 0) {
		// interrupt, but not during LI($9x) or EI($31) or while skipping
		if (m_intF && m_inteF && (m_op & 0xf0) != 0x90 && m_op != 0x31 && !m_skip)
			doInterrupt();

		m_prevOp = m_op;
		m_prevPc = m_pc;
		tick(1);
		m_op = romByte(m_pc);
		m_bitmask = uint8_t(1 << (m_op & 0x03));
		incrementPc();
		fetchArg();
		if (m_skip) {
			m_skip = false;
			m_op = 0; // nop
		}
		execute();
	}
	return cycles - m_icount;
}

void Cpu::execute()
{
	const uint8_t op = m_op;
	switch (op & 0xf0) {
	case 0x80: // LDZ X
		m_dph = 0;
		m_dpl = op & 0x0f;
		return;
	case 0x90: // LI X: only the first of a run of LIs executes
		if ((m_prevOp & 0xf0) != (op & 0xf0))
			m_acc = op & 0x0f;
		return;
	case 0xa0: // JMP / CAL
		if (op & 0x08)
			pushStack();
		m_pc = uint16_t(((op & 0x07) << 8 | m_arg) & kPrgMask);
		return;
	case 0xb0: // CZP
		pushStack();
		m_pc = uint16_t((op & 0x0f) << 2);
		return;
	case 0xc0:
	case 0xd0:
	case 0xe0:
	case 0xf0: // JCP
		m_pc = uint16_t((m_pc & ~0x3f) | (op & 0x3f));
		return;
	default:
		break;
	}

	switch (op) {
	case 0x00: return; // NOP
	case 0x01: m_inteF = 0; return; // DI
	case 0x02: ramW(m_acc); return; // S
	case 0x03: m_skip = m_intF != 0; m_intF = 0; return; // TIT
	case 0x04: m_skip = m_carry != 0; return; // TC
	case 0x05: m_skip = m_timerF != 0; return; // TTM
	case 0x06: m_acc = (m_acc + 6) & 0xf; return; // DAA
	case 0x07: m_dpl = m_acc; return; // TAL
	case 0x08: // AD
		m_acc += ramR();
		m_skip = (m_acc & 0x10) != 0;
		m_acc &= 0xf;
		return;
	case 0x09: // ADS
	case 0x19: // ADC
		m_acc += ramR() + m_carry;
		m_carry = (m_acc >> 4) & 1;
		m_acc &= 0xf;
		if (op == 0x09)
			m_skip = m_carry != 0;
		return;
	case 0x0a: m_acc = (m_acc + 10) & 0xf; return; // DAS
	case 0x0b: m_carry = 0; return; // CLC
	case 0x0c: m_skip = m_acc == ramR(); return; // CM
	case 0x0d: m_acc = (m_acc + 1) & 0xf; m_skip = m_acc == 0; return; // INC
	case 0x0e: output(m_dpl, m_acc); return; // OP
	case 0x0f: m_acc = (m_acc - 1) & 0xf; m_skip = m_acc == 0xf; return; // DEC
	case 0x10: m_acc ^= 0xf; return; // CMA
	case 0x11: m_acc = ((m_acc ^ 0xf) + 1) & 0xf; return; // CIA
	case 0x12: m_acc = m_dpl; return; // TLA
	case 0x13: m_dpl = (m_dpl - 1) & 0xf; m_skip = m_dpl == 0xf; return; // DED
	case 0x14: // STM X: timer in steps of 63 machine cycles
		m_timerF = 0;
		m_timerLeft = 63 * ((m_arg & 0x3f) + 1);
		return;
	case 0x15: m_dph = (m_arg >> 4) & 0xf; m_dpl = m_arg & 0x0f; return; // LDI
	case 0x16: m_skip = m_dpl == (m_arg & 0x0f); return; // CLI
	case 0x17: m_skip = m_acc == (m_arg & 0x0f); return; // CI
	case 0x18: m_acc ^= ramR(); return; // EXL
	case 0x1a: { const uint8_t c = m_carry; m_carry = m_carrySave; m_carrySave = c; return; } // XC
	case 0x1b: m_carry = 1; return; // STC
	case 0x1c: return; // illegal
	case 0x1d: { const uint8_t v = (ramR() + 1) & 0xf; ramW(v); m_skip = v == 0; return; } // INM
	case 0x1e: output(D, m_arg >> 4); output(C, m_arg & 0xf); return; // OCD
	case 0x1f: { const uint8_t v = (ramR() - 1) & 0xf; ramW(v); m_skip = v == 0xf; return; } // DEM
	case 0x30: { const uint8_t c = m_acc & 1; m_acc = uint8_t(m_acc >> 1 | m_carry << 3); m_carry = c; return; } // RAR
	case 0x31: m_inteF = 1; return; // EI
	case 0x32: m_acc = input(m_dpl); return; // IP
	case 0x33: m_dpl = (m_dpl + 1) & 0xf; m_skip = m_dpl == 0; return; // IND
	case 0x40: tick(1); m_acc = input(A); return; // IA
	case 0x41: tick(1); m_pc = uint16_t((m_pc & ~0x3f) | (m_acc << 2)); return; // JPA
	case 0x42: tick(1); regW(REG_Z, m_acc); return; // TAZ
	case 0x43: tick(1); regW(REG_W, m_acc); return; // TAW
	case 0x44: tick(1); output(E, m_acc); return; // OE
	case 0x45: return; // illegal
	case 0x46: tick(1); regW(REG_Y, m_dpl); return; // TLY
	case 0x47: tick(1); regW(REG_X, m_dph); return; // THX
	case 0x48: tick(1); popStack(); return; // RT
	case 0x49: tick(1); popStack(); m_skip = true; return; // RTS
	case 0x4a: { tick(1); const uint8_t a = m_acc; m_acc = regR(REG_Z); regW(REG_Z, a); return; } // XAZ
	case 0x4b: { tick(1); const uint8_t a = m_acc; m_acc = regR(REG_W); regW(REG_W, a); return; } // XAW
	case 0x4c: { tick(1); const uint8_t l = m_dpl; m_dpl = regR(REG_S); regW(REG_S, l); return; } // XLS
	case 0x4d: { tick(1); const uint8_t h = m_dph; m_dph = regR(REG_R); regW(REG_R, h); return; } // XHR
	case 0x4e: { tick(1); const uint8_t l = m_dpl; m_dpl = regR(REG_Y); regW(REG_Y, l); return; } // XLY
	case 0x4f: { tick(1); const uint8_t h = m_dph; m_dph = regR(REG_X); regW(REG_X, h); return; } // XHX
	default:
		break;
	}

	switch (op & 0xfc) {
	case 0x20: tick(1); m_skip = (regR(REG_F) & m_bitmask) == 0; return; // FBF
	case 0x24: m_skip = (m_acc & m_bitmask) != 0; return; // TAB
	case 0x28: // XM
	case 0x2c: // XMD
	case 0x3c: { // XMI
		const uint8_t a = m_acc;
		m_acc = ramR();
		ramW(a);
		m_dph ^= (op & 0x03);
		if ((op & 0xfc) == 0x2c) {
			m_dpl = (m_dpl - 1) & 0xf;
			m_skip = m_dpl == 0xf;
		} else if ((op & 0xfc) == 0x3c) {
			m_dpl = (m_dpl + 1) & 0xf;
			m_skip = m_dpl == 0;
		}
		return;
	}
	case 0x34: m_skip = (m_acc & m_bitmask) == (ramR() & m_bitmask); return; // CMB
	case 0x38: m_acc = ramR(); m_dph ^= (op & 0x03); return; // LM
	case 0x50: m_skip = (input(m_dpl) & m_bitmask) != 0; return; // TPB
	case 0x54: m_skip = (input(A) & m_bitmask) != 0; return; // TPA
	case 0x58: m_skip = (ramR() & m_bitmask) != 0; return; // TMB
	case 0x5c: tick(1); m_skip = (regR(REG_F) & m_bitmask) != 0; return; // FBT
	case 0x60: output(m_dpl, m_portOut[m_dpl] & ~m_bitmask); return; // RPB
	case 0x64: tick(1); output(E, m_portOut[E] & ~m_bitmask); return; // REB
	case 0x68: ramW(ramR() & ~m_bitmask); return; // RMB
	case 0x6c: tick(1); regW(REG_F, regR(REG_F) & ~m_bitmask); return; // RFB
	case 0x70: output(m_dpl, m_portOut[m_dpl] | m_bitmask); return; // SPB
	case 0x74: tick(1); output(E, m_portOut[E] | m_bitmask); return; // SEB
	case 0x78: ramW(ramR() | m_bitmask); return; // SMB
	case 0x7c: tick(1); regW(REG_F, regR(REG_F) | m_bitmask); return; // SFB
	default:
		return;
	}
}

void Cpu::save(State &s) const
{
	std::memcpy(s.stack, m_stack, sizeof(m_stack));
	std::memcpy(s.portOut, m_portOut, sizeof(m_portOut));
	std::memcpy(s.ram, m_ram, sizeof(m_ram));
	s.op = m_op;
	s.prevOp = m_prevOp;
	s.arg = m_arg;
	s.skip = m_skip;
	s.pc = m_pc;
	s.acc = m_acc;
	s.dpl = m_dpl;
	s.dph = m_dph;
	s.carry = m_carry;
	s.carrySave = m_carrySave;
	s.timerF = m_timerF;
	s.intF = m_intF;
	s.inteF = m_inteF;
	s.intLine = m_intLine;
	s.timerLeft = m_timerLeft;
}

void Cpu::load(const State &s)
{
	std::memcpy(m_stack, s.stack, sizeof(m_stack));
	std::memcpy(m_portOut, s.portOut, sizeof(m_portOut));
	std::memcpy(m_ram, s.ram, sizeof(m_ram));
	m_op = s.op;
	m_prevOp = s.prevOp;
	m_arg = s.arg;
	m_skip = s.skip;
	m_pc = s.pc;
	m_acc = s.acc;
	m_dpl = s.dpl;
	m_dph = s.dph;
	m_carry = s.carry;
	m_carrySave = s.carrySave;
	m_timerF = s.timerF;
	m_intF = s.intF;
	m_inteF = s.inteF;
	m_intLine = s.intLine;
	m_timerLeft = s.timerLeft;
}

} // namespace ucom43
