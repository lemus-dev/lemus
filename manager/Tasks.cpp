#include "Tasks.h"

#include <juce_cryptography/juce_cryptography.h>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace lemus::manager {

namespace {

const juce::String kDone = "lemus-done ";
const juce::String kFail = "lemus-fail ";
const juce::String kLog = "lemus-log";

bool sandbox()
{
	return root() != juce::File();
}

juce::String quoted(const juce::String &s)
{
	return "'" + s.replace("'", "'\\''") + "'";
}

juce::String quoted(const juce::File &f)
{
	return quoted(f.getFullPathName());
}

// "a", "a and b", "a, b and c"
juce::String listOf(const juce::StringArray &items)
{
	if (items.size() <= 1)
		return items.joinIntoString("");
	juce::StringArray first(items);
	first.removeRange(items.size() - 1, 1);
	return first.joinIntoString(", ") + " and " + items[items.size() - 1];
}

juce::String extensionOf(Format f)
{
	switch (f) {
	case Format::AU: return ".component";
	case Format::VST3: return ".vst3";
	case Format::App: return ".app";
	}
	return {};
}

juce::String componentOf(Format f)
{
	switch (f) {
	case Format::AU: return "au";
	case Format::VST3: return "vst3";
	case Format::App: return "app";
	}
	return {};
}

#if JUCE_WINDOWS
// Starts this app again with administrator rights to run the plan: 0 when it ran, -2 when the
// user said no, else its exit code (or -1 if it didn't start).
int runElevated(const juce::File &plan)
{
	const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();
	const auto params = "--apply \"" + plan.getFullPathName() + "\"";
	SHELLEXECUTEINFOW info{};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS;
	info.lpVerb = L"runas";
	info.lpFile = exe.toWideCharPointer();
	info.lpParameters = params.toWideCharPointer();
	info.nShow = SW_HIDE;
	if (!ShellExecuteExW(&info))
		return GetLastError() == ERROR_CANCELLED ? -2 : -1;
	WaitForSingleObject(info.hProcess, INFINITE);
	DWORD code = 1;
	GetExitCodeProcess(info.hProcess, &code);
	CloseHandle(info.hProcess);
	return int(code);
}
#endif

bool removeExisting(const juce::File &f)
{
	if (!f.exists())
		return true;
	if (f.isDirectory())
		f.deleteRecursively();
	else
		f.deleteFile();
	return !f.exists();
}

// Where a plan's steps for one plugin may write: its VST3, its app and the files beside it, its
// Start menu shortcut, and the two shared folders once they're empty; and where they may copy from:
// the plan's own folder. The elevated copy (--apply) checks every step against these, so a plan
// can touch nothing else, whoever wrote it.
struct Allowed {
	juce::File from, vst3, app, appFolder, link, startMenu, apps;

	Allowed(const juce::String &plugin, const juce::File &planFolder)
	{
		const auto p = places();
		from = planFolder;
		vst3 = bundleOf(p, Format::VST3, plugin);
		app = bundleOf(p, Format::App, plugin);
		apps = p.apps;
		// Windows: Lemus\<plugin> holds the app and its licence; macOS: the .app is the bundle.
		if (app.getParentDirectory() != p.apps && app.getParentDirectory().isAChildOf(p.apps))
			appFolder = app.getParentDirectory();
		if (p.startMenu != juce::File()) {
			startMenu = p.startMenu;
			link = p.startMenu.getChildFile(plugin + ".lnk");
		}
	}

	bool replaceable(const juce::File &f) const
	{
		if (f == vst3 || f == app)
			return true;
		for (const auto *text : {"LICENSE.txt", "NOTICES.txt", "README.txt"})
			if (appFolder != juce::File() && f == appFolder.getChildFile(text))
				return true;
		return false;
	}

	bool deletable(const juce::File &f) const
	{
		return f != juce::File() && (f == vst3 || f == app || f == appFolder || f == link);
	}
};

// One operation of a plan; empty if it went, else why not.
juce::String applyOp(const juce::var &op, const Allowed &allowed)
{
	const auto kind = op["op"].toString();
	const auto outside = [](const juce::File &f) { return "The plan asked for " + f.getFullPathName() + ", which isn't the plugin's, so nothing was done."; };
	if (kind == "replace") {
		const juce::File from(op["from"].toString()), to(op["to"].toString());
		if (!from.isAChildOf(allowed.from))
			return outside(from);
		if (!allowed.replaceable(to))
			return outside(to);
		if (!from.exists())
			return "The download had no " + from.getFileName() + ".";
		if (!removeExisting(to))
			return "Couldn't replace " + to.getFileName() + ": it may be open in a DAW. Close it and try again.";
		to.getParentDirectory().createDirectory();
		const bool ok = from.isDirectory() ? from.copyDirectoryTo(to) : from.copyFileTo(to);
		return ok ? juce::String() : "Couldn't copy " + to.getFileName() + " to " + to.getParentDirectory().getFullPathName() + ".";
	}
	if (kind == "delete") {
		const juce::File path(op["path"].toString());
		if (!allowed.deletable(path))
			return outside(path);
		return removeExisting(path) ? juce::String() : "Couldn't remove " + path.getFileName() + ": it may be open in a DAW.";
	}
	if (kind == "shortcut") {
		const juce::File target(op["target"].toString()), link(op["link"].toString());
		if (link != allowed.link || allowed.link == juce::File())
			return outside(link);
		if (target != allowed.app)
			return outside(target);
		link.getParentDirectory().createDirectory();
#if JUCE_WINDOWS
		if (!sandbox())
			return target.createShortcut(target.getFileNameWithoutExtension(), link) ? juce::String() : "Couldn't make the Start menu shortcut.";
#endif
		return link.replaceWithText(target.getFullPathName()) ? juce::String() : "Couldn't make the Start menu shortcut.";
	}
	if (kind == "deleteIfEmpty") {
		const juce::File path(op["path"].toString());
		if (path == juce::File() || (path != allowed.startMenu && path != allowed.apps))
			return outside(path);
		if (path.isDirectory() && path.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0)
			path.deleteFile();
		return {};
	}
	return "Unknown step " + kind + ".";
}

} // namespace

juce::String sha256Of(const juce::File &f)
{
	return juce::SHA256(f).toHexString().toLowerCase();
}

juce::String choicesOff(const juce::StringArray &choiceIds)
{
	juce::String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	                   "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
	                   "<plist version=\"1.0\">\n<array>\n";
	for (const auto &id : choiceIds)
		xml << "\t<dict>\n\t\t<key>attributeSetting</key>\n\t\t<integer>0</integer>\n"
		    << "\t\t<key>choiceAttribute</key>\n\t\t<string>selected</string>\n"
		    << "\t\t<key>choiceIdentifier</key>\n\t\t<string>" << juce::XmlElement::createTextElement(id)->getText() << "</string>\n\t</dict>\n";
	xml << "</array>\n</plist>\n";
	return xml;
}

juce::StringArray unwantedChoices(const juce::StringArray &choiceIds, const std::vector<Format> &wanted)
{
	juce::StringArray off;
	for (const auto &id : choiceIds)
		for (auto f : {Format::AU, Format::VST3, Format::App})
			if (id.endsWith("." + componentOf(f)) && std::find(wanted.begin(), wanted.end(), f) == wanted.end())
				off.add(id);
	return off;
}

juce::StringArray choiceIdsIn(const juce::String &showChoiceChangesXml)
{
	juce::StringArray ids;
	const auto xml = juce::parseXML(showChoiceChangesXml);
	if (xml == nullptr)
		return ids;
	for (auto *e : xml->getChildWithTagNameIterator("array"))
		for (auto *dict : e->getChildWithTagNameIterator("dict"))
			for (auto *k = dict->getFirstChildElement(); k != nullptr; k = k->getNextElement())
				if (k->hasTagName("key") && k->getAllSubText() == "choiceIdentifier" && k->getNextElement() != nullptr) {
					const auto id = k->getNextElement()->getAllSubText().trim();
					// Ids go into the script that runs as root: plain ones only.
					if (id != "default" && id.isNotEmpty() && id.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"))
						ids.addIfNotAlreadyThere(id);
				}
	return ids;
}

bool applyPlan(const juce::File &plan)
{
	const auto file = juce::JSON::parse(plan.loadFileAsString());
	const auto *jobs = file["jobs"].getArray();
	if (jobs == nullptr)
		return false;
	auto result = std::make_unique<juce::DynamicObject>();
	for (const auto &job : *jobs) {
		const auto plugin = job["plugin"].toString();
		if (!isPluginName(plugin))
			continue;
		const Allowed allowed(plugin, plan.getParentDirectory());
		juce::String error;
		if (const auto *ops = job["ops"].getArray())
			for (const auto &op : *ops)
				if (error.isEmpty())
					error = applyOp(op, allowed);
		result->setProperty(plugin, error);
	}
	return plan.withFileExtension("result").replaceWithText(juce::JSON::toString(juce::var(result.release())));
}

Batch::Batch(std::vector<Job> jobs, std::function<void()> changed)
	: Thread("Lemus Plugin Manager"), m_jobs(std::move(jobs)), m_changed(std::move(changed))
{
	m_work = juce::File::getSpecialLocation(juce::File::tempDirectory)
	             .getChildFile("lemus-plugin-manager")
	             .getChildFile(juce::String(juce::Time::currentTimeMillis()));
}

Batch::~Batch()
{
	stopThread(10000);
}

std::vector<Job> Batch::jobs() const
{
	const juce::ScopedLock l(m_lock);
	return m_jobs;
}

void Batch::set(size_t index, const std::function<void(Job &)> &change)
{
	{
		const juce::ScopedLock l(m_lock);
		change(m_jobs[index]);
	}
	if (m_changed)
		m_changed();
}

void Batch::run()
{
	m_work.createDirectory();
	for (size_t i = 0; i < m_jobs.size(); ++i) {
		if (jobs()[i].kind == Job::Kind::Remove) {
			set(i, [](Job &j) { j.state = Job::State::Ready; });
			continue;
		}
		if (m_stopping || threadShouldExit()) {
			set(i, [](Job &j) {
				j.state = Job::State::Stopped;
				j.error = "Stopped before it downloaded.";
			});
			continue;
		}
		download(i);
	}
	if (!threadShouldExit())
		installReady();
	bool failed = false;
	for (const auto &j : jobs())
		failed = failed || j.state == Job::State::Failed;
	if (!failed)
		m_work.deleteRecursively();
	m_done = true;
	if (m_changed)
		m_changed();
}

bool Batch::download(size_t index)
{
	const auto job = jobs()[index];
	const auto &d = job.entry.here();
	const bool local = sandbox() && d.url.startsWith("file://");
	if (!d.valid() && !local) {
		set(index, [](Job &j) {
			j.state = Job::State::Failed;
			j.error = "The list has no installer for this system.";
		});
		return false;
	}
	// Named for the plugin (a checked name), not from the URL.
#if JUCE_WINDOWS
	const auto target = m_work.getChildFile(job.entry.plugin + ".zip");
#else
	const auto target = m_work.getChildFile(job.entry.plugin + ".pkg");
#endif
	set(index, [](Job &j) {
		j.state = Job::State::Downloading;
		j.progress = 0;
	});
	bool got = false;
	if (local) {
		got = juce::URL(d.url).getLocalFile().copyFileTo(target);
	} else {
		auto stream = juce::URL(d.url).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
		                                                     .withConnectionTimeoutMs(15000)
		                                                     .withNumRedirectsToFollow(5)
		                                                     .withExtraHeaders(juce::String("User-Agent: ") + kUserAgent));
		target.deleteFile();
		juce::FileOutputStream out(target);
		if (stream != nullptr && out.openedOk()) {
			const auto total = d.size > 0 ? d.size : stream->getTotalLength();
			juce::HeapBlock<char> buffer(1 << 16);
			juce::int64 written = 0;
			double shown = 0;
			while (!stream->isExhausted() && !threadShouldExit()) {
				const auto n = stream->read(buffer.get(), 1 << 16);
				if (n <= 0)
					break;
				out.write(buffer.get(), size_t(n));
				written += n;
				const double p = total > 0 ? juce::jlimit(0.0, 1.0, double(written) / double(total)) : 0.0;
				if (p - shown >= 0.01) {
					shown = p;
					set(index, [p](Job &j) { j.progress = p; });
				}
			}
			out.flush();
			got = !threadShouldExit() && written > 0;
		}
	}
	if (!got) {
		target.deleteFile();
		set(index, [](Job &j) {
			j.state = Job::State::Failed;
			j.error = "The download didn't finish.";
		});
		return false;
	}
	set(index, [](Job &j) {
		j.state = Job::State::Checking;
		j.progress = 1;
	});
	const bool sizeOk = d.size <= 0 || target.getSize() == d.size;
	if (!sizeOk || sha256Of(target) != d.sha256) {
		target.deleteFile();
		set(index, [](Job &j) {
			j.state = Job::State::Failed;
			j.error = "The download didn't match its SHA-256, so it was thrown away. The installed version stays.";
		});
		return false;
	}
	set(index, [target](Job &j) {
		j.state = Job::State::Ready;
		j.file = target;
	});
	return true;
}

void Batch::installReady()
{
	std::vector<size_t> ready;
	juce::StringArray installs, updates, removes;
	for (size_t i = 0; i < m_jobs.size(); ++i) {
		const auto j = jobs()[i];
		if (j.state != Job::State::Ready)
			continue;
		ready.push_back(i);
		(j.kind == Job::Kind::Install ? installs : j.kind == Job::Kind::Update ? updates : removes).add(j.entry.plugin);
		set(i, [](Job &job) { job.state = Job::State::Installing; });
	}
	if (ready.empty())
		return;

	std::map<juce::String, juce::String> outcome; // plugin: "" done, else why not
	bool refused = false;
	const auto p = places();

#if JUCE_WINDOWS
	juce::Array<juce::var> planJobs;
	for (auto i : ready) {
		const auto j = jobs()[i];
		juce::Array<juce::var> ops;
		auto op = [&ops](std::initializer_list<std::pair<const char *, juce::String>> fields) {
			auto o = std::make_unique<juce::DynamicObject>();
			for (const auto &[k, v] : fields)
				o->setProperty(k, v);
			ops.add(juce::var(o.release()));
		};
		const auto vst3 = bundleOf(p, Format::VST3, j.entry.plugin), app = bundleOf(p, Format::App, j.entry.plugin);
		const auto link = p.startMenu.getChildFile(j.entry.plugin + ".lnk");
		if (j.kind == Job::Kind::Remove) {
			op({{"op", "delete"}, {"path", vst3.getFullPathName()}});
			op({{"op", "delete"}, {"path", app.getParentDirectory().getFullPathName()}});
			op({{"op", "delete"}, {"path", link.getFullPathName()}});
			op({{"op", "deleteIfEmpty"}, {"path", p.startMenu.getFullPathName()}});
			op({{"op", "deleteIfEmpty"}, {"path", p.apps.getFullPathName()}});
		} else {
			const auto unpacked = m_work.getChildFile(j.entry.plugin);
			unpacked.deleteRecursively();
			juce::ZipFile zip(j.file);
			if (zip.getNumEntries() == 0 || zip.uncompressTo(unpacked).failed()) {
				outcome[j.entry.plugin] = "The download didn't unpack.";
				continue;
			}
			auto found = unpacked.findChildFiles(juce::File::findDirectories, true, j.entry.plugin + ".vst3");
			if (found.isEmpty()) {
				outcome[j.entry.plugin] = "The download had no " + j.entry.plugin + ".vst3.";
				continue;
			}
			const auto inner = found[0].getParentDirectory();
			const auto wants = [&j](Format f) { return std::find(j.formats.begin(), j.formats.end(), f) != j.formats.end(); };
			if (wants(Format::VST3))
				op({{"op", "replace"}, {"from", found[0].getFullPathName()}, {"to", vst3.getFullPathName()}});
			const auto exe = inner.getChildFile(j.entry.plugin + ".exe");
			if (wants(Format::App) && exe.existsAsFile()) {
				op({{"op", "replace"}, {"from", exe.getFullPathName()}, {"to", app.getFullPathName()}});
				for (const auto *text : {"LICENSE.txt", "NOTICES.txt", "README.txt"})
					if (inner.getChildFile(text).existsAsFile())
						op({{"op", "replace"}, {"from", inner.getChildFile(text).getFullPathName()}, {"to", app.getSiblingFile(text).getFullPathName()}});
				op({{"op", "shortcut"}, {"target", app.getFullPathName()}, {"link", link.getFullPathName()}});
			} else if (!wants(Format::App)) {
				op({{"op", "delete"}, {"path", app.getParentDirectory().getFullPathName()}});
				op({{"op", "delete"}, {"path", link.getFullPathName()}});
			}
		}
		auto o = std::make_unique<juce::DynamicObject>();
		o->setProperty("plugin", j.entry.plugin);
		o->setProperty("ops", ops);
		planJobs.add(juce::var(o.release()));
	}
	auto planObject = std::make_unique<juce::DynamicObject>();
	planObject->setProperty("jobs", planJobs);
	const auto plan = m_work.getChildFile("plan.json");
	plan.replaceWithText(juce::JSON::toString(juce::var(planObject.release())));
	if (!planJobs.isEmpty()) {
		const int code = sandbox() ? (applyPlan(plan) ? 0 : 1) : runElevated(plan);
		refused = code == -2;
		const auto result = juce::JSON::parse(plan.withFileExtension("result").loadFileAsString());
		if (const auto *o = result.getDynamicObject())
			for (const auto &r : o->getProperties())
				outcome[r.name.toString()] = r.value.toString();
	}
#else
	// The script runs as root (osascript asks for the password) and is passed whole, never as a
	// file. It copies each download into a folder only it can write, checks the copy's SHA-256 again
	// and installs that copy, so nothing in the user's temporary folder, which any program running
	// as the user can change, is trusted after the check or written as root. The installer's log
	// comes back in the output after kLog, and the app saves it.
	const auto log = m_work.getChildFile("install.log");
	juce::String script = "# Lemus Plugin Manager\n"
	                      "d=$(/usr/bin/mktemp -d /tmp/lemus-plugin-manager.XXXXXX) || exit 1\n"
	                      "log=\"$d/install.log\"\n"
	                      "sha() { if [ -x /sbin/sha256 ]; then /sbin/sha256 -q \"$1\"; else /usr/bin/shasum -a 256 \"$1\" | /usr/bin/awk '{print $1}'; fi; }\n";
	// Copies the download to "$d/<plugin>.pkg" and checks it there.
	const auto fetch = [](const Job &j) {
		const auto pkg = "\"$d/" + j.entry.plugin + ".pkg\"";
		return "/bin/cp " + quoted(j.file) + " " + pkg + " >>\"$log\" 2>&1 && [ \"$(sha " + pkg + ")\" = " + quoted(j.entry.here().sha256) + " ]";
	};
	for (auto i : ready) {
		const auto j = jobs()[i];
		const auto &plugin = j.entry.plugin;
		const auto done = "echo " + quoted(kDone + plugin), fail = "echo " + quoted(kFail + plugin);
		if (j.kind == Job::Kind::Remove) {
			// Each copy's installer receipt is forgotten too, as though the plugin had never been
			// installed: the Lemus package installed at the copy's folder whose top-level item is
			// the copy. (pkgutil --file-info doesn't name a bundle's package.)
			juce::String files;
			script << "# remove " << plugin << "\n";
			for (const auto &c : j.before.copies) {
				files << " " << quoted(c.file);
				if (!sandbox())
					script << "for id in $(/usr/sbin/pkgutil --pkgs='org\\.lemusdev\\..*' 2>/dev/null); do\n"
					       << "  [ \"/$(/usr/sbin/pkgutil --pkg-info \"$id\" | /usr/bin/awk -F': ' '/^location:/ {print $2}')\" = "
					       << quoted(c.file.getParentDirectory()) << " ] && /usr/sbin/pkgutil --files \"$id\" | /usr/bin/grep -qx "
					       << quoted(c.file.getFileName()) << " && /usr/sbin/pkgutil --forget \"$id\" >/dev/null 2>&1\n"
					       << "done\n";
			}
			script << "if /bin/rm -rf" << files << "; then\n  " << done << "\nelse\n  " << fail << "\nfi\n";
			continue;
		}
		script << "# " << (j.kind == Job::Kind::Install ? "install " : "update ") << plugin << " " << j.entry.version << "\n";
		if (sandbox()) {
			// No installer under a test root: expand the .pkg and copy the bundles wanted.
			const auto expanded = "\"$d/" + plugin + ".expanded\"";
			script << "if " << fetch(j) << " && /usr/sbin/pkgutil --expand-full \"$d/" << plugin << ".pkg\" " << expanded << " >>\"$log\" 2>&1";
			for (auto f : j.formats) {
				const auto from = "\"$d/" + plugin + ".expanded/" + componentOf(f) + ".pkg/Payload/" + plugin + extensionOf(f) + "\"";
				const auto to = bundleOf(p, f, plugin);
				script << " && { [ ! -d " << from << " ] || { /bin/mkdir -p " << quoted(to.getParentDirectory()) << " && /bin/rm -rf " << quoted(to)
				       << " && /usr/bin/ditto " << from << " " << quoted(to) << "; }; }";
			}
			script << "; then\n  " << done << "\nelse\n  " << fail << "\nfi\n";
			continue;
		}
		juce::ChildProcess show;
		juce::StringArray ids;
		if (show.start(juce::StringArray{"/usr/sbin/installer", "-showChoiceChangesXML", "-pkg", j.file.getFullPathName()}))
			ids = choiceIdsIn(show.readAllProcessOutput());
		const auto off = unwantedChoices(ids, j.formats);
		const auto choices = "\"$d/" + plugin + ".choices.plist\"";
		if (!off.isEmpty())
			script << "/bin/cat >" << choices << " <<'LEMUS_END'\n" << choicesOff(off) << "LEMUS_END\n";
		script << "if " << fetch(j) << " && /usr/sbin/installer -pkg \"$d/" << plugin << ".pkg\" -target /";
		if (!off.isEmpty())
			script << " -applyChoiceChangesXML " << choices;
		script << " >>\"$log\" 2>&1; then\n  " << done << "\nelse\n  " << fail << "\nfi\n";
	}
	script << "echo " << quoted(kLog) << "\n/bin/cat \"$log\" 2>/dev/null\n/bin/rm -rf \"$d\"\n";

	juce::StringArray command;
	if (sandbox()) {
		command = {"/bin/sh", "-c", script};
	} else {
		juce::StringArray what;
		if (!installs.isEmpty())
			what.add("install " + listOf(installs));
		if (!updates.isEmpty())
			what.add("update " + listOf(updates));
		if (!removes.isEmpty())
			what.add("remove " + listOf(removes));
		const auto prompt = "Lemus Plugin Manager wants to " + listOf(what) + ".";
		// The script and the prompt go in as arguments, so neither is ever AppleScript source.
		command = {"/usr/bin/osascript", "-e", "on run argv", "-e",
		           "do shell script (item 1 of argv) with prompt (item 2 of argv) with administrator privileges", "-e", "end run", script, prompt};
	}
	juce::ChildProcess child;
	juce::String output;
	if (child.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
		output = child.readAllProcessOutput();
	const auto code = child.getExitCode();
	refused = !sandbox() && code != 0 && (output.contains("-128") || output.containsIgnoreCase("canceled") || output.containsIgnoreCase("cancelled"));
	const auto lines = juce::StringArray::fromLines(output.replace("\r\n", "\n").replace("\r", "\n"));
	const auto logAt = lines.indexOf(kLog.trimEnd());
	if (logAt >= 0) {
		juce::StringArray logLines(lines);
		logLines.removeRange(0, logAt + 1);
		log.replaceWithText(logLines.joinIntoString("\n"));
	}
	for (const auto &line : lines) {
		if (line == kLog.trimEnd())
			break;
		if (line.startsWith(kDone))
			outcome[line.fromFirstOccurrenceOf(kDone, false, false).trim()] = "";
		else if (line.startsWith(kFail))
			outcome[line.fromFirstOccurrenceOf(kFail, false, false).trim()] = "The installer stopped with an error. Its log is in " + log.getFullPathName() + ".";
	}
#endif

	for (auto i : ready) {
		const auto plugin = jobs()[i].entry.plugin;
		const auto it = outcome.find(plugin);
		set(i, [&](Job &j) {
			if (refused) {
				j.state = Job::State::Stopped;
#if JUCE_WINDOWS
				j.error = "Windows wasn't given permission, so nothing changed.";
#else
				j.error = "The password wasn't given, so nothing changed.";
#endif
			} else if (it == outcome.end()) {
				j.state = Job::State::Failed;
				j.error = "It didn't run.";
			} else if (it->second.isEmpty()) {
				j.state = Job::State::Done;
			} else {
				j.state = Job::State::Failed;
				j.error = it->second;
			}
		});
	}
}

} // namespace lemus::manager
