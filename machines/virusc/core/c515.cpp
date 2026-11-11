// license:BSD-3-Clause
// copyright-holders:Steve Ellenoff, Manuel Abadia, Couriersud
// The timers, the UART and the interrupt polling are MAME's (src/devices/cpu/mcs51/i8051.cpp). The
// interrupt system's sources and priorities, timer 2, the A/D converter and ports 4 and 5 follow
// the C515 user's manual (c515_um.pdf, chapters 6 and 7).
#include "c515.h"

namespace virusc {

namespace {
enum : uint8_t { SIO_IDLE, SIO_START_LE, SIO_START, SIO_DATA0, SIO_DATA1, SIO_DATA2, SIO_DATA3, SIO_DATA4, SIO_DATA5, SIO_DATA6, SIO_DATA7, SIO_DATA8, SIO_STOP };

constexpr int kAdcCycles = 13; // machine cycles of a conversion

// In polling order (manual, table 7-1): each group's left source first, the groups top to bottom.
struct Source {
	uint8_t group, vector;
	uint8_t enableBit; // in IEN0 (bit 0-5) or IEN1 (bit 8-13)
};
constexpr Source kSources[12] = {
	{0, 0x03, 0},  // external 0
	{0, 0x43, 8},  // A/D converter
	{1, 0x0b, 1},  // timer 0
	{1, 0x4b, 9},  // external 2
	{2, 0x13, 2},  // external 1
	{2, 0x53, 10}, // external 3
	{3, 0x1b, 3},  // timer 1
	{3, 0x5b, 11}, // external 4
	{4, 0x23, 4},  // serial
	{4, 0x63, 12}, // external 5
	{5, 0x2b, 5},  // timer 2
	{5, 0x6b, 13}, // external 6
};
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
#include "c515_ops.inc"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

C515::u8 C515::p(int port) const
{
	switch (port) {
	case 0: return m_p0;
	case 1: return m_p1;
	case 2: return m_p2;
	case 3: return m_p3;
	case 4: return m_p4;
	default: return m_p5;
	}
}

C515::u8 C515::sfrR(u8 a)
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
	case 0xa8: return m_ien0;
	case 0xa9: return m_ip0;
	case 0xb0: return m_rwm ? m_p3 : m_p3 & m_bus->portIn(3);
	case 0xb8: return m_ien1;
	case 0xb9: return m_ip1;
	case 0xc0: return m_ircon;
	case 0xc8: return m_t2con;
	case 0xca: return m_crcl;
	case 0xcb: return m_crch;
	case 0xcc: return m_tl2;
	case 0xcd: return m_th2;
	case 0xd0: return m_psw;
	case 0xd8: return m_adcon;
	case 0xd9: return m_addat;
	case 0xda: return m_dapr;
	case 0xe0: return m_acc;
	case 0xe8: return m_rwm ? m_p4 : m_p4 & m_bus->portIn(4);
	case 0xf0: return m_b;
	case 0xf8: return m_rwm ? m_p5 : m_p5 & m_bus->portIn(5);
	default: return m_sfrOther[a & 0x7f];
	}
}

void C515::sfrW(u8 a, u8 d)
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
	case 0xa8: m_ien0 = d; m_irq_delayed = 0xfff; break;
	case 0xa9: m_ip0 = d; m_irq_delayed = 0xfff; break;
	case 0xb0: m_p3 = d; m_bus->portOut(3, d); break;
	case 0xb8: m_ien1 = d; m_irq_delayed = 0xfff; break;
	case 0xb9: m_ip1 = d; m_irq_delayed = 0xfff; break;
	case 0xc0: m_ircon = d; break;
	case 0xc8: m_t2con = d; break;
	case 0xca: m_crcl = d; break;
	case 0xcb: m_crch = d; break;
	case 0xcc: m_tl2 = d; break;
	case 0xcd: m_th2 = d; break;
	case 0xd0: m_psw = u8((m_psw & 0x01) | (d & 0xfe)); break;
	case 0xd8: m_adcon = u8((m_adcon & (1 << ADCON_BSY)) | (d & ~(1 << ADCON_BSY))); break;
	case 0xda: // a write to DAPR starts a conversion (6.4.1)
		m_dapr = d;
		m_adcBusy = kAdcCycles;
		m_adcon |= 1 << ADCON_BSY;
		break;
	case 0xe0: acc_w(d); break;
	case 0xe8: m_p4 = d; m_bus->portOut(4, d); break;
	case 0xf0: m_b = d; break;
	case 0xf8: m_p5 = d; m_bus->portOut(5, d); break;
	default: m_sfrOther[a & 0x7f] = d; break;
	}
}

void C515::clear_current_irq()
{
	if (m_cur_irq_prio >= 0)
		m_irq_active = u8(m_irq_active & ~(1 << m_cur_irq_prio));
	m_cur_irq_prio = -1;
	for (int level = 3; level >= 0; --level)
		if (m_irq_active & (1 << level)) {
			m_cur_irq_prio = level;
			break;
		}
}

void C515::setInt0(bool level)
{
	if (BIT(m_tcon, TCON_IT0)) {
		if (m_int0 && !level)
			set_ie0(true);
	} else
		set_ie0(!level);
	m_int0 = level;
}

C515::u32 C515::pending_irqs() const
{
	u32 p = 0;
	if (BIT(m_tcon, TCON_IE0)) p |= 1 << 0;
	if (BIT(m_ircon, IRCON_IADC)) p |= 1 << 1;
	if (BIT(m_tcon, TCON_TF0)) p |= 1 << 2;
	if (BIT(m_ircon, 1)) p |= 1 << 3;
	if (BIT(m_tcon, TCON_IE1)) p |= 1 << 4;
	if (BIT(m_ircon, 2)) p |= 1 << 5;
	if (BIT(m_tcon, TCON_TF1)) p |= 1 << 6;
	if (BIT(m_ircon, 3)) p |= 1 << 7;
	if (BIT(m_scon, SCON_RI) || BIT(m_scon, SCON_TI)) p |= 1 << 8;
	if (BIT(m_ircon, 4)) p |= 1 << 9;
	if (BIT(m_ircon, IRCON_TF2) || BIT(m_ircon, IRCON_EXF2)) p |= 1 << 10;
	if (BIT(m_ircon, 5)) p |= 1 << 11;
	return p;
}

void C515::check_irqs()
{
	const u32 pending = pending_irqs();
	u32 ready = 0;
	if (BIT(m_ien0, 7))
		for (int i = 0; i < 12; ++i)
			if ((pending >> i) & 1) {
				const int bit = kSources[i].enableBit;
				if (bit < 8 ? BIT(m_ien0, bit) : BIT(m_ien1, bit - 8))
					ready |= 1u << i;
			}
	ready &= ~m_irq_delayed;
	m_irq_delayed = 0;
	if (!ready)
		return;

	int chosen = -1, priority = -1;
	for (int i = 0; i < 12; ++i)
		if ((ready >> i) & 1) {
			const int group = kSources[i].group;
			const int level = (BIT(m_ip1, group) << 1) | int(BIT(m_ip0, group));
			if (level > priority) {
				priority = level;
				chosen = i;
			}
		}
	if (m_irq_active && priority <= m_cur_irq_prio)
		return;

	push_pc();
	m_pc = kSources[chosen].vector;
	m_cur_irq_prio = priority;
	m_irq_active = u8(m_irq_active | (1 << priority));

	switch (chosen) {
	case 0: if (BIT(m_tcon, TCON_IT0)) set_ie0(false); break;
	case 2: set_tf0(false); break;
	case 4: if (BIT(m_tcon, TCON_IT1)) set_ie1(false); break;
	case 6: set_tf1(false); break;
	case 1: case 8: case 10: break; // these flags are cleared by software
	default: m_ircon = u8(m_ircon & ~(1 << (chosen == 3 ? 1 : chosen == 5 ? 2 : chosen == 7 ? 3 : chosen == 9 ? 4 : 5))); break;
	}
	m_inst_cycles += 2;
}

void C515::transmit_receive(int source)
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

void C515::update_timer_t0(int cycles)
{
	const int mode = (int(BIT(m_tmod, TMOD_M0_1)) << 1) | int(BIT(m_tmod, TMOD_M0_0));
	u32 count;
	if (BIT(m_tcon, TCON_TR0)) {
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

void C515::update_timer_t1(int cycles)
{
	const int mode = (int(BIT(m_tmod, TMOD_M1_1)) << 1) | int(BIT(m_tmod, TMOD_M1_0));
	const int mode0 = (int(BIT(m_tmod, TMOD_M0_1)) << 1) | int(BIT(m_tmod, TMOD_M0_0));
	if (mode0 != 3 && !BIT(m_tcon, TCON_TR1))
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

// Timer 2 (6.2.2): T2I1,T2I0 = 01 counts machine cycles (every second one with T2PS). The external
// and gated inputs aren't wired here. T2R1,T2R0 = 10 reloads from CRC on overflow.
void C515::update_timer_t2()
{
	if ((m_t2con & 3) != 1)
		return;
	if (BIT(m_t2con, 7)) {
		m_t2Prescale = !m_t2Prescale;
		if (m_t2Prescale)
			return;
	}
	if (++m_tl2 == 0 && ++m_th2 == 0) {
		m_ircon |= 1 << IRCON_TF2;
		if ((m_t2con & 0x18) == 0x10) {
			m_tl2 = m_crcl;
			m_th2 = m_crch;
		}
	}
}

void C515::update_adc()
{
	if (m_adcBusy > 0 && --m_adcBusy == 0) {
		m_addat = m_bus->analogIn(m_adcon & 7);
		m_ircon |= 1 << IRCON_IADC;
		if (BIT(m_adcon, ADCON_ADM))
			m_adcBusy = kAdcCycles;
		else
			m_adcon = u8(m_adcon & ~(1 << ADCON_BSY));
	}
}

void C515::update_peripherals()
{
	update_timer_t0(1);
	update_timer_t1(1);
	update_timer_t2();
	update_adc();
	transmit_receive(0);
}

void C515::burn_cycles(int cycles)
{
	m_icount -= cycles;
	m_cycles += uint64_t(cycles);
	while (cycles--) {
		const u32 before = cycles ? 0 : pending_irqs();
		update_peripherals();
		if (!cycles)
			m_irq_delayed |= pending_irqs() & ~before;
	}
}

int C515::run(int machineCycles)
{
	const uint64_t start = m_cycles;
	m_icount = machineCycles;
	do {
		check_irqs();
		m_ppc = m_pc;
		const u8 op = code(m_pc++);
		m_inst_cycles += mcs51_cycles[op];
		execute_op(op);
		burn_cycles(m_inst_cycles);
		m_inst_cycles = 0;
	} while (m_icount > 0);
	return int(m_cycles - start);
}

void C515::reset()
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
	m_ien0 = m_ien1 = m_ip0 = m_ip1 = m_ircon = m_t2con = 0;
	m_scon = m_tcon = m_tmod = m_pcon = 0;
	m_int0 = true;
	m_th0 = m_th1 = m_tl0 = m_tl1 = m_th2 = m_tl2 = m_crcl = m_crch = 0;
	m_adcon = m_addat = m_dapr = 0;
	m_adcBusy = 0;
	m_t2Prescale = false;
	m_uart = {};
	m_cycles = 0;
	m_p0 = m_p1 = m_p2 = m_p3 = m_p4 = m_p5 = 0xff;
	for (int i = 0; i < 6; ++i)
		m_bus->portOut(i, 0xff);
}

} // namespace virusc
