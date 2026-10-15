#include "HardwareView.h"
#include "Fonts.h"
#include "PanelData.h"

#include <cmath>
#include <string_view>

using tb303::Machine;
using namespace panel;

namespace {
constexpr float kPi = juce::MathConstants<float>::pi;

juce::Colour col(uint32_t argb) { return juce::Colour(argb); }

const Element &el(int i) { return elements[i]; }

int findElement(const char *id)
{
	for (int i = 0; i < kElementCount; ++i)
		if (std::strcmp(elements[i].id, id) == 0)
			return i;
	jassertfalse;
	return 0;
}

juce::Rectangle<float> boxOf(const Element &e) { return {e.x0, e.y0, e.x1 - e.x0, e.y1 - e.y0}; }
juce::Point<float> centreOf(const Element &e) { return {0.5f * (e.x0 + e.x1), 0.5f * (e.y0 + e.y1)}; }

// Panel legends. Helvetica's cap height is about 0.717 of the font height.
juce::Font legendFont(PFont f, float capH)
{
	switch (f) {
	case PFont::HelvBold:
		return juce::Font(juce::FontOptions("Helvetica Neue", capH / 0.717f, juce::Font::bold));
	case PFont::HelvRegular:
		return juce::Font(juce::FontOptions("Helvetica Neue", capH / 0.717f, juce::Font::plain));
	case PFont::TimesBold:
		return juce::Font(juce::FontOptions("Times New Roman", capH / 0.662f, juce::Font::bold));
	case PFont::TimesBoldItalic:
	case PFont::Segno:
		return juce::Font(juce::FontOptions("Times New Roman", capH / 0.662f, juce::Font::bold | juce::Font::italic));
	case PFont::HelvMedium:
	default:
		return juce::Font(juce::FontOptions("Helvetica Neue", capH / 0.717f, juce::Font::plain).withStyle("Medium"));
	}
}

// Draws text as glyph paths with its cap height centred in box, scaled horizontally to the
// measured width (within reason). '#' is drawn as the panel's raised, smaller sharp sign.
void drawLegendPath(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> box, juce::Font font, juce::Colour c,
                    bool fitWidth = true)
{
	const int sharp = text.indexOfChar('#');
	const juce::String main = sharp >= 0 ? text.substring(0, sharp) + text.substring(sharp + 1) : text;
	const juce::String before = sharp >= 0 ? text.substring(0, sharp) : text;
	juce::GlyphArrangement ga;
	ga.addLineOfText(font, before, 0, 0);
	juce::Path p;
	ga.createPath(p);
	float w = before.isEmpty() ? 0.0f : ga.getBoundingBox(0, -1, true).getRight();
	if (sharp >= 0) {
		const juce::Font sf = font.withHeight(font.getHeight() * 0.64f);
		juce::GlyphArrangement gs;
		const float capH = font.getHeight() * 0.717f;
		gs.addLineOfText(sf, "#", w + font.getHeight() * 0.04f, -capH + sf.getHeight() * 0.717f);
		juce::Path ps;
		gs.createPath(ps);
		p.addPath(ps);
		w = gs.getBoundingBox(0, -1, true).getRight();
		if (sharp + 1 < text.length()) {
			juce::GlyphArrangement ga2;
			ga2.addLineOfText(font, text.substring(sharp + 1), w, 0);
			juce::Path p2;
			ga2.createPath(p2);
			p.addPath(p2);
			w = ga2.getBoundingBox(0, -1, true).getRight();
		}
	}
	(void)main;
	const auto bb = p.getBounds();
	if (bb.isEmpty())
		return;
	float sx = 1.0f;
	if (fitWidth)
		sx = juce::jlimit(0.78f, 1.25f, box.getWidth() / bb.getWidth());
	const float capH = font.getHeight() * 0.717f;
	// Baseline so the cap height is centred in the box.
	const float baseline = box.getCentreY() + capH * 0.5f;
	p.applyTransform(juce::AffineTransform::translation(-bb.getCentreX(), 0)
	                     .scaled(sx, 1.0f)
	                     .translated(box.getCentreX(), baseline));
	g.setColour(c);
	g.fillPath(p);
}


// Small note glyph: head centre (x, y), stem up of height h, n flags.
void noteGlyph(juce::Path &p, float x, float y, float h, int flags, bool filled = true)
{
	const float hw = h * 0.22f, hh = h * 0.15f;
	juce::Path head;
	head.addEllipse(-hw, -hh, 2 * hw, 2 * hh);
	head.applyTransform(juce::AffineTransform::rotation(-0.35f).translated(x, y));
	if (filled)
		p.addPath(head);
	const float sx = x + hw * 0.92f;
	p.addRectangle(sx - h * 0.035f, y - h, h * 0.07f, h);
	for (int i = 0; i < flags; ++i) {
		juce::Path f;
		const float fy = y - h + i * h * 0.22f;
		f.startNewSubPath(sx, fy);
		f.cubicTo(sx + h * 0.08f, fy + h * 0.15f, sx + h * 0.33f, fy + h * 0.22f, sx + h * 0.22f, fy + h * 0.45f);
		f.cubicTo(sx + h * 0.27f, fy + h * 0.25f, sx + h * 0.06f, fy + h * 0.2f, sx, fy + h * 0.14f);
		f.closeSubPath();
		p.addPath(f);
	}
}
} // namespace

// ====================================================================== setup

HardwareView::HardwareView(TB303Processor &p) : m_proc(p)
{
	setSize(kWidth, kHeight);
	setWantsKeyboardFocus(true);
	setOpaque(true);

	auto knob = [this](const char *id, const char *param) { m_knobs.push_back({findElement(id), param}); };
	knob("knob_tuning", "tuning");
	knob("knob_cutoff", "cutoff");
	knob("knob_resonance", "resonance");
	knob("knob_envmod", "envmod");
	knob("knob_decay", "decay");
	knob("knob_accent", "accent");
	knob("knob_tempo", "tempo");
	knob("knob_volume", "volume");
	m_switches.push_back({findElement("switch_patt_group"), TB303Processor::kTrackId, {-105, -75, -45, -15, 15, 45, 75}});
	m_switches.push_back({findElement("switch_mode"), TB303Processor::kModeId, {45, 75, 105, 135}});

	auto key = [this](const char *id, int k) { m_keys.push_back({findElement(id), k}); };
	key("key_C", int(Machine::Key::C));
	key("key_Cs", int(Machine::Key::Cs));
	key("key_D", int(Machine::Key::D));
	key("key_Ds", int(Machine::Key::Ds));
	key("key_E", int(Machine::Key::E));
	key("key_F", int(Machine::Key::F));
	key("key_Fs", int(Machine::Key::Fs));
	key("key_G", int(Machine::Key::G));
	key("key_Gs", int(Machine::Key::Gs));
	key("key_A", int(Machine::Key::A));
	key("key_As", int(Machine::Key::As));
	key("key_B", int(Machine::Key::B));
	key("key_C2", int(Machine::Key::C2));
	key("key_down", int(Machine::Key::Down));
	key("key_up", int(Machine::Key::Up));
	key("key_accent", int(Machine::Key::Accent));
	key("key_slide", int(Machine::Key::Slide));
	key("key_function", int(Machine::Key::Function));
	key("key_pitch_mode", int(Machine::Key::PitchMode));
	key("key_time_mode", int(Machine::Key::TimeMode));
	key("key_pattern_clear", int(Machine::Key::PatternClear));
	key("key_back", int(Machine::Key::Back));
	key("key_tap", int(Machine::Key::Tap));
	key("key_run_stop", -1);
	m_keyDown.assign(m_keys.size(), false);
	m_latched.assign(m_keys.size(), false);

	auto led = [this](const char *id, Machine::Led l) { m_leds.push_back({findElement(id), l}); };
	led("led_pitch_mode", Machine::Led::PitchMode);
	led("led_time_mode", Machine::Led::TimeMode);
	led("led_function", Machine::Led::Function);
	led("led_run", Machine::Led::Run);
	led("led_C", Machine::Led::C);
	led("led_Cs", Machine::Led::Cs);
	led("led_D", Machine::Led::D);
	led("led_Ds", Machine::Led::Ds);
	led("led_E", Machine::Led::E);
	led("led_F", Machine::Led::F);
	led("led_Fs", Machine::Led::Fs);
	led("led_G", Machine::Led::G);
	led("led_Gs", Machine::Led::Gs);
	led("led_A", Machine::Led::A);
	led("led_As", Machine::Led::As);
	led("led_B", Machine::Led::B);
	led("led_C2", Machine::Led::C2);
	led("led_down", Machine::Led::Down);
	led("led_up", Machine::Led::Up);
	led("led_accent", Machine::Led::Accent);
	led("led_slide", Machine::Led::Slide);

	// The brand marks come from the artwork pack (tb303.svg in the ROM folder); without it the
	// panel has none. Pack ids are the panel's logo ids without "logo_".
	for (const auto &mark : p.artwork().marks())
		m_logos["logo_" + mark.id.toStdString()] = mark.outline;

	m_trim = juce::jlimit(0, 5, int(p.params().state.getProperty("serviceTrim", m_trim)));
	setService(bool(p.params().state.getProperty("service", false)));
	startTimerHz(30);
}

HardwareView::~HardwareView()
{
	m_proc.setScopeOn(false);
	releaseAllKeys();
}

// ====================================================================== parameters

float HardwareView::paramValue(const char *id) const
{
	if (auto *p = m_proc.params().getParameter(id))
		return p->getValue();
	return 0;
}

void HardwareView::setParamValue(const char *id, float v)
{
	if (auto *p = m_proc.params().getParameter(id))
		p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, v));
}

juce::String HardwareView::paramText(const char *id) const
{
	if (auto *p = m_proc.params().getParameter(id))
		return p->getName(32) + "  " + p->getCurrentValueAsText();
	return {};
}

void HardwareView::pressKey(int keyIndex, bool down)
{
	if (keyIndex < 0 || keyIndex >= int(m_keys.size()))
		return;
	if (!down)
		m_latched[size_t(keyIndex)] = false;
	if (m_keyDown[size_t(keyIndex)] == down)
		return;
	m_keyDown[size_t(keyIndex)] = down;
	const int k = m_keys[size_t(keyIndex)].key;
	if (k < 0) {
		if (down)
			m_proc.toggleRun();
	} else {
		m_proc.setKey(Machine::Key(k), down);
	}
	repaint();
}

void HardwareView::releaseAllKeys()
{
	for (size_t i = 0; i < m_keys.size(); ++i)
		pressKey(int(i), false);
	m_computerKeys.clear();
}

void HardwareView::timerCallback()
{
	m_snap = m_proc.snapshot();
	if (m_service)
		updateScope();
	repaint();
}

// ====================================================================== static artwork

void HardwareView::drawElement(juce::Graphics &g, int index)
{
	const Element &e = el(index);
	const auto box = boxOf(e);
	switch (e.type) {
	case PType::Case:
		break;
	case PType::Region: {
		g.setColour(col(e.colour));
		g.fillRect(box);
		break;
	}
	case PType::Fill: {
		g.setColour(col(e.colour));
		if (e.corner > 0) {
			// Bottom corners rounded (printed keys and bands).
			juce::Path p;
			p.addRoundedRectangle(box.getX(), box.getY(), box.getWidth(), box.getHeight(), e.corner, e.corner, false, false, true, true);
			g.fillPath(p);
		} else {
			g.fillRect(box);
		}
		break;
	}
	case PType::Outline:
		g.setColour(col(e.colour));
		if (std::strcmp(e.id, "box_function") == 0) {
			// The FUNCTION box's right edge stops where the PATTERN band's keyline takes over.
			juce::Path p;
			p.startNewSubPath(e.x0, e.y1);
			p.lineTo(e.x0, e.y0);
			p.lineTo(e.x1, e.y0);
			p.lineTo(e.x1, 111.23f);
			g.strokePath(p, juce::PathStrokeType(e.stroke));
		} else {
			g.drawRect(box, e.stroke);
		}
		break;
	case PType::Line: {
		// The rule between the tb303 and computer_controlled marks goes with them: without either
		// (no artwork pack) it would stand on its own.
		if (std::string_view(e.id) == "rule_tb303" && m_logos.count("logo_tb303") == 0 && m_logos.count("logo_computer_controlled") == 0)
			break;
		juce::Path p;
		for (int i = 0; i < e.pointCount; ++i) {
			const float x = points[2 * (e.pointStart + i)], y = points[2 * (e.pointStart + i) + 1];
			if (i == 0)
				p.startNewSubPath(x, y);
			else
				p.lineTo(x, y);
		}
		g.setColour(col(e.colour));
		if (e.pointCount > 2)
			p = p.createPathWithRoundedCorners(1.4f); // printed keylines turn with rounded corners
		g.strokePath(p, juce::PathStrokeType(e.stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
		break;
	}
	case PType::LabelBox: {
		g.setColour(col(e.colour));
		g.fillRect(box);
		const float capH = juce::jmin(box.getHeight() * 0.64f, 2.3f);
		drawLegendPath(g, juce::String(e.label), box.reduced(box.getWidth() * 0.12f, 0), legendFont(e.font, capH),
		               col(e.textColour), false);
		break;
	}
	case PType::OutlineBox: {
		g.setColour(col(e.colour));
		g.drawRect(box, e.stroke);
		const juce::String label(e.label);
		if (e.font == PFont::Segno) {
			// Segno: a slanted S with a slash and two dots.
			const auto c = box.getCentre();
			const float h = box.getHeight() * 0.72f;
			juce::Path s;
			juce::GlyphArrangement ga;
			ga.addLineOfText(legendFont(PFont::TimesBoldItalic, h * 0.75f), "S", 0, 0);
			ga.createPath(s);
			const auto bb = s.getBounds();
			s.applyTransform(juce::AffineTransform::translation(c.x - bb.getCentreX(), c.y - bb.getCentreY()));
			g.fillPath(s);
			g.drawLine(c.x - h * 0.32f, c.y + h * 0.42f, c.x + h * 0.32f, c.y - h * 0.42f, h * 0.07f);
			g.fillEllipse(c.x - h * 0.42f - h * 0.06f, c.y + h * 0.05f - h * 0.06f, h * 0.12f, h * 0.12f);
			g.fillEllipse(c.x + h * 0.42f - h * 0.06f, c.y - h * 0.05f - h * 0.06f, h * 0.12f, h * 0.12f);
		} else {
			const float capH = e.font == PFont::TimesBoldItalic ? box.getHeight() * 0.56f : 1.9f;
			drawLegendPath(g, label, box.reduced(box.getWidth() * (e.font == PFont::TimesBoldItalic ? 0.14f : 0.06f), 0),
			               legendFont(e.font, capH), col(e.colour), false);
		}
		break;
	}
	case PType::Text: {
		juce::String label(e.label);
		drawLegendPath(g, label, box, legendFont(e.font, e.capH), col(e.colour), label.length() > 1);
		break;
	}
	case PType::Symbol: {
		const juce::String label(e.label);
		const auto c = box.getCentre();
		g.setColour(col(e.colour));
		if (label == "square") {
			g.fillRect(box);
		} else if (label == "dot") {
			g.fillEllipse(box);
		} else if (label.startsWith("filled triangle")) {
			juce::Path t;
			t.addTriangle(box.getX(), box.getBottom(), box.getRight(), box.getBottom(), c.x, box.getY());
			g.fillPath(t);
		} else if (label.startsWith("sawtooth")) {
			juce::Path p;
			const float w = box.getWidth(), h = box.getHeight();
			p.startNewSubPath(box.getX(), box.getBottom());
			for (int i = 0; i < 2; ++i) {
				const float x = box.getX() + i * w / 2;
				p.lineTo(x, box.getY());
				p.lineTo(x + w / 2, box.getBottom());
			}
			(void)h;
			g.strokePath(p, juce::PathStrokeType(0.42f));
		} else if (label.startsWith("square-wave")) {
			juce::Path p;
			const float w = box.getWidth();
			float x = box.getX();
			p.startNewSubPath(x, box.getBottom());
			for (int i = 0; i < 2; ++i) {
				p.lineTo(x, box.getY());
				p.lineTo(x + w / 4, box.getY());
				p.lineTo(x + w / 4, box.getBottom());
				p.lineTo(x + w / 2, box.getBottom());
				x += w / 2;
			}
			g.strokePath(p, juce::PathStrokeType(0.42f));
		} else if (label.startsWith("filled dot")) {
			const float h = box.getHeight();
			g.fillEllipse(box.getX(), c.y - h * 0.12f, h * 0.28f, h * 0.28f);
			juce::Path n;
			noteGlyph(n, box.getRight() - h * 0.55f, box.getBottom() - h * 0.12f, h * 0.9f, 2);
			g.fillPath(n);
		} else if (label.startsWith("hollow dot")) {
			const float h = box.getHeight();
			g.drawEllipse(box.getX() + 0.15f, c.y - h * 0.1f, h * 0.24f, h * 0.24f, 0.18f);
			juce::Path n;
			noteGlyph(n, box.getRight() - h * 0.55f, box.getBottom() - h * 0.18f, h * 0.8f, 2);
			g.fillPath(n);
			juce::Path tie;
			tie.startNewSubPath(box.getRight() - h * 1.2f, box.getBottom() - h * 0.05f);
			tie.quadraticTo(box.getRight() - h * 0.8f, box.getBottom() + h * 0.12f, box.getRight() - h * 0.45f, box.getBottom() - h * 0.1f);
			g.strokePath(tie, juce::PathStrokeType(0.2f));
		} else if (label.startsWith("dash")) {
			const float h = box.getHeight();
			g.fillRect(box.getX(), c.y - 0.15f, h * 0.55f, 0.32f);
			// sixteenth rest: two flags on a slanted stem
			const float x = box.getRight() - h * 0.45f;
			juce::Path r;
			r.startNewSubPath(x + h * 0.25f, box.getY() + h * 0.1f);
			r.lineTo(x - h * 0.05f, box.getBottom());
			g.strokePath(r, juce::PathStrokeType(0.25f));
			for (int i = 0; i < 2; ++i) {
				const float fy = box.getY() + h * (0.15f + i * 0.3f);
				const float fx = x + h * 0.22f - i * h * 0.1f;
				g.fillEllipse(fx - h * 0.32f, fy, h * 0.18f, h * 0.18f);
				juce::Path f;
				f.startNewSubPath(fx - h * 0.25f, fy + h * 0.12f);
				f.quadraticTo(fx - h * 0.05f, fy + h * 0.15f, fx, fy);
				g.strokePath(f, juce::PathStrokeType(0.2f));
			}
		} else if (label.startsWith("three beamed")) {
			const float h = box.getHeight();
			juce::Path n;
			for (int i = 0; i < 3; ++i)
				noteGlyph(n, box.getX() + h * 0.25f + i * h * 0.4f, box.getY() + h * 0.58f, h * 0.5f, 0);
			n.addRectangle(box.getX() + h * 0.25f + h * 0.1f, box.getY() + h * 0.06f, h * 0.85f, h * 0.08f);
			g.fillPath(n);
			juce::Path br;
			const float by = box.getBottom() - h * 0.18f;
			br.startNewSubPath(box.getX() + h * 0.15f, by - h * 0.12f);
			br.lineTo(box.getX() + h * 0.15f, by);
			br.lineTo(box.getRight() - h * 0.15f, by);
			br.lineTo(box.getRight() - h * 0.15f, by - h * 0.12f);
			g.strokePath(br, juce::PathStrokeType(0.18f));
			drawLegendPath(g, "3", {c.x - 1.0f, by - 0.6f, 2.0f, 1.2f}, legendFont(PFont::HelvBold, 1.1f), col(e.colour), false);
		}
		break;
	}
	case PType::Logo: {
		auto it = m_logos.find(e.id);
		if (it != m_logos.end()) {
			g.setColour(col(e.colour));
			g.fillPath(it->second);
		}
		break;
	}
	default:
		break;
	}
}

void HardwareView::renderStatic(juce::Graphics &g)
{
	// Case and the three surfaces.
	g.fillAll(juce::Colour(0xff2a2b2d));
	g.setColour(col(c_case_silver));
	g.fillRoundedRectangle(0, 0, panel::kWidth, panel::kHeight, 1.3f);

	for (int i = 0; i < kElementCount; ++i)
		if (el(i).type == PType::Region)
			drawElement(g, i);

	// Satin sheen and grain on the upper surfaces, a cooler sheen on the sub-panel.
	{
		juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.10f), 0, 0, juce::Colours::black.withAlpha(0.05f), 0, 70, false);
		g.setGradientFill(sheen);
		g.fillRect(0.0f, 0.0f, panel::kWidth, 69.98f);
		juce::ColourGradient sheen2(juce::Colours::white.withAlpha(0.07f), 0, 71.5f, juce::Colours::black.withAlpha(0.06f), 0, 135.4f, false);
		g.setGradientFill(sheen2);
		g.fillRect(2.39f, 71.55f, 295.86f, 63.82f);
	}
	// Seam between the tone strip and the middle section, and the step down to the sub-panel.
	g.setColour(col(c_seam_dark));
	g.fillRect(1.38f, 26.66f, 297.4f, 0.83f);
	g.setColour(col(c_seam_hilite));
	g.fillRect(1.38f, 27.49f, 297.4f, 0.3f);
	{
		juce::ColourGradient step(juce::Colour(0xff8d8d89), 0, 69.98f, juce::Colour(0xffb5b5b1), 0, 71.55f, false);
		g.setGradientFill(step);
		g.fillRect(1.01f, 69.98f, 297.98f, 1.57f);
		g.setColour(juce::Colours::white.withAlpha(0.6f));
		g.fillRect(2.39f, 71.45f, 295.86f, 0.18f);
		// shade along the sub-panel's left, right and front edges
		g.setColour(juce::Colours::black.withAlpha(0.12f));
		g.fillRect(2.39f, 71.55f, 0.6f, 63.82f);
		g.fillRect(297.65f, 71.55f, 0.6f, 63.82f);
		juce::ColourGradient front(juce::Colour(0xff8f8f8b), 0, 135.37f, juce::Colour(0xffa9a9a5), 0, 137.0f, false);
		g.setGradientFill(front);
		g.fillRect(0.0f, 135.37f, panel::kWidth, 1.63f);
		g.setColour(col(c_case_front_rim));
		g.fillRect(0.0f, 137.0f, panel::kWidth, 5.0f);
		g.setColour(juce::Colours::white.withAlpha(0.5f));
		g.fillRect(0.5f, 141.6f, panel::kWidth - 1, 0.2f);
	}

	// Print, in layer order.
	for (PType t : {PType::Fill, PType::Outline, PType::Line, PType::LabelBox, PType::OutlineBox, PType::Text, PType::Symbol, PType::Logo})
		for (int i = 0; i < kElementCount; ++i)
			if (el(i).type == t)
				drawElement(g, i);

	// Knob scales and wells.
	for (const auto &k : m_knobs) {
		const Element &e = el(k.element);
		const auto c = centreOf(e);
		const bool large = e.extra > 15;
		g.setColour(col(c_print_black));
		if (large) {
			for (int i = 0; i <= 20; ++i) {
				const float a = juce::degreesToRadians(-150.0f + 15.0f * i);
				const bool longTick = i % 2 == 0;
				const float r0 = 12.8f, r1 = longTick ? 14.83f : 13.72f;
				juce::Line<float> l(c.x + r0 * std::sin(a), c.y - r0 * std::cos(a), c.x + r1 * std::sin(a), c.y - r1 * std::cos(a));
				g.drawLine(l, 0.45f);
			}
		} else {
			for (int i = 0; i <= 10; ++i) {
				if (i == 5)
					continue; // the square marker
				const float a = juce::degreesToRadians(-150.0f + 30.0f * i);
				const float r0 = 8.66f, r1 = 10.41f;
				juce::Line<float> l(c.x + r0 * std::sin(a), c.y - r0 * std::cos(a), c.x + r1 * std::sin(a), c.y - r1 * std::cos(a));
				g.drawLine(l, 0.4f);
			}
		}
		// Chrome well rim and the dark gap.
		const float rw = (e.x1 - e.x0) * 0.5f;
		juce::ColourGradient rim(juce::Colour(0xfff4f4f2), c.x - rw, c.y - rw, juce::Colour(0xff8e8e8a), c.x + rw, c.y + rw, false);
		g.setGradientFill(rim);
		g.fillEllipse(c.x - rw, c.y - rw, 2 * rw, 2 * rw);
		g.setColour(col(c_bezel_gap));
		const float rg = rw - 0.95f;
		g.fillEllipse(c.x - rg, c.y - rg, 2 * rg, 2 * rg);
	}
	for (const auto &s : m_switches) {
		const Element &e = el(s.element);
		const auto c = centreOf(e);
		const float rw = (e.x1 - e.x0) * 0.5f;
		juce::ColourGradient rim(juce::Colour(0xfff4f4f2), c.x - rw, c.y - rw, juce::Colour(0xff8e8e8a), c.x + rw, c.y + rw, false);
		g.setGradientFill(rim);
		g.fillEllipse(c.x - rw, c.y - rw, 2 * rw, 2 * rw);
		g.setColour(col(c_bezel_gap));
		const float rg = rw - 0.95f;
		g.fillEllipse(c.x - rg, c.y - rg, 2 * rg, 2 * rg);
	}
	// Key cut-outs and LED sockets.
	for (const auto &k : m_keys) {
		const auto b = boxOf(el(k.element));
		g.setColour(col(c_key_hole));
		g.fillRoundedRectangle(b, 0.35f);
	}
	for (const auto &l : m_leds) {
		const Element &e = el(l.element);
		const auto c = centreOf(e);
		g.setColour(juce::Colours::black.withAlpha(0.55f));
		g.fillEllipse(c.x - 1.75f, c.y - 1.75f, 3.5f, 3.5f);
	}
}

// ====================================================================== live parts

void HardwareView::drawKnob(juce::Graphics &g, const Knob &k, float v, bool hover)
{
	const Element &e = el(k.element);
	const auto c = centreOf(e);
	const bool large = e.extra > 15;
	const float rSkirt = e.extra * 0.5f;
	const float rCap = large ? 7.5f : 4.4f;
	const float angle = juce::degreesToRadians(-150.0f + 300.0f * v);

	// Shadow cast toward the front-right.
	g.setColour(juce::Colours::black.withAlpha(0.35f));
	g.fillEllipse(c.x - rSkirt + 0.5f, c.y - rSkirt + 0.9f, 2 * rSkirt + 0.4f, 2 * rSkirt + 0.4f);

	// Knurled skirt: bright, high-contrast ridges lit from the upper left.
	const int ridges = large ? 90 : 64;
	g.setColour(col(c_knob_knurl_dark));
	g.fillEllipse(c.x - rSkirt, c.y - rSkirt, 2 * rSkirt, 2 * rSkirt);
	for (int i = 0; i < ridges; ++i) {
		const float a0 = angle + 2 * kPi * i / ridges;
		const float a1 = a0 + 2 * kPi / ridges * 0.5f;
		const float light = 0.5f + 0.5f * std::cos(a0 - (-2.4f)); // light from upper left
		juce::Path w;
		w.addPieSegment(c.x - rSkirt, c.y - rSkirt, 2 * rSkirt, 2 * rSkirt, a0, a1, rCap / rSkirt * 0.98f);
		g.setColour(col(c_knob_knurl_dark).interpolatedWith(col(c_knob_knurl_light), 0.25f + 0.75f * light));
		g.fillPath(w);
	}
	// Skirt edge shading
	juce::ColourGradient edge(juce::Colours::white.withAlpha(0.18f), c.x - rSkirt, c.y - rSkirt, juce::Colours::black.withAlpha(0.35f),
	                          c.x + rSkirt, c.y + rSkirt, false);
	g.setGradientFill(edge);
	g.fillEllipse(c.x - rSkirt, c.y - rSkirt, 2 * rSkirt, 2 * rSkirt);

	// Flat satin cap, very slightly offset away from the viewer (top-down perspective).
	const auto cc = c.translated(0.0f, -0.25f);
	juce::ColourGradient cap(juce::Colour(0xffe2e2df), cc.x - rCap * 0.6f, cc.y - rCap * 0.7f, juce::Colour(0xff9c9c98), cc.x + rCap * 0.7f,
	                         cc.y + rCap * 0.8f, true);
	cap.addColour(0.45, col(c_knob_cap));
	g.setGradientFill(cap);
	g.fillEllipse(cc.x - rCap, cc.y - rCap, 2 * rCap, 2 * rCap);
	g.setColour(juce::Colours::white.withAlpha(0.45f));
	g.drawEllipse(cc.x - rCap + 0.1f, cc.y - rCap + 0.1f, 2 * rCap - 0.2f, 2 * rCap - 0.2f, 0.18f);
	// Pointer notch cut into the cap's rim.
	{
		juce::Path notch;
		const float nw = large ? 1.5f : 1.15f, nd = large ? 1.4f : 1.0f;
		notch.addRectangle(-nw * 0.5f, -rCap - 0.1f, nw, nd);
		notch.applyTransform(juce::AffineTransform::rotation(angle).translated(cc.x, cc.y));
		g.setColour(juce::Colour(0xff1e1d1b));
		g.fillPath(notch);
		// lit lower edge of the cut
		juce::Path lip;
		lip.addRectangle(-nw * 0.5f, -rCap - 0.1f + nd - 0.18f, nw, 0.18f);
		lip.applyTransform(juce::AffineTransform::rotation(angle).translated(cc.x, cc.y));
		g.setColour(juce::Colour(0xff8c8b87));
		g.fillPath(lip);
	}
	if (std::strcmp(k.param, "volume") == 0) {
		// The master unit's VOLUME cap shows an engraved radial line.
		juce::Line<float> l(cc.x, cc.y, cc.x + (rCap - 1.0f) * std::sin(angle), cc.y - (rCap - 1.0f) * std::cos(angle));
		g.setColour(juce::Colour(0xff6c6b67));
		g.drawLine(l, 0.25f);
	}
	if (hover) {
		g.setColour(juce::Colour(0x30ffffff));
		g.fillEllipse(cc.x - rCap, cc.y - rCap, 2 * rCap, 2 * rCap);
	}
}

void HardwareView::drawSwitch(juce::Graphics &g, const Switch &s, int pos, bool hover)
{
	const Element &e = el(s.element);
	const auto c = centreOf(e);
	const float rBase = e.extra * 0.5f;
	const float angle = juce::degreesToRadians(s.detentsDeg[size_t(juce::jlimit(0, int(s.detentsDeg.size()) - 1, pos))]);
	g.setColour(juce::Colours::black.withAlpha(0.35f));
	g.fillEllipse(c.x - rBase + 0.5f, c.y - rBase + 0.9f, 2 * rBase + 0.4f, 2 * rBase + 0.4f);
	juce::ColourGradient base(juce::Colour(0xffd8d8d5), c.x - rBase, c.y - rBase, juce::Colour(0xff6f6f6b), c.x + rBase, c.y + rBase, false);
	g.setGradientFill(base);
	g.fillEllipse(c.x - rBase, c.y - rBase, 2 * rBase, 2 * rBase);
	g.setColour(juce::Colours::black.withAlpha(0.25f));
	g.drawEllipse(c.x - rBase + 0.2f, c.y - rBase + 0.2f, 2 * rBase - 0.4f, 2 * rBase - 0.4f, 0.3f);

	// The fin: a chrome bar across the full diameter, sloped ends, pointer end notched.
	const float len = rBase * 2 - 0.4f, w = 7.0f;
	juce::Graphics::ScopedSaveState ss(g);
	g.addTransform(juce::AffineTransform::rotation(angle, 0, 0).translated(c.x, c.y));
	// side faces (visible as darker edges)
	g.setColour(juce::Colours::black.withAlpha(0.3f));
	g.fillRoundedRectangle(-w * 0.5f + 0.5f, -len * 0.5f + 0.5f, w, len, 1.0f);
	juce::ColourGradient fin(juce::Colour(0xff8a8a86), -w * 0.5f, 0, juce::Colour(0xff8a8a86), w * 0.5f, 0, false);
	fin.addColour(0.2, juce::Colour(0xfff1f1ee));
	fin.addColour(0.5, juce::Colour(0xffc9c9c6));
	fin.addColour(0.8, juce::Colour(0xffa4a4a0));
	g.setGradientFill(fin);
	g.fillRoundedRectangle(-w * 0.5f, -len * 0.5f, w, len, 0.9f);
	// flat top of the fin
	juce::ColourGradient top(juce::Colour(0xffe9e9e6), 0, -len * 0.5f, juce::Colour(0xffb3b3af), 0, len * 0.5f, false);
	g.setGradientFill(top);
	g.fillRect(-w * 0.32f, -len * 0.5f + 2.2f, w * 0.64f, len - 4.4f);
	// sloped ends
	g.setColour(juce::Colour(0xff9a9a96));
	g.fillRect(-w * 0.32f, -len * 0.5f + 0.6f, w * 0.64f, 1.6f);
	g.setColour(juce::Colour(0xff7d7d79));
	g.fillRect(-w * 0.32f, len * 0.5f - 2.2f, w * 0.64f, 1.6f);
	// pointer step at the leading (top) end
	g.setColour(juce::Colour(0xff4d4c49));
	g.fillRect(-w * 0.32f, -len * 0.5f + 2.2f, w * 0.64f, 0.35f);
	if (hover) {
		g.setColour(juce::Colour(0x28ffffff));
		g.fillRoundedRectangle(-w * 0.5f, -len * 0.5f, w, len, 0.9f);
	}
}

void HardwareView::drawKey(juce::Graphics &g, const Key &k, bool down)
{
	const Element &e = el(k.element);
	const auto hole = boxOf(e);
	const bool horizontal = hole.getWidth() > hole.getHeight() && e.extra < 0.5f;
	(void)horizontal;
	const float inset = 0.42f;
	auto cap = hole.reduced(inset);
	if (down)
		cap = cap.translated(0.1f, 0.25f);
	// side faces: the cap stands proud, its lower/right sides in shade
	g.setColour(col(c_keycap_shadow));
	g.fillRoundedRectangle(cap.translated(0.0f, down ? 0.15f : 0.55f), 0.45f);
	juce::ColourGradient face(down ? juce::Colour(0xffa8a8a5) : juce::Colour(0xffcfcfcc), cap.getX(), cap.getY(),
	                          down ? juce::Colour(0xff8f8f8c) : juce::Colour(0xffa9a9a6), cap.getRight(), cap.getBottom(), false);
	g.setGradientFill(face);
	g.fillRoundedRectangle(cap, 0.45f);
	// chamfer highlights
	g.setColour(col(c_keycap_hilite).withAlpha(down ? 0.35f : 0.85f));
	g.drawLine(cap.getX() + 0.4f, cap.getY() + 0.25f, cap.getRight() - 0.4f, cap.getY() + 0.25f, 0.3f);
	g.drawLine(cap.getX() + 0.25f, cap.getY() + 0.4f, cap.getX() + 0.25f, cap.getBottom() - 0.4f, 0.25f);
	g.setColour(col(c_keycap_shadow).withAlpha(0.7f));
	g.drawLine(cap.getX() + 0.4f, cap.getBottom() - 0.2f, cap.getRight() - 0.3f, cap.getBottom() - 0.2f, 0.3f);
	g.drawLine(cap.getRight() - 0.2f, cap.getY() + 0.4f, cap.getRight() - 0.2f, cap.getBottom() - 0.3f, 0.25f);
	// inner flat face
	juce::ColourGradient flat(juce::Colour(0xffc4c4c1), cap.getX(), cap.getY(), juce::Colour(0xffb0b0ad), cap.getX(), cap.getBottom(), false);
	g.setGradientFill(flat);
	g.fillRoundedRectangle(cap.reduced(0.7f), 0.3f);
	if (e.extra > 0.5f && !down) {
		// moulding line on the wide caps
		g.setColour(juce::Colours::black.withAlpha(0.12f));
		const float x = cap.getX() + cap.getWidth() * 0.75f;
		g.drawLine(x, cap.getY() + 0.8f, x, cap.getBottom() - 0.8f, 0.15f);
	}
}

void HardwareView::drawLed(juce::Graphics &g, const Led &l, float level)
{
	const Element &e = el(l.element);
	const auto c = centreOf(e);
	const float r = 1.5f;
	level = juce::jlimit(0.0f, 1.0f, level);
	const juce::Colour unlit = col(c_led_unlit), lit = col(c_led_lit);
	if (level > 0.02f) {
		juce::ColourGradient glow(lit.withAlpha(0.55f * level), c.x, c.y, lit.withAlpha(0.0f), c.x + r * 3.2f, c.y, true);
		g.setGradientFill(glow);
		g.fillEllipse(c.x - r * 3.2f, c.y - r * 3.2f, r * 6.4f, r * 6.4f);
	}
	const juce::Colour body = unlit.interpolatedWith(lit, level);
	juce::ColourGradient dome(body.brighter(0.4f + 0.5f * level), c.x - r * 0.4f, c.y - r * 0.4f, body.darker(0.6f - 0.4f * level), c.x + r,
	                          c.y + r, true);
	g.setGradientFill(dome);
	g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
	g.setColour(juce::Colours::white.withAlpha(0.55f));
	g.fillEllipse(c.x - r * 0.55f, c.y - r * 0.6f, r * 0.5f, r * 0.35f);
}

void HardwareView::drawWaveformSwitch(juce::Graphics &g, bool square)
{
	// Stand-in for the rear-panel slide switch, at the rear edge above the WAVEFORM glyphs.
	const juce::Rectangle<float> slot(32.45f, 0.15f, 5.5f, 1.55f);
	g.setColour(juce::Colour(0xff151515));
	g.fillRoundedRectangle(slot, 0.4f);
	const float w = 2.5f;
	juce::Rectangle<float> knob(square ? slot.getRight() - w - 0.18f : slot.getX() + 0.18f, slot.getY() + 0.18f, w, slot.getHeight() - 0.36f);
	juce::ColourGradient gk(juce::Colour(0xff6a6a6a), knob.getX(), knob.getY(), juce::Colour(0xff222222), knob.getX(), knob.getBottom(), false);
	g.setGradientFill(gk);
	g.fillRoundedRectangle(knob, 0.25f);
	g.setColour(juce::Colours::white.withAlpha(0.25f));
	for (int i = 1; i < 4; ++i)
		g.drawLine(knob.getX() + w * i / 4, knob.getY() + 0.2f, knob.getX() + w * i / 4, knob.getBottom() - 0.2f, 0.12f);
}

// ====================================================================== plugin bar

juce::Rectangle<float> HardwareView::barRect(BarItem i) const
{
	// Bar items in px (component coordinates), left to right. FACTORY TRIM is there in service.
	const float y = kPanelHeight + 8, h = kBarHeight - 16;
	switch (i) {
	case BarItem::Sync: return {12, y, 100, h};
	case BarItem::Top: return {120, y, 196, h};
	case BarItem::Service: return {324, y, 96, h};
	case BarItem::Factory: return m_service ? juce::Rectangle<float>(428, y, 120, h) : juce::Rectangle<float>();
	case BarItem::Help: return {1150, y, 38, h};
	}
	return {};
}

// CUT OFF MAX's track inside its item, after its label.
juce::Rectangle<float> HardwareView::sliderTrack() const
{
	const auto r = barRect(BarItem::Top);
	return {r.getX() + 90, r.getCentreY() - 1.5f, 62, 3};
}

void HardwareView::drawBar(juce::Graphics &g)
{
	const juce::Rectangle<float> bar(0, float(kPanelHeight), float(kWidth), float(kBarHeight));
	g.setColour(juce::Colour(0xff1b1d21));
	g.fillRect(bar);
	g.setColour(juce::Colour(0xff2a2f37));
	g.drawLine(0, float(kPanelHeight) + 0.5f, float(kWidth), float(kPanelHeight) + 0.5f, 1.0f);
	auto pill = [&](BarItem i, const juce::String &text, bool on, bool hover) {
		const auto r = barRect(i);
		g.setColour(on ? juce::Colour(0xff2a1e12) : hover ? juce::Colour(0xff2c323b) : juce::Colour(0xff232830));
		g.fillRoundedRectangle(r, 6);
		g.setColour(on ? juce::Colour(0xffff8c1a) : juce::Colour(0xff2e343d));
		g.drawRoundedRectangle(r.reduced(0.5f), 6, 1);
		g.setColour(on ? juce::Colour(0xffffb866) : juce::Colour(0xffe9ecf1));
		g.setFont(fonts::mono(12.0f, fonts::Weight::Medium));
		g.drawText(text, r, juce::Justification::centred);
	};
	pill(BarItem::Sync, paramValue("sync") > 0.5f ? "SYNC: HOST" : "SYNC: OFF", paramValue("sync") > 0.5f, m_hover.kind == HitKind::BarSync);
	auto slider = [&](BarItem i, HitKind kind, const char *label, const juce::String &value) {
		const auto r = barRect(i);
		const bool active = m_hover.kind == kind || m_drag.kind == kind;
		g.setColour(active ? juce::Colour(0xff2c323b) : juce::Colour(0xff232830));
		g.fillRoundedRectangle(r, 6);
		g.setColour(juce::Colour(0xff2e343d));
		g.drawRoundedRectangle(r.reduced(0.5f), 6, 1);
		g.setFont(fonts::mono(11.0f, fonts::Weight::Medium));
		g.setColour(juce::Colour(0xff9aa4b1));
		const auto track = sliderTrack();
		g.drawText(label, r.withRight(track.getX()).withTrimmedLeft(8), juce::Justification::centredLeft);
		const float v = paramValue(barSlider(kind));
		g.setColour(juce::Colour(0xff3a414c));
		g.fillRoundedRectangle(track, 1.5f);
		g.setColour(juce::Colour(0xffff8c1a));
		g.fillRoundedRectangle(track.withWidth(track.getWidth() * v), 1.5f);
		g.fillEllipse(track.getX() + track.getWidth() * v - 4, track.getCentreY() - 4, 8, 8);
		g.setColour(juce::Colour(0xffe9ecf1));
		g.drawText(value, r.withTrimmedLeft(track.getRight() - r.getX() + 6).withTrimmedRight(8), juce::Justification::centredRight);
	};
	const auto valueText = [&](const char *id) { return m_proc.params().getParameter(id)->getCurrentValueAsText(); };
	slider(BarItem::Top, HitKind::BarTop, "CUT OFF MAX", valueText("fullcutoff").upToFirstOccurrenceOf(" kHz", false, false) + "k");
	pill(BarItem::Service, "SERVICE", m_service, m_hover.kind == HitKind::BarService);
	{
		// A unit out of its factory trim: a dot on SERVICE.
		bool factory = true;
		for (int i = 0; i < 6; ++i)
			factory = factory && m_proc.trimAtFactory(i);
		if (!factory) {
			const auto r = barRect(BarItem::Service);
			g.setColour(juce::Colour(0xffff8c1a));
			g.fillEllipse(r.getRight() - 13, r.getCentreY() - 3, 6, 6);
		}
	}
	if (m_service)
		pill(BarItem::Factory, "FACTORY TRIM", false, m_hover.kind == HitKind::BarFactory);
	pill(BarItem::Help, "?", m_showHelp, m_hover.kind == HitKind::BarHelp);

	// Status: clock source, tempo, what the voice is doing.
	g.setFont(fonts::mono(11.0f));
	g.setColour(juce::Colour(0xff9aa4b1));
	juce::String status;
	status << (m_snap.synced ? "HOST " : m_snap.running ? "RUN  " : "STOP ") << juce::String(m_snap.bpm, 1) << " BPM";
	const auto &l = m_snap.lines;
	static const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	status << "  NOTE "<< names[l.note % 12] << (l.note / 12 - 1) << (l.gate ? "  GATE" : "      ") << (l.accent ? "  ACC" : "     ")
	       << (l.slide && l.gate ? "  SLIDE" : "       ") << "  VCF " << TB303Processor::hzText(m_snap.probe.vcfHz);
	if (!m_proc.romLoaded())
		status = "VOICE ONLY (no CPU ROM)   NOTE " + juce::String(names[l.note % 12]) + juce::String(l.note / 12 - 1) + (l.gate ? "  GATE" : "      ") +
		         (l.accent ? "  ACC" : "     ") + (l.slide && l.gate ? "  SLIDE" : "       ");
	if (m_service)
		status = juce::String::fromUTF8("PANEL OFF  pick a trimmer on the right \xc2\xb7 \xe2\x86\x91\xe2\x86\x93 pick, \xe2\x86\x90\xe2\x86\x92 turn \xc2\xb7 the keys play");
	const float sx = barRect(m_service ? BarItem::Factory : BarItem::Service).getRight() + 14;
	g.drawText(status, juce::Rectangle<float>(sx, float(kPanelHeight), barRect(BarItem::Help).getX() - 10 - sx, float(kBarHeight)),
	           juce::Justification::centredLeft);
}

void HardwareView::drawHelp(juce::Graphics &g)
{
	const juce::Rectangle<float> r(150, 60, 900, 483);
	g.setColour(juce::Colour(0xf01b1f25));
	g.fillRoundedRectangle(r, 12);
	g.setColour(juce::Colour(0xff2e343d));
	g.drawRoundedRectangle(r, 12, 1);
	g.setColour(juce::Colour(0xffe9ecf1));
	g.setFont(fonts::mono(15.0f, fonts::Weight::SemiBold));
	g.drawText("TB-303 KEYS", r.reduced(24).removeFromTop(24), juce::Justification::topLeft);
	const char *lines[] = {
		"A W S E D F T G Y H U J K   note keys C C# D D# E F F# G G# A A# B C (patterns 1-8)",
		"Z X C V                     DOWN  UP  ACCENT  SLIDE   (note / tie / rest in TIME MODE)",
		"Q                           PITCH MODE",
		"R                           TIME MODE",
		"I                           FUNCTION (BAR)",
		"Backspace                   PATTERN CLEAR (D.C. / BAR RESET)",
		",                           BACK",
		"Return                      WRITE/NEXT  (TAP)",
		"Space                       RUN/STOP (standalone; in a DAW it is the DAW's transport)",
		"1-7                         TRACK / PATT.GROUP switch",
		"F1 F2 F3 F4                 MODE: track write, track play, pattern play, pattern write",
		"",
		"Mouse: drag knobs (shift = fine, double-click = default), click switches, press keys.",
		"Right-click (ctrl-click) a key: it stays down until clicked again. Escape lets go of all.",
		"MIDI notes play the voice directly: velocity >= 100 accents, overlapping notes slide.",
		"SYNC: HOST runs the sequencer from the DAW transport (DIN sync: run + 24 ppqn clock).",
		"CUT OFF MAX: how high full CUT OFF reaches, 1.2 to 3 kHz: drag it (double-click: the fitted unit's).",
		"SERVICE: the panel off. Pick a trimmer on the right (up/down), turn it there or on the board (left/right).",
		"Programming follows the original manual: PATTERN WRITE, pick A/B and 1-8, hold the",
		"pattern key + PATTERN CLEAR, PITCH MODE to enter notes, TIME MODE for note/tie/rest.",
	};
	g.setFont(fonts::mono(12.5f));
	g.setColour(juce::Colour(0xffc8ced6));
	float y = r.getY() + 64;
	for (const auto *l : lines) {
		g.drawText(l, juce::Rectangle<float>(r.getX() + 24, y, r.getWidth() - 48, 20), juce::Justification::centredLeft);
		y += 21;
	}
}

void HardwareView::drawTooltip(juce::Graphics &g)
{
	juce::String text;
	juce::Point<float> at;
	const Hit &h = m_drag.kind != HitKind::None ? m_drag : m_hover;
	if (h.kind == HitKind::Knob) {
		const auto &k = m_knobs[size_t(h.index)];
		text = paramText(k.param);
		const Element &e = el(k.element);
		at = {centreOf(e).x * kPxPerMm, e.y1 * kPxPerMm + 6};
	} else if (h.kind == HitKind::Switch) {
		const auto &s = m_switches[size_t(h.index)];
		text = paramText(s.param);
		const Element &e = el(s.element);
		at = {centreOf(e).x * kPxPerMm, e.y1 * kPxPerMm + 6};
	} else if (h.kind == HitKind::Waveform) {
		text = paramText("waveform");
		at = {35.2f * kPxPerMm, 10 * kPxPerMm};
	} else if (h.kind == HitKind::BarTop) {
		auto *p = m_proc.params().getParameter("fullcutoff");
		text = "Full CUT OFF reaches " + p->getCurrentValueAsText() + "; drag to set, double-click for " +
		       p->getText(p->getDefaultValue(), 16);
		at = {barRect(BarItem::Top).getCentreX(), barRect(BarItem::Top).getY() - 30};
	} else if (h.kind == HitKind::BarService) {
		text = m_service ? "Put the panel back" : "Take the panel off: the trimmers, and the service notes' adjustments";
		at = {barRect(BarItem::Service).getCentreX(), barRect(BarItem::Service).getY() - 30};
	} else if (h.kind == HitKind::BarFactory) {
		text = "Every trimmer back to the factory's trim";
		at = {barRect(BarItem::Factory).getCentreX(), barRect(BarItem::Factory).getY() - 30};
	} else if (h.kind == HitKind::Trimmer) {
		text = trimmerText(h.index);
		const auto c = trimmerPos(h.index);
		at = {c.x, c.y + 26};
	}
	if (m_service && (h.kind == HitKind::Knob || h.kind == HitKind::Switch || h.kind == HitKind::Waveform)) {
		const auto c = h.kind == HitKind::Knob     ? servicePos(m_knobs[size_t(h.index)].element)
		               : h.kind == HitKind::Switch ? servicePos(m_switches[size_t(h.index)].element)
		                                           : waveformPos();
		at = {c.x, c.y + 32};
	}
	if (text.isEmpty())
		return;
	g.setFont(fonts::mono(12.0f, fonts::Weight::Medium));
	const float w = g.getCurrentFont().getStringWidthFloat(text) + 16;
	juce::Rectangle<float> r(at.x - w / 2, at.y, w, 22);
	r = r.constrainedWithin(getLocalBounds().toFloat().reduced(4));
	g.setColour(juce::Colour(0xe8101317));
	g.fillRoundedRectangle(r, 5);
	g.setColour(juce::Colour(0xffffb866));
	g.drawText(text, r, juce::Justification::centred);
}

// ====================================================================== paint

bool HardwareView::redrawCache(const juce::Image &image, float drawnAt, float phys) const
{
	if (!image.isValid())
		return true;
	if (std::abs(phys - drawnAt) <= 0.01f)
		return false;
	// The timer's next repaint after the resizing stops does it.
	return juce::Time::getMillisecondCounter() - m_resizedAt >= 200;
}

void HardwareView::paint(juce::Graphics &g)
{
	if (m_service) {
		paintService(g);
		drawBar(g);
		drawTooltip(g);
		if (m_showHelp)
			drawHelp(g);
		return;
	}
	const float phys = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
	if (redrawCache(m_static, m_staticScale, phys)) {
		m_staticScale = phys;
		m_static = juce::Image(juce::Image::RGB, juce::roundToInt(kWidth * phys), juce::roundToInt(kPanelHeight * phys), true);
		juce::Graphics sg(m_static);
		sg.addTransform(juce::AffineTransform::scale(phys * kPxPerMm));
		renderStatic(sg);
		// Paint grain: faint per-pixel noise on the silver surfaces (not on the print).
		juce::Image::BitmapData bd(m_static, juce::Image::BitmapData::readWrite);
		juce::Random rng(303);
		for (int y = 0; y < bd.height; ++y)
			for (int x = 0; x < bd.width; ++x) {
				auto c = bd.getPixelColour(x, y);
				if (c.getBrightness() < 0.55f || c.getSaturation() > 0.12f)
					continue;
				const float n = (rng.nextFloat() - 0.5f) * 0.035f;
				bd.setPixelColour(x, y, n > 0 ? c.brighter(n) : c.darker(-n));
			}
	}
	g.drawImageTransformed(m_static, juce::AffineTransform::scale(1.0f / m_staticScale));

	{
		juce::Graphics::ScopedSaveState s(g);
		g.addTransform(juce::AffineTransform::scale(kPxPerMm));
		drawWaveformSwitch(g, paramValue("waveform") > 0.5f);
		for (size_t i = 0; i < m_knobs.size(); ++i)
			drawKnob(g, m_knobs[i], paramValue(m_knobs[i].param),
			         (m_hover.kind == HitKind::Knob && m_hover.index == int(i)) || (m_drag.kind == HitKind::Knob && m_drag.index == int(i)));
		for (size_t i = 0; i < m_switches.size(); ++i) {
			auto *p = m_proc.params().getParameter(m_switches[i].param);
			const int n = int(m_switches[i].detentsDeg.size());
			const int pos = p ? juce::roundToInt(p->getValue() * (n - 1)) : 0;
			drawSwitch(g, m_switches[i], pos, m_hover.kind == HitKind::Switch && m_hover.index == int(i));
		}
		for (size_t i = 0; i < m_keys.size(); ++i)
			drawKey(g, m_keys[i], m_keyDown[i]);
		for (const auto &l : m_leds)
			drawLed(g, l, m_snap.leds[size_t(l.led)]);
	}
	drawBar(g);
	drawTooltip(g);
	if (m_showHelp)
		drawHelp(g);
}

// ====================================================================== interaction

HardwareView::Hit HardwareView::hitTest(juce::Point<float> p) const
{
	if (p.y >= kPanelHeight) {
		const std::pair<BarItem, HitKind> items[] = {{BarItem::Sync, HitKind::BarSync},       {BarItem::Top, HitKind::BarTop},
		                                             {BarItem::Service, HitKind::BarService}, {BarItem::Factory, HitKind::BarFactory},
		                                             {BarItem::Help, HitKind::BarHelp}};
		for (const auto &[i, kind] : items)
			if (barRect(i).contains(p))
				return {kind, int(i)};
		return {};
	}
	if (m_service)
		return hitTestService(p);
	const auto mm = toMm(p);
	for (size_t i = 0; i < m_knobs.size(); ++i) {
		const Element &e = el(m_knobs[i].element);
		if (centreOf(e).getDistanceFrom(mm) < (e.x1 - e.x0) * 0.5f + 0.5f)
			return {HitKind::Knob, int(i)};
	}
	for (size_t i = 0; i < m_switches.size(); ++i) {
		const Element &e = el(m_switches[i].element);
		if (centreOf(e).getDistanceFrom(mm) < (e.x1 - e.x0) * 0.5f + 1.0f)
			return {HitKind::Switch, int(i)};
	}
	for (size_t i = 0; i < m_keys.size(); ++i)
		if (boxOf(el(m_keys[i].element)).expanded(0.6f).contains(mm))
			return {HitKind::Key, int(i)};
	if (juce::Rectangle<float>(26.0f, 0.0f, 19.0f, 8.0f).contains(mm))
		return {HitKind::Waveform, 0};
	return {};
}

void HardwareView::mouseMove(const juce::MouseEvent &e)
{
	const Hit h = hitTest(e.position);
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
	if (m_showHelp) {
		m_showHelp = false;
		repaint();
		return;
	}
	m_drag = hitTest(e.position);
	m_dragStart = e.position;
	switch (m_drag.kind) {
	case HitKind::Knob:
		m_dragStartValue = paramValue(m_knobs[size_t(m_drag.index)].param);
		if (auto *p = m_proc.params().getParameter(m_knobs[size_t(m_drag.index)].param))
			p->beginChangeGesture();
		break;
	case HitKind::Switch: {
		auto *p = m_proc.params().getParameter(m_switches[size_t(m_drag.index)].param);
		m_dragStartValue = p ? p->getValue() : 0;
		break;
	}
	case HitKind::Key: {
		// A right-click (ctrl-click on a Mac) holds a key down until it's clicked again, so steps
		// that need two keys at once, such as TAP with ACCENT, work with the mouse alone. RUN/STOP
		// acts on the press, so it doesn't latch.
		const auto i = size_t(m_drag.index);
		if (m_latched[i]) {
			pressKey(m_drag.index, false);
			m_drag = {};
		} else if (e.mods.isPopupMenu() && m_keys[i].key >= 0) {
			pressKey(m_drag.index, true);
			m_latched[i] = true;
			m_drag = {};
		} else {
			pressKey(m_drag.index, true);
		}
		break;
	}
	case HitKind::Waveform:
		setParamValue("waveform", paramValue("waveform") > 0.5f ? 0.0f : 1.0f);
		break;
		break;
	case HitKind::BarSync:
		setParamValue("sync", paramValue("sync") > 0.5f ? 0.0f : 1.0f);
		break;
	case HitKind::BarTop:
		if (auto *p = m_proc.params().getParameter(barSlider(m_drag.kind)))
			p->beginChangeGesture();
		setParamValue(barSlider(m_drag.kind), (e.position.x - sliderTrack().getX()) / sliderTrack().getWidth());
		break;
	case HitKind::BarService:
		setService(!m_service);
		break;
	case HitKind::BarFactory:
		factoryTrim();
		break;
	case HitKind::Trimmer:
	case HitKind::TrimSlider:
		// On the board or its control: picking it lights it up and sets the instruments on it.
		if (m_drag.index != m_trim)
			setServiceTrim(m_drag.index);
		if (auto *p = m_proc.params().getParameter(TB303Processor::kTrimIds[m_drag.index]))
			p->beginChangeGesture();
		// A click only picks it; a drag turns it from where it is, never jumping to the mouse.
		m_dragStartValue = paramValue(TB303Processor::kTrimIds[m_drag.index]);
		break;
	case HitKind::TrimRow:
		setServiceTrim(m_drag.index);
		break;
	case HitKind::BarHelp:
		m_showHelp = !m_showHelp;
		break;
	default:
		break;
	}
	repaint();
}

void HardwareView::mouseDrag(const juce::MouseEvent &e)
{
	if (m_drag.kind == HitKind::Knob) {
		const float sens = e.mods.isShiftDown() ? 0.0008f : 0.004f;
		const float dy = (m_dragStart.y - e.position.y) + (e.position.x - m_dragStart.x) * 0.3f;
		setParamValue(m_knobs[size_t(m_drag.index)].param, m_dragStartValue + dy * sens);
	} else if (m_drag.kind == HitKind::Switch) {
		const auto &s = m_switches[size_t(m_drag.index)];
		const auto c = m_service ? servicePos(s.element) : centreOf(el(s.element)) * kPxPerMm;
		const float deg = juce::radiansToDegrees(std::atan2(e.position.x - c.x, -(e.position.y - c.y)));
		int best = 0;
		for (size_t i = 0; i < s.detentsDeg.size(); ++i)
			if (std::abs(s.detentsDeg[i] - deg) < std::abs(s.detentsDeg[size_t(best)] - deg))
				best = int(i);
		setParamValue(s.param, float(best) / float(s.detentsDeg.size() - 1));
	} else if (m_drag.kind == HitKind::Trimmer) {
		// A trimmer turns a long way for a short drag on the board: shift for a screwdriver's touch.
		const float sens = e.mods.isShiftDown() ? 0.0002f : 0.002f;
		const float dy = (m_dragStart.y - e.position.y) + (e.position.x - m_dragStart.x) * 0.3f;
		setParamValue(TB303Processor::kTrimIds[m_drag.index], m_dragStartValue + dy * sens);
	} else if (m_drag.kind == HitKind::TrimSlider) {
		// Along the control's track; shift a tenth as far.
		const float dx = (e.position.x - m_dragStart.x) / trimTrack(m_drag.index).getWidth();
		setParamValue(TB303Processor::kTrimIds[m_drag.index], m_dragStartValue + dx * (e.mods.isShiftDown() ? 0.1f : 1.0f));
	} else if (barSlider(m_drag.kind) != nullptr) {
		// Shift: a tenth of the speed from where the drag is, for small steps.
		const float x = e.mods.isShiftDown() ? m_dragStart.x + (e.position.x - m_dragStart.x) * 0.1f : e.position.x;
		setParamValue(barSlider(m_drag.kind), (x - sliderTrack().getX()) / sliderTrack().getWidth());
	}
	repaint();
}

void HardwareView::mouseUp(const juce::MouseEvent &e)
{
	if (m_drag.kind == HitKind::Key) {
		pressKey(m_drag.index, false);
	} else if (m_drag.kind == HitKind::Knob) {
		if (auto *p = m_proc.params().getParameter(m_knobs[size_t(m_drag.index)].param))
			p->endChangeGesture();
	} else if (m_drag.kind == HitKind::Trimmer || m_drag.kind == HitKind::TrimSlider) {
		if (auto *p = m_proc.params().getParameter(TB303Processor::kTrimIds[m_drag.index]))
			p->endChangeGesture();
	} else if (barSlider(m_drag.kind) != nullptr) {
		if (auto *p = m_proc.params().getParameter(barSlider(m_drag.kind)))
			p->endChangeGesture();
	} else if (m_drag.kind == HitKind::Switch && e.getDistanceFromDragStart() < 4) {
		// Click without dragging steps to the next position (wrapping).
		const auto &s = m_switches[size_t(m_drag.index)];
		const int n = int(s.detentsDeg.size());
		const int pos = juce::roundToInt(m_dragStartValue * (n - 1));
		setParamValue(s.param, float((pos + (e.mods.isRightButtonDown() || e.mods.isShiftDown() ? n - 1 : 1)) % n) / float(n - 1));
	}
	m_drag = {};
	repaint();
}

void HardwareView::mouseDoubleClick(const juce::MouseEvent &e)
{
	const Hit h = hitTest(e.position);
	if (h.kind == HitKind::Knob)
		if (auto *p = m_proc.params().getParameter(m_knobs[size_t(h.index)].param))
			p->setValueNotifyingHost(p->getDefaultValue());
	if (barSlider(h.kind) != nullptr)
		if (auto *p = m_proc.params().getParameter(barSlider(h.kind)))
			p->setValueNotifyingHost(p->getDefaultValue());
	if (h.kind == HitKind::Trimmer || h.kind == HitKind::TrimSlider)
		resetTrim(h.index);
}

void HardwareView::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
	const Hit h = hitTest(e.position);
	if (h.kind == HitKind::Knob) {
		const char *id = m_knobs[size_t(h.index)].param;
		setParamValue(id, paramValue(id) + w.deltaY * (e.mods.isShiftDown() ? 0.02f : 0.08f));
	} else if (h.kind == HitKind::Switch) {
		const auto &s = m_switches[size_t(h.index)];
		const int n = int(s.detentsDeg.size());
		const int pos = juce::roundToInt(paramValue(s.param) * (n - 1)) + (w.deltaY > 0 ? 1 : -1);
		setParamValue(s.param, float(juce::jlimit(0, n - 1, pos)) / float(n - 1));
	} else if (const char *id = barSlider(h.kind)) {
		setParamValue(id, paramValue(id) + w.deltaY * (e.mods.isShiftDown() ? 0.01f : 0.05f));
	} else if (h.kind == HitKind::Trimmer || h.kind == HitKind::TrimRow || h.kind == HitKind::TrimSlider) {
		nudgeTrim(h.index, w.deltaY * (e.mods.isShiftDown() ? 0.001f : 0.01f));
	}
}

const char *HardwareView::barSlider(HitKind k)
{
	return k == HitKind::BarTop ? "fullcutoff" : nullptr;
}

namespace {
// Computer keyboard -> panel key index (the order keys were added in the constructor).
int keyIndexForChar(juce::juce_wchar c)
{
	switch (juce::CharacterFunctions::toLowerCase(c)) {
	case 'a': return 0;   // C
	case 'w': return 1;   // C#
	case 's': return 2;   // D
	case 'e': return 3;   // D#
	case 'd': return 4;   // E
	case 'f': return 5;   // F
	case 't': return 6;   // F#
	case 'g': return 7;   // G
	case 'y': return 8;   // G#
	case 'h': return 9;   // A
	case 'u': return 10;  // A#
	case 'j': return 11;  // B
	case 'k': return 12;  // C'
	case 'z': return 13;  // DOWN
	case 'x': return 14;  // UP
	case 'c': return 15;  // ACCENT
	case 'v': return 16;  // SLIDE
	case 'i': return 17;  // FUNCTION
	case 'q': return 18;  // PITCH MODE
	case 'r': return 19;  // TIME MODE
	case ',': return 21;  // BACK
	default: return -1;
	}
}
} // namespace

bool HardwareView::keyPressed(const juce::KeyPress &k)
{
	const auto c = k.getTextCharacter();
	if (c == '?') {
		m_showHelp = !m_showHelp;
		repaint();
		return true;
	}
	if (k.getKeyCode() == juce::KeyPress::escapeKey) {
		m_showHelp = false;
		releaseAllKeys();
		repaint();
		return true;
	}
	if (m_service) {
		// Up/down pick a trimmer, left/right turn it (shift: a tenth as far).
		const int code = k.getKeyCode();
		if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
			setServiceTrim((m_trim + (code == juce::KeyPress::upKey ? 5 : 1)) % 6);
			return true;
		}
		if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
			nudgeTrim(m_trim, (code == juce::KeyPress::rightKey ? 1.0f : -1.0f) * (k.getModifiers().isShiftDown() ? 0.0002f : 0.002f));
			return true;
		}
	}
	if (c >= '1' && c <= '7') {
		setParamValue(TB303Processor::kTrackId, float(c - '1') / 6.0f);
		return true;
	}
	for (int f = 0; f < 4; ++f)
		if (k.getKeyCode() == juce::KeyPress::F1Key + f) {
			setParamValue(TB303Processor::kModeId, float(f) / 3.0f);
			return true;
		}
	int idx = keyIndexForChar(c);
	if (k.getKeyCode() == juce::KeyPress::backspaceKey)
		idx = 20; // PATTERN CLEAR
	else if (k.getKeyCode() == juce::KeyPress::returnKey)
		idx = 22; // TAP
	else if (k.getKeyCode() == juce::KeyPress::spaceKey && juce::JUCEApplicationBase::isStandaloneApp())
		idx = 23; // RUN/STOP; in a plugin, Space goes on to the host's transport
	if (idx < 0)
		return false;
	if (m_computerKeys.count(k.getKeyCode()) == 0) {
		m_computerKeys[k.getKeyCode()] = idx;
		pressKey(idx, true);
	}
	return true;
}

bool HardwareView::keyStateChanged(bool)
{
	bool handled = false;
	for (auto it = m_computerKeys.begin(); it != m_computerKeys.end();) {
		if (!juce::KeyPress::isKeyCurrentlyDown(it->first)) {
			pressKey(it->second, false);
			it = m_computerKeys.erase(it);
			handled = true;
		} else {
			++it;
		}
	}
	return handled;
}

void HardwareView::focusLost(FocusChangeType)
{
	// Keys held on the computer keyboard won't see their key-up now; latched keys stay down.
	for (const auto &[code, idx] : m_computerKeys)
		if (!m_latched[size_t(idx)])
			pressKey(idx, false);
	m_computerKeys.clear();
}
