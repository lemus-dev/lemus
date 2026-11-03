#pragma once

#include "tr909/core/machine.h"
#include "tr909/core/parallel.h"
#include "tr909/core/revisions.h"
#include "tr909/core/voiceboard.h"

#include "host/Artwork.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

// The TR-909: the original firmware (v4.0) on its CPU board model, driving the voicing board model
// through the trigger and accent latches. MIDI goes into the firmware's own MIDI IN, as on the
// unit: notes play the voices (velocity sets the accent), the knobs follow control changes. With
// SYNC on, the 909 is put in its MIDI sync mode and the host transport is sent to it as MIDI clock,
// start or song position and continue, and stop. Without the firmware ROM it is its voicing board
// alone, played from MIDI ("voice mode"); without a sample ROM, its voice is silent.
class TR909Processor : public juce::AudioProcessor, public lemus::ArtworkUser {
public:
	// firmware: the 8 KB program ROM (tr909_v4.ic609), or null for voice mode. hihat, crash, ride:
	// the 32 KB sample ROMs, each null for none. artwork: the panel's brand marks (host/Artwork.h),
	// none by default. v1: the version 1.0 program ROM (tr909_v1.ic609), whose loader
	// makes the factory patterns (core/factory.h), or null for none.
	TR909Processor(const juce::MemoryBlock *firmware, const juce::MemoryBlock *hihat, const juce::MemoryBlock *crash,
	               const juce::MemoryBlock *ride, lemus::Artwork artwork = {}, const juce::MemoryBlock *v1 = nullptr);
	~TR909Processor() override = default;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
	void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

	// The host's audio workgroup (macOS): the voices' worker threads join it, so the system schedules
	// them as it does the host's own audio threads.
	void audioWorkgroupContextChanged(const juce::AudioWorkgroup &workgroup) override;

	juce::AudioProcessorEditor *createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return "TR-909 Rhythm Composer"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return true; } // the MIDI OUT jack (machine.cmake MIDI_OUT)
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
	bool romLoaded() const { return m_romOk; } // the firmware: false is voice mode
	bool sampleRomLoaded(tr909::Voice v) const;
	bool voiceAsleep() const { return m_asleep; } // idle: the voicing board is skipped
	// The model version a session is saved with (the state's "model"), so old projects keep their
	// sound: a change to the model that changes the sound bumps kModelVersion and keeps the old
	// behaviour for sessions saved with an older version.
	static constexpr int kModelVersion = 1;
	int modelVersion() const { return m_modelVersion; }
	const lemus::Artwork &artwork() const { return m_artwork; }
	void setArtwork(lemus::Artwork artwork) override { m_artwork = std::move(artwork); } // the view reads it when it's built

	// The panel's knobs: parameter id and name, in the order the host's slots take them.
	struct Knob {
		const char *id, *name;
		float def;
	};
	static const std::vector<Knob> &knobs();

	// Panel -> machine, queued and applied at the start of the next audio block, in order.
	using Key = tr909::Machine::Key;
	void setKey(Key key, bool down);
	// True when the v1.0 ROM made the memory a new 909 held (Bank I: patterns 1-8 and track 1),
	// which a new instance starts with.
	bool hasFactoryPatterns() const { return m_factory != nullptr; }
	// The battery RAM as it would be loaded with a session, and the CPU restarted warm.
	void setRam(const std::vector<uint8_t> &ram);
	std::vector<uint8_t> ram() const;
	// Plays a voice from the panel or a tool: a MIDI note into the firmware (or straight to the
	// voicing board in voice mode).
	void playNote(int note, int velocity);
	// The rear panel's MULTI OUT jacks: a cable in one takes its voice out of the mix (the jack's
	// switching contact) to the plugin's output of that name; with none, every voice is in the
	// main pair and the jack outputs are silent. Saved with the session. outputOn: whether the
	// host has that output turned on (a cable into an output it has off goes nowhere).
	void setCable(tr909::Voice v, bool in) { m_cables[size_t(v)] = in; }
	bool cable(tr909::Voice v) const { return m_cables[size_t(v)]; }
	bool outputOn(tr909::Voice v) const;
	// The rear's POWER switch. Off: silence, no MIDI out, the panel dark, the keys and MIDI in
	// ignored; the CPU and the board stand still and the battery keeps the memory. On: the power-on
	// (boot()): the firmware's boot, the master mute, the thumps. Not saved: a session always loads
	// switched on, and warm (warmStart()). Message thread.
	void setPower(bool on);
	bool powered() const { return m_powered; }
	// Which unit this is, by its serial number: the board has the production changes up to it
	// (core/revisions.h, SN p.7), from the earliest units (360100) to the late board (426700 up,
	// the default). Changing it re-solders parts: the voices' circuits are rebuilt at their operating
	// points. Saved with the session. Message thread.
	void setSerial(int serial);
	int serial() const { return m_serial; }
	// The clap's trimmer TM ("tm" parameter, its wiper 0-1) as the unit left the factory.
	static double factoryTm();
	// SERVICE's scope: two probes on the board (VoiceBoard::setProbe: "hc.env1", "mix.mhc"), each
	// recorded at the board's rate into a ring with the trigger lines (channel 2: a bit per voice,
	// Voice order), from the next block. While a probe is on, the board doesn't sleep, as a unit
	// on the bench doesn't. "" takes a probe off. Message thread.
	static constexpr int kProbeFrames = 1 << 17;
	void setProbe(int channel, const juce::String &where);
	juce::String probe(int channel) const;
	bool probeFound(int channel) const { return m_probeOk[size_t(channel & 1)]; } // false: no such node
	double probeRate() const { return m_board.internalRate(); }
	uint32_t probeWritten() const { return m_probeWrite.load(std::memory_order_acquire); }
	void readProbe(int channel, float *dst, int n, uint32_t end) const; // the n frames up to `end`

	// Machine -> UI.
	struct Snapshot {
		std::array<float, size_t(tr909::Machine::Led::Count)> leds{};
		std::array<int, 3> digits{{15, 15, 15}}; // the MEAS/TEMPO display's BCD codes (10-15 blank)
		std::array<float, tr909::kVoices> voices{}; // each voice's jack level, for the UI
		int syncMode = 0;     // the firmware's: 0 INT, 1 MIDI, 2 DIN
		bool synced = false;  // the host transport is driving the clock
		bool running = false; // the sequencer is running (its START flag)
		bool powered = true;  // the rear's POWER switch
		int tempo = -1;       // the firmware's tempo value (VV:30) in INT sync, which it runs at; -1 none
	};
	Snapshot snapshot() const;

	// TEMPO (VR601) rotation to the BPM the v4.0 firmware runs at after a power-on or a session
	// load with the knob there, and back. The firmware follows the knob with a dead band (13FC,
	// docs/firmware.md "Sync"), so a knob turned later can run a step either side of this: the
	// panel shows snapshot().tempo, what it runs at. tempoBpmOf: the BPM of a tempo value (VV:30).
	static double tempoBpm(double rotation);
	static double tempoRotation(double bpm);
	static double tempoBpmOf(int value);

	// MIDI notes, the v4.0 key assignment (SN p.14): each voice's first note.
	static int noteFor(tr909::Voice v);
	static int voiceOfNote(int note); // -1 if none

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
	tr909::Controls currentControls() const;
	void handleMidi(const juce::MidiMessage &m);
	void voiceModeNote(int note, int velocity);
	void runSync(bool hostPlaying, double ppq, double bpm, int n, std::vector<std::pair<int, std::vector<uint8_t>>> &events);
	void renderBoard(float *l, float *r, int n, int offset);
	void midiOutByte(uint8_t b, int at, juce::MidiBuffer &out);
	void boot(); // switched on: the CPU from reset (the battery RAM as it is), the board's power-on
	// A 909 that has been on a while: the CPU from reset run past its boot at once, writes dropped,
	// the board left at its operating points. A new instance (State: Reset too) and a session load.
	// Under the callback lock or before prepare.
	void warmStart();
	static constexpr double kWarmSeconds = 1.0;

	juce::AudioProcessorValueTreeState m_params;
	std::vector<std::atomic<float> *> m_knobValues;
	std::atomic<float> *m_tempo = nullptr, *m_accent = nullptr, *m_sync = nullptr, *m_tm = nullptr;
	int m_serial = tr909::kLateSerial;
	// the scope (setProbe): wanted on the message thread, set on the audio thread
	juce::SpinLock m_probeLock;
	std::array<juce::String, tr909::VoiceBoard::kProbes> m_probeWant;
	std::atomic<bool> m_probeDirty{false};
	std::array<std::atomic<bool>, tr909::VoiceBoard::kProbes> m_probeOk{};
	bool m_probing = false;
	std::array<std::vector<float>, tr909::VoiceBoard::kProbes + 1> m_probeRing;
	std::atomic<uint32_t> m_probeWrite{0};

	tr909::Machine m_machine;
	tr909::VoiceBoard m_board;
	bool m_romOk = false;
	std::array<bool, 3> m_samplesOk{}; // hh, crash, ride
	lemus::Artwork m_artwork;
	double m_rate = 44100;
	int m_modelVersion = kModelVersion;
	tr909::Controls m_lastControls;
	bool m_asleep = false;
	// A boot is the 909 switched on: the board's power-on (VoiceBoard::powerOn, its master mute) at
	// the next block.
	std::atomic<bool> m_powerOn{false};
	std::atomic<bool> m_powered{true};
	std::array<float *, tr909::kVoices> m_jacks{}; // this block's MULTI OUT buses with a cable in, that the host turned on
	std::array<std::atomic<bool>, tr909::kVoices> m_cables{};

	// Voice mode: a MIDI note's trigger, high for one panel scan, as the firmware's.
	std::array<int, tr909::kVoices> m_voiceTrig{}; // samples left of each voice-mode trigger
	int m_trigSamples = 0;
	double m_stateAcc = 0; // the CPU's states owed to the samples run (a fraction)

	// Host sync: the firmware's sync mode is stepped with SHIFT + TEMPO MODE (a key macro).
	bool m_hostWasPlaying = false;
	double m_lastClock = 0; // the last 24 ppqn clock sent
	int m_syncMacro = 0;    // samples to the macro's next step
	int m_syncMacroStep = 0;

	struct KeyEvent {
		Key key;
		bool down;
	};
	juce::SpinLock m_queueLock;
	std::vector<KeyEvent> m_queue;
	std::vector<uint8_t> m_uiMidi;
	std::unique_ptr<std::array<uint8_t, tr909::Machine::kRamSize>> m_factory; // the factory memory, made once
	bool m_running = false; // the sequencer, as its MIDI out tells it (FA/FB start, FC stop)
	// The MIDI OUT jack's bytes made into messages (running status kept across blocks)
	uint8_t m_outStatus = 0;
	std::array<uint8_t, 2> m_outData{};
	int m_outCount = 0;

	mutable juce::SpinLock m_snapLock;
	Snapshot m_snap;

	// The voices' jobs on worker threads (core/parallel.h): a block takes about as long as its
	// heaviest voice. Last, so it stops before the rest goes.
	juce::SpinLock m_workgroupLock;
	juce::AudioWorkgroup m_workgroup;
	std::vector<juce::WorkgroupToken> m_tokens;
	std::unique_ptr<tr909::Parallel> m_parallel;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TR909Processor)
};
