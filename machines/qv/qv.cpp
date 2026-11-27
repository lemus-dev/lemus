// Alesis QuadraVerb and QuadraVerb Plus: the driver.
#include "machines/drivers.h"
#include "qv/plugin/PluginProcessor.h"

namespace lemus::drivers {

const MachineInfo &qv()
{
	static const MachineInfo info{
		"qv", "Alesis QuadraVerb", "Multi-effects (1989; Plus 1990)", Kind::Effect,
		// The 27C512 EPROM (U19), the Plus's firmware v2.03 or else the QuadraVerb's v1.07, as read
		// from the chip (its A0-A7 are wired in reverse on the board; the machine descrambles it). The
		// Plus is the same unit with another EPROM: the firmware decides which it is. Without it there
		// is nothing to run: the effects, the programs and the panel are the firmware.
		{{"qvv203.bin", 65536, "5f97c5cb6b209643bed6ec4046662c87c317a9ee", "f1219af2", false, {}, "Firmware ROM", {}, {}, {}, {},
		  {{"qvv107.bin", 65536, "224b6a308552921e0fc6a916f900f57f6d5fa623", "5140cbc2", "v1.07"}}},
		 {"hd44780_a00.bin", 4096, "65cf075a988cdcbb316b9afdd0529b374a1a65ec", "e459877c", false, {}, "LCD font ROM"}},
		// The panel's brand marks (optional, from the user): a file per place in artwork/qv/, each the
		// box docs/panel.json measures for it. The panel is drawn at up to 10.4 px/mm.
		{"qv.svg", nullptr, {{"maker", "alesis", "Alesis logo", 24.1f, 4.2f}, {"name", "quadraverb", "QuadraVerb logo", 61.3f, 8.6f}}, 10.4f,
		 "al-fx-quad"},
		[](const RomSet &roms) { return std::make_unique<QVProcessor>(roms.find("qvv203.bin"), roms.find("hd44780_a00.bin"), Artwork::load(roms, qv().artwork)); },
		// no other outputs, no side-chain; mono in, stereo out besides stereo: the rear's R (MONO)
		// input feeds the left too with no plug in L (sheet 1, J13's switch)
		{}, false, {}, true};
	return info;
}

} // namespace lemus::drivers
