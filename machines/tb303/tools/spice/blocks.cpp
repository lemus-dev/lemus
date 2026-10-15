// tb303blocks: one block of the voice model on its own, for comparison with a SPICE simulation
// of the same block (tools/spice/compare_*.py). It compiles core/voice.cpp into itself to reach
// the model's internals; the plugin and the other tools link the core library as usual.
//
//   tb303blocks vco FREQ SECONDS FROM OUT.txt
//   tb303blocks vcf INPUT FREQ AMP IF RES BETA TAILMA SECONDS FROM OUT.txt
//   tb303blocks iff CUTOFF ENVMOD RES CAL
//   tb303blocks env CAL KNOBS NOTES SECONDS OUT.txt
//   tb303blocks out INPUT FREQ AMP IABC RES VOLUME SECONDS FROM OUT.txt
//   tb303blocks voice CAL KNOBS saw|square NOTE NOTES SECONDS OUT.txt
//
// vco: the saw core and the Q8 square shaper at the VCO rate, loaded by the VCF input network as
//   with WAVEFORM on square. Writes "t saw square" rows from FROM seconds on, and prints the
//   voltage across C17 at the end (the SPICE run starts it there).
// vcf: the VCF input network and the filter (ladder, Q21 output amplifier, resonance loop) at
//   the internal rate, with ladder current IF (A), RESONANCE at RES, and Q21's beta and tail
//   current (mA) as given. INPUT is the voltage on the VCO side of R62: "saw" or "square" (the
//   VCO at FREQ, as Voice::render runs it), "sine" (8.2 V + AMP sin), or "impulse" (8.2 V plus
//   AMP for one sample at FROM). Writes "t in vcfout" rows (VCF OUT after C14) from FROM on.
// iff: the ladder current at rest for the given knobs and calibration ("service" or "roland").
// env: the control circuits (gate conditioning, MEG, accent, sweep, VEG, cutoff converter) at the
//   host rate for a note list. KNOBS = "cutoff,resonance,envmod,decay,accent"; NOTES =
//   "on,off,accent;..." in seconds (accent 0/1; it is latched from each note's start to the next).
//   Prints the pot fractions and TM3 the SPICE run needs, then writes rows
//   "t gate_int meg i_abc sweep_wiper i_ladder c42" (C42: the VEG capacitor, Q31's base).
// out: the VCA (both input branches, the BA662) and the output stage at the internal rate, fed
//   VCF OUT as INPUT "sine" (AMP sin) or "impulse" (AMP for one sample at FROM), with I_ABC (A),
//   RESONANCE (the wiper is VCF OUT times the loaded pot fraction, as in the VCF) and VOLUME.
//   Writes "t vcfout output vca" rows (the mixer output, AC; the BA662's output, pin 8, AC) from
//   FROM on.
// voice: the whole voice through Voice's public interface at 44.1 kHz, as the plugin runs it,
//   with KNOBS = "cutoff,resonance,envmod,decay,accent,volume", the DAC code NOTE held and NOTES
//   as for env. Prints what the SPICE run needs (pot fractions, TM3, the VCO's frequency, the
//   output's latency in samples), then writes "t jack" rows: the OUTPUT jack voltage.
#include "tb303/core/voice.cpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void useCalibration(tb303::Voice::Impl &v, const std::string &name)
{
	if (name == "roland") {
		v.cal.vcfRingHz = 405;
		v.cal.q21Beta = 435;
		v.cal.q21TailmA = 0.52;
	}
	v.applyCalibration();
}

int vco(int argc, char **argv)
{
	if (argc != 6)
		return 2;
	const double freq = std::atof(argv[2]), seconds = std::atof(argv[3]), from = std::atof(argv[4]);
	FILE *out = std::fopen(argv[5], "w");
	if (out == nullptr)
		return 1;
	tb303::Voice::Impl v;
	v.setRates(176400, 44100);
	v.vCv = 2.75 + std::log2(freq / 110.0);
	const long n = long(seconds * v.fsv);
	for (long i = 0; i < n; ++i) {
		const double t = i * v.hv;
		const double saw = v.sawStep();
		const double sq = v.squareStep(saw, true);
		// VCF input network, as Voice::render runs it (here at the VCO rate).
		v.c17 += v.hv / 1e-6 * (sq - tb303::kVref - v.c17) / 222.2e3;
		if (t >= from)
			std::fprintf(out, "%.9f %.6f %.6f\n", t, saw, sq);
	}
	std::fclose(out);
	std::printf("c17 %.4f\n", v.c17);
	return 0;
}

int vcf(int argc, char **argv)
{
	if (argc != 12 && argc != 13)
		return 2;
	const std::string input = argv[2];
	const double freq = std::atof(argv[3]), amp = std::atof(argv[4]), iF = std::atof(argv[5]), res = std::atof(argv[6]);
	const double seconds = std::atof(argv[9]), from = std::atof(argv[10]);
	FILE *out = std::fopen(argv[11], "w");
	if (out == nullptr)
		return 1;
	tb303::Voice::Impl v;
	v.setRates(176400, 44100);
	v.cal.q21Beta = std::atof(argv[7]);
	v.cal.q21TailmA = std::atof(argv[8]);
	v.applyCalibration();
	if (argc == 13)
		v.ampGain *= std::atof(argv[12]); // experiment: scale Q21's gain (output and loop)
	v.vCv = 2.75 + std::log2(std::max(freq, 1.0) / 110.0);
	const bool square = input == "square";
	// Start C17 charged to the input's mean, as the SPICE operating point does.
	const double mean = input == "saw" || input == "square" ? 0.5 * (decltype(v)::kSawTop + decltype(v)::kSawBottom) : 8.2;
	v.c17 = mean - tb303::kVref;
	const long n = long(seconds * v.fs);
	const long kick = long(from * v.fs);
	for (long i = 0; i < n; ++i) {
		const double t = i * v.h;
		double osc;
		if (input == "saw" || input == "square") {
			for (int o = 0; o < 2; ++o) {
				const double saw = v.sawStep();
				const double sq = v.squareStep(saw, square);
				v.vcoBuf[o] = float(square ? sq : saw);
			}
			osc = v.vcoDown.process(v.vcoBuf);
		} else if (input == "sine") {
			osc = 8.2 + amp * std::sin(2 * tb303::kPi * freq * t);
		} else {
			osc = 8.2 + (i == kick ? amp : 0.0);
		}
		const double i17 = (osc - tb303::kVref - v.c17) / 222.2e3;
		v.c17 += v.h / 1e-6 * i17;
		v.vcfStep(2.2e3 * i17, iF, res);
		if (t >= from)
			std::fprintf(out, "%.9f %.6f %.9f\n", t, osc, v.vcfOut);
	}
	std::fclose(out);
	return 0;
}

int iff(int argc, char **argv)
{
	if (argc != 6)
		return 2;
	tb303::Voice::Impl v;
	v.setRates(176400, 44100);
	useCalibration(v, argv[5]);
	tb303::Controls k;
	k.cutoff = std::atof(argv[2]);
	k.envMod = std::atof(argv[3]);
	k.resonance = std::atof(argv[4]);
	k.decay = 0;
	k.accent = 0;
	tb303::CpuLines lines;
	for (int i = 0; i < 44100; ++i)
		v.controlStep(k, lines, false);
	std::printf("%.6e\n", v.ifNow);
	return 0;
}

std::vector<double> numbers(const std::string &s, char sep)
{
	std::vector<double> v;
	size_t a = 0;
	while (a <= s.size()) {
		const size_t b = s.find(sep, a);
		v.push_back(std::atof(s.substr(a, b == std::string::npos ? std::string::npos : b - a).c_str()));
		if (b == std::string::npos)
			break;
		a = b + 1;
	}
	return v;
}

int env(int argc, char **argv)
{
	if (argc != 7)
		return 2;
	tb303::Voice::Impl v;
	const double fs = 44100;
	v.setRates(176400, fs);
	useCalibration(v, argv[2]);
	const auto kn = numbers(argv[3], ',');
	tb303::Controls k;
	k.cutoff = kn.at(0);
	k.resonance = kn.at(1);
	k.envMod = kn.at(2);
	k.decay = kn.at(3);
	k.accent = kn.at(4);
	struct Note {
		double on, off;
		bool accent;
	};
	std::vector<Note> notes;
	{
		const std::string all = argv[4];
		size_t a = 0;
		while (a < all.size()) {
			size_t b = all.find(';', a);
			if (b == std::string::npos)
				b = all.size();
			const auto n = numbers(all.substr(a, b - a), ',');
			notes.push_back({n.at(0), n.at(1), n.at(2) > 0.5});
			a = b + 1;
		}
	}
	const double seconds = std::atof(argv[5]);
	FILE *out = std::fopen(argv[6], "w");
	if (out == nullptr)
		return 1;
	std::printf("tcut=%.6f tenv=%.6f tdec=%.6f xacc=%.6f wres=%.6f tm3=%.1f\n", tb303::taperA(k.cutoff), tb303::taperA(k.envMod),
	            tb303::taperA(k.decay), tb303::taperB(k.accent), tb303::taperB(k.resonance), v.tm3);
	tb303::CpuLines lines;
	for (long i = 0; i < long(seconds * fs); ++i) {
		const double t = i / fs;
		bool gate = false, accent = false;
		for (size_t n = 0; n < notes.size(); ++n) {
			gate = gate || (t >= notes[n].on && t < notes[n].off);
			const double next = n + 1 < notes.size() ? notes[n + 1].on : 1e9;
			if (t >= notes[n].on - 0.001 && t < next - 0.001)
				accent = notes[n].accent;
		}
		lines.gate = gate;
		lines.accent = accent;
		v.controlStep(k, lines, false);
		std::fprintf(out, "%.6f %.5f %.5f %.4e %.5f %.4e %.5f\n", t, v.gateInt, v.meg, v.iAbcNow, v.sweepWiper, v.ifNow, v.c42);
	}
	std::fclose(out);
	return 0;
}

int out(int argc, char **argv)
{
	if (argc != 11)
		return 2;
	const std::string input = argv[2];
	const double freq = std::atof(argv[3]), amp = std::atof(argv[4]), iAbc = std::atof(argv[5]);
	const double res = std::atof(argv[6]), volume = std::atof(argv[7]);
	const double seconds = std::atof(argv[8]), from = std::atof(argv[9]);
	FILE *o = std::fopen(argv[10], "w");
	if (o == nullptr)
		return 1;
	tb303::Voice::Impl v;
	v.setRates(176400, 44100);
	const double kr = tb303::taperB(res);
	const double wiper = kr * 50e3 / (50e3 + kr * (1 - kr) * 50e3);
	const long n = long(seconds * v.fs), kick = long(from * v.fs);
	v.setVolume(volume);
	for (long i = 0; i < n; ++i) {
		const double t = i * v.h;
		v.vcfOut = input == "sine" ? amp * std::sin(2 * tb303::kPi * freq * t) : (i == kick ? amp : 0.0);
		v.vcfWiper = wiper * v.vcfOut;
		const double vca = v.vcaStep(iAbc);
		const double y = v.outputStage(vca);
		if (t >= from)
			std::fprintf(o, "%.9f %.6f %.9f %.9f\n", t, v.vcfOut, y, vca);
	}
	std::fclose(o);
	return 0;
}

int voice(int argc, char **argv)
{
	if (argc != 9)
		return 2;
	const auto kn = numbers(argv[3], ',');
	tb303::Controls k;
	k.cutoff = kn.at(0);
	k.resonance = kn.at(1);
	k.envMod = kn.at(2);
	k.decay = kn.at(3);
	k.accent = kn.at(4);
	k.volume = kn.at(5);
	k.square = std::string(argv[4]) == "square";
	const int note = std::atoi(argv[5]);
	struct Note {
		double on, off;
		bool accent;
	};
	std::vector<Note> notes;
	{
		const std::string all = argv[6];
		size_t a = 0;
		while (a < all.size()) {
			size_t b = all.find(';', a);
			if (b == std::string::npos)
				b = all.size();
			const auto n = numbers(all.substr(a, b - a), ',');
			notes.push_back({n.at(0), n.at(1), n.at(2) > 0.5});
			a = b + 1;
		}
	}
	const double seconds = std::atof(argv[7]);
	FILE *out = std::fopen(argv[8], "w");
	if (out == nullptr)
		return 1;
	const double fs = 44100;
	// The calibration's TM3, from a bare Impl set up the same way.
	tb303::Voice::Impl probe;
	probe.setRates(176400, fs);
	useCalibration(probe, argv[2]);
	tb303::Voice voice;
	voice.prepare(fs);
	voice.setCalibration(probe.cal);
	voice.setControls(k);
	tb303::CpuLines lines;
	lines.note = note;
	voice.setLines(lines, 0);
	float y = 0;
	for (int i = 0; i < int(0.3 * fs); ++i) // settle with the gate low
		voice.render(&y, 1);
	std::printf("tcut=%.6f tenv=%.6f tdec=%.6f xacc=%.6f wres=%.6f tm3=%.1f tvol=%.6f hz=%.6f latency=%.3f\n", tb303::taperA(k.cutoff),
	            tb303::taperA(k.envMod), tb303::taperA(k.decay), tb303::taperB(k.accent), tb303::taperB(k.resonance), probe.tm3,
	            tb303::taperA(k.volume), voice.probe().vcoHz, voice.latencySamples());
	unsigned triggers = 0;
	bool gateWas = false;
	for (long i = 0; i < long(seconds * fs); ++i) {
		const double t = i / fs;
		bool gate = false, accent = false;
		for (size_t n = 0; n < notes.size(); ++n) {
			gate = gate || (t >= notes[n].on && t < notes[n].off);
			const double next = n + 1 < notes.size() ? notes[n + 1].on : 1e9;
			if (t >= notes[n].on - 0.001 && t < next - 0.001)
				accent = notes[n].accent;
		}
		if (gate && !gateWas)
			++triggers;
		gateWas = gate;
		lines.gate = gate;
		lines.accent = accent;
		voice.setLines(lines, triggers);
		voice.render(&y, 1);
		std::fprintf(out, "%.6f %.6f\n", t, double(y) * tb303::kFullScaleVolts);
	}
	std::fclose(out);
	return 0;
}

} // namespace

int main(int argc, char **argv)
{
	const std::string mode = argc > 1 ? argv[1] : "";
	const int r = mode == "vco" ? vco(argc, argv) : mode == "vcf" ? vcf(argc, argv) : mode == "iff" ? iff(argc, argv) : mode == "env" ? env(argc, argv) : mode == "out" ? out(argc, argv) : mode == "voice" ? voice(argc, argv) : 2;
	if (r == 2)
		std::fprintf(stderr, "usage: tb303blocks vco FREQ SECONDS FROM OUT.txt\n"
		                     "       tb303blocks vcf saw|square|sine|impulse FREQ AMP IF RES BETA TAILMA SECONDS FROM OUT.txt\n"
		                     "       tb303blocks iff CUTOFF ENVMOD RES service|roland\n"
		                     "       tb303blocks env service|roland CUTOFF,RES,ENVMOD,DECAY,ACCENT ON,OFF,ACC;... SECONDS OUT.txt\n"
		                     "       tb303blocks out sine|impulse FREQ AMP IABC RES VOLUME SECONDS FROM OUT.txt\n"
		                     "       tb303blocks voice service|roland CUTOFF,RES,ENVMOD,DECAY,ACCENT,VOLUME saw|square NOTE ON,OFF,ACC;... SECONDS OUT.txt\n");
	return r;
}
