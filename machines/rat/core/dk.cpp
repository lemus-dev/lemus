#include "dk.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace rat {

namespace {

void shape(Mat &m, int rows, int cols)
{
	if (m.rows != rows || m.cols != cols) {
		m.rows = rows;
		m.cols = cols;
		m.a.assign(size_t(rows) * size_t(cols), 0.0);
	} else {
		std::fill(m.a.begin(), m.a.end(), 0.0);
	}
}

} // namespace

void multiply(Mat &r, const Mat &x, const Mat &y)
{
	shape(r, x.rows, y.cols);
	for (int i = 0; i < x.rows; ++i)
		for (int k = 0; k < x.cols; ++k) {
			const double a = x(i, k);
			if (a == 0)
				continue;
			for (int j = 0; j < y.cols; ++j)
				r(i, j) += a * y(k, j);
		}
}

bool invert(Mat &m, Mat &inv)
{
	const int n = m.rows;
	shape(inv, n, n);
	for (int i = 0; i < n; ++i)
		inv(i, i) = 1;
	// The columns where each row of inv can be nonzero (it starts as the identity), so the row
	// operations skip the rest, which only ever subtract zeros: the same result.
	uint64_t cols[64];
	if (n > 64)
		return false;
	for (int i = 0; i < n; ++i)
		cols[i] = uint64_t(1) << i;
	for (int c = 0; c < n; ++c) {
		int piv = c;
		for (int r = c + 1; r < n; ++r)
			if (std::fabs(m(r, c)) > std::fabs(m(piv, c)))
				piv = r;
		if (m(piv, c) == 0)
			return false;
		if (piv != c) {
			for (int k = 0; k < n; ++k) {
				std::swap(m(c, k), m(piv, k));
				std::swap(inv(c, k), inv(piv, k));
			}
			std::swap(cols[c], cols[piv]);
		}
		int nz[64], count = 0;
		for (int k = 0; k < n; ++k)
			if (cols[c] >> k & 1)
				nz[count++] = k;
		const double d = 1.0 / m(c, c);
		for (int k = 0; k < n; ++k)
			m(c, k) *= d;
		for (int t = 0; t < count; ++t)
			inv(c, nz[t]) *= d;
		for (int r = 0; r < n; ++r) {
			if (r == c)
				continue;
			const double f = m(r, c);
			if (f == 0)
				continue;
			for (int k = 0; k < n; ++k)
				m(r, k) -= f * m(c, k);
			for (int t = 0; t < count; ++t)
				inv(r, nz[t]) -= f * inv(c, nz[t]);
			cols[r] |= cols[c];
		}
	}
	std::swap(m.a, inv.a);
	return true;
}

int Netlist::node(const std::string &name)
{
	if (name == "0" || name == "gnd")
		return -1;
	const auto it = std::find(m_nodes.begin(), m_nodes.end(), name);
	if (it != m_nodes.end())
		return int(it - m_nodes.begin());
	m_nodes.push_back(name);
	return int(m_nodes.size()) - 1;
}

int Netlist::nodeIndex(const std::string &name) const
{
	const auto it = std::find(m_nodes.begin(), m_nodes.end(), name);
	return it == m_nodes.end() ? -1 : int(it - m_nodes.begin());
}

int Netlist::resistor(const std::string &a, const std::string &b, double ohms)
{
	m_res.push_back({node(a), node(b), ohms});
	return int(m_res.size()) - 1;
}

int Netlist::capacitor(const std::string &a, const std::string &b, double farads)
{
	m_cap.push_back({node(a), node(b), farads});
	return int(m_cap.size()) - 1;
}

int Netlist::source(const std::string &a, const std::string &b)
{
	m_src.push_back({node(a), node(b)});
	return int(m_src.size()) - 1;
}

int Netlist::sense(const std::string &a, const std::string &b)
{
	m_sense.push_back({node(a), node(b)});
	return int(m_sense.size()) - 1;
}

int Netlist::inject(const std::string &a, const std::string &b)
{
	m_inject.push_back({node(a), node(b)});
	return int(m_inject.size()) - 1;
}

int Netlist::output(const std::string &a, const std::string &b)
{
	m_out.push_back({node(a), node(b)});
	return int(m_out.size()) - 1;
}

namespace {

void stampConductance(Mat &s, int a, int b, double g)
{
	if (a >= 0)
		s(a, a) += g;
	if (b >= 0)
		s(b, b) += g;
	if (a >= 0 && b >= 0) {
		s(a, b) -= g;
		s(b, a) -= g;
	}
}

// The products with the incidence matrices, read straight off the node pairs. Multiplying by a
// column with +1 at a and -1 at b gives x(i, a) - x(i, b), rounded once, and so does this; ground
// (-1) contributes nothing.

// r(i, k) = x(i, a_k) - x(i, b_k): x times the incidence columns of `pairs`.
template <class Pairs>
void columnsBetween(Mat &r, const Mat &x, const Pairs &pairs)
{
	shape(r, x.rows, int(pairs.size()));
	for (size_t k = 0; k < pairs.size(); ++k) {
		const int a = pairs[k].a, b = pairs[k].b;
		for (int i = 0; i < x.rows; ++i)
			r(i, int(k)) = a >= 0 && b >= 0 ? x(i, a) - x(i, b) : a >= 0 ? x(i, a) : b >= 0 ? -x(i, b) : 0.0;
	}
}

// dst(k, j) = x(a_k, j) - x(b_k, j): the rows of `pairs` (V(a) - V(b)) times x.
template <class Pairs, size_t R, size_t C>
void rowsBetween(double (&dst)[R][C], const Mat &x, const Pairs &pairs)
{
	for (size_t k = 0; k < pairs.size(); ++k) {
		const int a = pairs[k].a, b = pairs[k].b;
		for (int j = 0; j < x.cols; ++j)
			dst[k][j] = a >= 0 && b >= 0 ? x(a, j) - x(b, j) : a >= 0 ? x(a, j) : b >= 0 ? -x(b, j) : 0.0;
	}
}

} // namespace

bool DKCompiler::compile(const Netlist &net, double T, DKMatrices &out)
{
	const int n = net.nodeCount();
	const int ns = int(net.sources().size());
	const int dim = n + ns;
	const int nz = int(net.capacitors().size());
	out.ok = false;
	out.nz = nz;
	out.nu = ns;
	out.nv = int(net.senses().size());
	out.ni = int(net.injections().size());
	out.ny = int(net.outputs().size());
	if (nz > kMaxStates || ns > kMaxSources || out.nv > kMaxV || out.ni > kMaxI || out.ny > kMaxOut)
		throw std::runtime_error("DK netlist exceeds the fixed size limits");

	// The MNA matrix: node conductances plus one row and column per voltage source.
	shape(m_s, dim, dim);
	for (int i = 0; i < n; ++i)
		m_s(i, i) += net.gmin;
	for (const auto &r : net.resistors())
		stampConductance(m_s, r.a, r.b, 1.0 / r.value);
	for (int k = 0; k < nz; ++k) {
		const auto &c = net.capacitors()[size_t(k)];
		out.Gc[k] = T > 0 ? 2.0 * c.value / T : 0.0;
		stampConductance(m_s, c.a, c.b, out.Gc[k]);
	}
	for (int k = 0; k < ns; ++k) {
		const auto &v = net.sources()[size_t(k)];
		if (v.a >= 0) {
			m_s(v.a, n + k) += 1;
			m_s(n + k, v.a) += 1;
		}
		if (v.b >= 0) {
			m_s(v.b, n + k) -= 1;
			m_s(n + k, v.b) -= 1;
		}
	}
	if (!invert(m_s, m_work))
		return false;

	// S^-1 times the capacitor, source and injection incidences, then the sensed, output and
	// capacitor voltages of those.
	columnsBetween(m_sz, m_s, net.capacitors());
	columnsBetween(m_si, m_s, net.injections());
	shape(m_su, dim, ns);
	for (int i = 0; i < dim; ++i)
		for (int k = 0; k < ns; ++k)
			m_su(i, k) = m_s(i, n + k);
	rowsBetween(out.D, m_sz, net.senses());
	rowsBetween(out.E, m_su, net.senses());
	rowsBetween(out.F, m_si, net.senses());
	rowsBetween(out.Do, m_sz, net.outputs());
	rowsBetween(out.Eo, m_su, net.outputs());
	rowsBetween(out.Fo, m_si, net.outputs());
	rowsBetween(out.Pz, m_sz, net.capacitors());
	rowsBetween(out.Pu, m_su, net.capacitors());
	rowsBetween(out.Pi, m_si, net.capacitors());
	// z' = Gc v_c + i_c = 2 Gc v_c - z.
	for (int r = 0; r < nz; ++r) {
		const double g2 = 2.0 * out.Gc[r];
		for (int c = 0; c < nz; ++c)
			out.A[r][c] = g2 * out.Pz[r][c] - (r == c ? 1.0 : 0.0);
		for (int c = 0; c < ns; ++c)
			out.B[r][c] = g2 * out.Pu[r][c];
		for (int c = 0; c < out.ni; ++c)
			out.C[r][c] = g2 * out.Pi[r][c];
	}
	out.ok = true;
	return true;
}

} // namespace rat
