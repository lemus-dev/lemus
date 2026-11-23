// Oberheim Xpander: the driver.
#include "machines/drivers.h"
#include "xpander/plugin/PluginProcessor.h"

namespace lemus::drivers {

const MachineInfo &xpander()
{
	static const MachineInfo info{
		"xpander", "Oberheim Xpander", "Six-voice analog synthesizer module (1984)", Kind::Instrument,
		// MAME's xpander set: the main processor's four EPROMs (version 1.2) and the voice board's
		// two (version 1.4). The firmware does everything, so all six are needed.
		{{"exp1.2-0.u8", 8192, "4064edc2fa0bea62684c28e8e004feb8229604fe", "dc3801b1", false, {}, "Main ROM 0"},
		 {"exp1.2-1.u7", 8192, "233efc3da3a96777b10a4d6fe7e80864e9231f04", "b57f8482", false, {}, "Main ROM 1"},
		 {"exp1.2-2.u6", 8192, "798c1b28fb59819baadbf0142ecd1348e759fa70", "30f28710", false, {}, "Main ROM 2"},
		 {"exp1.2-3.u5", 8192, "5897ad0cd66cf37b8e9b277654aab4eb83d1c8ab", "0bc8335e", false, {}, "Main ROM 3"},
		 {"ca1.4-0.u912", 8192, "5ab13ccc3b75df313a447520ca54a492102b7e3b", "79f4490a", false, {}, "Voice ROM 0"},
		 {"ca1.4-1.u914", 8192, "951bbc197a0140a93bb7621a7aedbe0f4037990c", "9b7a7914", false, {}, "Voice ROM 1"},
		 // Oberheim's factory single patches 00-99 as a SysEx dump ("Factory patches 1 (XP)", the
		 // set that came with the Xpander). Optional: a new unit's battery RAM gets them through
		 // the firmware's own SysEx receive; without them its 100 single patches start blank.
		 {"FACTORY1.SYX", 39900, "b8abde65cff74424438fd371eb2261c2e70ccaa8", "f556e0e6", true,
		  "the 100 single patches blank", "Factory patches", "Blank patches",
		  "The 100 factory single patches (SYNCOM 8 to BLANK) a new unit held in its battery RAM (ICs U2-U4), "
		  "as a SysEx dump. Oberheim's work, not included: Lemus uses your own dump.",
		  {"Sound, panel and MIDI: the same", "Patches: the OBERHEIM default, and those you make and store"},
		  {"Factory patches: single patches 00-99 start blank"}}},
		// The brand marks (optional, from the user): a file per place in artwork/xpander/, each the box
		// docs/panel.json measures: the Oberheim and Xpander wordmarks at the lower left of the
		// PROGRAMMER section (the rear repeats them; the same files fill those). The panel is drawn at
		// up to 7.2 px/mm (1.8 px/mm, a window twice the size, a Retina screen).
		{"xpander.svg", nullptr,
		 {{"maker", "oberheim", "Oberheim logo", 59.2f, 10.5f}, {"name", "xpander", "Xpander logo", 78.8f, 20.3f}},
		 7.2f, "ob-poly-xp"},
		[](const RomSet &roms) {
			std::vector<const juce::MemoryBlock *> main, voice;
			for (const char *n : {"exp1.2-0.u8", "exp1.2-1.u7", "exp1.2-2.u6", "exp1.2-3.u5"})
				main.push_back(roms.find(n));
			for (const char *n : {"ca1.4-0.u912", "ca1.4-1.u914"})
				voice.push_back(roms.find(n));
			return std::make_unique<XpanderProcessor>(main, voice, Artwork::load(roms, xpander().artwork), roms.find("FACTORY1.SYX"));
		},
		{"VOICE 1", "VOICE 2", "VOICE 3", "VOICE 4", "VOICE 5", "VOICE 6"}};
	return info;
}

} // namespace lemus::drivers
