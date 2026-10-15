#include "Updates.h"
#include "Roms.h"

#include <juce_data_structures/juce_data_structures.h>

namespace lemus::updates {

namespace {
constexpr juce::int64 kDayMs = 24 * 60 * 60 * 1000;
constexpr size_t kMaxBytes = 65536; // the file is about 100 bytes per plugin
} // namespace

bool enabled()
{
	return settings()->getBoolValue("updateCheck", true);
}

void setEnabled(bool on)
{
	auto s = settings();
	s->setValue("updateCheck", on);
	s->saveIfNeeded();
}

bool isNewer(const juce::String &a, const juce::String &b)
{
	auto x = juce::StringArray::fromTokens(a.trim(), ".", "");
	auto y = juce::StringArray::fromTokens(b.trim(), ".", "");
	for (int i = 0; i < juce::jmax(x.size(), y.size()); ++i) {
		const int p = x[i].getIntValue(), q = y[i].getIntValue();
		if (p != q)
			return p > q;
	}
	return false;
}

Release releaseOf(const juce::String &latestJson, const juce::String &plugin)
{
	const auto file = juce::JSON::parse(latestJson);
	const auto entry = plugin.isEmpty() ? juce::var() : file["plugins"][juce::Identifier(plugin)];
	const auto &r = entry.isObject() ? entry : file;
	return {r["version"].toString(), r["url"].toString()};
}

Release newer()
{
	if (!enabled())
		return {};
	auto r = releaseOf(settings()->getValue("latestJson"), JucePlugin_Name);
	if (!isNewer(r.version, JucePlugin_VersionString))
		return {};
	if (!r.url.startsWithIgnoreCase("https://"))
		r.url = LEMUS_SITE;
	return r;
}

Checker::Checker() : Thread("Lemus update check") {}

Checker::~Checker()
{
	signalThreadShouldExit();
	{
		const juce::ScopedLock l(m_lock);
		if (m_stream != nullptr)
			m_stream->cancel();
	}
	stopThread(3000);
}

void Checker::checkIfDue()
{
#if LEMUS_UPDATE_CHECK
	if (!enabled() || isThreadRunning())
		return;
	auto s = settings();
	const auto now = juce::Time::currentTimeMillis();
	const auto last = s->getValue("updateCheckedAt").getLargeIntValue();
	if (last <= now && now - last < kDayMs)
		return;
	// Noted before asking, so the other instances don't ask too.
	s->setValue("updateCheckedAt", juce::String(now));
	s->saveIfNeeded();
	startThread();
#endif
}

void Checker::run()
{
	juce::WebInputStream stream(juce::URL(juce::String(LEMUS_SITE) + "/latest.json"), false);
	stream.withConnectionTimeout(5000).withExtraHeaders("User-Agent: Lemus\r\n");
	{
		const juce::ScopedLock l(m_lock);
		if (threadShouldExit())
			return;
		m_stream = &stream;
	}
	juce::MemoryOutputStream body;
	if (stream.connect(nullptr) && stream.getStatusCode() == 200)
		body.writeFromInputStream(stream, kMaxBytes);
	{
		const juce::ScopedLock l(m_lock);
		m_stream = nullptr;
	}
	if (threadShouldExit())
		return;
	// Kept whole, so every plugin finds its own entry in this one check.
	const auto text = body.toString();
	if (!juce::JSON::parse(text).isObject())
		return;
	auto s = settings();
	s->setValue("latestJson", text);
	s->removeValue("latestVersion"); // the single-version file of the test builds
	s->removeValue("latestUrl");
	s->saveIfNeeded();
	sendChangeMessage();
}

} // namespace lemus::updates
