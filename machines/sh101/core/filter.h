#pragma once

#include "lines.h"
#include "osc.h"

#include <string>

namespace sh101 {

// The filter section's trims and the parts the schematic leaves open (docs/model-filter.md 6).
struct FilterCalibration {
	// IR3109 expo converter: control scale at pin 9 and the OTAs' current with pin 9 at the bus.
	double vcfMvPerOct = 19.0; // AS3109 data sheet -19 mV/oct (-17.5 to -20.5); OML measured 20.8
	double vcfI0uA = 2.4;      // AS3109: 250 Hz per stage at V9 = 0 (68k/68k/560, 240 pF)
	double th1Ohms = 1000;     // TH1 SDT1000 at 25 C (the pin-9 divider)
	double vr8Ohms = 0;        // VR8 VCF WIDTH; 0 = trimmed as the service notes do (F4/F5, 1 oct/oct)
	// The +15 V rail at FREQ's top and R187: IC1b's output (SN p.7's half-filled arrow), 3 x the bus,
	// 14.87 V measured (Synhouse). The ENV, KYBD's TR42, R196 and the VCF's TR26/TR27 are on +14 V.
	double v15Rail = 14.87;
	// ENV
	double envPotScale = 1.0;  // VR22/23/25 against their 1 Mohm (+-20 %)
	double envPotMid = 0.15;   // their A law's share of the track at mid-travel (Japanese A: 10-20 %)
	// The ENV's weight at the cutoff summer against the schematic (R189 100k, the ENV's 5.11 V
	// swing on the +14 V rail): 1 = as drawn. As drawn, with KYBD trimmed to 1 oct/oct, ENV 10
	// can't sweep less than 7.7 octaves; a real unit sweeps 7.0-7.3 and Roland's plug-in 7.65
	// (docs/model.md). The cause of the rest is not identified, so this stands in for it.
	double envCvScale = 1.0;
	// VCA
	double vcaOffsetMv = 0.1;  // BA662A input offset
	// Moves TR28's turn-on (volts) beyond what R196 does, for tools: 0 in every calibration. R196
	// on S6's side of R197 already opens the VCA 0.27 V lower than the circuit diagram's
	// placement, which is what a real unit's release shows (docs/model.md).
	double vcaKneeShiftVolts = 0;
	// OUTPUT jack load.
	double loadOhms = 47e3;

	static FilterCalibration serviceNotes();
	static FilterCalibration realUnit();
	static FilterCalibration roland();
	bool set(const std::string &name, double value);
};

// The synth board from the source mixer to the OUTPUT jack: the IR3109 VCF with its resonance
// path and CV summer (FREQ, ENV, MOD, KYBD, bender), the ENV generator with its trigger modes,
// the BA662 VCA (ENV or GATE) and the output stage with VOLUME. Runs at the voice's internal
// (oversampled) rate. docs/circuit-filter.md transcribes it, docs/model-filter.md documents the
// model.
class FilterSection {
public:
	FilterSection();
	~FilterSection();
	void prepare(double internalRate);
	void reset();
	void setControls(const Controls &c);
	void setCalibration(const FilterCalibration &c);
	void setPerformance(const Performance &p);
	void setLines(const CpuLines &lines);
	// +9 V while switched off (Ba662Vca::setSupply); reset() puts it back to 9 V.
	void setSupply(double v9);

	// The ENV generator, one internal sample (it runs first: the PWM's ENV position needs it).
	// Returns the ENV output (IC16b pin 7) in volts against ground: it rests at the +5 V bus and
	// peaks at 10.11 V (5.11 V above the bus, R158 and VR24 from +14 V).
	double tickEnv(const ModFrame &mod);
	// The VCF, VCA and output stage, one internal sample. Returns the OUTPUT jack in volts.
	double tick(const OscFrame &osc);

	double vcfHz() const; // probes: the stages' pole frequency
	double env() const;   // ENV, 0-1 of its swing
	double vca() const;   // TR28's current, 0-1 of its ENV peak
	double out4() const;  // the VCF's OUT4 against the bus
	double pin9() const;  // the expo's control against the bus
	double iAbc() const;  // the VCF OTAs' current

	struct Impl;

private:
	Impl *m;
};

} // namespace sh101
