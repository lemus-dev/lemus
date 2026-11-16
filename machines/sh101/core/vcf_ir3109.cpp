// The IR3109 VCF and its resonance loop, solved implicitly per sample (docs/model-filter.md 2).
#include "vcf_ir3109.h"

#include "osc_circuit.h"

#include <algorithm>
#include <cmath>

namespace sh101 {

namespace {

// A Newton step this small, unlimited, is accepted (solve()).
constexpr double kAcceptStep = 1e-4;

// Smooth min/max with a softplus of width w: the buffers' tops and TR26's saturation.
constexpr double kW = 0.05;
// Beyond 20 widths (1 V) the softplus is within 1e-10 V of its limit and the sigmoid within 2e-9,
// so skip the exp there: TR26 on the +14 V rail runs that close to saturation in self-oscillation.
double softplus(double z)
{
	if (z > 20 * kW)
		return z;
	if (z < -20 * kW)
		return 0;
	return kW * std::log1p(std::exp(z / kW));
}
double sigmoid(double z)
{
	if (z > 20 * kW)
		return 1;
	if (z < -20 * kW)
		return 0;
	return 1 / (1 + std::exp(-z / kW));
}

// tanh through expm1: within a couple of ulp of std::tanh, without cancellation near 0, and
// faster.
double otaTanh(double x)
{
	if (std::abs(x) > 19)
		return x > 0 ? 1.0 : -1.0;
	const double e = std::expm1(2 * x);
	return e / (e + 2);
}

// The networks' conductances as multipliers (the solver divides by none of them).
constexpr double kG113 = 1 / Ir3109Vcf::kR113, kG68 = 1 / Ir3109Vcf::kR68, kG129 = 1 / Ir3109Vcf::kR129;
constexpr double kG137 = 1 / Ir3109Vcf::kR137;
constexpr double kG2 = 2 / Ir3109Vcf::kR68 + 1 / Ir3109Vcf::kR560; // stages 2-4's input node
constexpr double kDudPrev = 1 / (Ir3109Vcf::kR68 * kG2);           // OUT into the next stage's input

} // namespace

void Ir3109Vcf::setParams(const Params &p)
{
	m_p = p;
	m_kIabc = -1;
}

void Ir3109Vcf::prepare(double fs)
{
	m_fs = fs;
	m_t = 1 / fs;
	// TR26: base at the bus (VR18 runs from OUT4 to the bus, OML's correction), emitter R131
	// 10k: 0.438 mA. Its collector gain is R136/(R131 + r_e), less TR27's base loading.
	// R132 (the output sum) loads the emitter too: 6.7 % more collector current per volt.
	const double ie26 = (5.0 - 0.62) / kR131;
	const double re26 = m_p.vt / ie26;
	const double beta = m_p.beta;
	const double rE = 1 / (1 / kR131 + 1 / kR130);
	m_follow26 = rE / (rE + re26);
	// DC: R135 from +14 V (SN p.7's filled arrow): C54's node 14.3 - (0.438 + 0.196) mA x 1k =
	// 13.67 V; TR26's collector 13.67 - 15k x 0.438 mA = 7.1 V; TR27's emitter a V_BE below,
	// 6.47 V, so 0.196 mA in R137. TR26 saturates when its collector falls to its emitter
	// (4.38 V) plus 0.15 V: an AC swing of -2.5 V plus the emitter's own swing.
	const double vc0 = oscm::kV14 - (ie26 + 0.196e-3) * 1e3 - kR136 * ie26;
	m_ie27 = (vc0 - 0.62) / kR137;
	m_cSat = (5.0 - 0.62) + 0.15 - vc0;
	// TR27's emitter can't leave the rails: ground and +14 V, against its rest.
	m_eLo = -(vc0 - 0.62);
	m_eHi = oscm::kV14 - (vc0 - 0.62);
	const double rLoad27 = 1 / (1 / kR137 + 1 / (kR129 + kR113));
	const double rin27 = (beta + 1) * (m_p.vt / m_ie27 + rLoad27);
	const double alphaF = beta / (beta + 1), load27 = rin27 / (rin27 + kR136);
	m_g26 = kR136 * alphaF * (1 / kR131 + 1 / kR130) * m_follow26 * load27;
	m_kJ = kR136 * alphaF / kR130 * load27; // R132's far end (C53's node) pushes back
	// The buffers. Loads against the bus: the two 68k each output drives (the IN nodes sit at the
	// bus within millivolts); OUT4 drives R122 68k, VR18 100k and R130 150k instead. Stages 1 and
	// 3 also feed their 20k pull-up, which sets their drain current; 2 and 4 run on their current
	// sources, which add their output resistance. At rest (OUT at the bus) the gain is about
	// g_m/(g_m + g_ds + b) (buffer()'s slope there), so each stage's integrator, and its pole, runs
	// that much slower; its DC gain (-1, from its 68k pair) is unchanged. The P-MOS's drain sits on
	// pin 8, so V_SD is pin 8's depth plus OUT: at rest (K/2) s^2 (1 + lambda V_SD) = a.
	const double gLoad[4] = {2 / kR68, 2 / kR68, 2 / kR68, 1 / kR68 + 1 / 100e3 + 1 / kR130};
	const double lam = m_p.bufLambda, vsd0 = m_p.pin8BelowBus;
	m_bufKw = m_p.bufK * lam;
	for (int i = 0; i < 4; ++i) {
		const bool pullUp = i == 0 || i == 2;
		m_bufA[i] = pullUp ? m_p.vccAboveBus / 20e3 : m_p.srcAmps;
		m_bufB[i] = gLoad[i] + (pullUp ? 1 / 20e3 : 1 / m_p.srcOhms);
		const double s0 = std::sqrt(2 * m_bufA[i] / (m_p.bufK * (1 + lam * vsd0)));
		m_bufK0[i] = m_p.bufK * (1 + lam * (vsd0 + s0));
		m_cap0[i] = -(m_p.bufVto + s0);
		double y, dy;
		buffer(i, m_cap0[i], y, dy);
		m_buf[i] = dy;
	}
	// The diode node can't carry more than R129 and R113 can feed it (a few mA).
	m_dMax = m_p.diodeN * m_p.vt * std::asinh(2e-3 / (2 * m_p.diodeIs));
	m_lnHalfIs = std::log(0.5 / m_p.otaIs);
	m_kIabc = -1;
	reset();
}

void Ir3109Vcf::buffer(int i, double c, double &y, double &dy) const
{
	// Square law: I_D = (K'/2) s^2 with s the overdrive and K' = K (1 + lambda V_SD), and
	// I_D = a - b (w + s) with w = c + |V_TO|. V_SD is taken at OUT = w plus the stage's overdrive
	// at rest (within 2 mV of OUT = w + s over the whole swing), so K' = K0 + Kw w.
	const double a = m_bufA[i], b = m_bufB[i];
	const double w = c + m_p.bufVto;
	const double q = a - b * w;
	if (q <= 0) {
		// Cut off: the pull-up against the load (stages 1 and 3's top).
		y = a / b;
		dy = 0;
	} else {
		const double k = m_bufK0[i] + m_bufKw * w;
		const double root = std::sqrt(b * b + 2 * k * q);
		const double s = 2 * q / (b + root);
		y = w + s;
		dy = 1 - (b + 0.5 * m_bufKw * s * s) / root;
	}
	if (i == 1 || i == 3) {
		// The current source saturates near pin 16.
		const double z = y - m_p.yHi24;
		dy *= 1 - sigmoid(z);
		y -= softplus(z);
	}
}

double Ir3109Vcf::otaGainLn(double lnI) const
{
	// ln(1 uA) = -13.8155.
	return m_p.otaGain1uA * (1 - m_p.otaGainPerE * (lnI + 13.815510557964274));
}

double Ir3109Vcf::otaGain(double iAbc) const { return otaGainLn(std::log(std::max(iAbc, 1e-18))); }

double Ir3109Vcf::poleHz(double iAbc) const
{
	const double rp = 1 / (2 / kR68 + 1 / kR560);
	const double g = std::sqrt(std::sqrt(m_buf[0] * m_buf[1] * m_buf[2] * m_buf[3]));
	return g * otaGain(iAbc) * iAbc * rp / (2 * 3.14159265358979323846 * 2 * m_p.vt * kR68 * (m_p.c + m_p.capPinF));
}

void Ir3109Vcf::otaOut(double t, double x, double &g, double &gt, double &gx) const
{
	// With p = 2 I_C1 / I_ABC = 1 + t and q = 2 I_C2 / I_ABC = 1 - t, the sink's saturation
	// tanh((x - x0Lo - V_T ln p) / 2V_T) is (A - p) / (A + p) with A = e^((x - x0Lo) / V_T), and the
	// source's the same with B = e^((x0Hi - x) / V_T) and q. More than 20 V_T (0.5 V) inside, A or B
	// is past 4.8e8 and the factor within 2e-8 of 1: skipped.
	const double vt = m_p.vt, p = 1 + t, q = 1 - t, band = 20 * vt;
	double lo = p, loP = 1, loX = 0, hi = q, hiQ = 1, hiX = 0;
	const double dLo = x - m_x0Lo, dHi = m_x0Hi - x;
	if (dLo < band) {
		const double a = std::exp(std::max(dLo / vt, -700.0)), s = a + p, s2 = s * s;
		lo = p * (a - p) / s;
		loP = (a * a - 2 * p * a - p * p) / s2;
		loX = 2 * p * p * a / (vt * s2);
	}
	if (dHi < band) {
		const double b = std::exp(std::max(dHi / vt, -700.0)), s = b + q, s2 = s * s;
		hi = q * (b - q) / s;
		hiQ = (b * b - 2 * q * b - q * q) / s2;
		hiX = -2 * q * q * b / (vt * s2);
	}
	// The change from -t, exactly 0 inside, and its derivatives.
	g = 0.5 * ((hi - q) - (lo - p));
	gt = 1 - 0.5 * (hiQ + loP);
	gx = 0.5 * (hiX - loX);
}

void Ir3109Vcf::reset()
{
	for (int i = 0; i < 6; ++i)
		m_x[i] = m_xPrev[i] = m_xPrev2[i] = i < 4 ? m_cap0[i] : 0.0;
	for (int i = 0; i < 4; ++i) {
		m_y[i] = 0;
		m_f[i] = 0;
	}
	m_v45 = m_v52 = m_v53 = m_v3 = 0;
	m_mixInit = false;
	m_settleLen = std::max(1, int(kSettleSeconds * m_fs));
	m_settle = m_settleLen;
}

void Ir3109Vcf::solve(double iAbc, double a, double vm, double gmix)
{
	const Params &p = m_p;
	const double h2 = m_t / 2;
	const double g1 = gmix + 1 / kR113 + 1 / kR68 + 1 / kR560; // stage 1's input node
	const double g2 = kG2;                                     // stages 2-4
	const double rg1 = 1 / g1;
	const double twoVt = 2 * p.vt, rTwoVt = 1 / twoVt;
	const double nvt = p.diodeN * p.vt, rNvt = 1 / nvt, rVt = 1 / p.vt;
	const double vPrev[4] = {m_x[0], m_x[1], m_x[2], m_x[3]};
	const double iMix = gmix * (vm - m_v45);
	// Prewarp: each stage's linear pole (with its buffer's gain) lands at its analog frequency.
	// k[i] is the integrator's full gain: OTA, capacitor, buffer and prewarp. Stages 1 and 3 have
	// the same buffer, so the same prewarp.
	if (iAbc != m_kIabc) {
		m_kIabc = iAbc;
		const double rp = 1 / g2, cap = p.c + p.capPinF;
		const double lnI = std::log(std::max(iAbc, 1e-18));
		const double gain = otaGainLn(lnI);
		double pw[4];
		for (int i = 0; i < 4; ++i) {
			const double kIdeal = p.gm[i] * iAbc / cap;
			if (i == 2 && m_buf[2] == m_buf[0]) {
				pw[2] = pw[0];
			} else {
				const double wT2 = m_buf[i] * gain * iAbc * rp / (twoVt * kR68 * cap) * h2;
				pw[i] = wT2 > 1e-6 ? std::tan(std::min(wT2, 1.4)) / std::min(wT2, 1.4) : 1.0;
			}
			m_kStage[i] = kIdeal * gain * pw[i];
			// The output resistance: g_m / (g_m r_o) from CAP towards its rest, on the same integrator
			// (g_m r_o measured against the ideal g_m, I_ABC / 2V_T).
			m_kLeak[i] = kIdeal * pw[i] / (twoVt * p.otaGmRo);
		}
		const double vbeHalf = p.vt * (lnI + m_lnHalfIs);
		m_x0Lo = -p.pin8BelowBus + vbeHalf;
		m_x0Hi = p.vccAboveBus - vbeHalf;
	}
	const double *kStage = m_kStage, *kLeak = m_kLeak;
	const double jNode = m_v53 + m_v3;

	// Start from a quadratic prediction off the last three samples (a linear one takes about half an
	// iteration more per sample once the filter rings above a few hundred hertz). The diode node is
	// bounded where its current would exceed what R129 and R113 can feed (m_dMax), where Newton's
	// steps are sane.
	const double dMax = m_dMax;
	double x[6];
	for (int i = 0; i < 6; ++i)
		x[i] = 3 * (m_x[i] - m_xPrev[i]) + m_xPrev2[i];
	// A capacitor can't settle more than a few V_T past where its mirrors saturate (otaOut), nor TR27's
	// emitter beyond the rails, so the prediction starts no further out. Near the top of the range
	// in self-oscillation the capacitors swing far within three samples, and an extrapolation deep
	// past a knee starts Newton where the saturation is flat.
	for (int i = 0; i < 4; ++i)
		x[i] = std::clamp(x[i], std::max(m_x0Lo, p.capLo), std::min(m_x0Hi, p.capHi));
	x[4] = std::clamp(x[4], -dMax, dMax);
	x[5] = std::clamp(x[5], m_eLo, m_eHi);
	// The last Newton evaluation, kept for the slopes at the accepted point. Each slope is split:
	// f, the input pair's -I_ABC tanh and the output resistance, integrated trapezoidally (kf its
	// derivative in u); and ff, what the mirrors' saturation near the rails changes of it (otaOut),
	// zero more than 0.5 V inside them. That part is stiff (a time constant of tens of ns against
	// the sample's microseconds), on which the trapezoidal rule rings from sample to sample, so it is
	// integrated by backward Euler: it holds the capacitor where the junctions balance. hku and hkx
	// are the step's derivatives in u and in the capacitor.
	double y[4], py[4], u[4], f[4], kf[4], ff[4], hku[4], hkx[4], tq[4], dx[6];
	const double h = m_t, band = 20 * p.vt; // otaOut's: exactly 0 further inside
	bool converged = false;
	int it = 0;
	for (; it < 12; ++it) {
		for (int i = 0; i < 4; ++i)
			buffer(i, x[i], y[i], py[i]);
		const double d = x[4], e = x[5];
		u[0] = (iMix + d * kG113 + y[0] * kG68) * rg1;
		for (int i = 1; i < 4; ++i)
			u[i] = (y[i - 1] + y[i]) * kDudPrev;
		for (int i = 0; i < 4; ++i) {
			const double th = otaTanh(u[i] * rTwoVt);
			tq[i] = th;
			const double k = kStage[i];
			const double dth = (1 - th * th) * rTwoVt;
			f[i] = -k * th - kLeak[i] * (x[i] - m_cap0[i]);
			kf[i] = -k * dth;
			if (x[i] - m_x0Lo < band || m_x0Hi - x[i] < band) {
				double g, gt, gx;
				otaOut(th, x[i], g, gt, gx);
				ff[i] = k * g;
				hku[i] = h2 * kf[i] + h * k * gt * dth;
				hkx[i] = h * k * gx;
			} else {
				ff[i] = 0;
				hku[i] = h2 * kf[i];
				hkx[i] = 0;
			}
		}
		// TR26's collector (AC): -g x the wiper, held above saturation.
		const double clin = -m_g26 * a * y[3] + m_kJ * jNode;
		const double cmin = m_cSat + m_follow26 * a * y[3];
		const double c = cmin + softplus(clin - cmin);
		const double dcdy4 = m_follow26 * a + sigmoid(clin - cmin) * (-m_g26 * a - m_follow26 * a);
		// TR27's emitter current (exponential in its V_BE, limited for Newton).
		double z = (c - e) * rVt;
		const double ezc = std::exp(std::min(z, 40.0));
		const double ez = ezc * (z > 40 ? 1 + (z - 40) : 1);
		const double ie = m_ie27 * ez;
		const double die = m_ie27 * ezc * rVt;
		// Diode pair.
		const double ed = std::exp(std::clamp(d * rNvt, -40.0, 40.0)), edi = 1 / ed;
		const double sd = 0.5 * (ed - edi), cd = 0.5 * (ed + edi);
		const double id = 2 * p.diodeIs * sd, did = 2 * p.diodeIs * cd * rNvt;
		const double i52 = (e - m_v52 - d) * kG129;

		double b[6];
		for (int i = 0; i < 4; ++i)
			b[i] = -(x[i] - vPrev[i] - h2 * (f[i] + m_f[i]) - h * ff[i]);
		b[4] = -(i52 - id - (d - u[0]) * kG113);
		b[5] = -(ie - m_ie27 - e * kG137 - i52);

		// The Jacobian is a chain: each capacitor row on its own and the one before (row 0 on the
		// diode node), the diode node's row on C1 and TR27's emitter, the emitter's on C4. Its
		// diagonal on the capacitors is at least 1, so they eliminate without pivoting down to a
		// 2x2 system in the diode node and the emitter.
		const double du1dv1 = py[0] * (kG68 * rg1), du1dd = kG113 * rg1;
		const double dudprev = kDudPrev;
		const double j00 = 1 - hku[0] * du1dv1 + h2 * kLeak[0] - hkx[0], j04 = -hku[0] * du1dd;
		double alpha[4], beta[4];
		alpha[0] = b[0] / j00;
		beta[0] = -j04 / j00;
		for (int i = 1; i < 4; ++i) {
			const double jl = -hku[i] * dudprev * py[i - 1], jd = 1 - hku[i] * dudprev * py[i] + h2 * kLeak[i] - hkx[i];
			alpha[i] = (b[i] - jl * alpha[i - 1]) / jd;
			beta[i] = -jl * beta[i - 1] / jd;
		}
		const double j40 = du1dv1 * kG113, j44 = -kG129 - did - (1 - du1dd) * kG113, j45 = kG129;
		const double j53 = die * dcdy4 * py[3], j54 = kG129, j55 = -die - kG137 - kG129;
		const double ma = j44 + j40 * beta[0], mb = j45, mp = b[4] - j40 * alpha[0];
		const double mc = j54 + j53 * beta[3], md = j55, mq = b[5] - j53 * alpha[3];
		const double det = ma * md - mb * mc;
		if (!(std::abs(det) > 1e-300) || !std::isfinite(det))
			break;
		dx[4] = (mp * md - mb * mq) / det;
		dx[5] = (ma * mq - mc * mp) / det;
		for (int i = 0; i < 4; ++i)
			dx[i] = alpha[i] + beta[i] * dx[4];
		// The diode node: step in current, not voltage, once the pair conducts (SPICE's junction
		// limiting): otherwise a start past the knee creeps back a few tens of mV per iteration.
		if (std::abs(d) > 3 * nvt) {
			const double idNew = id + did * dx[4];
			if (idNew * id > 0)
				dx[4] = nvt * std::asinh(idNew / (2 * p.diodeIs)) - d;
		}
		// TR27's junction the same way (it always conducts): its V_BE (c - e, c following C4) steps to
		// where its linearised current says. Otherwise an iterate that overdrives it creeps back less
		// than a V_T per iteration, which runs out of iterations with the cutoff near its top in
		// self-oscillation. Under a quarter of a V_T the two differ by under 3 % of the step.
		if (z < 40) {
			const double dc = dcdy4 * py[3] * dx[3], dvbe = dc - dx[5];
			if (std::abs(dvbe) > 0.25 * p.vt) {
				const double ieNew = ie + die * dvbe;
				if (ieNew > 0)
					dx[5] = dc - p.vt * std::log(ieNew / ie);
			}
		}
		bool limited = std::abs(dx[4]) > 0.3 || std::abs(x[4] + dx[4]) > dMax || std::abs(dx[5]) > 0.5;
		dx[4] = std::clamp(dx[4], -0.3, 0.3);
		dx[4] = std::clamp(x[4] + dx[4], -dMax, dMax) - x[4];
		dx[5] = std::clamp(dx[5], -0.5, 0.5);
		double big = 0;
		for (int i = 0; i < 6; ++i) {
			if (i < 4) {
				const double dxi = dx[i];
				dx[i] = std::clamp(dx[i], -1.0, 1.0);
				// The mirrors' saturation is a step a few V_T wide in the capacitor's own equation:
				// a long Newton step across it lands on the other flat side and comes back. One that
				// would cross a knee (where the junctions balance) stops on it, as SPICE limits a
				// junction's steps; from there Newton converges.
				// A knee sits at most V_T ln 2 inside m_x0Lo / m_x0Hi (p, q <= 2).
				const double xn = x[i] + dx[i], kn = p.vt * 0.6931471805599453;
				if (std::abs(dx[i]) > 2 * p.vt &&
				    (std::min(x[i], xn) < m_x0Lo + kn || std::max(x[i], xn) > m_x0Hi - kn)) {
					const double lo = m_x0Lo + p.vt * std::log(std::max(1 + tq[i], 1e-30));
					const double hi = m_x0Hi - p.vt * std::log(std::max(1 - tq[i], 1e-30));
					if ((x[i] - lo) * (xn - lo) < 0)
						dx[i] = lo - x[i];
					else if ((x[i] - hi) * (xn - hi) < 0)
						dx[i] = hi - x[i];
				}
				limited = limited || dx[i] != dxi;
			}
			x[i] += dx[i];
			big = std::max(big, std::abs(dx[i]));
		}
		// Converged once a step falls under 0.1 uV, or under 0.1 mV if nothing limited it: Newton
		// converges quadratically here, and the step after one under 0.1 mV measures 1e-9 to
		// 1e-13 V, well inside the 0.1 uV, so the iteration that would only confirm it is skipped.
		if (big < 1e-7 || (big < kAcceptStep && !limited)) {
			converged = true;
			break;
		}
	}
	m_iters = it + 1;

	// Accept, with the rails as a hard limit on the capacitors (a guard: the mirrors' saturation
	// keeps them inside).
	bool held = false;
	for (int i = 0; i < 6; ++i) {
		m_xPrev2[i] = m_xPrev[i];
		m_xPrev[i] = m_x[i];
		m_x[i] = x[i];
	}
	for (int i = 0; i < 4; ++i) {
		m_x[i] = std::clamp(m_x[i], p.capLo, p.capHi);
		held = held || m_x[i] <= p.capLo || m_x[i] >= p.capHi;
	}
	m_x[5] = std::clamp(m_x[5], m_eLo, m_eHi);
	// Slopes at the accepted point, for the next trapezoidal step. Converged, the accepted point
	// is the last evaluation plus a step under 0.1 uV: carried to first order from there (the
	// error is the step squared). Otherwise evaluated afresh.
	double u1;
	if (converged && !held) {
		double ya[4];
		for (int i = 0; i < 4; ++i)
			ya[i] = y[i] + py[i] * dx[i];
		double ua[4];
		ua[0] = (iMix + m_x[4] * kG113 + ya[0] * kG68) * rg1;
		for (int i = 1; i < 4; ++i)
			ua[i] = (ya[i - 1] + ya[i]) * kDudPrev;
		for (int i = 0; i < 4; ++i) {
			m_f[i] = f[i] + kf[i] * (ua[i] - u[i]) - kLeak[i] * dx[i];
			m_y[i] = ya[i];
		}
		u1 = ua[0];
	} else {
		double dy;
		for (int i = 0; i < 4; ++i)
			buffer(i, m_x[i], y[i], dy);
		u1 = (iMix + m_x[4] * kG113 + y[0] * kG68) * rg1;
		const double ua[4] = {u1, (y[0] + y[1]) * kDudPrev, (y[1] + y[2]) * kDudPrev, (y[2] + y[3]) * kDudPrev};
		for (int i = 0; i < 4; ++i) {
			double fi = -kStage[i] * otaTanh(ua[i] * rTwoVt) - kLeak[i] * (m_x[i] - m_cap0[i]);
			// A capacitor held at its limit doesn't keep the slope that pushes it there.
			if ((m_x[i] <= p.capLo && fi < 0) || (m_x[i] >= p.capHi && fi > 0))
				fi = 0;
			m_f[i] = fi;
			m_y[i] = y[i];
		}
	}
	// The coupling capacitors (time constants of a second and more): explicit.
	m_v45 += m_t * couplingRate() / kC45 * gmix * (vm - m_v45 - u1);
	// C52 sits inside the resonance loop and starts at its rest charge: no speed-up, which would
	// put a 300 Hz high-pass in the loop for the first 0.2 s.
	m_v52 += m_t / kC52 * (m_x[5] - m_v52 - m_x[4]) / kR129;
}

double Ir3109Vcf::tick(double iAbc, double res, double mixVolts, double mixOhms)
{
	const double gmix = mixOhms > 0 ? 1 / mixOhms : 0;
	if (!m_mixInit) {
		// Start with C45 charged to the mixer's level: no thump at power-on.
		m_v45 = mixVolts;
		m_mixInit = true;
	}
	// VR18 (100k B) from OUT4 to the bus, its wiper into TR26's base (about 3 Mohm).
	const double a = std::clamp(res, 0.0, 1.0);
	const double rth = 100e3 * a * (1 - a);
	const double rin26 = (m_p.beta + 1) * kR131;
	const double aEff = a * rin26 / (rin26 + rth);
	solve(std::max(iAbc, 0.0), aEff, mixVolts, gmix);
	for (double x : m_x)
		if (!std::isfinite(x)) {
			reset();
			return 0;
		}
	// R130 (OUT4) and R132 (TR26's emitter, which follows the wiper) into C53 and R139 1k.
	const double y4 = m_y[3];
	const double i53 = (y4 + m_follow26 * aEff * y4 - 2 * m_v53) / (2 * kR139 + kR130);
	m_v53 += m_t * couplingRate() / kC53 * i53;
	m_v3 = kR139 * i53;
	if (m_settle > 0)
		--m_settle;
	return m_v3;
}

} // namespace sh101
