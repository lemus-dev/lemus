#include "cymbals.h"

#include "circuit.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>

namespace tr909 {

namespace {
constexpr double kPi = 3.14159265358979323846;

// The sinc smoothing: a Kaiser-windowed sinc low-pass at 0.45 of the step rate, kHalf steps a
// side, integrated into a step (HiHat's).
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

// The clock's timings at the TUNE positions, by netlist and parameters: timed once in a process.
std::mutex g_cacheLock;
std::map<std::string, std::vector<Cymbal::Timing>> g_cache;

// The timing between TUNE positions i and i + 1, f of the way: a Catmull-Rom cubic through the
// positions around, the end segments through the parabola of their three positions (the half
// periods bend near TUNE 1: A's has its minimum at 0.97). Against the clock netlist run at the TUNE
// itself: within 2e-5 of the period halfway between positions, where a straight line between them
// is up to 1e-3 off near TUNE 1.
Cymbal::Timing interpolate(const std::vector<Cymbal::Timing> &tab, int i, double f)
{
	const int n = int(tab.size());
	const bool lo = i == 0, hi = i + 2 >= n;
	const Cymbal::Timing &b = tab[size_t(i)], &c = tab[size_t(i + 1)];
	const Cymbal::Timing &a = tab[size_t(lo ? i + 2 : i - 1)], &d = tab[size_t(hi ? i - 1 : i + 2)];
	auto cubic = [&](double p0, double p1, double p2, double p3) {
		if (lo) // p0 holds position i + 2: the parabola's value a position below i
			p0 = 3 * p1 - 3 * p2 + p0;
		if (hi) // p3 holds position i - 1
			p3 = p3 - 3 * p1 + 3 * p2;
		return p1 + 0.5 * f * (p2 - p0 + f * (2 * p0 - 5 * p1 + 4 * p2 - p3 + f * (3 * (p1 - p2) + p3 - p0)));
	};
	Cymbal::Timing t = f < 0.5 ? b : c;
	t.reset = cubic(a.reset, b.reset, c.reset, d.reset);
	t.ab = cubic(a.ab, b.ab, c.ab, d.ab);
	t.ba = cubic(a.ba, b.ba, c.ba, d.ba);
	if (a.firstIsA == b.firstIsA && b.firstIsA == c.firstIsA && c.firstIsA == d.firstIsA)
		for (int k = 0; k < 4; ++k)
			t.first[k] = cubic(a.first[k], b.first[k], c.first[k], d.first[k]);
	return t;
}
} // namespace

Cymbal::Cymbal()
{
	setRate(192000);
	// until setClock: about the AudioRealism unit's crash at TUNE centre (36.7 kHz)
	m_timing.reset = 6.9e-6;
	m_timing.ab = m_timing.ba = 0.25 / 36680;
	for (int k = 0; k < 4; ++k)
		m_timing.first[k] = 10e-6 + k * m_timing.ab;
	reset();
}

void Cymbal::setRom(const uint8_t *data, size_t size)
{
	m_rom.assign(size_t(kRomSize), 0x80);
	if (data != nullptr)
		std::copy(data, data + std::min(size, size_t(kRomSize)), m_rom.begin());
}

bool Cymbal::timeClock(const std::string &netlist, const std::map<std::string, double> &params, double tune, double rate,
                       Timing &out, std::vector<double> *trace)
{
	circuit::Netlist net;
	try {
		net = circuit::Netlist::parse(netlist, params);
	} catch (...) {
		return false;
	}
	const int iTrig = net.input("trig"), iRun = net.input("run");
	const int oA = net.output("osca"), oB = net.output("oscb"), oR = net.output("rst");
	const int cTune = net.control("tune");
	if (iTrig < 0 || iRun < 0 || oA < 0 || oB < 0 || oR < 0 || cTune < 0)
		return false;
	circuit::Circuit c(net);
	c.setRate(rate);
	c.setControl(cTune, tune);
	// the clock stopped and settled (RUN low: IC59b's output high, IC59c self-biased), then TRIG's
	// edge, which resets the counter and so lets RUN rise with it
	c.setInput(iTrig, 0);
	c.setInput(iRun, 0);
	if (!c.solveDc())
		return false;
	c.setInput(iTrig, kVdd);
	c.setInput(iRun, kVdd);
	c.jump();
	const double dt = 1 / rate;
	double a0 = c.output(oA), b0 = c.output(oB), r0 = kVdd;
	std::vector<std::pair<double, bool>> edges;
	out.reset = 0;
	const long limit = long(2e-3 * rate); // a clock that hasn't made its edges in 2 ms doesn't run
	for (long n = 1; n <= limit && edges.size() < 8; ++n) {
		c.step();
		const double a1 = c.output(oA), b1 = c.output(oB), r1 = c.output(oR);
		const double t = double(n) * dt;
		// each rising edge where it crosses the inputs' threshold, between the steps
		if (a0 < kThreshold && a1 >= kThreshold)
			edges.push_back({t - dt * (a1 - kThreshold) / (a1 - a0), true});
		if (b0 < kThreshold && b1 >= kThreshold)
			edges.push_back({t - dt * (b1 - kThreshold) / (b1 - b0), false});
		if (out.reset == 0 && r0 >= kThreshold && r1 < kThreshold)
			out.reset = t - dt * (kThreshold - r1) / (r0 - r1);
		if (trace != nullptr) {
			trace->push_back(t);
			trace->push_back(a1);
			trace->push_back(b1);
			trace->push_back(r1);
		}
		a0 = a1;
		b0 = b1;
		r0 = r1;
	}
	if (edges.size() < 8 || out.reset == 0)
		return false;
	std::sort(edges.begin(), edges.end());
	for (size_t k = 1; k < edges.size(); ++k)
		if (edges[k].second == edges[k - 1].second)
			return false; // A and B alternate, or it isn't the astable
	out.firstIsA = edges[0].second;
	for (int k = 0; k < 4; ++k)
		out.first[k] = edges[size_t(k)].first;
	// steadily: the two periods after the first four edges (12 edges move it by under 1e-5)
	const size_t n = edges.size();
	double ab = 0, ba = 0;
	for (size_t k = n - 4; k < n; ++k) {
		const double d = edges[k].first - edges[k - 1].first;
		(edges[k - 1].second ? ab : ba) += 0.5 * d;
	}
	out.ab = ab;
	out.ba = ba;
	return true;
}

bool Cymbal::setClock(const std::string &netlist, const std::map<std::string, double> &params)
{
	std::ostringstream key;
	key.precision(17);
	key << netlist;
	for (const auto &[k, v] : params)
		key << "\n" << k << "=" << v;
	std::vector<Timing> table;
	{
		std::lock_guard<std::mutex> lock(g_cacheLock);
		const auto it = g_cache.find(key.str());
		if (it != g_cache.end())
			table = it->second;
	}
	if (table.empty()) {
		// the positions on up to four threads (each its own circuit)
		table.resize(size_t(kTunePoints));
		std::atomic<int> next{0};
		std::atomic<bool> ok{true};
		auto work = [&]() {
			for (int k; (k = next++) < kTunePoints;)
				if (!timeClock(netlist, params, double(k) / (kTunePoints - 1), kClockRate, table[size_t(k)]))
					ok = false;
		};
		const int threads = int(std::clamp(std::thread::hardware_concurrency(), 1u, 4u));
		std::vector<std::thread> pool;
		for (int k = 1; k < threads; ++k)
			pool.emplace_back(work);
		work();
		for (auto &th : pool)
			th.join();
		if (!ok)
			return false;
		std::lock_guard<std::mutex> lock(g_cacheLock);
		g_cache[key.str()] = table;
	}
	m_table = table;
	m_timingSet = false;
	setTune(m_tune);
	return true;
}

void Cymbal::setTune(double rotation)
{
	rotation = std::clamp(rotation, 0.0, 1.0);
	if (m_table.empty() || (rotation == m_tune && m_timing.period() > 0 && m_timingSet)) {
		m_tune = rotation;
		return;
	}
	m_tune = rotation;
	const double x = m_tune * (kTunePoints - 1);
	const int i = std::min(int(x), kTunePoints - 2);
	const double old = m_timing.period();
	m_timing = interpolate(m_table, i, x - i);
	m_timingSet = true;
	// A running clock takes its new rate from now: what is left of its half period scales, and it
	// goes on steadily (a start's first edges are timed for one TUNE)
	if (m_oscOn && old > 0) {
		m_tNext *= m_timing.period() / old;
		m_startEdge = 4;
	}
}

void Cymbal::setRate(double hz)
{
	m_rate = hz;
	m_dt = 1.0 / hz;
	setSmoothing(m_smoothing);
}

void Cymbal::setSmoothing(Smoothing s)
{
	m_smoothing = s;
	if (s == Smoothing::Box) {
		m_width = 1;
		m_latency = 0.5;
		m_table1.clear();
		return;
	}
	// S(x) over [0, 2 kHalf]: the windowed sinc's running integral, normalised to end at 1
	m_width = 2 * kHalf;
	m_latency = kHalf;
	const int n = m_width * kTableRes;
	m_table1.assign(size_t(n + 1), 0.0);
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
		m_table1[size_t(k)] = sum;
		prev = v;
	}
	for (double &v : m_table1)
		v /= sum;
}

void Cymbal::setLevels()
{
	for (int k = 0; k < 6; ++k) {
		m_level[k] = ((m_code >> k) & 1) * kVdd;
		m_level[6 + k] = ((m_count >> (9 + k)) & 1) * kVdd;
	}
	m_level[12] = m_run ? kVdd : 0.0;
}

void Cymbal::reset()
{
	m_count = 0x8000;
	m_run = false;
	m_address = 0x7FFF;
	m_code = m_rom.empty() ? 32 : m_rom[0x7FFF] >> 2;
	m_oscOn = false;
	m_nextA = true;
	m_startEdge = 4;
	m_tOsc0 = m_tNext = 0;
	m_resetUntil = -1;
	m_qa = m_qb = false;
	m_time = 0;
	m_pending = 0;
	setLevels();
	for (int k = 0; k < kLines; ++k) {
		m_base[k] = m_level[k];
		std::fill(std::begin(m_ring[k]), std::end(m_ring[k]), 0.0);
		std::fill(std::begin(m_done[k]), std::end(m_done[k]), 0.0);
	}
	m_pos = 0;
	m_settled = true;
	m_sinceEdge = 1 << 20;
	for (int k = 0; k < 6; ++k) {
		m_out.d[k] = m_level[k];
		m_out.e[k] = m_level[6 + k];
	}
	m_out.run = m_level[12];
}

void Cymbal::start(double delay)
{
	if (m_pending == int(std::size(m_queue)))
		return;
	m_queue[m_pending++] = std::max(0.0, delay);
	std::sort(m_queue, m_queue + m_pending);
}

void Cymbal::startClock()
{
	// From the stopped astable: its first edges as the netlist times them, then steadily
	m_oscOn = true;
	m_tOsc0 = m_tNow;
	m_startEdge = 0;
	m_nextA = m_timing.firstIsA;
	m_tNext = m_tOsc0 + m_timing.first[0];
}

void Cymbal::scheduleNext()
{
	const bool a = m_nextA;
	m_nextA = !a;
	if (m_startEdge < 3) {
		++m_startEdge;
		m_tNext = m_tOsc0 + m_timing.first[m_startEdge];
	} else {
		m_startEdge = 4;
		m_tNext += a ? m_timing.ab : m_timing.ba;
	}
}

void Cymbal::latch()
{
	// IC63 takes ROM D7..D2 (only six of IC62's outputs are wired)
	const int code = m_rom.empty() ? 32 : m_rom[m_address & 0x7FFFu] >> 2;
	const int changed = code ^ m_code;
	if (changed == 0)
		return;
	for (int k = 0; k < 6; ++k)
		if ((changed >> k) & 1)
			lineStep(k, m_tNow, ((code >> k) & 1) ? kVdd : -kVdd);
	m_code = code;
	if (m_log != nullptr)
		m_log->push_back({m_time + m_tNow, m_code, int((m_count >> 9) & 63u), m_run});
}

void Cymbal::clockCounter()
{
	// IC61's clock falls: the count steps (the 4040's and 4520's ripple, well under a microsecond,
	// is left out), and with it the envelope D/A's bits 9-14 and, at 8000, NOT bit 15 (IC59d):
	// RUN falls, IC59b's output stays high and the clock stops; R427 mutes the VCA
	const unsigned old = m_count;
	m_count = (m_count + 1u) & 0xFFFFu;
	const unsigned changed = old ^ m_count;
	for (int k = 0; k < 6; ++k)
		if ((changed >> (9 + k)) & 1)
			lineStep(6 + k, m_tNow, ((m_count >> (9 + k)) & 1) ? kVdd : -kVdd);
	if (m_count & 0x8000u) {
		m_run = false;
		m_oscOn = false;
		lineStep(12, m_tNow, -kVdd);
	}
	if (m_log != nullptr && (changed >> 9) != 0)
		m_log->push_back({m_time + m_tNow, m_code, int((m_count >> 9) & 63u), m_run});
}

void Cymbal::edge(bool a)
{
	// IC60b toggles on A rising (IC59c pin 10), IC60a on B rising (IC59b pin 4). IC60a's Q is IC61's
	// clock (falling edge), its /Q IC62's clocked CE (active low: the address is taken as Q rises);
	// IC59a, the NAND of both Qs, clocks IC63 as it rises, when IC60a's Q falls. Held reset, the
	// flip-flops and the counter don't move.
	if (m_edgeLog != nullptr)
		m_edgeLog->push_back({m_time + m_tNow, a});
	scheduleNext();
	if (m_tNow < m_resetUntil)
		return;
	const bool both = m_qa && m_qb;
	if (a) {
		m_qb = !m_qb;
	} else {
		m_qa = !m_qa;
		if (m_qa)
			m_address = m_count & 0x7FFFu;
	}
	if (both && !(m_qa && m_qb))
		latch();
	if (!a && !m_qa)
		clockCounter();
}

void Cymbal::lineStep(int line, double t, double dv)
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
			const int i = std::min(int(f), int(m_table1.size()) - 2);
			s = m_table1[size_t(i)] + (m_table1[size_t(i + 1)] - m_table1[size_t(i)]) * (f - i);
		}
		m_ring[line][(m_pos + k) % kRing] += dv * s;
	}
	m_done[line][(m_pos + k) % kRing] += dv;
}

const Cymbal::Lines &Cymbal::step()
{
	for (;;) {
		double te = m_dt;
		int which = 0;
		if (m_pending > 0 && m_queue[0] < te) {
			te = m_queue[0];
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
		std::copy(m_queue + 1, m_queue + m_pending, m_queue);
		--m_pending;
		// C50/R189's pulse resets IC61, IC58a and IC60: a reset dropping IC60a's Q while both Qs
		// are high makes IC59a's edge, and IC63 takes the ROM's output once more. Bit 15 clears,
		// so RUN rises (IC59d) and a stopped clock starts; the latch isn't reset (its /R is tied high)
		if (m_qa && m_qb)
			latch();
		m_qa = m_qb = false;
		const unsigned changed = m_count & 0xFE00u;
		m_count = 0;
		for (int k = 0; k < 6; ++k)
			if ((changed >> (9 + k)) & 1)
				lineStep(6 + k, m_tNow, -kVdd);
		m_resetUntil = m_tNow + m_timing.reset;
		if (!m_run) {
			m_run = true;
			lineStep(12, m_tNow, kVdd);
			startClock();
		}
		if (m_log != nullptr)
			m_log->push_back({m_time + m_tNow, m_code, 0, m_run});
	}
	m_tNext -= m_dt;
	m_tOsc0 -= m_dt;
	m_resetUntil -= m_dt;
	for (int k = 0; k < m_pending; ++k)
		m_queue[k] -= m_dt;
	m_time += m_dt;

	// the lines' smoothed values at this step's end
	for (int k = 0; k < kLines; ++k) {
		m_base[k] += m_done[k][m_pos];
		m_done[k][m_pos] = 0;
		const double v = m_base[k] + m_ring[k][m_pos];
		m_ring[k][m_pos] = 0;
		if (k < 6)
			m_out.d[k] = v;
		else if (k < 12)
			m_out.e[k - 6] = v;
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
