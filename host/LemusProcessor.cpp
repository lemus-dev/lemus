#include "LemusProcessor.h"
#include "Artwork.h"
#include "LemusEditor.h"

#include <algorithm>

namespace {

// Saved state: a ValueTree with the machine's ID and the machine's own state, as it saves it.
const juce::Identifier kStateType{"LEMUS"};
const juce::Identifier kVersion{"version"};
const juce::Identifier kMachine{"machine"};
const juce::Identifier kMachineState{"state"};
const juce::Identifier kSavedBy{"savedBy"}; // the Lemus release that saved it
constexpr int kStateVersion = 1;

} // namespace

juce::AudioProcessor::BusesProperties LemusProcessor::buses()
{
	const auto stereo = juce::AudioChannelSet::stereo();
	auto b = kInstrument ? BusesProperties().withOutput("Output", stereo, true)
	                     : BusesProperties().withInput("Input", stereo, true).withOutput("Output", stereo, true);
#ifdef LEMUS_MACHINE
	// a machine's own plugin: its other outputs too (MachineInfo::outputs)
	if (const auto *info = lemus::findMachine(LEMUS_MACHINE))
		for (const auto &name : info->outputs)
			b = b.withOutput(name, juce::AudioChannelSet::mono(), false);
#endif
	return b;
}

LemusProcessor::LemusProcessor() : AudioProcessor(buses())
{
	for (int i = 0; i < kSlots; ++i) {
		auto *p = new lemus::SlotParameter(i);
		m_slots[size_t(i)] = p;
		addParameter(p);
	}
#ifdef LEMUS_MACHINE
	// A machine's own plugin: the machine is there from the start. Without its required ROMs the
	// instance waits for them, as for a session's machine that couldn't be loaded.
	if (const auto *info = lemus::findMachine(LEMUS_MACHINE)) {
		if (auto machine = createMachine(*info))
			install(*info, std::move(machine), false);
		else
			m_missingId = info->id;
	}
#endif
}

LemusProcessor::~LemusProcessor()
{
	if (m_machine != nullptr)
		m_machine->removeListener(this);
}

bool LemusProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	const auto out = layouts.getMainOutputChannelSet();
	if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
		return false;
	for (int b = 1; b < layouts.outputBuses.size(); ++b) {
		const auto &set = layouts.outputBuses.getReference(b);
		if (!set.isDisabled() && set != juce::AudioChannelSet::mono() && set != juce::AudioChannelSet::stereo())
			return false;
	}
	return kInstrument || layouts.getMainInputChannelSet() == out;
}

void LemusProcessor::configure(juce::AudioProcessor &machine)
{
	machine.setNonRealtime(isNonRealtime());
	if (!machine.setBusesLayout(getBusesLayout()))
		machine.setPlayConfigDetails(getTotalNumInputChannels(), getTotalNumOutputChannels(), m_rate, m_block);
	machine.setRateAndBufferSizeDetails(m_rate, m_block);
	machine.prepareToPlay(m_rate, m_block);
}

void LemusProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	m_rate = sampleRate;
	m_block = samplesPerBlock;
	m_prepared = true;
	if (m_machine != nullptr)
		configure(*m_machine);
}

void LemusProcessor::releaseResources()
{
	m_prepared = false;
	if (m_machine != nullptr)
		m_machine->releaseResources();
}

void LemusProcessor::setNonRealtime(bool isNonRealtime) noexcept
{
	AudioProcessor::setNonRealtime(isNonRealtime);
	const juce::ScopedLock sl(getCallbackLock());
	if (m_machine != nullptr)
		m_machine->setNonRealtime(isNonRealtime);
}

void LemusProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
	// The wrapper holds the callback lock around this call; machines are swapped under it.
	if (auto *m = m_machine.get()) {
		m->setPlayHead(getPlayHead());
		m->processBlock(buffer, midi);
		return;
	}
	// Empty instance: an effect passes its input through, an instrument is silent.
	const int from = kInstrument ? 0 : getTotalNumInputChannels();
	for (int ch = from; ch < buffer.getNumChannels(); ++ch)
		buffer.clear(ch, 0, buffer.getNumSamples());
}

std::vector<lemus::RomSpec> LemusProcessor::missingRoms(const lemus::MachineInfo &info)
{
	std::vector<lemus::RomSpec> missing;
	lemus::romfolder::load(lemus::romfolder::location(), info.roms, &missing);
	return missing;
}

bool LemusProcessor::anyRequired(const std::vector<lemus::RomSpec> &roms)
{
	return std::any_of(roms.begin(), roms.end(), [](const lemus::RomSpec &r) { return !r.optional; });
}

std::unique_ptr<juce::AudioProcessor> LemusProcessor::createMachine(const lemus::MachineInfo &info)
{
	const auto folder = lemus::romfolder::location();
	std::vector<lemus::RomSpec> missing;
	auto roms = lemus::romfolder::load(folder, info.roms, &missing);
	if (anyRequired(missing))
		return nullptr;
	// With every ROM, the answer to the prompt is forgotten: if one goes missing later, it shows again.
	if (!info.roms.empty() && missing.empty() && lemus::romPromptAnswered(info.id))
		lemus::setRomPromptAnswered(info.id, false);
	// The artwork: any pack is used, and a machine with places takes their own files too. Without
	// places and without a pack, the machine's blank pack goes in the folder for the user to fill in;
	// with places, a blank one an earlier release wrote comes out.
	const auto &spec = info.artwork;
	const auto art = lemus::romfolder::findAllArtwork(folder, spec);
	lemus::romfolder::addArtwork(roms, spec, art);
	if (!spec.places.empty())
		lemus::romfolder::removeBlankArtwork(folder, spec);
	else if (art.pack.data.isEmpty())
		lemus::romfolder::installBlankArtwork(folder, spec);
	auto machine = info.create(roms);
	juce::StringArray without;
	for (const auto &r : missing)
		without.add("Without " + r.name + ": " + r.without + ".");
	m_romNote = without.joinIntoString("\n");
	m_without = missing;
	m_artworkNote = artworkNote(spec, art);
	m_artworkPrint = fingerprint(art);
	return machine;
}

juce::String LemusProcessor::artworkNote(const lemus::ArtworkSpec &spec, const lemus::romfolder::ArtworkFound &art)
{
	if (spec.name.isEmpty())
		return {};
	if (spec.places.empty())
		return art.pack.data.isEmpty() ? "No artwork pack: the panel's brand marks come from " + spec.name + " in your ROM folder."
		                               : "Artwork: " + art.pack.where + ".";
	if (art.empty())
		return "No artwork: the panel's brand marks come from your own files, in artwork/" + spec.folder() + "/ in your ROM folder (ROMs on the bar lists them).";
	if (art.places.empty())
		return "Artwork: " + art.pack.where + ".";
	juce::StringArray from; // the folder, the zip, or both
	for (const auto &p : art.places)
		from.addIfNotAlreadyThere(p.file != juce::File() ? p.where.upToLastOccurrenceOf("/", true, false) : p.where.upToFirstOccurrenceOf(".zip", true, true));
	juce::String note = "Artwork: " + juce::String(int(art.places.size())) + " of " + juce::String(int(spec.places.size())) + " places from " +
	                    from.joinIntoString(" and ");
	return note + (art.pack.data.isEmpty() || art.places.size() == spec.places.size() ? "." : ", the others from " + art.pack.where + ".");
}

// What artwork the folder had: each file's path and checksum.
juce::String LemusProcessor::fingerprint(const lemus::romfolder::ArtworkFound &art)
{
	juce::StringArray print{art.pack.where + " " + lemus::sha1Hex(art.pack.data.getData(), art.pack.data.getSize())};
	for (const auto &p : art.places)
		print.add(p.where + " " + lemus::sha1Hex(p.data.getData(), p.data.getSize()));
	return print.joinIntoString("\n");
}

bool LemusProcessor::reloadArtwork()
{
	JUCE_ASSERT_MESSAGE_THREAD
	if (m_info == nullptr || m_machine == nullptr)
		return false;
	const auto &spec = m_info->artwork;
	const auto art = lemus::romfolder::findAllArtwork(lemus::romfolder::location(), spec);
	const auto print = fingerprint(art);
	if (print == m_artworkPrint)
		return false;
	m_artworkPrint = print;
	if (auto *user = dynamic_cast<lemus::ArtworkUser *>(m_machine.get())) {
		lemus::RomSet roms;
		lemus::romfolder::addArtwork(roms, spec, art);
		user->setArtwork(lemus::Artwork::load(roms, spec));
	}
	m_artworkNote = artworkNote(spec, art);
	++m_artworkVersion;
	sendChangeMessage();
	return true;
}

bool LemusProcessor::selectMachine(const juce::String &id)
{
	JUCE_ASSERT_MESSAGE_THREAD
	const auto *info = lemus::findMachine(id);
	if (info == nullptr)
		return false;
	if (m_info == info)
		return true;
	auto machine = createMachine(*info);
	if (machine == nullptr)
		return false;
	install(*info, std::move(machine), true);
	return true;
}

bool LemusProcessor::retryMissing()
{
	const auto *info = m_missingId.isNotEmpty() ? lemus::findMachine(m_missingId) : nullptr;
	auto machine = info != nullptr ? createMachine(*info) : nullptr;
	if (machine == nullptr)
		return false;
	machine->setStateInformation(m_missingState.getData(), int(m_missingState.getSize()));
	install(*info, std::move(machine), true);
	return true;
}

bool LemusProcessor::reloadWithRoms()
{
	JUCE_ASSERT_MESSAGE_THREAD
	if (m_info == nullptr || m_without.empty())
		return false;
	if (missingRoms(*m_info).size() >= m_without.size())
		return false;
	juce::MemoryBlock state;
	m_machine->getStateInformation(state);
	auto machine = createMachine(*m_info);
	if (machine == nullptr)
		return false;
	machine->setStateInformation(state.getData(), int(state.getSize()));
	install(*m_info, std::move(machine), true);
	return true;
}

void LemusProcessor::install(const lemus::MachineInfo &info, std::unique_ptr<juce::AudioProcessor> machine, bool announceValues)
{
	jassert(machine != nullptr);
	const auto &params = machine->getParameters();
	jassert(params.size() <= kSlots); // the machine has more parameters than the host has slots

	machine->addListener(this);
	if (m_prepared)
		configure(*machine);

	std::unique_ptr<juce::AudioProcessor> old;
	{
		const juce::ScopedLock sl(getCallbackLock());
		for (int i = 0; i < kSlots; ++i)
			m_slots[size_t(i)]->setTarget(i < params.size() ? params[i] : nullptr);
		old = std::exchange(m_machine, std::move(machine));
		m_info = &info;
		m_missingId.clear();
		m_missingState.reset();
	}
	if (old != nullptr) {
		old->removeListener(this);
		old->releaseResources();
		m_retired.push_back(std::move(old));
	}

	m_tail.store(m_machine->getTailLengthSeconds());
	setLatencySamples(m_machine->getLatencySamples());
	updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged(true).withNonParameterStateChanged(true));
	// Picked in the editor: tell the host each slot's new value. (A session load doesn't need
	// this; the wrapper reads every value back after setStateInformation.)
	if (announceValues)
		for (auto *s : m_slots)
			if (s->target() != nullptr) {
				s->beginChangeGesture();
				s->sendValueChangedMessageToListeners(s->getValue());
				s->endChangeGesture();
			}
	sendChangeMessage();
}

void LemusProcessor::audioProcessorParameterChanged(juce::AudioProcessor *, int index, float newValue)
{
	// A change the machine made itself (its editor, MIDI, a preset): tell the host. Host changes
	// pass through here too, on their way in from a slot; those don't go back out.
	if (lemus::SlotParameter::forwardingFromHost() || index < 0 || index >= kSlots)
		return;
	m_slots[size_t(index)]->sendValueChangedMessageToListeners(newValue);
}

void LemusProcessor::audioProcessorParameterChangeGestureBegin(juce::AudioProcessor *, int index)
{
	if (index >= 0 && index < kSlots)
		m_slots[size_t(index)]->beginChangeGesture();
}

void LemusProcessor::audioProcessorParameterChangeGestureEnd(juce::AudioProcessor *, int index)
{
	if (index >= 0 && index < kSlots)
		m_slots[size_t(index)]->endChangeGesture();
}

void LemusProcessor::audioProcessorChanged(juce::AudioProcessor *p, const juce::AudioProcessorListener::ChangeDetails &details)
{
	if (p != m_machine.get())
		return; // a machine being set up, or one already replaced
	if (details.latencyChanged)
		setLatencySamples(p->getLatencySamples());
	updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
	                      .withParameterInfoChanged(details.parameterInfoChanged)
	                      .withProgramChanged(details.programChanged)
	                      .withNonParameterStateChanged(details.nonParameterStateChanged));
}

juce::AudioProcessorEditor *LemusProcessor::createEditor()
{
	return new LemusEditor(*this);
}

void LemusProcessor::getStateInformation(juce::MemoryBlock &destData)
{
	juce::ValueTree state(kStateType);
	state.setProperty(kVersion, kStateVersion, nullptr);
	state.setProperty(kSavedBy, JucePlugin_VersionString, nullptr);
	if (m_machine != nullptr) {
		juce::MemoryBlock machineState;
		m_machine->getStateInformation(machineState);
		state.setProperty(kMachine, m_info->id, nullptr);
		state.setProperty(kMachineState, machineState, nullptr);
	} else if (m_missingId.isNotEmpty()) {
		state.setProperty(kMachine, m_missingId, nullptr);
		state.setProperty(kMachineState, m_missingState, nullptr);
	}
	juce::MemoryOutputStream out(destData, false);
	state.writeToStream(out);
}

void LemusProcessor::setStateInformation(const void *data, int sizeInBytes)
{
	const auto state = juce::ValueTree::readFromData(data, size_t(sizeInBytes));
	if (!state.hasType(kStateType))
		return;
	const juce::String id = state[kMachine];
	juce::MemoryBlock machineState;
	if (const auto *block = state[kMachineState].getBinaryData())
		machineState = *block;
	if (id.isEmpty())
		return; // saved before a machine was picked

	if (m_info != nullptr && m_info->id == id) {
		m_machine->setStateInformation(machineState.getData(), int(machineState.getSize()));
		return;
	}
	if (const auto *info = lemus::findMachine(id))
		if (auto machine = createMachine(*info)) {
			machine->setStateInformation(machineState.getData(), int(machineState.getSize()));
			install(*info, std::move(machine), false);
			return;
		}
	// Not in this build, or its ROMs are missing.
	if (m_machine == nullptr) {
		m_missingId = id;
		m_missingState = machineState;
		sendChangeMessage();
	}
}

juce::String LemusProcessor::saveStateTo(const juce::File &file)
{
	JUCE_ASSERT_MESSAGE_THREAD
	juce::MemoryBlock data;
	getStateInformation(data);
	if (!file.replaceWithData(data.getData(), data.getSize()))
		return "Couldn't write " + file.getFullPathName() + ".";
	return {};
}

juce::String LemusProcessor::loadStateFrom(const juce::File &file)
{
	JUCE_ASSERT_MESSAGE_THREAD
	const juce::String notOurs = file.getFileName() + " isn't a saved " + JucePlugin_Name + " state.";
	if (file.getSize() > 256 * 1024 * 1024)
		return notOurs;
	juce::MemoryBlock data;
	if (!file.loadFileAsData(data))
		return "Couldn't read " + file.getFullPathName() + ".";
	// The tree's type comes first, as a terminated string: anything else isn't ours.
	const auto type = kStateType.toString();
	if (data.getSize() <= size_t(type.length()) || std::memcmp(data.getData(), type.toRawUTF8(), size_t(type.length()) + 1) != 0)
		return notOurs;
	const auto state = juce::ValueTree::readFromData(data.getData(), data.getSize());
	const juce::String id = state[kMachine];
	if (!state.hasType(kStateType) || id.isEmpty())
		return notOurs;
	const auto *info = lemus::findMachine(id);
	if (info == nullptr)
		return file.getFileName() + " is a state of another machine (" + id + "), which " + JucePlugin_Name + " doesn't have.";
	juce::MemoryBlock machineState;
	if (const auto *block = state[kMachineState].getBinaryData())
		machineState = *block;

	if (m_info == info) {
		const bool wasSuspended = isSuspended();
		suspendProcessing(true);
		m_machine->setStateInformation(machineState.getData(), int(machineState.getSize()));
		suspendProcessing(wasSuspended);
		updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
		return {};
	}
	auto machine = createMachine(*info);
	if (machine == nullptr)
		return file.getFileName() + " is a state of the " + info->name + ", whose ROMs aren't in your ROM folder.";
	machine->setStateInformation(machineState.getData(), int(machineState.getSize()));
	install(*info, std::move(machine), true);
	return {};
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
	return new LemusProcessor();
}
