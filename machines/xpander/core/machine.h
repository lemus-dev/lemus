// The Xpander: the processor/pot/display boards (mainboard.h), the voice board's CPU side
// (voiceboard.h) and its six analog voices (voice.h), the noise generator on the processor board,
// the output section (research.md 3.5): the pan muxes U102-U107 (muted by AUTO*), the U101
// summers, the master volume and U1's buffers; and the LM311 (U108) that turns the six voices'
// sum into OSC for the voice CPU's tuning. The analog side runs at 4x the host rate; both CPUs at
// their 2 MHz E clock, interleaved with it one analog sample at a time.
#pragma once

#include "decimator.h"
#include "mainboard.h"
#include "voice.h"
#include "voiceboard.h"

#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace xpander {

class Machine
{
public:
    static constexpr int kVoices = 6;
    static constexpr double kCpuHz = 2e6;
    static constexpr double kFullScale = 10.0; // volts at the outputs for 0 dBFS

    // mainRom: 32 KB (exp1.2-0..3); voiceRom: 16 KB (ca1.4-0, ca1.4-1).
    Machine(const uint8_t* mainRom, const uint8_t* voiceRom);

    void prepare(double hostRate);
    // The POWER switch: the CPUs start from reset; the battery RAM stays.
    void powerOn();
    // n host samples into left/right (and the six VOICE outs, or nullptr).
    void process(float* left, float* right, int n, float* const* voiceOuts = nullptr);
    // The CPUs alone for n host samples, the voices not computed (OSC stays low): booting and
    // waiting on the firmware where nothing is heard.
    void idle(int n);

    MainBoard& main() { return m_main; }
    VoiceBoard& voiceBoard() { return m_voiceBoard; }
    const Voice& voice(int i) const { return m_voices[size_t(i)]; }
    Voice& voice(int i) { return m_voices[size_t(i)]; }
    // The six voices' solvers since the last change of rate (solver_stats.h).
    Solvers solverStats() const
    {
        Solvers s;
        for (const auto& v : m_voices)
            s += v.solverStats();
        return s;
    }
    // VOLUME, 0-1 of its rotation (50K dual audio-taper pot).
    void setVolume(double v) { m_volume = v; }
    int oversampling() const { return 1 << m_os.size(); }
    double latencySamples() const { return m_decL.latency(); }
    uint64_t cycles() const { return m_main.now(); }
    // Sleep: voices whose VCA is shut and whose output has been silent for 100 ms aren't computed
    // (exact zeros); the S&Hs and both CPUs run on. A voice wakes when its VCA CV rises.
    bool asleep() const
    {
        for (const bool a : m_asleep)
            if (!a)
                return false;
        return true;
    }
    int voicesAsleep() const
    {
        int n = 0;
        for (const bool a : m_asleep)
            n += a;
        return n;
    }

private:
    void applyEvents();
    double noise();

    std::vector<uint8_t> m_mainRom, m_voiceRom;
    VoiceBoard m_voiceBoard;
    MainBoard m_main;
    std::array<Voice, kVoices> m_voices;
    std::array<bool, kVoices> m_asleep {};
    std::array<int, kVoices> m_quiet {};

    double m_hostRate = 48000, m_rate = 192000;
    bool m_prepared = false;
    std::array<int, 2> m_os {}; // two half-band stages: 4x
    double m_cyclesPerSample = 0, m_cycleDebt = 0;
    uint64_t m_cycle = 0; // the bus cycle the analog side has reached: both CPUs run to it
    double m_volume = 0.8;
    // The voice board's S&H: the voice whose S&H is connected to the DAC, and the bus cycle its
    // charge has been brought up to.
    int m_selVoice = -1;
    uint64_t m_shCycle = 0;
    // The OSC comparator: its last input difference.
    double m_oscLast = -1;
    bool m_osc = false;
    std::vector<VoiceBoard::OscEdge> m_edges;
    // Noise generator (Q3, U29): its op-amp's output state.
    std::mt19937 m_rng { 0x58504e44 };
    double m_noiseSign = 1;
    double m_noiseFlip = 0; // probability of a flip in one sample
    std::normal_distribution<double> m_gauss { 0.0, 1.0 };
    // The band (a bandpass biquad) and the high-pass on the sum (biquad), direct form I states.
    double m_bandB[3] {}, m_bandA[3] {}, m_bandX[2] {}, m_bandY[2] {}, m_bandScale = 0;
    double m_hpB[3] {}, m_hpA[3] {}, m_hpX[2] {}, m_hpY[2] {};

    Decimator m_decL, m_decR;
    std::array<Decimator, kVoices> m_decV;
    std::vector<float> m_bufL, m_bufR;
    std::array<std::vector<float>, kVoices> m_bufV;
};

} // namespace xpander
