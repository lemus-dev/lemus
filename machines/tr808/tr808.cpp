// Roland TR-808 Rhythm Composer: the driver.
#include "machines/drivers.h"
#include "tr808/blank_artwork.h"
#include "tr808/plugin/PluginProcessor.h"

namespace lemus::drivers {

const MachineInfo &tr808()
{
	static const MachineInfo info{
		"tr808", "Roland TR-808", "Rhythm Composer drum machine (1980)", Kind::Instrument,
		// The uPD650C-085 CPU's mask ROM, as in MAME's tr808 set. Optional: without it the 808 is
		// its voicing board, played from MIDI, with no sequencer ("voice mode").
		{{"d650c-085.ic4", 2048, "eddac7af6396c3f220914d9cad2c9e398872b034", "06f0c405", true,
		  "voices only, played from MIDI, no sequencer", "CPU ROM", "Voice mode",
		  "The sequencer program, in the uPD650C-085 CPU's 2 KB mask ROM (IC4). Roland's copyrighted work, not included: Lemus uses your own dump.",
		  {"Sound: the same as with the ROM", "MIDI: notes; velocity 100 or more accents", "Controls: every knob, with automation"},
		  {"Sequencer: none (pattern and track write and play, tap, the factory rhythms)", "Panel keys: no response",
		   "Sync: none (host transport)"}}},
		// The panel's brand marks (optional, from the user): a file per place in artwork/tr808/, each
		// the panel's logo box (docs/panel.json; the tests check they match), or an older tr808.svg
		// pack. Earlier versions wrote the blank pack into the ROM folder: untouched copies are taken
		// out. The panel is drawn at up to 9.6 px/mm (2.4 px/mm, a window twice the size, a Retina
		// screen).
		{"tr808.svg",
		 tr808::kBlankArtwork,
		 {{"maker", "roland", "Roland logo", 80.4f, 12.2f},
		  {"name", "rhythm_composer", "Rhythm Composer", 104.9f, 13.4f},
		  {"model", "tr808", "TR-808", 30.7f, 7.0f},
		  {"tagline", "computer_controlled", "Computer Controlled", 61.5f, 7.45f}},
		 9.6f},
		[](const RomSet &roms) {
			return std::make_unique<TR808Processor>(roms.find("d650c-085.ic4"), Artwork::load(roms, tr808().artwork));
		},
		// the eleven MULTI OUT jacks (TR808Processor's buses after the main one)
		{"BD", "SD", "LT/LC", "MT/MC", "HT/HC", "RS/CL", "CP/MA", "CB", "CY", "OH", "CH"}};
	return info;
}

} // namespace lemus::drivers
