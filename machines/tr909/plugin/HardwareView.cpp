#include "HardwareView.h"
#include "Fonts.h"

#include <TR909Panel.h>

#include <cmath>

namespace tr909 {

namespace {
const lemus::panel::Mono kMono = [](float size, int weight) { return fonts::mono(size, fonts::Weight(weight)); };
constexpr float kPi = juce::MathConstants<float>::pi;

const juce::var &panelJson()
{
	static const juce::var json = juce::JSON::parse(juce::String::fromUTF8(TR909Panel::panel_json, TR909Panel::panel_jsonSize));
	return json;
}

juce::Colour colourOf(const juce::var &key, juce::Colour fallback = juce::Colours::magenta)
{
	if (!key.isString())
		return fallback;
	const auto c = panelJson()["colours"][juce::Identifier(key.toString())];
	if (!c.isObject())
		return fallback;
	return juce::Colour::fromString("ff" + c["hex"].toString().removeCharacters("#"));
}

float num(const juce::var &v, float fallback = 0)
{
	return v.isDouble() || v.isInt() || v.isInt64() ? float(double(v)) : fallback;
}

// The i-th item of a JSON array as a number (fallback if it isn't one).
float item(const juce::var &a, int i, float fallback = 0)
{
	auto *array = a.getArray();
	return array != nullptr && i < array->size() ? num((*array)[i], fallback) : fallback;
}

juce::Point<float> pointOf(const juce::var &p)
{
	return {item(p, 0), item(p, 1)};
}

juce::Rectangle<float> rectOf(const juce::var &bb)
{
	return {item(bb, 0), item(bb, 1), item(bb, 2) - item(bb, 0), item(bb, 3) - item(bb, 1)};
}

juce::Path polyline(const juce::var &points, bool closed)
{
	juce::Path p;
	if (auto *a = points.getArray())
		for (int i = 0; i < a->size(); ++i) {
			const auto q = pointOf((*a)[i]);
			if (i == 0)
				p.startNewSubPath(q);
			else
				p.lineTo(q);
		}
	if (closed)
		p.closeSubPath();
	return p;
}

// Panel legends: Helvetica's cap height is about 0.717 of the font height, Times's 0.662.
juce::Font legendFont(const juce::String &spec, float capH)
{
	if (spec.containsIgnoreCase("times"))
		return juce::Font(juce::FontOptions("Times New Roman", capH / 0.662f, juce::Font::bold));
	if (spec.containsIgnoreCase("bold"))
		return juce::Font(juce::FontOptions("Helvetica Neue", capH / 0.717f, juce::Font::bold));
	return juce::Font(juce::FontOptions("Helvetica Neue", capH / 0.717f, juce::Font::plain).withStyle("Medium"));
}

// Text as glyph paths, its cap height centred in box, squeezed or stretched a little to the
// measured width.
void drawLegend(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> box, const juce::Font &font, juce::Colour c, bool fitWidth)
{
	juce::GlyphArrangement ga;
	ga.addLineOfText(font, text, 0, 0);
	juce::Path p;
	ga.createPath(p);
	const auto bb = p.getBounds();
	if (bb.isEmpty())
		return;
	const float sx = fitWidth ? juce::jlimit(0.75f, 1.3f, box.getWidth() / bb.getWidth()) : 1.0f;
	const float capH = font.getHeight() * (text.containsOnly("I") ? 0.662f : 0.717f);
	const float base = box.getCentreY() + capH * 0.5f;
	p.applyTransform(juce::AffineTransform::translation(-bb.getCentreX(), 0).scaled(sx, 1.0f).translated(box.getCentreX(), base));
	g.setColour(c);
	g.fillPath(p);
}

// A note glyph filling box: the head a tilted oval at the lower left, the stem up from its right
// side, `flags` flags curling down to the right.
void noteGlyph(juce::Graphics &g, juce::Rectangle<float> box, int flags)
{
	const float h = box.getHeight();
	const float hw = h * 0.165f, hh = h * 0.12f;
	const float x = box.getX() + hw * 1.05f, y = box.getBottom() - hh * 1.1f;
	juce::Path p;
	juce::Path head;
	head.addEllipse(-hw, -hh, 2 * hw, 2 * hh);
	head.applyTransform(juce::AffineTransform::rotation(-0.38f).translated(x, y));
	p.addPath(head);
	const float sx = x + hw * 0.86f;
	const float sw = h * 0.075f;
	p.addRectangle(sx - sw, box.getY(), sw, y - box.getY());
	for (int i = 0; i < flags; ++i) {
		juce::Path f;
		const float fy = box.getY() + float(i) * h * 0.22f;
		const float fx = sx - sw * 0.5f;
		f.startNewSubPath(fx - sw * 0.5f, fy);
		f.cubicTo(fx + h * 0.06f, fy + h * 0.16f, fx + h * 0.34f, fy + h * 0.2f, fx + h * 0.24f, fy + h * 0.5f);
		f.cubicTo(fx + h * 0.27f, fy + h * 0.3f, fx + h * 0.05f, fy + h * 0.26f, fx - sw * 0.5f, fy + h * 0.2f);
		f.closeSubPath();
		p.addPath(f);
	}
	g.fillPath(p);
}

juce::Point<float> polar(juce::Point<float> c, float r, float a) { return {c.x + r * std::sin(a), c.y - r * std::cos(a)}; }

// A 7-segment digit in r (bit 0 = a .. bit 6 = g), slanted forward by `slant` (tan of the angle).
void drawDigit(juce::Graphics &g, juce::Rectangle<float> r, float t, float slant, uint8_t seg, juce::Colour on, juce::Colour off)
{
	const float w = r.getWidth(), h = r.getHeight(), x = r.getX(), y = r.getY();
	const float gap = t * 0.18f, half = (h - t) / 2;
	struct Seg {
		bool horizontal;
		float x, y, len;
	};
	const Seg s[7] = {
		{true, x + t / 2, y + t / 2, w - t},        // a
		{false, x + w - t / 2, y + t / 2, half},    // b
		{false, x + w - t / 2, y + h / 2, half},    // c
		{true, x + t / 2, y + h - t / 2, w - t},    // d
		{false, x + t / 2, y + h / 2, half},        // e
		{false, x + t / 2, y + t / 2, half},        // f
		{true, x + t / 2, y + h / 2, w - t},        // g
	};
	const auto skew = juce::AffineTransform::shear(-slant, 0).translated(slant * (y + h / 2), 0);
	for (int i = 0; i < 7; ++i) {
		// a segment: a hexagon with pointed ends, a little short of its neighbours
		const auto &q = s[i];
		juce::Path p;
		const float a = q.len - 2 * gap, ht = t / 2;
		if (q.horizontal) {
			const float x0 = q.x + gap, yc = q.y;
			p.startNewSubPath(x0, yc);
			p.lineTo(x0 + ht, yc - ht);
			p.lineTo(x0 + a - ht, yc - ht);
			p.lineTo(x0 + a, yc);
			p.lineTo(x0 + a - ht, yc + ht);
			p.lineTo(x0 + ht, yc + ht);
		} else {
			const float y0 = q.y + gap, xc = q.x;
			p.startNewSubPath(xc, y0);
			p.lineTo(xc + ht, y0 + ht);
			p.lineTo(xc + ht, y0 + a - ht);
			p.lineTo(xc, y0 + a);
			p.lineTo(xc - ht, y0 + a - ht);
			p.lineTo(xc - ht, y0 + ht);
		}
		p.closeSubPath();
		p.applyTransform(skew);
		g.setColour((seg >> i) & 1 ? on : off);
		g.fillPath(p);
	}
}

// The panel's parameter hints for keys (docs/panel.json: the tr909::Machine::Key enumerator) and
// LEDs (tr909::Machine::Led).
int machineKey(const juce::String &name)
{
	using K = Machine::Key;
	if (name.startsWith("Step"))
		return int(K::Step1) + juce::jlimit(1, 16, name.substring(4).getIntValue()) - 1;
	if (name.startsWith("Track"))
		return int(K::Track1) + juce::jlimit(1, 4, name.substring(5).getIntValue()) - 1;
	if (name.startsWith("Pattern"))
		return int(K::Pattern1) + juce::jlimit(1, 3, name.substring(7).getIntValue()) - 1;
	static const std::pair<const char *, K> keys[] = {
		{"Shift", K::Shift},         {"Scale", K::Scale},       {"InstSelect", K::InstSelect}, {"ShuffleFlam", K::ShuffleFlam},
		{"LastStep", K::LastStep},   {"StopCont", K::StopCont}, {"Start", K::Start},           {"Clear", K::Clear},
		{"EnterAccent", K::EnterAccent}, {"TapeSync", K::TapeSync}, {"Cycle", K::Cycle},         {"AvailableMeas", K::AvailableMeas},
		{"Fwd", K::Fwd},             {"Back", K::Back},         {"Tempo", K::Tempo},           {"ExtInst", K::ExtInst},
	};
	for (const auto &[n, k] : keys)
		if (name == n)
			return int(k);
	return -1;
}

int machineLed(const juce::String &name)
{
	using L = Machine::Led;
	if (name.startsWith("Step"))
		return int(L::Step1) + juce::jlimit(1, 16, name.substring(4).getIntValue()) - 1;
	if (name.startsWith("Track"))
		return int(L::Track1) + juce::jlimit(1, 4, name.substring(5).getIntValue()) - 1;
	if (name.startsWith("Pattern"))
		return int(L::Pattern1) + juce::jlimit(1, 3, name.substring(7).getIntValue()) - 1;
	if (name.startsWith("Scale"))
		return int(L::Scale1) + juce::jlimit(1, 4, name.substring(5).getIntValue()) - 1;
	static const std::pair<const char *, L> leds[] = {
		{"Enter", L::Enter}, {"ExtInst", L::ExtInst}, {"Tempo", L::Tempo},     {"Back", L::Back},
		{"Fwd", L::Fwd},     {"Cycle", L::Cycle},     {"AvailableMeas", L::AvailableMeas}, {"TapeSync", L::TapeSync},
	};
	for (const auto &[n, l] : leds)
		if (name == n)
			return int(l);
	return -1;
}
} // namespace

// ====================================================================== the panel

const std::vector<HardwareView::Element> &HardwareView::elements()
{
	static const std::vector<Element> list = [] {
		std::vector<Element> out;
		if (auto *a = panelJson()["elements"].getArray())
			for (const auto &v : *a) {
				Element e;
				e.type = v["type"].toString();
				e.id = v["id"].toString();
				e.param = v["param"].toString();
				e.label = v["label"].toString();
				e.box = rectOf(v["bbox"]);
				e.centre = {num(v["x"], e.box.getCentreX()), num(v["y"], e.box.getCentreY())};
				e.v = v;
				out.push_back(e);
			}
		return out;
	}();
	return list;
}

HardwareView::HardwareView(TR909Processor &p) : m_proc(p)
{
	setSize(kWidth, kHeight);
	setWantsKeyboardFocus(true);
	setOpaque(true);
	const auto &all = elements();
	std::map<juce::String, int> byId;
	for (size_t i = 0; i < all.size(); ++i)
		byId[all[i].id] = int(i);
	for (size_t i = 0; i < all.size(); ++i) {
		const auto &e = all[i];
		if (e.type == "knob")
			m_knobs.push_back(int(i));
		else if (e.type == "led")
			m_leds.push_back(int(i));
		else if (e.type == "display")
			m_display = int(i);
		else if (e.type == "key") {
			const int k = machineKey(e.param);
			if (k < 0)
				continue;
			const auto led = byId.find(e.v["led"].toString());
			m_keys.push_back({int(i), Machine::Key(k), led != byId.end() ? led->second : -1});
		}
	}
	m_held.reset(m_keys.size(), [this](int i, bool down) {
		m_proc.setKey(m_keys[size_t(i)].key, down);
		repaint();
	});
	using Bar = lemus::panel::Bar;
	m_barSync = m_bar.add(Bar::kSync, "OFF: the 909's own clock. HOST: MIDI sync, run from the DAW's transport");
	m_barRear = m_bar.add(Bar::kRear, "The rear panel: the MULTI OUT jacks and POWER");
	m_barService = m_bar.add(Bar::kService);
	// The brand marks come from the user's artwork (a file per place, or an older tr909.svg pack);
	// without it the panel has none. Their ids are the logo elements' "logo" names, and rear_roland.
	for (const auto &mark : p.artwork().marks())
		m_logos[mark.id] = mark;
	m_snap = p.snapshot();
	startTimerHz(30);
}

HardwareView::~HardwareView()
{
	releaseAllKeys(true);
	m_proc.setProbe(0, ""); // the scope goes with the window: the board may sleep again
	m_proc.setProbe(1, "");
}

// ====================================================================== parameters and keys

float HardwareView::paramValue(const juce::String &id) const
{
	if (auto *p = m_proc.params().getParameter(id))
		return p->getValue();
	return 0;
}

void HardwareView::setParamValue(const juce::String &id, float v)
{
	if (auto *p = m_proc.params().getParameter(id))
		p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, v));
}

juce::String HardwareView::tempoText() const
{
	return juce::String(TR909Processor::tempoBpmOf(m_snap.tempo), 1) + " BPM";
}

juce::String HardwareView::paramText(const juce::String &id) const
{
	auto *p = m_proc.params().getParameter(id);
	if (p == nullptr)
		return {};
	if (id == "tempo" && m_snap.tempo >= 0) // what the firmware runs at, a step either side of the knob's
		return p->getName(40) + "  " + tempoText();
	return p->getName(40) + "  " + p->getCurrentValueAsText();
}

int HardwareView::keyIndexOf(Machine::Key k) const
{
	for (size_t i = 0; i < m_keys.size(); ++i)
		if (m_keys[i].key == k)
			return int(i);
	return -1;
}

void HardwareView::pressKey(int keyIndex, bool down)
{
	m_held.set(keyIndex, down);
}

void HardwareView::releaseAllKeys(bool latchedToo)
{
	m_held.releaseAll(latchedToo);
}

float HardwareView::ledLevel(const Element &e) const
{
	const int l = machineLed(e.v["led"].toString());
	return l >= 0 && size_t(l) < m_snap.leds.size() ? m_snap.leds[size_t(l)] : 0.0f;
}

void HardwareView::timerCallback()
{
	// Repaint what changed: the LEDs and display, a knob the host moved, the bar's readings.
	const auto s = m_proc.snapshot();
	const bool power = s.powered != m_snap.powered;
	bool panel = s.leds != m_snap.leds || s.digits != m_snap.digits || power;
	const auto &all = elements();
	m_knobValues.resize(m_knobs.size(), -1.0f);
	for (size_t i = 0; i < m_knobs.size(); ++i) {
		const float v = paramValue(all[size_t(m_knobs[i])].param);
		panel = panel || v != m_knobValues[i];
		m_knobValues[i] = v;
	}
	const bool sync = paramValue("sync") > 0.5f;
	bool bar = s.voices != m_snap.voices || s.syncMode != m_snap.syncMode || s.synced != m_snap.synced || sync != m_syncShown;
	m_syncShown = sync;
	m_snap = s;
	// the cables and the host's outputs (a session load, the host turning an output on); the rear
	// shows only those
	bool rear = false;
	for (int v = 0; v < kVoices; ++v) {
		const bool cable = m_proc.cable(Voice(v)), on = m_proc.outputOn(Voice(v));
		bar = bar || cable != m_cablesShown[size_t(v)];
		rear = rear || cable != m_cablesShown[size_t(v)] || on != m_outputsShown[size_t(v)];
		m_cablesShown[size_t(v)] = cable;
		m_outputsShown[size_t(v)] = on;
	}
	if (m_rear)
		panel = rear || power;
	if (m_service) {
		// the detailed picture once the zoom or pan has settled; the serial or TM moved by a session
		// load or the host
		const bool stale = m_zoom > 1.001f && (m_detailZoom != m_zoom || m_detailPan != m_pan);
		const bool settled = juce::Time::getMillisecondCounter() - m_viewChangedAt >= 150 && !m_panning;
		const float tm = paramValue("tm");
		panel = (stale && settled) || m_proc.serial() != m_serialShown || tm != m_tmShown;
		m_serialShown = m_proc.serial();
		m_tmShown = tm;
		// the scope's sweep, and the voice under test played every 0.6 s, unaccented
		updateScope();
		const uint32_t now = juce::Time::getMillisecondCounter();
		if (m_scope.test && now - m_scope.lastTestMs >= 600) {
			m_scope.lastTestMs = now;
			m_proc.playNote(TR909Processor::noteFor(Voice(m_scope.trigger >= 0 ? m_scope.trigger : int(Voice::HC))), 50);
		}
	}
	if (panel)
		repaint();
	else if (bar)
		repaint(0, kPanelHeight, kWidth, kBarHeight);
}

// ====================================================================== static artwork

void HardwareView::drawStaticElement(juce::Graphics &g, const Element &e)
{
	const auto &v = e.v;
	const auto colour = colourOf(v["colour"]);
	if (e.type == "region" || e.type == "fill" || e.type == "label_box") {
		g.setColour(colour);
		g.fillRoundedRectangle(e.box, num(v["corner_r"]));
	} else if (e.type == "line") {
		g.setColour(colour);
		g.strokePath(polyline(v["points"], bool(v["closed"])),
		             juce::PathStrokeType(num(v["stroke"], 0.4f), juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
	} else if (e.type == "outline_box") {
		const float s = num(v["stroke"], 0.45f), r = num(v["corner_r"]);
		g.setColour(colour);
		if (v["points"].isArray()) {
			// the keyline's centre line is inset half a stroke from the measured outer edge
			auto p = polyline(v["points"], true);
			const auto b = p.getBounds();
			p.applyTransform(juce::AffineTransform::translation(-b.getCentreX(), -b.getCentreY())
			                     .scaled((b.getWidth() - s) / b.getWidth(), (b.getHeight() - s) / b.getHeight())
			                     .translated(b.getCentreX(), b.getCentreY()));
			if (r > 0)
				p = p.createPathWithRoundedCorners(r);
			g.strokePath(p, juce::PathStrokeType(s));
		} else {
			g.drawRoundedRectangle(e.box.reduced(s / 2), r, s);
		}
	} else if (e.type == "text") {
		const float capH = num(v["cap_h"], e.box.getHeight());
		drawLegend(g, e.label, e.box, legendFont(v["font"].toString(), capH), colour, e.label.length() > 1);
	} else if (e.type == "symbol") {
		g.setColour(colour);
		if (e.label.contains("eighth"))
			noteGlyph(g, e.box, 1);
		else if (e.label.contains("quarter"))
			noteGlyph(g, e.box, 0);
	} else if (e.type == "logo") {
		// a place's own file as it looks, fitted to the logo's box; a pack's outline where it is, in
		// the print colour
		const auto it = m_logos.find(v["logo"].toString());
		if (it != m_logos.end()) {
			if (it->second.picture != nullptr) {
				it->second.picture->drawWithin(g, e.box);
			} else {
				g.setColour(colour);
				g.fillPath(it->second.outline);
			}
		}
	} else if (e.type == "knob") {
		// the printed scale: ticks, the long ones at the ends and at 12 o'clock (or every 30 degrees)
		const auto sc = v["scale"];
		const auto c = e.centre;
		const float r0 = num(sc["r_inner"], 5.8f), r1 = num(sc["r_outer"], 7.8f), rl = num(sc["r_outer_long"], r1);
		juce::Array<float> longAt;
		if (auto *a = sc["long_at_deg"].getArray())
			for (const auto &x : *a)
				longAt.add(num(x));
		g.setColour(colourOf("print_dark"));
		if (auto *angles = sc["angles_deg"].getArray())
			for (const auto &deg : *angles) {
				const float d = num(deg);
				bool isLong = false;
				for (float l : longAt)
					isLong = isLong || std::abs(l - d) < 0.1f;
				const float a = juce::degreesToRadians(d);
				g.drawLine({polar(c, r0, a), polar(c, isLong ? rl : r1, a)}, num(sc["tick_w"], 0.4f));
			}
	} else if (e.type == "key") {
		// the cut-out in the panel: a dark gap round the cap, deepest along the front
		if (v["shape"].toString() != "large")
			return; // the small buttons draw their own gap
		g.setColour(colourOf("key_hole", juce::Colours::black));
		g.fillRoundedRectangle(rectOf(v["cutout_bbox"]), 0.4f);
	} else if (e.type == "display") {
		// the red filter in its window, flush with the panel: a fine dark edge round it
		g.setColour(juce::Colour(0xff201416));
		g.fillRoundedRectangle(e.box.expanded(0.35f), 0.5f);
		juce::ColourGradient filter(colour.brighter(0.08f), e.box.getX(), e.box.getY(), colour.darker(0.25f), e.box.getRight(), e.box.getBottom(), false);
		g.setGradientFill(filter);
		g.fillRect(e.box);
	} else if (e.type == "led") {
		// the round scale LEDs sit in a dark socket in the bar
		if (v["shape"].toString() == "round") {
			const float d = num(v["d"], 3.0f) + 0.5f;
			g.setColour(juce::Colour(0xff1d1d20));
			g.fillEllipse(e.centre.x - d / 2, e.centre.y - d / 2, d, d);
		}
	}
}

void HardwareView::renderStatic(juce::Graphics &g)
{
	const auto &all = elements();
	const auto find = [&](const char *id) -> const Element * {
		for (const auto &e : all)
			if (e.id == id)
				return &e;
		return nullptr;
	};
	g.fillAll(juce::Colour(0xff1a1a18));
	// The case from above: the olive side cheeks, the top plate between them.
	const auto cheek = colourOf("side_panel");
	g.setColour(cheek.darker(0.25f));
	g.fillRoundedRectangle(0, 0, kPanelW, kPanelH, 1.0f);
	for (const char *id : {"side_panel_left", "side_panel_right"})
		if (const auto *e = find(id)) {
			juce::ColourGradient cg(cheek.brighter(0.1f), e->box.getX(), 0, cheek.darker(0.12f), e->box.getRight(), 0, false);
			if (e->box.getX() > kPanelW / 2)
				cg = juce::ColourGradient(cheek.darker(0.12f), e->box.getX(), 0, cheek.brighter(0.1f), e->box.getRight(), 0, false);
			g.setGradientFill(cg);
			g.fillRoundedRectangle(e->box, 1.0f);
		}
	if (const auto *plate = find("top_plate")) {
		const auto b = plate->box;
		const auto paint = colourOf(plate->v["colour"]);
		g.setColour(paint);
		g.fillRoundedRectangle(b, num(plate->v["corner_r"]));
		// satin sheen: brighter towards the rear, the slope falling away towards the player
		juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.07f), 0, b.getY(), juce::Colours::black.withAlpha(0.045f), 0, b.getBottom(), false);
		sheen.addColour(0.45, juce::Colours::transparentWhite);
		g.setGradientFill(sheen);
		g.fillRect(b);
	}
	// The plate bends down at the rear and the front and meets the cheeks in a fine dark seam: the
	// panel's rear_bend, front_bend, seam and plate_edge elements draw them. Without them, a plain
	// shading.
	if (const auto *plate = find("top_plate"); plate != nullptr && find("rear_bend") == nullptr) {
		const auto b = plate->box;
		const auto paint = colourOf(plate->v["colour"]);
		juce::ColourGradient rear(paint.darker(0.22f), 0, b.getY(), paint.withAlpha(0.0f), 0, b.getY() + 1.1f, false);
		g.setGradientFill(rear);
		g.fillRect(b.withHeight(1.1f));
		juce::ColourGradient front(paint.withAlpha(0.0f), 0, b.getBottom() - 1.3f, paint.darker(0.35f), 0, b.getBottom(), false);
		g.setGradientFill(front);
		g.fillRect(b.withTrimmedTop(b.getHeight() - 1.3f));
		g.setColour(juce::Colours::black.withAlpha(0.6f));
		g.fillRect(b.getX(), b.getY(), 0.45f, b.getHeight());
		g.fillRect(b.getRight() - 0.45f, b.getY(), 0.45f, b.getHeight());
	}
	for (const char *type : {"fill", "label_box", "outline_box", "line", "text", "symbol", "logo", "knob", "key", "display", "led"})
		for (const auto &e : all)
			if (e.type == type)
				drawStaticElement(g, e);
}

// ====================================================================== moving parts

// A knob from above: the charcoal top face, a thin ring of its skirt, the orange pointer line
// running in from the rim.
void HardwareView::drawKnob(juce::Graphics &g, const Element &e, float value, bool hover)
{
	const auto &v = e.v;
	const auto c = e.centre;
	const bool large = num(v["knob_d"], 10.8f) > 15;
	const float rTop = num(v["knob_d"], 10.8f) / 2, rBase = num(v["base_d"], rTop * 2.15f) / 2;
	const float angle = juce::degreesToRadians(-150.0f + 300.0f * value);
	const auto body = colourOf(v["colour"], juce::Colour(0xff46443f));
	const auto shade = colourOf("knob_skirt_shade", body.darker(0.5f));

	// shadow cast towards the front
	{
		juce::ColourGradient sh(juce::Colours::black.withAlpha(0.42f), c.x, c.y + rBase * 0.25f, juce::Colours::black.withAlpha(0.0f), c.x,
		                        c.y + rBase * 1.45f, true);
		g.setGradientFill(sh);
		g.fillEllipse(c.x - rBase * 1.25f, c.y - rBase * 1.0f, rBase * 2.5f, rBase * 2.6f);
	}
	// the skirt round the top, lit from the rear left
	juce::ColourGradient skirt(body.interpolatedWith(shade, 0.35f), c.x - rBase * 0.7f, c.y - rBase * 0.7f, shade.darker(0.2f), c.x + rBase * 0.7f,
	                           c.y + rBase * 0.7f, false);
	g.setGradientFill(skirt);
	g.fillEllipse(c.x - rBase, c.y - rBase, 2 * rBase, 2 * rBase);
	// the top: satin, a soft highlight at the rear left and a darker front edge
	juce::ColourGradient top(body.brighter(0.18f), c.x - rTop * 0.45f, c.y - rTop * 0.55f, body.darker(0.18f), c.x + rTop * 0.6f, c.y + rTop * 0.8f, true);
	g.setGradientFill(top);
	g.fillEllipse(c.x - rTop, c.y - rTop, 2 * rTop, 2 * rTop);
	g.setColour(juce::Colours::white.withAlpha(0.12f));
	g.drawEllipse(c.x - rTop + 0.12f, c.y - rTop + 0.12f, 2 * rTop - 0.24f, 2 * rTop - 0.24f, 0.18f);
	// the pointer: an orange line from the rim inwards
	{
		const float len = large ? 4.6f : 2.1f, w = large ? 0.6f : 0.5f;
		juce::Path p;
		p.addRoundedRectangle(-w / 2, -rTop + 0.25f, w, len, w * 0.3f);
		p.applyTransform(juce::AffineTransform::rotation(angle).translated(c));
		g.setColour(colourOf(v["pointer_colour"], juce::Colours::orange));
		g.fillPath(p);
	}
	if (hover) {
		g.setColour(juce::Colours::white.withAlpha(0.1f));
		g.fillEllipse(c.x - rBase, c.y - rBase, 2 * rBase, 2 * rBase);
	}
}

// A key from above. The large keys (step keys, CARTRIDGE/ENTER, START, STOP/CONT): a cap with
// sloped sides filling the cut-out, a smaller flat top, the grey ones with the LED lens near its
// rear edge. The small buttons: a smoky translucent cap with a frosted centre, red-pink when the
// LED behind it is lit. Pressed, a key sinks: its top a little smaller and darker.
void HardwareView::drawKey(juce::Graphics &g, const Key &k, bool down, bool hover)
{
	const auto &e = elements()[size_t(k.element)];
	const auto &v = e.v;
	const bool large = v["shape"].toString() == "large";
	const bool ivory = v["key_style"].toString() == "ivory";
	const auto col = colourOf(v["colour"], juce::Colours::grey);
	const auto c = e.centre;
	const float topW = item(v["cap_top"], 0, large ? 13.0f : 6.6f), topH = item(v["cap_top"], 1, topW);
	const float level = k.led >= 0 ? ledLevel(elements()[size_t(k.led)]) : 0.0f;
	if (large) {
		// The cap fills the cut-out but for a dark gap, widest along the front; its sides slope up
		// to a smaller flat top, seen a little towards the front as the photos see it.
		auto base = e.box.reduced(0.3f).withTrimmedBottom(0.45f);
		auto top = juce::Rectangle<float>(topW, topH).withCentre(c.translated(0, 0.45f));
		if (down) {
			top = top.withSizeKeepingCentre(topW * 0.97f, topH * 0.97f).translated(0, 0.2f);
			base = base.reduced(0.1f);
		}
		const float rt = 1.9f;
		const juce::Point<float> b[4] = {base.getTopLeft(), base.getTopRight(), base.getBottomRight(), base.getBottomLeft()};
		const juce::Point<float> t[4] = {top.getTopLeft(), top.getTopRight(), top.getBottomRight(), top.getBottomLeft()};
		const float shadeOf[4] = {1.03f, 0.84f, 0.9f, 0.96f}; // rear, right, front, left: lit from the rear left
		const auto side = colourOf(ivory ? "ivory_cap" : "key_cap", col);
		for (int i = 0; i < 4; ++i) {
			juce::Path q;
			q.startNewSubPath(b[i]);
			q.lineTo(b[(i + 1) % 4]);
			q.lineTo(t[(i + 1) % 4]);
			q.lineTo(t[i]);
			q.closeSubPath();
			const float f = shadeOf[i] * (down ? 0.93f : 1.0f);
			const auto sc = side.withMultipliedBrightness(f);
			// each side darker towards its foot
			const auto foot = (b[i] + b[(i + 1) % 4]) / 2.0f, head = (t[i] + t[(i + 1) % 4]) / 2.0f;
			juce::ColourGradient sg(sc.darker(0.08f), foot.x, foot.y, sc.brighter(0.03f), head.x, head.y, false);
			g.setGradientFill(sg);
			g.fillPath(q);
		}
		auto face = colourOf(ivory ? "ivory_cap" : "key_cap_top", col);
		if (ivory)
			face = face.brighter(0.04f);
		if (down)
			face = face.darker(0.07f);
		juce::ColourGradient fg(face.brighter(0.03f), top.getX(), top.getY(), face.darker(0.05f), top.getRight(), top.getBottom(), false);
		g.setGradientFill(fg);
		g.fillRoundedRectangle(top, rt);
		g.setColour(juce::Colours::black.withAlpha(0.07f));
		g.drawRoundedRectangle(top, rt, 0.15f);
		if (k.led >= 0)
			drawLed(g, elements()[size_t(k.led)], level);
		if (hover) {
			g.setColour(juce::Colours::white.withAlpha(0.14f));
			g.fillRoundedRectangle(top, rt);
		}
		return;
	}
	// The small buttons: a translucent smoky cap in a square cut-out (its gap dark along the right
	// and the front), a frosted window with a bright rim on its top, the centre of which the LED
	// behind it shines through.
	auto cap = e.box;
	if (down)
		cap = cap.reduced(0.12f).translated(0, 0.1f);
	g.setColour(colourOf("key_hole", juce::Colours::black).withAlpha(0.85f));
	g.fillRoundedRectangle(cap.translated(0.35f, 0.4f), 0.5f);
	juce::ColourGradient rim(col.brighter(0.15f), cap.getX(), cap.getY(), col.darker(0.25f), cap.getRight(), cap.getBottom(), false);
	g.setGradientFill(rim);
	g.fillRoundedRectangle(cap, 0.5f);
	const auto top = juce::Rectangle<float>(topW, topH).withCentre(cap.getCentre());
	g.setColour(col.brighter(down ? 0.04f : 0.1f));
	g.fillRoundedRectangle(top, 0.45f);
	const auto *led = k.led >= 0 ? &elements()[size_t(k.led)] : nullptr;
	const float win = led != nullptr ? num(led->v["window"], topW * 0.74f) : topW * 0.74f;
	const auto window = juce::Rectangle<float>(win, win).withCentre(cap.getCentre());
	const auto frost = colourOf("small_btn_centre", col.brighter(0.4f)).withMultipliedBrightness(down ? 0.94f : 1.0f);
	juce::ColourGradient fg(frost.brighter(0.05f), window.getX(), window.getY(), frost.darker(0.1f), window.getRight(), window.getBottom(), false);
	g.setGradientFill(fg);
	g.fillRoundedRectangle(window, 0.35f);
	if (led != nullptr) {
		auto l = *led;
		l.box = l.box.withCentre(window.getCentre()); // it moves with the cap
		drawLed(g, l, level);
	} else {
		// no LED behind it: a plain frosted centre
		g.setColour(colourOf("small_btn_centre_plain", frost.darker(0.15f)));
		g.fillRoundedRectangle(juce::Rectangle<float>(win * 0.62f, win * 0.62f).withCentre(window.getCentre()), 0.3f);
	}
	if (hover) {
		g.setColour(juce::Colours::white.withAlpha(0.14f));
		g.fillRoundedRectangle(cap, 0.5f);
	}
}

void HardwareView::drawLed(juce::Graphics &g, const Element &e, float level)
{
	const auto &v = e.v;
	level = juce::jlimit(0.0f, 1.0f, level);
	const auto shape = v["shape"].toString();
	const auto unlit = colourOf(v["colour"], juce::Colour(0xff3a3a3c)), lit = colourOf(v["colour_lit"], juce::Colours::red);
	const auto b = e.box;
	const auto c = b.getCentre();
	if (shape == "backlight") {
		// the frosted centre of a lit button's cap, the LED's light diffused through it
		g.setColour(unlit);
		g.fillRoundedRectangle(b, 0.4f);
		if (level > 0.02f) {
			juce::ColourGradient glow(lit.withAlpha(0.95f * level), c.x, c.y, lit.withAlpha(0.3f * level), c.x + b.getWidth() * 0.62f, c.y, true);
			g.setGradientFill(glow);
			g.fillRoundedRectangle(b, 0.4f);
			juce::ColourGradient halo(lit.withAlpha(0.28f * level), c.x, c.y, lit.withAlpha(0.0f), c.x + b.getWidth() * 1.1f, c.y, true);
			g.setGradientFill(halo);
			g.fillRoundedRectangle(b.expanded(b.getWidth() * 0.45f), 0.6f);
		}
		return;
	}
	if (level > 0.02f) {
		const float r = std::max(b.getWidth(), b.getHeight()) * (shape == "round" ? 1.3f : 0.75f);
		juce::ColourGradient glow(lit.withAlpha(0.45f * level), c.x, c.y, lit.withAlpha(0.0f), c.x + r * 1.3f, c.y, true);
		g.setGradientFill(glow);
		g.fillEllipse(b.withSizeKeepingCentre(r * 2.6f, r * 2.6f));
	}
	const auto body = unlit.interpolatedWith(lit, level);
	juce::ColourGradient dome(body.brighter(0.25f + 0.4f * level), b.getX(), b.getY(), body.darker(0.45f - 0.3f * level), b.getRight(), b.getBottom(), false);
	g.setGradientFill(dome);
	if (shape == "round") {
		g.fillEllipse(b);
		juce::Path ring;
		ring.addCentredArc(c.x, c.y, b.getWidth() / 2 + 0.1f, b.getHeight() / 2 + 0.1f, 0, kPi * 0.75f, kPi * 1.75f, true);
		g.setColour(juce::Colours::white.withAlpha(0.35f));
		g.strokePath(ring, juce::PathStrokeType(0.22f));
		g.setColour(juce::Colours::white.withAlpha(0.3f + 0.3f * level));
		g.fillEllipse(c.x - b.getWidth() * 0.25f, c.y - b.getHeight() * 0.32f, b.getWidth() * 0.22f, b.getHeight() * 0.18f);
	} else {
		g.fillRoundedRectangle(b, 0.2f);
	}
}

// The MEAS/TEMPO display: the LB-603VK's three slanted digits behind the red filter, the unlit
// segments faintly visible through it.
void HardwareView::drawDisplay(juce::Graphics &g, const Element &e)
{
	const auto d = e.v["digit"];
	const float h = num(d["digit_h"], 14.2f), w = num(d["digit_w"], 8.0f), pitch = num(d["pitch"], 12.7f);
	const float t = num(d["segment_w"], 1.8f), slant = std::tan(juce::degreesToRadians(num(d["slant_deg"], 10.0f)));
	const auto filter = colourOf(e.v["colour"], juce::Colour(0xff4a141b));
	const auto lit = colourOf(e.v["segment_colour"], juce::Colours::red);
	const auto off = filter.interpolatedWith(lit, 0.07f);
	const auto b = e.box;
	juce::Array<float> xs;
	if (auto *a = d["x"].getArray())
		for (const auto &x : *a)
			xs.add(num(x));
	for (int i = 0; i < 3; ++i) {
		const float cx = i < xs.size() ? xs[i] : b.getCentreX() + pitch * float(i - 1);
		const float cy = num(d["y"], b.getCentreY());
		const juce::Rectangle<float> r(cx - w / 2, cy - h / 2, w, h);
		const auto seg = Machine::segments(m_snap.digits[size_t(i)]);
		if (seg != 0) {
			juce::ColourGradient glow(lit.withAlpha(0.22f), cx, cy, lit.withAlpha(0.0f), cx + w, cy, true);
			g.setGradientFill(glow);
			g.fillEllipse(r.expanded(w * 0.4f, h * 0.1f));
		}
		drawDigit(g, r, t, slant, seg, lit, off);
	}
	// the filter's gloss
	juce::ColourGradient gloss(juce::Colours::white.withAlpha(0.07f), 0, b.getY(), juce::Colours::white.withAlpha(0.0f), 0, b.getY() + b.getHeight() * 0.45f, false);
	g.setGradientFill(gloss);
	g.fillRect(b);
}

// ====================================================================== the rear's geometry

namespace {
// The rear face in the view: its top kRearTop mm down (the top plate's back edge and the knobs
// above it), 97 mm tall. panel.json gives the rear's items in top-panel x (seen from behind, left
// and right swap) and y down from the rear face's top edge.
constexpr float kRearTop = 30.0f, kRearH = 97.0f;
const char *const kRearJack[kVoices] = {"rear_out_bassdrum", "rear_out_snare", "rear_out_lowtom", "rear_out_midtom", "rear_out_hitom",
                                        "rear_out_rimshot",  "rear_out_clap",  "rear_out_hihat",  "rear_out_crash",  "rear_out_ride"};
const char *const kOutputName[kVoices] = {"BD", "SD", "LT", "MT", "HT", "RS", "HC", "HH", "Crash", "Ride"};
const char *const kJackTitle[kVoices] = {"BASS DRUM", "SNARE", "LOW TOM", "MID TOM", "HI TOM", "RIM SHOT", "CLAP", "HI HAT", "CRASH", "RIDE"};

juce::var rearItem(const juce::String &id)
{
	if (auto *items = panelJson()["rear_panel"]["items"].getArray())
		for (const auto &it : *items)
			if (it["id"].toString() == id)
				return it;
	return {};
}

// An item's centre in the view, in mm.
juce::Point<float> rearAt(const juce::var &it)
{
	return {HardwareView::kPanelW - num(it["x"]), kRearTop + num(it["y"])};
}

// The MULTI OUT jack of voice v (kVoices: MASTER OUT R, kVoices + 1: L), in mm.
juce::Point<float> rearJack(int v)
{
	return rearAt(rearItem(v < kVoices ? kRearJack[v] : v == kVoices ? "rear_master_out_r" : "rear_master_out_l"));
}

void rearLegend(juce::Graphics &g, const juce::String &text, juce::Point<float> centre, float capH)
{
	drawLegend(g, text, juce::Rectangle<float>(60, capH).withCentre(centre), legendFont("bold", capH), colourOf("print_dark"), false);
}

void rearJackShape(juce::Graphics &g, juce::Point<float> c, float d)
{
	const float r = d / 2;
	g.setColour(juce::Colour(0xff161618));
	g.fillEllipse(c.x - r, c.y - r, d, d);
	g.setColour(juce::Colour(0xff36383c));
	g.fillEllipse(c.x - r * 0.64f, c.y - r * 0.64f, r * 1.28f, r * 1.28f);
	g.setColour(juce::Colour(0xff09090a));
	g.fillEllipse(c.x - r * 0.36f, c.y - r * 0.36f, r * 0.72f, r * 0.72f);
}

void rearDin(juce::Graphics &g, juce::Point<float> c, float w, float h)
{
	g.setColour(juce::Colour(0xff28282b));
	for (float s : {-1.0f, 1.0f})
		g.fillEllipse(c.x + s * w * 0.42f - 1.5f, c.y - 1.5f, 3, 3);
	g.setColour(juce::Colour(0xff161618));
	g.fillEllipse(c.x - h / 2, c.y - h / 2, h, h);
	g.setColour(juce::Colour(0xff2e3034));
	g.fillEllipse(c.x - h * 0.36f, c.y - h * 0.36f, h * 0.72f, h * 0.72f);
	g.setColour(juce::Colour(0xff0a0a0b));
	for (int k = 0; k < 5; ++k) {
		const float a = kPi * (0.95f + 0.275f * float(k));
		g.fillEllipse(c.x + std::cos(a) * h * 0.22f - 0.6f, c.y - std::sin(a) * h * 0.22f - 0.6f, 1.2f, 1.2f);
	}
}

void rearScrew(juce::Graphics &g, float x, float y)
{
	g.setColour(juce::Colour(0xff5c5c5e));
	g.fillEllipse(x - 1.6f, y - 1.6f, 3.2f, 3.2f);
	g.setColour(juce::Colour(0xff1c1c1c));
	g.drawLine(x - 1.1f, y - 1.1f, x + 1.1f, y + 1.1f, 0.5f);
}
} // namespace

// ====================================================================== the bar

void HardwareView::drawBar(juce::Graphics &g)
{
	namespace panel = lemus::panel;
	panel::drawBarBackground(g, m_bar);
	const bool sync = paramValue("sync") > 0.5f;
	panel::drawPill(g, m_bar[m_barSync], sync ? "SYNC: HOST" : "SYNC: OFF", sync, m_hover.kind == HitKind::BarSync, kMono);
	panel::drawPill(g, m_bar[m_barRear], "REAR", m_rear, m_hover.kind == HitKind::BarRear, kMono);
	panel::drawPill(g, m_bar[m_barService], "SERVICE", m_service, m_hover.kind == HitKind::BarService, kMono);
	panel::drawPill(g, m_bar.help(), "?", m_showHelp, m_hover.kind == HitKind::BarHelp, kMono);
	m_bar.setTip(m_barService, m_service ? "Put the panel back" : "Take the panel off: every part on the boards, TM and a scope");

	juce::String status;
	if (m_service) {
		status = serviceStatus();
	} else if (!m_snap.powered) {
		status = "POWER OFF (the rear's POWER switch)";
	} else if (!m_proc.romLoaded()) {
		status = panel::voiceModeStatus("program ROM", true);
	} else {
		static const char *modes[] = {"INT", "MIDI", "DIN", "?"};
		status << "SYNC " << modes[m_snap.syncMode & 3] << (m_snap.synced ? " (HOST)" : "");
		// INT: the firmware's own tempo (before the audio has run, the knob's); MIDI and DIN follow
		// the clock
		if (m_snap.tempo >= 0)
			status << "   " << tempoText();
		else if (m_snap.syncMode == 0)
			status << "   " << m_proc.params().getParameter("tempo")->getCurrentValueAsText();
	}
	juce::StringArray missing;
	for (const auto &[v, name] : {std::pair<Voice, const char *>{Voice::HH, "HI HAT"}, {Voice::CR, "CRASH"}, {Voice::RD, "RIDE"}})
		if (!m_proc.sampleRomLoaded(v))
			missing.add(name);
	if (!missing.isEmpty() && !m_service)
		status << "   no " << missing.joinIntoString("/") << " ROM";
	juce::StringArray cables; // the MULTI OUT jacks with a cable in (voices out of the main mix)
	for (int v = 0; v < kVoices; ++v)
		if (m_proc.cable(Voice(v)))
			cables.add(kOutputName[v]);
	if (m_service) {
	} else if (!cables.isEmpty())
		status << "   MULTI OUT: " << cables.joinIntoString(" ");
	else if (m_rear)
		status << "   MULTI OUT: no cables, all on the main out";
	// A key held by a right-click changes what the others do (SHIFT, CLEAR, INSTRUMENT SELECT).
	const auto held = m_held.latchedNames([this](int i) { return elements()[size_t(m_keys[size_t(i)].element)].label; });
	if (!held.isEmpty() && !m_service)
		status = panel::heldStatus(held);
	// The voices sounding, at the right before ?.
	const float voicesX = m_bar.help().getX() - 330;
	panel::drawStatus(g, m_bar.status(voicesX - 6), status, kMono, held.isEmpty() || m_service ? panel::col::status : panel::col::accent);
	static const char *names[] = {"BD", "SD", "LT", "MT", "HT", "RS", "HC", "HH", "CR", "RD"};
	juce::String voices;
	for (int k = 0; k < kVoices; ++k)
		voices << (m_snap.voices[size_t(k)] > 0.02f ? names[k] : "  ") << " ";
	g.setColour(panel::col::accent);
	g.drawText(voices.trimEnd(), juce::Rectangle<float>(voicesX, float(kPanelHeight), 318, float(kBarHeight)), juce::Justification::centredRight);
	const int tip = m_hover.kind == HitKind::BarSync ? m_barSync : m_hover.kind == HitKind::BarRear ? m_barRear
	              : m_hover.kind == HitKind::BarService ? m_barService : m_hover.kind == HitKind::BarHelp ? panel::Bar::kHelp : -1;
	if (tip != -1 && m_drag.kind == HitKind::None && !m_showHelp)
		panel::drawBarTip(g, m_bar, tip, getLocalBounds().toFloat(), kMono);
}

void HardwareView::drawHelp(juce::Graphics &g)
{
	lemus::panel::drawHelp(g, {0, 0, float(kWidth), float(kPanelHeight)}, "TR-909",
	                       {
	                           "1 2 3 4 5 6 7 8  Q W E R T Y U I   step keys 1-16",
	                           "A S D F  /  G H J  /  K            TRACK PLAY 1-4  /  PATTERN PLAY 1-3  /  EXT INST",
	                           "Z X C V                            TEMPO (STEP), BACK (TAP), FWD (BANK I), AVAILABLE MEAS (BANK II)",
	                           "B N                                CYCLE/GUIDE (LAST MEAS), TAPE SYNC (TEMPO MODE)",
	                           "Return                             CARTRIDGE/ENTER (TOTAL ACCENT)",
	                           "Shift                              SHIFT, held while the computer's Shift is",
	                           "Backspace, Delete                  CLEAR",
	                           "F1 F2 F3 F4                        LAST STEP, SCALE, SHUFFLE/FLAM, INSTRUMENT SELECT",
	                           ",  .                               START, STOP/CONT",
	                           "Space                              START, or STOP/CONT while running",
	                           "",
	                           "SYNC: HOST puts the 909 in MIDI sync and runs it from the DAW's transport (clock, start, stop).",
	                           "REAR: the back panel. A cable in a MULTI OUT jack takes its voice out of the main mix to its own output.",
	                           "POWER, on the rear, switches the 909 off and on (a session always loads switched on).",
	                           "SERVICE: the top panel off, every part on the boards (wheel or pinch: zoom, drag: move); the",
	                           "clap's trimmer TM (SN p.11), the circuit by serial number (SN p.7), a scope on any part's pin.",
	                           "MIDI notes play the voices (36 BD, 38 SD, 43 LT, 47 MT, 50 HT, 37 RS, 39 HC, 42 CH, 46 OH,",
	                           "49 crash, 51 ride; velocity sets the accent), through the firmware as on the unit.",
	                           "MIDI out as the unit's: the EXT INST rows' notes (channel 11) and what comes in; not its own voices.",
	                           "Programming follows the owner's manual: SHIFT + PATTERN PLAY for the write modes, INSTRUMENT",
	                           "SELECT + a step key for the voice, the step keys for the steps (twice for an accent).",
	                       },
	                       kMono);
}

void HardwareView::drawTooltip(juce::Graphics &g)
{
	const Hit &h = m_drag.kind != HitKind::None ? m_drag : m_hover;
	if (h.kind != HitKind::Knob || h.index < 0)
		return;
	const auto &e = elements()[size_t(h.index)];
	const float r = num(e.v["base_d"], 11.6f) / 2;
	lemus::panel::drawTooltip(g, paramText(e.param), {e.centre.x * kPxPerMm, (e.centre.y + r) * kPxPerMm + 6},
	                          {0, 0, float(kWidth), float(kPanelHeight)}, kMono);
}

// ====================================================================== the rear

// The rear panel, still: the face, its jacks and sockets, the legends. The brand mark comes from
// the artwork pack, as on the top.
void HardwareView::renderRear(juce::Graphics &g)
{
	g.fillAll(juce::Colour(0xff1d1e21));
	const auto paint = colourOf("panel");
	// the top plate's back edge
	juce::ColourGradient edge(paint.darker(0.05f), 0, kRearTop - 19, paint.darker(0.2f), 0, kRearTop, false);
	g.setGradientFill(edge);
	g.fillRoundedRectangle(0, kRearTop - 19, kPanelW, 20, 2);
	juce::ColourGradient face(paint.darker(0.04f), 0, kRearTop, paint.darker(0.13f), 0, kRearTop + kRearH, false);
	g.setGradientFill(face);
	g.fillRect(0.0f, kRearTop, kPanelW, kRearH);
	g.setColour(paint.darker(0.32f));
	g.fillRect(0.0f, kRearTop, 1.6f, kRearH);
	g.fillRect(kPanelW - 1.6f, kRearTop, 1.6f, kRearH);
	g.fillRect(0.0f, kRearTop + kRearH - 0.8f, kPanelW, 0.8f);
	g.setColour(juce::Colour(0xff2a2a2c));
	for (float x : {12.0f, kPanelW - 33.0f})
		g.fillRoundedRectangle(x, kRearTop + kRearH, 21, 3.6f, 1.2f);
	for (const auto &[x, y] : {std::pair<float, float>{6, kRearTop + 8}, {kPanelW - 6, kRearTop + 8}, {6, kRearTop + kRearH - 8},
	                           {kPanelW - 6, kRearTop + kRearH - 8}})
		rearScrew(g, x, y);

	const auto ink = colourOf("print_dark");
	auto keyline = [&](float x0, float x1, float y) {
		g.setColour(ink);
		g.fillRect(x0, y - 0.25f, x1 - x0, 0.5f);
	};
	if (auto *items = panelJson()["rear_panel"]["items"].getArray())
		for (const auto &it : *items) {
			const auto id = it["id"].toString();
			const auto c = rearAt(it);
			const float w = num(it["w"]), h = num(it["h"]);
			if (id.startsWith("rear_out_") || id.startsWith("rear_master_out") || id == "rear_start_stop" || id == "rear_trig_out"
			    || id == "rear_save_out" || id == "rear_load_in") {
				rearJackShape(g, c, w);
			} else if (id == "rear_sync_in" || id.startsWith("rear_midi")) {
				rearDin(g, c, w, h);
			} else if (id == "rear_cartridge_slot") {
				g.setColour(juce::Colour(0xff141416));
				g.fillRect(juce::Rectangle<float>(w, h).withCentre(c));
				g.setColour(juce::Colour(0xff222226));
				g.fillRect(juce::Rectangle<float>(w - 4, h - 4).withCentre(c));
			} else if (id == "rear_vent") {
				g.setColour(juce::Colour(0xff26262a));
				for (int k = 0; k < 5; ++k)
					g.fillRoundedRectangle(c.x - 10 + 5 * float(k) - 0.7f, c.y - 9, 1.4f, 18, 0.7f);
			} else if (id == "rear_power_switch") { // the bezel (the rocker: drawRear)
				g.setColour(juce::Colour(0xff1d1d20));
				g.fillRect(juce::Rectangle<float>(w, h).withCentre(c));
			} else if (id == "rear_ac_inlet") {
				g.setColour(juce::Colour(0xff26262a));
				g.fillRoundedRectangle(juce::Rectangle<float>(w, h).withCentre(c), 3);
				g.setColour(juce::Colour(0xff0f0f10));
				g.fillRoundedRectangle(juce::Rectangle<float>(w * 0.55f, h * 0.68f).withCentre(c), 2);
			} else if (id == "rear_logo_roland") {
				// its own place (rear_maker); without a file, an older pack's front Roland outline, in
				// the rear's grey
				const auto box = juce::Rectangle<float>(110, 22).withCentre(c);
				if (const auto rear = m_logos.find("rear_roland"); rear != m_logos.end() && rear->second.picture != nullptr) {
					rear->second.picture->drawWithin(g, box);
				} else if (const auto logo = m_logos.find("roland"); logo != m_logos.end() && !logo->second.outline.isEmpty()) {
					g.setColour(juce::Colour(0xff858a93));
					g.fillPath(logo->second.outline, logo->second.outline.getTransformToScaleToFit(box, true));
				}
			}
		}
	// the legends, laid out round the measured jacks as on the unit
	// (above the jacks, clear of a plug's ring)
	for (int v = 0; v < kVoices; ++v)
		rearLegend(g, kJackTitle[v], rearJack(v).translated(0, -9.6f), 1.9f);
	rearLegend(g, "R(MONO)", rearJack(kVoices).translated(0, -9.6f), 1.9f);
	rearLegend(g, "L", rearJack(kVoices + 1).translated(0, -9.6f), 1.9f);
	const float titleY = rearJack(0).y - 16.5f;
	keyline(rearJack(9).x - 8, rearJack(0).x + 8, titleY + 2.9f);
	rearLegend(g, "MULTI OUT", {(rearJack(9).x + rearJack(0).x) / 2, titleY}, 2.5f);
	keyline(rearJack(kVoices).x - 8, rearJack(kVoices + 1).x + 8, titleY + 2.9f);
	rearLegend(g, "MASTER OUT", {(rearJack(kVoices).x + rearJack(kVoices + 1).x) / 2, titleY}, 2.5f);
	const auto slot = rearAt(rearItem("rear_cartridge_slot"));
	rearLegend(g, "CARTRIDGE", {slot.x + 27, kRearTop + 12}, 2.3f);
	rearLegend(g, "M-64C", {slot.x + 27, kRearTop + 16}, 1.6f);
	const auto at = [](const char *id) { return rearAt(rearItem(id)); };
	rearLegend(g, "SYNC  IN", at("rear_sync_in").translated(0, -11.4f), 1.9f);
	rearLegend(g, "START/STOP", at("rear_start_stop").translated(0, -9.0f), 1.9f);
	rearLegend(g, "TRIG OUT", at("rear_trig_out").translated(0, -11.6f), 1.9f);
	rearLegend(g, "(RIM SHOT)", at("rear_trig_out").translated(0, -8.6f), 1.5f);
	rearLegend(g, "SAVE/OUT", at("rear_save_out").translated(0, -9.0f), 1.9f);
	rearLegend(g, "LOAD/IN", at("rear_load_in").translated(0, -9.0f), 1.9f);
	rearLegend(g, "IN", at("rear_midi_in").translated(0, -11.4f), 1.9f);
	{
		const auto o1 = at("rear_midi_out_1"), o2 = at("rear_midi_out_2");
		const float y = o1.y - 11.2f;
		g.setColour(ink);
		g.fillRect(o1.x, y, o2.x - o1.x, 0.5f);
		g.fillRect(o1.x, y, 0.5f, 1.8f);
		g.fillRect(o2.x - 0.5f, y, 0.5f, 1.8f);
		rearLegend(g, "OUT", {(o1.x + o2.x) / 2, y - 2.4f}, 1.9f);
	}
	const float groupY = at("rear_midi_in").y + 12.5f;
	auto group = [&](float x0, float x1, const char *title) {
		keyline(x0, x1, groupY);
		rearLegend(g, title, {(x0 + x1) / 2, groupY + 3.2f}, 2.3f);
	};
	group(at("rear_sync_in").x - 12, at("rear_trig_out").x + 9, "EXT CONTROL");
	group(at("rear_save_out").x - 9, at("rear_load_in").x + 9, "TAPE MEMORY/SYNC");
	group(at("rear_midi_out_1").x - 14, at("rear_midi_in").x + 14, "MIDI");
	const auto power = at("rear_power_switch");
	rearLegend(g, "POWER", power.translated(0, -13), 2.2f);
	rearLegend(g, "ON", power.translated(-6, -9.2f), 1.5f);
	rearLegend(g, "OFF", power.translated(6, -9.2f), 1.5f);
	rearLegend(g, "AC", at("rear_ac_inlet").translated(0, -16), 2.2f);
}

// The cables: MASTER OUT L and R always (the main pair), and each MULTI OUT jack with one in; each
// hangs to a tag naming the plugin output it reaches. The jack under the mouse is ringed, and a
// note says what plugging it does.
void HardwareView::drawRear(juce::Graphics &g)
{
	struct Cable {
		int v;
		juce::Point<float> from;
		float tx, ty;
	};
	std::vector<Cable> cables;
	const float faceBottom = kRearTop + kRearH;
	float prev = -1e9f;
	std::vector<int> order;
	for (int v = 0; v < kVoices; ++v)
		if (m_proc.cable(Voice(v)))
			order.push_back(v);
	std::sort(order.begin(), order.end(), [](int a, int b) { return rearJack(a).x < rearJack(b).x; });
	for (int v : order) {
		const auto c = rearJack(v);
		const float tx = juce::jmin(kPanelW - 26, juce::jmax(c.x, prev + 48));
		prev = tx;
		cables.push_back({v, c, tx, faceBottom + 60});
	}
	cables.push_back({kVoices, rearJack(kVoices), rearJack(kVoices).x - 22, faceBottom + 44});
	cables.push_back({kVoices + 1, rearJack(kVoices + 1), rearJack(kVoices + 1).x + 22, faceBottom + 44});

	{ // the cables, in mm
		juce::Graphics::ScopedSaveState s(g);
		g.addTransform(juce::AffineTransform::scale(kPxPerMm));
		// the POWER rocker: the side pressed in dark, the other raised and lit from above
		const auto power = rearItem("rear_power_switch");
		const auto pc = rearAt(power);
		const float pw = num(power["w"]) * 0.7f, ph = num(power["h"]) * 0.7f;
		const bool on = m_proc.powered();
		const juce::Rectangle<float> rocker = juce::Rectangle<float>(pw, ph).withCentre(pc);
		const auto onSide = rocker.withWidth(pw / 2), offSide = rocker.withTrimmedLeft(pw / 2); // ON on the left, as printed
		g.setColour(juce::Colour(0xff2a2a2e));
		g.fillRect(on ? onSide : offSide);
		juce::ColourGradient lit(juce::Colour(0xff6a6a70), 0, rocker.getY(), juce::Colour(0xff3e3e43), 0, rocker.getBottom(), false);
		g.setGradientFill(lit);
		g.fillRect(on ? offSide : onSide);
		if (m_hover.kind == HitKind::Power) {
			g.setColour(juce::Colour(0xffff8c1a));
			g.drawRect(juce::Rectangle<float>(num(power["w"]) + 2, num(power["h"]) + 2).withCentre(pc), 0.9f);
		}
		if (m_hover.kind == HitKind::Jack && m_hover.index < kVoices) {
			const auto c = rearJack(m_hover.index);
			g.setColour(juce::Colour(0xffff8c1a));
			g.drawEllipse(c.x - 7.4f, c.y - 7.4f, 14.8f, 14.8f, 1.1f);
		}
		for (const auto &cb : cables) {
			juce::Path p;
			p.startNewSubPath(cb.from.translated(0, 2));
			p.cubicTo(cb.from.x, cb.from.y + 55, cb.tx, cb.ty - 42, cb.tx, cb.ty);
			g.setColour(juce::Colours::black.withAlpha(0.22f));
			g.strokePath(p, juce::PathStrokeType(3.0f), juce::AffineTransform::translation(1.0f, 1.4f));
			g.setColour(juce::Colour(0xff151517));
			g.strokePath(p, juce::PathStrokeType(2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
			g.setColour(juce::Colour(0xff4a4c52));
			g.strokePath(p, juce::PathStrokeType(0.6f), juce::AffineTransform::translation(-0.6f, 0));
			// the plug in the jack, end-on: its knurled ring and the black boot
			const auto c = cb.from;
			g.setColour(juce::Colour(0xff8e9095));
			g.fillEllipse(c.x - 7.2f, c.y - 7.2f, 14.4f, 14.4f);
			g.setColour(juce::Colour(0xffb9bbbf));
			g.fillEllipse(c.x - 5.6f, c.y - 5.6f, 11.2f, 11.2f);
			g.setColour(juce::Colour(0xff161618));
			g.fillEllipse(c.x - 3.9f, c.y - 3.9f, 7.8f, 7.8f);
			// its tag
			const bool main = cb.v >= kVoices;
			const bool on = main || m_proc.outputOn(Voice(cb.v));
			const juce::String text = main ? juce::String(cb.v == kVoices ? "Main R out" : "Main L out")
			                               : juce::String(kOutputName[cb.v]) + (on ? " output" : ": off in DAW");
			const auto colour = main ? juce::Colour(0xff7aa2f7) : on ? juce::Colour(0xffff8c1a) : juce::Colour(0xff9aa4b1);
			const juce::Rectangle<float> tag(cb.tx - 22, cb.ty, 44, 11);
			g.setColour(juce::Colour(0xff1b1d21));
			g.fillRoundedRectangle(tag, 2.4f);
			g.setColour(colour);
			juce::Path outline;
			outline.addRoundedRectangle(tag.reduced(0.3f), 2.4f);
			if (on) {
				g.strokePath(outline, juce::PathStrokeType(0.6f));
			} else {
				juce::Path dashed;
				const float dash[] = {1.6f, 1.0f};
				juce::PathStrokeType(0.6f).createDashedStroke(dashed, outline, dash, 2);
				g.fillPath(dashed);
			}
			g.setFont(fonts::mono(4.4f));
			g.drawText(text, tag, juce::Justification::centred);
		}
	}

	juce::String title = "MULTI OUT",
	             body = "Click a jack to plug a cable in: that voice leaves the main mix and plays on its own plugin output. "
	                    "Click again to pull it out.";
	if (m_hover.kind == HitKind::Power) {
		title = m_proc.powered() ? "POWER: ON" : "POWER: OFF";
		body = m_proc.powered() ? "Click to switch the 909 off: silent, its panel dark. The battery keeps the memory."
		                        : "Click to switch it on: it boots for 0.6 s, and its outputs are muted for about 2 s.";
	} else if (m_hover.kind == HitKind::Jack) {
		const int v = m_hover.index;
		if (v >= kVoices) {
			title = v == kVoices ? "MASTER OUT R" : "MASTER OUT L";
			body = "The main output: always connected. Every voice without a cable in its MULTI OUT jack plays here.";
		} else if (m_proc.cable(Voice(v))) {
			title = juce::String(kJackTitle[v]) + ": cable in";
			body = m_proc.outputOn(Voice(v))
			           ? "Plays only on the plugin's " + juce::String(kOutputName[v]) + " output, out of the main mix. Click to pull the cable out."
			           : "The host has the " + juce::String(kOutputName[v])
			                 + " output off, so this voice is silent. Turn the output on in the DAW, or pull the cable out.";
		} else {
			title = juce::String(kJackTitle[v]) + ": no cable";
			body = "In the main mix. Click to plug a cable in: it then plays on the plugin's " + juce::String(kOutputName[v]) + " output instead.";
		}
	}
	const juce::Rectangle<float> box(812, 352, 372, 104);
	g.setColour(juce::Colour(0xff1b1d21));
	g.fillRoundedRectangle(box, 10);
	g.setColour(juce::Colour(0xff2e343d));
	g.drawRoundedRectangle(box.reduced(0.5f), 10, 1);
	g.setColour(juce::Colour(0xffffb866));
	g.setFont(fonts::mono(13.0f, fonts::Weight::Medium));
	g.drawText(title, box.reduced(16, 12).removeFromTop(18), juce::Justification::centredLeft);
	g.setColour(juce::Colour(0xffc8ced6));
	g.setFont(fonts::mono(12.0f));
	g.drawFittedText(body, box.reduced(16, 12).withTrimmedTop(26).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
}

// ====================================================================== paint

void HardwareView::paint(juce::Graphics &g)
{
	const float phys = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
	if (m_service) {
		paintService(g);
		drawDrawer(g);
		drawBar(g);
		if (m_showHelp)
			drawHelp(g);
		return;
	}
	if (m_rear) {
		if (!m_rearStatic.isValid() || (std::abs(phys - m_rearScale) > 0.01f && juce::Time::getMillisecondCounter() - m_resizedAt >= 200)) {
			m_rearScale = phys;
			m_rearStatic = juce::Image(juce::Image::RGB, juce::roundToInt(kWidth * phys), juce::roundToInt(kPanelHeight * phys), true);
			juce::Graphics sg(m_rearStatic);
			sg.addTransform(juce::AffineTransform::scale(phys * kPxPerMm));
			renderRear(sg);
		}
		g.drawImage(m_rearStatic, juce::Rectangle<float>(0, 0, float(kWidth), float(kPanelHeight)));
		drawRear(g);
		drawBar(g);
		if (m_showHelp)
			drawHelp(g);
		return;
	}
	const bool stale = !m_static.isValid() || (std::abs(phys - m_staticScale) > 0.01f && juce::Time::getMillisecondCounter() - m_resizedAt >= 200);
	if (stale) {
		m_staticScale = phys;
		m_static = juce::Image(juce::Image::RGB, juce::roundToInt(kWidth * phys), juce::roundToInt(kPanelHeight * phys), true);
		{
			juce::Graphics sg(m_static);
			sg.addTransform(juce::AffineTransform::scale(phys * kPxPerMm));
			renderStatic(sg);
		}
		// paint grain: faint per-pixel noise on the light top plate, not on the print
		juce::Image::BitmapData bd(m_static, juce::Image::BitmapData::readWrite);
		juce::Random rng(909);
		for (int y = 0; y < bd.height; ++y)
			for (int x = 0; x < bd.width; ++x) {
				const auto c = bd.getPixelColour(x, y);
				if (c.getBrightness() < 0.75f || c.getSaturation() > 0.12f)
					continue;
				const float n = (rng.nextFloat() - 0.5f) * 0.03f;
				bd.setPixelColour(x, y, n > 0 ? c.brighter(n) : c.darker(-n));
			}
	}
	g.drawImage(m_static, juce::Rectangle<float>(0, 0, float(kWidth), float(kPanelHeight)));
	{
		juce::Graphics::ScopedSaveState s(g);
		g.addTransform(juce::AffineTransform::scale(kPxPerMm));
		const auto &all = elements();
		for (int i : m_leds)
			if (all[size_t(i)].v["in"].isVoid())
				drawLed(g, all[size_t(i)], ledLevel(all[size_t(i)]));
		if (m_display >= 0)
			drawDisplay(g, all[size_t(m_display)]);
		for (size_t i = 0; i < m_keys.size(); ++i)
			drawKey(g, m_keys[i], m_held.down(int(i)), m_hover.kind == HitKind::Key && m_hover.index == int(i));
		for (int i : m_knobs)
			drawKnob(g, all[size_t(i)], paramValue(all[size_t(i)].param),
			         (m_hover.kind == HitKind::Knob && m_hover.index == i) || (m_drag.kind == HitKind::Knob && m_drag.index == i));
	}
	drawBar(g);
	drawTooltip(g);
	if (m_showHelp)
		drawHelp(g);
}

// ====================================================================== mouse

HardwareView::Hit HardwareView::hitAt(juce::Point<float> px) const
{
	if (px.y >= float(kPanelHeight)) {
		const int i = m_bar.hit(px);
		if (i == lemus::panel::Bar::kHelp)
			return {HitKind::BarHelp, -1};
		if (i >= 0)
			return {i == m_barSync ? HitKind::BarSync : i == m_barRear ? HitKind::BarRear : HitKind::BarService, -1};
		return {};
	}
	if (m_service)
		return hitService(px);
	if (m_rear) {
		for (int v = 0; v < kVoices + 2; ++v)
			if (px.getDistanceFrom(rearJack(v) * kPxPerMm) < 20)
				return {HitKind::Jack, v};
		const auto power = rearItem("rear_power_switch");
		if (juce::Rectangle<float>(num(power["w"]) + 4, num(power["h"]) + 4).withCentre(rearAt(power)).contains(px / kPxPerMm))
			return {HitKind::Power, -1};
		return {};
	}
	const auto mm = px / kPxPerMm;
	const auto &all = elements();
	for (int i : m_knobs) {
		const auto &e = all[size_t(i)];
		if (mm.getDistanceFrom(e.centre) < num(e.v["base_d"], 11.6f) / 2 + 0.8f)
			return {HitKind::Knob, i};
	}
	for (size_t i = 0; i < m_keys.size(); ++i)
		if (all[size_t(m_keys[i].element)].box.expanded(0.5f).contains(mm))
			return {HitKind::Key, int(i)};
	return {};
}

void HardwareView::mouseMove(const juce::MouseEvent &e)
{
	const auto h = hitAt(e.position);
	if (h.kind != m_hover.kind || h.index != m_hover.index) {
		m_hover = h;
		setMouseCursor(h.kind == HitKind::None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
		repaint();
	}
}

void HardwareView::mouseExit(const juce::MouseEvent &)
{
	m_hover = {};
	repaint();
}

void HardwareView::mouseDown(const juce::MouseEvent &e)
{
	grabKeyboardFocus();
	if (m_showHelp) { // any click closes the key list
		m_showHelp = false;
		repaint();
		return;
	}
	m_drag = hitAt(e.position);
	m_dragStart = e.position;
	switch (m_drag.kind) {
	case HitKind::Knob: {
		const auto &el = elements()[size_t(m_drag.index)];
		m_dragStartValue = paramValue(el.param);
		if (auto *p = m_proc.params().getParameter(el.param))
			p->beginChangeGesture();
		break;
	}
	case HitKind::Key: {
		// A right-click (ctrl-click on a Mac) latches it, for what takes two keys at once (SHIFT or
		// CLEAR with a step key, INSTRUMENT SELECT with one).
		if (!m_held.mouseDown(m_drag.index, e.mods))
			m_drag = {};
		break;
	}
	case HitKind::BarSync:
		setParamValue("sync", paramValue("sync") > 0.5f ? 0.0f : 1.0f);
		break;
	case HitKind::BarHelp:
		m_showHelp = !m_showHelp;
		break;
	case HitKind::BarRear:
	case HitKind::BarService:
		if (m_drag.kind == HitKind::BarRear) {
			m_rear = !m_rear;
			m_service = false;
		} else {
			m_service = !m_service;
			m_rear = false;
		}
		if (!m_service) { // the scope off with the service view
			m_proc.setProbe(0, "");
			m_proc.setProbe(1, "");
			m_scope.test = false;
		}
		m_hover = {};
		break;
	case HitKind::Scope:
		scopeMenu(m_drag.index);
		m_drag = {};
		break;
	case HitKind::TrimButton: // TEST: the notes' set-up and the clap played, or stop
		if (m_scope.test)
			m_scope.test = false;
		else
			setUpTm();
		m_drag = {};
		break;
	case HitKind::Board:
	case HitKind::None:
		if (m_service) { // drag the boards about
			m_panning = true;
			m_dragStartValue = 0;
			m_drag = {HitKind::Board, -1};
			m_dragPan = m_pan;
		}
		break;
	case HitKind::Serial: {
		const auto serials = boardSerials();
		if (m_drag.index >= 0 && m_drag.index < int(serials.size()))
			m_proc.setSerial(serials[size_t(m_drag.index)]);
		break;
	}
	case HitKind::Trim:
		if (auto *p = m_proc.params().getParameter("tm"))
			p->beginChangeGesture();
		m_dragStartValue = paramValue("tm");
		break;
	case HitKind::Power:
		m_proc.setPower(!m_proc.powered());
		break;
	case HitKind::Jack:
		if (m_drag.index < kVoices)
			m_proc.setCable(Voice(m_drag.index), !m_proc.cable(Voice(m_drag.index)));
		break;
	default:
		break;
	}
	repaint();
}

void HardwareView::mouseDrag(const juce::MouseEvent &e)
{
	if (m_drag.kind == HitKind::Board && m_panning) {
		m_pan = m_dragPan + (e.position - m_dragStart);
		clampPan();
		m_viewChangedAt = juce::Time::getMillisecondCounter();
		repaint();
		return;
	}
	if (m_drag.kind == HitKind::Trim) { // TM: along its track (shift: a tenth as far)
		const float sens = (e.mods.isShiftDown() ? 0.1f : 1.0f) / 268.0f;
		setParamValue("tm", m_dragStartValue + (e.position.x - m_dragStart.x) * sens);
		repaint();
		return;
	}
	if (m_drag.kind != HitKind::Knob)
		return;
	const auto &el = elements()[size_t(m_drag.index)];
	const float sens = e.mods.isShiftDown() ? 0.0008f : 0.004f;
	const float dy = (m_dragStart.y - e.position.y) + (e.position.x - m_dragStart.x) * 0.3f;
	setParamValue(el.param, m_dragStartValue + dy * sens);
	repaint();
}

void HardwareView::mouseUp(const juce::MouseEvent &)
{
	if (m_drag.kind == HitKind::Key) {
		m_held.mouseUp(m_drag.index);
	} else if (m_drag.kind == HitKind::Knob) {
		if (auto *p = m_proc.params().getParameter(elements()[size_t(m_drag.index)].param))
			p->endChangeGesture();
	} else if (m_drag.kind == HitKind::Trim) {
		if (auto *p = m_proc.params().getParameter("tm"))
			p->endChangeGesture();
	}
	if (m_panning) {
		m_panning = false;
		m_viewChangedAt = juce::Time::getMillisecondCounter();
	}
	m_drag = {};
	repaint();
}

void HardwareView::mouseDoubleClick(const juce::MouseEvent &e)
{
	const auto h = hitAt(e.position);
	if (h.kind == HitKind::Knob)
		if (auto *p = m_proc.params().getParameter(elements()[size_t(h.index)].param))
			p->setValueNotifyingHost(p->getDefaultValue());
	if (h.kind == HitKind::Trim)
		if (auto *p = m_proc.params().getParameter("tm"))
			p->setValueNotifyingHost(p->getDefaultValue());
	if (m_service && (h.kind == HitKind::Board || h.kind == HitKind::None) && e.position.y < float(kPanelHeight)) {
		m_zoom = 1; // the whole unit again
		m_pan = {};
		m_viewChangedAt = juce::Time::getMillisecondCounter();
		repaint();
	}
}

void HardwareView::mouseMagnify(const juce::MouseEvent &e, float scale)
{
	if (m_service && e.position.x < float(kWidth) - 304 && e.position.y < float(kPanelHeight))
		zoomService(scale, e.position);
}

void HardwareView::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
	const auto h = hitAt(e.position);
	if (m_service && h.kind != HitKind::Knob) {
		if (h.kind == HitKind::Trim)
			setParamValue("tm", paramValue("tm") + w.deltaY * (e.mods.isShiftDown() ? 0.005f : 0.03f));
		else if (h.kind == HitKind::Board || h.kind == HitKind::None)
			zoomService(std::pow(2.0f, w.deltaY * 2.0f), e.position);
		return;
	}
	if (h.kind != HitKind::Knob)
		return;
	const auto &id = elements()[size_t(h.index)].param;
	setParamValue(id, paramValue(id) + w.deltaY * (e.mods.isShiftDown() ? 0.02f : 0.08f));
}

// ====================================================================== keyboard

namespace {
// The computer keyboard -> a panel key, -1 if none. Space is handled by keyPressed.
int panelKeyFor(const juce::KeyPress &k)
{
	using K = Machine::Key;
	const int code = k.getKeyCode();
	if (code == juce::KeyPress::returnKey)
		return int(K::EnterAccent);
	if (lemus::panel::isClear(k))
		return int(K::Clear);
	if (code >= juce::KeyPress::F1Key && code <= juce::KeyPress::F4Key) {
		static const K f[] = {K::LastStep, K::Scale, K::ShuffleFlam, K::InstSelect};
		return int(f[code - juce::KeyPress::F1Key]);
	}
	const auto c = juce::CharacterFunctions::toUpperCase(juce::juce_wchar(code));
	static const char steps[] = "12345678QWERTYUI";
	for (int i = 0; i < 16; ++i)
		if (c == juce::juce_wchar(steps[i]))
			return int(K::Step1) + i;
	switch (c) {
	case 'A': return int(K::Track1);
	case 'S': return int(K::Track2);
	case 'D': return int(K::Track3);
	case 'F': return int(K::Track4);
	case 'G': return int(K::Pattern1);
	case 'H': return int(K::Pattern2);
	case 'J': return int(K::Pattern3);
	case 'K': return int(K::ExtInst);
	case 'Z': return int(K::Tempo);
	case 'X': return int(K::Back);
	case 'C': return int(K::Fwd);
	case 'V': return int(K::AvailableMeas);
	case 'B': return int(K::Cycle);
	case 'N': return int(K::TapeSync);
	case ',': return int(K::Start);
	case '.': return int(K::StopCont);
	default: return -1;
	}
}
} // namespace

bool HardwareView::keyPressed(const juce::KeyPress &k)
{
	namespace panel = lemus::panel;
	switch (panel::commonKey(k)) {
	case panel::Common::Help:
		m_showHelp = !m_showHelp;
		repaint();
		return true;
	case panel::Common::Escape:
		m_showHelp = false;
		m_held.releaseAll(true);
		repaint();
		return true;
	case panel::Common::None:
		break;
	}
	int key = panelKeyFor(k);
	if (k.getKeyCode() == juce::KeyPress::spaceKey) {
		if (!panel::spaceIsOurs(k))
			return false;
		key = int(m_snap.running ? Machine::Key::StopCont : Machine::Key::Start);
	}
	const int idx = key >= 0 ? keyIndexOf(Machine::Key(key)) : -1;
	if (idx < 0)
		return false;
	m_held.keyDown(k.getKeyCode(), idx);
	return true;
}

bool HardwareView::keyStateChanged(bool)
{
	return m_held.keysChanged();
}

void HardwareView::modifierKeysChanged(const juce::ModifierKeys &m)
{
	// The computer's Shift holds the 909's SHIFT.
	if (m.isShiftDown() == m_shiftHeld)
		return;
	m_shiftHeld = m.isShiftDown();
	const int idx = keyIndexOf(Machine::Key::Shift);
	if (!m_held.latched(idx))
		m_held.set(idx, m_shiftHeld);
}

void HardwareView::focusLost(FocusChangeType)
{
	m_held.focusLost();
	m_shiftHeld = false;
}

} // namespace tr909
