// Headless checks of Lemus, the instruments host: its own here, and each machine's through it
// (machines/<id>/tests/host.cpp, registered with LEMUS_HOST_TEST).
//   lemustest_instrument                         run the checks
//   lemustest_instrument --snapshot <id>|picker <png> [about|addrom|prompt|service] [noroms|noart] [2x]   the editor in Lemus, as a PNG
//   lemustest_instrument --save-state <id> <file>   a session state with the machine loaded
#include "common.h"

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
}

// The prompt the first time a machine starts without an optional ROM, with the first machine that
// has one.
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
	RomFolder folder; // the answer is kept in memory, not in the user's settings
	lemus::setRomPromptAnswered(info->id, false);
	const auto voice = "Use " + info->roms.front().mode.toLowerCase();
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
	{
		LemusProcessor p;
		p.selectMachine(info->id);
		std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
		pump(100);
		check(findButton(*e, voice) != nullptr, "so when the ROM goes missing later, the prompt shows again");
	}
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI gui;
	if (argc >= 4 && juce::String(argv[1]) == "--snapshot") {
		juce::StringArray opts;
		for (int i = 4; i < argc; ++i)
			opts.add(argv[i]);
		return snapshot(argv[2], argv[3], opts.contains("about") ? "about" : opts.contains("addrom") ? "addrom" : opts.contains("prompt") ? "prompt" : opts.contains("service") ? "service" : "",
		                opts.contains("noroms"), opts.contains("noart"), opts.contains("2x") ? 2.0f : 1.0f);
	}
	if (argc == 4 && juce::String(argv[1]) == "--save-state")
		return saveState(argv[2], argv[3]);
	testHashes();
	testUpdates();
	testRomPrompt();
	runHostTests();
	finish();
	return g_failures == 0 ? 0 : 1;
}
