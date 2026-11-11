#include "HardwareView.h"
#include "Fonts.h"

#include <TR808Panel.h>

#include <algorithm>
#include <cmath>

namespace tr808 {

namespace {
const lemus::panel::Mono kMono = [](float size, int weight) { return fonts::mono(size, fonts::Weight(weight)); };
constexpr float kPi = juce::MathConstants<float>::pi;

const juce::var &panelJson()
{
	static const juce::var json = juce::JSON::parse(juce::String::fromUTF8(TR808Panel::panel_json, TR808Panel::panel_jsonSize));
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

juce::Point<float> pointOf(const juce::var &p)
{
	return {num(p[0]), num(p[1])};
}

juce::Path polygon(const juce::var &points, bool closed)
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

// Panel legends: Helvetica's cap height is about 0.717 of the font height.
juce::Font legendFont(const juce::String &spec, float capH)
{
	const bool bold = spec.containsIgnoreCase("bold");
	const bool regular = spec.containsIgnoreCase("regular");
	juce::FontOptions o("Helvetica Neue", capH / 0.717f, bold ? juce::Font::bold : juce::Font::plain);
	if (!bold && !regular)
		o = o.withStyle("Medium");
	return juce::Font(o);
}

// Text as glyph paths, its cap height centred in box (or on a baseline), squeezed or stretched a
// little to the measured width.
void drawLegend(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> box, const juce::Font &font, juce::Colour c,
                bool fitWidth, float baseline = -1)
{
	juce::GlyphArrangement ga;
	ga.addLineOfText(font, text, 0, 0);
	juce::Path p;
	ga.createPath(p);
	const auto bb = p.getBounds();
	if (bb.isEmpty())
		return;
	const float sx = fitWidth ? juce::jlimit(0.75f, 1.3f, box.getWidth() / bb.getWidth()) : 1.0f;
	const float capH = font.getHeight() * 0.717f;
	const float base = baseline >= 0 ? baseline : box.getCentreY() + capH * 0.5f;
	p.applyTransform(juce::AffineTransform::translation(-bb.getCentreX(), 0).scaled(sx, 1.0f).translated(box.getCentreX(), base));
	g.setColour(c);
	g.fillPath(p);
}

// An eighth or quarter note: head centre, stem up of height h.
void noteGlyph(juce::Graphics &g, juce::Point<float> head, float h, bool flag)
{
	const float hw = h * 0.21f, hh = h * 0.15f;
	juce::Path p;
	p.addEllipse(-hw, -hh, 2 * hw, 2 * hh);
	p.applyTransform(juce::AffineTransform::rotation(-0.35f).translated(head));
	const float sx = head.x + hw * 0.9f;
	p.addRectangle(sx - h * 0.035f, head.y - h, h * 0.07f, h);
	if (flag) {
		juce::Path f;
		const float fy = head.y - h;
		f.startNewSubPath(sx, fy);
		f.cubicTo(sx + h * 0.08f, fy + h * 0.15f, sx + h * 0.33f, fy + h * 0.22f, sx + h * 0.22f, fy + h * 0.47f);
		f.cubicTo(sx + h * 0.27f, fy + h * 0.27f, sx + h * 0.06f, fy + h * 0.22f, sx, fy + h * 0.16f);
		f.closeSubPath();
		p.addPath(f);
	}
	g.fillPath(p);
}

float angleOf(float v) { return juce::degreesToRadians(-150.0f + 300.0f * v); }
juce::Point<float> polar(juce::Point<float> c, float r, float a) { return {c.x + r * std::sin(a), c.y - r * std::cos(a)}; }

// A knob seen from above: knurled body, optional coloured insert, the pointer line (panel.json's
// "pointers", by knob style: where it runs from and to, in mm from the centre).
void knobBody(juce::Graphics &g, juce::Point<float> c, float d, float capD, juce::Colour body, juce::Colour cap, juce::Colour pointer,
              float angle, bool hover, const juce::var &style)
{
	const float r = d / 2;
	g.setColour(juce::Colours::black.withAlpha(0.35f));
	g.fillEllipse(c.x - r + 0.5f, c.y - r + 0.8f, d, d); // shadow
	juce::ColourGradient grad(body.brighter(0.1f), c.x - r, c.y - r, body.darker(0.4f), c.x + r, c.y + r, false);
	g.setGradientFill(grad);
	g.fillEllipse(c.x - r, c.y - r, d, d);
	// knurl
	g.setColour(juce::Colours::white.withAlpha(0.07f));
	for (int i = 0; i < 48; ++i) {
		const float a = 2 * kPi * float(i) / 48;
		g.drawLine({polar(c, r * 0.86f, a), polar(c, r * 0.99f, a)}, 0.18f);
	}
	if (capD > 0) {
		const float cr = capD / 2;
		const bool insert = capD < d * 0.6f; // a coloured insert; else the knob's own black top
		juce::ColourGradient cg(cap.brighter(insert ? 0.25f : 0.05f), c.x - cr, c.y - cr, cap.darker(0.25f), c.x + cr, c.y + cr, false);
		g.setGradientFill(cg);
		g.fillEllipse(c.x - cr, c.y - cr, capD, capD);
	}
	const auto spec = panelJson()["pointers"][juce::Identifier(style.toString().isEmpty() ? "n128" : style.toString())];
	g.setColour(pointer);
	g.drawLine({polar(c, num(spec["r_inner"], r * 0.4f), angle), polar(c, num(spec["r_outer"], r * 0.97f), angle)}, num(spec["w"], 0.8f));
	if (hover) {
		g.setColour(juce::Colours::white.withAlpha(0.12f));
		g.fillEllipse(c.x - r, c.y - r, d, d);
	}
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
				e.param = v.hasProperty("param") ? v["param"].toString() : v["param_hint"].toString();
				e.label = v["label"].toString();
				const auto bb = v["bbox"];
				e.box = {num(bb[0]), num(bb[1]), num(bb[2]) - num(bb[0]), num(bb[3]) - num(bb[1])};
				e.centre = {num(v["x"], e.box.getCentreX()), num(v["y"], e.box.getCentreY())};
				e.v = v;
				out.push_back(e);
			}
		return out;
	}();
	return list;
}

HardwareView::HardwareView(TR808Processor &p) : m_proc(p)
{
	setSize(kWidth, kHeight);
	setWantsKeyboardFocus(true);
	setOpaque(true);
	// The brand marks come from the user's artwork (a file per place, or an older tr808.svg pack);
	// without it the panel has none. Their ids are the logo elements' "logo" names.
	for (const auto &mark : p.artwork().marks())
		m_logos[mark.id] = mark;
	m_held.reset(elements().size(), [this](int el, bool down) {
		const int k = keyOf(elements()[size_t(el)]);
		if (k >= 0)
			m_proc.setKey(k, down);
		repaint();
	});
	m_barSync = m_bar.add(lemus::panel::Bar::kSync, "OFF: the 808's own clock. HOST: run from the DAW's transport (DIN sync)");
	m_barRear = m_bar.add(lemus::panel::Bar::kRear, "The rear panel: the MULTI OUT jacks");
	m_barService = m_bar.add(lemus::panel::Bar::kService);
	m_snap = p.snapshot();
	startTimerHz(30);
}

HardwareView::~HardwareView()
{
	releaseAllKeys();
	m_proc.setProbe(0, ""); // the scope goes with the window: the board may sleep again
	m_proc.setProbe(1, "");
}

// ====================================================================== parameters

namespace {
// panel.json's parameter hints -> the processor's ids
juce::String paramId(const juce::String &hint)
{
	if (hint.endsWith("_switch"))
		return hint.upToFirstOccurrenceOf("_", false, false);
	return hint;
}
} // namespace

float HardwareView::paramValue(const juce::String &id) const
{
	if (auto *p = m_proc.params().getParameter(paramId(id)))
		return p->getValue();
	return 0;
}

void HardwareView::setParamValue(const juce::String &id, float v)
{
	if (auto *p = m_proc.params().getParameter(paramId(id)))
		p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, v));
}

int HardwareView::choiceCount(const juce::String &id) const
{
	if (auto *c = dynamic_cast<juce::AudioParameterChoice *>(m_proc.params().getParameter(paramId(id))))
		return c->choices.size();
	return 2;
}

int HardwareView::choiceIndex(const juce::String &id) const
{
	if (auto *c = dynamic_cast<juce::AudioParameterChoice *>(m_proc.params().getParameter(paramId(id))))
		return c->getIndex();
	return paramValue(id) > 0.5f ? 1 : 0;
}

void HardwareView::setChoice(const juce::String &id, int index)
{
	if (auto *c = dynamic_cast<juce::AudioParameterChoice *>(m_proc.params().getParameter(paramId(id)))) {
		index = juce::jlimit(0, c->choices.size() - 1, index);
		if (index != c->getIndex())
			c->setValueNotifyingHost(c->convertTo0to1(float(index)));
	} else {
		setParamValue(id, index > 0 ? 1.0f : 0.0f);
	}
}

juce::String HardwareView::paramText(const juce::String &id) const
{
	if (auto *p = m_proc.params().getParameter(paramId(id)))
		return p->getName(40) + "  " + p->getCurrentValueAsText();
	return {};
}

int HardwareView::keyOf(const Element &e) const
{
	if (e.param.startsWith("step"))
		return TR808Processor::Step1 + e.param.substring(4).getIntValue() - 1;
	if (e.param == "clear")
		return TR808Processor::Clear;
	if (e.param == "start")
		return TR808Processor::StartStop;
	if (e.param == "tap")
		return TR808Processor::Tap;
	return -1;
}

void HardwareView::pressKey(int element, bool down)
{
	if (keyOf(elements()[size_t(element)]) >= 0)
		m_held.set(element, down);
}

void HardwareView::releaseAllKeys()
{
	m_held.releaseAll(true);
}

float HardwareView::ledLevel(const Element &e) const
{
	using L = tr808::Machine::Led;
	if (e.id.startsWith("led_step_"))
		return m_snap.steps[size_t(juce::jlimit(1, 16, e.id.substring(9).getIntValue()) - 1)];
	if (e.id == "led_variation_a")
		return m_snap.leds[size_t(L::VariationA)];
	if (e.id == "led_variation_b")
		return m_snap.leds[size_t(L::VariationB)];
	if (e.id == "led_part_1st")
		return m_snap.leds[size_t(L::FirstPart)];
	if (e.id == "led_part_2nd")
		return m_snap.leds[size_t(L::SecondPart)];
	return 0;
}

void HardwareView::timerCallback()
{
	m_snap = m_proc.snapshot();
	if (m_service) {
		// the boards drawn again for another serial (their parts' values); the scope's sweep, and
		// the clap played every 0.6 s, unaccented, while TM3's procedure runs
		if (m_proc.serial() != m_serialShown) {
			m_serialShown = m_proc.serial();
			m_serviceBase = {};
			m_detailZoom = 0;
		}
		updateScope();
		const uint32_t now = juce::Time::getMillisecondCounter();
		if (m_scope.test && now - m_scope.lastTestMs >= 600) {
			m_scope.lastTestMs = now;
			m_proc.playNote(39, 50); // CP
		}
	}
	repaint();
}

// ====================================================================== static artwork

void HardwareView::drawStaticElement(juce::Graphics &g, const Element &e)
{
	const auto &v = e.v;
	const auto colour = colourOf(v["colour"]);
	if (e.type == "case" || e.type == "region" || e.type == "fill") {
		g.setColour(colour);
		if (v["points"].isArray()) {
			auto p = polygon(v["points"], true);
			if (num(v["corner_r"]) > 0)
				p = p.createPathWithRoundedCorners(num(v["corner_r"]));
			g.fillPath(p);
		} else {
			g.fillRoundedRectangle(e.box, num(v["corner_r"]));
		}
	} else if (e.type == "line") {
		if (v.hasProperty("on"))
			return; // printed on a key cap: drawn with the key
		if (bool(v["with_logo"]) && m_logos.count("rhythm_composer") == 0)
			return; // the rule under "Rhythm Composer" goes with it (its descenders cross the rule)
		auto p = polygon(v["points"], bool(v["closed"]));
		g.setColour(colour);
		g.strokePath(p, juce::PathStrokeType(num(v["stroke"], 0.4f), juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
	} else if (e.type == "outline_box") {
		g.setColour(colour);
		const float s = num(v["stroke"], 0.4f);
		g.drawRoundedRectangle(e.box.reduced(s / 2), num(v["corner_r"]), s);
		bool hasText = false;
		for (const auto &t : elements())
			hasText = hasText || (t.type == "text" && e.box.contains(t.box.getCentre()));
		if (!hasText)
			drawLegend(g, e.label, e.box.reduced(e.box.getWidth() * 0.08f, 0), legendFont(v["font"].toString(), e.box.getHeight() * 0.45f),
			           colour, true);
	} else if (e.type == "label_box") {
		g.setColour(colour);
		g.fillRoundedRectangle(e.box, num(v["corner_r"]));
		const auto tc = colourOf(v["text_colour"], juce::Colours::black);
		const auto tb = v["text_bbox"];
		const juce::Rectangle<float> textBox = tb.isArray() ? juce::Rectangle<float>(num(tb[0]), num(tb[1]), num(tb[2]) - num(tb[0]), num(tb[3]) - num(tb[1]))
		                                                    : e.box.reduced(e.box.getWidth() * 0.1f, 0);
		if (auto *spans = v["spans"].getArray()) {
			// small caps: the code letters full height, the rest small capitals, on one baseline
			const float big = num(v["cap_h_large"], 4.0f), small = num(v["cap_h_small"], 2.0f);
			const float base = num(v["baseline_y"], textBox.getBottom());
			juce::Path p;
			float x = 0;
			for (const auto &s : *spans) {
				const bool large = s["size"].toString() == "large";
				juce::GlyphArrangement ga;
				ga.addLineOfText(legendFont("medium", large ? big : small), s["text"].toString(), x, 0);
				juce::Path sp;
				ga.createPath(sp);
				p.addPath(sp);
				x = ga.getBoundingBox(0, -1, true).getRight() + 0.15f;
			}
			const auto bb = p.getBounds();
			if (!bb.isEmpty()) {
				const float sx = juce::jlimit(0.7f, 1.3f, textBox.getWidth() / bb.getWidth());
				p.applyTransform(juce::AffineTransform::translation(-bb.getX(), 0).scaled(sx, 1).translated(textBox.getX(), base));
				g.setColour(tc);
				g.fillPath(p);
			}
		} else {
			const float capH = num(v["cap_h"], textBox.getHeight() * 0.7f);
			drawLegend(g, e.label, textBox, legendFont(v["font"].toString(), capH), tc, e.label.length() > 2);
		}
	} else if (e.type == "text") {
		if (v.hasProperty("on"))
			return; // printed on a key cap: drawn with the key
		drawLegend(g, e.label, e.box, legendFont(v["font"].toString(), num(v["cap_h"], e.box.getHeight())), colour, e.label.length() > 1);
	} else if (e.type == "symbol") {
		g.setColour(colour);
		const auto &label = e.label;
		if (label == "screw") {
			const float r = num(v["d"], 6) / 2;
			const auto c = e.centre;
			juce::ColourGradient sg(colour.brighter(0.28f), c.x - r, c.y - r, colour.darker(0.5f), c.x + r, c.y + r, false);
			g.setGradientFill(sg);
			g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
			g.setColour(juce::Colours::black.withAlpha(0.7f));
			g.drawLine(c.x - r * 0.55f, c.y, c.x + r * 0.55f, c.y, r * 0.18f);
			g.drawLine(c.x, c.y - r * 0.55f, c.x, c.y + r * 0.55f, r * 0.18f);
		} else if (label == "dot") {
			const float d = num(v["d"], e.box.getWidth());
			g.fillEllipse(e.centre.x - d / 2, e.centre.y - d / 2, d, d);
		} else if (label == "eighth_note" || label == "quarter_note") {
			const auto head = v["head"].isArray() ? pointOf(v["head"]) : juce::Point<float>(e.box.getX() + e.box.getWidth() * 0.3f, e.box.getBottom() - 0.7f);
			noteGlyph(g, head, e.box.getHeight() * 0.82f, label == "eighth_note");
		} else if (v["points"].isArray()) {
			g.fillPath(polygon(v["points"], true));
		} else if (label.startsWith("button_pressed")) {
			g.fillRect(e.box.getX(), e.box.getBottom() - e.box.getHeight() * 0.25f, e.box.getWidth(), e.box.getHeight() * 0.25f);
			g.fillRect(e.box.getX() + e.box.getWidth() * 0.2f, e.box.getY() + e.box.getHeight() * 0.35f, e.box.getWidth() * 0.6f, e.box.getHeight() * 0.3f);
		} else if (label.startsWith("button_raised")) {
			g.fillRect(e.box.getX(), e.box.getBottom() - e.box.getHeight() * 0.15f, e.box.getWidth(), e.box.getHeight() * 0.15f);
			g.fillRect(e.box.getX() + e.box.getWidth() * 0.2f, e.box.getY(), e.box.getWidth() * 0.6f, e.box.getHeight() * 0.8f);
		} else {
			g.fillRect(e.box);
		}
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
		// the scale: ticks or dots, and a LEVEL knob's orange "standard" dot
		const auto sc = v["scale"];
		const auto c = e.centre;
		g.setColour(colourOf(sc["colour"], juce::Colours::white));
		const int n = int(num(sc["count"], 11));
		const float step = juce::degreesToRadians(num(sc["step_deg"], 30));
		if (sc["type"].toString() == "ticks") {
			for (int i = 0; i < n; ++i) {
				const float a = juce::degreesToRadians(-150.0f) + step * float(i);
				g.drawLine({polar(c, num(sc["r_inner"]), a), polar(c, num(sc["r_outer"]), a)}, num(sc["tick_w"], 0.4f));
			}
		} else if (auto *pts = sc["points"].getArray()) {
			for (int i = 0; i < pts->size(); ++i) {
				const auto q = pointOf((*pts)[i]);
				const float d = i == pts->size() / 2 ? num(sc["top_dot_d"], 1.2f) : num(sc["dot_d"], 0.9f);
				g.fillEllipse(q.x - d / 2, q.y - d / 2, d, d);
			}
		}
		const auto sd = v["standard_dot"];
		if (sd.isObject()) {
			g.setColour(colourOf(sd["colour"]));
			const auto q = polar(c, num(sd["r"]), juce::degreesToRadians(num(sd["angle_deg"])));
			const float d = num(sd["d"], 1.2f);
			g.fillEllipse(q.x - d / 2, q.y - d / 2, d, d);
		}
	} else if (e.type == "rotary_switch") {
		g.setColour(colourOf("print_white"));
		if (auto *pos = v["positions"].getArray())
			for (const auto &p : *pos)
				if (p["dot"].isArray() && !p["number"].isArray()) { // a numbered position has no dot printed
					const auto q = pointOf(p["dot"]);
					g.fillEllipse(q.x - 0.5f, q.y - 0.5f, 1.0f, 1.0f);
				}
	} else if (e.type == "dial") {
		const auto c = e.centre;
		const float ro = num(v["ring_outer_d"]) / 2, ri = num(v["ring_inner_d"]) / 2;
		juce::Path ring;
		ring.addEllipse(c.x - ro, c.y - ro, 2 * ro, 2 * ro);
		ring.addEllipse(c.x - ri, c.y - ri, 2 * ri, 2 * ri);
		ring.setUsingNonZeroWinding(false);
		g.setColour(colourOf(v["ring_colour"]));
		g.fillPath(ring);
		if (auto *digits = v["digits"].getArray())
			for (const auto &d : *digits) {
				const auto bb = d["bbox"];
				const juce::Rectangle<float> b(num(bb[0]), num(bb[1]), num(bb[2]) - num(bb[0]), num(bb[3]) - num(bb[1]));
				drawLegend(g, d["label"].toString(), b, legendFont("medium", num(d["cap_h"], 2.9f)), colourOf(v["digit_colour"]), false);
			}
		const auto dots = v["dots"];
		g.setColour(colourOf(dots["colour"], juce::Colours::white));
		const int n = int(num(dots["count"], 41));
		for (int i = 0; i < n; ++i) {
			const auto q = polar(c, num(dots["r"]), juce::degreesToRadians(num(dots["start_deg"], -150) + num(dots["step_deg"], 7.5f) * float(i)));
			const float d = num(dots["d"], 0.8f);
			g.fillEllipse(q.x - d / 2, q.y - d / 2, d, d);
		}
	} else if (e.type == "slide_switch" || e.type == "lever_switch") {
		g.setColour(colourOf(v["slot_colour"], juce::Colours::black));
		g.fillRoundedRectangle(e.box, num(v["corner_r"]));
		g.setColour(juce::Colours::white.withAlpha(0.08f));
		g.drawRoundedRectangle(e.box, num(v["corner_r"]), 0.25f);
	} else if (e.type == "key") {
		const auto cb = v["cutout_bbox"];
		if (cb.isArray()) {
			const juce::Rectangle<float> b(num(cb[0]), num(cb[1]), num(cb[2]) - num(cb[0]), num(cb[3]) - num(cb[1]));
			g.setColour(colourOf("key_window", juce::Colours::black));
			g.fillRoundedRectangle(b, 1.0f);
		}
		if (v["shape"].toString() == "round") {
			const float r = num(v["d"]) / 2;
			g.setColour(colourOf(v["bezel_colour"], juce::Colours::grey));
			g.fillEllipse(e.centre.x - r, e.centre.y - r, 2 * r, 2 * r);
			g.setColour(colourOf("bezel_edge", juce::Colours::grey));
			g.drawEllipse(e.centre.x - r, e.centre.y - r, 2 * r, 2 * r, 0.25f);
		}
	}
}

// The POWER push button, in its bezel (drawn with the panel): raised when off (a shadow along its
// right and bottom sides), pressed in when on (a little darker).
void HardwareView::drawPower(juce::Graphics &g, const Element &e, bool on, bool hover)
{
	auto face = colourOf(e.v["colour"], juce::Colours::darkgrey);
	if (hover)
		face = face.brighter(0.15f);
	if (on) {
		g.setColour(face.darker(0.25f));
		g.fillRoundedRectangle(e.box.reduced(0.3f), 0.6f);
		return;
	}
	g.setColour(juce::Colours::black.withAlpha(0.7f));
	g.fillRoundedRectangle(e.box.translated(0.7f, 0.9f), 0.6f);
	g.setColour(face);
	g.fillRoundedRectangle(e.box, 0.6f);
}

void HardwareView::renderStatic(juce::Graphics &g)
{
	g.fillAll(juce::Colour(0xff111110));
	const auto &all = elements();
	for (const char *type : {"case", "region", "fill", "line", "outline_box", "label_box", "text", "symbol", "logo", "knob", "rotary_switch", "dial",
	                         "slide_switch", "lever_switch", "key"})
		for (const auto &e : all)
			if (e.type == type)
				drawStaticElement(g, e);
	// a faint sheen over the upper panel
	juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.04f), 0, 0, juce::Colours::black.withAlpha(0.06f), 0, 190, false);
	g.setGradientFill(sheen);
	g.fillRect(20.0f, 4.0f, 468.0f, 186.0f);
}

// ====================================================================== moving parts

void HardwareView::drawKnob(juce::Graphics &g, const Element &e, float value, bool hover)
{
	const auto &v = e.v;
	const auto body = colourOf(v["colour"], juce::Colours::black);
	const float capD = num(v["cap_d"], 0);
	knobBody(g, e.centre, num(v["knob_d"], 11), capD, body, colourOf(v["cap_colour"], body), capD > 0 ? juce::Colour(0xff111111) : colourOf(v["pointer_colour"], juce::Colours::red),
	         angleOf(value), hover, v["knob_style"]);
}

void HardwareView::drawRotary(juce::Graphics &g, const Element &e, int position, bool hover)
{
	const auto &v = e.v;
	float angle = 0;
	if (auto *pos = v["positions"].getArray())
		angle = juce::degreesToRadians(num((*pos)[juce::jlimit(0, pos->size() - 1, position)]["angle_deg"]));
	knobBody(g, e.centre, num(v["knob_d"], 21), num(v["cap_d"], 15), colourOf(v["colour"], juce::Colours::black), colourOf(v["colour"], juce::Colours::black).brighter(0.04f),
	         colourOf(v["pointer_colour"], juce::Colours::red), angle, hover, v["knob_style"]);
}

void HardwareView::drawDial(juce::Graphics &g, const Element &e, float value, bool hover)
{
	const auto k = e.v["knob"];
	knobBody(g, e.centre, num(k["knob_d"], 40), num(k["cap_d"], 32), colourOf(k["colour"], juce::Colours::black), colourOf(k["colour"], juce::Colours::black).brighter(0.04f),
	         colourOf(k["pointer_colour"], juce::Colours::red), angleOf(value), hover, k["knob_style"]);
}

void HardwareView::drawSlide(juce::Graphics &g, const Element &e, int position, bool hover)
{
	const auto &v = e.v;
	auto *pos = v["positions"].getArray();
	if (pos == nullptr || pos->isEmpty())
		return;
	const float off = num((*pos)[juce::jlimit(0, pos->size() - 1, position)]["offset"]);
	const auto s = v["slider"];
	const bool vertical = v["orientation"].toString() != "horizontal";
	const float w = num(s["w"], 3), h = num(s["h"], 6);
	const auto c = vertical ? e.centre.translated(0, off) : e.centre.translated(off, 0);
	const juce::Rectangle<float> r(c.x - w / 2, c.y - h / 2, w, h);
	const auto col = colourOf(s["colour"], juce::Colours::black);
	juce::ColourGradient grad(col.brighter(0.14f), r.getX(), r.getY(), col.darker(0.3f), r.getRight(), r.getBottom(), false);
	g.setGradientFill(grad);
	g.fillRoundedRectangle(r, 0.6f);
	g.setColour(juce::Colours::white.withAlpha(0.14f));
	const int ridges = int(num(s["ridges"], 6));
	for (int i = 0; i < ridges; ++i) {
		const float t = (float(i) + 0.5f) / float(ridges);
		if (vertical)
			g.drawLine(r.getX() + 0.5f, r.getY() + r.getHeight() * t, r.getRight() - 0.5f, r.getY() + r.getHeight() * t, 0.2f);
		else
			g.drawLine(r.getX() + r.getWidth() * t, r.getY() + 0.5f, r.getX() + r.getWidth() * t, r.getBottom() - 0.5f, 0.2f);
	}
	if (hover) {
		g.setColour(juce::Colours::white.withAlpha(0.15f));
		g.fillRoundedRectangle(r, 0.6f);
	}
}

void HardwareView::drawLever(juce::Graphics &g, const Element &e, int position, bool hover)
{
	const auto &v = e.v;
	auto *pos = v["positions"].getArray();
	if (pos == nullptr || pos->isEmpty())
		return;
	const float dx = num((*pos)[juce::jlimit(0, pos->size() - 1, position)]["dx"]);
	const float d = num(v["lever"]["d"], 5);
	const auto c = e.centre.translated(dx, 0);
	g.setColour(juce::Colour(0xff8c8c88));
	g.drawLine(e.centre.x + dx * 0.3f, e.centre.y, c.x, c.y, 1.2f); // the stem, leaning
	juce::ColourGradient chrome(juce::Colour(0xfff2f2ee), c.x - d / 2, c.y - d / 2, juce::Colour(0xff6a6a66), c.x + d / 2, c.y + d / 2, true);
	g.setGradientFill(chrome);
	g.fillEllipse(c.x - d / 2, c.y - d / 2, d, d);
	g.setColour(juce::Colours::black.withAlpha(0.4f));
	g.drawEllipse(c.x - d / 2, c.y - d / 2, d, d, 0.2f);
	if (hover) {
		g.setColour(juce::Colours::white.withAlpha(0.18f));
		g.fillEllipse(c.x - d / 2, c.y - d / 2, d, d);
	}
}

void HardwareView::drawKey(juce::Graphics &g, const Element &e, bool down, float /*led*/, bool hover)
{
	const auto &v = e.v;
	const juce::String shape = v["shape"].toString();
	const auto col = colourOf(v["colour"], juce::Colours::grey);
	if (shape == "round") {
		const float r = num(v["cap_d"], 6) / 2 * (down ? 0.94f : 1.0f);
		const auto c = e.centre;
		juce::ColourGradient grad(col.brighter(down ? 0.0f : 0.3f), c.x - r, c.y - r, col.darker(0.4f), c.x + r, c.y + r, false);
		g.setGradientFill(grad);
		g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
		if (hover) {
			g.setColour(juce::Colours::white.withAlpha(0.15f));
			g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
		}
		return;
	}
	auto r = e.box;
	if (down)
		r = r.reduced(0.25f).translated(0, 0.35f);
	// a wedge key: the flat rear (with the LED), then the face sloping down to the front
	g.setColour(juce::Colours::black.withAlpha(0.45f));
	g.fillRoundedRectangle(r.translated(0.4f, 0.7f), 0.8f);
	if (shape == "wedge") {
		// from the rear: the flat top with the LED, a steeper band, the long front face between its
		// drafted sides; the whole key a little narrower at the front
		const auto w = v["wedge"];
		const float inset = num(w["front_inset"], 0.4f), sideW = num(w["side_w"], 0.9f);
		const float y0 = r.getY(), y1 = y0 + num(w["top_face_h"], 6.4f), y2 = y1 + num(w["band_h"], 2.8f), y3 = r.getBottom() - 0.3f, y4 = r.getBottom();
		const auto in = [&](float y) { return inset * (y - y0) / (y4 - y0); };
		const auto poly = [](std::initializer_list<juce::Point<float>> pts) {
			juce::Path p;
			bool first = true;
			for (const auto q : pts) {
				if (first)
					p.startNewSubPath(q);
				else
					p.lineTo(q);
				first = false;
			}
			p.closeSubPath();
			return p;
		};
		const float l = r.getX(), rt = r.getRight();
		const auto outline = poly({{l, y0}, {rt, y0}, {rt - in(y4), y4}, {l + in(y4), y4}}).createPathWithRoundedCorners(0.6f);
		juce::Graphics::ScopedSaveState clip(g);
		g.reduceClipRegion(outline);
		g.setColour(col.darker(0.22f)); // the sides, and the thin front edge below the face
		g.fillPath(outline);
		g.setColour(col.brighter(down ? 0.0f : 0.05f));
		g.fillRect(l, y0, rt - l, y1 - y0);
		const juce::Point<float> band1l(l + in(y1), y1), band1r(rt - in(y1), y1), band2l(l + in(y2) + sideW, y2), band2r(rt - in(y2) - sideW, y2);
		g.setColour(col.brighter(down ? 0.03f : 0.09f));
		g.fillPath(poly({band1l, band1r, band2r, band2l}));
		juce::ColourGradient face(col, 0, y2, col.brighter(0.05f), 0, y3, false);
		g.setGradientFill(face);
		g.fillPath(poly({band2l, band2r, {rt - in(y3) - sideW, y3}, {l + in(y3) + sideW, y3}}));
		g.setColour(col.brighter(0.5f).withAlpha(0.5f)); // the two creases catching the light
		g.drawLine({band1l, band1r}, 0.22f);
		g.drawLine({band2l, band2r}, 0.22f);
		const auto led = v["led"];
		const float recess = num(w["led_recess_d"], 3.6f);
		const auto c = e.centre.translated(num(led["dx"]), num(led["dy"]));
		g.setColour(col.darker(1.4f)); // the LED's hole
		g.fillEllipse(c.x - recess / 2, c.y - recess / 2, recess, recess);
	} else {
		// a large cap: the flat top inside four sides sloping down to the panel
		const auto tf = v["top_face"];
		const auto top = juce::Rectangle<float>(num(tf[0], r.getWidth()), num(tf[1], r.getHeight())).withCentre(r.getCentre());
		const auto side = [&](juce::Point<float> a, juce::Point<float> b, juce::Point<float> c, juce::Point<float> d, float shade) {
			juce::Path p;
			p.addQuadrilateral(a.x, a.y, b.x, b.y, c.x, c.y, d.x, d.y);
			g.setColour(col.darker(shade));
			g.fillPath(p);
		};
		side(r.getTopLeft(), r.getTopRight(), top.getTopRight(), top.getTopLeft(), 0.22f);
		side(r.getTopRight(), r.getBottomRight(), top.getBottomRight(), top.getTopRight(), 0.38f);
		side(r.getBottomRight(), r.getBottomLeft(), top.getBottomLeft(), top.getBottomRight(), 0.55f);
		side(r.getBottomLeft(), r.getTopLeft(), top.getTopLeft(), top.getBottomLeft(), 0.3f);
		g.setColour(col.darker(down ? 0.04f : 0.0f));
		g.fillRoundedRectangle(top, 0.4f);
		g.setColour(col.brighter(0.35f).withAlpha(0.55f)); // the moulded edge catching the light
		g.drawRoundedRectangle(top, 0.4f, 0.18f);
	}
	// legends printed on the cap
	const auto capMove = r.getPosition() - e.box.getPosition();
	for (const auto &t : elements()) {
		if (t.v["on"].toString() != e.id)
			continue;
		if (t.type == "text") {
			drawLegend(g, t.label, t.box + capMove, legendFont(t.v["font"].toString(), num(t.v["cap_h"], 2.7f)), colourOf(t.v["colour"]), t.label.length() > 1);
		} else if (t.type == "line") {
			auto rule = polygon(t.v["points"], false);
			rule.applyTransform(juce::AffineTransform::translation(capMove));
			g.setColour(colourOf(t.v["colour"]));
			g.strokePath(rule, juce::PathStrokeType(num(t.v["stroke"], 0.3f), juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
		}
	}
	if (hover) {
		g.setColour(juce::Colours::white.withAlpha(0.12f));
		g.fillRoundedRectangle(r, 0.8f);
	}
}

void HardwareView::drawLed(juce::Graphics &g, const Element &e, float level)
{
	const float d = num(e.v["d"], 3);
	const auto c = e.centre;
	const auto off = colourOf(e.v["colour"], juce::Colour(0xff3a1010)), on = colourOf(e.v["colour_lit"], juce::Colours::red);
	level = juce::jlimit(0.0f, 1.0f, level);
	if (level > 0.02f) {
		juce::ColourGradient glow(on.withAlpha(0.5f * level), c.x, c.y, on.withAlpha(0.0f), c.x + d * 1.6f, c.y, true);
		g.setGradientFill(glow);
		g.fillEllipse(c.x - d * 1.6f, c.y - d * 1.6f, d * 3.2f, d * 3.2f);
	}
	g.setColour(off.interpolatedWith(on, level));
	g.fillEllipse(c.x - d / 2, c.y - d / 2, d, d);
	g.setColour(juce::Colours::white.withAlpha(0.25f + 0.35f * level));
	g.fillEllipse(c.x - d * 0.22f, c.y - d * 0.3f, d * 0.22f, d * 0.18f);
}

// ====================================================================== the rear

namespace {
// The rear face in the view: its top edge kRearTop mm down. panel.json gives the rear's items in
// top-panel x (seen from behind, left and right swap) and y down from the face's top edge.
constexpr float kRearTop = 56.0f;
const char *const kRearJack[kVoices] = {"rear_out_bd", "rear_out_sd", "rear_out_lt", "rear_out_mt", "rear_out_ht", "rear_out_rs",
                                        "rear_out_cp", "rear_out_cb", "rear_out_cy", "rear_out_oh", "rear_out_ch"};
const char *const kOutputName[kVoices] = {"BD", "SD", "LT/LC", "MT/MC", "HT/HC", "RS/CL", "CP/MA", "CB", "CY", "OH", "CH"};
const char *const kJackTitle[kVoices] = {"BASS DRUM", "SNARE DRUM", "LOW TOM / CONGA", "MID TOM / CONGA", "HI TOM / CONGA", "RIM SHOT / CLAVES",
                                         "HAND CLAP / MARACAS", "COW BELL", "CYMBAL", "OPEN HIHAT", "CLOSED HIHAT"};

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

// The MULTI OUT jack of voice v (kVoices: MASTER OUT HI, kVoices + 1: LO), in mm.
juce::Point<float> rearJack(int v)
{
	return rearAt(rearItem(v < kVoices ? kRearJack[v] : v == kVoices ? "rear_master_out_hi" : "rear_master_out_lo"));
}

void rearLegend(juce::Graphics &g, const juce::String &text, juce::Point<float> centre, float capH, float fitW = 0)
{
	drawLegend(g, text, juce::Rectangle<float>(fitW > 0 ? fitW : 60, capH).withCentre(centre), legendFont("bold", capH), colourOf("print_grey"), fitW > 0);
}

// A 6.3 mm jack: its shadow, the chrome nut (a softened hexagon), the step inside it, the bush, the hole.
void rearJackShape(juce::Graphics &g, juce::Point<float> c, float d)
{
	const float r = d / 2;
	g.setColour(juce::Colours::black.withAlpha(0.55f));
	g.fillEllipse(c.x - r - 0.8f, c.y - r - 0.8f, 2 * r + 1.6f, 2 * r + 1.6f);
	juce::Path nut;
	nut.addPolygon(c, 6, r * 1.03f, kPi / 6);
	juce::ColourGradient chrome(juce::Colour(0xffdcdcd8), c.x - r, c.y - r, juce::Colour(0xff85857f), c.x + r, c.y + r, false);
	g.setGradientFill(chrome);
	g.fillPath(nut.createPathWithRoundedCorners(3.0f));
	g.setColour(juce::Colour(0xff55554f));
	g.fillEllipse(c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);
	g.setColour(juce::Colour(0xffcdcdc8));
	g.fillEllipse(c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f);
	g.setColour(juce::Colour(0xff090909));
	g.fillEllipse(c.x - r * 0.4f, c.y - r * 0.4f, r * 0.8f, r * 0.8f);
}

void rearScrew(juce::Graphics &g, float x, float y, float r, bool plated = false)
{
	juce::ColourGradient head(juce::Colour(plated ? 0xffc9c9c4 : 0xff3a3a38), x - r, y - r, juce::Colour(plated ? 0xff5e5e59 : 0xff0d0d0c), x + r, y + r, false);
	g.setGradientFill(head);
	g.fillEllipse(x - r, y - r, 2 * r, 2 * r);
	g.setColour(juce::Colour(plated ? 0xff23231f : 0xff040404));
	g.drawLine(x - r * 0.6f, y, x + r * 0.6f, y, r * 0.24f);
	g.drawLine(x, y - r * 0.6f, x, y + r * 0.6f, r * 0.24f);
}

// The SYNC socket: its flange's two screws, the metal shell, the five contacts in their arc.
void rearDin(juce::Graphics &g, juce::Point<float> c, float w, float h)
{
	for (float s : {-1.0f, 1.0f})
		rearScrew(g, c.x + s * w * 0.42f, c.y, 3.0f, true);
	juce::ColourGradient shell(juce::Colour(0xffb9b9b4), c.x - h / 2, c.y - h / 2, juce::Colour(0xff6c6c67), c.x + h / 2, c.y + h / 2, false);
	g.setGradientFill(shell);
	g.fillEllipse(c.x - h / 2, c.y - h / 2, h, h);
	g.setColour(juce::Colour(0xff363634));
	g.fillEllipse(c.x - h * 0.42f, c.y - h * 0.42f, h * 0.84f, h * 0.84f);
	g.setColour(juce::Colour(0xff050505));
	for (int k = 0; k < 5; ++k) {
		const float a = kPi * (0.95f + 0.275f * float(k));
		g.fillEllipse(c.x + std::cos(a) * h * 0.27f - 0.8f, c.y + std::sin(a) * h * 0.27f - 0.8f, 1.6f, 1.6f);
	}
}
} // namespace

// The rear panel, still: the face, its jacks and sockets, the ruled groups and their legends.
void HardwareView::renderRear(juce::Graphics &g)
{
	const auto layout = panelJson()["rear_panel"]["layout"];
	const float faceX = num(layout["face_x"]), faceH = num(layout["face_h"]), lipH = num(layout["lip_h"]);
	const float top = kRearTop, bottom = top + faceH;
	g.fillAll(juce::Colour(0xff111110));
	g.setColour(juce::Colour(0xff0a0a09));
	for (float x : {52.0f, kPanelW - 76.0f})
		g.fillRoundedRectangle(x, bottom + lipH, 24, 12, 2);
	g.setColour(colourOf("case_black"));
	g.fillRect(faceX - 2, bottom, kPanelW - 2 * (faceX - 2), lipH);
	juce::ColourGradient face(juce::Colour(0xff3a3a38), 0, top, juce::Colour(0xff1d1d1b), 0, bottom, false);
	face.addColour(0.32, juce::Colour(0xff2b2b29));
	g.setGradientFill(face);
	g.fillRect(faceX, top, kPanelW - 2 * faceX, faceH);
	for (bool right : {false, true}) { // the cheeks, their outer edges slanting
		const auto x = [&](float v) { return right ? kPanelW - v : v; };
		juce::Path cheek;
		cheek.addQuadrilateral(x(10), top - 1.2f, x(faceX), top - 1.2f, x(faceX), bottom + 1, x(0), bottom + 1);
		g.setColour(colourOf("cheek"));
		g.fillPath(cheek);
		g.setColour(colourOf("cheek_edge"));
		g.drawLine(x(faceX), top - 1.2f, x(faceX), bottom + 1, 0.3f);
	}

	const auto ink = colourOf("print_grey");
	std::vector<float> xs;
	if (auto *a = layout["box_x"].getArray())
		for (const auto &x : *a)
			xs.push_back(num(x));
	const float y0 = top + num(layout["box_top"]), y1 = top + num(layout["box_rule"]), y2 = top + num(layout["box_bottom"]);
	const float titleY = top + num(layout["title_y"]), aboveY = top + num(layout["above_y"]), belowY = top + num(layout["below_y"]);
	g.setColour(ink);
	for (float x : xs)
		g.fillRect(x - 0.2f, y0, 0.4f, y2 - y0);
	g.fillRect(xs.front(), y2 - 0.2f, xs.back() - xs.front(), 0.4f);
	for (size_t k = 0; k + 1 < xs.size(); ++k)
		g.fillRect(xs[k] + 1, y1 - 0.2f, xs[k + 1] - xs[k] - 2, 0.4f);
	if (auto *groups = panelJson()["rear_panel"]["groups"].getArray())
		for (int k = 0; k < groups->size() && k + 1 < int(xs.size()); ++k)
			rearLegend(g, (*groups)[k]["label"].toString(), {(xs[size_t(k)] + xs[size_t(k + 1)]) / 2, titleY}, 3.3f);

	if (auto *screws = layout["screws"].getArray())
		for (const auto &s : *screws)
			rearScrew(g, kPanelW - num(s[0]), top + num(s[1]), num(s[2], 3.2f), s[3].toString() == "chrome");
	if (auto *items = panelJson()["rear_panel"]["items"].getArray())
		for (const auto &it : *items) {
			const auto id = it["id"].toString();
			const auto c = rearAt(it);
			const float w = num(it["w"]), h = num(it["h"]);
			if (id == "rear_sync_din") {
				rearDin(g, c, w, h);
			} else if (id == "rear_sync_switch") { // at OUT-PUT, where the 808 runs on its own clock
				const juce::Rectangle<float> slot = juce::Rectangle<float>(w, h).withCentre(c);
				g.setColour(juce::Colour(0xff0a0a0a));
				g.fillRoundedRectangle(slot, 0.6f);
				const auto cap = slot.reduced(0.8f).withTrimmedTop(h * 0.46f);
				g.setColour(juce::Colour(0xff1d1d1c));
				g.fillRect(cap);
				g.setColour(juce::Colours::white.withAlpha(0.2f));
				for (float y = cap.getY() + 0.6f; y < cap.getBottom(); y += 1.0f)
					g.fillRect(cap.getX(), y, cap.getWidth(), 0.25f);
				rearLegend(g, "INPUT", c.translated(9.2f, -3.9f), 2.4f);
				rearLegend(g, "OUT-", c.translated(8.9f, 1.7f), 2.4f);
				rearLegend(g, "PUT", c.translated(8.5f, 4.8f), 2.4f);
			} else {
				rearJackShape(g, c, w);
				rearLegend(g, it["below"].toString(), {c.x, belowY}, 2.7f, num(it["below_w"]));
				if (it.hasProperty("above"))
					rearLegend(g, it["above"].toString(), {c.x, aboveY}, 2.7f);
			}
		}
	// the bracket over MASTER OUT's two jacks, and the pedal's model
	const auto hi = rearJack(kVoices), lo = rearJack(kVoices + 1);
	const float x0 = hi.x + 2.1f, x1 = hi.x + 5.9f, x2 = lo.x - 2.0f, x3 = lo.x + 2.1f, rule = aboveY - 1.2f;
	rearLegend(g, "LEVEL", {(x1 + x2) / 2, aboveY}, 2.7f);
	g.setColour(ink);
	g.fillRect(x0, rule, x1 - x0, 0.4f);
	g.fillRect(x2, rule, x3 - x2, 0.4f);
	g.fillRect(x0, rule, 0.4f, 2.0f);
	g.fillRect(x3 - 0.4f, rule, 0.4f, 2.0f);
	const auto stop = rearAt(rearItem("rear_pedal_start_stop")), fill = rearAt(rearItem("rear_pedal_fill_in"));
	rearLegend(g, juce::String::fromUTF8("( DP\xe2\x80\x93" "2 )"), {(stop.x + fill.x) / 2, aboveY}, 2.7f, 12.4f);
}

// The cables: the main out in MASTER OUT HI always, and each MULTI OUT jack with one in; each hangs
// to a tag naming the plugin output it reaches. The jack under the mouse is ringed, and a note says
// what plugging it does.
void HardwareView::drawRear(juce::Graphics &g)
{
	struct Cable {
		int v;
		juce::Point<float> from;
		float tx, ty;
	};
	std::vector<Cable> cables;
	const auto layout = panelJson()["rear_panel"]["layout"];
	const float faceBottom = kRearTop + num(layout["face_h"]) + num(layout["lip_h"]);
	std::vector<int> order;
	for (int v = 0; v < kVoices; ++v)
		if (m_proc.cable(Voice(v)))
			order.push_back(v);
	std::sort(order.begin(), order.end(), [](int a, int b) { return rearJack(a).x < rearJack(b).x; });
	float prev = -1e9f;
	for (size_t k = 0; k < order.size(); ++k) { // two rows of tags, so eleven fit
		const auto c = rearJack(order[k]);
		const float tx = juce::jmin(kPanelW - 26, juce::jmax(c.x, prev + 24));
		prev = tx;
		cables.push_back({order[k], c, tx, faceBottom + 46 + (k % 2 == 0 ? 0.0f : 13.0f)});
	}
	cables.push_back({kVoices, rearJack(kVoices), rearJack(kVoices).x - 24, faceBottom + 32});

	{ // the cables, in mm
		juce::Graphics::ScopedSaveState s(g);
		g.addTransform(juce::AffineTransform::scale(kPxPerMm));
		if (m_hover.kind == HitKind::Jack) {
			const auto c = rearJack(m_hover.index);
			g.setColour(juce::Colour(0xffff8c1a));
			g.drawEllipse(c.x - 9.4f, c.y - 9.4f, 18.8f, 18.8f, 1.1f);
		}
		for (const auto &cb : cables) {
			juce::Path p;
			p.startNewSubPath(cb.from.translated(0, 2));
			p.cubicTo(cb.from.x, cb.from.y + 48, cb.tx, cb.ty - 30, cb.tx, cb.ty);
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
			const juce::String text = main ? juce::String("Main out") : juce::String(kOutputName[cb.v]) + (on ? " output" : ": off in DAW");
			const auto colour = main ? juce::Colour(0xff7aa2f7) : on ? juce::Colour(0xffff8c1a) : juce::Colour(0xff9aa4b1);
			const juce::Rectangle<float> tag(cb.tx - 23, cb.ty, 46, 11);
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
			g.setFont(fonts::mono(4.0f));
			g.drawText(text, tag, juce::Justification::centred);
		}
	}

	juce::String title = "MULTI OUT",
	             body = "Click a jack to plug a cable in: that voice leaves the main mix and plays on its own plugin output. "
	                    "Click again to pull it out.";
	if (m_hover.kind == HitKind::Jack) {
		const int v = m_hover.index;
		if (v == kVoices) {
			title = "MASTER OUT HI";
			body = "The main output: always connected. Every voice without a cable in its MULTI OUT jack plays here.";
		} else if (v == kVoices + 1) {
			title = "MASTER OUT LO";
			body = "The same mix at a tenth of the level. The plugin's main output is HI.";
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
	const juce::Rectangle<float> box(812, 490, 372, 104);
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
	m_bar.setTip(m_barService, m_service ? "Put the panel back" : "Take the panel off: every part on the boards, the six trimmers and a scope");

	juce::String status;
	if (m_service)
		status = serviceStatus();
	else if (!m_snap.power)
		status = "POWER OFF";
	else if (!m_proc.romLoaded())
		status = panel::voiceModeStatus("CPU ROM", true);
	else
		status << (m_snap.synced ? "HOST " : m_snap.running ? "RUN  " : "STOP ") << juce::String(m_snap.bpm, 1) << " BPM   "
		       << m_proc.params().getParameter(TR808Processor::kModeId)->getCurrentValueAsText().toUpperCase();
	juce::StringArray cables; // the MULTI OUT jacks with a cable in (voices out of the main mix)
	for (int v = 0; v < kVoices; ++v)
		if (m_proc.cable(Voice(v)))
			cables.add(kOutputName[v]);
	if (!m_service) {
		if (!cables.isEmpty())
			status << "   MULTI OUT: " << cables.joinIntoString(" ");
		else if (m_rear)
			status << "   MULTI OUT: no cables, all on the main out";
	}
	// A key held by a right-click (CLEAR with a step key) takes the status line over.
	const auto held = m_held.latchedNames([](int el) { return elements()[size_t(el)].label.replace("\n", " "); });
	if (!held.isEmpty() && !m_service)
		status = panel::heldStatus(held);
	static const char *names[] = {"BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH"};
	juce::String voices;
	for (int k = 0; k < tr808::kVoices; ++k)
		voices << (m_snap.voices[size_t(k)] > 0.05f ? names[k] : "  ") << " ";
	const float voicesX = m_bar.help().getX() - 12 - 450;
	panel::drawStatus(g, m_bar.status(voicesX - 6), status, kMono, held.isEmpty() ? panel::col::status : panel::col::accent);
	g.setColour(panel::col::accent);
	g.drawText(voices, juce::Rectangle<float>(voicesX, float(kPanelHeight), 450, float(kBarHeight)), juce::Justification::centredRight);
	const int tip = m_hover.kind == HitKind::BarSync ? m_barSync : m_hover.kind == HitKind::BarRear ? m_barRear : m_hover.kind == HitKind::BarService ? m_barService
	              : m_hover.kind == HitKind::BarHelp ? panel::Bar::kHelp : -1;
	if (tip != -1 && m_drag.kind == HitKind::None && !m_showHelp)
		panel::drawBarTip(g, m_bar, tip, getLocalBounds().toFloat(), kMono);
}

void HardwareView::drawHelp(juce::Graphics &g)
{
	lemus::panel::drawHelp(g, {0, 0, float(kWidth), float(kPanelHeight)}, "TR-808",
	                       {
	                           "1 2 3 4 5 6 7 8               step keys 1-8   (BASIC RHYTHM 1-8)",
	                           "Q W E R T Y U I               step keys 9-16  (BASIC RHYTHM 9-12, INTRO/FILL IN 1-4)",
	                           "Backspace, Delete             CLEAR",
	                           "Return                        TAP (INTRO SET / FILL IN TRIGGER)",
	                           "Space                         START/STOP",
	                           "F1-F6                         MODE: pattern clear, 1st part, 2nd part, manual play, play, compose",
	                           "",
	                           "TEMPO drags like a knob; click or drag the rotary switches, click the slide and lever switches.",
	                           "MIDI notes play the voices directly (36 BD, 37 RS, 38 SD, 39 CP, 42 CH, 43 LT, 46 OH,",
	                           "47 MT, 49 CY, 50 HT, 56 CB; congas 62-64, claves 75 and maracas 70 play their rows):",
	                           "velocity 100 and up accents.",
	                           "SYNC: HOST runs the sequencer from the DAW transport (DIN sync: run + 24 ppqn clock).",
	                           "REAR: the back panel. A cable in a MULTI OUT jack takes its voice out of the main mix to its own output.",
	                           "SERVICE: the top panel off, every part on the boards (wheel or pinch: zoom, drag: move); the",
	                           "six trimmers (SN p.14), the circuit by serial number (SN p.15), a scope on any part's pin.",
	                           "Programming follows the original manual: PATTERN CLEAR a rhythm, 1st PART, START,",
	                           "set PRE-SCALE and press CLEAR, pick the instrument, then the steps; hold a key about",
	                           "a step's length, as the original reads one key a step.",
	                       },
	                       kMono);
}


void HardwareView::drawTooltip(juce::Graphics &g)
{
	const Hit &h = m_drag.kind != HitKind::None ? m_drag : m_hover;
	if (h.index < 0 || h.kind == HitKind::Key)
		return;
	const auto &e = elements()[size_t(h.index)];
	juce::String text = paramText(e.param);
	if (text.isEmpty())
		return;
	lemus::panel::drawTooltip(g, text, {e.box.getCentreX() * kPxPerMm, e.box.getBottom() * kPxPerMm + 4},
	                          {0, 0, float(kWidth), float(kPanelHeight)}, kMono);
}

// ====================================================================== paint

void HardwareView::paint(juce::Graphics &g)
{
	if (m_service) {
		paintService(g);
		drawDrawer(g);
		drawBar(g);
		if (m_showHelp)
			drawHelp(g);
		return;
	}
	const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
	if (m_rear) {
		if (m_rearStatic.isNull() || std::abs(scale - m_rearScale) > 0.01f) {
			m_rearScale = scale;
			m_rearStatic = juce::Image(juce::Image::RGB, int(std::ceil(kWidth * scale)), int(std::ceil(kPanelHeight * scale)), true);
			juce::Graphics sg(m_rearStatic);
			sg.addTransform(juce::AffineTransform::scale(scale * kPxPerMm));
			renderRear(sg);
		}
		g.drawImage(m_rearStatic, juce::Rectangle<float>(0, 0, float(kWidth), float(kPanelHeight)));
		drawRear(g);
		drawBar(g);
		if (m_showHelp)
			drawHelp(g);
		return;
	}
	if (m_static.isNull() || std::abs(scale - m_staticScale) > 0.01f) {
		m_staticScale = scale;
		m_static = juce::Image(juce::Image::RGB, int(std::ceil(kWidth * scale)), int(std::ceil(kPanelHeight * scale)), true);
		juce::Graphics sg(m_static);
		sg.addTransform(juce::AffineTransform::scale(scale * kPxPerMm));
		renderStatic(sg);
	}
	g.drawImage(m_static, juce::Rectangle<float>(0, 0, float(kWidth), float(kPanelHeight)));
	{
		juce::Graphics::ScopedSaveState s(g);
		g.addTransform(juce::AffineTransform::scale(kPxPerMm));
		const auto &all = elements();
		for (size_t i = 0; i < all.size(); ++i) {
			const auto &e = all[i];
			const bool hover = m_hover.index == int(i) || m_drag.index == int(i);
			if (e.type == "knob")
				drawKnob(g, e, paramValue(e.param), hover);
			else if (e.type == "dial")
				drawDial(g, e, paramValue(e.param), hover);
			else if (e.type == "rotary_switch")
				drawRotary(g, e, choiceIndex(e.param), hover);
			else if (e.type == "slide_switch") {
				// voice switches: up (position 0) is the conga, claves or maracas, the parameter on
				const int pos = e.param == "prescale" ? choiceIndex(e.param) : (paramValue(e.param) > 0.5f ? 0 : 1);
				drawSlide(g, e, pos, hover);
			} else if (e.type == "lever_switch")
				drawLever(g, e, choiceIndex(e.param), hover);
			else if (e.type == "key") {
				drawKey(g, e, m_held.down(int(i)), 0, hover);
			} else if (e.type == "button" && e.param == "power")
				drawPower(g, e, m_proc.powered(), hover);
		}
		for (const auto &e : all)
			if (e.type == "led")
				drawLed(g, e, ledLevel(e));
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
		return i == lemus::panel::Bar::kHelp ? Hit{HitKind::BarHelp, -1}
		     : i == m_barSync                ? Hit{HitKind::BarSync, -1}
		     : i == m_barRear                ? Hit{HitKind::BarRear, -1}
		     : i == m_barService             ? Hit{HitKind::BarService, -1}
		                                     : Hit{};
	}
	if (m_service)
		return hitService(px);
	if (m_rear) {
		for (int v = 0; v < kVoices + 2; ++v)
			if (px.getDistanceFrom(rearJack(v) * kPxPerMm) < 22)
				return {HitKind::Jack, v};
		return {};
	}
	const auto mm = px / kPxPerMm;
	const auto &all = elements();
	for (size_t i = 0; i < all.size(); ++i) {
		const auto &e = all[i];
		if (e.type == "knob" || e.type == "dial") {
			const float r = (e.type == "dial" ? num(e.v["knob"]["knob_d"], 40) : num(e.v["knob_d"], 11)) / 2 + 1.5f;
			if (mm.getDistanceFrom(e.centre) < r)
				return {HitKind::Knob, int(i)};
		} else if (e.type == "rotary_switch") {
			if (mm.getDistanceFrom(e.centre) < num(e.v["knob_d"], 21) / 2 + 2)
				return {HitKind::Switch, int(i)};
		} else if (e.type == "slide_switch" && e.box.expanded(1.5f).contains(mm)) {
			return {HitKind::Slide, int(i)};
		} else if (e.type == "lever_switch" && e.box.expanded(1.5f).contains(mm)) {
			return {HitKind::Lever, int(i)};
		} else if (e.type == "key" && e.box.contains(mm)) {
			return {HitKind::Key, int(i)};
		} else if (e.type == "button" && e.param == "power" && e.box.expanded(1.2f).contains(mm)) {
			return {HitKind::Power, int(i)};
		}
	}
	return {};
}

void HardwareView::mouseMove(const juce::MouseEvent &e)
{
	const auto h = hitAt(e.position);
	if (h.kind != m_hover.kind || h.index != m_hover.index) {
		m_hover = h;
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
	const auto h = hitAt(e.position);
	m_drag = h;
	m_dragStart = e.position;
	if (h.kind == HitKind::BarSync) {
		setParamValue("sync", paramValue("sync") > 0.5f ? 0.0f : 1.0f);
	} else if (h.kind == HitKind::BarRear) {
		const bool rear = !m_rear;
		if (m_service)
			showService(false);
		showRear(rear);
		m_drag = {};
	} else if (h.kind == HitKind::Jack) {
		if (h.index < kVoices)
			m_proc.setCable(Voice(h.index), !m_proc.cable(Voice(h.index)));
		m_drag = {};
	} else if (h.kind == HitKind::BarService) {
		m_rear = false;
		showService(!m_service);
		m_drag = {};
	} else if (h.kind == HitKind::Scope) {
		scopeMenu(h.index);
		m_drag = {};
	} else if (h.kind == HitKind::TrimButton) { // its procedure set up, or the test stopped
		if (m_scope.procedure == h.index && m_scope.test)
			m_scope.test = false;
		else
			setUpTrim(h.index);
		m_drag = {};
	} else if (h.kind == HitKind::Board || h.kind == HitKind::Drawer || (m_service && h.kind == HitKind::None)) {
		if (m_service && h.kind != HitKind::Drawer) { // drag the boards about
			m_panning = true;
			m_drag = {HitKind::Board, -1};
			m_dragPan = m_pan;
		}
	} else if (h.kind == HitKind::Serial) {
		const auto serials = tr808::boardSerials();
		if (h.index >= 0 && h.index < int(serials.size())) {
			const int to = h.index + 1 < int(serials.size()) ? serials[size_t(h.index + 1)] - 1 : 999999;
			if (m_proc.serial() < serials[size_t(h.index)] || m_proc.serial() > to) // another unit: its range's first, Fischer's for the late board
				m_proc.setSerial(h.index + 1 == int(serials.size()) ? tr808::kFittedSerial : serials[size_t(h.index)]);
		}
		m_drag = {};
	} else if (h.kind == HitKind::Portion) {
		if ((h.index == 1) != m_proc.portionChanged())
			m_proc.setPortionChanged(h.index == 1);
		m_drag = {};
	} else if (h.kind == HitKind::Trim || h.kind == HitKind::Trimmer) {
		if (auto *p = m_proc.params().getParameter(TR808Processor::kTrimIds[h.index]))
			p->beginChangeGesture();
		m_dragStartValue = paramValue(TR808Processor::kTrimIds[h.index]);
	} else if (h.kind == HitKind::Power) {
		m_proc.setPower(!m_proc.powered());
	} else if (h.kind == HitKind::BarHelp) {
		m_showHelp = !m_showHelp;
	} else if (h.index >= 0) {
		const auto &el = elements()[size_t(h.index)];
		if (h.kind == HitKind::Knob) {
			m_dragStartValue = paramValue(el.param);
		} else if (h.kind == HitKind::Switch) {
			m_dragStartValue = float(choiceIndex(el.param));
			// a click on a dot's side goes there; on the knob it steps clockwise
			const auto mm = panelMm(e.position);
			int best = -1;
			float bestD = 3.0f;
			if (auto *pos = el.v["positions"].getArray())
				for (int k = 0; k < pos->size(); ++k)
					if ((*pos)[k]["dot"].isArray()) {
						const float d = mm.getDistanceFrom(pointOf((*pos)[k]["dot"]));
						if (d < bestD)
							bestD = d, best = k;
					}
			if (best >= 0)
				setChoice(el.param, best);
		} else if (h.kind == HitKind::Slide || h.kind == HitKind::Lever) {
			const auto mm = panelMm(e.position);
			int best = 0;
			float bestD = 1e9f;
			if (auto *pos = el.v["positions"].getArray())
				for (int k = 0; k < pos->size(); ++k) {
					const auto &p = (*pos)[k];
					const bool vertical = el.v["orientation"].toString() != "horizontal";
					const float off = h.kind == HitKind::Lever ? num(p["dx"]) : num(p["offset"]);
					const float d = std::abs(vertical && h.kind == HitKind::Slide ? mm.y - (el.centre.y + off) : mm.x - (el.centre.x + off));
					if (d < bestD)
						bestD = d, best = k;
				}
			if (el.param == "prescale" || h.kind == HitKind::Lever)
				setChoice(el.param, best);
			else
				setParamValue(el.param, best == 0 ? 1.0f : 0.0f);
		} else if (h.kind == HitKind::Key) {
			// A right-click (ctrl-click) latches it, for CLEAR with a step key; START/STOP acts on
			// the press, so it doesn't latch.
			if (keyOf(el) >= 0 && !m_held.mouseDown(h.index, e.mods, keyOf(el) != TR808Processor::StartStop))
				m_drag = {};
		}
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
	if (m_drag.kind == HitKind::Trim || m_drag.kind == HitKind::Trimmer) { // along its track (shift: a tenth as far)
		const float sens = (e.mods.isShiftDown() ? 0.1f : 1.0f) / 200.0f;
		setTrim(m_drag.index, m_dragStartValue + (e.position.x - m_dragStart.x) * sens);
		return;
	}
	if (m_drag.index < 0)
		return;
	const auto &el = elements()[size_t(m_drag.index)];
	const float dy = m_dragStart.y - e.position.y;
	if (m_drag.kind == HitKind::Knob) {
		const float range = e.mods.isShiftDown() ? 1200.0f : 240.0f;
		setParamValue(el.param, m_dragStartValue + dy / range);
	} else if (m_drag.kind == HitKind::Switch) {
		const int steps = int(std::round((dy + (e.position.x - m_dragStart.x)) / 22.0f));
		if (std::abs(e.position.getDistanceFrom(m_dragStart)) > 4)
			setChoice(el.param, int(m_dragStartValue) + steps);
	}
}

void HardwareView::mouseUp(const juce::MouseEvent &e)
{
	if (m_panning) {
		m_panning = false;
		m_viewChangedAt = juce::Time::getMillisecondCounter();
	}
	if (m_drag.kind == HitKind::Trim || m_drag.kind == HitKind::Trimmer) {
		if (auto *p = m_proc.params().getParameter(TR808Processor::kTrimIds[m_drag.index]))
			p->endChangeGesture();
	} else if (m_drag.kind == HitKind::Key)
		m_held.mouseUp(m_drag.index);
	else if (m_drag.kind == HitKind::Switch && e.position.getDistanceFrom(m_dragStart) < 4) {
		// a plain click on the knob (not a dot): one position clockwise, wrapping
		const auto &el = elements()[size_t(m_drag.index)];
		const auto mm = panelMm(e.position);
		if (mm.getDistanceFrom(el.centre) < num(el.v["knob_d"], 21) / 2 && int(m_dragStartValue) == choiceIndex(el.param))
			setChoice(el.param, (int(m_dragStartValue) + (e.mods.isRightButtonDown() ? -1 : 1) + choiceCount(el.param)) % choiceCount(el.param));
	}
	m_drag = {};
	repaint();
}

void HardwareView::mouseDoubleClick(const juce::MouseEvent &e)
{
	const auto h = hitAt(e.position);
	if (h.kind == HitKind::Knob)
		if (auto *p = m_proc.params().getParameter(paramId(elements()[size_t(h.index)].param)))
			p->setValueNotifyingHost(p->getDefaultValue());
	if (h.kind == HitKind::Trim || h.kind == HitKind::Trimmer) // back to the chosen unit's trim
		setTrim(h.index, float(m_proc.unitTrim(h.index)));
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
	if (m_service && (h.kind == HitKind::Trim || h.kind == HitKind::Trimmer)) {
		setTrim(h.index, paramValue(TR808Processor::kTrimIds[h.index]) + w.deltaY * (e.mods.isShiftDown() ? 0.005f : 0.03f));
		return;
	}
	if (m_service && (h.kind == HitKind::Board || h.kind == HitKind::None)) {
		zoomService(std::pow(2.0f, w.deltaY * 2.0f), e.position);
		return;
	}
	if (h.index < 0 || (h.kind != HitKind::Knob && h.kind != HitKind::Switch))
		return;
	const auto &el = elements()[size_t(h.index)];
	if (h.kind == HitKind::Knob)
		setParamValue(el.param, paramValue(el.param) + w.deltaY * (e.mods.isShiftDown() ? 0.02f : 0.1f));
	else if (h.kind == HitKind::Switch)
		setChoice(el.param, choiceIndex(el.param) + (w.deltaY > 0 ? 1 : -1));
}

// ====================================================================== keyboard

namespace {
int stepForKey(int code)
{
	const char *row1 = "12345678", *row2 = "QWERTYUI";
	for (int i = 0; i < 8; ++i) {
		if (code == row1[i])
			return i;
		if (code == row2[i] || code == std::tolower(row2[i]))
			return 8 + i;
	}
	return -1;
}
} // namespace

bool HardwareView::keyPressed(const juce::KeyPress &k)
{
	namespace panel = lemus::panel;
	const int code = k.getKeyCode();
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
	if (code >= juce::KeyPress::F1Key && code <= juce::KeyPress::F6Key) {
		setChoice(TR808Processor::kModeId, code - juce::KeyPress::F1Key);
		return true;
	}
	juce::String param;
	const int step = stepForKey(code);
	if (step >= 0)
		param = "step" + juce::String(step + 1);
	else if (panel::isClear(k))
		param = "clear";
	else if (code == juce::KeyPress::returnKey)
		param = "tap";
	else if (panel::spaceIsOurs(k))
		param = "start";
	if (param.isEmpty())
		return false;
	const auto &all = elements();
	for (size_t i = 0; i < all.size(); ++i)
		if (all[i].type == "key" && all[i].param == param) {
			m_held.keyDown(code, int(i));
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

} // namespace tr808
