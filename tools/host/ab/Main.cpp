// Lemus A/B: plays a scene (tools/host/README.md) through all its sources at once, in sync and
// looping, and lets you hear one at a time: Space switches, 1-9 pick, Return plays and stops.
// Sources are level-matched when the scene loads (one loop rendered offline), switching
// crossfades over 5 ms, and blind mode hides which is which until you reveal it. The other
// scenes in the same folder are a click (or Up/Down) away. After changing a source in its editor
// (its volume, say), Match levels (L) measures them again. The spectrum (Spectrum.h) shows what
// is playing against the reference; Left/Right stop and step it through the loop a frame at a time.
//
//   Lemus A/B.app/Contents/MacOS/Lemus A/B [SCENE.json]      or drop a scene on the window
#include "../Player.h"
#include "Spectrum.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <numeric>

using namespace lemus::host;

namespace {

namespace colours {
const juce::Colour background{0xff1b1d21}, panel{0xff232830}, edge{0xff2e343d}, text{0xffe9ecf1},
	dim{0xff9aa4b1}, accent{0xffff8c1a}, accentDim{0xff2a1e12};
}

// A plugin's editor in its own window; closing it hides it.
class EditorWindow final : public juce::DocumentWindow {
public:
	EditorWindow(const juce::String &name, juce::AudioProcessorEditor *editor)
		: DocumentWindow(name, colours::background, DocumentWindow::closeButton)
	{
		setUsingNativeTitleBar(true);
		setContentOwned(editor, true); // deleted with the window, before its plugin
		setVisible(true);
		toFront(true);
	}
	void closeButtonPressed() override { setVisible(false); }
};

class Main final : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   private juce::AudioIODeviceCallback,
                   private juce::Timer {
public:
	Main()
	{
		m_formats.addDefaultFormats();
		m_devices.initialiseWithDefaultDevices(0, 2);
		for (auto *b : {&m_open, &m_play, &m_match, &m_prev, &m_next, &m_blind, &m_reveal}) {
			addAndMakeVisible(*b);
			b->setWantsKeyboardFocus(false);
		}
		m_open.onClick = [this] { choose(); };
		m_play.onClick = [this] { togglePlay(); };
		m_blind.onClick = [this] { setBlind(!m_isBlind); };
		m_reveal.onClick = [this] { m_revealed = true; refresh(); };
		m_match.onClick = [this] { rematch(); };
		m_prev.onClick = [this] { step(-1); };
		m_next.onClick = [this] { step(1); };
		addAndMakeVisible(m_picker);
		m_picker.setWantsKeyboardFocus(false);
		m_picker.setTextWhenNothingSelected("Scenes in this folder");
		m_picker.onChange = [this] {
			const int i = m_picker.getSelectedItemIndex();
			if (i >= 0 && i < m_siblings.size() && m_siblings[i] != m_file)
				load(m_siblings[i]);
		};
		addAndMakeVisible(m_spectrum);
		addAndMakeVisible(m_status);
		m_status.setColour(juce::Label::textColourId, colours::dim);
		m_status.setJustificationType(juce::Justification::topLeft);
		setWantsKeyboardFocus(true);
		setSize(860, 660);
		startTimerHz(30);
		refresh();
	}

	~Main() override
	{
		m_devices.removeAudioCallback(this);
		m_editors.clear();
		m_players.clear();
	}

	void load(const juce::File &file)
	{
		const bool wasPlaying = m_playing;
		m_devices.removeAudioCallback(this);
		m_playing = false;
		m_editors.clear();
		m_players.clear();
		m_sourceButtons.clear();
		m_editButtons.clear();
		juce::String error;
		const auto templates = Templates::load(Templates::standardFiles(), error);
		if (error.isEmpty())
			m_scene = Scene::load(file, templates, error);
		m_file = file;
		listSiblings();
		juce::StringArray notes;
		if (error.isNotEmpty()) {
			m_spectrum.prepare(0, m_scene.rate);
			m_status.setText(error, juce::dontSendNotification);
			refresh();
			return;
		}
		// Run at the device's rate and block size.
		if (auto *dev = m_devices.getCurrentAudioDevice()) {
			m_scene.rate = dev->getCurrentSampleRate();
			m_scene.block = std::max(32, dev->getCurrentBufferSizeSamples());
		}
		m_loop = std::max<int64_t>(1, int64_t(std::llround(m_scene.beats * m_scene.samplesPerBeat())));
		for (const auto &src : m_scene.sources) {
			auto p = std::make_unique<Player>(m_formats, m_scene, src, false);
			for (const auto &w : p->warnings())
				notes.add(src.label + ": " + w);
			if (p->error().isNotEmpty())
				notes.add(src.label + ": " + p->error());
			m_players.push_back(std::move(p));
		}
		matchLevels();
		for (size_t i = 0; i < m_players.size(); ++i) {
			auto b = std::make_unique<juce::TextButton>();
			b->setClickingTogglesState(false);
			b->setWantsKeyboardFocus(false);
			b->onClick = [this, i] { select(int(i)); };
			addAndMakeVisible(*b);
			m_sourceButtons.push_back(std::move(b));
			auto e = std::make_unique<juce::TextButton>("edit");
			e->setWantsKeyboardFocus(false);
			e->setEnabled(m_players[i]->plugin() != nullptr && m_players[i]->plugin()->hasEditor());
			e->onClick = [this, i] { showEditor(int(i)); };
			addAndMakeVisible(*e);
			m_editButtons.push_back(std::move(e));
		}
		// The reference, drawn in the spectrum whichever source plays: the first recording, else the
		// first source.
		m_reference = 0;
		for (size_t i = 0; i < m_scene.sources.size(); ++i)
			if (m_scene.sources[i].audio != juce::File()) {
				m_reference = int(i);
				break;
			}
		m_gains.assign(m_players.size(), 0.0f);
		m_meters = std::vector<std::atomic<float>>(m_players.size());
		m_selected = 0;
		m_position = 0;
		allocate();
		m_spectrum.prepare(int(m_players.size()), m_scene.rate);
		setBlind(m_isBlind);
		for (auto &p : m_players) {
			p->setRealtime(true);
			p->reset();
		}
		if (wasPlaying) {
			m_restart = true;
			m_playing = true;
		}
		m_devices.addAudioCallback(this);
		// A scene's "about" says what it tests; it never names the sources.
		if (const auto about = juce::JSON::parse(file).getProperty("about", {}).toString(); about.isNotEmpty())
			notes.insert(0, about);
		notes.insert(0, "Space: next   1-" + juce::String(int(m_players.size())) +
		                    ": pick   Return: play/stop   B: blind   L: match levels   Up/Down: scene\n"
		                    "Left/Right: stop, step the spectrum a frame (with Shift, a 16th)\n");
		m_status.setText(notes.joinIntoString("\n"), juce::dontSendNotification);
		refresh();
		resized();
	}

	// Plays two loops into `out` with the audio device detached (so nothing is heard): source 0
	// for the first loop, switching to source 1 halfway through and back at the second loop.
	// Prints the level of each stretch and how many clicks (sample jumps) the switches made.
	void selfTest(const juce::File &out)
	{
		m_devices.removeAudioCallback(this);
		const int n = std::max(32, m_scene.block);
		const int64_t total = 2 * m_loop;
		juce::AudioBuffer<float> rec(2, int(total));
		m_playing = true;
		m_restart = true;
		for (int64_t pos = 0; pos < total; pos += n) {
			if (pos >= m_loop / 2 && pos < m_loop / 2 + n && m_players.size() > 1)
				m_selected = 1;
			if (pos >= m_loop && pos < m_loop + n)
				m_selected = 0;
			const int len = int(std::min<int64_t>(n, total - pos));
			float *ch[2] = {rec.getWritePointer(0, int(pos)), rec.getWritePointer(1, int(pos))};
			audioDeviceIOCallbackWithContext(nullptr, 0, ch, 2, len, {});
			showSpectrum();
			m_spectrum.update();
		}
		m_playing = false;
		auto level = [&](int64_t a, int64_t b) { return juce::Decibels::gainToDecibels(rec.getRMSLevel(0, int(a), int(b - a)), -150.0f); };
		int clicks = 0;
		for (int i = 1; i < rec.getNumSamples(); ++i)
			clicks += std::abs(rec.getSample(0, i) - rec.getSample(0, i - 1)) > 0.5f;
		std::printf("selftest: %s, loop %.2f s; A %.1f dB, B %.1f dB, A again %.1f dB; %d sample jumps > 0.5\n",
		            m_scene.name.toRawUTF8(), double(m_loop) / m_scene.rate, double(level(0, m_loop / 2)),
		            double(level(m_loop / 2, m_loop)), double(level(m_loop, total)), clicks);
		out.deleteFile();
		juce::WavAudioFormat wav;
		std::unique_ptr<juce::OutputStream> stream(out.createOutputStream());
		if (std::unique_ptr<juce::AudioFormatWriter> w{wav.createWriterFor(stream.get(), m_scene.rate, 2, 32, {}, 0)}) {
			stream.release();
			w->writeFromAudioSampleBuffer(rec, 0, rec.getNumSamples());
		}
	}

	bool keyPressed(const juce::KeyPress &k) override
	{
		if (k == juce::KeyPress::spaceKey && !m_players.empty())
			select((m_selected + 1) % int(m_players.size()));
		else if (k == juce::KeyPress::returnKey)
			togglePlay();
		else if (k.getTextCharacter() >= '1' && k.getTextCharacter() <= '9') {
			const int i = int(k.getTextCharacter() - '1');
			if (i < int(m_players.size()))
				select(m_isBlind ? m_order[size_t(i)] : i);
		} else if (k.getTextCharacter() == 'b' || k.getTextCharacter() == 'B')
			setBlind(!m_isBlind);
		else if (k.getTextCharacter() == 'l' || k.getTextCharacter() == 'L')
			rematch();
		else if (k == juce::KeyPress::upKey)
			step(-1);
		else if (k == juce::KeyPress::downKey)
			step(1);
		else if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey) // with Shift too
			stepFrame(k.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1, k.getModifiers().isShiftDown());
		else
			return false;
		return true;
	}

	bool isInterestedInFileDrag(const juce::StringArray &files) override { return files.size() == 1 && files[0].endsWith(".json"); }
	void filesDropped(const juce::StringArray &files, int, int) override { load(juce::File(files[0])); }

	void paint(juce::Graphics &g) override
	{
		g.fillAll(colours::background);
		g.setColour(colours::text);
		g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
		g.drawText(m_scene.name.isEmpty() ? juce::String("Open or drop a scene") : m_scene.name, 16, 12, getWidth() - 32, 28,
		           juce::Justification::centredLeft);
		if (m_players.empty())
			return;
		// Position through the loop.
		const auto bar = juce::Rectangle<float>(16, 112, float(getWidth() - 32), 6);
		g.setColour(colours::panel);
		g.fillRoundedRectangle(bar, 3);
		g.setColour(colours::accent);
		g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * float(m_position.load()) / float(m_loop)), 3);
		// Level per source (before the switch), under its button.
		for (size_t k = 0; k < m_sourceButtons.size(); ++k) {
			const size_t i = m_isBlind ? size_t(m_order[k]) : k;
			const auto r = m_sourceButtons[k]->getBounds().toFloat().translated(0, 52).withHeight(4);
			const float db = juce::Decibels::gainToDecibels(m_meters[i].load(), -60.0f);
			g.setColour(colours::panel);
			g.fillRoundedRectangle(r, 2);
			g.setColour(colours::dim);
			g.fillRoundedRectangle(r.withWidth(r.getWidth() * (db + 60) / 60), 2);
		}
	}

	void resized() override
	{
		auto r = getLocalBounds().reduced(16);
		r.removeFromTop(36);
		auto row = r.removeFromTop(44);
		m_open.setBounds(row.removeFromLeft(90).reduced(0, 4));
		row.removeFromLeft(8);
		m_play.setBounds(row.removeFromLeft(90).reduced(0, 4));
		row.removeFromLeft(8);
		m_match.setBounds(row.removeFromLeft(110).reduced(0, 4));
		m_reveal.setBounds(row.removeFromRight(90).reduced(0, 4));
		row.removeFromRight(8);
		m_blind.setBounds(row.removeFromRight(90).reduced(0, 4));
		row.reduce(16, 0);
		m_prev.setBounds(row.removeFromLeft(36).reduced(0, 4));
		m_next.setBounds(row.removeFromRight(36).reduced(0, 4));
		m_picker.setBounds(row.reduced(4, 4));
		r.removeFromTop(28);
		auto sources = r.removeFromTop(56);
		const int n = std::max(1, int(m_sourceButtons.size()));
		const int w = (sources.getWidth() - (n - 1) * 12) / n;
		for (size_t i = 0; i < m_sourceButtons.size(); ++i) {
			auto cell = sources.removeFromLeft(w);
			sources.removeFromLeft(12);
			m_editButtons[i]->setBounds(cell.removeFromRight(56).reduced(0, 10));
			cell.removeFromRight(6);
			m_sourceButtons[i]->setBounds(cell);
		}
		r.removeFromTop(16);
		m_status.setBounds(r.removeFromBottom(120));
		r.removeFromBottom(12);
		m_spectrum.setBounds(r);
	}

private:
	void choose()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Open a scene", m_file.exists() ? m_file.getParentDirectory()
		                                                                                : juce::File(LEMUS_MACHINES_DIR), "*.json");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
		                       [this](const juce::FileChooser &fc) {
			                       if (fc.getResult().existsAsFile())
				                       load(fc.getResult());
		                       });
	}

	// The scenes next to the current one, by name, for the picker and Up/Down.
	void listSiblings()
	{
		m_siblings = m_file.getParentDirectory().findChildFiles(juce::File::findFiles, false, "*.json");
		std::sort(m_siblings.begin(), m_siblings.end(),
		          [](const juce::File &a, const juce::File &b) { return a.getFileName().compareNatural(b.getFileName()) < 0; });
		m_picker.clear(juce::dontSendNotification);
		for (int i = 0; i < m_siblings.size(); ++i)
			m_picker.addItem(m_siblings[i].getFileNameWithoutExtension(), i + 1);
		m_picker.setSelectedItemIndex(m_siblings.indexOf(m_file), juce::dontSendNotification);
	}

	void step(int by)
	{
		const int i = m_siblings.indexOf(m_file);
		if (i >= 0 && i + by >= 0 && i + by < m_siblings.size())
			load(m_siblings[i + by]);
	}

	// Measures the levels again with the sources as they are now (e.g. after a change in an editor),
	// then plays on from the start of the loop. Blind mode keeps its order.
	void rematch()
	{
		if (m_players.empty())
			return;
		const bool wasPlaying = m_playing;
		m_devices.removeAudioCallback(this);
		m_playing = false;
		for (auto &p : m_players)
			p->setRealtime(false);
		matchLevels();
		for (auto &p : m_players) {
			p->setRealtime(true);
			p->reset();
		}
		m_position = 0;
		if (wasPlaying) {
			m_restart = true;
			m_playing = true;
		}
		m_devices.addAudioCallback(this);
		refresh();
	}

	// The outputs that are heard: the first two (the main pair), not a plugin's individual outputs
	// (Roland's TR-808 has 24, mostly silent, which would read 11 dB low).
	static int heard(int outputs) { return std::clamp(outputs, 1, 2); }

	// One loop of each source rendered offline; each gets the gain that brings the RMS of what is
	// heard to the sources' mean (on top of the scene's own gainDb). The renders are kept, in
	// mono, for stepping the spectrum.
	void matchLevels()
	{
		m_autoGain.assign(m_players.size(), 1.0f);
		m_renders.assign(m_players.size(), std::vector<float>(size_t(m_loop)));
		std::vector<double> db;
		for (size_t k = 0; k < m_players.size(); ++k) {
			auto &p = m_players[k];
			juce::AudioBuffer<float> buf(std::max(1, p->numOutputs()), m_scene.block);
			const int channels = heard(buf.getNumChannels());
			double sum = 0;
			int64_t count = 0;
			for (int64_t pos = 0; pos < m_loop; pos += m_scene.block) {
				juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), buf.getNumChannels(), 0,
				                              int(std::min<int64_t>(m_scene.block, m_loop - pos)));
				p->process(view, pos);
				for (int c = 0; c < channels; ++c)
					for (int i = 0; i < view.getNumSamples(); ++i) {
						sum += double(view.getSample(c, i)) * view.getSample(c, i), ++count;
						m_renders[k][size_t(pos + i)] += view.getSample(c, i) / float(channels);
					}
			}
			p->reset();
			db.push_back(10 * std::log10(sum / double(std::max<int64_t>(1, count)) + 1e-20));
		}
		double mean = 0;
		int live = 0;
		for (double d : db)
			if (d > -150)
				mean += d, ++live;
		mean /= std::max(1, live);
		m_levelText.clear();
		for (size_t i = 0; i < db.size(); ++i) {
			m_autoGain[i] = db[i] > -150 ? juce::Decibels::decibelsToGain(float(mean - db[i])) : 1.0f;
			m_levelText.push_back(juce::String(mean - db[i] >= 0 ? "+" : "") + juce::String(mean - db[i], 1) + " dB");
		}
	}

	// Stops, then moves the spectrum a frame (1/30 s, as it updates while playing) or a 16th
	// through the loop and shows each source's spectrum there, from the renders matchLevels kept.
	void stepFrame(int by, bool sixteenth)
	{
		if (m_players.empty() || m_renders.size() != m_players.size())
			return;
		m_playing = false;
		const int64_t hop = sixteenth ? std::llround(m_scene.samplesPerBeat() / 4) : std::llround(m_scene.rate / 30);
		const int64_t pos = ((m_position.load() + by * hop) % m_loop + m_loop) % m_loop;
		m_position = pos;
		for (size_t i = 0; i < m_players.size(); ++i)
			m_spectrum.frame(i, m_renders[i], pos, m_autoGain[i]);
		const double sixteenths = double(pos) / (m_scene.samplesPerBeat() / 4);
		m_spectrum.setCaption("frame at " + juce::String(double(pos) / m_scene.rate, 3) + " s, 16th " +
		                      juce::String(int(sixteenths) + 1) + " of " + juce::String(int(std::ceil(m_scene.beats * 4))));
		refresh();
	}

	void select(int i)
	{
		m_selected = i;
		refresh();
	}

	void togglePlay()
	{
		if (m_players.empty())
			return;
		if (m_playing)
			m_playing = false;
		else {
			m_restart = true;
			m_playing = true;
			m_spectrum.setCaption({});
		}
		refresh();
	}

	void setBlind(bool on)
	{
		m_isBlind = on;
		m_revealed = false;
		m_order.resize(m_players.size());
		std::iota(m_order.begin(), m_order.end(), 0);
		if (on) {
			juce::Random rng;
			for (size_t i = m_order.size(); i > 1; --i)
				std::swap(m_order[i - 1], m_order[size_t(rng.nextInt(int(i)))]);
		}
		refresh();
	}

	void showEditor(int i)
	{
		auto *plugin = m_players[size_t(i)]->plugin();
		if (plugin == nullptr)
			return;
		if (auto *open = plugin->getActiveEditor()) {
			if (auto *w = open->findParentComponentOfClass<EditorWindow>()) {
				w->setVisible(true);
				w->toFront(true);
			}
			return;
		}
		if (auto *editor = plugin->createEditorIfNeeded())
			m_editors.push_back(std::make_unique<EditorWindow>(m_scene.sources[size_t(i)].label, editor));
	}

	void refresh()
	{
		m_play.setButtonText(m_playing ? "Stop" : "Play");
		const int at = m_siblings.indexOf(m_file);
		m_prev.setEnabled(at > 0);
		m_next.setEnabled(at >= 0 && at + 1 < m_siblings.size());
		m_blind.setButtonText(m_isBlind ? "Blind: on" : "Blind: off");
		m_reveal.setVisible(m_isBlind && !m_revealed);
		for (size_t k = 0; k < m_sourceButtons.size(); ++k) {
			// In blind mode button k plays source m_order[k] and is named by its letter only.
			const int src = m_isBlind ? m_order[k] : int(k);
			const juce::String letter = juce::String::charToString(juce::juce_wchar('A' + int(k)));
			juce::String text = letter;
			if (!m_isBlind || m_revealed)
				text << ": " << m_scene.sources[size_t(src)].label << "  (" << m_levelText[size_t(src)] << ")";
			auto &b = *m_sourceButtons[k];
			b.setButtonText(text);
			b.onClick = [this, src] { select(src); };
			const bool on = src == m_selected;
			b.setColour(juce::TextButton::buttonColourId, on ? colours::accentDim : colours::panel);
			b.setColour(juce::TextButton::textColourOffId, on ? colours::accent : colours::text);
			m_editButtons[k]->onClick = [this, src] { showEditor(src); };
			m_editButtons[k]->setVisible(!m_isBlind || m_revealed);
		}
		showSpectrum();
		repaint();
	}

	// The reference and the source playing; in blind mode, until Reveal, only the one playing,
	// unnamed.
	void showSpectrum()
	{
		const int heard = m_selected;
		if (m_players.empty())
			m_spectrum.show(-1, {}, -1, {});
		else if (m_isBlind && !m_revealed)
			m_spectrum.show(-1, {}, heard, {});
		else
			m_spectrum.show(m_reference, m_scene.sources[size_t(m_reference)].label, heard, m_scene.sources[size_t(heard)].label);
	}

	void timerCallback() override
	{
		m_spectrum.update();
		if (m_playing)
			repaint(getLocalBounds().withBottom(m_spectrum.getY())); // the position and the meters
	}

	// ------------------------------------------------------------------ audio
	void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
	void audioDeviceStopped() override {}

	void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *out, int numOut, int n,
	                                      const juce::AudioIODeviceCallbackContext &) override
	{
		for (int c = 0; c < numOut; ++c)
			juce::FloatVectorOperations::clear(out[c], n);
		if (!m_playing || m_players.empty())
			return;
		if (m_restart.exchange(false)) {
			for (auto &p : m_players)
				p->reset();
			m_position = 0;
		}
		if (int(m_mix.size()) < int(m_players.size()))
			return;
		for (int done = 0; done < n;) {
			// Up to the loop's end; there every source starts over.
			int64_t pos = m_position.load();
			const int len = int(std::min<int64_t>(n - done, m_loop - pos));
			for (size_t i = 0; i < m_players.size(); ++i) {
				auto &buf = m_mix[i];
				juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), buf.getNumChannels(), 0, len);
				m_players[i]->process(view, pos);
				m_meters[i] = std::max(view.getRMSLevel(0, 0, len), m_meters[i].load() * 0.95f);
				m_spectrum.tap(i).push(view, std::min(heard(m_players[i]->numOutputs()), view.getNumChannels()), len, m_autoGain[i]);
				// Ramp each source's gain toward on (the selected one) or off: 5 ms.
				const float step = float(1.0 / (0.005 * m_scene.rate));
				const float target = int(i) == m_selected ? 1.0f : 0.0f;
				for (int s = 0; s < len; ++s) {
					m_gains[i] += juce::jlimit(-step, step, target - m_gains[i]);
					const float g = m_gains[i] * m_autoGain[i];
					if (g > 0)
						for (int c = 0; c < numOut; ++c)
							out[c][done + s] += g * view.getSample(std::min(c, view.getNumChannels() - 1), s);
				}
			}
			pos += len;
			done += len;
			if (pos >= m_loop) {
				pos = 0;
				for (auto &p : m_players)
					p->reset();
			}
			m_position = pos;
		}
	}

	juce::AudioDeviceManager m_devices;
	juce::AudioPluginFormatManager m_formats;
	Scene m_scene;
	juce::File m_file;
	std::vector<std::unique_ptr<Player>> m_players;
	std::vector<std::unique_ptr<juce::TextButton>> m_sourceButtons, m_editButtons;
	std::vector<std::unique_ptr<EditorWindow>> m_editors;
	std::vector<juce::AudioBuffer<float>> m_mix;
	std::vector<float> m_gains, m_autoGain;
	std::vector<std::vector<float>> m_renders; // a loop of each source, mono, before its gain
	std::vector<std::atomic<float>> m_meters;
	std::vector<juce::String> m_levelText;
	std::vector<int> m_order;
	std::atomic<int64_t> m_position{0};
	int64_t m_loop = 1;
	std::atomic<int> m_selected{0};
	int m_reference = 0;
	std::atomic<bool> m_playing{false}, m_restart{false};
	bool m_isBlind = false, m_revealed = false;
	juce::TextButton m_open{"Open"}, m_play{"Play"}, m_match{"Match levels"}, m_prev{"<"}, m_next{">"}, m_blind{"Blind: off"}, m_reveal{"Reveal"};
	juce::ComboBox m_picker;
	juce::Array<juce::File> m_siblings;
	juce::Label m_status;
	Spectrum m_spectrum{{colours::panel, colours::edge, colours::dim, colours::accent, colours::text.withAlpha(0.7f)}};
	std::unique_ptr<juce::FileChooser> m_chooser;

	// Buffers for the audio thread, sized once a scene is loaded (before the callback starts).
	void allocate()
	{
		m_mix.clear();
		for (auto &p : m_players)
			m_mix.emplace_back(std::max(2, p->numOutputs()), std::max(4096, m_scene.block * 4));
	}
};

class App final : public juce::JUCEApplication {
public:
	const juce::String getApplicationName() override { return "Lemus A/B"; }
	const juce::String getApplicationVersion() override { return "0.1"; }

	void initialise(const juce::String &commandLine) override
	{
		auto *main = new Main();
		m_window = std::make_unique<juce::DocumentWindow>("Lemus A/B", colours::background, juce::DocumentWindow::allButtons);
		m_window->setUsingNativeTitleBar(true);
		m_window->setContentOwned(main, true);
		m_window->setResizable(true, false);
		m_window->centreWithSize(main->getWidth(), main->getHeight());
		m_window->setVisible(true);
		main->grabKeyboardFocus();
		// [SCENE.json] [--selftest OUT.wav] [--snapshot OUT.png]: the snapshot writes the window
		// (scene loaded, not playing; after a selftest, its spectrum) and quits, for checks that
		// mustn't make a sound.
		const auto args = juce::StringArray::fromTokens(commandLine, true);
		const auto cwd = juce::File::getCurrentWorkingDirectory();
		for (const auto &a : args)
			if (a.unquoted().endsWith(".json"))
				main->load(cwd.getChildFile(a.unquoted()));
		const int st = args.indexOf("--selftest");
		if (st >= 0 && st + 1 < args.size()) {
			main->selfTest(cwd.getChildFile(args[st + 1].unquoted()));
			if (!args.contains("--snapshot")) {
				juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
				return;
			}
		}
		const int at = args.indexOf("--snapshot");
		if (at >= 0 && at + 1 < args.size()) {
			const auto out = cwd.getChildFile(args[at + 1].unquoted());
			juce::Timer::callAfterDelay(800, [main, out] {
				const auto image = main->createComponentSnapshot(main->getLocalBounds(), true, 2.0f);
				out.deleteFile();
				juce::FileOutputStream stream(out);
				juce::PNGImageFormat().writeImageToStream(image, stream);
				stream.flush();
				juce::JUCEApplicationBase::quit();
			});
		}
	}

	void shutdown() override { m_window.reset(); }
	void systemRequestedQuit() override { quit(); }

private:
	std::unique_ptr<juce::DocumentWindow> m_window;
};

} // namespace

START_JUCE_APPLICATION(App)
