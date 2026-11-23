#include "cem3372.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace xpander {

namespace {

constexpr double kPi = 3.14159265358979323846; // kPi isn't standard (MSVC lacks it)

// Trapezoidal gain for a pole at w (rad/s): prewarped (exact at the pole) while the pole is in the
// band; above, the plain bilinear gain, which keeps the response right at low frequencies, joined
// smoothly (the cell switched to 100 pF sits hundreds of times above the others, past Nyquist).
double warp(double w, double T)
{
    const double x = w * T / 2;
    if (x < 1.0)
        return std::tan(x);
    return x + (std::tan(1.0) - 1.0) * std::exp(1.0 - x);
}

} // namespace

// Cem3372::step inlined at both its calls: as a call it cost 0.8 % of chord6-48k's instructions.
#if defined(_MSC_VER)
#define XPANDER_STEP_INLINE __forceinline
#else
#define XPANDER_STEP_INLINE [[gnu::always_inline]] inline
#endif

void Cem3372::prepare(double rate)
{
    m_rate = rate;
    m_wFreq = std::numeric_limits<double>::quiet_NaN(); // the kept cell gains depend on the rate
    m_stats = {};
    reset();
}

void Cem3372::reset()
{
    m_s = {};
    m_y = {};
    m_m = {1, 1, 1, 1};
    m_hpS = 0;
    m_out = 0;
    m_coupled = 0;
}

void Cem3372::filter(double inA, double inB, double levelA, double levelB, double freq, double res)
{
    const auto& p = params;
    const double T = 1.0 / m_rate;
    const double v2t = 2 * p.vT;

    // The input VCAs: differential pairs, audio-taper gain (kept while the level is unchanged).
    auto taperOf = [&](double v) {
        const double x = std::clamp(v / 5.0, 0.0, 1.0);
        return (std::exp(p.taper * x) - 1) / (std::exp(p.taper) - 1);
    };
    if (!(levelA == m_tapLevel[0]))
    {
        m_tapLevel[0] = levelA;
        m_tapValue[0] = taperOf(levelA);
    }
    if (!(levelB == m_tapLevel[1]))
    {
        m_tapLevel[1] = levelB;
        m_tapValue[1] = taperOf(levelB);
    }
    // The chip's own noise at the cells' input, white: VCF OUT gets noiseOut in 20 kHz (outGain x
    // the cells' scale), a uniform sample with that density at this rate.
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    const double white = (double(m_rng >> 8) * (1.0 / 16777216.0) - 0.5) * 3.4641016151377546;
    const double u = p.mixScale * p.mixGain *
                         (m_tapValue[0] * v2t * std::tanh(inA / v2t) + m_tapValue[1] * v2t * std::tanh(inB / v2t)) +
                     white * p.noiseOut / p.outGain * std::sqrt(m_rate / 2 / 20e3);

    // Resonance loop gain k: pin 11 (VCF OUT x divider, AC-coupled) x 0-190 back into the input at
    // the cells' scale (1/100), so k = divider x gain x outGain / 100; oscillation near k = 4. G: the
    // resonance VCA's gain alone.
    const double k = m_resDiv * p.resGain * std::clamp(res, 0.0, 5.0) / 5.0 * p.outGain / 100;
    const double G = p.resGain * std::clamp(res, 0.0, 5.0) / 5.0;

    // The cells: one pole each at w = (cRef / C) w0 2^(freq / scale). A resistance in series with
    // cell 1's capacitor adds R x the cell's current to its output: in the trapezoidal step the
    // capacitor's gain g and the resistor's h = R gm = R C w (the same for either capacitor).
    // Kept while FREQ and the capacitors are unchanged.
    if (!(freq == m_wFreq) || m_c != m_wc)
    {
        m_wFreq = freq;
        m_wc = m_c;
        m_w0 = 2 * kPi * p.hzAt0 * std::exp2(std::clamp(freq, -0.2, 0.16) / p.voltsPerOctave);
        m_h0 = m_r1 * m_w0 * p.cRef;
        for (int i = 0; i < 4; ++i)
        {
            // Cell 4 runs at 1/100 of the current into 1/100 of the capacitance.
            const double c = i == 3 ? m_c[3] * 100 : m_c[size_t(i)];
            m_wv[size_t(i)] = warp(m_w0 * p.cRef / c, T);
        }
    }
    // One step for the sample. Over the controls' extremes (tools/corners: every mode, FREQ, RES and
    // the VCF FM at their tops, the CVs over the DAC's whole range, at 176.4 and 192 kHz) it
    // converges within 7 iterations. Capped at 4 and taken as they stood, 1 % of those samples
    // stayed unconverged, which no noise or level check would have shown: only the count does. One
    // that doesn't converge in 16 is done again from where it started in 2, then 4, 8, 16 shorter
    // steps with the inputs held, and counted (stats()).
    ++m_stats.solves;
    if (step(u, k, G, m_wv.data(), std::tan(kPi * m_resHpHz * T), false) > 0)
        return;
    ++m_stats.retried;
    const std::array<double, 4> s0 = m_s, m0 = m_m;
    const double hp0 = m_hpS;
    int done = 0;
    for (int parts = 2; parts <= kMaxParts && done <= 0; parts *= 2)
    {
        m_s = s0;
        m_m = m0;
        m_hpS = hp0;
        const double t = T / parts;
        double wv[4];
        for (int i = 0; i < 4; ++i)
        {
            const double c = i == 3 ? m_c[3] * 100 : m_c[size_t(i)];
            wv[i] = warp(m_w0 * p.cRef / c, t);
        }
        const double gh = std::tan(kPi * m_resHpHz * t);
        // The last split takes its steps as they come, converged or not, and goes on to the end.
        const bool last = parts == kMaxParts;
        done = 1;
        for (int n = 0; n < parts && (done > 0 || (last && done == 0)); ++n)
        {
            const int r = step(u, k, G, wv, gh, last);
            done = std::min(done, r);
        }
    }
    if (done > 0)
        return;
    ++m_stats.failures;
    if (done < 0)
        reset(); // NaN guard: a sample that can't be solved restarts the filter
}

XPANDER_STEP_INLINE int Cem3372::step(double u, double k, double G, const double* wv, double gh, bool accept)
{
    const auto& p = params;
    const double v2t = 2 * p.vT;
    const double h[4] = {m_h0, 0, 0, 0};
    double a[4], b[4];
    for (int i = 0; i < 4; ++i)
    {
        const double g = (wv[i] + h[i]) * m_m[size_t(i)];
        a[i] = g / (1 + g);
        b[i] = m_s[size_t(i)] / (1 + g);
    }
    const double A = a[0] * a[1] * a[2] * a[3];
    const double B = ((b[0] * a[1] + b[1]) * a[2] + b[2]) * a[3] + b[3];

    // The res input's coupling: pin 11 sees VCF OUT high-passed. hp = c x + d for x = outGain y4.
    const double hc = 1 / (1 + gh), hd = -m_hpS / (1 + gh);
    const double og = p.outGain;
    // u1 = (1 + k) u - k (hc y4 + hd / outGain); y4 = A u1 + B.
    const double y4 = (A * ((1 + k) * u - k * hd / og) + B) / (1 + A * k * hc);
    const double u1 = (1 + k) * u - k * (hc * y4 + hd / og);

    // The secant solve above is the first guess; Newton then solves the cells' tanh, the resonance
    // pair's tanh and pin 17's swing limit in the loop exactly for this sample (trapezoidal), so the
    // result doesn't depend on the last sample's gains (self-oscillation pitch and level against
    // ngspice, tools/spice/suite.py 3).
    double g[4], y[4];
    {
        double in = u1;
        for (int i = 0; i < 4; ++i)
        {
            g[i] = wv[i];
            y[i] = a[i] * in + b[i];
            in = y[i];
        }
    }
    const double L = p.outSwing;
    const double Lr = p.resPair;
    // Pin 11 = divider x VCF OUT (pin 17, AC-coupled). The resonance VCA's pair sees pin 11 against
    // the compensation node, the filter's input at pin 11's scale (outGain x divider x u: zero
    // difference in the passband, so the input isn't distorted there); its current, at most G x the
    // pair's 2 V_T, goes into the filter's input at 1/100. Small-signal: (1 + k) u - k pin11 / 100.
    // [C] The compensation node at the board's divider (k = divider x G), as the passband-constant
    // (1 + k) was before; the chip's own node is sized for its 22K/1K, the Xpander has 20K/1.2K.
    const double vComp = og * m_resDiv * u;
    auto pin11 = [&](double y4v) { return m_resDiv * (hc * L * std::tanh(og * y4v / L) + hd); };
    auto x1Of = [&](double y4v) { return u - G * Lr * std::tanh((pin11(y4v) - vComp) / Lr) / 100; };
    // Newton on the cells' tanh arguments z_i = (x_i - y_i) / 2V_T, with y_i = s_i + g_i 2V_T tanh z_i
    // (x_1 the loop's input, x_i = y_(i-1)): in these each cell's residual stays linear where its
    // tanh saturates, so a stiff cell (the 100 pF one, hundreds of times above the band) doesn't
    // throw the iteration from one rail to the other as it did on the y_i. Steps are scaled to move
    // no argument by more than 2 at once.
    double z[4];
    {
        double x = x1Of(y[3]);
        for (int i = 0; i < 4; ++i)
        {
            z[i] = (x - y[i]) / v2t;
            x = y[i];
        }
    }
    bool converged = false;
    int it = 0;
    double t[4], step[4] = {}; // the last iteration's tanh and the step it took
    double toLast = 0, y4Last = 0; // and pin 17's tanh at its y4
    for (; it < kIterations; ++it)
    {
        double q[4], r[4], D[4], E[4];
        for (int i = 0; i < 4; ++i)
        {
            t[i] = std::tanh(z[i]);
            y[i] = m_s[size_t(i)] + (g[i] + h[i]) * v2t * t[i];
            q[i] = (g[i] + h[i]) * v2t * (1 - t[i] * t[i]); // dy_i / dz_i
        }
        // x1Of(y[3]), with its two tanh kept for the Jacobian corner below.
        const double to = std::tanh(og * y[3] / L);
        toLast = to, y4Last = y[3];
        const double p11 = m_resDiv * (hc * L * to + hd);
        const double tr = std::tanh((p11 - vComp) / Lr);
        r[0] = v2t * z[0] + y[0] - (u - G * Lr * tr / 100);
        D[0] = v2t + q[0];
        for (int i = 1; i < 4; ++i)
        {
            r[i] = v2t * z[i] + y[i] - y[i - 1];
            D[i] = v2t + q[i];
            E[i] = -q[i - 1];
        }
        // r_1 depends on z_4 through the loop: d x1 / d y4 = -k hc sech^2(og y4 / L) sech^2(pair).
        const double C = k * hc * (1 - to * to) * (1 - tr * tr) * q[3];
        // Lower bidiagonal plus that corner: each delta as alpha + beta * delta4.
        const double al1 = -r[0] / D[0], be1 = -C / D[0];
        const double al2 = (-r[1] - E[1] * al1) / D[1], be2 = -E[1] * be1 / D[1];
        const double al3 = (-r[2] - E[2] * al2) / D[2], be3 = -E[2] * be2 / D[2];
        const double d4 = (-r[3] - E[3] * al3) / (D[3] + E[3] * be3);
        double d[4] = {al1 + be1 * d4, al2 + be2 * d4, al3 + be3 * d4, d4};
        double big = 0, moved = 0;
        for (int i = 0; i < 4; ++i)
            big = std::max(big, std::fabs(d[i]));
        const double scale = big > 2 ? 2 / big : 1;
        for (int i = 0; i < 4; ++i)
        {
            step[i] = scale * d[i];
            z[i] += step[i];
            moved = std::max(moved, std::fabs(d[i]) * D[i]); // volts: of y_i and of 2V_T z_i
        }
        if (moved < 1e-12)
        {
            converged = true;
            ++it;
            break;
        }
    }
    m_stats.iterations += it;
    m_stats.worst = std::max(m_stats.worst, it);
    if (!std::isfinite(z[0] + z[1] + z[2] + z[3]))
        return -1; // the state stays where it was
    if (!converged && !accept)
        return 0;
    for (int i = 0; i < 4; ++i)
    {
        // Converged, the last step is under 1e-12 V / D (about 2e-11 in z): tanh moves by its slope
        // times the step, the next term (step^2 tanh'' / 2, under 1e-21) below a double's resolution
        // [CPU round, 2026-11-23: saves four tanh a sample]. Not converged: computed again.
        const double tz = converged ? t[i] + (1 - t[i] * t[i]) * step[i] : std::tanh(z[i]);
        m_y[size_t(i)] = m_s[size_t(i)] + (g[i] + h[i]) * v2t * tz;
        m_s[size_t(i)] += 2 * g[i] * v2t * tz; // the capacitor's own voltage, trapezoidal
        m_m[size_t(i)] = std::fabs(z[i]) < 1e-6 ? 1.0 - z[i] * z[i] / 3 : tz / z[i]; // secant(z), its tanh reused
    }
    // Pin 17 (the swing limit), and the coupling capacitor's state: its low-pass of pin 17.
    // Converged, y4 moved by the last step's slope alone: the same first-order update as the cells.
    m_out = converged ? L * (toLast + (1 - toLast * toLast) * og * (m_y[3] - y4Last) / L) : L * std::tanh(og * m_y[3] / L);
    const double lp = (gh * m_out + m_hpS) / (1 + gh);
    m_hpS = 2 * lp - m_hpS;
    m_coupled = m_out - lp;
    return converged ? 1 : 0;
}

double Cem3372::vca(double iIn, double cv) const
{
    const auto& p = params;
    const double g = std::max(p.vcaFloor * p.vcaMax, p.vcaMax * (cv - p.vcaOff) / (p.vcaFull - p.vcaOff));
    // The output current flows against the input's: only this polarity at U108 lets voice ROM
    // 1.4's PW tune pass (it times OSC's high half), so the sense is taken from the firmware [F].
    return -g * p.vcaLimit * std::tanh(iIn / p.vcaLimit);
}

} // namespace xpander
