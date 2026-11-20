// The RAT through Lemus FX: its slots, audio bit-identical to the machine on its own, sessions,
// its editor inside the host's, and its artwork places.
#include "common.h"
#include "machines/drivers.h"
#include "rat/plugin/HardwareView.h"
#include "rat/plugin/PanelData.h"
#include "rat/plugin/PluginProcessor.h"

namespace {

using namespace lemus;

void testSlots()
{
	std::printf("\nRAT in the slots\n");
	LemusProcessor p;
	HostSpy spy;
	p.addListener(&spy);
	check(p.selectMachine("rat") && p.machine() != nullptr && p.machine()->id == "rat", "loads rat");
	check(!p.selectMachine("nonesuch"), "refuses an unknown machine");
	check(spy.infoChanges == 1, "tells the host the parameter names changed");

	auto *machine = p.machineProcessor();
	const auto &mp = machine->getParameters();
	bool same = true;
	for (int i = 0; i < mp.size(); ++i)
		same = same && p.slot(i).getName(64) == mp[i]->getName(64) && p.slot(i).getValue() == mp[i]->getValue() &&
		       p.slot(i).getDefaultValue() == mp[i]->getDefaultValue() && p.slot(i).getText(0.3f, 32) == mp[i]->getText(0.3f, 32);
	check(same, "slots 1-" + juce::String(mp.size()) + " show the machine's names, values, defaults and value text");
	check(p.slot(0).getName(64) == "Distortion", "slot 1 is Distortion");
	check(p.slot(mp.size()).getName(64) == "Slot " + juce::String(mp.size() + 1), "the next slot is unused");
	check(int(spy.changes.size()) == mp.size() && spy.gestureBegins == mp.size(), "announces each slot's value once");

	spy.changes.clear();
	p.slot(0).setValueNotifyingHost(0.8f); // as the host would
	check(std::abs(mp[0]->getValue() - 0.8f) < 1e-6f, "a host change reaches the machine");
	check(spy.changes.size() == 1 && spy.changes[0].index == 0, "and is reported once, not echoed back");

	spy.changes.clear();
	spy.gestureBegins = spy.gestureEnds = 0;
	mp[1]->beginChangeGesture(); // as the machine's editor would
	mp[1]->setValueNotifyingHost(0.25f);
	mp[1]->endChangeGesture();
	check(spy.changes.size() == 1 && spy.changes[0].index == 1 && spy.changes[0].value == 0.25f, "a machine change reaches the host");
	check(spy.gestureBegins == 1 && spy.gestureEnds == 1, "with its gesture");
	check(p.slot(1).getValue() == 0.25f, "and the slot reads it");
	p.removeListener(&spy);
}

void testAudio()
{
	std::printf("\nRAT audio, host against the machine alone\n");
	const auto in = testSignal(int(kRate * 3));
	const float settings[] = {0.8f, 0.35f, 0.6f};

	LemusProcessor p;
	p.selectMachine("rat");
	for (int i = 0; i < 3; ++i)
		p.slot(i).setValueNotifyingHost(settings[i]);
	const auto viaHost = render(p, in);

	auto alone = lemus::drivers::rat().create({});
	for (int i = 0; i < 3; ++i)
		alone->getParameters()[i]->setValueNotifyingHost(settings[i]);
	const auto direct = render(*alone, in);

	check(peak(direct) > 0.01f && differences(direct, in) > 0, "the machine alone processes the signal");
	check(differences(viaHost, direct) == 0, "Lemus FX output is bit-identical to the machine alone");
}

void testState()
{
	std::printf("\nSaved state\n");
	const auto in = testSignal(int(kRate));
	LemusProcessor a;
	a.selectMachine("rat");
	a.slot(0).setValueNotifyingHost(0.9f);
	a.slot(2).setValueNotifyingHost(0.4f);
	juce::MemoryBlock saved;
	a.getStateInformation(saved);

	LemusProcessor b;
	b.setStateInformation(saved.getData(), int(saved.getSize()));
	check(b.machine() != nullptr && b.machine()->id == "rat", "a session reopens with its machine");
	bool same = true;
	for (int i = 0; i < LemusProcessor::kSlots; ++i)
		same = same && a.slot(i).getValue() == b.slot(i).getValue() && a.slot(i).getName(64) == b.slot(i).getName(64);
	check(same, "with every slot's name and value");
	check(differences(render(a, in), render(b, in)) == 0, "and sounds the same");

	// Loading a session into a used instance replaces its settings.
	b.slot(0).setValueNotifyingHost(0.1f);
	b.setStateInformation(saved.getData(), int(saved.getSize()));
	check(b.slot(0).getValue() == a.slot(0).getValue(), "reloading a session restores its values");

	LemusProcessor empty;
	juce::MemoryBlock emptySaved;
	empty.getStateInformation(emptySaved);
	LemusProcessor c;
	c.setStateInformation(emptySaved.getData(), int(emptySaved.getSize()));
	check(c.machine() == nullptr, "an empty instance reopens empty");

	// A session from a build with a machine this one lacks keeps its data.
	juce::ValueTree foreign("LEMUS");
	foreign.setProperty("version", 1, nullptr);
	foreign.setProperty("machine", "nonesuch", nullptr);
	juce::MemoryBlock blob("abc123", 6);
	foreign.setProperty("state", blob, nullptr);
	juce::MemoryBlock foreignSaved;
	{
		juce::MemoryOutputStream out(foreignSaved, false);
		foreign.writeToStream(out);
	}
	LemusProcessor d;
	d.setStateInformation(foreignSaved.getData(), int(foreignSaved.getSize()));
	juce::MemoryBlock resaved;
	d.getStateInformation(resaved);
	const auto back = juce::ValueTree::readFromData(resaved.getData(), resaved.getSize());
	const auto *backBlob = back["state"].getBinaryData();
	check(d.machine() == nullptr && d.missingMachine() == "nonesuch", "an unknown machine leaves the instance empty");
	check(back["machine"].toString() == "nonesuch" && backBlob != nullptr && *backBlob == blob, "and its state survives a save");
}

void testEditor()
{
	std::printf("\nEditor\n");
	LemusProcessor p;
	std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
	auto *editor = dynamic_cast<LemusEditor *>(e.get());
	check(editor != nullptr && editor->machineEditor() == nullptr && editor->getWidth() == LemusEditor::kPickerWidth, "an empty instance shows the picker");

	p.selectMachine("rat");
	pump();
	auto *inner = editor->machineEditor();
	check(inner != nullptr, "picking a machine shows its editor");
	if (inner == nullptr)
		return;
	check(editor->getWidth() == inner->getWidth() && editor->getHeight() == inner->getHeight() + LemusEditor::kBarHeight,
	      "sized to the machine's editor plus the bar (" + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()) +
	          ")");
	check(editor->isResizable() == inner->isResizable(), "resizable if the machine's editor is");

	inner->setSize(inner->getWidth() * 3 / 2, inner->getHeight() * 3 / 2); // its own corner, say
	check(editor->getWidth() == inner->getWidth() && editor->getHeight() == inner->getHeight() + LemusEditor::kBarHeight,
	      "follows the machine's editor when it resizes");

	auto bounds = juce::Rectangle<int>(0, 0, 1000, 300);
	editor->getConstrainer()->checkBounds(bounds, editor->getBounds(), {}, false, false, true, true);
	auto area = bounds.withTrimmedTop(LemusEditor::kBarHeight);
	auto expected = area;
	inner->getConstrainer()->checkBounds(expected, inner->getBounds(), {}, false, false, true, true);
	check(area == expected, "a host resize keeps the machine editor's limits and aspect ratio below the bar");
	e.reset();
}

juce::MemoryBlock pngOf(int w, int h)
{
	juce::Image image(juce::Image::ARGB, w, h, true);
	juce::Graphics g(image);
	g.setColour(juce::Colours::black);
	g.fillRect(juce::Rectangle<int>(w / 10, h / 10, w * 8 / 10, h * 8 / 10));
	juce::MemoryOutputStream out;
	juce::PNGImageFormat().writeImageToStream(image, out);
	return out.getMemoryBlock();
}

// A file per place: the RAT letters and the Pro Co logo in the top's white block, the Pro Co Sound
// Inc logo on the front face; each drawn as it looks, fitted to its place.
void testArtwork()
{
	std::printf("\nArtwork places\n");
	const auto &spec = drivers::rat().artwork;
	{
		using namespace ratpanel;
		const Rect boxes[] = {kPlaceRat, kPlaceProco, kPlaceFront};
		const char *marks[] = {"rat", "proco", "proco_front"};
		bool same = spec.places.size() == 3 && std::abs(spec.sharpPxPerMm - 4 * HardwareView::kPxPerMm) < 0.001f;
		for (size_t i = 0; same && i < 3; ++i)
			same = spec.places[i].mark == marks[i] && std::abs(spec.places[i].width - boxes[i].w()) < 0.01f &&
			       std::abs(spec.places[i].height - boxes[i].h()) < 0.01f;
		check(same, "its three places are the panel's boxes, sharp in the largest window on a Retina screen");
		check(spec.places[0].id == "model" && spec.places[1].id == "maker" && spec.places[2].id == "front",
		      "named model, maker and front, with no brand in the names");
	}
	auto artOf = [](LemusProcessor &p) -> const Artwork & { return dynamic_cast<RatProcessor &>(*p.machineProcessor()).artwork(); };
	const RomFolder folder;
	LemusProcessor plain;
	check(plain.selectMachine("rat") && artOf(plain).empty(), "without artwork the RAT loads, with no marks");
	check(!folder.dir.getChildFile("artwork").exists() && !folder.dir.getChildFile("rat.svg").exists(), "and nothing is written into the ROM folder");

	folder.add("artwork/rat/model.png", pngOf(400, 100));
	folder.add("artwork/rat/Maker.PNG", pngOf(120, 100));
	folder.add("artwork/rat/front.png", pngOf(300, 240));
	LemusProcessor p;
	check(p.selectMachine("rat") && artOf(p).marks().size() == 3 && artOf(p).find("rat") != nullptr && artOf(p).find("proco") != nullptr &&
	          artOf(p).find("proco_front") != nullptr && artOf(p).find("rat")->picture != nullptr,
	      "each place takes its own file from artwork/rat/ in the ROM folder");

	// The private place files, traced from photos of a 1984 unit (not in the repo), if there.
	const juce::File priv(juce::String(LEMUS_PRIVATE_DIR) + "/roms/rat/artwork/rat");
	if (priv.isDirectory()) {
		const RomFolder own;
		for (const auto &f : priv.findChildFiles(juce::File::findFiles, false, "*.png"))
			own.add("artwork/rat/" + f.getFileName(), [&] { juce::MemoryBlock b; f.loadFileAsData(b); return b; }());
		LemusProcessor q;
		check(q.selectMachine("rat") && artOf(q).marks().size() == 3, "the private place files fill all three places");
		// RAT_VIEW_PNG=out.png: the view with them, for a look.
		if (const auto out = juce::SystemStats::getEnvironmentVariable("RAT_VIEW_PNG", {}); out.isNotEmpty()) {
			HardwareView view(dynamic_cast<RatProcessor &>(*q.machineProcessor()));
			view.setBounds(0, 0, HardwareView::kWidth, HardwareView::kHeight);
			const auto img = view.createComponentSnapshot(view.getLocalBounds(), true, 2.0f);
			juce::File f(out);
			f.deleteFile();
			juce::FileOutputStream s(f);
			juce::PNGImageFormat().writeImageToStream(img, s);
		}
	}
}

} // namespace

LEMUS_HOST_TEST(rat)
{
	testSlots();
	testAudio();
	testState();
	testArtwork();
	testEditor();
}
