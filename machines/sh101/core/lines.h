#pragma once

#include <cstdint>

namespace sh101 {

// What the CPU board hands the synth board (docs/firmware.md, I/O map). The three DAC
// sample-and-holds are kept as 6-bit codes; one code is 1/12 V once VR1-VR3 are trimmed.
struct CpuLines {
	int kcv = 0;           // S/H channel 0 (Y0): KCV, also the CV OUT jack
	int range = 0;         // S/H channel 1 (Y1): VCO RANGE plus key transpose
	int random = 0;        // S/H channel 2 (Y2): the RANDOM modulator wave (doubled by IC11a)
	bool gate = false;     // P27 inverted by TR9
	bool portaOff = false; // P23, "PORTA OFF" to the bender board (AUTO portamento)
	bool clkRst = false;   // P24 low: LFO phase reset, one loop long
};

// Panel switches. The CPU reads RANGE, the ENV trigger switch (S7) and TRANSPOSE; S7 also
// switches the envelope's trigger on the synth board.
enum class Range : uint8_t { Ft16, Ft8, Ft4, Ft2 };
enum class Trigger : uint8_t { GateTrig, Gate, Lfo };
enum class Transpose : uint8_t { Low, Mid, High };
enum class LfoWave : uint8_t { Triangle, Square, Random, Noise };
enum class PwmSource : uint8_t { Lfo, Manual, Env };
enum class SubMode : uint8_t { Oct1Square, Oct2Square, Oct2Pulse };
enum class VcaMode : uint8_t { Env, Gate };
enum class PortaMode : uint8_t { On, Auto, Off };

// The synth board's front-panel controls. Sliders and knobs are travel 0 (bottom / fully
// anticlockwise) to 1, as the panel scales print 0-10; the taper is the voice's business.
struct Controls {
	// MODULATOR
	double lfoRate = 0.5;
	LfoWave lfoWave = LfoWave::Triangle;
	// VCO
	double vcoMod = 0;
	double pulseWidth = 0;
	PwmSource pwmSource = PwmSource::Manual;
	// SOURCE MIXER
	double pulseLevel = 1;
	double sawLevel = 0;
	double subLevel = 0;
	double noiseLevel = 0;
	SubMode subMode = SubMode::Oct2Square;
	// VCF
	double vcfFreq = 0.5;
	double vcfRes = 0;
	double vcfEnv = 0;
	double vcfMod = 0;
	double vcfKybd = 0;
	// VCA
	VcaMode vcaMode = VcaMode::Env;
	// ENV
	double attack = 0;
	double decay = 0.3;
	double sustain = 0.6;
	double release = 0.2;
	Trigger trigger = Trigger::GateTrig;
	// Controller section
	double volume = 0.7;
	double tune = 0.5;         // TUNE knob, centre = calibrated pitch
	double portamento = 0;
	PortaMode portaMode = PortaMode::Off;
	double bendVco = 0.5;      // bender sensitivity sliders
	double bendVcf = 0;
	double lfoModDepth = 0.5;  // bender MOD: LFO depth when the lever is pushed
};

// Performance controls that are not panel settings: the bender lever and its push.
struct Performance {
	double bend = 0;   // -1 (left) .. +1 (right)
	double modPush = 0; // 0 .. 1, lever pushed forward
};

} // namespace sh101
