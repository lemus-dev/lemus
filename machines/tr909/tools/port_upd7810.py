#!/usr/bin/env python3
"""Builds core/upd7810/ from MAME's uPD7810 core in private/reference/mame (BSD-3-Clause,
Juergen Buchmueller).

The opcode and table files are copied unchanged. upd7810.h and upd7810.cpp keep MAME's code
for the uPD7810/7811 only (no 7801/7807/78C05 variants), with the MAME device framework
replaced by plain members: a Bus interface for program memory, callbacks from emu.h, and
run(cycles) in place of execute_run. FIXES below corrects MAME where the TR-909 depends on it.
Re-run after updating the MAME sources (DST can be given as the first argument).
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SRC = os.path.join(ROOT, "private/reference/mame/src/devices/cpu/upd7810")
DST = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "core/upd7810")

NOTE = ("// Ported from MAME (src/devices/cpu/upd7810) by tools/port_upd7810.py; MAME's licence and\n"
        "// copyright notice are kept above. Changes: MAME device framework replaced, see emu.h;\n"
        "// corrections marked \"Lemus\" (the FIXES in tools/port_upd7810.py say why).\n")


def read(name):
    with open(os.path.join(SRC, name)) as f:
        return f.read().split("\n")


def write(name, text):
    with open(os.path.join(DST, name), "w") as f:
        f.write(text)


def func_range(lines, signature):
    """Line range [start, end) of a top-level function whose definition starts with signature."""
    start = next(i for i, l in enumerate(lines) if l.startswith(signature))
    end = next(i for i in range(start + 1, len(lines)) if lines[i] == "}") + 1
    return start, end


def functions(lines, signatures):
    out = []
    for sig in signatures:
        a, b = func_range(lines, sig)
        out += lines[a:b] + [""]
    return out


# ---------------------------------------------------------------- header
h = read("upd7810.h")
first_op = next(i for i, l in enumerate(h) if l.strip() == "int m_icount;") + 1
end_cls = next(i for i in range(first_op, len(h)) if h[i] == "};")
ops = h[first_op:end_cls]
ops = [l for l in ops if l.strip() not in ("void base_device_start();",)]

# members from "address_space_config m_program_config" to "int m_icount;"
m0 = next(i for i, l in enumerate(h) if "address_space_config m_program_config" in l)
m1 = next(i for i, l in enumerate(h) if l.strip() == "int m_icount;")
members = []
for l in h[m0:m1 + 1]:
    if "address_space_config" in l or "memory_view" in l:
        continue
    l = l.replace("memory_access<16, 0, 0, ENDIANNESS_LITTLE>::cache m_opcodes;", "space m_opcodes;")
    l = l.replace("memory_access<16, 0, 0, ENDIANNESS_LITTLE>::specific m_program;", "space m_program;")
    l = l.replace("memory_access<16, 0, 0, ENDIANNESS_LITTLE>::specific m_io;", "io_space m_io;")
    members.append(l)

flags = h[next(i for i, l in enumerate(h) if l.strip() == "// flags"):
          next(i for i, l in enumerate(h) if "upd7810_device(const machine_config &mconfig, device_type type" in l)]

header = "\n".join(h[0:2]) + "\n" + NOTE + """#ifndef UPD7810_UPD7810_H
#define UPD7810_UPD7810_H

#pragma once

#include "emu.h"

""" + "\n".join(h[15:37]) + """


// uPD7810/7811 CPU. Program memory goes through a Bus; the 256 bytes of internal RAM at
// 0xff00-0xffff (enabled by MM bit 3) and, on the 7811, the 4 KB internal ROM at 0x0000
// (only when the mode pins select it) are handled here.
class upd7810_device
{
public:
	struct Bus
	{
		virtual ~Bus() = default;
		virtual u8 read(u16 addr) = 0;
		virtual void write(u16 addr, u8 data) = 0;
	};

	upd7810_device();

	void set_bus(Bus *bus) { m_bus = bus; }
	void set_internal_rom(const u8 *rom4k) { m_irom = rom4k; }  // nullptr = external only
	void reset() { device_reset(); }
	// Adds `cycles` states (clock / 3) to the budget and runs whole instructions while it lasts
	// (the last may overrun it; the overrun comes off the next call); returns the states run.
	int run(int cycles);
	void set_input_line(int line, int state) { execute_set_input(line, state); }
	u16 pc() const { return m_pc.w.l; }
	u16 ppc() const { return m_ppc.w.l; }
	bool halted() const { return m_halt != 0; }
	u8 iram(int i) const { return m_iram[i & 0xff]; }
	// States left in the current run() slice (negative once it has overrun); a callback can
	// compute its time within the slice from it.
	int icount() const { return m_icount; }
	// Port output latches and mode registers, for the board model.
	u8 pa_out() const { return m_pa_out; }
	u8 pb_out() const { return m_pb_out; }
	u8 pc_out() const { return m_pc_out; }
	u8 pf_out() const { return m_pf_out; }
	u8 mc() const { return m_mc; }
	u8 mcc() const { return m_mcc; }
	u8 to_level() const { return m_to; }
	devcb_read8 &an_func(int i) { return m_an_func[size_t(i & 7)]; }
	// Hands the serial port a received byte (async mode), as its receiver does at the end of
	// the stop bit: RXB = data and INTSR, if the receiver is enabled. The board model times
	// the bytes itself instead of driving RxD bit by bit.
	void sio_rx_byte(u8 data) { if (m_smh & 0x08) { m_rxb = data; m_irr |= INTFSR; } }
	// Called with the PC before each instruction when set (tools only).
	std::function<void(u16)> &trace() { return m_trace; }

	// configuration helpers
""" + "\n".join(re.sub(r"auto (\w+)\(\) \{ return (m_\w+(?:\[\d\])?)\.bind\(\); \}", r"auto &\1() { return \2; }", l)
                for l in h[46:79] if ".bind()" in l or l.strip() == "") + """

	void pa_w(uint8_t data, uint8_t mem_mask = ~0);
	void pb_w(uint8_t data, uint8_t mem_mask = ~0);
	void pc_w(uint8_t data, uint8_t mem_mask = ~0);
	void pd_w(uint8_t data, uint8_t mem_mask = ~0);
	void pf_w(uint8_t data, uint8_t mem_mask = ~0);

	void sck_w(int state);

	// set_pa_pullups() etc. as in MAME
	void set_pa_pullups(uint8_t p) { m_pa_pullups = p; }
	void set_pb_pullups(uint8_t p) { m_pb_pullups = p; }
	void set_pc_pullups(uint8_t p) { m_pc_pullups = p; }
	void set_pd_pullups(uint8_t p) { m_pd_pullups = p; }
	void set_pf_pullups(uint8_t p) { m_pf_pullups = p; }

protected:
""" + "\n".join(flags) + """
	struct space
	{
		upd7810_device *cpu;
		u8 read_byte(offs_t a) { return cpu->mem_read(a & 0xffff); }
		void write_byte(offs_t a, u8 d) { cpu->mem_write(a & 0xffff, d); }
	};
	struct io_space  // 7801 IN/OUT only
	{
		u8 read_byte(offs_t) { return 0xff; }
		void write_byte(offs_t, u8) {}
	};
	struct ram_view_t
	{
		bool on = true;
		void select(int) { on = true; }
		void disable() { on = false; }
	};

	u8 mem_read(u16 a)
	{
		if (a >= 0xff00 && m_ram_view.on) return m_iram[a & 0xff];
		if (a < 0x1000 && m_irom) return m_irom[a];
		return m_bus->read(a);
	}
	void mem_write(u16 a, u8 d)
	{
		if (a >= 0xff00 && m_ram_view.on) { m_iram[a & 0xff] = d; return; }
		if (a < 0x1000 && m_irom) return;
		m_bus->write(a, d);
	}
	const char *tag() const { return "maincpu"; }
	void standard_irq_callback(int, u16) {}

	void device_start();
	void device_reset();
	void execute_run();
	void execute_set_input(int inputnum, int state);

	void update_sio(int cycles);
	void handle_timers(int cycles);
	void upd7810_take_irq();

	void upd7810_handle_timer0(int cycles, int clkdiv);
	void upd7810_handle_timer1(int cycles, int clkdiv);

	void upd7810_to_output_change(int state);
	void upd7810_co0_output_change();
	void upd7810_co1_output_change();

	devcb_write_line  m_to_func;
	devcb_write_line  m_co0_func;
	devcb_write_line  m_co1_func;
	devcb_write_line  m_txd_func;
	devcb_read_line   m_rxd_func;
	devcb_read8::array<8> m_an_func;

	devcb_read8       m_pa_in_cb;
	devcb_read8       m_pb_in_cb;
	devcb_read8       m_pc_in_cb;
	devcb_read8       m_pd_in_cb;
	devcb_read8       m_pf_in_cb;
	devcb_write8      m_pa_out_cb;
	devcb_write8      m_pb_out_cb;
	devcb_write8      m_pc_out_cb;
	devcb_write8      m_pd_out_cb;
	devcb_write8      m_pf_out_cb;

	devcb_read8       m_pt_in_cb; // TODO: uPD7807 only

	typedef void (upd7810_device::*opcode_func)();

	struct opcode_s {
		opcode_func opfunc;
		uint8_t oplen;
		uint8_t cycles;
		uint8_t cycles_skip;
		uint8_t mask_l0_l1;
	};

	void configure_ops();
	uint8_t read_pc();
	void write_pc(uint8_t data);
	void write_smh(uint8_t data);

""" + "\n".join(l for l in h[206:240]) + """

	Bus *m_bus = nullptr;
	std::function<void(u16)> m_trace;
	const u8 *m_irom = nullptr;
	u8 m_iram[256] = {};
	ram_view_t m_ram_view;

""" + "\n".join(members) + "\n\n" + "\n".join(ops) + """
};

#endif // UPD7810_UPD7810_H
"""
write("upd7810.h", header)

# ---------------------------------------------------------------- core
c = read("upd7810.cpp")
inc = c.index('#include "upd7810_dasm.h"')
body = functions(c, [
    "void upd7810_device::configure_ops()",
    "void upd7810_device::pa_w(", "void upd7810_device::pb_w(", "void upd7810_device::pc_w(",
    "void upd7810_device::pd_w(", "void upd7810_device::pf_w(", "void upd7810_device::sck_w(",
    "uint8_t upd7810_device::RP(", "uint8_t upd7810_device::read_pc()",
    "void upd7810_device::WP(", "void upd7810_device::write_pc(", "void upd7810_device::write_smh(",
    "void upd7810_device::upd7810_take_irq()",
    "void upd7810_device::upd7810_to_output_change(", "void upd7810_device::upd7810_co0_output_change()",
    "void upd7810_device::upd7810_co1_output_change()", "void upd7810_device::upd7810_write_EOM()",
    "void upd7810_device::upd7810_write_TXB()",
])
a, _ = func_range(c, "void upd7810_device::upd7810_write_TXB()")
par = [l for l in c[a:a + 10] if l.startswith("#define PAR")]
body += par + [""]
body += functions(c, [
    "void upd7810_device::upd7810_sio_output()", "void upd7810_device::upd7810_sio_input()",
    "void upd7810_device::upd7810_handle_timer0(", "void upd7810_device::upd7810_handle_timer1(",
    "void upd7810_device::update_sio(", "void upd7810_device::handle_timers(",
    "void upd7810_device::device_reset()", "void upd7810_device::execute_run()",
    "void upd7810_device::execute_set_input(",
])
text = "\n".join(body)
text = text.replace("\t\t\tdebugger_wait_hook();\n", "").replace("\t\tdebugger_instruction_hook(PC);\n", "\t\tif (m_trace) m_trace(PC);\n")
# the 7810 only has ports A-F; RP() still names the 7807's port T
text = text.replace("\tcase UPD7807_PORTT: // NS20031301 partial implementation\n\t\tdata = m_pt_in_cb();\n\t\tbreak;\n", "")

# FIXES: corrections to MAME's peripherals, each with its reason. A fix that no longer applies
# (MAME changed the code) stops the port, so it is looked at again.
FIXES = [
    # Serial transmit. MAME raises INTST when the whole frame has been shifted out and starts the
    # next frame one bit time after the CPU has refilled TXB, so back-to-back bytes go out 11 bit
    # times apart. The uPD7810's transmitter is double buffered: TXB moves to the shift register
    # as soon as the previous frame's stop bit ends, and INTST means "TXB empty" (NEC 87AD
    # uPD78C18 user's manual, ch. 7, which shares the 7810's serial unit: "When the transmit
    # buffer register becomes empty, a serial transmission interrupt (INTST) is generated"; INTST
    # comes with the stop bit). The TR-909 sends MIDI from its INTST handler, so its bytes follow
    # each other at 10 bit times (320 us) as on the hardware.
    ("""	/* shift out more bits? */
	if (m_txcnt > 0)
	{
		TXD = m_txs & 1;
		m_txd_func(TXD);
		if (m_mcc & 0x01)
			WP(UPD7810_PORTC, m_pc_out);
		m_txs >>= 1;
		m_txcnt--;
		if (0 == m_txcnt)
			IRR |= INTFST;      /* serial transfer completed */
	}
	else
	if (SMH & 0x04) /* send enable ? */
	{
		/* nothing written into the transmitter buffer ? */
		if (0 == m_txbuf)
			return;
		m_txbuf = 0;
""", """	/* frame done: load the transmit buffer into the shift register, if it holds data */
	if (m_txcnt == 0)
	{
		/* send disabled, or nothing written into the transmitter buffer ? */
		if (0 == (SMH & 0x04) || 0 == m_txbuf)
			return;
		m_txbuf = 0;
		IRR |= INTFST;          /* transmit buffer empty (Lemus: was at the end of the frame) */
"""),
    ("""			/* synchronous mode */
			m_txs = TXB;
			m_txcnt = 8;
		}
	}
}
""", """			/* synchronous mode */
			m_txs = TXB;
			m_txcnt = 8;
		}
	}

	/* shift out the next bit: the start bit follows the previous stop bit directly */
	if (m_txcnt == 0)
		return;
	TXD = m_txs & 1;
	m_txd_func(TXD);
	if (m_mcc & 0x01)
		WP(UPD7810_PORTC, m_pc_out);
	m_txs >>= 1;
	m_txcnt--;
}
"""),
    # Interrupt acknowledge. MAME pushes PSW and PC and jumps to the vector without spending any
    # time. The CPU takes 16 states for it ("These interrupt operations are automatically carried
    # out in 16 states", NEC 87AD uPD78C18 user's manual, 9.4; 9.6 Table 9-2 component III), and
    # the timers, A/D converter and serial port run on meanwhile. The TR-909 takes an interrupt
    # every 192 us (A/D), so this is about 2 % of its CPU time, and the panel scan's length
    # decides whether it loses an A/D interrupt (the 1.92 / 2.11 ms scan periods).
    ("""		IFF = m_iff_pending = 0;
		IRR &= ~irr_mask;
		PSW &= ~(SK|L0|L1);
		PC = vector;
	}
}
""", """		IFF = m_iff_pending = 0;
		IRR &= ~irr_mask;
		PSW &= ~(SK|L0|L1);
		PC = vector;
		handle_timers(16);      /* Lemus: the acknowledge takes 16 states */
		m_icount -= 16;
	}
}
"""),
]
for old, new in FIXES:
    if old not in text:
        sys.exit("port_upd7810.py: a fix no longer matches MAME's code:\n" + old)
    text = text.replace(old, new)

core = "\n".join(c[0:2]) + "\n" + NOTE + """
#include "upd7810.h"

#include "upd7810_macros.h"


upd7810_device::upd7810_device()
	: m_pa_pullups(0xff)
	, m_pb_pullups(0xff)
	, m_pc_pullups(0xff)
	, m_pd_pullups(0xff)
	, m_pf_pullups(0xff)
{
	// MAME's default values for unset read callbacks
	m_rxd_func.dflt = 1;
	m_pa_in_cb.dflt = m_pb_in_cb.dflt = m_pc_in_cb.dflt = m_pd_in_cb.dflt = m_pf_in_cb.dflt = 0xff;
	m_opcodes.cpu = this;
	m_program.cpu = this;
	m_icount = 0;
	device_start();
}

void upd7810_device::device_start()
{
	configure_ops();
	MM = 8;
	m_ram_view.select(0);
}

int upd7810_device::run(int cycles)
{
	m_icount += cycles;
	// An instruction runs to its end, so a slice can overrun (m_icount < 0); the overrun is
	// paid from the next slice. execute_run() always runs at least one instruction, so it is
	// not called while the budget is still overdrawn: otherwise slices shorter than the last
	// overrun (a few states, at high sample rates or in tools) would run the CPU fast.
	if (m_icount <= 0)
		return 0;
	const int before = m_icount;
	execute_run();
	return before - m_icount;
}

""" + text + "\n"
write("upd7810.cpp", core)

# ---------------------------------------------------------------- opcodes and tables, unchanged
for name in ("upd7810_opcodes.cpp", "upd7810_table.cpp", "upd7810_macros.h"):
    t = "\n".join(read(name))
    t = t.replace('#include "emu.h"\n', "")
    lines = t.split("\n")
    write(name, "\n".join(lines[0:2]) + "\n" + NOTE + "\n".join(lines[2:]))
print("wrote", DST)
