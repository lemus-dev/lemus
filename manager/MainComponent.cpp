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
		g.setFont(headerFont());
		g.setColour(col::muted);
		const char *names[] = {"MACHINE", "PLUGIN", "HAVE", "STATUS", "ROM", "ART"};
		for (int i = 0; i < 6; ++i)
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
			g.setFont(mono(13.0f, on));
			g.setColour(on ? col::ink : col::inkSoft);
			g.drawText(e.shortName(), c[0], juce::Justification::centredLeft, true);
			g.setFont(mono(13.0f));
			g.setColour(on ? col::inkSoft : col::muted);
			g.drawText(e.plugin, c[1], juce::Justification::centredLeft, true);
			const auto have = in.installed() ? in.version() : juce::String::fromUTF8("\xe2\x80\x94");
			g.drawText(have.isEmpty() ? "?" : have, c[2], juce::Justification::centredLeft, true);
			const auto s = m_owner.statusOf(e);
			g.setColour(on ? s.onSelection : s.colour);
			g.drawText(s.text, c[3], juce::Justification::centredLeft, true);
			// What its ROM folder has: [✓] all, [~] it runs without what's missing, [✗] it doesn't; — none needed.
			if (const auto *check = m_owner.romCheckOf(e)) {
				const auto rom = check->rom();
				const auto art = check->art();
				const auto dash = juce::String::fromUTF8("\xe2\x80\x94");
				g.setColour(rom == RomCheck::Rom::Ready ? col::ok : rom == RomCheck::Rom::Reduced ? col::warn : rom == RomCheck::Rom::Missing ? kError : col::muted);
				g.drawText(rom == RomCheck::Rom::Ready ? kTick : rom == RomCheck::Rom::Reduced ? juce::String("[~]") : rom == RomCheck::Rom::Missing ? kCross : dash,
				           c[4], juce::Justification::centredLeft, false);
				g.setColour(art == RomCheck::Art::Ready ? col::ok : art == RomCheck::Art::Partial ? col::warn : art == RomCheck::Art::None ? col::inkSoft : col::muted);
				g.drawText(art == RomCheck::Art::Ready ? kTick : art == RomCheck::Art::Partial ? juce::String("[~]") : art == RomCheck::Art::None ? juce::String("[ ]") : dash,
				           c[5], juce::Justification::centredLeft, false);
			}
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
	static juce::Font headerFont() { return mono(12.0f, true).withExtraKerningFactor(0.06f); }
	static int widthOf(const juce::Font &f, const juce::String &s) { return juce::GlyphArrangement::getStringWidthInt(f, s) + 2; }

	// The machine and plugin as wide as their longest names (of every plugin, so searching doesn't
	// move them), the status the rest, at least "[ ] not installed"; 44 px version, 26 px ROM and
	// ART, 6 px between (and what's spare). Short of room, the machine and plugin give it up in
	// proportion, and their names end in "...".
	std::array<juce::Rectangle<int>, 6> columns(juce::Rectangle<int> r) const
	{
		const int gap = 6, have = 44, mark = 26, free = r.getWidth() - 5 * gap - have - 2 * mark;
		int machine = widthOf(headerFont(), "MACHINE"), plugin = widthOf(headerFont(), "PLUGIN");
		if (m_owner.m_catalog)
			for (const auto &e : m_owner.m_catalog->plugins) {
				machine = std::max(machine, widthOf(mono(13.0f, true), e.shortName()));
				plugin = std::max(plugin, widthOf(mono(13.0f), e.plugin));
			}
		const int status = widthOf(mono(13.0f), "[ ] not installed");
		if (const int over = machine + plugin + status - free; over > 0) {
			const int fromMachine = over * machine / (machine + plugin);
			machine -= fromMachine;
			plugin -= over - fromMachine;
		} else {
			// Room to spare: a third each after the machine and the plugin, a third to the status.
			machine -= over / 3;
			plugin -= over / 3;
		}
		std::array<juce::Rectangle<int>, 6> c;
		c[0] = r.removeFromLeft(machine);
		r.removeFromLeft(gap);
		c[1] = r.removeFromLeft(plugin);
		r.removeFromLeft(gap);
		c[2] = r.removeFromLeft(have);
		r.removeFromLeft(gap);
		c[5] = r.removeFromRight(mark);
		r.removeFromRight(gap);
		c[4] = r.removeFromRight(mark);
		r.removeFromRight(gap);
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

namespace {

// The Manage button: a word and a drawn chevron, opening the plugin's menu.
class ManageButton final : public juce::Button {
public:
	ManageButton() : Button("Manage") { setTitle("Manage"); }
	int idealWidth() const { return juce::GlyphArrangement::getStringWidthInt(mono(12.0f), "Manage") + 38; }

	void paintButton(juce::Graphics &g, bool over, bool down) override
	{
		const auto r = getLocalBounds().toFloat();
		g.setColour(down || m_open ? col::selection : over ? col::well.brighter(0.12f) : col::well);
		g.fillRoundedRectangle(r, r.getHeight() / 2);
		g.setColour(col::ink);
		g.setFont(mono(12.0f));
		auto t = getLocalBounds().withTrimmedLeft(12).withTrimmedRight(24);
		g.drawText("Manage", t, juce::Justification::centredLeft, false);
		juce::Path chevron;
		const float cx = r.getRight() - 15, cy = r.getCentreY();
		chevron.startNewSubPath(cx - 3.5f, cy - 1.75f);
		chevron.lineTo(cx, cy + 1.75f);
		chevron.lineTo(cx + 3.5f, cy - 1.75f);
		g.setColour(col::inkSoft);
		g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
	}

	void setOpen(bool open)
	{
		m_open = open;
		repaint();
	}

private:
	bool m_open = false;
};

// The menu's look: the app's colours and font.
class MenuLook final : public juce::LookAndFeel_V4 {
public:
	MenuLook()
	{
		setColour(juce::PopupMenu::backgroundColourId, col::well);
		setColour(juce::PopupMenu::textColourId, col::ink);
		setColour(juce::PopupMenu::highlightedBackgroundColourId, col::selection);
		setColour(juce::PopupMenu::highlightedTextColourId, col::ink);
	}
	juce::Font getPopupMenuFont() override { return mono(13.0f); }
	void drawPopupMenuBackground(juce::Graphics &g, int width, int height) override
	{
		g.fillAll(col::well);
		g.setColour(col::rule);
		g.drawRect(0, 0, width, height);
	}
};

} // namespace

// The selected plugin's details: what is installed, its ROM and artwork, what's new, the work under
// way, the main button; Manage holds the rest. Everything above the buttons scrolls.
class MainComponent::Detail final : public juce::Component {
public:
	explicit Detail(MainComponent &owner) : m_owner(owner)
	{
		for (auto *b : {static_cast<juce::Component *>(&m_primary), static_cast<juce::Component *>(&m_cancel),
		                static_cast<juce::Component *>(&m_confirm), static_cast<juce::Component *>(&m_copy)})
			addChildComponent(b);
		m_view.setViewedComponent(&m_body, false);
		m_view.setScrollBarsShown(true, false);
		m_view.setScrollBarThickness(6);
		m_view.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, col::well.brighter(0.2f));
		addAndMakeVisible(m_view);
		m_body.addAndMakeVisible(m_manage);
		m_manage.onClick = [this] { showMenu(); };
		m_primary.onClick = [this] { primary(); };
		m_cancel.onClick = [this] {
			m_confirming = false;
			m_owner.refresh();
		};
		m_confirm.onClick = [this] {
			m_confirming = false;
			if (const auto *e = m_owner.selected())
				m_owner.remove(*e);
		};
		m_copy.onClick = [this] {
			if (const auto *e = m_owner.selected())
				if (const auto *j = m_owner.jobOf(*e)) {
					juce::SystemClipboard::copyTextToClipboard(detailsOf(*j));
					m_copy.setButtonText("Copied");
				}
		};
	}

	~Detail() override { m_view.setViewedComponent(nullptr, false); }

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
		m_view.setViewPosition(0, 0);
	}

	// Lays out the details for the current state; the body paints m_items.
	void update()
	{
		m_body.items.clear();
		m_bottom.clear();
		m_checks.clear();
		for (auto *b : {&m_primary, &m_cancel, &m_confirm, &m_copy})
			b->setVisible(false);
		const auto *e = m_owner.selected();
		m_manage.setVisible(e != nullptr);
		if (e == nullptr) {
			m_body.setSize(getWidth(), 0);
			repaint();
			return;
		}
		const float x = 20, w = float(getWidth()) - 40;
		float y = 20;
		const int manageWidth = m_manage.idealWidth();
		m_manage.setBounds(int(x + w) - manageWidth, int(y) - 2, manageWidth, 26);
		y += text(e->emulates, mono(16.0f, true), col::ink, x, y, w - float(manageWidth) - 10) + 4;
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
			const auto upToDate = job == nullptr && !update ? kDot + "up to date" : juce::String();
			y += text(in.version() + kDot + formatsText(in.formats()) + upToDate, mono(13.0f), col::ink, x, y, w) + 16;
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
			y = notes(changes, x, y, w) + 4;
		}

		y = romBlock(*e, x, y, w);

		if (!in.installed() && job == nullptr) {
			label("INSTALL " + e->version, x, y);
			y += 24;
			const auto formats = formatsHere(e->standalone);
			const float wellHeight = 12 + float(formats.size()) * 30;
			m_body.items.push_back({Item::Kind::Well, {}, {}, {}, {x, y, w, wellHeight}});
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
				m_body.addAndMakeVisible(*c);
				m_checks.push_back(std::move(c));
				cy += 30;
			}
			y += wellHeight + 10;
			// Without the list of ROMs (roms.json didn't read): what the site says.
			if (m_owner.needsOf(*e) == nullptr) {
				if (e->withoutRom == "none")
					y += text("Runs only with its ROM. Lemus ships no ROMs; the plugin names the file it needs.", mono(13.0f), col::inkSoft, x, y, w) + 8;
				else if (e->withoutRom == "voice")
					y += text("Without its ROM it runs in voice mode. Lemus ships no ROMs.", mono(13.0f), col::inkSoft, x, y, w) + 8;
			}
		}

		// The buttons, from the bottom up.
		float bottom = float(getHeight()) - 20;
		const bool finished = job == nullptr || job->finished();
		if (!busy && finished) {
			bottom -= 16;
			m_bottom.push_back({Item::Kind::Text, permissionLine(), mono(12.0f), col::inkSoft, {x, bottom, w, 16}});
			bottom -= 8;
			const int by = int(bottom - 40);
			auto row = juce::Rectangle<int>(int(x), by, int(w), 40);
			if (m_confirming && in.installed()) {
				const auto ask = "Remove " + e->plugin + " " + in.version() + "? Its " + formatsText(in.formats())
				                 + " go. Your ROMs, artwork and songs stay.";
				const float h = layoutOf(ask, mono(13.0f), col::ink, w).getHeight();
				m_bottom.push_back({Item::Kind::Text, ask, mono(13.0f), col::ink, {x, float(by) - 10 - h, w, h}});
				m_confirm.setBounds(row.removeFromRight(m_confirm.idealWidth() + 16));
				row.removeFromRight(8);
				m_cancel.setBounds(row.removeFromRight(m_cancel.idealWidth() + 16));
				m_confirm.setVisible(true);
				m_cancel.setVisible(true);
				bottom = float(by) - 20 - h;
			} else {
				const bool failedJob = job != nullptr && job->state == Job::State::Failed;
				// A failed job: its details to paste into a report.
				if (failedJob) {
					if (!m_copied.isEmpty() && m_copied != e->plugin + job->error)
						m_copy.setButtonText("Copy details");
					m_copied = e->plugin + job->error;
					m_copy.setBounds(row.removeFromLeft(m_copy.idealWidth() + 20));
					m_copy.setVisible(true);
					row.removeFromLeft(8);
				}
				if (update || !in.installed()) {
					m_primary.setButtonText(update ? "Update to " + e->version : "Install " + e->version);
					m_primary.setEnabled(update || !m_ticked.empty());
					m_primary.setBounds(row);
					m_primary.setVisible(true);
				}
				if (m_primary.isVisible() || failedJob)
					bottom = float(by) - 12;
			}
		}
		m_view.setBounds(0, 0, getWidth(), juce::jmax(0, int(bottom)));
		m_body.setSize(m_view.getMaximumVisibleWidth() > 0 ? m_view.getMaximumVisibleWidth() : getWidth(), int(y + 12));
		m_body.repaint();
		repaint();
	}

	void paint(juce::Graphics &g) override
	{
		g.fillAll(col::panel);
		Body::draw(g, m_bottom);
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

	// What scrolls: the items, the format checks, Manage.
	struct Body final : public juce::Component {
		std::vector<Item> items;
		void paint(juce::Graphics &g) override
		{
			g.fillAll(col::panel);
			draw(g, items);
		}
		static void draw(juce::Graphics &g, const std::vector<Item> &list)
		{
			for (const auto &i : list) {
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
		m_body.items.push_back({Item::Kind::Text, t, font, colour, {x, y, w, h}});
		return h;
	}

	void label(const juce::String &t, float x, float y) { m_body.items.push_back({Item::Kind::Label, t, {}, {}, {x, y, 320, 16}}); }

	// Each version's notes under its number.
	float notes(const std::vector<updates::Change> &changes, float x, float y, float w)
	{
		for (const auto &c : changes) {
			if (changes.size() > 1) {
				text(c.version, mono(13.0f, true), col::ink, x, y, w);
				y += 20;
			}
			for (const auto &n : c.notes) {
				text(juce::String::fromUTF8("\xc2\xb7"), mono(13.0f), col::muted, x, y, 12);
				y += text(n, mono(13.0f), col::inkSoft, x + 16, y, w - 16) + 6;
			}
			y += 8;
		}
		return y;
	}

	// The plugin's ROMs and artwork: what it needs, what its ROM folder has, what's missing.
	float romBlock(const Entry &e, float x, float y, float w)
	{
		const auto *needs = m_owner.needsOf(e);
		if (needs == nullptr || (needs->roms.empty() && needs->artwork.places.empty()))
			return y;
		const auto *check = m_owner.romCheckOf(e);
		if (check == nullptr) {
			label(needs->roms.empty() ? "ARTWORK" : "ROM", x, y);
			y += 20;
			return y + text("Checking the ROM folder" + juce::String::fromUTF8("\xe2\x80\xa6"), mono(13.0f), col::muted, x, y, w) + 16;
		}
		bool missing = false;
		if (!check->images.empty()) {
			label(check->images.size() > 1 ? "ROMS" : "ROM", x, y);
			y += 22;
			const float top = y;
			y += 8;
			const bool full = check->images.size() <= 2; // a few: everything about each; more: Manage copies the rest
			for (const auto &i : check->images) {
				const auto mark = i.found ? kTick : i.spec.optional ? juce::String("[ ]") : kCross;
				const auto colour = i.found ? col::ok : col::ink;
				text(mark + " " + i.spec.name, mono(13.0f), colour, x + 12, y, w - 110);
				const auto state = i.found ? juce::String("found") : juce::String("not found");
				const auto stateColour = i.found ? col::ok : i.spec.optional ? col::warn : kError;
				text(state, mono(12.0f), stateColour, x + w - 92, y + 1, 80);
				y += 21;
				if (i.found)
					continue;
				missing = true;
				if (full) {
					y += text(i.spec.what + kDot + sizeText(i.spec.size) + kDot + "CRC " + i.spec.crc32, mono(11.5f), col::inkSoft, x + 12, y, w - 24);
					y += text("SHA1 " + i.spec.sha1, mono(10.5f), col::inkSoft, x + 12, y, w - 20) + 2;
					for (const auto &a : i.spec.alternatives) {
						y += text("or " + a.name + " (" + a.label + ")" + kDot + sizeText(a.size) + kDot + "CRC " + a.crc32, mono(11.5f), col::inkSoft, x + 12, y, w - 24);
						y += text("SHA1 " + a.sha1, mono(10.5f), col::inkSoft, x + 12, y, w - 20) + 2;
					}
				}
				y += text(i.spec.optional ? "Without it: " + i.spec.without + "." : juce::String("It doesn't run without it."), mono(12.0f),
				          i.spec.optional ? col::warn : kError, x + 12, y, w - 24)
				     + 6;
			}
			if (missing && !full)
				y += text("Manage " + juce::String::fromUTF8("\xe2\x80\xba") + " Copy ROM details lists their sizes, CRCs and SHA1s.",
				          mono(11.5f), col::muted, x + 12, y, w - 24)
				     + 4;
			y += 6;
			m_body.items.insert(m_body.items.end() - countSince(top), {Item::Kind::Well, {}, {}, {}, {x, top, w, y - top}});
			y += 14;
		}
		if (!check->places.empty()) {
			const auto art = check->art();
			label(juce::String("ARTWORK") + kDot + (art == RomCheck::Art::Ready ? "READY" : art == RomCheck::Art::Partial ? "PARTIAL" : "NONE"), x, y);
			y += 22;
			const float top = y;
			y += 6;
			const float colW = (w - 24) / 2;
			for (size_t i = 0; i < check->places.size(); ++i) {
				const auto &p = check->places[i];
				const bool has = p.where.isNotEmpty();
				missing = missing || !has;
				text((has ? kTick : juce::String("[ ]")) + " " + p.place.id, mono(13.0f), has ? col::ok : col::inkSoft,
				     x + 12 + float(i % 2) * colW, y + float(i / 2) * 20, colW);
			}
			y += float((check->places.size() + 1) / 2) * 20 + 6;
			m_body.items.insert(m_body.items.end() - int(check->places.size()), {Item::Kind::Well, {}, {}, {}, {x, top, w, y - top}});
			y += 8;
		}
		if (missing) {
			// A real name as the example ("<place>" reads as an arrow in JetBrains Mono: -< is a ligature).
			juce::String example = check->places.empty() ? juce::String() : check->places[0].place.id;
			for (const auto &p : check->places)
				if (p.where.isEmpty()) {
					example = p.place.id; // the first one missing
					break;
				}
			const auto artName = check->places.empty() ? juce::String() : " Artwork files are named for their place, like " + e.plugin + "-" + example + ".svg or .png.";
			const auto what = check->images.empty() ? juce::String("the artwork") : check->places.empty() ? juce::String("the ROM") : juce::String("the ROM or artwork");
			y += text("Drop " + what + " on this window, or put " + (check->images.size() + check->places.size() > 1 ? "them" : "it") + " in the ROM folder." + artName,
			          mono(12.0f), col::muted, x, y, w)
			     + 16;
		} else {
			y += 8;
		}
		return y;
	}

	int countSince(float top) const
	{
		int n = 0;
		for (auto it = m_body.items.rbegin(); it != m_body.items.rend() && it->area.getY() >= top; ++it)
			++n;
		return n;
	}

	static juce::String sizeText(size_t size)
	{
		return size >= 1024 * 1024 ? juce::String(int(size / (1024 * 1024))) + " MB" : size >= 1024 ? juce::String(int(size / 1024)) + " KB" : juce::String(int(size)) + " bytes";
	}

	void showMenu()
	{
		const auto *e = m_owner.selected();
		if (e == nullptr)
			return;
		const auto &in = m_owner.installOf(*e);
		const auto *needs = m_owner.needsOf(*e);
		const bool busy = m_owner.busy();
		juce::PopupMenu menu;
		menu.setLookAndFeel(&m_menuLook);
		menu.addItem("Its page " + juce::String::fromUTF8("\xe2\x86\x97"), [page = e->page] { juce::URL(page).launchInDefaultBrowser(); });
#if JUCE_WINDOWS
		const char *show = "Show in Explorer";
#else
		const char *show = "Show in Finder";
#endif
		if (in.installed()) {
			const auto file = in.copies[0].file;
			menu.addItem(show, [file] { file.revealToUser(); });
		}
		if (needs != nullptr && (!needs->roms.empty() || !needs->artwork.places.empty())) {
			menu.addItem("Show ROM folder", [machine = needs->machine] {
				const auto folder = romfolder::location(machine);
				folder.createDirectory();
				folder.startAsProcess();
			});
			if (!needs->roms.empty())
				menu.addItem("Copy ROM details", [this, plugin = e->plugin] {
					if (const auto *entry = m_owner.m_catalog ? m_owner.m_catalog->find(plugin) : nullptr)
						juce::SystemClipboard::copyTextToClipboard(romDetails(*entry));
				});
		}
		if (in.installed()) {
			menu.addSeparator();
			juce::PopupMenu::Item remove("Remove" + juce::String::fromUTF8("\xe2\x80\xa6"));
			remove.setColour(kError).setEnabled(!busy).setAction([this] { confirmRemove(); });
			menu.addItem(std::move(remove));
		}
		m_manage.setOpen(true);
		juce::Component::SafePointer<Detail> self(this);
		menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_manage).withMinimumWidth(200), [self](int) {
			if (self != nullptr)
				self->m_manage.setOpen(false);
		});
	}

	// For a report or a search of one's own: each ROM's name, size, CRC and SHA1, found or not.
	juce::String romDetails(const Entry &e) const
	{
		juce::String s = e.plugin + " (" + e.emulates + ") ROMs:\n";
		if (const auto *check = m_owner.romCheckOf(e))
			for (const auto &i : check->images) {
				s << i.spec.name << "  " << juce::String(juce::int64(i.spec.size)) << " bytes  CRC " << i.spec.crc32 << "  SHA1 " << i.spec.sha1
				  << (i.found ? "  found" : "  not found") << (i.spec.optional ? "" : "  (needed)") << "\n";
				for (const auto &a : i.spec.alternatives)
					s << "  or " << a.name << " (" << a.label << ")  " << juce::String(juce::int64(a.size)) << " bytes  CRC " << a.crc32 << "  SHA1 " << a.sha1 << "\n";
			}
		return s;
	}

	// The steps of the work on this plugin, as they go.
	float jobBlock(const Entry &e, const Job &job, float x, float y, float w)
	{
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
			m_body.items.push_back({Item::Kind::Text, line, mono(13.0f), colour, {x + 12, y, w - 24, 19}});
			y += 22;
			if (step.name == "Download" && s == Job::State::Downloading) {
				m_body.items.push_back({Item::Kind::Bar, juce::String(job.progress), {}, {}, {x + 12, y, w - 24, 4}});
				y += 10;
			}
		}
		y += 4;
		m_body.items.insert(m_body.items.end() - int(steps.size()) - (s == Job::State::Downloading ? 1 : 0),
		                    {Item::Kind::Well, {}, {}, {}, {x, top, w, y - top}});
		y += 12;
		if (failed || stopped)
			y += text(job.error, mono(13.0f), failed ? kError : col::warn, x, y, w) + 12;
		else if (job.kind == Job::Kind::Update && s != Job::State::Waiting)
			y += text("A DAW that has " + e.plugin + " open keeps playing " + job.before.version() + " until it loads the plugin again.",
			          mono(13.0f), col::inkSoft, x, y, w)
			     + 12;
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
	Body m_body;
	juce::Viewport m_view;
	std::vector<Item> m_bottom; // drawn below the scrolling part: the confirm question, the permission line
	std::vector<std::unique_ptr<Check>> m_checks;
	std::set<Format> m_ticked;
	bool m_confirming = false;
	int m_failedAt = 2;
	juce::String m_failedJob;
	ManageButton m_manage;
	MenuLook m_menuLook;
	Pill m_primary{{}, Pill::Kind::Primary, col::accent, 14.0f};
	Pill m_cancel{"Cancel"};
	Pill m_confirm{"Remove", Pill::Kind::Primary};
	Pill m_copy{"Copy details"};
	juce::String m_copied; // the failure the button's "Copied" is for
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

const Needs *MainComponent::needsOf(const Entry &e) const
{
	if (!m_needs)
		return nullptr;
	const auto it = m_needs->find(e.plugin);
	return it != m_needs->end() ? &it->second : nullptr;
}

const RomCheck *MainComponent::romCheckOf(const Entry &e) const
{
	const auto it = m_romChecks.find(e.plugin);
	return it != m_romChecks.end() ? &it->second : nullptr;
}

void MainComponent::checkRoms()
{
	if (!m_needs || !m_catalog)
		return;
	juce::WeakReference<MainComponent> self(this);
	juce::Thread::launch([self, needs = *m_needs, catalog = *m_catalog] {
		std::map<juce::String, RomCheck> checks;
		for (const auto &e : catalog.plugins)
			if (const auto it = needs.find(e.plugin); it != needs.end())
				checks[e.plugin] = check(it->second);
		juce::MessageManager::callAsync([self, checks = std::move(checks)] {
			if (auto *me = self.get()) {
				me->m_romChecks = checks;
				me->refresh();
			}
		});
	});
}

void MainComponent::say(const juce::String &note, bool ok)
{
	m_note = note;
	m_noteOk = ok;
	refresh();
	juce::WeakReference<MainComponent> self(this);
	juce::Timer::callAfterDelay(8000, [self, note] {
		if (auto *me = self.get(); me != nullptr && me->m_note == note) {
			me->m_note.clear();
			me->refresh();
		}
	});
}

void MainComponent::selectWhenLoaded(const juce::String &plugin)
{
	m_selectWhenLoaded = plugin;
	if (m_catalog && m_catalog->find(plugin) != nullptr) {
		select(plugin);
		refresh();
	}
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray &)
{
	return m_loading == Loading::Ready;
}

void MainComponent::fileDragEnter(const juce::StringArray &, int, int)
{
	m_dragging = true;
	repaint();
}

void MainComponent::fileDragExit(const juce::StringArray &)
{
	m_dragging = false;
	repaint();
}

void MainComponent::filesDropped(const juce::StringArray &files, int, int)
{
	m_dragging = false;
	repaint();
	if (!m_needs) {
		say("The list of ROMs didn't read, so nothing can be matched.", false);
		return;
	}
	say("Looking through what was dropped" + juce::String::fromUTF8("\xe2\x80\xa6"), true);
	juce::WeakReference<MainComponent> self(this);
	juce::Thread::launch([self, files, needs = *m_needs] {
		const auto added = addDropped(files, needs);
		bool fresh = false;
		for (const auto &a : added)
			fresh = fresh || !a.already;
		juce::MessageManager::callAsync([self, note = describe(added), fresh] {
			if (auto *me = self.get()) {
				me->say(note, fresh);
				me->checkRoms();
			}
		});
	});
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
	m_noteOk = false;
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
		std::vector<juce::String> plugins;
		if (catalog)
			for (const auto &e : catalog->plugins) {
				installed[e.plugin] = scan(e.plugin);
				plugins.push_back(e.plugin);
			}
		const bool read = catalog.has_value();
		juce::MessageManager::callAsync([self, catalog = std::move(catalog), found = std::move(installed), error] {
			if (auto *me = self.get()) {
				me->m_catalog = catalog;
				me->m_installed = found;
				me->m_error = error;
				me->m_loading = catalog ? Loading::Ready : Loading::Failed;
				if (catalog && me->m_selectWhenLoaded.isNotEmpty() && catalog->find(me->m_selectWhenLoaded) != nullptr)
					me->m_selected = me->m_selectWhenLoaded;
				if (catalog && me->selected() == nullptr && !catalog->plugins.empty()) {
					// Start on the first with an update, else the first.
					me->m_selected = catalog->plugins[0].plugin;
					for (const auto &e : catalog->plugins)
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
		if (!read)
			return;
		// Then each plugin's ROM folder: what it needs (roms.json) and what the folder has.
		auto needs = fetchNeeds();
		std::map<juce::String, RomCheck> checks;
		if (needs)
			for (const auto &plugin : plugins)
				if (const auto it = needs->find(plugin); it != needs->end())
					checks[plugin] = check(it->second);
		juce::MessageManager::callAsync([self, needs = std::move(needs), checks = std::move(checks)] {
			if (auto *me = self.get()) {
				me->m_needs = needs;
				me->m_romChecks = checks;
				me->refresh();
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
	else if (action == "drag")
		fileDragEnter({}, 0, 0);
	else if (action.startsWith("drop:"))
		filesDropped(juce::StringArray::fromTokens(action.fromFirstOccurrenceOf("drop:", false, false), "|", ""), 0, 0);
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
	g.setColour(m_note.isEmpty() ? col::inkSoft : m_noteOk ? col::ok : col::warn);
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

void MainComponent::paintOverChildren(juce::Graphics &g)
{
	if (!m_dragging)
		return;
	g.setColour(col::ground.withAlpha(0.92f));
	g.fillAll();
	const auto frame = getLocalBounds().toFloat().reduced(14.0f);
	juce::Path outline;
	outline.addRoundedRectangle(frame, 12.0f);
	juce::Path dashed;
	const float dashes[] = {8.0f, 6.0f};
	juce::PathStrokeType(2.0f).createDashedStroke(dashed, outline, dashes, 2);
	g.setColour(col::accent);
	g.fillPath(dashed);
	const float w = 480, cx = frame.getCentreX();
	auto y = frame.getCentreY() - 60;
	juce::Path arrow;
	arrow.startNewSubPath(cx, y);
	arrow.lineTo(cx, y + 22);
	arrow.startNewSubPath(cx - 8, y + 14);
	arrow.lineTo(cx, y + 22);
	arrow.lineTo(cx + 8, y + 14);
	arrow.startNewSubPath(cx - 13, y + 30);
	arrow.lineTo(cx + 13, y + 30);
	g.strokePath(arrow, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
	y += 46;
	g.setColour(col::title);
	g.setFont(mono(18.0f, true));
	g.drawText("Drop ROMs and artwork", juce::Rectangle<float>(cx - w / 2, y, w, 26), juce::Justification::centred, false);
	y += 34;
	juce::AttributedString s;
	s.append("Each file is matched by its SHA1 (ROMs) or its name (artwork) and copied into its plugin's ROM folder. Zips and folders too; anything else is left alone.",
	         mono(13.0f), col::inkSoft);
	s.setJustification(juce::Justification::centredTop);
	s.setWordWrap(juce::AttributedString::byWord);
	juce::TextLayout layout;
	layout.createLayout(s, w);
	layout.draw(g, {cx - w / 2, y, w, layout.getHeight()});
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
