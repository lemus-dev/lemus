// The SSM2120 dynamic range processor (U4 on sheet 1): two VCAs and two level detectors, from its
// data sheet (PMI SSM-2120/SSM-2122 rev B1, 5/90) and Analog Devices' AN-382 (docs/circuit.md):
//   VCA: current in (a virtual ground), current out (into an external op-amp's virtual ground), the
//     gain 10^((Vc+ - Vc-) / 120 mV) (+-6 mV/dB), the path through the external converter
//     non-inverting; the currents clip at +-2 I_REF.
//   Level detector: RECIN a virtual ground (2.1 V DC, AC-coupled in); its full-wave rectified
//     current charges the LOG AV capacitor through the logging pair, which makes the capacitor a
//     linear integrator of |I_IN| against I_REF: y' = (|I_IN| - I_REF y) / (C V_T), LOG AV =
//     V_T ln y. So the attack depends on the step and the decay is a linear ramp in dB (8.686 I_REF
//     / (C V_T) dB/s, the sheet's I_REF x 333 / C).
//   Control amplifier: non-inverting from LOG AV, its gain and offset from the THRESH network; a
//     unipolar output that only sinks ("Output Drive (Max Sink Current) 7.5 mA"), so it rests at
//     0 V on its load and swings down.
#pragma once

#include <algorithm>
#include <cmath>

namespace qvgt {

// The current through a series R-C from a driven node into a virtual ground: I = V sC / (1 + sRC),
// by the bilinear transform.
class SeriesRc {
public:
	void setup(double r, double c, double rate)
	{
		const double k = 2 * rate * r * c; // 2 RC / T
		m_b = 2 * rate * c / (1 + k);      // (2C/T) / (1 + 2RC/T)
		m_a = (1 - k) / (1 + k);
		m_x1 = m_y1 = 0;
	}
	double operator()(double v)
	{
		const double y = m_b * (v - m_x1) - m_a * m_y1;
		m_x1 = v;
		m_y1 = y;
		return y;
	}
	void reset() { m_x1 = m_y1 = 0; }

private:
	double m_a = 0, m_b = 0, m_x1 = 0, m_y1 = 0;
};

struct Ssm2120 {
	static constexpr double kVt = 0.02585;       // kT/q at 27 C
	static constexpr double kVoltsPerDb = 0.006; // the VCAs' control law

	// A VCA's current gain for its control ports.
	static double gain(double vcPlus, double vcMinus) { return std::pow(10.0, (vcPlus - vcMinus) / (20 * kVoltsPerDb)); }
	// A VCA's output current for `iin` into its input, the gain set by its control ports.
	static double vca(double iin, double vcPlus, double vcMinus, double iref) { return vcaAt(iin, gain(vcPlus, vcMinus), iref); }
	// The same at a gain already worked out.
	static double vcaAt(double iin, double gain, double iref) { return -std::clamp(gain * iin, -2 * iref, 2 * iref); }

	// A level detector: LOG AV's state for one step of `dt`.
	struct Detector {
		double c = 1e-6, iref = 12e-6;
		double y = 0; // the averaged |I_IN| / I_REF
		void setup(double dt)
		{
			m_k = std::exp(-dt * iref / (c * kVt));
			m_g = (1 - m_k) / iref;
		}
		void step(double iin) { y = y * m_k + std::abs(iin) * m_g; }
		// LOG AV's volts; the rectifier's floor 120 dB under I_REF (the sheet: 100-110 dB of range)
		double logAv() const { return kVt * std::log(std::max(y, 1e-6)); }

	private:
		double m_k = 1, m_g = 0;
	};
};

} // namespace qvgt
