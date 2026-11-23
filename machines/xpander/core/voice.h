// One Xpander voice, analog side (voice board 1808 sheet 3, SM p55; research.md 3-4): the nine
// smoothing sample-and-holds the voice CPU writes through the DAC, the CEM3374's two VCOs with the
// waveform switches (4016), the saw-to-pulse comparators and sync, the FM MDAC (AD7523) with its
// two destinations, the two mix buses into the CEM3372, the pole-mixing network (4051 and the
// cell buffers) and the final VCA with its I/V stage. Its output is VOICE X, before the pan mux.
#pragma once

#include "cem3372.h"
#include "cem3374.h"

#include <array>
#include <cstdint>

namespace xpander {

class Voice
{
public:
    // S&H channels as the voice board drawing (Rev B) numbers them; Rev D swaps 0 and 1.
    static constexpr int kChannels = 9; // 0-7 and RES

    // The host rate changed: the filters restart, the CVs and latches stay.
    void prepare(double rate);
    // Power-on: everything cleared.
    void reset();
    // Voice board revision D: S&H 0 is PW1 and 1 the VCA (research.md 7).
    void setRevD(bool on) { m_revD = on; }
    void seedNoise(uint32_t s) { m_vcf.seed(s); }
    // This voice's final VCA gain against the data sheet's typical (component spread).
    void setVcaGain(double g) { m_vcf.params.vcaMax = Cem3372::Params {}.vcaMax * g; }

    // The DAC's S&H events, in bus order: an S&H connected to the DAC net (VOICEN*), charged for
    // each stretch of bus cycles it stays connected, and let go (the next CLEAR*). The firmware
    // keeps one selected for as long as it likes: DAC MONO measures the VCO while its S&H is.
    static constexpr double kBusHz = 2e6; // the voice CPU's E clock, the events' time base
    void select(int channel, double volts, bool fast);
    void charge(uint32_t cycles);
    void deselect() { m_selected = -1; }
    void latch(int which, uint8_t data);
    const std::array<uint8_t, 3>& latches() const { return m_latch; }

    // One sample. noise: the NOISE net (volts); returns VOICE X (volts).
    double tick(double noise)
    {
        controlTick();
        return audioTick(noise, true);
    }
    // The S&Hs and the control pins alone: what keeps running while the voice sleeps.
    void controlTick();
    // The oscillators, buses, filter and (with vca) the VCA and output.
    double audioTick(double noise, bool vca);
    // Asleep: the VCA is shut (its S&H and pin below the cut-off) and nothing else can be heard.
    bool vcaShut() const { return cvOf(chVca()) < m_vcf.params.vcaOff - 0.02 && m_vcaPin < m_vcf.params.vcaOff; }
    void silenceOutput() { m_out = 0; m_vcaIn = 0; }

    // The CEM3374's temperature sensor (U805's input for this voice).
    double tempSensor() const { return 2.5; }
    // Probes for tests and the bench.
    double cv(int channel) const { return m_sh[size_t(channel)].c2; }
    int panChannel() const { return m_latch[2] >> 5; }
    double vcaCv() const { return m_vcaPin; }
    const Cem3374& vco1() const { return m_vco1; }
    const Cem3374& vco2() const { return m_vco2; }
    Cem3374& vco1() { return m_vco1; }
    Cem3374& vco2() { return m_vco2; }
    double vcfOut() const { return m_vcf.vcfOut(); }
    // The CEM3372's signal inputs as the last sample left them: pin 6 (INA) or pin 7 (INB).
    double vcfIn(int pin) const { return pin == 7 ? m_pinB : m_pinA; }
    // What the CEM3372's and the two CEM3374s' solvers did since prepare().
    Solvers solverStats() const
    {
        Solvers s;
        s.vcf = m_vcf.stats();
        s.vco = m_vco1.stats();
        s.vco += m_vco2.stats();
        return s;
    }

private:
    struct SampleHold
    {
        double c1 = 0, c2 = 0;
        bool still = false; // C1 and C2 have equalised to the last bit: sharing changes nothing
    };
    double cvOf(int ch) const { return m_sh[size_t(ch)].c2; }
    int chVca() const { return m_revD ? 1 : 0; }
    int chPw1() const { return m_revD ? 0 : 1; }

    double m_rate = 192000, m_T = 1.0 / 192000;
    // Per-sample coefficients of fixed time constants, computed once per rate.
    double m_eSh = 0, m_kVcin = 0, m_kVca = 0, m_hpMdac = 0, m_hpLin = 0, m_hpVcfFm = 0, m_hpVcaIn = 0, m_hpOut = 0;
    bool m_revD = true;
    std::array<SampleHold, kChannels> m_sh {};
    int m_selected = -1;
    double m_selVolts = 0;
    bool m_selFast = false;
    std::array<uint8_t, 3> m_latch {}; // 0 MDAC, 1 UX02 (waves, sync, FM), 2 UX01 (filter, noise, pan)

    Cem3374 m_vco1, m_vco2;
    Cem3372 m_vcf;

    // Control-voltage conditioning and couplings (one-pole states).
    double m_vcof1Pin = 0, m_vcof2Pin = 0; // 4.22K/100R with 0.1 uF at pins 12 and 7
    double m_vcaPin = 0;                   // RX51 18K, CX40 0.1 uF at pin 13
    bool m_ctlQuiet = false;               // controlTick changed nothing and nothing was charged since
    double m_busA = 0, m_busB = 0;         // CX37, CX33 (2.2 uF) charge, as volts
    double m_mdacIn = 0;                   // CX02 into VREF: low-pass state of the triangle
    double m_linFm = 0;                    // CX27 + RX28 into IREF
    double m_vcfFm = 0;                    // CX34 + RX55 into pin 15
    double m_vcaIn = 0;                    // CX47 + RX62
    double m_out = 0;                      // CX49 into the pan network
    double m_pinA = 0, m_pinB = 0;         // pins 6 and 7 (probe)
    double m_lastTri2 = 0;
};

} // namespace xpander
