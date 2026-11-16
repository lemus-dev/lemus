#include "HardwareView.h"
#include "Fonts.h"
#include "PanelData.h"

#include <cmath>
#include <cstring>
#include <iterator>

namespace sh101 {

using namespace panel;
using Button = Machine::Button;
using Led = Machine::Led;

namespace {
juce::Colour col(uint32_t argb) { return juce::Colour(argb); }

const Element &el(int i) { return elements[i]; }

juce::Rectangle<float> boxOf(const Element &e) { return {e.box[0], e.box[1], e.box[2] - e.box[0], e.box[3] - e.box[1]}; }
juce::Point<float> point(const Element &e, int i) { return {points[2 * (e.pointStart + i)], points[2 * (e.pointStart + i) + 1]}; }
juce::Point<float> polar(juce::Point<float> c, float r, float deg)
{
	const float a = juce::degreesToRadians(deg);
	return {c.x + r * std::sin(a), c.y - r * std::cos(a)};
}

// The panel legends: Helvetica Bold caps, sized by cap height (measured on an H: the font's height
// is its ascent plus descent, so the ratio depends on the face).
juce::Font legendFont(float capH)
{
	static const float capRatio = [] {
		juce::GlyphArrangement ga;
		ga.addLineOfText(juce::Font(juce::FontOptions("Helvetica Neue", 100.0f, juce::Font::bold)), "H", 0, 0);
		juce::Path p;
		ga.createPath(p);
		const float h = p.getBounds().getHeight() / 100.0f;
		return h > 0.3f ? h : 0.62f;
	}();
	return juce::Font(juce::FontOptions("Helvetica Neue", capH / capRatio, juce::Font::bold));
}

// Draws text as glyph paths with its cap height centred in box, scaled horizontally to the measured
// width (within reason).
void drawLegend(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> box, float capH, juce::Colour c)
{
	juce::GlyphArrangement ga;
	const auto font = legendFont(capH);
	ga.addLineOfText(font, text, 0, 0);
	juce::Path p;
	ga.createPath(p);
	const auto bb = p.getBounds();
	if (bb.isEmpty())
		return;
	const bool digitsOnly = text.containsOnly("0123456789");
	const float sx = digitsOnly ? 1.0f : juce::jlimit(0.75f, 1.3f, box.getWidth() / bb.getWidth());
	// Lower-case letters and signs (b, #, ~) have no cap height to centre: fit their own bounds.
	const bool caps = text.containsAnyOf("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
	const float baseline = caps ? box.getCentreY() + capH * 0.5f : box.getBottom() - (bb.getBottom() > 0 ? bb.getBottom() : 0);
	p.applyTransform(juce::AffineTransform::translation(-bb.getCentreX(), 0).scaled(sx, 1.0f).translated(box.getCentreX(), baseline));
	g.setColour(c);
	g.fillPath(p);
}

void arrowHead(juce::Graphics &g, juce::Point<float> tip, float dirDeg, float size)
{
	juce::Path p;
	p.addTriangle(0, 0, -size * 0.5f, size, size * 0.5f, size);
	p.applyTransform(juce::AffineTransform::rotation(juce::degreesToRadians(dirDeg)).translated(tip));
	g.fillPath(p);
}

// A moulded groove: a dark line with a lit edge below or to the right (light from the rear left).
void groove(juce::Graphics &g, juce::Point<float> a, juce::Point<float> b, float w)
{
	const bool horizontal = std::abs(b.y - a.y) < std::abs(b.x - a.x);
	const juce::Point<float> off = horizontal ? juce::Point<float>(0, w * 0.85f) : juce::Point<float>(w * 0.85f, 0);
	g.setColour(col(c_groove_light).withAlpha(0.75f));
	g.drawLine({a + off, b + off}, w * 0.55f);
	g.setColour(col(c_groove_dark));
	g.drawLine({a, b}, w);
}

// The black knobs (016H071): a soft drop shadow, a straight skirt and a slightly domed top.
void knobBody(juce::Graphics &g, juce::Point<float> c, float r)
{
	g.setColour(juce::Colours::black.withAlpha(0.28f));
	g.fillEllipse(c.x - r - 0.4f, c.y - r + 0.6f, 2 * r + 2.2f, 2 * r + 2.4f);
	g.setColour(juce::Colours::black.withAlpha(0.35f));
	g.fillEllipse(c.x - r + 0.2f, c.y - r + 0.7f, 2 * r + 0.6f, 2 * r + 0.8f);
	juce::ColourGradient skirt(juce::Colour(0xff4a474b), c.x - r, c.y - r, juce::Colour(0xff0e0d0f), c.x + r * 0.6f, c.y + r, false);
	g.setGradientFill(skirt);
	g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
	const float t = r * 0.86f;
	juce::ColourGradient top(juce::Colour(0xff39363a), c.x - t * 0.6f, c.y - t * 0.7f, juce::Colour(0xff171618), c.x + t * 0.5f, c.y + t * 0.8f, true);
	g.setGradientFill(top);
	g.fillEllipse(c.x - t, c.y - t + 0.15f, 2 * t, 2 * t);
	g.setColour(juce::Colours::white.withAlpha(0.10f));
	g.drawEllipse(c.x - t, c.y - t + 0.15f, 2 * t, 2 * t, 0.25f);
}

const char *buttonNames[] = {"load", "play", "arpDown", "arpUpDown", "arpUp", "hold", "keyTranspose"};

// The bar's font, help and tooltips: the panel frame's monospace is the machine's bundled one.
const lemus::panel::Mono kMono = [](float size, int weight) { return fonts::mono(size, fonts::Weight(weight)); };

// A key's or button's name, for the status line while a right-click holds it: the keys by pitch
// (key 7 is C4, as the computer keyboard plays it).
juce::String heldName(int i, int keys)
{
	static const char *buttons[] = {"LOAD", "PLAY", "DOWN", "U&D", "UP", "HOLD", "KEY TRANSPOSE"};
	static const char *notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	if (i >= keys)
		return buttons[juce::jlimit(0, 6, i - keys)];
	const int note = 60 + i - 7;
	return juce::String(notes[note % 12]) + juce::String(note / 12 - 1);
}
} // namespace

// ====================================================================== setup

HardwareView::HardwareView(SH101Processor &p) : m_proc(p)
{
	setSize(kWidth, kHeight);
	setWantsKeyboardFocus(true);
	setOpaque(true);

	m_buttons.assign(size_t(Button::Count), -1);
	m_leds.assign(size_t(Led::Count), -1);
	m_keys.assign(size_t(Machine::kKeys), -1);
	for (int i = 0; i < kElementCount; ++i) {
		const Element &e = el(i);
		switch (e.type) {
		case PType::Slider:
			m_sliders.push_back({i, e.param, {}});
			break;
		case PType::Knob:
			m_knobs.push_back({i, e.param, {}});
			break;
		case PType::VSwitch:
		case PType::Rotary: {
			std::vector<int> order;
			for (int k = 0; k < e.pointCount; ++k)
				order.push_back(k);
			m_switches.push_back({i, e.param, order});
			break;
		}
		case PType::HSwitch: {
			std::vector<int> order;
			for (const auto &s : juce::StringArray::fromTokens(e.label, ",", ""))
				order.push_back(s.getIntValue());
			m_switches.push_back({i, e.param, order});
			break;
		}
		case PType::Button:
			for (int b = 0; b < int(Button::Count); ++b)
				if (std::strcmp(e.label, buttonNames[b]) == 0)
					m_buttons[size_t(b)] = i;
			break;
		case PType::Led:
			for (int b = 0; b < int(Led::Count); ++b)
				if (std::strcmp(e.label, buttonNames[b]) == 0)
					m_leds[size_t(b)] = i;
			if (std::strcmp(e.label, "rate") == 0)
				m_ledRate = i;
			if (std::strcmp(e.label, "power") == 0)
				m_ledPower = i;
			break;
		case PType::Key:
			m_keys[size_t(juce::roundToInt(e.v[0]))] = i;
			break;
		case PType::Bender:
			m_bender = i;
			break;
		case PType::Case:
		case PType::Region:
		case PType::Groove:
		case PType::Line:
		case PType::Text:
		case PType::Scale:
		case PType::Jack:
		case PType::Ring:
		case PType::Dot:
		case PType::Arrow:
		case PType::Arc:
		case PType::Power:
			m_power = i;
			break;
		case PType::Logo:
		default:
			break;
		}
	}
	m_held.reset(m_keys.size() + m_buttons.size(), [this](int i, bool down) {
		if (i < int(m_keys.size()))
			m_proc.setKey(i, down);
		else
			m_proc.setButton(Button(i - int(m_keys.size())), down);
		repaint();
	});
	m_barSync = m_bar.add(160, "OFF: the MODULATOR clocks the arpeggio and sequencer. HOST: the DAW's tempo does (EXT CLK IN), "
	                           "a step per 1/16, 1/8, 1/4, 1/32 or triplet (click: next, shift-click: back)");

	// The brand marks come from the user's artwork (a file per place in artwork/sh101/, or an older
	// sh101.svg pack); without it the panel has none. Their ids are the panel's logo ids without
	// "logo_".
	for (const auto &mark : p.artwork().marks())
		m_logos["logo_" + mark.id.toStdString()] = mark;

	m_snap = p.snapshot();
	startTimerHz(30);
}

HardwareView::~HardwareView() { releaseAll(); }

// ====================================================================== parameters and panel actions

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

int HardwareView::detentOf(const Control &c) const
{
	auto *p = dynamic_cast<juce::AudioParameterChoice *>(m_proc.params().getParameter(c.param));
	if (p == nullptr)
		return 0;
	const int item = p->getIndex();
	for (size_t d = 0; d < c.order.size(); ++d)
		if (c.order[d] == item)
			return int(d);
	return 0;
}

void HardwareView::setDetent(const Control &c, int detent)
{
	auto *p = dynamic_cast<juce::AudioParameterChoice *>(m_proc.params().getParameter(c.param));
	if (p == nullptr || c.order.empty())
		return;
	const int item = c.order[size_t(juce::jlimit(0, int(c.order.size()) - 1, detent))];
	const int n = p->choices.size();
	p->setValueNotifyingHost(n > 1 ? float(item) / float(n - 1) : 0.0f);
}

float HardwareView::sliderValueAt(const Control &c, float yMm) const
{
	const Element &e = el(c.element);
	return juce::jlimit(0.0f, 1.0f, (yMm - e.v[3]) / (e.v[4] - e.v[3]));
}

void HardwareView::setBender(float bend, float push)
{
	m_bend = juce::jlimit(-1.0f, 1.0f, bend);
	m_push = juce::jlimit(0.0f, 1.0f, push);
	m_proc.setBender(m_bend);
	m_proc.setModPush(m_push);
	repaint();
}

void HardwareView::releaseAll()
{
	m_held.releaseAll(true);
	if (std::abs(m_bend) > 0.0f || std::abs(m_push) > 0.0f)
		setBender(0, 0);
	m_leverKeys.clear();
	m_mouseKey = -1;
}

void HardwareView::timerCallback()
{
	m_snap = m_proc.snapshot();
	repaint();
}

// ====================================================================== static artwork

void HardwareView::drawPrint(juce::Graphics &g, int index)
{
	const Element &e = el(index);
	const auto box = boxOf(e);
	switch (e.type) {
	case PType::Text:
		drawLegend(g, e.label, box, e.v[0], col(e.colour));
		break;
	case PType::Line: {
		juce::Path p;
		p.startNewSubPath(point(e, 0));
		for (int k = 1; k < e.pointCount; ++k)
			p.lineTo(point(e, k));
		g.setColour(col(e.colour));
		g.strokePath(p, juce::PathStrokeType(e.v[0], juce::PathStrokeType::mitered, juce::PathStrokeType::square));
		break;
	}
	case PType::Groove:
		groove(g, point(e, 0), point(e, 1), e.v[0]);
		break;
	case PType::Scale:
		g.setColour(col(e.colour));
		for (int k = 0; k < int(e.v[2]); ++k) {
			const float y = e.v[0] + float(k) * e.v[1];
			g.fillRoundedRectangle(e.box[0], y - e.v[3] * 0.5f, e.box[2] - e.box[0], e.v[3], e.v[3] * 0.45f);
		}
		break;
	case PType::Ring:
		g.setColour(col(e.colour));
		g.drawEllipse(e.v[0] - e.v[2], e.v[1] - e.v[2], 2 * e.v[2], 2 * e.v[2], e.v[3]);
		break;
	case PType::Dot:
		g.setColour(col(e.colour));
		g.fillEllipse(e.v[0] - e.v[2], e.v[1] - e.v[2], 2 * e.v[2], 2 * e.v[2]);
		break;
	case PType::Arrow:
		g.setColour(col(e.colour));
		arrowHead(g, {e.v[0], e.v[1]}, e.v[2], e.v[3]);
		break;
	case PType::Arc: {
		const juce::Point<float> c(e.v[0], e.v[1]);
		juce::Path p;
		p.addCentredArc(c.x, c.y, e.v[2], e.v[2], 0, juce::degreesToRadians(e.v[3]), juce::degreesToRadians(e.v[4]), true);
		g.setColour(col(e.colour));
		g.strokePath(p, juce::PathStrokeType(e.v[5]));
		// The ends point down at the b and # signs.
		arrowHead(g, polar(c, e.v[2], e.v[3]) + juce::Point<float>(0.2f, 1.2f), 200, 1.5f);
		arrowHead(g, polar(c, e.v[2], e.v[4]) + juce::Point<float>(-0.2f, 1.2f), 160, 1.5f);
		break;
	}
	case PType::Logo:
		// A place's own file as it looks, fitted to the mark's box; a pack's outline where it is, in
		// the print colour.
		if (auto it = m_logos.find(e.id); it != m_logos.end()) {
			if (it->second.picture != nullptr) {
				it->second.picture->drawWithin(g, boxOf(e));
			} else {
				g.setColour(col(e.colour));
				g.fillPath(it->second.outline);
			}
		}
		break;
	case PType::Case:
	case PType::Region:
	case PType::Jack:
	case PType::Slider:
	case PType::VSwitch:
	case PType::HSwitch:
	case PType::Knob:
	case PType::Rotary:
	case PType::Button:
	case PType::Power:
	case PType::Led:
	case PType::Key:
	case PType::Bender:
	default:
		break;
	}
}

void HardwareView::renderStatic(juce::Graphics &g)
{
	g.fillAll(juce::Colour(0xff1b1a19));
	// Case, with a bevelled rim.
	{
		juce::ColourGradient rim(col(c_case).brighter(0.15f), 0, 0, col(c_case).darker(0.25f), 0, panel::kHeight, false);
		g.setGradientFill(rim);
		g.fillRoundedRectangle(0, 0, panel::kWidth, panel::kHeight, 3.0f);
		g.setColour(juce::Colours::white.withAlpha(0.18f));
		g.drawRoundedRectangle(0.3f, 0.3f, panel::kWidth - 0.6f, panel::kHeight - 0.6f, 2.8f, 0.4f);
	}

	for (int i = 0; i < kElementCount; ++i) {
		const Element &e = el(i);
		if (e.type != PType::Region)
			continue;
		const auto b = boxOf(e);
		switch (juce::roundToInt(e.v[0])) {
		case 0: { // rear shelf: the jack face in shade behind a lit lip
			g.setColour(col(e.colour));
			g.fillRect(b);
			// The jacks sit on a face that slopes away from the light (very dark in the Commons photo's
			// raking light, barely darker in the dealers' even light): drawn in between.
			juce::ColourGradient shade(col(e.colour).darker(0.05f), 0, 4.0f, col(c_shelf_dark).darker(0.12f), 0, 18.4f, false);
			g.setGradientFill(shade);
			g.fillRect(b.getX(), 4.0f, b.getWidth(), 14.4f);
			g.setColour(juce::Colours::black.withAlpha(0.3f));
			g.fillRect(b.getX(), 18.4f, b.getWidth(), 0.9f);
			break;
		}
		case 1: { // the raised panel
			juce::ColourGradient sheen(col(e.colour).brighter(0.06f), 0, b.getY(), col(e.colour).darker(0.04f), 0, b.getBottom(), false);
			g.setGradientFill(sheen);
			g.fillRect(b);
			g.setColour(juce::Colours::white.withAlpha(0.22f));
			g.fillRect(b.getX(), b.getY(), b.getWidth(), 0.35f);
			break;
		}
		case 2: // the lower left panel and the right cheek
			g.setColour(col(e.colour));
			g.fillRect(b);
			break;
		case 3: // the key bed
			g.setColour(col(e.colour));
			g.fillRect(b);
			break;
		case 4: // battery lid and jack block: faint moulded outlines
			g.setColour(col(c_groove_dark).withAlpha(0.35f));
			g.drawRect(b, 0.35f);
			break;
		case 5: // insets: the MODULATION GRIP jacks, the DC socket
			g.setColour(col(e.colour));
			g.fillRoundedRectangle(b, 0.6f);
			g.setColour(juce::Colours::white.withAlpha(0.12f));
			g.drawRoundedRectangle(b.translated(0.25f, 0.3f), 0.6f, 0.3f);
			break;
		default:
			break;
		}
	}

	// Slider and switch channels: a recessed frame with a dark left and lower edge, and the slot.
	// The front face of the raised panel, lit, where it steps down to the keys and the left panel.
	{
		const float y0 = 162.6f, y1 = 166.0f;
		juce::ColourGradient face(col(c_panel).brighter(0.42f), 0, y0, col(c_panel).brighter(0.12f), 0, y1, false);
		g.setGradientFill(face);
		g.fillRect(3.5f, y0, 85.1f, y1 - y0);
		g.fillRect(537.9f, y0, 28.6f, y1 - y0);
		g.setColour(juce::Colours::black.withAlpha(0.3f));
		g.fillRect(3.5f, y0, 563.0f, 0.3f);
		// over the keys the face is in the keys' shadow
		juce::ColourGradient keys(juce::Colour(0xff3a3835), 0, y0, juce::Colour(0xff1d1c1b), 0, y0 + 1.6f, false);
		g.setGradientFill(keys);
		g.fillRect(88.6f, y0, 449.3f, 1.6f);
	}
	// The case's right side in shade.
	{
		juce::ColourGradient side(juce::Colours::transparentBlack, 563.0f, 0, juce::Colours::black.withAlpha(0.4f), panel::kWidth, 0, false);
		g.setGradientFill(side);
		g.fillRect(563.0f, 0.0f, panel::kWidth - 563.0f, panel::kHeight);
	}

	auto channel = [&](const Element &e) {
		const auto b = boxOf(e);
		g.setColour(col(c_panel).darker(0.06f));
		g.fillRect(b);
		g.setColour(juce::Colours::black.withAlpha(0.55f));
		g.fillRect(b.getX(), b.getY(), 0.6f, b.getHeight());
		g.fillRect(b.getX(), b.getBottom() - 0.7f, b.getWidth(), 0.7f);
		g.fillRect(b.getX(), b.getY(), b.getWidth(), 0.35f);
		g.setColour(juce::Colours::white.withAlpha(0.18f));
		g.fillRect(b.getRight() - 0.35f, b.getY(), 0.35f, b.getHeight());
		const float cx = e.v[0];
		g.setColour(col(c_slot_black));
		g.fillRect(cx - 1.6f, e.v[1], 3.2f, e.v[2] - e.v[1]);
	};
	for (const auto &s : m_sliders)
		channel(el(s.element));
	for (const auto &s : m_switches)
		if (el(s.element).type == PType::VSwitch)
			channel(el(s.element));
		else if (el(s.element).type == PType::HSwitch) {
			const auto b = boxOf(el(s.element));
			g.setColour(col(c_slot_black));
			g.fillRoundedRectangle(b, b.getHeight() * 0.5f);
			g.setColour(juce::Colours::white.withAlpha(0.2f));
			g.drawRoundedRectangle(b.translated(0.2f, 0.25f), b.getHeight() * 0.5f, 0.3f);
		}

	// Print, in layer order.
	for (PType t : {PType::Groove, PType::Scale, PType::Line, PType::Arc, PType::Ring, PType::Dot, PType::Arrow, PType::Text, PType::Logo})
		for (int i = 0; i < kElementCount; ++i)
			if (el(i).type == t)
				drawPrint(g, i);

	// Printed knob scales: VOLUME and PORTAMENTO ticks, the rotary switches' detent ticks.
	g.setColour(col(c_print_white));
	for (const auto &k : m_knobs) {
		const Element &e = el(k.element);
		const int n = juce::roundToInt(e.v[5]);
		for (int t = 0; t < n; ++t) {
			const float a = e.v[3] + (e.v[4] - e.v[3]) * float(t) / float(n - 1);
			g.drawLine({polar({e.v[0], e.v[1]}, e.v[6], a), polar({e.v[0], e.v[1]}, e.v[7], a)}, 0.55f);
		}
	}
	for (const auto &s : m_switches) {
		const Element &e = el(s.element);
		if (e.type != PType::Rotary)
			continue;
		for (int t = 0; t < e.pointCount; ++t) {
			const float a = point(e, t).x;
			g.drawLine({polar({e.v[0], e.v[1]}, e.v[6], a), polar({e.v[0], e.v[1]}, e.v[7], a)}, 0.6f);
		}
	}

	// Jacks: phone and mini jacks with their nuts, the DC socket.
	for (int i = 0; i < kElementCount; ++i) {
		const Element &e = el(i);
		if (e.type != PType::Jack)
			continue;
		const juce::Point<float> c(e.v[0], e.v[1]);
		const float r = e.v[2] * 0.5f;
		if (juce::roundToInt(e.v[3]) == 3) {
			g.setColour(juce::Colour(0xff2a2a2a));
			g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
			continue;
		}
		// A bright chrome nut round a dark hole.
		g.setColour(juce::Colours::black.withAlpha(0.45f));
		g.fillEllipse(c.x - r - 0.3f, c.y - r + 0.2f, 2 * r + 0.8f, 2 * r + 0.8f);
		juce::ColourGradient nut(col(c_jack_ring).brighter(0.7f), c.x - r, c.y - r, col(c_jack_ring).darker(0.4f), c.x + r, c.y + r, false);
		g.setGradientFill(nut);
		g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
		const float h = r * 0.74f;
		g.setColour(col(c_jack_black));
		g.fillEllipse(c.x - h, c.y - h, 2 * h, 2 * h);
	}

	// Key wells for the buttons, LED windows, the power switch and the bender slot.
	for (int b : m_buttons) {
		if (b < 0)
			continue;
		g.setColour(juce::Colour(0xff151414));
		g.fillRoundedRectangle(boxOf(el(b)), 0.5f);
	}
	for (int i = 0; i < kElementCount; ++i)
		if (el(i).type == PType::Led) {
			g.setColour(juce::Colours::black.withAlpha(0.6f));
			g.fillRect(boxOf(el(i)).expanded(0.35f));
		} else if (el(i).type == PType::Power) {
			const auto b = boxOf(el(i));
			g.setColour(juce::Colour(0xff121112));
			g.fillRoundedRectangle(b.expanded(0.6f), 0.6f);
			juce::ColourGradient cap(juce::Colour(0xff2e2c2f), 0, b.getY(), juce::Colour(0xff151416), 0, b.getBottom(), false);
			g.setGradientFill(cap);
			g.fillRoundedRectangle(b, 0.5f);
			g.setColour(juce::Colours::white.withAlpha(0.55f));
			g.fillRect(b.getX() + 0.6f, b.getBottom() - 1.4f, b.getWidth() - 1.2f, 0.35f);
		}
	if (m_bender >= 0) {
		const auto b = boxOf(el(m_bender));
		g.setColour(col(c_slot_black));
		g.fillRoundedRectangle(b, 0.8f);
		g.setColour(juce::Colours::white.withAlpha(0.15f));
		g.drawRoundedRectangle(b.translated(0.25f, 0.3f), 0.8f, 0.35f);
	}
}

// ====================================================================== live parts

void HardwareView::drawSlider(juce::Graphics &g, const Control &c, float v, bool hot)
{
	const Element &e = el(c.element);
	const float cx = e.v[0], cy = e.v[3] + (e.v[4] - e.v[3]) * v;
	const float w = e.v[5], h = e.v[6];
	const juce::Rectangle<float> cap(cx - w * 0.5f, cy - h * 0.5f, w, h);
	g.setColour(juce::Colours::black.withAlpha(0.45f));
	g.fillRoundedRectangle(cap.translated(0.5f, 1.0f).expanded(0.3f), 1.0f);
	juce::ColourGradient body(juce::Colour(hot ? 0xff3a383c : 0xff302e31), cap.getX(), cap.getY(), juce::Colour(0xff161517), cap.getX(), cap.getBottom(), false);
	g.setGradientFill(body);
	g.fillRoundedRectangle(cap, 0.9f);
	// The grip: a ridge across the top half and a step below the line.
	g.setColour(juce::Colours::white.withAlpha(0.13f));
	g.fillRect(cap.getX() + 0.5f, cap.getY() + 0.35f, w - 1.0f, 0.4f);
	g.fillRect(cap.getX() + 0.35f, cap.getY() + 0.5f, 0.35f, h - 1.0f);
	g.setColour(juce::Colours::black.withAlpha(0.35f));
	g.fillRect(cap.getX() + 0.6f, cy + h * 0.16f, w - 1.2f, 0.45f);
	static const uint32_t stripes[] = {c_stripe_green, c_stripe_orange, c_stripe_yellow};
	const auto stripe = col(stripes[juce::jlimit(0, 2, juce::roundToInt(e.v[7]))]);
	g.setColour(stripe);
	g.fillRect(cap.getX() + 0.6f, cy - 0.45f, w - 1.2f, 0.85f);
	g.setColour(stripe.brighter(0.6f).withAlpha(0.7f));
	g.fillRect(cap.getX() + 0.6f, cy - 0.45f, w - 1.2f, 0.25f);
}

void HardwareView::drawSwitch(juce::Graphics &g, const Control &c, int detent, bool hot)
{
	const Element &e = el(c.element);
	detent = juce::jlimit(0, juce::jmax(0, e.pointCount - 1), detent);
	if (e.type == PType::VSwitch) {
		const float cx = e.v[0], y = point(e, detent).y;
		const float w = e.v[5] - 0.6f, h = e.v[6];
		const juce::Rectangle<float> cap(cx - w * 0.5f, y - h * 0.47f, w, h);
		g.setColour(juce::Colours::black.withAlpha(0.45f));
		g.fillRoundedRectangle(cap.translated(0.5f, 1.0f).expanded(0.3f), 1.0f);
		juce::ColourGradient body(juce::Colour(hot ? 0xff3a383c : 0xff2e2c30), cap.getX(), cap.getY(), juce::Colour(0xff141315), cap.getX(), cap.getBottom(), false);
		g.setGradientFill(body);
		g.fillRoundedRectangle(cap, 1.1f);
		g.setColour(juce::Colours::white.withAlpha(0.12f));
		g.fillRect(cap.getX() + 0.35f, cap.getY() + 0.6f, 0.35f, h - 1.2f);
		g.setColour(juce::Colour(0xff0a0a0a));
		g.fillRect(cx - 1.4f, cap.getBottom(), 2.8f, 1.3f);
		const auto stripe = col(c_stripe_yellow);
		g.setColour(stripe);
		g.fillRect(cap.getX() + 0.7f, y - 0.42f, w - 1.4f, 0.85f);
		g.setColour(stripe.brighter(0.6f).withAlpha(0.6f));
		g.fillRect(cap.getX() + 0.7f, y - 0.42f, w - 1.4f, 0.25f);
	} else if (e.type == PType::HSwitch) {
		const auto p = point(e, detent);
		const float r = e.v[0] * 0.5f;
		g.setColour(juce::Colours::black.withAlpha(0.5f));
		g.fillEllipse(p.x - r + 0.4f, p.y - r + 0.6f, 2 * r, 2 * r);
		juce::ColourGradient ball(juce::Colour(0xfff4f2ee), p.x - r * 0.4f, p.y - r * 0.5f, juce::Colour(hot ? 0xff8d8a84 : 0xff6d6a64), p.x + r, p.y + r, true);
		g.setGradientFill(ball);
		g.fillEllipse(p.x - r, p.y - r, 2 * r, 2 * r);
	} else { // rotary
		const juce::Point<float> c0(e.v[0], e.v[1]);
		const float r = e.v[2] * 0.5f;
		knobBody(g, c0, r);
		const float a = point(e, detent).x;
		g.setColour(col(c_knob_pointer));
		g.drawLine({polar(c0, r * 0.5f, a), polar(c0, r * 0.97f, a)}, 0.9f);
		if (hot) {
			g.setColour(juce::Colours::white.withAlpha(0.08f));
			g.fillEllipse(c0.x - r, c0.y - r, 2 * r, 2 * r);
		}
	}
}

void HardwareView::drawKnob(juce::Graphics &g, const Control &c, float v, bool hot)
{
	const Element &e = el(c.element);
	const juce::Point<float> c0(e.v[0], e.v[1]);
	const float r = e.v[2] * 0.5f;
	knobBody(g, c0, r);
	const float a = e.v[3] + (e.v[4] - e.v[3]) * v;
	g.setColour(col(c_knob_pointer));
	g.drawLine({polar(c0, r * 0.5f, a), polar(c0, r * 0.97f, a)}, 0.9f);
	if (hot) {
		g.setColour(juce::Colours::white.withAlpha(0.08f));
		g.fillEllipse(c0.x - r, c0.y - r, 2 * r, 2 * r);
	}
}

void HardwareView::drawButton(juce::Graphics &g, int element, bool down)
{
	const Element &e = el(element);
	auto cap = juce::Rectangle<float>(e.v[0], e.v[1], e.v[2] - e.v[0], e.v[3] - e.v[1]);
	if (down)
		cap = cap.translated(0.1f, 0.25f);
	juce::ColourGradient body(col(c_cap_light).brighter(down ? 0.0f : 0.08f), 0, cap.getY(), col(c_cap_light).darker(down ? 0.35f : 0.18f), 0, cap.getBottom(), false);
	g.setGradientFill(body);
	g.fillRoundedRectangle(cap, 0.45f);
	g.setColour(juce::Colours::white.withAlpha(down ? 0.25f : 0.6f));
	g.fillRect(cap.getX() + 0.3f, cap.getY() + 0.2f, cap.getWidth() - 0.6f, 0.3f);
	g.setColour(juce::Colours::black.withAlpha(0.3f));
	g.fillRect(cap.getX() + 0.3f, cap.getBottom() - 0.45f, cap.getWidth() - 0.6f, 0.45f);
}

void HardwareView::drawLed(juce::Graphics &g, int element, float level)
{
	const auto b = boxOf(el(element));
	level = juce::jlimit(0.0f, 1.0f, level);
	if (level > 0.02f) {
		g.setColour(col(c_led_on).withAlpha(0.28f * level));
		g.fillRoundedRectangle(b.expanded(1.6f, 1.2f), 1.4f);
	}
	g.setColour(col(c_led_off).interpolatedWith(col(c_led_on), level));
	g.fillRect(b);
	g.setColour(juce::Colours::white.withAlpha(0.12f + 0.3f * level));
	g.fillRect(b.getX() + 0.4f, b.getY() + 0.35f, b.getWidth() - 0.8f, 0.4f);
}

void HardwareView::drawKey(juce::Graphics &g, int element, bool down)
{
	const Element &e = el(element);
	const auto b = boxOf(e);
	if (e.v[1] > 0.5f) {
		const float front = down ? 0.9f : 0.0f;
		juce::Path p;
		p.addRoundedRectangle(b.getX(), b.getY(), b.getWidth(), b.getHeight() - front, 0.8f, 0.8f, false, false, true, true);
		juce::ColourGradient face(col(c_key_white).darker(down ? 0.12f : 0.0f), 0, b.getY(), col(c_key_white).darker(down ? 0.2f : 0.06f), 0, b.getBottom(), false);
		g.setGradientFill(face);
		g.fillPath(p);
		g.setColour(juce::Colours::black.withAlpha(down ? 0.18f : 0.1f));
		g.fillRect(b.getX(), b.getY(), b.getWidth(), 2.2f);
		g.setColour(juce::Colours::black.withAlpha(0.12f));
		g.fillRect(b.getRight() - 0.4f, b.getY(), 0.4f, b.getHeight() - front);
	} else {
		g.setColour(juce::Colours::black.withAlpha(0.45f));
		g.fillRoundedRectangle(b.translated(0.6f, 0.8f), 1.0f);
		juce::ColourGradient body(col(c_key_black).brighter(down ? 0.0f : 0.15f), b.getX(), 0, col(c_key_black).darker(0.4f), b.getRight(), 0, false);
		g.setGradientFill(body);
		g.fillRoundedRectangle(b, 1.0f);
		// The top face narrows towards the front; it is lit along its left edge.
		const float inset = 1.8f, front = down ? 4.5f : 6.0f;
		const juce::Rectangle<float> top(b.getX() + inset, b.getY(), b.getWidth() - 2 * inset, b.getHeight() - front);
		juce::ColourGradient face(juce::Colour(0xff3a383b), 0, top.getY(), juce::Colour(down ? 0xff1c1b1d : 0xff2a292b), 0, top.getBottom(), false);
		g.setGradientFill(face);
		g.fillRoundedRectangle(top, 0.8f);
		g.setColour(juce::Colours::white.withAlpha(0.18f));
		g.fillRect(top.getX(), top.getY() + 2.0f, 0.5f, top.getHeight() - 4.0f);
		g.setColour(juce::Colours::white.withAlpha(0.35f));
		g.fillRoundedRectangle(top.getX() + 0.8f, top.getBottom() - 1.4f, top.getWidth() - 1.6f, 0.6f, 0.3f);
	}
}

void HardwareView::drawBender(juce::Graphics &g, float bend, float push)
{
	if (m_bender < 0)
		return;
	const Element &e = el(m_bender);
	const auto slot = boxOf(e);
	// The paddle pivots under the panel: it swings a little sideways and tips forward when pushed.
	const juce::Rectangle<float> lever0(e.v[0], e.v[1], e.v[2] - e.v[0], e.v[3] - e.v[1]);
	const auto pivot = juce::Point<float>(lever0.getCentreX(), lever0.getBottom() + 25.0f);
	const auto t = juce::AffineTransform::rotation(juce::degreesToRadians(bend * 2.8f), pivot.x, pivot.y).translated(0, -push * 1.2f);
	juce::Path paddle;
	paddle.addRoundedRectangle(lever0, 0.9f);
	paddle.applyTransform(t);
	{
		juce::Graphics::ScopedSaveState clip(g);
		g.reduceClipRegion(slot.toNearestInt().expanded(1));
		g.setColour(juce::Colours::black.withAlpha(0.5f));
		g.fillPath(paddle, juce::AffineTransform::translation(0.4f, 0.8f));
		juce::ColourGradient body(juce::Colour(0xff2c2a2d), lever0.getX(), lever0.getY(), juce::Colour(0xff0f0e10), lever0.getX(), lever0.getBottom(), false);
		g.setGradientFill(body);
		g.fillPath(paddle);
		juce::Path ridge;
		ridge.addRectangle(lever0.getX() + 1.0f, lever0.getY() + 0.5f, lever0.getWidth() - 2.0f, 0.35f);
		ridge.applyTransform(t);
		g.setColour(juce::Colours::white.withAlpha(0.16f));
		g.fillPath(ridge);
	}
	// The stop at the front edge.
	const auto stop = juce::Point<float>(lever0.getCentreX(), slot.getBottom() - 0.6f).transformedBy(t);
	g.setColour(juce::Colour(0xff0d0c0d));
	g.fillEllipse(stop.x - 1.6f, stop.y - 1.1f, 3.2f, 2.2f);
	g.setColour(juce::Colour(0xffbdb8ae));
	g.fillEllipse(stop.x - 0.5f, stop.y - 0.35f, 1.0f, 0.8f);
}

// ====================================================================== plugin bar

void HardwareView::drawBar(juce::Graphics &g)
{
	namespace lp = lemus::panel;
	lp::drawBarBackground(g, m_bar);
	const bool sync = paramValue(SH101Processor::Id::sync) > 0.5f;
	juce::String div = "1/16";
	if (auto *p = dynamic_cast<juce::AudioParameterChoice *>(m_proc.params().getParameter(SH101Processor::Id::clockDiv)))
		div = p->getCurrentChoiceName();
	lp::drawPill(g, m_bar[m_barSync], sync ? "SYNC: HOST " + div : juce::String("SYNC: OFF"), sync, m_hover.kind == HitKind::BarSync, kMono);
	lp::drawPill(g, m_bar.help(), "?", m_showHelp, m_hover.kind == HitKind::BarHelp, kMono);

	// Status: clock source, what the CPU sends the synth board, what the voice is doing.
	juce::String status;
	status << (m_snap.synced ? "HOST " + juce::String(m_snap.bpm, 1) + " BPM" : juce::String("LFO CLOCK"));
	const auto &l = m_snap.lines;
	status << "   KCV " << juce::String(l.kcv / 12.0, 3) << " V" << (l.gate ? "  GATE" : "      ");
	if (m_snap.probe.vcoHz > 0)
		status << "   VCO " << juce::String(m_snap.probe.vcoHz, 1) << " Hz";
	if (m_snap.probe.vcfHz > 0)
		status << "   VCF " << juce::String(juce::roundToInt(m_snap.probe.vcfHz)) << " Hz";
	status << "   OCT " << (m_octave > 0 ? "+" : "") << m_octave;
	if (!m_snap.powered)
		status = "POWER OFF";
	else if (!m_proc.romLoaded())
		status = lp::voiceModeStatus("CPU ROM", false) + juce::String(m_snap.muted ? "   MUTED (power-on)" : "");
	else if (m_snap.muted)
		status << "   MUTED (power-on)";
	// Keys and buttons a right-click holds take the status over until they're let go: a held key
	// keeps its note, a held button its mode, and the firmware may wait for it to come up.
	const auto held = m_held.latchedNames([this](int i) { return heldName(i, int(m_keys.size())); });
	if (!held.isEmpty())
		status = lp::heldStatus(held);
	lp::drawStatus(g, m_bar.status(), status, kMono, held.isEmpty() ? lp::col::status : lp::col::accent);
}

void HardwareView::drawHelp(juce::Graphics &g)
{
	lemus::panel::drawHelp(g, {0, 0, float(kWidth), float(kPanelHeight)}, "SH-101",
	                       lemus::panel::byPlace({
	                           "A W S E D F T G Y H U J K O L P ; '   keys C C# D D# E F F# G G# A A# B C C# D D# E F",
	                           "Z  X                                  octave down / up (the 32 keys run F to C)",
	                           "1 2                                   LOAD  PLAY            (SEQUENCER)",
	                           "3 4 5                                 DOWN  U&D  UP         (ARPEGGIO)",
	                           "6                                     HOLD                  (LEGATO while loading)",
	                           "7                                     KEY TRANSPOSE         (REST while loading)",
	                           "Space                                 PLAY",
	                           "Left / Right, Up                      the bender lever: bend, push (LFO MOD)",
	                           "",
	                           "Switches: click or drag. The bender: drag sideways, and forward for MOD. POWER switches it off and on.",
	                           "MIDI notes press the keys: the firmware does the priority, arpeggio and sequencer.",
	                           "Pitch bend moves the lever, the mod wheel pushes it, CC 64 is the HOLD pedal.",
	                           "SYNC: HOST 1/16 clocks the arpeggio and sequencer from the DAW, a step per 16th; click SYNC for 1/8,",
	                           "1/4, 1/32 or triplets. With SYNC off the MODULATOR clocks them, as with nothing in EXT CLK IN.",
	                           "Sequence: LOAD, then one key per step (HOLD + key = legato, KEY TRANSPOSE = rest); PLAY.",
	                       }),
	                       kMono);
}

void HardwareView::drawTooltip(juce::Graphics &g)
{
	juce::String text;
	juce::Point<float> at;
	const Hit &h = m_drag.kind != HitKind::None ? m_drag : m_hover;
	auto below = [](const Element &e) { return juce::Point<float>(0.5f * (e.box[0] + e.box[2]) * kPxPerMm, e.box[3] * kPxPerMm + 6); };
	if (h.kind == HitKind::Slider) {
		text = paramText(m_sliders[size_t(h.index)].param);
		at = below(el(m_sliders[size_t(h.index)].element));
	} else if (h.kind == HitKind::Knob) {
		text = paramText(m_knobs[size_t(h.index)].param);
		at = below(el(m_knobs[size_t(h.index)].element));
	} else if (h.kind == HitKind::Switch) {
		text = paramText(m_switches[size_t(h.index)].param);
		at = below(el(m_switches[size_t(h.index)].element));
	} else if (h.kind == HitKind::Bender) {
		text = "Bender: drag sideways to bend, forward for LFO MOD";
		at = below(el(m_bender));
		at.y -= 70;
	} else if (h.kind == HitKind::Power) {
		text = m_proc.powered() ? "POWER: on. Click to switch off (the battery keeps the sequence)"
		                        : "POWER: off. Click to switch on (the output is muted for about a second)";
		at = below(el(m_power));
	} else if (h.kind == HitKind::BarSync || h.kind == HitKind::BarHelp) {
		if (!m_showHelp)
			lemus::panel::drawBarTip(g, m_bar, h.kind == HitKind::BarSync ? m_barSync : lemus::panel::Bar::kHelp,
			                         getLocalBounds().toFloat(), kMono);
		return;
	}
	if (!m_showHelp)
		lemus::panel::drawTooltip(g, text, at, getLocalBounds().toFloat(), kMono);
}

// ====================================================================== paint

void HardwareView::paint(juce::Graphics &g)
{
	const float phys = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
	if (!m_static.isValid() || std::abs(phys - m_staticScale) > 0.01f) {
		m_staticScale = phys;
		m_static = juce::Image(juce::Image::RGB, juce::roundToInt(kWidth * phys), juce::roundToInt(kPanelHeight * phys), true);
		juce::Graphics sg(m_static);
		sg.addTransform(juce::AffineTransform::scale(phys * kPxPerMm));
		renderStatic(sg);
		// Moulding grain: faint per-pixel noise on the grey plastic (not on the print or the keys).
		juce::Image::BitmapData bd(m_static, juce::Image::BitmapData::readWrite);
		juce::Random rng(101);
		for (int y = 0; y < bd.height; ++y)
			for (int x = 0; x < bd.width; ++x) {
				auto c = bd.getPixelColour(x, y);
				const float br = c.getBrightness();
				if (br < 0.25f || br > 0.6f || c.getSaturation() > 0.15f)
					continue;
				const float n = (rng.nextFloat() - 0.5f) * 0.05f;
				bd.setPixelColour(x, y, n > 0 ? c.brighter(n) : c.darker(-n));
			}
	}
	g.drawImageTransformed(m_static, juce::AffineTransform::scale(1.0f / phys));

	{
		juce::Graphics::ScopedSaveState s(g);
		g.addTransform(juce::AffineTransform::scale(kPxPerMm));
		auto hot = [this](HitKind k, int i) {
			return (m_hover.kind == k && m_hover.index == i) || (m_drag.kind == k && m_drag.index == i);
		};
		for (size_t k = 0; k < m_keys.size(); ++k)
			if (el(m_keys[k]).v[1] > 0.5f)
				drawKey(g, m_keys[k], m_held.down(int(k)) || m_snap.keys[k]);
		for (size_t k = 0; k < m_keys.size(); ++k)
			if (el(m_keys[k]).v[1] < 0.5f)
				drawKey(g, m_keys[k], m_held.down(int(k)) || m_snap.keys[k]);
		for (size_t i = 0; i < m_sliders.size(); ++i)
			drawSlider(g, m_sliders[i], paramValue(m_sliders[i].param), hot(HitKind::Slider, int(i)));
		for (size_t i = 0; i < m_switches.size(); ++i)
			drawSwitch(g, m_switches[i], detentOf(m_switches[i]), hot(HitKind::Switch, int(i)));
		for (size_t i = 0; i < m_knobs.size(); ++i)
			drawKnob(g, m_knobs[i], paramValue(m_knobs[i].param), hot(HitKind::Knob, int(i)));
		for (size_t b = 0; b < m_buttons.size(); ++b)
			if (m_buttons[b] >= 0)
				drawButton(g, m_buttons[b], m_held.down(buttonKey(int(b))));
		for (size_t l = 0; l < m_leds.size(); ++l)
			if (m_leds[l] >= 0)
				drawLed(g, m_leds[l], m_snap.leds[l]);
		if (m_ledRate >= 0)
			drawLed(g, m_ledRate, m_snap.clock ? 1.0f : 0.0f);
		if (m_ledPower >= 0)
			drawLed(g, m_ledPower, m_snap.powered ? 1.0f : 0.0f);
		if (m_power >= 0 && (hot(HitKind::Power, 0))) {
			g.setColour(juce::Colour(0xffff8c1a));
			g.drawRoundedRectangle(boxOf(el(m_power)).expanded(0.9f), 0.8f, 0.35f);
		}
		const bool mouseOnLever = m_drag.kind == HitKind::Bender || std::abs(m_bend) > 0.0f || std::abs(m_push) > 0.0f;
		drawBender(g, mouseOnLever ? m_bend : float(m_snap.bend), mouseOnLever ? m_push : float(m_snap.modPush));
	}
	drawBar(g);
	drawTooltip(g);
	if (m_showHelp)
		drawHelp(g);
}

// ====================================================================== interaction

HardwareView::Hit HardwareView::hitAt(juce::Point<float> p) const
{
	if (p.y >= kPanelHeight) {
		const int i = m_bar.hit(p);
		if (i == lemus::panel::Bar::kHelp)
			return {HitKind::BarHelp, 0};
		if (i == m_barSync)
			return {HitKind::BarSync, 0};
		return {};
	}
	const auto mm = toMm(p);
	if (m_power >= 0 && boxOf(el(m_power)).expanded(0.5f).contains(mm))
		return {HitKind::Power, 0};
	for (size_t b = 0; b < m_buttons.size(); ++b)
		if (m_buttons[b] >= 0 && boxOf(el(m_buttons[b])).expanded(0.5f).contains(mm))
			return {HitKind::Button, int(b)};
	for (size_t i = 0; i < m_knobs.size(); ++i) {
		const Element &e = el(m_knobs[i].element);
		if (juce::Point<float>(e.v[0], e.v[1]).getDistanceFrom(mm) < e.v[2] * 0.5f + 1.0f)
			return {HitKind::Knob, int(i)};
	}
	for (size_t i = 0; i < m_switches.size(); ++i) {
		const Element &e = el(m_switches[i].element);
		if (e.type == PType::Rotary ? juce::Point<float>(e.v[0], e.v[1]).getDistanceFrom(mm) < e.v[2] * 0.5f + 1.5f
		                            : boxOf(e).expanded(0.5f).contains(mm))
			return {HitKind::Switch, int(i)};
	}
	for (size_t i = 0; i < m_sliders.size(); ++i)
		if (boxOf(el(m_sliders[i].element)).expanded(0.5f).contains(mm))
			return {HitKind::Slider, int(i)};
	if (m_bender >= 0 && boxOf(el(m_bender)).expanded(2.0f).contains(mm))
		return {HitKind::Bender, 0};
	for (int pass = 0; pass < 2; ++pass)
		for (size_t k = 0; k < m_keys.size(); ++k) {
			const Element &e = el(m_keys[k]);
			if ((pass == 0) == (e.v[1] < 0.5f) && boxOf(e).contains(mm))
				return {HitKind::Key, int(k)};
		}
	return {};
}

void HardwareView::mouseMove(const juce::MouseEvent &e)
{
	const Hit h = hitAt(e.position);
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
	m_drag = hitAt(e.position);
	m_dragStart = e.position;
	const auto mm = toMm(e.position);
	switch (m_drag.kind) {
	case HitKind::Slider: {
		const auto &c = m_sliders[size_t(m_drag.index)];
		const Element &s = el(c.element);
		float v = paramValue(c.param);
		const float capY = s.v[3] + (s.v[4] - s.v[3]) * v;
		// A click off the cap moves the cap there, then drags it.
		if (std::abs(mm.y - capY) > s.v[6] * 0.5f) {
			v = sliderValueAt(c, mm.y);
			setParamValue(c.param, v);
		}
		m_dragStartValue = v;
		if (auto *p = m_proc.params().getParameter(c.param))
			p->beginChangeGesture();
		break;
	}
	case HitKind::Knob:
		m_dragStartValue = paramValue(m_knobs[size_t(m_drag.index)].param);
		if (auto *p = m_proc.params().getParameter(m_knobs[size_t(m_drag.index)].param))
			p->beginChangeGesture();
		break;
	case HitKind::Switch: {
		const auto &c = m_switches[size_t(m_drag.index)];
		const Element &s = el(c.element);
		m_dragStartValue = float(detentOf(c));
		if (s.type != PType::Rotary)
			mouseDrag(e);
		break;
	}
	// A right-click (ctrl-click on a Mac) holds a key or button down until it is clicked again, so
	// the steps that need two at once (HOLD with a key for a legato step, KEY TRANSPOSE with a key)
	// work with the mouse alone.
	case HitKind::Button:
		if (!m_held.mouseDown(buttonKey(m_drag.index), e.mods))
			m_drag = {};
		break;
	case HitKind::Key:
		if (m_held.mouseDown(m_drag.index, e.mods))
			m_mouseKey = m_drag.index;
		else
			m_drag = {};
		break;
	case HitKind::Power:
		m_proc.setPower(!m_proc.powered());
		break;
	case HitKind::Bender:
		m_dragStartValue = 0;
		break;
	case HitKind::BarSync:
		// OFF -> HOST 1/16 -> 1/8 -> 1/4 -> 1/32 -> 1/16T -> 1/8T -> OFF; shift- or right-click goes back.
		// The steps are the Sync Clock parameter's choices (-1: SYNC off).
		if (auto *div = dynamic_cast<juce::AudioParameterChoice *>(m_proc.params().getParameter(SH101Processor::Id::clockDiv))) {
			static constexpr int kOrder[] = {-1, 3, 1, 0, 5, 4, 2};
			constexpr int n = int(std::size(kOrder));
			int at = 0;
			if (paramValue(SH101Processor::Id::sync) > 0.5f)
				for (int k = 1; k < n; ++k)
					if (kOrder[k] == div->getIndex())
						at = k;
			const int next = (at + (e.mods.isRightButtonDown() || e.mods.isShiftDown() ? n - 1 : 1)) % n;
			if (kOrder[next] >= 0)
				div->setValueNotifyingHost(float(kOrder[next]) / float(div->choices.size() - 1));
			setParamValue(SH101Processor::Id::sync, kOrder[next] >= 0 ? 1.0f : 0.0f);
		}
		break;
	case HitKind::BarHelp:
		m_showHelp = !m_showHelp;
		break;
	case HitKind::None:
	default:
		break;
	}
	repaint();
}

void HardwareView::mouseDrag(const juce::MouseEvent &e)
{
	const auto mm = toMm(e.position);
	switch (m_drag.kind) {
	case HitKind::Slider: {
		const auto &c = m_sliders[size_t(m_drag.index)];
		const Element &s = el(c.element);
		const float travel = (s.v[3] - s.v[4]) * kPxPerMm;
		const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
		setParamValue(c.param, m_dragStartValue + (m_dragStart.y - e.position.y) / travel * fine);
		break;
	}
	case HitKind::Knob: {
		const float sens = e.mods.isShiftDown() ? 0.0008f : 0.004f;
		const float dy = (m_dragStart.y - e.position.y) + (e.position.x - m_dragStart.x) * 0.3f;
		setParamValue(m_knobs[size_t(m_drag.index)].param, m_dragStartValue + dy * sens);
		break;
	}
	case HitKind::Switch: {
		const auto &c = m_switches[size_t(m_drag.index)];
		const Element &s = el(c.element);
		int best = 0;
		float bestD = 1e9f;
		for (int d = 0; d < s.pointCount; ++d) {
			float dist;
			if (s.type == PType::Rotary) {
				const float deg = juce::radiansToDegrees(std::atan2(mm.x - s.v[0], -(mm.y - s.v[1])));
				dist = std::abs(point(s, d).x - deg);
			} else if (s.type == PType::VSwitch) {
				dist = std::abs(point(s, d).y - mm.y);
			} else {
				dist = std::abs(point(s, d).x - mm.x);
			}
			if (dist < bestD) {
				bestD = dist;
				best = d;
			}
		}
		if (s.type != PType::Rotary || e.getDistanceFromDragStart() > 3)
			setDetent(c, best);
		break;
	}
	case HitKind::Key: {
		// Sliding along the keys plays them in turn (not over a latched one).
		const Hit h = hitAt(e.position);
		if (h.kind == HitKind::Key && h.index != m_mouseKey && !m_held.latched(h.index)) {
			m_held.mouseUp(m_mouseKey);
			m_mouseKey = m_held.mouseDown(h.index, {}) ? h.index : -1;
		}
		break;
	}
	case HitKind::Bender: {
		const float bend = (e.position.x - m_dragStart.x) / (16.0f * kPxPerMm);
		const float push = (m_dragStart.y - e.position.y) / (8.0f * kPxPerMm);
		setBender(bend, push);
		break;
	}
	case HitKind::None:
	case HitKind::Button:
	case HitKind::BarSync:
	case HitKind::Power:
	case HitKind::BarHelp:
	default:
		break;
	}
	repaint();
}

void HardwareView::mouseUp(const juce::MouseEvent &e)
{
	switch (m_drag.kind) {
	case HitKind::Slider:
		if (auto *p = m_proc.params().getParameter(m_sliders[size_t(m_drag.index)].param))
			p->endChangeGesture();
		break;
	case HitKind::Knob:
		if (auto *p = m_proc.params().getParameter(m_knobs[size_t(m_drag.index)].param))
			p->endChangeGesture();
		break;
	case HitKind::Switch: {
		// A click on a rotary switch without dragging steps it (shift or right-click steps back).
		const auto &c = m_switches[size_t(m_drag.index)];
		if (el(c.element).type == PType::Rotary && e.getDistanceFromDragStart() <= 3) {
			const int n = int(c.order.size());
			const int pos = juce::roundToInt(m_dragStartValue);
			setDetent(c, (pos + (e.mods.isRightButtonDown() || e.mods.isShiftDown() ? n - 1 : 1)) % n);
		}
		break;
	}
	case HitKind::Button:
		m_held.mouseUp(buttonKey(m_drag.index));
		break;
	case HitKind::Key:
		if (m_mouseKey >= 0)
			m_held.mouseUp(m_mouseKey);
		m_mouseKey = -1;
		break;
	case HitKind::Bender:
		setBender(0, 0); // the lever springs back
		break;
	case HitKind::None:
	case HitKind::BarSync:
	case HitKind::Power:
	case HitKind::BarHelp:
	default:
		break;
	}
	m_drag = {};
	repaint();
}

void HardwareView::mouseDoubleClick(const juce::MouseEvent &e)
{
	const Hit h = hitAt(e.position);
	const char *id = h.kind == HitKind::Slider ? m_sliders[size_t(h.index)].param : h.kind == HitKind::Knob ? m_knobs[size_t(h.index)].param : nullptr;
	if (id != nullptr)
		if (auto *p = m_proc.params().getParameter(id))
			p->setValueNotifyingHost(p->getDefaultValue());
}

void HardwareView::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
	const Hit h = hitAt(e.position);
	if (h.kind == HitKind::Slider || h.kind == HitKind::Knob) {
		const char *id = h.kind == HitKind::Slider ? m_sliders[size_t(h.index)].param : m_knobs[size_t(h.index)].param;
		setParamValue(id, paramValue(id) + w.deltaY * (e.mods.isShiftDown() ? 0.02f : 0.08f));
	} else if (h.kind == HitKind::Switch) {
		const auto &c = m_switches[size_t(h.index)];
		// Up the panel / clockwise for a positive wheel.
		const Element &s = el(c.element);
		const int dir = (w.deltaY > 0 ? 1 : -1) * (s.type == PType::VSwitch ? -1 : 1);
		setDetent(c, juce::jlimit(0, int(c.order.size()) - 1, detentOf(c) + dir));
	}
}

namespace {
constexpr int kLeftCode = -100, kRightCode = -101, kUpCode = -102;

// Computer keyboard -> semitones above the C the octave setting picks.
int semitoneForChar(juce::juce_wchar c)
{
	static const char *row = "awsedftgyhujkolp;'";
	const auto lc = juce::CharacterFunctions::toLowerCase(c);
	for (int i = 0; row[i] != 0; ++i)
		if (row[i] == lc)
			return i;
	return -1;
}
} // namespace

bool HardwareView::keyPressed(const juce::KeyPress &k)
{
	switch (lemus::panel::commonKey(k)) {
	case lemus::panel::Common::Help:
		m_showHelp = !m_showHelp;
		repaint();
		return true;
	case lemus::panel::Common::Escape:
		m_showHelp = false;
		releaseAll();
		repaint();
		return true;
	case lemus::panel::Common::None:
		break;
	}
	const auto c = lemus::panel::usKey(k);
	const auto lc = c;
	if (lc == 'z' || lc == 'x') {
		m_octave = juce::jlimit(-1, 1, m_octave + (lc == 'x' ? 1 : -1));
		repaint();
		return true;
	}
	const int code = k.getKeyCode();
	if (c >= '1' && c <= '7') {
		m_held.keyDown(code, buttonKey(int(c - '1')));
		return true;
	}
	// Space is PLAY in the standalone app; in a plugin it goes on to the DAW's transport.
	if (lemus::panel::spaceIsOurs(k)) {
		m_held.keyDown(code, buttonKey(int(Button::Play)));
		return true;
	}
	if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
		if (m_leverKeys.count(code) == 0) {
			m_leverKeys[code] = code == juce::KeyPress::leftKey ? kLeftCode : kRightCode;
			setBender(code == juce::KeyPress::leftKey ? -1.0f : 1.0f, m_push);
		}
		return true;
	}
	if (code == juce::KeyPress::upKey) {
		if (m_leverKeys.count(code) == 0) {
			m_leverKeys[code] = kUpCode;
			setBender(m_bend, 1.0f);
		}
		return true;
	}
	const int semi = semitoneForChar(c);
	if (semi < 0)
		return false;
	const int key = 7 + semi + 12 * m_octave; // C4 is key 7
	if (key >= 0 && key < Machine::kKeys)
		m_held.keyDown(code, key);
	return true;
}

bool HardwareView::keyStateChanged(bool)
{
	bool handled = m_held.keysChanged();
	for (auto it = m_leverKeys.begin(); it != m_leverKeys.end();) {
		if (juce::KeyPress::isKeyCurrentlyDown(it->first)) {
			++it;
			continue;
		}
		if (it->second == kUpCode)
			setBender(m_bend, 0);
		else
			setBender(0, m_push);
		it = m_leverKeys.erase(it);
		handled = true;
	}
	return handled;
}

void HardwareView::focusLost(FocusChangeType)
{
	// Keys and buttons held on the computer keyboard won't see their key-up now; latched ones stay
	// down. Escape lets go of everything.
	m_held.focusLost();
	if (!m_leverKeys.empty())
		setBender(0, 0);
	m_leverKeys.clear();
}

} // namespace sh101
