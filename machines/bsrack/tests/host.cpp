// The Bass Station Rack through Lemus: its firmware ROM found in the ROM folder, its slots, its
// artwork places, sessions. The ROM comes from the private folder; without it the ROM checks are
// skipped.
#include "common.h"

#include "machines/drivers.h"
#include "bsrack/plugin/PluginProcessor.h"

#include <clocale>
#include <string>

namespace {

using namespace lemus;
using namespace lemus::test;

void testDriver()
{
	std::printf("\nBass Station Rack driver\n");
	const auto *m = findMachine("bsrack");
	check(m != nullptr && m->kind == Kind::Instrument, "it is an instrument");
	check(m != nullptr && m->roms.size() == 1 && !m->roms[0].optional, "it needs one ROM, the firmware");
	check(m != nullptr && m->sideChain == "FILTER INPUT", "FILTER INPUT is its side-chain");
	check(m != nullptr && m->artwork.places.size() == 4, "four artwork places: the Novation and BassStation Rack logos, front and rear");
}

void testRom(const juce::MemoryBlock &rom)
{
	std::printf("\nBass Station Rack ROM\n");
	const auto &spec = drivers::bsrack().roms[0];
	check(sha1Hex(rom.getData(), rom.getSize()) == spec.sha1 && crc32Hex(rom.getData(), rom.getSize()) == spec.crc32,
	      "the private ROM is firmware v1.5 (MAME's bassstr)");
	const RomFolder folder;
	check(LemusProcessor::missingRoms(drivers::bsrack()).size() == 1, "an empty folder lacks it");
	folder.add("Bass Station Rack v1.5.bin", rom);
	check(LemusProcessor::missingRoms(drivers::bsrack()).empty(), "found under another name");
}

void testThroughHost(const juce::MemoryBlock &rom)
{
	std::printf("\nBass Station Rack through the host\n");
	const RomFolder folder;
	folder.add("v1.5.bin", rom);
	LemusProcessor p;
	check(p.selectMachine("bsrack"), "the host loads it");
	auto *q = dynamic_cast<BassStationProcessor *>(p.machineProcessor());
	check(q != nullptr && q->romLoaded(), "with its firmware");
}

void testSession()
{
	std::printf("\nBass Station Rack sessions\n");
	BassStationProcessor a;
	a.params().getParameter("frequency")->setValueNotifyingHost(0.25f);
	a.params().getParameter("cut_off")->setValueNotifyingHost(0.0f);
	std::vector<uint8_t> eeprom(bsrack::Machine::kEepromSize);
	for (size_t i = 0; i < eeprom.size(); ++i)
		eeprom[i] = uint8_t(i * 7);
	a.machineForTests().loadEeprom(eeprom);
	juce::MemoryBlock state;
	a.getStateInformation(state);
	BassStationProcessor b;
	b.setStateInformation(state.getData(), int(state.getSize()));
	juce::AudioBuffer<float> buf(2, 480);
	juce::MidiBuffer midi;
	b.prepareToPlay(kRate, 480);
	b.processBlock(buf, midi);
	check(b.machineForTests().saveEeprom() == eeprom, "a session brings back the EEPROM (the user programs and settings)");
	check(std::abs(b.params().getParameter("frequency")->getValue() - 0.25f) < 1e-6f, "and the pots");
	check(b.params().getParameter("cut_off")->getValue() < 0.5f, "and the switches");
	check(b.modelVersion() == BassStationProcessor::kModelVersion, "with the model version");
}

// Another plugin in the host can switch the process to a decimal-comma locale (de_DE): the
// netlists' numbers ("0.01u") read as C numbers all the same, so the Bass Station Rack builds and
// plays as in C.
void testCommaLocale(const juce::MemoryBlock &rom)
{
	std::printf("\nBass Station Rack in a decimal-comma locale\n");
	RomSet roms;
	roms.add("v1.5.bin", rom);
	const MidiEvents notes{{int(1.2 * kRate), juce::MidiMessage::noteOn(1, 48, juce::uint8(100))},
	                       {int(1.6 * kRate), juce::MidiMessage::noteOff(1, 48)}};
	juce::AudioBuffer<float> in(2, int(1.8 * kRate));
	in.clear();
	auto play = [&] { return render(*drivers::bsrack().create(roms), in, notes); };
	const auto plain = play();
	const std::string was = std::setlocale(LC_NUMERIC, nullptr);
	juce::String comma;
	for (const char *name : {"de_DE.UTF-8", "de_DE", "de-DE", "fr_FR.UTF-8", "fr_FR", "fr-FR"})
		if (std::setlocale(LC_NUMERIC, name) != nullptr && std::localeconv()->decimal_point[0] == ',') {
			comma = name;
			break;
		}
	if (comma.isEmpty()) {
		std::setlocale(LC_NUMERIC, was.c_str());
		std::printf("No decimal-comma locale here: skipped.\n");
		return;
	}
	juce::String error;
	juce::AudioBuffer<float> out;
	try {
		out = play();
	} catch (const std::exception &e) {
		error = e.what();
	}
	std::setlocale(LC_NUMERIC, was.c_str());
	check(error.isEmpty(), "with LC_NUMERIC " + comma + " it builds" + (error.isEmpty() ? "" : " (" + error + ")"));
	check(error.isEmpty() && peak(plain) > 0.01f && differences(out, plain) == 0, "and plays bit-identical to the C locale");
}

} // namespace

LEMUS_HOST_TEST(bsrack)
{
	testDriver();
	testSession();
	const auto rom = privateRom("bsrack", "v1.5.bin");
	if (rom.isEmpty()) {
		std::printf("\nNo Bass Station Rack ROM in the private folder: its ROM checks skipped.\n");
		return;
	}
	testRom(rom);
	testThroughHost(rom);
	testCommaLocale(rom);
}
