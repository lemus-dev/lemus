#include "LemusEditor.h"
#include "Style.h"
#include "Updates.h"

#include <algorithm>

namespace {

using lemus::style::mono;
namespace col = lemus::style::colours;

const juce::String kDisclaimer = "Machine names identify the original equipment and are trademarks of their owners. "
                                 "Lemus is not affiliated with or endorsed by them.";

constexpr int kRowHeight = 32;
constexpr int kDetailWidth = 330;
constexpr int kHeaderHeight = 64;
constexpr int kFooterHeight = 80;
constexpr int kYearWidth = 56, kStatusWidth = 170, kColumnGap = 12;

juce::String utf8(const char *s)
{
	return juce::String(juce::CharPointer_UTF8(s));
}

juce::String sizeText(size_t bytes)
{
	return bytes >= 1024 ? juce::String(bytes / 1024) + " KB" : juce::String(bytes) + " bytes";
}

// The year a machine came out, from the end of its description: "... (1981)".
juce::String yearOf(const lemus::MachineInfo &m)
{
	const auto d = m.description.trim();
	if (d.endsWithChar(')') && d.length() >= 6) {
		const auto y = d.substring(d.length() - 5, d.length() - 1);
		if (y.containsOnly("0123456789"))
			return y;
	}
	return {};
}

// Wrapped text: laid out at a width, drawn from the top of an area; returns its height.
juce::TextLayout layoutOf(const juce::String &text, const juce::Font &font, juce::Colour colour, float width)
{
	juce::AttributedString s;
	s.append(text, font, colour);
	juce::TextLayout l;
	l.createLayout(s, width);
	return l;
}

// A folder with the home folder as ~, so the part that matters fits.
juce::String shortPath(const juce::File &f)
{
	const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName();
	const auto path = f.getFullPathName();
	return path.startsWith(home + juce::File::getSeparatorString()) ? "~" + path.substring(home.length()) : path;
}

float drawWrapped(juce::Graphics &g, const juce::String &text, const juce::Font &font, juce::Colour colour, juce::Rectangle<float> area)
{
	const auto l = layoutOf(text, font, colour, area.getWidth());
	l.draw(g, area.withHeight(l.getHeight()));
	return l.getHeight();
}

// JUCE's own widgets (the search field, check boxes, call-out boxes) in the Lemus look.
class LemusLook final : public juce::LookAndFeel_V4 {
public:
	LemusLook()
	{
		setColour(juce::TextEditor::backgroundColourId, col::well);
		setColour(juce::TextEditor::textColourId, col::ink);
		setColour(juce::TextEditor::highlightColourId, col::selection);
		setColour(juce::TextEditor::highlightedTextColourId, col::ink);
		setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
		setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
		setColour(juce::CaretComponent::caretColourId, col::accent);
		setColour(juce::ScrollBar::thumbColourId, col::well);
		setColour(juce::ToggleButton::textColourId, col::inkSoft);
		setColour(juce::ToggleButton::tickColourId, col::accent);
		setColour(juce::ToggleButton::tickDisabledColourId, col::muted);
		setColour(juce::HyperlinkButton::textColourId, col::accent);
		setColour(juce::Label::textColourId, col::ink);
	}

	juce::Typeface::Ptr getTypefaceForFont(const juce::Font &f) override { return mono(f.getHeight(), f.isBold()).getTypefacePtr(); }

	void drawCallOutBoxBackground(juce::CallOutBox &, juce::Graphics &g, const juce::Path &path, juce::Image &) override
	{
		g.setColour(col::panel);
		g.fillPath(path);
		g.setColour(col::rule);
		g.strokePath(path, juce::PathStrokeType(1.0f));
	}
	int getCallOutBoxBorderSize(const juce::CallOutBox &) override { return 18; }

	void fillTextEditorBackground(juce::Graphics &g, int w, int h, juce::TextEditor &) override
	{
		g.setColour(col::well);
		g.fillRoundedRectangle(juce::Rectangle<float>(0, 0, float(w), float(h)), float(h) / 2);
	}
	void drawTextEditorOutline(juce::Graphics &, int, int, juce::TextEditor &) override {}
};

// A rounded button: on `well`, filled with a colour (primary), or outlined in one.
class Pill final : public juce::Button {
public:
	enum class Kind { Plain, Primary, Outline };

	explicit Pill(const juce::String &text, Kind kind = Kind::Plain, juce::Colour tint = col::accent, float size = 12.0f)
		: Button(text), m_kind(kind), m_tint(tint), m_size(size)
	{
		setMouseCursor(juce::MouseCursor::PointingHandCursor);
	}

	int idealWidth() const { return juce::GlyphArrangement::getStringWidthInt(font(), getButtonText()) + int(m_size * 2.4f); }

	void paintButton(juce::Graphics &g, bool over, bool down) override
	{
		const auto r = getLocalBounds().toFloat().reduced(0.5f);
		const float radius = r.getHeight() / 2;
		const bool on = isEnabled();
		if (m_kind == Kind::Outline) {
			g.setColour(m_tint.withAlpha(down ? 0.28f : over ? 0.18f : 0.1f));
			g.fillRoundedRectangle(r, radius);
			g.setColour(m_tint.withAlpha(on ? 0.75f : 0.3f));
			g.drawRoundedRectangle(r, radius, 1.0f);
			g.setColour(on ? m_tint : col::muted);
		} else {
			auto fill = m_kind == Kind::Primary && on ? m_tint : col::well;
			if (on && down)
				fill = fill.darker(0.2f);
			else if (on && over)
				fill = fill.brighter(0.12f);
			g.setColour(fill);
			g.fillRoundedRectangle(r, radius);
			g.setColour(!on ? col::muted : m_kind == Kind::Primary ? col::onAccent : col::ink);
		}
		g.setFont(font());
		g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
	}

private:
	juce::Font font() const { return mono(m_size, m_kind == Kind::Primary); }

	Kind m_kind;
	juce::Colour m_tint;
	float m_size;
};

// A check box with its label, in the Lemus look.
class Check final : public juce::Button {
public:
	explicit Check(const juce::String &text) : Button(text)
	{
		setClickingTogglesState(true);
		setMouseCursor(juce::MouseCursor::PointingHandCursor);
	}

	void paintButton(juce::Graphics &g, bool over, bool) override
	{
		const auto box = getLocalBounds().removeFromLeft(getHeight()).toFloat().withSizeKeepingCentre(16, 16);
		g.setColour(getToggleState() ? col::accent : over ? col::well.brighter(0.12f) : col::well);
		g.fillRoundedRectangle(box, 4.0f);
		if (getToggleState()) {
			juce::Path tick;
			tick.startNewSubPath(box.getX() + 4, box.getCentreY());
			tick.lineTo(box.getX() + 7, box.getBottom() - 4.5f);
			tick.lineTo(box.getRight() - 4, box.getY() + 4.5f);
			g.setColour(col::onAccent);
			g.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
		}
		g.setColour(col::inkSoft);
		g.setFont(mono(12.5f));
		g.drawText(getButtonText(), getLocalBounds().withTrimmedLeft(getHeight() + 4), juce::Justification::centredLeft, true);
	}
};

// A machine in the picker, with what the ROM folder has for it.
struct Entry {
	const lemus::MachineInfo *info = nullptr;
	std::vector<lemus::RomSpec> missing; // its ROMs the folder doesn't have, optional ones included
	bool artwork = false;                 // its artwork pack is there

	bool loadable() const { return !LemusProcessor::anyRequired(missing); }
	bool found(const lemus::RomSpec &r) const
	{
		return std::none_of(missing.begin(), missing.end(), [&](const lemus::RomSpec &m) { return m.name == r.name; });
	}
	const lemus::RomSpec *firstRequired() const
	{
		for (const auto &r : missing)
			if (!r.optional)
				return &r;
		return nullptr;
	}
	juce::String status() const
	{
		if (!loadable())
			return "[ ] needs ROM";
		for (const auto &r : missing)
			if (r.optional)
				return "[~] " + (r.mode.isNotEmpty() ? r.mode.toLowerCase() : "without " + r.name);
		return utf8("[\xe2\x9c\x93] ready");
	}
	juce::Colour statusColour() const { return !loadable() ? col::muted : missing.empty() ? col::ok : col::warn; }
};

// The picker's table columns in a row's area: name, year, ROM status.
struct Columns {
	juce::Rectangle<int> name, year, status;
};

Columns columnsIn(juce::Rectangle<int> row)
{
	Columns c;
	c.status = row.removeFromRight(kStatusWidth);
	row.removeFromRight(kColumnGap);
	c.year = row.removeFromRight(kYearWidth);
	row.removeFromRight(kColumnGap);
	c.name = row;
	return c;
}

// The rows of the picker's table (in a viewport): click selects, double-click loads.
class MachineList final : public juce::Component {
public:
	std::function<void(int)> onSelect;
	std::function<void()> onLoad;

	void setEntries(std::vector<const Entry *> entries, int selected)
	{
		m_entries = std::move(entries);
		m_selected = selected;
		setSize(getWidth(), juce::jmax(1, int(m_entries.size())) * kRowHeight);
		repaint();
	}

	void select(int i)
	{
		m_selected = i;
		repaint();
		auto *view = findParentComponentOfClass<juce::Viewport>();
		if (i >= 0 && view != nullptr) {
			const auto r = rowBounds(i);
			const int top = view->getViewPositionY();
			if (r.getY() < top)
				view->setViewPosition(0, r.getY());
			else if (r.getBottom() > top + view->getViewHeight())
				view->setViewPosition(0, r.getBottom() - view->getViewHeight());
		}
	}

	void paint(juce::Graphics &g) override
	{
		if (m_entries.empty()) {
			g.setColour(col::muted);
			g.setFont(mono(14.0f));
			g.drawText("No machine matches.", getLocalBounds().withTrimmedLeft(20), juce::Justification::centredLeft, false);
			return;
		}
		for (int i = 0; i < int(m_entries.size()); ++i) {
			const auto &e = *m_entries[size_t(i)];
			const auto r = rowBounds(i);
			if (i == m_selected || i == m_hover) {
				g.setColour(i == m_selected ? col::selection : col::ink.withAlpha(0.06f));
				g.fillRoundedRectangle(r.toFloat(), 6.0f);
			}
			const auto c = columnsIn(r.reduced(12, 0));
			const bool on = e.loadable();
			g.setFont(mono(14.0f, i == m_selected));
			g.setColour(on ? col::ink : col::inkSoft);
			g.drawText(e.info->name, c.name, juce::Justification::centredLeft, true);
			g.setFont(mono(14.0f));
			g.setColour(on ? col::inkSoft : col::muted);
			g.drawText(yearOf(*e.info), c.year, juce::Justification::centredLeft, false);
			g.setColour(e.statusColour());
			g.drawText(e.status(), c.status, juce::Justification::centredLeft, true);
		}
	}

	void mouseDown(const juce::MouseEvent &e) override
	{
		const int i = rowAt(e.y);
		if (i >= 0 && onSelect)
			onSelect(i);
	}
	void mouseDoubleClick(const juce::MouseEvent &e) override
	{
		if (rowAt(e.y) >= 0 && onLoad)
			onLoad();
	}
	void mouseMove(const juce::MouseEvent &e) override { setHover(rowAt(e.y)); }
	void mouseExit(const juce::MouseEvent &) override { setHover(-1); }

private:
	juce::Rectangle<int> rowBounds(int i) const { return {8, i * kRowHeight, getWidth() - 16, kRowHeight}; }
	int rowAt(int y) const
	{
		const int i = y / kRowHeight;
		return y >= 0 && i < int(m_entries.size()) ? i : -1;
	}
	void setHover(int i)
	{
		if (i != m_hover) {
			m_hover = i;
			repaint();
		}
	}

	std::vector<const Entry *> m_entries;
	int m_selected = -1, m_hover = -1;
};

// The search field: typing filters, the arrow keys move through the list, Return loads.
class SearchField final : public juce::TextEditor {
public:
	std::function<void(int)> onArrow;
	bool keyPressed(const juce::KeyPress &key) override
	{
		if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
			if (onArrow)
				onArrow(key == juce::KeyPress::upKey ? -1 : 1);
			return true;
		}
		return TextEditor::keyPressed(key);
	}
};

// The About box: the host and machine, the machine's notes, the disclaimer, links to the website
// and the update-check switch.
class AboutBox final : public juce::Component {
public:
	AboutBox(const lemus::MachineInfo &m, const juce::String &notes)
	{
		setLookAndFeel(&m_look.get());
		m_text = juce::String(JucePlugin_Name) + " " + JucePlugin_VersionString + "\n" + m.name + " emulation (" + m.description + ")\n\n" +
		         (notes.isNotEmpty() ? notes + "\n\n" : juce::String()) + kDisclaimer;

		const juce::String site = LEMUS_SITE;
		m_site.setButtonText(site.fromFirstOccurrenceOf("://", false, false));
		m_site.setURL(juce::URL(site));
#ifdef LEMUS_MACHINE
		// A machine's own plugin has its guide at /<plugin name>/guide; the all-in-one hosts don't.
		m_guide.setButtonText("User guide");
		m_guide.setURL(juce::URL(site + "/" + JucePlugin_Name + "/guide"));
		m_links.add(&m_guide);
#endif
		m_support.setButtonText("Help and support");
		m_support.setURL(juce::URL(site + "/support"));
		m_licences.setButtonText("Credits and licences");
		m_licences.setURL(juce::URL(site + "/licences"));
		m_links.addArray({&m_site, &m_support, &m_licences});
		for (auto *b : m_links) {
			b->setFont(mono(13.0f), false, juce::Justification::centredLeft);
			addAndMakeVisible(*b);
		}

		m_updates.setToggleState(lemus::updates::enabled(), juce::dontSendNotification);
		m_updates.onClick = [this] { lemus::updates::setEnabled(m_updates.getToggleState()); };
		addAndMakeVisible(m_updates);

		m_textHeight = int(std::ceil(layoutOf(m_text, mono(13.0f), col::ink, float(kWidth)).getHeight())) + 12;
		setSize(kWidth + 2 * kPad, m_textHeight + m_links.size() * 22 + 34 + 2 * kPad);
	}
	~AboutBox() override { setLookAndFeel(nullptr); }

	void paint(juce::Graphics &g) override { drawWrapped(g, m_text, mono(13.0f), col::ink, getLocalBounds().reduced(kPad).toFloat()); }

	void resized() override
	{
		auto b = getLocalBounds().reduced(kPad);
		b.removeFromTop(m_textHeight);
		for (auto *l : m_links)
			l->setBounds(b.removeFromTop(22).withWidth(240).withTrimmedLeft(-4));
		b.removeFromTop(6);
		m_updates.setBounds(b.removeFromTop(26));
	}

private:
	static constexpr int kWidth = 380, kPad = 14;
	juce::SharedResourcePointer<LemusLook> m_look;
	juce::String m_text;
	juce::HyperlinkButton m_guide, m_site, m_support, m_licences;
	juce::Array<juce::HyperlinkButton *> m_links; // in the order shown
	Check m_updates{"Check for updates once a day"};
	int m_textHeight = 0;
};

// Adding an optional ROM the machine runs without: what it is, where it goes, and a rescan that
// reloads the machine with it in place, keeping the session.
class RomPopover final : public juce::Component {
public:
	RomPopover(LemusProcessor &proc, std::function<void(const juce::String &)> onReloaded) : m_proc(proc), m_onReloaded(std::move(onReloaded))
	{
		setLookAndFeel(&m_look.get());
		m_roms = proc.romsWithout();
		m_folder = lemus::romfolder::location();
		m_show.onClick = [this] {
			m_folder.createDirectory();
			m_folder.revealToUser();
		};
		m_change.onClick = [this] { chooseFolder(); };
		m_rescan.onClick = [this] { rescan(); };
		for (auto *b : {&m_show, &m_change, &m_rescan})
			addAndMakeVisible(*b);
		relayout();
	}
	~RomPopover() override { setLookAndFeel(nullptr); }

	void paint(juce::Graphics &g) override { layout(&g); }

	void resized() override
	{
		auto row = juce::Rectangle<int>(kPad, m_buttonsY, kWidth, 32);
		m_show.setBounds(row.removeFromLeft(m_show.idealWidth()));
		row.removeFromLeft(8);
		m_change.setBounds(row.removeFromLeft(m_change.idealWidth()));
		m_rescan.setBounds(row.removeFromRight(m_rescan.idealWidth() + 8));
	}

private:
	static constexpr int kWidth = 420, kPad = 14;

	// Draws (with g) or only measures (without); sets where the buttons go and returns the height.
	int layout(juce::Graphics *g)
	{
		const float w = float(kWidth), ox = float(kPad);
		float y = float(kPad);
		auto text = [&](const juce::String &s, const juce::Font &f, juce::Colour c, float x = 0, float width = -1) {
			const auto l = layoutOf(s, f, c, width < 0 ? w - x : width);
			if (g != nullptr)
				l.draw(*g, {ox + x, y, l.getWidth(), l.getHeight()});
			return l.getHeight();
		};
		const auto &first = m_roms.front();
		const auto *info = m_proc.machine();
		const juce::String name = info != nullptr ? info->name : juce::String();
		y += text(name + " is in " + (first.mode.isNotEmpty() ? first.mode.toLowerCase() : "a reduced mode"), mono(15.0f, true), col::ink) + 8;
		y += text("It's running without its " + first.what + ": " + first.without + ". Lemus ships no ROMs; it uses your own dump.",
		          mono(13.0f), col::inkSoft) +
		     14;
		for (const auto &r : m_roms) {
			// A card: the image's name, size and checksums, on `well`.
			const float x = 12, cw = w - 24;
			const juce::StringArray lines{r.name, sizeText(r.size) + utf8(" \xc2\xb7 CRC ") + r.crc32, "SHA1 " + r.sha1};
			std::vector<juce::TextLayout> ls;
			float h = 20;
			for (int i = 0; i < lines.size(); ++i) {
				ls.push_back(layoutOf(lines[i], i == 0 ? mono(13.0f, true) : mono(12.0f), i == 0 ? col::ink : col::muted, cw));
				h += ls.back().getHeight() + (i > 0 ? 2 : 0);
			}
			if (g != nullptr) {
				g->setColour(col::well);
				g->fillRoundedRectangle({ox, y, w, h}, 8.0f);
				float ly = y + 10;
				for (size_t i = 0; i < ls.size(); ++i) {
					ls[i].draw(*g, {ox + x, ly, cw, ls[i].getHeight()});
					ly += ls[i].getHeight() + 2;
				}
			}
			y += h + 8;
		}
		y += 6;
		y += text("Put it in your ROM folder, loose or in a zip (MAME's sets work as they are). Any file name will do.", mono(13.0f),
		          col::inkSoft) +
		     8;
		{
			const juce::Rectangle<float> box(ox, y, w, 30);
			if (g != nullptr) {
				g->setColour(col::well);
				g->fillRoundedRectangle(box, 8.0f);
				g->setColour(col::ink);
				g->setFont(mono(12.0f));
				g->drawText(shortPath(m_folder), box.reduced(12, 0), juce::Justification::centredLeft, true);
			}
			y += box.getHeight() + 14;
		}
		m_buttonsY = int(y);
		y += 32;
		if (m_status.isNotEmpty()) {
			y += 12;
			y += text(m_status, mono(12.0f), col::warn);
		}
		return int(std::ceil(y)) + kPad;
	}

	void relayout()
	{
		setSize(kWidth + 2 * kPad, layout(nullptr));
		resized();
		repaint();
	}

	void rescan()
	{
		const auto roms = m_roms;
		if (m_proc.reloadWithRoms()) {
			const auto *info = m_proc.machine();
			const bool all = m_proc.romsWithout().empty();
			m_onReloaded(roms.front().what + " found. " + (all ? "The full " : "") + (info != nullptr ? info->name : juce::String()) +
			             (all ? " is running" : " has reloaded") + "; your settings are kept.");
			if (auto *box = findParentComponentOfClass<juce::CallOutBox>())
				box->dismiss();
			return;
		}
		juce::StringArray names;
		for (const auto &r : roms)
			names.add(r.name);
		m_status = "Not found yet: " + names.joinIntoString(", ") + " isn't in that folder.";
		relayout();
	}

	void chooseFolder()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Choose your ROM folder", m_folder.isDirectory() ? m_folder : juce::File::getSpecialLocation(juce::File::userHomeDirectory));
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
		                       [safe = juce::Component::SafePointer<RomPopover>(this)](const juce::FileChooser &fc) {
			                       const auto folder = fc.getResult();
			                       if (safe == nullptr || !folder.isDirectory())
				                       return;
			                       lemus::romfolder::setLocation(folder);
			                       safe->m_folder = folder;
			                       safe->rescan();
		                       });
	}

	LemusProcessor &m_proc;
	std::function<void(const juce::String &)> m_onReloaded;
	juce::SharedResourcePointer<LemusLook> m_look;
	std::vector<lemus::RomSpec> m_roms;
	juce::File m_folder;
	juce::String m_status;
	Pill m_show{"Show folder"}, m_change{utf8("Change\xe2\x80\xa6")}, m_rescan{"Rescan", Pill::Kind::Primary};
	std::unique_ptr<juce::FileChooser> m_chooser;
	int m_buttonsY = 0;
};

// The first time a machine starts without an optional ROM: what's missing and why (with the
// artwork pack, if it has none), where Lemus looked, and the two ways on, side by side. Locate ROM…
// takes the image, a zip holding it (copied into the ROM folder) or a folder that has it (which
// becomes the ROM folder), and reloads the machine with it, keeping the session. Use voice mode
// (or the machine's mode) carries on without it. Once answered it doesn't come back for that
// machine: the bar keeps offering the ROM, and its pop-up shows and changes the ROM folder.
class RomPromptBody final : public juce::Component {
public:
	RomPromptBody(LemusProcessor &proc, std::function<void(const juce::String &)> onReloaded, std::function<void()> onClosed)
		: m_proc(proc), m_info(*proc.machine()), m_roms(proc.romsWithout()), m_onReloaded(std::move(onReloaded)), m_onClosed(std::move(onClosed))
	{
		m_folder = lemus::romfolder::location();
		const auto &art = m_info.artwork;
		m_noArtwork = art.name.isNotEmpty() && lemus::romfolder::findArtwork(m_folder, art).data.isEmpty();
		m_voice.setButtonText("Use " + mode().toLowerCase());
		m_locate.onClick = [this] { locate(); };
		m_voice.onClick = [this] { close(); };
		addAndMakeVisible(m_locate);
		addAndMakeVisible(m_voice);
		relayout();
	}

	void paint(juce::Graphics &g) override { layout(&g); }

	void resized() override
	{
		m_locate.setBounds(m_locateAt);
		m_voice.setBounds(m_voiceAt);
	}

	void mouseMove(const juce::MouseEvent &e) override { setHover(m_cardA.contains(e.position) ? 1 : m_cardB.contains(e.position) ? 2 : 0); }
	void mouseExit(const juce::MouseEvent &) override { setHover(0); }
	void mouseUp(const juce::MouseEvent &e) override
	{
		if (!e.mouseWasClicked())
			return;
		if (m_cardA.contains(e.position))
			locate();
		else if (m_cardB.contains(e.position))
			close();
	}

private:
	static constexpr int kWidth = 760, kPad = 20, kCardPad = 14;

	void setHover(int hover)
	{
		if (hover == m_hover)
			return;
		m_hover = hover;
		setMouseCursor(hover != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
		repaint();
	}

	const lemus::RomSpec &rom() const { return m_roms.front(); }
	juce::String mode() const { return rom().mode.isNotEmpty() ? rom().mode : "Without " + rom().name; }

	// Draws (with g) or only measures (without); places the buttons and returns the height.
	int layout(juce::Graphics *g)
	{
		const float pad = float(kPad), iw = float(kWidth - 2 * kPad);
		auto text = [&](const juce::String &s, const juce::Font &f, juce::Colour c, float x, float y, float width) {
			const auto l = layoutOf(s, f, c, width);
			if (g != nullptr)
				l.draw(*g, {x, y, width, l.getHeight()});
			return l.getHeight();
		};
		auto measure = [&](const juce::String &s, const juce::Font &f, float width) { return layoutOf(s, f, col::ink, width).getHeight(); };
		auto box = [&](juce::Rectangle<float> r, juce::Colour fill, juce::Colour edge, float radius) {
			if (g == nullptr)
				return;
			g->setColour(fill);
			g->fillRoundedRectangle(r, radius);
			if (!edge.isTransparent()) {
				g->setColour(edge);
				g->drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
			}
		};
		box(getLocalBounds().toFloat(), col::panel, col::rule, 10.0f);

		float y = pad;
		y += text("No " + rom().what + " found for the " + m_info.name, mono(16.0f, true), col::ink, pad, y, iw) + 6;
		const auto about = rom().about.isNotEmpty() ? rom().about
		                                            : "The " + rom().what + " holds the " + m_info.name +
		                                                  "'s own program. It's copyrighted, so Lemus doesn't include it: it uses your own dump.";
		y += text(about, mono(13.0f), col::inkSoft, pad, y, iw) + 14;

		// The files, one box: each missing image, then the artwork pack if there's none; where Lemus
		// looked for them under it.
		struct File {
			juce::String name, status;
			juce::String details; // a muted line
			juce::String note;    // a line in ink-soft
		};
		std::vector<File> files;
		for (const auto &r : m_roms)
			files.push_back({r.name, "not found",
			                 r.what + utf8(" \xc2\xb7 ") + sizeText(r.size) + utf8(" \xc2\xb7 CRC ") + r.crc32 + utf8(" \xc2\xb7 SHA1 ") + r.sha1, {}});
		if (m_noArtwork) {
			const auto maker = m_info.name.upToFirstOccurrenceOf(" ", false, false);
			files.push_back({m_info.artwork.name, "not found", utf8("Artwork \xc2\xb7 optional"),
			                 maker + "'s logo and model names aren't included, so their places on the panel are blank. Put your own artwork in " +
			                     m_info.artwork.name + " in the ROM folder to fill them."});
		}
		{
			const float x = pad + 12, cw = iw - 24;
			float h = 20;
			for (size_t i = 0; i < files.size(); ++i)
				h += (i > 0 ? 21 : 0) + measure(files[i].name, mono(13.0f, true), cw) + 2 + measure(files[i].details, mono(12.0f), cw) +
				     (files[i].note.isNotEmpty() ? 4 + measure(files[i].note, mono(12.0f), cw) : 0.0f);
			box({pad, y, iw, h}, col::well, juce::Colours::transparentBlack, 8.0f);
			float ly = y + 10;
			for (size_t i = 0; i < files.size(); ++i) {
				const auto &f = files[i];
				if (i > 0) {
					if (g != nullptr) {
						g->setColour(col::panel);
						g->fillRect(juce::Rectangle<float>(x, ly + 10, cw, 1));
					}
					ly += 21;
				}
				const float headH = text(f.name, mono(13.0f, true), col::ink, x, ly, cw);
				if (g != nullptr) {
					g->setColour(col::warn);
					g->setFont(mono(12.0f));
					g->drawText(f.status, juce::Rectangle<float>(x, ly, cw, headH), juce::Justification::centredRight, false);
				}
				ly += headH + 2;
				ly += text(f.details, mono(12.0f), col::muted, x, ly, cw);
				if (f.note.isNotEmpty())
					ly += 4 + text(f.note, mono(12.0f), col::inkSoft, x, ly + 4, cw);
			}
			y += h + 6;
			// The folder holds both, so where Lemus looked goes under the box.
			y += text("Looked in " + shortPath(m_folder), mono(12.0f), col::muted, pad + 12, y, iw - 12) + 16;
		}

		// The two ways on, side by side: give it the ROM, or carry on without.
		// One question, two answers: each card is a button as a whole.
		y += text("How do you want to continue?", mono(14.0f, true), col::ink, pad, y, iw) + 10;
		const float choiceW = (iw - 12) / 2, bLeft = pad + choiceW + 12;
		const float tw = choiceW - 2 * kCardPad, ax = pad + kCardPad, bx = bLeft + kCardPad;
		const juce::String aText = "Pick the file, a zip with it in (MAME's sets work as they are), or the folder you keep your ROMs in. Lemus "
		                           "checks it and starts the full " +
		                           m_info.name + ".";
		const juce::String bText = "Without it, the " + m_info.name + " runs in " + mode().toLowerCase() + ":";
		const juce::String bNote = "You can add the " + rom().what + " later from the bar.";
		juce::StringArray has = rom().modeHas, lacks = rom().modeLacks;
		if (has.isEmpty() && lacks.isEmpty())
			lacks.add(rom().without.substring(0, 1).toUpperCase() + rom().without.substring(1));
		const float markW = 36;
		const float aH = 20 + 8 + measure(aText, mono(13.0f), tw);
		float bH = 20 + 8 + measure(bText, mono(13.0f), tw) + 8;
		for (const auto &s : has)
			bH += measure(s, mono(12.0f), tw - markW) + 3;
		for (const auto &s : lacks)
			bH += measure(s, mono(12.0f), tw - markW) + 3;
		bH += 5 + measure(bNote, mono(12.0f), tw);
		const float cardH = kCardPad + juce::jmax(aH, bH) + 14 + 32 + kCardPad;
		m_cardA = {pad, y, choiceW, cardH};
		m_cardB = {bLeft, y, choiceW, cardH};
		box(m_cardA, m_hover == 1 ? col::well.withAlpha(0.5f) : col::ground, m_hover == 1 ? col::accent : col::rule, 8.0f);
		box(m_cardB, m_hover == 2 ? col::well.withAlpha(0.5f) : col::ground, m_hover == 2 ? col::accent : col::selection, 8.0f);
		{
			float ly = y + kCardPad;
			ly += text("I have the ROM", mono(14.0f, true), col::ink, ax, ly, tw) + 8;
			text(aText, mono(13.0f), col::inkSoft, ax, ly, tw);
		}
		{
			float ly = y + kCardPad;
			ly += text("Use " + mode().toLowerCase(), mono(14.0f, true), col::ink, bx, ly, tw) + 8;
			ly += text(bText, mono(13.0f), col::inkSoft, bx, ly, tw) + 8;
			auto item = [&](const juce::String &mark, juce::Colour markColour, const juce::String &s, juce::Colour colour) {
				if (g != nullptr) {
					g->setColour(markColour);
					g->setFont(mono(12.0f));
					g->drawText(mark, juce::Rectangle<float>(bx, ly, markW, 16), juce::Justification::centredLeft, false);
				}
				ly += text(s, mono(12.0f), colour, bx + markW, ly, tw - markW) + 3;
			};
			for (const auto &s : has)
				item(utf8("[\xe2\x9c\x93]"), col::ok, s, col::ink);
			for (const auto &s : lacks)
				item("[ ]", col::muted, s, col::inkSoft);
			text(bNote, mono(12.0f), col::muted, bx, ly + 5, tw);
		}
		const float buttonsY = y + cardH - kCardPad - 32, voiceW = float(m_voice.idealWidth() + 8);
		m_locateAt = juce::Rectangle<float>(ax, buttonsY, float(m_locate.idealWidth()), 32).toNearestInt();
		m_voiceAt = juce::Rectangle<float>(bx + tw - voiceW, buttonsY, voiceW, 32).toNearestInt();
		y += cardH;

		if (m_status.isNotEmpty()) {
			y += 12;
			y += text(m_status, mono(12.0f), col::warn, pad, y, iw);
		}
		return int(std::ceil(y + pad));
	}

	void relayout()
	{
		setSize(kWidth, layout(nullptr));
		resized();
		repaint();
		if (auto *parent = getParentComponent())
			parent->resized();
	}

	void close()
	{
		lemus::setRomPromptAnswered(m_info.id, true);
		m_onClosed();
	}

	void fail(const juce::String &status)
	{
		m_status = status;
		relayout();
	}

	// The ROM is in the folder now: reload the machine with it.
	bool reload()
	{
		const auto what = rom().what;
		if (!m_proc.reloadWithRoms())
			return false;
		const bool all = m_proc.romsWithout().empty();
		if (!all) // with every ROM, the answer is forgotten (LemusProcessor::createMachine)
			lemus::setRomPromptAnswered(m_info.id, true);
		m_onReloaded(what + " found. " + (all ? "The full " : "") + m_info.name + (all ? " is running" : " has reloaded") + "; your settings are kept.");
		return true;
	}

	juce::String names() const
	{
		juce::StringArray n;
		for (const auto &r : m_roms)
			n.add(r.name);
		return n.joinIntoString(", ");
	}

	// True if `folder` has any of the missing images.
	bool hasAny(const juce::File &folder) const
	{
		std::vector<lemus::RomSpec> missing;
		lemus::romfolder::load(folder, m_roms, &missing);
		return missing.size() < m_roms.size();
	}

	void locate()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Locate " + names(), juce::File::getSpecialLocation(juce::File::userHomeDirectory));
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
		                           juce::FileBrowserComponent::canSelectDirectories,
		                       [safe = juce::Component::SafePointer<RomPromptBody>(this)](const juce::FileChooser &fc) {
			                       const auto picked = fc.getResult();
			                       if (safe == nullptr)
				                       return;
			                       if (picked.isDirectory())
				                       safe->useFolder(picked);
			                       else if (picked.existsAsFile())
				                       safe->addFile(picked);
		                       });
	}

	// A folder that has the ROM becomes the ROM folder.
	void useFolder(const juce::File &folder)
	{
		if (!hasAny(folder))
			return fail(names() + " isn't in " + shortPath(folder) + ". Your ROM folder is unchanged.");
		lemus::romfolder::setLocation(folder);
		m_folder = folder;
		if (!reload())
			fail("Found in " + shortPath(folder) + ", but it didn't load. Try Rescan from the bar.");
	}

	// The image, or a zip holding it, is checked, then copied into the ROM folder.
	void addFile(const juce::File &file)
	{
		const auto check = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("lemus-rom", "");
		const bool ok = check.createDirectory() && file.copyFileTo(check.getChildFile(file.getFileName())) && hasAny(check);
		check.deleteRecursively();
		if (!ok)
			return fail(file.getFileName() + " isn't " + names() + ": its checksum doesn't match.");
		auto target = m_folder.getChildFile(file.getFileName());
		if (target.exists() && !target.hasIdenticalContentTo(file))
			target = m_folder.getNonexistentChildFile(file.getFileNameWithoutExtension(), file.getFileExtension());
		if (!target.exists() && !(m_folder.createDirectory() && file.copyFileTo(target)))
			return fail("Couldn't copy " + file.getFileName() + " into " + shortPath(m_folder) + ".");
		if (!reload())
			fail("Copied into " + shortPath(m_folder) + ", but it didn't load. Try Rescan from the bar.");
	}

	LemusProcessor &m_proc;
	const lemus::MachineInfo &m_info;
	std::vector<lemus::RomSpec> m_roms;
	std::function<void(const juce::String &)> m_onReloaded;
	std::function<void()> m_onClosed;
	juce::File m_folder;
	bool m_noArtwork = false; // the machine has an artwork pack but the folder has no filled-in one
	juce::Rectangle<float> m_cardA, m_cardB; // the two choices: a click anywhere on one chooses it
	int m_hover = 0;                         // the choice under the pointer: 1 or 2, 0 for neither
	juce::String m_status;
	Pill m_locate{utf8("Locate ROM\xe2\x80\xa6"), Pill::Kind::Plain, col::accent, 12.0f};
	Pill m_voice{"Use voice mode", Pill::Kind::Primary, col::accent, 12.0f};
	juce::Rectangle<int> m_locateAt, m_voiceAt;
	std::unique_ptr<juce::FileChooser> m_chooser;
};

// The prompt over the whole editor: the panel dimmed behind it, the prompt centred and, in a small
// window, scaled down to fit.
class RomPrompt final : public juce::Component {
public:
	RomPrompt(LemusProcessor &proc, std::function<void(const juce::String &)> onReloaded, std::function<void()> onClosed)
		: m_body(proc, std::move(onReloaded), std::move(onClosed))
	{
		addAndMakeVisible(m_body);
	}

	void paint(juce::Graphics &g) override { g.fillAll(juce::Colour(0xbc0d0e14)); }

	void resized() override
	{
		const float w = float(m_body.getWidth()), h = float(m_body.getHeight());
		if (w <= 0 || h <= 0)
			return;
		const float s = juce::jmin(1.0f, (float(getWidth()) - 32) / w, (float(getHeight()) - 32) / h);
		m_body.setTransform(juce::AffineTransform::scale(s).translated((float(getWidth()) - w * s) / 2, (float(getHeight()) - h * s) / 2));
	}

private:
	RomPromptBody m_body;
};

} // namespace

//==============================================================================
// The machines this host has, as a table: search, pick one, see what the ROM folder has for it,
// load it. Picking one loads it into the instance for good. Machines whose required ROMs aren't
// in the ROM folder can't load; the folder can be shown, changed and rescanned.
class LemusEditor::Picker final : public juce::Component, private juce::TextEditor::Listener {
public:
	Picker(LemusProcessor &proc, std::function<void()> refresh) : m_proc(proc), m_refresh(std::move(refresh))
	{
		setLookAndFeel(&m_look.get());
		m_folder = lemus::romfolder::location();
		juce::String sessionMachine;
		if (const auto &id = proc.missingMachine(); id.isNotEmpty()) {
			const auto *info = lemus::findMachine(id);
			sessionMachine = id;
			m_notice = info == nullptr ? "This session uses a machine this build doesn't have (\"" + id + "\"). Its settings are kept until you pick another."
			                           : "This session uses the " + info->name + ", but its ROMs aren't in your ROM folder. Its settings are kept: add them and Rescan.";
		}
		for (const auto &m : lemus::machines())
			m_entries.push_back({&m, LemusProcessor::missingRoms(m),
			                     m.artwork.name.isNotEmpty() && !lemus::romfolder::findArtwork(m_folder, m.artwork).data.isEmpty()});

		m_search.setFont(mono(14.0f));
		m_search.setIndents(14, 0);
		m_search.setJustification(juce::Justification::centredLeft);
		m_search.setTextToShowWhenEmpty("Search machines", col::muted);
		m_search.addListener(this);
		m_search.onArrow = [this](int d) { move(d); };
		addAndMakeVisible(m_search);

		m_list.onSelect = [this](int i) { select(i); };
		m_list.onLoad = [this] { load(); };
		m_view.setViewedComponent(&m_list, false);
		m_view.setScrollBarsShown(true, false);
		m_view.setScrollBarThickness(8);
		addAndMakeVisible(m_view);

		m_load.onClick = [this] { load(); };
		addAndMakeVisible(m_load);
		m_show.onClick = [this] {
			m_folder.createDirectory();
			m_folder.revealToUser();
		};
		m_change.onClick = [this] { chooseFolder(); };
		m_rescan.onClick = [this] { m_refresh(); };
		for (auto *b : {&m_show, &m_change, &m_rescan})
			addAndMakeVisible(*b);

		filter();
		int first = 0;
		for (int i = 0; i < int(m_filtered.size()); ++i)
			if (m_filtered[size_t(i)]->info->id == sessionMachine) {
				first = i;
				break;
			}
		if (sessionMachine.isEmpty())
			for (int i = 0; i < int(m_filtered.size()); ++i)
				if (m_filtered[size_t(i)]->loadable()) {
					first = i;
					break;
				}
		select(first);
	}
	~Picker() override { setLookAndFeel(nullptr); }

	void visibilityChanged() override
	{
		if (isShowing())
			m_search.grabKeyboardFocus();
	}

	void paint(juce::Graphics &g) override
	{
		g.fillAll(col::ground);
		const auto l = layout();

		// Header: the host's name and how many machines there are.
		g.setColour(col::title);
		g.setFont(mono(16.0f, true));
		g.drawText(JucePlugin_Name, l.title, juce::Justification::centredLeft, false);
		g.setColour(col::muted);
		g.setFont(mono(12.0f));
		g.drawText(countText(), l.count, juce::Justification::centredRight, false);
		g.setColour(col::rule);
		g.fillRect(0, kHeaderHeight - 1, getWidth(), 1);
		if (!l.notice.isEmpty())
			drawWrapped(g, m_notice, mono(12.0f), col::warn, l.notice.toFloat().withTrimmedTop(6));

		// The table's column labels.
		const auto c = columnsIn(l.columns.withWidth(l.list.getWidth() - 8).reduced(20, 0));
		lemus::style::drawLabel(g, "Machine", c.name.toFloat());
		lemus::style::drawLabel(g, "Year", c.year.toFloat());
		lemus::style::drawLabel(g, "ROMs", c.status.toFloat());

		// The selected machine.
		g.setColour(col::panel);
		g.fillRect(l.detail);
		paintDetail(g, l.detail.reduced(20));

		// Footer: the ROM folder, the disclaimer.
		g.setColour(col::panel);
		g.fillRect(l.footer);
		g.setColour(col::rule);
		g.fillRect(l.footer.withHeight(1));
		g.setFont(mono(12.0f));
		g.setColour(col::muted);
		g.drawText("ROM folder", l.folderLabel, juce::Justification::centredLeft, false);
		g.setColour(col::inkSoft);
		g.drawText(m_folder.getFullPathName(), l.folderPath, juce::Justification::centredLeft, true);
		g.setColour(col::muted);
		g.setFont(mono(11.0f));
		g.drawFittedText(kDisclaimer, l.disclaimer, juce::Justification::topLeft, 2, 1.0f);
	}

	void resized() override
	{
		const auto l = layout();
		m_search.setBounds(l.search);
		m_view.setBounds(l.list);
		m_list.setSize(l.list.getWidth() - 8, m_list.getHeight());
		m_load.setBounds(l.detail.reduced(20).removeFromBottom(40));
		auto buttons = l.folderButtons;
		for (auto *b : {&m_rescan, &m_change, &m_show}) {
			b->setBounds(buttons.removeFromRight(b->idealWidth()).withSizeKeepingCentre(b->idealWidth(), 26));
			buttons.removeFromRight(8);
		}
	}

private:
	struct Layout {
		juce::Rectangle<int> title, search, count, notice, columns, list, detail, footer, folderLabel, folderPath, folderButtons, disclaimer;
	};

	Layout layout() const
	{
		Layout l;
		auto b = getLocalBounds();
		auto header = b.removeFromTop(kHeaderHeight).reduced(20, 16);
		l.title = header.removeFromLeft(juce::GlyphArrangement::getStringWidthInt(mono(16.0f, true), JucePlugin_Name) + 4);
		header.removeFromLeft(16);
		l.count = header.removeFromRight(juce::GlyphArrangement::getStringWidthInt(mono(12.0f), countText()) + 4);
		header.removeFromRight(16);
		l.search = header;
		l.footer = b.removeFromBottom(kFooterHeight);
		auto f = l.footer.reduced(20, 0);
		auto folderRow = f.removeFromTop(44);
		l.folderLabel = folderRow.removeFromLeft(90);
		l.folderButtons = folderRow.removeFromRight(m_show.idealWidth() + m_change.idealWidth() + m_rescan.idealWidth() + 24);
		l.folderPath = folderRow.withTrimmedRight(12);
		l.disclaimer = f.removeFromTop(30);
		l.detail = b.removeFromRight(kDetailWidth);
		if (m_notice.isNotEmpty())
			l.notice = b.removeFromTop(44).reduced(20, 0);
		b.removeFromTop(8);
		l.columns = b.removeFromTop(kRowHeight);
		l.list = b.withTrimmedBottom(8);
		return l;
	}

	juce::String countText() const
	{
		const int n = int(m_entries.size());
		const int needs = int(std::count_if(m_entries.begin(), m_entries.end(), [](const Entry &e) { return !e.loadable(); }));
		juce::String s = n == 1 ? "1 machine" : juce::String(n) + " machines";
		if (needs > 0)
			s << utf8(" \xc2\xb7 ") << needs << (needs == 1 ? " needs a ROM" : " need ROMs");
		return s;
	}

	const Entry *current() const { return m_selected >= 0 && m_selected < int(m_filtered.size()) ? m_filtered[size_t(m_selected)] : nullptr; }

	void paintDetail(juce::Graphics &g, juce::Rectangle<int> area)
	{
		const auto *e = current();
		if (e == nullptr)
			return;
		auto r = area.toFloat().withTrimmedBottom(56);
		const float w = r.getWidth();
		float y = r.getY();
		y += drawWrapped(g, e->info->name, mono(16.0f, true), col::ink, {r.getX(), y, w, 100}) + 4;
		y += drawWrapped(g, e->info->description, mono(13.0f), col::inkSoft, {r.getX(), y, w, 100}) + 18;
		lemus::style::drawLabel(g, "Files", {r.getX(), y, w, 16});
		y += 24;

		auto card = [&](const juce::String &name, const juce::String &state, juce::Colour stateColour, const juce::String &meta, const juce::String &note) {
			const float cx = r.getX() + 12, cw = w - 24;
			const auto metaL = layoutOf(meta, mono(12.0f), col::muted, cw);
			const auto noteL = layoutOf(note, mono(12.5f), col::inkSoft, cw);
			const float h = 10 + 20 + 2 + metaL.getHeight() + (note.isNotEmpty() ? 6 + noteL.getHeight() : 0) + 10;
			g.setColour(col::well);
			g.fillRoundedRectangle({r.getX(), y, w, h}, 8.0f);
			float cy = y + 10;
			g.setFont(mono(13.0f));
			g.setColour(stateColour);
			const int stateW = juce::GlyphArrangement::getStringWidthInt(mono(13.0f), state) + 2;
			g.drawText(state, juce::Rectangle<float>(cx + cw - float(stateW), cy, float(stateW), 20), juce::Justification::centredRight, false);
			g.setColour(col::ink);
			g.drawText(name, juce::Rectangle<float>(cx, cy, cw - float(stateW) - 8, 20), juce::Justification::centredLeft, true);
			cy += 22;
			metaL.draw(g, {cx, cy, cw, metaL.getHeight()});
			cy += metaL.getHeight();
			if (note.isNotEmpty())
				noteL.draw(g, {cx, cy + 6, cw, noteL.getHeight()});
			y += h + 8;
		};
		for (const auto &rom : e->info->roms) {
			const bool found = e->found(rom);
			juce::String meta = rom.what + utf8(" \xc2\xb7 ") + sizeText(rom.size) + utf8(" \xc2\xb7 CRC ") + rom.crc32;
			if (rom.optional)
				meta << utf8(" \xc2\xb7 optional");
			juce::String note;
			if (!found)
				note = rom.optional ? "Without it: " + rom.without + "." : "Needed to load the " + e->info->name + ".";
			card(rom.name, found ? "found" : "not found", found ? col::ok : col::warn, meta, note);
		}
		if (e->info->artwork.name.isNotEmpty())
			card(e->info->artwork.name, e->artwork ? "found" : "not found", e->artwork ? col::ok : col::muted, utf8("Artwork pack \xc2\xb7 optional"), {});
		if (e->info->roms.empty() && e->info->artwork.name.isEmpty())
			drawWrapped(g, "No ROMs needed.", mono(13.0f), col::inkSoft, {r.getX(), y, w, 40});
	}

	void textEditorTextChanged(juce::TextEditor &) override
	{
		const auto *was = current();
		filter();
		int i = 0;
		for (int k = 0; k < int(m_filtered.size()); ++k)
			if (m_filtered[size_t(k)] == was)
				i = k;
		select(m_filtered.empty() ? -1 : i);
	}
	void textEditorReturnKeyPressed(juce::TextEditor &) override { load(); }

	void filter()
	{
		const auto q = m_search.getText().trim();
		m_filtered.clear();
		for (const auto &e : m_entries)
			if (q.isEmpty() || e.info->name.containsIgnoreCase(q) || e.info->description.containsIgnoreCase(q) || e.info->id.containsIgnoreCase(q))
				m_filtered.push_back(&e);
		m_list.setEntries(m_filtered, -1);
	}

	void select(int i)
	{
		m_selected = m_filtered.empty() ? -1 : juce::jlimit(0, int(m_filtered.size()) - 1, i);
		m_list.select(m_selected);
		const auto *e = current();
		m_load.setVisible(e != nullptr);
		if (e != nullptr) {
			const auto *req = e->firstRequired();
			m_load.setEnabled(req == nullptr);
			m_load.setButtonText(req == nullptr ? "Load " + e->info->name : "Needs its " + req->what);
		}
		repaint();
	}

	void move(int delta)
	{
		if (!m_filtered.empty())
			select(m_selected + delta);
	}

	void load()
	{
		// Loading replaces this picker; the processor's change message does that, after the click.
		if (const auto *e = current(); e != nullptr && e->loadable())
			m_proc.selectMachine(e->info->id);
	}

	void chooseFolder()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Choose your ROM folder", m_folder.isDirectory() ? m_folder : juce::File::getSpecialLocation(juce::File::userHomeDirectory));
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser &fc) {
			const auto folder = fc.getResult();
			if (folder.isDirectory()) {
				lemus::romfolder::setLocation(folder);
				m_refresh();
			}
		});
	}

	LemusProcessor &m_proc;
	std::function<void()> m_refresh;
	juce::SharedResourcePointer<LemusLook> m_look;
	juce::String m_notice;
	std::vector<Entry> m_entries;
	std::vector<const Entry *> m_filtered;
	int m_selected = -1;
	juce::File m_folder;
	SearchField m_search;
	juce::Viewport m_view;
	MachineList m_list;
	Pill m_load{"Load", Pill::Kind::Primary, col::accent, 14.0f};
	Pill m_show{"Show"}, m_change{utf8("Change\xe2\x80\xa6")}, m_rescan{"Rescan"};
	std::unique_ptr<juce::FileChooser> m_chooser;
};

//==============================================================================
// The strip above a machine's editor: which host and machine this is; while it runs without an
// optional ROM, which mode that is and a way to add the ROM; a newer release when the update
// check has found one; the About box; and, after a reload, what happened.
class LemusEditor::Bar final : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
	Bar(LemusProcessor &proc, const juce::String &notes, const juce::String &flash, int flashMs, std::function<void(const juce::String &)> onReloaded)
		: m_proc(proc), m_info(*proc.machine()), m_notes(notes), m_without(proc.romsWithout()), m_flash(flash), m_onReloaded(std::move(onReloaded))
	{
		m_about.onClick = [this] { showAbout(); };
		addAndMakeVisible(m_about);

		if (!m_without.empty()) {
			m_addRom.setButtonText("Add " + m_without.front().what + utf8("\xe2\x80\xa6"));
			m_addRom.onClick = [this] { showAddRom(); };
			addAndMakeVisible(m_addRom);
		}

		m_update.onClick = [this] { juce::URL(m_release.url).launchInDefaultBrowser(); };
		addChildComponent(m_update);
		showRelease();
		m_checker->addChangeListener(this);
		m_checker->checkIfDue();
		if (m_flash.isNotEmpty())
			startTimer(juce::jmax(1, flashMs));
	}

	~Bar() override { m_checker->removeChangeListener(this); }

	void paint(juce::Graphics &g) override
	{
		g.fillAll(col::panel);
		g.setColour(col::rule);
		g.fillRect(getLocalBounds().removeFromBottom(1));
		auto b = getLocalBounds().reduced(12, 0).withRight(m_buttonsLeft - 12);

		// A narrow machine (a channel strip) keeps what fits: " emulation" goes first, then the
		// product's name, then the machine's name gets smaller, then it is cut short.
		const juce::String product = JucePlugin_Name;
		const int productW = juce::GlyphArrangement::getStringWidthInt(mono(13.0f, true), product) + 12;
		juce::String machine = m_info.name + " emulation";
		float size = 13.0f;
		auto machineW = [&] { return juce::GlyphArrangement::getStringWidthInt(mono(size), machine) + 14; };
		if (productW + machineW() > b.getWidth())
			machine = m_info.name;
		if (productW + machineW() <= b.getWidth()) {
			g.setColour(col::title);
			g.setFont(mono(13.0f, true));
			g.drawText(product, b.removeFromLeft(productW), juce::Justification::centredLeft, false);
		} else if (machineW() > b.getWidth()) {
			size = 11.0f;
		}
		g.setColour(col::ink);
		g.setFont(mono(size));
		g.drawText(machine, b.removeFromLeft(juce::jmin(machineW(), b.getWidth())), juce::Justification::centredLeft, true);

		if (m_flash.isNotEmpty()) {
			g.setColour(col::ok);
			g.setFont(mono(12.0f));
			g.drawText(utf8("\xe2\x9c\x93 ") + m_flash, b, juce::Justification::centredLeft, true);
		} else if (!m_without.empty()) {
			const auto &r = m_without.front();
			const juce::String mode = (r.mode.isNotEmpty() ? r.mode : "Without " + r.name).toUpperCase();
			const auto badgeFont = mono(11.0f, true).withExtraKerningFactor(0.08f);
			const float bw = float(juce::GlyphArrangement::getStringWidthInt(badgeFont, mode)) + 18;
			const juce::Rectangle<float> badge(float(b.getX()), float(b.getCentreY()) - 11, bw, 22);
			g.setColour(col::warn.withAlpha(0.12f));
			g.fillRoundedRectangle(badge, 11.0f);
			g.setColour(col::warn.withAlpha(0.6f));
			g.drawRoundedRectangle(badge.reduced(0.5f), 11.0f, 1.0f);
			g.setColour(col::warn);
			g.setFont(badgeFont);
			g.drawText(mode, badge, juce::Justification::centred, false);
			b.removeFromLeft(int(bw) + 12);
			g.setColour(col::muted);
			g.setFont(mono(12.0f));
			g.drawText("Without its " + r.what + ": " + r.without, b, juce::Justification::centredLeft, true);
		}
	}

	void resized() override
	{
		auto b = getLocalBounds().reduced(6, 0);
		auto place = [&](Pill &p) {
			if (!p.isVisible())
				return;
			const int w = p.idealWidth();
			p.setBounds(b.removeFromRight(w).withSizeKeepingCentre(w, 24));
			b.removeFromRight(8);
		};
		place(m_about);
		place(m_addRom);
		place(m_update);
		m_buttonsLeft = b.getRight();
	}

private:
	void changeListenerCallback(juce::ChangeBroadcaster *) override { showRelease(); }

	void timerCallback() override
	{
		stopTimer();
		m_flash.clear();
		repaint();
	}

	void showRelease()
	{
		m_release = lemus::updates::newer();
		m_update.setButtonText(juce::String(JucePlugin_Name) + " " + m_release.version + " is out");
		m_update.setVisible(m_release.version.isNotEmpty());
		resized();
		repaint();
	}

	void launch(std::unique_ptr<juce::Component> content, juce::Component &from)
	{
		auto *top = getTopLevelComponent();
		auto &box = juce::CallOutBox::launchAsynchronously(std::move(content), top->getLocalArea(&from, from.getLocalBounds()), top);
		box.setLookAndFeel(&m_look.get());
	}

	void showAbout() { launch(std::make_unique<AboutBox>(m_info, m_notes), m_about); }
	void showAddRom() { launch(std::make_unique<RomPopover>(m_proc, m_onReloaded), m_addRom); }

	LemusProcessor &m_proc;
	const lemus::MachineInfo &m_info;
	juce::String m_notes; // the ROMs it runs without, its artwork pack
	std::vector<lemus::RomSpec> m_without;
	juce::String m_flash;
	std::function<void(const juce::String &)> m_onReloaded;
	Pill m_about{"About"};
	Pill m_addRom{"Add ROM", Pill::Kind::Outline, col::warn};
	Pill m_update{"Update", Pill::Kind::Outline, col::accent};
	int m_buttonsLeft = 0;
	juce::SharedResourcePointer<LemusLook> m_look;
	juce::SharedResourcePointer<lemus::updates::Checker> m_checker;
	lemus::updates::Release m_release;
};

//==============================================================================
void LemusEditor::Constrainer::checkBounds(juce::Rectangle<int> &bounds, const juce::Rectangle<int> &previous,
                                           const juce::Rectangle<int> &limits, bool top, bool left, bool bottom, bool right)
{
	auto *inner = m_editor.m_inner != nullptr ? m_editor.m_inner->getConstrainer() : nullptr;
	if (inner == nullptr) {
		ComponentBoundsConstrainer::checkBounds(bounds, previous, limits, top, left, bottom, right);
		return;
	}
	auto area = bounds.withTrimmedTop(kBarHeight);
	inner->checkBounds(area, previous.withTrimmedTop(kBarHeight), limits.isEmpty() ? limits : limits.withTrimmedTop(kBarHeight), top, left,
	                   bottom, right);
	bounds = area.withTop(area.getY() - kBarHeight);
}

//==============================================================================
LemusEditor::LemusEditor(LemusProcessor &p) : AudioProcessorEditor(&p), m_proc(p)
{
	setConstrainer(&m_constrainer);
	m_proc.addChangeListener(this);
	showCurrentMachine();
}

LemusEditor::~LemusEditor()
{
	m_proc.removeChangeListener(this);
	if (m_inner != nullptr)
		m_inner->removeComponentListener(this);
	m_inner.reset();
}

void LemusEditor::changeListenerCallback(juce::ChangeBroadcaster *)
{
	showCurrentMachine();
}

void LemusEditor::showCurrentMachine()
{
	auto *machine = m_proc.machineProcessor();
	if (machine != nullptr && machine == m_innerFor)
		return;
	if (m_inner != nullptr)
		m_inner->removeComponentListener(this);
	m_inner.reset();
	m_prompt.reset();
	m_bar.reset();
	m_picker.reset();
	m_innerFor = machine;

	if (machine == nullptr) {
		// The picker's buttons call back into this editor, which replaces the picker; the
		// processor's change message does that, after the click has finished.
		m_picker = std::make_unique<Picker>(m_proc, [this] { refresh(); });
		addAndMakeVisible(*m_picker);
		setResizable(false, false);
		m_constrainer.setSizeLimits(kPickerWidth, kPickerHeight, kPickerWidth, kPickerHeight);
		setSize(kPickerWidth, kPickerHeight);
		resized();
		return;
	}

	m_inner.reset(machine->createEditorIfNeeded());
	if (m_inner == nullptr)
		m_inner = std::make_unique<juce::GenericAudioProcessorEditor>(*machine);
	juce::StringArray notes{m_proc.romNote(), m_proc.artworkNote()};
	notes.removeEmptyStrings();
	const auto left = m_flashUntil - juce::Time::currentTimeMillis();
	const auto flash = [safe = juce::Component::SafePointer<LemusEditor>(this)](const juce::String &text) {
		if (safe != nullptr) {
			safe->m_flash = text;
			safe->m_flashUntil = juce::Time::currentTimeMillis() + 8000;
		}
	};
	m_bar = std::make_unique<Bar>(m_proc, notes.joinIntoString("\n\n"), left > 0 ? m_flash : juce::String(), int(left), flash);
	addAndMakeVisible(*m_bar);
	addAndMakeVisible(*m_inner);
	m_inner->addComponentListener(this);
	if (!m_proc.romsWithout().empty() && !lemus::romPromptAnswered(m_proc.machine()->id)) {
		// Closed after the click that answered it has finished.
		m_prompt = std::make_unique<RomPrompt>(m_proc, flash, [safe = juce::Component::SafePointer<LemusEditor>(this)] {
			juce::MessageManager::callAsync([safe] {
				if (safe != nullptr)
					safe->m_prompt.reset();
			});
		});
		addAndMakeVisible(*m_prompt);
	}

	if (auto *c = m_inner->getConstrainer())
		m_constrainer.setSizeLimits(c->getMinimumWidth(), c->getMinimumHeight() + kBarHeight, c->getMaximumWidth(),
		                            c->getMaximumHeight() + kBarHeight);
	setResizable(m_inner->isResizable(), false);
	setSize(m_inner->getWidth(), m_inner->getHeight() + kBarHeight);
	resized();
}

void LemusEditor::refresh()
{
	// After the click that asked for it has finished: the picker is replaced.
	juce::MessageManager::callAsync([safe = juce::Component::SafePointer<LemusEditor>(this)] {
		if (safe != nullptr && !safe->m_proc.retryMissing()) // the session's machine, if it can load now
			safe->showCurrentMachine();
	});
}

void LemusEditor::componentMovedOrResized(juce::Component &c, bool, bool wasResized)
{
	// The machine editor resized itself (its own corner): follow it.
	if (wasResized && !m_layingOut && &c == m_inner.get())
		setSize(c.getWidth(), c.getHeight() + kBarHeight);
}

void LemusEditor::paint(juce::Graphics &g)
{
	g.fillAll(col::ground);
}

void LemusEditor::resized()
{
	const juce::ScopedValueSetter<bool> layingOut(m_layingOut, true);
	auto b = getLocalBounds();
	if (m_picker != nullptr)
		m_picker->setBounds(b);
	if (m_bar != nullptr)
		m_bar->setBounds(b.removeFromTop(kBarHeight));
	if (m_inner != nullptr)
		m_inner->setBounds(b);
	if (m_prompt != nullptr)
		m_prompt->setBounds(getLocalBounds());
}
