// The Bass Station Rack's front panel: 18 pots (17 read by the firmware through a mux into the CPU's
// ADC; VOLUME is analog, after the VCA), 12 switches (the switch matrix, rows from latch 0xA800, read
// at 0x8800), the keypad (port 9, rows from 0xA800 bits 4-7) and AUDITION (port 2 bit 4). Positions
// only: what each one does is the firmware's (docs/firmware.md).
#pragma once

#include <array>
#include <cstdint>

namespace bsrack {

// The pots, in panel order, left to right.
enum Pot : int {
	kVolume,
	kPortamento,
	kLfoSpeed,
	kLfoDelay,
	kMix,
	kDetune,
	kSemitone,
	kModEnvelope,
	kModLfo,
	kPulseWidth,
	kFrequency,
	kResonance,
	kModDepth,
	kAttack,
	kDecay,
	kSustain,
	kRelease,
	kVelocity,
	kPots
};

// The switches, in panel order, and how many positions each has.
enum Switch : int {
	kWrite,       // PROTECT, ENABLE
	kLfoShape,    // R'DOM, TRI, SAW
	kOsc1Wave,    // saw, pulse
	kOsc2Range,   // 16', 8', 4', 2'
	kOsc2Wave,    // saw, pulse
	kSync,        // OFF, ON
	kOscillator,  // 1, 2
	kPwmSource,   // ENV 2, MAN., LFO
	kCutOff,      // 12dB, 24dB
	kModSource,   // LFO, ENV 2
	kEnvelope,    // 1, 2, 1&2
	kTriggering,  // AUTOGLIDE, SINGLE, MULTI
	kSwitches
};
constexpr std::array<int, kSwitches> kSwitchPositions{2, 3, 2, 4, 2, 2, 2, 3, 2, 2, 3, 3};

// The buttons: the data-entry keypad, AUDITION/DEMO and MODE SELECT.
enum Button : int {
	kKey1,
	kKey2,
	kKey3,
	kKey4,
	kKey5,
	kKey6,
	kKey7,
	kKey8,
	kKey9,
	kKeyMinus,
	kKey0,
	kKeyPlus,
	kAudition,
	kMode,
	kButtons
};

struct Controls {
	std::array<double, kPots> pots{};    // 0 = fully left .. 1 = fully right
	std::array<int, kSwitches> switches{}; // 0 = leftmost position
};

} // namespace bsrack
