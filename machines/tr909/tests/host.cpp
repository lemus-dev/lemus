// The TR-909 through Lemus: its ROMs found in the ROM folder (loose, renamed, in MAME's zip; a bad
// dump refused), voice mode without the program ROM, its slots, MIDI and audio bit-identical to the
// machine on its own, sessions saved with and without the ROM, its artwork pack, and its editor
// inside the host's. The ROMs come from the private folder; without them these checks are skipped.
#include "common.h"
#include "machines/drivers.h"
#include "tr909/blank_artwork.h"
#include "tr909/plugin/HardwareView.h"
#include "tr909/plugin/PluginProcessor.h"

namespace {

using namespace lemus;

juce::MemoryBlock g_fw, g_hh, g_cr, g_rd;

TR909Processor *tr909Of(LemusProcessor &p) { return dynamic_cast<TR909Processor *>(p.machineProcessor()); }
bool romLoaded(LemusProcessor &p) { return tr909Of(p) != nullptr && tr909Of(p)->romLoaded(); }

const RomSpec *spec(const char *name)
{
	for (const auto &r : drivers::tr909().roms)
		if (r.name == name)
			return &r;
	return nullptr;
}

void addAll(const RomFolder &folder)
{
	folder.add("tr909_v4.ic609", g_fw);
	folder.add("hn61256p__c43.ic69", g_hh);
	folder.add("hn61256p__c42.ic62", g_cr);
	folder.add("hn61256p__c44.ic54", g_rd);
}

// A battery RAM with a mark in it (the firmware leaves an unused byte alone).
std::vector<uint8_t> markedRam(TR909Processor &p)
{
	auto ram = p.ram();
	ram[0x1ff0] = 0xa5;
	return ram;
}

void testRoms()
{
	std::printf("\nTR-909 ROMs\n");
	bool all = true;
	for (const auto &[name, data] : {std::pair<const char *, juce::MemoryBlock *>{"tr909_v4.ic609", &g_fw}, {"hn61256p__c43.ic69", &g_hh},
	                                 {"hn61256p__c42.ic62", &g_cr}, {"hn61256p__c44.ic54", &g_rd}}) {
		const auto *s = spec(name);
		all = all && s != nullptr && sha1Hex(data->getData(), data->getSize()) == s->sha1 && crc32Hex(data->getData(), data->getSize()) == s->crc32;
	}
	check(all, "the private ROMs match MAME's tr909 set (SHA1 and CRC)");
}

void testRomFolder()
{
	std::printf("\nROM folder\n");
	const RomFolder folder;
	const auto &tr909 = drivers::tr909();
	auto missing = LemusProcessor::missingRoms(tr909);
	bool optional = missing.size() == tr909.roms.size();
	for (const auto &m : missing)
		optional = optional && m.optional;
	check(optional, "an empty folder: the TR-909 lacks every image, each of them optional");
	LemusProcessor p;
	check(p.selectMachine("tr909") && p.machine()->id == "tr909" && !romLoaded(p), "and loads without them, in voice mode");
	p.machineProcessor()->getParameters()[2]->setValueNotifyingHost(0.123f);
	folder.add("dumps/909 program.bin", g_fw);
	folder.add("hh.bin", g_hh);
	folder.add("crash.bin", g_cr);
	folder.add("ride.bin", g_rd);
	missing = LemusProcessor::missingRoms(tr909);
	check(missing.size() == 1 && missing[0].name == "tr909_v1.ic609", "found under other names, in a subfolder (all but the v1.0 ROM)");
	check(p.reloadWithRoms() && romLoaded(p), "a rescan with them there reloads the 909 with its CPU, in place");
	check(std::abs(p.machineProcessor()->getParameters()[2]->getValue() - 0.123f) < 1e-3, "and keeps the session's settings");

	folder.clear();
	{
		juce::ZipFile::Builder zip;
		zip.addEntry(new juce::MemoryInputStream(g_fw, false), 9, "tr909_v4.ic609", juce::Time::getCurrentTime());
		zip.addEntry(new juce::MemoryInputStream(g_hh, false), 9, "hn61256p__c43.ic69", juce::Time::getCurrentTime());
		zip.addEntry(new juce::MemoryInputStream(g_cr, false), 9, "hn61256p__c42.ic62", juce::Time::getCurrentTime());
		zip.addEntry(new juce::MemoryInputStream(g_rd, false), 9, "hn61256p__c44.ic54", juce::Time::getCurrentTime());
		juce::FileOutputStream out(folder.dir.getChildFile("tr909.zip"));
		zip.writeToStream(out, nullptr);
	}
	check(LemusProcessor::missingRoms(tr909).size() == 1, "found inside MAME's tr909.zip");

	folder.clear();
	juce::MemoryBlock bad(g_fw);
	static_cast<uint8_t *>(bad.getData())[100] ^= 0x01;
	folder.add("tr909_v4.ic609", bad);
	missing = LemusProcessor::missingRoms(tr909);
	bool refused = false;
	for (const auto &m : missing)
		refused = refused || m.name == "tr909_v4.ic609";
	check(refused, "a bad dump of the right size and name is refused");
}

void testTr909()
{
	std::printf("\nTR-909 through the host\n");
	const RomFolder folder;
	addAll(folder);

	LemusProcessor p;
	HostSpy spy;
	p.addListener(&spy);
	check(p.selectMachine("tr909") && p.machine()->id == "tr909" && romLoaded(p), "loads tr909 with its ROMs in the folder");
	check(spy.infoChanges == 1, "tells the host the parameter names changed");
	const auto &mp = p.machineProcessor()->getParameters();
	check(mp.size() <= LemusProcessor::kSlots && p.slot(0).getName(64) == "BD Tune" && p.slot(1).getName(64) == "BD Level",
	      "slots 1-" + juce::String(mp.size()) + " are the 909's controls, from BD Tune on");
	p.removeListener(&spy);

	// A beat from MIDI, after the firmware's power-on delay: kick, snare, hats, a clap, an accent.
	const int len = int(kRate * 3.0);
	MidiEvents beat;
	const int notes[] = {36, 42, 38, 42, 36, 46, 39, 42};
	for (int i = 0; i < 8; ++i)
		beat.push_back({int(kRate * 1.0) + i * int(kRate * 0.25), juce::MidiMessage::noteOn(10, notes[i], juce::uint8(i == 0 ? 120 : 80))});
	const float settings[] = {0.6f, 0.8f, 0.4f, 0.6f};
	for (int i = 0; i < 4; ++i)
		p.slot(i).setValueNotifyingHost(settings[i]);
	juce::AudioBuffer<float> in(2, len);
	in.clear();
	const auto viaHost = render(p, in, beat);

	RomSet roms;
	roms.add("tr909_v4.ic609", g_fw);
	roms.add("hn61256p__c43.ic69", g_hh);
	roms.add("hn61256p__c42.ic62", g_cr);
	roms.add("hn61256p__c44.ic54", g_rd);
	auto alone = drivers::tr909().create(roms);
	for (int i = 0; i < 4; ++i)
		alone->getParameters()[i]->setValueNotifyingHost(settings[i]);
	const auto direct = render(*alone, in, beat);
	check(peak(direct) > 0.05f, "the machine alone plays the beat (peak " + juce::String(peak(direct), 2) + ")");
	check(differences(viaHost, direct) == 0, "Lemus output is bit-identical to the machine alone");

	// A session saved with the ROMs and a mark in its RAM, reopened without them (voice mode),
	// saved, then reopened with them again.
	auto sameSlots = [&p](LemusProcessor &q) {
		bool same = true;
		for (int i = 0; i < LemusProcessor::kSlots; ++i)
			same = same && q.slot(i).getValue() == p.slot(i).getValue();
		return same;
	};
	const auto ram = markedRam(*tr909Of(p));
	tr909Of(p)->setRam(ram);
	juce::MemoryBlock saved;
	p.getStateInformation(saved);
	folder.clear();
	LemusProcessor q;
	q.setStateInformation(saved.getData(), int(saved.getSize()));
	check(q.machine() != nullptr && q.machine()->id == "tr909" && q.missingMachine().isEmpty() && !romLoaded(q),
	      "reopened without the ROMs: the 909 in voice mode");
	check(sameSlots(q), "with the session's settings");
	check(tr909Of(q)->ram()[0x1ff0] == 0xa5, "its memory is kept");
	juce::MemoryBlock resaved;
	q.getStateInformation(resaved);
	addAll(folder);
	LemusProcessor r;
	r.setStateInformation(resaved.getData(), int(resaved.getSize()));
	check(romLoaded(r) && sameSlots(r) && tr909Of(r)->ram()[0x1ff0] == 0xa5,
	      "saved in voice mode and reopened with the ROMs: the full 909, with its settings and memory");
}

// A PNG of w x h pixels, transparent but for an opaque `fill` rectangle.
juce::MemoryBlock pngOf(int w, int h, juce::Rectangle<int> fill)
{
	juce::Image image(juce::Image::ARGB, w, h, true);
	juce::Graphics g(image);
	g.setColour(juce::Colours::red);
	g.fillRect(fill);
	juce::MemoryOutputStream out;
	juce::PNGImageFormat().writeImageToStream(image, out);
	return out.getMemoryBlock();
}

// The mark's picture, if it has one: 1 an SVG, 2 a PNG; 0 none (or no such mark).
int pictureOf(LemusProcessor &p, const char *id)
{
	const auto *m = tr909Of(p)->artwork().find(id);
	return m == nullptr || m->picture == nullptr ? 0 : m->picture->isSvg() ? 1 : 2;
}

void testArtwork()
{
	std::printf("\nArtwork\n");
	// the older pack: the panel's three brand marks (SERVICE's copper is generated: docs/boards.md)
	checkPack(drivers::tr909(), {"roland", "tr909", "rhythm_composer"});
	auto block = [](const juce::String &s) { return juce::MemoryBlock(s.toRawUTF8(), s.getNumBytesAsUTF8()); };

	// The places: the panel's logo boxes, and the rear's Roland logo.
	const auto &spec = drivers::tr909().artwork;
	{
		bool same = spec.places.size() == 4 && spec.sharpPxPerMm == 10.0f;
		int logos = 0;
		for (const auto &e : tr909::HardwareView::elements())
			if (e.type == "logo") {
				++logos;
				const auto id = e.v["logo"].toString();
				const auto it = std::find_if(spec.places.begin(), spec.places.end(), [&](const ArtworkPlace &q) { return q.mark == id; });
				same = same && it != spec.places.end() && std::abs(it->width - e.box.getWidth()) < 0.01f &&
				       std::abs(it->height - e.box.getHeight()) < 0.01f && it->label.isNotEmpty();
			}
		const auto rear = std::find_if(spec.places.begin(), spec.places.end(), [](const ArtworkPlace &q) { return q.mark == "rear_roland"; });
		check(same && logos == 3 && rear != spec.places.end() && rear->width == 110.0f && rear->height == 22.0f,
		      "its places are the panel's three logo boxes and the rear's Roland logo (110 x 22 mm)");
		juce::StringArray names;
		for (const auto &place : spec.places)
			names.add(place.id);
		check(names.joinIntoString(" ") == "maker model name rear_maker", "their files have plain names: maker, model, name, rear_maker");
	}

	const RomFolder folder;
	addAll(folder);
	LemusProcessor plain;
	check(plain.selectMachine("tr909") && tr909Of(plain)->artwork().empty(), "without artwork the 909 loads, with no marks");
	const auto blankFile = folder.dir.getChildFile("tr909.svg");
	check(!blankFile.exists() && !folder.dir.getChildFile("artwork").exists(), "and nothing is written into the ROM folder");

	// The blank pack earlier releases wrote: its slots are the panel's logo boxes, and it goes.
	const auto blank = block(tr909::kBlankArtwork);
	check(Artwork::parse(&blank).empty(), "the blank pack draws nothing (its slots are commented out)");
	juce::StringArray lines, open;
	lines.addLines(tr909::kBlankArtwork);
	for (int i = 0; i < lines.size(); ++i)
		if (!lines[i].startsWith("<!-- Slot ") && !(lines[i] == "-->" && i > 0 && lines[i - 1] == "</g>"))
			open.add(lines[i]);
	const auto opened = block(open.joinIntoString("\n"));
	const auto slots = Artwork::parse(&opened);
	bool boxes = slots.marks().size() == 3;
	for (const auto &e : tr909::HardwareView::elements())
		if (e.type == "logo") {
			const auto *mark = slots.find(e.v["logo"].toString());
			const auto b = mark != nullptr ? mark->outline.getBounds() : juce::Rectangle<float>();
			boxes = boxes && mark != nullptr && std::abs(b.getX() - e.box.getX()) < 0.01f && std::abs(b.getY() - e.box.getY()) < 0.01f &&
			        std::abs(b.getRight() - e.box.getRight()) < 0.01f && std::abs(b.getBottom() - e.box.getBottom()) < 0.01f;
		}
	check(boxes, "uncommented, its three slots are the panel's logo boxes");
	blankFile.replaceWithText(tr909::kBlankArtwork, false, false, "\n");
	LemusProcessor cleaned;
	check(cleaned.selectMachine("tr909") && !blankFile.exists(), "an untouched blank tr909.svg an earlier release wrote is taken out when the 909 loads");

	// A file per place, drawn as it looks; the older pack fills the places without one.
	const juce::String svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="210mm" height="297mm" viewBox="0 0 210 297">
<g transform="translate(20,30)"><rect x="10" y="20" width="100" height="14" fill="#c00000"/></g>
</svg>)svg";
	folder.add("artwork/tr909/maker.svg", block(svg));
	folder.add("artwork/tr909/Rear_Maker.PNG", pngOf(200, 60, {20, 10, 160, 40}));
	const juce::String drawn = R"svg(<svg xmlns="http://www.w3.org/2000/svg"><path id="roland" d="M 0 0 L 9 0 L 9 9 Z"/><path id="tr909" d="M 1 1 L 9 1 L 9 9 Z"/></svg>)svg";
	folder.add("my artwork/tr909.svg", block(drawn));
	LemusProcessor mixed;
	mixed.selectMachine("tr909");
	const auto *model = tr909Of(mixed)->artwork().find("tr909");
	check(pictureOf(mixed, "roland") == 1 && pictureOf(mixed, "rear_roland") == 2 && model != nullptr && model->picture == nullptr &&
	          !model->outline.isEmpty() && tr909Of(mixed)->artwork().marks().size() == 3,
	      "each place takes its own file from artwork/tr909/ (the rear's Roland logo its own), the pack's outlines the rest");
	check(mixed.artworkNote() == "Artwork: 2 of 4 places from artwork/tr909/, the others from my artwork/tr909.svg.", "and the About box says so");
	{
		std::unique_ptr<juce::AudioProcessorEditor> e(mixed.createEditorIfNeeded());
		check(e != nullptr, "the panel builds with them");
	}
}

void testEditor()
{
	std::printf("\nEditor\n");
	const RomFolder folder;
	addAll(folder);
	LemusProcessor p;
	p.selectMachine("tr909");
	std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
	auto *editor = dynamic_cast<LemusEditor *>(e.get());
	auto *inner = editor != nullptr ? editor->machineEditor() : nullptr;
	check(inner != nullptr && editor->getWidth() == inner->getWidth() && editor->getHeight() == inner->getHeight() + LemusEditor::kBarHeight,
	      "with the 909 loaded, its editor shows below the bar");
	e.reset();
}

} // namespace

LEMUS_HOST_TEST(tr909)
{
	g_fw = privateRom("tr909", "tr909_v4.ic609");
	g_hh = privateRom("tr909", "hn61256p__c43.ic69");
	g_cr = privateRom("tr909", "hn61256p__c42.ic62");
	g_rd = privateRom("tr909", "hn61256p__c44.ic54");
	if (g_fw.isEmpty() || g_hh.isEmpty() || g_cr.isEmpty() || g_rd.isEmpty()) {
		std::printf("\nNo TR-909 ROMs in the private folder: TR-909 checks skipped.\n");
		return;
	}
	testRoms();
	testRomFolder();
	testTr909();
	testArtwork();
	testEditor();
}
