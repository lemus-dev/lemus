// Roland SH-101: the driver.
#include "machines/drivers.h"
#include "sh101/blank_artwork.h"
#include "sh101/plugin/PluginProcessor.h"

namespace lemus::drivers {

const MachineInfo &sh101()
{
	static const MachineInfo info{
		"sh101", "Roland SH-101", "Monophonic synthesizer with sequencer and arpeggiator (1982)", Kind::Instrument,
		// The TMP80C49P-6-7301 CPU's mask ROM (Joe Britt's dump; MAME has no SH-101 set). Optional:
		// without it the SH-101 is its synth board, played from MIDI, with no sequencer or arpeggio
		// ("voice mode").
		{{"tmp80c49p-6-7301.ic6", 2048, "a84bb1758582dbb800001db3663eaf3fb9d0e25d", "4d9cb13c", true,
		  "voice only, played from MIDI, no sequencer or arpeggio", "CPU ROM", "Voice mode",
		  "The sequencer, arpeggio and keyboard-scan program, in the CPU's 2 KB mask ROM (IC6). Roland's copyrighted work, not included: Lemus uses your own dump.",
		  {"Sound: the same as with the ROM",
		   "MIDI: notes with the keyboard's note priority; HOLD pedal (CC 64); pitch bend; mod wheel",
		   "Controls: every slider and switch, with automation"},
		  {"Sequencer and arpeggio: none", "Panel keys and buttons: no response", "Sync: none",
		   "MODULATOR RANDOM: holds still (the CPU steps it)"}}},
		// The panel's brand marks (optional, from the user): a file per place in artwork/sh101/, each
		// the panel's logo box (docs/panel.json; the tests check they match), or an older sh101.svg
		// pack. Earlier builds wrote the blank pack into the ROM folder: untouched copies are taken
		// out. The panel is drawn at up to 8.8 px/mm (2.2 px/mm, a window twice the size, a Retina
		// screen).
		{"sh101.svg",
		 sh101::kBlankArtwork,
		 {{"maker", "roland", "Roland logo", 63.5f, 9.7f}, {"model", "sh101", "SH-101", 87.0f, 21.4f}},
		 8.8f},
		[](const RomSet &roms) {
			return std::make_unique<SH101Processor>(roms.find("tmp80c49p-6-7301.ic6"), Artwork::load(roms, sh101().artwork));
		}};
	return info;
}

} // namespace lemus::drivers
