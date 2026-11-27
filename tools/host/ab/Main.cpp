// Lemus A/B: plays a scene (tools/host/README.md) through all its sources at once, in sync and
// looping, and lets you hear one at a time: Space switches, 1-9 pick, Return plays and stops.
// Sources are level-matched when the scene loads (one loop rendered offline), switching
// crossfades over 5 ms, and blind mode hides which is which until you reveal it. The other
// scenes in the same folder are a click (or Up/Down) away, numbered from 1 in the folder's
// natural sort order (in the title and the picker: "scene 7, B" names a clip and a source). After changing a source in its editor
// (its volume, say), Match levels (L) measures them again. The spectrum (Spectrum.h) shows what
// is playing against the reference (hidden in blind mode until Reveal); Left/Right stop and step it
// through the loop a frame at a time.
// Dual mode (D) plays two at once, one in each ear, each in mono: the selected source left, another
// right; Space swaps them.
// One app, one window: a test per scene folder, listed down the left and named for the machines
// its scenes test. A scene opened while the app runs, from any session's build, goes to its
// folder's test, or a new one; only the test shown plays. The tests come back after a restart.
// Our own plugins load from a fresh copy whenever the installed one has changed, so a rebuilt
// plugin is heard at the next scene load or Reload, without a restart.
//
//   open -a "Lemus AB.app" /abs/SCENE.json            add the scene's folder as a test (or go to it)
//   open -a "Lemus AB.app" --args --reload [/abs/PATH] load the test again, with rebuilt plugins
//   open -a "Lemus AB.app" --args --remove /abs/PATH   take the test off the list
//   (or Lemus AB.app/Contents/MacOS/Lemus AB [ARGS], or drop a scene on the window; PATH is a
//   scene or its folder; without one, --reload is the test shown)
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

// A machine's plugin ("ko-mono-20", or a path to it) -> the machine's folder, from each
// machines/<id>/machine.cmake's lemus_plugin(<id> NAME "..."); none if it isn't one of ours.
juce::File machineOfPlugin(const juce::String &plugin)
{
	static const auto table = [] {
		std::map<juce::String, juce::File> t;
		for (const auto &dir : juce::File(LEMUS_MACHINES_DIR).findChildFiles(juce::File::findDirectories, false)) {
			const auto text = dir.getChildFile("machine.cmake").loadFileAsString();
			const int at = text.indexOf("lemus_plugin(");
			const auto name = at < 0 ? juce::String()
			                         : text.substring(at).fromFirstOccurrenceOf("NAME \"", false, false).upToFirstOccurrenceOf("\"", false, false);
			if (name.isNotEmpty())
				t[name] = dir;
		}
		return t;
	}();
	const auto key = juce::File::isAbsolutePath(plugin) ? juce::File(plugin).getFileNameWithoutExtension() : plugin;
	const auto it = table.find(key);
	return it != table.end() ? it->second : juce::File();
}

// The machines a scene tests: each source's template ("tb303.roland": machines/tb303), Lemus
// machine or own plugin ("ko-mono-20"), named by its README's first heading ("# E-mu Drumulator
// (em-drums-d)": E-mu Drumulator).
juce::StringArray machinesOf(const juce::File &file, const Templates &templates)
{
	juce::StringArray dirs, names;
	const auto json = juce::JSON::parse(file), list = json.getProperty("sources", {}); // kept: getArray points into them
	if (const auto *sources = list.getArray())
		for (const auto &s : *sources) {
			const auto use = s.getProperty("use", {}).toString();
			const auto it = templates.entries.find(use);
			if (use.isNotEmpty() && it != templates.entries.end())
				dirs.addIfNotAlreadyThere(it->second.second.getParentDirectory().getFullPathName()); // <id>/host
			else if (const auto m = s.getProperty("machine", {}).toString(); m.isNotEmpty())
				dirs.addIfNotAlreadyThere(juce::File(LEMUS_MACHINES_DIR).getChildFile(m).getFullPathName());
			else if (const auto dir = machineOfPlugin(s.getProperty("plugin", {}).toString()); dir != juce::File())
				dirs.addIfNotAlreadyThere(dir.getFullPathName());
		}
	for (const auto &d : dirs) {
		const juce::File dir(d);
		juce::String name = dir.getFileName();
		juce::StringArray lines;
		lines.addLines(dir.getChildFile("README.md").loadFileAsString());
		for (const auto &l : lines)
			if (l.startsWith("# ")) {
				name = l.substring(2).upToFirstOccurrenceOf(" (", false, false).trim();
				break;
			}
		names.addIfNotAlreadyThere(name);
	}
	return names;
}

// Where the app keeps its tests (tests.json), the commands handed to it (inbox/) and the plugins'
// copies (plugins/).
juce::File appDir()
{
	return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Application Support/Lemus AB");
}

// One of our plugins (a machine's own, or Lemus) loads from a copy of its bundle, made again
// whenever the installed one changes: macOS hands back a bundle it has already loaded from a path
// (CFBundle, dlopen), so a plugin rebuilt and installed over the old one would play the old binary
// until a restart, and replacing a binary that is loaded can crash the host. Other plugins (Roland's)
// load where they are.
juce::String freshCopy(const juce::String &plugin)
{
	if (plugin.isEmpty() || plugin.startsWith("au:"))
		return plugin;
	const auto installed = findPlugin(plugin);
	const auto name = installed.getFileNameWithoutExtension();
	if (!installed.isDirectory() || (machineOfPlugin(name) == juce::File() && name != "Lemus" && name != "Lemus FX"))
		return plugin;
	int64_t stamp = 0; // the binary's time and size
	for (const auto &f : installed.getChildFile("Contents/MacOS").findChildFiles(juce::File::findFiles, false))
		stamp = stamp * 31 + f.getLastModificationTime().toMilliseconds() * 7 + f.getSize();
	const auto copy = appDir().getChildFile("plugins").getChildFile(name).getChildFile(juce::String::toHexString(stamp)).getChildFile(installed.getFileName());
	if (!copy.isDirectory()) {
		const auto part = copy.getSiblingFile(installed.getFileName() + ".part");
		part.deleteRecursively();
		copy.getParentDirectory().createDirectory();
		juce::ChildProcess ditto; // keeps the bundle as it is (links, modes, the signature)
		if (!ditto.start(juce::StringArray{"/usr/bin/ditto", installed.getFullPathName(), part.getFullPathName()}) ||
		    !ditto.waitForProcessToFinish(60000) || ditto.getExitCode() != 0 || !part.moveFileTo(copy)) {
			part.deleteRecursively();
			return plugin;
		}
	}
	return copy.getFullPathName();
}

class Main final : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   private juce::AudioIODeviceCallback,
                   private juce::Timer {
public:
	Main(juce::AudioDeviceManager &devices, juce::AudioPluginFormatManager &formats) : m_devices(devices), m_formats(formats)
	{
		for (auto *b : {&m_open, &m_play, &m_match, &m_prev, &m_next, &m_dualButton, &m_blind, &m_reveal}) {
			addAndMakeVisible(*b);
			b->setWantsKeyboardFocus(false);
		}
		m_open.onClick = [this] { choose(); };
		m_play.onClick = [this] { togglePlay(); };
		m_dualButton.onClick = [this] { setDual(!m_dual); };
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

	std::function<void()> onPlay;                     // this test started playing (the others stop)
	std::function<void(const juce::File &)> onOpen;   // Open or a drop: the app finds the scene's test
	std::function<void()> onLoaded;                   // a scene loaded (the list and the saved tests)
	void stop()
	{
		if (m_playing) {
			m_playing = false;
			refresh();
		}
	}
	bool hasScene() const { return m_file != juce::File(); }
	bool isPlaying() const { return m_playing; }
	juce::File file() const { return m_file; }
	const juce::StringArray &machines() const { return m_machines; }
	int scenes() const { return m_siblings.size(); }
	// The scene again: the folder's list, the scene, its plugins (a rebuilt one from a new copy).
	void reload()
	{
		if (hasScene())
			load(m_file);
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
		for (auto &src : m_scene.sources)
			src.plugin = freshCopy(src.plugin);
		m_file = file;
		m_machines = error.isEmpty() ? machinesOf(file, templates) : juce::StringArray();
		listSiblings();
		juce::StringArray notes;
		if (error.isNotEmpty()) {
			m_spectrum.prepare(0, m_scene.rate);
			m_status.setText(error, juce::dontSendNotification);
			refresh();
			if (onLoaded)
				onLoaded();
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
			p->setLoop(m_loop); // each pass starts with new key-downs
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
		m_gainsL.assign(m_players.size(), 0.0f);
		m_gainsR.assign(m_players.size(), 0.0f);
		m_meters = std::vector<std::atomic<float>>(m_players.size());
		m_selected = 0;
		m_right = m_players.size() > 1 ? 1 : 0;
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
		notes.insert(0, "Space: next (dual: swap ears)   1-" + juce::String(int(m_players.size())) +
		                    ": pick   Return: play/stop   D: dual   B: blind   L: match levels   Up/Down: scene\n"
		                    "Left/Right: stop, step the spectrum a frame (with Shift, a 16th)   [ ]: test\n");
		m_status.setText(notes.joinIntoString("\n"), juce::dontSendNotification);
		refresh();
		resized();
		if (onLoaded)
			onLoaded();
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
			select(m_dual ? m_right.load() : (m_selected + 1) % int(m_players.size()));
		else if (k == juce::KeyPress::returnKey)
			togglePlay();
		else if (k.getTextCharacter() == 'd' || k.getTextCharacter() == 'D')
			setDual(!m_dual);
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
	void filesDropped(const juce::StringArray &files, int, int) override { open(juce::File(files[0])); }

	void paint(juce::Graphics &g) override
	{
		g.fillAll(colours::background);
		// The machines under test (in the accent), then the scene.
		const int at = m_siblings.indexOf(m_file);
		const juce::String number = at >= 0 ? juce::String(at + 1) + "  " : juce::String();
		juce::AttributedString title;
		title.setJustification(juce::Justification::centredLeft);
		title.setWordWrap(juce::AttributedString::none);
		if (!m_machines.isEmpty())
			title.append(m_machines.joinIntoString(", ") + "     ", juce::FontOptions(20.0f, juce::Font::bold), colours::accent);
		title.append(m_scene.name.isEmpty() ? juce::String("Open or drop a scene") : number + m_scene.name,
		             juce::FontOptions(20.0f, juce::Font::bold), colours::text);
		title.draw(g, juce::Rectangle<float>(16, 12, float(getWidth() - 32), 28));
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
		row.removeFromRight(8);
		m_dualButton.setBounds(row.removeFromRight(90).reduced(0, 4));
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
				                       open(fc.getResult());
		                       });
	}
	void open(const juce::File &file)
	{
		if (onOpen)
			onOpen(file);
		else
			load(file);
	}

	// The scenes next to the current one, by name, for the picker and Up/Down; numbered from 1 in
	// that order.
	void listSiblings()
	{
		m_siblings = m_file.getParentDirectory().findChildFiles(juce::File::findFiles, false, "*.json");
		std::sort(m_siblings.begin(), m_siblings.end(),
		          [](const juce::File &a, const juce::File &b) { return a.getFileName().compareNatural(b.getFileName()) < 0; });
		m_picker.clear(juce::dontSendNotification);
		for (int i = 0; i < m_siblings.size(); ++i)
			m_picker.addItem(juce::String(i + 1) + "  " + m_siblings[i].getFileNameWithoutExtension(), i + 1);
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
	// heard to the sources' mean (on top of the scene's own gainDb), unless the scene says
	// "matchLevels": false. The renders are kept, in
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
			if (!m_scene.matchLevels) { // as rendered: show each source's level instead of a gain
				m_levelText.push_back(db[i] > -150 ? juce::String(db[i], 1) + " dB RMS" : juce::String("silent"));
				continue;
			}
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

	// The source heard (in dual mode, in the left ear; picking the right one swaps them).
	void select(int i)
	{
		if (m_dual && i == m_right)
			m_right = m_selected.load();
		m_selected = i;
		refresh();
	}

	// Dual mode: the selected source in the left ear, another in the right.
	void setDual(bool on)
	{
		const int n = int(m_players.size());
		if (on && n > 1 && (m_right == m_selected || m_right >= n))
			m_right = (m_selected + 1) % n;
		m_dual = on;
		refresh();
	}

	void togglePlay()
	{
		if (m_players.empty())
			return;
		if (m_playing)
			m_playing = false;
		else {
			if (onPlay)
				onPlay();
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
		m_dualButton.setButtonText(m_dual ? "Dual: on" : "Dual: off");
		m_blind.setButtonText(m_isBlind ? "Blind: on" : "Blind: off");
		m_reveal.setVisible(m_isBlind && !m_revealed);
		for (size_t k = 0; k < m_sourceButtons.size(); ++k) {
			// In blind mode button k plays source m_order[k] and is named by its letter only.
			const int src = m_isBlind ? m_order[k] : int(k);
			const juce::String letter = juce::String::charToString(juce::juce_wchar('A' + int(k)));
			juce::String text = letter;
			if (!m_isBlind || m_revealed)
				text << ": " << m_scene.sources[size_t(src)].label << "  (" << m_levelText[size_t(src)] << ")";
			if (m_dual && src == m_selected)
				text << "  [left]";
			else if (m_dual && src == m_right)
				text << "  [right]";
			auto &b = *m_sourceButtons[k];
			b.setButtonText(text);
			b.onClick = [this, src] { select(src); };
			const bool on = src == m_selected || (m_dual && src == m_right);
			b.setColour(juce::TextButton::buttonColourId, on ? colours::accentDim : colours::panel);
			b.setColour(juce::TextButton::textColourOffId, on ? colours::accent : colours::text);
			m_editButtons[k]->onClick = [this, src] { showEditor(src); };
			m_editButtons[k]->setVisible(!m_isBlind || m_revealed);
		}
		showSpectrum();
		repaint();
	}

	// The reference and the source playing; in blind mode, until Reveal, nothing: a spectrum would
	// give the sources away.
	void showSpectrum()
	{
		const int heard = m_selected;
		m_spectrum.setVisible(!m_isBlind || m_revealed);
		if (m_players.empty())
			m_spectrum.show(-1, {}, -1, {});
		else
			m_spectrum.show(m_reference, m_scene.sources[size_t(m_reference)].label, heard, m_scene.sources[size_t(heard)].label);
	}

	void timerCallback() override
	{
		if (!isShowing())
			return;
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
		const int left = m_selected, right = m_dual ? m_right.load() : left;
		const bool dual = m_dual;
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
				// Ramp each source's gain in each ear toward on (the selected one; in dual mode, the
				// left ear's and the right ear's) or off: 5 ms. Dual: each source in mono, the left ear
				// on the first output, the right on the others.
				const float step = float(1.0 / (0.005 * m_scene.rate));
				const float targetL = int(i) == left ? 1.0f : 0.0f, targetR = int(i) == right ? 1.0f : 0.0f;
				const int last = view.getNumChannels() - 1, heardCh = std::min(heard(m_players[i]->numOutputs()), last + 1);
				for (int s = 0; s < len; ++s) {
					m_gainsL[i] += juce::jlimit(-step, step, targetL - m_gainsL[i]);
					m_gainsR[i] += juce::jlimit(-step, step, targetR - m_gainsR[i]);
					const float gl = m_gainsL[i] * m_autoGain[i], gr = m_gainsR[i] * m_autoGain[i];
					if (gl <= 0 && gr <= 0)
						continue;
					const float mono = heardCh > 1 ? 0.5f * (view.getSample(0, s) + view.getSample(1, s)) : view.getSample(0, s);
					for (int c = 0; c < numOut; ++c)
						out[c][done + s] += (c == 0 ? gl : gr) * (dual ? mono : view.getSample(std::min(c, last), s));
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

	juce::AudioDeviceManager &m_devices; // the app's, shared by the tests
	juce::AudioPluginFormatManager &m_formats;
	Scene m_scene;
	juce::File m_file;
	juce::StringArray m_machines; // what the scene tests, for the title
	std::vector<std::unique_ptr<Player>> m_players;
	std::vector<std::unique_ptr<juce::TextButton>> m_sourceButtons, m_editButtons;
	std::vector<std::unique_ptr<EditorWindow>> m_editors;
	std::vector<juce::AudioBuffer<float>> m_mix;
	std::vector<float> m_gainsL, m_gainsR, m_autoGain; // the switch's gains, per ear; the level match's
	std::vector<std::vector<float>> m_renders; // a loop of each source, mono, before its gain
	std::vector<std::atomic<float>> m_meters;
	std::vector<juce::String> m_levelText;
	std::vector<int> m_order;
	std::atomic<int64_t> m_position{0};
	int64_t m_loop = 1;
	std::atomic<int> m_selected{0}, m_right{1}; // heard (dual: left), dual's right
	int m_reference = 0;
	std::atomic<bool> m_playing{false}, m_restart{false}, m_dual{false};
	bool m_isBlind = false, m_revealed = false;
	juce::TextButton m_open{"Open"}, m_play{"Play"}, m_match{"Match levels"}, m_prev{"<"}, m_next{">"}, m_dualButton{"Dual: off"},
		m_blind{"Blind: off"}, m_reveal{"Reveal"};
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

// The window's content: the tests down the left, the one shown on the right. A test is a folder of
// scenes; its pane (a Main) keeps its scene, sources and modes while another is shown, and stops
// when another is shown. A test restored from tests.json loads when it is first shown.
class Shell final : public juce::Component, private juce::Timer {
public:
	static constexpr int kNav = 240, kHeader = 44, kRow = 56;

	Shell()
	{
		juce::addDefaultFormatsToManager(m_formats);
		m_devices.initialiseWithDefaultDevices(0, 2);
		m_blank = makePane();
		addAndMakeVisible(*m_blank);
		setWantsKeyboardFocus(true);
		setSize(kNav + 880, 680);
		startTimerHz(5);
	}
	~Shell() override
	{
		m_tests.clear();
		m_blank.reset();
	}

	std::function<void()> changed; // the tests, or the scene shown, changed (the app saves them)

	// Into the test on the scene's folder (loaded there), else a new one at the end; shows it.
	void open(const juce::File &file)
	{
		int i = find(file);
		if (i < 0) {
			m_tests.push_back(std::make_unique<Test>());
			m_tests.back()->folder = file.getParentDirectory();
			describe(*m_tests.back(), file);
			i = int(m_tests.size()) - 1;
		}
		auto &t = *m_tests[size_t(i)];
		if (t.pane == nullptr || !t.pane->hasScene())
			t.pending = file;
		if (!show(i))
			t.pane->load(file);
	}

	// The test on `path` (a scene or its folder; none: the one shown) loads its scene again, a
	// rebuilt plugin from a new copy. A test not shown yet loads fresh when it is.
	void reload(const juce::File &path)
	{
		const int i = path == juce::File() ? m_shown : find(path);
		if (i >= 0 && i < int(m_tests.size()) && m_tests[size_t(i)]->pane != nullptr)
			m_tests[size_t(i)]->pane->reload();
	}

	void remove(const juce::File &path)
	{
		const int i = path == juce::File() ? m_shown : find(path);
		if (i < 0 || i >= int(m_tests.size()))
			return;
		m_tests.erase(m_tests.begin() + i);
		const int next = m_shown == i ? std::min(i, int(m_tests.size()) - 1) : m_shown - (m_shown > i ? 1 : 0);
		m_shown = -1;
		if (next >= 0)
			show(next);
		else
			showBlank();
		if (changed)
			changed();
	}

	// {"tests": [{"scene": path}, ...], "shown": n}
	juce::var saved() const
	{
		juce::Array<juce::var> list;
		for (const auto &t : m_tests) {
			auto *o = new juce::DynamicObject();
			o->setProperty("scene", (t->pane != nullptr && t->pane->hasScene() ? t->pane->file() : t->pending).getFullPathName());
			list.add(juce::var(o));
		}
		auto *o = new juce::DynamicObject();
		o->setProperty("tests", list);
		o->setProperty("shown", m_shown);
		return juce::var(o);
	}
	void restore(const juce::var &saved)
	{
		if (const auto *list = saved.getProperty("tests", {}).getArray())
			for (const auto &e : *list) {
				const juce::File file(e.getProperty("scene", {}).toString());
				if (!juce::File::isAbsolutePath(file.getFullPathName()) || !file.existsAsFile() || find(file) >= 0)
					continue;
				m_tests.push_back(std::make_unique<Test>());
				m_tests.back()->folder = file.getParentDirectory();
				m_tests.back()->pending = file;
				describe(*m_tests.back(), file);
			}
		if (!m_tests.empty())
			show(std::clamp(int(saved.getProperty("shown", 0)), 0, int(m_tests.size()) - 1));
	}

	Main &shown() { return m_shown >= 0 ? *m_tests[size_t(m_shown)]->pane : *m_blank; }

	bool keyPressed(const juce::KeyPress &k) override
	{
		if ((k.getTextCharacter() == '[' || k.getTextCharacter() == ']') && !m_tests.empty()) {
			show((m_shown + (k.getTextCharacter() == '[' ? -1 : 1) + int(m_tests.size())) % int(m_tests.size()));
			return true;
		}
		return false;
	}

	void paint(juce::Graphics &g) override
	{
		g.fillAll(colours::background);
		const auto nav = getLocalBounds().removeFromLeft(kNav).toFloat();
		g.setColour(colours::panel);
		g.fillRect(nav);
		g.setColour(colours::edge);
		g.fillRect(nav.withX(nav.getRight() - 1).withWidth(1));
		g.setColour(colours::dim);
		g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
		g.drawText("TESTS", juce::Rectangle<float>(16, 0, kNav - 32, kHeader), juce::Justification::centredLeft);
		for (size_t i = 0; i < m_tests.size(); ++i) {
			const auto &t = *m_tests[i];
			const auto row = rowBounds(int(i)).toFloat();
			const bool on = int(i) == m_shown, playing = t.pane != nullptr && t.pane->isPlaying();
			if (on) {
				g.setColour(colours::accentDim);
				g.fillRect(row);
				g.setColour(colours::accent);
				g.fillRect(row.withWidth(3));
			}
			const auto text = row.reduced(16, 8);
			g.setColour(on ? colours::accent : colours::text);
			g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
			g.drawText(t.machines.isEmpty() ? t.where.upToFirstOccurrenceOf("/", false, false) : t.machines.joinIntoString(", "), text.withHeight(20).withTrimmedRight(14),
			           juce::Justification::centredLeft, true);
			g.setColour(colours::dim);
			g.setFont(juce::FontOptions(12.0f));
			g.drawText(t.where + "   " + juce::String(t.scenes) + (t.scenes == 1 ? " scene" : " scenes"),
			           text.withTrimmedTop(22).withHeight(18), juce::Justification::centredLeft, true);
			if (playing) {
				g.setColour(colours::accent);
				g.fillEllipse(text.getRight() - 8, text.getY() + 6, 8, 8);
			}
		}
		g.setColour(colours::dim);
		g.setFont(juce::FontOptions(12.0f));
		g.drawFittedText(m_tests.empty() ? "Open a scene, or drop one here" : "[ and ] step through the tests. Right-click one to reload or remove it.",
		                 juce::Rectangle<int>(16, getHeight() - 56, kNav - 32, 44), juce::Justification::bottomLeft, 3);
	}

	void resized() override
	{
		const auto pane = getLocalBounds().withTrimmedLeft(kNav);
		m_blank->setBounds(pane);
		for (auto &t : m_tests)
			if (t->pane != nullptr)
				t->pane->setBounds(pane);
	}

	void mouseDown(const juce::MouseEvent &e) override
	{
		const int i = e.x < kNav && e.y >= kHeader ? (e.y - kHeader) / kRow : -1;
		if (i < 0 || i >= int(m_tests.size()))
			return;
		if (!e.mods.isPopupMenu()) {
			show(i);
			return;
		}
		juce::PopupMenu menu;
		menu.addItem(1, "Reload", m_tests[size_t(i)]->pane != nullptr);
		menu.addItem(2, "Remove");
		const auto folder = m_tests[size_t(i)]->folder;
		menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(rowBounds(i))),
		                   [this, folder](int r) {
			                   if (r == 1)
				                   reload(folder);
			                   else if (r == 2)
				                   remove(folder);
		                   });
	}

private:
	struct Test {
		juce::File folder, pending; // pending: the scene to load when it is first shown
		std::unique_ptr<Main> pane;
		juce::StringArray machines;
		juce::String where; // the folder's last three parts
		int scenes = 0;
	};

	juce::Rectangle<int> rowBounds(int i) const { return {0, kHeader + i * kRow, kNav - 1, kRow}; }

	// The test on a scene's folder, or on a folder.
	int find(const juce::File &path) const
	{
		const auto folder = path.isDirectory() ? path : path.getParentDirectory();
		for (size_t i = 0; i < m_tests.size(); ++i)
			if (m_tests[i]->folder == folder)
				return int(i);
		return -1;
	}

	// The test's name: the machines its scene tests, else the first that another scene in its folder
	// names (a scene of recordings only names none).
	void describe(Test &t, const juce::File &scene)
	{
		juce::String error;
		const auto templates = Templates::load(Templates::standardFiles(), error);
		t.machines = machinesOf(scene, templates);
		for (const auto &other : t.folder.findChildFiles(juce::File::findFiles, false, "*.json"))
			if (t.machines.isEmpty())
				t.machines = machinesOf(other, templates);
		t.scenes = t.folder.getNumberOfChildFiles(juce::File::findFiles, "*.json");
		juce::StringArray parts;
		for (auto f = t.folder; parts.size() < 3 && f.getParentDirectory() != f; f = f.getParentDirectory())
			parts.insert(0, f.getFileName());
		t.where = parts.joinIntoString("/");
	}

	std::unique_ptr<Main> makePane()
	{
		auto pane = std::make_unique<Main>(m_devices, m_formats);
		pane->onOpen = [this](const juce::File &f) { open(f); };
		return pane;
	}

	// Shows test i (its pane made and its pending scene loaded the first time; true if it loaded
	// one), stopping the one shown before.
	bool show(int i)
	{
		auto &t = *m_tests[size_t(i)];
		for (auto &other : m_tests)
			if (other.get() != &t && other->pane != nullptr) {
				other->pane->stop();
				other->pane->setVisible(false);
			}
		m_blank->setVisible(false);
		m_shown = i;
		bool loaded = false;
		if (t.pane == nullptr) {
			t.pane = makePane();
			Main *pane = t.pane.get();
			pane->onLoaded = [this, pane] {
				for (auto &x : m_tests)
					if (x->pane.get() == pane) {
						if (!pane->machines().isEmpty()) // else the name it had (a scene of recordings only)
							x->machines = pane->machines();
						x->scenes = pane->scenes();
						x->pending = juce::File();
					}
				repaint();
				if (changed)
					changed();
			};
			addChildComponent(*pane);
			resized();
		}
		t.pane->setVisible(true);
		if (!t.pane->hasScene() && t.pending.existsAsFile()) {
			t.pane->load(t.pending);
			loaded = true;
		}
		t.pane->grabKeyboardFocus();
		repaint();
		if (changed)
			changed();
		return loaded;
	}

	void showBlank()
	{
		m_shown = -1;
		m_blank->setVisible(true);
		m_blank->grabKeyboardFocus();
		repaint();
	}

	void timerCallback() override { repaint(getLocalBounds().removeFromLeft(kNav)); } // the playing dots

	juce::AudioDeviceManager m_devices;
	juce::AudioPluginFormatManager m_formats;
	std::vector<std::unique_ptr<Test>> m_tests;
	std::unique_ptr<Main> m_blank; // shown with no tests: Open and drops
	int m_shown = -1;
};

class Window final : public juce::DocumentWindow {
public:
	Window() : DocumentWindow("Lemus AB", colours::background, DocumentWindow::allButtons)
	{
		setUsingNativeTitleBar(true);
		shell = new Shell();
		setContentOwned(shell, true);
		setResizable(true, false);
		centreWithSize(shell->getWidth(), shell->getHeight());
	}
	void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
	Shell *shell = nullptr;
};

class App final : public juce::JUCEApplication, private juce::Timer {
public:
	const juce::String getApplicationName() override { return "Lemus AB"; }
	const juce::String getApplicationVersion() override { return "0.3"; }
	// One instance for every build (each session builds its own), so all the tests are in one
	// window. A second launch writes its command line into inbox/ and quits; the running one reads
	// it (macOS's open-document events reach anotherInstanceStarted directly). JUCE's own check
	// can't do this: it hands over only between copies at the same path. When the launch that hands
	// over is a newer build, it takes over: the running one saves its tests and starts it. The
	// headless runs (--selftest, --snapshot) are their own.
	bool moreThanOneInstanceAllowed() override { return true; }

	void initialise(const juce::String &commandLine) override
	{
		// [SCENE.json ...] [--reload [PATH]] [--remove PATH] [--selftest OUT.wav] [--snapshot OUT.png]:
		// the snapshot writes the window (scene loaded, not playing; after a selftest, its spectrum)
		// and quits, for checks that mustn't make a sound.
		const auto args = juce::StringArray::fromTokens(commandLine, true);
		const auto cwd = juce::File::getCurrentWorkingDirectory();
		const bool headless = args.contains("--selftest") || args.contains("--snapshot");
		if (!headless) {
			if (!m_lock.enter(0)) { // running: hand the command line over
				handOver(args, cwd);
				juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
				return;
			}
			appDir().getChildFile("plugins").deleteRecursively(); // no copy is loaded now
			m_built = built(executable());
		}
		m_window = std::make_unique<Window>();
		Shell &shell = *m_window->shell;
		m_window->setVisible(true);
		if (!headless) {
			shell.restore(juce::JSON::parse(testsFile()));
			shell.changed = [this] { save(); };
		}
		run(args, cwd);
		if (!headless) {
			takeInbox();
			startTimer(250);
		}
		Main &main = shell.shown();
		const int st = args.indexOf("--selftest");
		if (st >= 0 && st + 1 < args.size()) {
			main.selfTest(cwd.getChildFile(args[st + 1].unquoted()));
			if (!args.contains("--snapshot")) {
				juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
				return;
			}
		}
		const int at = args.indexOf("--snapshot");
		if (at >= 0 && at + 1 < args.size()) {
			const auto out = cwd.getChildFile(args[at + 1].unquoted());
			juce::Timer::callAfterDelay(800, [&shell, out] {
				const auto image = shell.createComponentSnapshot(shell.getLocalBounds(), true, 2.0f);
				out.deleteFile();
				juce::FileOutputStream stream(out);
				juce::PNGImageFormat().writeImageToStream(image, stream);
				stream.flush();
				juce::JUCEApplicationBase::quit();
			});
		}
	}

	// A scene opened while the app runs (macOS's open-document event). In a launch that is handing
	// over (no window), `open -a ... SCENE.json`'s scene arrives this way after initialise: it goes
	// over too.
	void anotherInstanceStarted(const juce::String &commandLine) override
	{
		const auto args = juce::StringArray::fromTokens(commandLine, true);
		if (m_window == nullptr)
			handOver(args, juce::File::getCurrentWorkingDirectory());
		else
			run(args, juce::File::getCurrentWorkingDirectory());
	}

	void shutdown() override
	{
		stopTimer();
		m_window.reset();
	}
	void systemRequestedQuit() override { quit(); }

private:
	juce::InterProcessLock m_lock{"Lemus AB, one window"};
	std::unique_ptr<Window> m_window;
	int64_t m_built = 0; // this executable's time when it started

	static juce::File testsFile() { return appDir().getChildFile("tests.json"); }
	static juce::File inbox() { return appDir().getChildFile("inbox"); }
	static juce::File executable() { return juce::File::getSpecialLocation(juce::File::currentExecutableFile); }
	static int64_t built(const juce::File &exe) { return exe.getLastModificationTime().toMilliseconds(); }

	static void writeFile(const juce::File &file, const juce::String &text) // whole or not at all
	{
		file.getParentDirectory().createDirectory();
		const auto part = file.getSiblingFile(file.getFileName() + ".part");
		if (part.replaceWithText(text))
			part.moveFileTo(file);
	}

	void save()
	{
		if (m_window != nullptr)
			writeFile(testsFile(), juce::JSON::toString(m_window->shell->saved()));
	}

	// The scenes to open (each into its folder's test), --reload [PATH], --remove PATH.
	void run(const juce::StringArray &args, const juce::File &cwd)
	{
		if (m_window == nullptr)
			return;
		Shell &shell = *m_window->shell;
		bool opened = false;
		for (int i = 0; i < args.size(); ++i) {
			const auto a = args[i].unquoted();
			const bool hasPath = i + 1 < args.size() && !args[i + 1].startsWith("--");
			if (a == "--reload" || a == "--remove") {
				const auto path = hasPath ? cwd.getChildFile(args[++i].unquoted()) : juce::File();
				a == "--reload" ? shell.reload(path) : shell.remove(path);
			} else if (a == "--selftest" || a == "--snapshot")
				++i;
			else if (a.endsWith(".json")) {
				shell.open(cwd.getChildFile(a));
				opened = true;
			}
		}
		if (opened && !args.contains("--selftest") && !args.contains("--snapshot")) {
			m_window->toFront(true);
			juce::Process::makeForegroundProcess();
		}
	}

	void handOver(const juce::StringArray &args, const juce::File &cwd)
	{
		auto *o = new juce::DynamicObject();
		o->setProperty("cwd", cwd.getFullPathName());
		o->setProperty("exe", executable().getFullPathName());
		o->setProperty("built", built(executable()));
		juce::Array<juce::var> list;
		for (const auto &a : args)
			list.add(a);
		o->setProperty("args", list);
		writeFile(inbox().getChildFile(juce::String(juce::Time::currentTimeMillis()) + "-" + juce::String::toHexString(juce::Random().nextInt()) + ".json"),
		          juce::JSON::toString(juce::var(o)));
	}

	// The command lines handed over, oldest first. One from a newer build hands the app over to it
	// instead, the command left for the newer one to read.
	void takeInbox()
	{
		auto files = inbox().findChildFiles(juce::File::findFiles, false, "*.json");
		std::sort(files.begin(), files.end(), [](const juce::File &a, const juce::File &b) { return a.getFileName().compareNatural(b.getFileName()) < 0; });
		for (const auto &f : files) {
			const auto json = juce::JSON::parse(f);
			const juce::File exe(json.getProperty("exe", {}).toString());
			// newer than this one was at its start (a build in another folder, or this one's rebuilt)
			if (exe.existsAsFile() && int64_t(json.getProperty("built", 0)) > m_built &&
			    built(exe) == int64_t(json.getProperty("built", 0))) {
				takeOver(exe);
				return;
			}
			f.deleteFile();
			juce::StringArray args;
			if (const auto *list = json.getProperty("args", {}).getArray())
				for (const auto &a : *list)
					args.add(a.toString());
			run(args, juce::File(json.getProperty("cwd", "/").toString()));
		}
	}

	void takeOver(const juce::File &exe)
	{
		stopTimer();
		save();
		m_lock.exit();
		const auto app = exe.getParentDirectory().getParentDirectory().getParentDirectory(); // <app>/Contents/MacOS/<exe>
		juce::ChildProcess open;
		if (open.start(juce::StringArray{"/usr/bin/open", "-n", app.getFullPathName()}))
			open.waitForProcessToFinish(10000);
		quit();
	}

	void timerCallback() override { takeInbox(); }
};

} // namespace

START_JUCE_APPLICATION(App)
