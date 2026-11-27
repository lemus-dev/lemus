#include <cstdlib>
#include "machine.h"

#include "ms20/netlists.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>

namespace ms20 {

namespace {

constexpr int kControl = 0, kAudio = 1;

// TR3's junction noise into noise.cir (rz3 1k): white, 4.6 µV/√Hz, the level at which VCO 1's
// WHITE stands against its saw as in a real unit's recording (t8t17yEHHUQ: 200 Hz-4 kHz 9.99 dB
// under the saw's H1, ours 9.90; docs/compare.md §2). J25 then gives 2.1 Vpp (6 sigma) against the
// specification's 5 Vpp: NOISE LEVEL (VR3) has no procedure in §7, and this unit's sits low.
constexpr double kNoiseDensity = 4.6e-6;
// The MG's start-up: its operating point is the integrator's unstable balance; a real one starts
// from noise. 10 µA for 1 ms into IC14's summing node (mg.cir's mgkick).
constexpr double kKickAmps = 10e-6, kKickSeconds = 1e-3;
// POWER (docs/circuit/keyboard.md, "Power supply"): the transformer's secondary through D16/D19
// and D15/D18 charges C23 (drawn 1000 uF; the parts list's 10000 uF unconfirmed) and C24 (470 uF)
// to the raw rails, which IC16 7815 and IC17 7915 regulate to +-15 V. Neither the transformer's
// winding resistance nor the load on each rail is published (the specification gives 10 W in all),
// so how the rails move is an estimate: switched on, the raw rail charges as 18 V (1 - e^(-t/5 ms));
// switched off, it falls as 18 V e^(-t/0.18 s) (C23 into about 0.1 A, about 180 ohms). The
// regulated rails are the raw less the regulators' 1.7 V dropout, up to 15 V: on, they reach
// +-15 V 13 ms after the switch; off, they hold for 13 ms and are gone after 0.42 s. Both move
// together (C24, half C23, would let -15 V go first). Every constant source in the netlists (the
// supplies and what is drawn from them as constants) scales with them, and every op-amp's output
// swing follows them less its headroom (Circuit::setConstantScale), as do the wheel's and
// MOMENTARY's jacks, the VCOs' shapers into the mixer and the noise junction's noise. Not modelled:
// the op-amps' minimum supply (the run-down stops the board there instead: kDeadRail), the VCO
// cores' trip and top (vco.h), the mains' phase at the switch.
constexpr double kRawVolts = 18, kDropout = 1.7, kRail = 15;
constexpr double kRiseTau = 5e-3, kFallTau = 0.18;
// Switched on, the machine stays awake this long whatever SIGNAL OUT does: the board is still
// charging to its operating point (ms20bench boot: SIGNAL OUT within 1 mV of a warm start's after
// 0.26 s, the capacitors within 10 mV after 1.2 s, the last signal, IC11b's output as C35 charges
// through R120, within 1 mV after 3.5 s).
constexpr double kBootSeconds = 4.0;
// Switched off: below kDeadRail the op-amps' swing is down to 2-3.5 V (Circuit::setConstantScale),
// the sound has died with the rails (a held note -37 dB at 100 ms), and the solvers would go on
// past where the model holds (the MG's Schmitt trigger and EG 2's logic failed to converge at
// +-3-4 V). The board stops there; SIGNAL OUT is C33 (1 uF) emptying through R119, VOLUME and the
// load (about 3 kOhm: kDeadTau), off once under -120 dB or after kDownFade.
constexpr double kDeadRail = 4.5 / kRail, kDeadTau = 3e-3, kDownFade = 0.02;
// The falling rails reach the board every kRailEvery samples (stepRails): 0.7 ms at 48 kHz, steps
// of 0.45 % of the rails at most, each costing the blocks a linearisation (EG 2's 52 ports the
// most). Every sample, single samples of EG 1's and EG 2's logic failed to converge as the rails
// fell through 4-10 V with a key held; every 64, EG 2 a whole step at 96 kHz; every 32, none
// (8 settings, 44.1-96 kHz, a key held or not).
constexpr int kRailEvery = 32;
// The panel's LEDs need about 2 V across them and their resistors.
constexpr double kLedRail = 2.0 / kRail;
// IC13B's internal pole at switch-on (volts; its open-loop gain makes it the output's side).
constexpr double kSchmittStart = 1e-5;

std::string withModels(const char *text) { return std::string(netlists::models) + "\n" + text; }

double knob(double v) { return std::clamp(v, 0.0, 10.0) / 10; }
double centred(double v) { return (std::clamp(v, -10.0, 10.0) + 10) / 20; }

} // namespace

Machine::Machine() { build(); }

void Machine::build()
{
	const bool rev2 = m_rev == FilterRev::Rev2;
	std::vector<Board::BlockSpec> blocks = {
		// the control side, at the host's rate
		{"kbd", withModels(netlists::kbd), kControl, 2},
		{"trig", withModels(netlists::trig), kControl, 2},
		// EG 2's 52 ports (its 4007s and 4069s) on the sparse Newton: refactoring them each sample
		// while its gates cross their thresholds at a note event took a 64-sample block 1 ms
		{"eg2", withModels(netlists::eg2), kControl, 1, 0, true},
		{"eg1", withModels(netlists::eg1), kControl, 1},
		{"mg", withModels(netlists::mg), kControl, 1},
		{"mvca", withModels(netlists::mvca), kControl, 1},
		{"noise", withModels(netlists::noise), kControl, 1},
		{"sh", withModels(netlists::sh), kControl, 1},
		{"esp", withModels(netlists::esp), kControl, 1},
		{"espenv", withModels(netlists::espenv), kControl, 1},
		{"espfv", withModels(netlists::espfv), kControl, 4, 0, true},
		{"vcoctl", withModels(netlists::vcoctl), kControl, 1},
		// the audio path, oversampled
		{"mixer", withModels(netlists::mixer), kAudio, 1},
		// the KORG35s hold their quarters while full steps can't follow them (Circuit::setHold), and
		// their outputs are filtered from those quarters (Circuit::setDecimation): self-oscillating
		// near 37 kHz, their harmonics would fold into the band at the samples
		// (Rev 2's OTAs, tanh in their inputs, leave linear mode at once at 1e-6 V; at 1e-5 V the
		// SPICE cases stay within 0.6 %: klm307.md §11; 3e-5 since CPU round 6, decided: 1-8 %
		// cheaper, the self-oscillation's pitch within 0.17 cents)
		{"hpf", withModels(rev2 ? netlists::hpf307 : netlists::hpf), kAudio, 1, rev2 ? 3e-5 : 1e-6, true, 0, true, true},
		{"lpf", withModels(rev2 ? netlists::lpf307 : netlists::lpf), kAudio, 1, rev2 ? 3e-5 : 1e-6, true, 0, true, true},
		{"vca", withModels(rev2 ? netlists::vca307 : netlists::vca), kAudio, 1},
	};
	m_board.load(blocks);
	m_noiseWanted = true;
	m_board.setWanted({"sigout", "ia1", "ia2", "white"});
	m_ia1 = m_board.signalIndex("ia1");
	m_ia2 = m_board.signalIndex("ia2");
	m_white = m_board.signalIndex("white");
	m_sigout = m_board.signalIndex("sigout");
	m_ledv = m_board.signalIndex("ledv");
	m_xHostin = m_board.externalIndex("hostin");
	m_xNoise = m_board.externalIndex("noise");
	m_xKick = m_board.externalIndex("mgkick");
	m_xWheel = m_board.externalIndex("wheel");
	m_xMom = m_board.externalIndex("mom");
	m_xVco1 = m_board.externalIndex("vco1");
	m_xVco2 = m_board.externalIndex("vco2");
	m_xRamp1 = m_board.externalIndex("ramp1");
	m_xRamp2 = m_board.externalIndex("ramp2");
	m_filterRate[0].cv = m_board.signalIndex("hcv");
	m_filterRate[1].cv = m_board.signalIndex("lcv");
	m_b6 = m_board.signalIndex("b6");
	m_b7 = m_board.signalIndex("b7");
	m_vb = m_board.signalIndex("vblog");
	m_mgBlock = m_board.blockIndex("mg");
	m_mgRect = m_board.signalIndex("mgsq");
	m_mgC22 = m_mgBlock >= 0 ? m_board.capIndex(m_mgBlock, "C22") : -1;
	setCalibration(m_cal);
	// two threads: the audio group on the caller's, with the VCOs' outputs it sets
	m_board.setSide(kAudio, {m_xVco1, m_xVco2});
	m_carry.reserve(kCarryMax);
	m_ring.assign(size_t(kSplitMax) * kCarryMax, 0.0);
	// MS20_THREADS=1 (tools): one thread; MS20_HIST's tables are shared between the circuits
	if (const char *t = std::getenv("MS20_THREADS"); (t != nullptr && std::atoi(t) == 1) || std::getenv("MS20_HIST") != nullptr)
		m_threaded = false;
	if (std::thread::hardware_concurrency() < 2)
		m_threaded = false;
	m_timeWaits = std::getenv("MS20_THREADSTATS") != nullptr;
}

void Machine::setThreaded(bool on)
{
	m_threaded = on;
	if (!on)
		m_worker.reset();
	else if (m_prepared && !m_worker)
		m_worker = std::make_unique<Worker>(m_workerHooks);
}

void Machine::setCalibration(const Calibration &c)
{
	m_cal = c;
	m_board.setControl("kbd", "trim_vr4", c.vr4);
	m_board.setControl("kbd", "trim_vr3", c.vr3);
	m_board.setControl("vcoctl", "trim_vr1", c.vr1);
	m_board.setControl("vcoctl", "trim_vr2", c.vr2);
	m_board.setControl("vcoctl", "trim_vr5", c.vr5);
	m_board.setControl("vcoctl", "trim_vr6", c.vr6);
	const bool rev2 = m_rev == FilterRev::Rev2;
	m_board.setControl("hpf", "trim_vr7", rev2 ? c.vr7b : c.vr7);
	m_board.setControl("lpf", "trim_vr8", rev2 ? c.vr8b : c.vr8);
	if (rev2) {
		m_board.setControl("hpf", "trim_307vr1", c.k307vr1);
		m_board.setControl("lpf", "trim_307vr2", c.k307vr2);
	}
}

void Machine::setFilterRev(FilterRev rev)
{
	if (rev == m_rev)
		return;
	m_rev = rev;
	build();
	m_board.setPatch(m_patch);
	m_controlsDirty = true;
	if (m_prepared)
		prepare(m_rate);
}

void Machine::prepare(double sampleRate)
{
	m_rate = sampleRate > 0 ? sampleRate : 48000;
	m_board.setRate(kControl, m_rate);
	m_board.setRate(kAudio, m_rate * kOver);
	m_vcos.setRate(m_rate * kOver);
	m_decimator.setStages(2);
	m_noiseSigma = kNoiseDensity * std::sqrt(m_rate / 2);
	m_deadDecay = std::exp(-1 / (kDeadTau * m_rate));
	m_prepared = true;
	if (m_threaded && !m_worker)
		m_worker = std::make_unique<Worker>(m_workerHooks);
	start();
}

// The circuit from its operating point: a plugin load, a session load, powerOn().
void Machine::start()
{
	// the rails settled (a run-down that was under way is over)
	m_raw = kRawVolts;
	m_rail = 1;
	m_railsMoving = false;
	m_awakeFor = 0;
	m_board.setSupply(1);
	if (m_power == Power::Down)
		m_power = Power::Off;
	applyControls();
	// the cascodes' drain, held at the ramp's middle: the charge current moves under 0.005 % over
	// the ramp (vco.md §9), while feeding back the live ramp puts a 6 V step into the converters'
	// MHz-fast loops at every reset, which Newton can't follow
	m_board.setExternal("ramp1", (Vco::core1().top + Vco::core1().trip) / 2);
	m_board.setExternal("ramp2", (Vco::core2().top + Vco::core2().trip) / 2);
	// the wheel's and MOMENTARY SWITCH's jacks as render sets them, so a patch from them starts settled
	m_board.setExternalAt(m_xWheel, 5.0 * std::clamp(m_manual.wheel, -1.0, 1.0));
	m_board.setExternalAt(m_xMom, m_manual.momentary ? 0.0 : 5.16);
	m_board.settle();
	// C6 (the portamento's capacitor) from 0 V: the operating point with no key down is its
	// leakage equilibrium, about -3 V (docs/circuit/keyboard.md)
	m_board.setCapacitor("kbd", "C6", 0.0);
	m_vcos.reset();
	m_decimator.reset();
	m_up.fill(0);
	m_kick = int(kKickSeconds * m_rate);
	m_cdsR = m_cdsSet = 10e6; // the cell dark, as the netlist's pc1 0.1 has it
	for (auto &f : m_filterRate) {
		f.over = 1;
		f.hold = 0;
		m_board.setOver(f.block, 1);
	}
	m_board.setControl("mvca", "pc1", m_cdsR / 100e6);
	m_asleep = false;
	m_quiet = 0;
	m_mgFree.stage = MgFree::Stage::Off;
	m_mgLed = -1;
	resetDrift();
	// the asleep MG's state buffers sized here, not on the audio thread
	if (m_mgBlock >= 0) {
		auto &f = m_mgFree;
		for (auto *x : {&f.a0, &f.a1, &f.b0, &f.b1, &f.now, &f.wait})
			m_board.stateOf(m_mgBlock, *x);
		for (auto &r : f.ring)
			m_board.stateOf(m_mgBlock, r);
	}
}

void Machine::pickFilterRates(const double *v)
{
	for (auto &f : m_filterRate) {
		if (f.hold > 0) {
			--f.hold;
			continue;
		}
		// Rev 2's expo node (hpf307.cir, lpf307.cir) in Rev 1's pin-1 terms, so one pair of thresholds
		// holds for both
		const double raw = f.cv >= 0 ? v[f.cv] : 0.0;
		const double cv = m_rev == FilterRev::Rev2 ? kRev2CvScale * (raw - kRev2CvZero) : raw;
		// MS20_FILTER_OVER=N (tools): the KORG35s at N times the audio path's rate throughout (4: 16x)
		static const int forced = std::getenv("MS20_FILTER_OVER") ? std::atoi(std::getenv("MS20_FILTER_OVER")) : 0;
		const int want = forced > 0 ? forced : cv > kFastCv ? 2 : cv < kSlowCv ? 1 : f.over;
		if (want != f.over) {
			f.over = want;
			f.hold = int(kFilterDwell * m_rate);
			m_board.setOver(f.block, want);
		}
	}
}

// PC1's cell (docs/circuit/klm128.md §3.3): the steady resistance from Figure 5's table (log-log,
// extended past its ends, capped at the sheet's 10 Mohm dark), reached by a first-order conductance
// rise (tau 4 ms x (20 mA / I)^0.39) when brightening and a linear rise of 2.55 Mohm/s when
// darkening. PPC1 is set when the resistance has moved 0.1 %.
void Machine::stepCds()
{
	static constexpr double kI[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 20};               // mA
	static constexpr double kR[] = {335e3, 145e3, 46e3, 20.5e3, 9.8e3, 4.3e3, 2.47e3, 1.5e3}; // ohms
	constexpr int n = 8;
	const double i = m_ledv >= 0 ? m_board.signalAt(m_ledv) : 0.0; // V across R24 1k = mA
	double rss = 10e6;
	if (i > 0) {
		int k = 0;
		while (k < n - 2 && i > kI[k + 1])
			++k;
		const double t = std::log(i / kI[k]) / std::log(kI[k + 1] / kI[k]);
		rss = std::min(10e6, std::exp(std::log(kR[k]) + t * (std::log(kR[k + 1]) - std::log(kR[k]))));
	}
	const double dt = 1.0 / m_rate;
	if (rss < m_cdsR) {
		const double tau = 4e-3 * std::pow(20.0 / std::max(i, 1e-6), 0.39);
		const double g = 1 / m_cdsR + (1 / rss - 1 / m_cdsR) * (1 - std::exp(-dt / tau));
		m_cdsR = 1 / g;
	} else
		m_cdsR = std::min(rss, m_cdsR + 2.55e6 * dt);
	if (std::abs(m_cdsR - m_cdsSet) > 1e-3 * m_cdsSet) {
		m_board.setControl("mvca", "pc1", m_cdsR / 100e6);
		m_cdsSet = m_cdsR;
	}
}

void Machine::setControls(const Controls &c)
{
	if (std::memcmp(&c, &m_controls, sizeof(c)) == 0)
		return;
	m_controls = c;
	m_controlsDirty = true;
	wake();
}

void Machine::setPatch(const Patch &p)
{
	if (p == m_patch)
		return;
	m_patch = p;
	m_board.setPatch(p);
	m_mvcaUsed = false;
	for (int j = 0; j < kFirstOutput; ++j)
		if (p.patched(Jack(j)) && p.from[size_t(j)] == uint8_t(Jack::MvcaOut))
			m_mvcaUsed = true;
	wake();
}

void Machine::setKey(int key, bool down)
{
	if (key < 0 || key >= kKeys || m_keys[size_t(key)] == down)
		return;
	m_keys[size_t(key)] = down;
	m_board.setControl("key_" + std::to_string(key), down ? 1.0 : 0.0);
	wake();
}

int Machine::soundingKey() const
{
	for (int k = kKeys - 1; k >= 0; --k)
		if (m_keys[size_t(k)])
			return k;
	return -1;
}

void Machine::applyControls()
{
	const auto &c = m_controls;
	auto set = [&](const char *name, double v) { m_board.setControl(name, v); };
	// the noise board runs while VCO 1 takes its WHITE (a cable from WHITE or PINK makes it needed
	// through the board's wiring); idle, it holds its state and the plugin skips its steps
	const bool noiseWave = c.vco1Wave == Vco1Wave::Noise;
	if (noiseWave != m_noiseWanted) {
		m_noiseWanted = noiseWave;
		if (noiseWave)
			m_board.setWanted({"sigout", "ia1", "ia2", "white"});
		else
			m_board.setWanted({"sigout", "ia1", "ia2"});
	}
	set("portamento", knob(c.portamento));
	set("master_tune", centred(c.masterTune));
	set("vco2_pitch", centred(c.vco2Pitch));
	set("fm_mg", knob(c.fmMg));
	set("fm_eg1", knob(c.fmEg1));
	static const char *s1[] = {"vco1_scale_32", "vco1_scale_16", "vco1_scale_8", "vco1_scale_4"};
	static const char *s2[] = {"vco2_scale_16", "vco2_scale_8", "vco2_scale_4", "vco2_scale_2"};
	for (int i = 0; i < 4; ++i) {
		set(s1[i], c.vco1Scale == i ? 1.0 : 0.0);
		set(s2[i], c.vco2Scale == i ? 1.0 : 0.0);
	}
	set("vco1_level", knob(c.vco1Level));
	set("vco2_level", knob(c.vco2Level));
	set("hpf_cutoff", knob(c.hpfCutoff));
	set("hpf_peak", knob(c.hpfPeak));
	set("hpf_mg", knob(c.hpfMg));
	set("hpf_eg2", knob(c.hpfEg2));
	set("lpf_cutoff", knob(c.lpfCutoff));
	set("lpf_peak", knob(c.lpfPeak));
	set("lpf_mg", knob(c.lpfMg));
	set("lpf_eg2", knob(c.lpfEg2));
	set("mg_wave", knob(c.mgWave));
	set("mg_freq", knob(c.mgFreq));
	set("eg1_delay", knob(c.eg1Delay));
	set("eg1_attack", knob(c.eg1Attack));
	set("eg1_release", knob(c.eg1Release));
	set("eg2_hold", knob(c.eg2Hold));
	set("eg2_attack", knob(c.eg2Attack));
	set("eg2_decay", knob(c.eg2Decay));
	set("eg2_sustain", knob(c.eg2Sustain));
	set("eg2_release", knob(c.eg2Release));
	set("volume", knob(c.volume));
	set("esp_level", knob(c.espLevel));
	set("esp_lowcut", knob(c.espLowCut));
	set("esp_highcut", knob(c.espHighCut));
	set("esp_cvadj", knob(c.espCvAdj));
	set("esp_threshold", knob(c.espThreshold));
	m_controlsDirty = false;
}

Machine::Leds Machine::leds() const
{
	Leds l;
	if (m_power == Power::Off || m_rail < kLedRail)
		return l;
	l.kbd = m_board.signal("kbdtrig") < 0;
	const int mgLed = m_mgLed;
	l.mg = mgLed >= 0 ? mgLed == 1 : m_board.signal("mgsq") > 2.5;
	// the LED currents as volts across R8 4.7K and R7 2.7K: lit above about 1 mA
	l.espPeak = m_board.running("espenv") && m_board.signal("peakled") > 2.7;
	l.espTrig = m_board.running("espenv") && m_board.signal("trigled") > 4.7;
	return l;
}

void Machine::powerOn()
{
	if (m_power == Power::On && !m_railsMoving)
		return;
	m_power = Power::On;
	start();
}

void Machine::powerOff()
{
	m_power = Power::Off;
	m_railsMoving = false;
	m_raw = m_rail = 0;
}

void Machine::switchOn()
{
	if (m_power == Power::On)
		return;
	if (m_power == Power::Off) {
		// from cold: every capacitor of the board at 0 V, the rails at 0 (the VCO cores and shapers,
		// in C++, at rest as start() has them)
		m_raw = m_rail = 0;
		applyControls();
		for (auto &f : m_filterRate) {
			f.over = 1;
			f.hold = 0;
			m_board.setOver(f.block, 1);
		}
		m_board.setSupply(0);
		m_board.discharge();
		// IC13B, the trigger's Schmitt trigger, from the side no key down gives: as the rails come
		// up IC13A swings high (no key: its + input 0.44 V of the rail above the ladder) and charges
		// C17, which leads IC13B's + input within microseconds. With every node at 0 V the solver
		// would decide that tie at microvolts, either way (low: both envelopes fire at the switch).
		// A key held at the switch brings it low through C17 as any key does.
		m_board.setCapacitor("trig", "cXIC13B_pole", kSchmittStart);
		m_vcos.reset();
		resetDrift(); // tuned at power-on (the warm-up isn't modelled)
		m_decimator.reset();
		m_up.fill(0);
		m_kick = int(kKickSeconds * m_rate);
		m_cdsR = m_cdsSet = 10e6;
		m_board.setControl("mvca", "pc1", m_cdsR / 100e6);
	}
	// (still running down: the raw rail charges again from where it is)
	m_power = Power::On;
	m_railsMoving = true;
	m_awakeFor = long(kBootSeconds * m_rate);
	m_downQuiet = 0;
	wake();
}

void Machine::switchOff()
{
	if (m_power != Power::On)
		return;
	m_power = Power::Down;
	m_railsMoving = true;
	m_awakeFor = 0;
	m_downQuiet = 0;
	wake();
}

// One sample of the raw rail charging (on) or discharging (off), and the regulated rails' share:
// the board takes it each sample while it rises (13 ms from the switch, and steep), and every
// kRailEvery samples while it falls (slower: 0.4 s), so that the solvers' linear mode runs between
// (Circuit::setConstantScale).
void Machine::stepRails()
{
	const double dt = 1.0 / m_rate;
	if (m_power == Power::On)
		m_raw = kRawVolts - (kRawVolts - m_raw) * std::exp(-dt / kRiseTau);
	else
		m_raw *= std::exp(-dt / kFallTau);
	const double rail = std::clamp((m_raw - kDropout) / kRail, 0.0, 1.0);
	if (m_power == Power::On && rail >= 1) {
		m_raw = kRawVolts; // regulated from here
		m_railsMoving = false;
	} else if (m_power == Power::Down && rail < kDeadRail) {
		// the op-amps have stopped: the board stops with the rails it has (render)
		m_railsMoving = false;
		m_rail = rail;
		return;
	} else if (m_power == Power::Down && rail < 1 && ++m_railTick < kRailEvery)
		return;
	m_railTick = 0;
	m_rail = rail;
	m_board.setSupply(m_rail, m_power == Power::On && m_railsMoving); // (rising: again next sample)
	m_board.setExternalAt(m_xRamp1, m_rail * (Vco::core1().top + Vco::core1().trip) / 2);
	m_board.setExternalAt(m_xRamp2, m_rail * (Vco::core2().top + Vco::core2().trip) / 2);
}

void Machine::endRunDown()
{
	m_power = Power::Off;
	m_railsMoving = false;
	m_raw = m_rail = 0;
	m_asleep = false;
	m_quiet = m_downQuiet = 0;
	m_lastOut = 0;
	m_mgFree.stage = MgFree::Stage::Off;
	m_mgLed = -1;
}

// ========================================================================== drift

void Machine::resetDrift()
{
	m_driftT = 0;
	m_driftOsc = {0, 0};
	m_driftRng.seed(m_driftSeed);
}

void Machine::setSeed(uint64_t seed)
{
	m_noiseSeed = seed == 0 ? 20u : uint32_t(seed) | 1u;
	m_driftSeed = seed == 0 ? 21u : uint32_t(seed >> 32) ^ 0x9e3779b9u;
	m_rng.seed(m_noiseSeed);
	m_gauss.reset();
	m_driftRng.seed(m_driftSeed);
}

// Each walk an Ornstein-Uhlenbeck process: over any stretch, exactly (decay, and the spread the
// stretch adds), so a long call or a sleep steps as many short ones would on average.
void Machine::stepDrift(double seconds)
{
	if (seconds <= 0)
		return;
	std::normal_distribution<double> g(0.0, 1.0);
	auto walk = [&](double &x, double rms, double tau) {
		const double a = std::exp(-seconds / tau);
		x = x * a + rms * std::sqrt(std::max(0.0, 1 - a * a)) * g(m_driftRng);
	};
	walk(m_driftT, kDriftKelvin, kDriftTau);
	walk(m_driftOsc[0], kDriftOscCents, kDriftOscTau);
	walk(m_driftOsc[1], kDriftOscCents, kDriftOscTau);
}

double Machine::driftFactor(int vco, const double *v) const
{
	if (!m_driftOn)
		return 1.0;
	constexpr double kT0 = 300.15, kVt0 = 0.025864186; // the solver's V_T (27 C)
	const int b = vco == 0 ? m_b6 : m_b7;
	const double octaves = b >= 0 && m_vb >= 0 ? (v[b] - v[m_vb]) / (kVt0 * 0.69314718055994531) : 0.0;
	const double cents = kDriftLinear * m_driftT + m_driftOsc[size_t(vco)];
	return std::exp2(cents / 1200 - octaves * m_driftT / kT0);
}

// ========================================================================== asleep: the free runners

void Machine::fallAsleep(int rest)
{
	m_asleep = true;
	m_sleepDrift = {driftFactor(0, m_board.values()), driftFactor(1, m_board.values())};
	auto &f = m_mgFree;
	f.slept = 0;
	f.stage = m_mgBlock >= 0 && m_mgRect >= 0 && m_mgC22 >= 0 && m_board.running("mg") ? MgFree::Stage::Wait : MgFree::Stage::Off;
	f.high = m_board.signalAt(m_mgRect) > 2.5;
	for (auto &r : f.ring)
		r.vc.clear();
	m_mgLed = -1;
	sleepTick(rest);
}

void Machine::sleepTick(int n)
{
	using Stage = MgFree::Stage;
	auto &f = m_mgFree;
	auto stepping = [&] {
		return f.stage == Stage::Wait || f.stage == Stage::A || f.stage == Stage::B || f.stage == Stage::End || f.stage == Stage::Refine;
	};
	// where two lines of C22's voltage against time meet
	auto meet = [](double p1, double x1, double r1, double p2, double x2, double r2) {
		return (x2 - x1 + r1 * p1 - r2 * p2) / (r1 - r2);
	};
	for (int i = 0; i < n && stepping(); ++i) {
		const long long s = f.slept + i; // this sample, counted from the first asleep
		m_board.stepAlone(m_mgBlock);
		const bool high = m_board.signalAt(m_mgRect) > 2.5;
		const bool flipped = high != f.high;
		f.high = high;
		auto &early = f.ring[size_t(s % 4)]; // the state kMgEarly (4) samples ago, until saved below
		if (f.stage == Stage::Refine) {
			if (flipped && high == f.highA) { // a half A begins: the half B before it ends
				f.rb1 = early;
				f.rEndB = s - kMgEarly;
				++f.rCycles;
				f.rAAt = s + kMgSettle;
			} else if (flipped)
				f.rStartB = s;
			if (f.rStartB >= 0 && s == f.rStartB + kMgSettle)
				m_board.stateOf(m_mgBlock, f.rb0);
			if (f.rCycles >= kMgCycles - 1 && s == f.rAAt && f.rStartB >= 0) {
				// the turn that ends the last cycle, as turnNext was found; the length over them all
				m_board.stateOf(m_mgBlock, f.now);
				const size_t c = size_t(m_mgC22);
				const double pB = double(f.rStartB + kMgSettle);
				const double rB = (f.rb1.vc[c] - f.rb0.vc[c]) / (double(f.rEndB) - pB);
				const double t = meet(pB, f.rb0.vc[c], rB, double(s), f.now.vc[c], f.rA);
				const double len = (t - f.turnA) / double(1 + f.rCycles), len1 = f.halfA + f.halfB;
				if (std::isfinite(t) && std::abs(t - double(s - kMgSettle)) < kMgEarly && std::abs(len / len1 - 1) < 0.01) {
					f.halfA *= len / len1;
					f.halfB *= len / len1;
					f.ref = t;
				}
				f.stage = Stage::Known;
				break;
			}
			m_board.stateOf(m_mgBlock, early);
			continue;
		}
		if (flipped && f.stage == Stage::Wait) {
			f.stage = Stage::A;
			f.highA = high;
			f.startA = s;
			f.wait = early; // on the waiting half's line
		} else if (flipped && f.stage == Stage::A) {
			f.a1 = early;
			f.endA = s - kMgEarly;
			f.lenA = s - f.startA;
			f.stage = Stage::B;
			f.startB = s;
		} else if (flipped && f.stage == Stage::B) {
			f.b1 = early;
			f.endB = s - kMgEarly;
			f.lenB = s - f.startB;
			f.endAt = s + kMgSettle;
			f.stage = Stage::End;
		} else if (s >= (long long)(kMgGiveUp * m_rate) || (flipped && f.stage == Stage::End)) {
			f.stage = Stage::Off;
			break;
		}
		if (f.stage == Stage::A && s == f.startA + kMgSettle)
			m_board.stateOf(m_mgBlock, f.a0);
		if (f.stage == Stage::B && s == f.startB + kMgSettle)
			m_board.stateOf(m_mgBlock, f.b0);
		if (f.stage == Stage::End && s == f.endAt) {
			// C22's voltage on each half's line (kMgSettle in to the end), and where the lines
			// meet: the turns
			m_board.stateOf(m_mgBlock, f.now);
			const size_t c = size_t(m_mgC22);
			const double pA = double(f.startA + kMgSettle), pB = double(f.startB + kMgSettle);
			const double rA = (f.a1.vc[c] - f.a0.vc[c]) / (double(f.endA) - pA);
			const double rB = (f.b1.vc[c] - f.b0.vc[c]) / (double(f.endB) - pB);
			f.turnA = meet(double(f.startA - kMgEarly), f.wait.vc[c], rB, pA, f.a0.vc[c], rA);
			f.turnB = meet(pA, f.a0.vc[c], rA, pB, f.b0.vc[c], rB);
			f.turnNext = meet(pB, f.b0.vc[c], rB, double(s), f.now.vc[c], rA);
			// a turn within kMgEarly of the sample its flip showed at: on a steep, short half (a fast
			// MG near a ramp) the lines meet 2-3 samples before the rectangle flips
			const bool sane = std::isfinite(f.turnA + f.turnB + f.turnNext) && std::abs(f.turnA - double(f.startA)) < kMgEarly &&
			                  std::abs(f.turnB - double(f.startB)) < kMgEarly && std::abs(f.turnNext - double(f.startB + f.lenB)) < kMgEarly &&
			                  f.lenA > 2 * (kMgSettle + kMgEarly) && f.lenB > 2 * (kMgSettle + kMgEarly) && !f.wait.vc.empty();
			if (!sane) {
				f.stage = Stage::Off;
				break;
			}
			// known from here (a wake places it by this cycle); the MG steps on to refine it
			f.halfA = f.turnB - f.turnA;
			f.halfB = f.turnNext - f.turnB;
			f.ref = f.turnNext;
			f.rA = rA;
			f.rStartB = -1;
			f.rAAt = -1;
			f.rCycles = 0;
			f.stage = Stage::Refine;
		}
		m_board.stateOf(m_mgBlock, early);
	}
	f.slept += n;
	bool inA = false;
	double w = 0;
	if (mgPlace(f.slept - 1, inA, w))
		m_mgLed = inA == f.highA ? 1 : 0;
}

bool Machine::mgPlace(long long sample, bool &inA, double &w) const
{
	const auto &f = m_mgFree;
	if (f.stage != MgFree::Stage::Known && f.stage != MgFree::Stage::Refine)
		return false;
	const double halfA = f.halfA, halfB = f.halfB;
	const double firstA = f.turnB - f.turnA, firstB = f.turnNext - f.turnB; // the first cycle's
	if (!(halfA > 0 && halfB > 0 && firstA > 0 && firstB > 0))
		return false;
	// time into the cycle from the latest half A's turn found, then that time in the first cycle's
	// samples (a0 at start + kMgSettle, a1 at endA)
	double phase = std::fmod(double(sample) - f.ref, halfA + halfB);
	if (phase < 0)
		phase += halfA + halfB;
	inA = phase < halfA;
	if (inA)
		w = (f.turnA + phase * firstA / halfA - double(f.startA + kMgSettle)) / double(f.endA - f.startA - kMgSettle);
	else
		w = (f.turnB + (phase - halfA) * firstB / halfB - double(f.startB + kMgSettle)) / double(f.endB - f.startB - kMgSettle);
	return true;
}

void Machine::wakeUp()
{
	auto &f = m_mgFree;
	const double seconds = double(f.slept) / m_rate;
	if (m_ia1 >= 0 && m_ia2 >= 0) {
		// the drift over the sleep: midway between where it was and where it is
		const double *v = m_board.values();
		const double d1 = 0.5 * (m_sleepDrift[0] + driftFactor(0, v)), d2 = 0.5 * (m_sleepDrift[1] + driftFactor(1, v));
		m_vcos.advance(v[m_ia1] * d1, v[m_ia2] * d2, seconds);
	}
	bool inA = false;
	double w = 0;
	if (mgPlace(f.slept - 1, inA, w)) {
		// the last asleep sample's place in the cycle, and the state there
		const auto &x0 = inA ? f.a0 : f.b0;
		const auto &x1 = inA ? f.a1 : f.b1;
		auto mix = [w](const std::vector<double> &a, const std::vector<double> &b, std::vector<double> &out) {
			out.resize(a.size());
			for (size_t k = 0; k < a.size() && k < b.size(); ++k)
				out[k] = a[k] + (b[k] - a[k]) * w;
		};
		mix(x0.vc, x1.vc, f.now.vc);
		mix(x0.ic, x1.ic, f.now.ic);
		mix(x0.v, x1.v, f.now.v);
		mix(x0.u, x1.u, f.now.u);
		m_board.setStateOf(m_mgBlock, f.now); // its outputs follow at the next step
	}
	f.stage = MgFree::Stage::Off;
	f.slept = 0;
	m_mgLed = -1;
}

void Machine::render(float *out, int n, const float *in)
{
	if (m_power == Power::Off) {
		std::fill(out, out + n, 0.0f);
		return;
	}
	if (m_controlsDirty)
		applyControls();
	// a sound at the input wakes it
	if (in != nullptr && m_asleep)
		for (int i = 0; i < n; ++i)
			if (std::abs(in[i]) > 1e-6f) {
				wake();
				break;
			}
	stepDrift(n / m_rate);
	if (m_asleep) {
		sleepTick(n);
		std::fill(out, out + n, 0.0f);
		return;
	}
	const bool noiseWave = m_controls.vco1Wave == Vco1Wave::Noise;
	int splitFrom = 0; // where two threads may take the next stretch
	for (int i = 0; i < n; ++i) {
		if (i == splitFrom) {
			const int len = std::min(n - i, kSplitMax);
			splitFrom = i + len;
			if (canSplit(len)) {
				renderSplit(out + i, len, in != nullptr ? in + i : nullptr, noiseWave);
				i += len - 1;
				continue;
			}
			++m_threadStats.single;
		}
		// the rails rising or falling after the switch; what comes from them scales with them (at
		// 1, settled, exactly as it is)
		if (m_railsMoving)
			stepRails();
		if (m_power == Power::Down && m_rail < kDeadRail) {
			// the op-amps have stopped: SIGNAL OUT is C33 emptying into VOLUME and the load (the
			// board no longer steps), then off
			m_lastOut *= float(m_deadDecay);
			if (std::abs(m_lastOut) < 1e-6f || ++m_downQuiet >= int(kDownFade * m_rate)) {
				endRunDown();
				std::fill(out + i, out + n, 0.0f);
				return;
			}
			out[i] = m_lastOut;
			continue;
		}
		controlSample(in, i);
		const float y = audioSample(m_board.values(), false, noiseWave);
		out[i] = y;
		if (m_power == Power::Down) {
			m_lastOut = y;
			continue;
		}
		if (m_awakeFor > 0) { // booting
			--m_awakeFor;
			m_quiet = 0;
			continue;
		}

		// sleep: no key down and SIGNAL OUT under -120 dB for 100 ms
		if (soundingKey() < 0 && std::abs(y) < 1e-6f && !m_keepAwake) {
			if (++m_quiet >= int(0.1 * m_rate)) {
				fallAsleep(n - i - 1);
				std::fill(out + i, out + n, 0.0f);
				return;
			}
		} else
			m_quiet = 0;
	}
}

void Machine::controlSample(const float *in, int i)
{
	const double rail = m_rail;
	m_board.setExternalAt(m_xHostin, in != nullptr ? double(in[i]) * kFullScale : 0.0);
	m_board.setExternalAt(m_xNoise, rail * (m_noiseSigma * m_gauss(m_rng)));
	m_board.setExternalAt(m_xKick, m_kick > 0 ? kKickAmps : 0.0);
	// the PROGRAMMABLE CONTROL WHEEL's jack, ±5 V (specification), and the MOMENTARY SWITCH's,
	// shorted to ground while pressed (an S-trigger; open, the input it feeds pulls itself up:
	// taken as TRIG OUT's +5.16 V idle level)
	m_board.setExternalAt(m_xWheel, rail * (5.0 * std::clamp(m_manual.wheel, -1.0, 1.0)));
	m_board.setExternalAt(m_xMom, rail * (m_manual.momentary ? 0.0 : 5.16));
	if (m_kick > 0)
		--m_kick;
	m_board.step(kControl);
	if (m_mvcaUsed)
		stepCds();
}

float Machine::audioSample(double *v, bool side, bool noiseWave)
{
	const double rail = m_rail;
	pickFilterRates(v);
	const double ia1 = v[m_ia1] * driftFactor(0, v), ia2 = v[m_ia2] * driftFactor(1, v);
	const double white = noiseWave ? v[m_white] : 0.0;
	for (int k = 0; k < kOver; ++k) {
		m_vcos.step(ia1, ia2, white, m_controls);
		v[m_xVco1] = rail * m_vcos.common1();
		v[m_xVco2] = rail * m_vcos.common2();
		if (side)
			m_board.stepSide();
		else
			m_board.step(kAudio);
		m_up[size_t(k)] = float(v[m_sigout]);
	}
	return float(m_decimator.process(m_up.data()) / kFullScale);
}

// Two threads. The control group (keyboard, trigger, the EGs, MG, MVCA, S&H, ESP, the VCOs'
// converters: everything at the host's rate) feeds the audio path (the VCO cores, mixer, KORG35s,
// VCA at 4x) and, unpatched, takes nothing from it. So for a stretch of samples whose keys,
// controls and input are known before it starts (a render call, up to kSplitMax), the worker runs
// the control side through the stretch as fast as it goes, publishing each sample's signals that
// the audio path reads (Board::sideInputs, the VCOs' charge currents and WHITE) into a ring, and the
// caller runs the audio path for each sample as soon as it is published, on its own copy of the
// board's signals (Board::beginSide). Each side does exactly what one thread does in the same
// order, so the output is the same bit for bit; a note event's EG and keyboard spikes overlap the
// filters' work instead of adding to it. One thread runs everything when the stretch is short, a
// cable takes an audio signal into the control side (Board::splittable: the control side would
// need it a sample earlier than the audio side has it), the rails move (POWER's boot and run-down
// reach every block each sample), or sleep could start within the stretch (it stops the board
// mid-stretch, where the worker would have run on).
bool Machine::canSplit(int n)
{
	if (!m_threaded || !m_worker || n < kSplitMin || m_power != Power::On || m_railsMoving)
		return false;
	if (soundingKey() < 0 && m_quiet + n >= int(0.1 * m_rate))
		return false;
	if (!m_board.splittable())
		return false;
	if (m_carryWiring != m_board.wiring()) {
		m_carryWiring = m_board.wiring();
		m_carry.clear();
		bool fits = true;
		auto carry = [&](int sig) {
			if (sig < 0 || std::find(m_carry.begin(), m_carry.end(), sig) != m_carry.end())
				return;
			if (m_carry.size() < size_t(kCarryMax))
				m_carry.push_back(sig);
			else
				fits = false;
		};
		for (const int sig : m_board.sideInputs())
			carry(sig);
		carry(m_ia1);
		carry(m_ia2);
		carry(m_white);
		carry(m_b6); // (the drift's exponential part: driftFactor)
		carry(m_b7);
		carry(m_vb);
		if (!fits)
			m_carry.clear(); // (more than the ring holds: one thread)
	}
	return !m_carry.empty();
}

// The control side of samples [from, to) of the stretch, each published to the ring as it is done.
void Machine::controlSamples(int from, int to)
{
	const size_t c = m_carry.size();
	const double *values = m_board.values();
	for (int i = from; i < to; ++i) {
		controlSample(m_jobIn, i);
		double *slot = m_ring.data() + size_t(i) * kCarryMax;
		for (size_t k = 0; k < c; ++k)
			slot[k] = values[m_carry[k]];
		m_made.store(i + 1, std::memory_order_release);
	}
}

// The thread's job: the control side from where the caller hands it over to the stretch's end.
void Machine::controlJob()
{
	int from = m_handoff.load(std::memory_order_acquire);
	for (long spins = 0; from < 0; from = m_handoff.load(std::memory_order_acquire))
		if (++spins > (1 << 16))
			std::this_thread::yield();
		else
			Worker::pause();
	controlSamples(from, m_jobN);
}

// The caller runs the control side itself, sample by sample beside the audio path, until it sees
// the thread has woken and claimed the job (a few microseconds, longer on a busy computer), then
// hands it over at the next sample: no time spent waiting for the wake, and a thread that doesn't
// come in time costs nothing.
void Machine::renderSplit(float *out, int n, const float *in, bool noiseWave)
{
	++m_threadStats.split;
	m_board.beginSide();
	m_jobN = n;
	m_jobIn = in;
	m_made.store(0, std::memory_order_relaxed);
	m_handoff.store(-1, std::memory_order_relaxed);
	m_worker->post(&Machine::controlJobOf, this);
	double *v = m_board.sideValues();
	const size_t c = m_carry.size();
	bool handed = false;
	for (int i = 0; i < n; ++i) {
		if (!handed) {
			if (m_worker->claimed()) {
				m_handoff.store(i, std::memory_order_release);
				handed = true;
			} else {
				controlSamples(i, i + 1);
				++m_threadStats.early;
			}
		}
		// the thread's next sample: a note event's spike is a few hundred microseconds
		if (m_made.load(std::memory_order_acquire) <= i) {
			const auto t0 = m_timeWaits ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
			for (long spins = 0; m_made.load(std::memory_order_acquire) <= i; ++spins)
				if (spins > (1 << 16))
					std::this_thread::yield(); // (preempted: let it run)
				else
					Worker::pause();
			if (m_timeWaits)
				m_threadStats.waited += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		}
		const double *slot = m_ring.data() + size_t(i) * kCarryMax;
		for (size_t k = 0; k < c; ++k)
			v[m_carry[k]] = slot[k];
		const float y = audioSample(v, true, noiseWave);
		out[i] = y;
		// as one thread does it (canSplit: sleep can't start within the stretch)
		if (m_awakeFor > 0) {
			--m_awakeFor;
			m_quiet = 0;
			continue;
		}
		if (soundingKey() < 0 && std::abs(y) < 1e-6f)
			++m_quiet;
		else
			m_quiet = 0;
	}
	if (!handed) {
		// the thread never came: the job back, or (claimed since the last look) nothing left for it
		++m_threadStats.takenBack;
		if (!m_worker->cancel())
			m_handoff.store(n, std::memory_order_release);
	}
	// it returns from the job
	for (long spins = 0; !m_worker->idle(); ++spins)
		if (spins > (1 << 16))
			std::this_thread::yield();
		else
			Worker::pause();
	m_board.endSide();
}

} // namespace ms20
