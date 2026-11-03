#include "factory.h"

#include <memory>

namespace tr909 {

uint32_t romCrc32(const uint8_t *data, size_t size)
{
	uint32_t c = 0xffffffffu;
	for (size_t i = 0; i < size; i++) {
		c ^= data[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
	}
	return ~c;
}

bool factoryRam(const uint8_t *rom, size_t size, std::array<uint8_t, Machine::kRamSize> &ram)
{
	if (rom == nullptr || size != 0x2000 || romCrc32(rom, size) != kFirmwareV1Crc)
		return false;
	auto m = std::make_unique<Machine>();
	if (!m->setRom(rom, size))
		return false;
	// A unit whose memory is empty (as after a battery change): v1.0's loader (1F2E) clears
	// Bank I itself, and the power-on code sets up empty tracks in Bank II.
	const std::array<uint8_t, Machine::kRamSize> blank{};
	m->setRam(blank.data(), blank.size());
	m->powerOn();
	// TRACK 1 and PATTERN 1 held while it powers up. The firmware waits about 0.6 s, reads the
	// two keys (row 3 = 22h at 037F) and runs the loader; the keys stay down until it is done.
	m->setKey(Machine::Key::Track1, true);
	m->setKey(Machine::Key::Pattern1, true);
	const int step = int(Machine::kStateHz / 1000); // 1 ms
	for (int i = 0; i < 1500; i++)
		m->run(step);
	m->setKey(Machine::Key::Track1, false);
	m->setKey(Machine::Key::Pattern1, false);
	for (int i = 0; i < 500; i++)
		m->run(step);
	ram = m->ram();
	return true;
}

} // namespace tr909
