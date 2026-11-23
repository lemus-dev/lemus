#include "machine.h"

#include <algorithm>
#include <cmath>

namespace xpander {

namespace {

constexpr double kPi = 3.14159265358979323846; // kPi isn't standard (MSVC lacks it)
// The noise generator's spectrum [F: a Matrix-12's noise with the filter open, Yxxwb4d8GHA 10:58]:
// the clipped broadband part flat to kNoiseCorner, a band of kNoiseBandDb under it around
// kNoiseBandHz, both through a second-order high-pass at kNoiseHpHz (1.5 dB rms over 25 Hz-13 kHz).
constexpr double kNoiseCorner = 6000.0;
constexpr double kNoiseBandHz = 128.0, kNoiseBandQ = 3.3, kNoiseBandDb = -8.2, kNoiseHpHz = 73.0;
// The voices' levels against their mean, dB: no two are the same on a unit, and nothing trims them
// (voice 1.4's TUNE VCA, F176, only toggles the latches). [F: one unit's six voices, freesound
// 17457 "Melodirt", one note per voice in turn: 0.30 dB rms, each voice within 0.04 dB.]
constexpr double kVoiceLevelDb[6] = {-0.30, +0.42, +0.13, -0.12, +0.31, -0.41};
} // namespace

namespace {

// Pan mux channels (research.md 3.5): gains into the L and R summers (-10K / R); 7 is the voice's
// own output only.
constexpr double kPanL[8] = {-1.0, -10.0 / 12, -10.0 / 15, -0.5, -10.0 / 30, -10.0 / 56, 0.0, 0.0};
constexpr double kPanR[8] = {0.0, -10.0 / 56, -10.0 / 30, -0.5, -10.0 / 15, -10.0 / 12, -1.0, 0.0};

// U108 (LM311): - input = the six VOICE nets through R178-R183 10K with R186 4.7K to ground (ECO
// 602); + input R184 47K to ground. Input bias 100 nA (both inputs), offset 2 mV [D: typical].
constexpr double kRVoiceOsc = 10e3, kROscGnd = 4.7e3, kROscPlus = 47e3;
constexpr double kOscBias = 100e-9, kOscOffset = 2e-3;

// The output: U1 (TL072) on +-12 V.
constexpr double kOutRail = 10.5;

double audioTaper(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    constexpr double a = 4.394; // 10 % at mid rotation [C]
    return (std::exp(a * x) - 1) / (std::exp(a) - 1);
}

} // namespace

Machine::Machine(const uint8_t* mainRom, const uint8_t* voiceRom)
    : m_mainRom(mainRom, mainRom + 0x8000), m_voiceRom(voiceRom, voiceRom + 0x4000),
      m_voiceBoard(m_voiceRom.data()), m_main(m_mainRom.data(), m_voiceBoard)
{
    m_edges.reserve(64);
}

void Machine::prepare(double hostRate)
{
    if (m_prepared && hostRate == m_hostRate)
        return;
    m_prepared = true;
    m_hostRate = hostRate;
    const int os = 4;
    m_rate = hostRate * os;
    m_cyclesPerSample = kCpuHz / m_rate;
    // Flips at lambda = pi x corner a second: odd flip counts in one sample, (1 - e^(-2 lambda T)) / 2.
    m_noiseFlip = (1 - std::exp(-2 * kPi * kNoiseCorner / m_rate)) / 2;
    {
        // RBJ biquads: the band (0 dB at its centre) and a Butterworth high-pass.
        const double w = 2 * kPi * kNoiseBandHz / m_rate, al = std::sin(w) / (2 * kNoiseBandQ);
        const double a0 = 1 + al;
        m_bandB[0] = al / a0, m_bandB[1] = 0, m_bandB[2] = -al / a0;
        m_bandA[0] = 1, m_bandA[1] = -2 * std::cos(w) / a0, m_bandA[2] = (1 - al) / a0;
        // white of unit variance through it: variance pi f0 / (Q rate); scaled to the band's level
        m_bandScale = std::pow(10.0, kNoiseBandDb / 20) / std::sqrt(kPi * kNoiseBandHz / (kNoiseBandQ * m_rate));
        const double wh = 2 * kPi * kNoiseHpHz / m_rate, ah = std::sin(wh) / (2 * 0.70710678118654752), ch = std::cos(wh);
        const double h0 = 1 + ah;
        m_hpB[0] = (1 + ch) / 2 / h0, m_hpB[1] = -(1 + ch) / h0, m_hpB[2] = (1 + ch) / 2 / h0;
        m_hpA[0] = 1, m_hpA[1] = -2 * ch / h0, m_hpA[2] = (1 - ah) / h0;
    }
    for (size_t i = 0; i < m_voices.size(); ++i)
    {
        m_voices[i].prepare(m_rate);
        m_voices[i].setRevD(true);
        m_voices[i].seedNoise(uint32_t(0x9e3779b9u * (i + 1)));
        m_voices[i].setVcaGain(std::pow(10.0, kVoiceLevelDb[i] / 20));
    }
    m_decL.setStages(2);
    m_decR.setStages(2);
    for (auto& d : m_decV)
        d.setStages(2);
    m_bufL.assign(size_t(os), 0);
    m_bufR.assign(size_t(os), 0);
    for (auto& b : m_bufV)
        b.assign(size_t(os), 0);
}

void Machine::powerOn()
{
    for (auto& v : m_voices)
        v.reset();
    m_asleep.fill(false);
    m_quiet.fill(0);
    m_voiceBoard.events().clear();
    m_selVoice = -1;
    m_main.powerOn();
    m_cycle = std::max(m_main.now(), m_voiceBoard.now());
    m_shCycle = m_cycle;
}

double Machine::noise()
{
    // Q3's reverse-biased junction into U29 (LM301, gain rising with frequency, no compensation):
    // its output clipped at the rails, near-binary, +-11 V, through R41 10K over R44 47R: +-51 mV
    // ("100 mV pTop") [R]: a random telegraph signal, flat to its corner. Beside it a band of noise
    // around 128 Hz, and a high-pass on both: C27 and the junction's source impedance at the input,
    // the uncompensated LM301's own gain at the top [F: their shape fitted to a Matrix-12's noise].
    if (std::uniform_real_distribution<double>(0.0, 1.0)(m_rng) < m_noiseFlip)
        m_noiseSign = -m_noiseSign;
    const double g = m_gauss(m_rng);
    const double band = m_bandB[0] * g + m_bandB[1] * m_bandX[0] + m_bandB[2] * m_bandX[1] - m_bandA[1] * m_bandY[0] -
                        m_bandA[2] * m_bandY[1];
    m_bandX[1] = m_bandX[0], m_bandX[0] = g, m_bandY[1] = m_bandY[0], m_bandY[0] = band;
    const double x = m_noiseSign + band * m_bandScale;
    const double y = m_hpB[0] * x + m_hpB[1] * m_hpX[0] + m_hpB[2] * m_hpX[1] - m_hpA[1] * m_hpY[0] - m_hpA[2] * m_hpY[1];
    m_hpX[1] = m_hpX[0], m_hpX[0] = x, m_hpY[1] = m_hpY[0], m_hpY[0] = y;
    return y * 11.0 * 47.0 / 10047.0;
}

void Machine::applyEvents()
{
    // The events of the bus cycles the CPUs ran since the last sample, in order. The S&H connected
    // to the DAC charges for every cycle it stays connected: up to each event, then to the end of
    // the interval, so one held selected for milliseconds (DAC MONO measures VCO 1 while its S&H
    // is) has its new voltage from the start, not when the next CLEAR* lets it go.
    auto chargeTo = [this](uint64_t cycle) {
        if (cycle <= m_shCycle)
            return;
        if (m_selVoice >= 0)
            m_voices[size_t(m_selVoice)].charge(uint32_t(std::min<uint64_t>(cycle - m_shCycle, 1u << 30)));
        m_shCycle = cycle;
    };
    auto& ev = m_voiceBoard.events();
    for (const auto& e : ev)
    {
        switch (e.kind)
        {
            case VoiceEvent::Select:
                chargeTo(e.cycle);
                if (m_selVoice >= 0)
                    m_voices[size_t(m_selVoice)].deselect();
                m_selVoice = e.voice;
                m_voices[e.voice].select(e.cv, e.volts, e.fast);
                break;
            case VoiceEvent::Deselect:
                chargeTo(e.cycle);
                if (m_selVoice >= 0)
                    m_voices[size_t(m_selVoice)].deselect();
                m_selVoice = -1;
                break;
            case VoiceEvent::Latch: m_voices[e.voice].latch(e.cv, e.data); break;
            case VoiceEvent::DataOut: break;
        }
    }
    ev.clear();
    chargeTo(m_cycle);
}

void Machine::idle(int n)
{
    const int os = 4;
    for (int i = 0; i < n * os; ++i)
    {
        applyEvents();
        m_cycleDebt += m_cyclesPerSample;
        const int cycles = int(m_cycleDebt);
        m_cycleDebt -= cycles;
        const uint64_t end = m_cycle + uint64_t(cycles);
        if (m_osc)
        {
            m_osc = false;
            m_edges.assign(1, {double(m_cycle), false});
            m_voiceBoard.run(end, m_edges.data(), 1);
        }
        else
            m_voiceBoard.run(end, nullptr, 0);
        m_main.run(end);
        m_cycle = end;
    }
}

void Machine::process(float* left, float* right, int n, float* const* voiceOuts)
{
    const int os = 4;
    const double vol = audioTaper(m_volume);
    // The bias currents at U108's inputs: + sits at -Ib 47K, - at -Ib (its Thevenin resistance).
    const double rMinus = 1.0 / (kVoices / kRVoiceOsc + 1.0 / kROscGnd);
    const double vPlus = -kOscBias * kROscPlus + kOscOffset;
    for (int i = 0; i < n; ++i)
    {
        for (int k = 0; k < os; ++k)
        {
            applyEvents();
            const double nz = noise();
            const bool mute = m_voiceBoard.mute();
            double l = 0, r = 0, sumOsc = 0;
            double out[kVoices];
            for (int v = 0; v < kVoices; ++v)
            {
                auto& voice = m_voices[size_t(v)];
                double x = 0;
                if (m_asleep[size_t(v)])
                {
                    if (voice.vcaShut())
                        voice.controlTick();
                    else
                    {
                        // Waking: the oscillators and filter run on for 5 ms with the controls as
                        // they are, so the note doesn't start from a stale filter; then on as usual.
                        for (int w = 0, n = int(0.005 * m_rate); w < n; ++w)
                            voice.audioTick(0.0, false);
                        m_asleep[size_t(v)] = false;
                        m_quiet[size_t(v)] = 0;
                        x = voice.tick(nz);
                    }
                }
                else
                {
                    x = voice.tick(nz);
                    // Asleep once the VCA is shut and VOICE X has stayed under -120 dB (re 10 V) for 100 ms.
                    if (voice.vcaShut() && std::fabs(x) < kFullScale * 1e-6)
                    {
                        if (++m_quiet[size_t(v)] >= int(0.1 * m_rate))
                        {
                            m_asleep[size_t(v)] = true;
                            voice.silenceOutput();
                        }
                    }
                    else
                        m_quiet[size_t(v)] = 0;
                }
                out[v] = x;
                sumOsc += x / kRVoiceOsc;
                const int pan = m_voices[size_t(v)].panChannel();
                if (!mute)
                {
                    l += kPanL[pan] * x;
                    r += kPanR[pan] * x;
                }
            }
            // OSC: high while + is above -.
            const double vMinus = sumOsc * rMinus - kOscBias * rMinus;
            const double d = vPlus - vMinus;
            m_edges.clear();
            const bool osc = d > 0;
            if (osc != m_osc)
            {
                const double f = m_oscLast != d ? std::clamp(m_oscLast / (m_oscLast - d), 0.0, 1.0) : 1.0;
                m_osc = osc;
                m_edges.push_back({f, osc});
            }
            m_oscLast = d;

            // The CPUs for the same interval.
            m_cycleDebt += m_cyclesPerSample;
            const int cycles = int(m_cycleDebt);
            m_cycleDebt -= cycles;
            const uint64_t end = m_cycle + uint64_t(cycles);
            for (auto& e : m_edges)
                e.at = double(m_cycle) + e.at * cycles;
            m_voiceBoard.run(end, m_edges.data(), int(m_edges.size()));
            m_main.run(end);
            m_cycle = end;

            // U101 (summing, inverting) -> VOLUME -> U1 followers.
            const double lo = std::clamp(-l * vol, -kOutRail, kOutRail);
            const double ro = std::clamp(-r * vol, -kOutRail, kOutRail);
            m_bufL[size_t(k)] = float(lo / kFullScale);
            m_bufR[size_t(k)] = float(ro / kFullScale);
            if (voiceOuts != nullptr)
                for (int v = 0; v < kVoices; ++v)
                    m_bufV[size_t(v)][size_t(k)] =
                        float((!mute && m_voices[size_t(v)].panChannel() == 7) ? out[v] / kFullScale : 0.0);
        }
        left[i] = m_decL.process(m_bufL.data());
        right[i] = m_decR.process(m_bufR.data());
        if (voiceOuts != nullptr)
            for (int v = 0; v < kVoices; ++v)
                if (voiceOuts[v] != nullptr)
                    voiceOuts[v][i] = m_decV[size_t(v)].process(m_bufV[size_t(v)].data());
    }
}

} // namespace xpander
