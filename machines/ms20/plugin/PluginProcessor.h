// The MS-20 as a plugin: the machine (core/machine.h) at the host's rate, every panel control a
// parameter, MIDI playing its keyboard (notes C3-C6 are its 37 keys, so 8' sounds at the MIDI pitch;
// others folded into them by octaves), pitch bend the control wheel, the patch panel's cables and the
// wheel's position saved with the session, and the plugin's audio input as the cable from outside (a
// guitar into ESP SIGNAL IN, a sound into EXT SIGNAL IN). A plugin load is not a power-on; POWER
// is. A session loads warm: the circuit settled at its controls and cables.
#pragma once

#include "ms20/core/machine.h"

#include "host/Artwork.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

class Ms20Processor : public juce::AudioProcessor, public lemus::ArtworkUser {
public:
	explicit Ms20Processor(lemus::Artwork artwork = {});
	~Ms20Processor() override = default;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
	// The host's audio workgroup (macOS): the machine's second thread (the control side, core/worker.h)
	// joins it, so the system schedules it with the audio thread.
	void audioWorkgroupContextChanged(const juce::AudioWorkgroup &workgroup) override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return "MS-20"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return false; }
	bool isMidiEffect() const override { return false; }
	// EG 2's RELEASE at 10 (10 s, service manual §1) and the filters' ringing; measured again once
	// the envelopes are circuits.
	double getTailLengthSeconds() const override { return 40.0; }

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

	// The keyboard, from the panel (queued for the audio thread).
	void setKey(int key, bool down);
	// The control wheel (-1..1) and the momentary switch, from the panel.
	void setManual(double wheel, bool momentary);
	void setWheel(double wheel) { m_wheel = juce::jlimit(-1.0, 1.0, wheel); }
	void setMomentary(bool down) { m_momentary = down; }
	ms20::Manual manual() const { return {m_wheel.load(), m_momentary.load()}; }
	// The patch panel's cables, from the panel (applied at the next block).
	void setPatch(const ms20::Patch &p);
	ms20::Patch patch() const;
	// The cable from outside carries the host's side-chain only while the host has that input on.
	bool inputOn() const;
	// POWER: VOLUME's counter-clockwise click, below this on its 0-10 scale (docs/panel.json).
	static constexpr float kPowerOffBelow = 0.5f;
	bool powered() const { return m_powered.load(); }
	void setPowered(bool on);
	bool asleep() const { return m_asleep.load(std::memory_order_relaxed); }
	// The filter board (Machine::FilterRev): Rev 1's KORG35 modules or Rev 2's KLM-307, from the
	// bar (applied at the next block: the circuit restarts settled) and saved with the session.
	bool rev2() const { return m_rev2.load(); }
	void setRev2(bool on) { m_rev2 = on; }

	// The controls as the machine takes them, from the parameters.
	ms20::Controls controls() const;
	// MIDI note to key: C3-C6 (48-84) are the 37 keys (at 8' the bottom C is 130.8 Hz, C3);
	// others folded into that range by octaves.
	static int keyForNote(int note);

	struct Display {
		std::array<bool, ms20::Machine::kKeys> keys{};
		ms20::Machine::Leds leds;
		ms20::SolverStats solver; // every circuit solve since the processor was made (core/solver_stats.h)
	};
	Display display() const;

	ms20::Machine &machineForTests() { return m_machine; }
	// Tools and tests: instances made from now on use the machine's fixed random streams (renders
	// repeat), not their own (as MS20_SEED=0).
	static void fixedSeeds(bool on) { s_fixedSeeds = on; }

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout layout();
	void handleMidi(const juce::MidiMessage &m);
	// The machine at the controls, cables and wheel as they are now, settled there if prepared (a
	// plugin load, a session load): no step from the defaults' operating point in the first blocks.
	void settle();

	struct KeyEvent {
		int key;
		bool down;
	};

	lemus::Artwork m_artwork;
	juce::AudioProcessorValueTreeState m_params;
	int m_modelVersion = kModelVersion;
	// (before the machine: its thread uses them until the machine ends it)
	juce::SpinLock m_workgroupLock;
	juce::AudioWorkgroup m_workgroup;
	juce::WorkgroupToken m_workgroupToken; // the machine's thread's, used on it only
	ms20::Machine m_machine;
	// Each instance its own unit: its own hiss and drift, a new seed at each load and power-on
	// (reseed); fixed with fixedSeeds(true) or MS20_SEED=0.
	void reseed();
	bool m_fixedSeeds = false;
	uint64_t m_seedState = 0;
	static std::atomic<bool> s_fixedSeeds;
	std::array<int, ms20::Machine::kKeys> m_midiHeld{};
	std::array<bool, ms20::Machine::kKeys> m_panelHeld{};
	std::atomic<double> m_wheel{0};
	std::atomic<bool> m_momentary{false}, m_powered{true}, m_powerChange{false}, m_asleep{false}, m_rev2{false};
	bool m_volumeOn = true; // VOLUME out of its POWER OFF click, as last seen
	double m_rate = 0;      // as prepared (0: not yet)
	std::vector<float> m_in;

	juce::SpinLock m_queueLock;
	std::vector<KeyEvent> m_queue;
	mutable juce::SpinLock m_patchLock;
	ms20::Patch m_patch;

	mutable juce::SpinLock m_displayLock;
	Display m_display;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ms20Processor)
};
