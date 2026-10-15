// Black-box probe: the original ROM in the Lemus board model, observed only at the pins,
// the panel and the external RAM. Exposes no CPU internals.
#include "tb303/core/machine.h"
#include <cmath>
#include <cstring>
#include <vector>

using tb303::Machine;

struct Ev { double t; int type; int a, b, c; }; // type 1 = PI1/PI2 change (a=PI1,b=PI2), 2 = strobe (a=dac,b=accent), 3 = INT
struct Probe {
	Machine m;
	double t = 0, irqAcc = 0;
	const double dt = 1.0 / Machine::kCycleHz;
	int clockMode = 0; // 0 manual, 1 auto
	double bpm = 120, duty = 0.5, t0 = 0;
	bool manualClock = false;
	bool logInt = false;
	unsigned lastStrobes = 0;
	bool pi1 = false, pi2 = false;
	std::vector<Ev> evs;
	double ledOn[16] = {}, ledN = 0;
};

extern "C" {
Probe *p_new(const uint8_t *rom) { auto *p = new Probe; p->m.setRom(rom, 2048); return p; }
void p_free(Probe *p) { delete p; }
void p_power(Probe *p) { p->m.powerOn(); p->irqAcc = 0; }
void p_key(Probe *p, int k, int down) { p->m.setKey(Machine::Key(k), down != 0); }
void p_mode(Probe *p, int m) { p->m.setMode(Machine::Mode(m)); }
void p_track(Probe *p, int t) { p->m.setTrack(t); }
void p_run(Probe *p, int r) { p->m.setRun(r != 0); }
void p_clock(Probe *p, int level) { p->clockMode = 0; p->manualClock = level != 0; }
// auto clock: first rising edge at now + offset seconds
void p_clock_auto(Probe *p, double bpm, double duty, double offset) { p->clockMode = 1; p->bpm = bpm; p->duty = duty; p->t0 = p->t + offset; }
void p_log_int(Probe *p, int on) { p->logInt = on != 0; }
double p_time(Probe *p) { return p->t; }
double p_irq_phase(Probe *p) { return p->irqAcc; }
void p_advance(Probe *p, double seconds)
{
	const double end = p->t + seconds;
	while (p->t < end) {
		bool clk = p->manualClock;
		if (p->clockMode == 1) {
			if (p->t < p->t0) clk = false;
			else { double ph = (p->t - p->t0) * p->bpm / 60.0 * 24; ph -= std::floor(ph); clk = ph < p->duty; }
		}
		p->m.setClock(clk);
		// mirror the board model's INT accumulator to timestamp INT pulses
		double acc = p->irqAcc + p->dt;
		bool irq = acc >= Machine::kIrqSeconds;
		if (irq) acc -= Machine::kIrqSeconds;
		p->irqAcc = acc;
		if (irq && p->logInt) p->evs.push_back({p->t, 3, 0, 0, 0});
		p->m.tick(Machine::kCycleHz);
		const auto &l = p->m.lines();
		if (p->m.strobes() != p->lastStrobes) {
			p->lastStrobes = p->m.strobes();
			p->evs.push_back({p->t, 2, p->m.dacCode(), l.accent, 0});
		}
		if (l.slide != p->pi1 || l.gate != p->pi2) {
			p->pi1 = l.slide; p->pi2 = l.gate;
			p->evs.push_back({p->t, 1, p->pi1, p->pi2, 0});
		}
		const uint16_t mat = p->m.ledMatrix();
		for (int i = 0; i < 16; ++i) if (mat >> i & 1) p->ledOn[i] += 1;
		p->ledN += 1;
		p->t += p->dt;
	}
}
int p_events(Probe *p, Ev *out, int cap) { int n = int(p->evs.size()) < cap ? int(p->evs.size()) : cap; std::memcpy(out, p->evs.data(), sizeof(Ev) * n); p->evs.erase(p->evs.begin(), p->evs.begin() + n); return n; }
// matrix LED duty since last call (16), then smoothed panel levels (21), then pc holds (4)
void p_leds(Probe *p, float *out)
{
	for (int i = 0; i < 16; ++i) out[i] = p->ledN > 0 ? float(p->ledOn[i] / p->ledN) : 0, p->ledOn[i] = 0;
	p->ledN = 0;
	for (int i = 0; i < int(Machine::Led::Count); ++i) out[16 + i] = p->m.led(Machine::Led(i));
	for (int i = 0; i < 4; ++i) out[37 + i] = p->m.pcHold(i);
}
void p_ram(Probe *p, uint8_t *out) { std::memcpy(out, p->m.patternRam().data(), Machine::kRamNibbles); }
void p_setram(Probe *p, const uint8_t *in) { p->m.setPatternRam(in, Machine::kRamNibbles); }
int p_dac(Probe *p) { return p->m.dacCode(); }
int p_pins(Probe *p) { const auto &l = p->m.lines(); return (l.slide ? 2 : 0) | (l.gate ? 4 : 0) | (l.accent ? 8 : 0); }
}
