// Checks core/noise.*, the noise generator (docs/circuit/noise.md): that its feedback polynomial,
// x^31 + x^13 + 1, is primitive (so the register's period is 2^31 - 1 clocks), that the register
// really has that period, that the output is stage 1's sequence 35 clocks late, and that the
// per-sample average holds the stream's mean. With --raw FILE [SECONDS [RATE [CLOCK]]] it also
// writes the board's samples (float32, volts) for a spectrum.
//
//   c++ -std=c++17 -O2 -I machines machines/tr909/tools/noise/check.cpp machines/tr909/core/noise.cpp -o noisecheck
#include "tr909/core/noise.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using tr909::NoiseGenerator;

namespace {

// GF(2) polynomials of degree < 64 as bit masks (bit k: x^k).
using Poly = uint64_t;

int degree(Poly p)
{
	int d = -1;
	for (int k = 0; k < 64; ++k)
		if (p >> k & 1)
			d = k;
	return d;
}

// a * b mod m, deg a, b < deg m <= 32
Poly mulmod(Poly a, Poly b, Poly m)
{
	const int dm = degree(m);
	Poly r = 0;
	for (int k = degree(b); k >= 0; --k) {
		r <<= 1;
		if (r >> dm & 1)
			r ^= m;
		if (b >> k & 1)
			r ^= a;
	}
	return r;
}

Poly gcd(Poly a, Poly b)
{
	while (b) {
		while (degree(a) >= degree(b) && a)
			a ^= b << (degree(a) - degree(b));
		const Poly t = a;
		a = b;
		b = t;
	}
	return a;
}

bool check(bool ok, const char *what)
{
	std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	return ok;
}

} // namespace

int main(int argc, char **argv)
{
	bool ok = true;

	// 1. x^31 + x^13 + 1 is irreducible: of prime degree, so iff x^(2^31) = x mod p and it has no
	// root (gcd(x^2 + x, p) = 1). Then its roots have order 2^31 - 1 (a prime), so it is primitive.
	const Poly p = (Poly(1) << 31) | (Poly(1) << 13) | 1;
	Poly x = 2;
	for (int k = 0; k < 31; ++k)
		x = mulmod(x, x, p);
	ok &= check(x == 2, "x^(2^31) = x mod x^31 + x^13 + 1");
	ok &= check(gcd(p, 6) == 1, "x^31 + x^13 + 1 has no root in GF(2)");

	// 2. The register's period, from the start-up fill's all-ones state: 2^31 - 1 clocks for stages
	// 1-31, which set what follows (32-36 are a delay; the fill leaves them high, where the period
	// before would have held other bits). 2^31 - 1 is prime, so returning there then and not after
	// one clock proves it is the least period.
	{
		NoiseGenerator g;
		const uint64_t m31 = (uint64_t(1) << 31) - 1;
		const uint64_t start = g.state().bits & m31;
		g.clockOnce();
		ok &= check((g.state().bits & m31) != start, "the state moves");
		const long period = (1L << 31) - 1;
		long ones = long(g.state().bits & 1);
		for (long k = 1; k < period; ++k) {
			g.clockOnce();
			ones += long(g.state().bits & 1);
		}
		ok &= check((g.state().bits & m31) == start, "stages 1-31 back to the start after 2^31 - 1 clocks");
		std::printf("      stage 1 high on %ld of %ld clocks (a maximal sequence: 2^30)\n", ones, period);
		ok &= check(ones == (1L << 30), "2^30 ones a period");
	}

	// 3. Stage 36 is stage 1, 35 clocks late.
	{
		NoiseGenerator g;
		std::vector<int> first;
		bool same = true;
		for (int k = 0; k < 4000; ++k) {
			g.clockOnce();
			first.push_back(int(g.state().bits & 1));
			if (k >= 35 && g.output() != bool(first[size_t(k - 35)]))
				same = false;
		}
		ok &= check(same, "output = stage 1 delayed by 35 clocks");
	}

	// 4. The per-sample average keeps the stream's mean (7.5 V) and stays within 0-15 V.
	{
		NoiseGenerator g;
		g.setRate(96000);
		double sum = 0, lo = 1e9, hi = -1e9;
		const int n = 96000 * 20;
		for (int k = 0; k < n; ++k) {
			const double v = g.next();
			sum += v;
			lo = v < lo ? v : lo;
			hi = v > hi ? v : hi;
		}
		std::printf("      20 s at 96 kHz: mean %.4f V, range %.3f-%.3f V\n", sum / n, lo, hi);
		ok &= check(sum / n > 7.48 && sum / n < 7.52 && lo >= 0 && hi <= 15, "the average's mean and range");
	}

	// 5. Power-on: the output stays high through IC31d's hold, then runs.
	{
		NoiseGenerator g;
		g.setRate(96000);
		g.powerOn();
		const int hold = int(NoiseGenerator::kFillSeconds * 96000);
		bool high = true;
		for (int k = 0; k < hold - 2; ++k)
			high &= g.next() == NoiseGenerator::kHigh;
		bool moves = false;
		for (int k = 0; k < 1000; ++k)
			moves |= g.next() < NoiseGenerator::kHigh;
		ok &= check(high && moves, "power-on hold, then noise");
	}

	if (argc > 2 && !std::strcmp(argv[1], "--raw")) {
		const double seconds = argc > 3 ? std::atof(argv[3]) : 10;
		const double rate = argc > 4 ? std::atof(argv[4]) : 96000;
		NoiseGenerator g;
		g.setRate(rate);
		if (argc > 5)
			g.setClock(std::atof(argv[5]));
		FILE *f = std::fopen(argv[2], "wb");
		if (!f)
			return 2;
		for (long k = 0, n = long(seconds * rate); k < n; ++k) {
			const float v = float(g.next());
			std::fwrite(&v, sizeof v, 1, f);
		}
		std::fclose(f);
	}
	return ok ? 0 : 1;
}
