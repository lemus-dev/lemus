#pragma once

#include <cmath>

namespace tb303 {

// The six service trimmers (service notes p.6-7, docs/circuit.md §8) as rotations, 0 fully
// anticlockwise to 1 fully clockwise, their tracks linear (B taper). Each is wired here so
// clockwise raises what the service notes measure with it. Trims{} is the unit Lemus plays:
// every adjustment on the notes' spec, TM3 at the fitted unit's 2.5 ms ring (core/voice.h,
// unitCalibration). The plugin's SERVICE view turns them; the voice sees them through
// Calibration (vcfRingHz, vcoScale, tuneCents), the clock and CPU through tempoBpm and
// irqSeconds below.
namespace trims {

// TM3 FREQ (VCF): a 470k rheostat in series with R73 100k from the CUT OFF wiper into the
// expo's reference node (§4.5), so the cutoff at a given CUT OFF goes as 1/(R73 + TM3). Where
// in its travel the notes' 2 ms ring falls is not known: mid travel is assumed (UNCERTAIN),
// which makes the whole travel 0.6 to 3.4 ms.
constexpr double kR73 = 100e3, kTm3Ohms = 470e3, kTm3MidRingHz = 500;
inline double tm3RingHz(double p) { return kTm3MidRingHz * (kR73 + 0.5 * kTm3Ohms) / (kR73 + kTm3Ohms * p); }
inline double tm3For(double ringHz) { return (kTm3MidRingHz * (kR73 + 0.5 * kTm3Ohms) / ringHz - kR73) / kTm3Ohms; }

// TM6 CV: trims the 5.333 V reference (IC16b), which the DAC, the +12 V rail and everything
// biased from them follow, by a factor k: CV OUT's 1.000 V per octave becomes k volts. Its
// range is not worked out from the IC16b network (UNCERTAIN): +-5 % is assumed. Only the pitch
// side is modelled (the CV and the VCO's reference currents), not the VCF and VCA biases.
inline double cvRef(double p6) { return 1 + 0.05 * (2 * p6 - 1); }

// TM5 WIDTH: a 4.7k rheostat in series with R106 27k from the CV into the expo's base, against
// R100 (560R PTC) || R118 100k to the 5.333 V reference (§3.1). The divider sets the volts
// per octave; 1.000 V needs R106 + TM5 + R100||R118 = V_T ln 2 / 1 V into R100||R118.
constexpr double kVt = 0.02569;
constexpr double kRpar = 1 / (1 / 560.0 + 1 / 100e3);
constexpr double kR106 = 27e3, kTm5Ohms = 4.7e3;
inline double tm5Ohms(double p5) { return kTm5Ohms * (1 - p5); } // clockwise: less R, wider octaves
inline double tm5Nominal() { return 1 - (kRpar / (kVt * 0.6931471805599453) - kRpar - kR106) / kTm5Ohms; }
// Octaves per volt of CV at the expo, against exactly 1.
inline double width(double p5) { return (kRpar + kR106 + tm5Ohms(tm5Nominal())) / (kRpar + kR106 + tm5Ohms(p5)); }

// TM4 TUNE: its wiper, between the 5.333 V reference and +12 V, feeds the expo's reference
// collector (held at 5.333 V) through R102 100k, beside R103 220k from +12 V: I_ref, and the
// pitch with it, is 30 to 95 uA (43 uA for 110 Hz, §3.3). The trimmer is taken as 47k, so its
// wiper resistance loads R102 a little (the parts list; X0X has 50k).
constexpr double kR102 = 100e3, kR103 = 220e3, kTm4Ohms = 47e3;
inline double iRef(double p4, double k)
{
	const double dv = k * 5.333333333333333 * 2.2 / 1.8; // +12 V (11.85) less the reference, both trimmed by TM6
	return dv / kR103 + p4 * dv / (kR102 + kTm4Ohms * p4 * (1 - p4));
}
constexpr double kTm4Nominal = 0.22175067783382682; // I_ref = 43 uA

// The expo compares the CV with the 5.333 V reference: a width other than 1 turns the pitch
// about that level, 2.583 V above A's 2.75 V, and the reference currents set where A lands.
constexpr double kAToRefV = 5.333333333333333 - 2.75;

struct Trims {
	double tm1 = 0.5, tm2 = 0.5, tm3 = tm3For(400), tm4 = kTm4Nominal, tm5 = tm5Nominal(), tm6 = 0.5;
};

// The voice's VCO law f = 110 Hz x 2^((V_CV - 2.75) x vcoScale + tuneCents / 1200), with V_CV the
// DAC's volts at a nominal reference.
inline double vcoScale(const Trims &t) { return width(t.tm5) * cvRef(t.tm6); }
inline double tuneCents(const Trims &t)
{
	return 1200 * (kAToRefV * (1 - vcoScale(t)) + std::log2(iRef(t.tm4, cvRef(t.tm6)) / iRef(kTm4Nominal, 1)));
}

// TM1 TEMPO: R18 + TM1, the IC6 oscillator's fixed resistance beside the TEMPO pot. Which
// trimmer it is (47k or 220k) is UNCERTAIN: +-40 % of the trimmed value is assumed.
inline double tempoRScale(double p1) { return 0.6 + 0.8 * p1; }
// TM2 INT CLOCK: the CPU's interrupt oscillator, 1.8 ms trimmed; +-40 % assumed likewise.
inline double irqSeconds(double p2) { return 1.8e-3 * (0.6 + 0.8 * p2); }

// What the service notes measure, and their spec.
inline double tp1PeriodMs(const Trims &t) { return 60e3 / 300 / 24 * tempoRScale(t.tm1); } // TEMPO full clockwise
inline double tp2PeriodMs(const Trims &t) { return 1e3 * irqSeconds(t.tm2); }
inline double cvPerOctave(const Trims &t) { return cvRef(t.tm6); }       // volts, CV OUT
inline double octaveRatio(const Trims &t) { return std::exp2(vcoScale(t)); } // C' : C
inline double aHz(const Trims &t) { return 110 * std::exp2(tuneCents(t) / 1200); } // TUNING centre
inline double ringMs(const Trims &t) { return 1e3 / tm3RingHz(t.tm3); }

inline bool tp1InSpec(const Trims &t) { return std::abs(tp1PeriodMs(t) - 8) <= 1; }
inline bool tp2InSpec(const Trims &t) { return std::abs(tp2PeriodMs(t) - 1.8) <= 0.2; }
inline bool cvInSpec(const Trims &t) { return std::abs(cvPerOctave(t) - 1) <= 0.003; }
// "Confirm 4:1 +-0.5 %" over two octaves.
inline bool widthInSpec(const Trims &t) { return std::abs(octaveRatio(t) * octaveRatio(t) / 4 - 1) <= 0.005; }
// The notes give no tolerance for 110 Hz; a counter's reading within 0.2 Hz is taken as set.
inline bool tuneInSpec(const Trims &t) { return std::abs(aHz(t) - 110) <= 0.2; }
inline bool ringInSpec(const Trims &t) { return std::abs(ringMs(t) - 2) <= 0.5 + 1e-9; }

} // namespace trims
} // namespace tb303
