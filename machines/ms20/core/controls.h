// The MS-20's panel controls as the machine takes them: each knob at its printed position (most are
// 0-10; PITCH and MASTER TUNE are centre-zero), each rotary switch as its position from the left.
// The names follow the panel (owner's manual p. 4, service manual §1).
#pragma once

namespace ms20 {

// VCO 1 WAVE FORM, left to right: triangle, sawtooth, pulse (PW), white noise.
enum class Vco1Wave { Triangle, Saw, Pulse, Noise };
// VCO 2 WAVE FORM: sawtooth, square, narrow pulse, ring modulator.
enum class Vco2Wave { Saw, Square, Narrow, Ring };

struct Controls {
	// VCO 1
	Vco1Wave vco1Wave = Vco1Wave::Saw;
	double vco1Pw = 5;   // PW, 0-10
	int vco1Scale = 2;   // SCALE: 32', 16', 8', 4' (0-3)
	// VCO 2
	Vco2Wave vco2Wave = Vco2Wave::Saw;
	double vco2Pitch = 0; // PITCH, -10..+10 (about ±1 octave, service manual §1)
	int vco2Scale = 1;    // SCALE: 16', 8', 4', 2' (0-3)
	// VCO common
	double portamento = 0; // 0-10
	double masterTune = 0; // MASTER TUNE, -10..+10 (±100 cents, §1)
	double fmMg = 0;       // FREQUENCY MODULATION: MG/T.EXT, 0-10
	double fmEg1 = 0;      // EG 1/EXT, 0-10
	// VCO MIXER
	double vco1Level = 10, vco2Level = 0;
	// VC HPF
	double hpfCutoff = 0, hpfPeak = 0, hpfMg = 0, hpfEg2 = 0;
	// VC LPF
	double lpfCutoff = 10, lpfPeak = 0, lpfMg = 0, lpfEg2 = 0;
	// MODULATION GENERATOR
	double mgWave = 5, mgFreq = 5;
	// ENVELOPE GENERATOR 1
	double eg1Delay = 0, eg1Attack = 0, eg1Release = 0;
	// ENVELOPE GENERATOR 2
	double eg2Hold = 0, eg2Attack = 0, eg2Decay = 5, eg2Sustain = 10, eg2Release = 2;
	// VOLUME (by the patch panel)
	double volume = 7;
	// EXTERNAL SIGNAL PROCESSOR
	double espLevel = 0, espLowCut = 0, espHighCut = 10, espCvAdj = 5, espThreshold = 5;
};

// The performance controls: the PROGRAMMABLE CONTROL WHEEL (centre click, −1..+1) and the MOMENTARY
// SWITCH. They come out on their own jacks (a cable takes them anywhere).
struct Manual {
	double wheel = 0;
	bool momentary = false;
};

} // namespace ms20
