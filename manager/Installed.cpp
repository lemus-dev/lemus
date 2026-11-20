#include "Installed.h"

#include "host/Updates.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace lemus::manager {

namespace {

juce::File g_root;

#if JUCE_WINDOWS
juce::String exeVersion(const juce::File &exe)
{
	const auto path = exe.getFullPathName().toWideCharPointer();
	DWORD handle = 0;
	const DWORD size = GetFileVersionInfoSizeW(path, &handle);
	if (size == 0)
		return {};
	juce::HeapBlock<char> data(size);
	if (!GetFileVersionInfoW(path, 0, size, data.get()))
		return {};
	VS_FIXEDFILEINFO *info = nullptr;
	UINT length = 0;
	if (!VerQueryValueW(data.get(), L"\\", reinterpret_cast<void **>(&info), &length) || info == nullptr)
		return {};
	return juce::String(HIWORD(info->dwProductVersionMS)) + "." + juce::String(LOWORD(info->dwProductVersionMS)) + "."
	       + juce::String(HIWORD(info->dwProductVersionLS));
}
#endif

} // namespace

juce::String nameOf(Format f)
{
	switch (f) {
	case Format::AU: return "AU";
	case Format::VST3: return "VST3";
	case Format::App: return "App";
	}
	return {};
}

std::vector<Format> formatsHere(bool standalone)
{
#if JUCE_WINDOWS
	std::vector<Format> f{Format::VST3};
#else
	std::vector<Format> f{Format::AU, Format::VST3};
#endif
	if (standalone)
		f.push_back(Format::App);
	return f;
}

juce::File Places::pluginFolder() const
{
#if JUCE_WINDOWS
	return vst3;
#else
	return vst3.getParentDirectory();
#endif
}

void setRoot(const juce::File &r)
{
	g_root = r;
}

juce::File root()
{
	return g_root;
}

Places places()
{
	Places p;
	const bool sandbox = g_root != juce::File();
#if JUCE_WINDOWS
	const auto programs = sandbox ? g_root.getChildFile("Program Files")
	                              : juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
	p.vst3 = programs.getChildFile("Common Files").getChildFile("VST3");
	p.apps = programs.getChildFile("Lemus");
	p.startMenu = (sandbox ? g_root.getChildFile("ProgramData")
	                       : juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory))
	                  .getChildFile("Microsoft/Windows/Start Menu/Programs/Lemus");
#else
	const auto system = sandbox ? g_root : juce::File("/");
	p.au = system.getChildFile("Library/Audio/Plug-Ins/Components");
	p.vst3 = system.getChildFile("Library/Audio/Plug-Ins/VST3");
	p.apps = system.getChildFile("Applications");
	const auto home = sandbox ? g_root.getChildFile("Users/me") : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
	p.userAu = home.getChildFile("Library/Audio/Plug-Ins/Components");
	p.userVst3 = home.getChildFile("Library/Audio/Plug-Ins/VST3");
#endif
	return p;
}

juce::File bundleOf(const Places &p, Format f, const juce::String &plugin)
{
	switch (f) {
	case Format::AU: return p.au.getChildFile(plugin + ".component");
	case Format::VST3: return p.vst3.getChildFile(plugin + ".vst3");
	case Format::App:
#if JUCE_WINDOWS
		return p.apps.getChildFile(plugin).getChildFile(plugin + ".exe");
#else
		return p.apps.getChildFile(plugin + ".app");
#endif
	}
	return {};
}

bool Install::has(Format f) const
{
	for (const auto &c : copies)
		if (c.format == f)
			return true;
	return false;
}

std::vector<Format> Install::formats() const
{
	std::vector<Format> f;
	for (const auto &c : copies)
		f.push_back(c.format);
	return f;
}

juce::String Install::version() const
{
	juce::String oldest;
	for (const auto &c : copies)
		if (c.version.isNotEmpty() && (oldest.isEmpty() || updates::isNewer(oldest, c.version)))
			oldest = c.version;
	return oldest;
}

Install scan(const juce::String &plugin)
{
	Install in;
	const auto p = places();
	for (auto f : {Format::AU, Format::VST3, Format::App}) {
#if JUCE_WINDOWS
		if (f == Format::AU)
			continue;
#endif
		const auto file = bundleOf(p, f, plugin);
		if (file.exists())
			in.copies.push_back({f, file, versionOf(file, f)});
	}
#if !JUCE_WINDOWS
	for (const auto &[f, folder] : {std::pair{Format::AU, p.userAu}, std::pair{Format::VST3, p.userVst3}}) {
		const auto file = folder.getChildFile(plugin + (f == Format::AU ? ".component" : ".vst3"));
		if (file.exists())
			in.userCopies.push_back({f, file, versionOf(file, f)});
	}
#endif
	return in;
}

juce::String moduleInfoVersion(const juce::String &text)
{
	const auto at = text.indexOf("\"Version\"");
	if (at < 0)
		return {};
	const auto rest = text.substring(at + 9).fromFirstOccurrenceOf(":", false, false).trimStart();
	if (!rest.startsWith("\""))
		return {};
	return rest.substring(1).upToFirstOccurrenceOf("\"", false, false).trim();
}

juce::String plistVersion(const juce::String &text)
{
	const auto xml = juce::parseXML(text);
	if (xml == nullptr)
		return {};
	const auto *dict = xml->getChildByName("dict");
	if (dict == nullptr)
		return {};
	for (auto *e = dict->getFirstChildElement(); e != nullptr; e = e->getNextElement())
		if (e->hasTagName("key") && e->getAllSubText() == "CFBundleShortVersionString") {
			const auto *value = e->getNextElement();
			return value != nullptr ? value->getAllSubText().trim() : juce::String();
		}
	return {};
}

juce::String versionOf(const juce::File &copy, Format f)
{
	const auto plist = copy.getChildFile("Contents/Info.plist");
	if (plist.existsAsFile())
		return plistVersion(plist.loadFileAsString());
	if (f == Format::VST3) {
		const auto info = copy.getChildFile("Contents/Resources/moduleinfo.json");
		if (info.existsAsFile())
			return moduleInfoVersion(info.loadFileAsString());
	}
#if JUCE_WINDOWS
	if (f == Format::App && copy.existsAsFile())
		return exeVersion(copy);
#endif
	return {};
}

} // namespace lemus::manager
