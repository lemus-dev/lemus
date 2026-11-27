// The CEM3372 (Curtis data sheet, 1984; R. McClellan's notes on its VCF; research.md 3.3-3.4):
// - two input VCAs, differential pairs (75 mV p-p in for 5 % THD) with audio-taper gain 0-35, their
//   sum scaled by 1/100 into the filter;
// - the 4-pole low-pass: four diode-loaded transconductance cells, each a one-pole lag with a tanh
//   on its input difference, cut-off 1 kHz at 0 V on pin 15 with 0.03 uF cells, 19 mV per octave;
//   cell 4 at 1/100 of the current and capacitance with an output buffer (VCF OUT), x100 in the
//   data sheet, taken at x88 (outGain);
// - resonance: a VCA of the input VCAs' kind (the same "delta A" in the block diagram): a
//   differential pair between pin 11 and the internal node that carries the filter's input (so the
//   passband gain stays constant, data sheet), its output current 0-190 x for 0-5 V on pin 10
//   into the filter's input. The pair's tanh (2 V_T) limits the loop: VCF OUT oscillates at
//   5.5 V p-p at RES 5 V in the data sheet's circuit ("Nominal Output Swing with Resonance
//   4.4-5.0-5.6 V p-p"), with onset at 2.42 V (2.2-2.8-3.4 V); on the Xpander's 20K/1.2K, 5.5 V
//   p-p at the 4.67 V its tunes use, onset 1.87 V. Without that limit only pin 17's swing held
//   the loop (8.6 V p-p) and the hard-clipped oscillation locked to the sample rate;
// - the final VCA: current in, current out, linear in its control voltage, 0.93 at 5 V, shut off
//   at +85 mV, -120 dB feed-through; its output current flows against the input (see vca()).
// Each sample is solved exactly (trapezoidal integration, Newton on the cells' tanh, the resonance
// pair's tanh and pin 17's swing limit around the loop), from a zero-delay first guess with the
// cells' tanh as secant gains from the last sample.
#pragma once

#include "solver_stats.h"

#include <array>
#include <cstdint>
#include <limits>

namespace xpander {

class Cem3372
{
public:
    struct Params
    {
        double vT = 0.02585;          // kT/q
        double hzAt0 = 1000.0;        // initial frequency with 0.03 uF cells (data sheet typ 1 kHz)
        double cRef = 0.03e-6;
        double voltsPerOctave = 0.019; // data sheet typ 19 mV/octave
        // Input VCA gain at 5 V (data sheet 28-42, typical 35): 28.6 keeps the gain at VOL 63 (4.67 V)
        // where the earlier guessed taper (gain 0.1 at 2.5 V) had it, so patches at full volume sound
        // as before [C: no recording gives the absolute level].
        double mixGain = 28.58;
        double mixScale = 0.01;       // into the filter
        // The taper, (e^(k x) - 1) / (e^k - 1) with x = V / 5: gain 0.41 at 2.5 V. [F: factory patch
        // VIBES, VCO2 a triangle two octaves over VCO1's (where a triangle has no harmonic) at 2.69 V
        // against VCO1's 4.67 V: 6 dB under it on two units (qfg9Jni8nN8 dry, NJrIT7_8N0Q), within
        // 0.5 dB dry and 1.5 dB across chords; the data sheet only says "audio taper".] Below 2.7 V
        // the shape is the same family, not measured.
        double taper = 0.7;
        // VCF OUT (pin 17) on cell 4's voltage. [D: x100, no tolerance given; F: 88, round 15: the
        // phaser modes (3 POLE PHASE, 3 PHASE+1 LOW) are the dry signal minus a 3-pole all-pass, with
        // VCF OUT weighted about 4 against the board's x101 cell buffers, so their low end hangs on
        // this gain. A Matrix-12's four phaser clips (NuBRcBlNAH8) fit at 0.88 of the data sheet's:
        // fundamentals within -2.5 to +0.7 dB, from 3-13 dB under at x100; its 4P BP fits a little
        // better too, and no other mode moves. One unit.] The compensation node follows it, so the
        // passband stays constant.
        double outGain = 88.0;
        // Resonance VCA gain at 5 V (see model.md) [C: linear]: the loop gain (divider x this x
        // outGain / 100) as fitted in rounds 5 and 10 at x100 (115 there).
        double resGain = 115.0 * 100.0 / 88.0;
        // The resonance VCA's input pair: tanh(v / resPair) on pin 11 against the compensation node.
        // [D] 2 V_T, the input VCAs' pair (75 mV p-p for 5 % THD) by the block diagram's symbol;
        // checked against the data sheet's swing and onset with resonance (top of this file).
        double resPair = 2 * 0.02585;
        double outSwing = 5.0;        // VCF OUT peak swing (10 V p-p maximum)
        double vcaMax = 0.93;         // final VCA's maximum current gain
        double vcaFull = 5.0;         // control voltage for maximum gain
        double vcaOff = 0.085;        // control voltage for maximum attenuation
        double vcaFloor = 1e-6;       // -120 dB feed-through
        double vcaLimit = 0.8e-3;     // maximum possible input current (soft limit)
        // The filter's own noise, white, referred to its input: VCF OUT carries this much (V rms in
        // 20 kHz) with the filter wide open [D: 200 uV maximum; C: typical a quarter of it]. It also
        // starts self-oscillation when nothing else feeds the filter.
        double noiseOut = 50e-6;
    };

    void prepare(double rate);
    void reset();
    // The noise generator's seed: each chip its own.
    void seed(uint32_t s) { m_rng = s != 0 ? s : 1; }

    // Cell capacitances: C_A (switched by the Xpander's FD), C_B, C_C, C_D.
    void setCaps(double a, double b, double c, double d) { m_c = {a, b, c, d}; }
    // A resistance in series with cell 1's capacitor (the board's switch that selects it).
    void setCell1Series(double ohms) { m_r1 = ohms; }
    // The res input's divider (pin 11 from VCF OUT through RX64 and RX63) and its coupling capacitor
    // with the load it sees.
    void setResInput(double divider, double couplingHz) { m_resDiv = divider; m_resHpHz = couplingHz; }

    // One sample of the mixer and filter. inA/inB: pins 6 and 7 (volts); levelA/levelB: pins 5 and 8;
    // freq: pin 15; res: pin 10.
    void filter(double inA, double inB, double levelA, double levelB, double freq, double res);
    // The cells' voltages (cell scale: about 1/100 of the output) and VCF OUT (pin 17, AC part).
    double cell(int i) const { return m_y[size_t(i)]; }
    double vcfOut() const { return m_out; }
    // VCF OUT through the res input's coupling capacitor: the node that also feeds the pole mix's
    // inverter (the Xpander's CX48 into RX68 and RX64).
    double vcfOutCoupled() const { return m_coupled; }
    // The final VCA: input current (amps into pin 12's virtual ground), control voltage on pin 13;
    // returns the output current.
    double vca(double iIn, double cv) const;
    // The Newton solves since prepare() (solver_stats.h).
    const SolverStats& stats() const { return m_stats; }

    Params params;

private:
    // Newton's iterations for one step, and the most steps a retry splits a sample into.
    static constexpr int kIterations = 16, kMaxParts = 16;
    // One trapezoidal step with the inputs held: wv are the cells' gains and gh the coupling's for
    // the step's length. 1: converged, the state moved on; 0: not converged in kIterations, the
    // state moved on only if `accept`; -1: not finite, the state left as it was.
    int step(double u, double k, double G, const double* wv, double gh, bool accept);

    double m_rate = 192000;
    std::array<double, 4> m_c { 0.033e-6, 0.033e-6, 0.033e-6, 330e-12 };
    double m_r1 = 0;
    // Values that only change with their inputs, kept from the last sample (the same arithmetic).
    double m_wFreq = std::numeric_limits<double>::quiet_NaN(), m_w0 = 0, m_h0 = 0;
    std::array<double, 4> m_wc {}, m_wv {};
    double m_tapLevel[2] = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    double m_tapValue[2] = {0, 0};
    std::array<double, 4> m_s {}; // trapezoidal integrator states
    std::array<double, 4> m_y {};
    std::array<double, 4> m_m { 1, 1, 1, 1 }; // tanh secant gains
    double m_resDiv = 1.2 / 21.2, m_resHpHz = 4.0;
    double m_hpS = 0;  // the res input's coupling capacitor (one-pole low-pass state of VCF OUT)
    double m_out = 0;
    double m_coupled = 0;
    uint32_t m_rng = 1; // xorshift32
    SolverStats m_stats;
};

} // namespace xpander
