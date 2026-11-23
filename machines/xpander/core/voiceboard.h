// The Xpander's voice board, digital side (schematic 1808 sheets 1-2, SM p51-53; research.md 4-6):
// the voice 68B09 (U907) with its ROMs (U912, U914), RAM (U917), the 8253 (U921), DATAOUT/DATAIN,
// the three per-voice latches, and the DAC's address and data latches. What reaches the analog side
// is a list of timed events: S&H selections with the DAC's output voltage, deselections, latch
// writes and DATAOUT changes, each stamped with the bus cycle it happened in.
#pragma once

#include "m6809/m6809.h"
#include "pit8253.h"

#include <array>
#include <cstdint>
#include <vector>

namespace xpander {

struct VoiceEvent
{
    enum Kind : uint8_t
    {
        Select,   // an S&H connected to the DAC (VOICEN* write): voice, cv, volts, fast
        Deselect, // every S&H disconnected (CLEAR* write)
        Latch,    // voice latch `cv` (0 MDAC, 1 waveforms/sync/FM, 2 filter mode/noise/pan) = data
        DataOut,  // DATAOUT = data
    };
    uint64_t cycle;
    Kind kind;
    uint8_t voice; // 0-5 = voice 1-6
    uint8_t cv;    // S&H channel 0-7, or 8 for RES; latch number for Latch
    uint8_t data;
    bool fast;     // FAST*: the S&H's second capacitor is written directly too
    float volts;   // the DAC net (U813 output) while selected
};

class VoiceBoard final : public M6809::Bus
{
public:
    static constexpr int kRamSize = 0x2000;
    // S&H channel numbers as the voice board drawing (Rev B) has them; Rev D swaps 0 and 1.
    enum Cv { CvVcaOrPw1 = 0, CvPw1OrVca = 1, CvVolA = 2, CvVcof1 = 3, CvVolB = 4, CvVcff = 5, CvPw2 = 6, CvVcof2 = 7, CvRes = 8 };

    // rom: 16 KB, U912 then U914 ($C000-$FFFF).
    explicit VoiceBoard(const uint8_t* rom);

    // RES* from the main board (HALTSET bit 4): held in reset while asserted.
    void setReset(bool asserted);
    // HALTREQ0* from the main board; HALT* reaches the CPU unless HALTDS is set.
    void setHaltRequest(bool asserted) { m_haltReq = asserted; updateHalt(); }
    // HALTAKN*: the CPU has stopped and released its bus (BA high), so the main CPU owns the RAM.
    bool haltAcknowledged() const { return m_haltReq && m_cpu.busAvailable(); }
    uint8_t* ram() { return m_ram.data(); }

    // Runs to bus cycle `end`, counted from power-on. The CPU may finish an instruction a few cycles
    // past it; the next call then runs that much less, so the CPU's time never drifts from the
    // analog side's. `osc` lists the OSC comparator's transitions before `end`: the bus cycle
    // (fractional) and the new level, in order. The 8253 takes each at its own cycle.
    struct OscEdge { double at; bool level; };
    void run(uint64_t end, const OscEdge* osc, int edges);

    // The temperature-sensor voltages of the six CEM3374s (U805 ch1-6), in volts.
    void setTempSensors(const std::array<double, 6>& v) { m_tempSensor = v; }

    // The events since the last take, oldest first.
    std::vector<VoiceEvent>& events() { return m_events; }
    uint64_t now() const { return m_cpu.totalCycles(); }
    bool mute() const { return m_mute; }
    uint8_t dataOut() const { return m_dataOut; }
    bool osc() const { return m_osc; }
    const M6809& cpu() const { return m_cpu; }

    // The analog constants of the DAC chain (research.md 4.2).
    struct Dac
    {
        double refFixed = 3.43 * 1.416; // U815A ×3.43: REF in normal mode (4.86 V)
        double refGain = 3.43;          // U815B
        double hresFine = 10.0 / 30.1;  // R849
        double hresRef = 10.0 / 17.4;   // R850
        double eco609 = 10.0 / 422.0;   // ECO 609: 422K from REF into the summing node
        double clampHigh = 5.0;         // D802/D803 into the +5 V node
        double offset = 0.0;            // U812's offset, trimmed to 0 ± 1 mV by T801 (SM p34)
    };
    Dac dac;

    // Bus
    uint8_t read(uint16_t address) override;
    void write(uint16_t address, uint8_t data) override;
    void interruptAcknowledge(int line) override;

private:
    void updateHalt() { m_cpu.setHalt(m_haltReq && !(m_dataOut & 1)); }
    void catchUp(uint64_t cycle);
    void setGate2();
    void dacWrite(uint16_t address, uint8_t data, uint64_t cycle);
    double dacVolts(uint16_t code, double ref) const { return double(code) / 16384.0 * ref; }

    const uint8_t* m_rom;
    std::array<uint8_t, kRamSize> m_ram {};
    M6809 m_cpu;
    Pit8253 m_pit;
    uint64_t m_pitTime = 0; // bus cycles the PIT has been clocked to
    bool m_reset = true;
    bool m_haltReq = false;
    bool m_irq = false;
    bool m_out2 = true;
    bool m_osc = false;
    bool m_mute = true;
    uint8_t m_dataOut = 0;

    // DAC: the 7 MSBs latched by CLEAR* (U810), FAST from its D7, the fine sample and the reference.
    uint16_t m_dacCode = 0;
    bool m_fastLatched = false;
    double m_fine = 0;         // U815D: the sampled U812 output (volts, positive sense)
    double m_ref = 3.43 * 1.416;
    std::array<double, 6> m_tempSensor { 2.5, 2.5, 2.5, 2.5, 2.5, 2.5 };

    std::vector<VoiceEvent> m_events;
};

} // namespace xpander
