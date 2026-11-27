// The Bass Station Rack's analog voice (docs/circuit.md), driven by what the CPU board hands it each
// host sample (core/voice_io.h), run at twice the host rate:
//   - the CV path: the serial DAC's eight sample-and-holds and their buffers, each a 12-bit code
//     to a voltage and a first-order settle;
//   - the exponential converters (FMA1: two DTA124E PNPs with their 22k/22k resistors), solved per
//     sample as their DC law (they settle in microseconds);
//   - the DCOs: the ramp capacitor charged by its converter's current and emptied at each reset
//     edge, U18 as amplifier (saw) or comparator against the PWM CV (pulse), band-limited by polyBLEP;
//   - the mixer OTAs (U19, U21: I_abc tanh(v / 2V_T)) into OSC_MIX;
//   - the filter's two sections on the solver (core/netlists/vcf1.cir, vcf2.cir), the 4053's slope
//     select;
//   - the VCA OTA (U36) into U22C, C38, the VOLUME pot.
// Values marked [O] come from the OSCar's sheet, [F] are fitted (no value on the trace); each waits
// on a readable board photo (docs/circuit.md).
#pragma once

#include "circuit.h"
#include "oversampler.h"
#include "voice_io.h"

#include <array>
#include <memory>

namespace bsrack {

// The circuit's values the voice computes outside the netlists (the netlists hold their own).
struct VoiceParts {
	// rails (relative to AGND) [F]
	double vss = -4.0, vdd = 4.0;
	// the DAC: 12 bits, full scale [F] 5 V above VSS (the logic rail)
	double dacFull = 5.0;
	double cvTau = 0.40e-3;    // each S&H and its buffers' settle [F]
	double volTau = 4.0e-3;    // A_VOL/B_VOL from the on-chip registers [F: PWM or DAC, filtered]
	// the FMA1 (DTA124E pair): R1 22k, R2/R1 1.0 (Rohm data sheet), the internal PNPs' beta [F]
	double fmaR1 = 22e3, fmaR2 = 22e3, fmaBeta = 952; // beta: fitted to the demo with the VCF anchored (rounds 2, 3)
	// DCO converters: CV divider R48 (from the CV) / R47 (to AGND); reference R46 to VSS [F]
	double dcoRin = 33e3, dcoRgnd = 2.0e3, dcoRref = 820e3;
	double dcoCap = 47e-9; // C10, C41 [F]
	// U18: gain 1 + R32/R31 (saw), threshold R31/(R31+R32) of the PWM CV (pulse) [F]
	// R32 [F]: the pulse at 59 % (MAN, left end), 55 % (code 800), 20 % (E00) at note 60, as the
	// wavefo and pulsewisth clips show (round 2)
	double r31 = 10e3, r32 = 16.9e3;
	double oscHigh = 6.7, oscLow = 0.05; // U18's output swing above VSS (TLC272 at 8 V) [D]
	// mixer: R33/R34 input divider [O: the OSCar's 33k/330], R35/R28 bias from A/B_VOL_CV [F]
	double r33 = 33e3, r34 = 207, r35 = 44.1e3; // r34, r35 fitted (rounds 1-3)
	double abcDrop = 0.55; // an OTA bias input's diode to VSS at tens of uA [D]
	// FILTER INPUT into OSC_MIX through [F] 10k, its level from the MIX pot's analog side [F]
	double rExt = 10e3, extVolts = 1.0; // a full-scale sample is 1 V
	// VCF converter: R39 (from VCF_CV_LIN) / R38 (to AGND), reference R37 [F]; its current split
	// four ways into U26, U27, U31, U32's bias (R59, R60, R71, R72). Divider and reference from two
	// anchors (tools/compare/fitdemo.py --anchor full, round 2): code 000 rings at 13.0 kHz
	// (fiolter-4poles), and the firmware's 24 dB key tracking (32 codes a semitone) is full tracking.
	double vcfRin = 33e3, vcfRgnd = 2.98e3, vcfRref = 603e3;
	// [F] a current under the converter's: the OTAs' bias never quite off, the cutoff never under about
	// 45 Hz (fitted to the factory programs and the demo, accuracy round 4)
	double vcfFloor = 6e-6;
	// RES_CV into U28/U33's bias through R58/R70 [F]; resFloor [F]: the bias left at full RESONANCE
	// (code 000), where RES_CV sits at the OTA's bias diode
	double r58 = 15e3, resFloor = 0;
	// C33, C37 across the sections' feedback R57, R69 [F]: the resonance's damping rising with
	// frequency (core/netlists/vcf1.cir); the netlists' own value unless set
	double c33 = 0;
	// VCA converter: R42 / R41, reference R40 [F]; R79 bleeds the bias node to VSS (closes it), C60
	// settles it [F]
	double vcaRin = 33e3, vcaRgnd = 2.7e3, vcaRref = 7.15e6, r79 = 1.47e6, vcaTau = 0.5e-3; // fitted (rounds 1-3)
	// U36's input: R73 / R74 [F], U22C's feedback R80 [F]
	double r73 = 33e3, r74 = 2.69e3, r80 = 10e3; // r74 fitted (rounds 1-3)
	double outVolts = 2.0; // OUTPUT volts for a full-scale sample [F]
	double couplingHz = 7.0; // C38 2.2 uF into the load [F]
};

class Voice {
public:
	Voice();
	~Voice();
	void prepare(double rate);
	// One host sample of OUTPUT. filterIn: FILTER INPUT's sample (0 with nothing plugged in);
	// volumePot: the VOLUME pot's position 0..1 (analog, after the VCA; the firmware doesn't read it).
	float process(const VoiceFrame &f, float filterIn, double volumePot);
	// The VCA has been closed and the output below -120 dBFS for 100 ms: the voice may be skipped
	// (the plugin then outputs exact zeros) until the VCA CV moves.
	bool settled() const { return m_quiet >= m_quietLimit; }
	// Called when the machine resumes the voice after skipping it: the quiet count starts again.
	void wake() { m_quiet = 0; }
	// A session restored (Machine::restore): the S&H buffers at their codes at once, the filter from
	// rest, as at a load.
	void restart()
	{
		m_started = false;
		m_quiet = 0;
	}
	// Samples of delay the decimator adds (host rate).
	double latency() const { return m_decim.latency(); }

	const VoiceParts &parts() const { return m_p; }
	// The circuit's values, for the fitting tools; prepare() again after changing them.
	void setParts(const VoiceParts &p) { m_p = p; }
	// Solver health, for the corners tool: unconverged samples since the last call.
	int takeUnconverged();

	// The exponential converter's collector current for the pin voltage vp (the CV divider's
	// output) and the reference current iref: the FMA1's two PNPs with R1, R2 and base current.
	static double expoCurrent(double vp, double iref, const VoiceParts &p);

private:
	struct Dco {
		double v = 0;          // the ramp capacitor, volts above VSS
		double pendingBlep = 0; // polyBLEP correction carried into the next sample
		bool high = false;      // the comparator's state (pulse)
	};
	double runDco(Dco &d, double current, const float *edges, int nEdges, double edgeScale, double edgeOffset, bool pulse,
	              double threshold);
	double dacVolts(int code) const { return m_p.vss + m_p.dacFull * code / 4096.0; }

	VoiceParts m_p;
	double m_rate = 48000, m_runRate = 96000, m_dt = 1.0 / 96000;
	std::array<double, kCvChannels> m_cv{}; // the S&H buffers' outputs, volts
	double m_volA = 0, m_volB = 0;          // A/B_VOL_CV, volts
	double m_cvCoef = 0, m_volCoef = 0, m_vcaCoef = 0, m_hpCoef = 0;
	double m_vcaAbc = 0, m_hp = 0, m_hpIn = 0;
	Dco m_a, m_b;
	std::unique_ptr<circuit::Circuit> m_s1, m_s2;
	int m_s1Mix = 0, m_s1A1 = 0, m_s1A2 = 0, m_s1Ar = 0, m_s1Out = 0;
	int m_s2In = 0, m_s2A1 = 0, m_s2A2 = 0, m_s2Ar = 0, m_s2Out = 0;
	Decimator m_decim;
	int m_quiet = 0, m_quietLimit = 4800;
	bool m_started = false;
};

} // namespace bsrack
