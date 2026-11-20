#pragma once

#include "rat/core/unit.h"

#include "host/Artwork.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

class RatProcessor : public juce::AudioProcessor, public lemus::ArtworkUser {
public:
	// artwork: the pedal's brand marks from the user's own files (its places), or none.
	explicit RatProcessor(lemus::Artwork artwork = {});
	~RatProcessor() override = default;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return JucePlugin_Name; }
	bool acceptsMidi() const override { return false; }
	bool producesMidi() const override { return false; }
	bool isMidiEffect() const override { return false; }
	// After the input stops, the coupling caps take this long to fall under -90 dB from full-scale
	// input at any setting (0.84 s measured); -120 dB takes up to 5 s.
	double getTailLengthSeconds() const override { return 1.0; }

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String &) override {}

	void getStateInformation(juce::MemoryBlock &destData) override;
	void setStateInformation(const void *data, int sizeInBytes) override;

	// The model version a session was saved with, so that a later change to the sound can keep old
	// sessions sounding as they did: a change that changes the sound bumps kModelVersion and keeps
	// the old behaviour for sessions saved before it.
	static constexpr int kModelVersion = 1;
	int modelVersion() const { return m_modelVersion; }
	juce::AudioProcessorValueTreeState &params() { return m_params; }
	const lemus::Artwork &artwork() const { return m_artwork; }
	void setArtwork(lemus::Artwork artwork) override { m_artwork = std::move(artwork); } // the view reads it when it's built

	// Knob rotations 0..1 (the parameters run 0..10, like the 303's and Aphex's knobs).
	rat::Controls controls() const;
	bool engaged() const { return m_engaged->load() > 0.5f; }
	// What 0 dBFS is at the jacks: +4 dBu (1.74 V peak), about a hot humbucker or a line-level
	// synth. How hard the pedal is driven is the track's level, as a real one's is the source's,
	// and the INPUT gain in front of it (the parameter "input", dB; the bypass skips it).
	static double levelDbu() { return rat::kDefaultLevelDbu; }
	// The unit trims fitted to real RATs (core/pedal.h, capturedCalibration).
	rat::Calibration calibration() const;

	int oversampling() const { return m_ch[0].oversampling(); }

	// Standalone self-test (--snapshot): replace the input with a test signal (a riff).
	void setTestSignal(bool on) { m_testSignal = on; }
	bool asleep(int channel) const { return m_idle[size_t(channel & 1)].asleep; }
	// On by default. Off, the pedal runs through silence too (its tests compare the two).
	void setIdleSleep(bool on) { m_idleSleep = on; }

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
	void writeTestSignal(juce::AudioBuffer<float> &, int channels);
	void applySettings(bool force);

	juce::AudioProcessorValueTreeState m_params;
	std::atomic<float> *m_dist, *m_filter, *m_volume, *m_engaged, *m_input;
	lemus::Artwork m_artwork;

	std::array<rat::Channel, 2> m_ch;
	// Idle: a channel sleeps (no circuit model, exact zeros out) once its input is silent and its
	// output has settled, and wakes on any input or settings change. Its state is kept as it was:
	// some of the circuit's DC settles over minutes, so after a long silence the first ~50 ms of
	// the next note are as after a 2 s one (a host that suspends plugins by their tail does the
	// same, after 1 s). From about a second in, it is exactly as if it had run through.
	struct Idle {
		bool asleep = false;
		int quietSamples = 0;
	};
	std::array<Idle, 2> m_idle;
	std::array<double, 4> m_idleSettings{};
	bool m_idleSleep = true;
	bool m_lockstep = false; // the two channels' states are identical (see processBlock)
	double m_rate = 48000;
	int m_modelVersion = kModelVersion;

	std::atomic<bool> m_testSignal{false};
	double m_testTime = 0;
	juce::Random m_noise{0x308};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RatProcessor)
};
