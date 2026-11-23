#pragma once

#include "sh101/core/lfo.h"
#include "sh101/core/machine.h"
#include "sh101/core/midi.h"
#include "sh101/core/voice.h"
#include "sh101/core/voice_keys.h"

#include "host/Artwork.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

// The SH-101: the original firmware on its CPU board model, driving the synth board model.
// MIDI notes press the keyboard (the firmware does priority, arpeggio and the sequencer),
// pitch bend moves the bender lever and the mod wheel pushes it. SYNC on is a cable in EXT CLK
// IN: the host transport is the clock (none while it is stopped); with SYNC off the
// MODULATOR's square clocks the arpeggio and sequencer, as with nothing plugged in.
//
// Without the CPU ROM it is in voice mode: MIDI plays the synth board directly (VoiceKeys), with
// no sequencer or arpeggio, and the panel's keys and buttons don't respond.
//
// POWER switches it off (silent, dark, the CPU stopped, the battery keeping the sequence) and on
// (the CPU boots and jack board A mutes the output for about a second). A new instance is a unit
// that has been on a while, past that mute; a session always loads switched on.
class SH101Processor : public juce::AudioProcessor, public lemus::ArtworkUser {
public:
	// rom: the CPU's 2 KB mask ROM (tmp80c49p-6-7301.ic6), or null for voice mode.
	// artwork: the panel's brand marks (host/Artwork.h), none by default.
	explicit SH101Processor(const juce::MemoryBlock *rom, lemus::Artwork artwork = {});
	~SH101Processor() override = default;
	// With SYNC, the firmware's steps come one CPU loop or two after the host's grid: the
	// arpeggio's about 2.2 ms (the KCV moves at the loop that sees the clock edge), the sequencer's
	// about 4.9 ms (the gate drops for that loop, then rises with the new note). The plugin reports
	// the middle, 3.5 ms, with the voice's own latency, so the host puts both within about 1.4 ms of
	// its grid, and holds the host's MIDI back by the difference to a note's own lag (about 3.0 ms
	// through the key scan; all of it in voice mode), so notes stay on theirs.
	static constexpr double kLatencySeconds = 0.0035;
	static constexpr double kFirmwareNoteSeconds = 0.0030;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return "SH-101"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return false; }
	bool isMidiEffect() const override { return false; }
	// RELEASE at maximum keeps the output above -90 dB for up to 36 s after the gate closes (the
	// ENV settles at 33 s); measured with all sources up, the VCF open and at full resonance.
	double getTailLengthSeconds() const override { return 36.0; }

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String &) override {}

	void getStateInformation(juce::MemoryBlock &destData) override;
	void setStateInformation(const void *data, int sizeInBytes) override;

	juce::AudioProcessorValueTreeState &params() { return m_params; }
	bool romLoaded() const { return m_romOk; }
	// The model version a session is saved with (the state's "model"), so old projects keep their
	// sound: a change to the model that changes the sound bumps kModelVersion and keeps the old
	// behaviour for sessions saved with an older version. Sessions saved before there was one are 1.
	static constexpr int kModelVersion = 1;
	int modelVersion() const { return m_modelVersion; }
	// POWER (S1, on the panel). Not saved: a session always loads switched on. Any thread; it
	// takes effect at the next block.
	void setPower(bool on) { m_powered = on; }
	bool powered() const { return m_powered; }
	// For tools and tests: the unit as if switched on long ago, so its output isn't muted.
	// Before the audio runs.
	void settlePower() { m_voice.settlePower(); }
	bool voiceAsleep() const { return m_asleep; } // idle: the synth board model is skipped
	// On by default. Off, the voice runs through silence too (its tests compare the two).
	void setIdleSleep(bool on) { m_idleSleep = on; }
	const lemus::Artwork &artwork() const { return m_artwork; }
	void setArtwork(lemus::Artwork artwork) override { m_artwork = std::move(artwork); } // the view reads it when it's built

	// Panel -> machine and synth board. Queued and applied at the start of the next block.
	void setKey(int key, bool down); // 0 = F3 .. 31 = C6
	void setButton(sh101::Machine::Button b, bool down);
	void setBender(double bend);     // lever, -1..1
	void setModPush(double push);    // lever pushed, 0..1
	// Tests and tools: writes a sequence into the battery-backed RAM and boots the CPU, as loading
	// a session with it would. Not for use while the audio thread runs.
	void writeSequence(const std::vector<sh101::Machine::Step> &steps);

	// Machine -> UI.
	struct Snapshot {
		std::array<float, size_t(sh101::Machine::Led::Count)> leds{};
		std::array<bool, sh101::Machine::kKeys> keys{};
		sh101::CpuLines lines;
		sh101::Voice::Probe probe;
		float lfo = 0;
		bool clock = false;
		bool synced = false;
		double bpm = 120;
		double bend = 0, modPush = 0;
		float peak = 0;
		bool powered = true;  // POWER
		bool muted = false;   // jack board A's power-on mute still acts
	};
	Snapshot snapshot() const;

	// Parameter IDs, shared with the views and the tools.
	struct Id {
		static constexpr const char *lfoRate = "lfoRate", *lfoWave = "lfoWave";
		static constexpr const char *vcoMod = "vcoMod", *range = "range", *pulseWidth = "pulseWidth", *pwmSource = "pwmSource";
		static constexpr const char *pulseLevel = "pulseLevel", *sawLevel = "sawLevel", *subLevel = "subLevel",
		                            *noiseLevel = "noiseLevel", *subMode = "subMode";
		static constexpr const char *vcfFreq = "vcfFreq", *vcfRes = "vcfRes", *vcfEnv = "vcfEnv", *vcfMod = "vcfMod",
		                            *vcfKybd = "vcfKybd";
		static constexpr const char *vcaMode = "vcaMode";
		static constexpr const char *attack = "attack", *decay = "decay", *sustain = "sustain", *release = "release",
		                            *trigger = "trigger";
		static constexpr const char *volume = "volume", *tune = "tune", *portamento = "portamento",
		                            *portaMode = "portaMode", *bendVco = "bendVco", *bendVcf = "bendVcf",
		                            *lfoModDepth = "lfoModDepth";
		static constexpr const char *transpose = "transpose";
		static constexpr const char *sync = "sync", *clockDiv = "clockDiv";
	};

	// The plugin plays the Real unit calibration. For tools: another one (the service notes',
	// Roland's, or fields changed), applied at the next block.
	void setCalibrationOverride(const sh101::Calibration &c)
	{
		m_calOverride = c;
		m_hasCalOverride = true;
		m_calDirty = true;
	}

	// Host-sync clock divisions: CLK pulses per quarter note.
	static double clocksPerBeat(int division);

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
	sh101::Controls currentControls() const;
	void handleMidi(const juce::MidiMessage &m);
	int choice(const char *id) const;
	void renderVoice(float *out, int n, const sh101::ModFrame *mod);
	void wake() { m_asleep = false; m_quietSamples = 0; }
	void switchOn();
	void switchOff();
	void processSwitchedOff(juce::AudioBuffer<float> &buffer, const juce::MidiBuffer &midi);

	juce::AudioProcessorValueTreeState m_params;

	sh101::Machine m_machine;
	sh101::Voice m_voice;
	sh101::Lfo m_lfo;
	sh101::KeyboardDriver m_keyboard;
	sh101::VoiceKeys m_voiceKeys; // voice mode
	bool m_romOk = false;
	int m_modelVersion = kModelVersion;
	std::atomic<bool> m_powered{true};
	bool m_on = true; // POWER as the audio thread last applied it
	lemus::Artwork m_artwork;
	double m_rate = 44100;
	bool m_calDirty = false;
	sh101::Calibration m_calOverride;
	bool m_hasCalOverride = false;
	std::vector<sh101::ModFrame> m_mod;
	// Idle: once the voice has settled to silence it sleeps (renderVoice), skipping the synth
	// board model and putting out exact zeros, until its lines, controls, calibration or the
	// bender change. The CPU and the MODULATOR keep running.
	bool m_asleep = false, m_idleSleep = true;
	int m_quietSamples = 0;
	sh101::Voice::Probe m_quietFrom;
	sh101::CpuLines m_voiceLines;
	sh101::Controls m_lastControls;
	sh101::Performance m_lastPerf;

	bool m_hostWasPlaying = false; // synced to a playing host in the last block
	// The host's MIDI, held back until it is due (kLatencySeconds, kFirmwareNoteSeconds).
	struct HeldMidi {
		int64_t due; // in samples since prepareToPlay
		uint8_t data[3];
		uint8_t size;
	};
	std::array<HeldMidi, 512> m_held{};
	int m_heldFirst = 0, m_heldCount = 0;
	int64_t m_samples = 0; // since prepareToPlay, at the block's start
	double m_syncStartPpq = 0;     // where the host clock's first edge is
	double m_midiBend = 0, m_midiMod = 0;
	std::atomic<double> m_uiBend{0}, m_uiMod{0};
	std::atomic<bool> m_uiBendActive{false};

	// UI -> machine queue
	struct PanelEvent {
		int key;    // 0-31, or -1 - Button
		bool down;
	};
	juce::SpinLock m_queueLock;
	std::vector<PanelEvent> m_queue;
	// Per key and button: the firmware loop count when the panel last changed it (0 = never).
	std::array<uint64_t, sh101::Machine::kKeys + size_t(sh101::Machine::Button::Count)> m_switchChanged{};

	// Machine -> UI
	mutable juce::SpinLock m_snapLock;
	Snapshot m_snap;
	float m_peak = 0;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SH101Processor)
};
