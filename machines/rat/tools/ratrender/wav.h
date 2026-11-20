// Minimal WAV I/O for the command-line tools: reads PCM 16/24/32 and float 32/64 (first
// channel), writes mono float 32.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wav {

inline bool read(const std::string &path, std::vector<double> &out, double &rate)
{
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::vector<unsigned char> d;
	unsigned char buf[65536];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
		d.insert(d.end(), buf, buf + n);
	std::fclose(f);
	if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4))
		return false;
	auto u16 = [&](size_t p) { return uint16_t(d[p] | d[p + 1] << 8); };
	auto u32 = [&](size_t p) { return uint32_t(d[p] | d[p + 1] << 8 | d[p + 2] << 16 | uint32_t(d[p + 3]) << 24); };
	size_t p = 12;
	int fmt = 0, ch = 0, bits = 0;
	while (p + 8 <= d.size()) {
		const uint32_t len = u32(p + 4);
		if (!std::memcmp(d.data() + p, "fmt ", 4)) {
			fmt = u16(p + 8);
			ch = u16(p + 10);
			rate = u32(p + 12);
			bits = u16(p + 22);
			if (fmt == 0xFFFE && len >= 26)
				fmt = u16(p + 32);
		} else if (!std::memcmp(d.data() + p, "data", 4)) {
			const size_t bytes = bits / 8, frame = bytes * size_t(ch);
			const size_t frames = std::min<size_t>(len, d.size() - p - 8) / frame;
			out.resize(frames);
			const unsigned char *s = d.data() + p + 8;
			for (size_t i = 0; i < frames; ++i, s += frame) {
				double v = 0;
				if (fmt == 3 && bits == 32) {
					float x;
					std::memcpy(&x, s, 4);
					v = x;
				} else if (fmt == 3 && bits == 64) {
					std::memcpy(&v, s, 8);
				} else if (bits == 16) {
					v = int16_t(s[0] | s[1] << 8) / 32768.0;
				} else if (bits == 24) {
					int32_t x = s[0] | s[1] << 8 | s[2] << 16;
					if (x & 0x800000)
						x -= 0x1000000;
					v = x / 8388608.0;
				} else if (bits == 32) {
					int32_t x;
					std::memcpy(&x, s, 4);
					v = x / 2147483648.0;
				}
				out[i] = v;
			}
			return true;
		}
		p += 8 + len + (len & 1);
	}
	return false;
}

inline bool write(const std::string &path, const std::vector<double> &x, double rate)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	const uint32_t bytes = uint32_t(x.size() * 4);
	std::fwrite("RIFF", 1, 4, f);
	w32(36 + bytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	w32(16);
	w16(3);
	w16(1);
	w32(uint32_t(rate));
	w32(uint32_t(rate) * 4);
	w16(4);
	w16(32);
	std::fwrite("data", 1, 4, f);
	w32(bytes);
	for (double v : x) {
		const float s = float(v);
		std::fwrite(&s, 4, 1, f);
	}
	std::fclose(f);
	return true;
}

} // namespace wav
