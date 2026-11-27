#include "dasp24.h"

#include <algorithm>
#include <cmath>

namespace qv {

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

void Dasp24::load(const uint8_t *ctrl)
{
	for (int i = 0; i < kWords; ++i) {
		const uint8_t *w = ctrl + 4 * i;
		const int op = w[3];
		int32_t k = (w[2] << 3) | (op >> 5);
		if (op & 0x10)
			k = -k;
		m_ops[i] = {uint16_t(w[0] | (w[1] << 8)), k, (op & 8) != 0, (op & 4) != 0, uint8_t(op & 3)};
	}
}

void Dasp24::tick(double adc, double &outL, double &outR)
{
	constexpr int32_t kMax = (1 << 23) - 1, kMin = -(1 << 23);
	const int32_t adc16 = std::clamp(int32_t(std::floor(adc * 32768.0)), -32768, 32767);
	const int32_t adcWord = adc16 * 256;
	int32_t acc = m_acc;
	int32_t *const mem = m_mem.data();
	bool ovf = false;

	// one instruction; `in` stands for ACC in a write (the A/D's word in instruction 94); returns the
	// word read (or 0 for a write) for the D/A
	auto step = [&](const Op &o, uint16_t base, int32_t in) {
		const uint16_t addr = uint16_t(base + o.offset);
		int32_t x, read = 0;
		if (o.write) {
			x = in;
			mem[addr] = x >> 4; // truncated to the 20-bit DRAM word
		} else {
			read = mem[addr];
			x = read * 16;
		}
		int32_t addend;
		if (o.latch) {
			m_r[o.mode & 1] = x;
			addend = (o.mode & 2) ? acc : 0;
		} else
			addend = o.mode == 0 ? 0 : o.mode == 1 ? acc : m_r[o.mode - 2];
		int64_t sum = ((int64_t(x) * o.k) >> 11) + addend;
		if (sum > kMax) {
			sum = kMax;
			ovf = true;
		} else if (sum < kMin) {
			sum = kMin;
			ovf = true;
		}
		acc = int32_t(sum);
		return read;
	};

	uint16_t base = m_base;
	// instruction 0's read drives the left D/A, 95's the right; the pointer steps after 94
	{
		const int32_t read = step(m_ops[0], base, acc);
		if (!m_ops[0].write)
			outL = (read >> 4) / 32768.0;
	}
	for (int i = 1; i < 94; ++i)
		step(m_ops[i], base, acc);
	step(m_ops[94], base, adcWord);
	m_base = --base;
	{
		const int32_t read = step(m_ops[95], base, acc);
		if (!m_ops[95].write)
			outR = (read >> 4) / 32768.0;
	}
	m_acc = acc;
	if (ovf)
		overflow = true;
}

} // namespace qv
