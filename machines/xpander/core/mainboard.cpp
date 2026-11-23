#include "mainboard.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace xpander {

namespace {
constexpr int kTimerDivide = 8;  // E (2 MHz) to the 40103's 250 kHz (U10, 16 MHz / 64)
constexpr int kByteCycles = 640; // one MIDI byte (10 bits at 31.25 kbaud) in E cycles
constexpr double kAdcFullScale = 2.0 * 2.667; // VR/2 trimmed to 2.667 V (SM p35)
} // namespace

MainBoard::MainBoard(const uint8_t* rom, VoiceBoard& voice) : m_rom(rom), m_voice(voice), m_cpu(*this)
{
    m_switches.fill(0xff);
    m_cvIn.fill(std::numeric_limits<double>::quiet_NaN());
    m_cpu.setRom(0xa000, m_rom + 0x2000); // ROM1-3 (ROM0 at $8000 can be the voice board's RAM)
    m_cpu.setRam(kRamSize, m_ram.data()); // reads of RAM 0-2 straight from the array (writes keep MEMORY PROTECT)
}

void MainBoard::powerOn()
{
    m_haltSet = 0x00; // U25 cleared at power-up: HALTREQ0* and RES* low
    m_voice.setReset(true);
    m_voice.setHaltRequest(true);
    m_timerPreset = 0xff;
    m_timerCount = 0xff;
    m_firq = false;
    m_aciaControl = 0x03;
    m_rdrf = m_overrun = m_txBusy = false;
    m_tdre = true;
    m_rxQueue.clear();
    m_grid = -1;
    m_cpu.setInput(M6809::FIRQ, false);
    m_cpu.setInput(M6809::IRQ, false);
    m_cpu.reset();
}

void MainBoard::run(uint64_t end)
{
    while (m_cpu.totalCycles() < end)
    {
        m_cpu.run(int(end - m_cpu.totalCycles()));
        catchUp(m_cpu.totalCycles());
    }
}

void MainBoard::catchUp(uint64_t cycle)
{
    // The FIRQ timer: a count every 8 E cycles; at zero it reloads the preset (/TC to /PE) and the
    // next count clocks U16, which pulls FIRQ* low and blanks the display (DISPCLR*).
    // All the counts up to `cycle` at once (CPU round 23; was one loop pass a count): the first reaches
    // zero after m_timerCount counts and fires on the next; after a reload, one every preset + 1.
    if (m_timerTime < cycle)
    {
        const uint64_t elapsed = cycle - m_timerTime + uint64_t(m_timerPhase);
        const uint64_t counts = elapsed / kTimerDivide;
        m_timerPhase = int(elapsed % kTimerDivide);
        m_timerTime = cycle;
        if (counts > uint64_t(m_timerCount))
        {
            const uint64_t rest = (counts - uint64_t(m_timerCount) - 1) % (uint64_t(m_timerPreset) + 1);
            m_timerCount = m_timerPreset - int(rest);
            m_firq = true;
            m_cpu.setInput(M6809::FIRQ, true);
            m_grid = -1;
        }
        else
            m_timerCount -= int(counts);
    }

    // MIDI IN: the next byte lands when the previous one has been shifted in.
    while (!m_rxQueue.empty() && m_rxBusyUntil + kByteCycles <= cycle)
    {
        m_rxBusyUntil = std::max(m_rxBusyUntil + kByteCycles, cycle - kByteCycles);
        if (m_rdrf)
            m_overrun = true;
        else
        {
            m_rxData = m_rxQueue.front();
            m_rdrf = true;
        }
        m_rxQueue.pop_front();
    }
    if (m_rxQueue.empty() && m_rxBusyUntil + kByteCycles < cycle)
        m_rxBusyUntil = cycle - kByteCycles; // idle line: the next byte starts on arrival
    if (m_txBusy && m_txDoneAt <= cycle)
    {
        m_txOut.push_back(m_txData);
        m_txBusy = false;
        m_tdre = true;
    }
    updateAciaIrq();

    // Encoder detents: each clocks the direction flip-flop and sets the "moved" one (pot board
    // U5/U9; reading DIR* clears the moved flags). The next waits until the last was read.
    constexpr uint64_t kDetentCycles = 20000; // 10 ms
    for (int i = 0; i < kEncoders; ++i)
    {
        auto& pending = m_encPending[size_t(i)];
        if (pending == 0 || m_encChanged[size_t(i)] || cycle < m_encNext[size_t(i)])
            continue;
        m_encDir[size_t(i)] = pending > 0;
        m_encChanged[size_t(i)] = true;
        pending += pending > 0 ? -1 : 1;
        m_encNext[size_t(i)] = cycle + kDetentCycles;
    }
}

void MainBoard::updateAciaIrq()
{
    const bool rxIrq = (m_aciaControl & 0x80) && (m_rdrf || m_overrun);
    const bool txIrq = ((m_aciaControl & 0x60) == 0x20) && m_tdre;
    m_cpu.setInput(M6809::IRQ, rxIrq || txIrq);
}

void MainBoard::interruptAcknowledge(int line)
{
    // U16 clears FIRQ* on the acknowledge cycle with A3 = 0 (the FIRQ vector, $FFF6).
    if (line == M6809::FIRQ)
    {
        m_firq = false;
        m_cpu.setInput(M6809::FIRQ, false);
    }
}

void MainBoard::setSwitch(int column, int row, bool down)
{
    auto& c = m_switches[size_t(column & 7)];
    if (down)
        c = uint8_t(c & ~(1 << row));
    else
        c = uint8_t(c | (1 << row));
}

void MainBoard::turnEncoder(int encoder, int steps)
{
    m_encPending[size_t(encoder)] += steps;
}

uint8_t MainBoard::adcValue(int channel) const
{
    if (m_cvInhibit)
        return 0;
    double v = m_cvIn[size_t(channel)];
    if (std::isnan(v))
        v = channel >= 6 ? 12.0 : 0.0; // pedals: pulled to +12 V through 62K (R75/R76 gone, ECO 601)
    v = std::clamp(v, 0.0, 5.6);       // the input clamps
    const double code = std::round(v / kAdcFullScale * 255.0);
    return uint8_t(std::clamp(code, 0.0, 255.0));
}

uint8_t MainBoard::read(uint16_t address)
{
    if (address < 0x6000)
        return m_ram[address];
    if (address >= 0xa000)
        return m_rom[address - 0x8000];
    if (address >= 0x8000)
        return m_voice.haltAcknowledged() ? m_voice.ram()[address & 0x1fff] : m_rom[address - 0x8000];
    switch (address & 0xfc00)
    {
        case 0x6c00:
            if ((address & 6) != 6)
                return 0xff;
            catchUp(m_cpu.now());
            if (address & 1)
            {
                const uint8_t d = m_rxData;
                m_rdrf = false;
                m_overrun = false;
                updateAciaIrq();
                return d;
            }
            else
            {
                const bool irq = ((m_aciaControl & 0x80) && (m_rdrf || m_overrun)) || (((m_aciaControl & 0x60) == 0x20) && m_tdre);
                return uint8_t((m_rdrf ? 1 : 0) | (m_tdre ? 2 : 0) | (m_overrun ? 0x20 : 0) | (irq ? 0x80 : 0));
            }
        case 0x7000:
            // DATAIN (U15): D0 HALTAKN*, D1 memory protect (low when on), D2 cassette data.
            return uint8_t(0xf8 | 4 | (m_protect ? 0 : 2) | (m_voice.haltAcknowledged() ? 0 : 1));
        case 0x7c00:
            switch ((address >> 6) & 7)
            {
                case 0:
                {
                    // DIR* (U5): the direction flip-flops; the read clears the moved flags.
                    uint8_t d = 0;
                    for (int i = 0; i < kEncoders; ++i)
                        d = uint8_t(d | (m_encDir[size_t(i)] ? 1 << i : 0));
                    m_encChanged.fill(false);
                    return uint8_t(d | 0xc0);
                }
                case 1:
                {
                    uint8_t d = 0;
                    for (int i = 0; i < kEncoders; ++i)
                        d = uint8_t(d | (m_encChanged[size_t(i)] ? 1 << i : 0));
                    return uint8_t(d | 0xc0);
                }
                case 3:
                    // GATE* (U23): nothing plugged, so each input takes its PULL level; inverted
                    // by U22/Q1, TRIGGER (D7) inverted again by U2.
                    return uint8_t(~m_pull ^ 0x80);
                case 5: return m_switches[address & 7];
                case 6:
                    m_cvSelect = address & 7;
                    m_cvInhibit = (address & 8) != 0;
                    return m_adcResult;
                default: return 0xff;
            }
        default: return 0xff;
    }
}

void MainBoard::write(uint16_t address, uint8_t data)
{
    if (address < 0x4000)
    {
        if (!m_protect)
            m_ram[address] = data; // RAM0/1: WRITEA* blocked by MEMORY PROTECT
        return;
    }
    if (address < 0x6000)
    {
        m_ram[address] = data;
        return;
    }
    if (address >= 0x8000)
    {
        if (address < 0xa000 && m_voice.haltAcknowledged())
            m_voice.ram()[address & 0x1fff] = data;
        return;
    }
    switch (address & 0xfc00)
    {
        case 0x6000: displayWrite(address & 0x1ff, data); return;
        case 0x6400: m_timerPreset = data; return;
        case 0x6800:
            m_haltSet = data;
            m_voice.setHaltRequest((data & 1) == 0); // HALTREQ0*
            m_voice.setReset((data & 0x10) == 0);    // RES*
            return;
        case 0x6c00:
            if ((address & 6) != 6)
                return;
            catchUp(m_cpu.now());
            if (address & 1)
            {
                m_txData = data;
                m_tdre = false;
                m_txBusy = true;
                m_txDoneAt = m_cpu.now() + kByteCycles;
            }
            else
            {
                if ((data & 3) == 3)
                {
                    m_rdrf = m_overrun = m_txBusy = false;
                    m_tdre = true;
                }
                m_aciaControl = data;
            }
            updateAciaIrq();
            return;
        case 0x7400: m_led2 = data; return;
        case 0x7c00:
            switch ((address >> 6) & 7)
            {
                case 2: m_led1 = data; return;
                case 4: m_pull = uint8_t(((data >> 6) & 1) << 7 | 1 << 6 | (data & 0x3f)); return;
                case 6:
                    // CV*: selects the mux (U18 from A0-A3) and starts a conversion (U20).
                    m_cvSelect = address & 7;
                    m_cvInhibit = (address & 8) != 0;
                    m_adcResult = adcValue(m_cvSelect);
                    return;
                default: return;
            }
        default: return;
    }
}

void MainBoard::displayWrite(uint16_t offset, uint8_t data)
{
    // DISPCLR* then DISP*: the grid goes off, the addressed anode latch (U12 from A0-A2) takes the
    // byte, and with U12 output 5 the grid decoder lights grid A3-A8.
    const int display = (offset >> 1) & 3;
    if (display < kDisplays)
    {
        auto& a = m_anode[size_t(display)];
        a = (offset & 1) ? uint16_t((data << 8) | (a & 0x00ff)) : uint16_t((a & 0xff00) | data);
    }
    const int grid = (offset >> 3) & 0x3f;
    m_grid = ((offset & 7) == 5 && grid < kDigits) ? grid : -1;
    if (m_grid >= 0)
        for (int d = 0; d < kDisplays; ++d)
            m_shown[size_t(d)][size_t(m_grid)] = m_anode[size_t(d)];
}

} // namespace xpander
