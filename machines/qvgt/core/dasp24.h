// The DASP 24 ("ZAK 24 ASIC" in the GT's parts list): Alesis's effects DSP, as decoded from the
// firmware (docs/dasp24.md). A working hypothesis: every factory program behaves as its parameters
// say, but nothing has been checked against a unit.
//
// Program: 96 words of 4 bytes at control-store addresses 000h-17Fh, run once a sample:
//   bytes 0-1  delay-memory offset (little endian), from a pointer that steps down once a sample
//   byte 2     coefficient magnitude, bits 10-3
//   byte 3     bits 7-5: coefficient magnitude, bits 2-0; bit 4: negate; bit 3: write (store ACC;
//              the multiplicand is ACC); bit 2: latch the multiplicand into R[bit 0]; bits 1-0:
//              the addend (0, ACC, R0, R1; for a latch, bit 1 adds ACC)
// I/O: instruction 94's write takes the A/D's sample; the reads of instructions 0 and 95 drive the
// left and right D/A. The pointer steps between instructions 94 and 95.
// Arithmetic (fixed point, from the board: 20-bit DRAM words, 16-bit converters): a 24-bit
// saturating accumulator, 20-bit memory, an 11-bit coefficient, truncated products and stores.
#pragma once

#include <cstdint>
#include <vector>

namespace qvgt {

class Dasp24 {
public:
	static constexpr int kWords = 96;
	static constexpr int kProgramBytes = 4 * kWords; // 180h

	Dasp24() : m_mem(65536, 0) {}
	void reset();
	// One sample period over the program `ctrl` (kProgramBytes). The A/D input and the outputs are
	// the converters' 16-bit codes as fractions of full scale, [-1, 1).
	void tick(const uint8_t *ctrl, double adc, double &outL, double &outR);
	// How many delay-memory words are not zero, and the largest magnitude among them (tests).
	int nonZeroWords(int32_t *largest = nullptr) const;

	bool overflow = false; // the OVFLO pin (the clip LED): set on saturation, cleared by the caller

private:
	std::vector<int32_t> m_mem; // 20-bit words
	int32_t m_acc = 0, m_r[2] = {0, 0};
	uint16_t m_base = 0;
};

} // namespace qvgt
