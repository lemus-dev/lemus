#pragma once

#include "circuit.h"
#include "oversampler.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tr909 {

// The voices, in the order of the MULTI OUT jacks. HH is both hi-hats: one circuit, one jack.
enum class Voice : uint8_t { BD, SD, LT, MT, HT, RS, HC, HH, CR, RD, Count };
constexpr int kVoices = int(Voice::Count);

// The panel's voice controls as pot rotations, 0 (fully anticlockwise) to 1.
struct Controls {
	double bdTune = 0.5, bdLevel = 0.75, bdAttack = 0.5, bdDecay = 0.5;
	double sdTune = 0.5, sdLevel = 0.75, sdTone = 0.5, sdSnappy = 0.5;
	double ltTune = 0.5, ltLevel = 0.75, ltDecay = 0.5;
	double mtTune = 0.5, mtLevel = 0.75, mtDecay = 0.5;
	double htTune = 0.5, htLevel = 0.75, htDecay = 0.5;
	double rsLevel = 0.75, hcLevel = 0.75;
	double hhLevel = 0.75, chDecay = 0.5, ohDecay = 0.5;
	double crLevel = 0.75, crTune = 0.5;
	double rdLevel = 0.75, rdTune = 0.5;
	double volume = 0.7; // MASTER VOLUME
	// The clap's trimmer TM on the board (hc.cir: its wiper, 0 at the -15 V end, 1 at ground);
	// below 0, the unit's own trim (the Calibration's hc.tm, else the netlist's).
	double hcTm = -1;
};

// The sample ROMs, 32 KB each (MAME's hn61256p dumps); null: that voice is silent.
struct SampleRoms {
	const uint8_t *hihat = nullptr; // IC69, hn61256p__c43.ic69
	const uint8_t *crash = nullptr; // IC62, hn61256p__c42.ic62
	const uint8_t *ride = nullptr;  // IC54, hn61256p__c44.ic54
};

// The part values a unit's schematic leaves open or its production changed, as .param overrides
// of the voices' netlists, by "voice.param" ("bd.c9"). Empty: the late board as the service notes
// draw it (docs/circuit.md).
struct Calibration {
	std::map<std::string, double> parts;
	double noiseHz = 0; // the noise generator's clock (0: its parts' estimate, core/noise.h)
	double hhClockHz = 0; // the hi-hat's sample clock (0: its parts', HiHat's 31.571 kHz)
	// Knob laws: where a knob's pot track is at each rotation of the knob, by its Controls name
	// ("ltTune"): points (rotation, track position), straight lines between them, the ends held. A
	// knob without one is straight (its pot's own taper is in its netlist). The real units' pots
	// have dead zones at their ends, where the wiper is on the end pads (docs/model.md, "Knob laws").
	std::map<std::string, std::vector<std::pair<double, double>>> knobLaws;
	double knob(const char *name, double rotation) const;
};
Calibration unitCalibration(); // the unit Lemus plays (docs/model.md, "Calibration")

// The TR-909's voicing board and multi-jack board: the trigger and accent latches the CPU
// writes, the noise generator, the ten voice circuits, each run sample by sample from its netlist
// (core/netlists/, docs/circuit.md), the hi-hat's and cymbals' ROM playback (counters, latches,
// D/As) into theirs, and the jack board's mix to the master outputs. Runs at twice the host rate
// (88.2 or 96 kHz), at the host rate from 88.2 kHz up, and decimates.
class Parallel;

class VoiceBoard {
public:
	VoiceBoard();
	~VoiceBoard();
	// Threads for the voices' jobs (core/parallel.h), or null to run them on the caller's thread.
	// The result is the same either way. Not owned.
	void setParallel(Parallel *p);
	void setSampleRoms(const SampleRoms &roms); // copied
	void setCalibration(const Calibration &c);
	const Calibration &calibration() const { return m_cal; }
	void prepare(double hostRate);
	void reset(); // every circuit at its operating point (a new instance: a 909 that is on)
	// The 909 switched on, as its CPU comes out of reset: the latches cleared (every trigger low,
	// every DAC at 0 V), the noise generator's start, the hi-hat's, crash's and ride's analog stages
	// from 0 V, and the master outputs muted by Q82/Q83 until C143 has charged (mix.cir solved with
	// the rail at C143 at -15 V, then stepped to +15 V: the mute's release and the DC restorer's
	// half-minute come out of the netlist; docs/circuit/power.md). The other voices stay at their
	// operating points. After prepare(), between render()s.
	void powerOn();

	void setControls(const Controls &c);
	const Controls &controls() const { return m_controls; }
	// A cable in a voice's MULTI OUT jack: the jack's switching contact takes the voice out of the
	// mix, and the cable's input loads it (mix.cir, plug_<voice>). Kept across prepare().
	void setPlugged(Voice v, bool plugged);
	bool plugged(Voice v) const { return m_plugged[size_t(v)]; }

	// A write from the CPU board to the latches: reg 1-8 the accent DACs (IC2-IC9; D1-D6, bits
	// 1-6), 9 the trigger latch IC1 (BD SD LT MT HT on bits 1-5), 10 IC10 (RS, HH, CLOSED/OPEN,
	// crash, ride, HC on bits 1-6). Takes effect from the next sample.
	void write(int reg, uint8_t value);
	uint8_t latch(int reg) const { return m_latch[size_t(reg & 15)]; }

	// Renders n host-rate samples of the master outputs (L, R), in volts / kVoltsFullScale, and with
	// `jacks` (kVoices channels, null for any not wanted) each MULTI OUT jack the same way.
	static constexpr double kVoltsFullScale = 10.0;
	void render(float *left, float *right, int n, float *const *jacks = nullptr);

	// Each voice's jack voltage at the last internal sample, and whether its circuit is running.
	double voiceOut(Voice v) const;
	bool voiceAwake(Voice v) const;
	bool asleep() const; // nothing running: the outputs are the settled mixer's
	// Any board signal by name ("trig.bd", "acc.1", "noise", "jack.bd", "bd.<node>"), for tools;
	// -1e9 if none.
	double signal(const std::string &name) const;
	// A scope's probes: channel 0 or 1 on a node of a voice's netlist ("hc.env1"; "tomnoise.<node>",
	// "mix.<node>" for the tom noise and the jack board) or a board signal ("trig.hc", "acc.6"); ""
	// takes it off. Each render() records the probed voltages at every board sample (n x factor()),
	// read with probeSamples (null until a probe is set). False if there is no such node.
	static constexpr int kProbes = 2;
	bool setProbe(int channel, const std::string &where);
	const float *probeSamples(int channel) const;
	// The parts the netlists draw, by their designators on the board ("R215", "IC26a", "VR20",
	// "TM"), each pin with its node ("owner.node", for setProbe): a resistor's or capacitor's two
	// ends, a diode's anode and cathode, a transistor's C B E, an op-amp's or comparator's + - out,
	// a CMOS gate's in and out, a pot's ends and wiper. Not the cymbals' clocks (their circuits
	// aren't the board's units).
	struct Part {
		std::string ref, owner, value;
		std::vector<std::pair<std::string, std::string>> pins; // (pin, node)
	};
	static const std::vector<Part> &parts();
	double internalRate() const { return m_rate; }
	int factor() const { return m_factor; }
	std::string blockReport() const; // tools: each circuit's steps, Newton iterations, linear steps

	struct Impl;

private:
	Controls m_controls;
	Calibration m_cal;
	std::array<uint8_t, 16> m_latch{};
	double m_hostRate = 48000, m_rate = 96000;
	int m_factor = 2;
	std::array<bool, kVoices> m_plugged{};
	Decimator m_decL, m_decR;
	std::vector<float> m_workL, m_workR;
	std::array<Decimator, kVoices> m_jackDecimators;
	std::vector<float> m_jackWork; // kVoices x the factor
	std::unique_ptr<Impl> m_impl;
};

} // namespace tr909
