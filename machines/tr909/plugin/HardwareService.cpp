// SERVICE on the plugin bar: the TR-909 with its top panel off, the boards' component sides up, as
// the 303's SERVICE shows that machine. The voicing board sits where it hangs under the panel, the
// switch board folded out in front of it on its harness, and every part is where the service notes'
// board drawings put it (p.9, p.10, read off into docs/boards.json; docs/boards.md), drawn as the part
// it is: resistors banded by their netlist values, film, ceramic, tantalum and electrolytic
// capacitors, glass diodes, TO-92 transistors, the ICs with their markings. The knobs stay on their
// pots and the keys on their switches, as they are when the panel comes off (drawn and played as on
// the panel), the LEDs and the display lit by the firmware. The boards are single-sided: their copper
// shows through faintly, generated from the pads (makeCopper), not Roland's tracks. The transformer
// stands in the voicing board's cut-out.
//
// On the right, as the 303's: the scope with the notes' procedure, two readouts, the clap's trimmer
// TM (the TR-909's only one, on the board and as a control) and the unit's circuit revision by serial
// number (SN p.7's change table). The view zooms (the wheel, a pinch; double-click for the whole)
// and pans (drag).

#include "HardwareView.h"
#include "Fonts.h"

#include "tr909/core/circuit.h"

#include <TR909Boards.h>

#include <cmath>
#include <map>
#include <tuple>

namespace tr909 {

namespace {
constexpr float kPi = juce::MathConstants<float>::pi;

// ------------------------------------------------------------------ layout
constexpr float kDrawerW = 304;
constexpr float kCanvasW = float(HardwareView::kWidth) - kDrawerW, kCanvasH = float(HardwareView::kPanelHeight);
constexpr float kSwitchDy = 110;  // mm: the switch board folded out in front of the voicing board
constexpr float kMaxZoom = 8;
const juce::Rectangle<float> kContent(26, 6, 420, 382); // panel mm the view fits at zoom 1
const float kFit = std::min(kCanvasW / kContent.getWidth(), kCanvasH / kContent.getHeight());
const juce::Point<float> kOrigin((kCanvasW - kContent.getWidth() * kFit) / 2, (kCanvasH - kContent.getHeight() * kFit) / 2);

// The right-hand side, view px.
constexpr float kIx = kCanvasW + 8, kIw = kDrawerW - 16;
const juce::Rectangle<float> kScopeCase(kIx, 8, kIw, 192), kScopeScreen(kIx + 12, 32, kIw - 24, 118);
const juce::Rectangle<float> kVppCase(kIx, 208, 140, 56), kLevelCase(kIx + kIw - 140, 208, 140, 56);
const juce::Rectangle<float> kTrimCase(kIx, 272, kIw, 86);
const juce::Rectangle<float> kRevCase(kIx, 366, kIw, 325);
constexpr float kRevRowH = 47;
juce::Rectangle<float> trimRow() { return {kTrimCase.getX() + 6, kTrimCase.getY() + 30, kTrimCase.getWidth() - 12, 50}; }
juce::Rectangle<float> trimTrack() { return {trimRow().getX() + 30, trimRow().getY() + 32, trimRow().getWidth() - 60, 4}; }
juce::Rectangle<float> testPill() { return {kTrimCase.getRight() - 82, kTrimCase.getY() + 7, 72, 18}; }
juce::Rectangle<float> revRow(int i) { return {kRevCase.getX() + 6, kRevCase.getY() + 30 + float(i) * kRevRowH, kRevCase.getWidth() - 12, kRevRowH - 4}; }
// the scope's own controls: its caption (the probe), the time base, the volts, the trigger
juce::Rectangle<float> scopeHot(int i)
{
	switch (i) {
	case 0: return {kScopeCase.getX() + 8, kScopeCase.getY() + 4, kScopeCase.getWidth() - 100, 20};
	case 2: return {kScopeCase.getRight() - 88, kScopeCase.getY() + 4, 80, 20};
	case 3: return {kScopeScreen.getRight() - 70, kScopeScreen.getY() + 2, 66, 16};
	case 5: return {kScopeScreen.getX() + 4, kScopeScreen.getY() + 2, 90, 16};
	default: return {};
	}
}

const juce::Colour kOrange(0xffff8c1a), kProbe(0xfff5d742), kSilk(0xffe6ece6), kLead(0xffc9cdd2);
const juce::Colour kMask(0xff0f6247), kMaskEdge(0xff0a4732), kCopperThrough(0xff0c5640);

juce::Font sans(float px, bool bold = false) { return juce::Font(juce::FontOptions("Helvetica Neue", px, bold ? juce::Font::bold : juce::Font::plain)); }

uint32_t hashOf(const juce::String &s)
{
	uint32_t h = 2166136261u;
	for (auto c : s)
		h = (h ^ uint32_t(c)) * 16777619u;
	return h;
}

// ------------------------------------------------------------------ the boards (docs/boards.json)
enum class Kind { R, C, D, Q, Dual, Ic, Sip, Ra, Vr, Tm, Cn, Sw, Led, Display, Xtal, Other };

struct Board {
	juce::String id, name, number;
	juce::Path outline; // panel mm, as drawn (the switch board folded out)
	float dy = 0;       // where it's drawn: its own place, moved forward by this
	juce::Path lands;     // the other side's copper, generated (makeCopper): the pads' lands
	juce::Path tracks[3]; // and its tracks, by width (kTrackWidths)
};

struct Part {
	juce::String ref, value;          // value: the netlists' ("22k", "0.47u"), "" when they don't draw it
	Kind kind = Kind::Other;
	int board = 0;
	std::vector<juce::Point<float>> pads; // panel mm, as drawn; pin 1 first when it's known
	bool pin1 = false;
	std::vector<int> netlist;         // VoiceBoard::parts entries (one, or an IC's sections)
};

struct Boards {
	std::vector<Board> boards; // voicing, switch: the two drawn
	std::vector<Part> parts;
	juce::Rectangle<float> transformer;
};

Kind kindOf(const juce::String &ref)
{
	const auto alpha = ref.initialSectionNotContaining("0123456789");
	const int n = ref.retainCharacters("0123456789").getIntValue();
	if (alpha == "R") return Kind::R;
	if (alpha == "C") return Kind::C;
	if (alpha == "D") return n >= 601 && n <= 635 ? Kind::Led : n == 636 ? Kind::Display : Kind::D;
	if (alpha == "Q") return Kind::Q;
	if (alpha == "IC") return Kind::Ic;
	if (alpha == "RA") return Kind::Ra;
	if (alpha == "VR") return Kind::Vr;
	if (alpha == "TM") return Kind::Tm;
	if (alpha == "CN" || alpha == "CN-" || alpha == "F") return Kind::Cn;
	if (alpha == "SW") return Kind::Sw;
	if (alpha == "XTAL") return Kind::Xtal;
	return Kind::Other;
}

// A board's copper, seen from the component side: a land at every pad of boards.json (round, oval
// along x or y as its kind says), and tracks from each pad to its nearest neighbours of other parts
// (a spanning forest, shortest first), routed at 0/45/90 degrees as a board's traces run. Drawn as
// the 303's is, not copied: the real tracks are Roland's artwork and stay out of Lemus.
constexpr float kTrackWidths[3] = {0.5f, 0.65f, 0.8f}; // mm

void makeCopper(Boards &all, int board, const juce::var &pads)
{
	struct Hole {
		juce::Point<float> at;
		int part; // the part whose pad it is; a pad no part claims is its own (-1 - its index)
		int kind;
	};
	const float dy = all.boards[size_t(board)].dy;
	std::vector<Hole> holes;
	std::vector<std::pair<juce::Point<float>, int>> owned; // the parts' pads
	for (size_t i = 0; i < all.parts.size(); ++i)
		if (all.parts[i].board == board)
			for (const auto &q : all.parts[i].pads)
				owned.push_back({q, int(i)});
	if (const auto *list = pads.getArray())
		for (int k = 0; k < list->size(); ++k) {
			const auto &v = (*list)[k];
			const juce::Point<float> at(float(double(v[0])), float(double(v[1])) + dy);
			int part = -1 - k;
			float best = 0.6f; // mm: a part's pad and the drawing's are the same hole
			for (const auto &[q, i] : owned)
				if (const float d = q.getDistanceFrom(at); d < best)
					best = d, part = i;
			holes.push_back({at, part, int(v[2])});
		}

	auto &bd = all.boards[size_t(board)];
	for (const auto &h : holes) {
		const float w = h.kind == 1 ? 2.2f : 1.8f, ht = h.kind == 2 ? 2.2f : 1.8f;
		bd.lands.addEllipse(h.at.x - w / 2, h.at.y - ht / 2, w, ht);
	}

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
			if (b == a || holes[size_t(b)].part == holes[size_t(a)].part)
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
	std::sort(edges.begin(), edges.end(), [](const Edge &x, const Edge &y) { return std::tie(x.d, x.a, x.b) < std::tie(y.d, y.a, y.b); });
	std::vector<int> parent(static_cast<size_t>(m));
	for (int i = 0; i < m; ++i)
		parent[size_t(i)] = i;
	auto root = [&](int x) {
		while (parent[size_t(x)] != x)
			x = parent[size_t(x)] = parent[size_t(parent[size_t(x)])];
		return x;
	};
	uint32_t seed = 909u + uint32_t(board);
	auto next = [&] { return float((seed = seed * 1664525u + 1013904223u) >> 8) / float(1u << 24); };
	for (const auto &e : edges) {
		const int ra = root(e.a), rb = root(e.b);
		if (ra == rb)
			continue;
		parent[size_t(ra)] = rb;
		const auto a = holes[size_t(e.a)].at, b = holes[size_t(e.b)].at;
		const float dx = b.x - a.x, dy2 = b.y - a.y, diag = std::min(std::abs(dx), std::abs(dy2));
		const juce::Point<float> step(dx >= 0 ? diag : -diag, dy2 >= 0 ? diag : -diag);
		juce::Path t;
		t.startNewSubPath(a);
		t.lineTo(next() < 0.5f ? a + step : b - step);
		t.lineTo(b);
		bd.tracks[int(next() * 3) % 3].addPath(t);
	}
}

const Boards &boards()
{
	static const Boards b = [] {
		Boards out;
		const auto json = juce::JSON::parse(juce::String::fromUTF8(TR909Boards::boards_json, TR909Boards::boards_jsonSize));
		std::map<juce::String, int> index;
		if (const auto *list = json["boards"].getArray())
			for (const auto &v : *list) {
				const auto id = v["id"].toString();
				if (id != "voicing" && id != "switch")
					continue; // the jack boards and the power supply lie under the voicing board
				Board bd;
				bd.id = id;
				bd.name = v["name"].toString();
				bd.number = v["number"].toString();
				bd.dy = id == "switch" ? kSwitchDy : 0.0f;
				if (const auto *pts = v["outline"].getArray())
					for (int i = 0; i < pts->size(); ++i) {
						const auto p = juce::Point<float>{float(double((*pts)[i][0])), float(double((*pts)[i][1])) + bd.dy};
						if (i == 0)
							bd.outline.startNewSubPath(p);
						else
							bd.outline.lineTo(p);
					}
				bd.outline.closeSubPath();
				index[id] = int(out.boards.size());
				out.boards.push_back(bd);
			}
		const auto &net = VoiceBoard::parts();
		if (const auto *list = json["parts"].getArray())
			for (const auto &v : *list) {
				const auto it = index.find(v["board"].toString());
				if (it == index.end())
					continue;
				Part p;
				p.ref = v["ref"].toString();
				p.kind = kindOf(p.ref);
				p.board = it->second;
				p.pin1 = bool(v["pin1"]);
				const float dy = out.boards[size_t(p.board)].dy;
				if (const auto *pads = v["pads"].getArray())
					for (const auto &q : *pads)
						p.pads.push_back({float(double(q[0])), float(double(q[1])) + dy});
				if (p.pads.empty())
					continue;
				for (size_t k = 0; k < net.size(); ++k) {
					const juce::String r(net[k].ref);
					const bool section = p.ref.startsWith("IC") && r.length() == p.ref.length() + 1 && r.startsWith(p.ref)
					                     && juce::CharacterFunctions::isLetter(r.getLastCharacter());
					if (r == p.ref || section) {
						bool dup = false;
						for (int q : p.netlist)
							dup = dup || juce::String(net[size_t(q)].ref) == r;
						if (!dup)
							p.netlist.push_back(int(k));
					}
				}
				if (!p.netlist.empty())
					p.value = net[size_t(p.netlist[0])].value;
				if (p.kind == Kind::Q && p.pads.size() > 3)
					p.kind = Kind::Dual;
				out.parts.push_back(p);
			}
		const auto t = json["transformer"]["box"];
		out.transformer = {juce::Point<float>(float(double(t[0])), float(double(t[1]))), juce::Point<float>(float(double(t[2])), float(double(t[3])))};
		for (size_t i = 0; i < out.boards.size(); ++i)
			makeCopper(out, int(i), json["pads"][out.boards[i].id.toRawUTF8()]);
		return out;
	}();
	return b;
}

// What the parts the netlists don't draw are (SN p.2, p.3, p.5, p.8, p.11; docs/circuit/interface.md).
juce::String describe(const juce::String &ref)
{
	const int n = ref.retainCharacters("0123456789").getIntValue();
	if (ref == "VR1")
		return "TOTAL ACCENT, 50k(B), to the CPU's AN3";
	if (ref.startsWith("RA"))
		return n <= 8 ? "RM-0621 resistor array: an accent DAC (5 V x code / 63)" : "resistor array";
	if (ref.startsWith("IC")) {
		static const std::pair<int, const char *> chips[] = {
			{1, "HD14174BP hex D latch: the trigger lines"}, {10, "HD14174BP hex D latch: the trigger lines"},
			{31, "HD14070BP quad XOR: the noise generator's feedback"}, {32, "HD14006BP 18-bit shift register: the noise generator"},
			{33, "HD14006BP 18-bit shift register: the noise generator"}, {54, "HN61256P C44 mask ROM: the ride's samples"},
			{62, "HN61256P C42 mask ROM: the crash's samples"}, {69, "HN61256P C43 mask ROM: the hi-hat's samples"},
			{67, "HD14174BP hex D latch: the hi-hat's D/A"}, {68, "HD14174BP hex D latch: the hi-hat's D/A"},
			{70, "TC4520BP dual counter: the hi-hat's address"}, {71, "HD14040BP 12-bit counter: the hi-hat's address"},
			{72, "TC4011UBP quad NAND: the hi-hat's clock"}, {73, "TC4013BP dual D flip-flop: the hi-hat's run line"},
			{601, "HD14511BP BCD to 7 segments: the display"}, {602, "M54516P transistor array: the LEDs"},
			{603, "M54562P transistor array: the LEDs"}, {604, "uPD7811G-033-036 CPU (12 MHz)"}, {605, "PC900 photocoupler: MIDI IN"},
			{606, "HD74LS138P 3 to 8 decoder"}, {607, "HD74LS00P quad NAND"}, {608, "TC5565PL-15 8 KB static RAM (the memory)"},
			{609, "M5M2764 program ROM"}, {610, "SN74LS244N octal buffer: the key scan"}, {611, "HD74LS373 octal latch: the address"},
			{612, "HD74LS138P 3 to 8 decoder"}, {613, "HD74LS138P 3 to 8 decoder"}, {614, "HD74LS367AP hex bus driver"},
			{615, "HD74LS367AP hex bus driver"}, {616, "HD74LS367AP hex bus driver"}, {617, "SN74LS245N bus transceiver: the cartridge"}};
		for (const auto &[k, d] : chips)
			if (k == n)
				return d;
		if (n >= 2 && n <= 9)
			return "HD14174BP hex D latch: an accent DAC's code";
	}
	return {};
}

// An IC's marking: the netlist's kind of section, else the parts list's.
juce::String marking(const Part &p)
{
	if (!p.netlist.empty()) {
		const auto &v = VoiceBoard::parts()[size_t(p.netlist[0])].value;
		if (v == "op-amp") return "M5218L";
		if (v == "comparator") return "AN6912";
		if (v == "OTA") return "BA662A";
		if (v == "CMOS gate") {
			bool nand = false;
			for (int k : p.netlist)
				nand = nand || VoiceBoard::parts()[size_t(k)].pins.size() > 2;
			return nand ? "TC4011UBP" : "TC4069UBP";
		}
	}
	const auto d = describe(p.ref);
	return d.upToFirstOccurrenceOf(" ", false, false);
}

// ------------------------------------------------------------------ part drawing (mm, g transformed)
// A shape with a soft shadow under it toward the front-right (light from the rear-left).
void shadowed(juce::Graphics &g, const juce::Path &p, float lift = 1.0f)
{
	g.setColour(juce::Colours::black.withAlpha(0.28f));
	g.fillPath(p, juce::AffineTransform::translation(0.16f * lift + 0.08f, 0.32f * lift + 0.12f));
	g.setColour(juce::Colours::black.withAlpha(0.16f));
	g.fillPath(p, juce::AffineTransform::translation(0.32f * lift + 0.15f, 0.62f * lift + 0.2f));
}

void lead(juce::Graphics &g, juce::Point<float> a, juce::Point<float> b, float w = 0.5f)
{
	g.setColour(juce::Colour(0xff8d9298));
	g.drawLine(juce::Line<float>(a, b), w + 0.22f);
	g.setColour(kLead);
	g.drawLine(juce::Line<float>(a, b), w);
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

double valueOf(const juce::String &v)
{
	try {
		return v.isEmpty() || v.containsChar('{') ? -1 : circuit::parseValue(v.toStdString());
	} catch (...) {
		return -1;
	}
}

// Resistor colour codes from the netlist's value (E12/E24, gold tolerance); a part the netlists
// don't draw gets a code from its designator.
const juce::uint32 kBand[10] = {0xff1b1b1b, 0xff7a4a22, 0xffd8352a, 0xffee8a22, 0xfff0d23a, 0xff3e9a42, 0xff3159c4, 0xff8a4fc0, 0xff8f8f8f, 0xfff4f4f4};
void bandsFor(const Part &p, int out[3])
{
	double ohms = valueOf(p.value);
	if (ohms < 1) {
		static const int e12[12] = {10, 12, 15, 18, 22, 27, 33, 39, 47, 56, 68, 82};
		const uint32_t h = hashOf(p.ref);
		ohms = e12[h % 12] * std::pow(10.0, 1 + (h / 12) % 5);
	}
	int exp = int(std::floor(std::log10(ohms))) - 1;
	int two = int(std::lround(ohms / std::pow(10.0, exp)));
	if (two >= 100) {
		two /= 10;
		++exp;
	}
	out[0] = (two / 10) % 10, out[1] = two % 10, out[2] = std::clamp(exp, 0, 9);
}

void drawResistor(juce::Graphics &g, const Part &p)
{
	const auto a = p.pads[0], b = p.pads[1];
	const float d = a.getDistanceFrom(b);
	int bands[3];
	bandsFor(p, bands);
	const juce::Colour body(0xffe9d5ae);
	if (d < 6.2f) {
		// standing: the body upright over one pad, its top lead bent over to the other
		const float r = 1.25f;
		juce::Path loop;
		const auto pa = a + (b - a) * 0.15f;
		const auto mid = (pa + b) * 0.5f + juce::Point<float>(-(b - pa).y, (b - pa).x) * 0.25f;
		loop.startNewSubPath(pa);
		loop.quadraticTo(mid, b);
		g.setColour(juce::Colour(0xff8d9298));
		g.strokePath(loop, juce::PathStrokeType(0.72f));
		g.setColour(kLead);
		g.strokePath(loop, juce::PathStrokeType(0.5f));
		juce::Path disc;
		disc.addEllipse(a.x - r, a.y - r, 2 * r, 2 * r);
		shadowed(g, disc, 2.5f);
		g.setGradientFill(juce::ColourGradient(body.brighter(0.3f), a.x - r * 0.5f, a.y - r * 0.6f, body.darker(0.35f), a.x + r, a.y + r, true));
		g.fillPath(disc);
		g.setColour(juce::Colour(kBand[bands[0]]).withAlpha(0.9f));
		g.drawEllipse(a.x - r * 0.72f, a.y - r * 0.72f, r * 1.44f, r * 1.44f, 0.28f);
		return;
	}
	// lying flat: a 6.3 mm body between bent leads
	const auto dir = (b - a) / d;
	const float len = std::min(6.3f, d - 1.4f), hw = 1.2f;
	const auto m = (a + b) * 0.5f;
	const auto e0 = m - dir * (len / 2), e1 = m + dir * (len / 2);
	lead(g, a, e0);
	lead(g, e1, b);
	const auto t = juce::AffineTransform::rotation(std::atan2(dir.y, dir.x)).translated(m);
	juce::Path shape;
	shape.addRoundedRectangle(-len / 2, -hw, len, 2 * hw, hw);
	shape.addEllipse(-len / 2, -(hw + 0.12f), 1.9f, 2 * hw + 0.24f);
	shape.addEllipse(len / 2 - 1.9f, -(hw + 0.12f), 1.9f, 2 * hw + 0.24f);
	shape.applyTransform(t);
	shadowed(g, shape, 1.2f);
	g.setGradientFill(across(body, e0, e1, hw + 0.1f));
	g.fillPath(shape);
	const float xs[4] = {-len * 0.34f, -len * 0.21f, -len * 0.08f, len * 0.26f};
	for (int i = 0; i < 4; ++i) {
		juce::Path band;
		band.addRectangle(xs[i], -(hw - 0.05f), 0.5f, 2 * hw - 0.1f);
		band.applyTransform(t);
		const juce::Colour bc = i == 3 ? juce::Colour(0xffc9a24a) : juce::Colour(kBand[bands[i]]);
		g.setGradientFill(across(bc, e0, e1, hw));
		g.fillPath(band);
	}
}

void drawDiode(juce::Graphics &g, const Part &p)
{
	auto k = p.pads[0], a = p.pads[1];
	const float d = k.getDistanceFrom(a);
	const juce::Colour glass(0xffcf4b2a);
	if (d < 5.2f) {
		const float r = 0.95f;
		lead(g, a + (k - a) * 0.2f, k, 0.42f);
		juce::Path disc;
		disc.addEllipse(a.x - r, a.y - r, 2 * r, 2 * r);
		shadowed(g, disc, 2.0f);
		g.setGradientFill(juce::ColourGradient(glass.brighter(0.6f), a.x - r * 0.5f, a.y - r * 0.5f, glass.darker(0.4f), a.x + r, a.y + r, true));
		g.fillPath(disc);
		g.setColour(juce::Colour(0xff151515));
		g.drawEllipse(a.x - r * 0.75f, a.y - r * 0.75f, r * 1.5f, r * 1.5f, 0.25f);
		return;
	}
	const auto dir = (a - k) / d;
	const float len = 3.6f, hw = 0.85f;
	const auto m = (a + k) * 0.5f;
	lead(g, k, m - dir * (len / 2), 0.42f);
	lead(g, m + dir * (len / 2), a, 0.42f);
	const auto t = juce::AffineTransform::rotation(std::atan2(dir.y, dir.x)).translated(m);
	juce::Path body;
	body.addRoundedRectangle(-len / 2, -hw, len, 2 * hw, hw * 0.8f);
	body.applyTransform(t);
	shadowed(g, body, 0.9f);
	g.setGradientFill(across(glass, m - dir, m + dir, hw));
	g.fillPath(body);
	juce::Path band;
	band.addRectangle(-(len / 2 - 0.35f), -hw, 0.55f, 2 * hw);
	band.applyTransform(t);
	g.setColour(juce::Colour(0xff18181a));
	g.fillPath(band);
	juce::Path glint;
	glint.addRoundedRectangle(-0.9f, -hw * 0.65f, 1.9f, 0.35f, 0.15f);
	glint.applyTransform(t);
	g.setColour(juce::Colours::white.withAlpha(0.45f));
	g.fillPath(glint);
}

// The tantalums the netlists give dielectric absorption (lt, mt, ht, bd: docs/model.md).
bool tantalum(const juce::String &ref)
{
	for (const char *t : {"C8", "C9", "C21", "C22", "C23", "C35", "C36", "C37", "C98", "C99", "C100"})
		if (ref == t)
			return true;
	return false;
}

void drawCapacitor(juce::Graphics &g, const Part &p)
{
	const auto a = p.pads[0], b = p.pads.size() > 1 ? p.pads[1] : p.pads[0] + juce::Point<float>(2.5f, 0);
	const float d = std::max(2.0f, a.getDistanceFrom(b));
	const auto dir = (b - a) / d;
	const auto m = (a + b) * 0.5f;
	const auto t = juce::AffineTransform::rotation(std::atan2(dir.y, dir.x)).translated(m);
	const double f = valueOf(p.value);
	const bool electrolytic = f >= 1e-6 || p.ref == "C65" || p.ref == "C57" || (f < 0 && p.value.isEmpty() && d < 3.6f);
	if (tantalum(p.ref)) {
		// a dipped tantalum bead, its + marked
		juce::Path bead;
		bead.addEllipse(-1.9f, -1.6f, 3.8f, 3.6f);
		bead.applyTransform(t);
		shadowed(g, bead, 2.0f);
		const juce::Colour c(0xffd9a514);
		g.setGradientFill(juce::ColourGradient(c.brighter(0.5f), m.x - 1.2f, m.y - 1.2f, c.darker(0.45f), m.x + 1.8f, m.y + 1.8f, true));
		g.fillPath(bead);
		g.setColour(juce::Colour(0xff4a3608));
		g.drawLine(juce::Line<float>(m - dir * 1.0f + juce::Point<float>(0, -0.45f), m - dir * 1.0f + juce::Point<float>(0, 0.45f)), 0.25f);
		return;
	}
	if (electrolytic) {
		// a radial can from above: sleeve, minus stripe, the aluminium top with its vent
		const float dia = f >= 1e-3 ? 12.5f : f >= 4.7e-4 ? 10.0f : f >= 1e-4 ? 8.0f : f >= 3.3e-5 ? 6.3f : 5.0f;
		const float r = dia / 2;
		juce::Path sleeve;
		sleeve.addEllipse(m.x - r, m.y - r, 2 * r, 2 * r);
		shadowed(g, sleeve, 3.0f);
		const juce::Colour sl(0xff1c1e23);
		g.setGradientFill(juce::ColourGradient(sl.brighter(0.5f), m.x - r, m.y - r, sl.darker(0.3f), m.x + r, m.y + r, false));
		g.fillPath(sleeve);
		const float stripeAt = std::atan2(dir.x, -dir.y);
		juce::Path stripe;
		stripe.addCentredArc(m.x, m.y, r * 0.93f, r * 0.93f, 0, stripeAt - 0.55f, stripeAt + 0.55f, true);
		g.setColour(juce::Colour(0xffb9c0c9).withAlpha(0.85f));
		g.strokePath(stripe, juce::PathStrokeType(r * 0.12f));
		const float rt = r * 0.8f;
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe3e6ea), m.x - rt * 0.6f, m.y - rt * 0.6f, juce::Colour(0xff8f959d), m.x + rt, m.y + rt, true));
		g.fillEllipse(m.x - rt, m.y - rt, 2 * rt, 2 * rt);
		g.setColour(juce::Colour(0xff6f757d));
		const float v = rt * 0.5f, w = std::max(0.12f, rt * 0.08f);
		g.drawLine(m.x, m.y - v, m.x, m.y + v, w);
		g.drawLine(m.x, m.y, m.x + v * 0.85f, m.y - v * 0.6f, w);
		g.drawLine(m.x, m.y, m.x + v * 0.85f, m.y + v * 0.6f, w);
		return;
	}
	if (f > 0 && f < 1e-9) {
		// a ceramic disc, edge on
		juce::Path disc;
		disc.addRoundedRectangle(-2.0f, -1.0f, 4.0f, 2.0f, 0.9f);
		disc.applyTransform(t);
		shadowed(g, disc, 1.2f);
		g.setGradientFill(across(juce::Colour(0xffc98a3e), m - dir, m + dir, 1.0f));
		g.fillPath(disc);
		return;
	}
	// a dipped film capacitor: a glossy lozenge
	const float len = std::clamp(d + 1.6f, 4.5f, 7.4f), hw = 1.6f;
	juce::Path body;
	body.addRoundedRectangle(-len / 2, -hw, len, 2 * hw, hw * 0.95f);
	body.applyTransform(t);
	shadowed(g, body, 1.6f);
	const juce::Colour green(0xff2b8152);
	g.setGradientFill(across(green, m - dir, m + dir, hw));
	g.fillPath(body);
	juce::Path glint;
	glint.addRoundedRectangle(-(len / 2 - 1.1f), -hw * 0.7f, len - 2.8f, 0.5f, 0.25f);
	glint.applyTransform(t);
	g.setColour(juce::Colours::white.withAlpha(0.32f));
	g.fillPath(glint);
}

// A TO-92 from above: a D over its three leads, the flat along their line.
void drawTransistor(juce::Graphics &g, const Part &p)
{
	juce::Point<float> c;
	for (const auto &q : p.pads)
		c += q;
	c /= float(p.pads.size());
	const auto line = p.pads.size() >= 2 ? p.pads.back() - p.pads.front() : juce::Point<float>(1, 0);
	const float rot = std::atan2(line.y, line.x);
	const float r = 2.35f;
	juce::Path d;
	d.startNewSubPath(-r, 0.55f);
	d.lineTo(r, 0.55f);
	d.addCentredArc(0, 0.55f, r, 3.1f, 0, kPi / 2, kPi * 1.5f, false);
	d.closeSubPath();
	const auto t = juce::AffineTransform::rotation(rot).translated(c);
	d.applyTransform(t);
	shadowed(g, d, 2.2f);
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4c52), c.x - r, c.y - r, juce::Colour(0xff141518), c.x + r, c.y + r, false));
	g.fillPath(d);
	juce::Path flat;
	flat.addRectangle(-r * 0.92f, 0.1f, r * 1.84f, 0.45f);
	flat.applyTransform(t);
	g.setColour(juce::Colour(0xff5d6067));
	g.fillPath(flat);
}

// The pads' long axis and spread across it: a DIP's two rows (or a QUIP's staggered pairs), or a
// single row.
struct Rows {
	juce::Point<float> c, along, across;
	float length = 0, gap = 0; // along the rows; between the two rows (0: one row)
};
Rows rowsOf(const std::vector<juce::Point<float>> &pads)
{
	Rows r;
	for (const auto &q : pads)
		r.c += q;
	r.c /= float(pads.size());
	float sxx = 0, syy = 0, sxy = 0;
	for (const auto &q : pads) {
		const auto d = q - r.c;
		sxx += d.x * d.x, syy += d.y * d.y, sxy += d.x * d.y;
	}
	const float ang = 0.5f * std::atan2(2 * sxy, sxx - syy);
	r.along = {std::cos(ang), std::sin(ang)};
	r.across = {-r.along.y, r.along.x};
	float lo = 1e9f, hi = -1e9f, alo = 1e9f, ahi = -1e9f;
	for (const auto &q : pads) {
		const auto d = q - r.c;
		const float u = d.x * r.along.x + d.y * r.along.y, v = d.x * r.across.x + d.y * r.across.y;
		lo = std::min(lo, u), hi = std::max(hi, u), alo = std::min(alo, v), ahi = std::max(ahi, v);
	}
	r.length = hi - lo;
	r.gap = ahi - alo;
	r.c += r.along * ((lo + hi) / 2) + r.across * ((alo + ahi) / 2);
	return r;
}

void drawIc(juce::Graphics &g, const Part &p)
{
	const auto rw = rowsOf(p.pads);
	const bool single = rw.gap < 3.0f || p.kind == Kind::Sip || p.kind == Kind::Ra;
	const float len = rw.length + 2.4f;
	const float wid = single ? 3.0f : rw.gap > 14 ? rw.gap - 1.8f : std::max(5.0f, rw.gap - 1.4f);
	const auto t = juce::AffineTransform::rotation(std::atan2(rw.along.y, rw.along.x)).translated(rw.c);
	// the pins, bent down into their holes
	if (!single)
		for (const auto &q : p.pads) {
			const auto d = q - rw.c;
			const float v = d.x * rw.across.x + d.y * rw.across.y;
			const auto edge = rw.c + rw.along * (d.x * rw.along.x + d.y * rw.along.y) + rw.across * (v > 0 ? wid / 2 : -wid / 2);
			lead(g, edge, q, 0.45f);
		}
	juce::Path shape;
	shape.addRoundedRectangle(-len / 2, -wid / 2, len, wid, single ? 0.25f : 0.35f);
	shape.applyTransform(t);
	shadowed(g, shape, single ? 3.0f : 1.8f);
	const auto bb = shape.getBounds();
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff35373c), bb.getX(), bb.getY(), juce::Colour(0xff17181b), bb.getRight(), bb.getBottom(), false));
	g.fillPath(shape);
	g.setColour(juce::Colour(0x22ffffff));
	g.strokePath(shape, juce::PathStrokeType(0.18f));
	// pin 1's notch (a DIP's), at pin 1's end
	if (!single) {
		float u1 = -len / 2;
		if (p.pin1) {
			const auto d = p.pads[0] - rw.c;
			u1 = (d.x * rw.along.x + d.y * rw.along.y) < 0 ? -len / 2 : len / 2;
		}
		const auto n = rw.c + rw.along * u1;
		g.setColour(juce::Colour(0xff0d0e10));
		g.fillEllipse(juce::Rectangle<float>(1.6f, 1.6f).withCentre(n));
	}
	const auto mark = p.kind == Kind::Ra ? juce::String(p.ref.retainCharacters("0123456789").getIntValue() <= 8 ? "RM0621" : "") : marking(p);
	if (mark.isNotEmpty()) {
		juce::Graphics::ScopedSaveState s(g);
		float ang = std::atan2(rw.along.y, rw.along.x);
		if (ang > kPi / 2 || ang < -kPi / 2)
			ang += kPi; // reads left to right, or bottom to top
		g.addTransform(juce::AffineTransform::rotation(ang).translated(rw.c));
		juce::Font f(juce::FontOptions("Helvetica Neue", std::min(1.9f, wid * 0.42f), juce::Font::plain));
		const float avail = len - 2.0f, w = juce::GlyphArrangement::getStringWidth(f, mark);
		if (w > avail)
			f.setHorizontalScale(avail / w);
		g.setFont(f);
		g.setColour(juce::Colour(0xffb9bcc1));
		g.drawText(mark, juce::Rectangle<float>(len, 2.6f).withCentre({}), juce::Justification::centred, false);
	}
}

void drawConnector(juce::Graphics &g, const Part &p)
{
	const auto rw = rowsOf(p.pads);
	const float len = rw.length + 2.5f, wid = std::max(5.0f, rw.gap + 4.0f);
	const auto t = juce::AffineTransform::rotation(std::atan2(rw.along.y, rw.along.x)).translated(rw.c);
	juce::Path shape;
	shape.addRoundedRectangle(-len / 2, -wid / 2, len, wid, 0.4f);
	shape.applyTransform(t);
	shadowed(g, shape, 2.5f);
	const auto bb = shape.getBounds();
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff2ede0), bb.getX(), bb.getY(), juce::Colour(0xffb7ad97), bb.getRight(), bb.getBottom(), false));
	g.fillPath(shape);
	g.setColour(juce::Colour(0xff8d8473));
	for (const auto &q : p.pads)
		g.fillEllipse(juce::Rectangle<float>(0.9f, 0.9f).withCentre(q));
}

void drawCrystal(juce::Graphics &g, const Part &p)
{
	const auto a = p.pads[0], b = p.pads.back();
	const auto m = (a + b) * 0.5f, dir = (b - a) / std::max(1e-3f, a.getDistanceFrom(b));
	juce::Path can;
	can.addRoundedRectangle(-5.6f, -2.3f, 11.2f, 4.6f, 2.2f);
	can.applyTransform(juce::AffineTransform::rotation(std::atan2(dir.y, dir.x)).translated(m));
	shadowed(g, can, 2.5f);
	g.setGradientFill(across(juce::Colour(0xffc8ccd1), m - dir, m + dir, 2.3f));
	g.fillPath(can);
}

// A small pot from above, under its knob: the plastic body, the bracket to its three pins.
void drawPot(juce::Graphics &g, const Part &p)
{
	juce::Point<float> c;
	for (const auto &q : p.pads)
		c += q;
	c /= float(p.pads.size());
	const auto centre = c - juce::Point<float>(0, 10.0f); // the pins are 10 mm in front of its shaft
	for (const auto &q : p.pads)
		lead(g, q - juce::Point<float>(0, 2.2f), q, 0.55f);
	juce::Path body;
	body.addRoundedRectangle(centre.x - 4.9f, centre.y - 5.4f, 9.8f, 13.0f, 0.8f);
	shadowed(g, body, 3.0f);
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3c3e43), centre.x - 4.9f, centre.y - 5.4f, juce::Colour(0xff1d1e21), centre.x + 4.9f, centre.y + 7.6f, false));
	g.fillPath(body);
}

void drawSwitch(juce::Graphics &g, const Part &p)
{
	const auto rw = rowsOf(p.pads);
	juce::Path body;
	body.addRoundedRectangle(juce::Rectangle<float>(rw.length + 2.0f, rw.gap + 2.0f).withCentre(rw.c), 0.6f);
	g.setColour(juce::Colour(0xff26282c));
	g.fillPath(body);
}

// The trimmer's rotor: the white body and its square boss with the cross slot (the 303's).
void drawTrimmerAt(juce::Graphics &g, juce::Point<float> c, float r, float v)
{
	const float a = juce::degreesToRadians(-135.0f + 270.0f * v);
	juce::Path body;
	body.addEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
	g.setColour(juce::Colours::black.withAlpha(0.32f));
	g.fillEllipse(c.x - r + r * 0.12f, c.y - r + r * 0.25f, 2 * r, 2 * r);
	const juce::Colour base(0xffefebe1);
	g.setGradientFill(juce::ColourGradient(base.brighter(0.35f), c.x - r * 0.5f, c.y - r * 0.6f, base.darker(0.35f), c.x + r, c.y + r, true));
	g.fillPath(body);
	g.setColour(base.darker(0.5f).withAlpha(0.6f));
	g.strokePath(body, juce::PathStrokeType(r * 0.05f));
	const auto t = juce::AffineTransform::rotation(a).translated(c);
	const float s = r * 0.62f;
	juce::Path boss;
	boss.addRoundedRectangle(-s, -s, 2 * s, 2 * s, s * 0.22f);
	boss.applyTransform(t);
	g.setColour(base.darker(0.45f).withAlpha(0.5f));
	g.fillPath(boss, juce::AffineTransform::translation(r * 0.06f, r * 0.1f));
	g.setGradientFill(juce::ColourGradient(base.brighter(0.5f), c.x - s, c.y - s, base.darker(0.15f), c.x + s, c.y + s, false));
	g.fillPath(boss);
	for (int k = 0; k < 2; ++k) {
		juce::Path arm;
		const float lw = s * (k == 0 ? 0.95f : 0.8f), hw = s * 0.17f;
		arm.addRoundedRectangle(-hw, -lw, 2 * hw, 2 * lw, hw * 0.6f);
		arm.applyTransform(juce::AffineTransform::rotation(float(k) * kPi / 2).followedBy(t));
		g.setColour(juce::Colour(0xff9b968a));
		g.fillPath(arm);
	}
}

juce::Point<float> centreOf(const Part &p)
{
	juce::Point<float> c;
	for (const auto &q : p.pads)
		c += q;
	return c / float(std::max<size_t>(1, p.pads.size()));
}

const Part *tmPart()
{
	for (const auto &p : boards().parts)
		if (p.kind == Kind::Tm)
			return &p;
	return nullptr;
}

// The panel's elements on the switch board (the keys, the LEDs, the display, TEMPO) move with it.
bool onSwitchBoard(const HardwareView::Element &e)
{
	return e.type == "key" || e.type == "led" || e.type == "display" || e.id == "knob_tempo";
}

// ------------------------------------------------------------------ the scope's words
const float kTimes[] = {0.1f, 0.2f, 0.5f, 1, 2, 5, 10, 20, 50, 100, 200}; // ms/div
const float kVolts[] = {0.05f, 0.1f, 0.2f, 0.5f, 1, 2, 5, 10};            // V/div
const char *const kVoiceUpper[kVoices] = {"BD", "SD", "LT", "MT", "HT", "RS", "HC", "HH", "CR", "RD"};
const char *const kVoiceLower[kVoices] = {"bd", "sd", "lt", "mt", "ht", "rs", "hc", "hh", "cr", "rd"};

juce::String timeText(float ms)
{
	if (ms < 1)
		return juce::String(int(std::lround(ms * 1000))) + " us";
	return (std::abs(ms - std::round(ms)) < 1e-3f ? juce::String(int(std::lround(ms))) : juce::String(ms, 1)) + " ms";
}
juce::String voltText(float v) { return v < 1 ? juce::String(int(std::lround(v * 1000))) + " mV" : juce::String(int(v)) + " V"; }

// The parts by owner, and each node's parts (for the probe menu).
struct PartIndex {
	std::map<std::string, std::vector<int>> byOwner;
	std::map<std::string, std::vector<std::pair<int, int>>> byNode; // node -> (part, pin)
};
const PartIndex &partIndex()
{
	static const PartIndex ix = [] {
		PartIndex i;
		const auto &parts = VoiceBoard::parts();
		for (int k = 0; k < int(parts.size()); ++k) {
			i.byOwner[parts[size_t(k)].owner].push_back(k);
			for (int p = 0; p < int(parts[size_t(k)].pins.size()); ++p)
				i.byNode[parts[size_t(k)].pins[size_t(p)].second].push_back({k, p});
		}
		return i;
	}();
	return ix;
}

// A probe's name as a technician says it.
juce::String probeText(const juce::String &where)
{
	if (where.isEmpty())
		return "off";
	for (int v = 0; v < kVoices; ++v) {
		if (where == juce::String("mix.m") + kVoiceLower[v])
			return juce::String(kVoiceUpper[v]) + " MULTI OUT";
		if (where == juce::String("trig.") + kVoiceLower[v])
			return juce::String(kVoiceUpper[v]) + " TRIG";
	}
	if (where == "mix.l" || where == "mix.r")
		return juce::String("MASTER OUT ") + (where == "mix.l" ? "L" : "R");
	if (where.startsWith("acc."))
		return "ACCENT DAC " + where.fromFirstOccurrenceOf(".", false, false);
	const auto &ix = partIndex();
	const auto it = ix.byNode.find(where.toStdString());
	if (it != ix.byNode.end() && !it->second.empty()) {
		const auto &pt = VoiceBoard::parts()[size_t(it->second[0].first)];
		return juce::String(pt.ref) + " " + pt.pins[size_t(it->second[0].second)].first;
	}
	return where;
}

// What each serial range is: the change it starts with (SN p.7), plainly.
juce::String revisionWords(int index, const std::vector<int> &serials)
{
	if (index == 0)
		return "the first units: R495 on the BD, C9 0.22u, the old clocks";
	juce::StringArray what;
	for (const auto &c : changes())
		if (c.serial == serials[size_t(index)] && !c.before.empty())
			what.add(juce::String(c.parts).upToFirstOccurrenceOf(" (", false, false) + " " + juce::String(c.change).upToFirstOccurrenceOf(";", false, false));
	return what.joinIntoString(", ");
}
} // namespace

// ====================================================================== geometry

juce::AffineTransform HardwareView::underside(float zoom, juce::Point<float> pan) const
{
	// panel mm -> view px: the boards seen from above, the view fitted to kContent at zoom 1
	const float s = kFit * zoom;
	return juce::AffineTransform::translation(-kContent.getPosition()).scaled(s).translated(kOrigin + pan);
}

void HardwareView::clampPan()
{
	const float w = kContent.getWidth() * kFit * m_zoom, h = kContent.getHeight() * kFit * m_zoom;
	auto clampAxis = [](float pan, float start, float size, float view) {
		if (size <= view)
			return view / 2 - size / 2 - start;
		return juce::jlimit(view - size - start - 12, -start + 12, pan);
	};
	m_pan.x = clampAxis(m_pan.x, kOrigin.x, w, kCanvasW);
	m_pan.y = clampAxis(m_pan.y, kOrigin.y, h, kCanvasH);
}

void HardwareView::zoomService(float factor, juce::Point<float> about)
{
	const float z = juce::jlimit(1.0f, kMaxZoom, m_zoom * factor);
	if (z == m_zoom)
		return;
	const auto mm = about.transformedBy(underside(m_zoom, m_pan).inverted());
	m_zoom = z;
	m_pan += about - mm.transformedBy(underside(m_zoom, m_pan));
	clampPan();
	m_viewChangedAt = juce::Time::getMillisecondCounter();
	repaint();
}

void HardwareView::serviceView(float zoom, juce::Point<float> centreMm)
{
	m_service = true;
	m_zoom = juce::jlimit(1.0f, kMaxZoom, zoom);
	m_pan = {};
	m_pan = juce::Point<float>(kCanvasW / 2, kCanvasH / 2) - centreMm.transformedBy(underside(m_zoom, {}));
	clampPan();
	m_viewChangedAt = 0;
	repaint();
}

// ====================================================================== the still picture

void HardwareView::renderUnderside(juce::Graphics &g, const juce::AffineTransform &t)
{
	g.fillAll(juce::Colour(0xff15171a));
	const auto &all = boards();
	const bool detail = std::sqrt(std::abs(t.getDeterminant())) > 5.0f; // px per mm: designators when legible
	juce::Graphics::ScopedSaveState s(g);
	g.addTransform(t);
	// the transformer on the base, standing up through the voicing board's cut-out
	{
		const auto r = all.transformer;
		juce::Path frame;
		frame.addRoundedRectangle(r, 1.5f);
		shadowed(g, frame, 6.0f);
		g.setGradientFill(juce::ColourGradient(juce::Colour(0xff6a6e73), r.getX(), r.getY(), juce::Colour(0xff3a3d41), r.getRight(), r.getBottom(), false));
		g.fillPath(frame);
		const auto core = r.reduced(r.getWidth() * 0.16f, r.getHeight() * 0.1f);
		g.setColour(juce::Colour(0xff2c2f33));
		g.fillRect(core);
		g.setColour(juce::Colour(0xffb8743a));
		g.fillRect(core.reduced(core.getWidth() * 0.22f, core.getHeight() * 0.18f));
	}
	for (const auto &b : all.boards) {
		for (int k = 3; k >= 1; --k) {
			g.setColour(juce::Colours::black.withAlpha(0.14f));
			g.fillPath(b.outline, juce::AffineTransform::translation(0.5f * float(k), 0.9f * float(k)));
		}
		g.setColour(kMask);
		g.fillPath(b.outline);
		// the copper on the other side, faint through the board
		{
			juce::Graphics::ScopedSaveState c(g);
			g.reduceClipRegion(b.outline);
			g.setColour(kCopperThrough);
			g.fillPath(b.lands);
			for (int k = 0; k < 3; ++k)
				g.strokePath(b.tracks[k], juce::PathStrokeType(kTrackWidths[k], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
		}
		g.setColour(kMaskEdge);
		g.strokePath(b.outline, juce::PathStrokeType(0.5f));
		const auto box = b.outline.getBounds();
		g.setColour(kSilk.withAlpha(0.8f));
		g.setFont(sans(2.6f, true));
		g.drawText(b.name + "  " + b.number, juce::Rectangle<float>(box.getX() + 4, box.getBottom() - 5.5f, 120, 4), juce::Justification::centredLeft, false);
	}
	// the parts, flat ones first so the tall ones' shadows fall on them
	auto pass = [&](auto pred) {
		for (const auto &p : all.parts)
			if (pred(p.kind))
				switch (p.kind) {
				case Kind::R: if (p.pads.size() >= 2) drawResistor(g, p); break;
				case Kind::D: if (p.pads.size() >= 2) drawDiode(g, p); break;
				case Kind::C: drawCapacitor(g, p); break;
				case Kind::Q: case Kind::Dual: drawTransistor(g, p); break;
				case Kind::Ic: case Kind::Sip: case Kind::Ra: if (p.pads.size() >= 3) drawIc(g, p); break;
				case Kind::Cn: if (p.pads.size() >= 2) drawConnector(g, p); break;
				case Kind::Xtal: if (p.pads.size() >= 2) drawCrystal(g, p); break;
				case Kind::Vr: drawPot(g, p); break;
				case Kind::Sw: if (p.pads.size() >= 3) drawSwitch(g, p); break;
				default: break;
				}
	};
	pass([](Kind k) { return k == Kind::Sw || k == Kind::R || k == Kind::D; });
	pass([](Kind k) { return k == Kind::Ic || k == Kind::Sip || k == Kind::Ra || k == Kind::Cn || k == Kind::Xtal; });
	pass([](Kind k) { return k == Kind::C || k == Kind::Q || k == Kind::Dual || k == Kind::Vr; });
	if (detail) {
		// the designators, beside each part
		g.setFont(sans(1.35f, true));
		g.setColour(kSilk.withAlpha(0.85f));
		for (const auto &p : all.parts) {
			if (p.kind == Kind::Sw || p.kind == Kind::Led || p.kind == Kind::Display || p.kind == Kind::Vr)
				continue;
			const auto c = centreOf(p);
			const float off = p.kind == Kind::Ic ? 0.0f : p.kind == Kind::C && valueOf(p.value) >= 1e-6 ? 3.6f : 2.2f;
			g.drawText(p.ref, juce::Rectangle<float>(12, 1.6f).withCentre(c + juce::Point<float>(0, -off - 0.4f)), juce::Justification::centred, false);
		}
	}
}

// ====================================================================== paint

void HardwareView::paintService(juce::Graphics &g)
{
	const float phys = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
	const bool settled = juce::Time::getMillisecondCounter() - m_viewChangedAt >= 150 && !m_panning;
	if (!m_serviceBase.isValid() || (std::abs(phys - m_serviceBaseScale) > 0.01f && juce::Time::getMillisecondCounter() - m_resizedAt >= 200)) {
		m_serviceBaseScale = phys;
		m_serviceBase = juce::Image(juce::Image::RGB, juce::roundToInt(kCanvasW * phys), juce::roundToInt(kCanvasH * phys), true);
		juce::Graphics sg(m_serviceBase);
		renderUnderside(sg, underside(1, {}).scaled(phys));
		m_detailZoom = 0;
	}
	juce::Graphics::ScopedSaveState s(g);
	g.reduceClipRegion(juce::Rectangle<int>(0, 0, int(kCanvasW), int(kCanvasH)));
	if (m_zoom > 1.001f && settled && (m_detailZoom != m_zoom || m_detailPan != m_pan || std::abs(m_detailScale - phys) > 0.01f)) {
		m_detailZoom = m_zoom;
		m_detailPan = m_pan;
		m_detailScale = phys;
		m_serviceDetail = juce::Image(juce::Image::RGB, juce::roundToInt(kCanvasW * phys), juce::roundToInt(kCanvasH * phys), true);
		juce::Graphics dg(m_serviceDetail);
		renderUnderside(dg, underside(m_zoom, m_pan).scaled(phys));
	}
	if (m_zoom > 1.001f && m_detailZoom == m_zoom && m_detailPan == m_pan && m_serviceDetail.isValid()) {
		g.drawImage(m_serviceDetail, juce::Rectangle<float>(0, 0, kCanvasW, kCanvasH));
	} else {
		g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
		g.drawImageTransformed(m_serviceBase, juce::AffineTransform::scale(1 / phys).translated(-kOrigin).scaled(m_zoom).translated(kOrigin + m_pan));
	}
	// what moves: the knobs on their pots, the keys, the LEDs and the display as the firmware has
	// them, and TM's rotor
	const auto t = underside(m_zoom, m_pan);
	const auto &all = elements();
	for (int pass = 0; pass < 2; ++pass) {
		juce::Graphics::ScopedSaveState e(g);
		g.addTransform(pass == 1 ? juce::AffineTransform::translation(0, kSwitchDy).followedBy(t) : t);
		for (int i : m_leds)
			if (onSwitchBoard(all[size_t(i)]) == (pass == 1) && all[size_t(i)].v["in"].isVoid())
				drawLed(g, all[size_t(i)], ledLevel(all[size_t(i)]));
		if (pass == 1 && m_display >= 0)
			drawDisplay(g, all[size_t(m_display)]);
		if (pass == 1)
			for (size_t k = 0; k < m_keys.size(); ++k)
				drawKey(g, m_keys[k], m_held.down(int(k)), m_hover.kind == HitKind::Key && m_hover.index == int(k));
		for (int i : m_knobs)
			if (onSwitchBoard(all[size_t(i)]) == (pass == 1))
				drawKnob(g, all[size_t(i)], paramValue(all[size_t(i)].param),
				         (m_hover.kind == HitKind::Knob && m_hover.index == i) || (m_drag.kind == HitKind::Knob && m_drag.index == i));
	}
	if (const auto *tm = tmPart()) {
		juce::Graphics::ScopedSaveState e(g);
		g.addTransform(t);
		const auto c = centreOf(*tm);
		drawTrimmerAt(g, c, 3.4f, paramValue("tm"));
		const bool hot = m_hover.kind == HitKind::Trim || m_drag.kind == HitKind::Trim;
		// its label as the board prints it, and a callout while it's being trimmed
		g.setFont(sans(1.8f, true));
		g.setColour(kSilk);
		g.drawText("TM", juce::Rectangle<float>(8, 2.2f).withCentre(c + juce::Point<float>(0, 5.0f)), juce::Justification::centred, false);
		if (hot) {
			g.setColour(kOrange);
			g.drawEllipse(c.x - 4.4f, c.y - 4.4f, 8.8f, 8.8f, 0.5f);
		}
	}
}

// ====================================================================== the right-hand side

void HardwareView::drawScope(juce::Graphics &g)
{
	const auto sc = kScopeScreen;
	g.setColour(juce::Colour(0xff07120c));
	g.fillRoundedRectangle(sc, 4);
	g.setColour(juce::Colour(0xff0f2a1a));
	g.drawRoundedRectangle(sc, 4, 1);
	for (int i = 1; i < 10; ++i) {
		g.setColour(juce::Colour(i == 1 ? 0xff1d4433 : 0xff16382a));
		g.drawVerticalLine(juce::roundToInt(sc.getX() + sc.getWidth() * float(i) / 10), sc.getY(), sc.getBottom());
	}
	for (int i = 1; i < 8; ++i) {
		g.setColour(juce::Colour(i == 4 ? 0xff1d4433 : 0xff16382a));
		g.drawHorizontalLine(juce::roundToInt(sc.getY() + sc.getHeight() * float(i) / 8), sc.getX(), sc.getRight());
	}
	{
		juce::Graphics::ScopedSaveState s(g);
		g.reduceClipRegion(sc.toNearestInt());
		const auto &lo = m_scope.lo[0], &hi = m_scope.hi[0];
		if (!lo.empty() && m_proc.probe(0).isNotEmpty()) {
			const float perV = sc.getHeight() / 8 / kVolts[m_scope.volts[0]];
			juce::Path trace;
			const int n = int(lo.size());
			for (int x = 0; x < n; ++x) {
				const float y0 = sc.getCentreY() - hi[size_t(x)] * perV, y1 = sc.getCentreY() - lo[size_t(x)] * perV;
				trace.addRectangle(sc.getX() + float(x) * sc.getWidth() / float(n), y0 - 0.5f, sc.getWidth() / float(n) + 0.3f, std::max(1.0f, y1 - y0 + 1.0f));
			}
			g.setColour(juce::Colour(0xff7dffa8).withAlpha(0.22f));
			g.fillPath(trace, juce::AffineTransform::translation(0, 0.8f));
			g.setColour(juce::Colour(0xffb9ffd0));
			g.fillPath(trace);
		}
	}
	g.setFont(fonts::mono(10.0f));
	const bool hovT = m_hover.kind == HitKind::Scope && m_hover.index == 5, hovV = m_hover.kind == HitKind::Scope && m_hover.index == 3;
	g.setColour(hovT ? kOrange : juce::Colour(0xff7dffa8));
	g.drawText(m_scope.trigger < 0 ? juce::String("free run") : juce::String("trig ") + kVoiceUpper[m_scope.trigger], scopeHot(5), juce::Justification::centredLeft);
	g.setColour(hovV ? kOrange : juce::Colour(0xff4f9a6a));
	g.drawText(voltText(kVolts[m_scope.volts[0]]) + "/div", scopeHot(3), juce::Justification::centredRight);
}

void HardwareView::drawDrawer(juce::Graphics &g)
{
	g.setColour(juce::Colour(0xff15171a));
	g.fillRect(juce::Rectangle<float>(kCanvasW, 0, kDrawerW, kCanvasH));
	auto card = [&](juce::Rectangle<float> c) {
		g.setColour(juce::Colour(0xff1d2026));
		g.fillRoundedRectangle(c, 7);
		g.setColour(juce::Colour(0xff2a2f37));
		g.drawRoundedRectangle(c.reduced(0.5f), 7, 1);
	};
	auto caption = [&](juce::Rectangle<float> c, juce::Colour dot, const juce::String &text, const juce::String &right, bool hotLeft = false, bool hotRight = false) {
		const float y = c.getY() + 8;
		g.setColour(dot);
		g.fillEllipse(c.getX() + 12, y + 2, 7, 7);
		g.setFont(fonts::mono(10.5f, fonts::Weight::SemiBold));
		g.setColour(hotLeft ? kOrange : juce::Colour(0xffc9cfd8));
		g.drawText(text, juce::Rectangle<float>(c.getX() + 25, y - 2, c.getWidth() - 37, 14), juce::Justification::centredLeft);
		g.setFont(fonts::mono(10.0f));
		g.setColour(hotRight ? kOrange : juce::Colour(0xff8a929e));
		g.drawText(right, juce::Rectangle<float>(c.getX() + 25, y - 2, c.getWidth() - 37, 14), juce::Justification::centredRight);
	};
	// the scope, on what its probe is on
	card(kScopeCase);
	const auto probe = m_proc.probe(0);
	caption(kScopeCase, kProbe, probe.isEmpty() ? juce::String("SCOPE  probe off") : "SCOPE  on " + (m_scope.label[0].isNotEmpty() ? m_scope.label[0] : probeText(probe)),
	        timeText(kTimes[m_scope.time]) + "/div", m_hover.kind == HitKind::Scope && m_hover.index == 0, m_hover.kind == HitKind::Scope && m_hover.index == 2);
	drawScope(g);
	g.setFont(sans(10.5f));
	g.setColour(juce::Colour(0xffaab2bd));
	const juce::String hint = probe == "mix.mhc" ? "TM: HAND CLAP LEVEL at centre, MULTI OUT 2 Vp-p (SN p.11); this model reads 4.0 at its default trim, the calibrated unit's"
	                                             : probe.isEmpty() ? "Click the caption to clip the probe on: a jack, a trigger, any part's pin."
	                                                               : "Click the caption to move the probe, the time base or the trigger to change them.";
	g.drawFittedText(hint, juce::Rectangle<float>(kIx + 12, kScopeScreen.getBottom() + 4, kIw - 24, 32).toNearestInt(), juce::Justification::centredLeft, 2, 0.95f);

	// the readouts: the probe's peak to peak, and where HAND CLAP LEVEL is
	auto readout = [&](juce::Rectangle<float> c, juce::Colour bg, juce::Colour fg, const juce::String &digits, const juce::String &unit) {
		const juce::Rectangle<float> win(c.getX() + 10, c.getY() + 22, c.getWidth() - 20, 27);
		g.setColour(bg);
		g.fillRoundedRectangle(win, 3);
		g.setColour(fg);
		g.setFont(fonts::mono(18.0f, fonts::Weight::SemiBold));
		g.drawText(digits, win.withTrimmedRight(unit.length() > 2 ? 40 : 26), juce::Justification::centredRight);
		g.setFont(fonts::mono(11.0f, fonts::Weight::Medium));
		g.drawText(unit, win.withTrimmedRight(6), juce::Justification::centredRight);
	};
	card(kVppCase);
	caption(kVppCase, kProbe, "PEAK-PEAK", "CH1");
	readout(kVppCase, juce::Colour(0xff160707), juce::Colour(0xffff3b2f), probe.isEmpty() ? juce::String("----") : juce::String(m_scope.pp[0], 2), "Vp-p");
	card(kLevelCase);
	caption(kLevelCase, juce::Colour(0xffe8473a), "LEVEL", "HAND CLAP");
	readout(kLevelCase, juce::Colour(0xffb7c0a2), juce::Colour(0xff1d2218), m_proc.params().getParameter("hc_level")->getCurrentValueAsText(), "");

	// the trimmer: the TR-909's only one
	card(kTrimCase);
	g.setFont(fonts::mono(10.5f, fonts::Weight::SemiBold));
	g.setColour(juce::Colour(0xffc9cfd8));
	g.drawText("TRIMMER", juce::Rectangle<float>(kTrimCase.getX() + 14, kTrimCase.getY() + 6, 120, 18), juce::Justification::centredLeft);
	{
		const auto pill = testPill();
		const bool hov = m_hover.kind == HitKind::TrimButton && m_hover.index == 1;
		g.setColour(m_scope.test ? juce::Colour(0xff2a1e12) : hov ? juce::Colour(0xff2c323b) : juce::Colour(0xff23272e));
		g.fillRoundedRectangle(pill, 5);
		g.setColour(m_scope.test ? kOrange : juce::Colour(0xff3a414c));
		g.drawRoundedRectangle(pill.reduced(0.5f), 5, 1);
		g.setFont(fonts::mono(9.5f, fonts::Weight::Medium));
		g.setColour(m_scope.test ? kOrange : juce::Colour(0xffc9cfd8));
		g.drawText(m_scope.test ? "TESTING" : "TEST", pill, juce::Justification::centred);
	}
	{
		const auto row = trimRow();
		const bool hot = m_hover.kind == HitKind::Trim || m_drag.kind == HitKind::Trim;
		g.setColour(hot ? juce::Colour(0xff2b2620) : juce::Colour(0xff21252b));
		g.fillRoundedRectangle(row, 5);
		if (hot) {
			g.setColour(kOrange.withAlpha(0.7f));
			g.drawRoundedRectangle(row.reduced(0.5f), 5, 1);
			g.setColour(kOrange);
			g.fillRoundedRectangle(row.withWidth(3).reduced(0, 6), 1.5f);
		}
		const float tm = paramValue("tm"), factory = float(TR909Processor::factoryTm());
		const bool atFactory = std::abs(tm - factory) < 1e-4f;
		const float y1 = row.getY() + 4;
		g.setColour(atFactory ? juce::Colour(0xff46c37b) : kOrange);
		g.fillEllipse(row.getX() + 11, y1 + 5, 7, 7);
		g.setFont(fonts::mono(11.5f, fonts::Weight::SemiBold));
		g.setColour(juce::Colour(0xffeef1f5));
		g.drawText("TM", juce::Rectangle<float>(row.getX() + 24, y1, 40, 17), juce::Justification::centredLeft);
		g.setFont(sans(11.0f));
		g.setColour(juce::Colour(0xff8f98a5));
		g.drawText("hand clap, 10k(B)", juce::Rectangle<float>(row.getX() + 50, y1, 140, 17), juce::Justification::centredLeft);
		g.setFont(fonts::mono(11.0f, fonts::Weight::Medium));
		g.setColour(atFactory ? juce::Colour(0xffe9ecf1) : juce::Colour(0xffffb866));
		g.drawText(m_proc.params().getParameter("tm")->getCurrentValueAsText(), juce::Rectangle<float>(row.getRight() - 96, y1, 88, 17),
		           juce::Justification::centredRight);
		const auto track = trimTrack();
		g.setColour(juce::Colour(0xff3a414c));
		g.fillRoundedRectangle(track, 2);
		g.setColour(juce::Colour(0xffc9cfd8));
		const float fx = track.getX() + factory * track.getWidth();
		g.drawLine(fx, track.getY() - 5, fx, track.getBottom() + 5, 1.2f);
		drawTrimmerAt(g, {track.getX() + tm * track.getWidth(), track.getCentreY()}, 8.0f, tm);
	}

	// the circuit: which unit this is, by its serial number (SN p.7)
	card(kRevCase);
	g.setFont(fonts::mono(10.5f, fonts::Weight::SemiBold));
	g.setColour(juce::Colour(0xffc9cfd8));
	g.drawText("CIRCUIT", juce::Rectangle<float>(kRevCase.getX() + 14, kRevCase.getY() + 6, 120, 18), juce::Justification::centredLeft);
	g.setFont(fonts::mono(10.0f));
	g.setColour(juce::Colour(0xff8a929e));
	g.drawText("serial number  (SN p.7)", juce::Rectangle<float>(kRevCase.getX() + 14, kRevCase.getY() + 6, kRevCase.getWidth() - 28, 18),
	           juce::Justification::centredRight);
	const auto serials = boardSerials();
	for (int i = 0; i < int(serials.size()); ++i) {
		const auto row = revRow(i);
		const int from = serials[size_t(i)], to = i + 1 < int(serials.size()) ? serials[size_t(i + 1)] - 1 : 0;
		const bool on = m_proc.serial() >= from && (to == 0 || m_proc.serial() <= to);
		const bool hov = m_hover.kind == HitKind::Serial && m_hover.index == i;
		g.setColour(on ? juce::Colour(0xff2b2620) : hov ? juce::Colour(0xff262a31) : juce::Colour(0xff21252b));
		g.fillRoundedRectangle(row, 5);
		if (on) {
			g.setColour(kOrange.withAlpha(0.7f));
			g.drawRoundedRectangle(row.reduced(0.5f), 5, 1);
			g.setColour(kOrange);
			g.fillRoundedRectangle(row.withWidth(3).reduced(0, 6), 1.5f);
		}
		g.setColour(on ? kOrange : juce::Colour(0xff3a414c));
		g.fillEllipse(row.getX() + 11, row.getY() + 9, 7, 7);
		g.setFont(fonts::mono(11.5f, fonts::Weight::SemiBold));
		g.setColour(juce::Colour(0xffeef1f5));
		g.drawText(to == 0 ? "SN " + juce::String(from) + " up" : "SN " + juce::String(from) + " - " + juce::String(to),
		           juce::Rectangle<float>(row.getX() + 24, row.getY() + 4, row.getWidth() - 30, 17), juce::Justification::centredLeft);
		g.setFont(sans(10.5f));
		g.setColour(on ? juce::Colour(0xffd6dbe2) : juce::Colour(0xff8f98a5));
		g.drawText(revisionWords(i, serials), juce::Rectangle<float>(row.getX() + 24, row.getY() + 21, row.getWidth() - 30, 16),
		           juce::Justification::centredLeft, true);
	}
}

// The scope's sweep: the latest whole one after a trigger, each pixel column's min and max.
void HardwareView::updateScope()
{
	const int cols = int(kScopeScreen.getWidth());
	const double rate = m_proc.probeRate();
	const int frames = std::max(cols, int(std::lround(10 * kTimes[m_scope.time] * 1e-3 * rate)));
	const int pre = frames / 10;
	const uint32_t end = m_proc.probeWritten();
	if (end == 0 || frames * 3 > TR909Processor::kProbeFrames || m_proc.probe(0).isEmpty())
		return;
	int start = -1;
	const int window = std::min(TR909Processor::kProbeFrames / 2, frames * 2 + int(rate * 0.7));
	if (m_scope.trigger < 0) {
		start = window - frames;
	} else {
		std::vector<float> t(static_cast<size_t>(window));
		m_proc.readProbe(2, t.data(), window, end);
		const uint32_t bit = 1u << m_scope.trigger;
		for (int i = window - (frames - pre); i > pre; --i)
			if ((uint32_t(t[size_t(i)]) & bit) && !(uint32_t(t[size_t(i - 1)]) & bit)) {
				const uint32_t at = end - uint32_t(window) + uint32_t(i);
				if (at != m_scope.lastTrigger) {
					m_scope.lastTrigger = at;
					start = i - pre;
				}
				break;
			}
	}
	if (start < 0)
		return; // normal mode: the last sweep stays
	std::vector<float> x(static_cast<size_t>(window));
	auto &lo = m_scope.lo[0], &hi = m_scope.hi[0];
	lo.assign(size_t(cols), 0.0f);
	hi.assign(size_t(cols), 0.0f);
	m_proc.readProbe(0, x.data(), window, end);
	float mn = 1e9f, mx = -1e9f;
	for (int col = 0; col < cols; ++col) {
		const int a = start + int(int64_t(col) * frames / cols), b = std::max(a + 1, start + int(int64_t(col + 1) * frames / cols));
		float l = 1e9f, h = -1e9f;
		for (int i = a; i < b; ++i) {
			l = std::min(l, x[size_t(i)]);
			h = std::max(h, x[size_t(i)]);
		}
		lo[size_t(col)] = l;
		hi[size_t(col)] = h;
		mn = std::min(mn, l);
		mx = std::max(mx, h);
	}
	m_scope.pp[0] = mx - mn;
	repaint(kScopeCase.getUnion(kVppCase).toNearestInt());
}

// The scope's pop-ups: the probe (0), the time base (2), the volts (3), the trigger (5).
void HardwareView::scopeMenu(int button)
{
	juce::PopupMenu m;
	const auto now = m_proc.probe(0);
	if (button == 0) {
		m.addItem(1, "off", true, now.isEmpty());
		juce::PopupMenu multi, trig, more;
		for (int v = 0; v < kVoices; ++v) {
			multi.addItem(100 + v, juce::String(kVoiceUpper[v]) + " MULTI OUT", true, now == juce::String("mix.m") + kVoiceLower[v]);
			trig.addItem(200 + v, juce::String(kVoiceUpper[v]) + " TRIG", true, now == juce::String("trig.") + kVoiceLower[v]);
		}
		m.addSubMenu("MULTI OUT jacks", multi);
		m.addItem(300, "MASTER OUT L", true, now == "mix.l");
		m.addItem(301, "MASTER OUT R", true, now == "mix.r");
		m.addSubMenu("trigger lines (IC1, IC10)", trig);
		for (int d = 1; d <= 8; ++d)
			more.addItem(400 + d, "ACCENT DAC " + juce::String(d), true, now == "acc." + juce::String(d));
		m.addSubMenu("accent DACs (RA1-RA8)", more);
		// every part the netlists draw: by voice, by kind, each pin with its node and its neighbours
		m.addSeparator();
		const auto &parts = VoiceBoard::parts();
		const auto &ix = partIndex();
		static const std::pair<const char *, const char *> owners[] = {
			{"bd", "BD"}, {"sd", "SD"}, {"lt", "LT"}, {"mt", "MT"}, {"ht", "HT"}, {"tomnoise", "TOM NOISE"}, {"rs", "RS"},
			{"hc", "HC"}, {"hh", "HH"}, {"cr", "CRASH"}, {"rd", "RIDE"}, {"mix", "MIX, JACK BOARD"}};
		static const std::pair<const char *, const char *> kinds[] = {
			{"IC", "ICs"}, {"Q", "transistors"}, {"D", "diodes"}, {"R", "resistors"}, {"C", "capacitors"}, {"VR", "pots"}, {"TM", "TM"}};
		for (const auto &[owner, title] : owners) {
			const auto own = ix.byOwner.find(owner);
			if (own == ix.byOwner.end())
				continue;
			juce::PopupMenu voice;
			for (const auto &[kind, kindTitle] : kinds) {
				juce::PopupMenu list;
				for (int k : own->second) {
					const auto &pt = parts[size_t(k)];
					if (pt.ref.substr(0, pt.ref.find_first_of("0123456789")) != kind)
						continue;
					juce::PopupMenu pins;
					for (int p = 0; p < int(pt.pins.size()); ++p) {
						const auto &node = pt.pins[size_t(p)].second;
						juce::StringArray others;
						const auto on = ix.byNode.find(node);
						if (on != ix.byNode.end())
							for (const auto &[q, qp] : on->second)
								if (q != k && others.size() < 4)
									others.add(juce::String(parts[size_t(q)].ref) + " " + parts[size_t(q)].pins[size_t(qp)].first);
						pins.addItem(1000 + k * 8 + p,
						             juce::String(pt.pins[size_t(p)].first) + ": " + juce::String(node).fromFirstOccurrenceOf(".", false, false)
						                 + (others.isEmpty() ? juce::String() : "  (" + others.joinIntoString(", ") + ")"),
						             true, now == juce::String(node));
					}
					list.addSubMenu(juce::String(pt.ref) + "  " + pt.value, pins);
				}
				if (list.getNumItems() > 0)
					voice.addSubMenu(kindTitle, list);
			}
			m.addSubMenu(title, voice);
		}
	} else if (button == 2) {
		for (int i = 0; i < int(std::size(kTimes)); ++i)
			m.addItem(1 + i, timeText(kTimes[i]) + "/div", true, i == m_scope.time);
	} else if (button == 3) {
		for (int i = 0; i < int(std::size(kVolts)); ++i)
			m.addItem(1 + i, voltText(kVolts[i]) + "/div", true, i == m_scope.volts[0]);
	} else {
		m.addItem(1, "free running", true, m_scope.trigger < 0);
		for (int v = 0; v < kVoices; ++v)
			m.addItem(2 + v, juce::String(kVoiceUpper[v]) + " TRIG", true, m_scope.trigger == v);
	}
	m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(scopeHot(button).toNearestInt())),
	                [safe = juce::Component::SafePointer<HardwareView>(this), button](int r) {
		                if (safe == nullptr || r == 0)
			                return;
		                auto &v = *safe;
		                if (button == 0) {
			                juce::String where, label;
			                if (r >= 100 && r < 110)
				                where = juce::String("mix.m") + kVoiceLower[r - 100];
			                else if (r >= 200 && r < 210)
				                where = juce::String("trig.") + kVoiceLower[r - 200];
			                else if (r == 300 || r == 301)
				                where = r == 300 ? "mix.l" : "mix.r";
			                else if (r > 400 && r < 1000)
				                where = "acc." + juce::String(r - 400);
			                label = probeText(where);
			                if (r >= 1000) {
				                const auto &pt = VoiceBoard::parts()[size_t((r - 1000) / 8)];
				                const auto &pin = pt.pins[size_t((r - 1000) % 8)];
				                where = pin.second;
				                label = juce::String(pt.ref) + " " + pin.first;
			                }
			                v.m_scope.label[0] = where.isEmpty() ? juce::String() : label;
			                v.m_proc.setProbe(0, where);
		                } else if (button == 2) {
			                v.m_scope.time = r - 1;
		                } else if (button == 3) {
			                v.m_scope.volts[0] = r - 1;
		                } else {
			                v.m_scope.trigger = r - 2;
		                }
		                v.m_scope.lastTrigger = 0;
		                v.repaint();
	                });
}

// The notes' adjustment: the scope on the clap's MULTI OUT, triggered by its TRIG, at the scale of
// SN p.9's figure (500 mV/div, 10 ms/div), the clap played unaccented (velocity 50, the
// sequencer's normal level) every 0.6 s.
void HardwareView::setUpTm()
{
	m_proc.setProbe(0, "mix.mhc");
	m_proc.setProbe(1, "");
	m_scope.label = {};
	m_scope.time = 6;
	m_scope.volts[0] = 3;
	m_scope.trigger = int(Voice::HC);
	m_scope.test = true;
	m_scope.lastTrigger = 0;
	repaint();
}

// ====================================================================== hits and words

HardwareView::Hit HardwareView::hitService(juce::Point<float> px) const
{
	if (px.x >= kCanvasW) {
		for (int i : {0, 2, 3, 5})
			if (scopeHot(i).contains(px))
				return {HitKind::Scope, i};
		if (testPill().contains(px))
			return {HitKind::TrimButton, 1};
		if (trimRow().contains(px))
			return {HitKind::Trim, 0};
		const int ranges = int(boardSerials().size());
		for (int i = 0; i < ranges; ++i)
			if (revRow(i).contains(px))
				return {HitKind::Serial, i};
		return {HitKind::Drawer, -1};
	}
	const auto mm = px.transformedBy(underside(m_zoom, m_pan).inverted());
	if (const auto *tm = tmPart())
		if (mm.getDistanceFrom(centreOf(*tm)) < 4.0f)
			return {HitKind::Trim, 1};
	// the knobs on their pots and the keys on their switches work as on the panel
	const auto &all = elements();
	for (int i : m_knobs) {
		const auto &e = all[size_t(i)];
		const auto c = e.centre + juce::Point<float>(0, onSwitchBoard(e) ? kSwitchDy : 0.0f);
		if (mm.getDistanceFrom(c) < float(double(e.v["base_d"].isVoid() ? 11.6 : double(e.v["base_d"]))) / 2 + 0.8f)
			return {HitKind::Knob, i};
	}
	for (size_t k = 0; k < m_keys.size(); ++k)
		if (all[size_t(m_keys[k].element)].box.translated(0, kSwitchDy).expanded(0.5f).contains(mm))
			return {HitKind::Key, int(k)};
	return {HitKind::Board, -1};
}

juce::String HardwareView::serviceStatus() const
{
	if (m_hover.kind == HitKind::Trim)
		return "TM, the hand clap's trimmer: drag across (shift: finer), double-click: back to the calibrated unit's trim";
	if (m_hover.kind == HitKind::Serial)
		return "the circuit as Roland built it from that serial number on (SN p.7)";
	if (m_hover.kind == HitKind::Knob || m_hover.kind == HitKind::Key)
		return "the panel's controls, on the boards: they work as on the panel";
	return "SN " + juce::String(m_proc.serial()) + (m_proc.serial() >= kLateSerial ? " up" : "") + "   wheel: zoom, drag: move";
}

} // namespace tr909
