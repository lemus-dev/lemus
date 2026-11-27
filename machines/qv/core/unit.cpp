#include "unit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace qv {

namespace {
constexpr float kInputSilence = 1e-10f; // -200 dBFS
constexpr float kOutputSilence = 1e-6f; // -120 dB
constexpr double kSettleSeconds = 1.6;  // longer than the longest delay, 65536 / 41,666.67 Hz

// The A/D's input: U4's pre-emphasis, then R25 5.6K into the 20 kHz module, terminated by R26
// 8.2K with R47 51K (to C13) and R36 510K (to -12 V) across it: the module taken as passing its
// band as a doubly terminated ladder does, at the terminations' ratio.
constexpr double kAdcLoad = 1 / (1 / 8.2e3 + 1 / 51e3 + 1 / 510e3);
constexpr double kAdcDivider = kAdcLoad / (5.6e3 + kAdcLoad);
// The PCM54's full scale, both ways (the A/D is its successive approximation, with U21).
constexpr double kDacVolts = 3.0;
// The sample-and-holds: R42 750 and U9's on resistance charging C39/C40 2200p while the DASP's
// SNHOUT lines open them. The window isn't on the sheet; 32 of the DSP's clocks (1.33 us) is the one
// at which the held step is the pre-emphasis's inverse (within -0.58 and +0.03 dB to 18 kHz). The
// service manual (7.10): R42 sets the de-emphasis.
constexpr double kShTau = (750.0 + 125.0) * 2200e-12;
constexpr double kShWindow = 32 / 24e6;
// The op-amps' swing (TL084 on +-12 V).
constexpr double kSwing = 10.5;
// The OUTPUT pot's audio taper: the share of its track at mid rotation.
constexpr double kOutputTaperMid = 0.15;
// The level detector: R27 330K, C45 0.1u.
constexpr double kEnvTau = 330e3 * 0.1e-6;

// The CEM3381's current gain for a control voltage (its data sheet, the PA381's: "Control Scale
// 20 %/V", "Gain C.V. for -96dB Gain +0.1 V", "Max. Current Gain 1.0" at +5.0 V, typical): linear
// from 0.1 V, -96 dB under it; times kCemGain.
// kCemGain: the measured unit's (accuracy round 1, 2026-11-27: a line recording of a QuadraVerb with
// its dry source, six programs, 29 octave-band fits of its output as dry and wet paths, put the
// wet 1.6 dB higher against the dry than the data sheet's typical gain gives; median, quartiles 1.0
// and 2.1 dB). Placed here because the VCA's gain is the one part of either path the data sheet
// gives no limits for; the A/D's divider, the converters and the summers are fixed by resistors.
constexpr double kCemGain = 0.86;
double cemGain(double v)
{
	return kCemGain * std::max(0.2 * (std::min(v, 5.5) - 0.1), 1.58e-5);
}

double potShare(double position, double mid)
{
	const double t = std::clamp(position, 0.0, 1.0);
	return t < 0.5 ? 2 * t * mid : mid + (t - 0.5) * 2 * (1 - mid);
}

} // namespace

Unit::Unit(std::vector<uint8_t> descrambledRom)
	: m_board(std::make_unique<Board>(std::move(descrambledRom))),
	  // -6 dB at the DSP's Nyquist frequency; 60 taps a DSP sample (241 at 4x), Kaiser beta 9: 90 dB
	  // down from 22.9 kHz
	  m_decAdc(kOver, 0.5 / kOver, 60 * kOver + 1, 9.0)
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
	m_inL = std::make_unique<Resampler>(hostRate, m_inRate, 0.9);
	m_inR = std::make_unique<Resampler>(hostRate, m_inRate, 0.9);
	m_upL = std::make_unique<Resampler>(m_inRate, hostRate, 0.9);
	m_upR = std::make_unique<Resampler>(m_inRate, hostRate, 0.9);
	m_decAdc.reset();
	m_dryDelayL.assign(size_t(m_decAdc.delay()), 0.0);
	m_dryDelayR.assign(m_dryDelayL.size(), 0.0);
	m_dryPos = 0;

	// the stages' designs: core/analog.h
	analog::preEmphasis(m_preEmph, m_inRate);
	m_preEmph.reset();
	m_potSet = -1;
	updateInputPot();
	m_envDecay = std::exp(-1 / (kEnvTau * m_inRate));
	m_shAlpha = 1 - std::exp(-kShWindow / kShTau);
	m_shL = m_shR = 0;
	for (auto *b : {&m_reconL, &m_reconR}) {
		analog::reconstruction(*b, m_inRate);
		b->reset();
	}
	for (auto *f : {&m_summerL, &m_summerR}) {
		analog::summerPole(*f, m_inRate);
		f->reset();
	}
	m_dryInL.setup(analog::kDryR, analog::kDryC, m_inRate);
	m_dryInR.setup(analog::kDryR, analog::kDryC, m_inRate);
	m_dryCode = 0xff;
	m_dryStats = {};
	updateDryCv();
	m_cv = m_cvTarget;
	// The output: C4, C5 4.7u into the 10K pot (a high-pass at 3.4 Hz), then the non-inverting
	// stages (a gain of 2)
	analog::outputCoupling(m_couplingL, m_inRate);
	m_couplingL.reset();
	m_couplingR = m_couplingL;
	analog::outputStage(m_bufferL, m_inRate);
	m_bufferL.reset();
	m_bufferR = m_bufferL;

	m_board->setAdcServo(m_hiss);
	m_sigma = std::sqrt(m_inRate / 2);
	m_noise.reseed(0x5bd1e995u);
	m_hissTail.prepare(hostRate);
	m_envelope = 0;
	m_headroom = m_ovfHold = 0;

	// Each resampler interpolates about its kernel's centre: an output stands for the input time it
	// is at but exists only once the kernel has filled. The FIFO is primed with the chain's lag and a
	// margin, for a fixed latency.
	m_latency = int(std::ceil((m_inL->latencyInput() + 1) + (m_decAdc.delay() + kOver) * hostRate / m_inRate +
	                          (m_upL->latencyInput() + 1) * hostRate / m_inRate)) + 4;
	m_fifoL.assign(size_t(m_latency));
	m_fifoR.assign(size_t(m_latency));
	m_bufL.clear();
	m_bufR.clear();
	m_wetL.clear();
	m_wetR.clear();
	m_heldDebt = 0;
	m_settleSamples = int(std::ceil(kSettleSeconds * hostRate));
}

void Unit::setHiss(bool on)
{
	if (on == m_hiss)
		return;
	m_hiss = on;
	m_board->setAdcServo(on);
	wake();
}

// U1's inverting stages for the INPUT pot's rotation (core/analog.h), keeping their state: the
// capacitors' charge carries over a turn of the knob.
void Unit::updateInputPot()
{
	const double t = std::clamp(inputPot, 0.0, 1.0);
	if (t == m_potSet)
		return;
	m_potSet = t;
	for (auto *f : {&m_stageL, &m_stageR})
		analog::inputStage(*f, t, m_inRate);
	m_stageNoiseGain = analog::inputNoiseGain(t);
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

// VCA C.V. (sheet 2): latch 0600h b1-b4 through 1.5K, 3K, 6.2K and 12K to a node that R75 10K
// joins to the C.V.; b0 through R77 20K, and through D8 and 4.7K. The CEM3381's control input takes
// about 1.5 uA: none. Solved for the node's voltage and its source resistance (C52's time constant).
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
	bool converged = false, inner = true;
	for (int it = 0; it < 60; ++it) {
		// the diode's current for v: solve id = is (exp((d0 - v - id rs) / nvt) - 1) by Newton in id.
		// From id = 0 each iteration takes about one nVt off the exponent, which starts up to 72 deep
		// (d0 5 V, v at least 1.75 V): code 1 (D0 alone) needs 65 in the first outer iteration. It
		// used to stop at 60 there; the outer Newton reached the same v all the same.
		double id = 0;
		bool idConverged = false;
		for (int k = 0; k < 100; ++k) {
			const double e = std::exp(std::min((d0 - v - id * rs) / nvt, 80.0));
			const double f = is * (e - 1) - id, df = -is * e * rs / nvt - 1;
			const double step = -f / df;
			id += step;
			if (std::abs(step) < 1e-15) {
				idConverged = true;
				break;
			}
		}
		inner = inner && idConverged;
		const double e = std::exp(std::min((d0 - v - id * rs) / nvt, 80.0));
		gd = (is * e / nvt) / (1 + is * e * rs / nvt); // d id / d(d0 - v)
		const double f = (vDac - v) / rDac + (d0 - v) / 20e3 + id;
		const double df = -1 / rDac - 1 / 20e3 - gd;
		const double step = -f / df;
		v += step;
		if (std::abs(step) < 1e-12) {
			converged = true;
			break;
		}
	}
	// one solve a change of the code (all 32 converge, the worst in 5 outer and 65 inner iterations)
	++m_dryStats.solves;
	m_dryStats.failures += !(converged && inner);
	m_cvTarget = v;
	const double rth = 1 / (1 / rDac + 1 / 20e3 + gd);
	m_cvCoef = 1 - std::exp(-1 / (kSampleRate * rth * 0.1e-6)); // C52
}

void Unit::dspStep(double adc, double envelope)
{
	double l = 0, r = 0;
	m_board->runSample(adc, envelope, l, r);
	if (m_board->dsp().overflow) {
		m_board->dsp().overflow = false;
		if (m_ovfHold == 0)
			m_ovfHold = int(kSampleRate * 2.2e6 * 0.1e-6 * std::log(2.0)); // U24 preset once C73 is half down
	} else if (m_ovfHold > 0)
		--m_ovfHold;
	// U7's ladder from ground: R38 91K (the parts list; 92K on the drawing), R39 180K, R40 390K, R41
	// 750K to U24's Q (5 V; 0 V while it holds an overflow)
	if (m_ovfHold > 0)
		m_headroom = envelope > 0 ? 4 : 0;
	else
		m_headroom = (envelope > 5.0 * 91 / 1411) + (envelope > 5.0 * 271 / 1411) + (envelope > 5.0 * 661 / 1411) + (envelope > 5.0);

	// the D/A's sample-and-holds: charged through R42 for the window, then held (outputStep)
	m_shL += m_shAlpha * (l * kDacVolts - m_shL);
	m_shR += m_shAlpha * (r * kDacVolts - m_shR);
	updateDryCv();
	m_cv += m_cvCoef * (m_cvTarget - m_cv);
}

void Unit::outputStep(double dryL, double dryR, double &outL, double &outR)
{
	// U3's buffers and U4's low-passes on the held D/A
	double wetL = m_reconL(m_shL), wetR = m_reconR(m_shR);
	if (muteWet)
		wetL = wetR = 0;
	// HISS after the D/A: U3's and U4's input noise in the wet path (TL084, 18 nV/sqrt(Hz) each)
	const double en = 18e-9 * m_sigma;
	if (m_hiss) {
		wetL += en * std::sqrt(2.0) * m_noise.gauss();
		wetR += en * std::sqrt(2.0) * m_noise.gauss();
	}

	// the dry path: the CEM3381's current gain into U2's summers (R7, R8 20K with C26, C27 150p;
	// the wet through R34, R45 20K), inverting
	const double g = muteDry ? 0.0 : cemGain(m_cv);
	double sumL = -m_summerL(20e3 * g * m_dryInL(dryL) + wetL), sumR = -m_summerR(20e3 * g * m_dryInR(dryR) + wetR);
	if (m_hiss) {
		// U2's summers' input noise at a noise gain of 2, and the CEM3381's output noise (its data
		// sheet: 100 dB under 400 uA p-p in 20 kHz, 10 pA/sqrt(Hz), taken as rising with the square
		// root of its gain) into R7, R8 20K
		const double cem = 10e-12 * m_sigma * 20e3 * std::sqrt(g);
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
	updateInputPot();
	const double en = 18e-9 * std::sqrt(m_inRate / 2); // TL084's input noise per input-rate step
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
			// U1: the followers (their noise at the + inputs, then the inverting stages' at theirs,
			// both through the stage's noise gain), clipping at the rails
			double inL = m_bufL[k], inR = m_bufR[k];
			double nL = 0, nR = 0;
			if (m_hiss) {
				inL += en * m_noise.gauss();
				inR += en * m_noise.gauss();
				nL = en * m_stageNoiseGain * m_noise.gauss();
				nR = en * m_stageNoiseGain * m_noise.gauss();
			}
			const double dryL = std::clamp(m_stageL(inL) + nL, -kSwing, kSwing);
			const double dryR = std::clamp(m_stageR(inR) + nR, -kSwing, kSwing);
			// R17, R18 470R into U4's + inputs: the mono sum
			const double sum = 0.5 * (dryL + dryR);
			// U4 pins 5-7 and D2: the peak detector on C45, R27 discharging it
			m_envelope = std::max(sum, m_envelope * m_envDecay);
			// U4 pins 1-3: the pre-emphasis (its noise at the + input, through its gain)
			const double pre = std::clamp(m_preEmph(sum + (m_hiss ? en * m_noise.gauss() : 0.0)), -kSwing, kSwing);
			const double adcVolts = kAdcDivider * pre;
			double adc = 0;
			if (m_decAdc.push(adcVolts, adc))
				dspStep(std::clamp(adc / kDacVolts, -1.0, 1.0), m_envelope);
			// the dry signal, as late as the decimator makes the wet
			const double dl = m_dryDelayL[m_dryPos], dr = m_dryDelayR[m_dryPos];
			m_dryDelayL[m_dryPos] = dryL;
			m_dryDelayR[m_dryPos] = dryR;
			m_dryPos = m_dryPos + 1 == m_dryDelayL.size() ? 0 : m_dryPos + 1;
			double yl, yr;
			outputStep(dl, dr, yl, yr);
			m_wetL.clear();
			m_wetR.clear();
			m_upL->push(float(yl / kVoltsFS), m_wetL);
			m_upR->push(float(yr / kVoltsFS), m_wetR);
			m_fifoL.push(m_wetL);
			m_fifoR.push(m_wetR);
		}
		const float yl = m_fifoL.pop(), yr = m_fifoR.pop();
		// The output stages' swing again, at the host's rate: the resampler down from the analog
		// stages' rate can put peaks past the samples clamped there (the Gibbs overshoot of a clipped
		// edge band-limited), where the unit's output stages clip. Inside the rails it changes nothing.
		constexpr float kSwingFS = float(kSwing / kVoltsFS);
		outL[i] = std::clamp(yl, -kSwingFS, kSwingFS);
		outR[i] = std::clamp(yr, -kSwingFS, kSwingFS);

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

} // namespace qv
