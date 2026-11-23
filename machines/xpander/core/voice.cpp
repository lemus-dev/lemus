#include "voice.h"

#include <cstring>

#include <algorithm>
#include <cmath>

namespace xpander {

namespace {

constexpr double kPi = 3.14159265358979323846; // kPi isn't standard (MSVC lacks it)

// Smoothing S&H (UX06/UX07, research.md 4.3): C1 0.033 uF written through the 4051, R 1M, C2
// 0.0068 uF to the destination; FAST also writes C2 directly.
constexpr double kC1 = 0.033e-6, kC2 = 0.0068e-6, kRsh = 1e6;
constexpr double kRonSh = 250;   // CD4051 at 17 V across (VDD +5, VEE -12), UX06/07 and UX17 [C: data sheet range]
constexpr double kRon4016 = 600; // CD4016 at 5.6 V across (VDD +5A, VSS -0.6) [C]
constexpr double kRon4053 = 125; // CD4053B, typical at 15 V [D]

// Waveform paths into the mix buses (research.md 3.1).
constexpr double kRTri = 47e3;                        // RX42, RX31
constexpr double kRSawIn = 100e3, kRSawShunt = 10e3;  // RX45/RX44, RX33/RX32
constexpr double kRSawLoad = 47e3, kRSawOut = 3.3e3;  // RX46/RX47, RX34/RX35
constexpr double kRPulse = 360e3, kPulseHigh = 10.5 - 0.6; // TL082 high, less DX05/DX04
constexpr double kRNoise = 3.3e3 + 47;                // RX61 and the source's R44
constexpr double kRBus = 680;                         // RX56, RX40 at pins 6 and 7
constexpr double kCBus = 2.2e-6;                      // CX37, CX33

// Exponential control inputs: 4.22K over 100R (RX26/RX27, RX21/RX22), 0.1 uF.
constexpr double kVcinGain = 100.0 / 4320.0;
constexpr double kVcinTau = (4220.0 * 100.0 / 4320.0) * 0.1e-6;
// Pin 15: RX53 38.3K from VCFF, RX52 196K from -5 V, RX54 1K to ground; RX55 39K from VCFFM.
constexpr double kG15 = 1 / 38.3e3 + 1 / 196e3 + 1 / 1e3;
constexpr double kR15 = 1 / kG15;
constexpr double kFm15 = kR15 / (39e3 + kR15);
// Pin 13: RX51 18K, CX40 0.1 uF.
constexpr double kVcaTau = 18e3 * 0.1e-6;

// The pole-mix network (research.md 3.3): per 4051 channel, the resistor from each stage bus
// (s1 +101, s2 -101, s3 +101, s4 -VCF OUT) into UX15A's summing node; 0 for none.
constexpr double kMix[8][4] = {
    {0, 100e3, 49.9e3, 100e3},     {100e3, 33.2e3, 33.2e3, 100e3}, {100e3, 33.2e3, 16.5e3, 24.9e3},
    {100e3, 49.9e3, 49.9e3, 0},    {0, 62e3, 0, 0},                {100e3, 49.9e3, 100e3, 0},
    {0, 0, 0, 56e3},               {100e3, 100e3, 0, 0},
};
constexpr double kRMixFb = 68e3;  // RX66
constexpr double kRail = 11.0;    // TL07x/TL08x on +-12 V
constexpr double kRVcaIn = 33e3;  // RX62 after ECO 602
constexpr double kRVcaOut = 33e3; // RX70 after ECO 602

double hpCoef(double hz, double T) { return 1 - std::exp(-2 * kPi * hz * T); }

} // namespace

void Voice::prepare(double rate)
{
    m_rate = rate;
    m_T = 1.0 / rate;
    {
        const double T = m_T;
        static const double tau = kRsh * kC1 * kC2 / (kC1 + kC2);
        m_eSh = std::exp(-T / tau);
        m_kVcin = 1 - std::exp(-T / kVcinTau);
        m_kVca = 1 - std::exp(-T / kVcaTau);
        m_ctlQuiet = false;
        m_hpMdac = hpCoef(1 / (2 * kPi * 10e3 * 2.2e-6), T);
        m_hpLin = hpCoef(1 / (2 * kPi * 422e3 * 0.1e-6), T);
        m_hpVcfFm = hpCoef(1 / (2 * kPi * (39e3 + kR15) * 0.1e-6), T);
        m_hpVcaIn = hpCoef(1 / (2 * kPi * kRVcaIn * 2.2e-6), T);
        m_hpOut = hpCoef(1 / (2 * kPi * 10e3 * 2.2e-6), T);
    }
    m_vco1.prepare(rate);
    m_vco2.prepare(rate);
    m_vcf.prepare(rate);
    // RES in: CX48 2.2 uF into RX64 20K (ECO 603) + RX63 1.2K, beside RX68 100K.
    const double load = 1.0 / (1.0 / 21.2e3 + 1.0 / 100e3);
    m_vcf.setResInput(1.2 / 21.2, 1.0 / (2 * kPi * 2.2e-6 * load));
    // The CVs and latches stay as the voice CPU left them: a new rate is not a power-on.
}

void Voice::reset()
{
    m_sh = {};
    m_selected = -1;
    m_latch = {};
    m_vco1.reset();
    m_vco2.reset();
    m_vcf.reset();
    m_vcf.setCaps(0.033e-6, 0.033e-6, 0.033e-6, 330e-12);
    m_vcf.setCell1Series(kRon4053); // UX14b selects cell 1's capacitor (FD)
    m_vcof1Pin = m_vcof2Pin = m_vcaPin = 0;
    m_ctlQuiet = false;
    m_busA = m_busB = m_mdacIn = m_linFm = m_vcfFm = m_vcaIn = m_out = m_lastTri2 = 0;
    m_pinA = m_pinB = 0;
}

void Voice::select(int channel, double volts, bool fast)
{
    m_selected = channel;
    m_selVolts = volts;
    m_selFast = fast && channel < 8; // the RES S&H has no fast path
}

void Voice::charge(uint32_t cycles)
{
    // C1 through UX06's R_on from the DAC net and, with FAST, C2 through UX07's: exact
    // exponentials over the cycles connected (a sample is about 10; a table covers those).
    struct Table
    {
        std::array<double, 64> c1 {}, c2 {};
        Table()
        {
            for (size_t n = 0; n < c1.size(); ++n)
            {
                c1[n] = 1 - std::exp(-double(n) / (kBusHz * kRonSh * kC1));
                c2[n] = 1 - std::exp(-double(n) / (kBusHz * kRonSh * kC2));
            }
        }
    };
    static const Table table;
    if (m_selected < 0 || cycles == 0)
        return;
    auto& s = m_sh[size_t(m_selected)];
    s.still = false;
    m_ctlQuiet = false;
    const bool inTable = cycles < table.c1.size();
    s.c1 += (m_selVolts - s.c1) * (inTable ? table.c1[cycles] : 1 - std::exp(-double(cycles) / (kBusHz * kRonSh * kC1)));
    if (m_selFast)
        s.c2 += (m_selVolts - s.c2) * (inTable ? table.c2[cycles] : 1 - std::exp(-double(cycles) / (kBusHz * kRonSh * kC2)));
}

void Voice::latch(int which, uint8_t data)
{
    m_latch[size_t(which)] = data;
    if (which == 2)
        m_vcf.setCaps((data & 8) ? 100e-12 : 0.033e-6, 0.033e-6, 0.033e-6, 330e-12); // FD: UX14b, CX45/CX46
}

void Voice::controlTick()
{
    // Every S&H still and the pins at their fixed points last time, with no charge since: this tick
    // would change nothing (CPU round 23: most of an idle machine's voice time).
    if (m_ctlQuiet)
        return;
    bool moving = false;
    // The S&Hs between writes: C1 and C2 share their charge through 1M.
    {
        const double e = m_eSh;
        for (auto& s : m_sh)
        {
            if (s.still)
                continue;
            const double mean = (kC1 * s.c1 + kC2 * s.c2) / (kC1 + kC2);
            const double d = (s.c1 - s.c2) * e;
            const double c1 = mean + d * kC2 / (kC1 + kC2);
            const double c2 = mean - d * kC1 / (kC1 + kC2);
            s.still = std::memcmp(&c1, &s.c1, sizeof c1) == 0 && std::memcmp(&c2, &s.c2, sizeof c2) == 0;
            moving = moving || !s.still;
            s.c1 = c1;
            s.c2 = c2;
        }
    }

    // Control pins.
    const double p1 = m_vcof1Pin + (cvOf(3) * kVcinGain - m_vcof1Pin) * m_kVcin;
    const double p2 = m_vcof2Pin + (cvOf(7) * kVcinGain - m_vcof2Pin) * m_kVcin;
    const double p3 = m_vcaPin + (cvOf(chVca()) - m_vcaPin) * m_kVca;
    const bool same = std::memcmp(&p1, &m_vcof1Pin, sizeof p1) == 0 && std::memcmp(&p2, &m_vcof2Pin, sizeof p2) == 0 &&
                      std::memcmp(&p3, &m_vcaPin, sizeof p3) == 0;
    m_vcof1Pin = p1;
    m_vcof2Pin = p2;
    m_vcaPin = p3;
    m_ctlQuiet = !moving && same;
}

double Voice::audioTick(double noise, bool vca)
{
    const double T = m_T;
    const uint8_t waves = m_latch[1], mode = m_latch[2];
    const bool vcoFm = waves & 1, saw2 = waves & 2, tri1 = waves & 4, sync = waves & 8, tri2 = waves & 16, saw1 = waves & 32;

    // FM: VCO2's triangle through CX02 into the MDAC (VREF about 10K), times D/256, inverted by UX05A.
    m_mdacIn += (m_lastTri2 - m_mdacIn) * m_hpMdac;
    const double mdac = -(m_lastTri2 - m_mdacIn) * double(m_latch[0]) / 256.0;
    double iFm = 0, fmPin15 = 0;
    if (vcoFm)
    {
        // Linear FM: CX27 0.1 uF + RX28 422K into VCO1's IREF.
        m_linFm += (mdac - m_linFm) * m_hpLin;
        iFm = (mdac - m_linFm) / 422e3;
    }
    else
    {
        // Exponential FM of the VCF: CX34 0.1 uF + RX55 39K into pin 15.
        m_vcfFm += (mdac - m_vcfFm) * m_hpVcfFm;
        fmPin15 = (mdac - m_vcfFm) * kFm15;
    }

    // The oscillators: VCO1 is the chip's B side, VCO2 its A side, hard-reset by VCO1's saw edge
    // through CX21 when SYNC lifts DX03's clamp.
    m_vco1.tick(m_vcof1Pin, iFm, 2 * cvOf(chPw1()), 0);
    m_vco2.tick(m_vcof2Pin, 0, 2 * cvOf(6), sync ? m_vco1.sawDropAt() : 0);
    m_lastTri2 = m_vco2.triNow();

    // The mix buses: Norton sum of the switched paths into the coupling capacitor and 680R.
    auto sawPath = [](bool on, double saw, double& g) {
        if (!on)
        {
            g += 1 / (kRSawLoad + kRSawOut); // RX46/RX47 still hang on the bus
            return 0.0;
        }
        const double r1 = kRSawIn * kRSawShunt / (kRSawIn + kRSawShunt) + kRon4016;
        const double v2 = saw * kRSawShunt / (kRSawIn + kRSawShunt) * kRSawLoad / (kRSawLoad + r1);
        const double r2 = r1 * kRSawLoad / (r1 + kRSawLoad) + kRSawOut;
        g += 1 / r2;
        return v2 / r2;
    };
    auto bus = [&](double& vc, double is, double g) {
        const double ic = (is - g * vc) / (1 + kRBus * g);
        vc += ic * T / kCBus;
        return kRBus * ic;
    };
    double gA = 0, iA = 0;
    if (tri1)
    {
        gA += 1 / (kRTri + kRon4016);
        iA += m_vco1.tri() / (kRTri + kRon4016);
    }
    iA += sawPath(saw1, m_vco1.saw(), gA);
    gA += m_vco1.pulse() / kRPulse; // the comparator's high through DX05 and RX48 (open while low)
    iA += m_vco1.pulse() * kPulseHigh / kRPulse;
    double gB = 0, iB = 0;
    if (tri2)
    {
        gB += 1 / (kRTri + kRon4016);
        iB += m_vco2.tri() / (kRTri + kRon4016);
    }
    iB += sawPath(saw2, m_vco2.saw(), gB);
    gB += m_vco2.pulse() / kRPulse;
    iB += m_vco2.pulse() * kPulseHigh / kRPulse;
    if (mode & 16)
    {
        gB += 1 / (kRNoise + kRon4053);
        iB += noise / (kRNoise + kRon4053);
    }
    const double pinA = bus(m_busA, iA, gA);
    const double pinB = bus(m_busB, iB, gB);
    m_pinA = pinA;
    m_pinB = pinB;

    // The CEM3372.
    const double pin15 = (cvOf(5) / 38.3e3 - 5.0 / 196e3) * kR15 + fmPin15;
    m_vcf.filter(pinA, pinB, cvOf(2), cvOf(4), pin15, cvOf(8));

    // The pole mix: UX13's cell buffers, UX15B's inverted VCF OUT (after CX48), the 4051 (its R_on in
    // series with the channel's resistors into the virtual ground), UX15A.
    const double stage[4] = {101 * m_vcf.cell(0), -101 * m_vcf.cell(1), 101 * m_vcf.cell(2), -m_vcf.vcfOutCoupled()};
    const auto& r = kMix[mode & 7];
    double sum = 0, g = 0;
    for (int j = 0; j < 4; ++j)
        if (r[j] > 0)
        {
            sum += stage[j] / r[j];
            g += 1 / r[j];
        }
    double mix = -kRMixFb * sum / (1 + kRonSh * g);
    mix = kRail * std::tanh(mix / kRail);

    if (!vca)
        return 0; // waking: the oscillators and filter catch up, the VCA and the outputs wait

    // The final VCA through CX47/RX62, out through UX16 (RX70) and CX49 into the pan network.
    m_vcaIn += (mix - m_vcaIn) * m_hpVcaIn;
    const double iVca = m_vcf.vca((mix - m_vcaIn) / kRVcaIn, m_vcaPin);
    double v = -iVca * kRVcaOut;
    v = kRail * std::tanh(v / kRail);
    m_out += (v - m_out) * m_hpOut;
    return v - m_out;
}

} // namespace xpander
