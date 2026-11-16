// The QuadraVerb GT's analog preamp (sheet 1, docs/circuit.md), at 4 times the DSP's rate: the
// netlists in core/netlists/ (input, overdrive, shaper, gateout) on the circuit solver, joined by
// the SSM2120 (core/ssm2120.h) and the networks around its control ports, set by the latches the
// firmware writes (docs/firmware.md, "The preamp's controls").
#pragma once

#include "circuit.h"
#include "noise.h"
#include "ssm2120.h"

#include <algorithm>
#include <cstdint>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace qvgt {

// The preamp's steps per DSP sample (166,667 Hz): a third less CPU than 8, the top octave of the
// distorted programs 0.2-0.6 dB under a 16x reference where 8 is 0.05-0.14 (docs/circuit.md,
// "The rate"); chosen by ear, 2026-11-12.
constexpr int kPreampOver = 4;

// A netlist run as its partitions (its .part lines, circuit.h), each its own circuit, in the
// order listed, the later ones taking the earlier ones' nodes as inputs; one circuit if it has
// none. Inputs, outputs and controls by the netlist's names. setOver(N, K): the first K partitions
// (all: 0) take N steps a step, all K in turn at each (joined as at one), the inputs moving
// linearly from the last step's values, the noise inputs held; the rest then take one, from the
// first K's outputs at the step's end.
class Chain {
public:
	void load(const std::string &name, const std::string &text);
	// The op-amps' poles by the trapezoidal rule (circuit.h), backward Euler if not; before setRate.
	void setPoleTrap(bool on)
	{
		for (auto &c : m_stages)
			c->setPoleTrap(on);
	}
	void setOver(int n, int stages = 0) { m_over = std::max(1, n); m_overStages = stages; } // before setRate
	int over() const { return m_over; }
	void setRate(double rate);
	void solveDc();
	void step();
	int input(const std::string &name) const;   // -1: none
	int output(const std::string &name) const;  // -1: none
	int control(const std::string &name) const; // -1: none
	void setInput(int i, double v);
	// Where input i is kept in each stage that takes it (valid until load()).
	std::vector<double *> inputSlots(int i);
	double output(int o) const { const auto &r = m_outs[size_t(o)]; return m_stages[size_t(r.first)]->output(r.second); }
	void setControl(int c, double v);
	int inputs() const { return int(m_inNames.size()); }
	const std::string &inputName(int i) const { return m_inNames[size_t(i)]; }
	const std::string &name() const { return m_name; }
	int stages() const { return int(m_stages.size()); }
	circuit::Circuit &stage(int i) { return *m_stages[size_t(i)]; }

private:
	struct Link {
		int input, from, output; // the stage's input index, the stage it comes from, that stage's output
	};
	std::string m_name;
	std::vector<std::unique_ptr<circuit::Circuit>> m_stages;
	std::vector<std::vector<Link>> m_links;                    // per stage
	int m_over = 1, m_overStages = 0;
	size_t overStages() const { return m_overStages > 0 ? std::min(size_t(m_overStages), m_stages.size()) : m_stages.size(); }
	std::vector<double> m_last, m_want; // over > 1: the inputs at the last step, this step's
	std::vector<char> m_held;           // the noise inputs: held through the step
	void stepStages(size_t from, size_t to);
	std::vector<std::string> m_inNames, m_outNames, m_ctlNames;
	std::vector<std::vector<std::pair<int, int>>> m_ins, m_ctls; // per name: (stage, index)
	std::vector<std::pair<int, int>> m_outs;                     // per name: (stage, index)
};

class Preamp {
public:
	Preamp();

	// The rate it runs at (kPreampOver times the DSP's). Rebuilds; settles the circuits.
	void prepare(double rate);
	void reset();

	// The parts' own noise (HISS): each op-amp's input voltage noise (TL084, 18 nV/sqrt(Hz)) and the
	// SSM2120 VCAs' (19.6 pA/sqrt(Hz) at unity into the data sheet's 36K, rising 0.475 dB a dB of
	// gain, its Figure 1b). Off: noiseless.
	void setHiss(bool on) { m_hiss = on; }

	// A plug in the effects return (J8): without one, J8's switch holds the loop out. The plugin
	// has no return, so none.
	void setReturnPlugged(bool on) { m_returnPlugged = on; m_dirty = true; }

	// The latches: U10 (0C00h), U11 (0A00h), 0400h (the gate's threshold DAC), and the INPUT pot's
	// rotation 0-1 (the dual 10KB). Applied when they change.
	void setControls(uint8_t u10, uint8_t u11, uint8_t l0400, double inputPot);

	struct Out {
		double toAdc = 0;    // U13 pin 8: to the A/D chain (sheet 2)
		double dryL = 0, dryR = 0; // U2's stage outputs: the dry path (to U12)
		double envelope = 0; // sheet 2's U16 and D15: U2 pin 8's positive peaks, R145 330K, C52 0.1u
		                     // (the headroom LEDs and the level detector)
		double send = 0;     // J7, the effects send
	};
	// One step: the jacks' volts in.
	Out step(double inL, double inR, double fxReturn = 0);

	// Probes (tests): the compressor's and the gate's control outputs and VCA gains in dB.
	double conout1() const { return m_conout1; }
	double p8() const { return m_p8; }
	double conout2() const { return m_conout2; }
	// Tools: every circuit's inputs and outputs at the last step, and a call at the end of each.
	struct Probe {
		double inL, inR, p8, dryL, dryR; // the input circuit
		double i1, od8;                  // the overdrive's: VCA 1's current in, U3 pin 8 out
		double conout1, pre;             // the shaper's other input, its output
		double i2, out, send;            // after the gate: VCA 2's current in, U13 pin 8, J7
	};
	const Probe &probe() const { return m_probe; }
	double inputPot() const { return m_inputPot; }
	void setTap(std::function<void(const Preamp &)> tap) { m_tap = std::move(tap); }
	double vca1Db() const;
	double vca2Db() const;
	// The four netlists (input, overdrive, shaper, gateout), for their solver statistics.
	Chain &chain(int i) { return i == 0 ? m_input : i == 1 ? m_od : i == 2 ? m_shaper : m_gate; }

private:
	void applyControls();
	double compressorVcMinus(double conout) const;

	Chain m_input, m_od, m_shaper, m_gate;
	int m_inLeft = -1, m_inRight = -1, m_outP8 = -1, m_outDryL = -1, m_outDryR = -1, m_ctlInput = -1;
	int m_odIn = -1, m_odOut = -1, m_ctlOd = -1;
	int m_shIn = -1, m_shCon = -1, m_shOut = -1;
	int m_ctlDist = -1, m_ctlCh[8]{}, m_ctlPresence = -1, m_ctlFlat = -1, m_ctlCab = -1, m_ctlCab2 = -1, m_ctlBass = -1;
	int m_gtIn = -1, m_gtRet = -1, m_gtOut = -1, m_gtSend = -1, m_ctlLoop = -1;

	double m_rate = 0, m_dt = 0;
	SeriesRc m_vca1In, m_det1In, m_det2In, m_vca2In;
	Ssm2120::Detector m_det1, m_det2;
	double m_vcaIref = 0;

	// from the latches
	uint8_t m_u10 = 0, m_u11 = 0, m_l0400 = 0x1f;
	double m_inputPot = 1;
	bool m_dirty = true;
	bool m_returnPlugged = false;
	int m_compChannel = 0;   // U14's channel (0: off)
	double m_vcPlus1 = 0;    // the overdrive DAC and U3C's offset at VCA 1's +Vc
	double m_threshold = 0;  // U39's output (the gate threshold DAC)

	double m_conout1 = 0, m_conout2 = 0, m_vcMinus1 = 0, m_vcPlus2 = 0;
	double m_d11 = 0;        // the last D11 node voltage (Newton's start)
	double m_env = 0, m_envDecay = 0, m_p8 = 0;
	Probe m_probe{};
	std::function<void(const Preamp &)> m_tap;

	// noise: each chain's noise inputs, the op-amps' and the VCAs' per-step sigma
	bool m_hiss = true;
	Noise m_noise;
	std::vector<std::vector<double *>> m_noiseSlots; // per noise input, in order: where it goes
	double m_opampSigma = 0, m_vcaSigma = 0;
	void stepNoise();
};

} // namespace qvgt
