#include "HardwareView.h"
#include "Fonts.h"
#include "PanelData.h"

#include <BinaryData.h>

#include <cmath>

using namespace ratpanel;

namespace {
constexpr float kPi = juce::MathConstants<float>::pi;
// Where the panel's mm origin (rear-left corner of the outline) sits in the view, in mm.
constexpr float kOriginX = (HardwareView::kWidth / HardwareView::kPxPerMm - ratpanel::kWidth) / 2;
constexpr float kOriginY = 9.0f;
// The front face is drawn under the top as if seen slightly from the front: foreshortened.
constexpr float kFrontScale = 0.42f;
constexpr float kFrontTop = kShellTop.y1;
constexpr float kFrontBottom = kFrontTop + kFrontHeight * kFrontScale;

const char *const kKnobParams[3] = {"distortion", "filter", "volume"};
const char *const kKnobNames[3] = {"DISTORTION", "FILTER", "VOLUME"};

juce::Colour col(uint32_t argb) { return juce::Colour(argb); }
float rad(float deg) { return deg * kPi / 180.0f; }
juce::Point<float> polar(float cx, float cy, float r, float deg) { return {cx + r * std::sin(rad(deg)), cy - r * std::cos(rad(deg))}; }
juce::Rectangle<float> box(const Rect &r) { return {r.x0, r.y0, r.w(), r.h()}; }

const lemus::panel::Mono &barMono()
{
	static const lemus::panel::Mono m = [](float size, int weight) {
		return fonts::mono(size, weight == 2 ? fonts::Weight::SemiBold : weight == 1 ? fonts::Weight::Medium : fonts::Weight::Regular);
	};
	return m;
}

// The legends' Helvetica Bold, stood in for by Nimbus Sans Bold (fonts/README.md).
juce::Typeface::Ptr legendFace()
{
	static const auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::NimbusSansBold_ttf, size_t(BinaryData::NimbusSansBold_ttfSize));
	return face;
}

// A legend drawn so its letters fill their measured ink box.
void drawLegend(juce::Graphics &g, const Label &l)
{
	juce::GlyphArrangement ga;
	ga.addLineOfText(juce::Font(juce::FontOptions(legendFace()).withHeight(10.0f)), l.text, 0, 0);
	juce::Path p;
	ga.createPath(p);
	const auto ink = p.getBounds();
	p.applyTransform(juce::AffineTransform::fromTargetPoints(ink.getTopLeft(), {l.ink.x0, l.ink.y0}, ink.getTopRight(), {l.ink.x1, l.ink.y0},
	                                                         ink.getBottomLeft(), {l.ink.x0, l.ink.y1}));
	g.fillPath(p);
}

// A brand mark in its place: the user's own picture as it looks, or an older pack's outline in
// `ink`, each fitted to the place with its proportions kept.
void drawMark(juce::Graphics &g, const lemus::Artwork &art, const char *id, juce::Rectangle<float> place, juce::Colour ink)
{
	const auto *m = art.find(id);
	if (m == nullptr)
		return;
	if (m->picture != nullptr) {
		m->picture->drawWithin(g, place);
		return;
	}
	auto p = m->outline;
	p.applyTransform(p.getTransformToScaleToFit(place, true));
	g.setColour(ink);
	g.fillPath(p);
}
} // namespace

HardwareView::HardwareView(RatProcessor &p) : m_proc(p)
{
	setWantsKeyboardFocus(true);
	m_barInput = m_bar.add(150, "INPUT: gain in front of the pedal, to hit it harder or softer (0 dBFS is +4 dBu at its jacks). "
	                            "Drag or scroll; double-click: 0 dB");
	startTimerHz(30);
}

HardwareView::~HardwareView() = default;

float HardwareView::paramValue(const char *id) const
{
	auto *p = m_proc.params().getParameter(id);
	return p ? p->getValue() : 0.0f;
}

void HardwareView::setParamValue(const char *id, float v)
{
	if (auto *p = m_proc.params().getParameter(id))
		p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, v));
}

void HardwareView::toggleFootswitch()
{
	if (auto *p = m_proc.params().getParameter("on")) {
		p->beginChangeGesture();
		p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f);
		p->endChangeGesture();
	}
}

juce::Point<float> HardwareView::toMm(juce::Point<float> px) const
{
	return {px.x / kPxPerMm - kOriginX, px.y / kPxPerMm - kOriginY};
}

int HardwareView::knobAt(juce::Point<float> mm) const
{
	for (int k = 0; k < 3; ++k)
		if (mm.getDistanceFrom({kScales[k].cx, kScales[k].cy}) < kKnob.skirtD / 2 + 1.0f)
			return k;
	return -1;
}

bool HardwareView::onFootswitch(juce::Point<float> mm) const
{
	return mm.getDistanceFrom({kFootswitch.x, kFootswitch.y}) < kFootswitch.bezelD / 2 + 1.0f;
}

void HardwareView::timerCallback()
{
	const std::array<float, 5> now{paramValue("distortion"), paramValue("filter"), paramValue("volume"), paramValue("on"), paramValue("input")};
	if (now != m_shown) {
		m_shown = now;
		repaint();
	}
}

// ====================================================================== the pedal

void HardwareView::drawPedal(juce::Graphics &g)
{
	// The surface it sits on.
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff34363b), kWidth / kPxPerMm / 2, 0, juce::Colour(0xff1f2024), kWidth / kPxPerMm / 2,
	                                       kPanelHeight / kPxPerMm, false));
	g.fillRect(juce::Rectangle<float>(0, 0, kWidth / kPxPerMm, kPanelHeight / kPxPerMm));
	g.addTransform(juce::AffineTransform::translation(kOriginX, kOriginY));

	// Its shadow in front, then the cheeks: plain steel side plates, past the top plate at both ends
	// and down the sides of the front face.
	g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.55f), 0, kFrontBottom - 1, juce::Colours::transparentBlack, 0,
	                                       kFrontBottom + 4, false));
	g.fillRoundedRectangle(juce::Rectangle<float>(-0.5f, kFrontBottom - 1, kWidth + 1, 5), 2.0f);
	// Black like the shell; their worn top edges catch the light.
	for (const Rect &c : {kCheekLeft, kCheekRight}) {
		const juce::Rectangle<float> r(c.x0, c.y0, c.w(), kFrontBottom + 0.6f - c.y0);
		g.setColour(col(kCheek));
		g.fillRoundedRectangle(r, 1.2f);
		g.setColour(juce::Colour(0xff3c3c40));
		g.drawRoundedRectangle(r.reduced(0.15f), 1.1f, 0.3f);
	}

	// The front face, foreshortened, with the front logo's place.
	{
		const juce::Rectangle<float> face(kGapLeft.x0, kFrontTop, kGapRight.x1 - kGapLeft.x0, kFrontBottom - kFrontTop);
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xff17171a), 0, face.getY(), juce::Colour(0xff09090a), 0, face.getBottom(), false));
		g.fillRect(face);
		juce::Graphics::ScopedSaveState s(g);
		g.addTransform(juce::AffineTransform::scale(1.0f, kFrontScale, 0, kFrontTop).translated(0, 0));
		const juce::Rectangle<float> place(kPlaceFront.x0, kFrontTop + kPlaceFront.y0, kPlaceFront.w(), kPlaceFront.h());
		drawMark(g, m_proc.artwork(), "proco_front", place, col(kPrintWhite));
	}

	// The top plate between its black gaps, its rear edge lit, its front edge rolling away.
	g.setColour(col(kPlate));
	g.fillRect(box(kShellTop));
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3a3c), 0, kShellTop.y0, col(kPlate), 0, kShellTop.y0 + 1.6f, false));
	g.fillRect(juce::Rectangle<float>(kShellTop.x0, kShellTop.y0, kShellTop.w(), 1.6f));
	g.setGradientFill(juce::ColourGradient(col(kPlate), 0, kShellTop.y1 - 1.8f, juce::Colour(0xff26262a), 0, kShellTop.y1, false));
	g.fillRect(juce::Rectangle<float>(kShellTop.x0, kShellTop.y1 - 1.8f, kShellTop.w(), 1.8f));
	g.setColour(col(kGap));
	g.fillRect(juce::Rectangle<float>(kGapLeft.x0, kGapLeft.y0, kGapLeft.w(), kFrontBottom - kGapLeft.y0));
	g.fillRect(juce::Rectangle<float>(kGapRight.x0, kGapRight.y0, kGapRight.w(), kFrontBottom - kGapRight.y0));

	// The print: the legend box, its legends, the scales, the RAT box and its white block.
	const auto white = col(kPrintWhite);
	g.setColour(white);
	g.drawRect(box(kLegendBox).expanded(kLegendStroke / 2), kLegendStroke);
	for (float x : kLegendDivs)
		g.drawLine(x, kLegendBox.y0, x, kLegendBox.y1, kLegendStroke);
	for (const auto &l : kLabels)
		drawLegend(g, l);
	for (const auto &s : kScales) {
		juce::Path arc;
		arc.addCentredArc(s.cx, s.cy, s.r, s.r, 0, rad(kArcDeg[0]), rad(kArcDeg[1]), true);
		g.strokePath(arc, juce::PathStrokeType(s.stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
		for (float deg : kTickDeg)
			g.drawLine(juce::Line<float>(polar(s.cx, s.cy, kTickRIn, deg), polar(s.cx, s.cy, s.r + s.stroke / 2, deg)), kTickW);
	}
	g.drawRect(box(kRatBox).expanded(kRatBoxStroke / 2), kRatBoxStroke);
	g.setColour(col(kBlockWhite));
	g.fillRect(box(kWhiteBlock));
	drawMark(g, m_proc.artwork(), "proco", box(kPlaceProco), col(kPlate));
	drawMark(g, m_proc.artwork(), "rat", box(kPlaceRat), col(kPlate));
}

void HardwareView::drawKnob(juce::Graphics &g, int index, float value, bool hot)
{
	const auto &s = kScales[index];
	const float cx = s.cx, cy = s.cy, rs = kKnob.skirtD / 2, rt = kKnob.topD / 2;
	const float deg = kKnob.minDeg + value * (kKnob.maxDeg - kKnob.minDeg);

	g.setColour(juce::Colours::black.withAlpha(0.45f));
	g.fillEllipse(juce::Rectangle<float>(2 * rs + 0.8f, 2 * rs + 0.8f).withCentre({cx + 0.3f, cy + 0.9f}));
	// The knurled skirt: ridges lit from the rear, turning with the knob.
	g.setColour(juce::Colour(0xff121213));
	g.fillEllipse(juce::Rectangle<float>(2 * rs, 2 * rs).withCentre({cx, cy}));
	constexpr int kKnurls = 40;
	for (int i = 0; i < kKnurls; ++i) {
		const float a = 2 * kPi * float(i) / float(kKnurls) + rad(deg);
		const float sx = std::sin(a), sy = -std::cos(a);
		const float light = std::pow(std::max(0.0f, -sy), 1.2f) * 0.8f + 0.2f;
		const auto v = uint8_t(juce::jlimit(0.0f, 255.0f, 20 + 70 * light));
		g.setColour(juce::Colour(v, v, uint8_t(v + 1)));
		g.drawLine(cx + sx * rt, cy + sy * rt, cx + sx * rs, cy + sy * rs, 0.35f);
	}
	// The flat top, its edge catching the light, and the white pointer line.
	g.setColour(col(kKnobTop));
	g.fillEllipse(juce::Rectangle<float>(2 * rt, 2 * rt).withCentre({cx, cy}));
	{
		juce::Path rim;
		rim.addCentredArc(cx, cy, rt - 0.2f, rt - 0.2f, 0, rad(-70), rad(70), true);
		g.setColour(juce::Colour(0xff4a4a4d));
		g.strokePath(rim, juce::PathStrokeType(0.3f));
	}
	g.setColour(col(kPrintWhite).brighter(0.2f));
	g.drawLine(juce::Line<float>(polar(cx, cy, kKnob.pointerR0, deg), polar(cx, cy, kKnob.pointerR1, deg)), kKnob.pointerW);
	if (hot) {
		g.setColour(juce::Colour(0x30ffffff));
		g.drawEllipse(juce::Rectangle<float>(2 * rs + 1.2f, 2 * rs + 1.2f).withCentre({cx, cy}), 0.25f);
	}
}

void HardwareView::drawFootswitch(juce::Graphics &g)
{
	const float cx = kFootswitch.x, cy = kFootswitch.y, rb = kFootswitch.bezelD / 2, rp = kFootswitch.plungerD / 2;
	g.setColour(juce::Colours::black.withAlpha(0.5f));
	g.fillEllipse(juce::Rectangle<float>(2 * rb + 1, 2 * rb + 1).withCentre({cx + 0.3f, cy + 0.9f}));
	// The chrome bezel (washer and nut), then the dark ring of the bushing round the plunger.
	g.setGradientFill(juce::ColourGradient(col(kChrome).brighter(0.6f), cx - rb, cy - rb, col(kChrome).darker(0.4f), cx + rb, cy + rb, false));
	g.fillEllipse(juce::Rectangle<float>(2 * rb, 2 * rb).withCentre({cx, cy}));
	g.setColour(col(kChrome).darker(0.2f));
	g.drawEllipse(juce::Rectangle<float>(2 * rb - 2.2f, 2 * rb - 2.2f).withCentre({cx, cy}), 0.35f);
	g.setColour(juce::Colour(0xff151413));
	g.fillEllipse(juce::Rectangle<float>(2 * rp + 1.4f, 2 * rp + 1.4f).withCentre({cx, cy}));
	// The plunger's turned top. The 1984 RAT has no LED: on or bypassed, it looks the same (the bar
	// says which).
	const auto top = col(kPlunger).brighter(0.45f);
	g.setGradientFill(juce::ColourGradient(top.brighter(0.2f), cx - rp, cy - rp, top.darker(0.35f), cx + rp, cy + rp, false));
	g.fillEllipse(juce::Rectangle<float>(2 * rp, 2 * rp).withCentre({cx, cy}));
	g.setColour(top.darker(0.15f));
	for (float r = rp * 0.25f; r < rp; r += rp * 0.18f)
		g.drawEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre({cx, cy}), 0.12f);
	if (m_hoverFoot) {
		g.setColour(juce::Colour(0x30ffffff));
		g.drawEllipse(juce::Rectangle<float>(2 * rb + 1.2f, 2 * rb + 1.2f).withCentre({cx, cy}), 0.25f);
	}
}

// ====================================================================== bar

juce::String HardwareView::status() const
{
	if (m_dragKnob >= 0)
		if (auto *p = m_proc.params().getParameter(kKnobParams[m_dragKnob]))
			return juce::String(kKnobNames[m_dragKnob]) + "  " + p->getCurrentValueAsText();
	if (m_proc.engaged())
		return "ON";
	return "BYPASSED: the footswitch is off (true bypass)";
}

void HardwareView::drawBar(juce::Graphics &g)
{
	namespace pf = lemus::panel;
	pf::drawBarBackground(g, m_bar);
	{
		// INPUT: a pill with its value and a short track.
		const auto r = m_bar[m_barInput];
		pf::drawPill(g, r, {}, false, m_hoverBar == m_barInput || m_dragInput, barMono());
		const auto *p = m_proc.params().getParameter("input");
		g.setFont(barMono()(12.0f, 1));
		g.setColour(pf::col::ink);
		g.drawText("INPUT " + (p ? p->getCurrentValueAsText() : juce::String()), r.withTrimmedLeft(10).withTrimmedRight(44),
		           juce::Justification::centredLeft);
		const juce::Rectangle<float> track(r.getRight() - 44, r.getCentreY() - 1.5f, 34, 3);
		g.setColour(pf::col::border);
		g.fillRoundedRectangle(track, 1.5f);
		const float v = paramValue("input");
		g.setColour(pf::col::accent);
		g.fillRoundedRectangle(track.withWidth(track.getWidth() * v), 1.5f);
		g.fillEllipse(track.getX() + track.getWidth() * v - 3.5f, track.getCentreY() - 3.5f, 7, 7);
	}
	pf::drawStatus(g, m_bar.status(), status(), barMono());
	pf::drawPill(g, m_bar.help(), "?", m_help, m_hoverBar == pf::Bar::kHelp, barMono());
	if (m_hoverBar != -1 && !m_dragInput)
		pf::drawBarTip(g, m_bar, m_hoverBar, getLocalBounds().toFloat(), barMono());
}

void HardwareView::paint(juce::Graphics &g)
{
	{
		juce::Graphics::ScopedSaveState s(g);
		g.addTransform(juce::AffineTransform::scale(kPxPerMm));
		drawPedal(g);
		for (int k = 0; k < 3; ++k)
			drawKnob(g, k, paramValue(kKnobParams[k]), k == m_hoverKnob || k == m_dragKnob);
		drawFootswitch(g);
	}
	drawBar(g);
	const auto panel = getLocalBounds().toFloat().withTrimmedBottom(kBarHeight);
	if (m_help) {
		lemus::panel::drawHelp(g, panel, "RAT",
		                       {"Knobs        DISTORTION, FILTER (clockwise is darker), VOLUME",
		                        "Footswitch   click: on, or true bypass (the bar says which: the pedal has no LED)",
		                        "INPUT        gain in front of the pedal, to hit it harder; 0 dBFS is +4 dBu at its jacks (a hot pickup)"},
		                       barMono());
		return;
	}
	if (m_hoverKnob >= 0 && m_dragKnob < 0)
		if (auto *p = m_proc.params().getParameter(kKnobParams[m_hoverKnob])) {
			const auto &s = kScales[m_hoverKnob];
			lemus::panel::drawTooltip(g, juce::String(kKnobNames[m_hoverKnob]) + "  " + p->getCurrentValueAsText(),
			                          {(s.cx + kOriginX) * kPxPerMm, (s.cy + kOriginY + kKnob.skirtD / 2 + 4.5f) * kPxPerMm}, panel, barMono());
		}
	if (m_hoverFoot)
		lemus::panel::drawTooltip(g, m_proc.engaged() ? "ON (click to bypass)" : "BYPASSED (click to engage)",
		                          {(kFootswitch.x + kOriginX) * kPxPerMm, (kFootswitch.y + kOriginY + kFootswitch.bezelD / 2 + 2) * kPxPerMm}, panel,
		                          barMono());
}

// ====================================================================== interaction

void HardwareView::mouseMove(const juce::MouseEvent &e)
{
	const bool onPanel = e.position.y < float(kPanelHeight);
	const auto mm = toMm(e.position);
	m_hoverKnob = onPanel ? knobAt(mm) : -1;
	m_hoverFoot = onPanel && m_hoverKnob < 0 && onFootswitch(mm);
	m_hoverBar = onPanel ? -1 : m_bar.hit(e.position);
	repaint();
}

void HardwareView::mouseExit(const juce::MouseEvent &)
{
	m_hoverKnob = m_hoverBar = -1;
	m_hoverFoot = false;
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
		if (hit == lemus::panel::Bar::kHelp) {
			m_help = true;
		} else if (hit == m_barInput) {
			m_dragInput = true;
			m_dragStartX = e.position.x;
			m_dragStartValue = paramValue("input");
			if (auto *p = m_proc.params().getParameter("input"))
				p->beginChangeGesture();
		}
		repaint();
		return;
	}
	const auto mm = toMm(e.position);
	if (const int k = knobAt(mm); k >= 0) {
		m_dragKnob = k;
		m_dragStartY = e.position.y;
		m_dragStartValue = paramValue(kKnobParams[k]);
		if (auto *p = m_proc.params().getParameter(kKnobParams[k]))
			p->beginChangeGesture();
	} else if (onFootswitch(mm)) {
		toggleFootswitch();
	}
	repaint();
}

void HardwareView::mouseDrag(const juce::MouseEvent &e)
{
	const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;
	if (m_dragKnob >= 0) {
		setParamValue(kKnobParams[m_dragKnob], m_dragStartValue + (m_dragStartY - e.position.y) / 260.0f * fine);
		repaint();
	} else if (m_dragInput) {
		setParamValue("input", m_dragStartValue + (e.position.x - m_dragStartX) / 220.0f * fine);
		repaint();
	}
}

void HardwareView::mouseUp(const juce::MouseEvent &)
{
	if (m_dragKnob >= 0)
		if (auto *p = m_proc.params().getParameter(kKnobParams[m_dragKnob]))
			p->endChangeGesture();
	if (m_dragInput)
		if (auto *p = m_proc.params().getParameter("input"))
			p->endChangeGesture();
	m_dragKnob = -1;
	m_dragInput = false;
	repaint();
}

void HardwareView::mouseDoubleClick(const juce::MouseEvent &e)
{
	const char *id = nullptr;
	if (e.position.y >= float(kPanelHeight))
		id = m_bar.hit(e.position) == m_barInput ? "input" : nullptr;
	else if (const int k = knobAt(toMm(e.position)); k >= 0)
		id = kKnobParams[k];
	if (id == nullptr)
		return;
	if (auto *p = m_proc.params().getParameter(id)) {
		p->beginChangeGesture();
		p->setValueNotifyingHost(p->getDefaultValue());
		p->endChangeGesture();
	}
	repaint();
}

void HardwareView::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
	const char *id = nullptr;
	if (e.position.y >= float(kPanelHeight))
		id = m_bar.hit(e.position) == m_barInput ? "input" : nullptr;
	else if (const int k = knobAt(toMm(e.position)); k >= 0)
		id = kKnobParams[k];
	if (id == nullptr)
		return;
	if (auto *p = m_proc.params().getParameter(id)) {
		p->beginChangeGesture();
		p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->getValue() + (w.deltaY + w.deltaX) * (e.mods.isShiftDown() ? 0.05f : 0.25f)));
		p->endChangeGesture();
	}
	repaint();
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
		repaint();
		return true;
	case lemus::panel::Common::None: break;
	}
	// Space works the footswitch in the standalone app; in a plugin it goes on to the host's transport.
	if (lemus::panel::spaceIsOurs(k)) {
		toggleFootswitch();
		return true;
	}
	return false;
}
