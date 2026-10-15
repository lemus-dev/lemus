#include "SlotParameter.h"

namespace lemus {

namespace {

thread_local bool t_forwarding = false;

juce::ParameterID slotId(int slot)
{
	return {"p" + juce::String(slot + 1).paddedLeft('0', 3), 1};
}

} // namespace

SlotParameter::SlotParameter(int slot) : AudioProcessorParameterWithID(slotId(slot), "Slot " + juce::String(slot + 1)) {}

bool SlotParameter::forwardingFromHost()
{
	return t_forwarding;
}

float SlotParameter::getValue() const
{
	auto *t = target();
	return t != nullptr ? t->getValue() : m_idle.load();
}

void SlotParameter::setValue(float v)
{
	if (auto *t = target()) {
		// Notifying, so the machine sees it the way its own controls would (an APVTS parameter
		// only updates the value the machine reads when it's notified).
		const juce::ScopedValueSetter<bool> forwarding(t_forwarding, true);
		t->setValueNotifyingHost(v);
	} else {
		m_idle.store(v);
	}
}

float SlotParameter::getDefaultValue() const
{
	auto *t = target();
	return t != nullptr ? t->getDefaultValue() : 0.0f;
}

juce::String SlotParameter::getName(int maximumLength) const
{
	auto *t = target();
	return t != nullptr ? t->getName(maximumLength) : AudioProcessorParameterWithID::getName(maximumLength);
}

juce::String SlotParameter::getLabel() const
{
	auto *t = target();
	return t != nullptr ? t->getLabel() : juce::String();
}

juce::String SlotParameter::getText(float v, int maximumLength) const
{
	auto *t = target();
	return t != nullptr ? t->getText(v, maximumLength) : juce::String();
}

float SlotParameter::getValueForText(const juce::String &text) const
{
	auto *t = target();
	return t != nullptr ? t->getValueForText(text) : text.getFloatValue();
}

} // namespace lemus
