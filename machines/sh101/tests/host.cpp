// The SH-101 through Lemus: its ROM (optional: voice mode without it), its slots, a line played
// from MIDI bit-identical to the machine on its own, sessions saved with and without the ROM, and
// its artwork (places, and the older pack). The ROM comes from the private folder; without it these
// checks are skipped.
#include "common.h"
#include "machines/drivers.h"
#include "sh101/blank_artwork.h"
#include "sh101/plugin/HardwareView.h"
#include "sh101/plugin/PanelData.h"
#include "sh101/plugin/PluginProcessor.h"

#include <algorithm>

namespace {

using namespace lemus;

juce::MemoryBlock g_sh101Rom;

bool romLoaded(LemusProcessor &p)
{
	auto *sh = dynamic_cast<SH101Processor *>(p.machineProcessor());
	return sh != nullptr && sh->romLoaded();
}

// The battery-backed RAM (with the sequence) in a machine's saved state.
juce::MemoryBlock sequenceRam(LemusProcessor &p)
{
	juce::MemoryBlock state;
	p.machineProcessor()->getStateInformation(state);
	auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
	juce::MemoryOutputStream ram;
	if (xml != nullptr)
		juce::Base64::convertFromBase64(ram, xml->getStringAttribute("machine"));
	return juce::MemoryBlock(ram.getData(), ram.getDataSize());
}

void testSh101()
{
	std::printf("\nSH-101 through the host\n");
	const auto &info = drivers::sh101();
	const auto &spec = info.roms[0];
	check(sha1Hex(g_sh101Rom.getData(), g_sh101Rom.getSize()) == spec.sha1 && crc32Hex(g_sh101Rom.getData(), g_sh101Rom.getSize()) == spec.crc32,
	      "the private ROM is the TMP80C49P-6-7301 dump (SHA1 " + spec.sha1.substring(0, 8) + ", CRC " + spec.crc32 + ")");
	const RomFolder folder;
	const auto missing = LemusProcessor::missingRoms(info);
	check(missing.size() == 1 && missing[0].name == spec.name && missing[0].optional,
	      "an empty folder: the SH-101 lacks " + spec.name + ", which is optional");
	LemusProcessor empty;
	check(empty.selectMachine("sh101") && !romLoaded(empty), "and loads without it, in voice mode");
	check(empty.romNote() == "Without " + spec.name + ": voice only, played from MIDI, no sequencer or arpeggio.",
	      "the About box says what it lacks");
	check(empty.romsWithout().size() == 1 && empty.romsWithout()[0].mode == "Voice mode" && empty.romsWithout()[0].what == "CPU ROM",
	      "the bar shows voice mode and offers to add the CPU ROM");
	folder.add(spec.name, g_sh101Rom);
	check(empty.reloadWithRoms() && romLoaded(empty), "a rescan with the ROM there reloads it with its CPU, in place");

	LemusProcessor p;
	check(p.selectMachine("sh101") && p.machine()->id == "sh101", "loads sh101 with its ROM in the folder");
	const auto &mp = p.machineProcessor()->getParameters();
	check(mp.size() <= LemusProcessor::kSlots && p.slot(0).getName(64) == mp[0]->getName(64) && p.slot(mp.size() - 1).getName(64) == mp.getLast()->getName(64),
	      "slots 1-" + juce::String(mp.size()) + " are the SH-101's controls, from " + mp[0]->getName(64) + " to " + mp.getLast()->getName(64));

	// A line played from MIDI after the power-on mute: notes, a legato pair, pitch bend and the
	// mod lever.
	const double start = 2.0;
	const int len = int(kRate * (start + 3));
	MidiEvents line;
	const int notes[] = {48, 55, 60, 58, 53, 65, 48, 72};
	for (int i = 0; i < 8; ++i) {
		const int at = int(kRate * (start + 0.3 * i));
		line.push_back({at, juce::MidiMessage::noteOn(1, notes[i], juce::uint8(100))});
		line.push_back({at + int(kRate * (i == 3 ? 0.35 : 0.15)), juce::MidiMessage::noteOff(1, notes[i])});
	}
	line.push_back({int(kRate * (start + 1.0)), juce::MidiMessage::pitchWheel(1, 12000)});
	line.push_back({int(kRate * (start + 1.6)), juce::MidiMessage::pitchWheel(1, 8192)});
	line.push_back({int(kRate * (start + 1.8)), juce::MidiMessage::controllerEvent(1, 1, 100)});
	std::sort(line.begin(), line.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	std::vector<float> settings;
	for (int i = 0; i < 8; ++i)
		settings.push_back(0.2f + 0.08f * float(i));
	for (size_t i = 0; i < settings.size(); ++i)
		p.slot(int(i)).setValueNotifyingHost(settings[i]);
	juce::AudioBuffer<float> in(2, len);
	in.clear();
	const auto viaHost = render(p, in, line);

	RomSet roms;
	roms.add(spec.name, g_sh101Rom);
	auto alone = info.create(roms);
	for (size_t i = 0; i < settings.size(); ++i)
		alone->getParameters()[int(i)]->setValueNotifyingHost(settings[i]);
	const auto direct = render(*alone, in, line);
	check(peak(direct) > 0.01f, "the machine alone plays the MIDI line (peak " + juce::String(peak(direct), 3) + ")");
	check(differences(viaHost, direct) == 0, "Lemus output is bit-identical to the machine alone");

	// A session saved with the ROM, reopened without it (voice mode), saved, then reopened with
	// the ROM again.
	auto sameSlots = [&p](LemusProcessor &q) {
		bool same = true;
		for (int i = 0; i < LemusProcessor::kSlots; ++i)
			same = same && q.slot(i).getValue() == p.slot(i).getValue();
		return same;
	};
	// A sequence first, so the session's RAM differs from a new SH-101's (no sequence).
	const auto fresh = sequenceRam(p);
	{
		auto &sh = dynamic_cast<SH101Processor &>(*p.machineProcessor());
		sh101::Machine m;
		m.writeSequence(sh101::Machine::demoSequence());
		const auto ram = m.saveState();
		juce::MemoryBlock state;
		sh.getStateInformation(state);
		auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
		xml->setAttribute("machine", juce::Base64::toBase64(ram.data(), ram.size()));
		juce::MemoryBlock withSeq;
		juce::AudioProcessor::copyXmlToBinary(*xml, withSeq);
		sh.setStateInformation(withSeq.getData(), int(withSeq.getSize()));
	}
	check(sequenceRam(p) != fresh, "(the session has a sequence, unlike a new SH-101)");
	juce::MemoryBlock saved;
	p.getStateInformation(saved);
	folder.clear();
	LemusProcessor q;
	q.setStateInformation(saved.getData(), int(saved.getSize()));
	check(q.machine() != nullptr && q.machine()->id == "sh101" && q.missingMachine().isEmpty() && !romLoaded(q),
	      "reopened without the ROM: the SH-101 in voice mode");
	check(sameSlots(q), "with the session's settings");
	const auto voiced = render(q, in, line);
	check(peak(voiced) > 0.01f, "and MIDI plays it (peak " + juce::String(peak(voiced), 3) + ")");
	check(sequenceRam(q) == sequenceRam(p), "its sequence is kept");
	juce::MemoryBlock resaved;
	q.getStateInformation(resaved);
	folder.add(spec.name, g_sh101Rom);
	LemusProcessor r;
	r.setStateInformation(resaved.getData(), int(resaved.getSize()));
	check(romLoaded(r) && sameSlots(r) && sequenceRam(r) == sequenceRam(p),
	      "saved in voice mode and reopened with the ROM: the full SH-101, with its settings and sequence");
}

juce::MemoryBlock block(const juce::String &s)
{
	return juce::MemoryBlock(s.toRawUTF8(), s.getNumBytesAsUTF8());
}

// A PNG, filled red where `fill` says (transparent around it), or all red.
juce::MemoryBlock pngOf(int w, int h, juce::Rectangle<int> fill)
{
	juce::Image image(fill.isEmpty() ? juce::Image::RGB : juce::Image::ARGB, w, h, true);
	juce::Graphics g(image);
	g.setColour(juce::Colours::red);
	g.fillRect(fill.isEmpty() ? image.getBounds() : fill);
	juce::MemoryOutputStream out;
	juce::PNGImageFormat().writeImageToStream(image, out);
	return out.getMemoryBlock();
}

SH101Processor &sh101Of(LemusProcessor &p) { return dynamic_cast<SH101Processor &>(*p.machineProcessor()); }
const Artwork &artworkOf(LemusProcessor &p) { return sh101Of(p).artwork(); }

// The mark's picture, if it has one: 1 an SVG, 2 a PNG; 0 none (or no such mark).
int pictureOf(LemusProcessor &p, const char *id)
{
	const auto *m = artworkOf(p).find(id);
	return m == nullptr || m->picture == nullptr ? 0 : m->picture->isSvg() ? 1 : 2;
}

// The panel as the editor draws it (1x), and its colour at a point in panel mm.
juce::Image panelOf(LemusProcessor &p)
{
	sh101::HardwareView view(sh101Of(p));
	juce::Image image(juce::Image::RGB, view.getWidth(), view.getHeight(), true);
	juce::Graphics g(image);
	view.paintEntireComponent(g, true);
	return image;
}
juce::Colour colourAt(const juce::Image &panel, float x, float y)
{
	return panel.getPixelAt(int(x * sh101::HardwareView::kPxPerMm), int(y * sh101::HardwareView::kPxPerMm));
}
bool red(juce::Colour c) { return c.getRed() > 150 && c.getGreen() < 60 && c.getBlue() < 60; }

// The older single-SVG pack: still read, and its blank one no longer written.
void testArtworkPack()
{
	std::printf("\nSH-101 artwork pack (the older kind)\n");
	checkPack(drivers::sh101(), {"roland", "sh101"});
	const RomFolder folder;
	folder.add(drivers::sh101().roms[0].name, g_sh101Rom);
	LemusProcessor plain;
	check(plain.selectMachine("sh101") && artworkOf(plain).empty(), "without artwork the SH-101 loads, with no marks");
	check(!red(colourAt(panelOf(plain), 420.0f, 151.0f)), "and the panel draws none");
	const auto blankFile = folder.dir.getChildFile("sh101.svg");
	check(!blankFile.exists() && !folder.dir.getChildFile("artwork").exists(), "and nothing is written into the ROM folder");

	// The blank pack earlier builds wrote: its slots are the panel's logo boxes, and it goes.
	const auto blank = block(sh101::kBlankArtwork);
	check(Artwork::parse(&blank).empty(), "the blank pack draws nothing (its slots are commented out)");
	juce::StringArray lines, open;
	lines.addLines(sh101::kBlankArtwork);
	for (int i = 0; i < lines.size(); ++i)
		if (!lines[i].startsWith("<!-- Slot ") && !(lines[i] == "-->" && i > 0 && lines[i - 1] == "</g>"))
			open.add(lines[i]);
	const auto opened = block(open.joinIntoString("\n"));
	const auto slots = Artwork::parse(&opened);
	bool boxes = slots.marks().size() == 2;
	for (const auto &e : sh101::panel::elements)
		if (e.type == sh101::panel::PType::Logo) {
			const auto *mark = slots.find(juce::String(e.id).fromFirstOccurrenceOf("logo_", false, false));
			const auto b = mark != nullptr ? mark->outline.getBounds() : juce::Rectangle<float>();
			boxes = boxes && mark != nullptr && std::abs(b.getX() - e.box[0]) < 0.01f && std::abs(b.getY() - e.box[1]) < 0.01f &&
			        std::abs(b.getRight() - e.box[2]) < 0.01f && std::abs(b.getBottom() - e.box[3]) < 0.01f;
		}
	check(boxes, "uncommented, its two slots are the panel's logo boxes");
	blankFile.replaceWithText(sh101::kBlankArtwork, false, false, "\n");
	LemusProcessor cleaned;
	check(cleaned.selectMachine("sh101") && !blankFile.exists(), "an untouched blank sh101.svg an earlier build wrote is taken out when the SH-101 loads");

	// Drawn by hand: the Roland slot filled.
	const juce::String drawn = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="570mm" height="310mm" viewBox="0 0 570 310">
<g id="roland"><rect x="392.8" y="146" width="63.5" height="9.7"/></g>
</svg>)svg";
	folder.add("my artwork/SH101.svg", block(drawn));
	LemusProcessor hand;
	hand.selectMachine("sh101");
	const auto *roland = artworkOf(hand).find("roland");
	const auto rb = roland != nullptr ? roland->outline.getBounds() : juce::Rectangle<float>();
	check(roland != nullptr && roland->picture == nullptr && std::abs(rb.getX() - 392.8f) < 0.01f && std::abs(rb.getY() - 146.0f) < 0.01f &&
	          std::abs(rb.getWidth() - 63.5f) < 0.01f && std::abs(rb.getHeight() - 9.7f) < 0.01f,
	      "a hand-drawn pack is used, as outlines where it puts them (mm)");
	{
		const auto c = colourAt(panelOf(hand), 420.0f, 151.0f);
		check(c.getBrightness() > 0.8f && c.getSaturation() < 0.15f, "and the panel fills them in the print colour");
	}
}

// A file per place: drawn as it looks, fitted to the place.
void testArtworkPlaces()
{
	std::printf("\nSH-101 artwork places\n");
	const auto &spec = drivers::sh101().artwork;
	{
		bool same = spec.places.size() == 2 && std::abs(spec.sharpPxPerMm - 4 * sh101::HardwareView::kPxPerMm) < 0.001f;
		int logos = 0;
		for (const auto &e : sh101::panel::elements)
			if (e.type == sh101::panel::PType::Logo) {
				++logos;
				const auto id = juce::String(e.id).fromFirstOccurrenceOf("logo_", false, false);
				const auto it = std::find_if(spec.places.begin(), spec.places.end(), [&](const ArtworkPlace &q) { return q.mark == id; });
				same = same && it != spec.places.end() && std::abs(it->width - (e.box[2] - e.box[0])) < 0.01f &&
				       std::abs(it->height - (e.box[3] - e.box[1])) < 0.01f && it->label.isNotEmpty();
			}
		check(same && logos == 2, "its two places are the panel's logo boxes, sharp in the largest window on a Retina screen");
		juce::StringArray names;
		for (const auto &place : spec.places)
			names.add(place.id);
		check(names.joinIntoString(" ") == "maker model", "their files have plain names: maker, model");
	}

	// An editor's SVG: a page bigger than the mark, an invisible page frame, a white dot at the right.
	const juce::String svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="210mm" height="297mm" viewBox="0 0 210 297">
<rect x="0" y="0" width="210" height="297" fill="none"/>
<g transform="translate(20,30)"><rect x="10" y="20" width="100" height="14" fill="#c00000"/><circle cx="105" cy="27" r="3" fill="#ffffff"/></g>
</svg>)svg";
	const RomFolder folder;
	folder.add(drivers::sh101().roms[0].name, g_sh101Rom);
	folder.add("artwork/sh101/maker.svg", block(svg));
	folder.add("artwork/sh101/Model.PNG", pngOf(200, 60, {20, 10, 160, 40}));
	folder.add("artwork/sh101/notes.txt", block("not artwork"));
	LemusProcessor p;
	p.selectMachine("sh101");
	check(pictureOf(p, "roland") == 1 && pictureOf(p, "sh101") == 2 && artworkOf(p).marks().size() == 2,
	      "each place takes its own file from artwork/sh101/ in the ROM folder (SVG or PNG, any case of name)");
	check(p.artworkNote() == "Artwork: 2 of 2 places from artwork/sh101/.", "and the About box says so");
	{
		// maker: 100 x 14 into 63.5 x 9.7, full width, the white dot near the right end (453.1).
		// model: 4 to 1 into 87 x 21.4, full height, centred (462.1 to 547.7).
		const auto panel = panelOf(p);
		check(red(colourAt(panel, 400.0f, 151.0f)) && red(colourAt(panel, 445.0f, 151.0f)) && colourAt(panel, 453.1f, 151.0f).getBrightness() > 0.8f &&
		          red(colourAt(panel, 505.0f, 145.0f)) && red(colourAt(panel, 465.0f, 145.0f)) && !red(colourAt(panel, 459.0f, 145.0f)),
		      "the panel draws each as it looks, its colours kept, fitted to its place");
	}

	// The older pack fills the places without a file; a place's own file wins.
	const juce::String drawn = R"svg(<svg xmlns="http://www.w3.org/2000/svg"><path id="roland" d="M 0 0 L 9 0 L 9 9 Z"/><path id="sh101" d="M 1 1 L 9 1 L 9 9 Z"/></svg>)svg";
	folder.add("my artwork/sh101.svg", block(drawn));
	folder.dir.getChildFile("artwork/sh101/Model.PNG").deleteFile();
	LemusProcessor mixed;
	mixed.selectMachine("sh101");
	const auto *model = artworkOf(mixed).find("sh101");
	check(pictureOf(mixed, "roland") == 1 && model != nullptr && model->picture == nullptr && !model->outline.isEmpty() &&
	          mixed.artworkNote() == "Artwork: 1 of 2 places from artwork/sh101/, the others from my artwork/sh101.svg.",
	      "a place's own file wins over the pack's mark, and places without one take the pack's outline");
}

} // namespace

LEMUS_HOST_TEST(sh101)
{
	g_sh101Rom = privateRom("sh101", "tmp80c49p-6-7301.ic6");
	if (g_sh101Rom.isEmpty()) {
		std::printf("\nNo SH-101 ROM in the private folder: SH-101 checks skipped.\n");
		return;
	}
	testSh101();
	testArtworkPack();
	testArtworkPlaces();
}
