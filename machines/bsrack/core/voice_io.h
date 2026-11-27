// What the CPU board hands the analog voice (core/voice.h) each host sample: the DCO reset edges at
// their exact times, the sample-and-holds' DAC codes, the on-chip mixer levels and the 4053's
// selects. The board (core/board.h) fills it; the voice turns it into audio. docs/firmware.md says
// where each value comes from in v1.5.
#pragma once

#include <array>
#include <cstdint>

namespace bsrack {

// The serial DAC's sample-and-hold channels (A000 bits 4-6), as v1.5 drives them.
enum CvChannel : int {
	kCvOut,     // 0: the rear CV OUT (816 codes per octave)
	kVcfCv,     // 1: VCF_CV_LIN (code falls as the cutoff rises)
	kOscBPwm,   // 2: OSCB_PWM_CV
	kOscAPwm,   // 3: OSCA_PWM_CV
	kOscBCv,    // 4: OSCB_CV, the DCO's ramp-slope CV (code falls as the pitch rises)
	kOscACv,    // 5: OSCA_CV
	kResCv,     // 6: RES_CV (code falls as the resonance rises)
	kVcaCv,     // 7: VCA_CV_LIN, envelope 1 (code falls as the level rises)
	kCvChannels
};

struct VoiceFrame {
	// Each S&H's held 12-bit DAC code (0-4095) for this sample, after any reload in it.
	std::array<uint16_t, kCvChannels> dac{};
	// The mixer's oscillator levels, the on-chip registers (x'34') (osc 1, A_VOL_CV) and (x'32')
	// (osc 2, B_VOL_CV) as last written.
	uint8_t volA = 0, volB = 0;
	// The 4053's selects: true = pulse (port 1 bit 7 osc 1, bit 6 osc 2); slope12 = port 3 bit 1.
	bool osc1Pulse = false, osc2Pulse = false, slope12 = false;
	bool gateOut = false; // true while a note is held (port 3 bit 7 low: GATE OUT)
	// The MIX pot's position 0..1: v1.5 sets the oscillators' levels (volA, volB) from it, but
	// FILTER INPUT's level is analog, off the pot itself (no DAC channel or register for it).
	float mixPot = 0.5f;
	// The DCO sync lines' edges in this sample, as offsets 0 <= t < 1 within it (the 4070 makes a
	// reset pulse on every edge of either polarity). At most a few per sample at the top of the
	// range; more are dropped and counted.
	static constexpr int kMaxEdges = 8;
	std::array<float, kMaxEdges> edgesA{}, edgesB{};
	int nEdgesA = 0, nEdgesB = 0;
	int droppedEdges = 0;
};

} // namespace bsrack
