// The synth board: the oscillator section into the filter section, oversampled.
#include "voice.h"

#include <vector>

namespace sh101 {

struct Voice::Impl {
	Controls c;
	Calibration cal;
	OscSection osc;
	FilterSection filter;
	JackBoard jack;
	Decimator dec;
	double hostRate = 44100;
	double env = 0;
	std::vector<float> work;
	Probe probe;
};

Voice::Voice() : m(std::make_unique<Impl>()) {}
Voice::~Voice() = default;

void Voice::prepare(double hostRate)
{
	m->hostRate = hostRate;
	// 4x below 50 kHz host rates, 2x up to 100 kHz, then none: the internal rate stays at
	// 176-192 kHz.
	m->dec.setStages(hostRate < 50e3 ? 2 : hostRate < 100e3 ? 1 : 0);
	m->work.assign(size_t(m->dec.factor()), 0.0f);
	m->osc.prepare(internalRate());
	m->filter.prepare(internalRate());
	m->jack.setLoad(m->cal.filter.loadOhms);
	m->jack.prepare(internalRate());
	reset();
}

void Voice::reset()
{
	m->osc.reset();
	m->filter.reset();
	m->dec.reset();
	m->env = 0;
}

void Voice::setControls(const Controls &c)
{
	m->c = c;
	m->osc.setControls(c);
	m->filter.setControls(c);
}

const Controls &Voice::controls() const { return m->c; }

void Voice::setCalibration(const Calibration &c)
{
	m->cal = c;
	m->osc.setCalibration(c.osc);
	m->filter.setCalibration(c.filter);
	m->jack.setLoad(c.filter.loadOhms);
}

const Calibration &Voice::calibration() const { return m->cal; }

void Voice::setPerformance(const Performance &p)
{
	m->osc.setPerformance(p);
	m->filter.setPerformance(p);
}

void Voice::setLines(const CpuLines &lines)
{
	m->osc.setLines(lines);
	m->filter.setLines(lines);
	m->probe.gate = lines.gate;
}

void Voice::render(float *out, int n, const ModFrame *mod)
{
	const int f = m->dec.factor();
	for (int i = 0; i < n; ++i) {
		for (int k = 0; k < f; ++k) {
			const double env = m->filter.tickEnv(mod[i]);
			const OscFrame o = m->osc.tick(mod[i], env);
			if (m->jack.poweringDown())
				m->filter.setSupply(m->jack.railVolts());
			m->work[size_t(k)] = float(m->jack.tick(m->filter.tick(o)) * kOutputScale);
		}
		out[i] = f > 1 ? m->dec.process(m->work.data()) : m->work[0];
	}
	m->probe.cvVolts = m->osc.pitchVolts();
	m->probe.vcoHz = m->osc.vcoHz();
	m->probe.vcfHz = m->filter.vcfHz();
	m->probe.iAbc = m->filter.iAbc();
	m->probe.env = m->filter.env();
	m->probe.vca = m->filter.vca();
}

void Voice::powerOn() { m->jack.powerOn(); }
void Voice::settlePower() { m->jack.settle(); }
void Voice::powerOff() { m->jack.powerOff(); }
bool Voice::poweringDown() const { return m->jack.poweringDown(); }
bool Voice::powering() const { return m->jack.powering(); }
bool Voice::muting() const { return m->jack.muting(); }

double Voice::internalRate() const { return m->hostRate * m->dec.factor(); }
double Voice::latencySamples() const { return m->dec.latency(); }
Voice::Probe Voice::probe() const { return m->probe; }

} // namespace sh101
