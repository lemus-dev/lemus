// license:BSD-3-Clause
// copyright-holders:AJR
// Ported from MAME (src/devices/cpu/mn1880) by tools/port_mn1880.py; MAME's licence and
// copyright notice are kept above. Changes: MAME device framework replaced (emu.h and the
// interface in mn1880.h); additions inside MAME's code are marked "LEMUS".

#ifndef LEMUS_MN1880_MN1880_H
#define LEMUS_MN1880_MN1880_H

#pragma once

#include "emu.h"

#include <array>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>

// LEMUS: plain-old-data fields into and out of a session snapshot (save_state, load_state)
namespace lemus_state {
template <typename T> inline void put(std::vector<u8> &v, const T &x)
{
	const u8 *b = reinterpret_cast<const u8 *>(&x);
	v.insert(v.end(), b, b + sizeof(T));
}
template <typename T> inline bool get(const u8 *&p, const u8 *end, T &x)
{
	if (size_t(end - p) < sizeof(T))
		return false;
	std::memcpy(static_cast<void *>(&x), p, sizeof(T));
	p += sizeof(T);
	return true;
}
} // namespace lemus_state

// MAME's emu_timer on the CPU's own time line, in oscillator clocks. A timer fires between CPU
// cycles, at the first cycle boundary at or after its expiry, as MAME's scheduler fires it (MAME
// ends the CPU's timeslice when a timer is set to expire before the slice would end).
class emu_timer
{
public:
	emu_timer &adjust(attotime start_delay, s32 param = 0, const attotime &period = attotime::never);
	// Armed to a finite time. (MAME's enabled() is also true after adjust(attotime::never), when
	// remaining() is never; the MN1880023's timer 0 count read would then be meaningless.)
	bool enabled() const { return m_enabled; }
	attotime remaining() const;

private:
	friend class mn1880_device;

	mn1880_device *m_owner = nullptr;
	std::function<void(s32)> m_callback;
	attotime m_expire = attotime::never;
	attotime m_period = attotime::never;
	u64 m_order = 0;  // when it was last (re)armed: MAME's list fires equal expiries in that order
	s32 m_param = 0;
	bool m_enabled = false;
};


// Panasonic MN1880: two CPUs sharing one execution core, MAME's cycle-by-cycle model (see
// mn1880.cpp for what is guessed). A CPU cycle is 4 oscillator clocks. The on-chip registers
// (up to the model's internal I/O end) and internal RAM are handled here; program memory and the
// rest of the data space go through the Bus.
class mn1880_device
{
public:
	static constexpr int clocks_per_cycle = 4;  // MAME's execute_clocks_to_cycles

	struct Bus
	{
		virtual ~Bus() = default;
		virtual u8 read_program(u16 addr) = 0;
		virtual u8 read_data(u16 addr) = 0;
		virtual void write_data(u16 addr, u8 data) = 0;
	};

	// MAME's generic MN1880: only IE0, IE1 and CPUM on chip, no internal RAM.
	mn1880_device();
	virtual ~mn1880_device() = default;
	mn1880_device(const mn1880_device &) = delete;
	mn1880_device &operator=(const mn1880_device &) = delete;

	void set_bus(Bus *bus) { m_bus = bus; }
	// The internal RAM ends here (the model's map gives the default); configure before reset().
	void set_ram_end(u16 end);
	// Resets both CPUs and the peripherals (MAME's device_reset). Call once before run().
	void reset() { device_reset(); }
	// Runs exactly `cycles` CPU cycles and returns them. Timers due by the end have fired.
	int run(int cycles);
	// IRQ request inputs 0-11; a request latches when the input is asserted.
	void set_input_line(int line, int state) { execute_set_input(line, state); }

	// LEMUS: the whole device between two run() calls, for a session (bsrack::Machine::snapshot):
	// both register sets, the RAM, the timers and the peripherals. load_state takes what save_state
	// wrote into a device built the same way; false (and the device unspecified) if it doesn't fit.
	virtual void save_state(std::vector<u8> &out) const;
	virtual bool load_state(const u8 *&p, const u8 *end);

	// CPU cycles since construction: the time line the timers run on.
	u64 total_cycles() const { return local_cycles(); }
	// The same in oscillator clocks, exact also inside a timer's callback (its expiry time).
	u64 time_clocks() const { return local_time().clocks; }
	// State, for the board model and tools
	int active_cpu() const { return BIT(m_cpum, 4); }
	u16 pc() const { return get_active_cpu().irp; }  // instruction the active CPU is executing
	u16 pc(int cpu) const { return m_cpu[cpu & 1].irp; }
	u16 sp(int cpu) const { return m_cpu[cpu & 1].sp; }
	u16 xp(int cpu) const { return m_cpu[cpu & 1].xp; }
	u16 yp(int cpu) const { return m_cpu[cpu & 1].yp; }
	u8 fs(int cpu) const { return m_cpu[cpu & 1].fs; }
	u16 ie(int cpu) const { return m_cpu[cpu & 1].ie; }
	bool iemask(int cpu) const { return m_cpu[cpu & 1].iemask; }
	u8 cpum() const { return m_cpum; }
	u16 irq_requests() const { return m_if; }
	// Internal RAM without side effects (0 outside it)
	u8 ram(u16 addr) const { return addr >= m_ram_start && addr <= m_ram_end ? m_ram[addr - m_ram_start] : 0; }

	// Called with each instruction's address as it starts (MAME's debugger hook; tools only).
	std::function<void(u16)> &trace() { return m_trace; }
	// MAME's logerror messages (unset: dropped).
	std::function<void(const char *)> &log() { return m_log; }
	// Every access to an on-chip register with the byte read or written (tools only).
	std::function<void(u16 addr, u8 data, bool write)> &sfr_hook() { return m_sfr_hook; }

protected:
	mn1880_device(u16 internal_io_end, u16 ram_start, u16 ram_end);

	virtual void device_start();
	virtual void device_reset();
	void execute_run();
	void execute_set_input(int inputnum, int state);

	// The on-chip register map (generated from MAME's map): false when nothing on chip answers,
	// so the access goes to the bus.
	virtual bool internal_r(offs_t offset, u8 &data);
	virtual bool internal_w(offs_t offset, u8 data);

	// on-chip peripheral interrupt requests
	void request_interrupt(int irq) { m_if |= 1U << irq; }
	void clear_interrupt_requests(u16 mask) { m_if &= ~mask; }

	// MAME framework stand-ins
	attotime clocks_to_attotime(u64 clocks) const { return attotime{clocks}; }
	osc_clock clock() const { return osc_clock{}; }
	struct machine_time
	{
		const mn1880_device *dev;
		attotime time() const { return dev->local_time(); }
	};
	machine_time machine() const { return machine_time{this}; }
	template <typename T> emu_timer *timer_alloc(void (T::*callback)(s32), T *object)
	{
		emu_timer &timer = m_timers.at(size_t(m_timer_count++));
		timer.m_owner = this;
		timer.m_callback = [object, callback] (s32 param) { (object->*callback)(param); };
		return &timer;
	}
	void logerror(const char *format, ...) MAME_PRINTF(2, 3);
	void debugger_instruction_hook(offs_t pc) { if (m_trace) m_trace(u16(pc)); }
	int standard_irq_callback(int, offs_t) { return 0; }

private:
	friend class emu_timer;

	struct program_space
	{
		mn1880_device *cpu;
		u8 read_byte(offs_t addr) { return cpu->m_bus->read_program(u16(addr)); }
	};
	struct data_space
	{
		mn1880_device *cpu;
		u8 read_byte(offs_t addr) { return cpu->data_r(u16(addr)); }
		void write_byte(offs_t addr, u8 data) { cpu->data_w(u16(addr), data); }
	};

	u8 data_r(u16 addr);
	void data_w(u16 addr, u8 data);

	// The time line: m_slice_start + cycles done in the current execute_run() slice
	u64 local_cycles() const { return m_slice_start + u64(m_slice_len - m_icount); }
	attotime local_time() const { return m_callback_timer ? m_callback_time : attotime{local_cycles() * clocks_per_cycle}; }
	void timer_armed(const attotime &expire);
	void execute_timers();

	Bus *m_bus = nullptr;
	const u16 m_ram_start;
	u16 m_ram_end = 0;
	std::vector<u8> m_ram;
	std::function<void(u16)> m_trace;
	std::function<void(const char *)> m_log;
	std::function<void(u16, u8, bool)> m_sfr_hook;
	std::array<emu_timer, 16> m_timers;
	int m_timer_count = 0;
	u64 m_slice_start = 0;
	s32 m_slice_len = 0;
	emu_timer *m_callback_timer = nullptr;
	attotime m_callback_time;
	bool m_callback_timer_modified = false;
	u64 m_timer_order = 0;

	// MAME's state
	struct cpu_registers
	{
		cpu_registers() : ip(0), irp(0), ir(0), fs(0), xp(0), yp(0), sp(0), lp(0), wait(0), ie(0), iemask(false) { }

		u8 addcz(u8 data1, u8 data2, bool carry, bool holdz);
		u8 adddcz(u8 data1, u8 data2, bool carry);
		u8 subcz(u8 data1, u8 data2, bool carry, bool holdz);
		u8 subdcz(u8 data1, u8 data2, bool carry);
		u8 rolc(u8 data);
		u8 rorc(u8 data);
		u8 asrc(u8 data);
		void branch(u16 label);

		u16 ip;
		u16 irp;
		u8 ir;
		u8 fs;
		u16 xp;
		u16 yp;
		u16 sp;
		u16 lp;
		u16 wait;
		u16 ie;
		bool iemask;
	};

	enum class microstate : u8 {
		NEXT,
		NOP_1,
		REP_1,
		CLRSET_1, CLRSET_2,
		T1B_1,
		MOVL31_1, MOVL31_2,
		MOVL34_1, MOVL34_2, MOVL34_3, MOVL34_4,
		MOVL35_1, MOVL35_2, MOVL35_3, MOVL35_4,
		MOV36_1, MOV36_2,
		MOV37_1,
		MOVL38_1, MOVL38_2, MOVL38_3,
		MOVL39_1, MOVL39_2, MOVL39_3,
		ASL_1, ASL_2,
		ASR_1, ASR_2,
		DEC40_1,
		NOT_1,
		CMPM44_1, CMPM44_2, CMPM44_3,
		CMPM45_1, CMPM45_2,
		XCH4_1, XCH4_2,
		INC48_1,
		CLR4A_1,
		ROL_1, ROL_2,
		ROR_1, ROR_2,
		CMPM50_1, CMPM50_2, CMPM50_3,
		CMPM52_1, CMPM52_2,
		DIV51_1, DIV51_2, DIV51_3, DIV51_4, DIV51_5, DIV51_6, DIV51_7, DIV51_8, DIV51_9, DIV51_10,
		MOVDA_1, MOVDA_2, MOVDA_3,
		MOV54_1,
		MOV55_1,
		MOV56_1, MOV56_2,
		XCH58_1, XCH58_2, XCH58_3, XCH58_4,
		MUL59_1, MUL59_2, MUL59_3, MUL59_4, MUL59_5, MUL59_6, MUL59_7, MUL59_8,
		MOVL5C_1, MOVL5C_2, MOVL5C_3,
		MOVL5D_1, MOVL5D_2,
		MOVL5E_1, MOVL5E_2, MOVL5E_3, MOVL5E_4,
		MOVL5F_1, MOVL5F_2,
		CMP_1, CMP_2, CMP_3,
		AND_1, AND_2, AND_3,
		XOR_1, XOR_2, XOR_3,
		OR_1, OR_2, OR_3,
		SUBC_1, SUBC_2, SUBC_3,
		SUBD_1, SUBD_2, SUBD_3, SUBD_4,
		ADDC_1, ADDC_2, ADDC_3,
		ADDD_1, ADDD_2, ADDD_3, ADDD_4,
		BR80_1, BR88_1,
		CMPL_1, CMPL_2, CMPL_3, CMPL_4, CMPL_5,
		CLRFS_1, SETFS_1,
		CALL90_1, CALL90_2,
		BRA0_1,
		POPFS_1, POPFS_2,
		POPB4_1, POPB4_2, POPB4_3,
		POPB5_1, POPB5_2,
		POPB7_1,
		PUSHFS_1,
		PUSHBC_1, PUSHBC_2, PUSHBC_3,
		PUSHBD_1, PUSHBD_2, PUSHBD_3,
		PUSHBF_1,
		SUBCL_1, SUBCL_2, SUBCL_3, SUBCL_4, SUBCL_5, SUBCL_6,
		DIVC1_1, DIVC1_2,
		XCHC4_1, XCHC4_2,
		XCHC5_1, XCHC5_2,
		ADDCL_1, ADDCL_2, ADDCL_3, ADDCL_4, ADDCL_5, ADDCL_6,
		MULC9_1, MULC9_2, MULC9_3,
		MOVCC_1,
		MOVCD_1, MOVCD_2,
		CMPD0_1, CMPD0_2,
		CMPD1_1, CMPD1_2,
		XCHD4_1, XCHD4_2,
		XCHD7_1,
		MOVD8_1,
		MOVD9_1,
		MOVDC_1,
		MOVDD_1,
		LOOP_1, LOOP_2,
		DECE4_1,
		INCE5_1,
		ADDRE8_1, ADDRE8_2, ADDRE8_3,
		ADDRE9_1, ADDRE9_2, ADDRE9_3,
		ADDREC_1,
		ADDRED_1,
		CMPBF0_1,
		CMPBF1_1, CMPBF1_2, CMPBF1_3,
		MOV1_1, MOV1_2, MOV1_3, MOV1_4, MOV1N_4,
		WAIT_1, WAIT_2,
		RET_1, RET_2, RET_3,
		RETI_1, RETI_2, RETI_3, RETI_4, RETI_5,
		BR_1, BR_2,
		CALL_1, CALL_2, CALL_3,
		PUSHFB_1,
		BRFC_1,
		CALLFD_1,
		RDTBL_1, RDTBL_2, RDTBL_3,
		PI_1, PI_2, PI_3, PI_4,
		UNKNOWN
	};

	static void setl(u16 &pr, u8 data) { pr = (pr & 0xff00) | data; }
	static void seth(u16 &pr, u8 data) { pr = (pr & 0x00ff) | (data << 8); }

	cpu_registers &get_active_cpu() { return m_cpu[BIT(m_cpum, 4)]; }
	const cpu_registers &get_active_cpu() const { return m_cpu[BIT(m_cpum, 4)]; }
	bool output_queued() const { return m_output_queue_state != 0xff; }
	void set_output_queued() { m_output_queue_state = BIT(m_cpum, 4); }

	offs_t mmu_psen_translate(u16 addr) const { return BIT(m_mmu_enable, 6) && BIT(addr + 0x4000, 15) ? u32(m_mmu_bank[BIT(addr, 15)]) << 14 | (addr & 0x3fff) : addr; }
	offs_t mmu_data_translate(u16 addr) const { return BIT(m_mmu_enable, 7) && BIT(addr + 0x4000, 15) ? u32(m_mmu_bank[BIT(addr, 15) + 2]) << 14 | (addr & 0x3fff) : addr; }

	u8 ie0_r();
	void ie0_w(u8 data);
	u8 ie1_r();
	void ie1_w(u8 data);
	u8 cpum_r();
	void cpum_w(u8 data);

	void swap_cpus();
	void next_instruction(u8 input);

	static const microstate s_decode_map[256];
	static const u16 s_input_queue_map[16];
	static const u8 s_branch_fs[4];

	// address spaces
	program_space m_cache{this};
	data_space m_data{this};
	const u16 m_internal_io_end;

	// execution state
	cpu_registers m_cpu[2];
	u8 m_cpum;
	microstate m_ustate;
	u16 m_da;
	u16 m_tmp1;
	u16 m_tmp2;
	u8 m_output_queue_state;
	s32 m_icount;

	// interrupt state
	u16 m_input_state;
	u16 m_if;
	u16 m_irq;

	// MMU state
	u8 m_mmu_bank[4];
	u8 m_mmu_enable;
};


// MN1880023 (Drum Station, CR-504, PSR-500): MAME's peripherals, register use inferred from
// that firmware.
class mn1880023_device : public mn1880_device
{
public:
	mn1880023_device();

	template <int Port> auto &port_in_cb() { return m_port_in_cb[Port]; }
	template <int Port> auto &port_out_cb() { return m_port_out_cb[Port]; }
	template <int Channel> auto &ad_in_cb() { return m_ad_in_cb[Channel]; }
	template <int N> auto &sio_in_cb() { return m_sio_in_cb[N]; }
	template <int N> auto &sio_out_cb() { return m_sio_out_cb[N]; }
	auto &uart_txd_cb() { return m_uart_txd_cb; }
	auto &pwm_out_cb() { return m_pwm_out_cb; }

	// port direction registers are unknown, so the board names its input pins
	void set_port_input_mask(int port, u8 mask) { m_port_input_mask[port] = mask; }

	void timer2_capture_w(int state);
	void timer4_count_w(int state);
	void uart_rxd_w(int state);

	u16 pwm_reload() const;
	u16 pwm_compare() const;
	bool pwm_output_enabled() const { return BIT(m_regs[0x54], 2); }

	// LEMUS: the whole byte each time the UART transmitter ends a frame (its stop bit)
	devcb_write8 &uart_tx_byte_cb() { return m_uart_tx_byte_cb; }
	// LEMUS: sends bytes into RXD as a sender on the line would: whole frames (start bit, 8 data
	// bits LSB first, stop bit) at the UART's own bit period, back to back, the first now. They
	// go through MAME's receiver (uart_rxd_w), so a byte sent while the UART is off is lost.
	void uart_rx_byte(u8 data);
	size_t uart_rx_queued() const { return m_rx_queue.size() + (m_rx_line_bit ? 1 : 0); }
	// LEMUS: the register file as last written or set by the peripherals (tools only)
	u8 reg(int offset) const { return m_regs[offset % 0x60]; }
	// LEMUS: inferred from v1.5 (see mn1880.cpp): timer 1's output pin, toggled at each overflow
	devcb_write_line &timer1_out_cb() { return m_timer1_out_cb; }
	// LEMUS: the register whose bits 0-2 pick the ADC channel: 0x1D in MAME (Drum Station), 0x1E
	// in the Bass Station Rack's v1.5
	void set_adc_channel_register(u8 reg) { m_adc_channel_register = reg & 0x3f; }
	void save_state(std::vector<u8> &out) const override; // LEMUS
	bool load_state(const u8 *&p, const u8 *end) override;
	// LEMUS: oscillator clocks per count of timers 0 and 1 in control mode 14: 16, as MAME's timers
	// 0 and 3. Inferred from v1.5's period table (ROM 0200: 61156 counts = MIDI note 0 at 500 kHz),
	// with which MIDI note 60 plays 261.6 Hz at 8 MHz; 4 would put it two octaves up.
	static constexpr int TIMER01_CLOCKS = 16;

protected:
	void device_start() override;
	void device_reset() override;
	bool internal_r(offs_t offset, u8 &data) override;
	bool internal_w(offs_t offset, u8 data) override;

private:
	enum
	{
		IRQ_TIMER0 = 3,
		IRQ_PWM = 4,
		IRQ_TIMER3 = 6,
		IRQ_TIMER4 = 8,
		IRQ_UART_RX = 9
	};

	u8 reg_r(offs_t offset);
	void reg_w(offs_t offset, u8 data);
	u8 port_r(offs_t offset);
	void port_w(offs_t offset, u8 data);
	void irq_flags_w(u8 data);
	void sio0_control_w(u8 data);
	void sio0_data_w(u8 data);
	void sio1_data_w(u8 data);
	void adc_control_w(u8 data);
	u8 timer0_r(offs_t offset);
	void timer0_w(offs_t offset, u8 data);
	u8 timer2_r(offs_t offset);
	void timer2_w(offs_t offset, u8 data);
	void timer3_w(offs_t offset, u8 data);
	void timer_run_w(u8 data);
	void timer_control_w(u8 data);
	void timer3_control_w(u8 data);
	u8 timer4_r(offs_t offset);
	void pwm_reload_w(offs_t offset, u8 data);
	void pwm_control_w(u8 data);

	void update_timer0();
	void update_timer3();
	void update_pwm();
	u16 timer2_count() const;
	bool uart_enabled() const { return m_regs[0x19] == 0x62; }
	attotime uart_bit_period() const;
	void uart_txd(int state);

	TIMER_CALLBACK_MEMBER(timer0_tick);
	TIMER_CALLBACK_MEMBER(timer3_tick);
	TIMER_CALLBACK_MEMBER(sio0_tick);
	TIMER_CALLBACK_MEMBER(sio1_tick);
	TIMER_CALLBACK_MEMBER(uart_tx_tick);
	TIMER_CALLBACK_MEMBER(uart_rx_tick);
	TIMER_CALLBACK_MEMBER(adc_tick);
	TIMER_CALLBACK_MEMBER(pwm_tick);

	devcb_read8::array<10> m_port_in_cb;
	devcb_write8::array<10> m_port_out_cb;
	devcb_read8::array<8> m_ad_in_cb;
	devcb_read8::array<2> m_sio_in_cb;
	devcb_write8::array<2> m_sio_out_cb;
	devcb_write_line m_uart_txd_cb;
	devcb_write_line m_pwm_out_cb;

	emu_timer *m_timer0;
	emu_timer *m_timer3;
	emu_timer *m_sio_timer[2];
	emu_timer *m_uart_tx_timer;
	emu_timer *m_uart_rx_timer;
	emu_timer *m_adc_timer;
	emu_timer *m_pwm_timer;

	u8 m_port_input_mask[10];
	u8 m_regs[0x60];
	attotime m_timer2_start;
	u16 m_timer4_count;
	u8 m_timer4_prescale;
	bool m_timer2_capture_input;
	bool m_timer4_count_input;
	u8 m_uart_tx_data;
	u8 m_uart_rx_data;
	u8 m_uart_tx_bits;
	u8 m_uart_rx_bits;
	bool m_uart_rxd;
	bool m_uart_txd;

	// LEMUS
	static constexpr int IRQ_TIMER1 = 4;  // inferred from v1.5: vector 000C -> 77EF
	void update_timer1();
	void timer1_tick(s32 param);
	emu_timer *m_timer1 = nullptr;
	bool m_timer0_reloading = false;
	bool m_timer1_out = false;
	devcb_write_line m_timer1_out_cb;
	u8 m_adc_channel_register = 0x1d;
	void rx_line_tick(s32 param);
	devcb_write8 m_uart_tx_byte_cb;
	u8 m_uart_tx_byte = 0;
	emu_timer *m_rx_line_timer = nullptr;
	std::deque<u8> m_rx_queue;
	u8 m_rx_line_byte = 0;
	u8 m_rx_line_bit = 0;
};

#endif // LEMUS_MN1880_MN1880_H
