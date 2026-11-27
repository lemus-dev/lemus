#include "HardwareView.h"

#include "BSRackPanel.h"
#include "Fonts.h"

#include <cmath>
#include <map>

namespace bsrack {

namespace {

constexpr float kPx = HardwareView::kPxPerMm;

const lemus::panel::Mono &barMono()
{
	static const lemus::panel::Mono m = [](float size, int weight) { return fonts::mono(size, weight); };
	return m;
}

// docs/panel.json, built in.
const juce::var &panel()
{
	static const juce::var v = juce::JSON::parse(juce::String::fromUTF8(BSRackPanel::panel_json, BSRackPanel::panel_jsonSize));
	return v;
}

float num(const juce::var &v, float fallback = 0) { return v.isVoid() || v.isUndefined() ? fallback : float(double(v)); }
juce::Point<float> pointOf(const juce::var &v) { return {num(v[0]), num(v[1])}; }
juce::Rectangle<float> boxOf(const juce::var &v) { return {num(v[0]), num(v[1]), num(v[2]), num(v[3])}; }
// [x0, y0, x1, y1] (the rear's frames and boxes)
juce::Rectangle<float> cornersOf(const juce::var &v) { return juce::Rectangle<float>::leftTopRightBottom(num(v[0]), num(v[1]), num(v[2]), num(v[3])); }
// The rear plate's top left in view mm: centred in the front's 482.6 x 44.0 mm (the back has no ears).
juce::Point<float> rearOrigin()
{
	const auto &v = panel();
	const auto &plate = v["rear"]["plate"];
	return {(num(v["width"], 482.6f) - num(plate[0], 440.0f)) / 2, (num(v["height"], 44.0f) - num(plate[1], 43.2f)) / 2};
}
juce::Rectangle<float> view(juce::Rectangle<float> mm) { return mm * kPx; }
juce::Point<float> view(juce::Point<float> mm) { return mm * kPx; }
juce::Colour colour(const juce::String &name)
{
	static std::map<juce::String, juce::Colour> known; // the message thread's alone
	if (const auto it = known.find(name); it != known.end())
		return it->second;
	const auto s = panel()["colours"][juce::Identifier(name)].toString();
	const auto c = s.isEmpty() ? juce::Colours::magenta : juce::Colour::fromString("ff" + s.fromFirstOccurrenceOf("#", false, false));
	known[name] = c;
	return c;
}

// No LED is lit in the photo: lit, the display's segments and the LEDs are drawn as red LEDs.
const juce::Colour kLedOn{0xffff3a22};

// A point at `deg` (clockwise from 12 o'clock) and `r` mm from `c`.
juce::Point<float> polar(juce::Point<float> c, float deg, float r)
{
	const float a = juce::degreesToRadians(deg);
	return {c.x + r * std::sin(a), c.y - r * std::cos(a)};
}

// The legends' face: its cap height as a share of the font's height.
float capRatio()
{
	static const float r = [] {
		juce::GlyphArrangement ga;
		ga.addLineOfText(juce::Font(juce::FontOptions(fonts::legend()).withHeight(100.0f)), "H", 0, 0);
		juce::Path p;
		ga.createPath(p);
		return p.getBounds().getHeight() / 100.0f;
	}();
	return r;
}

// The printed legends run 0.86 of Liberation Sans Bold's width at their cap height (0.86 +- 0.03 over
// 49 words measured from the photo), the blue titles 1.07 (a wider cut).
float condense(const juce::Colour &c, const juce::Colour &blue) { return c == blue ? 1.07f : 0.86f; }

// `text` as a path in view pixels: its cap height `cap` mm centred on `midY`, its ink left of, centred
// on or right of x (align -1, 0, 1), or fitted to `w` mm from x.
juce::Path textPath(const juce::String &text, float x, float midY, float cap, int align, float sxPerSy, float w = 0)
{
	juce::GlyphArrangement ga;
	ga.addLineOfText(juce::Font(juce::FontOptions(fonts::legend()).withHeight(100.0f)), text, 0, 0);
	juce::Path p;
	ga.createPath(p);
	const auto b = p.getBounds();
	if (b.isEmpty())
		return {};
	const float sy = cap * kPx / (capRatio() * 100.0f);
	const float sx = w > 0 ? w * kPx / b.getWidth() : sy * sxPerSy;
	const float width = b.getWidth() * sx;
	float left = x * kPx;
	if (w <= 0)
		left -= align == 0 ? width / 2 : align > 0 ? width : 0.0f;
	p.applyTransform(juce::AffineTransform::translation(-b.getX(), 0).scaled(sx, sy).translated(left, (midY + cap / 2) * kPx));
	return p;
}

// A polyline in mm, as a path in view pixels.
juce::Path polyline(std::initializer_list<juce::Point<float>> pts)
{
	juce::Path p;
	bool first = true;
	for (const auto &q : pts) {
		if (first)
			p.startNewSubPath(view(q));
		else
			p.lineTo(view(q));
		first = false;
	}
	return p;
}

// The keys' names (their numeral and second function, as printed) and the computer keys that press
// them, in bsrack::Button order.
struct KeyInfo {
	const char *name, *computer;
};
const KeyInfo kKeyInfo[kButtons] = {
	{"1 / PITCH BEND", "1"},         {"2 / PITCH MOD", "2"},        {"3 / FILTER MOD", "3"},
	{"4 / AT/BTH P.MOD", "4"},       {"5 / AT/BTH F.MOD", "5"},     {"6 / AT/BTH EG BIAS", "6"},
	{"7 / MIDI/LFO CLK", "7"},       {"8 / CV / GATE CH.", "8"},    {"9 / TUNING", "9"},
	{"\xe2\x88\x92 / SAVE TYPE, COMPARE", "-"}, {"0 / POLY MODE", "0"}, {"+ / SAVE, WRITE", "="},
	{"AUDITION/DEMO", "A"},          {"MODE SELECT", "M"},
};

// The 7-segment digit's segments a-g in `box` (mm): their outlines.
std::array<juce::Path, 7> segments(juce::Rectangle<float> box)
{
	const float t = box.getWidth() * 0.17f, gap = t * 0.14f;
	const float x0 = box.getX(), x1 = box.getRight(), xl = x0 + t / 2, xr = x1 - t / 2;
	const float ya = box.getY() + t / 2, yg = box.getCentreY(), yd = box.getBottom() - t / 2;
	auto horizontal = [&](float yc) {
		juce::Path p;
		p.startNewSubPath(view(juce::Point<float>(x0 + t / 2 + gap, yc)));
		for (const auto &q : std::initializer_list<juce::Point<float>>{{x0 + t + gap, yc - t / 2}, {x1 - t - gap, yc - t / 2}, {x1 - t / 2 - gap, yc},
		                                                             {x1 - t - gap, yc + t / 2}, {x0 + t + gap, yc + t / 2}})
			p.lineTo(view(q));
		p.closeSubPath();
		return p;
	};
	auto vertical = [&](float xc, float y0, float y1) {
		juce::Path p;
		p.startNewSubPath(view(juce::Point<float>(xc, y0 + t / 2 + gap)));
		for (const auto &q : std::initializer_list<juce::Point<float>>{{xc + t / 2, y0 + t + gap}, {xc + t / 2, y1 - t - gap}, {xc, y1 - t / 2 - gap},
		                                                             {xc - t / 2, y1 - t - gap}, {xc - t / 2, y0 + t + gap}})
			p.lineTo(view(q));
		p.closeSubPath();
		return p;
	};
	return {horizontal(ya), vertical(xr, ya, yg), vertical(xr, yg, yd), horizontal(yd), vertical(xl, yg, yd), vertical(xl, ya, yg), horizontal(yg)};
}

// A digit's decimal point: right of its box's lower corner, where the EDIT and WRITE marks point.
juce::Point<float> decimalPoint(juce::Rectangle<float> box) { return {box.getRight() + 0.85f, box.getBottom() - 0.45f}; }

} // namespace

// ============================================================================== setup

HardwareView::HardwareView(BassStationProcessor &p) : m_proc(p)
{
	const auto &v = panel();
	auto &params = p.params();
	for (const auto &k : *v["knobs"].getArray()) {
		Knob knob;
		knob.param = params.getParameter(k["param"].toString());
		knob.c = pointOf(k["c"]);
		knob.d = num(k["d"], 12.6f);
		knob.capD = num(k["cap_d"], 8.9f);
		const auto &ticks = k.hasProperty("ticks") ? k["ticks"] : v["knob_ticks"];
		knob.from = num(ticks["from"], -141);
		knob.to = num(ticks["to"], 141);
		m_knobs.push_back(knob);
	}
	for (const auto &s : *v["switches"].getArray()) {
		Switch sw;
		sw.param = params.getParameter(s["param"].toString());
		sw.body = boxOf(s["body"]);
		for (const auto &x : *s["lever_x"].getArray())
			sw.leverX.push_back(num(x));
		m_switches.push_back(sw);
	}
	const auto &keys = *v["keys"].getArray();
	for (int i = 0; i < keys.size() && i < kButtons; ++i) {
		const auto &k = keys[i];
		Key key;
		key.id = k["id"].toString();
		key.name = juce::String::fromUTF8(kKeyInfo[i].name);
		key.computer = kKeyInfo[i].computer;
		key.box = boxOf(k["box"]);
		if (k.hasProperty("legend")) {
			const auto &l = k["legend"];
			key.legend = {l["text"].toString(), num(l["x"]), num(l["y"]), num(l["cap"], 1.7f), 0, colour(l["colour"].toString())};
		}
		if (k.hasProperty("icon"))
			key.note = boxOf(k["icon"]["box"]);
		m_keys.push_back(key);
	}
	m_held.reset(m_keys.size(), [this](int i, bool down) { m_proc.setButton(i, down); });
	{
		const auto &d = v["display"];
		m_displayAreas.push_back(view(boxOf(d["window"])).expanded(2).toNearestInt());
		for (const auto &l : *v["leds"].getArray()) {
			const float r = num(l["d"], 2.3f) * 1.2f;
			m_displayAreas.push_back(view(juce::Rectangle<float>(2 * r, 2 * r).withCentre(pointOf(l["c"]))).expanded(2).toNearestInt());
		}
	}
	m_barRear = m_bar.add(64, "REAR: turns the unit round to its jacks (FILTER's EXTERNAL AUDIO INPUT is the side-chain)");
	buildStatic();
	setWantsKeyboardFocus(true);
	setOpaque(true);
	m_display = m_proc.display();
	m_signature = signature();
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
	for (const auto &k : m_knobs)
		n += k.param == nullptr;
	for (const auto &s : m_switches)
		n += s.param == nullptr;
	return n;
}

void HardwareView::addFill(juce::Colour c, const juce::Path &p)
{
	if (!m_layers.empty() && m_layers.back().colour == c)
		m_layers.back().path.addPath(p);
	else
		m_layers.push_back({c, p});
}

void HardwareView::addStroke(juce::Colour c, const juce::Path &p, float widthMm)
{
	juce::Path out;
	juce::PathStrokeType(widthMm * kPx, juce::PathStrokeType::mitered, juce::PathStrokeType::butt).createStrokedPath(out, p);
	addFill(c, out);
}

void HardwareView::addText(const Text &t)
{
	addFill(t.colour, textPath(t.text, t.x, t.y, t.cap, t.align, condense(t.colour, colour("blue")), t.w));
}

// Everything printed or fixed: the face, the slots, the frames and strips, the scales, the
// legends, the display's window, PHONES. Made once as paths by colour.
void HardwareView::buildStatic()
{
	const auto &v = panel();
	{
		juce::Path p;
		p.addRectangle(0, 0, float(kWidth), float(kPanelHeight));
		addFill(colour("face"), p);
	}
	{
		const auto &s = v["slots"];
		juce::Path p;
		const float w = num(s["w"], 8.8f), h = num(s["h"], 6.2f);
		for (const auto &c : *s["centres"].getArray())
			p.addRoundedRectangle(view(juce::Rectangle<float>(w, h).withCentre(pointOf(c))), h * kPx / 2);
		addFill(juce::Colour(0xff0b0b0d), p);
	}
	{
		juce::Path p;
		for (const auto &f : *v["frames"].getArray())
			p.addRectangle(view(boxOf(f["box"])));
		addStroke(colour("frame"), p, num(v["frame_line_w"], 0.3f));
	}
	{
		juce::Path p;
		for (const auto &s : *v["strips"].getArray())
			p.addRectangle(view(boxOf(s["box"])));
		addFill(colour("strip"), p);
	}
	// the light boxes behind COMPARE and the two WRITEs, the strip under the logo
	{
		juce::Path p;
		auto box = [&p](const juce::var &t) {
			if (t.hasProperty("box"))
				p.addRectangle(view(boxOf(t["box"])));
		};
		for (const auto &k : *v["keys"].getArray())
			for (const auto &l : *k["labels"].getArray())
				box(l);
		for (const auto &s : *v["switches"].getArray())
			box(s["label"]);
		addFill(colour("label_box"), p);
		juce::Path strip;
		strip.addRectangle(view(boxOf(v["name_strip"]["box"])));
		addFill(colour("synth_strip"), strip);
	}
	// the display's window and its red filter, the unlit segments faintly behind it
	{
		const auto &d = v["display"];
		juce::Path w, f, segs;
		w.addRoundedRectangle(view(boxOf(d["window"])), 1.2f * kPx);
		f.addRectangle(view(boxOf(d["filter"])));
		addFill(colour("display"), w);
		addFill(colour("display_filter"), f);
		for (const auto &b : *d["digits"].getArray()) {
			for (const auto &s : segments(boxOf(b)))
				segs.addPath(s);
			segs.addEllipse(juce::Rectangle<float>(0.9f * kPx, 0.9f * kPx).withCentre(view(decimalPoint(boxOf(b)))));
		}
		addFill(colour("display_filter").brighter(0.1f), segs);
		juce::Path marks;
		for (const auto &m : *d["marks"].getArray()) {
			const auto &c = *m["corner"].getArray();
			juce::Path l;
			for (int i = 0; i < c.size(); ++i)
				i == 0 ? l.startNewSubPath(view(pointOf(c[i]))) : l.lineTo(view(pointOf(c[i])));
			marks.addPath(l);
		}
		addStroke(colour("legend"), marks, 0.25f);
	}
	// PHONES: the jack's nut, its socket, the headphones symbol
	{
		const auto &ph = v["phones"];
		const auto c = pointOf(ph["c"]);
		const float nut = num(ph["nut_d"], 8.44f), hole = num(ph["hole_d"], 6.34f);
		juce::Path n, h, ring;
		n.addEllipse(view(juce::Rectangle<float>(nut, nut).withCentre(c)));
		addFill(colour("phones_nut"), n);
		ring.addEllipse(view(juce::Rectangle<float>(hole + 0.5f, hole + 0.5f).withCentre(c)));
		addFill(colour("phones_nut").darker(0.6f), ring);
		h.addEllipse(view(juce::Rectangle<float>(hole, hole).withCentre(c)));
		addFill(colour("phones_hole"), h);
		const auto b = boxOf(ph["icon"]["box"]);
		const float r = b.getWidth() / 2 - 0.3f, cy = b.getY() + r + 0.25f;
		juce::Path band;
		band.addCentredArc(b.getCentreX() * kPx, cy * kPx, r * kPx, r * kPx, 0, -juce::MathConstants<float>::halfPi * 1.05f,
		                   juce::MathConstants<float>::halfPi * 1.05f, true);
		band.lineTo(view(juce::Point<float>(b.getRight() - 0.3f, b.getBottom() - 1.1f)));
		band.startNewSubPath(view(juce::Point<float>(b.getX() + 0.3f, cy)));
		band.lineTo(view(juce::Point<float>(b.getX() + 0.3f, b.getBottom() - 1.1f)));
		addStroke(colour("legend"), band, 0.35f);
		juce::Path cups;
		cups.addRoundedRectangle(view(juce::Rectangle<float>(b.getX(), b.getBottom() - 1.6f, 0.9f, 1.6f)), 0.25f * kPx);
		cups.addRoundedRectangle(view(juce::Rectangle<float>(b.getRight() - 0.9f, b.getBottom() - 1.6f, 0.9f, 1.6f)), 0.25f * kPx);
		addFill(colour("legend"), cups);
	}
	// the knobs' scales: the ticks, MIX's ring with its yellow EXT. end
	{
		const auto &common = v["knob_ticks"];
		juce::Path ticks;
		for (const auto &k : *v["knobs"].getArray()) {
			const auto c = pointOf(k["c"]);
			const auto &t = k.hasProperty("ticks") ? k["ticks"] : common;
			const int n = int(num(t["n"], 11));
			const float r0 = num(t["r_in"], 6.45f), r1 = num(t["r_out"], 7.98f), from = num(t["from"], -141), to = num(t["to"], 141);
			for (int i = 0; i < n; ++i) {
				const float a = from + (to - from) * float(i) / float(n - 1);
				ticks.startNewSubPath(view(polar(c, a, r0)));
				ticks.lineTo(view(polar(c, a, r1)));
			}
			if (k.hasProperty("ring")) {
				const auto &ring = k["ring"];
				const float rm = (num(ring["r_in"]) + num(ring["r_out"])) / 2, rw = num(ring["r_out"]) - num(ring["r_in"]);
				auto arc = [&](float a0, float a1) {
					juce::Path p;
					p.addCentredArc(c.x * kPx, c.y * kPx, rm * kPx, rm * kPx, 0, juce::degreesToRadians(a0), juce::degreesToRadians(a1), true);
					return p;
				};
				addStroke(colour(ring["colour"].toString()), arc(num(ring["from"]), num(ring["to"])), rw);
				const auto &y = ring["yellow"];
				addStroke(colour(y["colour"].toString()), arc(num(y["from"]), num(y["to"])), rw);
			}
		}
		addStroke(colour("tick"), ticks, num(common["w"], 0.3f));
	}
	// the knobs' black knurled skirts and their shadows, the switches' slots: they don't move
	{
		juce::Path shadow, skirt, flutes, rims, slots, slotRims;
		for (const auto &k : m_knobs) {
			const auto c = view(k.c);
			const float rs = k.d / 2 * kPx;
			shadow.addEllipse(juce::Rectangle<float>(2 * rs, 2 * rs).withCentre(c.translated(0.6f, 1.2f)));
			skirt.addEllipse(juce::Rectangle<float>(2 * rs, 2 * rs).withCentre(c));
			for (int i = 0; i < 40; ++i) {
				const float a = juce::MathConstants<float>::twoPi * float(i) / 40.0f;
				flutes.startNewSubPath(c.x + (rs - 2.2f) * std::sin(a), c.y - (rs - 2.2f) * std::cos(a));
				flutes.lineTo(c.x + (rs - 0.4f) * std::sin(a), c.y - (rs - 0.4f) * std::cos(a));
			}
			rims.addEllipse(juce::Rectangle<float>(2 * rs - 1, 2 * rs - 1).withCentre(c));
		}
		for (const auto &sw : m_switches) {
			slots.addRoundedRectangle(view(sw.body), 0.7f * kPx);
			slotRims.addRoundedRectangle(view(sw.body).reduced(0.5f), 0.7f * kPx);
		}
		addFill(juce::Colours::black.withAlpha(0.45f), shadow);
		addFill(colour("skirt"), skirt);
		addStroke(juce::Colours::white.withAlpha(0.1f), flutes, 0.8f / kPx);
		addStroke(juce::Colours::white.withAlpha(0.16f), rims, 1.0f / kPx);
		addFill(colour("switch_slot"), slots);
		addStroke(juce::Colours::white.withAlpha(0.07f), slotRims, 1.0f / kPx);
	}
	// the switches' waveform symbols, MODE's bracket line
	{
		juce::Path icons;
		for (const auto &s : *v["switches"].getArray())
			for (const auto &l : *s["legends"].getArray()) {
				if (!l.hasProperty("icon"))
					continue;
				const auto b = boxOf(l["box"]).reduced(0.12f);
				if (l["icon"].toString() == "saw")
					icons.addPath(polyline({b.getBottomLeft(), b.getTopRight(), b.getBottomRight()}));
				else
					icons.addPath(polyline({b.getBottomLeft(), b.getTopLeft(), {b.getCentreX(), b.getY()}, {b.getCentreX(), b.getBottom()},
					                        b.getBottomRight(), b.getTopRight()}));
			}
		addStroke(colour("legend"), icons, 0.28f);
		for (const auto &k : *v["keys"].getArray())
			if (k.hasProperty("bracket")) {
				const auto &br = k["bracket"];
				const auto &pts = *br["points"].getArray();
				juce::Path l;
				for (int i = 0; i < pts.size(); ++i)
					i == 0 ? l.startNewSubPath(view(pointOf(pts[i]))) : l.lineTo(view(pointOf(pts[i])));
				addStroke(colour(br["colour"].toString()), l, num(br["w"], 0.3f));
			}
	}
	// every printed word but the keys' own (they move with the key)
	{
		std::vector<Text> texts;
		for (const auto &section : panel()["texts"].getDynamicObject()->getProperties())
			for (const auto &t : *section.value.getArray()) {
				if (t["colour"].toString() == "key_text")
					continue;
				const auto c = colour(t["colour"].toString());
				const auto align = t["align"].toString();
				texts.push_back({t["text"].toString(), num(t["x"]), num(t["y"]), num(t["cap"], 1.5f), align == "left" ? -1 : align == "right" ? 1 : 0, c,
				                 num(t["w"])});
			}
		// grouped by colour: fewer layers
		std::stable_sort(texts.begin(), texts.end(), [](const Text &a, const Text &b) { return a.colour.getARGB() < b.colour.getARGB(); });
		for (const auto &t : texts)
			addText(t);
	}
}

// ============================================================================== painting

void HardwareView::timerCallback()
{
	if (!m_focused && isShowing()) {
		grabKeyboardFocus(); // the computer keys work without a click first
		m_focused = true;
	}
	const auto d = m_proc.display();
	const bool displayChanged = d.digits != m_display.digits || d.program != m_display.program || d.midiRx != m_display.midiRx ||
	                            d.midiTx != m_display.midiTx || d.utility != m_display.utility || d.save != m_display.save ||
	                            d.edit != m_display.edit || d.write != m_display.write || d.lfo != m_display.lfo;
	m_display = d;
	if (const auto s = signature(); s != m_signature) {
		m_signature = s;
		repaint();
	} else if (displayChanged) {
		// the display and the LEDs alone (the LFO SPEED LED flashes at the LFO's rate)
		for (const auto &r : m_displayAreas)
			repaint(r);
	}
}

// What else the panel shows that can change without the mouse: the controls (automation), the bar,
// the user's artwork (a rescan).
uint64_t HardwareView::signature() const
{
	uint64_t h = 1469598103934665603ull;
	auto mix = [&h](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
	for (const auto &k : m_knobs)
		if (k.param != nullptr)
			mix(uint64_t(k.param->getValue() * 65536.0f));
	for (const auto &s : m_switches)
		if (s.param != nullptr)
			mix(uint64_t(s.param->getValue() * 65536.0f));
	mix(uint64_t(m_proc.filterInRouted()) | uint64_t(m_proc.asleep()) << 1 | uint64_t(m_proc.romLoaded()) << 2);
	const auto &marks = m_proc.artwork().marks();
	mix(marks.size());
	for (const auto &m : marks)
		mix(uint64_t(reinterpret_cast<uintptr_t>(m.picture.get())) ^ uint64_t(m.outline.getBounds().getWidth() * 1000.0f));
	return h;
}

void HardwareView::paint(juce::Graphics &g)
{
	if (m_rear)
		paintRear(g);
	else
		paintPanel(g);
	drawBar(g);
	if (m_help)
		// over the bar too: the 1U panel alone is too short for the list
		lemus::panel::drawHelp(
			g, getLocalBounds().toFloat(), "BASS STATION RACK",
			lemus::panel::byPlace({"1 2 3 4 5 6 7 8 9 0   the keypad's digits (the numeric keypad's too)",
			                       juce::String::fromUTF8("-  =                  the keypad's \xe2\x88\x92 and + (the numeric keypad's - and + too)"),
			                       "A                     AUDITION/DEMO", "M                     MODE SELECT",
			                       "Knobs: drag up and down, Shift for fine, double-click for the start position, or the mouse wheel",
			                       "Switches: click to push the lever a position that way (two positions: it flips), drag it, or the wheel",
			                       "REAR (the bar): turns the unit round. FILTER's EXTERNAL AUDIO INPUT there is the plugin's side-chain:",
			                       "  route a track to it to play it through the filter (a cable shows in the jack)"}),
			barMono());
	else
		drawTip(g);
}

void HardwareView::paintPanel(juce::Graphics &g)
{
	// The printed panel, drawn once for each scale it is shown at (the window's size, the screen's
	// pixels), one image pixel to a screen pixel: its paths are too many to fill on every repaint.
	const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
	if (!m_static.isValid() || std::abs(scale - m_staticScale) > 1e-3f) {
		m_staticScale = scale;
		m_static = juce::Image(juce::Image::RGB, juce::jmax(1, int(std::ceil(float(kWidth) * scale))), juce::jmax(1, int(std::ceil(float(kPanelHeight) * scale))), false);
		juce::Graphics ig(m_static);
		ig.addTransform(juce::AffineTransform::scale(scale));
		for (const auto &l : m_layers) {
			ig.setColour(l.colour);
			ig.fillPath(l.path);
		}
	}
	g.drawImageTransformed(m_static, juce::AffineTransform::scale(1.0f / scale));
	drawMarks(g, false);
	drawDisplay(g);
	const auto &v = panel();
	for (const auto &l : *v["leds"].getArray()) {
		const bool lit = l["name"].toString() == "WRITE" ? m_display.write : m_display.lfo;
		drawLed(g, pointOf(l["c"]), num(l["d"], 2.3f), colour(l["colour"].toString()), lit && m_proc.romLoaded());
	}
	for (int i = 0; i < int(m_knobs.size()); ++i)
		drawKnob(g, m_knobs[size_t(i)], i == m_hoverKnob || i == m_dragKnob);
	for (int i = 0; i < int(m_switches.size()); ++i)
		drawSwitch(g, m_switches[size_t(i)], i == m_hoverSwitch || i == m_dragSwitch);
	for (int i = 0; i < int(m_keys.size()); ++i)
		drawKey(g, m_keys[size_t(i)], m_held.down(i), i == m_hoverKey);
}

// The brand marks: the user's artwork in its measured box, or nothing.
void HardwareView::drawMarks(juce::Graphics &g, bool rear) const
{
	for (const auto &l : *panel()["logos"].getArray()) {
		if ((l["side"].toString() == "rear") != rear)
			continue;
		const auto *mark = m_proc.artwork().find(l["mark"].toString());
		if (mark == nullptr)
			continue;
		const auto box = view(boxOf(l["box"]) + (rear ? rearOrigin() : juce::Point<float>()));
		if (mark->picture != nullptr) {
			mark->picture->drawWithin(g, box);
		} else {
			// a pack's outline, in panel millimetres, in the panel's colour for that mark
			g.setColour(rear ? colour("rear_ink") : colour(l["place"].toString() == "maker" ? "novation_logo" : "logo_blue"));
			g.fillPath(mark->outline, juce::AffineTransform::scale(kPx));
		}
	}
}

void HardwareView::drawDisplay(juce::Graphics &g) const
{
	if (!m_proc.romLoaded())
		return;
	const auto &d = panel()["display"];
	const auto &digits = *d["digits"].getArray();
	juce::Path lit, dots;
	for (int i = 0; i < digits.size() && i < 2; ++i) {
		const auto box = boxOf(digits[i]);
		const uint8_t bits = m_display.digits[size_t(i)];
		const auto segs = segments(box);
		for (int s = 0; s < 7; ++s)
			if ((bits >> s) & 1)
				lit.addPath(segs[size_t(s)]);
		if ((bits >> 7) & 1)
			dots.addEllipse(juce::Rectangle<float>(0.9f * kPx, 0.9f * kPx).withCentre(view(decimalPoint(box))));
	}
	lit.addPath(dots);
	if (!lit.isEmpty()) {
		g.setColour(kLedOn.withAlpha(0.25f));
		g.strokePath(lit, juce::PathStrokeType(0.5f * kPx, juce::PathStrokeType::curved));
		g.setColour(kLedOn);
		g.fillPath(lit);
	}
	const float ledD = num(d["led_d"], 2.1f);
	for (const auto &l : *d["leds"].getArray()) {
		const auto name = l["name"].toString();
		const bool on = name == "PROGRAM" ? m_display.program : name == "MIDI RX CH" ? m_display.midiRx : name == "MIDI TX CH" ? m_display.midiTx
		              : name == "UTILITY"   ? m_display.utility
		                                    : m_display.save;
		drawLed(g, pointOf(l["c"]), ledD, colour("led_display_off"), on);
	}
}

void HardwareView::drawLed(juce::Graphics &g, juce::Point<float> c, float d, juce::Colour off, bool lit) const
{
	const auto r = view(juce::Rectangle<float>(d, d).withCentre(c));
	if (lit) {
		g.setColour(kLedOn.withAlpha(0.3f));
		g.fillEllipse(r.expanded(r.getWidth() * 0.45f));
	}
	g.setColour(lit ? kLedOn : off);
	g.fillEllipse(r);
	g.setColour(juce::Colours::white.withAlpha(lit ? 0.5f : 0.14f));
	g.fillEllipse(r.reduced(r.getWidth() * 0.32f).translated(-r.getWidth() * 0.1f, -r.getHeight() * 0.12f));
}

void HardwareView::drawKnob(juce::Graphics &g, const Knob &k, bool hover) const
{
	const auto c = view(k.c);
	const float rc = k.capD / 2 * kPx;
	// the blue cap, lit from above (the skirt under it is printed with the panel)
	const auto cap = juce::Rectangle<float>(2 * rc, 2 * rc).withCentre(c);
	const auto blue = colour("cap");
	g.setGradientFill(juce::ColourGradient(blue.brighter(hover ? 0.35f : 0.22f), cap.getX(), cap.getY(), blue.darker(0.3f), cap.getRight(), cap.getBottom(), false));
	g.fillEllipse(cap);
	g.setColour(juce::Colours::black.withAlpha(0.35f));
	g.drawEllipse(cap, 0.8f);
	// the white pointer, over the printed scale's travel
	const float value = k.param != nullptr ? k.param->getValue() : 0.0f;
	const float a = juce::degreesToRadians(k.from + (k.to - k.from) * value);
	const auto &p = panel()["knob_pointer"];
	const float w = num(p["w"], 1.0f) * kPx, r0 = num(p["from_r"]) * kPx, r1 = num(p["to_r"], 4.4f) * kPx;
	juce::Path bar;
	bar.addRectangle(-w / 2, -r1, w, r1 - r0 + w / 2);
	g.setColour(colour("pointer"));
	g.fillPath(bar, juce::AffineTransform::rotation(a).translated(c));
}

void HardwareView::drawSwitch(juce::Graphics &g, const Switch &s, bool hover) const
{
	// the lever (its slot is drawn with the panel)
	if (s.leverX.empty())
		return;
	const auto &l = panel()["switch_lever"];
	const int pos = juce::jlimit(0, int(s.leverX.size()) - 1, position(s));
	const float w = num(l["w"], 5.5f), h = num(l["h"], 3.5f);
	const auto lever = view(juce::Rectangle<float>(w, h).withCentre({s.leverX[size_t(pos)], s.body.getCentreY()}));
	const auto face = colour("switch_lever");
	g.setGradientFill(juce::ColourGradient(face.brighter(hover ? 0.9f : 0.6f), lever.getX(), lever.getY(), face.darker(0.2f), lever.getX(), lever.getBottom(), false));
	g.fillRoundedRectangle(lever, 0.4f * kPx);
	// its ribs, its lit top edge
	g.setColour(juce::Colours::black.withAlpha(0.45f));
	for (int i = 1; i < 7; ++i) {
		const float x = lever.getX() + lever.getWidth() * float(i) / 7.0f;
		g.drawLine(x, lever.getY() + 2, x, lever.getBottom() - 2, 1.0f);
	}
	g.setColour(juce::Colours::white.withAlpha(0.45f));
	g.drawLine(lever.getX() + 2, lever.getY() + 1, lever.getRight() - 2, lever.getY() + 1, 1.2f);
}

void HardwareView::drawKey(juce::Graphics &g, const Key &k, bool down, bool hover) const
{
	auto r = view(k.box);
	const float dy = down ? 0.25f * kPx : 0.0f;
	const auto grey = colour("key");
	// the key's sides (darker, right and below), its face
	g.setColour(juce::Colours::black.withAlpha(0.45f));
	g.fillRoundedRectangle(r.translated(0.5f, 1.5f), 0.7f * kPx);
	g.setColour(grey.darker(0.55f));
	g.fillRoundedRectangle(r, 0.7f * kPx);
	auto face = r.withTrimmedRight(0.35f * kPx).withTrimmedBottom(0.45f * kPx).translated(0, dy);
	const auto top = down ? grey.darker(0.2f) : hover ? grey.brighter(0.15f) : grey;
	g.setGradientFill(juce::ColourGradient(top.brighter(0.12f), face.getX(), face.getY(), top.darker(0.08f), face.getX(), face.getBottom(), false));
	g.fillRoundedRectangle(face, 0.6f * kPx);
	g.setColour(juce::Colours::white.withAlpha(down ? 0.12f : 0.3f));
	g.drawLine(face.getX() + 3, face.getY() + 1.2f, face.getRight() - 3, face.getY() + 1.2f, 1.0f);
	const auto text = colour("key_text");
	if (k.legend.text.isNotEmpty()) {
		g.setColour(text);
		g.fillPath(textPath(k.legend.text, k.legend.x, k.legend.y, k.legend.cap, 0, 0.86f), juce::AffineTransform::translation(0, dy));
	}
	if (!k.note.isEmpty()) {
		// an eighth note: the head low left, the stem, the flag
		const auto b = k.note;
		const float hw = b.getWidth() * 0.62f, hh = b.getHeight() * 0.3f;
		juce::Path n;
		n.addEllipse(view(juce::Rectangle<float>(b.getX(), b.getBottom() - hh, hw, hh)));
		const float sx = b.getX() + hw - 0.22f;
		n.addRectangle(view(juce::Rectangle<float>(sx, b.getY(), 0.3f, b.getHeight() - hh * 0.5f)));
		juce::Path flag;
		flag.startNewSubPath(view(juce::Point<float>(sx + 0.15f, b.getY())));
		flag.quadraticTo(view(juce::Point<float>(b.getRight() + 0.1f, b.getY() + b.getHeight() * 0.3f)),
		                 view(juce::Point<float>(b.getRight() - 0.2f, b.getY() + b.getHeight() * 0.62f)));
		juce::PathStrokeType(0.32f * kPx).createStrokedPath(flag, flag);
		n.addPath(flag);
		g.setColour(text);
		g.fillPath(n, juce::AffineTransform::translation(0, dy));
	}
}

juce::String HardwareView::status() const
{
	if (!m_proc.romLoaded())
		return "NO ROM: add v1.5.bin to the ROM folder";
	if (m_held.anyLatched())
		return lemus::panel::heldStatus(m_held.latchedNames([this](int i) { return m_keys[size_t(i)].name; }));
	if (m_rear)
		return m_proc.filterInRouted() ? "THE REAR: the host's audio is in EXTERNAL AUDIO INPUT (the side-chain)"
		                               : "THE REAR: route a track to the side-chain to plug EXTERNAL AUDIO INPUT";
	// FILTER INPUT (the side-chain) isn't shown here: hosts keep it routed; REAR and the help say it
	return m_proc.asleep() ? "IDLE" : "";
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

// The control under the mouse (or being moved): its name and setting, or the key's name and its
// computer key, under it.
void HardwareView::drawTip(juce::Graphics &g)
{
	juce::String text;
	juce::Point<float> at;
	if (m_rear) {
		if (m_hoverJack < 0)
			return;
		const auto &j = (*panel()["rear"]["jacks"].getArray())[m_hoverJack];
		text = jackTip(m_hoverJack);
		at = view(juce::Point<float>(num(j["x"]), num(j["y"])) + rearOrigin() + juce::Point<float>(0, 7.4f));
		lemus::panel::drawTooltip(g, text, at, getLocalBounds().toFloat(), barMono());
		return;
	}
	if (const int k = m_dragKnob >= 0 ? m_dragKnob : m_hoverKnob; k >= 0) {
		const auto &knob = m_knobs[size_t(k)];
		if (knob.param == nullptr)
			return;
		text = knob.param->getName(100) + "  " + knob.param->getCurrentValueAsText();
		at = view(juce::Point<float>(knob.c.x, knob.c.y + knob.d / 2 + 0.6f));
	} else if (const int s = m_dragSwitch >= 0 ? m_dragSwitch : m_hoverSwitch; s >= 0) {
		const auto &sw = m_switches[size_t(s)];
		if (sw.param == nullptr)
			return;
		text = sw.param->getName(100) + "  " + sw.param->getCurrentValueAsText();
		at = view(juce::Point<float>(sw.body.getCentreX(), sw.body.getBottom() + 0.6f));
	} else if (m_hoverKey >= 0) {
		const auto &key = m_keys[size_t(m_hoverKey)];
		text = key.name + "   (" + key.computer + ")";
		at = view(juce::Point<float>(key.box.getCentreX(), key.box.getBottom() + 0.5f));
	} else {
		return;
	}
	lemus::panel::drawTooltip(g, text, at, getLocalBounds().toFloat(), barMono());
}

// ============================================================================== the rear

// The rear plate as panel.json's "rear" measures it: the print, the jacks, the MIDI and power
// sockets, the brand marks; a cable in EXTERNAL AUDIO INPUT while the host routes the side-chain.
void HardwareView::paintRear(juce::Graphics &g)
{
	const auto &r = panel()["rear"];
	const auto o = rearOrigin();
	const float pw = num(r["plate"][0], 440.0f), ph = num(r["plate"][1], 43.2f);
	const auto plate = colour("rear_plate"), ink = colour("rear_ink");
	g.setColour(juce::Colour(0xff141416)); // the rack around the body
	g.fillRect(0, 0, kWidth, kPanelHeight);
	{
		juce::Graphics::ScopedSaveState state(g);
		g.addTransform(juce::AffineTransform::translation(o.x, o.y).scaled(kPx));
		g.setGradientFill(juce::ColourGradient(plate.brighter(0.05f), 0, 0, plate.darker(0.06f), 0, ph, false));
		g.fillRect(0.0f, 0.0f, pw, ph);
		g.setColour(plate.darker(0.35f));
		g.fillRect(0.0f, 0.0f, pw, 0.35f);
		g.fillRect(0.0f, ph - 0.35f, pw, 0.35f);
		const float lw = num(r["frame_line_w"], 0.45f);
		juce::Path frames;
		for (const auto &f : *r["frames"].getArray())
			frames.addRectangle(cornersOf(f));
		for (const auto &d : *r["dividers"].getArray()) {
			frames.startNewSubPath(num(d[0]), num(d[1]));
			frames.lineTo(num(d[2]), num(d[3]));
		}
		g.setColour(ink);
		g.strokePath(frames, juce::PathStrokeType(lw));
		g.fillRect(cornersOf(r["module_box"]));

		auto circle = [](juce::Point<float> c, float d) { return juce::Rectangle<float>(d, d).withCentre(c); };
		const float ring = num(r["jack"]["ring_d"], 9.4f), hole = num(r["jack"]["hole_d"], 5.2f);
		const float shell = num(r["din"]["shell_d"], 14.0f), ins = num(r["din"]["insulator_d"], 7.2f);
		const float pinR = num(r["din"]["pin_circle_d"], 5.0f) / 2, pinD = num(r["din"]["pin_d"], 0.9f);
		const float powerD = num(r["power_socket"]["d"], 5.6f), powerPin = num(r["power_socket"]["pin_d"], 1.0f);
		auto metal = [&](juce::Point<float> c, float d, juce::Colour light, juce::Colour dark) {
			g.setGradientFill(juce::ColourGradient(light, c.x - d * 0.3f, c.y - d * 0.35f, dark, c.x + d * 0.35f, c.y + d * 0.4f, false));
			g.fillEllipse(circle(c, d));
		};
		for (const auto &j : *r["jacks"].getArray()) {
			const juce::Point<float> c(num(j["x"]), num(j["y"]));
			const auto kind = j["kind"].toString();
			if (kind == "jack") {
				g.setColour(juce::Colour(0xff0c0c0d));
				g.fillEllipse(circle(c, ring));
				metal(c, hole + 1.1f, juce::Colour(0xffc4c4c2), juce::Colour(0xff55555a));
				g.setColour(juce::Colour(0xff050505));
				g.fillEllipse(circle(c, hole - 0.5f));
			} else if (kind == "din") {
				metal(c, shell, juce::Colour(0xffd0d0ce), juce::Colour(0xff6c6c70));
				g.setColour(juce::Colour(0xff3a3a3e));
				g.drawEllipse(circle(c, shell - 0.3f), 0.3f);
				g.setColour(juce::Colour(0xff0e0e10));
				g.fillEllipse(circle(c, ins));
				g.setColour(juce::Colour(0xffb8b8b4)); // the key notch at the top, the five pins below
				g.fillRoundedRectangle(juce::Rectangle<float>(1.3f, 0.9f).withCentre(c + juce::Point<float>(0, -ins / 2 + 0.55f)), 0.2f);
				for (int k = 0; k < 5; ++k) {
					const float a = juce::MathConstants<float>::pi * float(k) / 4.0f;
					g.fillEllipse(circle(c + juce::Point<float>(pinR * std::cos(a), pinR * std::sin(a) * 0.95f + 0.3f), pinD));
				}
			} else if (kind == "power") {
				g.setColour(juce::Colour(0xff0c0c0d));
				g.fillEllipse(circle(c, powerD));
				metal(c, powerD * 0.62f, juce::Colour(0xff9a9a98), juce::Colour(0xff3c3c40));
				g.setColour(juce::Colour(0xff050505));
				g.fillEllipse(circle(c, powerD * 0.45f));
				metal(c, powerPin, juce::Colour(0xffe0e0dc), juce::Colour(0xff8a8a88));
			}
		}
	}
	// the print, in view px (textPath's units)
	{
		const auto &m = r["module_text"];
		const auto box = cornersOf(r["module_box"]);
		g.setColour(plate);
		g.fillPath(textPath(m["text"].toString(), o.x + box.getCentreX() - num(m["w"]) / 2, o.y + box.getCentreY(), num(m["cap"], 2.1f), 0, 0.86f, num(m["w"])));
	}
	g.setColour(ink);
	for (const auto &t : *r["texts"].getArray()) {
		// x is each line's centre; a measured width w fits it (textPath then takes its left edge)
		const auto align = t["align"].toString();
		const float w = num(t["w"]);
		g.fillPath(textPath(t["text"].toString(), o.x + num(t["x"]) - w / 2, o.y + num(t["y"]), num(t["cap"], 1.0f), align == "left" ? -1 : align == "right" ? 1 : 0,
		                    0.86f, w));
	}
	drawMarks(g, true);
	if (m_proc.filterInRouted())
		for (const auto &j : *r["jacks"].getArray())
			if (bool(j["cable"]))
				drawCable(g, juce::Point<float>(num(j["x"]), num(j["y"])) + o);
}

// A plug in the jack seen end on, its cable falling away below the body.
void HardwareView::drawCable(juce::Graphics &g, juce::Point<float> c) const
{
	juce::Graphics::ScopedSaveState state(g);
	g.reduceClipRegion(0, 0, kWidth, kPanelHeight);
	g.addTransform(juce::AffineTransform::scale(kPx));
	juce::Path run;
	run.startNewSubPath(c);
	run.cubicTo(c + juce::Point<float>(0.4f, 5.0f), c + juce::Point<float>(2.6f, 9.0f), c + juce::Point<float>(3.2f, 16.0f));
	const auto round = [](float w) { return juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
	g.setColour(juce::Colours::black.withAlpha(0.35f));
	g.strokePath(run, round(4.6f), juce::AffineTransform::translation(0.6f, 0.9f));
	g.setColour(juce::Colour(0xff0d0d0d));
	g.strokePath(run, round(3.4f));
	g.setColour(juce::Colour(0xff484848));
	g.strokePath(run, round(0.6f), juce::AffineTransform::translation(-0.6f, -0.3f));
	const float d = 10.6f; // the plug's boot, end on
	g.setColour(juce::Colours::black.withAlpha(0.45f));
	g.fillEllipse(juce::Rectangle<float>(d + 0.6f, d + 0.6f).withCentre(c + juce::Point<float>(0.5f, 0.9f)));
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3a3c), c.x - d * 0.3f, c.y - d * 0.35f, juce::Colour(0xff060606), c.x + d * 0.45f, c.y + d * 0.45f, true));
	g.fillEllipse(juce::Rectangle<float>(d, d).withCentre(c));
	juce::Path rim;
	rim.addCentredArc(c.x, c.y, d / 2 - 0.4f, d / 2 - 0.4f, 0, -1.9f, -0.2f, true);
	g.setColour(juce::Colour(0xff6a6a6e));
	g.strokePath(rim, round(0.45f));
}

int HardwareView::jackAt(juce::Point<float> viewMm) const
{
	const auto &r = panel()["rear"];
	const auto p = viewMm - rearOrigin();
	const auto &jacks = *r["jacks"].getArray();
	for (int i = 0; i < jacks.size(); ++i) {
		const auto &j = jacks[i];
		const auto kind = j["kind"].toString();
		const float d = kind == "din" ? num(r["din"]["shell_d"], 14.0f) : kind == "power" ? num(r["power_socket"]["d"], 5.6f) + 3.0f : num(r["jack"]["ring_d"], 9.4f);
		if (p.getDistanceFrom({num(j["x"]), num(j["y"])}) <= d / 2 + 0.6f)
			return i;
	}
	return -1;
}

juce::String HardwareView::jackTip(int jack) const
{
	const auto &j = (*panel()["rear"]["jacks"].getArray())[jack];
	if (bool(j["cable"]))
		return m_proc.filterInRouted() ? "FILTER: EXTERNAL AUDIO INPUT  the host's audio (the plugin's side-chain), into the filter"
		                               : "FILTER: EXTERNAL AUDIO INPUT  route a track to the plugin's side-chain to play it through the filter";
	return j["tip"].toString();
}

// ============================================================================== the mouse

int HardwareView::knobAt(juce::Point<float> mm) const
{
	for (size_t i = 0; i < m_knobs.size(); ++i)
		if (mm.getDistanceFrom(m_knobs[i].c) < m_knobs[i].d / 2 + 0.6f)
			return int(i);
	return -1;
}

int HardwareView::switchAt(juce::Point<float> mm) const
{
	for (size_t i = 0; i < m_switches.size(); ++i)
		if (m_switches[i].body.expanded(0.8f, 1.0f).contains(mm))
			return int(i);
	return -1;
}

int HardwareView::keyAt(juce::Point<float> mm) const
{
	for (size_t i = 0; i < m_keys.size(); ++i)
		if (m_keys[i].box.expanded(0.4f).contains(mm))
			return int(i);
	return -1;
}

int HardwareView::position(const Switch &s) const
{
	if (s.param == nullptr)
		return 0;
	return juce::roundToInt(s.param->convertFrom0to1(s.param->getValue()));
}

void HardwareView::setPosition(const Switch &s, int pos)
{
	if (s.param == nullptr)
		return;
	pos = juce::jlimit(0, int(s.leverX.size()) - 1, pos);
	if (pos == position(s))
		return;
	s.param->beginChangeGesture();
	s.param->setValueNotifyingHost(s.param->convertTo0to1(float(pos)));
	s.param->endChangeGesture();
}

int HardwareView::nearestPosition(const Switch &s, float xMm) const
{
	int best = 0;
	for (int p = 1; p < int(s.leverX.size()); ++p)
		if (std::abs(s.leverX[size_t(p)] - xMm) < std::abs(s.leverX[size_t(best)] - xMm))
			best = p;
	return best;
}

void HardwareView::setHover(juce::Point<float> at)
{
	const auto mm = toMm(at);
	const bool onPanel = at.y < float(kPanelHeight);
	if (m_rear) {
		const int jack = onPanel ? jackAt(mm) : -1;
		const int bar = onPanel ? -1 : m_bar.hit(at);
		if (jack != m_hoverJack || bar != m_hoverBar || m_hoverKnob >= 0 || m_hoverSwitch >= 0 || m_hoverKey >= 0) {
			m_hoverJack = jack;
			m_hoverBar = bar;
			m_hoverKnob = m_hoverSwitch = m_hoverKey = -1;
			repaint();
		}
		return;
	}
	const int knob = onPanel ? knobAt(mm) : -1;
	const int sw = onPanel && knob < 0 ? switchAt(mm) : -1;
	const int key = onPanel && knob < 0 && sw < 0 ? keyAt(mm) : -1;
	const int bar = onPanel ? -1 : m_bar.hit(at);
	if (knob != m_hoverKnob || sw != m_hoverSwitch || key != m_hoverKey || bar != m_hoverBar) {
		m_hoverKnob = knob;
		m_hoverSwitch = sw;
		m_hoverKey = key;
		m_hoverBar = bar;
		repaint();
	}
}

void HardwareView::hoverForTests(const juce::String &id)
{
	m_hoverKnob = m_hoverSwitch = m_hoverKey = -1;
	for (int i = 0; i < int(m_knobs.size()); ++i)
		if (m_knobs[size_t(i)].param != nullptr && m_knobs[size_t(i)].param->getParameterID() == id)
			m_hoverKnob = i;
	for (int i = 0; i < int(m_switches.size()); ++i)
		if (m_switches[size_t(i)].param != nullptr && m_switches[size_t(i)].param->getParameterID() == id)
			m_hoverSwitch = i;
	for (int i = 0; i < int(m_keys.size()); ++i)
		if (m_keys[size_t(i)].id == id)
			m_hoverKey = i;
	repaint();
}

void HardwareView::showHelpForTests(bool on)
{
	m_help = on;
	repaint();
}

void HardwareView::mouseMove(const juce::MouseEvent &e) { setHover(e.position); }

void HardwareView::mouseExit(const juce::MouseEvent &)
{
	m_hoverKnob = m_hoverSwitch = m_hoverKey = m_hoverBar = -1;
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
			showRear(!m_rear);
		repaint();
		return;
	}
	if (m_rear) // the jacks take no clicks: the side-chain's routing is the host's
		return;
	const auto mm = toMm(e.position);
	if (const int k = knobAt(mm); k >= 0 && m_knobs[size_t(k)].param != nullptr) {
		m_dragKnob = k;
		m_dragStart = e.position.y;
		m_dragValue = m_knobs[size_t(k)].param->getValue();
		m_knobs[size_t(k)].param->beginChangeGesture();
		return;
	}
	if (const int s = switchAt(mm); s >= 0) {
		// a slide switch: pushed a position towards the click; a two-position one flips
		const auto &sw = m_switches[size_t(s)];
		m_dragSwitch = s;
		m_switchMoved = false;
		m_switchFrom = e.position.x;
		const int pos = position(sw);
		if (sw.leverX.size() == 2)
			setPosition(sw, 1 - pos);
		else if (!sw.leverX.empty())
			setPosition(sw, mm.x < sw.leverX[size_t(juce::jlimit(0, int(sw.leverX.size()) - 1, pos))] ? pos - 1 : pos + 1);
		repaint();
		return;
	}
	if (const int k = keyAt(mm); k >= 0 && m_held.mouseDown(k, e.mods))
		m_mouseKey = k;
	repaint();
}

void HardwareView::mouseDrag(const juce::MouseEvent &e)
{
	if (m_dragKnob >= 0) {
		m_dragValue = juce::jlimit(0.0f, 1.0f, m_dragValue + (m_dragStart - e.position.y) / (e.mods.isShiftDown() ? 1000.0f : 200.0f));
		m_dragStart = e.position.y;
		m_knobs[size_t(m_dragKnob)].param->setValueNotifyingHost(m_dragValue);
		repaint();
		return;
	}
	if (m_dragSwitch >= 0) {
		// the lever follows the mouse to the nearest position
		if (std::abs(e.position.x - m_switchFrom) > 3)
			m_switchMoved = true;
		if (m_switchMoved) {
			const auto &sw = m_switches[size_t(m_dragSwitch)];
			setPosition(sw, nearestPosition(sw, toMm(e.position).x));
			repaint();
		}
	}
}

void HardwareView::mouseUp(const juce::MouseEvent &)
{
	if (m_dragKnob >= 0) {
		m_knobs[size_t(m_dragKnob)].param->endChangeGesture();
		m_dragKnob = -1;
	}
	m_dragSwitch = -1;
	if (m_mouseKey >= 0) {
		m_held.mouseUp(m_mouseKey);
		m_mouseKey = -1;
	}
	repaint();
}

void HardwareView::mouseDoubleClick(const juce::MouseEvent &e)
{
	if (e.position.y >= float(kPanelHeight) || m_rear)
		return;
	if (const int k = knobAt(toMm(e.position)); k >= 0 && m_knobs[size_t(k)].param != nullptr) {
		auto *p = m_knobs[size_t(k)].param;
		p->beginChangeGesture();
		p->setValueNotifyingHost(p->getDefaultValue());
		p->endChangeGesture();
		repaint();
	}
}

void HardwareView::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
	if (e.position.y >= float(kPanelHeight) || m_rear)
		return;
	const auto mm = toMm(e.position);
	if (const int k = knobAt(mm); k >= 0 && m_knobs[size_t(k)].param != nullptr) {
		auto *p = m_knobs[size_t(k)].param;
		p->beginChangeGesture();
		p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->getValue() + (w.deltaY + w.deltaX) * (e.mods.isShiftDown() ? 0.05f : 0.25f)));
		p->endChangeGesture();
		repaint();
		return;
	}
	if (const int s = switchAt(mm); s >= 0 && std::abs(w.deltaY + w.deltaX) > 1e-6f) {
		const auto &sw = m_switches[size_t(s)];
		setPosition(sw, position(sw) + (w.deltaY + w.deltaX > 0 ? 1 : -1)); // up: to the right
		repaint();
	}
}

// ============================================================================== the computer keys

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
	if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown())
		return false;
	int key = -1;
	// the keypad by place (the US keyboard's digits, - and =), or the numeric keypad's
	const int code = k.getKeyCode();
	if (code >= juce::KeyPress::numberPad0 && code <= juce::KeyPress::numberPad9) {
		const int d = code - juce::KeyPress::numberPad0;
		key = d == 0 ? kKey0 : kKey1 + d - 1;
	} else if (code == juce::KeyPress::numberPadSubtract) {
		key = kKeyMinus;
	} else if (code == juce::KeyPress::numberPadAdd) {
		key = kKeyPlus;
	} else if (const auto c = lemus::panel::usKey(k); c >= '1' && c <= '9') {
		key = kKey1 + int(c - '1');
	} else if (c == '0') {
		key = kKey0;
	} else if (c == '-') {
		key = kKeyMinus;
	} else if (c == '=') {
		key = kKeyPlus;
	} else {
		// AUDITION and MODE by their labels' letters, as typed
		const auto t = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
		if (t == 'a')
			key = kAudition;
		else if (t == 'm')
			key = kMode;
	}
	if (key < 0)
		return false; // Space and the rest go on to the host
	m_held.keyDown(code, key);
	repaint();
	return true;
}

bool HardwareView::keyStateChanged(bool)
{
	if (!m_held.keysChanged())
		return false;
	repaint();
	return true;
}

void HardwareView::focusLost(FocusChangeType)
{
	m_held.focusLost();
	repaint();
}

} // namespace bsrack
