// SERVICE on the plugin bar: the TR-808 with its top panel off, the boards' component sides up, as
// the TR-909's SERVICE shows that machine. The main board and the voicing board hang side by side on
// their pots, the switch board folded out in front of them on its harness (so that their front edges
// show), and every part is where the service notes' board drawings put it (p.10-p.12, read off into
// docs/boards.json; docs/boards.md), drawn as the part it is: resistors banded by their netlist
// values, film, ceramic and electrolytic capacitors, glass diodes, TO-92 transistors, the ICs with
// their markings. Jack boards A and B show behind the main board's rear edge, the transformer beside
// its left one; the power supply lies under it, out of sight. The knobs stay on their pots and the
// keys on their switches, as they are when the panel comes off (drawn and played as on the panel),
// the LEDs lit by the firmware. The boards are single-sided: their copper shows through faintly,
// generated from the pads (makeCopper), not Roland's tracks.
//
// On the right, as the 303's and the 909's: the scope with the notes' procedures, two readouts, the
// six trimmers (SN p.14, each on its board too) and the unit's circuit: its serial number (SN p.15's
// changes) and the snare's and clap's PORTION CHANGED (SN p.9), early or late. The view zooms (the
// wheel, a pinch; double-click for the whole) and pans (drag).

#include "HardwareView.h"
#include "Fonts.h"

#include "tr808/core/circuit.h"

#include <TR808Boards.h>

#include <cmath>
#include <map>
#include <tuple>

namespace tr808 {

namespace {
constexpr float kPi = juce::MathConstants<float>::pi;

// ------------------------------------------------------------------ layout
constexpr float kDrawerW = 304;
constexpr float kCanvasW = float(HardwareView::kWidth) - kDrawerW, kCanvasH = float(HardwareView::kPanelHeight);
constexpr float kSwitchDy = 62;  // mm: the switch board folded out in front of the main and voicing boards
constexpr float kMaxZoom = 8;
const juce::Rectangle<float> kContent(20, 4, 470, 346); // panel mm the view fits at zoom 1
const float kFit = std::min(kCanvasW / kContent.getWidth(), kCanvasH / kContent.getHeight());
const juce::Point<float> kOrigin((kCanvasW - kContent.getWidth() * kFit) / 2, (kCanvasH - kContent.getHeight() * kFit) / 2);

// The right-hand side, view px.
constexpr float kIx = kCanvasW + 8, kIw = kDrawerW - 16;
const juce::Rectangle<float> kScopeCase(kIx, 8, kIw, 192), kScopeScreen(kIx + 12, 32, kIw - 24, 118);
const juce::Rectangle<float> kVppCase(kIx, 208, 140, 56), kMeasCase(kIx + kIw - 140, 208, 140, 56);
constexpr float kTrimRowH = 36;
const juce::Rectangle<float> kTrimCase(kIx, 272, kIw, 28 + 6 * kTrimRowH + 4);
const juce::Rectangle<float> kRevCase(kIx, kTrimCase.getBottom() + 8, kIw, float(HardwareView::kPanelHeight) - kTrimCase.getBottom() - 16);
constexpr float kRevRowH = 36;
juce::Rectangle<float> trimRow(int i) { return {kTrimCase.getX() + 6, kTrimCase.getY() + 28 + float(i) * kTrimRowH, kTrimCase.getWidth() - 12, kTrimRowH - 3}; }
juce::Rectangle<float> trimTrack(int i) { return {trimRow(i).getX() + 24, trimRow(i).getY() + 24, trimRow(i).getWidth() - 24 - 92, 4}; }
juce::Rectangle<float> trimPill(int i) { return {trimRow(i).getRight() - 66, trimRow(i).getY() + 3, 60, 16}; }
juce::Rectangle<float> revRow(int i) { return {kRevCase.getX() + 6, kRevCase.getY() + 26 + float(i) * kRevRowH, kRevCase.getWidth() - 12, kRevRowH - 3}; }
juce::Rectangle<float> portionRow() { return {kRevCase.getX() + 6, revRow(3).getY(), kRevCase.getWidth() - 12, 25}; }
juce::Rectangle<float> portionPill(int k)
{
	const auto r = portionRow();
	return {r.getRight() - 4 - float(2 - k) * 78, r.getY() + 4, 74, r.getHeight() - 8};
}
// the scope's own controls: its caption (the probe), the time base, the volts, the trigger
juce::Rectangle<float> scopeHot(int i)
{
	switch (i) {
	case 0: return {kScopeCase.getX() + 8, kScopeCase.getY() + 4, kScopeCase.getWidth() - 100, 20};
	case 2: return {kScopeCase.getRight() - 88, kScopeCase.getY() + 4, 80, 20};
	case 3: return {kScopeScreen.getRight() - 70, kScopeScreen.getY() + 2, 66, 16};
	case 5: return {kScopeScreen.getX() + 4, kScopeScreen.getY() + 2, 110, 16};
	default: return {};
	}
}

const juce::Colour kOrange(0xffff8c1a), kProbe(0xfff5d742), kSilk(0xffe6ece6), kLead(0xffc9cdd2), kInSpec(0xff46c37b);
const juce::Colour kMask(0xff0f6247), kMaskEdge(0xff0a4732), kCopperThrough(0xff0c5640);

juce::Font sans(float px, bool bold = false) { return juce::Font(juce::FontOptions("Helvetica Neue", px, bold ? juce::Font::bold : juce::Font::plain)); }

uint32_t hashOf(const juce::String &s)
{
	uint32_t h = 2166136261u;
	for (auto c : s)
		h = (h ^ uint32_t(c)) * 16777619u;
	return h;
}

// ------------------------------------------------------------------ the trimmers (core/trimmers.h)
// Each with its procedure (SN p.14): the test point, the scope's time base, volts and trigger (-2
// auto: the probe's own rise; -1 free running; a voice's gate), and whether the procedure plays a
// voice (TM3's: "write, play CP at a tempo w/ LEVEL FCW").
struct TrimDef {
	const char *ref, *board, *name, *part, *tp, *probe;
	int time, volts, trigger;
	bool plays;
};
const TrimDef kTrims[kTrimmers] = {
	{"TM1", "main", "INT CLOCK", "100k(B)", "TP2", "tp2", 2, 5, -2, false},
	{"TM2", "main", "TEMPO CLOCK", "10k(B)", "TP3", "tp3", 4, 5, -2, false},
	{"TM3", "main", "C.P. OFFSET", "10k(B)", "TP5", "cp.o19", 6, 4, int(Voice::CP), true},
	{"TM4", "main", "W.N. LEVEL", "22k(B)", "TP4", "wn", 3, 2, -1, false},
	{"TM1", "voicing", "COWBELL FREQ.1", "220k(B)", "TP6", "o1", 2, 5, -2, false},
	{"TM2", "voicing", "COWBELL FREQ.2", "220k(B)", "TP7", "o2", 2, 5, -2, false},
};
// the procedure's reading as text; TM3's: its wiper's voltage (its ends at ground and -15 V)
juce::String readingText(int i, double v)
{
	switch (Trimmer(i)) {
	case Trimmer::IntClock:
	case Trimmer::TempoClock:
	case Trimmer::Cowbell1:
	case Trimmer::Cowbell2: return juce::String(v, v < 10 ? 2 : 1) + " ms";
	case Trimmer::Noise: return juce::String(juce::roundToInt(v)) + " mV";
	case Trimmer::CpOffset: return juce::String(-15.0 * (1 - v), 2) + " V";
	}
	return {};
}

// ------------------------------------------------------------------ the boards (docs/boards.json)
enum class Kind { R, C, D, Q, Ic, Vr, Tm, Cn, Sw, Led, Jumper, Tp, Ift, Other };

struct Board {
	juce::String id, name, number;
	juce::Path outline;   // panel mm, as drawn (the switch board folded out)
	float dy = 0;         // where it's drawn: its own place, moved forward by this
	int level = 1;        // 0 nearest the panel (docs/boards.md)
	juce::Path lands;     // the other side's copper, generated (makeCopper): the pads' lands
	juce::Path tracks[3]; // and its tracks, by width (kTrackWidths)
};

struct Part {
	juce::String ref, value; // value: the netlists' ("22k", "0.047u"), "" when they don't draw it
	Kind kind = Kind::Other;
	int board = 0;
	std::vector<juce::Point<float>> pads; // panel mm, as drawn; pin 1 first when it's known
	bool pin1 = false;
	std::vector<int> netlist; // VoiceBoard::parts entries (one, or an IC's sections)
	int trimmer = -1;         // a TM: which of kTrims
};

struct Boards {
	std::vector<Board> boards; // in drawing order: the jack boards, then main and voicing, then the switch board
	std::vector<Part> parts;
	juce::Rectangle<float> transformer;
};

Kind kindOf(const juce::String &ref)
{
	const auto alpha = ref.initialSectionNotContaining("0123456789");
	if (alpha == "R") return Kind::R;
	if (alpha == "C") return Kind::C;
	if (alpha == "D") return Kind::D;
	if (alpha == "Q") return Kind::Q;
	if (alpha == "IC") return Kind::Ic;
	if (alpha == "VR") return Kind::Vr;
	if (alpha == "TM") return Kind::Tm;
	if (alpha == "CN") return Kind::Cn;
	if (alpha == "SW") return Kind::Sw;
	if (alpha == "LED") return Kind::Led;
	if (alpha == "J") return Kind::Jumper;
	if (alpha == "TP") return Kind::Tp;
	if (alpha == "IFT") return Kind::Ift;
	return Kind::Other;
}

// A board's copper, seen from the component side: a land at every pad of boards.json (round, oval
// along x or y as its kind says), and tracks from each pad to its nearest neighbours of other parts
// (a spanning forest, shortest first), routed at 0/45/90 degrees as a board's traces run. Drawn as
// the 303's and the 909's are, not copied: the real tracks are Roland's artwork and stay out of Lemus.
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
	uint32_t seed = 808u + uint32_t(board);
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
		const auto json = juce::JSON::parse(juce::String::fromUTF8(TR808Boards::boards_json, TR808Boards::boards_jsonSize));
		std::map<juce::String, int> index;
		// the power supply lies under the main board, out of sight: not drawn
		for (const char *want : {"jacka", "jackb", "main", "voicing", "switch"})
			if (const auto *list = json["boards"].getArray())
				for (const auto &v : *list) {
					const auto id = v["id"].toString();
					if (id != want)
						continue;
					Board bd;
					bd.id = id;
					bd.name = v["name"].toString();
					bd.number = v["number"].toString().upToFirstOccurrenceOf(" ", false, false);
					bd.level = int(v["level"]);
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
		// the panel's LEDs that are parts on the main board (D76-D79): drawn lit, as the panel's
		std::vector<juce::Point<float>> leds;
		for (const auto &e : HardwareView::elements())
			if (e.type == "led" && !e.id.startsWith("led_step_"))
				leds.push_back(e.centre);
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
				const auto &boardId = out.boards[size_t(p.board)].id;
				// what it is in the circuit: the netlists' parts on this board by its designator (an IC
				// by its sections)
				for (size_t k = 0; k < net.size(); ++k) {
					if (boardId != juce::String(net[k].board))
						continue;
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
				if (p.kind == Kind::D && boardId == "main") {
					juce::Point<float> c;
					for (const auto &q : p.pads)
						c += q;
					c /= float(p.pads.size());
					for (const auto &l : leds)
						if (c.getDistanceFrom(l) < 4)
							p.kind = Kind::Led;
				}
				if (p.kind == Kind::Tm)
					for (int t = 0; t < kTrimmers; ++t)
						if (p.ref == kTrims[t].ref && boardId == kTrims[t].board)
							p.trimmer = t;
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

// The values of the parts the netlists don't draw but the notes print (SN p.8, p.13: the INT and
// tempo clocks, the six squares), and every value a unit's serial number changes (core/revisions.h):
// "" for a part that unit hasn't.
std::map<std::pair<juce::String, juce::String>, juce::String> drawnValues(int serial)
{
	std::map<std::pair<juce::String, juce::String>, juce::String> v = {
		{{"main", "R49"}, "100k"},      {{"main", "R48"}, "10k"},        {{"main", "C11"}, "0.047u"},   {{"main", "C203"}, "0.039u"},
		{{"main", "R38"}, "100k"},      {{"main", "R39"}, "100k"},       {{"main", "R32"}, "560k"},     {{"main", "R40"}, "47k"},
		{{"main", "R41"}, "560"},       {{"main", "R42"}, "10k"},        {{"main", "R43"}, "220k"},     {{"main", "R44"}, "33k"},
		{{"main", "D13"}, "1S2473"},    {{"voicing", "R40"}, "560k"},    {{"voicing", "R41"}, "560k"},  {{"voicing", "R42"}, "680k"},
		{{"voicing", "R43"}, "220k"},   {{"voicing", "R44"}, "150k"},    {{"voicing", "R45"}, "100k"},  {{"voicing", "C1"}, "0.018u"},
		{{"voicing", "C2"}, "0.01u"},   {{"voicing", "C3"}, "0.01u"},    {{"voicing", "C4"}, "0.018u"}, {{"voicing", "C5"}, "0.018u"},
		{{"voicing", "C6"}, "0.022u"},
	};
	auto text = [](double x, bool cap) {
		if (cap)
			return juce::String(x * 1e6, 4).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + "u";
		return x >= 1e6 ? juce::String(x / 1e6, 3).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + "M"
		     : x >= 1e3 ? juce::String(x / 1e3, 3).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + "k"
		                : juce::String(x, 0);
	};
	for (const auto &[k, x] : revisionParts(serial)) {
		const juce::String key(k), owner = key.upToFirstOccurrenceOf(".", false, false), param = key.fromFirstOccurrenceOf(".", false, false);
		const juce::String board = owner == "cbosc" ? "voicing" : "main";
		if (param == "c203" || param == "c202a" || param == "n200") // not fitted
			v[{board, param == "c203" ? "C203" : param == "c202a" ? "C202" : "C200"}] = "";
		else if (param.startsWith("r") || param.startsWith("c"))
			if (param.substring(1).containsOnly("0123456789"))
				v[{board, param.toUpperCase()}] = text(x, param.startsWith("c"));
	}
	return v;
}

// What the parts the netlists don't draw are (SN p.16's parts list, p.8, p.13).
juce::String describe(const juce::String &board, const juce::String &ref)
{
	const int n = ref.retainCharacters("0123456789").getIntValue();
	if (ref.startsWith("IC")) {
		if (board == "voicing")
			return n == 1 ? "HD14584BP hex Schmitt inverter: the six squares (TP6, TP7)" : "uPC4558C dual op-amp";
		static const std::pair<int, const char *> chips[] = {
			{1, "HD14584BP hex Schmitt inverter: the INT clock (TP2), the tempo clock"}, {2, "TC4013BP dual D flip-flop: the tempo clock (TP3), START/STOP"},
			{3, "TC4001BP quad NOR: the status gate"}, {4, "uPD650C-085 CPU"}, {5, "MC14051BCP multiplexer: the RAMs' address decoder"},
			{6, "TC4011BP quad NAND: the common trig's 1 ms monostable"}, {7, "uPD444C 1K x 4 static RAM"}, {8, "uPD444C 1K x 4 static RAM"},
			{9, "uPD444C 1K x 4 static RAM"}, {10, "uPD444C 1K x 4 static RAM"}, {11, "AN6912 quad comparator: the TRIGGER OUTs' one-shots"},
			{22, "BA662A OTA: the clap's VCA"}, {23, "AN6912 quad comparator: the clap's sawtooth envelope"}};
		for (const auto &[k, d] : chips)
			if (k == n)
				return d;
		return "uPC4558C dual op-amp";
	}
	if (ref == "IFT1")
		return "S74230 IFT (yellow): the CPU's clock, 2 us (TP1)";
	return {};
}

// An IC's marking: the netlist's kind of section, else the parts list's.
juce::String marking(const Part &p, const juce::String &board)
{
	if (!p.netlist.empty()) {
		const auto &v = VoiceBoard::parts()[size_t(p.netlist[0])].value;
		if (v == "op-amp") return "uPC4558C";
		if (v == "comparator") return "AN6912";
		if (v == "OTA") return "BA662A";
	}
	return describe(board, p.ref).upToFirstOccurrenceOf(" ", false, false);
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
		auto t = v.upToFirstOccurrenceOf("(", false, false);
		if (t.endsWithChar('M'))
			t = t.dropLastCharacters(1) + "meg";
		return t.isEmpty() || t.containsChar('{') ? -1 : circuit::parseValue(t.toStdString());
	} catch (...) {
		return -1;
	}
}

// Resistor colour codes from the value (E12/E24, gold tolerance); a part with none gets a code from
// its designator.
const juce::uint32 kBand[10] = {0xff1b1b1b, 0xff7a4a22, 0xffd8352a, 0xffee8a22, 0xfff0d23a, 0xff3e9a42, 0xff3159c4, 0xff8a4fc0, 0xff8f8f8f, 0xfff4f4f4};
void bandsFor(const Part &p, const juce::String &value, int out[3])
{
	double ohms = valueOf(value);
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

void drawResistor(juce::Graphics &g, const Part &p, const juce::String &value)
{
	const auto a = p.pads[0], b = p.pads[1];
	const float d = a.getDistanceFrom(b);
	int bands[3];
	bandsFor(p, value, bands);
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

void drawCapacitor(juce::Graphics &g, const Part &p, const juce::String &value)
{
	const auto a = p.pads[0], b = p.pads.size() > 1 ? p.pads[1] : p.pads[0] + juce::Point<float>(2.5f, 0);
	const float d = std::max(2.0f, a.getDistanceFrom(b));
	const auto dir = (b - a) / d;
	const auto m = (a + b) * 0.5f;
	const auto t = juce::AffineTransform::rotation(std::atan2(dir.y, dir.x)).translated(m);
	const double f = valueOf(value);
	const bool electrolytic = f >= 1e-6 || (f < 0 && value.isEmpty() && d < 3.6f);
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

juce::Point<float> centreOf(const Part &p)
{
	juce::Point<float> c;
	for (const auto &q : p.pads)
		c += q;
	return c / float(std::max<size_t>(1, p.pads.size()));
}

// A TO-92 from above: a D over its three leads, the flat along their line.
void drawTransistor(juce::Graphics &g, const Part &p)
{
	const auto c = centreOf(p);
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

// The pads' long axis and spread across it: a DIP's two rows, or a single row.
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

void drawIc(juce::Graphics &g, const Part &p, const juce::String &mark)
{
	const auto rw = rowsOf(p.pads);
	const bool single = rw.gap < 3.0f;
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
	if (mark.isNotEmpty()) {
		juce::Graphics::ScopedSaveState s(g);
		const auto text = mark.startsWith("uP") ? juce::String::fromUTF8("\xc2\xb5") + mark.substring(1) : mark; // NEC's micro sign
		float ang = std::atan2(rw.along.y, rw.along.x);
		if (ang > kPi / 2 || ang < -kPi / 2)
			ang += kPi; // reads left to right, or bottom to top
		g.addTransform(juce::AffineTransform::rotation(ang).translated(rw.c));
		juce::Font f(juce::FontOptions("Helvetica Neue", std::min(1.9f, wid * 0.42f), juce::Font::plain));
		const float avail = len - 2.0f, w = juce::GlyphArrangement::getStringWidth(f, text);
		if (w > avail)
			f.setHorizontalScale(avail / w);
		g.setFont(f);
		g.setColour(juce::Colour(0xffb9bcc1));
		g.drawText(text, juce::Rectangle<float>(len, 2.6f).withCentre({}), juce::Justification::centred, false);
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

// A small pot from above, under its knob: the plastic body, the bracket to its pins (about 12.5 mm
// in front of its shaft on these boards).
void drawPot(juce::Graphics &g, const Part &p)
{
	const auto centre = centreOf(p) - juce::Point<float>(0, 12.5f);
	for (const auto &q : p.pads)
		lead(g, q - juce::Point<float>(0, 2.2f), q, 0.55f);
	juce::Path body;
	body.addRoundedRectangle(centre.x - 4.9f, centre.y - 5.4f, 9.8f, 15.0f, 0.8f);
	shadowed(g, body, 3.0f);
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3c3e43), centre.x - 4.9f, centre.y - 5.4f, juce::Colour(0xff1d1e21), centre.x + 4.9f, centre.y + 9.6f, false));
	g.fillPath(body);
}

// A switch's housing: a rotary switch's round body under its knob, a slide or lever switch's or a
// key switch's box over its pins.
void drawSwitch(juce::Graphics &g, const Part &p)
{
	const auto rw = rowsOf(p.pads);
	juce::Path body;
	if (p.pads.size() >= 12 && std::abs(rw.length - rw.gap) < 0.3f * rw.length) {
		const float r = std::max(rw.length, rw.gap) / 2 + 1.6f;
		body.addEllipse(rw.c.x - r, rw.c.y - r, 2 * r, 2 * r);
	} else if (p.pads.size() <= 4) {
		body.addRoundedRectangle(juce::Rectangle<float>(std::max(7.0f, rw.length + 3.0f), std::max(7.0f, rw.gap + 3.0f)).withCentre(rw.c), 0.8f);
	} else {
		body.addRoundedRectangle(juce::Rectangle<float>(rw.length + 2.4f, rw.gap + 2.4f).withCentre(rw.c), 0.6f);
		body.applyTransform(juce::AffineTransform::rotation(std::atan2(rw.along.y, rw.along.x), rw.c.x, rw.c.y));
	}
	shadowed(g, body, 2.0f);
	const auto bb = body.getBounds();
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xff34363b), bb.getX(), bb.getY(), juce::Colour(0xff1a1b1e), bb.getRight(), bb.getBottom(), false));
	g.fillPath(body);
	g.setColour(juce::Colour(0x18ffffff));
	g.strokePath(body, juce::PathStrokeType(0.2f));
}

// A jumper: a bare tinned wire between its two pads.
void drawJumper(juce::Graphics &g, const Part &p)
{
	if (p.pads.size() < 2) { // a wire's end, soldered (the wires to the voicing board)
		const auto c = p.pads[0];
		g.setColour(juce::Colour(0xffb8bcc0));
		g.fillEllipse(c.x - 0.7f, c.y - 0.7f, 1.4f, 1.4f);
		return;
	}
	g.setColour(juce::Colours::black.withAlpha(0.25f));
	g.drawLine(juce::Line<float>(p.pads[0], p.pads[1]).withShortenedStart(-0.1f), 0.95f);
	g.setColour(juce::Colour(0xffb8bcc0));
	g.drawLine(juce::Line<float>(p.pads[0], p.pads[1]), 0.6f);
	g.setColour(juce::Colours::white.withAlpha(0.35f));
	g.drawLine(juce::Line<float>(p.pads[0] + juce::Point<float>(-0.1f, -0.1f), p.pads[1] + juce::Point<float>(-0.1f, -0.1f)), 0.2f);
}

// A test point: a pin standing in its pad, its loop seen from above.
void drawTestPoint(juce::Graphics &g, const Part &p)
{
	const auto c = p.pads[0];
	juce::Path ring;
	ring.addEllipse(c.x - 0.9f, c.y - 0.9f, 1.8f, 1.8f);
	shadowed(g, ring, 2.0f);
	g.setColour(juce::Colour(0xffd9dde2));
	g.strokePath(ring, juce::PathStrokeType(0.45f));
}

// The CPU's clock coil: a square can, its core's yellow top.
void drawIft(juce::Graphics &g, const Part &p)
{
	const auto c = centreOf(p);
	juce::Path can;
	can.addRoundedRectangle(c.x - 5, c.y - 5, 10, 10, 0.8f);
	shadowed(g, can, 4.0f);
	g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd5d9de), c.x - 5, c.y - 5, juce::Colour(0xff8c9198), c.x + 5, c.y + 5, false));
	g.fillPath(can);
	g.setColour(juce::Colour(0xff2a2b2e));
	g.fillEllipse(c.x - 2.4f, c.y - 2.4f, 4.8f, 4.8f);
	g.setColour(juce::Colour(0xffe8c42a));
	g.fillEllipse(c.x - 1.9f, c.y - 1.9f, 3.8f, 3.8f);
	g.setColour(juce::Colour(0xff6d5a10));
	g.drawLine(c.x - 1.3f, c.y, c.x + 1.3f, c.y, 0.35f);
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

// The panel's elements on the switch board (the step keys and their LEDs) move with it.
bool onSwitchBoard(const HardwareView::Element &e)
{
	return (e.type == "key" && e.param.startsWith("step")) || (e.type == "led" && e.id.startsWith("led_step_"));
}

// ------------------------------------------------------------------ the scope's words
const float kTimes[] = {0.1f, 0.2f, 0.5f, 1, 2, 5, 10, 20, 50, 100, 200}; // ms/div
const float kVolts[] = {0.05f, 0.1f, 0.2f, 0.5f, 1, 2, 5, 10};            // V/div
const char *const kVoiceUpper[kVoices] = {"BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH"};
const char *const kVoiceLower[kVoices] = {"bd", "sd", "lt", "mt", "ht", "rs", "cp", "cb", "cy", "oh", "ch"};
// the instrument data lines, as the CPU puts them out (SN p.3)
const char *const kDataPin[kVoices] = {"PF2", "PF1", "PF0", "PE3", "PE2", "PE1", "PE0", "PD3", "PD2", "PD1", "PD0"};

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
	for (const auto &d : kTrims)
		if (where == d.probe)
			return d.tp;
	for (int v = 0; v < kVoices; ++v) {
		if (where == juce::String("jack.") + kVoiceLower[v])
			return juce::String(kVoiceUpper[v]) + " MULTI OUT";
		if (where == juce::String("data.") + kVoiceLower[v])
			return juce::String(kVoiceUpper[v]) + " DATA (" + kDataPin[v] + ")";
	}
	if (where == "out" || where == "mix.lo")
		return juce::String("MASTER OUT ") + (where == "out" ? "HI" : "LO");
	if (where == "acc")
		return "ACCENT DATA (PF3)";
	if (where == "trig")
		return "COMMON TRIG";
	if (where == "ntrig")
		return "IC2a: THE NARROWED TRIG";
	if (where == "mono")
		return "IC6 PIN 10: THE 1 MS";
	const auto &ix = partIndex();
	const auto it = ix.byNode.find(where.toStdString());
	if (it != ix.byNode.end() && !it->second.empty()) {
		const auto &pt = VoiceBoard::parts()[size_t(it->second[0].first)];
		return juce::String(pt.ref) + " " + pt.pins[size_t(it->second[0].second)].first;
	}
	return where;
}

// What the procedure on a probe reads: a period (ms) on the squares and clocks, AC volts (mV rms) on
// the noise, peak to peak (V) on the clap.
enum class Measure { None, Period, Rms, PeakPeak };
Measure measureOf(const juce::String &probe)
{
	if (probe == "tp2" || probe == "tp3" || probe == "o1" || probe == "o2")
		return Measure::Period;
	if (probe == "wn")
		return Measure::Rms;
	if (probe == "cp.o19")
		return Measure::PeakPeak;
	return Measure::None;
}

// What each serial range is: the changes it has, plainly.
juce::String revisionWords(int index)
{
	switch (index) {
	case 0: return "INT C11 .068, R49 150k; cowbell C6 .01, R44 390k, R45 330k";
	case 1: return "CP R346 1k, R332 22k; the first noise gen.; gates 22k; R101 (not modelled)";
	default: return "the late board: as Fischer's SN 103852";
	}
}
} // namespace

// ====================================================================== geometry

juce::AffineTransform HardwareView::underside(float zoom, juce::Point<float> pan) const
{
	// panel mm -> view px: the boards seen from above, the view fitted to kContent at zoom 1
	const float s = kFit * zoom;
	return juce::AffineTransform::translation(-kContent.getPosition()).scaled(s).translated(kOrigin + pan);
}

juce::Point<float> HardwareView::panelMm(juce::Point<float> px) const
{
	return m_service ? px.transformedBy(underside(m_zoom, m_pan).inverted()) : px / kPxPerMm;
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

void HardwareView::showService(bool on)
{
	m_service = on;
	if (!on) { // the scope off with the service view
		m_proc.setProbe(0, "");
		m_proc.setProbe(1, "");
		m_scope.test = false;
		m_scope.procedure = -1;
	}
	m_hover = {};
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
	const auto values = drawnValues(m_proc.serial());
	const bool detail = std::sqrt(std::abs(t.getDeterminant())) > 5.0f; // px per mm: designators when legible
	juce::Graphics::ScopedSaveState s(g);
	g.addTransform(t);
	// the parts drawn on each board, in passes: flat ones first so the tall ones' shadows fall on them
	auto drawn = [&](const Part &p) {
		const auto it = values.find({all.boards[size_t(p.board)].id, p.ref});
		return it != values.end() ? std::make_pair(true, it->second) : std::make_pair(false, p.value);
	};
	auto drawParts = [&](int board) {
		auto pass = [&](auto pred) {
			for (const auto &p : all.parts) {
				if (p.board != board || !pred(p.kind))
					continue;
				const auto [known, value] = drawn(p);
				if (known && value.isEmpty())
					continue; // not fitted on this unit
				const auto &boardId = all.boards[size_t(p.board)].id;
				switch (p.kind) {
				case Kind::R: if (p.pads.size() >= 2) drawResistor(g, p, value); break;
				case Kind::D: if (p.pads.size() >= 2) drawDiode(g, p); break;
				case Kind::C: drawCapacitor(g, p, value); break;
				case Kind::Q: drawTransistor(g, p); break;
				case Kind::Ic: if (p.pads.size() >= 3) drawIc(g, p, marking(p, boardId)); break;
				case Kind::Cn: if (p.pads.size() >= 2) drawConnector(g, p); break;
				case Kind::Vr: drawPot(g, p); break;
				case Kind::Sw: drawSwitch(g, p); break;
				case Kind::Jumper: drawJumper(g, p); break;
				case Kind::Tp: drawTestPoint(g, p); break;
				case Kind::Ift: drawIft(g, p); break;
				default: break;
				}
			}
		};
		pass([](Kind k) { return k == Kind::Sw || k == Kind::R || k == Kind::D || k == Kind::Jumper || k == Kind::Tp; });
		pass([](Kind k) { return k == Kind::Ic || k == Kind::Cn; });
		pass([](Kind k) { return k == Kind::C || k == Kind::Q || k == Kind::Vr || k == Kind::Ift; });
	};
	auto drawBoard = [&](const Board &b, int index) {
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
		const bool rear = b.id.startsWith("jack"); // the jack boards' names show at their rear edge
		g.drawText(b.name + "  " + b.number, juce::Rectangle<float>(box.getX() + 4, rear ? box.getY() + 1.5f : box.getBottom() - 5.5f, 120, 4),
		           juce::Justification::centredLeft, false);
		drawParts(index);
	};
	// below: the jack boards behind the main board's rear edge, the transformer on the chassis
	// floor beside its left edge; then the main and voicing boards; the switch board folded out
	for (size_t i = 0; i < all.boards.size(); ++i)
		if (all.boards[i].level == 2)
			drawBoard(all.boards[i], int(i));
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
	for (int level : {1, 0})
		for (size_t i = 0; i < all.boards.size(); ++i)
			if (all.boards[i].level == level)
				drawBoard(all.boards[i], int(i));
	if (detail) {
		// the designators, beside each part; the test points' always
		g.setFont(sans(1.35f, true));
		g.setColour(kSilk.withAlpha(0.85f));
		for (const auto &p : all.parts) {
			if (p.kind == Kind::Sw || p.kind == Kind::Led || p.kind == Kind::Vr || p.kind == Kind::Jumper || p.kind == Kind::Tm)
				continue;
			const auto c = centreOf(p);
			float off = p.kind == Kind::Ift ? 5.8f : p.kind == Kind::C && valueOf(drawn(p).second) >= 1e-6 ? 3.6f : 2.2f;
			if (p.kind == Kind::Ic) { // over its pins' end, clear of its marking
				float top = c.y;
				for (const auto &q : p.pads)
					top = std::min(top, q.y);
				off = c.y - top + 1.8f;
			}
			g.drawText(p.ref, juce::Rectangle<float>(12, 1.6f).withCentre(c + juce::Point<float>(0, -off - 0.4f)), juce::Justification::centred, false);
		}
	} else {
		g.setFont(sans(2.2f, true));
		g.setColour(kSilk.withAlpha(0.9f));
		for (const auto &p : all.parts)
			if (p.kind == Kind::Tp)
				g.drawText(p.ref, juce::Rectangle<float>(10, 2.4f).withCentre(p.pads[0] + juce::Point<float>(0, -2.4f)), juce::Justification::centred, false);
	}
}

// ====================================================================== paint

void HardwareView::paintService(juce::Graphics &g)
{
	const float phys = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
	const bool settled = juce::Time::getMillisecondCounter() - m_viewChangedAt >= 150 && !m_panning;
	if (!m_serviceBase.isValid() || (std::abs(phys - m_serviceBaseScale) > 0.01f && juce::Time::getMillisecondCounter() - m_resizedAt >= 200)) {
		m_serviceBaseScale = phys;
		m_serialShown = m_proc.serial();
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
	// what moves: the knobs on their pots, the switches' levers and sliders, the keys, the POWER
	// button, the LEDs as the firmware has them, and the trimmers' rotors
	const auto t = underside(m_zoom, m_pan);
	const auto &all = elements();
	for (int pass = 0; pass < 2; ++pass) {
		juce::Graphics::ScopedSaveState e(g);
		g.addTransform(pass == 1 ? juce::AffineTransform::translation(0, kSwitchDy).followedBy(t) : t);
		for (size_t i = 0; i < all.size(); ++i) {
			const auto &e2 = all[i];
			if (onSwitchBoard(e2) != (pass == 1))
				continue;
			const bool hover = m_hover.index == int(i) || m_drag.index == int(i);
			if (e2.type == "knob")
				drawKnob(g, e2, paramValue(e2.param), hover);
			else if (e2.type == "dial")
				drawDial(g, e2, paramValue(e2.param), hover);
			else if (e2.type == "rotary_switch")
				drawRotary(g, e2, choiceIndex(e2.param), hover);
			else if (e2.type == "slide_switch")
				drawSlide(g, e2, e2.param == "prescale" ? choiceIndex(e2.param) : (paramValue(e2.param) > 0.5f ? 0 : 1), hover);
			else if (e2.type == "lever_switch")
				drawLever(g, e2, choiceIndex(e2.param), hover);
			else if (e2.type == "key")
				drawKey(g, e2, m_held.down(int(i)), 0, hover);
			else if (e2.type == "button" && e2.param == "power")
				drawPower(g, e2, m_proc.powered(), hover);
		}
		for (const auto &e2 : all)
			if (e2.type == "led" && onSwitchBoard(e2) == (pass == 1))
				drawLed(g, e2, ledLevel(e2));
	}
	{
		juce::Graphics::ScopedSaveState e(g);
		g.addTransform(t);
		for (const auto &p : boards().parts) {
			if (p.trimmer < 0)
				continue;
			const auto c = centreOf(p);
			drawTrimmerAt(g, c, 3.0f, paramValue(TR808Processor::kTrimIds[p.trimmer]));
			const bool hot = (m_hover.kind == HitKind::Trim || m_hover.kind == HitKind::Trimmer || m_drag.kind == HitKind::Trim || m_drag.kind == HitKind::Trimmer)
			                 && (m_hover.index == p.trimmer || m_drag.index == p.trimmer);
			// its label as the board prints it, and a ring while it's being trimmed
			g.setFont(sans(1.8f, true));
			g.setColour(kSilk);
			g.drawText(p.ref, juce::Rectangle<float>(8, 2.2f).withCentre(c + juce::Point<float>(0, -4.4f)), juce::Justification::centred, false);
			if (hot || m_scope.procedure == p.trimmer) {
				g.setColour(hot ? kOrange : kProbe);
				g.drawEllipse(c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f, 0.45f);
			}
		}
		// the probe's clip on its test point, while a procedure is set up (TP4 isn't drawn on the
		// board: on its net, IC24a's output, pin 1)
		if (m_scope.procedure >= 0) {
			const auto &d = kTrims[m_scope.procedure];
			const Part *at = nullptr;
			for (const auto &p : boards().parts)
				if (boards().boards[size_t(p.board)].id == d.board && (p.ref == d.tp || (at == nullptr && juce::String(d.tp) == "TP4" && p.ref == "IC24")))
					at = &p;
			if (at != nullptr) {
				const auto c = at->pads[0];
				g.setColour(kProbe);
				g.fillEllipse(c.x - 1.1f, c.y - 1.1f, 2.2f, 2.2f);
				g.drawLine(c.x, c.y, c.x + 6, c.y - 9, 0.6f);
			}
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
		const auto &lo = m_scope.lo, &hi = m_scope.hi;
		if (!lo.empty() && m_proc.probe(0).isNotEmpty()) {
			const float perV = sc.getHeight() / 8 / kVolts[m_scope.volts];
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
	g.drawText(m_scope.trigger == -1 ? juce::String("free run") : m_scope.trigger == -2 ? juce::String("auto") : juce::String("trig ") + kVoiceUpper[m_scope.trigger],
	           scopeHot(5), juce::Justification::centredLeft);
	g.setColour(hovV ? kOrange : juce::Colour(0xff4f9a6a));
	g.drawText(voltText(kVolts[m_scope.volts]) + "/div", scopeHot(3), juce::Justification::centredRight);
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
	auto pill = [&](juce::Rectangle<float> r, const juce::String &text, bool on, bool hov) {
		g.setColour(on ? juce::Colour(0xff2a1e12) : hov ? juce::Colour(0xff2c323b) : juce::Colour(0xff23272e));
		g.fillRoundedRectangle(r, 5);
		g.setColour(on ? kOrange : juce::Colour(0xff3a414c));
		g.drawRoundedRectangle(r.reduced(0.5f), 5, 1);
		g.setFont(fonts::mono(9.0f, fonts::Weight::Medium));
		g.setColour(on ? kOrange : juce::Colour(0xffc9cfd8));
		g.drawFittedText(text, r.toNearestInt(), juce::Justification::centred, 1, 0.8f);
	};
	// the scope, on what its probe is on
	card(kScopeCase);
	const auto probe = m_proc.probe(0);
	caption(kScopeCase, kProbe, probe.isEmpty() ? juce::String("SCOPE  probe off") : "SCOPE  on " + (m_scope.label.isNotEmpty() ? m_scope.label : probeText(probe)),
	        timeText(kTimes[m_scope.time]) + "/div", m_hover.kind == HitKind::Scope && m_hover.index == 0, m_hover.kind == HitKind::Scope && m_hover.index == 2);
	drawScope(g);
	juce::String hint;
	if (m_scope.procedure >= 0) {
		// the procedure's words (SN p.14), and what it asks of the knobs, said rather than done
		const int i = m_scope.procedure;
		const double target = i == 0 ? 1.9 : i == 1 ? 8.33 : i == 4 ? 1.85 : i == 5 ? 1.25 : 0.0;
		const double lo = m_proc.trimReading(i, 0), hi = m_proc.trimReading(i, 1);
		const bool reach = target <= 0 || (target >= std::min(lo, hi) && target <= std::max(lo, hi));
		switch (Trimmer(i)) {
		case Trimmer::IntClock: hint = "TP2, IC1's INT clock: TM1 for 1.9 ms a cycle."; break;
		case Trimmer::TempoClock: {
			const float tempo = paramValue("tempo"), fine = paramValue("fine");
			hint = "TP3: TEMPO and FINE fully clockwise, TM2 for 8.33 ms; then TEMPO fully anticlockwise reads 65 +-5 ms.";
			if (tempo < 0.999f || fine < 0.999f)
				hint << " TEMPO is at " << juce::String(tempo * 10, 1) << ", FINE at " << juce::String(fine * 10, 1) << ".";
			break;
		}
		case Trimmer::CpOffset: {
			hint = "TP5, CP played: TM3 for SN p.14's picture, 6 Vp-p, a 0.5 Vp-p step at its start.";
			if (paramValue("cp_level") < 0.999f)
				hint << " CP LEVEL is at " << juce::String(paramValue("cp_level") * 10, 1) << ": the notes have it fully clockwise.";
			break;
		}
		case Trimmer::Noise: hint = "TP4 on an AC voltmeter: TM4 for 130 mV rms."; break;
		case Trimmer::Cowbell1: hint = "TP6, the cowbell's first square: TM1 for 1.85 ms."; break;
		case Trimmer::Cowbell2: hint = "TP7, the second: TM2 for 1.25 ms."; break;
		}
		if (!reach)
			hint << " This board's " << kTrims[i].ref << " can't reach it (" << readingText(i, std::min(lo, hi)) << " to "
			     << readingText(i, std::max(lo, hi)) << (i >= 4 ? "; C5's early value isn't known" : "") << ").";
	} else {
		hint = probe.isEmpty() ? "Click the caption to clip the probe on: a jack, a trigger line, a test point, any part's pin. SET UP: a trimmer's procedure."
		                       : "Click the caption to move the probe, the time base or the trigger to change them.";
	}
	g.setFont(sans(10.5f));
	g.setColour(juce::Colour(0xffaab2bd));
	g.drawFittedText(hint, juce::Rectangle<float>(kIx + 12, kScopeScreen.getBottom() + 3, kIw - 24, 38).toNearestInt(), juce::Justification::centredLeft, 3, 0.9f);

	// the readouts: the probe's peak to peak, and what the procedure reads
	auto readout = [&](juce::Rectangle<float> c, juce::Colour bg, juce::Colour fg, const juce::String &digits, const juce::String &unit) {
		const juce::Rectangle<float> win(c.getX() + 10, c.getY() + 22, c.getWidth() - 20, 27);
		g.setColour(bg);
		g.fillRoundedRectangle(win, 3);
		g.setColour(fg);
		g.setFont(fonts::mono(18.0f, fonts::Weight::SemiBold));
		g.drawText(digits, win.withTrimmedRight(unit.length() > 2 ? 46 : 26), juce::Justification::centredRight);
		g.setFont(fonts::mono(11.0f, fonts::Weight::Medium));
		g.drawText(unit, win.withTrimmedRight(6), juce::Justification::centredRight);
	};
	card(kVppCase);
	caption(kVppCase, kProbe, "PEAK-PEAK", "CH1");
	readout(kVppCase, juce::Colour(0xff160707), juce::Colour(0xffff3b2f), probe.isEmpty() ? juce::String("----") : juce::String(m_scope.pp, 2), "Vp-p");
	card(kMeasCase);
	{
		const auto m = measureOf(probe);
		const juce::String what = m == Measure::Period ? "PERIOD" : m == Measure::Rms ? "AC VOLTS" : m == Measure::PeakPeak ? "PEAK-PEAK" : "MEASURE";
		caption(kMeasCase, juce::Colour(0xff46c37b), what, m == Measure::None ? juce::String() : probeText(probe));
		const bool have = m != Measure::None && m_scope.reading > 0;
		readout(kMeasCase, juce::Colour(0xff0b1710), juce::Colour(0xff7dffa8),
		        !have ? juce::String("----") : m == Measure::Rms ? juce::String(juce::roundToInt(m_scope.reading)) : juce::String(m_scope.reading, m_scope.reading < 10 ? 2 : 1),
		        m == Measure::Period ? "ms" : m == Measure::Rms ? "mVrms" : m == Measure::PeakPeak ? "Vp-p" : "");
	}

	// the six trimmers: each on its board, its reading, its procedure
	card(kTrimCase);
	g.setFont(fonts::mono(10.5f, fonts::Weight::SemiBold));
	g.setColour(juce::Colour(0xffc9cfd8));
	g.drawText("TRIMMERS", juce::Rectangle<float>(kTrimCase.getX() + 14, kTrimCase.getY() + 6, 120, 18), juce::Justification::centredLeft);
	g.setFont(fonts::mono(10.0f));
	g.setColour(juce::Colour(0xff8a929e));
	g.drawText("SN p.14  tick: the unit's trim", juce::Rectangle<float>(kTrimCase.getX() + 14, kTrimCase.getY() + 6, kTrimCase.getWidth() - 28, 18),
	           juce::Justification::centredRight);
	for (int i = 0; i < kTrimmers; ++i) {
		const auto row = trimRow(i);
		const auto &d = kTrims[i];
		const bool hot = ((m_hover.kind == HitKind::Trim || m_hover.kind == HitKind::Trimmer) && m_hover.index == i)
		                 || ((m_drag.kind == HitKind::Trim || m_drag.kind == HitKind::Trimmer) && m_drag.index == i);
		const bool picked = m_scope.procedure == i;
		g.setColour(hot || picked ? juce::Colour(0xff2b2620) : juce::Colour(0xff21252b));
		g.fillRoundedRectangle(row, 5);
		if (hot || picked) {
			g.setColour(kOrange.withAlpha(0.7f));
			g.drawRoundedRectangle(row.reduced(0.5f), 5, 1);
			g.setColour(kOrange);
			g.fillRoundedRectangle(row.withWidth(3).reduced(0, 6), 1.5f);
		}
		const float v = paramValue(TR808Processor::kTrimIds[i]), unit = float(m_proc.unitTrim(i));
		const bool atUnit = std::abs(v - unit) < 1e-4f;
		const float y1 = row.getY() + 3;
		g.setColour(atUnit ? kInSpec : kOrange);
		g.fillEllipse(row.getX() + 9, y1 + 5, 7, 7);
		g.setFont(fonts::mono(11.0f, fonts::Weight::SemiBold));
		g.setColour(juce::Colour(0xffeef1f5));
		g.drawText(d.ref, juce::Rectangle<float>(row.getX() + 21, y1, 34, 16), juce::Justification::centredLeft);
		g.setFont(sans(10.5f));
		g.setColour(juce::Colour(0xffc9cfd8));
		g.drawText(d.name, juce::Rectangle<float>(row.getX() + 52, y1, 110, 16), juce::Justification::centredLeft, true);
		g.setColour(juce::Colour(0xff8f98a5));
		g.drawText(juce::String(d.board), juce::Rectangle<float>(row.getX() + 52 + 104, y1, 50, 16), juce::Justification::centredLeft, true);
		pill(trimPill(i), d.plays ? (picked && m_scope.test ? "TESTING" : "TEST") : "SET UP", picked,
		     m_hover.kind == HitKind::TrimButton && m_hover.index == i);
		const auto track = trimTrack(i);
		g.setColour(juce::Colour(0xff3a414c));
		g.fillRoundedRectangle(track, 2);
		g.setColour(juce::Colour(0xffc9cfd8));
		const float fx = track.getX() + unit * track.getWidth();
		g.drawLine(fx, track.getY() - 4, fx, track.getBottom() + 4, 1.2f);
		drawTrimmerAt(g, {track.getX() + v * track.getWidth(), track.getCentreY()}, 6.5f, v);
		g.setFont(fonts::mono(11.0f, fonts::Weight::Medium));
		g.setColour(atUnit ? juce::Colour(0xffe9ecf1) : juce::Colour(0xffffb866));
		g.drawText(readingText(i, m_proc.trimReading(i, v)), juce::Rectangle<float>(row.getRight() - 90, row.getY() + 18, 84, 14),
		           juce::Justification::centredRight);
	}

	// the circuit: which unit this is, by its serial number (SN p.15), and the snare's and clap's
	// portion
	card(kRevCase);
	g.setFont(fonts::mono(10.5f, fonts::Weight::SemiBold));
	g.setColour(juce::Colour(0xffc9cfd8));
	g.drawText("CIRCUIT", juce::Rectangle<float>(kRevCase.getX() + 14, kRevCase.getY() + 6, 120, 18), juce::Justification::centredLeft);
	g.setFont(fonts::mono(10.0f));
	g.setColour(juce::Colour(0xff8a929e));
	g.drawText("serial number  (SN p.15)", juce::Rectangle<float>(kRevCase.getX() + 14, kRevCase.getY() + 6, kRevCase.getWidth() - 28, 18),
	           juce::Justification::centredRight);
	const auto serials = boardSerials();
	auto sn = [](int n) { return juce::String(n).paddedLeft('0', 6); };
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
		g.fillEllipse(row.getX() + 11, row.getY() + 7, 7, 7);
		g.setFont(fonts::mono(11.0f, fonts::Weight::SemiBold));
		g.setColour(juce::Colour(0xffeef1f5));
		g.drawText(to == 0 ? "SN " + sn(from) + " up" : "SN " + sn(from) + " - " + sn(to), juce::Rectangle<float>(row.getX() + 24, row.getY() + 2, row.getWidth() - 30, 16),
		           juce::Justification::centredLeft);
		if (on) {
			g.setFont(fonts::mono(10.0f));
			g.setColour(juce::Colour(0xff8a929e));
			g.drawText("this: " + sn(m_proc.serial()), juce::Rectangle<float>(row.getX() + 24, row.getY() + 2, row.getWidth() - 32, 16), juce::Justification::centredRight);
		}
		g.setFont(sans(10.0f));
		g.setColour(on ? juce::Colour(0xffd6dbe2) : juce::Colour(0xff8f98a5));
		g.drawFittedText(revisionWords(i), juce::Rectangle<float>(row.getX() + 24, row.getY() + 17, row.getWidth() - 30, 14).toNearestInt(),
		                 juce::Justification::centredLeft, 1, 0.8f);
	}
	const auto portion = portionRow();
	g.setColour(juce::Colour(0xff21252b));
	g.fillRoundedRectangle(portion, 5);
	g.setFont(fonts::mono(10.5f, fonts::Weight::SemiBold));
	g.setColour(juce::Colour(0xffeef1f5));
	g.drawText("SNARE + CLAP", juce::Rectangle<float>(portion.getX() + 10, portion.getY(), 100, portion.getHeight()), juce::Justification::centredLeft);
	for (int c = 0; c < 2; ++c)
		pill(portionPill(c), c == 0 ? "EARLY" : "LATE", (c == 1) == m_proc.portionChanged(), m_hover.kind == HitKind::Portion && m_hover.index == c);
}

// The scope's sweep: the latest whole one after a trigger, each pixel column's min and max; and
// what the procedure on the probe reads.
void HardwareView::updateScope()
{
	const int cols = int(kScopeScreen.getWidth());
	const double rate = m_proc.probeRate();
	const int frames = std::max(cols, int(std::lround(10 * kTimes[m_scope.time] * 1e-3 * rate)));
	const int pre = frames / 10;
	const uint32_t end = m_proc.probeWritten();
	const auto probe = m_proc.probe(0);
	if (end == 0 || frames * 3 > TR808Processor::kProbeFrames || probe.isEmpty())
		return;
	const int window = std::min(TR808Processor::kProbeFrames / 2, std::max(frames * 2 + int(rate * 0.7), int(rate * 0.6)));
	std::vector<float> x(static_cast<size_t>(window));
	m_proc.readProbe(0, x.data(), window, end);
	float lo = 1e9f, hi = -1e9f;
	for (float v : x)
		lo = std::min(lo, v), hi = std::max(hi, v);
	const float mid = (lo + hi) / 2, hyst = (hi - lo) * 0.1f;
	int start = -1;
	if (m_scope.trigger == -1) {
		start = window - frames;
	} else if (m_scope.trigger == -2) {
		// auto: the probe's own latest rise through its middle with a whole sweep after it; free
		// running without one
		bool below = false;
		int last = -1;
		for (int i = 0; i <= window - (frames - pre); ++i) {
			if (x[size_t(i)] < mid - hyst)
				below = true;
			else if (below && x[size_t(i)] >= mid) {
				below = false;
				if (i > pre)
					last = i;
			}
		}
		start = hi - lo > 1e-3f && last >= 0 ? last - pre : window - frames;
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
	if (start >= 0) {
		auto &l = m_scope.lo, &h = m_scope.hi;
		l.assign(size_t(cols), 0.0f);
		h.assign(size_t(cols), 0.0f);
		float mn = 1e9f, mx = -1e9f;
		for (int col = 0; col < cols; ++col) {
			const int a = start + int(int64_t(col) * frames / cols), b = std::max(a + 1, start + int(int64_t(col + 1) * frames / cols));
			float cl = 1e9f, ch = -1e9f;
			for (int i = a; i < b; ++i) {
				cl = std::min(cl, x[size_t(i)]);
				ch = std::max(ch, x[size_t(i)]);
			}
			l[size_t(col)] = cl;
			h[size_t(col)] = ch;
			mn = std::min(mn, cl);
			mx = std::max(mx, ch);
		}
		m_scope.pp = mx - mn;
	}
	// the procedure's reading over the latest 0.6 s (the noise's AC volts over 0.2 s, as a meter
	// averages)
	switch (measureOf(probe)) {
	case Measure::Period: {
		// rises through the middle, each placed between its two samples
		double first = -1, lastAt = -1;
		int count = 0;
		bool below = false;
		for (int i = 1; i < window; ++i) {
			if (x[size_t(i)] < mid - hyst)
				below = true;
			else if (below && x[size_t(i)] >= mid) {
				below = false;
				const float a = x[size_t(i - 1)], b = x[size_t(i)];
				const double at = i - 1 + (b > a ? double(mid - a) / double(b - a) : 1.0);
				if (first < 0)
					first = at;
				lastAt = at;
				++count;
			}
		}
		m_scope.reading = hi - lo > 0.5f && count >= 3 ? (lastAt - first) / (count - 1) / rate * 1000 : 0;
		break;
	}
	case Measure::Rms: {
		const int n = std::min(window, int(rate * 0.2));
		double sum = 0, sq = 0;
		for (int i = window - n; i < window; ++i)
			sum += x[size_t(i)];
		const double mean = sum / n;
		for (int i = window - n; i < window; ++i)
			sq += (x[size_t(i)] - mean) * (x[size_t(i)] - mean);
		m_scope.reading = std::sqrt(sq / n) * 1000;
		break;
	}
	case Measure::PeakPeak: m_scope.reading = m_scope.pp; break;
	case Measure::None: m_scope.reading = 0; break;
	}
	repaint(kScopeCase.getUnion(kMeasCase).toNearestInt());
}

// The scope's pop-ups: the probe (0), the time base (2), the volts (3), the trigger (5).
void HardwareView::scopeMenu(int button)
{
	juce::PopupMenu m;
	const auto now = m_proc.probe(0);
	if (button == 0) {
		m.addItem(1, "off", true, now.isEmpty());
		juce::PopupMenu multi, trig, tps;
		for (int v = 0; v < kVoices; ++v) {
			multi.addItem(100 + v, juce::String(kVoiceUpper[v]) + " MULTI OUT", true, now == juce::String("jack.") + kVoiceLower[v]);
			trig.addItem(200 + v, juce::String(kVoiceUpper[v]) + " DATA (" + kDataPin[v] + ")", true, now == juce::String("data.") + kVoiceLower[v]);
		}
		trig.addItem(220, "ACCENT DATA (PF3)", true, now == "acc");
		trig.addItem(221, "IC6 pin 10: the 1 ms", true, now == "mono");
		trig.addItem(222, "COMMON TRIG", true, now == "trig");
		trig.addItem(223, "IC2a: the narrowed trig (CB, CY, OH, CH)", true, now == "ntrig");
		for (int i = 0; i < kTrimmers; ++i)
			tps.addItem(400 + i, juce::String(kTrims[i].tp) + "  " + kTrims[i].name, true, now == kTrims[i].probe);
		m.addSubMenu("MULTI OUT jacks", multi);
		m.addItem(300, "MASTER OUT HI", true, now == "out");
		m.addItem(301, "MASTER OUT LO", true, now == "mix.lo");
		m.addSubMenu("trigger lines (the instrument data, the common trig)", trig);
		m.addSubMenu("test points (TP2-TP7)", tps);
		// every part the netlists draw: by circuit, by kind, each pin with its node and its neighbours
		m.addSeparator();
		const auto &parts = VoiceBoard::parts();
		const auto &ix = partIndex();
		static const std::pair<const char *, const char *> owners[] = {
			{"bd", "BD"}, {"sd", "SD"}, {"lt", "LT/LC"}, {"mt", "MT/MC"}, {"ht", "HT/HC"}, {"rs", "RS/CL"}, {"cp", "CP"}, {"ma", "MA"},
			{"cb", "CB"}, {"cy", "CY"}, {"oh", "OH, CH"}, {"accent", "ACCENT, COMMON TRIG"}, {"noise", "NOISE GENERATOR"},
			{"mtrig", "IC2a, THE NARROWED TRIG"}, {"metal", "IC3, THE METAL SOURCE"}, {"mix", "MIXER, JACK BOARD B"}};
		static const std::pair<const char *, const char *> kinds[] = {
			{"IC", "ICs"}, {"Q", "transistors"}, {"D", "diodes"}, {"R", "resistors"}, {"C", "capacitors"}, {"VR", "pots"}, {"TM", "trimmers"}};
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
			m.addItem(1 + i, voltText(kVolts[i]) + "/div", true, i == m_scope.volts);
	} else {
		m.addItem(1, "free running", true, m_scope.trigger == -1);
		m.addItem(2, "auto: the probe's own rise", true, m_scope.trigger == -2);
		for (int v = 0; v < kVoices; ++v)
			m.addItem(3 + v, juce::String(kVoiceUpper[v]) + " TRIG (its gate)", true, m_scope.trigger == v);
	}
	m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(scopeHot(button).toNearestInt())),
	                [safe = juce::Component::SafePointer<HardwareView>(this), button](int r) {
		                if (safe == nullptr || r == 0)
			                return;
		                auto &v = *safe;
		                if (button == 0) {
			                juce::String where;
			                if (r >= 100 && r < 100 + kVoices)
				                where = juce::String("jack.") + kVoiceLower[r - 100];
			                else if (r >= 200 && r < 200 + kVoices)
				                where = juce::String("data.") + kVoiceLower[r - 200];
			                else if (r >= 220 && r <= 223)
				                where = r == 220 ? "acc" : r == 221 ? "mono" : r == 222 ? "trig" : "ntrig";
			                else if (r == 300 || r == 301)
				                where = r == 300 ? "out" : "mix.lo";
			                else if (r >= 400 && r < 400 + kTrimmers)
				                where = kTrims[r - 400].probe;
			                juce::String label = probeText(where);
			                if (r >= 1000) {
				                const auto &pt = VoiceBoard::parts()[size_t((r - 1000) / 8)];
				                const auto &pin = pt.pins[size_t((r - 1000) % 8)];
				                where = pin.second;
				                label = juce::String(pt.ref) + " " + pin.first;
			                }
			                v.m_scope.label = where.isEmpty() ? juce::String() : label;
			                v.m_scope.procedure = -1;
			                v.m_scope.test = false;
			                v.m_proc.setProbe(0, where);
		                } else if (button == 2) {
			                v.m_scope.time = r - 1;
		                } else if (button == 3) {
			                v.m_scope.volts = r - 1;
		                } else {
			                v.m_scope.trigger = r == 1 ? -1 : r == 2 ? -2 : r - 3;
		                }
		                v.m_scope.lastTrigger = 0;
		                v.repaint();
	                });
}

// A trimmer's procedure (SN p.14): the probe on its test point, the scope at the scale its reading
// needs, triggered as it needs; TM3's plays the clap every 0.6 s (unaccented: velocity 50).
void HardwareView::setUpTrim(int trimmer)
{
	const auto &d = kTrims[juce::jlimit(0, kTrimmers - 1, trimmer)];
	m_proc.setProbe(0, d.probe);
	m_proc.setProbe(1, "");
	m_scope.label = juce::String(d.tp) + "  " + d.name;
	m_scope.time = d.time;
	m_scope.volts = d.volts;
	m_scope.trigger = d.trigger;
	m_scope.procedure = trimmer;
	m_scope.test = d.plays;
	m_scope.lastTrigger = 0;
	m_scope.reading = 0;
	m_scope.lo.clear();
	m_scope.hi.clear();
	repaint();
}

void HardwareView::setTrim(int trimmer, float rotation)
{
	setParamValue(TR808Processor::kTrimIds[juce::jlimit(0, kTrimmers - 1, trimmer)], rotation);
	repaint();
}

// ====================================================================== hits and words

HardwareView::Hit HardwareView::hitService(juce::Point<float> px) const
{
	if (px.x >= kCanvasW) {
		for (int i : {0, 2, 3, 5})
			if (scopeHot(i).contains(px))
				return {HitKind::Scope, i};
		for (int i = 0; i < kTrimmers; ++i) {
			if (trimPill(i).contains(px))
				return {HitKind::TrimButton, i};
			if (trimRow(i).contains(px))
				return {HitKind::Trim, i};
		}
		const int ranges = int(boardSerials().size());
		for (int i = 0; i < ranges; ++i)
			if (revRow(i).contains(px))
				return {HitKind::Serial, i};
		for (int c = 0; c < 2; ++c)
			if (portionPill(c).contains(px))
				return {HitKind::Portion, c};
		return {HitKind::Drawer, -1};
	}
	const auto mm = px.transformedBy(underside(m_zoom, m_pan).inverted());
	for (const auto &p : boards().parts)
		if (p.trimmer >= 0 && mm.getDistanceFrom(centreOf(p)) < 3.6f)
			return {HitKind::Trimmer, p.trimmer};
	// the panel's controls on the boards work as on the panel
	const auto &all = elements();
	for (size_t i = 0; i < all.size(); ++i) {
		const auto &e = all[i];
		const auto m = onSwitchBoard(e) ? mm - juce::Point<float>(0, kSwitchDy) : mm;
		auto num = [](const juce::var &v, float fallback) { return v.isDouble() || v.isInt() ? float(double(v)) : fallback; };
		if (e.type == "knob" || e.type == "dial") {
			const float r = (e.type == "dial" ? num(e.v["knob"]["knob_d"], 40) : num(e.v["knob_d"], 11)) / 2 + 1.0f;
			if (m.getDistanceFrom(e.centre) < r)
				return {HitKind::Knob, int(i)};
		} else if (e.type == "rotary_switch") {
			if (m.getDistanceFrom(e.centre) < num(e.v["knob_d"], 21) / 2 + 2)
				return {HitKind::Switch, int(i)};
		} else if (e.type == "slide_switch" && e.box.expanded(1.5f).contains(m)) {
			return {HitKind::Slide, int(i)};
		} else if (e.type == "lever_switch" && e.box.expanded(1.5f).contains(m)) {
			return {HitKind::Lever, int(i)};
		} else if (e.type == "key" && e.box.contains(m)) {
			return {HitKind::Key, int(i)};
		} else if (e.type == "button" && e.param == "power" && e.box.expanded(1.2f).contains(m)) {
			return {HitKind::Power, int(i)};
		}
	}
	return {HitKind::Board, -1};
}

juce::String HardwareView::serviceStatus() const
{
	if (m_hover.kind == HitKind::Trim || m_hover.kind == HitKind::Trimmer || m_hover.kind == HitKind::TrimButton) {
		const auto &d = kTrims[m_hover.index];
		if (m_hover.kind == HitKind::TrimButton)
			return juce::String(d.ref) + " " + d.name + ": the notes' procedure, the probe on " + d.tp + (d.plays ? ", CP played" : "");
		return juce::String(d.ref) + " " + d.part + " (" + d.board + ") " + d.name + ": drag across (shift: finer), double-click: back to the unit's trim";
	}
	if (m_hover.kind == HitKind::Serial)
		return "the circuit as Roland built it from that serial number on (SN p.15)";
	if (m_hover.kind == HitKind::Portion)
		return "the snare and clap as first drawn, or with the later values of the notes' PORTION CHANGED box (SN p.9, no serial given)";
	if (m_hover.kind == HitKind::Knob || m_hover.kind == HitKind::Key || m_hover.kind == HitKind::Switch || m_hover.kind == HitKind::Slide
	    || m_hover.kind == HitKind::Lever)
		return "the panel's controls, on the boards: they work as on the panel";
	return "SN " + juce::String(m_proc.serial()).paddedLeft('0', 6) + "   wheel: zoom, drag: move";
}

} // namespace tr808
