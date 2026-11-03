#include "hihat.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace tr909 {

namespace {
constexpr double kPi = 3.14159265358979323846;

// The resets (IC71 pin 11, IC70 pin 15, IC73 pins 4 and 10) are held while C159's charge runs
// down R491 to the CMOS threshold, VDD / 2: R491 C159 ln 2 = 6.9 us. The astable restarts with A
// rising at once (below) and B half a period later, 7.9 us: so IC73b takes that first B edge and
// the first sample is latched 1.5 periods (24 us) after the trigger. A threshold under 2.2 V would
// hold the reset past it and move every sample one half period (8 us) later.
constexpr double kResetTime = HiHat::kR491 * HiHat::kC159 * 0.69314718055994531;

// The sinc smoothing: a Kaiser-windowed sinc low-pass at 0.45 of the step rate, kHalf steps a
// side, integrated into a step.
constexpr int kHalf = 8;
constexpr double kCutoff = 0.45;
constexpr double kBeta = 9;

double besselI0(double x)
{
	double sum = 1, term = 1;
	for (int k = 1; k < 50; ++k) {
		term *= (x / (2 * k)) * (x / (2 * k));
		sum += term;
		if (term < 1e-14 * sum)
			break;
	}
	return sum;
}
} // namespace

HiHat::HiHat()
{
	setRate(192000);
}

void HiHat::setRom(const uint8_t *data, size_t size)
{
	m_rom.assign(size_t(kRomSize), 0x80);
	if (data != nullptr)
		std::copy(data, data + std::min(size, size_t(kRomSize)), m_rom.begin());
}

void HiHat::setRate(double hz)
{
	m_rate = hz;
	m_dt = 1.0 / hz;
	setSmoothing(m_smoothing);
}

void HiHat::setSmoothing(Smoothing s)
{
	m_smoothing = s;
	if (s == Smoothing::Box) {
		m_width = 1;
		m_latency = 0.5;
		m_table.clear();
		return;
	}
	// S(x) over [0, 2 kHalf]: the windowed sinc's running integral, normalised to end at 1
	m_width = 2 * kHalf;
	m_latency = kHalf;
	const int n = m_width * kTableRes;
	m_table.assign(size_t(n + 1), 0.0);
	const double i0 = besselI0(kBeta);
	auto kernel = [&](double x) { // x in steps from the centre
		const double r = x / kHalf;
		const double w = besselI0(kBeta * std::sqrt(std::max(0.0, 1 - r * r))) / i0;
		const double a = 2 * kPi * kCutoff * x;
		return 2 * kCutoff * (std::abs(a) < 1e-12 ? 1.0 : std::sin(a) / a) * w;
	};
	double sum = 0, prev = kernel(-kHalf);
	for (int k = 1; k <= n; ++k) { // trapezoidal rule on the table's grid
		const double v = kernel(double(k) / kTableRes - kHalf);
		sum += 0.5 * (prev + v) / kTableRes;
		m_table[size_t(k)] = sum;
		prev = v;
	}
	for (double &v : m_table)
		v /= sum;
}

void HiHat::setClock(double hz)
{
	m_halfPeriod = 0.25 / hz; // the sample clock is half the astable's frequency
}

void HiHat::reset()
{
	m_count = 0x2000;
	m_closed = true;
	m_run = false;
	m_address = 0x7FFF;
	m_code = m_rom.empty() ? 32 : m_rom[0x7FFF] >> 2;
	m_oscOn = false;
	m_nextA = true;
	m_tNext = 0;
	m_resetUntil = -1;
	m_qa = m_qb = false;
	m_time = 0;
	m_pending = 0;
	for (int k = 0; k < kLines; ++k) {
		m_level[k] = k < 6 ? ((m_code >> k) & 1) * kVdd : 0.0;
		m_base[k] = m_level[k];
		std::fill(std::begin(m_ring[k]), std::end(m_ring[k]), 0.0);
		std::fill(std::begin(m_done[k]), std::end(m_done[k]), 0.0);
	}
	m_pos = 0;
	m_settled = true;
	m_sinceEdge = 1 << 20;
	for (int k = 0; k < 6; ++k)
		m_out.d[k] = m_level[k];
	m_out.run = m_level[6];
}

void HiHat::start(bool closed, double delay)
{
	if (m_pending == int(std::size(m_queue)))
		return;
	m_queue[m_pending++] = {std::max(0.0, delay), true, closed};
	std::stable_sort(m_queue, m_queue + m_pending, [](const Pending &a, const Pending &b) { return a.t < b.t; });
}

void HiHat::setClosed(bool closed, double delay)
{
	if (m_pending == int(std::size(m_queue)))
		return;
	m_queue[m_pending++] = {std::max(0.0, delay), false, closed};
	std::stable_sort(m_queue, m_queue + m_pending, [](const Pending &a, const Pending &b) { return a.t < b.t; });
}

unsigned HiHat::address() const
{
	// D196-D199: counter bits 13 and 14 ORed with CLOSED onto A14 and A13 (in the public images'
	// order, bits 13 and 14): a closed hat reads 6000-7FFF. The image's closed region starts with
	// kClosedLead samples of 80h that the 909 doesn't play: on two units the open hat runs on until
	// a closed hat's sound starts, and a single closed hat holds its full level for the trigger's
	// 2 ms after its onset (docs/circuit/hh.md, "The closed hat's first 65 samples"); the image is
	// taken to be that much off the counter there, the 65 samples coming last.
	if (m_closed)
		return 0x6000u | ((m_count + kClosedLead) & 0x1FFFu);
	return m_count & 0x7FFFu;
}

void HiHat::updateRun()
{
	// IC72a: NAND of bit 13 and (bit 14 OR CLOSED): the counter stops at 6000 open, 2000 closed
	const bool b13 = (m_count & 0x2000u) != 0, b14 = (m_count & 0x4000u) != 0;
	const bool run = !(b13 && (b14 || m_closed));
	if (run == m_run)
		return;
	m_run = run;
	lineStep(6, m_tNow, run ? kVdd : -kVdd);
	if (m_log != nullptr)
		m_log->push_back({m_time + m_tNow, m_code, m_run});
	if (run) {
		// The stopped astable sits self-biased (IC72d's output forced high, IC72c's input at its
		// threshold through R492 + R493): when RUN rises IC72d's output falls, C160 takes the
		// junction 5 V down, and A (IC72c pin 10) rises at once, as at any A edge.
		if (!m_oscOn) {
			m_oscOn = true;
			m_nextA = true;
			m_tNext = m_tNow;
		}
	} else {
		m_oscOn = false;
	}
}

void HiHat::latch()
{
	// IC68 takes ROM D7..D2 (only six of IC69's outputs are wired)
	const int code = m_rom.empty() ? 32 : m_rom[m_address & 0x7FFFu] >> 2;
	const int changed = code ^ m_code;
	if (changed == 0)
		return;
	for (int k = 0; k < 6; ++k)
		if ((changed >> k) & 1)
			lineStep(k, m_tNow, ((code >> k) & 1) ? kVdd : -kVdd);
	m_code = code;
	if (m_log != nullptr)
		m_log->push_back({m_time + m_tNow, m_code, m_run});
}

void HiHat::clockCounter()
{
	m_count = (m_count + 1u) & 0xFFFFu;
	updateRun();
}

void HiHat::edge(bool a)
{
	// IC73a toggles on A rising, IC73b on B rising (A falling); IC73b's Q is IC71's clock (falling
	// edge) and IC69's clocked CE (the address is taken as it rises); IC72b, the NAND of both Qs,
	// clocks IC68 as it rises. Held reset, the flip-flops and the counter don't move.
	m_nextA = !a;
	m_tNext += m_halfPeriod;
	if (m_tNow < m_resetUntil)
		return;
	const bool both = m_qa && m_qb;
	if (a) {
		m_qa = !m_qa;
	} else {
		m_qb = !m_qb;
		if (m_qb)
			m_address = address();
	}
	if (both && !(m_qa && m_qb))
		latch();
	if (!a && !m_qb)
		clockCounter();
}

void HiHat::lineStep(int line, double t, double dv)
{
	m_level[line] += dv;
	m_settled = false;
	m_sinceEdge = 0;
	// p: the edge's place against this step's end, in steps (-1, 0]; the line's contribution to
	// the k-th output from now is dv S(k - p), S rising from 0 to 1 over [0, width]
	const double p = t / m_dt - 1;
	int k = 0;
	for (; k - p < m_width && k < kRing - 1; ++k) {
		const double x = k - p;
		double s;
		if (m_smoothing == Smoothing::Box) {
			s = std::min(1.0, x);
		} else {
			const double f = x * kTableRes;
			const int i = std::min(int(f), int(m_table.size()) - 2);
			s = m_table[size_t(i)] + (m_table[size_t(i + 1)] - m_table[size_t(i)]) * (f - i);
		}
		m_ring[line][(m_pos + k) % kRing] += dv * s;
	}
	m_done[line][(m_pos + k) % kRing] += dv;
}

const HiHat::Lines &HiHat::step()
{
	for (;;) {
		double te = m_dt;
		int which = 0;
		if (m_pending > 0 && m_queue[0].t < te) {
			te = m_queue[0].t;
			which = 1;
		}
		if (m_oscOn && m_tNext < te) {
			te = m_tNext;
			which = 2;
		}
		if (which == 0)
			break;
		m_tNow = std::max(0.0, te);
		if (which == 2) {
			edge(m_nextA);
			continue;
		}
		const Pending e = m_queue[0];
		std::copy(m_queue + 1, m_queue + m_pending, m_queue);
		--m_pending;
		m_closed = e.closed;
		if (e.trig) {
			// C159/R491's pulse resets IC71, IC70b and IC73: a reset dropping IC73b's Q while both
			// Qs are high makes IC72b's edge, and IC68 takes the ROM's output once more
			if (m_qa && m_qb)
				latch();
			m_qa = m_qb = false;
			m_count = 0;
			m_resetUntil = m_tNow + kResetTime;
		}
		updateRun();
	}
	m_tNext -= m_dt;
	m_resetUntil -= m_dt;
	for (int k = 0; k < m_pending; ++k)
		m_queue[k].t -= m_dt;
	m_time += m_dt;

	// the lines' smoothed values at this step's end
	for (int k = 0; k < kLines; ++k) {
		m_base[k] += m_done[k][m_pos];
		m_done[k][m_pos] = 0;
		const double v = m_base[k] + m_ring[k][m_pos];
		m_ring[k][m_pos] = 0;
		if (k < 6)
			m_out.d[k] = v;
		else
			m_out.run = v;
	}
	m_pos = (m_pos + 1) % kRing;
	if (!m_settled && ++m_sinceEdge > m_width + 1) {
		// the contributions have all landed: snap the levels to the logic's (no drift)
		for (int k = 0; k < kLines; ++k)
			m_base[k] = m_level[k];
		m_settled = true;
	}
	return m_out;
}

} // namespace tr909
