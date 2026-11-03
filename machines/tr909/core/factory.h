#pragma once

#include "tr909/core/machine.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace tr909 {

// The memory of a new TR-909: "factory-rhythm patterns in BANK I, TRACK 1 under as-delivered
// condition" (SN p.7, RELOADING FACTORY-PATTERNS), the "simple demonstration data ... (Bank I,
// Track 1, Rhythm Patterns 1 to 8)" of the owner's manual (OM p.7). Bank I holds patterns 1-8 of
// pattern group 1 and track 1 (65 measures, with a tempo memory); Bank II and everything else
// are empty.
//
// Only firmware v1.0 (and v2/v3) carry the data, with the loader the service notes describe:
// power on with TRACK 1 and PATTERN 1 held. v4.0 removed both (its loader at 1DF4 is a bare
// RET), but plays the same memory: the RAM format did not change (docs/firmware.md, "Factory
// patterns"). No ROM data is kept here: factoryRam() runs the user's v1.0 firmware on a Machine
// with the two keys held from power-on, as the service notes say, and returns the battery RAM
// it leaves, which is then loaded into a v4.0 Machine with setRam().
//
// Returns false (and leaves `ram` alone) unless `rom` is the v1.0 firmware (CRC32 bfb9682b).
// Takes about 2 s of emulated time (the firmware's 0.6 s power-on delay, then the loader).
bool factoryRam(const uint8_t *rom, size_t size, std::array<uint8_t, Machine::kRamSize> &ram);

// CRC32 (zlib) of a ROM image, to tell the firmware versions apart: v1.0 bfb9682b, v4.0 a9b45f30.
uint32_t romCrc32(const uint8_t *data, size_t size);
constexpr uint32_t kFirmwareV1Crc = 0xbfb9682b, kFirmwareV4Crc = 0xa9b45f30;

} // namespace tr909
