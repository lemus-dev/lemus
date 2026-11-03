// Minimal stand-ins for the parts of MAME's emu.h that the uPD7810 core uses, so MAME's
// upd7810_opcodes.cpp and upd7810_table.cpp compile unchanged outside MAME.
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using offs_t = uint32_t;

// MAME's PAIR (little-endian host)
union PAIR
{
	struct { u8 l, h, h2, h3; } b;
	struct { u16 l, h; } w;
	struct { s8 l, h, h2, h3; } sb;
	struct { s16 l, h; } sw;
	u32 d;
	s32 sd;
};

#define BIT(x, n) (((x) >> (n)) & 1)
#define COMBINE_DATA(varptr) (*(varptr) = (*(varptr) & ~mem_mask) | (data & mem_mask))
#define ATTR_COLD

enum { CLEAR_LINE = 0, ASSERT_LINE = 1 };
enum { INPUT_LINE_NMI = 32 };

// Logging is off unless UPD7810_LOG is defined.
#ifdef UPD7810_LOG
#define logerror(...) std::fprintf(stderr, __VA_ARGS__)
#else
#define logerror(...) do {} while (0)
#endif

[[noreturn]] inline void fatalerror(const char *msg)
{
	std::fprintf(stderr, "%s", msg);
	std::abort();
}

// Callbacks in place of MAME's devcb. Unset callbacks return the MAME default.
struct devcb_write_line
{
	std::function<void(int)> f;
	devcb_write_line &operator=(std::function<void(int)> fn) { f = std::move(fn); return *this; }
	void operator()(int state) { if (f) f(state); }
	bool isunset() const { return !f; }
};

struct devcb_read_line
{
	std::function<int()> f;
	int dflt = 0;
	devcb_read_line &operator=(std::function<int()> fn) { f = std::move(fn); return *this; }
	int operator()() { return f ? f() : dflt; }
	bool isunset() const { return !f; }
};

struct devcb_read8
{
	std::function<u8()> f;
	u8 dflt = 0;
	devcb_read8 &operator=(std::function<u8()> fn) { f = std::move(fn); return *this; }
	u8 operator()() { return f ? f() : dflt; }
	u8 operator()(offs_t, u8) { return (*this)(); }
	bool isunset() const { return !f; }
	template <int N> using array = std::array<devcb_read8, N>;
};

struct devcb_write8
{
	std::function<void(u8)> f;
	devcb_write8 &operator=(std::function<void(u8)> fn) { f = std::move(fn); return *this; }
	void operator()(u8 data) { if (f) f(data); }
	bool isunset() const { return !f; }
};
