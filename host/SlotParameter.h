// One of the host's fixed parameter slots. Hosts see the same parameters whatever the machine,
// so automation stays attached when a session reopens. Slot n stands in for the machine's
// parameter n: it takes that parameter's name, value text, units and default, and is named
// "Slot n" while no machine parameter uses it.
//
// Slots always look continuous (0-1) to the host, even for a switch or a choice. Hosts read a
// parameter's step count and scale once and may not read them again when a machine is loaded,
// so changing them per machine could scale automation wrongly. The value text still comes from
// the machine ("On", "LM308").
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace lemus {

class SlotParameter final : public juce::AudioProcessorParameterWithID {
public:
	explicit SlotParameter(int slot);

	// Points the slot at a machine parameter, or at nothing. Called with processing locked out.
	void setTarget(juce::AudioProcessorParameter *p) { m_target.store(p); }
	juce::AudioProcessorParameter *target() const { return m_target.load(); }

	// True while a slot on this thread is passing a host change on to its machine parameter, so
	// the processor doesn't report it back to the host as a change the machine made.
	static bool forwardingFromHost();

	float getValue() const override;
	void setValue(float v) override;
	float getDefaultValue() const override;
	juce::String getName(int maximumLength) const override;
	juce::String getLabel() const override;
	juce::String getText(float v, int maximumLength) const override;
	float getValueForText(const juce::String &text) const override;

private:
	std::atomic<juce::AudioProcessorParameter *> m_target{nullptr};
	std::atomic<float> m_idle{0.0f}; // the value while no machine parameter uses the slot
};

} // namespace lemus
