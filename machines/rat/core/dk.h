// Nodal DK method: a circuit of resistors, capacitors, voltage sources and nonlinear ports,
// discretised with the trapezoidal rule and reduced to a small nonlinear system per sample
// (D. T. Yeh, "Digital implementation of musical distortion circuits by analysis and
// simulation", Stanford 2009, ch. 6; M. Holters and U. Zölzer, "Physical modelling of a
// wah-wah effect pedal as a case study for application of the nodal DK method", DAFx-11).
//
// Each capacitor becomes a conductance Gc = 2C/T in parallel with a history current z (the
// state). Nonlinear elements are described by the voltages they sense (v, from node pairs)
// and the currents they inject (i, into node pairs); the circuit-specific code supplies
// i = g(v) and its Jacobian. Per sample, with u the source values:
//
//     v = D z + E u + F i(v)      (solved by Newton in v)
//     y = Do z + Eo u + Fo i      (outputs: node voltage differences)
//     z <- A z + B u + C i        (capacitor history currents)
//
// The matrices depend on the resistances, so a pot move means a recompile (a dense inverse
// of the MNA matrix: tens of microseconds for this size).
#pragma once

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace rat {

// Row-major dense matrix used while compiling.
struct Mat {
	int rows = 0, cols = 0;
	std::vector<double> a;

	Mat() = default;
	Mat(int r, int c) : rows(r), cols(c), a(size_t(r) * size_t(c), 0.0) {}
	double &operator()(int r, int c) { return a[size_t(r) * size_t(cols) + size_t(c)]; }
	double operator()(int r, int c) const { return a[size_t(r) * size_t(cols) + size_t(c)]; }
};

// r = x * y; r is resized only if its shape differs (no allocation once sized).
void multiply(Mat &r, const Mat &x, const Mat &y);
// In place, Gauss-Jordan with partial pivoting, using work (resized as needed). False if
// singular.
bool invert(Mat &m, Mat &work);

class Netlist {
public:
	// Nodes are named; "0" is ground. Each call returns the element's index in its own list.
	int resistor(const std::string &a, const std::string &b, double ohms);
	int capacitor(const std::string &a, const std::string &b, double farads);
	// Independent voltage source V(a) - V(b) = u[k], k = the returned index.
	int source(const std::string &a, const std::string &b);
	// Nonlinear sensing voltage v[k] = V(a) - V(b).
	int sense(const std::string &a, const std::string &b);
	// Nonlinear current i[k], injected into a and drawn from b.
	int inject(const std::string &a, const std::string &b);
	// Output y[k] = V(a) - V(b).
	int output(const std::string &a, const std::string &b);

	void setResistance(int index, double ohms) { m_res[size_t(index)].value = ohms; }
	double resistance(int index) const { return m_res[size_t(index)].value; }
	double capacitance(int index) const { return m_cap[size_t(index)].value; }
	void setCapacitance(int index, double farads) { m_cap[size_t(index)].value = farads; }

	int nodeCount() const { return int(m_nodes.size()); } // excluding ground
	int nodeIndex(const std::string &name) const;          // -1 for ground or unknown
	const std::string &nodeName(int i) const { return m_nodes[size_t(i)]; }

	struct Branch {
		int a, b;     // node indices, -1 = ground
		double value; // ohms or farads
	};
	struct Pair {
		int a, b;
	};

	const std::vector<Branch> &resistors() const { return m_res; }
	const std::vector<Branch> &capacitors() const { return m_cap; }
	const std::vector<Pair> &sources() const { return m_src; }
	const std::vector<Pair> &senses() const { return m_sense; }
	const std::vector<Pair> &injections() const { return m_inject; }
	const std::vector<Pair> &outputs() const { return m_out; }

	// Conductance from every node to ground, as SPICE's GMIN: keeps nodes that only touch
	// capacitors or reverse-biased junctions defined at DC.
	double gmin = 1e-12;

private:
	int node(const std::string &name);

	std::vector<std::string> m_nodes;
	std::vector<Branch> m_res, m_cap;
	std::vector<Pair> m_src, m_sense, m_inject, m_out;
};

// Fixed upper bounds keep the per-sample code free of allocation.
constexpr int kMaxStates = 24;
constexpr int kMaxSources = 4;
constexpr int kMaxV = 8;
constexpr int kMaxI = 8;
constexpr int kMaxOut = 8;

// The compiled system for one time step (or DC, with capacitors open).
struct DKMatrices {
	int nz = 0, nu = 0, nv = 0, ni = 0, ny = 0;
	double A[kMaxStates][kMaxStates]{}, B[kMaxStates][kMaxSources]{}, C[kMaxStates][kMaxI]{};
	double D[kMaxV][kMaxStates]{}, E[kMaxV][kMaxSources]{}, F[kMaxV][kMaxI]{};
	double Do[kMaxOut][kMaxStates]{}, Eo[kMaxOut][kMaxSources]{}, Fo[kMaxOut][kMaxI]{};
	// Capacitor voltages from (z, u, i), for setting the states from a DC solution.
	double Pz[kMaxStates][kMaxStates]{}, Pu[kMaxStates][kMaxSources]{}, Pi[kMaxStates][kMaxI]{};
	double Gc[kMaxStates]{};
	bool ok = false;
};

// Builds DKMatrices from a netlist. T > 0: trapezoidal step of T seconds. T = 0: DC
// (capacitors open, z ignored). Keeps its work matrices, so recompiling the same netlist
// after a resistance change does not allocate.
class DKCompiler {
public:
	bool compile(const Netlist &net, double T, DKMatrices &out);

private:
	Mat m_s, m_work, m_sz, m_su, m_si;
};

// Newton solve of v = p + F g(v). NL provides
//   void eval(const double *v, double *i, double (*G)[kMaxV]) const  -- i = g(v), G = di/dv
//   void limit(const double *v, double *dv) const                    -- clamp a Newton step
// v holds the starting guess and returns the solution; i returns g(v). Returns the iteration
// count, or -1 if it did not converge (v and i then hold the last iterate).
template <class NL>
int solveNewton(const DKMatrices &m, const double *p, double *v, double *i, const NL &nl,
	int maxIter = 60, double tol = 1e-10)
{
	const int nv = m.nv, ni = m.ni;
	double G[kMaxI][kMaxV];
	double J[kMaxV][kMaxV + 1];
	for (int it = 0; it < maxIter; ++it) {
		nl.eval(v, i, G);
		// Residual r = v - p - F i, Jacobian I - F G.
		for (int r = 0; r < nv; ++r) {
			double fi = 0;
			for (int k = 0; k < ni; ++k)
				fi += m.F[r][k] * i[k];
			J[r][nv] = -(v[r] - p[r] - fi);
			for (int c = 0; c < nv; ++c) {
				double fg = 0;
				for (int k = 0; k < ni; ++k)
					fg += m.F[r][k] * G[k][c];
				J[r][c] = (r == c ? 1.0 : 0.0) - fg;
			}
		}
		// Gaussian elimination with partial pivoting on the augmented matrix.
		for (int c = 0; c < nv; ++c) {
			int piv = c;
			for (int r = c + 1; r < nv; ++r)
				if (std::fabs(J[r][c]) > std::fabs(J[piv][c]))
					piv = r;
			if (piv != c)
				for (int k = c; k <= nv; ++k)
					std::swap(J[c][k], J[piv][k]);
			const double d = J[c][c];
			if (d == 0)
				return -1;
			for (int r = c + 1; r < nv; ++r) {
				const double f = J[r][c] / d;
				if (f != 0)
					for (int k = c; k <= nv; ++k)
						J[r][k] -= f * J[c][k];
			}
		}
		double dv[kMaxV];
		for (int r = nv - 1; r >= 0; --r) {
			double s = J[r][nv];
			for (int k = r + 1; k < nv; ++k)
				s -= J[r][k] * dv[k];
			dv[r] = s / J[r][r];
		}
		nl.limit(v, dv);
		double big = 0;
		for (int r = 0; r < nv; ++r) {
			v[r] += dv[r];
			big = std::fmax(big, std::fabs(dv[r]));
		}
		if (!(big == big))
			return -1;
		if (big < tol) {
			nl.eval(v, i, G);
			return it + 1;
		}
	}
	nl.eval(v, i, G);
	return -1;
}

// The same Newton iteration for a fixed-size block, used per sample: v = p + F g(v) with
// NV sensed voltages and NI currents. eval(v, i, G) fills i and G[NI][NV]; limit(v, dv)
// clamps the step. On convergence i is corrected to first order for the last step instead
// of a final evaluation. One iterate() call is one iteration, so the solves of two channels
// can be interleaved iteration by iteration (their arithmetic is the same as one at a time).
template <int NV, int NI>
struct NewtonRun {
	int it = 0;
	int result = 0; // while running 0; then the iteration count, or -1 if it did not converge

	// Solves J dv = b, b in column NV; false if J is singular. 2 x 2 and 3 x 3 systems (the JFET,
	// the op amp and diodes, every sample) are solved in closed form, as the adjugate over the
	// determinant: no pivoting and one division, so the chain of dependent operations is short.
	// The solution is elimination's to rounding, and Newton's iterates and iteration counts stay
	// the same.
	static bool solve(double (&J)[NV][NV + 1], double *dv)
	{
		if constexpr (NV == 2) {
			const double det = J[0][0] * J[1][1] - J[0][1] * J[1][0];
			if (det == 0)
				return false;
			const double inv = 1.0 / det;
			dv[0] = (J[1][1] * J[0][2] - J[0][1] * J[1][2]) * inv;
			dv[1] = (J[0][0] * J[1][2] - J[1][0] * J[0][2]) * inv;
			return true;
		}
		if constexpr (NV == 3) {
			const double c00 = J[1][1] * J[2][2] - J[1][2] * J[2][1];
			const double c01 = J[1][2] * J[2][0] - J[1][0] * J[2][2];
			const double c02 = J[1][0] * J[2][1] - J[1][1] * J[2][0];
			const double det = J[0][0] * c00 + J[0][1] * c01 + J[0][2] * c02;
			if (det == 0)
				return false;
			const double inv = 1.0 / det, b0 = J[0][3], b1 = J[1][3], b2 = J[2][3];
			dv[0] = (c00 * b0 + (J[0][2] * J[2][1] - J[0][1] * J[2][2]) * b1 + (J[0][1] * J[1][2] - J[0][2] * J[1][1]) * b2) * inv;
			dv[1] = (c01 * b0 + (J[0][0] * J[2][2] - J[0][2] * J[2][0]) * b1 + (J[0][2] * J[1][0] - J[0][0] * J[1][2]) * b2) * inv;
			dv[2] = (c02 * b0 + (J[0][1] * J[2][0] - J[0][0] * J[2][1]) * b1 + (J[0][0] * J[1][1] - J[0][1] * J[1][0]) * b2) * inv;
			return true;
		}
		// Gaussian elimination with partial pivoting on the augmented matrix.
		for (int c = 0; c < NV; ++c) {
			int piv = c;
			for (int r = c + 1; r < NV; ++r)
				if (std::fabs(J[r][c]) > std::fabs(J[piv][c]))
					piv = r;
			if (piv != c)
				for (int k = c; k <= NV; ++k)
					std::swap(J[c][k], J[piv][k]);
			const double d = J[c][c];
			if (d == 0)
				return false;
			for (int r = c + 1; r < NV; ++r) {
				const double f = J[r][c] / d;
				for (int k = c; k <= NV; ++k)
					J[r][k] -= f * J[c][k];
			}
		}
		for (int r = NV - 1; r >= 0; --r) {
			double s = J[r][NV];
			for (int k = r + 1; k < NV; ++k)
				s -= J[r][k] * dv[k];
			dv[r] = s / J[r][r];
		}
		return true;
	}

	// One iteration; true when finished (see result).
	// Not inlined: one compiled copy serves channels solved alone and in pairs, so the compiler
	// cannot fuse multiply-adds differently in the two and make them round differently.
	template <class Eval, class Limit>
	__attribute__((noinline)) bool iterate(const double (&F)[NV][NI], const double *p, double *v, double *i,
		Eval &&eval, Limit &&limit, int maxIter, double tol)
	{
		if (it >= maxIter) {
			result = -1;
			return true;
		}
		double G[NI][NV];
		eval(v, i, G);
		double J[NV][NV + 1];
		for (int r = 0; r < NV; ++r) {
			double fi = 0;
			for (int k = 0; k < NI; ++k)
				fi += F[r][k] * i[k];
			J[r][NV] = p[r] + fi - v[r];
			for (int c = 0; c < NV; ++c) {
				double fg = 0;
				for (int k = 0; k < NI; ++k)
					fg += F[r][k] * G[k][c];
				J[r][c] = (r == c ? 1.0 : 0.0) - fg;
			}
		}
		double dv[NV];
		if (!solve(J, dv)) {
			result = -1;
			return true;
		}
		limit(v, dv);
		double big = 0;
		for (int r = 0; r < NV; ++r) {
			v[r] += dv[r];
			big = std::fmax(big, std::fabs(dv[r]));
		}
		++it;
		if (!(big == big)) {
			result = -1;
			return true;
		}
		if (big < tol) {
			for (int k = 0; k < NI; ++k)
				for (int c = 0; c < NV; ++c)
					i[k] += G[k][c] * dv[c];
			result = it;
			return true;
		}
		return false;
	}
};

template <int NV, int NI, class Eval, class Limit>
int newtonBlock(const double (&F)[NV][NI], const double *p, double *v, double *i, Eval &&eval,
	Limit &&limit, int maxIter, double tol)
{
	NewtonRun<NV, NI> run;
	while (!run.iterate(F, p, v, i, eval, limit, maxIter, tol)) {
	}
	return run.result;
}

} // namespace rat
