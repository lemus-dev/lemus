// The frame every machine's panel shares, so that they all work alike: the bar under the panel,
// keys held by a right-click, the computer keyboard's common keys, the key list (?) and tooltips.
// The hardware is each machine's own; this is everything around it (the top README, "Machine
// panels", says what goes where). Header only: a machine's editor and its tools include it without
// linking the host, as they do host/Artwork.h.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <functional>
#include <map>
#include <utility>
#include <vector>

namespace lemus::panel {

// The panel's monospace (each machine bundles IBM Plex Mono): a size in px and a weight, 0 regular,
// 1 medium, 2 semibold.
using Mono = std::function<juce::Font(float size, int weight)>;

namespace col {
inline const juce::Colour bar{0xff1b1d21}, barLine{0xff2a2f37}, pill{0xff232830}, pillHover{0xff2c323b}, pillOn{0xff2a1e12},
	border{0xff2e343d}, accent{0xffff8c1a}, ink{0xffe9ecf1}, inkOn{0xffffb866}, status{0xff9aa4b1}, overlay{0xf01b1f25},
	overlayText{0xffc8ced6}, tip{0xe8101317};
}

// ====================================================================== the bar

// Under the panel, what the unit doesn't have: pills from the left, in the order SYNC, the
// machine's own, REAR, SERVICE (each where the machine has it); the status line after them; ? at
// the right end. Never a button for what the unit can't do itself (no memory clear, no factory
// trim): the host's State pop-up has Reset.
class Bar {
public:
	static constexpr float kPillHeight = 26, kLeft = 10, kGap = 8;
	// The standard pills' widths.
	static constexpr float kSync = 110, kRear = 64, kService = 82;
	static constexpr int kHelp = -2; // hit(): the ? pill

	Bar(int width, int panelHeight, int barHeight)
		: m_width(float(width)), m_top(float(panelHeight)), m_height(float(barHeight)), m_y(float(panelHeight) + (float(barHeight) - kPillHeight) / 2)
	{
	}

	// Adds a pill `width` px wide after the others, with the tooltip it shows; returns its index.
	int add(float width, juce::String tip = {})
	{
		const float x = m_items.empty() ? kLeft : m_items.back().r.getRight() + kGap;
		m_items.push_back({{x, m_y, width, kPillHeight}, std::move(tip)});
		return int(m_items.size()) - 1;
	}
	void setTip(int i, juce::String tip) { m_items[size_t(i)].tip = std::move(tip); }

	juce::Rectangle<float> operator[](int i) const { return i == kHelp ? help() : m_items[size_t(i)].r; }
	juce::Rectangle<float> help() const { return {m_width - 44, m_y, 34, kPillHeight}; }
	juce::Rectangle<float> area() const { return {0, m_top, m_width, m_height}; }
	// The status line: from after the last pill to `right` (by default, up to ?).
	juce::Rectangle<float> status(float right = -1) const
	{
		const float x = (m_items.empty() ? 0 : m_items.back().r.getRight()) + 14;
		const float r = right < 0 ? help().getX() - 12 : right;
		return {x, m_top, juce::jmax(0.0f, r - x), m_height};
	}
	// The pill at `p`: its index, kHelp, or -1.
	int hit(juce::Point<float> p) const
	{
		if (help().contains(p))
			return kHelp;
		for (size_t i = 0; i < m_items.size(); ++i)
			if (m_items[i].r.contains(p))
				return int(i);
		return -1;
	}
	// Its tooltip, above it.
	const juce::String &tip(int i) const
	{
		static const juce::String keys("The computer keys and what the mouse does");
		return i == kHelp ? keys : m_items[size_t(i)].tip;
	}

private:
	struct Item {
		juce::Rectangle<float> r;
		juce::String tip;
	};
	float m_width, m_top, m_height, m_y;
	std::vector<Item> m_items;
};

inline void drawBarBackground(juce::Graphics &g, const Bar &bar)
{
	const auto a = bar.area();
	g.setColour(col::bar);
	g.fillRect(a);
	g.setColour(col::barLine);
	g.drawLine(0, a.getY() + 0.5f, a.getWidth(), a.getY() + 0.5f, 1.0f);
}

// A pill: lit (orange) while its setting is on.
inline void drawPill(juce::Graphics &g, juce::Rectangle<float> r, const juce::String &text, bool on, bool hover, const Mono &mono)
{
	g.setColour(on ? col::pillOn : hover ? col::pillHover : col::pill);
	g.fillRoundedRectangle(r, 6);
	g.setColour(on ? col::accent : col::border);
	g.drawRoundedRectangle(r.reduced(0.5f), 6, 1);
	g.setColour(on ? col::inkOn : col::ink);
	g.setFont(mono(12.0f, 1));
	g.drawText(text, r, juce::Justification::centred);
}

inline void drawStatus(juce::Graphics &g, juce::Rectangle<float> r, const juce::String &text, const Mono &mono, juce::Colour c = col::status)
{
	g.setFont(mono(11.0f, 0));
	g.setColour(c);
	g.drawText(text, r, juce::Justification::centredLeft, true);
}

// The status line's wording, the same on every machine.
// Without the program ROM: `rom` is its name in the machine's ROM list, as the host's prompt says it.
inline juce::String voiceModeStatus(const juce::String &rom, bool voices)
{
	return "VOICE MODE (no " + rom + "): MIDI plays the " + (voices ? "voices" : "voice");
}
// Keys held by a right-click take the status line over until they're let go: they're easy to
// forget, and they change what the other keys do.
inline juce::String heldStatus(const juce::StringArray &held)
{
	return held.joinIntoString(", ") + " held: click " + (held.size() > 1 ? "them" : "it") + " or Esc to release";
}

// ====================================================================== the keys

// The panel's keys as the mouse and the computer keyboard hold them. A click holds a key while the
// button is down. A right-click (ctrl-click) holds it until it's clicked again, for what takes two
// keys at once; Escape lets go of them all. A computer key holds its panel key while it's down; a
// key latched by the mouse stays down when its computer key comes up.
class Keys {
public:
	// Presses (true) or lets go of panel key i on the machine; called only on a change.
	using Send = std::function<void(int key, bool down)>;

	void reset(size_t count, Send send)
	{
		m_down.assign(count, false);
		m_latched.assign(count, false);
		m_computer.clear();
		m_send = std::move(send);
	}
	size_t size() const { return m_down.size(); }
	bool down(int i) const { return valid(i) && m_down[size_t(i)]; }
	bool latched(int i) const { return valid(i) && m_latched[size_t(i)]; }
	bool anyLatched() const { return std::find(m_latched.begin(), m_latched.end(), true) != m_latched.end(); }

	// A mouse press on key i: true if the mouse now holds it (mouseUp() lets go). A key that acts
	// on the press alone (`canLatch` false) is never latched.
	bool mouseDown(int i, const juce::ModifierKeys &mods, bool canLatch = true)
	{
		if (!valid(i))
			return false;
		if (m_latched[size_t(i)]) {
			set(i, false);
			return false;
		}
		if (mods.isPopupMenu() && canLatch) {
			set(i, true);
			m_latched[size_t(i)] = true;
			return false;
		}
		set(i, true);
		return true;
	}
	void mouseUp(int i)
	{
		if (valid(i) && !m_latched[size_t(i)])
			set(i, false);
	}

	// A computer key (its KeyPress code) going down on panel key i.
	void keyDown(int code, int i)
	{
		if (!valid(i) || m_computer.count(code) != 0)
			return;
		m_computer[code] = i;
		set(i, true);
	}
	// From keyStateChanged: lets go of the keys whose computer key came up. True if any did.
	bool keysChanged()
	{
		bool any = false;
		for (auto it = m_computer.begin(); it != m_computer.end();) {
			if (juce::KeyPress::isKeyCurrentlyDown(it->first)) {
				++it;
				continue;
			}
			if (!m_latched[size_t(it->second)])
				set(it->second, false);
			it = m_computer.erase(it);
			any = true;
		}
		return any;
	}
	// Focus gone: the computer keys won't see their key-up; latched keys stay down.
	void focusLost() { releaseAll(false); }
	// Escape (latchedToo) and the like.
	void releaseAll(bool latchedToo)
	{
		for (size_t i = 0; i < m_down.size(); ++i)
			if (latchedToo || !m_latched[i])
				set(int(i), false);
		m_computer.clear();
	}
	// Presses or lets go of a key directly (a machine's own keys, such as the computer's Shift).
	void set(int i, bool down)
	{
		if (!valid(i))
			return;
		if (!down)
			m_latched[size_t(i)] = false;
		if (m_down[size_t(i)] == down)
			return;
		m_down[size_t(i)] = down;
		if (m_send)
			m_send(i, down);
	}
	// The latched keys' names, for heldStatus().
	juce::StringArray latchedNames(const std::function<juce::String(int)> &name) const
	{
		juce::StringArray out;
		for (size_t i = 0; i < m_latched.size(); ++i)
			if (m_latched[i])
				out.add(name(int(i)));
		return out;
	}

private:
	bool valid(int i) const { return i >= 0 && size_t(i) < m_down.size(); }
	std::vector<bool> m_down, m_latched;
	std::map<int, int> m_computer; // KeyPress code -> key
	Send m_send;
};

// The computer keys every panel reads alike, before its own map: ? shows the key list, Escape
// closes it and lets go of every key.
enum class Common { None, Help, Escape };
inline Common commonKey(const juce::KeyPress &k)
{
	if (k.getTextCharacter() == '?' || (k.getKeyCode() == '/' && k.getModifiers().isShiftDown()))
		return Common::Help;
	if (k.getKeyCode() == juce::KeyPress::escapeKey)
		return Common::Escape;
	return Common::None;
}
// Space is the machine's run key in the standalone app only: in a plugin it goes on to the DAW's
// transport.
inline bool spaceIsOurs(const juce::KeyPress &k)
{
	return k.getKeyCode() == juce::KeyPress::spaceKey && juce::JUCEApplicationBase::isStandaloneApp();
}
// Backspace and Delete are both the panel's clear key.
inline bool isClear(const juce::KeyPress &k)
{
	return k.getKeyCode() == juce::KeyPress::backspaceKey || k.getKeyCode() == juce::KeyPress::deleteKey;
}

// ====================================================================== the key list and tooltips

// The lines every key list ends with.
inline std::vector<juce::String> commonHelp()
{
	return {"",
	        "Mouse: drag the knobs (shift = fine, double-click = default, or the wheel), press the keys.",
	        "Right-click (ctrl-click) a key: it stays down until clicked again. Escape lets go of them all.",
	        "Space: standalone only (in a DAW it is the DAW's transport). Any click, ? or Escape closes this."};
}

// The key list (?), over the panel `over`: "<MODEL> KEYS", the machine's lines, then commonHelp().
inline void drawHelp(juce::Graphics &g, juce::Rectangle<float> over, const juce::String &model, std::vector<juce::String> lines, const Mono &mono)
{
	for (auto &l : commonHelp())
		lines.push_back(std::move(l));
	// 21 px a line, closer (and the text smaller) when that won't fit over the panel
	constexpr float kWidth = 980, kFrame = 64 + 20;
	const float line = juce::jmin(21.0f, (over.getHeight() - 20 - kFrame) / float(lines.size()));
	const auto r = juce::Rectangle<float>(juce::jmin(kWidth, over.getWidth() - 40), kFrame + line * float(lines.size())).withCentre(over.getCentre());
	g.setColour(col::overlay);
	g.fillRoundedRectangle(r, 12);
	g.setColour(col::border);
	g.drawRoundedRectangle(r, 12, 1);
	g.setColour(col::ink);
	g.setFont(mono(15.0f, 2));
	g.drawText(model + " KEYS", r.reduced(24).removeFromTop(24), juce::Justification::topLeft);
	g.setFont(mono(juce::jmin(12.5f, line - 6.0f), 0));
	g.setColour(col::overlayText);
	float y = r.getY() + 64;
	for (const auto &l : lines) {
		g.drawText(l, juce::Rectangle<float>(r.getX() + 24, y, r.getWidth() - 48, line), juce::Justification::centredLeft);
		y += line;
	}
}

// A tooltip: `text` centred under (or, for the bar, over) `at`, kept inside `within`.
inline void drawTooltip(juce::Graphics &g, const juce::String &text, juce::Point<float> at, juce::Rectangle<float> within, const Mono &mono)
{
	if (text.isEmpty())
		return;
	const auto font = mono(12.0f, 1);
	g.setFont(font);
	juce::GlyphArrangement ga;
	ga.addLineOfText(font, text, 0, 0);
	const float w = ga.getBoundingBox(0, -1, true).getWidth() + 16;
	const auto box = juce::Rectangle<float>(at.x - w / 2, at.y, w, 22).constrainedWithin(within.reduced(4));
	g.setColour(col::tip);
	g.fillRoundedRectangle(box, 5);
	g.setColour(col::inkOn);
	g.drawText(text, box, juce::Justification::centred);
}
// A bar pill's tooltip, above it.
inline void drawBarTip(juce::Graphics &g, const Bar &bar, int item, juce::Rectangle<float> within, const Mono &mono)
{
	const auto r = bar[item];
	drawTooltip(g, bar.tip(item), {r.getCentreX(), r.getY() - 30}, within, mono);
}

} // namespace lemus::panel
