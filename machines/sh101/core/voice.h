#pragma once

#include "filter.h"
#include "jack_board.h"
#include "lines.h"
#include "osc.h"
#include "oversampler.h"

#include <memory>
#include <string>

namespace sh101 {

// Unit-to-unit and calibration constants, per section. serviceNotes() is a unit trimmed by the
// service-notes procedure with nominal parts; realUnit() is fitted to what recordings of a real
// SH-101 measure; roland() is fitted to Roland's SH-101 plug-in (tools/compare/fit_roland.py).
// docs/model.md documents all three.
struct Calibration {
	OscCalibration osc;
	FilterCalibration filter;

	static Calibration serviceNotes() { return {OscCalibration::serviceNotes(), FilterCalibration::serviceNotes()}; }
	static Calibration realUnit() { return {OscCalibration::realUnit(), FilterCalibration::realUnit()}; }
	static Calibration roland() { return {OscCalibration::roland(), FilterCalibration::roland()}; }
	// The plugin's CAL choices, in order: 0 service notes, 1 real unit, 2 Roland.
	static Calibration byIndex(int i) { return i == 0 ? serviceNotes() : i == 2 ? roland() : realUnit(); }
	// For tools: sets a field by name ("vcoTuneHz=441"); false if there is no such field.
	bool set(const std::string &name, double value) { return osc.set(name, value) || filter.set(name, value); }
};

// The synth board from the S/H outputs onwards: the oscillator section (core/osc.h) feeding
// the filter section (core/filter.h). Runs both oversampled and decimates to the host rate.
class Voice {
public:
	Voice();
	~Voice();
	void prepare(double hostRate);
	void reset();
	void setControls(const Controls &c);
	const Controls &controls() const;
	void setCalibration(const Calibration &c);
	const Calibration &calibration() const;
	void setPerformance(const Performance &p);
	void setLines(const CpuLines &lines);
	// Jack board A's mute (core/jack_board.h). powerOn(): switched on now, so the output is shunted
	// for about a second and opens over the next half. settlePower(): switched on long ago (a new
	// Voice is). Neither touches the rest of the voice; reset() leaves the mute alone.
	// powerOff(): switched off now. The rails fall: the voice stops within a few milliseconds,
	// TR8 and C14 shunt the output and IC1a's fall to ground thumps through C2. render() goes on
	// for half a second while poweringDown(), then gives exact zeros. powerOn() after reset()
	// starts again from there.
	void powerOn();
	void settlePower();
	void powerOff();
	bool poweringDown() const;
	bool powering() const; // the power-on mute still acts, if only just
	bool muting() const;   // the power-on mute is still audible

	// Renders n host-rate samples; mod holds n modulator frames. Output is the OUTPUT jack
	// voltage, scaled by kOutputScale.
	void render(float *out, int n, const ModFrame *mod);

	// OUTPUT jack volts to sample value: 0 dBFS = 2.2 V peak, which puts the unit at the level of
	// Roland's SH-101 plug-in at VOLUME 10, as the TB-303 matches Roland's software
	// (docs/model.md). A full saw at VOLUME 10 (the spec's 0 dBm max) peaks near -5 dBFS; IC1a's
	// own clipping (+2.6/-3.6 V at the jack) is the only thing that passes full scale.
	static constexpr double kOutputScale = 1 / 2.2;

	double internalRate() const;
	double latencySamples() const;

	// Probes for the editor and tests, updated each render.
	struct Probe {
		double cvVolts = 0, vcoHz = 0, vcfHz = 0, iAbc = 0, env = 0, vca = 0, gate = 0;
	};
	Probe probe() const;

	struct Impl;

private:
	std::unique_ptr<Impl> m;
};

} // namespace sh101
