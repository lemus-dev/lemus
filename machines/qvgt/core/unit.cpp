#include "unit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace qvgt {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kInputSilence = 1e-10f; // -200 dBFS
constexpr float kOutputSilence = 1e-6f; // -120 dB
constexpr double kSettleSeconds = 1.6;  // longer than the longest delay, 65536 / 41,666.67 Hz

int preampOver()
{
	const char *e = std::getenv("QVGT_PREAMP_OVER"); // tools
	return e ? std::max(2, std::atoi(e)) : kPreampOver;
}

// The A/D's input: U16's pre-emphasis, then R143 5.6K into the 20 kHz module, terminated by R138
// 8.2K with R137 51K (to C64) and R136 510K (to -12 V) across it: the module taken as passing its
// band as a doubly terminated ladder does, at the terminations' ratio.
constexpr double kAdcLoad = 1 / (1 / 8.2e3 + 1 / 51e3 + 1 / 510e3);
constexpr double kAdcDivider = kAdcLoad / (5.6e3 + kAdcLoad);
// The PCM54's full scale, both ways (the A/D is its successive approximation, with U21).
constexpr double kDacVolts = 3.0;
// The sample-and-holds: R154 750 and U19's on resistance charging C59/C60 2200p while U22 opens
// them. The window isn't on the sheet; 32 of the DSP's clocks (1.33 us) is the one at which the
// held step is the pre-emphasis's inverse (within -0.58 and +0.03 dB to 18 kHz). The QuadraVerb's
// service manual: R42 (this R154) sets the de-emphasis.
constexpr double kShTau = (750.0 + 125.0) * 2200e-12;
constexpr double kShWindow = 32 / 24e6;
// The op-amps' swing (TL084 on +-12 V).
constexpr double kSwing = 10.5;
// The OUTPUT pot's audio taper: the share of its track at mid rotation.
constexpr double kOutputTaperMid = 0.15;

// The CEM3381's current gain for a control voltage (its data sheet: exponential from -100 dB at
// 0 V to about -28 dB at 0.2 V, then linear, 20 %/V, to unity at 5 V).
double cemGain(double v)
{
	if (v >= 0.2)
		return std::min(v, 5.5) / 5.0;
	return 1e-5 * std::pow(0.04 / 1e-5, std::max(v, 0.0) / 0.2);
}

double potShare(double position, double mid)
{
	const double t = std::clamp(position, 0.0, 1.0);
	return t < 0.5 ? 2 * t * mid : mid + (t - 0.5) * 2 * (1 - mid);
}

} // namespace

Unit::Unit(std::vector<uint8_t> descrambledRom)
	: m_board(std::make_unique<Board>(std::move(descrambledRom))),
	  m_over(preampOver()), m_preampRate(kSampleRate * m_over),
	  // -6 dB at the DSP's Nyquist frequency; 60 taps a DSP sample (241 at 4x), Kaiser beta 9: 90 dB
	  // down from 22.9 kHz
	  m_decAdc(m_over, 0.5 / m_over, 60 * m_over + 1, 9.0), m_decDryL(m_over, 0.5 / m_over, 60 * m_over + 1, 9.0),
	  m_decDryR(m_over, 0.5 / m_over, 60 * m_over + 1, 9.0)
{
	prepare(48000);
}

void Unit::runSilent(double seconds)
{
	const int samples = int(seconds * kSampleRate);
	for (int s = 0; s < samples; ++s) {
		double l, r;
		m_board->runSample(0, 0, l, r);
	}
}

void Unit::powerOn(double seconds)
{
	m_board->powerOn();
	runSilent(seconds);
	wake();
}

void Unit::hold(int key, double seconds)
{
	m_board->setKey(key, true);
	runSilent(0.1);
	m_board->setKey(key, false);
	runSilent(seconds);
}

void Unit::recallFactoryPrograms()
{
	hold(kKeyStore, 0.3);
	for (int i = 0; i < 3; ++i)
		hold(kKeyPageUp, 0.3);
	hold(kKeyStore, 2.5);
	wake();
}

void Unit::prepare(double hostRate)
{
	m_hostRate = hostRate;
	m_inL = std::make_unique<Resampler>(hostRate, m_preampRate, 0.9);
	m_inR = std::make_unique<Resampler>(hostRate, m_preampRate, 0.9);
	m_upL = std::make_unique<Resampler>(kSampleRate, hostRate, 0.9);
	m_upR = std::make_unique<Resampler>(kSampleRate, hostRate, 0.9);
	m_decAdc.reset();
	m_decDryL.reset();
	m_decDryR.reset();

	auto bilinear = [](FirstOrder &f, double tn, double td, double k) { // (1 + s tn) / (1 + s td)
		const double d = 1 + td * k;
		f.b0 = (1 + tn * k) / d;
		f.b1 = (1 - tn * k) / d;
		f.a1 = (1 - td * k) / d;
		f.reset();
	};
	// Pre-emphasis (1 + s (R142 + R144) C41) / (1 + s R144 C41), prewarped at 10 kHz.
	bilinear(m_preEmph, 330e-12 * 95e3, 330e-12 * 20e3, 2 * kPi * 10e3 / std::tan(kPi * 10e3 / m_preampRate));
	m_shAlpha = 1 - std::exp(-kShWindow / kShTau);
	m_shL = m_shR = 0;
	m_zohL = Zoh();
	m_zohR = Zoh();
	// U16's Sallen-Key low-pass 1 / (1 + s 2R C49 + s^2 R^2 C51 C49), prewarped at 15 kHz
	{
		const double r = 1.1e3, c1 = 0.01e-6, c2 = 3300e-12;
		const double k = 2 * kPi * 15e3 / std::tan(kPi * 15e3 / kSampleRate);
		const double a1s = 2 * r * c2, a2s = r * r * c1 * c2;
		const double d = 1 + a1s * k + a2s * k * k;
		for (auto *b : {&m_reconL, &m_reconR}) {
			b->b0 = 1 / d;
			b->b1 = 2 / d;
			b->b2 = 1 / d;
			b->a1 = (2 - 2 * a2s * k * k) / d;
			b->a2 = (1 - a1s * k + a2s * k * k) / d;
			b->reset();
		}
	}
	m_dryInL.setup(10e3, 4.7e-6, kSampleRate);
	m_dryInR.setup(10e3, 4.7e-6, kSampleRate);
	m_dryCode = 0xff;
	updateDryCv();
	m_cv = m_cvTarget;
	// The output: C45 10u into the 10K pot (a high-pass at 1.6 Hz), then the non-inverting stage
	// 1 + R125 / (R126 + 1 / s C53)
	const double kOut = 2 * kSampleRate;
	bilinear(m_couplingL, 0, 10e3 * 10e-6, kOut);
	m_couplingL.b0 = 10e3 * 10e-6 * kOut / (1 + 10e3 * 10e-6 * kOut);
	m_couplingL.b1 = -m_couplingL.b0;
	m_couplingR = m_couplingL;
	bilinear(m_bufferL, (4.7e3 + 2e3) * 4.7e-6, 2e3 * 4.7e-6, kOut);
	m_bufferR = m_bufferL;

	m_preamp.setControls(m_board->latch(Board::kLatchU10), m_board->latch(Board::kLatchU11), m_board->latch(Board::kLatch0400), inputPot);
	m_preamp.setHiss(m_hiss);
	m_board->setAdcServo(m_hiss);
	m_preamp.prepare(m_preampRate);
	m_dspSigma = std::sqrt(kSampleRate / 2);
	m_noise.reseed(0x5bd1e995u);
	m_hissTail.prepare(hostRate);
	m_envelope = 0;
	m_headroom = m_ovfHold = 0;

	// Each resampler interpolates about its kernel's centre: an output stands for the input time it
	// is at but exists only once the kernel has filled. The FIFO is primed with the chain's lag and a
	// margin, for a fixed latency.
	m_latency = int(std::ceil((m_inL->latencyInput() + 1) + (m_decAdc.delay() + m_over) * hostRate / m_preampRate +
	                          (m_upL->latencyInput() + 1) * hostRate / kSampleRate)) + 4;
	m_fifoL.assign(size_t(m_latency), 0.0f);
	m_fifoR.assign(size_t(m_latency), 0.0f);
	m_bufL.clear();
	m_bufR.clear();
	m_wetL.clear();
	m_wetR.clear();
	m_heldDebt = 0;
	m_settleSamples = int(std::ceil(kSettleSeconds * hostRate));
}

// VCA C.V. (sheet 2): latch 0600h b1-b4 through R190 1.5K, R191 3K, R192 6.2K, R193 12K to a node
// that R187 10K joins to the C.V.; b0 through R189 20K, and through D18 and R188 4.7K. The CEM3381's
// control input takes about 1.5 uA: none. Solved for the node's voltage and its source resistance
// (C100's time constant).
void Unit::setHiss(bool on)
{
	if (on == m_hiss)
		return;
	m_hiss = on;
	m_preamp.setHiss(on);
	m_board->setAdcServo(on);
	wake();
}

void Unit::HissTail::prepare(double rate)
{
	const size_t n = size_t(rate * 0.25);
	ring[0].assign(n, 0.0f);
	ring[1].assign(n, 0.0f);
	pos = 0;
	blockSum = 0;
	blockCount = 0;
	blockLen = int(rate * 0.1);
	levels.clear();
	for (auto &yy : y)
		for (auto &v : yy)
			v = 0;
}

void Unit::HissTail::push(float l, float r)
{
	ring[0][pos] = l;
	ring[1][pos] = r;
	pos = pos + 1 == ring[0].size() ? 0 : pos + 1;
	blockSum += 0.5 * (double(l) * l + double(r) * r);
	if (++blockCount == blockLen) {
		levels.push_back(10 * std::log10(blockSum / blockLen + 1e-30));
		if (levels.size() > 16)
			levels.erase(levels.begin());
		blockSum = 0;
		blockCount = 0;
	}
}

bool Unit::HissTail::steady() const
{
	if (levels.size() < 16)
		return false;
	const auto [lo, hi] = std::minmax_element(levels.begin(), levels.end());
	return *hi - *lo < 1.0 && *hi < -40.0;
}

// An all-pole fit (autocorrelation, Levinson-Durbin) of each channel's last 0.25 s: white noise
// through it has the same spectrum and level.
void Unit::HissTail::fit()
{
	const size_t n = ring[0].size();
	for (int ch = 0; ch < 2; ++ch) {
		double r[kOrder + 1]{};
		for (int k = 0; k <= kOrder; ++k)
			for (size_t i = size_t(k); i < n; ++i)
				r[k] += double(ring[ch][(pos + i) % n]) * ring[ch][(pos + i - size_t(k)) % n];
		for (auto &v : a[ch])
			v = 0;
		a[ch][0] = 1;
		double e = r[0] * (1 + 1e-9) + 1e-30;
		for (int i = 1; i <= kOrder; ++i) {
			double acc = r[i];
			for (int j = 1; j < i; ++j)
				acc += a[ch][j] * r[i - j];
			const double k = -acc / e;
			double tmp[kOrder + 1];
			for (int j = 0; j <= kOrder; ++j)
				tmp[j] = a[ch][j];
			for (int j = 1; j < i; ++j)
				a[ch][j] = tmp[j] + k * tmp[i - j];
			a[ch][i] = k;
			e *= 1 - k * k;
		}
		gain[ch] = std::sqrt(std::max(e, 0.0) / double(n));
		for (auto &v : y[ch])
			v = 0;
	}
}

void Unit::HissTail::render(Noise &noise, float &l, float &r)
{
	for (int ch = 0; ch < 2; ++ch) {
		double v = gain[ch] * noise.gauss();
		for (int k = 1; k <= kOrder; ++k)
			v -= a[ch][k] * y[ch][k - 1];
		for (int k = kOrder - 1; k > 0; --k)
			y[ch][k] = y[ch][k - 1];
		y[ch][0] = v;
		(ch == 0 ? l : r) = float(v);
	}
}

void Unit::updateDryCv()
{
	const uint8_t code = m_board->latch(Board::kLatch0600) & 0x1f;
	if (code == m_dryCode)
		return;
	m_dryCode = code;
	const double r[4] = {1.5e3, 3e3, 6.2e3, 12e3};
	double g = 0, i = 0;
	for (int b = 0; b < 4; ++b) {
		g += 1 / r[b];
		i += ((code >> (b + 1)) & 1) * 5.0 / r[b];
	}
	const double vDac = i / g, rDac = 1 / g + 10e3; // the ladder through R187
	const double d0 = (code & 1) * 5.0;
	// KCL at the C.V.: (vDac - v)/rDac + (d0 - v)/20K + D18's current through 4.7K = 0
	const double is = 2.52e-9, nvt = 1.752 * 0.02585, rs = 4.7e3;
	double v = (vDac / rDac + d0 / 20e3) / (1 / rDac + 1 / 20e3), gd = 0;
	for (int it = 0; it < 60; ++it) {
		// the diode's current for v: solve id = is (exp((d0 - v - id rs) / nvt) - 1) by Newton in id
		double id = 0;
		for (int k = 0; k < 60; ++k) {
			const double e = std::exp(std::min((d0 - v - id * rs) / nvt, 80.0));
			const double f = is * (e - 1) - id, df = -is * e * rs / nvt - 1;
			const double step = -f / df;
			id += step;
			if (std::abs(step) < 1e-15)
				break;
		}
		const double e = std::exp(std::min((d0 - v - id * rs) / nvt, 80.0));
		gd = (is * e / nvt) / (1 + is * e * rs / nvt); // d id / d(d0 - v)
		const double f = (vDac - v) / rDac + (d0 - v) / 20e3 + id;
		const double df = -1 / rDac - 1 / 20e3 - gd;
		const double step = -f / df;
		v += step;
		if (std::abs(step) < 1e-12)
			break;
	}
	m_cvTarget = v;
	const double rth = 1 / (1 / rDac + 1 / 20e3 + gd);
	m_cvCoef = 1 - std::exp(-1 / (kSampleRate * rth * 0.1e-6));
}

void Unit::dspSample(double adc, double envelope, double dryL, double dryR, double &outL, double &outR)
{
	double l = 0, r = 0;
	m_board->runSample(adc, envelope, l, r);
	if (m_board->dsp().overflow) {
		m_board->dsp().overflow = false;
		if (m_ovfHold == 0)
			m_ovfHold = int(kSampleRate * 2.2e6 * 0.1e-6 * std::log(2.0)); // U33 preset once C87 is half down
	} else if (m_ovfHold > 0)
		--m_ovfHold;
	// U17's ladder from ground: R146 91K, R148 180K, R149 390K, R150 750K to U33's Q (5 V; 0 V
	// while it holds an overflow)
	if (m_ovfHold > 0)
		m_headroom = envelope > 0 ? 4 : 0;
	else
		m_headroom = (envelope > 5.0 * 91 / 1411) + (envelope > 5.0 * 271 / 1411) + (envelope > 5.0 * 661 / 1411) + (envelope > 5.0);

	// the D/A, its sample-and-holds, U18, U16's low-passes
	m_shL += m_shAlpha * (l * kDacVolts - m_shL);
	m_shR += m_shAlpha * (r * kDacVolts - m_shR);
	double wetL = m_reconL(m_zohL(m_shL)), wetR = m_reconR(m_zohR(m_shR));
	// HISS after the D/A: U18's and U16's input noise in the wet path (TL084, 18 nV/sqrt(Hz) each)
	const double en = 18e-9 * m_dspSigma;
	if (m_hiss) {
		wetL += en * std::sqrt(2.0) * m_noise.gauss();
		wetR += en * std::sqrt(2.0) * m_noise.gauss();
	}

	// the dry path: the CEM3381's current gain into U15's summers (R127 20K; the wet through R128,
	// R131 20K), inverting
	updateDryCv();
	m_cv += m_cvCoef * (m_cvTarget - m_cv);
	const double g = cemGain(m_cv);
	double sumL = -(20e3 * g * m_dryInL(dryL) + wetL), sumR = -(20e3 * g * m_dryInR(dryR) + wetR);
	if (m_hiss) {
		// U15's summers' input noise at a noise gain of 2, and the CEM3381's output noise (its data
		// sheet: 100 dB under 400 uA p-p in 20 kHz, 10 pA/sqrt(Hz), taken as rising with the square
		// root of its gain) into R127 20K
		const double cem = 10e-12 * m_dspSigma * 20e3 * std::sqrt(g);
		sumL += 2 * en * m_noise.gauss() + cem * m_noise.gauss();
		sumR += 2 * en * m_noise.gauss() + cem * m_noise.gauss();
	}
	sumL = std::clamp(sumL, -kSwing, kSwing);
	sumR = std::clamp(sumR, -kSwing, kSwing);

	// the OUTPUT pot and the output stages (their input noise at the + input, through their gain)
	const double share = potShare(outputPot, kOutputTaperMid);
	const double nl = m_hiss ? en * m_noise.gauss() : 0.0, nr = m_hiss ? en * m_noise.gauss() : 0.0;
	outL = std::clamp(m_bufferL(m_couplingL(sumL) * share + nl), -kSwing, kSwing);
	outR = std::clamp(m_bufferR(m_couplingR(sumR) * share + nr), -kSwing, kSwing);
}

void Unit::process(const float *inL, const float *inR, float *outL, float *outR, int n)
{
	m_preamp.setControls(m_board->latch(Board::kLatchU10), m_board->latch(Board::kLatchU11), m_board->latch(Board::kLatch0400), inputPot);
	for (int i = 0; i < n; ++i) {
		const float l = inL[i], r = inR[i];

		if (std::abs(l) >= kInputSilence || std::abs(r) >= kInputSilence) {
			m_quiet = 0;
			m_asleep = false;
		} else if (m_quiet < m_settleSamples)
			++m_quiet;

		if (m_asleep) {
			// the 80C31 keeps its time; the DSP holds its memory; the output is silent, or the hiss
			for (m_heldDebt += kSampleRate / m_hostRate; m_heldDebt >= 1.0; m_heldDebt -= 1.0)
				m_board->runSampleDspHeld();
			m_headroom = 0;
			if (m_hiss)
				m_hissTail.render(m_noise, outL[i], outR[i]);
			else
				outL[i] = outR[i] = 0.0f;
			continue;
		}

		m_bufL.clear();
		m_bufR.clear();
		m_inL->push(float(l * kVoltsFS), m_bufL);
		m_inR->push(float(r * kVoltsFS), m_bufR);
		const size_t steps = std::min(m_bufL.size(), m_bufR.size());
		for (size_t k = 0; k < steps; ++k) {
			const auto o = m_preamp.step(m_bufL[k], m_bufR[k]);
			const double adcVolts = kAdcDivider * m_preEmph(o.toAdc);
			double adc = 0, dl = 0, dr = 0;
			m_decDryL.push(o.dryL, dl);
			m_decDryR.push(o.dryR, dr);
			if (!m_decAdc.push(adcVolts, adc))
				continue;
			m_envelope = o.envelope;
			double yl, yr;
			dspSample(std::clamp(adc / kDacVolts, -1.0, 1.0), m_envelope, dl, dr, yl, yr);
			m_wetL.clear();
			m_wetR.clear();
			m_upL->push(float(yl / kVoltsFS), m_wetL);
			m_upR->push(float(yr / kVoltsFS), m_wetR);
			m_fifoL.insert(m_fifoL.end(), m_wetL.begin(), m_wetL.end());
			m_fifoR.insert(m_fifoR.end(), m_wetR.begin(), m_wetR.end());
		}
		float yl = 0, yr = 0;
		if (!m_fifoL.empty()) {
			yl = m_fifoL.front();
			m_fifoL.pop_front();
		}
		if (!m_fifoR.empty()) {
			yr = m_fifoR.front();
			m_fifoR.pop_front();
		}
		outL[i] = yl;
		outR[i] = yr;

		// Asleep once the input has been silent and the output under -120 dB for longer than the
		// longest delay (65,536 words, 1.573 s): anything still circulating above that would have
		// come out by then. The memory itself never empties: the truncating arithmetic leaves small
		// DC limit cycles in it, as the unit would, far under the output's AC coupling.
		if (m_hiss) {
			// with the hiss, settled is a steady level once the input has been silent long enough
			m_hissTail.push(outL[i], outR[i]);
			if (m_idleSleep && m_quiet >= m_settleSamples && m_hissTail.steady()) {
				m_hissTail.fit();
				m_asleep = true;
				m_heldDebt = 0;
			}
		} else if (m_quiet >= m_settleSamples && std::abs(outL[i]) < kOutputSilence && std::abs(outR[i]) < kOutputSilence) {
			if (m_idleSleep && ++m_lowRun >= m_settleSamples) {
				m_asleep = true;
				m_heldDebt = 0;
			}
		} else
			m_lowRun = 0;
	}
}

} // namespace qvgt
