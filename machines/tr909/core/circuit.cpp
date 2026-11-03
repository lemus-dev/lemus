#include "circuit.h"

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#else
#include <emmintrin.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace tr909::circuit {

namespace {

constexpr double kVt = 0.025864186; // kT/q at 27 C, as SPICE's nominal temperature
constexpr double kGmin = 1e-12;     // across every junction, as SPICE adds it
constexpr double kGshunt = 1e-9;    // from each node with no DC path to ground (see load)
// A port the linear network leaves this loose (its self-impedance there, with every node shunted
// by kGshunt, is over kAnchorR) gets an anchor of kAnchorG (see Circuit::m_anchor, build).
constexpr double kAnchorR = 1e8, kAnchorG = 1e-5;
// Linear mode (Circuit::linearize): kLinTolerance is the voltage error a junction's linearisation
// may make at its port before the step goes back to Newton; kLinWindow the window of the ports
// with no better estimate (JFETs and MOSFETs that conduct, OTA inputs, a sinking comparator's
// output); kOffCurrent the current under which a JFET's gate or an OTA's control counts as off.
const double kLinWindow = std::getenv("TR909_LINWINDOW") ? std::atof(std::getenv("TR909_LINWINDOW")) : 2e-3;
const double kLinTolerance = std::getenv("TR909_LINTOL") ? std::atof(std::getenv("TR909_LINTOL")) : 1e-4; // volts
const bool kLinOff = std::getenv("TR909_NOLINEAR") != nullptr;
const bool kMosWindows = std::getenv("TR909_NOMOSWINDOWS") == nullptr; // tools: MOSFETs' windows kLinWindow
const bool kSlewWindows = std::getenv("TR909_NOSLEWWINDOWS") == nullptr; // tools: an op-amp past its linear range to Newton
const bool kRowsStale = std::getenv("TR909_NOSTALEROWS") == nullptr; // tools: M kept at every update
// Tools' diagnostics, read once (not behind a function-local static's guard on every call).
const bool kCheckJac = std::getenv("TR909_CHECKJAC") != nullptr;
constexpr int kDensePorts = 16; // Newton's residual over all currents up to this many ports
const int kTrace = std::getenv("TR909_TRACE") ? std::atoi(std::getenv("TR909_TRACE")) : -1;
const bool kNyquist = std::getenv("TR909_NYQ") != nullptr;
const bool kFailDebug = std::getenv("TR909_FAILDEBUG") != nullptr;
const bool kLinDebug = std::getenv("TR909_LINDEBUG") != nullptr;
const bool kNoJump = std::getenv("TR909_NOJUMP") != nullptr; // tools: edges as ramps across their sample
const bool kNoFetLim = std::getenv("TR909_NOFETLIM") != nullptr; // tools: MOSFETs' ports not limited
const bool kDcDebug = std::getenv("TR909_DEBUG") != nullptr;
const bool kNoFlips = std::getenv("TR909_NOFLIPS") != nullptr; // tools: comparators as other op-amps
// A comparator's flip: its output moving this far in a sample; the steps a flip's sample is redone in
// (a power of 4 up to 64), and the feedback (output to input, in the sample-rate network) over which
// an op-amp counts as one.
constexpr double kFlipVolts = 0.5;
// The sample after a redone one skips its trial while a comparator's output is short of its rail or
// its input stage within this many linear ranges (the regenerative start): further out, its input
// still on its way to the threshold with the output at the rail, the sample is an ordinary one.
constexpr double kMidFlipRanges = 2;
// flipAhead: a comparator's input predicted within this many linear ranges of its threshold
const double kAheadZone = std::getenv("TR909_AHEADZONE") ? std::atof(std::getenv("TR909_AHEADZONE")) : 1;
const int kFlipSteps = std::getenv("TR909_FLIPSTEPS") ? std::atoi(std::getenv("TR909_FLIPSTEPS")) : 16;
// Newton's iterations for a step, and for a trial step that is redone in smaller ones if it fails
constexpr int kMaxIterations = 40;
const int kTrialIterations = std::getenv("TR909_TRIALIT") ? std::atoi(std::getenv("TR909_TRIALIT")) : 8;
constexpr double kComparatorBeta = -0.01;
constexpr double kSlewSat = 10; // linear ranges: an op-amp's input stage counts as saturated past this
// Linear mode: a run of more samples than this pays for its linearisation (a few Newton steps' worth)
const long kLinPays = std::getenv("TR909_LINPAYS") ? std::atol(std::getenv("TR909_LINPAYS")) : 16;
// Linear mode's updates (Circuit::linearStep, relinearize): a device leaving its window is
// linearised again where it went, up to kRelinDevices devices a step and kRelinRounds times, and
// the whole circuit afresh after kRelinRefresh such updates.
const bool kRelin = std::getenv("TR909_NORELIN") == nullptr;
constexpr int kRelinDevices = 4, kRelinRounds = 2;
constexpr double kLinVoltsMax = 1e4;   // a port beyond this in a linear step: not linear
// The updates' rate is a decaying average over about 1 / kRelinRateK steps.
constexpr double kRelinRateK = 1.0 / 32;
constexpr double kRelinPivot = 1e-6;   // an update whose S = I - dG Z_P has a pivot under this: refused
const long kRelinRefresh = std::getenv("TR909_RELINREFRESH") ? std::atol(std::getenv("TR909_RELINREFRESH")) : 256;
constexpr double kOffCurrent = 1e-10;
constexpr double kLimiterKnee = 1.5; // volts: an op-amp output's soft clipping starts this far below vsat
// Anti-windup: past vsat + kWindupMargin (in output volts) the limiter draws kWindupG amps a volt
// from the op-amp's internal pole node (softly, over kWindupSoft), as a real op-amp's internal
// stages saturate: a clipped op-amp comes back within a sample instead of after its integrator
// unwinds (125 us for the narrowed trig at accent).
constexpr double kWindupMargin = 2.5, kWindupSoft = 0.05, kWindupG = 1e-5;
// Small systems factor afresh every iteration (plain Newton): it costs less than the chord
// method's extra iterations.
const int kPlainPorts = std::getenv("TR909_PLAIN") ? std::atoi(std::getenv("TR909_PLAIN")) : 6;
const double kChordRatio = std::getenv("TR909_CHORD") ? std::atof(std::getenv("TR909_CHORD")) : 0.1;

// Tools: TR909_HIST prints, at exit, each circuit's samples by how they were stepped, with their
// Newton iterations, and why linear mode ended or couldn't start.
const bool kHist = std::getenv("TR909_HIST") != nullptr;
enum { kHLinear, kHFull, kHFullFail, kHQuarters, kHJump, kHFlipRedo, kHFlipAhead, kHKinds };
struct HistEntry {
	long n[kHKinds] = {}, it[kHKinds] = {};
	double ns[kHKinds] = {}; // time in each kind of step (nanoseconds)
	std::map<std::string, long> exits, fails, relins;
	long waitSteps = 0;
	int sizes[5] = {}; // unknowns, states, ports, varying inputs, outputs
};
std::map<std::string, HistEntry> &histAll();
// this circuit's entry, or a scratch one for an unnamed circuit (the cymbal clocks' timing runs on
// threads; their circuits have no names)
HistEntry &histOf(const std::string &name)
{
	static thread_local HistEntry scratch;
	return name.empty() ? scratch : histAll()[name];
}
std::map<std::string, HistEntry> &histAll()
{
	static std::map<std::string, HistEntry> h;
	static bool reg = false;
	if (!reg) {
		reg = true;
		std::atexit([] {
			static const char *nm[] = {"linear", "full", "full failed", "quarters", "jump", "flip redo", "flip ahead"};
			for (const auto &[name, e] : histAll()) {
				long tot = 0, its = 0;
				for (int k = 0; k < kHKinds; ++k) {
					tot += e.n[k];
					its += e.it[k];
				}
				double ns = 0;
				for (int k = 0; k < kHKinds; ++k)
					ns += e.ns[k];
				std::fprintf(stderr, "%-12s %8ld samples %8ld its (%.2f/sample) %.1f ms  [n %d x %d p %d u %d y %d]\n", name.c_str(), tot, its, tot ? double(its) / tot : 0.0, 1e-6 * ns,
				             e.sizes[0], e.sizes[1], e.sizes[2], e.sizes[3], e.sizes[4]);
				for (int k = 0; k < kHKinds; ++k)
					if (e.n[k])
						std::fprintf(stderr, "    %-12s %8ld samples %8ld its (%.2f) %8.1f ms %6.0f ns/sample\n", nm[k], e.n[k], e.it[k], double(e.it[k]) / e.n[k],
						             1e-6 * e.ns[k], e.ns[k] / double(e.n[k]));
				if (e.waitSteps)
					std::fprintf(stderr, "    full while waiting to linearise: %ld\n", e.waitSteps);
				for (const auto &[why, c] : e.exits)
					std::fprintf(stderr, "    exit %-30s %ld\n", why.c_str(), c);
				for (const auto &[why, c] : e.fails)
					std::fprintf(stderr, "    no linear: %-25s %ld\n", why.c_str(), c);
				for (const auto &[why, c] : e.relins)
					std::fprintf(stderr, "    relinearised %-23s %ld\n", why.c_str(), c);
			}
		});
	}
	return h;
}

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
[[gnu::always_inline]] inline double madd(double s, double a, double x) { return __builtin_fma(a, x, s); }
[[gnu::always_inline]] inline double msub(double s, double a, double x) { return __builtin_fma(-a, x, s); }
#else
using Pair = __m128d;
[[gnu::always_inline]] inline Pair pairLoad(const double *p) { return _mm_loadu_pd(p); }
[[gnu::always_inline]] inline void pairStore(double *p, Pair v) { _mm_storeu_pd(p, v); }
[[gnu::always_inline]] inline Pair pairDup(double x) { return _mm_set1_pd(x); }
[[gnu::always_inline]] inline Pair pairMadd(Pair s, Pair a, Pair x) { return _mm_add_pd(s, _mm_mul_pd(a, x)); }
[[gnu::always_inline]] inline Pair pairMsub(Pair s, Pair a, Pair x) { return _mm_sub_pd(s, _mm_mul_pd(a, x)); }
[[gnu::always_inline]] inline double madd(double s, double a, double x) { return s + a * x; }
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
		const double *__restrict pk = a + k * n;
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

// out[r] = (acc ? out[r] : c[r]) + A[r][:] x for rows r of stride `stride`: four rows a pass, each
// row's sum in the order of a plain loop (the four chains run side by side, x loaded once).
template <bool acc>
inline void matVec(const double *__restrict A, int stride, const double *__restrict c, const double *__restrict x, double *__restrict out, int rows,
                   int cols)
{
	int r = 0;
	for (; r + 4 <= rows; r += 4) {
		const double *a0 = A + size_t(r) * size_t(stride), *a1 = a0 + stride, *a2 = a1 + stride, *a3 = a2 + stride;
		double s0 = acc ? out[r] : c[r], s1 = acc ? out[r + 1] : c[r + 1], s2 = acc ? out[r + 2] : c[r + 2], s3 = acc ? out[r + 3] : c[r + 3];
		for (int k = 0; k < cols; ++k) {
			const double xk = x[k];
			s0 += a0[k] * xk;
			s1 += a1[k] * xk;
			s2 += a2[k] * xk;
			s3 += a3[k] * xk;
		}
		out[r] = s0;
		out[r + 1] = s1;
		out[r + 2] = s2;
		out[r + 3] = s3;
	}
	for (; r < rows; ++r) {
		const double *a0 = A + size_t(r) * size_t(stride);
		double s0 = acc ? out[r] : c[r];
		for (int k = 0; k < cols; ++k)
			s0 += a0[k] * x[k];
		out[r] = s0;
	}
}

// out[r] = (acc ? out[r] : c[r]) + A[r][:cols] x for rows r0 .. r0 + rows - 1 of a ColMat (out and c
// from those rows' first), down the columns two rows at a time, up to 16 rows a pass. Each row's
// sum is in the order of a plain loop, with a fused multiply-add at each term where the machine
// has one (as the compiler contracts the plain loop's).
template <int NA, bool acc, bool sub>
[[gnu::always_inline]] inline void colBlock(const double *__restrict a, int ld, const double *__restrict c, const double *__restrict x,
                                            double *__restrict out, int cols, const double *__restrict x2, int cols2)
{
	Pair s[NA];
	for (int j = 0; j < NA; ++j)
		s[j] = pairLoad((acc ? out : c) + 2 * j);
	for (int k = 0; k < cols; ++k) {
		const Pair xk = pairDup(x[k]);
		const double *ak = a + size_t(k) * size_t(ld);
		for (int j = 0; j < NA; ++j)
			s[j] = sub ? pairMsub(s[j], pairLoad(ak + 2 * j), xk) : pairMadd(s[j], pairLoad(ak + 2 * j), xk);
	}
	for (int k = 0; k < cols2; ++k) {
		const Pair xk = pairDup(x2[k]);
		const double *ak = a + size_t(cols + k) * size_t(ld);
		for (int j = 0; j < NA; ++j)
			s[j] = sub ? pairMsub(s[j], pairLoad(ak + 2 * j), xk) : pairMadd(s[j], pairLoad(ak + 2 * j), xk);
	}
	for (int j = 0; j < NA; ++j)
		pairStore(out + 2 * j, s[j]);
}

// (sub: out[r] = c[r] - A[r][:cols] x, each term subtracted in turn.) colMatVec2: x in two parts,
// the first `cols` columns' x then the next `cols2` columns' x2 (a step's states and inputs, where
// they are kept).
template <bool acc, bool sub = false>
void colMatVec2(const ColMat &A, int r0, int rows, int cols, const double *__restrict x, int cols2, const double *__restrict x2,
                const double *__restrict c, double *__restrict out)
{
	const int ld = A.ld;
	const double *a = A.d.data() + r0;
	int r = 0;
	for (; r + 16 <= rows; r += 16)
		colBlock<8, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2);
	switch ((rows - r) / 2) {
	case 1: colBlock<1, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2); break;
	case 2: colBlock<2, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2); break;
	case 3: colBlock<3, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2); break;
	case 4: colBlock<4, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2); break;
	case 5: colBlock<5, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2); break;
	case 6: colBlock<6, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2); break;
	case 7: colBlock<7, acc, sub>(a + r, ld, c + r, x, out + r, cols, x2, cols2); break;
	default: break;
	}
	r += (rows - r) & ~1;
	if (r < rows) { // an odd last row
		double s = acc ? out[r] : c[r];
		for (int k = 0; k < cols; ++k)
			s = sub ? msub(s, a[size_t(k) * size_t(ld) + size_t(r)], x[k]) : madd(s, a[size_t(k) * size_t(ld) + size_t(r)], x[k]);
		for (int k = 0; k < cols2; ++k)
			s = sub ? msub(s, a[size_t(cols + k) * size_t(ld) + size_t(r)], x2[k]) : madd(s, a[size_t(cols + k) * size_t(ld) + size_t(r)], x2[k]);
		out[r] = s;
	}
}

template <bool acc, bool sub = false>
inline void colMatVec(const ColMat &A, int r0, int rows, int cols, const double *__restrict c, const double *__restrict x, double *__restrict out)
{
	colMatVec2<acc, sub>(A, r0, rows, cols, x, 0, nullptr, c, out);
}

// Whether any of x[k] is outside [lo[k], hi[k]], or not under `big` in size (NaN included), two at a time.
[[gnu::always_inline]] inline bool anyOutside(const double *x, const double *lo, const double *hi, int n, double big)
{
#if defined(__aarch64__) || defined(_M_ARM64)
	const float64x2_t b = vdupq_n_f64(big);
	uint64x2_t bad = vdupq_n_u64(0);
	int k = 0;
	for (; k + 2 <= n; k += 2) {
		const float64x2_t v = vld1q_f64(x + k);
		bad = vorrq_u64(bad, vorrq_u64(vcltq_f64(v, vld1q_f64(lo + k)), vcgtq_f64(v, vld1q_f64(hi + k))));
		bad = vorrq_u64(bad, veorq_u64(vcaltq_f64(v, b), vdupq_n_u64(~0ull)));
	}
	bool any = (vgetq_lane_u64(bad, 0) | vgetq_lane_u64(bad, 1)) != 0;
	if (k < n)
		any = any || x[k] < lo[k] || x[k] > hi[k] || !(std::abs(x[k]) < big);
	return any;
#else
	for (int k = 0; k < n; ++k)
		if (x[k] < lo[k] || x[k] > hi[k] || !(std::abs(x[k]) < big))
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

// The Shichman-Hodges channel of SPICE's level-1 JFET and MOSFET (jfetload, mos1load): the current
// from drain to source, beta vgst^2 (1 + lambda vds) in saturation, and its derivatives in vgs and
// vgd, all in the device's sign frame (vto the threshold there); past vds < 0 the drain and source
// swap roles.
[[gnu::always_inline]] inline void channel(double beta, double vto, double lambda, double vgs, double vgd, double &ids,
                                           double &dvgs, double &dvgd)
{
	ids = 0, dvgs = 0, dvgd = 0;
	const double vds = vgs - vgd;
	if (vds >= 0) {
		const double vgst = vgs - vto;
		if (vgst > 0) {
			const double l = 1 + lambda * vds;
			if (vds < vgst) { // linear
				ids = beta * vds * (2 * vgst - vds) * l;
				const double dIdvds = beta * ((2 * vgst - 2 * vds) * l + vds * (2 * vgst - vds) * lambda);
				const double dIdvgst = beta * 2 * vds * l;
				dvgs = dIdvgst + dIdvds;
				dvgd = -dIdvds;
			} else { // saturation
				ids = beta * vgst * vgst * l;
				const double dIdvds = beta * vgst * vgst * lambda;
				dvgs = beta * 2 * vgst * l + dIdvds;
				dvgd = -dIdvds;
			}
		}
	} else { // drain and source swap roles
		const double vgdt = vgd - vto, vsd = -vds;
		if (vgdt > 0) {
			const double l = 1 + lambda * vsd;
			double isd, dIdvsd, dIdvgdt;
			if (vsd < vgdt) {
				isd = beta * vsd * (2 * vgdt - vsd) * l;
				dIdvsd = beta * ((2 * vgdt - 2 * vsd) * l + vsd * (2 * vgdt - vsd) * lambda);
				dIdvgdt = beta * 2 * vsd * l;
			} else {
				isd = beta * vgdt * vgdt * l;
				dIdvsd = beta * vgdt * vgdt * lambda;
				dIdvgdt = beta * 2 * vgdt * l;
			}
			ids = -isd; // vsd = vgd - vgs
			dvgd = -(dIdvgdt + dIdvsd);
			dvgs = dIdvsd;
		}
	}
}

// SPICE's fetlim: a MOSFET's gate voltage doesn't cross its threshold, or move far beyond it, in one
// Newton step.
double fetLimit(double vnew, double vold, double vto)
{
	const double vtsthi = std::abs(2 * (vold - vto)) + 2;
	const double vtstlo = vtsthi / 2 + 2;
	const double vtox = vto + 3.5;
	const double delv = vnew - vold;
	if (vold >= vto) {
		if (vold >= vtox) {
			if (delv <= 0) { // going off
				if (vnew >= vtox) {
					if (-delv > vtstlo)
						vnew = vold - vtstlo;
				} else {
					vnew = std::max(vnew, vto + 2);
				}
			} else if (delv >= vtsthi) { // staying on
				vnew = vold + vtsthi;
			}
		} else { // the middle region
			vnew = delv <= 0 ? std::max(vnew, vto - 0.5) : std::min(vnew, vto + 4);
		}
	} else if (delv <= 0) { // off
		if (-delv > vtsthi)
			vnew = vold - vtsthi;
	} else {
		const double vtemp = vto + 0.5;
		if (vnew <= vtemp) {
			if (delv > vtstlo)
				vnew = vold + vtstlo;
		} else {
			vnew = vtemp;
		}
	}
	return vnew;
}

// SPICE's limvds: a MOSFET's drain-source voltage grows at most about threefold a Newton step.
double limitVds(double vnew, double vold)
{
	if (vold >= 3.5) {
		if (vnew > vold)
			vnew = std::min(vnew, 3 * vold + 2);
		else if (vnew < 3.5)
			vnew = std::max(vnew, 2.0);
	} else {
		vnew = vnew > vold ? std::min(vnew, 4.0) : std::max(vnew, -0.5);
	}
	return vnew;
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
		int over = 1;
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
			} else if (w.rfind("over=", 0) == 0) {
				p.over = std::max(1, std::atoi(w.c_str() + 5));
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
		case 'q': case 'j': case 'm': case 'x': case 'p': count = 3; break;
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
		out.push_back({p.name, o.str(), p.sleeps, p.over});
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
				else if (type == "nmos" || type == "pmos") {
					m.type = type == "nmos" ? Model::Nmos : Model::Pmos;
					m.vto = 0; // SPICE's level-1 defaults (kp, w, l are the Model's)
				} else
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
					else if (key == "kp") m.kp = v;
					else if (key == "w") m.w = v;
					else if (key == "l") m.l = v;
					else if (key == "level" && v != 1) throw std::runtime_error("only level-1 MOSFETs: " + l);
					// charge storage and the rest: SPICE only, ignored here
				}
				if (m.type == Model::Nmos || m.type == Model::Pmos) {
					if (m.kp <= 0 || m.w <= 0 || m.l <= 0)
						throw std::runtime_error("KP, W and L must be positive: " + l);
					m.beta = m.kp / 2 * m.w / m.l;
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
		case 'M':
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
	for (const auto &e : net.elements) {
		if (e.kind != 'D' && e.kind != 'Q' && e.kind != 'J' && e.kind != 'M' && e.kind != 'B')
			continue;
		const auto it = net.models.find(e.model);
		if (it == net.models.end())
			throw std::runtime_error("no model " + e.model + " for " + e.name);
		const bool mos = it->second.type == Model::Nmos || it->second.type == Model::Pmos;
		if (mos != (e.kind == 'M'))
			throw std::runtime_error(e.name + (mos ? ": a MOSFET model on another element" : ": not a MOSFET model"));
	}
	return net;
}

double Circuit::defaultTolerance()
{
	// 10 uV at a junction is 0.04 % of its current: against 1 uV, envelopes move under 0.1 dB
	// (the hats; 0.01 elsewhere), spectra under 0.05 dB, Fischer's medians and the SPICE suite not
	// at all (the metal voices' 2-5 % within 0.2), for 6 % less CPU.
	static const double tol = std::getenv("TR909_TOL") ? std::atof(std::getenv("TR909_TOL")) : 1e-5;
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
			if (e.kind == 'D' || e.kind == 'Q' || e.kind == 'J' || e.kind == 'M' || e.kind == 'B' || e.kind == 'I')
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

	// Nodes with no DC path to ground at all, not even through a junction or a channel (a node
	// between two capacitors, a MOSFET's gate driven through one): kGshunt to ground keeps their DC
	// defined, as ngspice's rshunt would (toSpice exports these).
	{
		std::vector<int> parent(m_net.nodes.size());
		for (size_t n = 0; n < parent.size(); ++n)
			parent[n] = int(n);
		std::function<int(int)> root = [&](int n) { return parent[size_t(n)] == n ? n : parent[size_t(n)] = root(parent[size_t(n)]); };
		auto join = [&](int a, int b) { parent[size_t(root(a))] = root(b); };
		for (const auto &e : m_net.elements) {
			switch (e.kind) {
			case 'R': case 'P': case 'S': case 'V': case 'D': case 'Q': case 'J':
				for (int n : e.nodes)
					join(n, e.nodes[0]);
				break;
			case 'M': // the channel; the gate is insulated
				join(e.nodes[0], e.nodes[2]);
				break;
			case 'E':
				join(e.nodes[0], e.nodes[1]);
				break;
			case 'X':
				join(e.nodes[2], 0);
				break;
			case 'B': {
				const auto t = m_net.models.at(e.model).type;
				if (t == Netlist::Model::Ota)
					join(e.nodes[0], e.nodes[1]); // the abc diode
				else if (t == Netlist::Model::Limiter)
					join(e.nodes[1], 0); // the op-amp's output
				else if (t == Netlist::Model::Slew)
					join(e.nodes[0], e.nodes[1]);
				break;
			}
			default:
				break;
			}
		}
		m_shuntNodes.clear();
		for (size_t n = 1; n < m_net.nodes.size(); ++n)
			if (root(int(n)) != root(0))
				m_shuntNodes.push_back(int(n));
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
	m_mos.clear();
	m_limiterPorts.clear();
	m_constCount = 0;
	std::vector<std::pair<std::string, Comp>> opamps;
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
		if (e.kind == 'D' || e.kind == 'Q' || e.kind == 'J' || e.kind == 'M' || e.kind == 'B') {
			Device d;
			d.model = &m_net.models.at(e.model);
			d.port = int(m_portPlus.size());
			d.sign = d.model->type == Netlist::Model::Pnp || d.model->type == Netlist::Model::Pjf
			              || d.model->type == Netlist::Model::Pmos
			             ? -1
			             : 1;
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
			} else { // b-e, b-c (g-s, g-d; a MOSFET's too)
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
			if (d.model->type == Netlist::Model::Slew || d.model->type == Netlist::Model::Limiter) {
				// an op-amp's (parse: b<x>_slew, b<x>_out, c<x>_pole)
				const bool slew = d.model->type == Netlist::Model::Slew;
				const std::string x = e.name.substr(1, e.name.size() - (slew ? 6 : 5));
				auto it = std::find_if(opamps.begin(), opamps.end(), [&](const auto &o) { return o.first == x; });
				if (it == opamps.end()) {
					opamps.push_back({x, Comp{}});
					it = opamps.end() - 1;
				}
				if (slew) {
					it->second.slew = d.port;
					it->second.imax = d.model->is;
					it->second.gm = d.model->gm;
				} else {
					it->second.lim = d.port;
					it->second.vsat = d.model->vsat;
				}
			}
		}
	}
	m_comps.clear();
	for (auto &[x, c] : opamps) {
		if (c.slew < 0 || c.lim < 0)
			continue;
		for (int k = 0; k < int(m_caps.size()); ++k)
			if (m_net.elements[size_t(m_caps[size_t(k)])].name == "c" + x + "_pole")
				c.cap = k;
		m_comps.push_back(c);
	}
	m_compsClassified = false;
	m_comparators = 0;
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
				m_limiterPorts.push_back(d.port);
				// its control is the op-amp's pole node, gm (the open-loop gain) times smaller than
				// the output it sets: Newton judges it in output volts
				m_portWeight[size_t(d.port)] = m.gm;
			}
			continue;
		}
		if (m.type == Netlist::Model::Nmos || m.type == Netlist::Model::Pmos) {
			// no junctions: Newton limits the two ports together (newtonT)
			for (int k = 0; k < 2; ++k) {
				m_portVt[size_t(d.port + k)] = 0;
				m_portSign[size_t(d.port + k)] = d.sign;
			}
			m_mos.push_back({d.port, d.sign, d.sign * m.vto});
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
	for (int a = 0; a < m_np; ++a) {
		const double vt = m_portVt[size_t(a)];
		m_portClass[vt == 0 ? 0 : vt == -1 ? 1 : vt == -2 ? 2 : 3].push_back(a);
	}
	const size_t nx = size_t(m_nx), np = size_t(m_np);
	m_vc.assign(nx, 0.0);
	m_ic.assign(nx, 0.0);
	m_xpLast.assign(nx, 0.0);
	m_xn.assign(nx, 0.0);
	m_x.assign(nx, 0.0);
	m_xValid = false;
	m_capsStale = false;
	m_w.assign(nx + size_t(m_nu - m_constCount) + np, 0.0);
	m_vcSave.assign(nx, 0.0);
	m_icSave.assign(nx, 0.0);
	m_v.assign(np, 0.0);
	m_i.assign(np, 0.0);
	m_current = false;
	m_p.assign(np, 0.0);
	m_vp.assign(np + 1, 0.0);
	m_r.assign(np, 0.0);
	m_vOld.assign(np, 0.0);
	m_vSave.assign(np, 0.0);
	m_iSave.assign(np, 0.0);
	m_jac.assign(np * np, 0.0);
	m_ra.assign(np, 0.0);
	m_sigPorts.assign(np, 0);
	m_mosOld.assign(2 * m_mos.size(), 0.0);
	m_portDevice.assign(np, 0);
	for (size_t k = 0; k < m_devices.size(); ++k)
		for (int q = 0; q < m_devices[k].ports; ++q)
			m_portDevice[size_t(m_devices[k].port + q)] = int(k);
	m_anchor.assign(np, 0.0);
	m_anchored.clear();
	m_anchorsChosen = false;
	m_dcFailed = false;
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
	{
		auto &lin = m_lin;
		const size_t nx = size_t(m_nx), np = size_t(m_np), w1 = nx + size_t(m_nu - m_constCount), nr = nx + size_t(m_ny);
		lin.evI.assign(np, 0.0);
		lin.evJac.assign(np * np, 0.0);
		lin.Zc.assign(6 * std::max(np, nr), 0.0);
		lin.T.assign(3 * (w1 + np), 0.0);
		lin.GZ.assign(3 * np, 0.0);
		lin.gzc.assign(np + 1, 0.0);
		lin.CZc.assign(3 * (nr + 1), 0.0);
		lin.zeros.assign(nr + 1, 0.0);
		lin.loSave.assign(3, 0.0);
		lin.hiSave.assign(3, 0.0);
		lin.relin.assign(np, 0);
		lin.il.assign(np, 0.0);
		lin.rows.assign(nr, 0.0);
	}
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
	m_opOut.clear();
	for (const auto &e : m_net.elements) {
		switch (e.kind) {
		case 'R':
			conductance(e.nodes[0], e.nodes[1], 1.0 / e.value);
			break;
		case 'Q':
			// SPICE's gmin across an NPN's collector-substrate junction, the substrate on ground (a
			// vertical NPN's, as ngspice takes a three-terminal one; a PNP is lateral, without)
			if (m_net.models.at(e.model).type == Netlist::Model::Npn)
				conductance(e.nodes[0], 0, kGmin);
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
			if (e.name.size() > 6 && e.name.compare(0, 2, "ex") == 0 && e.name.compare(e.name.size() - 4, 4, "_out") == 0 && ip > 0)
				m_opOut.push_back({k, ip, e.value}); // an op-amp's output stage (parse)
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
	for (int k : m_shuntNodes)
		G(k - 1, k - 1) += kGshunt;
	// Anchors (see Circuit::m_anchor), chosen once from the DC network (the capacitors open) with
	// every node shunted to ground by kGshunt, as this solver had it before: a port whose
	// self-impedance is still over kAnchorR is tied down by the shunts alone.
	if (!m_anchorsChosen && m_np > 0) {
		m_anchorsChosen = true;
		std::vector<double> S = m_G;
		for (int k = 0; k < nodes; ++k)
			S[size_t(k * n + k)] += kGshunt;
		std::vector<int> piv(static_cast<size_t>(n));
		if (luDecomposeAny(S.data(), n, piv.data())) {
			std::vector<double> col(static_cast<size_t>(n));
			for (int p = 0; p < m_np; ++p) {
				for (int r = 0; r < n; ++r)
					col[size_t(r)] = m_Nn[size_t(r * m_np + p)];
				luSolveAny(S.data(), n, piv.data(), col.data());
				const int a = m_portPlus[size_t(p)], b = m_portMinus[size_t(p)];
				const double f = (a > 0 ? col[size_t(a - 1)] : 0.0) - (b > 0 ? col[size_t(b - 1)] : 0.0);
				if (std::abs(f) > kAnchorR) {
					m_anchor[size_t(p)] = kAnchorG;
					m_anchored.push_back(p);
				}
			}
		}
	}
	for (int p : m_anchored)
		conductance(m_portPlus[size_t(p)], m_portMinus[size_t(p)], m_anchor[size_t(p)]);
	// Level 0 now; the substep levels and the edges' when first used.
	double T = 1.0 / m_rate;
	for (int lv = 0; lv < kLevels; ++lv) {
		m_levels[lv].T = m_edgeLevels[lv].T = T;
		m_levels[lv].built = m_edgeLevels[lv].built = false;
		m_levels[lv].luValid = m_edgeLevels[lv].luValid = false;
		T /= 4;
	}
	buildLevel(m_levels[0], false);
	if (!m_compsClassified) {
		// Comparators: the feedback from an op-amp's output to its input difference in the
		// sample-rate network (the devices' ports open), d(v+ - v-)/d out, is the ratio of F's
		// entries for its output port's current; under kComparatorBeta it's negative feedback.
		m_compsClassified = true;
		const Level &L0 = m_levels[0];
		bool changed = false;
		for (auto &c : m_comps) {
			const int out = c.lim + 1;
			const double fo = L0.F[size_t(out * m_np + out)], fs = L0.F[size_t(c.slew * m_np + out)];
			c.comparator = !kNoFlips && fo != 0 && fs / fo > kComparatorBeta;
			if (c.comparator) {
				++m_comparators;
				if (c.cap >= 0 && m_capBE[size_t(c.cap)]) {
					m_capBE[size_t(c.cap)] = 0;
					changed = true;
				}
			}
			if (kDcDebug)
				std::fprintf(stderr, "%s: op-amp at port %d: feedback %.3g%s\n", m_name.c_str(), c.slew, fo != 0 ? fs / fo : 0.0,
				             c.comparator ? ", a comparator" : "");
		}
		if (changed) {
			for (int lv = 0; lv < kLevels; ++lv)
				m_levels[lv].built = m_edgeLevels[lv].built = false;
			buildLevel(m_levels[0], false);
		}
	}
	m_dirty = false;
}

void Circuit::setPoleTrap(bool on)
{
	if (on == m_poleTrap)
		return;
	m_poleTrap = on;
	for (int k = 0; k < int(m_caps.size()); ++k) {
		const auto &name = m_net.elements[size_t(m_caps[size_t(k)])].name;
		if (name.size() > 5 && name.compare(name.size() - 5, 5, "_pole") == 0)
			m_capBE[size_t(k)] = on ? 0 : 1;
	}
	if (!on) { // the comparators' poles back to the trapezoidal rule when they're found again
		m_compsClassified = false;
		m_comparators = 0;
	}
	m_dirty = true;
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
		L.sConst.assign(size_t(m_ny + m_nx), 0.0);
		auto row = [&](int r, const std::vector<double> &X, const std::vector<double> &U, const std::vector<double> &I, int src) {
			double *d = &L.S[size_t(r * w2)];
			for (int k = 0; k < m_nx; ++k)
				d[k] = X[size_t(src * m_nx + k)];
			for (int j = 0; j < nv; ++j)
				d[m_nx + j] = U[size_t(src * m_nu + m_constCount + j)];
			for (int k = 0; k < m_np; ++k)
				d[w1 + k] = I[size_t(src * m_np + k)];
		};
		for (int o = 0; o < m_ny; ++o) {
			row(o, L.Dy, L.Ey, L.Fy, o);
			L.sConst[size_t(o)] = L.yConst[size_t(o)];
		}
		for (int r = 0; r < m_nx; ++r) {
			row(m_ny + r, L.A, L.B, L.C, r);
			L.sConst[size_t(m_ny + r)] = L.xConst[size_t(r)];
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
	L.FT.assign(size_t(m_np * m_np), 0.0);
	for (int a = 0; a < m_np; ++a)
		for (int q = 0; q < m_np; ++q)
			L.FT[size_t(q * m_np + a)] = L.F[size_t(a * m_np + q)];
	toColumns(L.F.data(), m_np, m_np, m_np, L.Fc);
	L.fColMax.assign(size_t(m_np), 0.0);
	for (int q = 0; q < m_np; ++q)
		for (int a = 0; a < m_np; ++a)
			L.fColMax[size_t(q)] = std::max(L.fColMax[size_t(q)], std::abs(L.F[size_t(a * m_np + q)]));
}

[[gnu::always_inline]] inline void Circuit::deviceEvalRaw(const Device &d, const double *v, double *i, double *jac) const
{
	const auto &m = *d.model;
	const int p = d.port;
	if (m.type == Netlist::Model::Diode) {
		double e;
		const double x = safeExp(v[p] * d.kf, e);
		i[p] = m.is * (x - 1) + kGmin * v[p];
		jac[p * m_np + p] = m.is * e * d.kf + kGmin;
		return;
	}
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
		return;
	}
	if (m.type == Netlist::Model::Slew) {
		// An op-amp's input stage: imax tanh(v / (r imax)) into its pole
		const double th = std::tanh(v[p] * m.gm / m.is);
		i[p] = m.is * th;
		jac[p * m_np + p] = m.gm * (1 - th * th);
		return;
	}
	if (m.type == Netlist::Model::Limiter) {
		// An op-amp's output stage: drives out through 1 ohm to gain v_b, exactly linear up to
		// kLimiterKnee below the rails (so Newton's Jacobian stays put while the op-amp works in
		// its linear range: its loop gain makes the chord iterations sensitive to any change of
		// slope), then rounding off into +-vsat over that last stretch.
		const double gain = m.gm * m_dcGain;
		const double x = gain * v[p];
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
		jac[p * m_np + p] = kWindupG * daw * gain;
		jac[p * m_np + p + 1] = 0;
		jac[(p + 1) * m_np + p] = -gain * d;
		jac[(p + 1) * m_np + p + 1] = 1;
		return;
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
		return;
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
		// channel current from drain to source and its derivatives in vgs, vgd
		double ids, dvgs, dvgd;
		channel(m.beta, m.vto, m.lambda, vgs, vgd, ids, dvgs, dvgd);
		// Port 0 (g-s): out of the source; port 1 (g-d): out of the drain.
		i[p] = s * (igs + ids);
		i[p + 1] = s * (igd - ids);
		jac[p * m_np + p] = ggs + dvgs;
		jac[p * m_np + p + 1] = dvgd;
		jac[(p + 1) * m_np + p] = -dvgs;
		jac[(p + 1) * m_np + p + 1] = ggd - dvgd;
		return;
	}
	if (m.type == Netlist::Model::Nmos || m.type == Netlist::Model::Pmos) {
		// SPICE's level-1 MOSFET (mos1load) with the bulk on the source and GAMMA 0: the channel
		// alone, no gate current; SPICE's gmin across the bulk-drain junction is drain to source.
		const double s = d.sign;
		const double vgs = s * v[p], vgd = s * v[p + 1];
		double ids, dvgs, dvgd;
		channel(m.beta, s * m.vto, m.lambda, vgs, vgd, ids, dvgs, dvgd);
		ids += kGmin * (vgs - vgd);
		dvgs += kGmin;
		dvgd -= kGmin;
		// Port 0 (g-s): out of the source; port 1 (g-d): out of the drain.
		i[p] = s * ids;
		i[p + 1] = -s * ids;
		jac[p * m_np + p] = dvgs;
		jac[p * m_np + p + 1] = dvgd;
		jac[(p + 1) * m_np + p] = -dvgs;
		jac[(p + 1) * m_np + p + 1] = -dvgd;
		return;
	}
	// Gummel-Poon without high injection or charge: SPICE's bjtload with the same terms, gmin
	// across both junctions in the base current only (with the leakage terms, as bjtload adds
	// it), and for an NPN across the collector-substrate junction too (see build).
	const double s = d.sign;
	const double vbe = s * v[p], vbc = s * v[p + 1];
	double ef, er;
	const double xf = safeExp(vbe * d.kf, ef), xr = safeExp(vbc * d.kr, er);
	const double If = m.is * (xf - 1), gIf = m.is * ef * d.kf;
	const double Ir = m.is * (xr - 1), gIr = m.is * er * d.kr;
	double Ibe = kGmin * vbe, gIbe = kGmin, Ibc = kGmin * vbc, gIbc = kGmin;
	if (m.ise > 0) {
		double e;
		const double x = safeExp(vbe * d.ke, e);
		Ibe += m.ise * (x - 1);
		gIbe += m.ise * e * d.ke;
	}
	if (m.isc > 0) {
		double e;
		const double x = safeExp(vbc * d.kc, e);
		Ibc += m.isc * (x - 1);
		gIbc += m.isc * e * d.kc;
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

void Circuit::deviceEval(const Device &d, const double *v, double *i, double *jac) const
{
	deviceEvalRaw(d, v, i, jac);
	for (int p = d.port; p < d.port + d.ports; ++p) { // as deviceCurrents
		if (m_anchor[size_t(p)] > 0) {
			i[p] -= m_anchor[size_t(p)] * v[p];
			jac[p * m_np + p] -= m_anchor[size_t(p)];
		}
		if (m_dcGmin > 0) {
			i[p] += m_dcGmin * v[p];
			jac[p * m_np + p] += m_dcGmin;
		}
	}
}

void Circuit::deviceCurrents(const double *v, double *i, double *jac) const
{
	// jac is np x np; only the devices' diagonal blocks are written.
	for (const auto &d : m_devices)
		deviceEvalRaw(d, v, i, jac);
	// the anchors' currents, which the linear network carries, are the devices' own
	for (int p : m_anchored) {
		i[p] -= m_anchor[size_t(p)] * v[p];
		jac[p * m_np + p] -= m_anchor[size_t(p)];
	}
	if (m_dcGmin > 0)
		for (int p = 0; p < m_np; ++p) {
			i[p] += m_dcGmin * v[p];
			jac[p * m_np + p] += m_dcGmin;
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
			double big = 0;
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
	case 12: return newtonT<12>(L, p, v, i, maxIterations);
	case 13: return newtonT<13>(L, p, v, i, maxIterations);
	case 14: return newtonT<14>(L, p, v, i, maxIterations);
	case 15: return newtonT<15>(L, p, v, i, maxIterations);
	case 16: return newtonT<16>(L, p, v, i, maxIterations);
	case 18: return newtonT<18>(L, p, v, i, maxIterations);
	case 19: return newtonT<19>(L, p, v, i, maxIterations);
	case 24: return newtonT<24>(L, p, v, i, maxIterations);
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
			// down F's columns (each row's sum in a plain loop's order)
			double *vp = m_vp.data();
			for (int a = 0; a < np; ++a)
				vp[a] = v[a] - p[a];
			colMatVec<false, true>(L.Fc, 0, np, np, vp, i, r);
			for (int a = 0; a < np; ++a)
				r[a] = -r[a];
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
		double *mosOld = m_mosOld.data();
		for (size_t k = 0; k < m_mos.size(); ++k) {
			mosOld[2 * k] = v[m_mos[k].port];
			mosOld[2 * k + 1] = v[m_mos[k].port + 1];
		}
		for (int a : m_portClass[0])
			v[a] += r[a];
		for (int a : m_portClass[1]) { // an open collector: approach the rail geometrically
			const double vnew = v[a] + r[a];
			v[a] = vnew < 0 && v[a] > 0 ? 0.1 * v[a] : vnew;
		}
		for (int a : m_portClass[2]) { // a limiter's input, in units of its knee
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
		}
		for (int a : m_portClass[3]) {
			const double s = m_portSign[size_t(a)];
			const double vold = s * v[a];
			v[a] = s * limitJunction(vold + s * r[a], vold, m_portVt[size_t(a)], m_portVcrit[size_t(a)]);
		}
		// MOSFETs as mos1load limits them: the gate of whichever end is the source, then vds.
		for (size_t k = 0; k < (kNoFetLim ? 0 : m_mos.size()); ++k) {
			const Mos &q = m_mos[k];
			const double s = q.sign;
			double vgs = s * v[q.port], vgd = s * v[q.port + 1];
			const double vgsOld = s * mosOld[2 * k], vgdOld = s * mosOld[2 * k + 1], vdsOld = vgsOld - vgdOld;
			if (vdsOld >= 0) {
				vgs = fetLimit(vgs, vgsOld, q.vth);
				vgd = vgs - limitVds(vgs - vgd, vdsOld);
			} else {
				vgd = fetLimit(vgd, vgdOld, q.vth);
				vgs = vgd - limitVds(vgd - vgs, -vdsOld);
			}
			v[q.port] = s * vgs;
			v[q.port + 1] = s * vgd;
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
		const double xn = m_x[size_t(k)], xp = m_xpLast[size_t(k)];
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
	Level &L = level(lv);
	const int nx = m_nx, np = m_np, nu = m_nu;
	const int c0 = m_constCount;
	const int w1 = nx + nu - c0, w2 = w1 + np;
	// w: the companion states for this step size (from the capacitors' voltages and currents), the
	// inputs that vary, and (after Newton) the ports' currents
	double *w = m_w.data();
	const bool full = lv == 0 && !m_inEdge; // a full sample: the state stays as companion states
	if (full && m_xValid) {
		copyN(m_x.data(), nx, w);
	} else {
		if (!full)
			dropX();
		syncCaps();
		for (int k = 0; k < nx; ++k)
			w[k] = L.gc[size_t(k)] * m_vc[size_t(k)] + (L.be[size_t(k)] ? 0 : m_ic[size_t(k)]);
	}
	for (int k = c0; k < nu; ++k)
		w[nx + k - c0] = u[k];
	colMatVec<false>(L.Pc, 0, np, w1, L.pConst.data(), w, m_p.data());
	for (int a = 0; a < np; ++a) {
		m_vSave[size_t(a)] = m_v[size_t(a)];
		m_iSave[size_t(a)] = m_i[size_t(a)];
	}
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
	if (kTrace == np)
		std::fprintf(stderr, "%s step %ld lv %d edge %d lin %d\n", m_name.c_str(), m_stats.steps, lv, int(m_inEdge), int(m_lin.on));
	if (!newton(L, m_p.data(), m_v.data(), m_i.data(), m_maxIt)) {
		m_prevValid = false;
		for (int a = 0; a < np; ++a) {
			m_v[size_t(a)] = m_vSave[size_t(a)];
			m_i[size_t(a)] = m_iSave[size_t(a)];
		}
		L.luValid = false;
		m_current = false;
		return false;
	}
	for (int k = 0; k < np; ++k)
		w[w1 + k] = m_i[size_t(k)];
	// the outputs, then the next states
	const int ny = m_ny;
	colMatVec<false>(L.Sc, 0, ny, w2, L.sConst.data(), w, m_y.data());
	colMatVec<false>(L.Sc, ny, nx, w2, &L.sConst[size_t(ny)], w, m_xn.data());
	copyN(w, nx, m_xpLast.data());
	if (full) {
		// keep x[n] as the next step's companion states (m_vc/m_ic follow when asked for)
		std::swap(m_x, m_xn); // m_xn is scratch
		m_xValid = true;
		m_capsStale = true;
	} else {
		// back to voltages and currents: x[n] = 2C/T v[n] - x[n-1], i[n] = (x[n] - x[n-1]) / 2
		// (backward Euler: x[n] = C/T v[n], i[n] = x[n] - x[n-1])
		for (int k = 0; k < nx; ++k) {
			const double xn = m_xn[size_t(k)], xp = w[k];
			if (L.be[size_t(k)]) {
				m_vc[size_t(k)] = xn * L.vcScale[size_t(k)];
				m_ic[size_t(k)] = xn - xp;
				continue;
			}
			m_vc[size_t(k)] = (xn + xp) * L.vcScale[size_t(k)];
			m_ic[size_t(k)] = 0.5 * (xn - xp);
		}
	}
	if (lv == 0 && !m_inEdge)
		m_predict.count(m_stats.iterations - itBefore);
	if (lv == 0 && !m_inEdge) {
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

void Circuit::saveStep(Saved &s)
{
	s.x = m_x;
	s.xpLast = m_xpLast;
	s.vc = m_vc;
	s.ic = m_ic;
	s.v = m_v;
	s.i = m_i;
	s.vOld = m_vOld;
	s.y = m_y;
	s.xValid = m_xValid;
	s.capsStale = m_capsStale;
	s.prevValid = m_prevValid;
}

void Circuit::restoreStep(const Saved &s, bool invalidate)
{
	m_x = s.x;
	m_xpLast = s.xpLast;
	m_vc = s.vc;
	m_ic = s.ic;
	m_v = s.v;
	m_i = s.i;
	m_vOld = s.vOld;
	m_y = s.y;
	m_xValid = s.xValid;
	m_capsStale = s.capsStale;
	m_prevValid = s.prevValid;
	m_current = false;
	if (invalidate)
		for (auto &L : m_levels)
			L.luValid = false;
}

bool Circuit::flipped(const Saved &s) const
{
	for (const auto &c : m_comps) {
		if (!c.comparator)
			continue;
		const double a = s.i[size_t(c.slew)], b = m_i[size_t(c.slew)];
		if (a * b < 0 && std::abs(b - a) > c.imax)
			return true;
		if (std::abs(m_v[size_t(c.lim + 1)] - s.v[size_t(c.lim + 1)]) > kFlipVolts)
			return true;
	}
	return false;
}

bool Circuit::flipping(const double *v) const
{
	// a comparator between its rails: its input stage near its linear range (kMidFlipRanges), or
	// its output short of the rail
	for (const auto &c : m_comps) {
		if (!c.comparator)
			continue;
		const double range = c.imax / c.gm;
		if (std::abs(v[c.slew]) < kMidFlipRanges * range || std::abs(v[c.lim]) * m_portVcrit[size_t(c.lim)] * c.vsat < c.vsat + kLimiterKnee)
			return true;
	}
	return false;
}

bool Circuit::flipAhead() const
{
	if (!m_prevValid)
		return false;
	for (const auto &c : m_comps) {
		if (!c.comparator)
			continue;
		const double v = m_v[size_t(c.slew)], next = 2 * v - m_vOld[size_t(c.slew)];
		const double zone = c.imax / c.gm; // its input stage's linear range
		if (std::abs(v) < kSlewSat * zone && (v * next <= 0 || std::abs(next) < kAheadZone * zone))
			return true;
	}
	return false;
}

bool Circuit::advanceFine(const double *u0, const double *u1)
{
	const int n = kFlipSteps >= 64 ? 64 : kFlipSteps >= 16 ? 16 : 4, lv = n == 64 ? 3 : n == 16 ? 2 : 1;
	std::vector<double> &from = m_uMid, &to = m_uTo;
	std::copy(u0, u0 + m_nu, from.begin());
	std::copy(u1, u1 + m_nu, to.begin());
	for (int k = 1; k <= n; ++k) {
		for (int j = m_constCount; j < m_nu; ++j)
			to[size_t(j)] = u0[j] + (u1[j] - u0[j]) * k / double(n);
		if (!advanceSplit(lv, from.data(), to.data()))
			return false;
		std::copy(to.begin(), to.end(), from.begin());
	}
	return true;
}

void Circuit::step()
{
	buildIfDirty();
	++m_stats.steps;
	// the common case first: a linear step (with nothing asked of the sample, and no statistics)
	if (m_lin.on && !kHist && !m_jump && m_subLeft == 0 && m_edgeLeft == 0) {
		if (linearStep(m_u.data())) {
			copyN(m_u.data() + m_constCount, m_nu - m_constCount, m_uLast.data() + m_constCount); // the constants never change
			return;
		}
		leaveLinear();
	}
	int histKind = -1;
	struct HistGuard {
		Circuit &c;
		int &kind;
		long it0;
		bool lin0, jump0, sub0, wait0;
		std::chrono::steady_clock::time_point t0 = kHist ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		~HistGuard()
		{
			if (!kHist)
				return;
			const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
			auto &e = histOf(c.m_name);
			e.sizes[0] = c.m_n;
			e.sizes[1] = c.m_nx;
			e.sizes[2] = c.m_np;
			e.sizes[3] = c.m_nu - c.m_constCount;
			e.sizes[4] = c.m_ny;
			const long its = c.m_stats.iterations - it0;
			int k = kind;
			if (k < 0)
				k = jump0 ? kHJump : sub0 ? kHQuarters : (lin0 && its == 0 && c.m_lin.on) ? kHLinear : kHFull;
			if (k == kHFull && wait0)
				++e.waitSteps;
			++e.n[k];
			e.it[k] += its;
			e.ns[k] += ns;
		}
	} histGuard{*this, histKind, m_stats.iterations, m_lin.on, m_jump, m_subLeft > 0 || m_edgeLeft > 0, !m_lin.on && m_lin.wait > 1};
	const double *u0 = m_uLast.data(), *u1 = m_u.data();
	bool ok;
	const bool lastRedone = m_lastRedone;
	m_lastRedone = false;
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
		const bool watch = m_comparators > 0 && m_edgeLeft == 0;
		if (watch)
			saveStep();
		ok = advanceFour(1, u0, u1);
		if (ok && watch && flipped()) {
			restoreStep();
			ok = advanceFine(u0, u1);
			if (!ok) { // the quarters' result after all
				restoreStep();
				ok = advanceFour(1, u0, u1);
			}
		}
	} else {
		if (m_lin.on) {
			if (linearStep(u1)) {
				for (int k = m_constCount; k < m_nu; ++k) // the constants never change
					m_uLast[size_t(k)] = m_u[size_t(k)];
				return;
			}
			leaveLinear();
		}
		const bool watch = m_comparators > 0;
		if (watch)
			saveStep();
		if (watch && flipAhead()) {
			// a comparator's input crosses within this sample, by the last two: straight to the
			// sixteenths (a full step would fail at the flip, then quarters, then this)
			++m_stats.substepped;
			histKind = kHFlipAhead;
			m_lastRedone = true;
			ok = advanceFine(u0, u1);
			if (!ok) {
				restoreStep();
				m_subLeft = 32;
				ok = advanceFour(1, u0, u1);
			}
			for (int k = m_constCount; k < m_nu; ++k)
				m_uLast[size_t(k)] = m_u[size_t(k)];
			if (!ok) {
				++m_stats.failures;
				if (kFailDebug)
					std::fprintf(stderr, "%s: step %ld failed\n", m_name.c_str(), m_stats.steps);
			}
			return;
		}
		// a comparator's sample that Newton finds hard is a flip's: its sixteenths, not a long solve
		// the sample after a flip's while its comparator is still between its rails: no trial
		const bool midFlip = watch && lastRedone && flipping(m_v.data());
		if (watch)
			m_maxIt = kTrialIterations;
		ok = midFlip ? false : advance(0, u1);
		m_maxIt = kMaxIterations;
		const bool full = ok;
		bool trialFailed = false;
		if (!ok && watch) {
			trialFailed = true;
			restoreStep();
			ok = true; // redone below as a flip
		} else if (!ok) {
			++m_stats.substepped;
			m_subLeft = 32;
			histKind = kHFullFail;
			ok = advanceFour(1, u0, u1);
		}
		bool redone = false;
		if (ok && watch && (trialFailed || flipped())) {
			restoreStep();
			if (full)
				++m_stats.substepped;
			redone = true;
			histKind = kHFlipRedo;
			const int subLeft = m_subLeft;
			m_subLeft = 0; // a flip, not a stiff event: the next samples as usual
			ok = advanceFine(u0, u1);
			if (!ok) { // as a failed sample
				restoreStep();
				m_subLeft = std::max(subLeft, 32);
				ok = advanceFour(1, u0, u1);
			}
		}
		m_lastRedone = redone;
		if (ok && full && !redone && !kLinOff && m_np > 0 && --m_lin.wait <= 0)
			linearize();
	}
	if (!ok) {
		++m_stats.failures;
		if (kFailDebug)
			std::fprintf(stderr, "%s: step %ld failed\n", m_name.c_str(), m_stats.steps);
	}
	for (int k = m_constCount; k < m_nu; ++k)
		m_uLast[size_t(k)] = m_u[size_t(k)];
}

std::string Circuit::portName(int port) const
{
	static const char *types[] = {"D", "NPN", "PNP", "NJF", "PJF", "NMOS", "PMOS", "OTA", "CMP", "LIM", "SLEW"};
	for (const auto &d : m_devices)
		if (port >= d.port && port < d.port + d.ports) {
			int e = 0;
			for (size_t k = 0; k < m_devices.size(); ++k)
				if (&m_devices[k] == &d)
					e = int(k);
			return std::string(types[d.model->type]) + "#" + std::to_string(e) + "." + std::to_string(port - d.port);
		}
	return "?";
}

int Circuit::deviceWindow(const Device &d, const double *v0, const char *&why) const
{
	// The window of each of the device's ports around v0 (lin.lo, lin.hi): 0, or 1 if it can't be
	// linear there (why), 2 if it is a comparator's input stage in its flip (try again soon).
	auto &lin = const_cast<Linear &>(m_lin);
	const auto &m = *d.model;
	const int p = d.port;
	for (int k = 0; k < d.ports; ++k) {
		lin.lo[size_t(p + k)] = -1e30;
		lin.hi[size_t(p + k)] = 1e30;
	}
	auto junction = [&](int p, double is, double nvt, double sign, double zport) {
		// The window keeps the linearisation's current error, is e^(v0/nV_T) (e^u - 1 - u) with
		// u = (v - v0) / nV_T (the exponential's departure from its tangent, either way), under
		// kLinTolerance / zeff: zeff is the most a current at this port moves the ports (as Newton
		// judges them, weighted), the largest entry of (I - F G)^-1 F's column for it, the devices
		// as linearised (the junction's own conductance among them). Deep in reverse the window
		// reaches up to where the junction's current would start to tell; near zero bias in a node
		// only junctions hold (the toms' line A) it is a few millivolts.
		const double v = sign * v0[p];
		const double zc = std::max(zport, 1e-3);
		const double a = is * std::exp(std::min(v / nvt, 80.0));
		const double q = kLinTolerance / (zc * std::max(a, 1e-300));
		double uh, ul;
		if (q < 1e-2) {
			const double r = std::sqrt(2 * q);
			uh = r * (1 - r / 6);
			ul = -r * (1 + r / 6);
		} else {
			// e^u - 1 - u = q. Up: by Newton from above under q of 3, else u = log(1 + q + u), a
			// contraction from log(1 + q); down: from -(q + 1) (where e^u has gone, past q of 20), by
			// Newton (convex, from below)
			if (q < 3) { // Newton from above (convex)
				uh = std::sqrt(2 * q);
				for (int it = 0; it < 12; ++it) {
					const double eh = std::exp(uh), fh = eh - 1 - uh - q;
					uh -= fh / (eh - 1);
					if (std::abs(fh) < 1e-6 * q)
						break;
				}
			} else {
				uh = std::log1p(q);
				for (int it = 0; it < 6; ++it)
					uh = std::log1p(q + uh);
			}
			ul = -(q + 1);
			if (q < 20)
				for (int it = 0; it < 12; ++it) {
					const double el = std::exp(ul), fl = el - 1 - ul - q;
					ul -= fl / (el - 1);
					if (std::abs(fl) < 1e-6 * q)
						break;
				}
		}
		// at least 0.1 mV (a window under it would end every step)
		const double hiW = std::max(nvt * uh, 1e-4), loW = std::max(-nvt * ul, 1e-4);
		if (sign > 0) {
			lin.lo[size_t(p)] = ul < -1e20 ? -1e30 : v0[p] - loW;
			lin.hi[size_t(p)] = v0[p] + hiW;
		} else {
			lin.lo[size_t(p)] = v0[p] - hiW;
			lin.hi[size_t(p)] = ul < -1e20 ? 1e30 : v0[p] + loW;
		}
	};
	auto window = [&](int p, double w) {
		lin.lo[size_t(p)] = v0[p] - w;
		lin.hi[size_t(p)] = v0[p] + w;
	};
	switch (m.type) {
	case Netlist::Model::Diode:
		junction(p, m.is, m.n * kVt, 1, lin.zeff[size_t(p)]);
		break;
	case Netlist::Model::Npn:
	case Netlist::Model::Pnp: {
		// each junction's exponential flows out of both ports (the transport current): its error
		// judged by the larger zeff
		const double z = std::max(lin.zeff[size_t(p)], lin.zeff[size_t(p + 1)]);
		junction(p, m.is, m.nf * kVt, d.sign, z);
		junction(p + 1, m.is, m.nr * kVt, d.sign, z);
		break;
	}
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
	case Netlist::Model::Nmos:
	case Netlist::Model::Pmos: {
		// channel off at both ends: only gmin, exactly linear while it stays off
		const double vth = d.sign * m.vto;
		const double x = d.sign * v0[p] - vth, y = d.sign * v0[p + 1] - vth; // the two ends' overdrives
		if (x < 0 && y < 0) {
			for (int k = 0; k < 2; ++k) {
				if (d.sign > 0)
					lin.hi[size_t(p + k)] = vth;
				else
					lin.lo[size_t(p + k)] = -vth;
			}
		} else if (kMosWindows) {
			// conducting: the channel's current is beta (x^2 - y^2)(1 + lambda (x - y)) in the
			// overdrives (beta x^2 (1 + lambda (x - y)) with the drain end pinched off), smooth to
			// the first derivative across pinch-off and cut-off. Its second derivatives stay under
			// 2 beta K, K = 1 + lambda (2 |vds| + 4 (|x| + |y|) + 6), over a window of up to 0.5 V,
			// so the linearisation's current error on a window w at both ports is under
			// beta K w^2. It flows out of one port and into the other: it moves the ports by at
			// most the two ports' zeff together.
			const double z = std::max(lin.zeff[size_t(p)] + lin.zeff[size_t(p + 1)], 1e-3);
			const double K = 1 + m.lambda * (2 * std::abs(x - y) + 4 * (std::abs(x) + std::abs(y)) + 6);
			const double w = std::clamp(std::sqrt(kLinTolerance / (m.beta * K * z)), 1e-4, 0.5);
			window(p, w);
			window(p + 1, w);
		} else {
			window(p, kLinWindow);
			window(p + 1, kLinWindow);
		}
		break;
	}
	case Netlist::Model::Slew: {
		// linear while tanh's departure, (r imax) x^3 / 3 port volts, is within the tolerance
		// in the port's weight (1000); or saturated (a comparator in a rail, an op-amp slewing),
		// where its current is imax to 4e-9 while the port stays beyond kSlewSat linear ranges
		const double range = m.is / m.gm;
		const double lim = range * std::cbrt(3 * kLinTolerance / (1000 * range));
		if (std::abs(v0[p]) >= kSlewSat * range) {
			if (v0[p] > 0)
				lin.lo[size_t(p)] = kSlewSat * range;
			else
				lin.hi[size_t(p)] = -kSlewSat * range;
			break;
		}
		if (std::abs(v0[p]) >= lim) {
			for (const auto &c : m_comps)
				if (c.comparator && c.slew == p) { // in a flip: try again soon, no longer wait
					if (kLinDebug)
						std::fprintf(stderr, "linear %s: comparator at port %d slewing\n", m_name.c_str(), p);
					why = "comparator slewing";
					return 2;
				}
			if (kSlewWindows) {
				// an op-amp between its linear range and saturation, in feedback with its output
				// moving fast (a noise source's buffer): imax tanh(v / range) departs from its tangent
				// by under (imax / range^2) g w^2 on a window w, g = tanh sech^2 at the window's far
				// end (its largest, 0.385, from 0.658 ranges on), and that current moves the ports
				// by zeff times it. The window from g at v0, then again from g at v0 + w.
				const double z = std::max(lin.zeff[size_t(p)], 1e-3);
				const double t0 = std::abs(v0[p]) / range;
				auto gmax = [](double t) {
					if (t >= 0.658)
						return 0.385;
					const double th = std::tanh(t);
					return th * (1 - th * th);
				};
				double w = std::sqrt(kLinTolerance * range * range / (z * m.is * gmax(t0)));
				w = std::sqrt(kLinTolerance * range * range / (z * m.is * gmax(t0 + w / range)));
				if (w >= 1e-4) {
					window(p, w);
					break;
				}
			}
			if (kLinDebug)
				std::fprintf(stderr, "linear %s: op-amp at port %d slewing\n", m_name.c_str(), p);
			why = "op-amp slewing";
			return 1;
		}
		lin.lo[size_t(p)] = -lim;
		lin.hi[size_t(p)] = lim;
		break;
	}
	case Netlist::Model::Limiter: {
		const double knee = (m.vsat - kLimiterKnee) / m.gm;
		const double x = std::abs(v0[p]) * m.gm; // in output volts
		const double aw = m.vsat + kWindupMargin + 40 * kWindupSoft; // the anti-windup linear past this
		if (x >= aw && (x - (m.vsat - kLimiterKnee)) / kLimiterKnee >= 4) {
			// saturated: the output flat and the anti-windup linear; within the window the
			// output's curvature (tanh's) keeps the linearisation's error under kLinTolerance
			const double th = std::tanh((x - (m.vsat - kLimiterKnee)) / kLimiterKnee);
			const double curv = 2 * th * (1 - th * th) / kLimiterKnee; // d2 out / dx2
			const double w = std::min(std::sqrt(2 * kLinTolerance / std::max(curv, 1e-30)), x - aw) / m.gm;
			window(p, w);
			break;
		}
		if (std::abs(v0[p]) >= knee) {
			// in its knee: the output's curvature against the output port's zeff (an op-amp in
			// feedback clipping softly: the accent amplifier at full accent); a comparator's knee
			// is its flip's, which Newton takes
			bool comparator = false;
			for (const auto &c : m_comps)
				comparator = comparator || (c.comparator && c.lim == p);
			const double xs = x - (m.vsat - kLimiterKnee); // past the knee's start, output volts
			const double sw = (x - (m.vsat + kWindupMargin)) / kWindupSoft; // the anti-windup's
			if (!comparator && sw < -40 && xs < 4 * kLimiterKnee) {
				const double th = std::tanh(xs / kLimiterKnee);
				const double curv = std::max(2 * th * (1 - th * th), 0.1) / kLimiterKnee; // d2 out / dx2, and near its start the third order's
				const double zo = std::max(lin.zeff[size_t(p + 1)], 1e-3);
				const double w = std::sqrt(2 * kLinTolerance / (curv * zo));
				// within the knee's own stretch: not back past its start nor on to the anti-windup
				const double lo = std::max(x - w, m.vsat - kLimiterKnee) / m.gm, hi = std::min(x + w, m.vsat + kWindupMargin - 40 * kWindupSoft) / m.gm;
				if (v0[p] > 0) {
					lin.lo[size_t(p)] = lo;
					lin.hi[size_t(p)] = hi;
				} else {
					lin.lo[size_t(p)] = -hi;
					lin.hi[size_t(p)] = -lo;
				}
				break;
			}
			if (kLinDebug)
				std::fprintf(stderr, "linear %s: limiter at port %d clipping\n", m_name.c_str(), p);
			why = "limiter knee";
			return 1;
		}
		lin.lo[size_t(p)] = -knee;
		lin.hi[size_t(p)] = knee;
		break;
	}
	case Netlist::Model::Ota: {
		// I_abc's exponential reaches the output gm tanh times over: its junction's window judged
		// by the output's zeff with that gain; the input pair's tanh by its curvature (at most
		// 0.77 gm I_abc / (2 V_T)^2) against the output's zeff
		const double zout = std::max({lin.zeff[size_t(p)], lin.zeff[size_t(p + 1)], lin.zeff[size_t(p + 2)]});
		const double th = std::tanh(v0[p + 1] / (2 * kVt));
		const double gain = std::max(1.0, m.gm * std::abs(th));
		junction(p, m.is * gain, kVt, 1, zout);
		const double iabc = std::max(m.is * std::expm1(v0[p] / kVt), 0.0);
		const double curv = 0.77 * m.gm * iabc / (4 * kVt * kVt);
		const double w = curv > 0 ? std::clamp(std::sqrt(2 * kLinTolerance / (std::max(zout, 1e-3) * curv)), 1e-4, 0.1) : 0.1;
		window(p + 1, w);
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
			why = "comparator undecided";
			return 1;
		}
		window(p, margin / 2);
		window(p + 1, margin / 2);
		if (vd < 0)
			window(p + 2, kLinWindow);
		break;
	}
	}
	return 0;
}

bool Circuit::linearize()
{
	// The devices at the last step's solution, and each port's window.
	const int np = m_np;
	if (kHist)
		histOf(m_name).relins["(linearize)"]++;
	auto &lin = m_lin;
	lin.v0 = m_v;
	lin.i0.assign(size_t(np), 0.0);
	lin.G.assign(size_t(np * np), 0.0);
	deviceCurrents(lin.v0.data(), lin.i0.data(), lin.G.data());
	lin.lo.assign(size_t(np), -1e30);
	lin.hi.assign(size_t(np), 1e30);
	const double *v0 = lin.v0.data();
	auto fail = [&](const char *why = "lu") {
		if (kHist)
			histOf(m_name).fails[why]++;
		lin.wait = lin.backoff;
		lin.backoff = std::min(lin.backoff * 2, 4096);
		return false;
	};
	// a comparator slewing (deviceWindow's test, which needs no zeff) before the matrices: these
	// come every few samples through a flip
	for (const auto &c : m_comps) {
		if (!c.comparator)
			continue;
		const double range = c.imax / c.gm, x = std::abs(v0[c.slew]);
		if (x < kSlewSat * range && x >= range * std::cbrt(3 * kLinTolerance / (1000 * range))) {
			if (kHist)
				histOf(m_name).fails["comparator slewing"]++;
			lin.wait = 2;
			return false;
		}
	}
	if (!linearMatrices())
		return fail();
	for (const auto &d : m_devices) {
		const char *why = "";
		const int r = deviceWindow(d, v0, why);
		if (r == 2) {
			if (kHist)
				histOf(m_name).fails[why]++;
			lin.wait = 2;
			return false;
		}
		if (r == 1)
			return fail(why);
	}
	lin.updates = 0;
	if (kHist)
		histOf(m_name).relins["(linearize ok)"]++;
	lin.v = m_v;
	lin.on = true;
	lin.since = 0;
	++m_stats.linearizations;
	return true;
}

bool Circuit::linearMatrices()
{
	// M = I - F G (G is the devices' diagonal blocks), factored; K = M^-1 [D E], kv = M^-1 (pConst + F c)
	// with the currents i = G v + c (the devices at lin.v0, lin.i0, lin.G).
	const int np = m_np, nx = m_nx, nu = m_nu, c0 = m_constCount, nv = nu - c0, ny = m_ny;
	const int w1 = nx + nv, nr = nx + ny;
	auto &lin = m_lin;
	const double *v0 = lin.v0.data();
	const Level &L = m_levels[0];
	lin.lu.assign(size_t(np * np), 0.0);
	for (int a = 0; a < np; ++a)
		lin.lu[size_t(a * np + a)] = 1;
	for (const auto &d : m_devices)
		for (int a = 0; a < np; ++a) {
			const double *f = &L.F[size_t(a * np)];
			for (int q = d.port; q < d.port + d.ports; ++q) {
				if (f[q] == 0)
					continue;
				const double *g = &lin.G[size_t(q * np)];
				for (int b = d.port; b < d.port + d.ports; ++b)
					lin.lu[size_t(a * np + b)] -= f[q] * g[b];
			}
		}
	lin.piv.assign(size_t(np), 0);
	if (!luDecompose(lin.lu.data(), np, lin.piv.data()))
		return false;
	// Zf = M^-1 F (a current at each port to the ports' voltages, the devices as linearised), and
	// each port's zeff (deviceWindow): the largest weighted entry of its column
	lin.Zf.assign(np, np);
	lin.col.assign(size_t(np), 0.0);
	for (int q = 0; q < np; ++q) {
		for (int a = 0; a < np; ++a)
			lin.col[size_t(a)] = L.F[size_t(a * np + q)];
		luSolve(lin.lu.data(), np, lin.piv.data(), lin.col.data());
		for (int a = 0; a < np; ++a)
			lin.Zf(a, q) = lin.col[size_t(a)];
	}
	std::vector<double> &c = lin.c, &col = lin.col;
	c.assign(size_t(np), 0.0);
	for (const auto &d : m_devices)
		for (int a = d.port; a < d.port + d.ports; ++a) {
			double s = lin.i0[size_t(a)];
			for (int b = d.port; b < d.port + d.ports; ++b)
				s -= lin.G[size_t(a * np + b)] * v0[b];
			c[size_t(a)] = s;
		}
	lin.mRow = np + (np & 1);
	lin.KM.assign(lin.mRow + nr, w1);
	lin.kvm.assign(size_t(lin.KM.ld), 0.0);
	lin.fused.assign(size_t(lin.KM.ld), 0.0);
	for (int k = 0; k < w1; ++k) {
		for (int a = 0; a < np; ++a)
			col[size_t(a)] = L.P[size_t(a * w1 + k)];
		luSolve(lin.lu.data(), np, lin.piv.data(), col.data());
		for (int a = 0; a < np; ++a)
			lin.KM(a, k) = col[size_t(a)];
	}
	for (int a = 0; a < np; ++a) {
		double s = L.pConst[size_t(a)];
		for (int q = 0; q < np; ++q)
			s += L.F[size_t(a * np + q)] * c[size_t(q)];
		col[size_t(a)] = s;
	}
	luSolve(lin.lu.data(), np, lin.piv.data(), col.data());
	copyN(col.data(), np, lin.kvm.data());
	stepRows();
	lin.rowsStale = false;
	// over this many updates a step, keeping M costs more than the rows from the currents (the
	// operation counts' ratio, times 2 as measured on the busy kit with the updates' column pass)
	lin.rowsRate = 2 * double(nr * np) / (2.0 * nr * (np + 2 * w1) + 4 * np);
	lin.zeff.assign(size_t(np), 0.0);
	for (int q = 0; q < np; ++q)
		lin.zeff[size_t(q)] = zeffOf(q);
	return true;
}

void Circuit::stepRows()
{
	// The currents as functions of [x | u] (Ic, ki), then the states' and outputs' rows (states
	// first): M = [A B; Dy Ey] + Cs Ic, mConst = [xConst; yConst] + Cs ki, Cs their currents' columns.
	const int np = m_np, nx = m_nx, nu = m_nu, c0 = m_constCount, nv = nu - c0, ny = m_ny;
	const int w1 = nx + nv, w2 = w1 + np, nr = nx + ny;
	auto &lin = m_lin;
	const double *v0 = lin.v0.data();
	const Level &L = m_levels[0];
	std::vector<double> &c = lin.c;
	c.assign(size_t(np), 0.0);
	for (const auto &d : m_devices)
		for (int a = d.port; a < d.port + d.ports; ++a) {
			double s = lin.i0[size_t(a)];
			for (int b = d.port; b < d.port + d.ports; ++b)
				s -= lin.G[size_t(a * np + b)] * v0[b];
			c[size_t(a)] = s;
		}
	std::vector<double> &Ic = lin.Ic, &ki = lin.ki;
	Ic.assign(size_t(np * w1), 0.0);
	ki.assign(size_t(np), 0.0);
	for (const auto &d : m_devices)
		for (int a = d.port; a < d.port + d.ports; ++a) {
			double *ic = &Ic[size_t(a * w1)];
			for (int b = d.port; b < d.port + d.ports; ++b) {
				const double g = lin.G[size_t(a * np + b)];
				if (g == 0)
					continue;
				for (int k = 0; k < w1; ++k)
					ic[k] += g * lin.KM(b, k);
				ki[size_t(a)] += g * lin.kvm[size_t(b)];
			}
			ki[size_t(a)] += c[size_t(a)];
		}
	lin.Cs.assign(nr, np);
	const int m0 = lin.mRow; // M's rows in KM, mConst's in kvm
	std::vector<double> &mr = lin.col;
	mr.resize(std::max(mr.size(), size_t(w1)));
	for (int r = 0; r < nr; ++r) {
		const int src = r < nx ? ny + r : r - nx;
		const double *srow = &L.S[size_t(src * w2)];
		for (int k = 0; k < w1; ++k)
			mr[size_t(k)] = srow[k];
		double s = L.sConst[size_t(src)];
		for (int q = 0; q < np; ++q) {
			const double cq = srow[w1 + q];
			lin.Cs(r, q) = cq;
			if (cq == 0)
				continue;
			const double *ic = &Ic[size_t(q * w1)];
			for (int k = 0; k < w1; ++k)
				mr[size_t(k)] += cq * ic[k];
			s += cq * ki[size_t(q)];
		}
		for (int k = 0; k < w1; ++k)
			lin.KM(m0 + r, k) = mr[size_t(k)];
		lin.kvm[size_t(m0 + r)] = s;
	}
}

void Circuit::linearRows(const double *x, const double *u, const double *v, double *xn, double *y)
{
	const int np = m_np, nx = m_nx, nv = m_nu - m_constCount, ny = m_ny;
	auto &lin = m_lin;
	if (!lin.rowsStale) {
		colMatVec2<false>(lin.KM, lin.mRow, nx, nx, x, nv, u, lin.kvm.data() + lin.mRow, xn);
		colMatVec2<false>(lin.KM, lin.mRow + nx, ny, nx, x, nv, u, lin.kvm.data() + lin.mRow + nx, y);
		return;
	}
	// the devices' currents as linearised at the ports v, then S's rows
	// over [x | u | i]
	double *il = lin.il.data();
	for (const auto &d : m_devices)
		for (int a = d.port; a < d.port + d.ports; ++a) {
			double s = lin.i0[size_t(a)];
			const double *g = &lin.G[size_t(a * np)];
			for (int b = d.port; b < d.port + d.ports; ++b)
				s += g[b] * (v[b] - lin.v0[size_t(b)]);
			il[a] = s;
		}
	const Level &L = m_levels[0];
	// S's rows: the states' (from row ny), then the outputs'
	colMatVec2<false>(L.Sc, ny, nx, nx, x, nv, u, &L.sConst[size_t(ny)], xn);
	colMatVec2<false>(L.Sc, 0, ny, nx, x, nv, u, L.sConst.data(), y);
	colMatVec<true>(lin.Cs, 0, nx, np, nullptr, il, xn);
	colMatVec<true>(lin.Cs, nx, ny, np, nullptr, il, y);
}

double Circuit::zeffOf(int q) const
{
	// The most a unit current error at port q moves a port, weighted as Newton judges it: the
	// largest entry of Zf's column q
	const auto &lin = m_lin;
	const int np = m_np;
	double z = 0;
	for (int a = 0; a < np; ++a)
		z = std::max(z, std::abs(lin.Zf(a, q)) * m_portWeight[size_t(a)]);
	return z;
}

// A device's update (Circuit::relinearize), column by column (K and M share their columns, KM):
// T = S^-1 dG K_P from K's rows P
// before the update, then K += Z T; (while M's rows are kept) DK = dG K'_P from K's rows after it,
// and M += CZ T + Cs[:, P] DK; Zf likewise, Zf += Z (S^-1 dG Zf_P). T's columns first, so the updates
// are independent multiply-adds down the columns, two rows at a time (the padding row too: its
// terms are zeros); each entry's terms are added in the order separate passes over T, K, DK and M
// add them. T: scratch for k x (K's columns + Zf's).
template <int k> [[gnu::always_inline]] inline void relinT(const double *c, int P, const double *W, double *t)
{
	for (int a = 0; a < k; ++a) {
		double s = 0;
		for (int b = 0; b < k; ++b) {
			const double f = W[a * k + b];
			if (f != 0)
				s += f * c[P + b];
		}
		t[a] = s;
	}
}

template <int k> [[gnu::always_inline]] inline void relinUpdate(double *__restrict c, int ld, const double *__restrict Zc, const double *t)
{
	Pair tj[k];
	for (int j = 0; j < k; ++j)
		tj[j] = pairDup(t[j]);
	for (int r = 0; r < ld; r += 2) {
		Pair s = pairLoad(c + r);
		for (int j = 0; j < k; ++j)
			s = pairMadd(s, pairLoad(Zc + j * ld + r), tj[j]);
		pairStore(c + r, s);
	}
}

template <int k, bool rows>
void relinColumns(ColMat &KM, int mRow, int ldm, const ColMat &Cs, ColMat &Zf, int w1, int P, const double *SdG, const double *dG,
                  const double *Zc, const double *cz, double *T)
{
	// K is KM's first mRow rows (padded to even), M its next ldm
	const int ldk = mRow, nz = Zf.cols;
	for (int q = 0; q < w1; ++q)
		relinT<k>(KM.col(q), P, SdG, T + q * k);
	for (int q = 0; q < nz; ++q)
		relinT<k>(Zf.col(q), P, SdG, T + (w1 + q) * k);
	for (int q = 0; q < w1; ++q)
		relinUpdate<k>(KM.col(q), ldk, Zc, T + q * k);
	for (int q = 0; q < nz; ++q)
		relinUpdate<k>(Zf.col(q), ldk, Zc, T + (w1 + q) * k);
	if (!rows)
		return;
	for (int q = 0; q < w1; ++q) {
		const double *kc = KM.col(q), *t = T + q * k;
		Pair tj[k], dk[k];
		for (int a = 0; a < k; ++a) {
			double s = 0;
			for (int b = 0; b < k; ++b) {
				const double g = dG[a * k + b];
				if (g != 0)
					s += g * kc[P + b];
			}
			dk[a] = pairDup(s);
			tj[a] = pairDup(t[a]);
		}
		double *__restrict mc = KM.col(q) + mRow;
		for (int r = 0; r < ldm; r += 2) {
			Pair s = pairLoad(mc + r);
			for (int j = 0; j < k; ++j) {
				s = pairMadd(s, pairLoad(cz + j * ldm + r), tj[j]);
				s = pairMadd(s, pairLoad(Cs.col(P + j) + r), dk[j]);
			}
			pairStore(mc + r, s);
		}
	}
}

bool Circuit::relinearize(const Device &d, const double *v, const char *&why)
{
	switch (d.ports) {
	case 1: return relinearizeT<1>(d, v, why);
	case 2: return relinearizeT<2>(d, v, why);
	default: return relinearizeT<3>(d, v, why);
	}
}

template <int K> bool Circuit::relinearizeT(const Device &d, const double *v, const char *&why)
{
	// Device d linearised again at v, the rest as they were: G changes in d's block by dG, c by dc,
	// so M = I - F G by a rank-k update (k = its ports), and the step's matrices with it (Woodbury):
	// with Z = M^-1 F[:, P] and S = I - dG Z_P, K += Z S^-1 dG K_P, kv += Z S^-1 (dG kv_P + dc_P),
	// M^-1 += Z S^-1 dG (M^-1)_P; the currents' map G K changes by (G Z) T + E_P dG K'_P.
	auto &lin = m_lin;
	const int np = m_np, nx = m_nx, nv = m_nu - m_constCount, ny = m_ny, w1 = nx + nv, nr = nx + ny;
	constexpr int k = K; // d.ports, compiled for each count (the small loops unrolled)
	const int P = d.port;
	// not at a point Newton itself wouldn't step to: a junction up past its critical voltage by
	// more than a few V_T (pnjlim's), a channel half a volt away
	for (int q = P; q < P + k; ++q) {
		const double vt = m_portVt[size_t(q)], dv = v[q] - lin.v0[size_t(q)];
		if (vt > 0 && m_portSign[size_t(q)] * v[q] > m_portVcrit[size_t(q)] && m_portSign[size_t(q)] * dv > 2 * vt) {
			why = "past vcrit";
			return false;
		}
	}
	for (const auto &mo : m_mos)
		if (mo.port == P && (std::abs(v[P] - lin.v0[size_t(P)]) > 0.5 || std::abs(v[P + 1] - lin.v0[size_t(P + 1)]) > 0.5)) {
			why = "channel step";
			return false;
		}
	double *ei = lin.evI.data(), *ej = lin.evJac.data();
	deviceEval(d, v, ei, ej);
	double dG[9], dc[3], S[9], Si[9];
	for (int a = 0; a < k; ++a) {
		double cOld = lin.i0[size_t(P + a)], cNew = ei[P + a];
		for (int b = 0; b < k; ++b) {
			const double gNew = ej[(P + a) * np + P + b], gOld = lin.G[size_t((P + a) * np + P + b)];
			dG[a * k + b] = gNew - gOld;
			cOld -= gOld * lin.v0[size_t(P + b)];
			cNew -= gNew * v[P + b];
		}
		dc[a] = cNew - cOld;
	}
	// Z = M^-1 F[:, P] (np x k): Zf's columns, copied (Zc, by columns ldz apart)
	const int ldz = lin.Zf.ld;
	double *Zc = lin.Zc.data();
	for (int j = 0; j < k; ++j)
		copyN(lin.Zf.col(P + j), ldz, Zc + j * ldz);
	for (int a = 0; a < k; ++a)
		for (int b = 0; b < k; ++b) {
			double s = a == b ? 1.0 : 0.0;
			for (int q = 0; q < k; ++q)
				s -= dG[a * k + q] * Zc[b * ldz + P + q];
			S[a * k + b] = s;
		}
	double zeffSave[3];
	for (int j = 0; j < k; ++j)
		zeffSave[j] = lin.zeff[size_t(P + j)];
	// S^-1 (k <= 3), by Gauss-Jordan with partial pivoting
	{
		double A[9];
		std::copy(S, S + k * k, A);
		for (int a = 0; a < k * k; ++a)
			Si[a] = 0;
		for (int a = 0; a < k; ++a)
			Si[a * k + a] = 1;
		for (int c = 0; c < k; ++c) {
			int pr = c;
			for (int r = c + 1; r < k; ++r)
				if (std::abs(A[r * k + c]) > std::abs(A[pr * k + c]))
					pr = r;
			if (std::abs(A[pr * k + c]) < kRelinPivot) { // the update would make I - F G (nearly) singular
				why = "singular";
				return false;
			}
			for (int q = 0; q < k; ++q) {
				std::swap(A[c * k + q], A[pr * k + q]);
				std::swap(Si[c * k + q], Si[pr * k + q]);
			}
			const double inv = 1 / A[c * k + c];
			for (int q = 0; q < k; ++q) {
				A[c * k + q] *= inv;
				Si[c * k + q] *= inv;
			}
			for (int r = 0; r < k; ++r) {
				if (r == c)
					continue;
				const double f = A[r * k + c];
				if (f == 0)
					continue;
				for (int q = 0; q < k; ++q) {
					A[r * k + q] -= f * A[c * k + q];
					Si[r * k + q] -= f * Si[c * k + q];
				}
			}
		}
	}
	// its window there, with its ports' zeff as the update makes them: M'^-1 F[:, P] = Z S^-1
	for (int j = 0; j < k; ++j) {
		double zm = 0;
		for (int a = 0; a < np; ++a) {
			double s = 0;
			for (int q = 0; q < k; ++q)
				s += Zc[q * ldz + a] * Si[q * k + j];
			zm = std::max(zm, std::abs(s) * m_portWeight[size_t(a)]);
		}
		lin.zeff[size_t(P + j)] = zm;
	}
	if (deviceWindow(d, v, why) != 0) {
		for (int j = 0; j < k; ++j)
			lin.zeff[size_t(P + j)] = zeffSave[j];
		return false;
	}
	double SdG[9]; // S^-1 dG
	for (int a = 0; a < k; ++a)
		for (int b = 0; b < k; ++b) {
			double s = 0;
			for (int q = 0; q < k; ++q)
				s += Si[a * k + q] * dG[q * k + b];
			SdG[a * k + b] = s;
		}
	// t = S^-1 (dG kv_P + dc) (T = S^-1 dG K_P, k x w1, is worked out column by column below)
	double t[3];
	for (int a = 0; a < k; ++a) {
		double s = 0;
		for (int b = 0; b < k; ++b) {
			double dgk = dc[b];
			for (int q = 0; q < k; ++q)
				dgk += dG[b * k + q] * lin.kvm[size_t(P + q)];
			s += Si[a * k + b] * dgk;
		}
		t[a] = s;
	}
	// G Z with the old G (np x k), then Cs G Z (nr x k, by columns); none of the rows' terms while
	// they're left stale (linearRows works them out from the currents)
	double *GZ = lin.GZ.data(), *cz = lin.CZc.data();
	const int ldm = nr + (nr & 1); // M's rows in KM, padded (and Cs's, cz's)
	const bool rows = !lin.rowsStale;
	if (rows) {
		for (const auto &e : m_devices)
			for (int a = e.port; a < e.port + e.ports; ++a)
				for (int j = 0; j < k; ++j) {
					double s = 0;
					for (int b = e.port; b < e.port + e.ports; ++b)
						s += lin.G[size_t(a * np + b)] * Zc[j * ldz + b];
					GZ[a * k + j] = s;
				}
		double *gz = lin.gzc.data();
		for (int j = 0; j < k; ++j) {
			for (int q = 0; q < np; ++q)
				gz[q] = GZ[q * k + j];
			colMatVec<false>(lin.Cs, 0, nr, np, lin.zeros.data(), gz, cz + j * ldm);
			if (ldm > nr)
				cz[j * ldm + nr] = 0;
		}
	}
	// kv += Z t
	for (int a = 0; a < np; ++a)
		for (int j = 0; j < k; ++j)
			if (Zc[j * ldz + a] != 0)
				lin.kvm[size_t(a)] += Zc[j * ldz + a] * t[j];
	// the constants' rows: + CZ t + Cs[:, P] (dG kv'_P + dc)
	double dkv[3];
	for (int a = 0; rows && a < k; ++a) {
		double s = dc[a];
		for (int b = 0; b < k; ++b) {
			const double g = dG[a * k + b];
			if (g == 0)
				continue;
			s += g * lin.kvm[size_t(P + b)];
		}
		dkv[a] = s;
	}
	// K += Z T, and the states' and outputs' rows M += CZ T + Cs[:, P] (dG K'_P)
	switch (k * 2 + (rows ? 1 : 0)) {
	case 2: relinColumns<1, false>(lin.KM, lin.mRow, ldm, lin.Cs, lin.Zf, w1, P, SdG, dG, Zc, cz, lin.T.data()); break;
	case 3: relinColumns<1, true>(lin.KM, lin.mRow, ldm, lin.Cs, lin.Zf, w1, P, SdG, dG, Zc, cz, lin.T.data()); break;
	case 4: relinColumns<2, false>(lin.KM, lin.mRow, ldm, lin.Cs, lin.Zf, w1, P, SdG, dG, Zc, cz, lin.T.data()); break;
	case 5: relinColumns<2, true>(lin.KM, lin.mRow, ldm, lin.Cs, lin.Zf, w1, P, SdG, dG, Zc, cz, lin.T.data()); break;
	case 6: relinColumns<3, false>(lin.KM, lin.mRow, ldm, lin.Cs, lin.Zf, w1, P, SdG, dG, Zc, cz, lin.T.data()); break;
	default: relinColumns<3, true>(lin.KM, lin.mRow, ldm, lin.Cs, lin.Zf, w1, P, SdG, dG, Zc, cz, lin.T.data()); break;
	}
	if (rows)
		for (int r = 0; r < nr; ++r) {
			double s = lin.kvm[size_t(lin.mRow + r)];
			for (int j = 0; j < k; ++j)
				s += cz[j * ldm + r] * t[j] + lin.Cs(r, P + j) * dkv[j];
			lin.kvm[size_t(lin.mRow + r)] = s;
		}
	// the device's new point
	for (int a = 0; a < k; ++a) {
		lin.i0[size_t(P + a)] = ei[P + a];
		lin.v0[size_t(P + a)] = v[P + a];
		for (int b = 0; b < k; ++b)
			lin.G[size_t((P + a) * np + P + b)] = ej[(P + a) * np + P + b];
	}
	++lin.updates;
	++m_stats.linearizations;
	return true;
}

bool Circuit::linearStep(const double *u)
{
	auto &lin = m_lin;
	const int np = m_np, nx = m_nx, c0 = m_constCount, nv = m_nu - c0, ny = m_ny;
	const Level &L = m_levels[0];
	const int w1 = nx + nv;
	// the step's [x | u]: the states (the companion states the last step left, or worked out from
	// the capacitors) and the inputs that vary, read where they are kept
	double *x = m_x.data();
	if (!m_xValid) {
		syncCaps();
		for (int k = 0; k < nx; ++k)
			x[k] = L.gc[size_t(k)] * m_vc[size_t(k)] + (L.be[size_t(k)] ? 0 : m_ic[size_t(k)]);
	}
	const double *uv = u + c0;
	// The ports first. A device whose ports leave their windows is linearised again where they
	// went (a rank update, as long as its own region allows it), and the ports worked out again;
	// otherwise the step is Newton's.
	// While M is kept (its rows not stale), the first round works out the next states and the
	// outputs with the ports, in one pass down KM's columns (fused: [v; xn; y]); they stand unless
	// a device is linearised again.
	double *v = m_r.data();
	bool fused = !lin.rowsStale;
	for (int round = 0;; ++round) {
		int out = 0, first = -1;
		if (round == 0 && fused) {
			colMatVec2<false>(lin.KM, 0, lin.mRow + m_nx + ny, nx, x, nv, uv, lin.kvm.data(), lin.fused.data());
			copyN(lin.fused.data(), np, v);
		} else {
			fused = false;
			colMatVec2<false>(lin.KM, 0, np, nx, x, nv, uv, lin.kvm.data(), v);
		}
		if (!anyOutside(v, lin.lo.data(), lin.hi.data(), np, kLinVoltsMax))
			break;
		for (int a = 0; a < np; ++a) {
			const double s = v[a];
			if (!(std::abs(s) < kLinVoltsMax)) { // the update went wrong somewhere: Newton's
				lin.exitPort = a;
				return false;
			}
			if (s < lin.lo[size_t(a)] || s > lin.hi[size_t(a)]) {
				if (first < 0)
					first = a;
				const int dv = m_portDevice[size_t(a)];
				if (out == 0 || lin.relin[size_t(out - 1)] != dv)
					lin.relin[size_t(out++)] = dv;
			}
		}
		if (first < 0)
			break;
		bool ok = kRelin && round < kRelinRounds && out <= kRelinDevices;
		for (int j = 0; ok && j < out; ++j) {
			const int dv = lin.relin[size_t(j)];
			const Device &d = m_devices[size_t(dv)];
			const char *why = "";
			// the window it would have there (relinearize writes it), the old one kept if not
			for (int q = d.port; q < d.port + d.ports; ++q) {
				lin.loSave[size_t(q - d.port)] = lin.lo[size_t(q)];
				lin.hiSave[size_t(q - d.port)] = lin.hi[size_t(q)];
			}
			const auto rt0 = kHist ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
			const bool relinOk = relinearize(d, v, why);
			if (kHist) {
				histOf(m_name).relins["(relin ns)"] += long(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - rt0).count());
				histOf(m_name).relins["(relin calls)"]++;
			}
			if (!relinOk) {
				for (int q = d.port; q < d.port + d.ports; ++q) {
					lin.lo[size_t(q)] = lin.loSave[size_t(q - d.port)];
					lin.hi[size_t(q)] = lin.hiSave[size_t(q - d.port)];
				}
				ok = false;
			} else {
				lin.relinRate += kRelinRateK;
				if (kHist)
					histOf(m_name).relins[portName(d.port)]++;
			}
		}
		if (!ok) {
			lin.exitPort = first;
			if (kHist)
				histOf(m_name).exits[portName(first) + (v[first] < lin.lo[size_t(first)] ? " low" : " high")]++;
			if (kLinDebug)
				std::fprintf(stderr, "linear %s: port %d at %.6g left [%.6g, %.6g] (v0 %.6g) after %ld\n", m_name.c_str(), first, v[first],
				             lin.lo[size_t(first)], lin.hi[size_t(first)], lin.v0[size_t(first)], lin.since);
			return false;
		}
	}
	std::swap(lin.v, m_r); // the ports at this step (v stays valid: m_r is scratch)
	static const char *verify = std::getenv("TR909_LINVERIFY");
	if (verify && m_name.find(verify) != std::string::npos) {
		// the linear step against Newton's for the same step: the capacitors' voltages after it
		const int w2 = w1 + np;
		double *w = m_w.data();
		copyN(x, nx, w);
		copyN(uv, nv, w + nx);
		std::vector<double> vt(v, v + np), it(static_cast<size_t>(np)), pp(static_cast<size_t>(np)), vlin(v, v + np);
		for (int a = 0; a < np; ++a) {
			double s = L.pConst[size_t(a)];
			for (int k = 0; k < w1; ++k)
				s += L.P[size_t(a * w1 + k)] * w[k];
			pp[size_t(a)] = s;
		}
		m_current = false;
		Level &LL = m_levels[0];
		const bool conv = newton(LL, pp.data(), vt.data(), it.data(), 60);
		m_current = false;
		std::copy(vlin.begin(), vlin.end(), v);
		double worst = 0;
		int wc = -1;
		linearRows(x, uv, v, lin.rows.data(), lin.rows.data() + nx);
		for (int r = 0; r < nx; ++r) {
			double sl = lin.rows[size_t(r)], st = L.sConst[size_t(ny + r)];
			const double *srow = &L.S[size_t((ny + r) * w2)];
			for (int k = 0; k < w1; ++k)
				st += srow[k] * w[k];
			for (int q = 0; q < np; ++q)
				st += srow[w1 + q] * it[size_t(q)];
			const double e = std::abs(sl - st) * L.vcScale[size_t(r)];
			if (e > worst) {
				worst = e;
				wc = r;
			}
		}
		if (worst > kLinTolerance || !conv) {
			int wp = 0;
			for (int a = 0; a < np; ++a)
				if (std::abs(vlin[size_t(a)] - vt[size_t(a)]) * m_portWeight[size_t(a)] > std::abs(vlin[size_t(wp)] - vt[size_t(wp)]) * m_portWeight[size_t(wp)])
					wp = a;
			std::fprintf(stderr, "%s step %ld: linear step's capacitor %s off by %.3g V%s; worst port %s %.6g (Newton %.6g, window [%.6g %.6g], v0 %.6g), updates %ld\n",
			             m_name.c_str(), m_stats.steps, wc >= 0 ? m_net.elements[size_t(m_caps[size_t(wc)])].name.c_str() : "-", worst, conv ? "" : " (Newton failed)",
			             portName(wp).c_str(), vlin[size_t(wp)], vt[size_t(wp)], lin.lo[size_t(wp)], lin.hi[size_t(wp)], lin.v0[size_t(wp)], lin.updates);
			// the device whose linearisation error moves that capacitor most (through Newton's M^-1)
			if (wc >= 0) {
				std::vector<double> itl(static_cast<size_t>(np)), jtl(static_cast<size_t>(np * np));
				deviceCurrents(vlin.data(), itl.data(), jtl.data());
				for (const auto &d : m_devices) {
					double e = 0;
					for (int q = d.port; q < d.port + d.ports; ++q) {
						double il = lin.i0[size_t(q)];
						for (int b = d.port; b < d.port + d.ports; ++b)
							il += lin.G[size_t(q * np + b)] * (vlin[size_t(b)] - lin.v0[size_t(b)]);
						e = std::max(e, std::abs(itl[size_t(q)] - il));
					}
					if (e > 1e-12)
						std::fprintf(stderr, "    %s: current off by %.3g A (v %.6g, v0 %.6g)\n", portName(d.port).c_str(), e, vlin[size_t(d.port)], lin.v0[size_t(d.port)]);
				}
			}
		}
	}
	if (fused) {
		copyN(lin.fused.data() + lin.mRow, nx, m_xn.data());
		copyN(lin.fused.data() + lin.mRow + nx, ny, m_y.data());
	} else {
		linearRows(x, uv, v, m_xn.data(), m_y.data());
	}
	// the step's states become the last step's, its next states the states (m_xn is scratch)
	std::swap(m_xpLast, m_x);
	std::swap(m_x, m_xn);
	m_xValid = true;
	m_capsStale = true;
	++lin.since;
	++m_stats.linearSteps;
	lin.relinRate *= 1 - kRelinRateK;
	// updating M at every update costs more than the rows from the currents once updates come
	// often (a VCA through a hit); back to M (rebuilt) once they have become rare
	if (kRowsStale) {
		if (!lin.rowsStale && lin.relinRate > 1.25 * lin.rowsRate) {
			lin.rowsStale = true;
		} else if (lin.rowsStale && lin.relinRate < 0.5 * lin.rowsRate) {
			stepRows();
			lin.rowsStale = false;
		}
	}
	// many updates on top of each other: linearised afresh from here (the rounding of the updates
	// doesn't build up)
	if (lin.updates >= kRelinRefresh) {
		m_v = lin.v;
		const int wait = lin.wait, backoff = lin.backoff;
		if (!linearize()) {
			leaveLinear();
			lin.wait = wait;
			lin.backoff = backoff;
		}
	}
	return true;
}

void Circuit::leaveLinear()
{
	m_prevValid = false;
	// Back to Newton from the last linear step: its ports, and their currents on the linearisation.
	auto &lin = m_lin;
	const int np = m_np;
	m_v = lin.v;
	for (int a = 0; a < np; ++a) {
		double s = lin.i0[size_t(a)];
		for (int b = 0; b < np; ++b)
			s += lin.G[size_t(a * np + b)] * (lin.v[size_t(b)] - lin.v0[size_t(b)]);
		m_i[size_t(a)] = s;
	}
	m_current = false;
	lin.on = false;
	// A run that paid for its linearisation earns quick re-entry; a short one waits longer each
	// time. A comparator's input
	// reaching its flip ends a run as it should (a VCO's half period): back soon after the flip.
	bool flip = false;
	for (const auto &c : m_comps)
		flip = flip || (c.comparator && (lin.exitPort == c.slew || lin.exitPort == c.lim));
	lin.exitPort = -1;
	if (flip) {
		lin.wait = 4;
		lin.backoff = 8;
		return;
	}
	if (lin.since > kLinPays)
		lin.backoff = 8;
	lin.wait = lin.since > kLinPays ? 1 : lin.backoff;
	if (lin.since <= kLinPays)
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
	solveFull(m_xpLast.data(), m_u.data(), m_i.data(), z);
	return z[size_t(node - 1)];
}

bool Circuit::solveDc()
{
	buildIfDirty();
	// The DC system: no capacitors (open). SPICE's gmin is across the junctions, in their currents.
	// With the op-amps' open-loop gain scaled by gainScale (the output stages that are VCVSs here,
	// the limiters in deviceCurrents) for gain stepping.
	const int n = m_n;
	std::vector<double> S, SiNu(static_cast<size_t>(n * m_nu)), SiNn(static_cast<size_t>(n * m_np)), col(static_cast<size_t>(n));
	std::vector<int> piv(static_cast<size_t>(n));
	std::vector<double> E(static_cast<size_t>(m_np * m_nu));
	Level dc;
	dc.F.assign(size_t(m_np * m_np), 0.0);
	dc.luM.assign(size_t(m_np * m_np), 0.0);
	dc.luPiv.assign(size_t(m_np), 0);
	dc.cols.assign(size_t(m_np * m_np), 0.0);
	dc.active.assign(size_t(m_np), 0);
	dc.pos.assign(size_t(m_np), -1);
	dc.fColMax.assign(size_t(m_np), 1e12); // every current counts in the DC solve
	dc.FT.assign(size_t(m_np * m_np), 0.0);
	auto diff = [&](const std::vector<double> &M, int cols, int c, int plus, int minus) {
		double v = 0;
		if (plus > 0) v += M[size_t((plus - 1) * cols + c)];
		if (minus > 0) v -= M[size_t((minus - 1) * cols + c)];
		return v;
	};
	auto setup = [&](double gainScale) {
		S = m_G;
		if (gainScale != 1)
			for (const auto &o : m_opOut)
				S[size_t(o.row * n + o.in - 1)] = -o.gain * gainScale;
		if (!luDecompose(S.data(), n, piv.data()))
			return false;
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
		for (int p = 0; p < m_np; ++p) {
			for (int c = 0; c < m_nu; ++c)
				E[size_t(p * m_nu + c)] = diff(SiNu, m_nu, c, m_portPlus[size_t(p)], m_portMinus[size_t(p)]);
			for (int c = 0; c < m_np; ++c)
				dc.F[size_t(p * m_np + c)] = diff(SiNn, m_np, c, m_portPlus[size_t(p)], m_portMinus[size_t(p)]);
		}
		for (int a = 0; a < m_np; ++a)
			for (int q = 0; q < m_np; ++q)
				dc.FT[size_t(q * m_np + a)] = dc.F[size_t(a * m_np + q)];
		toColumns(dc.F.data(), m_np, m_np, m_np, dc.Fc);
		return true;
	};
	if (!setup(1))
		return false;
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
		// gmin stepping, as SPICE's: a conductance across every port, from 10 mS down by decades
		// (by half decades where a decade fails), each solve starting from the last
		std::fill(v.begin(), v.end(), 0.0);
		double g = 1e-2, step = 10;
		m_dcGmin = g;
		ok = solveAt(1);
		std::vector<double> vGood = v;
		while (ok && m_dcGmin > 0) {
			const double next = g / step < 1e-13 ? 0 : g / step;
			m_dcGmin = next;
			if (solveAt(1)) {
				g = next;
				vGood = v;
				step = std::min(step * 2, 10.0);
			} else if (step > 1.2) {
				v = vGood;
				m_dcGmin = g;
				step = std::sqrt(step);
			} else {
				ok = false;
			}
		}
		if (kDcDebug)
			std::fprintf(stderr, "DC: gmin stepping %s at %g\n", ok ? "converged" : "failed", g);
		m_dcGmin = 0;
	}
	if (!ok && !(m_opOut.empty() && m_limiterPorts.empty())) {
		// gain stepping: the op-amps' open-loop gain from 1 up to its own, by half decades (less
		// where that fails), following the solution; a VCO's loop (integrator, comparator, CMOS
		// switch) has its one operating point on a knife's edge at full gain, which Newton finds
		// only from close by
		std::fill(v.begin(), v.end(), 0.0);
		std::vector<double> vcrit = m_portVcrit;
		auto at = [&](double f) {
			m_dcGain = f;
			for (int q : m_limiterPorts)
				m_portVcrit[size_t(q)] = vcrit[size_t(q)] * f;
			return setup(f) && solveAt(1);
		};
		double f = 1e-5, step = std::sqrt(10.0);
		ok = at(f);
		std::vector<double> vGood = v;
		while (ok && f < 1) {
			const double next = std::min(f * step, 1.0);
			if (at(next)) {
				f = next;
				vGood = v;
				step = std::min(step * step, std::sqrt(10.0));
			} else if (step > 1.05) {
				v = vGood;
				step = std::sqrt(step);
			} else {
				ok = false;
			}
		}
		if (kDcDebug)
			std::fprintf(stderr, "DC: gain stepping %s at %g\n", ok ? "converged" : "failed", f);
		m_dcGain = 1;
		m_portVcrit = vcrit;
		setup(1);
		if (ok)
			ok = solveAt(1);
	}
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
	m_dcFailed = !ok;
	if (!ok) {
		if (kDcDebug)
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
		m_xpLast[size_t(k)] = m_levels[0].gc[size_t(k)] * m_vc[size_t(k)];
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
	o << "* exported by tr909::circuit::Circuit::toSpice\n";
	// cshunt: ngspice's capacitance from every node to ground, which the solver doesn't have. 0.1 pF
	// still steadies its timestep at a trigger's edge (the toms' IC15b failed at 10 fF, BD's Q1 with
	// none), where 1 pF ran the snare's VCO 0.06 % slow
	o << ".options reltol=1e-6 abstol=1e-13 vntol=1e-9 gmin=1e-12 method=trap cshunt=1e-13\n";
	for (int k : m_shuntNodes)
		o << "Rshunt_" << node(k).substr(2) << " " << node(k) << " 0 " << fmt(1 / kGshunt) << "\n";
	if (!m_dirty) {
		// This circuit's operating point (its last step's state) as ngspice's first guess: without a
		// shunt on every node, nodes between junctions that leakage alone sets (a transistor's base
		// on an island) defeat its gmin and source stepping; where there are several (a VCO's
		// unstable one and its saturated ones), the same one. Where solveDc failed (a free-running
		// VCO), ngspice starts from this state instead (UIC), as the solver does.
		std::vector<double> z;
		solveFull(m_xpLast.data(), m_u.data(), m_i.data(), z);
		const int nodes = int(m_net.nodes.size()) - 1;
		for (int k = 1; k <= nodes; ++k)
			o << (m_dcFailed ? ".ic V(" : ".nodeset V(") << node(k) << ")=" << fmt(z[size_t(k - 1)]) << "\n";
	}
	for (const auto &[name, m] : m_net.models) {
		if (m.type == Netlist::Model::Ota || m.type == Netlist::Model::Comparator || m.type == Netlist::Model::Limiter
		    || m.type == Netlist::Model::Slew)
			continue;
		if (m.type == Netlist::Model::Diode) {
			o << ".model " << name << " D(IS=" << fmt(m.is) << " N=" << fmt(m.n) << ")\n";
		} else if (m.type == Netlist::Model::Njf || m.type == Netlist::Model::Pjf) {
			o << ".model " << name << (m.type == Netlist::Model::Njf ? " NJF" : " PJF") << "(VTO=" << fmt(m.vto)
			  << " BETA=" << fmt(m.beta) << " LAMBDA=" << fmt(m.lambda) << " IS=" << fmt(m.is) << " N=" << fmt(m.n) << ")\n";
		} else if (m.type == Netlist::Model::Nmos || m.type == Netlist::Model::Pmos) {
			// level 1 without body effect (GAMMA 0), charge (no TOX, no junction or overlap
			// capacitances) or bulk junction currents (IS 0: only gmin across them)
			o << ".model " << name << (m.type == Netlist::Model::Nmos ? " NMOS" : " PMOS") << "(LEVEL=1 VTO=" << fmt(m.vto)
			  << " KP=" << fmt(m.kp) << " LAMBDA=" << fmt(m.lambda) << " IS=0)\n";
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
		case 'M': { // the bulk on the source
			const auto &m = m_net.models.at(e.model);
			o << nm << " " << node(e.nodes[0]) << " " << node(e.nodes[1]) << " " << node(e.nodes[2]) << " " << node(e.nodes[2]) << " "
			  << e.model << " W=" << fmt(m.w) << " L=" << fmt(m.l) << "\n";
			break;
		}
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
	o << ".tran " << fmt(step) << " " << fmt(seconds) << " 0 " << fmt(step) << (m_dcFailed && !m_dirty ? " uic" : "") << "\n";
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

} // namespace tr909::circuit
