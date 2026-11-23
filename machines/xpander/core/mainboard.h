// The Xpander's processor, pot and display boards, digital side (SM p39-49; research.md 2, 6.1,
// 6.3): the main 68B09 (U9), ROM0-3, the 24 KB battery RAM, the 40103 FIRQ timer (U12), the 6850
// MIDI ACIA (U26), the HALTSET latch that halts and resets the voice CPU, the ADC (U20) behind the
// CV mux, the gate inputs, the 8 x 8 switch matrix, the six encoders' flip-flops, the LED latches
// and the three VFDs' anode and grid latches. The voice board is reached through HALTREQ/HALTAKN
// and its RAM, which replaces ROM0 at $8000 while the voice CPU is halted.
#pragma once

#include "m6809/m6809.h"
#include "voiceboard.h"

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace xpander {

class MainBoard final : public M6809::Bus
{
public:
    static constexpr int kRamSize = 0x6000;
    static constexpr int kDisplays = 3, kDigits = 40;
    static constexpr int kEncoders = 6;

    // rom: 32 KB, U8, U7, U6, U5 ($8000-$FFFF).
    MainBoard(const uint8_t* rom, VoiceBoard& voice);

    void powerOn();
    // Runs to bus cycle `end`, counted from power-on (as VoiceBoard::run).
    void run(uint64_t end);
    uint64_t now() const { return m_cpu.totalCycles(); }

    // Battery RAM (U2, U3, U4): the session's state.
    std::array<uint8_t, kRamSize>& ram() { return m_ram; }

    // Panel. Switches by matrix position: column 0-7, row 0-7 (MAME's xpander_base order).
    void setSwitch(int column, int row, bool down);
    // Detents of encoder 0-5: + clockwise, - counter-clockwise. A hand turns them one at a time: each
    // reaches the flip-flops once the last was read and at least 10 ms after it.
    void turnEncoder(int encoder, int steps);
    void setMemoryProtect(bool on) { m_protect = on; }
    bool memoryProtect() const { return m_protect; }
    // MIDI IN: bytes in arrival order, delivered at 31.25 kbaud.
    void midiIn(uint8_t byte) { m_rxQueue.push_back(byte); }
    size_t midiInPending() const { return m_rxQueue.size(); }
    std::vector<uint8_t>& midiOut() { return m_txOut; }
    // CV1-6 and the two pedal inputs, volts at the jack; NaN = nothing plugged.
    void setCvInput(int n, double volts) { m_cvIn[size_t(n)] = volts; }

    // What the panel shows.
    using Segments = std::array<std::array<uint16_t, kDigits>, kDisplays>;
    const Segments& display() const { return m_shown; }
    uint8_t led1() const { return m_led1; } // active low, pot board U14
    uint8_t led2() const { return m_led2; } // active low, processor board U24
    uint8_t haltSet() const { return m_haltSet; }

    // Bus
    uint8_t read(uint16_t address) override;
    void write(uint16_t address, uint8_t data) override;
    void interruptAcknowledge(int line) override;

    const M6809& cpu() const { return m_cpu; }

private:
    void catchUp(uint64_t cycle);
    void displayWrite(uint16_t offset, uint8_t data);
    uint8_t adcValue(int channel) const;

    const uint8_t* m_rom;
    VoiceBoard& m_voice;
    M6809 m_cpu;
    std::array<uint8_t, kRamSize> m_ram {};
    bool m_protect = false;

    // 40103 (U12) at 250 kHz (one count per 8 E cycles), preset from INTSET*, FIRQ on terminal count.
    uint64_t m_timerTime = 0;
    int m_timerPhase = 0;
    int m_timerCount = 0xff;
    uint8_t m_timerPreset = 0xff; // pulled high until written
    bool m_firq = false;

    // 6850 ACIA (U26): 500 kHz / 16 = 31.25 kbaud, 10 bits = 640 E cycles a byte.
    uint8_t m_aciaControl = 0x03;
    bool m_rdrf = false, m_tdre = true, m_overrun = false;
    uint8_t m_rxData = 0;
    uint64_t m_rxBusyUntil = 0;
    std::deque<uint8_t> m_rxQueue;
    uint64_t m_txDoneAt = 0;
    bool m_txBusy = false;
    uint8_t m_txData = 0;
    std::vector<uint8_t> m_txOut;
    void updateAciaIrq();

    uint8_t m_haltSet = 0xff;
    uint8_t m_led1 = 0xff, m_led2 = 0xff;
    uint8_t m_pull = 0;
    int m_cvSelect = 7;
    bool m_cvInhibit = true;
    uint8_t m_adcResult = 0;
    std::array<double, 8> m_cvIn;

    std::array<uint8_t, 8> m_switches; // active low, by column
    std::array<bool, kEncoders> m_encDir {}, m_encChanged {};
    std::array<int, kEncoders> m_encPending {};
    std::array<uint64_t, kEncoders> m_encNext {};

    std::array<uint16_t, kDisplays> m_anode {};
    int m_grid = -1;
    Segments m_shown {};
};

} // namespace xpander
