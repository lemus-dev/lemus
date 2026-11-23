#include "cem3374.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace xpander {

namespace {
constexpr double kNever = std::numeric_limits<double>::infinity();

// I = I0 2^(-I R / V_oct): with k = R ln 2 / V_oct and x = k I0, I = I0 W(x) / x (Lambert W).
// Below x = 0.05 (about 130 uA at 10 ohm) the series is exact to 4e-6 (0.006 cents); above it,
// four Newton steps on y e^y = x from log1p(x). The control currents reach x = 0.37 (tools/corners),
// where the last step moves y by 1e-12; one whose last step still moved it by 1e-6 or more (the
// error after a step is about its square) is counted as unconverged.
double bulkDrop(double i0, double k, SolverStats& stats)
{
    const double x = i0 * k;
    if (x <= 0)
        return i0;
    if (x < 0.05)
        return i0 * (1 - x * (1 - x * (1.5 - x * (8.0 / 3.0 - x * (125.0 / 24.0)))));
    double y = std::log1p(x), d = 0;
    for (int n = 0; n < 4; ++n)
    {
        const double e = std::exp(y);
        d = (y * e - x) / (e * (1 + y));
        y -= d;
    }
    ++stats.solves;
    stats.iterations += 4;
    stats.worst = std::max(stats.worst, 4);
    if (!(std::fabs(d) < 1e-6))
        ++stats.failures;
    return y / k;
}
} // namespace

void Cem3374::reset()
{
    m_x = 0;
    m_vn = 0;
    m_dir = 1;
    m_flipIn = -1;
    m_pulseHigh = false;
    m_triBlep.reset(0);
    m_sawBlep.reset(0);
    m_pulseBlep.reset(0);
    m_triOut = m_sawOut = m_pulseOut = m_dropAt = m_hz = 0;
}

void Cem3374::advance(double dt, double i)
{
    if (dt <= 0)
        return;
    const double tau = params.rHf * params.cHf;
    const double a = m_dir * i * params.rHf;
    m_x += m_dir * i / params.cT * dt;
    if (!(dt == m_eDt))
    {
        m_eDt = dt;
        m_eVal = std::exp(-dt / tau);
    }
    m_vn = a + (m_vn - a) * m_eVal;
}

void Cem3374::tick(double vcIn, double iExtra, double pwSaw, double syncAt)
{
    const double T = 1.0 / m_rate;
    const double vth = params.vTh;
    const double tau = params.rHf * params.cHf;
    if (!(vcIn == m_kVc && iExtra == m_kIx))
    {
        m_kVc = vcIn;
        m_kIx = iExtra;
        const double i0 = (params.iRef + iExtra) * std::exp2(vcIn / params.voltsPerOctave);
        m_kI = std::clamp(bulkDrop(i0, params.rBulk * 0.69314718055994531 / params.voltsPerOctave, m_stats), 0.0, params.iMax);
        // The comparator's shift, kept with the current (its constant part is the PW tune's).
        m_kPwShift = m_kI > 0 ? params.pwVbe * 0.025852 * std::log(m_kI / params.iRef) : 0.0;
    }
    const double iUp = m_kI * (1 + params.asym), iDown = m_kI * (1 - params.asym);
    auto cur = [&](int dir) { return dir > 0 ? iUp : iDown; };
    double i = cur(m_dir);
    pwSaw -= m_kPwShift; // lower currents: the saw reads lower, the pulse stays high longer
    m_hz = m_kI / (2 * params.cT * vth);
    m_dropAt = 0;

    auto slope = [&](int dir) { return dir * cur(dir) / params.cT + (dir * cur(dir) - m_vn / params.rHf) / params.cHf; };
    // Seconds until dir * (pin - target) reaches 0 (it rises monotonically), within `horizon`.
    auto timeTo = [&](double target, double horizon) {
        const double a = m_dir * i * params.rHf;
        auto f = [&](double t) {
            const double p = m_x + m_dir * i / params.cT * t + a + (m_vn - a) * std::exp(-t / tau);
            return m_dir * (p - target);
        };
        if (f(0) >= 0)
            return 0.0;
        if (f(horizon) < 0)
            return kNever;
        double lo = 0, hi = horizon;
        double t = std::clamp(-f(0) / std::max(1e-30, m_dir * slope(m_dir)), lo, hi);
        ++m_stats.solves;
        bool converged = false;
        int k = 0;
        for (; k < 8; ++k)
        {
            const double v = f(t);
            if (v < 0)
                lo = t;
            else
                hi = t;
            const double d = m_dir * (m_dir * i / params.cT - (m_vn - a) / tau * std::exp(-t / tau));
            double tn = d > 0 ? t - v / d : 0.5 * (lo + hi);
            if (!(tn > lo && tn < hi))
                tn = 0.5 * (lo + hi);
            if (std::fabs(tn - t) < 1e-13)
            {
                t = tn;
                converged = true;
                ++k;
                break;
            }
            t = tn;
        }
        if (!converged)
        {
            // The loop above takes a Newton point only strictly inside the bracket. When its first
            // guess is the root itself (the pin rises in a straight line once the HF network has
            // settled, as it has at audio pitches), the next point lands on the bracket's end, or a
            // rounding error past it, and is refused, and the loop halves towards the root 8 times
            // and stops short of it. Taken as it stood (before 2026-11-16) that placed 40-60 % of a
            // held note's events up to 0.14 us early or late (mean 16 ns, 0.3 % of a sample): pitch
            // within 0.07 cents below 4 kHz, a jitter floor 120 dB under the note, and with both VCOs
            // ultrasonic, broadband noise. From the bracket's upper end, Newton steps clamped into the
            // bracket (a point a hair outside it is its end), halving only when a step leaves it, until
            // a step or the bracket is under 1e-13 s: one or two more iterations.
            ++m_stats.retried;
            t = hi;
            for (int r = 0; r < 64 && !converged; ++r)
            {
                ++k;
                const double v = f(t);
                if (v < 0)
                    lo = t;
                else
                    hi = t;
                const double d = m_dir * (m_dir * i / params.cT - (m_vn - a) / tau * std::exp(-t / tau));
                double tn = 0.5 * (lo + hi);
                if (d > 0)
                {
                    const double nt = t - v / d;
                    if (nt > lo - 1e-13 && nt < hi + 1e-13)
                        tn = std::clamp(nt, lo, hi);
                }
                converged = std::fabs(tn - t) < 1e-13 || hi - lo < 1e-13;
                t = tn;
            }
        }
        m_stats.iterations += k;
        m_stats.worst = std::max(m_stats.worst, k);
        if (!converged)
            ++m_stats.failures;
        return t;
    };

    // The pulse follows the comparator at once if the width moved across the saw.
    {
        const bool want = pwSaw > sawOf(pin(), m_dir);
        if (want != m_pulseHigh)
        {
            m_pulseHigh = want;
            m_pulseBlep.step(want ? 1 : -1, 1e-9);
        }
    }

    double t = 0;
    bool syncPending = syncAt > 0;
    bool done = false;
    for (int guard = 0; guard < 24; ++guard)
    {
        const double left = T - t;
        const double tFlip = m_flipIn >= 0 ? m_flipIn : kNever;
        const double tCross = m_flipIn < 0 && i > 0 ? timeTo(m_dir > 0 ? vth : 0.0, left) : kNever;
        double tPulse = kNever;
        if (m_pulseHigh && i > 0)
        {
            // The saw rises through the threshold: on the way up while pin < V_TH, then on the way
            // down (saw = 2 V_TH - pin).
            const double target = m_dir > 0 ? pwSaw : 2 * vth - pwSaw;
            if ((m_dir > 0 && pwSaw <= vth) || (m_dir < 0 && pwSaw > vth))
                tPulse = timeTo(target, left);
        }
        const double tSync = syncPending ? std::max(0.0, syncAt * T - t) : kNever;
        const double tn = std::min({tFlip, tCross, tPulse, tSync});
        if (tn > left)
        {
            advance(left, i);
            if (m_flipIn >= 0)
                m_flipIn -= left;
            done = true;
            break;
        }
        advance(tn, i);
        if (m_flipIn >= 0)
            m_flipIn = std::max(0.0, m_flipIn - tn);
        t += tn;
        const double frac = std::max(t / T, 1e-9);

        if (tn == tSync && syncPending)
        {
            // Standard sync: triangle and saw to zero at once, charging up again.
            syncPending = false;
            const double p = pin();
            const double s0 = slope(m_dir) * T, saw0 = sawOf(p, m_dir), sawSlope0 = (m_dir > 0 ? 1 : -1) * s0;
            m_x = 0;
            m_vn = 0;
            m_dir = 1;
            i = cur(m_dir);
            m_flipIn = -1;
            const double s1 = slope(1) * T;
            m_triBlep.step(-p, frac);
            m_triBlep.corner(s1 - s0, frac);
            m_sawBlep.step(-saw0, frac);
            m_sawBlep.corner(s1 - sawSlope0, frac);
            const bool want = pwSaw > 0;
            if (want != m_pulseHigh)
            {
                m_pulseHigh = want;
                m_pulseBlep.step(want ? 1 : -1, frac);
            }
            continue;
        }
        if (tn == tFlip)
        {
            // The comparator's delayed decision: the current reverses.
            const double p = pin();
            const int before = m_dir;
            const double s0 = slope(before) * T;
            const double saw0 = sawOf(p, before), sawSlope0 = (before > 0 ? 1 : -1) * s0;
            m_dir = -m_dir;
            i = cur(m_dir);
            m_flipIn = -1;
            const double s1 = slope(m_dir) * T;
            const double saw1 = sawOf(p, m_dir), sawSlope1 = (m_dir > 0 ? 1 : -1) * s1;
            m_triBlep.corner(s1 - s0, frac);
            m_sawBlep.step(saw1 - saw0, frac);
            m_sawBlep.corner(sawSlope1 - sawSlope0, frac);
            if (m_dir > 0)
            {
                // The bottom: the saw drops; the pulse goes high again.
                m_dropAt = frac;
                const bool want = pwSaw > saw1;
                if (want != m_pulseHigh)
                {
                    m_pulseHigh = want;
                    m_pulseBlep.step(want ? 1 : -1, frac);
                }
            }
            continue;
        }
        if (tn == tCross)
        {
            m_flipIn = params.delay;
            continue;
        }
        if (tn == tPulse)
        {
            m_pulseHigh = false;
            m_pulseBlep.step(-1, frac);
            continue;
        }
    }

    // More events in a sample than the loop takes (at most 5 over the CVs' ranges): the rest of the
    // sample is lost.
    if (!done)
        ++m_stats.failures;
    const double p = pin();
    m_triOut = m_triBlep.push(p);
    m_sawOut = m_sawBlep.push(sawOf(p, m_dir));
    m_pulseOut = m_pulseBlep.push(m_pulseHigh ? 1.0 : 0.0);
}

} // namespace xpander
