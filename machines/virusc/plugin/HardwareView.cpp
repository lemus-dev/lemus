#include "HardwareView.h"

#include "Fonts.h"
#include "LcdFont.h"
#include "VirusCPanel.h"

#include <cmath>

namespace virusc {

namespace {

const juce::var &panelJson()
{
	static const juce::var v = juce::JSON::parse(juce::String::fromUTF8(VirusCPanel::panel_json, VirusCPanel::panel_jsonSize));
	return v;
}

juce::Colour colour(const juce::String &name)
{
	static std::map<juce::String, juce::Colour> table = [] {
		std::map<juce::String, juce::Colour> t;
		if (auto *obj = panelJson()["colours"].getDynamicObject())
			for (const auto &p : obj->getProperties())
				if (p.value.isObject())
					t[p.name.toString()] = juce::Colour::fromString("ff" + p.value["hex"].toString().removeCharacters("#"));
		return t;
	}();
	const auto it = table.find(name);
	return it != table.end() ? it->second : juce::Colours::magenta;
}

juce::Rectangle<float> rectOf(const juce::var &b)
{
	return b.isArray() && b.size() == 4
	           ? juce::Rectangle<float>::leftTopRightBottom(float(b[0]), float(b[1]), float(b[2]), float(b[3]))
	           : juce::Rectangle<float>();
}

juce::Path polyline(const juce::var &points, bool closed)
{
	juce::Path p;
	if (!points.isArray())
		return p;
	for (int i = 0; i < points.size(); ++i) {
		const juce::Point<float> q{float(points[i][0]), float(points[i][1])};
		if (i == 0)
			p.startNewSubPath(q);
		else
			p.lineTo(q);
	}
	if (closed)
		p.closeSubPath();
	return p;
}

// The ink of laid-out glyphs (a GlyphArrangement's own bounding box is the line's, ascent to descent).
juce::Rectangle<float> inkOf(const juce::GlyphArrangement &ga)
{
	juce::Path p;
	ga.createPath(p);
	return p.getBounds();
}

const lemus::panel::Mono &barMono()
{
	static const lemus::panel::Mono m = [](float size, int weight) { return fonts::mono(size, weight); };
	return m;
}

// A legend in Michroma, its ink fitted to the measured box: the cap height as measured, the width
// stretched or narrowed to the box (the panel's face is wider than Michroma in places).
void drawLegend(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> box, float capH, juce::Colour c)
{
	if (text.isEmpty() || box.isEmpty())
		return;
	const auto font = fonts::legend(capH);
	juce::GlyphArrangement ga;
	ga.addLineOfText(font, text, 0, 0);
	const auto ink = inkOf(ga);
	if (ink.isEmpty())
		return;
	const float sx = juce::jlimit(0.55f, 1.6f, box.getWidth() / ink.getWidth());
	const float sy = box.getHeight() / ink.getHeight();
	g.setColour(c);
	ga.draw(g, juce::AffineTransform::translation(-ink.getX(), -ink.getY())
	               .scaled(sx, juce::jlimit(0.8f, 1.25f, sy))
	               .translated(box.getX() + (box.getWidth() - ink.getWidth() * sx) / 2, box.getY()));
}

// The PART and PARAMETER legends: solid triangles at both ends of the word.
void drawArrowLegend(juce::Graphics &g, juce::String text, juce::Rectangle<float> box, float capH, juce::Colour c)
{
	const bool left = text.contains(juce::String::charToString(0x25c4)), right = text.contains(juce::String::charToString(0x25ba));
	text = text.removeCharacters(juce::String::charToString(0x25c4) + juce::String::charToString(0x25ba)).trim();
	const float t = capH * 0.9f;
	auto inner = box;
	g.setColour(c);
	if (left) {
		juce::Path p;
		const auto a = inner.removeFromLeft(t * 0.9f);
		p.addTriangle(a.getRight(), a.getCentreY() - t / 2, a.getRight(), a.getCentreY() + t / 2, a.getX(), a.getCentreY());
		g.fillPath(p);
	}
	if (right) {
		juce::Path p;
		const auto a = inner.removeFromRight(t * 0.9f);
		p.addTriangle(a.getX(), a.getCentreY() - t / 2, a.getX(), a.getCentreY() + t / 2, a.getRight(), a.getCentreY());
		g.fillPath(p);
	}
	const float textW = juce::jmin(inner.getWidth(), capH * 0.95f * float(text.length()) * 1.25f);
	drawLegend(g, text, inner.withSizeKeepingCentre(textW, capH), capH, c);
}

void drawSymbol(juce::Graphics &g, const juce::String &kind, juce::Rectangle<float> b, float stroke, juce::Colour c)
{
	juce::Path p;
	const float x0 = b.getX(), x1 = b.getRight(), y0 = b.getY(), y1 = b.getBottom(), ym = b.getCentreY(), w = b.getWidth();
	if (kind.startsWith("sine")) {
		for (int i = 0; i <= 40; ++i) {
			const float x = x0 + w * float(i) / 40, y = ym - (b.getHeight() / 2) * std::sin(juce::MathConstants<float>::twoPi * float(i) / 40);
			if (i == 0)
				p.startNewSubPath(x, y);
			else
				p.lineTo(x, y);
		}
	} else if (kind.startsWith("triangle")) {
		p.startNewSubPath(x0, ym);
		p.lineTo(x0 + w / 4, y0);
		p.lineTo(x0 + 3 * w / 4, y1);
		p.lineTo(x1, ym);
	} else if (kind.startsWith("saw")) {
		p.startNewSubPath(x0, y1);
		p.lineTo(x1, y0);
		p.lineTo(x1, y1);
	} else if (kind.startsWith("square") || kind.startsWith("pulse")) {
		const float mid = kind.startsWith("pulse") ? x0 + w * 0.3f : x0 + w / 2;
		p.startNewSubPath(x0, y1);
		p.lineTo(x0, y0);
		p.lineTo(mid, y0);
		p.lineTo(mid, y1);
		p.lineTo(x1, y1);
		p.lineTo(x1, y0);
	}
	g.setColour(c);
	g.strokePath(p, juce::PathStrokeType(stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}

// The display's arrows and degree sign, which the firmware may use from the controller's ROM.
std::array<uint8_t, 8> romSpecial(uint8_t code)
{
	switch (code) {
	case 0x7e: return {0x00, 0x04, 0x02, 0x1f, 0x02, 0x04, 0x00, 0x00};
	case 0x7f: return {0x00, 0x04, 0x08, 0x1f, 0x08, 0x04, 0x00, 0x00};
	case 0xdf: return {0x1c, 0x14, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00};
	default: return {};
	}
}

} // namespace

const std::vector<HardwareView::Element> &HardwareView::elements()
{
	static const std::vector<Element> list = [] {
		std::vector<Element> out;
		const auto &arr = panelJson()["elements"];
		for (int i = 0; i < arr.size(); ++i) {
			const auto &v = arr[i];
			Element e;
			e.type = v["type"].toString();
			e.id = v["id"].toString();
			e.label = v["label"].toString();
			e.box = rectOf(v["bbox"]);
			e.centre = v.hasProperty("x") ? juce::Point<float>{float(v["x"]), float(v["y"])} : e.box.getCentre();
			e.v = v;
			out.push_back(e);
		}
		return out;
	}();
	return list;
}

HardwareView::HardwareView(VirusCProcessor &p) : m_proc(p)
{
	const auto &els = elements();
	for (int i = 0; i < int(els.size()); ++i) {
		const auto &e = els[size_t(i)];
		if (e.type == "button" && e.v["mame_key"].isArray())
			m_keys.push_back({i, int(e.v["mame_key"][0]), int(e.v["mame_key"][1]),
			                  (e.v["section"].isString() && e.label.length() <= 6 ? e.v["section"].toString() + " " : juce::String()) + e.label});
		else if (e.type == "knob")
			m_knobs.push_back({i, dynamic_cast<juce::RangedAudioParameter *>(p.params().getParameter("knob" + e.v["mame_knob"].toString()))});
		else if (e.type == "led" && e.v["matrix"].isString()) {
			const auto m = e.v["matrix"].toString();
			m_leds.push_back({i, m.substring(0, 1).getIntValue(), (m[1] == 'B' ? 7 : 0) + m.substring(2, 3).getIntValue()});
		} else if (e.type == "led" && e.v.hasProperty("dsp_led"))
			m_leds.push_back({i, -1, int(e.v["dsp_led"])});
		else if (e.type == "lcd")
			m_lcdElement = i;
	}
	m_held.reset(m_keys.size(), [this](int key, bool down) {
		const auto &k = m_keys[size_t(key)];
		m_proc.setKey(k.row, k.bit, down);
		repaint();
	});
	m_barRear = m_bar.add(lemus::panel::Bar::kRear, "The rear panel: the jacks and the ON/OFF switch");
	setWantsKeyboardFocus(true);
	setSize(kWidth, kHeight);
	startTimerHz(30);
}

HardwareView::~HardwareView() = default;

void HardwareView::timerCallback()
{
	m_lcd = m_proc.lcd();
	m_proc.ledState(m_ledLevels, m_lfoLevels);
	repaint();
}

// ---------------------------------------------------------------- drawing

void HardwareView::drawStaticElement(juce::Graphics &g, const Element &e)
{
	const auto &v = e.v;
	const auto c = colour(v["colour"].toString());
	if (e.type == "case") {
		g.setColour(colour("case_black"));
		g.fillPath(polyline(v["points"], true));
	} else if (e.type == "region") {
		g.setColour(c);
		g.fillRoundedRectangle(e.box, float(v.getProperty("corner_r", 0.0)));
	} else if (e.type == "fill") {
		g.setColour(c);
		g.fillRect(e.box);
	} else if (e.type == "line") {
		g.setColour(c);
		g.strokePath(polyline(v["points"], false), juce::PathStrokeType(float(v.getProperty("stroke", 0.3))));
	} else if (e.type == "outline_box") {
		g.setColour(c);
		g.drawRoundedRectangle(e.box, float(v.getProperty("corner_r", 0.0)), float(v.getProperty("stroke", 0.3)));
	} else if (e.type == "label_box") {
		g.setColour(c);
		g.fillRoundedRectangle(e.box, float(v.getProperty("corner_r", 0.0)));
		const float capH = float(v.getProperty("cap_h", 2.0));
		drawLegend(g, e.label, e.box.withSizeKeepingCentre(juce::jmin(e.box.getWidth() - 0.6f, capH * 0.95f * float(e.label.length())), capH),
		           capH, colour(v["text_colour"].toString()));
	} else if (e.type == "text") {
		const float capH = float(v.getProperty("cap_h", 2.0));
		if (e.label.containsAnyOf(juce::String::charToString(0x25c4) + juce::String::charToString(0x25ba)))
			drawArrowLegend(g, e.label, e.box.reduced(0.1f), capH, c);
		else
			drawLegend(g, e.label, e.box.reduced(0.1f), capH, c);
	} else if (e.type == "text_arc") {
		// letters along the arc, angles from the top, clockwise
		const juce::Point<float> centre{float(v["centre"][0]), float(v["centre"][1])};
		const float r = float(v["r"]), a0 = float(v["start_deg"]), a1 = float(v["end_deg"]);
		const float capH = float(v.getProperty("cap_h", 1.9));
		const auto font = fonts::legend(capH);
		const int n = e.label.length();
		g.setColour(c);
		for (int i = 0; i < n; ++i) {
			const float a = juce::degreesToRadians(a0 + (a1 - a0) * (float(i) + 0.5f) / float(n));
			juce::GlyphArrangement ga;
			ga.addLineOfText(font, e.label.substring(i, i + 1), 0, 0);
			const auto ink = inkOf(ga);
			const juce::Point<float> at{centre.x + r * std::sin(a), centre.y - r * std::cos(a)};
			ga.draw(g, juce::AffineTransform::translation(-ink.getCentreX(), -ink.getCentreY()).rotated(a).translated(at));
		}
	} else if (e.type == "symbol") {
		drawSymbol(g, e.label, e.box, float(v.getProperty("stroke", 0.4)), c);
	} else if (e.type == "logo") {
		// the place's mark, by the ids the driver gives the places (virusc.cpp)
		static const std::map<juce::String, juce::String> marks = {
			{"maker", "access"}, {"name", "virus_c"}, {"tagline", "advanced_simulated_analog_synthesizer"}};
		const auto m = marks.find(v["logo"].toString());
		if (const auto *mark = m != marks.end() ? m_proc.artwork().find(m->second) : nullptr) {
			if (mark->picture != nullptr)
				mark->picture->drawWithin(g, e.box);
			else if (!mark->outline.isEmpty()) {
				g.setColour(c);
				g.fillPath(mark->outline);
			}
		}
	} else if (e.type == "knob") {
		const auto &scale = v["scale"];
		g.setColour(colour(scale["colour"].toString()));
		const float d = float(scale.getProperty("dot_d", 0.9));
		if (scale["points"].isArray())
			for (int i = 0; i < scale["points"].size(); ++i)
				g.fillEllipse(juce::Rectangle<float>(d, d).withCentre({float(scale["points"][i][0]), float(scale["points"][i][1])}));
		if (scale["labels"].isArray())
			for (int i = 0; i < scale["labels"].size(); ++i) {
				const auto &l = scale["labels"][i];
				drawLegend(g, l["text"].toString(), rectOf(l["bbox"]).reduced(0.1f), float(l.getProperty("cap_h", 1.9)), colour(scale["colour"].toString()));
			}
	} else if (e.type == "button") {
		// the recess the caps sit in
		if (const auto cut = rectOf(v["cutout_bbox"]); !cut.isEmpty()) {
			g.setColour(colour("gap_black"));
			g.fillRoundedRectangle(cut, 0.5f);
		}
	} else if (e.type == "lcd") {
		g.setColour(colour("lcd_bezel").darker(0.6f));
		g.fillRect(e.box.expanded(0.6f));
		g.setColour(colour("lcd_bg"));
		g.fillRect(e.box);
	}
}

void HardwareView::renderStatic(juce::Graphics &g)
{
	g.fillAll(colour("case_black"));
	g.addTransform(juce::AffineTransform::scale(kPxPerMm));
	for (const auto &e : elements())
		if (e.type == "case")
			drawStaticElement(g, e);
	// regions and fills under everything printed
	for (const auto &e : elements())
		if (e.type == "region" || e.type == "fill")
			drawStaticElement(g, e);
	for (const auto &e : elements())
		if (e.type != "case" && e.type != "region" && e.type != "fill" && e.type != "led")
			drawStaticElement(g, e);
}

void HardwareView::drawKnob(juce::Graphics &g, const Element &e, float value, bool hover)
{
	const auto &v = e.v;
	const float kd = float(v.getProperty("knob_d", 13.0)), cd = float(v.getProperty("cap_d", 12.3));
	const auto &rot = v["rotation"];
	const float a0 = float(rot.getProperty("min_deg", -130.0)), a1 = float(rot.getProperty("max_deg", 130.0));
	const auto body = juce::Rectangle<float>(kd, kd).withCentre(e.centre);
	// a soft shadow below, the skirt, the top
	g.setColour(juce::Colours::black.withAlpha(0.45f));
	g.fillEllipse(body.translated(0.5f, 0.9f).expanded(0.4f));
	const auto grey = colour(v["colour"].toString());
	g.setGradientFill(juce::ColourGradient(grey.brighter(0.35f), body.getX(), body.getY(), grey.darker(0.5f), body.getRight(), body.getBottom(), false));
	g.fillEllipse(body);
	const auto top = juce::Rectangle<float>(cd, cd).withCentre(e.centre);
	g.setGradientFill(juce::ColourGradient(grey.brighter(hover ? 0.3f : 0.18f), top.getCentreX(), top.getY(), grey.darker(0.15f), top.getCentreX(), top.getBottom(), false));
	g.fillEllipse(top);
	g.setColour(juce::Colours::black.withAlpha(0.35f));
	g.drawEllipse(top, 0.15f);
	// the pointer: a light stripe from near the centre to the edge
	const float a = juce::degreesToRadians(a0 + (a1 - a0) * value);
	const juce::Point<float> dir{std::sin(a), -std::cos(a)};
	g.setColour(colour(v["pointer_colour"].toString()).brighter(0.4f));
	g.drawLine(juce::Line<float>(e.centre + dir * (cd * 0.12f), e.centre + dir * (cd * 0.48f)), 0.85f);
}

void HardwareView::drawKey(juce::Graphics &g, const Element &e, bool down, bool hover)
{
	auto cap = e.box.reduced(0.25f);
	if (down)
		cap = cap.reduced(0.3f).translated(0, 0.25f);
	const auto grey = colour(e.v["colour"].toString());
	g.setColour(juce::Colours::black.withAlpha(0.5f));
	g.fillRoundedRectangle(cap.translated(0.3f, down ? 0.2f : 0.6f), 0.6f);
	const auto top = down ? grey.darker(0.25f) : hover ? grey.brighter(0.12f) : grey;
	g.setGradientFill(juce::ColourGradient(top.brighter(0.12f), cap.getX(), cap.getY(), top.darker(0.2f), cap.getX(), cap.getBottom(), false));
	g.fillRoundedRectangle(cap, 0.6f);
	g.setColour(juce::Colours::black.withAlpha(0.3f));
	g.drawRoundedRectangle(cap, 0.6f, 0.12f);
}

void HardwareView::drawLed(juce::Graphics &g, const Element &e, float level)
{
	const float d = float(e.v.getProperty("d", 3.0));
	const auto r = juce::Rectangle<float>(d, d).withCentre(e.centre);
	const juce::Colour off{0xff3a1012}, on{0xffff3b1f}, core{0xffffd2b8};
	g.setColour(off.interpolatedWith(on, level));
	g.fillEllipse(r);
	if (level > 0.02f) {
		g.setGradientFill(juce::ColourGradient(on.withAlpha(0.55f * level), e.centre.x, e.centre.y, on.withAlpha(0.0f), e.centre.x + d * 1.4f, e.centre.y, true));
		g.fillEllipse(r.expanded(d * 0.9f));
		g.setColour(core.withAlpha(0.8f * level));
		g.fillEllipse(r.reduced(d * 0.28f));
	}
	g.setColour(juce::Colours::black.withAlpha(0.4f));
	g.drawEllipse(r, 0.12f);
}

const std::array<uint8_t, 8> &HardwareView::glyph(uint8_t code)
{
	const auto it = m_glyphs.find(code);
	if (it != m_glyphs.end())
		return it->second;
	std::array<uint8_t, 8> rows{};
	if (code == 0x7e || code == 0x7f || code == 0xdf)
		rows = romSpecial(code);
	else if (code >= 0x20 && code < 0x7e)
		for (int r = 0; r < 8; ++r)
			rows[size_t(r)] = kLcdFont[code - 0x20][r];
	return m_glyphs[code] = rows;
}

void HardwareView::drawLcd(juce::Graphics &g, const Element &e)
{
	const auto &v = e.v;
	const float ox = float(v["char_origin"][0]), oy = float(v["char_origin"][1]);
	const float cp = float(v["char_pitch"]), lp = float(v["line_pitch"]);
	const float px = float(v["dot_pitch"][0]), py = float(v["dot_pitch"][1]);
	const float dw = float(v["dot"][0]), dh = float(v["dot"][1]);
	const auto offCol = colour("lcd_off"), onCol = colour("lcd_on");
	const bool booted = m_proc.booted();
	for (int line = 0; line < 2; ++line)
		for (int col = 0; col < 16; ++col) {
			const uint8_t code = uint8_t(m_lcd.text[line][col]);
			std::array<uint8_t, 8> rows{};
			if (booted && m_lcd.on) {
				if (code < 16)
					for (int r = 0; r < 8; ++r)
						rows[size_t(r)] = uint8_t(m_lcd.cgram[(code & 7) * 8 + r] & 0x1f);
				else
					rows = glyph(code);
			}
			for (int r = 0; r < 8; ++r)
				for (int c = 0; c < 5; ++c) {
					const bool lit = (rows[size_t(r)] >> (4 - c)) & 1;
					g.setColour(lit ? onCol : offCol);
					g.fillRect(ox + float(col) * cp + float(c) * px, oy + float(line) * lp + float(r) * py, dw, dh);
				}
		}
}

juce::String HardwareView::status() const
{
	if (!m_proc.romLoaded())
		return "NO FIRMWARE: add virus_c_660x368.bin to the ROM folder";
	if (!m_proc.powered())
		return "OFF: the ON/OFF switch on the rear panel";
	if (!m_proc.booted())
		return "STARTING: the firmware is booting";
	if (m_dragKnob >= 0) {
		const auto &k = m_knobs[size_t(m_dragKnob)];
		return elements()[size_t(k.element)].label + "  " + (k.param ? k.param->getCurrentValueAsText() : juce::String());
	}
	if (m_held.anyLatched())
		return lemus::panel::heldStatus(m_held.latchedNames([this](int i) { return m_keys[size_t(i)].name; }));
	return {};
}

void HardwareView::drawBar(juce::Graphics &g)
{
	lemus::panel::drawBarBackground(g, m_bar);
	lemus::panel::drawStatus(g, m_bar.status(), status(), barMono(),
	                         m_held.anyLatched() ? lemus::panel::col::accent : lemus::panel::col::status);
	lemus::panel::drawPill(g, m_bar[m_barRear], "REAR", m_rear, m_hoverBar == m_barRear, barMono());
	lemus::panel::drawPill(g, m_bar.help(), "?", m_help, m_hoverBar == lemus::panel::Bar::kHelp, barMono());
	if (m_hoverBar >= 0 || m_hoverBar == lemus::panel::Bar::kHelp)
		lemus::panel::drawBarTip(g, m_bar, m_hoverBar, getLocalBounds().toFloat(), barMono());
}

// The rear view: the case seen from behind and a little above, the top plate's back edge at the top,
// the rear face below it with the jacks where docs/panel.json measured them (mirrored, seen from
// behind), cables in the jacks in use, and the ON/OFF rocker.
namespace {
constexpr float kRearTop = 40.0f, kRearH = 60.0f;
const juce::var &rearItems()
{
	static const juce::var items = [] {
		const auto doc = juce::JSON::parse(juce::String::fromUTF8(VirusCPanel::panel_json, VirusCPanel::panel_jsonSize));
		return doc["rear_panel"]["items"];
	}();
	return items;
}
juce::Point<float> rearAt(const juce::var &it)
{
	return {HardwareView::kPanelW - float(it["x"]), kRearTop + float(it["y_from_top"])};
}
} // namespace

juce::Rectangle<float> HardwareView::rearSwitch() const
{
	for (const auto &it : *rearItems().getArray())
		if (it["id"] == "rear_switch")
			return juce::Rectangle<float>(12.8f, 18.0f).withCentre(rearAt(it));
	return {};
}

void HardwareView::drawRear(juce::Graphics &g)
{
	g.fillAll(juce::Colour(0xff16181b));
	juce::Graphics::ScopedSaveState s(g);
	g.addTransform(juce::AffineTransform::scale(kPxPerMm));
	const float w = kPanelW;
	// the top plate's back edge, falling away, and its bend
	juce::ColourGradient top(colour("panel_ground").darker(0.35f), 0, kRearTop - 26, colour("panel_ground"), 0, kRearTop - 2, false);
	g.setGradientFill(top);
	g.fillRoundedRectangle(2, kRearTop - 26, w - 4, 26, 3);
	g.setColour(colour("rear_bend"));
	g.fillRect(2.0f, kRearTop - 2.2f, w - 4, 1.1f);
	// the rear face
	juce::ColourGradient face(colour("case_black").brighter(0.08f), 0, kRearTop, colour("case_black").darker(0.25f), 0, kRearTop + kRearH, false);
	g.setGradientFill(face);
	g.fillRoundedRectangle(0, kRearTop - 1, w, kRearH + 1, 2.5f);
	g.setColour(colour("gap_black"));
	g.fillRect(0.0f, kRearTop + kRearH - 1.2f, w, 1.2f);

	const auto print = colour("print_white");
	const auto metal = juce::Colour(0xff9a9da2), hole = juce::Colour(0xff0b0c0e);
	std::vector<std::pair<juce::Point<float>, bool>> cables; // where, a 6.3 mm plug (or a DIN plug)
	for (const auto &it : *rearItems().getArray()) {
		const auto c = rearAt(it);
		const auto id = it["id"].toString(), kind = it["kind"].toString();
		// the legend above the socket
		// (two lines where it has two names: OUT 1 L / PHONES, the jacks are 16.5 mm apart)
		const auto label = it["label"].toString();
		g.setColour(print.withAlpha(0.85f));
		g.setFont(juce::FontOptions(2.4f));
		const auto first = label.upToFirstOccurrenceOf(" / ", false, false), second = label.fromFirstOccurrenceOf(" / ", false, false);
		g.drawText(first, juce::Rectangle<float>(c.x - 8, second.isEmpty() ? c.y - 14.5f : c.y - 17.0f, 16, 3), juce::Justification::centred, false);
		if (second.isNotEmpty())
			g.drawText(second, juce::Rectangle<float>(c.x - 8, c.y - 14.0f, 16, 3), juce::Justification::centred, false);
		if (kind.startsWith("5-pin DIN")) {
			g.setColour(metal.darker(0.2f));
			g.fillEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre(c));
			g.setColour(hole);
			g.fillEllipse(juce::Rectangle<float>(13.0f, 13.0f).withCentre(c));
			g.setColour(metal);
			for (int pin = 0; pin < 5; ++pin) {
				const float a = juce::MathConstants<float>::pi * (1.0f + float(pin) / 4.0f);
				g.fillEllipse(juce::Rectangle<float>(1.3f, 1.3f).withCentre(c + juce::Point<float>(4.0f * std::cos(a), -4.0f * std::sin(a) + 0.8f)));
			}
			g.fillRect(juce::Rectangle<float>(1.6f, 1.6f).withCentre(c + juce::Point<float>(0, 5.8f)));
			if (id == "rear_midi_in")
				cables.push_back({c, false});
		} else if (kind.startsWith("6.3 mm")) {
			g.setColour(metal);
			g.fillEllipse(juce::Rectangle<float>(11.0f, 11.0f).withCentre(c));
			g.setColour(metal.darker(0.5f));
			g.drawEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(c), 0.4f);
			g.setColour(hole);
			g.fillEllipse(juce::Rectangle<float>(6.5f, 6.5f).withCentre(c));
			const bool used = id.startsWith("rear_out1") || (id.startsWith("rear_out2") && m_proc.jackOn(1))
			                  || (id.startsWith("rear_out3") && m_proc.jackOn(2)) || (id.startsWith("rear_ext_in") && m_proc.inputOn());
			if (used)
				cables.push_back({c, true});
		} else if (kind.startsWith("DC")) {
			g.setColour(metal.darker(0.1f));
			g.fillEllipse(juce::Rectangle<float>(9.5f, 9.5f).withCentre(c));
			g.setColour(hole);
			g.fillEllipse(juce::Rectangle<float>(6.5f, 6.5f).withCentre(c));
			g.setColour(metal);
			g.fillEllipse(juce::Rectangle<float>(1.6f, 1.6f).withCentre(c));
			cables.push_back({c, true});
		} else if (id == "rear_switch") {
			const auto r = rearSwitch();
			const bool on = m_proc.powered();
			g.setColour(juce::Colour(0xff0e0f11));
			g.fillRoundedRectangle(r.expanded(1.2f), 1.2f);
			const auto upper = r.withHeight(r.getHeight() / 2), lower = r.withTrimmedTop(r.getHeight() / 2);
			// the pressed half dark, the other raised and lit from above; I (on) at the top
			g.setColour(juce::Colour(0xff26282c));
			g.fillRect(on ? upper : lower);
			juce::ColourGradient lit(juce::Colour(0xff5c5f65), 0, r.getY(), juce::Colour(0xff34363a), 0, r.getBottom(), false);
			g.setGradientFill(lit);
			g.fillRect(on ? lower : upper);
			g.setColour(print.withAlpha(0.8f));
			g.setFont(juce::FontOptions(3.2f));
			g.drawText("I", upper, juce::Justification::centred);
			g.drawText("O", lower, juce::Justification::centred);
			if (m_hoverSwitch) {
				g.setColour(lemus::panel::col::accent.withAlpha(0.8f));
				g.drawRoundedRectangle(r.expanded(2.0f), 1.5f, 0.5f);
			}
		}
	}
	// the plugs and their cables, falling to the desk
	for (const auto &[c, jack] : cables) {
		const float bodyW = jack ? 9.0f : 14.5f;
		const auto body = juce::Rectangle<float>(bodyW, bodyW).withCentre(c);
		juce::ColourGradient plug(juce::Colour(0xff4a4d53), 0, body.getY(), juce::Colour(0xff1c1e21), 0, body.getBottom(), false);
		g.setGradientFill(plug);
		g.fillEllipse(body);
		g.setColour(juce::Colour(0xff6a6d73));
		g.drawEllipse(body, 0.5f);
		juce::Path cable;
		cable.startNewSubPath(c);
		cable.cubicTo(c.x, c.y + 18, c.x + 2, kRearTop + kRearH + 20, c.x + 4, kPanelH);
		g.setColour(juce::Colour(0xff101113));
		g.strokePath(cable, juce::PathStrokeType(3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
		g.setColour(juce::Colour(0xff34373c));
		g.strokePath(cable, juce::PathStrokeType(0.6f), juce::AffineTransform::translation(-0.9f, 0));
	}
}

void HardwareView::paint(juce::Graphics &g)
{
	if (m_rear) {
		drawRear(g);
		drawBar(g);
		return;
	}
	const float scale = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
	if (m_static.isNull() || std::abs(scale - m_staticScale) > 0.01f) {
		m_staticScale = scale;
		m_static = juce::Image(juce::Image::RGB, juce::roundToInt(kWidth * scale), juce::roundToInt(kPanelHeight * scale), true);
		juce::Graphics sg(m_static);
		sg.addTransform(juce::AffineTransform::scale(scale));
		renderStatic(sg);
	}
	g.drawImageTransformed(m_static, juce::AffineTransform::scale(1.0f / scale));

	{
		juce::Graphics::ScopedSaveState s(g);
		g.addTransform(juce::AffineTransform::scale(kPxPerMm));
		const auto &els = elements();
		for (size_t i = 0; i < m_knobs.size(); ++i) {
			const auto &k = m_knobs[i];
			drawKnob(g, els[size_t(k.element)], k.param ? k.param->getValue() : 0.5f, int(i) == m_hoverKnob || int(i) == m_dragKnob);
		}
		for (size_t i = 0; i < m_keys.size(); ++i)
			drawKey(g, els[size_t(m_keys[i].element)], m_held.down(int(i)), int(i) == m_hoverKey);
		const bool booted = m_proc.booted();
		for (const auto &l : m_leds) {
			const float level = l.column < 0 ? m_lfoLevels[l.line] : m_ledLevels[l.column][l.line];
			drawLed(g, els[size_t(l.element)], booted ? juce::jlimit(0.0f, 1.0f, level) : 0.0f);
		}
		if (m_lcdElement >= 0)
			drawLcd(g, els[size_t(m_lcdElement)]);
	}

	drawBar(g);
	if (m_help)
		lemus::panel::drawHelp(g, getLocalBounds().toFloat().withTrimmedBottom(kBarHeight), "VIRUS C",
		                       {"Left / Right   PARAMETER < >", "Down / Up      VALUE - +", "Page Down / Up PART < >",
		                        "Return         STORE", "U              UNDO", "E              EDIT", "M / S          MULTI / SINGLE",
		                        "Knobs: drag up and down, shift for fine, double-click for the start position"},
		                       barMono());
}

// ---------------------------------------------------------------- mouse and keys

int HardwareView::knobAt(juce::Point<float> mm) const
{
	const auto &els = elements();
	for (size_t i = 0; i < m_knobs.size(); ++i) {
		const auto &e = els[size_t(m_knobs[i].element)];
		if (e.centre.getDistanceFrom(mm) <= float(e.v.getProperty("knob_d", 13.0)) / 2 + 1.0f)
			return int(i);
	}
	return -1;
}

int HardwareView::keyAt(juce::Point<float> mm) const
{
	const auto &els = elements();
	for (size_t i = 0; i < m_keys.size(); ++i)
		if (els[size_t(m_keys[i].element)].box.contains(mm))
			return int(i);
	return -1;
}

void HardwareView::mouseMove(const juce::MouseEvent &e)
{
	const auto mm = toMm(e.position);
	m_hoverKnob = knobAt(mm);
	m_hoverKey = m_hoverKnob < 0 ? keyAt(mm) : -1;
	m_hoverBar = e.position.y >= float(kPanelHeight) ? m_bar.hit(e.position) : -1;
	m_hoverSwitch = m_rear && e.position.y < float(kPanelHeight) && rearSwitch().expanded(2.0f).contains(toMm(e.position));
	repaint();
}

void HardwareView::mouseExit(const juce::MouseEvent &)
{
	m_hoverKnob = m_hoverKey = m_hoverBar = -1;
	repaint();
}

void HardwareView::mouseDown(const juce::MouseEvent &e)
{
	grabKeyboardFocus();
	if (m_help) {
		m_help = false;
		repaint();
		return;
	}
	if (e.position.y >= float(kPanelHeight)) {
		const int hit = m_bar.hit(e.position);
		if (hit == lemus::panel::Bar::kHelp)
			m_help = true;
		else if (hit == m_barRear && m_barRear >= 0)
			m_rear = !m_rear;
		repaint();
		return;
	}
	if (m_rear) {
		if (rearSwitch().expanded(2.0f).contains(toMm(e.position)))
			m_proc.setPowered(!m_proc.powered());
		repaint();
		return;
	}
	const auto mm = toMm(e.position);
	if (const int k = knobAt(mm); k >= 0 && m_knobs[size_t(k)].param != nullptr) {
		m_dragKnob = k;
		m_dragStart = e.position.y;
		m_dragValue = m_knobs[size_t(k)].param->getValue();
		m_knobs[size_t(k)].param->beginChangeGesture();
		return;
	}
	if (const int k = keyAt(mm); k >= 0 && m_held.mouseDown(k, e.mods))
		m_mouseKey = k;
	repaint();
}

void HardwareView::mouseDrag(const juce::MouseEvent &e)
{
	if (m_dragKnob < 0)
		return;
	auto *p = m_knobs[size_t(m_dragKnob)].param;
	// relative to the last position, so shift (fine) can come and go during a drag
	m_dragValue = juce::jlimit(0.0f, 1.0f, m_dragValue + (m_dragStart - e.position.y) / (e.mods.isShiftDown() ? 1000.0f : 200.0f));
	m_dragStart = e.position.y;
	const float v = m_dragValue;
	p->setValueNotifyingHost(v);
	repaint();
}

void HardwareView::mouseUp(const juce::MouseEvent &)
{
	if (m_dragKnob >= 0) {
		m_knobs[size_t(m_dragKnob)].param->endChangeGesture();
		m_dragKnob = -1;
	}
	if (m_mouseKey >= 0) {
		m_held.mouseUp(m_mouseKey);
		m_mouseKey = -1;
	}
	repaint();
}

void HardwareView::mouseDoubleClick(const juce::MouseEvent &e)
{
	if (const int k = knobAt(toMm(e.position)); k >= 0 && m_knobs[size_t(k)].param != nullptr) {
		auto *p = m_knobs[size_t(k)].param;
		p->beginChangeGesture();
		p->setValueNotifyingHost(p->getDefaultValue());
		p->endChangeGesture();
	}
}

void HardwareView::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
	if (const int k = knobAt(toMm(e.position)); k >= 0 && m_knobs[size_t(k)].param != nullptr) {
		auto *p = m_knobs[size_t(k)].param;
		const float step = (w.deltaY + w.deltaX) * (e.mods.isShiftDown() ? 0.05f : 0.25f);
		p->beginChangeGesture();
		p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->getValue() + step));
		p->endChangeGesture();
	}
}

bool HardwareView::keyPressed(const juce::KeyPress &k)
{
	switch (lemus::panel::commonKey(k)) {
	case lemus::panel::Common::Help:
		m_help = !m_help;
		repaint();
		return true;
	case lemus::panel::Common::Escape:
		m_help = false;
		m_held.releaseAll(true);
		repaint();
		return true;
	case lemus::panel::Common::None: break;
	}
	static const std::map<int, juce::String> map = {
		{juce::KeyPress::leftKey, "button_parameter_down"}, {juce::KeyPress::rightKey, "button_parameter_up"},
		{juce::KeyPress::downKey, "button_value_down"},     {juce::KeyPress::upKey, "button_value_up"},
		{juce::KeyPress::pageDownKey, "button_part_down"},  {juce::KeyPress::pageUpKey, "button_part_up"},
		{juce::KeyPress::returnKey, "button_store"},        {'U', "button_undo"},
		{'E', "button_edit"},                               {'M', "button_multi"},
		{'S', "button_single"}};
	int code = k.getKeyCode();
	if (code >= 'a' && code <= 'z')
		code -= 32;
	const auto it = map.find(code);
	if (it == map.end() || k.getModifiers().isCommandDown())
		return false;
	for (size_t i = 0; i < m_keys.size(); ++i)
		if (elements()[size_t(m_keys[i].element)].id == it->second) {
			m_held.keyDown(k.getKeyCode(), int(i));
			return true;
		}
	return false;
}

bool HardwareView::keyStateChanged(bool)
{
	return m_held.keysChanged();
}

void HardwareView::focusLost(FocusChangeType)
{
	m_held.focusLost();
}

} // namespace virusc
