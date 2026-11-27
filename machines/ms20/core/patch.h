// The MS-20's patch panel: its jacks and the cables between them. An input jack with no cable takes
// what its switch normals it to (the keyboard CV into VCO 1+2 CV IN, EG 2 into the filters' EXT
// CUTOFF FREQ...); a cable breaks that and brings an output's signal instead. One output can feed
// several inputs, as the Junction Box (MS-02) or a stacked cable would; an input takes one cable.
//
// The jacks and their J numbers come from the service manual's circuit diagrams (1) and (2) and its
// block diagram (docs/research.md §1.3–1.4); the panel layout (docs/panel.json) places them.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ms20 {

enum class Jack : uint8_t {
	// inputs
	TotalExtMod,  // J4  TOTAL EXT MODULATION (normal: MG triangle)
	VcoExtFreq,   // J6  VCO 1+2 EXT FREQ, FM EG 1/EXT (normal: EG 1)
	VcoCvIn,      // J26 VCO 1+2 CV IN (normal: KBD CV)
	Vco2CvIn,     // J24 VCO 2 CV IN (normal: VCO 1+2 CV)
	ExtSigIn,     // J13 EXT SIGNAL IN (into the VCO mixer)
	HpfExtCutoff, // J14 VC HPF EXT CUTOFF FREQ (normal: EG 2)
	LpfExtCutoff, // J21 VC LPF EXT CUTOFF FREQ (normal: EG 2)
	InitialGain,  // J22 VCA INITIAL GAIN
	TrigIn,       // J27 EG 1+2 TRIG IN (normal: KBD TRIG)
	Eg1TrigIn,    // J20 EG 1 TRIG IN (normal: EG 1+2 TRIG)
	ShIn,         // J2  S&H IN
	ShClock,      // J5  S&H CLOCK
	MvcaIn,       // J10 MVCA IN
	MvcaControl,  // J15 MVCA CONTROL (normal: EG 1)
	EspSignalIn,  // J1  ESP SIGNAL IN
	// outputs
	MgTriangle,   // J3  MG triangle (5 Vpp)
	MgRectangle,  // J7  MG rectangle (0/+5 V)
	Eg1RevOut,    // J11 EG 1 REVERSE OUT
	Eg2RevOut,    // J23 EG 2 REV OUT
	KbdCvOut,     // J33 KBD CV OUT
	KbdTrigOut,   // J32 KBD TRIG OUT
	PinkNoise,    // J19 PINK NOISE
	WhiteNoise,   // J25 WHITE NOISE
	ShOut,        // J8  S&H OUT
	MvcaOut,      // J16 MVCA OUT
	Wheel,        // J28 CONTROL WHEEL
	Momentary,    // J31 MOMENTARY SWITCH
	EspAmpOut,    // J9  ESP AMP OUT
	EspFilterOut, // J17 ESP BAND-PASS OUT
	EspFvOut,     // J18 ESP F-V CONVERTER CV OUT
	EspEnvOut,    // J29 ESP ENV OUT
	EspTrigOut,   // J30 ESP TRIG OUT
	SignalOut,    // J35 SIGNAL OUT
	Eg1Out,       // J12 EG-1 OUT (KLM-128)
	Count
};

constexpr int kJacks = int(Jack::Count);
constexpr int kFirstOutput = int(Jack::MgTriangle);
inline bool isInput(Jack j) { return int(j) < kFirstOutput; }

// The external source a cable can come from besides the panel's outputs: the plugin's audio input
// (a guitar or a microphone plugged into SIGNAL IN, as the manual shows).
constexpr int kHostInput = 255;

// The cables: for each input jack, the output it is patched from (an output Jack, kHostInput), or
// none.
struct Patch {
	static constexpr uint8_t kNone = 254;
	std::array<uint8_t, kFirstOutput> from{};
	Patch() { from.fill(kNone); }
	bool patched(Jack in) const { return from[size_t(in)] != kNone; }
	void connect(Jack in, int out) { from[size_t(in)] = uint8_t(out); }
	void disconnect(Jack in) { from[size_t(in)] = kNone; }
	bool operator==(const Patch &o) const { return from == o.from; }
	bool operator!=(const Patch &o) const { return !(*this == o); }
};

} // namespace ms20
