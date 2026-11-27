#include "board.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>

namespace ms20 {

const std::map<std::string, Jack> &Board::jackOf()
{
	static const std::map<std::string, Jack> m = {
		{"totalext", Jack::TotalExtMod}, {"extfreq", Jack::VcoExtFreq}, {"vcocv", Jack::VcoCvIn}, {"vco2cv", Jack::Vco2CvIn},
		{"extsig", Jack::ExtSigIn}, {"hpfext", Jack::HpfExtCutoff}, {"lpfext", Jack::LpfExtCutoff}, {"initgain", Jack::InitialGain},
		{"trig", Jack::TrigIn}, {"eg1trig", Jack::Eg1TrigIn}, {"shin", Jack::ShIn}, {"shclk", Jack::ShClock}, {"mvcain", Jack::MvcaIn},
		{"mvcactl", Jack::MvcaControl}, {"espin", Jack::EspSignalIn},
		{"mg", Jack::MgTriangle}, {"mgsq", Jack::MgRectangle}, {"eg1rev", Jack::Eg1RevOut}, {"eg2rev", Jack::Eg2RevOut},
		{"kbdcv", Jack::KbdCvOut}, {"kbdtrig", Jack::KbdTrigOut}, {"pink", Jack::PinkNoise}, {"white", Jack::WhiteNoise},
		{"shout", Jack::ShOut}, {"mvcaout", Jack::MvcaOut}, {"wheel", Jack::Wheel}, {"mom", Jack::Momentary},
		{"espamp", Jack::EspAmpOut}, {"espfilt", Jack::EspFilterOut}, {"espfv", Jack::EspFvOut}, {"espenv", Jack::EspEnvOut},
		{"esptrig", Jack::EspTrigOut}, {"sigout", Jack::SignalOut}, {"eg1out", Jack::Eg1Out},
	};
	return m;
}

// What an input jack takes with no cable in it (its switch contact). A normal that names another
// input jack takes whatever that jack takes: VCO 2 CV IN follows VCO 1+2 CV IN, cable or not. The
// normals that go through parts are in the netlists instead: EG 1+2 TRIG IN and EG 1 TRIG IN
// (R80 from KBD TRIG, eg1.cir, eg2.cir), MVCA CONTROL (EG 1 through R20, mvca.cir), S&H CLOCK (its
// bias, sh.cir); VCO 1+2 EXT FREQ takes EG 1 as KLM-128's R1/R2 divide it (eg1vco), the filters'
// EXT CUTOFF FREQ EG 2's TO VCF (eg2vcf).
const std::map<Jack, std::string> &Board::normalOf()
{
	static const std::map<Jack, std::string> m = {
		{Jack::TotalExtMod, "mg"},      {Jack::VcoExtFreq, "eg1vco"},   {Jack::VcoCvIn, "kbdcv"}, {Jack::Vco2CvIn, "vcocv"},
		{Jack::HpfExtCutoff, "eg2vcf"}, {Jack::LpfExtCutoff, "eg2vcf"},
	};
	return m;
}

const char *Board::nameOf(Jack j)
{
	for (const auto &[name, jack] : jackOf())
		if (jack == j)
			return name.c_str();
	return "";
}

int Board::signalFor(const std::string &name)
{
	const auto it = m_signalIndex.find(name);
	if (it != m_signalIndex.end())
		return it->second;
	const int i = int(m_values.size());
	m_signalIndex[name] = i;
	m_values.push_back(0);
	return i;
}

int Board::signalIndex(const std::string &name) const
{
	const auto it = m_signalIndex.find(name);
	return it == m_signalIndex.end() ? -1 : it->second;
}

double Board::signal(const std::string &name) const
{
	const int i = signalIndex(name);
	return i < 0 ? 0 : m_values[size_t(i)];
}

void Board::load(const std::vector<BlockSpec> &blocks)
{
	m_blocks.clear();
	m_signalIndex.clear();
	m_values.clear();
	m_wires.clear();
	m_wireIndex.clear();
	for (const auto &spec : blocks) {
		auto b = std::make_unique<Block>();
		b->name = spec.name;
		b->over = spec.over > 0 ? spec.over : 1;
		b->group = spec.group;
		b->jump = spec.jump;
		b->circuit.setName(spec.name);
		b->circuit.load(circuit::Netlist::parse(spec.text));
		if (spec.linTol > 0)
			b->circuit.setLinearTolerance(spec.linTol);
		static const std::string sparseEnv = std::getenv("MS20_SPARSE") ? std::getenv("MS20_SPARSE") : ""; // tools: blocks to try sparse
		if (spec.sparse || (!sparseEnv.empty() && ("," + sparseEnv + ",").find("," + spec.name + ",") != std::string::npos))
			b->circuit.setSparse(true);
		b->circuit.setHold(spec.hold);
		if (spec.decimate && std::getenv("MS20_NODECIM") == nullptr) // tools: outputs at the samples
			b->circuit.setDecimation(true);
		const auto &net = b->circuit.netlist();
		for (size_t o = 0; o < net.outputs.size(); ++o)
			b->outputs.push_back({int(o), signalFor(net.outputs[o].name)});
		for (size_t i = 0; i < net.inputs.size(); ++i) {
			const auto &name = net.inputs[i];
			signalFor(name); // a name nothing on the board makes is an external (setExternal)
			auto it = m_wireIndex.find(name);
			if (it == m_wireIndex.end()) {
				it = m_wireIndex.emplace(name, int(m_wires.size())).first;
				m_wires.push_back({name, -1});
			}
			b->inputs.push_back({int(i), it->second});
		}
		m_blocks.push_back(std::move(b));
	}
	for (auto &b : m_blocks)
		b->circuit.setRate((m_rates.count(b->group) ? m_rates[b->group] : 48000) * b->over);
	m_wired = false;
}

void Board::setRate(int group, double rate)
{
	m_rates[group] = rate;
	for (auto &b : m_blocks)
		if (b->group == group)
			b->circuit.setRate(rate * b->over);
}

void Board::setOver(const std::string &block, int over)
{
	for (auto &b : m_blocks)
		if (b->name == block && b->over != over) {
			// into two steps a sample: the first sample publishes what one step a sample would have
			// (the decimation's next sample), then the first steps' outputs a sample late
			b->heldValid = false;
			if (over == 2 && b->over == 1) {
				b->held.assign(b->outputs.size(), 0.0);
				for (size_t j = 0; j < b->outputs.size(); ++j)
					b->held[j] = b->circuit.pendingOutput(b->outputs[j].first);
				b->heldValid = true;
			}
			const double ratio = double(over) / double(b->over);
			b->over = over;
			b->circuit.setRate((m_rates.count(b->group) ? m_rates[b->group] : 48000) * over);
			b->circuit.retime(ratio);
		}
}

void Board::setControl(const std::string &block, const std::string &name, double value)
{
	for (auto &b : m_blocks)
		if (b->name == block) {
			const int c = b->circuit.netlist().control(name);
			if (c >= 0 && b->circuit.controlValue(c) != value)
				b->circuit.setControl(c, value);
		}
}

void Board::setCapacitor(const std::string &block, const std::string &cap, double volts)
{
	for (auto &b : m_blocks)
		if (b->name == block) {
			const int c = b->circuit.capIndex(cap);
			if (c < 0)
				continue;
			auto st = b->circuit.state();
			st.vc[size_t(c)] = volts;
			b->circuit.setState(st);
		}
}

void Board::setControl(const std::string &name, double value)
{
	for (auto &b : m_blocks) {
		const int c = b->circuit.netlist().control(name);
		if (c >= 0 && b->circuit.controlValue(c) != value)
			b->circuit.setControl(c, value);
	}
}

void Board::setExternal(const std::string &name, double value) { m_values[size_t(signalFor(name))] = value; }

void Board::wire()
{
	// what a name resolves to: a jack's cable or its normal (following normals that name jacks), or
	// the named signal itself
	std::function<int(const std::string &, int)> resolve = [&](const std::string &name, int depth) -> int {
		if (depth > 4)
			return -1;
		const auto jack = jackOf().find(name);
		if (jack != jackOf().end() && isInput(jack->second)) {
			const Jack in = jack->second;
			if (m_patch.patched(in)) {
				const int from = m_patch.from[size_t(in)];
				if (from == kHostInput)
					return signalFor("hostin");
				return signalIndex(nameOf(Jack(from)));
			}
			const auto normal = normalOf().find(in);
			return normal == normalOf().end() ? -1 : resolve(normal->second, depth + 1);
		}
		return signalIndex(name);
	};
	for (auto &w : m_wires)
		w.signal = resolve(w.name, 0);
	// the blocks that run: those making a wanted signal, and what they take signals from
	{
		std::map<int, Block *> maker; // signal -> block
		for (auto &b : m_blocks) {
			b->needed = m_wanted.empty();
			for (const auto &[co, sig] : b->outputs)
				maker[sig] = b.get();
		}
		std::vector<Block *> todo;
		for (const auto &name : m_wanted) {
			const int sig = signalIndex(name);
			if (maker.count(sig) && !maker[sig]->needed) {
				maker[sig]->needed = true;
				todo.push_back(maker[sig]);
			}
		}
		while (!todo.empty()) {
			Block *b = todo.back();
			todo.pop_back();
			for (const auto &[ci, w] : b->inputs) {
				const int sig = m_wires[size_t(w)].signal;
				if (sig >= 0 && maker.count(sig) && !maker[sig]->needed) {
					maker[sig]->needed = true;
					todo.push_back(maker[sig]);
				}
			}
		}
	}
	// the jacks' switches as the netlists draw them: jack_<name> (and the KLM-128's <name>_patch)
	// is 1 with a cable in
	for (const auto &[name, jack] : jackOf())
		if (isInput(jack)) {
			const double in = m_patch.patched(jack) ? 1 : 0;
			setControl("jack_" + name, in);
			setControl(name + "_patch", in);
		}
	m_wired = true;
	++m_wiring;
	splitPlan();
}

void Board::setSide(int group, std::vector<int> externals)
{
	m_sideGroup = group;
	m_sideExternals = std::move(externals);
	m_wired = false;
}

// What the side makes (its running blocks' outputs and its thread's externals) against what the
// others read: a control block that reads an audio signal (a cable from the audio path into the
// control side, ESP SIGNAL IN from SIGNAL OUT) needs that signal at the sample before, which the
// side, a sample behind, hasn't made yet; then the board steps as one.
void Board::splitPlan()
{
	m_splittable = false;
	m_sideIn.clear();
	m_sideOut.clear();
	if (m_sideGroup < 0)
		return;
	const size_t n = m_values.size();
	m_planFlags.assign(4 * n, 0); // (allocates the first time only)
	char *sideMakes = m_planFlags.data(), *othersMake = sideMakes + n, *sideReads = othersMake + n, *othersRead = sideReads + n;
	for (const int x : m_sideExternals)
		if (x >= 0 && size_t(x) < n)
			sideMakes[size_t(x)] = 1;
	for (const auto &b : m_blocks) {
		if (!b->needed)
			continue;
		const bool side = b->group == m_sideGroup;
		for (const auto &[co, sig] : b->outputs)
			(side ? sideMakes : othersMake)[size_t(sig)] = 1;
		for (const auto &[ci, w] : b->inputs) {
			const int sig = m_wires[size_t(w)].signal;
			if (sig >= 0)
				(side ? sideReads : othersRead)[size_t(sig)] = 1;
		}
	}
	for (size_t s = 0; s < n; ++s) {
		if (sideMakes[s] && othersRead[s])
			return; // the side feeds the others
		if (sideMakes[s] && othersMake[s] && sideReads[s])
			return; // made on both sides and read
	}
	for (size_t s = 0; s < n; ++s) {
		if (sideReads[s] && !sideMakes[s])
			m_sideIn.push_back(int(s));
		if (sideMakes[s])
			m_sideOut.push_back(int(s));
	}
	m_side.resize(n);
	m_splittable = true;
}

bool Board::splittable()
{
	if (!m_wired)
		wire();
	return m_splittable;
}

void Board::beginSide()
{
	if (m_side.size() != m_values.size()) // (an external named since the plan: not in a render)
		m_side.resize(m_values.size());
	std::copy(m_values.begin(), m_values.end(), m_side.begin());
}

void Board::endSide()
{
	for (const int s : m_sideOut)
		m_values[size_t(s)] = m_side[size_t(s)];
}

bool Board::running(const std::string &block) const
{
	for (const auto &b : m_blocks)
		if (b->name == block)
			return b->needed;
	return false;
}

void Board::settle()
{
	if (!m_wired)
		wire();
	for (int pass = 0; pass < 2; ++pass) // twice: a block settled before the one feeding it sees its value the second time
		for (auto &b : m_blocks) {
			for (const auto &[ci, w] : b->inputs)
				b->circuit.setInput(ci, m_wires[size_t(w)].signal >= 0 ? m_values[size_t(m_wires[size_t(w)].signal)] : 0);
			if (!b->circuit.solveDc())
				++b->dcFailures;
			for (const auto &[co, s] : b->outputs)
				m_values[size_t(s)] = b->circuit.output(co);
		}
}

void Board::setSupply(double scale, bool moving)
{
	if (scale == m_supply)
		return;
	m_supply = scale;
	for (auto &b : m_blocks)
		b->circuit.setConstantScale(scale, moving);
}

void Board::discharge()
{
	if (!m_wired)
		wire();
	for (auto &b : m_blocks) {
		if (!b->needed)
			continue;
		b->circuit.discharge();
		for (const auto &[co, s] : b->outputs)
			m_values[size_t(s)] = 0;
		b->last.assign(b->inputs.size(), 0.0);
		b->held.assign(b->outputs.size(), 0.0);
		b->heldValid = false;
	}
}

std::vector<double> Board::capVoltages() const
{
	std::vector<double> v;
	for (const auto &b : m_blocks)
		if (b->needed)
			for (int k = 0; k < b->circuit.stateCount(); ++k)
				v.push_back(b->circuit.capVoltage(k));
	return v;
}

void Board::step(int group)
{
	if (!m_wired)
		wire();
	stepOn(group, m_values.data());
}

void Board::stepOn(int group, double *values)
{
	for (auto &b : m_blocks) {
		if (b->group != group || !b->needed)
			continue;
		if (b->last.size() != b->inputs.size())
			b->last.assign(b->inputs.size(), 0.0);
		bool jumped = false;
		for (size_t k = 0; k < b->inputs.size(); ++k) {
			const auto &[ci, w] = b->inputs[k];
			const double v = m_wires[size_t(w)].signal >= 0 ? values[size_t(m_wires[size_t(w)].signal)] : 0;
			jumped = jumped || (b->jump > 0 && std::abs(v - b->last[k]) > b->jump);
			b->last[k] = v;
			b->circuit.setInput(ci, v);
		}
		// a fast input (the keyboard CV's step into the VCO converters' MHz loops): smaller steps
		// from the start, rather than a failed Newton solve and then smaller steps
		if (jumped)
			b->circuit.substeps(1);
		static const bool profile = std::getenv("MS20_PROFILE") != nullptr;
		const auto t0 = profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
		const bool late = b->over == 2;
		for (int k = 0; k < b->over; ++k) {
			b->circuit.step();
			if (late && k == 0) {
				// the first step's outputs, published at the next sample (see setOver)
				if (b->held.size() != b->outputs.size())
					b->held.assign(b->outputs.size(), 0.0);
				if (b->heldValid)
					for (size_t j = 0; j < b->outputs.size(); ++j)
						values[size_t(b->outputs[j].second)] = b->held[j];
				for (size_t j = 0; j < b->outputs.size(); ++j)
					b->held[j] = b->circuit.output(b->outputs[j].first);
			}
		}
		if (profile) {
			const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			// MS20_SLOW=<block>: that block's samples over 15 us, with the solver's running counts
			static const char *slow = std::getenv("MS20_SLOW");
			if (slow != nullptr && b->name == slow && took > 15e-6) {
				const auto &st = b->circuit.stats();
				std::fprintf(stderr, "slow %s step %ld %.1f us: iterations %ld factorizations %ld substepped %ld failures %ld\n", b->name.c_str(),
				             st.steps, took * 1e6, st.iterations, st.factorizations, st.substepped, st.failures);
			}
			b->seconds += took;
			b->worst = std::max(b->worst, took);
		}
		if (late && b->heldValid)
			continue; // published above, from the last sample's first step
		for (const auto &[co, s] : b->outputs)
			values[size_t(s)] = b->circuit.output(co);
		if (late)
			b->heldValid = true;
	}
}

int Board::blockIndex(const std::string &name) const
{
	for (size_t k = 0; k < m_blocks.size(); ++k)
		if (m_blocks[k]->name == name)
			return int(k);
	return -1;
}

int Board::capIndex(int block, const std::string &cap) const
{
	return m_blocks[size_t(block)]->circuit.capIndex(cap);
}

void Board::stepAlone(int block)
{
	auto &b = *m_blocks[size_t(block)];
	for (const auto &[ci, w] : b.inputs)
		b.circuit.setInput(ci, m_wires[size_t(w)].signal >= 0 ? m_values[size_t(m_wires[size_t(w)].signal)] : 0);
	for (int k = 0; k < b.over; ++k)
		b.circuit.step();
	for (const auto &[co, s] : b.outputs)
		m_values[size_t(s)] = b.circuit.output(co);
}

void Board::stateOf(int block, circuit::Circuit::State &into) const
{
	m_blocks[size_t(block)]->circuit.copyState(into);
}

void Board::setStateOf(int block, const circuit::Circuit::State &s)
{
	m_blocks[size_t(block)]->circuit.setState(s);
}

void Board::report() const
{
	for (const auto &b : m_blocks) {
		const auto &st = b->circuit.stats();
		const auto si = b->circuit.sparseInfo();
		std::fprintf(stderr, "%-8s ports %3d states %3d sparse %d(%d nodes) ", b->name.c_str(), b->circuit.portCount(), b->circuit.stateCount(), si.ok, si.nodes);
		std::fprintf(stderr, "%s %8.3f s  worst %7.1f us  steps %9ld  it/step %5.2f  fact/step %5.2f  substepped %7ld  held %7ld  "
		             "softened %5ld  failures %6ld  dc failures %ld  linear %.0f %%\n",
		             b->needed ? "run " : "idle", b->seconds, b->worst * 1e6, st.steps,
		             st.steps ? double(st.iterations) / st.steps : 0.0, st.steps ? double(st.factorizations) / st.steps : 0.0,
		             st.substepped, st.held, st.softened, st.failures, b->dcFailures,
		             st.steps ? 100.0 * double(st.linearSteps) / double(st.steps) : 0.0);
	}
}

namespace {
SolverStats statsOf(const circuit::Circuit &c, long dcFailures)
{
	const auto &st = c.stats();
	SolverStats s;
	s.solves = st.steps;
	s.substepped = st.substepped;
	s.held = st.held;
	s.softened = st.softened;
	s.failures = st.failures;
	s.dcFailures = dcFailures;
	return s;
}
} // namespace

SolverStats Board::solverStats() const
{
	SolverStats s;
	for (const auto &b : m_blocks)
		s += statsOf(b->circuit, b->dcFailures);
	return s;
}

SolverStats Board::solverStats(const std::string &block) const
{
	SolverStats s;
	for (const auto &b : m_blocks)
		if (b->name == block)
			s += statsOf(b->circuit, b->dcFailures);
	return s;
}

std::vector<std::string> Board::blockNames() const
{
	std::vector<std::string> n;
	for (const auto &b : m_blocks)
		n.push_back(b->name);
	return n;
}

std::vector<double> Board::blockSeconds() const
{
	std::vector<double> t;
	for (const auto &b : m_blocks)
		t.push_back(b->seconds);
	return t;
}

} // namespace ms20
