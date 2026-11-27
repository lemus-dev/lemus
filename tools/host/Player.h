// Loads a scene's source plugin and plays the scene through it, block by block: the notes as
// MIDI, the automation as parameter changes every few samples, the input file into its inputs,
// a playhead at the scene's tempo. lemus-render runs it offline; the A/B player in real time.
#pragma once

#include "Scene.h"

#include <array>

namespace lemus::host {

// A plugin file for "Lemus", "Roland/TB-303", "au:Name" or a path: the user's and the system's
// VST3 and Components folders are searched.
juce::File findPlugin(const juce::String &name);

class Player : private juce::AudioPlayHead {
public:
	// Loads and prepares the plugin, picks the Lemus machine, restores the state, sets the
	// parameters, runs the preroll. Check error() afterwards.
	Player(juce::AudioPluginFormatManager &formats, const Scene &scene, const Source &source, bool realtime);
	~Player() override;

	const juce::String &error() const { return m_error; }
	const juce::StringArray &warnings() const { return m_warnings; }
	juce::AudioPluginInstance *plugin() { return m_plugin.get(); }
	bool isRecording() const { return m_recording.getNumSamples() > 0; }
	void setRealtime(bool realtime);
	int numOutputs() const;
	int latency() const;
	int64_t length() const; // the scene in samples

	// Plays samples [position, position + out.getNumSamples()) of the scene into out (out's channel
	// count is the plugin's outputs). Positions past the scene's notes play the tail.
	void process(juce::AudioBuffer<float> &out, int64_t position);
	// All notes off, before jumping; with notes still held (a restart mid-loop), the plugin then
	// runs kKeyUp of silence, so the next note is a new key-down.
	void reset();
	// Played as a loop ending at `end` (Lemus AB): every held note is released kKeyUp before the
	// end, and note-ons in that stretch are dropped, so each pass starts with new key-downs, as
	// lemus-render's single pass does (a note held into the next pass makes its first note legato:
	// a mono synth's envelopes don't start again). The MS-20's keyboard needs 5-10 ms of key-up.
	void setLoop(int64_t end) { m_loopEnd = end; }
	static constexpr double kKeyUp = 0.02; // seconds

	// The plugin parameter a generic name maps to (the source's "names", else the name itself;
	// matched to parameter names, case-insensitively), or null.
	juce::AudioProcessorParameter *findParameter(const juce::String &generic) const;

private:
	juce::Optional<PositionInfo> getPosition() const override;
	void setValue(juce::AudioProcessorParameter *p, const juce::var &v);

	const Scene &m_scene;
	const Source &m_source;
	std::unique_ptr<juce::AudioPluginInstance> m_plugin;
	juce::String m_error;
	juce::StringArray m_warnings;
	std::vector<std::pair<int64_t, juce::MidiMessage>> m_events; // sorted by sample
	std::vector<std::pair<const Automation *, juce::AudioProcessorParameter *>> m_automation;
	juce::AudioBuffer<float> m_input, m_work, m_recording;
	int64_t m_now = 0;
	size_t m_next = 0;
	int64_t m_loopEnd = -1;            // setLoop's end, or -1: no loop
	std::array<int, 128> m_held = {}; // note-ons sent minus note-offs, per note
	bool m_prerolling = false; // the source's preroll: the transport reads stopped
};

} // namespace lemus::host
