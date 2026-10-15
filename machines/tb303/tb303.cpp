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
		  "The TB-303's sequencer is a program in its CPU's 2 KB mask ROM. It's Roland's copyrighted work, so Lemus doesn't "
		  "include it: it uses your own dump.",
		  {"The complete voice: the same sound as with the ROM", "MIDI notes: velocity 100 or more accents, overlapping notes slide",
		   "Every knob, with automation"},
		  {"No sequencer: pattern write, track and tap need the ROM", "The panel's keys and RUN/STOP don't respond"}}},
		// The panel's brand marks (optional, from the user): roland, bass_line, tb303,
		// computer_controlled, in panel mm. The blank pack has their slots, commented out.
		{"tb303.svg", tb303::kBlankArtwork},
		[](const RomSet &roms) { return std::make_unique<TB303Processor>(roms.find("d650c-133.ic8"), roms.find("tb303.svg")); }};
	return info;
}

} // namespace lemus::drivers
