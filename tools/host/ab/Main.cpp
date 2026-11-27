// Lemus A/B: plays a scene (tools/host/README.md) through all its sources at once, in sync and
// looping, and lets you hear one at a time: Space switches, 1-9 pick, Return plays and stops.
// Sources are level-matched when the scene loads (one loop rendered offline), switching
// crossfades over 5 ms, and blind mode hides which is which until you reveal it. The other
// scenes in the same folder are listed at the pane's left (a click, Up/Down, or type to filter) in
// the folder's natural sort order. Every scene has its own number, across all machines and kept for
// good (SceneIds): "scene 214, B" names a clip and a source. After changing a source in its editor
// (its volume, say), Match levels (L) measures them again. The spectrum (Spectrum.h) shows what
// is playing against the reference (hidden in blind mode until Reveal); Left/Right stop and step it
// through the loop a frame at a time.
// Dual mode (D) plays two at once, one in each ear, each in mono: the selected source left, another
// right; Space swaps them.
// Drag across the position bar (or drag its ends) to loop a part of the scene; a part plays from
// the renders the level match made (so after changing a plugin, Match levels hears the change).
// Drag across the spectrum to hear only that band (every source, so switching compares the band).
// Double-click either, or Esc, for all of it again.
// One app, one window: a test (a set) per scene folder, named for the machines its scenes test.
// The machines are listed down the left, newest first (a scene or clip changed, or a session
// opened the set); a machine's sets are tabs over the pane. A scene opened while the app runs, from
// any session's build, goes to its folder's set, or a new one; only the set shown plays. The sets
// come back after a restart.
// Our own plugins load from a fresh copy whenever the installed one has changed, so a rebuilt
// plugin is heard at the next scene load or Reload, without a restart.
//
//   open -a "Lemus AB.app" /abs/SCENE.json            add the scene's folder as a test (or go to it)
//   open -a "Lemus AB.app" --args --reload [/abs/PATH] load the test again, with rebuilt plugins
//   open -a "Lemus AB.app" --args --remove /abs/PATH   take the test off the list
//   Lemus AB.app/Contents/MacOS/Lemus AB --which N     print scene N's file
//   (or Lemus AB.app/Contents/MacOS/Lemus AB [ARGS], or drop a scene on the window; PATH is a
//   scene or its folder; without one, --reload is the test shown)
#include "../Player.h"
#include "Spectrum.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <map>
#include <set>
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

// A folder's scenes, in natural order (by file name: 2 before 10).
juce::Array<juce::File> scenesIn(const juce::File &folder)
{
	auto files = folder.findChildFiles(juce::File::findFiles, false, "*.json");
	std::sort(files.begin(), files.end(), [](const juce::File &a, const juce::File &b) { return a.getFileName().compareNatural(b.getFileName()) < 0; });
	return files;
}

// Every scene's number, to name it by: given once, the first time the app sees the scene (a set's
// scenes together, in their order, so they run on), never reused, from 101 (so it can't be taken for
// a list position). Kept in scene-ids.json in the app's folder ({"next": n, "ids": {path: n}}),
// written only by the app itself (not a headless run); a session looks a number up there or with
// --which N. A scene renamed or moved is a new scene.
class SceneIds {
public:
	static SceneIds &get()
	{
		static SceneIds ids;
		return ids;
	}
	void setSaving(bool on) { m_saving = on; }
	int of(const juce::File &f) const
	{
		const auto it = m_ids.find(f.getFullPathName());
		return it != m_ids.end() ? it->second : 0;
	}
	juce::File fileOf(int id) const
	{
		for (const auto &[path, i] : m_ids)
			if (i == id)
				return juce::File(path);
		return {};
	}
	void give(const juce::Array<juce::File> &scenes)
	{
		bool added = false;
		for (const auto &f : scenes)
			if (m_ids.emplace(f.getFullPathName(), m_next).second) {
				++m_next;
				added = true;
			}
		if (added && m_saving)
			save();
	}

private:
	SceneIds()
	{
		const auto json = juce::JSON::parse(file());
		m_next = std::max(101, int(json.getProperty("next", 101)));
		if (const auto *o = json.getProperty("ids", {}).getDynamicObject())
			for (const auto &p : o->getProperties()) {
				m_ids[p.name.toString()] = int(p.value);
				m_next = std::max(m_next, int(p.value) + 1);
			}
	}
	static juce::File file() { return appDir().getChildFile("scene-ids.json"); }
	void save() const
	{
		auto *ids = new juce::DynamicObject();
		for (const auto &[path, i] : m_ids)
			ids->setProperty(path, i);
		auto *o = new juce::DynamicObject();
		o->setProperty("next", m_next);
		o->setProperty("ids", juce::var(ids));
		appDir().createDirectory();
		file().replaceWithText(juce::JSON::toString(juce::var(o)));
	}
	std::map<juce::String, int> m_ids;
	int m_next = 101;
	bool m_saving = false;
};

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
                   private juce::ListBoxModel,
                   private juce::Timer {
public:
	Main(juce::AudioDeviceManager &devices, juce::AudioPluginFormatManager &formats) : m_devices(devices), m_formats(formats)
	{
		for (auto *b : {&m_open, &m_play, &m_match, &m_dualButton, &m_blind, &m_reveal}) {
			addAndMakeVisible(*b);
			b->setWantsKeyboardFocus(false);
		}
		m_open.onClick = [this] { choose(); };
		m_play.onClick = [this] { togglePlay(); };
		m_dualButton.onClick = [this] { setDual(!m_dual); };
		m_blind.onClick = [this] { setBlind(!m_isBlind); };
		m_reveal.onClick = [this] { m_revealed = true; refresh(); };
		m_match.onClick = [this] { rematch(); };
		// The folder's scenes: a list, filtered by what is typed above it (Return: the first match;
		// Esc: all again). The list never takes the keys: Up/Down step through what it shows.
		addAndMakeVisible(m_filter);
		m_filter.setFont(juce::FontOptions(13.0f));
		m_filter.setIndents(8, 7);
		m_filter.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1f2227));
		m_filter.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff3a414b));
		m_filter.setColour(juce::TextEditor::focusedOutlineColourId, colours::accent);
		m_filter.setColour(juce::TextEditor::textColourId, colours::text);
		m_filter.setColour(juce::TextEditor::highlightColourId, colours::accent.withAlpha(0.35f));
		m_filter.setColour(juce::CaretComponent::caretColourId, colours::accent);
		m_filter.onTextChange = [this] { filterScenes(); };
		m_filter.onReturnKey = [this] {
			if (!m_visible.empty() && std::find(m_visible.begin(), m_visible.end(), m_siblings.indexOf(m_file)) == m_visible.end())
				load(m_siblings[m_visible.front()]);
			grabKeyboardFocus();
		};
		m_filter.onEscapeKey = [this] {
			m_filter.clear();
			filterScenes();
			grabKeyboardFocus();
		};
		addAndMakeVisible(m_list);
		m_list.setModel(this);
		m_list.setRowHeight(30);
		m_list.setWantsKeyboardFocus(false);
		m_list.setColour(juce::ListBox::backgroundColourId, colours::background);
		m_list.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, colours::edge);
		addAndMakeVisible(m_spectrum);
		m_spectrum.onBand = [this](float low, float high) {
			m_bandLow = low;
			m_bandHigh = high;
		};
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
	// For headless checks (--part, --band): a part of the loop (seconds), a band (Hz).
	void loopPart(double from, double to) { setPart(std::llround(from * m_scene.rate), std::llround(to * m_scene.rate)); }
	void hearBand(float low, float high) { setBand(low, high); }
	bool hasScene() const { return m_file != juce::File(); }
	bool isPlaying() const { return m_playing; }
	juce::File file() const { return m_file; }
	const juce::StringArray &machines() const { return m_machines; }
	int scenes() const { return m_siblings.size(); }
	// The folder's list again (a scene added or taken away), the scene playing on.
	void relist()
	{
		if (hasScene()) {
			listSiblings();
			repaint();
		}
	}
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
		m_partFrom = 0;
		m_partTo = m_loop;
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
		                    ": pick   Return: play/stop   D: dual   B: blind   L: match levels\n"
		                    "Up/Down: scene   Shift+Up/Down: set   [ ]: machine   Left/Right: stop, step the spectrum a frame (Shift: a 16th)\n"
		                    "Drag across the bar: loop a part   Drag across the spectrum: hear a band   Esc: all\n");
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
		else if (k == juce::KeyPress::upKey && !k.getModifiers().isShiftDown()) // with Shift: the set (the app's)
			step(-1);
		else if (k == juce::KeyPress::downKey && !k.getModifiers().isShiftDown())
			step(1);
		else if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey) // with Shift too
			stepFrame(k.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1, k.getModifiers().isShiftDown());
		else if (k == juce::KeyPress::escapeKey) {
			setPart(0, m_loop);
			setBand(0, 0);
		} else
			return false;
		return true;
	}

	// The position bar: a press on an end of the part (within 6 px) takes that end; a drag anywhere
	// else on the bar loops the part dragged across; a double-click, all of it.
	void mouseDown(const juce::MouseEvent &e) override
	{
		m_barDrag = BarDrag::None;
		if (m_players.empty() || !barHit(e.position))
			return;
		const float x0 = xOfSample(m_partFrom.load()), x1 = xOfSample(m_partTo.load());
		if (std::abs(e.position.x - x0) <= 6)
			m_barDrag = BarDrag::From;
		else if (std::abs(e.position.x - x1) <= 6)
			m_barDrag = BarDrag::To;
		else {
			m_barDrag = BarDrag::New;
			m_barAnchor = sampleAt(e.position.x);
		}
	}
	void mouseDrag(const juce::MouseEvent &e) override
	{
		const int64_t at = sampleAt(e.position.x), shortest = std::llround(0.05 * m_scene.rate);
		const int64_t from = m_partFrom.load(), to = m_partTo.load();
		if (m_barDrag == BarDrag::From)
			setPart(std::min(at, to - shortest), to);
		else if (m_barDrag == BarDrag::To)
			setPart(from, std::max(at, from + shortest));
		else if (m_barDrag == BarDrag::New && std::abs(e.getDistanceFromDragStartX()) > 3)
			setPart(std::min(at, m_barAnchor), std::max(std::max(at, m_barAnchor), std::min(at, m_barAnchor) + shortest));
	}
	void mouseUp(const juce::MouseEvent &) override { m_barDrag = BarDrag::None; }
	void mouseDoubleClick(const juce::MouseEvent &e) override
	{
		if (!m_players.empty() && barHit(e.position))
			setPart(0, m_loop);
	}
	void mouseMove(const juce::MouseEvent &e) override
	{
		const bool edge = !m_players.empty() && barHit(e.position) &&
		                  (std::abs(e.position.x - xOfSample(m_partFrom.load())) <= 6 || std::abs(e.position.x - xOfSample(m_partTo.load())) <= 6);
		setMouseCursor(edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
	}

	bool isInterestedInFileDrag(const juce::StringArray &files) override { return files.size() == 1 && files[0].endsWith(".json"); }
	void filesDropped(const juce::StringArray &files, int, int) override { open(juce::File(files[0])); }

	void paint(juce::Graphics &g) override
	{
		g.fillAll(colours::background);
		g.setColour(colours::edge);
		g.fillRect(m_list.getRight(), 0, 1, getHeight());
		// The scene: its number (SceneIds, in the accent), its name, its recording and length.
		const int at = m_siblings.indexOf(m_file);
		juce::AttributedString title;
		title.setJustification(juce::Justification::centredLeft);
		title.setWordWrap(juce::AttributedString::none);
		if (at >= 0)
			title.append(juce::String(m_rows[size_t(at)].id) + "  ", juce::FontOptions(19.0f, juce::Font::bold), colours::accent);
		title.append(at >= 0 ? m_rows[size_t(at)].name : m_scene.name.isEmpty() ? juce::String("Open or drop a scene") : m_scene.name,
		             juce::FontOptions(19.0f, juce::Font::bold), colours::text);
		if (at >= 0)
			title.append("    " + m_rows[size_t(at)].detail(), juce::FontOptions(13.0f), colours::dim);
		title.draw(g, m_title.toFloat());
		if (m_players.empty())
			return;
		// Position through the loop, and the part looped: lit, with a handle at each end.
		const auto bar = barRect();
		const int64_t from = m_partFrom.load(), to = m_partTo.load();
		const float x0 = xOfSample(from), x1 = xOfSample(to), xp = xOfSample(m_position.load());
		g.setColour(colours::panel);
		g.fillRoundedRectangle(bar, 3);
		if (isPart()) {
			g.setColour(colours::accent.withAlpha(0.25f));
			g.fillRect(juce::Rectangle<float>(x0, bar.getY(), x1 - x0, bar.getHeight()));
		}
		g.setColour(colours::accent);
		g.fillRoundedRectangle(juce::Rectangle<float>(x0, bar.getY(), std::max(0.0f, xp - x0), bar.getHeight()), 3);
		g.setColour(isPart() ? colours::accent : colours::dim.withAlpha(0.5f));
		for (float x : {x0, x1})
			g.fillRoundedRectangle(juce::Rectangle<float>(x - 2, bar.getY() - 5, 4, bar.getHeight() + 10), 2);
		if (isPart()) {
			g.setColour(colours::dim);
			g.setFont(juce::FontOptions(12.0f));
			g.drawText("looping " + juce::String(double(from) / m_scene.rate, 2) + " to " + juce::String(double(to) / m_scene.rate, 2) +
			               " s   (double-click the bar: all)",
			           bar.withY(bar.getY() - 19).withHeight(13).toNearestInt(), juce::Justification::centredLeft);
		}
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
		auto all = getLocalBounds();
		auto side = all.removeFromLeft(getWidth() >= 1000 ? 340 : 260);
		m_filter.setBounds(side.removeFromTop(48).reduced(12, 8));
		m_list.setBounds(side.withTrimmedRight(1));
		auto r = all.reduced(18, 14);
		m_title = r.removeFromTop(30);
		r.removeFromTop(6);
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
		m_bar = r.removeFromTop(28).withTrimmedTop(16).withHeight(6).toFloat();
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

	// A scene in the list: its name to read (its "title", else its file's name without the number in
	// front, the recording's id or the dashes), the recording (the id in brackets in a source's
	// label: "MS-20 (g6OfcNTvJow)") and the loop's length.
	struct SceneRow {
		juce::String name, recording, file;
		double seconds = 0;
		int id = 0; // SceneIds'
		juce::String detail() const
		{
			return (recording.isNotEmpty() ? recording + "  " : juce::String()) + juce::String(seconds, 1) + " s";
		}
	};
	static SceneRow describeScene(const juce::File &f)
	{
		SceneRow row;
		row.file = f.getFileNameWithoutExtension();
		const auto json = juce::JSON::parse(f), list = json.getProperty("sources", {});
		if (const auto *sources = list.getArray())
			for (const auto &src : *sources) {
				const auto label = src.getProperty("label", {}).toString();
				const auto id = label.fromLastOccurrenceOf("(", false, false).upToFirstOccurrenceOf(")", false, false);
				if (label.containsChar('(') && id.length() >= 6 && !id.containsAnyOf(" ,:")) {
					row.recording = id;
					break;
				}
			}
		const double bpm = json.getProperty("bpm", 120.0);
		row.seconds = double(json.getProperty("beats", 0.0)) * 60.0 / (bpm > 0 ? bpm : 120.0);
		if (const auto title = json.getProperty("title", {}).toString(); title.isNotEmpty()) {
			row.name = title;
			return row;
		}
		auto s = row.file;
		int digits = 0;
		while (digits < s.length() && juce::CharacterFunctions::isDigit(s[digits]))
			++digits;
		if (digits > 0 && digits < s.length() && s[digits] == '-')
			s = s.substring(digits + 1);
		if (row.recording.isNotEmpty() && s.startsWith(row.recording + "-"))
			s = s.substring(row.recording.length() + 1);
		s = s.trimCharactersAtEnd("-");
		for (int i = 0; i < s.length(); ++i) {
			const bool between = i > 0 && i + 1 < s.length() && juce::CharacterFunctions::isDigit(s[i - 1]) &&
			                     juce::CharacterFunctions::isDigit(s[i + 1]);
			row.name += s[i] == '-' && !between ? juce::juce_wchar(' ') : s[i];
		}
		return row;
	}

	// The scenes next to the current one, by name, for the list and Up/Down, each with its number
	// (new ones get theirs now).
	void listSiblings()
	{
		m_siblings = scenesIn(m_file.getParentDirectory());
		SceneIds::get().give(m_siblings);
		m_rows.clear();
		for (const auto &f : m_siblings) {
			m_rows.push_back(describeScene(f));
			m_rows.back().id = SceneIds::get().of(f);
		}
		m_filter.setTextToShowWhenEmpty("Filter the " + juce::String(m_siblings.size()) + (m_siblings.size() == 1 ? " scene" : " scenes"),
		                                colours::dim);
		filterScenes();
	}
	// The rows the filter leaves (every word typed, in the number, the name, the file or the recording).
	void filterScenes()
	{
		juce::StringArray words;
		words.addTokens(m_filter.getText().toLowerCase(), " ", "");
		words.removeEmptyStrings();
		m_visible.clear();
		for (size_t i = 0; i < m_rows.size(); ++i) {
			const auto &r = m_rows[i];
			const auto hay = (juce::String(r.id) + " " + r.name + " " + r.file + " " + r.recording).toLowerCase();
			bool all = true;
			for (const auto &w : words)
				all = all && hay.contains(w);
			if (all)
				m_visible.push_back(int(i));
		}
		m_list.updateContent();
		const auto at = std::find(m_visible.begin(), m_visible.end(), m_siblings.indexOf(m_file));
		if (at != m_visible.end())
			m_list.scrollToEnsureRowIsOnscreen(int(at - m_visible.begin()));
		m_list.repaint();
	}

	// Up/Down: the next scene the list shows (filtered: from the current one, else the first).
	void step(int by)
	{
		if (m_visible.empty())
			return;
		const auto at = std::find(m_visible.begin(), m_visible.end(), m_siblings.indexOf(m_file));
		if (at == m_visible.end()) {
			load(m_siblings[m_visible.front()]);
			return;
		}
		const int k = int(at - m_visible.begin()) + by;
		if (k >= 0 && k < int(m_visible.size()))
			load(m_siblings[m_visible[size_t(k)]]);
	}

	int getNumRows() override { return int(m_visible.size()); }
	void paintListBoxItem(int k, juce::Graphics &g, int w, int h, bool) override
	{
		if (k < 0 || k >= int(m_visible.size()))
			return;
		const int i = m_visible[size_t(k)];
		const auto &r = m_rows[size_t(i)];
		const bool on = m_siblings[i] == m_file;
		if (on) {
			g.fillAll(colours::accentDim);
			g.setColour(colours::accent);
			g.fillRect(0, 0, 3, h);
		}
		auto box = juce::Rectangle<int>(0, 0, w, h).reduced(12, 0);
		g.setFont(juce::FontOptions(13.0f));
		g.setColour(on ? colours::accent : colours::dim);
		g.drawText(juce::String(r.id), box.removeFromLeft(34), juce::Justification::centredRight);
		box.removeFromLeft(10);
		g.setFont(juce::FontOptions(12.0f));
		g.setColour(colours::dim);
		g.drawText(juce::String(r.seconds, 1) + " s", box.removeFromRight(44), juce::Justification::centredRight);
		box.removeFromRight(8);
		g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
		g.setColour(on ? colours::accent : colours::text);
		g.drawText(r.name, box, juce::Justification::centredLeft, true);
	}
	void listBoxItemClicked(int k, const juce::MouseEvent &) override
	{
		if (k >= 0 && k < int(m_visible.size()) && m_siblings[m_visible[size_t(k)]] != m_file)
			load(m_siblings[m_visible[size_t(k)]]);
		grabKeyboardFocus();
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
		m_full.clear();
		std::vector<double> db;
		for (size_t k = 0; k < m_players.size(); ++k) {
			auto &p = m_players[k];
			juce::AudioBuffer<float> buf(std::max(1, p->numOutputs()), m_scene.block);
			const int channels = heard(buf.getNumChannels());
			m_full.emplace_back(channels, int(m_loop));
			double sum = 0;
			int64_t count = 0;
			for (int64_t pos = 0; pos < m_loop; pos += m_scene.block) {
				juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), buf.getNumChannels(), 0,
				                              int(std::min<int64_t>(m_scene.block, m_loop - pos)));
				p->process(view, pos);
				for (int c = 0; c < channels; ++c) {
					m_full[k].copyFrom(c, int(pos), view, c, 0, view.getNumSamples());
					for (int i = 0; i < view.getNumSamples(); ++i) {
						sum += double(view.getSample(c, i)) * view.getSample(c, i), ++count;
						m_renders[k][size_t(pos + i)] += view.getSample(c, i) / float(channels);
					}
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
		m_list.repaint();
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

	// ------------------------------------------------------------------ part and band
	juce::Rectangle<float> barRect() const { return m_bar; }
	bool barHit(juce::Point<float> p) const
	{
		const auto bar = barRect();
		return p.y >= bar.getY() - 12 && p.y <= bar.getBottom() + 6 && p.x >= bar.getX() - 8 && p.x <= bar.getRight() + 8;
	}
	float xOfSample(int64_t s) const
	{
		const auto bar = barRect();
		return bar.getX() + bar.getWidth() * float(double(s) / double(m_loop));
	}
	int64_t sampleAt(float x) const
	{
		const auto bar = barRect();
		return std::llround(double(m_loop) * juce::jlimit(0.0, 1.0, double(x - bar.getX()) / double(bar.getWidth())));
	}
	bool isPart() const { return m_partFrom.load() > 0 || m_partTo.load() < m_loop; }
	// The part looped, in samples ([0, m_loop): all of it, played live). Back to all of it, every
	// source starts again from the top.
	void setPart(int64_t from, int64_t to)
	{
		from = std::clamp<int64_t>(from, 0, m_loop);
		to = std::clamp<int64_t>(to, from, m_loop);
		const bool was = isPart();
		m_partFrom = from;
		m_partTo = to;
		if (was && !isPart())
			m_restart = true;
		repaint();
	}
	void setBand(float low, float high)
	{
		m_spectrum.setBand(low, high);
		m_bandLow = low;
		m_bandHigh = high;
	}

	// Source i's render (the level match's) from pos for len samples into view, faded over 3 ms at
	// the part's ends so the loop doesn't click.
	void playRender(size_t i, juce::AudioBuffer<float> &view, int64_t pos, int len, int64_t from, int64_t to) const
	{
		const auto &src = m_full[i];
		const int have = src.getNumChannels();
		const double fade = std::max(1.0, 0.003 * m_scene.rate);
		for (int c = 0; c < view.getNumChannels(); ++c) {
			const float *in = src.getReadPointer(std::min(c, have - 1));
			float *o = view.getWritePointer(c);
			for (int s = 0; s < len; ++s) {
				const int64_t at = pos + s;
				const double g = std::min({1.0, double(at - from + 1) / fade, double(to - at) / fade});
				o[s] = in[at] * float(g);
			}
		}
	}

	// The band (Butterworth, 8th order each side: a high-pass at its bottom, a low-pass at its top)
	// over what is heard, worked out again on the audio thread when it changes.
	struct Biquad {
		double b0, b1, b2, a1, a2;
	};
	void designBand(float low, float high)
	{
		constexpr double q[] = {0.50979558, 0.60134489, 0.89997622, 2.56291545};
		const bool wasOff = m_stages == 0;
		m_stages = 0;
		const auto add = [&](double f, double qq, bool highPass) {
			const double w = juce::MathConstants<double>::twoPi * f / m_scene.rate, cs = std::cos(w), alpha = std::sin(w) / (2 * qq);
			const double a0 = 1 + alpha;
			const double b0 = highPass ? (1 + cs) / 2 : (1 - cs) / 2, b1 = highPass ? -(1 + cs) : 1 - cs;
			m_biquads[size_t(m_stages++)] = {b0 / a0, b1 / a0, b0 / a0, -2 * cs / a0, (1 - alpha) / a0};
		};
		if (high > low) {
			if (low > 20)
				for (double qq : q)
					add(low, qq, true);
			if (high < 0.45 * m_scene.rate)
				for (double qq : q)
					add(high, qq, false);
		}
		if (wasOff)
			for (auto &ch : m_z)
				for (auto &st : ch)
					st[0] = st[1] = 0;
		m_bandLowSet = low;
		m_bandHighSet = high;
	}
	void applyBand(float *const *out, int numOut, int n)
	{
		const float low = m_bandLow.load(), high = m_bandHigh.load();
		if (low != m_bandLowSet || high != m_bandHighSet)
			designBand(low, high);
		for (int c = 0; c < std::min(numOut, kBandChannels); ++c)
			for (int k = 0; k < m_stages; ++k) {
				const auto &b = m_biquads[size_t(k)];
				auto &z = m_z[size_t(c)][size_t(k)];
				for (int s = 0; s < n; ++s) {
					const double x = out[c][s], y = b.b0 * x + z[0];
					z[0] = b.b1 * x - b.a1 * y + z[1];
					z[1] = b.b2 * x - b.a2 * y;
					out[c][s] = float(y);
				}
			}
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
		// A part of the loop plays from the renders; all of it, live.
		const int64_t from = m_partFrom.load(), to = m_partTo.load();
		const bool part = (from > 0 || to < m_loop) && to > from && m_full.size() == m_players.size();
		const int64_t start = part ? from : 0, end = part ? to : m_loop;
		if (m_restart.exchange(false)) {
			if (!part)
				for (auto &p : m_players)
					p->reset();
			m_position = start;
		}
		if (int(m_mix.size()) < int(m_players.size()))
			return;
		const int left = m_selected, right = m_dual ? m_right.load() : left;
		const bool dual = m_dual;
		for (int done = 0; done < n;) {
			// Up to the loop's (or the part's) end; there every source starts over.
			int64_t pos = m_position.load();
			if (pos < start || pos >= end) // the part has moved
				pos = start;
			const int len = int(std::min<int64_t>(n - done, end - pos));
			for (size_t i = 0; i < m_players.size(); ++i) {
				auto &buf = m_mix[i];
				juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), buf.getNumChannels(), 0, len);
				if (part)
					playRender(i, view, pos, len, start, end);
				else
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
			if (pos >= end) {
				pos = start;
				if (!part)
					for (auto &p : m_players)
						p->reset();
			}
			m_position = pos;
		}
		applyBand(out, numOut, n);
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
	std::atomic<int64_t> m_partFrom{0}, m_partTo{1};       // the part looped (all of it: 0, m_loop)
	std::vector<juce::AudioBuffer<float>> m_full;          // a loop of each source as heard, before its gain
	enum class BarDrag { None, New, From, To } m_barDrag = BarDrag::None;
	int64_t m_barAnchor = 0;
	std::atomic<float> m_bandLow{0}, m_bandHigh{0};        // the band heard (0, 0: all)
	static constexpr int kBandChannels = 8, kBandStages = 8;
	float m_bandLowSet = 0, m_bandHighSet = 0;             // the audio thread's: what m_biquads are for
	int m_stages = 0;
	std::array<Biquad, kBandStages> m_biquads{};
	std::array<std::array<std::array<double, 2>, kBandStages>, kBandChannels> m_z{};
	std::atomic<int> m_selected{0}, m_right{1}; // heard (dual: left), dual's right
	int m_reference = 0;
	std::atomic<bool> m_playing{false}, m_restart{false}, m_dual{false};
	bool m_isBlind = false, m_revealed = false;
	juce::TextButton m_open{"Open"}, m_play{"Play"}, m_match{"Match levels"}, m_dualButton{"Dual: off"}, m_blind{"Blind: off"},
		m_reveal{"Reveal"};
	juce::TextEditor m_filter;
	juce::ListBox m_list;
	juce::Array<juce::File> m_siblings;
	std::vector<SceneRow> m_rows; // one per sibling
	std::vector<int> m_visible;   // the siblings the filter leaves, in order
	juce::Rectangle<int> m_title;
	juce::Rectangle<float> m_bar; // the position bar
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

// The window's content: the machines down the left, newest first; the shown machine's sets as tabs
// over the pane; the set shown below them. A set is a folder of scenes (a "test" in the commands and
// tests.json); its pane (a Main) keeps its scene, sources and modes while another is shown, and
// stops when another is shown. A set restored from tests.json loads when it is first shown.
// A set's time is the newest of its files' (a scene or clip changed) and the last time a session
// opened or reloaded it; a machine's, its newest set's. The files are looked at again every 5 s.
class Shell final : public juce::Component, private juce::Timer {
public:
	static constexpr int kNav = 250, kHeader = 44, kRow = 56, kFoot = 64, kTabs = 60; // kFoot: the help under the list

	Shell()
	{
		juce::addDefaultFormatsToManager(m_formats);
		m_devices.initialiseWithDefaultDevices(0, 2);
		m_blank = makePane();
		addAndMakeVisible(*m_blank);
		setWantsKeyboardFocus(true);
		setSize(kNav + 1190, 820);
		startTimerHz(5);
	}
	~Shell() override
	{
		m_tests.clear();
		m_blank.reset();
	}

	std::function<void()> changed; // the sets, or the scene shown, changed (the app saves them)

	// Into the set on the scene's folder (loaded there), else a new one; shows it.
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
		t.touched = juce::Time::getCurrentTime();
		scan(t);
		if (t.pane == nullptr || !t.pane->hasScene())
			t.pending = file;
		regroup();
		if (!show(i))
			t.pane->load(file);
	}

	// The set on `path` (a scene or its folder; none: the one shown) loads its scene again, a
	// rebuilt plugin from a new copy. A set not shown yet loads fresh when it is.
	void reload(const juce::File &path)
	{
		const int i = path == juce::File() ? m_shown : find(path);
		if (i < 0 || i >= int(m_tests.size()))
			return;
		auto &t = *m_tests[size_t(i)];
		t.touched = juce::Time::getCurrentTime();
		scan(t);
		regroup();
		if (t.pane != nullptr)
			t.pane->reload();
		if (changed)
			changed();
	}

	void remove(const juce::File &path)
	{
		const int i = path == juce::File() ? m_shown : find(path);
		if (i >= 0 && i < int(m_tests.size()))
			removeIf([i](int k) { return k == i; }, -1);
	}

	// {"tests": [{"scene": path, "touched": ms}, ...], "shown": n}
	juce::var saved() const
	{
		juce::Array<juce::var> list;
		for (const auto &t : m_tests) {
			auto *o = new juce::DynamicObject();
			o->setProperty("scene", (t->pane != nullptr && t->pane->hasScene() ? t->pane->file() : t->pending).getFullPathName());
			o->setProperty("touched", t->touched.toMilliseconds());
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
				auto &t = *m_tests.back();
				t.folder = file.getParentDirectory();
				t.pending = file;
				t.touched = juce::Time(juce::int64(e.getProperty("touched", 0)));
				describe(t, file);
				scan(t);
			}
		regroup();
		if (!m_tests.empty())
			show(std::clamp(int(saved.getProperty("shown", 0)), 0, int(m_tests.size()) - 1));
	}

	Main &shown() { return m_shown >= 0 ? *m_tests[size_t(m_shown)]->pane : *m_blank; }

	// [ ]: the machine above or below; Shift+Up/Down: the set left or right (the pane passes them on).
	bool keyPressed(const juce::KeyPress &k) override
	{
		const auto c = k.getTextCharacter();
		if ((c == '[' || c == ']') && !m_groups.empty()) {
			const int n = int(m_groups.size()), g = std::max(0, groupOf(m_shown));
			showGroup((g + (c == '[' ? -1 : 1) + n) % n);
			return true;
		}
		if (k.getModifiers().isShiftDown() && (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey)) {
			if (const int g = groupOf(m_shown); g >= 0) {
				const auto &sets = m_groups[size_t(g)].sets;
				const int at = int(std::find(sets.begin(), sets.end(), m_shown) - sets.begin());
				const int next = at + (k.getKeyCode() == juce::KeyPress::upKey ? -1 : 1);
				if (next >= 0 && next < int(sets.size()))
					show(sets[size_t(next)]);
			}
			return true;
		}
		return false;
	}

	void paint(juce::Graphics &g) override
	{
		g.fillAll(colours::background);
		paintNav(g);
		paintTabs(g);
	}

	void resized() override
	{
		const auto pane = getLocalBounds().withTrimmedLeft(kNav).withTrimmedTop(kTabs);
		m_blank->setBounds(pane);
		for (auto &t : m_tests)
			if (t->pane != nullptr)
				t->pane->setBounds(pane);
		clampScroll();
	}

	void mouseDown(const juce::MouseEvent &e) override
	{
		if (const int tab = tabAt(e.getPosition()); tab >= 0) {
			const auto folder = m_tests[size_t(tab)]->folder;
			if (!e.mods.isPopupMenu()) {
				show(tab);
				return;
			}
			juce::PopupMenu menu;
			menu.addItem(1, "Reload", m_tests[size_t(tab)]->pane != nullptr);
			menu.addItem(2, "Remove");
			menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(tabBounds()[size_t(tabIndex(tab))])),
			                   [this, folder](int r) {
				                   if (r == 1)
					                   reload(folder);
				                   else if (r == 2)
					                   remove(folder);
			                   });
			return;
		}
		const int gi = rowAt(e.getPosition());
		if (gi < 0)
			return;
		const auto name = m_groups[size_t(gi)].name;
		if (!e.mods.isPopupMenu()) {
			if (closeBounds(gi).contains(e.getPosition()))
				removeMachines([name](const Group &g) { return g.name == name; }, {});
			else
				showGroup(gi);
			return;
		}
		juce::PopupMenu menu;
		menu.addItem(1, "Reload");
		menu.addItem(2, "Remove");
		menu.addItem(3, "Remove the ones below", gi + 1 < int(m_groups.size()));
		menu.addItem(4, "Remove all others", m_groups.size() > 1);
		menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(rowBounds(gi))),
		                   [this, name](int r) {
			                   const int at = groupNamed(name); // the list may have changed meanwhile
			                   if (at < 0)
				                   return;
			                   if (r == 1)
				                   reload(m_tests[size_t(remembered(at))]->folder);
			                   else if (r == 2)
				                   removeMachines([name](const Group &g) { return g.name == name; }, {});
			                   else if (r == 3) {
				                   const auto below = groupTime(at);
				                   removeMachines([below, name](const Group &g) { return g.name != name && g.updated <= below; }, name);
			                   } else if (r == 4)
				                   removeMachines([name](const Group &g) { return g.name != name; }, name);
		                   });
	}

	void mouseMove(const juce::MouseEvent &e) override
	{
		const int gi = rowAt(e.getPosition());
		const bool close = gi >= 0 && closeBounds(gi).contains(e.getPosition());
		if (gi != m_hover || close != m_hoverClose) {
			m_hover = gi;
			m_hoverClose = close;
			repaint(getLocalBounds().removeFromLeft(kNav));
		}
	}
	void mouseExit(const juce::MouseEvent &) override
	{
		m_hover = -1;
		m_hoverClose = false;
		repaint(getLocalBounds().removeFromLeft(kNav));
	}

	// Over the list (a pane passes on the wheel it doesn't use, so check where)
	void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &wheel) override
	{
		if (e.x >= kNav)
			return;
		m_scroll -= juce::roundToInt(wheel.deltaY * 224.0f); // as juce::Viewport's step
		clampScroll();
		mouseMove(e);
		repaint(getLocalBounds().removeFromLeft(kNav));
	}

private:
	struct Test {
		juce::File folder, pending; // pending: the scene to load when it is first shown
		std::unique_ptr<Main> pane;
		juce::StringArray machines;
		juce::String where; // the folder's last three parts
		int scenes = 0;
		juce::Time touched, changedOn, shownAt; // a session opened it; its newest file; last shown here
		juce::Time updated() const { return std::max(touched, changedOn); }
	};
	// A machine: its sets (positions in m_tests, newest first), its time and how many scenes.
	struct Group {
		juce::String name;
		std::vector<int> sets;
		juce::Time updated;
		int scenes = 0;
	};

	static juce::String nameOf(const Test &t)
	{
		return t.machines.isEmpty() ? t.where.upToFirstOccurrenceOf("/", false, false) : t.machines.joinIntoString(", ");
	}
	static juce::String when(juce::Time t)
	{
		return t.toMilliseconds() <= 0 ? juce::String() : juce::String(t.getDayOfMonth()) + " " + t.getMonthName(true) + " " + t.formatted("%H:%M");
	}
	// The set's name on its tab: its folder's, or with the folder above when another set of the
	// machine has the same.
	juce::String label(int i) const
	{
		const auto &t = *m_tests[size_t(i)];
		const auto name = t.folder.getFileName();
		for (size_t k = 0; k < m_tests.size(); ++k)
			if (int(k) != i && m_tests[k]->folder.getFileName() == name && nameOf(*m_tests[k]) == nameOf(t))
				return t.folder.getParentDirectory().getFileName() + "/" + name;
		return name;
	}

	// The newest of the set's files (not the hidden ones: Finder's .DS_Store).
	static void scan(Test &t)
	{
		juce::Time newest;
		int seen = 0;
		for (const auto &entry : juce::RangedDirectoryIterator(t.folder, true, "*", juce::File::findFiles)) {
			if (++seen > 5000)
				break;
			if (!entry.getFile().getFileName().startsWithChar('.'))
				newest = std::max(newest, entry.getModificationTime());
		}
		t.changedOn = newest;
	}

	void regroup()
	{
		std::map<juce::String, Group> by;
		for (size_t i = 0; i < m_tests.size(); ++i) {
			const auto &t = *m_tests[i];
			auto &g = by[nameOf(t)];
			g.name = nameOf(t);
			g.sets.push_back(int(i));
			g.updated = std::max(g.updated, t.updated());
			g.scenes += t.scenes;
		}
		m_groups.clear();
		for (auto &entry : by) {
			auto &g = entry.second;
			std::stable_sort(g.sets.begin(), g.sets.end(), [this](int a, int b) { return m_tests[size_t(b)]->updated() < m_tests[size_t(a)]->updated(); });
			m_groups.push_back(std::move(g));
		}
		std::stable_sort(m_groups.begin(), m_groups.end(), [](const Group &a, const Group &b) { return b.updated < a.updated; });
		clampScroll();
		repaint();
	}
	int groupOf(int test) const
	{
		for (size_t g = 0; g < m_groups.size(); ++g)
			if (std::find(m_groups[g].sets.begin(), m_groups[g].sets.end(), test) != m_groups[g].sets.end())
				return int(g);
		return -1;
	}
	int groupNamed(const juce::String &name) const
	{
		for (size_t g = 0; g < m_groups.size(); ++g)
			if (m_groups[g].name == name)
				return int(g);
		return -1;
	}
	juce::Time groupTime(int g) const { return m_groups[size_t(g)].updated; }
	// The machine's set to show: the one shown here last, else its newest.
	int remembered(int g) const
	{
		const auto &sets = m_groups[size_t(g)].sets;
		int best = sets.front();
		for (int i : sets)
			if (m_tests[size_t(best)]->shownAt < m_tests[size_t(i)]->shownAt)
				best = i;
		return best;
	}
	void showGroup(int g)
	{
		if (g >= 0 && g < int(m_groups.size()))
			show(remembered(g));
	}

	// Takes off the sets of the machines `drop` picks; shows the set shown before if it stays, else
	// machine `keep`'s.
	void removeMachines(const std::function<bool(const Group &)> &drop, const juce::String &keep)
	{
		std::set<juce::String> names;
		for (const auto &g : m_groups)
			if (drop(g))
				names.insert(g.name);
		const int k = groupNamed(keep);
		removeIf([this, names](int i) { return names.count(nameOf(*m_tests[size_t(i)])) > 0; }, k >= 0 ? remembered(k) : -1);
	}
	// Takes off the sets `drop` picks (by position); shows the one shown before if it stays, else
	// set `fallback` (a position before the removal, kept), else the newest machine's.
	void removeIf(const std::function<bool(int)> &drop, int fallback)
	{
		const Test *was = m_shown >= 0 ? m_tests[size_t(m_shown)].get() : nullptr;
		const Test *alt = fallback >= 0 && fallback < int(m_tests.size()) ? m_tests[size_t(fallback)].get() : nullptr;
		std::vector<std::unique_ptr<Test>> kept;
		for (size_t i = 0; i < m_tests.size(); ++i)
			if (!drop(int(i)))
				kept.push_back(std::move(m_tests[i]));
		m_tests = std::move(kept); // the dropped ones' panes go with them
		m_shown = -1;
		m_hover = -1;
		regroup();
		int next = -1;
		for (size_t i = 0; i < m_tests.size(); ++i)
			if (m_tests[i].get() == was || (m_tests[i].get() == alt && next < 0))
				next = int(i);
		if (next < 0 && !m_groups.empty())
			next = remembered(0);
		if (next >= 0)
			show(next);
		else
			showBlank();
		if (changed)
			changed();
	}

	// ------------------------------------------------------------------ the machines
	juce::Rectangle<int> listArea() const { return {0, kHeader, kNav - 1, std::max(0, getHeight() - kHeader - kFoot)}; }
	juce::Rectangle<int> rowBounds(int g) const { return {0, kHeader + g * kRow - m_scroll, kNav - 1, kRow}; }
	juce::Rectangle<int> closeBounds(int g) const { return rowBounds(g).reduced(16, 8).removeFromRight(18).withSizeKeepingCentre(18, 18); }
	int rowAt(juce::Point<int> p) const
	{
		if (!listArea().contains(p))
			return -1;
		const int g = (p.y - kHeader + m_scroll) / kRow;
		return g < int(m_groups.size()) ? g : -1;
	}
	void clampScroll() { m_scroll = std::clamp(m_scroll, 0, std::max(0, int(m_groups.size()) * kRow - listArea().getHeight())); }
	void scrollTo(int g)
	{
		if (g < 0)
			return;
		const auto list = listArea();
		const auto row = rowBounds(g);
		if (row.getY() < list.getY())
			m_scroll -= list.getY() - row.getY();
		else if (row.getBottom() > list.getBottom())
			m_scroll += row.getBottom() - list.getBottom();
		clampScroll();
	}

	void paintNav(juce::Graphics &g)
	{
		const auto nav = getLocalBounds().removeFromLeft(kNav).toFloat();
		g.setColour(colours::panel);
		g.fillRect(nav);
		g.setColour(colours::edge);
		g.fillRect(nav.withX(nav.getRight() - 1).withWidth(1));
		g.setColour(colours::dim);
		g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
		g.drawText("MACHINES", juce::Rectangle<float>(16, 0, kNav - 32, kHeader), juce::Justification::centredLeft);
		const int shown = groupOf(m_shown);
		g.saveState();
		g.reduceClipRegion(listArea());
		for (size_t gi = 0; gi < m_groups.size(); ++gi) {
			const auto &grp = m_groups[gi];
			const auto row = rowBounds(int(gi)).toFloat();
			if (!row.intersects(listArea().toFloat()))
				continue;
			bool playing = false;
			for (int i : grp.sets)
				playing = playing || (m_tests[size_t(i)]->pane != nullptr && m_tests[size_t(i)]->pane->isPlaying());
			const bool on = int(gi) == shown, hover = int(gi) == m_hover;
			if (on) {
				g.setColour(colours::accentDim);
				g.fillRect(row);
				g.setColour(colours::accent);
				g.fillRect(row.withWidth(3));
			}
			const auto text = row.reduced(16, 8);
			g.setColour(on ? colours::accent : colours::text);
			g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
			g.drawText(grp.name, text.withHeight(20).withTrimmedRight(hover ? 22 : 14), juce::Justification::centredLeft, true);
			juce::String line = when(grp.updated);
			if (grp.sets.size() > 1)
				line << (line.isEmpty() ? "" : " · ") << int(grp.sets.size()) << " sets";
			line << (line.isEmpty() ? "" : " · ") << grp.scenes << (grp.scenes == 1 ? " scene" : " scenes");
			g.setColour(colours::dim);
			g.setFont(juce::FontOptions(12.0f));
			g.drawText(line, text.withTrimmedTop(22).withHeight(18).withTrimmedRight(hover ? 22 : 0), juce::Justification::centredLeft, true);
			if (hover) {
				const auto x = closeBounds(int(gi)).toFloat();
				g.setColour(m_hoverClose ? colours::text : colours::dim);
				g.drawLine(x.getX() + 4, x.getY() + 4, x.getRight() - 4, x.getBottom() - 4, 1.5f);
				g.drawLine(x.getRight() - 4, x.getY() + 4, x.getX() + 4, x.getBottom() - 4, 1.5f);
			} else if (playing) {
				g.setColour(colours::accent);
				g.fillEllipse(text.getRight() - 8, text.getY() + 6, 8, 8);
			}
		}
		g.restoreState();
		const auto list = listArea();
		const int content = int(m_groups.size()) * kRow;
		if (content > list.getHeight()) { // where the list is scrolled to
			const float h = std::max(24.0f, float(list.getHeight()) * float(list.getHeight()) / float(content));
			const float y = float(list.getY()) + float(m_scroll) / float(content - list.getHeight()) * (float(list.getHeight()) - h);
			g.setColour(colours::edge);
			g.fillRoundedRectangle(float(kNav - 6), y, 3.0f, h, 1.5f);
		}
		g.setColour(colours::dim);
		g.setFont(juce::FontOptions(12.0f));
		g.drawFittedText(m_tests.empty() ? juce::String("Open a scene, or drop one here")
		                                 : juce::String(juce::CharPointer_UTF8("Newest first. [ ]: machine. \xc3\x97 removes one; right-click for more.")),
		                 juce::Rectangle<int>(16, getHeight() - 56, kNav - 32, 44), juce::Justification::bottomLeft, 3);
	}

	// ------------------------------------------------------------------ the sets, as tabs
	static juce::Font nameFont() { return juce::Font(juce::FontOptions(14.0f, juce::Font::bold)); }
	static juce::Font countFont() { return juce::Font(juce::FontOptions(14.0f)); }
	static juce::Font dateFont() { return juce::Font(juce::FontOptions(11.0f)); }
	static juce::Font machineFont() { return juce::Font(juce::FontOptions(19.0f, juce::Font::bold)); }
	// The tabs of the shown machine's sets, in its order (newest first).
	std::vector<juce::Rectangle<int>> tabBounds() const
	{
		std::vector<juce::Rectangle<int>> r;
		const int gi = groupOf(m_shown);
		if (gi < 0)
			return r;
		const auto &grp = m_groups[size_t(gi)];
		float x = float(kNav + 20) + juce::GlyphArrangement::getStringWidth(machineFont(), grp.name) + 22;
		for (int i : grp.sets) {
			const auto &t = *m_tests[size_t(i)];
			const float w = std::max(juce::GlyphArrangement::getStringWidth(nameFont(), label(i)) +
			                             juce::GlyphArrangement::getStringWidth(countFont(), "  " + juce::String(t.scenes)),
			                         juce::GlyphArrangement::getStringWidth(dateFont(), when(t.updated()))) + 32;
			r.emplace_back(int(x), 8, int(std::ceil(w)), kTabs - 9);
			x += w + 4;
		}
		return r;
	}
	int tabIndex(int test) const
	{
		const int gi = groupOf(test);
		if (gi < 0)
			return -1;
		const auto &sets = m_groups[size_t(gi)].sets;
		return int(std::find(sets.begin(), sets.end(), test) - sets.begin());
	}
	int tabAt(juce::Point<int> p) const
	{
		const int gi = groupOf(m_shown);
		if (gi < 0 || p.y >= kTabs)
			return -1;
		const auto tabs = tabBounds();
		for (size_t k = 0; k < tabs.size(); ++k)
			if (tabs[k].contains(p))
				return m_groups[size_t(gi)].sets[k];
		return -1;
	}

	void paintTabs(juce::Graphics &g)
	{
		const auto strip = juce::Rectangle<int>(kNav, 0, getWidth() - kNav, kTabs);
		g.setColour(colours::edge);
		g.fillRect(strip.withTop(kTabs - 1).withHeight(1));
		const int gi = groupOf(m_shown);
		if (gi < 0)
			return;
		g.saveState();
		g.reduceClipRegion(strip);
		g.setColour(colours::accent);
		g.setFont(machineFont());
		g.drawText(m_groups[size_t(gi)].name, juce::Rectangle<int>(kNav + 20, 8, getWidth(), kTabs - 9), juce::Justification::centredLeft, false);
		const auto tabs = tabBounds();
		const auto &sets = m_groups[size_t(gi)].sets;
		for (size_t k = 0; k < tabs.size(); ++k) {
			const int i = sets[k];
			const auto &t = *m_tests[size_t(i)];
			const auto r = tabs[k];
			const bool on = i == m_shown;
			if (on) {
				g.setColour(colours::accentDim);
				g.fillRoundedRectangle(r.toFloat().withTrimmedBottom(-6), 6); // square at the bottom: the strip's edge cuts it
				g.setColour(colours::accent);
				g.fillRect(r.withTop(r.getBottom() - 3));
			}
			auto text = r.reduced(16, 9);
			auto top = text.removeFromTop(18);
			const auto name = label(i);
			const int nw = int(std::ceil(juce::GlyphArrangement::getStringWidth(nameFont(), name)));
			g.setFont(nameFont());
			g.setColour(on ? colours::accent : colours::text);
			g.drawText(name, top.removeFromLeft(nw), juce::Justification::centredLeft, false);
			g.setFont(countFont());
			g.setColour(colours::dim);
			g.drawText("  " + juce::String(t.scenes), top, juce::Justification::centredLeft, false);
			g.setFont(dateFont());
			g.drawText(when(t.updated()), text.withTrimmedTop(2), juce::Justification::topLeft, false);
		}
		g.restoreState();
	}

	// ------------------------------------------------------------------ sets
	// The set on a scene's folder, or on a folder.
	int find(const juce::File &path) const
	{
		const auto folder = path.isDirectory() ? path : path.getParentDirectory();
		for (size_t i = 0; i < m_tests.size(); ++i)
			if (m_tests[i]->folder == folder)
				return int(i);
		return -1;
	}

	// The set's machine: the machines its scene tests, else the first that another scene in its
	// folder names (a scene of recordings only names none).
	void describe(Test &t, const juce::File &scene)
	{
		juce::String error;
		const auto templates = Templates::load(Templates::standardFiles(), error);
		t.machines = machinesOf(scene, templates);
		for (const auto &other : t.folder.findChildFiles(juce::File::findFiles, false, "*.json"))
			if (t.machines.isEmpty())
				t.machines = machinesOf(other, templates);
		const auto scenes = scenesIn(t.folder);
		SceneIds::get().give(scenes); // a set's numbers run on, given when it first comes
		t.scenes = scenes.size();
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

	// Shows set i (its pane made and its pending scene loaded the first time; true if it loaded
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
		t.shownAt = juce::Time::getCurrentTime();
		scrollTo(groupOf(i));
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
				regroup();
				scrollTo(groupOf(m_shown));
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

	// The playing dots; every 5 s, the sets' files again (another session's new scene or render).
	void timerCallback() override
	{
		if (++m_ticks % 25 == 0) {
			bool moved = false;
			for (auto &t : m_tests) {
				const auto before = t->changedOn;
				scan(*t);
				moved = moved || t->changedOn != before;
				if (const int n = t->folder.getNumberOfChildFiles(juce::File::findFiles, "*.json"); n != t->scenes) {
					t->scenes = n; // a scene added or taken away: the list again
					SceneIds::get().give(scenesIn(t->folder));
					if (t->pane != nullptr)
						t->pane->relist();
					moved = true;
				}
			}
			if (moved)
				regroup();
		}
		repaint(getLocalBounds().removeFromLeft(kNav));
	}

	juce::AudioDeviceManager m_devices;
	juce::AudioPluginFormatManager m_formats;
	std::vector<std::unique_ptr<Test>> m_tests;
	std::vector<Group> m_groups;   // the machines, newest first
	std::unique_ptr<Main> m_blank; // shown with no sets: Open and drops
	int m_shown = -1;
	int m_scroll = 0;              // the machine list's, in pixels
	int m_hover = -1;              // the machine under the mouse (its × shows)
	bool m_hoverClose = false;
	int m_ticks = 0;
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
		// [SCENE.json ...] [--reload [PATH]] [--remove PATH] [--selftest OUT.wav [--part FROM TO] [--band LOW HIGH]] [--snapshot OUT.png]:
		// the snapshot writes the window (scene loaded, not playing; after a selftest, its spectrum)
		// and quits, for checks that mustn't make a sound. --which N prints scene N's file and quits.
		const auto args = juce::StringArray::fromTokens(commandLine, true);
		const auto cwd = juce::File::getCurrentWorkingDirectory();
		if (const int w = args.indexOf("--which"); w >= 0) {
			const auto f = w + 1 < args.size() ? SceneIds::get().fileOf(args[w + 1].getIntValue()) : juce::File();
			std::printf("%s\n", f == juce::File() ? "no such scene" : f.getFullPathName().toRawUTF8());
			std::fflush(stdout);
			juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
			return;
		}
		const bool headless = args.contains("--selftest") || args.contains("--snapshot");
		SceneIds::get().setSaving(!headless);
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
		// --part FROM TO (seconds), --band LOW HIGH (Hz): the selftest plays that part, that band
		if (const int p = args.indexOf("--part"); p >= 0 && p + 2 < args.size())
			main.loopPart(args[p + 1].getDoubleValue(), args[p + 2].getDoubleValue());
		if (const int b = args.indexOf("--band"); b >= 0 && b + 2 < args.size())
			main.hearBand(args[b + 1].getFloatValue(), args[b + 2].getFloatValue());
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
