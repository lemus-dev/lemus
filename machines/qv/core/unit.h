// The QuadraVerb at the host's rate: the board (core/board.h) between models of its analog
// stages (docs/circuit.md; the service manual's sheet 1, rev F). No JUCE, no threads: the plugin
// and the tools drive it.
//
//   host in -> volts at the jacks (0 dBFS = +4 dBu) -> 4 x the DSP's rate (166,667 Hz) -> the input
//   stages (U1: followers, the INPUT pot's inverting stages) -> R17, R18 into one node (the mono
//   sum) -> U4's pre-emphasis -> R25 into the 20 kHz module, R26 -> down to the DSP's 41,666.67 Hz
//   (the decimator standing in for the module) -> the board -> the D/A (PCM54, +-3 V) -> its
//   sample-and-holds (U9, R42: the de-emphasis), held through each DSP sample at 4 x the rate -> U3,
//   U4's reconstruction filters -> U2's summers with the dry path (the CEM3381, U5; the input stages'
//   output delayed by the decimator's delay, so dry and wet line up as through the converters) ->
//   the OUTPUT pot and U2's output stages -> down to the host's rate -> volts at the jacks back to
//   the host's scale. The analog stages run at 4 x so that the ones with corners near or above the
//   DSP's Nyquist frequency (the pre-emphasis's pole at 24 kHz, the reconstruction filters' 25 kHz,
//   the summers' 53 kHz) keep their response in the audio band (tools/spice/ac.py).
#pragma once

#include "analog.h"
#include "board.h"
#include "decimator.h"
#include "noise.h"
#include "resampler.h"
#include "solver_stats.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace qv {

class Unit {
public:
	// Panel keys as the firmware numbers them (core/board.h).
	enum Key {
		kKeyEq = 0, kKeyName = 1, kKeyValueDown = 2, kKeyValueUp = 3, kKeyMidi = 4, kKeyBypass = 5, kKeyPageDown = 6, kKeyPageUp = 7,
		kKeyPitch = 8, kKeyMod = 9, kKeyStore = 10, kKeyProgram = 11, kKeyDelay = 12, kKeyMix = 13, kKeyConfig = 14, kKeyReverb = 15
	};
	// The key force (the piezo's volts, core/board.h) of a light press and a hard one: holding
	// VALUE for 3 s steps about 4 programs at the first and 58 (the most) at the second.
	static constexpr double kLightPress = 0.1, kHardPress = 2.0;
	// The host's full scale at the jacks: 0 dBFS = +4 dBu, a sine's peak.
	static constexpr double kVoltsFS = 1.736;

	explicit Unit(std::vector<uint8_t> descrambledRom);

	Board &board() { return *m_board; }

	// The POWER switch on: the 80C31 from reset with the RAM as the battery kept it, the boot
	// banners run through without audio (`seconds` of the unit's time).
	void powerOn(double seconds = 3.2);
	// What the unit does at the factory: STORE, PAGE UP three times ("RECALL ALL 90 ALESIS
	// PROGRAMS"), STORE, through the panel keys, without audio; it lands on program 00. (A power-on
	// holding BYPASS and PROG, the service manual's reinitialization, also loads them, but leaves
	// "BASIC PROGRAM" in the edit buffer.)
	void recallFactoryPrograms();
	// Runs the board without audio.
	void runSilent(double seconds);

	void prepare(double hostRate);
	int latency() const { return m_latency; }
	// Stereo in, stereo out; the buffers may alias.
	void process(const float *inL, const float *inR, float *outL, float *outR, int n);

	// The INPUT pot (dual 10K B, the gain of U1's inverting stages: 0 at the bottom, +20 dB at the
	// top) and the OUTPUT pot (dual 10K A, ahead of U2's output stages), as rotations 0-1.
	double inputPot = kInputUnity, outputPot = 1.0;
	// The INPUT pot's rotation for unity gain: R_b = 1K + R_a, 0.55 of its travel.
	static constexpr double kInputUnity = 0.55;

	// The parts' own noise: the op-amps', the VCAs' and the output stages', and the A/D's offset
	// servo (core/board.h). Always on in the plugin, as on the unit (no setting); off only for the
	// tools' comparisons against SPICE (qvbench QV_HISS=0): noiseless, and asleep exact zeros.
	void setHiss(bool on);
	bool hiss() const { return m_hiss; }

	// Sleep (effects: the input under -200 dBFS): once the input has been silent for 1.6 s (longer
	// than the longest delay) and the output has settled, the DSP and the analog stages stop, with the
	// DSP's memory as it is; the 80C31 runs on. Settled: the output's level steady (within 1 dB over
	// 1.6 s) and under -40 dBFS, and asleep the hiss goes on as a noise filter fitted to that last
	// idle output (without the hiss, tools only: under -120 dB for 1.6 s, then exact zeros). Any
	// input, key or MIDI byte wakes it.
	bool asleep() const { return m_asleep; }
	void wake()
	{
		m_asleep = false;
		m_quiet = m_lowRun = 0;
		m_hissTail.levels.clear();
	}
	void setIdleSleep(bool on) { m_idleSleep = on; }

	// Tools only (tools/compare): the summers without the wet signal, or without the dry, to take
	// the output apart into its two paths.
	bool muteWet = false, muteDry = false;

	// The AUDIO SIGNAL DISPLAY (J2, sheet 1): how many of its four LEDs are lit, from the envelope
	// against U7's ladder (0.33, 0.96, 2.34 and 5 V); the DSP's overflow (OVFLO) lights all four for
	// about 0.15 s (U24, R55 2.2M, C73 0.1u).
	int headroom() const { return m_headroom; }
	bool clip() const { return m_ovfHold > 0; }

	// What the iterative solves did since prepare() (solver_stats.h): the dry VCA's control voltage.
	SolverStats solverStats() const { return m_dryStats; }

private:
	// one DSP sample: the board, the overflow and signal display, the D/A's sample-and-holds
	void dspStep(double adc, double envelope);
	// one step of the analog after the D/A at 4 x the DSP's rate, with the dry signal
	void outputStep(double dryL, double dryR, double &outL, double &outR);
	void hold(int key, double seconds);
	void updateDryCv();
	void updateInputPot();

	std::unique_ptr<Board> m_board;
	double m_hostRate = 48000;
	int m_latency = 0;

	// the analog stages' steps per DSP sample and their rate
	static constexpr int kOver = 4;
	double m_inRate = kSampleRate * kOver;
	// host -> the analog stages' rate, that rate -> DSP rate (the A/D; the dry signal's delay to
	// match), the analog stages' rate -> host
	std::unique_ptr<Resampler> m_inL, m_inR, m_upL, m_upR;
	Decimator m_decAdc;
	std::vector<double> m_dryDelayL, m_dryDelayR;
	size_t m_dryPos = 0;
	std::vector<float> m_bufL, m_bufR, m_wetL, m_wetR;
	// the output, held back by the latency: a queue over a vector (compacted as it is read)
	struct Fifo {
		std::vector<float> buf;
		size_t head = 0;
		void assign(size_t n)
		{
			buf.assign(n, 0.0f);
			head = 0;
		}
		void push(const std::vector<float> &v) { buf.insert(buf.end(), v.begin(), v.end()); }
		float pop()
		{
			if (head == buf.size())
				return 0.0f;
			const float y = buf[head++];
			if (head >= 4096 && head * 2 >= buf.size()) {
				buf.erase(buf.begin(), buf.begin() + long(head));
				head = 0;
			}
			return y;
		}
	};
	Fifo m_fifoL, m_fifoR;

	using FirstOrder = analog::FirstOrder;
	using Biquad = analog::Biquad;
	using SeriesRc = analog::SeriesRc;
	// U1's inverting stages: C1, C2 4.7u in series with R1, R4 1K and the INPUT pot's track to its
	// wiper (R_a), the rest of the track (R_b) the feedback: -R_b / (1K + R_a + 1 / sC).
	FirstOrder m_stageL, m_stageR;
	double m_potSet = -1;          // the INPUT rotation the stages were set for
	double m_stageNoiseGain = 1;   // 1 + R_b / (1K + R_a): the stage's + input noise to its output
	FirstOrder m_preEmph;          // U4 (pins 1-3): R23 75K, R24 20K, C43 330p, at the input rate
	double m_envDecay = 0;         // R27 330K and C45 0.1u, per input-rate step
	double m_shAlpha = 0.5;        // the sample-and-holds' share of a step taken per sample
	double m_shL = 0, m_shR = 0;   // held through the DSP sample
	Biquad m_reconL, m_reconR;     // U4's Sallen-Key low-passes (R29-R31 1.1K, C46, C50 .01u, C48, C49 3300p)
	FirstOrder m_summerL, m_summerR; // U2's summers' C26, C27 150p across R7, R8 20K
	SeriesRc m_dryInL, m_dryInR;   // R32, R33 20K and C9, C10 4.7u into the CEM3381's inputs
	uint8_t m_dryCode = 0xff;      // latch 0600h b0-b4 as last solved
	SolverStats m_dryStats;        // updateDryCv's solves
	double m_cvTarget = 0, m_cvCoef = 0, m_cv = 0; // VCA C.V. (C52 0.1u)
	FirstOrder m_couplingL, m_couplingR; // C4, C5 4.7u into the OUTPUT pot
	FirstOrder m_bufferL, m_bufferR;     // U2's output stages: R10, R13 2K, R11, R12 2K, C8, C6 4.7u
	double m_envelope = 0;

	// noise
	bool m_hiss = true;
	Noise m_noise{0x5bd1e995u};
	double m_sigma = 0; // white noise of 1 V/sqrt(Hz) at the analog stages' rate, per step

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

} // namespace qv
