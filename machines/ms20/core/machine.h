// The MS-20: the keyboard, the panel, the patch panel and the circuit behind them, rendering SIGNAL
// OUT at the host's rate. No CPU and no ROM: everything is circuit (docs/research.md).
//
// The blocks are the netlists of docs/circuit/ on the board (core/board.h), in two groups: the
// control side at the host's rate (keyboard and trigger at twice it, docs/circuit/keyboard.md),
// and the audio path (mixer, the two KORG35 filters, the VCA) at kOver times it, which the KORG35s
// need (docs/circuit/filters.md, "Oversampling"); the two VCO cores and shapers are C++ between
// them (core/vco.h, docs/circuit/vco.md §9), driven by the charge currents vcoctl.cir works out.
#pragma once

#include "board.h"
#include "controls.h"
#include "oversampler.h"
#include "patch.h"
#include "vco.h"
#include "worker.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <random>

namespace ms20 {

class Machine {
public:
	static constexpr int kKeys = 37; // C to C, three octaves
	static constexpr int kOver = 4;  // the audio path's oversampling

	// SIGNAL OUT volts to sample value [chosen until a level reference exists]: 0 dBFS = 1 V peak,
	// the specification's 2 Vpp maximum at SIGNAL OUT (service manual §1). The plugin's input is
	// taken at the same scale (a cable from outside at 0 dBFS = 1 V peak).
	static constexpr double kFullScale = 1.0;

	// The trims, each a rotation 0-1, as the adjustment procedure (service manual §7) sets them on
	// this model: tools/service (ms20service calibrate) works them out, the numbers are pasted here.
	struct Calibration {
		double vr4 = 0.4649, vr3 = 0.5413; // KBD CV high (5) and low (6), §7-3
		double vr1 = 0.1128, vr2 = 0.7054; // VCO 1 high (1) and low (2), §7-2, the error split: C-1..C-4 -1.1, +1.0, +1.1, -1.1 cents
		double vr6 = 0.4778, vr5 = 0.6936; // VCO 2 high (3) and low (4), §7-2
		double vr7 = 0.3305, vr8 = 0.3370; // HPF and LPF FC ADJ (1, 2 in fig. 2), §7-4: 500 Hz
		// Rev 2 (KLM-307): FC ADJ by the same §7-4 check; KLM-307's own presets VR1, VR2 (the
		// resonance amplifiers' gain) have no procedure: both fitted to the one Rev 2 recorded on
		// both filters (BwBwJosbQkg): VR1 so the HPF oscillates at its top from PEAK 8, VR2 just under
		// self-oscillation at PEAK 9.5 (improvements.md)
		double vr7b = 0.5347, vr8b = 0.5488;
		double k307vr1 = 0.3, k307vr2 = 0.55;
	};
	void setCalibration(const Calibration &c);
	const Calibration &calibration() const { return m_cal; }

	// The filters' revision (improvements.md, "Rev 2"): Rev1, the two KORG35 modules on KLM-127 as
	// the service manual draws them; Rev2, the later KLM-307 daughterboard (LM13600 OTAs, with the
	// VCA on it too). A unit is one or the other; the plugin's bar picks it. A change rebuilds the
	// board and starts it settled, as a load does.
	enum class FilterRev { Rev1, Rev2 };
	void setFilterRev(FilterRev rev);
	FilterRev filterRev() const { return m_rev; }

	Machine();
	void prepare(double sampleRate);

	// Two threads (machine.cpp, "Two threads"): the control side (keyboard, envelopes, MG...) on a
	// thread of its own, ahead of the audio path on the caller's, bit for bit what one thread gives.
	// On unless MS20_THREADS=1 (tools) or the computer has one core; where it can't apply (a cable
	// from the audio path into the control side, POWER's boot and run-down, near sleep, short
	// stretches between MIDI events) the caller's thread runs everything. The thread starts at
	// prepare() and ends with the machine.
	void setThreaded(bool on);
	bool threaded() const { return m_threaded; }
	// The plugin's hooks for the thread (Worker::Hooks: its priority, the host's audio workgroup),
	// before prepare(); rejoinWorker() has it join the workgroup again before its next job.
	void setWorkerHooks(Worker::Hooks hooks) { m_workerHooks = std::move(hooks); }
	void rejoinWorker()
	{
		if (m_worker)
			m_worker->rejoin();
	}
	// Tools: stretches of samples rendered on two threads (of which the thread never joined:
	// it woke too late), on one; and the samples whose control side the caller ran itself before
	// the thread joined.
	// With MS20_THREADSTATS set, also the seconds the caller spent waiting for the thread's samples.
	struct ThreadStats {
		long split = 0, takenBack = 0, single = 0, early = 0;
		double waited = 0;
	};
	ThreadStats threadStats() const { return m_threadStats; }

	void setControls(const Controls &c);
	void setManual(const Manual &m) { m_manual = m; }
	void setPatch(const Patch &p);
	const Controls &controls() const { return m_controls; }
	const Patch &patch() const { return m_patch; }

	// A key on the keyboard (0 = the bottom C): its contact on the ladder (kbd.cir).
	void setKey(int key, bool down);
	bool key(int key) const { return key >= 0 && key < kKeys && m_keys[size_t(key)]; }
	// The key the keyboard's ladder sounds: the highest one down (docs/circuit/keyboard.md).
	int soundingKey() const;

	// POWER, as the switch does it (the plugin's VOLUME click). switchOn(): the unit boots from cold,
	// every capacitor at 0 V and the rails rising (machine.cpp, "POWER"), and reaches its operating
	// point as the circuit charges; switched on while still running down, the rails rise again from
	// where they are. switchOff(): the rails fall and the circuit runs down with them; once they are
	// under 4.5 V the board stops, SIGNAL OUT dies away as C33 empties, then exact zeros.
	void switchOn();
	void switchOff();
	// POWER for a plugin or session load and the tools: on at the settled operating point (as
	// prepare() starts it), or off at once, silent.
	void powerOn();
	void powerOff();
	// On, or switched off and still running down.
	bool powered() const { return m_power != Power::Off; }
	// The regulated rails as a share of ±15 V: 1 settled, 0 off, between while they rise or fall.
	double rails() const { return m_rail; }

	// SIGNAL OUT for `n` samples at the prepared rate; `in` is the plugin's audio input (null: none).
	void render(float *out, int n, const float *in);

	// Settled and silent: the outputs are exact zeros until a key, a cable, a knob or the input
	// wakes it.
	bool asleep() const { return m_asleep; }
	// For tools: never fall asleep (a reference to compare a sleeping machine against).
	void keepAwake(bool on) { m_keepAwake = on; }
	// For tools: no drift (two machines compared sample for sample over a sleep).
	void setDrift(bool on) { m_driftOn = on; }
	// Each instance its own unit (the plugin, Ms20Processor::reseed): the hiss's and the drift's
	// random streams from `seed`, at once; 0, the fixed streams (the default: tools' renders repeat).
	void setSeed(uint64_t seed);
	// The panel's LEDs: KBD TRIG (lit while a key is held: IC13B low), MG (IC15's rectangle high),
	// the ESP's PEAK and TRIG (their LED currents, when the ESP runs).
	struct Leds {
		bool kbd = false, mg = false, espPeak = false, espTrig = false;
	};
	Leds leds() const;

	// What the circuit solvers did since the machine was built (core/solver_stats.h).
	SolverStats solverStats() const { return m_board.solverStats(); }
	// The board, for tools (signals by name).
	const Board &board() const { return m_board; }
	// the decimator's, and the two KORG35s' filters' (Circuit::setDecimation) at the 4x rate
	// The KORG35 blocks (hpf, lpf) run at the oversampled rate (4x), or twice it while their cutoff
	// is high: there, at 4x, a resonance or self-oscillation folds its harmonics back as grit and
	// ticks (improvements.md). Picked per block from its pin-1 CV with hysteresis: up above
	// kFastCv (about 6.5 kHz), down below kSlowCv (about 5 kHz), at most once in kFilterDwell s. The
	// board keeps a block's delay the same at either rate.
	static constexpr double kFastCv = 2.45, kSlowCv = 2.15, kFilterDwell = 0.05;
	// Rev 2's expo node to the KORG35's pin-1 volts at the same self-oscillation pitch (lpf307.cir)
	static constexpr double kRev2CvScale = 39.92, kRev2CvZero = 13.19e-3;
	double latencySamples() const { return m_decimator.latency() + 2.0 * circuit::Circuit::kDecimDelay / kOver; }

private:
	void build();
	void start();
	void applyControls();
	void wake()
	{
		if (m_asleep)
			wakeUp();
		m_asleep = false;
		m_quiet = 0;
	}

	double m_rate = 48000;
	FilterRev m_rev = FilterRev::Rev1;
	Controls m_controls;
	Manual m_manual;
	Patch m_patch;
	Calibration m_cal;
	std::array<bool, kKeys> m_keys{};
	bool m_controlsDirty = true;
	// POWER: on, off, or switched off and running down; the raw rail (volts) while it charges or
	// discharges, the regulated rails' share the board has (m_rail), whether that still moves; the
	// samples the machine stays awake for (a boot's charging, which a quiet SIGNAL OUT doesn't
	// show); a run-down's samples since the op-amps stopped; samples since the falling rails last
	// reached the board.
	enum class Power { Off, On, Down };
	Power m_power = Power::On;
	double m_raw = 0, m_rail = 1;
	bool m_railsMoving = false;
	long m_awakeFor = 0;
	int m_downQuiet = 0, m_railTick = 0;
	float m_lastOut = 0;    // SIGNAL OUT's last sample while running down
	double m_deadDecay = 1; // its decay a sample once the op-amps have stopped (C33)
	void stepRails();
	void endRunDown();

	Board m_board;
	Vcos m_vcos;
	Decimator m_decimator;
	std::array<float, kOver> m_up{};
	int m_ia1 = -1, m_ia2 = -1, m_white = -1, m_sigout = -1, m_ledv = -1; // signal indices
	bool m_noiseWanted = true; // "white" among the board's wanted signals (applyControls)
	int m_xHostin = -1, m_xNoise = -1, m_xKick = -1, m_xWheel = -1, m_xMom = -1, m_xVco1 = -1, m_xVco2 = -1; // externals
	int m_xRamp1 = -1, m_xRamp2 = -1;
	struct FilterRate {
		const char *block;
		int cv = -1;      // its pin-1 CV's signal
		int over = 1;     // steps a 4x sample now
		int hold = 0;     // samples before it may change again
	};
	std::array<FilterRate, 2> m_filterRate{{{"hpf"}, {"lpf"}}};
	void pickFilterRates(const double *v);
	// the MVCA's CdS cell (PC1), which the solver can't model: its resistance follows the LED
	// current by docs/circuit/klm128.md §3.3's law, stepped only while MVCA OUT is patched
	void stepCds();
	bool m_mvcaUsed = false;
	double m_cdsR = 10e6, m_cdsSet = 10e6; // ohms: now, and what the netlist's PPC1 was last set to
	// Drift (improvements.md, "Drift"): the oscillators' slow wander with temperature, always on.
	// One temperature walk for the board (kelvin from where it was tuned), pulled back to tuned,
	// moves both cores' charge currents: linearly (C2/C11's polystyrene, the scale ladders' carbon
	// resistors: kDriftLinear cents a kelvin) and through each converter's exponential part, whose
	// scale is V_T's (the 2SC1583 pair: a modulation of m octaves at its base moves m T/T0); each
	// oscillator has a walk of its own as well (its parts' own tempcos and place). From tuned at
	// each start; stepped once a render call in closed form, and through sleep.
	static constexpr double kDriftKelvin = 1.0, kDriftTau = 600; // rms, seconds
	static constexpr double kDriftOscCents = 0.3, kDriftOscTau = 600;
	static constexpr double kDriftLinear = 0.6; // cents a kelvin
	bool m_driftOn = true;
	std::array<double, 2> m_sleepDrift{1, 1};   // driftFactor as the machine fell asleep
	double m_driftT = 0;                        // kelvin
	std::array<double, 2> m_driftOsc{};         // cents
	std::mt19937 m_driftRng{21};
	uint32_t m_noiseSeed = 20, m_driftSeed = 21; // setSeed
	int m_b6 = -1, m_b7 = -1, m_vb = -1;         // the converters' antilog bases and the log bases
	void stepDrift(double seconds);
	void resetDrift();
	double driftFactor(int vco, const double *v) const;
	std::mt19937 m_rng{20};
	std::normal_distribution<double> m_gauss{0.0, 1.0};
	double m_noiseSigma = 0;
	int m_kick = 0; // samples of the MG's start-up kick left

	bool m_asleep = false, m_keepAwake = false;
	int m_quiet = 0;

	// Asleep, the board stops, but a real unit's VCOs and MG run on (improvements.md, "Asleep").
	// On waking the VCOs move on at once (Vcos::advance) by the time asleep. The MG runs alone for
	// one cycle after falling asleep, keeping its state at both ends of both halves; from then on
	// its phase follows the clock, and waking puts it where that phase is: within a half every
	// state moves in a straight line (the integrator's ramp on a constant current), so a state
	// between the ends is theirs in proportion. No steps while asleep after that first cycle, and
	// none at the wake.
	// The turns fall between samples: each is where the two halves' lines (C22's voltage against
	// time) meet, so the cycle's length isn't rounded to samples (a sample a half would put a fast
	// MG a quarter-cycle out after a few seconds).
	// The cycle's length is then refined over kMgCycles cycles (Refine: the turn that ends the last,
	// by the same lines), since a turn found to a third of a sample puts a fast MG with a short half
	// (near a ramp) 0.01 % a cycle out; the halves keep the first cycle's proportions.
	struct MgFree {
		enum class Stage { Off, Wait, A, B, End, Refine, Known };
		Stage stage = Stage::Off;
		long long slept = 0;          // samples asleep so far
		long long startA = 0;         // the sample (counted in slept) half A began at (after its turn)
		long long startB = 0;         // and half B
		long long lenA = 0, lenB = 0; // the halves' samples
		double turnA = 0;             // when half A began (its turn, between samples)
		double turnB = 0;             // half B
		double turnNext = 0;          // the next half A, the cycle's end
		long long endAt = 0;          // the next half A's sample its line starts at (Known after it)
		bool high = false;            // the rectangle (J7) high at the last step
		bool highA = false;           // and in half A
		long long endA = 0, endB = 0; // the samples a1 and b1 were taken at
		double halfA = 0, halfB = 0;  // the halves' lengths (refined), samples
		double ref = 0;               // a half A's turn the phase counts from (the latest found)
		double rA = 0;                // C22's slope in half A (the first cycle's), volts a sample
		long long rStartB = -1;       // Refine: the latest half B's first sample (-1: none yet)
		long long rEndB = 0;          // and the sample rb1 was taken at
		long long rAAt = -1;          // the sample kMgSettle into the half A after it
		int rCycles = 0;              // half A turns since the first cycle's end
		circuit::Circuit::State rb0, rb1; // the latest half B's line, as b0, b1
		// a0, b0: kMgSettle samples into each half; a1, b1: kMgEarly samples before each half's
		// end. Near a turn the states are off the half's line: the integrator's op-amp catching up
		// after it, the Schmitt trigger already slewing a sample before the rectangle shows it.
		circuit::Circuit::State a0, a1, b0, b1, now, wait;
		std::array<circuit::Circuit::State, 4> ring; // the last samples' states, by sample % 4
	};
	static constexpr int kMgSettle = 8, kMgEarly = 4, kMgCycles = 4;
	MgFree m_mgFree;
	int m_mgBlock = -1, m_mgRect = -1, m_mgC22 = -1; // the MG's block, its rectangle's signal, C22
	// the sample (counted in slept) whose MG state wakeUp sets, as a half and a share of a0-a1 (b0-b1)
	bool mgPlace(long long sample, bool &inA, double &w) const;
	std::atomic<int> m_mgLed{-1};     // asleep with the MG's cycle known: its LED (0, 1); else -1
	static constexpr double kMgGiveUp = 15; // seconds asleep without a cycle: leave the MG stopped
	void fallAsleep(int rest);
	void sleepTick(int n);
	void wakeUp();

	// one sample's control side: the externals it reads, the control group, the MVCA's cell
	void controlSample(const float *in, int i);
	// one sample's audio path on the signals `v` (the board's, or the side's on two threads):
	// the filters' rates, the VCOs, the audio group (side: Board::stepSide), the decimator
	float audioSample(double *v, bool side, bool noiseWave);

	// Two threads: the stretch of samples a job covers at most (the ring holds them all, so the
	// control side never waits), the shortest worth waking the thread for, the signals carried
	// a sample at most (Board::sideInputs and the VCOs' ia1, ia2, white).
	static constexpr int kSplitMax = 256, kSplitMin = 16, kCarryMax = 24;
	bool m_threaded = true, m_prepared = false, m_timeWaits = false;
	Worker::Hooks m_workerHooks;
	ThreadStats m_threadStats;
	std::vector<int> m_carry; // the signals carried each sample, as the board was wired
	std::vector<double> m_ring; // kSplitMax x kCarryMax
	long m_carryWiring = -1; // Board::wiring() the carry list is for
	int m_jobN = 0;
	const float *m_jobIn = nullptr;
	// (padded to a cache line of their own: the caller polls them while the thread writes; padding
	// rather than alignas, which heap allocation on Windows doesn't always honour)
	[[maybe_unused]] char m_padBefore[64] = {};
	std::atomic<int> m_made{0};     // samples whose control side is done (either thread)
	std::atomic<int> m_handoff{-1}; // the sample the thread takes the control side from
	[[maybe_unused]] char m_padAfter[64] = {};
	bool canSplit(int n);
	void renderSplit(float *out, int n, const float *in, bool noiseWave);
	void controlSamples(int from, int to);
	void controlJob();
	static void controlJobOf(void *self) { static_cast<Machine *>(self)->controlJob(); }
	// last: its thread ends before the rest goes
	std::unique_ptr<Worker> m_worker;
};

} // namespace ms20
