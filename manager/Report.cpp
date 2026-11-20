#include "Report.h"

#include <cstdio>
#include <string>

namespace lemus::manager {

namespace {

juce::File g_crashFile;

// Kept ready, so a crash only writes.
std::string g_crashPath, g_crashHeader;

juce::String formatsOf(const std::vector<Format> &formats)
{
	juce::StringArray names;
	for (auto f : formats)
		names.add(nameOf(f));
	return names.joinIntoString(", ");
}

void prepareCrashReport()
{
	g_crashPath = crashFile().getFullPathName().toStdString();
	g_crashHeader = (aboutThisCopy() + "\nIt quit unexpectedly. Started " + juce::Time::getCurrentTime().toString(true, true, false, true)
	                 + ".\n\nWhere it stopped:\n")
	                    .toStdString();
}

void onCrash(void *)
{
	writeCrashReport();
}

} // namespace

juce::String aboutThisCopy()
{
#if JUCE_ARM
	const char *arch = JUCE_64BIT ? "arm64" : "arm";
#else
	const char *arch = JUCE_64BIT ? "x86_64" : "x86";
#endif
	return juce::String("Lemus Plugin Manager ") + LEMUS_MANAGER_VERSION + juce::String::fromUTF8(" \xc2\xb7 ")
	       + juce::SystemStats::getOperatingSystemName() + " (" + arch + ")";
}

juce::String details(const juce::String &what, const juce::String &error)
{
	juce::String s = aboutThisCopy() + "\n" + what + ": failed\n" + error + "\n";
	// "… Its log is in <file>." names the installer's log.
	const juce::String marker = "Its log is in ";
	if (error.contains(marker)) {
		const juce::File log(error.fromLastOccurrenceOf(marker, false, false).trim().trimCharactersAtEnd("."));
		if (log.existsAsFile()) {
			juce::StringArray lines;
			lines.addLines(log.loadFileAsString().trimEnd());
			if (lines.size() > 80)
				lines.removeRange(0, lines.size() - 80);
			s << "\nThe installer's log:\n" << lines.joinIntoString("\n") << "\n";
		}
	}
	return s;
}

juce::String detailsOf(const Job &j)
{
	if (j.kind == Job::Kind::Remove)
		return details("Remove " + j.entry.plugin + " " + j.before.version() + " (" + formatsOf(j.before.formats()) + ")", j.error);
	const auto from = j.kind == Job::Kind::Update && j.before.version().isNotEmpty() ? " from " + j.before.version() : juce::String();
	return details((j.kind == Job::Kind::Update ? "Update " : "Install ") + j.entry.plugin + " " + j.entry.version + from + " ("
	                   + formatsOf(j.formats) + ")",
	               j.error);
}

juce::File crashFile()
{
	if (g_crashFile != juce::File())
		return g_crashFile;
	return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if JUCE_MAC
	    .getChildFile("Application Support")
#endif
	    .getChildFile("Lemus")
	    .getChildFile("Plugin Manager crash.txt");
}

void setCrashFile(const juce::File &f)
{
	g_crashFile = f;
	prepareCrashReport();
}

void writeCrashReport()
{
	if (g_crashPath.empty())
		prepareCrashReport();
#if JUCE_WINDOWS
	FILE *f = _wfopen(juce::String(g_crashPath).toWideCharPointer(), L"w");
#else
	FILE *f = std::fopen(g_crashPath.c_str(), "w");
#endif
	if (f == nullptr)
		return;
	std::fputs(g_crashHeader.c_str(), f);
	std::fputs(juce::SystemStats::getStackBacktrace().toRawUTF8(), f);
	std::fclose(f);
}

void installCrashHandler()
{
	crashFile().getParentDirectory().createDirectory();
	prepareCrashReport();
	juce::SystemStats::setApplicationCrashHandler(onCrash);
}

} // namespace lemus::manager
