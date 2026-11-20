// When something goes wrong, the manager gives the user what helps to report it, and sends
// nothing anywhere itself (the site's privacy note: nothing leaves the machine but the update
// check). Two places:
//   - a failed job, or its own update, has Copy details: the version and system, what it was
//     doing, why it failed and the installer's log;
//   - a crash writes the stack to crashFile(); the next launch says so and offers to copy it,
//     then deletes it.
#pragma once

#include "Tasks.h"

#include <juce_core/juce_core.h>

namespace lemus::manager {

// "Lemus Plugin Manager 0.1.0 · Mac OSX 15.1 (arm64)"
juce::String aboutThisCopy();

// Details to paste: aboutThisCopy(), what was being done, why it failed, and the installer's log
// (its last 80 lines) when the error names one ("Its log is in <file>.").
juce::String details(const juce::String &what, const juce::String &error);
juce::String detailsOf(const Job &);

// Where a crash's report goes: the Lemus settings folder, "Plugin Manager crash.txt".
juce::File crashFile();
// For tests: write it here instead (empty to stop).
void setCrashFile(const juce::File &);
// Writes the crash report now (what the crash handler does): aboutThisCopy() and the stack.
void writeCrashReport();
// Installs the handler that calls writeCrashReport() when the app crashes.
void installCrashHandler();

} // namespace lemus::manager
