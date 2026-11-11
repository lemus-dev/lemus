#include "circuit.h"

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#else
#include <emmintrin.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace tr808::circuit {

namespace {

constexpr double kVt = 0.025864186; // kT/q at 27 C, as SPICE's nominal temperature
constexpr double kGmin = 1e-12;     // across every junction, as SPICE adds it
constexpr double kGshunt = 1e-9;    // from every node to ground (see build)
// Linear mode (Circuit::linearize): kLinTolerance is the voltage error a junction's linearisation
// may make at its port before the step goes back to Newton; kLinWindow the window of the ports
// with no better estimate (JFETs, OTA inputs, a sinking comparator's output); kOffCurrent the
// current under which a JFET's gate or an OTA's control counts as off.
const double kLinWindow = std::getenv("TR808_LINWINDOW") ? std::atof(std::getenv("TR808_LINWINDOW")) : 2e-3;
const double kLinTolerance = std::getenv("TR808_LINTOL") ? std::atof(std::getenv("TR808_LINTOL")) : 1e-5; // volts
const bool kLinOff = std::getenv("TR808_NOLINEAR") != nullptr;
// Tools' diagnostics, read once (not behind a function-local static's guard on every call).
const bool kCheckJac = std::getenv("TR808_CHECKJAC") != nullptr;
constexpr int kDensePorts = 16; // Newton's residual over all currents up to this many ports
const int kTrace = std::getenv("TR808_TRACE") ? std::atoi(std::getenv("TR808_TRACE")) : -1;
const bool kNyquist = std::getenv("TR808_NYQ") != nullptr;
const bool kFailDebug = std::getenv("TR808_FAILDEBUG") != nullptr;
const bool kLinDebug = std::getenv("TR808_LINDEBUG") != nullptr;
const bool kNoJump = std::getenv("TR808_NOJUMP") != nullptr; // tools: edges as ramps across their sample
constexpr double kOffCurrent = 1e-10;
constexpr double kLimiterKnee = 1.5; // volts: an op-amp output's soft clipping starts this far below vsat
// Anti-windup: past vsat + kWindupMargin (in output volts) the limiter draws kWindupG amps a volt
// from the op-amp's internal pole node (softly, over kWindupSoft), as a real op-amp's internal
// stages saturate: a clipped op-amp comes back within a sample instead of after its integrator
// unwinds (125 us for the narrowed trig at accent).
constexpr double kWindupMargin = 2.5, kWindupSoft = 0.05, kWindupG = 1e-5;
// Small systems factor afresh every iteration (plain Newton): it costs less than the chord
// method's extra iterations.
const int kPlainPorts = std::getenv("TR808_PLAIN") ? std::atoi(std::getenv("TR808_PLAIN")) : 6;
const double kChordRatio = std::getenv("TR808_CHORD") ? std::atof(std::getenv("TR808_CHORD")) : 0.1;

std::string lower(std::string s)
{
	for (char &c : s)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::vector<std::string> split(const std::string &line)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : line) {
		if (std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == ',') {
			if (!cur.empty())
				out.push_back(cur);
			cur.clear();
		} else {
			cur += c;
		}
	}
	if (!cur.empty())
		out.push_back(cur);
	return out;
}

std::string fmt(double v)
{
	char buf[40];
	std::snprintf(buf, sizeof(buf), "%.12g", v);
	return buf;
}

// Two doubles at a time, and the fused multiply-add the compiler contracts a plain loop's a * x + s
// to where the machine has one: the vector loops below give each element the same arithmetic.
#if defined(__aarch64__) || defined(_M_ARM64)
using Pair = float64x2_t;
[[gnu::always_inline]] inline Pair pairLoad(const double *p) { return vld1q_f64(p); }
[[gnu::always_inline]] inline void pairStore(double *p, Pair v) { vst1q_f64(p, v); }
[[gnu::always_inline]] inline Pair pairDup(double x) { return vdupq_n_f64(x); }
[[gnu::always_inline]] inline Pair pairMadd(Pair s, Pair a, Pair x) { return vfmaq_f64(s, a, x); }
[[gnu::always_inline]] inline Pair pairMsub(Pair s, Pair a, Pair x) { return vfmsq_f64(s, a, x); }
[[gnu::always_inline]] inline double msub(double s, double a, double x) { return __builtin_fma(-a, x, s); }
#else
using Pair = __m128d;
[[gnu::always_inline]] inline Pair pairLoad(const double *p) { return _mm_loadu_pd(p); }
[[gnu::always_inline]] inline void pairStore(double *p, Pair v) { _mm_storeu_pd(p, v); }
[[gnu::always_inline]] inline Pair pairDup(double x) { return _mm_set1_pd(x); }
[[gnu::always_inline]] inline Pair pairMadd(Pair s, Pair a, Pair x) { return _mm_add_pd(s, _mm_mul_pd(a, x)); }
[[gnu::always_inline]] inline Pair pairMsub(Pair s, Pair a, Pair x) { return _mm_sub_pd(s, _mm_mul_pd(a, x)); }
[[gnu::always_inline]] inline double msub(double s, double a, double x) { return s - a * x; }
#endif

// The largest |x[k]| (x finite), two at a time.
[[gnu::always_inline]] inline double maxAbs(const double *x, int n)
{
#if defined(__aarch64__) || defined(_M_ARM64)
	float64x2_t m = vdupq_n_f64(0);
	int k = 0;
	for (; k + 2 <= n; k += 2)
		m = vmaxq_f64(m, vabsq_f64(vld1q_f64(x + k)));
	double big = vmaxvq_f64(m);
	if (k < n)
		big = std::max(big, std::abs(x[k]));
	return big;
#else
	double big = 0;
	for (int k = 0; k < n; ++k)
		big = std::max(big, std::abs(x[k]));
	return big;
#endif
}

// LU decomposition with partial pivoting of the n x n row-major matrix `a`, in place. The small
// sizes Newton meets every step (a VCA's 2-6 active ports) are compiled for their size: the same
// operations in the same order, unrolled.
template <int N> bool luDecomposeN(double *a, int *piv);
template <int N> void luSolveN(const double *lu, const int *piv, double *b);
bool luDecomposeAny(double *a, int n, int *piv);
void luSolveAny(const double *lu, int n, const int *piv, double *b);

bool luDecompose(double *a, int n, int *piv)
{
	switch (n) {
	case 1: return luDecomposeN<1>(a, piv);
	case 2: return luDecomposeN<2>(a, piv);
	case 3: return luDecomposeN<3>(a, piv);
	case 4: return luDecomposeN<4>(a, piv);
	case 5: return luDecomposeN<5>(a, piv);
	case 6: return luDecomposeN<6>(a, piv);
	case 7: return luDecomposeN<7>(a, piv);
	case 8: return luDecomposeN<8>(a, piv);
	case 9: return luDecomposeN<9>(a, piv);
	case 10: return luDecomposeN<10>(a, piv);
	case 11: return luDecomposeN<11>(a, piv);
	case 12: return luDecomposeN<12>(a, piv);
	case 13: return luDecomposeN<13>(a, piv);
	case 14: return luDecomposeN<14>(a, piv);
	case 15: return luDecomposeN<15>(a, piv);
	case 16: return luDecomposeN<16>(a, piv);
	default: return luDecomposeAny(a, n, piv);
	}
}

void luSolve(const double *lu, int n, const int *piv, double *b)
{
	switch (n) {
	case 1: return luSolveN<1>(lu, piv, b);
	case 2: return luSolveN<2>(lu, piv, b);
	case 3: return luSolveN<3>(lu, piv, b);
	case 4: return luSolveN<4>(lu, piv, b);
	case 5: return luSolveN<5>(lu, piv, b);
	case 6: return luSolveN<6>(lu, piv, b);
	case 7: return luSolveN<7>(lu, piv, b);
	case 8: return luSolveN<8>(lu, piv, b);
	case 9: return luSolveN<9>(lu, piv, b);
	case 10: return luSolveN<10>(lu, piv, b);
	case 11: return luSolveN<11>(lu, piv, b);
	case 12: return luSolveN<12>(lu, piv, b);
	case 13: return luSolveN<13>(lu, piv, b);
	case 14: return luSolveN<14>(lu, piv, b);
	case 15: return luSolveN<15>(lu, piv, b);
	case 16: return luSolveN<16>(lu, piv, b);
	default: return luSolveAny(lu, n, piv, b);
	}
}

template <int N> bool luDecomposeN(double *a, int *piv)
{
	constexpr int n = N;
#pragma clang loop unroll(full)
	for (int k = 0; k < n; ++k) {
		int p = k;
		double best = std::abs(a[k * n + k]);
#pragma clang loop unroll(full)
		for (int r = k + 1; r < n; ++r)
			if (std::abs(a[r * n + k]) > best) {
				best = std::abs(a[r * n + k]);
				p = r;
			}
		piv[k] = p;
		if (best == 0)
			return false;
		if (p != k)
			for (int c = 0; c < n; ++c)
				std::swap(a[k * n + c], a[p * n + c]);
		const double inv = 1.0 / a[k * n + k];
		const double *__restrict pk = a + k * n;
#pragma clang loop unroll(full)
		for (int r = k + 1; r < n; ++r) {
			double *__restrict ar = a + r * n;
			const double f = ar[k] * inv;
			ar[k] = f;
			if (f == 0)
				continue;
			// a[r][c] -= f a[k][c], two columns at a time (each a fused multiply-subtract, as the
			// plain loop's contraction)
			const Pair fp = pairDup(f);
			int c = k + 1;
			for (; c + 2 <= n; c += 2)
				pairStore(ar + c, pairMsub(pairLoad(ar + c), pairLoad(pk + c), fp));
			if (c < n)
				ar[c] = msub(ar[c], pk[c], f);
		}
	}
	return true;
}

template <int N> void luSolveN(const double *lu, const int *piv, double *b)
{
	constexpr int n = N;
	for (int k = 0; k < n; ++k)
		if (piv[k] != k)
			std::swap(b[k], b[piv[k]]);
	for (int r = 1; r < n; ++r) {
		double s = b[r];
		for (int c = 0; c < r; ++c)
			s -= lu[r * n + c] * b[c];
		b[r] = s;
	}
	for (int r = n - 1; r >= 0; --r) {
		double s = b[r];
		for (int c = r + 1; c < n; ++c)
			s -= lu[r * n + c] * b[c];
		b[r] = s / lu[r * n + r];
	}
}

bool luDecomposeAny(double *a, int n, int *piv)
{
	for (int k = 0; k < n; ++k) {
		int p = k;
		double best = std::abs(a[k * n + k]);
		for (int r = k + 1; r < n; ++r)
			if (std::abs(a[r * n + k]) > best) {
				best = std::abs(a[r * n + k]);
				p = r;
			}
		piv[k] = p;
		if (best == 0)
			return false;
		if (p != k)
			for (int c = 0; c < n; ++c)
				std::swap(a[k * n + c], a[p * n + c]);
		const double inv = 1.0 / a[k * n + k];
		for (int r = k + 1; r < n; ++r) {
			const double f = a[r * n + k] * inv;
			a[r * n + k] = f;
			if (f != 0)
				for (int c = k + 1; c < n; ++c)
					a[r * n + c] -= f * a[k * n + c];
		}
	}
	return true;
}

void luSolveAny(const double *lu, int n, const int *piv, double *b)
{
	for (int k = 0; k < n; ++k)
		if (piv[k] != k)
			std::swap(b[k], b[piv[k]]);
	for (int r = 1; r < n; ++r) {
		double s = b[r];
		for (int c = 0; c < r; ++c)
			s -= lu[r * n + c] * b[c];
		b[r] = s;
	}
	for (int r = n - 1; r >= 0; --r) {
		double s = b[r];
		for (int c = r + 1; c < n; ++c)
			s -= lu[r * n + c] * b[c];
		b[r] = s / lu[r * n + r];
	}
}

// A copy of the few doubles a step moves about (5-30): inline (circuit.cpp is built without the
// memcpy/memmove builtins, so the compiler doesn't make such loops library calls).
[[gnu::always_inline]] inline void copyN(const double *__restrict from, int n, double *__restrict to)
{
	for (int k = 0; k < n; ++k)
		to[k] = from[k];
}

// SPICE's pnjlim: keeps Newton from overshooting up an exponential.
double limitJunction(double vnew, double vold, double vt, double vcrit)
{
	// A conducting junction doesn't drop far into reverse in one step either: going from on to
	// off it stops a little below zero first (the off solution is then found from there).
	if (vold > 0.5 * vcrit && vnew < -1)
		return -1;
	if (vnew > vcrit && std::abs(vnew - vold) > 2 * vt) {
		if (vold > 0) {
			const double arg = 1 + (vnew - vold) / vt;
			vnew = arg > 0 ? vold + vt * std::log(arg) : vcrit;
		} else {
			vnew = vt * std::log(vnew / vt);
		}
	}
	return vnew;
}

// out[r] = (acc ? out[r] : c[r]) + A[r][:cols] x for rows r0 .. r0 + rows - 1 of a ColMat (out and c
// from those rows' first), down the columns two rows at a time, up to 16 rows a pass. Each row's
// sum is in the order of a plain loop, with a fused multiply-add at each term where the machine
// has one (as the compiler contracts the plain loop's).
// The columns [k0, k0 + cols) of a block of NA row pairs times x, into the sums s.
template <int NA, bool sub>
[[gnu::always_inline]] inline void colPart(Pair *s, const double *__restrict a, int ld, int k0, const double *__restrict x, int cols)
{
	for (int k = 0; k < cols; ++k) {
		const Pair xk = pairDup(x[k]);
		const double *ak = a + size_t(k0 + k) * size_t(ld);
		for (int j = 0; j < NA; ++j)
			s[j] = sub ? pairMsub(s[j], pairLoad(ak + 2 * j), xk) : pairMadd(s[j], pairLoad(ak + 2 * j), xk);
	}
}

template <int NA, bool acc, bool sub>
[[gnu::always_inline]] inline void colBlock(const double *__restrict a, int ld, const double *__restrict c, const double *__restrict x,
                                            double *__restrict out, int cols, const double *__restrict x2, int cols2,
                                            const double *__restrict x3, int cols3)
{
	Pair s[NA];
	for (int j = 0; j < NA; ++j)
		s[j] = pairLoad((acc ? out : c) + 2 * j);
	colPart<NA, sub>(s, a, ld, 0, x, cols);
	colPart<NA, sub>(s, a, ld, cols, x2, cols2);
	colPart<NA, sub>(s, a, ld, cols + cols2, x3, cols3);
	for (int j = 0; j < NA; ++j)
		pairStore(out + 2 * j, s[j]);
}

// (sub: out[r] = c[r] - A[r][:cols] x, each term subtracted in turn.) colMatVec3: x in three parts,
// the first `cols` columns' x, the next `cols2` columns' x2, then `cols3` columns' x3 (a step's
// states, inputs and currents, read where they are kept). The rows run to A's last (r0 + rows ==
// A.rows): an odd last row goes with A's padding row as a pair, so c and out have room for one
// more element there (`padded`); that element of out is scratch. (A row on its own would be one
// chain of dependent multiply-adds; in a pair it runs alongside the others, the same arithmetic.)
template <bool acc, bool sub = false>
[[gnu::always_inline]] inline void colMatVec3(const ColMat &A, int r0, int rows, int cols, const double *__restrict x, int cols2, const double *__restrict x2,
                int cols3, const double *__restrict x3, const double *__restrict c, double *__restrict out)
{
	const int ld = A.ld;
	const double *a = A.d.data() + r0;
	int r = 0;
	for (; r + 16 <= rows; r += 16)
		colBlock<8, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2, x3, cols3);
	switch ((rows - r + 1) / 2) {
	case 1: colBlock<1, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2, x3, cols3); break;
	case 2: colBlock<2, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2, x3, cols3); break;
	case 3: colBlock<3, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2, x3, cols3); break;
	case 4: colBlock<4, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2, x3, cols3); break;
	case 5: colBlock<5, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2, x3, cols3); break;
	case 6: colBlock<6, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2, x3, cols3); break;
	case 7: colBlock<7, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2, x3, cols3); break;
	case 8: colBlock<8, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2, x3, cols3); break;
	default: break;
	}
}

// n rounded up to even: the length of a vector colMatVec3 reads or writes as c or out.
constexpr size_t padded(size_t n) { return n + (n & 1); }

template <bool acc, bool sub = false>
[[gnu::always_inline]] inline void colMatVec2(const ColMat &A, int r0, int rows, int cols, const double *__restrict x, int cols2, const double *__restrict x2,
                const double *__restrict c, double *__restrict out)
{
	colMatVec3<acc, sub>(A, r0, rows, cols, x, cols2, x2, 0, nullptr, c, out);
}

template <bool acc, bool sub = false>
inline void colMatVec(const ColMat &A, int r0, int rows, int cols, const double *__restrict c, const double *__restrict x, double *__restrict out)
{
	colMatVec3<acc, sub>(A, r0, rows, cols, x, 0, nullptr, 0, nullptr, c, out);
}

// Whether any of x[k] is outside [lo[k], hi[k]], two at a time.
[[gnu::always_inline]] inline bool anyOutside(const double *x, const double *lo, const double *hi, int n)
{
#if defined(__aarch64__) || defined(_M_ARM64)
	uint64x2_t bad = vdupq_n_u64(0);
	int k = 0;
	for (; k + 2 <= n; k += 2) {
		const float64x2_t v = vld1q_f64(x + k);
		bad = vorrq_u64(bad, vorrq_u64(vcltq_f64(v, vld1q_f64(lo + k)), vcgtq_f64(v, vld1q_f64(hi + k))));
	}
	bool any = (vgetq_lane_u64(bad, 0) | vgetq_lane_u64(bad, 1)) != 0;
	if (k < n)
		any = any || x[k] < lo[k] || x[k] > hi[k];
	return any;
#else
	for (int k = 0; k < n; ++k)
		if (x[k] < lo[k] || x[k] > hi[k])
			return true;
	return false;
#endif
}

// A row-major matrix (rows x cols, rows `stride` apart) as a ColMat.
void toColumns(const double *src, int rows, int cols, int stride, ColMat &dst)
{
	dst.assign(rows, cols);
	for (int r = 0; r < rows; ++r)
		for (int k = 0; k < cols; ++k)
			dst(r, k) = src[size_t(r) * size_t(stride) + size_t(k)];
}

// exp() that grows linearly past a bound, so a wild Newton step can't overflow.
inline double safeExp(double a, double &d)
{
	constexpr double kMax = 80;
	if (a > kMax) {
		const double e = std::exp(kMax);
		d = e;
		return e * (1 + (a - kMax));
	}
	// Far into reverse, exp(a) is under half an ulp of the "- 1" every junction term subtracts, so
	// 0 gives the same currents to the bit (and the conductance it adds is 30 decades under gmin).
	if (a < -40) {
		d = 0;
		return 0;
	}
	const double e = std::exp(a);
	d = e;
	return e;
}

} // namespace

double parseValue(const std::string &text)
{
	const std::string s = lower(text);
	size_t pos = 0;
	double v = 0;
	try {
		v = std::stod(s, &pos);
	} catch (...) {
		throw std::runtime_error("not a number: " + text);
	}
	const std::string rest = s.substr(pos);
	if (rest.rfind("meg", 0) == 0)
		return v * 1e6;
	if (rest.rfind("mil", 0) == 0)
		return v * 25.4e-6;
	if (rest.empty())
		return v;
	switch (rest[0]) {
	case 'f': return v * 1e-15;
	case 'p': return v * 1e-12;
	case 'n': return v * 1e-9;
	case 'u': return v * 1e-6;
	case 'm': return v * 1e-3;
	case 'k': return v * 1e3;
	case 'g': return v * 1e9;
	case 't': return v * 1e12;
	default: return v; // unit letters (ohm, f, v) after a bare number
	}
}

double potShare(double position, double taperMid)
{
	const double t = std::clamp(position, 0.0, 1.0);
	if (taperMid == 0.5)
		return t;
	return t < 0.5 ? 2 * t * taperMid : taperMid + (t - 0.5) * 2 * (1 - taperMid);
}

int Netlist::node(const std::string &name) const
{
	const std::string n = lower(name);
	if (n == "0" || n == "gnd")
		return 0;
	for (size_t i = 1; i < nodes.size(); ++i)
		if (nodes[i] == n)
			return int(i);
	return -1;
}

int Netlist::input(const std::string &name) const
{
	for (size_t i = 0; i < inputs.size(); ++i)
		if (inputs[i] == lower(name))
			return int(i);
	return -1;
}

int Netlist::control(const std::string &name) const
{
	for (size_t i = 0; i < controls.size(); ++i)
		if (controls[i] == lower(name))
			return int(i);
	return -1;
}

int Netlist::output(const std::string &name) const
{
	for (size_t i = 0; i < outputs.size(); ++i)
		if (outputs[i].name == lower(name))
			return int(i);
	return -1;
}

std::vector<Netlist::Partition> Netlist::partitions(const std::string &text)
{
	// Lines with continuations joined (comments kept out).
	std::vector<std::string> lines;
	{
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line)) {
			const auto semi = line.find(';');
			if (semi != std::string::npos)
				line.resize(semi);
			size_t start = 0;
			while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])))
				++start;
			line = line.substr(start);
			if (line.empty() || line[0] == '*')
				continue;
			if (line[0] == '+' && !lines.empty())
				lines.back() += " " + line.substr(1);
			else
				lines.push_back(line);
		}
	}
	struct Part {
		std::string name;
		std::vector<std::string> elements, inputs;
		bool sleeps = false;
	};
	std::vector<Part> parts;
	for (const auto &l : lines) {
		const auto t = split(l);
		if (lower(t[0]) != ".part")
			continue;
		if (t.size() < 3)
			throw std::runtime_error("bad .part: " + l);
		Part p;
		p.name = lower(t[1]);
		// in=a,b,c (split() parts it at the commas, so read it from the line)
		std::istringstream words(l);
		std::string w;
		words >> w >> w;
		while (words >> w) {
			w = lower(w);
			if (w.rfind("in=", 0) == 0) {
				std::stringstream ss(w.substr(3));
				std::string n;
				while (std::getline(ss, n, ','))
					if (!n.empty())
						p.inputs.push_back(n);
			} else if (w == "sleep") {
				p.sleeps = true;
			} else {
				p.elements.push_back(w);
			}
		}
		parts.push_back(p);
	}
	if (parts.empty())
		return {};
	// Each element line's nodes, as the parser reads them.
	auto nodesOf = [](const std::vector<std::string> &t) {
		std::vector<std::string> n;
		const char k = char(std::tolower(static_cast<unsigned char>(t[0][0])));
		size_t count = 0;
		switch (k) {
		case 'r': case 'c': case 'd': case 's': case 'v': case 'i': count = 2; break;
		case 'q': case 'j': case 'x': case 'p': count = 3; break;
		case 'e': count = 4; break;
		case 'b': {
			bool comparator = false;
			for (const auto &w : t)
				comparator = comparator || lower(w) == "comparator";
			count = comparator ? 4 : 5;
			break;
		}
		default: break;
		}
		for (size_t i = 1; i <= count && i < t.size(); ++i)
			n.push_back(lower(t[i]));
		return n;
	};
	auto isSource = [](const std::vector<std::string> &t) {
		const char k = char(std::tolower(static_cast<unsigned char>(t[0][0])));
		return k == 'v' || k == 'i';
	};
	std::map<std::string, std::vector<std::string>> lineOf; // element name -> its tokens
	std::vector<std::string> order;
	for (const auto &l : lines) {
		const auto t = split(l);
		if (t[0][0] == '.')
			continue;
		lineOf[lower(t[0])] = t;
		order.push_back(lower(t[0]));
	}
	// every element (but the sources, which go where their nodes are used) in some partition
	{
		std::set<std::string> listed;
		for (const auto &p : parts)
			listed.insert(p.elements.begin(), p.elements.end());
		for (const auto &e : order)
			if (!listed.count(e) && !isSource(lineOf[e]))
				throw std::runtime_error("element " + e + " is in no .part");
	}
	std::vector<Partition> out;
	for (size_t pi = 0; pi < parts.size(); ++pi) {
		const Part &p = parts[pi];
		std::set<std::string> keep(p.elements.begin(), p.elements.end()), touched;
		const std::set<std::string> ins(p.inputs.begin(), p.inputs.end());
		for (const auto &e : p.elements) {
			const auto it = lineOf.find(e);
			if (it == lineOf.end())
				throw std::runtime_error(".part " + p.name + ": no element " + e);
			for (const auto &n : nodesOf(it->second))
				touched.insert(n);
		}
		std::ostringstream o;
		o << "* partition " << p.name << "\n";
		for (const auto &l : lines) {
			const auto t = split(l);
			const std::string head = lower(t[0]);
			if (head == ".part")
				continue;
			if (head == ".output") {
				// kept if its nodes are this partition's own
				bool ok = true;
				for (size_t k = 2; k < t.size(); ++k)
					ok = ok && (lower(t[k]) == "0" || (touched.count(lower(t[k])) && !ins.count(lower(t[k]))));
				if (ok)
					o << l << "\n";
				continue;
			}
			if (head[0] == '.') {
				o << l << "\n";
				continue;
			}
			if (keep.count(head)) {
				o << l << "\n";
				continue;
			}
			// supplies and the circuit's own inputs, where this partition uses their nodes
			if (isSource(t)) {
				bool used = false;
				for (const auto &n : nodesOf(t))
					used = used || (n != "0" && n != "gnd" && touched.count(n) && !ins.count(n));
				if (used)
					o << l << "\n";
			}
		}
		for (const auto &n : p.inputs)
			o << "Vpart_" << n << " " << n << " 0 input @" << n << "\n";
		// what later or earlier partitions take from this one
		std::set<std::string> given;
		for (size_t q = 0; q < parts.size(); ++q)
			for (const auto &n : parts[q].inputs)
				if (q != pi && touched.count(n) && !ins.count(n) && given.insert(n).second)
					o << ".output @" << n << " " << n << "\n";
		out.push_back({p.name, o.str(), p.sleeps});
	}
	return out;
}

Netlist Netlist::parse(const std::string &text, const std::map<std::string, double> &params)
{
	// Lines, with continuations joined and comments dropped.
	std::vector<std::string> lines;
	{
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line)) {
			const auto semi = line.find(';');
			if (semi != std::string::npos)
				line.resize(semi);
			size_t start = 0;
			while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])))
				++start;
			line = line.substr(start);
			if (line.empty() || line[0] == '*')
				continue;
			if (line[0] == '+' && !lines.empty())
				lines.back() += " " + line.substr(1);
			else
				lines.push_back(line);
		}
	}
	// Parameters: .param lines, then the caller's.
	std::map<std::string, double> values;
	for (const auto &l : lines) {
		auto t = split(l);
		if (t.empty() || lower(t[0]) != ".param")
			continue;
		for (size_t k = 1; k < t.size(); ++k) {
			const auto eq = t[k].find('=');
			if (eq == std::string::npos)
				throw std::runtime_error("bad .param: " + l);
			values[lower(t[k].substr(0, eq))] = parseValue(t[k].substr(eq + 1));
		}
	}
	for (const auto &[k, v] : params)
		values[lower(k)] = v;
	for (auto &l : lines) {
		size_t open;
		while ((open = l.find('{')) != std::string::npos) {
			const auto close = l.find('}', open);
			if (close == std::string::npos)
				throw std::runtime_error("unclosed {: " + l);
			const std::string key = lower(l.substr(open + 1, close - open - 1));
			const auto it = values.find(key);
			if (it == values.end())
				throw std::runtime_error("no parameter " + key + ": " + l);
			l = l.substr(0, open) + fmt(it->second) + l.substr(close + 1);
		}
	}

	Netlist net;
	auto nodeOf = [&net](const std::string &name) {
		const int n = net.node(name);
		if (n >= 0)
			return n;
		net.nodes.push_back(lower(name));
		return int(net.nodes.size() - 1);
	};
	auto inputOf = [&net](const std::string &name) {
		const int i = net.input(name);
		if (i >= 0)
			return i;
		net.inputs.push_back(lower(name));
		return int(net.inputs.size() - 1);
	};
	auto controlOf = [&net](const std::string &name) {
		const int i = net.control(name);
		if (i >= 0)
			return i;
		net.controls.push_back(lower(name));
		net.controlDefaults.push_back(0.5);
		return int(net.controls.size() - 1);
	};

	for (const auto &l : lines) {
		const auto t = split(l);
		const std::string head = lower(t[0]);
		auto need = [&](size_t n) {
			if (t.size() < n)
				throw std::runtime_error("too few fields: " + l);
		};
		if (head[0] == '.') {
			if (head == ".param" || head == ".end" || head == ".part")
				continue;
			if (head == ".model") {
				need(3);
				Model m;
				const std::string type = lower(t[2]);
				if (type == "d")
					m.type = Model::Diode;
				else if (type == "npn")
					m.type = Model::Npn;
				else if (type == "pnp")
					m.type = Model::Pnp;
				else if (type == "njf")
					m.type = Model::Njf;
				else if (type == "pjf")
					m.type = Model::Pjf;
				else
					throw std::runtime_error("unknown model type: " + l);
				for (size_t k = 3; k < t.size(); ++k) {
					const auto eq = t[k].find('=');
					if (eq == std::string::npos)
						throw std::runtime_error("bad model parameter: " + l);
					const std::string key = lower(t[k].substr(0, eq));
					const double v = parseValue(t[k].substr(eq + 1));
					if (key == "is") m.is = v;
					else if (key == "n") m.n = v;
					else if (key == "rs") m.rs = v;
					else if (key == "bf") m.bf = v;
					else if (key == "br") m.br = v;
					else if (key == "nf") m.nf = v;
					else if (key == "nr") m.nr = v;
					else if (key == "vaf" || key == "va") m.vaf = v;
					else if (key == "ise") m.ise = v;
					else if (key == "ne") m.ne = v;
					else if (key == "isc") m.isc = v;
					else if (key == "nc") m.nc = v;
					else if (key == "vto") m.vto = v;
					else if (key == "beta") m.beta = v;
					else if (key == "lambda") m.lambda = v;
					// charge storage and the rest: SPICE only, ignored here
				}
				net.models[lower(t[1])] = m;
			} else if (head == ".output") {
				need(3);
				Output o;
				o.name = lower(t[1]);
				o.plus = nodeOf(t[2]);
				o.minus = t.size() > 3 ? nodeOf(t[3]) : 0;
				net.outputs.push_back(o);
			} else if (head == ".control") {
				need(3);
				net.controlDefaults[size_t(controlOf(t[1]))] = parseValue(t[2]);
			} else if (head == ".input") {
				need(2);
				inputOf(t[1]);
			} else {
				throw std::runtime_error("unknown directive: " + l);
			}
			continue;
		}
		Element e;
		e.kind = char(std::toupper(static_cast<unsigned char>(head[0])));
		e.name = lower(t[0]);
		switch (e.kind) {
		case 'R':
		case 'C':
			need(4);
			e.nodes = {nodeOf(t[1]), nodeOf(t[2])};
			e.value = parseValue(t[3]);
			if (e.value <= 0)
				throw std::runtime_error("value must be positive: " + l);
			if (e.kind == 'C') {
				// da= and leak=: a real capacitor's dielectric absorption and leakage, as R and C
				// elements across it (circuit.h)
				for (size_t k = 4; k < t.size(); ++k) {
					const auto eq = t[k].find('=');
					const std::string key = lower(t[k].substr(0, eq));
					const double v = eq == std::string::npos ? 0 : parseValue(t[k].substr(eq + 1));
					if (key == "da" && v > 0) {
						const double tau[] = {0.01, 0.1, 1, 10};
						for (int b = 0; b < 4; ++b) {
							const std::string tag = e.name + "#da" + std::to_string(b);
							const int mid = nodeOf(tag);
							Element rb, cb;
							rb.kind = 'R'; rb.name = "r" + tag; rb.nodes = {e.nodes[0], mid};
							cb.kind = 'C'; cb.name = "c" + tag; cb.nodes = {mid, e.nodes[1]}; cb.value = e.value * v / 4;
							rb.value = tau[b] / cb.value;
							net.elements.push_back(rb);
							net.elements.push_back(cb);
						}
					} else if (key == "leak" && v > 0) {
						Element rl;
						rl.kind = 'R'; rl.name = "r" + e.name + "#leak"; rl.nodes = e.nodes; rl.value = v;
						net.elements.push_back(rl);
					} else if (key != "da" && key != "leak") {
						throw std::runtime_error("unknown capacitor option " + t[k] + ": " + l);
					}
				}
			}
			break;
		case 'D':
			need(4);
			e.nodes = {nodeOf(t[1]), nodeOf(t[2])};
			e.model = lower(t[3]);
			break;
		case 'Q':
		case 'J':
			need(5);
			e.nodes = {nodeOf(t[1]), nodeOf(t[2]), nodeOf(t[3])};
			e.model = lower(t[4]);
			break;
		case 'V':
		case 'I':
			need(4);
			e.nodes = {nodeOf(t[1]), nodeOf(t[2])};
			if (lower(t[3]) == "input") {
				need(5);
				e.input = inputOf(t[4]);
			} else {
				e.value = parseValue(lower(t[3]) == "dc" && t.size() > 4 ? t[4] : t[3]);
			}
			break;
		case 'E':
			need(6);
			e.nodes = {nodeOf(t[1]), nodeOf(t[2]), nodeOf(t[3]), nodeOf(t[4])};
			e.value = parseValue(t[5]);
			break;
		case 'B': {
			// a behavioural device with its own model, named after it
			Model m;
			const std::string what = t.size() > 5 ? lower(t[5]) : "";
			size_t first;
			if (t.size() > 6 && lower(t[6]) == "ota") {
				m.type = Model::Ota;
				m.is = 5e-16;
				e.nodes = {nodeOf(t[1]), nodeOf(t[2]), nodeOf(t[3]), nodeOf(t[4]), nodeOf(t[5])};
				first = 7;
			} else if (what == "comparator") {
				m.type = Model::Comparator;
				e.nodes = {nodeOf(t[1]), nodeOf(t[2]), nodeOf(t[3]), nodeOf(t[4])};
				first = 6;
			} else {
				throw std::runtime_error("B elements are ota or comparator: " + l);
			}
			for (size_t k = first; k < t.size(); ++k) {
				const auto eq = t[k].find('=');
				if (eq == std::string::npos)
					throw std::runtime_error("bad parameter: " + l);
				const std::string key = lower(t[k].substr(0, eq));
				const double v = parseValue(t[k].substr(eq + 1));
				if (key == "is") m.is = v;
				else if (key == "rs") m.rs = v;
				else if (key == "gm") m.gm = v;
				else if (key == "isink") m.isink = v;
				else if (key == "vg") m.vg = v;
				else if (key == "vsat") m.vsat = v;
				else if (key == "vcm") m.vcm = v;
				else if (key == "vos") m.vos = v;
				else throw std::runtime_error("unknown parameter " + key + ": " + l);
			}
			e.model = "#" + e.name;
			net.models[e.model] = m;
			break;
		}
		case 'X': {
			need(5);
			if (lower(t[4]) != "opamp")
				throw std::runtime_error("only op-amps (X ... opamp [gbw [gain]]): " + l);
			e.nodes = {nodeOf(t[1]), nodeOf(t[2]), nodeOf(t[3])};
			if (t.size() > 5) {
				// A single-pole op-amp: E1 (unity, the input difference) into R/C (the dominant pole
				// at gbw/gain), then E2 (the open-loop gain) drives the output.
				const double gbw = parseValue(t[5]);
				const double gain = t.size() > 6 ? parseValue(t[6]) : 1e5;
				const int a = nodeOf(e.name + "#a"), b = nodeOf(e.name + "#b");
				Element e1, r, c, e2;
				e1.kind = 'E'; e1.name = "e" + e.name + "_in"; e1.nodes = {a, 0, e.nodes[0], e.nodes[1]}; e1.value = 1;
				r.kind = 'R'; r.name = "r" + e.name + "_pole"; r.nodes = {a, b}; r.value = 1000;
				c.kind = 'C'; c.name = "c" + e.name + "_pole"; c.nodes = {b, 0}; c.value = gain / (2 * 3.14159265358979 * gbw * 1000);
				if (t.size() > 8 && parseValue(t[8]) > 0) {
					// Slew rate: the pole's charging current saturates (a bipolar input pair's tail
					// current), r's current as imax tanh(v / (r imax)). imax = C sr / gain, so its
					// linear range r imax is sr / (2 pi gbw), 53 mV at 1 V/us and 3 MHz: a plain
					// differential pair's 2 V_T.
					Model m;
					m.type = Model::Slew;
					m.gm = 1 / r.value;
					m.is = c.value * parseValue(t[8]) * 1e6 / gain;
					r.kind = 'B';
					r.name = "b" + e.name + "_slew";
					r.model = "#" + r.name;
					net.models[r.model] = m;
				}
				net.elements.push_back(e1);
				net.elements.push_back(r);
				net.elements.push_back(c);
				if (t.size() > 7) {
					// out = gain v_b, clipping softly at +-vsat, through 1 ohm: a limiter device
					Model m;
					m.type = Model::Limiter;
					m.gm = gain;
					m.vsat = parseValue(t[7]);
					e2.kind = 'B';
					e2.name = "b" + e.name + "_out";
					e2.nodes = {b, e.nodes[2]};
					e2.model = "#" + e2.name;
					net.models[e2.model] = m;
				} else {
					e2.kind = 'E'; e2.name = "e" + e.name + "_out"; e2.nodes = {e.nodes[2], 0, b, 0}; e2.value = gain;
				}
				net.elements.push_back(e2);
				continue;
			}
			break;
		}
		case 'P': {
			need(6);
			e.nodes = {nodeOf(t[1]), nodeOf(t[2]), nodeOf(t[3])};
			e.value = parseValue(t[4]);
			e.control = controlOf(t[5]);
			if (t.size() > 6) {
				const std::string taper = lower(t[6]);
				if (taper == "lin" || taper == "b")
					e.taperMid = 0.5;
				else if (taper[0] == 'a')
					e.taperMid = parseValue(taper.substr(1));
				else if (taper[0] == 'c')
					e.taperMid = 1 - parseValue(taper.substr(1));
				else
					throw std::runtime_error("unknown taper: " + l);
			}
			break;
		}
		case 'S': {
			need(4);
			e.nodes = {nodeOf(t[1]), nodeOf(t[2])};
			std::string c = lower(t[3]);
			if (c[0] == '!') {
				e.invert = true;
				c = c.substr(1);
			}
			e.control = controlOf(c);
			if (t.size() > 4)
				e.ron = parseValue(t[4]);
			if (t.size() > 5)
				e.roff = parseValue(t[5]);
			break;
		}
		default:
			throw std::runtime_error("unknown element: " + l);
		}
		net.elements.push_back(e);
	}
	for (const auto &e : net.elements)
		if ((e.kind == 'D' || e.kind == 'Q' || e.kind == 'J' || e.kind == 'B') && net.models.find(e.model) == net.models.end())
			throw std::runtime_error("no model " + e.model + " for " + e.name);
	return net;
}

double Circuit::defaultTolerance()
{
	// 10 uV at a junction is 0.04 % of its current: against 1 uV, envelopes move under 0.1 dB
	// (the hats; 0.01 elsewhere), spectra under 0.05 dB, Fischer's medians and the SPICE suite not
	// at all (the metal voices' 2-5 % within 0.2), for 6 % less CPU.
	static const double tol = std::getenv("TR808_TOL") ? std::atof(std::getenv("TR808_TOL")) : 1e-5;
	return tol;
}

void Circuit::load(const Netlist &netlist)
{
	m_net = netlist;
	// A diode's series resistance gets an internal node.
	std::vector<Netlist::Element> extra;
	for (auto &e : m_net.elements) {
		if (e.kind != 'D' && e.kind != 'B')
			continue;
		const auto &m = m_net.models.at(e.model);
		if (m.rs <= 0 || m.type == Netlist::Model::Comparator)
			continue;
		m_net.nodes.push_back(e.name + "#a");
		const int inner = int(m_net.nodes.size() - 1);
		Netlist::Element r;
		r.kind = 'R';
		r.name = "r" + e.name + "_rs";
		r.nodes = {e.nodes[0], inner};
		r.value = m.rs;
		extra.push_back(r);
		e.nodes[0] = inner;
	}
	m_net.elements.insert(m_net.elements.end(), extra.begin(), extra.end());
	// Nodes joined to ground only through devices (a transistor's collector into a diode) float
	// between junctions; their voltages hang on picoamps, which Newton can't resolve. Real
	// junctions have a few picofarads there: give each such island 10 pF to ground (an
	// audio-irrelevant load). Islands are found with a union-find over the linear elements.
	{
		std::vector<int> parent(m_net.nodes.size());
		for (size_t n = 0; n < parent.size(); ++n)
			parent[n] = int(n);
		std::function<int(int)> root = [&](int n) { return parent[size_t(n)] == n ? n : parent[size_t(n)] = root(parent[size_t(n)]); };
		auto join = [&](int a, int b) { parent[size_t(root(a))] = root(b); };
		for (const auto &e : m_net.elements) {
			if (e.kind == 'D' || e.kind == 'Q' || e.kind == 'J' || e.kind == 'B' || e.kind == 'I')
				continue;
			for (int n : e.nodes)
				join(n, e.nodes[0]);
			if (e.kind == 'X' || e.kind == 'E')
				join(e.nodes[e.kind == 'X' ? 2 : 0], 0); // a driven output
		}
		std::vector<bool> done(m_net.nodes.size(), false);
		for (size_t n = 1; n < m_net.nodes.size(); ++n) {
			const int r = root(int(n));
			if (r == root(0) || done[size_t(r)])
				continue;
			done[size_t(r)] = true;
			Netlist::Element c;
			c.kind = 'C';
			c.name = "c" + m_net.nodes[n] + "_float";
			for (char &ch : c.name)
				if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_')
					ch = '_';
			c.nodes = {int(n), 0};
			c.value = 10e-12;
			m_net.elements.push_back(c);
		}
	}

	m_controls = m_net.controlDefaults;
	// Unknowns: node voltages, then a current for each voltage source, VCVS and op-amp.
	m_n = int(m_net.nodes.size()) - 1;
	m_caps.clear();
	m_capF.clear();
	m_capBE.clear();
	m_devices.clear();
	m_portPlus.clear();
	m_portMinus.clear();
	m_constCount = 0;
	for (size_t k = 0; k < m_net.elements.size(); ++k) {
		const auto &e = m_net.elements[k];
		switch (e.kind) {
		case 'V': case 'E': case 'X': ++m_n; break;
		case 'C':
			m_caps.push_back(int(k));
			m_capF.push_back(e.value);
			{
				auto ends = [&](const char *suffix) {
					const size_t n = std::strlen(suffix);
					return e.name.size() > n && e.name.compare(e.name.size() - n, n, suffix) == 0;
				};
				m_capBE.push_back((ends("_pole") && !m_poleTrap) || ends("_float"));
			}
			break;
		default: break;
		}
		if ((e.kind == 'V' || e.kind == 'I') && e.input < 0)
			++m_constCount;
		if (e.kind == 'D' || e.kind == 'Q' || e.kind == 'J' || e.kind == 'B') {
			Device d;
			d.model = &m_net.models.at(e.model);
			d.port = int(m_portPlus.size());
			d.sign = d.model->type == Netlist::Model::Pnp || d.model->type == Netlist::Model::Pjf ? -1 : 1;
			d.ports = e.kind == 'D' || d.model->type == Netlist::Model::Slew ? 1
			        : d.model->type == Netlist::Model::Ota || d.model->type == Netlist::Model::Comparator ? 3 : 2;
			if (d.model->type == Netlist::Model::Slew) {
				m_portPlus.push_back(e.nodes[0]);
				m_portMinus.push_back(e.nodes[1]);
			} else if (d.model->type == Netlist::Model::Limiter) { // control-ground, out-ground
				m_portPlus.push_back(e.nodes[0]);
				m_portMinus.push_back(0);
				m_portPlus.push_back(e.nodes[1]);
				m_portMinus.push_back(0);
			} else if (d.model->type == Netlist::Model::Ota) { // abc-vee, in+ - in-, out-ground
				m_portPlus.push_back(e.nodes[0]);
				m_portMinus.push_back(e.nodes[1]);
				m_portPlus.push_back(e.nodes[2]);
				m_portMinus.push_back(e.nodes[3]);
				m_portPlus.push_back(e.nodes[4]);
				m_portMinus.push_back(0);
			} else if (d.model->type == Netlist::Model::Comparator) { // in+, in- (to ground), out-vee
				m_portPlus.push_back(e.nodes[0]);
				m_portMinus.push_back(0);
				m_portPlus.push_back(e.nodes[1]);
				m_portMinus.push_back(0);
				m_portPlus.push_back(e.nodes[2]);
				m_portMinus.push_back(e.nodes[3]);
			} else if (e.kind == 'D') {
				m_portPlus.push_back(e.nodes[0]);
				m_portMinus.push_back(e.nodes[1]);
			} else { // b-e, b-c (g-s, g-d)
				m_portPlus.push_back(e.nodes[1]);
				m_portMinus.push_back(e.nodes[2]);
				m_portPlus.push_back(e.nodes[1]);
				m_portMinus.push_back(e.nodes[0]);
			}
			{
				const auto &m = *d.model;
				d.kf = 1 / ((m.type == Netlist::Model::Diode ? m.n : m.nf) * kVt);
				d.kr = 1 / (m.nr * kVt);
				d.ke = 1 / (m.ne * kVt);
				d.kc = 1 / (m.nc * kVt);
				d.kaf = m.vaf > 0 ? 1 / m.vaf : 0;
				d.kbf = 1 / m.bf;
				d.kbr = 1 / m.br;
			}
			m_devices.push_back(d);
		}
	}
	m_bjts.clear();
	m_diodes.clear();
	m_otherDevices.clear();
	for (size_t k = 0; k < m_devices.size(); ++k) {
		const Device &d = m_devices[k];
		const auto &m = *d.model;
		if (m.type == Netlist::Model::Npn || m.type == Netlist::Model::Pnp) {
			Bjt b;
			b.p = d.port;
			b.s = d.sign;
			b.is = m.is;
			b.kf = d.kf;
			b.kr = d.kr;
			b.ke = d.ke;
			b.kc = d.kc;
			b.ise = m.ise;
			b.isc = m.isc;
			b.kaf = d.kaf;
			b.kbf = d.kbf;
			b.kbr = d.kbr;
			m_bjts.push_back(b);
		} else if (m.type == Netlist::Model::Diode) {
			m_diodes.push_back({d.port, m.is, d.kf});
		} else {
			m_otherDevices.push_back(int(k));
		}
	}
	m_nx = int(m_caps.size());
	m_np = int(m_portPlus.size());
	m_nu = m_constCount + int(m_net.inputs.size());
	m_ny = int(m_net.outputs.size());
	m_portVt.assign(size_t(m_np), kVt);
	m_portVcrit.assign(size_t(m_np), 0.0);
	m_portSign.assign(size_t(m_np), 1.0);
	m_portWeight.assign(size_t(m_np), 1.0);
	for (const auto &d : m_devices) {
		const auto &m = *d.model;
		if (m.type == Netlist::Model::Slew) {
			// as a limiter's input: in units of its linear range. A residual here is a current error
			// into the pole, which the open-loop gain makes 2 pi gbw T output volts a port volt
			// (about 200 at 96 kHz): judged as 1000.
			m_portVt[size_t(d.port)] = -2;
			m_portVcrit[size_t(d.port)] = m.gm / m.is;
			m_portWeight[size_t(d.port)] = 1000;
			continue;
		}
		if (m.type == Netlist::Model::Ota || m.type == Netlist::Model::Comparator || m.type == Netlist::Model::Limiter) {
			// only the OTA's abc diode is a junction; the other ports aren't limited (vt 0)
			for (int k = 0; k < d.ports; ++k)
				m_portVt[size_t(d.port + k)] = 0;
			if (m.type == Netlist::Model::Ota) {
				m_portVt[size_t(d.port)] = kVt;
				m_portVcrit[size_t(d.port)] = kVt * std::log(kVt / (std::sqrt(2.0) * m.is));
			} else if (m.type == Netlist::Model::Comparator) {
				m_portVt[size_t(d.port + 2)] = -1; // the output: never step below the rail at once
			} else { // limiter: don't jump deep into saturation (or across it) in one step
				m_portVt[size_t(d.port)] = -2;
				m_portVcrit[size_t(d.port)] = m.gm / m.vsat;
				// its control is the op-amp's pole node, gm (the open-loop gain) times smaller than
				// the output it sets: Newton judges it in output volts
				m_portWeight[size_t(d.port)] = m.gm;
			}
			continue;
		}
		const int ports = d.ports;
		for (int k = 0; k < ports; ++k) {
			const bool jfet = m.type == Netlist::Model::Njf || m.type == Netlist::Model::Pjf;
			const double n = m.type == Netlist::Model::Diode || jfet ? m.n : k == 0 ? m.nf : m.nr;
			const double vt = n * kVt;
			m_portVt[size_t(d.port + k)] = vt;
			m_portVcrit[size_t(d.port + k)] = vt * std::log(vt / (std::sqrt(2.0) * m.is));
			m_portSign[size_t(d.port + k)] = d.sign;
		}
	}
	// the ports by how Newton limits them (newtonT): unlimited, an open collector, a limiter's input,
	// a junction
	for (auto &c : m_portClass)
		c.clear();
	m_portKind.assign(size_t(m_np), 0);
	for (int a = 0; a < m_np; ++a) {
		const double vt = m_portVt[size_t(a)];
		const int kind = vt == 0 ? 0 : vt == -1 ? 1 : vt == -2 ? 2 : 3;
		m_portClass[kind].push_back(a);
		m_portKind[size_t(a)] = char(kind);
	}
	const size_t nx = size_t(m_nx), np = size_t(m_np);
	m_vc.assign(nx, 0.0);
	m_ic.assign(nx, 0.0);
	// the three state buffers, each [x | y | v] (circuit.h)
	m_vOff = int(padded(nx + m_net.outputs.size()));
	m_sbStride = m_vOff + int(padded(np));
	m_sb.assign(3 * size_t(m_sbStride), 0.0);
	m_bx = 0;
	m_bxn = 1;
	m_bxp = 2;
	m_xValid = false;
	m_capsStale = false;
	m_w.assign(nx + size_t(m_nu - m_constCount) + np, 0.0);
	m_vcSave.assign(nx, 0.0);
	m_icSave.assign(nx, 0.0);
	m_v.assign(np, 0.0);
	m_i.assign(np, 0.0);
	m_current = false;
	m_vp.assign(np + 1, 0.0);
	m_p.assign(padded(np), 0.0);
	m_r.assign(padded(np), 0.0);
	m_vOld.assign(np, 0.0);
	m_vSave.assign(np, 0.0);
	m_iSave.assign(np, 0.0);
	m_jac.assign(np * np, 0.0);
	m_ra.assign(np, 0.0);
	m_sigPorts.assign(np, 0);
	m_u.assign(size_t(m_nu), 0.0);
	m_y.assign(size_t(m_ny), 0.0);
	int c = 0;
	for (const auto &e : m_net.elements)
		if ((e.kind == 'V' || e.kind == 'I') && e.input < 0)
			m_u[size_t(c++)] = e.value;
	m_uLast = m_u;
	m_uMid = m_u;
	m_uTo = m_u;
	for (int lv = 0; lv < kLevels; ++lv) {
		m_splitTo[size_t(lv)] = m_u;
		m_splitFrom[size_t(lv)] = m_u;
	}
	m_subLeft = 0;
	m_lin = Linear{};
	m_dirty = true;
}

int Circuit::capIndex(const std::string &name) const
{
	for (int k = 0; k < m_nx; ++k)
		if (m_net.elements[size_t(m_caps[size_t(k)])].name == lower(name))
			return k;
	return -1;
}

void Circuit::setState(const State &s)
{
	m_lin.on = false;
	m_current = false;
	m_capsStale = false;
	m_xValid = false;
	if (s.vc.size() == m_vc.size()) m_vc = s.vc;
	if (s.ic.size() == m_ic.size()) m_ic = s.ic;
	if (s.v.size() == m_v.size()) m_v = s.v;
	if (s.u.size() == m_u.size()) {
		m_u = s.u;
		m_uLast = s.u;
	}
	for (auto &L : m_levels)
		L.luValid = false;
	for (auto &L : m_edgeLevels)
		L.luValid = false;
}

void Circuit::setRate(double rate)
{
	if (rate != m_rate) {
		m_rate = rate;
		m_dirty = true;
	}
}

void Circuit::setPoleTrap(bool on)
{
	if (on == m_poleTrap)
		return;
	m_poleTrap = on;
	for (size_t k = 0; k < m_caps.size(); ++k) {
		const auto &name = m_net.elements[size_t(m_caps[k])].name;
		if (name.size() > 5 && name.compare(name.size() - 5, 5, "_pole") == 0)
			m_capBE[k] = on ? 0 : 1;
	}
	m_dirty = true;
}

void Circuit::setControl(int index, double position)
{
	if (index < 0 || index >= int(m_controls.size()))
		return;
	if (m_controls[size_t(index)] != position) {
		m_controls[size_t(index)] = position;
		m_dirty = true;
	}
}

void Circuit::build()
{
	if (m_lin.on) {
		// a control moved: the linearisation is the old circuit's; try again right after the next
		// step (this isn't the circuit leaving its linear region)
		const int backoff = m_lin.backoff;
		leaveLinear();
		m_lin.backoff = backoff;
		m_lin.wait = 1;
	}
	// The state as voltages and currents (the companion states are the old step size's).
	dropX();
	// The conductances (resistors, pots, switches) and the source and op-amp rows, without the
	// capacitors, whose companions depend on the step size; then each level's system.
	const int n = m_n;
	const int nodes = int(m_net.nodes.size()) - 1;
	m_G.assign(size_t(n * n), 0.0);
	m_Nx.assign(size_t(n * m_nx), 0.0);
	m_Nu.assign(size_t(n * m_nu), 0.0);
	m_Nn.assign(size_t(n * m_np), 0.0);
	auto G = [&](int r, int c) -> double & { return m_G[size_t(r * n + c)]; };
	auto conductance = [&](int a, int b, double g) {
		if (a > 0) G(a - 1, a - 1) += g;
		if (b > 0) G(b - 1, b - 1) += g;
		if (a > 0 && b > 0) {
			G(a - 1, b - 1) -= g;
			G(b - 1, a - 1) -= g;
		}
	};
	int extra = nodes, cap = 0, constIndex = 0;
	for (const auto &e : m_net.elements) {
		switch (e.kind) {
		case 'R':
			conductance(e.nodes[0], e.nodes[1], 1.0 / e.value);
			break;
		case 'P': {
			const double share = potShare(m_controls[size_t(e.control)], e.taperMid);
			const double floor = 0.1;
			conductance(e.nodes[1], e.nodes[2], 1.0 / std::max(e.value * share, floor));
			conductance(e.nodes[0], e.nodes[1], 1.0 / std::max(e.value * (1 - share), floor));
			break;
		}
		case 'S': {
			const bool closed = (m_controls[size_t(e.control)] >= 0.5) != e.invert;
			conductance(e.nodes[0], e.nodes[1], 1.0 / (closed ? e.ron : e.roff));
			break;
		}
		case 'C':
			if (e.nodes[0] > 0) m_Nx[size_t((e.nodes[0] - 1) * m_nx + cap)] += 1;
			if (e.nodes[1] > 0) m_Nx[size_t((e.nodes[1] - 1) * m_nx + cap)] -= 1;
			++cap;
			break;
		case 'V': {
			const int k = extra++;
			const int a = e.nodes[0], b = e.nodes[1];
			if (a > 0) { G(a - 1, k) += 1; G(k, a - 1) += 1; }
			if (b > 0) { G(b - 1, k) -= 1; G(k, b - 1) -= 1; }
			const int u = e.input < 0 ? constIndex++ : m_constCount + e.input;
			m_Nu[size_t(k * m_nu + u)] = 1;
			break;
		}
		case 'I': {
			const int u = e.input < 0 ? constIndex++ : m_constCount + e.input;
			if (e.nodes[0] > 0) m_Nu[size_t((e.nodes[0] - 1) * m_nu + u)] -= 1;
			if (e.nodes[1] > 0) m_Nu[size_t((e.nodes[1] - 1) * m_nu + u)] += 1;
			break;
		}
		case 'E': {
			const int k = extra++;
			const int op = e.nodes[0], om = e.nodes[1], ip = e.nodes[2], im = e.nodes[3];
			if (op > 0) { G(op - 1, k) += 1; G(k, op - 1) += 1; }
			if (om > 0) { G(om - 1, k) -= 1; G(k, om - 1) -= 1; }
			if (ip > 0) G(k, ip - 1) -= e.value;
			if (im > 0) G(k, im - 1) += e.value;
			break;
		}
		case 'X': {
			const int k = extra++;
			const int ip = e.nodes[0], im = e.nodes[1], out = e.nodes[2];
			if (out > 0) G(out - 1, k) += 1;
			if (ip > 0) G(k, ip - 1) += 1;
			if (im > 0) G(k, im - 1) -= 1;
			break;
		}
		default:
			break;
		}
	}
	for (int p = 0; p < m_np; ++p) {
		if (m_portPlus[size_t(p)] > 0) m_Nn[size_t((m_portPlus[size_t(p)] - 1) * m_np + p)] -= 1;
		if (m_portMinus[size_t(p)] > 0) m_Nn[size_t((m_portMinus[size_t(p)] - 1) * m_np + p)] += 1;
	}
	// 1 Gohm from every node to ground, so a node joined only by devices (an emitter between two
	// diodes) still has a linear path: SPICE's junction conductances do this for it.
	for (int k = 0; k < nodes; ++k)
		G(k, k) += kGshunt;
	// Level 0 now; the substep levels and the edges' when first used.
	double T = 1.0 / m_rate;
	for (int lv = 0; lv < kLevels; ++lv) {
		m_levels[lv].T = m_edgeLevels[lv].T = T;
		m_levels[lv].built = m_edgeLevels[lv].built = false;
		m_levels[lv].luValid = m_edgeLevels[lv].luValid = false;
		T /= 4;
	}
	buildLevel(m_levels[0], false);
	m_dirty = false;
}

void Circuit::buildLevel(Level &L, bool allBackwardEuler)
{
	L.built = true;
	const int n = m_n;
	std::vector<double> S = m_G;
	L.gc.assign(size_t(m_nx), 0.0);
	L.vcScale.assign(size_t(m_nx), 0.0);
	L.be = m_capBE;
	if (allBackwardEuler)
		std::fill(L.be.begin(), L.be.end(), char(1));
	for (int k = 0; k < m_nx; ++k) {
		const auto &e = m_net.elements[size_t(m_caps[size_t(k)])];
		// trapezoidal: i = 2C/T v - (2C/T v[n-1] + i[n-1]); backward Euler: i = C/T (v - v[n-1])
		const double g = (L.be[size_t(k)] ? 1 : 2) * m_capF[size_t(k)] / L.T;
		L.gc[size_t(k)] = g;
		L.vcScale[size_t(k)] = L.be[size_t(k)] ? 1 / g : 1 / (2 * g);
		const int a = e.nodes[0], b = e.nodes[1];
		if (a > 0) S[size_t((a - 1) * n + a - 1)] += g;
		if (b > 0) S[size_t((b - 1) * n + b - 1)] += g;
		if (a > 0 && b > 0) {
			S[size_t((a - 1) * n + b - 1)] -= g;
			S[size_t((b - 1) * n + a - 1)] -= g;
		}
	}
	std::vector<int> piv(static_cast<size_t>(n));
	if (!luDecompose(S.data(), n, piv.data()))
		throw std::runtime_error("the circuit's matrix is singular (a floating node or a loop of sources?)");
	// S^-1 times each incidence matrix, column by column.
	// All the columns at once: each row operation runs across the columns (every column's own
	// sums in the order a single solve makes them, so the same results).
	auto solveCols = [&](const std::vector<double> &N, int cols) {
		std::vector<double> B = N;
		const double *lu = S.data();
		double *b = B.data();
		for (int k = 0; k < n; ++k)
			if (piv[size_t(k)] != k)
				for (int c = 0; c < cols; ++c)
					std::swap(b[k * cols + c], b[piv[size_t(k)] * cols + c]);
		for (int r = 1; r < n; ++r)
			for (int q = 0; q < r; ++q) {
				const double f = lu[r * n + q];
				if (f != 0)
					for (int c = 0; c < cols; ++c)
						b[r * cols + c] -= f * b[q * cols + c];
			}
		for (int r = n - 1; r >= 0; --r) {
			for (int q = r + 1; q < n; ++q) {
				const double f = lu[r * n + q];
				if (f != 0)
					for (int c = 0; c < cols; ++c)
						b[r * cols + c] -= f * b[q * cols + c];
			}
			const double d = lu[r * n + r];
			for (int c = 0; c < cols; ++c)
				b[r * cols + c] /= d;
		}
		return B;
	};
	const auto SiNx = solveCols(m_Nx, m_nx), SiNu = solveCols(m_Nu, m_nu), SiNn = solveCols(m_Nn, m_np);
	auto rowDiff = [&](const std::vector<double> &M, int cols, int plus, int minus, double scale, double *dst) {
		for (int c = 0; c < cols; ++c) {
			double v = 0;
			if (plus > 0) v += M[size_t((plus - 1) * cols + c)];
			if (minus > 0) v -= M[size_t((minus - 1) * cols + c)];
			dst[c] = scale * v;
		}
	};
	L.A.assign(size_t(m_nx * m_nx), 0.0);
	L.B.assign(size_t(m_nx * m_nu), 0.0);
	L.C.assign(size_t(m_nx * m_np), 0.0);
	for (int k = 0; k < m_nx; ++k) {
		const auto &e = m_net.elements[size_t(m_caps[size_t(k)])];
		// the next state: trapezoidal x[n] = 2 (2C/T) v[n] - x[n-1]; backward Euler x[n] = C/T v[n]
		const bool be = L.be[size_t(k)];
		const double s = (be ? 1 : 2) * L.gc[size_t(k)];
		rowDiff(SiNx, m_nx, e.nodes[0], e.nodes[1], s, &L.A[size_t(k * m_nx)]);
		if (!be)
			L.A[size_t(k * m_nx + k)] -= 1;
		rowDiff(SiNu, m_nu, e.nodes[0], e.nodes[1], s, &L.B[size_t(k * m_nu)]);
		rowDiff(SiNn, m_np, e.nodes[0], e.nodes[1], s, &L.C[size_t(k * m_np)]);
	}
	L.D.assign(size_t(m_np * m_nx), 0.0);
	L.E.assign(size_t(m_np * m_nu), 0.0);
	L.F.assign(size_t(m_np * m_np), 0.0);
	for (int p = 0; p < m_np; ++p) {
		rowDiff(SiNx, m_nx, m_portPlus[size_t(p)], m_portMinus[size_t(p)], 1, &L.D[size_t(p * m_nx)]);
		rowDiff(SiNu, m_nu, m_portPlus[size_t(p)], m_portMinus[size_t(p)], 1, &L.E[size_t(p * m_nu)]);
		rowDiff(SiNn, m_np, m_portPlus[size_t(p)], m_portMinus[size_t(p)], 1, &L.F[size_t(p * m_np)]);
	}
	L.Dy.assign(size_t(m_ny * m_nx), 0.0);
	L.Ey.assign(size_t(m_ny * m_nu), 0.0);
	L.Fy.assign(size_t(m_ny * m_np), 0.0);
	for (int o = 0; o < m_ny; ++o) {
		const auto &out = m_net.outputs[size_t(o)];
		rowDiff(SiNx, m_nx, out.plus, out.minus, 1, &L.Dy[size_t(o * m_nx)]);
		rowDiff(SiNu, m_nu, out.plus, out.minus, 1, &L.Ey[size_t(o * m_nu)]);
		rowDiff(SiNn, m_np, out.plus, out.minus, 1, &L.Fy[size_t(o * m_np)]);
	}
	// The constant sources' share, so a step only multiplies the inputs that change.
	auto constPart = [&](const std::vector<double> &M, int rows, std::vector<double> &dst) {
		dst.assign(size_t(rows), 0.0);
		for (int r = 0; r < rows; ++r)
			for (int c = 0; c < m_constCount; ++c)
				dst[size_t(r)] += M[size_t(r * m_nu + c)] * m_u[size_t(c)];
	};
	constPart(L.E, m_np, L.pConst);
	L.pConst.resize(padded(size_t(m_np)), 0.0);
	constPart(L.B, m_nx, L.xConst);
	constPart(L.Ey, m_ny, L.yConst);
	{
		const int nv = m_nu - m_constCount, w1 = m_nx + nv, w2 = w1 + m_np;
		L.P.assign(size_t(m_np * w1), 0.0);
		for (int a = 0; a < m_np; ++a) {
			for (int k = 0; k < m_nx; ++k)
				L.P[size_t(a * w1 + k)] = L.D[size_t(a * m_nx + k)];
			for (int j = 0; j < nv; ++j)
				L.P[size_t(a * w1 + m_nx + j)] = L.E[size_t(a * m_nu + m_constCount + j)];
		}
		L.S.assign(size_t((m_ny + m_nx) * w2), 0.0);
		L.sConst.assign(padded(size_t(m_ny + m_nx)), 0.0);
		auto row = [&](int r, const std::vector<double> &X, const std::vector<double> &U, const std::vector<double> &I, int src) {
			double *d = &L.S[size_t(r * w2)];
			for (int k = 0; k < m_nx; ++k)
				d[k] = X[size_t(src * m_nx + k)];
			for (int j = 0; j < nv; ++j)
				d[m_nx + j] = U[size_t(src * m_nu + m_constCount + j)];
			for (int k = 0; k < m_np; ++k)
				d[w1 + k] = I[size_t(src * m_np + k)];
		};
		for (int r = 0; r < m_nx; ++r) {
			row(r, L.A, L.B, L.C, r);
			L.sConst[size_t(r)] = L.xConst[size_t(r)];
		}
		for (int o = 0; o < m_ny; ++o) {
			row(m_nx + o, L.Dy, L.Ey, L.Fy, o);
			L.sConst[size_t(m_nx + o)] = L.yConst[size_t(o)];
		}
		toColumns(L.P.data(), m_np, w1, w1, L.Pc);
		toColumns(L.S.data(), m_ny + m_nx, w2, w2, L.Sc);
	}
	L.luM.assign(size_t(m_np * m_np), 0.0);
	L.luPiv.assign(size_t(m_np), 0);
	L.luValid = false;
	L.cols.assign(size_t(m_np * m_np), 0.0);
	L.active.assign(size_t(m_np), 0);
	L.pos.assign(size_t(m_np), -1);
	L.nActive = 0;
	toColumns(L.F.data(), m_np, m_np, m_np, L.Fc);
	L.FT.assign(size_t(m_np * m_np), 0.0);
	for (int a = 0; a < m_np; ++a)
		for (int q = 0; q < m_np; ++q)
			L.FT[size_t(q * m_np + a)] = L.F[size_t(a * m_np + q)];
	L.fColMax.assign(size_t(m_np), 0.0);
	for (int q = 0; q < m_np; ++q)
		for (int a = 0; a < m_np; ++a)
			L.fColMax[size_t(q)] = std::max(L.fColMax[size_t(q)], std::abs(L.F[size_t(a * m_np + q)]));
}

void Circuit::deviceCurrents(const double *v, double *i, double *jac) const
{
	// jac is np x np; only the devices' diagonal blocks are written.
	for (const Bjt &d : m_bjts) {
		// Gummel-Poon without high injection or charge: SPICE's bjtload with the same terms.
		const int p = d.p;
		const double s = d.s;
		const double vbe = s * v[p], vbc = s * v[p + 1];
		double ef, er;
		const double xf = safeExp(vbe * d.kf, ef), xr = safeExp(vbc * d.kr, er);
		const double If = d.is * (xf - 1) + kGmin * vbe, gIf = d.is * ef * d.kf + kGmin;
		const double Ir = d.is * (xr - 1) + kGmin * vbc, gIr = d.is * er * d.kr + kGmin;
		double Ibe = 0, gIbe = 0, Ibc = 0, gIbc = 0;
		if (d.ise > 0) {
			double e;
			const double x = safeExp(vbe * d.ke, e);
			Ibe = d.ise * (x - 1);
			gIbe = d.ise * e * d.ke;
		}
		if (d.isc > 0) {
			double e;
			const double x = safeExp(vbc * d.kc, e);
			Ibc = d.isc * (x - 1);
			gIbc = d.isc * e * d.kc;
		}
		const double q = 1 - vbc * d.kaf; // 1/qb (1 without VAF)
		const double dqdvbc = -d.kaf;
		const double Icc = (If - Ir) * q;
		const double dIccdvbe = gIf * q, dIccdvbc = -gIr * q + (If - Ir) * dqdvbc;
		// Port 0 (b-e): emitter current; port 1 (b-c): minus the collector current.
		i[p] = s * (Icc + If * d.kbf + Ibe);
		i[p + 1] = s * (-Icc + Ir * d.kbr + Ibc);
		jac[p * m_np + p] = dIccdvbe + gIf * d.kbf + gIbe;
		jac[p * m_np + p + 1] = dIccdvbc;
		jac[(p + 1) * m_np + p] = -dIccdvbe;
		jac[(p + 1) * m_np + p + 1] = -dIccdvbc + gIr * d.kbr + gIbc;
	}
	for (const Diode &d : m_diodes) {
		const int p = d.p;
		double e;
		const double x = safeExp(v[p] * d.kf, e);
		i[p] = d.is * (x - 1) + kGmin * v[p];
		jac[p * m_np + p] = d.is * e * d.kf + kGmin;
	}
	for (int k : m_otherDevices) {
		const Device &d = m_devices[size_t(k)];
		const auto &m = *d.model;
		const int p = d.port;
		if (m.type == Netlist::Model::Ota) {
			// I_abc into a diode (a mirror's input); out gets gm I_abc tanh(vd / 2V_T).
			double e;
			const double x = safeExp(v[p] / kVt, e);
			const double iabc = m.is * (x - 1) + kGmin * v[p], gabc = m.is * e / kVt + kGmin;
			const double th = std::tanh(v[p + 1] / (2 * kVt));
			i[p] = iabc;
			i[p + 1] = 0;
			i[p + 2] = -m.gm * std::max(iabc, 0.0) * th;
			for (int a = 0; a < 3; ++a)
				for (int b = 0; b < 3; ++b)
					jac[(p + a) * m_np + p + b] = 0;
			jac[p * m_np + p] = gabc;
			jac[(p + 2) * m_np + p] = iabc > 0 ? -m.gm * gabc * th : 0;
			jac[(p + 2) * m_np + p + 1] = -m.gm * std::max(iabc, 0.0) * (1 - th * th) / (2 * kVt);
			continue;
		}
		if (m.type == Netlist::Model::Slew) {
			// An op-amp's input stage: imax tanh(v / (r imax)) into its pole
			const double th = std::tanh(v[p] * m.gm / m.is);
			i[p] = m.is * th;
			jac[p * m_np + p] = m.gm * (1 - th * th);
			continue;
		}
		if (m.type == Netlist::Model::Limiter) {
			// An op-amp's output stage: drives out through 1 ohm to gain v_b, exactly linear up to
			// kLimiterKnee below the rails (so Newton's Jacobian stays put while the op-amp works in
			// its linear range: its loop gain makes the chord iterations sensitive to any change of
			// slope), then rounding off into +-vsat over that last stretch.
			const double x = m.gm * v[p];
			const double knee = m.vsat - kLimiterKnee;
			double o = x, d = 1;
			if (std::abs(x) > knee) {
				const double th = std::tanh(std::min((std::abs(x) - knee) / kLimiterKnee, 40.0));
				o = (x > 0 ? 1 : -1) * (knee + kLimiterKnee * th);
				d = 1 - th * th;
			}
			// anti-windup: kWindupG * softplus(|x| - vsat - margin) out of the pole node
			const double z = (std::abs(x) - m.vsat - kWindupMargin) / kWindupSoft;
			double aw = 0, daw = 0;
			if (z > -40) {
				const double e = std::exp(std::min(z, 40.0));
				aw = z > 40 ? z * kWindupSoft : kWindupSoft * std::log1p(e);
				daw = z > 40 ? 1 : e / (1 + e);
			}
			i[p] = (x > 0 ? 1 : -1) * kWindupG * aw;
			i[p + 1] = v[p + 1] - o;
			jac[p * m_np + p] = kWindupG * daw * m.gm;
			jac[p * m_np + p + 1] = 0;
			jac[(p + 1) * m_np + p] = -m.gm * d;
			jac[(p + 1) * m_np + p + 1] = 1;
			continue;
		}
		if (m.type == Netlist::Model::Comparator) {
			// The output transistor sinks while in+ + vos < in-: isink, saturating below vsat. Each
			// input counts as no higher than vcm (softly, over 50 mV), the top of an LM339's
			// common-mode range: with both inputs above it, the output is on.
			constexpr double w = 0.05;
			auto cm = [&](double x, double &dx) {
				const double z = (m.vcm - x) / w;
				if (z > 40) { dx = 1; return x; }
				if (z < -40) { dx = 0; return m.vcm; }
				const double e = std::exp(z);
				dx = e / (1 + e);
				return m.vcm - w * std::log1p(e);
			};
			double dp, dn;
			const double vd = cm(v[p], dp) - cm(v[p + 1], dn) + m.vos;
			const double vg = m.vg * m_soften;
			const double z = std::clamp(vd / vg, -60.0, 60.0);
			const double on = 1 / (1 + std::exp(z));
			const double don = -on * (1 - on) / vg;
			// tanh above the rail, carried on linearly below it (never a solution; keeps Newton sane)
			const double x = v[p + 2] / m.vsat;
			const double th = x > 0 ? std::tanh(x) : x;
			const double dth = x > 0 ? 1 - th * th : 1;
			i[p] = 0;
			i[p + 1] = 0;
			i[p + 2] = m.isink * on * th;
			for (int a = 0; a < 3; ++a)
				for (int b = 0; b < 3; ++b)
					jac[(p + a) * m_np + p + b] = 0;
			jac[(p + 2) * m_np + p] = m.isink * don * dp * th;
			jac[(p + 2) * m_np + p + 1] = -m.isink * don * dn * th;
			jac[(p + 2) * m_np + p + 2] = m.isink * on * dth / m.vsat;
			continue;
		}
		if (m.type == Netlist::Model::Njf || m.type == Netlist::Model::Pjf) {
			// SPICE's level-1 JFET (jfetload): gate diodes and the Shichman-Hodges channel.
			const double s = d.sign;
			const double vgs = s * v[p], vgd = s * v[p + 1];
			const double nvt = m.n * kVt;
			double es, ed;
			const double xs = safeExp(vgs / nvt, es), xd = safeExp(vgd / nvt, ed);
			const double igs = m.is * (xs - 1) + kGmin * vgs, ggs = m.is * es / nvt + kGmin;
			const double igd = m.is * (xd - 1) + kGmin * vgd, ggd = m.is * ed / nvt + kGmin;
			const double vds = vgs - vgd;
			// channel current from drain to source and its derivatives in vgs, vgd
			double ids = 0, dvgs = 0, dvgd = 0;
			if (vds >= 0) {
				const double vgst = vgs - m.vto;
				if (vgst > 0) {
					const double l = 1 + m.lambda * vds;
					if (vds < vgst) { // linear
						ids = m.beta * vds * (2 * vgst - vds) * l;
						const double dIdvds = m.beta * ((2 * vgst - 2 * vds) * l + vds * (2 * vgst - vds) * m.lambda);
						const double dIdvgst = m.beta * 2 * vds * l;
						dvgs = dIdvgst + dIdvds;
						dvgd = -dIdvds;
					} else { // saturation
						ids = m.beta * vgst * vgst * l;
						const double dIdvds = m.beta * vgst * vgst * m.lambda;
						dvgs = m.beta * 2 * vgst * l + dIdvds;
						dvgd = -dIdvds;
					}
				}
			} else { // drain and source swap roles
				const double vgdt = vgd - m.vto, vsd = -vds;
				if (vgdt > 0) {
					const double l = 1 + m.lambda * vsd;
					double isd, dIdvsd, dIdvgdt;
					if (vsd < vgdt) {
						isd = m.beta * vsd * (2 * vgdt - vsd) * l;
						dIdvsd = m.beta * ((2 * vgdt - 2 * vsd) * l + vsd * (2 * vgdt - vsd) * m.lambda);
						dIdvgdt = m.beta * 2 * vsd * l;
					} else {
						isd = m.beta * vgdt * vgdt * l;
						dIdvsd = m.beta * vgdt * vgdt * m.lambda;
						dIdvgdt = m.beta * 2 * vgdt * l;
					}
					ids = -isd; // vsd = vgd - vgs
					dvgd = -(dIdvgdt + dIdvsd);
					dvgs = dIdvsd;
				}
			}
			// Port 0 (g-s): out of the source; port 1 (g-d): out of the drain.
			i[p] = s * (igs + ids);
			i[p + 1] = s * (igd - ids);
			jac[p * m_np + p] = ggs + dvgs;
			jac[p * m_np + p + 1] = dvgd;
			jac[(p + 1) * m_np + p] = -dvgs;
			jac[(p + 1) * m_np + p + 1] = ggd - dvgd;
			continue;
		}
	}
}

template <int NP> bool Circuit::factor(Level &L)
{
	// The columns of M = I - F G for each device block; a port whose column's F G part is under
	// 1e-4 everywhere counts as inactive (its column the identity). That part is at most
	// fColMax[q] |G[q][c]| over the block's rows q, so only the columns that bound lets through are
	// built.
	const int np = NP > 0 ? NP : m_np;
	const double *FT = L.FT.data();
	const double *fmax = L.fColMax.data();
	const double *jac = m_jac.data();
	double *col = L.cols.data(); // np x np, column c at col[c * np]
	int nA = 0;
	for (const auto &d : m_devices) {
		const int k = d.port, n = d.ports;
		// a slewing op-amp's input stage always counts: half saturated, its column is small, but
		// its loop's gain makes the error of leaving it out more than the residual tolerates
		const bool always = d.model->type == Netlist::Model::Slew;
		for (int c = k; c < k + n; ++c) {
			double bound = 0;
			for (int q = k; q < k + n; ++q)
				bound = std::max(bound, fmax[q] * std::abs(jac[q * np + c]));
			if (bound <= 1e-4 && !always) {
				L.pos[size_t(c)] = -1;
				continue;
			}
			double *__restrict mc = col + size_t(c) * size_t(np);
			bool first = true;
			double big;
			for (int q = k; q < k + n; ++q) {
				const double g = jac[q * np + c];
				if (g == 0)
					continue;
				const double *__restrict f = FT + size_t(q) * size_t(np);
				if (first)
					for (int a = 0; a < np; ++a)
						mc[a] = -f[a] * g;
				else
					for (int a = 0; a < np; ++a)
						mc[a] -= f[a] * g;
				first = false;
			}
			big = maxAbs(mc, np);
			if (big > 1e-4 || (always && !first))
				L.active[size_t(nA++)] = c;
			else
				L.pos[size_t(c)] = -1;
		}
	}
	L.nActive = nA;
	for (int k = 0; k < nA; ++k)
		L.pos[size_t(L.active[size_t(k)])] = k;
	// M_AA, factored
	double *M = L.luM.data();
	if (NP > 0 && nA == NP) {
		// every port active (in port order): the whole of M, compiled for its size
		for (int r = 0; r < NP; ++r)
			for (int c = 0; c < NP; ++c)
				M[r * NP + c] = col[c * NP + r] + (r == c ? 1.0 : 0.0);
		if (!luDecomposeN<NP>(M, L.luPiv.data()))
			return false;
		L.luValid = true;
		++m_stats.factorizations;
		return true;
	}
	for (int r = 0; r < nA; ++r) {
		const int a = L.active[size_t(r)];
		for (int c = 0; c < nA; ++c) {
			const int b = L.active[size_t(c)];
			M[r * nA + c] = col[size_t(b) * size_t(np) + size_t(a)] + (a == b ? 1.0 : 0.0);
		}
	}
	if (!luDecompose(M, nA, L.luPiv.data()))
		return false;
	L.luValid = true;
	++m_stats.factorizations;
	return true;
}

template <int NP> void Circuit::solveActive(Level &L, double *r) const
{
	// dv_A = M_AA^-1 r_A, then dv_I = r_I - M_IA dv_A (the inactive ports' rows of the active columns).
	const int np = NP > 0 ? NP : m_np, nA = L.nActive;
	if (nA == 0)
		return;
	if (NP > 0 && nA == NP) { // every port active, in port order
		luSolveN<NP>(L.luM.data(), L.luPiv.data(), r);
		return;
	}
	double *ra = m_ra.data();
	for (int k = 0; k < nA; ++k)
		ra[k] = r[L.active[size_t(k)]];
	luSolve(L.luM.data(), nA, L.luPiv.data(), ra);
	const double *col = L.cols.data();
	for (int a = 0; a < np; ++a) {
		if (L.pos[size_t(a)] >= 0)
			continue;
		double s = r[a];
		for (int k = 0; k < nA; ++k)
			s -= col[size_t(L.active[size_t(k)]) * size_t(np) + size_t(a)] * ra[k];
		r[a] = s;
	}
	for (int k = 0; k < nA; ++k)
		r[L.active[size_t(k)]] = ra[k];
}

bool Circuit::newton(Level &L, const double *p, double *v, double *i, int maxIterations)
{
	switch (m_np) {
	case 1: return newtonT<1>(L, p, v, i, maxIterations);
	case 2: return newtonT<2>(L, p, v, i, maxIterations);
	case 3: return newtonT<3>(L, p, v, i, maxIterations);
	case 4: return newtonT<4>(L, p, v, i, maxIterations);
	case 5: return newtonT<5>(L, p, v, i, maxIterations);
	case 6: return newtonT<6>(L, p, v, i, maxIterations);
	case 7: return newtonT<7>(L, p, v, i, maxIterations);
	case 8: return newtonT<8>(L, p, v, i, maxIterations);
	case 9: return newtonT<9>(L, p, v, i, maxIterations);
	case 10: return newtonT<10>(L, p, v, i, maxIterations);
	case 11: return newtonT<11>(L, p, v, i, maxIterations);
	case 13: return newtonT<13>(L, p, v, i, maxIterations);
	case 16: return newtonT<16>(L, p, v, i, maxIterations);
	default: return newtonT<0>(L, p, v, i, maxIterations);
	}
}

template <int NP> bool Circuit::newtonT(Level &L, const double *p, double *v, double *i, int maxIterations)
{
	if (kCheckJac && m_np > 0) {
		// finite-difference check of the devices' Jacobians at the starting point
		std::vector<double> i0(static_cast<size_t>(m_np)), i1(static_cast<size_t>(m_np)), j0(static_cast<size_t>(m_np * m_np), 0.0), j1(static_cast<size_t>(m_np * m_np), 0.0);
		std::vector<double> vv(v, v + m_np);
		deviceCurrents(vv.data(), i0.data(), j0.data());
		for (int c = 0; c < m_np; ++c) {
			const double h = 1e-7;
			vv[size_t(c)] += h;
			deviceCurrents(vv.data(), i1.data(), j1.data());
			vv[size_t(c)] -= h;
			for (int q = 0; q < m_np; ++q) {
				const double num = (i1[size_t(q)] - i0[size_t(q)]) / h, ana = j0[size_t(q * m_np + c)];
				if (std::abs(num - ana) > 1e-4 * (std::abs(num) + std::abs(ana)) + 1e-9)
					std::fprintf(stderr, "jacobian d i%d / d v%d: analytic %.6g numeric %.6g (v %.4g)\n", q, c, ana, num, vv[size_t(c)]);
			}
		}
	}
	// Newton on the port voltages: v = p + F i(v), with SPICE's junction limiting. The Jacobian
	// M = I - F G is factored only over the active ports (whose devices conduct enough for their
	// column of F G to matter; the others' columns are the identity), and that factorisation is
	// kept between steps and reused (the chord method) while it keeps converging, rebuilt when an
	// iteration fails to shrink the residual tenfold. Converged results are exact either way; the
	// approximations only steer the iterations.
	const int np = NP > 0 ? NP : m_np;
	if (np == 0)
		return true;
	const double *F = L.F.data();
	const double *fmax = L.fColMax.data();
	double *r = m_r.data();
	int *sig = m_sigPorts.data();
	double last = 0;
	bool fresh = false, plain = false;
	for (int it = 0; it < maxIterations; ++it) {
		// The first iteration starts where the last step ended: its currents and Jacobian are known.
		if (it > 0 || !m_current)
			deviceCurrents(v, i, m_jac.data());
		m_current = true;
		double worst = 0;
		const double *w = m_portWeight.data();
		if (np <= kDensePorts) { // every current (a gather costs more than it saves at this size)
			// down F's columns (each row's sum in a plain loop's order): r = -((v - p) - F i), as
			// (p - v) + F i (rounding is symmetric: each partial sum is the other's negated)
			double *pv = m_vp.data();
			for (int a = 0; a < np; ++a)
				pv[a] = p[a] - v[a];
			if constexpr (NP > 0) // compiled for the size: F is NP x NP, its columns padded(NP) apart
				colBlock<(NP + 1) / 2, false, false>(L.Fc.d.data(), int(padded(NP)), pv, i, r, NP, nullptr, 0, nullptr, 0);
			else
				colMatVec<false>(L.Fc, 0, np, np, pv, i, r);
		} else {
			// the currents that move any port by more than 0.1 nV
			int ns = 0;
			for (int q = 0; q < np; ++q)
				if (std::abs(i[q]) * fmax[q] > 1e-10)
					sig[ns++] = q;
			int a = 0;
			for (; a + 4 <= np; a += 4) {
				const double *f0 = F + a * np, *f1 = f0 + np, *f2 = f1 + np, *f3 = f2 + np;
				double s0 = v[a] - p[a], s1 = v[a + 1] - p[a + 1], s2 = v[a + 2] - p[a + 2], s3 = v[a + 3] - p[a + 3];
				for (int k = 0; k < ns; ++k) {
					const int q = sig[k];
					const double iq = i[q];
					s0 -= f0[q] * iq;
					s1 -= f1[q] * iq;
					s2 -= f2[q] * iq;
					s3 -= f3[q] * iq;
				}
				r[a] = -s0;
				r[a + 1] = -s1;
				r[a + 2] = -s2;
				r[a + 3] = -s3;
			}
			for (; a < np; ++a) {
				double s = v[a] - p[a];
				const double *f = F + a * np;
				for (int k = 0; k < ns; ++k)
					s -= f[sig[k]] * i[sig[k]];
				r[a] = -s;
			}
		}
		for (int a = 0; a < np; ++a)
			worst = std::max(worst, std::abs(r[a]) * w[a]);
		if (kTrace == 0 || kTrace == np) {
			int wa = 0;
			for (int a = 0; a < np; ++a)
				if (std::abs(r[a]) > std::abs(r[wa]))
					wa = a;
			std::fprintf(stderr, "  it %d worst %.3g at port %d (v %.6g, i %.4g) fresh %d active %d\n", it, worst, wa, v[wa], i[wa],
			             int(fresh), L.nActive);
		}
		if (!(worst < 1e30))
			return false; // NaN or runaway
		if (worst < m_tolerance) {
			m_stats.worstResidual = std::max(m_stats.worstResidual, worst);
			return true;
		}
		++m_stats.iterations;
		// After a chord step that didn't shrink the residual tenfold, every later iteration of
		// this solve gets a fresh factorisation (plain Newton).
		// (A small system factors afresh each iteration, but its first one of a step reuses the
		// last step's factorisation: the operating point has hardly moved.)
		if ((it > 0 && worst > kChordRatio * last && !fresh) || (np <= kPlainPorts && it > 0))
			plain = true;
		if (!L.luValid || plain) {
			if (!factor<NP>(L)) {
				L.luValid = false;
				return false;
			}
			fresh = true;
		} else {
			fresh = false;
		}
		last = worst;
		solveActive<NP>(L, r);
		// Converged when the Newton step itself is negligible (as SPICE's vntol/reltol): a node
		// between two reverse-biased junctions has a huge F entry, so picoamps of current error
		// show as volts of residual there while the voltages are settled.
		double stepMax = 0, vMax = 0;
		for (int a = 0; a < np; ++a) {
			stepMax = std::max(stepMax, std::abs(r[a]) * w[a]);
			vMax = std::max(vMax, std::abs(v[a]) * w[a]);
		}
		if (stepMax < m_tolerance * 1e-2 * (1 + vMax) && fresh) {
			for (int a = 0; a < np; ++a)
				v[a] += r[a];
			deviceCurrents(v, i, m_jac.data());
			m_stats.worstResidual = std::max(m_stats.worstResidual, stepMax);
			return true;
		}
		auto openCollector = [&](int a) { // approach the rail geometrically
			const double vnew = v[a] + r[a];
			v[a] = vnew < 0 && v[a] > 0 ? 0.1 * v[a] : vnew;
		};
		auto limiter = [&](int a) { // a limiter's input, in units of its knee
			const double k = m_portVcrit[size_t(a)];
			const double xo = v[a] * k;
			double xn = (v[a] + r[a]) * k;
			if (std::abs(xn) > 4) {
				if (std::abs(xo) < 4)
					xn = xn > 0 ? 4 : -4;
				else if (xn * xo < 0)
					xn = 0;
			}
			v[a] = xn / k;
		};
		auto junction = [&](int a) {
			const double s = m_portSign[size_t(a)];
			const double vold = s * v[a];
			v[a] = s * limitJunction(vold + s * r[a], vold, m_portVt[size_t(a)], m_portVcrit[size_t(a)]);
		};
		if (NP > 0) { // one pass over the ports (each port's update is its own)
			const char *kind = m_portKind.data();
			for (int a = 0; a < NP; ++a)
				switch (kind[a]) {
				case 0: v[a] += r[a]; break;
				case 1: openCollector(a); break;
				case 2: limiter(a); break;
				default: junction(a); break;
				}
		} else {
			for (int a : m_portClass[0])
				v[a] += r[a];
			for (int a : m_portClass[1])
				openCollector(a);
			for (int a : m_portClass[2])
				limiter(a);
			for (int a : m_portClass[3])
				junction(a);
		}
	}
	deviceCurrents(v, i, m_jac.data());
	return false;
}

void Circuit::syncCaps() const
{
	if (!m_capsStale)
		return;
	m_capsStale = false;
	const Level &L = m_levels[0];
	for (int k = 0; k < m_nx; ++k) {
		const double xn = x()[k], xp = xpLast()[k];
		if (L.be[size_t(k)]) {
			m_vc[size_t(k)] = xn * L.vcScale[size_t(k)];
			m_ic[size_t(k)] = xn - xp;
		} else {
			m_vc[size_t(k)] = (xn + xp) * L.vcScale[size_t(k)];
			m_ic[size_t(k)] = 0.5 * (xn - xp);
		}
	}
}

void Circuit::dropX()
{
	syncCaps();
	m_xValid = false;
}

bool Circuit::advance(int lv, const double *u)
{
	switch (m_np) {
	case 1: return advanceT<1>(lv, u);
	case 2: return advanceT<2>(lv, u);
	case 3: return advanceT<3>(lv, u);
	case 4: return advanceT<4>(lv, u);
	case 5: return advanceT<5>(lv, u);
	case 6: return advanceT<6>(lv, u);
	case 7: return advanceT<7>(lv, u);
	case 8: return advanceT<8>(lv, u);
	case 9: return advanceT<9>(lv, u);
	case 10: return advanceT<10>(lv, u);
	case 11: return advanceT<11>(lv, u);
	case 13: return advanceT<13>(lv, u);
	case 16: return advanceT<16>(lv, u);
	default: return advanceT<0>(lv, u);
	}
}

template <int NP> bool Circuit::advanceT(int lv, const double *u)
{
	Level &L = level(lv);
	const int nx = m_nx, np = NP > 0 ? NP : m_np, nu = m_nu;
	const int c0 = m_constCount, nv = nu - c0;
	// the step's [x | the inputs that vary | (after Newton) the ports' currents], each read where it
	// is kept: the companion states for this step size (the last full step's, or worked out from the
	// capacitors' voltages and currents into m_w), u, m_i
	const bool full = lv == 0 && !m_inEdge; // a full sample: the state stays as companion states
	const double *x = this->x();
	if (!(full && m_xValid)) {
		if (!full)
			dropX();
		syncCaps();
		double *w = m_w.data();
		for (int k = 0; k < nx; ++k)
			w[k] = L.gc[size_t(k)] * m_vc[size_t(k)] + (L.be[size_t(k)] ? 0 : m_ic[size_t(k)]);
		x = w;
	}
	const double *uv = u + c0;
	if constexpr (NP > 0) // the ports' rows, compiled for their count
		colBlock<(NP + 1) / 2, false, false>(L.Pc.d.data(), L.Pc.ld, L.pConst.data(), x, m_p.data(), nx, uv, nv, nullptr, 0);
	else
		colMatVec2<false>(L.Pc, 0, np, nx, x, nv, uv, L.pConst.data(), m_p.data());
	copyN(m_v.data(), np, m_vSave.data());
	copyN(m_i.data(), np, m_iSave.data());
	if (lv == 0 && !m_inEdge && m_prevValid && m_predict.use()) {
		// Start Newton from the ports' straight-line continuation of the last two steps (a
		// resonator's smooth swing), with the junctions limited as Newton's own steps are.
		for (int a = 0; a < np; ++a) {
			const double v = m_v[size_t(a)];
			double vp = v + (v - m_vOld[size_t(a)]);
			const double vt = m_portVt[size_t(a)];
			if (vt > 0) {
				const double sg = m_portSign[size_t(a)];
				vp = sg * limitJunction(sg * vp, sg * v, vt, m_portVcrit[size_t(a)]);
			} else if (vt < 0) {
				vp = v;
			}
			m_v[size_t(a)] = vp;
		}
		m_current = false;
	}
	const long itBefore = m_stats.iterations;
	if (np > 0 && !newtonT<NP>(L, m_p.data(), m_v.data(), m_i.data(), 40)) {
		m_prevValid = false;
		copyN(m_vSave.data(), np, m_v.data());
		copyN(m_iSave.data(), np, m_i.data());
		L.luValid = false;
		m_current = false;
		return false;
	}
	// the next states, then the outputs, into xn() ([x | y])
	const int ny = m_ny;
	double *next = xn();
	colMatVec3<false>(L.Sc, 0, nx + ny, nx, x, nv, uv, np, m_i.data(), L.sConst.data(), next);
	copyN(next + nx, ny, m_y.data());
	if (full) {
		// keep x[n] as the next step's companion states (m_vc/m_ic follow when asked for)
		if (x == this->x()) {
			rotate();
		} else {
			copyN(x, nx, xpLast());
			std::swap(m_bx, m_bxn);
		}
		m_xValid = true;
		m_capsStale = true;
	} else {
		copyN(x, nx, xpLast());
		// back to voltages and currents: x[n] = 2C/T v[n] - x[n-1], i[n] = (x[n] - x[n-1]) / 2
		// (backward Euler: x[n] = C/T v[n], i[n] = x[n] - x[n-1])
		for (int k = 0; k < nx; ++k) {
			const double xn = next[k], xp = x[k];
			if (L.be[size_t(k)]) {
				m_vc[size_t(k)] = xn * L.vcScale[size_t(k)];
				m_ic[size_t(k)] = xn - xp;
				continue;
			}
			m_vc[size_t(k)] = (xn + xp) * L.vcScale[size_t(k)];
			m_ic[size_t(k)] = 0.5 * (xn - xp);
		}
	}
	if (lv == 0 && !m_inEdge) {
		m_predict.count(m_stats.iterations - itBefore);
		if (kNyquist && m_prevValid) {
			for (int a = 0; a < np; ++a) {
				const double d1 = m_v[size_t(a)] - m_vSave[size_t(a)], d0 = m_vSave[size_t(a)] - m_vOld[size_t(a)];
				if (d1 * d0 < 0 && std::abs(d1) > 1e-4 && std::abs(d0) > 1e-4) {
					++m_stats.nyquist;
					break;
				}
			}
		}
		copyN(m_vSave.data(), np, m_vOld.data());
		m_prevValid = true;
	} else {
		m_prevValid = false;
	}
	return true;
}

bool Circuit::advanceSplit(int lv, const double *u0, const double *u1)
{
	if (advance(lv, u1))
		return true;
	if (lv + 1 >= kLevels) {
		// The smallest step failed too: a comparator flipping faster than even it resolves (the
		// clap's multivibrator). Widen the comparators' transitions for this one step, as the DC
		// solve does, rather than leave the step undone.
		bool ok = false;
		for (double soften : {4.0, 16.0, 64.0}) {
			m_soften = soften;
			level(lv).luValid = false;
			m_current = false;
			ok = advance(lv, u1);
			if (ok)
				break;
		}
		m_soften = 1;
		level(lv).luValid = false;
		m_current = false;
		if (ok)
			++m_stats.softened;
		return ok;
	}
	// Four steps of the next level, the inputs moving linearly across them (the buffers are this
	// level's: the recursion below uses the next level's).
	std::vector<double> &u = m_splitTo[size_t(lv)], &from = m_splitFrom[size_t(lv)];
	std::copy(u0, u0 + m_nu, from.begin());
	std::copy(u0, u0 + m_nu, u.begin());
	for (int k = 1; k <= 4; ++k) {
		for (int j = m_constCount; j < m_nu; ++j)
			u[size_t(j)] = u0[j] + (u1[j] - u0[j]) * k / 4.0;
		if (!advanceSplit(lv + 1, from.data(), u.data()))
			return false;
		std::copy(u.begin(), u.end(), from.begin());
	}
	return true;
}

bool Circuit::advanceFour(int lv, const double *u0, const double *u1)
{
	std::vector<double> &from = m_uMid, &to = m_uTo;
	std::copy(u0, u0 + m_nu, from.begin());
	std::copy(u1, u1 + m_nu, to.begin());
	for (int k = 1; k <= 4; ++k) {
		for (int j = m_constCount; j < m_nu; ++j)
			to[size_t(j)] = u0[j] + (u1[j] - u0[j]) * k / 4.0;
		// an edge's first quarter steps by backward Euler (edge())
		m_inEdge = m_edgeLeft > 0;
		if (m_edgeLeft > 0)
			--m_edgeLeft;
		const bool ok = advanceSplit(lv, from.data(), to.data());
		m_inEdge = false;
		if (!ok)
			return false;
		std::copy(to.begin(), to.end(), from.begin());
	}
	return true;
}

bool Circuit::advanceEdge(const double *u1)
{
	// The inputs at their new values from the start, the sample in sixteenths; an edge's first
	// quarters by backward Euler as in advanceFour (counted in quarters).
	for (int q = 0; q < 4; ++q) {
		const bool be = m_edgeLeft > 0;
		if (m_edgeLeft > 0)
			--m_edgeLeft;
		for (int k = 0; k < 4; ++k) {
			m_inEdge = be;
			const bool ok = advanceSplit(2, u1, u1);
			m_inEdge = false;
			if (!ok)
				return false;
		}
	}
	return true;
}

bool Circuit::linearSample()
{
	if (linearStep(m_u.data())) {
		copyN(m_u.data() + m_constCount, m_nu - m_constCount, m_uLast.data() + m_constCount); // the constants never change
		return true;
	}
	leaveLinear();
	return false;
}

void Circuit::stepRest()
{
	buildIfDirty();
	++m_stats.steps;
	const int c0 = m_constCount, nv = m_nu - c0; // the inputs that vary (the constants never change)
	// the common cases first, nothing asked of the sample: a linear step, or a circuit without
	// devices (the step is its products, as advance's)
	if (!m_jump && m_subLeft == 0 && m_edgeLeft == 0) {
		if (m_lin.on) {
			if (linearStep(m_u.data())) {
				copyN(m_u.data() + c0, nv, m_uLast.data() + c0);
				return;
			}
			leaveLinear();
		} else if (m_np == 0 && m_xValid) {
			const Level &L = m_levels[0];
			const int nx = m_nx, ny = m_ny;
			double *next = xn();
			colMatVec3<false>(L.Sc, 0, nx + ny, nx, x(), nv, m_u.data() + c0, 0, nullptr, L.sConst.data(), next);
			copyN(next + nx, ny, m_y.data());
			rotate();
			m_capsStale = true;
			m_prevValid = true;
			copyN(m_u.data() + c0, nv, m_uLast.data() + c0);
			return;
		}
	}
	const double *u0 = m_uLast.data(), *u1 = m_u.data();
	bool ok;
	const bool jump = m_jump && !kNoJump;
	m_jump = false;
	if (jump) {
		if (m_lin.on)
			leaveLinear();
		m_subLeft = std::max(0, m_subLeft - 1);
		++m_stats.substepped;
		ok = advanceEdge(u1);
	} else if (m_subLeft > 0 || m_edgeLeft > 0) {
		if (m_lin.on)
			leaveLinear();
		m_subLeft = std::max(0, m_subLeft - 1);
		++m_stats.substepped;
		ok = advanceFour(1, u0, u1);
	} else {
		if (m_lin.on) {
			if (linearStep(u1)) {
				copyN(m_u.data() + c0, nv, m_uLast.data() + c0);
				return;
			}
			leaveLinear();
		}
		ok = advance(0, u1);
		if (ok && !kLinOff && m_np > 0 && --m_lin.wait <= 0)
			linearize();
		if (!ok) {
			++m_stats.substepped;
			m_subLeft = 32;
			ok = advanceFour(1, u0, u1);
		}
	}
	if (!ok) {
		++m_stats.failures;
		if (kFailDebug)
			std::fprintf(stderr, "%s: step %ld failed\n", m_name.c_str(), m_stats.steps);
	}
	copyN(m_u.data() + c0, nv, m_uLast.data() + c0);
}

bool Circuit::linearize()
{
	// The devices at the last step's solution, and each port's window.
	const int np = m_np, nx = m_nx, nu = m_nu, c0 = m_constCount, nv = nu - c0, ny = m_ny;
	auto &lin = m_lin;
	lin.v0 = m_v;
	lin.i0.assign(size_t(np), 0.0);
	lin.G.assign(size_t(np * np), 0.0);
	deviceCurrents(lin.v0.data(), lin.i0.data(), lin.G.data());
	lin.lo.assign(size_t(np), -1e30);
	lin.hi.assign(size_t(np), 1e30);
	const double *v0 = lin.v0.data();
	auto fail = [&]() {
		lin.wait = lin.backoff;
		lin.backoff = std::min(lin.backoff * 2, 4096);
		return false;
	};
	auto junction = [&](int p, double is, double nvt, double sign) {
		// The window keeps the linearisation's voltage error at the port, I (dv / nV_T)^2 / 2 times the
		// impedance there (the junction's own conductance in parallel with the circuit's, -F_pp),
		// under kLinTolerance; an off junction may rise until its current would make that error.
		const double v = sign * v0[p];
		const double zc = std::max(-m_levels[0].F[size_t(p * m_np + p)], 1e-3);
		const double i = std::max(is * std::expm1(v / nvt), 0.0) + is;
		const double z = 1 / (i / nvt + 1 / zc);
		const double off = nvt * std::log1p(kLinTolerance / zc / is);
		if (v < off) {
			if (sign > 0)
				lin.hi[size_t(p)] = off;
			else
				lin.lo[size_t(p)] = -off;
		} else {
			const double w = std::clamp(nvt * std::sqrt(2 * kLinTolerance / (i * z)), 1e-4, nvt);
			lin.lo[size_t(p)] = v0[p] - w;
			lin.hi[size_t(p)] = v0[p] + w;
		}
	};
	auto window = [&](int p, double w) {
		lin.lo[size_t(p)] = v0[p] - w;
		lin.hi[size_t(p)] = v0[p] + w;
	};
	for (const auto &d : m_devices) {
		const auto &m = *d.model;
		const int p = d.port;
		switch (m.type) {
		case Netlist::Model::Diode:
			junction(p, m.is, m.n * kVt, 1);
			break;
		case Netlist::Model::Npn:
		case Netlist::Model::Pnp:
			junction(p, m.is, m.nf * kVt, d.sign);
			junction(p + 1, m.is, m.nr * kVt, d.sign);
			break;
		case Netlist::Model::Njf:
		case Netlist::Model::Pjf: {
			const double vgs = d.sign * v0[p], vgd = d.sign * v0[p + 1];
			if (vgs < m.vto && vgd < m.vto) { // channel off: stays off, gate junctions too
				const double off = std::min(m.vto, m.n * kVt * std::log1p(kOffCurrent / m.is));
				for (int k = 0; k < 2; ++k) {
					if (d.sign > 0)
						lin.hi[size_t(p + k)] = off;
					else
						lin.lo[size_t(p + k)] = -off;
				}
			} else {
				window(p, kLinWindow);
				window(p + 1, kLinWindow);
			}
			break;
		}
		case Netlist::Model::Slew: {
			// linear while tanh's departure, (r imax) x^3 / 3 port volts, is within the tolerance
			// in the port's weight (1000)
			const double range = m.is / m.gm;
			const double lim = range * std::cbrt(3 * kLinTolerance / (1000 * range));
			if (std::abs(v0[p]) >= lim) {
				if (kLinDebug)
					std::fprintf(stderr, "linear %s: op-amp at port %d slewing\n", m_name.c_str(), p);
				return fail();
			}
			lin.lo[size_t(p)] = -lim;
			lin.hi[size_t(p)] = lim;
			break;
		}
		case Netlist::Model::Limiter: {
			const double knee = (m.vsat - kLimiterKnee) / m.gm;
			if (std::abs(v0[p]) >= knee) {
				if (kLinDebug)
					std::fprintf(stderr, "linear %s: limiter at port %d clipping\n", m_name.c_str(), p);
				return fail();
			}
			lin.lo[size_t(p)] = -knee;
			lin.hi[size_t(p)] = knee;
			break;
		}
		case Netlist::Model::Ota: {
			const double off = kVt * std::log1p(kOffCurrent / m.is);
			if (v0[p] < off) {
				lin.hi[size_t(p)] = off; // no I_abc: the output is off whatever the inputs do
			} else {
				window(p, kLinWindow);
				window(p + 1, kLinWindow);
			}
			break;
		}
		case Netlist::Model::Comparator: {
			// firmly one way: keep its inputs from closing half the margin, and its output (when
			// it sinks) in the window
			auto cm = [&](double x) { return x >= m.vcm ? m.vcm : x; };
			const double vd = cm(v0[p]) - cm(v0[p + 1]) + m.vos;
			const double margin = std::abs(vd) - 20 * m.vg;
			if (margin <= 2 * kLinWindow || std::abs(v0[p] - m.vcm) < 0.5 || std::abs(v0[p + 1] - m.vcm) < 0.5) {
				if (kLinDebug)
					std::fprintf(stderr, "linear %s: comparator at port %d undecided (vd %.4g, in %.4g %.4g)\n", m_name.c_str(), p, vd, v0[p], v0[p + 1]);
				return fail();
			}
			window(p, margin / 2);
			window(p + 1, margin / 2);
			if (vd < 0)
				window(p + 2, kLinWindow);
			break;
		}
		}
	}
	// M = I - F G, and its inverse applied to D, E and the constant part. G is block-diagonal (each
	// device's ports): the terms of column b are those of b's block (the others add exact zeros).
	const Level &L = m_levels[0];
	auto &M = lin.M;
	M.resize(size_t(np * np));
	for (const auto &d : m_devices)
		for (int b = d.port; b < d.port + d.ports; ++b)
			for (int a = 0; a < np; ++a) {
				double s = a == b ? 1.0 : 0.0;
				for (int q = d.port; q < d.port + d.ports; ++q)
					s -= L.F[size_t(a * np + q)] * lin.G[size_t(q * np + b)];
				M[size_t(a * np + b)] = s;
			}
	auto &piv = lin.piv;
	piv.resize(size_t(np));
	if (!luDecompose(M.data(), np, piv.data()))
		return fail();
	auto &c = lin.c, &col = lin.col;
	c.resize(size_t(np));
	col.resize(size_t(np));
	for (int a = 0; a < np; ++a) {
		double s = lin.i0[size_t(a)];
		for (int b = 0; b < np; ++b)
			s -= lin.G[size_t(a * np + b)] * v0[b];
		c[size_t(a)] = s;
	}
	lin.Kx.assign(size_t(np * nx), 0.0);
	lin.Ku.assign(size_t(np * nv), 0.0);
	lin.kv.assign(size_t(np), 0.0);
	for (int k = 0; k < nx; ++k) {
		for (int a = 0; a < np; ++a)
			col[size_t(a)] = L.D[size_t(a * nx + k)];
		luSolve(M.data(), np, piv.data(), col.data());
		for (int a = 0; a < np; ++a)
			lin.Kx[size_t(a * nx + k)] = col[size_t(a)];
	}
	for (int k = 0; k < nv; ++k) {
		for (int a = 0; a < np; ++a)
			col[size_t(a)] = L.E[size_t(a * nu + c0 + k)];
		luSolve(M.data(), np, piv.data(), col.data());
		for (int a = 0; a < np; ++a)
			lin.Ku[size_t(a * nv + k)] = col[size_t(a)];
	}
	for (int a = 0; a < np; ++a) {
		double s = L.pConst[size_t(a)];
		for (int q = 0; q < np; ++q)
			s += L.F[size_t(a * np + q)] * c[size_t(q)];
		col[size_t(a)] = s;
	}
	luSolve(M.data(), np, piv.data(), col.data());
	lin.kv.assign(col.begin(), col.end());
	// The currents i = G v + c as functions of (x, u), then the states and outputs.
	auto &Ix = lin.Ix, &Iu = lin.Iu, &ki = lin.ki;
	Ix.assign(size_t(np * nx), 0.0);
	Iu.assign(size_t(np * nv), 0.0);
	ki.assign(size_t(np), 0.0);
	for (int a = 0; a < np; ++a) {
		for (int q = 0; q < np; ++q) {
			const double g = lin.G[size_t(a * np + q)];
			if (g == 0)
				continue;
			for (int k = 0; k < nx; ++k)
				Ix[size_t(a * nx + k)] += g * lin.Kx[size_t(q * nx + k)];
			for (int k = 0; k < nv; ++k)
				Iu[size_t(a * nv + k)] += g * lin.Ku[size_t(q * nv + k)];
			ki[size_t(a)] += g * lin.kv[size_t(q)];
		}
		ki[size_t(a)] += c[size_t(a)];
	}
	lin.Phi.assign(size_t(nx * nx), 0.0);
	lin.Gam.assign(size_t(nx * nv), 0.0);
	lin.kap.assign(size_t(nx), 0.0);
	for (int r = 0; r < nx; ++r) {
		for (int k = 0; k < nx; ++k) {
			double s = L.A[size_t(r * nx + k)];
			for (int q = 0; q < np; ++q)
				s += L.C[size_t(r * np + q)] * Ix[size_t(q * nx + k)];
			lin.Phi[size_t(r * nx + k)] = s;
		}
		for (int k = 0; k < nv; ++k) {
			double s = L.B[size_t(r * nu + c0 + k)];
			for (int q = 0; q < np; ++q)
				s += L.C[size_t(r * np + q)] * Iu[size_t(q * nv + k)];
			lin.Gam[size_t(r * nv + k)] = s;
		}
		double s = L.xConst[size_t(r)];
		for (int q = 0; q < np; ++q)
			s += L.C[size_t(r * np + q)] * ki[size_t(q)];
		lin.kap[size_t(r)] = s;
	}
	lin.Yx.assign(size_t(ny * nx), 0.0);
	lin.Yu.assign(size_t(ny * nv), 0.0);
	lin.ky.assign(size_t(ny), 0.0);
	for (int o = 0; o < ny; ++o) {
		for (int k = 0; k < nx; ++k) {
			double s = L.Dy[size_t(o * nx + k)];
			for (int q = 0; q < np; ++q)
				s += L.Fy[size_t(o * np + q)] * Ix[size_t(q * nx + k)];
			lin.Yx[size_t(o * nx + k)] = s;
		}
		for (int k = 0; k < nv; ++k) {
			double s = L.Ey[size_t(o * nu + c0 + k)];
			for (int q = 0; q < np; ++q)
				s += L.Fy[size_t(o * np + q)] * Iu[size_t(q * nv + k)];
			lin.Yu[size_t(o * nv + k)] = s;
		}
		double s = L.yConst[size_t(o)];
		for (int q = 0; q < np; ++q)
			s += L.Fy[size_t(o * np + q)] * ki[size_t(q)];
		lin.ky[size_t(o)] = s;
	}
	{
		const int w1 = nx + nv;
		const int vo = m_vOff;
		lin.KM.assign(vo + np, w1);
		lin.kvm.assign(size_t(lin.KM.ld), 0.0);
		for (int r = 0; r < nx; ++r) {
			for (int k = 0; k < nx; ++k)
				lin.KM(r, k) = lin.Phi[size_t(r * nx + k)];
			for (int k = 0; k < nv; ++k)
				lin.KM(r, nx + k) = lin.Gam[size_t(r * nv + k)];
			lin.kvm[size_t(r)] = lin.kap[size_t(r)];
		}
		for (int o = 0; o < ny; ++o) {
			for (int k = 0; k < nx; ++k)
				lin.KM(nx + o, k) = lin.Yx[size_t(o * nx + k)];
			for (int k = 0; k < nv; ++k)
				lin.KM(nx + o, nx + k) = lin.Yu[size_t(o * nv + k)];
			lin.kvm[size_t(nx + o)] = lin.ky[size_t(o)];
		}
		for (int a = 0; a < np; ++a) {
			for (int k = 0; k < nx; ++k)
				lin.KM(vo + a, k) = lin.Kx[size_t(a * nx + k)];
			for (int k = 0; k < nv; ++k)
				lin.KM(vo + a, nx + k) = lin.Ku[size_t(a * nv + k)];
			lin.kvm[size_t(vo + a)] = lin.kv[size_t(a)];
		}
	}
	lin.v = m_v;
	lin.on = true;
	lin.since = 0;
	++m_stats.linearizations;
	return true;
}

bool Circuit::linearStep(const double *u)
{
	auto &lin = m_lin;
	const int np = m_np, nx = m_nx, c0 = m_constCount, nv = m_nu - c0, ny = m_ny;
	// the step's [x | u]: the states (the companion states the last step left, or worked out from
	// the capacitors) and the inputs that vary, read where they are kept
	double *x = this->x();
	if (!m_xValid) {
		const Level &L = m_levels[0];
		syncCaps();
		for (int k = 0; k < nx; ++k)
			x[k] = L.gc[size_t(k)] * m_vc[size_t(k)] + (L.be[size_t(k)] ? 0 : m_ic[size_t(k)]);
	}
	// The next states, the outputs and the ports in one pass down KM's columns, into the next state
	// buffer ([xn | y | v]); if any port leaves its window, the step is Newton's.
	double *f = xn();
	colMatVec2<false>(lin.KM, 0, m_vOff + np, nx, x, nv, u + c0, lin.kvm.data(), f);
	const double *v = f + m_vOff;
	if (anyOutside(v, lin.lo.data(), lin.hi.data(), np)) {
		if (kLinDebug)
			for (int a = 0; a < np; ++a)
				if (v[a] < lin.lo[size_t(a)] || v[a] > lin.hi[size_t(a)]) {
					std::fprintf(stderr, "linear %s: port %d at %.6g left [%.6g, %.6g] (v0 %.6g) after %ld\n", m_name.c_str(), a, v[a],
					             lin.lo[size_t(a)], lin.hi[size_t(a)], lin.v0[size_t(a)], lin.since);
					break;
				}
		return false;
	}
	copyN(f + nx, ny, m_y.data());
	// the step's states become the last step's, its next states the states
	rotate();
	m_xValid = true;
	m_capsStale = true;
	++lin.since;
	++m_stats.linearSteps;
	return true;
}

void Circuit::leaveLinear()
{
	m_prevValid = false;
	// Back to Newton from the last linear step: its ports, and their currents on the linearisation.
	auto &lin = m_lin;
	const int np = m_np;
	// the last linear step's ports (with its state), or the linearisation's if none ran
	if (lin.since > 0)
		copyN(x() + m_vOff, np, lin.v.data());
	m_v = lin.v;
	for (int a = 0; a < np; ++a) {
		double s = lin.i0[size_t(a)];
		for (int b = 0; b < np; ++b)
			s += lin.G[size_t(a * np + b)] * (lin.v[size_t(b)] - lin.v0[size_t(b)]);
		m_i[size_t(a)] = s;
	}
	m_current = false;
	lin.on = false;
	// A long run earns quick re-entry; a short one waits longer each time.
	if (lin.since > 256)
		lin.backoff = 8;
	lin.wait = lin.since > 256 ? 1 : lin.backoff;
	if (lin.since <= 256)
		lin.backoff = std::min(lin.backoff * 2, 4096);
}

void Circuit::solveFull(const double *x, const double *u, const double *i, std::vector<double> &z) const
{
	// S for the sample's step size, then z = S^-1 (Nx x + Nu u + Nn i).
	const int n = m_n;
	const Level &L = m_levels[0];
	std::vector<double> S = m_G;
	for (int k = 0; k < m_nx; ++k) {
		const auto &e = m_net.elements[size_t(m_caps[size_t(k)])];
		const double g = L.gc[size_t(k)];
		const int a = e.nodes[0], b = e.nodes[1];
		if (a > 0) S[size_t((a - 1) * n + a - 1)] += g;
		if (b > 0) S[size_t((b - 1) * n + b - 1)] += g;
		if (a > 0 && b > 0) {
			S[size_t((a - 1) * n + b - 1)] -= g;
			S[size_t((b - 1) * n + a - 1)] -= g;
		}
	}
	std::vector<int> piv(static_cast<size_t>(n));
	z.assign(size_t(n), 0.0);
	if (!luDecompose(S.data(), n, piv.data()))
		return;
	for (int r = 0; r < n; ++r) {
		double s = 0;
		for (int k = 0; k < m_nx; ++k)
			s += m_Nx[size_t(r * m_nx + k)] * x[k];
		for (int k = 0; k < m_nu; ++k)
			s += m_Nu[size_t(r * m_nu + k)] * u[k];
		for (int k = 0; k < m_np; ++k)
			s += m_Nn[size_t(r * m_np + k)] * i[k];
		z[size_t(r)] = s;
	}
	luSolve(S.data(), n, piv.data(), z.data());
}

double Circuit::nodeVoltage(int node) const
{
	if (node <= 0 || m_dirty)
		return 0;
	std::vector<double> z;
	solveFull(xpLast(), m_u.data(), m_i.data(), z);
	return z[size_t(node - 1)];
}

bool Circuit::solveDc()
{
	buildIfDirty();
	// The DC system: no capacitors (open), plus gmin from every node to ground.
	const int n = m_n;
	const int nodes = int(m_net.nodes.size()) - 1;
	std::vector<double> S = m_G;
	for (int k = 0; k < nodes; ++k)
		S[size_t(k * n + k)] += kGmin;
	std::vector<int> piv(static_cast<size_t>(n));
	if (!luDecompose(S.data(), n, piv.data()))
		return false;
	std::vector<double> SiNu(static_cast<size_t>(n * m_nu)), SiNn(static_cast<size_t>(n * m_np)), col(static_cast<size_t>(n));
	for (int c = 0; c < m_nu; ++c) {
		for (int r = 0; r < n; ++r)
			col[size_t(r)] = m_Nu[size_t(r * m_nu + c)];
		luSolve(S.data(), n, piv.data(), col.data());
		for (int r = 0; r < n; ++r)
			SiNu[size_t(r * m_nu + c)] = col[size_t(r)];
	}
	for (int c = 0; c < m_np; ++c) {
		for (int r = 0; r < n; ++r)
			col[size_t(r)] = m_Nn[size_t(r * m_np + c)];
		luSolve(S.data(), n, piv.data(), col.data());
		for (int r = 0; r < n; ++r)
			SiNn[size_t(r * m_np + c)] = col[size_t(r)];
	}
	auto diff = [&](const std::vector<double> &M, int cols, int c, int plus, int minus) {
		double v = 0;
		if (plus > 0) v += M[size_t((plus - 1) * cols + c)];
		if (minus > 0) v -= M[size_t((minus - 1) * cols + c)];
		return v;
	};
	Level dc;
	dc.F.assign(size_t(m_np * m_np), 0.0);
	dc.luM.assign(size_t(m_np * m_np), 0.0);
	dc.luPiv.assign(size_t(m_np), 0);
	dc.cols.assign(size_t(m_np * m_np), 0.0);
	dc.active.assign(size_t(m_np), 0);
	dc.pos.assign(size_t(m_np), -1);
	dc.fColMax.assign(size_t(m_np), 1e12); // every current counts in the DC solve
	std::vector<double> E(static_cast<size_t>(m_np * m_nu));
	for (int p = 0; p < m_np; ++p) {
		for (int c = 0; c < m_nu; ++c)
			E[size_t(p * m_nu + c)] = diff(SiNu, m_nu, c, m_portPlus[size_t(p)], m_portMinus[size_t(p)]);
		for (int c = 0; c < m_np; ++c)
			dc.F[size_t(p * m_np + c)] = diff(SiNn, m_np, c, m_portPlus[size_t(p)], m_portMinus[size_t(p)]);
	}
	toColumns(dc.F.data(), m_np, m_np, m_np, dc.Fc);
	dc.FT.assign(size_t(m_np * m_np), 0.0);
	for (int a = 0; a < m_np; ++a)
		for (int q = 0; q < m_np; ++q)
			dc.FT[size_t(q * m_np + a)] = dc.F[size_t(a * m_np + q)];
	// Newton, stepping the sources up from zero if a direct solve fails.
	std::vector<double> v(static_cast<size_t>(m_np), 0.0), i(static_cast<size_t>(m_np), 0.0), p(static_cast<size_t>(m_np), 0.0);
	auto solveAt = [&](double scale) {
		for (int a = 0; a < m_np; ++a) {
			double s = 0;
			for (int k = 0; k < m_nu; ++k)
				s += E[size_t(a * m_nu + k)] * m_u[size_t(k)] * scale;
			p[size_t(a)] = s;
		}
		dc.luValid = false;
		m_current = false;
		return newton(dc, p.data(), v.data(), i.data(), 200);
	};
	bool ok = solveAt(1);
	if (!ok) {
		std::fill(v.begin(), v.end(), 0.0);
		ok = true;
		for (int s = 1; s <= 200 && ok; ++s)
			ok = solveAt(s / 200.0);
	}
	if (!ok) {
		// Comparators: start them soft (wide transitions) and sharpen them step by step.
		std::fill(v.begin(), v.end(), 0.0);
		ok = true;
		for (double soften = 1e4; soften >= 1 && ok; soften /= 2) {
			m_soften = soften;
			ok = solveAt(1);
			if (!ok) {
				ok = true;
				for (int s = 1; s <= 50 && ok; ++s)
					ok = solveAt(s / 50.0);
			}
		}
		if (ok) {
			m_soften = 1;
			ok = solveAt(1);
		}
		m_soften = 1;
	}
	if (!ok) {
		if (std::getenv("TR808_DEBUG"))
			std::fprintf(stderr, "DC: Newton failed\n");
		return false;
	}
	std::vector<double> z(static_cast<size_t>(n));
	for (int r = 0; r < n; ++r) {
		double s = 0;
		for (int k = 0; k < m_nu; ++k)
			s += SiNu[size_t(r * m_nu + k)] * m_u[size_t(k)];
		for (int k = 0; k < m_np; ++k)
			s += SiNn[size_t(r * m_np + k)] * i[size_t(k)];
		z[size_t(r)] = s;
	}
	auto nodeV = [&](int k) { return k > 0 ? z[size_t(k - 1)] : 0.0; };
	m_capsStale = false;
	m_xValid = false;
	for (int k = 0; k < m_nx; ++k) {
		const auto &e = m_net.elements[size_t(m_caps[size_t(k)])];
		m_vc[size_t(k)] = nodeV(e.nodes[0]) - nodeV(e.nodes[1]);
		m_ic[size_t(k)] = 0;
	}
	for (int k = 0; k < m_nx; ++k)
		xpLast()[k] = m_levels[0].gc[size_t(k)] * m_vc[size_t(k)];
	m_v = v;
	m_i = i;
	m_current = false;
	m_lin.on = false;
	m_lin.wait = 0;
	m_uLast = m_u;
	for (auto &L : m_levels)
		L.luValid = false;
	for (int o = 0; o < m_ny; ++o) {
		const auto &out = m_net.outputs[size_t(o)];
		m_y[size_t(o)] = nodeV(out.plus) - nodeV(out.minus);
	}
	return true;
}

std::string Circuit::toSpice(const std::vector<std::vector<std::pair<double, double>>> &inputWaves, double seconds,
                             double step, const std::string &dataFile) const
{
	auto node = [this](int k) {
		if (k == 0)
			return std::string("0");
		std::string s = m_net.nodes[size_t(k)];
		for (char &c : s)
			if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
				c = '_';
		return "n_" + s;
	};
	std::ostringstream o;
	o << "* exported by tr808::circuit::Circuit::toSpice\n";
	o << ".options reltol=1e-6 abstol=1e-13 vntol=1e-9 gmin=1e-12 method=trap cshunt=1e-12 rshunt=1e9\n";
	for (const auto &[name, m] : m_net.models) {
		if (m.type == Netlist::Model::Ota || m.type == Netlist::Model::Comparator || m.type == Netlist::Model::Limiter
		    || m.type == Netlist::Model::Slew)
			continue;
		if (m.type == Netlist::Model::Diode) {
			o << ".model " << name << " D(IS=" << fmt(m.is) << " N=" << fmt(m.n) << ")\n";
		} else if (m.type == Netlist::Model::Njf || m.type == Netlist::Model::Pjf) {
			o << ".model " << name << (m.type == Netlist::Model::Njf ? " NJF" : " PJF") << "(VTO=" << fmt(m.vto)
			  << " BETA=" << fmt(m.beta) << " LAMBDA=" << fmt(m.lambda) << " IS=" << fmt(m.is) << " N=" << fmt(m.n) << ")\n";
		} else {
			o << ".model " << name << (m.type == Netlist::Model::Npn ? " NPN" : " PNP") << "(IS=" << fmt(m.is)
			  << " BF=" << fmt(m.bf) << " BR=" << fmt(m.br) << " NF=" << fmt(m.nf) << " NR=" << fmt(m.nr);
			if (m.vaf > 0) o << " VAF=" << fmt(m.vaf);
			if (m.ise > 0) o << " ISE=" << fmt(m.ise) << " NE=" << fmt(m.ne);
			if (m.isc > 0) o << " ISC=" << fmt(m.isc) << " NC=" << fmt(m.nc);
			o << ")\n";
		}
	}
	int constIndex = 0;
	for (const auto &e : m_net.elements) {
		const std::string nm = e.name;
		switch (e.kind) {
		case 'R':
			o << nm << " " << node(e.nodes[0]) << " " << node(e.nodes[1]) << " " << fmt(e.value) << "\n";
			break;
		case 'C':
			o << nm << " " << node(e.nodes[0]) << " " << node(e.nodes[1]) << " " << fmt(e.value) << "\n";
			break;
		case 'P': {
			const double share = potShare(m_controls[size_t(e.control)], e.taperMid);
			o << "R" << nm << "_aw " << node(e.nodes[0]) << " " << node(e.nodes[1]) << " "
			  << fmt(std::max(e.value * (1 - share), 0.1)) << "\n";
			o << "R" << nm << "_wb " << node(e.nodes[1]) << " " << node(e.nodes[2]) << " "
			  << fmt(std::max(e.value * share, 0.1)) << "\n";
			break;
		}
		case 'S': {
			const bool closed = (m_controls[size_t(e.control)] >= 0.5) != e.invert;
			o << "R" << nm << " " << node(e.nodes[0]) << " " << node(e.nodes[1]) << " " << fmt(closed ? e.ron : e.roff) << "\n";
			break;
		}
		case 'D':
			o << nm << " " << node(e.nodes[0]) << " " << node(e.nodes[1]) << " " << e.model << "\n";
			break;
		case 'Q':
		case 'J':
			o << nm << " " << node(e.nodes[0]) << " " << node(e.nodes[1]) << " " << node(e.nodes[2]) << " " << e.model << "\n";
			break;
		case 'B': {
			const auto &m = m_net.models.at(e.model);
			const std::string id = e.name.substr(1);
			if (m.type == Netlist::Model::Slew) {
				o << "Bslew_" << id << " " << node(e.nodes[0]) << " " << node(e.nodes[1]) << " I = " << fmt(m.is) << " * tanh(V("
				  << node(e.nodes[0]) << ", " << node(e.nodes[1]) << ") / " << fmt(m.is / m.gm) << ")\n";
			} else if (m.type == Netlist::Model::Limiter) {
				const std::string x = fmt(m.gm) + " * V(" + node(e.nodes[0]) + ")";
				const double knee = m.vsat - kLimiterKnee;
				o << "Blim_" << id << " " << node(e.nodes[1]) << " 0 I = V(" << node(e.nodes[1]) << ") - (abs(" << x << ") < " << fmt(knee)
				  << " ? " << x << " : sgn(" << x << ") * (" << fmt(knee) << " + " << fmt(kLimiterKnee) << " * tanh((abs(" << x << ") - "
				  << fmt(knee) << ") / " << fmt(kLimiterKnee) << ")))\n";
				const std::string z = "((abs(" + x + ") - " + fmt(m.vsat + kWindupMargin) + ") / " + fmt(kWindupSoft) + ")";
				o << "Baw_" << id << " " << node(e.nodes[0]) << " 0 I = sgn(" << x << ") * " << fmt(kWindupG * kWindupSoft) << " * (max(" << z
				  << ", 0) + ln(1 + exp(-abs(" << z << "))))\n";
			} else if (m.type == Netlist::Model::Ota) {
				o << "Vabc_" << id << " " << node(e.nodes[0]) << " n_" << id << "_abc 0\n";
				o << "Dabc_" << id << " n_" << id << "_abc " << node(e.nodes[1]) << " dabc_" << id << "\n";
				o << ".model dabc_" << id << " D(IS=" << fmt(m.is) << " N=1)\n";
				o << "Bout_" << id << " 0 " << node(e.nodes[4]) << " I = " << fmt(m.gm) << " * max(I(Vabc_" << id
				  << "), 0) * tanh((V(" << node(e.nodes[2]) << ") - V(" << node(e.nodes[3]) << ")) / " << fmt(2 * kVt) << ")\n";
			} else {
				// vcm - w ln(1 + e^z), z = (vcm - V) / w, as max(z, 0) + ln(1 + e^-|z|) so that inputs far
				// below vcm read as themselves (the solver's cm())
				auto in = [&](int n) {
					const std::string z = "((" + fmt(m.vcm) + " - V(" + node(n) + ")) / 0.05)";
					return "(" + fmt(m.vcm) + " - 0.05 * (max(" + z + ", 0) + ln(1 + exp(-abs(" + z + ")))))";
				};
				o << "Bcmp_" << id << " " << node(e.nodes[2]) << " " << node(e.nodes[3]) << " I = " << fmt(m.isink)
				  << " / (1 + exp(min(max((" << in(e.nodes[0]) << " - " << in(e.nodes[1]) << " + " << fmt(m.vos) << ") / "
				  << fmt(m.vg) << ", -60), 60))) * (V(" << node(e.nodes[2]) << ", " << node(e.nodes[3]) << ") > 0 ? tanh(V("
				  << node(e.nodes[2]) << ", " << node(e.nodes[3]) << ") / " << fmt(m.vsat) << ") : V(" << node(e.nodes[2]) << ", "
				  << node(e.nodes[3]) << ") / " << fmt(m.vsat) << ")\n";
			}
			break;
		}
		case 'V':
		case 'I': {
			o << nm << " " << node(e.nodes[0]) << " " << node(e.nodes[1]) << " ";
			if (e.input < 0) {
				o << "DC " << fmt(m_u[size_t(constIndex++)]) << "\n";
				break;
			}
			const auto *wave = size_t(e.input) < inputWaves.size() ? &inputWaves[size_t(e.input)] : nullptr;
			if (wave == nullptr || wave->empty()) {
				o << "DC " << fmt(m_u[size_t(m_constCount + e.input)]) << "\n";
				break;
			}
			o << "PWL(";
			for (const auto &[t, v] : *wave)
				o << " " << fmt(t) << " " << fmt(v);
			o << " )\n";
			break;
		}
		case 'E':
			o << nm << " " << node(e.nodes[0]) << " " << node(e.nodes[1]) << " " << node(e.nodes[2]) << " "
			  << node(e.nodes[3]) << " " << fmt(e.value) << "\n";
			break;
		case 'X':
			o << "E" << nm << " " << node(e.nodes[2]) << " 0 " << node(e.nodes[0]) << " " << node(e.nodes[1]) << " 1e7\n";
			break;
		default:
			break;
		}
	}
	o << ".tran " << fmt(step) << " " << fmt(seconds) << " 0 " << fmt(step) << "\n";
	o << ".control\nset wr_singlescale\nset wr_vecnames\nrun\nwrdata " << dataFile;
	for (const auto &out : m_net.outputs) {
		if (out.minus == 0)
			o << " v(" << node(out.plus) << ")";
		else
			o << " v(" << node(out.plus) << "," << node(out.minus) << ")";
	}
	o << "\n.endc\n.end\n";
	return o.str();
}

} // namespace tr808::circuit
