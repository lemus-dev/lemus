// Korg MS-20: the driver. Circuit only, no ROM.
#include "machines/drivers.h"
#include "ms20/plugin/PluginProcessor.h"

namespace lemus::drivers {

const MachineInfo &ms20()
{
	static const MachineInfo info{
		"ms20", "Korg MS-20", "Monophonic synthesizer (1978)", Kind::Instrument,
		// No ROM: there is no CPU (docs/research.md §3).
		{},
		// The brand mark (optional, from the user): the KORG logotype printed on the panel, bottom
		// right, its box as docs/panel.json measures it. There is no model name on the front. The
		// panel is drawn at up to 9.6 px/mm (2.4 px/mm, a window twice the size, a Retina screen).
		{"ms20.svg", nullptr, {{"maker", "korg", "KORG logo", 38.99f, 12.31f}}, 9.6f, "ko-mono-20"},
		[](const RomSet &roms) { return std::make_unique<Ms20Processor>(Artwork::load(roms, ms20().artwork)); },
		{},
		false,
		"SIGNAL IN"};
	return info;
}

} // namespace lemus::drivers
