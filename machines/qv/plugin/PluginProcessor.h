// The QuadraVerb (and Plus) as a plugin: the unit (core/unit.h) at the host's rate, its INPUT and OUTPUT
// pots as parameters, MIDI in and out, its battery-backed RAM in the session. A plugin load is not
// a power-on from the player's side: the firmware boots on a worker thread, with the factory
// programs the first time (as a unit leaves the factory), and the unit is silent until it has.
#pragma once

#include "qv/core/unit.h"

#include "host/Artwork.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class QVProcessor : public juce::AudioProcessor, public lemus::ArtworkUser {
public:
	// rom: the firmware EPROM, v2.03 (Plus) or v1.07; lcdFont: the HD44780's character ROM (MAME's
	// hd44780_a00.bin), which the LCD is drawn from. Both are needed.
	QVProcessor(const juce::MemoryBlock *rom, const juce::MemoryBlock *lcdFont, lemus::Artwork artwork = {});
	~QVProcessor() override;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return "QuadraVerb"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return true; }
	bool isMidiEffect() const override { return false; }
	// The longest factory reverb (WATERFALLS) takes about 27 s to fall under -120 dB after the input
	// stops; to measure across every program (tools/plugintest).
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

	bool romLoaded() const { return !m_rom.empty() && m_lcdFont.size() == 4096; }
	// The firmware the EPROM holds, by its bytes: "v2.03" (the Plus), "v1.07" (empty: not one of
	// those).
	const juce::String &firmware() const { return m_firmware; }
	bool plus() const { return m_firmware.startsWith("v2"); }
	// The firmware v2.03's and v1.07's EPROM images, as read (their SHA1s).
	static constexpr const char *kV203Sha1 = "5f97c5cb6b209643bed6ec4046662c87c317a9ee";
	static constexpr const char *kV107Sha1 = "224b6a308552921e0fc6a916f900f57f6d5fa623";
	// The HD44780's character ROM: 16 bytes a character code, rows 0-7 of a 5 x 8 character in
	// bits 4-0 (empty: none loaded).
	const std::vector<uint8_t> &lcdFont() const { return m_lcdFont; }
	bool booted() const { return m_ready.load(std::memory_order_acquire); }
	bool asleep() const { return m_asleep.load(std::memory_order_relaxed); }
	// Blocks until the worker has finished booting (tests, tools).
	void waitForBoot();
	qv::Unit &unitForTests() { return *m_unit; }

	// The POWER switch. Off: silent, the battery keeps the RAM. On: a power-on, the firmware from reset.
	bool powered() const { return m_powered; }
	void setPowered(bool on);

	// A panel key (core/board.h numbering), queued for the audio thread; `hard`: a hard press (the
	// keys sense force: the harder, the faster VALUE and PAGE scroll).
	void setKey(int key, bool down, bool hard = false);
	// The rear's footswitch jacks: 0 BYPASS, 1 ADVANCE (a momentary footswitch, pressed while down).
	void setFootswitch(int which, bool down) { setKey(kFootswitchKey + which, down); }
	static constexpr int kFootswitchKey = 100; // in the key queue

	// What the panel shows, copied at the end of each block.
	struct Display {
		uint8_t text[2][16]{};
		uint8_t cgram[64]{};
		bool on = false;
		uint8_t latch0400 = 0xff, latch0600 = 0xff, latch0800 = 0xff; // the LEDs, active low
		bool clip = false;
		int headroom = 0; // the AUDIO SIGNAL DISPLAY's LEDs lit, 0-4
	};
	Display display() const;
	// What the unit's iterative solves did since the last prepare (core/solver_stats.h), as of the
	// end of the last block: tools/corners.
	qv::SolverStats solverStats() const;

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout layout();
	void startBoot(const std::vector<uint8_t> &ram);
	void stopBoot();
	void publishDisplay();
	void applyGains();

	struct Key {
		int key;
		bool down, hard;
	};

	std::vector<uint8_t> m_midiOut; // MIDI out bytes not yet whole messages (audio thread)
	uint8_t m_midiStatus = 0;       // the last status sent, for running status
	std::vector<uint8_t> m_lcdFont;
	std::vector<uint8_t> m_rom; // descrambled; a copy: the host's ROM set doesn't outlive the constructor
	lemus::Artwork m_artwork;
	juce::AudioProcessorValueTreeState m_params;
	std::atomic<float> *m_input = nullptr, *m_output = nullptr;
	int m_modelVersion = kModelVersion;

	std::unique_ptr<qv::Unit> m_unit;
	std::thread m_boot;
	std::atomic<bool> m_ready{false}, m_cancelBoot{false};
	juce::SpinLock m_audioLock;
	std::atomic<double> m_rate{48000.0};
	std::atomic<bool> m_asleep{false};

	std::vector<Key> m_keys;
	juce::SpinLock m_keysLock;
	std::vector<float> m_silence;
	std::vector<float> m_right; // the right output, with one output jack

	// the RAM, handed from the audio thread to whoever saves the session
	std::atomic<int> m_ramAsk{0};
	std::vector<uint8_t> m_ramCopy;
	bool m_powered = true;
	std::vector<uint8_t> m_offRam, m_bootRam;
	// The firmware running, and the RAM of a session saved with another, kept with the session
	// for when that firmware is back: each firmware keeps its programs and settings in its own way.
	juce::String m_firmware;
	juce::String m_asideFirmware;
	std::vector<uint8_t> m_asideRam;

	mutable juce::SpinLock m_displayLock;
	Display m_display;
	qv::SolverStats m_solverStats; // with the display, under its lock

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QVProcessor)
};
