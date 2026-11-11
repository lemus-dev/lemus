#pragma once

#include "circuit.h"
#include "oversampler.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tr808 {

class Parallel;

// The voices, in the order the voice board's outputs are kept. CP is HAND CLAP or MARACAS by its
// switch (SW12), and the toms the congas by theirs (SW8-SW10).
enum class Voice : uint8_t { BD, SD, LT, MT, HT, RS, CP, CB, CY, OH, CH, Count };
constexpr int kVoices = int(Voice::Count);

// Instrument data as the CPU puts it on PD/PE/PF while the common trig fires: one line per voice,
// plus ACCENT (PF3). service notes p.3.
enum : uint16_t {
	kBitBD = 1 << 0, kBitSD = 1 << 1, kBitLT = 1 << 2, kBitMT = 1 << 3, kBitHT = 1 << 4, kBitRS = 1 << 5,
	kBitCP = 1 << 6, kBitCB = 1 << 7, kBitCY = 1 << 8, kBitOH = 1 << 9, kBitCH = 1 << 10, kBitAC = 1 << 11,
};
constexpr uint16_t bitOf(Voice v) { return uint16_t(1u << unsigned(v)); }

// The panel's voice controls as pot rotations, 0 (fully anticlockwise) to 1, and its switches.
struct Controls {
	double accent = 0.5;
	double bdLevel = 0.8, bdTone = 0.5, bdDecay = 0.5;
	double sdLevel = 0.8, sdTone = 0.5, sdSnappy = 0.5;
	double ltLevel = 0.8, ltTuning = 0.5;
	double mtLevel = 0.8, mtTuning = 0.5;
	double htLevel = 0.8, htTuning = 0.5;
	double rsLevel = 0.8, cpLevel = 0.8, cbLevel = 0.8;
	double cyLevel = 0.8, cyTone = 0.5, cyDecay = 0.5;
	double ohLevel = 0.8, ohDecay = 0.5;
	double chLevel = 0.8;
	double volume = 0.7; // MASTER VOLUME
	bool lc = false, mc = false, hc = false, cl = false, ma = false; // conga, claves, maracas
};

// The trims and the part values a unit's schematic leaves open or its production lot changed.
// The defaults are the service notes' nominal unit (docs/circuit.md); unitCalibration() is the one
// Lemus plays.
struct Calibration {
	double cbHz1 = 1000 / 1.85; // TM1 (voicing board): COWBELL FREQ.1, 1.85 ms
	double cbHz2 = 1000 / 1.25; // TM2: COWBELL FREQ.2, 1.25 ms
	// The four fixed metal oscillators (IC1, R43/C4, R42/C3, R41/C2, R40/C1), from the HD14584's
	// typical thresholds (Werner, Abel and Smith 2014): their frequencies and duty cycle.
	std::array<double, 4> metalHz{522.7, 304.4, 369.6, 205.3};
	double metalDuty = 0.47;
	double noiseRms = 0.130; // TM4: TP4 white noise, 130 mV rms
	double cpOffset = 0.5;   // TM3 (C.P. OFFSET) rotation
	double monoMs = 1.0;     // the common trig's width (IC6)
	double claveR313 = 390e3; // the claves' feedback; the schematic's value can't decay (docs/model.md)
	bool lateSnare = true;   // the "PORTION CHANGED" snare values (C58 .056, C61 .015, R191 10k, R200 0)
	bool lateClap = true;    // and the clap's added R200 (10k, Q70 emitter to -15 V)
	// Part values a voice's netlist takes as .param, by "voice.param" ("bd.r170"): a fitted unit's
	// values where they differ from the schematic's (docs/model.md, "Calibration").
	std::map<std::string, double> parts;
	// The voice knobs' scale against their pots' travel: the rotation the 2.5, 5 and 7.5 ticks set
	// (straight lines between them and the ends). Every knob under a tick ring (ACCENT and the voice
	// section) takes it; the defaults are a linear scale.
	std::array<double, 3> knobLaw{0.25, 0.5, 0.75};
	std::map<std::string, std::array<double, 3>> knobLaws; // a knob's own, by its Controls name ("ltTuning")
	double knob(const char *name, double tick) const;
};
Calibration unitCalibration();

// The TR-808's voicing: the common trig and accent circuit, the noise generator and metallic
// source, all eleven voices and the mixer, each run sample by sample from its netlist
// (core/netlists/, tools/spice for the SPICE comparisons). Runs at the host rate times 2 (88.2 or
// 96 kHz), times 1 at 88.2 kHz and up, and decimates.
class VoiceBoard {
public:
	VoiceBoard();
	~VoiceBoard();
	void prepare(double hostRate);
	void reset(); // every circuit at its operating point, warmed up (a new instance: an 808 that is on)
	// POWER switched on: every capacitor discharged, the master muted by Q13 for the power-on
	// detector's 1.5 s while the board charges (docs/model.md, "Sleep and power-on").
	void coldStart();

	void setControls(const Controls &c);
	const Controls &controls() const { return m_controls; }
	void setCalibration(const Calibration &c);
	const Calibration &calibration() const { return m_cal; }

	// The CPU's lines: mono starts the common trig's monostable (IC6) on its rising edge (the
	// CPU's PI2 step pulse), data is the instrument data held on the ports (kBit*). Take effect
	// from the next sample.
	void setLines(bool stepPulse, uint16_t data);

	// A cable in a voice's MULTI OUT jack: the jack's normal contact opens, taking the voice out of
	// the mix, and the cable's input loads it (mix.cir's plug_<voice>, the voice's own). Kept across
	// prepare().
	void setPlugged(Voice v, bool plugged);
	bool plugged(Voice v) const { return m_plugged[size_t(v)]; }

	// Renders n host-rate samples of the master HI output, in volts / kVoltsFullScale, and with
	// `jacks` (kVoices channels, null for any not wanted) each voice's MULTI OUT jack the same way.
	static constexpr double kVoltsFullScale = 10.0;
	void render(float *out, int n, float *const *jacks = nullptr);
	// Worker threads for render's voices (core/parallel.h), or null to run them all on the caller's
	// thread: the same result either way.
	void setParallel(Parallel *p);

	// Each voice's jack (MULTI OUT) voltage at the last internal sample, and whether its circuit
	// is running.
	double voiceOut(Voice v) const;
	bool voiceAwake(Voice v) const;
	bool asleep() const; // nothing running: the output is the settled mixer's
	// Any board signal by name ("cy.a1", "hi", "jack.bd"), for tools; -1e9 if none.
	double signal(const std::string &name) const;
	double internalRate() const { return m_rate; }
	int factor() const { return m_factor; }

	// SERVICE (plugin/HardwareService.cpp). The trimmers that are the board's, moved without a
	// rebuild: TM1 and TM2 on the voicing board (the cowbell's squares, Calibration::cbHz1 and
	// cbHz2), TM4 (the white noise's level, noiseRms) and TM3 (the clap's offset, cpOffset).
	void setTrims(double cbHz1, double cbHz2, double noiseRms, double cpOffset);
	// What TP4 reads (volts rms) for a noiseRms of 1, with the noise generator as loaded: the
	// density formula takes IC24a's gain flat to C202's corner, the circuit has its own band (worked
	// out from its impulse response at prepare()).
	double tp4PerNoiseRms() const;
	// A scope's probes: channel 0 or 1 on a node of a netlist ("<owner>.<node>": "cp.o19", "noise.1";
	// the owners are parts()'s) or a board signal ("jack.cp", "o1", "wn", "trig", "data.bd"); ""
	// takes it off. A probe on a node loads its partition again with the node as an output, its
	// state kept. Each render() records the probed voltages at every board sample (n x factor(); a
	// voice at twice the board's rate its second step's), read with probeSamples (null while the
	// probe is off), and the voices' gates (the common trig's 1 ms with each voice's data line, a
	// bit per voice, kBit*) with gateSamples. False if there is no such node.
	static constexpr int kProbes = 2;
	bool setProbe(int channel, const std::string &where);
	const float *probeSamples(int channel) const;
	const uint16_t *gateSamples() const;
	// The parts the netlists draw, by their designators on the boards ("R215", "IC24a", "VR17",
	// "TM3", "Q13" for the JFETs), each with its netlist ("owner": accent, mtrig, noise, metal, mix,
	// the voices' bd ... cb, cy, oh for the hats, ma for the maracas), its board ("main", "voicing";
	// the mixer's R1-R14 and C1 "jackb") and each pin with its node ("owner.node", for setProbe): a
	// resistor's or capacitor's two ends, a diode's anode and cathode, a transistor's C B E (D G S),
	// an op-amp's or comparator's + - out, the OTA's I abc + - out, a pot's ends and wiper.
	struct Part {
		std::string ref, owner, board, value;
		std::vector<std::pair<std::string, std::string>> pins; // (pin, node)
	};
	static const std::vector<Part> &parts();

	struct Stats {
		long samples = 0;                         // internal samples
		std::array<long, kVoices> voiceSteps{};   // internal samples each voice ran
		std::array<long, kVoices> iterations{}, factorizations{}; // their Newton work
		std::array<long, kVoices> linearSteps{}, linearizations{}; // block steps in linear mode, and its rebuilds
		std::array<int, kVoices> ports{};
		std::array<double, kVoices> seconds{}; // CPU time each voice took (TR808_PROFILE set)
		long substepped = 0, failures = 0;
	};
	Stats stats() const;
	std::string blockReport() const; // tools: each circuit's steps, Newton iterations, linear steps
	bool runsJobs() const;           // tools: render() runs the voices as jobs (checkJobs passed), not interleaved

	struct Impl;

private:
	// render with every circuit stepped sample by sample in turn (short blocks)
	void renderInterleaved(float *out, int n, float *const *jacks);
	Controls m_controls;
	std::array<bool, kVoices> m_plugged{};
	Calibration m_cal;
	double m_hostRate = 48000, m_rate = 96000;
	int m_factor = 2;
	Decimator m_decimator;
	std::vector<float> m_work;
	std::array<Decimator, kVoices> m_jackDecimators; // the jacks, board rate to host rate
	std::vector<float> m_jackWork;                   // kVoices x the factor
	std::unique_ptr<Impl> m_impl;
};

} // namespace tr808
