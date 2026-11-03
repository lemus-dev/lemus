#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tr909 {

// The hi-hat's digital half, voicing board 73133090 (service notes p.11, grid C-F 5-9; p.5
// "DIGITAL VOICE GENERATORS"; docs/circuit/hh.md): the RC oscillator IC72c/d (R492, C160), the
// timing flip-flops IC73a/b, the address counter IC71 (4040) and IC70b (4520), the CLOSED/OPEN
// address logic D196-D199 and the stop gate IC72a, the sample ROM IC69 (HN61256P C43) and the data
// latch IC68 (4174). Its outputs are the lines into the analog half, core/netlists/hh.cir: IC68's
// six outputs (inputs d0..d5, into RA9) and IC72a's RUN/STOP (input run).
//
// The latch moves once a sample clock (f_osc / 2, 31.57 kHz) at instants that don't fall on the
// board's steps, and the filters after the D/A pass its first image band (16-47 kHz), so where a
// step falls matters: step() gives each line band-limited, each D/A step placed at its own time
// within the board's steps (a windowed-sinc step, or the line's average over each step), with a
// fixed latency (latency()). docs/circuit/hh.md, "Rates and the D/A's timing", has the
// measurements against ngspice.
class HiHat {
public:
	static constexpr int kRomSize = 0x8000;

	// How a D/A step is spread over the netlist's steps: Sinc, a Kaiser-windowed sinc step 16 steps
	// long (latency 8 steps); Box, the line's average over each step (latency half a step; 0.1-0.15
	// dB more loss at 15-20 kHz at 192 kHz, and more aliasing).
	enum class Smoothing { Sinc, Box };

	// The netlist's inputs for one step, volts.
	struct Lines {
		double d[6] = {}; // IC68 Q outputs into RA9: d[0] into 32R (ROM byte bit 2) .. d[5] into R (bit 7)
		double run = 0;   // IC72a pin 3, RUN/STOP: 5 V while the counter runs
	};

	HiHat();

	// The sample ROM, hn61256p__c43.ic69 in playback (counter) order, 32 KiB.
	void setRom(const uint8_t *data, size_t size);
	bool hasRom() const { return !m_rom.empty(); }

	// The hh netlist's step rate (twice the board's: docs/circuit/hh.md). Rebuilds the smoothing;
	// call reset() after.
	void setRate(double hz);
	double rate() const { return m_rate; }
	void setSmoothing(Smoothing s);
	Smoothing smoothing() const { return m_smoothing; }
	// The lines' delay against the netlist's other inputs, in steps: feed it trig, ohsel and acc
	// that many whole steps late to line them up.
	double latency() const { return m_latency; }

	// The sample clock: half the astable's frequency. Default from R492 and C160 with the
	// astable's period constant fitted to the measured 31.57 kHz (kAstable).
	void setClock(double hz);
	double clock() const { return 0.5 / m_halfPeriod; }

	// Power-on, as after the firmware's power-up write (400A = 3Ch, a closed hat with the
	// cymbals): the counter stopped at 0x2000 with CLOSED high, IC68 holding ROM[7FFF], the
	// oscillator stopped.
	void reset();

	// TRIG's rising edge (IC10 Q3, into C159), `delay` seconds into the next step, with the
	// CLOSED/OPEN line latched with it (IC10 Q1; true for a closed hat).
	void start(bool closed, double delay = 0);
	// The CLOSED/OPEN line changing on its own (the firmware changes it only with a hi-hat
	// trigger; the logic follows it either way).
	void setClosed(bool closed, double delay = 0);

	// One step of the netlist: its lines.
	const Lines &step();
	const Lines &lines() const { return m_out; }

	bool running() const { return m_run; } // RUN high: the oscillator runs, the counter counts
	// Stopped, nothing pending, and the lines settled: step() gives the same lines again.
	bool idle() const { return !m_run && m_pending == 0 && m_settled; }

	// For tools and tests.
	unsigned count() const { return m_count; } // IC71 + IC70b, bits 0-15
	bool closed() const { return m_closed; }
	int code() const { return m_code; }        // IC68's latched code, 0-63
	double time() const { return m_time; }     // seconds since reset() at the end of the last step
	// Every change of IC68's outputs and of RUN/STOP, with its exact time (seconds since reset()),
	// while a log is set (nullptr: none): the SPICE comparison's input.
	struct Event {
		double t;
		int code;
		bool run;
	};
	void setLog(std::vector<Event> *log) { m_log = log; }

	// Parts (service notes p.11) and the fitted astable constant.
	static constexpr double kR492 = 12e3, kC160 = 470e-12; // the astable's timing R and C
	static constexpr double kAstable = 2.808; // its period / (R492 C160): 63.14 kHz, measured
	static constexpr double kR491 = 100e3, kC159 = 100e-12; // TRIG's differentiator into the resets
	static constexpr double kVdd = 5;
	static constexpr unsigned kClosedLead = 65; // the closed region's unplayed lead-in in the image (address())

private:
	void edge(bool a);        // an oscillator edge: A (IC72c pin 10) or B (IC72d pin 11) rising
	void latch();             // IC68 takes the ROM's output
	void clockCounter();      // IC71's clock falls (IC73b Q)
	void updateRun();         // IC72a from the counter and CLOSED
	unsigned address() const; // IC69's address lines from the counter, A13/A14 ORed with CLOSED
	void lineStep(int line, double t, double dv); // a line moves by dv at t (seconds into this step)

	std::vector<uint8_t> m_rom;
	double m_rate = 192000, m_dt = 1.0 / 192000;
	double m_halfPeriod = 0.5 * kAstable * kR492 * kC160;
	Smoothing m_smoothing = Smoothing::Sinc;
	double m_latency = 8;

	// the logic
	unsigned m_count = 0x2000, m_address = 0;
	bool m_closed = true, m_run = false;
	bool m_oscOn = false, m_nextA = true; // the oscillator's next edge: A or B rising
	double m_tNext = 0;                   // when, seconds into this step
	double m_tNow = 0;                    // the event being handled, seconds into this step
	double m_resetUntil = -1;             // the flip-flops and counters are held reset until then
	bool m_qa = false, m_qb = false;      // IC73a, IC73b Q
	int m_code = 32;
	double m_time = 0;

	// external events within the coming steps
	struct Pending {
		double t; // seconds into this step
		bool trig, closed;
	};
	Pending m_queue[8];
	int m_pending = 0;

	// The lines: their levels (logic, 0 or kVdd), and the smoothing's pending contributions, per
	// line, for this step and the next ones (a ring of kRing).
	static constexpr int kLines = 7; // d0..d5, run
	static constexpr int kRing = 32;
	double m_level[kLines] = {};
	double m_ring[kLines][kRing] = {};
	double m_done[kLines][kRing] = {}; // steps that finish at a later sample: the level from then on
	double m_base[kLines] = {};        // the smoothed level that has settled
	int m_pos = 0;
	int m_width = 16; // the kernel's length in steps
	std::vector<double> m_table; // Sinc: the step's integral S(x) over [0, width], kTableRes a step
	static constexpr int kTableRes = 256;
	bool m_settled = true;
	int m_sinceEdge = 0; // steps since a line last moved
	Lines m_out;
	std::vector<Event> *m_log = nullptr;
};

} // namespace tr909
