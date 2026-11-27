#!/usr/bin/env python3
"""Builds core/mn1880/ from MAME's MN1880 core in private/reference/mame (BSD-3-Clause, AJR).

mn1880d.h and mn1880d.cpp (the disassembler, MN1870/MN1860 tables included) are copied unchanged
but for an include and [[maybe_unused]] on the one unused parameter (MAME builds without
-Wunused-parameter). mn1880.h and mn1880.cpp keep MAME's MN1880 core and its MN1880023 peripherals
(the MN18801A and its MMU registers are left out), with the MAME device framework replaced by
plain C++17:
  - a Bus interface for the program space and for the data space above the on-chip registers and
    RAM; the on-chip register map becomes switch statements generated from MAME's internal_map();
  - run(cycles) in place of execute_run, with MAME's timers (emu_timer, attotime) counted in
    oscillator clocks on the CPU's own time line and fired between cycles, where MAME's scheduler
    fires them: deterministic, no wall clock;
  - devcb callbacks as std::function, logerror and the debugger hook as optional callbacks;
    save states and debugger state left out.
Additions inside MAME's functions are listed in LEMUS below and marked "LEMUS" in the output.
Re-run after updating the MAME sources (DST can be given as the first argument); the output
depends only on them.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SRC = os.path.join(ROOT, "private/reference/mame")
DST = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "core/mn1880")

NOTE = ("// Ported from MAME (src/devices/cpu/mn1880) by tools/port_mn1880.py; MAME's licence and\n"
        "// copyright notice are kept above. Changes: MAME device framework replaced (emu.h and the\n"
        "// interface in mn1880.h); additions inside MAME's code are marked \"LEMUS\".\n")


def die(msg):
    sys.exit("port_mn1880.py: " + msg)


def read(name):
    with open(os.path.join(SRC, name)) as f:
        return f.read().split("\n")


def write(name, text):
    os.makedirs(DST, exist_ok=True)
    with open(os.path.join(DST, name), "w", newline="\n") as f:
        f.write(text)


def tabs(text):
    """Templates here are indented with 4 spaces per level; MAME's code uses tabs."""
    out = []
    for line in text.split("\n"):
        n = len(line) - len(line.lstrip(" "))
        out.append("\t" * (n // 4) + " " * (n % 4) + line.lstrip(" "))
    return "\n".join(out)


def find(lines, text, start=0, end=None):
    for i in range(start, len(lines) if end is None else end):
        if lines[i] == text:
            return i
    die("not found in MAME's code: " + repr(text))


def definition(lines, signature):
    """[start, end) of the one top-level definition starting with signature, with the comment
    lines just above it."""
    starts = [i for i, l in enumerate(lines) if l.startswith(signature)]
    if len(starts) != 1:
        die("expected one definition of %r, found %d" % (signature, len(starts)))
    a = starts[0]
    b = next(i for i in range(a + 1, len(lines)) if lines[i] in ("}", "};")) + 1
    while a > 0 and lines[a - 1].startswith("//") and not lines[a - 1].startswith("//*"):
        a -= 1
    return a, b


def replace(text, old, new):
    n = text.count(old)
    if n != 1:
        die("expected one %r in MAME's code, found %d" % (old, n))
    return text.replace(old, new)


def drop_lines(lines, drop):
    for d in drop:
        if lines.count(d) != 1:
            die("expected one line %r in MAME's code" % d)
    return [l for l in lines if l not in drop]


def squeeze(lines):
    """No two blank lines in a row, none at either end."""
    out = []
    for l in lines:
        if l.strip() == "" and (not out or out[-1].strip() == ""):
            continue
        out.append(l)
    while out and out[-1].strip() == "":
        out.pop()
    return out


h = read("mn1880.h")
c = read("mn1880.cpp")

# ---------------------------------------------------------------- the on-chip register maps
# Handler arity, from the declarations: MAME passes an offset from the map entry's start only to
# handlers that take one.
DECL = re.compile(r"^\t(?:virtual )?(?:u8|u16|void) (\w+)\(([^)]*)\)")
takes_offset = {}
for l in h:
    m = DECL.match(l)
    if m:
        takes_offset[m.group(1)] = m.group(2).startswith("offs_t offset")

MAP = re.compile(r"^\tmap\((0x[0-9a-f]+), (0x[0-9a-f]+)\)\.(.*);$")
HANDLER = re.compile(r"FUNC\((\w+)::(\w+)\)")


def parse_map(signature):
    """map() entries in order (a later entry overrides an earlier one, as in MAME); the MMU
    registers (MN18801A only) are left out."""
    a, b = definition(c, signature)
    body = c[a:b]
    body = body[next(i for i, l in enumerate(body) if l == "{") + 1:-1]
    entries, skip = [], False
    for l in body:
        s = l.strip()
        if skip:
            skip = s != "}"
            continue
        if s == "if (m_has_mmu)":
            skip = True
            continue
        if s == "mn1880_device::internal_data_map(map);":
            entries += parse_map("void mn1880_device::internal_data_map(address_map &map)")
            continue
        m = MAP.match(l)
        if not m:
            die("unexpected line in an internal map: " + repr(l))
        start, end, how = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
        e = {"start": start, "end": end, "ram": False, "r": None, "w": None}
        hs = HANDLER.findall(how)
        if how == "ram()":
            e["ram"] = True
        elif how.startswith("rw(") and len(hs) == 2:
            e["r"], e["w"] = hs
        elif how.startswith("r(") and len(hs) == 1:
            e["r"] = hs[0]
        elif how.startswith("w(") and len(hs) == 1:
            e["w"] = hs[0]
        else:
            die("unexpected map entry: " + repr(l))
        for hcls, name in hs:
            if name not in takes_offset:
                die("no declaration found for handler " + name)
        entries.append(e)
    return entries


def resolve(entries, io_end):
    rd, wr, ram = {}, {}, None
    for e in entries:
        if e["ram"]:
            if e["start"] <= io_end:
                die("internal RAM inside the register range")
            ram = (e["start"], e["end"])
            continue
        for a in range(e["start"], e["end"] + 1):
            if a > io_end:
                die("register map entry above the internal I/O end")
            if e["r"]:
                rd[a] = (e["r"][0], e["r"][1], e["start"])
            if e["w"]:
                wr[a] = (e["w"][0], e["w"][1], e["start"])
    return rd, wr, ram


def emit_dispatch(cls, table, write_side):
    """A switch over the register addresses, one group per handler (and entry start)."""
    groups = {}
    for a in sorted(table):
        hcls = table[a][0]
        groups.setdefault(table[a] if hcls == cls else (hcls, None, None), []).append(a)
    fn = "internal_w(offs_t offset, u8 data)" if write_side else "internal_r(offs_t offset, u8 &data)"
    out = ["bool %s::%s" % (cls, fn), "{", "\tswitch (offset)", "\t{"]
    for (hcls, name, start), addrs in sorted(groups.items(), key=lambda g: g[1][0]):
        labels = ["case 0x%02x:" % a for a in addrs]
        for i in range(0, len(labels), 8):
            out.append("\t" + " ".join(labels[i:i + 8]))
        if hcls != cls:
            out.append("\t\treturn %s::%s;" % (hcls, "internal_w(offset, data)" if write_side else "internal_r(offset, data)"))
            continue
        off = ("offset - 0x%02x" % start if start else "offset") if takes_offset[name] else ""
        if write_side:
            out.append("\t\t%s(%s);" % (name, off + ", data" if off else "data"))
        else:
            out.append("\t\tdata = %s(%s);" % (name, off))
        out.append("\t\treturn true;")
    out += ["\tdefault:", "\t\treturn false;", "\t}", "}", ""]
    return out


def ctor_io_end(signature, model):
    a, b = definition(c, signature)
    m = re.search(r"mn1880_device\(mconfig, %s, tag, owner, clock, false, (0x[0-9a-f]+), " % model, "\n".join(c[a:b]))
    if not m:
        die("can't read the internal I/O end of " + model)
    return int(m.group(1), 16)


BASE_IO_END = ctor_io_end("mn1880_device::mn1880_device(const machine_config &mconfig, const char *tag,", "MN1880")
M23_IO_END = ctor_io_end("mn1880023_device::mn1880023_device(", "MN1880023")
base_rd, base_wr, base_ram = resolve(parse_map("void mn1880_device::internal_data_map(address_map &map)"), BASE_IO_END)
m23_rd, m23_wr, m23_ram = resolve(parse_map("void mn1880023_device::internal_map(address_map &map)"), M23_IO_END)
if base_ram is not None or m23_ram is None:
    die("unexpected internal RAM layout")

# ---------------------------------------------------------------- emu.h
EMU_H = tabs("""// Stand-ins for the parts of MAME's emu.h that its MN1880 core and disassembler use, so that
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
""")

# ---------------------------------------------------------------- mn1880.h
cls0 = find(h, "class mn1880_device : public cpu_device")
cls1 = find(h, "class mn1880023_device : public mn1880_device")
cls2 = find(h, "class mn18801a_device : public mn1880_device")

# mn1880_device: MAME's private section, less the address space configs and the MMU registers
priv0 = find(h, "private:", cls0, cls1)
end0 = max(i for i in range(cls0, cls1) if h[i] == "};")
base_private = drop_lines(h[priv0 + 1:end0], [
    "\taddress_space_config m_program_config;",
    "\taddress_space_config m_data_config;",
    "\tconst bool m_has_mmu;",
    "\tu8 mmu_bank_r(offs_t offset);",
    "\tvoid mmu_bank_w(offs_t offset, u8 data);",
    "\tu8 mmu_enable_r();",
    "\tvoid mmu_enable_w(u8 data);",
])
SPACES = {
    "\tmemory_access<16, 0, 0, ENDIANNESS_BIG>::cache m_cache;": "\tprogram_space m_cache{this};",
    "\tmemory_access<16, 0, 0, ENDIANNESS_LITTLE>::specific m_data;": "\tdata_space m_data{this};",
}
for old in SPACES:
    if base_private.count(old) != 1:
        die("expected one line %r in MAME's header" % old)
base_private = squeeze([SPACES.get(l, l) for l in base_private])

irq0 = find(h, "\t// on-chip peripheral interrupt requests", cls0, cls1)
base_irq = h[irq0:irq0 + 3]
if not base_irq[2].startswith("\tvoid clear_interrupt_requests("):
    die("MAME's interrupt request helpers moved")

# mn1880023_device
pub1 = find(h, "public:", cls1, cls2)
prot1 = find(h, "protected:", cls1, cls2)
priv1 = find(h, "private:", cls1, cls2)
end1 = max(i for i in range(cls1, cls2) if h[i] == "};")
m23_public = drop_lines(h[pub1 + 1:prot1], [
    "\tmn1880023_device(const machine_config &config, const char *tag, device_t *owner, u32 clock);"])
m23_public = squeeze([re.sub(r"auto (\w+)\(\) \{ return (m_\w+(?:\[\w+\])?)\.bind\(\); \}",
                             r"auto &\1() { return \2; }", l) for l in m23_public])
if any(".bind()" in l for l in m23_public):
    die("a callback helper didn't convert")
m23_private = squeeze(drop_lines(h[priv1 + 1:end1], ["\tvoid internal_map(address_map &map) ATTR_COLD;"]))

HEADER = "\n".join(h[0:2]) + "\n" + NOTE + tabs("""
#ifndef LEMUS_MN1880_MN1880_H
#define LEMUS_MN1880_MN1880_H

#pragma once

#include "emu.h"

#include <array>
#include <deque>
#include <functional>
#include <vector>

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

@BASE_IRQ@

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
@BASE_PRIVATE@
};


// MN1880023 (Drum Station, CR-504, PSR-500): MAME's peripherals, register use inferred from
// that firmware.
class mn1880023_device : public mn1880_device
{
public:
    mn1880023_device();

@M23_PUBLIC@

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
@M23_PRIVATE@

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
""")
HEADER = (HEADER.replace("@BASE_IRQ@", "\n".join(base_irq))
          .replace("@BASE_PRIVATE@", "\n".join(base_private))
          .replace("@M23_PUBLIC@", "\n".join(m23_public))
          .replace("@M23_PRIVATE@", "\n".join(m23_private)))

# ---------------------------------------------------------------- mn1880.cpp
desc_end = find(c, "***************************************************************************/")
description = c[2:desc_end + 1]

# base constructor: MAME's initialisers, less the framework's
a, b = definition(c, "mn1880_device::mn1880_device(const machine_config &mconfig, device_type type,")
init = c[a + 1:b]
init = drop_lines(init, [
    "\t: cpu_device(mconfig, type, tag, owner, clock)",
    '\t, m_program_config("program", ENDIANNESS_BIG, 8, has_mmu ? 21 : 16, 0, 16, 14)',
    '\t, m_data_config("data", ENDIANNESS_LITTLE, 8, has_mmu ? 21 : 16, 0, 16, 14, data_map)',
])
init = drop_lines(init, ["\t, m_has_mmu(has_mmu)"])  # always false here: no MMU without the MN18801A
if init[-2:] != ["{", "}"]:
    die("MAME's base constructor changed")
init = init[:-2]
base_ctor = (["mn1880_device::mn1880_device(u16 internal_io_end, u16 ram_start, u16 ram_end)",
              "\t: m_ram_start(ram_start)"] + init +
             ["{", "\tset_ram_end(ram_end);", "}", "",
              "mn1880_device::mn1880_device()",
              "\t: mn1880_device(0x%02x, 0x0060, 0x005f)" % BASE_IO_END,
              "{", "\tdevice_start();", "}", ""])

BASE_FUNCS = [
    "void mn1880_device::execute_set_input(",
    "u8 mn1880_device::ie0_r()", "void mn1880_device::ie0_w(",
    "u8 mn1880_device::ie1_r()", "void mn1880_device::ie1_w(",
    "u8 mn1880_device::cpum_r()", "void mn1880_device::cpum_w(",
    "void mn1880_device::device_reset()",
    "const mn1880_device::microstate mn1880_device::s_decode_map[256] =",
    "const u16 mn1880_device::s_input_queue_map[16] =",
    "const u8 mn1880_device::s_branch_fs[4] =",
    "u8 mn1880_device::cpu_registers::addcz(", "u8 mn1880_device::cpu_registers::adddcz(",
    "u8 mn1880_device::cpu_registers::subcz(", "u8 mn1880_device::cpu_registers::subdcz(",
    "u8 mn1880_device::cpu_registers::rolc(", "u8 mn1880_device::cpu_registers::rorc(",
    "u8 mn1880_device::cpu_registers::asrc(", "void mn1880_device::cpu_registers::branch(",
    "void mn1880_device::swap_cpus()", "void mn1880_device::next_instruction(",
    "void mn1880_device::execute_run()",
]
base_body = []
for sig in BASE_FUNCS:
    a, b = definition(c, sig)
    base_body += c[a:b] + [""]
base_text = "\n".join(base_body)
base_text = replace(base_text, "std::bit_width(", "mame_bit_width(")

# MN1880023: constructor, then everything from device_start() to the MN18801A section
a, b = definition(c, "mn1880023_device::mn1880023_device(")
m23_ctor = c[a:b]
first = "\t: mn1880_device(mconfig, MN1880023, tag, owner, clock, false, 0x%02x, address_map_constructor(FUNC(mn1880023_device::internal_map), this))" % M23_IO_END
if m23_ctor[1] != first or m23_ctor[-2:] != ["{", "}"]:
    die("MAME's MN1880023 constructor changed")
m23_ctor = (["mn1880023_device::mn1880023_device()",
             "\t: mn1880_device(0x%02x, 0x%04x, 0x%04x)" % (M23_IO_END, m23_ram[0], m23_ram[1])] +
            m23_ctor[2:-1] + ["\tdevice_start();", "}", ""])

a, _ = definition(c, "void mn1880023_device::device_start()")
z = find(c, "//  MN18801A")
m23_body = squeeze([l for l in c[a:z - 1] if not l.startswith("\tsave_item(")])
m23_text = "\n".join(m23_body)
m23_text = m23_text.replace("\n\n}\n", "\n}\n")  # where the save_item() block was

# LEMUS: additions inside MAME's MN1880023 functions, each matched exactly once
LEMUS = [
    # the RXD line sender's timer, after MAME's
    ("\tm_pwm_timer = timer_alloc(FUNC(mn1880023_device::pwm_tick), this);\n",
     "\tm_pwm_timer = timer_alloc(FUNC(mn1880023_device::pwm_tick), this);\n"
     "\tm_rx_line_timer = timer_alloc(FUNC(mn1880023_device::rx_line_tick), this); // LEMUS\n"),
    # keep the byte being sent for uart_tx_byte_cb
    ("\t\t\tm_uart_tx_data = data;\n",
     "\t\t\tm_uart_tx_data = data;\n"
     "\t\t\tm_uart_tx_byte = data; // LEMUS\n"),
    # and hand it over when the stop bit ends
    ("\t\tm_uart_tx_bits = 0;\n\t\tm_regs[0x18] &= ~0x08;\n\t\treturn;\n",
     "\t\tm_uart_tx_bits = 0;\n\t\tm_regs[0x18] &= ~0x08;\n"
     "\t\tm_uart_tx_byte_cb(m_uart_tx_byte); // LEMUS\n\t\treturn;\n"),
]
for old, new in LEMUS:
    m23_text = replace(m23_text, old, new)

# V15: MN1880023 behaviour MAME doesn't have, inferred from how the Bass Station Rack v1.5 firmware
# uses the chip. Each is marked "LEMUS: inferred from v1.5 at <address>" in the output, with the
# reasoning here.
#   Timers 0 and 1 in control mode 14 ((x'33') = 14, 35D8). v1.5 runs both DCOs from them:
#   - They count up: the period is 0 - (table value >> octave) (5C6B-5C76, 5D0E-5D16), and the
#     osc-sync code loads FFFE to get an overflow "now" (77D8).
#   - They reload from their register at each overflow and a write doesn't restart them: v1.5
#     rewrites 0x20 and 0x22 every 4 ms (785B, 784E) while the oscillators run, and only the
#     sync code restarts timer 1.
#   - Timer 0's overflow is IRQ3 (vector 000A -> 77D4); timer 1's is IRQ4 (vector 000C -> 77EF,
#     which only reloads 0x22). Run bits: 0x31 bit 4 (MAME's) and bit 5, which v1.5 clears and
#     sets to restart timer 1 from its reload value when osc 1 resets osc 2 (77DC-77DE).
#   - Timer 1 drives a pin by hardware: nothing in software follows its overflows when sync is
#     off (IRQ4 is only enabled by the sync code, 77E0), yet osc 2 runs. Timer 0's oscillator is
#     driven in software instead: the IRQ3 handler toggles port 3 bit 3 (77E2-77EE).
#   - Counting rate: TIMER01_CLOCKS = 16 oscillator clocks per count. v1.5's period table at ROM
#     0200 starts at 61156 = 500 kHz / 8.176 Hz (MIDI note 0) and shifts right by the octave, so
#     note 60 plays 261.6 Hz at 8 MHz with 16; 4 would put every note two octaves up.
#   The ADC channel: v1.5 never writes (x'1D'), MAME's channel register; it writes 70/71/73 to
#   (x'1E') and reads (x'1C') (34AD, 34C4, 34DB), so the channel is (x'1E') bits 0-2. A board
#   chooses the register with set_adc_channel_register(), as the Drum Station's use is MAME's.
V15 = [
    ("void mn1880023_device::update_timer0()\n{\n",
     "void mn1880023_device::update_timer0()\n{\n"
     "\t// LEMUS: inferred from v1.5 at 35D8, 785B, 77D4: control 14 counts up from the value in\n"
     "\t// 0x20/0x21 and reloads it at each overflow; a write takes effect then, without a restart\n"
     "\tif (BIT(m_regs[0x31], 4) && (m_regs[0x33] & 0x9f) == 0x14)\n"
     "\t{\n"
     "\t\tif (!m_timer0_reloading)\n"
     "\t\t{\n"
     "\t\t\tm_timer0_reloading = true;\n"
     "\t\t\tm_timer0->adjust(clocks_to_attotime(TIMER01_CLOCKS * (0x10000 - get_u16le(&m_regs[0x20]))));\n"
     "\t\t}\n"
     "\t\treturn;\n"
     "\t}\n"
     "\tm_timer0_reloading = false;\n\n"),
    ("\trequest_interrupt(IRQ_TIMER0);\n}\n",
     "\trequest_interrupt(IRQ_TIMER0);\n"
     "\tif (m_timer0_reloading) // LEMUS: inferred from v1.5 at 785B (the next period from the value now in 0x20/0x21)\n"
     "\t\tm_timer0->adjust(clocks_to_attotime(TIMER01_CLOCKS * (0x10000 - get_u16le(&m_regs[0x20]))));\n}\n"),
    ("\tif (BIT(changed, 4))\n\t\tupdate_timer0();\n\tif (BIT(changed, 7))\n",
     "\tif (BIT(changed, 4))\n\t\tupdate_timer0();\n"
     "\tif (BIT(changed, 5)) // LEMUS: inferred from v1.5 at 77DC-77DE (timer 1 run bit)\n\t\tupdate_timer1();\n"
     "\tif (BIT(changed, 7))\n"),
    ("\tif (changed & 0x9f)\n\t\tupdate_timer0();\n",
     "\tif (changed & 0x9f)\n\t{\n\t\tupdate_timer0();\n"
     "\t\tupdate_timer1(); // LEMUS: inferred from v1.5 at 35D8\n\t}\n"),
    ("\tm_timer0->adjust(attotime::never);\n\tm_timer3->adjust(attotime::never);\n",
     "\tm_timer0->adjust(attotime::never);\n"
     "\tm_timer1->adjust(attotime::never); // LEMUS: inferred from v1.5\n"
     "\tm_timer0_reloading = false;\n"
     "\tm_timer1_out = false;\n"
     "\tm_timer3->adjust(attotime::never);\n"),
    ("\tm_rx_line_timer = timer_alloc(FUNC(mn1880023_device::rx_line_tick), this); // LEMUS\n",
     "\tm_rx_line_timer = timer_alloc(FUNC(mn1880023_device::rx_line_tick), this); // LEMUS\n"
     "\tm_timer1 = timer_alloc(FUNC(mn1880023_device::timer1_tick), this); // LEMUS: inferred from v1.5\n"),
    ("\tm_regs[0x1c] = m_ad_in_cb[m_regs[0x1d] & 7]();\n",
     "\tm_regs[0x1c] = m_ad_in_cb[m_regs[m_adc_channel_register] & 7](); // LEMUS: inferred from v1.5 at 34AD (register 1E there)\n"),
]
for old, new in V15:
    m23_text = replace(m23_text, old, new)

FRAMEWORK = tabs("""
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
""")

MAPS = ["", "//**************************************************************************",
        "//  On-chip register maps, generated by tools/port_mn1880.py from MAME's internal",
        "//  maps (a later map() entry overrides an earlier one, as in MAME)",
        "//**************************************************************************", ""]
MAPS += emit_dispatch("mn1880_device", base_rd, False)
MAPS += emit_dispatch("mn1880_device", base_wr, True)
MAPS += emit_dispatch("mn1880023_device", m23_rd, False)
MAPS += emit_dispatch("mn1880023_device", m23_wr, True)

CORE = "\n".join(c[0:2]) + "\n" + NOTE + "\n".join(description) + """

#include "mn1880.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <iterator>
#include <utility>

""" + "\n".join(base_ctor) + "\n" + base_text + """

//**************************************************************************
//  MN1880023
//**************************************************************************

""" + "\n".join(m23_ctor) + "\n" + m23_text + "\n" + "\n".join(MAPS) + FRAMEWORK

# ---------------------------------------------------------------- disassembler, unchanged
dh = read("mn1880d.h")
pragma = find(dh, "#pragma once")
DASM_H = "\n".join(dh[0:2]) + "\n" + NOTE + "\n".join(dh[2:pragma + 1]) + '\n\n#include "emu.h"\n' + "\n".join(dh[pragma + 1:])
dc = read("mn1880d.cpp")
DASM_CPP = "\n".join(dc[0:2]) + "\n" + NOTE + "\n".join(dc[2:])
DASM_CPP = replace(DASM_CPP, "const mn1880_disassembler::data_buffer &opcodes, const mn1880_disassembler::data_buffer &params)\n{",
                   "const mn1880_disassembler::data_buffer &opcodes, [[maybe_unused]] const mn1880_disassembler::data_buffer &params)\n{")

write("emu.h", EMU_H)
write("mn1880.h", HEADER)
write("mn1880.cpp", CORE)
write("mn1880d.h", DASM_H)
write("mn1880d.cpp", DASM_CPP)
print("wrote", DST)
