#pragma once

#include "tb303/core/machine.h"
#include "tb303/core/midi.h"
#include "tb303/core/tempoclock.h"
#include "tb303/core/trims.h"
#include "tb303/core/voice.h"

#include "host/Artwork.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

// The TB-303: the original sequencer firmware on its CPU board model, driving the voice
// board model. MIDI notes play the voice directly (velocity >= 100 accents, legato slides),
// like Roland's own software with KEYBOARD on. With SYNC on, the host transport is the DIN
// sync input (run level and 24 ppqn clock); otherwise RUN/STOP and TEMPO run the unit.
class TB303Processor : public juce::AudioProcessor {
public:
	// rom: the CPU's 2 KB mask ROM (d650c-133.ic8); without it the machine stays silent.
	// artwork: the artwork pack (tb303.svg) with the panel's brand marks, or null for none.
	explicit TB303Processor(const juce::MemoryBlock *rom, const juce::MemoryBlock *artwork = nullptr);
	~TB303Processor() override = default;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return "TB-303 Bass Line"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return false; }
	bool isMidiEffect() const override { return false; }
	double getTailLengthSeconds() const override { return 0.5; }

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String &) override {}

	void getStateInformation(juce::MemoryBlock &destData) override;
	void setStateInformation(const void *data, int sizeInBytes) override;

	juce::AudioProcessorValueTreeState &params() { return m_params; }
	bool romLoaded() const { return m_romOk; }
	bool voiceAsleep() const { return m_asleep; } // idle: the circuit model is skipped
	// The model version a session is saved with (the state's "model"), so old projects keep their
	// sound: a change to the model that changes the sound bumps kModelVersion and keeps the old
	// behaviour for sessions saved with an older version. Sessions saved before there was one are 1.
	static constexpr int kModelVersion = 1;
	int modelVersion() const { return m_modelVersion; }
	const lemus::Artwork &artwork() const { return m_artwork; }

	// Panel -> machine. Queued and applied at the start of the next audio block, in order.
	void setKey(tb303::Machine::Key k, bool down);
	void toggleRun(); // RUN/STOP key (flip-flop in the clock circuit)
	// Tests and tools: writes a pattern into the battery-backed RAM and restarts the CPU, as
	// loading a session with it would. Not for use while the audio thread runs.
	void writePattern(int group, int section, int pattern, const std::vector<tb303::Machine::Step> &steps);

	// Machine -> UI.
	struct Snapshot {
		std::array<float, size_t(tb303::Machine::Led::Count)> leds{};
		bool running = false;
		bool synced = false; // host transport is driving the clock
		bool powered = true; // VOLUME is off the OFF end (POWER SW)
		double bpm = 120;
		tb303::CpuLines lines;
		tb303::Voice::Probe probe;
		double cvRef = 1; // TM6: CV OUT's volts per octave
		double rate = 44100;
	};
	Snapshot snapshot() const;

	// The service trimmers (core/trims.h) from their parameters, TM1-TM6. A parameter at its
	// default gives the unit's exact value (parameters are floats).
	static constexpr const char *kTrimIds[6] = {"tm1", "tm2", "tm3", "tm4", "tm5", "tm6"};
	tb303::trims::Trims trims() const;
	// Whether trimmer i (0-5) is at the factory's trim: within 1e-4 of its travel counts (a host
	// may hand a parameter back a hair off), and gives the unit's exact value.
	bool trimAtFactory(int i) const;
	static tb303::trims::Trims trimDefaults() { return {}; }

	// SERVICE view's instruments: the check points' signals, one frame per host sample, kept
	// while the view shows them (setScopeOn). The voice stays awake meanwhile, as the real
	// VCO runs on between notes.
	enum ScopeChannel { ScopeS1, ScopeTp6, ScopeTp1, ScopeTp2, kScopeChannels };
	void setScopeOn(bool on);
	// Frames written so far (wrapping); read channels at one such count to keep them aligned.
	uint32_t scopeWritten() const { return m_scopeWrite.load(std::memory_order_acquire); }
	// The n frames of a channel up to frame `end`, oldest first, into dst. n is at most half the ring.
	void readScope(int channel, float *dst, int n, uint32_t end) const;
	static constexpr int kScopeFrames = 1 << 15;

	// Tempo knob (0-1) to BPM: the IC6 oscillator with the 1M C-taper TEMPO pot (§ docs/model.md),
	// R18 + TM1 at rScale times its trimmed value.
	static double tempoBpm(double knob, double rScale = 1);
	static juce::String hzText(double hz);

	static constexpr const char *kModeId = "mode";
	static constexpr const char *kTrackId = "track";

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
	tb303::Controls currentControls() const;
	void applyCalibration();
	void handleMidi(const juce::MidiMessage &m);
	void renderVoice(float *out, int n, int tapAt = -1); // tapAt: where in the block's taps
	void processPoweredOff(juce::AudioBuffer<float> &buffer, const juce::MidiBuffer &midi);
	void wake() { m_asleep = false; m_quietSamples = 0; }

	juce::AudioProcessorValueTreeState m_params;
	std::atomic<float> *m_tuning, *m_cutoff, *m_resonance, *m_envMod, *m_decay, *m_accent, *m_volume;
	std::atomic<float> *m_tempo, *m_waveform, *m_mode, *m_track, *m_sync, *m_fullCutOff;
	std::array<std::atomic<float> *, 6> m_trims{};

	tb303::Machine m_machine;
	tb303::Voice m_voice;
	tb303::MidiDriver m_midi;
	bool m_romOk = false;
	lemus::Artwork m_artwork;
	double m_rate = 44100;
	int m_modelVersion = kModelVersion;
	std::array<float, 7> m_lastCal{}; // the trims and FULL CUT OFF as last applied
	bool calibrationChanged();

	// Clock generation
	tb303::TempoClock m_clock; // the internal tempo clock and its RUN monostable
	// Idle: once the voice has settled to silence it sleeps (renderVoice), skipping the circuit
	// model and putting out exact zeros, until its lines or controls change. The CPU keeps running.
	bool m_asleep = false, m_voiceGate = false;
	int m_quietSamples = 0;
	tb303::Voice::Probe m_quietFrom;
	tb303::Controls m_lastControls;
	bool m_internalRun = false;
	bool m_powered = true; // POWER SW, on VOLUME
	bool m_hostWasPlaying = false;
	bool m_midiActive = false;

	// UI -> machine queue
	struct KeyEvent {
		int key; // -1 = run toggle
		bool down;
	};
	juce::SpinLock m_queueLock;
	std::vector<KeyEvent> m_queue;

	// Machine -> UI
	mutable juce::SpinLock m_snapLock;
	Snapshot m_snap;

	// Scope capture: a ring of frames per channel, and the block's taps before they go in.
	std::array<std::vector<float>, kScopeChannels> m_scope, m_tap;
	std::atomic<uint32_t> m_scopeWrite{0};
	std::atomic<bool> m_scopeOn{false};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TB303Processor)
};
