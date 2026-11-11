// The Virus C as a plugin: the board (core/machine.h) resampled to the host's rate, the knobs as
// parameters, MIDI in and out. A plugin load is not a power-on from the player's side: the firmware
// boots on a worker thread and the instrument is silent until it has finished.
// Its buses are the unit's jacks: OUT 1 (the main output), OUT 2 and OUT 3 (stereo, off until the
// host turns them on; a mono bus gets the jack's left side) and the two inputs as a stereo
// side-chain, read only while the host has it on (host/Machine.h, MachineInfo::sideChain).
#pragma once

#include "virusc/core/dac.h"
#include "virusc/core/machine.h"
#include "virusc/core/resampler.h"

#include "host/Artwork.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class VirusCProcessor : public juce::AudioProcessor, public lemus::ArtworkUser {
public:
	explicit VirusCProcessor(const juce::MemoryBlock *rom, lemus::Artwork artwork = {});
	~VirusCProcessor() override;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return "Virus C"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return true; }
	bool isMidiEffect() const override { return false; }
	double getTailLengthSeconds() const override { return 8.0; }

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String &) override {}

	void getStateInformation(juce::MemoryBlock &destData) override;
	void setStateInformation(const void *data, int sizeInBytes) override;

	void setArtwork(lemus::Artwork artwork) override { m_artwork = std::move(artwork); }

	static constexpr int kModelVersion = 1;
	int modelVersion() const { return m_modelVersion; }
	juce::AudioProcessorValueTreeState &params() { return m_params; }

	bool romLoaded() const { return !m_rom.isEmpty(); }
	bool booted() const { return m_ready.load(std::memory_order_acquire); }
	bool asleep() const { return m_asleep; }
	virusc::Machine &machineForTests() { return *m_machine; }
	// Blocks until the worker has finished booting (tests, tools).
	void waitForBoot();
	// The rear panel's ON/OFF switch. Off: the board stops and the output is silent; the battery keeps
	// banks A and B and the flash keeps what was written to it. On: a power-on, the firmware from reset.
	// A plugin load is not a power-on (the header above): it boots, but the switch was never off.
	bool powered() const { return m_powered; }
	void setPowered(bool on);
	// The rear panel's jacks in use: OUT 2 and OUT 3 while the host has their buses on (pair 1, 2; OUT 1
	// always), and the inputs while it feeds the side-chain.
	bool jackOn(int pair) const { return pair == 0 || (pair < int(m_pairOn.size()) && m_pairOn[size_t(pair)]); }
	bool inputOn() const { return m_inputWasOn; }

	// A panel key, row and bit as MAME's matrix has them; queued for the audio thread.
	void setKey(int row, int bit, bool down);

	// The display, copied at the end of each block: its two lines of character codes (shift applied),
	// the controller's character RAM (codes 0-7) and whether it is on.
	struct Lcd {
		char text[2][17]{};
		uint8_t cgram[64]{};
		bool on = false;
	};
	void lcdText(char out[2][17]) const;
	Lcd lcd() const;
	const lemus::Artwork &artwork() const { return m_artwork; }
	// The scanned LEDs' brightness (core/machine.h, ledDuty) and the two LFO RATE LEDs' (lfoLed).
	void ledState(float out[virusc::Machine::kLedColumns][virusc::Machine::kLedLines], float lfo[2]) const;

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout layout();
	void resetInput();
	void pushInput(float left, float right);
	void startBoot(const std::vector<uint8_t> &ram, const std::vector<uint8_t> &flash = {});
	void stopBoot();
	void publishDisplay();

	struct Key {
		int row, bit;
		bool down;
	};

	juce::MemoryBlock m_rom; // a copy: the host's ROM set doesn't outlive the constructor
	lemus::Artwork m_artwork;
	juce::AudioProcessorValueTreeState m_params;
	std::array<std::atomic<float> *, virusc::kKnobs> m_knobParams{};
	std::array<int, virusc::kKnobs> m_knobSent{};
	int m_modelVersion = kModelVersion;

	std::unique_ptr<virusc::Machine> m_machine;
	std::thread m_boot;
	std::atomic<bool> m_ready{false}, m_cancelBoot{false};
	juce::SpinLock m_audioLock;

	// The six output jacks, each resampled to the host's rate while its bus is on (OUT 1 always), and
	// the two inputs to the board's while the host has the side-chain on.
	std::array<virusc::Resampler, virusc::Machine::kOutputs> m_outRes;
	// Each pair's DAC (core/dac.h), before its resampler; frames pushed since every DAC in use muted.
	std::array<virusc::Dac, virusc::Machine::kOutputs / 2> m_dac;
	int m_dacQuiet = 0;
	bool m_dacOn = true;
	void pushDac(const virusc::Machine *m);
	std::array<bool, virusc::Machine::kOutputs / 2> m_pairOn{true, false, false};
	std::array<virusc::Resampler, 2> m_inRes;
	// the inputs at the board's rate, waiting for the frames that take them
	static constexpr int kInFifo = 64;
	std::array<std::array<float, kInFifo>, 2> m_inFifo{};
	int m_inRead = 0, m_inCount = 0;
	bool m_inPrimed = false, m_inputWasOn = false;
	double m_rate = 48000.0;

	// Sleep: the output stayed under -120 dB for 100 ms with no notes held. The DSP is held; the
	// 80C515 runs on, and its first access to the DSP wakes it.
	bool m_asleep = false;
	int m_silentFrames = 0, m_notesHeld = 0;
	double m_heldDebt = 0;
	uint64_t m_heldAccesses = 0;

	std::vector<Key> m_keys;
	juce::SpinLock m_keysLock;

	// the RAM, handed from the audio thread to whoever saves the session
	std::atomic<int> m_ramAsk{0};
	std::vector<uint8_t> m_ramCopy, m_flashCopy;
	bool m_powered = true;
	std::vector<uint8_t> m_offRam, m_offFlash;   // what the battery and the flash hold while it is off
	std::vector<uint8_t> m_bootRam, m_bootFlash; // what the last boot started from

	mutable juce::SpinLock m_displayLock;
	char m_lcd[2][17]{};
	uint8_t m_cgram[64]{};
	bool m_lcdOn = false;
	float m_leds[virusc::Machine::kLedColumns][virusc::Machine::kLedLines]{};
	float m_lfoLeds[2]{};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VirusCProcessor)
};
