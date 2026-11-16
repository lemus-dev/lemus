#include "dasp24.h"

#include <algorithm>
#include <cmath>

namespace qvgt {

void Dasp24::reset()
{
	std::fill(m_mem.begin(), m_mem.end(), 0);
	m_acc = m_r[0] = m_r[1] = 0;
	m_base = 0;
	overflow = false;
}

int Dasp24::nonZeroWords(int32_t *largest) const
{
	int n = 0;
	int32_t big = 0;
	for (const int32_t w : m_mem)
		if (w != 0) {
			++n;
			big = std::max(big, w < 0 ? -w : w);
		}
	if (largest != nullptr)
		*largest = big;
	return n;
}

void Dasp24::tick(const uint8_t *ctrl, double adc, double &outL, double &outR)
{
	constexpr int32_t kMax = (1 << 23) - 1, kMin = -(1 << 23);
	// the 16-bit SAR conversion, left-justified in the 24-bit datapath
	const int32_t adc16 = std::clamp(int32_t(std::floor(adc * 32768.0)), -32768, 32767);
	const int32_t adcWord = adc16 * 256;

	for (int i = 0; i < kWords; ++i) {
		const uint8_t *w = ctrl + 4 * i;
		const uint16_t addr = uint16_t(m_base + (w[0] | (w[1] << 8)));
		const int op = w[3];
		int32_t k = (w[2] << 3) | (op >> 5);
		if (op & 0x10)
			k = -k;
		const bool write = (op & 8) != 0, latch = (op & 4) != 0;
		const int mode = op & 3;

		int32_t x;
		if (write) {
			// instruction 94 is the A/D's slot: the sample replaces ACC as the data and multiplicand
			x = i == 94 ? adcWord : m_acc;
			m_mem[addr] = x >> 4; // truncated to the 20-bit DRAM word
		} else {
			x = m_mem[addr] * 16;
			// the PCM54 takes the word's top 16 bits
			if (i == 0)
				outL = (m_mem[addr] >> 4) / 32768.0;
			if (i == 95)
				outR = (m_mem[addr] >> 4) / 32768.0;
		}
		int32_t addend;
		if (latch) {
			m_r[mode & 1] = x;
			addend = (mode & 2) ? m_acc : 0;
		} else
			addend = mode == 0 ? 0 : mode == 1 ? m_acc : m_r[mode - 2];
		int64_t sum = ((int64_t(x) * k) >> 11) + addend;
		if (sum > kMax) {
			sum = kMax;
			overflow = true;
		} else if (sum < kMin) {
			sum = kMin;
			overflow = true;
		}
		m_acc = int32_t(sum);
		// the pointer steps between the A/D's write (94) and the right D/A's read (95), so 95
		// already has the next sample's addressing
		if (i == 94)
			--m_base;
	}
}

} // namespace qvgt
