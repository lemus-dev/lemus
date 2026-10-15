// The TB-303 through Lemus: its ROM found in the ROM folder (loose, renamed, in MAME's zip; a bad
// dump refused), voice mode without it, its slots, MIDI and audio bit-identical to the machine on
// its own, sessions saved with and without the ROM, its artwork pack, and its editor inside the
// host's. The ROM comes from the private folder; without it these checks are skipped.
#include "common.h"
#include "machines/drivers.h"
#include "tb303/blank_artwork.h"
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

void testArtwork()
{
	std::printf("\nArtwork pack\n");
	checkPack(drivers::tb303(), {"roland", "bass_line", "tb303", "computer_controlled"});
	auto artworkOf = [](LemusProcessor &p) -> const Artwork & { return dynamic_cast<TB303Processor &>(*p.machineProcessor()).artwork(); };
	auto block = [](const juce::String &s) { return juce::MemoryBlock(s.toRawUTF8(), s.getNumBytesAsUTF8()); };
	const RomFolder folder;
	folder.add("d650c-133.ic8", g_rom);
	LemusProcessor plain;
	check(plain.selectMachine("tb303") && artworkOf(plain).empty(), "without a pack the 303 loads, with no marks");
	check(plain.artworkNote().startsWith("No artwork pack"), "and the About box says where the marks come from");

	// The blank pack: written into the folder, drawing nothing until its slots are filled in.
	const auto blankFile = folder.dir.getChildFile("tb303.svg");
	check(blankFile.loadFileAsString() == juce::String(tb303::kBlankArtwork), "and the blank pack is written into the ROM folder as tb303.svg");
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
	const juce::String mine = R"svg(<svg xmlns="http://www.w3.org/2000/svg"><!-- my own, not finished --></svg>)svg";
	blankFile.replaceWithText(mine);
	LemusProcessor again;
	check(again.selectMachine("tb303") && blankFile.loadFileAsString() == mine && artworkOf(again).empty(),
	      "an existing tb303.svg is never overwritten, and one with no marks counts as no pack");
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
	check(roland != nullptr && bassLine != nullptr, "a hand-drawn pack (any SVG with the marks' ids, any case of name) is used");
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
	check(!folder.dir.getChildFile("tb303.svg").exists(), "and with a pack in place, no blank one is written");
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

void testEditor()
{
	std::printf("\nEditor\n");
	const RomFolder folder;
	LemusProcessor p;
	{
		std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
		auto *editor = dynamic_cast<LemusEditor *>(e.get());
		check(editor != nullptr && editor->machineEditor() == nullptr, "with no machine picked, the picker shows");
	}
	folder.add("d650c-133.ic8", g_rom);
	p.selectMachine("tb303");
	std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
	auto *editor = dynamic_cast<LemusEditor *>(e.get());
	auto *inner = editor != nullptr ? editor->machineEditor() : nullptr;
	check(inner != nullptr && editor->getWidth() == inner->getWidth() && editor->getHeight() == inner->getHeight() + LemusEditor::kBarHeight,
	      "with the 303 loaded, its editor shows below the bar");
	e.reset();
}

} // namespace

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
	testArtwork();
	testEditor();
}
