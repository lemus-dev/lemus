#pragma once

#include "Machine.h"
#include "SlotParameter.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

// The host plugin. Each machine's own plugin (ro-mono-303) is this class built with that machine
// alone (LEMUS_MACHINE): an instance starts with it loaded. Lemus (instruments) and Lemus FX
// (effects) are this class built with all their machines: an instance starts empty, the user picks
// a machine, and the instance keeps that machine. Either way the session saves which machine it
// is. The machine is an AudioProcessor of its own, run inside this one: audio, MIDI, the host's
// transport, state and the editor pass straight through, and its parameters appear to the host
// through the fixed slots.
class LemusProcessor final : public juce::AudioProcessor, public juce::ChangeBroadcaster, private juce::AudioProcessorListener {
public:
	static constexpr int kSlots = 128;
	static constexpr bool kInstrument = JucePlugin_IsSynth != 0;

	LemusProcessor();
	~LemusProcessor() override;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override;
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
	void setNonRealtime(bool isNonRealtime) noexcept override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return JucePlugin_Name; }
	bool acceptsMidi() const override { return JucePlugin_WantsMidiInput != 0; }
	bool producesMidi() const override { return JucePlugin_ProducesMidiOutput != 0; }
	bool isMidiEffect() const override { return JucePlugin_IsMidiEffect != 0; }
	double getTailLengthSeconds() const override { return m_tail.load(); }

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String &) override {}

	void getStateInformation(juce::MemoryBlock &destData) override;
	void setStateInformation(const void *data, int sizeInBytes) override;

	// The loaded machine, or nullptr while the instance is empty. Message thread.
	const lemus::MachineInfo *machine() const { return m_info; }
	juce::AudioProcessor *machineProcessor() const { return m_machine.get(); }
	lemus::SlotParameter &slot(int i) { return *m_slots[size_t(i)]; }

	// Loads a machine into the instance (message thread). False if this host doesn't have it or
	// its required ROMs aren't in the ROM folder. Listeners get a change message.
	bool selectMachine(const juce::String &id);

	// The ROMs a machine lists that the ROM folder doesn't have, optional ones included.
	static std::vector<lemus::RomSpec> missingRoms(const lemus::MachineInfo &info);
	// True if any of `roms` is required.
	static bool anyRequired(const std::vector<lemus::RomSpec> &roms);

	// A session's machine that couldn't be loaded (this build doesn't have it, or its ROMs are
	// missing). Its state is kept, so saving doesn't lose it; retryMissing() loads it once it can.
	const juce::String &missingMachine() const { return m_missingId; }
	bool retryMissing();

	// Which artwork pack the loaded machine got, for the About box.
	const juce::String &artworkNote() const { return m_artworkNote; }
	// The optional ROMs the loaded machine runs without, and what it lacks, for the About box.
	const juce::String &romNote() const { return m_romNote; }
	// The same ROMs, for the bar: while there are any it shows the mode and offers to add them.
	const std::vector<lemus::RomSpec> &romsWithout() const { return m_without; }
	// Reloads the machine if the ROM folder now has more of its ROMs, keeping its state (message
	// thread): an optional ROM added since it loaded comes into use. False if nothing new is there.
	bool reloadWithRoms();

private:
	static BusesProperties buses();
	std::unique_ptr<juce::AudioProcessor> createMachine(const lemus::MachineInfo &info);
	void install(const lemus::MachineInfo &info, std::unique_ptr<juce::AudioProcessor> machine, bool announceValues);
	void configure(juce::AudioProcessor &machine);

	void audioProcessorParameterChanged(juce::AudioProcessor *, int index, float newValue) override;
	void audioProcessorChanged(juce::AudioProcessor *, const juce::AudioProcessorListener::ChangeDetails &) override;
	void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor *, int index) override;
	void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor *, int index) override;

	std::array<lemus::SlotParameter *, kSlots> m_slots{};
	const lemus::MachineInfo *m_info = nullptr;
	std::unique_ptr<juce::AudioProcessor> m_machine;
	// Machines replaced by a session load. Kept until this instance goes: a host may still be
	// reading a slot that pointed into one.
	std::vector<std::unique_ptr<juce::AudioProcessor>> m_retired;

	juce::String m_missingId;
	juce::MemoryBlock m_missingState;
	juce::String m_artworkNote;
	juce::String m_romNote;
	std::vector<lemus::RomSpec> m_without;

	double m_rate = 44100.0;
	int m_block = 512;
	bool m_prepared = false;
	std::atomic<double> m_tail{0.0};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LemusProcessor)
};
