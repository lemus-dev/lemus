#include "HardwareView.h"

#include "XpanderFonts.h"
#include "XpanderPanel.h"

#include <cmath>
#include <map>

namespace xpander {

namespace {

constexpr float kPx = HardwareView::kPxPerMm;
constexpr float kPi = 3.14159265f;
// The rear, seen from behind: the jack strip (47.5 mm), the hinge (6) and the lower panel (62.5),
// centred in the panel's area.
constexpr float kRearH = 116.0f, kRearTop = (HardwareView::kUnitH - kRearH) / 2, kLowerTop = 53.5f;
// An encoder: 30 detents a turn; a drag of this many px (or wheel units) turns it one.
constexpr float kDetentDeg = 12.0f, kDragPerDetent = 7.0f, kFineDragPerDetent = 24.0f, kWheelPerDetent = 0.06f;

// docs/panel.json, built in.
const juce::var &panel()
{
	static const juce::var v = juce::JSON::parse(juce::String::fromUTF8(XpanderPanel::panel_json, XpanderPanel::panel_jsonSize));
	return v;
}
const juce::Array<juce::var> &elements()
{
	static const juce::Array<juce::var> none;
	const auto *a = panel()["elements"].getArray();
	return a != nullptr ? *a : none;
}

float num(const juce::var &v, float fallback = 0) { return v.isVoid() || v.isUndefined() ? fallback : float(double(v)); }
// [x0, y0, x1, y1]; an empty box where an element has none
juce::Rectangle<float> bboxOf(const juce::var &b)
{
	if (!b.isArray() || b.size() < 4)
		return {};
	return {num(b[0]), num(b[1]), num(b[2]) - num(b[0]), num(b[3]) - num(b[1])};
}
juce::Rectangle<float> circle(juce::Point<float> c, float r) { return {c.x - r, c.y - r, 2 * r, 2 * r}; }
juce::Colour colour(const juce::String &name)
{
	static std::map<juce::String, juce::Colour> cache;
	if (const auto it = cache.find(name); it != cache.end())
		return it->second;
	const auto s = panel()["colours"][juce::Identifier(name)]["hex"].toString();
	const auto c = s.isEmpty() ? juce::Colours::magenta : juce::Colour::fromString("ff" + s.fromFirstOccurrenceOf("#", false, false));
	cache[name] = c;
	return c;
}

// ---------------------------------------------------------------------------------- fonts

juce::Typeface::Ptr load(const char *data, int size) { return juce::Typeface::createSystemTypefaceFor(data, size_t(size)); }

// IBM Plex Mono for the bar, the tips and the key list: size in px, weight 0 regular, 1 medium, 2
// semibold.
juce::Font mono(float size, int weight)
{
	static const auto regular = load(XpanderFonts::IBMPlexMonoRegular_ttf, XpanderFonts::IBMPlexMonoRegular_ttfSize);
	static const auto medium = load(XpanderFonts::IBMPlexMonoMedium_ttf, XpanderFonts::IBMPlexMonoMedium_ttfSize);
	static const auto semibold = load(XpanderFonts::IBMPlexMonoSemiBold_ttf, XpanderFonts::IBMPlexMonoSemiBold_ttfSize);
	return juce::Font(juce::FontOptions(weight == 2 ? semibold : weight == 1 ? medium : regular).withHeight(size));
}
const lemus::panel::Mono &barMono()
{
	static const lemus::panel::Mono m = [](float size, int weight) { return mono(size, weight); };
	return m;
}

// The panel's print, Helvetica Condensed, stood in for by Nimbus Sans Narrow (fonts/README.md).
juce::Path textPath(const juce::String &text, bool bold)
{
	static const auto regular = load(XpanderFonts::NimbusSansNarrowRegular_ttf, XpanderFonts::NimbusSansNarrowRegular_ttfSize);
	static const auto heavy = load(XpanderFonts::NimbusSansNarrowBold_ttf, XpanderFonts::NimbusSansNarrowBold_ttfSize);
	const juce::Font font(juce::FontOptions(bold ? heavy : regular).withHeight(100.0f));
	juce::GlyphArrangement ga;
	ga.addLineOfText(font, text, 0, 0);
	juce::Path p;
	ga.createPath(p);
	return p;
}
// The capital height of the 100 px font.
float capHeight(bool bold)
{
	static const float regular = textPath("H", false).getBounds().getHeight(), heavy = textPath("H", true).getBounds().getHeight();
	return bold ? heavy : regular;
}

// A legend fitted to its measured ink box (panel.json's bbox: capitals, parentheses and figures
// included), with a little stroke for the print's medium weight.
void fillText(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> ink, juce::Colour c, bool bold = false, float weight = 0)
{
	auto p = textPath(text, bold);
	const auto b = p.getBounds();
	if (b.isEmpty() || ink.isEmpty())
		return;
	p.applyTransform(juce::AffineTransform::translation(-b.getX(), -b.getY())
	                     .scaled(ink.getWidth() / b.getWidth(), ink.getHeight() / b.getHeight())
	                     .translated(ink.getX(), ink.getY()));
	g.setColour(c);
	g.fillPath(p);
	if (weight > 0)
		g.strokePath(p, juce::PathStrokeType(weight, juce::PathStrokeType::mitered));
}

// A legend at a cap height, its capitals centred on `at` (left: its left edge at at.x); `squeeze`
// narrows it.
void fillTextAt(juce::Graphics &g, const juce::String &text, juce::Point<float> at, float cap, juce::Colour c, bool left = false,
                float squeeze = 1.0f, float weight = 0.05f, bool bold = false)
{
	auto p = textPath(text, bold);
	const auto b = p.getBounds();
	if (b.isEmpty())
		return;
	const float s = cap / capHeight(bold);
	const float x = left ? at.x : at.x - b.getWidth() * s * squeeze / 2;
	// the baseline is at y = 0 in the path
	p.applyTransform(juce::AffineTransform::translation(-b.getX(), 0).scaled(s * squeeze, s).translated(x, at.y + cap / 2));
	g.setColour(c);
	g.fillPath(p);
	if (weight > 0)
		g.strokePath(p, juce::PathStrokeType(weight, juce::PathStrokeType::mitered));
}

float textWidth(const juce::String &text, float cap, bool bold = false) { return textPath(text, bold).getBounds().getWidth() * cap / capHeight(bold); }

void strokePolyline(juce::Graphics &g, const juce::var &points, float stroke, juce::Colour c)
{
	if (!points.isArray() || points.size() < 2)
		return;
	juce::Path p;
	for (int i = 0; i < points.size(); ++i) {
		const juce::Point<float> q(num(points[i][0]), num(points[i][1]));
		if (i == 0)
			p.startNewSubPath(q);
		else
			p.lineTo(q);
	}
	g.setColour(c);
	g.strokePath(p, juce::PathStrokeType(stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}

// ---------------------------------------------------------------------------------- the VFDs

// The FG405A2's character: 14 segments, the point and the underline, as MAME numbers them (0 top,
// 1 right top, 2 right bottom, 3 bottom, 4 left bottom, 5 left top, 6 middle left, 7 middle right,
// 8 centre top, 9 centre bottom, 10 diagonal left bottom, 11 diagonal left top, 12 diagonal right
// top, 13 diagonal right bottom, 14 point, 15 underline), with the anode bit that lights each:
// MAME's bitswap<15>(data, 1,3,6,5,4,2,7,12,11,10,13,15,14,9,8), and bit 0 the underline.
constexpr int kSegmentBit[16] = {8, 9, 14, 15, 13, 10, 11, 12, 7, 2, 4, 5, 6, 3, 1, 0};

struct Glyph {
	std::array<juce::Path, 16> core, glow; // around the character's centre x and its top
};

// panel.json's character cell: 3.0 x 6.5 mm, slanted 10 degrees, 0.45 mm strokes; the point after
// the lower right corner, the underline 7.5 mm below the top.
const Glyph &glyph()
{
	static const Glyph g = [] {
		Glyph out;
		const juce::var *vfd = nullptr;
		for (const auto &e : elements())
			if (e["type"].toString() == "vfd" && vfd == nullptr)
				vfd = &e;
		const auto &c = vfd != nullptr ? (*vfd)["chars"] : juce::var();
		const float w = num(c["cell_w"], 3.0f), h = num(c["cell_h"], 6.5f), stroke = num(c["stroke"], 0.45f);
		const float slant = std::tan(num(c["slant_deg"], 10.0f) * kPi / 180);
		const float hw = w / 2, mid = h / 2, e = 0.42f, d = 0.32f;
		const auto shear = [&](float x, float y) { return juce::Point<float>(x + (mid - y) * slant, y); };
		const float seg[14][4] = {
			{-hw + e, 0, hw - e, 0},          {hw, e, hw, mid - e},           {hw, mid + e, hw, h - e},
			{-hw + e, h, hw - e, h},          {-hw, mid + e, -hw, h - e},     {-hw, e, -hw, mid - e},
			{-hw + e, mid, -d, mid},          {d, mid, hw - e, mid},          {0, e, 0, mid - e},
			{0, mid + e, 0, h - e},           {-d, mid + e, -hw + e, h - e},  {-hw + e, e, -d, mid - e},
			{hw - e, e, d, mid - e},          {d, mid + e, hw - e, h - e},
		};
		const juce::PathStrokeType core(stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded);
		const juce::PathStrokeType glow(stroke * 2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
		for (int s = 0; s < 14; ++s) {
			juce::Path line;
			line.startNewSubPath(shear(seg[s][0], seg[s][1]));
			line.lineTo(shear(seg[s][2], seg[s][3]));
			core.createStrokedPath(out.core[size_t(s)], line);
			glow.createStrokedPath(out.glow[size_t(s)], line);
		}
		const float dpD = num(c["dp"]["d"], 0.6f);
		const auto dp = shear(num(c["dp"]["dx"], 1.9f), num(c["dp"]["dy"], 6.3f));
		out.core[14].addEllipse(circle(dp, dpD / 2));
		out.glow[14].addEllipse(circle(dp, dpD * 1.2f));
		const float uy = num(c["underline"]["dy"], 7.5f), uw = num(c["underline"]["w"], w) / 2;
		juce::Path under;
		under.startNewSubPath(shear(-uw + 0.1f, uy));
		under.lineTo(shear(uw - 0.1f, uy));
		juce::PathStrokeType(num(c["underline"]["stroke"], stroke), juce::PathStrokeType::mitered, juce::PathStrokeType::butt).createStrokedPath(out.core[15], under);
		glow.createStrokedPath(out.glow[15], under);
		return out;
	}();
	return g;
}

// ---------------------------------------------------------------------------------- names and keys

juce::String nameOf(const juce::var &e)
{
	const auto id = e["id"].toString();
	const auto label = e["label"].toString();
	if (id.startsWith("btn_key_"))
		return "KEYPAD " + e["cap"]["print"]["text"].toString();
	if (id.startsWith("btn_voice_"))
		return "PATCH EDIT " + id.getLastCharacters(1);
	if (id.startsWith("btn_p1_"))
		return "UPPER SOFT KEY " + id.getLastCharacters(1);
	if (id.startsWith("btn_p2_"))
		return "LOWER SOFT KEY " + id.getLastCharacters(1);
	if (id.startsWith("btn_mod_")) {
		const auto below = e["legend_below"].toString();
		return e["legend_above"].toString() + " / " + (below.length() == 1 ? "X SELECT " + below : below);
	}
	if (label.contains(" / ")) {
		const auto a = label.upToFirstOccurrenceOf(" / ", false, false), b = label.fromFirstOccurrenceOf(" / ", false, false);
		return a == b ? a : label;
	}
	return label;
}

// The computer keys: the keypad, STORE, CLEAR and the keys used most.
struct ComputerKey {
	juce::juce_wchar ch;
	const char *button;
	const char *shown;
};
const ComputerKey kComputerKeys[] = {
	{'0', "btn_key_0", "0"},     {'1', "btn_key_1", "1"},      {'2', "btn_key_2", "2"},      {'3', "btn_key_3", "3"},
	{'4', "btn_key_4", "4"},     {'5', "btn_key_5", "5"},      {'6', "btn_key_6", "6"},      {'7', "btn_key_7", "7"},
	{'8', "btn_key_8", "8"},     {'9', "btn_key_9", "9"},      {'+', "btn_key_plus", "+"},   {'=', "btn_key_plus", "="},
	{'-', "btn_key_minus", "-"}, {'s', "btn_single", "S"},     {'m', "btn_multi", "M"},      {'t', "btn_tune", "T"},
	{'y', "btn_master", "Y"},    {'p', "btn_page2", "P"},      {'v', "btn_vco1", "V"},       {'b', "btn_vco2", "B"},
	{'f', "btn_fm_lag", "F"},    {'k', "btn_track_x", "K"},    {'c', "btn_vcf_vca", "C"},    {'e', "btn_env_x", "E"},
	{'l', "btn_lfo_x", "L"},     {'r', "btn_ramp_x", "R"},     {'i', "btn_misc", "I"},
};
constexpr const char *kStore = "btn_store", *kClear = "btn_mod_ramp";

} // namespace

// ====================================================================================== setup

HardwareView::HardwareView(XpanderProcessor &p) : m_proc(p)
{
	for (const auto &e : elements()) {
		const auto type = e["type"].toString();
		const juce::Point<float> c(num(e["x"]), num(e["y"]));
		if (type == "button") {
			Button b;
			b.id = e["id"].toString();
			b.name = nameOf(e);
			b.c = c;
			const float hole = num(e["hole"], 16.2f);
			b.hole = juce::Rectangle<float>(hole, hole).withCentre(c);
			b.column = e["matrix"]["port"].toString().getTrailingIntValue();
			b.row = int(num(e["matrix"]["bit"]));
			b.red = e["cap"]["colour"].toString() == "cap_red";
			const auto &print = e["cap"]["print"];
			if (print.isObject()) {
				b.print = print["text"].toString();
				b.printBox = juce::Rectangle<float>(num(print["w"], 4.0f), num(print["cap_h"], 6.4f))
				                 .withCentre(c + juce::Point<float>(num(print["dx"]), num(print["dy"])));
			}
			m_buttons.push_back(b);
		} else if (type == "led") {
			m_leds.push_back({c, num(e["d"], 3.8f), e["latch"]["name"].toString() == "LED2" ? 2 : 1, int(num(e["latch"]["bit"]))});
		} else if (type == "encoder") {
			m_knobs.push_back({c, int(num(e["encoder"]))});
		} else if (type == "knob") {
			m_knobs.push_back({c, -1});
		} else if (type == "vfd") {
			const int d = e["id"].toString().getTrailingIntValue() - 1;
			if (d >= 0 && d < 3) {
				const auto &ch = e["chars"];
				m_vfds[size_t(d)] = {bboxOf(e["window"]["bbox"]), num(ch["first_centre_x"]), num(ch["pitch"], 4.6f), num(ch["top_y"])};
			}
		}
	}
	auto index = [this](const char *id) {
		for (size_t i = 0; i < m_buttons.size(); ++i)
			if (m_buttons[i].id == id)
				return int(i);
		return -1;
	};
	for (const auto &k : kComputerKeys)
		if (const int i = index(k.button); i >= 0 && m_buttons[size_t(i)].computerKey.isEmpty())
			m_buttons[size_t(i)].computerKey = k.shown;
	if (const int i = index(kStore); i >= 0)
		m_buttons[size_t(i)].computerKey = "Return";
	if (const int i = index(kClear); i >= 0)
		m_buttons[size_t(i)].computerKey = "Backspace";

	m_volume = p.params().getParameter("volume");
	m_pacer.tick = [this] { return drainDetents(); };
	m_held.reset(m_buttons.size(), [this](int i, bool down) {
		const auto &b = m_buttons[size_t(i)];
		m_proc.setSwitch(b.column, b.row, down);
		repaintMm(b.hole);
	});
	m_barRear = m_bar.add(lemus::panel::Bar::kRear, "The rear panel: the jacks, MIDI, MEMORY PROTECT and POWER (click them to switch)");
	m_shownProtect = m_proc.memoryProtect();
	setWantsKeyboardFocus(true);
	setOpaque(true);
	startTimerHz(30);
}

HardwareView::~HardwareView()
{
	stopTimer();
	m_pacer.stopTimer();
	m_held.releaseAll(true); // no key stays down behind a closed window
}

void HardwareView::showRear(bool on)
{
	m_rear = on;
	repaint();
}

void HardwareView::showHelp(bool on)
{
	m_help = on;
	repaint();
}

bool HardwareView::pressForTests(const juce::String &id, bool down)
{
	for (size_t i = 0; i < m_buttons.size(); ++i)
		if (m_buttons[i].id == id) {
			m_held.set(int(i), down);
			return true;
		}
	return false;
}

void HardwareView::turnForTests(int encoder, int detents)
{
	for (size_t i = 0; i < m_knobs.size(); ++i)
		if (m_knobs[i].encoder == encoder)
			turn(int(i), detents);
}

bool HardwareView::held(const Button &b, const XpanderProcessor::Display &d) const
{
	return ((d.switches[size_t(b.column & 7)] >> b.row) & 1) != 0;
}

bool HardwareView::lit(const Led &l, const XpanderProcessor::Display &d) const
{
	return (((l.latch == 2 ? d.led2 : d.led1) >> l.bit) & 1) == 0; // active low
}

float HardwareView::knobDegrees(const Knob &k) const
{
	if (k.encoder >= 0)
		return kDetentDeg * float(m_encoderDetents[size_t(k.encoder)]);
	return m_volume != nullptr ? -150.0f + 300.0f * m_volume->getValue() : 0.0f;
}

void HardwareView::repaintMm(juce::Rectangle<float> mm) { repaint((mm * kPx).getSmallestIntegerContainer().expanded(2)); }

// What moved since the last tick: only that is repainted.
void HardwareView::timerCallback()
{
	const auto d = m_proc.display();
	if (!m_rear) {
		for (size_t v = 0; v < m_vfds.size(); ++v)
			if (d.vfd[v] != m_display.vfd[v])
				repaintMm(m_vfds[v].window);
		for (const auto &l : m_leds)
			if (lit(l, d) != lit(l, m_display))
				repaintMm(circle(l.c, l.d * 1.2f));
		for (const auto &b : m_buttons)
			if (held(b, d) != held(b, m_display))
				repaintMm(b.hole);
	}
	m_display = d;
	const float volume = m_volume != nullptr ? m_volume->getValue() : 0.0f;
	if (std::abs(volume - m_shownVolume) > 1e-6f) {
		m_shownVolume = volume; // the host or a preset moved it
		if (!m_rear)
			for (const auto &k : m_knobs)
				if (k.encoder < 0)
					repaintMm(circle(k.c, 15.0f));
	}
	if (m_proc.powered() != m_shownPower) {
		m_shownPower = m_proc.powered();
		repaint();
	}
	if (m_proc.memoryProtect() != m_shownProtect) {
		m_shownProtect = m_proc.memoryProtect();
		repaint();
	}
	if (const auto s = status(); s != m_shownStatus) {
		m_shownStatus = s;
		repaint(m_bar.area().toNearestInt());
	}
}

// ====================================================================================== painting

void HardwareView::paint(juce::Graphics &g)
{
	// The panel (or the rear) as printed, at the screen's scale, drawn once.
	const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
	auto &image = m_rear ? m_rearImage : m_panelImage;
	auto &imageScale = m_rear ? m_rearScale : m_panelScale;
	if (image.isNull() || std::abs(scale - imageScale) > 0.01f) {
		imageScale = scale;
		image = juce::Image(juce::Image::RGB, int(std::ceil(float(kWidth) * scale)), int(std::ceil(float(kPanelHeight) * scale)), true);
		juce::Graphics ig(image);
		ig.addTransform(juce::AffineTransform::scale(scale * kPx));
		if (m_rear)
			renderRear(ig);
		else
			renderPanel(ig);
	}
	{
		juce::Graphics::ScopedSaveState s(g);
		g.reduceClipRegion(0, 0, kWidth, kPanelHeight);
		g.drawImage(image, juce::Rectangle<float>(0, 0, float(image.getWidth()) / scale, float(image.getHeight()) / scale));

		// What moves, over it: only what the clip reaches.
		const auto clip = g.getClipBounds().toFloat() / kPx;
		g.addTransform(juce::AffineTransform::scale(kPx));
		if (m_rear) {
			drawProtect(g);
		} else {
			for (int v = 0; v < 3; ++v)
				if (clip.intersects(m_vfds[size_t(v)].window))
					drawVfd(g, v);
			for (const auto &l : m_leds)
				if (lit(l, m_display) && clip.intersects(circle(l.c, l.d)))
					drawLitLed(g, l);
			for (size_t i = 0; i < m_buttons.size(); ++i) {
				const auto &b = m_buttons[i];
				const bool down = m_held.down(int(i)) || held(b, m_display), hover = int(i) == m_hoverButton;
				if ((down || hover) && clip.intersects(b.hole))
					drawCap(g, b, down, hover);
			}
			for (size_t i = 0; i < m_knobs.size(); ++i)
				if (clip.intersects(circle(m_knobs[i].c, 14.0f)))
					drawKnob(g, m_knobs[i], int(i) == m_hoverKnob || int(i) == m_dragKnob);
		}
	}
	drawTip(g);
	drawBar(g);
	if (m_help)
		lemus::panel::drawHelp(
			g, getLocalBounds().toFloat().withTrimmedBottom(kBarHeight), "XPANDER",
			{"0-9  +  -       the keypad (= is + too; the number pad works)",
			 "Return          STORE                  Backspace, Delete   CLEAR (the RAMP X / CLEAR key)",
			 "S  M            SINGLE PATCH, MULTI PATCH                  T  Y   TUNE PAGE, MASTER PAGE",
			 "P               PAGE 2",
			 "V  B  F  K      the pages VCO1, VCO2, FM/LAG, TRACK X",
			 "C  E  L  R  I   the pages VCF/VCA, ENV X, LFO X, RAMP X, MISC.",
			 "Encoders 1-6: drag up or down, or the wheel: they turn in detents (shift: slower). VOLUME is a knob.",
			 "REAR: the back of the unit; click POWER to switch it, MEMORY PROTECT to slide it ON or OFF"},
			barMono());
}

void HardwareView::drawMark(juce::Graphics &g, const juce::String &place, juce::Rectangle<float> boxMm, bool fitOutline) const
{
	// the places the driver lists (xpander.cpp), by the marks they hold
	const juce::String mark = place == "maker" ? "oberheim" : place == "name" ? "xpander" : "";
	const auto *m = mark.isEmpty() ? nullptr : m_proc.artwork().find(mark);
	if (m == nullptr)
		return;
	if (m->picture != nullptr) {
		m->picture->drawWithin(g, boxMm);
	} else if (!m->outline.isEmpty()) {
		// a pack's outline, in the print's colour: on the top panel where the pack draws it (its
		// units are the panel's mm), on the rear fitted to the box
		auto p = m->outline;
		if (fitOutline)
			p.applyTransform(juce::RectanglePlacement(juce::RectanglePlacement::centred).getTransformToFit(p.getBounds(), boxMm));
		g.setColour(colour("print_white"));
		g.fillPath(p);
	}
}

// The top view, in mm: everything that doesn't move, and the keys up.
void HardwareView::renderPanel(juce::Graphics &g)
{
	g.fillAll(colour("case_black"));
	std::vector<const juce::var *> print;
	for (const auto &e : elements()) {
		const auto type = e["type"].toString();
		const auto box = bboxOf(e["bbox"]);
		if (type == "case") {
			g.setColour(colour(e["colour"].toString()));
			g.fillRoundedRectangle(box, num(e["corner_r"]));
		} else if (type == "region") {
			const auto name = e["colour"].toString();
			g.setColour(colour(name));
			g.fillRect(box);
			if (name == "wood") {
				// oiled wood: a few grain lines along the end-bell, its inner edge in shade
				juce::Random rng(int(box.getX() * 10) + 7);
				for (int i = 0; i < 9; ++i) {
					const float x = box.getX() + box.getWidth() * (0.08f + 0.84f * rng.nextFloat());
					juce::Path grain;
					grain.startNewSubPath(x, box.getY());
					for (float y = box.getY(); y <= box.getBottom(); y += 20.0f)
						grain.lineTo(x + 0.6f * std::sin(y * 0.05f + float(i)), y);
					g.setColour(colour("wood_shade").withAlpha(0.18f + 0.2f * rng.nextFloat()));
					g.strokePath(grain, juce::PathStrokeType(0.25f + 0.35f * rng.nextFloat()));
				}
				const bool left = box.getX() < 1;
				const auto inner = left ? box.withLeft(box.getRight() - 1.2f) : box.withWidth(1.2f);
				g.setColour(colour("wood_shade").withAlpha(0.8f));
				g.fillRect(inner);
				g.setColour(juce::Colours::white.withAlpha(0.06f));
				g.fillRect(left ? box.withWidth(0.6f) : box.withLeft(box.getRight() - 0.6f));
			} else if (e["id"].toString().startsWith("rail_")) {
				// the extrusion's edges catch a little light
				g.setColour(juce::Colours::white.withAlpha(0.07f));
				g.fillRect(box.withHeight(0.5f));
				g.setColour(juce::Colours::black.withAlpha(0.35f));
				g.fillRect(box.withTop(box.getBottom() - 0.6f));
			}
		} else if (type == "fill") {
			g.setColour(colour(e["colour"].toString()));
			g.fillRect(box);
		} else if (type == "opening") {
			g.setColour(colour("hole_black"));
			g.fillRect(box);
		} else if (type == "line" || type == "outline_box" || type == "symbol" || type == "knob_symbol" || type == "text") {
			print.push_back(&e);
		} else if (type == "logo") {
			drawMark(g, e["place"].toString(), box);
		}
	}

	// the keys' square holes (in the shared openings, their cells)
	g.setColour(colour("hole_black"));
	for (const auto &b : m_buttons)
		g.fillRoundedRectangle(b.hole, 0.4f);

	// the VFD windows, the grid of unlit segments showing through the filter
	const auto &gl = glyph();
	for (const auto &v : m_vfds) {
		g.setColour(colour("vfd_window"));
		g.fillRoundedRectangle(v.window, 0.8f);
		juce::Path unlit;
		for (int i = 0; i < 40; ++i)
			for (const auto &s : gl.core)
				unlit.addPath(s, juce::AffineTransform::translation(v.firstX + float(i) * v.pitch, v.top));
		g.setColour(colour("vfd_window").interpolatedWith(colour("vfd_unlit"), 0.75f));
		g.fillPath(unlit);
		// the cut-out's edge: a little shade along the top
		g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.35f), 0, v.window.getY(), juce::Colours::transparentBlack, 0,
		                                       v.window.getY() + 2.5f, false));
		g.fillRoundedRectangle(v.window, 0.8f);
	}

	// the LEDs, off
	for (const auto &l : m_leds) {
		const auto r = circle(l.c, l.d / 2);
		g.setColour(colour("hole_black"));
		g.fillEllipse(r.expanded(0.25f));
		g.setGradientFill(juce::ColourGradient(colour("led_off").brighter(0.25f), l.c.x - 0.5f, l.c.y - 0.6f, colour("led_off").darker(0.4f), l.c.x + 1.6f,
		                                       l.c.y + 1.6f, true));
		g.fillEllipse(r);
	}

	// the print
	for (const auto *pe : print) {
		const auto &e = *pe;
		const auto type = e["type"].toString();
		const auto c = colour(e["colour"].toString());
		if (type == "line") {
			strokePolyline(g, e["points"], num(e["stroke"], 0.6f), c);
		} else if (type == "outline_box") {
			juce::Path p;
			p.addRectangle(bboxOf(e["bbox"]));
			g.setColour(c);
			g.strokePath(p, juce::PathStrokeType(num(e["stroke"], 0.6f), juce::PathStrokeType::mitered));
		} else if (type == "symbol") {
			// the FM DESTINATION switch: a pivot, two throws, the lever
			g.setColour(c);
			const auto &pivot = e["pivot"];
			g.drawEllipse(circle({num(pivot["x"]), num(pivot["y"])}, num(pivot["r"], 0.7f)), 0.35f);
			if (const auto *throws = e["throws"].getArray())
				for (const auto &t : *throws)
					g.drawEllipse(circle({num(t["x"]), num(t["y"])}, num(t["r"], 0.7f)), 0.35f);
			strokePolyline(g, e["lever"], 0.45f, c);
		} else if (type == "knob_symbol") {
			// a printed knob: its ring, the pointer to the lower left, the encoder's number inside
			const juce::Point<float> at(num(e["x"]), num(e["y"]));
			const float r = num(e["r"], 3.11f), stroke = num(e["stroke"], 0.45f);
			g.setColour(c);
			g.drawEllipse(circle(at, r), stroke);
			const auto &ptr = e["pointer"];
			const float a = num(ptr["deg"], 217) * kPi / 180;
			const juce::Point<float> u(std::sin(a), -std::cos(a));
			const auto p0 = at + u * num(ptr["r0"], 1.65f), p1 = at + u * num(ptr["r1"], r);
			g.drawLine(p0.x, p0.y, p1.x, p1.y, num(ptr["stroke"], stroke));
			const auto &n = e["numeral"];
			fillTextAt(g, n["text"].toString(), at + juce::Point<float>(0.25f, -0.35f), num(n["cap_h"], 2.1f) * 0.85f, colour(n["colour"].toString()), false, 1.0f, 0.03f);
		} else if (type == "text") {
			const auto font = e["font"].toString();
			const bool title = font == "title";
			fillText(g, e["label"].toString(), bboxOf(e["bbox"]), c, false, title ? 0.0f : font == "strip_head" || font == "legend" || font == "page" ? 0.1f : 0.05f);
		}
	}

	// the knobs' shadows (they stand 15 mm tall)
	for (const auto &k : m_knobs) {
		g.setColour(juce::Colours::black.withAlpha(0.14f));
		g.fillEllipse(circle(k.c + juce::Point<float>(0.9f, 1.4f), 14.3f));
		g.setColour(juce::Colours::black.withAlpha(0.28f));
		g.fillEllipse(circle(k.c + juce::Point<float>(0.5f, 0.8f), 13.7f));
	}

	// the keys, up
	for (const auto &b : m_buttons)
		drawCap(g, b, false, false);
}

// A key: the square satin cap with its round dish, in its hole. Down, it sits lower and darker.
void HardwareView::drawCap(juce::Graphics &g, const Button &b, bool down, bool hover) const
{
	g.setColour(colour("hole_black"));
	g.fillRect(b.hole);
	auto cap = juce::Rectangle<float>(15.0f, 15.0f).withCentre(b.c);
	if (down)
		cap = cap.reduced(0.3f).translated(0.05f, 0.12f);
	constexpr float r = 1.2f;
	const auto base = b.red ? colour("cap_red") : colour("cap_black");
	const auto face = down ? base.darker(0.3f) : base;
	// the cap's sides, then its top lit from the front left
	g.setColour(b.red ? base.darker(1.2f) : colour("cap_edge"));
	g.fillRoundedRectangle(cap, r);
	const auto top = cap.reduced(0.45f);
	g.setGradientFill(juce::ColourGradient(face.brighter(b.red ? 0.08f : 0.22f), top.getX(), top.getY(), face.darker(0.2f), top.getRight(), top.getBottom(), false));
	g.fillRoundedRectangle(top, r * 0.8f);
	// the concave dish: its far rim in shade, its near rim catching the light
	const auto dish = juce::Rectangle<float>(11.3f, 11.3f).withCentre(top.getCentre());
	g.setGradientFill(juce::ColourGradient(face.darker(0.45f), dish.getX(), dish.getY(), face.brighter(b.red ? 0.1f : 0.3f), dish.getRight(), dish.getBottom(), false));
	g.fillEllipse(dish);
	g.setColour(face.darker(b.red ? 0.05f : 0.1f));
	g.fillEllipse(dish.reduced(0.55f));
	if (hover) {
		g.setColour(juce::Colours::white.withAlpha(0.07f));
		g.fillRoundedRectangle(top, r * 0.8f);
	}
	if (b.print.isNotEmpty())
		fillText(g, b.print, down ? b.printBox.translated(0.05f, 0.12f).reduced(0.1f) : b.printBox, colour("cap_print").withAlpha(down ? 0.85f : 1.0f), true);
}

// A knob from above: the flat skirt with its white triangle, the fluted body, the domed cap; the
// triangle and the flutes turn with it.
void HardwareView::drawKnob(juce::Graphics &g, const Knob &k, bool hover) const
{
	const float a = knobDegrees(k) * kPi / 180;
	const auto c = k.c;
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2e2e32), c.x - 7, c.y - 9, juce::Colour(0xff111113), c.x + 8, c.y + 10, false));
	g.fillEllipse(circle(c, 13.5f));
	g.setColour(juce::Colours::white.withAlpha(hover ? 0.2f : 0.1f));
	g.drawEllipse(circle(c, 13.3f), 0.3f);
	juce::Path tri;
	tri.addTriangle(-1.85f, -7.6f, 1.85f, -7.6f, 0.0f, -11.8f);
	tri.applyTransform(juce::AffineTransform::rotation(a).translated(c));
	g.setColour(colour("pointer_white"));
	g.fillPath(tri);
	// the fluted body
	g.setColour(juce::Colour(0xff0c0c0d));
	g.fillEllipse(circle(c, 8.2f));
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a2a2d), c.x - 4, c.y - 5, juce::Colour(0xff141416), c.x + 5, c.y + 6, false));
	g.fillEllipse(circle(c, 7.9f));
	for (int f = 0; f < 24; ++f) {
		const float t = a + float(f) * 2 * kPi / 24;
		const juce::Point<float> u(std::sin(t), -std::cos(t));
		const auto p0 = c + u * 6.4f, p1 = c + u * 7.9f;
		// the flutes facing the light are lighter
		const float lightness = 0.5f + 0.5f * (-u.x * 0.6f - u.y * 0.8f);
		g.setColour(juce::Colours::white.withAlpha(0.03f + 0.12f * lightness));
		g.drawLine(p0.x, p0.y, p1.x, p1.y, 0.55f);
	}
	// the domed cap
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff55565b), c.x - 1.8f, c.y - 2.2f, juce::Colour(0xff151517), c.x + 3.0f, c.y + 3.6f, true));
	g.fillEllipse(circle(c, 5.25f));
	g.setColour(juce::Colours::black.withAlpha(0.5f));
	g.drawEllipse(circle(c, 5.25f), 0.25f);
}

void HardwareView::drawLitLed(juce::Graphics &g, const Led &l) const
{
	const auto on = colour("led_on");
	g.setGradientFill(juce::ColourGradient(on.withAlpha(0.45f), l.c.x, l.c.y, on.withAlpha(0.0f), l.c.x + l.d, l.c.y, true));
	g.fillEllipse(circle(l.c, l.d));
	g.setColour(on);
	g.fillEllipse(circle(l.c, l.d / 2));
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xffffd0b0).withAlpha(0.85f), l.c.x - 0.3f, l.c.y - 0.3f, on.withAlpha(0.0f), l.c.x + l.d * 0.35f, l.c.y, true));
	g.fillEllipse(circle(l.c, l.d / 2));
}

// The lit segments, cyan-green with a glow, over the unlit grid.
void HardwareView::drawVfd(juce::Graphics &g, int d) const
{
	const auto &gl = glyph();
	const auto &v = m_vfds[size_t(d)];
	juce::Path core, glow;
	for (int i = 0; i < 40; ++i) {
		const uint16_t w = m_display.vfd[size_t(d)][size_t(i)];
		if (w == 0)
			continue;
		const auto t = juce::AffineTransform::translation(v.firstX + float(i) * v.pitch, v.top);
		for (int s = 0; s < 16; ++s)
			if ((w >> kSegmentBit[s]) & 1) {
				core.addPath(gl.core[size_t(s)], t);
				glow.addPath(gl.glow[size_t(s)], t);
			}
	}
	if (core.isEmpty())
		return;
	const auto lit = colour("vfd_lit");
	g.setColour(lit.withAlpha(0.2f));
	g.fillPath(glow);
	g.setColour(lit);
	g.fillPath(core);
}

// ====================================================================================== the rear

// The rear seen from behind (x from its left as seen there): the jack strip, the hinge, the lower
// panel; brand marks from the user's artwork, the rest drawn.
void HardwareView::renderRear(juce::Graphics &g)
{
	g.fillAll(colour("case_black"));
	const auto &rear = panel()["rear_panel"];
	const float y0 = kRearTop, x0 = 12.9f, x1 = kUnitW - 12.9f;
	const auto white = colour("print_white");
	auto xSeen = [](const juce::var &i) {
		const auto &s = i["x_seen_from_rear"];
		return s.isArray() ? juce::Range<float>(num(s[0]), num(s[1])) : juce::Range<float>(num(s), num(s));
	};

	// the faces and the hinge between them
	g.setColour(colour("rail_black"));
	g.fillRect(juce::Rectangle<float>(x0, y0, x1 - x0, kRearH));
	g.setColour(juce::Colours::white.withAlpha(0.08f));
	g.fillRect(juce::Rectangle<float>(x0, y0, x1 - x0, 0.5f));
	const auto hinge = juce::Rectangle<float>(x0, y0 + 47.5f, x1 - x0, 6.0f);
	g.setColour(colour("hinge_black"));
	g.fillRect(hinge);
	for (float x = x0; x < x1; x += 16.0f) {
		g.setColour(juce::Colours::white.withAlpha(0.05f));
		g.fillRect(juce::Rectangle<float>(x + 0.4f, hinge.getY() + 1.6f, 15.2f, 1.2f));
		g.setColour(juce::Colours::black.withAlpha(0.6f));
		g.fillRect(juce::Rectangle<float>(x, hinge.getY(), 0.35f, hinge.getHeight()));
	}
	// the end-bells frame both faces
	for (const float x : {0.0f, x1}) {
		const auto r = juce::Rectangle<float>(x, y0 - 6.0f, 12.9f, kRearH + 8.0f);
		g.setColour(colour("wood"));
		g.fillRect(r);
		juce::Random rng(int(x) + 3);
		for (int i = 0; i < 7; ++i) {
			g.setColour(colour("wood_shade").withAlpha(0.2f + 0.2f * rng.nextFloat()));
			const float gx = r.getX() + r.getWidth() * (0.1f + 0.8f * rng.nextFloat());
			g.drawLine(gx, r.getY(), gx + 0.5f, r.getBottom(), 0.3f);
		}
	}

	const auto *upper = rear["upper_strip"]["items"].getArray();
	if (upper != nullptr)
		for (const auto &i : *upper) {
			const auto type = i["type"].toString();
			const auto xs = xSeen(i);
			const auto &legend = i["legend"];
			auto legends = [&](float x) {
				if (const auto *lines = legend["lines"].getArray())
					for (int n = 0; n < lines->size(); ++n)
						fillTextAt(g, (*lines)[n].toString(), {x, y0 + num(legend["z"][n])}, num(legend["cap_h"], 2.0f), white);
			};
			if (type == "jack") {
				const juce::Point<float> c(xs.getStart(), y0 + num(i["z"]));
				const float d = num(i["d_nut"], 10.0f);
				const bool mini = d < 8;
				// the dark nut, the sleeve, the hole
				g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5e5f63), c.x - d * 0.3f, c.y - d * 0.35f, juce::Colour(0xff1c1c1e), c.x + d * 0.35f, c.y + d * 0.4f, false));
				g.fillEllipse(circle(c, d / 2));
				g.setColour(juce::Colour(0xff0e0e10));
				g.drawEllipse(circle(c, d / 2), 0.25f);
				g.setColour(juce::Colour(0xff3c3d40));
				g.fillEllipse(circle(c, mini ? 2.3f : 4.0f));
				g.setColour(juce::Colour(0xff050506));
				g.fillEllipse(circle(c, mini ? 1.75f : 3.2f));
				legends(c.x);
			} else if (type == "din") {
				const juce::Point<float> c(xs.getStart(), y0 + num(i["z"]));
				const float d = num(i["d"], 21.6f);
				g.setColour(juce::Colour(0xff3a3b3e));
				g.fillEllipse(circle(c, d / 2));
				g.setGradientFill(juce::ColourGradient(juce::Colour(0xffb8b8bc), c.x - 4, c.y - 5, juce::Colour(0xff4c4c50), c.x + 5, c.y + 6, false));
				g.fillEllipse(circle(c, 8.0f));
				g.setColour(juce::Colour(0xff0a0a0b));
				g.fillEllipse(circle(c, 7.1f));
				g.fillRect(juce::Rectangle<float>(2.2f, 1.6f).withCentre({c.x, c.y + 7.4f}));
				g.setColour(juce::Colour(0xff8a8a8e));
				for (const float deg : {-90.0f, -45.0f, 0.0f, 45.0f, 90.0f}) {
					const float t = deg * kPi / 180;
					g.fillEllipse(circle(c + juce::Point<float>(std::sin(t), -std::cos(t)) * 3.6f, 0.55f));
				}
				// the two screws of its flange
				for (const float s : {-1.0f, 1.0f}) {
					const auto sc = c + juce::Point<float>(s * 11.2f, 0);
					g.setColour(juce::Colour(0xff6c6c70));
					g.fillEllipse(circle(sc, 1.3f));
					g.setColour(juce::Colour(0xff1a1a1c));
					g.drawLine(sc.x - 0.9f, sc.y, sc.x + 0.9f, sc.y, 0.35f);
				}
				legends(c.x);
			} else if (type == "switch") {
				// MEMORY PROTECT: the slot here, the slider drawn over it (drawProtect)
				const juce::Point<float> c(xs.getStart(), y0 + num(i["z"]));
				m_protectMm = juce::Rectangle<float>(num(i["w"], 7.0f), num(i["h"], 3.5f)).withCentre(c);
				g.setColour(juce::Colour(0xff050506));
				g.fillRoundedRectangle(m_protectMm.expanded(0.4f), 0.5f);
				legends(c.x);
				const auto &pos = i["position_legends"];
				for (const char *p : {"OFF", "ON"})
					fillTextAt(g, p, {c.x + num(pos[p]["dx_seen_from_rear"]), y0 + num(pos[p]["z"])}, num(legend["cap_h"], 2.0f), white);
			} else if (type == "heading") {
				const juce::Point<float> c(xs.getStart(), y0 + num(i["z"]));
				const float cap = num(i["cap_h"], 2.4f);
				const auto text = i["label"].toString();
				fillTextAt(g, text, c, cap, white, false, 1.0f, 0.08f);
				if (i["rule"].isObject()) {
					// a thin rule each side, broken by the heading
					const float a = kUnitW - num(i["rule"]["x"][1]), b = kUnitW - num(i["rule"]["x"][0]), half = textWidth(text, cap) / 2 + 2.0f;
					g.setColour(white);
					g.fillRect(juce::Rectangle<float>(a, c.y - 0.2f, c.x - half - a, 0.4f));
					g.fillRect(juce::Rectangle<float>(c.x + half, c.y - 0.2f, b - c.x - half, 0.4f));
				}
			} else if (type == "text_block") {
				// the CAUTION block: its lines fill the measured box, the widest one its width
				const auto *lines = i["lines"].getArray();
				if (lines == nullptr || lines->isEmpty())
					continue;
				const float cap = num(i["cap_h"], 1.7f), z0 = num(i["z"][0]), z1 = num(i["z"][1]);
				float widest = 0;
				for (const auto &l : *lines)
					widest = juce::jmax(widest, textWidth(l.toString(), cap));
				const float squeeze = juce::jmin(1.6f, xs.getLength() / widest), pitch = (z1 - z0) / float(lines->size());
				for (int n = 0; n < lines->size(); ++n)
					fillTextAt(g, (*lines)[n].toString(), {xs.getStart(), y0 + z0 + pitch * (float(n) + 0.5f)}, cap, white, true, squeeze, 0.04f);
			} else if (type == "vent") {
				const int slots = int(num(i["slots"], 11));
				const float w = num(i["slot_w"], 2.0f), pitch = num(i["pitch"], 5.0f), z0 = num(i["z"][0]), z1 = num(i["z"][1]);
				for (int n = 0; n < slots; ++n) {
					const auto r = juce::Rectangle<float>(xs.getStart() + float(n) * pitch, y0 + z0, w, z1 - z0);
					g.setColour(juce::Colour(0xff050506));
					g.fillRoundedRectangle(r, w / 2);
					g.setColour(juce::Colours::white.withAlpha(0.06f));
					g.drawRoundedRectangle(r.translated(0, 0.3f), w / 2, 0.2f);
				}
			}
		}

	const float ly = y0 + kLowerTop;
	const auto *lower = rear["lower_panel"]["items"].getArray();
	if (lower != nullptr)
		for (const auto &i : *lower) {
			const auto type = i["type"].toString();
			const auto xs = xSeen(i);
			const auto r = juce::Rectangle<float>(xs.getStart(), ly + num(i["z"][0]), xs.getLength(), num(i["z"][1]) - num(i["z"][0]));
			if (type == "switch" && i["kind"].toString() == "rocker") {
				// POWER: a translucent red rocker in a black bezel, on (drawProtect shades it when off)
				m_powerMm = r;
				g.setColour(juce::Colour(0xff0b0b0c));
				g.fillRoundedRectangle(r, 1.5f);
				const auto rocker = r.reduced(2.0f);
				const auto red = colour("power_red");
				g.setGradientFill(juce::ColourGradient(red.brighter(0.35f), rocker.getX(), rocker.getY(), red.darker(0.6f), rocker.getX(), rocker.getBottom(), false));
				g.fillRoundedRectangle(rocker, 1.0f);
				g.setColour(juce::Colours::black.withAlpha(0.35f));
				g.fillRect(rocker.withTrimmedTop(rocker.getHeight() * 0.48f).withHeight(0.5f));
			} else if (type == "iec_inlet") {
				// the mains inlet on its two-screw flange
				const auto &sx = i["screws_x_seen"];
				const float fa = sx.isArray() ? num(sx[0]) - 3.0f : r.getX() - 6, fb = sx.isArray() ? num(sx[1]) + 3.0f : r.getRight() + 6;
				const auto flange = juce::Rectangle<float>(fa, r.getY() - 1.0f, fb - fa, r.getHeight() + 2.0f);
				g.setColour(juce::Colour(0xff111112));
				g.fillRoundedRectangle(flange, 2.0f);
				if (sx.isArray())
					for (int n = 0; n < 2; ++n) {
						const juce::Point<float> sc(num(sx[n]), flange.getCentreY());
						g.setColour(juce::Colour(0xff6c6c70));
						g.fillEllipse(circle(sc, 1.4f));
						g.setColour(juce::Colour(0xff1a1a1c));
						g.drawLine(sc.x - 1.0f, sc.y, sc.x + 1.0f, sc.y, 0.35f);
					}
				juce::Path inlet;
				const auto in = r.reduced(4.0f, 3.0f);
				inlet.addRoundedRectangle(in, 1.0f);
				g.setColour(juce::Colour(0xff050506));
				g.fillPath(inlet);
				g.setColour(juce::Colour(0xff9a9a9e));
				for (const float px : {-0.3f, 0.0f, 0.3f})
					g.fillRect(juce::Rectangle<float>(1.2f, 3.6f).withCentre({in.getCentreX() + px * in.getWidth() * 0.8f, in.getCentreY() + (px == 0.0f ? -1.5f : 1.0f)}));
			} else if (type == "switch") {
				// the voltage selector
				g.setColour(juce::Colour(0xff0a0a0b));
				g.fillRoundedRectangle(r, 0.6f);
				g.setColour(juce::Colour(0xff4a4a4e));
				g.fillRoundedRectangle(r.reduced(0.9f).withTrimmedBottom(r.getHeight() * 0.5f), 0.4f);
				fillTextAt(g, "115", {r.getCentreX(), r.getY() - 2.6f}, 1.6f, white);
			} else if (type == "logo") {
				const auto place = i["place"].toString();
				if (place == "serial") {
					// The model and serial labels: two silver-white foil stickers with black print, each
					// drawn where panel.json measures it, every line at its measured cap height and width
					// (the print is a bold extended grotesque; the bold face is stretched to the width).
					// Lines naming the product or the maker are left out, like the brand marks.
					const auto *stickers = i["stickers"].getArray();
					if (stickers != nullptr)
						for (const auto &s : *stickers) {
							const auto sx = xSeen(s);
							const auto box = juce::Rectangle<float>(sx.getStart(), ly + num(s["z"][0]), sx.getLength(), num(s["z"][1]) - num(s["z"][0]));
							g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff1f1ef), box.getX(), box.getY(), juce::Colour(0xffd3d4d6), box.getRight(), box.getBottom(), false));
							g.fillRect(box);
							g.setColour(juce::Colours::black.withAlpha(0.25f));
							g.drawRect(box, 0.2f);
							if (const auto *print = s["print"].getArray(); print != nullptr)
								for (const auto &l : *print) {
									if (!static_cast<bool>(l.getProperty("drawn", true)))
										continue;
									const auto text = l["text"].toString();
									const float cap = num(l["cap_h"]), w = num(l["width"]);
									const float squeeze = w > 0 ? w / textWidth(text, cap, true) : 1.0f;
									const bool left = l.hasProperty("x_left");
									const float x = box.getX() + num(left ? l["x_left"] : l["x_centre"]);
									fillTextAt(g, text, {x, box.getY() + num(l["z"])}, cap, juce::Colour(0xff141414), left, squeeze, 0.02f, true);
								}
						}
				} else {
					drawMark(g, place, r, true);
				}
			}
		}
}

// MEMORY PROTECT's slider: ON is to the right, seen from behind. And POWER: the rocker dark when off.
void HardwareView::drawProtect(juce::Graphics &g) const
{
	if (!m_powerMm.isEmpty() && !m_proc.powered()) {
		const auto rocker = m_powerMm.reduced(2.0f);
		g.setColour(juce::Colours::black.withAlpha(0.6f));
		g.fillRoundedRectangle(rocker, 1.0f);
		g.setColour(juce::Colours::black.withAlpha(0.5f));
		g.fillRect(rocker.withHeight(rocker.getHeight() * 0.48f));
	}
	if (m_protectMm.isEmpty())
		return;
	const bool on = m_proc.memoryProtect();
	const auto slot = m_protectMm;
	const auto knob = slot.withWidth(slot.getWidth() / 2).translated(on ? slot.getWidth() / 2 : 0, 0).reduced(0.2f, 0.1f);
	g.setColour(juce::Colour(0xff050506));
	g.fillRoundedRectangle(slot.expanded(0.4f), 0.5f);
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4a4e), knob.getX(), knob.getY(), juce::Colour(0xff1c1c1e), knob.getX(), knob.getBottom(), false));
	g.fillRoundedRectangle(knob, 0.4f);
	g.setColour(juce::Colours::white.withAlpha(m_hoverProtect ? 0.35f : 0.18f));
	for (int n = 1; n < 4; ++n) {
		const float x = knob.getX() + knob.getWidth() * float(n) / 4;
		g.drawLine(x, knob.getY() + 0.5f, x, knob.getBottom() - 0.5f, 0.2f);
	}
}

// ====================================================================================== the bar

juce::String HardwareView::status() const
{
	const auto held = m_held.latchedNames([this](int i) { return m_buttons[size_t(i)].name; });
	if (!held.isEmpty())
		return lemus::panel::heldStatus(held);
	if (const auto s = m_proc.status(); s.isNotEmpty())
		return s;
	if (!m_proc.powered())
		return "POWER OFF (REAR)";
	return m_proc.memoryProtect() ? "MEMORY PROTECT ON (REAR)" : juce::String();
}

void HardwareView::drawBar(juce::Graphics &g)
{
	lemus::panel::drawBarBackground(g, m_bar);
	lemus::panel::drawPill(g, m_bar[m_barRear], "REAR", m_rear, m_hoverBar == m_barRear, barMono());
	lemus::panel::drawPill(g, m_bar.help(), "?", m_help, m_hoverBar == lemus::panel::Bar::kHelp, barMono());
	const bool latched = m_held.anyLatched();
	lemus::panel::drawStatus(g, m_bar.status(), status(), barMono(), latched ? lemus::panel::col::inkOn : lemus::panel::col::status);
	if (m_hoverBar != -1 && !m_help)
		lemus::panel::drawBarTip(g, m_bar, m_hoverBar, getLocalBounds().toFloat(), barMono());
}

// The control under the mouse: its name (and computer key, or setting), under it.
void HardwareView::drawTip(juce::Graphics &g)
{
	if (m_help)
		return;
	juce::String text;
	juce::Point<float> at;
	if (m_rear) {
		if (m_hoverPower) {
			text = m_proc.powered() ? "POWER: on. Click to switch off (the battery keeps the patches and the tune)"
			                        : "POWER: off. Click to switch on (the firmware starts again)";
			at = {m_powerMm.getCentreX() * kPx, (m_powerMm.getBottom() + 6.0f) * kPx};
		} else if (m_hoverProtect) {
			const bool on = m_proc.memoryProtect();
			text = juce::String("MEMORY PROTECT ") + (on ? "ON" : "OFF") + ": click to slide it " + (on ? "OFF" : "ON");
			at = {m_protectMm.getCentreX() * kPx, (m_protectMm.getBottom() + 12.0f) * kPx};
		} else {
			return;
		}
	} else if (const int k = m_dragKnob >= 0 ? m_dragKnob : m_hoverKnob; k >= 0) {
		const auto &knob = m_knobs[size_t(k)];
		if (knob.encoder < 0)
			text = "VOLUME  " + (m_volume != nullptr ? m_volume->getCurrentValueAsText() : juce::String());
		else
			text = "ENCODER " + juce::String(knob.encoder + 1);
		at = {knob.c.x * kPx, (knob.c.y + 14.5f) * kPx};
	} else if (m_hoverButton >= 0) {
		const auto &b = m_buttons[size_t(m_hoverButton)];
		text = b.name;
		if (b.computerKey.isNotEmpty())
			text << "   (" << b.computerKey << ")";
		at = {b.c.x * kPx, (b.hole.getBottom() + 0.8f) * kPx};
	} else {
		return;
	}
	lemus::panel::drawTooltip(g, text, at, getLocalBounds().toFloat().withTrimmedBottom(kBarHeight), barMono());
}

// ====================================================================================== the mouse

int HardwareView::buttonAt(juce::Point<float> mm) const
{
	for (size_t i = 0; i < m_buttons.size(); ++i)
		if (m_buttons[i].hole.contains(mm))
			return int(i);
	return -1;
}

int HardwareView::knobAt(juce::Point<float> mm) const
{
	for (size_t i = 0; i < m_knobs.size(); ++i)
		if (m_knobs[i].c.getDistanceFrom(mm) <= 13.5f)
			return int(i);
	return -1;
}

bool HardwareView::overProtect(juce::Point<float> mm) const { return !m_protectMm.isEmpty() && m_protectMm.expanded(4.0f, 3.0f).contains(mm); }
bool HardwareView::overPower(juce::Point<float> mm) const { return !m_powerMm.isEmpty() && m_powerMm.expanded(2.0f).contains(mm); }

void HardwareView::turn(int knob, int detents)
{
	const auto &k = m_knobs[size_t(knob)];
	if (k.encoder < 0 || detents == 0)
		return;
	m_encoderDetents[size_t(k.encoder)] += detents;
	// at most a turn behind the hand
	auto &pending = m_pendingDetents[size_t(k.encoder)];
	pending = juce::jlimit(-30, 30, pending + detents);
	if (!m_pacer.isTimerRunning() && drainDetents())
		m_pacer.startTimer(20);
	repaintMm(circle(k.c, 15.0f));
}

// One detent of each encoder that has some to go; false when none are left.
bool HardwareView::drainDetents()
{
	bool more = false;
	for (int e = 0; e < int(m_pendingDetents.size()); ++e) {
		auto &pending = m_pendingDetents[size_t(e)];
		if (pending == 0)
			continue;
		const int step = pending > 0 ? 1 : -1;
		m_proc.turnEncoder(e, step);
		pending -= step;
		more = more || pending != 0;
	}
	return more;
}

void HardwareView::setVolume(float normalised)
{
	if (m_volume == nullptr)
		return;
	m_volume->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalised));
	repaint();
}

void HardwareView::mouseMove(const juce::MouseEvent &e)
{
	const bool inPanel = e.position.y < float(kPanelHeight) && !m_help;
	const int bar = e.position.y >= float(kPanelHeight) ? m_bar.hit(e.position) : -1;
	const auto mm = toMm(e.position);
	const int b = inPanel && !m_rear ? buttonAt(mm) : -1;
	const int k = inPanel && !m_rear ? knobAt(mm) : -1;
	const bool p = inPanel && m_rear && overProtect(mm);
	const bool pw = inPanel && m_rear && overPower(mm);
	if (k != m_hoverKnob)
		m_wheelAccum = 0;
	if (bar != m_hoverBar || b != m_hoverButton || k != m_hoverKnob || p != m_hoverProtect || pw != m_hoverPower) {
		m_hoverBar = bar;
		m_hoverButton = b;
		m_hoverKnob = k;
		m_hoverProtect = p;
		m_hoverPower = pw;
		repaint();
	}
}

void HardwareView::mouseExit(const juce::MouseEvent &)
{
	m_hoverBar = m_hoverButton = m_hoverKnob = -1;
	m_hoverProtect = m_hoverPower = false;
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
		else if (hit == m_barRear) {
			m_rear = !m_rear;
			m_hoverButton = m_hoverKnob = -1;
			m_hoverProtect = m_hoverPower = false;
		}
		repaint();
		return;
	}
	const auto mm = toMm(e.position);
	if (m_rear) {
		if (overPower(mm)) {
			m_proc.setPowered(!m_proc.powered());
			m_shownPower = m_proc.powered();
			repaint();
			return;
		}
		if (overProtect(mm)) {
			m_proc.setMemoryProtect(!m_proc.memoryProtect());
			m_shownProtect = m_proc.memoryProtect();
			repaint();
		}
		return;
	}
	if (const int k = knobAt(mm); k >= 0) {
		m_dragKnob = k;
		m_dragLast = e.position.y;
		m_dragAccum = 0;
		if (m_knobs[size_t(k)].encoder < 0 && m_volume != nullptr)
			m_volume->beginChangeGesture();
		repaint();
		return;
	}
	if (const int b = buttonAt(mm); b >= 0 && m_held.mouseDown(b, e.mods))
		m_mouseButton = b;
	repaint();
}

void HardwareView::mouseDrag(const juce::MouseEvent &e)
{
	if (m_dragKnob < 0)
		return;
	const float dy = m_dragLast - e.position.y;
	m_dragLast = e.position.y;
	const bool fine = e.mods.isShiftDown();
	if (m_knobs[size_t(m_dragKnob)].encoder < 0) {
		if (m_volume != nullptr)
			setVolume(m_volume->getValue() + dy / (fine ? 1000.0f : 200.0f));
		return;
	}
	m_dragAccum += dy / (fine ? kFineDragPerDetent : kDragPerDetent);
	const int n = int(m_dragAccum); // towards zero
	if (n != 0) {
		m_dragAccum -= float(n);
		turn(m_dragKnob, n);
	}
}

void HardwareView::mouseUp(const juce::MouseEvent &)
{
	if (m_dragKnob >= 0) {
		if (m_knobs[size_t(m_dragKnob)].encoder < 0 && m_volume != nullptr)
			m_volume->endChangeGesture();
		m_dragKnob = -1;
		repaint();
	}
	if (m_mouseButton >= 0) {
		m_held.mouseUp(m_mouseButton);
		m_mouseButton = -1;
		repaint();
	}
}

void HardwareView::mouseDoubleClick(const juce::MouseEvent &e)
{
	if (m_rear || m_help || m_volume == nullptr)
		return;
	if (const int k = knobAt(toMm(e.position)); k >= 0 && m_knobs[size_t(k)].encoder < 0) {
		m_volume->beginChangeGesture();
		setVolume(m_volume->getDefaultValue());
		m_volume->endChangeGesture();
	}
}

void HardwareView::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
	if (m_rear || m_help)
		return;
	const int k = knobAt(toMm(e.position));
	if (k < 0)
		return;
	const float delta = w.deltaY + w.deltaX;
	if (m_knobs[size_t(k)].encoder < 0) {
		if (m_volume != nullptr) {
			m_volume->beginChangeGesture();
			setVolume(m_volume->getValue() + delta * (e.mods.isShiftDown() ? 0.05f : 0.25f));
			m_volume->endChangeGesture();
		}
		return;
	}
	m_wheelAccum += delta / (e.mods.isShiftDown() ? kWheelPerDetent * 3 : kWheelPerDetent);
	const int n = int(m_wheelAccum);
	if (n != 0) {
		m_wheelAccum -= float(n);
		turn(k, n);
	}
}

// ====================================================================================== the keys

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
	const auto mods = k.getModifiers();
	// Space isn't ours (the Xpander has no run key; in a DAW it's the transport), nor shortcuts
	if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown() || k.getKeyCode() == juce::KeyPress::spaceKey)
		return false;
	juce::String id;
	const int code = k.getKeyCode();
	if (lemus::panel::isClear(k))
		id = kClear;
	else if (code == juce::KeyPress::returnKey)
		id = kStore;
	else if (code >= juce::KeyPress::numberPad0 && code <= juce::KeyPress::numberPad9)
		id = "btn_key_" + juce::String(code - juce::KeyPress::numberPad0);
	else if (code == juce::KeyPress::numberPadAdd)
		id = "btn_key_plus";
	else if (code == juce::KeyPress::numberPadSubtract)
		id = "btn_key_minus";
	else {
		const auto c = k.getTextCharacter();
		const auto lc = juce::CharacterFunctions::toLowerCase(c != 0 ? c : juce::juce_wchar(code));
		for (const auto &ck : kComputerKeys)
			if (ck.ch == lc)
				id = ck.button;
	}
	if (id.isEmpty())
		return false;
	for (size_t i = 0; i < m_buttons.size(); ++i)
		if (m_buttons[i].id == id) {
			m_held.keyDown(code, int(i));
			return true;
		}
	return false;
}

bool HardwareView::keyStateChanged(bool) { return m_held.keysChanged(); }

void HardwareView::focusLost(FocusChangeType) { m_held.focusLost(); }

} // namespace xpander
