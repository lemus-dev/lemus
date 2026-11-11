#pragma once

#include "tr808/core/machine.h"
#include "tr808/core/parallel.h"
#include "tr808/core/revisions.h"
#include "tr808/core/trimmers.h"
#include "tr808/core/voiceboard.h"

#include "host/Artwork.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

// The TR-808: the original firmware on its CPU board model, driving the voicing board model. MIDI
// notes play the voices directly (velocity 100 and up accents), as Roland's own software does. With
// SYNC on, the host transport is the DIN sync input (run level and 24 ppqn clock); otherwise
// START/STOP and TEMPO run the unit. Without the CPU's ROM it is its voicing board alone, played
// from MIDI ("voice mode").
class TR808Processor : public juce::AudioProcessor, public lemus::ArtworkUser {
public:
	// rom: the CPU's 2 KB mask ROM (d650c-085.ic4), or null for voice mode.
	// artwork: the panel's brand marks (host/Artwork.h), none by default.
	explicit TR808Processor(const juce::MemoryBlock *rom, lemus::Artwork artwork = {});
	~TR808Processor() override = default;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
	// The host's audio workgroup (macOS): the voices' worker threads join it, so the system schedules
	// them as it does the host's own audio threads.
	void audioWorkgroupContextChanged(const juce::AudioWorkgroup &workgroup) override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return "TR-808 Rhythm Composer"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return false; }
	bool isMidiEffect() const override { return false; }
	double getTailLengthSeconds() const override { return 2.0; }

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String &) override {}

	void getStateInformation(juce::MemoryBlock &destData) override;
	void setStateInformation(const void *data, int sizeInBytes) override;

	juce::AudioProcessorValueTreeState &params() { return m_params; }
	bool romLoaded() const { return m_romOk; }
	bool voiceAsleep() const { return m_asleep; } // idle: the voicing board is skipped
	// The model version a session is saved with (the state's "model"), so old projects keep their
	// sound: a change to the model that changes the sound bumps kModelVersion and keeps the old
	// behaviour for sessions saved with an older version.
	static constexpr int kModelVersion = 1;
	int modelVersion() const { return m_modelVersion; }
	const lemus::Artwork &artwork() const { return m_artwork; }
	void setArtwork(lemus::Artwork artwork) override { m_artwork = std::move(artwork); } // the view reads it when it's built

	// Panel -> machine, queued and applied at the start of the next audio block, in order.
	enum Key : int { Step1 = 0, Clear = 16, StartStop = 17, Tap = 18 }; // steps 0-15
	void setKey(int key, bool down);
	// Tests and tools: the RAM as it would be loaded with a session, and the CPU restarted. Not for
	// use while the audio thread runs.
	void setRam(const std::vector<uint8_t> &ram);
	std::vector<uint8_t> ram() const;

	// Machine -> UI.
	struct Snapshot {
		std::array<float, 16> steps{};
		std::array<float, size_t(tr808::Machine::Led::Count)> leds{};
		std::array<float, tr808::kVoices> voices{}; // each voice's jack level, for the UI
		bool running = false;
		bool synced = false; // the host transport is driving the clock
		bool power = true;
		double bpm = 120;
	};
	Snapshot snapshot() const;

	// The POWER switch (the panel's push button). Off: silent, the LEDs dark, the CPU, the clock,
	// the keys' START/STOP and MIDI dead; ringing voices die away unheard. On: the CPU starts from
	// reset, stopped, with the battery-backed patterns kept. Not saved: an instance starts on.
	void setPower(bool on) { m_power = on; }
	bool powered() const { return m_power; }

	// TEMPO and FINE (0-1) to BPM (tr808::TempoClock::bpm), TM2 at its trim.
	static double tempoBpm(double tempo, double fine) { return tr808::TempoClock::bpm(tempo, fine); }

	// SERVICE (plugin/HardwareService.cpp). Which unit this is, by its serial number: the board has
	// the production changes up to it (core/revisions.h, SN p.15), from the first units (000001) to
	// the late board (010600 up; the default is Fischer's 103852). The snare's and the clap's
	// "PORTION CHANGED" (SN p.9, no serial given): the later values or the drawn ones. Each rebuilds
	// the board's circuits at their operating points; with `retrim`, the six trimmers are set to
	// that unit's trims. Saved with the session. Message thread.
	void setSerial(int serial, bool retrim = true);
	int serial() const { return m_serial; }
	void setPortionChanged(bool late, bool retrim = true);
	bool portionChanged() const { return m_portionLate; }
	// The six trimmers (core/trimmers.h, in its order), host parameters: their rotations, 0-1.
	static constexpr const char *kTrimIds[tr808::kTrimmers] = {"tm_int", "tm_tempo", "tm_cp", "tm_noise", "tm_cb1", "tm_cb2"};
	double unitTrim(int i) const;       // the chosen unit's rotation for trimmer i
	tr808::TrimParts trimParts() const; // the parts round them on this unit
	// What trimmer i at `rotation` reads by its procedure (SN p.14): TP2's, TP3's (TEMPO and FINE
	// fully clockwise), TP6's and TP7's periods in ms, TP4's mV rms, TM3's rotation.
	double trimReading(int i, double rotation) const;
	// Plays a voice from the panel or a tool: a MIDI note, at the next block.
	void playNote(int note, int velocity);
	// The rear panel's MULTI OUT jacks: a cable in one takes its voice out of the mix (the jack's
	// normal contact) to the plugin's output of that name; with none, every voice is in the main
	// output and the jack outputs are silent. Saved with the session. outputOn: whether the host
	// has that output turned on (a cable into an output it has off goes nowhere).
	void setCable(tr808::Voice v, bool in) { m_cables[size_t(v)] = in; }
	bool cable(tr808::Voice v) const { return m_cables[size_t(v)]; }
	bool outputOn(tr808::Voice v) const;
	// SERVICE's scope: two probes (VoiceBoard::setProbe's nodes and signals, and "tp2", "tp3": the
	// CPU board's INT and tempo clocks), each recorded at the board's rate into a ring with the
	// voices' gates (channel 2: kBit* bits, the common trig with each data line), from the next block.
	// While a probe is on, the board doesn't sleep, as a unit on the bench doesn't. "" takes a probe
	// off. Message thread.
	static constexpr int kProbeFrames = 1 << 17;
	void setProbe(int channel, const juce::String &where);
	juce::String probe(int channel) const;
	bool probeFound(int channel) const { return m_probeOk[size_t(channel & 1)]; } // false: no such node
	double probeRate() const { return m_board.internalRate(); }
	uint32_t probeWritten() const { return m_probeWrite.load(std::memory_order_acquire); }
	void readProbe(int channel, float *dst, int n, uint32_t end) const; // the n frames up to `end`
	double tp4PerNoiseRms() const { return m_board.tp4PerNoiseRms(); }
	bool boardRunsJobs() const { return m_board.runsJobs(); } // tools: the voices as jobs, not interleaved

	// MIDI note -> voice (kBit*), 0 if none: Roland's TR-08 map, with the General MIDI drum notes
	// for the same sounds. The conga, claves and maracas notes play their shared rows; the panel's
	// switches pick which sound that row makes.
	static uint16_t noteVoice(int note);
	static constexpr int kAccentVelocity = 100;

	static constexpr const char *kModeId = "mode";
	static constexpr const char *kInstrumentId = "instrument";

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
	tr808::Controls currentControls() const;
	tr808::Calibration calibration() const; // the unit, its revision and its trims
	void rebuild(bool retrim);              // the board for the unit chosen (setSerial, setPortionChanged)
	double trimValue(int i, float rotation) const; // what trimmer i sets (the unit's own value at its own rotation)
	void applyTrims(const std::array<float, tr808::kTrimmers> &rotations);
	void applyPanelSwitches();
	void handleMidi(const juce::MidiMessage &m, int sample);
	void renderBoard(float *out, int n, int offset); // offset: where out starts in the block (the jacks')

	juce::AudioProcessorValueTreeState m_params;
	struct KnobRef {
		const char *id;
		double tr808::Controls::*field;
		std::atomic<float> *value = nullptr;
	};
	std::vector<KnobRef> m_knobs;
	std::atomic<float> *m_lc, *m_mc, *m_hc, *m_cl, *m_ma;
	std::atomic<float> *m_tempo, *m_fine, *m_mode, *m_instrument, *m_autoFill, *m_variation, *m_ifVariation, *m_prescale, *m_sync;

	// SERVICE: the unit (setSerial, setPortionChanged) and its trimmers. The unit's own
	// rotations and values are set with the board (under the callback lock): a trimmer at its unit's
	// rotation sets that unit's value as it is, not one worked out from the rotation.
	int m_serial = tr808::kFittedSerial;
	bool m_portionLate = true;
	tr808::TrimParts m_trimParts;
	std::array<std::atomic<float> *, tr808::kTrimmers> m_trims{};
	std::array<double, tr808::kTrimmers> m_unitRot{}, m_unitValue{};
	std::array<bool, tr808::kTrimmers> m_unitExact{};
	std::array<float, tr808::kTrimmers> m_trimsApplied{};
	double m_tm2 = 0.5;     // TM2's rotation
	bool m_tm2AtTrim = true; // at its unit's: TempoClock::bpm as it is
	// the scope (setProbe): wanted on the message thread, set on the audio thread
	juce::SpinLock m_probeLock;
	std::array<juce::String, tr808::VoiceBoard::kProbes> m_probeWant;
	std::atomic<bool> m_probeDirty{false};
	std::array<std::atomic<bool>, tr808::VoiceBoard::kProbes> m_probeOk{};
	std::array<int, tr808::VoiceBoard::kProbes> m_cpuProbe{}; // 0: the board's; 2, 3: TP2, TP3
	bool m_probing = false;
	std::array<std::vector<float>, tr808::VoiceBoard::kProbes> m_cpuProbeValues; // this block's, per host sample
	double m_intAcc = 0; // TP2 without the CPU (voice mode): the INT clock still runs
	std::array<std::vector<float>, tr808::VoiceBoard::kProbes + 1> m_probeRing;
	std::atomic<uint32_t> m_probeWrite{0};
	juce::SpinLock m_uiNoteLock;
	std::vector<std::pair<int, int>> m_uiNotes; // playNote's, for the next block

	tr808::Machine m_machine;
	tr808::VoiceBoard m_board;
	tr808::TempoClock m_clock;
	bool m_romOk = false;
	lemus::Artwork m_artwork;
	double m_rate = 44100;
	int m_modelVersion = kModelVersion;

	// The common trig as the board sees it: the CPU's step pulse with its port data, or a MIDI
	// note's own 1 ms pulse with its voice's line (the ports carry RAM addresses between steps).
	unsigned m_lastPulses = 0;
	int m_cpuPulse = 0;   // samples left of the CPU's step (the IC6 monostable, 1 ms)
	int m_midiPulse = 0;  // samples left of a MIDI trigger
	uint16_t m_midiBits = 0;
	bool m_pulseLevel = false;
	// Idle: once the board has settled it sleeps, skipping the circuits and putting out exact
	// zeros, until a trigger or a knob wakes it. The CPU keeps running.
	bool m_asleep = false;
	tr808::Controls m_lastControls;
	bool m_internalRun = false; // START/STOP's flip-flop (IC2b)
	std::atomic<bool> m_power{true};
	bool m_wasPowered = true;
	float m_outGain = 1.0f; // ramps to 0 over 5 ms when switched off, back up when on
	std::array<float *, tr808::kVoices> m_jacks{}; // this block's MULTI OUT buses with a cable in, that the host turned on
	std::array<std::atomic<bool>, tr808::kVoices> m_cables{};
	bool m_tapDown = false;
	bool m_hostWasPlaying = false;

	struct KeyEvent {
		int key;
		bool down;
	};
	juce::SpinLock m_queueLock;
	std::vector<KeyEvent> m_queue;

	mutable juce::SpinLock m_snapLock;
	Snapshot m_snap;

	// The voices' jobs on worker threads (core/parallel.h): a block takes about as long as its
	// heaviest voice. Last, so it stops before the rest goes.
	juce::SpinLock m_workgroupLock;
	juce::AudioWorkgroup m_workgroup;
	std::vector<juce::WorkgroupToken> m_tokens;
	std::unique_ptr<tr808::Parallel> m_parallel;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TR808Processor)
};
