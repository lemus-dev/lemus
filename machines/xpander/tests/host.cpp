// The Xpander through Lemus: its six ROMs found in the ROM folder (it doesn't load without all of
// them), MIDI playing it through the host, audio bit-identical to the machine on its own, and a
// session's battery RAM kept. The ROMs come from the private folder; without them these checks are
// skipped.
#include "common.h"
#include "machines/drivers.h"
#include "xpander/plugin/PluginProcessor.h"

namespace {

using namespace lemus;

const char *const kNames[] = {"exp1.2-0.u8", "exp1.2-1.u7", "exp1.2-2.u6", "exp1.2-3.u5", "ca1.4-0.u912", "ca1.4-1.u914"};
juce::MemoryBlock g_roms[6];

XpanderProcessor *xpanderOf(LemusProcessor &p) { return dynamic_cast<XpanderProcessor *>(p.machineProcessor()); }

void testDriver()
{
	std::printf("\nXpander driver\n");
	const auto *m = findMachine("xpander");
	check(m != nullptr && m->kind == Kind::Instrument, "it is an instrument");
	check(m != nullptr && m->roms.size() == 7 && std::count_if(m->roms.begin(), m->roms.end(), [](const RomSpec &r) { return !r.optional; }) == 6,
	      "it needs six ROMs, the main board's four and the voice board's two; the factory patches are optional");
	bool all = true;
	for (size_t i = 0; m != nullptr && i < 6; ++i)
		all = all && sha1Hex(g_roms[i].getData(), g_roms[i].getSize()) == m->roms[i].sha1;
	check(all, "the private ROMs match MAME's xpander set (main 1.2, voice 1.4)");
	check(m != nullptr && m->outputs.size() == 6, "its six VOICE outs are extra outputs");
}

void testThroughHost()
{
	std::printf("\nXpander through the host\n");
	const RomFolder folder;
	LemusProcessor p;
	check(!p.selectMachine("xpander"), "it doesn't load without its ROMs");
	for (int i = 0; i < 5; ++i)
		folder.add(kNames[i], g_roms[i]);
	check(!p.selectMachine("xpander"), "nor with one missing");
	folder.add(kNames[5], g_roms[5]);
	check(p.selectMachine("xpander") && xpanderOf(p) != nullptr && xpanderOf(p)->romsLoaded(), "with all six it loads");
	auto *q = xpanderOf(p);
	if (q == nullptr)
		return;
	q->waitForWarmUp();

	MidiEvents notes{{0, juce::MidiMessage::noteOn(1, 60, juce::uint8(100))}, {int(0.6 * kRate), juce::MidiMessage::noteOff(1, 60)}};
	juce::AudioBuffer<float> in(2, int(1.2 * kRate));
	in.clear();
	juce::MemoryBlock state;
	q->getStateInformation(state);
	LemusProcessor p2;
	p2.selectMachine("xpander");
	xpanderOf(p2)->setStateInformation(state.getData(), int(state.getSize()));
	std::vector<const juce::MemoryBlock *> main, voice;
	for (int i = 0; i < 4; ++i)
		main.push_back(&g_roms[i]);
	for (int i = 4; i < 6; ++i)
		voice.push_back(&g_roms[i]);
	XpanderProcessor alone(main, voice);
	alone.setStateInformation(state.getData(), int(state.getSize()));
	const auto hosted = render(p2, in, notes);
	const auto direct = render(alone, in, notes);
	check(peak(hosted) > 0.001f, "MIDI plays it");
	check(differences(hosted, direct) == 0, "the same bit for bit as the machine alone");
}

void testSession()
{
	std::printf("\nXpander sessions\n");
	std::vector<const juce::MemoryBlock *> main, voice;
	for (int i = 0; i < 4; ++i)
		main.push_back(&g_roms[i]);
	for (int i = 4; i < 6; ++i)
		voice.push_back(&g_roms[i]);
	XpanderProcessor a(main, voice);
	a.waitForWarmUp();
	auto ram = a.ram();
	ram[0x5ff0] = 0x5a; // somewhere in RAM2 (never write-protected)
	a.setRam(ram);
	juce::MemoryBlock state;
	a.getStateInformation(state);
	XpanderProcessor b(main, voice);
	b.setStateInformation(state.getData(), int(state.getSize()));
	check(b.ram().size() == ram.size() && b.ram()[0x5ff0] == 0x5a, "a session brings back the battery RAM");
}

} // namespace

LEMUS_HOST_TEST(xpander)
{
	for (int i = 0; i < 6; ++i)
		g_roms[i] = privateRom("xpander", kNames[i]);
	for (const auto &r : g_roms)
		if (r.getSize() == 0) {
			std::printf("\nXpander: no ROMs in the private folder, skipped\n");
			return;
		}
	testDriver();
	testThroughHost();
	testSession();
}
