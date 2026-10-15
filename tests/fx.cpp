// Headless checks of Lemus FX, the effects host: its own here, and each machine's through it
// (machines/<id>/tests/host.cpp, registered with LEMUS_HOST_TEST).
//   lemustest_fx                           run the checks
//   lemustest_fx --snapshot <id> <png>     the machine's editor in Lemus FX, as a PNG
//   lemustest_fx --save-state <id> <file>  a session state with the machine loaded
#include "common.h"

namespace {

using namespace lemus;

void testEmpty()
{
	std::printf("\nEmpty instance\n");
	LemusProcessor p;
	check(p.getParameters().size() == LemusProcessor::kSlots, "128 parameters");
	check(p.slot(0).getName(64) == "Slot 1" && p.slot(0).getParameterID() == "p001", "slot 1 is \"Slot 1\", ID p001");
	check(p.slot(127).getParameterID() == "p128", "slot 128 has ID p128");
	check(p.machine() == nullptr, "no machine loaded");
	const auto in = testSignal(int(kRate));
	check(differences(render(p, in), in) == 0, "passes audio through unchanged");
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI gui;
	if (argc == 4 && juce::String(argv[1]) == "--snapshot")
		return snapshot(argv[2], argv[3]);
	if (argc == 4 && juce::String(argv[1]) == "--save-state")
		return saveState(argv[2], argv[3]);
	const RomFolder emptyFolder; // the checks never see the user's ROM folder
	testEmpty();
	runHostTests();
	finish();
	return g_failures == 0 ? 0 : 1;
}
