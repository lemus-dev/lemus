// The MS-20's boards as one rack of netlists: each block (core/netlists/<block>.cir) runs on the
// solver, and the signals between them go by the interface names of docs/circuit/README.md. A
// block's input that is a patch-panel jack takes the cable's signal when one is plugged in, else
// the signal its switch normals it to; any other input takes the named output of the block that
// makes it. Blocks run in a fixed order each sample; an input made by a later block (a cable patched
// backwards, the MG into the VCOs) reads that block's last sample, as one sample of cable delay.
#pragma once

#include "circuit.h"
#include "patch.h"
#include "solver_stats.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ms20 {

class Board {
public:
	// The blocks, in the order they run, with the netlist texts (models prepended).
	struct BlockSpec {
		std::string name;
		std::string text;
		int group = 0;        // step(group) runs the group's blocks; a group has its own rate
		int over = 1;         // runs at `over` times its group's rate, `over` steps a sample
		double linTol = 0;    // linear mode's tolerance (0: the solver's default)
		bool sparse = false;  // the sparse Newton
		double jump = 0;      // substep ahead when an input moves more than this (volts) in a sample
		bool hold = false;    // the solver's holds (Circuit::setHold)
		bool decimate = false; // outputs filtered from the quarters, kDecimDelay samples late (Circuit::setDecimation)
	};
	void load(const std::vector<BlockSpec> &blocks);
	// Each group's rate (the control blocks at the host's, the audio path at the oversampled one).
	void setRate(int group, double rate);
	// A block's steps per sample of its group, changed while running (the circuit keeps its state,
	// as for a control change). Above 1 the block's outputs come one sample late from the first of
	// the steps, so with the circuit's decimation (kDecimDelay steps) a block at 2 is exactly as
	// late as at 1: 2 x kDecimDelay steps of its own.
	void setOver(const std::string &block, int over);

	// A control by name in every block that has it (the panel's parameter ids, `<id>_<position>`
	// for a switch position, `trim_<VR>` for a trim, `key_<n>` for a key contact), 0-1.
	void setControl(const std::string &name, double value);
	// In one block only (a trim's name, trim_vr2, is used on three boards).
	void setControl(const std::string &block, const std::string &name, double value);
	// A capacitor's voltage in one block (a power-on state the operating point doesn't give).
	void setCapacitor(const std::string &block, const std::string &cap, double volts);
	// A signal from outside the rack (the plugin's audio input, the noise source), by name.
	void setExternal(const std::string &name, double value);
	// an external's index (made if no block makes or reads it), for setExternalAt in a sample loop
	int externalIndex(const std::string &name) { return signalFor(name); }
	void setExternalAt(int index, double value) { m_values[size_t(index)] = value; }

	void setPatch(const Patch &p) { m_patch = p; m_wired = false; }
	// The signals the machine reads itself (SIGNAL OUT, the VCOs' charge currents...): the blocks
	// that make them, and the blocks those take signals from, run; the rest (the ESP with nothing
	// patched from it) wait until a cable needs them.
	void setWanted(std::vector<std::string> names) { m_wanted = std::move(names); m_wired = false; }
	bool running(const std::string &block) const;
	// Per block: seconds spent stepping and the solver's statistics (MS20_PROFILE=1 measures time).
	void report() const;
	// The solvers' statistics (solver_stats.h): every block's summed, or one block's.
	SolverStats solverStats() const;
	SolverStats solverStats(const std::string &block) const;
	std::vector<std::string> blockNames() const;
	// Per block, seconds spent stepping so far (MS20_PROFILE=1), in blockNames()' order.
	std::vector<double> blockSeconds() const;

	// Settles every block at its operating point.
	void settle();
	// POWER (Machine::switchOn): the supplies at `scale` times their ±15 V in every block
	// (Circuit::setConstantScale), 1 as the netlists have them; and the running blocks switched off
	// long ago, every capacitor and signal they make at 0 V (Circuit::discharge). The blocks that
	// wait (setWanted) keep their state.
	void setSupply(double scale, bool moving = false);
	double supply() const { return m_supply; }
	void discharge();
	// For tools: every running block's capacitor voltages, block by block in order.
	std::vector<double> capVoltages() const;
	// One sample of a group's blocks at the group's rate.
	void step(int group);
	// One block by itself (Machine's MG while asleep): its index by name (-1 if none), one sample
	// of it alone on the board's signals, its state saved and restored (Circuit::copyState).
	int blockIndex(const std::string &name) const;
	int capIndex(int block, const std::string &cap) const;
	void stepAlone(int block);
	void stateOf(int block, circuit::Circuit::State &into) const;
	void setStateOf(int block, const circuit::Circuit::State &s);

	// Two threads (Machine::render, "Two threads"): the `side` group's blocks step on a copy of the
	// signals (sideValues()), a sample behind the other groups, which step on the board's. The
	// externals the side's thread sets itself (the VCOs' outputs) count as made by the side.
	void setSide(int group, std::vector<int> externals);
	// Whether the side can run apart, as the board is wired now (it wires first): no block outside
	// it reads a signal the side makes, and no signal a block reads is made on both. Then
	// sideInputs() lists the signals the side's blocks read that the others make or the caller sets
	// (copied into the side's signals each sample).
	bool splittable();
	// Counts the board's wirings (a patch, the wanted signals): what splittable() says holds until it changes.
	long wiring() const { return m_wiring; }
	const std::vector<int> &sideInputs() const { return m_sideIn; }
	// beginSide copies the board's signals to the side's; stepSide is step(side) on the side's
	// signals; endSide copies what the side makes back to the board's.
	void beginSide();
	void stepSide() { stepOn(m_sideGroup, m_side.data()); }
	double *sideValues() { return m_side.data(); }
	void endSide();

	double signal(const std::string &name) const;
	int signalIndex(const std::string &name) const;
	double signalAt(int index) const { return m_values[size_t(index)]; }
	// The signals by index, for a sample loop (as signalAt, setExternalAt).
	double *values() { return m_values.data(); }

	// The jack each interface name is (core/patch.h), and what an unpatched jack is normalled to.
	static const std::map<std::string, Jack> &jackOf();
	static const std::map<Jack, std::string> &normalOf();
	static const char *nameOf(Jack j);

private:
	struct Block {
		std::string name;
		circuit::Circuit circuit;
		int group = 0, over = 1;
		bool needed = true;
		double seconds = 0, worst = 0; // total, and the most one sample took
		double jump = 0;                // an input moving more than this in a sample: substep ahead
		long dcFailures = 0;            // settle()'s operating points that didn't converge
		std::vector<double> last;       // each input's last value
		std::vector<double> held;       // over 2: the outputs after the first step, published next sample
		bool heldValid = false;
		std::vector<std::pair<int, int>> inputs;  // circuit input, wire (index into m_wires)
		std::vector<std::pair<int, int>> outputs; // circuit output, signal
	};
	struct Wire {
		std::string name;
		int signal = -1; // the signal it reads; -1 = zero
	};
	int signalFor(const std::string &name);
	void wire();
	void stepOn(int group, double *values);
	void splitPlan(); // wire()'s last part: whether the side can run apart, its inputs and outputs

	std::vector<std::unique_ptr<Block>> m_blocks;
	std::map<std::string, int> m_signalIndex;
	std::vector<double> m_values;
	std::vector<Wire> m_wires; // one per distinct input name
	std::map<std::string, int> m_wireIndex;
	Patch m_patch;
	bool m_wired = false;
	std::map<int, double> m_rates;
	std::vector<std::string> m_wanted;
	double m_supply = 1;
	// two threads: the side group, the externals its thread sets, its copy of the signals, the
	// signals copied in each sample and out at the end, whether it can run apart
	int m_sideGroup = -1;
	std::vector<int> m_sideExternals, m_sideIn, m_sideOut;
	std::vector<double> m_side;
	std::vector<char> m_planFlags; // splitPlan's scratch: four flags a signal
	bool m_splittable = false;
	long m_wiring = 0;
};

} // namespace ms20
