// SERVICE on the plugin bar: the TB-303 with its top panel lifted off. The main board sits where
// the panel was, the switch board in front of it on its ribbons, as a technician folds it out.
// Every part is where the service notes' board drawing puts it (p.6, "view from component side",
// digitised into docs/boards.json -> BoardData.h) with its designator printed where the board
// prints it; nothing on the boards is invented except the copper under the mask, which is drawn
// between neighbouring pads rather than copied. The knobs stay on their pots, as they do when the
// panel comes off; the keys' switches and LEDs sit under the keys. Colours follow photos of real
// boards: green mask, white legend, cream resistors, green film caps, white trimmers for TM1-TM4
// and blue ones for TM5 and TM6.
//
// On the right: the six trimmers' controls. Picking one lights it up on the board and hooks the
// scope, counter and meter onto its check point as the notes' adjustment (p.6-7) does; each
// control shows where its travel is in spec. The values are the trims worked out (core/trims.h),
// the instruments' readings are measured on the running model.

#include "BoardData.h"
#include "HardwareView.h"
#include "Fonts.h"
#include "PanelData.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>

using namespace panel;
namespace tr = tb303::trims;

namespace {
constexpr float kPi = juce::MathConstants<float>::pi;

// Board mm (the panel's) to view px: 2.96 px/mm, so both boards fit above the bar.
constexpr float kPx = 2.96f, kOx = -0.8f, kOy = 8.0f;
constexpr float kSwitchDy = 48.6f; // the keys' sub-panel -> the switch board, laid out in front
juce::Point<float> P(float x, float y) { return {kOx + x * kPx, kOy + y * kPx}; }
juce::Point<float> P(juce::Point<float> mm) { return P(mm.x, mm.y); }
float L(float mm) { return mm * kPx; }
juce::Rectangle<float> R(float x0, float y0, float x1, float y1) { return {P(x0, y0), P(x1, y1)}; }
const juce::AffineTransform kMmToPx = juce::AffineTransform::scale(kPx).translated(kOx, kOy);

juce::Point<float> centreOf(const Element &e) { return {0.5f * (e.x0 + e.x1), 0.5f * (e.y0 + e.y1)}; }

uint32_t hashOf(const char *s)
{
	uint32_t h = 2166136261u;
	for (; *s; ++s)
		h = (h ^ uint8_t(*s)) * 16777619u;
	return h;
}

// ------------------------------------------------------------------ the parts (BoardData.h)
using board::Kind;
using Part = board::Part;

const Part *findPart(const char *ref, bool switchBoard = false)
{
	for (const auto &p : board::parts)
		if (p.switchBoard == switchBoard && std::strcmp(p.ref, ref) == 0)
			return &p;
	return nullptr;
}
juce::Point<float> padA(const Part &p) { return {p.ax, p.ay}; }
juce::Point<float> padB(const Part &p) { return {p.bx, p.by}; }
juce::Point<float> centreMm(const Part &p) { return {p.cx, p.cy}; }
bool has(float v) { return !std::isnan(v); }

// The six trimmers, TM1-TM6, and what the notes set each one with.
struct TrimDef {
	const char *ref, *silk, *plain;
	int channel; // TB303Processor::ScopeChannel
	float msPerDiv;
	const char *probe; // where the scope's probe goes: a test point, or S1 for the VCO
	const char *hint;  // the notes' procedure, plainly
	bool blue;
};
const TrimDef kTrims[6] = {
	{"TM1", "TEMPO", "tempo range", TB303Processor::ScopeTp1, 2, "TP1", "TEMPO fully clockwise: a clock period of 8 ms (7 to 9) at TP1", false},
	{"TM2", "INT", "CPU interrupt", TB303Processor::ScopeTp2, 0.5f, "TP2", "The CPU's interrupt at TP2: a pulse every 1.8 ms (1.6 to 2.0)", false},
	{"TM3", "FREQ", "filter cutoff", TB303Processor::ScopeTp6, 2, "TP6",
	 "CUT OFF centre, RESONANCE full, ENV MOD, DECAY, ACCENT down; hold a note: TP6 rings every 2 ms (1.5 to 2.5)", false},
	{"TM4", "TUNE", "tuning", TB303Processor::ScopeS1, 2, "S1", "TUNING centre, hold A: the counter reads 110 Hz", false},
	{"TM5", "WIDTH", "octave width", TB303Processor::ScopeS1, 5, "S1", "Play C, then C': C' at exactly twice the frequency (2.000 : 1)", true},
	{"TM6", "", "CV out level", TB303Processor::ScopeS1, 5, "S1", "Hold C, then C': CV OUT rises by 1.000 V (3 mV either way)", true},
};
constexpr float kTrimmerDia = 9.6f;

juce::Point<float> trimmerMm(int i)
{
	static const auto at = [] {
		std::array<juce::Point<float>, 6> a{};
		const juce::Point<float> fallback[6] = {{60, 45}, {75.5f, 64}, {152, 53}, {176.5f, 67.5f}, {202, 79}, {282, 91}};
		for (int k = 0; k < 6; ++k) {
			const Part *p = findPart(kTrims[k].ref);
			a[size_t(k)] = p ? centreMm(*p) : fallback[k];
		}
		return a;
	}();
	return at[size_t(i)];
}

// A check point on the main board: TP<n>, or S1 for the VCO (the WAVEFORM switch carries it).
juce::Point<float> checkPointMm(const char *ref)
{
	if (const Part *p = findPart(ref))
		return std::strcmp(ref, "S1") == 0 ? juce::Point<float>(p->cx, p->cy) : centreMm(*p);
	return {35, 5};
}

// The CV OUT jack: the small jack nearest where the rear panel has it.
juce::Point<float> cvJackMm()
{
	juce::Point<float> best(219.75f, 4), want(219.75f, 4);
	float d = 1e9f;
	for (const auto &p : board::parts)
		if (!p.switchBoard && p.kind == Kind::J && centreMm(p).getDistanceFrom(want) < d)
			d = centreMm(p).getDistanceFrom(want), best = {p.cx, p.ay + 3.0f};
	return best;
}

// ------------------------------------------------------------------ the right-hand side (view px)
constexpr float kIx = 892, kIw = 1188 - 892;
const juce::Rectangle<float> kScopeCase(kIx, 8, kIw, 182), kScopeScreen(kIx + 12, 32, kIw - 24, 116);
const juce::Rectangle<float> kCounterCase(kIx, 198, 144, 56), kMeterCase(kIx + kIw - 144, 198, 144, 56);
const juce::Rectangle<float> kTrimPanel(kIx, 262, kIw, 298);
constexpr float kRowH = 45;

const juce::Colour kSilk(0xffe6ece6), kOrange(0xffff8c1a), kProbe(0xfff5d742), kMeterProbe(0xffe8473a);
const juce::Colour kInSpec(0xff46c37b);

juce::Font sans(float px, bool bold = false) { return juce::Font(juce::FontOptions("Helvetica Neue", px, bold ? juce::Font::bold : juce::Font::plain)); }

// The board's legend: a plain sans, slightly condensed, centred on (x, y) mm and turned by rot.
void silk(juce::Graphics &g, const juce::String &t, float x, float y, float capMm, int rot = 0, juce::Colour c = kSilk)
{
	juce::Font f(juce::FontOptions("Helvetica Neue", L(capMm) / 0.72f, juce::Font::bold));
	f.setHorizontalScale(0.86f);
	juce::GlyphArrangement ga;
	ga.addLineOfText(f, t, 0, 0);
	juce::Path p;
	ga.createPath(p);
	const auto b = p.getBounds();
	const auto at = P(x, y);
	p.applyTransform(juce::AffineTransform::translation(-b.getCentreX(), -b.getCentreY())
	                     .rotated(juce::degreesToRadians(-float(rot)))
	                     .translated(at));
	g.setColour(c);
	g.fillPath(p);
}

// A fixed sequence, the same on every platform.
struct Rng {
	uint32_t s;
	float next()
	{
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return float(s & 0xffffff) / 16777216.0f;
	}
	float range(float a, float b) { return a + (b - a) * next(); }
};

// ------------------------------------------------------------------ part drawing (board mm)
const juce::Colour kLead(0xffc9cdd2), kCopper(0xffb5763e), kMask(0xff0f6247), kMaskDark(0xff0b5039), kTrace(0xff1b8c61);

// A shape with a soft shadow under it toward the front-right (light from the rear-left).
void shadowed(juce::Graphics &g, const juce::Path &p, float lift = 1.0f)
{
	g.setColour(juce::Colours::black.withAlpha(0.28f));
	g.fillPath(p, juce::AffineTransform::translation(0.45f * lift + 0.3f, 0.9f * lift + 0.4f));
	g.setColour(juce::Colours::black.withAlpha(0.18f));
	g.fillPath(p, juce::AffineTransform::translation(0.9f * lift + 0.5f, 1.8f * lift + 0.6f));
}

void lead(juce::Graphics &g, juce::Point<float> a, juce::Point<float> b, float w = 0.5f)
{
	g.setColour(juce::Colour(0xff8d9298));
	g.drawLine(juce::Line<float>(P(a), P(b)), L(w) + 0.6f);
	g.setColour(kLead);
	g.drawLine(juce::Line<float>(P(a), P(b)), L(w));
}

void pad(juce::Graphics &g, juce::Point<float> mm, float dia = 1.9f)
{
	const auto c = P(mm);
	const float r = L(dia / 2);
	g.setColour(kCopper.darker(0.25f));
	g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
	g.setGradientFill(juce::ColourGradient(kCopper.brighter(0.35f), c.x - r, c.y - r, kCopper.darker(0.1f), c.x + r, c.y + r, false));
	g.fillEllipse(c.x - r * 0.88f, c.y - r * 0.88f, r * 1.76f, r * 1.76f);
	// The lead's end and its solder.
	const float s = L(0.42f);
	g.setColour(juce::Colour(0xffe2e5e8));
	g.fillEllipse(c.x - s, c.y - s, 2 * s, 2 * s);
}

// A cylinder's shading across its width: lit edge, body, shaded edge.
juce::ColourGradient across(juce::Colour body, juce::Point<float> a, juce::Point<float> b, float halfW)
{
	const auto d = (b - a) / std::max(1e-3f, a.getDistanceFrom(b));
	const juce::Point<float> n(-d.y, d.x);
	const auto m = (a + b) * 0.5f;
	juce::ColourGradient gr(body.brighter(0.45f), m - n * halfW, body.darker(0.45f), m + n * halfW, false);
	gr.addColour(0.35, body.brighter(0.12f));
	gr.addColour(0.7, body);
	return gr;
}

// Resistor colour codes (E12, gold tolerance), chosen per designator: the notes' parts list isn't
// read in, so the bands are a real code but not that resistor's own.
const juce::uint32 kBand[10] = {0xff1b1b1b, 0xff7a4a22, 0xffd8352a, 0xffee8a22, 0xfff0d23a, 0xff3e9a42, 0xff3159c4, 0xff8a4fc0, 0xff8f8f8f, 0xfff4f4f4};
void bandsFor(const char *ref, int out[4])
{
	static const int e12[12][2] = {{1, 0}, {1, 2}, {1, 5}, {1, 8}, {2, 2}, {2, 7}, {3, 3}, {3, 9}, {4, 7}, {5, 6}, {6, 8}, {8, 2}};
	const uint32_t h = hashOf(ref);
	const auto &v = e12[h % 12];
	out[0] = v[0], out[1] = v[1], out[2] = int(1 + (h / 12) % 5); // 100 R to 1 M
	out[3] = -1;
}

void drawResistor(juce::Graphics &g, const Part &p)
{
	const auto a = padA(p), b = padB(p);
	const float d = a.getDistanceFrom(b);
	int bands[4];
	bandsFor(p.ref, bands);
	const juce::Colour body(0xffe9d5ae);
	if (d < 6.2f) {
		// Standing: the body upright over one pad, its top lead bent over to the other.
		const auto top = a;
		const auto c = P(top);
		const float r = L(1.25f);
		juce::Path loop;
		const auto pa = P(top + (b - a) * 0.15f), pb = P(b);
		const auto mid = (pa + pb) * 0.5f + juce::Point<float>(-(pb - pa).y, (pb - pa).x) * 0.25f;
		loop.startNewSubPath(pa);
		loop.quadraticTo(mid, pb);
		g.setColour(juce::Colours::black.withAlpha(0.3f));
		g.strokePath(loop, juce::PathStrokeType(L(0.5f) + 0.6f), juce::AffineTransform::translation(0.8f, 1.6f));
		g.setColour(juce::Colour(0xff8d9298));
		g.strokePath(loop, juce::PathStrokeType(L(0.5f) + 0.6f));
		g.setColour(kLead);
		g.strokePath(loop, juce::PathStrokeType(L(0.5f)));
		juce::Path disc;
		disc.addEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
		shadowed(g, disc, 2.5f);
		g.setGradientFill(juce::ColourGradient(body.brighter(0.3f), c.x - r * 0.5f, c.y - r * 0.6f, body.darker(0.35f), c.x + r, c.y + r, true));
		g.fillPath(disc);
		g.setColour(juce::Colour(kBand[bands[0]]).withAlpha(0.9f));
		g.drawEllipse(c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f, L(0.28f));
		return;
	}
	// Lying flat: a 6.3 mm body between bent leads.
	const auto dir = (b - a) / d;
	const float len = 6.3f, hw = 1.2f;
	const auto m = (a + b) * 0.5f;
	const auto e0 = m - dir * (len / 2), e1 = m + dir * (len / 2);
	lead(g, a, e0);
	lead(g, e1, b);
	const float ang = std::atan2(dir.y, dir.x);
	const auto t = juce::AffineTransform::rotation(ang).translated(P(m));
	juce::Path shape;
	shape.addRoundedRectangle(-L(len / 2), -L(hw), L(len), L(2 * hw), L(hw));
	// The end caps bulge a little.
	shape.addEllipse(-L(len / 2), -L(hw + 0.12f), L(1.9f), L(2 * hw + 0.24f));
	shape.addEllipse(L(len / 2 - 1.9f), -L(hw + 0.12f), L(1.9f), L(2 * hw + 0.24f));
	shape.applyTransform(t);
	shadowed(g, shape, 1.2f);
	g.setGradientFill(across(body, P(e0), P(e1), L(hw + 0.1f)));
	g.fillPath(shape);
	const float xs[4] = {-2.15f, -1.35f, -0.55f, 1.6f};
	for (int i = 0; i < 4; ++i) {
		juce::Path band;
		band.addRectangle(L(xs[i]), -L(hw - 0.05f), L(0.5f), L(2 * hw - 0.1f));
		band.applyTransform(t);
		const juce::Colour bc = i == 3 ? juce::Colour(0xffc9a24a) : juce::Colour(kBand[bands[i]]);
		g.setGradientFill(across(bc, P(e0), P(e1), L(hw)));
		g.fillPath(band);
	}
}

void drawDiode(juce::Graphics &g, const Part &p)
{
	auto k = padA(p), a = padB(p); // cathode first
	const float d = k.getDistanceFrom(a);
	const bool zener = p.kind == Kind::ZD;
	const juce::Colour glass = zener ? juce::Colour(0xffd96a3a) : juce::Colour(0xffcf4b2a);
	if (d < 5.2f) {
		// Standing, as the 1S2473s are where they're packed tight.
		const auto c = P(a);
		const float r = L(0.95f);
		lead(g, a + (k - a) * 0.2f, k);
		juce::Path disc;
		disc.addEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
		shadowed(g, disc, 2.0f);
		g.setGradientFill(juce::ColourGradient(glass.brighter(0.6f), c.x - r * 0.5f, c.y - r * 0.5f, glass.darker(0.4f), c.x + r, c.y + r, true));
		g.fillPath(disc);
		g.setColour(juce::Colour(0xff151515));
		g.drawEllipse(c.x - r * 0.75f, c.y - r * 0.75f, r * 1.5f, r * 1.5f, L(0.25f));
		return;
	}
	const auto dir = (a - k) / d;
	const float len = 3.6f, hw = 0.85f;
	const auto m = (a + k) * 0.5f;
	lead(g, k, m - dir * (len / 2), 0.42f);
	lead(g, m + dir * (len / 2), a, 0.42f);
	const auto t = juce::AffineTransform::rotation(std::atan2(dir.y, dir.x)).translated(P(m));
	juce::Path body;
	body.addRoundedRectangle(-L(len / 2), -L(hw), L(len), L(2 * hw), L(hw * 0.8f));
	body.applyTransform(t);
	shadowed(g, body, 0.9f);
	g.setGradientFill(across(glass, P(m - dir), P(m + dir), L(hw)));
	g.fillPath(body);
	// The cathode band, at the cathode's end.
	juce::Path band;
	band.addRectangle(-L(len / 2 - 0.35f), -L(hw), L(0.55f), L(2 * hw));
	band.applyTransform(t);
	g.setColour(juce::Colour(0xff18181a));
	g.fillPath(band);
	// The glass's glint.
	juce::Path glint;
	glint.addRoundedRectangle(-L(0.9f), -L(hw * 0.65f), L(1.9f), L(0.35f), L(0.15f));
	glint.applyTransform(t);
	g.setColour(juce::Colours::white.withAlpha(0.45f));
	g.fillPath(glint);
}

void drawFilmCap(juce::Graphics &g, const Part &p)
{
	const auto a = padA(p), b = padB(p);
	const float d = std::max(2.5f, a.getDistanceFrom(b));
	const auto dir = (b - a) / d;
	const auto m = (a + b) * 0.5f;
	const auto t = juce::AffineTransform::rotation(std::atan2(dir.y, dir.x)).translated(P(m));
	const uint32_t h = hashOf(p.ref);
	if (h % 4 == 0) {
		// A ceramic disc, edge on.
		juce::Path disc;
		disc.addRoundedRectangle(-L(2.3f), -L(1.2f), L(4.6f), L(2.4f), L(1.1f));
		disc.applyTransform(t);
		shadowed(g, disc, 1.2f);
		const juce::Colour c(0xffc98a3e);
		g.setGradientFill(across(c, P(m - dir), P(m + dir), L(1.2f)));
		g.fillPath(disc);
		return;
	}
	// The green dipped film caps: a glossy lozenge.
	const float len = std::clamp(d + 1.6f, 5.0f, 7.4f), hw = 1.75f;
	juce::Path body;
	body.addRoundedRectangle(-L(len / 2), -L(hw), L(len), L(2 * hw), L(hw * 0.95f));
	body.applyTransform(t);
	shadowed(g, body, 1.6f);
	const juce::Colour green(0xff2b8152);
	g.setGradientFill(across(green, P(m - dir), P(m + dir), L(hw)));
	g.fillPath(body);
	juce::Path glint;
	glint.addRoundedRectangle(-L(len / 2 - 1.1f), -L(hw * 0.7f), L(len - 2.8f), L(0.55f), L(0.25f));
	glint.applyTransform(t);
	g.setColour(juce::Colours::white.withAlpha(0.32f));
	g.fillPath(glint);
}

void drawElectrolytic(juce::Graphics &g, const Part &p)
{
	if (p.dir == 'L') {
		// On its side: the sleeve's length, its minus stripe along one side, the seal at one end.
		const auto r = R(std::min(p.ax, p.bx), std::min(p.ay, p.by), std::max(p.ax, p.bx), std::max(p.ay, p.by));
		const bool across = r.getWidth() > r.getHeight();
		juce::Path can;
		can.addRoundedRectangle(r, L(0.9f));
		shadowed(g, can, 2.5f);
		const juce::Colour sl(0xff1c1e23);
		const auto c = r.getCentre();
		const auto n = across ? juce::Point<float>(0, r.getHeight() / 2) : juce::Point<float>(r.getWidth() / 2, 0);
		g.setGradientFill(across ? juce::ColourGradient(sl.brighter(0.6f), c - n, sl.darker(0.4f), c + n, false)
		                         : juce::ColourGradient(sl.brighter(0.6f), c - n, sl.darker(0.4f), c + n, false));
		g.fillPath(can);
		g.setColour(juce::Colour(0xffb9c0c9).withAlpha(0.8f));
		if (across)
			g.fillRect(r.getX() + L(1), r.getBottom() - L(1.1f), r.getWidth() - L(2), L(0.6f));
		else
			g.fillRect(r.getRight() - L(1.1f), r.getY() + L(1), L(0.6f), r.getHeight() - L(2));
		g.setColour(juce::Colour(0xff8f959d));
		if (across)
			g.fillRect(r.getX(), r.getY() + L(0.4f), L(0.7f), r.getHeight() - L(0.8f));
		else
			g.fillRect(r.getX() + L(0.4f), r.getY(), r.getWidth() - L(0.8f), L(0.7f));
		return;
	}
	const auto m = has(p.cx) ? centreMm(p) : (padA(p) + padB(p)) * 0.5f;
	const float dia = p.size > 2 ? std::min(p.size, 13.0f) : 5.0f;
	const auto c = P(m);
	const float r = L(dia / 2);
	juce::Path sleeve;
	sleeve.addEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
	shadowed(g, sleeve, 3.0f);
	const bool blue = dia > 9.5f && hashOf(p.ref) % 2 == 0;
	const juce::Colour sl = blue ? juce::Colour(0xff23407e) : juce::Colour(0xff1c1e23);
	g.setGradientFill(juce::ColourGradient(sl.brighter(0.5f), c.x - r, c.y - r, sl.darker(0.3f), c.x + r, c.y + r, false));
	g.fillPath(sleeve);
	// The sleeve's minus stripe, on the side away from +.
	float stripeAt = 0;
	if (p.dir == 'a' || p.dir == 'b') {
		const auto plus = p.dir == 'a' ? padA(p) : padB(p);
		const auto away = m - plus;
		stripeAt = std::atan2(away.x, -away.y);
	}
	juce::Path stripe;
	stripe.addCentredArc(c.x, c.y, r * 0.93f, r * 0.93f, 0, stripeAt - 0.55f, stripeAt + 0.55f, true);
	g.setColour(juce::Colour(0xffb9c0c9).withAlpha(0.85f));
	g.strokePath(stripe, juce::PathStrokeType(r * 0.12f));
	// The aluminium top, its vent pressed in.
	const float rt = r * 0.8f;
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe3e6ea), c.x - rt * 0.6f, c.y - rt * 0.6f, juce::Colour(0xff8f959d), c.x + rt, c.y + rt, true));
	g.fillEllipse(c.x - rt, c.y - rt, 2 * rt, 2 * rt);
	g.setColour(juce::Colour(0xff6f757d));
	const float v = rt * 0.5f;
	g.drawLine(c.x, c.y - v, c.x, c.y + v, std::max(0.6f, rt * 0.08f));
	g.drawLine(c.x, c.y, c.x + v * 0.85f, c.y - v * 0.6f, std::max(0.6f, rt * 0.08f));
	g.drawLine(c.x, c.y, c.x + v * 0.85f, c.y + v * 0.6f, std::max(0.6f, rt * 0.08f));
}

void drawTransistor(juce::Graphics &g, const Part &p)
{
	const auto m = has(p.cx) ? centreMm(p) : (padA(p) + padB(p)) * 0.5f;
	if (p.dir == 'B') {
		const auto r = R(p.ax, p.ay, p.bx, p.by);
		juce::Path body;
		body.addRoundedRectangle(r, L(0.4f));
		shadowed(g, body, 1.5f);
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xff35373c), r.getX(), r.getY(), juce::Colour(0xff17181b), r.getRight(), r.getBottom(), false));
		g.fillPath(body);
		return;
	}
	// TO-92 from above: a D, its flat to dir.
	float rot = 0; // flat facing north
	switch (p.dir) {
	case 'E': rot = kPi / 2; break;
	case 'S': rot = kPi; break;
	case 'W': rot = -kPi / 2; break;
	default: break;
	}
	const float r = L(2.35f);
	juce::Path d;
	d.startNewSubPath(-r, -L(0.55f));
	d.lineTo(r, -L(0.55f));
	d.addCentredArc(0, -L(0.55f), r, L(3.1f), 0, kPi / 2, kPi * 1.5f);
	d.closeSubPath();
	d.applyTransform(juce::AffineTransform::rotation(rot).translated(P(m)));
	shadowed(g, d, 2.2f);
	const auto c = P(m);
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4c52), c.x - r, c.y - r, juce::Colour(0xff141518), c.x + r, c.y + r, false));
	g.fillPath(d);
	// The flat face catches the light.
	juce::Path flat;
	flat.addRectangle(-r * 0.92f, -L(0.55f), r * 1.84f, L(0.45f));
	flat.applyTransform(juce::AffineTransform::rotation(rot).translated(P(m)));
	g.setColour(juce::Colour(0xff5d6067));
	g.fillPath(flat);
}

void drawIc(juce::Graphics &g, const Part &p)
{
	juce::Rectangle<float> box(juce::Point<float>(std::min(p.ax, p.bx), std::min(p.ay, p.by)), juce::Point<float>(std::max(p.ax, p.bx), std::max(p.ay, p.by)));
	const bool vertical = box.getHeight() > box.getWidth();
	const int pins = p.size > 3 ? int(p.size) : 2 * juce::roundToInt((vertical ? box.getHeight() : box.getWidth()) / 2.54f);
	const int perSide = std::max(1, pins / 2);
	// The package: its long sides carry the pins at 2.54 mm.
	const float len = perSide * 2.54f, wid = perSide > 15 ? 13.8f : 6.3f;
	const auto c = box.getCentre();
	const juce::Rectangle<float> body = vertical ? juce::Rectangle<float>(wid, len).withCentre(c) : juce::Rectangle<float>(len, wid).withCentre(c);
	for (int i = 0; i < perSide; ++i) {
		const float o = -len / 2 + (i + 0.5f) * 2.54f;
		for (int s : {-1, 1}) {
			const auto pin = vertical ? juce::Point<float>(c.x + s * (wid / 2 + 0.7f), c.y + o) : juce::Point<float>(c.x + o, c.y + s * (wid / 2 + 0.7f));
			pad(g, pin, 1.7f);
			const float e0 = s * wid / 2, e1 = s * (wid / 2 + 0.9f);
			const auto r = vertical ? R(c.x + std::min(e0, e1), pin.y - 0.32f, c.x + std::max(e0, e1), pin.y + 0.32f)
			                        : R(pin.x - 0.32f, c.y + std::min(e0, e1), pin.x + 0.32f, c.y + std::max(e0, e1));
			g.setColour(juce::Colour(0xffc4c8cd));
			g.fillRect(r);
		}
	}
	juce::Path shape;
	shape.addRoundedRectangle(R(body.getX(), body.getY(), body.getRight(), body.getBottom()), L(0.35f));
	shadowed(g, shape, 1.8f);
	const auto br = R(body.getX(), body.getY(), body.getRight(), body.getBottom());
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff313338), br.getX(), br.getY(), juce::Colour(0xff17181b), br.getRight(), br.getBottom(), false));
	g.fillPath(shape);
	g.setColour(juce::Colour(0x22ffffff));
	g.strokePath(shape, juce::PathStrokeType(0.7f));
	// Pin 1's notch, on the side the drawing shows it (else the left / top end).
	char notch = p.dir ? p.dir : (vertical ? 'N' : 'W');
	juce::Point<float> nAt = notch == 'N' ? juce::Point<float>(c.x, body.getY()) : notch == 'S' ? juce::Point<float>(c.x, body.getBottom())
	                       : notch == 'E' ? juce::Point<float>(body.getRight(), c.y) : juce::Point<float>(body.getX(), c.y);
	g.setColour(juce::Colour(0xff0d0e10));
	g.fillEllipse(juce::Rectangle<float>(L(1.8f), L(1.8f)).withCentre(P(nAt)));
	// The maker's marking: date code over the part number.
	static const std::pair<const char *, const char *> markings[] = {
		{"IC1", "MN4013B"},   {"IC3", "TC5514PL"},  {"IC4", "TC5514PL"},  {"IC5", "TC5514PL"},  {"IC6", "HD14584BP"},
		{"IC7", "HD14001BP"}, {"IC8", "D650C 133"}, {"IC9", "HD14174BP"}, {"IC10", "HD14050BP"}, {"IC11", "6562"},
		{"IC12", "HD14066BP"}, {"IC13", "MN4013B"}, {"IC16", "6562"},     {"IC2", "HD14049UBP"}};
	const char *mark = "";
	for (const auto &[ref, m] : markings)
		if (std::strcmp(ref, p.ref) == 0)
			mark = m;
	if (*mark) {
		juce::Graphics::ScopedSaveState s(g);
		const auto cc = P(c);
		const float avail = L((vertical ? len : len) - 3.0f);
		g.addTransform(juce::AffineTransform::rotation(vertical ? -kPi / 2 : 0, cc.x, cc.y));
		juce::Font f(juce::FontOptions("Helvetica Neue", L(std::min(2.3f, wid * 0.32f)), juce::Font::plain));
		const float w = f.getStringWidthFloat(mark);
		if (w > avail)
			f.setHorizontalScale(avail / w);
		g.setFont(f);
		g.setColour(juce::Colour(0xffb9bcc1));
		const bool code = std::strncmp(mark, "HD", 2) == 0 || std::strncmp(mark, "MN", 2) == 0;
		g.drawText(mark, juce::Rectangle<float>(L(len), L(2.6f)).withCentre(cc + juce::Point<float>(0, code ? L(0.9f) : 0)), juce::Justification::centred, false);
		if (code) {
			g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", L(1.6f), juce::Font::plain)));
			g.drawText(juce::String::formatted("2%c%d 4", 'A' + int(hashOf(p.ref) % 6), int(1 + hashOf(p.ref) % 4)),
			           juce::Rectangle<float>(L(len), L(2)).withCentre(cc - juce::Point<float>(0, L(1.3f))), juce::Justification::centred, false);
		}
	}
}

void drawSip(juce::Graphics &g, const Part &p)
{
	const juce::Rectangle<float> box(juce::Point<float>(std::min(p.ax, p.bx), std::min(p.ay, p.by)), juce::Point<float>(std::max(p.ax, p.bx), std::max(p.ay, p.by)));
	const bool vertical = box.getHeight() > box.getWidth();
	const float len = vertical ? box.getHeight() : box.getWidth();
	const auto c = box.getCentre();
	const auto body = vertical ? juce::Rectangle<float>(3.4f, len).withCentre(c) : juce::Rectangle<float>(len, 3.4f).withCentre(c);
	juce::Path shape;
	shape.addRoundedRectangle(R(body.getX(), body.getY(), body.getRight(), body.getBottom()), L(0.3f));
	shadowed(g, shape, 3.0f);
	const auto br = R(body.getX(), body.getY(), body.getRight(), body.getBottom());
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff36383d), br.getX(), br.getY(), juce::Colour(0xff1a1b1e), br.getRight(), br.getBottom(), false));
	g.fillPath(shape);
	if (std::strcmp(p.ref, "IC15") == 0 || std::strcmp(p.ref, "IC14") == 0) {
		// IC14's tab is its heat sink; the markings face the front.
		juce::Graphics::ScopedSaveState s(g);
		const auto cc = P(c);
		if (vertical)
			g.addTransform(juce::AffineTransform::rotation(-kPi / 2, cc.x, cc.y));
		g.setFont(juce::Font(juce::FontOptions("Helvetica Neue", L(1.9f), juce::Font::plain)));
		g.setColour(juce::Colour(0xffb9bcc1));
		g.drawText(std::strcmp(p.ref, "IC15") == 0 ? "BA662" : "LA4140", juce::Rectangle<float>(L(len), L(3)).withCentre(cc), juce::Justification::centred, false);
	}
}

// Coils: T1, the IF can (square, its slotted ferrite core showing); T2, the converter's round pot core.
void drawCoil(juce::Graphics &g, const Part &p)
{
	const auto c = P(centreMm(p));
	if (has(p.ax) && has(p.bx)) {
		const auto r = R(std::min(p.ax, p.bx), std::min(p.ay, p.by), std::max(p.ax, p.bx), std::max(p.ay, p.by));
		juce::Path can;
		can.addRoundedRectangle(r, L(0.6f));
		shadowed(g, can, 4.0f);
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe3e6e9), r.getX(), r.getY(), juce::Colour(0xff8c939a), r.getRight(), r.getBottom(), false));
		g.fillPath(can);
		g.setColour(juce::Colour(0xff6c737a));
		g.strokePath(can, juce::PathStrokeType(0.8f));
		const float rc = std::min(r.getWidth(), r.getHeight()) * 0.28f;
		g.setColour(juce::Colour(0xff2a2522));
		g.fillEllipse(c.x - rc, c.y - rc, 2 * rc, 2 * rc);
		g.setColour(juce::Colour(0xff6b5f55));
		g.fillRect(c.x - rc * 0.8f, c.y - rc * 0.15f, rc * 1.6f, rc * 0.3f);
		return;
	}
	const float r = L((p.size > 2 ? p.size : 8.0f) / 2);
	juce::Path core;
	core.addEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
	shadowed(g, core, 4.0f);
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4440), c.x - r, c.y - r, juce::Colour(0xff1d1a18), c.x + r, c.y + r, false));
	g.fillPath(core);
	// The winding's copper showing between the halves.
	g.setColour(juce::Colour(0xffb8743a));
	g.drawEllipse(c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f, r * 0.12f);
	g.setColour(juce::Colour(0xff2b2724));
	g.fillEllipse(c.x - r * 0.25f, c.y - r * 0.25f, r * 0.5f, r * 0.5f);
}

void drawJack(juce::Graphics &g, const Part &p)
{
	const auto r0 = juce::Rectangle<float>(juce::Point<float>(std::min(p.ax, p.bx), std::min(p.ay, p.by)), juce::Point<float>(std::max(p.ax, p.bx), std::max(p.ay, p.by)));
	const auto r = R(r0.getX(), r0.getY(), r0.getRight(), r0.getBottom());
	const bool din = r0.getWidth() > 17 && r0.getHeight() < 26;
	const bool small = r0.getWidth() < 12;
	juce::Path body;
	body.addRoundedRectangle(r, L(0.7f));
	shadowed(g, body, 4.0f);
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2c2e33), r.getX(), r.getY(), juce::Colour(0xff15161a), r.getRight(), r.getBottom(), false));
	g.fillPath(body);
	g.setColour(juce::Colour(0x30ffffff));
	g.drawRoundedRectangle(r.reduced(0.6f), L(0.6f), 0.8f);
	if (din) {
		// The DIN socket's metal shell, its round face to the rear.
		const auto shell = R(r0.getX() + 1.2f, r0.getY(), r0.getRight() - 1.2f, r0.getY() + 4.5f);
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd9dce0), shell.getX(), shell.getY(), juce::Colour(0xff7d838a), shell.getX(), shell.getBottom(), false));
		g.fillRect(shell);
		g.setColour(juce::Colour(0xff1d1e21));
		for (int i = 0; i < 3; ++i)
			g.fillRect(R(r0.getX() + 3 + i * (r0.getWidth() - 6) / 2 - 0.4f, r0.getY() + 6, r0.getX() + 3 + i * (r0.getWidth() - 6) / 2 + 0.4f, r0.getBottom() - 2));
		return;
	}
	// Moulded ribs along the body, and the threaded bush out to the rear panel.
	g.setColour(juce::Colour(0xff0e0f11));
	for (float y = r0.getY() + 5; y < r0.getBottom() - 1.5f; y += 2.2f)
		g.drawLine(juce::Line<float>(P(r0.getX() + 1, y), P(r0.getRight() - 1, y)), 1.0f);
	const float bw = small ? 5.0f : 8.5f;
	const auto bush = R(r0.getCentreX() - bw / 2, r0.getY() - 0.5f, r0.getCentreX() + bw / 2, r0.getY() + 3.5f);
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd8c69a), bush.getX(), 0, juce::Colour(0xff8c7a4e), bush.getRight(), 0, false));
	g.fillRect(bush);
	g.setColour(juce::Colour(0x55000000));
	for (float x = bush.getX() + 1.5f; x < bush.getRight(); x += 2.2f)
		g.drawVerticalLine(int(x), bush.getY(), bush.getBottom());
}

// The trimmer's rotor: the white or blue body and its square boss with the cross slot.
void drawTrimmerAt(juce::Graphics &g, juce::Point<float> c, float r, float v, bool blue)
{
	const float a = juce::degreesToRadians(-135.0f + 270.0f * v);
	juce::Path body;
	body.addEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
	g.setColour(juce::Colours::black.withAlpha(0.32f));
	g.fillEllipse(c.x - r + r * 0.12f, c.y - r + r * 0.25f, 2 * r, 2 * r);
	const juce::Colour base = blue ? juce::Colour(0xff3f72c6) : juce::Colour(0xffefebe1);
	g.setGradientFill(juce::ColourGradient(base.brighter(0.35f), c.x - r * 0.5f, c.y - r * 0.6f, base.darker(0.35f), c.x + r, c.y + r, true));
	g.fillPath(body);
	g.setColour(base.darker(0.5f).withAlpha(0.6f));
	g.strokePath(body, juce::PathStrokeType(std::max(0.6f, r * 0.05f)));
	// The boss, turned with the rotor.
	const auto t = juce::AffineTransform::rotation(a).translated(c);
	const float s = r * 0.62f;
	juce::Path boss;
	boss.addRoundedRectangle(-s, -s, 2 * s, 2 * s, s * 0.22f);
	boss.applyTransform(t);
	g.setColour(base.darker(0.45f).withAlpha(0.5f));
	g.fillPath(boss, juce::AffineTransform::translation(r * 0.06f, r * 0.1f));
	g.setGradientFill(juce::ColourGradient(base.brighter(0.5f), c.x - s, c.y - s, base.darker(0.15f), c.x + s, c.y + s, false));
	g.fillPath(boss);
	// The cross slot, one arm longer: the screwdriver's pointer.
	const juce::Colour slot = blue ? juce::Colour(0xff1c3d80) : juce::Colour(0xff9b968a);
	for (int k = 0; k < 2; ++k) {
		juce::Path arm;
		const float lw = s * (k == 0 ? 0.95f : 0.8f), hw = s * 0.17f;
		arm.addRoundedRectangle(-hw, -lw, 2 * hw, 2 * lw, hw * 0.6f);
		arm.applyTransform(juce::AffineTransform::rotation(k * kPi / 2).followedBy(t));
		g.setColour(slot);
		g.fillPath(arm);
	}
}
} // namespace

// ====================================================================== geometry

juce::Point<float> HardwareView::servicePos(int element) const
{
	const auto c = centreOf(elements[element]);
	const bool onSwitchBoard = std::strncmp(elements[element].id, "led_", 4) == 0;
	return P(c.x, c.y + (onSwitchBoard ? kSwitchDy : 0));
}

juce::Point<float> HardwareView::trimmerPos(int i) const { return P(trimmerMm(i)); }
juce::Point<float> HardwareView::waveformPos() const { return P(checkPointMm("S1")); }

juce::Rectangle<float> HardwareView::serviceKey(int keyIndex) const
{
	// The key switch under the key: the same square switch under the wide keys too.
	const Element &e = elements[m_keys[size_t(keyIndex)].element];
	const auto c = centreOf(e);
	const float w = 10.4f, h = 10.0f;
	return R(c.x - w / 2, c.y + kSwitchDy - h / 2, c.x + w / 2, c.y + kSwitchDy + h / 2);
}

juce::Rectangle<float> HardwareView::trimRow(int i) const { return {kIx + 4, kTrimPanel.getY() + 28 + i * kRowH, kIw - 8, kRowH - 3}; }
juce::Rectangle<float> HardwareView::trimTrack(int i) const
{
	const auto r = trimRow(i);
	return {r.getX() + 30, r.getY() + 31, r.getWidth() - 50, 4};
}

void HardwareView::setService(bool on)
{
	if (m_service != on)
		releaseAllKeys();
	m_service = on;
	m_proc.params().state.setProperty("service", on, nullptr);
	m_proc.setScopeOn(on);
	m_traces.clear();
	m_heldNotes.clear();
	m_hover = {};
	repaint();
}

void HardwareView::setServiceTrim(int trim)
{
	m_trim = juce::jlimit(0, 5, trim);
	m_traces.clear();
	m_scopeReading.clear();
	m_proc.params().state.setProperty("serviceTrim", m_trim, nullptr);
	repaint();
}

void HardwareView::factoryTrim()
{
	for (int i = 0; i < 6; ++i)
		resetTrim(i);
}

void HardwareView::resetTrim(int i)
{
	if (auto *p = m_proc.params().getParameter(TB303Processor::kTrimIds[i])) {
		p->beginChangeGesture();
		p->setValueNotifyingHost(p->getDefaultValue());
		p->endChangeGesture();
	}
	repaint();
}

void HardwareView::nudgeTrim(int i, float delta)
{
	if (auto *p = m_proc.params().getParameter(TB303Processor::kTrimIds[i])) {
		p->beginChangeGesture();
		p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->getValue() + delta));
		p->endChangeGesture();
	}
	repaint();
}

namespace {
// What the notes measure for each trimmer, as the control shows it, and whether it's in spec.
juce::String trimValue(int i, const tr::Trims &t)
{
	switch (i) {
	case 0: return juce::String(tr::tp1PeriodMs(t), 2) + " ms";
	case 1: return juce::String(tr::tp2PeriodMs(t), 2) + " ms";
	case 2: return juce::String(tr::ringMs(t), 2) + " ms";
	case 3: return juce::String(tr::aHz(t), 1) + " Hz";
	case 4: return juce::String(tr::octaveRatio(t), 3) + " : 1";
	case 5: return juce::String(tr::cvPerOctave(t), 3) + " V";
	default: return {};
	}
}
bool trimInSpec(int i, const tr::Trims &t)
{
	switch (i) {
	case 0: return tr::tp1InSpec(t);
	case 1: return tr::tp2InSpec(t);
	case 2: return tr::ringInSpec(t);
	case 3: return tr::tuneInSpec(t);
	case 4: return tr::widthInSpec(t);
	case 5: return tr::cvInSpec(t);
	default: return true;
	}
}
double &trimRef(tr::Trims &t, int i)
{
	double *v[6] = {&t.tm1, &t.tm2, &t.tm3, &t.tm4, &t.tm5, &t.tm6};
	return *v[i];
}
} // namespace

juce::String HardwareView::trimmerText(int i) const
{
	const auto t = m_proc.trims(), d = TB303Processor::trimDefaults();
	return juce::String(kTrims[i].ref) + "  " + kTrims[i].plain + "  " + trimValue(i, t) + "  (factory " + trimValue(i, d) + ")";
}

HardwareView::Hit HardwareView::hitTestService(juce::Point<float> p) const
{
	for (int i = 0; i < 6; ++i) {
		const auto track = trimTrack(i);
		if (juce::Rectangle<float>(track.getX() - 12, track.getY() - 12, track.getWidth() + 24, track.getHeight() + 24).contains(p))
			return {HitKind::TrimSlider, i};
		if (trimRow(i).contains(p))
			return {HitKind::TrimRow, i};
	}
	for (int i = 0; i < 6; ++i)
		if (trimmerPos(i).getDistanceFrom(p) < L(kTrimmerDia / 2 + 1.5f))
			return {HitKind::Trimmer, i};
	for (size_t i = 0; i < m_knobs.size(); ++i) {
		const Element &e = elements[m_knobs[i].element];
		if (servicePos(m_knobs[i].element).getDistanceFrom(p) < L(e.extra * 0.5f + 0.5f))
			return {HitKind::Knob, int(i)};
	}
	for (size_t i = 0; i < m_switches.size(); ++i)
		if (servicePos(m_switches[i].element).getDistanceFrom(p) < L(12.5f))
			return {HitKind::Switch, int(i)};
	for (size_t i = 0; i < m_keys.size(); ++i)
		if (serviceKey(int(i)).expanded(L(1)).contains(p))
			return {HitKind::Key, int(i)};
	if (waveformPos().getDistanceFrom(p) < L(6))
		return {HitKind::Waveform, 0};
	return {};
}

// ====================================================================== the boards (cached)

void HardwareView::renderServiceStatic(juce::Graphics &g)
{
	// The bench: a grey anti-static mat.
	g.fillAll(juce::Colour(0xff1e2126));
	{
		Rng rng{77};
		for (int i = 0; i < 2600; ++i) {
			g.setColour(juce::Colours::white.withAlpha(rng.range(0.01f, 0.035f)));
			g.fillRect(rng.range(0, float(kWidth)), rng.range(0, float(kPanelHeight)), 1.0f, 1.0f);
		}
	}

	// The boards: edge, then the mask.
	auto outline = [](const float *pts, size_t n) {
		juce::Path p;
		for (size_t i = 0; i + 1 < n; i += 2)
			if (i == 0)
				p.startNewSubPath(P(pts[i], pts[i + 1]));
			else
				p.lineTo(P(pts[i], pts[i + 1]));
		p.closeSubPath();
		return p.createPathWithRoundedCorners(L(0.8f));
	};
	const juce::Path boards[2] = {outline(board::kMainOutline, std::size(board::kMainOutline)), outline(board::kSwitchOutline, std::size(board::kSwitchOutline))};
	for (const auto &b : boards) {
		juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 12, {2, 5}).drawForPath(g, b);
		g.setColour(juce::Colour(0xffcabf8f)); // the laminate's cut edge
		g.fillPath(b);
		const auto r = b.getBounds();
		g.setGradientFill(juce::ColourGradient(kMask.brighter(0.08f), r.getX(), r.getY(), kMaskDark, r.getRight(), r.getBottom(), false));
		juce::Graphics::ScopedSaveState s(g);
		g.reduceClipRegion(b);
		g.fillRect(r.reduced(1.2f));
	}

	// Every lead hole, for the copper.
	struct Hole {
		juce::Point<float> at;
		int part;
		bool sw;
	};
	std::vector<Hole> holes;
	const int n = int(std::size(board::parts));
	for (int i = 0; i < n; ++i) {
		const Part &p = board::parts[i];
		switch (p.kind) {
		case Kind::CE:
			if (p.dir != 'L' && has(p.ax) && has(p.bx))
				holes.push_back({padA(p), i, p.switchBoard}), holes.push_back({padB(p), i, p.switchBoard});
			break;
		case Kind::R:
		case Kind::C:
		case Kind::D:
		case Kind::ZD:
		case Kind::L:
		case Kind::JW:
			if (has(p.ax) && has(p.bx))
				holes.push_back({padA(p), i, p.switchBoard}), holes.push_back({padB(p), i, p.switchBoard});
			break;
		case Kind::Q:
			if (p.dir != 'B' && has(p.ax)) {
				holes.push_back({padA(p), i, p.switchBoard});
				holes.push_back({padB(p), i, p.switchBoard});
				if (has(p.qx))
					holes.push_back({{p.qx, p.qy}, i, p.switchBoard});
			}
			break;
		case Kind::TP:
			if (has(p.ax) && has(p.bx)) {
				holes.push_back({padA(p), i, p.switchBoard}), holes.push_back({padB(p), i, p.switchBoard});
				break;
			}
			holes.push_back({centreMm(p), i, p.switchBoard});
			break;
		case Kind::WP:
			holes.push_back({centreMm(p), i, p.switchBoard});
			break;
		case Kind::IC: {
			const juce::Rectangle<float> box(juce::Point<float>(std::min(p.ax, p.bx), std::min(p.ay, p.by)), juce::Point<float>(std::max(p.ax, p.bx), std::max(p.ay, p.by)));
			const bool vertical = box.getHeight() > box.getWidth();
			const int per = std::max(1, (p.size > 3 ? int(p.size) : 2 * juce::roundToInt((vertical ? box.getHeight() : box.getWidth()) / 2.54f)) / 2);
			const float wid = per > 15 ? 13.8f : 6.3f;
			for (int k = 0; k < per; ++k) {
				const float o = -per * 2.54f / 2 + (k + 0.5f) * 2.54f;
				for (int s : {-1, 1})
					holes.push_back({vertical ? juce::Point<float>(box.getCentreX() + s * (wid / 2 + 0.7f), box.getCentreY() + o)
					                          : juce::Point<float>(box.getCentreX() + o, box.getCentreY() + s * (wid / 2 + 0.7f)),
					                 i, p.switchBoard});
			}
			break;
		}
		case Kind::SIP:
		case Kind::VR:
		case Kind::TM:
		case Kind::S:
		case Kind::J:
		case Kind::Hole:
		case Kind::Txt:
			break;
		}
	}
	// The pots' and trimmers' tags, and the key switches' and LEDs' legs.
	for (const auto &k : m_knobs) {
		const Element &e = elements[k.element];
		const auto c = centreOf(e);
		const float dy = e.extra > 15 ? 11.5f : 9.6f;
		for (float dx : {-5.0f, 0.0f, 5.0f})
			holes.push_back({{c.x + dx, c.y + dy}, -1, false});
	}
	for (int i = 0; i < 6; ++i)
		for (const auto &leg : {juce::Point<float>(-2.5f, 3.4f), juce::Point<float>(0, -3.8f), juce::Point<float>(2.5f, 3.4f)})
			holes.push_back({trimmerMm(i) + leg, -2 - i, false});
	for (size_t i = 0; i < m_keys.size(); ++i) {
		const auto c = centreOf(elements[m_keys[i].element]);
		for (float dx : {-3.8f, 3.8f})
			for (float dy : {-3.2f, 3.2f})
				holes.push_back({{c.x + dx, c.y + kSwitchDy + dy}, -100 - int(i), true});
	}
	for (const auto &l : m_leds) {
		const auto c = centreOf(elements[l.element]);
		for (float dx : {-1.27f, 1.27f})
			holes.push_back({{c.x + dx, c.y + kSwitchDy}, -200, true});
	}

	// Copper under the mask: each hole to its nearest neighbours of other parts (a spanning
	// forest), routed at 0/45/90 degrees as a board's traces run. Drawn, not copied.
	{
		struct Edge {
			float d;
			int a, b;
		};
		std::vector<Edge> edges;
		const int m = int(holes.size());
		for (int a = 0; a < m; ++a) {
			std::array<std::pair<float, int>, 4> best;
			best.fill({1e9f, -1});
			for (int b = 0; b < m; ++b) {
				if (b == a || holes[size_t(b)].sw != holes[size_t(a)].sw || (holes[size_t(b)].part == holes[size_t(a)].part && holes[size_t(a)].part >= 0))
					continue;
				const float d = holes[size_t(a)].at.getDistanceFrom(holes[size_t(b)].at);
				if (d < best.back().first) {
					best.back() = {d, b};
					std::sort(best.begin(), best.end());
				}
			}
			for (const auto &[d, b] : best)
				if (b >= 0 && d < 16)
					edges.push_back({d, std::min(a, b), std::max(a, b)});
		}
		std::sort(edges.begin(), edges.end(), [](const Edge &x, const Edge &y) { return x.d < y.d; });
		std::vector<int> parent(static_cast<size_t>(m));
		std::iota(parent.begin(), parent.end(), 0);
		std::function<int(int)> root = [&](int x) { return parent[size_t(x)] == x ? x : parent[size_t(x)] = root(parent[size_t(x)]); };
		Rng rng{303};
		juce::Path copper[3];
		for (const auto &e : edges) {
			const int ra = root(e.a), rb = root(e.b);
			if (ra == rb)
				continue;
			parent[size_t(ra)] = rb;
			const auto a = holes[size_t(e.a)].at, b = holes[size_t(e.b)].at;
			const float dx = b.x - a.x, dy = b.y - a.y, adx = std::abs(dx), ady = std::abs(dy);
			const float diag = std::min(adx, ady);
			const juce::Point<float> step(dx >= 0 ? diag : -diag, dy >= 0 ? diag : -diag);
			juce::Path t;
			t.startNewSubPath(P(a));
			if (rng.next() < 0.5f) {
				t.lineTo(P(a + step));
			} else {
				t.lineTo(P(b - step));
			}
			t.lineTo(P(b));
			copper[int(rng.next() * 3) % 3].addPath(t);
		}
		const float widths[3] = {0.5f, 0.65f, 0.8f};
		for (int k = 0; k < 3; ++k) {
			g.setColour(kTrace.darker(0.35f).withAlpha(0.55f));
			g.strokePath(copper[k], juce::PathStrokeType(L(widths[k]) + 1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
			g.setColour(kTrace);
			g.strokePath(copper[k], juce::PathStrokeType(L(widths[k]), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
		}
	}

	// Mounting holes.
	for (const auto &p : board::parts)
		if (p.kind == Kind::Hole) {
			const auto c = P(centreMm(p));
			const float r = L((p.size > 0.5f ? p.size : 3.0f) / 2);
			if (p.size >= 2.5f) {
				// A mounting hole, its ring plated.
				g.setColour(juce::Colour(0xffc4c9cc));
				g.fillEllipse(c.x - r - L(0.9f), c.y - r - L(0.9f), 2 * (r + L(0.9f)), 2 * (r + L(0.9f)));
			}
			g.setColour(juce::Colour(0xff121417));
			g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
		}

	// The legend: outlines under the bigger parts, then every designator where it's printed.
	g.setColour(kSilk.withAlpha(0.85f));
	for (const auto &p : board::parts) {
		if (p.kind == Kind::IC || p.kind == Kind::SIP || (p.kind == Kind::J && std::strncmp(p.ref, "CONN", 4) == 0)) {
			const auto r = R(std::min(p.ax, p.bx) - 0.9f, std::min(p.ay, p.by) - 0.9f, std::max(p.ax, p.bx) + 0.9f, std::max(p.ay, p.by) + 0.9f);
			g.drawRect(r, L(0.25f));
		} else if (p.kind == Kind::TM) {
			const float r = L(std::max(p.size, kTrimmerDia + 2) / 2);
			const auto c = P(centreMm(p));
			g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r, L(0.25f));
		}
	}
	for (const auto &p : board::parts) {
		if (!has(p.tx) || std::strcmp(p.label, "?") == 0)
			continue;
		const float cap = p.kind == Kind::Txt ? (p.size > 0.5f ? p.size : 1.8f) : p.kind == Kind::TM || p.kind == Kind::TP ? 1.7f : 1.45f;
		silk(g, p.label, p.tx, p.ty, cap, p.rot);
	}

	// Pads.
	for (const auto &h : holes)
		if (h.part >= 0 || h.part <= -100)
			pad(g, h.at);

	// The parts, low ones first.
	auto each = [&](std::initializer_list<Kind> kinds, auto draw) {
		for (const auto &p : board::parts)
			for (Kind k : kinds)
				if (p.kind == k)
					draw(p);
	};
	each({Kind::JW}, [&](const Part &p) {
		if (has(p.ax) && has(p.bx))
			lead(g, padA(p), padB(p), 0.6f);
	});
	each({Kind::R}, [&](const Part &p) {
		if (has(p.ax) && has(p.bx))
			drawResistor(g, p);
	});
	each({Kind::D, Kind::ZD}, [&](const Part &p) {
		if (!has(p.ax) || !has(p.bx))
			return;
		// The switch board's LEDs are drawn as diodes; the LEDs themselves come later.
		if (p.switchBoard)
			for (const auto &l : m_leds) {
				const auto c = centreOf(elements[l.element]);
				if (centreMm(p).getDistanceFrom({c.x, c.y + kSwitchDy}) < 3)
					return;
			}
		drawDiode(g, p);
	});
	each({Kind::C}, [&](const Part &p) {
		if (has(p.ax) && has(p.bx))
			drawFilmCap(g, p);
	});
	each({Kind::L}, [&](const Part &p) { drawCoil(g, p); });
	each({Kind::Q}, [&](const Part &p) { drawTransistor(g, p); });
	each({Kind::IC}, [&](const Part &p) {
		if (has(p.ax) && has(p.bx))
			drawIc(g, p);
	});
	each({Kind::SIP}, [&](const Part &p) {
		if (has(p.ax) && has(p.bx))
			drawSip(g, p);
	});
	each({Kind::CE}, [&](const Part &p) { drawElectrolytic(g, p); });
	each({Kind::J}, [&](const Part &p) {
		if (has(p.ax) && has(p.bx) && std::strncmp(p.ref, "CONN", 4) != 0)
			drawJack(g, p);
	});
	// Test points: a wire loop standing in the pad, or bridging two (TP3).
	each({Kind::TP}, [&](const Part &p) {
		if (has(p.ax) && has(p.bx)) {
			const auto a = P(padA(p)), b = P(padB(p));
			const auto m = (a + b) * 0.5f + juce::Point<float>(L(1.8f), 0);
			juce::Path loop;
			loop.startNewSubPath(a);
			loop.quadraticTo(m, b);
			g.setColour(juce::Colours::black.withAlpha(0.3f));
			g.strokePath(loop, juce::PathStrokeType(L(0.5f)), juce::AffineTransform::translation(1.2f, 2.4f));
			g.setColour(juce::Colour(0xffd4d8dc));
			g.strokePath(loop, juce::PathStrokeType(L(0.5f)));
			return;
		}
		const auto c = P(centreMm(p));
		g.setColour(juce::Colours::black.withAlpha(0.3f));
		g.drawEllipse(c.x - L(0.9f) + 1, c.y - L(0.9f) + 2, L(1.8f), L(1.8f), L(0.4f));
		g.setColour(juce::Colour(0xffd4d8dc));
		g.drawEllipse(c.x - L(0.9f), c.y - L(0.9f), L(1.8f), L(1.8f), L(0.45f));
	});

	// S1, the WAVEFORM slide switch, at the rear edge: its frame (the slider moves).
	if (const Part *s1 = findPart("S1")) {
		const auto c = centreMm(*s1);
		const float hw = has(s1->ax) ? std::min(6.0f, std::abs(s1->bx - s1->ax) / 2) : 5.0f;
		const auto r = R(c.x - hw, c.y - 3.4f, c.x + hw, c.y + 3.4f);
		juce::Path p;
		p.addRoundedRectangle(r, L(0.5f));
		shadowed(g, p, 2.5f);
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xffdfe2e5), 0, r.getY(), juce::Colour(0xff9aa0a7), 0, r.getBottom(), false));
		g.fillPath(p);
		g.setColour(juce::Colour(0xff1e1f22));
		g.fillRect(R(c.x - 3.6f, c.y - 1.5f, c.x + 3.6f, c.y + 1.5f));
	}

	// Pot bodies (their knobs turn on top) and the rotary switches' dust covers.
	for (const auto &k : m_knobs) {
		const Element &e = elements[k.element];
		const auto c0 = centreOf(e);
		const bool big = e.extra > 15;
		const float r = big ? 12.0f : 8.3f;
		const float dy = big ? 11.5f : 9.6f;
		for (float dx : {-5.0f, 0.0f, 5.0f}) {
			const auto tab = R(c0.x + dx - 1.2f, c0.y + r - 1.5f, c0.x + dx + 1.2f, c0.y + dy + 0.9f);
			g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe2e5e8), tab.getX(), 0, juce::Colour(0xff8e949b), tab.getRight(), 0, false));
			g.fillRoundedRectangle(tab, 1.5f);
		}
		juce::Path body;
		if (big)
			body.addRoundedRectangle(R(c0.x - r, c0.y - r, c0.x + r, c0.y + r), L(1.2f));
		else
			body.addEllipse(juce::Rectangle<float>(L(2 * r), L(2 * r)).withCentre(P(c0)));
		shadowed(g, body, 3.0f);
		const auto b = body.getBounds();
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd7dade), b.getX(), b.getY(), juce::Colour(0xff7e848b), b.getRight(), b.getBottom(), false));
		g.fillPath(body);
		g.setColour(juce::Colour(0xff6c7178));
		g.strokePath(body, juce::PathStrokeType(0.8f));
	}
	for (const auto &s : m_switches) {
		const auto c = P(centreOf(elements[s.element]));
		juce::Path disc;
		disc.addEllipse(juce::Rectangle<float>(L(25.5f), L(25.5f)).withCentre(c));
		shadowed(g, disc, 3.0f);
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff6f1e4), c.x - L(6), c.y - L(7), juce::Colour(0xffcfc6ae), c.x + L(12), c.y + L(12), true));
		g.fillPath(disc);
		g.setColour(juce::Colour(0xffb3aa92));
		g.strokePath(disc, juce::PathStrokeType(0.8f));
		g.setColour(juce::Colour(0x18000000));
		g.drawEllipse(juce::Rectangle<float>(L(15), L(15)).withCentre(c), 1.0f);
	}

	// Switch board: the key switches' cans (their plungers move) and the LEDs' spacers.
	for (size_t i = 0; i < m_keys.size(); ++i) {
		const auto r = serviceKey(int(i));
		juce::Path p;
		p.addRoundedRectangle(r, L(0.6f));
		shadowed(g, p, 3.0f);
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe4e6e8), r.getX(), r.getY(), juce::Colour(0xff9ea3a9), r.getRight(), r.getBottom(), false));
		g.fillPath(p);
		g.setColour(juce::Colour(0xff7b8087));
		g.strokePath(p, juce::PathStrokeType(0.8f));
		// The can's folded rim.
		g.setColour(juce::Colour(0x40ffffff));
		g.drawRoundedRectangle(r.reduced(L(0.8f)), L(0.4f), 0.8f);
	}
	for (const auto &l : m_leds) {
		const auto c = servicePos(l.element);
		g.setColour(juce::Colours::black.withAlpha(0.3f));
		g.fillEllipse(juce::Rectangle<float>(L(4.6f), L(4.6f)).withCentre(c + juce::Point<float>(1, 2)));
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xfffbfaf6), c.x - L(1.5f), c.y - L(1.5f), juce::Colour(0xffcfcbc2), c.x + L(2.3f), c.y + L(2.3f), false));
		g.fillEllipse(juce::Rectangle<float>(L(4.6f), L(4.6f)).withCentre(c));
	}

	// The wiring between the boards: each numbered wire pad on the switch board goes to its number
	// on the main board, a run of consecutive numbers as one flat ribbon, a lone one as a wire.
	{
		std::map<int, std::pair<juce::Point<float>, juce::Point<float>>> ends; // number -> (main, switch)
		std::map<int, int> seen;
		for (const auto &p : board::parts) {
			if (p.kind != Kind::WP)
				continue;
			// The main board's RUN pad is the switch board's 1 (printed RUN beside it).
			const int n = std::strcmp(p.ref, "RUN") == 0 ? 1 : juce::String(p.ref).getIntValue();
			if (n <= 0 || (juce::String(n) != juce::String(p.ref) && n != 1))
				continue;
			(p.switchBoard ? ends[n].second : ends[n].first) = centreMm(p);
			seen[n] |= p.switchBoard ? 2 : 1;
		}
		std::vector<std::vector<int>> runs;
		for (const auto &[n, both] : seen) {
			if (both != 3)
				continue;
			// A run goes on while the next pad is beside the last on both boards.
			const bool next = !runs.empty() && runs.back().back() == n - 1 && ends[n].first.getDistanceFrom(ends[n - 1].first) < 4.5f &&
			                  ends[n].second.getDistanceFrom(ends[n - 1].second) < 4.5f;
			if (!next)
				runs.push_back({});
			runs.back().push_back(n);
		}
		for (const auto &run : runs) {
			auto path = [&](int n, float dx = 0) {
				const auto a = ends[n].first + juce::Point<float>(dx, 0), b = ends[n].second + juce::Point<float>(dx, 0);
				const float sag = std::max(3.0f, (b.y - a.y) * 0.45f);
				juce::Path w;
				w.startNewSubPath(P(a));
				w.cubicTo(P(a.x, a.y + sag), P(b.x, b.y - sag), P(b));
				return w;
			};
			if (run.size() == 1) {
				// The hook-up wires lie dressed around the parts, as photos of a real board show them
				// (the drawing has only their pads): TAP, purple, from beside S1 along under IC7 and
				// over VR2's tags, down between the rotary switches and past IC9 and IC10 to the
				// switch board; RUN, brown, down beside the TEMPO pot; R/S, tan, down the left edge.
				struct Route {
					int n;
					juce::uint32 colour;
					std::vector<juce::Point<float>> via;
				};
				static const Route routes[] = {
					{27, 0xff6f4aa0, {{49, 25}, {60, 28.8f}, {98, 28.8f}, {105, 37}, {105.5f, 72}, {111, 89}, {131, 99.5f}, {170, 104.5f}, {182, 108}}},
					{1, 0xff7a4f2a, {{50, 22}, {54.4f, 31}, {54.4f, 58}, {50, 68}, {30, 78}, {14, 100}, {8, 112}}},
					{2, 0xffb07a4e, {{9.5f, 68}, {7, 100}}},
					{28, 0xff26272b, {{298.2f, 30}, {298.2f, 100}, {296, 112}}},
				};
				const Route *route = nullptr;
				for (const auto &r : routes)
					if (r.n == run[0])
						route = &r;
				std::vector<juce::Point<float>> pts{ends[run[0]].first};
				if (route != nullptr)
					pts.insert(pts.end(), route->via.begin(), route->via.end());
				pts.push_back(ends[run[0]].second);
				// Through the points as a Catmull-Rom curve.
				juce::Path w;
				w.startNewSubPath(P(pts[0]));
				if (pts.size() == 2) {
					w = path(run[0]);
				} else {
					for (size_t k = 0; k + 1 < pts.size(); ++k) {
						const auto p0 = P(pts[k > 0 ? k - 1 : k]), p1 = P(pts[k]), p2 = P(pts[k + 1]), p3 = P(pts[std::min(k + 2, pts.size() - 1)]);
						w.cubicTo(p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
					}
				}
				const juce::Colour c(route != nullptr ? route->colour : 0xff26272b);
				g.setColour(juce::Colours::black.withAlpha(0.3f));
				g.strokePath(w, juce::PathStrokeType(L(1.1f)), juce::AffineTransform::translation(1.5f, 3));
				g.setColour(c.darker(0.4f));
				g.strokePath(w, juce::PathStrokeType(L(1.1f) + 0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
				g.setColour(c);
				g.strokePath(w, juce::PathStrokeType(L(1.1f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
				g.setColour(juce::Colours::white.withAlpha(0.25f));
				g.strokePath(w, juce::PathStrokeType(L(0.3f)), juce::AffineTransform::translation(-0.6f, -0.6f));
				continue;
			}
			// The flat cable: the outermost conductors' paths, half a pitch out, closed between them.
			const int first = run.front(), last = run.back();
			const float pa = (ends[last].first.x - ends[first].first.x) / float(run.size() - 1) * 0.5f;
			const float pb = (ends[last].second.x - ends[first].second.x) / float(run.size() - 1) * 0.5f;
			juce::Path ribbon;
			{
				const auto a0 = ends[first].first - juce::Point<float>(std::abs(pa), 0), b0 = ends[first].second - juce::Point<float>(std::abs(pb), 0);
				const auto a1 = ends[last].first + juce::Point<float>(std::abs(pa), 0), b1 = ends[last].second + juce::Point<float>(std::abs(pb), 0);
				const float sa = std::max(3.0f, (b0.y - a0.y) * 0.45f), sb = std::max(3.0f, (b1.y - a1.y) * 0.45f);
				ribbon.startNewSubPath(P(a0));
				ribbon.cubicTo(P(a0.x, a0.y + sa), P(b0.x, b0.y - sa), P(b0));
				ribbon.lineTo(P(b1));
				ribbon.cubicTo(P(b1.x, b1.y - sb), P(a1.x, a1.y + sb), P(a1));
				ribbon.closeSubPath();
			}
			shadowed(g, ribbon, 3.0f);
			g.setColour(juce::Colour(0xfff1f0ea));
			g.fillPath(ribbon);
			g.setColour(juce::Colour(0xffcdcac0));
			for (int n : run)
				g.strokePath(path(n), juce::PathStrokeType(0.7f));
			for (int n : run)
				pad(g, ends[n].first, 1.5f), pad(g, ends[n].second, 1.5f);
		}
	}

	// The instruments' cases and the trimmers' panel.
	for (const auto &r : {kScopeCase, kCounterCase, kMeterCase}) {
		juce::Path p;
		p.addRoundedRectangle(r, 8);
		juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 10, {0, 3}).drawForPath(g, p);
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xff30343a), r.getX(), r.getY(), juce::Colour(0xff25282d), r.getX(), r.getBottom(), false));
		g.fillPath(p);
		g.setColour(juce::Colour(0xff40454d));
		g.strokePath(p, juce::PathStrokeType(1));
	}
	{
		juce::Path p;
		p.addRoundedRectangle(kTrimPanel, 8);
		juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 10, {0, 3}).drawForPath(g, p);
		g.setColour(juce::Colour(0xff1b1e23));
		g.fillPath(p);
		g.setColour(juce::Colour(0xff2f343c));
		g.strokePath(p, juce::PathStrokeType(1));
		g.setColour(juce::Colour(0xffc9cfd8));
		g.setFont(fonts::mono(11.0f, fonts::Weight::SemiBold));
		g.drawText("TRIMMERS", kTrimPanel.reduced(12, 0).withHeight(26), juce::Justification::centredLeft);
		g.setColour(juce::Colour(0xff6f7884));
		g.setFont(fonts::mono(10.0f));
		g.drawText(juce::String::fromUTF8("green: in spec  \xc2\xb7  tick: factory"), kTrimPanel.reduced(12, 0).withHeight(26), juce::Justification::centredRight);
	}

	// Grain on the boards (not on the bench or the instruments).
	juce::Image::BitmapData bd(m_serviceStatic, juce::Image::BitmapData::readWrite);
	juce::Random rng(101);
	for (int y = 0; y < bd.height; ++y)
		for (int x = 0; x < bd.width; ++x) {
			auto c = bd.getPixelColour(x, y);
			if (c.getGreen() < c.getRed() + 30 || c.getGreen() < c.getBlue() + 10)
				continue;
			const float grain = (rng.nextFloat() - 0.5f) * 0.06f;
			bd.setPixelColour(x, y, grain > 0 ? c.brighter(grain) : c.darker(-grain));
		}
}

// ====================================================================== what moves

void HardwareView::drawTrimmer(juce::Graphics &g, int i)
{
	drawTrimmerAt(g, trimmerPos(i), L(kTrimmerDia / 2), paramValue(TB303Processor::kTrimIds[i]), kTrims[i].blue);
	const bool hot = ((m_hover.kind == HitKind::Trimmer || m_hover.kind == HitKind::TrimRow || m_hover.kind == HitKind::TrimSlider) && m_hover.index == i) ||
	                 ((m_drag.kind == HitKind::Trimmer || m_drag.kind == HitKind::TrimSlider) && m_drag.index == i);
	if (hot && i != m_trim) {
		const auto c = trimmerPos(i);
		const float r = L(kTrimmerDia / 2 + 2.0f);
		g.setColour(juce::Colours::white.withAlpha(0.7f));
		juce::Path ring;
		ring.addEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
		juce::Path dashed;
		const float dash[] = {4, 3};
		juce::PathStrokeType(1.4f).createDashedStroke(dashed, ring, dash, 2);
		g.fillPath(dashed);
	}
}

// The picked trimmer: a ring on it and its name beside it.
void HardwareView::drawPicked(juce::Graphics &g)
{
	const double now = juce::Time::getMillisecondCounterHiRes();
	const auto c = trimmerPos(m_trim);
	const float r = L(kTrimmerDia / 2 + 2.2f);
	const float pulse = 0.5f + 0.5f * std::sin(float(now) * 0.0062f);
	g.setColour(kOrange.withAlpha(0.18f + 0.22f * pulse));
	g.drawEllipse(c.x - r - 2, c.y - r - 2, 2 * r + 4, 2 * r + 4, 6.0f);
	g.setColour(kOrange);
	g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r, 2.0f);
	// Its name, as the board prints it, on a tag beside it.
	const juce::String name = juce::String(kTrims[m_trim].ref) + (*kTrims[m_trim].silk ? juce::String(" ") + kTrims[m_trim].silk : juce::String());
	g.setFont(fonts::mono(11.0f, fonts::Weight::SemiBold));
	const float w = g.getCurrentFont().getStringWidthFloat(name) + 14;
	const bool left = c.x + r + 14 + w > kIx - 8;
	const juce::Rectangle<float> tag(left ? c.x - r - 12 - w : c.x + r + 12, c.y - r - 18, w, 18);
	g.setColour(kOrange);
	g.drawLine(juce::Line<float>(c + juce::Point<float>(left ? -r * 0.72f : r * 0.72f, -r * 0.72f), {left ? tag.getRight() : tag.getX(), tag.getBottom() - 4}), 1.6f);
	g.fillRoundedRectangle(tag, 4);
	g.setColour(juce::Colour(0xff17120c));
	g.drawText(name, tag, juce::Justification::centred);
}

// The instruments' probes: the scope's hook on the picked trimmer's check point, the meter's
// on CV OUT, their leads off the top of the bench.
void HardwareView::drawProbes(juce::Graphics &g)
{
	auto probe = [&](juce::Point<float> tip, juce::Colour band, float lean) {
		const juce::Point<float> dir(std::sin(lean), -std::cos(lean));
		const auto back = tip + dir * L(16);
		// The lead.
		juce::Path cable;
		cable.startNewSubPath(back);
		const juce::Point<float> out(back.x + L(6), -10);
		cable.cubicTo(back + dir * L(14), out + juce::Point<float>(0, L(18)), out);
		g.setColour(juce::Colours::black.withAlpha(0.35f));
		g.strokePath(cable, juce::PathStrokeType(L(1.6f)), juce::AffineTransform::translation(2, 4));
		g.setColour(juce::Colour(0xff2b2d31));
		g.strokePath(cable, juce::PathStrokeType(L(1.6f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
		g.setColour(juce::Colour(0x30ffffff));
		g.strokePath(cable, juce::PathStrokeType(L(0.4f)), juce::AffineTransform::translation(-1, -1));
		// The body, its band and the hook.
		const auto t = juce::AffineTransform::rotation(lean).translated(tip);
		juce::Path body;
		body.addRoundedRectangle(-L(1.5f), -L(16), L(3), L(12.5f), L(1.2f));
		body.applyTransform(t);
		g.setColour(juce::Colours::black.withAlpha(0.35f));
		g.fillPath(body, juce::AffineTransform::translation(3, 5));
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4d53), tip.x - L(2), tip.y, juce::Colour(0xff1d1f22), tip.x + L(2), tip.y, false));
		g.fillPath(body);
		juce::Path ring;
		ring.addRectangle(-L(1.55f), -L(7.2f), L(3.1f), L(1.3f));
		ring.applyTransform(t);
		g.setColour(band);
		g.fillPath(ring);
		juce::Path hook;
		hook.startNewSubPath(0, -L(3.6f));
		hook.lineTo(0, -L(0.6f));
		hook.quadraticTo(L(0.8f), L(0.6f), L(1.4f), -L(0.4f));
		hook.applyTransform(t);
		g.setColour(juce::Colour(0xffd9dde1));
		g.strokePath(hook, juce::PathStrokeType(L(0.45f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
	};
	probe(P(checkPointMm(kTrims[m_trim].probe)), kProbe, 0.45f);
	probe(P(cvJackMm()), kMeterProbe, -0.35f);
}

void HardwareView::drawServiceParts(juce::Graphics &g)
{
	// The knobs stay on their pots; the bar knobs on the rotary switches.
	{
		juce::Graphics::ScopedSaveState s(g);
		g.addTransform(kMmToPx);
		for (size_t i = 0; i < m_knobs.size(); ++i)
			drawKnob(g, m_knobs[i], paramValue(m_knobs[i].param),
			         (m_hover.kind == HitKind::Knob && m_hover.index == int(i)) || (m_drag.kind == HitKind::Knob && m_drag.index == int(i)));
		for (size_t i = 0; i < m_switches.size(); ++i) {
			auto *p = m_proc.params().getParameter(m_switches[i].param);
			const int n = int(m_switches[i].detentsDeg.size());
			const int pos = p ? juce::roundToInt(p->getValue() * (n - 1)) : 0;
			drawSwitch(g, m_switches[i], pos, m_hover.kind == HitKind::Switch && m_hover.index == int(i));
		}
	}
	{
		// S1's slider.
		const auto c = checkPointMm("S1");
		const bool square = paramValue("waveform") > 0.5f;
		const auto r = R(c.x - 3.4f + (square ? 3.4f : 0), c.y - 1.3f, c.x + (square ? 3.4f : 0), c.y + 1.3f);
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5e6167), r.getX(), r.getY(), juce::Colour(0xff222326), r.getX(), r.getBottom(), false));
		g.fillRoundedRectangle(r, L(0.3f));
		if (m_hover.kind == HitKind::Waveform) {
			g.setColour(kOrange.withAlpha(0.8f));
			g.drawRoundedRectangle(R(c.x - 5.6f, c.y - 4, c.x + 5.6f, c.y + 4), 3, 1.6f);
		}
	}

	for (int i = 0; i < 6; ++i)
		drawTrimmer(g, i);

	// Key switches' plungers and the LEDs.
	for (size_t i = 0; i < m_keys.size(); ++i) {
		const auto r = serviceKey(int(i));
		const bool down = m_keyDown[i];
		const auto pl = r.reduced(L(2.6f), L(2.4f)).translated(0, down ? 0.6f : 0);
		g.setColour(juce::Colours::black.withAlpha(down ? 0.15f : 0.35f));
		g.fillRoundedRectangle(pl.translated(0.8f, down ? 0.6f : 1.8f), L(0.4f));
		g.setGradientFill(juce::ColourGradient(down ? juce::Colour(0xffb7bbc0) : juce::Colour(0xfff1f2f3), pl.getX(), pl.getY(),
		                                       down ? juce::Colour(0xff8d9298) : juce::Colour(0xffb5b9be), pl.getRight(), pl.getBottom(), false));
		g.fillRoundedRectangle(pl, L(0.4f));
		if (down) {
			g.setColour(kOrange.withAlpha(0.35f));
			g.drawRoundedRectangle(r.expanded(L(1)), L(1.2f), 4);
			g.setColour(kOrange);
			g.drawRoundedRectangle(r.expanded(L(1)), L(1.2f), 1.6f);
		}
	}
	for (const auto &l : m_leds) {
		const auto c = servicePos(l.element);
		const float level = m_snap.leds[size_t(l.led)];
		const float r = L(1.5f);
		if (level > 0.02f) {
			g.setGradientFill(juce::ColourGradient(juce::Colour(0xffff3b1f).withAlpha(0.55f * level), c.x, c.y, juce::Colour(0x00ff3b1f), c.x + r * 4, c.y, true));
			g.fillEllipse(c.x - r * 4, c.y - r * 4, r * 8, r * 8);
		}
		const auto lit = juce::Colour(0xff8a120d).interpolatedWith(juce::Colour(0xffff5a3c), level);
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xffff9a8a).interpolatedWith(juce::Colours::white, level * 0.8f), c.x - r * 0.3f, c.y - r * 0.4f,
		                                       lit, c.x + r, c.y + r, true));
		g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
	}

	drawProbes(g);
	drawPicked(g);
}

// ====================================================================== instruments

void HardwareView::updateScope()
{
	// Notes held, by their CV (to the semitone) and the VCO's frequency: the WIDTH and CV steps
	// compare two of them an octave apart.
	const auto &pr = m_snap.probe;
	if (pr.gate > 0.5 && pr.vcoHz > 0) {
		const double cv = std::round(pr.cvVolts * 12) / 12;
		if (std::abs(pr.cvVolts - cv) < 0.002) {
			if (!m_heldNotes.empty() && std::abs(m_heldNotes.back().first - cv) < 1e-6)
				m_heldNotes.back().second = pr.vcoHz;
			else
				m_heldNotes.push_back({cv, pr.vcoHz});
			if (m_heldNotes.size() > 8)
				m_heldNotes.erase(m_heldNotes.begin());
		}
	}

	const TrimDef &st = kTrims[m_trim];
	const double rate = m_snap.rate > 0 ? m_snap.rate : 44100;
	const int show = juce::jmax(16, int(10 * st.msPerDiv * 1e-3 * rate));
	const int pre = show / 20;
	const int want = juce::jmin(TB303Processor::kScopeFrames / 2, show * 3);
	m_scopeBuf.resize(size_t(want));
	m_scopeTrig.resize(size_t(want));
	const uint32_t end = m_proc.scopeWritten();
	m_proc.readScope(st.channel, m_scopeBuf.data(), want, end);
	const int trigCh = st.channel == TB303Processor::ScopeTp6 ? int(TB303Processor::ScopeS1) : st.channel;
	if (trigCh != st.channel)
		m_proc.readScope(trigCh, m_scopeTrig.data(), want, end);
	else
		m_scopeTrig = m_scopeBuf;
	const auto &tg = m_scopeTrig;

	// Edges: the saw's reset (or the square's rise), TP1's rise, TP2's fall.
	float lo = tg[0], hi = tg[0];
	for (float v : tg)
		lo = std::min(lo, v), hi = std::max(hi, v);
	auto edge = [&](int i) {
		if (trigCh == TB303Processor::ScopeTp1)
			return tg[size_t(i - 1)] < 3 && tg[size_t(i)] >= 3;
		if (trigCh == TB303Processor::ScopeTp2)
			return tg[size_t(i - 1)] >= 3 && tg[size_t(i)] < 3;
		return hi - lo > 0.2f && tg[size_t(i)] - tg[size_t(i - 1)] > 0.25f * (hi - lo);
	};
	std::vector<int> edges;
	for (int i = 1; i < want; ++i)
		if (edge(i) && (edges.empty() || i - edges.back() > 4))
			edges.push_back(i);
	int t = want - show + pre;
	for (auto it = edges.rbegin(); it != edges.rend(); ++it)
		if (*it >= pre && *it <= want - show + pre) {
			t = *it;
			break;
		}

	// The trace, a min/max pair per column, in screen px; volts as the channel's scale has them.
	const auto sc = kScopeScreen;
	const float H = sc.getHeight(), W = sc.getWidth();
	auto yFor = [&](float v) {
		switch (st.channel) {
		case TB303Processor::ScopeS1: return H - v / 16.0f * H;              // 2 V/div, ground at the bottom
		case TB303Processor::ScopeTp6: return H / 2 - v / (8 * m_tp6Div) * H; // AC
		default: return H - (v + 2) / 16.0f * H;                               // 2 V/div, ground a division up
		}
	};
	if (st.channel == TB303Processor::ScopeTp6) {
		float peak = 0;
		for (int k = 0; k < show; ++k)
			peak = std::max(peak, std::abs(m_scopeBuf[size_t(t - pre + k)]));
		const float steps[] = {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1, 2};
		if (peak > 3.8f * m_tp6Div || peak < 1.0f * m_tp6Div) {
			m_tp6Div = steps[7];
			for (float s : steps)
				if (peak <= 3.2f * s) {
					m_tp6Div = s;
					break;
				}
		}
	}
	juce::Path path;
	const int cols = int(W * 2);
	for (int c = 0; c < cols; ++c) {
		const int a = t - pre + c * show / cols, b = std::max(a + 1, t - pre + (c + 1) * show / cols);
		float mn = m_scopeBuf[size_t(a)], mx = mn;
		for (int k = a; k < b; ++k)
			mn = std::min(mn, m_scopeBuf[size_t(k)]), mx = std::max(mx, m_scopeBuf[size_t(k)]);
		const float x = W * float(c) / float(cols);
		const float y0 = juce::jlimit(-2.0f, H + 2, yFor(mx)), y1 = juce::jlimit(-2.0f, H + 2, yFor(mn));
		if (c == 0)
			path.startNewSubPath(x, y0);
		else
			path.lineTo(x, y0);
		if (y1 - y0 > 0.5f)
			path.lineTo(x, y1);
	}
	for (auto &tr : m_traces)
		++tr.age;
	m_traces.erase(std::remove_if(m_traces.begin(), m_traces.end(), [](const Trace &tr) { return tr.age > 10; }), m_traces.end());
	m_traces.push_back({path, 0});

	// What the scope measures.
	auto periodMs = [&]() {
		if (edges.size() < 2)
			return 0.0;
		return 1e3 * double(edges.back() - edges.front()) / double(edges.size() - 1) / rate;
	};
	auto octavePair = [&](double &lowHz, double &highHz) {
		for (size_t i = m_heldNotes.size(); i-- > 0;)
			for (size_t j = i; j-- > 0;)
				if (std::abs(std::abs(m_heldNotes[i].first - m_heldNotes[j].first) - 1) < 0.01) {
					const bool iHigh = m_heldNotes[i].first > m_heldNotes[j].first;
					highHz = iHigh ? m_heldNotes[i].second : m_heldNotes[j].second;
					lowHz = iHigh ? m_heldNotes[j].second : m_heldNotes[i].second;
					return true;
				}
		return false;
	};
	double lowHz = 0, highHz = 0;
	switch (m_trim) {
	case 0:
	case 1: {
		const double ms = periodMs();
		m_scopeReading = ms > 0 ? "period " + juce::String(ms, 2) + " ms" : juce::String("no clock");
		break;
	}
	case 5:
		m_scopeReading = octavePair(lowHz, highHz) ? "C' - C  " + juce::String(m_snap.cvRef, 4) + " V" : juce::String("hold C, then C'");
		break;
	case 4:
		m_scopeReading = octavePair(lowHz, highHz) ? "C' : C  " + juce::String(highHz / lowHz, 3) + " : 1" : juce::String("play C and C'");
		break;
	case 3:
		m_scopeReading = std::abs(pr.cvVolts - 2.75) < 0.01 ? "A  " + juce::String(pr.vcoHz, 2) + " Hz" : juce::String("hold A");
		break;
	case 2: {
		// The ring after the reset: the median period between rising zero crossings of the high-passed
		// TP6, from 0.3 ms after the reset to the next one (as tools/service/check.py times it).
		const int from = t + int(3e-4 * rate);
		int to = want;
		for (int e : edges)
			if (e > t) {
				to = e;
				break;
			}
		std::vector<double> cross;
		const double a = std::exp(-2 * juce::MathConstants<double>::pi * 200 / rate);
		double y = 0, xPrev = m_scopeBuf[size_t(std::max(0, from - 1))], yPrev = 0;
		for (int k = from; k < to; ++k) {
			const double x = m_scopeBuf[size_t(k)];
			y = a * (y + x - xPrev);
			if (yPrev < 0 && y >= 0)
				cross.push_back(k - yPrev / (y - yPrev));
			xPrev = x;
			yPrev = y;
		}
		std::vector<double> periods;
		for (size_t k = 1; k < cross.size(); ++k) {
			const double p = (cross[k] - cross[k - 1]) / rate;
			if (p > 1 / 5000.0 && p < 1 / 150.0)
				periods.push_back(p);
		}
		if (periods.size() >= 2) {
			std::nth_element(periods.begin(), periods.begin() + long(periods.size() / 2), periods.end());
			m_scopeReading = "ring " + juce::String(1e3 * periods[periods.size() / 2], 2) + " ms";
		} else {
			m_scopeReading = "hold a note";
		}
		break;
	}
	default:
		break;
	}
}

void HardwareView::drawScope(juce::Graphics &g, juce::Rectangle<float> sc)
{
	const TrimDef &st = kTrims[m_trim];
	g.setColour(juce::Colour(0xff07120c));
	g.fillRoundedRectangle(sc, 4);
	g.setColour(juce::Colour(0xff0f2a1a));
	g.drawRoundedRectangle(sc, 4, 1);
	for (int i = 1; i < 10; ++i) {
		g.setColour(juce::Colour(i == 5 ? 0xff1d4433 : 0xff16382a));
		g.drawVerticalLine(juce::roundToInt(sc.getX() + sc.getWidth() * i / 10), sc.getY(), sc.getBottom());
	}
	for (int i = 1; i < 8; ++i) {
		g.setColour(juce::Colour(i == 4 ? 0xff1d4433 : 0xff16382a));
		g.drawHorizontalLine(juce::roundToInt(sc.getY() + sc.getHeight() * i / 8), sc.getX(), sc.getRight());
	}
	{
		juce::Graphics::ScopedSaveState s(g);
		g.reduceClipRegion(sc.toNearestInt());
		for (const auto &t : m_traces) {
			const float alpha = 0.95f * std::pow(1 - t.age / 11.0f, 1.5f);
			const auto at = juce::AffineTransform::translation(sc.getX(), sc.getY());
			g.setColour(juce::Colour(0xff7dffa8).withAlpha(alpha * 0.25f));
			g.strokePath(t.path, juce::PathStrokeType(3.5f), at);
			g.setColour(juce::Colour(0xffb9ffd0).withAlpha(alpha));
			g.strokePath(t.path, juce::PathStrokeType(1.3f), at);
		}
	}
	g.setFont(fonts::mono(10.0f));
	g.setColour(juce::Colour(0xff7dffa8));
	g.drawText(m_scopeReading, sc.reduced(6, 4).withHeight(14), juce::Justification::topLeft);
	g.setColour(juce::Colour(0xff4f9a6a));
	const juce::String vdiv = st.channel == TB303Processor::ScopeTp6 ? juce::String(m_tp6Div < 0.1f ? m_tp6Div * 1000 : m_tp6Div, m_tp6Div < 0.1f ? 0 : 1) +
	                                                                     (m_tp6Div < 0.1f ? " mV/div" : " V/div")
	                                                               : juce::String("2 V/div");
	g.drawText(vdiv, sc.reduced(6, 4).withHeight(14), juce::Justification::topRight);
}

void HardwareView::drawTrimmerList(juce::Graphics &g)
{
	const auto t = m_proc.trims();
	// Each trimmer's travel in spec, with the others where they are now.
	const std::array<double, 6> now = {t.tm1, t.tm2, t.tm3, t.tm4, t.tm5, t.tm6};
	if (now != m_specBandsFor) {
		m_specBandsFor = now;
		for (int i = 0; i < 6; ++i) {
			auto &bands = m_specBands[size_t(i)];
			bands.clear();
			tr::Trims u = t;
			constexpr int N = 3000;
			int start = -1;
			for (int k = 0; k <= N; ++k) {
				trimRef(u, i) = double(k) / N;
				const bool in = trimInSpec(i, u);
				if (in && start < 0)
					start = k;
				if ((!in || k == N) && start >= 0) {
					bands.push_back({float(start) / N, float(in ? k : k - 1) / N});
					start = -1;
				}
			}
		}
	}
	for (int i = 0; i < 6; ++i) {
		const auto row = trimRow(i);
		const TrimDef &d = kTrims[i];
		const bool picked = i == m_trim;
		const bool hover = (m_hover.kind == HitKind::TrimRow || m_hover.kind == HitKind::TrimSlider || m_hover.kind == HitKind::Trimmer) && m_hover.index == i;
		g.setColour(picked ? juce::Colour(0xff2b2620) : hover ? juce::Colour(0xff262a31) : juce::Colour(0xff21252b));
		g.fillRoundedRectangle(row, 5);
		if (picked) {
			g.setColour(kOrange.withAlpha(0.7f));
			g.drawRoundedRectangle(row.reduced(0.5f), 5, 1);
			g.setColour(kOrange);
			g.fillRoundedRectangle(row.withWidth(3).reduced(0, 6), 1.5f);
		}
		const bool ok = trimInSpec(i, t);
		const float v = paramValue(TB303Processor::kTrimIds[i]);
		const float def = m_proc.params().getParameter(TB303Processor::kTrimIds[i])->getDefaultValue();
		// Line 1: the name as printed, what it does, the reading.
		const float y1 = row.getY() + 4;
		g.setColour(ok ? kInSpec : kOrange);
		g.fillEllipse(row.getX() + 11, y1 + 5, 7, 7);
		g.setFont(fonts::mono(11.5f, fonts::Weight::SemiBold));
		g.setColour(juce::Colour(0xffeef1f5));
		const juce::String name = juce::String(d.ref) + (*d.silk ? juce::String(" ") + d.silk : juce::String());
		g.drawText(name, juce::Rectangle<float>(row.getX() + 24, y1, 100, 17), juce::Justification::centredLeft);
		const float nameW = g.getCurrentFont().getStringWidthFloat(name);
		g.setFont(sans(11.0f));
		g.setColour(juce::Colour(0xff8f98a5));
		g.drawText(d.plain, juce::Rectangle<float>(row.getX() + 24 + nameW + 8, y1, 110, 17), juce::Justification::centredLeft);
		g.setFont(fonts::mono(11.0f, fonts::Weight::Medium));
		g.setColour(ok ? juce::Colour(0xffe9ecf1) : juce::Colour(0xffffb866));
		g.drawText(trimValue(i, t), juce::Rectangle<float>(row.getRight() - 96, y1, 88, 17), juce::Justification::centredRight);
		// Line 2: the travel, in-spec stretches green, the factory's trim ticked, the rotor as the thumb.
		const auto track = trimTrack(i);
		g.setColour(juce::Colour(0xff3a414c));
		g.fillRoundedRectangle(track, 2);
		for (const auto &[a, b] : m_specBands[size_t(i)]) {
			float x0 = track.getX() + a * track.getWidth(), x1 = track.getX() + b * track.getWidth();
			if (x1 - x0 < 3) // a narrow window: TUNE's is a few cents
				x0 = (x0 + x1) / 2 - 1.5f, x1 = x0 + 3;
			g.setColour(kInSpec.withAlpha(0.85f));
			g.fillRoundedRectangle(juce::Rectangle<float>(x0, track.getY() - 1, x1 - x0, track.getHeight() + 2), 2);
			// And over the track, where the thumb can't hide it.
			g.fillRoundedRectangle(juce::Rectangle<float>(std::min(x0, (x0 + x1) / 2 - 3), track.getY() - 10, std::max(x1 - x0, 6.0f), 2.5f), 1.2f);
		}
		g.setColour(juce::Colour(0xffc9cfd8));
		const float fx = track.getX() + def * track.getWidth();
		g.drawLine(fx, track.getY() - 5, fx, track.getBottom() + 5, 1.2f);
		const juce::Point<float> thumb(track.getX() + v * track.getWidth(), track.getCentreY());
		drawTrimmerAt(g, thumb, 8.0f, v, d.blue);
	}
}

void HardwareView::drawInstruments(juce::Graphics &g)
{
	const TrimDef &st = kTrims[m_trim];
	auto caption = [&](juce::Rectangle<float> c, juce::Colour dot, const juce::String &text, const juce::String &right) {
		const float y = c.getY() + 8;
		g.setColour(dot);
		g.fillEllipse(c.getX() + 12, y + 2, 7, 7);
		g.setFont(fonts::mono(10.5f, fonts::Weight::SemiBold));
		g.setColour(juce::Colour(0xffc9cfd8));
		g.drawText(text, juce::Rectangle<float>(c.getX() + 25, y - 2, c.getWidth() - 37, 14), juce::Justification::centredLeft);
		g.setFont(fonts::mono(10.0f));
		g.setColour(juce::Colour(0xff8a929e));
		g.drawText(right, juce::Rectangle<float>(c.getX() + 25, y - 2, c.getWidth() - 37, 14), juce::Justification::centredRight);
	};
	const juce::String perDiv = st.msPerDiv < 1 ? juce::String(st.msPerDiv, 1) : juce::String(int(st.msPerDiv));
	const juce::String where = std::strcmp(st.probe, "S1") == 0 ? juce::String("VCO at S1") : juce::String(st.probe);
	caption(kScopeCase, kProbe, "SCOPE  on " + where, perDiv + " ms/div");
	drawScope(g, kScopeScreen);
	g.setFont(sans(10.5f));
	g.setColour(juce::Colour(0xffaab2bd));
	g.drawFittedText(juce::String(st.ref) + ": " + st.hint, juce::Rectangle<float>(kIx + 12, kScopeScreen.getBottom() + 3, kIw - 24, 30).toNearestInt(),
	                 juce::Justification::centredLeft, 2, 0.95f);

	const auto &pr = m_snap.probe;
	auto readout = [&](juce::Rectangle<float> c, juce::Colour bg, juce::Colour fg, const juce::String &digits, const juce::String &unit) {
		const juce::Rectangle<float> win(c.getX() + 10, c.getY() + 22, c.getWidth() - 20, 27);
		g.setColour(bg);
		g.fillRoundedRectangle(win, 3);
		g.setColour(fg);
		g.setFont(fonts::mono(18.0f, fonts::Weight::SemiBold));
		g.drawText(digits, win.withTrimmedRight(26), juce::Justification::centredRight);
		g.setFont(fonts::mono(11.0f, fonts::Weight::Medium));
		g.drawText(unit, win.withTrimmedRight(6), juce::Justification::centredRight);
	};
	caption(kCounterCase, kProbe, "COUNTER", "VCO");
	readout(kCounterCase, juce::Colour(0xff160707), juce::Colour(0xffff3b2f), pr.vcoHz > 0 ? juce::String(pr.vcoHz, 2) : juce::String("----"), "Hz");
	caption(kMeterCase, kMeterProbe, "METER", "CV OUT");
	readout(kMeterCase, juce::Colour(0xffb7c0a2), juce::Colour(0xff1d2218), juce::String(pr.cvVolts * m_snap.cvRef, 3), "V");
	drawTrimmerList(g);
}

void HardwareView::paintService(juce::Graphics &g)
{
	const float phys = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
	if (redrawCache(m_serviceStatic, m_serviceStaticScale, phys)) {
		m_serviceStaticScale = phys;
		m_serviceStatic = juce::Image(juce::Image::RGB, juce::roundToInt(kWidth * phys), juce::roundToInt(kPanelHeight * phys), true);
		juce::Graphics sg(m_serviceStatic);
		sg.addTransform(juce::AffineTransform::scale(phys));
		renderServiceStatic(sg);
	}
	g.drawImageTransformed(m_serviceStatic, juce::AffineTransform::scale(1.0f / m_serviceStaticScale));
	drawServiceParts(g);
	drawInstruments(g);
}

// ====================================================================== self-test

juce::StringArray HardwareView::serviceSelfTest()
{
	juce::StringArray out;
	auto check = [&](bool ok, const juce::String &what) { out.add((ok ? "ok    " : "FAIL  ") + what); };
	auto src = juce::Desktop::getInstance().getMainMouseSource();
	auto ev = [&](juce::Point<float> at, juce::Point<float> down, int clicks = 1, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier) {
		const auto now = juce::Time::getCurrentTime();
		return juce::MouseEvent(src, at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, this, this, now, down, now, clicks, at != down);
	};
	auto click = [&](juce::Point<float> at) {
		mouseMove(ev(at, at, 0, {}));
		mouseDown(ev(at, at));
		mouseUp(ev(at, at));
	};
	auto drag = [&](juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier) {
		mouseDown(ev(from, from, 1, mods));
		mouseDrag(ev(to, from, 1, mods));
		mouseUp(ev(to, from, 1, mods));
	};
	auto value = [&](const char *id) { return paramValue(id); };
	auto def = [&](const char *id) { return m_proc.params().getParameter(id)->getDefaultValue(); };
	auto approx = [](float a, float b, float tol = 1e-4f) { return std::abs(a - b) < tol; };

	click(barRect(BarItem::Service).getCentre());
	check(m_service && bool(m_proc.params().state.getProperty("service")), "SERVICE on the bar takes the panel off");

	// Every trimmer is on the main board, its designator printed there.
	{
		bool found = true;
		for (const auto &d : kTrims)
			found = found && findPart(d.ref) != nullptr;
		check(found, "TM1-TM6 are on the digitised main board");
	}

	// Each control: clicking its row picks the trimmer, dragging its track turns it, the button resets it.
	for (int i = 0; i < 6; ++i) {
		const char *id = TB303Processor::kTrimIds[i];
		click(trimRow(i).getTopLeft() + juce::Point<float>(60, 8));
		const bool picked = m_trim == i && int(m_proc.params().state.getProperty("serviceTrim")) == i;
		const auto track = trimTrack(i);
		const juce::Point<float> thumb(track.getX() + value(id) * track.getWidth(), track.getCentreY());
		drag(thumb, thumb + juce::Point<float>(track.getWidth() * 0.1f, 0));
		const float turned = value(id);
		const auto at = track.getCentre();
		mouseDoubleClick(ev(at, at, 2));
		check(picked && approx(turned, juce::jmin(1.0f, def(id) + 0.1f), 2e-3f) && approx(value(id), def(id)),
		      juce::String(kTrims[i].ref) + ": picked from its row, its control turned " + juce::String(def(id), 3) + " -> " + juce::String(turned, 3) +
		          ", a double-click resets it");
	}
	// Shift turns it a tenth as far; a click on the track only picks it.
	{
		const auto track = trimTrack(4);
		const juce::Point<float> thumb(track.getX() + value("tm5") * track.getWidth(), track.getCentreY());
		drag(thumb, thumb + juce::Point<float>(40, 0), juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
		const float fine = value("tm5") - def("tm5");
		resetTrim(4);
		check(approx(fine, 0.1f * 40 / track.getWidth(), 1e-3f), "shift-drag on a control turns it a tenth as far");
		click(trimRow(0).getCentre());
		click({track.getX() + track.getWidth() * 0.9f, track.getCentreY()});
		check(m_trim == 4 && approx(value("tm5"), def("tm5")), "a click on a control's track picks it without turning it");
		resetTrim(4);
	}
	// On the board: dragging a trimmer turns it and picks it; the keyboard picks and turns.
	{
		const auto c = trimmerPos(2);
		drag(c, c - juce::Point<float>(0, 40));
		check(m_trim == 2 && approx(value("tm3"), juce::jmin(1.0f, def("tm3") + 40 * 0.002f)), "TM3 dragged on the board turns, and is picked");
		mouseDoubleClick(ev(c, c, 2));
		check(approx(value("tm3"), def("tm3")), "double-click on the board resets it");
		keyPressed(juce::KeyPress(juce::KeyPress::downKey));
		keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
		check(m_trim == 3 && approx(value("tm4"), def("tm4") + 0.002f), "down picks TM4, right turns it");
		resetTrim(3);
	}

	// FACTORY TRIM puts every trimmer back.
	for (const char *id : TB303Processor::kTrimIds)
		setParamValue(id, 0.9f);
	click(barRect(BarItem::Factory).getCentre());
	bool factory = true;
	for (const char *id : TB303Processor::kTrimIds)
		factory = factory && approx(value(id), def(id));
	check(factory, "FACTORY TRIM puts all six back");

	// Pots, switches, S1 and the keys work on the boards.
	{
		const float cutoff = value("cutoff");
		const auto c = servicePos(m_knobs[1].element);
		drag(c, c - juce::Point<float>(0, 30));
		check(value("cutoff") > cutoff + 0.1f, "CUT OFF's knob drags on its pot");
		setParamValue("cutoff", cutoff);
		const float track = value(TB303Processor::kTrackId);
		click(servicePos(m_switches[0].element));
		check(!approx(value(TB303Processor::kTrackId), track), "PATT.GROUP's switch clicks round");
		setParamValue(TB303Processor::kTrackId, track);
		const float wave = value("waveform");
		click(waveformPos());
		check(!approx(value("waveform"), wave), "S1 switches the waveform");
		setParamValue("waveform", wave);
		const int k = 12; // C'
		const auto kc = serviceKey(k).getCentre();
		mouseDown(ev(kc, kc));
		const bool down = m_keyDown[size_t(k)];
		mouseUp(ev(kc, kc));
		check(down && !m_keyDown[size_t(k)], "a key switch on the switch board presses and releases");
	}

	// Nothing on the boards reaches under the instruments.
	{
		bool clear = true;
		for (const auto &p : board::parts)
			clear = clear && P(centreMm(p)).x < kIx;
		check(clear && kTrimPanel.getBottom() <= float(kPanelHeight), "parts on their boards, boards clear of the instruments");
	}

	click(barRect(BarItem::Service).getCentre());
	check(!m_service, "SERVICE again puts the panel back");
	return out;
}
