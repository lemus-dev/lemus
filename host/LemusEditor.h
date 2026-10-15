#pragma once

#include "LemusProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// An empty instance shows the machine picker (in a machine's own plugin, only while the machine's
// required ROMs are missing: the picker then lists that machine alone). Once a machine is
// loaded, the editor is a thin
// Lemus bar above the machine's own editor, and it takes the machine editor's size, resize limits
// and aspect ratio (plus the bar).
class LemusEditor final : public juce::AudioProcessorEditor, private juce::ChangeListener, private juce::ComponentListener {
public:
	explicit LemusEditor(LemusProcessor &);
	~LemusEditor() override;

	void paint(juce::Graphics &) override;
	void resized() override;

	static constexpr int kBarHeight = 32;
	static constexpr int kPickerWidth = 860, kPickerHeight = 520;

	// For tests: the machine's editor, or nullptr while the picker shows.
	juce::AudioProcessorEditor *machineEditor() const { return m_inner.get(); }

private:
	class Picker;
	class Bar;

	// Applies the machine editor's constraints to the area below the bar.
	class Constrainer final : public juce::ComponentBoundsConstrainer {
	public:
		explicit Constrainer(LemusEditor &e) : m_editor(e) {}
		void checkBounds(juce::Rectangle<int> &bounds, const juce::Rectangle<int> &previous, const juce::Rectangle<int> &limits, bool top,
		                 bool left, bool bottom, bool right) override;

	private:
		LemusEditor &m_editor;
	};

	void changeListenerCallback(juce::ChangeBroadcaster *) override;
	void componentMovedOrResized(juce::Component &, bool wasMoved, bool wasResized) override;
	void showCurrentMachine();
	void refresh(); // the ROM folder changed or was rescanned

	LemusProcessor &m_proc;
	Constrainer m_constrainer{*this};
	std::unique_ptr<Picker> m_picker;
	std::unique_ptr<Bar> m_bar;
	std::unique_ptr<juce::AudioProcessorEditor> m_inner;
	std::unique_ptr<juce::Component> m_prompt; // the first start without an optional ROM, over the rest
	juce::AudioProcessor *m_innerFor = nullptr;
	bool m_layingOut = false;
	juce::String m_flash; // a notice for the bar, after a reload
	juce::int64 m_flashUntil = 0;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LemusEditor)
};
