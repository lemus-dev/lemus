// One oscillator of the CEM3374 (Curtis data sheet, 1984; research.md 3.1) as the Xpander wires it:
// a triangle core whose timing capacitor (1000 pF) is charged and discharged by the exponential
// converter's current between 0 and V_TH (+4REF), with the HF network in series with it (1.2K
// parallel 0.01 uF) and the comparator's delay; the triangle at pin 13's buffer, the tri-to-saw
// converter (saw = tri rising, 2 V_TH - tri falling), the external pulse comparator (high while
// PW > saw / 2) and the standard hard sync (oscillator A: both waveforms reset to zero).
// Everything is computed at the event times inside each sample and band-limited (blep.h).
#pragma once

#include <limits>
#include "blep.h"
#include "solver_stats.h"

namespace xpander {

class Cem3374
{
public:
    struct Params
    {
        double cT = 1000e-12;     // timing capacitor (CX22, CX28 poly)
        double vTh = 3.99;        // V_TH: +4REF (U815C, 3.99 V)
        double rHf = 1200;        // RX20/RX25
        double cHf = 0.01e-6;     // CX20/CX26
        double iRef = 3.99 / 806e3; // IREF: +4REF through RX23/RX29 806K into the virtual ground
        double voltsPerOctave = 0.0179; // exponential scale (data sheet: 17.0-19.0, typ 17.9 mV)
        // The chip's high-end flatness has two sources (CEM3340/3345 data sheet, "Trimming the scale
        // error": the exponential transistor's bulk emitter resistance above about 100 uA and the
        // comparator's switching delay above about 5 kHz); the CEM3374's untrimmed HF scale error
        // lumps them: 3 % typ, 7 % max at 10 kHz, 100 uA [D]. The board's fixed HF network (RX25
        // 1.2K // CX26 0.01 uF) takes about 4.6 % off at 10 kHz. With the delay alone (2 %) the VCO
        // stayed +1.3, +3.2 and +8.1 cents sharp at C4, E5 and G#6 before tuning, and voice ROM 1.4
        // continues its correction below C4 along the parabola through those three points: the
        // bottom octave came out 6 cents flat (-9 at C1).
        //
        // The comparator's delay before the current reverses. The triangle runs on past V_TH (or 0)
        // for the delay and comes back, so each reversal adds twice the delay to the period (all of
        // the typical 3 % would be 0.75 us). At the top the saw (folded about V_TH) dips, and where
        // PW = V_TH / 2 (50 %) the measured duty jumps: voice ROM 1.4's PW tune aims exactly there
        // and must land in a 4-count window. 0.5 us passes; at 1.4 us the duty jumps about 10 counts
        // between two neighbouring tries and the tune fails (1.75, 2.0 and 2.4 us fail; 0.75-1.3
        // and 1.5 pass only where the window happens to fall) [F: the firmware's PW tune].
        double delay = 0.5e-6;
        // The exponential converter's bulk resistance: the output current drops I x rBulk, so
        // I = I0 2^(-I rBulk / V_oct), flat in proportion to the current; the reference transistor's
        // drop at IREF (5 uA) only offsets the pitch, which the tune takes out. 10 ohm [F: the four
        // freesound patches' bottom octave (-1.2 to +1.0 cents from their medians) and stretch (-0.5
        // to +0.3 cents/octave), the model then -0.1 and +0.0 (tools/compare/notes.py; 9-11.5 ohm
        // all fall inside, the tune itself moves the bottom octave by about 0.7 cents)]: with the
        // delay 5.7 % at 10 kHz, 100 uA (data sheet: 3 % typ, 7 % max).
        double rBulk = 10;
        // The pulse comparator sees the saw shifted by a base-emitter voltage of the core's current: per
        // octave down the pulse widens by pwVbe x V_T ln 2 / V_TH (0.26 %) [F: a Matrix-12's pulse run,
        // Yxxwb4d8GHA 6:00, duty 50.00/50.27/50.55/50.78 % at C4/C3/C2/C1; its triangle stays symmetric].
        double pwVbe = 1.15;
        double iMax = 600e-6;     // charge current limit (data sheet: 350-550 uA maximum)
        // The core's up and down currents differ by this fraction (+ charges faster): the triangle leans
        // and gains even harmonics, h2 about pi/4 x asym under h1. 0.3 % [F: a Matrix-12's triangle
        // takes, Yxxwb4d8GHA: h2/h4/h6 -53/-59/-64 dB at C3, the model -53/-60/-64; round 21, chosen by
        // ear over 0]. The same on every VCO (real units spread; one unit is all that's measured).
        double asym = 0.003;
    };

    void prepare(double rate)
    {
        m_rate = rate;
        m_kVc = std::numeric_limits<double>::quiet_NaN();
        m_eDt = std::numeric_limits<double>::quiet_NaN();
        m_stats = {};
    }
    void reset();
    // One sample. vcIn: the exponential control voltage at the chip pin; iExtra: current added to
    // IREF (linear FM, amps); pwSaw: the pulse comparator's threshold in saw volts (2 x PW CV);
    // syncAt: where in this interval (0..1] a hard reset lands, 0 for none.
    void tick(double vcIn, double iExtra, double pwSaw, double syncAt);

    // Band-limited outputs, two samples late: pin 13's triangle (0..V_TH), the saw (0..2 V_TH) and
    // the pulse comparator's state (0 or 1).
    double tri() const { return m_triOut; }
    double saw() const { return m_sawOut; }
    double pulse() const { return m_pulseOut; }
    // The triangle now (not delayed): what the MDAC sees for FM.
    double triNow() const { return m_x + m_vn; }
    // Where the saw dropped in the last interval (0: it didn't): the edge that syncs the other.
    double sawDropAt() const { return m_dropAt; }
    double hz() const { return m_hz; }
    // The solves since prepare() (solver_stats.h).
    const SolverStats& stats() const { return m_stats; }

    Params params;

private:
    double pin() const { return m_x + m_vn; }
    double sawOf(double p, int dir) const { return dir > 0 ? p : 2 * params.vTh - p; }
    void advance(double dt, double i);

    double m_rate = 192000;
    // Kept from the last sample while their inputs are unchanged (the same arithmetic).
    double m_kVc = std::numeric_limits<double>::quiet_NaN(), m_kIx = 0, m_kI = 0, m_kPwShift = 0;
    double m_eDt = std::numeric_limits<double>::quiet_NaN(), m_eVal = 0;
    double m_x = 0;    // the timing capacitor's voltage
    double m_vn = 0;   // the HF network's voltage
    int m_dir = 1;     // charging up (+1) or down (-1)
    double m_flipIn = -1; // seconds until a reversal the comparator has decided on
    bool m_pulseHigh = true;
    double m_triOut = 0, m_sawOut = 0, m_pulseOut = 0, m_dropAt = 0, m_hz = 0;
    Blep m_triBlep, m_sawBlep, m_pulseBlep;
    SolverStats m_stats;
};

} // namespace xpander
