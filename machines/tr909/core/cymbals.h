#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tr909 {

// The crash's or ride's digital half, voicing board 73133090 (service notes p.11, grid B-F 11-27;
// p.5 "DIGITAL VOICE GENERATORS"; docs/circuit/cymbals.md): the timing flip-flops (crash IC60a/b,
// ride IC56a/b), the address counter (IC61 4040 then IC58a 4520; IC58b 4520 then IC55 4040), the
// sample ROM (IC62 HN61256P C42; IC54 C44), the data latch (IC63; IC53: 4174) and the gates IC59a/d
// (IC57d/a). The clock is a netlist of its own, crclk.cir or rdclk.cir (the astable IC59b/c or
// IC57b/c with TUNE, and the reset differentiator C50/R189 or C40/R188): Cymbal runs it on its own
// at TUNE positions and times its edges from its output nodes, so the clock's period and its TUNE
// law are the parts'. The outputs are the lines into the analog half, cr.cir or rd.cir: the latch's
// six outputs (d0..d5, into RA10/RA12), the counter's bits 9-14 (e0..e5, into the envelope D/A
// RA11/RA13) and IC59d's (IC57a's) NOT bit 15 (run).
//
// The latch and the counter move once a sample clock (half the astable's 56-112 kHz) at instants
// that don't fall on the board's steps, and the filters after the D/A pass the first image band,
// so where each step falls matters: step() gives each line band-limited, each step placed at its
// own time within the board's steps (a windowed-sinc step, or the line's average over each step),
// with a fixed latency (latency()), as HiHat does. docs/circuit/cymbals.md, "Rates", has the
// measurements against ngspice.
class Cymbal {
public:
	static constexpr int kRomSize = 0x8000;

	// How a line's step is spread over the board's steps: Sinc, a Kaiser-windowed sinc step 16
	// steps long (latency 8 steps); Box, the line's average over each step (latency half a step).
	enum class Smoothing { Sinc, Box };

	// The netlist's inputs for one board step, volts.
	struct Lines {
		double d[6] = {}; // the latch's outputs into RA10/RA12: d[0] into 32R (ROM byte bit 2) .. d[5] into R (bit 7)
		double e[6] = {}; // counter bits 9..14 into RA11/RA13: e[0] into 32R (bit 9) .. e[5] into R (bit 14)
		double run = 0;   // IC59d pin 11 (IC57a pin 3): NOT counter bit 15, 5 V while the counter runs
	};

	// The clock as its netlist times it at one TUNE position: from the stopped clock (RUN low, the
	// astable settled), TRIG and RUN rising at 0. Seconds.
	struct Timing {
		double reset = 0;      // the reset line falls through the inputs' threshold: the flip-flops and counters let go
		double first[4] = {};  // the first four rising edges, alternately of A (osca) and B (oscb)
		bool firstIsA = true;  // which comes first
		double ab = 0, ba = 0; // then steadily: A rising to B rising, B rising to A rising
		double period() const { return ab + ba; }
	};

	Cymbal();

	// The sample ROM in playback (counter) order, 32 KiB: hn61256p__c42.ic62 (crash) or
	// hn61256p__c44.ic54 (ride).
	void setRom(const uint8_t *data, size_t size);
	bool hasRom() const { return !m_rom.empty(); }

	// The clock: models.cir and crclk.cir (rdclk.cir) as one text, with the voice's .param overrides
	// (r442=33k for a crash before SN 392600). Times it at kTunePoints TUNE positions (about 0.4 s of
	// CPU, on up to four threads, once per netlist and parameters in a process); false if the
	// netlist doesn't parse or doesn't oscillate.
	bool setClock(const std::string &netlist, const std::map<std::string, double> &params = {});
	// TUNE (VR25, VR27), rotation 0-1: the clock's timing, interpolated between the positions.
	void setTune(double rotation);
	double tune() const { return m_tune; }
	const Timing &timing() const { return m_timing; } // at the current TUNE
	double sampleRate() const { return 0.5 / m_timing.period(); } // the sample clock: one D/A step every two periods

	// The board's step rate for this voice. Rebuilds the smoothing; call reset() after.
	void setRate(double hz);
	double rate() const { return m_rate; }
	void setSmoothing(Smoothing s);
	Smoothing smoothing() const { return m_smoothing; }
	// The lines' delay against the board's other inputs (trig, acc), in steps.
	double latency() const { return m_latency; }

	// Power-on, as after the firmware's power-up trigger: the counter stopped at 0x8000, the latch
	// holding ROM[7FFF], the clock stopped.
	void reset();

	// TRIG's rising edge (IC10, into C50 / C40), `delay` seconds into the next step.
	void start(double delay = 0);

	// One board step: the lines for the netlist.
	const Lines &step();
	const Lines &lines() const { return m_out; }

	bool running() const { return m_run; } // NOT bit 15 high: the clock runs, the counter counts
	// Stopped, nothing pending, and the lines settled: step() gives the same lines again.
	bool idle() const { return !m_run && m_pending == 0 && m_settled; }

	// For tools and tests.
	unsigned count() const { return m_count; } // bits 0-15
	int code() const { return m_code; }        // the latch's code, 0-63
	double time() const { return m_time; }     // seconds since reset() at the end of the last step
	// Every change of the lines (the latch, the counter's bits 9-15), with its exact time (seconds
	// since reset()), while a log is set (nullptr: none): the SPICE comparison's input.
	struct Event {
		double t;
		int code;     // the latch
		int envelope; // bits 9-14
		bool run;
	};
	void setLog(std::vector<Event> *log) { m_log = log; }
	// The clock's rising edges as Cymbal places them, while a log is set (tools: against the clock
	// netlist run along).
	struct Edge {
		double t;
		bool a;
	};
	void setEdgeLog(std::vector<Edge> *log) { m_edgeLog = log; }

	// The TUNE positions the clock is timed at (every 0.025: between them the cubic is within 4e-5
	// of the period, 2e-4 over the last 0.025), and the timing step (ngspice agrees on the period
	// to 0.01 % at 100 MHz: docs/circuit/cymbals.md, "Rates").
	static constexpr int kTunePoints = 41;
	static constexpr double kClockRate = 100e6;
	static constexpr double kVdd = 5;
	static constexpr double kThreshold = 2.5; // the 4013s' and 4040/4520s' inputs, V_DD / 2
	// Times the clock netlist at one TUNE position, stepping it at `rate`; tools may log the nodes.
	static bool timeClock(const std::string &netlist, const std::map<std::string, double> &params, double tune, double rate,
	                      Timing &out, std::vector<double> *trace = nullptr);

private:
	void advanceTo(double t); // runs the logic to `t` (seconds into this step), integrating the lines
	void edge(bool a);        // a clock edge: A (osca, clocks IC60b) or B (oscb, clocks IC60a) rising
	void latch();             // the 4174 takes the ROM's output
	void clockCounter();      // the 4040's clock (IC60a Q) falls
	void startClock();        // RUN rises with the clock stopped: its first edges from the timing
	void scheduleNext();      // the clock's next edge after the one just handled
	void lineStep(int line, double t, double dv); // a line moves by dv at t (seconds into this step)
	void setLevels();         // the lines' logic levels from the latch, the counter and RUN

	std::vector<uint8_t> m_rom;
	std::vector<Timing> m_table; // at TUNE 0, 0.025, ... 1
	Timing m_timing;
	bool m_timingSet = false; // m_timing is the table's at m_tune
	double m_tune = 0.5;
	double m_rate = 192000, m_dt = 1.0 / 192000;
	Smoothing m_smoothing = Smoothing::Sinc;
	double m_latency = 8;

	// the logic
	unsigned m_count = 0x8000, m_address = 0x7FFF;
	bool m_run = false;
	bool m_oscOn = false, m_nextA = true; // the clock's next edge: A or B rising
	int m_startEdge = 4;                  // the next of Timing::first (4: past them, steady)
	double m_tOsc0 = 0;                   // when the clock started, seconds into this step
	double m_tNext = 0;                   // the next edge, seconds into this step
	double m_tNow = 0;                    // the event being handled, seconds into this step
	double m_resetUntil = -1;             // the flip-flops and counters are held reset until then
	bool m_qa = false, m_qb = false;      // IC60a, IC60b Q (IC56a, IC56b)
	int m_code = 32;
	double m_time = 0;

	double m_queue[8]; // TRIG edges within the coming steps, seconds into this step
	int m_pending = 0;

	// The lines: their levels (logic, 0 or kVdd), and the smoothing's pending contributions, per
	// line, for this step and the next ones (a ring of kRing).
	static constexpr int kLines = 13; // d0..d5, e0..e5, run
	static constexpr int kRing = 32;
	double m_level[kLines] = {};
	double m_ring[kLines][kRing] = {};
	double m_done[kLines][kRing] = {}; // steps that finish at a later step: the level from then on
	double m_base[kLines] = {};        // the smoothed level that has settled
	int m_pos = 0;
	int m_width = 16; // the kernel's length in steps
	std::vector<double> m_table1; // Sinc: the step's integral S(x) over [0, width], kTableRes a step
	static constexpr int kTableRes = 256;
	bool m_settled = true;
	int m_sinceEdge = 0; // steps since a line last moved
	Lines m_out;
	std::vector<Event> *m_log = nullptr;
	std::vector<Edge> *m_edgeLog = nullptr;
};

} // namespace tr909
