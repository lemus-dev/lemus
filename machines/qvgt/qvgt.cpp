// Alesis QuadraVerb GT: the driver.
#include "machines/drivers.h"
#include "qvgt/plugin/PluginProcessor.h"

namespace lemus::drivers {

const MachineInfo &qvgt()
{
	static const MachineInfo info{
		"qvgt", "Alesis QuadraVerb GT", "Guitar preamp and multi-effects (1991)", Kind::Effect,
		// The 27C512 EPROM (U30), firmware v1.03 or else v1.00, as read from the chip (its A0-A7 are
		// wired in reverse on the board; the machine descrambles it). Without it there is nothing to
		// run: the effects, the programs and the panel are the firmware.
		{{"qvgtv103.bin", 65536, "0ac063e74b6c6ecf9edda10824df9a79c621ccd9", "51813667", false, {}, "Firmware ROM", {}, {}, {}, {},
		  {{"qvgtv100.bin", 65536, "f09ee9d50425a407fa33b6ac8eb79eca660e491f", "6084d63b", "v1.00"}}},
		 {"hd44780_a00.bin", 4096, "65cf075a988cdcbb316b9afdd0529b374a1a65ec", "e459877c", false, {}, "LCD font ROM"}},
		// The panel's brand marks (optional, from the user): a file per place in artwork/qvgt/, each
		// the box docs/panel.json measures for it. The panel is drawn at up to 10.4 px/mm (2.6 px/mm,
		// a window twice the size, a Retina screen).
		{"qvgt.svg", nullptr, {{"maker", "alesis", "Alesis logo", 27.7f, 4.9f}, {"name", "quadraverb_gt", "QuadraVerb GT logo", 66.6f, 8.4f}}, 10.4f,
		 "al-fx-quadgt"},
		[](const RomSet &roms) { return std::make_unique<QVGTProcessor>(roms.find("qvgtv103.bin"), roms.find("hd44780_a00.bin"), Artwork::load(roms, qvgt().artwork)); },
		// no other outputs, no side-chain; mono in, stereo out: the owner's manual's "Mono In,
		// Stereo Out", one guitar into the cross-normalled inputs
		{}, false, {}, true};
	return info;
}

} // namespace lemus::drivers
