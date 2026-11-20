#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstring>

juce::AudioProcessorValueTreeState::ParameterLayout RatProcessor::createLayout()
{
	using namespace juce;
	auto number = [](const String &t) { return t.retainCharacters("-+.0123456789").getFloatValue(); };
	auto knob = [number](const String &id, const String &name, float def) {
		return std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name, NormalisableRange<float>(0.0f, 10.0f, 0.01f), def,
		                                             AudioParameterFloatAttributes()
		                                                 .withStringFromValueFunction([](float v, int) { return String(v, 1); })
		                                                 .withValueFromStringFunction(number));
	};
	std::vector<std::unique_ptr<RangedAudioParameter>> p;
	p.push_back(knob("distortion", "Distortion", 5.0f));
	p.push_back(knob("filter", "Filter", 4.0f));
	p.push_back(knob("volume", "Volume", 5.0f));
	p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"on", 1}, "On", true));
	p.push_back(std::make_unique<AudioParameterFloat>(
		ParameterID{"input", 1}, "Input", NormalisableRange<float>(-12.0f, 24.0f, 0.1f), 0.0f,
		AudioParameterFloatAttributes()
			.withLabel("dB")
			.withStringFromValueFunction([](float v, int) { return (v > 0 ? "+" : "") + String(v, 1) + " dB"; })
			.withValueFromStringFunction(number)));
	return {p.begin(), p.end()};
}

RatProcessor::RatProcessor(lemus::Artwork artwork)
	: AudioProcessor(BusesProperties()
		.withInput("Input", juce::AudioChannelSet::stereo(), true)
		.withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  m_params(*this, nullptr, "RAT", createLayout()), m_artwork(std::move(artwork))
{
	m_dist = m_params.getRawParameterValue("distortion");
	m_filter = m_params.getRawParameterValue("filter");
	m_volume = m_params.getRawParameterValue("volume");
	m_engaged = m_params.getRawParameterValue("on");
	m_input = m_params.getRawParameterValue("input");
}

bool RatProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	const auto out = layouts.getMainOutputChannelSet();
	const auto in = layouts.getMainInputChannelSet();
	if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
		return false;
	return in == out;
}

rat::Controls RatProcessor::controls() const
{
	rat::Controls c;
	c.distortion = m_dist->load() / 10.0;
	c.filter = m_filter->load() / 10.0;
	c.volume = m_volume->load() / 10.0;
	return c;
}

rat::Calibration RatProcessor::calibration() const
{
	return rat::capturedCalibration();
}

void RatProcessor::applySettings(bool force)
{
	for (auto &c : m_ch) {
		if (force) {
			// Building the circuit resets it to its operating point (a few ms of work, only on prepare).
			c.pedal().setControls(controls(), true);
			c.pedal().setCalibration(calibration());
			c.setLevelDbu(levelDbu());
		} else {
			c.pedal().setControls(controls());
		}
		c.setInputGainDb(m_input->load());
		c.setEngaged(engaged());
	}
}

void RatProcessor::prepareToPlay(double sampleRate, int)
{
	m_rate = sampleRate;
	applySettings(true);
	for (auto &c : m_ch)
		c.prepare(sampleRate);
	m_idle = {};
	m_lockstep = true; // both channels are now in the same state
}

void RatProcessor::writeTestSignal(juce::AudioBuffer<float> &buffer, int channels)
{
	// A palm-muted riff on the low E and A strings, eighth notes at 120 BPM, about -12 dBFS.
	static const double notes[] = {82.41, 82.41, 98.0, 82.41, 110.0, 82.41, 123.47, 98.0};
	for (int i = 0; i < buffer.getNumSamples(); ++i) {
		const double step = m_testTime / 0.25;
		const int k = int(step) % 8;
		const double t = (step - std::floor(step)) * 0.25;
		const double f = notes[k];
		double v = 0;
		for (int h = 1; h <= 6; ++h)
			v += std::sin(2 * juce::MathConstants<double>::pi * f * h * t) / (h * h);
		v *= 0.25 * std::exp(-t * 9);
		v += 0.002 * (m_noise.nextFloat() * 2 - 1);
		for (int ch = 0; ch < channels; ++ch)
			buffer.setSample(ch, i, float(v));
		m_testTime += 1 / m_rate;
	}
}

void RatProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &)
{
	juce::ScopedNoDenormals noDenormals;
	applySettings(false);
	const int n = buffer.getNumSamples();
	const int channels = std::min(buffer.getNumChannels(), 2);
	for (int ch = channels; ch < buffer.getNumChannels(); ++ch)
		buffer.clear(ch, 0, n);
	if (m_testSignal)
		writeTestSignal(buffer, channels);

	const auto k = controls();
	const std::array<double, 4> settings{k.distortion, k.filter, k.volume, engaged() ? 1.0 : 0.0};
	if (settings != m_idleSettings) {
		m_idleSettings = settings;
		m_idle = {};
	}
	// Lockstep: while the two sides have had the same input since the channels were prepared,
	// their states are the same, so the left channel does the work for both. The first block
	// whose sides differ gives the right channel a copy of the left's state.
	int live = channels;
	if (channels == 2 && m_lockstep) {
		if (std::memcmp(buffer.getReadPointer(0), buffer.getReadPointer(1), sizeof(float) * size_t(n)) == 0)
			live = 1;
		else {
			m_ch[1].copyStateFrom(m_ch[0]);
			m_lockstep = false;
		}
		m_idle[1] = m_idle[0];
	}

	bool run[2] = {}, silentIn[2] = {};
	for (int ch = 0; ch < live; ++ch) {
		// Silent input means under -200 dBFS: at full DISTORTION the pedal has about 70 dB of gain,
		// so anything louder can still be heard.
		silentIn[ch] = buffer.getMagnitude(ch, 0, n) <= 1e-10f;
		auto &idle = m_idle[size_t(ch)];
		if (idle.asleep && silentIn[ch])
			buffer.clear(ch, 0, n);
		else
			run[ch] = true;
		idle.asleep = false;
	}
	if (run[0] && run[1]) {
		// Both channels at once: the same results, with their solves interleaved.
		float *l = buffer.getWritePointer(0), *r = buffer.getWritePointer(1);
		for (int i = 0; i < n; ++i) {
			double yl, yr;
			rat::Channel::processPair(m_ch[0], m_ch[1], l[i], r[i], yl, yr);
			l[i] = float(yl);
			r[i] = float(yr);
		}
	} else {
		for (int ch = 0; ch < live; ++ch)
			if (run[ch]) {
				float *d = buffer.getWritePointer(ch);
				auto &c = m_ch[size_t(ch)];
				for (int i = 0; i < n; ++i)
					d[i] = float(c.process(d[i]));
			}
	}
	for (int ch = 0; ch < live; ++ch) {
		// Asleep after 100 ms of silent input with the output under -120 dB.
		auto &idle = m_idle[size_t(ch)];
		if (!run[ch])
			idle.asleep = true;
		else if (!silentIn[ch] || buffer.getMagnitude(ch, 0, n) > 1e-6f)
			idle.quietSamples = 0;
		else if ((idle.quietSamples += n) >= int(0.1 * m_rate))
			idle.asleep = m_idleSleep;
	}
	if (live < channels) {
		buffer.copyFrom(1, 0, buffer, 0, 0, n);
		m_idle[1] = m_idle[0];
	}
}

juce::AudioProcessorEditor *RatProcessor::createEditor()
{
	return new RatEditor(*this);
}

void RatProcessor::getStateInformation(juce::MemoryBlock &destData)
{
	auto state = m_params.copyState();
	state.setProperty("model", kModelVersion, nullptr);
	if (auto xml = state.createXml())
		copyXmlToBinary(*xml, destData);
}

void RatProcessor::setStateInformation(const void *data, int sizeInBytes)
{
	auto xml = getXmlFromBinary(data, sizeInBytes);
	if (xml == nullptr || !xml->hasTagName(m_params.state.getType()))
		return;
	auto state = juce::ValueTree::fromXml(*xml);
	// Sessions from before the model version were made with this model.
	m_modelVersion = juce::jlimit(1, kModelVersion, int(state.getProperty("model", 1)));
	state.removeProperty("model", nullptr);
	m_params.replaceState(state);
}
