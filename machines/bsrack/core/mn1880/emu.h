// Stand-ins for the parts of MAME's emu.h that its MN1880 core and disassembler use, so that
// MAME's code compiles outside MAME as plain C++17 (written by tools/port_mn1880.py). Time is
// counted in oscillator clocks on the CPU's own time line, not in attoseconds: nothing here reads
// a wall clock.
#pragma once

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <ostream>
#include <utility>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using offs_t = uint32_t;

#define BIT(x, n) (((x) >> (n)) & 1)
#define ATTR_COLD
#define FUNC(f) &f
#define TIMER_CALLBACK_MEMBER(name) void name([[maybe_unused]] s32 param)

#if defined(__GNUC__) || defined(__clang__)
#define MAME_PRINTF(fmt, first) __attribute__((format(printf, fmt, first)))
#else
#define MAME_PRINTF(fmt, first)
#endif

enum { CLEAR_LINE = 0, ASSERT_LINE = 1 };

// multibyte.h
inline u16 get_u16le(const u8 *p) { return u16(p[0] | p[1] << 8); }
inline void put_u16le(u8 *p, u16 v) { p[0] = u8(v); p[1] = u8(v >> 8); }
constexpr u16 swapendian_int16(u16 v) { return u16(v << 8 | v >> 8); }

// std::bit_width, which is C++20
constexpr unsigned mame_bit_width(unsigned x)
{
	unsigned n = 0;
	for (; x != 0; x >>= 1)
		++n;
	return n;
}

// A frequency as a divisor of the CPU's oscillator: MAME's clock() and clock() / n.
struct osc_clock
{
	u32 div = 1;
	constexpr osc_clock operator/(u32 d) const { return osc_clock{div * d}; }
};

// MAME's attotime, as a count of oscillator clocks. Everything the MN1880023 times is a whole
// number of clocks, so this is exact. never is the largest count.
struct attotime
{
	u64 clocks = 0;

	static const attotime never;
	static const attotime zero;

	constexpr bool is_never() const { return clocks == ~u64(0); }
	constexpr attotime operator/(u32 d) const { return is_never() ? *this : attotime{clocks / d}; }
	constexpr attotime operator-(const attotime &b) const { return is_never() ? *this : attotime{clocks - b.clocks}; }
	constexpr u64 as_ticks(osc_clock f) const { return clocks / f.div; }
};
inline const attotime attotime::never{~u64(0)};
inline const attotime attotime::zero{0};

class mn1880_device;

// MAME's devcb callbacks as std::function. An unset read returns MAME's default value.
struct devcb_write_line
{
	std::function<void(int)> f;
	devcb_write_line() = default;
	explicit devcb_write_line(mn1880_device &) {}
	devcb_write_line &operator=(std::function<void(int)> fn) { f = std::move(fn); return *this; }
	void operator()(int state) { if (f) f(state); }
	bool isunset() const { return !f; }
};

struct devcb_read8
{
	std::function<u8()> f;
	u8 dflt = 0;
	devcb_read8 &operator=(std::function<u8()> fn) { f = std::move(fn); return *this; }
	u8 operator()() { return f ? f() : dflt; }
	bool isunset() const { return !f; }

	template <int N> struct array : std::array<devcb_read8, N>
	{
		array(mn1880_device &, u8 dflt) { for (devcb_read8 &cb : *this) cb.dflt = dflt; }
	};
};

struct devcb_write8
{
	std::function<void(u8)> f;
	devcb_write8 &operator=(std::function<void(u8)> fn) { f = std::move(fn); return *this; }
	void operator()(u8 data) { if (f) f(data); }
	bool isunset() const { return !f; }

	template <int N> struct array : std::array<devcb_write8, N>
	{
		explicit array(mn1880_device &) {}
	};
};

namespace util {

// printf-style formatting into a stream (MAME's util::stream_format)
inline void stream_format(std::ostream &stream, const char *format, ...) MAME_PRINTF(2, 3);
inline void stream_format(std::ostream &stream, const char *format, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, format);
	std::vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	stream << buf;
}

// MAME's disassembler interface. disassemble() returns the length (LENGTHMASK) and the flags.
class disasm_interface
{
public:
	enum : u32
	{
		STEP_COND = 0x10000000,
		STEP_OVER = 0x20000000,
		STEP_OUT = 0x40000000,
		SUPPORTED = 0x80000000,
		LENGTHMASK = 0x0000ffff
	};

	// Program bytes; r16 is big-endian, as MAME's MN1880 program space.
	class data_buffer
	{
	public:
		explicit data_buffer(std::function<u8(offs_t)> read) : m_read(std::move(read)) {}
		u8 r8(offs_t pc) const { return m_read(pc & 0xffff); }
		u16 r16(offs_t pc) const { return u16(r8(pc) << 8 | r8(pc + 1)); }

	private:
		std::function<u8(offs_t)> m_read;
	};

	virtual ~disasm_interface() = default;
	virtual u32 opcode_alignment() const = 0;
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) = 0;
};

} // namespace util
