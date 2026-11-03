// Roland TB-303 Bass Line: the driver.
#include "machines/drivers.h"
#include "tb303/blank_artwork.h"
#include "tb303/plugin/PluginProcessor.h"

namespace lemus::drivers {

const MachineInfo &tb303()
{
	static const MachineInfo info{
		"tb303", "Roland TB-303", "Bass Line synthesizer and sequencer (1981)", Kind::Instrument,
		// The uPD650C-133 CPU's mask ROM, as in MAME's tb303 set. Optional: without it the 303 is
		// its voice, played from MIDI, with no sequencer ("voice mode").
		{{"d650c-133.ic8", 2048, "7a4236b2bc9a5cd4c80c63ca1a193e03becfcb4c", "268a8d8b", true,
		  "voice only, played from MIDI, no sequencer", "CPU ROM", "Voice mode",
		  "The sequencer program, in the CPU's 2 KB mask ROM (IC8). Roland's copyrighted work, not included: Lemus uses your own dump.",
		  {"Sound: the same as with the ROM", "MIDI: notes; velocity 100 or more accents; overlapping notes slide",
		   "Controls: every knob, with automation"},
		  {"Sequencer: none (pattern and track write and play, tap)", "Panel keys and RUN/STOP: no response", "Sync: none"}}},
		// The panel's brand marks (optional, from the user): a file per place in artwork/tb303/, each
		// the panel's logo box (docs/panel.json; the tests check they match), or an older tb303.svg
		// pack. Earlier releases wrote the blank pack into the ROM folder: untouched copies are taken
		// out. The panel is drawn at up to 16 px/mm (4 px/mm, a window twice the size, a Retina screen).
		{"tb303.svg",
		 tb303::kBlankArtwork,
		 {{"maker", "roland", "Roland logo", 51.75f, 7.36f},
		  {"name", "bass_line", "Bass Line", 56.72f, 7.18f},
		  {"model", "tb303", "TB-303", 28.74f, 6.08f},
		  {"tagline", "computer_controlled", "Computer Controlled", 56.54f, 6.44f}},
		 16.0f},
		[](const RomSet &roms) {
			return std::make_unique<TB303Processor>(roms.find("d650c-133.ic8"), Artwork::load(roms, tb303().artwork));
		}};
	return info;
}

} // namespace lemus::drivers
