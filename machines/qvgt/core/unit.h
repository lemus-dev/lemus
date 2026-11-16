// The QuadraVerb GT at the host's rate: the board (core/board.h) between models of its analog
// stages (docs/circuit.md). No JUCE, no threads: the plugin and the tools drive it.
//
//   host in -> volts at the jacks (0 dBFS = +4 dBu) -> 8 x the DSP's rate -> the preamp (sheet 1,
//   core/preamp.h) -> the A/D's input (sheet 2: pre-emphasis, R143 into the 20 kHz module, R138)
//   -> down to the DSP's 41,666.67 Hz (the decimator standing in for the module) -> the board
//   -> the D/A (PCM54, +-3 V) -> its sample-and-holds (the de-emphasis) -> the reconstruction
//   filter (U16) -> U15's summers with the dry path (the CEM3381 VCA, U12) -> the OUTPUT pot and
//   U15's output stages -> up to the host's rate -> volts at the jacks back to the host's scale.
#pragma once

#include "board.h"
#include "decimator.h"
#include "noise.h"
#include "preamp.h"
#include "resampler.h"
#include "ssm2120.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace qvgt {

class Unit {
public:
	// Panel keys as the firmware numbers them (core/board.h).
	enum Key {
		kKeyEq = 0, kKeyPreAmp = 1, kKeyValueDown = 2, kKeyValueUp = 3, kKeyMidi = 4, kKeyBypass = 5, kKeyPageDown = 6, kKeyPageUp = 7,
		kKeyPitch = 8, kKeyNameMod = 9, kKeyStore = 10, kKeyProgram = 11, kKeyDelay = 12, kKeyMix = 13, kKeyConfig = 14, kKeyReverb = 15
	};
	// The key force (the piezo's volts, core/board.h) of a light press and a hard one: holding
	// VALUE for 3 s steps about 4 programs at the first and 58 (the most) at the second.
	static constexpr double kLightPress = 0.1, kHardPress = 2.0;
	// The host's full scale at the jacks: 0 dBFS = +4 dBu, a sine's peak.
	static constexpr double kVoltsFS = 1.736;

	explicit Unit(std::vector<uint8_t> descrambledRom);

	Board &board() { return *m_board; }
	Preamp &preamp() { return m_preamp; }

	// The POWER switch on: the 80C31 from reset with the RAM as the battery kept it, the boot
	// banners run through without audio (`seconds` of the unit's time).
	void powerOn(double seconds = 3.2);
	// What the unit does at the factory: STORE, PAGE UP three times ("RECALL ALL 90 ALESIS
	// PROGRAMS"), STORE (reference manual 3.6), through the panel keys, without audio.
	void recallFactoryPrograms();
	// Runs the board without audio.
	void runSilent(double seconds);

	void prepare(double hostRate);
	int latency() const { return m_latency; }
	// Stereo in, stereo out; the buffers may alias.
	void process(const float *inL, const float *inR, float *outL, float *outR, int n);

	// The INPUT pot (dual 10KB, the gain of U2's stages) and the OUTPUT pot (dual 10KA, ahead of
	// U15's output stages), as rotations 0-1.
	double inputPot = 1.0, outputPot = 1.0;

	// The parts' own noise (a bar setting, on by default): the op-amps', the VCAs' and the output
	// stages', and the A/D's offset servo (core/board.h). Off: noiseless, and asleep exact zeros.
	void setHiss(bool on);
	bool hiss() const { return m_hiss; }

	// Sleep (effects: the input under -200 dBFS): once the input has been silent for 1.6 s (longer
	// than the longest delay) and the output has settled, the DSP and the analog stages stop, with the
	// DSP's memory as it is; the 80C31 runs on. Settled: under -120 dB for 1.6 s, and asleep the output
	// is exact zeros; with HISS, its level steady (within 1 dB over 1.6 s) and under -40 dBFS, and
	// asleep the hiss goes on as a noise filter fitted to that last idle output. Any input, key or
	// MIDI byte wakes it.
	bool asleep() const { return m_asleep; }
	void wake()
	{
		m_asleep = false;
		m_quiet = m_lowRun = 0;
		m_hissTail.levels.clear();
	}
	void setIdleSleep(bool on) { m_idleSleep = on; }

	// The AUDIO SIGNAL DISPLAY (J6, sheet 2): how many of its four LEDs are lit, from the envelope
	// against U17's ladder (0.32, 0.96, 2.34 and 5 V); the DSP's overflow (OVFLO) lights all four for
	// about 0.15 s (U33, R155 2.2M, C87 0.1u).
	int headroom() const { return m_headroom; }
	bool clip() const { return m_ovfHold > 0; }

private:
	void dspSample(double adc, double envelope, double dryL, double dryR, double &outL, double &outR);
	void hold(int key, double seconds);
	void updateDryCv();

	std::unique_ptr<Board> m_board;
	Preamp m_preamp;
	double m_hostRate = 48000;
	int m_latency = 0;

	// the preamp's steps per DSP sample (kPreampOver; tools: QVGT_PREAMP_OVER) and its rate
	int m_over = kPreampOver;
	double m_preampRate = kSampleRate * kPreampOver;
	// host -> preamp rate, preamp rate -> DSP rate, DSP rate -> host
	std::unique_ptr<Resampler> m_inL, m_inR, m_upL, m_upR;
	Decimator m_decAdc, m_decDryL, m_decDryR;
	std::vector<float> m_bufL, m_bufR, m_wetL, m_wetR;
	std::deque<float> m_fifoL, m_fifoR;

	// first-order sections by the bilinear transform: y = b0 x + b1 x1 - a1 y1
	struct FirstOrder {
		double b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
		double operator()(double x)
		{
			const double y = b0 * x + b1 * x1 - a1 * y1;
			x1 = x;
			y1 = y;
			return y;
		}
		void reset() { x1 = y1 = 0; }
	};
	struct Biquad {
		double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
		double operator()(double x)
		{
			const double y = b0 * x + z1;
			z1 = b1 * x - a1 * y + z2;
			z2 = b2 * x - a2 * y;
			return y;
		}
		void reset() { z1 = z2 = 0; }
	};
	// the D/A's zero-order hold: its sinc droop as a 7-tap FIR
	struct Zoh {
		double h[7] = {};
		double operator()(double x)
		{
			for (int i = 6; i > 0; --i)
				h[i] = h[i - 1];
			h[0] = x;
			return 0.87553 * h[3] + 0.072777 * (h[2] + h[4]) - 0.013946 * (h[1] + h[5]) + 0.004617 * (h[0] + h[6]);
		}
	};

	FirstOrder m_preEmph;          // U16 (pins 1-3): R142 75K, R144 20K, C41 330p, at the preamp's rate
	double m_shAlpha = 0.5;        // the sample-and-holds' share of a step taken per sample
	double m_shL = 0, m_shR = 0;
	Zoh m_zohL, m_zohR;
	Biquad m_reconL, m_reconR;     // U16's Sallen-Key low-passes (R134, R133 1.1K, C51 .01u, C49 3300p)
	SeriesRc m_dryInL, m_dryInR;   // R97, R98 10K and C37, C36 4.7u into the CEM3381's inputs
	uint8_t m_dryCode = 0xff;      // latch 0600h b0-b4 as last solved
	double m_cvTarget = 0, m_cvCoef = 0, m_cv = 0; // VCA C.V. (C100 0.1u)
	FirstOrder m_couplingL, m_couplingR; // C45, C44 10u into the OUTPUT pot
	FirstOrder m_bufferL, m_bufferR;     // U15's output stages: R125 4.7K, R126 2K, C53 4.7u
	double m_envelope = 0;

	// noise
	bool m_hiss = true;
	Noise m_noise{0x5bd1e995u};
	double m_dspSigma = 0; // white noise of 1 V/sqrt(Hz) at the DSP's rate, per sample

	// the hiss while asleep: the idle output's last 0.25 s, its 100 ms levels, and the all-pole
	// filters fitted to it
	struct HissTail {
		static constexpr int kOrder = 10;
		std::vector<float> ring[2];
		size_t pos = 0;
		double blockSum = 0;
		int blockCount = 0, blockLen = 4800;
		std::vector<double> levels; // dB, the last 16 blocks
		double a[2][kOrder + 1]{}, gain[2]{}, y[2][kOrder]{};
		void prepare(double rate);
		void push(float l, float r);
		bool steady() const; // 16 blocks within 1 dB, under -40 dBFS
		void fit();
		void render(Noise &n, float &l, float &r);
	} m_hissTail;

	int m_headroom = 0, m_ovfHold = 0;

	bool m_idleSleep = true, m_asleep = false;
	int m_quiet = 0;          // host samples of silent input
	int m_lowRun = 0;         // host samples of output under -120 dB, with the input silent
	int m_settleSamples = 1;
	double m_heldDebt = 0;    // DSP samples owed to the 80C31 while asleep
};

} // namespace qvgt
