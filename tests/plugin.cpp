// Headless checks of a machine's own plugin (ro-mono-303): the host built with one machine
// (LEMUS_MACHINE), as it ships. The machine's own checks run through Lemus (lemustest_instrument,
// lemustest_fx); these check what is different here: the machine is there from the start, and a
// session can't swap it for another.
//   lemustest_<id>                                        run the checks
//   lemustest_<id> --snapshot <png> [about|addrom|prompt|service] [noroms|noart] [2x]   the plugin's editor, as a PNG
#include "common.h"

namespace {

using namespace lemus;

const juce::String kId = LEMUS_MACHINE;
// A note goes in after a new instance's power-on: a machine may mute its outputs at first, as the
// TR-909 does (2.5 s), and its firmware may take a while to boot.
const int kAfterPowerOn = int(3.0 * kRate);

juce::MemoryBlock stateOf(juce::AudioProcessor &p)
{
	juce::MemoryBlock b;
	p.getStateInformation(b);
	return b;
}

void testMachine()
{
	std::printf("\n%s\n", JucePlugin_Name);
	check(machines().size() == 1 && machines().front().id == kId, "it has the " + kId + " alone");

	RomFolder empty;
	const auto *info = findMachine(kId);
	const bool needsRoms = LemusProcessor::anyRequired(LemusProcessor::missingRoms(*info));
	LemusProcessor p;
	check(p.getName() == JucePlugin_Name, juce::String("the host sees it as ") + JucePlugin_Name);
	if (needsRoms) {
		check(p.machine() == nullptr && p.missingMachine() == kId, "without its ROMs it waits for them, as the session's machine");
		std::unique_ptr<LemusEditor> e(dynamic_cast<LemusEditor *>(p.createEditorIfNeeded()));
		check(e != nullptr && e->machineEditor() == nullptr, "and its editor shows the picker with it");
		return;
	}
	check(p.machine() != nullptr && p.machine()->id == kId, "a new instance has the " + kId + " loaded, with no ROM folder");
	check(!p.selectMachine("lemustest-other") && p.machine()->id == kId, "and can't load another machine");
	std::unique_ptr<LemusEditor> e(dynamic_cast<LemusEditor *>(p.createEditorIfNeeded()));
	check(e != nullptr && e->machineEditor() != nullptr, "its editor is the machine's, under the bar: no picker");
	e.reset();

	if (LemusProcessor::kInstrument) {
		juce::AudioBuffer<float> silence(2, int(kAfterPowerOn + kRate));
		silence.clear();
		const auto out = render(p, silence, {{kAfterPowerOn, juce::MidiMessage::noteOn(1, 48, uint8_t(100))},
		                                     {kAfterPowerOn + int(kRate / 2), juce::MidiMessage::noteOff(1, 48)}});
		check(peak(out) > 0.01f, "it plays a note from MIDI");
	} else {
		const auto in = testSignal(int(kRate));
		check(peak(render(p, in)) > 0.01f, "it passes the test signal through its circuit");
	}
}

// The machine's other outputs (MachineInfo::outputs, a drum machine's individual outs): after the
// main one, named, off until the host turns them on. A host may turn them all on, stereo (Onyx
// does), and the machine then plays on the same layout.
void testOutputs()
{
	const auto &outs = findMachine(kId)->outputs;
	if (outs.empty())
		return;
	std::printf("\nOutputs\n");
	RomFolder empty;
	if (LemusProcessor::anyRequired(LemusProcessor::missingRoms(*findMachine(kId))))
		return;
	LemusProcessor p;
	bool named = p.getBusCount(false) == 1 + int(outs.size());
	for (int i = 0; named && i < int(outs.size()); ++i)
		named = p.getBus(false, 1 + i)->getName() == outs[size_t(i)] && !p.getBus(false, 1 + i)->isEnabled();
	check(named, "its " + juce::String(int(outs.size())) + " other outputs after the main one, named (" + outs.front() + " ...), off");
	auto layout = p.getBusesLayout();
	for (int b = 1; b < layout.outputBuses.size(); ++b)
		layout.outputBuses.getReference(b) = juce::AudioChannelSet::stereo();
	check(p.setBusesLayout(layout) && p.getTotalNumOutputChannels() == 2 * (1 + int(outs.size())), "a host can turn them all on, stereo");
	juce::AudioBuffer<float> silence(p.getTotalNumOutputChannels(), int(kAfterPowerOn + kRate));
	silence.clear();
	const auto out = render(p, silence, {{kAfterPowerOn, juce::MidiMessage::noteOn(1, 48, uint8_t(100))},
	                                     {kAfterPowerOn + int(kRate / 2), juce::MidiMessage::noteOff(1, 48)}});
	check(p.machineProcessor()->getBusesLayout() == p.getBusesLayout() && peak(out) > 0.01f, "the machine takes that layout, and plays a note on it");
}

void testSessions()
{
	std::printf("\nSessions\n");
	RomFolder empty;
	if (LemusProcessor::anyRequired(LemusProcessor::missingRoms(*findMachine(kId))))
		return;
	LemusProcessor a;
	auto &knob = a.slot(0);
	knob.setValueNotifyingHost(knob.getValue() < 0.5f ? 0.8f : 0.2f);
	const float set = knob.getValue();
	const auto saved = stateOf(a);
	const auto tree = juce::ValueTree::readFromData(saved.getData(), saved.getSize());
	check(tree.hasType("LEMUS") && tree["machine"].toString() == kId, "a session saves the machine as Lemus does, so either opens it");

	LemusProcessor b;
	b.setStateInformation(saved.getData(), int(saved.getSize()));
	check(b.machine() != nullptr && b.machine()->id == kId && b.slot(0).getValue() == set, "a saved session loads back, its settings with it");

	juce::ValueTree other("LEMUS");
	other.setProperty("version", 1, nullptr);
	other.setProperty("machine", "lemustest-other", nullptr);
	other.setProperty("state", juce::var(juce::MemoryBlock("x", 1)), nullptr);
	juce::MemoryOutputStream out;
	other.writeToStream(out);
	b.setStateInformation(out.getData(), int(out.getDataSize()));
	check(b.machine() != nullptr && b.machine()->id == kId && b.slot(0).getValue() == set, "another machine's session leaves it as it was");
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI gui;
	if (argc >= 3 && juce::String(argv[1]) == "--snapshot") {
		juce::StringArray opts;
		for (int i = 3; i < argc; ++i)
			opts.add(argv[i]);
		return snapshot(kId, argv[2], opts.contains("about") ? "about" : opts.contains("addrom") ? "addrom" : opts.contains("prompt") ? "prompt" : opts.contains("service") ? "service" : "",
		                opts.contains("noroms"), opts.contains("noart"), opts.contains("2x") ? 2.0f : 1.0f);
	}
	testMachine();
	testOutputs();
	testSessions();
	finish();
	return g_failures == 0 ? 0 : 1;
}
