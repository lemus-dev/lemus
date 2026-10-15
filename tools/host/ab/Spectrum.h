// The A/B player's spectrum analyser: the reference (the real recording) always, filled, and the
// source you hear as a line over it, each after its level match. Frequency is log, 20 Hz to
// 20 kHz; level is dBFS (a full-scale sine reads 0 dB). Peaks show at once and fall at 40 dB/s;
// stopped, it holds, or shows single frames (frame()). Hover for a frequency and both levels there.
#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lemus::host {

// One source's samples on their way from the audio thread to the window: one writer, one reader.
class Tap {
public:
	// The mean of the first `channels` channels of b's first n samples, times gain. What doesn't
	// fit is dropped (the window hasn't kept up).
	void push(const juce::AudioBuffer<float> &b, int channels, int n, float gain)
	{
		int s1, n1, s2, n2;
		m_fifo.prepareToWrite(n, s1, n1, s2, n2);
		const float scale = gain / float(std::max(1, channels));
		auto put = [&](int to, int count, int from) {
			for (int i = 0; i < count; ++i) {
				float x = 0;
				for (int c = 0; c < channels; ++c)
					x += b.getSample(c, from + i);
				m_data[size_t(to + i)] = x * scale;
			}
		};
		put(s1, n1, 0);
		put(s2, n2, n1);
		m_fifo.finishedWrite(n1 + n2);
	}

	// Moves the waiting samples onto the end of `history`, its oldest falling off the front.
	// Returns how many samples have passed since the last pull.
	int pull(std::vector<float> &history)
	{
		const int ready = m_fifo.getNumReady();
		const int n = std::min(ready, int(history.size()));
		if (ready > n)
			m_fifo.finishedRead(ready - n); // older than history holds
		int s1, n1, s2, n2;
		m_fifo.prepareToRead(n, s1, n1, s2, n2);
		std::move(history.begin() + n, history.end(), history.begin());
		float *to = history.data() + history.size() - size_t(n);
		std::copy_n(m_data.data() + s1, n1, to);
		std::copy_n(m_data.data() + s2, n2, to + n1);
		m_fifo.finishedRead(n1 + n2);
		return ready;
	}

private:
	static constexpr int kSize = 1 << 16;
	juce::AbstractFifo m_fifo{kSize};
	std::vector<float> m_data = std::vector<float>(size_t(kSize));
};

class Spectrum final : public juce::Component {
public:
	struct Look {
		juce::Colour plot, grid, label, reference, heard;
	};

	explicit Spectrum(Look look) : m_look(look)
	{
		setWantsKeyboardFocus(false);
		setMouseClickGrabsKeyboardFocus(false);
	}

	// For `sources` sources at `rate`, all quiet. Only while the audio thread isn't pushing.
	void prepare(int sources, double rate)
	{
		m_rate = rate;
		// ~6 Hz bins, so the harmonics of a low bass note stay apart.
		m_fft = std::make_unique<juce::dsp::FFT>(rate > 60000 ? 14 : 13);
		const int n = m_fft->getSize();
		m_window.resize(size_t(n));
		for (int k = 0; k < n; ++k)
			m_window[size_t(k)] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * float(k) / float(n));
		m_work.assign(size_t(2 * n), 0.0f);
		m_taps.clear();
		m_sources.clear();
		m_caption.clear();
		for (int i = 0; i < sources; ++i) {
			m_taps.push_back(std::make_unique<Tap>());
			m_sources.push_back({std::vector<float>(size_t(n)), std::vector<float>(size_t(n / 2 + 1), kSilent)});
		}
		repaint();
	}

	Tap &tap(size_t i) { return *m_taps[i]; }

	// Which sources to draw, by index (-1: none), and their names for the key.
	void show(int reference, const juce::String &referenceName, int heard, const juce::String &heardName)
	{
		if (reference == m_reference && heard == m_heard && referenceName == m_referenceName && heardName == m_heardName)
			return;
		m_reference = reference;
		m_heard = heard;
		m_referenceName = referenceName;
		m_heardName = heardName;
		repaint();
	}

	// Takes the new samples and works out each source's spectrum.
	void update()
	{
		bool changed = false;
		const int n = m_fft != nullptr ? m_fft->getSize() : 0;
		for (size_t i = 0; i < m_sources.size(); ++i) {
			auto &s = m_sources[i];
			const int got = m_taps[i]->pull(s.history);
			if (got == 0)
				continue;
			changed = true;
			for (int k = 0; k < n; ++k)
				m_work[size_t(k)] = s.history[size_t(k)] * m_window[size_t(k)];
			transform(s.db, kFall * float(got / m_rate));
		}
		if (changed)
			repaint();
	}

	// One frame, held until the next: source i's spectrum of the FFT's length of `samples` centred
	// on `centre` (zeros past their ends), times gain.
	void frame(size_t i, const std::vector<float> &samples, int64_t centre, float gain)
	{
		if (m_fft == nullptr || i >= m_sources.size())
			return;
		const int n = m_fft->getSize();
		for (int k = 0; k < n; ++k) {
			const int64_t at = centre - n / 2 + k;
			const float x = at >= 0 && at < int64_t(samples.size()) ? samples[size_t(at)] * gain : 0.0f;
			m_work[size_t(k)] = x * m_window[size_t(k)];
		}
		transform(m_sources[i].db, std::numeric_limits<float>::infinity());
		repaint();
	}

	// A line after the key (where a frame is from, say); empty for none.
	void setCaption(const juce::String &caption)
	{
		if (caption != m_caption) {
			m_caption = caption;
			repaint();
		}
	}

	void paint(juce::Graphics &g) override
	{
		const auto plot = plotArea();
		g.setColour(m_look.plot);
		g.fillRect(plot);
		g.setFont(juce::FontOptions(11.0f));
		// Level lines every 20 dB; frequency lines at 1, 2 and 5 of each decade.
		for (float db = 0; db >= kFloor; db -= 20) {
			const float y = yOf(db, plot);
			g.setColour(m_look.grid);
			g.drawHorizontalLine(int(y), plot.getX(), plot.getRight());
			g.setColour(m_look.label);
			g.drawText(juce::String(int(db)), 0, int(y) - 7, int(plot.getX()) - 6, 14, juce::Justification::centredRight);
		}
		for (float decade = 10; decade < 100000; decade *= 10)
			for (float m : {1.0f, 2.0f, 5.0f}) {
				const float f = decade * m;
				if (f < kLow || f > high())
					continue;
				const float x = xOf(f, plot);
				g.setColour(m_look.grid);
				g.drawVerticalLine(int(x), plot.getY(), plot.getBottom());
				g.setColour(m_look.label);
				g.drawText(f >= 1000 ? juce::String(int(f / 1000)) + "k" : juce::String(int(f)), int(x) - 20,
				           int(plot.getBottom()) + 3, 40, 14, juce::Justification::centred);
			}
		if (m_fft == nullptr)
			return;
		const auto curve = [&](const std::vector<float> &db) {
			juce::Path p;
			for (float x = plot.getX(); x <= plot.getRight(); x += 1) {
				const float y = yOf(levelBetween(db, freqAt(x - 0.5f, plot), freqAt(x + 0.5f, plot)), plot);
				if (x == plot.getX())
					p.startNewSubPath(x, y);
				else
					p.lineTo(x, y);
			}
			return p;
		};
		const bool reference = m_reference >= 0 && m_reference < int(m_sources.size());
		const bool heard = m_heard >= 0 && m_heard < int(m_sources.size()) && m_heard != m_reference;
		g.saveState();
		g.reduceClipRegion(plot.toNearestInt());
		if (reference) {
			auto p = curve(m_sources[size_t(m_reference)].db);
			auto fill = p;
			fill.lineTo(plot.getRight(), plot.getBottom());
			fill.lineTo(plot.getX(), plot.getBottom());
			fill.closeSubPath();
			g.setColour(m_look.reference.withAlpha(0.18f));
			g.fillPath(fill);
			g.setColour(m_look.reference);
			g.strokePath(p, juce::PathStrokeType(1.5f));
		}
		if (heard) {
			g.setColour(m_look.heard);
			g.strokePath(curve(m_sources[size_t(m_heard)].db), juce::PathStrokeType(1.5f));
		}
		g.restoreState();
		// The key, top left: each curve's colour and name.
		auto key = plot.reduced(10, 6).withHeight(16);
		const auto keyEntry = [&](juce::Colour c, const juce::String &name) {
			g.setColour(c);
			g.fillRect(key.removeFromLeft(14).withSizeKeepingCentre(14, 3));
			key.removeFromLeft(6);
			g.setColour(m_look.label);
			const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), name) + 2;
			g.drawText(name, key.removeFromLeft(float(w)), juce::Justification::centredLeft);
			key.removeFromLeft(16);
		};
		if (reference)
			keyEntry(m_look.reference, m_referenceName);
		if (heard)
			keyEntry(m_look.heard, m_heardName);
		g.setColour(m_look.label);
		g.drawText(m_caption, key, juce::Justification::centredLeft);
		if (m_mouseX >= plot.getX() && m_mouseX <= plot.getRight()) {
			g.setColour(m_look.label.withAlpha(0.5f));
			g.drawVerticalLine(int(m_mouseX), plot.getY(), plot.getBottom());
			const float f = freqAt(m_mouseX, plot), f0 = freqAt(m_mouseX - 0.5f, plot), f1 = freqAt(m_mouseX + 0.5f, plot);
			juce::String text = f < 1000 ? juce::String(juce::roundToInt(f)) + " Hz" : juce::String(f / 1000, 2) + " kHz";
			const auto level = [&](int i) {
				const float db = levelBetween(m_sources[size_t(i)].db, f0, f1);
				return db > kFloor ? juce::String(db, 1) + " dB" : juce::String("-");
			};
			if (reference)
				text << "   " << m_referenceName << " " << level(m_reference);
			if (heard)
				text << "   " << m_heardName << " " << level(m_heard);
			g.setColour(m_look.label);
			g.drawText(text, plot.reduced(10, 6), juce::Justification::topRight);
		}
	}

	void mouseMove(const juce::MouseEvent &e) override
	{
		m_mouseX = e.position.x;
		repaint();
	}
	void mouseExit(const juce::MouseEvent &) override
	{
		m_mouseX = -1;
		repaint();
	}

private:
	static constexpr float kLow = 20, kFloor = -100, kSilent = -200, kFall = 40; // Hz, dB, dB, dB/s

	struct Source {
		std::vector<float> history, db; // the last FFT's worth of samples; dB per bin
	};

	juce::Rectangle<float> plotArea() const
	{
		return getLocalBounds().toFloat().withTrimmedLeft(34).withTrimmedRight(14).withTrimmedTop(8).withTrimmedBottom(18);
	}
	// The windowed samples in m_work to dB per bin in db: rising at once, falling at most `fall` dB.
	void transform(std::vector<float> &db, float fall)
	{
		const int n = m_fft->getSize();
		std::fill(m_work.begin() + n, m_work.end(), 0.0f);
		m_fft->performFrequencyOnlyForwardTransform(m_work.data(), true);
		for (size_t b = 0; b < db.size(); ++b) {
			// A Hann window's peak for a sine of amplitude a is a * n / 4.
			const float now = juce::Decibels::gainToDecibels(m_work[b] * 4.0f / float(n), kSilent);
			db[b] = std::max(now, db[b] - fall);
		}
	}

	float high() const { return std::min(20000.0f, float(m_rate / 2)); }
	float xOf(float f, juce::Rectangle<float> plot) const
	{
		return plot.getX() + plot.getWidth() * std::log(f / kLow) / std::log(high() / kLow);
	}
	float freqAt(float x, juce::Rectangle<float> plot) const
	{
		return kLow * std::pow(high() / kLow, (x - plot.getX()) / plot.getWidth());
	}
	float yOf(float db, juce::Rectangle<float> plot) const
	{
		return juce::jlimit(plot.getY(), plot.getBottom(), plot.getY() + plot.getHeight() * db / kFloor);
	}

	// The loudest bin between f0 and f1, or between two bins (low down, where a pixel is narrower
	// than a bin) the level interpolated.
	float levelBetween(const std::vector<float> &db, float f0, float f1) const
	{
		const float bin = float(m_rate) / float(m_fft->getSize());
		const float b0 = f0 / bin, b1 = f1 / bin;
		const int last = int(db.size()) - 1;
		if (int(b1) > int(b0)) {
			float most = kSilent;
			for (int b = int(b0) + 1; b <= std::min(int(b1), last); ++b)
				most = std::max(most, db[size_t(b)]);
			return most;
		}
		const float c = 0.5f * (b0 + b1);
		const int b = std::min(int(c), last - 1);
		const float t = c - float(b);
		return db[size_t(b)] + t * (db[size_t(b + 1)] - db[size_t(b)]);
	}

	Look m_look;
	double m_rate = 48000;
	std::unique_ptr<juce::dsp::FFT> m_fft;
	std::vector<float> m_window, m_work;
	std::vector<std::unique_ptr<Tap>> m_taps;
	std::vector<Source> m_sources;
	int m_reference = -1, m_heard = -1;
	juce::String m_referenceName, m_heardName, m_caption;
	float m_mouseX = -1;
};

} // namespace lemus::host
