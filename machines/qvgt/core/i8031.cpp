// license:BSD-3-Clause
// copyright-holders:Steve Ellenoff, Manuel Abadia, Couriersud
// The timers, the UART, the interrupt polling and the idle and power-down modes are MAME's
// (src/devices/cpu/mcs51/i8051.cpp, i80c51.cpp), as machines/virusc/core/c515.cpp ports them.
#include "i8031.h"

namespace qvgt {

namespace {
enum : uint8_t { SIO_IDLE, SIO_START_LE, SIO_START, SIO_DATA0, SIO_DATA1, SIO_DATA2, SIO_DATA3, SIO_DATA4, SIO_DATA5, SIO_DATA6, SIO_DATA7, SIO_DATA8, SIO_STOP };
enum : uint8_t { PCON_IDL = 0, PCON_PD = 1 };
enum : uint8_t { IE_A = 7 };
constexpr int kSources = 5; // IE0, TF0, IE1, TF1, RI/TI: vectors 03h + 8 * source
} // namespace

using u8 = uint8_t;
using s8 = int8_t;
using u16 = uint16_t;
using s16 = int16_t;
using u32 = uint32_t;

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "i8031_ops.inc"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

I8031::u8 I8031::p(int port) const
{
	switch (port) {
	case 0: return m_p0;
	case 1: return m_p1;
	case 2: return m_p2;
	default: return m_p3;
	}
}

I8031::u8 I8031::sfrR(u8 a)
{
	switch (a) {
	case 0x80: return m_rwm ? m_p0 : m_p0 & m_bus->portIn(0);
	case 0x81: return m_sp;
	case 0x82: return u8(m_dptr);
	case 0x83: return u8(m_dptr >> 8);
	case 0x87: return m_pcon;
	case 0x88: return m_tcon;
	case 0x89: return m_tmod;
	case 0x8a: return m_tl0;
	case 0x8b: return m_tl1;
	case 0x8c: return m_th0;
	case 0x8d: return m_th1;
	case 0x90: return m_rwm ? m_p1 : m_p1 & m_bus->portIn(1);
	case 0x98: return m_scon;
	case 0x99: return m_sbuf;
	case 0xa0: return m_rwm ? m_p2 : m_p2 & m_bus->portIn(2);
	case 0xa8: return m_ie;
	case 0xb0: return m_rwm ? m_p3 : m_p3 & m_bus->portIn(3);
	case 0xb8: return m_ip;
	case 0xd0: return m_psw;
	case 0xe0: return m_acc;
	case 0xf0: return m_b;
	default: return m_sfrOther[a & 0x7f];
	}
}

void I8031::sfrW(u8 a, u8 d)
{
	switch (a) {
	case 0x80: m_p0 = d; m_bus->portOut(0, d); break;
	case 0x81: m_sp = d; break;
	case 0x82: m_dptr = u16((m_dptr & 0xff00) | d); break;
	case 0x83: m_dptr = u16((m_dptr & 0x00ff) | (d << 8)); break;
	case 0x87: m_pcon = d; break;
	case 0x88: m_tcon = d; break;
	case 0x89: m_tmod = d; break;
	case 0x8a: m_tl0 = d; break;
	case 0x8b: m_tl1 = d; break;
	case 0x8c: m_th0 = d; break;
	case 0x8d: m_th1 = d; break;
	case 0x90: m_p1 = d; m_bus->portOut(1, d); break;
	case 0x98:
		if (!BIT(m_scon, SCON_REN) && BIT(d, SCON_REN))
			m_uart.rxbit = SIO_IDLE;
		m_scon = d;
		break;
	case 0x99:
		m_uart.data_out = d;
		m_uart.txbit = SIO_START;
		break;
	case 0xa0: m_p2 = d; m_bus->portOut(2, d); break;
	case 0xa8: m_ie = d; m_irq_delayed = 0xff; break;
	case 0xb0: m_p3 = d; m_bus->portOut(3, d); break;
	case 0xb8: m_ip = d; m_irq_delayed = 0xff; break;
	case 0xd0: m_psw = u8((m_psw & 0x01) | (d & 0xfe)); break;
	case 0xe0: acc_w(d); break;
	case 0xf0: m_b = d; break;
	default: m_sfrOther[a & 0x7f] = d; break;
	}
}

void I8031::clear_current_irq()
{
	if (m_cur_irq_prio >= 0)
		m_irq_active = u8(m_irq_active & ~(1 << m_cur_irq_prio));
	m_cur_irq_prio = m_irq_active & 2 ? 1 : m_irq_active & 1 ? 0 : -1;
}

void I8031::setInt(int n, bool level)
{
	const int it = n == 0 ? TCON_IT0 : TCON_IT1;
	const int ie = n == 0 ? TCON_IE0 : TCON_IE1;
	if (BIT(m_tcon, it)) {
		if (m_intPin[n] && !level)
			m_tcon = u8(m_tcon | (1 << ie));
	} else
		m_tcon = u8((m_tcon & ~(1 << ie)) | (level ? 0 : 1 << ie));
	m_intPin[n] = level;
}

I8031::u32 I8031::pending_irqs() const
{
	return u32(BIT(m_tcon, TCON_IE0) | (BIT(m_tcon, TCON_TF0) << 1) | (BIT(m_tcon, TCON_IE1) << 2) | (BIT(m_tcon, TCON_TF1) << 3) |
	           ((u32(BIT(m_scon, SCON_RI)) | u32(BIT(m_scon, SCON_TI))) << 4));
}

void I8031::check_irqs()
{
	u32 ints = BIT(m_ie, IE_A) ? pending_irqs() & m_ie : 0;
	// Flags are sampled in one machine cycle and polled in the next; RETI and IE/IP writes inhibit
	// every source for that polling cycle.
	ints &= ~m_irq_delayed;
	m_irq_delayed = 0;
	if (!ints)
		return;

	// any interrupt ends idle mode; an external one ends power-down too (i80c51.cpp)
	m_pcon = u8(m_pcon & ~(1 << PCON_IDL));
	if (ints & (u32(BIT(m_tcon, TCON_IE0)) | (u32(BIT(m_tcon, TCON_IE1)) << 2)))
		m_pcon = u8(m_pcon & ~(1 << PCON_PD));
	if (BIT(m_pcon, PCON_PD))
		return;

	int chosen = -1, priority = -1;
	for (int i = 0; i < kSources; ++i)
		if ((ints >> i) & 1) {
			const int level = BIT(m_ip, i);
			if (level > priority) {
				priority = level;
				chosen = i;
			}
		}
	if (m_irq_active && priority <= m_cur_irq_prio)
		return;

	// MAME's work-around for the polling latency of JB INT0 / JB INT1
	if (chosen == 0 && m_last_op == 0x20 && m_last_bit == 0xb2)
		m_pc = u16(m_ppc + 3);
	else if (chosen == 2 && m_last_op == 0x20 && m_last_bit == 0xb3)
		m_pc = u16(m_ppc + 3);

	push_pc();
	m_pc = u16((chosen << 3) | 3);
	if (chosen >= 0 && chosen < 5)
		++irqEntries[chosen];
	m_cur_irq_prio = priority;
	m_irq_active = u8(m_irq_active | (1 << priority));

	switch (chosen) {
	case 0: if (BIT(m_tcon, TCON_IT0)) set_ie0(false); break;
	case 1: set_tf0(false); break;
	case 2: if (BIT(m_tcon, TCON_IT1)) set_ie1(false); break;
	case 3: set_tf1(false); break;
	default: break; // TI and RI stay set until software clears them
	}
	m_inst_cycles += 2;
}

void I8031::transmit_receive(int source)
{
	const int mode = (int(BIT(m_scon, SCON_SM0)) << 1) | int(BIT(m_scon, SCON_SM1));
	if (source == 1)
		m_uart.smod_div = (m_uart.smod_div + 1) & !BIT(m_pcon, PCON_SMOD);
	if (mode == 0)
		return; // the shift register mode: not used on this board

	if (mode == 2) {
		const int add = source == 0 ? (BIT(m_pcon, PCON_SMOD) ? 6 : 3) : 0;
		m_uart.rx_clk += add;
		m_uart.tx_clk += add;
	} else if (source == 1) {
		m_uart.tx_clk += !m_uart.smod_div;
		m_uart.rx_clk += !m_uart.smod_div;
	}

	if (m_uart.tx_clk >= 16) {
		m_uart.tx_clk &= 0x0f;
		switch (m_uart.txbit) {
		case SIO_IDLE: break;
		case SIO_START: m_uart.txbit = SIO_DATA0; break;
		case SIO_DATA0: case SIO_DATA1: case SIO_DATA2: case SIO_DATA3:
		case SIO_DATA4: case SIO_DATA5: case SIO_DATA6: case SIO_DATA7:
			if (mode == 1 && m_uart.txbit == SIO_DATA7)
				m_uart.txbit = SIO_STOP;
			else
				m_uart.txbit++;
			break;
		case SIO_DATA8: m_uart.txbit = SIO_STOP; break;
		case SIO_STOP:
			m_bus->txByte(m_uart.data_out);
			set_ti(true);
			m_uart.txbit = SIO_IDLE;
			break;
		}
	}

	// Waiting for a start bit on a line that stays high: sampling it changes nothing but the masking
	if (m_uart.rxbit == SIO_START_LE && m_bus->rxIdle()) {
		if (BIT(m_scon, SCON_REN))
			m_uart.rx_clk &= 0x0f;
		return;
	}
	if ((m_uart.rx_clk >= 16 || m_uart.rxbit < SIO_START) && BIT(m_scon, SCON_REN)) {
		m_uart.rx_clk &= 0x0f;
		const int data = m_bus->rxd() ? 1 : 0;
		switch (m_uart.rxbit) {
		case SIO_IDLE:
			if (data)
				m_uart.rxbit = SIO_START_LE;
			break;
		case SIO_START_LE:
			if (!data) {
				m_uart.rxbit = SIO_START;
				m_uart.rx_clk = 8;
			}
			break;
		case SIO_START:
			if (!data) {
				m_uart.data_in = 0;
				m_uart.rxbit = SIO_DATA0;
			} else
				m_uart.rxbit = SIO_START_LE;
			break;
		case SIO_DATA0: case SIO_DATA1: case SIO_DATA2: case SIO_DATA3:
		case SIO_DATA4: case SIO_DATA5: case SIO_DATA6: case SIO_DATA7:
			if (data)
				m_uart.data_in = u8(m_uart.data_in | (1u << (m_uart.rxbit - SIO_DATA0)));
			if (mode == 1 && m_uart.rxbit == SIO_DATA7)
				m_uart.rxbit = SIO_STOP;
			else
				m_uart.rxbit++;
			break;
		case SIO_DATA8:
			m_uart.rxb8 = u8(data);
			m_uart.rxbit = SIO_STOP;
			break;
		case SIO_STOP:
			if (!BIT(m_scon, SCON_RI)) {
				m_sbuf = m_uart.data_in;
				if (mode == 1) {
					if (!BIT(m_scon, SCON_SM2)) {
						set_rb8(data);
						set_ri(true);
					} else if (data)
						set_ri(true);
				} else {
					set_rb8(m_uart.rxb8);
					if (!BIT(m_scon, SCON_SM2) || BIT(m_scon, SCON_RB8))
						set_ri(true);
				}
			}
			m_uart.rxbit = data ? SIO_START_LE : SIO_IDLE;
			break;
		}
	}
}

void I8031::update_timer_t0(int cycles)
{
	const int mode = (int(BIT(m_tmod, TMOD_M0_1)) << 1) | int(BIT(m_tmod, TMOD_M0_0));
	u32 count;
	if (BIT(m_tcon, TCON_TR0) && (!BIT(m_tmod, TMOD_GATE0) || m_intPin[0])) {
		const u32 delta = BIT(m_tmod, TMOD_CT0) ? 0 : u32(cycles);
		switch (mode) {
		case 0:
			count = u32((m_th0 << 5) | (m_tl0 & 0x1f)) + delta;
			if (count & 0xffffe000) set_tf0(true);
			m_th0 = u8(count >> 5);
			m_tl0 = u8(count & 0x1f);
			break;
		case 1:
			count = u32((m_th0 << 8) | m_tl0) + delta;
			if (count & 0xffff0000) set_tf0(true);
			m_th0 = u8(count >> 8);
			m_tl0 = u8(count);
			break;
		case 2:
			count = u32(m_tl0) + delta;
			if (count & 0xffffff00) {
				set_tf0(true);
				count += m_th0;
			}
			m_tl0 = u8(count);
			break;
		case 3:
			count = u32(m_tl0) + delta;
			if (count & 0xffffff00) set_tf0(true);
			m_tl0 = u8(count);
			break;
		}
	}
	if (BIT(m_tcon, TCON_TR1) && mode == 3) {
		count = u32(m_th0) + u32(cycles);
		if (count & 0xffffff00) set_tf1(true);
		m_th0 = u8(count);
	}
}

void I8031::update_timer_t1(int cycles)
{
	const int mode = (int(BIT(m_tmod, TMOD_M1_1)) << 1) | int(BIT(m_tmod, TMOD_M1_0));
	const int mode0 = (int(BIT(m_tmod, TMOD_M0_1)) << 1) | int(BIT(m_tmod, TMOD_M0_0));
	if (mode0 != 3 && !(BIT(m_tcon, TCON_TR1) && (!BIT(m_tmod, TMOD_GATE1) || m_intPin[1])))
		return;
	const u32 delta = (mode0 != 3 && BIT(m_tmod, TMOD_CT1)) ? 0 : u32(cycles);
	u32 count, overflow = 0;
	switch (mode) {
	case 0:
		count = u32((m_th1 << 5) | (m_tl1 & 0x1f)) + delta;
		overflow = count & 0xffffe000;
		m_th1 = u8(count >> 5);
		m_tl1 = u8(count & 0x1f);
		break;
	case 1:
		count = u32((m_th1 << 8) | m_tl1) + delta;
		overflow = count & 0xffff0000;
		m_th1 = u8(count >> 8);
		m_tl1 = u8(count);
		break;
	case 2:
		count = u32(m_tl1) + delta;
		overflow = count & 0xffffff00;
		if (overflow) count += m_th1;
		m_tl1 = u8(count);
		break;
	default: break;
	}
	if (overflow) {
		if (mode0 != 3)
			set_tf1(true);
		transmit_receive(1);
	}
}

void I8031::update_peripherals()
{
	update_timer_t0(1);
	update_timer_t1(1);
	transmit_receive(0);
}

void I8031::burn_cycles(int cycles)
{
	m_icount -= cycles;
	m_cycles += uint64_t(cycles);
	while (cycles--) {
		const u32 before = cycles ? 0 : pending_irqs();
		update_peripherals();
		// remember flags raised in the instruction's last cycle
		if (!cycles)
			m_irq_delayed |= pending_irqs() & ~before;
	}
}

int I8031::run(int machineCycles)
{
	const uint64_t start = m_cycles;
	m_icount = machineCycles;
	do {
		check_irqs();
		if (BIT(m_pcon, PCON_PD)) { // power-down: only an external interrupt wakes it
			burn_cycles(m_icount > 0 ? m_icount : 1);
			break;
		}
		if (!BIT(m_pcon, PCON_IDL)) {
			m_ppc = m_pc;
			const u8 op = code(m_pc++);
			m_inst_cycles += mcs51_cycles[op];
			execute_op(op);
		} else
			m_inst_cycles++;
		burn_cycles(m_inst_cycles);
		m_inst_cycles = 0;
	} while (m_icount > 0);
	return int(m_cycles - start);
}

void I8031::reset()
{
	m_iram.fill(0);
	m_sfrOther.fill(0);
	m_cur_irq_prio = -1;
	m_irq_active = 0;
	m_irq_delayed = 0;
	m_last_op = m_last_bit = 0;
	m_rwm = 0;
	m_ppc = m_pc = 0;
	m_sp = 7;
	m_psw = m_acc = m_b = 0;
	m_dptr = 0;
	m_ie = m_ip = 0;
	m_scon = m_tcon = m_tmod = m_pcon = 0;
	m_intPin[0] = m_intPin[1] = true;
	m_th0 = m_th1 = m_tl0 = m_tl1 = 0;
	m_uart = {};
	m_cycles = 0;
	m_p0 = m_p1 = m_p2 = m_p3 = 0xff;
	for (int i = 0; i < 4; ++i)
		m_bus->portOut(i, 0xff);
}

} // namespace qvgt
