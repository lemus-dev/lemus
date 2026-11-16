#pragma once

#include "lines.h"

#include <string>

namespace sh101 {

// One host-rate sample of the modulator (core/lfo.h), for the voice.
struct ModFrame {
	float tri = 0;       // the triangle (IC3a's output) relative to the +5 V bus: about -4.9..+5.3 V
	bool square = false; // CLK: high while the Schmitt's output is high (or CLK RST holds it)
};

// The oscillator section's trims and the parts the schematic leaves open (docs/model-osc.md).
struct OscCalibration {
	double vcoTuneHz = 442;    // VR7 VCO TUNE: the VCO at KCV 4.75 V, RANGE 0, TUNE centred (service notes: 442 Hz)
	double vcoScale = 1.0;     // VR6 VCO WIDTH: octaves per volt at the 100k inputs (1.000 = trimmed)
	double rangeScale = 1.0;   // VR5 RANGE WIDTH: the RANGE input's weight against KCV (1.000 = trimmed)
	double vcoDelayUs = 1.1;   // CEM3340 comparator delay per period (the HF-track trim's target)
	double pwFullVolts = 5.0;  // CEM3340 pin 5 for 100 % chip pulse width (data sheet 4.6-5.4 V)
	double portaTauSec = 0.66; // portamento time constant at full knob (2 MΩ x 0.33 µF)
	double leverVolts = 1.48;  // the bender lever's full deflection at IC3b (a real unit: +14.7 st at VCO 9)
	double noiseRms = 1.05;    // IC12a's noise, V RMS (no schematic value; the Real unit calibration has its own)
	double noiseCornerHz = 1150; // C31's pole with TR22's output resistance: three units' 1130-1200 Hz (r_d ~1.5k)

	static OscCalibration serviceNotes();
	static OscCalibration realUnit();
	static OscCalibration roland();
	bool set(const std::string &name, double value);
};

// What the oscillator section hands the filter section, once per internal sample.
struct OscFrame {
	// The source mixer: each source's voltage at the far end of its mixing resistor (after its
	// level slider), and that resistor, in the order saw, pulse, sub, noise. The filter section
	// sums them into the IR3109's first stage, whose node it owns.
	struct Source {
		double volts = 0;
		double ohms = 1e12;
	};
	// Volts are absolute (to ground), as at the slider wipers; C45 takes out the DC.
	Source mix[4];
	// The MODULATOR waveform the switch selects: IC18a's output, which feeds the VCO MOD and VCF
	// MOD sliders, relative to the +5 V bus (the sliders' other end).
	double modVolts = 0;
	// The CV line after portamento (IC2b's output, absolute volts): it drives VR8 VCF WIDTH and
	// the KYBD slider, whose divider the filter section owns.
	double kybdVolts = 0;
	bool lfoSquare = false; // the MODULATOR's square (= ModFrame::square), for the ENV's LFO trigger
	// The bender board's two outputs to the VCF summer, as Thevenin sources relative to the bus:
	// VCF BEND (VR5's wiper, to R185) and VCF LFO (the grip's LFO MOD through VR6, to R184).
	Source vcfBend, vcfLfo;
};

// The synth board from the S/H outputs to the source mixer: portamento and the pitch summer
// (KCV, RANGE, TUNE, bender, MOD), the CEM3340 VCO with its saw, pulse and PWM, the 4013
// sub-oscillator, the noise source and the mixer sliders, plus the MODULATOR's waveform switch.
// Runs at the voice's internal (oversampled) rate.
class OscSection {
public:
	OscSection();
	~OscSection();
	void prepare(double internalRate);
	void reset();
	void setControls(const Controls &c);
	void setCalibration(const OscCalibration &c);
	void setPerformance(const Performance &p);
	void setLines(const CpuLines &lines);

	// One internal sample. mod: the modulator at this instant; envVolts: the ENV output (for the
	// PWM switch's ENV position), from the filter section's last sample.
	OscFrame tick(const ModFrame &mod, double envVolts);

	double vcoHz() const;      // probe
	double pitchVolts() const; // probe: the pitch CV after portamento, KCV + RANGE scale

	struct Impl;

private:
	Impl *m;
};

} // namespace sh101
