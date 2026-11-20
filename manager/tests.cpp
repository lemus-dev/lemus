// The manager's checks, headless: the catalogue, what is installed, the installer's choices, the
// Windows plan, and a whole batch (download, check, install, remove) under a test root, from a
// release .pkg in dist/ when there is one.
#include "manager/Catalog.h"
#include "manager/Installed.h"
#include "manager/Tasks.h"

#include <cstdio>

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
	setRoot({});
	sandbox.deleteRecursively();
	std::printf(g_failures == 0 ? "all passed\n" : "%d failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
