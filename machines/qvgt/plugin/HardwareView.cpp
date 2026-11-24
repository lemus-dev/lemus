#include "HardwareView.h"
#include "Fonts.h"
#include "QVGTPanel.h"

namespace qvgt {

namespace {

const lemus::panel::Mono &barMono()
{
	static const lemus::panel::Mono m = [](float size, int weight) { return fonts::mono(size, weight); };
	return m;
}

// docs/panel.json, built in.
const juce::var &panel()
{
	static const juce::var v = juce::JSON::parse(juce::String::fromUTF8(QVGTPanel::panel_json, QVGTPanel::panel_jsonSize));
	return v;
}

float num(const juce::var &v, float fallback = 0) { return v.isVoid() ? fallback : float(double(v)); }
juce::Point<float> pointOf(const juce::var &v) { return {num(v[0]), num(v[1])}; }
juce::Rectangle<float> boxOf(const juce::var &v) { return {num(v[0]), num(v[1]), num(v[2]), num(v[3])}; }
juce::Colour colour(const juce::String &name)
{
	const auto s = panel()["colours"][juce::Identifier(name)].toString();
	return juce::Colour::fromString("ff" + s.fromFirstOccurrenceOf("#", false, false));
}

constexpr float kPx = HardwareView::kPxPerMm;

// Panel legends in Helvetica (its cap height about 0.717 of the font's height), cap height in mm.
juce::Font legendFont(float capMm, bool bold, bool italic)
{
	int style = bold ? juce::Font::bold : juce::Font::plain;
	if (italic)
		style |= juce::Font::italic;
	return juce::Font(juce::FontOptions("Helvetica Neue", capMm * kPx / 0.717f, style));
}

// Text with its cap height centred on `midY` (mm): left of, centred on or right of x; or squeezed
// to `width` when it's given.
void drawLegend(juce::Graphics &g, const juce::String &text, float x, float midY, float capMm, juce::Colour c, bool bold, bool italic,
                juce::Justification just, float width = 0)
{
	const auto font = legendFont(capMm, bold, italic);
	juce::GlyphArrangement ga;
	ga.addLineOfText(font, text, 0, 0);
	auto bounds = ga.getBoundingBox(0, -1, true);
	float sx = 1;
	if (width > 0 && bounds.getWidth() > 0)
		sx = width * kPx / bounds.getWidth();
	const float w = bounds.getWidth() * sx;
	float left = x * kPx;
	if (width <= 0) {
		if (just.testFlags(juce::Justification::horizontallyCentred))
			left -= w / 2;
		else if (just.testFlags(juce::Justification::right))
			left -= w;
	}
	const float baseline = midY * kPx + capMm * kPx / 2;
	g.setColour(c);
	ga.draw(g, juce::AffineTransform::translation(-bounds.getX(), 0).scaled(sx, 1).translated(left, baseline));
}

float textWidthMm(const juce::String &text, float capMm, bool bold, bool italic)
{
	juce::GlyphArrangement ga;
	ga.addLineOfText(legendFont(capMm, bold, italic), text, 0, 0);
	return ga.getBoundingBox(0, -1, true).getWidth() / kPx;
}

float rearJackY() { return num(panel()["rear"]["jack_y"], 17.6f); }

// The computer keys that press panel keys (the firmware's key numbers), as the key list and the
// tooltips name them.
struct ComputerKey {
	int code, key;
	const char *shown;
};
const std::vector<ComputerKey> &computerKeys()
{
	static const std::vector<ComputerKey> keys = {{juce::KeyPress::leftKey, 6, "Left"},     {juce::KeyPress::rightKey, 7, "Right"},
	                                              {juce::KeyPress::downKey, 2, "Down"},     {juce::KeyPress::upKey, 3, "Up"},
	                                              {juce::KeyPress::returnKey, 10, "Return"}, {'P', 11, "P"},
	                                              {'B', 5, "B"}};
	return keys;
}

} // namespace

HardwareView::HardwareView(QVGTProcessor &p) : m_proc(p)
{
	const auto &v = panel();
	const auto cap = v["key_cap"];
	const float kw = num(cap[0], 15.1f), kh = num(cap[1], 5.7f);
	for (const auto &k : *v["keys"].getArray()) {
		const auto row = v["rows"][juce::Identifier(k["row"].toString())];
		const float w = num(k["w"], kw);
		KeyRef ref{int(k["key"]), k["name"].toString(), {num(k["x"]) - w / 2, num(row["key_y"]), w, kh}};
		if (k["led"].isArray()) {
			ref.ledLatch = int(k["led"][0]);
			ref.ledBit = int(k["led"][1]);
		}
		m_keys.push_back(ref);
	}
	for (const auto &k : *v["knobs"].getArray())
		m_knobs.push_back({k["name"].toString(), pointOf(k["c"]), num(k["d"], 15.8f), num(k["label_y"]), p.params().getParameter(k["param"].toString())});
	m_power = boxOf(v["power"]["box"]);
	for (const auto &mark : p.artwork().marks())
		m_logos[mark.id] = mark;
	for (const auto &j : *v["rear"]["jacks"].getArray()) {
		juce::StringArray label;
		for (const auto &l : *j["label"].getArray())
			label.add(l.toString());
		m_jacks.push_back({j["name"].toString(), num(j["x"]), num(j["d"], 19.0f), j.hasProperty("footswitch") ? int(j["footswitch"]) : -1,
		                   j["kind"].toString() == "din", label});
	}
	m_held.reset(m_keys.size(), [this](int i, bool down) { m_proc.setKey(m_keys[size_t(i)].key, down, m_hardPress); });
	m_barRear = m_bar.add(lemus::panel::Bar::kRear, "The rear panel: click the BYPASS or ADVANCE jack to press its footswitch");
	setWantsKeyboardFocus(true);
	setOpaque(true);
	startTimerHz(30);
}

HardwareView::~HardwareView()
{
	stopTimer();
	if (m_footswitch >= 0)
		m_proc.setFootswitch(m_footswitch, false);
}

void HardwareView::timerCallback()
{
	m_display = m_proc.display();
	repaint();
}

void HardwareView::paint(juce::Graphics &g)
{
	if (m_rear)
		paintRear(g);
	else
		paintFront(g);
	drawBar(g);
	if (!m_help)
		drawTip(g);
	if (m_help)
		// over the bar too: the 1U panel alone is too short for the list
		lemus::panel::drawHelp(g, getLocalBounds().toFloat(), "QUADRAVERB GT",
		                       {"Left / Right   PAGE down / up", "Down / Up      VALUE down / up", "Return         STORE", "P              PROG",
		                        "B              BYPASS", "Shift with a key or a click: a hard press (the keys sense force: harder scrolls faster)",
		                        "Knobs: drag up and down, Shift for fine, double-click for the start position, or the mouse wheel",
		                        "REAR: click BYPASS or ADVANCE for a footswitch press"},
		                       barMono());
}

void HardwareView::paintFront(juce::Graphics &g)
{
	const auto &v = panel();
	const auto face = colour("face"), legend = colour("legend"), digit = colour("digit"), blue = colour("blue");
	g.fillAll(face);
	// brushed: faint horizontal grain
	for (int y = 0; y < kPanelHeight; y += 2)
		g.setColour(juce::Colours::white.withAlpha((y * 7919 % 13) * 0.0012f)), g.fillRect(0, y, kWidth, 1);

	// rack slots and screws
	const auto slots = v["slots"];
	for (const auto &c : *slots["centres"].getArray()) {
		const auto r = juce::Rectangle<float>(num(slots["w"]), num(slots["h"])).withCentre(pointOf(c));
		g.setColour(juce::Colour(0xff0b0b0d));
		g.fillRoundedRectangle(toView(r), r.getHeight() * kPx / 2);
		g.setColour(juce::Colours::white.withAlpha(0.08f));
		g.drawRoundedRectangle(toView(r), r.getHeight() * kPx / 2, 1.0f);
	}
	const auto screws = v["screws"];
	for (const auto &c : *screws["centres"].getArray()) {
		const auto r = toView(juce::Rectangle<float>(num(screws["d"]), num(screws["d"])).withCentre(pointOf(c)));
		g.setColour(juce::Colour(0xff2c2c30));
		g.fillEllipse(r);
		g.setColour(juce::Colours::white.withAlpha(0.15f));
		g.drawEllipse(r.reduced(0.5f), 1.0f);
		g.setColour(juce::Colour(0xff0e0e10));
		const auto m = r.getCentre();
		const float a = r.getWidth() * 0.32f;
		g.drawLine(m.x - a, m.y, m.x + a, m.y, 1.6f);
		g.drawLine(m.x, m.y - a, m.x, m.y + a, 1.6f);
	}

	// brand marks: the user's artwork, or none
	for (const auto &l : *v["logos"].getArray()) {
		const auto it = m_logos.find(l["mark"].toString());
		if (it == m_logos.end())
			continue;
		const auto box = toView(boxOf(l["box"]));
		if (it->second.picture != nullptr) {
			it->second.picture->drawWithin(g, box);
		} else {
			g.setColour(l["place"].toString() == "maker" ? legend : blue);
			g.fillPath(it->second.outline, juce::AffineTransform::scale(kPx));
		}
	}
	// the rule from the Alesis logo to the QuadraVerb GT logo
	{
		const auto r = v["rule"];
		const auto a = pointOf(r["from"]) * kPx, b = pointOf(r["to"]) * kPx;
		g.setColour(colour("rule"));
		g.drawLine(a.x, a.y, b.x, b.y, num(r["w"]) * kPx);
	}

	// the signal display: the -18 dB LED lights first, CLIP with the DSP's overflow
	{
		const auto m = v["meter"];
		const float cap = num(m["cap"], 2.6f), d = num(m["led_d"], 2.6f);
		const auto &rows = *m["rows"].getArray();
		for (int i = 0; i < rows.size(); ++i) {
			const auto &row = rows[i];
			const float y = num(row["y"]);
			drawLegend(g, row["label"].toString(), num(m["labels_right"]), y, cap, legend, false, true, juce::Justification::right);
			const bool lit = m_proc.powered() && m_display.headroom >= rows.size() - i;
			const auto on = colour(row["colour"].toString());
			const auto r = toView(juce::Rectangle<float>(d, d).withCentre({num(m["led_x"]), y}));
			g.setColour(lit ? on : on.withMultipliedBrightness(0.22f).withMultipliedSaturation(0.6f));
			g.fillEllipse(r);
			g.setColour(juce::Colours::white.withAlpha(lit ? 0.45f : 0.12f));
			g.fillEllipse(r.reduced(r.getWidth() * 0.3f).translated(-r.getWidth() * 0.12f, -r.getHeight() * 0.12f));
		}
	}

	for (int i = 0; i < int(m_knobs.size()); ++i)
		drawKnob(g, m_knobs[size_t(i)], i == m_hoverKnob);
	drawLcd(g);

	for (const auto &t : *v["texts"].getArray()) {
		const auto b = boxOf(t["box"]);
		const auto c = colour(t["colour"].toString());
		drawLegend(g, t["text"].toString(), b.getX(), b.getCentreY(), b.getHeight(), c, bool(t["bold"]), bool(t["italic"]),
		           juce::Justification::left, b.getWidth());
		if (t.hasProperty("rule_below")) {
			const float y = (b.getBottom() + num(t["rule_below"]) * 0.6f) * kPx;
			g.setColour(c);
			g.drawLine(b.getX() * kPx, y, b.getRight() * kPx, y, 0.35f * kPx);
		}
	}
	for (const auto &t : *v["triangles"].getArray()) {
		const auto c = pointOf(t["c"]) * kPx;
		const float w = num(t["w"]) * kPx, h = num(t["h"]) * kPx;
		juce::Path p;
		if (bool(t["up"]))
			p.addTriangle(c.x - w / 2, c.y + h / 2, c.x + w / 2, c.y + h / 2, c.x, c.y - h / 2);
		else
			p.addTriangle(c.x - w / 2, c.y - h / 2, c.x + w / 2, c.y - h / 2, c.x, c.y + h / 2);
		g.setColour(legend);
		g.strokePath(p, juce::PathStrokeType(0.32f * kPx));
	}

	// the keys and their legends
	const float cap = num(v["key_label_cap"], 2.7f);
	for (size_t i = 0; i < m_keys.size(); ++i)
		drawKey(g, m_keys[i], m_held.down(int(i)), int(i) == m_hoverKey);
	for (const auto &k : *v["keys"].getArray()) {
		if (!k.hasProperty("label"))
			continue;
		const auto row = v["rows"][juce::Identifier(k["row"].toString())];
		const auto label = k["label"].toString(), num_ = k["digit"].toString();
		const float y = num(row["label_y"]), gap = 1.3f;
		const float wl = textWidthMm(label, cap, false, false), wd = num_.isEmpty() ? 0 : textWidthMm(num_, cap, false, false) + gap;
		const float left = num(k["x"]) - (wl + wd) / 2;
		drawLegend(g, label, left, y, cap, k.hasProperty("label_colour") ? colour(k["label_colour"].toString()) : legend, false, false,
		           juce::Justification::left);
		if (!num_.isEmpty())
			drawLegend(g, num_, left + wl + gap, y, cap, digit, false, false, juce::Justification::left);
	}
	const float pvY = num(v["page_value_label_y"], 25.3f);
	drawLegend(g, "PAGE", m_keys[0].box.getCentreX(), pvY, cap, legend, true, true, juce::Justification::horizontallyCentred);
	drawLegend(g, "VALUE", m_keys[2].box.getCentreX(), pvY, cap, legend, true, true, juce::Justification::horizontallyCentred);

	// POWER
	{
		const auto pw = v["power"];
		const auto r = toView(m_power);
		g.setColour(colour("key_power"));
		g.fillRoundedRectangle(r, 0.8f * kPx);
		g.setColour(juce::Colours::white.withAlpha(0.25f));
		g.drawLine(r.getX() + 3, r.getY() + 1.5f, r.getRight() - 3, r.getY() + 1.5f, 1.0f);
		g.setColour(juce::Colours::black.withAlpha(0.35f));
		g.drawLine(r.getX() + 3, r.getBottom() - 1, r.getRight() - 3, r.getBottom() - 1, 1.5f);
		drawLegend(g, pw["label"].toString(), m_power.getCentreX(), num(pw["label_y"]), num(pw["label_cap"], 1.9f), legend, true, false,
		           juce::Justification::horizontallyCentred);
	}
}

void HardwareView::paintRear(juce::Graphics &g)
{
	const auto &rear = panel()["rear"];
	const auto face = colour("face"), legend = colour("legend");
	g.fillAll(face.darker(0.45f));
	// the chassis' back, between the front panel's ears
	const auto box = boxOf(rear["face"]);
	g.setColour(juce::Colour(0xff151517));
	g.fillRect(toView(box));
	for (const auto &c : *rear["screws"].getArray()) {
		const auto r = toView(juce::Rectangle<float>(num(rear["screw_d"]), num(rear["screw_d"])).withCentre(pointOf(c)));
		g.setColour(juce::Colour(0xff2c2c30));
		g.fillEllipse(r);
		g.setColour(juce::Colour(0xff0e0e10));
		const auto m = r.getCentre();
		const float a = r.getWidth() * 0.32f;
		g.drawLine(m.x - a, m.y, m.x + a, m.y, 1.4f);
		g.drawLine(m.x, m.y - a, m.x, m.y + a, 1.4f);
	}
	const float jy = rearJackY(), cap = num(rear["label_cap"], 2.0f);
	const float ly0 = num(rear["label_y"][0]), ly1 = num(rear["label_y"][1]);
	for (int i = 0; i < int(m_jacks.size()); ++i) {
		const auto &j = m_jacks[size_t(i)];
		const bool foot = j.footswitch >= 0, hover = i == m_hoverJack && foot;
		const auto r = toView(juce::Rectangle<float>(j.d, j.d).withCentre({j.x, jy}));
		if (j.din) {
			// a DIN socket: the black shell, the pins
			g.setColour(juce::Colour(0xff2a2a2d));
			g.fillEllipse(r);
			g.setColour(juce::Colour(0xff0a0a0b));
			g.fillEllipse(r.reduced(r.getWidth() * 0.12f));
			g.setColour(juce::Colour(0xff8a8a8e));
			for (int pin = 0; pin < 5; ++pin) {
				const float a = juce::MathConstants<float>::pi * (1.0f + 0.25f * float(pin));
				const auto c = r.getCentre() + juce::Point<float>(std::cos(a), std::sin(a)) * (r.getWidth() * 0.25f);
				g.fillEllipse(juce::Rectangle<float>(2.4f, 2.4f).withCentre(c));
			}
		} else {
			// a 1/4" jack: its black nut, the socket
			g.setColour(juce::Colour(0xff2a2a2d));
			g.fillEllipse(r);
			g.setColour(juce::Colour(0xff4a4a4e));
			g.drawEllipse(r.reduced(r.getWidth() * 0.18f), 1.2f);
			g.setColour(juce::Colour(0xff050506));
			g.fillEllipse(r.withSizeKeepingCentre(r.getWidth() * 0.42f, r.getHeight() * 0.42f));
		}
		if (foot && (hover || m_footswitch == j.footswitch)) {
			g.setColour(lemus::panel::col::accent.withAlpha(m_footswitch == j.footswitch ? 0.9f : 0.5f));
			g.drawEllipse(r.expanded(3), 2.0f);
		}
		for (int k = 0; k < j.label.size(); ++k)
			drawLegend(g, j.label[k], j.x, k == 0 ? ly0 : ly1, cap, legend, true, false, juce::Justification::horizontallyCentred);
	}
	// the brackets under the pairs: a line, the name in its middle
	for (const auto &b : *rear["brackets"].getArray()) {
		const float x0 = num(b["from"]) - 3, x1 = num(b["to"]) + 3, y = ly1;
		const auto text = b["text"].toString();
		const float tw = textWidthMm(text, cap, true, false) + 1.6f, mid = (x0 + x1) / 2;
		g.setColour(legend);
		const float w = 0.25f * kPx;
		g.drawLine(x0 * kPx, (y - 1.4f) * kPx, x0 * kPx, y * kPx, w);
		g.drawLine(x1 * kPx, (y - 1.4f) * kPx, x1 * kPx, y * kPx, w);
		g.drawLine(x0 * kPx, y * kPx, (mid - tw / 2) * kPx, y * kPx, w);
		g.drawLine((mid + tw / 2) * kPx, y * kPx, x1 * kPx, y * kPx, w);
		drawLegend(g, text, mid, y, cap, legend, true, false, juce::Justification::horizontallyCentred);
	}
	// the maker's mark (the user's artwork, as on the front), the FCC notice
	if (const auto it = m_logos.find(rear["logo"]["mark"].toString()); it != m_logos.end()) {
		const auto lb = toView(boxOf(rear["logo"]["box"]));
		if (it->second.picture != nullptr)
			it->second.picture->drawWithin(g, lb);
	}
	for (const auto &t : *rear["texts"].getArray()) {
		const auto b = boxOf(t["box"]);
		drawLegend(g, t["text"].toString(), b.getX(), b.getCentreY(), b.getHeight(), legend, true, false, juce::Justification::left, b.getWidth());
	}
}

void HardwareView::drawKnob(juce::Graphics &g, const Knob &k, bool hover)
{
	const auto &v = panel();
	const float r = k.d / 2 * kPx;
	const auto c = k.centre * kPx;
	// a black cap with a fluted skirt, lit from above
	g.setColour(juce::Colour(0xff0c0c0e));
	g.fillEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(c));
	g.setColour(juce::Colours::white.withAlpha(hover ? 0.16f : 0.09f));
	for (int i = 0; i < 36; ++i) {
		const float a = juce::MathConstants<float>::twoPi * float(i) / 36.0f;
		g.drawLine(c.x + (r - 3) * std::sin(a), c.y - (r - 3) * std::cos(a), c.x + r * std::sin(a), c.y - r * std::cos(a), 0.8f);
	}
	g.setColour(juce::Colour(0xff17171a));
	g.fillEllipse(juce::Rectangle<float>(2 * (r - 3.5f), 2 * (r - 3.5f)).withCentre(c));
	g.setColour(juce::Colours::white.withAlpha(0.18f));
	g.drawEllipse(juce::Rectangle<float>(2 * r - 1, 2 * r - 1).withCentre(c), 1.0f);
	// the white pointer insert, from the centre outward, over 300 degrees
	const float value = k.param != nullptr ? k.param->getValue() : 0.0f;
	const float a = juce::degreesToRadians(-150.0f + 300.0f * value);
	const auto p = v["knob_pointer"];
	const float w = num(p["w"], 2.3f) * kPx, from = num(p["from_r"]) * kPx, to = num(p["to_r"], 3.2f) * kPx;
	juce::Path bar;
	bar.addRectangle(-w / 2, -to, w, to - from);
	g.setColour(juce::Colour(0xfff2f2ee));
	g.fillPath(bar, juce::AffineTransform::rotation(a).translated(c));
	drawLegend(g, k.name, k.centre.x, k.labelY, num(v["knob_label_cap"], 3.0f), colour("legend"), false, true,
	           juce::Justification::horizontallyCentred);
}

void HardwareView::drawKey(juce::Graphics &g, const KeyRef &k, bool down, bool hover)
{
	auto r = toView(k.box);
	if (down)
		r = r.translated(0, 1.0f);
	const auto key = colour("key");
	g.setColour(juce::Colours::black.withAlpha(0.5f));
	g.fillRoundedRectangle(r.translated(0, 2.0f), 0.9f * kPx);
	g.setColour(down ? key.darker(0.25f) : hover ? key.brighter(0.12f) : key);
	g.fillRoundedRectangle(r, 0.9f * kPx);
	g.setColour(juce::Colours::white.withAlpha(0.28f));
	g.drawLine(r.getX() + 3, r.getY() + 1.5f, r.getRight() - 3, r.getY() + 1.5f, 1.0f);
	if (k.ledLatch >= 0) {
		const uint8_t latch = k.ledLatch == 2 ? m_display.latch0400 : k.ledLatch == 3 ? m_display.latch0600 : m_display.latch0800;
		const bool lit = m_proc.booted() && m_proc.powered() && !((latch >> k.ledBit) & 1);
		const float d = num(panel()["led_in_key_d"], 1.8f) * kPx;
		const auto led = juce::Rectangle<float>(d, d).withCentre(r.getCentre());
		g.setColour(lit ? colour("led_red") : juce::Colour(0xff2a1210));
		g.fillEllipse(led);
		if (lit) {
			g.setColour(colour("led_red").withAlpha(0.3f));
			g.fillEllipse(led.expanded(d * 0.6f));
		}
	}
}

void HardwareView::drawLcd(juce::Graphics &g)
{
	const auto &v = panel();
	const auto window = toView(boxOf(v["lcd"]["window"]));
	const auto text = boxOf(v["lcd"]["text"]);
	g.setColour(juce::Colour(0xff050506));
	g.fillRoundedRectangle(window, 2.0f);
	const bool on = m_proc.powered() && m_proc.booted() && m_display.on;
	const auto glass = toView(text.expanded(2.6f, 1.6f));
	const auto lit = colour("lcd_lit"), ink = colour("lcd_ink");
	g.setColour(on ? lit : colour("lcd_off"));
	g.fillRoundedRectangle(glass, 1.5f);
	if (!on)
		return;
	// 16 x 2 cells of 5 x 8 dots: codes 0-15 the firmware's own characters (CGRAM), the rest the
	// HD44780's character ROM
	const float cw = text.getWidth() / 16.0f, ch = 5.6f;
	const float rowY[2] = {text.getY(), text.getBottom() - ch};
	const auto unlit = lit.interpolatedWith(ink, 0.07f);
	const auto &font = m_proc.lcdFont();
	juce::RectangleList<float> dots, blanks;
	for (int line = 0; line < 2; ++line)
		for (int i = 0; i < 16; ++i) {
			const auto cell = juce::Rectangle<float>(text.getX() + cw * float(i) + 0.2f, rowY[line], cw - 0.4f, ch);
			const uint8_t c = m_display.text[line][i];
			const uint8_t *rows = c < 0x10 ? m_display.cgram + (c & 7) * 8 : font.size() == 4096 ? font.data() + c * 16 : nullptr;
			const float dx = cell.getWidth() / 5.0f, dy = cell.getHeight() / 8.0f;
			for (int y = 0; y < 8; ++y)
				for (int x = 0; x < 5; ++x) {
					const bool dot = rows != nullptr && ((rows[y] >> (4 - x)) & 1);
					(dot ? dots : blanks).addWithoutMerging(toView({cell.getX() + dx * float(x) + 0.05f, cell.getY() + dy * float(y) + 0.05f, dx - 0.1f, dy - 0.1f}));
				}
		}
	g.setColour(unlit);
	g.fillRectList(blanks);
	g.setColour(ink);
	g.fillRectList(dots);
}

juce::String HardwareView::status() const
{
	if (!m_proc.romLoaded())
		return "NO ROMS: add qvgtv103.bin and hd44780_a00.bin to the ROM folder";
	if (!m_proc.powered())
		return "OFF: the POWER switch";
	if (!m_proc.booted())
		return "STARTING: the firmware is booting";
	if (m_footswitch >= 0)
		return m_footswitch == 0 ? "BYPASS footswitch down" : "ADVANCE footswitch down";
	if (m_held.anyLatched())
		return lemus::panel::heldStatus(m_held.latchedNames([this](int i) { return m_keys[size_t(i)].name; }));
	return {};
}

void HardwareView::drawBar(juce::Graphics &g)
{
	lemus::panel::drawBarBackground(g, m_bar);
	lemus::panel::drawPill(g, m_bar[m_barRear], "REAR", m_rear, m_hoverBar == m_barRear, barMono());
	lemus::panel::drawStatus(g, m_bar.status(), status(), barMono(), m_held.anyLatched() ? lemus::panel::col::accent : lemus::panel::col::status);
	lemus::panel::drawPill(g, m_bar.help(), "?", m_help, m_hoverBar == lemus::panel::Bar::kHelp, barMono());
	if (m_hoverBar != -1 && !m_help)
		lemus::panel::drawBarTip(g, m_bar, m_hoverBar, getLocalBounds().toFloat(), barMono());
}

// The control under the mouse (or the knob being turned): its name and setting, or its computer
// key, under it.
void HardwareView::drawTip(juce::Graphics &g)
{
	juce::String text;
	juce::Point<float> at;
	if (m_rear) {
		if (m_hoverJack < 0)
			return;
		const auto &j = m_jacks[size_t(m_hoverJack)];
		text = juce::String(j.footswitch == 0 ? "BYPASS" : "ADVANCE") + " footswitch: click to press it";
		at = {j.x * kPx, (rearJackY() + j.d / 2 + 2.0f) * kPx};
	} else if (const int k = m_dragKnob >= 0 ? m_dragKnob : m_hoverKnob; k >= 0) {
		const auto &knob = m_knobs[size_t(k)];
		text = knob.name + "  " + (knob.param != nullptr ? knob.param->getCurrentValueAsText() : juce::String());
		at = {knob.centre.x * kPx, (knob.centre.y + knob.d / 2 + 1.0f) * kPx};
	} else if (m_hoverKey >= 0) {
		const auto &key = m_keys[size_t(m_hoverKey)];
		text = key.name;
		for (const auto &c : computerKeys())
			if (c.key == key.key)
				text << "   (" << c.shown << ")";
		at = {key.box.getCentreX() * kPx, (key.box.getBottom() + 0.8f) * kPx};
	} else if (m_hoverPower) {
		text = m_proc.powered() ? "POWER: on. Click to switch off (the battery keeps the programs and the edit)"
		                        : "POWER: off. Click to switch on (the firmware starts again)";
		at = {m_power.getCentreX() * kPx, (m_power.getBottom() + 1.0f) * kPx};
	} else {
		return;
	}
	lemus::panel::drawTooltip(g, text, at, getLocalBounds().toFloat(), barMono());
}

int HardwareView::knobAt(juce::Point<float> mm) const
{
	for (size_t i = 0; i < m_knobs.size(); ++i)
		if (mm.getDistanceFrom(m_knobs[i].centre) < m_knobs[i].d / 2 + 0.5f)
			return int(i);
	return -1;
}

int HardwareView::keyAt(juce::Point<float> mm) const
{
	for (size_t i = 0; i < m_keys.size(); ++i)
		if (m_keys[i].box.expanded(0.8f).contains(mm))
			return int(i);
	return -1;
}

int HardwareView::jackAt(juce::Point<float> mm) const
{
	for (size_t i = 0; i < m_jacks.size(); ++i)
		if (m_jacks[i].footswitch >= 0 && mm.getDistanceFrom({m_jacks[i].x, rearJackY()}) < m_jacks[i].d / 2 + 1.0f)
			return int(i);
	return -1;
}

void HardwareView::setHover(juce::Point<float> at)
{
	const auto mm = toMm(at);
	const bool onPanel = at.y < float(kPanelHeight);
	m_hoverKnob = onPanel && !m_rear ? knobAt(mm) : -1;
	m_hoverKey = onPanel && !m_rear && m_hoverKnob < 0 ? keyAt(mm) : -1;
	m_hoverPower = onPanel && !m_rear && m_hoverKnob < 0 && m_hoverKey < 0 && m_power.expanded(1.0f).contains(mm);
	m_hoverJack = onPanel && m_rear ? jackAt(mm) : -1;
	m_hoverBar = onPanel ? -1 : m_bar.hit(at);
	repaint();
}

void HardwareView::mouseMove(const juce::MouseEvent &e) { setHover(e.position); }

void HardwareView::mouseExit(const juce::MouseEvent &)
{
	m_hoverKnob = m_hoverKey = m_hoverBar = m_hoverJack = -1;
	m_hoverPower = false;
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
		else if (hit == m_barRear)
			m_rear = !m_rear;
		repaint();
		return;
	}
	const auto mm = toMm(e.position);
	if (m_rear) {
		if (const int j = jackAt(mm); j >= 0) {
			m_footswitch = m_jacks[size_t(j)].footswitch;
			m_proc.setFootswitch(m_footswitch, true);
		}
		repaint();
		return;
	}
	if (m_power.expanded(1.0f).contains(mm)) {
		m_proc.setPowered(!m_proc.powered());
		repaint();
		return;
	}
	if (const int k = knobAt(mm); k >= 0 && m_knobs[size_t(k)].param != nullptr) {
		m_dragKnob = k;
		m_dragStart = e.position.y;
		m_dragValue = m_knobs[size_t(k)].param->getValue();
		m_knobs[size_t(k)].param->beginChangeGesture();
		return;
	}
	m_hardPress = e.mods.isShiftDown();
	if (const int k = keyAt(mm); k >= 0 && m_held.mouseDown(k, e.mods))
		m_mouseKey = k;
	repaint();
}

void HardwareView::mouseDrag(const juce::MouseEvent &e)
{
	if (m_dragKnob < 0)
		return;
	m_dragValue = juce::jlimit(0.0f, 1.0f, m_dragValue + (m_dragStart - e.position.y) / (e.mods.isShiftDown() ? 1000.0f : 200.0f));
	m_dragStart = e.position.y;
	m_knobs[size_t(m_dragKnob)].param->setValueNotifyingHost(m_dragValue);
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
	if (m_footswitch >= 0) {
		m_proc.setFootswitch(m_footswitch, false);
		m_footswitch = -1;
	}
	repaint();
}

void HardwareView::mouseDoubleClick(const juce::MouseEvent &e)
{
	if (m_rear)
		return;
	if (const int k = knobAt(toMm(e.position)); k >= 0 && m_knobs[size_t(k)].param != nullptr) {
		auto *p = m_knobs[size_t(k)].param;
		p->beginChangeGesture();
		p->setValueNotifyingHost(p->getDefaultValue());
		p->endChangeGesture();
	}
}

void HardwareView::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
	if (m_rear)
		return;
	if (const int k = knobAt(toMm(e.position)); k >= 0 && m_knobs[size_t(k)].param != nullptr) {
		auto *p = m_knobs[size_t(k)].param;
		p->beginChangeGesture();
		p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->getValue() + (w.deltaY + w.deltaX) * (e.mods.isShiftDown() ? 0.05f : 0.25f)));
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
	int code = k.getKeyCode();
	if (code >= 'a' && code <= 'z')
		code -= 32;
	m_hardPress = k.getModifiers().isShiftDown();
	const auto &keys = computerKeys();
	const auto it = std::find_if(keys.begin(), keys.end(), [code](const ComputerKey &c) { return c.code == code; });
	if (it == keys.end() || k.getModifiers().isCommandDown())
		return false;
	for (size_t i = 0; i < m_keys.size(); ++i)
		if (m_keys[i].key == it->key) {
			m_held.keyDown(k.getKeyCode(), int(i));
			return true;
		}
	return false;
}

bool HardwareView::keyStateChanged(bool) { return m_held.keysChanged(); }

void HardwareView::focusLost(FocusChangeType) { m_held.focusLost(); }

} // namespace qvgt
