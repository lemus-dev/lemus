// license:BSD-3-Clause
// copyright-holders:Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud
//
// Intel MCS-48 CPU core, ported from MAME's src/devices/cpu/mcs48/mcs48.cpp (see mcs48.h).
//
//     Intel MCS-48/UPI-41 Portable Emulator
//     Copyright Mirko Buffoni
//     Based on the original work Copyright Dan Boris, an 8048 emulator
//
// MAME's opcode handlers are inlined into execute()'s switch in the order of MAME's
// s_mcs48_opcodes table; each case keeps its handler's burn_cycles() count.
#include "mcs48.h"

#include <cstring>

namespace mcs48 {

namespace {
// timer/counter enable bits
constexpr uint8_t TIMER_ENABLED = 0x01;
constexpr uint8_t COUNTER_ENABLED = 0x02;
// flag bits
constexpr uint8_t C_FLAG = 0x80;
constexpr uint8_t A_FLAG = 0x40;
constexpr uint8_t F_FLAG = 0x20;
constexpr uint8_t B_FLAG = 0x10;
// 8243 expander operations
enum { EXPANDER_OP_READ = 0, EXPANDER_OP_WRITE = 1, EXPANDER_OP_OR = 2, EXPANDER_OP_AND = 3 };
} // namespace

Cpu::Cpu(Bus &bus, const Config &config) : m_bus(bus)
{
	m_ramMask = (config.ramSize == 64 ? 64 : config.ramSize == 256 ? 256 : 128) - 1;
	// MAME's device_start: EA defaults high only for ROM-less parts.
	m_ea = config.romSize == 0;
	updateRegptr();
}

void Cpu::reset()
{
	// confirmed from reset description
	m_pc = 0;
	m_psw = m_psw & (C_FLAG | A_FLAG);
	updateRegptr();
	m_f1 = false;
	m_a11 = 0;

	m_tirqEnabled = false;
	m_xirqEnabled = false;
	m_timecountEnabled = 0;
	m_timerFlag = false;

	// confirmed from interrupt logic description
	m_irqInProgress = false;
	m_timerOverflow = false;
	m_irqPolled = false;

	// BUS goes high-impedance (MAME writes 0xff with an empty mask when EA is low): not
	// forwarded unless EA is high.
	m_dbbo = 0xff;
	if (m_ea)
		m_bus.busOut(0xff);

	// port 1 and port 2 are set to input mode
	m_p1 = 0xff;
	m_p2 = 0xff;
	m_bus.portOut(1, m_p1);
	m_bus.portOut(2, m_p2);
	m_icount = 0;
}

void Cpu::pushPcPsw()
{
	const uint8_t sp = m_psw & 0x07;
	ramW(uint8_t(8 + 2 * sp), uint8_t(m_pc));
	ramW(uint8_t(9 + 2 * sp), uint8_t(((m_pc >> 8) & 0x0f) | (m_psw & 0xf0)));
	m_psw = uint8_t((m_psw & 0xf0) | ((sp + 1) & 0x07));
}

void Cpu::pullPcPsw()
{
	const uint8_t sp = (m_psw - 1) & 0x07;
	m_pc = ramR(uint8_t(8 + 2 * sp));
	m_pc |= uint16_t(ramR(uint8_t(9 + 2 * sp)) << 8);
	m_psw = uint8_t(((m_pc >> 8) & 0xf0) | sp);
	m_pc &= m_irqInProgress ? 0x7ff : 0xfff;
	updateRegptr();
}

void Cpu::pullPc()
{
	const uint8_t sp = (m_psw - 1) & 0x07;
	m_pc = ramR(uint8_t(8 + 2 * sp));
	m_pc |= uint16_t(ramR(uint8_t(9 + 2 * sp)) << 8);
	m_pc &= m_irqInProgress ? 0x7ff : 0xfff;
	m_psw = uint8_t((m_psw & 0xf0) | sp);
}

void Cpu::add(uint8_t dat)
{
	const unsigned temp = m_a + dat;
	const unsigned temp4 = (m_a & 0x0f) + (dat & 0x0f);
	m_psw &= ~(C_FLAG | A_FLAG);
	m_psw |= (temp4 << 2) & A_FLAG;
	m_psw |= (temp >> 1) & C_FLAG;
	m_a = uint8_t(temp);
}

void Cpu::addc(uint8_t dat)
{
	const unsigned carryin = (m_psw & C_FLAG) >> 7;
	const unsigned temp = m_a + dat + carryin;
	const unsigned temp4 = (m_a & 0x0f) + (dat & 0x0f) + carryin;
	m_psw &= ~(C_FLAG | A_FLAG);
	m_psw |= (temp4 << 2) & A_FLAG;
	m_psw |= (temp >> 1) & C_FLAG;
	m_a = uint8_t(temp);
}

void Cpu::expander(int operation, int port)
{
	// put opcode on low 4 bits of P2 (overwriting latch)
	m_p2 = uint8_t((m_p2 & 0xf0) | (operation << 2) | (port & 3));
	m_bus.portOut(2, m_p2);
	// generate high-to-low transition on PROG line
	m_bus.prog(0);
	// transfer data on low 4 bits of P2
	if (operation != EXPANDER_OP_READ) {
		m_p2 = uint8_t((m_p2 & 0xf0) | (m_a & 0x0f));
		m_bus.portOut(2, m_p2);
	} else {
		// place P20-P23 in input mode
		m_p2 |= 0x0f;
		m_bus.portOut(2, m_p2);
		// input data to lower 4 bits of A (upper 4 bits are cleared)
		m_a = m_bus.portIn(2) & 0x0f;
	}
	// generate low-to-high transition on PROG line
	m_bus.prog(1);
}

void Cpu::burn(int count)
{
	if (m_timecountEnabled) {
		bool timerover = false;
		// if the timer is enabled, accumulate prescaler cycles
		if (m_timecountEnabled & TIMER_ENABLED) {
			const uint8_t oldtimer = m_timer;
			m_prescaler = uint8_t(m_prescaler + count);
			m_timer = uint8_t(m_timer + (m_prescaler >> 5));
			m_prescaler &= 0x1f;
			timerover = m_timer < oldtimer;
		}
		// if the counter is enabled, poll the T1 test input once for each cycle
		else if (m_timecountEnabled & COUNTER_ENABLED) {
			for (; count > 0; count--, m_icount--, m_totalCycles++) {
				m_t1History = uint8_t((m_t1History << 1) | (m_bus.testIn(1) & 1));
				if ((m_t1History & 3) == 2) {
					if (++m_timer == 0)
						timerover = true;
				}
			}
		}
		// if either source caused a timer overflow, set the flags
		if (timerover) {
			m_timerFlag = true;
			// according to the docs, if an overflow occurs with interrupts disabled, the
			// overflow is not stored
			if (m_tirqEnabled)
				m_timerOverflow = true;
		}
	}
	// (note: if timer counter is enabled, count was already reduced to 0)
	m_icount -= count;
	m_totalCycles += uint64_t(count);
}

void Cpu::checkIrqs()
{
	// if something is in progress, we do nothing
	if (m_irqInProgress)
		return;
	// external interrupts take priority
	if (m_irqState && m_xirqEnabled) {
		burn(2);
		m_irqInProgress = true;
		// force JNI to be taken (hack)
		if (m_irqPolled) {
			m_pc = uint16_t(((m_prevPc + 1) & 0x7ff) | (m_prevPc & 0x800));
			jcc(true);
		}
		// transfer to location 0x03
		call(0x03);
	}
	// timer overflow interrupts follow
	else if (m_timerOverflow && m_tirqEnabled) {
		burn(2);
		m_irqInProgress = true;
		// transfer to location 0x07
		call(0x07);
		// timer overflow flip-flop is reset once taken
		m_timerOverflow = false;
	}
}

int Cpu::run(int cycles)
{
	m_icount = cycles;
	const uint64_t start = m_totalCycles;
	// iterate over remaining cycles, guaranteeing at least one instruction
	do {
		if (m_irqState || m_timerOverflow)
			checkIrqs();
		m_irqPolled = false;
		m_prevPc = m_pc;
		execute(fetch());
	} while (m_icount > 0);
	return int(m_totalCycles - start);
}

void Cpu::execute(uint8_t op)
{
	// r0-r7 by low opcode bits; @R0/@R1 by bit 0.
	const int r = op & 7;
	const int ri = op & 1;
	switch (op) {
	// 00
	case 0x00: burn(1); break;                                   // nop
	case 0x02: burn(2); m_bus.busOut(m_a); break;                // outl_bus_a
	case 0x03: burn(2); add(fetch()); break;                     // add_a_n
	case 0x04: case 0x24: case 0x44: case 0x64: case 0x84: case 0xa4: case 0xc4: case 0xe4:
		burn(2); jmp(uint16_t(fetch() | ((op & 0xe0) << 3))); break; // jmp_n
	case 0x05: burn(1); m_xirqEnabled = true; break;             // en_i
	case 0x07: burn(1); m_a--; break;                            // dec_a
	case 0x08: burn(2); m_a = m_bus.busIn(); break;              // ins_a_bus
	case 0x09: burn(2); m_a = m_bus.portIn(1) & m_p1; break;     // in_a_p1
	case 0x0a: burn(2); m_a = m_bus.portIn(2) & m_p2; break;     // in_a_p2
	case 0x0c: case 0x0d: case 0x0e: case 0x0f:
		burn(2); expander(EXPANDER_OP_READ, 4 + (op & 3)); break; // movd_a_pN
	// 10
	case 0x10: case 0x11: burn(1); ramW(reg(ri), uint8_t(ramR(reg(ri)) + 1)); break; // inc_xrN
	case 0x12: case 0x32: case 0x52: case 0x72: case 0x92: case 0xb2: case 0xd2: case 0xf2:
		burn(2); jcc((m_a & (1 << (op >> 5))) != 0); break;     // jb_N
	case 0x13: burn(2); addc(fetch()); break;                    // adc_a_n
	case 0x14: case 0x34: case 0x54: case 0x74: case 0x94: case 0xb4: case 0xd4: case 0xf4:
		burn(2); call(uint16_t(fetch() | ((op & 0xe0) << 3))); break; // call_n
	case 0x15: burn(1); m_xirqEnabled = false; break;            // dis_i
	case 0x16: burn(2); jcc(m_timerFlag); m_timerFlag = false; break; // jtf
	case 0x17: burn(1); m_a++; break;                            // inc_a
	case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
		burn(1); reg(r)++; break;                                // inc_rN
	// 20
	case 0x20: case 0x21: {                                      // xch_a_xrN
		burn(1);
		const uint8_t tmp = m_a;
		m_a = ramR(reg(ri));
		ramW(reg(ri), tmp);
		break;
	}
	case 0x23: burn(2); m_a = fetch(); break;                    // mov_a_n
	case 0x25: burn(1); m_tirqEnabled = true; break;             // en_tcnti
	case 0x26: burn(2); jcc(m_bus.testIn(0) == 0); break;        // jnt_0
	case 0x27: burn(1); m_a = 0; break;                          // clr_a
	case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f: { // xch_a_rN
		burn(1);
		const uint8_t tmp = m_a;
		m_a = reg(r);
		reg(r) = tmp;
		break;
	}
	// 30
	case 0x30: case 0x31: {                                      // xchd_a_xrN
		burn(1);
		const uint8_t oldram = ramR(reg(ri));
		ramW(reg(ri), uint8_t((oldram & 0xf0) | (m_a & 0x0f)));
		m_a = uint8_t((m_a & 0xf0) | (oldram & 0x0f));
		break;
	}
	case 0x35: burn(1); m_tirqEnabled = false; m_timerOverflow = false; break; // dis_tcnti
	case 0x36: burn(2); jcc(m_bus.testIn(0) != 0); break;        // jt_0
	case 0x37: burn(1); m_a ^= 0xff; break;                      // cpl_a
	case 0x39: burn(2); m_p1 = m_a; m_bus.portOut(1, m_p1); break; // outl_p1_a
	case 0x3a: burn(2); m_p2 = m_a; m_bus.portOut(2, m_p2); break; // outl_p2_a
	case 0x3c: case 0x3d: case 0x3e: case 0x3f:
		burn(2); expander(EXPANDER_OP_WRITE, 4 + (op & 3)); break; // movd_pN_a
	// 40
	case 0x40: case 0x41: burn(1); m_a |= ramR(reg(ri)); break;  // orl_a_xrN
	case 0x42: burn(1); m_a = m_timer; break;                    // mov_a_t
	case 0x43: burn(2); m_a |= fetch(); break;                   // orl_a_n
	case 0x45:                                                   // strt_cnt
		burn(1);
		if (!(m_timecountEnabled & COUNTER_ENABLED))
			m_t1History = uint8_t(m_bus.testIn(1));
		m_timecountEnabled = COUNTER_ENABLED;
		break;
	case 0x46: burn(2); jcc(m_bus.testIn(1) == 0); break;        // jnt_1
	case 0x47: burn(1); m_a = uint8_t((m_a << 4) | (m_a >> 4)); break; // swap_a
	case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
		burn(1); m_a |= reg(r); break;                           // orl_a_rN
	// 50
	case 0x50: case 0x51: burn(1); m_a &= ramR(reg(ri)); break;  // anl_a_xrN
	case 0x53: burn(2); m_a &= fetch(); break;                   // anl_a_n
	case 0x55: burn(1); m_timecountEnabled = TIMER_ENABLED; m_prescaler = 0; break; // strt_t
	case 0x56: burn(2); jcc(m_bus.testIn(1) != 0); break;        // jt_1
	case 0x57:                                                   // da_a
		burn(1);
		if ((m_a & 0x0f) > 0x09 || (m_psw & A_FLAG)) {
			if (m_a > 0xf9)
				m_psw |= C_FLAG;
			m_a = uint8_t(m_a + 0x06);
		}
		if ((m_a & 0xf0) > 0x90 || (m_psw & C_FLAG)) {
			m_a = uint8_t(m_a + 0x60);
			m_psw |= C_FLAG;
		}
		break;
	case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
		burn(1); m_a &= reg(r); break;                           // anl_a_rN
	// 60
	case 0x60: case 0x61: burn(1); add(ramR(reg(ri))); break;    // add_a_xrN
	case 0x62: burn(1); m_timer = m_a; break;                    // mov_t_a
	case 0x65: burn(1); m_timecountEnabled = 0; break;           // stop_tcnt
	case 0x67: {                                                 // rrc_a
		burn(1);
		const uint8_t newc = uint8_t((m_a << 7) & C_FLAG);
		m_a = uint8_t((m_a >> 1) | (m_psw & C_FLAG));
		m_psw = uint8_t((m_psw & ~C_FLAG) | newc);
		break;
	}
	case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
		burn(1); add(reg(r)); break;                             // add_a_rN
	// 70
	case 0x70: case 0x71: burn(1); addc(ramR(reg(ri))); break;   // adc_a_xrN
	case 0x75: burn(1); break;                                   // ent0_clk (no T0 clock consumer)
	case 0x76: burn(2); jcc(m_f1); break;                        // jf1
	case 0x77: burn(1); m_a = uint8_t((m_a >> 1) | (m_a << 7)); break; // rr_a
	case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
		burn(1); addc(reg(r)); break;                            // adc_a_rN
	// 80
	case 0x80: case 0x81: burn(2); m_a = m_bus.extIn(reg(ri)); break; // movx_a_xrN
	case 0x83: burn(2); pullPc(); break;                         // ret
	case 0x85: burn(1); m_psw &= ~F_FLAG; break;                 // clr_f0
	case 0x86: burn(2); m_irqPolled = !m_irqState; jcc(m_irqState); break; // jni
	case 0x88: burn(2); m_bus.busOut(uint8_t(m_bus.busIn() | fetch())); break; // orl_bus_n
	case 0x89: burn(2); m_p1 |= fetch(); m_bus.portOut(1, m_p1); break; // orl_p1_n
	case 0x8a: burn(2); m_p2 |= fetch(); m_bus.portOut(2, m_p2); break; // orl_p2_n
	case 0x8c: case 0x8d: case 0x8e: case 0x8f:
		burn(2); expander(EXPANDER_OP_OR, 4 + (op & 3)); break;  // orld_pN_a
	// 90
	case 0x90: case 0x91: burn(2); m_bus.extOut(reg(ri), m_a); break; // movx_xrN_a
	case 0x93:                                                   // retr
		burn(2);
		// implicitly clear the IRQ in progress flip flop
		m_irqInProgress = false;
		pullPcPsw();
		break;
	case 0x95: burn(1); m_psw ^= F_FLAG; break;                  // cpl_f0
	case 0x96: burn(2); jcc(m_a != 0); break;                    // jnz
	case 0x97: burn(1); m_psw &= ~C_FLAG; break;                 // clr_c
	case 0x98: burn(2); m_bus.busOut(uint8_t(m_bus.busIn() & fetch())); break; // anl_bus_n
	case 0x99: burn(2); m_p1 &= fetch(); m_bus.portOut(1, m_p1); break; // anl_p1_n
	case 0x9a: burn(2); m_p2 &= fetch(); m_bus.portOut(2, m_p2); break; // anl_p2_n
	case 0x9c: case 0x9d: case 0x9e: case 0x9f:
		burn(2); expander(EXPANDER_OP_AND, 4 + (op & 3)); break; // anld_pN_a
	// A0
	case 0xa0: case 0xa1: burn(1); ramW(reg(ri), m_a); break;    // mov_xrN_a
	case 0xa3: burn(2); m_a = programRead(uint16_t((m_pc & 0xf00) | m_a)); break; // movp_a_xa
	case 0xa5: burn(1); m_f1 = false; break;                     // clr_f1
	case 0xa7: burn(1); m_psw ^= C_FLAG; break;                  // cpl_c
	case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
		burn(1); reg(r) = m_a; break;                            // mov_rN_a
	// B0
	case 0xb0: case 0xb1: { burn(2); const uint8_t n = fetch(); ramW(reg(ri), n); break; } // mov_xrN_n
	case 0xb3:                                                   // jmpp_xa
		burn(2);
		m_pc &= 0xf00;
		m_pc |= programRead(uint16_t(m_pc | m_a));
		break;
	case 0xb5: burn(1); m_f1 = !m_f1; break;                     // cpl_f1
	case 0xb6: burn(2); jcc((m_psw & F_FLAG) != 0); break;       // jf0
	case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
		burn(2); reg(r) = fetch(); break;                        // mov_rN_n
	// C0
	case 0xc5: burn(1); m_psw &= ~B_FLAG; updateRegptr(); break; // sel_rb0
	case 0xc6: burn(2); jcc(m_a == 0); break;                    // jz
	case 0xc7: burn(1); m_a = m_psw | 0x08; break;               // mov_a_psw
	case 0xc8: case 0xc9: case 0xca: case 0xcb: case 0xcc: case 0xcd: case 0xce: case 0xcf:
		burn(1); reg(r)--; break;                                // dec_rN
	// D0
	case 0xd0: case 0xd1: burn(1); m_a ^= ramR(reg(ri)); break;  // xrl_a_xrN
	case 0xd3: burn(2); m_a ^= fetch(); break;                   // xrl_a_n
	case 0xd5: burn(1); m_psw |= B_FLAG; updateRegptr(); break;  // sel_rb1
	case 0xd7: burn(1); m_psw = m_a & ~0x08; updateRegptr(); break; // mov_psw_a
	case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf:
		burn(1); m_a ^= reg(r); break;                           // xrl_a_rN
	// E0
	case 0xe3: burn(2); m_a = programRead(uint16_t(0x300 | m_a)); break; // movp3_a_xa
	case 0xe5: burn(1); m_a11 = 0x000; break;                    // sel_mb0
	case 0xe6: burn(2); jcc((m_psw & C_FLAG) == 0); break;       // jnc
	case 0xe7: burn(1); m_a = uint8_t((m_a << 1) | (m_a >> 7)); break; // rl_a
	case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: case 0xed: case 0xee: case 0xef:
		burn(2); jcc(--reg(r) != 0); break;                      // djnz_rN
	// F0
	case 0xf0: case 0xf1: burn(1); m_a = ramR(reg(ri)); break;   // mov_a_xrN
	case 0xf5: burn(1); m_a11 = 0x800; break;                    // sel_mb1
	case 0xf6: burn(2); jcc((m_psw & C_FLAG) != 0); break;       // jc
	case 0xf7: {                                                 // rlc_a
		burn(1);
		const uint8_t newc = m_a & C_FLAG;
		m_a = uint8_t((m_a << 1) | (m_psw >> 7));
		m_psw = uint8_t((m_psw & ~C_FLAG) | newc);
		break;
	}
	case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfc: case 0xfd: case 0xfe: case 0xff:
		burn(1); m_a = reg(r); break;                            // mov_a_rN
	// illegal: 01 06 0B 22 33 38 3B 63 66 73 82 87 8B 9B A2 A6 B7 C0-C3 D6 E0-E2 F3
	default: burn(1); break;
	}
}

void Cpu::save(State &s) const
{
	std::memset(&s, 0, sizeof(s));
	s.totalCycles = m_totalCycles;
	s.prevPc = m_prevPc;
	s.pc = m_pc;
	s.a11 = m_a11;
	s.a = m_a;
	s.psw = m_psw;
	s.p1 = m_p1;
	s.p2 = m_p2;
	s.timer = m_timer;
	s.prescaler = m_prescaler;
	s.t1History = m_t1History;
	s.dbbo = m_dbbo;
	s.f1 = m_f1;
	s.ea = m_ea;
	s.irqState = m_irqState;
	s.irqPolled = m_irqPolled;
	s.irqInProgress = m_irqInProgress;
	s.timerOverflow = m_timerOverflow;
	s.timerFlag = m_timerFlag;
	s.tirqEnabled = m_tirqEnabled;
	s.xirqEnabled = m_xirqEnabled;
	s.timecountEnabled = m_timecountEnabled;
	s.icount = m_icount;
	std::memcpy(s.ram, m_ram, sizeof(m_ram));
}

void Cpu::load(const State &s)
{
	m_totalCycles = s.totalCycles;
	m_prevPc = s.prevPc;
	m_pc = s.pc;
	m_a11 = s.a11;
	m_a = s.a;
	m_psw = s.psw;
	m_p1 = s.p1;
	m_p2 = s.p2;
	m_timer = s.timer;
	m_prescaler = s.prescaler;
	m_t1History = s.t1History;
	m_dbbo = s.dbbo;
	m_f1 = s.f1;
	m_ea = s.ea;
	m_irqState = s.irqState;
	m_irqPolled = s.irqPolled;
	m_irqInProgress = s.irqInProgress;
	m_timerOverflow = s.timerOverflow;
	m_timerFlag = s.timerFlag;
	m_tirqEnabled = s.tirqEnabled;
	m_xirqEnabled = s.xirqEnabled;
	m_timecountEnabled = s.timecountEnabled;
	m_icount = s.icount;
	std::memcpy(m_ram, s.ram, sizeof(m_ram));
	updateRegptr();
}

} // namespace mcs48
