// license:BSD-3-Clause
// copyright-holders:AJR
// Ported from MAME (src/devices/cpu/mn1880) by tools/port_mn1880.py; MAME's licence and
// copyright notice are kept above. Changes: MAME device framework replaced (emu.h and the
// interface in mn1880.h); additions inside MAME's code are marked "LEMUS".
/***************************************************************************

    Panasonic MN1880 series

    This is a cycle-by-cycle emulation of the "8-Bit Dual Microcomputer"
    architecture, which has two independent CPUs sharing the same memory
    spaces and execution core, with execution phases interleaved when both
    CPUs are operating. Pipelining permits operand fetches to overlap with
    data writes.

    Known issues:
    * Only a generic device type and two specific models have been
      provided. Though the broad functional descriptions of many actual
      models are known, more detailed documentation is not easy to find.
    * Few internal special function registers have been emulated, again
      owing to lack of documentation. The MN1880023 and MN18801A
      peripherals are emulated only as far as CR-504, Drum Station and
      PSR-500 firmware uses them, and their register assignments are
      inferred from it.
    * Instruction timings are based mostly on those documented for the
      MN1870, which lacks a number of MN1880 instructions and differs in
      some other important ways. Many have been guessed at.
    * Some instruction behavior, especially for repeated cases, has been
      guessed at and may not be strictly correct. It seems possible that
      many of the more complicated instructions are not supposed to be
      repeatable.
    * Interrupt behavior, including vectoring, has also been guessed at,
      though the specific sources of each interrupt remain largely
      unidentified. Logical interrupt request inputs are provided.
      Separate interrupt enable registers have been provided for each CPU,
      though these seem to share the same internal addresses. The WAIT
      operand is treated as a temporary wakeup mask; only 02 00 has been
      seen. Repeated instructions are fully
      interruptible. Interrupt acknowledgment has been implemented as a
      special invocation of the PI instruction; however, PI usage is
      nonexistent in extant code.
    * The output queue has been implemented only for memory writes, even
      though the MN1870 documentation shows it as applicable for
      instructions that do none of those. It may also be used to
      pipeline updates to XP, YP, SP or FS.
    * Every cycle fetches a byte from the instruction space, whether it
      is needed for execution or not. This fetching may not happen quite
      so continuously on actual hardware (a few dummy fetches may be
      unavoidable), but it is more or less continuous on some other
      microcontrollers with Harvard-like architectures such as MCS-51.
    * Data writes occur simultaneously with program fetches. Harvard
      architecture makes instruction and data spaces independent from the
      code's perspective, but simultaneous access is obviously impossible
      when both addresses are external since there is at most one external
      address bus. Contention should slow prefetching and execution down.
    * Additional wait states for external memory, if any, are not emulated.
    * The LP register likely defines some sort of stack limit. This has not
      been implemented.
    * When execution is stopped in the debugger, IP already points to the
      byte following the opcode which has been loaded into IR. This at
      least seems consistent with the prefetch model and the handling of
      repeated instructions.
    * There is no way to focus on one of the two CPUs in the debugger when
      both are executing. This is not an issue on systems that simply
      disable CPUb from the start.
    * The debugger will not single-step through repeated instructions.
      Making MAME's context-insensitive disassembler produce any sensible
      output for these would be very difficult.
    * When the debugger is stopped after an instruction, its execution may
      have loaded the output queue but not emptied it yet. Examining the
      contents of locations about to be written to may show misleading
      values. Likewise, PCs at which watchpoint hits occur may be
      incorrectly reported for writes.
    * Due to the pipelining of writes, an interrupt may be accepted during
      an instruction which attempts to disable interrupts by setting the
      IEMASK bit, which may be cleared instead when control reaches the next
      sequential instruction from a RETI. This sequencing glitch is
      documented in the MN187XX23 user's manual, along with a failsafe way
      of setting IEMASK, and similar workarounds in extant program code
      suggest it is likewise present in the MN1880 series.
    * The optional MMU, which expands the memory spaces in certain models
      which contain neither internal ROM nor RAM, has been emulated only
      to the extent required by psr500, though it likely has a few other
      features and quirks.
    * In DF mode, MOV (XP),(da) apparently takes the high byte of the
      direct address from YP, unlike the MN1870 documentation. MOV (da),(YP)
      also uses YP, as documented for the MN1870.

***************************************************************************/

#include "mn1880.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <iterator>
#include <utility>

mn1880_device::mn1880_device(u16 internal_io_end, u16 ram_start, u16 ram_end)
	: m_ram_start(ram_start)
	, m_internal_io_end(internal_io_end)
	, m_cpum(0)
	, m_ustate(microstate::UNKNOWN)
	, m_da(0)
	, m_tmp1(0)
	, m_tmp2(0)
	, m_output_queue_state(0xff)
	, m_icount(0)
	, m_input_state(0)
	, m_if(0)
	, m_irq(0)
	, m_mmu_bank{0, 0, 0, 0}
	, m_mmu_enable(0)
{
	set_ram_end(ram_end);
}

mn1880_device::mn1880_device()
	: mn1880_device(0x16, 0x0060, 0x005f)
{
	device_start();
}

// requests latch on assertion; deasserting the input does not clear them
void mn1880_device::execute_set_input(int inputnum, int state)
{
	if (inputnum >= 0 && inputnum < 12)
	{
		const u16 mask = 1U << inputnum;
		if (state != CLEAR_LINE)
		{
			if (!(m_input_state & mask))
				m_if |= mask;
			m_input_state |= mask;
		}
		else
		{
			m_input_state &= ~mask;
		}
	}
}

u8 mn1880_device::ie0_r()
{
	return (get_active_cpu().ie & 0x00f) | (m_cpu[0].iemask ? 0x10 : 0) | (m_cpu[1].iemask ? 0x20 : 0);
}

void mn1880_device::ie0_w(u8 data)
{
	cpu_registers &cpu = output_queued() ? m_cpu[m_output_queue_state] : get_active_cpu();
	cpu.ie = (cpu.ie & 0xff0) | (data & 0x0f);
	m_cpu[0].iemask = BIT(data, 4);
	m_cpu[1].iemask = BIT(data, 5);
}

u8 mn1880_device::ie1_r()
{
	return (get_active_cpu().ie & 0xff0) >> 4;
}

void mn1880_device::ie1_w(u8 data)
{
	cpu_registers &cpu = output_queued() ? m_cpu[m_output_queue_state] : get_active_cpu();
	cpu.ie = u16(data) << 4 | (cpu.ie & 0x00f);
}

u8 mn1880_device::cpum_r()
{
	return m_cpum;
}

void mn1880_device::cpum_w(u8 data)
{
	m_cpum = (data & 0xef) | (m_cpum & 0x10);

	// clearing a stop bit also ends WAIT
	for (int i = 0; i < 2; ++i)
	{
		if (!BIT(data, i))
			m_cpu[i].wait = 0;
	}
}

void mn1880_device::device_reset()
{
	for (cpu_registers &cpu : m_cpu)
	{
		cpu.fs &= 0xc0; // CF & ZF might or might not be cleared as well
		cpu.fs |= 0x10; // HACK: skip fake first instruction in debugger
		cpu.ir = 0xf6;
		cpu.wait = 0;
		cpu.ie = 0;
		cpu.iemask = true;
	}
	m_cpu[0].ip = 0x0000;
	m_cpu[1].ip = 0x0020;

	// TBD: exactly what are SP and LP initialized to for each CPU?
	m_cpu[0].sp = 0x0100;
	m_cpu[0].lp = 0x0060;
	m_cpu[1].sp = 0x0200;
	m_cpu[1].lp = 0x0160;

	m_if = 0;
	m_irq = 0;
	m_cpum = 0x0c;
	m_ustate = microstate::NEXT;
	m_output_queue_state = 0xff;

	m_mmu_enable = 0;
}

const mn1880_device::microstate mn1880_device::s_decode_map[256] =
{
	microstate::NOP_1, microstate::REP_1, microstate::REP_1, microstate::REP_1, microstate::REP_1, microstate::REP_1, microstate::REP_1, microstate::REP_1,
	microstate::REP_1, microstate::REP_1, microstate::REP_1, microstate::REP_1, microstate::REP_1, microstate::REP_1, microstate::REP_1, microstate::REP_1,
	microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1,
	microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1, microstate::CLRSET_1,
	microstate::T1B_1, microstate::T1B_1, microstate::T1B_1, microstate::T1B_1, microstate::T1B_1, microstate::T1B_1, microstate::T1B_1, microstate::T1B_1,
	microstate::T1B_1, microstate::T1B_1, microstate::T1B_1, microstate::T1B_1, microstate::T1B_1, microstate::T1B_1, microstate::T1B_1, microstate::T1B_1,
	microstate::UNKNOWN, microstate::MOVL31_1, microstate::UNKNOWN, microstate::MOVL31_1, microstate::MOVL34_1, microstate::MOVL35_1, microstate::MOV36_1, microstate::MOV37_1,
	microstate::MOVL38_1, microstate::MOVL39_1, microstate::MOVL38_1, microstate::MOVL39_1, microstate::ASL_1, microstate::ASL_1, microstate::ASR_1, microstate::ASR_1,
	microstate::DEC40_1, microstate::DEC40_1, microstate::NOT_1, microstate::NOT_1, microstate::CMPM44_1, microstate::CMPM45_1, microstate::XCH4_1, microstate::XCH4_1,
	microstate::INC48_1, microstate::INC48_1, microstate::CLR4A_1, microstate::CLR4A_1, microstate::ROL_1, microstate::ROL_1, microstate::ROR_1, microstate::ROR_1,
	microstate::CMPM50_1, microstate::DIV51_1, microstate::CMPM52_1, microstate::MOVDA_1, microstate::MOV54_1, microstate::MOV55_1, microstate::MOV56_1, microstate::MOV56_1,
	microstate::XCH58_1, microstate::MUL59_1, microstate::XCH58_1, microstate::UNKNOWN, microstate::MOVL5C_1, microstate::MOVL5D_1, microstate::MOVL5E_1, microstate::MOVL5F_1,
	microstate::CMP_1, microstate::CMP_1, microstate::CMP_1, microstate::CMP_1, microstate::AND_1, microstate::AND_1, microstate::AND_1, microstate::AND_1,
	microstate::XOR_1, microstate::XOR_1, microstate::XOR_1, microstate::XOR_1, microstate::OR_1, microstate::OR_1, microstate::OR_1, microstate::OR_1,
	microstate::SUBC_1, microstate::SUBC_1, microstate::SUBC_1, microstate::SUBC_1, microstate::SUBD_1, microstate::SUBD_1, microstate::SUBD_1, microstate::SUBD_1,
	microstate::ADDC_1, microstate::ADDC_1, microstate::ADDC_1, microstate::ADDC_1, microstate::ADDD_1, microstate::ADDD_1, microstate::ADDD_1, microstate::ADDD_1,
	microstate::BR80_1, microstate::BR80_1, microstate::BR80_1, microstate::BR80_1, microstate::CMPL_1, microstate::CLRFS_1, microstate::CMPL_1, microstate::CLRFS_1,
	microstate::BR88_1, microstate::BR88_1, microstate::BR88_1, microstate::BR88_1, microstate::UNKNOWN, microstate::SETFS_1, microstate::UNKNOWN, microstate::SETFS_1,
	microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1,
	microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1, microstate::CALL90_1,
	microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1,
	microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1, microstate::BRA0_1,
	microstate::UNKNOWN, microstate::POPFS_1, microstate::UNKNOWN, microstate::POPFS_1, microstate::POPB4_1, microstate::POPB5_1, microstate::POPB4_1, microstate::POPB7_1,
	microstate::UNKNOWN, microstate::PUSHFS_1, microstate::UNKNOWN, microstate::PUSHFS_1, microstate::PUSHBC_1, microstate::PUSHBD_1, microstate::PUSHBC_1, microstate::PUSHBF_1,
	microstate::SUBCL_1, microstate::DIVC1_1, microstate::SUBCL_1, microstate::UNKNOWN, microstate::XCHC4_1, microstate::XCHC5_1, microstate::XCHC4_1, microstate::XCHC5_1,
	microstate::ADDCL_1, microstate::MULC9_1, microstate::ADDCL_1, microstate::UNKNOWN, microstate::MOVCC_1, microstate::MOVCD_1, microstate::MOVCC_1, microstate::MOVCD_1,
	microstate::CMPD0_1, microstate::CMPD1_1, microstate::CMPD0_1, microstate::CMPD1_1, microstate::XCHD4_1, microstate::XCHD4_1, microstate::UNKNOWN, microstate::XCHD7_1,
	microstate::MOVD8_1, microstate::MOVD9_1, microstate::MOVD8_1, microstate::MOVD9_1, microstate::MOVDC_1, microstate::MOVDD_1, microstate::MOVDC_1, microstate::MOVDD_1,
	microstate::LOOP_1, microstate::LOOP_1, microstate::LOOP_1, microstate::LOOP_1, microstate::DECE4_1, microstate::INCE5_1, microstate::DECE4_1, microstate::INCE5_1,
	microstate::ADDRE8_1, microstate::ADDRE9_1, microstate::ADDRE8_1, microstate::ADDRE9_1, microstate::ADDREC_1, microstate::ADDRED_1, microstate::ADDREC_1, microstate::ADDRED_1,
	microstate::CMPBF0_1, microstate::CMPBF1_1, microstate::MOV1_1, microstate::WAIT_1, microstate::RET_1, microstate::RETI_1, microstate::BR_1, microstate::CALL_1,
	microstate::CMPBF0_1, microstate::CMPBF1_1, microstate::MOV1_1, microstate::PUSHFB_1, microstate::BRFC_1, microstate::CALLFD_1, microstate::RDTBL_1, microstate::PI_1
};

const u16 mn1880_device::s_input_queue_map[16] =
{
	//FEDCBA9876543210
	0b0000000000000000, // 0x
	0b1111111111111111, // 1x
	0b1111111111111111, // 2x
	0b1010010101111010, // 3x
	0b1010101010111010, // 4x
	0b1110010011101101, // 5x
	0b1110111011101110, // 6x
	0b1110111011101110, // 7x
	0b0000111101001111, // 8x
	0b0000000000000000, // 9x
	0b1111111111111111, // Ax
	0b0000000000000000, // Bx
	0b1111111010101110, // Cx
	0b0000111100001111, // Dx
	0b1010111100000000, // Ex
	0b0000111101001111  // Fx
};

const u8 mn1880_device::s_branch_fs[4] =
{
	0x00, 0x80, 0x40, 0xc0
};

u8 mn1880_device::cpu_registers::addcz(u8 data1, u8 data2, bool carry, bool holdz)
{
	if ((data1 + data2 + (carry ? 1 : 0)) >= 0x100)
		fs |= 0x80;
	else
		fs &= 0x7f;
	data1 += data2 + (carry ? 1 : 0);
	if (u8(data1) != 0)
		fs &= 0xbf;
	else if (!holdz)
		fs |= 0x40;
	return data1;
}

// 4-bit decimal add with carry (without adjust)
u8 mn1880_device::cpu_registers::adddcz(u8 data1, u8 data2, bool carry)
{
	if (((data1 & 0x0f) + (data2 & 0x0f) + (carry ? 1 : 0)) >= 0x0a)
		fs |= 0x80;
	else
		fs &= 0x7f;
	data1 = (data1 & 0xf0) | ((data1 + data2 + (carry ? 1 : 0)) & 0x0f);
	if (u8(data1 & 0x0f) == 0)
		fs |= 0x40;
	else
		fs &= 0xbf;
	return data1;
}

u8 mn1880_device::cpu_registers::subcz(u8 data1, u8 data2, bool carry, bool holdz)
{
	if (data1 < data2 + (carry ? 1 : 0))
		fs |= 0x80;
	else
		fs &= 0x7f;
	data1 -= data2 + (carry ? 1 : 0);
	if (u8(data1) != 0)
		fs &= 0xbf;
	else if (!holdz)
		fs |= 0x40;
	return data1;
}

// 4-bit decimal subtract with carry (without adjust)
u8 mn1880_device::cpu_registers::subdcz(u8 data1, u8 data2, bool carry)
{
	if ((data1 & 0x0f) < (data2 & 0x0f) + (carry ? 1 : 0))
		fs |= 0x80;
	else
		fs &= 0x7f;
	data1 = (data1 & 0xf0) | ((data1 - data2 - (carry ? 1 : 0)) & 0x0f);
	if (u8(data1 & 0x0f) == 0)
		fs |= 0x40;
	else
		fs &= 0xbf;
	return data1;
}

u8 mn1880_device::cpu_registers::rolc(u8 data)
{
	if (BIT(fs, 7))
	{
		if (!BIT(data, 7))
			fs &= 0x7f;
		return (data << 1) | 0x01;
	}
	else
	{
		if (BIT(data, 7))
			fs |= 0x80;
		return data << 1;
	}
}

u8 mn1880_device::cpu_registers::rorc(u8 data)
{
	if (BIT(fs, 7))
	{
		if (!BIT(data, 0))
			fs &= 0x7f;
		return (data >> 1) | 0x80;
	}
	else
	{
		if (BIT(data, 0))
			fs |= 0x80;
		return data >> 1;
	}
}

u8 mn1880_device::cpu_registers::asrc(u8 data)
{
	if (BIT(data, 0))
		fs |= 0x80;
	else
		fs &= 0x7f;
	return (data >> 1) | (data & 0x80);
}

void mn1880_device::cpu_registers::branch(u16 label)
{
	ip = label;
	fs &= 0xe0;
}

void mn1880_device::swap_cpus()
{
	if (!BIT(m_cpum, BIT(m_cpum, 4) ^ 1))
		m_cpum ^= 0x10;
}

void mn1880_device::next_instruction(u8 input)
{
	cpu_registers &cpu = get_active_cpu();
	if ((cpu.fs & 0x0f) != 0)
		cpu.fs = (cpu.fs - 1) | 0x10;
	else
	{
		cpu.fs &= 0xe0;
		cpu.irp = cpu.ip++;
		cpu.ir = input;
	}

	m_ustate = microstate::NEXT;
	swap_cpus();
}

void mn1880_device::execute_run()
{
	while (m_icount > 0)
	{
		--m_icount;

		// retire the queued write before sampling interrupts and CPUM
		const bool output_cycle = m_ustate == microstate::NEXT && output_queued();
		if (output_cycle)
		{
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1 & 0x00ff);
			m_output_queue_state = 0xff;
		}

		// an enabled interrupt wakes a stopped context
		for (int i = 0; i < 2; ++i)
		{
			if (!m_cpu[i].iemask && ((m_cpu[i].ie | m_cpu[i].wait) & m_if))
				m_cpum &= ~(1U << i);
		}

		if (m_ustate == microstate::NEXT && BIT(m_cpum, BIT(m_cpum, 4)))
		{
			swap_cpus();
			if (BIT(m_cpum, BIT(m_cpum, 4)))
				continue;
		}

		cpu_registers &cpu = get_active_cpu();

		if (m_ustate == microstate::NEXT)
		{
			m_irq = cpu.iemask ? 0 : (cpu.ie | cpu.wait) & m_if;
			if (m_irq != 0)
			{
				cpu.wait = 0;
				// REP has already fetched its opcode, so IP stays on the operand
				if (!(cpu.fs & 0x1f))
					--cpu.ip;
				if (!output_cycle)
					m_ustate = microstate::PI_1;
			}
			else if (cpu.wait == 0)
			{
				if (!BIT(cpu.fs, 4))
					debugger_instruction_hook(cpu.irp);
				if (!output_cycle && !BIT(s_input_queue_map[cpu.ir >> 4], cpu.ir & 0x0f))
					m_ustate = s_decode_map[cpu.ir];
			}
		}

		u8 input = m_cache.read_byte(mmu_psen_translate(cpu.ip));
		switch (m_ustate)
		{
		case microstate::NEXT:
			if (cpu.wait != 0)
			{
				swap_cpus();
			}
			else
			{
				if (m_irq != 0)
					m_ustate = microstate::PI_1;
				else
				{
					if (BIT(s_input_queue_map[cpu.ir >> 4], cpu.ir & 0x0f))
					{
						++cpu.ip;
						m_da = input;
					}
					m_ustate = s_decode_map[cpu.ir];
				}
			}
			break;

		case microstate::NOP_1:
			next_instruction(input);
			break;

		case microstate::REP_1:
			cpu.fs = (cpu.fs & 0xe0) | (cpu.ir & 0x0f);
			cpu.irp = cpu.ip++;
			cpu.ir = input;
			m_ustate = microstate::NEXT;
			break;

		case microstate::CLRSET_1:
			if (BIT(cpu.fs, 5))
				m_da |= cpu.xp & 0xff00;
			if (m_da > m_internal_io_end)
				m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_ustate = microstate::CLRSET_2;
			break;

		case microstate::CLRSET_2:
			// on-chip registers are read and written in the same cycle (inferred)
			if (m_da <= m_internal_io_end)
				m_tmp1 = m_data.read_byte(mmu_data_translate(m_da)); // TODO: read latch instead of terminal
			if (BIT(cpu.ir, 3))
				m_tmp1 |= 1 << (cpu.ir & 0x07);
			else
				m_tmp1 &= ~(1 << (cpu.ir & 0x07));
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1);
			next_instruction(input);
			break;

		case microstate::T1B_1:
			++cpu.ip;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.xp & 0xff00;
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da)) & (1 << (cpu.ir & 0x07));
			m_tmp2 = cpu.ip + s8(input);
			m_ustate = microstate::CMPBF1_3;
			break;

		case microstate::MOVL31_1:
			++cpu.ip;
			m_tmp1 = (m_da & 0xff) | u16(input) << 8;
			m_ustate = microstate::MOVL31_2;
			break;

		case microstate::MOVL31_2:
			if (BIT(cpu.ir, 1))
				cpu.yp = m_tmp1;
			else
				cpu.xp = m_tmp1;
			next_instruction(input);
			break;

		case microstate::MOVL34_1:
			if (BIT(cpu.fs, 5))
				m_da |= cpu.yp & 0xff00;
			m_ustate = microstate::MOVL34_2;
			break;

		case microstate::MOVL34_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_tmp2 = m_da + 1;
			m_da = cpu.xp;
			m_ustate = microstate::MOVL34_3;
			break;

		case microstate::MOVL34_3:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1);
			m_da = m_tmp2;
			++cpu.xp;
			m_ustate = microstate::MOVL34_4;
			break;

		case microstate::MOVL34_4:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_da = cpu.xp;
			if ((cpu.fs & 0x1f) != 0)
				++cpu.xp;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::MOVL35_1:
			if (BIT(cpu.fs, 5))
				m_da |= cpu.xp & 0xff00;
			m_tmp2 = m_da;
			m_da = cpu.yp;
			m_ustate = microstate::MOVL35_2;
			break;

		case microstate::MOVL35_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_da = m_tmp2;
			++cpu.yp;
			m_ustate = microstate::MOVL35_3;
			break;

		case microstate::MOVL35_3:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1);
			m_da = cpu.yp;
			m_ustate = microstate::MOVL35_4;
			break;

		case microstate::MOVL35_4:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_da = m_tmp2 + 1;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::MOV36_1:
			if (BIT(cpu.fs, 5))
				m_da |= cpu.yp & 0xff00;
			m_ustate = microstate::MOV36_2;
			break;

		case microstate::MOV36_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_da = cpu.xp;
			if ((cpu.fs & 0x1f) != 0)
				++cpu.xp;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::MOV37_1:
			++cpu.ip;
			m_da = cpu.yp;
			if ((cpu.fs & 0x1f) != 0)
				++cpu.yp;
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_da = input;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.yp & 0xff00;
			m_ustate = microstate::MOV56_2;
			break;

		case microstate::MOVL38_1:
			if (BIT(cpu.fs, 5))
				m_da |= (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0xff00;
			m_ustate = microstate::MOVL38_2;
			break;

		case microstate::MOVL38_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			++m_da;
			m_ustate = microstate::MOVL38_3;
			break;

		case microstate::MOVL38_3:
			m_tmp1 |= m_data.read_byte(mmu_data_translate(m_da)) << 8;
			m_ustate = microstate::MOVL31_2; // TODO: output queue
			break;

		case microstate::MOVL39_1:
			++cpu.ip;
			m_da = input;
			if (BIT(cpu.fs, 5))
				m_da |= (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0xff00;
			m_ustate = microstate::MOVL39_2;
			break;

		case microstate::MOVL39_2:
			m_tmp1 = BIT(cpu.ir, 1) ? cpu.yp : cpu.xp;
			m_ustate = microstate::MOVL39_3;
			break;

		case microstate::MOVL39_3:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1 & 0x00ff);
			++m_da;
			m_tmp1 >>= 8;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::ASL_1:
			if (BIT(cpu.ir, 0))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
				m_da = cpu.xp;
			m_ustate = microstate::ASL_2;
			break;

		case microstate::ASL_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da)); // TODO: read latch instead of terminal
			cpu.fs = (m_tmp1 & 0x80) | (cpu.fs & 0x7f);
			m_tmp1 <<= 1;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::ASR_1:
			if (BIT(cpu.ir, 0))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
				m_da = cpu.xp;
			m_ustate = microstate::ASR_2;
			break;

		case microstate::ASR_2:
			m_tmp1 = cpu.asrc(m_data.read_byte(mmu_data_translate(m_da))); // TODO: read latch instead of terminal
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::DEC40_1:
			if (BIT(cpu.ir, 0))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_tmp1 = 0;
			if (!BIT(cpu.fs, 4))
				cpu.fs |= 0x80;
			m_ustate = microstate::SUBC_3;
			break;

		case microstate::NOT_1:
			if (BIT(cpu.ir, 0))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
				m_da = cpu.xp;
			m_tmp1 = 0xff;
			m_ustate = microstate::XOR_3;
			break;

		case microstate::CMPM44_1:
			m_tmp1 = m_da & 0x00ff;
			m_da = cpu.xp;
			m_ustate = microstate::CMPM44_2;
			break;

		case microstate::CMPM44_2:
			++cpu.ip;
			m_tmp1 &= m_data.read_byte(mmu_data_translate(m_da));
			m_tmp2 = input;
			m_ustate = microstate::CMPM44_3;
			break;

		case microstate::CMPM44_3:
			(void)cpu.subcz(m_tmp1, m_tmp2, false, false);
			m_ustate = microstate::NOP_1; // TODO: output queue (but what?)
			break;

		case microstate::CMPM45_1:
			if (BIT(cpu.fs, 5))
				m_da |= cpu.xp & 0xff00;
			m_ustate = microstate::CMPM45_2;
			break;

		case microstate::CMPM45_2:
			++cpu.ip;
			m_tmp1 = input;
			m_ustate = microstate::CMPM44_2;
			break;

		case microstate::CMPM50_1:
			m_tmp2 = m_data.read_byte(mmu_data_translate(cpu.yp));
			m_tmp1 = m_da & 0x00ff;
			m_da = cpu.xp;
			m_ustate = microstate::CMPM50_2;
			break;

		case microstate::CMPM50_2:
			m_tmp2 &= m_tmp1;
			m_ustate = microstate::CMPM50_3;
			break;

		case microstate::CMPM50_3:
			m_tmp1 &= m_data.read_byte(mmu_data_translate(m_da));
			m_ustate = microstate::CMPM44_3;
			break;

		case microstate::CMPM52_1:
			++cpu.ip;
			m_tmp1 = input;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.yp & 0xff00;
			m_ustate = microstate::CMPM50_2;
			break;

		case microstate::CMPM52_2:
			++cpu.ip;
			m_tmp2 = m_data.read_byte(mmu_data_translate(m_da));
			m_da = input;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.xp & 0xff00;
			m_ustate = microstate::CMPM50_2;
			break;

		case microstate::XCH4_1:
			if (BIT(cpu.ir, 0))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_ustate = microstate::XCH4_2;
			break;

		case microstate::XCH4_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da)); // TODO: read latch instead of terminal
			m_tmp1 = (m_tmp1 << 4) | (m_tmp1 >> 4);
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::INC48_1:
			if (BIT(cpu.ir, 0))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_tmp1 = 0;
			if (!BIT(cpu.fs, 4))
				cpu.fs |= 0x80;
			m_ustate = microstate::ADDC_3;
			break;

		case microstate::CLR4A_1:
			if (BIT(cpu.ir, 0))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_tmp1 = 0;
			m_ustate = microstate::AND_3;
			break;

		case microstate::ROL_1:
			if (BIT(cpu.ir, 0))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_ustate = microstate::ROL_2;
			break;

		case microstate::ROL_2:
			m_tmp1 = cpu.rolc(m_data.read_byte(mmu_data_translate(m_da))); // TODO: read latch instead of terminal
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::ROR_1:
			if (BIT(cpu.ir, 0))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_ustate = microstate::ROR_2;
			break;

		case microstate::ROR_2:
			m_tmp1 = cpu.rorc(m_data.read_byte(mmu_data_translate(m_da))); // TODO: read latch instead of terminal
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::DIV51_1:
			m_da = cpu.yp;
			m_ustate = microstate::DIV51_2;
			break;

		case microstate::DIV51_2:
			m_tmp2 = m_data.read_byte(mmu_data_translate(m_da));
			m_da = cpu.xp;
			m_ustate = microstate::DIV51_3;
			break;

		case microstate::DIV51_3:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			++m_da;
			m_ustate = microstate::DIV51_4;
			break;

		case microstate::DIV51_4:
			m_tmp1 |= m_data.read_byte(mmu_data_translate(m_da)) << 8;
			--m_da;
			m_ustate = microstate::DIV51_5;
			break;

		case microstate::DIV51_5:
			m_ustate = microstate::DIV51_6;
			break;

		case microstate::DIV51_6:
			m_ustate = microstate::DIV51_7;
			break;

		case microstate::DIV51_7:
			m_ustate = microstate::DIV51_8;
			break;

		case microstate::DIV51_8:
			m_ustate = microstate::DIV51_9;
			break;

		case microstate::DIV51_9:
			if (m_tmp2 == 0)
			{
				// TBD: documentation doesn't define what happens when the divisor is zero
				logerror("%04X: %04X divided by zero (XP = %04X, YP = %04X)\n", cpu.irp, m_tmp1, cpu.xp, cpu.yp);
				m_tmp1 = 0xff;
				m_tmp2 = 0xff;
			}
			else
				m_tmp1 /= std::exchange(m_tmp2, m_tmp1 % m_tmp2);
			m_ustate = microstate::DIV51_10;
			break;

		case microstate::DIV51_10:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1 & 0x00ff);
			m_tmp1 = m_tmp2; // remainder follows the quotient
			++m_da;
			cpu.xp = m_da;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::MOVDA_1:
			++cpu.ip;
			m_da = (m_da << 8) | input;
			m_ustate = microstate::MOVDA_2;
			break;

		case microstate::MOVDA_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			++cpu.ip;
			m_da = input;
			m_ustate = microstate::MOVDA_3;
			break;

		case microstate::MOVDA_3:
			++cpu.ip;
			m_da = (m_da << 8) | input;
			m_ustate = microstate::MOV56_2;
			break;

		case microstate::MOV54_1:
			m_da = cpu.yp;
			if ((cpu.fs & 0x1f) != 0)
				++cpu.yp;
			m_ustate = microstate::MOV36_2;
			break;

		case microstate::MOV55_1:
			m_data.write_byte(mmu_data_translate(cpu.xp), m_da & 0x00ff);
			if ((cpu.fs & 0x1f) != 0)
				++cpu.xp;
			next_instruction(input);
			break;

		case microstate::MOV56_1:
			++cpu.ip;
			if (BIT(cpu.ir, 0))
				m_tmp1 = m_da & 0x00ff;
			else
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.yp & 0xff00;
				m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			}
			m_da = input;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.xp & 0xff00;
			m_ustate = microstate::MOV56_2;
			break;

		case microstate::MOV56_2:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1);
			next_instruction(input);
			break;

		case microstate::XCH58_1:
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
				m_tmp2 = input;
			}
			else
			{
				m_da = cpu.yp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.yp;
			}
			m_ustate = microstate::XCH58_2;
			break;

		case microstate::XCH58_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da)); // TODO: read latch instead of terminal
			if (BIT(cpu.ir, 1))
			{
				std::swap(m_da, m_tmp2);
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_tmp2 = cpu.yp;
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_ustate = microstate::XCH58_3;
			break;

		case microstate::XCH58_3:
			m_tmp1 |= u16(m_data.read_byte(mmu_data_translate(m_da))) << 8; // TODO: read latch instead of terminal
			m_ustate = microstate::XCH58_4;
			break;

		case microstate::XCH58_4:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1 & 0x00ff);
			m_tmp1 >>= 8;
			m_da = m_tmp2;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::MUL59_1:
			m_da = cpu.xp;
			m_ustate = microstate::MUL59_2;
			break;

		case microstate::MUL59_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			++m_da;
			m_ustate = microstate::MUL59_3;
			break;

		case microstate::MUL59_3:
			m_tmp2 = m_data.read_byte(mmu_data_translate(m_da));
			m_ustate = microstate::MUL59_4;
			break;

		case microstate::MUL59_4:
			m_ustate = microstate::MUL59_5;
			break;

		case microstate::MUL59_5:
			m_ustate = microstate::MUL59_6;
			break;

		case microstate::MUL59_6:
			m_ustate = microstate::MUL59_7;
			break;

		case microstate::MUL59_7:
			m_tmp1 *= m_tmp2;
			m_da = cpu.xp;
			++cpu.xp;
			m_ustate = microstate::MUL59_8;
			break;

		case microstate::MUL59_8:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1 & 0x00ff);
			m_da = cpu.xp;
			if ((cpu.fs & 0x1f) != 0)
				++cpu.xp;
			m_tmp1 >>= 8;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::MOVL5C_1:
			m_da = cpu.yp;
			++cpu.yp;
			m_ustate = microstate::MOVL5C_2;
			break;

		case microstate::MOVL5C_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_da = cpu.xp;
			++cpu.xp;
			m_ustate = microstate::MOVL5C_3;
			break;

		case microstate::MOVL5C_3:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1);
			m_da = cpu.yp;
			if ((cpu.fs & 0x1f) != 0)
				++cpu.yp;
			m_ustate = microstate::MOVL34_4;
			break;

		case microstate::MOVL5D_1:
			++cpu.ip;
			m_data.write_byte(mmu_data_translate(cpu.xp), m_da & 0x00ff);
			++cpu.xp;
			m_tmp1 = input;
			m_ustate = microstate::MOVL5D_2;
			break;

		case microstate::MOVL5D_2:
			m_data.write_byte(mmu_data_translate(cpu.xp), m_tmp1);
			if ((cpu.fs & 0x1f) != 0)
				++cpu.xp;
			next_instruction(input);
			break;

		case microstate::MOVL5E_1:
			++cpu.ip;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.yp & 0xff00;
			m_tmp2 = input;
			if (BIT(cpu.fs, 5))
				m_tmp2 |= cpu.xp & 0xff00;
			m_ustate = microstate::MOVL5E_2;
			break;

		case microstate::MOVL5E_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			std::swap(m_da, m_tmp2);
			m_ustate = microstate::MOVL5E_3;
			break;

		case microstate::MOVL5E_3:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1);
			std::swap(m_da, m_tmp2);
			++m_da;
			m_ustate = microstate::MOVL5E_4;
			break;

		case microstate::MOVL5E_4:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_da = m_tmp2 + 1;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::MOVL5F_1:
			++cpu.ip;
			m_tmp1 = m_da & 0x00ff;
			m_da = input;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.xp & 0xff00;
			m_ustate = microstate::MOVL5F_2;
			break;

		case microstate::MOVL5F_2:
			++cpu.ip;
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1);
			m_tmp1 = input;
			++m_da;
			m_ustate = microstate::MOV56_2;
			break;

		case microstate::CMP_1:
			if (BIT(cpu.ir, 0))
				m_tmp1 = m_da & 0x00ff;
			else
			{
				if (BIT(cpu.ir, 1))
				{
					if (BIT(cpu.fs, 5))
						m_da |= cpu.yp & 0xff00;
				}
				else
				{
					m_da = cpu.yp;
					if ((cpu.fs & 0x1f) != 0)
						++cpu.yp;
				}
			}
			m_ustate = microstate::CMP_2;
			break;

		case microstate::CMP_2:
			if (!BIT(cpu.ir, 0))
				m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				m_da = input;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
				m_da = cpu.xp;
			}
			m_ustate = microstate::CMP_3;
			if (!BIT(cpu.fs, 4))
				cpu.fs &= 0x7f;
			break;

		case microstate::CMP_3:
			(void)cpu.subcz(m_data.read_byte(mmu_data_translate(m_da)), m_tmp1, BIT(cpu.fs, 7), BIT(cpu.fs, 4));
			m_ustate = microstate::NOP_1; // TODO: output queue (but just what is the output?)
			break;

		case microstate::AND_1:
			if (BIT(cpu.ir, 0))
				m_tmp1 = m_da & 0x00ff;
			else
			{
				if (BIT(cpu.ir, 1))
				{
					if (BIT(cpu.fs, 5))
						m_da |= cpu.yp & 0xff00;
				}
				else
				{
					m_da = cpu.yp;
					if ((cpu.fs & 0x1f) != 0)
						++cpu.yp;
				}
			}
			m_ustate = microstate::AND_2;
			break;

		case microstate::AND_2:
			if (!BIT(cpu.ir, 0))
				m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				m_da = input;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_ustate = microstate::AND_3;
			break;

		case microstate::AND_3:
			m_tmp1 &= m_data.read_byte(mmu_data_translate(m_da)); // TODO: read latch instead of terminal
			if (u8(m_tmp1) == 0)
				cpu.fs |= 0x40;
			else
				cpu.fs &= 0xbf;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::XOR_1:
			if (BIT(cpu.ir, 0))
				m_tmp1 = m_da & 0x00ff;
			else
			{
				if (BIT(cpu.ir, 1))
				{
					if (BIT(cpu.fs, 5))
						m_da |= cpu.yp & 0xff00;
				}
				else
				{
					m_da = cpu.yp;
					if ((cpu.fs & 0x1f) != 0)
						++cpu.yp;
				}
			}
			m_ustate = microstate::XOR_2;
			break;

		case microstate::XOR_2:
			if (!BIT(cpu.ir, 0))
				m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				m_da = input;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_ustate = microstate::XOR_3;
			break;

		case microstate::XOR_3:
			m_tmp1 ^= m_data.read_byte(mmu_data_translate(m_da)); // TODO: read latch instead of terminal
			if (u8(m_tmp1) == 0)
				cpu.fs |= 0x40;
			else
				cpu.fs &= 0xbf;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::OR_1:
			if (BIT(cpu.ir, 0))
				m_tmp1 = m_da & 0x00ff;
			else
			{
				if (BIT(cpu.ir, 1))
				{
					if (BIT(cpu.fs, 5))
						m_da |= cpu.yp & 0xff00;
				}
				else
				{
					m_da = cpu.yp;
					if ((cpu.fs & 0x1f) != 0)
						++cpu.yp;
				}
			}
			m_ustate = microstate::OR_2;
			break;

		case microstate::OR_2:
			if (!BIT(cpu.ir, 0))
				m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				m_da = input;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_ustate = microstate::OR_3;
			break;

		case microstate::OR_3:
			m_tmp1 |= m_data.read_byte(mmu_data_translate(m_da)); // TODO: read latch instead of terminal
			if (u8(m_tmp1) == 0)
				cpu.fs |= 0x40;
			else
				cpu.fs &= 0xbf;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::SUBC_1:
			if (BIT(cpu.ir, 0))
				m_tmp1 = m_da & 0x00ff;
			else
			{
				if (BIT(cpu.ir, 1))
				{
					if (BIT(cpu.fs, 5))
						m_da |= cpu.yp & 0xff00;
				}
				else
				{
					m_da = cpu.yp;
					if ((cpu.fs & 0x1f) != 0)
						++cpu.yp;
				}
			}
			m_ustate = microstate::SUBC_2;
			break;

		case microstate::SUBC_2:
			if (!BIT(cpu.ir, 0))
				m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				m_da = input;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_ustate = microstate::SUBC_3;
			break;

		case microstate::SUBC_3:
			m_tmp1 = cpu.subcz(m_data.read_byte(mmu_data_translate(m_da)), m_tmp1, BIT(cpu.fs, 7), BIT(cpu.fs, 4)); // TODO: read latch instead of terminal
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::SUBD_1:
			if (BIT(cpu.ir, 0))
				m_tmp1 = m_da & 0x00ff;
			else
			{
				if (BIT(cpu.ir, 1))
				{
					if (BIT(cpu.fs, 5))
						m_da |= cpu.yp & 0xff00;
				}
				else
				{
					m_da = cpu.yp;
					if ((cpu.fs & 0x1f) != 0)
						++cpu.yp;
				}
			}
			m_ustate = microstate::SUBD_2;
			break;

		case microstate::SUBD_2:
			if (!BIT(cpu.ir, 0))
				m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				m_da = input;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_ustate = microstate::SUBD_3;
			break;

		case microstate::SUBD_3:
			m_tmp1 = cpu.subdcz(m_data.read_byte(mmu_data_translate(m_da)), m_tmp1, BIT(cpu.fs, 7)); // TODO: read latch instead of terminal
			m_ustate = microstate::SUBD_4;
			break;

		case microstate::SUBD_4:
			// Decimal adjust
			if (BIT(cpu.fs, 7))
			{
				m_tmp1 = (m_tmp1 & 0xf0) | ((m_tmp1 - 0x06) & 0x0f);
				if ((m_tmp1 & 0x0f) == 0)
					cpu.fs |= 0x40;
				else
					cpu.fs &= 0xbf;
			}
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::ADDC_1:
			if (BIT(cpu.ir, 0))
				m_tmp1 = m_da & 0x00ff;
			else
			{
				if (BIT(cpu.ir, 1))
				{
					if (BIT(cpu.fs, 5))
						m_da |= cpu.yp & 0xff00;
				}
				else
				{
					m_da = cpu.yp;
					if ((cpu.fs & 0x1f) != 0)
						++cpu.yp;
				}
			}
			m_ustate = microstate::ADDC_2;
			break;

		case microstate::ADDC_2:
			if (!BIT(cpu.ir, 0))
				m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				m_da = input;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_ustate = microstate::ADDC_3;
			break;

		case microstate::ADDC_3:
			m_tmp1 = cpu.addcz(m_data.read_byte(mmu_data_translate(m_da)), m_tmp1, BIT(cpu.fs, 7), BIT(cpu.fs, 4)); // TODO: read latch instead of terminal
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::ADDD_1:
			if (BIT(cpu.ir, 0))
				m_tmp1 = m_da & 0x00ff;
			else
			{
				if (BIT(cpu.ir, 1))
				{
					if (BIT(cpu.fs, 5))
						m_da |= cpu.yp & 0xff00;
				}
				else
				{
					m_da = cpu.yp;
					if ((cpu.fs & 0x1f) != 0)
						++cpu.yp;
				}
			}
			m_ustate = microstate::ADDD_2;
			break;

		case microstate::ADDD_2:
			if (!BIT(cpu.ir, 0))
				m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				m_da = input;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
			{
				m_da = cpu.xp;
				if ((cpu.fs & 0x1f) != 0)
					++cpu.xp;
			}
			m_ustate = microstate::ADDD_3;
			break;

		case microstate::ADDD_3:
			m_tmp1 = cpu.adddcz(m_data.read_byte(mmu_data_translate(m_da)), m_tmp1, BIT(cpu.fs, 7)); // TODO: read latch instead of terminal
			m_ustate = microstate::ADDD_4;
			break;

		case microstate::ADDD_4:
			// Decimal adjust
			if (BIT(cpu.fs, 7))
			{
				m_tmp1 = (m_tmp1 & 0xf0) | ((m_tmp1 + 0x06) & 0x0f);
				if ((m_tmp1 & 0x0f) == 0)
					cpu.fs |= 0x40;
				else
					cpu.fs &= 0xbf;
			}
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::BR80_1:
			if ((cpu.fs & s_branch_fs[cpu.ir & 0x03]) != 0)
				cpu.branch(cpu.ip + s8(m_da & 0x00ff));
			m_ustate = microstate::BR_2;
			break;

		case microstate::CMPL_1:
			if (BIT(cpu.ir, 1))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.yp & 0xff00;
			}
			else
				m_da = cpu.yp;
			m_ustate = microstate::CMPL_2;
			break;

		case microstate::CMPL_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_tmp2 = m_da;
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				m_da = input;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
				m_da = cpu.xp;
			m_ustate = microstate::CMPL_3;
			if (!BIT(cpu.fs, 4))
				cpu.fs &= 0x7f;
			break;

		case microstate::CMPL_3:
			(void)cpu.subcz(m_data.read_byte(mmu_data_translate(m_da)), m_tmp1, BIT(cpu.fs, 7), BIT(cpu.fs, 4));
			std::swap(m_da, m_tmp2);
			++m_da;
			m_ustate = microstate::CMPL_4;
			break;

		case microstate::CMPL_4:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			if (!BIT(cpu.ir, 1))
				cpu.yp = m_da + 1;
			m_da = m_tmp2 + 1;
			m_ustate = microstate::CMPL_5;
			break;

		case microstate::CMPL_5:
			(void)cpu.subcz(m_data.read_byte(mmu_data_translate(m_da)), m_tmp1, BIT(cpu.fs, 7), true);
			if (!BIT(cpu.ir, 1))
				cpu.xp = m_da + 1;
			m_ustate = microstate::NOP_1; // TODO: output queue (if any...)
			break;

		case microstate::CLRFS_1:
			cpu.fs &= ~(1 << (cpu.ir & 0x07));
			next_instruction(input);
			break;

		case microstate::BR88_1:
			if ((cpu.fs & s_branch_fs[cpu.ir & 0x03]) == 0)
				cpu.branch(cpu.ip + s8(m_da & 0x00ff));
			m_ustate = microstate::BR_2;
			break;

		case microstate::SETFS_1:
			cpu.fs |= 1 << (cpu.ir & 0x07);
			next_instruction(input);
			break;

		case microstate::SUBCL_1:
			if (BIT(cpu.ir, 1))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.yp & 0xff00;
			}
			else
				m_da = cpu.yp;
			m_ustate = microstate::SUBCL_2;
			break;

		case microstate::SUBCL_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_tmp2 = m_da;
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				m_da = input;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
				m_da = cpu.xp;
			m_ustate = microstate::SUBCL_3;
			break;

		case microstate::SUBCL_3:
			m_tmp1 = cpu.subcz(m_data.read_byte(mmu_data_translate(m_da)), m_tmp1, BIT(cpu.fs, 7), BIT(cpu.fs, 4)); // TODO: read latch instead of terminal
			++m_tmp2;
			m_ustate = microstate::SUBCL_4;
			break;

		case microstate::SUBCL_4:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1);
			std::swap(m_da, m_tmp2);
			m_ustate = microstate::SUBCL_5;
			break;

		case microstate::SUBCL_5:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			if (!BIT(cpu.ir, 1))
				cpu.yp = m_da + 1;
			m_da = m_tmp2 + 1;
			m_ustate = microstate::SUBCL_6;
			break;

		case microstate::SUBCL_6:
			m_tmp1 = cpu.subcz(m_data.read_byte(mmu_data_translate(m_da)), m_tmp1, BIT(cpu.fs, 7), true); // TODO: read latch instead of terminal
			if (!BIT(cpu.ir, 1))
				cpu.xp = m_da + 1;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::ADDCL_1:
			if (BIT(cpu.ir, 1))
			{
				if (BIT(cpu.fs, 5))
					m_da |= cpu.yp & 0xff00;
			}
			else
				m_da = cpu.yp;
			m_ustate = microstate::ADDCL_2;
			break;

		case microstate::ADDCL_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_tmp2 = m_da;
			if (BIT(cpu.ir, 1))
			{
				++cpu.ip;
				m_da = input;
				if (BIT(cpu.fs, 5))
					m_da |= cpu.xp & 0xff00;
			}
			else
				m_da = cpu.xp;
			m_ustate = microstate::ADDCL_3;
			break;

		case microstate::ADDCL_3:
			m_tmp1 = cpu.addcz(m_data.read_byte(mmu_data_translate(m_da)), m_tmp1, BIT(cpu.fs, 7), BIT(cpu.fs, 4)); // TODO: read latch instead of terminal
			++m_tmp2;
			m_ustate = microstate::ADDCL_4;
			break;

		case microstate::ADDCL_4:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1);
			std::swap(m_da, m_tmp2);
			m_ustate = microstate::ADDCL_5;
			break;

		case microstate::ADDCL_5:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			if (!BIT(cpu.ir, 1))
				cpu.yp = m_da + 1;
			m_da = m_tmp2 + 1;
			m_ustate = microstate::ADDCL_6;
			break;

		case microstate::ADDCL_6:
			m_tmp1 = cpu.addcz(m_data.read_byte(mmu_data_translate(m_da)), m_tmp1, BIT(cpu.fs, 7), true); // TODO: read latch instead of terminal
			if (!BIT(cpu.ir, 1))
				cpu.xp = m_da + 1;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::CALL90_1:
			++cpu.ip;
			m_tmp1 = (cpu.ip & 0xf000) | u16(cpu.ir & 0x0f) << 8 | input;
			m_tmp2 = cpu.ip;
			m_da = cpu.sp - 1;
			m_ustate = microstate::CALL90_2;
			break;

		case microstate::CALL90_2:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp2 >> 8);
			--m_da;
			cpu.branch(m_tmp1);
			m_ustate = microstate::CALL_3;
			break;

		case microstate::BRA0_1:
			cpu.branch((cpu.ip & 0xf000) | u16(cpu.ir & 0x0f) << 8 | (m_da & 0x00ff));
			m_ustate = microstate::BR_2;
			break;

		case microstate::POPFS_1:
			m_da = cpu.sp;
			m_ustate = microstate::POPFS_2;
			break;

		case microstate::POPFS_2:
			cpu.fs = m_data.read_byte(mmu_data_translate(m_da)); // TODO: read latch instead of terminal
			cpu.sp = m_da + 1;
			m_ustate = microstate::NOP_1; // TODO: output queue (only FS?)
			break;

		case microstate::POPB4_1:
			m_da = cpu.sp;
			m_ustate = microstate::POPB4_2;
			break;

		case microstate::POPB4_2:
			(BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) = m_data.read_byte(mmu_data_translate(m_da));
			++m_da;
			m_ustate = microstate::POPB4_3;
			break;

		case microstate::POPB4_3:
			(BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) |= m_data.read_byte(mmu_data_translate(m_da)) << 8;
			cpu.sp = m_da + 1;
			m_ustate = microstate::NOP_1; // TODO: output queue (only XP/YP?)
			break;

		case microstate::POPB5_1:
			++cpu.ip;
			m_da = cpu.sp;
			m_tmp2 = input;
			if (BIT(cpu.fs, 5))
				m_tmp2 |= cpu.xp & 0xff00;
			m_ustate = microstate::POPB5_2;
			break;

		case microstate::POPB5_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da)); // TODO: read latch instead of terminal
			cpu.sp = m_da + 1;
			m_da = m_tmp2;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::POPB7_1:
			m_da = cpu.sp;
			m_tmp2 = cpu.xp;
			if ((cpu.fs & 0x1f) != 0)
				++cpu.xp;
			m_ustate = microstate::POPB5_2;
			break;

		case microstate::PUSHFS_1:
			m_da = cpu.sp - 1;
			m_tmp1 = cpu.fs;
			m_ustate = microstate::PUSHBC_3;
			break;

		case microstate::PUSHBC_1:
			m_da = cpu.sp - 1;
			m_tmp1 = BIT(cpu.ir, 1) ? cpu.yp : cpu.xp;
			m_ustate = microstate::PUSHBC_2;
			break;

		case microstate::PUSHBC_2:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1 >> 8);
			--m_da;
			m_ustate = microstate::PUSHBC_3;
			break;

		case microstate::PUSHBC_3:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1 & 0x00ff);
			cpu.sp = m_da;
			m_ustate = microstate::NOP_1; // TODO: output queue (only SP?)
			break;

		case microstate::PUSHBD_1:
			++cpu.ip;
			m_da = input;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.xp & 0xff00;
			m_ustate = microstate::PUSHBD_2;
			break;

		case microstate::PUSHBD_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_da = cpu.sp - 1;
			m_ustate = microstate::PUSHBD_3;
			break;

		case microstate::PUSHBD_3:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1 & 0x00ff);
			cpu.sp = m_da;
			m_ustate = microstate::NOP_1; // TODO: output queue (only SP?)
			break;

		case microstate::PUSHBF_1:
			m_da = cpu.xp;
			if ((cpu.fs & 0x1f) != 0)
				--cpu.xp;
			m_ustate = microstate::PUSHBD_2;
			break;

		case microstate::DIVC1_1:
			m_tmp2 = m_da & 0x00ff;
			m_ustate = microstate::DIVC1_2;
			break;

		case microstate::DIVC1_2:
			m_da = cpu.xp;
			m_ustate = microstate::DIV51_3;
			break;

		case microstate::XCHC4_1:
			if (BIT(cpu.ir, 1))
				cpu.yp = (cpu.yp >> 8) | (cpu.yp << 8);
			else
				cpu.xp = (cpu.xp >> 8) | (cpu.xp << 8);
			m_ustate = microstate::XCHC4_2;
			break;

		case microstate::XCHC4_2:
			m_ustate = microstate::NOP_1; // TODO: output queue (only XPl/YPl?)
			break;

		case microstate::XCHC5_1:
			if (BIT(cpu.fs, 5))
				m_da |= (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0xff00;
			m_tmp2 = m_data.read_byte(mmu_data_translate(m_da)); // TODO: read latch instead of terminal
			m_tmp1 = (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0x00ff;
			m_ustate = microstate::XCHC5_2;
			break;

		case microstate::XCHC5_2:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1);
			setl(BIT(cpu.ir, 1) ? cpu.yp : cpu.xp, m_tmp2);
			next_instruction(input);
			break;

		case microstate::MOVCC_1:
			if (BIT(cpu.fs, 5))
				m_da |= (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0xff00;
			m_tmp1 = (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0x00ff;
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::MOVCD_1:
			if (BIT(cpu.fs, 5))
				m_da |= (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0xff00;
			m_ustate = microstate::MOVCD_2;
			break;

		case microstate::MOVCD_2:
			setl(BIT(cpu.ir, 1) ? cpu.yp : cpu.xp, m_data.read_byte(mmu_data_translate(m_da)));
			next_instruction(input); // TODO: output queue
			break;

		case microstate::MULC9_1:
			m_tmp2 = m_da & 0x00ff;
			m_da = cpu.xp;
			m_ustate = microstate::MULC9_2;
			break;

		case microstate::MULC9_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_ustate = microstate::MULC9_3;
			break;

		case microstate::MULC9_3:
			m_ustate = microstate::MUL59_4;
			break;

		case microstate::CMPD0_1:
			if (BIT(cpu.fs, 5))
				m_da |= (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0xff00;
			m_ustate = microstate::CMPD0_2;
			break;

		case microstate::CMPD0_2:
			(void)cpu.subcz((BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0x00ff, m_data.read_byte(mmu_data_translate(m_da)), false, false);
			m_ustate = microstate::CMPD1_2;
			break;

		case microstate::CMPD1_1:
			(void)cpu.subcz((BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0x00ff, m_da & 0x00ff, false, false);
			m_ustate = microstate::CMPD1_2;
			break;

		case microstate::CMPD1_2:
			next_instruction(input);
			break;

		case microstate::XCHD4_1:
			m_tmp1 = std::exchange(BIT(cpu.ir, 0) ? cpu.sp : cpu.lp, cpu.xp);
			m_ustate = microstate::XCHD4_2;
			break;

		case microstate::XCHD4_2:
			cpu.xp = m_tmp1;
			next_instruction(input);
			break;

		case microstate::XCHD7_1:
			m_tmp1 = std::exchange(cpu.yp, cpu.xp);
			m_ustate = microstate::XCHD4_2;
			break;

		case microstate::MOVD8_1:
			setl(BIT(cpu.ir, 1) ? cpu.yp : cpu.xp, m_da & 0x00ff);
			next_instruction(input);
			break;

		case microstate::MOVD9_1:
			seth(BIT(cpu.ir, 1) ? cpu.yp : cpu.xp, m_da & 0x00ff);
			next_instruction(input);
			break;

		case microstate::MOVDC_1:
			if (BIT(cpu.ir, 1))
				cpu.yp = cpu.xp;
			else
				cpu.sp = cpu.xp;
			next_instruction(input);
			break;

		case microstate::MOVDD_1:
			if (BIT(cpu.ir, 1))
				cpu.xp = cpu.yp;
			else
				cpu.xp = cpu.sp;
			next_instruction(input);
			break;

		case microstate::LOOP_1:
			++cpu.ip;
			if (BIT(cpu.ir, 0))
			{
				if (BIT(cpu.ir, 1))
					++cpu.yp;
				else
					++cpu.xp;
			}
			m_da = cpu.sp;
			m_tmp2 = cpu.ip + s8(input);
			m_ustate = microstate::LOOP_2;
			break;

		case microstate::LOOP_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da)) - 1; // TODO: read latch instead of terminal
			if (m_tmp1 != 0)
				cpu.branch(m_tmp2);
			else
				cpu.sp = m_da + 1;
			m_ustate = microstate::MOV56_2;
			break;

		case microstate::DECE4_1:
			if (BIT(cpu.ir, 1))
				--cpu.yp;
			else
				--cpu.xp;
			next_instruction(input);
			break;

		case microstate::INCE5_1:
			if (BIT(cpu.ir, 1))
				++cpu.yp;
			else
				++cpu.xp;
			next_instruction(input);
			break;

		case microstate::ADDRE8_1:
			++cpu.ip;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.yp & 0xff00;
			m_tmp2 = input;
			m_ustate = microstate::ADDRE8_2;
			break;

		case microstate::ADDRE8_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_da = m_tmp2;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.xp & 0xff00;
			m_ustate = microstate::ADDRE8_3;
			break;

		case microstate::ADDRE8_3:
			if (BIT(cpu.ir, 1))
				setl(cpu.yp, cpu.addcz(m_tmp1, m_data.read_byte(mmu_data_translate(m_da)), false, false));
			else
				setl(cpu.xp, cpu.addcz(m_tmp1, m_data.read_byte(mmu_data_translate(m_da)), false, false));
			m_ustate = microstate::NOP_1; // TODO: output queue (XPl only?)
			break;

		case microstate::ADDRE9_1:
			++cpu.ip;
			m_tmp2 = m_da & 0x00ff;
			m_da = input;
			if (BIT(cpu.fs, 5))
				m_da |= (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0xff00;
			m_ustate = microstate::ADDRE9_2;
			break;

		case microstate::ADDRE9_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			m_ustate = microstate::ADDRE9_3;
			break;

		case microstate::ADDRE9_3:
			setl(BIT(cpu.ir, 1) ? cpu.yp : cpu.xp, cpu.addcz(m_tmp1, m_tmp2, false, false));
			m_ustate = microstate::NOP_1; // TODO: output queue (XPl/YPl only?)
			break;

		case microstate::ADDREC_1:
			m_tmp1 = (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0x00ff;
			m_tmp2 = (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) >> 8;
			m_ustate = microstate::ADDRE9_3;
			break;

		case microstate::ADDRED_1:
			m_tmp1 = (BIT(cpu.ir, 1) ? cpu.yp : cpu.xp) & 0x00ff;
			m_tmp2 = m_da & 0x00ff;
			m_ustate = microstate::ADDRE9_3;
			break;

		case microstate::CMPBF0_1:
			++cpu.ip;
			m_tmp1 = m_data.read_byte(mmu_data_translate(cpu.xp)) ^ (m_da & 0x00ff);
			m_tmp2 = cpu.ip + s8(input);
			m_ustate = microstate::CMPBF1_3;
			break;

		case microstate::CMPBF1_1:
			++cpu.ip;
			m_tmp1 = m_da & 0x00ff;
			m_da = input;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.xp & 0xff00;
			m_ustate = microstate::CMPBF1_2;
			break;

		case microstate::CMPBF1_2:
			++cpu.ip;
			m_tmp1 ^= m_data.read_byte(mmu_data_translate(m_da));
			m_tmp2 = cpu.ip + s8(input);
			m_ustate = microstate::CMPBF1_3;
			break;

		case microstate::CMPBF1_3:
			if (BIT(cpu.ir, 3) ? m_tmp1 == 0 : m_tmp1 != 0)
				cpu.branch(m_tmp2);
			m_ustate = microstate::BR_2;
			break;

		case microstate::MOV1_1:
			++cpu.ip;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.yp & 0xff00;
			m_tmp2 = input;
			m_tmp1 = 1 << (m_tmp2 & 0x07);
			m_ustate = microstate::MOV1_2;
			break;

		case microstate::MOV1_2:
			++cpu.ip;
			m_tmp1 &= m_data.read_byte(mmu_data_translate(m_da));
			m_da = input;
			if (BIT(cpu.fs, 5))
				m_da |= cpu.xp & 0xff00;
			m_ustate = microstate::MOV1_3;
			break;

		case microstate::MOV1_3:
			if (BIT(cpu.ir, 3) ? m_tmp1 == 0 : m_tmp1 != 0)
				m_ustate = microstate::MOV1_4;
			else
				m_ustate = microstate::MOV1N_4;
			m_tmp1 = 1 << ((m_tmp2 >> 4) & 0x07);
			break;

		case microstate::MOV1_4:
			m_tmp1 |= m_data.read_byte(mmu_data_translate(m_da)); // TODO: read latch instead of terminal
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::MOV1N_4:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da)) & ~m_tmp1; // TODO: read latch instead of terminal
			set_output_queued();
			next_instruction(input);
			break;

		case microstate::WAIT_1:
			++cpu.ip;
			// TODO: operand treated as a wakeup interrupt mask; byte order and layout unverified
			m_tmp1 = m_da | (input << 8);
			m_ustate = microstate::WAIT_2;
			break;

		case microstate::WAIT_2:
			cpu.wait = m_tmp1;
			m_cpum |= 1U << BIT(m_cpum, 4);
			next_instruction(input);
			break;

		case microstate::RET_1:
			m_da = cpu.sp;
			m_ustate = microstate::RET_2;
			break;

		case microstate::RET_2:
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			++m_da;
			m_ustate = microstate::RET_3;
			break;

		case microstate::RET_3:
			cpu.branch(u16(m_data.read_byte(mmu_data_translate(m_da))) << 8 | m_tmp1);
			cpu.sp = m_da + 1;
			m_ustate = microstate::BR_2;
			break;

		case microstate::RETI_1:
			m_da = cpu.sp;
			m_ustate = microstate::RETI_2;
			break;

		case microstate::RETI_2:
			cpu.fs = m_data.read_byte(mmu_data_translate(m_da));
			++m_da;
			m_ustate = microstate::RETI_3;
			break;

		case microstate::RETI_3:
			cpu.iemask = false;
			m_tmp1 = m_data.read_byte(mmu_data_translate(m_da));
			++m_da;
			m_ustate = microstate::RETI_4;
			break;

		case microstate::RETI_4:
			cpu.ip = u16(m_data.read_byte(mmu_data_translate(m_da))) << 8 | m_tmp1;
			if ((cpu.fs & 0x1f) != 0)
			{
				++m_da;
				m_ustate = microstate::RETI_5;
			}
			else
			{
				cpu.sp = m_da + 1;
				m_ustate = microstate::BR_2;
			}
			break;

		case microstate::RETI_5:
			cpu.ir = m_data.read_byte(mmu_data_translate(m_da));
			cpu.sp = m_da + 1;
			m_ustate = microstate::NEXT;
			swap_cpus();
			break;

		case microstate::BR_1:
			cpu.branch((m_da & 0x00ff) << 8 | input);
			m_ustate = microstate::BR_2;
			break;

		case microstate::BR_2:
			next_instruction(input);
			break;

		case microstate::CALL_1:
			++cpu.ip;
			m_tmp1 = input;
			m_tmp2 = cpu.ip + 1;
			m_da = cpu.sp - 1;
			m_ustate = microstate::CALL_2;
			break;

		case microstate::CALL_2:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp2 >> 8);
			--m_da;
			cpu.branch(m_tmp1 << 8 | input);
			m_ustate = microstate::CALL_3;
			break;

		case microstate::CALL_3:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp2 & 0x00ff);
			cpu.sp = m_da;
			next_instruction(input);
			break;

		case microstate::PUSHFB_1:
			m_tmp1 = m_da & 0x00ff;
			m_da = cpu.sp - 1;
			m_ustate = microstate::PUSHBC_3;
			break;

		case microstate::BRFC_1:
			cpu.branch(cpu.xp);
			m_ustate = microstate::BR_2;
			break;

		case microstate::CALLFD_1:
			m_tmp1 = cpu.xp;
			m_tmp2 = cpu.ip;
			m_da = cpu.sp - 1;
			m_ustate = microstate::CALL90_2;
			break;

		case microstate::RDTBL_1:
			m_ustate = microstate::RDTBL_2;
			break;

		case microstate::RDTBL_2:
			m_tmp2 = std::exchange(cpu.ip, cpu.yp);
			if ((cpu.fs & 0x1f) != 0)
				++cpu.yp;
			m_ustate = microstate::RDTBL_3;
			break;

		case microstate::RDTBL_3:
			m_tmp1 = input;
			m_da = cpu.xp;
			if ((cpu.fs & 0x1f) != 0)
				++cpu.xp;
			cpu.ip = m_tmp2;
			m_ustate = microstate::MOV56_2;
			break;

		case microstate::PI_1:
			m_tmp2 = cpu.ip;
			if (m_irq != 0)
			{
				// IRQ0 (first of four external edge inputs?) has the highest priority (after RESET)
				unsigned level = mame_bit_width((m_irq - 1U) & ~m_irq);
				(void)standard_irq_callback(level, cpu.ip);
				cpu.ie &= ~(1 << level); // No separate in-service lockout; handler must re-enable specific interrupt
				m_if &= ~(1 << level);
				cpu.iemask = true;
				cpu.ip = 0x0004 + (level << 1);
			}
			else
				cpu.ip = 0x0002; // PI vector unconfirmed
			if (BIT(m_cpum, 4))
				cpu.ip |= 0x0020;
			m_da = cpu.sp - 1;
			if ((cpu.fs & 0x1f) != 0)
			{
				m_tmp1 = cpu.ir;
				m_ustate = microstate::PI_2;
			}
			else
				m_ustate = microstate::PI_3;
			break;

		case microstate::PI_2:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp1);
			--m_da;
			m_ustate = microstate::PI_3;
			break;

		case microstate::PI_3:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp2 >> 8);
			--m_da;
			m_tmp1 = u16(input) << 8;
			++cpu.ip;
			m_ustate = microstate::PI_4;
			break;

		case microstate::PI_4:
			m_data.write_byte(mmu_data_translate(m_da), m_tmp2 & 0x00ff);
			--m_da;
			m_tmp2 = cpu.fs;
			cpu.branch(m_tmp1 | input); // FS repeat bits are cleared here
			m_ustate = microstate::CALL_3;
			break;

		case microstate::UNKNOWN:
			logerror("%04X: Unknown or unemulated instruction %02X encountered\n", cpu.irp, cpu.ir);
			next_instruction(input);
			break;
		}
	}
}


//**************************************************************************
//  MN1880023
//**************************************************************************

mn1880023_device::mn1880023_device()
	: mn1880_device(0x5f, 0x0060, 0x031f)
	, m_port_in_cb(*this, 0xff)
	, m_port_out_cb(*this)
	, m_ad_in_cb(*this, 0)
	, m_sio_in_cb(*this, 0xff)
	, m_sio_out_cb(*this)
	, m_uart_txd_cb(*this)
	, m_pwm_out_cb(*this)
	, m_timer0(nullptr)
	, m_timer3(nullptr)
	, m_sio_timer{nullptr, nullptr}
	, m_uart_tx_timer(nullptr)
	, m_uart_rx_timer(nullptr)
	, m_adc_timer(nullptr)
	, m_pwm_timer(nullptr)
	, m_port_input_mask{}
	, m_regs{}
	, m_timer2_start(attotime::zero)
	, m_timer4_count(0)
	, m_timer4_prescale(0)
	, m_timer2_capture_input(false)
	, m_timer4_count_input(false)
	, m_uart_tx_data(0)
	, m_uart_rx_data(0)
	, m_uart_tx_bits(0)
	, m_uart_rx_bits(0)
	, m_uart_rxd(true)
	, m_uart_txd(true)
{
	device_start();
}

void mn1880023_device::device_start()
{
	mn1880_device::device_start();

	m_timer0 = timer_alloc(FUNC(mn1880023_device::timer0_tick), this);
	m_timer3 = timer_alloc(FUNC(mn1880023_device::timer3_tick), this);
	m_sio_timer[0] = timer_alloc(FUNC(mn1880023_device::sio0_tick), this);
	m_sio_timer[1] = timer_alloc(FUNC(mn1880023_device::sio1_tick), this);
	m_uart_tx_timer = timer_alloc(FUNC(mn1880023_device::uart_tx_tick), this);
	m_uart_rx_timer = timer_alloc(FUNC(mn1880023_device::uart_rx_tick), this);
	m_adc_timer = timer_alloc(FUNC(mn1880023_device::adc_tick), this);
	m_pwm_timer = timer_alloc(FUNC(mn1880023_device::pwm_tick), this);
	m_rx_line_timer = timer_alloc(FUNC(mn1880023_device::rx_line_tick), this); // LEMUS
	m_timer1 = timer_alloc(FUNC(mn1880023_device::timer1_tick), this); // LEMUS: inferred from v1.5
}

void mn1880023_device::device_reset()
{
	mn1880_device::device_reset();

	// GPIO pin levels at reset are unknown, so no GPIO outputs are driven here
	std::fill(std::begin(m_regs), std::end(m_regs), 0);
	m_timer0->adjust(attotime::never);
	m_timer1->adjust(attotime::never); // LEMUS: inferred from v1.5
	m_timer0_reloading = false;
	m_timer1_out = false;
	m_timer3->adjust(attotime::never);
	m_sio_timer[0]->adjust(attotime::never);
	m_sio_timer[1]->adjust(attotime::never);
	m_uart_tx_timer->adjust(attotime::never);
	m_uart_rx_timer->adjust(attotime::never);
	m_uart_tx_data = 0;
	m_uart_rx_data = 0;
	m_uart_tx_bits = 0;
	m_uart_rx_bits = 0;
	m_uart_txd = true; // TXD idles high
	m_uart_txd_cb(1);
	m_adc_timer->adjust(attotime::never);
	m_pwm_timer->adjust(attotime::never);
	m_timer2_start = machine().time();
	m_timer4_count = 0;
	m_timer4_prescale = 0;
}

u8 mn1880023_device::reg_r(offs_t offset)
{
	return m_regs[offset];
}

void mn1880023_device::reg_w(offs_t offset, u8 data)
{
	m_regs[offset] = data;
}

u8 mn1880023_device::port_r(offs_t offset)
{
	const u8 mask = m_port_input_mask[offset];
	if (!mask)
		return m_regs[offset];

	return (m_regs[offset] & ~mask) | (m_port_in_cb[offset]() & mask);
}

void mn1880023_device::port_w(offs_t offset, u8 data)
{
	m_regs[offset] = data;
	m_port_out_cb[offset](data);
}

void mn1880023_device::irq_flags_w(u8 data)
{
	// writing 0 to flag bit 0, 2 or 4 also clears request 4, 6 or 8
	m_regs[0x14] = data;
	clear_interrupt_requests((~data & 0x15) << 4);
}

void mn1880023_device::sio0_control_w(u8 data)
{
	if (m_regs[0x19] != data)
	{
		m_sio_timer[0]->adjust(attotime::never);
		m_uart_tx_timer->adjust(attotime::never);
		m_uart_rx_timer->adjust(attotime::never);
		m_uart_tx_bits = 0;
		m_uart_rx_bits = 0;
		m_regs[0x18] &= ~0x88;
		clear_interrupt_requests(1U << IRQ_UART_RX);
		uart_txd(1);
	}
	m_regs[0x19] = data;
}

void mn1880023_device::sio0_data_w(u8 data)
{
	m_regs[0x1a] = data;
	if (uart_enabled())
	{
		// no transmit FIFO is known
		if (!m_uart_tx_bits)
		{
			m_uart_tx_data = data;
			m_uart_tx_byte = data; // LEMUS
			m_uart_tx_bits = 1;
			m_regs[0x18] |= 0x08;
			uart_txd(0);
			m_uart_tx_timer->adjust(uart_bit_period());
		}
		return;
	}
	m_regs[0x18] |= 0x80;
	m_regs[0x14] &= ~0x20;
	m_sio_timer[0]->adjust(clocks_to_attotime(64));
}

TIMER_CALLBACK_MEMBER(mn1880023_device::sio0_tick)
{
	m_regs[0x18] &= ~0x80;
	m_regs[0x14] |= 0x20;

	// only the transmit (a6) and receive (af) modes used by CR-504 firmware are known
	if (m_regs[0x19] == 0xa6)
		m_sio_out_cb[0](m_regs[0x1a]);
	else if (m_regs[0x19] == 0xaf)
		m_regs[0x1b] = m_sio_in_cb[0]();
}

attotime mn1880023_device::uart_bit_period() const
{
	// TODO: only 8N1 at oscillator/256 (control 62) is known
	return clocks_to_attotime(256);
}

TIMER_CALLBACK_MEMBER(mn1880023_device::uart_tx_tick)
{
	if (m_uart_tx_bits <= 8)
	{
		uart_txd(BIT(m_uart_tx_data, 0));
		m_uart_tx_data >>= 1;
	}
	else if (m_uart_tx_bits == 9)
	{
		uart_txd(1); // stop bit
	}
	else
	{
		m_uart_tx_bits = 0;
		m_regs[0x18] &= ~0x08;
		m_uart_tx_byte_cb(m_uart_tx_byte); // LEMUS
		return;
	}
	++m_uart_tx_bits;
	m_uart_tx_timer->adjust(uart_bit_period());
}

void mn1880023_device::uart_rxd_w(int state)
{
	if (m_uart_rxd && !state && !m_uart_rx_bits && uart_enabled())
	{
		m_uart_rx_bits = 1;
		m_uart_rx_data = 0;
		m_uart_rx_timer->adjust(uart_bit_period() / 2);
	}
	m_uart_rxd = bool(state);
}

TIMER_CALLBACK_MEMBER(mn1880023_device::uart_rx_tick)
{
	if (m_uart_rx_bits == 1)
	{
		if (m_uart_rxd) // reject a start pulse shorter than half a bit
		{
			m_uart_rx_bits = 0;
			return;
		}
	}
	else if (m_uart_rx_bits <= 9)
	{
		m_uart_rx_data |= u8(m_uart_rxd) << (m_uart_rx_bits - 2);
	}
	else
	{
		// status bits are unknown, so framing errors and breaks are dropped
		if (m_uart_rxd)
		{
			m_regs[0x1b] = m_uart_rx_data;
			request_interrupt(IRQ_UART_RX);
		}
		m_uart_rx_bits = 0;
		return;
	}
	++m_uart_rx_bits;
	m_uart_rx_timer->adjust(uart_bit_period());
}

void mn1880023_device::uart_txd(int state)
{
	if (m_uart_txd != bool(state))
	{
		m_uart_txd = bool(state);
		m_uart_txd_cb(state);
	}
}

void mn1880023_device::sio1_data_w(u8 data)
{
	m_regs[0x3a] = data;
	m_regs[0x37] &= ~0x10;
	m_sio_timer[1]->adjust(clocks_to_attotime(64));
}

TIMER_CALLBACK_MEMBER(mn1880023_device::sio1_tick)
{
	m_sio_out_cb[1](m_regs[0x3a]);
	m_regs[0x3b] = m_sio_in_cb[1]();
	m_regs[0x37] |= 0x10;
}

void mn1880023_device::adc_control_w(u8 data)
{
	m_regs[0x1e] = data;
	// CR-504 starts with bit 3, Drum Station with bit 5
	if (BIT(data, 3) || BIT(data, 5))
		m_adc_timer->adjust(clocks_to_attotime(64));
}

TIMER_CALLBACK_MEMBER(mn1880023_device::adc_tick)
{
	m_regs[0x1c] = m_ad_in_cb[m_regs[m_adc_channel_register] & 7](); // LEMUS: inferred from v1.5 at 34AD (register 1E there)
	m_regs[0x14] |= 0x40;
	m_regs[0x37] |= 0x08;
}

u8 mn1880023_device::timer0_r(offs_t offset)
{
	if (!m_timer0->enabled())
		return m_regs[0x20 + offset];

	const u16 count = 0x10000 - (m_timer0->remaining().as_ticks(clock()) + 15) / 16;
	return offset ? (count >> 8) : count;
}

void mn1880023_device::timer0_w(offs_t offset, u8 data)
{
	m_regs[0x20 + offset] = data;
	update_timer0();
}

void mn1880023_device::update_timer0()
{
	// LEMUS: inferred from v1.5 at 35D8, 785B, 77D4: control 14 counts up from the value in
	// 0x20/0x21 and reloads it at each overflow; a write takes effect then, without a restart
	if (BIT(m_regs[0x31], 4) && (m_regs[0x33] & 0x9f) == 0x14)
	{
		if (!m_timer0_reloading)
		{
			m_timer0_reloading = true;
			m_timer0->adjust(clocks_to_attotime(TIMER01_CLOCKS * (0x10000 - get_u16le(&m_regs[0x20]))));
		}
		return;
	}
	m_timer0_reloading = false;

	// only the periodic mode (control 8a) used by CR-504 firmware is emulated
	if (BIT(m_regs[0x31], 4) && (m_regs[0x33] & 0x9f) == 0x8a)
	{
		const attotime period = clocks_to_attotime(16 * (0x10000 - get_u16le(&m_regs[0x20])));
		m_timer0->adjust(period, 0, period);
	}
	else
	{
		m_timer0->adjust(attotime::never);
	}
}

TIMER_CALLBACK_MEMBER(mn1880023_device::timer0_tick)
{
	request_interrupt(IRQ_TIMER0);
	if (m_timer0_reloading) // LEMUS: inferred from v1.5 at 785B (the next period from the value now in 0x20/0x21)
		m_timer0->adjust(clocks_to_attotime(TIMER01_CLOCKS * (0x10000 - get_u16le(&m_regs[0x20]))));
}

u16 mn1880023_device::timer2_count() const
{
	const u16 count = get_u16le(&m_regs[0x24]);
	if (!BIT(m_regs[0x31], 6))
		return count;

	return count + (machine().time() - m_timer2_start).as_ticks(clock() / 16);
}

u8 mn1880023_device::timer2_r(offs_t offset)
{
	const u16 count = timer2_count();
	return offset ? (count >> 8) : count;
}

void mn1880023_device::timer2_w(offs_t offset, u8 data)
{
	m_regs[0x24 + offset] = data;
	m_timer2_start = machine().time();
}

void mn1880023_device::timer2_capture_w(int state)
{
	if (state && !m_timer2_capture_input)
		put_u16le(&m_regs[0x2c], timer2_count());
	m_timer2_capture_input = state;
}

void mn1880023_device::timer3_w(offs_t offset, u8 data)
{
	m_regs[0x26 + offset] = data;
	update_timer3();
}

void mn1880023_device::update_timer3()
{
	if (BIT(m_regs[0x31], 7))
	{
		// 0x35 bit 4 is periodic (inferred); other modes stop after one overflow
		const attotime period = clocks_to_attotime(16 * (0x10000 - get_u16le(&m_regs[0x26])));
		m_timer3->adjust(period, 0, BIT(m_regs[0x35], 4) ? period : attotime::never);
	}
	else
		m_timer3->adjust(attotime::never);
}

TIMER_CALLBACK_MEMBER(mn1880023_device::timer3_tick)
{
	m_regs[0x14] |= 0x04;
	m_regs[0x37] |= 0x02;
	// periodic mode also requests the interrupt (inferred from Drum Station)
	if (BIT(m_regs[0x35], 4) || BIT(m_regs[0x36], 1))
		request_interrupt(IRQ_TIMER3);
}

void mn1880023_device::timer_run_w(u8 data)
{
	const u8 changed = m_regs[0x31] ^ data;
	m_regs[0x31] = data;
	if (BIT(changed, 6))
		m_timer2_start = machine().time();
	if (BIT(changed, 4))
		update_timer0();
	if (BIT(changed, 5)) // LEMUS: inferred from v1.5 at 77DC-77DE (timer 1 run bit)
		update_timer1();
	if (BIT(changed, 7))
		update_timer3();
}

void mn1880023_device::timer_control_w(u8 data)
{
	const u8 changed = m_regs[0x33] ^ data;
	m_regs[0x33] = data;
	if (changed & 0x9f)
	{
		update_timer0();
		update_timer1(); // LEMUS: inferred from v1.5 at 35D8
	}
	if (BIT(changed & data, 6))
	{
		m_timer4_count = get_u16le(&m_regs[0x4c]);
		m_timer4_prescale = 0;
	}
}

void mn1880023_device::timer3_control_w(u8 data)
{
	const u8 changed = m_regs[0x35] ^ data;
	m_regs[0x35] = data;
	if (BIT(changed, 4))
		update_timer3();
}

u8 mn1880023_device::timer4_r(offs_t offset)
{
	return offset ? (m_timer4_count >> 8) : m_timer4_count;
}

void mn1880023_device::timer4_count_w(int state)
{
	// counts every second input edge (inferred)
	if (state && !m_timer4_count_input && BIT(m_regs[0x33], 6) && ++m_timer4_prescale == 2)
	{
		m_timer4_prescale = 0;
		if (!++m_timer4_count)
		{
			m_regs[0x14] |= 0x10;
			request_interrupt(IRQ_TIMER4);
		}
	}
	m_timer4_count_input = state;
}

u16 mn1880023_device::pwm_reload() const
{
	return get_u16le(&m_regs[0x50]) & 0x3ff;
}

u16 mn1880023_device::pwm_compare() const
{
	return get_u16le(&m_regs[0x52]) & 0x3ff;
}

void mn1880023_device::pwm_reload_w(offs_t offset, u8 data)
{
	m_regs[0x50 + offset] = data;
	update_pwm();
}

void mn1880023_device::pwm_control_w(u8 data)
{
	const u8 changed = m_regs[0x55] ^ data;
	m_regs[0x55] = data;
	if (BIT(changed, 0))
		update_pwm();
}

void mn1880023_device::update_pwm()
{
	// 10-bit counter clocked at oscillator/4
	if (BIT(m_regs[0x55], 0))
	{
		const attotime period = clocks_to_attotime(4 * (0x400 - pwm_reload()));
		m_pwm_timer->adjust(period, 0, period);
	}
	else
	{
		m_pwm_timer->adjust(attotime::never);
	}
}

TIMER_CALLBACK_MEMBER(mn1880023_device::pwm_tick)
{
	// only the rising edge at the start of each period is emulated
	m_pwm_out_cb(1);
	m_pwm_out_cb(0);

	m_regs[0x37] |= 0x04;
	m_regs[0x14] |= 0x01;
	if (BIT(m_regs[0x36], 2))
		request_interrupt(IRQ_PWM);
}

//**************************************************************************
//  On-chip register maps, generated by tools/port_mn1880.py from MAME's internal
//  maps (a later map() entry overrides an earlier one, as in MAME)
//**************************************************************************

bool mn1880_device::internal_r(offs_t offset, u8 &data)
{
	switch (offset)
	{
	case 0x12:
		data = ie0_r();
		return true;
	case 0x15:
		data = ie1_r();
		return true;
	case 0x16:
		data = cpum_r();
		return true;
	default:
		return false;
	}
}

bool mn1880_device::internal_w(offs_t offset, u8 data)
{
	switch (offset)
	{
	case 0x12:
		ie0_w(data);
		return true;
	case 0x15:
		ie1_w(data);
		return true;
	case 0x16:
		cpum_w(data);
		return true;
	default:
		return false;
	}
}

bool mn1880023_device::internal_r(offs_t offset, u8 &data)
{
	switch (offset)
	{
	case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
	case 0x08: case 0x09:
		data = port_r(offset);
		return true;
	case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f: case 0x10: case 0x11:
	case 0x13: case 0x14: case 0x17: case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c:
	case 0x1d: case 0x1e: case 0x1f: case 0x22: case 0x23: case 0x26: case 0x27: case 0x28:
	case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x30:
	case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37: case 0x38:
	case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40:
	case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: case 0x48:
	case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x50: case 0x51: case 0x52:
	case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a:
	case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
		data = reg_r(offset);
		return true;
	case 0x12: case 0x15: case 0x16:
		return mn1880_device::internal_r(offset, data);
	case 0x20: case 0x21:
		data = timer0_r(offset - 0x20);
		return true;
	case 0x24: case 0x25:
		data = timer2_r(offset - 0x24);
		return true;
	case 0x4e: case 0x4f:
		data = timer4_r(offset - 0x4e);
		return true;
	default:
		return false;
	}
}

bool mn1880023_device::internal_w(offs_t offset, u8 data)
{
	switch (offset)
	{
	case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
	case 0x08: case 0x09:
		port_w(offset, data);
		return true;
	case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f: case 0x10: case 0x11:
	case 0x13: case 0x17: case 0x18: case 0x1b: case 0x1c: case 0x1d: case 0x1f: case 0x22:
	case 0x23: case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e:
	case 0x2f: case 0x30: case 0x32: case 0x34: case 0x36: case 0x37: case 0x38: case 0x39:
	case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40: case 0x41: case 0x42:
	case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a:
	case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x52: case 0x53: case 0x54:
	case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d:
	case 0x5e: case 0x5f:
		reg_w(offset, data);
		return true;
	case 0x12: case 0x15: case 0x16:
		return mn1880_device::internal_w(offset, data);
	case 0x14:
		irq_flags_w(data);
		return true;
	case 0x19:
		sio0_control_w(data);
		return true;
	case 0x1a:
		sio0_data_w(data);
		return true;
	case 0x1e:
		adc_control_w(data);
		return true;
	case 0x20: case 0x21:
		timer0_w(offset - 0x20, data);
		return true;
	case 0x24: case 0x25:
		timer2_w(offset - 0x24, data);
		return true;
	case 0x26: case 0x27:
		timer3_w(offset - 0x26, data);
		return true;
	case 0x31:
		timer_run_w(data);
		return true;
	case 0x33:
		timer_control_w(data);
		return true;
	case 0x35:
		timer3_control_w(data);
		return true;
	case 0x3a:
		sio1_data_w(data);
		return true;
	case 0x50: case 0x51:
		pwm_reload_w(offset - 0x50, data);
		return true;
	case 0x55:
		pwm_control_w(data);
		return true;
	default:
		return false;
	}
}

//**************************************************************************
//  LEMUS: in place of MAME's device framework
//**************************************************************************

emu_timer &emu_timer::adjust(attotime start_delay, s32 param, const attotime &period)
{
	if (m_owner->m_callback_timer == this)
		m_owner->m_callback_timer_modified = true;
	m_param = param;
	m_period = period;
	m_expire = start_delay.is_never() ? attotime::never : attotime{m_owner->local_time().clocks + start_delay.clocks};
	m_enabled = !m_expire.is_never();
	m_order = ++m_owner->m_timer_order;
	if (m_enabled)
		m_owner->timer_armed(m_expire);
	return *this;
}

attotime emu_timer::remaining() const
{
	const u64 now = m_owner->local_time().clocks;
	return now >= m_expire.clocks ? attotime::zero : attotime{m_expire.clocks - now};
}

void mn1880_device::device_start()
{
	// MAME sets up the address spaces, debugger state and save states here
}

void mn1880_device::set_ram_end(u16 end)
{
	m_ram_end = end;
	m_ram.resize(end >= m_ram_start ? size_t(end - m_ram_start + 1) : 0, 0);
}

int mn1880_device::run(int cycles)
{
	if (cycles <= 0)
		return 0;
	const u64 end = m_slice_start + u64(cycles);
	for (;;)
	{
		execute_timers();
		if (m_slice_start >= end)
			break;

		// run up to the next timer, which then fires before the next cycle
		u64 stop = end;
		for (int i = 0; i < m_timer_count; i++)
		{
			const emu_timer &timer = m_timers[size_t(i)];
			if (timer.m_enabled)
				stop = std::min(stop, (timer.m_expire.clocks + clocks_per_cycle - 1) / clocks_per_cycle);
		}
		m_slice_len = s32(stop - m_slice_start);
		m_icount = m_slice_len;
		execute_run();
		m_slice_start += u64(m_slice_len - m_icount);
		m_slice_len = m_icount = 0;
	}
	return cycles;
}

// A timer was set to expire: if that is before the current slice ends, end the slice at the
// timer's cycle (MAME aborts the timeslice).
void mn1880_device::timer_armed(const attotime &expire)
{
	if (m_icount <= 0)
		return;
	const u64 now = local_cycles();
	const u64 due = (expire.clocks + clocks_per_cycle - 1) / clocks_per_cycle;
	const u64 left = due > now ? due - now : 0;
	if (left < u64(m_icount))
	{
		m_slice_len -= m_icount - s32(left);
		m_icount = s32(left);
	}
}

// Fires the timers that are due, earliest first and equal ones in the order they were armed
// (MAME's device_scheduler::execute_timers and timer list): a one-shot timer is disabled before
// its callback, a periodic one moves on by its period after it, unless the callback set it again.
void mn1880_device::execute_timers()
{
	const u64 now = m_slice_start * clocks_per_cycle;
	for (;;)
	{
		emu_timer *first = nullptr;
		for (int i = 0; i < m_timer_count; i++)
		{
			emu_timer &timer = m_timers[size_t(i)];
			if (timer.m_enabled && timer.m_expire.clocks <= now && (!first || timer.m_expire.clocks < first->m_expire.clocks ||
					(timer.m_expire.clocks == first->m_expire.clocks && timer.m_order < first->m_order)))
				first = &timer;
		}
		if (!first)
			return;

		emu_timer &timer = *first;
		const bool periodic = !timer.m_period.is_never() && timer.m_period.clocks != 0;
		if (!periodic)
			timer.m_enabled = false;
		m_callback_timer = &timer;
		m_callback_time = timer.m_expire;
		m_callback_timer_modified = false;
		if (timer.m_callback)
			timer.m_callback(timer.m_param);
		m_callback_timer = nullptr;
		if (!m_callback_timer_modified)
		{
			timer.m_expire = periodic ? attotime{timer.m_expire.clocks + timer.m_period.clocks} : attotime::never;
			timer.m_order = ++m_timer_order;
		}
	}
}

u8 mn1880_device::data_r(u16 addr)
{
	u8 data;
	if (addr <= m_internal_io_end && internal_r(addr, data))
	{
		if (m_sfr_hook)
			m_sfr_hook(addr, data, false);
		return data;
	}
	if (addr >= m_ram_start && addr <= m_ram_end)
		return m_ram[addr - m_ram_start];
	return m_bus->read_data(addr);
}

void mn1880_device::data_w(u16 addr, u8 data)
{
	if (addr <= m_internal_io_end)
	{
		if (m_sfr_hook)
			m_sfr_hook(addr, data, true);
		if (internal_w(addr, data))
			return;
	}
	if (addr >= m_ram_start && addr <= m_ram_end)
		m_ram[addr - m_ram_start] = data;
	else
		m_bus->write_data(addr, data);
}

void mn1880_device::logerror(const char *format, ...)
{
	if (!m_log)
		return;
	char buf[256];
	va_list ap;
	va_start(ap, format);
	std::vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	m_log(buf);
}

// LEMUS: the sender on RXD (uart_rx_byte)
void mn1880023_device::uart_rx_byte(u8 data)
{
	m_rx_queue.push_back(data);
	if (m_rx_line_bit == 0)
		rx_line_tick(0);
}

void mn1880023_device::rx_line_tick(s32)
{
	if (m_rx_line_bit == 0)
	{
		if (m_rx_queue.empty())
			return;
		m_rx_line_byte = m_rx_queue.front();
		m_rx_queue.pop_front();
		uart_rxd_w(0); // start bit
	}
	else if (m_rx_line_bit <= 8)
		uart_rxd_w(BIT(m_rx_line_byte, m_rx_line_bit - 1));
	else if (m_rx_line_bit == 9)
		uart_rxd_w(1); // stop bit
	else
	{
		// the stop bit has ended; the next frame, if any, starts now
		m_rx_line_bit = 0;
		rx_line_tick(0);
		return;
	}
	++m_rx_line_bit;
	m_rx_line_timer->adjust(uart_bit_period());
}

// LEMUS: inferred from v1.5 at 77D4-77EF, 784E: timer 1 (0x22/0x23, run bit 0x31 bit 5) works as
// timer 0 in control mode 14, requests IRQ4 at each overflow and toggles its output pin. Setting
// the run bit (again) starts it from the value in 0x22/0x23.
void mn1880023_device::update_timer1()
{
	if (BIT(m_regs[0x31], 5) && (m_regs[0x33] & 0x9f) == 0x14)
		m_timer1->adjust(clocks_to_attotime(TIMER01_CLOCKS * (0x10000 - get_u16le(&m_regs[0x22]))));
	else
		m_timer1->adjust(attotime::never);
}

void mn1880023_device::timer1_tick(s32)
{
	request_interrupt(IRQ_TIMER1);
	m_timer1_out = !m_timer1_out;
	m_timer1_out_cb(m_timer1_out ? 1 : 0);
	m_timer1->adjust(clocks_to_attotime(TIMER01_CLOCKS * (0x10000 - get_u16le(&m_regs[0x22]))));
}


// LEMUS: session snapshots (mn1880.h). Every field that changes as the CPU runs, in a fixed order.
void mn1880_device::save_state(std::vector<u8> &v) const
{
	using lemus_state::put;
	put(v, m_ram_end);
	put(v, u32(m_ram.size()));
	v.insert(v.end(), m_ram.begin(), m_ram.end());
	put(v, m_timer_count);
	for (int i = 0; i < m_timer_count; i++)
	{
		const emu_timer &t = m_timers[size_t(i)];
		put(v, t.m_expire.clocks);
		put(v, t.m_period.clocks);
		put(v, t.m_order);
		put(v, t.m_param);
		put(v, t.m_enabled);
	}
	put(v, m_slice_start);
	put(v, m_slice_len);
	put(v, m_callback_time.clocks);
	put(v, m_callback_timer_modified);
	put(v, m_timer_order);
	for (const cpu_registers &c : m_cpu)
	{
		put(v, c.ip);
		put(v, c.irp);
		put(v, c.ir);
		put(v, c.fs);
		put(v, c.xp);
		put(v, c.yp);
		put(v, c.sp);
		put(v, c.lp);
		put(v, c.wait);
		put(v, c.ie);
		put(v, c.iemask);
	}
	put(v, m_cpum);
	put(v, m_ustate);
	put(v, m_da);
	put(v, m_tmp1);
	put(v, m_tmp2);
	put(v, m_output_queue_state);
	put(v, m_icount);
	put(v, m_input_state);
	put(v, m_if);
	put(v, m_irq);
	put(v, m_mmu_bank);
	put(v, m_mmu_enable);
}

bool mn1880_device::load_state(const u8 *&p, const u8 *end)
{
	using lemus_state::get;
	u32 ramSize = 0;
	int timers = 0;
	if (m_callback_timer != nullptr || !get(p, end, m_ram_end) || !get(p, end, ramSize) || ramSize != m_ram.size() || size_t(end - p) < ramSize)
		return false;
	std::memcpy(m_ram.data(), p, ramSize);
	p += ramSize;
	if (!get(p, end, timers) || timers != m_timer_count)
		return false;
	for (int i = 0; i < m_timer_count; i++)
	{
		emu_timer &t = m_timers[size_t(i)];
		if (!get(p, end, t.m_expire.clocks) || !get(p, end, t.m_period.clocks) || !get(p, end, t.m_order) || !get(p, end, t.m_param) || !get(p, end, t.m_enabled))
			return false;
	}
	bool ok = get(p, end, m_slice_start) && get(p, end, m_slice_len) && get(p, end, m_callback_time.clocks) && get(p, end, m_callback_timer_modified) && get(p, end, m_timer_order);
	for (cpu_registers &c : m_cpu)
		ok = ok && get(p, end, c.ip) && get(p, end, c.irp) && get(p, end, c.ir) && get(p, end, c.fs) && get(p, end, c.xp) && get(p, end, c.yp) && get(p, end, c.sp) && get(p, end, c.lp) && get(p, end, c.wait) && get(p, end, c.ie) && get(p, end, c.iemask);
	return ok && get(p, end, m_cpum) && get(p, end, m_ustate) && get(p, end, m_da) && get(p, end, m_tmp1) && get(p, end, m_tmp2) && get(p, end, m_output_queue_state) && get(p, end, m_icount) && get(p, end, m_input_state) && get(p, end, m_if) && get(p, end, m_irq) && get(p, end, m_mmu_bank) && get(p, end, m_mmu_enable);
}

void mn1880023_device::save_state(std::vector<u8> &v) const
{
	using lemus_state::put;
	mn1880_device::save_state(v);
	put(v, m_port_input_mask);
	put(v, m_regs);
	put(v, m_timer2_start.clocks);
	put(v, m_timer4_count);
	put(v, m_timer4_prescale);
	put(v, m_timer2_capture_input);
	put(v, m_timer4_count_input);
	put(v, m_uart_tx_data);
	put(v, m_uart_rx_data);
	put(v, m_uart_tx_bits);
	put(v, m_uart_rx_bits);
	put(v, m_uart_rxd);
	put(v, m_uart_txd);
	put(v, m_timer0_reloading);
	put(v, m_timer1_out);
	put(v, m_adc_channel_register);
	put(v, m_uart_tx_byte);
	put(v, u32(m_rx_queue.size()));
	for (u8 b : m_rx_queue)
		put(v, b);
	put(v, m_rx_line_byte);
	put(v, m_rx_line_bit);
}

bool mn1880023_device::load_state(const u8 *&p, const u8 *end)
{
	using lemus_state::get;
	if (!mn1880_device::load_state(p, end))
		return false;
	u32 queued = 0;
	if (!(get(p, end, m_port_input_mask) && get(p, end, m_regs) && get(p, end, m_timer2_start.clocks) && get(p, end, m_timer4_count) && get(p, end, m_timer4_prescale) && get(p, end, m_timer2_capture_input) && get(p, end, m_timer4_count_input) && get(p, end, m_uart_tx_data) && get(p, end, m_uart_rx_data) && get(p, end, m_uart_tx_bits) && get(p, end, m_uart_rx_bits) && get(p, end, m_uart_rxd) && get(p, end, m_uart_txd) && get(p, end, m_timer0_reloading) && get(p, end, m_timer1_out) && get(p, end, m_adc_channel_register) && get(p, end, m_uart_tx_byte) && get(p, end, queued)))
		return false;
	m_rx_queue.clear();
	for (u32 i = 0; i < queued; i++)
	{
		u8 b = 0;
		if (!get(p, end, b))
			return false;
		m_rx_queue.push_back(b);
	}
	return get(p, end, m_rx_line_byte) && get(p, end, m_rx_line_bit);
}
