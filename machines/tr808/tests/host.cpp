// The TR-808 through Lemus: its ROM found in the ROM folder (loose, renamed, in MAME's zip; a bad
// dump refused), voice mode without it, its slots, MIDI and audio bit-identical to the machine on
// its own, sessions saved with and without the ROM, its artwork (a file per place, and the older
// pack), and its editor inside the host's. The ROM comes from the private folder; without it these
// checks are skipped.
#include "common.h"
#include "machines/drivers.h"
#include "tr808/blank_artwork.h"
#include "tr808/plugin/HardwareView.h"
#include "tr808/plugin/PluginProcessor.h"

namespace {

using namespace lemus;

juce::MemoryBlock g_rom;

TR808Processor *tr808Of(LemusProcessor &p) { return dynamic_cast<TR808Processor *>(p.machineProcessor()); }
bool romLoaded(LemusProcessor &p) { return tr808Of(p) != nullptr && tr808Of(p)->romLoaded(); }

// BASIC RHYTHM 1: kick, snare, closed hat.
std::vector<uint8_t> patternRam()
{
	tr808::Machine m;
	std::vector<uint16_t> steps(16, 0);
	for (int s = 0; s < 16; ++s)
		steps[size_t(s)] = uint16_t((s % 2 == 0 ? tr808::kBitCH : 0) | (s % 4 == 0 ? tr808::kBitBD : 0) | (s == 4 || s == 12 ? tr808::kBitSD : 0));
	m.writePattern(0, 0, steps, 2);
	return {m.ram().begin(), m.ram().end()};
}

// The driver declares an output for each MULTI OUT jack (the plugin's buses come from this list).
void testOutputs()
{
	std::printf("\nTR-808 outputs\n");
	const auto *machine = findMachine("tr808");
	check(machine != nullptr && int(machine->outputs.size()) == tr808::kVoices, "the driver declares an output for each MULTI OUT jack");
}

void testRom()
{
	std::printf("\nTR-808 ROM\n");
	const auto &spec = drivers::tr808().roms[0];
	check(sha1Hex(g_rom.getData(), g_rom.getSize()) == spec.sha1 && crc32Hex(g_rom.getData(), g_rom.getSize()) == spec.crc32,
	      "the private ROM matches MAME's tr808 set (SHA1 " + spec.sha1.substring(0, 8) + ", CRC " + spec.crc32 + ")");
}

void testRomFolder()
{
	std::printf("\nROM folder\n");
	const RomFolder folder;
	const auto &tr808 = drivers::tr808();
	const auto missing = LemusProcessor::missingRoms(tr808);
	check(missing.size() == 1 && missing[0].name == "d650c-085.ic4" && missing[0].optional,
	      "an empty folder: the TR-808 lacks d650c-085.ic4, which is optional");
	LemusProcessor p;
	check(p.selectMachine("tr808") && p.machine()->id == "tr808" && !romLoaded(p), "and loads without it, in voice mode");
	check(p.romNote() == "Without d650c-085.ic4: voices only, played from MIDI, no sequencer.", "the About box says what it lacks");
	check(p.romsWithout().size() == 1 && p.romsWithout()[0].mode == "Voice mode" && p.romsWithout()[0].what == "CPU ROM",
	      "the bar shows voice mode and offers to add the CPU ROM");
	p.machineProcessor()->getParameters()[2]->setValueNotifyingHost(0.123f);
	folder.add("dumps/808 cpu.bin", g_rom);
	check(LemusProcessor::missingRoms(tr808).empty(), "found under another name, in a subfolder");
	check(p.reloadWithRoms() && romLoaded(p) && p.romsWithout().empty() && p.romNote().isEmpty(),
	      "a rescan with it there reloads the 808 with its CPU, in place");
	check(std::abs(p.machineProcessor()->getParameters()[2]->getValue() - 0.123f) < 1e-3, "and keeps the session's settings");

	folder.clear();
	{
		juce::ZipFile::Builder zip;
		zip.addEntry(new juce::MemoryInputStream(g_rom, false), 9, "d650c-085.ic4", juce::Time::getCurrentTime());
		juce::FileOutputStream out(folder.dir.getChildFile("tr808.zip"));
		zip.writeToStream(out, nullptr);
	}
	check(LemusProcessor::missingRoms(tr808).empty(), "found inside MAME's tr808.zip");

	folder.clear();
	juce::MemoryBlock bad(g_rom);
	static_cast<uint8_t *>(bad.getData())[100] ^= 0x01;
	folder.add("d650c-085.ic4", bad);
	check(LemusProcessor::missingRoms(tr808).size() == 1, "a bad dump of the right size and name is refused");
}

void testTr808()
{
	std::printf("\nTR-808 through the host\n");
	const RomFolder folder;
	folder.add("d650c-085.ic4", g_rom);

	LemusProcessor p;
	HostSpy spy;
	p.addListener(&spy);
	check(p.selectMachine("tr808") && p.machine()->id == "tr808", "loads tr808 with its ROM in the folder");
	check(spy.infoChanges == 1, "tells the host the parameter names changed");
	const auto &mp = p.machineProcessor()->getParameters();
	check(mp.size() <= LemusProcessor::kSlots && p.slot(0).getName(64) == "Accent Level" && p.slot(1).getName(64) == "BD Level",
	      "slots 1-" + juce::String(mp.size()) + " are the 808's controls, from Accent Level on");
	p.removeListener(&spy);

	// A beat from MIDI: kick, snare, hats, an accent.
	const int len = int(kRate * 2.5);
	MidiEvents beat;
	const int notes[] = {36, 42, 38, 42, 36, 46, 38, 42};
	for (int i = 0; i < 8; ++i)
		beat.push_back({i * int(kRate * 0.25), juce::MidiMessage::noteOn(1, notes[i], juce::uint8(i == 0 ? 127 : 90))});
	const float settings[] = {0.9f, 0.8f, 0.4f, 0.6f};
	for (int i = 0; i < 4; ++i)
		p.slot(i).setValueNotifyingHost(settings[i]);
	juce::AudioBuffer<float> in(2, len);
	in.clear();
	const auto viaHost = render(p, in, beat);

	RomSet roms;
	roms.add("d650c-085.ic4", g_rom);
	auto alone = drivers::tr808().create(roms);
	for (int i = 0; i < 4; ++i)
		alone->getParameters()[i]->setValueNotifyingHost(settings[i]);
	const auto direct = render(*alone, in, beat);
	check(peak(direct) > 0.05f, "the machine alone plays the beat (peak " + juce::String(peak(direct), 2) + ")");
	check(differences(viaHost, direct) == 0, "Lemus output is bit-identical to the machine alone");

	// A session saved with the ROM and a rhythm in its RAM, reopened without the ROM (voice
	// mode), saved, then reopened with the ROM again.
	auto sameSlots = [&p](LemusProcessor &q) {
		bool same = true;
		for (int i = 0; i < LemusProcessor::kSlots; ++i)
			same = same && q.slot(i).getValue() == p.slot(i).getValue();
		return same;
	};
	tr808Of(p)->setRam(patternRam());
	juce::MemoryBlock saved;
	p.getStateInformation(saved);
	folder.clear();
	LemusProcessor q;
	q.setStateInformation(saved.getData(), int(saved.getSize()));
	check(q.machine() != nullptr && q.machine()->id == "tr808" && q.missingMachine().isEmpty() && !romLoaded(q),
	      "reopened without the ROM: the 808 in voice mode");
	check(sameSlots(q), "with the session's settings");
	check(differences(render(q, in, beat), viaHost) == 0, "and the beat sounds the same");
	check(tr808Of(q)->ram() == patternRam(), "its rhythms are kept");
	juce::MemoryBlock resaved;
	q.getStateInformation(resaved);
	folder.add("d650c-085.ic4", g_rom);
	LemusProcessor r;
	r.setStateInformation(resaved.getData(), int(resaved.getSize()));
	check(romLoaded(r) && sameSlots(r) && tr808Of(r)->ram() == patternRam(),
	      "saved in voice mode and reopened with the ROM: the full 808, with its settings and rhythms");
}

juce::MemoryBlock block(const juce::String &s)
{
	return juce::MemoryBlock(s.toRawUTF8(), s.getNumBytesAsUTF8());
}

// A PNG of w x h pixels, transparent but for an opaque `fill` rectangle (none: all opaque, no alpha).
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

const Artwork &artworkOf(LemusProcessor &p)
{
	return tr808Of(p)->artwork();
}

// The mark's picture, if it has one: 1 an SVG, 2 a PNG; 0 none (or no such mark).
int pictureOf(LemusProcessor &p, const char *id)
{
	const auto *m = artworkOf(p).find(id);
	return m == nullptr || m->picture == nullptr ? 0 : m->picture->isSvg() ? 1 : 2;
}

// The machine's panel, drawn at 1x as its editor draws it.
juce::Image panelOf(LemusProcessor &p)
{
	tr808::HardwareView view(*tr808Of(p));
	juce::Image image(juce::Image::RGB, view.getWidth(), view.getHeight(), true);
	juce::Graphics g(image);
	view.paintEntireComponent(g, true);
	return image;
}

// The panel's colour at a point in panel mm.
juce::Colour colourAt(const juce::Image &panel, float x, float y)
{
	return panel.getPixelAt(int(x * tr808::HardwareView::kPxPerMm), int(y * tr808::HardwareView::kPxPerMm));
}

// Red, as the test pictures are (the panel's sheen dims it a little).
bool red(juce::Colour c) { return c.getRed() > 150 && c.getGreen() < 60 && c.getBlue() < 60; }

// The orange rule under "Rhythm Composer" (panel.json's rule_rhythm_composer, left of the mark) is
// drawn: it goes with the rhythm_composer mark.
bool ruleShown(const juce::Image &panel)
{
	const auto c = colourAt(panel, 200.0f, 177.85f);
	return c.getRed() > 150 && c.getGreen() > 70 && c.getBlue() < 110;
}

// The older single-SVG pack: still read, and its blank one no longer written.
void testArtworkPack()
{
	std::printf("\nArtwork pack (the older kind)\n");
	checkPack(drivers::tr808(), {"roland", "rhythm_composer", "tr808", "computer_controlled"});
	const RomFolder folder;
	folder.add("d650c-085.ic4", g_rom);
	LemusProcessor plain;
	check(plain.selectMachine("tr808") && artworkOf(plain).empty(), "without artwork the 808 loads, with no marks");
	check(!ruleShown(panelOf(plain)), "and the panel draws none, nor the rule under Rhythm Composer");
	check(plain.artworkNote().startsWith("No artwork"), "and the About box says where the marks come from");
	const auto blankFile = folder.dir.getChildFile("tr808.svg");
	check(!blankFile.exists() && !folder.dir.getChildFile("artwork").exists(), "and nothing is written into the ROM folder");

	// The blank pack earlier versions wrote: its slots are the panel's logo boxes, and it goes.
	const auto blank = block(tr808::kBlankArtwork);
	check(Artwork::parse(&blank).empty(), "the blank pack draws nothing (its slots are commented out)");
	juce::StringArray lines, open;
	lines.addLines(tr808::kBlankArtwork);
	for (int i = 0; i < lines.size(); ++i)
		if (!lines[i].startsWith("<!-- Slot ") && !(lines[i] == "-->" && i > 0 && lines[i - 1] == "</g>"))
			open.add(lines[i]);
	const auto opened = block(open.joinIntoString("\n"));
	const auto slots = Artwork::parse(&opened);
	bool boxes = slots.marks().size() == 4;
	for (const auto &e : tr808::HardwareView::elements())
		if (e.type == "logo") {
			const auto *mark = slots.find(e.v["logo"].toString());
			const auto b = mark != nullptr ? mark->outline.getBounds() : juce::Rectangle<float>();
			boxes = boxes && mark != nullptr && std::abs(b.getX() - e.box.getX()) < 0.01f && std::abs(b.getY() - e.box.getY()) < 0.01f &&
			        std::abs(b.getRight() - e.box.getRight()) < 0.01f && std::abs(b.getBottom() - e.box.getBottom()) < 0.01f;
		}
	check(boxes, "uncommented, its four slots are the panel's logo boxes");
	blankFile.replaceWithText(tr808::kBlankArtwork, false, false, "\n");
	LemusProcessor cleaned;
	check(cleaned.selectMachine("tr808") && !blankFile.exists(), "an untouched blank tr808.svg an earlier version wrote is taken out when the 808 loads");
	const juce::String mine = R"svg(<svg xmlns="http://www.w3.org/2000/svg"><!-- my own, not finished --></svg>)svg";
	blankFile.replaceWithText(mine);
	LemusProcessor again;
	check(again.selectMachine("tr808") && blankFile.loadFileAsString() == mine && artworkOf(again).empty(),
	      "a tr808.svg someone changed stays, and one with no marks counts as none");
	blankFile.deleteFile();

	// Drawn by hand in an editor: a layer with a transform, a curve, a rectangle in a group.
	const juce::String drawn = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="508mm" height="305mm" viewBox="0 0 508 305">
<g id="layer1" transform="translate(10,5)">
<path id="roland" d="M 0 0 C 12 0 12 10 0 10 Z" style="fill:#000000"/>
<g id="rhythm_composer"><rect x="260" y="162" width="100" height="12"/></g>
</g>
</svg>)svg";
	folder.add("my artwork/TR808.svg", block(drawn));
	LemusProcessor hand;
	hand.selectMachine("tr808");
	const auto *roland = artworkOf(hand).find("roland");
	const auto *name = artworkOf(hand).find("rhythm_composer");
	check(roland != nullptr && name != nullptr && roland->picture == nullptr,
	      "a hand-drawn pack (any SVG with the marks' ids, any case of name) is used, as outlines");
	check(roland != nullptr && roland->outline.getBounds().getTopLeft() == juce::Point<float>(10, 5) && name != nullptr &&
	          name->outline.getBounds() == juce::Rectangle<float>(270, 167, 100, 12),
	      "in its own units (mm), with its transforms, curves and shapes");
	check(hand.artworkNote() == "Artwork: my artwork/TR808.svg.", "and the About box says which file it is");
	{
		const auto panel = panelOf(hand);
		const auto c = colourAt(panel, 320.0f, 173.0f);
		check(c.getRed() > 150 && c.getGreen() > 70 && c.getBlue() < 110 && ruleShown(panel),
		      "the panel fills its outlines in the print colour, with the rule under Rhythm Composer");
	}

	folder.clear();
	folder.add("d650c-085.ic4", g_rom);
	{
		juce::ZipFile::Builder zip;
		zip.addEntry(new juce::MemoryInputStream(block(drawn), true), 9, "artwork/marks.svg", juce::Time::getCurrentTime());
		juce::FileOutputStream out(folder.dir.getChildFile("tr808.zip"));
		zip.writeToStream(out, nullptr);
	}
	LemusProcessor zipped;
	zipped.selectMachine("tr808");
	check(artworkOf(zipped).find("roland") != nullptr, "an SVG in tr808.zip, under any name, is used");

	const auto pack = privatePack("tr808");
	if (pack.isEmpty())
		return;
	folder.clear();
	folder.add("d650c-085.ic4", g_rom);
	folder.add("zzz/tr808.svg", pack);
	LemusProcessor traced;
	check(traced.selectMachine("tr808") && artworkOf(traced).marks().size() == 4, "the private pack gives all four marks");
}

// A file per place: drawn as it looks, fitted to the place.
void testArtworkPlaces()
{
	std::printf("\nArtwork places\n");
	const auto &spec = drivers::tr808().artwork;
	{
		bool same = spec.places.size() == 4 && spec.sharpPxPerMm == 4 * tr808::HardwareView::kPxPerMm;
		int logos = 0;
		for (const auto &e : tr808::HardwareView::elements())
			if (e.type == "logo") {
				++logos;
				const auto id = e.v["logo"].toString();
				const auto it = std::find_if(spec.places.begin(), spec.places.end(), [&](const ArtworkPlace &q) { return q.mark == id; });
				same = same && it != spec.places.end() && std::abs(it->width - e.box.getWidth()) < 0.01f &&
				       std::abs(it->height - e.box.getHeight()) < 0.01f && it->label.isNotEmpty();
			}
		check(same && logos == 4, "its four places are the panel's logo boxes, sharp in the largest window on a Retina screen");
		juce::StringArray names;
		for (const auto &place : spec.places)
			names.add(place.id);
		check(names.joinIntoString(" ") == "maker name model tagline", "their files have plain names: maker, name, model, tagline");
	}

	// An editor's SVG: a viewBox, a page bigger than the mark, an invisible page frame.
	const juce::String svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="210mm" height="297mm" viewBox="0 0 210 297">
<rect x="0" y="0" width="210" height="297" fill="none"/>
<g transform="translate(20,30)"><rect x="10" y="20" width="100" height="14" fill="#c00000"/><circle cx="105" cy="27" r="3" fill="#ffffff"/></g>
</svg>)svg";
	const RomFolder folder;
	folder.add("d650c-085.ic4", g_rom);
	folder.add("artwork/tr808/maker.svg", block(svg));
	folder.add("artwork/tr808/Name.PNG", pngOf(200, 60, {20, 10, 160, 40}));
	folder.add("artwork/tr808/notes.txt", block("not artwork"));
	LemusProcessor p;
	p.selectMachine("tr808");
	check(pictureOf(p, "roland") == 1 && pictureOf(p, "rhythm_composer") == 2 && artworkOf(p).marks().size() == 2,
	      "each place takes its own file from artwork/tr808/ in the ROM folder (SVG or PNG, any case of name)");
	check(p.artworkNote() == "Artwork: 2 of 4 places from artwork/tr808/.", "and the About box says so");
	{
		// maker: 100 x 14 into 80.4 x 12.2, full width, its white dot near the right end (407.2).
		// name: 4 to 1 into 104.9 x 13.4, full height, centred (288 to 341.7).
		const auto panel = panelOf(p);
		check(red(colourAt(panel, 332.0f, 37.6f)) && red(colourAt(panel, 395.0f, 37.6f)) && colourAt(panel, 407.0f, 37.6f).getBrightness() > 0.8f &&
		          red(colourAt(panel, 314.85f, 172.3f)) && !red(colourAt(panel, 280.0f, 172.3f)) && !red(colourAt(panel, 350.0f, 172.3f)),
		      "the panel draws each as it looks, fitted to its place with its proportions, centred");
		check(ruleShown(panel), "and the rule under Rhythm Composer with it");
	}

	// The older pack fills the places without a file.
	const juce::String drawn = R"svg(<svg xmlns="http://www.w3.org/2000/svg"><path id="roland" d="M 0 0 L 9 0 L 9 9 Z"/><path id="tr808" d="M 1 1 L 9 1 L 9 9 Z"/></svg>)svg";
	folder.add("my artwork/tr808.svg", block(drawn));
	folder.add("artwork/tr808/model.svg", block(svg));
	LemusProcessor mixed;
	mixed.selectMachine("tr808");
	check(pictureOf(mixed, "roland") == 1 && pictureOf(mixed, "tr808") == 1 && artworkOf(mixed).marks().size() == 3,
	      "a place's own file wins over the pack's mark");
	folder.dir.getChildFile("artwork/tr808/model.svg").deleteFile();
	LemusProcessor packed;
	packed.selectMachine("tr808");
	const auto *model = artworkOf(packed).find("tr808");
	check(model != nullptr && model->picture == nullptr && !model->outline.isEmpty() &&
	          packed.artworkNote() == "Artwork: 2 of 4 places from artwork/tr808/, the others from my artwork/tr808.svg.",
	      "and places without a file of their own take the pack's outline");
	folder.dir.getChildFile("my artwork").deleteRecursively();
	folder.dir.getChildFile("artwork/tr808/Name.PNG").deleteFile();
	LemusProcessor makerOnly;
	makerOnly.selectMachine("tr808");
	check(artworkOf(makerOnly).marks().size() == 1 && !ruleShown(panelOf(makerOnly)), "without a Rhythm Composer mark, no rule under it");

	// A zip of the folder, for moving a set between computers: loose files win, and it's never a pack.
	{
		juce::ZipFile::Builder zip;
		zip.addEntry(new juce::MemoryInputStream(block(drawn), true), 9, "tr808/tagline.svg", juce::Time::getCurrentTime());
		zip.addEntry(new juce::MemoryInputStream(pngOf(30, 10, {}), true), 9, "tr808/maker.png", juce::Time::getCurrentTime());
		juce::FileOutputStream out(folder.dir.getChildFile("artwork/tr808.zip"));
		zip.writeToStream(out, nullptr);
	}
	LemusProcessor zipped;
	zipped.selectMachine("tr808");
	check(pictureOf(zipped, "computer_controlled") == 1 && pictureOf(zipped, "roland") == 1 && artworkOf(zipped).find("tr808") == nullptr,
	      "artwork/tr808.zip gives the places it has, the folder's own files win, and its one SVG isn't taken for a pack");

	// Files added while it runs: the ROMs pop-up's Rescan (and opening it) reads the folder again.
	folder.clear();
	folder.add("d650c-085.ic4", g_rom);
	LemusProcessor live;
	live.selectMachine("tr808");
	std::unique_ptr<juce::AudioProcessorEditor> e(live.createEditorIfNeeded());
	auto *editor = dynamic_cast<LemusEditor *>(e.get());
	if (editor != nullptr && editor->machineEditor() != nullptr)
		editor->machineEditor()->getProperties().set("built before", true); // a new one can reuse its address
	check(editor != nullptr && findButton(*editor, "ROMs") != nullptr, "the bar has ROMs");
	const int version = live.artworkVersion();
	check(!live.reloadArtwork() && live.artworkVersion() == version, "reading the folder again with nothing new changes nothing");
	folder.add("artwork/tr808/maker.png", pngOf(200, 60, {20, 10, 160, 40}));
	check(live.reloadArtwork() && pictureOf(live, "roland") == 2 && live.artworkVersion() == version + 1, "a file added since: the machine has it");
	pump(100);
	check(editor != nullptr && editor->machineEditor() != nullptr && !editor->machineEditor()->getProperties().contains("built before"),
	      "and the panel is built again with it");
	folder.dir.getChildFile("artwork/tr808/maker.png").deleteFile();
	folder.add("artwork/tr808/maker.svg", block(svg));
	check(live.reloadArtwork() && pictureOf(live, "roland") == 1, "a file changed: the new one");
	folder.dir.getChildFile("artwork/tr808/maker.svg").deleteFile();
	check(live.reloadArtwork() && artworkOf(live).empty() && live.artworkNote().startsWith("No artwork"), "a file taken out: the place is blank again");
	e.reset();
}

void testEditor()
{
	std::printf("\nEditor\n");
	const RomFolder folder;
	folder.add("d650c-085.ic4", g_rom);
	LemusProcessor p;
	p.selectMachine("tr808");
	std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
	auto *editor = dynamic_cast<LemusEditor *>(e.get());
	auto *inner = editor != nullptr ? editor->machineEditor() : nullptr;
	check(inner != nullptr && editor->getWidth() == inner->getWidth() && editor->getHeight() == inner->getHeight() + LemusEditor::kBarHeight,
	      "with the 808 loaded, its editor shows below the bar");
	e.reset();
}

} // namespace

LEMUS_HOST_TEST(tr808)
{
	g_rom = privateRom("tr808", "d650c-085.ic4");
	if (g_rom.isEmpty()) {
		std::printf("\nNo TR-808 ROM in the private folder: TR-808 checks skipped.\n");
		return;
	}
	testOutputs();
	testRom();
	testRomFolder();
	testTr808();
	testArtworkPack();
	testArtworkPlaces();
	testEditor();
}
