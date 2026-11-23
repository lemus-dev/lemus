#include "pit8253.h"

#include <algorithm>

namespace xpander {

void Pit8253::reset()
{
    for (auto& c : m_c)
        c = Counter{};
}

uint32_t Pit8253::length(const Counter& c, uint16_t v)
{
    if (!c.bcd)
        return v == 0 ? 65536u : v;
    const uint32_t d = uint32_t((v >> 12) & 15) * 1000 + uint32_t((v >> 8) & 15) * 100 + uint32_t((v >> 4) & 15) * 10 + (v & 15);
    return d == 0 ? 10000u : d;
}

uint16_t Pit8253::decrement(const Counter& c, uint16_t v, int by)
{
    if (!c.bcd)
        return uint16_t(v - by);
    int d = int((v >> 12) & 15) * 1000 + int((v >> 8) & 15) * 100 + int((v >> 4) & 15) * 10 + int(v & 15);
    d = (d - by + 10000) % 10000;
    return uint16_t(((d / 1000) << 12) | (((d / 100) % 10) << 8) | (((d / 10) % 10) << 4) | (d % 10));
}

void Pit8253::write(int address, uint8_t data)
{
    address &= 3;
    if (address == 3)
    {
        const int sc = data >> 6;
        if (sc == 3)
            return; // illegal on the 8253 (read-back exists only on the 8254)
        auto& c = m_c[size_t(sc)];
        const int rl = (data >> 4) & 3;
        if (rl == 0)
        {
            // Counter latch command: holds the count until it is read.
            if (!c.latched)
            {
                c.latched = true;
                c.latch = c.ce;
                c.readMsbNext = false;
            }
            return;
        }
        c.rl = rl;
        c.mode = (data >> 1) & 7;
        if (c.mode > 5)
            c.mode -= 4; // modes 6 and 7 are 2 and 3
        c.bcd = data & 1;
        c.writeMsbNext = false;
        c.readMsbNext = false;
        c.latched = false;
        c.loadPending = false;
        c.armed = false;
        c.trigger = false;
        // Setting the mode: OUT low in mode 0, high in the others.
        c.out = c.mode != 0;
        return;
    }
    auto& c = m_c[size_t(address)];
    switch (c.rl)
    {
        case 1: c.cr = data; break;
        case 2: c.cr = uint16_t(data << 8); break;
        default:
            if (!c.writeMsbNext)
            {
                c.cr = uint16_t((c.cr & 0xff00) | data);
                c.writeMsbNext = true;
                if (c.mode == 0)
                    c.out = false; // writing the first byte stops a mode-0 count
                return;
            }
            c.cr = uint16_t((c.cr & 0x00ff) | (data << 8));
            c.writeMsbNext = false;
            break;
    }
    loaded(c);
}

void Pit8253::loaded(Counter& c)
{
    switch (c.mode)
    {
        case 0:
            c.out = false;
            c.loadPending = true;
            break;
        case 1:
        case 5:
            // Loaded by the next trigger; a count in progress is not disturbed.
            break;
        case 2:
        case 3:
        case 4:
            // Modes 2 and 3 take a new count at the end of the current period once running.
            if (!c.armed || c.mode == 4)
                c.loadPending = true;
            break;
    }
}

uint8_t Pit8253::read(int address)
{
    address &= 3;
    if (address == 3)
        return 0xff;
    auto& c = m_c[size_t(address)];
    const uint16_t v = c.latched ? c.latch : c.ce;
    uint8_t r;
    switch (c.rl)
    {
        case 1: r = uint8_t(v); c.latched = false; break;
        case 2: r = uint8_t(v >> 8); c.latched = false; break;
        default:
            if (!c.readMsbNext)
            {
                r = uint8_t(v);
                c.readMsbNext = true;
            }
            else
            {
                r = uint8_t(v >> 8);
                c.readMsbNext = false;
                c.latched = false;
            }
            break;
    }
    return r;
}

void Pit8253::setGate(int n, bool level)
{
    auto& c = m_c[size_t(n)];
    if (level && !c.gate)
        c.trigger = true;
    if (!level && (c.mode == 2 || c.mode == 3))
        c.out = true; // gate low forces OUT high in modes 2 and 3
    c.gate = level;
}

void Pit8253::clock(int n)
{
    auto& c = m_c[size_t(n)];
    switch (c.mode)
    {
        case 0: // interrupt on terminal count
            if (c.loadPending)
            {
                c.ce = c.cr;
                c.loadPending = false;
                c.armed = true;
                return;
            }
            if (!c.armed || !c.gate)
                return;
            c.ce = decrement(c, c.ce);
            if (c.ce == 0)
                c.out = true;
            return;

        case 1: // hardware retriggerable one-shot
            if (c.trigger)
            {
                c.trigger = false;
                c.ce = c.cr;
                c.armed = true;
                c.out = false;
                return;
            }
            if (!c.armed)
                return;
            c.ce = decrement(c, c.ce);
            if (c.ce == 0 && !c.out)
                c.out = true;
            return;

        case 2: // rate generator
            if (c.loadPending || (c.trigger && c.armed))
            {
                c.ce = c.cr;
                c.loadPending = false;
                c.trigger = false;
                c.armed = true;
                c.out = true;
                return;
            }
            c.trigger = false;
            if (!c.armed || !c.gate)
                return;
            c.ce = decrement(c, c.ce);
            if (c.ce == 1)
                c.out = false;
            else if (c.ce == 0)
            {
                c.ce = c.cr;
                c.out = true;
            }
            return;

        case 3: // square wave: high for (N+1)/2 CLKs, low for N/2
        {
            const uint32_t len = length(c, c.cr);
            if (c.loadPending || (c.trigger && c.armed))
            {
                c.loadPending = false;
                c.trigger = false;
                c.armed = true;
                c.out = true;
                c.phase = int((len + 1) / 2);
                c.ce = c.cr;
                return;
            }
            c.trigger = false;
            if (!c.armed || !c.gate)
                return;
            c.ce = decrement(c, c.ce, 2);
            if (--c.phase <= 0)
            {
                c.out = !c.out;
                c.phase = int(c.out ? (len + 1) / 2 : len / 2);
                if (c.phase <= 0)
                    c.phase = 1;
                c.ce = c.cr;
            }
            return;
        }

        case 4: // software triggered strobe
        case 5: // hardware triggered strobe
            if ((c.mode == 4 && c.loadPending) || (c.mode == 5 && c.trigger))
            {
                c.ce = c.cr;
                c.loadPending = false;
                c.trigger = false;
                c.armed = true;
                c.out = true;
                return;
            }
            if (!c.out)
            {
                c.out = true; // the strobe lasts one CLK
                return;
            }
            if (!c.armed || (c.mode == 4 && !c.gate))
                return;
            c.ce = decrement(c, c.ce);
            if (c.ce == 0)
            {
                c.out = false;
                c.armed = false;
            }
            return;
    }
}


int Pit8253::quiet(int n, int limit) const
{
    const auto& c = m_c[size_t(n)];
    // The counting element's value as the count runs (0 = 2^16, or 10^4 in BCD).
    auto value = [&](uint16_t v) { return int(length(c, v)); };
    const int bcdCap = c.bcd ? 4999 : limit; // decrement() takes a step of at most 10^4 in BCD
    limit = std::min(limit, bcdCap);
    switch (c.mode)
    {
        case 0:
        case 1:
            if ((c.mode == 0 && c.loadPending) || (c.mode == 1 && c.trigger))
                return 0;
            if (!c.armed || (c.mode == 0 && !c.gate))
                return limit;
            if (c.out)
                return limit; // reaching 0 sets OUT, already high
            return std::min(limit, value(c.ce) - 1);
        case 2:
            if (c.loadPending || c.trigger)
                return 0;
            if (!c.armed || !c.gate)
                return limit;
            return std::max(0, std::min(limit, value(c.ce) - 2));
        case 3:
            if (c.loadPending || c.trigger)
                return 0;
            if (!c.armed || !c.gate)
                return limit;
            return std::max(0, std::min(limit, c.phase - 1));
        case 4:
        case 5:
            if ((c.mode == 4 && c.loadPending) || (c.mode == 5 && c.trigger) || !c.out)
                return 0;
            if (!c.armed || (c.mode == 4 && !c.gate))
                return limit;
            return std::max(0, std::min(limit, value(c.ce) - 1));
    }
    return 0;
}

void Pit8253::countDown(int n, int k)
{
    auto& c = m_c[size_t(n)];
    if (k <= 0)
        return;
    switch (c.mode)
    {
        case 0:
        case 1:
            if (c.armed && (c.mode == 1 || c.gate))
                c.ce = decrement(c, c.ce, k);
            return;
        case 2:
            if (c.armed && c.gate)
                c.ce = decrement(c, c.ce, k);
            return;
        case 3:
            if (c.armed && c.gate)
            {
                for (int left = k; left > 0;)
                {
                    const int step = std::min(left, 4999);
                    c.ce = decrement(c, c.ce, 2 * step);
                    left -= step;
                }
                c.phase -= k;
            }
            return;
        case 4:
        case 5:
            if (c.armed && (c.mode == 5 || c.gate))
                c.ce = decrement(c, c.ce, k);
            return;
    }
}

} // namespace xpander
