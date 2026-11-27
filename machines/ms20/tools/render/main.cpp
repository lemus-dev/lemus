// ms20render: plays notes on the machine (core only) and writes SIGNAL OUT as a WAV, and the board's
// signals as a CSV or a summary.
//
//   ms20render OUT.wav [--rate HZ] [--seconds S] [--note KEY,ON,OFF]... [--set NAME=VALUE]...
//              [--probe A,B,...] [--csv FILE] [--summary] [--rev2]
// --rev2: the Rev 2 filters (KLM-307, Machine::setFilterRev).
//
// KEY is the keyboard's key (0 = the bottom C, 36 the top), ON and OFF in seconds. --set takes a
// Controls field by its parameter id (lpf_cutoff=5, vco1_wave=1...). --summary prints each probed
// signal's minimum, maximum and RMS, and SIGNAL OUT's peak and RMS in dBFS.
#include "ms20/core/machine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

void writeWav(const char *path, const std::vector<float> &x, int rate)
{
	FILE *f = std::fopen(path, "wb");
	if (f == nullptr)
		return;
	const uint32_t n = uint32_t(x.size()), bytes = n * 4;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + bytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(3);
	u16(1);
	u32(uint32_t(rate));
	u32(uint32_t(rate) * 4);
	u16(4);
	u16(32);
	std::fwrite("data", 1, 4, f);
	u32(bytes);
	std::fwrite(x.data(), 4, n, f);
	std::fclose(f);
}

bool setControl(ms20::Controls &c, const std::string &name, double v)
{
	std::map<std::string, double *> knobs = {
		{"vco1_pw", &c.vco1Pw}, {"vco2_pitch", &c.vco2Pitch}, {"portamento", &c.portamento}, {"master_tune", &c.masterTune},
		{"fm_mg", &c.fmMg}, {"fm_eg1", &c.fmEg1}, {"vco1_level", &c.vco1Level}, {"vco2_level", &c.vco2Level},
		{"hpf_cutoff", &c.hpfCutoff}, {"hpf_peak", &c.hpfPeak}, {"hpf_mg", &c.hpfMg}, {"hpf_eg2", &c.hpfEg2},
		{"lpf_cutoff", &c.lpfCutoff}, {"lpf_peak", &c.lpfPeak}, {"lpf_mg", &c.lpfMg}, {"lpf_eg2", &c.lpfEg2},
		{"mg_wave", &c.mgWave}, {"mg_freq", &c.mgFreq}, {"eg1_delay", &c.eg1Delay}, {"eg1_attack", &c.eg1Attack},
		{"eg1_release", &c.eg1Release}, {"eg2_hold", &c.eg2Hold}, {"eg2_attack", &c.eg2Attack}, {"eg2_decay", &c.eg2Decay},
		{"eg2_sustain", &c.eg2Sustain}, {"eg2_release", &c.eg2Release}, {"volume", &c.volume}, {"esp_level", &c.espLevel},
		{"esp_lowcut", &c.espLowCut}, {"esp_highcut", &c.espHighCut}, {"esp_cvadj", &c.espCvAdj}, {"esp_threshold", &c.espThreshold},
	};
	if (auto it = knobs.find(name); it != knobs.end()) {
		*it->second = v;
		return true;
	}
	if (name == "vco1_wave")
		c.vco1Wave = ms20::Vco1Wave(int(v));
	else if (name == "vco2_wave")
		c.vco2Wave = ms20::Vco2Wave(int(v));
	else if (name == "vco1_scale")
		c.vco1Scale = int(v);
	else if (name == "vco2_scale")
		c.vco2Scale = int(v);
	else
		return false;
	return true;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: ms20render OUT.wav [--rate HZ] [--seconds S] [--note KEY,ON,OFF]... [--set N=V]... "
		                     "[--probe A,B] [--csv FILE] [--summary] [--rev2]\n");
		return 2;
	}
	const char *wav = argv[1];
	double rate = 48000, seconds = 1;
	struct Note {
		int key;
		double on, off;
	};
	std::vector<Note> notes;
	ms20::Controls c;
	std::vector<std::string> probes;
	const char *csv = nullptr;
	bool summary = false, rev2 = false;
	for (int i = 2; i < argc; ++i) {
		const std::string a = argv[i];
		auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "--rate")
			rate = std::atof(next().c_str());
		else if (a == "--seconds")
			seconds = std::atof(next().c_str());
		else if (a == "--note") {
			Note n{};
			std::sscanf(next().c_str(), "%d,%lf,%lf", &n.key, &n.on, &n.off);
			notes.push_back(n);
		} else if (a == "--set") {
			const auto s = next();
			const auto eq = s.find('=');
			if (eq == std::string::npos || !setControl(c, s.substr(0, eq), std::atof(s.c_str() + eq + 1))) {
				std::fprintf(stderr, "unknown control: %s\n", s.c_str());
				return 2;
			}
		} else if (a == "--probe") {
			std::stringstream ss(next());
			std::string p;
			while (std::getline(ss, p, ','))
				probes.push_back(p);
		} else if (a == "--csv")
			csv = argv[++i];
		else if (a == "--summary")
			summary = true;
		else if (a == "--rev2")
			rev2 = true;
	}

	ms20::Machine m;
	if (rev2)
		m.setFilterRev(ms20::Machine::FilterRev::Rev2);
	m.setControls(c);
	m.prepare(rate);
	const int n = int(seconds * rate);
	std::vector<float> out(static_cast<size_t>(n));
	std::vector<int> idx;
	for (const auto &p : probes)
		idx.push_back(m.board().signalIndex(p));
	std::vector<std::vector<double>> trace(probes.size());
	for (int i = 0; i < n; ++i) {
		const double t = i / rate;
		for (const auto &note : notes) {
			if (i == int(note.on * rate))
				m.setKey(note.key, true);
			if (i == int(note.off * rate))
				m.setKey(note.key, false);
		}
		m.render(&out[size_t(i)], 1, nullptr);
		for (size_t k = 0; k < idx.size(); ++k)
			trace[k].push_back(idx[k] >= 0 ? m.board().signalAt(idx[k]) : 0.0);
		(void)t;
	}
	writeWav(wav, out, int(rate));
	if (std::getenv("MS20_PROFILE") != nullptr)
		m.board().report();
	if (csv != nullptr) {
		FILE *f = std::fopen(csv, "w");
		std::fprintf(f, "t,out");
		for (const auto &p : probes)
			std::fprintf(f, ",%s", p.c_str());
		std::fprintf(f, "\n");
		for (int i = 0; i < n; ++i) {
			std::fprintf(f, "%.7f,%.7g", i / rate, out[size_t(i)]);
			for (size_t k = 0; k < probes.size(); ++k)
				std::fprintf(f, ",%.7g", trace[k][size_t(i)]);
			std::fprintf(f, "\n");
		}
		std::fclose(f);
	}
	if (summary) {
		double peak = 0, sum = 0;
		for (float v : out) {
			peak = std::max(peak, double(std::fabs(v)));
			sum += double(v) * v;
		}
		std::printf("SIGNAL OUT: peak %.1f dBFS, rms %.1f dBFS\n", 20 * std::log10(peak + 1e-30), 10 * std::log10(sum / n + 1e-30));
		for (size_t k = 0; k < probes.size(); ++k) {
			double lo = 1e30, hi = -1e30, s2 = 0;
			for (double v : trace[k]) {
				lo = std::min(lo, v);
				hi = std::max(hi, v);
				s2 += v * v;
			}
			std::printf("%-10s %s min %+.5g max %+.5g rms %.5g\n", probes[k].c_str(), idx[k] < 0 ? "(none)" : "", lo, hi, std::sqrt(s2 / n));
		}
	}
	return 0;
}
