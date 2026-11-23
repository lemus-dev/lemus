#include "voiceboard.h"

#include <algorithm>
#include <cmath>

namespace xpander {

VoiceBoard::VoiceBoard(const uint8_t* rom) : m_rom(rom), m_cpu(*this)
{
    m_events.reserve(4096);
    m_pit.reset();
    m_cpu.setRom(0xc000, m_rom); // U912, U914: read() returns them as they are
    m_cpu.setRam(kRamSize, m_ram.data()); // U917's first image read straight from the array (its mirrors via read())
}

void VoiceBoard::setReset(bool asserted)
{
    if (asserted == m_reset)
        return;
    m_reset = asserted;
    if (asserted)
    {
        // RESET* clears DATAOUT (U920) and the IRQ flip-flop (U919): HALTDS off, outputs muted.
        m_dataOut = 0;
        m_mute = true;
        m_irq = false;
        m_cpu.setInput(M6809::IRQ, false);
        m_events.push_back({m_cpu.totalCycles(), VoiceEvent::DataOut, 0, 0, 0, false, 0});
        updateHalt();
        m_pit.setGate(0, false);
        setGate2();
    }
    else
        m_cpu.reset();
}

void VoiceBoard::setGate2()
{
    // GATE2 = PW* OR OSC (U915 used as a gate).
    m_pit.setGate(2, (m_dataOut & 4) != 0 || m_osc);
}

void VoiceBoard::catchUp(uint64_t cycle)
{
    while (m_pitTime < cycle)
    {
        // Stretches where counters 1 and 2 only count down (OUT2 can't rise) in one go.
        const int limit = int(std::min<uint64_t>(cycle - m_pitTime, 1u << 20));
        const int k = std::min(m_pit.quiet(1, limit), m_pit.quiet(2, limit));
        if (k > 1)
        {
            m_pit.countDown(1, k);
            m_pit.countDown(2, k);
            m_pitTime += uint64_t(k);
            continue;
        }
        // CLK1 and CLK2 are Q (2 MHz): one falling edge per bus cycle.
        const bool out0 = m_pit.out(0);
        m_pit.clock(1);
        m_pit.clock(2);
        const bool out2 = m_pit.out(2);
        if (out2 && !m_out2)
        {
            // OUT2's rising edge sets U919: IRQ* until the acknowledge cycle.
            m_irq = true;
            m_cpu.setInput(M6809::IRQ, true);
        }
        m_out2 = out2;
        (void)out0;
        ++m_pitTime;
    }
}

void VoiceBoard::run(uint64_t end, const OscEdge* osc, int edges)
{
    auto runTo = [this](uint64_t target) {
        while (m_cpu.totalCycles() < target)
        {
            const int n = int(target - m_cpu.totalCycles());
            if (m_reset)
            {
                // Held in reset: time passes, nothing runs.
                m_cpu.setHalt(true);
                m_cpu.run(n);
                updateHalt();
            }
            else
                m_cpu.run(n);
            // The 8253 only to the target: an edge due there must find it there, not at the end of
            // the instruction the CPU was finishing.
            catchUp(std::min(m_cpu.totalCycles(), target));
        }
    };
    for (int i = 0; i < edges; ++i)
    {
        const uint64_t at = std::min(uint64_t(std::ceil(std::max(0.0, osc[i].at))), end);
        runTo(at);
        catchUp(at);
        if (osc[i].level == m_osc)
            continue;
        m_osc = osc[i].level;
        if (!m_osc)
        {
            // CLK0 = OSC: the 8253 counts on the falling edge. OUT0 is AUTODNE*, and GATE1 its inverse.
            m_pit.clock(0);
            m_pit.setGate(1, !m_pit.out(0));
        }
        setGate2();
    }
    runTo(end);
    catchUp(end);
}

void VoiceBoard::interruptAcknowledge(int line)
{
    if (line == M6809::IRQ)
    {
        m_irq = false;
        m_cpu.setInput(M6809::IRQ, false);
    }
}

uint8_t VoiceBoard::read(uint16_t address)
{
    if (address < 0x8000)
        return m_ram[address & (kRamSize - 1)]; // U917, CS = A15, mirrored
    if (address >= 0xc000)
        return m_rom[address - 0xc000];
    if (address >= 0xa000)
        return 0xff; // U909: the spare socket
    switch (address & 0xfc00)
    {
        case 0x9400:
        {
            // DATAIN (U916): D0 AUTODNE* (= OUT0), D1 OSC; the rest pulled up.
            catchUp(m_cpu.now());
            return uint8_t(0xfc | (m_osc ? 2 : 0) | (m_pit.out(0) ? 1 : 0));
        }
        case 0x9800:
        {
            catchUp(m_cpu.now());
            const uint8_t v = m_pit.read(address & 3);
            return v;
        }
        default: return 0xff; // write-only latches; the data bus is pulled high
    }
}

void VoiceBoard::write(uint16_t address, uint8_t data)
{
    if (address < 0x8000)
    {
        m_ram[address & (kRamSize - 1)] = data;
        return;
    }
    const uint64_t cycle = m_cpu.now();
    switch (address & 0xfc00)
    {
        case 0x8000:
        case 0x8400:
        case 0x8800:
        {
            // LATCH0*/1*/2* through U804/U802/U801 (74LS42): outputs 0-5 are voices 6..1.
            const int sel = address & 7;
            if (sel <= 5)
                m_events.push_back({cycle, VoiceEvent::Latch, uint8_t(5 - sel), uint8_t((address >> 10) & 3), data, false, 0});
            return;
        }
        case 0x8c00:
            dacWrite(address, data, cycle);
            return;
        case 0x9000:
        {
            catchUp(cycle);
            m_dataOut = data & 0x3f;
            updateHalt();
            m_pit.setGate(0, (data & 2) != 0); // AUTOST
            setGate2();
            // MUTE = NOT(AUTO* · RESET*) into the pan muxes' INH.
            m_mute = (data & 8) == 0;
            m_events.push_back({cycle, VoiceEvent::DataOut, 0, 0, m_dataOut, false, 0});
            return;
        }
        case 0x9800:
            catchUp(cycle);
            m_pit.write(address & 3, data);
            m_pit.setGate(1, !m_pit.out(0));
            return;
        default: return;
    }
}

void VoiceBoard::dacWrite(uint16_t address, uint8_t data, uint64_t cycle)
{
    if ((address & 1) == 0)
    {
        // CLEAR*: U806 cleared (every S&H let go); D6..D0 are the DAC's 7 MSBs, D7 the FAST flag.
        m_events.push_back({cycle, VoiceEvent::Deselect, 0, 0, 0, false, 0});
        m_dacCode = uint16_t(((data & 0x7f) << 7) | (m_dacCode & 0x7f));
        m_fastLatched = (data & 0x80) != 0;
        return;
    }
    // VOICEN*: D7..D1 the 7 LSBs; A1..A8 latched into U806.
    m_dacCode = uint16_t((m_dacCode & 0x3f80) | (data >> 1));
    const int cv = (address >> 1) & 7;
    const int sel = (address >> 4) & 7;
    const bool ftsh = (address >> 7) & 1;
    const bool hres = ((address >> 8) & 1) == 0;

    // The reference into C806: U814c gives the fixed one unless HRES*; with HRES*, U805 picks a
    // voice's temperature sensor (1-6) or the fixed one (7); address 0 leaves C806 holding.
    if (!hres || sel == 7)
        m_ref = dac.refFixed;
    else if (sel >= 1 && sel <= 6)
        m_ref = dac.refGain * m_tempSensor[size_t(sel - 1)];

    const double u812 = dacVolts(m_dacCode, m_ref) + dac.offset; // inverted at U812, positive sense here
    if (ftsh)
    {
        // FTSH: U814b samples U812's output onto C805; U803 is disabled, so no S&H is selected.
        m_fine = u812;
        return;
    }
    double v = u812 - dac.eco609 * m_ref;
    if (hres)
        v += dac.hresFine * m_fine - dac.hresRef * m_ref;
    v = std::min(v, dac.clampHigh);
    if (sel == 0)
        return; // U803 output 0 is not connected
    if (sel == 7)
    {
        // The RES S&Hs (U816 picks the voice by A1-A3); no fast path.
        if (cv <= 5)
            m_events.push_back({cycle, VoiceEvent::Select, uint8_t(cv), uint8_t(CvRes), 0, false, float(v)});
        return;
    }
    m_events.push_back({cycle, VoiceEvent::Select, uint8_t(sel - 1), uint8_t(cv), 0, m_fastLatched, float(v)});
}

} // namespace xpander
