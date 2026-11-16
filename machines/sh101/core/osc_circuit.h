#pragma once

// The oscillator section's circuit blocks, each from the schematic (docs/circuit-osc.md). OscSection
// (core/osc.h) joins them; tools/spice/osc/blocks.cpp runs them one at a time against SPICE.

#include "lines.h"

#include <cstdint>

namespace sh101::oscm {

// Rails and references (circuit-osc.md 1).
// SN p.7 labels them on the bender connector: pin 19 "+15V" carries the half-filled arrow, pin 21
// "+14V" the filled one.
constexpr double kV15 = 14.87;     // +15 V: IC1b's output, 3 x the bus (half-filled arrow), as measured (Synhouse)
constexpr double kV14 = 14.3;      // +14 V: TR3's emitter, a V_BE below (filled arrow), as measured (U-Labor): bender board, R92, the 4013
constexpr double kVref = 5.0;      // the trimmed +5 V reference bus (Roland's dot-stub)
constexpr double kVT = 0.025693;   // kT/q at 25 °C, as the filter section (tools/spice/*/models.lib TNOM=25)
constexpr double kPi = 3.14159265358979323846;

// Slide pots: the fraction of the track between the wiper and the 0 end, for travel 0..1.
double taperB(double t); // linear (Roland "B")
double taperA(double t); // audio (Roland "A"): 15 % at mid-travel, an exponential law

// A wiper as a Thevenin source: the pot's 0 end on vBot, its top end driven by vTop through
// rSrc, total track rPot, wiper at fraction k of the track from the 0 end.
struct Thevenin {
	double v = 0, r = 0;
};
Thevenin potWiper(double vTop, double rSrc, double vBot, double rPot, double k);
// The current such a source pushes into a node held at vNode through rSeries.
inline double into(const Thevenin &t, double vNode, double rSeries) { return (t.v - vNode) / (rSeries + t.r); }

// IR9022 on a single supply: the output stays about 1.5 V inside each rail (Sharp: 26 Vpp on ±15 V).
double opampClamp(double v, double vNeg, double vPos);

// ---------------------------------------------------------------------------------------------
// Portamento (bender board, circuit-osc.md 3): VR2 2M (A) as a rheostat into C10 0.33 µF and the
// IC2b follower; TR1 (2SK30A) shorts VR2 whenever TR5 conducts. S3: ON leaves TR5 off, OFF holds
// it on through R24, AUTO feeds it the GATE IN differentiator (R15, C11) and PORTA OFF (R16).
class Portamento {
public:
	void prepare(double rate);
	void reset(double volts);
	void set(double knob, PortaMode mode, double tauMax);
	// One sample: the KCV (the CV IN jack, volts), the GATE line and PORTA OFF (P23). Returns the
	// CV line (IC2b's output).
	double tick(double kcv, bool gate, bool portaOff);
	double volts() const { return m_v; }
	bool shorted() const { return m_on; } // TR1 conducting: no glide
	double nodeX() const { return m_vx; } // TR5's base (AUTO), for the SPICE comparison

	static constexpr double kC10 = 0.33e-6, kRpot = 2e6, kRend = 0.005;
	// TR1 2SK30A: I_DSS 1 mA, V_P -1 V (R_on 500 ohm).
	static constexpr double kBeta = 1e-3, kVp = 1.0, kLambda = 0.01;
	static constexpr double kR15 = 47e3, kC11 = 4.7e-9, kR16 = 220e3, kR23 = 100e3, kR24 = 220e3;
	static constexpr double kGateHigh = 12.0, kPortaOffHigh = 5.0;

private:
	double m_dt = 1 / 176400.0;
	double m_v = 0;          // C10
	double m_vc11 = 0;       // C11: gate side minus node X
	double m_vx = 0;         // node X / TR5's base
	double m_rPot = kRpot * kRend;
	double m_tauScale = 1;
	PortaMode m_mode = PortaMode::Off;
	bool m_on = true;
	double m_kOff = 1;     // per-sample RC coefficient with TR1 off
	int m_subSteps = 1;    // explicit steps per sample with TR1 on
	bool m_tr5 = false, m_lastGate = false, m_lastPortaOff = false;
	int m_busy = 0;        // samples left to solve node X after an edge
	void coefficients();
};

// ---------------------------------------------------------------------------------------------
// Bender board (circuit-osc.md 6): the lever buffer IC3b and its depth pots, the grip's LFO MOD
// (TR2 sine shaper, IC2a, the TR3 tail with its R22/C9 fade-in, TR6 to stop it), and IC3a, which
// sums VCO MOD IN, LFO MOD and the VCO bend for the main board's IC12b.
class Bender {
public:
	void prepare(double rate);
	void reset();
	void set(const Controls &c, const Performance &p, double leverVolts);
	struct Out {
		double ic3a = kVref;   // IC3a's output: "VCO MOD" to R91
		Thevenin vcfBend;      // VR5's wiper: "VCF BEND" (absolute)
		Thevenin vcfLfo;       // VR6's wiper: "VCF LFO" (absolute)
		double ic2a = kVref;   // the grip's sine (IC2a's output)
		double tail = 0;       // TR3's collector current (A)
	};
	// One sample: the LFO triangle (IC3a on the LFO board, absolute volts) and the VCO MOD
	// slider's wiper (VCO MOD IN).
	Out tick(double triVolts, const Thevenin &vcoModIn);

	static constexpr double kR22 = 1e6, kC9 = 0.47e-6, kR25 = 68e3, kR27 = 100, kBeta = 250;
	static constexpr double kR13 = 100e3, kR7 = 2.2e3, kR12 = 22e3, kR10 = 68e3, kR8 = 22e3, kR9 = 470e3;
	static constexpr double kR1 = 150e3, kR2 = 150e3, kR3 = 100e3, kR4 = 100e3;

private:
	double m_dt = 1 / 176400.0;
	double m_kRelease = 1;  // C9 emptying through R27, per sample
	double m_v9 = 0;        // C9
	double m_lever = 0;     // IC3b's deviation from the bus
	bool m_modOn = false;   // the MOD switch closed (TR6 off)
	double m_kBendVco = 0, m_kBendVcf = 0, m_kLfo = 0;
	double m_vr3 = 0;       // LFO MOD OFFSET trim: VR3's wiper (V), nulls IC2a with the MOD released
};

// IC12b (main board): inverting around the bus, R89 100k ∥ C35 680p, from IC3a through R91 100k
// and the panel TUNE through R90 4.7M. tick() returns its output (absolute volts).
class PitchMixer {
public:
	void prepare(double rate);
	void reset();
	double tick(double ic3a, const Thevenin &tune);
	static constexpr double kR89 = 100e3, kC35 = 680e-12, kR91 = 100e3, kR90 = 4.7e6;

private:
	double m_k = 1, m_y = 0; // the deviation from the bus, through the 2.34 kHz pole
};

// ---------------------------------------------------------------------------------------------
// CEM3340 (circuit-osc.md 4): the current into pin 15 and the oscillation frequency.
struct Cem3340 {
	static constexpr double kRT = 5.6e3, kRZ = 28e3, kRref = 1.5e6, kCF = 1e-9, kVCC = kV15;
	static constexpr double kR104 = 100e3, kR103 = 100e3, kR99 = 97.6e3, kR108 = 470e3, kR100 = 1.5e6, kR199 = 10e3;
	double rS = 1890;        // R106 + VR6
	double rRange = 100e3;   // R99 + VR5
	double vVr7 = -1.0;      // VR7's wiper (0 .. -5 V)
	double tDelay = 1.1e-6;  // comparator delay per period (the "4 t_d" of the triangle turns)

	// The generator's frequency f = 3 I_EG / (2 VCC CF) for a pin-15 current; I_C < 0 acts as 0.
	double idealHz(double ic) const;
	mutable double m_icLast = -1, m_rsLast = -1, m_hzLast = 0; // idealHz's last inputs and result
	// The actual frequency, with the comparator delay.
	double hz(double ic) const;
	// The pin-15 current from the CV line, the RANGE S/H, IC12b and the HF-track feedback (from the
	// last generator frequency).
	double current(double vCv, double vRange, double vIc12b, double lastIdealHz) const;
	// The CEM3340's own generator current for a frequency, and its pin-7 feedback through R199/R100.
	static double hfTrack(double idealHz);
};

// ---------------------------------------------------------------------------------------------
// The PWM network (circuit-osc.md 4.4): S4 picks LFO (the triangle through R176 82k, plus R177
// 150k from +14 V), MAN (R178 120k from +14 V; SN p.7's filled arrow) or ENV (R190 18k) for the
// top of VR12 100k (B),
// whose other end is the bus; TR25 follows the wiper into R96 100k / R94 100k + VR4 / C37.
struct PwmNetwork {
	static constexpr double kR176 = 82e3, kR177 = 150e3, kR178 = 120e3, kR190 = 18e3, kR96 = 100e3, kR94 = 100e3;
	static constexpr double kC37 = 10e-9;
	double vr4 = 28.2e3;  // trimmed so PULSE WIDTH 0 gives a square
	// Pin 5's voltage (without C37) for a source, slider position and the triangle / ENV volts.
	double pin5(PwmSource src, double slider, double triVolts, double envVolts);
	// TR25's base-emitter drop at its emitter current.
	static double vbe(double ie);
	// The VR4 setting that gives pin 5 = vHalf at PULSE WIDTH 0 (the service notes' 1:1 trim).
	static double trimVr4(double vHalf);
	double tau() const; // C37 with what drives it

private:
	double m_vbe = 0.55;
	// pin5()'s last inputs, the TR25 V_BE it started from and its result: repeated while that V_BE
	// is a fixed point.
	PwmSource m_src = PwmSource::Manual;
	double m_slider = -1, m_in = 0, m_vr4 = -1, m_vbeIn = -1, m_vbeOut = -2, m_out = 0;
};

// ---------------------------------------------------------------------------------------------
// The LFO-board modulator as the waveform switch S2 and IC18a hand it on (circuit-osc.md 5.3):
// absolute volts at IC18a's output.
struct ModSwitch {
	// R169 and R188 are 100k on real boards; the service notes print 47k. R31 and R35, blank in the
	// service notes, are 100k as on Din Sync's dismantled unit, which a real unit's top LFO rate
	// supports (circuit-osc.md 5.3).
	static constexpr double kR170 = 47e3, kR175 = 33e3, kR188 = 100e3, kR31 = 100e3, kR35 = 100e3, kR169 = 100e3;
	static constexpr double kR32 = 220e3, kR172 = 1e3, kC60 = 10e-9, kR173 = 100e3, kR174 = 2.2e6, kC61 = 10e-6;
	static constexpr double kVsat = 0.02; // TR5/TR6 saturated (SPICE: 0.01-0.03 V)
	// The Schmitt's thresholds: the low one is TR5's saturation, the high one the TR5/TR6 node
	// with both off, which depends on what S2 hangs on R31 (pin 11).
	static double thresholdLow() { return kVsat; }
	static double thresholdHigh(LfoWave w);
	static double triangle(double tri) { return (tri * kR188 + kVref * kR170) / (kR170 + kR188); }
	static double square(bool clk);
	static double random(int code); // the RANDOM S/H (code / 12 V) doubled by IC11a, through R175
};

// ---------------------------------------------------------------------------------------------
// The noise generator (circuit-osc.md 4.6): TR23's junction noise, TR22, the C31 pole and IC12a
// (gain 23 with R86 220k ∥ C32 240p over R88 10k). Absolute volts at IC12a's output.
class Noise {
public:
	void prepare(double rate);
	void reset();
	void set(double rmsVolts, double cornerHz);
	double tick();
	static constexpr double kR86 = 220e3, kC32 = 240e-12, kR88 = 10e3;

private:
	double m_rate = 176400;
	uint64_t m_s[2] = {0x9e3779b97f4a7c15ull, 0xbf58476d1ce4e5b9ull};
	double m_sigma = 0;
	double m_lp = 0, m_a1 = 0;          // C31 pole
	double m_sh = 0, m_b0 = 0, m_b1 = 0, m_a = 0, m_x1 = 0; // IC12a's shelf
	double m_cornerHz = 900, m_rms = 1;
	bool m_haveSpare = false;
	double m_spare = 0;
	double gauss();
	double uniform();
};

// ---------------------------------------------------------------------------------------------
// The VCO's waveforms, band-limited: the CEM3340's phase, the saw (pin 8), the SH-101 pulse
// (TR24's collector) and the 4013 sub-oscillator, with a 4-point B-spline polyBLEP at every
// edge. Outputs lag the phase by two samples.
class Blep {
public:
	void reset(double level);
	// Adds a step of height h at fraction `frac` (0..1] of the interval ending at the sample about
	// to be pushed.
	void step(double h, double frac);
	// Pushes the naive value of the current sample; returns the band-limited one, two samples late.
	double push(double naive);
	static double residual(double x); // B(x) - H(x) for the B-spline kernel, x in samples

private:
	double m_buf[4] = {0, 0, 0, 0};
	int m_pos = 0;
};

class Vco {
public:
	struct Edge {
		enum Kind : uint8_t { Wrap, Rise, Fall } ;
		double frac; // where in the last interval (0..1]
		Kind kind;
	};
	void prepare(double rate);
	void reset();
	// One sample at frequency hz; the SH-101 pulse is high while the phase is at or above
	// pwPhase (the chip's pulse is high while saw/2 < pin 5, and TR24 inverts it).
	void tick(double hz, double pwPhase);
	double phase() const { return m_phase; }
	// The edges of the last tick, in time order: the saw's wrap and the pulse's rises and falls.
	int edges() const { return m_nEdges; }
	const Edge &edge(int i) const { return m_edges[i]; }
	double wrapFrac() const { return m_wrapFrac; } // 0: no wrap in the last tick
	bool pulseHigh() const { return m_pulse; }
	bool q1() const { return m_q1; }
	bool q2() const { return m_q2; }
	bool q1Changed() const { return m_q1Changed; }
	bool q2Changed() const { return m_q2Changed; }

private:
	static constexpr int kMaxEdges = 4;
	double m_rate = 176400;
	double m_phase = 0, m_lastPw = 0.5;
	double m_wrapFrac = 0;
	Edge m_edges[kMaxEdges] = {};
	int m_nEdges = 0;
	bool m_pulse = false, m_q1 = false, m_q2 = false, m_q1Changed = false, m_q2Changed = false;
	void addEdge(double frac, Edge::Kind k);
};

// The sub-oscillator's outputs at the S5 contacts (circuit-osc.md 4.5): the 4013 (on +14 V) Q outputs
// through R181/R182 47k, the 25 % pulse through the D38/D39 OR, all loaded by VR15 100k to ground.
struct SubLevels {
	// Thevenin source at the top of VR15 for a mode and flip-flop state.
	static Thevenin top(SubMode mode, bool q1, bool q2);
};

// The pulse at the top of VR13: TR24 saturated, or R92 68k from +14 V.
struct PulseStage {
	static constexpr double kR92 = 68e3, kVsat = 0.01;
	static Thevenin top(bool high);
};

} // namespace sh101::oscm
