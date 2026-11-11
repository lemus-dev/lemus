// The TB-303 through Lemus: its ROM found in the ROM folder (loose, renamed, in MAME's zip; a bad
// dump refused), voice mode without it, its slots, MIDI and audio bit-identical to the machine on
// its own, sessions saved with and without the ROM, its artwork pack, and its editor inside the
// host's. The ROM comes from the private folder; without it these checks are skipped.
#include "common.h"
#include "machines/drivers.h"
#include "tb303/blank_artwork.h"
#include "tb303/core/midi.h"
#include "tb303/core/patterns.h"
#include "tb303/plugin/PanelData.h"
#include "tb303/plugin/PluginProcessor.h"

namespace {

using namespace lemus;

juce::MemoryBlock g_rom;

bool romLoaded(LemusProcessor &p)
{
	auto *tb = dynamic_cast<TB303Processor *>(p.machineProcessor());
	return tb != nullptr && tb->romLoaded();
}

// The battery-backed pattern RAM in a machine's saved state.
juce::MemoryBlock patternRam(LemusProcessor &p)
{
	juce::MemoryBlock state;
	p.machineProcessor()->getStateInformation(state);
	auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
	juce::MemoryOutputStream ram;
	if (xml != nullptr)
		juce::Base64::convertFromBase64(ram, xml->getStringAttribute("machine"));
	return juce::MemoryBlock(ram.getData(), std::min<size_t>(ram.getDataSize(), 4 + tb303::Machine::kRamNibbles));
}

void testRom()
{
	std::printf("\nTB-303 ROM\n");
	const auto &spec = drivers::tb303().roms[0];
	check(sha1Hex(g_rom.getData(), g_rom.getSize()) == spec.sha1 && crc32Hex(g_rom.getData(), g_rom.getSize()) == spec.crc32,
	      "the private ROM matches MAME's tb303 set (SHA1 " + spec.sha1.substring(0, 8) + ", CRC " + spec.crc32 + ")");
}

void testRomFolder()
{
	std::printf("\nROM folder\n");
	const RomFolder folder;
	const auto &tb303 = drivers::tb303();
	const auto missing = LemusProcessor::missingRoms(tb303);
	check(missing.size() == 1 && missing[0].name == "d650c-133.ic8" && missing[0].optional,
	      "an empty folder: the TB-303 lacks d650c-133.ic8, which is optional");
	LemusProcessor p;
	check(p.selectMachine("tb303") && p.machine()->id == "tb303" && !romLoaded(p), "and loads without it, in voice mode");
	check(p.romNote() == "Without d650c-133.ic8: voice only, played from MIDI, no sequencer.", "the About box says what it lacks");

	check(p.romsWithout().size() == 1 && p.romsWithout()[0].mode == "Voice mode" && p.romsWithout()[0].what == "CPU ROM",
	      "the bar shows voice mode and offers to add the CPU ROM");
	check(!p.reloadWithRoms(), "a rescan without the ROM leaves it as it is");
	p.machineProcessor()->getParameters()[1]->setValueNotifyingHost(0.123f);

	folder.add("my dumps/303 cpu.bin", g_rom);
	check(LemusProcessor::missingRoms(tb303).empty(), "found under another name, in a subfolder");
	check(p.reloadWithRoms() && romLoaded(p) && p.romsWithout().empty() && p.romNote().isEmpty(),
	      "a rescan with it there reloads the 303 with its CPU, in place");
	check(std::abs(p.machineProcessor()->getParameters()[1]->getValue() - 0.123f) < 1e-3 && p.slot(1).getValue() > 0.12f,
	      "and keeps the session's settings");
	LemusProcessor full;
	check(full.selectMachine("tb303") && romLoaded(full) && full.romNote().isEmpty(), "and then the 303 loads with its CPU");

	folder.clear();
	{
		juce::ZipFile::Builder zip;
		zip.addEntry(new juce::MemoryInputStream(g_rom, false), 9, "d650c-133.ic8", juce::Time::getCurrentTime());
		juce::FileOutputStream out(folder.dir.getChildFile("tb303.zip"));
		zip.writeToStream(out, nullptr);
	}
	check(LemusProcessor::missingRoms(tb303).empty(), "found inside MAME's tb303.zip");

	folder.clear();
	juce::MemoryBlock bad(g_rom);
	static_cast<uint8_t *>(bad.getData())[100] ^= 0x01;
	folder.add("d650c-133.ic8", bad);
	check(LemusProcessor::missingRoms(tb303).size() == 1, "a bad dump of the right size and name is refused");
}

void testTb303()
{
	std::printf("\nTB-303 through the host\n");
	const RomFolder folder;
	folder.add("d650c-133.ic8", g_rom);

	LemusProcessor p;
	HostSpy spy;
	p.addListener(&spy);
	check(p.selectMachine("tb303") && p.machine()->id == "tb303", "loads tb303 with its ROM in the folder");
	check(spy.infoChanges == 1, "tells the host the parameter names changed");
	const auto &mp = p.machineProcessor()->getParameters();
	check(p.slot(0).getName(64) == "Tuning" && p.slot(1).getName(64) == "Cut Off Freq" && p.slot(mp.size() - 1).getName(64) == "Full Cut Off",
	      "slots 1-" + juce::String(mp.size()) + " are the 303's controls, from Tuning to Full Cut Off");
	p.removeListener(&spy);

	// A line played from MIDI: notes with an accent and a slide (legato).
	const int len = int(kRate * 3);
	MidiEvents line;
	const int notes[] = {36, 36, 48, 39, 41, 36, 43, 46};
	for (int i = 0; i < 8; ++i) {
		const int at = i * int(kRate * 0.3);
		const bool slide = i == 3;
		line.push_back({at, juce::MidiMessage::noteOn(1, notes[i], juce::uint8(i == 2 ? 127 : 90))});
		line.push_back({at + int(kRate * (slide ? 0.35 : 0.15)), juce::MidiMessage::noteOff(1, notes[i])});
	}
	const float settings[] = {0.5f, 0.35f, 0.8f, 0.6f, 0.4f, 0.7f, 0.7f};
	for (int i = 0; i < 7; ++i)
		p.slot(i).setValueNotifyingHost(settings[i]);
	juce::AudioBuffer<float> in(2, len);
	in.clear();
	const auto viaHost = render(p, in, line);

	RomSet roms;
	roms.add("d650c-133.ic8", g_rom);
	auto alone = drivers::tb303().create(roms);
	for (int i = 0; i < 7; ++i)
		alone->getParameters()[i]->setValueNotifyingHost(settings[i]);
	const auto direct = render(*alone, in, line);
	check(peak(direct) > 0.05f, "the machine alone plays the MIDI line (peak " + juce::String(peak(direct), 2) + ")");
	check(differences(viaHost, direct) == 0, "Lemus output is bit-identical to the machine alone");

	// A session saved with the ROM, reopened without it (voice mode), saved, then reopened with
	// the ROM again.
	auto sameSlots = [&p](LemusProcessor &q) {
		bool same = true;
		for (int i = 0; i < LemusProcessor::kSlots; ++i)
			same = same && q.slot(i).getValue() == p.slot(i).getValue();
		return same;
	};
	// A pattern first, so the session's pattern RAM differs from a new 303's (all cleared).
	const auto fresh = patternRam(p);
	dynamic_cast<TB303Processor &>(*p.machineProcessor()).writePattern(0, 0, 0, tb303::parsePattern(tb303::kTestPattern));
	check(patternRam(p) != fresh, "(the session has a pattern, unlike a new 303)");
	juce::MemoryBlock saved;
	p.getStateInformation(saved);
	folder.clear();
	LemusProcessor q;
	q.setStateInformation(saved.getData(), int(saved.getSize()));
	check(q.machine() != nullptr && q.machine()->id == "tb303" && q.missingMachine().isEmpty() && !romLoaded(q),
	      "reopened without the ROM: the 303 in voice mode");
	check(sameSlots(q), "with the session's settings");
	check(differences(render(q, in, line), viaHost) == 0, "and the MIDI line sounds the same");
	check(patternRam(q) == patternRam(p), "its patterns are kept");
	juce::MemoryBlock resaved;
	q.getStateInformation(resaved);
	folder.add("d650c-133.ic8", g_rom);
	LemusProcessor r;
	r.setStateInformation(resaved.getData(), int(resaved.getSize()));
	check(romLoaded(r) && sameSlots(r) && patternRam(r) == patternRam(p),
	      "saved in voice mode and reopened with the ROM: the full 303, with its settings and patterns");
	check(differences(render(r, in, line), viaHost) == 0, "and it sounds the same");
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
	return dynamic_cast<TB303Processor &>(*p.machineProcessor()).artwork();
}

// The mark's picture, if it has one: 1 an SVG, 2 a PNG; 0 none (or no such mark).
int pictureOf(LemusProcessor &p, const char *id)
{
	const auto *m = artworkOf(p).find(id);
	return m == nullptr || m->picture == nullptr ? 0 : m->picture->isSvg() ? 1 : 2;
}

// The older single-SVG pack: still read, and its blank one no longer written.
void testArtworkPack()
{
	std::printf("\nArtwork pack (the older kind)\n");
	checkPack(drivers::tb303(), {"roland", "bass_line", "tb303", "computer_controlled"});
	const RomFolder folder;
	folder.add("d650c-133.ic8", g_rom);
	LemusProcessor plain;
	check(plain.selectMachine("tb303") && artworkOf(plain).empty(), "without artwork the 303 loads, with no marks");
	check(plain.artworkNote().startsWith("No artwork"), "and the About box says where the marks come from");
	const auto blankFile = folder.dir.getChildFile("tb303.svg");
	check(!blankFile.exists() && !folder.dir.getChildFile("artwork").exists(), "and nothing is written into the ROM folder");

	// The blank pack earlier releases wrote: its slots are the panel's logo boxes, and it goes.
	const auto blank = block(tb303::kBlankArtwork);
	check(Artwork::parse(&blank).empty(), "the blank pack draws nothing (its slots are commented out)");
	juce::StringArray lines, open;
	lines.addLines(tb303::kBlankArtwork);
	for (int i = 0; i < lines.size(); ++i)
		if (!lines[i].startsWith("<!-- Slot ") && !(lines[i] == "-->" && i > 0 && lines[i - 1] == "</g>"))
			open.add(lines[i]);
	const auto opened = block(open.joinIntoString("\n"));
	const auto slots = Artwork::parse(&opened);
	bool boxes = slots.marks().size() == 4;
	for (const auto &e : panel::elements)
		if (e.type == panel::PType::Logo) {
			const auto *mark = slots.find(juce::String(e.id).fromFirstOccurrenceOf("logo_", false, false));
			const auto b = mark != nullptr ? mark->outline.getBounds() : juce::Rectangle<float>();
			boxes = boxes && mark != nullptr && std::abs(b.getX() - e.x0) < 0.01f && std::abs(b.getY() - e.y0) < 0.01f &&
			        std::abs(b.getRight() - e.x1) < 0.01f && std::abs(b.getBottom() - e.y1) < 0.01f;
		}
	check(boxes, "uncommented, its four slots are the panel's logo boxes");
	blankFile.replaceWithText(tb303::kBlankArtwork, false, false, "\n");
	LemusProcessor cleaned;
	check(cleaned.selectMachine("tb303") && !blankFile.exists(), "an untouched blank tb303.svg an earlier release wrote is taken out when the 303 loads");
	const juce::String mine = R"svg(<svg xmlns="http://www.w3.org/2000/svg"><!-- my own, not finished --></svg>)svg";
	blankFile.replaceWithText(mine);
	LemusProcessor again;
	check(again.selectMachine("tb303") && blankFile.loadFileAsString() == mine && artworkOf(again).empty(),
	      "a tb303.svg someone changed stays, and one with no marks counts as none");
	blankFile.deleteFile();

	// Drawn by hand in an editor: a layer with a transform, a curve, a rectangle in a group.
	const juce::String drawn = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="300mm" height="142mm" viewBox="0 0 300 142">
<g id="layer1" transform="translate(10,5)">
<path id="roland" d="M 0 0 C 12 0 12 10 0 10 Z" style="fill:#000000"/>
<g id="bass_line"><rect x="200" y="10" width="40" height="8"/></g>
</g>
</svg>)svg";
	folder.add("my artwork/TB303.svg", block(drawn));
	LemusProcessor hand;
	hand.selectMachine("tb303");
	const auto *roland = artworkOf(hand).find("roland");
	const auto *bassLine = artworkOf(hand).find("bass_line");
	check(roland != nullptr && bassLine != nullptr && roland->picture == nullptr,
	      "a hand-drawn pack (any SVG with the marks' ids, any case of name) is used, as outlines");
	check(roland != nullptr && roland->outline.getBounds().getTopLeft() == juce::Point<float>(10, 5) && bassLine != nullptr &&
	          bassLine->outline.getBounds() == juce::Rectangle<float>(210, 15, 40, 8),
	      "in its own units (mm), with its transforms, curves and shapes");
	check(hand.artworkNote() == "Artwork: my artwork/TB303.svg.", "and the About box says which file it is");

	folder.clear();
	folder.add("d650c-133.ic8", g_rom);
	{
		juce::ZipFile::Builder zip;
		zip.addEntry(new juce::MemoryInputStream(block(drawn), true), 9, "artwork/marks.svg", juce::Time::getCurrentTime());
		juce::FileOutputStream out(folder.dir.getChildFile("tb303.zip"));
		zip.writeToStream(out, nullptr);
	}
	LemusProcessor zipped;
	zipped.selectMachine("tb303");
	check(artworkOf(zipped).find("roland") != nullptr, "an SVG in tb303.zip, under any name, is used");
	folder.add("tb303.svg", block(tb303::kBlankArtwork));
	LemusProcessor zippedBlank;
	zippedBlank.selectMachine("tb303");
	check(artworkOf(zippedBlank).find("roland") != nullptr, "a blank tb303.svg next to it doesn't hide it");

	const auto pack = privatePack("tb303");
	if (pack.isEmpty())
		return;
	folder.clear();
	folder.add("d650c-133.ic8", g_rom);
	folder.add("zzz/tb303.svg", pack);
	LemusProcessor traced;
	check(traced.selectMachine("tb303") && artworkOf(traced).marks().size() == 4, "the private pack gives all four marks");
	folder.add("my artwork/TB303.svg", block(drawn));
	LemusProcessor both;
	both.selectMachine("tb303");
	check(artworkOf(both).find("tb303") == nullptr && both.artworkNote() == "Artwork: my artwork/TB303.svg.",
	      "with several packs, the first by path is used");
}

// A file per place: drawn as it looks, fitted to the place.
void testArtworkPlaces()
{
	std::printf("\nArtwork places\n");
	const auto &spec = drivers::tb303().artwork;
	{
		bool same = spec.places.size() == 4 && spec.sharpPxPerMm == 16.0f;
		int logos = 0;
		for (const auto &e : panel::elements)
			if (e.type == panel::PType::Logo) {
				++logos;
				const auto id = juce::String(e.id).fromFirstOccurrenceOf("logo_", false, false);
				const auto it = std::find_if(spec.places.begin(), spec.places.end(), [&](const ArtworkPlace &p) { return p.mark == id; });
				same = same && it != spec.places.end() && std::abs(it->width - (e.x1 - e.x0)) < 0.01f && std::abs(it->height - (e.y1 - e.y0)) < 0.01f &&
				       it->label.isNotEmpty();
			}
		check(same && logos == 4, "its four places are the panel's logo boxes");
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
	{
		const auto pic = Picture::load(block(svg));
		const auto c = pic != nullptr ? pic->content() : juce::Rectangle<float>();
		check(pic != nullptr && pic->isSvg() && std::abs(c.getX() - 30) < 0.15f && std::abs(c.getY() - 50) < 0.15f && std::abs(c.getWidth() - 100) < 0.3f &&
		          std::abs(c.getHeight() - 14) < 0.3f,
		      "an SVG's picture is what it draws, without the page or an invisible frame");
		juce::Image out(juce::Image::ARGB, 120, 40, true);
		{
			juce::Graphics g(out);
			pic->drawWithin(g, {10, 10, 100, 20});
		}
		// 100 x 14 into 100 x 20: full width, centred in height (13 to 27).
		const auto at = [&](int x, int y) { return out.getPixelAt(x, y); };
		check(at(12, 20).getRed() > 150 && at(12, 20).getAlpha() == 255 && at(12, 11).getAlpha() == 0 && at(8, 20).getAlpha() == 0 &&
		          at(107, 20).getAlpha() == 255 && at(112, 20).getAlpha() == 0 && at(104, 20) == juce::Colours::white,
		      "drawn as it looks, its colours kept, fitted to the place with its proportions, centred");
	}
	{
		const auto pic = Picture::load(pngOf(200, 60, {20, 10, 160, 40}));
		check(pic != nullptr && !pic->isSvg() && pic->content() == juce::Rectangle<float>(0, 0, 160, 40),
		      "a PNG's picture leaves out its transparent border");
		const auto opaque = Picture::load(pngOf(50, 20, {}));
		check(opaque != nullptr && opaque->content() == juce::Rectangle<float>(0, 0, 50, 20), "a PNG with no transparency is all picture");
		{
			juce::Image clear(juce::Image::ARGB, 40, 40, true);
			juce::MemoryOutputStream out;
			juce::PNGImageFormat().writeImageToStream(clear, out);
			check(Picture::load(out.getMemoryBlock()) == nullptr, "a PNG that's all transparent draws nothing, so it isn't a picture");
		}
		check(Picture::load(block("<svg xmlns=\"http://www.w3.org/2000/svg\"/>")) == nullptr && Picture::load(block("not a picture")) == nullptr,
		      "an empty SVG or another file isn't a picture");
		juce::MemoryBlock big = pngOf(8, 8, {1, 1, 2, 2});
		static_cast<uint8_t *>(big.getData())[18] = 0x7f; // the header says 0x7f00.. pixels wide
		check(Picture::load(big) == nullptr, "nor is a PNG over 8192 pixels a side");
	}

	const RomFolder folder;
	folder.add("d650c-133.ic8", g_rom);
	folder.add("artwork/tb303/maker.svg", block(svg));
	folder.add("artwork/tb303/Name.PNG", pngOf(200, 60, {20, 10, 160, 40}));
	folder.add("artwork/tb303/notes.txt", block("not artwork"));
	LemusProcessor p;
	p.selectMachine("tb303");
	check(pictureOf(p, "roland") == 1 && pictureOf(p, "bass_line") == 2 && artworkOf(p).marks().size() == 2,
	      "each place takes its own file from artwork/tb303/ in the ROM folder (SVG or PNG, any case of name)");
	check(p.artworkNote() == "Artwork: 2 of 4 places from artwork/tb303/.", "and the About box says so");

	// The older pack fills the places without a file.
	const juce::String drawn = R"svg(<svg xmlns="http://www.w3.org/2000/svg"><path id="roland" d="M 0 0 L 9 0 L 9 9 Z"/><path id="tb303" d="M 1 1 L 9 1 L 9 9 Z"/></svg>)svg";
	folder.add("my artwork/tb303.svg", block(drawn));
	folder.add("artwork/tb303/model.svg", block(svg));
	LemusProcessor mixed;
	mixed.selectMachine("tb303");
	check(pictureOf(mixed, "roland") == 1 && pictureOf(mixed, "tb303") == 1 && artworkOf(mixed).marks().size() == 3,
	      "a place's own file wins over the pack's mark");
	folder.dir.getChildFile("artwork/tb303/model.svg").deleteFile();
	LemusProcessor packed;
	packed.selectMachine("tb303");
	const auto *tb = artworkOf(packed).find("tb303");
	check(tb != nullptr && tb->picture == nullptr && !tb->outline.isEmpty() &&
	          packed.artworkNote() == "Artwork: 2 of 4 places from artwork/tb303/, the others from my artwork/tb303.svg.",
	      "and places without a file of their own take the pack's outline");
	folder.dir.getChildFile("my artwork").deleteRecursively();

	// A zip of the folder, for moving a set between computers: loose files win, and it's never a pack.
	{
		juce::ZipFile::Builder zip;
		zip.addEntry(new juce::MemoryInputStream(block(drawn), true), 9, "tb303/tagline.svg", juce::Time::getCurrentTime());
		zip.addEntry(new juce::MemoryInputStream(pngOf(30, 10, {}), true), 9, "tb303/maker.png", juce::Time::getCurrentTime());
		juce::FileOutputStream out(folder.dir.getChildFile("artwork/tb303.zip"));
		zip.writeToStream(out, nullptr);
	}
	LemusProcessor zipped;
	zipped.selectMachine("tb303");
	check(pictureOf(zipped, "computer_controlled") == 1 && pictureOf(zipped, "roland") == 1 && artworkOf(zipped).find("tb303") == nullptr,
	      "artwork/tb303.zip gives the places it has, the folder's own files win, and its one SVG isn't taken for a pack");

	// Files added while it runs: the ROMs pop-up's Rescan (and opening it) reads the folder again.
	folder.clear();
	folder.add("d650c-133.ic8", g_rom);
	LemusProcessor live;
	live.selectMachine("tb303");
	std::unique_ptr<juce::AudioProcessorEditor> e(live.createEditorIfNeeded());
	auto *editor = dynamic_cast<LemusEditor *>(e.get());
	if (editor != nullptr && editor->machineEditor() != nullptr)
		editor->machineEditor()->getProperties().set("built before", true); // a new one can reuse its address
	check(editor != nullptr && findButton(*editor, "ROMs") != nullptr, "the bar has ROMs");
	const int version = live.artworkVersion();
	check(!live.reloadArtwork() && live.artworkVersion() == version, "reading the folder again with nothing new changes nothing");
	folder.add("artwork/tb303/maker.png", pngOf(200, 60, {20, 10, 160, 40}));
	check(live.reloadArtwork() && pictureOf(live, "roland") == 2 && live.artworkVersion() == version + 1, "a file added since: the machine has it");
	pump(100);
	check(editor->machineEditor() != nullptr && !editor->machineEditor()->getProperties().contains("built before"), "and the panel is built again with it");
	folder.dir.getChildFile("artwork/tb303/maker.png").deleteFile();
	folder.add("artwork/tb303/maker.svg", block(svg));
	check(live.reloadArtwork() && pictureOf(live, "roland") == 1, "a file changed: the new one");
	folder.dir.getChildFile("artwork/tb303/maker.svg").deleteFile();
	check(live.reloadArtwork() && artworkOf(live).empty() && live.artworkNote().startsWith("No artwork"), "a file taken out: the place is blank again");
	e.reset();
}

void testEditor()
{
	std::printf("\nEditor\n");
	const RomFolder folder;
	LemusProcessor p;
	{
		std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
		auto *editor = dynamic_cast<LemusEditor *>(e.get());
		check(editor != nullptr && editor->machineEditor() == nullptr, "with no machine picked, the picker shows");
		p.editorBeingDeleted(e.get()); // the window closed, as a plugin wrapper closes it
	}
	folder.add("d650c-133.ic8", g_rom);
	p.selectMachine("tb303");
	std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
	auto *editor = dynamic_cast<LemusEditor *>(e.get());
	auto *inner = editor != nullptr ? editor->machineEditor() : nullptr;
	check(inner != nullptr && editor->getWidth() == inner->getWidth() && editor->getHeight() == inner->getHeight() + LemusEditor::kBarHeight,
	      "with the 303 loaded, its editor shows below the bar");
	const auto ownPanel = [](LemusEditor *ed) {
		return ed != nullptr && ed->machineEditor() != nullptr && dynamic_cast<juce::GenericAudioProcessorEditor *>(ed->machineEditor()) == nullptr;
	};
	check(ownPanel(editor), "and it is the 303's own panel");
	// JUCE 8.0.15 forgets a processor's open editor only through editorBeingDeleted(): the
	// machine's own editor, which the window deletes, must be let go of too.
	p.editorBeingDeleted(e.get());
	e.reset();
	e.reset(p.createEditorIfNeeded());
	check(ownPanel(dynamic_cast<LemusEditor *>(e.get())), "closed and opened again, the window has the 303's own panel again");
	p.editorBeingDeleted(e.get());
	e.reset();
}

// MIDI notes to the voice's lines: accents, slides, and slides into the same pitch.
void testMidi()
{
	std::printf("\nMIDI notes\n");
	using tb303::MidiDriver;
	auto starts = [](const MidiDriver &m, unsigned before) { return m.triggers() - before; };
	{
		MidiDriver m;
		m.noteOn(36, 90);
		const unsigned t = m.triggers();
		m.noteOn(38, 110);
		m.noteOff(36);
		const auto &l = m.lines();
		check(l.gate && l.slide && l.accent && l.note == 38 && starts(m, t) == 0, "an accented note overlapping the one before slides to it");
		m.noteOff(38);
		check(!m.lines().gate, "and its note-off ends it");
	}
	{
		MidiDriver m;
		m.noteOn(36, 90);
		const unsigned t = m.triggers();
		m.noteOn(36, 110);
		m.noteOff(36);
		const auto &l = m.lines();
		check(l.gate && l.slide && l.accent && l.note == 36 && starts(m, t) == 0,
		      "overlapping the same pitch, it slides, accented, and the first note's note-off doesn't cut it");
		m.noteOff(36);
		check(!m.lines().gate, "its own note-off ends it");
	}
	{
		MidiDriver m;
		m.noteOn(36, 90);
		m.noteOn(38, 90);
		m.noteOn(36, 90);
		m.noteOff(36);
		check(m.lines().gate && m.lines().note == 36, "with the same pitch held twice, a note-off ends the older one");
		m.noteOff(36);
		check(m.lines().gate && m.lines().note == 38 && m.lines().slide, "and releasing the newest slides back to the one before");
	}
	{
		MidiDriver m;
		m.noteOn(36, 90);
		const unsigned t = m.triggers();
		m.noteOff(36);
		m.noteOn(36, 110);
		check(m.lines().gate && !m.lines().slide && starts(m, t) == 1, "touching notes of one pitch (note-off, then note-on) retrigger");
	}
	{
		MidiDriver m;
		m.setSustain(true);
		m.noteOn(36, 90);
		const unsigned t = m.triggers();
		m.noteOff(36);
		m.noteOn(36, 110);
		const auto &l = m.lines();
		check(l.gate && l.slide && l.accent && starts(m, t) == 0, "with the sustain pedal down they slide instead: an accented slide into the same note");
		m.noteOff(36);
		check(m.lines().gate, "note-offs wait while the pedal is down");
		m.setSustain(false);
		check(!m.lines().gate, "and end the note when it comes up");
	}
}

} // namespace

// A place's file named after the plugin, anywhere in the ROM folder.
void testNamedArtwork()
{
	std::printf("\nArtwork named after the plugin\n");
	check(findMachine("tb303")->artwork.plugin == "ro-mono-303", "the host knows the name the machine's plugin ships under");
	const auto svg = block("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"><rect width=\"10\" height=\"10\"/></svg>");

	const RomFolder folder;
	folder.add("d650c-133.ic8", g_rom);
	folder.add("ro-mono-303-maker.svg", svg);
	folder.add("my art/RO-MONO-303-Name.PNG", pngOf(200, 60, {20, 10, 160, 40}));
	folder.add("ro-drums-909-model.svg", svg);
	LemusProcessor p;
	p.selectMachine("tb303");
	check(pictureOf(p, "roland") == 1 && pictureOf(p, "bass_line") == 2 && artworkOf(p).marks().size() == 2,
	      "found by name anywhere in the folder, any case, and another plugin's isn't taken");
	check(p.artworkNote() == "Artwork: 2 of 4 places from your ROM folder and my art/.", "and the About box says so");

	folder.add("artwork/tb303/maker.png", pngOf(200, 60, {20, 10, 160, 40}));
	check(p.reloadArtwork() && pictureOf(p, "roland") == 2, "a file in artwork/tb303/ wins over a named one");
}

// Each plugin's own ROM folder: the rule in host/Roms.h.
void testFolderPerPlugin()
{
	std::printf("\nA ROM folder per plugin\n");
	const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("lemustest-folders", "");
	const auto a = dir.getChildFile("a"), b = dir.getChildFile("b"), c = dir.getChildFile("c");
	for (const auto &f : {a, b, c})
		f.createDirectory();
	auto fresh = [&, n = 0]() mutable { romfolder::setSettingsFile(dir.getChildFile("settings" + juce::String(++n))); };

	fresh();
	check(romfolder::location("tb303") == romfolder::defaultLocation(), "nothing chosen: the default folder");
	romfolder::setLocation("tb303", a);
	check(romfolder::location("tb303") == a && romfolder::location("tr909") == a, "chosen in one plugin, it's also the folder of a plugin that has none");
	romfolder::setLocation("tr909", b);
	check(romfolder::location("tb303") == a && romfolder::location("tr909") == b, "chosen in another, it doesn't replace the first's");
	check(romfolder::location("sh101") == b && romfolder::location() == b, "and a plugin with none follows the last folder chosen");
	romfolder::keepLocation("sh101", b);
	romfolder::setLocation("tb303", c);
	check(romfolder::location("sh101") == b, "a plugin that found something there keeps it");
	romfolder::keepLocation("tb303", a);
	check(romfolder::location("tb303") == c, "finding something doesn't replace a folder the plugin has");

	// Through the processor, with only today's setting in the file: what an update starts from.
	const auto rom = a.getChildFile("d650c-133.ic8");
	rom.replaceWithData(g_rom.getData(), g_rom.getSize());
	fresh();
	{
		auto s = settings();
		s->setValue("romFolder", a.getFullPathName());
		s->saveIfNeeded();
	}
	{
		LemusProcessor p;
		p.selectMachine("tb303");
		check(p.romsWithout().empty(), "an existing folder setting still finds the ROM");
	}
	romfolder::setLocation("tr909", b);
	check(romfolder::location("tb303") == a, "the plugin that found its ROM there stays when another plugin chooses a folder");

	fresh();
	romfolder::setLocation("tr909", b);
	{
		LemusProcessor p;
		p.selectMachine("tb303");
		check(!p.romsWithout().empty(), "a plugin that finds nothing runs without");
	}
	romfolder::setLocation("tr909", a);
	check(romfolder::location("tb303") == a, "and goes on following the last folder chosen");
	{
		LemusProcessor p;
		p.selectMachine("tb303");
		check(p.romsWithout().empty(), "so it finds the ROM there");
	}

	romfolder::setSettingsFile({});
	dir.deleteRecursively();
}

LEMUS_HOST_TEST(tb303)
{
	g_rom = privateRom("tb303", "d650c-133.ic8");
	if (g_rom.isEmpty()) {
		std::printf("\nNo TB-303 ROM in the private folder: TB-303 checks skipped.\n");
		return;
	}
	testRom();
	testRomFolder();
	testTb303();
	testMidi();
	testArtworkPack();
	testArtworkPlaces();
	testNamedArtwork();
	testFolderPerPlugin();
	testEditor();
}
