// Roland TR-909 Rhythm Composer: the driver.
#include "machines/drivers.h"
#include "tr909/blank_artwork.h"
#include "tr909/plugin/PluginProcessor.h"

namespace lemus::drivers {

const MachineInfo &tr909()
{
	static const MachineInfo info{
		"tr909", "Roland TR-909", "Rhythm Composer drum machine (1984)", Kind::Instrument,
		// The images in MAME's tr909 set. The v4.0 program ROM is optional: without it the 909 is its
		// voicing board, played from MIDI, with no sequencer ("voice mode"). The sample ROMs are each
		// optional too: without one, its voice is silent.
		{{"tr909_v4.ic609", 8192, "b94374e2cd6b5a6c405da4fa7793c98c0a6015c4", "a9b45f30", true,
		  "voices only, played from MIDI, no sequencer", "Program ROM", "Voice mode",
		  "The sequencer program, version 4.0, in an 8 KB EPROM (IC609). Roland's copyrighted work, not included: Lemus uses your own dump.",
		  {"Sound: the same as with the ROM", "MIDI: notes; velocity sets the accent", "Controls: every knob, with automation"},
		  {"Sequencer: none (pattern and track write and play, tap, the display)", "Panel keys: no response",
		   "Sync: none (host transport, MIDI clock)", "MIDI out: none"}},
		 {"hn61256p__c43.ic69", 32768, "22a34d603e78673bb096f5e56a8971afe14d8fee", "2aaae11b", true,
		  "no hi-hat", "Hi-hat ROM", "No hi-hat",
		  "The hi-hat recording, in a 32 KB mask ROM (IC69). Roland's copyrighted work, not included: Lemus uses your own dump.",
		  {"Sound: every other voice"}, {"Hi-hats: silent (closed and open)"}},
		 {"hn61256p__c42.ic62", 32768, "bc730107b0c391f4879c9777a3e85f3e57f3aad6", "dc61cf1a", true,
		  "no crash", "Crash ROM", "No crash",
		  "The crash cymbal recording, in a 32 KB mask ROM (IC62). Roland's copyrighted work, not included: Lemus uses your own dump.",
		  {"Sound: every other voice"}, {"Crash: silent"}},
		 {"hn61256p__c44.ic54", 32768, "daa54c58c7e3ae3398f125568537ec82d5bd1dfd", "01a9b435", true,
		  "no ride", "Ride ROM", "No ride",
		  "The ride cymbal recording, in a 32 KB mask ROM (IC54). Roland's copyrighted work, not included: Lemus uses your own dump.",
		  {"Sound: every other voice"}, {"Ride: silent"}},
		 // The version 1.0 program ROM: its loader makes the factory patterns a new 909 held
		 // (core/factory.h). Lemus plays version 4.0 either way.
		 {"tr909_v1.ic609", 8192, "bcd754900574d1861034af8056574ffa77db3067", "bfb9682b", true,
		  "no factory patterns", "Factory patterns", "No factory patterns",
		  "The sequencer program, version 1.0, in an 8 KB EPROM (IC609). Its loader makes the demonstration patterns a new "
		  "TR-909 held (Bank I, patterns 1-8, track 1); Lemus plays them on version 4.0. Roland's copyrighted work, not included: Lemus uses your own dump.",
		  {"Sound and sequencer: unchanged (version 4.0)"}, {"Factory patterns: none (a new instance starts with empty memory)"}}},
		// The panel's brand marks (optional, from the user): a file per place in artwork/tr909/, each
		// the box of a logo on the panel (docs/panel.json; the tests check they match) or, for the rear
		// Roland logo, of its mark on the rear panel; or an older tr909.svg pack. Earlier releases
		// wrote the blank pack into the ROM folder: untouched copies are taken out. The panel is
		// drawn at up to 10 px/mm (2.5 px/mm, a window twice the size, a Retina screen).
		{"tr909.svg",
		 tr909::kBlankArtwork,
		 {{"maker", "roland", "Roland logo", 80.5f, 12.63f},
		  {"model", "tr909", "TR-909", 107.88f, 15.62f},
		  {"name", "rhythm_composer", "Rhythm Composer", 135.5f, 6.88f},
		  {"rear_maker", "rear_roland", "Roland logo, rear panel", 110.0f, 22.0f}},
		 10.0f},
		[](const RomSet &roms) {
			return std::make_unique<TR909Processor>(roms.find("tr909_v4.ic609"), roms.find("hn61256p__c43.ic69"),
			                                        roms.find("hn61256p__c42.ic62"), roms.find("hn61256p__c44.ic54"),
			                                        Artwork::load(roms, tr909().artwork), roms.find("tr909_v1.ic609"));
		},
		// the ten MULTI OUT jacks (TR909Processor's buses after the main one)
		{"BD", "SD", "LT", "MT", "HT", "RS", "HC", "HH", "Crash", "Ride"}};
	return info;
}

} // namespace lemus::drivers
