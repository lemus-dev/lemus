// The Bass Station Rack as a plugin: the machine (core/machine.h) at the host's rate, every front-panel
// pot and switch a parameter, MIDI bytes into the unit's MIDI IN, FILTER INPUT as a side-chain. The
// EEPROM (the user programs and the global settings) is saved with the session: a plugin load is
// not a power-on.
#pragma once

#include "bsrack/core/machine.h"

#include "host/Artwork.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

class BassStationProcessor : public juce::AudioProcessor, public lemus::ArtworkUser {
public:
	// rom: the program EPROM from the user's ROM folder (nullptr: none).
	explicit BassStationProcessor(const juce::MemoryBlock *rom = nullptr, lemus::Artwork artwork = {});
	~BassStationProcessor() override = default;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return "Bass Station Rack"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return true; } // the MIDI OUT jack (machine.cmake MIDI_OUT)
	bool isMidiEffect() const override { return false; }
	// RELEASE at 10 is "10 seconds" (owner's manual p. 17) [to measure once the firmware runs].
	double getTailLengthSeconds() const override { return 12.0; }

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String &) override {}

	void getStateInformation(juce::MemoryBlock &destData) override;
	void setStateInformation(const void *data, int sizeInBytes) override;

	void setArtwork(lemus::Artwork artwork) override { m_artwork = std::move(artwork); }
	const lemus::Artwork &artwork() const { return m_artwork; }

	// The model version a session is saved with, so old projects keep their sound: a change that
	// changes the sound bumps it and keeps the old behaviour for older sessions.
	static constexpr int kModelVersion = 1;
	int modelVersion() const { return m_modelVersion; }
	juce::AudioProcessorValueTreeState &params() { return m_params; }

	bool romLoaded() const { return m_machine.romLoaded(); }
	bool asleep() const { return m_asleep.load(std::memory_order_relaxed); }

	// A button, from the panel (queued for the audio thread).
	void setButton(int button, bool down);
	// The host routes audio to FILTER INPUT (the side-chain): a cable in the rear's jack. Nothing
	// routed: nothing plugged in.
	bool filterInRouted() const { return m_filterIn.load(std::memory_order_relaxed); }

	// What the panel shows (the display, its LEDs), copied at the end of each block.
	using Display = bsrack::Display;
	Display display() const;

	// The controls as the machine takes them, from the parameters.
	bsrack::Controls controls() const;

	// Parameter ids, in panel order (core/controls.h).
	static const char *potId(int pot);
	static const char *switchId(int sw);

	bsrack::Machine &machineForTests() { return m_machine; }

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout layout();

	struct ButtonEvent {
		int button;
		bool down;
	};

	lemus::Artwork m_artwork;
	juce::AudioProcessorValueTreeState m_params;
	int m_modelVersion = kModelVersion;
	bsrack::Machine m_machine;
	std::atomic<bool> m_asleep{true}, m_filterIn{false}; // m_filterIn: the side-chain routed
	std::vector<float> m_in;

	juce::SpinLock m_queueLock;
	std::vector<ButtonEvent> m_queue;
	std::vector<uint8_t> m_pendingEeprom;
	// MIDI OUT: the UART's bytes as messages (midiOutByte), into the block's MidiBuffer
	void midiOutByte(uint8_t b, int at, juce::MidiBuffer &out);
	juce::MidiBuffer m_midiOut;
	std::array<uint8_t, 512> m_outBytes{};
	std::array<uint64_t, 512> m_outAt{};
	uint8_t m_outStatus = 0;
	std::array<uint8_t, 2> m_outData{};
	int m_outCount = 0;
	std::vector<uint8_t> m_sysex; // F0 ... F7, reserved at prepareToPlay
	bool m_inSysex = false;
	std::atomic<bool> m_haveEeprom{false};
	juce::SpinLock m_eepromLock;

	mutable juce::SpinLock m_displayLock;
	Display m_display;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassStationProcessor)
};
