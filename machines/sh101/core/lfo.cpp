// The MODULATOR's oscillator: IC3a/IC3b, TR4-TR7, the RATE divider (docs/circuit-osc.md 5).
#include "lfo.h"

#include "osc_circuit.h"

#include <algorithm>
#include <cmath>

namespace sh101 {

namespace {
using namespace oscm;
constexpr double kC9 = 0.1e-6, kR15 = 120e3, kR16 = 15e3, kR168 = 220, kRpot = 100e3;
constexpr double kHigh = kV15 - 2.0, kLow = -3.0;    // IR9022 on +15 / -5 V, 2 V inside the rails
// CLK RST turns TR4 on: R24 and R17 drive its base from +15 V through D10, and that base current
// flows into IC3a's summing node whatever the square does; while the square is high, D9 and R16 add
// TR4's collector current.
constexpr double kBase4 = (kV15 - 0.62 - 0.65 - kVref) / (82e3 + 47e3);
constexpr double kFast = (kHigh - 0.6 - 0.1 - kVref) / kR16;
constexpr double kLoopSec = 1.5e-3;                  // one CPU loop, for reset()
} // namespace

void Lfo::prepare(double rate)
{
	m_dt = 1 / rate;
	update();
}

void Lfo::setRate(double knob)
{
	if (knob != m_knob) {
		m_knob = knob;
		update();
	}
}

void Lfo::setWave(LfoWave w) { m_vh = ModSwitch::thresholdHigh(w); }

void Lfo::setClkRst(bool active) { m_rstLevel = active; }

void Lfo::reset() { m_rstTimer = kLoopSec; }

void Lfo::update()
{
	// VR10 100k (A) from IC3b's output to the bus through R168: the wiper's share of the square,
	// and the wiper's source resistance in series with R15.
	const double a = taperA(m_knob);
	const double rb = a * kRpot + kR168, rt = (1 - a) * kRpot;
	m_k = rb / (rb + rt);
	m_rIn = kR15 + rb * rt / (rb + rt);
}

double Lfo::hz() const
{
	const double i = m_k * (kHigh - kVref) / m_rIn;
	return i / (2 * kC9 * (m_vh - ModSwitch::thresholdLow()));
}

Lfo::Out Lfo::tick()
{
	const bool rst = m_rstLevel || m_rstTimer > 0;
	const double vl = ModSwitch::thresholdLow();
	double t = m_dt;
	for (int guard = 0; guard < 8 && t > 0; ++guard) {
		m_v = std::max(m_v, kLow + 0.5); // IC3a's output limit
		// The integrator inverts: the square high drives the triangle down.
		const double vsq = m_high ? kHigh : kLow;
		double slope = -m_k * (vsq - kVref) / m_rIn / kC9;
		if (rst)
			slope -= (kBase4 + (m_high ? kFast : 0)) / kC9;
		if (m_high) {
			// Falls to the lower threshold (TR5 or TR6 saturated), then IC3b flips low.
			const double hit = (m_v - vl) / -slope;
			if (hit >= t) {
				m_v += slope * t;
				break;
			}
			m_v = vl;
			t -= hit;
			m_high = false;
		} else {
			// Rises to the node, which TR6 holds low during CLK RST. There TR4's base current can
			// outweigh the RATE current and keep the triangle falling below the threshold.
			const double thr = rst ? vl : m_vh;
			if (m_v > thr + 1e-9) {
				m_high = true;
				continue;
			}
			if (slope <= 0) {
				m_v += slope * t;
				break;
			}
			const double hit = (thr - m_v) / slope;
			if (hit >= t) {
				m_v += slope * t;
				break;
			}
			m_v = thr;
			t -= hit;
			m_high = true;
		}
	}
	if (m_rstTimer > 0)
		m_rstTimer -= m_dt;
	m_out.tri = float(m_v - kVref);
	m_out.square = m_high || rst;
	return m_out;
}

} // namespace sh101
