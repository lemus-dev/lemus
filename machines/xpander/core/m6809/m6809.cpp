// license:BSD-3-Clause
// copyright-holders:Nathan Woods
/*** m6809: Portable 6809 emulator ******************************************

    Copyright John Butler

    References:

        6809 Simulator V09, By L.C. Benschop, Eindhoven The Netherlands.

        m6809: Portable 6809 emulator, DS (6809 code in MAME, derived from
            the 6809 Simulator V09)

        6809 Microcomputer Programming & Interfacing with Experiments"
            by Andrew C. Staugaard, Jr.; Howard W. Sams & Co., Inc.

    (History and timing notes: see MAME's src/devices/cpu/m6809/m6809.cpp.)

*****************************************************************************/
// Ported from MAME (src/devices/cpu/m6809/m6809.cpp and m6809inl.h); see m6809.h for the changes.

#include "m6809.h"

#include <cstdio>
#include <cstdlib>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-label"
#endif
#ifdef _MSC_VER
#pragma warning(disable : 4102)
#endif

#if defined(__GNUC__)
#define UNEXPECTED(exp) __builtin_expect(!!(exp), 0)
#else
#define UNEXPECTED(exp) (exp)
#endif

namespace xpander {

// MAME's BIT(x, n) from emucore.h.
template <typename T> static constexpr T BIT(T x, int n) { return (x >> n) & T(1); }

void M6809::fatalerror(const char* what)
{
    std::fprintf(stderr, "M6809: %s\n", what);
    std::abort();
}

void M6809::reset()
{
    m_nmi_asserted = false;
    m_lds_encountered = false;
    m_free_run = false;
    m_halted = false;
    m_dp = 0x00;
    m_cc |= CC_I;
    m_cc |= CC_F;
    set_ea(VECTOR_RESET_FFFE);
    reset_state();
}

void M6809::setInput(Line line, bool asserted)
{
    switch (line)
    {
        case NMI:
            // NMI is edge triggered, and armed only after the first LDS.
            m_nmi_asserted = m_nmi_asserted || (asserted && !m_nmi_line && m_lds_encountered);
            m_nmi_line = asserted;
            break;
        case FIRQ: m_firq_line = asserted; break;
        case IRQ: m_irq_line = asserted; break;
    }
}

int M6809::run(int cycles)
{
    m_icount = cycles;
    m_runCycles = cycles;
    do
    {
        // HALT is sampled at the end of each instruction; the bus is released until it goes high.
        if (m_state == 0 && m_haltLine)
        {
            m_halted = true;
            m_icount = 0;
            break;
        }
        m_halted = false;
        execute_one();
    } while (m_icount > 0);
    const int used = cycles - m_icount;
    m_total += uint64_t(used);
    m_runCycles = 0;
    m_icount = 0;
    return used;
}

void M6809::eat_remaining()
{
    debugger_wait_hook();
    eat(m_icount);
}

uint16_t M6809::read_tfr_exg_816_register(uint8_t reg)
{
    switch (reg & 0x0F)
    {
        case 0: return m_q.r.d;
        case 1: return m_x.w;
        case 2: return m_y.w;
        case 3: return m_u.w;
        case 4: return m_s.w;
        case 5: return m_pc.w;
        case 8: return uint16_t(0xff00 | m_q.r.a);
        case 9: return uint16_t(0xff00 | m_q.r.b);
        case 10: return uint16_t(uint16_t(m_cc) << 8 | m_cc);
        case 11: return uint16_t(uint16_t(m_dp) << 8 | m_dp);
        default: return 0xffff;
    }
}

uint16_t M6809::read_exg_168_register(uint8_t reg)
{
    switch (reg & 0x0F)
    {
        case 0: return m_q.r.d;
        case 1: return m_x.w;
        case 2: return m_y.w;
        case 3: return m_u.w;
        case 4: return m_s.w;
        case 5: return m_pc.w;
        case 8: return uint16_t(0xff00 | m_q.r.a);
        case 9: return uint16_t(0xff00 | m_q.r.b);
        case 10: return uint16_t(0xff00 | m_cc);
        case 11: return uint16_t(0xff00 | m_dp);
        default: return 0xffff;
    }
}

void M6809::write_exgtfr_register(uint8_t reg, uint16_t value)
{
    switch (reg & 0x0F)
    {
        case 0: m_q.r.d = value; break;
        case 1: m_x.w = value; break;
        case 2: m_y.w = value; break;
        case 3: m_u.w = value; break;
        case 4: m_s.w = value; break;
        case 5: m_pc.w = value; break;
        case 8: m_q.r.a = uint8_t(value); break;
        case 9: m_q.r.b = uint8_t(value); break;
        case 10: m_cc = uint8_t(value); break;
        case 11: m_dp = uint8_t(value); break;
    }
}

// ---- m6809inl.h ----

template <class T>
inline T M6809::rotate_right(T value)
{
    bool new_carry = (value & 1) ? true : false;
    value = T(value >> 1);
    T high_bit = T(T(1) << (sizeof(T) * 8 - 1));
    if (m_cc & CC_C)
        value |= high_bit;
    else
        value &= T(~high_bit);
    if (new_carry)
        m_cc |= CC_C;
    else
        m_cc &= uint8_t(~CC_C);
    return value;
}

template <class T>
inline uint32_t M6809::rotate_left(T value)
{
    T high_bit = T(T(1) << (sizeof(T) * 8 - 1));
    bool new_carry = (value & high_bit) ? true : false;
    uint32_t new_value = value;
    new_value <<= 1;
    if (m_cc & CC_C)
        new_value |= 1;
    else
        new_value &= ~1u;
    if (new_carry)
        m_cc |= CC_C;
    else
        m_cc &= uint8_t(~CC_C);
    return new_value;
}

inline uint8_t M6809::read_operand()
{
    switch (m_addressing_mode)
    {
        case ADDRESSING_MODE_EA: return read_memory(m_ea.w);
        case ADDRESSING_MODE_IMMEDIATE: return read_opcode_arg();
        case ADDRESSING_MODE_REGISTER_A: return m_q.r.a;
        case ADDRESSING_MODE_REGISTER_B: return m_q.r.b;
        default: fatalerror("Unexpected");
    }
}

inline uint8_t M6809::read_operand(int ordinal)
{
    switch (m_addressing_mode)
    {
        case ADDRESSING_MODE_EA: return read_memory(uint16_t(m_ea.w + ordinal));
        case ADDRESSING_MODE_IMMEDIATE: return read_opcode_arg();
        default: fatalerror("Unexpected");
    }
}

inline void M6809::write_operand(uint8_t data)
{
    switch (m_addressing_mode)
    {
        case ADDRESSING_MODE_IMMEDIATE: break;
        case ADDRESSING_MODE_EA: write_memory(m_ea.w, data); break;
        case ADDRESSING_MODE_REGISTER_A: m_q.r.a = data; break;
        case ADDRESSING_MODE_REGISTER_B: m_q.r.b = data; break;
        default: fatalerror("Unexpected");
    }
}

inline void M6809::write_operand(int ordinal, uint8_t data)
{
    switch (m_addressing_mode)
    {
        case ADDRESSING_MODE_IMMEDIATE: break;
        case ADDRESSING_MODE_EA: write_memory(uint16_t(m_ea.w + ordinal), data); break;
        default: fatalerror("Unexpected");
    }
}

inline void M6809::daa()
{
    uint16_t t, cf = 0;
    uint8_t msn = m_q.r.a & 0xF0;
    uint8_t lsn = m_q.r.a & 0x0F;
    if (lsn > 0x09 || m_cc & CC_H) cf |= 0x06;
    if (msn > 0x80 && lsn > 0x09) cf |= 0x60;
    if (msn > 0x90 || m_cc & CC_C) cf |= 0x60;
    t = uint16_t(m_q.r.a + cf);
    m_cc &= uint8_t(~CC_V);
    if (t & 0x0100)
        m_cc |= CC_C;
    m_q.r.a = set_flags(CC_NZ, uint8_t(t));
}

inline void M6809::mul()
{
    uint16_t result = uint16_t(uint16_t(m_q.r.a) * uint16_t(m_q.r.b));
    m_q.r.d = set_flags(CC_Z, result);
    if (m_q.r.d & 0x0080)
        m_cc |= CC_C;
    else
        m_cc &= uint8_t(~CC_C);
}

inline uint16_t& M6809::ireg()
{
    switch (m_opcode & 0x60)
    {
        case 0x00: return m_x.w;
        case 0x20: return m_y.w;
        case 0x40: return m_u.w;
        case 0x60: return m_s.w;
        default: fatalerror("Unexpected");
    }
}

template <class T>
inline T M6809::set_flags(uint8_t mask, T a, T b, uint32_t r)
{
    T hi_bit = T(1 << (sizeof(T) * 8 - 1));
    m_cc &= uint8_t(~mask);
    if (mask & CC_H)
        m_cc |= ((a ^ b ^ r) & 0x10) ? CC_H : 0;
    if (mask & CC_N)
        m_cc |= (r & hi_bit) ? CC_N : 0;
    if (mask & CC_Z)
        m_cc |= (T(r) == 0) ? CC_Z : 0;
    if (mask & CC_V)
        m_cc |= ((a ^ b ^ r ^ (r >> 1)) & hi_bit) ? CC_V : 0;
    if (mask & CC_C)
        m_cc |= (r & (uint32_t(hi_bit) << 1)) ? CC_C : 0;
    return T(r);
}

template <class T>
inline T M6809::set_flags(uint8_t mask, T r)
{
    return set_flags(mask, T(0), r, r);
}

inline bool M6809::is_register_addressing_mode()
{
    return (m_addressing_mode != ADDRESSING_MODE_IMMEDIATE) && (m_addressing_mode != ADDRESSING_MODE_EA);
}

inline uint16_t M6809::get_pending_interrupt()
{
    if (m_nmi_asserted)
        return VECTOR_NMI;
    else if (!(m_cc & CC_F) && m_firq_line)
        return VECTOR_FIRQ;
    else if (!(m_cc & CC_I) && m_irq_line)
        return VECTOR_IRQ;
    else
        return 0;
}

void M6809::execute_one()
{
    switch (pop_state())
    {
#include "m6809.hxx"
    }
}

} // namespace xpander
