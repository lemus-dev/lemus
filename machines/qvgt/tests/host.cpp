// The QuadraVerb GT through Lemus FX: its ROMs (the firmware and the LCD font) found in the ROM
// folder, and audio bit-identical to the machine on its own. The ROMs come from the private folder;
// without them these checks are skipped.
#include "common.h"

#include <cstring>
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

	// The other hd44780_a00.bin in many ROM sets, drawn from the datasheet: six characters differ.
	juce::MemoryBlock datasheet(font);
	for (const auto &[at, value] : std::initializer_list<std::pair<int, uint8_t>>{
	         {0x371, 0x11}, {0x372, 0x01}, {0x373, 0x02}, {0x374, 0x04}, {0x375, 0x04}, {0x376, 0x04},
	         {0x773, 0x15}, {0xaa2, 0x1f}, {0xaa3, 0x04}, {0xf55, 0x11}, {0xf56, 0x13}, {0xf57, 0x0d},
	         {0xfd1, 0x04}, {0xfd2, 0x00}, {0xfd3, 0x1f}, {0xfd4, 0x00}, {0xfd5, 0x04}, {0xfd6, 0x00}})
		datasheet[at] = char(value);
	check(crc32Hex(datasheet.getData(), datasheet.getSize()) == "01d108e2", "the datasheet font made from it");
	folder.clear();
	folder.add("qvgtv103.bin", rom);
	folder.add("hd44780_a00.bin", datasheet);
	check(LemusProcessor::missingRoms(drivers::qvgt()).empty(), "the datasheet font is taken too");
	const auto set = romfolder::load(folder.dir, drivers::qvgt().roms);
	const auto *got = set.find("hd44780_a00.bin");
	check(got != nullptr && *got == font, "and corrected to the one named");
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

// Loaded into a host already playing at 44.1 kHz, the machine is prepared while its firmware boots:
// the host is told the latency at 44.1 kHz (it used to be 48 kHz's, 94 samples for 89). And loaded
// for a bounce (offline), the first block waits for the boot rather than coming out silent.
void testLoadedWhilePlaying(const juce::MemoryBlock &rom, const juce::MemoryBlock &font)
{
	std::printf("\nQuadraVerb GT loaded into a running host\n");
	const RomFolder folder;
	folder.add("qvgtv103.bin", rom);
	folder.add("hd44780_a00.bin", font);
	{
		LemusProcessor p;
		p.setRateAndBufferSizeDetails(44100.0, kBlock);
		p.prepareToPlay(44100.0, kBlock);
		check(p.selectMachine("qvgt"), "the host loads it at 44.1 kHz");
		auto *q = dynamic_cast<QVGTProcessor *>(p.machineProcessor());
		if (q == nullptr)
			return;
		const int told = p.getLatencySamples();
		q->waitForBoot();
		std::printf("    told %d samples, the unit's %d once booted\n", told, q->unitForTests().latency());
		check(q->booted() && told == q->unitForTests().latency() && told == q->unitForTests().latencyAt(44100.0),
		      "the host is told its latency at 44.1 kHz, and it runs at 44.1 kHz");
	}
	{
		LemusProcessor p;
		p.setNonRealtime(true);
		check(p.selectMachine("qvgt"), "loaded for a bounce");
		const auto out = render(p, testSignal(int(kRate / 4)));
		const int latency = p.getLatencySamples();
		check(latency > 0 && out.getSample(0, latency) != 0.0f && peak(out) > 0.01f, "the bounce has the effects from the start (the latency)");
	}
}

// Firmware v1.00 in v1.03's place: found when v1.03 isn't there, never ahead of it; it boots and
// sounds through the host; and a session's RAM stays with the firmware it was saved with.
void testV100(const juce::MemoryBlock &v103, const juce::MemoryBlock &v100, const juce::MemoryBlock &font)
{
	std::printf("\nQuadraVerb GT firmware v1.00\n");
	const RomFolder folder;
	folder.add("hd44780_a00.bin", font);
	folder.add("qvgtv100.bin", v100);
	check(LemusProcessor::missingRoms(drivers::qvgt()).empty(), "v1.00 alone is enough");
	auto got = romfolder::load(folder.dir, drivers::qvgt().roms);
	check(got.find("qvgtv103.bin") != nullptr && *got.find("qvgtv103.bin") == v100, "and it fills the firmware's place");
	{
		juce::ZipFile::Builder zip;
		zip.addEntry(new juce::MemoryInputStream(v103, true), 9, "qvgt/qvgtv103.bin", juce::Time::getCurrentTime());
		juce::FileOutputStream out(folder.dir.getChildFile("zzz.zip"));
		zip.writeToStream(out, nullptr);
	}
	got = romfolder::load(folder.dir, drivers::qvgt().roms);
	check(got.find("qvgtv103.bin") != nullptr && *got.find("qvgtv103.bin") == v103, "v1.03 wins when both are there, even in a zip found later");
	folder.clear();
	folder.add("hd44780_a00.bin", font);
	folder.add("qvgtv100.bin", v100);

	LemusProcessor p;
	check(p.selectMachine("qvgt"), "the host loads it with v1.00");
	auto *q = dynamic_cast<QVGTProcessor *>(p.machineProcessor());
	check(q != nullptr && q->romLoaded() && q->firmware() == "v1.00", "running v1.00");
	if (q == nullptr)
		return;
	q->waitForBoot();
	check(q->booted(), "it boots, with its factory programs");
	check(peak(render(p, testSignal(int(kRate)))) > 0.01f, "the effects sound");
	const auto lcd = q->display();
	check(std::memcmp(lcd.text[1], "\"GUITARBYALESIS\"", 16) == 0, "on program 00 of the v1.00 factory set, \"GUITARBYALESIS\"");

	// A session saved off (so the RAM is the battery's, not a running unit's) with v1.03.
	auto stateOf = [](juce::AudioProcessor &a) {
		juce::MemoryBlock m;
		a.getStateInformation(m);
		return m;
	};
	auto tree = [](const juce::MemoryBlock &m) {
		const auto xml = juce::AudioProcessor::getXmlFromBinary(m.getData(), int(m.getSize()));
		return xml != nullptr ? juce::ValueTree::fromXml(*xml) : juce::ValueTree();
	};
	auto ramOf = [](const juce::ValueTree &t, const char *name) {
		const auto *b = t.getProperty(name).getBinaryData();
		return b != nullptr ? *b : juce::MemoryBlock();
	};
	QVGTProcessor a103(&v103, &font);
	a103.waitForBoot();
	a103.setPowered(false);
	const auto s1 = stateOf(a103);
	const auto r1 = ramOf(tree(s1), "ram");
	check(r1.getSize() == qvgt::kRamSize && tree(s1)["firmware"].toString() == "v1.03", "a v1.03 session holds its RAM, marked v1.03");

	QVGTProcessor b100(&v100, &font);
	b100.waitForBoot();
	b100.setStateInformation(s1.getData(), int(s1.getSize()));
	const auto s2 = tree(stateOf(b100));
	check(s2["firmware"].toString() == "v1.00" && ramOf(s2, "ram").isEmpty(), "opened with v1.00: a new unit, factory programs");
	check(s2["asideFirmware"].toString() == "v1.03" && ramOf(s2, "asideRam") == r1, "and the v1.03 RAM kept in the session as it was");

	QVGTProcessor c103(&v103, &font);
	c103.waitForBoot();
	const auto s2b = stateOf(b100);
	c103.setStateInformation(s2b.getData(), int(s2b.getSize()));
	const auto s3 = tree(stateOf(c103));
	check(s3["firmware"].toString() == "v1.03" && ramOf(s3, "ram") == r1, "back with v1.03: its RAM again");

	// A session from 0.1.0: no firmware named, so v1.03's.
	auto old = tree(s1);
	old.removeProperty("firmware", nullptr);
	juce::MemoryBlock oldState;
	if (const auto xml = old.createXml())
		juce::AudioProcessor::copyXmlToBinary(*xml, oldState);
	QVGTProcessor d103(&v103, &font);
	d103.waitForBoot();
	d103.setStateInformation(oldState.getData(), int(oldState.getSize()));
	check(ramOf(tree(stateOf(d103)), "ram") == r1, "a 0.1.0 session opens as v1.03's");
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
	testLoadedWhilePlaying(rom, font);
	if (const auto v100 = privateRom("qvgt", "qvgtv100.bin"); !v100.isEmpty())
		testV100(rom, v100, font);
	else
		std::printf("\nNo QuadraVerb GT v1.00 EPROM in the private folder: its checks skipped.\n");
}
