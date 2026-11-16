// The QuadraVerb GT through Lemus FX: its ROMs (the firmware and the LCD font) found in the ROM
// folder, and audio bit-identical to the machine on its own. The ROMs come from the private folder;
// without them these checks are skipped.
#include "common.h"
#include "machines/drivers.h"
#include "qvgt/plugin/PluginProcessor.h"

namespace {

using namespace lemus;
using namespace lemus::test;

void testRom(const juce::MemoryBlock &rom, const juce::MemoryBlock &font)
{
	std::printf("\nQuadraVerb GT ROMs\n");
	const auto &spec = drivers::qvgt().roms[0], &fontSpec = drivers::qvgt().roms[1];
	check(sha1Hex(rom.getData(), rom.getSize()) == spec.sha1 && crc32Hex(rom.getData(), rom.getSize()) == spec.crc32,
	      "the private ROM is firmware v1.03 as read from the EPROM");
	check(sha1Hex(font.getData(), font.getSize()) == fontSpec.sha1, "and the LCD font is MAME's hd44780_a00");
	const RomFolder folder;
	check(LemusProcessor::missingRoms(drivers::qvgt()).size() == 2, "an empty folder lacks the firmware and the LCD font, both needed");
	folder.add("QuadraVerb GT 1.03.bin", rom);
	check(LemusProcessor::missingRoms(drivers::qvgt()).size() == 1, "the firmware found under another name; the font still missing");
	folder.add("hd44780_a00.bin", font);
	check(LemusProcessor::missingRoms(drivers::qvgt()).empty(), "both found");
}

// Through the host, whose ROM set is gone once the machine is built: a session load restarts the
// firmware from the machine's own copy and the RAM the session holds, and the audio is the
// machine's own, bit for bit.
void testThroughHost(const juce::MemoryBlock &rom, const juce::MemoryBlock &font)
{
	std::printf("\nQuadraVerb GT through the host\n");
	const RomFolder folder;
	folder.add("qvgtv103.bin", rom);
	folder.add("hd44780_a00.bin", font);
	LemusProcessor p;
	check(p.selectMachine("qvgt"), "the host loads it");
	auto *q = dynamic_cast<QVGTProcessor *>(p.machineProcessor());
	check(q != nullptr && q->romLoaded(), "with its firmware");
	if (q == nullptr)
		return;
	q->waitForBoot();
	check(q->booted(), "it boots, with the factory programs");

	juce::MemoryBlock state;
	q->getStateInformation(state);
	q->setStateInformation(state.getData(), int(state.getSize()));
	q->waitForBoot();
	check(q->booted(), "a session load boots it again, after the host's ROM set has gone");

	QVGTProcessor alone(&rom, &font);
	alone.setStateInformation(state.getData(), int(state.getSize()));
	alone.waitForBoot();

	const auto input = testSignal(int(kRate));
	const auto hosted = render(p, input);
	const auto direct = render(alone, input);
	check(peak(hosted) > 0.01f, "the effects sound");
	check(differences(hosted, direct) == 0, "the same bit for bit as the machine alone");
}

} // namespace

LEMUS_HOST_TEST(qvgt)
{
	const auto rom = privateRom("qvgt", "qvgtv103.bin"), font = privateRom("qvgt", "hd44780_a00.bin");
	if (rom.isEmpty() || font.isEmpty()) {
		std::printf("\nNo QuadraVerb GT ROMs in the private folder: QuadraVerb GT checks skipped.\n");
		return;
	}
	testRom(rom, font);
	testThroughHost(rom, font);
}
