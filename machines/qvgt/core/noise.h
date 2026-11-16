// Gaussian white noise for the analog parts' own noise: one xoshiro256+ draw a sample, its four
// 16-bit quarters summed (Irwin-Hall, scaled to unit variance; tails cut at 3.46 sigma, which hiss
// doesn't miss). Seeded alike everywhere, so a render is the same every time.
#pragma once

#include <cstdint>

namespace qvgt {

class Noise {
public:
	explicit Noise(uint32_t seed = 0x9e3779b9u) { reseed(seed); }

	void reseed(uint32_t seed)
	{
		uint64_t z = seed;
		for (auto &w : m_s) { // splitmix64 to fill the state
			z += 0x9e3779b97f4a7c15ull;
			uint64_t x = z;
			x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
			x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
			w = x ^ (x >> 31);
		}
	}

	// One sample of zero mean and unit variance.
	double gauss()
	{
		const uint64_t r = next();
		const uint64_t s = (r & 0xffff) + ((r >> 16) & 0xffff) + ((r >> 32) & 0xffff) + (r >> 48);
		// each quarter (k + 1/2) / 65536, mean 1/2; the sum's variance 4/12: sqrt(12 / 4)
		return (double(s) * (1.0 / 65536.0) - (2.0 - 2.0 / 65536.0)) * 1.7320508075688772;
	}

private:
	uint64_t next() // xoshiro256+
	{
		const uint64_t r = m_s[0] + m_s[3], t = m_s[1] << 17;
		m_s[2] ^= m_s[0];
		m_s[3] ^= m_s[1];
		m_s[1] ^= m_s[2];
		m_s[0] ^= m_s[3];
		m_s[2] ^= t;
		m_s[3] = (m_s[3] << 45) | (m_s[3] >> 19);
		return r;
	}
	uint64_t m_s[4]{};
};

} // namespace qvgt
