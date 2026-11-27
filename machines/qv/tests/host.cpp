// The QuadraVerb through Lemus FX: its ROMs (the firmware and the LCD font) found in the ROM
// folder, and audio bit-identical to the machine on its own. The ROMs come from the private folder;
// without them these checks are skipped.
#include "common.h"

#include <cstring>
#include "machines/drivers.h"
#include "qv/plugin/PluginProcessor.h"

namespace {

using namespace lemus;
using namespace lemus::test;

void testRom(const juce::MemoryBlock &rom, const juce::MemoryBlock &font)
{
	std::printf("\nQuadraVerb ROMs\n");
	const auto &spec = drivers::qv().roms[0], &fontSpec = drivers::qv().roms[1];
	check(sha1Hex(rom.getData(), rom.getSize()) == spec.sha1 && crc32Hex(rom.getData(), rom.getSize()) == spec.crc32,
	      "the private ROM is the Plus firmware v2.03 as read from the EPROM");
	check(sha1Hex(font.getData(), font.getSize()) == fontSpec.sha1, "and the LCD font is MAME's hd44780_a00");
	const RomFolder folder;
	check(LemusProcessor::missingRoms(drivers::qv()).size() == 2, "an empty folder lacks the firmware and the LCD font, both needed");
	folder.add("QuadraVerb Plus 2.03.bin", rom);
	check(LemusProcessor::missingRoms(drivers::qv()).size() == 1, "the firmware found under another name; the font still missing");
	folder.add("hd44780_a00.bin", font);
	check(LemusProcessor::missingRoms(drivers::qv()).empty(), "both found");

	// The other hd44780_a00.bin in many ROM sets, drawn from the datasheet: six characters differ.
	juce::MemoryBlock datasheet(font);
	for (const auto &[at, value] : std::initializer_list<std::pair<int, uint8_t>>{
	         {0x371, 0x11}, {0x372, 0x01}, {0x373, 0x02}, {0x374, 0x04}, {0x375, 0x04}, {0x376, 0x04},
	         {0x773, 0x15}, {0xaa2, 0x1f}, {0xaa3, 0x04}, {0xf55, 0x11}, {0xf56, 0x13}, {0xf57, 0x0d},
	         {0xfd1, 0x04}, {0xfd2, 0x00}, {0xfd3, 0x1f}, {0xfd4, 0x00}, {0xfd5, 0x04}, {0xfd6, 0x00}})
		datasheet[at] = char(value);
	check(crc32Hex(datasheet.getData(), datasheet.getSize()) == "01d108e2", "the datasheet font made from it");
	folder.clear();
	folder.add("qvv203.bin", rom);
	folder.add("hd44780_a00.bin", datasheet);
	check(LemusProcessor::missingRoms(drivers::qv()).empty(), "the datasheet font is taken too");
	const auto set = romfolder::load(folder.dir, drivers::qv().roms);
	const auto *got = set.find("hd44780_a00.bin");
	check(got != nullptr && *got == font, "and corrected to the one named");
}

// Through the host, whose ROM set is gone once the machine is built: a session load restarts the
// firmware from the machine's own copy and the RAM the session holds, and the audio is the
// machine's own, bit for bit.
void testThroughHost(const juce::MemoryBlock &rom, const juce::MemoryBlock &font)
{
	std::printf("\nQuadraVerb through the host\n");
	const RomFolder folder;
	folder.add("qvv203.bin", rom);
	folder.add("hd44780_a00.bin", font);
	LemusProcessor p;
	check(p.selectMachine("qv"), "the host loads it");
	auto *q = dynamic_cast<QVProcessor *>(p.machineProcessor());
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

	QVProcessor alone(&rom, &font);
	alone.setStateInformation(state.getData(), int(state.getSize()));
	alone.waitForBoot();

	const auto input = testSignal(int(kRate));
	const auto hosted = render(p, input);
	const auto direct = render(alone, input);
	check(peak(hosted) > 0.01f, "the effects sound");
	check(differences(hosted, direct) == 0, "the same bit for bit as the machine alone");
}

// The QuadraVerb's firmware v1.07 in the Plus's v2.03's place: found when v2.03 isn't there, never
// ahead of it; it boots and sounds through the host; and a session's RAM stays with the firmware it
// was saved with.
void testV107(const juce::MemoryBlock &v203, const juce::MemoryBlock &v107, const juce::MemoryBlock &font)
{
	std::printf("\nQuadraVerb firmware v1.07\n");
	const RomFolder folder;
	folder.add("hd44780_a00.bin", font);
	folder.add("qvv107.bin", v107);
	check(LemusProcessor::missingRoms(drivers::qv()).empty(), "v1.07 alone is enough");
	auto got = romfolder::load(folder.dir, drivers::qv().roms);
	check(got.find("qvv203.bin") != nullptr && *got.find("qvv203.bin") == v107, "and it fills the firmware's place");
	{
		juce::ZipFile::Builder zip;
		zip.addEntry(new juce::MemoryInputStream(v203, true), 9, "qv/qvv203.bin", juce::Time::getCurrentTime());
		juce::FileOutputStream out(folder.dir.getChildFile("zzz.zip"));
		zip.writeToStream(out, nullptr);
	}
	got = romfolder::load(folder.dir, drivers::qv().roms);
	check(got.find("qvv203.bin") != nullptr && *got.find("qvv203.bin") == v203, "v2.03 wins when both are there, even in a zip found later");
	folder.clear();
	folder.add("hd44780_a00.bin", font);
	folder.add("qvv107.bin", v107);

	LemusProcessor p;
	check(p.selectMachine("qv"), "the host loads it with v1.07");
	auto *q = dynamic_cast<QVProcessor *>(p.machineProcessor());
	check(q != nullptr && q->romLoaded() && q->firmware() == "v1.07", "running v1.07");
	if (q == nullptr)
		return;
	q->waitForBoot();
	check(q->booted(), "it boots, with its factory programs");
	check(peak(render(p, testSignal(int(kRate)))) > 0.01f, "the effects sound");
	const auto lcd = q->display();
	check(std::memcmp(lcd.text[1], "\"EQ+CRS+DLY+RVB\"", 16) == 0, "on program 00 of the v1.07 factory set, \"EQ+CRS+DLY+RVB\"");

	// A session saved off (so the RAM is the battery's, not a running unit's) with v2.03.
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
	QVProcessor a103(&v203, &font);
	a103.waitForBoot();
	a103.setPowered(false);
	const auto s1 = stateOf(a103);
	const auto r1 = ramOf(tree(s1), "ram");
	check(r1.getSize() == qv::kRamSize && tree(s1)["firmware"].toString() == "v2.03", "a v2.03 session holds its RAM, marked v2.03");

	QVProcessor b100(&v107, &font);
	b100.waitForBoot();
	b100.setStateInformation(s1.getData(), int(s1.getSize()));
	const auto s2 = tree(stateOf(b100));
	check(s2["firmware"].toString() == "v1.07" && ramOf(s2, "ram").isEmpty(), "opened with v1.07: a new unit, factory programs");
	check(s2["asideFirmware"].toString() == "v2.03" && ramOf(s2, "asideRam") == r1, "and the v2.03 RAM kept in the session as it was");

	QVProcessor c103(&v203, &font);
	c103.waitForBoot();
	const auto s2b = stateOf(b100);
	c103.setStateInformation(s2b.getData(), int(s2b.getSize()));
	const auto s3 = tree(stateOf(c103));
	check(s3["firmware"].toString() == "v2.03" && ramOf(s3, "ram") == r1, "back with v2.03: its RAM again");
}

} // namespace

LEMUS_HOST_TEST(qv)
{
	const auto rom = privateRom("qv", "qvv203.bin"), font = privateRom("qv", "hd44780_a00.bin");
	if (rom.isEmpty() || font.isEmpty()) {
		std::printf("\nNo QuadraVerb ROMs in the private folder: QuadraVerb checks skipped.\n");
		return;
	}
	testRom(rom, font);
	testThroughHost(rom, font);
	if (const auto v107 = privateRom("qv", "qvv107.bin"); !v107.isEmpty())
		testV107(rom, v107, font);
	else
		std::printf("\nNo QuadraVerb v1.07 EPROM in the private folder: its checks skipped.\n");
}
