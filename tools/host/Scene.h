// A scene: what lemus-render (and the A/B player) plays through one or more plugins. JSON, see
// tools/host/README.md. Times are in beats at the scene's tempo; parameter values are 0-1 (the
// plugin's normalised value) or text the plugin parses ("440 Hz", "Square").
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <vector>

namespace lemus::host {

struct Note {
	double at = 0, len = 1; // beats
	int note = 60, velocity = 100;
};

struct Automation {
	juce::String param;                          // generic name, mapped per source
	std::vector<std::pair<double, double>> points; // (beat, normalised value), linear between
	double valueAt(double beat) const;
};

struct Source {
	juce::String label;   // names the output: <scene>-<label>.wav
	juce::String plugin;  // "Lemus", "Roland/TB-303", a .vst3/.component path, or "au:Name"
	juce::String machine; // Lemus / Lemus FX: the machine to load
	juce::File state;     // optional: a plugin state saved before (getStateInformation), applied first
	juce::File audio;     // instead of a plugin: a recording, played from the scene's start
	std::map<juce::String, juce::String> names; // generic parameter name -> the plugin's own
	std::map<juce::String, juce::var> params;   // in the plugin's own names, after the scene's
	double gainDb = 0;    // applied to the output (level matching)
	// A clip on a plugin's output, standing in for a recording chain that squashed or cut the peaks
	// of the recording it is compared with: level * (tanh(x / level + bias) - tanh(bias)), or with
	// clipHard the same through a hard limit at ±1 instead of tanh. clipLevel 0: none.
	double clipLevel = 0, clipBias = 0;
	bool clipHard = false;
};

// Source templates by name ("tb303.roland"), from every machines/<id>/host/sources.json (or the
// files given). A template's own relative paths (state, audio) are taken next to its file.
struct Templates {
	std::map<juce::String, std::pair<juce::var, juce::File>> entries; // name -> template, its folder
	static juce::Array<juce::File> standardFiles();
	static Templates load(const juce::Array<juce::File> &files, juce::String &error);
};

struct Scene {
	juce::String name;
	double rate = 48000, bpm = 120, beats = 16, tail = 1;
	int block = 256, automationStep = 32; // automation is applied every this many samples
	bool onsFirst = false; // at the same time, note-ons before note-offs (as Onyx Tracker sends
	                       // them, so touching notes are legato); else note-offs first
	bool playing = true;   // the transport the plugins see (false: stopped, so a plugin's own
	                       // sequencer stays quiet and only the notes play)
	std::vector<Note> notes;
	std::map<juce::String, juce::var> params; // generic names
	std::vector<Automation> automation;
	juce::File input;                         // audio into the plugin (effects); silence if none
	std::vector<Source> sources;

	double seconds() const { return beats * 60.0 / bpm + tail; }
	double samplesPerBeat() const { return rate * 60.0 / bpm; }

	// Parses `file`. Sources may say {"use": "tb303.roland"} to start from a template; their own
	// fields then override it.
	static Scene load(const juce::File &file, const Templates &templates, juce::String &error);
};

// "C-3" / "C#3" / "Db3" (Onyx Tracker's names, C-3 = MIDI 48) or a number.
int parseNote(const juce::var &v);

} // namespace lemus::host
