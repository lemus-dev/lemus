#include "HardwareView.h"

#include "Fonts.h"
#include "Ms20Panel.h"
#include "ms20/core/board.h"

#include <cmath>

namespace ms20 {

namespace {

const char kKeyRow[] = "awsedftgyhujkolp;'";
const lemus::panel::Mono kMono = [](float size, int weight) { return fonts::mono(size, weight); };

const juce::var &panel()
{
	static const juce::var v = juce::JSON::parse(juce::String::fromUTF8(Ms20Panel::panel_json, Ms20Panel::panel_jsonSize));
	return v;
}

juce::Colour colour(const juce::String &name)
{
	const auto c = panel()["colours"][juce::Identifier(name)]["hex"].toString();
	return c.isEmpty() ? juce::Colours::magenta : juce::Colour::fromString("ff" + c.substring(1));
}

float num(const juce::var &v) { return float(double(v)); }

juce::Rectangle<float> bboxOf(const juce::var &b)
{
	if (!b.isArray() || b.size() < 4)
		return {};
	return juce::Rectangle<float>::leftTopRightBottom(num(b[0]), num(b[1]), num(b[2]), num(b[3]));
}

juce::Path polyOf(const juce::var &points, bool close)
{
	juce::Path p;
	if (!points.isArray())
		return p;
	for (int i = 0; i < points.size(); ++i) {
		const juce::Point<float> q{num(points[i][0]), num(points[i][1])};
		if (i == 0)
			p.startNewSubPath(q);
		else
			p.lineTo(q);
	}
	if (close)
		p.closeSubPath();
	return p;
}

// A point at `deg` (clockwise from 12 o'clock) and radius `r` round `c`.
juce::Point<float> polar(juce::Point<float> c, float deg, float r)
{
	const float a = juce::degreesToRadians(deg);
	return {c.x + r * std::sin(a), c.y - r * std::cos(a)};
}

// A legend's outline in panel mm, fitted to its measured ink box (the print is a condensed
// grotesque: Nimbus Sans Narrow stretched to each legend's measured width and cap height).
juce::Path legendPath(const juce::String &text, juce::Rectangle<float> box)
{
	juce::GlyphArrangement ga;
	ga.addLineOfText(fonts::panel(100.0f), text, 0, 0);
	juce::Path p;
	ga.createPath(p);
	const auto b = p.getBounds();
	if (b.isEmpty() || box.isEmpty())
		return {};
	p.applyTransform(juce::AffineTransform::fromTargetPoints(b.getTopLeft(), box.getTopLeft(), b.getTopRight(), box.getTopRight(),
	                                                          b.getBottomLeft(), box.getBottomLeft()));
	return p;
}

// The cables' colours, by the input they go into.
const juce::Colour kCable[] = {juce::Colour(0xffd23c3c), juce::Colour(0xffe6b437), juce::Colour(0xff3c82d2), juce::Colour(0xff46a050),
                               juce::Colour(0xffe8e8e8), juce::Colour(0xff9b59b6), juce::Colour(0xffe67e22)};

// The view's legends, built once: (path in mm, colour).
struct Print {
	std::vector<std::pair<juce::Path, juce::Colour>> fills;
	std::vector<std::tuple<juce::Path, juce::Colour, float>> strokes;
};

const Print &print()
{
	static const Print pr = [] {
		Print out;
		for (const auto &e : *panel()["elements"].getArray()) {
			const auto type = e["type"].toString();
			const auto col = colour(e["colour"].toString());
			if (type == "text") {
				out.fills.push_back({legendPath(e["text"].toString(), bboxOf(e["bbox"])), col});
			} else if (type == "line") {
				out.strokes.push_back({polyOf(e["points"], false), col, num(e["stroke"])});
			} else if (type == "outline_box") {
				juce::Path p;
				p.addRectangle(bboxOf(e["bbox"]));
				out.strokes.push_back({p, col, num(e["stroke"])});
			} else if (type == "polygon") {
				out.strokes.push_back({polyOf(e["points"], true), col, num(e["stroke"])});
			} else if (type == "arrowhead") {
				out.fills.push_back({polyOf(e["points"], true), col});
			} else if (type == "symbol") {
				juce::Path p;
				for (const auto &line : *e["polylines"].getArray())
					p.addPath(polyOf(line, false));
				out.strokes.push_back({p, col, num(e["stroke"])});
			} else if (type == "knob" || type == "rotary") {
				const auto &s = e["scale"];
				const juce::Point<float> c{num(e["x"]), num(e["y"])};
				const auto tc = colour(s["colour"].toString());
				juce::Path ticks;
				if (s["ticks_deg"].isArray())
					for (const auto &d : *s["ticks_deg"].getArray()) {
						ticks.startNewSubPath(polar(c, num(d), num(s["tick_r"][0])));
						ticks.lineTo(polar(c, num(d), num(s["tick_r"][1])));
					}
				out.strokes.push_back({ticks, tc, num(s["tick_w"])});
				if (s["numerals"].isArray())
					for (const auto &n : *s["numerals"].getArray())
						out.fills.push_back({legendPath(n["text"].toString(), bboxOf(n["bbox"])), tc});
			}
		}
		return out;
	}();
	return pr;
}

} // namespace

namespace {

// A key's name for the status line while a right-click holds it: by pitch, key 0 the bottom C (C3,
// as MIDI plays it), or the momentary switch.
juce::String heldName(int i)
{
	static const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	if (i >= Machine::kKeys)
		return "MOMENTARY SWITCH";
	return juce::String(names[i % 12]) + juce::String(i / 12 + 3);
}

// A jack's name as the panel prints it (panel.json marks the cheek's two outputs "(out)").
juce::String jackName(const juce::String &label) { return label.endsWith(" (out)") ? label.dropLastCharacters(6) : label; }

const juce::String kServiceTip = "SERVICE: the unit's circuit. CIRCUIT: which filter board it has, REV 1 (two KORG35 "
                                 "modules) or REV 2 (the later KLM-307 board)";
const char *const kRevText[2][2] = {
	{"REV 1", "Two KORG35 filter modules on KLM-127, as the service manual draws."},
	{"REV 2", "The later KLM-307 board: LM13600 filters, its own VCA."}};

// The signal inputs the host's audio can go into, by a click on the jack (the cable from outside).
bool takesHost(int jack) { return jack == int(Jack::EspSignalIn) || jack == int(Jack::ExtSigIn); }

} // namespace

HardwareView::HardwareView(Ms20Processor &p) : m_proc(p)
{
	setWantsKeyboardFocus(true);
	setOpaque(true);
	const auto &jackOf = Board::jackOf();
	for (const auto &e : *panel()["elements"].getArray()) {
		const auto type = e["type"].toString();
		if (type == "knob" || type == "rotary") {
			Control c;
			c.id = e["id"].toString();
			c.label = e["label"].toString();
			c.rotary = type == "rotary";
			c.c = {num(e["x"]), num(e["y"])};
			c.d = num(e["d"]);
			c.topD = num(e["top_d"]);
			c.minDeg = num(e["min_deg"]);
			c.maxDeg = num(e["max_deg"]);
			if (e["detents_deg"].isArray())
				for (const auto &d : *e["detents_deg"].getArray())
					c.detents.push_back(num(d));
			c.param = p.params().getParameter(e["param"].toString());
			c.element = e;
			if (c.param != nullptr && c.param->getParameterID() == "volume")
				m_volume = int(m_controls.size());
			m_controls.push_back(std::move(c));
		} else if (type == "jack") {
			Socket j;
			j.id = e["id"].toString();
			j.label = e["label"].toString();
			j.input = e["io"].toString() == "in";
			j.c = {num(e["x"]), num(e["y"])};
			j.d = num(e["d"]);
			j.nut = num(e["nut_af"]);
			j.nutRot = num(e["nut_rot_deg"]);
			const auto it = jackOf.find(j.id.toStdString());
			j.jack = it == jackOf.end() ? -1 : int(it->second);
			m_jacks.push_back(std::move(j));
		} else if (type == "key") {
			const auto ff = e["front_face"];
			m_keys.push_back({int(e["key"]), bool(e["white"]), bboxOf(e["bbox"]), ff.isArray() ? bboxOf(ff) : juce::Rectangle<float>()});
		} else if (type == "wheel") {
			// Drawn from above, as the other machines' wheels are: a straight slot along the keys,
			// the face's width (panel.json's opening is the photo's slanted view of it), over the
			// momentary switch's centre (set once the switch is read, below).
			const auto seen = polyOf(e["opening"]["polygon"], true).getBounds();
			const float w = float(double(e["face"]["w"])) + 1.6f;
			m_wheel = {seen.getCentreX() - w / 2, seen.getY(), w, seen.getHeight()};
		} else if (type == "switch") {
			m_switch = bboxOf(e["bbox"]);
		}
	}
	if (!m_wheel.isEmpty()) {
		if (!m_switch.isEmpty())
			m_wheel.setCentre(m_switch.getCentreX(), m_wheel.getCentreY());
		m_wheelSlot.clear();
		m_wheelSlot.addRoundedRectangle(m_wheel, 1.2f);
	}
	std::stable_sort(m_keys.begin(), m_keys.end(), [](const Key &a, const Key &b) { return a.white && !b.white; });
	// The KORG logotype comes from the user's artwork (a file for its place, or an older ms20.svg
	// pack); without it the panel has none.
	for (const auto &mark : p.artwork().marks())
		m_logos[mark.id] = mark;
	m_held.reset(size_t(Machine::kKeys) + 1, [this](int key, bool down) {
		if (key == kMomentary)
			m_proc.setMomentary(down);
		else
			m_proc.setKey(key, down);
		repaint();
	});
	m_barService = m_bar.add(lemus::panel::Bar::kService, kServiceTip);
	m_wheelValue = m_proc.manual().wheel;
	m_display = m_proc.display();
	startTimerHz(30);
}

HardwareView::~HardwareView()
{
	stopTimer();
	m_held.releaseAll(true);
}

int HardwareView::unmappedControls() const
{
	int n = 0;
	for (const auto &c : m_controls)
		n += c.param == nullptr;
	return n;
}

juce::Point<float> HardwareView::placeForTests(const juce::String &part) const
{
	for (const auto &c : m_controls)
		if (c.param != nullptr && c.param->getParameterID() == part)
			return px(c.c);
	for (const auto &j : m_jacks)
		if ("jack:" + j.id == part)
			return px(j.c);
	for (const auto &k : m_keys)
		if ("key:" + juce::String(k.key) == part)
			return px(k.box.getCentre());
	if (part == "wheel")
		return px(m_wheel.getCentre());
	if (part == "switch")
		return px(m_switch.getCentre());
	if (part == "service")
		return m_bar[m_barService].getCentre();
	if (part == "circuit:1" || part == "circuit:2")
		return serviceOption(part.getLastCharacter() == '2' ? 1 : 0).getCentre();
	return {-1, -1};
}

int HardwareView::jackIndex(int patchJack) const
{
	for (size_t i = 0; i < m_jacks.size(); ++i)
		if (m_jacks[i].jack == patchJack)
			return int(i);
	return -1;
}

double HardwareView::value(const Control &c) const { return c.param != nullptr ? double(c.param->getValue()) : 0.0; }

int HardwareView::detentOf(const Control &c) const
{
	if (c.detents.empty())
		return 0;
	return juce::jlimit(0, int(c.detents.size()) - 1, juce::roundToInt(value(c) * double(c.detents.size() - 1)));
}

int HardwareView::detentAt(const Control &c, juce::Point<float> mm) const
{
	const float deg = juce::radiansToDegrees(std::atan2(mm.x - c.c.x, -(mm.y - c.c.y)));
	int best = 0;
	float bestD = 1e9f;
	for (size_t d = 0; d < c.detents.size(); ++d)
		if (const float dist = std::abs(c.detents[d] - deg); dist < bestD) {
			bestD = dist;
			best = int(d);
		}
	return best;
}

float HardwareView::angle(const Control &c) const
{
	if (c.rotary && !c.detents.empty())
		return c.detents[size_t(detentOf(c))];
	return c.minDeg + float(value(c)) * (c.maxDeg - c.minDeg);
}

void HardwareView::setValue(const Control &c, double v)
{
	if (c.param == nullptr)
		return;
	v = juce::jlimit(0.0, 1.0, v);
	if (c.rotary && !c.detents.empty()) {
		const double n = double(c.detents.size() - 1);
		v = std::round(v * n) / n;
	}
	c.param->setValueNotifyingHost(float(v));
	repaint();
}

int HardwareView::controlAt(juce::Point<float> mm) const
{
	for (size_t i = 0; i < m_controls.size(); ++i)
		if (mm.getDistanceFrom(m_controls[i].c) <= m_controls[i].d / 2 + 1)
			return int(i);
	return -1;
}

int HardwareView::jackAt(juce::Point<float> mm) const
{
	for (size_t i = 0; i < m_jacks.size(); ++i)
		if (mm.getDistanceFrom(m_jacks[i].c) <= m_jacks[i].nut / 2 + 0.5f)
			return int(i);
	return -1;
}

int HardwareView::keyAt(juce::Point<float> mm) const
{
	for (auto it = m_keys.rbegin(); it != m_keys.rend(); ++it) // black keys are last: hit first
		if (it->box.contains(mm) || it->front.contains(mm))
			return it->key;
	return -1;
}

bool HardwareView::overWheel(juce::Point<float> mm) const { return m_wheelSlot.contains(mm); }
bool HardwareView::overSwitch(juce::Point<float> mm) const { return m_switch.expanded(1).contains(mm); }

void HardwareView::connect(int a, int b)
{
	if (a < 0 || b < 0 || a == b)
		return;
	const auto &ja = m_jacks[size_t(a)], &jb = m_jacks[size_t(b)];
	if (ja.input == jb.input || ja.jack < 0 || jb.jack < 0)
		return; // an output into an output, or into PHONES: nothing happens
	const auto &in = ja.input ? ja : jb;
	const auto &out = ja.input ? jb : ja;
	auto p = m_proc.patch();
	p.connect(Jack(in.jack), out.jack);
	m_proc.setPatch(p);
}

void HardwareView::cancelCable()
{
	if (m_cableFrom < 0)
		return;
	if (m_pulledIn >= 0) {
		auto p = m_proc.patch();
		p.connect(Jack(m_pulledIn), m_pulledFrom);
		m_proc.setPatch(p);
	}
	m_cableFrom = -1;
	m_pulledIn = m_pulledFrom = -1;
}

int HardwareView::signalIn() const
{
	const auto p = m_proc.patch();
	if (p.from[size_t(Jack::EspSignalIn)] == kHostInput)
		return 1;
	if (p.from[size_t(Jack::ExtSigIn)] == kHostInput)
		return 2;
	for (int in = 0; in < kFirstOutput; ++in)
		if (p.from[size_t(in)] == kHostInput)
			return 3;
	return 0;
}

void HardwareView::setSignalIn(int where)
{
	auto p = m_proc.patch();
	for (int in = 0; in < kFirstOutput; ++in)
		if (p.from[size_t(in)] == kHostInput)
			p.disconnect(Jack(in));
	if (where == 1)
		p.connect(Jack::EspSignalIn, kHostInput);
	else if (where == 2)
		p.connect(Jack::ExtSigIn, kHostInput);
	m_proc.setPatch(p);
}

void HardwareView::setWheel(double v)
{
	m_wheelValue = juce::jlimit(-1.0, 1.0, v);
	m_proc.setWheel(m_wheelValue);
	repaint();
}

// ======================================================================================== painting

void HardwareView::paint(juce::Graphics &g)
{
	const float phys = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
	if (!m_static.isValid() || std::abs(phys - m_staticScale) > 0.01f) {
		m_staticScale = phys;
		m_static = juce::Image(juce::Image::RGB, juce::roundToInt(kWidth * phys), juce::roundToInt(kPanelHeight * phys), true);
		juce::Graphics sg(m_static);
		sg.addTransform(juce::AffineTransform::scale(phys));
		renderStatic(sg);
	}
	g.drawImageTransformed(m_static, juce::AffineTransform::scale(1.0f / phys));

	drawLeds(g);
	if (m_hoverJack >= 0) {
		const auto &j = m_jacks[size_t(m_hoverJack)];
		g.setColour(lemus::panel::col::accent);
		g.drawEllipse(juce::Rectangle<float>(j.nut * kPxPerMm + 2, j.nut * kPxPerMm + 2).withCentre(px(j.c)), 1.5f);
	}
	for (size_t i = 0; i < m_controls.size(); ++i)
		drawControl(g, m_controls[i], int(i) == m_hoverControl || int(i) == m_dragControl);
	if (!m_proc.powered()) {
		g.setColour(juce::Colours::black.withAlpha(0.18f));
		g.fillRect(getLocalBounds().withHeight(kPanelHeight));
	}
	drawKeys(g);
	drawCheek(g);
	drawCables(g);
	drawBar(g);
	if (m_service)
		drawService(g);
	if (m_help)
		drawHelp(g);
	else
		drawTooltip(g);
}

void HardwareView::renderStatic(juce::Graphics &g)
{
	g.fillAll(colour("case_black"));
	const auto scale = mmToPx();
	// the case, the panel's plate and the regions round it
	for (const auto &e : *panel()["elements"].getArray()) {
		const auto type = e["type"].toString();
		if (type == "case" || type == "region") {
			g.setColour(colour(e["colour"].toString()));
			g.fillPath(polyOf(e["polygon"], true), scale);
		} else if (type == "fill") {
			g.setColour(colour(e["colour"].toString()));
			g.fillRect(px(bboxOf(e["bbox"])));
		}
	}
	// the print
	const auto &pr = print();
	for (const auto &[path, col, w] : pr.strokes) {
		g.setColour(col);
		g.strokePath(path, juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), scale);
	}
	for (const auto &[path, col] : pr.fills) {
		g.setColour(col);
		g.fillPath(path, scale);
	}
	// the hardware: holes, screws, the logo's place (the LEDs are drawn live)
	for (const auto &e : *panel()["elements"].getArray()) {
		const auto type = e["type"].toString();
		const auto box = px(bboxOf(e["bbox"]));
		if (type == "hole") {
			g.setColour(colour(e["colour"].toString()));
			g.fillEllipse(box);
		} else if (type == "screw") {
			g.setColour(colour("screw_chrome"));
			g.fillEllipse(box);
			g.setColour(colour("screw_chrome").darker(0.6f));
			const auto c = box.getCentre();
			const float r = box.getWidth() * 0.32f;
			g.drawLine(c.x - r, c.y, c.x + r, c.y, 1.2f);
			g.drawLine(c.x, c.y - r, c.x, c.y + r, 1.2f);
		} else if (type == "logo") {
			// The place's own file as it looks, fitted to the mark's box; a pack's outline where it
			// is, in the print colour.
			if (const auto it = m_logos.find("korg"); it != m_logos.end()) {
				if (it->second.picture != nullptr) {
					it->second.picture->drawWithin(g, box);
				} else {
					g.setColour(colour(e["colour"].toString()));
					g.fillPath(it->second.outline, scale);
				}
			}
		}
	}
	for (const auto &j : m_jacks)
		drawJackBody(g, j);
	for (const auto &c : m_controls)
		drawKnobBody(g, c);
}

void HardwareView::drawKnobBody(juce::Graphics &g, const Control &c)
{
	const auto centre = px(c.c);
	const float r = c.d / 2 * kPxPerMm, rt = c.topD / 2 * kPxPerMm;
	// shadow, skirt, top
	g.setColour(juce::Colours::black.withAlpha(0.45f));
	g.fillEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(centre.translated(1.5f, 2.5f)));
	const auto base = colour("knob_black");
	g.setGradientFill(juce::ColourGradient(base.brighter(0.35f), centre.translated(-r, -r), base.darker(0.4f), centre.translated(r, r), false));
	g.fillEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(centre));
	g.setGradientFill(juce::ColourGradient(base.brighter(0.55f), centre.translated(-rt, -rt), base, centre.translated(rt, rt), false));
	g.fillEllipse(juce::Rectangle<float>(2 * rt, 2 * rt).withCentre(centre));
}

void HardwareView::drawControl(juce::Graphics &g, const Control &c, bool hover)
{
	const auto centre = px(c.c);
	const float r = c.d / 2 * kPxPerMm, rt = c.topD / 2 * kPxPerMm;
	if (hover) {
		g.setColour(lemus::panel::col::accent.withAlpha(0.7f));
		g.drawEllipse(juce::Rectangle<float>(2 * r + 3, 2 * r + 3).withCentre(centre), 1.2f);
	}
	// the pointer line on the cap
	const float a = angle(c);
	g.setColour(colour("pointer_white"));
	g.drawLine(juce::Line<float>(polar(centre, a, rt * 0.25f), polar(centre, a, rt * 0.95f)), 2.0f);
}

void HardwareView::drawJackBody(juce::Graphics &g, const Socket &j)
{
	const auto c = px(j.c);
	// the hex nut, then the opening
	juce::Path nut;
	const float r = j.nut / 2 / std::cos(juce::MathConstants<float>::pi / 6) * kPxPerMm;
	for (int k = 0; k < 6; ++k) {
		const auto q = polar(c, j.nutRot + 60.0f * float(k), r);
		if (k == 0)
			nut.startNewSubPath(q);
		else
			nut.lineTo(q);
	}
	nut.closeSubPath();
	const auto chrome = colour("nut_chrome");
	g.setGradientFill(juce::ColourGradient(chrome.brighter(0.3f), c.translated(-r, -r), chrome.darker(0.5f), c.translated(r, r), false));
	g.fillPath(nut);
	g.setColour(colour("hole_black"));
	g.fillEllipse(juce::Rectangle<float>(j.d * kPxPerMm, j.d * kPxPerMm).withCentre(c));
}

void HardwareView::drawLeds(juce::Graphics &g)
{
	for (const auto &e : *panel()["elements"].getArray()) {
		if (e["type"].toString() != "led")
			continue;
		const auto box = px(bboxOf(e["bbox"]));
		const auto id = e["id"].toString();
		const auto &l = m_display.leds;
		const bool on = id == "led_mg" ? l.mg : id == "led_kbd" ? l.kbd : id == "led_esp_peak" ? l.espPeak : l.espTrig;
		g.setColour(colour(on ? "led_on" : "led_off"));
		g.fillEllipse(box);
		if (on) {
			g.setColour(colour("led_on").withAlpha(0.25f));
			g.fillEllipse(box.expanded(box.getWidth() * 0.6f));
		}
	}
}

void HardwareView::drawCable(juce::Graphics &g, juce::Point<float> a, juce::Point<float> b, juce::Colour col, bool plugA, bool plugB)
{
	// a cord hanging between its plugs: a cubic whose sag grows with the span
	const float span = a.getDistanceFrom(b);
	const float sag = 18.0f + span * 0.28f;
	juce::Path p;
	p.startNewSubPath(a);
	p.cubicTo(a.translated(0, sag), b.translated(0, sag), b);
	g.setColour(juce::Colours::black.withAlpha(0.35f));
	g.strokePath(p, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
	             juce::AffineTransform::translation(2, 3));
	g.setColour(col.darker(0.25f));
	g.strokePath(p, juce::PathStrokeType(4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
	g.setColour(col.brighter(0.25f));
	g.strokePath(p, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
	             juce::AffineTransform::translation(-0.8f, -0.8f));
	const float pr = 4.4f * kPxPerMm;
	for (const auto &[q, plug] : {std::pair{a, plugA}, std::pair{b, plugB}})
		if (plug) {
			g.setColour(col.darker(0.5f));
			g.fillEllipse(juce::Rectangle<float>(pr * 2, pr * 2).withCentre(q));
			g.setColour(col);
			g.fillEllipse(juce::Rectangle<float>(pr * 1.4f, pr * 1.4f).withCentre(q));
		}
}

void HardwareView::drawCables(juce::Graphics &g)
{
	const auto p = m_proc.patch();
	for (int in = 0; in < kFirstOutput; ++in) {
		if (!p.patched(Jack(in)))
			continue;
		const int ji = jackIndex(in);
		if (ji < 0)
			continue;
		const auto col = kCable[size_t(in) % std::size(kCable)];
		const auto to = px(m_jacks[size_t(ji)].c);
		if (p.from[size_t(in)] == kHostInput) {
			// the cable from outside comes in over the top of the unit
			drawCable(g, {to.x + 40, -10.0f}, to, col, false, true);
			continue;
		}
		const int jo = jackIndex(p.from[size_t(in)]);
		if (jo >= 0)
			drawCable(g, px(m_jacks[size_t(jo)].c), to, col, true, true);
	}
	if (m_cableFrom >= 0)
		drawCable(g, px(m_jacks[size_t(m_cableFrom)].c), px(m_cableTo), lemus::panel::col::accent, true, true);
}

void HardwareView::drawKeys(juce::Graphics &g)
{
	for (const auto &k : m_keys) {
		const bool down = m_display.keys[size_t(k.key)] || m_held.down(k.key);
		auto box = px(k.box);
		if (k.white) {
			g.setColour(down ? colour("key_white").darker(0.18f) : colour("key_white"));
			g.fillRect(box.reduced(0.6f, 0));
			if (!k.front.isEmpty()) {
				g.setColour(colour("key_white").darker(down ? 0.5f : 0.35f));
				g.fillRect(px(k.front).reduced(0.6f, 0));
			}
		} else {
			g.setColour(juce::Colours::black.withAlpha(0.5f));
			g.fillRect(box.translated(1.5f, 2.5f));
			g.setColour(down ? colour("key_black").brighter(0.25f) : colour("key_black"));
			g.fillRect(box);
			g.setColour(colour("key_black").brighter(0.18f));
			g.fillRect(box.reduced(box.getWidth() * 0.18f, 0).withTrimmedBottom(box.getHeight() * 0.12f));
		}
	}
}

void HardwareView::drawCheek(juce::Graphics &g)
{
	const auto scale = mmToPx();
	if (!m_wheelSlot.isEmpty()) {
		// The slot, and the wheel in it seen from above: its ribbed rim, darker where it curves away
		// at the front and back; the ribs move as it turns, a lighter one at the centre click.
		g.setColour(colour("slot_black"));
		g.fillPath(m_wheelSlot, scale);
		g.setColour(juce::Colours::white.withAlpha(0.08f));
		g.strokePath(m_wheelSlot, juce::PathStrokeType(0.25f), scale);
		const auto face = px(m_wheel).reduced(0.8f * kPxPerMm, 0.6f * kPxPerMm);
		auto base = colour("wheel_black").brighter(0.25f);
		if (m_hoverWheel || m_dragWheel)
			base = base.brighter(0.1f);
		juce::ColourGradient grad(base.darker(0.8f), 0, face.getY(), base.darker(0.8f), 0, face.getBottom(), false);
		grad.addColour(0.25, base.darker(0.1f));
		grad.addColour(0.5, base.brighter(0.35f));
		grad.addColour(0.75, base.darker(0.1f));
		g.setGradientFill(grad);
		const float round = 0.8f * kPxPerMm;
		g.fillRoundedRectangle(face, round);
		{
			juce::Graphics::ScopedSaveState state(g);
			juce::Path clip;
			clip.addRoundedRectangle(face, round);
			g.reduceClipRegion(clip);
			const float half = face.getHeight() / 2, cy = face.getCentreY();
			const float r = half * 1.08f;               // the rim's radius as seen
			const float pitch = 1.6f * kPxPerMm;        // along the rim
			const float turn = float(m_wheelValue) * r * 1.2f; // the rim's travel, end to end
			const auto rib = [&](float arc, juce::Colour c, float h) {
				const float ang = arc / r;
				if (std::abs(ang) >= juce::MathConstants<float>::halfPi)
					return;
				const float y = cy + r * std::sin(ang);
				g.setColour(c.withMultipliedAlpha(std::cos(ang) * 0.7f + 0.3f));
				g.fillRect(face.getX() + 1, y - h / 2, face.getWidth() - 2, h);
			};
			for (int k = -40; k <= 40; ++k) {
				const float arc = float(k) * pitch - std::fmod(turn, pitch);
				rib(arc, juce::Colours::black.withAlpha(0.35f), 1.0f);
			}
			rib(-turn, juce::Colour(0xff9a9ca3), 1.4f); // the centre mark
		}
		g.setColour(juce::Colours::black.withAlpha(0.45f));
		g.drawRoundedRectangle(face, round, 0.8f);
		juce::ColourGradient crown(juce::Colours::white.withAlpha(0.0f), face.getX(), 0, juce::Colours::white.withAlpha(0.0f), face.getRight(), 0, false);
		crown.addColour(0.35, juce::Colours::white.withAlpha(0.12f));
		g.setGradientFill(crown);
		g.fillRoundedRectangle(face, round);
	}
	if (!m_switch.isEmpty()) {
		const auto box = px(m_switch);
		g.setColour(colour("nut_chrome"));
		g.fillEllipse(box);
		g.setColour(m_held.down(kMomentary) ? colour("button_black").brighter(0.3f) : colour("button_black"));
		g.fillEllipse(box.reduced(box.getWidth() * 0.18f));
		if (m_hoverSwitch) {
			g.setColour(lemus::panel::col::accent.withAlpha(0.7f));
			g.drawEllipse(box.expanded(1.5f), 1.2f);
		}
	}
}

void HardwareView::drawBar(juce::Graphics &g)
{
	namespace lp = lemus::panel;
	lp::drawBarBackground(g, m_bar);
	lp::drawPill(g, m_bar[m_barService], "SERVICE", m_service, m_hoverBar == m_barService, kMono);
	// Keys and the momentary switch a right-click holds take the status line over until let go.
	const bool held = m_held.anyLatched();
	lp::drawStatus(g, m_bar.status(), status(), kMono, held ? lp::col::accent : lp::col::status);
	lp::drawPill(g, m_bar.help(), "?", m_help, m_hoverBar == lp::Bar::kHelp, kMono);
}

// SERVICE's card, over the bar above its pill: CIRCUIT, the two boards, the one fitted marked.
juce::Rectangle<float> HardwareView::serviceCard() const
{
	const auto pill = m_bar[m_barService];
	return {pill.getX(), float(kPanelHeight) - 8 - 196, 430, 196};
}
juce::Rectangle<float> HardwareView::serviceOption(int i) const
{
	const auto card = serviceCard().reduced(14);
	return {card.getX(), card.getY() + 24 + float(i) * 60, card.getWidth(), 56};
}
void HardwareView::drawService(juce::Graphics &g)
{
	namespace lp = lemus::panel;
	const auto card = serviceCard();
	g.setColour(juce::Colours::black.withAlpha(0.45f));
	g.fillRoundedRectangle(card.translated(0, 8).expanded(6), 12);
	g.setColour(lp::col::bar);
	g.fillRoundedRectangle(card, 8);
	g.setColour(lp::col::border.brighter(0.15f));
	g.drawRoundedRectangle(card.reduced(0.5f), 8, 1);
	const auto in = card.reduced(14);
	g.setColour(lp::col::status);
	g.setFont(kMono(11.0f, 1));
	g.drawText("CIRCUIT", in.withHeight(18).translated(4, 0), juce::Justification::centredLeft);
	const int fitted = m_proc.rev2() ? 1 : 0;
	for (int i = 0; i < 2; ++i) {
		const auto r = serviceOption(i);
		const bool on = i == fitted;
		if (on || i == m_hoverOption) {
			g.setColour(on ? lp::col::pillOn : lp::col::pillHover);
			g.fillRoundedRectangle(r, 6);
		}
		if (on) {
			g.setColour(lp::col::accent);
			g.drawRoundedRectangle(r.reduced(0.5f), 6, 1);
		}
		const auto dot = juce::Rectangle<float>(r.getX() + 12, r.getY() + 12, 14, 14);
		g.setColour(on ? lp::col::accent : lp::col::status);
		g.drawEllipse(dot.reduced(1), 2);
		if (on)
			g.fillEllipse(dot.reduced(4));
		auto text = r.withTrimmedLeft(38).reduced(0, 9);
		g.setFont(kMono(13.0f, 1));
		g.setColour(on ? lp::col::inkOn : lp::col::ink);
		g.drawText(kRevText[i][0], text.removeFromTop(17), juce::Justification::centredLeft);
		g.setFont(kMono(11.0f, 0));
		g.setColour(on ? lp::col::overlayText : lp::col::status);
		g.drawText(kRevText[i][1], text.withTrimmedTop(3), juce::Justification::topLeft, true);
	}
	g.setFont(kMono(11.0f, 0));
	g.setColour(lp::col::status);
	g.drawFittedText("A unit has one or the other. Switching restarts the circuit settled.", in.withTrimmedTop(150).toNearestInt(),
	                 juce::Justification::topLeft, 2);
}

void HardwareView::drawHelp(juce::Graphics &g)
{
	lemus::panel::drawHelp(g, getLocalBounds().toFloat().withHeight(float(kPanelHeight)), "MS-20",
	                       lemus::panel::byPlace({
	                           "A W S E D F T G Y H U J K O L P ; '   keys C C# D D# E F F# G G# A A# B C C# D D# E F",
	                           "Z  X                                  octave down / up, -1 to +1 (A is C3, C4 or C5; the keys run C3-C6)",
	                           "1                                     MOMENTARY SWITCH",
	                           "",
	                           "Cables: drag from a jack to another, an output into an input (one output feeds several).",
	                           "Drag a plug out of an input to move or pull it, right-click a jack to pull its cables;",
	                           "Escape drops a cable being drawn.",
	                           "Rotary switches: click for the next position (shift- or right-click: back), or drag round to one.",
	                           "VOLUME's counter-clockwise click is POWER OFF. The wheel: drag up or down (double-click centres it).",
	                           "ESP SIGNAL IN, EXT SIGNAL IN: click for the host's audio (the plugin's input); click again to pull it out.",
	                           "SERVICE (bar): CIRCUIT, the unit's filter board, REV 1 (two KORG35 modules) or REV 2 (KLM-307).",
	                           "MIDI notes C3-C6 play the keys (others fold in by octaves); pitch bend moves the wheel.",
	                       }),
	                       kMono);
}

juce::String HardwareView::status() const
{
	const auto held = m_held.latchedNames(heldName);
	if (!held.isEmpty())
		return lemus::panel::heldStatus(held);
	if (!m_proc.powered())
		return "POWER OFF (VOLUME's click): turn VOLUME up to switch it on";
	int cables = 0;
	const auto p = m_proc.patch();
	for (int in = 0; in < kFirstOutput; ++in)
		cables += p.patched(Jack(in));
	juce::String host;
	if (const int in = signalIn(); in == 1 || in == 2)
		host = juce::String(", the host's audio in ") + (in == 1 ? "ESP SIGNAL IN" : "EXT SIGNAL IN") + (m_proc.inputOn() ? "" : " (off in the host)");
	return juce::String(cables) + (cables == 1 ? " cable" : " cables") + host + "   OCT " + (m_octave > 0 ? "+" : "") + juce::String(m_octave);
}

juce::String HardwareView::controlTip(const Control &c) const
{
	if (c.param == nullptr)
		return c.label;
	const auto text = c.param->getName(32) + "  " + c.param->getCurrentValueAsText();
	if (m_volume < 0 || &c != &m_controls[size_t(m_volume)])
		return text;
	return c.param->convertFrom0to1(c.param->getValue()) < Ms20Processor::kPowerOffBelow
	           ? text + " (turn it up out of the click to switch on)"
	           : text + " (fully down it clicks into POWER OFF)";
}

juce::String HardwareView::jackTip(const Socket &j) const
{
	const auto name = jackName(j.label);
	if (j.jack < 0)
		return name + " (output): headphones; the plugin plays SIGNAL OUT";
	if (m_cableFrom >= 0) // a cable being drawn: where it would go
		return name + (j.input ? " (input)" : " (output)");
	const auto p = m_proc.patch();
	if (j.input) {
		if (!p.patched(Jack(j.jack)))
			return name + (takesHost(j.jack) ? " (input): no cable. Click for the host's audio (the plugin's input), or drag from it to an output"
			                                 : " (input): no cable. Drag from it to an output to patch one");
		const int from = p.from[size_t(j.jack)];
		if (from == kHostInput)
			return name + (m_proc.inputOn() ? " (input): the host's audio (the plugin's input). Click to pull it out"
			                                : " (input): the host's audio, silent: the host has the plugin's input off. "
			                                  "Turn it on in the DAW, or click to pull the cable out");
		const int jo = jackIndex(from);
		return name + " (input): a cable from " + (jo >= 0 ? jackName(m_jacks[size_t(jo)].label) : juce::String("?")) +
		       ". Drag the plug to move it or pull it out; right-click pulls it";
	}
	juce::StringArray to;
	for (int in = 0; in < kFirstOutput; ++in)
		if (p.from[size_t(in)] == j.jack)
			if (const int ji = jackIndex(in); ji >= 0)
				to.add(jackName(m_jacks[size_t(ji)].label));
	const auto io = j.jack == int(Jack::SignalOut) ? juce::String(" (output, the plugin's)") : juce::String(" (output)");
	if (to.isEmpty())
		return name + io + ": no cable. Drag from it to an input to patch one";
	return name + io + ": " + (to.size() > 1 ? "cables to " : "a cable to ") + to.joinIntoString(", ") +
	       ". Drag from it for another; right-click pulls " + (to.size() > 1 ? "them" : "it");
}

void HardwareView::drawTooltip(juce::Graphics &g)
{
	const bool dragging = m_dragControl >= 0 || m_cableFrom >= 0 || m_dragWheel || m_mouseSwitch || m_keyDrag;
	if (m_service && m_hoverOption < 0 && m_hoverBar != m_barService && !dragging)
		return; // the card is the tip
	if ((m_hoverBar == m_barService || m_hoverBar == lemus::panel::Bar::kHelp) && !dragging) {
		lemus::panel::drawBarTip(g, m_bar, m_hoverBar, getLocalBounds().toFloat(), kMono);
		return;
	}
	juce::String text;
	juce::Point<float> at;
	if (const int c = m_dragControl >= 0 ? m_dragControl : m_hoverControl; c >= 0) {
		const auto &ctl = m_controls[size_t(c)];
		text = controlTip(ctl);
		at = px(ctl.c + juce::Point<float>(0, ctl.d / 2)) + juce::Point<float>(0, 6);
	} else if (m_hoverJack >= 0) {
		const auto &j = m_jacks[size_t(m_hoverJack)];
		text = jackTip(j);
		at = px(j.c + juce::Point<float>(0, j.nut / 2)) + juce::Point<float>(0, 6);
	} else if (m_dragWheel || m_hoverWheel) {
		const double v = m_wheelValue * 5;
		const auto volts = std::abs(v) < 0.05 ? juce::String("0.0") : (v > 0 ? "+" : "") + juce::String(v, 1);
		text = "Control wheel  " + volts + " V: drag up or down, double-click to centre (pitch bend moves it too)";
		at = {px(m_wheel).getCentreX(), px(m_wheel).getBottom() + 6};
	} else if (m_hoverSwitch || m_mouseSwitch) {
		text = m_held.latched(kMomentary) ? "Momentary switch: held down. Click it or press Esc to let go"
		                                  : "Momentary switch: hold it with the mouse (right-click: it stays down until clicked again)";
		at = {px(m_switch).getCentreX(), px(m_switch).getBottom() + 6};
	}
	lemus::panel::drawTooltip(g, text, at, getLocalBounds().toFloat().withHeight(float(kPanelHeight)), kMono);
}

// =========================================================================================== mouse

void HardwareView::mouseMove(const juce::MouseEvent &e)
{
	const auto mm = toMm(e.position);
	const bool onPanel = e.position.y < kPanelHeight;
	const int c = onPanel ? controlAt(mm) : -1;
	const int j = onPanel && c < 0 ? jackAt(mm) : -1;
	const bool s = onPanel && c < 0 && j < 0 && overSwitch(mm);
	const bool w = onPanel && c < 0 && j < 0 && !s && overWheel(mm);
	const int b = m_bar.hit(e.position);
	int o = -1;
	for (int i = 0; m_service && i < 2; ++i)
		if (serviceOption(i).contains(e.position))
			o = i;
	if (o != m_hoverOption) {
		m_hoverOption = o;
		repaint();
	}
	if (c != m_hoverControl || j != m_hoverJack || b != m_hoverBar || s != m_hoverSwitch || w != m_hoverWheel) {
		m_hoverControl = c;
		m_hoverJack = j;
		m_hoverBar = b;
		m_hoverSwitch = s;
		m_hoverWheel = w;
		repaint();
	}
}

void HardwareView::mouseExit(const juce::MouseEvent &)
{
	m_hoverControl = m_hoverJack = m_hoverBar = -1;
	m_hoverSwitch = m_hoverWheel = false;
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
	const int b = m_bar.hit(e.position);
	if (b == lemus::panel::Bar::kHelp) {
		m_help = true;
		repaint();
		return;
	}
	if (b == m_barService) {
		m_service = !m_service;
		m_hoverOption = -1;
		repaint();
		return;
	}
	if (m_service) {
		// the card open: a click on an option picks the board; anywhere else closes it
		for (int i = 0; i < 2; ++i)
			if (serviceOption(i).contains(e.position)) {
				if (m_proc.rev2() != (i == 1))
					m_proc.setRev2(i == 1);
				repaint();
				return;
			}
		if (!serviceCard().contains(e.position)) {
			m_service = false;
			m_hoverOption = -1;
			repaint();
		}
		return;
	}
	if (e.position.y >= kPanelHeight)
		return;
	const auto mm = toMm(e.position);
	if (const int c = controlAt(mm); c >= 0) {
		// a knob drags; a rotary switch steps on the click (mouseUp) or follows a drag round
		const auto &ctl = m_controls[size_t(c)];
		m_dragControl = c;
		m_dragStart = e.position.y;
		m_dragValue = float(value(ctl));
		m_dragDetent = detentOf(ctl);
		if (ctl.param != nullptr)
			ctl.param->beginChangeGesture();
		repaint();
		return;
	}
	if (const int j = jackAt(mm); j >= 0) {
		const auto &jack = m_jacks[size_t(j)];
		auto p = m_proc.patch();
		if (e.mods.isPopupMenu()) {
			// pull every cable at this jack
			for (int in = 0; in < kFirstOutput; ++in)
				if ((jack.input && in == jack.jack) || (!jack.input && p.from[size_t(in)] == jack.jack))
					p.disconnect(Jack(in));
			m_proc.setPatch(p);
			repaint();
			return;
		}
		if (jack.input && jack.jack >= 0 && p.patched(Jack(jack.jack))) {
			// pull the plug out of this input: its cable now hangs from its output end
			const int from = p.from[size_t(jack.jack)];
			p.disconnect(Jack(jack.jack));
			m_proc.setPatch(p);
			m_cableFrom = from == kHostInput ? -1 : jackIndex(from);
			if (m_cableFrom >= 0) {
				m_pulledIn = jack.jack;
				m_pulledFrom = from;
			}
		} else {
			m_cableFrom = j;
		}
		m_cableTo = mm;
		repaint();
		return;
	}
	if (overSwitch(mm)) {
		// the momentary switch: held while the mouse is down, or latched by a right-click
		m_mouseSwitch = m_held.mouseDown(kMomentary, e.mods);
		repaint();
		return;
	}
	if (overWheel(mm)) {
		m_dragWheel = true;
		m_dragStart = e.position.y;
		m_wheelStart = m_wheelValue;
		return;
	}
	if (const int k = keyAt(mm); k >= 0 && m_held.mouseDown(k, e.mods)) {
		m_mouseKey = k;
		m_keyDrag = true;
	}
	repaint();
}

void HardwareView::mouseDrag(const juce::MouseEvent &e)
{
	if (m_dragControl >= 0) {
		const auto &c = m_controls[size_t(m_dragControl)];
		if (c.rotary) {
			// dragged round: the position nearest the mouse
			if (c.detents.size() > 1 && e.getDistanceFromDragStart() > 3)
				if (const int d = detentAt(c, toMm(e.position)); d != detentOf(c))
					setValue(c, double(d) / double(c.detents.size() - 1));
			return;
		}
		const float range = e.mods.isShiftDown() ? 1100.0f : 220.0f; // shift: a fifth as far
		setValue(c, double(m_dragValue) + double(m_dragStart - e.position.y) / range);
		return;
	}
	if (m_cableFrom >= 0) {
		m_cableTo = toMm(e.position);
		m_hoverJack = jackAt(m_cableTo);
		repaint();
		return;
	}
	if (m_dragWheel) {
		double v = juce::jlimit(-1.0, 1.0, m_wheelStart + double(m_dragStart - e.position.y) / 120.0);
		// the centre click
		if (std::abs(v) < 0.04)
			v = 0;
		setWheel(v);
		return;
	}
	if (m_keyDrag) {
		// sliding along the keys plays them in turn (not over a latched one)
		const int k = e.position.y < kPanelHeight ? keyAt(toMm(e.position)) : -1;
		if (k >= 0 && k != m_mouseKey && !m_held.latched(k)) {
			m_held.mouseUp(m_mouseKey);
			m_mouseKey = m_held.mouseDown(k, {}) ? k : -1;
		}
	}
}

void HardwareView::mouseUp(const juce::MouseEvent &e)
{
	if (m_dragControl >= 0) {
		const auto &c = m_controls[size_t(m_dragControl)];
		if (c.rotary && c.detents.size() > 1 && e.getDistanceFromDragStart() <= 3) {
			// a click: one position clockwise, wrapping (shift- or right-click: one back)
			const int n = int(c.detents.size());
			setValue(c, double((m_dragDetent + (e.mods.isRightButtonDown() || e.mods.isShiftDown() ? n - 1 : 1)) % n) / double(n - 1));
		}
		if (c.param != nullptr)
			c.param->endChangeGesture();
		m_dragControl = -1;
	}
	if (m_cableFrom >= 0) {
		const int at = jackAt(toMm(e.position));
		const auto &from = m_jacks[size_t(m_cableFrom)];
		if (at == m_cableFrom && e.getDistanceFromDragStart() <= 3 && from.input && takesHost(from.jack) && m_pulledIn < 0) {
			// a click on an empty signal input: the host's audio goes in here (out of the other one)
			setSignalIn(from.jack == int(Jack::EspSignalIn) ? 1 : 2);
			repaint();
		} else {
			connect(m_cableFrom, at);
		}
		m_cableFrom = -1;
	}
	m_pulledIn = m_pulledFrom = -1;
	if (m_mouseSwitch)
		m_held.mouseUp(kMomentary);
	m_mouseSwitch = false;
	m_dragWheel = false;
	if (m_mouseKey >= 0)
		m_held.mouseUp(m_mouseKey);
	m_mouseKey = -1;
	m_keyDrag = false;
	repaint();
}

void HardwareView::mouseDoubleClick(const juce::MouseEvent &e)
{
	const auto mm = toMm(e.position);
	if (const int c = controlAt(mm); c >= 0) {
		const auto &ctl = m_controls[size_t(c)];
		if (ctl.param != nullptr && !ctl.rotary)
			setValue(ctl, double(ctl.param->getDefaultValue()));
		return;
	}
	if (overWheel(mm))
		setWheel(0);
}

void HardwareView::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
	const int c = controlAt(toMm(e.position));
	if (c < 0)
		return;
	const auto &ctl = m_controls[size_t(c)];
	if (ctl.rotary) {
		const double n = double(ctl.detents.size() - 1);
		setValue(ctl, value(ctl) + (w.deltaY > 0 ? 1.0 : -1.0) / n);
	} else {
		setValue(ctl, value(ctl) + double(w.deltaY) * (e.mods.isShiftDown() ? 0.02 : 0.08));
	}
}

// ======================================================================================== keyboard

bool HardwareView::keyPressed(const juce::KeyPress &k)
{
	switch (lemus::panel::commonKey(k)) {
	case lemus::panel::Common::Help:
		m_help = !m_help;
		repaint();
		return true;
	case lemus::panel::Common::Escape:
		m_help = false;
		m_service = false;
		cancelCable();
		m_held.releaseAll(true);
		repaint();
		return true;
	case lemus::panel::Common::None: break;
	}
	if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown() || k.getKeyCode() == juce::KeyPress::spaceKey)
		return false;
	const auto lc = lemus::panel::usKey(k);
	if (lc == 'z' || lc == 'x') {
		m_octave = juce::jlimit(-1, 1, m_octave + (lc == 'x' ? 1 : -1));
		repaint();
		return true;
	}
	if (lc == '1') {
		m_held.keyDown(k.getKeyCode(), kMomentary);
		return true;
	}
	int semi = -1;
	for (int i = 0; kKeyRow[i] != 0; ++i)
		if (juce::juce_wchar(kKeyRow[i]) == lc)
			semi = i;
	if (semi < 0)
		return false;
	const int key = 12 + 12 * m_octave + semi; // A is the keyboard's middle C (key 12, C4)
	if (key >= 0 && key < Machine::kKeys)
		m_held.keyDown(k.getKeyCode(), key);
	return true;
}

bool HardwareView::keyStateChanged(bool) { return m_held.keysChanged(); }

void HardwareView::focusLost(FocusChangeType) { m_held.focusLost(); }

void HardwareView::timerCallback()
{
	m_display = m_proc.display();
	if (!m_dragWheel)
		m_wheelValue = m_proc.manual().wheel; // pitch bend moves it too
	repaint();
}

} // namespace ms20
