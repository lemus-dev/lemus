// Headless checks of Lemus, the instruments host: its own here, and each machine's through it
// (machines/<id>/tests/host.cpp, registered with LEMUS_HOST_TEST).
//   lemustest_instrument                         run the checks
//   lemustest_instrument --snapshot <id>|picker <png> [about|roms|prompt|service] [noroms|noart] [2x]   the editor in Lemus, as a PNG
//   lemustest_instrument --save-state <id> <file>   a session state with the machine loaded
#include "common.h"
#include "host/PanelFrame.h"

#include <algorithm>

namespace {

using namespace lemus;

void testHashes()
{
	std::printf("\nHashes\n");
	check(sha1Hex("abc", 3) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA1 of \"abc\"");
	check(crc32Hex("123456789", 9) == "cbf43926", "CRC32 check value");
}

void testUpdates()
{
	std::printf("\nUpdate check\n");
	check(updates::isNewer("0.2.0", "0.1.0") && updates::isNewer("0.10.0", "0.9.1") && updates::isNewer("1.0", "0.9.9"),
	      "a later version is newer, number by number");
	check(!updates::isNewer("0.1.0", "0.1.0") && !updates::isNewer("0.1", "0.1.0") && !updates::isNewer("0.9.1", "0.10.0") &&
	          !updates::isNewer("", "0.1.0"),
	      "the same, an earlier or no version isn't");

	const juce::String file = R"({"version": "9.0.0", "url": "https://lemusdev.org/download",
		"plugins": {"ro-mono-303": {"version": "0.2.0", "url": "https://lemusdev.org/ro-mono-303"}}})";
	const auto own = updates::releaseOf(file, "ro-mono-303");
	check(own.version == "0.2.0" && own.url == "https://lemusdev.org/ro-mono-303", "latest.json: a listed plugin reads its own entry");
	check(updates::releaseOf(file, "ro-drum-808").version == "9.0.0", "latest.json: an unlisted plugin reads the file's own");
	check(updates::releaseOf(R"({"plugins": {"ro-mono-303": {"version": "0.2.0"}}})", "ro-drum-808").version.isEmpty() &&
	          updates::releaseOf("not json", "ro-mono-303").version.isEmpty(),
	      "latest.json: nothing for an unlisted plugin without a file-level version, or a broken file");

	const juce::String notes = R"({"plugins": {"ro-mono-303": {"version": "0.3.0", "url": "https://lemusdev.org/ro-mono-303", "changes": [
		{"version": "0.4.0", "notes": ["not out yet"]}, {"version": "0.1.0", "notes": ["old"]},
		{"version": "0.3.0", "notes": ["three a", "three b"]}, {"version": "0.2.0", "notes": ["two"]}]}}})";
	const auto since = updates::releaseOf(notes, "ro-mono-303", "0.1.0");
	check(since.changes.size() == 2 && since.changes[0].version == "0.3.0" && since.changes[0].notes.size() == 2 && since.changes[1].version == "0.2.0",
	      "latest.json: the changes after this build's version, up to the release, newest first");
	check(updates::releaseOf(notes, "ro-mono-303").changes.size() == 3 && updates::releaseOf(file, "ro-mono-303").changes.empty(),
	      "latest.json: without a version, every change up to the release; a file without notes has none");

	const juce::int64 now = 1000LL * 24 * 60 * 60 * 1000, day = 24 * 60 * 60 * 1000;
	updates::Release r;
	r.version = "0.3.0";
	check(updates::popUpDue(r, {}, {}, now) && !updates::popUpDue(updates::Release{}, {}, {}, now), "what's new: due for a newer release, not without one");
	check(!updates::popUpDue(r, "0.3.0", {}, now) && updates::popUpDue(r, "0.2.0", {}, now), "what's new: not for a skipped version; a later one shows");
	check(!updates::popUpDue(r, {}, "0.3.0 " + juce::String(now - 6 * day), now) && updates::popUpDue(r, {}, "0.3.0 " + juce::String(now - 7 * day), now),
	      "what's new: shown, it comes back after a week");
	check(updates::popUpDue(r, {}, "0.2.0 " + juce::String(now - day), now), "what's new: a newer version shows at once, whatever the last one's answer");
}

// The prompt the first time a machine starts without an optional ROM, with the first machine that
// has one. Its required ROMs (if any) come from the private folder; without them it isn't checked.
void testRomPrompt()
{
	std::printf("\nFirst start without an optional ROM\n");
	const lemus::MachineInfo *info = nullptr;
	for (const auto &m : lemus::machines())
		if (info == nullptr && std::any_of(m.roms.begin(), m.roms.end(), [](const lemus::RomSpec &r) { return r.optional; }))
			info = &m;
	if (info == nullptr) {
		std::printf("      no machine here runs without an optional ROM: not checked\n");
		return;
	}
	const auto &optional = *std::find_if(info->roms.begin(), info->roms.end(), [](const lemus::RomSpec &r) { return r.optional; });
	RomFolder folder; // the answer is kept in memory, not in the user's settings
	auto addRequired = [&] {
		for (const auto &r : info->roms) {
			if (r.optional)
				continue;
			const auto data = privateRom(info->id, r.name);
			if (data.isEmpty()) {
				std::printf("      no %s in the private ROM folder: %s's prompt not checked\n", r.name.toRawUTF8(), info->name.toRawUTF8());
				return false;
			}
			folder.add(r.name, data);
		}
		return true;
	};
	if (!addRequired())
		return;
	lemus::setRomPromptAnswered(info->id, false);
	const auto voice = "Use " + optional.mode.toLowerCase();
	{
		LemusProcessor p;
		check(p.selectMachine(info->id), info->name + " loads without its optional ROM");
		std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
		pump(100);
		auto *use = findButton(*e, voice);
		check(use != nullptr && use->isVisible() && findButton(*e, "Locate ROM") != nullptr,
		      "the first time, the prompt shows over the panel: locate the ROM or \"" + voice + "\"");
		if (use != nullptr)
			use->triggerClick();
		pump(100);
		check(findButton(*e, voice) == nullptr && lemus::romPromptAnswered(info->id), "\"" + voice + "\" closes it and is remembered");
	}
	{
		LemusProcessor p;
		p.selectMachine(info->id);
		std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
		pump(100);
		check(findButton(*e, voice) == nullptr, "after that, the machine opens straight onto its panel");
	}
	for (const auto &r : info->roms) {
		const auto data = privateRom(info->id, r.name);
		if (data.isEmpty()) {
			std::printf("      no %s in the private ROM folder: a ROM that goes missing not checked\n", r.name.toRawUTF8());
			return;
		}
		folder.add(r.name, data);
	}
	{
		LemusProcessor p;
		p.selectMachine(info->id);
		check(p.romsWithout().empty() && !lemus::romPromptAnswered(info->id), "loaded with its ROM, the answer is forgotten");
	}
	folder.clear();
	addRequired();
	{
		LemusProcessor p;
		p.selectMachine(info->id);
		std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
		pump(100);
		check(findButton(*e, voice) != nullptr, "so when the ROM goes missing later, the prompt shows again");
	}
}

// Computer keys by place (host/KeyPlace.cpp): a panel's note and step keys sit where they do on a
// US keyboard whatever the layout, so on a German keyboard the key typing z is the US y.
const char *g_layout = nullptr;
char32_t inTestLayout(char32_t c)
{
	return panel::usKeyIn(g_layout, c);
}

void testKeyPlace()
{
	std::printf("\nComputer keys by place\n");
#if JUCE_MAC || JUCE_WINDOWS
#if JUCE_MAC
	const char *us = "com.apple.keylayout.US", *de = "com.apple.keylayout.German", *fr = "com.apple.keylayout.French";
#else
	const char *us = "00000409", *de = "00000407", *fr = "0000040C";
#endif
	panel::usKeyAt = nullptr;
	{
		LemusProcessor p;
		std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
	}
	check(panel::usKeyAt != nullptr, "the host's editor looks keys up in the keyboard layout");
	struct Case
	{
		const char *layout, *name;
		char32_t typed, at;
	};
	const Case cases[] = {
		{us, "US", U'a', U'a'},     {us, "US", U'y', U'y'},     {us, "US", U'z', U'z'},       {us, "US", U'1', U'1'},
		{us, "US", U'!', U'1'},     {us, "US", U';', U';'},     {us, "US", U',', U','},       {de, "German", U'z', U'y'},
		{de, "German", U'y', U'z'}, {de, "German", U'a', U'a'}, {de, "German", 0xf6, U';'},   {de, "German", 0xe4, U'\''},
		{de, "German", U'1', U'1'}, {de, "German", U',', U','}, {fr, "French", U'a', U'q'},   {fr, "French", U'q', U'a'},
		{fr, "French", U'z', U'w'}, {fr, "French", U'w', U'z'}, {fr, "French", U'm', U';'},   {fr, "French", U',', U'm'},
		{fr, "French", U'&', U'1'}, {fr, "French", U'1', U'1'}, {fr, "French", U';', U','},
	};
	for (const auto &c : cases) {
		const char32_t got = panel::usKeyIn(c.layout, c.typed);
		check(got == c.at, juce::String(c.name) + ": the key typing " + juce::String::charToString(juce::juce_wchar(c.typed)) + " is the US "
		                       + juce::String::charToString(juce::juce_wchar(c.at))
		                       + (got == c.at ? juce::String() : " (got " + juce::String::charToString(juce::juce_wchar(got)) + ")"));
	}
	// The panel side, as JUCE hands a key over (its code is the typed character, upper case).
	const auto saved = panel::usKeyAt;
	panel::usKeyAt = &inTestLayout;
	g_layout = de;
	check(panel::usKey(juce::KeyPress('Z', {}, 'z')) == 'y' && panel::usKey(juce::KeyPress('Y', juce::ModifierKeys::shiftModifier, 'Y')) == 'z',
	      "German: usKey gives the 303's G# for Z and DOWN for Y, shifted too");
	check(panel::usKey(juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 'z')) == 0, "a Command/Ctrl key is left to the host");
	check(panel::byPlace({"keys"}).size() == 3, "German: the key list says the keys go by place");
	{
		// Latching with keys by place, as the panels call panel::Keys: a right-click latches a key,
		// a computer key holds its key while down (nothing is down here, so keysChanged() is its
		// key-up), Escape lets go of all. The 303's map: the US Y is G# (8), the US Z is DOWN (13).
		const auto key303 = [](juce::juce_wchar us) { return us == 'y' ? 8 : us == 'z' ? 13 : -1; };
		panel::Keys keys;
		int sends = 0;
		keys.reset(24, [&](int, bool) { ++sends; });
		keys.mouseDown(8, juce::ModifierKeys::rightButtonModifier);
		const juce::KeyPress gSharp('Z', {}, 'z'), down('Y', {}, 'y');
		keys.keyDown(gSharp.getKeyCode(), key303(panel::usKey(gSharp)));
		keys.keysChanged();
		check(keys.down(8) && keys.latched(8), "German: G# latched by a right-click stays down when its computer key (Z) comes up");
		keys.keyDown(down.getKeyCode(), key303(panel::usKey(down)));
		const bool held = keys.down(13);
		keys.keysChanged();
		check(held && !keys.down(13) && keys.down(8), "German: DOWN (Y) holds while its key is down and lets go after; G# stays latched");
		keys.focusLost();
		check(keys.down(8), "losing focus leaves the latched key down");
		keys.mouseDown(8, {});
		check(!keys.down(8) && !keys.latched(8), "a click on the latched key lets it go");
		keys.mouseDown(8, juce::ModifierKeys::rightButtonModifier);
		keys.releaseAll(true);
		check(!keys.down(8) && !keys.latched(8) && sends == 6, "Escape lets go of a latched key; each change sent once");
	}
	g_layout = us;
	check(panel::byPlace({"keys"}).size() == 1, "US: the key list doesn't");
	panel::usKeyAt = saved;
#else
	check(panel::usKeyIn("", U'a') == 0, "no layout lookup on this platform: keys as typed");
#endif
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI gui;
	if (argc >= 4 && juce::String(argv[1]) == "--snapshot") {
		juce::StringArray opts;
		for (int i = 4; i < argc; ++i)
			opts.add(argv[i]);
		return snapshot(argv[2], argv[3], opts.contains("about") ? "about" : opts.contains("addrom") ? "addrom" : opts.contains("roms") ? "roms" : opts.contains("prompt") ? "prompt" : opts.contains("service") ? "service" : "",
		                opts.contains("noroms"), opts.contains("noart"), opts.contains("2x") ? 2.0f : 1.0f);
	}
	if (argc == 4 && juce::String(argv[1]) == "--save-state")
		return saveState(argv[2], argv[3]);
	testHashes();
	testUpdates();
	testRomPrompt();
	testKeyPlace();
	runHostTests();
	finish();
	return g_failures == 0 ? 0 : 1;
}
