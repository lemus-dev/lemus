// qvanalog: the unit's linear analog stages (core/analog.h) as the model runs them, their frequency
// response at the rate the unit runs them at (4 x the DSP's), for tools/spice/ac.py to compare with
// ngspice.
//
//   qvanalog BLOCK [PARAM] < frequencies    one frequency (Hz) a line in; "f re im" a line out
//
// Blocks: input T (U1's stage at INPUT rotation T), preemphasis (U4 pins 1-3), reconstruction (U4's
// Sallen-Key), summer (U2's summer from the wet's R34 20K, inverting), output (C4 into the OUTPUT pot
// at the top, U2's output stage), dry G (C9, R33 into the CEM3381 at current gain G, into U2's
// summer).
#include "qv/core/analog.h"
#include "qv/core/board.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace qv::analog;

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: qvanalog input T | preemphasis | reconstruction | summer | output | dry G < freqs\n");
		return 2;
	}
	const std::string block = argv[1];
	const double param = argc > 2 ? std::atof(argv[2]) : 1.0;
	const double dsp = 4 * qv::kSampleRate, in = dsp;
	double f;
	while (std::scanf("%lf", &f) == 1) {
		std::complex<double> h;
		if (block == "input") {
			FirstOrder s;
			inputStage(s, param, in);
			h = s.response(f, in);
		} else if (block == "preemphasis") {
			FirstOrder s;
			preEmphasis(s, in);
			h = s.response(f, in);
		} else if (block == "reconstruction") {
			Biquad b;
			reconstruction(b, dsp);
			h = b.response(f, dsp);
		} else if (block == "summer") {
			FirstOrder s;
			summerPole(s, dsp);
			h = -s.response(f, dsp); // the wet's R34 20K over R7 20K: unity, inverting
		} else if (block == "output") {
			FirstOrder c, o;
			outputCoupling(c, dsp);
			outputStage(o, dsp);
			h = c.response(f, dsp) * o.response(f, dsp);
		} else if (block == "dry") {
			SeriesRc r;
			FirstOrder s;
			r.setup(kDryR, kDryC, dsp);
			summerPole(s, dsp);
			h = -r.response(f, dsp) * param * 20e3 * s.response(f, dsp);
		} else {
			std::fprintf(stderr, "unknown block %s\n", block.c_str());
			return 2;
		}
		std::printf("%.6g %.9g %.9g\n", f, h.real(), h.imag());
	}
	return 0;
}
