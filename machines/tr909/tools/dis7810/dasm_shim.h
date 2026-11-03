// Stand-ins for MAME's util::disasm_interface so upd7810_dasm.cpp compiles unchanged.
#pragma once
#include "../../core/upd7810/emu.h"
#include <cstdarg>
#include <ostream>
#include <string>

namespace util {
class disasm_interface
{
public:
	enum : u32 { SUPPORTED = 0x80000000, STEP_OVER = 0x20000000, STEP_OUT = 0x40000000, LENGTHMASK = 0x0000ffff };
	class data_buffer
	{
	public:
		explicit data_buffer(const u8 *mem) : m(mem) {}
		u8 r8(offs_t a) const { return m[a & 0xffff]; }
	private:
		const u8 *m;
	};
	virtual ~disasm_interface() = default;
	virtual u32 opcode_alignment() const = 0;
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) = 0;
};
inline void stream_format(std::ostream &s, const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	s << buf;
}
}
