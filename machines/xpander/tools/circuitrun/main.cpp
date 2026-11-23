// The model's blocks alone, for tools/spice/suite.py to hold against ngspice:
//   xpandercircuitrun vcf-ac PIN15 LEVEL F1 F2 ...       the CEM3372 alone, pin 6 to VCF OUT, no resonance:
//                                                         per frequency "Hz dB degrees" (LEVEL on pin 5)
//   xpandercircuitrun vco-hz VCIN                         the CEM3374 alone: the frequency (Hz) at VCIN (V on pin 12)
//   xpandercircuitrun voice-ac MODE VCFF F1 F2 ...        a voice, the NOISE net to VOICE X (and to VCF OUT), small
//                                                         signal: per frequency "Hz dBx degx dBv degv"
//   xpandercircuitrun voice-onset MODE VCFF [S]           the RES CV (V) where a kick at NOISE still rings S s on
//   xpandercircuitrun voice-osc MODE VCFF RES [S]         the oscillation's frequency (Hz) and VCF OUT's p-p (V)
//   xpandercircuitrun voice-vco VCOF1                     VCO1's frequency (Hz) through the voice's VCOF1 S&H
//   xpandercircuitrun bus WAVES PW1 PW2 VCOF1 VCOF2       pins 6 and 7 with UX02 = WAVES: "f1 f2 rms6 pp6 rms7 pp7"
//   xpandercircuitrun bus-noise AMP HZ                    pin 7 with a sine of AMP (V peak) on the NOISE net: "rms pp"
// --rate R first runs it at R instead of 768 kHz (the model's own rate is 4 x the host's).
// MODE is UX01's filter bits (FD x 8 + 4051 channel); every CV is an S&H voltage (Rev D order).
#include "xpander/core/cem3372.h"
#include "xpander/core/cem3374.h"
#include "xpander/core/voice.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace xpander;

namespace {
double kRate = 192000 * 4; // well above the model's own rate (--rate R to change it)
constexpr double kPi = 3.14159265358979323846;
using cplx = std::complex<double>;

// Rev D's S&H order (research.md 4.3): 0 PW1, 1 VCA, 2 VOLA, 3 VCOF1, 4 VOLB, 5 VCFF, 6 PW2, 7 VCOF2, 8 RES.
struct Setup
{
    int mode = 0;
    double vcff = 0, res = 0, volA = 0, volB = 5, vca = 5, pw1 = -0.2, pw2 = -0.2, vcof1 = 0, vcof2 = 0;
    uint8_t waves = 0, mdac = 0;
    bool noise = true;
};

// The firmware's way: the S&H written through the 4051 (FAST also writes C2); RES has no fast path,
// so it is written again and again, as each IRQ does, until C2 has it.
void prime(Voice& v, const Setup& s)
{
    v.prepare(kRate);
    v.setRevD(true);
    v.reset();
    const double cv[8] = {s.pw1, s.vca, s.volA, s.vcof1, s.volB, s.vcff, s.pw2, s.vcof2};
    for (int ch = 0; ch < 8; ++ch)
    {
        v.select(ch, cv[ch], true);
        v.charge(uint32_t(Voice::kBusHz)); // connected for 1 s
        v.deselect();
    }
    v.latch(0, s.mdac);
    v.latch(1, s.waves);
    v.latch(2, uint8_t((s.mode & 15) | (s.noise ? 16 : 0)));
    const int writes = s.res != 0 ? 16 : 1;
    for (int w = 0; w < writes; ++w)
    {
        v.select(8, s.res, false);
        v.charge(uint32_t(Voice::kBusHz)); // connected for 1 s
        v.deselect();
        for (int i = 0; i < int(0.03 * kRate); ++i)
            v.tick(0);
    }
}

// Multi-tone lock-in: every tone a whole number of cycles in a 1 s window (1 Hz grid).
struct Tones
{
    std::vector<double> hz;
    std::vector<double> phase;
    explicit Tones(int argc, char** argv, int first)
    {
        for (int a = first; a < argc; ++a)
        {
            double f = std::max(1.0, std::round(std::atof(argv[a])));
            if (std::find(hz.begin(), hz.end(), f) == hz.end())
                hz.push_back(f);
        }
        for (size_t k = 0; k < hz.size(); ++k)
            phase.push_back(kPi * double(k * k) / double(hz.size())); // Schroeder phases: a low crest factor
    }
    double at(long n, double amp) const
    {
        double x = 0;
        for (size_t k = 0; k < hz.size(); ++k)
            x += amp * std::sin(2 * kPi * hz[k] * double(n) / kRate + phase[k]);
        return x;
    }
};

struct LockIn
{
    std::vector<cplx> rot, step, x, y1, y2;
    explicit LockIn(const std::vector<double>& hz)
    {
        for (double f : hz)
        {
            rot.push_back(1);
            step.push_back(std::polar(1.0, -2 * kPi * f / kRate));
        }
        x.assign(hz.size(), 0);
        y1.assign(hz.size(), 0);
        y2.assign(hz.size(), 0);
    }
    void add(double in, double out1, double out2)
    {
        for (size_t k = 0; k < rot.size(); ++k)
        {
            x[k] += in * rot[k];
            y1[k] += out1 * rot[k];
            y2[k] += out2 * rot[k];
            rot[k] *= step[k];
            rot[k] /= std::abs(rot[k]);
        }
    }
};

void printResponse(cplx h)
{
    std::printf(" %.6f %.4f", 20 * std::log10(std::abs(h)), std::arg(h) * 180 / kPi);
}

// The kick and what follows: VCF OUT over the last quarter of `seconds`.
void ring(const Setup& s, double seconds, double& hz, double& pp)
{
    Voice v;
    prime(v, s);
    const long n = long(kRate * seconds), kick = std::lround(50e-6 * kRate);
    std::vector<double> y;
    for (long i = 0; i < n; ++i)
    {
        v.tick(i < kick ? 0.1 : 0.0); // 0.1 V for 50 us on the NOISE net, as the deck's kick
        if (i > n * 3 / 4)
            y.push_back(v.vcfOut());
    }
    double mean = 0;
    for (double x : y)
        mean += x;
    mean /= double(y.size());
    const auto mm = std::minmax_element(y.begin(), y.end());
    pp = *mm.second - *mm.first;
    // Rising crossings of the mean, interpolated.
    double first = -1, last = 0;
    int ups = 0;
    for (size_t i = 1; i < y.size(); ++i)
        if (y[i - 1] < mean && y[i] >= mean)
        {
            const double t = double(i - 1) + (mean - y[i - 1]) / (y[i] - y[i - 1]);
            if (first < 0)
                first = t;
            last = t;
            ++ups;
        }
    hz = ups > 1 ? (ups - 1) * kRate / (last - first) : 0;
}

// Saw drops of an oscillator as absolute times (samples), from tick results.
struct Drops
{
    std::vector<double> t;
    void see(long n, double at)
    {
        if (at > 0)
            t.push_back(double(n) + at);
    }
    double hz() const { return t.size() > 1 ? double(t.size() - 1) * kRate / (t.back() - t.front()) : 0; }
};
} // namespace

int main(int argc, char** argv)
{
    if (argc > 2 && !std::strcmp(argv[1], "--rate"))
    {
        kRate = std::atof(argv[2]);
        argc -= 2;
        argv += 2;
    }
    if (argc < 3)
        return 1;
    const char* cmd = argv[1];

    if (!std::strcmp(cmd, "vcf-ac") && argc > 4)
    {
        const double pin15 = std::atof(argv[2]), level = std::atof(argv[3]);
        const Tones tones(argc, argv, 4);
        Cem3372 f;
        f.prepare(kRate);
        LockIn li(tones.hz);
        const long settle = long(0.5 * kRate), window = long(kRate);
        for (long i = 0; i < settle + window; ++i)
        {
            const double x = tones.at(i, 1e-4); // 0.1 mV a tone at pin 6: small signal for the 2 V_T pair
            f.filter(x, 0, level, 0, pin15, 0);
            if (i >= settle)
                li.add(x, f.vcfOut(), 0);
        }
        for (size_t k = 0; k < tones.hz.size(); ++k)
        {
            std::printf("%.0f", tones.hz[k]);
            printResponse(li.y1[k] / li.x[k]);
            std::printf("\n");
        }
    }
    else if (!std::strcmp(cmd, "vco-hz"))
    {
        Cem3374 o;
        o.prepare(kRate);
        o.reset();
        const double vcin = std::atof(argv[2]);
        const double nominal = o.params.iRef * std::exp2(vcin / o.params.voltsPerOctave) / (2 * o.params.cT * o.params.vTh);
        const long settle = long(kRate * 4 / nominal) + 100;
        const long n = settle + long(kRate * std::max(0.2, 40 / nominal));
        Drops d;
        for (long i = 0; i < n; ++i)
        {
            o.tick(vcin, 0, -1, 0);
            if (i >= settle)
                d.see(i, o.sawDropAt());
        }
        std::printf("%.6f\n", d.hz());
    }
    else if (!std::strcmp(cmd, "voice-ac") && argc > 4)
    {
        Setup s;
        s.mode = std::atoi(argv[2]);
        s.vcff = std::atof(argv[3]);
        const Tones tones(argc, argv, 4);
        Voice v;
        prime(v, s);
        LockIn li(tones.hz);
        const long settle = long(1.5 * kRate), window = long(kRate);
        for (long i = 0; i < settle + window; ++i)
        {
            const double x = tones.at(i, 1e-3); // 1 mV a tone on the NOISE net
            const double out = v.tick(x);
            if (i >= settle)
                li.add(x, out, v.vcfOut());
        }
        for (size_t k = 0; k < tones.hz.size(); ++k)
        {
            std::printf("%.0f", tones.hz[k]);
            printResponse(li.y1[k] / li.x[k]);
            printResponse(li.y2[k] / li.x[k]);
            std::printf("\n");
        }
    }
    else if (!std::strcmp(cmd, "voice-onset") && argc > 3)
    {
        Setup s;
        s.mode = std::atoi(argv[2]);
        s.vcff = std::atof(argv[3]);
        const double seconds = argc > 4 ? std::atof(argv[4]) : 0.4;
        double lo = 0.5, hi = 5.0;
        for (int it = 0; it < 18; ++it)
        {
            s.res = (lo + hi) / 2;
            double hz, pp;
            ring(s, seconds, hz, pp);
            (pp > 0.2 ? hi : lo) = s.res;
        }
        std::printf("%.5f\n", (lo + hi) / 2);
    }
    else if (!std::strcmp(cmd, "voice-osc") && argc > 4)
    {
        Setup s;
        s.mode = std::atoi(argv[2]);
        s.vcff = std::atof(argv[3]);
        s.res = std::atof(argv[4]);
        double hz, pp;
        ring(s, argc > 5 ? std::atof(argv[5]) : 0.3, hz, pp);
        std::printf("%.4f %.5f\n", hz, pp);
    }
    else if (!std::strcmp(cmd, "voice-vco"))
    {
        Setup s;
        s.vcof1 = std::atof(argv[2]);
        s.noise = false;
        Voice v;
        prime(v, s);
        Drops d;
        const long n = long(kRate * 0.5);
        for (long i = 0; i < n; ++i)
        {
            v.tick(0);
            d.see(i, v.vco1().sawDropAt());
        }
        std::printf("%.6f\n", d.hz());
    }
    else if (!std::strcmp(cmd, "bus") && argc > 6)
    {
        Setup s;
        s.waves = uint8_t(std::atoi(argv[2]));
        s.pw1 = std::atof(argv[3]);
        s.pw2 = std::atof(argv[4]);
        s.vcof1 = std::atof(argv[5]);
        s.vcof2 = std::atof(argv[6]);
        s.noise = false;
        Voice v;
        prime(v, s);
        const long settle = long(1.5 * kRate), n = settle + long(0.5 * kRate);
        std::vector<double> a, b;
        Drops d1, d2;
        for (long i = 0; i < n; ++i)
        {
            v.tick(0);
            if (i >= settle)
            {
                d1.see(i - settle, v.vco1().sawDropAt());
                d2.see(i - settle, v.vco2().sawDropAt());
                a.push_back(v.vcfIn(6));
                b.push_back(v.vcfIn(7));
            }
        }
        // RMS and p-p over whole periods (first to last drop).
        auto measure = [](const std::vector<double>& y, const Drops& d, double& rms, double& pp) {
            const size_t i0 = size_t(std::ceil(d.t.front())), i1 = size_t(std::ceil(d.t.back()));
            double mean = 0, s2 = 0;
            for (size_t i = i0; i < i1; ++i)
                mean += y[i];
            mean /= double(i1 - i0);
            for (size_t i = i0; i < i1; ++i)
                s2 += (y[i] - mean) * (y[i] - mean);
            rms = std::sqrt(s2 / double(i1 - i0));
            const auto mm = std::minmax_element(y.begin() + long(i0), y.begin() + long(i1));
            pp = *mm.second - *mm.first;
        };
        double rmsA, ppA, rmsB, ppB;
        measure(a, d1, rmsA, ppA);
        measure(b, d2, rmsB, ppB);
        std::printf("%.6f %.6f %.7f %.7f %.7f %.7f\n", d1.hz(), d2.hz(), rmsA, ppA, rmsB, ppB);
    }
    else if (!std::strcmp(cmd, "bus-noise") && argc > 3)
    {
        Setup s;
        const double amp = std::atof(argv[2]), hz = std::round(std::atof(argv[3]));
        Voice v;
        prime(v, s);
        const long settle = long(1.5 * kRate), window = long(kRate * std::round(0.2 * hz) / hz);
        std::vector<double> b;
        for (long i = 0; i < settle + window; ++i)
        {
            v.tick(amp * std::sin(2 * kPi * hz * double(i) / kRate));
            if (i >= settle)
                b.push_back(v.vcfIn(7));
        }
        double mean = 0, s2 = 0;
        for (double x : b)
            mean += x;
        mean /= double(b.size());
        for (double x : b)
            s2 += (x - mean) * (x - mean);
        const auto mm = std::minmax_element(b.begin(), b.end());
        std::printf("%.7f %.7f\n", std::sqrt(s2 / double(b.size())), *mm.second - *mm.first);
    }
    else
        return 1;
    return 0;
}
