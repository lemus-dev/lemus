#pragma once

#include "midi.h"
#include "oversampler.h"

#include <memory>
#include <vector>

namespace tb303 {

// Front-panel knobs as pot rotations, 0 (fully anticlockwise) to 1.
struct Controls {
	double tuning = 0.5;
	double cutoff = 0.5;
	double resonance = 0.5;
	double envMod = 0.5;
	double decay = 0.5;
	double accent = 0.5;
	double volume = 0.7;
	bool square = false;
};

// Unit-to-unit and calibration constants: the trimmers and the parts whose values the
// schematic leaves open. Defaults are a calibrated unit (docs/circuit.md, docs/model.md).
struct Calibration {
	double vcfRingHz = 500;     // TM3: ring frequency at cutoff centre, res max (service notes 400-667)
	double q21Beta = 300;       // sets the in-loop high-pass of the C25/C27 output coupling (data-sheet typical)
	double q21TailmA = 0.52;    // Q20 tail current: output amp gain and limiting (the bias network, SPICE)
	double vcoScale = 1.0;      // TM5 WIDTH (1.000 = exactly 1 V/oct)
	double tuneCents = 0;       // TM4 offset
	double tuningRangeCents = 700; // TUNING knob end to end /2 (service notes: about a fifth)
	double megPeakV = 10.0;     // MEG output at the top of the attack
	double decayPotOhms = 1e6;  // VR6 DECAY end to end (1M A)
	double megCapUF = 1.0;      // C62, the MEG's hold capacitor (1 uF electrolytic)
	double taperAMid = 0.15;    // CUT OFF and ENV MOD (one part, 50k A): share of the track at mid rotation
	double taperAShape = 0;     // their law: 0 exponential, 1 two straight segments meeting at mid
	double cutoffTaperMid = 0;  // CUT OFF's own share at mid when > 0 (a unit whose CUT OFF pot differs:
	                            // with TM3 holding cutoff centre, it sets how high full CUT OFF reaches)
	double decayTaperMid = 0.15; // DECAY (1M A): the same
	double decayTaperShape = 0;
	double pin1ShiftV = 0;      // the BA662's I_ABC input (pin 1) against Q31's V_EB, from nominal: how deep Q31
	                            // starts each note in saturation, which sets how fast the VCA opens
	double vcaSwing = 1;        // the BA662 output's swing either side of its bias, against the reverse-engineered chip's
	double c13UF = 1.0;         // C13, the accent sweep's capacitor (1 uF electrolytic)
	double vr4bOhms = 50e3;     // VR4-B, RESONANCE's second gang in the accent sweep (50k B)
};

// The unit Lemus plays: the defaults above with the trims and open part values fitted to a real
// TB-303 (Din Sync's reference recordings, 400 notes; tools/compare/dinsync_fit.py, docs/model.md).
inline Calibration unitCalibration()
{
	Calibration c;
	c.vcfRingHz = 400;        // TM3 at a 2.5 ms ring, the slow end of the service tolerance
	c.q21Beta = 385;
	c.megPeakV = 9.3;
	c.decayPotOhms = 860e3;   // within a 1M pot's tolerance
	// The A-taper pots as two straight segments: CUT OFF and ENV MOD 21.5 % of the track at mid
	// rotation, DECAY 14 %.
	c.taperAMid = 0.215;
	c.taperAShape = 1;
	c.decayTaperMid = 0.14;
	c.decayTaperShape = 1;
	// Pin 1 80 mV lower against Q31's V_EB (both within their spreads): the VCA opens within
	// ~1.5 ms of the sound starting, as on Din Sync's and acidvoice.com's units, not over ~8 ms.
	c.pin1ShiftV = -0.08;
	// The VCA's output saturates ~1 V either side of its bias, a fifth of the reverse-engineered
	// chip's swing: both real units flatten their loudest accents so (plain notes don't reach it).
	c.vcaSwing = 0.2;
	// VR4-B, RESONANCE's second gang, 25 % under its 50k (a carbon track's tolerance): the accent
	// sweep charges C13 faster and reaches as high as acidvoice.com's unit's (it was 1.3-2.3
	// semitones short), and Din Sync's accented notes' first 150 ms come closer too.
	c.vr4bOhms = 37.5e3;
	return c;
}

// How high CUT OFF reaches with calibration c: the ladder's ring at full CUT OFF, at rest (no env
// mod, MEG or accent), TM3 set as c calibrates it. The plugin's probe reads the same.
double fullCutOffHz(const Calibration &c);

// The 303's main board from the CPU latch onwards: pitch DAC and slide, VCO and wave
// shaper, VCF, the envelope and accent circuits, VCA and output stage. Runs internally at
// 2, 4 or 8 times the host rate (whichever lands at or above 176.4 kHz) and decimates.
class Voice {
public:
	Voice();
	~Voice();
	void prepare(double hostRate);
	void reset();
	void setControls(const Controls &c) { m_controls = c; }
	const Controls &controls() const { return m_controls; }
	void setCalibration(const Calibration &c);
	const Calibration &calibration() const { return m_cal; }

	// CPU lines. `triggers` increases when a note starts with GATE raised; if it changed
	// without GATE falling in between, the gate is dropped for one control step so the
	// envelopes restart (two MIDI notes touching end to start).
	void setLines(const CpuLines &lines, unsigned triggers);

	// Renders n host-rate samples. Output is the OUTPUT jack voltage / 2.72 V. Given tap buffers
	// (n samples each), also the volts at two check points, one sample per host sample: s1 the
	// WAVEFORM switch's common (the saw at Q28's source, or the square), tp6 the VCF output (AC).
	void render(float *out, int n, float *s1 = nullptr, float *tp6 = nullptr);

	double internalRate() const { return m_rate; }
	double latencySamples() const;

	// Probes for the editor and tests, updated each render.
	struct Probe {
		double cvVolts = 0, vcoHz = 0, vcfHz = 0, meg = 0, veg = 0, gate = 0, accentSweep = 0;
	};
	Probe probe() const;

	struct Impl;

private:
	Controls m_controls;
	Calibration m_cal;
	CpuLines m_lines;
	unsigned m_triggers = 0;
	bool m_retrigger = false;
	bool m_needsReset = false, m_inWarmup = false;
	double m_hostRate = 44100, m_rate = 176400;
	int m_factor = 4;
	Decimator m_decimator;
	std::vector<float> m_work;
	std::unique_ptr<Impl> m_impl;
};

} // namespace tb303
