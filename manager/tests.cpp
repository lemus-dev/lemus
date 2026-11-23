// The manager's checks, headless: the catalogue, what is installed, the installer's choices, the
// Windows plan, and a whole batch (download, check, install, remove) under a test root, from a
// release .pkg in dist/ when there is one.
#include "manager/Catalog.h"
#include "manager/Installed.h"
#include "manager/Report.h"
#include "manager/RomCheck.h"
#include "manager/Tasks.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstdio>

using namespace lemus;
using namespace lemus::manager;

namespace {

int g_failures = 0;

void check(bool ok, const juce::String &what)
{
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
	if (!ok)
		++g_failures;
}

const char *kCatalog = R"({"plugins": {
  "ro-mono-303": {"version": "0.2.5", "url": "https://lemusdev.org/ro-mono-303", "emulates": "Roland TB-303 Bass Line", "year": 1981, "type": "mono synth", "stage": "beta", "standalone": true,
    "changes": [{"version": "0.2.5", "notes": ["Five."]}, {"version": "0.2.4", "notes": ["Four."]}, {"version": "0.2.3", "notes": ["Three."]}],
    "downloads": {"mac": {"url": "https://github.com/x/ro-mono-303-0.2.5-mac.pkg", "sha256": "sha256:9C5AC1C551B6D003B4141341E85F9AF0D282251893B24BA52ED51AF70AA5E4B6", "size": 16292488},
                  "windows": {"url": "https://github.com/x/ro-mono-303-0.2.5-windows.zip", "sha256": "ceb92231eec61868d0c2d58adb578e16ba8ec15c4bdcd02db9db984f1b15d0cd", "size": 7000732}}},
  "ac-digital-vc": {"version": "0.1.1", "url": "https://lemusdev.org/ac-digital-vc",
    "emulates": "Access Virus C", "year": 2002, "type": "virtual analog synth", "changes": []},
  "pr-dist-r": {"version": "0.1.0", "url": "https://lemusdev.org/pr-dist-r", "emulates": "Pro Co RAT", "standalone": false},
  "lemus-plugin-manager": {"version": "0.1.1", "url": "https://lemusdev.org/plugin-manager"},
  "old-entry": {"url": "https://lemusdev.org/x"}
}})";

void testCatalog()
{
	const auto c = Catalog::parse(kCatalog);
	check(c.has_value(), "the catalogue reads");
	if (!c)
		return;
	check(c->plugins.size() == 3, "three plugins (the manager's own entry and one with no version left out)");
	check(c->plugins[0].plugin == "ac-digital-vc" && c->plugins[1].plugin == "pr-dist-r" && c->plugins[2].plugin == "ro-mono-303",
	      "sorted by the machine's name");
	check(c->manager.has_value() && c->manager->version == "0.1.1", "the manager's own release");
	const auto *tb = c->find("ro-mono-303");
	check(tb != nullptr && tb->year == 1981 && tb->stage == "beta", "an entry's fields");
	if (tb == nullptr)
		return;
	check(tb->mac.sha256 == "9c5ac1c551b6d003b4141341e85f9af0d282251893b24ba52ed51af70aa5e4b6" && tb->mac.valid(),
	      "a sha256 with GitHub's sha256: prefix and capitals reads as plain lower-case hex");
	check(tb->windows.size == 7000732 && tb->windows.valid(), "the Windows installer");
	check(!c->find("ac-digital-vc")->mac.valid(), "an entry with no installers has none valid");
	check(tb->shortName() == "Roland TB-303", "short name: up to the model");
	check(c->find("ac-digital-vc")->shortName() == "Access Virus C", "short name: a name with no number stays whole");
	check(tb->subtitle() == juce::String::fromUTF8("Mono synth \xc2\xb7 1981 \xc2\xb7 ro-mono-303"), "subtitle");
	check(!c->find("pr-dist-r")->standalone && tb->standalone, "standalone");
	const auto since = c->changesSince(*tb, "0.2.3");
	check(since.size() == 2 && since[0].version == "0.2.5" && since[1].version == "0.2.4", "changes since the installed version");
	check(c->changesSince(*tb, {}).size() == 3, "all changes for a plugin not installed");
	check(!Catalog::parse("{\"version\": \"0.1.0\"}").has_value() && !Catalog::parse("<html>").has_value(), "not a catalogue");

	check(isPluginName("ro-mono-303") && isPluginName("ac-digital-vc") && !isPluginName("..") && !isPluginName("a/b")
	          && !isPluginName("Ro-mono") && !isPluginName("-a") && !isPluginName("a--b") && !isPluginName("a\nb") && !isPluginName(""),
	      "plugin names: lower-case words joined by single hyphens, nothing else");
	check(isVersion("0.2.5") && isVersion("1.0.0-rc1") && !isVersion("0.2.5\nrm") && !isVersion("0.2 5") && !isVersion(""), "versions");
	const auto odd = Catalog::parse(R"({"plugins": {
	  "..": {"version": "0.1.0"}, "a/b": {"version": "0.1.0"}, "x\ny": {"version": "0.1.0"},
	  "ok-1": {"version": "0.1.0\n# oops"}, "ok-2": {"version": "0.1.0"}}})");
	check(odd && odd->plugins.size() == 1 && odd->plugins[0].plugin == "ok-2", "entries with an odd name or version are skipped");
}

void testVersions()
{
	const char *moduleInfo = "{\n  \"Name\": \"ro-mono-303\",\n  \"Version\": \"0.2.4\",\n  \"Factory Info\": {\n    \"Vendor\": \"Lemus\",\n  },\n}";
	check(moduleInfoVersion(moduleInfo) == "0.2.4", "a VST3's moduleinfo.json (JSON5, trailing commas)");
	const char *plist = R"(<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
  <key>CFBundleIdentifier</key><string>org.lemusdev.tb303</string>
  <key>CFBundleShortVersionString</key>
  <string>0.2.5</string>
</dict></plist>)";
	check(plistVersion(plist) == "0.2.5", "a bundle's Info.plist");
	check(plistVersion("not xml").isEmpty() && moduleInfoVersion("{}").isEmpty(), "no version");
}

void makeBundle(const juce::File &bundle, const juce::String &version)
{
	bundle.getChildFile("Contents").createDirectory();
	bundle.getChildFile("Contents/Info.plist")
	    .replaceWithText("<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>CFBundleShortVersionString</key><string>" + version
	                     + "</string></dict></plist>");
}

void testScan(const juce::File &sandbox)
{
	setRoot(sandbox);
	const auto p = places();
	check(p.vst3.isAChildOf(sandbox), "places are under the test root");
	check(!scan("ro-mono-303").installed(), "nothing installed in an empty root");
#if !JUCE_WINDOWS
	makeBundle(bundleOf(p, Format::AU, "ro-mono-303"), "0.2.4");
	makeBundle(bundleOf(p, Format::VST3, "ro-mono-303"), "0.2.5");
	makeBundle(p.userVst3.getChildFile("ro-mono-303.vst3"), "0.3.0");
	const auto in = scan("ro-mono-303");
	check(in.installed() && in.has(Format::AU) && in.has(Format::VST3) && !in.has(Format::App), "the formats installed");
	check(in.version() == "0.2.4", "the installed version is the oldest copy's");
	check(in.userCopies.size() == 1 && in.userCopies[0].version == "0.3.0", "a copy in the user's own folder is reported apart");
	check(formatsHere(true).size() == 3 && formatsHere(false).size() == 2, "formats on macOS");
#endif
	sandbox.deleteRecursively();
	sandbox.createDirectory();
}

void testChoices()
{
	const char *shown = R"(<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><array>
 <dict><key>attributeSetting</key><true/><key>choiceAttribute</key><string>visible</string><key>choiceIdentifier</key><string>default</string></dict>
 <dict><key>attributeSetting</key><false/><key>choiceAttribute</key><string>visible</string><key>choiceIdentifier</key><string>org.lemusdev.tb303.au</string></dict>
 <dict><key>attributeSetting</key><integer>1</integer><key>choiceAttribute</key><string>selected</string><key>choiceIdentifier</key><string>org.lemusdev.tb303.au</string></dict>
 <dict><key>attributeSetting</key><integer>1</integer><key>choiceAttribute</key><string>selected</string><key>choiceIdentifier</key><string>org.lemusdev.tb303.vst3</string></dict>
 <dict><key>attributeSetting</key><integer>1</integer><key>choiceAttribute</key><string>selected</string><key>choiceIdentifier</key><string>org.lemusdev.tb303.app</string></dict>
</array></plist>)";
	const auto ids = choiceIdsIn(shown);
	check(ids == juce::StringArray{"org.lemusdev.tb303.au", "org.lemusdev.tb303.vst3", "org.lemusdev.tb303.app"}, "the installer's choices");
	const auto off = unwantedChoices(ids, {Format::VST3});
	check(off == juce::StringArray{"org.lemusdev.tb303.au", "org.lemusdev.tb303.app"}, "the choices of formats not wanted");
	check(unwantedChoices(ids, {Format::AU, Format::VST3, Format::App}).isEmpty(), "every format wanted: no changes");
	const auto xml = juce::parseXML(choicesOff(off));
	check(xml != nullptr && xml->getChildByName("array") != nullptr && xml->getChildByName("array")->getNumChildElements() == 2,
	      "the choice changes file");
}

void testPlan(const juce::File &sandbox)
{
	setRoot(sandbox);
	const auto p = places();
	const auto work = sandbox.getChildFile("work");
	const auto from = work.getChildFile("ro-mono-303/ro-mono-303.vst3");
	from.getChildFile("Contents/x86_64-win").createDirectory();
	from.getChildFile("Contents/x86_64-win/ro-mono-303.vst3").replaceWithText("new");
	const auto to = bundleOf(p, Format::VST3, "ro-mono-303");
	to.getChildFile("Contents").createDirectory();
	to.getChildFile("Contents/old.txt").replaceWithText("old");
	const auto gone = bundleOf(p, Format::VST3, "ro-drums-909");
	gone.createDirectory();
	const auto keep = p.vst3.getChildFile("someone-else.vst3");
	keep.createDirectory();
	const auto json = [](const juce::File &f) { return f.getFullPathName().replace("\\", "\\\\"); };
	const auto plan = work.getChildFile("plan.json");
	plan.replaceWithText(juce::String(R"({"jobs": [
	  {"plugin": "ro-mono-303", "ops": [{"op": "replace", "from": ")") + json(from) + R"(", "to": ")" + json(to) + R"("}]},
	  {"plugin": "ro-drums-909", "ops": [{"op": "delete", "path": ")" + json(gone) + R"("}]},
	  {"plugin": "ro-drums-808", "ops": [{"op": "delete", "path": ")" + json(keep) + R"("}]},
	  {"plugin": "ro-mono-101", "ops": [{"op": "replace", "from": ")" + json(sandbox.getChildFile("elsewhere.vst3")) + R"(", "to": ")"
	                     + json(bundleOf(p, Format::VST3, "ro-mono-101")) + R"("}]},
	  {"plugin": "ac-digital-vc", "ops": [{"op": "deleteIfEmpty", "path": ")" + json(sandbox) + R"("}]},
	  {"plugin": "..", "ops": [{"op": "delete", "path": ")" + json(p.vst3) + R"("}]}]})");
	check(applyPlan(plan), "the plan runs");
	const auto result = juce::JSON::parse(plan.withFileExtension("result").loadFileAsString());
	check(result["ro-mono-303"].toString().isEmpty() && to.getChildFile("Contents/x86_64-win/ro-mono-303.vst3").loadFileAsString() == "new"
	          && !to.getChildFile("Contents/old.txt").exists(),
	      "replace: the old copy goes, the new one is in place");
	check(result["ro-drums-909"].toString().isEmpty() && !gone.exists(), "delete");
	check(result["ro-drums-808"].toString().contains("isn't the plugin's") && keep.exists(), "a plan can't delete another plugin's copy");
	check(result["ro-mono-101"].toString().contains("isn't the plugin's") && !bundleOf(p, Format::VST3, "ro-mono-101").exists(),
	      "a plan can't copy from outside its own folder");
	check(result["ac-digital-vc"].toString().contains("isn't the plugin's") && sandbox.exists(), "a plan can't empty other folders");
	check(!result.hasProperty("..") && p.vst3.exists(), "a job with an odd plugin name is skipped");
	sandbox.deleteRecursively();
	sandbox.createDirectory();
}

std::vector<Job> runBatch(std::vector<Job> jobs)
{
	Batch batch(std::move(jobs), {});
	batch.start();
	for (int i = 0; i < 1200 && !batch.done(); ++i)
		juce::Thread::sleep(100);
	return batch.jobs();
}

void testBatch(const juce::File &sandbox)
{
#if !JUCE_WINDOWS
	const auto pkg = juce::File(LEMUS_ROOT_DIR).getChildFile("dist/ro-mono-303-0.2.4-mac.pkg");
	if (!pkg.existsAsFile()) {
		std::printf("skip  a batch from a release .pkg (no %s)\n", pkg.getFullPathName().toRawUTF8());
		return;
	}
	setRoot(sandbox);
	Entry e;
	e.plugin = "ro-mono-303";
	e.version = "0.2.4";
	e.mac.url = juce::URL(pkg).toString(false);
	e.mac.sha256 = sha256Of(pkg);
	e.mac.size = pkg.getSize();

	Job wrong;
	wrong.kind = Job::Kind::Install;
	wrong.entry = e;
	wrong.entry.mac.sha256 = juce::String::repeatedString("0", 64);
	wrong.formats = {Format::AU, Format::VST3, Format::App};
	auto done = runBatch({wrong});
	check(done[0].state == Job::State::Failed && done[0].error.contains("SHA-256") && !scan("ro-mono-303").installed(),
	      "a download that doesn't match its SHA-256 isn't installed");

	Job install = wrong;
	install.entry = e;
	install.formats = {Format::VST3, Format::App};
	done = runBatch({install});
	auto in = scan("ro-mono-303");
	check(done[0].state == Job::State::Done, "install: done (" + done[0].error + ")");
	check(in.has(Format::VST3) && in.has(Format::App) && !in.has(Format::AU) && in.version() == "0.2.4",
	      "install: only the formats asked for, at the release's version");

	Job remove;
	remove.kind = Job::Kind::Remove;
	remove.entry = e;
	remove.before = in;
	done = runBatch({remove});
	check(done[0].state == Job::State::Done && !scan("ro-mono-303").installed(), "remove: every copy gone");
	sandbox.deleteRecursively();
	sandbox.createDirectory();
#endif
}

// Windows: a zip laid out as tools/release/windows.ps1 makes them, installed and removed.
void testZipBatch(const juce::File &sandbox)
{
#if JUCE_WINDOWS
	setRoot(sandbox);
	const auto zipFile = sandbox.getChildFile("ro-mono-303-0.2.5-windows.zip");
	{
		juce::ZipFile::Builder zip;
		const auto add = [&zip](const juce::String &path, const juce::String &text) {
			zip.addEntry(new juce::MemoryInputStream(text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 9, path, juce::Time::getCurrentTime());
		};
		add("ro-mono-303-0.2.5/ro-mono-303.vst3/Contents/Resources/moduleinfo.json", "{\"Name\": \"ro-mono-303\", \"Version\": \"0.2.5\",}");
		add("ro-mono-303-0.2.5/ro-mono-303.vst3/Contents/x86_64-win/ro-mono-303.vst3", "dll");
		add("ro-mono-303-0.2.5/ro-mono-303.exe", "exe");
		add("ro-mono-303-0.2.5/LICENSE.txt", "licence");
		add("ro-mono-303-0.2.5/NOTICES.txt", "notices");
		juce::FileOutputStream out(zipFile);
		zip.writeToStream(out, nullptr);
	}
	Entry e;
	e.plugin = "ro-mono-303";
	e.version = "0.2.5";
	e.windows.url = juce::URL(zipFile).toString(false);
	e.windows.sha256 = sha256Of(zipFile);
	e.windows.size = zipFile.getSize();
	Job install;
	install.kind = Job::Kind::Install;
	install.entry = e;
	install.formats = {Format::VST3, Format::App};
	auto done = runBatch({install});
	auto in = scan("ro-mono-303");
	const auto p = places();
	check(done[0].state == Job::State::Done, "zip install: done (" + done[0].error + ")");
	check(in.has(Format::VST3) && in.has(Format::App) && in.version() == "0.2.5", "zip install: the VST3 and the app in place");
	check(bundleOf(p, Format::App, "ro-mono-303").getSiblingFile("LICENSE.txt").existsAsFile()
	          && p.startMenu.getChildFile("ro-mono-303.lnk").existsAsFile(),
	      "zip install: the licence beside the app, a Start menu shortcut");
	Job remove;
	remove.kind = Job::Kind::Remove;
	remove.entry = e;
	remove.before = in;
	done = runBatch({remove});
	check(done[0].state == Job::State::Done && !scan("ro-mono-303").installed() && !p.startMenu.exists(), "zip remove: every copy gone");
	sandbox.deleteRecursively();
	sandbox.createDirectory();
#else
	juce::ignoreUnused(sandbox);
#endif
}

// The ROM and artwork checks, and files dropped on the window, under a test ROM folder.
void testRoms(const juce::File &sandbox)
{
	const auto roms = sandbox.getChildFile("ROMs");
	romfolder::setOverride(roms);
	const juce::String romA = "first rom bytes", romB = "second rom bytes";
	const auto sha = [](const juce::String &t) { return sha1Hex(t.toRawUTF8(), t.getNumBytesAsUTF8()); };
	const auto json = juce::String(R"({"plugins": {
	  "ro-test-1": {"machine": "test1", "roms": [
	    {"name": "cpu.ic1", "size": 15, "sha1": ")") + sha(romA) + R"(", "crc32": "00000000", "what": "CPU ROM", "optional": true, "without": "voices only"},
	    {"name": "voice.ic2", "size": 16, "sha1": ")" + sha(romB) + R"(", "crc32": "00000000", "what": "Voice ROM", "optional": false}],
	    "artwork": "test1.svg", "places": [{"id": "maker", "label": "Maker"}, {"id": "model", "label": "Model"}]},
	  "pr-test-2": {"machine": "test2", "roms": []},
	  "../bad": {"machine": "x", "roms": []},
	  "ro-test-3": {"machine": "../up", "roms": []},
	  "ro-test-4": {"machine": "test4", "roms": [{"name": "../escape", "size": 1, "sha1": ")" + sha(romA) + R"(", "crc32": "0"}]}
	}})";
	const auto needs = parseNeeds(json);
	check(needs && needs->size() == 3 && needs->count("ro-test-1") && needs->count("pr-test-2") && needs->count("ro-test-4"),
	      "roms.json: odd plugin names and machine ids are skipped");
	if (!needs)
		return;
	check(needs->at("ro-test-4").roms.empty(), "roms.json: a ROM name with a path in it is skipped");
	const auto &n = needs->at("ro-test-1");
	check(n.roms.size() == 2 && n.roms[0].optional && !n.roms[1].optional && n.artwork.places.size() == 2 && n.artwork.plugin == "ro-test-1",
	      "roms.json: ROMs and places");

	auto c = check(n);
	check(c.rom() == RomCheck::Rom::Missing && c.art() == RomCheck::Art::None, "an empty folder: a needed ROM missing, no artwork");
	check(check(needs->at("pr-test-2")).rom() == RomCheck::Rom::NotNeeded, "no ROMs: not needed");

	// Dropped: a loose ROM under another name, a zip with the other, artwork, and a stranger.
	const auto drop = sandbox.getChildFile("drop");
	drop.createDirectory();
	drop.getChildFile("whatever.bin").replaceWithText(romB);
	drop.getChildFile("ro-test-1-maker.svg").replaceWithText("<svg xmlns=\"http://www.w3.org/2000/svg\"/>");
	drop.getChildFile("notes.txt").replaceWithText("nothing");
	auto added = addDropped({drop.getChildFile("whatever.bin").getFullPathName(), drop.getChildFile("ro-test-1-maker.svg").getFullPathName(),
	                         drop.getChildFile("notes.txt").getFullPathName()},
	                        *needs);
	check(added.size() == 2 && roms.getChildFile("voice.ic2").loadFileAsString() == romB && roms.getChildFile("ro-test-1-maker.svg").existsAsFile(),
	      "dropped: the ROM by its SHA1 under its own name, the artwork by its name; the rest left alone");
	c = check(n);
	check(c.rom() == RomCheck::Rom::Reduced && c.art() == RomCheck::Art::Partial, "then: runs without the optional ROM, artwork partial");
	{
		juce::ZipFile::Builder zip;
		zip.addEntry(new juce::MemoryInputStream(romA.toRawUTF8(), romA.getNumBytesAsUTF8(), true), 9, "set/cpu.bin", juce::Time::getCurrentTime());
		juce::FileOutputStream out(drop.getChildFile("set.zip"));
		zip.writeToStream(out, nullptr);
	}
	added = addDropped({drop.getFullPathName()}, *needs);
	check(roms.getChildFile("cpu.ic1").loadFileAsString() == romA, "dropped: a folder, and a ROM inside a zip in it");
	check(check(n).rom() == RomCheck::Rom::Ready, "then: every ROM found");
	check(describe(addDropped({drop.getChildFile("whatever.bin").getFullPathName()}, *needs)).contains("already"), "dropping it again: it has it already");
	check(describe({}).startsWith("Nothing"), "dropping nothing of ours says so");
	check(describe({{"ro-test-1", "cpu.ic1", false}}) == "Added cpu.ic1 to ro-test-1", "what one file added says");

	// A ROM with an alternative (an earlier firmware): the alternative alone is enough, filed under
	// its own name, and the ROM's own image comes first when both are there.
	const juce::String fwNew = "firmware v2 image", fwOld = "firmware v1 image";
	const auto altNeeds = parseNeeds(juce::String(R"({"plugins": {"ro-test-5": {"machine": "test5", "roms": [
	    {"name": "fw-v2.bin", "size": 17, "sha1": ")") + sha(fwNew) + R"(", "crc32": "0", "what": "Firmware ROM", "optional": false,
	     "alternatives": [{"name": "fw-v1.bin", "size": 17, "sha1": ")" + sha(fwOld) + R"(", "crc32": "0", "label": "v1"},
	                      {"name": "../no", "size": 17, "sha1": ")" + sha(fwOld) + R"(", "crc32": "0", "label": "bad"}]}]}}})");
	check(altNeeds && altNeeds->count("ro-test-5") && altNeeds->at("ro-test-5").roms.size() == 1 &&
	          altNeeds->at("ro-test-5").roms[0].alternatives.size() == 1 && altNeeds->at("ro-test-5").roms[0].alternatives[0].label == "v1",
	      "roms.json: a ROM's alternatives; one with a path in its name is skipped");
	if (altNeeds) {
		const auto &n5 = altNeeds->at("ro-test-5");
		check(check(n5).rom() == RomCheck::Rom::Missing, "neither firmware: missing");
		drop.getChildFile("old.bin").replaceWithText(fwOld);
		addDropped({drop.getChildFile("old.bin").getFullPathName()}, *altNeeds);
		check(roms.getChildFile("fw-v1.bin").loadFileAsString() == fwOld, "dropped: the alternative under its own name");
		check(check(n5).rom() == RomCheck::Rom::Ready, "then: ready on the alternative");
		roms.getChildFile("fw-v2.bin").replaceWithText(fwNew);
		const auto set = romfolder::load(roms, n5.roms);
		check(set.find("fw-v2.bin") != nullptr && set.find("fw-v2.bin")->toString() == fwNew, "with both, the ROM's own image");
	}
	romfolder::setOverride({});
	sandbox.deleteRecursively();
	sandbox.createDirectory();
}

// The plugins' Open Plugin Manager: where the manager is, and the plugin a plugin asked for.
void testHandOff(const juce::File &sandbox)
{
	romfolder::setSettingsFile(sandbox.getChildFile("Lemus.settings"));
	check(updates::pluginManager() == juce::File(), "no manager recorded: none");
	const auto app = sandbox.getChildFile("Lemus Plugin Manager.app");
	app.createDirectory();
	updates::rememberPluginManager(app);
	check(updates::pluginManager() == app, "the manager records where it is");
	{
		auto s = settings();
		s->setValue("pluginManagerShow", "ro-drums-808 " + juce::String(juce::Time::currentTimeMillis()));
		s->saveIfNeeded();
	}
	check(updates::takePluginManagerRequest() == "ro-drums-808" && updates::takePluginManagerRequest().isEmpty(),
	      "a plugin's ask is read once");
	{
		auto s = settings();
		s->setValue("pluginManagerShow", "ro-drums-808 " + juce::String(juce::Time::currentTimeMillis() - 5 * 60 * 1000));
		s->saveIfNeeded();
	}
	check(updates::takePluginManagerRequest().isEmpty(), "an old ask is ignored");
	app.deleteRecursively();
	check(updates::pluginManager() == juce::File(), "a manager that's gone: none");
	romfolder::setSettingsFile({});
	sandbox.deleteRecursively();
	sandbox.createDirectory();
}

void testReports(const juce::File &sandbox)
{
	const auto log = sandbox.getChildFile("install.log");
	juce::String text;
	for (int i = 1; i <= 100; ++i)
		text << "line " << i << "\n";
	log.replaceWithText(text);
	Job j;
	j.kind = Job::Kind::Update;
	j.entry.plugin = "ro-drums-808";
	j.entry.version = "0.1.4";
	j.formats = {Format::VST3};
	j.state = Job::State::Failed;
	j.error = "The installer stopped with an error. Its log is in " + log.getFullPathName() + ".";
	const auto d = detailsOf(j);
	check(d.startsWith(aboutThisCopy()) && d.contains("Update ro-drums-808 0.1.4 (VST3): failed") && d.contains(j.error),
	      "details: the version, what it was doing and why");
	check(d.contains("line 100") && d.contains("line 21\n") && !d.contains("line 20\n"), "details: the installer's log, its last 80 lines");
	const auto crash = sandbox.getChildFile("crash.txt");
	setCrashFile(crash);
	writeCrashReport();
	check(crash.loadFileAsString().startsWith(aboutThisCopy()) && crash.loadFileAsString().contains("Where it stopped"), "a crash report");
	setCrashFile({});
	sandbox.deleteRecursively();
	sandbox.createDirectory();
}

SelfUpdate::State runSelfUpdate(const Entry &e, juce::String &error)
{
	SelfUpdate self(e, {});
	self.start();
	for (int i = 0; i < 1200 && !self.finished(); ++i)
		juce::Thread::sleep(100);
	error = self.error();
	return self.state();
}

// The manager updating itself under a test root: macOS from the release .pkg in dist/ (expanded
// into <root>/Applications), Windows from a zip laid out as manager-windows.ps1 makes it.
void testSelfUpdate(const juce::File &sandbox)
{
	setRoot(sandbox);
	Entry e;
	e.plugin = "lemus-plugin-manager";
	e.version = "0.2.0";
#if JUCE_WINDOWS
	const auto zipFile = sandbox.getChildFile("lemus-plugin-manager-0.2.0-windows.zip");
	{
		juce::ZipFile::Builder zip;
		const juce::String text = "new";
		zip.addEntry(new juce::MemoryInputStream(text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 9,
		             "lemus-plugin-manager-0.2.0/Lemus Plugin Manager.exe", juce::Time::getCurrentTime());
		juce::FileOutputStream out(zipFile);
		zip.writeToStream(out, nullptr);
	}
	e.windows.url = juce::URL(zipFile).toString(false);
	e.windows.sha256 = sha256Of(zipFile);
	e.windows.size = zipFile.getSize();
	const auto exe = sandbox.getChildFile("Lemus Plugin Manager.exe"), old = sandbox.getChildFile("Lemus Plugin Manager.exe.old");
	exe.replaceWithText("old");
	auto &d = e.windows;
#else
	const auto pkg = juce::File(LEMUS_ROOT_DIR).getChildFile("dist/lemus-plugin-manager-0.1.0-mac.pkg");
	if (!pkg.existsAsFile()) {
		std::printf("skip  the manager updating itself (no %s)\n", pkg.getFullPathName().toRawUTF8());
		return;
	}
	e.mac.url = juce::URL(pkg).toString(false);
	e.mac.sha256 = sha256Of(pkg);
	e.mac.size = pkg.getSize();
	auto &d = e.mac;
	const auto app = places().apps.getChildFile("Lemus Plugin Manager.app");
#endif
	const auto sha = d.sha256;
	d.sha256 = juce::String::repeatedString("0", 64);
	juce::String error;
	check(runSelfUpdate(e, error) == SelfUpdate::State::Failed && error.contains("SHA-256"), "self-update: a download that doesn't match isn't installed");
	d.sha256 = sha;
	const auto state = runSelfUpdate(e, error);
	check(state == SelfUpdate::State::Done, "self-update: done (" + error + ")");
#if JUCE_WINDOWS
	check(exe.loadFileAsString() == "new" && old.loadFileAsString() == "old", "self-update: the new exe in place, the old one beside it");
	removeOldSelf();
	check(!old.exists(), "self-update: the old exe removed on the next start");
#else
	check(versionOf(app, Format::App) == "0.1.0", "self-update: the new app in place");
#endif
	sandbox.deleteRecursively();
	sandbox.createDirectory();
}

} // namespace

int main()
{
	juce::ScopedJuceInitialiser_GUI juce;
	const auto sandbox = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("lemustest_manager");
	sandbox.deleteRecursively();
	sandbox.createDirectory();
	testCatalog();
	testVersions();
	testScan(sandbox);
	testChoices();
	testPlan(sandbox);
	testBatch(sandbox);
	testZipBatch(sandbox);
	testSelfUpdate(sandbox);
	testRoms(sandbox);
	testHandOff(sandbox);
	testReports(sandbox);
	setRoot({});
	sandbox.deleteRecursively();
	std::printf(g_failures == 0 ? "all passed\n" : "%d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
