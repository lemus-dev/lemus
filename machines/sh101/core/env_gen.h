#pragma once

namespace sh101 {

// The SH-101's discrete ENV GEN (docs/circuit-filter.md 5), all on the +14 V rail (TR3's emitter):
// C59 4.7 uF charged by TR34 from +14 V through R141 and the ATTACK pot until IC16a sees the
// threshold (R158 82k against
// SUSTAIN's 100k to the bus), then discharged through R164 and the DECAY pot to the SUSTAIN
// wiper (TR37/TR36 follower) and, with the gate off, through R148, the RELEASE pot and TR31 to
// the bus (R163, C58). TR32 resets it (release on, attack off, IC16a's latch cleared through
// D33) while the gate is off, during a TRIG pulse in GATE+TRIG, and while the LFO square holds
// it in LFO mode. IC16b buffers C59 as the ENV output. Also the GATE level for the VCA's GATE
// mode (TR30, R142, R41, C57).
//
// Voltages are absolute (against ground). The ENV output rests at the +5 V bus and peaks at the
// threshold, 10.11 V.
class EnvGen {
public:
	struct Params {
		double v14 = 14.3;      // the +14 V rail: TR3's emitter (14.3 V measured, U-Labor), SN p.7's filled arrow
		double vRef = 5.0;      // the +5 V reference bus
		double potOhms = 1e6;   // VR22, VR23, VR25 (1M A); real pots are +-20 %
		double potEndOhms = 200; // the pots' residual resistance at zero
		double potMid = 0.15;    // their A law's share of the track at mid-travel (Japanese A: 10-20 %)
		double c59 = 4.7e-6;
		double vceSat = 0.006;  // TR34, TR35, TR31, TR30 switched on (plus 8 ohm)
	};
	enum class Mode { GateTrig, Gate, Lfo };

	void setParams(const Params &p);
	void prepare(double fs);
	void reset();

	// The panel: slider travel 0-1, S7 mode.
	void setControls(double attack, double decay, double sustain, double release, Mode mode);

	// One sample. gate: the CPU's GATE (TR29). kcvVolts: the KCV sample-and-hold, for the TRIG
	// detector. lfoSquare: the MODULATOR's square (S7 deck 2 in the LFO position).
	double tick(bool gate, double kcvVolts, bool lfoSquare);
	// The current the VCA's GATE mode draws from C57's node (R133 into TR28), amps.
	void setGateLoad(double amps) { m_gateLoad = amps; }

	double env() const { return m_v; }      // IC16b pin 7
	double gateLevel() const { return m_v57; } // C57's node (pin 83): the VCA's GATE mode
	double threshold() const { return m_vth; }
	double sustainLevel() const { return m_vs; }
	bool attacking() const { return m_attack; }
	bool latched() const { return m_latch; }
	bool resetting() const { return m_reset; }

	// The pot law for a slider travel (1M A taper, 15 % at mid travel).
	static double taperA(double x);
	// An A law with `mid` of the track at mid-travel, as an exponential curve.
	static double taperA(double x, double mid);

private:
	Params m_p;
	double m_fs = 176400, m_t = 1 / 176400.0;
	double m_a = 0, m_d = 0.3, m_r = 0.2;
	double m_ra = 0, m_rd = 0, m_rr = 0, m_s = 0;
	Mode m_mode = Mode::GateTrig;
	double m_vth = 10.11, m_vs = 5;
	// State.
	double m_v = 5, m_v58 = 5, m_v57 = 5;
	bool m_latch = false, m_attack = false, m_reset = true;
	double m_trigHp = 0, m_kcvLast = 0, m_trigAmp = 0;
	bool m_kcvInit = false;
	double m_gateLoad = 0;
	double m_i = 0; // the active path's current
	// Per-sample factors that only change with the controls, the parts or the rate (setControls).
	double m_kTrigHp = 0, m_kTrigAmp = 0, m_kAttack = 0, m_kDecay = 0, m_k58 = 0, m_k57On = 0, m_k57Off = 0;
	double m_vbe37 = 0, m_v57On = 0, m_r57On = 0;
	double m_ib35 = 0, m_i159 = 0; // TR35's base junction current and R159's, in the decay (setControls)
};

} // namespace sh101
