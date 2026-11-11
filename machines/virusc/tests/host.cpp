// The Virus C through Lemus: its ROM found in the ROM folder, audio bit-identical to the machine on
// its own. The ROM comes from the private folder; without it these checks are skipped.
#include "common.h"
#include "machines/drivers.h"
#include "virusc/plugin/PluginProcessor.h"

namespace {

using namespace lemus;
using namespace lemus::test;

void testRom(const juce::MemoryBlock &rom)
{
	std::printf("\nVirus C ROM\n");
	const auto &spec = drivers::virusc().roms[0];
	check(sha1Hex(rom.getData(), rom.getSize()) == spec.sha1 && crc32Hex(rom.getData(), rom.getSize()) == spec.crc32,
	      "the private ROM matches MAME's virusc set, BIOS v6.6");
	const RomFolder folder;
	check(LemusProcessor::missingRoms(drivers::virusc()).size() == 1, "an empty folder lacks the firmware");
	folder.add("os66.bin", rom);
	check(LemusProcessor::missingRoms(drivers::virusc()).empty(), "found under another name");
}

// Through the host, whose ROM set is gone once the machine is built: a session load restarts the
// firmware from the machine's own copy, and the audio is the machine's own, bit for bit.
void testThroughHost(const juce::MemoryBlock &rom)
{
	std::printf("\nVirus C through the host\n");
	const RomFolder folder;
	folder.add("virus_c_660x368.bin", rom);
	LemusProcessor p;
	check(p.selectMachine("virusc"), "the host loads it");
	auto *v = dynamic_cast<VirusCProcessor *>(p.machineProcessor());
	check(v != nullptr && v->romLoaded(), "with its firmware");
	if (v == nullptr)
		return;
	v->waitForBoot();
	check(v->booted(), "it boots");

	juce::MemoryBlock state;
	v->getStateInformation(state);
	v->setStateInformation(state.getData(), int(state.getSize()));
	v->waitForBoot();
	check(v->booted(), "a session load boots it again, after the host's ROM set has gone");

	VirusCProcessor alone(&rom);
	alone.setStateInformation(state.getData(), int(state.getSize()));
	alone.waitForBoot();

	juce::AudioBuffer<float> silence(2, int(kRate));
	silence.clear();
	const MidiEvents notes = {{480, juce::MidiMessage::noteOn(1, 60, uint8_t(100))}, {24000, juce::MidiMessage::noteOff(1, 60)}};
	const auto hosted = render(p, silence, notes);
	const auto direct = render(alone, silence, notes);
	check(peak(hosted) > 0.01f, "a note sounds");
	check(differences(hosted, direct) == 0, "the same bit for bit as the machine alone");
}

} // namespace

LEMUS_HOST_TEST(virusc)
{
	const auto rom = privateRom("virusc", "virus_c_660x368.bin");
	if (rom.isEmpty()) {
		std::printf("\nNo Virus C ROM in the private folder: Virus C checks skipped.\n");
		return;
	}
	testRom(rom);
	testThroughHost(rom);
}
