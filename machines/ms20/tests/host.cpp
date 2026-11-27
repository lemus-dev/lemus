// The MS-20 through Lemus: no ROM to find, its slots, MIDI playing its keyboard, audio bit-identical
// to the machine on its own, sessions (the controls and the patch panel's cables come back), its
// artwork places.
#include "common.h"
#include "machines/drivers.h"
#include "ms20/plugin/PluginProcessor.h"

#include <clocale>
#include <string>

namespace {

using namespace lemus;

Ms20Processor *ms20Of(LemusProcessor &p) { return dynamic_cast<Ms20Processor *>(p.machineProcessor()); }

void testDriver()
{
	std::printf("\nMS-20 driver\n");
	const auto *m = findMachine("ms20");
	check(m != nullptr && m->kind == Kind::Instrument, "it is an instrument");
	check(m != nullptr && m->roms.empty() && LemusProcessor::missingRoms(*m).empty(), "it needs no ROM: there is no CPU");
	check(m != nullptr && m->sideChain == "SIGNAL IN", "SIGNAL IN is its side-chain");
	check(m != nullptr && m->artwork.places.size() == 1, "one artwork place: the maker's logo (no model name on the front)");
	check(Ms20Processor::keyForNote(48) == 0 && Ms20Processor::keyForNote(84) == 36 && Ms20Processor::keyForNote(36) == 0 &&
	          Ms20Processor::keyForNote(97) == 25,
	      "MIDI C3-C6 are its 37 keys (8' at the MIDI pitch); notes outside fold in by octaves");
}

void testThroughHost()
{
	std::printf("\nMS-20 through the host\n");
	Ms20Processor::fixedSeeds(true); // (bit for bit between instances below)
	LemusProcessor p;
	check(p.selectMachine("ms20"), "the host loads it");
	auto *q = ms20Of(p);
	check(q != nullptr, "as the MS-20's processor");
	if (q == nullptr)
		return;

	MidiEvents notes{{0, juce::MidiMessage::noteOn(1, 48, juce::uint8(100))},
	                 {int(0.4 * kRate), juce::MidiMessage::noteOff(1, 48)},
	                 {int(0.5 * kRate), juce::MidiMessage::noteOn(1, 55, juce::uint8(100))},
	                 {int(0.9 * kRate), juce::MidiMessage::noteOff(1, 55)}};
	juce::AudioBuffer<float> in(2, int(1.5 * kRate));
	in.clear();
	const auto hosted = render(p, in, notes);
	check(peak(hosted) > 0.01f, "MIDI plays it");

	juce::MemoryBlock state;
	q->getStateInformation(state);
	Ms20Processor alone;
	alone.setStateInformation(state.getData(), int(state.getSize()));
	LemusProcessor p2;
	p2.selectMachine("ms20");
	ms20Of(p2)->setStateInformation(state.getData(), int(state.getSize()));
	const auto direct = render(alone, in, notes);
	const auto again = render(p2, in, notes);
	check(differences(again, direct) == 0, "the same bit for bit as the machine alone");
}

void testSession()
{
	std::printf("\nMS-20 sessions\n");
	Ms20Processor a;
	a.params().getParameter("lpf_cutoff")->setValueNotifyingHost(0.25f);
	ms20::Patch cables;
	cables.connect(ms20::Jack::HpfExtCutoff, int(ms20::Jack::MgTriangle));
	cables.connect(ms20::Jack::EspSignalIn, ms20::kHostInput);
	a.setPatch(cables);
	a.setPowered(false);
	juce::MemoryBlock state;
	a.getStateInformation(state);
	Ms20Processor b;
	b.setStateInformation(state.getData(), int(state.getSize()));
	check(std::abs(b.params().getParameter("lpf_cutoff")->getValue() - 0.25f) < 1e-6f, "a session brings back the controls");
	check(b.patch() == cables, "and the patch panel's cables, the outside one too");
	check(!b.powered(), "and POWER");
}

// Another plugin in the host can switch the process to a decimal-comma locale (de_DE): the
// netlists' numbers ("0.01u") read as C numbers all the same, so the MS-20 builds and plays as
// in C.
void testCommaLocale()
{
	std::printf("\nMS-20 in a decimal-comma locale\n");
	Ms20Processor::fixedSeeds(true); // (bit for bit between the two instances)
	const MidiEvents notes{{0, juce::MidiMessage::noteOn(1, 48, juce::uint8(100))}, {int(0.4 * kRate), juce::MidiMessage::noteOff(1, 48)}};
	juce::AudioBuffer<float> in(2, int(0.6 * kRate));
	in.clear();
	auto play = [&] { return render(*drivers::ms20().create({}), in, notes); };
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
		Ms20Processor::fixedSeeds(false);
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
	Ms20Processor::fixedSeeds(false);
	check(error.isEmpty(), "with LC_NUMERIC " + comma + " it builds" + (error.isEmpty() ? "" : " (" + error + ")"));
	check(error.isEmpty() && peak(plain) > 0.01f && differences(out, plain) == 0, "and plays bit-identical to the C locale");
}

} // namespace

LEMUS_HOST_TEST(ms20)
{
	testCommaLocale();
	testDriver();
	testThroughHost();
	testSession();
}
