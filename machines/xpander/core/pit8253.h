// Intel 8253 programmable interval timer, from Intel's data sheet (1979 Peripheral Design Handbook,
// pp. 46-56): three 16-bit down counters, modes 0-5, binary or BCD, LSB/MSB/word access and the
// counter latch command. Each counter is stepped by its CLK's falling edges, given one at a time;
// GATE is a level, its rising edges trigger modes 1, 2, 3 and 5.
#pragma once

#include <array>
#include <cstdint>

namespace xpander {

class Pit8253
{
public:
    void reset();
    void write(int address, uint8_t data); // A1 A0: 0-2 counters, 3 control word
    uint8_t read(int address);
    // One falling edge of counter n's CLK.
    void clock(int n);
    // How many CLKs from now on counter n only count down (no load, no trigger, no change of OUT),
    // at most `limit`; countDown applies k of them at once, exactly as k calls of clock would.
    int quiet(int n, int limit) const;
    void countDown(int n, int k);
    void setGate(int n, bool level);
    bool out(int n) const { return m_c[size_t(n)].out; }

private:
    struct Counter
    {
        int mode = 0;
        bool bcd = false;
        int rl = 3;              // 1 LSB, 2 MSB, 3 LSB then MSB
        bool writeMsbNext = false;
        bool readMsbNext = false;
        bool latched = false;
        uint16_t latch = 0;
        uint16_t cr = 0;         // count register (as written)
        uint16_t ce = 0;         // counting element
        bool loadPending = false; // a count written, loaded on the next CLK
        bool armed = false;      // a count has been loaded since the mode was set
        bool trigger = false;    // a GATE rising edge waits for the next CLK
        bool gate = true;
        bool out = true;
        int phase = 0;           // mode 3: CLKs left in the current half
    };

    static uint16_t decrement(const Counter& c, uint16_t v, int by = 1);
    static uint32_t length(const Counter& c, uint16_t v); // a count of 0 is 2^16 (10^4 in BCD)
    void loaded(Counter& c);

    std::array<Counter, 3> m_c;
};

} // namespace xpander
