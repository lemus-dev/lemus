#include "oversampler.h"

#include <algorithm>
#include <cmath>

namespace rat {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Elliptic modulus and nome for a half-band transition (HIIR's PolyphaseIir2Designer).
void transitionParams(double transition, double &k, double &q)
{
	k = std::tan((1 - transition * 2) * kPi / 4);
	k *= k;
	const double kksqrt = std::pow(1 - k * k, 0.25);
	const double e = 0.5 * (1 - kksqrt) / (1 + kksqrt);
	const double e4 = e * e * e * e;
	q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)));
}

double accNum(double q, int order, int c)
{
	double acc = 0, term = 0;
	int i = 0, sign = 1;
	do {
		term = std::pow(q, double(i * (i + 1))) * std::sin(double((i * 2 + 1) * c) * kPi / order) * sign;
		acc += term;
		sign = -sign;
		++i;
	} while (std::fabs(term) > 1e-100 && i < 1000);
	return acc;
}

double accDen(double q, int order, int c)
{
	double acc = 0, term = 0;
	int i = 1, sign = -1;
	do {
		term = std::pow(q, double(i * i)) * std::cos(double(i * 2 * c) * kPi / order) * sign;
		acc += term;
		sign = -sign;
		++i;
	} while (std::fabs(term) > 1e-100 && i < 1000);
	return acc;
}

} // namespace

std::vector<double> HalfBand::design(double attenuationDb, double transition)
{
	double k, q;
	transitionParams(transition, k, q);
	const double p2 = std::pow(10.0, -attenuationDb / 10);
	const double a = p2 / (1 - p2);
	int order = int(std::ceil(std::log(a * a / 16) / std::log(q)));
	if ((order & 1) == 0)
		++order;
	order = std::max(order, 3);
	const int count = (order - 1) / 2;
	std::vector<double> coefs(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i) {
		const int c = i + 1;
		const double num = accNum(q, order, c) * std::pow(q, 0.25);
		const double den = accDen(q, order, c) + 0.5;
		const double ww = num / den;
		const double wwsq = ww * ww;
		const double x = std::sqrt((1 - wwsq * k) * (1 - wwsq / k)) / (1 + wwsq);
		coefs[size_t(i)] = (1 - x) / (1 + x);
	}
	return coefs;
}

void HalfBand::setCoefs(const std::vector<double> &c)
{
	m_count[0] = m_count[1] = 0;
	for (auto &a : m_a)
		a = V2{1, 1};
	for (size_t i = 0; i < c.size() && i < 2 * kMaxSections; ++i) {
		const int lane = int(i & 1);
		m_a[m_count[lane]++][lane] = c[i];
	}
	m_n = std::max(m_count[0], m_count[1]);
	reset();
}

void HalfBand::reset()
{
	for (int i = 0; i < kMaxSections; ++i)
		m_x1[i] = m_y1[i] = V2{0, 0};
}

double HalfBand::delay() const
{
	// 0.5 (A0(z^2) + z^-1 A1(z^2)): each first-order section delays DC by (1 - a) / (1 + a) low-rate
	// samples, i.e. twice that at the high rate; the z^-1 adds one.
	double d[2] = {0, 0};
	for (int lane = 0; lane < 2; ++lane)
		for (int i = 0; i < m_count[lane]; ++i)
			d[lane] += 2 * (1 - m_a[i][lane]) / (1 + m_a[i][lane]);
	return 0.5 * (d[0] + 1 + d[1]);
}

double Oversampler::upDelay() const
{
	double d = 0;
	for (int s = 0; s < m_stages; ++s)
		d += m_up[size_t(s)].delay() / double(2 << s);
	return d;
}

double Oversampler::roundTripDelay() const
{
	// Up then down at stage s costs 2 delay() - 1 samples at its high rate: the z^-1 in each
	// filter is the interleaving itself, and the decimator's output lines up with its later input.
	double d = 0;
	for (int s = 0; s < m_stages; ++s)
		d += (2 * m_up[size_t(s)].delay() - 1) / double(2 << s);
	return d;
}

void Oversampler::prepare(double baseRate, int factor, double passHz)
{
	m_factor = factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
	m_stages = m_factor == 8 ? 3 : m_factor == 4 ? 2 : m_factor == 2 ? 1 : 0;
	passHz = std::min(passHz, 0.48 * baseRate);
	for (int s = 0; s < m_stages; ++s) {
		// Stage s runs at baseRate * 2^(s+1). The band up to passHz must pass, and its image
		// around the low rate's Nyquist must be stopped.
		const double high = baseRate * double(2 << s);
		const double transition = 0.25 - passHz / high;
		const auto c = HalfBand::design(s == 0 ? 110.0 : 100.0, transition);
		m_up[size_t(s)].setCoefs(c);
		m_down[size_t(s)].setCoefs(c);
	}
	reset();
}

void Oversampler::reset()
{
	for (int s = 0; s < m_stages; ++s) {
		m_up[size_t(s)].reset();
		m_down[size_t(s)].reset();
	}
}

void Oversampler::up(double x, double *out)
{
	double a[8], b[8];
	a[0] = x;
	double *src = a, *dst = b;
	int n = 1;
	for (int s = 0; s < m_stages; ++s) {
		for (int i = 0; i < n; ++i)
			m_up[size_t(s)].up(src[i], dst[2 * i], dst[2 * i + 1]);
		std::swap(src, dst);
		n *= 2;
	}
	for (int i = 0; i < n; ++i)
		out[i] = src[i];
}

double Oversampler::down(double *in)
{
	// In place: each output overwrites a sample that has already been read.
	int n = m_factor;
	for (int s = m_stages - 1; s >= 0; --s) {
		n /= 2;
		for (int i = 0; i < n; ++i)
			in[i] = m_down[size_t(s)].down(in[2 * i], in[2 * i + 1]);
	}
	return in[0];
}

} // namespace rat
