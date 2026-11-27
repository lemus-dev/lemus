// The Xpander as a plugin: the machine (core/machine.h) with its real firmware; host MIDI into
// MIDI IN as a serial stream, MIDI OUT back to the host; the panel's buttons, encoders and VOLUME;
// the six VOICE outs as extra mono outputs. The battery RAM (patches, multis, the master page and
// the tuning corrections) is the session's state.
//
// A new instance is a unit as it leaves the factory: memory reset and tuned. The first time the
// machine starts on a computer it runs its own RESET MEMORY and TUNE ALL (core/warm.h) in the
// background, which takes a while, and keeps the result in Lemus's folder; every new instance
// after that starts from it.
#pragma once

#include "xpander/core/machine.h"

#include "host/Artwork.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class XpanderProcessor : public juce::AudioProcessor, public lemus::ArtworkUser {
public:
	// main: the four main-board EPROMs in order (U8, U7, U6, U5); voice: U912, U914. patches:
	// Oberheim's factory single patches as SysEx dumps (FACTORY1.SYX), or null: a new unit then has
	// them in its memory.
	XpanderProcessor(const std::vector<const juce::MemoryBlock *> &main, const std::vector<const juce::MemoryBlock *> &voice,
	                 lemus::Artwork artwork = {}, const juce::MemoryBlock *patches = nullptr);
	~XpanderProcessor() override;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return "Xpander"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return true; }
	bool isMidiEffect() const override { return false; }
	// A release can run for many seconds (the envelopes' longest times).
	double getTailLengthSeconds() const override { return 30.0; }

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return "Default"; }
	void changeProgramName(int, const juce::String &) override {}

	void getStateInformation(juce::MemoryBlock &destData) override;
	void setStateInformation(const void *data, int sizeInBytes) override;

	void setArtwork(lemus::Artwork artwork) override { m_artwork = std::move(artwork); }
	const lemus::Artwork &artwork() const { return m_artwork; }

	static constexpr int kModelVersion = 1;
	int modelVersion() const { return m_modelVersion; }
	juce::AudioProcessorValueTreeState &params() { return m_params; }
	bool romsLoaded() const { return m_romOk; }

	// Panel -> machine, queued for the audio thread.
	void setSwitch(int column, int row, bool down);
	void turnEncoder(int encoder, int steps);
	void setMemoryProtect(bool on);
	bool memoryProtect() const { return m_protect.load(); }
	// POWER (rear): off stops the unit (silence, dark panel); on starts the firmware again from the
	// battery RAM, as at power-on. Saved with the session.
	bool powered() const { return m_powered.load(); }
	void setPowered(bool on);

	// Machine -> panel, copied at the end of each block.
	struct Display {
		xpander::MainBoard::Segments vfd{};
		uint8_t led1 = 0xff, led2 = 0xff; // active low
		std::array<uint8_t, 8> switches{}; // held, by column (bit per row)
	};
	Display display() const;
	// What the bar says: "" when ready, else e.g. "TUNING (first start)".
	juce::String status() const;
	std::string displayText(int d) const;

	// Tests and tools.
	std::vector<uint8_t> ram() const;
	void setRam(const std::vector<uint8_t> &ram); // and power on, as after a session load
	bool warmingUp() const { return m_warming.load(); }
	bool asleep() const { return m_asleep.load(std::memory_order_relaxed); }
	void waitForWarmUp();
	juce::File warmCacheFile() const; // the factory state, with or without the patches
	bool factoryPatches() const { return !m_patches.empty(); }
	// The voices' solvers since the machine was last prepared, as the last block left them
	// (core/solver_stats.h; tools/corners).
	xpander::Solvers solverStats() const;

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout layout();
	std::unique_ptr<xpander::Machine> newMachine(const std::vector<uint8_t> *ram) const;
	void startWarmUp();
	void feedMidi(xpander::Machine &m, const juce::MidiMessage &msg);
	void collectMidiOut(xpander::Machine &m, juce::MidiBuffer &out, int sample);

	struct PanelEvent {
		enum Kind : uint8_t { Switch, Encoder } kind;
		int a, b;
		bool down;
	};

	juce::AudioProcessorValueTreeState m_params;
	std::atomic<float> *m_volume = nullptr;
	lemus::Artwork m_artwork;
	std::vector<uint8_t> m_mainRom, m_voiceRom, m_patches;
	bool m_romOk = false;
	int m_modelVersion = kModelVersion;
	double m_rate = 48000;

	std::unique_ptr<xpander::Machine> m_machine;
	std::unique_ptr<xpander::Machine> m_pending; // a machine built off the audio thread, swapped in
	juce::SpinLock m_swapLock;
	std::thread m_warmThread;
	std::atomic<bool> m_warming{false}, m_cancelWarm{false}, m_protect{false}, m_powered{true}, m_asleep{false};

	juce::SpinLock m_queueLock;
	std::vector<PanelEvent> m_queue;
	std::array<uint8_t, 8> m_held{};
	std::vector<uint8_t> m_midiOutBytes;
	std::vector<float> m_scratch;

	mutable juce::SpinLock m_displayLock;
	Display m_display;
	xpander::Solvers m_solvers;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XpanderProcessor)
};
