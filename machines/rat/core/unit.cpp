#include "unit.h"

#include <algorithm>
#include <iterator>
#include <type_traits>

namespace rat {

int oversamplingFor(double hostRate)
{
	if (hostRate <= 50000)
		return 8;
	if (hostRate <= 100000)
		return 4;
	return 2;
}

void Channel::prepare(double hostRate)
{
	const int f = oversamplingFor(hostRate);
	m_os.prepare(hostRate, f, 20000);
	m_pedal.prepare(hostRate * f);
	m_fadeStep = 1.0 / std::max(1.0, 0.005 * hostRate);
	m_mix = m_engaged ? 1.0 : 0.0;
	m_inGlide = 1.0 - std::exp(-1.0 / (0.01 * hostRate / 3)); // 95 % of the way in 10 ms
	m_inGain = m_inTarget;
	reset();
}

void Channel::reset()
{
	m_os.reset();
	m_pedal.reset();
}

static_assert(std::is_trivially_copyable<Oversampler>::value, "copyStateFrom must not allocate");
static_assert(std::is_trivially_copyable<DKMatrices>::value, "copyStateFrom must not allocate");

void Channel::copyStateFrom(const Channel &o)
{
	m_pedal.copyStateFrom(o.m_pedal);
	m_os = o.m_os;
	m_vfs = o.m_vfs;
	m_inGain = o.m_inGain;
	m_inTarget = o.m_inTarget;
	m_inGlide = o.m_inGlide;
	m_engaged = o.m_engaged;
	m_mix = o.m_mix;
	m_fadeStep = o.m_fadeStep;
	std::copy(std::begin(o.m_buf), std::end(o.m_buf), std::begin(m_buf));
}

double Channel::process(double x)
{
	const int f = m_os.factor();
	m_os.up(x * inputGain() * m_vfs, m_buf);
	for (int k = 0; k < f; ++k)
		m_buf[k] = m_pedal.process(m_buf[k]);
	return mix(m_os.down(m_buf) / m_vfs, x);
}

void Channel::processPair(Channel &a, Channel &b, double xa, double xb, double &ya, double &yb)
{
	const int f = a.m_os.factor();
	if (b.m_os.factor() != f) {
		ya = a.process(xa);
		yb = b.process(xb);
		return;
	}
	a.m_os.up(xa * a.inputGain() * a.m_vfs, a.m_buf);
	b.m_os.up(xb * b.inputGain() * b.m_vfs, b.m_buf);
	for (int k = 0; k < f; ++k)
		Pedal::processPair(a.m_pedal, b.m_pedal, a.m_buf[k], b.m_buf[k], a.m_buf[k], b.m_buf[k]);
	ya = a.mix(a.m_os.down(a.m_buf) / a.m_vfs, xa);
	yb = b.mix(b.m_os.down(b.m_buf) / b.m_vfs, xb);
}

double Channel::mix(double wet, double x)
{
	const double target = m_engaged ? 1.0 : 0.0;
	if (m_mix != target)
		m_mix = target > m_mix ? std::min(target, m_mix + m_fadeStep) : std::max(target, m_mix - m_fadeStep);
	if (m_mix >= 1.0)
		return wet;
	return m_mix * wet + (1 - m_mix) * x;
}

} // namespace rat
