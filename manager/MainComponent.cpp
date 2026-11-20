#include "MainComponent.h"

namespace lemus::manager {

namespace {

constexpr int kWidth = 900, kHeight = 600;
constexpr int kHeader = 65, kFooter = 47, kFine = 24, kPane = 360, kRow = 32;

// The plug-in folder as the footer shows it: under a test root (--root), the system path it
// stands for, so snapshots read as they would on a user's machine.
juce::String shownFolder()
{
	const auto folder = places().pluginFolder();
	if (root() == juce::File())
		return folder.getFullPathName();
	return "/" + folder.getRelativePathFrom(root()).replaceCharacter('\\', '/');
}

const juce::String kDot = juce::String::fromUTF8(" \xc2\xb7 ");
const juce::String kUp = juce::String::fromUTF8("[\xe2\x86\x91]");    // [↑]
const juce::String kDown = juce::String::fromUTF8("[\xe2\x86\x93]");  // [↓]
const juce::String kTick = juce::String::fromUTF8("[\xe2\x9c\x93]");  // [✓]
const juce::String kCross = juce::String::fromUTF8("[\xe2\x9c\x97]"); // [✗]
const juce::String kNext = juce::String::fromUTF8("[\xc2\xb7]");      // [·]

juce::String formatsText(const std::vector<Format> &formats)
{
	juce::StringArray names;
	for (auto f : formats)
		names.add(nameOf(f));
	return names.joinIntoString(", ");
}

// What a user sees; under a test root (--root) nothing asks, but snapshots show the real line.
juce::String permissionLine()
{
#if JUCE_WINDOWS
	return "Windows asks for permission once.";
#else
	return "macOS asks for your password once.";
#endif
}

juce::String percent(double p)
{
	return juce::String(juce::roundToInt(p * 100)) + " %";
}

} // namespace

// The list: column labels, then a row per plugin.
class MainComponent::List final : public juce::Component {
public:
	explicit List(MainComponent &owner) : m_owner(owner)
	{
		setWantsKeyboardFocus(true);
		setTitle("Plugins");
	}

	static constexpr int kTop = 40; // the column labels

	void paint(juce::Graphics &g) override
	{
		const auto cols = columns(getLocalBounds().withTrimmedLeft(20).withTrimmedRight(20).withHeight(kTop).withTrimmedTop(8));
		g.setFont(mono(12.0f, true).withExtraKerningFactor(0.06f));
		g.setColour(col::muted);
		const char *names[] = {"MACHINE", "PLUGIN", "HAVE", "STATUS"};
		for (int i = 0; i < 4; ++i)
			g.drawText(names[i], cols[size_t(i)], juce::Justification::centredLeft, false);

		const auto rows = m_owner.visible();
		for (size_t i = 0; i < rows.size(); ++i) {
			const auto &e = *rows[i];
			const bool on = e.plugin == m_owner.m_selected;
			const auto r = rowBounds(int(i));
			if (on) {
				g.setColour(col::selection);
				g.fillRoundedRectangle(r.toFloat(), 6.0f);
			} else if (int(i) == m_hover) {
				g.setColour(col::ink.withAlpha(0.06f));
				g.fillRoundedRectangle(r.toFloat(), 6.0f);
			}
			const auto c = columns(r.reduced(12, 0));
			const auto &in = m_owner.installOf(e);
			g.setFont(mono(14.0f, on));
			g.setColour(on ? col::ink : col::inkSoft);
			g.drawText(e.shortName(), c[0], juce::Justification::centredLeft, true);
			g.setFont(mono(14.0f));
			g.setColour(on ? col::inkSoft : col::muted);
			g.drawText(e.plugin, c[1], juce::Justification::centredLeft, true);
			const auto have = in.installed() ? in.version() : juce::String::fromUTF8("\xe2\x80\x94");
			g.drawText(have.isEmpty() ? "?" : have, c[2], juce::Justification::centredLeft, true);
			const auto s = m_owner.statusOf(e);
			g.setColour(on ? s.onSelection : s.colour);
			g.drawText(s.text, c[3], juce::Justification::centredLeft, true);
			if (const auto *job = m_owner.jobOf(e); job != nullptr && job->state == Job::State::Downloading) {
				g.setColour(on ? col::title : col::accent);
				g.fillRect(juce::Rectangle<float>(float(r.getX()), float(r.getBottom() - 2), float(r.getWidth()) * float(job->progress), 2.0f));
			}
		}
	}

	void mouseMove(const juce::MouseEvent &e) override { setHover(rowAt(e.getPosition())); }
	void mouseExit(const juce::MouseEvent &) override { setHover(-1); }
	void mouseDown(const juce::MouseEvent &e) override
	{
		const auto rows = m_owner.visible();
		const int i = rowAt(e.getPosition());
		if (i >= 0 && i < int(rows.size()))
			m_owner.select(rows[size_t(i)]->plugin);
		grabKeyboardFocus();
	}

	int heightFor(int rows) const { return kTop + rows * (kRow + 2) + 8; }

private:
	static std::array<juce::Rectangle<int>, 4> columns(juce::Rectangle<int> r)
	{
		// minmax(0, 1.3fr) minmax(0, 1.2fr) 48px minmax(0, 1.5fr), 12px between
		const int gap = 12, fixed = 48, free = r.getWidth() - 3 * gap - fixed;
		std::array<juce::Rectangle<int>, 4> c;
		c[0] = r.removeFromLeft(free * 13 / 40);
		r.removeFromLeft(gap);
		c[1] = r.removeFromLeft(free * 12 / 40);
		r.removeFromLeft(gap);
		c[2] = r.removeFromLeft(fixed);
		r.removeFromLeft(gap);
		c[3] = r;
		return c;
	}

	juce::Rectangle<int> rowBounds(int i) const { return {8, kTop + i * (kRow + 2), getWidth() - 16, kRow}; }
	int rowAt(juce::Point<int> p) const { return p.y < kTop ? -1 : (p.y - kTop) / (kRow + 2); }
	void setHover(int i)
	{
		if (i != m_hover) {
			m_hover = i;
			repaint();
		}
	}

	MainComponent &m_owner;
	int m_hover = -1;
};

// The selected plugin's details: what is installed, what's new, the work under way, the buttons.
class MainComponent::Detail final : public juce::Component {
public:
	explicit Detail(MainComponent &owner) : m_owner(owner)
	{
		for (auto *b : {&m_primary, &m_remove, &m_cancel, &m_confirm, &m_page, &m_copy})
			addChildComponent(b);
		m_notesView.setViewedComponent(&m_notes, false);
		m_notesView.setScrollBarsShown(true, false);
		m_notesView.setScrollBarThickness(6);
		m_notesView.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, col::well.brighter(0.2f));
		addChildComponent(m_notesView);
		m_primary.onClick = [this] { primary(); };
		m_remove.onClick = [this] {
			m_confirming = true;
			m_owner.refresh();
		};
		m_cancel.onClick = [this] {
			m_confirming = false;
			m_owner.refresh();
		};
		m_confirm.onClick = [this] {
			m_confirming = false;
			if (const auto *e = m_owner.selected())
				m_owner.remove(*e);
		};
		m_page.onClick = [this] {
			if (const auto *e = m_owner.selected())
				juce::URL(e->page).launchInDefaultBrowser();
		};
		m_copy.onClick = [this] {
			if (const auto *e = m_owner.selected())
				if (const auto *j = m_owner.jobOf(*e)) {
					juce::SystemClipboard::copyTextToClipboard(detailsOf(*j));
					m_copy.setButtonText("Copied");
				}
		};
	}

	void confirmRemove()
	{
		m_confirming = true;
		m_owner.refresh();
	}

	void selectionChanged()
	{
		m_confirming = false;
		m_ticked.clear();
		if (const auto *e = m_owner.selected())
			for (auto f : formatsHere(e->standalone))
				m_ticked.insert(f);
	}

	// Lays out the details for the current state; paint draws m_items.
	void update()
	{
		m_items.clear();
		m_checks.clear();
		for (auto *b : {&m_primary, &m_remove, &m_cancel, &m_confirm, &m_page, &m_copy})
			b->setVisible(false);
		m_notesView.setVisible(false);
		const auto *e = m_owner.selected();
		if (e == nullptr) {
			repaint();
			return;
		}
		const float x = 20, w = float(getWidth()) - 40;
		float y = 20;
		y += text(e->emulates, mono(16.0f, true), col::ink, x, y, w) + 4;
		// The plugin's name on a line of its own when the line would break inside it.
		auto sub = e->subtitle();
		if (juce::GlyphArrangement::getStringWidth(mono(13.0f), sub) > w)
			sub = sub.upToLastOccurrenceOf(kDot, false, false) + "\n" + e->plugin;
		y += text(sub, mono(13.0f), col::inkSoft, x, y, w) + 16;

		const auto &in = m_owner.installOf(*e);
		const auto *job = m_owner.jobOf(*e);
		const bool busy = m_owner.busy();
		const bool update = m_owner.hasUpdate(*e);

		if (in.installed()) {
			label("INSTALLED", x, y);
			y += 20;
			y += text(in.version() + kDot + formatsText(in.formats()), mono(13.0f), col::ink, x, y, w) + 16;
		}
		if (!in.userCopies.empty()) {
			std::vector<Format> f;
			for (const auto &c : in.userCopies)
				f.push_back(c.format);
			const auto v = in.userCopies[0].version;
			y += text("Also in your own ~/Library/Audio/Plug-Ins" + (v.isNotEmpty() ? " (" + v + ", " : " (") + formatsText(f)
			              + "): a DAW may load either copy. This list looks after the copies for every user.",
			          mono(12.0f), col::warn, x, y, w)
			     + 16;
		}

		if (job != nullptr) {
			y = jobBlock(*e, *job, x, y, w);
		} else if (update) {
			const auto changes = m_owner.m_catalog->changesSince(*e, in.version());
			label(changes.size() > 1 ? "NEW SINCE " + in.version() : "NEW IN " + e->version, x, y);
			y += 24;
			m_notes.set(changes, int(w));
			m_notesTop = y;
			m_notesView.setVisible(true);
		} else if (in.installed()) {
			y += text("Up to date.", mono(13.0f), col::inkSoft, x, y, w) + 16;
		}

		if (!in.installed() && job == nullptr) {
			label("INSTALL " + e->version, x, y);
			y += 24;
			const auto formats = formatsHere(e->standalone);
			const float wellHeight = 12 + float(formats.size()) * 30;
			m_items.push_back({Item::Kind::Well, {}, {}, {}, {x, y, w, wellHeight}});
			float cy = y + 6;
			for (auto f : formats) {
				auto c = std::make_unique<Check>(nameOf(f), e->plugin + fileExtension(f));
				c->setToggleState(m_ticked.count(f) > 0, juce::dontSendNotification);
				c->setEnabled(!busy);
				c->onClick = [this, f, raw = c.get()] {
					if (raw->getToggleState())
						m_ticked.insert(f);
					else
						m_ticked.erase(f);
					m_owner.refresh();
				};
				c->setBounds(int(x + 12), int(cy), int(w - 24), 30);
				addAndMakeVisible(*c);
				m_checks.push_back(std::move(c));
				cy += 30;
			}
			y += wellHeight + 10;
			if (e->withoutRom == "none")
				y += text("Runs only with its ROM. Lemus ships no ROMs; the plugin names the file it needs.", mono(13.0f), col::inkSoft, x, y, w) + 8;
			else if (e->withoutRom == "voice")
				y += text("Without its ROM it runs in voice mode. Lemus ships no ROMs.", mono(13.0f), col::inkSoft, x, y, w) + 8;
		}

		// The buttons, from the bottom up.
		float bottom = float(getHeight()) - 20;
		const bool finished = job == nullptr || job->finished();
		if (!busy && finished) {
			const auto perm = permissionLine();
			bottom -= 16;
			m_items.push_back({Item::Kind::Text, perm, mono(12.0f), col::inkSoft, {x, bottom, w, 16}});
			bottom -= 8;
			const int by = int(bottom - 40);
			auto row = juce::Rectangle<int>(int(x), by, int(w), 40);
			if (m_confirming && in.installed()) {
				const auto ask = "Remove " + e->plugin + " " + in.version() + "? Its " + formatsText(in.formats())
				                 + " go. Your ROMs, artwork and songs stay.";
				const float h = layoutOf(ask, mono(13.0f), col::ink, w).getHeight();
				m_items.push_back({Item::Kind::Text, ask, mono(13.0f), col::ink, {x, float(by) - 10 - h, w, h}});
				m_confirm.setButtonText("Remove");
				m_confirm.setBounds(row.removeFromRight(m_confirm.idealWidth() + 16));
				row.removeFromRight(8);
				m_cancel.setBounds(row.removeFromRight(m_cancel.idealWidth() + 16));
				m_confirm.setVisible(true);
				m_cancel.setVisible(true);
				bottom = float(by) - 20 - h;
			} else {
				// A failed job: its details to paste into a report.
				if (job != nullptr && job->state == Job::State::Failed) {
					if (!m_copied.isEmpty() && m_copied != e->plugin + job->error)
						m_copy.setButtonText("Copy details");
					m_copied = e->plugin + job->error;
					m_copy.setBounds(row.removeFromLeft(m_copy.idealWidth() + 20));
					m_copy.setVisible(true);
					row.removeFromLeft(8);
				}
				if (in.installed()) {
					m_remove.setBounds(row.removeFromRight(m_remove.idealWidth() + 20));
					m_remove.setVisible(true);
					row.removeFromRight(8);
				}
				if (update || !in.installed()) {
					m_primary.setButtonText(update ? "Update to " + e->version : "Install " + e->version);
					m_primary.setEnabled(update || !m_ticked.empty());
					m_primary.setBounds(row);
					m_primary.setVisible(true);
				} else if (job == nullptr || job->state != Job::State::Failed) {
					m_page.setBounds(row.removeFromLeft(m_page.idealWidth() + 24));
					m_page.setVisible(true);
				}
				bottom = float(by) - 16;
			}
		}
		if (m_notesView.isVisible()) {
			m_notesView.setBounds(int(x), int(m_notesTop), int(w) + 8, juce::jmax(0, int(bottom - m_notesTop)));
			m_notes.setSize(int(w), m_notes.height());
		}
		repaint();
	}

	void paint(juce::Graphics &g) override
	{
		g.fillAll(col::panel);
		for (const auto &i : m_items) {
			switch (i.kind) {
			case Item::Kind::Text: layoutOf(i.text, *i.font, i.colour, i.area.getWidth()).draw(g, i.area); break;
			case Item::Kind::Label: lemus::style::drawLabel(g, i.text, i.area); break;
			case Item::Kind::Well:
				g.setColour(col::well);
				g.fillRoundedRectangle(i.area, 8.0f);
				break;
			case Item::Kind::Bar:
				g.setColour(col::well.brighter(0.12f));
				g.fillRoundedRectangle(i.area, 2.0f);
				g.setColour(col::accent);
				g.fillRoundedRectangle(i.area.withWidth(i.area.getWidth() * float(i.text.getDoubleValue())), 2.0f);
				break;
			}
		}
	}

	void resized() override { update(); }

private:
	struct Item {
		enum class Kind { Text, Label, Well, Bar };
		Kind kind;
		juce::String text;
		std::optional<juce::Font> font;
		juce::Colour colour;
		juce::Rectangle<float> area;
	};

	// What's new: each version's notes under its number.
	class Notes final : public juce::Component {
	public:
		void set(const std::vector<updates::Change> &changes, int width)
		{
			m_changes = changes;
			m_width = width;
			setSize(width, height());
			repaint();
		}
		int height() const
		{
			float y = 0;
			for (size_t i = 0; i < m_changes.size(); ++i) {
				if (m_changes.size() > 1)
					y += 20;
				for (const auto &n : m_changes[i].notes)
					y += layoutOf(n, mono(13.0f), col::inkSoft, float(m_width - 16)).getHeight() + 6;
				y += 8;
			}
			return int(y);
		}
		void paint(juce::Graphics &g) override
		{
			float y = 0;
			for (const auto &c : m_changes) {
				if (m_changes.size() > 1) {
					g.setColour(col::ink);
					g.setFont(mono(13.0f, true));
					g.drawText(c.version, juce::Rectangle<float>(0, y, float(m_width), 18), juce::Justification::centredLeft, false);
					y += 20;
				}
				for (const auto &n : c.notes) {
					g.setColour(col::muted);
					g.setFont(mono(13.0f));
					g.drawText(juce::String::fromUTF8("\xc2\xb7"), juce::Rectangle<float>(0, y, 12, 19), juce::Justification::centredLeft, false);
					y += drawWrapped(g, n, mono(13.0f), col::inkSoft, 16, y, float(m_width - 16)) + 6;
				}
				y += 8;
			}
		}

	private:
		std::vector<updates::Change> m_changes;
		int m_width = 300;
	};

	static juce::String fileExtension(Format f)
	{
#if JUCE_WINDOWS
		return f == Format::App ? ".exe" : ".vst3";
#else
		return f == Format::AU ? ".component" : f == Format::VST3 ? ".vst3" : ".app";
#endif
	}

	float text(const juce::String &t, const juce::Font &font, juce::Colour colour, float x, float y, float w)
	{
		const float h = layoutOf(t, font, colour, w).getHeight();
		m_items.push_back({Item::Kind::Text, t, font, colour, {x, y, w, h}});
		return h;
	}

	void label(const juce::String &t, float x, float y) { m_items.push_back({Item::Kind::Label, t, {}, {}, {x, y, 320, 16}}); }

	// The steps of the work on this plugin, as they go.
	float jobBlock(const Entry &e, const Job &job, float x, float y, float w)
	{
		const auto &in = m_owner.installOf(e);
		juce::String head;
		switch (job.kind) {
		case Job::Kind::Update: head = job.before.version() + " TO " + e.version; break;
		case Job::Kind::Install: head = (job.state == Job::State::Done ? "INSTALLED " : "INSTALLING ") + e.version; break;
		case Job::Kind::Remove: head = (job.state == Job::State::Done ? "REMOVED " : "REMOVING ") + job.before.version(); break;
		}
		label(head, x, y);
		y += 24;
		struct Step {
			juce::String name;
			int state; // 0 to come, 1 under way, 2 done, 3 failed
		};
		std::vector<Step> steps;
		const auto s = job.state;
		const bool failed = s == Job::State::Failed, stopped = s == Job::State::Stopped;
		const auto at = [&](int step) {
			// 0 download, 1 check, 2 install
			const int now = s == Job::State::Waiting ? -1
			                : s == Job::State::Downloading ? 0
			                : s == Job::State::Checking ? 1
			                : s == Job::State::Ready || s == Job::State::Installing ? 2
			                : s == Job::State::Done ? 3
			                                        : m_failedAt;
			if (failed && step == now)
				return 3;
			return step < now ? 2 : step == now && !stopped ? 1 : 0;
		};
		if (failed && m_failedJob != job.entry.plugin) {
			// Where it failed: the download, its check, or the install.
			m_failedAt = job.error.contains("SHA-256") ? 1 : job.error.contains("download") || job.error.contains("installer for") ? 0 : 2;
			m_failedJob = job.entry.plugin;
		}
		const auto formats = formatsText(job.kind == Job::Kind::Remove ? job.before.formats() : job.formats);
		if (job.kind == Job::Kind::Remove) {
			steps.push_back({"Remove " + formats, s == Job::State::Done ? 2 : failed ? 3 : s == Job::State::Installing ? 1 : 0});
		} else {
			steps.push_back({"Download", at(0)});
			steps.push_back({"Check SHA-256", at(1)});
			steps.push_back({(job.kind == Job::Kind::Update ? "Replace " : "Install ") + formats, at(2)});
		}
		const float top = y;
		y += 10;
		for (const auto &step : steps) {
			const auto mark = step.state == 2 ? kTick : step.state == 3 ? kCross : step.state == 1 ? kNext : juce::String("[ ]");
			const auto colour = step.state == 2 ? col::ok : step.state == 3 ? kError : step.state == 1 ? col::ink : col::inkSoft;
			auto line = mark + " " + step.name;
			if (step.name == "Download" && s == Job::State::Downloading)
				line << "  " << percent(job.progress);
			m_items.push_back({Item::Kind::Text, line, mono(13.0f), colour, {x + 12, y, w - 24, 19}});
			y += 22;
			if (step.name == "Download" && s == Job::State::Downloading) {
				m_items.push_back({Item::Kind::Bar, juce::String(job.progress), {}, {}, {x + 12, y, w - 24, 4}});
				y += 10;
			}
		}
		y += 4;
		m_items.insert(m_items.end() - int(steps.size()) - (s == Job::State::Downloading ? 1 : 0),
		               {Item::Kind::Well, {}, {}, {}, {x, top, w, y - top}});
		y += 12;
		if (failed || stopped)
			y += text(job.error, mono(13.0f), failed ? kError : col::warn, x, y, w) + 12;
		else if (job.kind == Job::Kind::Update && s != Job::State::Waiting)
			y += text("A DAW that has " + e.plugin + " open keeps playing " + job.before.version() + " until it loads the plugin again.",
			          mono(13.0f), col::inkSoft, x, y, w)
			     + 12;
		juce::ignoreUnused(in);
		return y;
	}

	void primary()
	{
		const auto *e = m_owner.selected();
		if (e == nullptr)
			return;
		if (m_owner.hasUpdate(*e)) {
			m_owner.update(*e);
		} else if (!m_owner.installOf(*e).installed()) {
			std::vector<Format> formats;
			for (auto f : formatsHere(e->standalone))
				if (m_ticked.count(f) > 0)
					formats.push_back(f);
			if (!formats.empty())
				m_owner.install(*e, formats);
		}
	}

	MainComponent &m_owner;
	std::vector<Item> m_items;
	std::vector<std::unique_ptr<Check>> m_checks;
	std::set<Format> m_ticked;
	bool m_confirming = false;
	int m_failedAt = 2;
	juce::String m_failedJob;
	Pill m_primary{{}, Pill::Kind::Primary, col::accent, 14.0f};
	Pill m_remove{"Remove"};
	Pill m_cancel{"Cancel"};
	Pill m_confirm{"Remove", Pill::Kind::Primary};
	Pill m_page{"Its page"};
	Pill m_copy{"Copy details"};
	juce::String m_copied; // the failure the button's "Copied" is for
	Notes m_notes;
	juce::Viewport m_notesView;
	float m_notesTop = 0;
};

// A newer manager: a card over the window, as the other plugin managers ask on launch. What's new,
// then Later or Update now; while it runs, the same three steps as a plugin's; then the app
// restarts as the new version.
class MainComponent::Prompt final : public juce::Component {
public:
	explicit Prompt(MainComponent &owner) : m_owner(owner)
	{
		for (auto *b : {&m_later, &m_now, &m_page, &m_close, &m_copy, &m_forum})
			addChildComponent(b);
		m_later.onClick = [this] { m_owner.hidePrompt(); };
		m_close.onClick = [this] {
			if (m_owner.m_crash.isNotEmpty())
				m_owner.closeCrashNote();
			else
				m_owner.hidePrompt();
		};
		m_now.onClick = [this] { m_owner.startSelfUpdate(); };
		m_page.onClick = [this] {
			if (const auto *e = m_owner.newerManager())
				juce::URL(e->page).launchInDefaultBrowser();
		};
		m_forum.onClick = [] { juce::URL(LEMUS_FORUM).launchInDefaultBrowser(); };
		m_copy.onClick = [this] {
			const auto *self = m_owner.m_self.get();
			juce::SystemClipboard::copyTextToClipboard(
			    m_owner.m_crash.isNotEmpty() ? m_owner.m_crash
			    : self != nullptr            ? details("Update Lemus Plugin Manager to " + self->entry().version, self->error())
			                                 : juce::String());
			m_copy.setButtonText("Copied");
		};
		setWantsKeyboardFocus(true);
	}

	void update()
	{
		const bool crash = m_owner.m_crash.isNotEmpty();
		const auto *self = crash ? nullptr : m_owner.m_self.get();
		const bool idle = !crash && self == nullptr;
		const bool failed = self != nullptr && (self->state() == SelfUpdate::State::Failed || self->state() == SelfUpdate::State::Stopped);
		m_later.setVisible(idle);
		m_now.setVisible(idle || failed);
		m_now.setButtonText(failed ? "Try again" : "Update now");
		m_now.setEnabled(!m_owner.batchBusy() || failed);
		m_page.setVisible(failed && self->error().contains("page"));
		m_close.setVisible(crash || failed || (self != nullptr && self->state() == SelfUpdate::State::Done));
		m_forum.setVisible(crash);
		const bool copy = crash || (failed && self->state() == SelfUpdate::State::Failed);
		if (copy && !m_copy.isVisible())
			m_copy.setButtonText("Copy details");
		m_copy.setVisible(copy);
		resized();
		repaint();
	}

	void resized() override
	{
		const auto card = cardBounds();
		auto row = card.reduced(kPad).removeFromBottom(36);
		for (auto *b : {&m_now, &m_close, &m_later, &m_copy, &m_page, &m_forum})
			if (b->isVisible()) {
				b->setBounds(row.removeFromRight(b->idealWidth() + 12).withSizeKeepingCentre(b->idealWidth() + 12, 34));
				row.removeFromRight(10);
			}
	}

	void paint(juce::Graphics &g) override
	{
		g.fillAll(col::ground.withAlpha(0.72f));
		const auto card = cardBounds().toFloat();
		g.setColour(col::panel);
		g.fillRoundedRectangle(card, 10.0f);
		g.setColour(col::rule);
		g.drawRoundedRectangle(card.reduced(0.5f), 10.0f, 1.0f);
		content(&g);
	}

	void mouseDown(const juce::MouseEvent &) override {} // the window behind waits

	bool keyPressed(const juce::KeyPress &key) override
	{
		if (key == juce::KeyPress::escapeKey && m_owner.m_crash.isNotEmpty()) {
			m_owner.closeCrashNote();
			return true;
		}
		if (key == juce::KeyPress::escapeKey && m_owner.m_self == nullptr) {
			m_owner.hidePrompt();
			return true;
		}
		return false;
	}

private:
	static constexpr int kWidth = 500, kPad = 24;

	juce::Rectangle<int> cardBounds() const
	{
		const int h = int(content(nullptr)) + kPad + 36 + kPad;
		return juce::Rectangle<int>(kWidth, h).withCentre(getLocalBounds().getCentre());
	}

	// Lays out (and with a Graphics, draws) the card's text from its top; returns the height.
	float content(juce::Graphics *g) const
	{
		const auto *e = m_owner.newerManager();
		const auto *self = m_owner.m_self.get();
		const auto version = e != nullptr ? e->version : self != nullptr ? self->entry().version : juce::String();
		const float w = float(kWidth - 2 * kPad);
		const auto card = g != nullptr ? cardBounds().toFloat() : juce::Rectangle<float>();
		const float x = card.getX() + kPad;
		float y = kPad;
		const auto at = [&card](float top) { return card.getY() + top; };
		const auto text = [&](const juce::String &t, const juce::Font &font, juce::Colour colour) {
			const float h = layoutOf(t, font, colour, w).getHeight();
			if (g != nullptr)
				drawWrapped(*g, t, font, colour, x, at(y), w);
			y += h;
		};

		if (m_owner.m_crash.isNotEmpty()) {
			text("Lemus Plugin Manager quit unexpectedly last time", mono(16.0f, true), col::title);
			y += 12;
			text("Nothing was sent anywhere. To report it, post on the forum and paste the details: the version, your system and "
			     "where it stopped.",
			     mono(13.0f), col::inkSoft);
			return y + 16;
		}

		text("Lemus Plugin Manager " + version + " is out", mono(16.0f, true), col::title);
		y += 4;
		text("You have " + juce::String(LEMUS_MANAGER_VERSION) + ".", mono(13.0f), col::inkSoft);
		y += 18;

		if (self == nullptr) {
			const auto changes = e != nullptr && m_owner.m_catalog ? m_owner.m_catalog->changesSince(*e, LEMUS_MANAGER_VERSION)
			                                                       : std::vector<updates::Change>{};
			int shown = 0;
			if (!changes.empty()) {
				if (g != nullptr)
					lemus::style::drawLabel(*g, "WHAT'S NEW", {x, at(y), w, 14});
				y += 22;
				for (const auto &c : changes)
					for (const auto &n : c.notes)
						if (shown++ < 6) {
							if (g != nullptr) {
								g->setColour(col::muted);
								g->setFont(mono(13.0f));
								g->drawText(juce::String::fromUTF8("\xc2\xb7"), juce::Rectangle<float>(x, at(y), 12, 19), juce::Justification::centredLeft, false);
								drawWrapped(*g, n, mono(13.0f), col::inkSoft, x + 16, at(y), w - 16);
							}
							y += layoutOf(n, mono(13.0f), col::inkSoft, w - 16).getHeight() + 6;
						}
				y += 10;
			}
#if JUCE_WINDOWS
			text("Then it restarts as the new version.", mono(12.5f), col::muted);
#else
			text("macOS asks for your password once, then it restarts as the new version.", mono(12.5f), col::muted);
#endif
			return y + 8;
		}

		// The three steps, as a plugin's.
		const auto s = self->state();
		const bool failed = s == SelfUpdate::State::Failed, stopped = s == SelfUpdate::State::Stopped;
		const int now = s == SelfUpdate::State::Downloading ? 0
		                : s == SelfUpdate::State::Checking  ? 1
		                : s == SelfUpdate::State::Installing ? 2
		                : s == SelfUpdate::State::Done       ? 3
		                : self->error().contains("SHA-256") ? 1
		                : self->error().contains("download") || self->error().contains("installer for") ? 0
		                                                                                                : 2;
		const float top = y;
		y += 10;
		const juce::StringArray names{"Download", "Check SHA-256", "Install " + version};
		for (int i = 0; i < names.size(); ++i) {
			const int state = (failed && i == now) ? 3 : i < now ? 2 : i == now && !stopped ? 1 : 0;
			const auto mark = state == 2 ? kTick : state == 3 ? kCross : state == 1 ? kNext : juce::String("[ ]");
			const auto colour = state == 2 ? col::ok : state == 3 ? kError : state == 1 ? col::ink : col::inkSoft;
			auto line = mark + " " + names[i];
			const bool downloading = i == 0 && s == SelfUpdate::State::Downloading;
			if (downloading)
				line << "  " << percent(self->progress());
			if (g != nullptr) {
				if (i == 0) {
					const float wellH = 10 + 22 * 3 + (downloading ? 10 : 0) + 4;
					g->setColour(col::well);
					g->fillRoundedRectangle({x, at(top), w, wellH}, 8.0f);
				}
				g->setColour(colour);
				g->setFont(mono(13.0f));
				g->drawText(line, juce::Rectangle<float>(x + 12, at(y), w - 24, 19), juce::Justification::centredLeft, false);
			}
			y += 22;
			if (downloading) {
				if (g != nullptr) {
					const juce::Rectangle<float> bar(x + 12, at(y), w - 24, 4);
					g->setColour(col::well.brighter(0.12f));
					g->fillRoundedRectangle(bar, 2.0f);
					g->setColour(col::accent);
					g->fillRoundedRectangle(bar.withWidth(bar.getWidth() * float(self->progress())), 2.0f);
				}
				y += 10;
			}
		}
		y += 4 + 14;
		if (failed || stopped)
			text(self->error(), mono(13.0f), failed ? kError : col::warn);
		else if (s == SelfUpdate::State::Done)
			text(m_owner.m_relaunched ? "Restarting." : "Updated. Open it again to use " + version + ".", mono(13.0f), col::ok);
		return y + 8;
	}

	MainComponent &m_owner;
	Pill m_later{"Later"};
	Pill m_now{"Update now", Pill::Kind::Primary, col::accent, 14.0f};
	Pill m_page{"Its page"};
	Pill m_close{"Close"};
	Pill m_copy{"Copy details"};
	Pill m_forum{"Forum"};
};

MainComponent::MainComponent()
{
	m_list = std::make_unique<List>(*this);
	m_listView = std::make_unique<juce::Viewport>();
	m_listView->setViewedComponent(m_list.get(), false);
	m_listView->setScrollBarsShown(true, false);
	m_listView->setScrollBarThickness(6);
	m_listView->getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, col::well.brighter(0.2f));
	m_detail = std::make_unique<Detail>(*this);
	addAndMakeVisible(m_search);
	addAndMakeVisible(m_updateAll);
	addAndMakeVisible(*m_listView);
	addAndMakeVisible(*m_detail);
	addAndMakeVisible(m_show);
	addAndMakeVisible(m_daily);
	addChildComponent(m_retry);
	addChildComponent(m_managerUpdate);

	m_search.editor().onTextChange = [this] { refresh(); };
	m_updateAll.onClick = [this] {
		if (batchBusy())
			m_batch->stop();
		else
			updateAll();
		refresh();
	};
	m_retry.onClick = [this] { load(); };
	m_show.onClick = [] {
		const auto folder = places().pluginFolder();
		if (folder.isDirectory())
			folder.startAsProcess();
	};
	m_daily.setToggleState(updates::enabled(), juce::dontSendNotification);
	m_daily.onClick = [this] { updates::setEnabled(m_daily.getToggleState()); };
	m_daily.setTooltip("The plugins' own check: once a day, while a plugin window is open.");
	m_managerUpdate.onClick = [this] {
		if (newerManager() != nullptr)
			showPrompt();
		else if (m_catalog && m_catalog->manager)
			juce::URL(m_catalog->manager->page).launchInDefaultBrowser();
	};
	m_prompt = std::make_unique<Prompt>(*this);
	addChildComponent(*m_prompt);

	setSize(kWidth, kHeight);
	setWantsKeyboardFocus(true);
	if (crashFile().existsAsFile()) {
		m_crash = crashFile().loadFileAsString();
		m_prompt->setVisible(true);
		m_prompt->update();
	}
	load();
}

void MainComponent::closeCrashNote()
{
	m_crash.clear();
	crashFile().deleteFile();
	if (!m_promptOffered && newerManager() != nullptr)
		showPrompt();
	else
		hidePrompt();
}

MainComponent::~MainComponent()
{
	m_listView->setViewedComponent(nullptr, false);
	m_batch.reset();
	m_self.reset();
}

bool MainComponent::batchBusy() const
{
	return m_batch != nullptr && !m_batch->done();
}

bool MainComponent::busy() const
{
	return (m_batch != nullptr && !m_batch->done()) || (m_self != nullptr && !m_self->finished());
}

const Entry *MainComponent::newerManager() const
{
	if (!m_catalog || !m_catalog->manager || !updates::isNewer(m_catalog->manager->version, LEMUS_MANAGER_VERSION))
		return nullptr;
	const auto &d = m_catalog->manager->here();
	const bool local = root() != juce::File() && d.url.startsWith("file://");
	return d.valid() || local ? &*m_catalog->manager : nullptr;
}

void MainComponent::showPrompt()
{
	m_promptOffered = true;
	m_prompt->setBounds(getLocalBounds());
	m_prompt->setVisible(true);
	m_prompt->toFront(true);
	m_prompt->update();
}

void MainComponent::hidePrompt()
{
	if (m_self != nullptr && !m_self->finished())
		return;
	m_self.reset();
	m_prompt->setVisible(false);
	refresh();
}

void MainComponent::startSelfUpdate()
{
	const auto *e = newerManager();
	if (e == nullptr || (m_batch != nullptr && !m_batch->done()) || (m_self != nullptr && !m_self->finished()))
		return;
	m_self = std::make_unique<SelfUpdate>(*e, [this] { triggerAsyncUpdate(); });
	m_self->start();
	refresh();
}

void MainComponent::sayBusy()
{
	m_note = "Closes when the work under way is done.";
	refresh();
}

void MainComponent::load()
{
	m_loading = Loading::Reading;
	m_error.clear();
	refresh();
	juce::WeakReference<MainComponent> self(this);
	juce::Thread::launch([self] {
		juce::String error;
		auto catalog = fetchCatalog(error);
		std::map<juce::String, Install> installed;
		if (catalog)
			for (const auto &e : catalog->plugins)
				installed[e.plugin] = scan(e.plugin);
		juce::MessageManager::callAsync([self, read = std::move(catalog), found = std::move(installed), error] {
			if (auto *me = self.get()) {
				me->m_catalog = read;
				me->m_installed = found;
				me->m_error = error;
				me->m_loading = read ? Loading::Ready : Loading::Failed;
				if (read && me->selected() == nullptr && !read->plugins.empty()) {
					// Start on the first with an update, else the first.
					me->m_selected = read->plugins[0].plugin;
					for (const auto &e : read->plugins)
						if (me->hasUpdate(e)) {
							me->m_selected = e.plugin;
							break;
						}
				}
				me->m_detail->selectionChanged();
				me->refresh();
				if (!me->m_promptOffered && me->m_crash.isEmpty() && me->newerManager() != nullptr)
					me->showPrompt();
			}
		});
	});
}

void MainComponent::rescan()
{
	if (!m_catalog)
		return;
	for (const auto &e : m_catalog->plugins)
		m_installed[e.plugin] = scan(e.plugin);
}

std::vector<const Entry *> MainComponent::visible() const
{
	std::vector<const Entry *> v;
	if (!m_catalog)
		return v;
	const auto words = juce::StringArray::fromTokens(m_search.editor().getText().trim(), " ", "");
	for (const auto &e : m_catalog->plugins) {
		const auto hay = e.emulates + " " + e.plugin + " " + e.type + " " + juce::String(e.year);
		bool all = true;
		for (const auto &w : words)
			all = all && hay.containsIgnoreCase(w);
		if (all)
			v.push_back(&e);
	}
	return v;
}

const Entry *MainComponent::selected() const
{
	return m_catalog ? m_catalog->find(m_selected) : nullptr;
}

void MainComponent::select(const juce::String &plugin)
{
	if (plugin == m_selected)
		return;
	m_selected = plugin;
	m_detail->selectionChanged();
	refresh();
}

void MainComponent::moveSelection(int by)
{
	const auto rows = visible();
	if (rows.empty())
		return;
	int at = -1;
	for (size_t i = 0; i < rows.size(); ++i)
		if (rows[i]->plugin == m_selected)
			at = int(i);
	at = juce::jlimit(0, int(rows.size()) - 1, at < 0 ? 0 : at + by);
	select(rows[size_t(at)]->plugin);
}

const Install &MainComponent::installOf(const Entry &e) const
{
	static const Install none;
	const auto it = m_installed.find(e.plugin);
	return it != m_installed.end() ? it->second : none;
}

const Job *MainComponent::jobOf(const Entry &e) const
{
	for (const auto &j : m_jobs)
		if (j.entry.plugin == e.plugin)
			return &j;
	return nullptr;
}

bool MainComponent::hasUpdate(const Entry &e) const
{
	const auto &in = installOf(e);
	return in.installed() && in.version().isNotEmpty() && updates::isNewer(e.version, in.version());
}

int MainComponent::updateCount() const
{
	int n = 0;
	if (m_catalog)
		for (const auto &e : m_catalog->plugins)
			n += hasUpdate(e) ? 1 : 0;
	return n;
}

MainComponent::Status MainComponent::statusOf(const Entry &e) const
{
	if (const auto *j = jobOf(e)) {
		const bool remove = j->kind == Job::Kind::Remove;
		switch (j->state) {
		case Job::State::Waiting: return {kNext + (remove ? " to remove" : " next: " + e.version), col::inkSoft, col::inkSoft};
		case Job::State::Downloading: return {kDown + " " + e.version + kDot + percent(j->progress), col::accent, col::title};
		case Job::State::Checking: return {kDown + " checking", col::accent, col::title};
		case Job::State::Ready: return {kNext + (remove ? " to remove" : " downloaded"), col::inkSoft, col::inkSoft};
		case Job::State::Installing: return {kDown + (remove ? " removing" : " installing"), col::accent, col::title};
		case Job::State::Done:
			return {kTick + (remove ? " removed" : j->kind == Job::Kind::Update ? " updated" : " installed"), col::ok, col::ok};
		case Job::State::Failed: return {kCross + " failed", kError, kError};
		case Job::State::Stopped: return {"[ ] stopped", col::warn, col::warn};
		}
	}
	const auto &in = installOf(e);
	if (!in.installed())
		return {"[ ] not installed", col::muted, col::inkSoft};
	if (hasUpdate(e))
		return {kUp + " " + e.version, col::accent, col::title};
	if (in.version().isEmpty())
		return {kTick + " installed", col::ok, col::ok};
	return {kTick + " latest", col::ok, col::ok};
}

juce::String MainComponent::summary() const
{
	if (m_note.isNotEmpty())
		return m_note;
	if (m_loading == Loading::Reading)
		return "Reading the list";
	if (!m_catalog)
		return {};
	if (m_self != nullptr && !m_self->finished())
		return "Updating the Plugin Manager";
	if (batchBusy()) {
		const auto jobs = m_batch->jobs();
		int finished = 0;
		std::set<Job::Kind> kinds;
		for (const auto &j : jobs) {
			finished += j.finished() ? 1 : 0;
			kinds.insert(j.kind);
		}
		const auto verb = kinds.size() > 1                     ? juce::String("Working on")
		                  : *kinds.begin() == Job::Kind::Update ? juce::String("Updating")
		                  : *kinds.begin() == Job::Kind::Install ? juce::String("Installing")
		                                                        : juce::String("Removing");
		if (jobs.size() == 1)
			return verb + " " + jobs[0].entry.plugin;
		// Downloads go one by one; then one step installs them all.
		int fetched = 0, fetching = 0;
		for (const auto &j : jobs) {
			fetched += j.state == Job::State::Ready || j.state == Job::State::Installing || j.finished() ? 1 : 0;
			fetching += j.state == Job::State::Downloading || j.state == Job::State::Checking ? 1 : 0;
		}
		juce::ignoreUnused(finished);
		if (fetching > 0)
			return "Downloading " + juce::String(fetched + 1) + " of " + juce::String(int(jobs.size()));
		return verb + " " + juce::String(int(jobs.size())) + " plugins";
	}
	// "5 plugins · none installed", "5 plugins · 2 updates", "2 of 5 installed · up to date"
	const int n = int(m_catalog->plugins.size()), u = updateCount();
	int installed = 0;
	for (const auto &e : m_catalog->plugins)
		installed += installOf(e).installed() ? 1 : 0;
	const auto plugins = juce::String(n) + (n == 1 ? " plugin" : " plugins");
	if (installed == 0)
		return plugins + kDot + "none installed";
	if (u > 0)
		return plugins + kDot + juce::String(u) + (u == 1 ? " update" : " updates");
	return juce::String(installed) + " of " + juce::String(n) + " installed" + kDot + "up to date";
}

void MainComponent::startJobs(std::vector<Job> jobs)
{
	if (busy() || jobs.empty())
		return;
	m_note.clear();
	m_jobs = jobs;
	m_batch = std::make_unique<Batch>(std::move(jobs), [this] { triggerAsyncUpdate(); });
	m_batch->start();
	refresh();
}

void MainComponent::update(const Entry &e)
{
	Job j;
	j.kind = Job::Kind::Update;
	j.entry = e;
	j.before = installOf(e);
	j.formats = j.before.formats();
	startJobs({j});
}

void MainComponent::install(const Entry &e, const std::vector<Format> &formats)
{
	Job j;
	j.kind = Job::Kind::Install;
	j.entry = e;
	j.formats = formats;
	startJobs({j});
}

void MainComponent::remove(const Entry &e)
{
	Job j;
	j.kind = Job::Kind::Remove;
	j.entry = e;
	j.before = installOf(e);
	startJobs({j});
}

void MainComponent::updateAll()
{
	if (!m_catalog)
		return;
	std::vector<Job> jobs;
	for (const auto &e : m_catalog->plugins)
		if (hasUpdate(e)) {
			Job j;
			j.kind = Job::Kind::Update;
			j.entry = e;
			j.before = installOf(e);
			j.formats = j.before.formats();
			jobs.push_back(j);
		}
	startJobs(std::move(jobs));
}

bool MainComponent::failed() const
{
	if (m_loading == Loading::Failed)
		return true;
	for (const auto &j : m_jobs)
		if (j.state == Job::State::Failed || j.state == Job::State::Stopped)
			return true;
	return m_self != nullptr && (m_self->state() == SelfUpdate::State::Failed || m_self->state() == SelfUpdate::State::Stopped);
}

void MainComponent::act(const juce::String &action)
{
	const auto *e = selected();
	if (action == "update-all")
		updateAll();
	else if (e != nullptr && action == "update")
		update(*e);
	else if (e != nullptr && action == "install")
		install(*e, formatsHere(e->standalone));
	else if (e != nullptr && action == "remove")
		remove(*e);
	else if (action == "confirm-remove")
		m_detail->confirmRemove();
	else if (action == "self-update")
		startSelfUpdate();
	else if (action == "later")
		hidePrompt();
}

void MainComponent::handleAsyncUpdate()
{
	if (m_batch != nullptr) {
		m_jobs = m_batch->jobs();
		if (m_batch->done()) {
			rescan();
			m_note.clear();
		}
	}
	// Updated: the new version starts and waits for this one to quit.
	if (m_self != nullptr && m_self->state() == SelfUpdate::State::Done && !m_relaunchTried) {
		m_relaunchTried = true;
		if (relaunch()) {
			m_relaunched = true;
			m_prompt->update();
			juce::JUCEApplication::getInstance()->quit();
			return;
		}
	}
	refresh();
}

void MainComponent::refresh()
{
	const bool ready = m_loading == Loading::Ready;
	m_listView->setVisible(ready);
	m_detail->setVisible(ready);
	m_retry.setVisible(m_loading == Loading::Failed);
	m_search.setEnabled(ready);

	const bool b = batchBusy();
	m_updateAll.setButtonText(b ? (m_batch->stopping() ? "Stopping" : "Stop after this one") : "Update all");
	m_updateAll.setStyle(b ? Pill::Kind::Plain : Pill::Kind::Outline);
	m_updateAll.setEnabled(b ? !m_batch->stopping() : ready && updateCount() > 0 && !busy());

	const bool newer = m_catalog && m_catalog->manager && updates::isNewer(m_catalog->manager->version, LEMUS_MANAGER_VERSION);
	m_managerUpdate.setVisible(newer);
	if (newer)
		m_managerUpdate.setButtonText("Plugin Manager " + m_catalog->manager->version + " is out");

	resized();
	m_list->setSize(m_listView->getMaximumVisibleWidth() > 0 ? m_listView->getMaximumVisibleWidth() : m_listView->getWidth(),
	                m_list->heightFor(int(visible().size())));
	m_list->repaint();
	m_detail->update();
	if (m_prompt != nullptr && m_prompt->isVisible())
		m_prompt->update();
	repaint();
}

void MainComponent::resized()
{
	auto r = getLocalBounds();
	auto header = r.removeFromTop(kHeader).reduced(20, 16).withTrimmedBottom(1);
	const auto titleWidth = juce::GlyphArrangement::getStringWidthInt(mono(16.0f, true), "Lemus")
	                        + juce::GlyphArrangement::getStringWidthInt(mono(16.0f), " Plugin Manager");
	header.removeFromLeft(titleWidth + 16);
	m_updateAll.setBounds(header.removeFromRight(m_updateAll.idealWidth() + 4));
	header.removeFromRight(16);
	const auto summaryWidth = juce::GlyphArrangement::getStringWidthInt(mono(12.0f), summary());
	header.removeFromRight(summaryWidth + 16);
	m_search.setBounds(header);

	auto fine = r.removeFromBottom(kFine);
	juce::ignoreUnused(fine);
	auto footer = r.removeFromBottom(kFooter).reduced(20, 10);
	footer.removeFromTop(1);
	const auto folderText = shownFolder();
	const auto labelWidth = juce::GlyphArrangement::getStringWidthInt(mono(12.0f), "Plug-in folder");
	const auto pathWidth = juce::jmin(360, juce::GlyphArrangement::getStringWidthInt(mono(12.0f), folderText));
	footer.removeFromLeft(labelWidth + 8 + pathWidth + 8);
	m_show.setBounds(footer.removeFromLeft(m_show.idealWidth() + 8).withSizeKeepingCentre(m_show.idealWidth() + 8, 26));
	const int checkWidth = juce::GlyphArrangement::getStringWidthInt(mono(12.5f), m_daily.getButtonText()) + 30;
	m_daily.setBounds(footer.removeFromRight(checkWidth).withSizeKeepingCentre(checkWidth, 24));
	if (m_managerUpdate.isVisible()) {
		footer.removeFromRight(12);
		m_managerUpdate.setBounds(footer.removeFromRight(m_managerUpdate.idealWidth() + 8).withSizeKeepingCentre(m_managerUpdate.idealWidth() + 8, 26));
	}

	m_detail->setBounds(r.removeFromRight(kPane));
	m_listView->setBounds(r.withTrimmedTop(8));
	m_retry.setBounds(juce::Rectangle<int>(20, kHeader + 60, m_retry.idealWidth() + 16, 32));
	if (m_prompt != nullptr)
		m_prompt->setBounds(getLocalBounds());
}

void MainComponent::paint(juce::Graphics &g)
{
	g.fillAll(col::ground);
	auto header = getLocalBounds().removeFromTop(kHeader);
	g.setColour(col::rule);
	g.fillRect(header.removeFromBottom(1));
	auto t = header.reduced(20, 16);
	g.setFont(mono(16.0f, true));
	g.setColour(col::title);
	const int lemus = juce::GlyphArrangement::getStringWidthInt(mono(16.0f, true), "Lemus");
	g.drawText("Lemus", t.removeFromLeft(lemus), juce::Justification::centredLeft, false);
	g.setFont(mono(16.0f));
	g.setColour(col::ink);
	g.drawText(" Plugin Manager", t, juce::Justification::centredLeft, false);
	const auto s = summary();
	g.setFont(mono(12.0f));
	g.setColour(m_note.isNotEmpty() ? col::warn : col::inkSoft);
	const auto sw = juce::GlyphArrangement::getStringWidthInt(mono(12.0f), s);
	g.drawText(s, juce::Rectangle<int>(m_updateAll.getX() - 16 - sw, t.getY(), sw, t.getHeight()), juce::Justification::centredRight, false);

	if (m_loading != Loading::Ready) {
		const auto body = juce::Rectangle<int>(20, kHeader + 24, getWidth() - 40, 24);
		g.setFont(mono(14.0f));
		g.setColour(m_loading == Loading::Failed ? col::warn : col::inkSoft);
		g.drawText(m_loading == Loading::Failed ? m_error : "Reading the list of plugins from " + juce::URL(catalogSource()).getDomain() + juce::String::fromUTF8("\xe2\x80\xa6"),
		           body, juce::Justification::centredLeft, true);
	}

	auto bottom = getLocalBounds().removeFromBottom(kFooter + kFine);
	g.setColour(col::panel);
	g.fillRect(bottom);
	g.setColour(col::rule);
	g.fillRect(bottom.removeFromTop(1));
	auto footer = bottom.removeFromTop(kFooter - 1).reduced(20, 0);
	g.setFont(mono(12.0f));
	g.setColour(col::muted);
	const auto labelWidth = juce::GlyphArrangement::getStringWidthInt(mono(12.0f), "Plug-in folder");
	g.drawText("Plug-in folder", footer.removeFromLeft(labelWidth), juce::Justification::centredLeft, false);
	footer.removeFromLeft(8);
	g.setColour(col::inkSoft);
	const auto folderText = shownFolder();
	g.drawText(folderText, footer.removeFromLeft(juce::jmin(360, juce::GlyphArrangement::getStringWidthInt(mono(12.0f), folderText))),
	           juce::Justification::centredLeft, true);
	g.setFont(mono(10.5f));
	g.setColour(col::muted);
	g.drawText("Machine names identify the original equipment and are trademarks of their owners. Lemus is not affiliated with or endorsed by them.",
	           bottom.reduced(20, 0).withTrimmedBottom(8), juce::Justification::centredLeft, true);
}

bool MainComponent::keyPressed(const juce::KeyPress &key)
{
	if (key == juce::KeyPress::upKey) {
		moveSelection(-1);
		return true;
	}
	if (key == juce::KeyPress::downKey) {
		moveSelection(1);
		return true;
	}
	return false;
}

} // namespace lemus::manager
